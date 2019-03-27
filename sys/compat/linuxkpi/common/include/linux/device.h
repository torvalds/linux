/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2016 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_LINUX_DEVICE_H_
#define	_LINUX_DEVICE_H_

#include <linux/err.h>
#include <linux/types.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <linux/kdev_t.h>
#include <asm/atomic.h>

#include <sys/bus.h>

struct device;
struct fwnode_handle;

struct class {
	const char	*name;
	struct module	*owner;
	struct kobject	kobj;
	devclass_t	bsdclass;
	const struct dev_pm_ops *pm;
	void		(*class_release)(struct class *class);
	void		(*dev_release)(struct device *dev);
	char *		(*devnode)(struct device *dev, umode_t *mode);
};

struct dev_pm_ops {
	int (*suspend)(struct device *dev);
	int (*suspend_late)(struct device *dev);
	int (*resume)(struct device *dev);
	int (*resume_early)(struct device *dev);
	int (*freeze)(struct device *dev);
	int (*freeze_late)(struct device *dev);
	int (*thaw)(struct device *dev);
	int (*thaw_early)(struct device *dev);
	int (*poweroff)(struct device *dev);
	int (*poweroff_late)(struct device *dev);
	int (*restore)(struct device *dev);
	int (*restore_early)(struct device *dev);
	int (*runtime_suspend)(struct device *dev);
	int (*runtime_resume)(struct device *dev);
	int (*runtime_idle)(struct device *dev);
};

struct device_driver {
	const char	*name;
	const struct dev_pm_ops *pm;
};

struct device_type {
	const char	*name;
};

struct device {
	struct device	*parent;
	struct list_head irqents;
	device_t	bsddev;
	/*
	 * The following flag is used to determine if the LinuxKPI is
	 * responsible for detaching the BSD device or not. If the
	 * LinuxKPI got the BSD device using devclass_get_device(), it
	 * must not try to detach or delete it, because it's already
	 * done somewhere else.
	 */
	bool		bsddev_attached_here;
	struct device_driver *driver;
	struct device_type *type;
	dev_t		devt;
	struct class	*class;
	void		(*release)(struct device *dev);
	struct kobject	kobj;
	uint64_t	*dma_mask;
	void		*driver_data;
	unsigned int	irq;
#define	LINUX_IRQ_INVALID	65535
	unsigned int	msix;
	unsigned int	msix_max;
	const struct attribute_group **groups;
	struct fwnode_handle *fwnode;

	spinlock_t	devres_lock;
	struct list_head devres_head;
};

extern struct device linux_root_device;
extern struct kobject linux_class_root;
extern const struct kobj_type linux_dev_ktype;
extern const struct kobj_type linux_class_ktype;

struct class_attribute {
	struct attribute attr;
	ssize_t (*show)(struct class *, struct class_attribute *, char *);
	ssize_t (*store)(struct class *, struct class_attribute *, const char *, size_t);
	const void *(*namespace)(struct class *, const struct class_attribute *);
};

#define	CLASS_ATTR(_name, _mode, _show, _store)				\
	struct class_attribute class_attr_##_name =			\
	    { { #_name, NULL, _mode }, _show, _store }

struct device_attribute {
	struct attribute	attr;
	ssize_t			(*show)(struct device *,
					struct device_attribute *, char *);
	ssize_t			(*store)(struct device *,
					struct device_attribute *, const char *,
					size_t);
};

#define	DEVICE_ATTR(_name, _mode, _show, _store)			\
	struct device_attribute dev_attr_##_name =			\
	    __ATTR(_name, _mode, _show, _store)
#define	DEVICE_ATTR_RO(_name)						\
	struct device_attribute dev_attr_##_name = __ATTR_RO(_name)
#define	DEVICE_ATTR_WO(_name)						\
	struct device_attribute dev_attr_##_name = __ATTR_WO(_name)
#define	DEVICE_ATTR_RW(_name)						\
	struct device_attribute dev_attr_##_name = __ATTR_RW(_name)

/* Simple class attribute that is just a static string */
struct class_attribute_string {
	struct class_attribute attr;
	char *str;
};

static inline ssize_t
show_class_attr_string(struct class *class,
				struct class_attribute *attr, char *buf)
{
	struct class_attribute_string *cs;
	cs = container_of(attr, struct class_attribute_string, attr);
	return snprintf(buf, PAGE_SIZE, "%s\n", cs->str);
}

/* Currently read-only only */
#define _CLASS_ATTR_STRING(_name, _mode, _str) \
	{ __ATTR(_name, _mode, show_class_attr_string, NULL), _str }
#define CLASS_ATTR_STRING(_name, _mode, _str) \
	struct class_attribute_string class_attr_##_name = \
		_CLASS_ATTR_STRING(_name, _mode, _str)

#define	dev_err(dev, fmt, ...)	device_printf((dev)->bsddev, fmt, ##__VA_ARGS__)
#define	dev_warn(dev, fmt, ...)	device_printf((dev)->bsddev, fmt, ##__VA_ARGS__)
#define	dev_info(dev, fmt, ...)	device_printf((dev)->bsddev, fmt, ##__VA_ARGS__)
#define	dev_notice(dev, fmt, ...)	device_printf((dev)->bsddev, fmt, ##__VA_ARGS__)
#define	dev_dbg(dev, fmt, ...)	do { } while (0)
#define	dev_printk(lvl, dev, fmt, ...)					\
	    device_printf((dev)->bsddev, fmt, ##__VA_ARGS__)

#define	dev_err_once(dev, ...) do {		\
	static bool __dev_err_once;		\
	if (!__dev_err_once) {			\
		__dev_err_once = 1;		\
		dev_err(dev, __VA_ARGS__);	\
	}					\
} while (0)

#define	dev_err_ratelimited(dev, ...) do {	\
	static linux_ratelimit_t __ratelimited;	\
	if (linux_ratelimited(&__ratelimited))	\
		dev_err(dev, __VA_ARGS__);	\
} while (0)

#define	dev_warn_ratelimited(dev, ...) do {	\
	static linux_ratelimit_t __ratelimited;	\
	if (linux_ratelimited(&__ratelimited))	\
		dev_warn(dev, __VA_ARGS__);	\
} while (0)

