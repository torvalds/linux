/*
 *   fs/cifs/cifsfs.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2007
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   Common Internet FileSystem (CIFS) client
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* Note that BB means BUGBUG (ie something to fix eventually) */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/seq_file.h>
#include <linux/vfs.h>
#include <linux/mempool.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include "cifsfs.h"
#include "cifspdu.h"
#define DECLARE_GLOBALS_HERE
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include <linux/mm.h>
#include <linux/key-type.h>
#include "dns_resolve.h"
#include "cifs_spnego.h"
#define CIFS_MAGIC_NUMBER 0xFF534D42	/* the first four bytes of SMB PDUs */

#ifdef CONFIG_CIFS_QUOTA
static struct quotactl_ops cifs_quotactl_ops;
#endif /* QUOTA */

int cifsFYI = 0;
int cifsERROR = 1;
int traceSMB = 0;
unsigned int oplockEnabled = 1;
unsigned int experimEnabled = 0;
unsigned int linuxExtEnabled = 1;
unsigned int lookupCacheEnabled = 1;
unsigned int multiuser_mount = 0;
unsigned int extended_security = CIFSSEC_DEF;
/* unsigned int ntlmv2_support = 0; */
unsigned int sign_CIFS_PDUs = 1;
extern struct task_struct *oplockThread; /* remove sparse warning */
struct task_struct *oplockThread = NULL;
/* extern struct task_struct * dnotifyThread; remove sparse warning */
static struct task_struct *dnotifyThread = NULL;
static const struct super_operations cifs_super_ops;
unsigned int CIFSMaxBufSize = CIFS_MAX_MSGSIZE;
module_param(CIFSMaxBufSize, int, 0);
MODULE_PARM_DESC(CIFSMaxBufSize, "Network buffer size (not including header). "
				 "Default: 16384 Range: 8192 to 130048");
unsigned int cifs_min_rcv = CIFS_MIN_RCV_POOL;
module_param(cifs_min_rcv, int, 0);
MODULE_PARM_DESC(cifs_min_rcv, "Network buffers in pool. Default: 4 Range: "
				"1 to 64");
unsigned int cifs_min_small = 30;
module_param(cifs_min_small, int, 0);
MODULE_PARM_DESC(cifs_min_small, "Small network buffers in pool. Default: 30 "
				 "Range: 2 to 256");
unsigned int cifs_max_pending = CIFS_MAX_REQ;
module_param(cifs_max_pending, int, 0);
MODULE_PARM_DESC(cifs_max_pending, "Simultaneous requests to server. "
				   "Default: 50 Range: 2 to 256");

extern mempool_t *cifs_sm_req_poolp;
extern mempool_t *cifs_req_poolp;
extern mempool_t *cifs_mid_poolp;

extern struct kmem_cache *cifs_oplock_cachep;

static int
cifs_read_super(struct super_block *sb, void *data,
		const char *devname, int silent)
{
	struct inode *inode;
	struct cifs_sb_info *cifs_sb;
#ifdef CONFIG_CIFS_DFS_UPCALL
	int len;
#endif
	int rc = 0;

	/* BB should we make this contingent on mount parm? */
	sb->s_flags |= MS_NODIRATIME | MS_NOATIME;
	sb->s_fs_info = kzalloc(sizeof(struct cifs_sb_info), GFP_KERNEL);
	cifs_sb = CIFS_SB(sb);
	if (cifs_sb == NULL)
		return -ENOMEM;

#ifdef CONFIG_CIFS_DFS_UPCALL
	/* copy mount params to sb for use in submounts */
	/* BB: should we move this after the mount so we
	 * do not have to do the copy on failed mounts?
	 * BB: May be it is better to do simple copy before
	 * complex operation (mount), and in case of fail
	 * just exit instead of doing mount and attempting
	 * undo it if this copy fails?*/
	len = strlen(data);
	cifs_sb->mountdata = kzalloc(len + 1, GFP_KERNEL);
	if (cifs_sb->mountdata == NULL) {
		kfree(sb->s_fs_info);
		sb->s_fs_info = NULL;
		return -ENOMEM;
	}
	strncpy(cifs_sb->mountdata, data, len + 1);
	cifs_sb->mountdata[len] = '\0';
#endif

	rc = cifs_mount(sb, cifs_sb, data, devname);

	if (rc) {
		if (!silent)
			cERROR(1,
			       ("cifs_mount failed w/return code = %d", rc));
		goto out_mount_failed;
	}

	sb->s_magic = CIFS_MAGIC_NUMBER;
	sb->s_op = &cifs_super_ops;
/*	if (cifs_sb->tcon->ses->server->maxBuf > MAX_CIFS_HDR_SIZE + 512)
	    sb->s_blocksize =
		cifs_sb->tcon->ses->server->maxBuf - MAX_CIFS_HDR_SIZE; */
#ifdef CONFIG_CIFS_QUOTA
	sb->s_qcop = &cifs_quotactl_ops;
#endif
	sb->s_blocksize = CIFS_MAX_MSGSIZE;
	sb->s_blocksize_bits = 14;	/* default 2**14 = CIFS_MAX_MSGSIZE */
	inode = cifs_iget(sb, ROOT_I);

	if (IS_ERR(inode)) {
		rc = PTR_ERR(inode);
		inode = NULL;
		goto out_no_root;
	}

	sb->s_root = d_alloc_root(inode);

	if (!sb->s_root) {
		rc = -ENOMEM;
		goto out_no_root;
	}

#ifdef CONFIG_CIFS_EXPERIMENTAL
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM) {
		cFYI(1, ("export ops supported"));
		sb->s_export_op = &cifs_export_ops;
	}
