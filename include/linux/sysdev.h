/**
 * System devices follow a slightly different driver model. 
 * They don't need to do dynammic driver binding, can't be probed, 
 * and don't reside on any type of peripheral bus. 
 * So, we represent and treat them a little differently.
 * 
 * We still have a notion of a driver for a system device, because we still
 * want to perform basic operations on these devices. 
 *
 * We also support auxillary drivers binding to devices of a certain class.
 * 
 * This allows configurable drivers to register themselves for devices of
 * a certain type. And, it allows class definitions to reside in generic
 * code while arch-specific code can register specific drivers.
 *
 * Auxillary drivers registered with a NULL cls are registered as drivers
 * for all system devices, and get notification calls for each device. 
 */


#ifndef _SYSDEV_H_
#define _SYSDEV_H_

#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/pm.h>


struct sys_device;

struct sysdev_class {
	const char *name;
	struct list_head	drivers;

	/* Default operations for these types of devices */
	int	(*shutdown)(struct sys_device *);
	int	(*suspend)(struct sys_device *, pm_message_t state);
	int	(*resume)(struct sys_device *);
	struct kset		kset;
};

struct sysdev_class_attribute {
	struct attribute attr;
	ssize_t (*show)(struct sysdev_class *, char *);
	ssize_t (*store)(struct sysdev_class *, const char *, size_t);
};

#define _SYSDEV_CLASS_ATTR(_name,_mode,_show,_store) 		\
{					 			\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show	= _show,					\
	.store	= _store,					\
}

#define SYSDEV_CLASS_ATTR(_name,_mode,_show,_store) 		\
	struct sysdev_class_attribute attr_##_name = 		\
		_SYSDEV_CLASS_ATTR(_name,_mode,_show,_store)


extern int sysdev_class_register(struct sysdev_class *);
extern void sysdev_class_unregister(struct sysdev_class *);

extern int sysdev_class_create_file(struct sysdev_class *,
	struct sysdev_class_attribute *);
extern void sysdev_class_remove_file(struct sysdev_class *,
	struct sysdev_class_attribute *);
/**
 * Auxillary system device drivers.
 */

struct sysdev_driver {
	struct list_head	entry;
	int	(*add)(struct sys_device *);
	int	(*remove)(struct sys_device *);
	int	(*shutdown)(struct sys_device *);
	int	(*suspend)(struct sys_device *, pm_message_t state);
	int	(*resume)(struct sys_device *);
};


extern int sysdev_driver_register(struct sysdev_class *, struct sysdev_driver *);
extern void sysdev_driver_unregister(struct sysdev_class *, struct sysdev_driver *);


/**
 * sys_devices can be simplified a lot from regular devices, because they're
 * simply not as versatile. 
 */

struct sys_device {
	u32		id;
	struct sysdev_class	* cls;
	struct kobject		kobj;
};

extern int sysdev_register(struct sys_device *);
extern void sysdev_unregister(struct sys_device *);


struct sysdev_attribute { 
	struct attribute	attr;
	ssize_t (*show)(struct sys_device *, struct sysdev_attribute *, char *);
	ssize_t (*store)(struct sys_device *, struct sysdev_attribute *,
			 const char *, size_t);
};


#define _SYSDEV_ATTR(_name, _mode, _show, _store)		\
{								\
	.attr = { .name = __stringify(_name), .mode = _mode },	\
	.show	= _show,					\
	.store	= _store,					\
}

#define SYSDEV_ATTR(_name, _mode, _show, _store)		\
	struct sysdev_attribute attr_##_name =			\
		_SYSDEV_ATTR(_name, _mode, _show, _store);

extern int sysdev_create_file(struct sys_device *, struct sysdev_attribute *);
extern void sysdev_remove_file(struct sys_device *, struct sysdev_attribute *);

struct sysdev_ext_attribute {
	struct sysdev_attribute attr;
	void *var;
};

/*
 * Support for simple variable sysdev attributes.
 * The pointer to the variable is stored in a sysdev_ext_attribute
 */

/* Add more types as needed */

extern ssize_t sysdev_show_ulong(struct sys_device *, struct sysdev_attribute *,
				char *);
extern ssize_t sysdev_store_ulong(struct sys_device *,
			struct sysdev_attribute *, const char *, size_t);
extern ssize_t sysdev_show_int(struct sys_device *, struct sysdev_attribute *,
				char *);
extern ssize_t sysdev_store_int(struct sys_device *,
			struct sysdev_attribute *, const char *, size_t);

#define _SYSDEV_ULONG_ATTR(_name, _mode, _var)				\
	{ _SYSDEV_ATTR(_name, _mode, sysdev_show_ulong, sysdev_store_ulong), \
	  &(_var) }
#define SYSDEV_ULONG_ATTR(_name, _mode, _var)			\
	struct sysdev_ext_attribute attr_##_name = 		\
		_SYSDEV_ULONG_ATTR(_name, _mode, _var);
#define _SYSDEV_INT_ATTR(_name, _mode, _var)				\
	{ _SYSDEV_ATTR(_name, _mode, sysdev_show_int, sysdev_store_int), \
	  &(_var) }
#define SYSDEV_INT_ATTR(_name, _mode, _var)			\
	struct sysdev_ext_attribute attr_##_name = 		\
		_SYSDEV_INT_ATTR(_name, _mode, _var);

#endif /* _SYSDEV_H_ */
