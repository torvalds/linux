/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmglue.h
 *
 * description here
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */


#ifndef DLMGLUE_H
#define DLMGLUE_H

#include "dcache.h"

#define OCFS2_LVB_VERSION 5

struct ocfs2_meta_lvb {
	__u8         lvb_version;
	__u8         lvb_reserved0;
	__be16       lvb_idynfeatures;
	__be32       lvb_iclusters;
	__be32       lvb_iuid;
	__be32       lvb_igid;
	__be64       lvb_iatime_packed;
	__be64       lvb_ictime_packed;
	__be64       lvb_imtime_packed;
	__be64       lvb_isize;
	__be16       lvb_imode;
	__be16       lvb_inlink;
	__be32       lvb_iattr;
	__be32       lvb_igeneration;
	__be32       lvb_reserved2;
};

#define OCFS2_QINFO_LVB_VERSION 1

struct ocfs2_qinfo_lvb {
	__u8	lvb_version;
	__u8	lvb_reserved[3];
	__be32	lvb_bgrace;
	__be32	lvb_igrace;
	__be32	lvb_syncms;
	__be32	lvb_blocks;
	__be32	lvb_free_blk;
	__be32	lvb_free_entry;
};

#define OCFS2_ORPHAN_LVB_VERSION 1

struct ocfs2_orphan_scan_lvb {
	__u8	lvb_version;
	__u8	lvb_reserved[3];
	__be32	lvb_os_seqno;
};

#define OCFS2_TRIMFS_LVB_VERSION 1

struct ocfs2_trim_fs_lvb {
	__u8	lvb_version;
	__u8	lvb_success;
	__u8	lvb_reserved[2];
	__be32	lvb_nodenum;
	__be64	lvb_start;
	__be64	lvb_len;
	__be64	lvb_minlen;
	__be64	lvb_trimlen;
};

struct ocfs2_trim_fs_info {
	u8	tf_valid;	/* lvb is valid, or not */
	u8	tf_success;	/* trim is successful, or not */
	u32	tf_nodenum;	/* osb node number */
	u64	tf_start;	/* trim start offset in clusters */
	u64	tf_len;		/* trim end offset in clusters */
	u64	tf_minlen;	/* trim minimum contiguous free clusters */
	u64	tf_trimlen;	/* trimmed length in bytes */
};

struct ocfs2_lock_holder {
	struct list_head oh_list;
	struct pid *oh_owner_pid;
	int oh_ex;
};

/* ocfs2_inode_lock_full() 'arg_flags' flags */
/* don't wait on recovery. */
#define OCFS2_META_LOCK_RECOVERY	(0x01)
/* Instruct the dlm not to queue ourselves on the other node. */
#define OCFS2_META_LOCK_NOQUEUE		(0x02)
/* don't block waiting for the downconvert thread, instead return -EAGAIN */
#define OCFS2_LOCK_NONBLOCK		(0x04)
/* just get back disk inode bh if we've got cluster lock. */
#define OCFS2_META_LOCK_GETBH		(0x08)

/* Locking subclasses of inode cluster lock */
enum {
	OI_LS_NORMAL = 0,
	OI_LS_PARENT,
	OI_LS_RENAME1,
	OI_LS_RENAME2,
	OI_LS_REFLINK_TARGET,
};

int ocfs2_dlm_init(struct ocfs2_super *osb);
void ocfs2_dlm_shutdown(struct ocfs2_super *osb, int hangup_pending);
void ocfs2_lock_res_init_once(struct ocfs2_lock_res *res);
void ocfs2_inode_lock_res_init(struct ocfs2_lock_res *res,
			       enum ocfs2_lock_type type,
			       unsigned int generation,
			       struct inode *inode);
void ocfs2_dentry_lock_res_init(struct ocfs2_dentry_lock *dl,
				u64 parent, struct inode *inode);
struct ocfs2_file_private;
void ocfs2_file_lock_res_init(struct ocfs2_lock_res *lockres,
			      struct ocfs2_file_private *fp);
struct ocfs2_mem_dqinfo;
void ocfs2_qinfo_lock_res_init(struct ocfs2_lock_res *lockres,
                               struct ocfs2_mem_dqinfo *info);
void ocfs2_refcount_lock_res_init(struct ocfs2_lock_res *lockres,
				  struct ocfs2_super *osb, u64 ref_blkno,
				  unsigned int generation);