#endif /* EXPERIMENTAL */

	return 0;

out_no_root:
	cERROR(1, ("cifs_read_super: get root inode failed"));
	if (inode)
		iput(inode);

out_mount_failed:
	if (cifs_sb) {
#ifdef CONFIG_CIFS_DFS_UPCALL
		if (cifs_sb->mountdata) {
			kfree(cifs_sb->mountdata);
			cifs_sb->mountdata = NULL;
		}
#endif
		if (cifs_sb->local_nls)
			unload_nls(cifs_sb->local_nls);
		kfree(cifs_sb);
	}
	return rc;
}

static void
cifs_put_super(struct super_block *sb)
{
	int rc = 0;
	struct cifs_sb_info *cifs_sb;

	cFYI(1, ("In cifs_put_super"));
	cifs_sb = CIFS_SB(sb);
	if (cifs_sb == NULL) {
		cFYI(1, ("Empty cifs superblock info passed to unmount"));
		return;
	}
	rc = cifs_umount(sb, cifs_sb);
	if (rc)
		cERROR(1, ("cifs_umount failed with return code %d", rc));
#ifdef CONFIG_CIFS_DFS_UPCALL
	if (cifs_sb->mountdata) {
		kfree(cifs_sb->mountdata);
		cifs_sb->mountdata = NULL;
	}
#endif

	unload_nls(cifs_sb->local_nls);
	kfree(cifs_sb);
	return;
}

static int
cifs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	int xid;
	int rc = -EOPNOTSUPP;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;

	xid = GetXid();

	cifs_sb = CIFS_SB(sb);
	pTcon = cifs_sb->tcon;

	buf->f_type = CIFS_MAGIC_NUMBER;

	/* instead could get the real value via SMB_QUERY_FS_ATTRIBUTE_INFO */
	buf->f_namelen = PATH_MAX; /* PATH_MAX may be too long - it would
				      presumably be total path, but note
				      that some servers (includinng Samba 3)
				      have a shorter maximum path */
	buf->f_files = 0;	/* undefined */
	buf->f_ffree = 0;	/* unlimited */

/* BB we could add a second check for a QFS Unix capability bit */
/* BB FIXME check CIFS_POSIX_EXTENSIONS Unix cap first FIXME BB */
    if ((pTcon->ses->capabilities & CAP_UNIX) && (CIFS_POSIX_EXTENSIONS &
			le64_to_cpu(pTcon->fsUnixInfo.Capability)))
	    rc = CIFSSMBQFSPosixInfo(xid, pTcon, buf);

    /* Only need to call the old QFSInfo if failed
    on newer one */
    if (rc)
	if (pTcon->ses->capabilities & CAP_NT_SMBS)
		rc = CIFSSMBQFSInfo(xid, pTcon, buf); /* not supported by OS2 */

	/* Some old Windows servers also do not support level 103, retry with
	   older level one if old server failed the previous call or we
	   bypassed it because we detected that this was an older LANMAN sess */
	if (rc)
		rc = SMBOldQFSInfo(xid, pTcon, buf);
	/* int f_type;
	   __fsid_t f_fsid;
	   int f_namelen;  */
	/* BB get from info in tcon struct at mount time call to QFSAttrInfo */
	FreeXid(xid);
	return 0;		/* always return success? what if volume is no
				   longer available? */
}

static int cifs_permission(struct inode *inode, int mask, struct nameidata *nd)
{
	struct cifs_sb_info *cifs_sb;

	cifs_sb = CIFS_SB(inode->i_sb);

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_PERM)
		return 0;
	else /* file mode might have been restricted at mount time
		on the client (above and beyond ACL on servers) for
		servers which do not support setting and viewing mode bits,
		so allowing client to check permissions is useful */
		return generic_permission(inode, mask, NULL);
}

