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
static void close_cached_dir_locked(struct cached_fid *cfid);

struct cached_dir_dentry {
	struct list_head entry;
	struct dentry *dentry;
};

static struct cached_fid *find_or_create_cached_dir(struct cached_fids *cfids,
						    const char *path,
						    bool lookup_only,
						    __u32 max_cached_dirs)
{
	struct cached_fid *cfid;

	list_for_each_entry(cfid, &cfids->entries, entry) {
		if (!strcmp(cfid->path, path)) {
			/*
			 * If it doesn't have a lease it is either not yet
			 * fully cached or it may be in the process of
			 * being deleted due to a lease break.
			 */
			if (!is_valid_cached_dir(cfid))
				return NULL;
			kref_get(&cfid->refcount);
			return cfid;
		}
	}
	if (lookup_only) {
		return NULL;
	}
	if (cfids->num_entries >= max_cached_dirs) {
		return NULL;
	}
	cfid = init_cached_dir(path);
	if (cfid == NULL) {
		return NULL;
	}
	cfid->cfids = cfids;
	cfids->num_entries++;
	list_add(&cfid->entry, &cfids->entries);
	cfid->on_list = true;
	kref_get(&cfid->refcount);
	/*
	 * Set @cfid->has_lease to true during construction so that the lease
	 * reference can be put in cached_dir_lease_break() due to a potential
	 * lease break right after the request is sent or while @cfid is still
	 * being cached, or if a reconnection is triggered during construction.
	 * Concurrent processes won't be to use it yet due to @cfid->time being
	 * zero.
	 */
	cfid->has_lease = true;

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

		child = lookup_noperm_positive_unlocked(&QSTR_LEN(p, s - p),
							dentry);
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
	__le32 lease_flags = 0;

	if (cifs_sb->root == NULL)
		return -ENOENT;

	if (tcon == NULL)
		return -EOPNOTSUPP;

	ses = tcon->ses;
	cfids = tcon->cfids;

	if (cfids == NULL)
		return -EOPNOTSUPP;

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

	spin_lock(&cfids->cfid_list_lock);
	cfid = find_or_create_cached_dir(cfids, path, lookup_only, tcon->max_cached_dirs);
	if (cfid == NULL) {
		spin_unlock(&cfids->cfid_list_lock);
		kfree(utf16_path);
		return -ENOENT;
	}
	/*
	 * Return cached fid if it is valid (has a lease and has a time).
	 * Otherwise, it is either a new entry or laundromat worker removed it
	 * from @cfids->entries.  Caller will put last reference if the latter.
	 */
	if (is_valid_cached_dir(cfid)) {
		cfid->last_access_time = jiffies;
		spin_unlock(&cfids->cfid_list_lock);
		*ret_cfid = cfid;
		kfree(utf16_path);
		return 0;
	}
	spin_unlock(&cfids->cfid_list_lock);

	pfid = &cfid->fid;

	/*
	 * Skip any prefix paths in @path as lookup_noperm_positive_unlocked() ends up
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
		if (dentry->d_parent && server->dialect >= SMB30_PROT_ID) {
			struct cached_fid *parent_cfid;

			spin_lock(&cfids->cfid_list_lock);
			list_for_each_entry(parent_cfid, &cfids->entries, entry) {
				if (parent_cfid->dentry == dentry->d_parent) {
					cifs_dbg(FYI, "found a parent cached file handle\n");
					if (is_valid_cached_dir(parent_cfid)) {
						lease_flags
							|= SMB2_LEASE_FLAG_PARENT_LEASE_KEY_SET_LE;
						memcpy(pfid->parent_lease_key,
						       parent_cfid->fid.lease_key,
						       SMB2_LEASE_KEY_SIZE);
					}
					break;
				}
			}
			spin_unlock(&cfids->cfid_list_lock);
		}
	}
	cfid->dentry = dentry;
	cfid->tcon = tcon;

	/*
	 * We do not hold the lock for the open because in case
	 * SMB2_open needs to reconnect.
	 * This is safe because no other thread will be able to get a ref
	 * to the cfid until we have finished opening the file and (possibly)
	 * acquired a lease.
	 */
	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

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
		.desired_access =  FILE_READ_DATA | FILE_READ_ATTRIBUTES |
				   FILE_READ_EA,
		.disposition = FILE_OPEN,
		.fid = pfid,
		.lease_flags = lease_flags,
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
	cfid->last_access_time = jiffies;
	spin_unlock(&cfids->cfid_list_lock);
	/* At this point the directory handle is fully cached */
	rc = 0;

oshr_free:
	SMB2_open_free(&rqst[0]);
	SMB2_query_info_free(&rqst[1]);
	free_rsp_buf(resp_buftype[0], rsp_iov[0].iov_base);
	free_rsp_buf(resp_buftype[1], rsp_iov[1].iov_base);
out:
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
			 * lease. Release one here, and the second below.
			 */
			cfid->has_lease = false;
			close_cached_dir_locked(cfid);
		}
		spin_unlock(&cfids->cfid_list_lock);

		close_cached_dir(cfid);
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
		return -EOPNOTSUPP;

	if (!dentry)
		return -ENOENT;

	spin_lock(&cfids->cfid_list_lock);
	list_for_each_entry(cfid, &cfids->entries, entry) {
		if (cfid->dentry == dentry) {
			if (!is_valid_cached_dir(cfid))
				break;
			cifs_dbg(FYI, "found a cached file handle by dentry\n");
			kref_get(&cfid->refcount);
			*ret_cfid = cfid;
			cfid->last_access_time = jiffies;
			spin_unlock(&cfids->cfid_list_lock);
			return 0;
		}
	}
	spin_unlock(&cfids->cfid_list_lock);
	return -ENOENT;
}

