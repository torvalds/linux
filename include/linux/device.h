/*
 * device.h - generic, centralized driver model
 *
 * Copyright (c) 2001-2003 Patrick Mochel <mochel@osdl.org>
 * Copyright (c) 2004-2007 Greg Kroah-Hartman <gregkh@suse.de>
 *
 * This file is released under the GPLv2
 *
 * See Documentation/driver-model/ for more information.
 */

#ifndef _DEVICE_H_
#define _DEVICE_H_

#include <linux/ioport.h>
#include <linux/kobject.h>
#include <linux/klist.h>
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <asm/semaphore.h>
#include <asm/atomic.h>
#include <asm/device.h>

#define DEVICE_NAME_SIZE	50
/* DEVICE_NAME_HALF is really less than half to accommodate slop */
#define DEVICE_NAME_HALF	__stringify(20)
#define DEVICE_ID_SIZE		32
#define BUS_ID_SIZE		KOBJ_NAME_LEN


struct device;
struct device_driver;
struct driver_private;
struct class;
struct class_device;
struct bus_type;
struct bus_type_private;

struct bus_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct bus_type *bus, char *buf);
	ssize_t (*store)(struct bus_type *bus, const char *buf, size_t count);
};

#define BUS_ATTR(_name, _mode, _show, _store)	\
struct bus_attribute bus_attr_##_name = __ATTR(_name, _mode, _show, _store)

extern int __must_check bus_create_file(struct bus_type *,
					struct bus_attribute *);
extern void bus_remove_file(struct bus_type *, struct bus_attribute *);

struct bus_type {
	const char		*name;
	struct bus_attribute	*bus_attrs;
	struct device_attribute	*dev_attrs;
	struct driver_attribute	*drv_attrs;

	int (*match)(struct device *dev, struct device_driver *drv);
	int (*uevent)(struct device *dev, struct kobj_uevent_env *env);
	int (*probe)(struct device *dev);
	int (*remove)(struct device *dev);
	void (*shutdown)(struct device *dev);

	int (*suspend)(struct device *dev, pm_message_t state);
	int (*suspend_late)(struct device *dev, pm_message_t state);
	int (*resume_early)(struct device *dev);
	int (*resume)(struct device *dev);

	struct bus_type_private *p;
};

extern int __must_check bus_register(struct bus_type *bus);
extern void bus_unregister(struct bus_type *bus);

extern int __must_check bus_rescan_devices(struct bus_type *bus);

/* iterator helpers for buses */

int bus_for_each_dev(struct bus_type *bus, struct device *start, void *data,
		     int (*fn)(struct device *dev, void *data));
struct device *bus_find_device(struct bus_type *bus, struct device *start,
			       void *data,
			       int (*match)(struct device *dev, void *data));
struct device *bus_find_device_by_name(struct bus_type *bus,
				       struct device *start,
				       const char *name);

int __must_check bus_for_each_drv(struct bus_type *bus,
				  struct device_driver *start, void *data,
				  int (*fn)(struct device_driver *, void *));

/*
 * Bus notifiers: Get notified of addition/removal of devices
 * and binding/unbinding of drivers to devices.
 * In the long run, it should be a replacement for the platform
 * notify hooks.
 */
struct notifier_block;

extern int bus_register_notifier(struct bus_type *bus,
				 struct notifier_block *nb);
extern int bus_unregister_notifier(struct bus_type *bus,
				   struct notifier_block *nb);

/* All 4 notifers below get called with the target struct device *
 * as an argument. Note that those functions are likely to be called
 * with the device semaphore held in the core, so be careful.
 */
#define BUS_NOTIFY_ADD_DEVICE		0x00000001 /* device added */
#define BUS_NOTIFY_DEL_DEVICE		0x00000002 /* device removed */
#define BUS_NOTIFY_BOUND_DRIVER		0x00000003 /* driver bound to device */
#define BUS_NOTIFY_UNBIND_DRIVER	0x00000004 /* driver about to be
						      unbound */

extern struct kset *bus_get_kset(struct bus_type *bus);
extern struct klist *bus_get_device_klist(struct bus_type *bus);

struct device_driver {
	const char		*name;
	struct bus_type		*bus;

	struct module		*owner;
	const char 		*mod_name;	/* used for built-in modules */