static struct kmem_cache *cifs_inode_cachep;
static struct kmem_cache *cifs_req_cachep;
static struct kmem_cache *cifs_mid_cachep;
struct kmem_cache *cifs_oplock_cachep;
static struct kmem_cache *cifs_sm_req_cachep;
mempool_t *cifs_sm_req_poolp;
mempool_t *cifs_req_poolp;
mempool_t *cifs_mid_poolp;

static struct inode *
cifs_alloc_inode(struct super_block *sb)
{
	struct cifsInodeInfo *cifs_inode;
	cifs_inode = kmem_cache_alloc(cifs_inode_cachep, GFP_KERNEL);
	if (!cifs_inode)
		return NULL;
	cifs_inode->cifsAttrs = 0x20;	/* default */
	atomic_set(&cifs_inode->inUse, 0);
	cifs_inode->time = 0;
	cifs_inode->write_behind_rc = 0;
	/* Until the file is open and we have gotten oplock
	info back from the server, can not assume caching of
	file data or metadata */
	cifs_inode->clientCanCacheRead = FALSE;
	cifs_inode->clientCanCacheAll = FALSE;
	cifs_inode->vfs_inode.i_blkbits = 14;  /* 2**14 = CIFS_MAX_MSGSIZE */

	/* Can not set i_flags here - they get immediately overwritten
	   to zero by the VFS */
/*	cifs_inode->vfs_inode.i_flags = S_NOATIME | S_NOCMTIME;*/
	INIT_LIST_HEAD(&cifs_inode->openFileList);
	return &cifs_inode->vfs_inode;
}

static void
cifs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(cifs_inode_cachep, CIFS_I(inode));
}

/*
 * cifs_show_options() is for displaying mount options in /proc/mounts.
 * Not all settable options are displayed but most of the important
 * ones are.
 */
static int
cifs_show_options(struct seq_file *s, struct vfsmount *m)
{
	struct cifs_sb_info *cifs_sb;

	cifs_sb = CIFS_SB(m->mnt_sb);

	if (cifs_sb) {
		if (cifs_sb->tcon) {
/* BB add prepath to mount options displayed */
			seq_printf(s, ",unc=%s", cifs_sb->tcon->treeName);
			if (cifs_sb->tcon->ses) {
				if (cifs_sb->tcon->ses->userName)
					seq_printf(s, ",username=%s",
					   cifs_sb->tcon->ses->userName);
				if (cifs_sb->tcon->ses->domainName)
					seq_printf(s, ",domain=%s",
					   cifs_sb->tcon->ses->domainName);
			}
			if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_OVERR_UID) ||
			   !(cifs_sb->tcon->unix_ext))
				seq_printf(s, ",uid=%d", cifs_sb->mnt_uid);
			if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_OVERR_GID) ||
			   !(cifs_sb->tcon->unix_ext))
				seq_printf(s, ",gid=%d", cifs_sb->mnt_gid);
		}
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_POSIX_PATHS)
			seq_printf(s, ",posixpaths");
		seq_printf(s, ",rsize=%d", cifs_sb->rsize);
		seq_printf(s, ",wsize=%d", cifs_sb->wsize);
	}
	return 0;
}

#ifdef CONFIG_CIFS_QUOTA
int cifs_xquota_set(struct super_block *sb, int quota_type, qid_t qid,
		struct fs_disk_quota *pdquota)
{
	int xid;
	int rc = 0;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifsTconInfo *pTcon;

	if (cifs_sb)
		pTcon = cifs_sb->tcon;
	else
		return -EIO;


	xid = GetXid();
	if (pTcon) {
		cFYI(1, ("set type: 0x%x id: %d", quota_type, qid));
	} else {
		rc = -EIO;
	}

	FreeXid(xid);
	return rc;
}

int cifs_xquota_get(struct super_block *sb, int quota_type, qid_t qid,
		    struct fs_disk_quota *pdquota)
{
	int xid;
	int rc = 0;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifsTconInfo *pTcon;

	if (cifs_sb)
		pTcon = cifs_sb->tcon;
	else
		return -EIO;

	xid = GetXid();
	if (pTcon) {
		cFYI(1, ("set type: 0x%x id: %d", quota_type, qid));
	} else {
		rc = -EIO;
	}

	FreeXid(xid);
	return rc;
}

int cifs_xstate_set(struct super_block *sb, unsigned int flags, int operation)
{
	int xid;
	int rc = 0;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifsTconInfo *pTcon;

	if (cifs_sb)
		pTcon = cifs_sb->tcon;
	else
		return -EIO;

	xid = GetXid();
	if (pTcon) {
		cFYI(1, ("flags: 0x%x operation: 0x%x", flags, operation));
	} else {
		rc = -EIO;
	}

	FreeXid(xid);
	return rc;
}