static void
smb2_close_cached_fid(struct kref *ref)
__releases(&cfid->cfids->cfid_list_lock)
{
	struct cached_fid *cfid = container_of(ref, struct cached_fid,
					       refcount);
	int rc;

	lockdep_assert_held(&cfid->cfids->cfid_list_lock);

	if (cfid->on_list) {
		list_del(&cfid->entry);
		cfid->on_list = false;
		cfid->cfids->num_entries--;
	}
	spin_unlock(&cfid->cfids->cfid_list_lock);

	dput(cfid->dentry);
	cfid->dentry = NULL;

	if (cfid->is_open) {
		rc = SMB2_close(0, cfid->tcon, cfid->fid.persistent_fid,
			   cfid->fid.volatile_fid);
		if (rc) /* should we retry on -EBUSY or -EAGAIN? */
			cifs_dbg(VFS, "close cached dir rc %d\n", rc);
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
		close_cached_dir_locked(cfid);
	}
	spin_unlock(&cfid->cfids->cfid_list_lock);
	close_cached_dir(cfid);
}

/**
 * close_cached_dir - drop a reference of a cached dir
 *
 * The release function will be called with cfid_list_lock held to remove the
 * cached dirs from the list before any other thread can take another @cfid
 * ref. Must not be called with cfid_list_lock held; use
 * close_cached_dir_locked() called instead.
 *
 * @cfid: cached dir
 */
void close_cached_dir(struct cached_fid *cfid)
{
	lockdep_assert_not_held(&cfid->cfids->cfid_list_lock);
	kref_put_lock(&cfid->refcount, smb2_close_cached_fid, &cfid->cfids->cfid_list_lock);
}

/**
 * close_cached_dir_locked - put a reference of a cached dir with
 * cfid_list_lock held
 *
 * Calling close_cached_dir() with cfid_list_lock held has the potential effect
 * of causing a deadlock if the invariant of refcount >= 2 is false.
 *
 * This function is used in paths that hold cfid_list_lock and expect at least
 * two references. If that invariant is violated, WARNs and returns without
 * dropping a reference; the final put must still go through
 * close_cached_dir().
 *
 * @cfid: cached dir
 */
