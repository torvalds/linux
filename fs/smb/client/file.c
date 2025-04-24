// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   vfs operations that deal with files
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2010
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Jeremy Allison (jra@samba.org)
 *
 */
#include <linux/fs.h>
#include <linux/filelock.h>
#include <linux/backing-dev.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/writeback.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/delay.h>
#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/mm.h>
#include <asm/div64.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "smb2proto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "fscache.h"
#include "smbdirect.h"
#include "fs_context.h"
#include "cifs_ioctl.h"
#include "cached_dir.h"
#include <trace/events/netfs.h>

static int cifs_reopen_file(struct cifsFileInfo *cfile, bool can_flush);

/*
 * Prepare a subrequest to upload to the server.  We need to allocate credits
 * so that we know the maximum amount of data that we can include in it.
 */
static void cifs_prepare_write(struct netfs_io_subrequest *subreq)
{
	struct cifs_io_subrequest *wdata =
		container_of(subreq, struct cifs_io_subrequest, subreq);
	struct cifs_io_request *req = wdata->req;
	struct netfs_io_stream *stream = &req->rreq.io_streams[subreq->stream_nr];
	struct TCP_Server_Info *server;
	struct cifsFileInfo *open_file = req->cfile;
	size_t wsize = req->rreq.wsize;
	int rc;

	if (!wdata->have_xid) {
		wdata->xid = get_xid();
		wdata->have_xid = true;
	}

	server = cifs_pick_channel(tlink_tcon(open_file->tlink)->ses);
	wdata->server = server;

retry:
	if (open_file->invalidHandle) {
		rc = cifs_reopen_file(open_file, false);
		if (rc < 0) {
			if (rc == -EAGAIN)
				goto retry;
			subreq->error = rc;
			return netfs_prepare_write_failed(subreq);
		}
	}

	rc = server->ops->wait_mtu_credits(server, wsize, &stream->sreq_max_len,
					   &wdata->credits);
	if (rc < 0) {
		subreq->error = rc;
		return netfs_prepare_write_failed(subreq);
	}

	wdata->credits.rreq_debug_id = subreq->rreq->debug_id;
	wdata->credits.rreq_debug_index = subreq->debug_index;
	wdata->credits.in_flight_check = 1;
	trace_smb3_rw_credits(wdata->rreq->debug_id,
			      wdata->subreq.debug_index,
			      wdata->credits.value,
			      server->credits, server->in_flight,
			      wdata->credits.value,
			      cifs_trace_rw_credits_write_prepare);

#ifdef CONFIG_CIFS_SMB_DIRECT
	if (server->smbd_conn)
		stream->sreq_max_segs = server->smbd_conn->max_frmr_depth;
#endif
}

/*
 * Issue a subrequest to upload to the server.
 */
static void cifs_issue_write(struct netfs_io_subrequest *subreq)
{
	struct cifs_io_subrequest *wdata =
		container_of(subreq, struct cifs_io_subrequest, subreq);
	struct cifs_sb_info *sbi = CIFS_SB(subreq->rreq->inode->i_sb);
	int rc;

	if (cifs_forced_shutdown(sbi)) {
		rc = -EIO;
		goto fail;
	}

	rc = adjust_credits(wdata->server, wdata, cifs_trace_rw_credits_issue_write_adjust);
	if (rc)
		goto fail;

	rc = -EAGAIN;
	if (wdata->req->cfile->invalidHandle)
		goto fail;

	wdata->server->ops->async_writev(wdata);
out:
	return;

fail:
	if (rc == -EAGAIN)
		trace_netfs_sreq(subreq, netfs_sreq_trace_retry);
	else
		trace_netfs_sreq(subreq, netfs_sreq_trace_fail);
	add_credits_and_wake_if(wdata->server, &wdata->credits, 0);
	cifs_write_subrequest_terminated(wdata, rc, false);
	goto out;
}

static void cifs_netfs_invalidate_cache(struct netfs_io_request *wreq)
{
	cifs_invalidate_cache(wreq->inode, 0);
}

/*
 * Negotiate the size of a read operation on behalf of the netfs library.
 */
static int cifs_prepare_read(struct netfs_io_subrequest *subreq)
{
	struct netfs_io_request *rreq = subreq->rreq;
	struct cifs_io_subrequest *rdata = container_of(subreq, struct cifs_io_subrequest, subreq);
	struct cifs_io_request *req = container_of(subreq->rreq, struct cifs_io_request, rreq);
	struct TCP_Server_Info *server;
	struct cifs_sb_info *cifs_sb = CIFS_SB(rreq->inode->i_sb);
	size_t size;
	int rc = 0;

	if (!rdata->have_xid) {
		rdata->xid = get_xid();
		rdata->have_xid = true;
	}

	server = cifs_pick_channel(tlink_tcon(req->cfile->tlink)->ses);
	rdata->server = server;

	if (cifs_sb->ctx->rsize == 0)
		cifs_sb->ctx->rsize =
			server->ops->negotiate_rsize(tlink_tcon(req->cfile->tlink),
						     cifs_sb->ctx);

	rc = server->ops->wait_mtu_credits(server, cifs_sb->ctx->rsize,
					   &size, &rdata->credits);
	if (rc)
		return rc;

	rreq->io_streams[0].sreq_max_len = size;

	rdata->credits.in_flight_check = 1;
	rdata->credits.rreq_debug_id = rreq->debug_id;
	rdata->credits.rreq_debug_index = subreq->debug_index;

	trace_smb3_rw_credits(rdata->rreq->debug_id,
			      rdata->subreq.debug_index,
			      rdata->credits.value,
			      server->credits, server->in_flight, 0,
			      cifs_trace_rw_credits_read_submit);

#ifdef CONFIG_CIFS_SMB_DIRECT
	if (server->smbd_conn)
		rreq->io_streams[0].sreq_max_segs = server->smbd_conn->max_frmr_depth;
#endif
	return 0;
}

/*
 * Issue a read operation on behalf of the netfs helper functions.  We're asked
 * to make a read of a certain size at a point in the file.  We are permitted
 * to only read a portion of that, but as long as we read something, the netfs
 * helper will call us again so that we can issue another read.
 */
static void cifs_issue_read(struct netfs_io_subrequest *subreq)
{
	struct netfs_io_request *rreq = subreq->rreq;
	struct cifs_io_subrequest *rdata = container_of(subreq, struct cifs_io_subrequest, subreq);
	struct cifs_io_request *req = container_of(subreq->rreq, struct cifs_io_request, rreq);
	struct TCP_Server_Info *server = rdata->server;
	int rc = 0;

	cifs_dbg(FYI, "%s: op=%08x[%x] mapping=%p len=%zu/%zu\n",
		 __func__, rreq->debug_id, subreq->debug_index, rreq->mapping,
		 subreq->transferred, subreq->len);

	rc = adjust_credits(server, rdata, cifs_trace_rw_credits_issue_read_adjust);
	if (rc)
		goto failed;

	if (req->cfile->invalidHandle) {
		do {
			rc = cifs_reopen_file(req->cfile, true);
		} while (rc == -EAGAIN);
		if (rc)
			goto failed;
	}

	if (subreq->rreq->origin != NETFS_DIO_READ)
		__set_bit(NETFS_SREQ_CLEAR_TAIL, &subreq->flags);

	trace_netfs_sreq(subreq, netfs_sreq_trace_submit);
	rc = rdata->server->ops->async_readv(rdata);
	if (rc)
		goto failed;
	return;

failed:
	subreq->error = rc;
	netfs_read_subreq_terminated(subreq);
}

/*
 * Writeback calls this when it finds a folio that needs uploading.  This isn't
 * called if writeback only has copy-to-cache to deal with.
 */
static void cifs_begin_writeback(struct netfs_io_request *wreq)
{
	struct cifs_io_request *req = container_of(wreq, struct cifs_io_request, rreq);
	int ret;

	ret = cifs_get_writable_file(CIFS_I(wreq->inode), FIND_WR_ANY, &req->cfile);
	if (ret) {
		cifs_dbg(VFS, "No writable handle in writepages ret=%d\n", ret);
		return;
	}

	wreq->io_streams[0].avail = true;
}

/*
 * Initialise a request.
 */
static int cifs_init_request(struct netfs_io_request *rreq, struct file *file)
{
	struct cifs_io_request *req = container_of(rreq, struct cifs_io_request, rreq);
	struct cifs_sb_info *cifs_sb = CIFS_SB(rreq->inode->i_sb);
	struct cifsFileInfo *open_file = NULL;

	rreq->rsize = cifs_sb->ctx->rsize;
	rreq->wsize = cifs_sb->ctx->wsize;
	req->pid = current->tgid; // Ummm...  This may be a workqueue

	if (file) {
		open_file = file->private_data;
		rreq->netfs_priv = file->private_data;
		req->cfile = cifsFileInfo_get(open_file);
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_RWPIDFORWARD)
			req->pid = req->cfile->pid;
	} else if (rreq->origin != NETFS_WRITEBACK) {
		WARN_ON_ONCE(1);
		return -EIO;
	}

	return 0;
}

/*
 * Completion of a request operation.
 */
static void cifs_rreq_done(struct netfs_io_request *rreq)
{
	struct timespec64 atime, mtime;
	struct inode *inode = rreq->inode;

	/* we do not want atime to be less than mtime, it broke some apps */
	atime = inode_set_atime_to_ts(inode, current_time(inode));
	mtime = inode_get_mtime(inode);
	if (timespec64_compare(&atime, &mtime))
		inode_set_atime_to_ts(inode, inode_get_mtime(inode));
}

static void cifs_free_request(struct netfs_io_request *rreq)
{
	struct cifs_io_request *req = container_of(rreq, struct cifs_io_request, rreq);

	if (req->cfile)
		cifsFileInfo_put(req->cfile);
}

static void cifs_free_subrequest(struct netfs_io_subrequest *subreq)
{
	struct cifs_io_subrequest *rdata =
		container_of(subreq, struct cifs_io_subrequest, subreq);
	int rc = subreq->error;

	if (rdata->subreq.source == NETFS_DOWNLOAD_FROM_SERVER) {
#ifdef CONFIG_CIFS_SMB_DIRECT
		if (rdata->mr) {
			smbd_deregister_mr(rdata->mr);
			rdata->mr = NULL;
		}
#endif
	}

	if (rdata->credits.value != 0) {
		trace_smb3_rw_credits(rdata->rreq->debug_id,
				      rdata->subreq.debug_index,
				      rdata->credits.value,
				      rdata->server ? rdata->server->credits : 0,
				      rdata->server ? rdata->server->in_flight : 0,
				      -rdata->credits.value,
				      cifs_trace_rw_credits_free_subreq);
		if (rdata->server)
			add_credits_and_wake_if(rdata->server, &rdata->credits, 0);
		else
			rdata->credits.value = 0;
	}

	if (rdata->have_xid)
		free_xid(rdata->xid);
}

const struct netfs_request_ops cifs_req_ops = {
	.request_pool		= &cifs_io_request_pool,
	.subrequest_pool	= &cifs_io_subrequest_pool,
	.init_request		= cifs_init_request,
	.free_request		= cifs_free_request,
	.free_subrequest	= cifs_free_subrequest,
	.prepare_read		= cifs_prepare_read,
	.issue_read		= cifs_issue_read,
	.done			= cifs_rreq_done,
	.begin_writeback	= cifs_begin_writeback,
	.prepare_write		= cifs_prepare_write,
	.issue_write		= cifs_issue_write,
	.invalidate_cache	= cifs_netfs_invalidate_cache,
};

/*
 * Mark as invalid, all open files on tree connections since they
 * were closed when session to server was lost.
 */
void
cifs_mark_open_files_invalid(struct cifs_tcon *tcon)
{
	struct cifsFileInfo *open_file = NULL;
	struct list_head *tmp;
	struct list_head *tmp1;

	/* only send once per connect */
	spin_lock(&tcon->tc_lock);
	if (tcon->need_reconnect)
		tcon->status = TID_NEED_RECON;

	if (tcon->status != TID_NEED_RECON) {
		spin_unlock(&tcon->tc_lock);
		return;
	}
	tcon->status = TID_IN_FILES_INVALIDATE;
	spin_unlock(&tcon->tc_lock);

	/* list all files open on tree connection and mark them invalid */
	spin_lock(&tcon->open_file_lock);
	list_for_each_safe(tmp, tmp1, &tcon->openFileList) {
		open_file = list_entry(tmp, struct cifsFileInfo, tlist);
		open_file->invalidHandle = true;
		open_file->oplock_break_cancelled = true;
	}
	spin_unlock(&tcon->open_file_lock);

	invalidate_all_cached_dirs(tcon);
	spin_lock(&tcon->tc_lock);
	if (tcon->status == TID_IN_FILES_INVALIDATE)
		tcon->status = TID_NEED_TCON;
	spin_unlock(&tcon->tc_lock);

	/*
	 * BB Add call to evict_inodes(sb) for all superblocks mounted
	 * to this tcon.
	 */
}

