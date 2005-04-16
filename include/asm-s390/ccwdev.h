/*
 *  include/asm-s390/ccwdev.h
 *  include/asm-s390x/ccwdev.h
 *
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Arnd Bergmann <arndb@de.ibm.com>
 *
 *  Interface for CCW device drivers
 */
#ifndef _S390_CCWDEV_H_
#define _S390_CCWDEV_H_

#include <linux/device.h>
#include <linux/mod_devicetable.h>

/* structs from asm/cio.h */
struct irb;
struct ccw1;

/* simplified initializers for struct ccw_device:
 * CCW_DEVICE and CCW_DEVICE_DEVTYPE initialize one
 * entry in your MODULE_DEVICE_TABLE and set the match_flag correctly */
#define CCW_DEVICE(cu, cum) 						\
	.cu_type=(cu), .cu_model=(cum),					\
	.match_flags=(CCW_DEVICE_ID_MATCH_CU_TYPE			\
		   | (cum ? CCW_DEVICE_ID_MATCH_CU_MODEL : 0))

#define CCW_DEVICE_DEVTYPE(cu, cum, dev, devm)				\
	.cu_type=(cu), .cu_model=(cum), .dev_type=(dev), .dev_model=(devm),\
	.match_flags=CCW_DEVICE_ID_MATCH_CU_TYPE			\
		   | ((cum) ? CCW_DEVICE_ID_MATCH_CU_MODEL : 0) 	\
		   | CCW_DEVICE_ID_MATCH_DEVICE_TYPE			\
		   | ((devm) ? CCW_DEVICE_ID_MATCH_DEVICE_MODEL : 0)

/* scan through an array of device ids and return the first
 * entry that matches the device.
 *
 * the array must end with an entry containing zero match_flags
 */
static inline const struct ccw_device_id *
ccw_device_id_match(const struct ccw_device_id *array,
			const struct ccw_device_id *match)
{
	const struct ccw_device_id *id = array;

	for (id = array; id->match_flags; id++) {
		if ((id->match_flags & CCW_DEVICE_ID_MATCH_CU_TYPE)
		    && (id->cu_type != match->cu_type))
			continue;

		if ((id->match_flags & CCW_DEVICE_ID_MATCH_CU_MODEL)
		    && (id->cu_model != match->cu_model))
			continue;

		if ((id->match_flags & CCW_DEVICE_ID_MATCH_DEVICE_TYPE)
		    && (id->dev_type != match->dev_type))
			continue;

		if ((id->match_flags & CCW_DEVICE_ID_MATCH_DEVICE_MODEL)
		    && (id->dev_model != match->dev_model))
			continue;

		return id;
	}

	return 0;
}

/* The struct ccw device is our replacement for the globally accessible
 * ioinfo array. ioinfo will mutate into a subchannel device later.
 *
 * Reference: Documentation/s390/driver-model.txt */
struct ccw_device {
	spinlock_t *ccwlock;
	struct ccw_device_private *private;	/* cio private information */
	struct ccw_device_id id;	/* id of this device, driver_info is
					   set by ccw_find_driver */
	struct ccw_driver *drv;		/* */
	struct device dev;		/* */
	int online;
	/* This is sick, but a driver can have different interrupt handlers 
	   for different ccw_devices (multi-subchannel drivers)... */
	void (*handler) (struct ccw_device *, unsigned long, struct irb *);
};


/* Each ccw driver registers with the ccw root bus */
struct ccw_driver {
	struct module *owner;		/* for automatic MOD_INC_USE_COUNT   */
	struct ccw_device_id *ids;	/* probe driver with these devs      */
	int (*probe) (struct ccw_device *); /* ask driver to probe dev 	     */
	void (*remove) (struct ccw_device *);
					/* device is no longer available     */
	int (*set_online) (struct ccw_device *);
	int (*set_offline) (struct ccw_device *);
	int (*notify) (struct ccw_device *, int);
	struct device_driver driver;	/* higher level structure, don't init
					   this from your driver	     */
	char *name;
};

