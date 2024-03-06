// SPDX-License-Identifier: GPL-2.0
/*
 *  Functions to handle the cached directory entries
 *
 *  Copyright (c) 2022, Ronnie Sahlberg <lsahlber@redhat.com>
 */

#include <linux/namei.h>
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "smb2proto.h"
#include "cached_dir.h"

static struct cached_fid *init_cached_dir(const char *path);
static void free_cached_dir(struct cached_fid *cfid);
static void smb2_close_cached_fid(struct kref *ref);
static void cfids_laundromat_worker(struct work_struct *work);

static struct cached_fid *find_or_create_cached_dir(struct cached_fids *cfids,
						    const char *path,
						    bool lookup_only,
						    __u32 max_cached_dirs)
{
	struct cached_fid *cfid;

	spin_lock(&cfids->cfid_list_lock);
	list_for_each_entry(cfid, &cfids->entries, entry) {
		if (!strcmp(cfid->path, path)) {
			/*
			 * If it doesn't have a lease it is either not yet
			 * fully cached or it may be in the process of
			 * being deleted due to a lease break.
			 */
			if (!cfid->time || !cfid->has_lease) {
				spin_unlock(&cfids->cfid_list_lock);
				return NULL;
			}
			kref_get(&cfid->refcount);
			spin_unlock(&cfids->cfid_list_lock);
			return cfid;
		}
	}
	if (lookup_only) {
		spin_unlock(&cfids->cfid_list_lock);
		return NULL;
	}
	if (cfids->num_entries >= max_cached_dirs) {
		spin_unlock(&cfids->cfid_list_lock);
		return NULL;
	}
	cfid = init_cached_dir(path);
	if (cfid == NULL) {
		spin_unlock(&cfids->cfid_list_lock);
		return NULL;
	}
	cfid->cfids = cfids;
	cfids->num_entries++;
	list_add(&cfid->entry, &cfids->entries);
	cfid->on_list = true;
	kref_get(&cfid->refcount);
	spin_unlock(&cfids->cfid_list_lock);
	return cfid;
}

static struct dentry *
path_to_dentry(struct cifs_sb_info *cifs_sb, const char *path)
{
	struct dentry *dentry;
	const char *s, *p;
	char sep;

	sep = CIFS_DIR_SEP(cifs_sb);
	dentry = dget(cifs_sb->root);
	s = path;

	do {
		struct inode *dir = d_inode(dentry);
		struct dentry *child;

		if (!S_ISDIR(dir->i_mode)) {
			dput(dentry);
			dentry = ERR_PTR(-ENOTDIR);
			break;
		}

		/* skip separators */
		while (*s == sep)
			s++;
		if (!*s)
			break;
		p = s++;
		/* next separator */
		while (*s && *s != sep)
			s++;

		child = lookup_positive_unlocked(p, dentry, s - p);
		dput(dentry);
		dentry = child;
	} while (!IS_ERR(dentry));
	return dentry;
}

static const char *path_no_prefix(struct cifs_sb_info *cifs_sb,
				  const char *path)
{
	size_t len = 0;

	if (!*path)
		return path;

	if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_USE_PREFIX_PATH) &&
	    cifs_sb->prepath) {
		len = strlen(cifs_sb->prepath) + 1;
		if (unlikely(len > strlen(path)))
			return ERR_PTR(-EINVAL);
	}
	return path + len;
}

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
	__le16 *utf16_path = NULL;
	u8 oplock = SMB2_OPLOCK_LEVEL_II;
	struct cifs_fid *pfid;
	struct dentry *dentry = NULL;
	struct cached_fid *cfid;
	struct cached_fids *cfids;
	const char *npath;
	int retries = 0, cur_sleep = 1;

	if (tcon == NULL || tcon->cfids == NULL || tcon->nohandlecache ||
	    is_smb1_server(tcon->ses->server) || (dir_cache_timeout == 0))
		return -EOPNOTSUPP;

	ses = tcon->ses;
	cfids = tcon->cfids;

	if (cifs_sb->root == NULL)
		return -ENOENT;