int cifs_xstate_get(struct super_block *sb, struct fs_quota_stat *qstats)
{
	int xid;
	int rc = 0;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifsTconInfo *pTcon;

	if (cifs_sb) {
		pTcon = cifs_sb->tcon;
	} else {
		return -EIO;
	}
	xid = GetXid();
	if (pTcon) {
		cFYI(1, ("pqstats %p", qstats));
	} else {
		rc = -EIO;
	}

	FreeXid(xid);
	return rc;
}

static struct quotactl_ops cifs_quotactl_ops = {
	.set_xquota	= cifs_xquota_set,
	.get_xquota	= cifs_xquota_get,
	.set_xstate	= cifs_xstate_set,
	.get_xstate	= cifs_xstate_get,
};
#endif

static void cifs_umount_begin(struct vfsmount *vfsmnt, int flags)
{
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *tcon;

	dfs_shrink_umount_helper(vfsmnt);

	if (!(flags & MNT_FORCE))
		return;
	cifs_sb = CIFS_SB(vfsmnt->mnt_sb);
	if (cifs_sb == NULL)
		return;

	tcon = cifs_sb->tcon;
	if (tcon == NULL)
		return;
	down(&tcon->tconSem);
	if (atomic_read(&tcon->useCount) == 1)
		tcon->tidStatus = CifsExiting;
	up(&tcon->tconSem);

	/* cancel_brl_requests(tcon); */ /* BB mark all brl mids as exiting */
	/* cancel_notify_requests(tcon); */
	if (tcon->ses && tcon->ses->server) {
		cFYI(1, ("wake up tasks now - umount begin not complete"));
		wake_up_all(&tcon->ses->server->request_q);
		wake_up_all(&tcon->ses->server->response_q);
		msleep(1); /* yield */
		/* we have to kick the requests once more */
		wake_up_all(&tcon->ses->server->response_q);
		msleep(1);
	}
/* BB FIXME - finish add checks for tidStatus BB */

	return;
}

#ifdef CONFIG_CIFS_STATS2
static int cifs_show_stats(struct seq_file *s, struct vfsmount *mnt)
{
	/* BB FIXME */
	return 0;
}
#endif

static int cifs_remount(struct super_block *sb, int *flags, char *data)
{
	*flags |= MS_NODIRATIME;
	return 0;
}

static const struct super_operations cifs_super_ops = {
	.put_super = cifs_put_super,
	.statfs = cifs_statfs,
	.alloc_inode = cifs_alloc_inode,
	.destroy_inode = cifs_destroy_inode,
/*	.drop_inode	    = generic_delete_inode,
	.delete_inode	= cifs_delete_inode,  */  /* Do not need above two
	functions unless later we add lazy close of inodes or unless the
	kernel forgets to call us with the same number of releases (closes)
	as opens */
	.show_options = cifs_show_options,
	.umount_begin   = cifs_umount_begin,
	.remount_fs = cifs_remount,
#ifdef CONFIG_CIFS_STATS2
	.show_stats = cifs_show_stats,
#endif
};

static int
cifs_get_sb(struct file_system_type *fs_type,
	    int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	int rc;
	struct super_block *sb = sget(fs_type, NULL, set_anon_super, NULL);

	cFYI(1, ("Devname: %s flags: %d ", dev_name, flags));

	if (IS_ERR(sb))
		return PTR_ERR(sb);

	sb->s_flags = flags;

	rc = cifs_read_super(sb, data, dev_name, flags & MS_SILENT ? 1 : 0);
	if (rc) {
		up_write(&sb->s_umount);
		deactivate_super(sb);
		return rc;
	}
	sb->s_flags |= MS_ACTIVE;
	return simple_set_mnt(mnt, sb);
}

static ssize_t cifs_file_aio_write(struct kiocb *iocb, const struct iovec *iov,
				   unsigned long nr_segs, loff_t pos)
{
	struct inode *inode = iocb->ki_filp->f_path.dentry->d_inode;
	ssize_t written;

	written = generic_file_aio_write(iocb, iov, nr_segs, pos);
	if (!CIFS_I(inode)->clientCanCacheAll)
		filemap_fdatawrite(inode->i_mapping);
	return written;
}

static loff_t cifs_llseek(struct file *file, loff_t offset, int origin)
{
	/* origin == SEEK_END => we must revalidate the cached file length */
	if (origin == SEEK_END) {
		int retval;

		/* some applications poll for the file length in this strange
		   way so we must seek to end on non-oplocked files by
		   setting the revalidate time to zero */
		CIFS_I(file->f_path.dentry->d_inode)->time = 0;

		retval = cifs_revalidate(file->f_path.dentry);
		if (retval < 0)
			return (loff_t)retval;
	}
	return remote_llseek(file, offset, origin);
}