static inline int cifs_convert_flags(unsigned int flags, int rdwr_for_fscache)
{
	if ((flags & O_ACCMODE) == O_RDONLY)
		return GENERIC_READ;
	else if ((flags & O_ACCMODE) == O_WRONLY)
		return rdwr_for_fscache == 1 ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_WRITE;
	else if ((flags & O_ACCMODE) == O_RDWR) {
		/* GENERIC_ALL is too much permission to request
		   can cause unnecessary access denied on create */
		/* return GENERIC_ALL; */
		return (GENERIC_READ | GENERIC_WRITE);
	}

	return (READ_CONTROL | FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES |
		FILE_WRITE_EA | FILE_APPEND_DATA | FILE_WRITE_DATA |
		FILE_READ_DATA);
}

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
static u32 cifs_posix_convert_flags(unsigned int flags)
{
	u32 posix_flags = 0;

	if ((flags & O_ACCMODE) == O_RDONLY)
		posix_flags = SMB_O_RDONLY;
	else if ((flags & O_ACCMODE) == O_WRONLY)
		posix_flags = SMB_O_WRONLY;
	else if ((flags & O_ACCMODE) == O_RDWR)
		posix_flags = SMB_O_RDWR;

	if (flags & O_CREAT) {
		posix_flags |= SMB_O_CREAT;
		if (flags & O_EXCL)
			posix_flags |= SMB_O_EXCL;
	} else if (flags & O_EXCL)
		cifs_dbg(FYI, "Application %s pid %d has incorrectly set O_EXCL flag but not O_CREAT on file open. Ignoring O_EXCL\n",
			 current->comm, current->tgid);

	if (flags & O_TRUNC)
		posix_flags |= SMB_O_TRUNC;
	/* be safe and imply O_SYNC for O_DSYNC */
	if (flags & O_DSYNC)
		posix_flags |= SMB_O_SYNC;
	if (flags & O_DIRECTORY)
		posix_flags |= SMB_O_DIRECTORY;
	if (flags & O_NOFOLLOW)
		posix_flags |= SMB_O_NOFOLLOW;
	if (flags & O_DIRECT)
		posix_flags |= SMB_O_DIRECT;

	return posix_flags;
}
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */

static inline int cifs_get_disposition(unsigned int flags)
{
	if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
		return FILE_CREATE;
	else if ((flags & (O_CREAT | O_TRUNC)) == (O_CREAT | O_TRUNC))
		return FILE_OVERWRITE_IF;
	else if ((flags & O_CREAT) == O_CREAT)
		return FILE_OPEN_IF;
	else if ((flags & O_TRUNC) == O_TRUNC)
		return FILE_OVERWRITE;
	else
		return FILE_OPEN;
}

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
int cifs_posix_open(const char *full_path, struct inode **pinode,
			struct super_block *sb, int mode, unsigned int f_flags,
			__u32 *poplock, __u16 *pnetfid, unsigned int xid)
{
	int rc;
	FILE_UNIX_BASIC_INFO *presp_data;
	__u32 posix_flags = 0;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifs_fattr fattr;
	struct tcon_link *tlink;
	struct cifs_tcon *tcon;

	cifs_dbg(FYI, "posix open %s\n", full_path);

	presp_data = kzalloc(sizeof(FILE_UNIX_BASIC_INFO), GFP_KERNEL);
	if (presp_data == NULL)
		return -ENOMEM;

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink)) {
		rc = PTR_ERR(tlink);
		goto posix_open_ret;
	}

	tcon = tlink_tcon(tlink);
	mode &= ~current_umask();

	posix_flags = cifs_posix_convert_flags(f_flags);
	rc = CIFSPOSIXCreate(xid, tcon, posix_flags, mode, pnetfid, presp_data,
			     poplock, full_path, cifs_sb->local_nls,
			     cifs_remap(cifs_sb));
	cifs_put_tlink(tlink);

	if (rc)
		goto posix_open_ret;

	if (presp_data->Type == cpu_to_le32(-1))
		goto posix_open_ret; /* open ok, caller does qpathinfo */

	if (!pinode)
		goto posix_open_ret; /* caller does not need info */

	cifs_unix_basic_to_fattr(&fattr, presp_data, cifs_sb);

	/* get new inode and set it up */
	if (*pinode == NULL) {
		cifs_fill_uniqueid(sb, &fattr);
		*pinode = cifs_iget(sb, &fattr);
		if (!*pinode) {
			rc = -ENOMEM;
			goto posix_open_ret;
		}
	} else {
		cifs_revalidate_mapping(*pinode);
		rc = cifs_fattr_to_inode(*pinode, &fattr, false);
	}

posix_open_ret:
	kfree(presp_data);
	return rc;
}
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */

static int cifs_nt_open(const char *full_path, struct inode *inode, struct cifs_sb_info *cifs_sb,
			struct cifs_tcon *tcon, unsigned int f_flags, __u32 *oplock,
			struct cifs_fid *fid, unsigned int xid, struct cifs_open_info_data *buf)
{
	int rc;
	int desired_access;
	int disposition;
	int create_options = CREATE_NOT_DIR;
	struct TCP_Server_Info *server = tcon->ses->server;
	struct cifs_open_parms oparms;
	int rdwr_for_fscache = 0;

	if (!server->ops->open)
		return -ENOSYS;

	/* If we're caching, we need to be able to fill in around partial writes. */
	if (cifs_fscache_enabled(inode) && (f_flags & O_ACCMODE) == O_WRONLY)
		rdwr_for_fscache = 1;

	desired_access = cifs_convert_flags(f_flags, rdwr_for_fscache);

/*********************************************************************
 *  open flag mapping table:
 *
 *	POSIX Flag            CIFS Disposition
 *	----------            ----------------
 *	O_CREAT               FILE_OPEN_IF
 *	O_CREAT | O_EXCL      FILE_CREATE
 *	O_CREAT | O_TRUNC     FILE_OVERWRITE_IF
 *	O_TRUNC               FILE_OVERWRITE
 *	none of the above     FILE_OPEN
 *
 *	Note that there is not a direct match between disposition
 *	FILE_SUPERSEDE (ie create whether or not file exists although
 *	O_CREAT | O_TRUNC is similar but truncates the existing
 *	file rather than creating a new file as FILE_SUPERSEDE does
 *	(which uses the attributes / metadata passed in on open call)
 *?
 *?  O_SYNC is a reasonable match to CIFS writethrough flag
 *?  and the read write flags match reasonably.  O_LARGEFILE
 *?  is irrelevant because largefile support is always used
 *?  by this client. Flags O_APPEND, O_DIRECT, O_DIRECTORY,
 *	 O_FASYNC, O_NOFOLLOW, O_NONBLOCK need further investigation
 *********************************************************************/

	disposition = cifs_get_disposition(f_flags);

	/* BB pass O_SYNC flag through on file attributes .. BB */

	/* O_SYNC also has bit for O_DSYNC so following check picks up either */
	if (f_flags & O_SYNC)
		create_options |= CREATE_WRITE_THROUGH;

	if (f_flags & O_DIRECT)
		create_options |= CREATE_NO_BUFFER;

retry_open:
	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.cifs_sb = cifs_sb,
		.desired_access = desired_access,
		.create_options = cifs_create_options(cifs_sb, create_options),
		.disposition = disposition,
		.path = full_path,
		.fid = fid,
	};

	rc = server->ops->open(xid, &oparms, oplock, buf);
	if (rc) {
		if (rc == -EACCES && rdwr_for_fscache == 1) {
			desired_access = cifs_convert_flags(f_flags, 0);
			rdwr_for_fscache = 2;
			goto retry_open;
		}
		return rc;
	}
	if (rdwr_for_fscache == 2)
		cifs_invalidate_cache(inode, FSCACHE_INVAL_DIO_WRITE);

	/* TODO: Add support for calling posix query info but with passing in fid */
	if (tcon->unix_ext)
		rc = cifs_get_inode_info_unix(&inode, full_path, inode->i_sb,
					      xid);
	else
		rc = cifs_get_inode_info(&inode, full_path, buf, inode->i_sb,
					 xid, fid);

	if (rc) {
		server->ops->close(xid, tcon, fid);
		if (rc == -ESTALE)
			rc = -EOPENSTALE;
	}

	return rc;
}

static bool
cifs_has_mand_locks(struct cifsInodeInfo *cinode)
{
	struct cifs_fid_locks *cur;
	bool has_locks = false;

	down_read(&cinode->lock_sem);
	list_for_each_entry(cur, &cinode->llist, llist) {
		if (!list_empty(&cur->locks)) {
			has_locks = true;
			break;
		}
	}
	up_read(&cinode->lock_sem);
	return has_locks;
}

void
cifs_down_write(struct rw_semaphore *sem)
{
	while (!down_write_trylock(sem))
		msleep(10);
}

static void cifsFileInfo_put_work(struct work_struct *work);
void serverclose_work(struct work_struct *work);

struct cifsFileInfo *cifs_new_fileinfo(struct cifs_fid *fid, struct file *file,
				       struct tcon_link *tlink, __u32 oplock,
				       const char *symlink_target)
{
	struct dentry *dentry = file_dentry(file);
	struct inode *inode = d_inode(dentry);
	struct cifsInodeInfo *cinode = CIFS_I(inode);
	struct cifsFileInfo *cfile;
	struct cifs_fid_locks *fdlocks;
	struct cifs_tcon *tcon = tlink_tcon(tlink);
	struct TCP_Server_Info *server = tcon->ses->server;

	cfile = kzalloc(sizeof(struct cifsFileInfo), GFP_KERNEL);
	if (cfile == NULL)
		return cfile;

	fdlocks = kzalloc(sizeof(struct cifs_fid_locks), GFP_KERNEL);
	if (!fdlocks) {
		kfree(cfile);
		return NULL;
	}

	if (symlink_target) {
		cfile->symlink_target = kstrdup(symlink_target, GFP_KERNEL);
		if (!cfile->symlink_target) {
			kfree(fdlocks);
			kfree(cfile);
			return NULL;
		}
	}

	INIT_LIST_HEAD(&fdlocks->locks);
	fdlocks->cfile = cfile;
	cfile->llist = fdlocks;

	cfile->count = 1;
	cfile->pid = current->tgid;
	cfile->uid = current_fsuid();
	cfile->dentry = dget(dentry);
	cfile->f_flags = file->f_flags;
	cfile->invalidHandle = false;
	cfile->deferred_close_scheduled = false;
	cfile->tlink = cifs_get_tlink(tlink);
	INIT_WORK(&cfile->oplock_break, cifs_oplock_break);
	INIT_WORK(&cfile->put, cifsFileInfo_put_work);
	INIT_WORK(&cfile->serverclose, serverclose_work);
	INIT_DELAYED_WORK(&cfile->deferred, smb2_deferred_work_close);
	mutex_init(&cfile->fh_mutex);
	spin_lock_init(&cfile->file_info_lock);

	cifs_sb_active(inode->i_sb);

	/*
	 * If the server returned a read oplock and we have mandatory brlocks,
	 * set oplock level to None.
	 */
	if (server->ops->is_read_op(oplock) && cifs_has_mand_locks(cinode)) {
		cifs_dbg(FYI, "Reset oplock val from read to None due to mand locks\n");
		oplock = 0;
	}

	cifs_down_write(&cinode->lock_sem);
	list_add(&fdlocks->llist, &cinode->llist);
	up_write(&cinode->lock_sem);

	spin_lock(&tcon->open_file_lock);
	if (fid->pending_open->oplock != CIFS_OPLOCK_NO_CHANGE && oplock)
		oplock = fid->pending_open->oplock;
	list_del(&fid->pending_open->olist);

	fid->purge_cache = false;
	server->ops->set_fid(cfile, fid, oplock);

	list_add(&cfile->tlist, &tcon->openFileList);
	atomic_inc(&tcon->num_local_opens);

	/* if readable file instance put first in list*/
	spin_lock(&cinode->open_file_lock);
	if (file->f_mode & FMODE_READ)
		list_add(&cfile->flist, &cinode->openFileList);
	else
		list_add_tail(&cfile->flist, &cinode->openFileList);
	spin_unlock(&cinode->open_file_lock);
	spin_unlock(&tcon->open_file_lock);

	if (fid->purge_cache)
		cifs_zap_mapping(inode);

	file->private_data = cfile;
	return cfile;
}

struct cifsFileInfo *
cifsFileInfo_get(struct cifsFileInfo *cifs_file)
{
	spin_lock(&cifs_file->file_info_lock);
	cifsFileInfo_get_locked(cifs_file);
	spin_unlock(&cifs_file->file_info_lock);
	return cifs_file;
}

static void cifsFileInfo_put_final(struct cifsFileInfo *cifs_file)
{
	struct inode *inode = d_inode(cifs_file->dentry);
	struct cifsInodeInfo *cifsi = CIFS_I(inode);
	struct cifsLockInfo *li, *tmp;
	struct super_block *sb = inode->i_sb;

	/*
	 * Delete any outstanding lock records. We'll lose them when the file
	 * is closed anyway.
	 */
	cifs_down_write(&cifsi->lock_sem);
	list_for_each_entry_safe(li, tmp, &cifs_file->llist->locks, llist) {
		list_del(&li->llist);
		cifs_del_lock_waiters(li);
		kfree(li);
	}
	list_del(&cifs_file->llist->llist);
	kfree(cifs_file->llist);
	up_write(&cifsi->lock_sem);

	cifs_put_tlink(cifs_file->tlink);
	dput(cifs_file->dentry);
	cifs_sb_deactive(sb);
	kfree(cifs_file->symlink_target);
	kfree(cifs_file);
}

static void cifsFileInfo_put_work(struct work_struct *work)
{
	struct cifsFileInfo *cifs_file = container_of(work,
			struct cifsFileInfo, put);

	cifsFileInfo_put_final(cifs_file);
}

