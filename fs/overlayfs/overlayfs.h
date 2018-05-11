/*
 *
 * Copyright (C) 2011 Novell Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/uuid.h>
#include <linux/fs.h>
#include "ovl_entry.h"

enum ovl_path_type {
	__OVL_PATH_UPPER	= (1 << 0),
	__OVL_PATH_MERGE	= (1 << 1),
	__OVL_PATH_ORIGIN	= (1 << 2),
};

#define OVL_TYPE_UPPER(type)	((type) & __OVL_PATH_UPPER)
#define OVL_TYPE_MERGE(type)	((type) & __OVL_PATH_MERGE)
#define OVL_TYPE_ORIGIN(type)	((type) & __OVL_PATH_ORIGIN)

#define OVL_XATTR_PREFIX XATTR_TRUSTED_PREFIX "overlay."
#define OVL_XATTR_OPAQUE OVL_XATTR_PREFIX "opaque"
#define OVL_XATTR_REDIRECT OVL_XATTR_PREFIX "redirect"
#define OVL_XATTR_ORIGIN OVL_XATTR_PREFIX "origin"
#define OVL_XATTR_IMPURE OVL_XATTR_PREFIX "impure"
#define OVL_XATTR_NLINK OVL_XATTR_PREFIX "nlink"
#define OVL_XATTR_UPPER OVL_XATTR_PREFIX "upper"
#define OVL_XATTR_METACOPY OVL_XATTR_PREFIX "metacopy"

enum ovl_inode_flag {
	/* Pure upper dir that may contain non pure upper entries */
	OVL_IMPURE,
	/* Non-merge dir that may contain whiteout entries */
	OVL_WHITEOUTS,
	OVL_INDEX,
	OVL_UPPERDATA,
};

enum ovl_entry_flag {
	OVL_E_UPPER_ALIAS,
	OVL_E_OPAQUE,
	OVL_E_CONNECTED,
};

/*
 * The tuple (fh,uuid) is a universal unique identifier for a copy up origin,
 * where:
 * origin.fh	- exported file handle of the lower file
 * origin.uuid	- uuid of the lower filesystem
 */
#define OVL_FH_VERSION	0
#define OVL_FH_MAGIC	0xfb

/* CPU byte order required for fid decoding:  */
#define OVL_FH_FLAG_BIG_ENDIAN	(1 << 0)
#define OVL_FH_FLAG_ANY_ENDIAN	(1 << 1)
/* Is the real inode encoded in fid an upper inode? */
#define OVL_FH_FLAG_PATH_UPPER	(1 << 2)

#define OVL_FH_FLAG_ALL (OVL_FH_FLAG_BIG_ENDIAN | OVL_FH_FLAG_ANY_ENDIAN | \
			 OVL_FH_FLAG_PATH_UPPER)

#if defined(__LITTLE_ENDIAN)
#define OVL_FH_FLAG_CPU_ENDIAN 0
#elif defined(__BIG_ENDIAN)
#define OVL_FH_FLAG_CPU_ENDIAN OVL_FH_FLAG_BIG_ENDIAN
#else
#error Endianness not defined
#endif

/* The type returned by overlay exportfs ops when encoding an ovl_fh handle */
#define OVL_FILEID	0xfb

/* On-disk and in-memeory format for redirect by file handle */
struct ovl_fh {
	u8 version;	/* 0 */
	u8 magic;	/* 0xfb */
	u8 len;		/* size of this header + size of fid */
	u8 flags;	/* OVL_FH_FLAG_* */
	u8 type;	/* fid_type of fid */
	uuid_t uuid;	/* uuid of filesystem */
	u8 fid[0];	/* file identifier */
} __packed;

static inline int ovl_do_rmdir(struct inode *dir, struct dentry *dentry)
{
	int err = vfs_rmdir(dir, dentry);

	pr_debug("rmdir(%pd2) = %i\n", dentry, err);
	return err;
}

static inline int ovl_do_unlink(struct inode *dir, struct dentry *dentry)
{
	int err = vfs_unlink(dir, dentry, NULL);

	pr_debug("unlink(%pd2) = %i\n", dentry, err);
	return err;
}

static inline int ovl_do_link(struct dentry *old_dentry, struct inode *dir,
			      struct dentry *new_dentry)
{
	int err = vfs_link(old_dentry, dir, new_dentry, NULL);

	pr_debug("link(%pd2, %pd2) = %i\n", old_dentry, new_dentry, err);
	return err;
}

static inline int ovl_do_create(struct inode *dir, struct dentry *dentry,
				umode_t mode)
{
	int err = vfs_create(dir, dentry, mode, true);

	pr_debug("create(%pd2, 0%o) = %i\n", dentry, mode, err);
	return err;
}

