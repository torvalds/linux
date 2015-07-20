/*
 * fs/sdcardfs/sdcardfs.h
 *
 * The sdcardfs v2.0
 *   This file system replaces the sdcard daemon on Android
 *   On version 2.0, some of the daemon functions have been ported
 *   to support the multi-user concepts of Android 4.4
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd
 *   Authors: Daeho Jeong, Woojoong Lee, Seunghwan Hyun,
 *               Sunghwan Yun, Sungjong Seo
 *
 * This program has been developed as a stackable file system based on
 * the WrapFS which written by
 *
 * Copyright (c) 1998-2011 Erez Zadok
 * Copyright (c) 2009     Shrikar Archak
 * Copyright (c) 2003-2011 Stony Brook University
 * Copyright (c) 2003-2011 The Research Foundation of SUNY
 *
 * This file is dual licensed.  It may be redistributed and/or modified
 * under the terms of the Apache 2.0 License OR version 2 of the GNU
 * General Public License.
 */

#ifndef _SDCARDFS_H_
#define _SDCARDFS_H_

#include <linux/dcache.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/seq_file.h>
#include <linux/statfs.h>
#include <linux/fs_stack.h>
#include <linux/magic.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/security.h>
#include <linux/string.h>
#include "multiuser.h"

/* the file system name */
#define SDCARDFS_NAME "sdcardfs"

/* sdcardfs root inode number */
#define SDCARDFS_ROOT_INO     1

/* useful for tracking code reachability */
#define UDBG printk(KERN_DEFAULT "DBG:%s:%s:%d\n", __FILE__, __func__, __LINE__)

#define SDCARDFS_DIRENT_SIZE 256

/* temporary static uid settings for development */
#define AID_ROOT             0	/* uid for accessing /mnt/sdcard & extSdcard */
#define AID_MEDIA_RW      1023	/* internal media storage write access */

#define AID_SDCARD_RW     1015	/* external storage write access */
#define AID_SDCARD_R      1028	/* external storage read access */
#define AID_SDCARD_PICS   1033	/* external storage photos access */
#define AID_SDCARD_AV     1034	/* external storage audio/video access */
#define AID_SDCARD_ALL    1035	/* access all users external storage */

#define AID_PACKAGE_INFO  1027

#define fix_derived_permission(x)	\
	do {						\
		(x)->i_uid = SDCARDFS_I(x)->d_uid;	\
		(x)->i_gid = SDCARDFS_I(x)->d_gid;	\
		(x)->i_mode = ((x)->i_mode & S_IFMT) | SDCARDFS_I(x)->d_mode;\
	} while (0)

/* OVERRIDE_CRED() and REVERT_CRED()
 * 	OVERRID_CRED()
 * 		backup original task->cred
 * 		and modifies task->cred->fsuid/fsgid to specified value.
 *	REVERT_CRED()
 * 		restore original task->cred->fsuid/fsgid.
 * These two macro should be used in pair, and OVERRIDE_CRED() should be
 * placed at the beginning of a function, right after variable declaration.
 */
#define OVERRIDE_CRED(sdcardfs_sbi, saved_cred)		\
	saved_cred = override_fsids(sdcardfs_sbi);	\
	if (!saved_cred) { return -ENOMEM; }

#define OVERRIDE_CRED_PTR(sdcardfs_sbi, saved_cred)	\
	saved_cred = override_fsids(sdcardfs_sbi);	\
	if (!saved_cred) { return ERR_PTR(-ENOMEM); }

#define REVERT_CRED(saved_cred)	revert_fsids(saved_cred)

#define DEBUG_CRED()		\
	printk("KAKJAGI: %s:%d fsuid %d fsgid %d\n", 	\
		__FUNCTION__, __LINE__, 		\
		(int)current->cred->fsuid, 		\
		(int)current->cred->fsgid);

/* Android 4.4 support */

/* Permission mode for a specific node. Controls how file permissions
 * are derived for children nodes. */
typedef enum {
	/* Nothing special; this node should just inherit from its parent. */
	PERM_INHERIT,
	/* This node is one level above a normal root; used for legacy layouts
	 * which use the first level to represent user_id. */
	PERM_LEGACY_PRE_ROOT,
	/* This node is "/" */
	PERM_ROOT,
	/* This node is "/Android" */
	PERM_ANDROID,
	/* This node is "/Android/data" */
	PERM_ANDROID_DATA,
	/* This node is "/Android/obb" */
	PERM_ANDROID_OBB,
	/* This node is "/Android/user" */
	PERM_ANDROID_USER,
} perm_t;

