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
#include <linux/idr.h>
#include <linux/lockdep.h>

struct file;
struct iattr;
struct seq_file;
struct vm_area_struct;
struct super_block;
struct file_system_type;

struct sysfs_dirent;

struct kernfs_root {
	/* published fields */
	struct sysfs_dirent	*sd;

	/* private fields, do not use outside kernfs proper */
	struct ida		ino_ida;
};

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

struct kernfs_ops {
	/*
	 * Read is handled by either seq_file or raw_read().
	 *
	 * If seq_show() is present, seq_file path is active.  Other seq
	 * operations are optional and if not implemented, the behavior is
	 * equivalent to single_open().  @sf->private points to the
	 * associated sysfs_open_file.
	 *
	 * read() is bounced through kernel buffer and a read larger than
	 * PAGE_SIZE results in partial operation of PAGE_SIZE.
	 */
	int (*seq_show)(struct seq_file *sf, void *v);

	void *(*seq_start)(struct seq_file *sf, loff_t *ppos);
	void *(*seq_next)(struct seq_file *sf, void *v, loff_t *ppos);
	void (*seq_stop)(struct seq_file *sf, void *v);

	ssize_t (*read)(struct sysfs_open_file *of, char *buf, size_t bytes,
			loff_t off);

	/*
	 * write() is bounced through kernel buffer and a write larger than
	 * PAGE_SIZE results in partial operation of PAGE_SIZE.
	 */
	ssize_t (*write)(struct sysfs_open_file *of, char *buf, size_t bytes,
			 loff_t off);

	int (*mmap)(struct sysfs_open_file *of, struct vm_area_struct *vma);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lock_class_key	lockdep_key;
#endif
};

#ifdef CONFIG_SYSFS

struct sysfs_dirent *kernfs_find_and_get_ns(struct sysfs_dirent *parent,
					    const char *name, const void *ns);
void kernfs_get(struct sysfs_dirent *sd);
void kernfs_put(struct sysfs_dirent *sd);

struct kernfs_root *kernfs_create_root(void *priv);
void kernfs_destroy_root(struct kernfs_root *root);

struct sysfs_dirent *kernfs_create_dir_ns(struct sysfs_dirent *parent,
					  const char *name, void *priv,
					  const void *ns);
struct sysfs_dirent *kernfs_create_file_ns_key(struct sysfs_dirent *parent,
					       const char *name,
					       umode_t mode, loff_t size,
					       const struct kernfs_ops *ops,
					       void *priv, const void *ns,
					       struct lock_class_key *key);
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
void kernfs_notify(struct sysfs_dirent *sd);

const void *kernfs_super_ns(struct super_block *sb);
struct dentry *kernfs_mount_ns(struct file_system_type *fs_type, int flags,
			       struct kernfs_root *root, const void *ns);
void kernfs_kill_sb(struct super_block *sb);

void kernfs_init(void);

#else	/* CONFIG_SYSFS */

static inline struct sysfs_dirent *
kernfs_find_and_get_ns(struct sysfs_dirent *parent, const char *name,
		       const void *ns)
{ return NULL; }

static inline void kernfs_get(struct sysfs_dirent *sd) { }
static inline void kernfs_put(struct sysfs_dirent *sd) { }

static inline struct kernfs_root *kernfs_create_root(void *priv)
{ return ERR_PTR(-ENOSYS); }

static inline void kernfs_destroy_root(struct kernfs_root *root) { }

static inline struct sysfs_dirent *
kernfs_create_dir_ns(struct sysfs_dirent *parent, const char *name, void *priv,
		     const void *ns)
{ return ERR_PTR(-ENOSYS); }

static inline struct sysfs_dirent *
kernfs_create_file_ns_key(struct sysfs_dirent *parent, const char *name,
			  umode_t mode, loff_t size,
			  const struct kernfs_ops *ops, void *priv,
			  const void *ns, struct lock_class_key *key)
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

static inline void kernfs_notify(struct sysfs_dirent *sd) { }

static inline const void *kernfs_super_ns(struct super_block *sb)
{ return NULL; }

static inline struct dentry *
kernfs_mount_ns(struct file_system_type *fs_type, int flags,
		struct kernfs_root *root, const void *ns)
{ return ERR_PTR(-ENOSYS); }

static inline void kernfs_kill_sb(struct super_block *sb) { }

static inline void kernfs_init(void) { }

#endif	/* CONFIG_SYSFS */

static inline struct sysfs_dirent *
kernfs_find_and_get(struct sysfs_dirent *sd, const char *name)
{
	return kernfs_find_and_get_ns(sd, name, NULL);
}

static inline struct sysfs_dirent *
kernfs_create_dir(struct sysfs_dirent *parent, const char *name, void *priv)
{
	return kernfs_create_dir_ns(parent, name, priv, NULL);
}

static inline struct sysfs_dirent *
kernfs_create_file_ns(struct sysfs_dirent *parent, const char *name,
		      umode_t mode, loff_t size, const struct kernfs_ops *ops,
		      void *priv, const void *ns)
{
	struct lock_class_key *key = NULL;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	key = (struct lock_class_key *)&ops->lockdep_key;
#endif
	return kernfs_create_file_ns_key(parent, name, mode, size, ops, priv,
					 ns, key);
}

static inline struct sysfs_dirent *
kernfs_create_file(struct sysfs_dirent *parent, const char *name, umode_t mode,
		   loff_t size, const struct kernfs_ops *ops, void *priv)
{
	return kernfs_create_file_ns(parent, name, mode, size, ops, priv, NULL);
}

static inline int kernfs_remove_by_name(struct sysfs_dirent *parent,
					const char *name)
{
	return kernfs_remove_by_name_ns(parent, name, NULL);
}

static inline struct dentry *
kernfs_mount(struct file_system_type *fs_type, int flags,
	     struct kernfs_root *root)
{
	return kernfs_mount_ns(fs_type, flags, root, NULL);
}

#endif	/* __LINUX_KERNFS_H */