static inline void *
dev_get_drvdata(const struct device *dev)
{

	return dev->driver_data;
}

static inline void
dev_set_drvdata(struct device *dev, void *data)
{

	dev->driver_data = data;
}

static inline struct device *
get_device(struct device *dev)
{

	if (dev)
		kobject_get(&dev->kobj);

	return (dev);
}

static inline char *
dev_name(const struct device *dev)
{

	return kobject_name(&dev->kobj);
}

#define	dev_set_name(_dev, _fmt, ...)					\
	kobject_set_name(&(_dev)->kobj, (_fmt), ##__VA_ARGS__)

static inline void
put_device(struct device *dev)
{

	if (dev)
		kobject_put(&dev->kobj);
}

static inline int
class_register(struct class *class)
{

	class->bsdclass = devclass_create(class->name);
	kobject_init(&class->kobj, &linux_class_ktype);
	kobject_set_name(&class->kobj, class->name);
	kobject_add(&class->kobj, &linux_class_root, class->name);

	return (0);
}

static inline void
class_unregister(struct class *class)
{

	kobject_put(&class->kobj);
}

static inline struct device *kobj_to_dev(struct kobject *kobj)
{
	return container_of(kobj, struct device, kobj);
}

/*
 * Devices are registered and created for exporting to sysfs. Create
 * implies register and register assumes the device fields have been
 * setup appropriately before being called.
 */
static inline void
device_initialize(struct device *dev)
{
	device_t bsddev = NULL;
	int unit = -1;

	if (dev->devt) {
		unit = MINOR(dev->devt);
		bsddev = devclass_get_device(dev->class->bsdclass, unit);
		dev->bsddev_attached_here = false;
	} else if (dev->parent == NULL) {
		bsddev = devclass_get_device(dev->class->bsdclass, 0);
		dev->bsddev_attached_here = false;
	} else {
		dev->bsddev_attached_here = true;
	}

	if (bsddev == NULL && dev->parent != NULL) {
		bsddev = device_add_child(dev->parent->bsddev,
		    dev->class->kobj.name, unit);
	}

	if (bsddev != NULL)
		device_set_softc(bsddev, dev);

	dev->bsddev = bsddev;
	MPASS(dev->bsddev != NULL);
	kobject_init(&dev->kobj, &linux_dev_ktype);

	spin_lock_init(&dev->devres_lock);
	INIT_LIST_HEAD(&dev->devres_head);
}

static inline int
device_add(struct device *dev)
{
	if (dev->bsddev != NULL) {
		if (dev->devt == 0)
			dev->devt = makedev(0, device_get_unit(dev->bsddev));
	}
	kobject_add(&dev->kobj, &dev->class->kobj, dev_name(dev));
	return (0);
}

static inline void
device_create_release(struct device *dev)
{
	kfree(dev);
}

static inline struct device *
device_create_groups_vargs(struct class *class, struct device *parent,
    dev_t devt, void *drvdata, const struct attribute_group **groups,
    const char *fmt, va_list args)
{
	struct device *dev = NULL;
	int retval = -ENODEV;

	if (class == NULL || IS_ERR(class))
		goto error;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		retval = -ENOMEM;
		goto error;
	}

	dev->devt = devt;
	dev->class = class;
	dev->parent = parent;
	dev->groups = groups;
	dev->release = device_create_release;
	/* device_initialize() needs the class and parent to be set */
	device_initialize(dev);
	dev_set_drvdata(dev, drvdata);

	retval = kobject_set_name_vargs(&dev->kobj, fmt, args);
	if (retval)
		goto error;

	retval = device_add(dev);
	if (retval)
		goto error;

	return dev;

error:
	put_device(dev);
	return ERR_PTR(retval);
}