extern struct ccw_device *get_ccwdev_by_busid(struct ccw_driver *cdrv,
					      const char *bus_id);

/* devices drivers call these during module load and unload.
 * When a driver is registered, its probe method is called
 * when new devices for its type pop up */
extern int  ccw_driver_register   (struct ccw_driver *driver);
extern void ccw_driver_unregister (struct ccw_driver *driver);

struct ccw1;

extern int ccw_device_set_options(struct ccw_device *, unsigned long);

/* Allow for i/o completion notification after primary interrupt status. */
#define CCWDEV_EARLY_NOTIFICATION	0x0001
/* Report all interrupt conditions. */
#define CCWDEV_REPORT_ALL	 	0x0002
/* Try to perform path grouping. */
#define CCWDEV_DO_PATHGROUP             0x0004
/* Allow forced onlining of boxed devices. */
#define CCWDEV_ALLOW_FORCE              0x0008

/*
 * ccw_device_start()
 *
 *  Start a S/390 channel program. When the interrupt arrives, the
 *  IRQ handler is called, either immediately, delayed (dev-end missing,
 *  or sense required) or never (no IRQ handler registered).
 *  Depending on the action taken, ccw_device_start() returns:  
 *                           0	     - Success
 *			     -EBUSY  - Device busy, or status pending
 *			     -ENODEV - Device not operational
 *                           -EINVAL - Device invalid for operation
 */
extern int ccw_device_start(struct ccw_device *, struct ccw1 *,
			    unsigned long, __u8, unsigned long);
/*
 * ccw_device_start_timeout()
 *
 * This function notifies the device driver if the channel program has not
 * completed during the specified time. If a timeout occurs, the channel
 * program is terminated via xsch(), hsch() or csch().
 */
extern int ccw_device_start_timeout(struct ccw_device *, struct ccw1 *,
				    unsigned long, __u8, unsigned long, int);
/*
 * ccw_device_start_key()
 * ccw_device_start_key_timeout()
 *
 * Same as ccw_device_start() and ccw_device_start_timeout(), except a
 * storage key != default key can be provided for the I/O.
 */
extern int ccw_device_start_key(struct ccw_device *, struct ccw1 *,
				unsigned long, __u8, __u8, unsigned long);
extern int ccw_device_start_timeout_key(struct ccw_device *, struct ccw1 *,
					unsigned long, __u8, __u8,
					unsigned long, int);


extern int ccw_device_resume(struct ccw_device *);
extern int ccw_device_halt(struct ccw_device *, unsigned long);
extern int ccw_device_clear(struct ccw_device *, unsigned long);

extern int read_dev_chars(struct ccw_device *cdev, void **buffer, int length);
extern int read_conf_data(struct ccw_device *cdev, void **buffer, int *length);
extern int read_conf_data_lpm(struct ccw_device *cdev, void **buffer,
			      int *length, __u8 lpm);

extern int ccw_device_set_online(struct ccw_device *cdev);
extern int ccw_device_set_offline(struct ccw_device *cdev);


extern struct ciw *ccw_device_get_ciw(struct ccw_device *, __u32 cmd);
extern __u8 ccw_device_get_path_mask(struct ccw_device *);

#define get_ccwdev_lock(x) (x)->ccwlock

#define to_ccwdev(n) container_of(n, struct ccw_device, dev)
#define to_ccwdrv(n) container_of(n, struct ccw_driver, driver)

extern struct ccw_device *ccw_device_probe_console(void);

// FIXME: these have to go
extern int _ccw_device_get_device_number(struct ccw_device *);
extern int _ccw_device_get_subchannel_number(struct ccw_device *);

extern struct device *s390_root_dev_register(const char *);
extern void s390_root_dev_unregister(struct device *);

extern void *ccw_device_get_chp_desc(struct ccw_device *, int);
#endif /* _S390_CCWDEV_H_ */