void serverclose_work(struct work_struct *work)
{
	struct cifsFileInfo *cifs_file = container_of(work,
			struct cifsFileInfo, serverclose);

	struct cifs_tcon *tcon = tlink_tcon(cifs_file->tlink);

	struct TCP_Server_Info *server = tcon->ses->server;
	int rc = 0;
	int retries = 0;
	int MAX_RETRIES = 4;

	do {
		if (server->ops->close_getattr)
			rc = server->ops->close_getattr(0, tcon, cifs_file);
		else if (server->ops->close)
			rc = server->ops->close(0, tcon, &cifs_file->fid);

		if (rc == -EBUSY || rc == -EAGAIN) {
			retries++;
			msleep(250);
		}
	} while ((rc == -EBUSY || rc == -EAGAIN) && (retries < MAX_RETRIES)
	);

	if (retries == MAX_RETRIES)
		pr_warn("Serverclose failed %d times, giving up\n", MAX_RETRIES);

	if (cifs_file->offload)
		queue_work(fileinfo_put_wq, &cifs_file->put);
	else
		cifsFileInfo_put_final(cifs_file);
}

/**
 * cifsFileInfo_put - release a reference of file priv data
 *
 * Always potentially wait for oplock handler. See _cifsFileInfo_put().
 *
 * @cifs_file:	cifs/smb3 specific info (eg refcounts) for an open file
 */
void cifsFileInfo_put(struct cifsFileInfo *cifs_file)
{
	_cifsFileInfo_put(cifs_file, true, true);
}

/**
 * _cifsFileInfo_put - release a reference of file priv data
 *
 * This may involve closing the filehandle @cifs_file out on the
 * server. Must be called without holding tcon->open_file_lock,
 * cinode->open_file_lock and cifs_file->file_info_lock.
 *
 * If @wait_for_oplock_handler is true and we are releasing the last
 * reference, wait for any running oplock break handler of the file
 * and cancel any pending one.
 *
 * @cifs_file:	cifs/smb3 specific info (eg refcounts) for an open file
 * @wait_oplock_handler: must be false if called from oplock_break_handler
 * @offload:	not offloaded on close and oplock breaks
 *
 */
void _cifsFileInfo_put(struct cifsFileInfo *cifs_file,
		       bool wait_oplock_handler, bool offload)
{
	struct inode *inode = d_inode(cifs_file->dentry);
	struct cifs_tcon *tcon = tlink_tcon(cifs_file->tlink);
	struct TCP_Server_Info *server = tcon->ses->server;
	struct cifsInodeInfo *cifsi = CIFS_I(inode);
	struct super_block *sb = inode->i_sb;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifs_fid fid = {};
	struct cifs_pending_open open;
	bool oplock_break_cancelled;
	bool serverclose_offloaded = false;

	spin_lock(&tcon->open_file_lock);
	spin_lock(&cifsi->open_file_lock);
	spin_lock(&cifs_file->file_info_lock);

	cifs_file->offload = offload;
	if (--cifs_file->count > 0) {
		spin_unlock(&cifs_file->file_info_lock);
		spin_unlock(&cifsi->open_file_lock);
		spin_unlock(&tcon->open_file_lock);
		return;
	}
	spin_unlock(&cifs_file->file_info_lock);

	if (server->ops->get_lease_key)
		server->ops->get_lease_key(inode, &fid);

	/* store open in pending opens to make sure we don't miss lease break */
	cifs_add_pending_open_locked(&fid, cifs_file->tlink, &open);

	/* remove it from the lists */
	list_del(&cifs_file->flist);
	list_del(&cifs_file->tlist);
	atomic_dec(&tcon->num_local_opens);

	if (list_empty(&cifsi->openFileList)) {
		cifs_dbg(FYI, "closing last open instance for inode %p\n",
			 d_inode(cifs_file->dentry));
		/*
		 * In strict cache mode we need invalidate mapping on the last
		 * close  because it may cause a error when we open this file
		 * again and get at least level II oplock.
		 */
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_STRICT_IO)
			set_bit(CIFS_INO_INVALID_MAPPING, &cifsi->flags);
		cifs_set_oplock_level(cifsi, 0);
	}

	spin_unlock(&cifsi->open_file_lock);
	spin_unlock(&tcon->open_file_lock);

	oplock_break_cancelled = wait_oplock_handler ?
		cancel_work_sync(&cifs_file->oplock_break) : false;

	if (!tcon->need_reconnect && !cifs_file->invalidHandle) {
		struct TCP_Server_Info *server = tcon->ses->server;
		unsigned int xid;
		int rc = 0;

		xid = get_xid();
		if (server->ops->close_getattr)
			rc = server->ops->close_getattr(xid, tcon, cifs_file);
		else if (server->ops->close)
			rc = server->ops->close(xid, tcon, &cifs_file->fid);
		_free_xid(xid);

		if (rc == -EBUSY || rc == -EAGAIN) {
			// Server close failed, hence offloading it as an async op
			queue_work(serverclose_wq, &cifs_file->serverclose);
			serverclose_offloaded = true;
		}
	}

	if (oplock_break_cancelled)
		cifs_done_oplock_break(cifsi);

	cifs_del_pending_open(&open);

	// if serverclose has been offloaded to wq (on failure), it will
	// handle offloading put as well. If serverclose not offloaded,
	// we need to handle offloading put here.
	if (!serverclose_offloaded) {
		if (offload)
			queue_work(fileinfo_put_wq, &cifs_file->put);
		else
			cifsFileInfo_put_final(cifs_file);
	}
}

int cifs_open(struct inode *inode, struct file *file)

{
	int rc = -EACCES;
	unsigned int xid;
	__u32 oplock;
	struct cifs_sb_info *cifs_sb;
	struct TCP_Server_Info *server;
	struct cifs_tcon *tcon;
	struct tcon_link *tlink;
	struct cifsFileInfo *cfile = NULL;
	void *page;
	const char *full_path;
	bool posix_open_ok = false;
	struct cifs_fid fid = {};
	struct cifs_pending_open open;
	struct cifs_open_info_data data = {};

	xid = get_xid();

	cifs_sb = CIFS_SB(inode->i_sb);
	if (unlikely(cifs_forced_shutdown(cifs_sb))) {
		free_xid(xid);
		return -EIO;
	}

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink)) {
		free_xid(xid);
		return PTR_ERR(tlink);
	}
	tcon = tlink_tcon(tlink);
	server = tcon->ses->server;

	page = alloc_dentry_path();
	full_path = build_path_from_dentry(file_dentry(file), page);
	if (IS_ERR(full_path)) {
		rc = PTR_ERR(full_path);
		goto out;
	}

	cifs_dbg(FYI, "inode = 0x%p file flags are 0x%x for %s\n",
		 inode, file->f_flags, full_path);

	if (file->f_flags & O_DIRECT &&
	    cifs_sb->mnt_cifs_flags & CIFS_MOUNT_STRICT_IO) {
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
			file->f_op = &cifs_file_direct_nobrl_ops;
		else
			file->f_op = &cifs_file_direct_ops;
	}

	/* Get the cached handle as SMB2 close is deferred */
	if (OPEN_FMODE(file->f_flags) & FMODE_WRITE) {
		rc = cifs_get_writable_path(tcon, full_path, FIND_WR_FSUID_ONLY, &cfile);
	} else {
		rc = cifs_get_readable_path(tcon, full_path, &cfile);
	}
	if (rc == 0) {
		if (file->f_flags == cfile->f_flags) {
			file->private_data = cfile;
			spin_lock(&CIFS_I(inode)->deferred_lock);
			cifs_del_deferred_close(cfile);
			spin_unlock(&CIFS_I(inode)->deferred_lock);
			goto use_cache;
		} else {
			_cifsFileInfo_put(cfile, true, false);
		}
	}

	if (server->oplocks)
		oplock = REQ_OPLOCK;
	else
		oplock = 0;

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
	if (!tcon->broken_posix_open && tcon->unix_ext &&
	    cap_unix(tcon->ses) && (CIFS_UNIX_POSIX_PATH_OPS_CAP &
				le64_to_cpu(tcon->fsUnixInfo.Capability))) {
		/* can not refresh inode info since size could be stale */
		rc = cifs_posix_open(full_path, &inode, inode->i_sb,
				cifs_sb->ctx->file_mode /* ignored */,
				file->f_flags, &oplock, &fid.netfid, xid);
		if (rc == 0) {
			cifs_dbg(FYI, "posix open succeeded\n");
			posix_open_ok = true;
		} else if ((rc == -EINVAL) || (rc == -EOPNOTSUPP)) {
			if (tcon->ses->serverNOS)
				cifs_dbg(VFS, "server %s of type %s returned unexpected error on SMB posix open, disabling posix open support. Check if server update available.\n",
					 tcon->ses->ip_addr,
					 tcon->ses->serverNOS);
			tcon->broken_posix_open = true;
		} else if ((rc != -EIO) && (rc != -EREMOTE) &&
			 (rc != -EOPNOTSUPP)) /* path not found or net err */
			goto out;
		/*
		 * Else fallthrough to retry open the old way on network i/o
		 * or DFS errors.
		 */
	}
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */

	if (server->ops->get_lease_key)
		server->ops->get_lease_key(inode, &fid);

	cifs_add_pending_open(&fid, tlink, &open);

	if (!posix_open_ok) {
		if (server->ops->get_lease_key)
			server->ops->get_lease_key(inode, &fid);

		rc = cifs_nt_open(full_path, inode, cifs_sb, tcon, file->f_flags, &oplock, &fid,
				  xid, &data);
		if (rc) {
			cifs_del_pending_open(&open);
			goto out;
		}
	}

	cfile = cifs_new_fileinfo(&fid, file, tlink, oplock, data.symlink_target);
	if (cfile == NULL) {
		if (server->ops->close)
			server->ops->close(xid, tcon, &fid);
		cifs_del_pending_open(&open);
		rc = -ENOMEM;
		goto out;
	}

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
	if ((oplock & CIFS_CREATE_ACTION) && !posix_open_ok && tcon->unix_ext) {
		/*
		 * Time to set mode which we can not set earlier due to
		 * problems creating new read-only files.
		 */
		struct cifs_unix_set_info_args args = {
			.mode	= inode->i_mode,
			.uid	= INVALID_UID, /* no change */
			.gid	= INVALID_GID, /* no change */
			.ctime	= NO_CHANGE_64,
			.atime	= NO_CHANGE_64,
			.mtime	= NO_CHANGE_64,
			.device	= 0,
		};
		CIFSSMBUnixSetFileInfo(xid, tcon, &args, fid.netfid,
				       cfile->pid);
	}
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */

use_cache:
	fscache_use_cookie(cifs_inode_cookie(file_inode(file)),
			   file->f_mode & FMODE_WRITE);
	if (!(file->f_flags & O_DIRECT))
		goto out;
	if ((file->f_flags & (O_ACCMODE | O_APPEND)) == O_RDONLY)
		goto out;
	cifs_invalidate_cache(file_inode(file), FSCACHE_INVAL_DIO_WRITE);

out:
	free_dentry_path(page);
	free_xid(xid);
	cifs_put_tlink(tlink);
	cifs_free_open_info(&data);
	return rc;
}

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
static int cifs_push_posix_locks(struct cifsFileInfo *cfile);
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */

/*
 * Try to reacquire byte range locks that were released when session
 * to server was lost.
 */
static int
cifs_relock_file(struct cifsFileInfo *cfile)
{
	struct cifsInodeInfo *cinode = CIFS_I(d_inode(cfile->dentry));
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	int rc = 0;
#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
	struct cifs_sb_info *cifs_sb = CIFS_SB(cfile->dentry->d_sb);
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */

	down_read_nested(&cinode->lock_sem, SINGLE_DEPTH_NESTING);
	if (cinode->can_cache_brlcks) {
		/* can cache locks - no need to relock */
		up_read(&cinode->lock_sem);
		return rc;
	}

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
	if (cap_unix(tcon->ses) &&
	    (CIFS_UNIX_FCNTL_CAP & le64_to_cpu(tcon->fsUnixInfo.Capability)) &&
	    ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOPOSIXBRL) == 0))
		rc = cifs_push_posix_locks(cfile);
	else
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */
		rc = tcon->ses->server->ops->push_mand_locks(cfile);

	up_read(&cinode->lock_sem);
	return rc;
}

