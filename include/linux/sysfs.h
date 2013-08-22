/*
 * sysfs.h - definitions for the device driver filesystem
 *
 * Copyright (c) 2001,2002 Patrick Mochel
 * Copyright (c) 2004 Silicon Graphics, Inc.
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#ifndef _SYSFS_H_
#define _SYSFS_H_

#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/kobject_ns.h>
#include <linux/stat.h>
#include <linux/atomic.h>

struct kobject;
struct module;
struct bin_attribute;
enum kobj_ns_type;

struct attribute {
	const char		*name;
	umode_t			mode;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	bool			ignore_lockdep:1;
	struct lock_class_key	*key;
	struct lock_class_key	skey;
#endif
};

/**
 *	sysfs_attr_init - initialize a dynamically allocated sysfs attribute
 *	@attr: struct attribute to initialize
 *
 *	Initialize a dynamically allocated struct attribute so we can
 *	make lockdep happy.  This is a new requirement for attributes
 *	and initially this is only needed when lockdep is enabled.
 *	Lockdep gives a nice error when your attribute is added to
 *	sysfs if you don't have this.
 */
#ifdef CONFIG_DEBUG_LOCK_ALLOC
#define sysfs_attr_init(attr)				\
do {							\
	static struct lock_class_key __key;		\
							\
	(attr)->key = &__key;				\
} while (0)
#else
#define sysfs_attr_init(attr) do {} while (0)
#endif

struct attribute_group {
	const char		*name;
	umode_t			(*is_visible)(struct kobject *,
					      struct attribute *, int);
	struct attribute	**attrs;
	struct bin_attribute	**bin_attrs;
};

/**
 * Use these macros to make defining attributes easier. See include/linux/device.h
 * for examples..
 */

#define __ATTR(_name, _mode, _show, _store) {				\
	.attr = {.name = __stringify(_name), .mode = _mode },		\
	.show	= _show,						\
	.store	= _store,						\
}

#define __ATTR_RO(_name) {						\
	.attr	= { .name = __stringify(_name), .mode = S_IRUGO },	\
	.show	= _name##_show,						\
}

#define __ATTR_RW(_name) __ATTR(_name, (S_IWUSR | S_IRUGO),		\
			 _name##_show, _name##_store)

#define __ATTR_NULL { .attr = { .name = NULL } }

#ifdef CONFIG_DEBUG_LOCK_ALLOC
#define __ATTR_IGNORE_LOCKDEP(_name, _mode, _show, _store) {	\
	.attr = {.name = __stringify(_name), .mode = _mode,	\
			.ignore_lockdep = true },		\
	.show		= _show,				\
	.store		= _store,				\
}
#else
#define __ATTR_IGNORE_LOCKDEP	__ATTR
#endif

#define __ATTRIBUTE_GROUPS(_name)				\
static const struct attribute_group *_name##_groups[] = {	\
	&_name##_group,						\
	NULL,							\
}

#define ATTRIBUTE_GROUPS(_name)					\
static const struct attribute_group _name##_group = {		\
	.attrs = _name##_attrs,					\
};								\
__ATTRIBUTE_GROUPS(_name)

#define attr_name(_attr) (_attr).attr.name

struct file;
struct vm_area_struct;

struct bin_attribute {
	struct attribute	attr;
	size_t			size;
	void			*private;
	ssize_t (*read)(struct file *, struct kobject *, struct bin_attribute *,
			char *, loff_t, size_t);
	ssize_t (*write)(struct file *, struct kobject *, struct bin_attribute *,
			 char *, loff_t, size_t);
	int (*mmap)(struct file *, struct kobject *, struct bin_attribute *attr,
		    struct vm_area_struct *vma);
};

/**
 *	sysfs_bin_attr_init - initialize a dynamically allocated bin_attribute
 *	@attr: struct bin_attribute to initialize
 *
 *	Initialize a dynamically allocated struct bin_attribute so we
 *	can make lockdep happy.  This is a new requirement for
 *	attributes and initially this is only needed when lockdep is
 *	enabled.  Lockdep gives a nice error when your attribute is
 *	added to sysfs if you don't have this.
 */
#define sysfs_bin_attr_init(bin_attr) sysfs_attr_init(&(bin_attr)->attr)