static inline int ovl_do_mkdir(struct inode *dir, struct dentry *dentry,
			       umode_t mode)
{
	int err = vfs_mkdir(dir, dentry, mode);
	pr_debug("mkdir(%pd2, 0%o) = %i\n", dentry, mode, err);
	return err;
}

static inline int ovl_do_mknod(struct inode *dir, struct dentry *dentry,
			       umode_t mode, dev_t dev)
{
	int err = vfs_mknod(dir, dentry, mode, dev);

	pr_debug("mknod(%pd2, 0%o, 0%o) = %i\n", dentry, mode, dev, err);
	return err;
}

static inline int ovl_do_symlink(struct inode *dir, struct dentry *dentry,
				 const char *oldname)
{
	int err = vfs_symlink(dir, dentry, oldname);

	pr_debug("symlink(\"%s\", %pd2) = %i\n", oldname, dentry, err);
	return err;
}

static inline int ovl_do_setxattr(struct dentry *dentry, const char *name,
				  const void *value, size_t size, int flags)
{
	int err = vfs_setxattr(dentry, name, value, size, flags);
	pr_debug("setxattr(%pd2, \"%s\", \"%*s\", 0x%x) = %i\n",
		 dentry, name, (int) size, (char *) value, flags, err);
	return err;
}

static inline int ovl_do_removexattr(struct dentry *dentry, const char *name)
{
	int err = vfs_removexattr(dentry, name);
	pr_debug("removexattr(%pd2, \"%s\") = %i\n", dentry, name, err);
	return err;
}

static inline int ovl_do_rename(struct inode *olddir, struct dentry *olddentry,
				struct inode *newdir, struct dentry *newdentry,
				unsigned int flags)
{
	int err;

	pr_debug("rename(%pd2, %pd2, 0x%x)\n", olddentry, newdentry, flags);
	err = vfs_rename(olddir, olddentry, newdir, newdentry, NULL, flags);
	if (err) {
		pr_debug("...rename(%pd2, %pd2, ...) = %i\n",
			 olddentry, newdentry, err);
	}
	return err;
}

static inline int ovl_do_whiteout(struct inode *dir, struct dentry *dentry)
{
	int err = vfs_whiteout(dir, dentry);
	pr_debug("whiteout(%pd2) = %i\n", dentry, err);
	return err;
}

static inline struct dentry *ovl_do_tmpfile(struct dentry *dentry, umode_t mode)
{
	struct dentry *ret = vfs_tmpfile(dentry, mode, 0);
	int err = PTR_ERR_OR_ZERO(ret);

	pr_debug("tmpfile(%pd2, 0%o) = %i\n", dentry, mode, err);
	return ret;
}

static inline bool ovl_open_flags_need_copy_up(int flags)
{
	if (!flags)
		return false;

	return ((OPEN_FMODE(flags) & FMODE_WRITE) || (flags & O_TRUNC));
}

/* util.c */
int ovl_want_write(struct dentry *dentry);
void ovl_drop_write(struct dentry *dentry);
struct dentry *ovl_workdir(struct dentry *dentry);
const struct cred *ovl_override_creds(struct super_block *sb);
struct super_block *ovl_same_sb(struct super_block *sb);
int ovl_can_decode_fh(struct super_block *sb);
struct dentry *ovl_indexdir(struct super_block *sb);
bool ovl_index_all(struct super_block *sb);
bool ovl_verify_lower(struct super_block *sb);
struct ovl_entry *ovl_alloc_entry(unsigned int numlower);
bool ovl_dentry_remote(struct dentry *dentry);
bool ovl_dentry_weird(struct dentry *dentry);
enum ovl_path_type ovl_path_type(struct dentry *dentry);
void ovl_path_upper(struct dentry *dentry, struct path *path);
void ovl_path_lower(struct dentry *dentry, struct path *path);
enum ovl_path_type ovl_path_real(struct dentry *dentry, struct path *path);
struct dentry *ovl_dentry_upper(struct dentry *dentry);
struct dentry *ovl_dentry_lower(struct dentry *dentry);
struct ovl_layer *ovl_layer_lower(struct dentry *dentry);
struct dentry *ovl_dentry_real(struct dentry *dentry);
struct dentry *ovl_i_dentry_upper(struct inode *inode);
struct inode *ovl_inode_upper(struct inode *inode);
struct inode *ovl_inode_lower(struct inode *inode);
struct inode *ovl_inode_real(struct inode *inode);
struct ovl_dir_cache *ovl_dir_cache(struct inode *inode);
void ovl_set_dir_cache(struct inode *inode, struct ovl_dir_cache *cache);
void ovl_dentry_set_flag(unsigned long flag, struct dentry *dentry);
void ovl_dentry_clear_flag(unsigned long flag, struct dentry *dentry);
bool ovl_dentry_test_flag(unsigned long flag, struct dentry *dentry);
bool ovl_dentry_is_opaque(struct dentry *dentry);
bool ovl_dentry_is_whiteout(struct dentry *dentry);
void ovl_dentry_set_opaque(struct dentry *dentry);
bool ovl_dentry_has_upper_alias(struct dentry *dentry);
void ovl_dentry_set_upper_alias(struct dentry *dentry);
bool ovl_dentry_needs_data_copy_up(struct dentry *dentry, int flags);
bool ovl_dentry_needs_data_copy_up_locked(struct dentry *dentry, int flags);
bool ovl_has_upperdata(struct inode *inode);
void ovl_set_upperdata(struct inode *inode);
bool ovl_redirect_dir(struct super_block *sb);
const char *ovl_dentry_get_redirect(struct dentry *dentry);
void ovl_dentry_set_redirect(struct dentry *dentry, const char *redirect);
void ovl_inode_init(struct inode *inode, struct dentry *upperdentry,
		    struct dentry *lowerdentry);
