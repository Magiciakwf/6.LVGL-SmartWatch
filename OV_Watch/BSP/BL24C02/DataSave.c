#include "DataSave.h"
#include <string.h>

/*
 * AT24C02 layout (256 bytes):
 *   0x00..0x1F  user data slot A
 *   0x20..0x3F  user data slot B
 *   0x40..0x9F  OTA metadata copy 0
 *   0xA0..0xFF  OTA metadata copy 1
 *
 * User record (24 bytes, serialized explicitly to avoid compiler padding):
 *   0..3   magic "USR1"
 *   4      format version
 *   5      record size
 *   6..9   monotonically increasing sequence, little endian
 *   10     flags: bit0 wrist wake, bit1 APP time sync
 *   11..13 year, month, day
 *   14..15 steps, little endian
 *   16..20 reserved (included in CRC)
 *   21..22 CRC16-CCITT over bytes 0..20
 *   23     commit marker, written last
 */

#define USER_DATA_MAGIC_0          0x55U
#define USER_DATA_MAGIC_1          0x53U
#define USER_DATA_MAGIC_2          0x52U
#define USER_DATA_MAGIC_3          0x31U
#define USER_DATA_FORMAT_VERSION   1U
#define USER_DATA_COMMIT_MARKER    0xA5U
#define USER_DATA_INVALID_MARKER   0x00U
#define USER_DATA_FLAG_WRIST       (1U << 0)
#define USER_DATA_FLAG_APP_SYNC    (1U << 1)
#define USER_DATA_FLAG_MASK        (USER_DATA_FLAG_WRIST | USER_DATA_FLAG_APP_SYNC)

#define RECORD_OFFSET_VERSION      4U
#define RECORD_OFFSET_SIZE         5U
#define RECORD_OFFSET_SEQUENCE     6U
#define RECORD_OFFSET_FLAGS        10U
#define RECORD_OFFSET_YEAR         11U
#define RECORD_OFFSET_MONTH        12U
#define RECORD_OFFSET_DAY          13U
#define RECORD_OFFSET_STEPS        14U
#define RECORD_OFFSET_CRC          21U
#define RECORD_OFFSET_COMMIT       23U
#define RECORD_CRC_LENGTH          21U

/* Legacy layout, used only for one-time migration. */
#define LEGACY_CHECK_ADDRESS       0x00U
#define LEGACY_SETTINGS_ADDRESS    0x10U
#define LEGACY_STEPS_ADDRESS       0x20U

#if USER_DATA_RECORD_SIZE > USER_DATA_SLOT_SIZE
#error "User data record does not fit in its EEPROM slot"
#endif

static uint32_t active_sequence;
static uint8_t active_slot = 0xFFU;

static uint16_t crc16_ccitt(const uint8_t *data, uint8_t length)
{
	uint16_t crc = 0xFFFFU;
	uint8_t i;

	while(length-- > 0U)
	{
		crc ^= (uint16_t)(*data++) << 8;
		for(i = 0U; i < 8U; ++i)
			crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U)
									 : (uint16_t)(crc << 1);
	}

	return crc;
}

static void put_u16_le(uint8_t *dst, uint16_t value)
{
	dst[0] = (uint8_t)value;
	dst[1] = (uint8_t)(value >> 8);
}

