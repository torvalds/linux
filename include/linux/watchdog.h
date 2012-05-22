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
#define	WDIOC_SETPRETIMEOUT	_IOWR(WATCHDOG_IOCTL_BASE, 8, int)
#define	WDIOC_GETPRETIMEOUT	_IOR(WATCHDOG_IOCTL_BASE, 9, int)
#define	WDIOC_GETTIMELEFT	_IOR(WATCHDOG_IOCTL_BASE, 10, int)

#define	WDIOF_UNKNOWN		-1	/* Unknown flag error */
#define	WDIOS_UNKNOWN		-1	/* Unknown status error */

#define	WDIOF_OVERHEAT		0x0001	/* Reset due to CPU overheat */
#define	WDIOF_FANFAULT		0x0002	/* Fan failed */
#define	WDIOF_EXTERN1		0x0004	/* External relay 1 */
#define	WDIOF_EXTERN2		0x0008	/* External relay 2 */
#define	WDIOF_POWERUNDER	0x0010	/* Power bad/power fault */
#define	WDIOF_CARDRESET		0x0020	/* Card previously reset the CPU */
#define	WDIOF_POWEROVER		0x0040	/* Power over voltage */
#define	WDIOF_SETTIMEOUT	0x0080  /* Set timeout (in seconds) */
#define	WDIOF_MAGICCLOSE	0x0100	/* Supports magic close char */
#define	WDIOF_PRETIMEOUT	0x0200  /* Pretimeout (in seconds), get/set */
#define	WDIOF_ALARMONLY		0x0400	/* Watchdog triggers a management or
					   other external alarm not a reboot */
#define	WDIOF_KEEPALIVEPING	0x8000	/* Keep alive ping reply */

#define	WDIOS_DISABLECARD	0x0001	/* Turn off the watchdog timer */
#define	WDIOS_ENABLECARD	0x0002	/* Turn on the watchdog timer */
#define	WDIOS_TEMPPANIC		0x0004	/* Kernel panic on temperature trip */

#ifdef __KERNEL__

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/cdev.h>

struct watchdog_ops;
struct watchdog_device;

/** struct watchdog_ops - The watchdog-devices operations
 *
 * @owner:	The module owner.
 * @start:	The routine for starting the watchdog device.
 * @stop:	The routine for stopping the watchdog device.
 * @ping:	The routine that sends a keepalive ping to the watchdog device.
 * @status:	The routine that shows the status of the watchdog device.
 * @set_timeout:The routine for setting the watchdog devices timeout value.
 * @get_timeleft:The routine that get's the time that's left before a reset.
 * @ref:	The ref operation for dyn. allocated watchdog_device structs
 * @unref:	The unref operation for dyn. allocated watchdog_device structs
 * @ioctl:	The routines that handles extra ioctl calls.
 *
 * The watchdog_ops structure contains a list of low-level operations
 * that control a watchdog device. It also contains the module that owns
 * these operations. The start and stop function are mandatory, all other
 * functions are optonal.
 */
struct watchdog_ops {
	struct module *owner;
	/* mandatory operations */
	int (*start)(struct watchdog_device *);
	int (*stop)(struct watchdog_device *);
	/* optional operations */
	int (*ping)(struct watchdog_device *);
	unsigned int (*status)(struct watchdog_device *);
	int (*set_timeout)(struct watchdog_device *, unsigned int);
	unsigned int (*get_timeleft)(struct watchdog_device *);
	void (*ref)(struct watchdog_device *);
	void (*unref)(struct watchdog_device *);
	long (*ioctl)(struct watchdog_device *, unsigned int, unsigned long);
};

/** struct watchdog_device - The structure that defines a watchdog device
 *
 * @id:		The watchdog's ID. (Allocated by watchdog_register_device)
 * @cdev:	The watchdog's Character device.
 * @dev:	The device for our watchdog
 * @parent:	The parent bus device
 * @info:	Pointer to a watchdog_info structure.
 * @ops:	Pointer to the list of watchdog operations.
 * @bootstatus:	Status of the watchdog device at boot.
 * @timeout:	The watchdog devices timeout value.
 * @min_timeout:The watchdog devices minimum timeout value.
 * @max_timeout:The watchdog devices maximum timeout value.
 * @driver-data:Pointer to the drivers private data.
 * @lock:	Lock for watchdog core internal use only.
 * @status:	Field that contains the devices internal status bits.
 *
 * The watchdog_device structure contains all information about a
 * watchdog timer device.
 *
 * The driver-data field may not be accessed directly. It must be accessed
 * via the watchdog_set_drvdata and watchdog_get_drvdata helpers.
 *
 * The lock field is for watchdog core internal use only and should not be
 * touched.
 */
struct watchdog_device {
	int id;
	struct cdev cdev;
	struct device *dev;
	struct device *parent;
	const struct watchdog_info *info;
	const struct watchdog_ops *ops;
	unsigned int bootstatus;
	unsigned int timeout;
	unsigned int min_timeout;
	unsigned int max_timeout;
	void *driver_data;
	struct mutex lock;
	unsigned long status;
/* Bit numbers for status flags */
#define WDOG_ACTIVE		0	/* Is the watchdog running/active */
#define WDOG_DEV_OPEN		1	/* Opened via /dev/watchdog ? */
#define WDOG_ALLOW_RELEASE	2	/* Did we receive the magic char ? */
#define WDOG_NO_WAY_OUT		3	/* Is 'nowayout' feature set ? */
#define WDOG_UNREGISTERED	4	/* Has the device been unregistered */
};

#ifdef CONFIG_WATCHDOG_NOWAYOUT
#define WATCHDOG_NOWAYOUT		1
#define WATCHDOG_NOWAYOUT_INIT_STATUS	(1 << WDOG_NO_WAY_OUT)
#else
#define WATCHDOG_NOWAYOUT		0
#define WATCHDOG_NOWAYOUT_INIT_STATUS	0
#endif

/* Use the following function to check wether or not the watchdog is active */
static inline bool watchdog_active(struct watchdog_device *wdd)
{
	return test_bit(WDOG_ACTIVE, &wdd->status);
}

/* Use the following function to set the nowayout feature */
static inline void watchdog_set_nowayout(struct watchdog_device *wdd, bool nowayout)
{
	if (nowayout)
		set_bit(WDOG_NO_WAY_OUT, &wdd->status);
}

/* Use the following functions to manipulate watchdog driver specific data */
static inline void watchdog_set_drvdata(struct watchdog_device *wdd, void *data)
{
	wdd->driver_data = data;
}

static inline void *watchdog_get_drvdata(struct watchdog_device *wdd)
{
	return wdd->driver_data;
}

/* drivers/watchdog/core/watchdog_core.c */
extern int watchdog_register_device(struct watchdog_device *);
extern void watchdog_unregister_device(struct watchdog_device *);

#endif	/* __KERNEL__ */

#endif  /* ifndef _LINUX_WATCHDOG_H */
