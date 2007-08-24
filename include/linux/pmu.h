/*
 * Definitions for talking to the PMU.  The PMU is a microcontroller
 * which controls battery charging and system power on PowerBook 3400
 * and 2400 models as well as the RTC and various other things.
 *
 * Copyright (C) 1998 Paul Mackerras.
 */


#define PMU_DRIVER_VERSION	2

/*
 * PMU commands
 */
#define PMU_POWER_CTRL0		0x10	/* control power of some devices */
#define PMU_POWER_CTRL		0x11	/* control power of some devices */
#define PMU_ADB_CMD		0x20	/* send ADB packet */
#define PMU_ADB_POLL_OFF	0x21	/* disable ADB auto-poll */
#define PMU_WRITE_NVRAM		0x33	/* write non-volatile RAM */
#define PMU_READ_NVRAM		0x3b	/* read non-volatile RAM */
#define PMU_SET_RTC		0x30	/* set real-time clock */
#define PMU_READ_RTC		0x38	/* read real-time clock */
#define PMU_SET_VOLBUTTON	0x40	/* set volume up/down position */
#define PMU_BACKLIGHT_BRIGHT	0x41	/* set backlight brightness */
#define PMU_GET_VOLBUTTON	0x48	/* get volume up/down position */
#define PMU_PCEJECT		0x4c	/* eject PC-card from slot */
#define PMU_BATTERY_STATE	0x6b	/* report battery state etc. */
#define PMU_SMART_BATTERY_STATE	0x6f	/* report battery state (new way) */
#define PMU_SET_INTR_MASK	0x70	/* set PMU interrupt mask */
#define PMU_INT_ACK		0x78	/* read interrupt bits */
#define PMU_SHUTDOWN		0x7e	/* turn power off */
#define PMU_CPU_SPEED		0x7d	/* control CPU speed on some models */
#define PMU_SLEEP		0x7f	/* put CPU to sleep */
#define PMU_POWER_EVENTS	0x8f	/* Send power-event commands to PMU */
#define PMU_I2C_CMD		0x9a	/* I2C operations */
#define PMU_RESET		0xd0	/* reset CPU */
#define PMU_GET_BRIGHTBUTTON	0xd9	/* report brightness up/down pos */
#define PMU_GET_COVER		0xdc	/* report cover open/closed */
#define PMU_SYSTEM_READY	0xdf	/* tell PMU we are awake */
#define PMU_GET_VERSION		0xea	/* read the PMU version */

/* Bits to use with the PMU_POWER_CTRL0 command */
#define PMU_POW0_ON		0x80	/* OR this to power ON the device */
#define PMU_POW0_OFF		0x00	/* leave bit 7 to 0 to power it OFF */
#define PMU_POW0_HARD_DRIVE	0x04	/* Hard drive power (on wallstreet/lombard ?) */

/* Bits to use with the PMU_POWER_CTRL command */
#define PMU_POW_ON		0x80	/* OR this to power ON the device */
#define PMU_POW_OFF		0x00	/* leave bit 7 to 0 to power it OFF */
#define PMU_POW_BACKLIGHT	0x01	/* backlight power */
#define PMU_POW_CHARGER		0x02	/* battery charger power */
#define PMU_POW_IRLED		0x04	/* IR led power (on wallstreet) */
#define PMU_POW_MEDIABAY	0x08	/* media bay power (wallstreet/lombard ?) */

/* Bits in PMU interrupt and interrupt mask bytes */
#define PMU_INT_PCEJECT		0x04	/* PC-card eject buttons */
#define PMU_INT_SNDBRT		0x08	/* sound/brightness up/down buttons */
#define PMU_INT_ADB		0x10	/* ADB autopoll or reply data */
#define PMU_INT_BATTERY		0x20	/* Battery state change */
#define PMU_INT_ENVIRONMENT	0x40	/* Environment interrupts */
#define PMU_INT_TICK		0x80	/* 1-second tick interrupt */

/* Other bits in PMU interrupt valid when PMU_INT_ADB is set */
#define PMU_INT_ADB_AUTO	0x04	/* ADB autopoll, when PMU_INT_ADB */
#define PMU_INT_WAITING_CHARGER	0x01	/* ??? */
#define PMU_INT_AUTO_SRQ_POLL	0x02	/* ??? */

/* Bits in the environement message (either obtained via PMU_GET_COVER,
 * or via PMU_INT_ENVIRONMENT on core99 */
#define PMU_ENV_LID_CLOSED	0x01	/* The lid is closed */

/* I2C related definitions */
#define PMU_I2C_MODE_SIMPLE	0
#define PMU_I2C_MODE_STDSUB	1
#define PMU_I2C_MODE_COMBINED	2

#define PMU_I2C_BUS_STATUS	0
#define PMU_I2C_BUS_SYSCLK	1
#define PMU_I2C_BUS_POWER	2

#define PMU_I2C_STATUS_OK	0
#define PMU_I2C_STATUS_DATAREAD	1
#define PMU_I2C_STATUS_BUSY	0xfe


/* Kind of PMU (model) */
enum {
	PMU_UNKNOWN,
	PMU_OHARE_BASED,	/* 2400, 3400, 3500 (old G3 powerbook) */
	PMU_HEATHROW_BASED,	/* PowerBook G3 series */
	PMU_PADDINGTON_BASED,	/* 1999 PowerBook G3 */
	PMU_KEYLARGO_BASED,	/* Core99 motherboard (PMU99) */
	PMU_68K_V1,		/* 68K PMU, version 1 */
	PMU_68K_V2, 		/* 68K PMU, version 2 */
};