	int (*probe) (struct device *dev);
	int (*remove) (struct device *dev);
	void (*shutdown) (struct device *dev);
	int (*suspend) (struct device *dev, pm_message_t state);
	int (*resume) (struct device *dev);
	struct attribute_group **groups;

	struct driver_private *p;
};


extern int __must_check driver_register(struct device_driver *drv);
extern void driver_unregister(struct device_driver *drv);

extern struct device_driver *get_driver(struct device_driver *drv);
extern void put_driver(struct device_driver *drv);
extern struct device_driver *driver_find(const char *name,
					 struct bus_type *bus);
extern int driver_probe_done(void);

/* sysfs interface for exporting driver attributes */

struct driver_attribute {
	struct attribute attr;
	ssize_t (*show)(struct device_driver *driver, char *buf);
	ssize_t (*store)(struct device_driver *driver, const char *buf,
			 size_t count);
};

#define DRIVER_ATTR(_name, _mode, _show, _store)	\
struct driver_attribute driver_attr_##_name =		\
	__ATTR(_name, _mode, _show, _store)

extern int __must_check driver_create_file(struct device_driver *driver,
					   struct driver_attribute *attr);
extern void driver_remove_file(struct device_driver *driver,
			       struct driver_attribute *attr);

extern int __must_check driver_add_kobj(struct device_driver *drv,
					struct kobject *kobj,
					const char *fmt, ...);

extern int __must_check driver_for_each_device(struct device_driver *drv,
					       struct device *start,
					       void *data,
					       int (*fn)(struct device *dev,
							 void *));
struct device *driver_find_device(struct device_driver *drv,
				  struct device *start, void *data,
				  int (*match)(struct device *dev, void *data));

/*
 * device classes
 */
struct class {
	const char		*name;
	struct module		*owner;

	struct kset		subsys;
	struct list_head	children;
	struct list_head	devices;
	struct list_head	interfaces;
	struct kset		class_dirs;
	struct semaphore	sem; /* locks children, devices, interfaces */
	struct class_attribute		*class_attrs;
	struct class_device_attribute	*class_dev_attrs;
	struct device_attribute		*dev_attrs;

	int (*uevent)(struct class_device *dev, struct kobj_uevent_env *env);
	int (*dev_uevent)(struct device *dev, struct kobj_uevent_env *env);

	void (*release)(struct class_device *dev);
	void (*class_release)(struct class *class);
	void (*dev_release)(struct device *dev);

	int (*suspend)(struct device *dev, pm_message_t state);
	int (*resume)(struct device *dev);
};

extern int __must_check class_register(struct class *class);
extern void class_unregister(struct class *class);
extern int class_for_each_device(struct class *class, void *data,
				 int (*fn)(struct device *dev, void *data));
extern struct device *class_find_device(struct class *class, void *data,
					int (*match)(struct device *, void *));
extern struct class_device *class_find_child(struct class *class, void *data,
				   int (*match)(struct class_device *, void *));


struct class_attribute {
	struct attribute attr;
	ssize_t (*show)(struct class *class, char *buf);
	ssize_t (*store)(struct class *class, const char *buf, size_t count);
};

#define CLASS_ATTR(_name, _mode, _show, _store)			\
struct class_attribute class_attr_##_name = __ATTR(_name, _mode, _show, _store)

extern int __must_check class_create_file(struct class *class,
					  const struct class_attribute *attr);
extern void class_remove_file(struct class *class,
			      const struct class_attribute *attr);

struct class_device_attribute {
	struct attribute attr;
	ssize_t (*show)(struct class_device *, char *buf);
	ssize_t (*store)(struct class_device *, const char *buf, size_t count);
};

#define CLASS_DEVICE_ATTR(_name, _mode, _show, _store)		\
struct class_device_attribute class_device_attr_##_name = 	\
	__ATTR(_name, _mode, _show, _store)

extern int __must_check class_device_create_file(struct class_device *,
				    const struct class_device_attribute *);