/* macros to create static binary attributes easier */
#define __BIN_ATTR(_name, _mode, _read, _write, _size) {		\
	.attr = { .name = __stringify(_name), .mode = _mode },		\
	.read	= _read,						\
	.write	= _write,						\
	.size	= _size,						\
}

#define __BIN_ATTR_RO(_name, _size) {					\
	.attr	= { .name = __stringify(_name), .mode = S_IRUGO },	\
	.read	= _name##_read,						\
	.size	= _size,						\
}

#define __BIN_ATTR_RW(_name, _size) __BIN_ATTR(_name,			\
				   (S_IWUSR | S_IRUGO), _name##_read,	\
				   _name##_write, _size)

#define __BIN_ATTR_NULL __ATTR_NULL

#define BIN_ATTR(_name, _mode, _read, _write, _size)			\
struct bin_attribute bin_attr_##_name = __BIN_ATTR(_name, _mode, _read,	\
					_write, _size)

#define BIN_ATTR_RO(_name, _size)					\
struct bin_attribute bin_attr_##_name = __BIN_ATTR_RO(_name, _size)

#define BIN_ATTR_RW(_name, _size)					\
struct bin_attribute bin_attr_##_name = __BIN_ATTR_RW(_name, _size)

struct sysfs_ops {
	ssize_t	(*show)(struct kobject *, struct attribute *, char *);
	ssize_t	(*store)(struct kobject *, struct attribute *, const char *, size_t);
	const void *(*namespace)(struct kobject *, const struct attribute *);
};

struct sysfs_dirent;

#ifdef CONFIG_SYSFS

int sysfs_schedule_callback(struct kobject *kobj, void (*func)(void *),
			    void *data, struct module *owner);

int __must_check sysfs_create_dir(struct kobject *kobj);
void sysfs_remove_dir(struct kobject *kobj);
int __must_check sysfs_rename_dir(struct kobject *kobj, const char *new_name);
int __must_check sysfs_move_dir(struct kobject *kobj,
				struct kobject *new_parent_kobj);

int __must_check sysfs_create_file(struct kobject *kobj,
				   const struct attribute *attr);
int __must_check sysfs_create_files(struct kobject *kobj,
				   const struct attribute **attr);
int __must_check sysfs_chmod_file(struct kobject *kobj,
				  const struct attribute *attr, umode_t mode);
void sysfs_remove_file(struct kobject *kobj, const struct attribute *attr);
void sysfs_remove_files(struct kobject *kobj, const struct attribute **attr);

int __must_check sysfs_create_bin_file(struct kobject *kobj,
				       const struct bin_attribute *attr);
void sysfs_remove_bin_file(struct kobject *kobj,
			   const struct bin_attribute *attr);

int __must_check sysfs_create_link(struct kobject *kobj, struct kobject *target,
				   const char *name);
int __must_check sysfs_create_link_nowarn(struct kobject *kobj,
					  struct kobject *target,
					  const char *name);
void sysfs_remove_link(struct kobject *kobj, const char *name);

int sysfs_rename_link(struct kobject *kobj, struct kobject *target,
			const char *old_name, const char *new_name);

void sysfs_delete_link(struct kobject *dir, struct kobject *targ,
			const char *name);

int __must_check sysfs_create_group(struct kobject *kobj,
				    const struct attribute_group *grp);
int __must_check sysfs_create_groups(struct kobject *kobj,
				     const struct attribute_group **groups);
int sysfs_update_group(struct kobject *kobj,
		       const struct attribute_group *grp);
void sysfs_remove_group(struct kobject *kobj,
			const struct attribute_group *grp);
void sysfs_remove_groups(struct kobject *kobj,
			 const struct attribute_group **groups);
int sysfs_add_file_to_group(struct kobject *kobj,
			const struct attribute *attr, const char *group);
void sysfs_remove_file_from_group(struct kobject *kobj,
			const struct attribute *attr, const char *group);
int sysfs_merge_group(struct kobject *kobj,
		       const struct attribute_group *grp);
void sysfs_unmerge_group(struct kobject *kobj,
		       const struct attribute_group *grp);
int sysfs_add_link_to_group(struct kobject *kobj, const char *group_name,
			    struct kobject *target, const char *link_name);
void sysfs_remove_link_from_group(struct kobject *kobj, const char *group_name,
				  const char *link_name);

