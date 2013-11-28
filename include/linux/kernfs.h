/*
 * kernfs.h - pseudo filesystem decoupled from vfs locking
 *
 * This file is released under the GPLv2.
 */

#ifndef __LINUX_KERNFS_H
#define __LINUX_KERNFS_H

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/mutex.h>

struct file;
struct iattr;
struct seq_file;
struct vm_area_struct;

struct sysfs_dirent;

struct sysfs_open_file {
	/* published fields */
	struct sysfs_dirent	*sd;
	struct file		*file;

	/* private fields, do not use outside kernfs proper */
	struct mutex		mutex;
	int			event;
	struct list_head	list;

	bool			mmapped;
	const struct vm_operations_struct *vm_ops;
};

#ifdef CONFIG_SYSFS

struct sysfs_dirent *kernfs_create_dir_ns(struct sysfs_dirent *parent,
					  const char *name, void *priv,
					  const void *ns);
struct sysfs_dirent *kernfs_create_link(struct sysfs_dirent *parent,
					const char *name,
					struct sysfs_dirent *target);
void kernfs_remove(struct sysfs_dirent *sd);
int kernfs_remove_by_name_ns(struct sysfs_dirent *parent, const char *name,
			     const void *ns);
int kernfs_rename_ns(struct sysfs_dirent *sd, struct sysfs_dirent *new_parent,
		     const char *new_name, const void *new_ns);
void kernfs_enable_ns(struct sysfs_dirent *sd);
int kernfs_setattr(struct sysfs_dirent *sd, const struct iattr *iattr);

#else	/* CONFIG_SYSFS */

static inline struct sysfs_dirent *
kernfs_create_dir_ns(struct sysfs_dirent *parent, const char *name, void *priv,
		     const void *ns)
{ return ERR_PTR(-ENOSYS); }

static inline struct sysfs_dirent *
kernfs_create_link(struct sysfs_dirent *parent, const char *name,
		   struct sysfs_dirent *target)
{ return ERR_PTR(-ENOSYS); }

static inline void kernfs_remove(struct sysfs_dirent *sd) { }

static inline int kernfs_remove_by_name_ns(struct sysfs_dirent *parent,
					   const char *name, const void *ns)
{ return -ENOSYS; }

static inline int kernfs_rename_ns(struct sysfs_dirent *sd,
				   struct sysfs_dirent *new_parent,
				   const char *new_name, const void *new_ns)
{ return -ENOSYS; }

static inline void kernfs_enable_ns(struct sysfs_dirent *sd) { }

static inline int kernfs_setattr(struct sysfs_dirent *sd,
				 const struct iattr *iattr)
{ return -ENOSYS; }

#endif	/* CONFIG_SYSFS */

static inline struct sysfs_dirent *
kernfs_create_dir(struct sysfs_dirent *parent, const char *name, void *priv)
{
	return kernfs_create_dir_ns(parent, name, priv, NULL);
}

static inline int kernfs_remove_by_name(struct sysfs_dirent *parent,
					const char *name)
{
	return kernfs_remove_by_name_ns(parent, name, NULL);
}

#endif	/* __LINUX_KERNFS_H */