/* Permissions structure to derive */
typedef enum {
	DERIVE_NONE,
	DERIVE_LEGACY,
	DERIVE_UNIFIED,
} derive_t;

typedef enum {
	LOWER_FS_EXT4,
	LOWER_FS_FAT,
} lower_fs_t;

struct sdcardfs_sb_info;
struct sdcardfs_mount_options;

/* Do not directly use this function. Use OVERRIDE_CRED() instead. */
const struct cred * override_fsids(struct sdcardfs_sb_info* sbi);
/* Do not directly use this function, use REVERT_CRED() instead. */
void revert_fsids(const struct cred * old_cred);

/* operations vectors defined in specific files */
extern const struct file_operations sdcardfs_main_fops;
extern const struct file_operations sdcardfs_dir_fops;
extern const struct inode_operations sdcardfs_main_iops;
extern const struct inode_operations sdcardfs_dir_iops;
extern const struct inode_operations sdcardfs_symlink_iops;
extern const struct super_operations sdcardfs_sops;
extern const struct dentry_operations sdcardfs_ci_dops;
extern const struct address_space_operations sdcardfs_aops, sdcardfs_dummy_aops;
extern const struct vm_operations_struct sdcardfs_vm_ops;

extern int sdcardfs_init_inode_cache(void);
extern void sdcardfs_destroy_inode_cache(void);
extern int sdcardfs_init_dentry_cache(void);
extern void sdcardfs_destroy_dentry_cache(void);
extern int new_dentry_private_data(struct dentry *dentry);
extern void free_dentry_private_data(struct dentry *dentry);
extern struct dentry *sdcardfs_lookup(struct inode *dir, struct dentry *dentry,
				    struct nameidata *nd);
extern int sdcardfs_interpose(struct dentry *dentry, struct super_block *sb,
			    struct path *lower_path);

/* file private data */
struct sdcardfs_file_info {
	struct file *lower_file;
	const struct vm_operations_struct *lower_vm_ops;
};

/* sdcardfs inode data in memory */
struct sdcardfs_inode_info {
	struct inode *lower_inode;
	/* state derived based on current position in hierachy
	 * caution: d_mode does not include file types
	 */
	perm_t perm;
	userid_t userid;
	uid_t d_uid;
	gid_t d_gid;
	mode_t d_mode;

	struct inode vfs_inode;
};

/* sdcardfs dentry data in memory */
struct sdcardfs_dentry_info {
	spinlock_t lock;	/* protects lower_path */
	struct path lower_path;
	struct path orig_path;
};

struct sdcardfs_mount_options {
	uid_t fs_low_uid;
	gid_t fs_low_gid;
	gid_t write_gid;
	int split_perms;
	derive_t derive;
	lower_fs_t lower_fs;
	unsigned int reserved_mb;
};

/* sdcardfs super-block data in memory */
struct sdcardfs_sb_info {
	struct super_block *lower_sb;
	/* derived perm policy : some of options have been added
	 * to sdcardfs_mount_options (Android 4.4 support) */
	struct sdcardfs_mount_options options;
	spinlock_t lock;	/* protects obbpath */
	char *obbpath_s;
	struct path obbpath;
	void *pkgl_id;
};

/*
 * inode to private data
 *
 * Since we use containers and the struct inode is _inside_ the
 * sdcardfs_inode_info structure, SDCARDFS_I will always (given a non-NULL
 * inode pointer), return a valid non-NULL pointer.
 */
static inline struct sdcardfs_inode_info *SDCARDFS_I(const struct inode *inode)
{
	return container_of(inode, struct sdcardfs_inode_info, vfs_inode);
}

/* dentry to private data */
#define SDCARDFS_D(dent) ((struct sdcardfs_dentry_info *)(dent)->d_fsdata)

/* superblock to private data */
#define SDCARDFS_SB(super) ((struct sdcardfs_sb_info *)(super)->s_fs_info)

