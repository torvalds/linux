/*
 *  Registers for the SGS-Thomson M48T35 Timekeeper RAM chip
 *  and
 *  Registers for the SGS-Thomson M48T37 Timekeeper RAM chip
 *  The 37 is the 35 plus alarm and century thus the offsets
 *  are shifted by the extra registers.
 */

#ifndef __PPC_M48T35_H
#define __PPC_M48T35_H

/* RTC offsets */
#define M48T35_RTC_FLAGS	(-8)	/* the negative regs are really T37 only */
#define M48T35_RTC_CENTURY	(-7)
#define M48T35_RTC_AL_SEC	(-6)
#define M48T35_RTC_AL_MIN	(-5)
#define M48T35_RTC_AL_HRS	(-4)
#define M48T35_RTC_AL_DOM	(-3)
#define M48T35_RTC_INTERRUPT	(-2)
#define M48T35_RTC_WATCHDOG	(-1)
#define M48T35_RTC_CONTROL	0	/* T35 starts here */
#define M48T35_RTC_SECONDS	1
#define M48T35_RTC_MINUTES	2
#define M48T35_RTC_HOURS	3
#define M48T35_RTC_DAY		4
#define M48T35_RTC_DOM		5
#define M48T35_RTC_MONTH	6
#define M48T35_RTC_YEAR		7

/* this way help us know which bits go with which regs */
#define M48T35_RTC_FLAGS_BL           0x10
#define M48T35_RTC_FLAGS_AF           0x40
#define M48T35_RTC_FLAGS_WDF          0x80

#define M48T35_RTC_INTERRUPT_AFE       0x80
#define M48T35_RTC_INTERRUPT_ABE       0x20
#define M48T35_RTC_INTERRUPT_ALL       (M48T35_RTC_INTERRUPT_AFE|M48T35_RTC_INTERRUPT_ABE)

#define M48T35_RTC_WATCHDOG_RB         0x03
#define M48T35_RTC_WATCHDOG_BMB        0x7c
#define M48T35_RTC_WATCHDOG_WDS        0x80
#define M48T35_RTC_WATCHDOG_ALL        (M48T35_RTC_WATCHDOG_RB|M48T35_RTC_WATCHDOG_BMB|M48T35_RTC_W

#define M48T35_RTC_CONTROL_WRITE       0x80
#define M48T35_RTC_CONTROL_READ        0x40
#define M48T35_RTC_CONTROL_CAL_SIGN    0x20
#define M48T35_RTC_CONTROL_CAL_VALUE   0x1f
#define M48T35_RTC_CONTROL_LOCKED      (M48T35_RTC_WRITE|M48T35_RTC_READ)
#define M48T35_RTC_CONTROL_CALIBRATION (M48T35_RTC_CONTROL_CAL_SIGN|M48T35_RTC_CONTROL_CAL_VALUE)

#define M48T35_RTC_SECONDS_SEC_1       0x0f
#define M48T35_RTC_SECONDS_SEC_10      0x70
#define M48T35_RTC_SECONDS_ST          0x80
#define M48T35_RTC_SECONDS_SEC_ALL     (M48T35_RTC_SECONDS_SEC_1|M48T35_RTC_SECONDS_SEC_10)

#define M48T35_RTC_MINUTES_MIN_1       0x0f
#define M48T35_RTC_MINUTES_MIN_10      0x70
#define M48T35_RTC_MINUTES_MIN_ALL     (M48T35_RTC_MINUTES_MIN_1|M48T35_RTC_MINUTES_MIN_10)

#define M48T35_RTC_HOURS_HRS_1         0x0f
#define M48T35_RTC_HOURS_HRS_10        0x30
#define M48T35_RTC_HOURS_HRS_ALL       (M48T35_RTC_HOURS_HRS_1|M48T35_RTC_HOURS_HRS_10)

#define M48T35_RTC_DAY_DAY_1           0x03
#define M48T35_RTC_DAY_FT              0x40

#define M48T35_RTC_ALARM_OFF           0x00
#define M48T35_RTC_WATCHDOG_OFF        0x00


/* legacy */
#define M48T35_RTC_SET      0x80
#define M48T35_RTC_STOPPED  0x80
#define M48T35_RTC_READ     0x40


#endif