replay_again:
	/* reinitialize for possible replay */
	flags = 0;
	oplock = SMB2_OPLOCK_LEVEL_II;
	server = cifs_pick_channel(ses);

	if (!server->ops->new_lease_key)
		return -EIO;

	utf16_path = cifs_convert_path_to_utf16(path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	cfid = find_or_create_cached_dir(cfids, path, lookup_only, tcon->max_cached_dirs);
	if (cfid == NULL) {
		kfree(utf16_path);
		return -ENOENT;
	}
	/*
	 * Return cached fid if it has a lease.  Otherwise, it is either a new
	 * entry or laundromat worker removed it from @cfids->entries.  Caller
	 * will put last reference if the latter.
	 */
	spin_lock(&cfids->cfid_list_lock);
	if (cfid->has_lease) {
		spin_unlock(&cfids->cfid_list_lock);
		*ret_cfid = cfid;
		kfree(utf16_path);
		return 0;
	}
	spin_unlock(&cfids->cfid_list_lock);

	/*
	 * Skip any prefix paths in @path as lookup_positive_unlocked() ends up
	 * calling ->lookup() which already adds those through
	 * build_path_from_dentry().  Also, do it earlier as we might reconnect
	 * below when trying to send compounded request and then potentially
	 * having a different prefix path (e.g. after DFS failover).
	 */
	npath = path_no_prefix(cifs_sb, path);
	if (IS_ERR(npath)) {
		rc = PTR_ERR(npath);
		goto out;
	}

	if (!npath[0]) {
		dentry = dget(cifs_sb->root);
	} else {
		dentry = path_to_dentry(cifs_sb, npath);
		if (IS_ERR(dentry)) {
			rc = -ENOENT;
			goto out;
		}
	}
	cfid->dentry = dentry;

	/*
	 * We do not hold the lock for the open because in case
	 * SMB2_open needs to reconnect.
	 * This is safe because no other thread will be able to get a ref
	 * to the cfid until we have finished opening the file and (possibly)
	 * acquired a lease.
	 */
	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	pfid = &cfid->fid;
	server->ops->new_lease_key(pfid);

	memset(rqst, 0, sizeof(rqst));
	resp_buftype[0] = resp_buftype[1] = CIFS_NO_BUFFER;
	memset(rsp_iov, 0, sizeof(rsp_iov));

	/* Open */
	memset(&open_iov, 0, sizeof(open_iov));
	rqst[0].rq_iov = open_iov;
	rqst[0].rq_nvec = SMB2_CREATE_IOV_SIZE;

	oparms = (struct cifs_open_parms) {
		.tcon = tcon,
		.path = path,
		.create_options = cifs_create_options(cifs_sb, CREATE_NOT_FILE),
		.desired_access =  FILE_READ_DATA | FILE_READ_ATTRIBUTES,
		.disposition = FILE_OPEN,
		.fid = pfid,
		.replay = !!(retries),
	};

	rc = SMB2_open_init(tcon, server,
			    &rqst[0], &oplock, &oparms, utf16_path);
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

	/*
	 * Set @cfid->has_lease to true before sending out compounded request so
	 * its lease reference can be put in cached_dir_lease_break() due to a
	 * potential lease break right after the request is sent or while @cfid
	 * is still being cached.  Concurrent processes won't be to use it yet
	 * due to @cfid->time being zero.
	 */
	cfid->has_lease = true;

	if (retries) {
		smb2_set_replay(server, &rqst[0]);
		smb2_set_replay(server, &rqst[1]);
	}

	rc = compound_send_recv(xid, ses, server,
				flags, 2, rqst,
				resp_buftype, rsp_iov);
	if (rc) {
		if (rc == -EREMCHG) {
			tcon->need_reconnect = true;
			pr_warn_once("server share %s deleted\n",
				     tcon->tree_name);
		}
		goto oshr_free;
	}
	cfid->tcon = tcon;
	cfid->is_open = true;

	spin_lock(&cfids->cfid_list_lock);

	o_rsp = (struct smb2_create_rsp *)rsp_iov[0].iov_base;
	oparms.fid->persistent_fid = o_rsp->PersistentFileId;
	oparms.fid->volatile_fid = o_rsp->VolatileFileId;
#ifdef CONFIG_CIFS_DEBUG2
	oparms.fid->mid = le64_to_cpu(o_rsp->hdr.MessageId);
#endif /* CIFS_DEBUG2 */


	if (o_rsp->OplockLevel != SMB2_OPLOCK_LEVEL_LEASE) {
		spin_unlock(&cfids->cfid_list_lock);
		rc = -EINVAL;
		goto oshr_free;
	}

	rc = smb2_parse_contexts(server, rsp_iov,
				 &oparms.fid->epoch,
				 oparms.fid->lease_key,
				 &oplock, NULL, NULL);
	if (rc) {
		spin_unlock(&cfids->cfid_list_lock);
		goto oshr_free;
	}

	rc = -EINVAL;
	if (!(oplock & SMB2_LEASE_READ_CACHING_HE)) {
		spin_unlock(&cfids->cfid_list_lock);
		goto oshr_free;
	}
	qi_rsp = (struct smb2_query_info_rsp *)rsp_iov[1].iov_base;
	if (le32_to_cpu(qi_rsp->OutputBufferLength) < sizeof(struct smb2_file_all_info)) {
		spin_unlock(&cfids->cfid_list_lock);
		goto oshr_free;
	}
	if (!smb2_validate_and_copy_iov(
				le16_to_cpu(qi_rsp->OutputBufferOffset),
				sizeof(struct smb2_file_all_info),
				&rsp_iov[1], sizeof(struct smb2_file_all_info),
				(char *)&cfid->file_all_info))
		cfid->file_all_info_is_valid = true;

	cfid->time = jiffies;
	spin_unlock(&cfids->cfid_list_lock);
	/* At this point the directory handle is fully cached */
	rc = 0;

oshr_free:
	SMB2_open_free(&rqst[0]);
	SMB2_query_info_free(&rqst[1]);
	free_rsp_buf(resp_buftype[0], rsp_iov[0].iov_base);
	free_rsp_buf(resp_buftype[1], rsp_iov[1].iov_base);
	if (rc) {
		spin_lock(&cfids->cfid_list_lock);
		if (cfid->on_list) {
			list_del(&cfid->entry);
			cfid->on_list = false;
			cfids->num_entries--;
		}
		if (cfid->has_lease) {
			/*
			 * We are guaranteed to have two references at this
			 * point. One for the caller and one for a potential
			 * lease. Release the Lease-ref so that the directory
			 * will be closed when the caller closes the cached
			 * handle.
			 */
			cfid->has_lease = false;
			spin_unlock(&cfids->cfid_list_lock);
			kref_put(&cfid->refcount, smb2_close_cached_fid);
			goto out;
		}
		spin_unlock(&cfids->cfid_list_lock);
	}
out:
	if (rc) {
		if (cfid->is_open)
			SMB2_close(0, cfid->tcon, cfid->fid.persistent_fid,
				   cfid->fid.volatile_fid);
		free_cached_dir(cfid);
	} else {
		*ret_cfid = cfid;
		atomic_inc(&tcon->num_remote_opens);
	}
	kfree(utf16_path);

	if (is_replayable_error(rc) &&
	    smb2_should_replay(tcon, &retries, &cur_sleep))
		goto replay_again;

	return rc;
}