/* file to private Data */
#define SDCARDFS_F(file) ((struct sdcardfs_file_info *)((file)->private_data))

/* file to lower file */
static inline struct file *sdcardfs_lower_file(const struct file *f)
{
	return SDCARDFS_F(f)->lower_file;
}

static inline void sdcardfs_set_lower_file(struct file *f, struct file *val)
{
	SDCARDFS_F(f)->lower_file = val;
}

/* inode to lower inode. */
static inline struct inode *sdcardfs_lower_inode(const struct inode *i)
{
	return SDCARDFS_I(i)->lower_inode;
}

static inline void sdcardfs_set_lower_inode(struct inode *i, struct inode *val)
{
	SDCARDFS_I(i)->lower_inode = val;
}

/* superblock to lower superblock */
static inline struct super_block *sdcardfs_lower_super(
	const struct super_block *sb)
{
	return SDCARDFS_SB(sb)->lower_sb;
}

static inline void sdcardfs_set_lower_super(struct super_block *sb,
					  struct super_block *val)
{
	SDCARDFS_SB(sb)->lower_sb = val;
}

/* path based (dentry/mnt) macros */
static inline void pathcpy(struct path *dst, const struct path *src)
{
	dst->dentry = src->dentry;
	dst->mnt = src->mnt;
}

/* sdcardfs_get_pname functions calls path_get()
 * therefore, the caller must call "proper" path_put functions
 */
#define SDCARDFS_DENT_FUNC(pname) \
static inline void sdcardfs_get_##pname(const struct dentry *dent, \
					struct path *pname) \
{ \
	spin_lock(&SDCARDFS_D(dent)->lock); \
	pathcpy(pname, &SDCARDFS_D(dent)->pname); \
	path_get(pname); \
	spin_unlock(&SDCARDFS_D(dent)->lock); \
	return; \
} \
static inline void sdcardfs_put_##pname(const struct dentry *dent, \
					struct path *pname) \
{ \
	path_put(pname); \
	return; \
} \
static inline void sdcardfs_set_##pname(const struct dentry *dent, \
					struct path *pname) \
{ \
	spin_lock(&SDCARDFS_D(dent)->lock); \
	pathcpy(&SDCARDFS_D(dent)->pname, pname); \
	spin_unlock(&SDCARDFS_D(dent)->lock); \
	return; \
} \
static inline void sdcardfs_reset_##pname(const struct dentry *dent) \
{ \
	spin_lock(&SDCARDFS_D(dent)->lock); \
	SDCARDFS_D(dent)->pname.dentry = NULL; \
	SDCARDFS_D(dent)->pname.mnt = NULL; \
	spin_unlock(&SDCARDFS_D(dent)->lock); \
	return; \
} \
static inline void sdcardfs_put_reset_##pname(const struct dentry *dent) \
{ \
	struct path pname; \
	spin_lock(&SDCARDFS_D(dent)->lock); \
	if(SDCARDFS_D(dent)->pname.dentry) { \
		pathcpy(&pname, &SDCARDFS_D(dent)->pname); \
		SDCARDFS_D(dent)->pname.dentry = NULL; \
		SDCARDFS_D(dent)->pname.mnt = NULL; \
		spin_unlock(&SDCARDFS_D(dent)->lock); \
		path_put(&pname); \
	} else \
		spin_unlock(&SDCARDFS_D(dent)->lock); \
	return; \
}

SDCARDFS_DENT_FUNC(lower_path)
SDCARDFS_DENT_FUNC(orig_path)

static inline int has_graft_path(const struct dentry *dent)
{
	int ret = 0;

	spin_lock(&SDCARDFS_D(dent)->lock);
	if (SDCARDFS_D(dent)->orig_path.dentry != NULL)
		ret = 1;
	spin_unlock(&SDCARDFS_D(dent)->lock);

	return ret;
}

static inline void sdcardfs_get_real_lower(const struct dentry *dent,
						struct path *real_lower)
{
	/* in case of a local obb dentry
	 * the orig_path should be returned
	 */
	if(has_graft_path(dent))
		sdcardfs_get_orig_path(dent, real_lower);
	else
		sdcardfs_get_lower_path(dent, real_lower);
}