struct file_system_type cifs_fs_type = {
	.owner = THIS_MODULE,
	.name = "cifs",
	.get_sb = cifs_get_sb,
	.kill_sb = kill_anon_super,
	/*  .fs_flags */
};
const struct inode_operations cifs_dir_inode_ops = {
	.create = cifs_create,
	.lookup = cifs_lookup,
	.getattr = cifs_getattr,
	.unlink = cifs_unlink,
	.link = cifs_hardlink,
	.mkdir = cifs_mkdir,
	.rmdir = cifs_rmdir,
	.rename = cifs_rename,
	.permission = cifs_permission,
/*	revalidate:cifs_revalidate,   */
	.setattr = cifs_setattr,
	.symlink = cifs_symlink,
	.mknod   = cifs_mknod,
#ifdef CONFIG_CIFS_XATTR
	.setxattr = cifs_setxattr,
	.getxattr = cifs_getxattr,
	.listxattr = cifs_listxattr,
	.removexattr = cifs_removexattr,
#endif
};

const struct inode_operations cifs_file_inode_ops = {
/*	revalidate:cifs_revalidate, */
	.setattr = cifs_setattr,
	.getattr = cifs_getattr, /* do we need this anymore? */
	.rename = cifs_rename,
	.permission = cifs_permission,
#ifdef CONFIG_CIFS_XATTR
	.setxattr = cifs_setxattr,
	.getxattr = cifs_getxattr,
	.listxattr = cifs_listxattr,
	.removexattr = cifs_removexattr,
#endif
};

const struct inode_operations cifs_symlink_inode_ops = {
	.readlink = generic_readlink,
	.follow_link = cifs_follow_link,
	.put_link = cifs_put_link,
	.permission = cifs_permission,
	/* BB add the following two eventually */
	/* revalidate: cifs_revalidate,
	   setattr:    cifs_notify_change, *//* BB do we need notify change */
#ifdef CONFIG_CIFS_XATTR
	.setxattr = cifs_setxattr,
	.getxattr = cifs_getxattr,
	.listxattr = cifs_listxattr,
	.removexattr = cifs_removexattr,
#endif
};

const struct file_operations cifs_file_ops = {
	.read = do_sync_read,
	.write = do_sync_write,
	.aio_read = generic_file_aio_read,
	.aio_write = cifs_file_aio_write,
	.open = cifs_open,
	.release = cifs_close,
	.lock = cifs_lock,
	.fsync = cifs_fsync,
	.flush = cifs_flush,
	.mmap  = cifs_file_mmap,
	.splice_read = generic_file_splice_read,
	.llseek = cifs_llseek,
#ifdef CONFIG_CIFS_POSIX
	.ioctl	= cifs_ioctl,
#endif /* CONFIG_CIFS_POSIX */

#ifdef CONFIG_CIFS_EXPERIMENTAL
	.dir_notify = cifs_dir_notify,
#endif /* CONFIG_CIFS_EXPERIMENTAL */
};

const struct file_operations cifs_file_direct_ops = {
	/* no mmap, no aio, no readv -
	   BB reevaluate whether they can be done with directio, no cache */
	.read = cifs_user_read,
	.write = cifs_user_write,
	.open = cifs_open,
	.release = cifs_close,
	.lock = cifs_lock,
	.fsync = cifs_fsync,
	.flush = cifs_flush,
	.splice_read = generic_file_splice_read,
#ifdef CONFIG_CIFS_POSIX
	.ioctl  = cifs_ioctl,
#endif /* CONFIG_CIFS_POSIX */
	.llseek = cifs_llseek,
#ifdef CONFIG_CIFS_EXPERIMENTAL
	.dir_notify = cifs_dir_notify,
#endif /* CONFIG_CIFS_EXPERIMENTAL */
};
const struct file_operations cifs_file_nobrl_ops = {
	.read = do_sync_read,
	.write = do_sync_write,
	.aio_read = generic_file_aio_read,
	.aio_write = cifs_file_aio_write,
	.open = cifs_open,
	.release = cifs_close,
	.fsync = cifs_fsync,
	.flush = cifs_flush,
	.mmap  = cifs_file_mmap,
	.splice_read = generic_file_splice_read,
	.llseek = cifs_llseek,
#ifdef CONFIG_CIFS_POSIX
	.ioctl	= cifs_ioctl,
#endif /* CONFIG_CIFS_POSIX */

#ifdef CONFIG_CIFS_EXPERIMENTAL
	.dir_notify = cifs_dir_notify,
#endif /* CONFIG_CIFS_EXPERIMENTAL */
};

const struct file_operations cifs_file_direct_nobrl_ops = {
	/* no mmap, no aio, no readv -
	   BB reevaluate whether they can be done with directio, no cache */
	.read = cifs_user_read,
	.write = cifs_user_write,
	.open = cifs_open,
	.release = cifs_close,
	.fsync = cifs_fsync,
	.flush = cifs_flush,
	.splice_read = generic_file_splice_read,
#ifdef CONFIG_CIFS_POSIX
	.ioctl  = cifs_ioctl,
#endif /* CONFIG_CIFS_POSIX */
	.llseek = cifs_llseek,
#ifdef CONFIG_CIFS_EXPERIMENTAL
	.dir_notify = cifs_dir_notify,
#endif /* CONFIG_CIFS_EXPERIMENTAL */
};

