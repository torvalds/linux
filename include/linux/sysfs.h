/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sysfs.h - definitions for the device driver filesystem
 *
 * Copyright (c) 2001,2002 Patrick Mochel
 * Copyright (c) 2004 Silicon Graphics, Inc.
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * Please see Documentation/filesystems/sysfs.rst for more information.
 */

#ifndef _SYSFS_H_
#define _SYSFS_H_

#include <linux/kernfs.h>
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

/**
 * struct attribute_group - data structure used to declare an attribute group.
 * @name:	Optional: Attribute group name
 *		If specified, the attribute group will be created in a
 *		new subdirectory with this name. Additionally when a
 *		group is named, @is_visible and @is_bin_visible may
 *		return SYSFS_GROUP_INVISIBLE to control visibility of
 *		the directory itself.
 * @is_visible:	Optional: Function to return permissions associated with an
 *		attribute of the group. Will be called repeatedly for
 *		each non-binary attribute in the group. Only read/write
 *		permissions as well as SYSFS_PREALLOC are accepted. Must
 *		return 0 if an attribute is not visible. The returned
 *		value will replace static permissions defined in struct
 *		attribute. Use SYSFS_GROUP_VISIBLE() when assigning this
 *		callback to specify separate _group_visible() and
 *		_attr_visible() handlers.
 * @is_bin_visible:
 *		Optional: Function to return permissions associated with a
 *		binary attribute of the group. Will be called repeatedly
 *		for each binary attribute in the group. Only read/write
 *		permissions as well as SYSFS_PREALLOC (and the
 *		visibility flags for named groups) are accepted. Must
 *		return 0 if a binary attribute is not visible. The
 *		returned value will replace static permissions defined
 *		in struct bin_attribute. If @is_visible is not set, Use
 *		SYSFS_GROUP_VISIBLE() when assigning this callback to
 *		specify separate _group_visible() and _attr_visible()
 *		handlers.
 * @attrs:	Pointer to NULL terminated list of attributes.
 * @bin_attrs:	Pointer to NULL terminated list of binary attributes.
 *		Either attrs or bin_attrs or both must be provided.
 */
struct attribute_group {
	const char		*name;
	umode_t			(*is_visible)(struct kobject *,
					      struct attribute *, int);
	umode_t			(*is_bin_visible)(struct kobject *,
						  struct bin_attribute *, int);
	struct attribute	**attrs;
	struct bin_attribute	**bin_attrs;
};

#define SYSFS_PREALLOC		010000
#define SYSFS_GROUP_INVISIBLE	020000

/*
 * DEFINE_SYSFS_GROUP_VISIBLE(name):
 *	A helper macro to pair with the assignment of ".is_visible =
 *	SYSFS_GROUP_VISIBLE(name)", that arranges for the directory
 *	associated with a named attribute_group to optionally be hidden.
 *	This allows for static declaration of attribute_groups, and the
 *	simplification of attribute visibility lifetime that implies,
 *	without polluting sysfs with empty attribute directories.
 * Ex.
 *
 * static umode_t example_attr_visible(struct kobject *kobj,
 *                                   struct attribute *attr, int n)
 * {
 *       if (example_attr_condition)
 *               return 0;
 *       else if (ro_attr_condition)
 *               return 0444;
 *       return a->mode;
 * }
 *
 * static bool example_group_visible(struct kobject *kobj)
 * {
 *       if (example_group_condition)
 *               return false;
 *       return true;
 * }
 *
 * DEFINE_SYSFS_GROUP_VISIBLE(example);
 *
 * static struct attribute_group example_group = {
 *       .name = "example",
 *       .is_visible = SYSFS_GROUP_VISIBLE(example),
 *       .attrs = &example_attrs,
 * };
 *
 * Note that it expects <name>_attr_visible and <name>_group_visible to
 * be defined. For cases where individual attributes do not need
 * separate visibility consideration, only entire group visibility at
 * once, see DEFINE_SIMPLE_SYSFS_GROUP_VISIBLE().
 */