static inline void sdcardfs_put_real_lower(const struct dentry *dent,
						struct path *real_lower)
{
	if(has_graft_path(dent))
		sdcardfs_put_orig_path(dent, real_lower);
	else
		sdcardfs_put_lower_path(dent, real_lower);
}

/* for packagelist.c */
extern int get_caller_has_rw_locked(void *pkgl_id, derive_t derive);
extern appid_t get_appid(void *pkgl_id, const char *app_name);
extern int check_caller_access_to_name(struct inode *parent_node, const char* name,
                                        derive_t derive, int w_ok, int has_rw);
extern int open_flags_to_access_mode(int open_flags);
extern void * packagelist_create(gid_t write_gid);
extern void packagelist_destroy(void *pkgl_id);
extern int packagelist_init(void);
extern void packagelist_exit(void);

/* for derived_perm.c */
extern void setup_derived_state(struct inode *inode, perm_t perm,
			userid_t userid, uid_t uid, gid_t gid, mode_t mode);
extern void get_derived_permission(struct dentry *parent, struct dentry *dentry);
extern void update_derived_permission(struct dentry *dentry);
extern int need_graft_path(struct dentry *dentry);
extern int is_base_obbpath(struct dentry *dentry);
extern int is_obbpath_invalid(struct dentry *dentry);
extern int setup_obb_dentry(struct dentry *dentry, struct path *lower_path);

/* locking helpers */
static inline struct dentry *lock_parent(struct dentry *dentry)
{
	struct dentry *dir = dget_parent(dentry);
	mutex_lock_nested(&dir->d_inode->i_mutex, I_MUTEX_PARENT);
	return dir;
}

static inline void unlock_dir(struct dentry *dir)
{
	mutex_unlock(&dir->d_inode->i_mutex);
	dput(dir);
}

static inline int prepare_dir(const char *path_s, uid_t uid, gid_t gid, mode_t mode)
{
	int err;
	struct dentry *dent;
	struct iattr attrs;
	struct nameidata nd;

	err = kern_path_parent(path_s, &nd);
	if (err) {
		if (err == -EEXIST)
			err = 0;
		goto out;
	}

	dent = lookup_create(&nd, 1);
	if (IS_ERR(dent)) {
		err = PTR_ERR(dent);
		if (err == -EEXIST)
			err = 0;
		goto out_unlock;
	}

	err = vfs_mkdir(nd.path.dentry->d_inode, dent, mode);
	if (err) {
		if (err == -EEXIST)
			err = 0;
		goto out_dput;
	}

	attrs.ia_uid = uid;
	attrs.ia_gid = gid;
	attrs.ia_valid = ATTR_UID | ATTR_GID;
	mutex_lock(&dent->d_inode->i_mutex);
	notify_change(dent, &attrs);
	mutex_unlock(&dent->d_inode->i_mutex);

out_dput:
	dput(dent);

out_unlock:
	/* parent dentry locked by lookup_create */
	mutex_unlock(&nd.path.dentry->d_inode->i_mutex);
	path_put(&nd.path);

out:
	return err;
}

/*
 * Return 1, if a disk has enough free space, otherwise 0.
 * We assume that any files can not be overwritten.
 */
static inline int check_min_free_space(struct dentry *dentry, size_t size, int dir)
{
	int err;
	struct path lower_path;
	struct kstatfs statfs;
	u64 avail;
	struct sdcardfs_sb_info *sbi = SDCARDFS_SB(dentry->d_sb);

	if (sbi->options.reserved_mb) {
		/* Get fs stat of lower filesystem. */
		sdcardfs_get_lower_path(dentry, &lower_path);
		err = vfs_statfs(&lower_path, &statfs);
		sdcardfs_put_lower_path(dentry, &lower_path);

		if (unlikely(err))
			return 0;

		/* Invalid statfs informations. */
		if (unlikely(statfs.f_bsize == 0))
			return 0;

		/* if you are checking directory, set size to f_bsize. */
		if (unlikely(dir))
			size = statfs.f_bsize;

		/* available size */
		avail = statfs.f_bavail * statfs.f_bsize;

		/* not enough space */
		if ((u64)size > avail)
			return 0;

		/* enough space */
		if ((avail - size) > (sbi->options.reserved_mb * 1024 * 1024))
			return 1;

		return 0;
	} else
		return 1;
}

#endif	/* not _SDCARDFS_H_ */
