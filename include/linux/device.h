/*
 * device.h - generic, centralized driver model
 *
 * Copyright (c) 2001-2003 Patrick Mochel <mochel@osdl.org>
 *
 * This file is released under the GPLv2
 *
 * See Documentation/driver-model/ for more information.
 */

#ifndef _DEVICE_H_
#define _DEVICE_H_

#include <linux/config.h>
#include <linux/ioport.h>
#include <linux/kobject.h>
#include <linux/klist.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <asm/semaphore.h>
#include <asm/atomic.h>

#define DEVICE_NAME_SIZE	50
#define DEVICE_NAME_HALF	__stringify(20)	/* Less than half to accommodate slop */
#define DEVICE_ID_SIZE		32
#define BUS_ID_SIZE		KOBJ_NAME_LEN


struct device;
struct device_driver;
struct class;
struct class_device;

struct bus_type {
	const char		* name;

	struct subsystem	subsys;
	struct kset		drivers;
	struct kset		devices;
	struct klist		klist_devices;
	struct klist		klist_drivers;

	struct bus_attribute	* bus_attrs;
	struct device_attribute	* dev_attrs;
	struct driver_attribute	* drv_attrs;

	int		(*match)(struct device * dev, struct device_driver * drv);
	int		(*hotplug) (struct device *dev, char **envp, 
				    int num_envp, char *buffer, int buffer_size);
	int		(*suspend)(struct device * dev, pm_message_t state);
	int		(*resume)(struct device * dev);
};

extern int bus_register(struct bus_type * bus);
extern void bus_unregister(struct bus_type * bus);

extern void bus_rescan_devices(struct bus_type * bus);

extern struct bus_type * get_bus(struct bus_type * bus);
extern void put_bus(struct bus_type * bus);

extern struct bus_type * find_bus(char * name);

/* iterator helpers for buses */

int bus_for_each_dev(struct bus_type * bus, struct device * start, void * data,
		     int (*fn)(struct device *, void *));
struct device * bus_find_device(struct bus_type *bus, struct device *start,
				void *data, int (*match)(struct device *, void *));

int bus_for_each_drv(struct bus_type * bus, struct device_driver * start, 
		     void * data, int (*fn)(struct device_driver *, void *));


/* driverfs interface for exporting bus attributes */

struct bus_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct bus_type *, char * buf);
	ssize_t (*store)(struct bus_type *, const char * buf, size_t count);
};

#define BUS_ATTR(_name,_mode,_show,_store)	\
struct bus_attribute bus_attr_##_name = __ATTR(_name,_mode,_show,_store)

extern int bus_create_file(struct bus_type *, struct bus_attribute *);
extern void bus_remove_file(struct bus_type *, struct bus_attribute *);

struct device_driver {
	const char		* name;
	struct bus_type		* bus;

	struct completion	unloaded;
	struct kobject		kobj;
	struct klist		klist_devices;
	struct klist_node	knode_bus;

	struct module		* owner;

	int	(*probe)	(struct device * dev);
	int	(*remove)	(struct device * dev);
	void	(*shutdown)	(struct device * dev);
	int	(*suspend)	(struct device * dev, pm_message_t state);
	int	(*resume)	(struct device * dev);
};


extern int driver_register(struct device_driver * drv);
extern void driver_unregister(struct device_driver * drv);

extern struct device_driver * get_driver(struct device_driver * drv);
extern void put_driver(struct device_driver * drv);
extern struct device_driver *driver_find(const char *name, struct bus_type *bus);


/* driverfs interface for exporting driver attributes */

struct driver_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct device_driver *, char * buf);
	ssize_t (*store)(struct device_driver *, const char * buf, size_t count);
};

#define DRIVER_ATTR(_name,_mode,_show,_store)	\
struct driver_attribute driver_attr_##_name = __ATTR(_name,_mode,_show,_store)

extern int driver_create_file(struct device_driver *, struct driver_attribute *);
extern void driver_remove_file(struct device_driver *, struct driver_attribute *);

extern int driver_for_each_device(struct device_driver * drv, struct device * start,
				  void * data, int (*fn)(struct device *, void *));
struct device * driver_find_device(struct device_driver *drv,
				   struct device *start, void *data,
				   int (*match)(struct device *, void *));


/*
 * device classes
 */
struct class {
	const char		* name;
	struct module		* owner;

	struct subsystem	subsys;
	struct list_head	children;
	struct list_head	interfaces;
	struct semaphore	sem;	/* locks both the children and interfaces lists */

	struct class_attribute		* class_attrs;
	struct class_device_attribute	* class_dev_attrs;

	int	(*hotplug)(struct class_device *dev, char **envp, 
			   int num_envp, char *buffer, int buffer_size);