#define DEFINE_SYSFS_GROUP_VISIBLE(name)                             \
	static inline umode_t sysfs_group_visible_##name(            \
		struct kobject *kobj, struct attribute *attr, int n) \
	{                                                            \
		if (n == 0 && !name##_group_visible(kobj))           \
			return SYSFS_GROUP_INVISIBLE;                \
		return name##_attr_visible(kobj, attr, n);           \
	}

/*
 * DEFINE_SIMPLE_SYSFS_GROUP_VISIBLE(name):
 *	A helper macro to pair with SYSFS_GROUP_VISIBLE() that like
 *	DEFINE_SYSFS_GROUP_VISIBLE() controls group visibility, but does
 *	not require the implementation of a per-attribute visibility
 *	callback.
 * Ex.
 *
 * static bool example_group_visible(struct kobject *kobj)
 * {
 *       if (example_group_condition)
 *               return false;
 *       return true;
 * }
 *
 * DEFINE_SIMPLE_SYSFS_GROUP_VISIBLE(example);
 *
 * static struct attribute_group example_group = {
 *       .name = "example",
 *       .is_visible = SYSFS_GROUP_VISIBLE(example),
 *       .attrs = &example_attrs,
 * };
 */
#define DEFINE_SIMPLE_SYSFS_GROUP_VISIBLE(name)                   \
	static inline umode_t sysfs_group_visible_##name(         \
		struct kobject *kobj, struct attribute *a, int n) \
	{                                                         \
		if (n == 0 && !name##_group_visible(kobj))        \
			return SYSFS_GROUP_INVISIBLE;             \
		return a->mode;                                   \
	}

/*
 * Same as DEFINE_SYSFS_GROUP_VISIBLE, but for groups with only binary
 * attributes. If an attribute_group defines both text and binary
 * attributes, the group visibility is determined by the function
 * specified to is_visible() not is_bin_visible()
 */
#define DEFINE_SYSFS_BIN_GROUP_VISIBLE(name)                             \
	static inline umode_t sysfs_group_visible_##name(                \
		struct kobject *kobj, struct bin_attribute *attr, int n) \
	{                                                                \
		if (n == 0 && !name##_group_visible(kobj))               \
			return SYSFS_GROUP_INVISIBLE;                    \
		return name##_attr_visible(kobj, attr, n);               \
	}

#define DEFINE_SIMPLE_SYSFS_BIN_GROUP_VISIBLE(name)                   \
	static inline umode_t sysfs_group_visible_##name(             \
		struct kobject *kobj, struct bin_attribute *a, int n) \
	{                                                             \
		if (n == 0 && !name##_group_visible(kobj))            \
			return SYSFS_GROUP_INVISIBLE;                 \
		return a->mode;                                       \
	}

#define SYSFS_GROUP_VISIBLE(fn) sysfs_group_visible_##fn

/*
 * Use these macros to make defining attributes easier.
 * See include/linux/device.h for examples..
 */

#define __ATTR(_name, _mode, _show, _store) {				\
	.attr = {.name = __stringify(_name),				\
		 .mode = VERIFY_OCTAL_PERMISSIONS(_mode) },		\
	.show	= _show,						\
	.store	= _store,						\
}

#define __ATTR_PREALLOC(_name, _mode, _show, _store) {			\
	.attr = {.name = __stringify(_name),				\
		 .mode = SYSFS_PREALLOC | VERIFY_OCTAL_PERMISSIONS(_mode) },\
	.show	= _show,						\
	.store	= _store,						\
}

#define __ATTR_RO(_name) {						\
	.attr	= { .name = __stringify(_name), .mode = 0444 },		\
	.show	= _name##_show,						\
}

#define __ATTR_RO_MODE(_name, _mode) {					\
	.attr	= { .name = __stringify(_name),				\
		    .mode = VERIFY_OCTAL_PERMISSIONS(_mode) },		\
	.show	= _name##_show,						\
}

#define __ATTR_RW_MODE(_name, _mode) {					\
	.attr	= { .name = __stringify(_name),				\
		    .mode = VERIFY_OCTAL_PERMISSIONS(_mode) },		\
	.show	= _name##_show,						\
	.store	= _name##_store,					\
}

#define __ATTR_WO(_name) {						\
	.attr	= { .name = __stringify(_name), .mode = 0200 },		\
	.store	= _name##_store,					\
}

