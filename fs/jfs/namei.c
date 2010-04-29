/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 *   Portions Copyright (C) Christoph Hellwig, 2001-2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/quotaops.h>
#include <linux/exportfs.h>
#include "jfs_incore.h"
#include "jfs_superblock.h"
#include "jfs_inode.h"
#include "jfs_dinode.h"
#include "jfs_dmap.h"
#include "jfs_unicode.h"
#include "jfs_metapage.h"
#include "jfs_xattr.h"
#include "jfs_acl.h"
#include "jfs_debug.h"

/*
 * forward references
 */
const struct dentry_operations jfs_ci_dentry_operations;

static s64 commitZeroLink(tid_t, struct inode *);

/*
 * NAME:	free_ea_wmap(inode)
 *
 * FUNCTION:	free uncommitted extended attributes from working map
 *
 */
static inline void free_ea_wmap(struct inode *inode)
{
	dxd_t *ea = &JFS_IP(inode)->ea;

	if (ea->flag & DXD_EXTENT) {
		/* free EA pages from cache */
		invalidate_dxd_metapages(inode, *ea);
		dbFree(inode, addressDXD(ea), lengthDXD(ea));
	}
	ea->flag = 0;
}

/*
 * NAME:	jfs_create(dip, dentry, mode)
 *
 * FUNCTION:	create a regular file in the parent directory <dip>
 *		with name = <from dentry> and mode = <mode>
 *
 * PARAMETER:	dip	- parent directory vnode
 *		dentry	- dentry of new file
 *		mode	- create mode (rwxrwxrwx).
 *		nd- nd struct
 *
 * RETURN:	Errors from subroutines
 *
 */
static int jfs_create(struct inode *dip, struct dentry *dentry, int mode,
		struct nameidata *nd)
{
	int rc = 0;
	tid_t tid;		/* transaction id */
	struct inode *ip = NULL;	/* child directory inode */
	ino_t ino;
	struct component_name dname;	/* child directory name */
	struct btstack btstack;
	struct inode *iplist[2];
	struct tblock *tblk;

	jfs_info("jfs_create: dip:0x%p name:%s", dip, dentry->d_name.name);

	dquot_initialize(dip);

	/*
	 * search parent directory for entry/freespace
	 * (dtSearch() returns parent directory page pinned)
	 */
	if ((rc = get_UCSname(&dname, dentry)))
		goto out1;

	/*
	 * Either iAlloc() or txBegin() may block.  Deadlock can occur if we
	 * block there while holding dtree page, so we allocate the inode &
	 * begin the transaction before we search the directory.
	 */
	ip = ialloc(dip, mode);
	if (IS_ERR(ip)) {
		rc = PTR_ERR(ip);
		goto out2;
	}

	tid = txBegin(dip->i_sb, 0);

	mutex_lock_nested(&JFS_IP(dip)->commit_mutex, COMMIT_MUTEX_PARENT);
	mutex_lock_nested(&JFS_IP(ip)->commit_mutex, COMMIT_MUTEX_CHILD);

	rc = jfs_init_acl(tid, ip, dip);
	if (rc)
		goto out3;

	rc = jfs_init_security(tid, ip, dip);
	if (rc) {
		txAbort(tid, 0);
		goto out3;
	}

	if ((rc = dtSearch(dip, &dname, &ino, &btstack, JFS_CREATE))) {
		jfs_err("jfs_create: dtSearch returned %d", rc);
		txAbort(tid, 0);
		goto out3;
	}

	tblk = tid_to_tblock(tid);
	tblk->xflag |= COMMIT_CREATE;
	tblk->ino = ip->i_ino;
	tblk->u.ixpxd = JFS_IP(ip)->ixpxd;

	iplist[0] = dip;
	iplist[1] = ip;

	/*
	 * initialize the child XAD tree root in-line in inode
	 */
	xtInitRoot(tid, ip);

	/*
	 * create entry in parent directory for child directory
	 * (dtInsert() releases parent directory page)
	 */
	ino = ip->i_ino;
	if ((rc = dtInsert(tid, dip, &dname, &ino, &btstack))) {
		if (rc == -EIO) {
			jfs_err("jfs_create: dtInsert returned -EIO");
			txAbort(tid, 1);	/* Marks Filesystem dirty */
		} else
			txAbort(tid, 0);	/* Filesystem full */
		goto out3;
	}

	ip->i_op = &jfs_file_inode_operations;
	ip->i_fop = &jfs_file_operations;
	ip->i_mapping->a_ops = &jfs_aops;

	mark_inode_dirty(ip);

	dip->i_ctime = dip->i_mtime = CURRENT_TIME;

	mark_inode_dirty(dip);

	rc = txCommit(tid, 2, &iplist[0], 0);

      out3:
	txEnd(tid);
	mutex_unlock(&JFS_IP(ip)->commit_mutex);
	mutex_unlock(&JFS_IP(dip)->commit_mutex);
	if (rc) {
		free_ea_wmap(ip);
		ip->i_nlink = 0;
		unlock_new_inode(ip);
		iput(ip);
	} else {
		d_instantiate(dentry, ip);
		unlock_new_inode(ip);
	}

      out2:
	free_UCSname(&dname);

      out1:

	jfs_info("jfs_create: rc:%d", rc);
	return rc;
}


/*
 * NAME:	jfs_mkdir(dip, dentry, mode)
 *
 * FUNCTION:	create a child directory in the parent directory <dip>
 *		with name = <from dentry> and mode = <mode>
 *
 * PARAMETER:	dip	- parent directory vnode
 *		dentry	- dentry of child directory
 *		mode	- create mode (rwxrwxrwx).
 *
 * RETURN:	Errors from subroutines
 *
 * note:
 * EACCESS: user needs search+write permission on the parent directory
 */