static inline struct device *
device_create_with_groups(struct class *class,
    struct device *parent, dev_t devt, void *drvdata,
    const struct attribute_group **groups, const char *fmt, ...)
{
	va_list vargs;
	struct device *dev;

	va_start(vargs, fmt);
	dev = device_create_groups_vargs(class, parent, devt, drvdata,
	    groups, fmt, vargs);
	va_end(vargs);
	return dev;
}

static inline bool
device_is_registered(struct device *dev)
{

	return (dev->bsddev != NULL);
}

static inline int
device_register(struct device *dev)
{
	device_t bsddev = NULL;
	int unit = -1;

	if (device_is_registered(dev))
		goto done;

	if (dev->devt) {
		unit = MINOR(dev->devt);
		bsddev = devclass_get_device(dev->class->bsdclass, unit);
		dev->bsddev_attached_here = false;
	} else if (dev->parent == NULL) {
		bsddev = devclass_get_device(dev->class->bsdclass, 0);
		dev->bsddev_attached_here = false;
	} else {
		dev->bsddev_attached_here = true;
	}
	if (bsddev == NULL && dev->parent != NULL) {
		bsddev = device_add_child(dev->parent->bsddev,
		    dev->class->kobj.name, unit);
	}
	if (bsddev != NULL) {
		if (dev->devt == 0)
			dev->devt = makedev(0, device_get_unit(bsddev));
		device_set_softc(bsddev, dev);
	}
	dev->bsddev = bsddev;
done:
	kobject_init(&dev->kobj, &linux_dev_ktype);
	kobject_add(&dev->kobj, &dev->class->kobj, dev_name(dev));

	return (0);
}

static inline void
device_unregister(struct device *dev)
{
	device_t bsddev;

	bsddev = dev->bsddev;
	dev->bsddev = NULL;

	if (bsddev != NULL && dev->bsddev_attached_here) {
		mtx_lock(&Giant);
		device_delete_child(device_get_parent(bsddev), bsddev);
		mtx_unlock(&Giant);
	}
	put_device(dev);
}

static inline void
device_del(struct device *dev)
{
	device_t bsddev;

	bsddev = dev->bsddev;
	dev->bsddev = NULL;

	if (bsddev != NULL && dev->bsddev_attached_here) {
		mtx_lock(&Giant);
		device_delete_child(device_get_parent(bsddev), bsddev);
		mtx_unlock(&Giant);
	}
}

struct device *device_create(struct class *class, struct device *parent,
	    dev_t devt, void *drvdata, const char *fmt, ...);

static inline void
device_destroy(struct class *class, dev_t devt)
{
	device_t bsddev;
	int unit;

	unit = MINOR(devt);
	bsddev = devclass_get_device(class->bsdclass, unit);
	if (bsddev != NULL)
		device_unregister(device_get_softc(bsddev));
}

#define	dev_pm_set_driver_flags(dev, flags) do { \
} while (0)

static inline void
linux_class_kfree(struct class *class)
{

	kfree(class);
}

static inline struct class *
class_create(struct module *owner, const char *name)
{
	struct class *class;
	int error;

	class = kzalloc(sizeof(*class), M_WAITOK);
	class->owner = owner;
	class->name = name;
	class->class_release = linux_class_kfree;
	error = class_register(class);
	if (error) {
		kfree(class);
		return (NULL);
	}

	return (class);
}

static inline void
class_destroy(struct class *class)
{

	if (class == NULL)
		return;
	class_unregister(class);
}

static inline int
device_create_file(struct device *dev, const struct device_attribute *attr)
{

	if (dev)
		return sysfs_create_file(&dev->kobj, &attr->attr);
	return -EINVAL;
}

static inline void
device_remove_file(struct device *dev, const struct device_attribute *attr)
{

	if (dev)
		sysfs_remove_file(&dev->kobj, &attr->attr);
}

static inline int
class_create_file(struct class *class, const struct class_attribute *attr)
{

	if (class)
		return sysfs_create_file(&class->kobj, &attr->attr);
	return -EINVAL;
}

static inline void
class_remove_file(struct class *class, const struct class_attribute *attr)
{

	if (class)
		sysfs_remove_file(&class->kobj, &attr->attr);
}

static inline int
dev_to_node(struct device *dev)
{
	return -1;
}

char *kvasprintf(gfp_t, const char *, va_list);
char *kasprintf(gfp_t, const char *, ...);

#endif	/* _LINUX_DEVICE_H_ */