#define __ATTR_RW(_name) __ATTR(_name, 0644, _name##_show, _name##_store)

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

#define BIN_ATTRIBUTE_GROUPS(_name)				\
static const struct attribute_group _name##_group = {		\
	.bin_attrs = _name##_attrs,				\
};								\
__ATTRIBUTE_GROUPS(_name)

struct file;
struct vm_area_struct;
struct address_space;

struct bin_attribute {
	struct attribute	attr;
	size_t			size;
	void			*private;
	struct address_space *(*f_mapping)(void);
	ssize_t (*read)(struct file *, struct kobject *, struct bin_attribute *,
			char *, loff_t, size_t);
	ssize_t (*write)(struct file *, struct kobject *, struct bin_attribute *,
			 char *, loff_t, size_t);
	loff_t (*llseek)(struct file *, struct kobject *, struct bin_attribute *,
			 loff_t, int);
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
	.attr	= { .name = __stringify(_name), .mode = 0444 },		\
	.read	= _name##_read,						\
	.size	= _size,						\
}

#define __BIN_ATTR_WO(_name, _size) {					\
	.attr	= { .name = __stringify(_name), .mode = 0200 },		\
	.write	= _name##_write,					\
	.size	= _size,						\
}

#define __BIN_ATTR_RW(_name, _size)					\
	__BIN_ATTR(_name, 0644, _name##_read, _name##_write, _size)

#define __BIN_ATTR_NULL __ATTR_NULL

#define BIN_ATTR(_name, _mode, _read, _write, _size)			\
struct bin_attribute bin_attr_##_name = __BIN_ATTR(_name, _mode, _read,	\
					_write, _size)

#define BIN_ATTR_RO(_name, _size)					\
struct bin_attribute bin_attr_##_name = __BIN_ATTR_RO(_name, _size)

#define BIN_ATTR_WO(_name, _size)					\
struct bin_attribute bin_attr_##_name = __BIN_ATTR_WO(_name, _size)

#define BIN_ATTR_RW(_name, _size)					\
struct bin_attribute bin_attr_##_name = __BIN_ATTR_RW(_name, _size)


#define __BIN_ATTR_ADMIN_RO(_name, _size) {					\
	.attr	= { .name = __stringify(_name), .mode = 0400 },		\
	.read	= _name##_read,						\
	.size	= _size,						\
}

#define __BIN_ATTR_ADMIN_RW(_name, _size)					\
	__BIN_ATTR(_name, 0600, _name##_read, _name##_write, _size)

#define BIN_ATTR_ADMIN_RO(_name, _size)					\
struct bin_attribute bin_attr_##_name = __BIN_ATTR_ADMIN_RO(_name, _size)

#define BIN_ATTR_ADMIN_RW(_name, _size)					\
struct bin_attribute bin_attr_##_name = __BIN_ATTR_ADMIN_RW(_name, _size)

#define __BIN_ATTR_SIMPLE_RO(_name, _mode) {				\
	.attr	= { .name = __stringify(_name), .mode = _mode },	\
	.read	= sysfs_bin_attr_simple_read,				\
}

#define BIN_ATTR_SIMPLE_RO(_name)					\
struct bin_attribute bin_attr_##_name = __BIN_ATTR_SIMPLE_RO(_name, 0444)

#define BIN_ATTR_SIMPLE_ADMIN_RO(_name)					\
struct bin_attribute bin_attr_##_name = __BIN_ATTR_SIMPLE_RO(_name, 0400)

struct sysfs_ops {
	ssize_t	(*show)(struct kobject *, struct attribute *, char *);
	ssize_t	(*store)(struct kobject *, struct attribute *, const char *, size_t);
};

#ifdef CONFIG_SYSFS

int __must_check sysfs_create_dir_ns(struct kobject *kobj, const void *ns);
void sysfs_remove_dir(struct kobject *kobj);
int __must_check sysfs_rename_dir_ns(struct kobject *kobj, const char *new_name,
				     const void *new_ns);
int __must_check sysfs_move_dir_ns(struct kobject *kobj,
				   struct kobject *new_parent_kobj,
				   const void *new_ns);