void ovl_inode_update(struct inode *inode, struct dentry *upperdentry);
void ovl_dir_modified(struct dentry *dentry, bool impurity);
u64 ovl_dentry_version_get(struct dentry *dentry);
bool ovl_is_whiteout(struct dentry *dentry);
struct file *ovl_path_open(struct path *path, int flags);
int ovl_copy_up_start(struct dentry *dentry, int flags);
void ovl_copy_up_end(struct dentry *dentry);
bool ovl_already_copied_up(struct dentry *dentry, int flags);
bool ovl_check_origin_xattr(struct dentry *dentry);
bool ovl_check_dir_xattr(struct dentry *dentry, const char *name);
int ovl_check_setxattr(struct dentry *dentry, struct dentry *upperdentry,
		       const char *name, const void *value, size_t size,
		       int xerr);
int ovl_set_impure(struct dentry *dentry, struct dentry *upperdentry);
void ovl_set_flag(unsigned long flag, struct inode *inode);
void ovl_clear_flag(unsigned long flag, struct inode *inode);
bool ovl_test_flag(unsigned long flag, struct inode *inode);
bool ovl_inuse_trylock(struct dentry *dentry);
void ovl_inuse_unlock(struct dentry *dentry);
bool ovl_need_index(struct dentry *dentry);
int ovl_nlink_start(struct dentry *dentry, bool *locked);
void ovl_nlink_end(struct dentry *dentry, bool locked);
int ovl_lock_rename_workdir(struct dentry *workdir, struct dentry *upperdir);
int ovl_check_metacopy_xattr(struct dentry *dentry);

static inline bool ovl_is_impuredir(struct dentry *dentry)
{
	return ovl_check_dir_xattr(dentry, OVL_XATTR_IMPURE);
}

static inline unsigned int ovl_xino_bits(struct super_block *sb)
{
	struct ovl_fs *ofs = sb->s_fs_info;

	return ofs->xino_bits;
}


/* namei.c */
int ovl_check_fh_len(struct ovl_fh *fh, int fh_len);
struct dentry *ovl_decode_real_fh(struct ovl_fh *fh, struct vfsmount *mnt,
				  bool connected);
int ovl_check_origin_fh(struct ovl_fs *ofs, struct ovl_fh *fh, bool connected,
			struct dentry *upperdentry, struct ovl_path **stackp);
int ovl_verify_set_fh(struct dentry *dentry, const char *name,
		      struct dentry *real, bool is_upper, bool set);
struct dentry *ovl_index_upper(struct ovl_fs *ofs, struct dentry *index);
int ovl_verify_index(struct ovl_fs *ofs, struct dentry *index);
int ovl_get_index_name(struct dentry *origin, struct qstr *name);
struct dentry *ovl_get_index_fh(struct ovl_fs *ofs, struct ovl_fh *fh);
struct dentry *ovl_lookup_index(struct ovl_fs *ofs, struct dentry *upper,
				struct dentry *origin, bool verify);
int ovl_path_next(int idx, struct dentry *dentry, struct path *path);
struct dentry *ovl_lookup(struct inode *dir, struct dentry *dentry,
			  unsigned int flags);
bool ovl_lower_positive(struct dentry *dentry);

