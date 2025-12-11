/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *
 *   Copyright (c) International Business Machines  Corp., 2002, 2007
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */

#ifndef _CIFSFS_H
#define _CIFSFS_H

#include <linux/hash.h>

#define ROOT_I 2

/*
 * ino_t is 32-bits on 32-bit arch. We have to squash the 64-bit value down
 * so that it will fit. We use hash_64 to convert the value to 31 bits, and
 * then add 1, to ensure that we don't end up with a 0 as the value.
 */
static inline ino_t
cifs_uniqueid_to_ino_t(u64 fileid)
{
	if ((sizeof(ino_t)) < (sizeof(u64)))
		return (ino_t)hash_64(fileid, (sizeof(ino_t) * 8) - 1) + 1;

	return (ino_t)fileid;

}

static inline void cifs_set_time(struct dentry *dentry, unsigned long time)
{
	dentry->d_fsdata = (void *) time;
}

static inline unsigned long cifs_get_time(struct dentry *dentry)
{
	return (unsigned long) dentry->d_fsdata;
}

extern struct file_system_type cifs_fs_type, smb3_fs_type;
extern const struct address_space_operations cifs_addr_ops;
extern const struct address_space_operations cifs_addr_ops_smallbuf;

/* Functions related to super block operations */
void cifs_sb_active(struct super_block *sb);
void cifs_sb_deactive(struct super_block *sb);

/* Functions related to inodes */
extern const struct inode_operations cifs_dir_inode_ops;
struct inode *cifs_root_iget(struct super_block *sb);
int cifs_create(struct mnt_idmap *idmap, struct inode *inode,
		struct dentry *direntry, umode_t mode, bool excl);
int cifs_atomic_open(struct inode *inode, struct dentry *direntry,
		     struct file *file, unsigned int oflags, umode_t mode);
struct dentry *cifs_lookup(struct inode *parent_dir_inode,
			   struct dentry *direntry, unsigned int flags);
int cifs_unlink(struct inode *dir, struct dentry *dentry);
int cifs_hardlink(struct dentry *old_file, struct inode *inode,
		  struct dentry *direntry);
int cifs_mknod(struct mnt_idmap *idmap, struct inode *inode,
	       struct dentry *direntry, umode_t mode, dev_t device_number);
struct dentry *cifs_mkdir(struct mnt_idmap *idmap, struct inode *inode,
			  struct dentry *direntry, umode_t mode);
int cifs_rmdir(struct inode *inode, struct dentry *direntry);
int cifs_rename2(struct mnt_idmap *idmap, struct inode *source_dir,
		 struct dentry *source_dentry, struct inode *target_dir,
		 struct dentry *target_dentry, unsigned int flags);
int cifs_revalidate_file_attr(struct file *filp);
int cifs_revalidate_dentry_attr(struct dentry *dentry);
int cifs_revalidate_file(struct file *filp);
int cifs_revalidate_dentry(struct dentry *dentry);
int cifs_revalidate_mapping(struct inode *inode);
int cifs_zap_mapping(struct inode *inode);
int cifs_getattr(struct mnt_idmap *idmap, const struct path *path,
		 struct kstat *stat, u32 request_mask, unsigned int flags);
int cifs_setattr(struct mnt_idmap *idmap, struct dentry *direntry,
		 struct iattr *attrs);
int cifs_fiemap(struct inode *inode, struct fiemap_extent_info *fei, u64 start,
		u64 len);

extern const struct inode_operations cifs_file_inode_ops;
extern const struct inode_operations cifs_symlink_inode_ops;
extern const struct inode_operations cifs_namespace_inode_operations;


/* Functions related to files and directories */
extern const struct netfs_request_ops cifs_req_ops;
extern const struct file_operations cifs_file_ops;
extern const struct file_operations cifs_file_direct_ops; /* if directio mnt */
extern const struct file_operations cifs_file_strict_ops; /* if strictio mnt */
extern const struct file_operations cifs_file_nobrl_ops; /* no brlocks */
extern const struct file_operations cifs_file_direct_nobrl_ops;
extern const struct file_operations cifs_file_strict_nobrl_ops;
int cifs_open(struct inode *inode, struct file *file);
int cifs_close(struct inode *inode, struct file *file);
int cifs_closedir(struct inode *inode, struct file *file);
ssize_t cifs_strict_readv(struct kiocb *iocb, struct iov_iter *to);
ssize_t cifs_strict_writev(struct kiocb *iocb, struct iov_iter *from);
ssize_t cifs_file_write_iter(struct kiocb *iocb, struct iov_iter *from);
ssize_t cifs_loose_read_iter(struct kiocb *iocb, struct iov_iter *iter);
int cifs_flock(struct file *file, int cmd, struct file_lock *fl);
int cifs_lock(struct file *file, int cmd, struct file_lock *flock);
int cifs_fsync(struct file *file, loff_t start, loff_t end, int datasync);
int cifs_strict_fsync(struct file *file, loff_t start, loff_t end,
		      int datasync);
int cifs_flush(struct file *file, fl_owner_t id);
int cifs_file_mmap_prepare(struct vm_area_desc *desc);
int cifs_file_strict_mmap_prepare(struct vm_area_desc *desc);
extern const struct file_operations cifs_dir_ops;
int cifs_readdir(struct file *file, struct dir_context *ctx);

/* Functions related to dir entries */
extern const struct dentry_operations cifs_dentry_ops;
extern const struct dentry_operations cifs_ci_dentry_ops;

struct vfsmount *cifs_d_automount(struct path *path);

/* Functions related to symlinks */
const char *cifs_get_link(struct dentry *dentry, struct inode *inode,
			  struct delayed_call *done);
int cifs_symlink(struct mnt_idmap *idmap, struct inode *inode,
		 struct dentry *direntry, const char *symname);

#ifdef CONFIG_CIFS_XATTR
extern const struct xattr_handler * const cifs_xattr_handlers[];
ssize_t cifs_listxattr(struct dentry *direntry, char *data, size_t buf_size);
#else
# define cifs_xattr_handlers NULL
# define cifs_listxattr NULL
#endif

ssize_t cifs_file_copychunk_range(unsigned int xid, struct file *src_file,
				  loff_t off, struct file *dst_file,
				  loff_t destoff, size_t len,
				  unsigned int flags);

long cifs_ioctl(struct file *filep, unsigned int command, unsigned long arg);
void cifs_setsize(struct inode *inode, loff_t offset);

struct smb3_fs_context;
struct dentry *cifs_smb3_do_mount(struct file_system_type *fs_type, int flags,
				  struct smb3_fs_context *old_ctx);

#ifdef CONFIG_CIFS_NFSD_EXPORT
extern const struct export_operations cifs_export_ops;
#endif /* CONFIG_CIFS_NFSD_EXPORT */

/* when changing internal version - update following two lines at same time */
#define SMB3_PRODUCT_BUILD 58
#define CIFS_VERSION   "2.58"
#endif				/* _CIFSFS_H */
