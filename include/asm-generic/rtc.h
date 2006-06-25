/* 
 * inclue/asm-generic/rtc.h
 *
 * Author: Tom Rini <trini@mvista.com>
 *
 * Based on:
 * drivers/char/rtc.c
 *
 * Please read the COPYING file for all license details.
 */

#ifndef __ASM_RTC_H__
#define __ASM_RTC_H__

#ifdef __KERNEL__

#include <linux/mc146818rtc.h>
#include <linux/rtc.h>
#include <linux/bcd.h>

#define RTC_PIE 0x40		/* periodic interrupt enable */
#define RTC_AIE 0x20		/* alarm interrupt enable */
#define RTC_UIE 0x10		/* update-finished interrupt enable */

/* some dummy definitions */
#define RTC_BATT_BAD 0x100	/* battery bad */
#define RTC_SQWE 0x08		/* enable square-wave output */
#define RTC_DM_BINARY 0x04	/* all time/date values are BCD if clear */
#define RTC_24H 0x02		/* 24 hour mode - else hours bit 7 means pm */
#define RTC_DST_EN 0x01	        /* auto switch DST - works f. USA only */

/*
 * Returns true if a clock update is in progress
 */
static inline unsigned char rtc_is_updating(void)
{
	unsigned char uip;

	spin_lock_irq(&rtc_lock);
	uip = (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP);
	spin_unlock_irq(&rtc_lock);
	return uip;
}

static inline unsigned int get_rtc_time(struct rtc_time *time)
{
	unsigned long uip_watchdog = jiffies;
	unsigned char ctrl;
#ifdef CONFIG_MACH_DECSTATION
	unsigned int real_year;
#endif

	/*
	 * read RTC once any update in progress is done. The update
	 * can take just over 2ms. We wait 10 to 20ms. There is no need to
	 * to poll-wait (up to 1s - eeccch) for the falling edge of RTC_UIP.
	 * If you need to know *exactly* when a second has started, enable
	 * periodic update complete interrupts, (via ioctl) and then 
	 * immediately read /dev/rtc which will block until you get the IRQ.
	 * Once the read clears, read the RTC time (again via ioctl). Easy.
	 */

	if (rtc_is_updating() != 0)
		while (jiffies - uip_watchdog < 2*HZ/100) {
			barrier();
			cpu_relax();
		}

	/*
	 * Only the values that we read from the RTC are set. We leave
	 * tm_wday, tm_yday and tm_isdst untouched. Even though the
	 * RTC has RTC_DAY_OF_WEEK, we ignore it, as it is only updated
	 * by the RTC when initially set to a non-zero value.
	 */
	spin_lock_irq(&rtc_lock);
	time->tm_sec = CMOS_READ(RTC_SECONDS);
	time->tm_min = CMOS_READ(RTC_MINUTES);
	time->tm_hour = CMOS_READ(RTC_HOURS);
	time->tm_mday = CMOS_READ(RTC_DAY_OF_MONTH);
	time->tm_mon = CMOS_READ(RTC_MONTH);
	time->tm_year = CMOS_READ(RTC_YEAR);
#ifdef CONFIG_MACH_DECSTATION
	real_year = CMOS_READ(RTC_DEC_YEAR);
#endif
	ctrl = CMOS_READ(RTC_CONTROL);
	spin_unlock_irq(&rtc_lock);

	if (!(ctrl & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
	{
		BCD_TO_BIN(time->tm_sec);
		BCD_TO_BIN(time->tm_min);
		BCD_TO_BIN(time->tm_hour);
		BCD_TO_BIN(time->tm_mday);
		BCD_TO_BIN(time->tm_mon);
		BCD_TO_BIN(time->tm_year);
	}

#ifdef CONFIG_MACH_DECSTATION
	time->tm_year += real_year - 72;
#endif

	/*
	 * Account for differences between how the RTC uses the values
	 * and how they are defined in a struct rtc_time;
	 */
	if (time->tm_year <= 69)
		time->tm_year += 100;

	time->tm_mon--;

	return RTC_24H;
}

/* Set the current date and time in the real time clock. */
static inline int set_rtc_time(struct rtc_time *time)
{
	unsigned long flags;
	unsigned char mon, day, hrs, min, sec;
	unsigned char save_control, save_freq_select;
	unsigned int yrs;
#ifdef CONFIG_MACH_DECSTATION
	unsigned int real_yrs, leap_yr;
#endif

	yrs = time->tm_year;
	mon = time->tm_mon + 1;   /* tm_mon starts at zero */
	day = time->tm_mday;
	hrs = time->tm_hour;
	min = time->tm_min;
	sec = time->tm_sec;

	if (yrs > 255)	/* They are unsigned */
		return -EINVAL;

	spin_lock_irqsave(&rtc_lock, flags);
#ifdef CONFIG_MACH_DECSTATION
	real_yrs = yrs;
	leap_yr = ((!((yrs + 1900) % 4) && ((yrs + 1900) % 100)) ||
			!((yrs + 1900) % 400));
	yrs = 72;

	/*
	 * We want to keep the year set to 73 until March
	 * for non-leap years, so that Feb, 29th is handled
	 * correctly.
	 */
	if (!leap_yr && mon < 3) {
		real_yrs--;
		yrs = 73;
	}
#endif
	/* These limits and adjustments are independent of
	 * whether the chip is in binary mode or not.
	 */
	if (yrs > 169) {
		spin_unlock_irqrestore(&rtc_lock, flags);
		return -EINVAL;
	}

	if (yrs >= 100)
		yrs -= 100;

	if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY)
	    || RTC_ALWAYS_BCD) {
		BIN_TO_BCD(sec);
		BIN_TO_BCD(min);
		BIN_TO_BCD(hrs);
		BIN_TO_BCD(day);
		BIN_TO_BCD(mon);
		BIN_TO_BCD(yrs);
	}

	save_control = CMOS_READ(RTC_CONTROL);
	CMOS_WRITE((save_control|RTC_SET), RTC_CONTROL);
	save_freq_select = CMOS_READ(RTC_FREQ_SELECT);
	CMOS_WRITE((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

#ifdef CONFIG_MACH_DECSTATION
	CMOS_WRITE(real_yrs, RTC_DEC_YEAR);
#endif
	CMOS_WRITE(yrs, RTC_YEAR);
	CMOS_WRITE(mon, RTC_MONTH);
	CMOS_WRITE(day, RTC_DAY_OF_MONTH);
	CMOS_WRITE(hrs, RTC_HOURS);
	CMOS_WRITE(min, RTC_MINUTES);
	CMOS_WRITE(sec, RTC_SECONDS);

	CMOS_WRITE(save_control, RTC_CONTROL);
	CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);

	spin_unlock_irqrestore(&rtc_lock, flags);

	return 0;
}

static inline unsigned int get_rtc_ss(void)
{
	struct rtc_time h;

	get_rtc_time(&h);
	return h.tm_sec;
}

static inline int get_rtc_pll(struct rtc_pll_info *pll)
{
	return -EINVAL;
}
static inline int set_rtc_pll(struct rtc_pll_info *pll)
{
	return -EINVAL;
}

#endif /* __KERNEL__ */
#endif /* __ASM_RTC_H__ */