static int jfs_mkdir(struct inode *dip, struct dentry *dentry, int mode)
{
	int rc = 0;
	tid_t tid;		/* transaction id */
	struct inode *ip = NULL;	/* child directory inode */
	ino_t ino;
	struct component_name dname;	/* child directory name */
	struct btstack btstack;
	struct inode *iplist[2];
	struct tblock *tblk;

	jfs_info("jfs_mkdir: dip:0x%p name:%s", dip, dentry->d_name.name);

	dquot_initialize(dip);

	/* link count overflow on parent directory ? */
	if (dip->i_nlink == JFS_LINK_MAX) {
		rc = -EMLINK;
		goto out1;
	}

	/*
	 * search parent directory for entry/freespace
	 * (dtSearch() returns parent directory page pinned)
	 */
	if ((rc = get_UCSname(&dname, dentry)))
		goto out1;

	/*
	 * Either iAlloc() or txBegin() may block.  Deadlock can occur if we
	 * block there while holding dtree page, so we allocate the inode &
	 * begin the transaction before we search the directory.
	 */
	ip = ialloc(dip, S_IFDIR | mode);
	if (IS_ERR(ip)) {
		rc = PTR_ERR(ip);
		goto out2;
	}

	tid = txBegin(dip->i_sb, 0);

	mutex_lock_nested(&JFS_IP(dip)->commit_mutex, COMMIT_MUTEX_PARENT);
	mutex_lock_nested(&JFS_IP(ip)->commit_mutex, COMMIT_MUTEX_CHILD);

	rc = jfs_init_acl(tid, ip, dip);
	if (rc)
		goto out3;

	rc = jfs_init_security(tid, ip, dip);
	if (rc) {
		txAbort(tid, 0);
		goto out3;
	}

	if ((rc = dtSearch(dip, &dname, &ino, &btstack, JFS_CREATE))) {
		jfs_err("jfs_mkdir: dtSearch returned %d", rc);
		txAbort(tid, 0);
		goto out3;
	}

	tblk = tid_to_tblock(tid);
	tblk->xflag |= COMMIT_CREATE;
	tblk->ino = ip->i_ino;
	tblk->u.ixpxd = JFS_IP(ip)->ixpxd;

	iplist[0] = dip;
	iplist[1] = ip;

	/*
	 * initialize the child directory in-line in inode
	 */
	dtInitRoot(tid, ip, dip->i_ino);

	/*
	 * create entry in parent directory for child directory
	 * (dtInsert() releases parent directory page)
	 */
	ino = ip->i_ino;
	if ((rc = dtInsert(tid, dip, &dname, &ino, &btstack))) {
		if (rc == -EIO) {
			jfs_err("jfs_mkdir: dtInsert returned -EIO");
			txAbort(tid, 1);	/* Marks Filesystem dirty */
		} else
			txAbort(tid, 0);	/* Filesystem full */
		goto out3;
	}

	ip->i_nlink = 2;	/* for '.' */
	ip->i_op = &jfs_dir_inode_operations;
	ip->i_fop = &jfs_dir_operations;

	mark_inode_dirty(ip);

	/* update parent directory inode */
	inc_nlink(dip);		/* for '..' from child directory */
	dip->i_ctime = dip->i_mtime = CURRENT_TIME;
	mark_inode_dirty(dip);

	rc = txCommit(tid, 2, &iplist[0], 0);

      out3:
	txEnd(tid);
	mutex_unlock(&JFS_IP(ip)->commit_mutex);
	mutex_unlock(&JFS_IP(dip)->commit_mutex);
	if (rc) {
		free_ea_wmap(ip);
		ip->i_nlink = 0;
		unlock_new_inode(ip);
		iput(ip);
	} else {
		d_instantiate(dentry, ip);
		unlock_new_inode(ip);
	}

      out2:
	free_UCSname(&dname);


      out1:

	jfs_info("jfs_mkdir: rc:%d", rc);
	return rc;
}

/*
 * NAME:	jfs_rmdir(dip, dentry)
 *
 * FUNCTION:	remove a link to child directory
 *
 * PARAMETER:	dip	- parent inode
 *		dentry	- child directory dentry
 *
 * RETURN:	-EINVAL	- if name is . or ..
 *		-EINVAL - if . or .. exist but are invalid.
 *		errors from subroutines
 *
 * note:
 * if other threads have the directory open when the last link
 * is removed, the "." and ".." entries, if present, are removed before
 * rmdir() returns and no new entries may be created in the directory,
 * but the directory is not removed until the last reference to
 * the directory is released (cf.unlink() of regular file).
 */
static int jfs_rmdir(struct inode *dip, struct dentry *dentry)
{
	int rc;
	tid_t tid;		/* transaction id */
	struct inode *ip = dentry->d_inode;
	ino_t ino;
	struct component_name dname;
	struct inode *iplist[2];
	struct tblock *tblk;

	jfs_info("jfs_rmdir: dip:0x%p name:%s", dip, dentry->d_name.name);

	/* Init inode for quota operations. */
	dquot_initialize(dip);
	dquot_initialize(ip);

	/* directory must be empty to be removed */
	if (!dtEmpty(ip)) {
		rc = -ENOTEMPTY;
		goto out;
	}

	if ((rc = get_UCSname(&dname, dentry))) {
		goto out;
	}

	tid = txBegin(dip->i_sb, 0);

	mutex_lock_nested(&JFS_IP(dip)->commit_mutex, COMMIT_MUTEX_PARENT);
	mutex_lock_nested(&JFS_IP(ip)->commit_mutex, COMMIT_MUTEX_CHILD);

	iplist[0] = dip;
	iplist[1] = ip;

	tblk = tid_to_tblock(tid);
	tblk->xflag |= COMMIT_DELETE;
	tblk->u.ip = ip;

	/*
	 * delete the entry of target directory from parent directory
	 */
	ino = ip->i_ino;
	if ((rc = dtDelete(tid, dip, &dname, &ino, JFS_REMOVE))) {
		jfs_err("jfs_rmdir: dtDelete returned %d", rc);
		if (rc == -EIO)
			txAbort(tid, 1);
		txEnd(tid);
		mutex_unlock(&JFS_IP(ip)->commit_mutex);
		mutex_unlock(&JFS_IP(dip)->commit_mutex);

		goto out2;
	}

	/* update parent directory's link count corresponding
	 * to ".." entry of the target directory deleted
	 */
	dip->i_ctime = dip->i_mtime = CURRENT_TIME;
	inode_dec_link_count(dip);

	/*
	 * OS/2 could have created EA and/or ACL
	 */
	/* free EA from both persistent and working map */
	if (JFS_IP(ip)->ea.flag & DXD_EXTENT) {
		/* free EA pages */
		txEA(tid, ip, &JFS_IP(ip)->ea, NULL);
	}
	JFS_IP(ip)->ea.flag = 0;

	/* free ACL from both persistent and working map */
	if (JFS_IP(ip)->acl.flag & DXD_EXTENT) {
		/* free ACL pages */
		txEA(tid, ip, &JFS_IP(ip)->acl, NULL);
	}
	JFS_IP(ip)->acl.flag = 0;

	/* mark the target directory as deleted */
	clear_nlink(ip);
	mark_inode_dirty(ip);

	rc = txCommit(tid, 2, &iplist[0], 0);

	txEnd(tid);

	mutex_unlock(&JFS_IP(ip)->commit_mutex);
	mutex_unlock(&JFS_IP(dip)->commit_mutex);

	/*
	 * Truncating the directory index table is not guaranteed.  It
	 * may need to be done iteratively
	 */
	if (test_cflag(COMMIT_Stale, dip)) {
		if (dip->i_size > 1)
			jfs_truncate_nolock(dip, 0);

		clear_cflag(COMMIT_Stale, dip);
	}

      out2:
	free_UCSname(&dname);

      out:
	jfs_info("jfs_rmdir: rc:%d", rc);
	return rc;
}

