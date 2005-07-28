/*
 *	Generic watchdog defines. Derived from..
 *
 * Berkshire PC Watchdog Defines
 * by Ken Hollis <khollis@bitgate.com>
 *
 */

#ifndef _LINUX_WATCHDOG_H
#define _LINUX_WATCHDOG_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define	WATCHDOG_IOCTL_BASE	'W'

struct watchdog_info {
	__u32 options;		/* Options the card/driver supports */
	__u32 firmware_version;	/* Firmware version of the card */
	__u8  identity[32];	/* Identity of the board */
};

#define	WDIOC_GETSUPPORT	_IOR(WATCHDOG_IOCTL_BASE, 0, struct watchdog_info)
#define	WDIOC_GETSTATUS		_IOR(WATCHDOG_IOCTL_BASE, 1, int)
#define	WDIOC_GETBOOTSTATUS	_IOR(WATCHDOG_IOCTL_BASE, 2, int)
#define	WDIOC_GETTEMP		_IOR(WATCHDOG_IOCTL_BASE, 3, int)
#define	WDIOC_SETOPTIONS	_IOR(WATCHDOG_IOCTL_BASE, 4, int)
#define	WDIOC_KEEPALIVE		_IOR(WATCHDOG_IOCTL_BASE, 5, int)
#define	WDIOC_SETTIMEOUT        _IOWR(WATCHDOG_IOCTL_BASE, 6, int)
#define	WDIOC_GETTIMEOUT        _IOR(WATCHDOG_IOCTL_BASE, 7, int)

#define	WDIOF_UNKNOWN		-1	/* Unknown flag error */
#define	WDIOS_UNKNOWN		-1	/* Unknown status error */

#define	WDIOF_OVERHEAT		0x0001	/* Reset due to CPU overheat */
#define	WDIOF_FANFAULT		0x0002	/* Fan failed */
#define	WDIOF_EXTERN1		0x0004	/* External relay 1 */
#define	WDIOF_EXTERN2		0x0008	/* External relay 2 */
#define	WDIOF_POWERUNDER	0x0010	/* Power bad/power fault */
#define	WDIOF_CARDRESET		0x0020	/* Card previously reset the CPU */
#define WDIOF_POWEROVER		0x0040	/* Power over voltage */
#define WDIOF_SETTIMEOUT	0x0080  /* Set timeout (in seconds) */
#define WDIOF_MAGICCLOSE	0x0100	/* Supports magic close char */
#define	WDIOF_KEEPALIVEPING	0x8000	/* Keep alive ping reply */

#define	WDIOS_DISABLECARD	0x0001	/* Turn off the watchdog timer */
#define	WDIOS_ENABLECARD	0x0002	/* Turn on the watchdog timer */
#define	WDIOS_TEMPPANIC		0x0004	/* Kernel panic on temperature trip */

#ifdef __KERNEL__

#ifdef CONFIG_WATCHDOG_NOWAYOUT
#define WATCHDOG_NOWAYOUT	1
#else
#define WATCHDOG_NOWAYOUT	0
#endif

#endif	/* __KERNEL__ */

#endif  /* ifndef _LINUX_WATCHDOG_H */