	void	(*release)(struct class_device *dev);
	void	(*class_release)(struct class *class);
};

extern int class_register(struct class *);
extern void class_unregister(struct class *);

extern struct class * class_get(struct class *);
extern void class_put(struct class *);


struct class_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct class *, char * buf);
	ssize_t (*store)(struct class *, const char * buf, size_t count);
};

#define CLASS_ATTR(_name,_mode,_show,_store)			\
struct class_attribute class_attr_##_name = __ATTR(_name,_mode,_show,_store) 

extern int class_create_file(struct class *, const struct class_attribute *);
extern void class_remove_file(struct class *, const struct class_attribute *);

struct class_device_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct class_device *, char * buf);
	ssize_t (*store)(struct class_device *, const char * buf, size_t count);
};

#define CLASS_DEVICE_ATTR(_name,_mode,_show,_store)		\
struct class_device_attribute class_device_attr_##_name = 	\
	__ATTR(_name,_mode,_show,_store)

extern int class_device_create_file(struct class_device *,
				    const struct class_device_attribute *);

/**
 * struct class_device - class devices
 * @class: pointer to the parent class for this class device.  This is required.
 * @devt: for internal use by the driver core only.
 * @node: for internal use by the driver core only.
 * @kobj: for internal use by the driver core only.
 * @devt_attr: for internal use by the driver core only.
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
 * @hotplug: pointer to a hotplug function for this struct class_device.  If
 * set, this will be called instead of the class specific hotplug function.
 * Only use this if you want to override the default hotplug function, like
 * when you are nesting class_device structures.
 */
struct class_device {
	struct list_head	node;

	struct kobject		kobj;
	struct class		* class;	/* required */
	dev_t			devt;		/* dev_t, creates the sysfs "dev" */
	struct class_device_attribute *devt_attr;
	struct class_device_attribute uevent_attr;
	struct device		* dev;		/* not necessary, but nice to have */
	void			* class_data;	/* class-specific data */
	struct class_device	*parent;	/* parent of this child device, if there is one */

	void	(*release)(struct class_device *dev);
	int	(*hotplug)(struct class_device *dev, char **envp,
			   int num_envp, char *buffer, int buffer_size);
	char	class_id[BUS_ID_SIZE];	/* unique to this class */
};

static inline void *
class_get_devdata (struct class_device *dev)
{
	return dev->class_data;
}

static inline void
class_set_devdata (struct class_device *dev, void *data)
{
	dev->class_data = data;
}


extern int class_device_register(struct class_device *);
extern void class_device_unregister(struct class_device *);
extern void class_device_initialize(struct class_device *);
extern int class_device_add(struct class_device *);
extern void class_device_del(struct class_device *);

extern int class_device_rename(struct class_device *, char *);

extern struct class_device * class_device_get(struct class_device *);
extern void class_device_put(struct class_device *);

extern void class_device_remove_file(struct class_device *, 
				     const struct class_device_attribute *);
extern int class_device_create_bin_file(struct class_device *,
					struct bin_attribute *);
extern void class_device_remove_bin_file(struct class_device *,
					 struct bin_attribute *);

struct class_interface {
	struct list_head	node;
	struct class		*class;

	int (*add)	(struct class_device *, struct class_interface *);
	void (*remove)	(struct class_device *, struct class_interface *);
};

extern int class_interface_register(struct class_interface *);
extern void class_interface_unregister(struct class_interface *);

extern struct class *class_create(struct module *owner, char *name);
extern void class_destroy(struct class *cls);
extern struct class_device *class_device_create(struct class *cls,
						struct class_device *parent,
						dev_t devt,
						struct device *device,
						char *fmt, ...)
					__attribute__((format(printf,5,6)));
extern void class_device_destroy(struct class *cls, dev_t devt);


/* interface for exporting device attributes */
struct device_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct device *dev, struct device_attribute *attr,
			char *buf);
	ssize_t (*store)(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count);
};

#define DEVICE_ATTR(_name,_mode,_show,_store) \
struct device_attribute dev_attr_##_name = __ATTR(_name,_mode,_show,_store)

extern int device_create_file(struct device *device, struct device_attribute * entry);
extern void device_remove_file(struct device * dev, struct device_attribute * attr);
struct device {
	struct klist		klist_children;
	struct klist_node	knode_parent;		/* node in sibling list */
	struct klist_node	knode_driver;
	struct klist_node	knode_bus;
	struct device 	* parent;

	struct kobject kobj;
	char	bus_id[BUS_ID_SIZE];	/* position on parent bus */
	struct device_attribute uevent_attr;