static int
cifs_reopen_file(struct cifsFileInfo *cfile, bool can_flush)
{
	int rc = -EACCES;
	unsigned int xid;
	__u32 oplock;
	struct cifs_sb_info *cifs_sb;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	struct cifsInodeInfo *cinode;
	struct inode *inode;
	void *page;
	const char *full_path;
	int desired_access;
	int disposition = FILE_OPEN;
	int create_options = CREATE_NOT_DIR;
	struct cifs_open_parms oparms;
	int rdwr_for_fscache = 0;

	xid = get_xid();
	mutex_lock(&cfile->fh_mutex);
	if (!cfile->invalidHandle) {
		mutex_unlock(&cfile->fh_mutex);
		free_xid(xid);
		return 0;
	}

	inode = d_inode(cfile->dentry);
	cifs_sb = CIFS_SB(inode->i_sb);
	tcon = tlink_tcon(cfile->tlink);
	server = tcon->ses->server;

	/*
	 * Can not grab rename sem here because various ops, including those
	 * that already have the rename sem can end up causing writepage to get
	 * called and if the server was down that means we end up here, and we
	 * can never tell if the caller already has the rename_sem.
	 */
	page = alloc_dentry_path();
	full_path = build_path_from_dentry(cfile->dentry, page);
	if (IS_ERR(full_path)) {
		mutex_unlock(&cfile->fh_mutex);
		free_dentry_path(page);
		free_xid(xid);
		return PTR_ERR(full_path);
	}

	cifs_dbg(FYI, "inode = 0x%p file flags 0x%x for %s\n",
		 inode, cfile->f_flags, full_path);

	if (tcon->ses->server->oplocks)
		oplock = REQ_OPLOCK;
	else
		oplock = 0;

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
	if (tcon->unix_ext && cap_unix(tcon->ses) &&
	    (CIFS_UNIX_POSIX_PATH_OPS_CAP &
				le64_to_cpu(tcon->fsUnixInfo.Capability))) {
		/*
		 * O_CREAT, O_EXCL and O_TRUNC already had their effect on the
		 * original open. Must mask them off for a reopen.
		 */
		unsigned int oflags = cfile->f_flags &
						~(O_CREAT | O_EXCL | O_TRUNC);

		rc = cifs_posix_open(full_path, NULL, inode->i_sb,
				     cifs_sb->ctx->file_mode /* ignored */,
				     oflags, &oplock, &cfile->fid.netfid, xid);
		if (rc == 0) {
			cifs_dbg(FYI, "posix reopen succeeded\n");
			oparms.reconnect = true;
			goto reopen_success;
		}
		/*
		 * fallthrough to retry open the old way on errors, especially
		 * in the reconnect path it is important to retry hard
		 */
	}
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */

	/* If we're caching, we need to be able to fill in around partial writes. */
	if (cifs_fscache_enabled(inode) && (cfile->f_flags & O_ACCMODE) == O_WRONLY)
		rdwr_for_fscache = 1;

	desired_access = cifs_convert_flags(cfile->f_flags, rdwr_for_fscache);

	/* O_SYNC also has bit for O_DSYNC so following check picks up either */
	if (cfile->f_flags & O_SYNC)
		create_options |= CREATE_WRITE_THROUGH;

	if (cfile->f_flags & O_DIRECT)
		create_options |= CREATE_NO_BUFFER;

	if (server->ops->get_lease_key)
		server->ops->get_lease_key(inode, &cfile->fid);

retry_open:
	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.cifs_sb = cifs_sb,
		.desired_access = desired_access,
		.create_options = cifs_create_options(cifs_sb, create_options),
		.disposition = disposition,
		.path = full_path,
		.fid = &cfile->fid,
		.reconnect = true,
	};

	/*
	 * Can not refresh inode by passing in file_info buf to be returned by
	 * ops->open and then calling get_inode_info with returned buf since
	 * file might have write behind data that needs to be flushed and server
	 * version of file size can be stale. If we knew for sure that inode was
	 * not dirty locally we could do this.
	 */
	rc = server->ops->open(xid, &oparms, &oplock, NULL);
	if (rc == -ENOENT && oparms.reconnect == false) {
		/* durable handle timeout is expired - open the file again */
		rc = server->ops->open(xid, &oparms, &oplock, NULL);
		/* indicate that we need to relock the file */
		oparms.reconnect = true;
	}
	if (rc == -EACCES && rdwr_for_fscache == 1) {
		desired_access = cifs_convert_flags(cfile->f_flags, 0);
		rdwr_for_fscache = 2;
		goto retry_open;
	}

	if (rc) {
		mutex_unlock(&cfile->fh_mutex);
		cifs_dbg(FYI, "cifs_reopen returned 0x%x\n", rc);
		cifs_dbg(FYI, "oplock: %d\n", oplock);
		goto reopen_error_exit;
	}

	if (rdwr_for_fscache == 2)
		cifs_invalidate_cache(inode, FSCACHE_INVAL_DIO_WRITE);

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
reopen_success:
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */
	cfile->invalidHandle = false;
	mutex_unlock(&cfile->fh_mutex);
	cinode = CIFS_I(inode);

	if (can_flush) {
		rc = filemap_write_and_wait(inode->i_mapping);
		if (!is_interrupt_error(rc))
			mapping_set_error(inode->i_mapping, rc);

		if (tcon->posix_extensions) {
			rc = smb311_posix_get_inode_info(&inode, full_path,
							 NULL, inode->i_sb, xid);
		} else if (tcon->unix_ext) {
			rc = cifs_get_inode_info_unix(&inode, full_path,
						      inode->i_sb, xid);
		} else {
			rc = cifs_get_inode_info(&inode, full_path, NULL,
						 inode->i_sb, xid, NULL);
		}
	}
	/*
	 * Else we are writing out data to server already and could deadlock if
	 * we tried to flush data, and since we do not know if we have data that
	 * would invalidate the current end of file on the server we can not go
	 * to the server to get the new inode info.
	 */

	/*
	 * If the server returned a read oplock and we have mandatory brlocks,
	 * set oplock level to None.
	 */
	if (server->ops->is_read_op(oplock) && cifs_has_mand_locks(cinode)) {
		cifs_dbg(FYI, "Reset oplock val from read to None due to mand locks\n");
		oplock = 0;
	}

	server->ops->set_fid(cfile, &cfile->fid, oplock);
	if (oparms.reconnect)
		cifs_relock_file(cfile);

reopen_error_exit:
	free_dentry_path(page);
	free_xid(xid);
	return rc;
}

void smb2_deferred_work_close(struct work_struct *work)
{
	struct cifsFileInfo *cfile = container_of(work,
			struct cifsFileInfo, deferred.work);

	spin_lock(&CIFS_I(d_inode(cfile->dentry))->deferred_lock);
	cifs_del_deferred_close(cfile);
	cfile->deferred_close_scheduled = false;
	spin_unlock(&CIFS_I(d_inode(cfile->dentry))->deferred_lock);
	_cifsFileInfo_put(cfile, true, false);
}

static bool
smb2_can_defer_close(struct inode *inode, struct cifs_deferred_close *dclose)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifsInodeInfo *cinode = CIFS_I(inode);

	return (cifs_sb->ctx->closetimeo && cinode->lease_granted && dclose &&
			(cinode->oplock == CIFS_CACHE_RHW_FLG ||
			 cinode->oplock == CIFS_CACHE_RH_FLG) &&
			!test_bit(CIFS_INO_CLOSE_ON_LOCK, &cinode->flags));

}

int cifs_close(struct inode *inode, struct file *file)
{
	struct cifsFileInfo *cfile;
	struct cifsInodeInfo *cinode = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifs_deferred_close *dclose;

	cifs_fscache_unuse_inode_cookie(inode, file->f_mode & FMODE_WRITE);

	if (file->private_data != NULL) {
		cfile = file->private_data;
		file->private_data = NULL;
		dclose = kmalloc(sizeof(struct cifs_deferred_close), GFP_KERNEL);
		if ((cfile->status_file_deleted == false) &&
		    (smb2_can_defer_close(inode, dclose))) {
			if (test_and_clear_bit(NETFS_ICTX_MODIFIED_ATTR, &cinode->netfs.flags)) {
				inode_set_mtime_to_ts(inode,
						      inode_set_ctime_current(inode));
			}
			spin_lock(&cinode->deferred_lock);
			cifs_add_deferred_close(cfile, dclose);
			if (cfile->deferred_close_scheduled &&
			    delayed_work_pending(&cfile->deferred)) {
				/*
				 * If there is no pending work, mod_delayed_work queues new work.
				 * So, Increase the ref count to avoid use-after-free.
				 */
				if (!mod_delayed_work(deferredclose_wq,
						&cfile->deferred, cifs_sb->ctx->closetimeo))
					cifsFileInfo_get(cfile);
			} else {
				/* Deferred close for files */
				queue_delayed_work(deferredclose_wq,
						&cfile->deferred, cifs_sb->ctx->closetimeo);
				cfile->deferred_close_scheduled = true;
				spin_unlock(&cinode->deferred_lock);
				return 0;
			}
			spin_unlock(&cinode->deferred_lock);
			_cifsFileInfo_put(cfile, true, false);
		} else {
			_cifsFileInfo_put(cfile, true, false);
			kfree(dclose);
		}
	}

	/* return code from the ->release op is always ignored */
	return 0;
}

void
cifs_reopen_persistent_handles(struct cifs_tcon *tcon)
{
	struct cifsFileInfo *open_file, *tmp;
	LIST_HEAD(tmp_list);

	if (!tcon->use_persistent || !tcon->need_reopen_files)
		return;

	tcon->need_reopen_files = false;

	cifs_dbg(FYI, "Reopen persistent handles\n");

	/* list all files open on tree connection, reopen resilient handles  */
	spin_lock(&tcon->open_file_lock);
	list_for_each_entry(open_file, &tcon->openFileList, tlist) {
		if (!open_file->invalidHandle)
			continue;
		cifsFileInfo_get(open_file);
		list_add_tail(&open_file->rlist, &tmp_list);
	}
	spin_unlock(&tcon->open_file_lock);

	list_for_each_entry_safe(open_file, tmp, &tmp_list, rlist) {
		if (cifs_reopen_file(open_file, false /* do not flush */))
			tcon->need_reopen_files = true;
		list_del_init(&open_file->rlist);
		cifsFileInfo_put(open_file);
	}
}

int cifs_closedir(struct inode *inode, struct file *file)
{
	int rc = 0;
	unsigned int xid;
	struct cifsFileInfo *cfile = file->private_data;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	char *buf;

	cifs_dbg(FYI, "Closedir inode = 0x%p\n", inode);

	if (cfile == NULL)
		return rc;

	xid = get_xid();
	tcon = tlink_tcon(cfile->tlink);
	server = tcon->ses->server;

	cifs_dbg(FYI, "Freeing private data in close dir\n");
	spin_lock(&cfile->file_info_lock);
	if (server->ops->dir_needs_close(cfile)) {
		cfile->invalidHandle = true;
		spin_unlock(&cfile->file_info_lock);
		if (server->ops->close_dir)
			rc = server->ops->close_dir(xid, tcon, &cfile->fid);
		else
			rc = -ENOSYS;
		cifs_dbg(FYI, "Closing uncompleted readdir with rc %d\n", rc);
		/* not much we can do if it fails anyway, ignore rc */
		rc = 0;
	} else
		spin_unlock(&cfile->file_info_lock);

	buf = cfile->srch_inf.ntwrk_buf_start;
	if (buf) {
		cifs_dbg(FYI, "closedir free smb buf in srch struct\n");
		cfile->srch_inf.ntwrk_buf_start = NULL;
		if (cfile->srch_inf.smallBuf)
			cifs_small_buf_release(buf);
		else
			cifs_buf_release(buf);
	}

	cifs_put_tlink(cfile->tlink);
	kfree(file->private_data);
	file->private_data = NULL;
	/* BB can we lock the filestruct while this is going on? */
	free_xid(xid);
	return rc;
}

static struct cifsLockInfo *
cifs_lock_init(__u64 offset, __u64 length, __u8 type, __u16 flags)
{
	struct cifsLockInfo *lock =
		kmalloc(sizeof(struct cifsLockInfo), GFP_KERNEL);
	if (!lock)
		return lock;
	lock->offset = offset;
	lock->length = length;
	lock->type = type;
	lock->pid = current->tgid;
	lock->flags = flags;
	INIT_LIST_HEAD(&lock->blist);
	init_waitqueue_head(&lock->block_q);
	return lock;
}

void
cifs_del_lock_waiters(struct cifsLockInfo *lock)
{
	struct cifsLockInfo *li, *tmp;
	list_for_each_entry_safe(li, tmp, &lock->blist, blist) {
		list_del_init(&li->blist);
		wake_up(&li->block_q);
	}
}

#define CIFS_LOCK_OP	0
#define CIFS_READ_OP	1
#define CIFS_WRITE_OP	2

/* @rw_check : 0 - no op, 1 - read, 2 - write */
static bool
cifs_find_fid_lock_conflict(struct cifs_fid_locks *fdlocks, __u64 offset,
			    __u64 length, __u8 type, __u16 flags,
			    struct cifsFileInfo *cfile,
			    struct cifsLockInfo **conf_lock, int rw_check)
{
	struct cifsLockInfo *li;
	struct cifsFileInfo *cur_cfile = fdlocks->cfile;
	struct TCP_Server_Info *server = tlink_tcon(cfile->tlink)->ses->server;

	list_for_each_entry(li, &fdlocks->locks, llist) {
		if (offset + length <= li->offset ||
		    offset >= li->offset + li->length)
			continue;
		if (rw_check != CIFS_LOCK_OP && current->tgid == li->pid &&
		    server->ops->compare_fids(cfile, cur_cfile)) {
			/* shared lock prevents write op through the same fid */
			if (!(li->type & server->vals->shared_lock_type) ||
			    rw_check != CIFS_WRITE_OP)
				continue;
		}
		if ((type & server->vals->shared_lock_type) &&
		    ((server->ops->compare_fids(cfile, cur_cfile) &&
		     current->tgid == li->pid) || type == li->type))
			continue;
		if (rw_check == CIFS_LOCK_OP &&
		    (flags & FL_OFDLCK) && (li->flags & FL_OFDLCK) &&
		    server->ops->compare_fids(cfile, cur_cfile))
			continue;
		if (conf_lock)
			*conf_lock = li;
		return true;
	}
	return false;
}

bool
cifs_find_lock_conflict(struct cifsFileInfo *cfile, __u64 offset, __u64 length,
			__u8 type, __u16 flags,
			struct cifsLockInfo **conf_lock, int rw_check)
{
	bool rc = false;
	struct cifs_fid_locks *cur;
	struct cifsInodeInfo *cinode = CIFS_I(d_inode(cfile->dentry));

	list_for_each_entry(cur, &cinode->llist, llist) {
		rc = cifs_find_fid_lock_conflict(cur, offset, length, type,
						 flags, cfile, conf_lock,
						 rw_check);
		if (rc)
			break;
	}

	return rc;
}

/*
 * Check if there is another lock that prevents us to set the lock (mandatory
 * style). If such a lock exists, update the flock structure with its
 * properties. Otherwise, set the flock type to F_UNLCK if we can cache brlocks
 * or leave it the same if we can't. Returns 0 if we don't need to request to
 * the server or 1 otherwise.
 */
