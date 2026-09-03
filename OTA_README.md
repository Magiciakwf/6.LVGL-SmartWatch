# STM32F411 + OneNET A/B OTA 使用说明

## 1. 实现结构

STM32F411 不能从普通 SPI NOR 直接执行代码，因此 W25Q64 中的 A/B 是镜像槽，
实际运行镜像由 Bootloader 安装到片内 Flash 的 APP 区。

| 存储器 | 地址 | 用途 |
|---|---:|---|
| 片内 Flash | `0x08000000-0x0800FFFF` | Bootloader，64 KiB |
| 片内 Flash | `0x08010000-0x0807FFFF` | 当前运行 APP，最大 448 KiB |
| W25Q64 | `0x000000-0x0FFFFF` | A 镜像槽，1 MiB |
| W25Q64 | `0x100000-0x1FFFFF` | B 镜像槽，1 MiB |
| W25Q64 | `0x200000-0x7FFFFF` | 保留给业务数据 |
| AT24C02 | `0x00-0x3F` | 原工程参数区 |
| AT24C02 | `0x40-0x9F` | OTA 元数据副本 0 |
| AT24C02 | `0xA0-0xFF` | OTA 元数据副本 1 |

AT24C02 元数据带序列号和 CRC32，采用双副本交替提交，掉电时至少保留一份有效记录。

默认新增接线定义在 `OTA_Common/ota_config.h`：

- W25Q64：`SCK=PA5`、`MISO=PA6`、`MOSI=PA7`、`CS=PB10`；
- AT24C02：`SCL=PA12`、`SDA=PA11`，与原 `BL24C02` 驱动一致。

W25Q64 使用独立软件 SPI，避免与 ST7789 的 SPI1 以及 PB4 复位脚冲突。PCB 引脚不同
时只需覆盖 `OTA_W25_*` / `OTA_AT24_*` 宏，无需改状态机。

## 2. 状态机

`IDLE -> DOWNLOADING -> PENDING -> reset -> INSTALLING -> TESTING`

- APP 从 OneNET 按 Range 分片下载完整 `.ota` 包到非活动槽；每 4 KiB 在 AT24C02
  保存一次断点，复位后可从已提交偏移继续。
- 下载完成验证本地镜像头中的 CRC32，再将镜像标记为待安装。
- Bootloader 验证密文 CRC32、解密后的明文 CRC32 和向量表，验证通过后才擦除
  片内 APP。
- 第一次升级时，Bootloader 会先把当前片内 APP 备份到另一个 W25Q64 槽。
- 新 APP 完成硬件、LVGL 初始化后调用 `ota_app_confirm_running_image()`，状态经过
  `CONFIRMED` 回到 `IDLE`。
- TESTING 连续 3 次启动仍未确认、镜像安装失败或安装时掉电，会进入 `ROLLBACK` 并恢复
  上一个槽。

## 3. AES 镜像打包

先用 Keil 构建 APP，工程会在 `OV_Watch/MDK-ARM/output` 生成从 `0x08010000`
链接的 `OV_Watch.bin`。然后执行：

```powershell
python tools/ota_pack.py `
  OV_Watch/MDK-ARM/output/OV_Watch.bin `
  OV_Watch/MDK-ARM/output/OV_Watch_v2.4.4.ota `
  --version 2.4.4 `
  --key-hex 2b7e151628aed2a6abf7158809cf4f3c
```

脚本输出的 `package_size` 应与 OneNET 检查升级任务接口返回值一致。
上传到 OneNET 的是 `.ota`，不是原始 `.bin`。

镜像格式为 256 字节头 + AES-128-CTR 密文，头中保存明文/密文 CRC32、
随机 IV、加载地址、固件版本和 Key ID。默认密钥只用于联调；量产前必须同时替换
`OTA_Common/ota_config.h` 和打包命令中的密钥，并避免把量产密钥提交到仓库。

## 4. OneNET 对接点

原工程只有 UART/BLE，没有 TCP/IP、HTTP 客户端或具体的 4G/Wi-Fi/NB 模组型号，因此
网络相关代码不能安全地假设某组 AT 指令。`OV_Watch/User/OTA/ota_onenet_port.c` 提供了
三个弱函数，接入实际模组时覆盖它们：

```c
int onenet_ota_port_check(uint32_t current_version, onenet_ota_job_t *job);
int onenet_ota_port_download_range(const onenet_ota_job_t *job,
                                   uint32_t offset, uint8_t *buffer,
                                   uint32_t capacity, uint32_t *received);
void onenet_ota_port_report(const onenet_ota_job_t *job, ota_state_t state,
                            uint8_t progress, ota_error_t error);
```

适配规则：

1. `check` 上报/携带当前版本并调用 OneNET 检查升级任务接口，填入任务 ID、目标版本、
   整包大小和下载 token；无任务返回 `ONENET_OTA_NO_UPDATE`。
2. `download_range` 使用 `offset` 和 `capacity` 发 HTTP Range 请求，返回的数据必须连续，
   不得自行跳过或插入 HTTP 头。
3. `report` 把下载百分比、成功、确认或失败状态映射到 OneNET 的任务状态接口。

通用 OneNET OTA 使用 `ota.heclouds.com` 的上报版本、检查任务、分片下载和进度上报接口；
OneNET Studio 则使用 `studio-ota.heclouds.com/ota/{pro_id}/{dev_name}/...`。两者鉴权均由
网络适配层生成 `Authorization`，不要把产品密钥写入 OTA 状态机。

官方说明：<https://iot.10086.cn/doc/book/device-develop/OTA/manual/ota_develop_document.html>

## 5. 首次烧录和验证

1. 先烧录 `bootloader/MDK-ARM/bootloader/bootloader.hex`。
2. 再烧录 `OV_Watch/MDK-ARM/output/OV_Watch.hex`；该 HEX 已从 `0x08010000` 开始。
3. 确认 W25Q64 JEDEC ID 为 `EF xx 17`，并核对实际接线宏。
4. 先在 OneNET 创建单台验证升级，不要直接批量下发。
5. 测试下载中断续传、安装时断电、TESTING 不确认三次以及正常确认四条路径。

Bootloader 和 APP 均已在 ARMCC 5.06 下编译通过。AES、CRC32、元数据掉电副本另有
`tests/test_ota_crypto.c` 和 `tests/test_ota_storage.c` 主机测试。
