/*
 * fsync.c
 *
 * PURPOSE
 *  Fsync handling routines for the OSTA-UDF(tm) filesystem.
 *
 * CONTACTS
 *  E-mail regarding any portion of the Linux UDF file system should be
 *  directed to the development team mailing list (run by majordomo):
 *      linux_udf@hpesjro.fc.hp.com
 *
 * COPYRIGHT
 *  This file is distributed under the terms of the GNU General Public
 *  License (GPL). Copies of the GPL can be obtained from:
 *      ftp://prep.ai.mit.edu/pub/gnu/GPL
 *  Each contributing author retains all rights to their own work.
 *
 *  (C) 1999-2001 Ben Fennema
 *  (C) 1999-2000 Stelias Computing Inc
 *
 * HISTORY
 *
 *  05/22/99 blf  Created.
 */

#include "udfdecl.h"

#include <linux/fs.h>
#include <linux/smp_lock.h>

static int udf_fsync_inode(struct inode *, int);

/*
 *	File may be NULL when we are called. Perhaps we shouldn't
 *	even pass file to fsync ?
 */

int udf_fsync_file(struct file * file, struct dentry *dentry, int datasync)
{
	struct inode *inode = dentry->d_inode;
	return udf_fsync_inode(inode, datasync);
}

static int udf_fsync_inode(struct inode *inode, int datasync)
{
	int err;

	err = sync_mapping_buffers(inode->i_mapping);
	if (!(inode->i_state & I_DIRTY))
		return err;
	if (datasync && !(inode->i_state & I_DIRTY_DATASYNC))
		return err;

	err |= udf_sync_inode (inode);
	return err ? -EIO : 0;
}