static int
cifs_lock_test(struct cifsFileInfo *cfile, __u64 offset, __u64 length,
	       __u8 type, struct file_lock *flock)
{
	int rc = 0;
	struct cifsLockInfo *conf_lock;
	struct cifsInodeInfo *cinode = CIFS_I(d_inode(cfile->dentry));
	struct TCP_Server_Info *server = tlink_tcon(cfile->tlink)->ses->server;
	bool exist;

	down_read(&cinode->lock_sem);

	exist = cifs_find_lock_conflict(cfile, offset, length, type,
					flock->c.flc_flags, &conf_lock,
					CIFS_LOCK_OP);
	if (exist) {
		flock->fl_start = conf_lock->offset;
		flock->fl_end = conf_lock->offset + conf_lock->length - 1;
		flock->c.flc_pid = conf_lock->pid;
		if (conf_lock->type & server->vals->shared_lock_type)
			flock->c.flc_type = F_RDLCK;
		else
			flock->c.flc_type = F_WRLCK;
	} else if (!cinode->can_cache_brlcks)
		rc = 1;
	else
		flock->c.flc_type = F_UNLCK;

	up_read(&cinode->lock_sem);
	return rc;
}

static void
cifs_lock_add(struct cifsFileInfo *cfile, struct cifsLockInfo *lock)
{
	struct cifsInodeInfo *cinode = CIFS_I(d_inode(cfile->dentry));
	cifs_down_write(&cinode->lock_sem);
	list_add_tail(&lock->llist, &cfile->llist->locks);
	up_write(&cinode->lock_sem);
}

/*
 * Set the byte-range lock (mandatory style). Returns:
 * 1) 0, if we set the lock and don't need to request to the server;
 * 2) 1, if no locks prevent us but we need to request to the server;
 * 3) -EACCES, if there is a lock that prevents us and wait is false.
 */
static int
cifs_lock_add_if(struct cifsFileInfo *cfile, struct cifsLockInfo *lock,
		 bool wait)
{
	struct cifsLockInfo *conf_lock;
	struct cifsInodeInfo *cinode = CIFS_I(d_inode(cfile->dentry));
	bool exist;
	int rc = 0;

try_again:
	exist = false;
	cifs_down_write(&cinode->lock_sem);

	exist = cifs_find_lock_conflict(cfile, lock->offset, lock->length,
					lock->type, lock->flags, &conf_lock,
					CIFS_LOCK_OP);
	if (!exist && cinode->can_cache_brlcks) {
		list_add_tail(&lock->llist, &cfile->llist->locks);
		up_write(&cinode->lock_sem);
		return rc;
	}

	if (!exist)
		rc = 1;
	else if (!wait)
		rc = -EACCES;
	else {
		list_add_tail(&lock->blist, &conf_lock->blist);
		up_write(&cinode->lock_sem);
		rc = wait_event_interruptible(lock->block_q,
					(lock->blist.prev == &lock->blist) &&
					(lock->blist.next == &lock->blist));
		if (!rc)
			goto try_again;
		cifs_down_write(&cinode->lock_sem);
		list_del_init(&lock->blist);
	}

	up_write(&cinode->lock_sem);
	return rc;
}

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
/*
 * Check if there is another lock that prevents us to set the lock (posix
 * style). If such a lock exists, update the flock structure with its
 * properties. Otherwise, set the flock type to F_UNLCK if we can cache brlocks
 * or leave it the same if we can't. Returns 0 if we don't need to request to
 * the server or 1 otherwise.
 */
static int
cifs_posix_lock_test(struct file *file, struct file_lock *flock)
{
	int rc = 0;
	struct cifsInodeInfo *cinode = CIFS_I(file_inode(file));
	unsigned char saved_type = flock->c.flc_type;

	if ((flock->c.flc_flags & FL_POSIX) == 0)
		return 1;

	down_read(&cinode->lock_sem);
	posix_test_lock(file, flock);

	if (lock_is_unlock(flock) && !cinode->can_cache_brlcks) {
		flock->c.flc_type = saved_type;
		rc = 1;
	}

	up_read(&cinode->lock_sem);
	return rc;
}

/*
 * Set the byte-range lock (posix style). Returns:
 * 1) <0, if the error occurs while setting the lock;
 * 2) 0, if we set the lock and don't need to request to the server;
 * 3) FILE_LOCK_DEFERRED, if we will wait for some other file_lock;
 * 4) FILE_LOCK_DEFERRED + 1, if we need to request to the server.
 */
static int
cifs_posix_lock_set(struct file *file, struct file_lock *flock)
{
	struct cifsInodeInfo *cinode = CIFS_I(file_inode(file));
	int rc = FILE_LOCK_DEFERRED + 1;

	if ((flock->c.flc_flags & FL_POSIX) == 0)
		return rc;

	cifs_down_write(&cinode->lock_sem);
	if (!cinode->can_cache_brlcks) {
		up_write(&cinode->lock_sem);
		return rc;
	}

	rc = posix_lock_file(file, flock, NULL);
	up_write(&cinode->lock_sem);
	return rc;
}

int
cifs_push_mandatory_locks(struct cifsFileInfo *cfile)
{
	unsigned int xid;
	int rc = 0, stored_rc;
	struct cifsLockInfo *li, *tmp;
	struct cifs_tcon *tcon;
	unsigned int num, max_num, max_buf;
	LOCKING_ANDX_RANGE *buf, *cur;
	static const int types[] = {
		LOCKING_ANDX_LARGE_FILES,
		LOCKING_ANDX_SHARED_LOCK | LOCKING_ANDX_LARGE_FILES
	};
	int i;

	xid = get_xid();
	tcon = tlink_tcon(cfile->tlink);

	/*
	 * Accessing maxBuf is racy with cifs_reconnect - need to store value
	 * and check it before using.
	 */
	max_buf = tcon->ses->server->maxBuf;
	if (max_buf < (sizeof(struct smb_hdr) + sizeof(LOCKING_ANDX_RANGE))) {
		free_xid(xid);
		return -EINVAL;
	}

	BUILD_BUG_ON(sizeof(struct smb_hdr) + sizeof(LOCKING_ANDX_RANGE) >
		     PAGE_SIZE);
	max_buf = min_t(unsigned int, max_buf - sizeof(struct smb_hdr),
			PAGE_SIZE);
	max_num = (max_buf - sizeof(struct smb_hdr)) /
						sizeof(LOCKING_ANDX_RANGE);
	buf = kcalloc(max_num, sizeof(LOCKING_ANDX_RANGE), GFP_KERNEL);
	if (!buf) {
		free_xid(xid);
		return -ENOMEM;
	}

	for (i = 0; i < 2; i++) {
		cur = buf;
		num = 0;
		list_for_each_entry_safe(li, tmp, &cfile->llist->locks, llist) {
			if (li->type != types[i])
				continue;
			cur->Pid = cpu_to_le16(li->pid);
			cur->LengthLow = cpu_to_le32((u32)li->length);
			cur->LengthHigh = cpu_to_le32((u32)(li->length>>32));
			cur->OffsetLow = cpu_to_le32((u32)li->offset);
			cur->OffsetHigh = cpu_to_le32((u32)(li->offset>>32));
			if (++num == max_num) {
				stored_rc = cifs_lockv(xid, tcon,
						       cfile->fid.netfid,
						       (__u8)li->type, 0, num,
						       buf);
				if (stored_rc)
					rc = stored_rc;
				cur = buf;
				num = 0;
			} else
				cur++;
		}

		if (num) {
			stored_rc = cifs_lockv(xid, tcon, cfile->fid.netfid,
					       (__u8)types[i], 0, num, buf);
			if (stored_rc)
				rc = stored_rc;
		}
	}

	kfree(buf);
	free_xid(xid);
	return rc;
}

static __u32
hash_lockowner(fl_owner_t owner)
{
	return cifs_lock_secret ^ hash32_ptr((const void *)owner);
}
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */

struct lock_to_push {
	struct list_head llist;
	__u64 offset;
	__u64 length;
	__u32 pid;
	__u16 netfid;
	__u8 type;
};

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
static int
cifs_push_posix_locks(struct cifsFileInfo *cfile)
{
	struct inode *inode = d_inode(cfile->dentry);
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	struct file_lock *flock;
	struct file_lock_context *flctx = locks_inode_context(inode);
	unsigned int count = 0, i;
	int rc = 0, xid, type;
	struct list_head locks_to_send, *el;
	struct lock_to_push *lck, *tmp;
	__u64 length;

	xid = get_xid();

	if (!flctx)
		goto out;

	spin_lock(&flctx->flc_lock);
	list_for_each(el, &flctx->flc_posix) {
		count++;
	}
	spin_unlock(&flctx->flc_lock);

	INIT_LIST_HEAD(&locks_to_send);

	/*
	 * Allocating count locks is enough because no FL_POSIX locks can be
	 * added to the list while we are holding cinode->lock_sem that
	 * protects locking operations of this inode.
	 */
	for (i = 0; i < count; i++) {
		lck = kmalloc(sizeof(struct lock_to_push), GFP_KERNEL);
		if (!lck) {
			rc = -ENOMEM;
			goto err_out;
		}
		list_add_tail(&lck->llist, &locks_to_send);
	}

	el = locks_to_send.next;
	spin_lock(&flctx->flc_lock);
	for_each_file_lock(flock, &flctx->flc_posix) {
		unsigned char ftype = flock->c.flc_type;

		if (el == &locks_to_send) {
			/*
			 * The list ended. We don't have enough allocated
			 * structures - something is really wrong.
			 */
			cifs_dbg(VFS, "Can't push all brlocks!\n");
			break;
		}
		length = cifs_flock_len(flock);
		if (ftype == F_RDLCK || ftype == F_SHLCK)
			type = CIFS_RDLCK;
		else
			type = CIFS_WRLCK;
		lck = list_entry(el, struct lock_to_push, llist);
		lck->pid = hash_lockowner(flock->c.flc_owner);
		lck->netfid = cfile->fid.netfid;
		lck->length = length;
		lck->type = type;
		lck->offset = flock->fl_start;
	}
	spin_unlock(&flctx->flc_lock);

	list_for_each_entry_safe(lck, tmp, &locks_to_send, llist) {
		int stored_rc;

		stored_rc = CIFSSMBPosixLock(xid, tcon, lck->netfid, lck->pid,
					     lck->offset, lck->length, NULL,
					     lck->type, 0);
		if (stored_rc)
			rc = stored_rc;
		list_del(&lck->llist);
		kfree(lck);
	}

out:
	free_xid(xid);
	return rc;
err_out:
	list_for_each_entry_safe(lck, tmp, &locks_to_send, llist) {
		list_del(&lck->llist);
		kfree(lck);
	}
	goto out;
}
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */

static int
cifs_push_locks(struct cifsFileInfo *cfile)
{
	struct cifsInodeInfo *cinode = CIFS_I(d_inode(cfile->dentry));
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	int rc = 0;
#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
	struct cifs_sb_info *cifs_sb = CIFS_SB(cfile->dentry->d_sb);
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */

	/* we are going to update can_cache_brlcks here - need a write access */
	cifs_down_write(&cinode->lock_sem);
	if (!cinode->can_cache_brlcks) {
		up_write(&cinode->lock_sem);
		return rc;
	}

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
	if (cap_unix(tcon->ses) &&
	    (CIFS_UNIX_FCNTL_CAP & le64_to_cpu(tcon->fsUnixInfo.Capability)) &&
	    ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOPOSIXBRL) == 0))
		rc = cifs_push_posix_locks(cfile);
	else
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */
		rc = tcon->ses->server->ops->push_mand_locks(cfile);

	cinode->can_cache_brlcks = false;
	up_write(&cinode->lock_sem);
	return rc;
}

static void
cifs_read_flock(struct file_lock *flock, __u32 *type, int *lock, int *unlock,
		bool *wait_flag, struct TCP_Server_Info *server)
{
	if (flock->c.flc_flags & FL_POSIX)
		cifs_dbg(FYI, "Posix\n");
	if (flock->c.flc_flags & FL_FLOCK)
		cifs_dbg(FYI, "Flock\n");
	if (flock->c.flc_flags & FL_SLEEP) {
		cifs_dbg(FYI, "Blocking lock\n");
		*wait_flag = true;
	}
	if (flock->c.flc_flags & FL_ACCESS)
		cifs_dbg(FYI, "Process suspended by mandatory locking - not implemented yet\n");
	if (flock->c.flc_flags & FL_LEASE)
		cifs_dbg(FYI, "Lease on file - not implemented yet\n");
	if (flock->c.flc_flags &
	    (~(FL_POSIX | FL_FLOCK | FL_SLEEP |
	       FL_ACCESS | FL_LEASE | FL_CLOSE | FL_OFDLCK)))
		cifs_dbg(FYI, "Unknown lock flags 0x%x\n",
		         flock->c.flc_flags);

	*type = server->vals->large_lock_type;
	if (lock_is_write(flock)) {
		cifs_dbg(FYI, "F_WRLCK\n");
		*type |= server->vals->exclusive_lock_type;
		*lock = 1;
	} else if (lock_is_unlock(flock)) {
		cifs_dbg(FYI, "F_UNLCK\n");
		*type |= server->vals->unlock_lock_type;
		*unlock = 1;
		/* Check if unlock includes more than one lock range */
	} else if (lock_is_read(flock)) {
		cifs_dbg(FYI, "F_RDLCK\n");
		*type |= server->vals->shared_lock_type;
		*lock = 1;
	} else if (flock->c.flc_type == F_EXLCK) {
		cifs_dbg(FYI, "F_EXLCK\n");
		*type |= server->vals->exclusive_lock_type;
		*lock = 1;
	} else if (flock->c.flc_type == F_SHLCK) {
		cifs_dbg(FYI, "F_SHLCK\n");
		*type |= server->vals->shared_lock_type;
		*lock = 1;
	} else
		cifs_dbg(FYI, "Unknown type of lock\n");
}

