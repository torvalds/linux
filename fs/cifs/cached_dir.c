// SPDX-License-Identifier: GPL-2.0
/*
 *  Functions to handle the cached directory entries
 *
 *  Copyright (c) 2022, Ronnie Sahlberg <lsahlber@redhat.com>
 */

#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "smb2proto.h"
#include "cached_dir.h"

/*
 * Open the and cache a directory handle.
 * If error then *cfid is not initialized.
 */
int open_cached_dir(unsigned int xid, struct cifs_tcon *tcon,
		    const char *path,
		    struct cifs_sb_info *cifs_sb,
		    bool lookup_only, struct cached_fid **ret_cfid)
{
	struct cifs_ses *ses;
	struct TCP_Server_Info *server;
	struct cifs_open_parms oparms;
	struct smb2_create_rsp *o_rsp = NULL;
	struct smb2_query_info_rsp *qi_rsp = NULL;
	int resp_buftype[2];
	struct smb_rqst rqst[2];
	struct kvec rsp_iov[2];
	struct kvec open_iov[SMB2_CREATE_IOV_SIZE];
	struct kvec qi_iov[1];
	int rc, flags = 0;
	__le16 utf16_path = 0; /* Null - since an open of top of share */
	u8 oplock = SMB2_OPLOCK_LEVEL_II;
	struct cifs_fid *pfid;
	struct dentry *dentry;
	struct cached_fid *cfid;

	if (tcon == NULL || tcon->nohandlecache ||
	    is_smb1_server(tcon->ses->server))
		return -EOPNOTSUPP;

	ses = tcon->ses;
	server = ses->server;

	if (cifs_sb->root == NULL)
		return -ENOENT;

	if (strlen(path))
		return -ENOENT;

	dentry = cifs_sb->root;

	cfid = tcon->cfid;
	mutex_lock(&cfid->fid_mutex);
	if (cfid->is_valid) {
		cifs_dbg(FYI, "found a cached root file handle\n");
		*ret_cfid = cfid;
		kref_get(&cfid->refcount);
		mutex_unlock(&cfid->fid_mutex);
		return 0;
	}

	/*
	 * We do not hold the lock for the open because in case
	 * SMB2_open needs to reconnect, it will end up calling
	 * cifs_mark_open_files_invalid() which takes the lock again
	 * thus causing a deadlock
	 */
	mutex_unlock(&cfid->fid_mutex);

	if (lookup_only)
		return -ENOENT;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	if (!server->ops->new_lease_key)
		return -EIO;

	pfid = &cfid->fid;
	server->ops->new_lease_key(pfid);

	memset(rqst, 0, sizeof(rqst));
	resp_buftype[0] = resp_buftype[1] = CIFS_NO_BUFFER;
	memset(rsp_iov, 0, sizeof(rsp_iov));

	/* Open */
	memset(&open_iov, 0, sizeof(open_iov));
	rqst[0].rq_iov = open_iov;
	rqst[0].rq_nvec = SMB2_CREATE_IOV_SIZE;

	oparms.tcon = tcon;
	oparms.create_options = cifs_create_options(cifs_sb, CREATE_NOT_FILE);
	oparms.desired_access = FILE_READ_ATTRIBUTES;
	oparms.disposition = FILE_OPEN;
	oparms.fid = pfid;
	oparms.reconnect = false;

	rc = SMB2_open_init(tcon, server,
			    &rqst[0], &oplock, &oparms, &utf16_path);
	if (rc)
		goto oshr_free;
	smb2_set_next_command(tcon, &rqst[0]);

	memset(&qi_iov, 0, sizeof(qi_iov));
	rqst[1].rq_iov = qi_iov;
	rqst[1].rq_nvec = 1;

	rc = SMB2_query_info_init(tcon, server,
				  &rqst[1], COMPOUND_FID,
				  COMPOUND_FID, FILE_ALL_INFORMATION,
				  SMB2_O_INFO_FILE, 0,
				  sizeof(struct smb2_file_all_info) +
				  PATH_MAX * 2, 0, NULL);
	if (rc)
		goto oshr_free;

	smb2_set_related(&rqst[1]);

	rc = compound_send_recv(xid, ses, server,
				flags, 2, rqst,
				resp_buftype, rsp_iov);
	mutex_lock(&cfid->fid_mutex);

	/*
	 * Now we need to check again as the cached root might have
	 * been successfully re-opened from a concurrent process
	 */

	if (cfid->is_valid) {
		/* work was already done */

		/* stash fids for close() later */
		struct cifs_fid fid = {
			.persistent_fid = pfid->persistent_fid,
			.volatile_fid = pfid->volatile_fid,
		};

		/*
		 * caller expects this func to set the fid in cfid to valid
		 * cached root, so increment the refcount.
		 */
		kref_get(&cfid->refcount);

		mutex_unlock(&cfid->fid_mutex);

		if (rc == 0) {
			/* close extra handle outside of crit sec */
			SMB2_close(xid, tcon, fid.persistent_fid, fid.volatile_fid);
		}
		rc = 0;
		goto oshr_free;
	}

	/* Cached root is still invalid, continue normaly */

	if (rc) {
		if (rc == -EREMCHG) {
			tcon->need_reconnect = true;
			pr_warn_once("server share %s deleted\n",
				     tcon->treeName);
		}
		goto oshr_exit;
	}

	atomic_inc(&tcon->num_remote_opens);

	o_rsp = (struct smb2_create_rsp *)rsp_iov[0].iov_base;
	oparms.fid->persistent_fid = o_rsp->PersistentFileId;
	oparms.fid->volatile_fid = o_rsp->VolatileFileId;
#ifdef CONFIG_CIFS_DEBUG2
	oparms.fid->mid = le64_to_cpu(o_rsp->hdr.MessageId);
#endif /* CIFS_DEBUG2 */

	cfid->tcon = tcon;
	cfid->is_valid = true;
	cfid->dentry = dentry;
	dget(dentry);
	kref_init(&cfid->refcount);

	/* BB TBD check to see if oplock level check can be removed below */
	if (o_rsp->OplockLevel == SMB2_OPLOCK_LEVEL_LEASE) {
		/*
		 * See commit 2f94a3125b87. Increment the refcount when we
		 * get a lease for root, release it if lease break occurs
		 */
		kref_get(&cfid->refcount);
		cfid->has_lease = true;
		smb2_parse_contexts(server, o_rsp,
				&oparms.fid->epoch,
				    oparms.fid->lease_key, &oplock,
				    NULL, NULL);
	} else
		goto oshr_exit;

	qi_rsp = (struct smb2_query_info_rsp *)rsp_iov[1].iov_base;
	if (le32_to_cpu(qi_rsp->OutputBufferLength) < sizeof(struct smb2_file_all_info))
		goto oshr_exit;
	if (!smb2_validate_and_copy_iov(
				le16_to_cpu(qi_rsp->OutputBufferOffset),
				sizeof(struct smb2_file_all_info),
				&rsp_iov[1], sizeof(struct smb2_file_all_info),
				(char *)&cfid->file_all_info))
		cfid->file_all_info_is_valid = true;

	cfid->time = jiffies;

oshr_exit:
	mutex_unlock(&cfid->fid_mutex);
oshr_free:
	SMB2_open_free(&rqst[0]);
	SMB2_query_info_free(&rqst[1]);
	free_rsp_buf(resp_buftype[0], rsp_iov[0].iov_base);
	free_rsp_buf(resp_buftype[1], rsp_iov[1].iov_base);
	if (rc == 0)
		*ret_cfid = cfid;

	return rc;
}