int open_cached_dir_by_dentry(struct cifs_tcon *tcon,
			      struct dentry *dentry,
			      struct cached_fid **ret_cfid)
{
	struct cached_fid *cfid;
	struct cached_fids *cfids = tcon->cfids;

	if (cfids == NULL)
		return -ENOENT;

	spin_lock(&cfids->cfid_list_lock);
	list_for_each_entry(cfid, &cfids->entries, entry) {
		if (dentry && cfid->dentry == dentry) {
			cifs_dbg(FYI, "found a cached root file handle by dentry\n");
			kref_get(&cfid->refcount);
			*ret_cfid = cfid;
			spin_unlock(&cfids->cfid_list_lock);
			return 0;
		}
	}
	spin_unlock(&cfids->cfid_list_lock);
	return -ENOENT;
}

static void
smb2_close_cached_fid(struct kref *ref)
{
	struct cached_fid *cfid = container_of(ref, struct cached_fid,
					       refcount);

	spin_lock(&cfid->cfids->cfid_list_lock);
	if (cfid->on_list) {
		list_del(&cfid->entry);
		cfid->on_list = false;
		cfid->cfids->num_entries--;
	}
	spin_unlock(&cfid->cfids->cfid_list_lock);

	dput(cfid->dentry);
	cfid->dentry = NULL;

	if (cfid->is_open) {
		SMB2_close(0, cfid->tcon, cfid->fid.persistent_fid,
			   cfid->fid.volatile_fid);
		atomic_dec(&cfid->tcon->num_remote_opens);
	}

	free_cached_dir(cfid);
}