const struct file_operations cifs_dir_ops = {
	.readdir = cifs_readdir,
	.release = cifs_closedir,
	.read    = generic_read_dir,
#ifdef CONFIG_CIFS_EXPERIMENTAL
	.dir_notify = cifs_dir_notify,
#endif /* CONFIG_CIFS_EXPERIMENTAL */
	.ioctl  = cifs_ioctl,
};

static void
cifs_init_once(struct kmem_cache *cachep, void *inode)
{
	struct cifsInodeInfo *cifsi = inode;

	inode_init_once(&cifsi->vfs_inode);
	INIT_LIST_HEAD(&cifsi->lockList);
}

static int
cifs_init_inodecache(void)
{
	cifs_inode_cachep = kmem_cache_create("cifs_inode_cache",
					      sizeof(struct cifsInodeInfo),
					      0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					      cifs_init_once);
	if (cifs_inode_cachep == NULL)
		return -ENOMEM;

	return 0;
}

static void
cifs_destroy_inodecache(void)
{
	kmem_cache_destroy(cifs_inode_cachep);
}

static int
cifs_init_request_bufs(void)
{
	if (CIFSMaxBufSize < 8192) {
	/* Buffer size can not be smaller than 2 * PATH_MAX since maximum
	Unicode path name has to fit in any SMB/CIFS path based frames */
		CIFSMaxBufSize = 8192;
	} else if (CIFSMaxBufSize > 1024*127) {
		CIFSMaxBufSize = 1024 * 127;
	} else {
		CIFSMaxBufSize &= 0x1FE00; /* Round size to even 512 byte mult*/
	}
/*	cERROR(1,("CIFSMaxBufSize %d 0x%x",CIFSMaxBufSize,CIFSMaxBufSize)); */
	cifs_req_cachep = kmem_cache_create("cifs_request",
					    CIFSMaxBufSize +
					    MAX_CIFS_HDR_SIZE, 0,
					    SLAB_HWCACHE_ALIGN, NULL);
	if (cifs_req_cachep == NULL)
		return -ENOMEM;

	if (cifs_min_rcv < 1)
		cifs_min_rcv = 1;
	else if (cifs_min_rcv > 64) {
		cifs_min_rcv = 64;
		cERROR(1, ("cifs_min_rcv set to maximum (64)"));
	}

	cifs_req_poolp = mempool_create_slab_pool(cifs_min_rcv,
						  cifs_req_cachep);

	if (cifs_req_poolp == NULL) {
		kmem_cache_destroy(cifs_req_cachep);
		return -ENOMEM;
	}
	/* MAX_CIFS_SMALL_BUFFER_SIZE bytes is enough for most SMB responses and
	almost all handle based requests (but not write response, nor is it
	sufficient for path based requests).  A smaller size would have
	been more efficient (compacting multiple slab items on one 4k page)
	for the case in which debug was on, but this larger size allows
	more SMBs to use small buffer alloc and is still much more
	efficient to alloc 1 per page off the slab compared to 17K (5page)
	alloc of large cifs buffers even when page debugging is on */
	cifs_sm_req_cachep = kmem_cache_create("cifs_small_rq",
			MAX_CIFS_SMALL_BUFFER_SIZE, 0, SLAB_HWCACHE_ALIGN,
			NULL);
	if (cifs_sm_req_cachep == NULL) {
		mempool_destroy(cifs_req_poolp);
		kmem_cache_destroy(cifs_req_cachep);
		return -ENOMEM;
	}

	if (cifs_min_small < 2)
		cifs_min_small = 2;
	else if (cifs_min_small > 256) {
		cifs_min_small = 256;
		cFYI(1, ("cifs_min_small set to maximum (256)"));
	}

	cifs_sm_req_poolp = mempool_create_slab_pool(cifs_min_small,
						     cifs_sm_req_cachep);

	if (cifs_sm_req_poolp == NULL) {
		mempool_destroy(cifs_req_poolp);
		kmem_cache_destroy(cifs_req_cachep);
		kmem_cache_destroy(cifs_sm_req_cachep);
		return -ENOMEM;
	}

	return 0;
}

static void
cifs_destroy_request_bufs(void)
{
	mempool_destroy(cifs_req_poolp);
	kmem_cache_destroy(cifs_req_cachep);
	mempool_destroy(cifs_sm_req_poolp);
	kmem_cache_destroy(cifs_sm_req_cachep);
}

