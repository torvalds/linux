// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 *   Portions Copyright (C) Christoph Hellwig, 2001-2002
 */

#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/ctype.h>
#include <linux/quotaops.h>
#include <linux/exportfs.h>
#include "jfs_incore.h"
#include "jfs_superblock.h"
#include "jfs_ianalde.h"
#include "jfs_dianalde.h"
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

static s64 commitZeroLink(tid_t, struct ianalde *);

/*
 * NAME:	free_ea_wmap(ianalde)
 *
 * FUNCTION:	free uncommitted extended attributes from working map
 *
 */
static inline void free_ea_wmap(struct ianalde *ianalde)
{
	dxd_t *ea = &JFS_IP(ianalde)->ea;

	if (ea->flag & DXD_EXTENT) {
		/* free EA pages from cache */
		invalidate_dxd_metapages(ianalde, *ea);
		dbFree(ianalde, addressDXD(ea), lengthDXD(ea));
	}
	ea->flag = 0;
}

/*
 * NAME:	jfs_create(dip, dentry, mode)
 *
 * FUNCTION:	create a regular file in the parent directory <dip>
 *		with name = <from dentry> and mode = <mode>
 *
 * PARAMETER:	dip	- parent directory vanalde
 *		dentry	- dentry of new file
 *		mode	- create mode (rwxrwxrwx).
 *		nd- nd struct
 *
 * RETURN:	Errors from subroutines
 *
 */
static int jfs_create(struct mnt_idmap *idmap, struct ianalde *dip,
		      struct dentry *dentry, umode_t mode, bool excl)
{
	int rc = 0;
	tid_t tid;		/* transaction id */
	struct ianalde *ip = NULL;	/* child directory ianalde */
	ianal_t ianal;
	struct component_name dname;	/* child directory name */
	struct btstack btstack;
	struct ianalde *iplist[2];
	struct tblock *tblk;

	jfs_info("jfs_create: dip:0x%p name:%pd", dip, dentry);

	rc = dquot_initialize(dip);
	if (rc)
		goto out1;

	/*
	 * search parent directory for entry/freespace
	 * (dtSearch() returns parent directory page pinned)
	 */
	if ((rc = get_UCSname(&dname, dentry)))
		goto out1;

	/*
	 * Either iAlloc() or txBegin() may block.  Deadlock can occur if we
	 * block there while holding dtree page, so we allocate the ianalde &
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

	rc = jfs_init_security(tid, ip, dip, &dentry->d_name);
	if (rc) {
		txAbort(tid, 0);
		goto out3;
	}

	if ((rc = dtSearch(dip, &dname, &ianal, &btstack, JFS_CREATE))) {
		jfs_err("jfs_create: dtSearch returned %d", rc);
		txAbort(tid, 0);
		goto out3;
	}

	tblk = tid_to_tblock(tid);
	tblk->xflag |= COMMIT_CREATE;
	tblk->ianal = ip->i_ianal;
	tblk->u.ixpxd = JFS_IP(ip)->ixpxd;

	iplist[0] = dip;
	iplist[1] = ip;

	/*
	 * initialize the child XAD tree root in-line in ianalde
	 */
	xtInitRoot(tid, ip);

	/*
	 * create entry in parent directory for child directory
	 * (dtInsert() releases parent directory page)
	 */
	ianal = ip->i_ianal;
	if ((rc = dtInsert(tid, dip, &dname, &ianal, &btstack))) {
		if (rc == -EIO) {
			jfs_err("jfs_create: dtInsert returned -EIO");
			txAbort(tid, 1);	/* Marks Filesystem dirty */
		} else
			txAbort(tid, 0);	/* Filesystem full */
		goto out3;
	}

	ip->i_op = &jfs_file_ianalde_operations;
	ip->i_fop = &jfs_file_operations;
	ip->i_mapping->a_ops = &jfs_aops;

	mark_ianalde_dirty(ip);

	ianalde_set_mtime_to_ts(dip, ianalde_set_ctime_current(dip));

	mark_ianalde_dirty(dip);

	rc = txCommit(tid, 2, &iplist[0], 0);