static uint16_t get_u16_le(const uint8_t *src)
{
	return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static void put_u32_le(uint8_t *dst, uint32_t value)
{
	dst[0] = (uint8_t)value;
	dst[1] = (uint8_t)(value >> 8);
	dst[2] = (uint8_t)(value >> 16);
	dst[3] = (uint8_t)(value >> 24);
}

static uint32_t get_u32_le(const uint8_t *src)
{
	return (uint32_t)src[0] |
		  ((uint32_t)src[1] << 8) |
		  ((uint32_t)src[2] << 16) |
		  ((uint32_t)src[3] << 24);
}

static uint8_t leap_year(uint8_t year)
{
	/* RTC year is interpreted as 2000..2099; divisibility by four is enough. */
	return (uint8_t)((year & 3U) == 0U);
}

static uint8_t date_valid(uint8_t year, uint8_t month, uint8_t day)
{
	static const uint8_t days_in_month[12] =
		{31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};
	uint8_t max_day;

	if(month < 1U || month > 12U)
		return 0U;

	max_day = days_in_month[month - 1U];
	if(month == 2U && leap_year(year))
		max_day = 29U;

	return (uint8_t)(day >= 1U && day <= max_day);
}

static uint8_t user_data_valid(const EEPROM_UserData_t *data)
{
	return (uint8_t)(data != NULL &&
					 data->wrist_enabled <= 1U &&
					 data->app_sync_enabled <= 1U &&
					 date_valid(data->year, data->month, data->day));
}

static void record_encode(uint8_t record[USER_DATA_RECORD_SIZE],
						  const EEPROM_UserData_t *data, uint32_t sequence)
{
	uint8_t flags = 0U;
	uint16_t crc;

	memset(record, 0, USER_DATA_RECORD_SIZE);
	record[0] = USER_DATA_MAGIC_0;
	record[1] = USER_DATA_MAGIC_1;
	record[2] = USER_DATA_MAGIC_2;
	record[3] = USER_DATA_MAGIC_3;
	record[RECORD_OFFSET_VERSION] = USER_DATA_FORMAT_VERSION;
	record[RECORD_OFFSET_SIZE] = USER_DATA_RECORD_SIZE;
	put_u32_le(&record[RECORD_OFFSET_SEQUENCE], sequence);
	if(data->wrist_enabled)
		flags |= USER_DATA_FLAG_WRIST;
	if(data->app_sync_enabled)
		flags |= USER_DATA_FLAG_APP_SYNC;
	record[RECORD_OFFSET_FLAGS] = flags;
	record[RECORD_OFFSET_YEAR] = data->year;
	record[RECORD_OFFSET_MONTH] = data->month;
	record[RECORD_OFFSET_DAY] = data->day;
	put_u16_le(&record[RECORD_OFFSET_STEPS], data->steps);
	crc = crc16_ccitt(record, RECORD_CRC_LENGTH);
	put_u16_le(&record[RECORD_OFFSET_CRC], crc);
	record[RECORD_OFFSET_COMMIT] = USER_DATA_INVALID_MARKER;
}

static uint8_t record_decode(const uint8_t record[USER_DATA_RECORD_SIZE],
						  EEPROM_UserData_t *data, uint32_t *sequence)
{
	uint8_t flags;
	//Magic_num = USR1
	//CRC16校验:计算0-20字节的CRC16
	if(record[0] != USER_DATA_MAGIC_0 ||
	   record[1] != USER_DATA_MAGIC_1 ||
	   record[2] != USER_DATA_MAGIC_2 ||
	   record[3] != USER_DATA_MAGIC_3 ||
	   record[RECORD_OFFSET_VERSION] != USER_DATA_FORMAT_VERSION ||
	   record[RECORD_OFFSET_SIZE] != USER_DATA_RECORD_SIZE ||
	   record[RECORD_OFFSET_COMMIT] != USER_DATA_COMMIT_MARKER ||
	   get_u16_le(&record[RECORD_OFFSET_CRC]) != crc16_ccitt(record, RECORD_CRC_LENGTH))
		return 0U;

	flags = record[RECORD_OFFSET_FLAGS];
	//检测未知标志位
	if((flags & (uint8_t)~USER_DATA_FLAG_MASK) != 0U)
		return 0U;
	//解析抬腕，app同步标志位，解析年月日和步数，序列号
	data->wrist_enabled = (flags & USER_DATA_FLAG_WRIST) ? 1U : 0U;
	data->app_sync_enabled = (flags & USER_DATA_FLAG_APP_SYNC) ? 1U : 0U;
	data->year = record[RECORD_OFFSET_YEAR];
	data->month = record[RECORD_OFFSET_MONTH];
	data->day = record[RECORD_OFFSET_DAY];
	data->steps = get_u16_le(&record[RECORD_OFFSET_STEPS]);
	if(!user_data_valid(data))
		return 0U;

	*sequence = get_u32_le(&record[RECORD_OFFSET_SEQUENCE]);
	return 1U;
}

static uint8_t sequence_newer(uint32_t first, uint32_t second)
{
	return (uint8_t)((int32_t)(first - second) > 0);
}

static uint8_t legacy_load(EEPROM_UserData_t *data,
						   uint8_t now_year, uint8_t now_month, uint8_t now_day)
{
	uint8_t check[2];
	uint8_t settings[2];
	uint8_t step_data[3];

	if(BL24C02_ReadSafe(LEGACY_CHECK_ADDRESS, sizeof(check), check) ||
	   check[0] != 0x55U || check[1] != 0xAAU)
		return 0U;

	data->wrist_enabled = 0U;
	data->app_sync_enabled = 0U;
	data->year = now_year;
	data->month = now_month;
	data->day = now_day;
	data->steps = 0U;

	if(!BL24C02_ReadSafe(LEGACY_SETTINGS_ADDRESS, sizeof(settings), settings) &&
	   settings[0] <= 1U && settings[1] <= 1U)
	{
		data->wrist_enabled = settings[0];
		data->app_sync_enabled = settings[1];
	}

	if(!BL24C02_ReadSafe(LEGACY_STEPS_ADDRESS, sizeof(step_data), step_data) &&
	   step_data[0] == now_day)
	{
		/* The legacy format stores the step high byte first. */
		data->steps = ((uint16_t)step_data[1] << 8) | step_data[2];
	}

	return 1U;
}

void EEPROM_Init(void)
{
	BL24C02_Init();
}

uint8_t EEPROM_UserDataLoad(EEPROM_UserData_t *data,
								uint8_t now_year, uint8_t now_month, uint8_t now_day)
{

	uint8_t first_record[USER_DATA_RECORD_SIZE];//读取A槽原始数据
	uint8_t second_record[USER_DATA_RECORD_SIZE];//读取B槽原始数据

	EEPROM_UserData_t first_data;//A槽解码后的业务数据
	EEPROM_UserData_t second_data;//B槽解码后的业务数据
	uint32_t first_sequence = 0U;//A槽记录的序列号
	uint32_t second_sequence = 0U;//B槽记录的序列号
	uint8_t first_valid;//A槽是否读取和校验成功
	uint8_t second_valid;//B槽是否读取和校验成功

	if(data == NULL || !date_valid(now_year, now_month, now_day))
		return 1U;
	//record_decode:校验，解析数据到结构体里
	first_valid = (uint8_t)(!BL24C02_ReadSafe(USER_DATA_SLOT_A_ADDRESS,
											 USER_DATA_RECORD_SIZE, first_record) &&
							  record_decode(first_record, &first_data, &first_sequence));
	second_valid = (uint8_t)(!BL24C02_ReadSafe(USER_DATA_SLOT_B_ADDRESS,
											  USER_DATA_RECORD_SIZE, second_record) &&
							   record_decode(second_record, &second_data, &second_sequence));

	if(first_valid || second_valid)
	{
		//判断选择A槽还是B槽
		if(first_valid && (!second_valid || !sequence_newer(second_sequence, first_sequence)))
		{
			*data = first_data;
			active_sequence = first_sequence;
			active_slot = USER_DATA_SLOT_A_ADDRESS;
		}
		else
		{
			*data = second_data;
			active_sequence = second_sequence;
			active_slot = USER_DATA_SLOT_B_ADDRESS;
		}
		return 0U;
	}

	/* No new-format record: migrate the old sparse layout when possible. */
	if(!legacy_load(data, now_year, now_month, now_day))
	{
		data->wrist_enabled = 0U;
		data->app_sync_enabled = 0U;
		data->year = now_year;
		data->month = now_month;
		data->day = now_day;
		data->steps = 0U;
	}

	active_sequence = 0U;
	active_slot = 0xFFU;
	return EEPROM_UserDataSave(data);
}


//给备用槽数据+1
//重新写入CRC
//最后写commit标志
uint8_t EEPROM_UserDataSave(const EEPROM_UserData_t *data)
{
	uint8_t record[USER_DATA_RECORD_SIZE];
	uint8_t verify[USER_DATA_RECORD_SIZE];
	uint8_t target;
	uint8_t marker;
	uint32_t next_sequence;
	uint32_t verify_sequence;
	EEPROM_UserData_t verify_data;

	if(!user_data_valid(data))
		return 1U;

	target = (active_slot == USER_DATA_SLOT_A_ADDRESS) ?
			 USER_DATA_SLOT_B_ADDRESS : USER_DATA_SLOT_A_ADDRESS;
	next_sequence = active_sequence + 1U;
	record_encode(record, data, next_sequence);

	/* Invalidate the target, write and verify its body, then commit it last. */
	marker = USER_DATA_INVALID_MARKER;
	if(BL24C02_WriteSafe((uint8_t)(target + RECORD_OFFSET_COMMIT), 1U, &marker) ||
	   BL24C02_WriteSafe(target, RECORD_OFFSET_COMMIT, record) ||
	   BL24C02_ReadSafe(target, RECORD_OFFSET_COMMIT, verify) ||
	   memcmp(record, verify, RECORD_OFFSET_COMMIT) != 0)
		return 1U;

	marker = USER_DATA_COMMIT_MARKER;
	if(BL24C02_WriteSafe((uint8_t)(target + RECORD_OFFSET_COMMIT), 1U, &marker) ||
	   BL24C02_ReadSafe(target, USER_DATA_RECORD_SIZE, verify) ||
	   !record_decode(verify, &verify_data, &verify_sequence) ||
	   verify_sequence != next_sequence)
		return 1U;

	active_sequence = next_sequence;
	active_slot = target;
	return 0U;
}