static void close_cached_dir_locked(struct cached_fid *cfid)
{
	lockdep_assert_held(&cfid->cfids->cfid_list_lock);

	if (WARN_ON(kref_read(&cfid->refcount) < 2))
		return;

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
	struct cached_dir_dentry *tmp_list, *q;
	LIST_HEAD(entry);

	spin_lock(&cifs_sb->tlink_tree_lock);
	for (node = rb_first(root); node; node = rb_next(node)) {
		tlink = rb_entry(node, struct tcon_link, tl_rbnode);
		tcon = tlink_tcon(tlink);
		if (IS_ERR(tcon))
			continue;
		cfids = tcon->cfids;
		if (cfids == NULL)
			continue;
		spin_lock(&cfids->cfid_list_lock);
		list_for_each_entry(cfid, &cfids->entries, entry) {
			tmp_list = kmalloc(sizeof(*tmp_list), GFP_ATOMIC);
			if (tmp_list == NULL) {
				/*
				 * If the malloc() fails, we won't drop all
				 * dentries, and unmounting is likely to trigger
				 * a 'Dentry still in use' error.
				 */
				cifs_tcon_dbg(VFS, "Out of memory while dropping dentries\n");
				spin_unlock(&cfids->cfid_list_lock);
				spin_unlock(&cifs_sb->tlink_tree_lock);
				goto done;
			}

			tmp_list->dentry = cfid->dentry;
			cfid->dentry = NULL;

			list_add_tail(&tmp_list->entry, &entry);
		}
		spin_unlock(&cfids->cfid_list_lock);
	}
	spin_unlock(&cifs_sb->tlink_tree_lock);

done:
	list_for_each_entry_safe(tmp_list, q, &entry, entry) {
		list_del(&tmp_list->entry);
		dput(tmp_list->dentry);
		kfree(tmp_list);
	}

	/* Flush any pending work that will drop dentries */
	flush_workqueue(cfid_put_wq);
}

/*
 * Invalidate all cached dirs when a TCON has been reset
 * due to a session loss.
 */
void invalidate_all_cached_dirs(struct cifs_tcon *tcon)
{
	struct cached_fids *cfids = tcon->cfids;
	struct cached_fid *cfid, *q;

	if (cfids == NULL)
		return;

	/*
	 * Mark all the cfids as closed, and move them to the cfids->dying list.
	 * They'll be cleaned up by laundromat.  Take a reference to each cfid
	 * during this process.
	 */
	spin_lock(&cfids->cfid_list_lock);
	list_for_each_entry_safe(cfid, q, &cfids->entries, entry) {
		list_move(&cfid->entry, &cfids->dying);
		cfids->num_entries--;
		cfid->is_open = false;
		cfid->on_list = false;
		if (cfid->has_lease) {
			/*
			 * The lease was never cancelled from the server,
			 * so steal that reference.
			 */
			cfid->has_lease = false;
		} else
			kref_get(&cfid->refcount);
	}
	spin_unlock(&cfids->cfid_list_lock);

	/* run laundromat unconditionally now as there might have been previously queued work */
	mod_delayed_work(cfid_put_wq, &cfids->laundromat_work, 0);
	flush_delayed_work(&cfids->laundromat_work);
}

static void
cached_dir_offload_close(struct work_struct *work)
{
	struct cached_fid *cfid = container_of(work,
				struct cached_fid, close_work);
	struct cifs_tcon *tcon = cfid->tcon;

	WARN_ON(cfid->on_list);

	close_cached_dir(cfid);
	cifs_put_tcon(tcon, netfs_trace_tcon_ref_put_cached_close);
}

/*
 * Release the cached directory's dentry, and then queue work to drop cached
 * directory itself (closing on server if needed).
 *
 * Must be called with a reference to the cached_fid and a reference to the
 * tcon.
 */
static void cached_dir_put_work(struct work_struct *work)
{
	struct cached_fid *cfid = container_of(work, struct cached_fid,
					       put_work);
	dput(cfid->dentry);
	cfid->dentry = NULL;

	queue_work(serverclose_wq, &cfid->close_work);
}