/**
 * struct class_device - class devices
 * @class: pointer to the parent class for this class device.  This is required.
 * @devt: for internal use by the driver core only.
 * @node: for internal use by the driver core only.
 * @kobj: for internal use by the driver core only.
 * @groups: optional additional groups to be created
 * @dev: if set, a symlink to the struct device is created in the sysfs
 * directory for this struct class device.
 * @class_data: pointer to whatever you want to store here for this struct
 * class_device.  Use class_get_devdata() and class_set_devdata() to get and
 * set this pointer.
 * @parent: pointer to a struct class_device that is the parent of this struct
 * class_device.  If NULL, this class_device will show up at the root of the
 * struct class in sysfs (which is probably what you want to have happen.)
 * @release: pointer to a release function for this struct class_device.  If
 * set, this will be called instead of the class specific release function.
 * Only use this if you want to override the default release function, like
 * when you are nesting class_device structures.
 * @uevent: pointer to a uevent function for this struct class_device.  If
 * set, this will be called instead of the class specific uevent function.
 * Only use this if you want to override the default uevent function, like
 * when you are nesting class_device structures.
 */
struct class_device {
	struct list_head	node;

	struct kobject		kobj;
	struct class		*class;
	dev_t			devt;
	struct device		*dev;
	void			*class_data;
	struct class_device	*parent;
	struct attribute_group  **groups;

	void (*release)(struct class_device *dev);
	int (*uevent)(struct class_device *dev, struct kobj_uevent_env *env);
	char class_id[BUS_ID_SIZE];
};

static inline void *class_get_devdata(struct class_device *dev)
{
	return dev->class_data;
}

static inline void class_set_devdata(struct class_device *dev, void *data)
{
	dev->class_data = data;
}


extern int __must_check class_device_register(struct class_device *);
extern void class_device_unregister(struct class_device *);
extern void class_device_initialize(struct class_device *);
extern int __must_check class_device_add(struct class_device *);
extern void class_device_del(struct class_device *);

extern struct class_device *class_device_get(struct class_device *);
extern void class_device_put(struct class_device *);

extern void class_device_remove_file(struct class_device *,
				     const struct class_device_attribute *);
extern int __must_check class_device_create_bin_file(struct class_device *,
					struct bin_attribute *);
extern void class_device_remove_bin_file(struct class_device *,
					 struct bin_attribute *);

struct class_interface {
	struct list_head	node;
	struct class		*class;

	int (*add)	(struct class_device *, struct class_interface *);
	void (*remove)	(struct class_device *, struct class_interface *);
	int (*add_dev)		(struct device *, struct class_interface *);
	void (*remove_dev)	(struct device *, struct class_interface *);
};

extern int __must_check class_interface_register(struct class_interface *);
extern void class_interface_unregister(struct class_interface *);

extern struct class *class_create(struct module *owner, const char *name);
extern void class_destroy(struct class *cls);
extern struct class_device *class_device_create(struct class *cls,
						struct class_device *parent,
						dev_t devt,
						struct device *device,
						const char *fmt, ...)
					__attribute__((format(printf, 5, 6)));
extern void class_device_destroy(struct class *cls, dev_t devt);

/*
 * The type of device, "struct device" is embedded in. A class
 * or bus can contain devices of different types
 * like "partitions" and "disks", "mouse" and "event".
 * This identifies the device type and carries type-specific
 * information, equivalent to the kobj_type of a kobject.
 * If "name" is specified, the uevent will contain it in
 * the DEVTYPE variable.
 */
struct device_type {
	const char *name;
	struct attribute_group **groups;
	int (*uevent)(struct device *dev, struct kobj_uevent_env *env);
	void (*release)(struct device *dev);
	int (*suspend)(struct device *dev, pm_message_t state);
	int (*resume)(struct device *dev);
};

/* interface for exporting device attributes */
struct device_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct device *dev, struct device_attribute *attr,
			char *buf);
	ssize_t (*store)(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count);
};

#define DEVICE_ATTR(_name, _mode, _show, _store) \
struct device_attribute dev_attr_##_name = __ATTR(_name, _mode, _show, _store)

extern int __must_check device_create_file(struct device *device,
					   struct device_attribute *entry);
extern void device_remove_file(struct device *dev,
			       struct device_attribute *attr);
extern int __must_check device_create_bin_file(struct device *dev,
					       struct bin_attribute *attr);
extern void device_remove_bin_file(struct device *dev,
				   struct bin_attribute *attr);
extern int device_schedule_callback_owner(struct device *dev,
		void (*func)(struct device *dev), struct module *owner);

