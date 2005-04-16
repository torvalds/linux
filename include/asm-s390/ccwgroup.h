#ifndef S390_CCWGROUP_H
#define S390_CCWGROUP_H

struct ccw_device;
struct ccw_driver;

struct ccwgroup_device {
	unsigned long creator_id;	/* unique number of the driver */
	enum {
		CCWGROUP_OFFLINE,
		CCWGROUP_ONLINE,
	} state;
	atomic_t onoff;
	unsigned int count;		/* number of attached slave devices */
	struct device	dev;		/* master device		    */
	struct ccw_device *cdev[0];	/* variable number, allocate as needed */
};

struct ccwgroup_driver {
	struct module *owner;
	char *name;
	int max_slaves;
	unsigned long driver_id;

	int (*probe) (struct ccwgroup_device *);
	void (*remove) (struct ccwgroup_device *);
	int (*set_online) (struct ccwgroup_device *);
	int (*set_offline) (struct ccwgroup_device *);

	struct device_driver driver;		/* this driver */
};

extern int  ccwgroup_driver_register   (struct ccwgroup_driver *cdriver);
extern void ccwgroup_driver_unregister (struct ccwgroup_driver *cdriver);
extern int ccwgroup_create (struct device *root,
			    unsigned int creator_id,
			    struct ccw_driver *gdrv,
			    int argc, char *argv[]);

extern int ccwgroup_probe_ccwdev(struct ccw_device *cdev);
extern void ccwgroup_remove_ccwdev(struct ccw_device *cdev);

#define to_ccwgroupdev(x) container_of((x), struct ccwgroup_device, dev)
#define to_ccwgroupdrv(x) container_of((x), struct ccwgroup_driver, driver)
#endif