void sysfs_notify(struct kobject *kobj, const char *dir, const char *attr);
void sysfs_notify_dirent(struct sysfs_dirent *sd);
struct sysfs_dirent *sysfs_get_dirent(struct sysfs_dirent *parent_sd,
				      const void *ns,
				      const unsigned char *name);
struct sysfs_dirent *sysfs_get(struct sysfs_dirent *sd);
void sysfs_put(struct sysfs_dirent *sd);

int __must_check sysfs_init(void);

#else /* CONFIG_SYSFS */

static inline int sysfs_schedule_callback(struct kobject *kobj,
		void (*func)(void *), void *data, struct module *owner)
{
	return -ENOSYS;
}

static inline int sysfs_create_dir(struct kobject *kobj)
{
	return 0;
}

static inline void sysfs_remove_dir(struct kobject *kobj)
{
}

static inline int sysfs_rename_dir(struct kobject *kobj, const char *new_name)
{
	return 0;
}

static inline int sysfs_move_dir(struct kobject *kobj,
				 struct kobject *new_parent_kobj)
{
	return 0;
}

static inline int sysfs_create_file(struct kobject *kobj,
				    const struct attribute *attr)
{
	return 0;
}

static inline int sysfs_create_files(struct kobject *kobj,
				    const struct attribute **attr)
{
	return 0;
}

static inline int sysfs_chmod_file(struct kobject *kobj,
				   const struct attribute *attr, umode_t mode)
{
	return 0;
}

static inline void sysfs_remove_file(struct kobject *kobj,
				     const struct attribute *attr)
{
}

static inline void sysfs_remove_files(struct kobject *kobj,
				     const struct attribute **attr)
{
}

static inline int sysfs_create_bin_file(struct kobject *kobj,
					const struct bin_attribute *attr)
{
	return 0;
}

static inline void sysfs_remove_bin_file(struct kobject *kobj,
					 const struct bin_attribute *attr)
{
}

static inline int sysfs_create_link(struct kobject *kobj,
				    struct kobject *target, const char *name)
{
	return 0;
}

static inline int sysfs_create_link_nowarn(struct kobject *kobj,
					   struct kobject *target,
					   const char *name)
{
	return 0;
}

static inline void sysfs_remove_link(struct kobject *kobj, const char *name)
{
}

static inline int sysfs_rename_link(struct kobject *k, struct kobject *t,
				    const char *old_name, const char *new_name)
{
	return 0;
}

static inline void sysfs_delete_link(struct kobject *k, struct kobject *t,
				     const char *name)
{
}

static inline int sysfs_create_group(struct kobject *kobj,
				     const struct attribute_group *grp)
{
	return 0;
}

static inline int sysfs_update_group(struct kobject *kobj,
				const struct attribute_group *grp)
{
	return 0;
}

static inline void sysfs_remove_group(struct kobject *kobj,
				      const struct attribute_group *grp)
{
}

static inline int sysfs_add_file_to_group(struct kobject *kobj,
		const struct attribute *attr, const char *group)
{
	return 0;
}

static inline void sysfs_remove_file_from_group(struct kobject *kobj,
		const struct attribute *attr, const char *group)
{
}

static inline int sysfs_merge_group(struct kobject *kobj,
		       const struct attribute_group *grp)
{
	return 0;
}

static inline void sysfs_unmerge_group(struct kobject *kobj,
		       const struct attribute_group *grp)
{
}

static inline int sysfs_add_link_to_group(struct kobject *kobj,
		const char *group_name, struct kobject *target,
		const char *link_name)
{
	return 0;
}

static inline void sysfs_remove_link_from_group(struct kobject *kobj,
		const char *group_name, const char *link_name)
{
}

static inline void sysfs_notify(struct kobject *kobj, const char *dir,
				const char *attr)
{
}
static inline void sysfs_notify_dirent(struct sysfs_dirent *sd)
{
}
static inline
struct sysfs_dirent *sysfs_get_dirent(struct sysfs_dirent *parent_sd,
				      const void *ns,
				      const unsigned char *name)
{
	return NULL;
}
static inline struct sysfs_dirent *sysfs_get(struct sysfs_dirent *sd)
{
	return NULL;
}
static inline void sysfs_put(struct sysfs_dirent *sd)
{
}

static inline int __must_check sysfs_init(void)
{
	return 0;
}

#endif /* CONFIG_SYSFS */

#endif /* _SYSFS_H_ */
