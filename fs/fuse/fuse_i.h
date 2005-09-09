/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2005  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#include <linux/fuse.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/backing-dev.h>
#include <asm/semaphore.h>

/** FUSE inode */
struct fuse_inode {
	/** Inode data */
	struct inode inode;

	/** Unique ID, which identifies the inode between userspace
	 * and kernel */
	u64 nodeid;

	/** Time in jiffies until the file attributes are valid */
	unsigned long i_time;
};

/**
 * A Fuse connection.
 *
 * This structure is created, when the filesystem is mounted, and is
 * destroyed, when the client device is closed and the filesystem is
 * unmounted.
 */
struct fuse_conn {
	/** The superblock of the mounted filesystem */
	struct super_block *sb;

	/** The user id for this mount */
	uid_t user_id;

	/** Backing dev info */
	struct backing_dev_info bdi;
};

static inline struct fuse_conn **get_fuse_conn_super_p(struct super_block *sb)
{
	return (struct fuse_conn **) &sb->s_fs_info;
}

static inline struct fuse_conn *get_fuse_conn_super(struct super_block *sb)
{
	return *get_fuse_conn_super_p(sb);
}

static inline struct fuse_conn *get_fuse_conn(struct inode *inode)
{
	return get_fuse_conn_super(inode->i_sb);
}

static inline struct fuse_inode *get_fuse_inode(struct inode *inode)
{
	return container_of(inode, struct fuse_inode, inode);
}

static inline u64 get_node_id(struct inode *inode)
{
	return get_fuse_inode(inode)->nodeid;
}

/**
 * This is the single global spinlock which protects FUSE's structures
 *
 * The following data is protected by this lock:
 *
 *  - the s_fs_info field of the super block
 *  - the sb (super_block) field in fuse_conn
 */
extern spinlock_t fuse_lock;

/**
 * Check if the connection can be released, and if yes, then free the
 * connection structure
 */
void fuse_release_conn(struct fuse_conn *fc);