/*
 * NAME:	jfs_unlink(dip, dentry)
 *
 * FUNCTION:	remove a link to object <vp> named by <name>
 *		from parent directory <dvp>
 *
 * PARAMETER:	dip	- inode of parent directory
 *		dentry	- dentry of object to be removed
 *
 * RETURN:	errors from subroutines
 *
 * note:
 * temporary file: if one or more processes have the file open
 * when the last link is removed, the link will be removed before
 * unlink() returns, but the removal of the file contents will be
 * postponed until all references to the files are closed.
 *
 * JFS does NOT support unlink() on directories.
 *
 */
static int jfs_unlink(struct inode *dip, struct dentry *dentry)
{
	int rc;
	tid_t tid;		/* transaction id */
	struct inode *ip = dentry->d_inode;
	ino_t ino;
	struct component_name dname;	/* object name */
	struct inode *iplist[2];
	struct tblock *tblk;
	s64 new_size = 0;
	int commit_flag;

	jfs_info("jfs_unlink: dip:0x%p name:%s", dip, dentry->d_name.name);

	/* Init inode for quota operations. */
	dquot_initialize(dip);
	dquot_initialize(ip);

	if ((rc = get_UCSname(&dname, dentry)))
		goto out;

	IWRITE_LOCK(ip, RDWRLOCK_NORMAL);

	tid = txBegin(dip->i_sb, 0);

	mutex_lock_nested(&JFS_IP(dip)->commit_mutex, COMMIT_MUTEX_PARENT);
	mutex_lock_nested(&JFS_IP(ip)->commit_mutex, COMMIT_MUTEX_CHILD);

	iplist[0] = dip;
	iplist[1] = ip;

	/*
	 * delete the entry of target file from parent directory
	 */
	ino = ip->i_ino;
	if ((rc = dtDelete(tid, dip, &dname, &ino, JFS_REMOVE))) {
		jfs_err("jfs_unlink: dtDelete returned %d", rc);
		if (rc == -EIO)
			txAbort(tid, 1);	/* Marks FS Dirty */
		txEnd(tid);
		mutex_unlock(&JFS_IP(ip)->commit_mutex);
		mutex_unlock(&JFS_IP(dip)->commit_mutex);
		IWRITE_UNLOCK(ip);
		goto out1;
	}

	ASSERT(ip->i_nlink);

	ip->i_ctime = dip->i_ctime = dip->i_mtime = CURRENT_TIME;
	mark_inode_dirty(dip);

	/* update target's inode */
	inode_dec_link_count(ip);

	/*
	 *	commit zero link count object
	 */
	if (ip->i_nlink == 0) {
		assert(!test_cflag(COMMIT_Nolink, ip));
		/* free block resources */
		if ((new_size = commitZeroLink(tid, ip)) < 0) {
			txAbort(tid, 1);	/* Marks FS Dirty */
			txEnd(tid);
			mutex_unlock(&JFS_IP(ip)->commit_mutex);
			mutex_unlock(&JFS_IP(dip)->commit_mutex);
			IWRITE_UNLOCK(ip);
			rc = new_size;
			goto out1;
		}
		tblk = tid_to_tblock(tid);
		tblk->xflag |= COMMIT_DELETE;
		tblk->u.ip = ip;
	}

	/*
	 * Incomplete truncate of file data can
	 * result in timing problems unless we synchronously commit the
	 * transaction.
	 */
	if (new_size)
		commit_flag = COMMIT_SYNC;
	else
		commit_flag = 0;

	/*
	 * If xtTruncate was incomplete, commit synchronously to avoid
	 * timing complications
	 */
	rc = txCommit(tid, 2, &iplist[0], commit_flag);

	txEnd(tid);

	mutex_unlock(&JFS_IP(ip)->commit_mutex);
	mutex_unlock(&JFS_IP(dip)->commit_mutex);

	while (new_size && (rc == 0)) {
		tid = txBegin(dip->i_sb, 0);
		mutex_lock(&JFS_IP(ip)->commit_mutex);
		new_size = xtTruncate_pmap(tid, ip, new_size);
		if (new_size < 0) {
			txAbort(tid, 1);	/* Marks FS Dirty */
			rc = new_size;
		} else
			rc = txCommit(tid, 2, &iplist[0], COMMIT_SYNC);
		txEnd(tid);
		mutex_unlock(&JFS_IP(ip)->commit_mutex);
	}

	if (ip->i_nlink == 0)
		set_cflag(COMMIT_Nolink, ip);

	IWRITE_UNLOCK(ip);

	/*
	 * Truncating the directory index table is not guaranteed.  It
	 * may need to be done iteratively
	 */
	if (test_cflag(COMMIT_Stale, dip)) {
		if (dip->i_size > 1)
			jfs_truncate_nolock(dip, 0);

		clear_cflag(COMMIT_Stale, dip);
	}

      out1:
	free_UCSname(&dname);
      out:
	jfs_info("jfs_unlink: rc:%d", rc);
	return rc;
}