/* This is a macro to avoid include problems with THIS_MODULE */
#define device_schedule_callback(dev, func)			\
	device_schedule_callback_owner(dev, func, THIS_MODULE)

/* device resource management */
typedef void (*dr_release_t)(struct device *dev, void *res);
typedef int (*dr_match_t)(struct device *dev, void *res, void *match_data);

#ifdef CONFIG_DEBUG_DEVRES
extern void *__devres_alloc(dr_release_t release, size_t size, gfp_t gfp,
			     const char *name);
#define devres_alloc(release, size, gfp) \
	__devres_alloc(release, size, gfp, #release)
#else
extern void *devres_alloc(dr_release_t release, size_t size, gfp_t gfp);
#endif
extern void devres_free(void *res);
extern void devres_add(struct device *dev, void *res);
extern void *devres_find(struct device *dev, dr_release_t release,
			 dr_match_t match, void *match_data);
extern void *devres_get(struct device *dev, void *new_res,
			dr_match_t match, void *match_data);
extern void *devres_remove(struct device *dev, dr_release_t release,
			   dr_match_t match, void *match_data);
extern int devres_destroy(struct device *dev, dr_release_t release,
			  dr_match_t match, void *match_data);

/* devres group */
extern void * __must_check devres_open_group(struct device *dev, void *id,
					     gfp_t gfp);
extern void devres_close_group(struct device *dev, void *id);
extern void devres_remove_group(struct device *dev, void *id);
extern int devres_release_group(struct device *dev, void *id);

/* managed kzalloc/kfree for device drivers, no kmalloc, always use kzalloc */
extern void *devm_kzalloc(struct device *dev, size_t size, gfp_t gfp);
extern void devm_kfree(struct device *dev, void *p);

struct device {
	struct klist		klist_children;
	struct klist_node	knode_parent;	/* node in sibling list */
	struct klist_node	knode_driver;
	struct klist_node	knode_bus;
	struct device		*parent;

	struct kobject kobj;
	char	bus_id[BUS_ID_SIZE];	/* position on parent bus */
	struct device_type	*type;
	unsigned		is_registered:1;
	unsigned		uevent_suppress:1;

	struct semaphore	sem;	/* semaphore to synchronize calls to
					 * its driver.
					 */

	struct bus_type	*bus;		/* type of bus device is on */
	struct device_driver *driver;	/* which driver has allocated this
					   device */
	void		*driver_data;	/* data private to the driver */
	void		*platform_data;	/* Platform specific data, device
					   core doesn't touch it */
	struct dev_pm_info	power;

#ifdef CONFIG_NUMA
	int		numa_node;	/* NUMA node this device is close to */
#endif
	u64		*dma_mask;	/* dma mask (if dma'able device) */
	u64		coherent_dma_mask;/* Like dma_mask, but for
					     alloc_coherent mappings as
					     not all hardware supports
					     64 bit addresses for consistent
					     allocations such descriptors. */

	struct list_head	dma_pools;	/* dma pools (if dma'ble) */

	struct dma_coherent_mem	*dma_mem; /* internal for coherent mem
					     override */
	/* arch specific additions */
	struct dev_archdata	archdata;

	spinlock_t		devres_lock;
	struct list_head	devres_head;

	/* class_device migration path */
	struct list_head	node;
	struct class		*class;
	dev_t			devt;	/* dev_t, creates the sysfs "dev" */
	struct attribute_group	**groups;	/* optional groups */

	void	(*release)(struct device *dev);
};

#ifdef CONFIG_NUMA
static inline int dev_to_node(struct device *dev)
{
	return dev->numa_node;
}
static inline void set_dev_node(struct device *dev, int node)
{
	dev->numa_node = node;
}
#else
static inline int dev_to_node(struct device *dev)
{
	return -1;
}
static inline void set_dev_node(struct device *dev, int node)
{
}
#endif

static inline void *dev_get_drvdata(struct device *dev)
{
	return dev->driver_data;
}

static inline void dev_set_drvdata(struct device *dev, void *data)
{
	dev->driver_data = data;
}

static inline int device_is_registered(struct device *dev)
{
	return dev->is_registered;
}

void driver_init(void);

/*
 * High level routines for use by the bus drivers
 */
extern int __must_check device_register(struct device *dev);
extern void device_unregister(struct device *dev);
extern void device_initialize(struct device *dev);
extern int __must_check device_add(struct device *dev);
extern void device_del(struct device *dev);
extern int device_for_each_child(struct device *dev, void *data,
		     int (*fn)(struct device *dev, void *data));
extern struct device *device_find_child(struct device *dev, void *data,
				int (*match)(struct device *dev, void *data));
extern int device_rename(struct device *dev, char *new_name);
extern int device_move(struct device *dev, struct device *new_parent);

/*
 * Manual binding of a device to driver. See drivers/base/bus.c
 * for information on use.
 */
extern int __must_check device_bind_driver(struct device *dev);
extern void device_release_driver(struct device *dev);
extern int  __must_check device_attach(struct device *dev);
extern int __must_check driver_attach(struct device_driver *drv);
extern int __must_check device_reprobe(struct device *dev);

/*
 * Easy functions for dynamically creating devices on the fly
 */
extern struct device *device_create(struct class *cls, struct device *parent,
				    dev_t devt, const char *fmt, ...)
				    __attribute__((format(printf, 4, 5)));
extern void device_destroy(struct class *cls, dev_t devt);
#ifdef CONFIG_PM_SLEEP
extern void destroy_suspended_device(struct class *cls, dev_t devt);
extern void device_pm_schedule_removal(struct device *);
#else /* !CONFIG_PM_SLEEP */
static inline void destroy_suspended_device(struct class *cls, dev_t devt)
{
	device_destroy(cls, devt);
}

static inline void device_pm_schedule_removal(struct device *dev)
{
	device_unregister(dev);
}
#endif /* !CONFIG_PM_SLEEP */

/*
 * Platform "fixup" functions - allow the platform to have their say
 * about devices and actions that the general device layer doesn't
 * know about.
 */
/* Notify platform of device discovery */
extern int (*platform_notify)(struct device *dev);

extern int (*platform_notify_remove)(struct device *dev);


/**
 * get_device - atomically increment the reference count for the device.
 *
 */
extern struct device *get_device(struct device *dev);
extern void put_device(struct device *dev);


/* drivers/base/power/shutdown.c */
extern void device_shutdown(void);

/* drivers/base/sys.c */
extern void sysdev_shutdown(void);

/* debugging and troubleshooting/diagnostic helpers. */
extern const char *dev_driver_string(struct device *dev);
#define dev_printk(level, dev, format, arg...)	\
	printk(level "%s %s: " format , dev_driver_string(dev) , \
	       (dev)->bus_id , ## arg)

#define dev_emerg(dev, format, arg...)		\
	dev_printk(KERN_EMERG , dev , format , ## arg)
#define dev_alert(dev, format, arg...)		\
	dev_printk(KERN_ALERT , dev , format , ## arg)
#define dev_crit(dev, format, arg...)		\
	dev_printk(KERN_CRIT , dev , format , ## arg)
#define dev_err(dev, format, arg...)		\
	dev_printk(KERN_ERR , dev , format , ## arg)
#define dev_warn(dev, format, arg...)		\
	dev_printk(KERN_WARNING , dev , format , ## arg)
#define dev_notice(dev, format, arg...)		\
	dev_printk(KERN_NOTICE , dev , format , ## arg)
#define dev_info(dev, format, arg...)		\
	dev_printk(KERN_INFO , dev , format , ## arg)

#ifdef DEBUG
#define dev_dbg(dev, format, arg...)		\
	dev_printk(KERN_DEBUG , dev , format , ## arg)
#else
static inline int __attribute__ ((format (printf, 2, 3)))
dev_dbg(struct device *dev, const char *fmt, ...)
{
	return 0;
}
#endif

#ifdef VERBOSE_DEBUG
#define dev_vdbg	dev_dbg
#else
static inline int __attribute__ ((format (printf, 2, 3)))
dev_vdbg(struct device *dev, const char *fmt, ...)
{
	return 0;
}
#endif

/* Create alias, so I can be autoloaded. */
#define MODULE_ALIAS_CHARDEV(major,minor) \
	MODULE_ALIAS("char-major-" __stringify(major) "-" __stringify(minor))
#define MODULE_ALIAS_CHARDEV_MAJOR(major) \
	MODULE_ALIAS("char-major-" __stringify(major) "-*")
#endif /* _DEVICE_H_ */