int open_cached_dir_by_dentry(struct cifs_tcon *tcon,
			      struct dentry *dentry,
			      struct cached_fid **ret_cfid)
{
	struct cached_fid *cfid;

	cfid = tcon->cfid;

	mutex_lock(&cfid->fid_mutex);
	if (cfid->dentry == dentry) {
		cifs_dbg(FYI, "found a cached root file handle by dentry\n");
		*ret_cfid = cfid;
		kref_get(&cfid->refcount);
		mutex_unlock(&cfid->fid_mutex);
		return 0;
	}
	mutex_unlock(&cfid->fid_mutex);
	return -ENOENT;
}

static void
smb2_close_cached_fid(struct kref *ref)
{
	struct cached_fid *cfid = container_of(ref, struct cached_fid,
					       refcount);
	struct cached_dirent *dirent, *q;

	if (cfid->is_valid) {
		cifs_dbg(FYI, "clear cached root file handle\n");
		SMB2_close(0, cfid->tcon, cfid->fid.persistent_fid,
			   cfid->fid.volatile_fid);
	}

	/*
	 * We only check validity above to send SMB2_close,
	 * but we still need to invalidate these entries
	 * when this function is called
	 */
	cfid->is_valid = false;
	cfid->file_all_info_is_valid = false;
	cfid->has_lease = false;
	if (cfid->dentry) {
		dput(cfid->dentry);
		cfid->dentry = NULL;
	}
	/*
	 * Delete all cached dirent names
	 */
	mutex_lock(&cfid->dirents.de_mutex);
	list_for_each_entry_safe(dirent, q, &cfid->dirents.entries, entry) {
		list_del(&dirent->entry);
		kfree(dirent->name);
		kfree(dirent);
	}
	cfid->dirents.is_valid = 0;
	cfid->dirents.is_failed = 0;
	cfid->dirents.ctx = NULL;
	cfid->dirents.pos = 0;
	mutex_unlock(&cfid->dirents.de_mutex);

}