	struct semaphore	sem;	/* semaphore to synchronize calls to
					 * its driver.
					 */

	struct bus_type	* bus;		/* type of bus device is on */
	struct device_driver *driver;	/* which driver has allocated this
					   device */
	void		*driver_data;	/* data private to the driver */
	void		*platform_data;	/* Platform specific data, device
					   core doesn't touch it */
	void		*firmware_data; /* Firmware specific data (e.g. ACPI,
					   BIOS data),reserved for device core*/
	struct dev_pm_info	power;

	u64		*dma_mask;	/* dma mask (if dma'able device) */
	u64		coherent_dma_mask;/* Like dma_mask, but for
					     alloc_coherent mappings as
					     not all hardware supports
					     64 bit addresses for consistent
					     allocations such descriptors. */

	struct list_head	dma_pools;	/* dma pools (if dma'ble) */

	struct dma_coherent_mem	*dma_mem; /* internal for coherent mem
					     override */

	void	(*release)(struct device * dev);
};

static inline void *
dev_get_drvdata (struct device *dev)
{
	return dev->driver_data;
}

static inline void
dev_set_drvdata (struct device *dev, void *data)
{
	dev->driver_data = data;
}

static inline int device_is_registered(struct device *dev)
{
	return klist_node_attached(&dev->knode_bus);
}

/*
 * High level routines for use by the bus drivers
 */
extern int device_register(struct device * dev);
extern void device_unregister(struct device * dev);
extern void device_initialize(struct device * dev);
extern int device_add(struct device * dev);
extern void device_del(struct device * dev);
extern int device_for_each_child(struct device *, void *,
		     int (*fn)(struct device *, void *));

/*
 * Manual binding of a device to driver. See drivers/base/bus.c
 * for information on use.
 */
extern void device_bind_driver(struct device * dev);
extern void device_release_driver(struct device * dev);
extern int  device_attach(struct device * dev);
extern void driver_attach(struct device_driver * drv);


/*
 * Platform "fixup" functions - allow the platform to have their say
 * about devices and actions that the general device layer doesn't
 * know about.
 */
/* Notify platform of device discovery */
extern int (*platform_notify)(struct device * dev);

extern int (*platform_notify_remove)(struct device * dev);


/**
 * get_device - atomically increment the reference count for the device.
 *
 */
extern struct device * get_device(struct device * dev);
extern void put_device(struct device * dev);


/* drivers/base/platform.c */

struct platform_device {
	const char	* name;
	u32		id;
	struct device	dev;
	u32		num_resources;
	struct resource	* resource;
};

#define to_platform_device(x) container_of((x), struct platform_device, dev)

extern int platform_device_register(struct platform_device *);
extern void platform_device_unregister(struct platform_device *);

extern struct bus_type platform_bus_type;
extern struct device platform_bus;

extern struct resource *platform_get_resource(struct platform_device *, unsigned int, unsigned int);
extern int platform_get_irq(struct platform_device *, unsigned int);
extern struct resource *platform_get_resource_byname(struct platform_device *, unsigned int, char *);
extern int platform_get_irq_byname(struct platform_device *, char *);
extern int platform_add_devices(struct platform_device **, int);

extern struct platform_device *platform_device_register_simple(char *, unsigned int, struct resource *, unsigned int);

/* drivers/base/power.c */
extern void device_shutdown(void);


/* drivers/base/firmware.c */
extern int firmware_register(struct subsystem *);
extern void firmware_unregister(struct subsystem *);

/* debugging and troubleshooting/diagnostic helpers. */
#define dev_printk(level, dev, format, arg...)	\
	printk(level "%s %s: " format , (dev)->driver ? (dev)->driver->name : "" , (dev)->bus_id , ## arg)

#ifdef DEBUG
#define dev_dbg(dev, format, arg...)		\
	dev_printk(KERN_DEBUG , dev , format , ## arg)
#else
#define dev_dbg(dev, format, arg...) do { (void)(dev); } while (0)
#endif

#define dev_err(dev, format, arg...)		\
	dev_printk(KERN_ERR , dev , format , ## arg)
#define dev_info(dev, format, arg...)		\
	dev_printk(KERN_INFO , dev , format , ## arg)
#define dev_warn(dev, format, arg...)		\
	dev_printk(KERN_WARNING , dev , format , ## arg)

/* Create alias, so I can be autoloaded. */
#define MODULE_ALIAS_CHARDEV(major,minor) \
	MODULE_ALIAS("char-major-" __stringify(major) "-" __stringify(minor))
#define MODULE_ALIAS_CHARDEV_MAJOR(major) \
	MODULE_ALIAS("char-major-" __stringify(major) "-*")
#endif /* _DEVICE_H_ */
