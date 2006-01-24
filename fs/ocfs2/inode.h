/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * inode.h
 *
 * Function prototypes
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

#ifndef OCFS2_INODE_H
#define OCFS2_INODE_H

/* OCFS2 Inode Private Data */
struct ocfs2_inode_info
{
	u64			ip_blkno;

	struct ocfs2_lock_res		ip_rw_lockres;
	struct ocfs2_lock_res		ip_meta_lockres;
	struct ocfs2_lock_res		ip_data_lockres;

	/* protects allocation changes on this inode. */
	struct rw_semaphore		ip_alloc_sem;

	/* These fields are protected by ip_lock */
	spinlock_t			ip_lock;
	u32				ip_open_count;
	u32				ip_clusters;
	struct ocfs2_extent_map		ip_map;
	struct list_head		ip_io_markers;
	int				ip_orphaned_slot;

	struct semaphore		ip_io_sem;

	/* Used by the journalling code to attach an inode to a
	 * handle.  These are protected by ip_io_sem in order to lock
	 * out other I/O to the inode until we either commit or
	 * abort. */
	struct list_head		ip_handle_list;
	struct ocfs2_journal_handle	*ip_handle;

	u32				ip_flags; /* see below */

	/* protected by recovery_lock. */
	struct inode			*ip_next_orphan;

	u32				ip_dir_start_lookup;

	/* next two are protected by trans_inc_lock */
	/* which transaction were we created on? Zero if none. */
	unsigned long			ip_created_trans;
	/* last transaction we were a part of. */
	unsigned long			ip_last_trans;

	struct ocfs2_caching_info	ip_metadata_cache;

	struct inode			vfs_inode;
};

/*
 * Flags for the ip_flags field
 */
/* System file inodes  */
#define OCFS2_INODE_SYSTEM_FILE		0x00000001
#define OCFS2_INODE_JOURNAL		0x00000002
#define OCFS2_INODE_BITMAP		0x00000004
/* This inode has been wiped from disk */
#define OCFS2_INODE_DELETED		0x00000008
/* Another node is deleting, so our delete is a nop */
#define OCFS2_INODE_SKIP_DELETE		0x00000010
/* Has the inode been orphaned on another node?
 *
 * This hints to ocfs2_drop_inode that it should clear i_nlink before
 * continuing.
 *
 * We *only* set this on unlink vote from another node. If the inode
 * was locally orphaned, then we're sure of the state and don't need
 * to twiddle i_nlink later - it's either zero or not depending on
 * whether our unlink succeeded. Otherwise we got this from a node
 * whose intention was to orphan the inode, however he may have
 * crashed, failed etc, so we let ocfs2_drop_inode zero the value and
 * rely on ocfs2_delete_inode to sort things out under the proper
 * cluster locks.
 */
#define OCFS2_INODE_MAYBE_ORPHANED	0x00000020
/* Does someone have the file open O_DIRECT */
#define OCFS2_INODE_OPEN_DIRECT		0x00000040
/* Indicates that the metadata cache should be used as an array. */
#define OCFS2_INODE_CACHE_INLINE	0x00000080

static inline struct ocfs2_inode_info *OCFS2_I(struct inode *inode)
{
	return container_of(inode, struct ocfs2_inode_info, vfs_inode);
}

#define INODE_JOURNAL(i) (OCFS2_I(i)->ip_flags & OCFS2_INODE_JOURNAL)
#define SET_INODE_JOURNAL(i) (OCFS2_I(i)->ip_flags |= OCFS2_INODE_JOURNAL)

extern kmem_cache_t *ocfs2_inode_cache;

extern struct address_space_operations ocfs2_aops;

struct buffer_head *ocfs2_bread(struct inode *inode, int block,
				int *err, int reada);
void ocfs2_clear_inode(struct inode *inode);
void ocfs2_delete_inode(struct inode *inode);
void ocfs2_drop_inode(struct inode *inode);
struct inode *ocfs2_iget(struct ocfs2_super *osb, u64 feoff);
struct inode *ocfs2_ilookup_for_vote(struct ocfs2_super *osb,
				     u64 blkno,
				     int delete_vote);
int ocfs2_inode_init_private(struct inode *inode);
int ocfs2_inode_revalidate(struct dentry *dentry);
int ocfs2_populate_inode(struct inode *inode, struct ocfs2_dinode *fe,
			 int create_ino);
void ocfs2_read_inode(struct inode *inode);
void ocfs2_read_inode2(struct inode *inode, void *opaque);
ssize_t ocfs2_rw_direct(int rw, struct file *filp, char *buf,
			size_t size, loff_t *offp);
void ocfs2_sync_blockdev(struct super_block *sb);
void ocfs2_refresh_inode(struct inode *inode,
			 struct ocfs2_dinode *fe);
int ocfs2_mark_inode_dirty(struct ocfs2_journal_handle *handle,
			   struct inode *inode,
			   struct buffer_head *bh);
int ocfs2_aio_read(struct file *file, struct kiocb *req, struct iocb *iocb);
int ocfs2_aio_write(struct file *file, struct kiocb *req, struct iocb *iocb);

#endif /* OCFS2_INODE_H */