/* PMU PMU_POWER_EVENTS commands */
enum {
	PMU_PWR_GET_POWERUP_EVENTS	= 0x00,
	PMU_PWR_SET_POWERUP_EVENTS	= 0x01,
	PMU_PWR_CLR_POWERUP_EVENTS	= 0x02,
	PMU_PWR_GET_WAKEUP_EVENTS	= 0x03,
	PMU_PWR_SET_WAKEUP_EVENTS	= 0x04,
	PMU_PWR_CLR_WAKEUP_EVENTS	= 0x05,
};

/* Power events wakeup bits */
enum {
	PMU_PWR_WAKEUP_KEY		= 0x01,	/* Wake on key press */
	PMU_PWR_WAKEUP_AC_INSERT	= 0x02, /* Wake on AC adapter plug */
	PMU_PWR_WAKEUP_AC_CHANGE	= 0x04,
	PMU_PWR_WAKEUP_LID_OPEN		= 0x08,
	PMU_PWR_WAKEUP_RING		= 0x10,
};
	
/*
 * Ioctl commands for the /dev/pmu device
 */
#include <linux/ioctl.h>

/* no param */
#define PMU_IOC_SLEEP		_IO('B', 0)
/* out param: u32*	backlight value: 0 to 15 */
#define PMU_IOC_GET_BACKLIGHT	_IOR('B', 1, size_t)
/* in param: u32	backlight value: 0 to 15 */
#define PMU_IOC_SET_BACKLIGHT	_IOW('B', 2, size_t)
/* out param: u32*	PMU model */
#define PMU_IOC_GET_MODEL	_IOR('B', 3, size_t)
/* out param: u32*	has_adb: 0 or 1 */
#define PMU_IOC_HAS_ADB		_IOR('B', 4, size_t) 
/* out param: u32*	can_sleep: 0 or 1 */
#define PMU_IOC_CAN_SLEEP	_IOR('B', 5, size_t) 
/* no param, but historically was _IOR('B', 6, 0), meaning 4 bytes */
#define PMU_IOC_GRAB_BACKLIGHT	_IOR('B', 6, size_t) 

#ifdef __KERNEL__

extern int find_via_pmu(void);

extern int pmu_request(struct adb_request *req,
		void (*done)(struct adb_request *), int nbytes, ...);
extern int pmu_queue_request(struct adb_request *req);
extern void pmu_poll(void);
extern void pmu_poll_adb(void); /* For use by xmon */
extern void pmu_wait_complete(struct adb_request *req);

/* For use before switching interrupts off for a long time;
 * warning: not stackable
 */
extern void pmu_suspend(void);
extern void pmu_resume(void);

extern void pmu_enable_irled(int on);

extern void pmu_restart(void);
extern void pmu_shutdown(void);
extern void pmu_unlock(void);

extern int pmu_present(void);
extern int pmu_get_model(void);

#ifdef CONFIG_PM
/*
 * Stuff for putting the powerbook to sleep and waking it again.
 *
 */
#include <linux/list.h>

struct pmu_sleep_notifier
{
	void (*notifier_call)(struct pmu_sleep_notifier *self, int when);
	int priority;
	struct list_head list;
};

/* Code values for calling sleep/wakeup handlers
 */
#define PBOOK_SLEEP_REQUEST	1
#define PBOOK_SLEEP_NOW		2
#define PBOOK_WAKE		3

/* priority levels in notifiers */
#define SLEEP_LEVEL_VIDEO	100	/* Video driver (first wake) */
#define SLEEP_LEVEL_MEDIABAY	90	/* Media bay driver */
#define SLEEP_LEVEL_BLOCK	80	/* IDE, SCSI */
#define SLEEP_LEVEL_NET		70	/* bmac, gmac */
#define SLEEP_LEVEL_MISC	60	/* Anything else */
#define SLEEP_LEVEL_USERLAND	55	/* Reserved for apm_emu */
#define SLEEP_LEVEL_ADB		50	/* ADB (async) */
#define SLEEP_LEVEL_SOUND	40	/* Sound driver (blocking) */

/* special register notifier functions */
int pmu_register_sleep_notifier(struct pmu_sleep_notifier* notifier);
int pmu_unregister_sleep_notifier(struct pmu_sleep_notifier* notifier);

#endif /* CONFIG_PM */

#define PMU_MAX_BATTERIES	2

/* values for pmu_power_flags */
#define PMU_PWR_AC_PRESENT	0x00000001

/* values for pmu_battery_info.flags */
#define PMU_BATT_PRESENT	0x00000001
#define PMU_BATT_CHARGING	0x00000002
#define PMU_BATT_TYPE_MASK	0x000000f0
#define PMU_BATT_TYPE_SMART	0x00000010 /* Smart battery */
#define PMU_BATT_TYPE_HOOPER	0x00000020 /* 3400/3500 */
#define PMU_BATT_TYPE_COMET	0x00000030 /* 2400 */

struct pmu_battery_info
{
	unsigned int	flags;
	unsigned int	charge;		/* current charge */
	unsigned int	max_charge;	/* maximum charge */
	signed int	amperage;	/* current, positive if charging */
	unsigned int	voltage;	/* voltage */
	unsigned int	time_remaining;	/* remaining time */
};

extern int pmu_battery_count;
extern struct pmu_battery_info pmu_batteries[PMU_MAX_BATTERIES];
extern unsigned int pmu_power_flags;

/* Backlight */
extern void pmu_backlight_init(void);

/* some code needs to know if the PMU was suspended for hibernation */
#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_PPC32)
extern int pmu_sys_suspended;
#else
/* if power management is not configured it can't be suspended */
#define pmu_sys_suspended	0
#endif

#endif	/* __KERNEL__ */
