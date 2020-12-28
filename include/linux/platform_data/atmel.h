/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * atmel platform data
 */

#ifndef __ATMEL_H__
#define __ATMEL_H__

/* FIXME: this needs a better location, but gets stuff building again */
#ifdef CONFIG_ATMEL_PM
extern int at91_suspend_entering_slow_clock(void);
#else
static inline int at91_suspend_entering_slow_clock(void)
{
	return 0;
}
#endif

#endif /* __ATMEL_H__ */
