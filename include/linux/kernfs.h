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

struct kernfs_open_node;
struct kernfs_iattrs;

enum kernfs_node_type {
	KERNFS_DIR		= 0x0001,
	KERNFS_FILE		= 0x0002,
	KERNFS_LINK		= 0x0004,
};

#define KERNFS_TYPE_MASK	0x000f
#define KERNFS_ACTIVE_REF	KERNFS_FILE
#define KERNFS_FLAG_MASK	~KERNFS_TYPE_MASK

enum kernfs_node_flag {
	KERNFS_REMOVED		= 0x0010,
	KERNFS_NS		= 0x0020,
	KERNFS_HAS_SEQ_SHOW	= 0x0040,
	KERNFS_HAS_MMAP		= 0x0080,
	KERNFS_LOCKDEP		= 0x0100,
	KERNFS_STATIC_NAME	= 0x0200,
};

/* type-specific structures for kernfs_node union members */
struct kernfs_elem_dir {
	unsigned long		subdirs;
	/* children rbtree starts here and goes through kn->rb */
	struct rb_root		children;

	/*
	 * The kernfs hierarchy this directory belongs to.  This fits
	 * better directly in kernfs_node but is here to save space.
	 */
	struct kernfs_root	*root;
};

struct kernfs_elem_symlink {
	struct kernfs_node	*target_kn;
};

struct kernfs_elem_attr {
	const struct kernfs_ops	*ops;
	struct kernfs_open_node	*open;
	loff_t			size;
};

/*
 * kernfs_node - the building block of kernfs hierarchy.  Each and every
 * kernfs node is represented by single kernfs_node.  Most fields are
 * private to kernfs and shouldn't be accessed directly by kernfs users.
 *
 * As long as s_count reference is held, the kernfs_node itself is
 * accessible.  Dereferencing elem or any other outer entity requires
 * active reference.
 */
struct kernfs_node {
	atomic_t		count;
	atomic_t		active;
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map	dep_map;
#endif
	/* the following two fields are published */
	struct kernfs_node	*parent;
	const char		*name;

	struct rb_node		rb;

	union {
		struct completion	*completion;
		struct kernfs_node	*removed_list;
	} u;

	const void		*ns;	/* namespace tag */
	unsigned int		hash;	/* ns + name hash */
	union {
		struct kernfs_elem_dir		dir;
		struct kernfs_elem_symlink	symlink;
		struct kernfs_elem_attr		attr;
	};

	void			*priv;

	unsigned short		flags;
	umode_t			mode;
	unsigned int		ino;
	struct kernfs_iattrs	*iattr;
};

struct kernfs_root {
	/* published fields */
	struct kernfs_node	*kn;

	/* private fields, do not use outside kernfs proper */
	struct ida		ino_ida;
};

struct kernfs_open_file {
	/* published fields */
	struct kernfs_node	*kn;
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
	 * associated kernfs_open_file.
	 *
	 * read() is bounced through kernel buffer and a read larger than
	 * PAGE_SIZE results in partial operation of PAGE_SIZE.
	 */
	int (*seq_show)(struct seq_file *sf, void *v);

	void *(*seq_start)(struct seq_file *sf, loff_t *ppos);
	void *(*seq_next)(struct seq_file *sf, void *v, loff_t *ppos);
	void (*seq_stop)(struct seq_file *sf, void *v);

	ssize_t (*read)(struct kernfs_open_file *of, char *buf, size_t bytes,
			loff_t off);

	/*
	 * write() is bounced through kernel buffer and a write larger than
	 * PAGE_SIZE results in partial operation of PAGE_SIZE.
	 */
	ssize_t (*write)(struct kernfs_open_file *of, char *buf, size_t bytes,
			 loff_t off);

	int (*mmap)(struct kernfs_open_file *of, struct vm_area_struct *vma);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lock_class_key	lockdep_key;
#endif
};

#ifdef CONFIG_SYSFS

static inline enum kernfs_node_type kernfs_type(struct kernfs_node *kn)
{
	return kn->flags & KERNFS_TYPE_MASK;
}

/**
 * kernfs_enable_ns - enable namespace under a directory
 * @kn: directory of interest, should be empty
 *
 * This is to be called right after @kn is created to enable namespace
 * under it.  All children of @kn must have non-NULL namespace tags and
 * only the ones which match the super_block's tag will be visible.
 */
static inline void kernfs_enable_ns(struct kernfs_node *kn)
{
	WARN_ON_ONCE(kernfs_type(kn) != KERNFS_DIR);
	WARN_ON_ONCE(!RB_EMPTY_ROOT(&kn->dir.children));
	kn->flags |= KERNFS_NS;
}

/**
 * kernfs_ns_enabled - test whether namespace is enabled
 * @kn: the node to test
 *
 * Test whether namespace filtering is enabled for the children of @ns.
 */
static inline bool kernfs_ns_enabled(struct kernfs_node *kn)
{
	return kn->flags & KERNFS_NS;
}

struct kernfs_node *kernfs_find_and_get_ns(struct kernfs_node *parent,
					   const char *name, const void *ns);
void kernfs_get(struct kernfs_node *kn);
void kernfs_put(struct kernfs_node *kn);