void ocfs2_lock_res_free(struct ocfs2_lock_res *res);
int ocfs2_create_new_inode_locks(struct inode *inode);
int ocfs2_drop_inode_locks(struct inode *inode);
int ocfs2_rw_lock(struct inode *inode, int write);
int ocfs2_try_rw_lock(struct inode *inode, int write);
void ocfs2_rw_unlock(struct inode *inode, int write);
int ocfs2_open_lock(struct inode *inode);
int ocfs2_try_open_lock(struct inode *inode, int write);
void ocfs2_open_unlock(struct inode *inode);
int ocfs2_inode_lock_atime(struct inode *inode,
			  struct vfsmount *vfsmnt,
			  int *level, int wait);
int ocfs2_inode_lock_full_nested(struct inode *inode,
			 struct buffer_head **ret_bh,
			 int ex,
			 int arg_flags,
			 int subclass);
int ocfs2_inode_lock_with_page(struct inode *inode,
			      struct buffer_head **ret_bh,
			      int ex,
			      struct page *page);
/* Variants without special locking class or flags */
#define ocfs2_inode_lock_full(i, r, e, f)\
		ocfs2_inode_lock_full_nested(i, r, e, f, OI_LS_NORMAL)
#define ocfs2_inode_lock_nested(i, b, e, s)\
		ocfs2_inode_lock_full_nested(i, b, e, 0, s)
/* 99% of the time we don't want to supply any additional flags --
 * those are for very specific cases only. */
#define ocfs2_inode_lock(i, b, e) ocfs2_inode_lock_full_nested(i, b, e, 0, OI_LS_NORMAL)
#define ocfs2_try_inode_lock(i, b, e)\
		ocfs2_inode_lock_full_nested(i, b, e, OCFS2_META_LOCK_NOQUEUE,\
		OI_LS_NORMAL)
void ocfs2_inode_unlock(struct inode *inode,
		       int ex);
int ocfs2_super_lock(struct ocfs2_super *osb,
		     int ex);
void ocfs2_super_unlock(struct ocfs2_super *osb,
			int ex);
int ocfs2_orphan_scan_lock(struct ocfs2_super *osb, u32 *seqno);
void ocfs2_orphan_scan_unlock(struct ocfs2_super *osb, u32 seqno);

int ocfs2_rename_lock(struct ocfs2_super *osb);
void ocfs2_rename_unlock(struct ocfs2_super *osb);
int ocfs2_nfs_sync_lock(struct ocfs2_super *osb, int ex);
void ocfs2_nfs_sync_unlock(struct ocfs2_super *osb, int ex);
void ocfs2_trim_fs_lock_res_init(struct ocfs2_super *osb);
void ocfs2_trim_fs_lock_res_uninit(struct ocfs2_super *osb);
int ocfs2_trim_fs_lock(struct ocfs2_super *osb,
		       struct ocfs2_trim_fs_info *info, int trylock);
void ocfs2_trim_fs_unlock(struct ocfs2_super *osb,
			  struct ocfs2_trim_fs_info *info);
int ocfs2_dentry_lock(struct dentry *dentry, int ex);
void ocfs2_dentry_unlock(struct dentry *dentry, int ex);
int ocfs2_file_lock(struct file *file, int ex, int trylock);
void ocfs2_file_unlock(struct file *file);
int ocfs2_qinfo_lock(struct ocfs2_mem_dqinfo *oinfo, int ex);
void ocfs2_qinfo_unlock(struct ocfs2_mem_dqinfo *oinfo, int ex);
struct ocfs2_refcount_tree;
int ocfs2_refcount_lock(struct ocfs2_refcount_tree *ref_tree, int ex);
void ocfs2_refcount_unlock(struct ocfs2_refcount_tree *ref_tree, int ex);


void ocfs2_mark_lockres_freeing(struct ocfs2_super *osb,
				struct ocfs2_lock_res *lockres);
void ocfs2_simple_drop_lockres(struct ocfs2_super *osb,
			       struct ocfs2_lock_res *lockres);

/* for the downconvert thread */
void ocfs2_wake_downconvert_thread(struct ocfs2_super *osb);

struct ocfs2_dlm_debug *ocfs2_new_dlm_debug(void);
void ocfs2_put_dlm_debug(struct ocfs2_dlm_debug *dlm_debug);

/* To set the locking protocol on module initialization */
void ocfs2_set_locking_protocol(void);

/* The _tracker pair is used to avoid cluster recursive locking */
int ocfs2_inode_lock_tracker(struct inode *inode,
			     struct buffer_head **ret_bh,
			     int ex,
			     struct ocfs2_lock_holder *oh);
void ocfs2_inode_unlock_tracker(struct inode *inode,
				int ex,
				struct ocfs2_lock_holder *oh,
				int had_lock);

#endif	/* DLMGLUE_H */