/*
 * NAME:	commitZeroLink()
 *
 * FUNCTION:	for non-directory, called by jfs_remove(),
 *		truncate a regular file, directory or symbolic
 *		link to zero length. return 0 if type is not
 *		one of these.
 *
 *		if the file is currently associated with a VM segment
 *		only permanent disk and inode map resources are freed,
 *		and neither the inode nor indirect blocks are modified
 *		so that the resources can be later freed in the work
 *		map by ctrunc1.
 *		if there is no VM segment on entry, the resources are
 *		freed in both work and permanent map.
 *		(? for temporary file - memory object is cached even
 *		after no reference:
 *		reference count > 0 -   )
 *
 * PARAMETERS:	cd	- pointer to commit data structure.
 *			  current inode is the one to truncate.
 *
 * RETURN:	Errors from subroutines
 */
static s64 commitZeroLink(tid_t tid, struct inode *ip)
{
	int filetype;
	struct tblock *tblk;

	jfs_info("commitZeroLink: tid = %d, ip = 0x%p", tid, ip);

	filetype = ip->i_mode & S_IFMT;
	switch (filetype) {
	case S_IFREG:
		break;
	case S_IFLNK:
		/* fast symbolic link */
		if (ip->i_size < IDATASIZE) {
			ip->i_size = 0;
			return 0;
		}
		break;
	default:
		assert(filetype != S_IFDIR);
		return 0;
	}

	set_cflag(COMMIT_Freewmap, ip);

	/* mark transaction of block map update type */
	tblk = tid_to_tblock(tid);
	tblk->xflag |= COMMIT_PMAP;

	/*
	 * free EA
	 */
	if (JFS_IP(ip)->ea.flag & DXD_EXTENT)
		/* acquire maplock on EA to be freed from block map */
		txEA(tid, ip, &JFS_IP(ip)->ea, NULL);

	/*
	 * free ACL
	 */
	if (JFS_IP(ip)->acl.flag & DXD_EXTENT)
		/* acquire maplock on EA to be freed from block map */
		txEA(tid, ip, &JFS_IP(ip)->acl, NULL);

	/*
	 * free xtree/data (truncate to zero length):
	 * free xtree/data pages from cache if COMMIT_PWMAP,
	 * free xtree/data blocks from persistent block map, and
	 * free xtree/data blocks from working block map if COMMIT_PWMAP;
	 */
	if (ip->i_size)
		return xtTruncate_pmap(tid, ip, 0);

	return 0;
}


/*
 * NAME:	jfs_free_zero_link()
 *
 * FUNCTION:	for non-directory, called by iClose(),
 *		free resources of a file from cache and WORKING map
 *		for a file previously committed with zero link count
 *		while associated with a pager object,
 *
 * PARAMETER:	ip	- pointer to inode of file.
 */
void jfs_free_zero_link(struct inode *ip)
{
	int type;

	jfs_info("jfs_free_zero_link: ip = 0x%p", ip);

	/* return if not reg or symbolic link or if size is
	 * already ok.
	 */
	type = ip->i_mode & S_IFMT;

	switch (type) {
	case S_IFREG:
		break;
	case S_IFLNK:
		/* if its contained in inode nothing to do */
		if (ip->i_size < IDATASIZE)
			return;
		break;
	default:
		return;
	}

	/*
	 * free EA
	 */
	if (JFS_IP(ip)->ea.flag & DXD_EXTENT) {
		s64 xaddr = addressDXD(&JFS_IP(ip)->ea);
		int xlen = lengthDXD(&JFS_IP(ip)->ea);
		struct maplock maplock;	/* maplock for COMMIT_WMAP */
		struct pxd_lock *pxdlock;	/* maplock for COMMIT_WMAP */

		/* free EA pages from cache */
		invalidate_dxd_metapages(ip, JFS_IP(ip)->ea);

		/* free EA extent from working block map */
		maplock.index = 1;
		pxdlock = (struct pxd_lock *) & maplock;
		pxdlock->flag = mlckFREEPXD;
		PXDaddress(&pxdlock->pxd, xaddr);
		PXDlength(&pxdlock->pxd, xlen);
		txFreeMap(ip, pxdlock, NULL, COMMIT_WMAP);
	}

	/*
	 * free ACL
	 */
	if (JFS_IP(ip)->acl.flag & DXD_EXTENT) {
		s64 xaddr = addressDXD(&JFS_IP(ip)->acl);
		int xlen = lengthDXD(&JFS_IP(ip)->acl);
		struct maplock maplock;	/* maplock for COMMIT_WMAP */
		struct pxd_lock *pxdlock;	/* maplock for COMMIT_WMAP */

		invalidate_dxd_metapages(ip, JFS_IP(ip)->acl);

		/* free ACL extent from working block map */
		maplock.index = 1;
		pxdlock = (struct pxd_lock *) & maplock;
		pxdlock->flag = mlckFREEPXD;
		PXDaddress(&pxdlock->pxd, xaddr);
		PXDlength(&pxdlock->pxd, xlen);
		txFreeMap(ip, pxdlock, NULL, COMMIT_WMAP);
	}

	/*
	 * free xtree/data (truncate to zero length):
	 * free xtree/data pages from cache, and
	 * free xtree/data blocks from working block map;
	 */
	if (ip->i_size)
		xtTruncate(0, ip, 0, COMMIT_WMAP);
}

/*
 * NAME:	jfs_link(vp, dvp, name, crp)
 *
 * FUNCTION:	create a link to <vp> by the name = <name>
 *		in the parent directory <dvp>
 *
 * PARAMETER:	vp	- target object
 *		dvp	- parent directory of new link
 *		name	- name of new link to target object
 *		crp	- credential
 *
 * RETURN:	Errors from subroutines
 *
 * note:
 * JFS does NOT support link() on directories (to prevent circular
 * path in the directory hierarchy);
 * EPERM: the target object is a directory, and either the caller
 * does not have appropriate privileges or the implementation prohibits
 * using link() on directories [XPG4.2].
 *
 * JFS does NOT support links between file systems:
 * EXDEV: target object and new link are on different file systems and
 * implementation does not support links between file systems [XPG4.2].
 */
static int jfs_link(struct dentry *old_dentry,
	     struct inode *dir, struct dentry *dentry)
{
	int rc;
	tid_t tid;
	struct inode *ip = old_dentry->d_inode;
	ino_t ino;
	struct component_name dname;
	struct btstack btstack;
	struct inode *iplist[2];

	jfs_info("jfs_link: %s %s", old_dentry->d_name.name,
		 dentry->d_name.name);

	if (ip->i_nlink == JFS_LINK_MAX)
		return -EMLINK;

	if (ip->i_nlink == 0)
		return -ENOENT;