void drop_cached_dir_by_name(const unsigned int xid, struct cifs_tcon *tcon,
			     const char *name, struct cifs_sb_info *cifs_sb)
{
	struct cached_fid *cfid = NULL;
	int rc;

	rc = open_cached_dir(xid, tcon, name, cifs_sb, true, &cfid);
	if (rc) {
		cifs_dbg(FYI, "no cached dir found for rmdir(%s)\n", name);
		return;
	}
	spin_lock(&cfid->cfids->cfid_list_lock);
	if (cfid->has_lease) {
		cfid->has_lease = false;
		kref_put(&cfid->refcount, smb2_close_cached_fid);
	}
	spin_unlock(&cfid->cfids->cfid_list_lock);
	close_cached_dir(cfid);
}


void close_cached_dir(struct cached_fid *cfid)
{
	kref_put(&cfid->refcount, smb2_close_cached_fid);
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
	struct cached_fids *cfids;

	for (node = rb_first(root); node; node = rb_next(node)) {
		tlink = rb_entry(node, struct tcon_link, tl_rbnode);
		tcon = tlink_tcon(tlink);
		if (IS_ERR(tcon))
			continue;
		cfids = tcon->cfids;
		if (cfids == NULL)
			continue;
		list_for_each_entry(cfid, &cfids->entries, entry) {
			dput(cfid->dentry);
			cfid->dentry = NULL;
		}
	}
}

/*
 * Invalidate all cached dirs when a TCON has been reset
 * due to a session loss.
 */
void invalidate_all_cached_dirs(struct cifs_tcon *tcon)
{
	struct cached_fids *cfids = tcon->cfids;
	struct cached_fid *cfid, *q;
	LIST_HEAD(entry);

	if (cfids == NULL)
		return;

	spin_lock(&cfids->cfid_list_lock);
	list_for_each_entry_safe(cfid, q, &cfids->entries, entry) {
		list_move(&cfid->entry, &entry);
		cfids->num_entries--;
		cfid->is_open = false;
		cfid->on_list = false;
		/* To prevent race with smb2_cached_lease_break() */
		kref_get(&cfid->refcount);
	}
	spin_unlock(&cfids->cfid_list_lock);

	list_for_each_entry_safe(cfid, q, &entry, entry) {
		list_del(&cfid->entry);
		cancel_work_sync(&cfid->lease_break);
		if (cfid->has_lease) {
			/*
			 * We lease was never cancelled from the server so we
			 * need to drop the reference.
			 */
			spin_lock(&cfids->cfid_list_lock);
			cfid->has_lease = false;
			spin_unlock(&cfids->cfid_list_lock);
			kref_put(&cfid->refcount, smb2_close_cached_fid);
		}
		/* Drop the extra reference opened above*/
		kref_put(&cfid->refcount, smb2_close_cached_fid);
	}
}

static void
smb2_cached_lease_break(struct work_struct *work)
{
	struct cached_fid *cfid = container_of(work,
				struct cached_fid, lease_break);

	spin_lock(&cfid->cfids->cfid_list_lock);
	cfid->has_lease = false;
	spin_unlock(&cfid->cfids->cfid_list_lock);
	kref_put(&cfid->refcount, smb2_close_cached_fid);
}

int cached_dir_lease_break(struct cifs_tcon *tcon, __u8 lease_key[16])
{
	struct cached_fids *cfids = tcon->cfids;
	struct cached_fid *cfid;

	if (cfids == NULL)
		return false;

	spin_lock(&cfids->cfid_list_lock);
	list_for_each_entry(cfid, &cfids->entries, entry) {
		if (cfid->has_lease &&
		    !memcmp(lease_key,
			    cfid->fid.lease_key,
			    SMB2_LEASE_KEY_SIZE)) {
			cfid->time = 0;
			/*
			 * We found a lease remove it from the list
			 * so no threads can access it.
			 */
			list_del(&cfid->entry);
			cfid->on_list = false;
			cfids->num_entries--;

			queue_work(cifsiod_wq,
				   &cfid->lease_break);
			spin_unlock(&cfids->cfid_list_lock);
			return true;
		}
	}
	spin_unlock(&cfids->cfid_list_lock);
	return false;
}

