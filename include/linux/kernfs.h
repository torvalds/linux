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
#include <linux/rbtree.h>
#include <linux/atomic.h>
#include <linux/completion.h>

struct file;
struct iattr;
struct seq_file;
struct vm_area_struct;
struct super_block;
struct file_system_type;

struct sysfs_open_dirent;
struct sysfs_inode_attrs;

enum kernfs_node_type {
	SYSFS_DIR		= 0x0001,
	SYSFS_KOBJ_ATTR		= 0x0002,
	SYSFS_KOBJ_LINK		= 0x0004,
};

#define SYSFS_TYPE_MASK		0x000f
#define SYSFS_COPY_NAME		(SYSFS_DIR | SYSFS_KOBJ_LINK)
#define SYSFS_ACTIVE_REF	SYSFS_KOBJ_ATTR
#define SYSFS_FLAG_MASK		~SYSFS_TYPE_MASK

enum kernfs_node_flag {
	SYSFS_FLAG_REMOVED	= 0x0010,
	SYSFS_FLAG_NS		= 0x0020,
	SYSFS_FLAG_HAS_SEQ_SHOW	= 0x0040,
	SYSFS_FLAG_HAS_MMAP	= 0x0080,
	SYSFS_FLAG_LOCKDEP	= 0x0100,
};

/* type-specific structures for sysfs_dirent->s_* union members */
struct sysfs_elem_dir {
	unsigned long		subdirs;
	/* children rbtree starts here and goes through sd->s_rb */
	struct rb_root		children;

	/*
	 * The kernfs hierarchy this directory belongs to.  This fits
	 * better directly in sysfs_dirent but is here to save space.
	 */
	struct kernfs_root	*root;
};

struct sysfs_elem_symlink {
	struct sysfs_dirent	*target_sd;
};

struct sysfs_elem_attr {
	const struct kernfs_ops	*ops;
	struct sysfs_open_dirent *open;
	loff_t			size;
};

/*
 * sysfs_dirent - the building block of sysfs hierarchy.  Each and every
 * sysfs node is represented by single sysfs_dirent.  Most fields are
 * private to kernfs and shouldn't be accessed directly by kernfs users.
 *
 * As long as s_count reference is held, the sysfs_dirent itself is
 * accessible.  Dereferencing s_elem or any other outer entity
 * requires s_active reference.
 */
struct sysfs_dirent {
	atomic_t		s_count;
	atomic_t		s_active;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
	/* the following two fields are published */
	struct sysfs_dirent	*s_parent;
	const char		*s_name;

	struct rb_node		s_rb;

	union {
		struct completion	*completion;
		struct sysfs_dirent	*removed_list;
	} u;

	const void		*s_ns; /* namespace tag */
	unsigned int		s_hash; /* ns + name hash */
	union {
		struct sysfs_elem_dir		s_dir;
		struct sysfs_elem_symlink	s_symlink;
		struct sysfs_elem_attr		s_attr;
	};

	void			*priv;

	unsigned short		s_flags;
	umode_t			s_mode;
	unsigned int		s_ino;
	struct sysfs_inode_attrs *s_iattr;
};

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

static inline enum kernfs_node_type sysfs_type(struct sysfs_dirent *sd)
{
	return sd->s_flags & SYSFS_TYPE_MASK;
}

/**
 * kernfs_enable_ns - enable namespace under a directory
 * @sd: directory of interest, should be empty
 *
 * This is to be called right after @sd is created to enable namespace
 * under it.  All children of @sd must have non-NULL namespace tags and
 * only the ones which match the super_block's tag will be visible.
 */
static inline void kernfs_enable_ns(struct sysfs_dirent *sd)
{
	WARN_ON_ONCE(sysfs_type(sd) != SYSFS_DIR);
	WARN_ON_ONCE(!RB_EMPTY_ROOT(&sd->s_dir.children));
	sd->s_flags |= SYSFS_FLAG_NS;
}

/**
 * kernfs_ns_enabled - test whether namespace is enabled
 * @sd: the node to test
 *
 * Test whether namespace filtering is enabled for the children of @ns.
 */
static inline bool kernfs_ns_enabled(struct sysfs_dirent *sd)
{
	return sd->s_flags & SYSFS_FLAG_NS;
}

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
int kernfs_setattr(struct sysfs_dirent *sd, const struct iattr *iattr);
void kernfs_notify(struct sysfs_dirent *sd);

const void *kernfs_super_ns(struct super_block *sb);
struct dentry *kernfs_mount_ns(struct file_system_type *fs_type, int flags,
			       struct kernfs_root *root, const void *ns);
void kernfs_kill_sb(struct super_block *sb);

void kernfs_init(void);

#else	/* CONFIG_SYSFS */

static inline enum kernfs_node_type sysfs_type(struct sysfs_dirent *sd)
{ return 0; }	/* whatever */

static inline void kernfs_enable_ns(struct sysfs_dirent *sd) { }

static inline bool kernfs_ns_enabled(struct sysfs_dirent *sd)
{ return false; }

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