int __must_check sysfs_create_mount_point(struct kobject *parent_kobj,
					  const char *name);
void sysfs_remove_mount_point(struct kobject *parent_kobj,
			      const char *name);

int __must_check sysfs_create_file_ns(struct kobject *kobj,
				      const struct attribute *attr,
				      const void *ns);
int __must_check sysfs_create_files(struct kobject *kobj,
				   const struct attribute * const *attr);
int __must_check sysfs_chmod_file(struct kobject *kobj,
				  const struct attribute *attr, umode_t mode);
struct kernfs_node *sysfs_break_active_protection(struct kobject *kobj,
						  const struct attribute *attr);
void sysfs_unbreak_active_protection(struct kernfs_node *kn);
void sysfs_remove_file_ns(struct kobject *kobj, const struct attribute *attr,
			  const void *ns);
bool sysfs_remove_file_self(struct kobject *kobj, const struct attribute *attr);
void sysfs_remove_files(struct kobject *kobj, const struct attribute * const *attr);

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

int sysfs_rename_link_ns(struct kobject *kobj, struct kobject *target,
			 const char *old_name, const char *new_name,
			 const void *new_ns);

void sysfs_delete_link(struct kobject *dir, struct kobject *targ,
			const char *name);

int __must_check sysfs_create_group(struct kobject *kobj,
				    const struct attribute_group *grp);
int __must_check sysfs_create_groups(struct kobject *kobj,
				     const struct attribute_group **groups);