bool cached_dir_lease_break(struct cifs_tcon *tcon, __u8 lease_key[16])
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
			cfid->has_lease = false;
			cfid->time = 0;
			/*
			 * We found a lease remove it from the list
			 * so no threads can access it.
			 */
			list_del(&cfid->entry);
			cfid->on_list = false;
			cfids->num_entries--;

			++tcon->tc_count;
			trace_smb3_tcon_ref(tcon->debug_id, tcon->tc_count,
					    netfs_trace_tcon_ref_get_cached_lease_break);
			queue_work(cfid_put_wq, &cfid->put_work);
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

	INIT_WORK(&cfid->close_work, cached_dir_offload_close);
	INIT_WORK(&cfid->put_work, cached_dir_put_work);
	INIT_LIST_HEAD(&cfid->entry);
	INIT_LIST_HEAD(&cfid->dirents.entries);
	mutex_init(&cfid->dirents.de_mutex);
	kref_init(&cfid->refcount);
	return cfid;
}

static void free_cached_dir(struct cached_fid *cfid)
{
	struct cached_dirent *dirent, *q;

	WARN_ON(work_pending(&cfid->close_work));
	WARN_ON(work_pending(&cfid->put_work));

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

	/* adjust tcon-level counters and reset per-dir accounting */
	if (cfid->cfids) {
		if (cfid->dirents.entries_count)
			atomic_long_sub((long)cfid->dirents.entries_count,
					&cfid->cfids->total_dirents_entries);
		if (cfid->dirents.bytes_used) {
			atomic64_sub((long long)cfid->dirents.bytes_used,
					&cfid->cfids->total_dirents_bytes);
			atomic64_sub((long long)cfid->dirents.bytes_used,
					&cifs_dircache_bytes_used);
		}
	}
	cfid->dirents.entries_count = 0;
	cfid->dirents.bytes_used = 0;

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
	/* move cfids->dying to the local list */
	list_cut_before(&entry, &cfids->dying, &cfids->dying);

	list_for_each_entry_safe(cfid, q, &cfids->entries, entry) {
		if (cfid->last_access_time &&
		    time_after(jiffies, cfid->last_access_time + HZ * dir_cache_timeout)) {
			cfid->on_list = false;
			list_move(&cfid->entry, &entry);
			cfids->num_entries--;
			if (cfid->has_lease) {
				/*
				 * Our lease has not yet been cancelled from the
				 * server. Steal that reference.
				 */
				cfid->has_lease = false;
			} else
				kref_get(&cfid->refcount);
		}
	}
	spin_unlock(&cfids->cfid_list_lock);

	list_for_each_entry_safe(cfid, q, &entry, entry) {
		list_del(&cfid->entry);

		dput(cfid->dentry);
		cfid->dentry = NULL;

		if (cfid->is_open) {
			spin_lock(&cifs_tcp_ses_lock);
			++cfid->tcon->tc_count;
			trace_smb3_tcon_ref(cfid->tcon->debug_id, cfid->tcon->tc_count,
					    netfs_trace_tcon_ref_get_cached_laundromat);
			spin_unlock(&cifs_tcp_ses_lock);
			queue_work(serverclose_wq, &cfid->close_work);
		} else
			/*
			 * Drop the ref-count from above, either the lease-ref (if there
			 * was one) or the extra one acquired.
			 */
			close_cached_dir(cfid);
	}
	queue_delayed_work(cfid_put_wq, &cfids->laundromat_work,
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
	INIT_LIST_HEAD(&cfids->dying);

	INIT_DELAYED_WORK(&cfids->laundromat_work, cfids_laundromat_worker);
	queue_delayed_work(cfid_put_wq, &cfids->laundromat_work,
			   dir_cache_timeout * HZ);

	atomic_long_set(&cfids->total_dirents_entries, 0);
	atomic64_set(&cfids->total_dirents_bytes, 0);

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
	list_for_each_entry_safe(cfid, q, &cfids->dying, entry) {
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
