/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * configfs.h - definitions for the device driver filesystem
 *
 * Based on sysfs:
 * 	sysfs is Copyright (C) 2001, 2002, 2003 Patrick Mochel
 *
 * Based on kobject.h:
 *      Copyright (c) 2002-2003	Patrick Mochel
 *      Copyright (c) 2002-2003	Open Source Development Labs
 *
 * configfs Copyright (C) 2005 Oracle.  All rights reserved.
 *
 * Please read Documentation/filesystems/configfs.rst before using
 * the configfs interface, ESPECIALLY the parts about reference counts and
 * item destructors.
 */

#ifndef _CONFIGFS_H_
#define _CONFIGFS_H_

#include <linux/stat.h>   /* S_IRUGO */
#include <linux/types.h>  /* ssize_t */
#include <linux/list.h>   /* struct list_head */
#include <linux/kref.h>   /* struct kref */
#include <linux/mutex.h>  /* struct mutex */

#define CONFIGFS_ITEM_NAME_LEN	20

struct module;

struct configfs_item_operations;
struct configfs_group_operations;
struct configfs_attribute;
struct configfs_bin_attribute;
struct configfs_subsystem;

struct config_item {
	char			*ci_name;
	char			ci_namebuf[CONFIGFS_ITEM_NAME_LEN];
	struct kref		ci_kref;
	struct list_head	ci_entry;
	struct config_item	*ci_parent;
	struct config_group	*ci_group;
	const struct config_item_type	*ci_type;
	struct dentry		*ci_dentry;
};

extern __printf(2, 3)
int config_item_set_name(struct config_item *, const char *, ...);

static inline char *config_item_name(struct config_item * item)
{
	return item->ci_name;
}

extern void config_item_init_type_name(struct config_item *item,
				       const char *name,
				       const struct config_item_type *type);

extern struct config_item *config_item_get(struct config_item *);
extern struct config_item *config_item_get_unless_zero(struct config_item *);
extern void config_item_put(struct config_item *);

struct config_item_type {
	struct module				*ct_owner;
	struct configfs_item_operations		*ct_item_ops;
	struct configfs_group_operations	*ct_group_ops;
	struct configfs_attribute		**ct_attrs;
	struct configfs_bin_attribute		**ct_bin_attrs;
};

/**
 *	group - a group of config_items of a specific type, belonging
 *	to a specific subsystem.
 */
struct config_group {
	struct config_item		cg_item;
	struct list_head		cg_children;
	struct configfs_subsystem 	*cg_subsys;
	struct list_head		default_groups;
	struct list_head		group_entry;
};

extern void config_group_init(struct config_group *group);
extern void config_group_init_type_name(struct config_group *group,
					const char *name,
					const struct config_item_type *type);

static inline struct config_group *to_config_group(struct config_item *item)
{
	return item ? container_of(item,struct config_group,cg_item) : NULL;
}

static inline struct config_group *config_group_get(struct config_group *group)
{
	return group ? to_config_group(config_item_get(&group->cg_item)) : NULL;
}

static inline void config_group_put(struct config_group *group)
{
	config_item_put(&group->cg_item);
}

extern struct config_item *config_group_find_item(struct config_group *,
						  const char *);


static inline void configfs_add_default_group(struct config_group *new_group,
		struct config_group *group)
{
	list_add_tail(&new_group->group_entry, &group->default_groups);
}

struct configfs_attribute {
	const char		*ca_name;
	struct module 		*ca_owner;
	umode_t			ca_mode;
	ssize_t (*show)(struct config_item *, char *);
	ssize_t (*store)(struct config_item *, const char *, size_t);
};

#define CONFIGFS_ATTR(_pfx, _name)			\
static struct configfs_attribute _pfx##attr_##_name = {	\
	.ca_name	= __stringify(_name),		\
	.ca_mode	= S_IRUGO | S_IWUSR,		\
	.ca_owner	= THIS_MODULE,			\
	.show		= _pfx##_name##_show,		\
	.store		= _pfx##_name##_store,		\
}

#define CONFIGFS_ATTR_RO(_pfx, _name)			\
static struct configfs_attribute _pfx##attr_##_name = {	\
	.ca_name	= __stringify(_name),		\
	.ca_mode	= S_IRUGO,			\
	.ca_owner	= THIS_MODULE,			\
	.show		= _pfx##_name##_show,		\
}

#define CONFIGFS_ATTR_WO(_pfx, _name)			\
static struct configfs_attribute _pfx##attr_##_name = {	\
	.ca_name	= __stringify(_name),		\
	.ca_mode	= S_IWUSR,			\
	.ca_owner	= THIS_MODULE,			\
	.store		= _pfx##_name##_store,		\
}

struct file;
struct vm_area_struct;