static inline int ovl_verify_origin(struct dentry *upper,
				    struct dentry *origin, bool set)
{
	return ovl_verify_set_fh(upper, OVL_XATTR_ORIGIN, origin, false, set);
}

static inline int ovl_verify_upper(struct dentry *index,
				    struct dentry *upper, bool set)
{
	return ovl_verify_set_fh(index, OVL_XATTR_UPPER, upper, true, set);
}

/* readdir.c */
extern const struct file_operations ovl_dir_operations;
int ovl_check_empty_dir(struct dentry *dentry, struct list_head *list);
void ovl_cleanup_whiteouts(struct dentry *upper, struct list_head *list);
void ovl_cache_free(struct list_head *list);
void ovl_dir_cache_free(struct inode *inode);
int ovl_check_d_type_supported(struct path *realpath);
void ovl_workdir_cleanup(struct inode *dir, struct vfsmount *mnt,
			 struct dentry *dentry, int level);
int ovl_indexdir_cleanup(struct ovl_fs *ofs);

/* inode.c */
int ovl_set_nlink_upper(struct dentry *dentry);
int ovl_set_nlink_lower(struct dentry *dentry);
unsigned int ovl_get_nlink(struct dentry *lowerdentry,
			   struct dentry *upperdentry,
			   unsigned int fallback);
int ovl_setattr(struct dentry *dentry, struct iattr *attr);
int ovl_getattr(const struct path *path, struct kstat *stat,
		u32 request_mask, unsigned int flags);
int ovl_permission(struct inode *inode, int mask);
int ovl_xattr_set(struct dentry *dentry, struct inode *inode, const char *name,
		  const void *value, size_t size, int flags);
int ovl_xattr_get(struct dentry *dentry, struct inode *inode, const char *name,
		  void *value, size_t size);
ssize_t ovl_listxattr(struct dentry *dentry, char *list, size_t size);
struct posix_acl *ovl_get_acl(struct inode *inode, int type);
int ovl_update_time(struct inode *inode, struct timespec64 *ts, int flags);
bool ovl_is_private_xattr(const char *name);

struct ovl_inode_params {
	struct inode *newinode;
	struct dentry *upperdentry;
	struct ovl_path *lowerpath;
	struct dentry *index;
	unsigned int numlower;
	char *redirect;
};
struct inode *ovl_new_inode(struct super_block *sb, umode_t mode, dev_t rdev);
struct inode *ovl_lookup_inode(struct super_block *sb, struct dentry *real,
			       bool is_upper);
struct inode *ovl_get_inode(struct super_block *sb,
			    struct ovl_inode_params *oip);
static inline void ovl_copyattr(struct inode *from, struct inode *to)
{
	to->i_uid = from->i_uid;
	to->i_gid = from->i_gid;
	to->i_mode = from->i_mode;
	to->i_atime = from->i_atime;
	to->i_mtime = from->i_mtime;
	to->i_ctime = from->i_ctime;
	i_size_write(to, i_size_read(from));
}

static inline void ovl_copyflags(struct inode *from, struct inode *to)
{
	unsigned int mask = S_SYNC | S_IMMUTABLE | S_APPEND | S_NOATIME;

	inode_set_flags(to, from->i_flags & mask, mask);
}

/* dir.c */
extern const struct inode_operations ovl_dir_inode_operations;
int ovl_cleanup_and_whiteout(struct dentry *workdir, struct inode *dir,
			     struct dentry *dentry);
struct ovl_cattr {
	dev_t rdev;
	umode_t mode;
	const char *link;
	struct dentry *hardlink;
};

#define OVL_CATTR(m) (&(struct ovl_cattr) { .mode = (m) })

struct dentry *ovl_create_real(struct inode *dir, struct dentry *newdentry,
			       struct ovl_cattr *attr);
int ovl_cleanup(struct inode *dir, struct dentry *dentry);
struct dentry *ovl_create_temp(struct dentry *workdir, struct ovl_cattr *attr);

/* file.c */
extern const struct file_operations ovl_file_operations;

/* copy_up.c */
int ovl_copy_up(struct dentry *dentry);
int ovl_copy_up_flags(struct dentry *dentry, int flags);
int ovl_open_maybe_copy_up(struct dentry *dentry, unsigned int file_flags);
int ovl_copy_xattr(struct dentry *old, struct dentry *new);
int ovl_set_attr(struct dentry *upper, struct kstat *stat);
struct ovl_fh *ovl_encode_real_fh(struct dentry *real, bool is_upper);
int ovl_set_origin(struct dentry *dentry, struct dentry *lower,
		   struct dentry *upper);

/* export.c */
extern const struct export_operations ovl_export_operations;