static int
cifs_getlk(struct file *file, struct file_lock *flock, __u32 type,
	   bool wait_flag, bool posix_lck, unsigned int xid)
{
	int rc = 0;
	__u64 length = cifs_flock_len(flock);
	struct cifsFileInfo *cfile = (struct cifsFileInfo *)file->private_data;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	struct TCP_Server_Info *server = tcon->ses->server;
#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
	__u16 netfid = cfile->fid.netfid;

	if (posix_lck) {
		int posix_lock_type;

		rc = cifs_posix_lock_test(file, flock);
		if (!rc)
			return rc;

		if (type & server->vals->shared_lock_type)
			posix_lock_type = CIFS_RDLCK;
		else
			posix_lock_type = CIFS_WRLCK;
		rc = CIFSSMBPosixLock(xid, tcon, netfid,
				      hash_lockowner(flock->c.flc_owner),
				      flock->fl_start, length, flock,
				      posix_lock_type, wait_flag);
		return rc;
	}
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */

	rc = cifs_lock_test(cfile, flock->fl_start, length, type, flock);
	if (!rc)
		return rc;

	/* BB we could chain these into one lock request BB */
	rc = server->ops->mand_lock(xid, cfile, flock->fl_start, length, type,
				    1, 0, false);
	if (rc == 0) {
		rc = server->ops->mand_lock(xid, cfile, flock->fl_start, length,
					    type, 0, 1, false);
		flock->c.flc_type = F_UNLCK;
		if (rc != 0)
			cifs_dbg(VFS, "Error unlocking previously locked range %d during test of lock\n",
				 rc);
		return 0;
	}

	if (type & server->vals->shared_lock_type) {
		flock->c.flc_type = F_WRLCK;
		return 0;
	}

	type &= ~server->vals->exclusive_lock_type;

	rc = server->ops->mand_lock(xid, cfile, flock->fl_start, length,
				    type | server->vals->shared_lock_type,
				    1, 0, false);
	if (rc == 0) {
		rc = server->ops->mand_lock(xid, cfile, flock->fl_start, length,
			type | server->vals->shared_lock_type, 0, 1, false);
		flock->c.flc_type = F_RDLCK;
		if (rc != 0)
			cifs_dbg(VFS, "Error unlocking previously locked range %d during test of lock\n",
				 rc);
	} else
		flock->c.flc_type = F_WRLCK;

	return 0;
}

void
cifs_move_llist(struct list_head *source, struct list_head *dest)
{
	struct list_head *li, *tmp;
	list_for_each_safe(li, tmp, source)
		list_move(li, dest);
}

void
cifs_free_llist(struct list_head *llist)
{
	struct cifsLockInfo *li, *tmp;
	list_for_each_entry_safe(li, tmp, llist, llist) {
		cifs_del_lock_waiters(li);
		list_del(&li->llist);
		kfree(li);
	}
}

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
int
cifs_unlock_range(struct cifsFileInfo *cfile, struct file_lock *flock,
		  unsigned int xid)
{
	int rc = 0, stored_rc;
	static const int types[] = {
		LOCKING_ANDX_LARGE_FILES,
		LOCKING_ANDX_SHARED_LOCK | LOCKING_ANDX_LARGE_FILES
	};
	unsigned int i;
	unsigned int max_num, num, max_buf;
	LOCKING_ANDX_RANGE *buf, *cur;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	struct cifsInodeInfo *cinode = CIFS_I(d_inode(cfile->dentry));
	struct cifsLockInfo *li, *tmp;
	__u64 length = cifs_flock_len(flock);
	LIST_HEAD(tmp_llist);

	/*
	 * Accessing maxBuf is racy with cifs_reconnect - need to store value
	 * and check it before using.
	 */
	max_buf = tcon->ses->server->maxBuf;
	if (max_buf < (sizeof(struct smb_hdr) + sizeof(LOCKING_ANDX_RANGE)))
		return -EINVAL;

	BUILD_BUG_ON(sizeof(struct smb_hdr) + sizeof(LOCKING_ANDX_RANGE) >
		     PAGE_SIZE);
	max_buf = min_t(unsigned int, max_buf - sizeof(struct smb_hdr),
			PAGE_SIZE);
	max_num = (max_buf - sizeof(struct smb_hdr)) /
						sizeof(LOCKING_ANDX_RANGE);
	buf = kcalloc(max_num, sizeof(LOCKING_ANDX_RANGE), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	cifs_down_write(&cinode->lock_sem);
	for (i = 0; i < 2; i++) {
		cur = buf;
		num = 0;
		list_for_each_entry_safe(li, tmp, &cfile->llist->locks, llist) {
			if (flock->fl_start > li->offset ||
			    (flock->fl_start + length) <
			    (li->offset + li->length))
				continue;
			if (current->tgid != li->pid)
				continue;
			if (types[i] != li->type)
				continue;
			if (cinode->can_cache_brlcks) {
				/*
				 * We can cache brlock requests - simply remove
				 * a lock from the file's list.
				 */
				list_del(&li->llist);
				cifs_del_lock_waiters(li);
				kfree(li);
				continue;
			}
			cur->Pid = cpu_to_le16(li->pid);
			cur->LengthLow = cpu_to_le32((u32)li->length);
			cur->LengthHigh = cpu_to_le32((u32)(li->length>>32));
			cur->OffsetLow = cpu_to_le32((u32)li->offset);
			cur->OffsetHigh = cpu_to_le32((u32)(li->offset>>32));
			/*
			 * We need to save a lock here to let us add it again to
			 * the file's list if the unlock range request fails on
			 * the server.
			 */
			list_move(&li->llist, &tmp_llist);
			if (++num == max_num) {
				stored_rc = cifs_lockv(xid, tcon,
						       cfile->fid.netfid,
						       li->type, num, 0, buf);
				if (stored_rc) {
					/*
					 * We failed on the unlock range
					 * request - add all locks from the tmp
					 * list to the head of the file's list.
					 */
					cifs_move_llist(&tmp_llist,
							&cfile->llist->locks);
					rc = stored_rc;
				} else
					/*
					 * The unlock range request succeed -
					 * free the tmp list.
					 */
					cifs_free_llist(&tmp_llist);
				cur = buf;
				num = 0;
			} else
				cur++;
		}
		if (num) {
			stored_rc = cifs_lockv(xid, tcon, cfile->fid.netfid,
					       types[i], num, 0, buf);
			if (stored_rc) {
				cifs_move_llist(&tmp_llist,
						&cfile->llist->locks);
				rc = stored_rc;
			} else
				cifs_free_llist(&tmp_llist);
		}
	}

	up_write(&cinode->lock_sem);
	kfree(buf);
	return rc;
}
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */

static int
cifs_setlk(struct file *file, struct file_lock *flock, __u32 type,
	   bool wait_flag, bool posix_lck, int lock, int unlock,
	   unsigned int xid)
{
	int rc = 0;
	__u64 length = cifs_flock_len(flock);
	struct cifsFileInfo *cfile = (struct cifsFileInfo *)file->private_data;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	struct TCP_Server_Info *server = tcon->ses->server;
	struct inode *inode = d_inode(cfile->dentry);

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY
	if (posix_lck) {
		int posix_lock_type;

		rc = cifs_posix_lock_set(file, flock);
		if (rc <= FILE_LOCK_DEFERRED)
			return rc;

		if (type & server->vals->shared_lock_type)
			posix_lock_type = CIFS_RDLCK;
		else
			posix_lock_type = CIFS_WRLCK;

		if (unlock == 1)
			posix_lock_type = CIFS_UNLCK;

		rc = CIFSSMBPosixLock(xid, tcon, cfile->fid.netfid,
				      hash_lockowner(flock->c.flc_owner),
				      flock->fl_start, length,
				      NULL, posix_lock_type, wait_flag);
		goto out;
	}
#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */
	if (lock) {
		struct cifsLockInfo *lock;

		lock = cifs_lock_init(flock->fl_start, length, type,
				      flock->c.flc_flags);
		if (!lock)
			return -ENOMEM;

		rc = cifs_lock_add_if(cfile, lock, wait_flag);
		if (rc < 0) {
			kfree(lock);
			return rc;
		}
		if (!rc)
			goto out;

		/*
		 * Windows 7 server can delay breaking lease from read to None
		 * if we set a byte-range lock on a file - break it explicitly
		 * before sending the lock to the server to be sure the next
		 * read won't conflict with non-overlapted locks due to
		 * pagereading.
		 */
		if (!CIFS_CACHE_WRITE(CIFS_I(inode)) &&
					CIFS_CACHE_READ(CIFS_I(inode))) {
			cifs_zap_mapping(inode);
			cifs_dbg(FYI, "Set no oplock for inode=%p due to mand locks\n",
				 inode);
			CIFS_I(inode)->oplock = 0;
		}

		rc = server->ops->mand_lock(xid, cfile, flock->fl_start, length,
					    type, 1, 0, wait_flag);
		if (rc) {
			kfree(lock);
			return rc;
		}

		cifs_lock_add(cfile, lock);
	} else if (unlock)
		rc = server->ops->mand_unlock_range(cfile, flock, xid);

out:
	if ((flock->c.flc_flags & FL_POSIX) || (flock->c.flc_flags & FL_FLOCK)) {
		/*
		 * If this is a request to remove all locks because we
		 * are closing the file, it doesn't matter if the
		 * unlocking failed as both cifs.ko and the SMB server
		 * remove the lock on file close
		 */
		if (rc) {
			cifs_dbg(VFS, "%s failed rc=%d\n", __func__, rc);
			if (!(flock->c.flc_flags & FL_CLOSE))
				return rc;
		}
		rc = locks_lock_file_wait(file, flock);
	}
	return rc;
}

int cifs_flock(struct file *file, int cmd, struct file_lock *fl)
{
	int rc, xid;
	int lock = 0, unlock = 0;
	bool wait_flag = false;
	bool posix_lck = false;
	struct cifs_sb_info *cifs_sb;
	struct cifs_tcon *tcon;
	struct cifsFileInfo *cfile;
	__u32 type;

	xid = get_xid();

	if (!(fl->c.flc_flags & FL_FLOCK)) {
		rc = -ENOLCK;
		free_xid(xid);
		return rc;
	}

	cfile = (struct cifsFileInfo *)file->private_data;
	tcon = tlink_tcon(cfile->tlink);

	cifs_read_flock(fl, &type, &lock, &unlock, &wait_flag,
			tcon->ses->server);
	cifs_sb = CIFS_FILE_SB(file);

	if (cap_unix(tcon->ses) &&
	    (CIFS_UNIX_FCNTL_CAP & le64_to_cpu(tcon->fsUnixInfo.Capability)) &&
	    ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOPOSIXBRL) == 0))
		posix_lck = true;

	if (!lock && !unlock) {
		/*
		 * if no lock or unlock then nothing to do since we do not
		 * know what it is
		 */
		rc = -EOPNOTSUPP;
		free_xid(xid);
		return rc;
	}

	rc = cifs_setlk(file, fl, type, wait_flag, posix_lck, lock, unlock,
			xid);
	free_xid(xid);
	return rc;


}

int cifs_lock(struct file *file, int cmd, struct file_lock *flock)
{
	int rc, xid;
	int lock = 0, unlock = 0;
	bool wait_flag = false;
	bool posix_lck = false;
	struct cifs_sb_info *cifs_sb;
	struct cifs_tcon *tcon;
	struct cifsFileInfo *cfile;
	__u32 type;

	rc = -EACCES;
	xid = get_xid();

	cifs_dbg(FYI, "%s: %pD2 cmd=0x%x type=0x%x flags=0x%x r=%lld:%lld\n", __func__, file, cmd,
		 flock->c.flc_flags, flock->c.flc_type,
		 (long long)flock->fl_start,
		 (long long)flock->fl_end);

	cfile = (struct cifsFileInfo *)file->private_data;
	tcon = tlink_tcon(cfile->tlink);

	cifs_read_flock(flock, &type, &lock, &unlock, &wait_flag,
			tcon->ses->server);
	cifs_sb = CIFS_FILE_SB(file);
	set_bit(CIFS_INO_CLOSE_ON_LOCK, &CIFS_I(d_inode(cfile->dentry))->flags);

	if (cap_unix(tcon->ses) &&
	    (CIFS_UNIX_FCNTL_CAP & le64_to_cpu(tcon->fsUnixInfo.Capability)) &&
	    ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOPOSIXBRL) == 0))
		posix_lck = true;
	/*
	 * BB add code here to normalize offset and length to account for
	 * negative length which we can not accept over the wire.
	 */
	if (IS_GETLK(cmd)) {
		rc = cifs_getlk(file, flock, type, wait_flag, posix_lck, xid);
		free_xid(xid);
		return rc;
	}

	if (!lock && !unlock) {
		/*
		 * if no lock or unlock then nothing to do since we do not
		 * know what it is
		 */
		free_xid(xid);
		return -EOPNOTSUPP;
	}

	rc = cifs_setlk(file, flock, type, wait_flag, posix_lck, lock, unlock,
			xid);
	free_xid(xid);
	return rc;
}