void close_cached_dir(struct cached_fid *cfid)
{
	mutex_lock(&cfid->fid_mutex);
	kref_put(&cfid->refcount, smb2_close_cached_fid);
	mutex_unlock(&cfid->fid_mutex);
}

void close_cached_dir_lease_locked(struct cached_fid *cfid)
{
	if (cfid->has_lease) {
		cfid->has_lease = false;
		kref_put(&cfid->refcount, smb2_close_cached_fid);
	}
}

void close_cached_dir_lease(struct cached_fid *cfid)
{
	mutex_lock(&cfid->fid_mutex);
	close_cached_dir_lease_locked(cfid);
	mutex_unlock(&cfid->fid_mutex);
}

/*
 * Called from cifs_kill_sb when we unmount a share
 */
void close_all_cached_dirs(struct cifs_sb_info *cifs_sb)
{
	struct rb_root *root = &cifs_sb->tlink_tree;
	struct rb_node *node;
	struct cached_fid *cfid;
	struct cifs_tcon *tcon;
	struct tcon_link *tlink;

	for (node = rb_first(root); node; node = rb_next(node)) {
		tlink = rb_entry(node, struct tcon_link, tl_rbnode);
		tcon = tlink_tcon(tlink);
		if (IS_ERR(tcon))
			continue;
		cfid = tcon->cfid;
		mutex_lock(&cfid->fid_mutex);
		if (cfid->dentry) {
			dput(cfid->dentry);
			cfid->dentry = NULL;
		}
		mutex_unlock(&cfid->fid_mutex);
	}
}

/*
 * Invalidate and close all cached dirs when a TCON has been reset
 * due to a session loss.
 */
void invalidate_all_cached_dirs(struct cifs_tcon *tcon)
{
	mutex_lock(&tcon->cfid->fid_mutex);
	tcon->cfid->is_valid = false;
	/* cached handle is not valid, so SMB2_CLOSE won't be sent below */
	close_cached_dir_lease_locked(tcon->cfid);
	memset(&tcon->cfid->fid, 0, sizeof(struct cifs_fid));
	mutex_unlock(&tcon->cfid->fid_mutex);
}

static void
smb2_cached_lease_break(struct work_struct *work)
{
	struct cached_fid *cfid = container_of(work,
				struct cached_fid, lease_break);

	close_cached_dir_lease(cfid);
}

int cached_dir_lease_break(struct cifs_tcon *tcon, __u8 lease_key[16])
{
	if (tcon->cfid->is_valid &&
	    !memcmp(lease_key,
		    tcon->cfid->fid.lease_key,
		    SMB2_LEASE_KEY_SIZE)) {
		tcon->cfid->time = 0;
		INIT_WORK(&tcon->cfid->lease_break,
			  smb2_cached_lease_break);
		queue_work(cifsiod_wq,
			   &tcon->cfid->lease_break);
		return true;
	}
	return false;
}

struct cached_fid *init_cached_dir(void)
{
	struct cached_fid *cfid;

	cfid = kzalloc(sizeof(*cfid), GFP_KERNEL);
	if (!cfid)
		return NULL;
	INIT_LIST_HEAD(&cfid->dirents.entries);
	mutex_init(&cfid->dirents.de_mutex);
	mutex_init(&cfid->fid_mutex);
	return cfid;
}

void free_cached_dir(struct cifs_tcon *tcon)
{
	kfree(tcon->cfid);
}