	dquot_initialize(dir);

	tid = txBegin(ip->i_sb, 0);

	mutex_lock_nested(&JFS_IP(dir)->commit_mutex, COMMIT_MUTEX_PARENT);
	mutex_lock_nested(&JFS_IP(ip)->commit_mutex, COMMIT_MUTEX_CHILD);

	/*
	 * scan parent directory for entry/freespace
	 */
	if ((rc = get_UCSname(&dname, dentry)))
		goto out;

	if ((rc = dtSearch(dir, &dname, &ino, &btstack, JFS_CREATE)))
		goto free_dname;

	/*
	 * create entry for new link in parent directory
	 */
	ino = ip->i_ino;
	if ((rc = dtInsert(tid, dir, &dname, &ino, &btstack)))
		goto free_dname;

	/* update object inode */
	inc_nlink(ip);		/* for new link */
	ip->i_ctime = CURRENT_TIME;
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(dir);
	atomic_inc(&ip->i_count);

	iplist[0] = ip;
	iplist[1] = dir;
	rc = txCommit(tid, 2, &iplist[0], 0);

	if (rc) {
		ip->i_nlink--; /* never instantiated */
		iput(ip);
	} else
		d_instantiate(dentry, ip);

      free_dname:
	free_UCSname(&dname);

      out:
	txEnd(tid);

	mutex_unlock(&JFS_IP(ip)->commit_mutex);
	mutex_unlock(&JFS_IP(dir)->commit_mutex);

	jfs_info("jfs_link: rc:%d", rc);
	return rc;
}

/*
 * NAME:	jfs_symlink(dip, dentry, name)
 *
 * FUNCTION:	creates a symbolic link to <symlink> by name <name>
 *			in directory <dip>
 *
 * PARAMETER:	dip	- parent directory vnode
 *		dentry	- dentry of symbolic link
 *		name	- the path name of the existing object
 *			  that will be the source of the link
 *
 * RETURN:	errors from subroutines
 *
 * note:
 * ENAMETOOLONG: pathname resolution of a symbolic link produced
 * an intermediate result whose length exceeds PATH_MAX [XPG4.2]
*/

static int jfs_symlink(struct inode *dip, struct dentry *dentry,
		const char *name)
{
	int rc;
	tid_t tid;
	ino_t ino = 0;
	struct component_name dname;
	int ssize;		/* source pathname size */
	struct btstack btstack;
	struct inode *ip = dentry->d_inode;
	unchar *i_fastsymlink;
	s64 xlen = 0;
	int bmask = 0, xsize;
	s64 extent = 0, xaddr;
	struct metapage *mp;
	struct super_block *sb;
	struct tblock *tblk;

	struct inode *iplist[2];

	jfs_info("jfs_symlink: dip:0x%p name:%s", dip, name);

	dquot_initialize(dip);

	ssize = strlen(name) + 1;

	/*
	 * search parent directory for entry/freespace
	 * (dtSearch() returns parent directory page pinned)
	 */

	if ((rc = get_UCSname(&dname, dentry)))
		goto out1;

	/*
	 * allocate on-disk/in-memory inode for symbolic link:
	 * (iAlloc() returns new, locked inode)
	 */
	ip = ialloc(dip, S_IFLNK | 0777);
	if (IS_ERR(ip)) {
		rc = PTR_ERR(ip);
		goto out2;
	}

	tid = txBegin(dip->i_sb, 0);

	mutex_lock_nested(&JFS_IP(dip)->commit_mutex, COMMIT_MUTEX_PARENT);
	mutex_lock_nested(&JFS_IP(ip)->commit_mutex, COMMIT_MUTEX_CHILD);

	rc = jfs_init_security(tid, ip, dip);
	if (rc)
		goto out3;

	tblk = tid_to_tblock(tid);
	tblk->xflag |= COMMIT_CREATE;
	tblk->ino = ip->i_ino;
	tblk->u.ixpxd = JFS_IP(ip)->ixpxd;

	/* fix symlink access permission
	 * (dir_create() ANDs in the u.u_cmask,
	 * but symlinks really need to be 777 access)
	 */
	ip->i_mode |= 0777;

	/*
	 * write symbolic link target path name
	 */
	xtInitRoot(tid, ip);

	/*
	 * write source path name inline in on-disk inode (fast symbolic link)
	 */

	if (ssize <= IDATASIZE) {
		ip->i_op = &jfs_fast_symlink_inode_operations;

		i_fastsymlink = JFS_IP(ip)->i_inline;
		memcpy(i_fastsymlink, name, ssize);
		ip->i_size = ssize - 1;

		/*
		 * if symlink is > 128 bytes, we don't have the space to
		 * store inline extended attributes
		 */
		if (ssize > sizeof (JFS_IP(ip)->i_inline))
			JFS_IP(ip)->mode2 &= ~INLINEEA;

		jfs_info("jfs_symlink: fast symlink added  ssize:%d name:%s ",
			 ssize, name);
	}
	/*
	 * write source path name in a single extent
	 */
	else {
		jfs_info("jfs_symlink: allocate extent ip:0x%p", ip);

		ip->i_op = &jfs_symlink_inode_operations;
		ip->i_mapping->a_ops = &jfs_aops;

		/*
		 * even though the data of symlink object (source
		 * path name) is treated as non-journaled user data,
		 * it is read/written thru buffer cache for performance.
		 */
		sb = ip->i_sb;
		bmask = JFS_SBI(sb)->bsize - 1;
		xsize = (ssize + bmask) & ~bmask;
		xaddr = 0;
		xlen = xsize >> JFS_SBI(sb)->l2bsize;
		if ((rc = xtInsert(tid, ip, 0, 0, xlen, &xaddr, 0))) {
			txAbort(tid, 0);
			goto out3;
		}
		extent = xaddr;
		ip->i_size = ssize - 1;
		while (ssize) {
			/* This is kind of silly since PATH_MAX == 4K */
			int copy_size = min(ssize, PSIZE);

			mp = get_metapage(ip, xaddr, PSIZE, 1);

			if (mp == NULL) {
				xtTruncate(tid, ip, 0, COMMIT_PWMAP);
				rc = -EIO;
				txAbort(tid, 0);
				goto out3;
			}
			memcpy(mp->data, name, copy_size);
			flush_metapage(mp);
			ssize -= copy_size;
			name += copy_size;
			xaddr += JFS_SBI(sb)->nbperpage;
		}
	}

	/*
	 * create entry for symbolic link in parent directory
	 */
	rc = dtSearch(dip, &dname, &ino, &btstack, JFS_CREATE);
	if (rc == 0) {
		ino = ip->i_ino;
		rc = dtInsert(tid, dip, &dname, &ino, &btstack);
	}
	if (rc) {
		if (xlen)
			xtTruncate(tid, ip, 0, COMMIT_PWMAP);
		txAbort(tid, 0);
		/* discard new inode */
		goto out3;
	}

	mark_inode_dirty(ip);

	dip->i_ctime = dip->i_mtime = CURRENT_TIME;
	mark_inode_dirty(dip);
	/*
	 * commit update of parent directory and link object
	 */

	iplist[0] = dip;
	iplist[1] = ip;
	rc = txCommit(tid, 2, &iplist[0], 0);

      out3:
	txEnd(tid);
	mutex_unlock(&JFS_IP(ip)->commit_mutex);
	mutex_unlock(&JFS_IP(dip)->commit_mutex);
	if (rc) {
		free_ea_wmap(ip);
		ip->i_nlink = 0;
		unlock_new_inode(ip);
		iput(ip);
	} else {
		d_instantiate(dentry, ip);
		unlock_new_inode(ip);
	}

      out2:
	free_UCSname(&dname);

      out1:
	jfs_info("jfs_symlink: rc:%d", rc);
	return rc;
}