void cifs_write_subrequest_terminated(struct cifs_io_subrequest *wdata, ssize_t result,
				      bool was_async)
{
	struct netfs_io_request *wreq = wdata->rreq;
	struct netfs_inode *ictx = netfs_inode(wreq->inode);
	loff_t wrend;

	if (result > 0) {
		wrend = wdata->subreq.start + wdata->subreq.transferred + result;

		if (wrend > ictx->zero_point &&
		    (wdata->rreq->origin == NETFS_UNBUFFERED_WRITE ||
		     wdata->rreq->origin == NETFS_DIO_WRITE))
			ictx->zero_point = wrend;
		if (wrend > ictx->remote_i_size)
			netfs_resize_file(ictx, wrend, true);
	}

	netfs_write_subrequest_terminated(&wdata->subreq, result, was_async);
}

struct cifsFileInfo *find_readable_file(struct cifsInodeInfo *cifs_inode,
					bool fsuid_only)
{
	struct cifsFileInfo *open_file = NULL;
	struct cifs_sb_info *cifs_sb = CIFS_SB(cifs_inode->netfs.inode.i_sb);

	/* only filter by fsuid on multiuser mounts */
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MULTIUSER))
		fsuid_only = false;

	spin_lock(&cifs_inode->open_file_lock);
	/* we could simply get the first_list_entry since write-only entries
	   are always at the end of the list but since the first entry might
	   have a close pending, we go through the whole list */
	list_for_each_entry(open_file, &cifs_inode->openFileList, flist) {
		if (fsuid_only && !uid_eq(open_file->uid, current_fsuid()))
			continue;
		if (OPEN_FMODE(open_file->f_flags) & FMODE_READ) {
			if ((!open_file->invalidHandle)) {
				/* found a good file */
				/* lock it so it will not be closed on us */
				cifsFileInfo_get(open_file);
				spin_unlock(&cifs_inode->open_file_lock);
				return open_file;
			} /* else might as well continue, and look for
			     another, or simply have the caller reopen it
			     again rather than trying to fix this handle */
		} else /* write only file */
			break; /* write only files are last so must be done */
	}
	spin_unlock(&cifs_inode->open_file_lock);
	return NULL;
}

/* Return -EBADF if no handle is found and general rc otherwise */
int
cifs_get_writable_file(struct cifsInodeInfo *cifs_inode, int flags,
		       struct cifsFileInfo **ret_file)
{
	struct cifsFileInfo *open_file, *inv_file = NULL;
	struct cifs_sb_info *cifs_sb;
	bool any_available = false;
	int rc = -EBADF;
	unsigned int refind = 0;
	bool fsuid_only = flags & FIND_WR_FSUID_ONLY;
	bool with_delete = flags & FIND_WR_WITH_DELETE;
	*ret_file = NULL;

	/*
	 * Having a null inode here (because mapping->host was set to zero by
	 * the VFS or MM) should not happen but we had reports of on oops (due
	 * to it being zero) during stress testcases so we need to check for it
	 */

	if (cifs_inode == NULL) {
		cifs_dbg(VFS, "Null inode passed to cifs_writeable_file\n");
		dump_stack();
		return rc;
	}

	cifs_sb = CIFS_SB(cifs_inode->netfs.inode.i_sb);

	/* only filter by fsuid on multiuser mounts */
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MULTIUSER))
		fsuid_only = false;

	spin_lock(&cifs_inode->open_file_lock);
refind_writable:
	if (refind > MAX_REOPEN_ATT) {
		spin_unlock(&cifs_inode->open_file_lock);
		return rc;
	}
	list_for_each_entry(open_file, &cifs_inode->openFileList, flist) {
		if (!any_available && open_file->pid != current->tgid)
			continue;
		if (fsuid_only && !uid_eq(open_file->uid, current_fsuid()))
			continue;
		if (with_delete && !(open_file->fid.access & DELETE))
			continue;
		if (OPEN_FMODE(open_file->f_flags) & FMODE_WRITE) {
			if (!open_file->invalidHandle) {
				/* found a good writable file */
				cifsFileInfo_get(open_file);
				spin_unlock(&cifs_inode->open_file_lock);
				*ret_file = open_file;
				return 0;
			} else {
				if (!inv_file)
					inv_file = open_file;
			}
		}
	}
	/* couldn't find usable FH with same pid, try any available */
	if (!any_available) {
		any_available = true;
		goto refind_writable;
	}

	if (inv_file) {
		any_available = false;
		cifsFileInfo_get(inv_file);
	}

	spin_unlock(&cifs_inode->open_file_lock);

	if (inv_file) {
		rc = cifs_reopen_file(inv_file, false);
		if (!rc) {
			*ret_file = inv_file;
			return 0;
		}

		spin_lock(&cifs_inode->open_file_lock);
		list_move_tail(&inv_file->flist, &cifs_inode->openFileList);
		spin_unlock(&cifs_inode->open_file_lock);
		cifsFileInfo_put(inv_file);
		++refind;
		inv_file = NULL;
		spin_lock(&cifs_inode->open_file_lock);
		goto refind_writable;
	}

	return rc;
}

struct cifsFileInfo *
find_writable_file(struct cifsInodeInfo *cifs_inode, int flags)
{
	struct cifsFileInfo *cfile;
	int rc;

	rc = cifs_get_writable_file(cifs_inode, flags, &cfile);
	if (rc)
		cifs_dbg(FYI, "Couldn't find writable handle rc=%d\n", rc);

	return cfile;
}

int
cifs_get_writable_path(struct cifs_tcon *tcon, const char *name,
		       int flags,
		       struct cifsFileInfo **ret_file)
{
	struct cifsFileInfo *cfile;
	void *page = alloc_dentry_path();

	*ret_file = NULL;

	spin_lock(&tcon->open_file_lock);
	list_for_each_entry(cfile, &tcon->openFileList, tlist) {
		struct cifsInodeInfo *cinode;
		const char *full_path = build_path_from_dentry(cfile->dentry, page);
		if (IS_ERR(full_path)) {
			spin_unlock(&tcon->open_file_lock);
			free_dentry_path(page);
			return PTR_ERR(full_path);
		}
		if (strcmp(full_path, name))
			continue;

		cinode = CIFS_I(d_inode(cfile->dentry));
		spin_unlock(&tcon->open_file_lock);
		free_dentry_path(page);
		return cifs_get_writable_file(cinode, flags, ret_file);
	}

	spin_unlock(&tcon->open_file_lock);
	free_dentry_path(page);
	return -ENOENT;
}

int
cifs_get_readable_path(struct cifs_tcon *tcon, const char *name,
		       struct cifsFileInfo **ret_file)
{
	struct cifsFileInfo *cfile;
	void *page = alloc_dentry_path();

	*ret_file = NULL;

	spin_lock(&tcon->open_file_lock);
	list_for_each_entry(cfile, &tcon->openFileList, tlist) {
		struct cifsInodeInfo *cinode;
		const char *full_path = build_path_from_dentry(cfile->dentry, page);
		if (IS_ERR(full_path)) {
			spin_unlock(&tcon->open_file_lock);
			free_dentry_path(page);
			return PTR_ERR(full_path);
		}
		if (strcmp(full_path, name))
			continue;

		cinode = CIFS_I(d_inode(cfile->dentry));
		spin_unlock(&tcon->open_file_lock);
		free_dentry_path(page);
		*ret_file = find_readable_file(cinode, 0);
		return *ret_file ? 0 : -ENOENT;
	}

	spin_unlock(&tcon->open_file_lock);
	free_dentry_path(page);
	return -ENOENT;
}

/*
 * Flush data on a strict file.
 */
int cifs_strict_fsync(struct file *file, loff_t start, loff_t end,
		      int datasync)
{
	unsigned int xid;
	int rc = 0;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	struct cifsFileInfo *smbfile = file->private_data;
	struct inode *inode = file_inode(file);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);

	rc = file_write_and_wait_range(file, start, end);
	if (rc) {
		trace_cifs_fsync_err(inode->i_ino, rc);
		return rc;
	}

	xid = get_xid();

	cifs_dbg(FYI, "Sync file - name: %pD datasync: 0x%x\n",
		 file, datasync);

	if (!CIFS_CACHE_READ(CIFS_I(inode))) {
		rc = cifs_zap_mapping(inode);
		if (rc) {
			cifs_dbg(FYI, "rc: %d during invalidate phase\n", rc);
			rc = 0; /* don't care about it in fsync */
		}
	}

	tcon = tlink_tcon(smbfile->tlink);
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOSSYNC)) {
		server = tcon->ses->server;
		if (server->ops->flush == NULL) {
			rc = -ENOSYS;
			goto strict_fsync_exit;
		}

		if ((OPEN_FMODE(smbfile->f_flags) & FMODE_WRITE) == 0) {
			smbfile = find_writable_file(CIFS_I(inode), FIND_WR_ANY);
			if (smbfile) {
				rc = server->ops->flush(xid, tcon, &smbfile->fid);
				cifsFileInfo_put(smbfile);
			} else
				cifs_dbg(FYI, "ignore fsync for file not open for write\n");
		} else
			rc = server->ops->flush(xid, tcon, &smbfile->fid);
	}

strict_fsync_exit:
	free_xid(xid);
	return rc;
}

/*
 * Flush data on a non-strict data.
 */
int cifs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	unsigned int xid;
	int rc = 0;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	struct cifsFileInfo *smbfile = file->private_data;
	struct inode *inode = file_inode(file);
	struct cifs_sb_info *cifs_sb = CIFS_FILE_SB(file);

	rc = file_write_and_wait_range(file, start, end);
	if (rc) {
		trace_cifs_fsync_err(file_inode(file)->i_ino, rc);
		return rc;
	}

	xid = get_xid();

	cifs_dbg(FYI, "Sync file - name: %pD datasync: 0x%x\n",
		 file, datasync);

	tcon = tlink_tcon(smbfile->tlink);
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOSSYNC)) {
		server = tcon->ses->server;
		if (server->ops->flush == NULL) {
			rc = -ENOSYS;
			goto fsync_exit;
		}

		if ((OPEN_FMODE(smbfile->f_flags) & FMODE_WRITE) == 0) {
			smbfile = find_writable_file(CIFS_I(inode), FIND_WR_ANY);
			if (smbfile) {
				rc = server->ops->flush(xid, tcon, &smbfile->fid);
				cifsFileInfo_put(smbfile);
			} else
				cifs_dbg(FYI, "ignore fsync for file not open for write\n");
		} else
			rc = server->ops->flush(xid, tcon, &smbfile->fid);
	}

fsync_exit:
	free_xid(xid);
	return rc;
}

/*
 * As file closes, flush all cached write data for this inode checking
 * for write behind errors.
 */
int cifs_flush(struct file *file, fl_owner_t id)
{
	struct inode *inode = file_inode(file);
	int rc = 0;

	if (file->f_mode & FMODE_WRITE)
		rc = filemap_write_and_wait(inode->i_mapping);

	cifs_dbg(FYI, "Flush inode %p file %p rc %d\n", inode, file, rc);
	if (rc) {
		/* get more nuanced writeback errors */
		rc = filemap_check_wb_err(file->f_mapping, 0);
		trace_cifs_flush_err(inode->i_ino, rc);
	}
	return rc;
}

static ssize_t
cifs_writev(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct cifsFileInfo *cfile = (struct cifsFileInfo *)file->private_data;
	struct inode *inode = file->f_mapping->host;
	struct cifsInodeInfo *cinode = CIFS_I(inode);
	struct TCP_Server_Info *server = tlink_tcon(cfile->tlink)->ses->server;
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	ssize_t rc;

	rc = netfs_start_io_write(inode);
	if (rc < 0)
		return rc;

	/*
	 * We need to hold the sem to be sure nobody modifies lock list
	 * with a brlock that prevents writing.
	 */
	down_read(&cinode->lock_sem);

	rc = generic_write_checks(iocb, from);
	if (rc <= 0)
		goto out;

	if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOPOSIXBRL) &&
	    (cifs_find_lock_conflict(cfile, iocb->ki_pos, iov_iter_count(from),
				     server->vals->exclusive_lock_type, 0,
				     NULL, CIFS_WRITE_OP))) {
		rc = -EACCES;
		goto out;
	}

	rc = netfs_buffered_write_iter_locked(iocb, from, NULL);

out:
	up_read(&cinode->lock_sem);
	netfs_end_io_write(inode);
	if (rc > 0)
		rc = generic_write_sync(iocb, rc);
	return rc;
}

ssize_t
cifs_strict_writev(struct kiocb *iocb, struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct cifsInodeInfo *cinode = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifsFileInfo *cfile = (struct cifsFileInfo *)
						iocb->ki_filp->private_data;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	ssize_t written;

	written = cifs_get_writer(cinode);
	if (written)
		return written;

	if (CIFS_CACHE_WRITE(cinode)) {
		if (cap_unix(tcon->ses) &&
		    (CIFS_UNIX_FCNTL_CAP & le64_to_cpu(tcon->fsUnixInfo.Capability)) &&
		    ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOPOSIXBRL) == 0)) {
			written = netfs_file_write_iter(iocb, from);
			goto out;
		}
		written = cifs_writev(iocb, from);
		goto out;
	}
	/*
	 * For non-oplocked files in strict cache mode we need to write the data
	 * to the server exactly from the pos to pos+len-1 rather than flush all
	 * affected pages because it may cause a error with mandatory locks on
	 * these pages but not on the region from pos to ppos+len-1.
	 */
	written = netfs_file_write_iter(iocb, from);
	if (CIFS_CACHE_READ(cinode)) {
		/*
		 * We have read level caching and we have just sent a write
		 * request to the server thus making data in the cache stale.
		 * Zap the cache and set oplock/lease level to NONE to avoid
		 * reading stale data from the cache. All subsequent read
		 * operations will read new data from the server.
		 */
		cifs_zap_mapping(inode);
		cifs_dbg(FYI, "Set Oplock/Lease to NONE for inode=%p after write\n",
			 inode);
		cinode->oplock = 0;
	}