int __must_check sysfs_update_groups(struct kobject *kobj,
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
int compat_only_sysfs_link_entry_to_kobj(struct kobject *kobj,
					 struct kobject *target_kobj,
					 const char *target_name,
					 const char *symlink_name);

void sysfs_notify(struct kobject *kobj, const char *dir, const char *attr);

int __must_check sysfs_init(void);

static inline void sysfs_enable_ns(struct kernfs_node *kn)
{
	return kernfs_enable_ns(kn);
}

int sysfs_file_change_owner(struct kobject *kobj, const char *name, kuid_t kuid,
			    kgid_t kgid);
int sysfs_change_owner(struct kobject *kobj, kuid_t kuid, kgid_t kgid);
int sysfs_link_change_owner(struct kobject *kobj, struct kobject *targ,
			    const char *name, kuid_t kuid, kgid_t kgid);
int sysfs_groups_change_owner(struct kobject *kobj,
			      const struct attribute_group **groups,
			      kuid_t kuid, kgid_t kgid);
int sysfs_group_change_owner(struct kobject *kobj,
			     const struct attribute_group *groups, kuid_t kuid,
			     kgid_t kgid);
__printf(2, 3)
int sysfs_emit(char *buf, const char *fmt, ...);
__printf(3, 4)
int sysfs_emit_at(char *buf, int at, const char *fmt, ...);

ssize_t sysfs_bin_attr_simple_read(struct file *file, struct kobject *kobj,
				   struct bin_attribute *attr, char *buf,
				   loff_t off, size_t count);

#else /* CONFIG_SYSFS */

static inline int sysfs_create_dir_ns(struct kobject *kobj, const void *ns)
{
	return 0;
}

static inline void sysfs_remove_dir(struct kobject *kobj)
{
}

static inline int sysfs_rename_dir_ns(struct kobject *kobj,
				      const char *new_name, const void *new_ns)
{
	return 0;
}

static inline int sysfs_move_dir_ns(struct kobject *kobj,
				    struct kobject *new_parent_kobj,
				    const void *new_ns)
{
	return 0;
}

static inline int sysfs_create_mount_point(struct kobject *parent_kobj,
					   const char *name)
{
	return 0;
}

static inline void sysfs_remove_mount_point(struct kobject *parent_kobj,
					    const char *name)
{
}

static inline int sysfs_create_file_ns(struct kobject *kobj,
				       const struct attribute *attr,
				       const void *ns)
{
	return 0;
}

static inline int sysfs_create_files(struct kobject *kobj,
				    const struct attribute * const *attr)
{
	return 0;
}

static inline int sysfs_chmod_file(struct kobject *kobj,
				   const struct attribute *attr, umode_t mode)
{
	return 0;
}

static inline struct kernfs_node *
sysfs_break_active_protection(struct kobject *kobj,
			      const struct attribute *attr)
{
	return NULL;
}

static inline void sysfs_unbreak_active_protection(struct kernfs_node *kn)
{
}

static inline void sysfs_remove_file_ns(struct kobject *kobj,
					const struct attribute *attr,
					const void *ns)
{
}

static inline bool sysfs_remove_file_self(struct kobject *kobj,
					  const struct attribute *attr)
{
	return false;
}

static inline void sysfs_remove_files(struct kobject *kobj,
				     const struct attribute * const *attr)
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

static inline int sysfs_rename_link_ns(struct kobject *k, struct kobject *t,
				       const char *old_name,
				       const char *new_name, const void *ns)
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

static inline int sysfs_create_groups(struct kobject *kobj,
				      const struct attribute_group **groups)
{
	return 0;
}

static inline int sysfs_update_groups(struct kobject *kobj,
				      const struct attribute_group **groups)
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

static inline void sysfs_remove_groups(struct kobject *kobj,
				       const struct attribute_group **groups)
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

static inline int compat_only_sysfs_link_entry_to_kobj(struct kobject *kobj,
						       struct kobject *target_kobj,
						       const char *target_name,
						       const char *symlink_name)
{
	return 0;
}

static inline void sysfs_notify(struct kobject *kobj, const char *dir,
				const char *attr)
{
}

static inline int __must_check sysfs_init(void)
{
	return 0;
}

static inline void sysfs_enable_ns(struct kernfs_node *kn)
{
}

static inline int sysfs_file_change_owner(struct kobject *kobj,
					  const char *name, kuid_t kuid,
					  kgid_t kgid)
{
	return 0;
}

static inline int sysfs_link_change_owner(struct kobject *kobj,
					  struct kobject *targ,
					  const char *name, kuid_t kuid,
					  kgid_t kgid)
{
	return 0;
}

static inline int sysfs_change_owner(struct kobject *kobj, kuid_t kuid, kgid_t kgid)
{
	return 0;
}

static inline int sysfs_groups_change_owner(struct kobject *kobj,
			  const struct attribute_group **groups,
			  kuid_t kuid, kgid_t kgid)
{
	return 0;
}

static inline int sysfs_group_change_owner(struct kobject *kobj,
					   const struct attribute_group *groups,
					   kuid_t kuid, kgid_t kgid)
{
	return 0;
}

__printf(2, 3)
static inline int sysfs_emit(char *buf, const char *fmt, ...)
{
	return 0;
}

__printf(3, 4)
static inline int sysfs_emit_at(char *buf, int at, const char *fmt, ...)
{
	return 0;
}

static inline ssize_t sysfs_bin_attr_simple_read(struct file *file,
						 struct kobject *kobj,
						 struct bin_attribute *attr,
						 char *buf, loff_t off,
						 size_t count)
{
	return 0;
}
#endif /* CONFIG_SYSFS */

static inline int __must_check sysfs_create_file(struct kobject *kobj,
						 const struct attribute *attr)
{
	return sysfs_create_file_ns(kobj, attr, NULL);
}

static inline void sysfs_remove_file(struct kobject *kobj,
				     const struct attribute *attr)
{
	sysfs_remove_file_ns(kobj, attr, NULL);
}

static inline int sysfs_rename_link(struct kobject *kobj, struct kobject *target,
				    const char *old_name, const char *new_name)
{
	return sysfs_rename_link_ns(kobj, target, old_name, new_name, NULL);
}

static inline void sysfs_notify_dirent(struct kernfs_node *kn)
{
	kernfs_notify(kn);
}

static inline struct kernfs_node *sysfs_get_dirent(struct kernfs_node *parent,
						   const char *name)
{
	return kernfs_find_and_get(parent, name);
}

static inline struct kernfs_node *sysfs_get(struct kernfs_node *kn)
{
	kernfs_get(kn);
	return kn;
}

static inline void sysfs_put(struct kernfs_node *kn)
{
	kernfs_put(kn);
}

#endif /* _SYSFS_H_ */
