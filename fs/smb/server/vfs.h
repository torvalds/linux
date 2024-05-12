/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2016 Namjae Jeon <linkinjeon@kernel.org>
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#ifndef __KSMBD_VFS_H__
#define __KSMBD_VFS_H__

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <uapi/linux/xattr.h>
#include <linux/posix_acl.h>
#include <linux/unicode.h>

#include "smbacl.h"
#include "xattr.h"

/*
 * Enumeration for stream type.
 */
enum {
	DATA_STREAM	= 1,	/* type $DATA */
	DIR_STREAM		/* type $INDEX_ALLOCATION */
};

/* CreateOptions */
#define CREATE_TREE_CONNECTION			cpu_to_le32(0x00000080)
#define FILE_RESERVE_OPFILTER_LE		cpu_to_le32(0x00100000)

#define CREATE_OPTION_READONLY			0x10000000
/* system. NB not sent over wire */
#define CREATE_OPTION_SPECIAL			0x20000000

struct ksmbd_work;
struct ksmbd_file;
struct ksmbd_conn;

struct ksmbd_dir_info {
	const char	*name;
	char		*wptr;
	char		*rptr;
	int		name_len;
	int		out_buf_len;
	int		num_entry;
	int		data_count;
	int		last_entry_offset;
	bool		hide_dot_file;
	int		flags;
	int		last_entry_off_align;
};

struct ksmbd_readdir_data {
	struct dir_context	ctx;
	union {
		void		*private;
		char		*dirent;
	};

	unsigned int		used;
	unsigned int		dirent_count;
	unsigned int		file_attr;
	struct unicode_map	*um;
};

/* ksmbd kstat wrapper to get valid create time when reading dir entry */
struct ksmbd_kstat {
	struct kstat		*kstat;
	unsigned long long	create_time;
	__le32			file_attributes;
};

int ksmbd_vfs_lock_parent(struct dentry *parent, struct dentry *child);
int ksmbd_vfs_query_maximal_access(struct mnt_idmap *idmap,
				   struct dentry *dentry, __le32 *daccess);
int ksmbd_vfs_create(struct ksmbd_work *work, const char *name, umode_t mode);
int ksmbd_vfs_mkdir(struct ksmbd_work *work, const char *name, umode_t mode);
int ksmbd_vfs_read(struct ksmbd_work *work, struct ksmbd_file *fp,
		   size_t count, loff_t *pos);
int ksmbd_vfs_write(struct ksmbd_work *work, struct ksmbd_file *fp,
		    char *buf, size_t count, loff_t *pos, bool sync,
		    ssize_t *written);
int ksmbd_vfs_fsync(struct ksmbd_work *work, u64 fid, u64 p_id);
int ksmbd_vfs_remove_file(struct ksmbd_work *work, const struct path *path);
int ksmbd_vfs_link(struct ksmbd_work *work,
		   const char *oldname, const char *newname);
int ksmbd_vfs_getattr(const struct path *path, struct kstat *stat);
int ksmbd_vfs_rename(struct ksmbd_work *work, const struct path *old_path,
		     char *newname, int flags);
int ksmbd_vfs_truncate(struct ksmbd_work *work,
		       struct ksmbd_file *fp, loff_t size);
struct srv_copychunk;
int ksmbd_vfs_copy_file_ranges(struct ksmbd_work *work,
			       struct ksmbd_file *src_fp,
			       struct ksmbd_file *dst_fp,
			       struct srv_copychunk *chunks,
			       unsigned int chunk_count,
			       unsigned int *chunk_count_written,
			       unsigned int *chunk_size_written,
			       loff_t  *total_size_written);
ssize_t ksmbd_vfs_listxattr(struct dentry *dentry, char **list);
ssize_t ksmbd_vfs_getxattr(struct mnt_idmap *idmap,
			   struct dentry *dentry,
			   char *xattr_name,
			   char **xattr_buf);
ssize_t ksmbd_vfs_casexattr_len(struct mnt_idmap *idmap,
				struct dentry *dentry, char *attr_name,
				int attr_name_len);
int ksmbd_vfs_setxattr(struct mnt_idmap *idmap,
		       const struct path *path, const char *attr_name,
		       void *attr_value, size_t attr_size, int flags);
int ksmbd_vfs_xattr_stream_name(char *stream_name, char **xattr_stream_name,
				size_t *xattr_stream_name_size, int s_type);
int ksmbd_vfs_remove_xattr(struct mnt_idmap *idmap,
			   const struct path *path, char *attr_name);
int ksmbd_vfs_kern_path_locked(struct ksmbd_work *work, char *name,
			       unsigned int flags, struct path *path,
			       bool caseless);
struct dentry *ksmbd_vfs_kern_path_create(struct ksmbd_work *work,
					  const char *name,
					  unsigned int flags,
					  struct path *path);
int ksmbd_vfs_empty_dir(struct ksmbd_file *fp);
void ksmbd_vfs_set_fadvise(struct file *filp, __le32 option);
int ksmbd_vfs_zero_data(struct ksmbd_work *work, struct ksmbd_file *fp,
			loff_t off, loff_t len);
struct file_allocated_range_buffer;
int ksmbd_vfs_fqar_lseek(struct ksmbd_file *fp, loff_t start, loff_t length,
			 struct file_allocated_range_buffer *ranges,
			 unsigned int in_count, unsigned int *out_count);
int ksmbd_vfs_unlink(struct file *filp);
void *ksmbd_vfs_init_kstat(char **p, struct ksmbd_kstat *ksmbd_kstat);
int ksmbd_vfs_fill_dentry_attrs(struct ksmbd_work *work,
				struct mnt_idmap *idmap,
				struct dentry *dentry,
				struct ksmbd_kstat *ksmbd_kstat);
void ksmbd_vfs_posix_lock_wait(struct file_lock *flock);
int ksmbd_vfs_posix_lock_wait_timeout(struct file_lock *flock, long timeout);
void ksmbd_vfs_posix_lock_unblock(struct file_lock *flock);
int ksmbd_vfs_remove_acl_xattrs(struct mnt_idmap *idmap,
				const struct path *path);
int ksmbd_vfs_remove_sd_xattrs(struct mnt_idmap *idmap, const struct path *path);
int ksmbd_vfs_set_sd_xattr(struct ksmbd_conn *conn,
			   struct mnt_idmap *idmap,
			   const struct path *path,
			   struct smb_ntsd *pntsd, int len);
int ksmbd_vfs_get_sd_xattr(struct ksmbd_conn *conn,
			   struct mnt_idmap *idmap,
			   struct dentry *dentry,
			   struct smb_ntsd **pntsd);
int ksmbd_vfs_set_dos_attrib_xattr(struct mnt_idmap *idmap,
				   const struct path *path,
				   struct xattr_dos_attrib *da);
int ksmbd_vfs_get_dos_attrib_xattr(struct mnt_idmap *idmap,
				   struct dentry *dentry,
				   struct xattr_dos_attrib *da);
int ksmbd_vfs_set_init_posix_acl(struct mnt_idmap *idmap,
				 struct path *path);
int ksmbd_vfs_inherit_posix_acl(struct mnt_idmap *idmap,
				struct path *path,
				struct inode *parent_inode);
#endif /* __KSMBD_VFS_H__ */
