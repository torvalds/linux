/*
 * sysfs.h - definitions for the device driver filesystem
 *
 * Copyright (c) 2001,2002 Patrick Mochel
 * Copyright (c) 2004 Silicon Graphics, Inc.
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#ifndef _SYSFS_H_
#define _SYSFS_H_

#include <linux/compiler.h>
#include <linux/list.h>
#include <asm/atomic.h>

struct kobject;
struct module;
struct nameidata;
struct dentry;

struct attribute {
	const char		* name;
	struct module 		* owner;
	mode_t			mode;
};

struct attribute_group {
	const char		* name;
	struct attribute	** attrs;
};



/**
 * Use these macros to make defining attributes easier. See include/linux/device.h
 * for examples..
 */

#define __ATTR(_name,_mode,_show,_store) { \
	.attr = {.name = __stringify(_name), .mode = _mode, .owner = THIS_MODULE },	\
	.show	= _show,					\
	.store	= _store,					\
}

#define __ATTR_RO(_name) { \
	.attr	= { .name = __stringify(_name), .mode = 0444, .owner = THIS_MODULE },	\
	.show	= _name##_show,	\
}

#define __ATTR_NULL { .attr = { .name = NULL } }

#define attr_name(_attr) (_attr).attr.name

struct vm_area_struct;

struct bin_attribute {
	struct attribute	attr;
	size_t			size;
	void			*private;
	ssize_t (*read)(struct kobject *, char *, loff_t, size_t);
	ssize_t (*write)(struct kobject *, char *, loff_t, size_t);
	int (*mmap)(struct kobject *, struct bin_attribute *attr,
		    struct vm_area_struct *vma);
};

struct sysfs_ops {
	ssize_t	(*show)(struct kobject *, struct attribute *,char *);
	ssize_t	(*store)(struct kobject *,struct attribute *,const char *, size_t);
};

#define SYSFS_ROOT		0x0001
#define SYSFS_DIR		0x0002
#define SYSFS_KOBJ_ATTR 	0x0004
#define SYSFS_KOBJ_BIN_ATTR	0x0008
#define SYSFS_KOBJ_LINK 	0x0020
#define SYSFS_NOT_PINNED	(SYSFS_KOBJ_ATTR | SYSFS_KOBJ_BIN_ATTR | SYSFS_KOBJ_LINK)

#ifdef CONFIG_SYSFS

extern int __must_check
sysfs_create_dir(struct kobject *, struct dentry *);

extern void
sysfs_remove_dir(struct kobject *);

extern int __must_check
sysfs_rename_dir(struct kobject *, struct dentry *, const char *new_name);

extern int __must_check
sysfs_move_dir(struct kobject *, struct kobject *);

extern int __must_check
sysfs_create_file(struct kobject *, const struct attribute *);

extern int __must_check
sysfs_update_file(struct kobject *, const struct attribute *);

extern int __must_check
sysfs_chmod_file(struct kobject *kobj, struct attribute *attr, mode_t mode);

extern void
sysfs_remove_file(struct kobject *, const struct attribute *);

extern int __must_check
sysfs_create_link(struct kobject * kobj, struct kobject * target, const char * name);

extern void
sysfs_remove_link(struct kobject *, const char * name);

int __must_check sysfs_create_bin_file(struct kobject *kobj,
					struct bin_attribute *attr);
void sysfs_remove_bin_file(struct kobject *kobj, struct bin_attribute *attr);

int __must_check sysfs_create_group(struct kobject *,
					const struct attribute_group *);
void sysfs_remove_group(struct kobject *, const struct attribute_group *);
int sysfs_add_file_to_group(struct kobject *kobj,
		const struct attribute *attr, const char *group);
void sysfs_remove_file_from_group(struct kobject *kobj,
		const struct attribute *attr, const char *group);

void sysfs_notify(struct kobject * k, char *dir, char *attr);


extern int sysfs_make_shadowed_dir(struct kobject *kobj,
	void * (*follow_link)(struct dentry *, struct nameidata *));
extern struct dentry *sysfs_create_shadow_dir(struct kobject *kobj);
extern void sysfs_remove_shadow_dir(struct dentry *dir);

extern int __must_check sysfs_init(void);

#else /* CONFIG_SYSFS */

static inline int sysfs_create_dir(struct kobject * k, struct dentry *shadow)
{
	return 0;
}

static inline void sysfs_remove_dir(struct kobject * k)
{
	;
}

static inline int sysfs_rename_dir(struct kobject * k,
					struct dentry *new_parent,
					const char *new_name)
{
	return 0;
}

static inline int sysfs_move_dir(struct kobject * k, struct kobject * new_parent)
{
	return 0;
}

static inline int sysfs_create_file(struct kobject * k, const struct attribute * a)
{
	return 0;
}

static inline int sysfs_update_file(struct kobject * k, const struct attribute * a)
{
	return 0;
}
static inline int sysfs_chmod_file(struct kobject *kobj, struct attribute *attr, mode_t mode)
{
	return 0;
}

static inline void sysfs_remove_file(struct kobject * k, const struct attribute * a)
{
	;
}

static inline int sysfs_create_link(struct kobject * k, struct kobject * t, const char * n)
{
	return 0;
}

static inline void sysfs_remove_link(struct kobject * k, const char * name)
{
	;
}


static inline int sysfs_create_bin_file(struct kobject * k, struct bin_attribute * a)
{
	return 0;
}

static inline int sysfs_remove_bin_file(struct kobject * k, struct bin_attribute * a)
{
	return 0;
}

static inline int sysfs_create_group(struct kobject * k, const struct attribute_group *g)
{
	return 0;
}

static inline void sysfs_remove_group(struct kobject * k, const struct attribute_group * g)
{
	;
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

static inline void sysfs_notify(struct kobject * k, char *dir, char *attr)
{
}

static inline int sysfs_make_shadowed_dir(struct kobject *kobj,
	void * (*follow_link)(struct dentry *, struct nameidata *))
{
	return 0;
}

static inline int __must_check sysfs_init(void)
{
	return 0;
}

#endif /* CONFIG_SYSFS */

#endif /* _SYSFS_H_ */