static struct cached_fid *init_cached_dir(const char *path)
{
	struct cached_fid *cfid;

	cfid = kzalloc(sizeof(*cfid), GFP_ATOMIC);
	if (!cfid)
		return NULL;
	cfid->path = kstrdup(path, GFP_ATOMIC);
	if (!cfid->path) {
		kfree(cfid);
		return NULL;
	}

	INIT_WORK(&cfid->lease_break, smb2_cached_lease_break);
	INIT_LIST_HEAD(&cfid->entry);
	INIT_LIST_HEAD(&cfid->dirents.entries);
	mutex_init(&cfid->dirents.de_mutex);
	spin_lock_init(&cfid->fid_lock);
	kref_init(&cfid->refcount);
	return cfid;
}

static void free_cached_dir(struct cached_fid *cfid)
{
	struct cached_dirent *dirent, *q;

	dput(cfid->dentry);
	cfid->dentry = NULL;

	/*
	 * Delete all cached dirent names
	 */
	list_for_each_entry_safe(dirent, q, &cfid->dirents.entries, entry) {
		list_del(&dirent->entry);
		kfree(dirent->name);
		kfree(dirent);
	}

	kfree(cfid->path);
	cfid->path = NULL;
	kfree(cfid);
}

static void cfids_laundromat_worker(struct work_struct *work)
{
	struct cached_fids *cfids;
	struct cached_fid *cfid, *q;
	LIST_HEAD(entry);

	cfids = container_of(work, struct cached_fids, laundromat_work.work);

	spin_lock(&cfids->cfid_list_lock);
	list_for_each_entry_safe(cfid, q, &cfids->entries, entry) {
		if (cfid->time &&
		    time_after(jiffies, cfid->time + HZ * dir_cache_timeout)) {
			cfid->on_list = false;
			list_move(&cfid->entry, &entry);
			cfids->num_entries--;
			/* To prevent race with smb2_cached_lease_break() */
			kref_get(&cfid->refcount);
		}
	}
	spin_unlock(&cfids->cfid_list_lock);

	list_for_each_entry_safe(cfid, q, &entry, entry) {
		list_del(&cfid->entry);
		/*
		 * Cancel and wait for the work to finish in case we are racing
		 * with it.
		 */
		cancel_work_sync(&cfid->lease_break);
		if (cfid->has_lease) {
			/*
			 * Our lease has not yet been cancelled from the server
			 * so we need to drop the reference.
			 */
			spin_lock(&cfids->cfid_list_lock);
			cfid->has_lease = false;
			spin_unlock(&cfids->cfid_list_lock);
			kref_put(&cfid->refcount, smb2_close_cached_fid);
		}
		/* Drop the extra reference opened above */
		kref_put(&cfid->refcount, smb2_close_cached_fid);
	}
	queue_delayed_work(cifsiod_wq, &cfids->laundromat_work,
			   dir_cache_timeout * HZ);
}

struct cached_fids *init_cached_dirs(void)
{
	struct cached_fids *cfids;

	cfids = kzalloc(sizeof(*cfids), GFP_KERNEL);
	if (!cfids)
		return NULL;
	spin_lock_init(&cfids->cfid_list_lock);
	INIT_LIST_HEAD(&cfids->entries);

	INIT_DELAYED_WORK(&cfids->laundromat_work, cfids_laundromat_worker);
	queue_delayed_work(cifsiod_wq, &cfids->laundromat_work,
			   dir_cache_timeout * HZ);

	return cfids;
}

/*
 * Called from tconInfoFree when we are tearing down the tcon.
 * There are no active users or open files/directories at this point.
 */
void free_cached_dirs(struct cached_fids *cfids)
{
	struct cached_fid *cfid, *q;
	LIST_HEAD(entry);

	if (cfids == NULL)
		return;

	cancel_delayed_work_sync(&cfids->laundromat_work);

	spin_lock(&cfids->cfid_list_lock);
	list_for_each_entry_safe(cfid, q, &cfids->entries, entry) {
		cfid->on_list = false;
		cfid->is_open = false;
		list_move(&cfid->entry, &entry);
	}
	spin_unlock(&cfids->cfid_list_lock);

	list_for_each_entry_safe(cfid, q, &entry, entry) {
		list_del(&cfid->entry);
		free_cached_dir(cfid);
	}

	kfree(cfids);
}
