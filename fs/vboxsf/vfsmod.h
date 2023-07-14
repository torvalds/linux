/* SPDX-License-Identifier: MIT */
/*
 * VirtualBox Guest Shared Folders support: module header.
 *
 * Copyright (C) 2006-2018 Oracle Corporation
 */

#ifndef VFSMOD_H
#define VFSMOD_H

#include <linux/backing-dev.h>
#include <linux/idr.h>
#include "shfl_hostintf.h"

#define DIR_BUFFER_SIZE SZ_16K

/* The cast is to prevent assignment of void * to pointers of arbitrary type */
#define VBOXSF_SBI(sb)	((struct vboxsf_sbi *)(sb)->s_fs_info)
#define VBOXSF_I(i)	container_of(i, struct vboxsf_inode, vfs_inode)

struct vboxsf_handle;

struct vboxsf_options {
	unsigned long ttl;
	kuid_t uid;
	kgid_t gid;
	bool dmode_set;
	bool fmode_set;
	umode_t dmode;
	umode_t fmode;
	umode_t dmask;
	umode_t fmask;
};

struct vboxsf_fs_context {
	struct vboxsf_options o;
	char *nls_name;
};

/* per-shared folder information */
struct vboxsf_sbi {
	struct vboxsf_options o;
	struct shfl_fsobjinfo root_info;
	struct idr ino_idr;
	spinlock_t ino_idr_lock; /* This protects ino_idr */
	struct nls_table *nls;
	u32 next_generation;
	u32 root;
	int bdi_id;
};

/* per-inode information */
struct vboxsf_inode {
	/* some information was changed, update data on next revalidate */
	int force_restat;
	/* list of open handles for this inode + lock protecting it */
	struct list_head handle_list;
	/* This mutex protects handle_list accesses */
	struct mutex handle_list_mutex;
	/* The VFS inode struct */
	struct inode vfs_inode;
};

struct vboxsf_dir_info {
	struct list_head info_list;
};

struct vboxsf_dir_buf {
	size_t entries;
	size_t free;
	size_t used;
	void *buf;
	struct list_head head;
};

/* globals */
extern const struct inode_operations vboxsf_dir_iops;
extern const struct inode_operations vboxsf_lnk_iops;
extern const struct inode_operations vboxsf_reg_iops;
extern const struct file_operations vboxsf_dir_fops;
extern const struct file_operations vboxsf_reg_fops;
extern const struct address_space_operations vboxsf_reg_aops;
extern const struct dentry_operations vboxsf_dentry_ops;

/* from file.c */
struct vboxsf_handle *vboxsf_create_sf_handle(struct inode *inode,
					      u64 handle, u32 access_flags);
void vboxsf_release_sf_handle(struct inode *inode, struct vboxsf_handle *sf_handle);

/* from utils.c */
struct inode *vboxsf_new_inode(struct super_block *sb);
int vboxsf_init_inode(struct vboxsf_sbi *sbi, struct inode *inode,
		       const struct shfl_fsobjinfo *info, bool reinit);
int vboxsf_create_at_dentry(struct dentry *dentry,
			    struct shfl_createparms *params);
int vboxsf_stat(struct vboxsf_sbi *sbi, struct shfl_string *path,
		struct shfl_fsobjinfo *info);
int vboxsf_stat_dentry(struct dentry *dentry, struct shfl_fsobjinfo *info);
int vboxsf_inode_revalidate(struct dentry *dentry);
int vboxsf_getattr(struct mnt_idmap *idmap, const struct path *path,
		   struct kstat *kstat, u32 request_mask,
		   unsigned int query_flags);
int vboxsf_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		   struct iattr *iattr);
struct shfl_string *vboxsf_path_from_dentry(struct vboxsf_sbi *sbi,
					    struct dentry *dentry);
int vboxsf_nlscpy(struct vboxsf_sbi *sbi, char *name, size_t name_bound_len,
		  const unsigned char *utf8_name, size_t utf8_len);
struct vboxsf_dir_info *vboxsf_dir_info_alloc(void);
void vboxsf_dir_info_free(struct vboxsf_dir_info *p);
int vboxsf_dir_read_all(struct vboxsf_sbi *sbi, struct vboxsf_dir_info *sf_d,
			u64 handle);

/* from vboxsf_wrappers.c */
int vboxsf_connect(void);
void vboxsf_disconnect(void);

int vboxsf_create(u32 root, struct shfl_string *parsed_path,
		  struct shfl_createparms *create_parms);

int vboxsf_close(u32 root, u64 handle);
int vboxsf_remove(u32 root, struct shfl_string *parsed_path, u32 flags);
int vboxsf_rename(u32 root, struct shfl_string *src_path,
		  struct shfl_string *dest_path, u32 flags);

int vboxsf_read(u32 root, u64 handle, u64 offset, u32 *buf_len, u8 *buf);
int vboxsf_write(u32 root, u64 handle, u64 offset, u32 *buf_len, u8 *buf);

int vboxsf_dirinfo(u32 root, u64 handle,
		   struct shfl_string *parsed_path, u32 flags, u32 index,
		   u32 *buf_len, struct shfl_dirinfo *buf, u32 *file_count);
int vboxsf_fsinfo(u32 root, u64 handle, u32 flags,
		  u32 *buf_len, void *buf);

int vboxsf_map_folder(struct shfl_string *folder_name, u32 *root);
int vboxsf_unmap_folder(u32 root);

int vboxsf_readlink(u32 root, struct shfl_string *parsed_path,
		    u32 buf_len, u8 *buf);
int vboxsf_symlink(u32 root, struct shfl_string *new_path,
		   struct shfl_string *old_path, struct shfl_fsobjinfo *buf);

int vboxsf_set_utf8(void);
int vboxsf_set_symlinks(void);

#endif