/*
 * NAME:	jfs_rename
 *
 * FUNCTION:	rename a file or directory
 */
static int jfs_rename(struct inode *old_dir, struct dentry *old_dentry,
	       struct inode *new_dir, struct dentry *new_dentry)
{
	struct btstack btstack;
	ino_t ino;
	struct component_name new_dname;
	struct inode *new_ip;
	struct component_name old_dname;
	struct inode *old_ip;
	int rc;
	tid_t tid;
	struct tlock *tlck;
	struct dt_lock *dtlck;
	struct lv *lv;
	int ipcount;
	struct inode *iplist[4];
	struct tblock *tblk;
	s64 new_size = 0;
	int commit_flag;


	jfs_info("jfs_rename: %s %s", old_dentry->d_name.name,
		 new_dentry->d_name.name);

	dquot_initialize(old_dir);
	dquot_initialize(new_dir);

	old_ip = old_dentry->d_inode;
	new_ip = new_dentry->d_inode;

	if ((rc = get_UCSname(&old_dname, old_dentry)))
		goto out1;

	if ((rc = get_UCSname(&new_dname, new_dentry)))
		goto out2;

	/*
	 * Make sure source inode number is what we think it is
	 */
	rc = dtSearch(old_dir, &old_dname, &ino, &btstack, JFS_LOOKUP);
	if (rc || (ino != old_ip->i_ino)) {
		rc = -ENOENT;
		goto out3;
	}

	/*
	 * Make sure dest inode number (if any) is what we think it is
	 */
	rc = dtSearch(new_dir, &new_dname, &ino, &btstack, JFS_LOOKUP);
	if (!rc) {
		if ((!new_ip) || (ino != new_ip->i_ino)) {
			rc = -ESTALE;
			goto out3;
		}
	} else if (rc != -ENOENT)
		goto out3;
	else if (new_ip) {
		/* no entry exists, but one was expected */
		rc = -ESTALE;
		goto out3;
	}

	if (S_ISDIR(old_ip->i_mode)) {
		if (new_ip) {
			if (!dtEmpty(new_ip)) {
				rc = -ENOTEMPTY;
				goto out3;
			}
		} else if ((new_dir != old_dir) &&
			   (new_dir->i_nlink == JFS_LINK_MAX)) {
			rc = -EMLINK;
			goto out3;
		}
	} else if (new_ip) {
		IWRITE_LOCK(new_ip, RDWRLOCK_NORMAL);
		/* Init inode for quota operations. */
		dquot_initialize(new_ip);
	}

	/*
	 * The real work starts here
	 */
	tid = txBegin(new_dir->i_sb, 0);

	/*
	 * How do we know the locking is safe from deadlocks?
	 * The vfs does the hard part for us.  Any time we are taking nested
	 * commit_mutexes, the vfs already has i_mutex held on the parent.
	 * Here, the vfs has already taken i_mutex on both old_dir and new_dir.
	 */
	mutex_lock_nested(&JFS_IP(new_dir)->commit_mutex, COMMIT_MUTEX_PARENT);
	mutex_lock_nested(&JFS_IP(old_ip)->commit_mutex, COMMIT_MUTEX_CHILD);
	if (old_dir != new_dir)
		mutex_lock_nested(&JFS_IP(old_dir)->commit_mutex,
				  COMMIT_MUTEX_SECOND_PARENT);

	if (new_ip) {
		mutex_lock_nested(&JFS_IP(new_ip)->commit_mutex,
				  COMMIT_MUTEX_VICTIM);
		/*
		 * Change existing directory entry to new inode number
		 */
		ino = new_ip->i_ino;
		rc = dtModify(tid, new_dir, &new_dname, &ino,
			      old_ip->i_ino, JFS_RENAME);
		if (rc)
			goto out4;
		drop_nlink(new_ip);
		if (S_ISDIR(new_ip->i_mode)) {
			drop_nlink(new_ip);
			if (new_ip->i_nlink) {
				mutex_unlock(&JFS_IP(new_ip)->commit_mutex);
				if (old_dir != new_dir)
					mutex_unlock(&JFS_IP(old_dir)->commit_mutex);
				mutex_unlock(&JFS_IP(old_ip)->commit_mutex);
				mutex_unlock(&JFS_IP(new_dir)->commit_mutex);
				if (!S_ISDIR(old_ip->i_mode) && new_ip)
					IWRITE_UNLOCK(new_ip);
				jfs_error(new_ip->i_sb,
					  "jfs_rename: new_ip->i_nlink != 0");
				return -EIO;
			}
			tblk = tid_to_tblock(tid);
			tblk->xflag |= COMMIT_DELETE;
			tblk->u.ip = new_ip;
		} else if (new_ip->i_nlink == 0) {
			assert(!test_cflag(COMMIT_Nolink, new_ip));
			/* free block resources */
			if ((new_size = commitZeroLink(tid, new_ip)) < 0) {
				txAbort(tid, 1);	/* Marks FS Dirty */
				rc = new_size;
				goto out4;
			}
			tblk = tid_to_tblock(tid);
			tblk->xflag |= COMMIT_DELETE;
			tblk->u.ip = new_ip;
		} else {
			new_ip->i_ctime = CURRENT_TIME;
			mark_inode_dirty(new_ip);
		}
	} else {
		/*
		 * Add new directory entry
		 */
		rc = dtSearch(new_dir, &new_dname, &ino, &btstack,
			      JFS_CREATE);
		if (rc) {
			jfs_err("jfs_rename didn't expect dtSearch to fail "
				"w/rc = %d", rc);
			goto out4;
		}

		ino = old_ip->i_ino;
		rc = dtInsert(tid, new_dir, &new_dname, &ino, &btstack);
		if (rc) {
			if (rc == -EIO)
				jfs_err("jfs_rename: dtInsert returned -EIO");
			goto out4;
		}
		if (S_ISDIR(old_ip->i_mode))
			inc_nlink(new_dir);
	}
	/*
	 * Remove old directory entry
	 */