out:
	cifs_put_writer(cinode);
	return written;
}

ssize_t cifs_loose_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	ssize_t rc;
	struct inode *inode = file_inode(iocb->ki_filp);

	if (iocb->ki_flags & IOCB_DIRECT)
		return netfs_unbuffered_read_iter(iocb, iter);

	rc = cifs_revalidate_mapping(inode);
	if (rc)
		return rc;

	return netfs_file_read_iter(iocb, iter);
}

ssize_t cifs_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct cifsInodeInfo *cinode = CIFS_I(inode);
	ssize_t written;
	int rc;

	if (iocb->ki_filp->f_flags & O_DIRECT) {
		written = netfs_unbuffered_write_iter(iocb, from);
		if (written > 0 && CIFS_CACHE_READ(cinode)) {
			cifs_zap_mapping(inode);
			cifs_dbg(FYI,
				 "Set no oplock for inode=%p after a write operation\n",
				 inode);
			cinode->oplock = 0;
		}
		return written;
	}

	written = cifs_get_writer(cinode);
	if (written)
		return written;

	written = netfs_file_write_iter(iocb, from);

	if (!CIFS_CACHE_WRITE(CIFS_I(inode))) {
		rc = filemap_fdatawrite(inode->i_mapping);
		if (rc)
			cifs_dbg(FYI, "cifs_file_write_iter: %d rc on %p inode\n",
				 rc, inode);
	}

	cifs_put_writer(cinode);
	return written;
}

ssize_t
cifs_strict_readv(struct kiocb *iocb, struct iov_iter *to)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct cifsInodeInfo *cinode = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifsFileInfo *cfile = (struct cifsFileInfo *)
						iocb->ki_filp->private_data;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	int rc = -EACCES;

	/*
	 * In strict cache mode we need to read from the server all the time
	 * if we don't have level II oplock because the server can delay mtime
	 * change - so we can't make a decision about inode invalidating.
	 * And we can also fail with pagereading if there are mandatory locks
	 * on pages affected by this read but not on the region from pos to
	 * pos+len-1.
	 */
	if (!CIFS_CACHE_READ(cinode))
		return netfs_unbuffered_read_iter(iocb, to);

	if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOPOSIXBRL) == 0) {
		if (iocb->ki_flags & IOCB_DIRECT)
			return netfs_unbuffered_read_iter(iocb, to);
		return netfs_buffered_read_iter(iocb, to);
	}

	/*
	 * We need to hold the sem to be sure nobody modifies lock list
	 * with a brlock that prevents reading.
	 */
	if (iocb->ki_flags & IOCB_DIRECT) {
		rc = netfs_start_io_direct(inode);
		if (rc < 0)
			goto out;
		rc = -EACCES;
		down_read(&cinode->lock_sem);
		if (!cifs_find_lock_conflict(
			    cfile, iocb->ki_pos, iov_iter_count(to),
			    tcon->ses->server->vals->shared_lock_type,
			    0, NULL, CIFS_READ_OP))
			rc = netfs_unbuffered_read_iter_locked(iocb, to);
		up_read(&cinode->lock_sem);
		netfs_end_io_direct(inode);
	} else {
		rc = netfs_start_io_read(inode);
		if (rc < 0)
			goto out;
		rc = -EACCES;
		down_read(&cinode->lock_sem);
		if (!cifs_find_lock_conflict(
			    cfile, iocb->ki_pos, iov_iter_count(to),
			    tcon->ses->server->vals->shared_lock_type,
			    0, NULL, CIFS_READ_OP))
			rc = filemap_read(iocb, to, 0);
		up_read(&cinode->lock_sem);
		netfs_end_io_read(inode);
	}
out:
	return rc;
}

static vm_fault_t cifs_page_mkwrite(struct vm_fault *vmf)
{
	return netfs_page_mkwrite(vmf, NULL);
}

static const struct vm_operations_struct cifs_file_vm_ops = {
	.fault = filemap_fault,
	.map_pages = filemap_map_pages,
	.page_mkwrite = cifs_page_mkwrite,
};

int cifs_file_strict_mmap(struct file *file, struct vm_area_struct *vma)
{
	int xid, rc = 0;
	struct inode *inode = file_inode(file);

	xid = get_xid();

	if (!CIFS_CACHE_READ(CIFS_I(inode)))
		rc = cifs_zap_mapping(inode);
	if (!rc)
		rc = generic_file_mmap(file, vma);
	if (!rc)
		vma->vm_ops = &cifs_file_vm_ops;

	free_xid(xid);
	return rc;
}

int cifs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	int rc, xid;

	xid = get_xid();

	rc = cifs_revalidate_file(file);
	if (rc)
		cifs_dbg(FYI, "Validation prior to mmap failed, error=%d\n",
			 rc);
	if (!rc)
		rc = generic_file_mmap(file, vma);
	if (!rc)
		vma->vm_ops = &cifs_file_vm_ops;

	free_xid(xid);
	return rc;
}

static int is_inode_writable(struct cifsInodeInfo *cifs_inode)
{
	struct cifsFileInfo *open_file;

	spin_lock(&cifs_inode->open_file_lock);
	list_for_each_entry(open_file, &cifs_inode->openFileList, flist) {
		if (OPEN_FMODE(open_file->f_flags) & FMODE_WRITE) {
			spin_unlock(&cifs_inode->open_file_lock);
			return 1;
		}
	}
	spin_unlock(&cifs_inode->open_file_lock);
	return 0;
}

/* We do not want to update the file size from server for inodes
   open for write - to avoid races with writepage extending
   the file - in the future we could consider allowing
   refreshing the inode only on increases in the file size
   but this is tricky to do without racing with writebehind
   page caching in the current Linux kernel design */
bool is_size_safe_to_change(struct cifsInodeInfo *cifsInode, __u64 end_of_file,
			    bool from_readdir)
{
	if (!cifsInode)
		return true;

	if (is_inode_writable(cifsInode) ||
		((cifsInode->oplock & CIFS_CACHE_RW_FLG) != 0 && from_readdir)) {
		/* This inode is open for write at least once */
		struct cifs_sb_info *cifs_sb;

		cifs_sb = CIFS_SB(cifsInode->netfs.inode.i_sb);
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DIRECT_IO) {
			/* since no page cache to corrupt on directio
			we can change size safely */
			return true;
		}

		if (i_size_read(&cifsInode->netfs.inode) < end_of_file)
			return true;

		return false;
	} else
		return true;
}

void cifs_oplock_break(struct work_struct *work)
{
	struct cifsFileInfo *cfile = container_of(work, struct cifsFileInfo,
						  oplock_break);
	struct inode *inode = d_inode(cfile->dentry);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifsInodeInfo *cinode = CIFS_I(inode);
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	struct tcon_link *tlink;
	int rc = 0;
	bool purge_cache = false, oplock_break_cancelled;
	__u64 persistent_fid, volatile_fid;
	__u16 net_fid;

	wait_on_bit(&cinode->flags, CIFS_INODE_PENDING_WRITERS,
			TASK_UNINTERRUPTIBLE);

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		goto out;
	tcon = tlink_tcon(tlink);
	server = tcon->ses->server;

	server->ops->downgrade_oplock(server, cinode, cfile->oplock_level,
				      cfile->oplock_epoch, &purge_cache);

	if (!CIFS_CACHE_WRITE(cinode) && CIFS_CACHE_READ(cinode) &&
						cifs_has_mand_locks(cinode)) {
		cifs_dbg(FYI, "Reset oplock to None for inode=%p due to mand locks\n",
			 inode);
		cinode->oplock = 0;
	}

	if (inode && S_ISREG(inode->i_mode)) {
		if (CIFS_CACHE_READ(cinode))
			break_lease(inode, O_RDONLY);
		else
			break_lease(inode, O_WRONLY);
		rc = filemap_fdatawrite(inode->i_mapping);
		if (!CIFS_CACHE_READ(cinode) || purge_cache) {
			rc = filemap_fdatawait(inode->i_mapping);
			mapping_set_error(inode->i_mapping, rc);
			cifs_zap_mapping(inode);
		}
		cifs_dbg(FYI, "Oplock flush inode %p rc %d\n", inode, rc);
		if (CIFS_CACHE_WRITE(cinode))
			goto oplock_break_ack;
	}

	rc = cifs_push_locks(cfile);
	if (rc)
		cifs_dbg(VFS, "Push locks rc = %d\n", rc);

oplock_break_ack:
	/*
	 * When oplock break is received and there are no active
	 * file handles but cached, then schedule deferred close immediately.
	 * So, new open will not use cached handle.
	 */

	if (!CIFS_CACHE_HANDLE(cinode) && !list_empty(&cinode->deferred_closes))
		cifs_close_deferred_file(cinode);

	persistent_fid = cfile->fid.persistent_fid;
	volatile_fid = cfile->fid.volatile_fid;
	net_fid = cfile->fid.netfid;
	oplock_break_cancelled = cfile->oplock_break_cancelled;

	_cifsFileInfo_put(cfile, false /* do not wait for ourself */, false);
	/*
	 * MS-SMB2 3.2.5.19.1 and 3.2.5.19.2 (and MS-CIFS 3.2.5.42) do not require
	 * an acknowledgment to be sent when the file has already been closed.
	 */
	spin_lock(&cinode->open_file_lock);
	/* check list empty since can race with kill_sb calling tree disconnect */
	if (!oplock_break_cancelled && !list_empty(&cinode->openFileList)) {
		spin_unlock(&cinode->open_file_lock);
		rc = server->ops->oplock_response(tcon, persistent_fid,
						  volatile_fid, net_fid, cinode);
		cifs_dbg(FYI, "Oplock release rc = %d\n", rc);
	} else
		spin_unlock(&cinode->open_file_lock);

	cifs_put_tlink(tlink);
out:
	cifs_done_oplock_break(cinode);
}

static int cifs_swap_activate(struct swap_info_struct *sis,
			      struct file *swap_file, sector_t *span)
{
	struct cifsFileInfo *cfile = swap_file->private_data;
	struct inode *inode = swap_file->f_mapping->host;
	unsigned long blocks;
	long long isize;

	cifs_dbg(FYI, "swap activate\n");

	if (!swap_file->f_mapping->a_ops->swap_rw)
		/* Cannot support swap */
		return -EINVAL;

	spin_lock(&inode->i_lock);
	blocks = inode->i_blocks;
	isize = inode->i_size;
	spin_unlock(&inode->i_lock);
	if (blocks*512 < isize) {
		pr_warn("swap activate: swapfile has holes\n");
		return -EINVAL;
	}
	*span = sis->pages;

	pr_warn_once("Swap support over SMB3 is experimental\n");

	/*
	 * TODO: consider adding ACL (or documenting how) to prevent other
	 * users (on this or other systems) from reading it
	 */


	/* TODO: add sk_set_memalloc(inet) or similar */

	if (cfile)
		cfile->swapfile = true;
	/*
	 * TODO: Since file already open, we can't open with DENY_ALL here
	 * but we could add call to grab a byte range lock to prevent others
	 * from reading or writing the file
	 */

	sis->flags |= SWP_FS_OPS;
	return add_swap_extent(sis, 0, sis->max, 0);
}

static void cifs_swap_deactivate(struct file *file)
{
	struct cifsFileInfo *cfile = file->private_data;

	cifs_dbg(FYI, "swap deactivate\n");

	/* TODO: undo sk_set_memalloc(inet) will eventually be needed */

	if (cfile)
		cfile->swapfile = false;

	/* do we need to unpin (or unlock) the file */
}

/**
 * cifs_swap_rw - SMB3 address space operation for swap I/O
 * @iocb: target I/O control block
 * @iter: I/O buffer
 *
 * Perform IO to the swap-file.  This is much like direct IO.
 */
static int cifs_swap_rw(struct kiocb *iocb, struct iov_iter *iter)
{
	ssize_t ret;

	if (iov_iter_rw(iter) == READ)
		ret = netfs_unbuffered_read_iter_locked(iocb, iter);
	else
		ret = netfs_unbuffered_write_iter_locked(iocb, iter, NULL);
	if (ret < 0)
		return ret;
	return 0;
}

const struct address_space_operations cifs_addr_ops = {
	.read_folio	= netfs_read_folio,
	.readahead	= netfs_readahead,
	.writepages	= netfs_writepages,
	.dirty_folio	= netfs_dirty_folio,
	.release_folio	= netfs_release_folio,
	.direct_IO	= noop_direct_IO,
	.invalidate_folio = netfs_invalidate_folio,
	.migrate_folio	= filemap_migrate_folio,
	/*
	 * TODO: investigate and if useful we could add an is_dirty_writeback
	 * helper if needed
	 */
	.swap_activate	= cifs_swap_activate,
	.swap_deactivate = cifs_swap_deactivate,
	.swap_rw = cifs_swap_rw,
};

/*
 * cifs_readahead requires the server to support a buffer large enough to
 * contain the header plus one complete page of data.  Otherwise, we need
 * to leave cifs_readahead out of the address space operations.
 */
const struct address_space_operations cifs_addr_ops_smallbuf = {
	.read_folio	= netfs_read_folio,
	.writepages	= netfs_writepages,
	.dirty_folio	= netfs_dirty_folio,
	.release_folio	= netfs_release_folio,
	.invalidate_folio = netfs_invalidate_folio,
	.migrate_folio	= filemap_migrate_folio,
};