static int
cifs_init_mids(void)
{
	cifs_mid_cachep = kmem_cache_create("cifs_mpx_ids",
					    sizeof(struct mid_q_entry), 0,
					    SLAB_HWCACHE_ALIGN, NULL);
	if (cifs_mid_cachep == NULL)
		return -ENOMEM;

	/* 3 is a reasonable minimum number of simultaneous operations */
	cifs_mid_poolp = mempool_create_slab_pool(3, cifs_mid_cachep);
	if (cifs_mid_poolp == NULL) {
		kmem_cache_destroy(cifs_mid_cachep);
		return -ENOMEM;
	}

	cifs_oplock_cachep = kmem_cache_create("cifs_oplock_structs",
					sizeof(struct oplock_q_entry), 0,
					SLAB_HWCACHE_ALIGN, NULL);
	if (cifs_oplock_cachep == NULL) {
		mempool_destroy(cifs_mid_poolp);
		kmem_cache_destroy(cifs_mid_cachep);
		return -ENOMEM;
	}

	return 0;
}

static void
cifs_destroy_mids(void)
{
	mempool_destroy(cifs_mid_poolp);
	kmem_cache_destroy(cifs_mid_cachep);
	kmem_cache_destroy(cifs_oplock_cachep);
}

static int cifs_oplock_thread(void *dummyarg)
{
	struct oplock_q_entry *oplock_item;
	struct cifsTconInfo *pTcon;
	struct inode *inode;
	__u16  netfid;
	int rc, waitrc = 0;

	set_freezable();
	do {
		if (try_to_freeze())
			continue;

		spin_lock(&GlobalMid_Lock);
		if (list_empty(&GlobalOplock_Q)) {
			spin_unlock(&GlobalMid_Lock);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(39*HZ);
		} else {
			oplock_item = list_entry(GlobalOplock_Q.next,
				struct oplock_q_entry, qhead);
			if (oplock_item) {
				cFYI(1, ("found oplock item to write out"));
				pTcon = oplock_item->tcon;
				inode = oplock_item->pinode;
				netfid = oplock_item->netfid;
				spin_unlock(&GlobalMid_Lock);
				DeleteOplockQEntry(oplock_item);
				/* can not grab inode sem here since it would
				deadlock when oplock received on delete
				since vfs_unlink holds the i_mutex across
				the call */
				/* mutex_lock(&inode->i_mutex);*/
				if (S_ISREG(inode->i_mode)) {
					rc =
					   filemap_fdatawrite(inode->i_mapping);
					if (CIFS_I(inode)->clientCanCacheRead
									 == 0) {
						waitrc = filemap_fdatawait(inode->i_mapping);
						invalidate_remote_inode(inode);
					}
					if (rc == 0)
						rc = waitrc;
				} else
					rc = 0;
				/* mutex_unlock(&inode->i_mutex);*/
				if (rc)
					CIFS_I(inode)->write_behind_rc = rc;
				cFYI(1, ("Oplock flush inode %p rc %d",
					inode, rc));

				/* releasing stale oplock after recent reconnect
				of smb session using a now incorrect file
				handle is not a data integrity issue but do
				not bother sending an oplock release if session
				to server still is disconnected since oplock
				already released by the server in that case */
				if (pTcon->tidStatus != CifsNeedReconnect) {
				    rc = CIFSSMBLock(0, pTcon, netfid,
					    0 /* len */ , 0 /* offset */, 0,
					    0, LOCKING_ANDX_OPLOCK_RELEASE,
					    0 /* wait flag */);
					cFYI(1, ("Oplock release rc = %d", rc));
				}
			} else
				spin_unlock(&GlobalMid_Lock);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(1);  /* yield in case q were corrupt */
		}
	} while (!kthread_should_stop());

	return 0;
}

static int cifs_dnotify_thread(void *dummyarg)
{
	struct list_head *tmp;
	struct cifsSesInfo *ses;

	do {
		if (try_to_freeze())
			continue;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(15*HZ);
		read_lock(&GlobalSMBSeslock);
		/* check if any stuck requests that need
		   to be woken up and wakeq so the
		   thread can wake up and error out */
		list_for_each(tmp, &GlobalSMBSessionList) {
			ses = list_entry(tmp, struct cifsSesInfo,
				cifsSessionList);
			if (ses && ses->server &&
			     atomic_read(&ses->server->inFlight))
				wake_up_all(&ses->server->response_q);
		}
		read_unlock(&GlobalSMBSeslock);
	} while (!kthread_should_stop());

	return 0;
}