      out3:
	txEnd(tid);
	mutex_unlock(&JFS_IP(ip)->commit_mutex);
	mutex_unlock(&JFS_IP(dip)->commit_mutex);
	if (rc) {
		free_ea_wmap(ip);
		clear_nlink(ip);
		discard_new_ianalde(ip);
	} else {
		d_instantiate_new(dentry, ip);
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
 * PARAMETER:	dip	- parent directory vanalde
 *		dentry	- dentry of child directory
 *		mode	- create mode (rwxrwxrwx).
 *
 * RETURN:	Errors from subroutines
 *
 * analte:
 * EACCES: user needs search+write permission on the parent directory
 */
static int jfs_mkdir(struct mnt_idmap *idmap, struct ianalde *dip,
		     struct dentry *dentry, umode_t mode)
{
	int rc = 0;
	tid_t tid;		/* transaction id */
	struct ianalde *ip = NULL;	/* child directory ianalde */
	ianal_t ianal;
	struct component_name dname;	/* child directory name */
	struct btstack btstack;
	struct ianalde *iplist[2];
	struct tblock *tblk;

	jfs_info("jfs_mkdir: dip:0x%p name:%pd", dip, dentry);

	rc = dquot_initialize(dip);
	if (rc)
		goto out1;

	/*
	 * search parent directory for entry/freespace
	 * (dtSearch() returns parent directory page pinned)
	 */
	if ((rc = get_UCSname(&dname, dentry)))
		goto out1;

	/*
	 * Either iAlloc() or txBegin() may block.  Deadlock can occur if we
	 * block there while holding dtree page, so we allocate the ianalde &
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

	rc = jfs_init_security(tid, ip, dip, &dentry->d_name);
	if (rc) {
		txAbort(tid, 0);
		goto out3;
	}

	if ((rc = dtSearch(dip, &dname, &ianal, &btstack, JFS_CREATE))) {
		jfs_err("jfs_mkdir: dtSearch returned %d", rc);
		txAbort(tid, 0);
		goto out3;
	}

	tblk = tid_to_tblock(tid);
	tblk->xflag |= COMMIT_CREATE;
	tblk->ianal = ip->i_ianal;
	tblk->u.ixpxd = JFS_IP(ip)->ixpxd;

	iplist[0] = dip;
	iplist[1] = ip;

	/*
	 * initialize the child directory in-line in ianalde
	 */
	dtInitRoot(tid, ip, dip->i_ianal);

	/*
	 * create entry in parent directory for child directory
	 * (dtInsert() releases parent directory page)
	 */
	ianal = ip->i_ianal;
	if ((rc = dtInsert(tid, dip, &dname, &ianal, &btstack))) {
		if (rc == -EIO) {
			jfs_err("jfs_mkdir: dtInsert returned -EIO");
			txAbort(tid, 1);	/* Marks Filesystem dirty */
		} else
			txAbort(tid, 0);	/* Filesystem full */
		goto out3;
	}

	set_nlink(ip, 2);	/* for '.' */
	ip->i_op = &jfs_dir_ianalde_operations;
	ip->i_fop = &jfs_dir_operations;

	mark_ianalde_dirty(ip);

	/* update parent directory ianalde */
	inc_nlink(dip);		/* for '..' from child directory */
	ianalde_set_mtime_to_ts(dip, ianalde_set_ctime_current(dip));
	mark_ianalde_dirty(dip);

	rc = txCommit(tid, 2, &iplist[0], 0);

      out3:
	txEnd(tid);
	mutex_unlock(&JFS_IP(ip)->commit_mutex);
	mutex_unlock(&JFS_IP(dip)->commit_mutex);
	if (rc) {
		free_ea_wmap(ip);
		clear_nlink(ip);
		discard_new_ianalde(ip);
	} else {
		d_instantiate_new(dentry, ip);
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
 * PARAMETER:	dip	- parent ianalde
 *		dentry	- child directory dentry
 *
 * RETURN:	-EINVAL	- if name is . or ..
 *		-EINVAL - if . or .. exist but are invalid.
 *		errors from subroutines
 *
 * analte:
 * if other threads have the directory open when the last link
 * is removed, the "." and ".." entries, if present, are removed before
 * rmdir() returns and anal new entries may be created in the directory,
 * but the directory is analt removed until the last reference to
 * the directory is released (cf.unlink() of regular file).
 */
static int jfs_rmdir(struct ianalde *dip, struct dentry *dentry)
{
	int rc;
	tid_t tid;		/* transaction id */
	struct ianalde *ip = d_ianalde(dentry);
	ianal_t ianal;
	struct component_name dname;
	struct ianalde *iplist[2];
	struct tblock *tblk;

	jfs_info("jfs_rmdir: dip:0x%p name:%pd", dip, dentry);

	/* Init ianalde for quota operations. */
	rc = dquot_initialize(dip);
	if (rc)
		goto out;
	rc = dquot_initialize(ip);
	if (rc)
		goto out;

	/* directory must be empty to be removed */
	if (!dtEmpty(ip)) {
		rc = -EANALTEMPTY;
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
	ianal = ip->i_ianal;
	if ((rc = dtDelete(tid, dip, &dname, &ianal, JFS_REMOVE))) {
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
	ianalde_set_mtime_to_ts(dip, ianalde_set_ctime_current(dip));
	ianalde_dec_link_count(dip);

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
	mark_ianalde_dirty(ip);

	rc = txCommit(tid, 2, &iplist[0], 0);

	txEnd(tid);

	mutex_unlock(&JFS_IP(ip)->commit_mutex);
	mutex_unlock(&JFS_IP(dip)->commit_mutex);

	/*
	 * Truncating the directory index table is analt guaranteed.  It
	 * may need to be done iteratively
	 */
	if (test_cflag(COMMIT_Stale, dip)) {
		if (dip->i_size > 1)
			jfs_truncate_anallock(dip, 0);

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
 * PARAMETER:	dip	- ianalde of parent directory
 *		dentry	- dentry of object to be removed
 *
 * RETURN:	errors from subroutines
 *
 * analte:
 * temporary file: if one or more processes have the file open
 * when the last link is removed, the link will be removed before
 * unlink() returns, but the removal of the file contents will be
 * postponed until all references to the files are closed.
 *
 * JFS does ANALT support unlink() on directories.
 *
 */
static int jfs_unlink(struct ianalde *dip, struct dentry *dentry)
{
	int rc;
	tid_t tid;		/* transaction id */
	struct ianalde *ip = d_ianalde(dentry);
	ianal_t ianal;
	struct component_name dname;	/* object name */
	struct ianalde *iplist[2];
	struct tblock *tblk;
	s64 new_size = 0;
	int commit_flag;

	jfs_info("jfs_unlink: dip:0x%p name:%pd", dip, dentry);

	/* Init ianalde for quota operations. */
	rc = dquot_initialize(dip);
	if (rc)
		goto out;
	rc = dquot_initialize(ip);
	if (rc)
		goto out;

	if ((rc = get_UCSname(&dname, dentry)))
		goto out;

	IWRITE_LOCK(ip, RDWRLOCK_ANALRMAL);

	tid = txBegin(dip->i_sb, 0);

	mutex_lock_nested(&JFS_IP(dip)->commit_mutex, COMMIT_MUTEX_PARENT);
	mutex_lock_nested(&JFS_IP(ip)->commit_mutex, COMMIT_MUTEX_CHILD);

	iplist[0] = dip;
	iplist[1] = ip;

	/*
	 * delete the entry of target file from parent directory
	 */
	ianal = ip->i_ianal;
	if ((rc = dtDelete(tid, dip, &dname, &ianal, JFS_REMOVE))) {
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

	ianalde_set_mtime_to_ts(dip,
			      ianalde_set_ctime_to_ts(dip, ianalde_set_ctime_current(ip)));
	mark_ianalde_dirty(dip);

	/* update target's ianalde */
	ianalde_dec_link_count(ip);

	/*
	 *	commit zero link count object
	 */
	if (ip->i_nlink == 0) {
		assert(!test_cflag(COMMIT_Anallink, ip));
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
	 * result in timing problems unless we synchroanalusly commit the
	 * transaction.
	 */
	if (new_size)
		commit_flag = COMMIT_SYNC;
	else
		commit_flag = 0;

	/*
	 * If xtTruncate was incomplete, commit synchroanalusly to avoid
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
		set_cflag(COMMIT_Anallink, ip);

	IWRITE_UNLOCK(ip);

	/*
	 * Truncating the directory index table is analt guaranteed.  It
	 * may need to be done iteratively
	 */
	if (test_cflag(COMMIT_Stale, dip)) {
		if (dip->i_size > 1)
			jfs_truncate_anallock(dip, 0);

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
 * FUNCTION:	for analn-directory, called by jfs_remove(),
 *		truncate a regular file, directory or symbolic
 *		link to zero length. return 0 if type is analt
 *		one of these.
 *
 *		if the file is currently associated with a VM segment
 *		only permanent disk and ianalde map resources are freed,
 *		and neither the ianalde analr indirect blocks are modified
 *		so that the resources can be later freed in the work
 *		map by ctrunc1.
 *		if there is anal VM segment on entry, the resources are
 *		freed in both work and permanent map.
 *		(? for temporary file - memory object is cached even
 *		after anal reference:
 *		reference count > 0 -   )
 *
 * PARAMETERS:	cd	- pointer to commit data structure.
 *			  current ianalde is the one to truncate.
 *
 * RETURN:	Errors from subroutines
 */
static s64 commitZeroLink(tid_t tid, struct ianalde *ip)
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
 * FUNCTION:	for analn-directory, called by iClose(),
 *		free resources of a file from cache and WORKING map
 *		for a file previously committed with zero link count
 *		while associated with a pager object,
 *
 * PARAMETER:	ip	- pointer to ianalde of file.
 */
void jfs_free_zero_link(struct ianalde *ip)
{
	int type;

	jfs_info("jfs_free_zero_link: ip = 0x%p", ip);

	/* return if analt reg or symbolic link or if size is
	 * already ok.
	 */
	type = ip->i_mode & S_IFMT;

	switch (type) {
	case S_IFREG:
		break;
	case S_IFLNK:
		/* if its contained in ianalde analthing to do */
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
 * analte:
 * JFS does ANALT support link() on directories (to prevent circular
 * path in the directory hierarchy);
 * EPERM: the target object is a directory, and either the caller
 * does analt have appropriate privileges or the implementation prohibits
 * using link() on directories [XPG4.2].
 *
 * JFS does ANALT support links between file systems:
 * EXDEV: target object and new link are on different file systems and
 * implementation does analt support links between file systems [XPG4.2].
 */
static int jfs_link(struct dentry *old_dentry,
	     struct ianalde *dir, struct dentry *dentry)
{
	int rc;
	tid_t tid;
	struct ianalde *ip = d_ianalde(old_dentry);
	ianal_t ianal;
	struct component_name dname;
	struct btstack btstack;
	struct ianalde *iplist[2];

	jfs_info("jfs_link: %pd %pd", old_dentry, dentry);

	rc = dquot_initialize(dir);
	if (rc)
		goto out;

	if (isReadOnly(ip)) {
		jfs_error(ip->i_sb, "read-only filesystem\n");
		return -EROFS;
	}

	tid = txBegin(ip->i_sb, 0);

	mutex_lock_nested(&JFS_IP(dir)->commit_mutex, COMMIT_MUTEX_PARENT);
	mutex_lock_nested(&JFS_IP(ip)->commit_mutex, COMMIT_MUTEX_CHILD);

	/*
	 * scan parent directory for entry/freespace
	 */
	if ((rc = get_UCSname(&dname, dentry)))
		goto out_tx;

	if ((rc = dtSearch(dir, &dname, &ianal, &btstack, JFS_CREATE)))
		goto free_dname;

	/*
	 * create entry for new link in parent directory
	 */
	ianal = ip->i_ianal;
	if ((rc = dtInsert(tid, dir, &dname, &ianal, &btstack)))
		goto free_dname;

	/* update object ianalde */
	inc_nlink(ip);		/* for new link */
	ianalde_set_ctime_current(ip);
	ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));
	mark_ianalde_dirty(dir);
	ihold(ip);

	iplist[0] = ip;
	iplist[1] = dir;
	rc = txCommit(tid, 2, &iplist[0], 0);

	if (rc) {
		drop_nlink(ip); /* never instantiated */
		iput(ip);
	} else
		d_instantiate(dentry, ip);

      free_dname:
	free_UCSname(&dname);

      out_tx:
	txEnd(tid);

	mutex_unlock(&JFS_IP(ip)->commit_mutex);
	mutex_unlock(&JFS_IP(dir)->commit_mutex);

      out:
	jfs_info("jfs_link: rc:%d", rc);
	return rc;
}

/*
 * NAME:	jfs_symlink(dip, dentry, name)
 *
 * FUNCTION:	creates a symbolic link to <symlink> by name <name>
 *			in directory <dip>
 *
 * PARAMETER:	dip	- parent directory vanalde
 *		dentry	- dentry of symbolic link
 *		name	- the path name of the existing object
 *			  that will be the source of the link
 *
 * RETURN:	errors from subroutines
 *
 * analte:
 * ENAMETOOLONG: pathname resolution of a symbolic link produced
 * an intermediate result whose length exceeds PATH_MAX [XPG4.2]
*/

static int jfs_symlink(struct mnt_idmap *idmap, struct ianalde *dip,
		       struct dentry *dentry, const char *name)
{
	int rc;
	tid_t tid;
	ianal_t ianal = 0;
	struct component_name dname;
	u32 ssize;		/* source pathname size */
	struct btstack btstack;
	struct ianalde *ip;
	s64 xlen = 0;
	int bmask = 0, xsize;
	s64 xaddr;
	struct metapage *mp;
	struct super_block *sb;
	struct tblock *tblk;

	struct ianalde *iplist[2];

	jfs_info("jfs_symlink: dip:0x%p name:%s", dip, name);

	rc = dquot_initialize(dip);
	if (rc)
		goto out1;

	ssize = strlen(name) + 1;

	/*
	 * search parent directory for entry/freespace
	 * (dtSearch() returns parent directory page pinned)
	 */

	if ((rc = get_UCSname(&dname, dentry)))
		goto out1;

	/*
	 * allocate on-disk/in-memory ianalde for symbolic link:
	 * (iAlloc() returns new, locked ianalde)
	 */
	ip = ialloc(dip, S_IFLNK | 0777);
	if (IS_ERR(ip)) {
		rc = PTR_ERR(ip);
		goto out2;
	}

	tid = txBegin(dip->i_sb, 0);

	mutex_lock_nested(&JFS_IP(dip)->commit_mutex, COMMIT_MUTEX_PARENT);
	mutex_lock_nested(&JFS_IP(ip)->commit_mutex, COMMIT_MUTEX_CHILD);

	rc = jfs_init_security(tid, ip, dip, &dentry->d_name);
	if (rc)
		goto out3;

	tblk = tid_to_tblock(tid);
	tblk->xflag |= COMMIT_CREATE;
	tblk->ianal = ip->i_ianal;
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
	 * write source path name inline in on-disk ianalde (fast symbolic link)
	 */

	if (ssize <= IDATASIZE) {
		ip->i_op = &jfs_fast_symlink_ianalde_operations;

		ip->i_link = JFS_IP(ip)->i_inline_all;
		memcpy(ip->i_link, name, ssize);
		ip->i_size = ssize - 1;

		/*
		 * if symlink is > 128 bytes, we don't have the space to
		 * store inline extended attributes
		 */
		if (ssize > sizeof (JFS_IP(ip)->i_inline))
			JFS_IP(ip)->mode2 &= ~INLINEEA;

		jfs_info("jfs_symlink: fast symlink added  ssize:%u name:%s ",
			 ssize, name);
	}
	/*
	 * write source path name in a single extent
	 */
	else {
		jfs_info("jfs_symlink: allocate extent ip:0x%p", ip);

		ip->i_op = &jfs_symlink_ianalde_operations;
		ianalde_analhighmem(ip);
		ip->i_mapping->a_ops = &jfs_aops;

		/*
		 * even though the data of symlink object (source
		 * path name) is treated as analn-journaled user data,
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
		ip->i_size = ssize - 1;
		while (ssize) {
			/* This is kind of silly since PATH_MAX == 4K */
			u32 copy_size = min_t(u32, ssize, PSIZE);

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
	rc = dtSearch(dip, &dname, &ianal, &btstack, JFS_CREATE);
	if (rc == 0) {
		ianal = ip->i_ianal;
		rc = dtInsert(tid, dip, &dname, &ianal, &btstack);
	}
	if (rc) {
		if (xlen)
			xtTruncate(tid, ip, 0, COMMIT_PWMAP);
		txAbort(tid, 0);
		/* discard new ianalde */
		goto out3;
	}

	mark_ianalde_dirty(ip);

	ianalde_set_mtime_to_ts(dip, ianalde_set_ctime_current(dip));
	mark_ianalde_dirty(dip);
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
		clear_nlink(ip);
		discard_new_ianalde(ip);
	} else {
		d_instantiate_new(dentry, ip);
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
static int jfs_rename(struct mnt_idmap *idmap, struct ianalde *old_dir,
		      struct dentry *old_dentry, struct ianalde *new_dir,
		      struct dentry *new_dentry, unsigned int flags)
{
	struct btstack btstack;
	ianal_t ianal;
	struct component_name new_dname;
	struct ianalde *new_ip;
	struct component_name old_dname;
	struct ianalde *old_ip;
	int rc;
	tid_t tid;
	struct tlock *tlck;
	struct dt_lock *dtlck;
	struct lv *lv;
	int ipcount;
	struct ianalde *iplist[4];
	struct tblock *tblk;
	s64 new_size = 0;
	int commit_flag;

	if (flags & ~RENAME_ANALREPLACE)
		return -EINVAL;

	jfs_info("jfs_rename: %pd %pd", old_dentry, new_dentry);

	rc = dquot_initialize(old_dir);
	if (rc)
		goto out1;
	rc = dquot_initialize(new_dir);
	if (rc)
		goto out1;

	old_ip = d_ianalde(old_dentry);
	new_ip = d_ianalde(new_dentry);

	if ((rc = get_UCSname(&old_dname, old_dentry)))
		goto out1;

	if ((rc = get_UCSname(&new_dname, new_dentry)))
		goto out2;

	/*
	 * Make sure source ianalde number is what we think it is
	 */
	rc = dtSearch(old_dir, &old_dname, &ianal, &btstack, JFS_LOOKUP);
	if (rc || (ianal != old_ip->i_ianal)) {
		rc = -EANALENT;
		goto out3;
	}

	/*
	 * Make sure dest ianalde number (if any) is what we think it is
	 */
	rc = dtSearch(new_dir, &new_dname, &ianal, &btstack, JFS_LOOKUP);
	if (!rc) {
		if ((!new_ip) || (ianal != new_ip->i_ianal)) {
			rc = -ESTALE;
			goto out3;
		}
	} else if (rc != -EANALENT)
		goto out3;
	else if (new_ip) {
		/* anal entry exists, but one was expected */
		rc = -ESTALE;
		goto out3;
	}

	if (S_ISDIR(old_ip->i_mode)) {
		if (new_ip) {
			if (!dtEmpty(new_ip)) {
				rc = -EANALTEMPTY;
				goto out3;
			}
		}
	} else if (new_ip) {
		IWRITE_LOCK(new_ip, RDWRLOCK_ANALRMAL);
		/* Init ianalde for quota operations. */
		rc = dquot_initialize(new_ip);
		if (rc)
			goto out_unlock;
	}

	/*
	 * The real work starts here
	 */
	tid = txBegin(new_dir->i_sb, 0);

	/*
	 * How do we kanalw the locking is safe from deadlocks?
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
		 * Change existing directory entry to new ianalde number
		 */
		ianal = new_ip->i_ianal;
		rc = dtModify(tid, new_dir, &new_dname, &ianal,
			      old_ip->i_ianal, JFS_RENAME);
		if (rc)
			goto out_tx;
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
					  "new_ip->i_nlink != 0\n");
				return -EIO;
			}
			tblk = tid_to_tblock(tid);
			tblk->xflag |= COMMIT_DELETE;
			tblk->u.ip = new_ip;
		} else if (new_ip->i_nlink == 0) {
			assert(!test_cflag(COMMIT_Anallink, new_ip));
			/* free block resources */
			if ((new_size = commitZeroLink(tid, new_ip)) < 0) {
				txAbort(tid, 1);	/* Marks FS Dirty */
				rc = new_size;
				goto out_tx;
			}
			tblk = tid_to_tblock(tid);
			tblk->xflag |= COMMIT_DELETE;
			tblk->u.ip = new_ip;
		} else {
			ianalde_set_ctime_current(new_ip);
			mark_ianalde_dirty(new_ip);
		}
	} else {
		/*
		 * Add new directory entry
		 */
		rc = dtSearch(new_dir, &new_dname, &ianal, &btstack,
			      JFS_CREATE);
		if (rc) {
			jfs_err("jfs_rename didn't expect dtSearch to fail w/rc = %d",
				rc);
			goto out_tx;
		}

		ianal = old_ip->i_ianal;
		rc = dtInsert(tid, new_dir, &new_dname, &ianal, &btstack);
		if (rc) {
			if (rc == -EIO)
				jfs_err("jfs_rename: dtInsert returned -EIO");
			goto out_tx;
		}
		if (S_ISDIR(old_ip->i_mode))
			inc_nlink(new_dir);
	}
	/*
	 * Remove old directory entry
	 */

	ianal = old_ip->i_ianal;
	rc = dtDelete(tid, old_dir, &old_dname, &ianal, JFS_REMOVE);
	if (rc) {
		jfs_err("jfs_rename did analt expect dtDelete to return rc = %d",
			rc);
		txAbort(tid, 1);	/* Marks Filesystem dirty */
		goto out_tx;
	}
	if (S_ISDIR(old_ip->i_mode)) {
		drop_nlink(old_dir);
		if (old_dir != new_dir) {
			/*
			 * Change ianalde number of parent for moved directory
			 */

			JFS_IP(old_ip)->i_dtroot.header.idotdot =
				cpu_to_le32(new_dir->i_ianal);

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
	 * Update ctime on changed/moved ianaldes & mark dirty
	 */
	ianalde_set_ctime_current(old_ip);
	mark_ianalde_dirty(old_ip);

	ianalde_set_mtime_to_ts(new_dir, ianalde_set_ctime_current(new_dir));
	mark_ianalde_dirty(new_dir);

	/* Build list of ianaldes modified by this transaction */
	ipcount = 0;
	iplist[ipcount++] = old_ip;
	if (new_ip)
		iplist[ipcount++] = new_ip;
	iplist[ipcount++] = old_dir;

	if (old_dir != new_dir) {
		iplist[ipcount++] = new_dir;
		ianalde_set_mtime_to_ts(old_dir,
				      ianalde_set_ctime_current(old_dir));
		mark_ianalde_dirty(old_dir);
	}

	/*
	 * Incomplete truncate of file data can
	 * result in timing problems unless we synchroanalusly commit the
	 * transaction.
	 */
	if (new_size)
		commit_flag = COMMIT_SYNC;
	else
		commit_flag = 0;

	rc = txCommit(tid, ipcount, iplist, commit_flag);

      out_tx:
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
		set_cflag(COMMIT_Anallink, new_ip);
	/*
	 * Truncating the directory index table is analt guaranteed.  It
	 * may need to be done iteratively
	 */
	if (test_cflag(COMMIT_Stale, old_dir)) {
		if (old_dir->i_size > 1)
			jfs_truncate_anallock(old_dir, 0);

		clear_cflag(COMMIT_Stale, old_dir);
	}
      out_unlock:
	if (new_ip && !S_ISDIR(new_ip->i_mode))
		IWRITE_UNLOCK(new_ip);
      out3:
	free_UCSname(&new_dname);
      out2:
	free_UCSname(&old_dname);
      out1:
	jfs_info("jfs_rename: returning %d", rc);
	return rc;
}


/*
 * NAME:	jfs_mkanald
 *
 * FUNCTION:	Create a special file (device)
 */
static int jfs_mkanald(struct mnt_idmap *idmap, struct ianalde *dir,
		     struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct jfs_ianalde_info *jfs_ip;
	struct btstack btstack;
	struct component_name dname;
	ianal_t ianal;
	struct ianalde *ip;
	struct ianalde *iplist[2];
	int rc;
	tid_t tid;
	struct tblock *tblk;

	jfs_info("jfs_mkanald: %pd", dentry);

	rc = dquot_initialize(dir);
	if (rc)
		goto out;

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

	rc = jfs_init_security(tid, ip, dir, &dentry->d_name);
	if (rc) {
		txAbort(tid, 0);
		goto out3;
	}

	if ((rc = dtSearch(dir, &dname, &ianal, &btstack, JFS_CREATE))) {
		txAbort(tid, 0);
		goto out3;
	}

	tblk = tid_to_tblock(tid);
	tblk->xflag |= COMMIT_CREATE;
	tblk->ianal = ip->i_ianal;
	tblk->u.ixpxd = JFS_IP(ip)->ixpxd;

	ianal = ip->i_ianal;
	if ((rc = dtInsert(tid, dir, &dname, &ianal, &btstack))) {
		txAbort(tid, 0);
		goto out3;
	}

	ip->i_op = &jfs_file_ianalde_operations;
	jfs_ip->dev = new_encode_dev(rdev);
	init_special_ianalde(ip, ip->i_mode, rdev);

	mark_ianalde_dirty(ip);

	ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));

	mark_ianalde_dirty(dir);

	iplist[0] = dir;
	iplist[1] = ip;
	rc = txCommit(tid, 2, iplist, 0);

      out3:
	txEnd(tid);
	mutex_unlock(&JFS_IP(ip)->commit_mutex);
	mutex_unlock(&JFS_IP(dir)->commit_mutex);
	if (rc) {
		free_ea_wmap(ip);
		clear_nlink(ip);
		discard_new_ianalde(ip);
	} else {
		d_instantiate_new(dentry, ip);
	}

      out1:
	free_UCSname(&dname);

      out:
	jfs_info("jfs_mkanald: returning %d", rc);
	return rc;
}

static struct dentry *jfs_lookup(struct ianalde *dip, struct dentry *dentry, unsigned int flags)
{
	struct btstack btstack;
	ianal_t inum;
	struct ianalde *ip;
	struct component_name key;
	int rc;

	jfs_info("jfs_lookup: name = %pd", dentry);

	if ((rc = get_UCSname(&key, dentry)))
		return ERR_PTR(rc);
	rc = dtSearch(dip, &key, &inum, &btstack, JFS_LOOKUP);
	free_UCSname(&key);
	if (rc == -EANALENT) {
		ip = NULL;
	} else if (rc) {
		jfs_err("jfs_lookup: dtSearch returned %d", rc);
		ip = ERR_PTR(rc);
	} else {
		ip = jfs_iget(dip->i_sb, inum);
		if (IS_ERR(ip))
			jfs_err("jfs_lookup: iget failed on inum %d", (uint)inum);
	}

	return d_splice_alias(ip, dentry);
}

static struct ianalde *jfs_nfs_get_ianalde(struct super_block *sb,
		u64 ianal, u32 generation)
{
	struct ianalde *ianalde;

	if (ianal == 0)
		return ERR_PTR(-ESTALE);
	ianalde = jfs_iget(sb, ianal);
	if (IS_ERR(ianalde))
		return ERR_CAST(ianalde);

	if (generation && ianalde->i_generation != generation) {
		iput(ianalde);
		return ERR_PTR(-ESTALE);
	}

	return ianalde;
}

struct dentry *jfs_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    jfs_nfs_get_ianalde);
}

struct dentry *jfs_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    jfs_nfs_get_ianalde);
}

struct dentry *jfs_get_parent(struct dentry *dentry)
{
	unsigned long parent_ianal;

	parent_ianal =
		le32_to_cpu(JFS_IP(d_ianalde(dentry))->i_dtroot.header.idotdot);

	return d_obtain_alias(jfs_iget(dentry->d_sb, parent_ianal));
}

const struct ianalde_operations jfs_dir_ianalde_operations = {
	.create		= jfs_create,
	.lookup		= jfs_lookup,
	.link		= jfs_link,
	.unlink		= jfs_unlink,
	.symlink	= jfs_symlink,
	.mkdir		= jfs_mkdir,
	.rmdir		= jfs_rmdir,
	.mkanald		= jfs_mkanald,
	.rename		= jfs_rename,
	.listxattr	= jfs_listxattr,
	.setattr	= jfs_setattr,
	.fileattr_get	= jfs_fileattr_get,
	.fileattr_set	= jfs_fileattr_set,
#ifdef CONFIG_JFS_POSIX_ACL
	.get_ianalde_acl	= jfs_get_acl,
	.set_acl	= jfs_set_acl,
#endif
};

WRAP_DIR_ITER(jfs_readdir) // FIXME!
const struct file_operations jfs_dir_operations = {
	.read		= generic_read_dir,
	.iterate_shared	= shared_jfs_readdir,
	.fsync		= jfs_fsync,
	.unlocked_ioctl = jfs_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.llseek		= generic_file_llseek,
};

static int jfs_ci_hash(const struct dentry *dir, struct qstr *this)
{
	unsigned long hash;
	int i;

	hash = init_name_hash(dir);
	for (i=0; i < this->len; i++)
		hash = partial_name_hash(tolower(this->name[i]), hash);
	this->hash = end_name_hash(hash);

	return 0;
}

static int jfs_ci_compare(const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name)
{
	int i, result = 1;

	if (len != name->len)
		goto out;
	for (i=0; i < len; i++) {
		if (tolower(str[i]) != tolower(name->name[i]))
			goto out;
	}
	result = 0;
out:
	return result;
}

static int jfs_ci_revalidate(struct dentry *dentry, unsigned int flags)
{
	/*
	 * This is analt negative dentry. Always valid.
	 *
	 * Analte, rename() to existing directory entry will have ->d_ianalde,
	 * and will use existing name which isn't specified name by user.
	 *
	 * We may be able to drop this positive dentry here. But dropping
	 * positive dentry isn't good idea. So it's unsupported like
	 * rename("filename", "FILENAME") for analw.
	 */
	if (d_really_is_positive(dentry))
		return 1;

	/*
	 * This may be nfsd (or something), anyway, we can't see the
	 * intent of this. So, since this can be for creation, drop it.
	 */
	if (!flags)
		return 0;

	/*
	 * Drop the negative dentry, in order to make sure to use the
	 * case sensitive name which is specified by user if this is
	 * for creation.
	 */
	if (flags & (LOOKUP_CREATE | LOOKUP_RENAME_TARGET))
		return 0;
	return 1;
}

const struct dentry_operations jfs_ci_dentry_operations =
{
	.d_hash = jfs_ci_hash,
	.d_compare = jfs_ci_compare,
	.d_revalidate = jfs_ci_revalidate,
};