	ino = old_ip->i_ino;
	rc = dtDelete(tid, old_dir, &old_dname, &ino, JFS_REMOVE);
	if (rc) {
		jfs_err("jfs_rename did not expect dtDelete to return rc = %d",
			rc);
		txAbort(tid, 1);	/* Marks Filesystem dirty */
		goto out4;
	}
	if (S_ISDIR(old_ip->i_mode)) {
		drop_nlink(old_dir);
		if (old_dir != new_dir) {
			/*
			 * Change inode number of parent for moved directory
			 */

			JFS_IP(old_ip)->i_dtroot.header.idotdot =
				cpu_to_le32(new_dir->i_ino);

			/* Linelock header of dtree */
			tlck = txLock(tid, old_ip,
				    (struct metapage *) &JFS_IP(old_ip)->bxflag,
				      tlckDTREE | tlckBTROOT | tlckRELINK);
			dtlck = (struct dt_lock *) & tlck->lock;
			ASSERT(dtlck->index == 0);
			lv = & dtlck->lv[0];
			lv->offset = 0;
			lv->length = 1;
			dtlck->index++;
		}
	}

	/*
	 * Update ctime on changed/moved inodes & mark dirty
	 */
	old_ip->i_ctime = CURRENT_TIME;
	mark_inode_dirty(old_ip);

	new_dir->i_ctime = new_dir->i_mtime = current_fs_time(new_dir->i_sb);
	mark_inode_dirty(new_dir);

	/* Build list of inodes modified by this transaction */
	ipcount = 0;
	iplist[ipcount++] = old_ip;
	if (new_ip)
		iplist[ipcount++] = new_ip;
	iplist[ipcount++] = old_dir;

	if (old_dir != new_dir) {
		iplist[ipcount++] = new_dir;
		old_dir->i_ctime = old_dir->i_mtime = CURRENT_TIME;
		mark_inode_dirty(old_dir);
	}

	/*
	 * Incomplete truncate of file data can
	 * result in timing problems unless we synchronously commit the
	 * transaction.
	 */
	if (new_size)
		commit_flag = COMMIT_SYNC;
	else
		commit_flag = 0;

	rc = txCommit(tid, ipcount, iplist, commit_flag);

      out4:
	txEnd(tid);
	if (new_ip)
		mutex_unlock(&JFS_IP(new_ip)->commit_mutex);
	if (old_dir != new_dir)
		mutex_unlock(&JFS_IP(old_dir)->commit_mutex);
	mutex_unlock(&JFS_IP(old_ip)->commit_mutex);
	mutex_unlock(&JFS_IP(new_dir)->commit_mutex);

	while (new_size && (rc == 0)) {
		tid = txBegin(new_ip->i_sb, 0);
		mutex_lock(&JFS_IP(new_ip)->commit_mutex);
		new_size = xtTruncate_pmap(tid, new_ip, new_size);
		if (new_size < 0) {
			txAbort(tid, 1);
			rc = new_size;
		} else
			rc = txCommit(tid, 1, &new_ip, COMMIT_SYNC);
		txEnd(tid);
		mutex_unlock(&JFS_IP(new_ip)->commit_mutex);
	}
	if (new_ip && (new_ip->i_nlink == 0))
		set_cflag(COMMIT_Nolink, new_ip);
      out3:
	free_UCSname(&new_dname);
      out2:
	free_UCSname(&old_dname);
      out1:
	if (new_ip && !S_ISDIR(new_ip->i_mode))
		IWRITE_UNLOCK(new_ip);
	/*
	 * Truncating the directory index table is not guaranteed.  It
	 * may need to be done iteratively
	 */
	if (test_cflag(COMMIT_Stale, old_dir)) {
		if (old_dir->i_size > 1)
			jfs_truncate_nolock(old_dir, 0);

		clear_cflag(COMMIT_Stale, old_dir);
	}

	jfs_info("jfs_rename: returning %d", rc);
	return rc;
}


/*
 * NAME:	jfs_mknod
 *
 * FUNCTION:	Create a special file (device)
 */