static int __init
init_cifs(void)
{
	int rc = 0;
	cifs_proc_init();
/*	INIT_LIST_HEAD(&GlobalServerList);*/	/* BB not implemented yet */
	INIT_LIST_HEAD(&GlobalSMBSessionList);
	INIT_LIST_HEAD(&GlobalTreeConnectionList);
	INIT_LIST_HEAD(&GlobalOplock_Q);
#ifdef CONFIG_CIFS_EXPERIMENTAL
	INIT_LIST_HEAD(&GlobalDnotifyReqList);
	INIT_LIST_HEAD(&GlobalDnotifyRsp_Q);
#endif
/*
 *  Initialize Global counters
 */
	atomic_set(&sesInfoAllocCount, 0);
	atomic_set(&tconInfoAllocCount, 0);
	atomic_set(&tcpSesAllocCount, 0);
	atomic_set(&tcpSesReconnectCount, 0);
	atomic_set(&tconInfoReconnectCount, 0);

	atomic_set(&bufAllocCount, 0);
	atomic_set(&smBufAllocCount, 0);
#ifdef CONFIG_CIFS_STATS2
	atomic_set(&totBufAllocCount, 0);
	atomic_set(&totSmBufAllocCount, 0);
#endif /* CONFIG_CIFS_STATS2 */

	atomic_set(&midCount, 0);
	GlobalCurrentXid = 0;
	GlobalTotalActiveXid = 0;
	GlobalMaxActiveXid = 0;
	memset(Local_System_Name, 0, 15);
	rwlock_init(&GlobalSMBSeslock);
	spin_lock_init(&GlobalMid_Lock);

	if (cifs_max_pending < 2) {
		cifs_max_pending = 2;
		cFYI(1, ("cifs_max_pending set to min of 2"));
	} else if (cifs_max_pending > 256) {
		cifs_max_pending = 256;
		cFYI(1, ("cifs_max_pending set to max of 256"));
	}

	rc = cifs_init_inodecache();
	if (rc)
		goto out_clean_proc;

	rc = cifs_init_mids();
	if (rc)
		goto out_destroy_inodecache;

	rc = cifs_init_request_bufs();
	if (rc)
		goto out_destroy_mids;

	rc = register_filesystem(&cifs_fs_type);
	if (rc)
		goto out_destroy_request_bufs;
#ifdef CONFIG_CIFS_UPCALL
	rc = register_key_type(&cifs_spnego_key_type);
	if (rc)
		goto out_unregister_filesystem;
#endif
#ifdef CONFIG_CIFS_DFS_UPCALL
	rc = register_key_type(&key_type_dns_resolver);
	if (rc)
		goto out_unregister_key_type;
#endif
	oplockThread = kthread_run(cifs_oplock_thread, NULL, "cifsoplockd");
	if (IS_ERR(oplockThread)) {
		rc = PTR_ERR(oplockThread);
		cERROR(1, ("error %d create oplock thread", rc));
		goto out_unregister_dfs_key_type;
	}

	dnotifyThread = kthread_run(cifs_dnotify_thread, NULL, "cifsdnotifyd");
	if (IS_ERR(dnotifyThread)) {
		rc = PTR_ERR(dnotifyThread);
		cERROR(1, ("error %d create dnotify thread", rc));
		goto out_stop_oplock_thread;
	}

	return 0;

 out_stop_oplock_thread:
	kthread_stop(oplockThread);
 out_unregister_dfs_key_type:
#ifdef CONFIG_CIFS_DFS_UPCALL
	unregister_key_type(&key_type_dns_resolver);
 out_unregister_key_type:
#endif
#ifdef CONFIG_CIFS_UPCALL
	unregister_key_type(&cifs_spnego_key_type);
 out_unregister_filesystem:
#endif
	unregister_filesystem(&cifs_fs_type);
 out_destroy_request_bufs:
	cifs_destroy_request_bufs();
 out_destroy_mids:
	cifs_destroy_mids();
 out_destroy_inodecache:
	cifs_destroy_inodecache();
 out_clean_proc:
	cifs_proc_clean();
	return rc;
}

static void __exit
exit_cifs(void)
{
	cFYI(DBG2, ("exit_cifs"));
	cifs_proc_clean();
#ifdef CONFIG_CIFS_DFS_UPCALL
	unregister_key_type(&key_type_dns_resolver);
#endif
#ifdef CONFIG_CIFS_UPCALL
	unregister_key_type(&cifs_spnego_key_type);
#endif
	unregister_filesystem(&cifs_fs_type);
	cifs_destroy_inodecache();
	cifs_destroy_mids();
	cifs_destroy_request_bufs();
	kthread_stop(oplockThread);
	kthread_stop(dnotifyThread);
}

MODULE_AUTHOR("Steve French <sfrench@us.ibm.com>");
MODULE_LICENSE("GPL");	/* combination of LGPL + GPL source behaves as GPL */
MODULE_DESCRIPTION
    ("VFS to access servers complying with the SNIA CIFS Specification "
     "e.g. Samba and Windows");
MODULE_VERSION(CIFS_VERSION);
module_init(init_cifs)
module_exit(exit_cifs)
