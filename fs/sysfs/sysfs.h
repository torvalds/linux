/*
 * fs/sysfs/sysfs.h - sysfs internal header file
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * This file is released under the GPLv2.
 */

#include <linux/lockdep.h>
#include <linux/kobject_ns.h>
#include <linux/fs.h>
#include <linux/rbtree.h>

struct sysfs_open_dirent;

/* type-specific structures for sysfs_dirent->s_* union members */
struct sysfs_elem_dir {
	struct kobject		*kobj;

	unsigned long		subdirs;
	/* children rbtree starts here and goes through sd->s_rb */
	struct rb_root		children;
};

struct sysfs_elem_symlink {
	struct sysfs_dirent	*target_sd;
};

struct sysfs_elem_attr {
	union {
		struct attribute	*attr;
		struct bin_attribute	*bin_attr;
	};
	struct sysfs_open_dirent *open;
};

struct sysfs_inode_attrs {
	struct iattr	ia_iattr;
	void		*ia_secdata;
	u32		ia_secdata_len;
};

/*
 * sysfs_dirent - the building block of sysfs hierarchy.  Each and
 * every sysfs node is represented by single sysfs_dirent.
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

	unsigned short		s_flags;
	umode_t			s_mode;
	unsigned int		s_ino;
	struct sysfs_inode_attrs *s_iattr;
};

#define SD_DEACTIVATED_BIAS		INT_MIN

#define SYSFS_TYPE_MASK			0x00ff
#define SYSFS_DIR			0x0001
#define SYSFS_KOBJ_ATTR			0x0002
#define SYSFS_KOBJ_BIN_ATTR		0x0004
#define SYSFS_KOBJ_LINK			0x0008
#define SYSFS_COPY_NAME			(SYSFS_DIR | SYSFS_KOBJ_LINK)
#define SYSFS_ACTIVE_REF		(SYSFS_KOBJ_ATTR | SYSFS_KOBJ_BIN_ATTR)

#define SYSFS_FLAG_MASK			~SYSFS_TYPE_MASK
#define SYSFS_FLAG_NS			0x01000
#define SYSFS_FLAG_REMOVED		0x02000

static inline unsigned int sysfs_type(struct sysfs_dirent *sd)
{
	return sd->s_flags & SYSFS_TYPE_MASK;
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC

#define sysfs_dirent_init_lockdep(sd)				\
do {								\
	struct attribute *attr = sd->s_attr.attr;		\
	struct lock_class_key *key = attr->key;			\
	if (!key)						\
		key = &attr->skey;				\
								\
	lockdep_init_map(&sd->dep_map, "s_active", key, 0);	\
} while (0)

/* Test for attributes that want to ignore lockdep for read-locking */
static inline bool sysfs_ignore_lockdep(struct sysfs_dirent *sd)
{
	int type = sysfs_type(sd);

	return (type == SYSFS_KOBJ_ATTR || type == SYSFS_KOBJ_BIN_ATTR) &&
		sd->s_attr.attr->ignore_lockdep;
}

#else

#define sysfs_dirent_init_lockdep(sd) do {} while (0)

static inline bool sysfs_ignore_lockdep(struct sysfs_dirent *sd)
{
	return true;
}

#endif

/*
 * Context structure to be used while adding/removing nodes.
 */
struct sysfs_addrm_cxt {
	struct sysfs_dirent	*removed;
};

/*
 * mount.c
 */

/*
 * Each sb is associated with one namespace tag, currently the network
 * namespace of the task which mounted this sysfs instance.  If multiple
 * tags become necessary, make the following an array and compare
 * sysfs_dirent tag against every entry.
 */
struct sysfs_super_info {
	void *ns;
};
#define sysfs_info(SB) ((struct sysfs_super_info *)(SB->s_fs_info))
extern struct sysfs_dirent sysfs_root;
extern struct kmem_cache *sysfs_dir_cachep;

/*
 * dir.c
 */
extern struct mutex sysfs_mutex;
extern spinlock_t sysfs_symlink_target_lock;
extern const struct dentry_operations sysfs_dentry_ops;

extern const struct file_operations sysfs_dir_operations;
extern const struct inode_operations sysfs_dir_inode_operations;

struct sysfs_dirent *sysfs_get_active(struct sysfs_dirent *sd);
void sysfs_put_active(struct sysfs_dirent *sd);
void sysfs_addrm_start(struct sysfs_addrm_cxt *acxt);
void sysfs_warn_dup(struct sysfs_dirent *parent, const char *name);
int __sysfs_add_one(struct sysfs_addrm_cxt *acxt, struct sysfs_dirent *sd,
		    struct sysfs_dirent *parent_sd);
int sysfs_add_one(struct sysfs_addrm_cxt *acxt, struct sysfs_dirent *sd,
		  struct sysfs_dirent *parent_sd);
void sysfs_addrm_finish(struct sysfs_addrm_cxt *acxt);

struct sysfs_dirent *sysfs_find_dirent(struct sysfs_dirent *parent_sd,
				       const unsigned char *name,
				       const void *ns);
struct sysfs_dirent *sysfs_new_dirent(const char *name, umode_t mode, int type);

void release_sysfs_dirent(struct sysfs_dirent *sd);

int sysfs_create_subdir(struct kobject *kobj, const char *name,
			struct sysfs_dirent **p_sd);

int sysfs_rename(struct sysfs_dirent *sd, struct sysfs_dirent *new_parent_sd,
		 const char *new_name, const void *new_ns);

static inline struct sysfs_dirent *__sysfs_get(struct sysfs_dirent *sd)
{
	if (sd) {
		WARN_ON(!atomic_read(&sd->s_count));
		atomic_inc(&sd->s_count);
	}
	return sd;
}
#define sysfs_get(sd) __sysfs_get(sd)

static inline void __sysfs_put(struct sysfs_dirent *sd)
{
	if (sd && atomic_dec_and_test(&sd->s_count))
		release_sysfs_dirent(sd);
}
#define sysfs_put(sd) __sysfs_put(sd)

/*
 * inode.c
 */
struct inode *sysfs_get_inode(struct super_block *sb, struct sysfs_dirent *sd);
void sysfs_evict_inode(struct inode *inode);
int sysfs_sd_setattr(struct sysfs_dirent *sd, struct iattr *iattr);
int sysfs_permission(struct inode *inode, int mask);
int sysfs_setattr(struct dentry *dentry, struct iattr *iattr);
int sysfs_getattr(struct vfsmount *mnt, struct dentry *dentry,
		  struct kstat *stat);
int sysfs_setxattr(struct dentry *dentry, const char *name, const void *value,
		   size_t size, int flags);
int sysfs_inode_init(void);

/*
 * file.c
 */
extern const struct file_operations sysfs_file_operations;
extern const struct file_operations sysfs_bin_operations;

int sysfs_add_file(struct sysfs_dirent *dir_sd,
		   const struct attribute *attr, int type);

int sysfs_add_file_mode_ns(struct sysfs_dirent *dir_sd,
			   const struct attribute *attr, int type,
			   umode_t amode, const void *ns);
void sysfs_unmap_bin_file(struct sysfs_dirent *sd);

/*
 * symlink.c
 */
extern const struct inode_operations sysfs_symlink_inode_operations;
int sysfs_create_link_sd(struct sysfs_dirent *sd, struct kobject *target,
			 const char *name);
