/* $Id: jffs2_fs_i.h,v 1.18 2005/07/17 11:13:48 dedekind Exp $ */

#ifndef _JFFS2_FS_I
#define _JFFS2_FS_I

#include <linux/version.h>
#include <linux/rbtree.h>
#include <asm/semaphore.h>

struct jffs2_inode_info {
	/* We need an internal semaphore similar to inode->i_sem.
	   Unfortunately, we can't used the existing one, because
	   either the GC would deadlock, or we'd have to release it
	   before letting GC proceed. Or we'd have to put ugliness
	   into the GC code so it didn't attempt to obtain the i_sem
	   for the inode(s) which are already locked */
	struct semaphore sem;

	/* The highest (datanode) version number used for this ino */
	uint32_t highest_version;

	/* List of data fragments which make up the file */
	struct rb_root fragtree;

	/* There may be one datanode which isn't referenced by any of the
	   above fragments, if it contains a metadata update but no actual
	   data - or if this is a directory inode */
	/* This also holds the _only_ dnode for symlinks/device nodes, 
	   etc. */
	struct jffs2_full_dnode *metadata;

	/* Directory entries */
	struct jffs2_full_dirent *dents;

	/* The target path if this is the inode of a symlink */
	unsigned char *target;

	/* Some stuff we just have to keep in-core at all times, for each inode. */
	struct jffs2_inode_cache *inocache;

	uint16_t flags;
	uint8_t usercompr;
#if !defined (__ECOS)
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,2)
	struct inode vfs_inode;
#endif
#endif
};

#endif /* _JFFS2_FS_I */
