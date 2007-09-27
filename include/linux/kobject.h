/*
 * kobject.h - generic kernel object infrastructure.
 *
 * Copyright (c) 2002-2003 Patrick Mochel
 * Copyright (c) 2002-2003 Open Source Development Labs
 * Copyright (c) 2006-2007 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (c) 2006-2007 Novell Inc.
 *
 * This file is released under the GPLv2.
 *
 * 
 * Please read Documentation/kobject.txt before using the kobject
 * interface, ESPECIALLY the parts about reference counts and object
 * destructors. 
 */

#ifndef _KOBJECT_H_
#define _KOBJECT_H_

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/list.h>
#include <linux/sysfs.h>
#include <linux/compiler.h>
#include <linux/spinlock.h>
#include <linux/kref.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <asm/atomic.h>

#define KOBJ_NAME_LEN			20
#define UEVENT_HELPER_PATH_LEN		256
#define UEVENT_NUM_ENVP			32	/* number of env pointers */
#define UEVENT_BUFFER_SIZE		2048	/* buffer for the variables */

/* path to the userspace helper executed on an event */
extern char uevent_helper[];

/* counter to tag the uevent, read only except for the kobject core */
extern u64 uevent_seqnum;

/*
 * The actions here must match the index to the string array
 * in lib/kobject_uevent.c
 *
 * Do not add new actions here without checking with the driver-core
 * maintainers. Action strings are not meant to express subsystem
 * or device specific properties. In most cases you want to send a
 * kobject_uevent_env(kobj, KOBJ_CHANGE, env) with additional event
 * specific variables added to the event environment.
 */
enum kobject_action {
	KOBJ_ADD,
	KOBJ_REMOVE,
	KOBJ_CHANGE,
	KOBJ_MOVE,
	KOBJ_ONLINE,
	KOBJ_OFFLINE,
	KOBJ_MAX
};

struct kobject {
	const char		* k_name;
	struct kref		kref;
	struct list_head	entry;
	struct kobject		* parent;
	struct kset		* kset;
	struct kobj_type	* ktype;
	struct sysfs_dirent	* sd;
};

extern int kobject_set_name(struct kobject *, const char *, ...)
	__attribute__((format(printf,2,3)));

static inline const char * kobject_name(const struct kobject * kobj)
{
	return kobj->k_name;
}

extern void kobject_init(struct kobject *);
extern void kobject_cleanup(struct kobject *);

extern int __must_check kobject_add(struct kobject *);
extern void kobject_del(struct kobject *);

extern int __must_check kobject_rename(struct kobject *, const char *new_name);
extern int __must_check kobject_move(struct kobject *, struct kobject *);

extern int __must_check kobject_register(struct kobject *);
extern void kobject_unregister(struct kobject *);

extern struct kobject * kobject_get(struct kobject *);
extern void kobject_put(struct kobject *);

extern struct kobject *kobject_kset_add_dir(struct kset *kset,
					    struct kobject *, const char *);
extern struct kobject *kobject_add_dir(struct kobject *, const char *);

extern char * kobject_get_path(struct kobject *, gfp_t);

struct kobj_type {
	void (*release)(struct kobject *);
	struct sysfs_ops	* sysfs_ops;
	struct attribute	** default_attrs;
};

struct kobj_uevent_env {
	char *envp[UEVENT_NUM_ENVP];
	int envp_idx;
	char buf[UEVENT_BUFFER_SIZE];
	int buflen;
};

struct kset_uevent_ops {
	int (*filter)(struct kset *kset, struct kobject *kobj);
	const char *(*name)(struct kset *kset, struct kobject *kobj);
	int (*uevent)(struct kset *kset, struct kobject *kobj,
		      struct kobj_uevent_env *env);
};

/**
 * struct kset - a set of kobjects of a specific type, belonging to a specific subsystem.
 *
 * A kset defines a group of kobjects.  They can be individually
 * different "types" but overall these kobjects all want to be grouped
 * together and operated on in the same manner.  ksets are used to
 * define the attribute callbacks and other common events that happen to
 * a kobject.
 *
 * @ktype: the struct kobj_type for this specific kset
 * @list: the list of all kobjects for this kset
 * @list_lock: a lock for iterating over the kobjects
 * @kobj: the embedded kobject for this kset (recursion, isn't it fun...)
 * @uevent_ops: the set of uevent operations for this kset.  These are
 * called whenever a kobject has something happen to it so that the kset
 * can add new environment variables, or filter out the uevents if so
 * desired.
 */
