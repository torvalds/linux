/*	$OpenBSD: cpu.h,v 1.4 2011/06/24 19:47:48 naddy Exp $	*/
/*	$NetBSD: cpu.h,v 1.1 2006/09/01 21:26:18 uwe Exp $	*/

#ifndef	_MACHINE_CPU_H_
#define	_MACHINE_CPU_H_

void machine_reset(void);

#include <sh/cpu.h>

/*
 * CTL_MACHDEP definitions.
 */
#define	CPU_CONSDEV		1	/* dev_t: console terminal device */
		/*		2	   formerly: keyboard reset */
#define	CPU_LED_BLINK		3	/* blink leds */
#define	CPU_MAXID		4	/* number of valid machdep ids */

#define	CTL_MACHDEP_NAMES {						\
	{ 0, 0 },							\
	{ "console_device",	CTLTYPE_STRUCT },			\
	{ 0, 0 },							\
	{ "led_blink",		CTLTYPE_INT }				\
}

#endif	/* _MACHINE_CPU_H_ */