static int jfs_mknod(struct inode *dir, struct dentry *dentry,
		int mode, dev_t rdev)
{
	struct jfs_inode_info *jfs_ip;
	struct btstack btstack;
	struct component_name dname;
	ino_t ino;
	struct inode *ip;
	struct inode *iplist[2];
	int rc;
	tid_t tid;
	struct tblock *tblk;

	if (!new_valid_dev(rdev))
		return -EINVAL;

	jfs_info("jfs_mknod: %s", dentry->d_name.name);

	dquot_initialize(dir);

	if ((rc = get_UCSname(&dname, dentry)))
		goto out;

	ip = ialloc(dir, mode);
	if (IS_ERR(ip)) {
		rc = PTR_ERR(ip);
		goto out1;
	}
	jfs_ip = JFS_IP(ip);

	tid = txBegin(dir->i_sb, 0);

	mutex_lock_nested(&JFS_IP(dir)->commit_mutex, COMMIT_MUTEX_PARENT);
	mutex_lock_nested(&JFS_IP(ip)->commit_mutex, COMMIT_MUTEX_CHILD);

	rc = jfs_init_acl(tid, ip, dir);
	if (rc)
		goto out3;

	rc = jfs_init_security(tid, ip, dir);
	if (rc) {
		txAbort(tid, 0);
		goto out3;
	}

	if ((rc = dtSearch(dir, &dname, &ino, &btstack, JFS_CREATE))) {
		txAbort(tid, 0);
		goto out3;
	}

	tblk = tid_to_tblock(tid);
	tblk->xflag |= COMMIT_CREATE;
	tblk->ino = ip->i_ino;
	tblk->u.ixpxd = JFS_IP(ip)->ixpxd;

	ino = ip->i_ino;
	if ((rc = dtInsert(tid, dir, &dname, &ino, &btstack))) {
		txAbort(tid, 0);
		goto out3;
	}

	ip->i_op = &jfs_file_inode_operations;
	jfs_ip->dev = new_encode_dev(rdev);
	init_special_inode(ip, ip->i_mode, rdev);

	mark_inode_dirty(ip);

	dir->i_ctime = dir->i_mtime = CURRENT_TIME;

	mark_inode_dirty(dir);

	iplist[0] = dir;
	iplist[1] = ip;
	rc = txCommit(tid, 2, iplist, 0);

      out3:
	txEnd(tid);
	mutex_unlock(&JFS_IP(ip)->commit_mutex);
	mutex_unlock(&JFS_IP(dir)->commit_mutex);
	if (rc) {
		free_ea_wmap(ip);
		ip->i_nlink = 0;
		unlock_new_inode(ip);
		iput(ip);
	} else {
		d_instantiate(dentry, ip);
		unlock_new_inode(ip);
	}

      out1:
	free_UCSname(&dname);

      out:
	jfs_info("jfs_mknod: returning %d", rc);
	return rc;
}

static struct dentry *jfs_lookup(struct inode *dip, struct dentry *dentry, struct nameidata *nd)
{
	struct btstack btstack;
	ino_t inum;
	struct inode *ip;
	struct component_name key;
	const char *name = dentry->d_name.name;
	int len = dentry->d_name.len;
	int rc;

	jfs_info("jfs_lookup: name = %s", name);

	if (JFS_SBI(dip->i_sb)->mntflag & JFS_OS2)
		dentry->d_op = &jfs_ci_dentry_operations;

	if ((name[0] == '.') && (len == 1))
		inum = dip->i_ino;
	else if (strcmp(name, "..") == 0)
		inum = PARENT(dip);
	else {
		if ((rc = get_UCSname(&key, dentry)))
			return ERR_PTR(rc);
		rc = dtSearch(dip, &key, &inum, &btstack, JFS_LOOKUP);
		free_UCSname(&key);
		if (rc == -ENOENT) {
			d_add(dentry, NULL);
			return NULL;
		} else if (rc) {
			jfs_err("jfs_lookup: dtSearch returned %d", rc);
			return ERR_PTR(rc);
		}
	}

	ip = jfs_iget(dip->i_sb, inum);
	if (IS_ERR(ip)) {
		jfs_err("jfs_lookup: iget failed on inum %d", (uint) inum);
		return ERR_CAST(ip);
	}

	dentry = d_splice_alias(ip, dentry);

	if (dentry && (JFS_SBI(dip->i_sb)->mntflag & JFS_OS2))
		dentry->d_op = &jfs_ci_dentry_operations;

	return dentry;
}

static struct inode *jfs_nfs_get_inode(struct super_block *sb,
		u64 ino, u32 generation)
{
	struct inode *inode;

	if (ino == 0)
		return ERR_PTR(-ESTALE);
	inode = jfs_iget(sb, ino);
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	if (generation && inode->i_generation != generation) {
		iput(inode);
		return ERR_PTR(-ESTALE);
	}

	return inode;
}

struct dentry *jfs_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    jfs_nfs_get_inode);
}

struct dentry *jfs_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    jfs_nfs_get_inode);
}

struct dentry *jfs_get_parent(struct dentry *dentry)
{
	unsigned long parent_ino;

	parent_ino =
		le32_to_cpu(JFS_IP(dentry->d_inode)->i_dtroot.header.idotdot);

	return d_obtain_alias(jfs_iget(dentry->d_inode->i_sb, parent_ino));
}

const struct inode_operations jfs_dir_inode_operations = {
	.create		= jfs_create,
	.lookup		= jfs_lookup,
	.link		= jfs_link,
	.unlink		= jfs_unlink,
	.symlink	= jfs_symlink,
	.mkdir		= jfs_mkdir,
	.rmdir		= jfs_rmdir,
	.mknod		= jfs_mknod,
	.rename		= jfs_rename,
	.setxattr	= jfs_setxattr,
	.getxattr	= jfs_getxattr,
	.listxattr	= jfs_listxattr,
	.removexattr	= jfs_removexattr,
	.setattr	= jfs_setattr,
#ifdef CONFIG_JFS_POSIX_ACL
	.check_acl	= jfs_check_acl,
#endif
};

const struct file_operations jfs_dir_operations = {
	.read		= generic_read_dir,
	.readdir	= jfs_readdir,
	.fsync		= jfs_fsync,
	.unlocked_ioctl = jfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= jfs_compat_ioctl,
#endif
	.llseek		= generic_file_llseek,
};

static int jfs_ci_hash(struct dentry *dir, struct qstr *this)
{
	unsigned long hash;
	int i;

	hash = init_name_hash();
	for (i=0; i < this->len; i++)
		hash = partial_name_hash(tolower(this->name[i]), hash);
	this->hash = end_name_hash(hash);

	return 0;
}

static int jfs_ci_compare(struct dentry *dir, struct qstr *a, struct qstr *b)
{
	int i, result = 1;

	if (a->len != b->len)
		goto out;
	for (i=0; i < a->len; i++) {
		if (tolower(a->name[i]) != tolower(b->name[i]))
			goto out;
	}
	result = 0;

	/*
	 * We want creates to preserve case.  A negative dentry, a, that
	 * has a different case than b may cause a new entry to be created
	 * with the wrong case.  Since we can't tell if a comes from a negative
	 * dentry, we blindly replace it with b.  This should be harmless if
	 * a is not a negative dentry.
	 */
	memcpy((unsigned char *)a->name, b->name, a->len);
out:
	return result;
}

const struct dentry_operations jfs_ci_dentry_operations =
{
	.d_hash = jfs_ci_hash,
	.d_compare = jfs_ci_compare,
};