struct kernfs_root *kernfs_create_root(void *priv);
void kernfs_destroy_root(struct kernfs_root *root);

struct kernfs_node *kernfs_create_dir_ns(struct kernfs_node *parent,
					 const char *name, umode_t mode,
					 void *priv, const void *ns);
struct kernfs_node *__kernfs_create_file(struct kernfs_node *parent,
					 const char *name,
					 umode_t mode, loff_t size,
					 const struct kernfs_ops *ops,
					 void *priv, const void *ns,
					 bool name_is_static,
					 struct lock_class_key *key);
struct kernfs_node *kernfs_create_link(struct kernfs_node *parent,
				       const char *name,
				       struct kernfs_node *target);
void kernfs_remove(struct kernfs_node *kn);
int kernfs_remove_by_name_ns(struct kernfs_node *parent, const char *name,
			     const void *ns);
int kernfs_rename_ns(struct kernfs_node *kn, struct kernfs_node *new_parent,
		     const char *new_name, const void *new_ns);
int kernfs_setattr(struct kernfs_node *kn, const struct iattr *iattr);
void kernfs_notify(struct kernfs_node *kn);

const void *kernfs_super_ns(struct super_block *sb);
struct dentry *kernfs_mount_ns(struct file_system_type *fs_type, int flags,
			       struct kernfs_root *root, const void *ns);
void kernfs_kill_sb(struct super_block *sb);

void kernfs_init(void);

#else	/* CONFIG_SYSFS */

static inline enum kernfs_node_type kernfs_type(struct kernfs_node *kn)
{ return 0; }	/* whatever */

static inline void kernfs_enable_ns(struct kernfs_node *kn) { }

static inline bool kernfs_ns_enabled(struct kernfs_node *kn)
{ return false; }

static inline struct kernfs_node *
kernfs_find_and_get_ns(struct kernfs_node *parent, const char *name,
		       const void *ns)
{ return NULL; }

static inline void kernfs_get(struct kernfs_node *kn) { }
static inline void kernfs_put(struct kernfs_node *kn) { }

static inline struct kernfs_root *kernfs_create_root(void *priv)
{ return ERR_PTR(-ENOSYS); }

static inline void kernfs_destroy_root(struct kernfs_root *root) { }

static inline struct kernfs_node *
kernfs_create_dir_ns(struct kernfs_node *parent, const char *name,
		     umode_t mode, void *priv, const void *ns)
{ return ERR_PTR(-ENOSYS); }

static inline struct kernfs_node *
__kernfs_create_file(struct kernfs_node *parent, const char *name,
		     umode_t mode, loff_t size, const struct kernfs_ops *ops,
		     void *priv, const void *ns, bool name_is_static,
		     struct lock_class_key *key)
{ return ERR_PTR(-ENOSYS); }

static inline struct kernfs_node *
kernfs_create_link(struct kernfs_node *parent, const char *name,
		   struct kernfs_node *target)
{ return ERR_PTR(-ENOSYS); }

static inline void kernfs_remove(struct kernfs_node *kn) { }

static inline int kernfs_remove_by_name_ns(struct kernfs_node *kn,
					   const char *name, const void *ns)
{ return -ENOSYS; }

static inline int kernfs_rename_ns(struct kernfs_node *kn,
				   struct kernfs_node *new_parent,
				   const char *new_name, const void *new_ns)
{ return -ENOSYS; }

static inline int kernfs_setattr(struct kernfs_node *kn,
				 const struct iattr *iattr)
{ return -ENOSYS; }

static inline void kernfs_notify(struct kernfs_node *kn) { }

static inline const void *kernfs_super_ns(struct super_block *sb)
{ return NULL; }

static inline struct dentry *
kernfs_mount_ns(struct file_system_type *fs_type, int flags,
		struct kernfs_root *root, const void *ns)
{ return ERR_PTR(-ENOSYS); }

static inline void kernfs_kill_sb(struct super_block *sb) { }

static inline void kernfs_init(void) { }

#endif	/* CONFIG_SYSFS */

static inline struct kernfs_node *
kernfs_find_and_get(struct kernfs_node *kn, const char *name)
{
	return kernfs_find_and_get_ns(kn, name, NULL);
}

static inline struct kernfs_node *
kernfs_create_dir(struct kernfs_node *parent, const char *name, umode_t mode,
		  void *priv)
{
	return kernfs_create_dir_ns(parent, name, mode, priv, NULL);
}

static inline struct kernfs_node *
kernfs_create_file_ns(struct kernfs_node *parent, const char *name,
		      umode_t mode, loff_t size, const struct kernfs_ops *ops,
		      void *priv, const void *ns)
{
	struct lock_class_key *key = NULL;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	key = (struct lock_class_key *)&ops->lockdep_key;
#endif
	return __kernfs_create_file(parent, name, mode, size, ops, priv, ns,
				    false, key);
}

static inline struct kernfs_node *
kernfs_create_file(struct kernfs_node *parent, const char *name, umode_t mode,
		   loff_t size, const struct kernfs_ops *ops, void *priv)
{
	return kernfs_create_file_ns(parent, name, mode, size, ops, priv, NULL);
}

static inline int kernfs_remove_by_name(struct kernfs_node *parent,
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