struct configfs_bin_attribute {
	struct configfs_attribute cb_attr;	/* std. attribute */
	void *cb_private;			/* for user       */
	size_t cb_max_size;			/* max core size  */
	ssize_t (*read)(struct config_item *, void *, size_t);
	ssize_t (*write)(struct config_item *, const void *, size_t);
};

#define CONFIGFS_BIN_ATTR(_pfx, _name, _priv, _maxsz)		\
static struct configfs_bin_attribute _pfx##attr_##_name = {	\
	.cb_attr = {						\
		.ca_name	= __stringify(_name),		\
		.ca_mode	= S_IRUGO | S_IWUSR,		\
		.ca_owner	= THIS_MODULE,			\
	},							\
	.cb_private	= _priv,				\
	.cb_max_size	= _maxsz,				\
	.read		= _pfx##_name##_read,			\
	.write		= _pfx##_name##_write,			\
}

#define CONFIGFS_BIN_ATTR_RO(_pfx, _name, _priv, _maxsz)	\
static struct configfs_bin_attribute _pfx##attr_##_name = {	\
	.cb_attr = {						\
		.ca_name	= __stringify(_name),		\
		.ca_mode	= S_IRUGO,			\
		.ca_owner	= THIS_MODULE,			\
	},							\
	.cb_private	= _priv,				\
	.cb_max_size	= _maxsz,				\
	.read		= _pfx##_name##_read,			\
}

#define CONFIGFS_BIN_ATTR_WO(_pfx, _name, _priv, _maxsz)	\
static struct configfs_bin_attribute _pfx##attr_##_name = {	\
	.cb_attr = {						\
		.ca_name	= __stringify(_name),		\
		.ca_mode	= S_IWUSR,			\
		.ca_owner	= THIS_MODULE,			\
	},							\
	.cb_private	= _priv,				\
	.cb_max_size	= _maxsz,				\
	.write		= _pfx##_name##_write,			\
}

/*
 * If allow_link() exists, the item can symlink(2) out to other
 * items.  If the item is a group, it may support mkdir(2).
 * Groups supply one of make_group() and make_item().  If the
 * group supports make_group(), one can create group children.  If it
 * supports make_item(), one can create config_item children.  make_group()
 * and make_item() return ERR_PTR() on errors.  If it has
 * default_groups on group->default_groups, it has automatically created
 * group children.  default_groups may coexist alongsize make_group() or
 * make_item(), but if the group wishes to have only default_groups
 * children (disallowing mkdir(2)), it need not provide either function.
 */
struct configfs_item_operations {
	void (*release)(struct config_item *);
	int (*allow_link)(struct config_item *src, struct config_item *target);
	void (*drop_link)(struct config_item *src, struct config_item *target);
};

struct configfs_group_operations {
	struct config_item *(*make_item)(struct config_group *group, const char *name);
	struct config_group *(*make_group)(struct config_group *group, const char *name);
	void (*disconnect_notify)(struct config_group *group, struct config_item *item);
	void (*drop_item)(struct config_group *group, struct config_item *item);
	bool (*is_visible)(struct config_item *item, struct configfs_attribute *attr, int n);
	bool (*is_bin_visible)(struct config_item *item, struct configfs_bin_attribute *attr,
			       int n);
};

struct configfs_subsystem {
	struct config_group	su_group;
	struct mutex		su_mutex;
};

static inline struct configfs_subsystem *to_configfs_subsystem(struct config_group *group)
{
	return group ?
		container_of(group, struct configfs_subsystem, su_group) :
		NULL;
}

int configfs_register_subsystem(struct configfs_subsystem *subsys);
void configfs_unregister_subsystem(struct configfs_subsystem *subsys);

int configfs_register_group(struct config_group *parent_group,
			    struct config_group *group);
void configfs_unregister_group(struct config_group *group);

void configfs_remove_default_groups(struct config_group *group);

struct config_group *
configfs_register_default_group(struct config_group *parent_group,
				const char *name,
				const struct config_item_type *item_type);
void configfs_unregister_default_group(struct config_group *group);

/* These functions can sleep and can alloc with GFP_KERNEL */
/* WARNING: These cannot be called underneath configfs callbacks!! */
int configfs_depend_item(struct configfs_subsystem *subsys,
			 struct config_item *target);
void configfs_undepend_item(struct config_item *target);

/*
 * These functions can sleep and can alloc with GFP_KERNEL
 * NOTE: These should be called only underneath configfs callbacks.
 * NOTE: First parameter is a caller's subsystem, not target's.
 * WARNING: These cannot be called on newly created item
 *        (in make_group()/make_item() callback)
 */
int configfs_depend_item_unlocked(struct configfs_subsystem *caller_subsys,
				  struct config_item *target);


static inline void configfs_undepend_item_unlocked(struct config_item *target)
{
	configfs_undepend_item(target);
}

#endif /* _CONFIGFS_H_ */