struct kset {
	struct kobj_type	*ktype;
	struct list_head	list;
	spinlock_t		list_lock;
	struct kobject		kobj;
	struct kset_uevent_ops	*uevent_ops;
};


extern void kset_init(struct kset * k);
extern int __must_check kset_add(struct kset * k);
extern int __must_check kset_register(struct kset * k);
extern void kset_unregister(struct kset * k);

static inline struct kset * to_kset(struct kobject * kobj)
{
	return kobj ? container_of(kobj,struct kset,kobj) : NULL;
}

static inline struct kset * kset_get(struct kset * k)
{
	return k ? to_kset(kobject_get(&k->kobj)) : NULL;
}

static inline void kset_put(struct kset * k)
{
	kobject_put(&k->kobj);
}

static inline struct kobj_type * get_ktype(struct kobject * k)
{
	if (k->kset && k->kset->ktype)
		return k->kset->ktype;
	else 
		return k->ktype;
}

extern struct kobject * kset_find_obj(struct kset *, const char *);


/*
 * Use this when initializing an embedded kset with no other 
 * fields to initialize.
 */
#define set_kset_name(str)	.kset = { .kobj = { .k_name = str } }


#define decl_subsys(_name,_type,_uevent_ops) \
struct kset _name##_subsys = { \
	.kobj = { .k_name = __stringify(_name) }, \
	.ktype = _type, \
	.uevent_ops =_uevent_ops, \
}
#define decl_subsys_name(_varname,_name,_type,_uevent_ops) \
struct kset _varname##_subsys = { \
	.kobj = { .k_name = __stringify(_name) }, \
	.ktype = _type, \
	.uevent_ops =_uevent_ops, \
}

/* The global /sys/kernel/ subsystem for people to chain off of */
extern struct kset kernel_subsys;
/* The global /sys/hypervisor/ subsystem  */
extern struct kset hypervisor_subsys;

/*
 * Helpers for setting the kset of registered objects.
 * Often, a registered object belongs to a kset embedded in a 
 * subsystem. These do no magic, just make the resulting code
 * easier to follow. 
 */

/**
 *	kobj_set_kset_s(obj,subsys) - set kset for embedded kobject.
 *	@obj:		ptr to some object type.
 *	@subsys:	a subsystem object (not a ptr).
 *
 *	Can be used for any object type with an embedded ->kobj.
 */

#define kobj_set_kset_s(obj,subsys) \
	(obj)->kobj.kset = &(subsys)

extern int __must_check subsystem_register(struct kset *);
extern void subsystem_unregister(struct kset *);

struct subsys_attribute {
	struct attribute attr;
	ssize_t (*show)(struct kset *, char *);
	ssize_t (*store)(struct kset *, const char *, size_t);
};

extern int __must_check subsys_create_file(struct kset *,
					struct subsys_attribute *);

#if defined(CONFIG_HOTPLUG)
int kobject_uevent(struct kobject *kobj, enum kobject_action action);
int kobject_uevent_env(struct kobject *kobj, enum kobject_action action,
			char *envp[]);

int add_uevent_var(struct kobj_uevent_env *env, const char *format, ...)
	__attribute__((format (printf, 2, 3)));

int kobject_action_type(const char *buf, size_t count,
			enum kobject_action *type);
#else
static inline int kobject_uevent(struct kobject *kobj, enum kobject_action action)
{ return 0; }
static inline int kobject_uevent_env(struct kobject *kobj,
				      enum kobject_action action,
				      char *envp[])
{ return 0; }

static inline int add_uevent_var(struct kobj_uevent_env *env, const char *format, ...)
{ return 0; }

static inline int kobject_action_type(const char *buf, size_t count,
			enum kobject_action *type)
{ return -EINVAL; }
#endif

#endif /* __KERNEL__ */
#endif /* _KOBJECT_H_ */
