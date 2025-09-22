/* Public domain. */

#ifndef _LINUX_KOBJECT_H
#define _LINUX_KOBJECT_H

#include <linux/kref.h>
#include <linux/sysfs.h>
#include <linux/container_of.h>

struct kobject {
	struct kref kref;
	struct kobj_type *type;
};

struct kobj_type {
	void (*release)(struct kobject *);
};

struct kobj_attribute {
};

static inline void
kobject_init(struct kobject *obj, struct kobj_type *type)
{
	kref_init(&obj->kref);
	obj->type = type;
}

static inline int
kobject_init_and_add(struct kobject *obj, struct kobj_type *type,
    struct kobject *parent, const char *fmt, ...)
{
	kobject_init(obj, type);
	return (0);
}

static inline struct kobject *
kobject_get(struct kobject *obj)
{
	if (obj != NULL)
		kref_get(&obj->kref);
	return (obj);
}

static inline void
kobject_release(struct kref *ref)
{
	struct kobject *obj = container_of(ref, struct kobject, kref);
	if (obj->type && obj->type->release)
		obj->type->release(obj);
}

static inline void
kobject_put(struct kobject *obj)
{
	if (obj != NULL)
		kref_put(&obj->kref, kobject_release);
}

static inline void
kobject_del(struct kobject *obj)
{
}

#define kobject_uevent_env(obj, act, envp)

#endif
