#ifndef _DS1216_H
#define _DS1216_H

extern volatile unsigned char *ds1216_base;
unsigned long ds1216_get_cmos_time(void);
int ds1216_set_rtc_mmss(unsigned long nowtime);

#define DS1216_SEC_BYTE		1
#define DS1216_MIN_BYTE		2
#define DS1216_HOUR_BYTE	3
#define DS1216_HOUR_MASK	(0x1f)
#define DS1216_AMPM_MASK	(1<<5)
#define DS1216_1224_MASK	(1<<7)
#define DS1216_DAY_BYTE		4
#define DS1216_DAY_MASK		(0x7)
#define DS1216_DATE_BYTE	5
#define DS1216_DATE_MASK	(0x3f)
#define DS1216_MONTH_BYTE	6
#define DS1216_MONTH_MASK	(0x1f)
#define DS1216_YEAR_BYTE	7

#define DS1216_SEC(buf)		(buf[DS1216_SEC_BYTE])
#define DS1216_MIN(buf)		(buf[DS1216_MIN_BYTE])
#define DS1216_HOUR(buf)	(buf[DS1216_HOUR_BYTE] & DS1216_HOUR_MASK)
#define DS1216_AMPM(buf)	(buf[DS1216_HOUR_BYTE] & DS1216_AMPM_MASK)
#define DS1216_1224(buf)	(buf[DS1216_HOUR_BYTE] & DS1216_1224_MASK)
#define DS1216_DATE(buf)	(buf[DS1216_DATE_BYTE] & DS1216_DATE_MASK)
#define DS1216_MONTH(buf)	(buf[DS1216_MONTH_BYTE] & DS1216_MONTH_MASK)
#define DS1216_YEAR(buf)	(buf[DS1216_YEAR_BYTE])

#endif
