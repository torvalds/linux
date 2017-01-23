#include <linux/ceph/ceph_debug.h>

#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/utsname.h>
#include <linux/ratelimit.h>

#include "super.h"
#include "mds_client.h"

#include <linux/ceph/ceph_features.h>
#include <linux/ceph/messenger.h>
#include <linux/ceph/decode.h>
#include <linux/ceph/pagelist.h>
#include <linux/ceph/auth.h>
#include <linux/ceph/debugfs.h>

/*
 * A cluster of MDS (metadata server) daemons is responsible for
 * managing the file system namespace (the directory hierarchy and
 * inodes) and for coordinating shared access to storage.  Metadata is
 * partitioning hierarchically across a number of servers, and that
 * partition varies over time as the cluster adjusts the distribution
 * in order to balance load.
 *
 * The MDS client is primarily responsible to managing synchronous
 * metadata requests for operations like open, unlink, and so forth.
 * If there is a MDS failure, we find out about it when we (possibly
 * request and) receive a new MDS map, and can resubmit affected
 * requests.
 *
 * For the most part, though, we take advantage of a lossless
 * communications channel to the MDS, and do not need to worry about
 * timing out or resubmitting requests.
 *
 * We maintain a stateful "session" with each MDS we interact with.
 * Within each session, we sent periodic heartbeat messages to ensure
 * any capabilities or leases we have been issues remain valid.  If
 * the session times out and goes stale, our leases and capabilities
 * are no longer valid.
 */

struct ceph_reconnect_state {
	int nr_caps;
	struct ceph_pagelist *pagelist;
	unsigned msg_version;
};

static void __wake_requests(struct ceph_mds_client *mdsc,
			    struct list_head *head);

static const struct ceph_connection_operations mds_con_ops;


/*
 * mds reply parsing
 */

/*
 * parse individual inode info
 */
static int parse_reply_info_in(void **p, void *end,
			       struct ceph_mds_reply_info_in *info,
			       u64 features)
{
	int err = -EIO;

	info->in = *p;
	*p += sizeof(struct ceph_mds_reply_inode) +
		sizeof(*info->in->fragtree.splits) *
		le32_to_cpu(info->in->fragtree.nsplits);

	ceph_decode_32_safe(p, end, info->symlink_len, bad);
	ceph_decode_need(p, end, info->symlink_len, bad);
	info->symlink = *p;
	*p += info->symlink_len;

	if (features & CEPH_FEATURE_DIRLAYOUTHASH)
		ceph_decode_copy_safe(p, end, &info->dir_layout,
				      sizeof(info->dir_layout), bad);
	else
		memset(&info->dir_layout, 0, sizeof(info->dir_layout));

	ceph_decode_32_safe(p, end, info->xattr_len, bad);
	ceph_decode_need(p, end, info->xattr_len, bad);
	info->xattr_data = *p;
	*p += info->xattr_len;

	if (features & CEPH_FEATURE_MDS_INLINE_DATA) {
		ceph_decode_64_safe(p, end, info->inline_version, bad);
		ceph_decode_32_safe(p, end, info->inline_len, bad);
		ceph_decode_need(p, end, info->inline_len, bad);
		info->inline_data = *p;
		*p += info->inline_len;
	} else
		info->inline_version = CEPH_INLINE_NONE;

	info->pool_ns_len = 0;
	info->pool_ns_data = NULL;
	if (features & CEPH_FEATURE_FS_FILE_LAYOUT_V2) {
		ceph_decode_32_safe(p, end, info->pool_ns_len, bad);
		if (info->pool_ns_len > 0) {
			ceph_decode_need(p, end, info->pool_ns_len, bad);
			info->pool_ns_data = *p;
			*p += info->pool_ns_len;
		}
	}

	return 0;
bad:
	return err;
}

/*
 * parse a normal reply, which may contain a (dir+)dentry and/or a
 * target inode.
 */
static int parse_reply_info_trace(void **p, void *end,
				  struct ceph_mds_reply_info_parsed *info,
				  u64 features)
{
	int err;

	if (info->head->is_dentry) {
		err = parse_reply_info_in(p, end, &info->diri, features);
		if (err < 0)
			goto out_bad;

		if (unlikely(*p + sizeof(*info->dirfrag) > end))
			goto bad;
		info->dirfrag = *p;
		*p += sizeof(*info->dirfrag) +
			sizeof(u32)*le32_to_cpu(info->dirfrag->ndist);
		if (unlikely(*p > end))
			goto bad;

		ceph_decode_32_safe(p, end, info->dname_len, bad);
		ceph_decode_need(p, end, info->dname_len, bad);
		info->dname = *p;
		*p += info->dname_len;
		info->dlease = *p;
		*p += sizeof(*info->dlease);
	}

	if (info->head->is_target) {
		err = parse_reply_info_in(p, end, &info->targeti, features);
		if (err < 0)
			goto out_bad;
	}

	if (unlikely(*p != end))
		goto bad;
	return 0;

bad:
	err = -EIO;
out_bad:
	pr_err("problem parsing mds trace %d\n", err);
	return err;
}

/*
 * parse readdir results
 */
static int parse_reply_info_dir(void **p, void *end,
				struct ceph_mds_reply_info_parsed *info,
				u64 features)
{
	u32 num, i = 0;
	int err;

	info->dir_dir = *p;
	if (*p + sizeof(*info->dir_dir) > end)
		goto bad;
	*p += sizeof(*info->dir_dir) +
		sizeof(u32)*le32_to_cpu(info->dir_dir->ndist);
	if (*p > end)
		goto bad;

	ceph_decode_need(p, end, sizeof(num) + 2, bad);
	num = ceph_decode_32(p);
	{
		u16 flags = ceph_decode_16(p);
		info->dir_end = !!(flags & CEPH_READDIR_FRAG_END);
		info->dir_complete = !!(flags & CEPH_READDIR_FRAG_COMPLETE);
		info->hash_order = !!(flags & CEPH_READDIR_HASH_ORDER);
	}
	if (num == 0)
		goto done;

	BUG_ON(!info->dir_entries);
	if ((unsigned long)(info->dir_entries + num) >
	    (unsigned long)info->dir_entries + info->dir_buf_size) {
		pr_err("dir contents are larger than expected\n");
		WARN_ON(1);
		goto bad;
	}

	info->dir_nr = num;
	while (num) {
		struct ceph_mds_reply_dir_entry *rde = info->dir_entries + i;
		/* dentry */
		ceph_decode_need(p, end, sizeof(u32)*2, bad);
		rde->name_len = ceph_decode_32(p);
		ceph_decode_need(p, end, rde->name_len, bad);
		rde->name = *p;
		*p += rde->name_len;
		dout("parsed dir dname '%.*s'\n", rde->name_len, rde->name);
		rde->lease = *p;
		*p += sizeof(struct ceph_mds_reply_lease);

		/* inode */
		err = parse_reply_info_in(p, end, &rde->inode, features);
		if (err < 0)
			goto out_bad;
		/* ceph_readdir_prepopulate() will update it */
		rde->offset = 0;
		i++;
		num--;
	}

done:
	if (*p != end)
		goto bad;
	return 0;

bad:
	err = -EIO;
out_bad:
	pr_err("problem parsing dir contents %d\n", err);
	return err;
}

/*
 * parse fcntl F_GETLK results
 */
static int parse_reply_info_filelock(void **p, void *end,
				     struct ceph_mds_reply_info_parsed *info,
				     u64 features)
{
	if (*p + sizeof(*info->filelock_reply) > end)
		goto bad;

	info->filelock_reply = *p;
	*p += sizeof(*info->filelock_reply);

	if (unlikely(*p != end))
		goto bad;
	return 0;

bad:
	return -EIO;
}

/*
 * parse create results
 */
static int parse_reply_info_create(void **p, void *end,
				  struct ceph_mds_reply_info_parsed *info,
				  u64 features)
{
	if (features & CEPH_FEATURE_REPLY_CREATE_INODE) {
		if (*p == end) {
			info->has_create_ino = false;
		} else {
			info->has_create_ino = true;
			info->ino = ceph_decode_64(p);
		}
	}

	if (unlikely(*p != end))
		goto bad;
	return 0;

bad:
	return -EIO;
}

/*
 * parse extra results
 */
static int parse_reply_info_extra(void **p, void *end,
				  struct ceph_mds_reply_info_parsed *info,
				  u64 features)
{
	if (info->head->op == CEPH_MDS_OP_GETFILELOCK)
		return parse_reply_info_filelock(p, end, info, features);
	else if (info->head->op == CEPH_MDS_OP_READDIR ||
		 info->head->op == CEPH_MDS_OP_LSSNAP)
		return parse_reply_info_dir(p, end, info, features);
	else if (info->head->op == CEPH_MDS_OP_CREATE)
		return parse_reply_info_create(p, end, info, features);
	else
		return -EIO;
}

/*
 * parse entire mds reply
 */
static int parse_reply_info(struct ceph_msg *msg,
			    struct ceph_mds_reply_info_parsed *info,
			    u64 features)
{
	void *p, *end;
	u32 len;
	int err;

	info->head = msg->front.iov_base;
	p = msg->front.iov_base + sizeof(struct ceph_mds_reply_head);
	end = p + msg->front.iov_len - sizeof(struct ceph_mds_reply_head);

	/* trace */
	ceph_decode_32_safe(&p, end, len, bad);
	if (len > 0) {
		ceph_decode_need(&p, end, len, bad);
		err = parse_reply_info_trace(&p, p+len, info, features);
		if (err < 0)
			goto out_bad;
	}

	/* extra */
	ceph_decode_32_safe(&p, end, len, bad);
	if (len > 0) {
		ceph_decode_need(&p, end, len, bad);
		err = parse_reply_info_extra(&p, p+len, info, features);
		if (err < 0)
			goto out_bad;
	}

	/* snap blob */
	ceph_decode_32_safe(&p, end, len, bad);
	info->snapblob_len = len;
	info->snapblob = p;
	p += len;

	if (p != end)
		goto bad;
	return 0;

bad:
	err = -EIO;
out_bad:
	pr_err("mds parse_reply err %d\n", err);
	return err;
}

static void destroy_reply_info(struct ceph_mds_reply_info_parsed *info)
{
	if (!info->dir_entries)
		return;
	free_pages((unsigned long)info->dir_entries, get_order(info->dir_buf_size));
}


/*
 * sessions
 */
const char *ceph_session_state_name(int s)
{
	switch (s) {
	case CEPH_MDS_SESSION_NEW: return "new";
	case CEPH_MDS_SESSION_OPENING: return "opening";
	case CEPH_MDS_SESSION_OPEN: return "open";
	case CEPH_MDS_SESSION_HUNG: return "hung";
	case CEPH_MDS_SESSION_CLOSING: return "closing";
	case CEPH_MDS_SESSION_RESTARTING: return "restarting";
	case CEPH_MDS_SESSION_RECONNECTING: return "reconnecting";
	case CEPH_MDS_SESSION_REJECTED: return "rejected";
	default: return "???";
	}
}

static struct ceph_mds_session *get_session(struct ceph_mds_session *s)
{
	if (atomic_inc_not_zero(&s->s_ref)) {
		dout("mdsc get_session %p %d -> %d\n", s,
		     atomic_read(&s->s_ref)-1, atomic_read(&s->s_ref));
		return s;
	} else {
		dout("mdsc get_session %p 0 -- FAIL", s);
		return NULL;
	}
}

void ceph_put_mds_session(struct ceph_mds_session *s)
{
	dout("mdsc put_session %p %d -> %d\n", s,
	     atomic_read(&s->s_ref), atomic_read(&s->s_ref)-1);
	if (atomic_dec_and_test(&s->s_ref)) {
		if (s->s_auth.authorizer)
			ceph_auth_destroy_authorizer(s->s_auth.authorizer);
		kfree(s);
	}
}

/*
 * called under mdsc->mutex
 */
struct ceph_mds_session *__ceph_lookup_mds_session(struct ceph_mds_client *mdsc,
						   int mds)
{
	struct ceph_mds_session *session;

	if (mds >= mdsc->max_sessions || mdsc->sessions[mds] == NULL)
		return NULL;
	session = mdsc->sessions[mds];
	dout("lookup_mds_session %p %d\n", session,
	     atomic_read(&session->s_ref));
	get_session(session);
	return session;
}

static bool __have_session(struct ceph_mds_client *mdsc, int mds)
{
	if (mds >= mdsc->max_sessions)
		return false;
	return mdsc->sessions[mds];
}

static int __verify_registered_session(struct ceph_mds_client *mdsc,
				       struct ceph_mds_session *s)
{
	if (s->s_mds >= mdsc->max_sessions ||
	    mdsc->sessions[s->s_mds] != s)
		return -ENOENT;
	return 0;
}

/*
 * create+register a new session for given mds.
 * called under mdsc->mutex.
 */
static struct ceph_mds_session *register_session(struct ceph_mds_client *mdsc,
						 int mds)
{
	struct ceph_mds_session *s;

	if (mds >= mdsc->mdsmap->m_max_mds)
		return ERR_PTR(-EINVAL);

	s = kzalloc(sizeof(*s), GFP_NOFS);
	if (!s)
		return ERR_PTR(-ENOMEM);
	s->s_mdsc = mdsc;
	s->s_mds = mds;
	s->s_state = CEPH_MDS_SESSION_NEW;
	s->s_ttl = 0;
	s->s_seq = 0;
	mutex_init(&s->s_mutex);

	ceph_con_init(&s->s_con, s, &mds_con_ops, &mdsc->fsc->client->msgr);

	spin_lock_init(&s->s_gen_ttl_lock);
	s->s_cap_gen = 0;
	s->s_cap_ttl = jiffies - 1;

	spin_lock_init(&s->s_cap_lock);
	s->s_renew_requested = 0;
	s->s_renew_seq = 0;
	INIT_LIST_HEAD(&s->s_caps);
	s->s_nr_caps = 0;
	s->s_trim_caps = 0;
	atomic_set(&s->s_ref, 1);
	INIT_LIST_HEAD(&s->s_waiting);
	INIT_LIST_HEAD(&s->s_unsafe);
	s->s_num_cap_releases = 0;
	s->s_cap_reconnect = 0;
	s->s_cap_iterator = NULL;
	INIT_LIST_HEAD(&s->s_cap_releases);
	INIT_LIST_HEAD(&s->s_cap_flushing);

	dout("register_session mds%d\n", mds);
	if (mds >= mdsc->max_sessions) {
		int newmax = 1 << get_count_order(mds+1);
		struct ceph_mds_session **sa;

		dout("register_session realloc to %d\n", newmax);
		sa = kcalloc(newmax, sizeof(void *), GFP_NOFS);
		if (sa == NULL)
			goto fail_realloc;
		if (mdsc->sessions) {
			memcpy(sa, mdsc->sessions,
			       mdsc->max_sessions * sizeof(void *));
			kfree(mdsc->sessions);
		}
		mdsc->sessions = sa;
		mdsc->max_sessions = newmax;
	}
	mdsc->sessions[mds] = s;
	atomic_inc(&mdsc->num_sessions);
	atomic_inc(&s->s_ref);  /* one ref to sessions[], one to caller */

	ceph_con_open(&s->s_con, CEPH_ENTITY_TYPE_MDS, mds,
		      ceph_mdsmap_get_addr(mdsc->mdsmap, mds));

	return s;

fail_realloc:
	kfree(s);
	return ERR_PTR(-ENOMEM);
}

/*
 * called under mdsc->mutex
 */
static void __unregister_session(struct ceph_mds_client *mdsc,
			       struct ceph_mds_session *s)
{
	dout("__unregister_session mds%d %p\n", s->s_mds, s);
	BUG_ON(mdsc->sessions[s->s_mds] != s);
	mdsc->sessions[s->s_mds] = NULL;
	ceph_con_close(&s->s_con);
	ceph_put_mds_session(s);
	atomic_dec(&mdsc->num_sessions);
}

/*
 * drop session refs in request.
 *
 * should be last request ref, or hold mdsc->mutex
 */
static void put_request_session(struct ceph_mds_request *req)
{
	if (req->r_session) {
		ceph_put_mds_session(req->r_session);
		req->r_session = NULL;
	}
}

void ceph_mdsc_release_request(struct kref *kref)
{
	struct ceph_mds_request *req = container_of(kref,
						    struct ceph_mds_request,
						    r_kref);
	destroy_reply_info(&req->r_reply_info);
	if (req->r_request)
		ceph_msg_put(req->r_request);
	if (req->r_reply)
		ceph_msg_put(req->r_reply);
	if (req->r_inode) {
		ceph_put_cap_refs(ceph_inode(req->r_inode), CEPH_CAP_PIN);
		iput(req->r_inode);
	}
	if (req->r_locked_dir)
		ceph_put_cap_refs(ceph_inode(req->r_locked_dir), CEPH_CAP_PIN);
	iput(req->r_target_inode);
	if (req->r_dentry)
		dput(req->r_dentry);
	if (req->r_old_dentry)
		dput(req->r_old_dentry);
	if (req->r_old_dentry_dir) {
		/*
		 * track (and drop pins for) r_old_dentry_dir
		 * separately, since r_old_dentry's d_parent may have
		 * changed between the dir mutex being dropped and
		 * this request being freed.
		 */
		ceph_put_cap_refs(ceph_inode(req->r_old_dentry_dir),
				  CEPH_CAP_PIN);
		iput(req->r_old_dentry_dir);
	}
	kfree(req->r_path1);
	kfree(req->r_path2);
	if (req->r_pagelist)
		ceph_pagelist_release(req->r_pagelist);
	put_request_session(req);
	ceph_unreserve_caps(req->r_mdsc, &req->r_caps_reservation);
	kfree(req);
}

DEFINE_RB_FUNCS(request, struct ceph_mds_request, r_tid, r_node)

/*
 * lookup session, bump ref if found.
 *
 * called under mdsc->mutex.
 */
static struct ceph_mds_request *
lookup_get_request(struct ceph_mds_client *mdsc, u64 tid)
{
	struct ceph_mds_request *req;

	req = lookup_request(&mdsc->request_tree, tid);
	if (req)
		ceph_mdsc_get_request(req);

	return req;
}

/*
 * Register an in-flight request, and assign a tid.  Link to directory
 * are modifying (if any).
 *
 * Called under mdsc->mutex.
 */
static void __register_request(struct ceph_mds_client *mdsc,
			       struct ceph_mds_request *req,
			       struct inode *dir)
{
	req->r_tid = ++mdsc->last_tid;
	if (req->r_num_caps)
		ceph_reserve_caps(mdsc, &req->r_caps_reservation,
				  req->r_num_caps);
	dout("__register_request %p tid %lld\n", req, req->r_tid);
	ceph_mdsc_get_request(req);
	insert_request(&mdsc->request_tree, req);

	req->r_uid = current_fsuid();
	req->r_gid = current_fsgid();

	if (mdsc->oldest_tid == 0 && req->r_op != CEPH_MDS_OP_SETFILELOCK)
		mdsc->oldest_tid = req->r_tid;

	if (dir) {
		ihold(dir);
		req->r_unsafe_dir = dir;
	}
}

static void __unregister_request(struct ceph_mds_client *mdsc,
				 struct ceph_mds_request *req)
{
	dout("__unregister_request %p tid %lld\n", req, req->r_tid);

	if (req->r_tid == mdsc->oldest_tid) {
		struct rb_node *p = rb_next(&req->r_node);
		mdsc->oldest_tid = 0;
		while (p) {
			struct ceph_mds_request *next_req =
				rb_entry(p, struct ceph_mds_request, r_node);
			if (next_req->r_op != CEPH_MDS_OP_SETFILELOCK) {
				mdsc->oldest_tid = next_req->r_tid;
				break;
			}
			p = rb_next(p);
		}
	}

	erase_request(&mdsc->request_tree, req);

	if (req->r_unsafe_dir && req->r_got_unsafe) {
		struct ceph_inode_info *ci = ceph_inode(req->r_unsafe_dir);
		spin_lock(&ci->i_unsafe_lock);
		list_del_init(&req->r_unsafe_dir_item);
		spin_unlock(&ci->i_unsafe_lock);
	}
	if (req->r_target_inode && req->r_got_unsafe) {
		struct ceph_inode_info *ci = ceph_inode(req->r_target_inode);
		spin_lock(&ci->i_unsafe_lock);
		list_del_init(&req->r_unsafe_target_item);
		spin_unlock(&ci->i_unsafe_lock);
	}

	if (req->r_unsafe_dir) {
		iput(req->r_unsafe_dir);
		req->r_unsafe_dir = NULL;
	}

	complete_all(&req->r_safe_completion);

	ceph_mdsc_put_request(req);
}

/*
 * Choose mds to send request to next.  If there is a hint set in the
 * request (e.g., due to a prior forward hint from the mds), use that.
 * Otherwise, consult frag tree and/or caps to identify the
 * appropriate mds.  If all else fails, choose randomly.
 *
 * Called under mdsc->mutex.
 */
static struct dentry *get_nonsnap_parent(struct dentry *dentry)
{
	/*
	 * we don't need to worry about protecting the d_parent access
	 * here because we never renaming inside the snapped namespace
	 * except to resplice to another snapdir, and either the old or new
	 * result is a valid result.
	 */
	while (!IS_ROOT(dentry) && ceph_snap(d_inode(dentry)) != CEPH_NOSNAP)
		dentry = dentry->d_parent;
	return dentry;
}

static int __choose_mds(struct ceph_mds_client *mdsc,
			struct ceph_mds_request *req)
{
	struct inode *inode;
	struct ceph_inode_info *ci;
	struct ceph_cap *cap;
	int mode = req->r_direct_mode;
	int mds = -1;
	u32 hash = req->r_direct_hash;
	bool is_hash = req->r_direct_is_hash;

	/*
	 * is there a specific mds we should try?  ignore hint if we have
	 * no session and the mds is not up (active or recovering).
	 */
	if (req->r_resend_mds >= 0 &&
	    (__have_session(mdsc, req->r_resend_mds) ||
	     ceph_mdsmap_get_state(mdsc->mdsmap, req->r_resend_mds) > 0)) {
		dout("choose_mds using resend_mds mds%d\n",
		     req->r_resend_mds);
		return req->r_resend_mds;
	}

	if (mode == USE_RANDOM_MDS)
		goto random;

	inode = NULL;
	if (req->r_inode) {
		inode = req->r_inode;
	} else if (req->r_dentry) {
		/* ignore race with rename; old or new d_parent is okay */
		struct dentry *parent = req->r_dentry->d_parent;
		struct inode *dir = d_inode(parent);

		if (dir->i_sb != mdsc->fsc->sb) {
			/* not this fs! */
			inode = d_inode(req->r_dentry);
		} else if (ceph_snap(dir) != CEPH_NOSNAP) {
			/* direct snapped/virtual snapdir requests
			 * based on parent dir inode */
			struct dentry *dn = get_nonsnap_parent(parent);
			inode = d_inode(dn);
			dout("__choose_mds using nonsnap parent %p\n", inode);
		} else {
			/* dentry target */
			inode = d_inode(req->r_dentry);
			if (!inode || mode == USE_AUTH_MDS) {
				/* dir + name */
				inode = dir;
				hash = ceph_dentry_hash(dir, req->r_dentry);
				is_hash = true;
			}
		}
	}

	dout("__choose_mds %p is_hash=%d (%d) mode %d\n", inode, (int)is_hash,
	     (int)hash, mode);
	if (!inode)
		goto random;
	ci = ceph_inode(inode);

	if (is_hash && S_ISDIR(inode->i_mode)) {
		struct ceph_inode_frag frag;
		int found;

		ceph_choose_frag(ci, hash, &frag, &found);
		if (found) {
			if (mode == USE_ANY_MDS && frag.ndist > 0) {
				u8 r;

				/* choose a random replica */
				get_random_bytes(&r, 1);
				r %= frag.ndist;
				mds = frag.dist[r];
				dout("choose_mds %p %llx.%llx "
				     "frag %u mds%d (%d/%d)\n",
				     inode, ceph_vinop(inode),
				     frag.frag, mds,
				     (int)r, frag.ndist);
				if (ceph_mdsmap_get_state(mdsc->mdsmap, mds) >=
				    CEPH_MDS_STATE_ACTIVE)
					return mds;
			}

			/* since this file/dir wasn't known to be
			 * replicated, then we want to look for the
			 * authoritative mds. */
			mode = USE_AUTH_MDS;
			if (frag.mds >= 0) {
				/* choose auth mds */
				mds = frag.mds;
				dout("choose_mds %p %llx.%llx "
				     "frag %u mds%d (auth)\n",
				     inode, ceph_vinop(inode), frag.frag, mds);
				if (ceph_mdsmap_get_state(mdsc->mdsmap, mds) >=
				    CEPH_MDS_STATE_ACTIVE)
					return mds;
			}
		}
	}

	spin_lock(&ci->i_ceph_lock);
	cap = NULL;
	if (mode == USE_AUTH_MDS)
		cap = ci->i_auth_cap;
	if (!cap && !RB_EMPTY_ROOT(&ci->i_caps))
		cap = rb_entry(rb_first(&ci->i_caps), struct ceph_cap, ci_node);
	if (!cap) {
		spin_unlock(&ci->i_ceph_lock);
		goto random;
	}
	mds = cap->session->s_mds;
	dout("choose_mds %p %llx.%llx mds%d (%scap %p)\n",
	     inode, ceph_vinop(inode), mds,
	     cap == ci->i_auth_cap ? "auth " : "", cap);
	spin_unlock(&ci->i_ceph_lock);
	return mds;

random:
	mds = ceph_mdsmap_get_random_mds(mdsc->mdsmap);
	dout("choose_mds chose random mds%d\n", mds);
	return mds;
}


/*
 * session messages
 */
static struct ceph_msg *create_session_msg(u32 op, u64 seq)
{
	struct ceph_msg *msg;
	struct ceph_mds_session_head *h;

	msg = ceph_msg_new(CEPH_MSG_CLIENT_SESSION, sizeof(*h), GFP_NOFS,
			   false);
	if (!msg) {
		pr_err("create_session_msg ENOMEM creating msg\n");
		return NULL;
	}
	h = msg->front.iov_base;
	h->op = cpu_to_le32(op);
	h->seq = cpu_to_le64(seq);

	return msg;
}

/*
 * session message, specialization for CEPH_SESSION_REQUEST_OPEN
 * to include additional client metadata fields.
 */
static struct ceph_msg *create_session_open_msg(struct ceph_mds_client *mdsc, u64 seq)
{
	struct ceph_msg *msg;
	struct ceph_mds_session_head *h;
	int i = -1;
	int metadata_bytes = 0;
	int metadata_key_count = 0;
	struct ceph_options *opt = mdsc->fsc->client->options;
	struct ceph_mount_options *fsopt = mdsc->fsc->mount_options;
	void *p;

	const char* metadata[][2] = {
		{"hostname", utsname()->nodename},
		{"kernel_version", utsname()->release},
		{"entity_id", opt->name ? : ""},
		{"root", fsopt->server_path ? : "/"},
		{NULL, NULL}
	};

	/* Calculate serialized length of metadata */
	metadata_bytes = 4;  /* map length */
	for (i = 0; metadata[i][0] != NULL; ++i) {
		metadata_bytes += 8 + strlen(metadata[i][0]) +
			strlen(metadata[i][1]);
		metadata_key_count++;
	}

	/* Allocate the message */
	msg = ceph_msg_new(CEPH_MSG_CLIENT_SESSION, sizeof(*h) + metadata_bytes,
			   GFP_NOFS, false);
	if (!msg) {
		pr_err("create_session_msg ENOMEM creating msg\n");
		return NULL;
	}
	h = msg->front.iov_base;
	h->op = cpu_to_le32(CEPH_SESSION_REQUEST_OPEN);
	h->seq = cpu_to_le64(seq);

	/*
	 * Serialize client metadata into waiting buffer space, using
	 * the format that userspace expects for map<string, string>
	 *
	 * ClientSession messages with metadata are v2
	 */
	msg->hdr.version = cpu_to_le16(2);
	msg->hdr.compat_version = cpu_to_le16(1);

	/* The write pointer, following the session_head structure */
	p = msg->front.iov_base + sizeof(*h);

	/* Number of entries in the map */
	ceph_encode_32(&p, metadata_key_count);

	/* Two length-prefixed strings for each entry in the map */
	for (i = 0; metadata[i][0] != NULL; ++i) {
		size_t const key_len = strlen(metadata[i][0]);
		size_t const val_len = strlen(metadata[i][1]);

		ceph_encode_32(&p, key_len);
		memcpy(p, metadata[i][0], key_len);
		p += key_len;
		ceph_encode_32(&p, val_len);
		memcpy(p, metadata[i][1], val_len);
		p += val_len;
	}

	return msg;
}

/*
 * send session open request.
 *
 * called under mdsc->mutex
 */
static int __open_session(struct ceph_mds_client *mdsc,
			  struct ceph_mds_session *session)
{
	struct ceph_msg *msg;
	int mstate;
	int mds = session->s_mds;

	/* wait for mds to go active? */
	mstate = ceph_mdsmap_get_state(mdsc->mdsmap, mds);
	dout("open_session to mds%d (%s)\n", mds,
	     ceph_mds_state_name(mstate));
	session->s_state = CEPH_MDS_SESSION_OPENING;
	session->s_renew_requested = jiffies;

	/* send connect message */
	msg = create_session_open_msg(mdsc, session->s_seq);
	if (!msg)
		return -ENOMEM;
	ceph_con_send(&session->s_con, msg);
	return 0;
}

/*
 * open sessions for any export targets for the given mds
 *
 * called under mdsc->mutex
 */
static struct ceph_mds_session *
__open_export_target_session(struct ceph_mds_client *mdsc, int target)
{
	struct ceph_mds_session *session;

	session = __ceph_lookup_mds_session(mdsc, target);
	if (!session) {
		session = register_session(mdsc, target);
		if (IS_ERR(session))
			return session;
	}
	if (session->s_state == CEPH_MDS_SESSION_NEW ||
	    session->s_state == CEPH_MDS_SESSION_CLOSING)
		__open_session(mdsc, session);

	return session;
}

struct ceph_mds_session *
ceph_mdsc_open_export_target_session(struct ceph_mds_client *mdsc, int target)
{
	struct ceph_mds_session *session;

	dout("open_export_target_session to mds%d\n", target);

	mutex_lock(&mdsc->mutex);
	session = __open_export_target_session(mdsc, target);
	mutex_unlock(&mdsc->mutex);

	return session;
}

static void __open_export_target_sessions(struct ceph_mds_client *mdsc,
					  struct ceph_mds_session *session)
{
	struct ceph_mds_info *mi;
	struct ceph_mds_session *ts;
	int i, mds = session->s_mds;

	if (mds >= mdsc->mdsmap->m_max_mds)
		return;

	mi = &mdsc->mdsmap->m_info[mds];
	dout("open_export_target_sessions for mds%d (%d targets)\n",
	     session->s_mds, mi->num_export_targets);

	for (i = 0; i < mi->num_export_targets; i++) {
		ts = __open_export_target_session(mdsc, mi->export_targets[i]);
		if (!IS_ERR(ts))
			ceph_put_mds_session(ts);
	}
}

void ceph_mdsc_open_export_target_sessions(struct ceph_mds_client *mdsc,
					   struct ceph_mds_session *session)
{
	mutex_lock(&mdsc->mutex);
	__open_export_target_sessions(mdsc, session);
	mutex_unlock(&mdsc->mutex);
}

/*
 * session caps
 */

/* caller holds s_cap_lock, we drop it */
static void cleanup_cap_releases(struct ceph_mds_client *mdsc,
				 struct ceph_mds_session *session)
	__releases(session->s_cap_lock)
{
	LIST_HEAD(tmp_list);
	list_splice_init(&session->s_cap_releases, &tmp_list);
	session->s_num_cap_releases = 0;
	spin_unlock(&session->s_cap_lock);

	dout("cleanup_cap_releases mds%d\n", session->s_mds);
	while (!list_empty(&tmp_list)) {
		struct ceph_cap *cap;
		/* zero out the in-progress message */
		cap = list_first_entry(&tmp_list,
					struct ceph_cap, session_caps);
		list_del(&cap->session_caps);
		ceph_put_cap(mdsc, cap);
	}
}

static void cleanup_session_requests(struct ceph_mds_client *mdsc,
				     struct ceph_mds_session *session)
{
	struct ceph_mds_request *req;
	struct rb_node *p;

	dout("cleanup_session_requests mds%d\n", session->s_mds);
	mutex_lock(&mdsc->mutex);
	while (!list_empty(&session->s_unsafe)) {
		req = list_first_entry(&session->s_unsafe,
				       struct ceph_mds_request, r_unsafe_item);
		list_del_init(&req->r_unsafe_item);
		pr_warn_ratelimited(" dropping unsafe request %llu\n",
				    req->r_tid);
		__unregister_request(mdsc, req);
	}
	/* zero r_attempts, so kick_requests() will re-send requests */
	p = rb_first(&mdsc->request_tree);
	while (p) {
		req = rb_entry(p, struct ceph_mds_request, r_node);
		p = rb_next(p);
		if (req->r_session &&
		    req->r_session->s_mds == session->s_mds)
			req->r_attempts = 0;
	}
	mutex_unlock(&mdsc->mutex);
}

/*
 * Helper to safely iterate over all caps associated with a session, with
 * special care taken to handle a racing __ceph_remove_cap().
 *
 * Caller must hold session s_mutex.
 */
static int iterate_session_caps(struct ceph_mds_session *session,
				 int (*cb)(struct inode *, struct ceph_cap *,
					    void *), void *arg)
{
	struct list_head *p;
	struct ceph_cap *cap;
	struct inode *inode, *last_inode = NULL;
	struct ceph_cap *old_cap = NULL;
	int ret;

	dout("iterate_session_caps %p mds%d\n", session, session->s_mds);
	spin_lock(&session->s_cap_lock);
	p = session->s_caps.next;
	while (p != &session->s_caps) {
		cap = list_entry(p, struct ceph_cap, session_caps);
		inode = igrab(&cap->ci->vfs_inode);
		if (!inode) {
			p = p->next;
			continue;
		}
		session->s_cap_iterator = cap;
		spin_unlock(&session->s_cap_lock);

		if (last_inode) {
			iput(last_inode);
			last_inode = NULL;
		}
		if (old_cap) {
			ceph_put_cap(session->s_mdsc, old_cap);
			old_cap = NULL;
		}

		ret = cb(inode, cap, arg);
		last_inode = inode;

		spin_lock(&session->s_cap_lock);
		p = p->next;
		if (cap->ci == NULL) {
			dout("iterate_session_caps  finishing cap %p removal\n",
			     cap);
			BUG_ON(cap->session != session);
			cap->session = NULL;
			list_del_init(&cap->session_caps);
			session->s_nr_caps--;
			if (cap->queue_release) {
				list_add_tail(&cap->session_caps,
					      &session->s_cap_releases);
				session->s_num_cap_releases++;
			} else {
				old_cap = cap;  /* put_cap it w/o locks held */
			}
		}
		if (ret < 0)
			goto out;
	}
	ret = 0;
out:
	session->s_cap_iterator = NULL;
	spin_unlock(&session->s_cap_lock);

	iput(last_inode);
	if (old_cap)
		ceph_put_cap(session->s_mdsc, old_cap);

	return ret;
}

static int remove_session_caps_cb(struct inode *inode, struct ceph_cap *cap,
				  void *arg)
{
	struct ceph_fs_client *fsc = (struct ceph_fs_client *)arg;
	struct ceph_inode_info *ci = ceph_inode(inode);
	LIST_HEAD(to_remove);
	bool drop = false;
	bool invalidate = false;

	dout("removing cap %p, ci is %p, inode is %p\n",
	     cap, ci, &ci->vfs_inode);
	spin_lock(&ci->i_ceph_lock);
	__ceph_remove_cap(cap, false);
	if (!ci->i_auth_cap) {
		struct ceph_cap_flush *cf;
		struct ceph_mds_client *mdsc = fsc->mdsc;

		ci->i_ceph_flags |= CEPH_I_CAP_DROPPED;

		if (ci->i_wrbuffer_ref > 0 &&
		    ACCESS_ONCE(fsc->mount_state) == CEPH_MOUNT_SHUTDOWN)
			invalidate = true;

		while (!list_empty(&ci->i_cap_flush_list)) {
			cf = list_first_entry(&ci->i_cap_flush_list,
					      struct ceph_cap_flush, i_list);
			list_move(&cf->i_list, &to_remove);
		}

		spin_lock(&mdsc->cap_dirty_lock);

		list_for_each_entry(cf, &to_remove, i_list)
			list_del(&cf->g_list);

		if (!list_empty(&ci->i_dirty_item)) {
			pr_warn_ratelimited(
				" dropping dirty %s state for %p %lld\n",
				ceph_cap_string(ci->i_dirty_caps),
				inode, ceph_ino(inode));
			ci->i_dirty_caps = 0;
			list_del_init(&ci->i_dirty_item);
			drop = true;
		}
		if (!list_empty(&ci->i_flushing_item)) {
			pr_warn_ratelimited(
				" dropping dirty+flushing %s state for %p %lld\n",
				ceph_cap_string(ci->i_flushing_caps),
				inode, ceph_ino(inode));
			ci->i_flushing_caps = 0;
			list_del_init(&ci->i_flushing_item);
			mdsc->num_cap_flushing--;
			drop = true;
		}
		spin_unlock(&mdsc->cap_dirty_lock);

		if (!ci->i_dirty_caps && ci->i_prealloc_cap_flush) {
			list_add(&ci->i_prealloc_cap_flush->i_list, &to_remove);
			ci->i_prealloc_cap_flush = NULL;
		}
	}
	spin_unlock(&ci->i_ceph_lock);
	while (!list_empty(&to_remove)) {
		struct ceph_cap_flush *cf;
		cf = list_first_entry(&to_remove,
				      struct ceph_cap_flush, i_list);
		list_del(&cf->i_list);
		ceph_free_cap_flush(cf);
	}

	wake_up_all(&ci->i_cap_wq);
	if (invalidate)
		ceph_queue_invalidate(inode);
	if (drop)
		iput(inode);
	return 0;
}

/*
 * caller must hold session s_mutex
 */
static void remove_session_caps(struct ceph_mds_session *session)
{
	struct ceph_fs_client *fsc = session->s_mdsc->fsc;
	struct super_block *sb = fsc->sb;
	dout("remove_session_caps on %p\n", session);
	iterate_session_caps(session, remove_session_caps_cb, fsc);

	wake_up_all(&fsc->mdsc->cap_flushing_wq);

	spin_lock(&session->s_cap_lock);
	if (session->s_nr_caps > 0) {
		struct inode *inode;
		struct ceph_cap *cap, *prev = NULL;
		struct ceph_vino vino;
		/*
		 * iterate_session_caps() skips inodes that are being
		 * deleted, we need to wait until deletions are complete.
		 * __wait_on_freeing_inode() is designed for the job,
		 * but it is not exported, so use lookup inode function
		 * to access it.
		 */
		while (!list_empty(&session->s_caps)) {
			cap = list_entry(session->s_caps.next,
					 struct ceph_cap, session_caps);
			if (cap == prev)
				break;
			prev = cap;
			vino = cap->ci->i_vino;
			spin_unlock(&session->s_cap_lock);

			inode = ceph_find_inode(sb, vino);
			iput(inode);

			spin_lock(&session->s_cap_lock);
		}
	}

	// drop cap expires and unlock s_cap_lock
	cleanup_cap_releases(session->s_mdsc, session);

	BUG_ON(session->s_nr_caps > 0);
	BUG_ON(!list_empty(&session->s_cap_flushing));
}

/*
 * wake up any threads waiting on this session's caps.  if the cap is
 * old (didn't get renewed on the client reconnect), remove it now.
 *
 * caller must hold s_mutex.
 */
static int wake_up_session_cb(struct inode *inode, struct ceph_cap *cap,
			      void *arg)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	if (arg) {
		spin_lock(&ci->i_ceph_lock);
		ci->i_wanted_max_size = 0;
		ci->i_requested_max_size = 0;
		spin_unlock(&ci->i_ceph_lock);
	}
	wake_up_all(&ci->i_cap_wq);
	return 0;
}

static void wake_up_session_caps(struct ceph_mds_session *session,
				 int reconnect)
{
	dout("wake_up_session_caps %p mds%d\n", session, session->s_mds);
	iterate_session_caps(session, wake_up_session_cb,
			     (void *)(unsigned long)reconnect);
}

/*
 * Send periodic message to MDS renewing all currently held caps.  The
 * ack will reset the expiration for all caps from this session.
 *
 * caller holds s_mutex
 */
static int send_renew_caps(struct ceph_mds_client *mdsc,
			   struct ceph_mds_session *session)
{
	struct ceph_msg *msg;
	int state;

	if (time_after_eq(jiffies, session->s_cap_ttl) &&
	    time_after_eq(session->s_cap_ttl, session->s_renew_requested))
		pr_info("mds%d caps stale\n", session->s_mds);
	session->s_renew_requested = jiffies;

	/* do not try to renew caps until a recovering mds has reconnected
	 * with its clients. */
	state = ceph_mdsmap_get_state(mdsc->mdsmap, session->s_mds);
	if (state < CEPH_MDS_STATE_RECONNECT) {
		dout("send_renew_caps ignoring mds%d (%s)\n",
		     session->s_mds, ceph_mds_state_name(state));
		return 0;
	}

	dout("send_renew_caps to mds%d (%s)\n", session->s_mds,
		ceph_mds_state_name(state));
	msg = create_session_msg(CEPH_SESSION_REQUEST_RENEWCAPS,
				 ++session->s_renew_seq);
	if (!msg)
		return -ENOMEM;
	ceph_con_send(&session->s_con, msg);
	return 0;
}

static int send_flushmsg_ack(struct ceph_mds_client *mdsc,
			     struct ceph_mds_session *session, u64 seq)
{
	struct ceph_msg *msg;

	dout("send_flushmsg_ack to mds%d (%s)s seq %lld\n",
	     session->s_mds, ceph_session_state_name(session->s_state), seq);
	msg = create_session_msg(CEPH_SESSION_FLUSHMSG_ACK, seq);
	if (!msg)
		return -ENOMEM;
	ceph_con_send(&session->s_con, msg);
	return 0;
}


/*
 * Note new cap ttl, and any transition from stale -> not stale (fresh?).
 *
 * Called under session->s_mutex
 */
static void renewed_caps(struct ceph_mds_client *mdsc,
			 struct ceph_mds_session *session, int is_renew)
{
	int was_stale;
	int wake = 0;

	spin_lock(&session->s_cap_lock);
	was_stale = is_renew && time_after_eq(jiffies, session->s_cap_ttl);

	session->s_cap_ttl = session->s_renew_requested +
		mdsc->mdsmap->m_session_timeout*HZ;

	if (was_stale) {
		if (time_before(jiffies, session->s_cap_ttl)) {
			pr_info("mds%d caps renewed\n", session->s_mds);
			wake = 1;
		} else {
			pr_info("mds%d caps still stale\n", session->s_mds);
		}
	}
	dout("renewed_caps mds%d ttl now %lu, was %s, now %s\n",
	     session->s_mds, session->s_cap_ttl, was_stale ? "stale" : "fresh",
	     time_before(jiffies, session->s_cap_ttl) ? "stale" : "fresh");
	spin_unlock(&session->s_cap_lock);

	if (wake)
		wake_up_session_caps(session, 0);
}

/*
 * send a session close request
 */
static int request_close_session(struct ceph_mds_client *mdsc,
				 struct ceph_mds_session *session)
{
	struct ceph_msg *msg;

	dout("request_close_session mds%d state %s seq %lld\n",
	     session->s_mds, ceph_session_state_name(session->s_state),
	     session->s_seq);
	msg = create_session_msg(CEPH_SESSION_REQUEST_CLOSE, session->s_seq);
	if (!msg)
		return -ENOMEM;
	ceph_con_send(&session->s_con, msg);
	return 1;
}

/*
 * Called with s_mutex held.
 */
static int __close_session(struct ceph_mds_client *mdsc,
			 struct ceph_mds_session *session)
{
	if (session->s_state >= CEPH_MDS_SESSION_CLOSING)
		return 0;
	session->s_state = CEPH_MDS_SESSION_CLOSING;
	return request_close_session(mdsc, session);
}

/*
 * Trim old(er) caps.
 *
 * Because we can't cache an inode without one or more caps, we do
 * this indirectly: if a cap is unused, we prune its aliases, at which
 * point the inode will hopefully get dropped to.
 *
 * Yes, this is a bit sloppy.  Our only real goal here is to respond to
 * memory pressure from the MDS, though, so it needn't be perfect.
 */
static int trim_caps_cb(struct inode *inode, struct ceph_cap *cap, void *arg)
{
	struct ceph_mds_session *session = arg;
	struct ceph_inode_info *ci = ceph_inode(inode);
	int used, wanted, oissued, mine;

	if (session->s_trim_caps <= 0)
		return -1;

	spin_lock(&ci->i_ceph_lock);
	mine = cap->issued | cap->implemented;
	used = __ceph_caps_used(ci);
	wanted = __ceph_caps_file_wanted(ci);
	oissued = __ceph_caps_issued_other(ci, cap);

	dout("trim_caps_cb %p cap %p mine %s oissued %s used %s wanted %s\n",
	     inode, cap, ceph_cap_string(mine), ceph_cap_string(oissued),
	     ceph_cap_string(used), ceph_cap_string(wanted));
	if (cap == ci->i_auth_cap) {
		if (ci->i_dirty_caps || ci->i_flushing_caps ||
		    !list_empty(&ci->i_cap_snaps))
			goto out;
		if ((used | wanted) & CEPH_CAP_ANY_WR)
			goto out;
	}
	/* The inode has cached pages, but it's no longer used.
	 * we can safely drop it */
	if (wanted == 0 && used == CEPH_CAP_FILE_CACHE &&
	    !(oissued & CEPH_CAP_FILE_CACHE)) {
	  used = 0;
	  oissued = 0;
	}
	if ((used | wanted) & ~oissued & mine)
		goto out;   /* we need these caps */

	session->s_trim_caps--;
	if (oissued) {
		/* we aren't the only cap.. just remove us */
		__ceph_remove_cap(cap, true);
	} else {
		/* try dropping referring dentries */
		spin_unlock(&ci->i_ceph_lock);
		d_prune_aliases(inode);
		dout("trim_caps_cb %p cap %p  pruned, count now %d\n",
		     inode, cap, atomic_read(&inode->i_count));
		return 0;
	}

out:
	spin_unlock(&ci->i_ceph_lock);
	return 0;
}

/*
 * Trim session cap count down to some max number.
 */
static int trim_caps(struct ceph_mds_client *mdsc,
		     struct ceph_mds_session *session,
		     int max_caps)
{
	int trim_caps = session->s_nr_caps - max_caps;

	dout("trim_caps mds%d start: %d / %d, trim %d\n",
	     session->s_mds, session->s_nr_caps, max_caps, trim_caps);
	if (trim_caps > 0) {
		session->s_trim_caps = trim_caps;
		iterate_session_caps(session, trim_caps_cb, session);
		dout("trim_caps mds%d done: %d / %d, trimmed %d\n",
		     session->s_mds, session->s_nr_caps, max_caps,
			trim_caps - session->s_trim_caps);
		session->s_trim_caps = 0;
	}

	ceph_send_cap_releases(mdsc, session);
	return 0;
}

static int check_caps_flush(struct ceph_mds_client *mdsc,
			    u64 want_flush_tid)
{
	int ret = 1;

	spin_lock(&mdsc->cap_dirty_lock);
	if (!list_empty(&mdsc->cap_flush_list)) {
		struct ceph_cap_flush *cf =
			list_first_entry(&mdsc->cap_flush_list,
					 struct ceph_cap_flush, g_list);
		if (cf->tid <= want_flush_tid) {
			dout("check_caps_flush still flushing tid "
			     "%llu <= %llu\n", cf->tid, want_flush_tid);
			ret = 0;
		}
	}
	spin_unlock(&mdsc->cap_dirty_lock);
	return ret;
}

/*
 * flush all dirty inode data to disk.
 *
 * returns true if we've flushed through want_flush_tid
 */
static void wait_caps_flush(struct ceph_mds_client *mdsc,
			    u64 want_flush_tid)
{
	dout("check_caps_flush want %llu\n", want_flush_tid);

	wait_event(mdsc->cap_flushing_wq,
		   check_caps_flush(mdsc, want_flush_tid));

	dout("check_caps_flush ok, flushed thru %llu\n", want_flush_tid);
}

/*
 * called under s_mutex
 */
void ceph_send_cap_releases(struct ceph_mds_client *mdsc,
			    struct ceph_mds_session *session)
{
	struct ceph_msg *msg = NULL;
	struct ceph_mds_cap_release *head;
	struct ceph_mds_cap_item *item;
	struct ceph_cap *cap;
	LIST_HEAD(tmp_list);
	int num_cap_releases;

	spin_lock(&session->s_cap_lock);
again:
	list_splice_init(&session->s_cap_releases, &tmp_list);
	num_cap_releases = session->s_num_cap_releases;
	session->s_num_cap_releases = 0;
	spin_unlock(&session->s_cap_lock);

	while (!list_empty(&tmp_list)) {
		if (!msg) {
			msg = ceph_msg_new(CEPH_MSG_CLIENT_CAPRELEASE,
					PAGE_SIZE, GFP_NOFS, false);
			if (!msg)
				goto out_err;
			head = msg->front.iov_base;
			head->num = cpu_to_le32(0);
			msg->front.iov_len = sizeof(*head);
		}
		cap = list_first_entry(&tmp_list, struct ceph_cap,
					session_caps);
		list_del(&cap->session_caps);
		num_cap_releases--;

		head = msg->front.iov_base;
		le32_add_cpu(&head->num, 1);
		item = msg->front.iov_base + msg->front.iov_len;
		item->ino = cpu_to_le64(cap->cap_ino);
		item->cap_id = cpu_to_le64(cap->cap_id);
		item->migrate_seq = cpu_to_le32(cap->mseq);
		item->seq = cpu_to_le32(cap->issue_seq);
		msg->front.iov_len += sizeof(*item);

		ceph_put_cap(mdsc, cap);

		if (le32_to_cpu(head->num) == CEPH_CAPS_PER_RELEASE) {
			msg->hdr.front_len = cpu_to_le32(msg->front.iov_len);
			dout("send_cap_releases mds%d %p\n", session->s_mds, msg);
			ceph_con_send(&session->s_con, msg);
			msg = NULL;
		}
	}

	BUG_ON(num_cap_releases != 0);

	spin_lock(&session->s_cap_lock);
	if (!list_empty(&session->s_cap_releases))
		goto again;
	spin_unlock(&session->s_cap_lock);

	if (msg) {
		msg->hdr.front_len = cpu_to_le32(msg->front.iov_len);
		dout("send_cap_releases mds%d %p\n", session->s_mds, msg);
		ceph_con_send(&session->s_con, msg);
	}
	return;
out_err:
	pr_err("send_cap_releases mds%d, failed to allocate message\n",
		session->s_mds);
	spin_lock(&session->s_cap_lock);
	list_splice(&tmp_list, &session->s_cap_releases);
	session->s_num_cap_releases += num_cap_releases;
	spin_unlock(&session->s_cap_lock);
}

/*
 * requests
 */

int ceph_alloc_readdir_reply_buffer(struct ceph_mds_request *req,
				    struct inode *dir)
{
	struct ceph_inode_info *ci = ceph_inode(dir);
	struct ceph_mds_reply_info_parsed *rinfo = &req->r_reply_info;
	struct ceph_mount_options *opt = req->r_mdsc->fsc->mount_options;
	size_t size = sizeof(struct ceph_mds_reply_dir_entry);
	int order, num_entries;

	spin_lock(&ci->i_ceph_lock);
	num_entries = ci->i_files + ci->i_subdirs;
	spin_unlock(&ci->i_ceph_lock);
	num_entries = max(num_entries, 1);
	num_entries = min(num_entries, opt->max_readdir);

	order = get_order(size * num_entries);
	while (order >= 0) {
		rinfo->dir_entries = (void*)__get_free_pages(GFP_KERNEL |
							     __GFP_NOWARN,
							     order);
		if (rinfo->dir_entries)
			break;
		order--;
	}
	if (!rinfo->dir_entries)
		return -ENOMEM;

	num_entries = (PAGE_SIZE << order) / size;
	num_entries = min(num_entries, opt->max_readdir);

	rinfo->dir_buf_size = PAGE_SIZE << order;
	req->r_num_caps = num_entries + 1;
	req->r_args.readdir.max_entries = cpu_to_le32(num_entries);
	req->r_args.readdir.max_bytes = cpu_to_le32(opt->max_readdir_bytes);
	return 0;
}

/*
 * Create an mds request.
 */
struct ceph_mds_request *
ceph_mdsc_create_request(struct ceph_mds_client *mdsc, int op, int mode)
{
	struct ceph_mds_request *req = kzalloc(sizeof(*req), GFP_NOFS);

	if (!req)
		return ERR_PTR(-ENOMEM);

	mutex_init(&req->r_fill_mutex);
	req->r_mdsc = mdsc;
	req->r_started = jiffies;
	req->r_resend_mds = -1;
	INIT_LIST_HEAD(&req->r_unsafe_dir_item);
	INIT_LIST_HEAD(&req->r_unsafe_target_item);
	req->r_fmode = -1;
	kref_init(&req->r_kref);
	RB_CLEAR_NODE(&req->r_node);
	INIT_LIST_HEAD(&req->r_wait);
	init_completion(&req->r_completion);
	init_completion(&req->r_safe_completion);
	INIT_LIST_HEAD(&req->r_unsafe_item);

	req->r_stamp = current_fs_time(mdsc->fsc->sb);

	req->r_op = op;
	req->r_direct_mode = mode;
	return req;
}

/*
 * return oldest (lowest) request, tid in request tree, 0 if none.
 *
 * called under mdsc->mutex.
 */
static struct ceph_mds_request *__get_oldest_req(struct ceph_mds_client *mdsc)
{
	if (RB_EMPTY_ROOT(&mdsc->request_tree))
		return NULL;
	return rb_entry(rb_first(&mdsc->request_tree),
			struct ceph_mds_request, r_node);
}

static inline  u64 __get_oldest_tid(struct ceph_mds_client *mdsc)
{
	return mdsc->oldest_tid;
}

/*
 * Build a dentry's path.  Allocate on heap; caller must kfree.  Based
 * on build_path_from_dentry in fs/cifs/dir.c.
 *
 * If @stop_on_nosnap, generate path relative to the first non-snapped
 * inode.
 *
 * Encode hidden .snap dirs as a double /, i.e.
 *   foo/.snap/bar -> foo//bar
 */
char *ceph_mdsc_build_path(struct dentry *dentry, int *plen, u64 *base,
			   int stop_on_nosnap)
{
	struct dentry *temp;
	char *path;
	int len, pos;
	unsigned seq;

	if (dentry == NULL)
		return ERR_PTR(-EINVAL);

retry:
	len = 0;
	seq = read_seqbegin(&rename_lock);
	rcu_read_lock();
	for (temp = dentry; !IS_ROOT(temp);) {
		struct inode *inode = d_inode(temp);
		if (inode && ceph_snap(inode) == CEPH_SNAPDIR)
			len++;  /* slash only */
		else if (stop_on_nosnap && inode &&
			 ceph_snap(inode) == CEPH_NOSNAP)
			break;
		else
			len += 1 + temp->d_name.len;
		temp = temp->d_parent;
	}
	rcu_read_unlock();
	if (len)
		len--;  /* no leading '/' */

	path = kmalloc(len+1, GFP_NOFS);
	if (path == NULL)
		return ERR_PTR(-ENOMEM);
	pos = len;
	path[pos] = 0;	/* trailing null */
	rcu_read_lock();
	for (temp = dentry; !IS_ROOT(temp) && pos != 0; ) {
		struct inode *inode;

		spin_lock(&temp->d_lock);
		inode = d_inode(temp);
		if (inode && ceph_snap(inode) == CEPH_SNAPDIR) {
			dout("build_path path+%d: %p SNAPDIR\n",
			     pos, temp);
		} else if (stop_on_nosnap && inode &&
			   ceph_snap(inode) == CEPH_NOSNAP) {
			spin_unlock(&temp->d_lock);
			break;
		} else {
			pos -= temp->d_name.len;
			if (pos < 0) {
				spin_unlock(&temp->d_lock);
				break;
			}
			strncpy(path + pos, temp->d_name.name,
				temp->d_name.len);
		}
		spin_unlock(&temp->d_lock);
		if (pos)
			path[--pos] = '/';
		temp = temp->d_parent;
	}
	rcu_read_unlock();
	if (pos != 0 || read_seqretry(&rename_lock, seq)) {
		pr_err("build_path did not end path lookup where "
		       "expected, namelen is %d, pos is %d\n", len, pos);
		/* presumably this is only possible if racing with a
		   rename of one of the parent directories (we can not
		   lock the dentries above us to prevent this, but
		   retrying should be harmless) */
		kfree(path);
		goto retry;
	}

	*base = ceph_ino(d_inode(temp));
	*plen = len;
	dout("build_path on %p %d built %llx '%.*s'\n",
	     dentry, d_count(dentry), *base, len, path);
	return path;
}

static int build_dentry_path(struct dentry *dentry,
			     const char **ppath, int *ppathlen, u64 *pino,
			     int *pfreepath)
{
	char *path;

	if (ceph_snap(d_inode(dentry->d_parent)) == CEPH_NOSNAP) {
		*pino = ceph_ino(d_inode(dentry->d_parent));
		*ppath = dentry->d_name.name;
		*ppathlen = dentry->d_name.len;
		return 0;
	}
	path = ceph_mdsc_build_path(dentry, ppathlen, pino, 1);
	if (IS_ERR(path))
		return PTR_ERR(path);
	*ppath = path;
	*pfreepath = 1;
	return 0;
}

static int build_inode_path(struct inode *inode,
			    const char **ppath, int *ppathlen, u64 *pino,
			    int *pfreepath)
{
	struct dentry *dentry;
	char *path;

	if (ceph_snap(inode) == CEPH_NOSNAP) {
		*pino = ceph_ino(inode);
		*ppathlen = 0;
		return 0;
	}
	dentry = d_find_alias(inode);
	path = ceph_mdsc_build_path(dentry, ppathlen, pino, 1);
	dput(dentry);
	if (IS_ERR(path))
		return PTR_ERR(path);
	*ppath = path;
	*pfreepath = 1;
	return 0;
}

/*
 * request arguments may be specified via an inode *, a dentry *, or
 * an explicit ino+path.
 */
static int set_request_path_attr(struct inode *rinode, struct dentry *rdentry,
				  const char *rpath, u64 rino,
				  const char **ppath, int *pathlen,
				  u64 *ino, int *freepath)
{
	int r = 0;

	if (rinode) {
		r = build_inode_path(rinode, ppath, pathlen, ino, freepath);
		dout(" inode %p %llx.%llx\n", rinode, ceph_ino(rinode),
		     ceph_snap(rinode));
	} else if (rdentry) {
		r = build_dentry_path(rdentry, ppath, pathlen, ino, freepath);
		dout(" dentry %p %llx/%.*s\n", rdentry, *ino, *pathlen,
		     *ppath);
	} else if (rpath || rino) {
		*ino = rino;
		*ppath = rpath;
		*pathlen = rpath ? strlen(rpath) : 0;
		dout(" path %.*s\n", *pathlen, rpath);
	}

	return r;
}

/*
 * called under mdsc->mutex
 */
static struct ceph_msg *create_request_message(struct ceph_mds_client *mdsc,
					       struct ceph_mds_request *req,
					       int mds, bool drop_cap_releases)
{
	struct ceph_msg *msg;
	struct ceph_mds_request_head *head;
	const char *path1 = NULL;
	const char *path2 = NULL;
	u64 ino1 = 0, ino2 = 0;
	int pathlen1 = 0, pathlen2 = 0;
	int freepath1 = 0, freepath2 = 0;
	int len;
	u16 releases;
	void *p, *end;
	int ret;

	ret = set_request_path_attr(req->r_inode, req->r_dentry,
			      req->r_path1, req->r_ino1.ino,
			      &path1, &pathlen1, &ino1, &freepath1);
	if (ret < 0) {
		msg = ERR_PTR(ret);
		goto out;
	}

	ret = set_request_path_attr(NULL, req->r_old_dentry,
			      req->r_path2, req->r_ino2.ino,
			      &path2, &pathlen2, &ino2, &freepath2);
	if (ret < 0) {
		msg = ERR_PTR(ret);
		goto out_free1;
	}

	len = sizeof(*head) +
		pathlen1 + pathlen2 + 2*(1 + sizeof(u32) + sizeof(u64)) +
		sizeof(struct ceph_timespec);

	/* calculate (max) length for cap releases */
	len += sizeof(struct ceph_mds_request_release) *
		(!!req->r_inode_drop + !!req->r_dentry_drop +
		 !!req->r_old_inode_drop + !!req->r_old_dentry_drop);
	if (req->r_dentry_drop)
		len += req->r_dentry->d_name.len;
	if (req->r_old_dentry_drop)
		len += req->r_old_dentry->d_name.len;

	msg = ceph_msg_new(CEPH_MSG_CLIENT_REQUEST, len, GFP_NOFS, false);
	if (!msg) {
		msg = ERR_PTR(-ENOMEM);
		goto out_free2;
	}

	msg->hdr.version = cpu_to_le16(2);
	msg->hdr.tid = cpu_to_le64(req->r_tid);

	head = msg->front.iov_base;
	p = msg->front.iov_base + sizeof(*head);
	end = msg->front.iov_base + msg->front.iov_len;

	head->mdsmap_epoch = cpu_to_le32(mdsc->mdsmap->m_epoch);
	head->op = cpu_to_le32(req->r_op);
	head->caller_uid = cpu_to_le32(from_kuid(&init_user_ns, req->r_uid));
	head->caller_gid = cpu_to_le32(from_kgid(&init_user_ns, req->r_gid));
	head->args = req->r_args;

	ceph_encode_filepath(&p, end, ino1, path1);
	ceph_encode_filepath(&p, end, ino2, path2);

	/* make note of release offset, in case we need to replay */
	req->r_request_release_offset = p - msg->front.iov_base;

	/* cap releases */
	releases = 0;
	if (req->r_inode_drop)
		releases += ceph_encode_inode_release(&p,
		      req->r_inode ? req->r_inode : d_inode(req->r_dentry),
		      mds, req->r_inode_drop, req->r_inode_unless, 0);
	if (req->r_dentry_drop)
		releases += ceph_encode_dentry_release(&p, req->r_dentry,
		       mds, req->r_dentry_drop, req->r_dentry_unless);
	if (req->r_old_dentry_drop)
		releases += ceph_encode_dentry_release(&p, req->r_old_dentry,
		       mds, req->r_old_dentry_drop, req->r_old_dentry_unless);
	if (req->r_old_inode_drop)
		releases += ceph_encode_inode_release(&p,
		      d_inode(req->r_old_dentry),
		      mds, req->r_old_inode_drop, req->r_old_inode_unless, 0);

	if (drop_cap_releases) {
		releases = 0;
		p = msg->front.iov_base + req->r_request_release_offset;
	}

	head->num_releases = cpu_to_le16(releases);

	/* time stamp */
	{
		struct ceph_timespec ts;
		ceph_encode_timespec(&ts, &req->r_stamp);
		ceph_encode_copy(&p, &ts, sizeof(ts));
	}

	BUG_ON(p > end);
	msg->front.iov_len = p - msg->front.iov_base;
	msg->hdr.front_len = cpu_to_le32(msg->front.iov_len);

	if (req->r_pagelist) {
		struct ceph_pagelist *pagelist = req->r_pagelist;
		atomic_inc(&pagelist->refcnt);
		ceph_msg_data_add_pagelist(msg, pagelist);
		msg->hdr.data_len = cpu_to_le32(pagelist->length);
	} else {
		msg->hdr.data_len = 0;
	}

	msg->hdr.data_off = cpu_to_le16(0);

out_free2:
	if (freepath2)
		kfree((char *)path2);
out_free1:
	if (freepath1)
		kfree((char *)path1);
out:
	return msg;
}

/*
 * called under mdsc->mutex if error, under no mutex if
 * success.
 */
static void complete_request(struct ceph_mds_client *mdsc,
			     struct ceph_mds_request *req)
{
	if (req->r_callback)
		req->r_callback(mdsc, req);
	else
		complete_all(&req->r_completion);
}

/*
 * called under mdsc->mutex
 */
static int __prepare_send_request(struct ceph_mds_client *mdsc,
				  struct ceph_mds_request *req,
				  int mds, bool drop_cap_releases)
{
	struct ceph_mds_request_head *rhead;
	struct ceph_msg *msg;
	int flags = 0;

	req->r_attempts++;
	if (req->r_inode) {
		struct ceph_cap *cap =
			ceph_get_cap_for_mds(ceph_inode(req->r_inode), mds);

		if (cap)
			req->r_sent_on_mseq = cap->mseq;
		else
			req->r_sent_on_mseq = -1;
	}
	dout("prepare_send_request %p tid %lld %s (attempt %d)\n", req,
	     req->r_tid, ceph_mds_op_name(req->r_op), req->r_attempts);

	if (req->r_got_unsafe) {
		void *p;
		/*
		 * Replay.  Do not regenerate message (and rebuild
		 * paths, etc.); just use the original message.
		 * Rebuilding paths will break for renames because
		 * d_move mangles the src name.
		 */
		msg = req->r_request;
		rhead = msg->front.iov_base;

		flags = le32_to_cpu(rhead->flags);
		flags |= CEPH_MDS_FLAG_REPLAY;
		rhead->flags = cpu_to_le32(flags);

		if (req->r_target_inode)
			rhead->ino = cpu_to_le64(ceph_ino(req->r_target_inode));

		rhead->num_retry = req->r_attempts - 1;

		/* remove cap/dentry releases from message */
		rhead->num_releases = 0;

		/* time stamp */
		p = msg->front.iov_base + req->r_request_release_offset;
		{
			struct ceph_timespec ts;
			ceph_encode_timespec(&ts, &req->r_stamp);
			ceph_encode_copy(&p, &ts, sizeof(ts));
		}

		msg->front.iov_len = p - msg->front.iov_base;
		msg->hdr.front_len = cpu_to_le32(msg->front.iov_len);
		return 0;
	}

	if (req->r_request) {
		ceph_msg_put(req->r_request);
		req->r_request = NULL;
	}
	msg = create_request_message(mdsc, req, mds, drop_cap_releases);
	if (IS_ERR(msg)) {
		req->r_err = PTR_ERR(msg);
		return PTR_ERR(msg);
	}
	req->r_request = msg;

	rhead = msg->front.iov_base;
	rhead->oldest_client_tid = cpu_to_le64(__get_oldest_tid(mdsc));
	if (req->r_got_unsafe)
		flags |= CEPH_MDS_FLAG_REPLAY;
	if (req->r_locked_dir)
		flags |= CEPH_MDS_FLAG_WANT_DENTRY;
	rhead->flags = cpu_to_le32(flags);
	rhead->num_fwd = req->r_num_fwd;
	rhead->num_retry = req->r_attempts - 1;
	rhead->ino = 0;

	dout(" r_locked_dir = %p\n", req->r_locked_dir);
	return 0;
}

/*
 * send request, or put it on the appropriate wait list.
 */
static int __do_request(struct ceph_mds_client *mdsc,
			struct ceph_mds_request *req)
{
	struct ceph_mds_session *session = NULL;
	int mds = -1;
	int err = 0;

	if (req->r_err || req->r_got_result) {
		if (req->r_aborted)
			__unregister_request(mdsc, req);
		goto out;
	}

	if (req->r_timeout &&
	    time_after_eq(jiffies, req->r_started + req->r_timeout)) {
		dout("do_request timed out\n");
		err = -EIO;
		goto finish;
	}
	if (ACCESS_ONCE(mdsc->fsc->mount_state) == CEPH_MOUNT_SHUTDOWN) {
		dout("do_request forced umount\n");
		err = -EIO;
		goto finish;
	}
	if (ACCESS_ONCE(mdsc->fsc->mount_state) == CEPH_MOUNT_MOUNTING) {
		if (mdsc->mdsmap_err) {
			err = mdsc->mdsmap_err;
			dout("do_request mdsmap err %d\n", err);
			goto finish;
		}
		if (mdsc->mdsmap->m_epoch == 0) {
			dout("do_request no mdsmap, waiting for map\n");
			list_add(&req->r_wait, &mdsc->waiting_for_map);
			goto finish;
		}
		if (!(mdsc->fsc->mount_options->flags &
		      CEPH_MOUNT_OPT_MOUNTWAIT) &&
		    !ceph_mdsmap_is_cluster_available(mdsc->mdsmap)) {
			err = -ENOENT;
			pr_info("probably no mds server is up\n");
			goto finish;
		}
	}

	put_request_session(req);

	mds = __choose_mds(mdsc, req);
	if (mds < 0 ||
	    ceph_mdsmap_get_state(mdsc->mdsmap, mds) < CEPH_MDS_STATE_ACTIVE) {
		dout("do_request no mds or not active, waiting for map\n");
		list_add(&req->r_wait, &mdsc->waiting_for_map);
		goto out;
	}

	/* get, open session */
	session = __ceph_lookup_mds_session(mdsc, mds);
	if (!session) {
		session = register_session(mdsc, mds);
		if (IS_ERR(session)) {
			err = PTR_ERR(session);
			goto finish;
		}
	}
	req->r_session = get_session(session);

	dout("do_request mds%d session %p state %s\n", mds, session,
	     ceph_session_state_name(session->s_state));
	if (session->s_state != CEPH_MDS_SESSION_OPEN &&
	    session->s_state != CEPH_MDS_SESSION_HUNG) {
		if (session->s_state == CEPH_MDS_SESSION_REJECTED) {
			err = -EACCES;
			goto out_session;
		}
		if (session->s_state == CEPH_MDS_SESSION_NEW ||
		    session->s_state == CEPH_MDS_SESSION_CLOSING)
			__open_session(mdsc, session);
		list_add(&req->r_wait, &session->s_waiting);
		goto out_session;
	}

	/* send request */
	req->r_resend_mds = -1;   /* forget any previous mds hint */

	if (req->r_request_started == 0)   /* note request start time */
		req->r_request_started = jiffies;

	err = __prepare_send_request(mdsc, req, mds, false);
	if (!err) {
		ceph_msg_get(req->r_request);
		ceph_con_send(&session->s_con, req->r_request);
	}

out_session:
	ceph_put_mds_session(session);
finish:
	if (err) {
		dout("__do_request early error %d\n", err);
		req->r_err = err;
		complete_request(mdsc, req);
		__unregister_request(mdsc, req);
	}
out:
	return err;
}

/*
 * called under mdsc->mutex
 */
static void __wake_requests(struct ceph_mds_client *mdsc,
			    struct list_head *head)
{
	struct ceph_mds_request *req;
	LIST_HEAD(tmp_list);

	list_splice_init(head, &tmp_list);

	while (!list_empty(&tmp_list)) {
		req = list_entry(tmp_list.next,
				 struct ceph_mds_request, r_wait);
		list_del_init(&req->r_wait);
		dout(" wake request %p tid %llu\n", req, req->r_tid);
		__do_request(mdsc, req);
	}
}

/*
 * Wake up threads with requests pending for @mds, so that they can
 * resubmit their requests to a possibly different mds.
 */
static void kick_requests(struct ceph_mds_client *mdsc, int mds)
{
	struct ceph_mds_request *req;
	struct rb_node *p = rb_first(&mdsc->request_tree);

	dout("kick_requests mds%d\n", mds);
	while (p) {
		req = rb_entry(p, struct ceph_mds_request, r_node);
		p = rb_next(p);
		if (req->r_got_unsafe)
			continue;
		if (req->r_attempts > 0)
			continue; /* only new requests */
		if (req->r_session &&
		    req->r_session->s_mds == mds) {
			dout(" kicking tid %llu\n", req->r_tid);
			list_del_init(&req->r_wait);
			__do_request(mdsc, req);
		}
	}
}

void ceph_mdsc_submit_request(struct ceph_mds_client *mdsc,
			      struct ceph_mds_request *req)
{
	dout("submit_request on %p\n", req);
	mutex_lock(&mdsc->mutex);
	__register_request(mdsc, req, NULL);
	__do_request(mdsc, req);
	mutex_unlock(&mdsc->mutex);
}

/*
 * Synchrously perform an mds request.  Take care of all of the
 * session setup, forwarding, retry details.
 */
int ceph_mdsc_do_request(struct ceph_mds_client *mdsc,
			 struct inode *dir,
			 struct ceph_mds_request *req)
{
	int err;

	dout("do_request on %p\n", req);

	/* take CAP_PIN refs for r_inode, r_locked_dir, r_old_dentry */
	if (req->r_inode)
		ceph_get_cap_refs(ceph_inode(req->r_inode), CEPH_CAP_PIN);
	if (req->r_locked_dir)
		ceph_get_cap_refs(ceph_inode(req->r_locked_dir), CEPH_CAP_PIN);
	if (req->r_old_dentry_dir)
		ceph_get_cap_refs(ceph_inode(req->r_old_dentry_dir),
				  CEPH_CAP_PIN);

	/* issue */
	mutex_lock(&mdsc->mutex);
	__register_request(mdsc, req, dir);
	__do_request(mdsc, req);

	if (req->r_err) {
		err = req->r_err;
		goto out;
	}

	/* wait */
	mutex_unlock(&mdsc->mutex);
	dout("do_request waiting\n");
	if (!req->r_timeout && req->r_wait_for_completion) {
		err = req->r_wait_for_completion(mdsc, req);
	} else {
		long timeleft = wait_for_completion_killable_timeout(
					&req->r_completion,
					ceph_timeout_jiffies(req->r_timeout));
		if (timeleft > 0)
			err = 0;
		else if (!timeleft)
			err = -EIO;  /* timed out */
		else
			err = timeleft;  /* killed */
	}
	dout("do_request waited, got %d\n", err);
	mutex_lock(&mdsc->mutex);

	/* only abort if we didn't race with a real reply */
	if (req->r_got_result) {
		err = le32_to_cpu(req->r_reply_info.head->result);
	} else if (err < 0) {
		dout("aborted request %lld with %d\n", req->r_tid, err);

		/*
		 * ensure we aren't running concurrently with
		 * ceph_fill_trace or ceph_readdir_prepopulate, which
		 * rely on locks (dir mutex) held by our caller.
		 */
		mutex_lock(&req->r_fill_mutex);
		req->r_err = err;
		req->r_aborted = true;
		mutex_unlock(&req->r_fill_mutex);

		if (req->r_locked_dir &&
		    (req->r_op & CEPH_MDS_OP_WRITE))
			ceph_invalidate_dir_request(req);
	} else {
		err = req->r_err;
	}

out:
	mutex_unlock(&mdsc->mutex);
	dout("do_request %p done, result %d\n", req, err);
	return err;
}

/*
 * Invalidate dir's completeness, dentry lease state on an aborted MDS
 * namespace request.
 */
void ceph_invalidate_dir_request(struct ceph_mds_request *req)
{
	struct inode *inode = req->r_locked_dir;

	dout("invalidate_dir_request %p (complete, lease(s))\n", inode);

	ceph_dir_clear_complete(inode);
	if (req->r_dentry)
		ceph_invalidate_dentry_lease(req->r_dentry);
	if (req->r_old_dentry)
		ceph_invalidate_dentry_lease(req->r_old_dentry);
}

/*
 * Handle mds reply.
 *
 * We take the session mutex and parse and process the reply immediately.
 * This preserves the logical ordering of replies, capabilities, etc., sent
 * by the MDS as they are applied to our local cache.
 */
static void handle_reply(struct ceph_mds_session *session, struct ceph_msg *msg)
{
	struct ceph_mds_client *mdsc = session->s_mdsc;
	struct ceph_mds_request *req;
	struct ceph_mds_reply_head *head = msg->front.iov_base;
	struct ceph_mds_reply_info_parsed *rinfo;  /* parsed reply info */
	struct ceph_snap_realm *realm;
	u64 tid;
	int err, result;
	int mds = session->s_mds;

	if (msg->front.iov_len < sizeof(*head)) {
		pr_err("mdsc_handle_reply got corrupt (short) reply\n");
		ceph_msg_dump(msg);
		return;
	}

	/* get request, session */
	tid = le64_to_cpu(msg->hdr.tid);
	mutex_lock(&mdsc->mutex);
	req = lookup_get_request(mdsc, tid);
	if (!req) {
		dout("handle_reply on unknown tid %llu\n", tid);
		mutex_unlock(&mdsc->mutex);
		return;
	}
	dout("handle_reply %p\n", req);

	/* correct session? */
	if (req->r_session != session) {
		pr_err("mdsc_handle_reply got %llu on session mds%d"
		       " not mds%d\n", tid, session->s_mds,
		       req->r_session ? req->r_session->s_mds : -1);
		mutex_unlock(&mdsc->mutex);
		goto out;
	}

	/* dup? */
	if ((req->r_got_unsafe && !head->safe) ||
	    (req->r_got_safe && head->safe)) {
		pr_warn("got a dup %s reply on %llu from mds%d\n",
			   head->safe ? "safe" : "unsafe", tid, mds);
		mutex_unlock(&mdsc->mutex);
		goto out;
	}
	if (req->r_got_safe) {
		pr_warn("got unsafe after safe on %llu from mds%d\n",
			   tid, mds);
		mutex_unlock(&mdsc->mutex);
		goto out;
	}

	result = le32_to_cpu(head->result);

	/*
	 * Handle an ESTALE
	 * if we're not talking to the authority, send to them
	 * if the authority has changed while we weren't looking,
	 * send to new authority
	 * Otherwise we just have to return an ESTALE
	 */
	if (result == -ESTALE) {
		dout("got ESTALE on request %llu", req->r_tid);
		req->r_resend_mds = -1;
		if (req->r_direct_mode != USE_AUTH_MDS) {
			dout("not using auth, setting for that now");
			req->r_direct_mode = USE_AUTH_MDS;
			__do_request(mdsc, req);
			mutex_unlock(&mdsc->mutex);
			goto out;
		} else  {
			int mds = __choose_mds(mdsc, req);
			if (mds >= 0 && mds != req->r_session->s_mds) {
				dout("but auth changed, so resending");
				__do_request(mdsc, req);
				mutex_unlock(&mdsc->mutex);
				goto out;
			}
		}
		dout("have to return ESTALE on request %llu", req->r_tid);
	}


	if (head->safe) {
		req->r_got_safe = true;
		__unregister_request(mdsc, req);

		if (req->r_got_unsafe) {
			/*
			 * We already handled the unsafe response, now do the
			 * cleanup.  No need to examine the response; the MDS
			 * doesn't include any result info in the safe
			 * response.  And even if it did, there is nothing
			 * useful we could do with a revised return value.
			 */
			dout("got safe reply %llu, mds%d\n", tid, mds);
			list_del_init(&req->r_unsafe_item);

			/* last unsafe request during umount? */
			if (mdsc->stopping && !__get_oldest_req(mdsc))
				complete_all(&mdsc->safe_umount_waiters);
			mutex_unlock(&mdsc->mutex);
			goto out;
		}
	} else {
		req->r_got_unsafe = true;
		list_add_tail(&req->r_unsafe_item, &req->r_session->s_unsafe);
		if (req->r_unsafe_dir) {
			struct ceph_inode_info *ci =
					ceph_inode(req->r_unsafe_dir);
			spin_lock(&ci->i_unsafe_lock);
			list_add_tail(&req->r_unsafe_dir_item,
				      &ci->i_unsafe_dirops);
			spin_unlock(&ci->i_unsafe_lock);
		}
	}

	dout("handle_reply tid %lld result %d\n", tid, result);
	rinfo = &req->r_reply_info;
	err = parse_reply_info(msg, rinfo, session->s_con.peer_features);
	mutex_unlock(&mdsc->mutex);

	mutex_lock(&session->s_mutex);
	if (err < 0) {
		pr_err("mdsc_handle_reply got corrupt reply mds%d(tid:%lld)\n", mds, tid);
		ceph_msg_dump(msg);
		goto out_err;
	}

	/* snap trace */
	realm = NULL;
	if (rinfo->snapblob_len) {
		down_write(&mdsc->snap_rwsem);
		ceph_update_snap_trace(mdsc, rinfo->snapblob,
				rinfo->snapblob + rinfo->snapblob_len,
				le32_to_cpu(head->op) == CEPH_MDS_OP_RMSNAP,
				&realm);
		downgrade_write(&mdsc->snap_rwsem);
	} else {
		down_read(&mdsc->snap_rwsem);
	}

	/* insert trace into our cache */
	mutex_lock(&req->r_fill_mutex);
	current->journal_info = req;
	err = ceph_fill_trace(mdsc->fsc->sb, req, req->r_session);
	if (err == 0) {
		if (result == 0 && (req->r_op == CEPH_MDS_OP_READDIR ||
				    req->r_op == CEPH_MDS_OP_LSSNAP))
			ceph_readdir_prepopulate(req, req->r_session);
		ceph_unreserve_caps(mdsc, &req->r_caps_reservation);
	}
	current->journal_info = NULL;
	mutex_unlock(&req->r_fill_mutex);

	up_read(&mdsc->snap_rwsem);
	if (realm)
		ceph_put_snap_realm(mdsc, realm);

	if (err == 0 && req->r_got_unsafe && req->r_target_inode) {
		struct ceph_inode_info *ci = ceph_inode(req->r_target_inode);
		spin_lock(&ci->i_unsafe_lock);
		list_add_tail(&req->r_unsafe_target_item, &ci->i_unsafe_iops);
		spin_unlock(&ci->i_unsafe_lock);
	}
out_err:
	mutex_lock(&mdsc->mutex);
	if (!req->r_aborted) {
		if (err) {
			req->r_err = err;
		} else {
			req->r_reply =  ceph_msg_get(msg);
			req->r_got_result = true;
		}
	} else {
		dout("reply arrived after request %lld was aborted\n", tid);
	}
	mutex_unlock(&mdsc->mutex);

	mutex_unlock(&session->s_mutex);

	/* kick calling process */
	complete_request(mdsc, req);
out:
	ceph_mdsc_put_request(req);
	return;
}



/*
 * handle mds notification that our request has been forwarded.
 */
static void handle_forward(struct ceph_mds_client *mdsc,
			   struct ceph_mds_session *session,
			   struct ceph_msg *msg)
{
	struct ceph_mds_request *req;
	u64 tid = le64_to_cpu(msg->hdr.tid);
	u32 next_mds;
	u32 fwd_seq;
	int err = -EINVAL;
	void *p = msg->front.iov_base;
	void *end = p + msg->front.iov_len;

	ceph_decode_need(&p, end, 2*sizeof(u32), bad);
	next_mds = ceph_decode_32(&p);
	fwd_seq = ceph_decode_32(&p);

	mutex_lock(&mdsc->mutex);
	req = lookup_get_request(mdsc, tid);
	if (!req) {
		dout("forward tid %llu to mds%d - req dne\n", tid, next_mds);
		goto out;  /* dup reply? */
	}

	if (req->r_aborted) {
		dout("forward tid %llu aborted, unregistering\n", tid);
		__unregister_request(mdsc, req);
	} else if (fwd_seq <= req->r_num_fwd) {
		dout("forward tid %llu to mds%d - old seq %d <= %d\n",
		     tid, next_mds, req->r_num_fwd, fwd_seq);
	} else {
		/* resend. forward race not possible; mds would drop */
		dout("forward tid %llu to mds%d (we resend)\n", tid, next_mds);
		BUG_ON(req->r_err);
		BUG_ON(req->r_got_result);
		req->r_attempts = 0;
		req->r_num_fwd = fwd_seq;
		req->r_resend_mds = next_mds;
		put_request_session(req);
		__do_request(mdsc, req);
	}
	ceph_mdsc_put_request(req);
out:
	mutex_unlock(&mdsc->mutex);
	return;

bad:
	pr_err("mdsc_handle_forward decode error err=%d\n", err);
}

/*
 * handle a mds session control message
 */
static void handle_session(struct ceph_mds_session *session,
			   struct ceph_msg *msg)
{
	struct ceph_mds_client *mdsc = session->s_mdsc;
	u32 op;
	u64 seq;
	int mds = session->s_mds;
	struct ceph_mds_session_head *h = msg->front.iov_base;
	int wake = 0;

	/* decode */
	if (msg->front.iov_len != sizeof(*h))
		goto bad;
	op = le32_to_cpu(h->op);
	seq = le64_to_cpu(h->seq);

	mutex_lock(&mdsc->mutex);
	if (op == CEPH_SESSION_CLOSE)
		__unregister_session(mdsc, session);
	/* FIXME: this ttl calculation is generous */
	session->s_ttl = jiffies + HZ*mdsc->mdsmap->m_session_autoclose;
	mutex_unlock(&mdsc->mutex);

	mutex_lock(&session->s_mutex);

	dout("handle_session mds%d %s %p state %s seq %llu\n",
	     mds, ceph_session_op_name(op), session,
	     ceph_session_state_name(session->s_state), seq);

	if (session->s_state == CEPH_MDS_SESSION_HUNG) {
		session->s_state = CEPH_MDS_SESSION_OPEN;
		pr_info("mds%d came back\n", session->s_mds);
	}

	switch (op) {
	case CEPH_SESSION_OPEN:
		if (session->s_state == CEPH_MDS_SESSION_RECONNECTING)
			pr_info("mds%d reconnect success\n", session->s_mds);
		session->s_state = CEPH_MDS_SESSION_OPEN;
		renewed_caps(mdsc, session, 0);
		wake = 1;
		if (mdsc->stopping)
			__close_session(mdsc, session);
		break;

	case CEPH_SESSION_RENEWCAPS:
		if (session->s_renew_seq == seq)
			renewed_caps(mdsc, session, 1);
		break;

	case CEPH_SESSION_CLOSE:
		if (session->s_state == CEPH_MDS_SESSION_RECONNECTING)
			pr_info("mds%d reconnect denied\n", session->s_mds);
		cleanup_session_requests(mdsc, session);
		remove_session_caps(session);
		wake = 2; /* for good measure */
		wake_up_all(&mdsc->session_close_wq);
		break;

	case CEPH_SESSION_STALE:
		pr_info("mds%d caps went stale, renewing\n",
			session->s_mds);
		spin_lock(&session->s_gen_ttl_lock);
		session->s_cap_gen++;
		session->s_cap_ttl = jiffies - 1;
		spin_unlock(&session->s_gen_ttl_lock);
		send_renew_caps(mdsc, session);
		break;

	case CEPH_SESSION_RECALL_STATE:
		trim_caps(mdsc, session, le32_to_cpu(h->max_caps));
		break;

	case CEPH_SESSION_FLUSHMSG:
		send_flushmsg_ack(mdsc, session, seq);
		break;

	case CEPH_SESSION_FORCE_RO:
		dout("force_session_readonly %p\n", session);
		spin_lock(&session->s_cap_lock);
		session->s_readonly = true;
		spin_unlock(&session->s_cap_lock);
		wake_up_session_caps(session, 0);
		break;

	case CEPH_SESSION_REJECT:
		WARN_ON(session->s_state != CEPH_MDS_SESSION_OPENING);
		pr_info("mds%d rejected session\n", session->s_mds);
		session->s_state = CEPH_MDS_SESSION_REJECTED;
		cleanup_session_requests(mdsc, session);
		remove_session_caps(session);
		wake = 2; /* for good measure */
		break;

	default:
		pr_err("mdsc_handle_session bad op %d mds%d\n", op, mds);
		WARN_ON(1);
	}

	mutex_unlock(&session->s_mutex);
	if (wake) {
		mutex_lock(&mdsc->mutex);
		__wake_requests(mdsc, &session->s_waiting);
		if (wake == 2)
			kick_requests(mdsc, mds);
		mutex_unlock(&mdsc->mutex);
	}
	return;

bad:
	pr_err("mdsc_handle_session corrupt message mds%d len %d\n", mds,
	       (int)msg->front.iov_len);
	ceph_msg_dump(msg);
	return;
}


/*
 * called under session->mutex.
 */
static void replay_unsafe_requests(struct ceph_mds_client *mdsc,
				   struct ceph_mds_session *session)
{
	struct ceph_mds_request *req, *nreq;
	struct rb_node *p;
	int err;

	dout("replay_unsafe_requests mds%d\n", session->s_mds);

	mutex_lock(&mdsc->mutex);
	list_for_each_entry_safe(req, nreq, &session->s_unsafe, r_unsafe_item) {
		err = __prepare_send_request(mdsc, req, session->s_mds, true);
		if (!err) {
			ceph_msg_get(req->r_request);
			ceph_con_send(&session->s_con, req->r_request);
		}
	}

	/*
	 * also re-send old requests when MDS enters reconnect stage. So that MDS
	 * can process completed request in clientreplay stage.
	 */
	p = rb_first(&mdsc->request_tree);
	while (p) {
		req = rb_entry(p, struct ceph_mds_request, r_node);
		p = rb_next(p);
		if (req->r_got_unsafe)
			continue;
		if (req->r_attempts == 0)
			continue; /* only old requests */
		if (req->r_session &&
		    req->r_session->s_mds == session->s_mds) {
			err = __prepare_send_request(mdsc, req,
						     session->s_mds, true);
			if (!err) {
				ceph_msg_get(req->r_request);
				ceph_con_send(&session->s_con, req->r_request);
			}
		}
	}
	mutex_unlock(&mdsc->mutex);
}

/*
 * Encode information about a cap for a reconnect with the MDS.
 */
static int encode_caps_cb(struct inode *inode, struct ceph_cap *cap,
			  void *arg)
{
	union {
		struct ceph_mds_cap_reconnect v2;
		struct ceph_mds_cap_reconnect_v1 v1;
	} rec;
	struct ceph_inode_info *ci;
	struct ceph_reconnect_state *recon_state = arg;
	struct ceph_pagelist *pagelist = recon_state->pagelist;
	char *path;
	int pathlen, err;
	u64 pathbase;
	u64 snap_follows;
	struct dentry *dentry;

	ci = cap->ci;

	dout(" adding %p ino %llx.%llx cap %p %lld %s\n",
	     inode, ceph_vinop(inode), cap, cap->cap_id,
	     ceph_cap_string(cap->issued));
	err = ceph_pagelist_encode_64(pagelist, ceph_ino(inode));
	if (err)
		return err;

	dentry = d_find_alias(inode);
	if (dentry) {
		path = ceph_mdsc_build_path(dentry, &pathlen, &pathbase, 0);
		if (IS_ERR(path)) {
			err = PTR_ERR(path);
			goto out_dput;
		}
	} else {
		path = NULL;
		pathlen = 0;
		pathbase = 0;
	}

	spin_lock(&ci->i_ceph_lock);
	cap->seq = 0;        /* reset cap seq */
	cap->issue_seq = 0;  /* and issue_seq */
	cap->mseq = 0;       /* and migrate_seq */
	cap->cap_gen = cap->session->s_cap_gen;

	if (recon_state->msg_version >= 2) {
		rec.v2.cap_id = cpu_to_le64(cap->cap_id);
		rec.v2.wanted = cpu_to_le32(__ceph_caps_wanted(ci));
		rec.v2.issued = cpu_to_le32(cap->issued);
		rec.v2.snaprealm = cpu_to_le64(ci->i_snap_realm->ino);
		rec.v2.pathbase = cpu_to_le64(pathbase);
		rec.v2.flock_len = 0;
	} else {
		rec.v1.cap_id = cpu_to_le64(cap->cap_id);
		rec.v1.wanted = cpu_to_le32(__ceph_caps_wanted(ci));
		rec.v1.issued = cpu_to_le32(cap->issued);
		rec.v1.size = cpu_to_le64(inode->i_size);
		ceph_encode_timespec(&rec.v1.mtime, &inode->i_mtime);
		ceph_encode_timespec(&rec.v1.atime, &inode->i_atime);
		rec.v1.snaprealm = cpu_to_le64(ci->i_snap_realm->ino);
		rec.v1.pathbase = cpu_to_le64(pathbase);
	}

	if (list_empty(&ci->i_cap_snaps)) {
		snap_follows = 0;
	} else {
		struct ceph_cap_snap *capsnap =
			list_first_entry(&ci->i_cap_snaps,
					 struct ceph_cap_snap, ci_item);
		snap_follows = capsnap->follows;
	}
	spin_unlock(&ci->i_ceph_lock);

	if (recon_state->msg_version >= 2) {
		int num_fcntl_locks, num_flock_locks;
		struct ceph_filelock *flocks;
		size_t struct_len, total_len = 0;
		u8 struct_v = 0;

encode_again:
		ceph_count_locks(inode, &num_fcntl_locks, &num_flock_locks);
		flocks = kmalloc((num_fcntl_locks+num_flock_locks) *
				 sizeof(struct ceph_filelock), GFP_NOFS);
		if (!flocks) {
			err = -ENOMEM;
			goto out_free;
		}
		err = ceph_encode_locks_to_buffer(inode, flocks,
						  num_fcntl_locks,
						  num_flock_locks);
		if (err) {
			kfree(flocks);
			if (err == -ENOSPC)
				goto encode_again;
			goto out_free;
		}

		if (recon_state->msg_version >= 3) {
			/* version, compat_version and struct_len */
			total_len = 2 * sizeof(u8) + sizeof(u32);
			struct_v = 2;
		}
		/*
		 * number of encoded locks is stable, so copy to pagelist
		 */
		struct_len = 2 * sizeof(u32) +
			    (num_fcntl_locks + num_flock_locks) *
			    sizeof(struct ceph_filelock);
		rec.v2.flock_len = cpu_to_le32(struct_len);

		struct_len += sizeof(rec.v2);
		struct_len += sizeof(u32) + pathlen;

		if (struct_v >= 2)
			struct_len += sizeof(u64); /* snap_follows */

		total_len += struct_len;
		err = ceph_pagelist_reserve(pagelist, total_len);

		if (!err) {
			if (recon_state->msg_version >= 3) {
				ceph_pagelist_encode_8(pagelist, struct_v);
				ceph_pagelist_encode_8(pagelist, 1);
				ceph_pagelist_encode_32(pagelist, struct_len);
			}
			ceph_pagelist_encode_string(pagelist, path, pathlen);
			ceph_pagelist_append(pagelist, &rec, sizeof(rec.v2));
			ceph_locks_to_pagelist(flocks, pagelist,
					       num_fcntl_locks,
					       num_flock_locks);
			if (struct_v >= 2)
				ceph_pagelist_encode_64(pagelist, snap_follows);
		}
		kfree(flocks);
	} else {
		size_t size = sizeof(u32) + pathlen + sizeof(rec.v1);
		err = ceph_pagelist_reserve(pagelist, size);
		if (!err) {
			ceph_pagelist_encode_string(pagelist, path, pathlen);
			ceph_pagelist_append(pagelist, &rec, sizeof(rec.v1));
		}
	}

	recon_state->nr_caps++;
out_free:
	kfree(path);
out_dput:
	dput(dentry);
	return err;
}


/*
 * If an MDS fails and recovers, clients need to reconnect in order to
 * reestablish shared state.  This includes all caps issued through
 * this session _and_ the snap_realm hierarchy.  Because it's not
 * clear which snap realms the mds cares about, we send everything we
 * know about.. that ensures we'll then get any new info the
 * recovering MDS might have.
 *
 * This is a relatively heavyweight operation, but it's rare.
 *
 * called with mdsc->mutex held.
 */
static void send_mds_reconnect(struct ceph_mds_client *mdsc,
			       struct ceph_mds_session *session)
{
	struct ceph_msg *reply;
	struct rb_node *p;
	int mds = session->s_mds;
	int err = -ENOMEM;
	int s_nr_caps;
	struct ceph_pagelist *pagelist;
	struct ceph_reconnect_state recon_state;

	pr_info("mds%d reconnect start\n", mds);

	pagelist = kmalloc(sizeof(*pagelist), GFP_NOFS);
	if (!pagelist)
		goto fail_nopagelist;
	ceph_pagelist_init(pagelist);

	reply = ceph_msg_new(CEPH_MSG_CLIENT_RECONNECT, 0, GFP_NOFS, false);
	if (!reply)
		goto fail_nomsg;

	mutex_lock(&session->s_mutex);
	session->s_state = CEPH_MDS_SESSION_RECONNECTING;
	session->s_seq = 0;

	dout("session %p state %s\n", session,
	     ceph_session_state_name(session->s_state));

	spin_lock(&session->s_gen_ttl_lock);
	session->s_cap_gen++;
	spin_unlock(&session->s_gen_ttl_lock);

	spin_lock(&session->s_cap_lock);
	/* don't know if session is readonly */
	session->s_readonly = 0;
	/*
	 * notify __ceph_remove_cap() that we are composing cap reconnect.
	 * If a cap get released before being added to the cap reconnect,
	 * __ceph_remove_cap() should skip queuing cap release.
	 */
	session->s_cap_reconnect = 1;
	/* drop old cap expires; we're about to reestablish that state */
	cleanup_cap_releases(mdsc, session);

	/* trim unused caps to reduce MDS's cache rejoin time */
	if (mdsc->fsc->sb->s_root)
		shrink_dcache_parent(mdsc->fsc->sb->s_root);

	ceph_con_close(&session->s_con);
	ceph_con_open(&session->s_con,
		      CEPH_ENTITY_TYPE_MDS, mds,
		      ceph_mdsmap_get_addr(mdsc->mdsmap, mds));

	/* replay unsafe requests */
	replay_unsafe_requests(mdsc, session);

	down_read(&mdsc->snap_rwsem);

	/* traverse this session's caps */
	s_nr_caps = session->s_nr_caps;
	err = ceph_pagelist_encode_32(pagelist, s_nr_caps);
	if (err)
		goto fail;

	recon_state.nr_caps = 0;
	recon_state.pagelist = pagelist;
	if (session->s_con.peer_features & CEPH_FEATURE_MDSENC)
		recon_state.msg_version = 3;
	else if (session->s_con.peer_features & CEPH_FEATURE_FLOCK)
		recon_state.msg_version = 2;
	else
		recon_state.msg_version = 1;
	err = iterate_session_caps(session, encode_caps_cb, &recon_state);
	if (err < 0)
		goto fail;

	spin_lock(&session->s_cap_lock);
	session->s_cap_reconnect = 0;
	spin_unlock(&session->s_cap_lock);

	/*
	 * snaprealms.  we provide mds with the ino, seq (version), and
	 * parent for all of our realms.  If the mds has any newer info,
	 * it will tell us.
	 */
	for (p = rb_first(&mdsc->snap_realms); p; p = rb_next(p)) {
		struct ceph_snap_realm *realm =
			rb_entry(p, struct ceph_snap_realm, node);
		struct ceph_mds_snaprealm_reconnect sr_rec;

		dout(" adding snap realm %llx seq %lld parent %llx\n",
		     realm->ino, realm->seq, realm->parent_ino);
		sr_rec.ino = cpu_to_le64(realm->ino);
		sr_rec.seq = cpu_to_le64(realm->seq);
		sr_rec.parent = cpu_to_le64(realm->parent_ino);
		err = ceph_pagelist_append(pagelist, &sr_rec, sizeof(sr_rec));
		if (err)
			goto fail;
	}

	reply->hdr.version = cpu_to_le16(recon_state.msg_version);

	/* raced with cap release? */
	if (s_nr_caps != recon_state.nr_caps) {
		struct page *page = list_first_entry(&pagelist->head,
						     struct page, lru);
		__le32 *addr = kmap_atomic(page);
		*addr = cpu_to_le32(recon_state.nr_caps);
		kunmap_atomic(addr);
	}

	reply->hdr.data_len = cpu_to_le32(pagelist->length);
	ceph_msg_data_add_pagelist(reply, pagelist);

	ceph_early_kick_flushing_caps(mdsc, session);

	ceph_con_send(&session->s_con, reply);

	mutex_unlock(&session->s_mutex);

	mutex_lock(&mdsc->mutex);
	__wake_requests(mdsc, &session->s_waiting);
	mutex_unlock(&mdsc->mutex);

	up_read(&mdsc->snap_rwsem);
	return;

fail:
	ceph_msg_put(reply);
	up_read(&mdsc->snap_rwsem);
	mutex_unlock(&session->s_mutex);
fail_nomsg:
	ceph_pagelist_release(pagelist);
fail_nopagelist:
	pr_err("error %d preparing reconnect for mds%d\n", err, mds);
	return;
}


/*
 * compare old and new mdsmaps, kicking requests
 * and closing out old connections as necessary
 *
 * called under mdsc->mutex.
 */
static void check_new_map(struct ceph_mds_client *mdsc,
			  struct ceph_mdsmap *newmap,
			  struct ceph_mdsmap *oldmap)
{
	int i;
	int oldstate, newstate;
	struct ceph_mds_session *s;

	dout("check_new_map new %u old %u\n",
	     newmap->m_epoch, oldmap->m_epoch);

	for (i = 0; i < oldmap->m_max_mds && i < mdsc->max_sessions; i++) {
		if (mdsc->sessions[i] == NULL)
			continue;
		s = mdsc->sessions[i];
		oldstate = ceph_mdsmap_get_state(oldmap, i);
		newstate = ceph_mdsmap_get_state(newmap, i);

		dout("check_new_map mds%d state %s%s -> %s%s (session %s)\n",
		     i, ceph_mds_state_name(oldstate),
		     ceph_mdsmap_is_laggy(oldmap, i) ? " (laggy)" : "",
		     ceph_mds_state_name(newstate),
		     ceph_mdsmap_is_laggy(newmap, i) ? " (laggy)" : "",
		     ceph_session_state_name(s->s_state));

		if (i >= newmap->m_max_mds ||
		    memcmp(ceph_mdsmap_get_addr(oldmap, i),
			   ceph_mdsmap_get_addr(newmap, i),
			   sizeof(struct ceph_entity_addr))) {
			if (s->s_state == CEPH_MDS_SESSION_OPENING) {
				/* the session never opened, just close it
				 * out now */
				__wake_requests(mdsc, &s->s_waiting);
				__unregister_session(mdsc, s);
			} else {
				/* just close it */
				mutex_unlock(&mdsc->mutex);
				mutex_lock(&s->s_mutex);
				mutex_lock(&mdsc->mutex);
				ceph_con_close(&s->s_con);
				mutex_unlock(&s->s_mutex);
				s->s_state = CEPH_MDS_SESSION_RESTARTING;
			}
		} else if (oldstate == newstate) {
			continue;  /* nothing new with this mds */
		}

		/*
		 * send reconnect?
		 */
		if (s->s_state == CEPH_MDS_SESSION_RESTARTING &&
		    newstate >= CEPH_MDS_STATE_RECONNECT) {
			mutex_unlock(&mdsc->mutex);
			send_mds_reconnect(mdsc, s);
			mutex_lock(&mdsc->mutex);
		}

		/*
		 * kick request on any mds that has gone active.
		 */
		if (oldstate < CEPH_MDS_STATE_ACTIVE &&
		    newstate >= CEPH_MDS_STATE_ACTIVE) {
			if (oldstate != CEPH_MDS_STATE_CREATING &&
			    oldstate != CEPH_MDS_STATE_STARTING)
				pr_info("mds%d recovery completed\n", s->s_mds);
			kick_requests(mdsc, i);
			ceph_kick_flushing_caps(mdsc, s);
			wake_up_session_caps(s, 1);
		}
	}

	for (i = 0; i < newmap->m_max_mds && i < mdsc->max_sessions; i++) {
		s = mdsc->sessions[i];
		if (!s)
			continue;
		if (!ceph_mdsmap_is_laggy(newmap, i))
			continue;
		if (s->s_state == CEPH_MDS_SESSION_OPEN ||
		    s->s_state == CEPH_MDS_SESSION_HUNG ||
		    s->s_state == CEPH_MDS_SESSION_CLOSING) {
			dout(" connecting to export targets of laggy mds%d\n",
			     i);
			__open_export_target_sessions(mdsc, s);
		}
	}
}



/*
 * leases
 */

/*
 * caller must hold session s_mutex, dentry->d_lock
 */
void __ceph_mdsc_drop_dentry_lease(struct dentry *dentry)
{
	struct ceph_dentry_info *di = ceph_dentry(dentry);

	ceph_put_mds_session(di->lease_session);
	di->lease_session = NULL;
}

static void handle_lease(struct ceph_mds_client *mdsc,
			 struct ceph_mds_session *session,
			 struct ceph_msg *msg)
{
	struct super_block *sb = mdsc->fsc->sb;
	struct inode *inode;
	struct dentry *parent, *dentry;
	struct ceph_dentry_info *di;
	int mds = session->s_mds;
	struct ceph_mds_lease *h = msg->front.iov_base;
	u32 seq;
	struct ceph_vino vino;
	struct qstr dname;
	int release = 0;

	dout("handle_lease from mds%d\n", mds);

	/* decode */
	if (msg->front.iov_len < sizeof(*h) + sizeof(u32))
		goto bad;
	vino.ino = le64_to_cpu(h->ino);
	vino.snap = CEPH_NOSNAP;
	seq = le32_to_cpu(h->seq);
	dname.name = (void *)h + sizeof(*h) + sizeof(u32);
	dname.len = msg->front.iov_len - sizeof(*h) - sizeof(u32);
	if (dname.len != get_unaligned_le32(h+1))
		goto bad;

	/* lookup inode */
	inode = ceph_find_inode(sb, vino);
	dout("handle_lease %s, ino %llx %p %.*s\n",
	     ceph_lease_op_name(h->action), vino.ino, inode,
	     dname.len, dname.name);

	mutex_lock(&session->s_mutex);
	session->s_seq++;

	if (inode == NULL) {
		dout("handle_lease no inode %llx\n", vino.ino);
		goto release;
	}

	/* dentry */
	parent = d_find_alias(inode);
	if (!parent) {
		dout("no parent dentry on inode %p\n", inode);
		WARN_ON(1);
		goto release;  /* hrm... */
	}
	dname.hash = full_name_hash(parent, dname.name, dname.len);
	dentry = d_lookup(parent, &dname);
	dput(parent);
	if (!dentry)
		goto release;

	spin_lock(&dentry->d_lock);
	di = ceph_dentry(dentry);
	switch (h->action) {
	case CEPH_MDS_LEASE_REVOKE:
		if (di->lease_session == session) {
			if (ceph_seq_cmp(di->lease_seq, seq) > 0)
				h->seq = cpu_to_le32(di->lease_seq);
			__ceph_mdsc_drop_dentry_lease(dentry);
		}
		release = 1;
		break;

	case CEPH_MDS_LEASE_RENEW:
		if (di->lease_session == session &&
		    di->lease_gen == session->s_cap_gen &&
		    di->lease_renew_from &&
		    di->lease_renew_after == 0) {
			unsigned long duration =
				msecs_to_jiffies(le32_to_cpu(h->duration_ms));

			di->lease_seq = seq;
			di->time = di->lease_renew_from + duration;
			di->lease_renew_after = di->lease_renew_from +
				(duration >> 1);
			di->lease_renew_from = 0;
		}
		break;
	}
	spin_unlock(&dentry->d_lock);
	dput(dentry);

	if (!release)
		goto out;

release:
	/* let's just reuse the same message */
	h->action = CEPH_MDS_LEASE_REVOKE_ACK;
	ceph_msg_get(msg);
	ceph_con_send(&session->s_con, msg);

out:
	iput(inode);
	mutex_unlock(&session->s_mutex);
	return;

bad:
	pr_err("corrupt lease message\n");
	ceph_msg_dump(msg);
}

void ceph_mdsc_lease_send_msg(struct ceph_mds_session *session,
			      struct inode *inode,
			      struct dentry *dentry, char action,
			      u32 seq)
{
	struct ceph_msg *msg;
	struct ceph_mds_lease *lease;
	int len = sizeof(*lease) + sizeof(u32);
	int dnamelen = 0;

	dout("lease_send_msg inode %p dentry %p %s to mds%d\n",
	     inode, dentry, ceph_lease_op_name(action), session->s_mds);
	dnamelen = dentry->d_name.len;
	len += dnamelen;

	msg = ceph_msg_new(CEPH_MSG_CLIENT_LEASE, len, GFP_NOFS, false);
	if (!msg)
		return;
	lease = msg->front.iov_base;
	lease->action = action;
	lease->ino = cpu_to_le64(ceph_vino(inode).ino);
	lease->first = lease->last = cpu_to_le64(ceph_vino(inode).snap);
	lease->seq = cpu_to_le32(seq);
	put_unaligned_le32(dnamelen, lease + 1);
	memcpy((void *)(lease + 1) + 4, dentry->d_name.name, dnamelen);

	/*
	 * if this is a preemptive lease RELEASE, no need to
	 * flush request stream, since the actual request will
	 * soon follow.
	 */
	msg->more_to_follow = (action == CEPH_MDS_LEASE_RELEASE);

	ceph_con_send(&session->s_con, msg);
}

/*
 * drop all leases (and dentry refs) in preparation for umount
 */
static void drop_leases(struct ceph_mds_client *mdsc)
{
	int i;

	dout("drop_leases\n");
	mutex_lock(&mdsc->mutex);
	for (i = 0; i < mdsc->max_sessions; i++) {
		struct ceph_mds_session *s = __ceph_lookup_mds_session(mdsc, i);
		if (!s)
			continue;
		mutex_unlock(&mdsc->mutex);
		mutex_lock(&s->s_mutex);
		mutex_unlock(&s->s_mutex);
		ceph_put_mds_session(s);
		mutex_lock(&mdsc->mutex);
	}
	mutex_unlock(&mdsc->mutex);
}



/*
 * delayed work -- periodically trim expired leases, renew caps with mds
 */
static void schedule_delayed(struct ceph_mds_client *mdsc)
{
	int delay = 5;
	unsigned hz = round_jiffies_relative(HZ * delay);
	schedule_delayed_work(&mdsc->delayed_work, hz);
}

static void delayed_work(struct work_struct *work)
{
	int i;
	struct ceph_mds_client *mdsc =
		container_of(work, struct ceph_mds_client, delayed_work.work);
	int renew_interval;
	int renew_caps;

	dout("mdsc delayed_work\n");
	ceph_check_delayed_caps(mdsc);

	mutex_lock(&mdsc->mutex);
	renew_interval = mdsc->mdsmap->m_session_timeout >> 2;
	renew_caps = time_after_eq(jiffies, HZ*renew_interval +
				   mdsc->last_renew_caps);
	if (renew_caps)
		mdsc->last_renew_caps = jiffies;

	for (i = 0; i < mdsc->max_sessions; i++) {
		struct ceph_mds_session *s = __ceph_lookup_mds_session(mdsc, i);
		if (s == NULL)
			continue;
		if (s->s_state == CEPH_MDS_SESSION_CLOSING) {
			dout("resending session close request for mds%d\n",
			     s->s_mds);
			request_close_session(mdsc, s);
			ceph_put_mds_session(s);
			continue;
		}
		if (s->s_ttl && time_after(jiffies, s->s_ttl)) {
			if (s->s_state == CEPH_MDS_SESSION_OPEN) {
				s->s_state = CEPH_MDS_SESSION_HUNG;
				pr_info("mds%d hung\n", s->s_mds);
			}
		}
		if (s->s_state < CEPH_MDS_SESSION_OPEN) {
			/* this mds is failed or recovering, just wait */
			ceph_put_mds_session(s);
			continue;
		}
		mutex_unlock(&mdsc->mutex);

		mutex_lock(&s->s_mutex);
		if (renew_caps)
			send_renew_caps(mdsc, s);
		else
			ceph_con_keepalive(&s->s_con);
		if (s->s_state == CEPH_MDS_SESSION_OPEN ||
		    s->s_state == CEPH_MDS_SESSION_HUNG)
			ceph_send_cap_releases(mdsc, s);
		mutex_unlock(&s->s_mutex);
		ceph_put_mds_session(s);

		mutex_lock(&mdsc->mutex);
	}
	mutex_unlock(&mdsc->mutex);

	schedule_delayed(mdsc);
}

int ceph_mdsc_init(struct ceph_fs_client *fsc)

{
	struct ceph_mds_client *mdsc;

	mdsc = kzalloc(sizeof(struct ceph_mds_client), GFP_NOFS);
	if (!mdsc)
		return -ENOMEM;
	mdsc->fsc = fsc;
	fsc->mdsc = mdsc;
	mutex_init(&mdsc->mutex);
	mdsc->mdsmap = kzalloc(sizeof(*mdsc->mdsmap), GFP_NOFS);
	if (mdsc->mdsmap == NULL) {
		kfree(mdsc);
		return -ENOMEM;
	}

	init_completion(&mdsc->safe_umount_waiters);
	init_waitqueue_head(&mdsc->session_close_wq);
	INIT_LIST_HEAD(&mdsc->waiting_for_map);
	mdsc->sessions = NULL;
	atomic_set(&mdsc->num_sessions, 0);
	mdsc->max_sessions = 0;
	mdsc->stopping = 0;
	mdsc->last_snap_seq = 0;
	init_rwsem(&mdsc->snap_rwsem);
	mdsc->snap_realms = RB_ROOT;
	INIT_LIST_HEAD(&mdsc->snap_empty);
	spin_lock_init(&mdsc->snap_empty_lock);
	mdsc->last_tid = 0;
	mdsc->oldest_tid = 0;
	mdsc->request_tree = RB_ROOT;
	INIT_DELAYED_WORK(&mdsc->delayed_work, delayed_work);
	mdsc->last_renew_caps = jiffies;
	INIT_LIST_HEAD(&mdsc->cap_delay_list);
	spin_lock_init(&mdsc->cap_delay_lock);
	INIT_LIST_HEAD(&mdsc->snap_flush_list);
	spin_lock_init(&mdsc->snap_flush_lock);
	mdsc->last_cap_flush_tid = 1;
	INIT_LIST_HEAD(&mdsc->cap_flush_list);
	INIT_LIST_HEAD(&mdsc->cap_dirty);
	INIT_LIST_HEAD(&mdsc->cap_dirty_migrating);
	mdsc->num_cap_flushing = 0;
	spin_lock_init(&mdsc->cap_dirty_lock);
	init_waitqueue_head(&mdsc->cap_flushing_wq);
	spin_lock_init(&mdsc->dentry_lru_lock);
	INIT_LIST_HEAD(&mdsc->dentry_lru);

	ceph_caps_init(mdsc);
	ceph_adjust_min_caps(mdsc, fsc->min_caps);

	init_rwsem(&mdsc->pool_perm_rwsem);
	mdsc->pool_perm_tree = RB_ROOT;

	return 0;
}

/*
 * Wait for safe replies on open mds requests.  If we time out, drop
 * all requests from the tree to avoid dangling dentry refs.
 */
static void wait_requests(struct ceph_mds_client *mdsc)
{
	struct ceph_options *opts = mdsc->fsc->client->options;
	struct ceph_mds_request *req;

	mutex_lock(&mdsc->mutex);
	if (__get_oldest_req(mdsc)) {
		mutex_unlock(&mdsc->mutex);

		dout("wait_requests waiting for requests\n");
		wait_for_completion_timeout(&mdsc->safe_umount_waiters,
				    ceph_timeout_jiffies(opts->mount_timeout));

		/* tear down remaining requests */
		mutex_lock(&mdsc->mutex);
		while ((req = __get_oldest_req(mdsc))) {
			dout("wait_requests timed out on tid %llu\n",
			     req->r_tid);
			__unregister_request(mdsc, req);
		}
	}
	mutex_unlock(&mdsc->mutex);
	dout("wait_requests done\n");
}

/*
 * called before mount is ro, and before dentries are torn down.
 * (hmm, does this still race with new lookups?)
 */
void ceph_mdsc_pre_umount(struct ceph_mds_client *mdsc)
{
	dout("pre_umount\n");
	mdsc->stopping = 1;

	drop_leases(mdsc);
	ceph_flush_dirty_caps(mdsc);
	wait_requests(mdsc);

	/*
	 * wait for reply handlers to drop their request refs and
	 * their inode/dcache refs
	 */
	ceph_msgr_flush();
}

/*
 * wait for all write mds requests to flush.
 */
static void wait_unsafe_requests(struct ceph_mds_client *mdsc, u64 want_tid)
{
	struct ceph_mds_request *req = NULL, *nextreq;
	struct rb_node *n;

	mutex_lock(&mdsc->mutex);
	dout("wait_unsafe_requests want %lld\n", want_tid);
restart:
	req = __get_oldest_req(mdsc);
	while (req && req->r_tid <= want_tid) {
		/* find next request */
		n = rb_next(&req->r_node);
		if (n)
			nextreq = rb_entry(n, struct ceph_mds_request, r_node);
		else
			nextreq = NULL;
		if (req->r_op != CEPH_MDS_OP_SETFILELOCK &&
		    (req->r_op & CEPH_MDS_OP_WRITE)) {
			/* write op */
			ceph_mdsc_get_request(req);
			if (nextreq)
				ceph_mdsc_get_request(nextreq);
			mutex_unlock(&mdsc->mutex);
			dout("wait_unsafe_requests  wait on %llu (want %llu)\n",
			     req->r_tid, want_tid);
			wait_for_completion(&req->r_safe_completion);
			mutex_lock(&mdsc->mutex);
			ceph_mdsc_put_request(req);
			if (!nextreq)
				break;  /* next dne before, so we're done! */
			if (RB_EMPTY_NODE(&nextreq->r_node)) {
				/* next request was removed from tree */
				ceph_mdsc_put_request(nextreq);
				goto restart;
			}
			ceph_mdsc_put_request(nextreq);  /* won't go away */
		}
		req = nextreq;
	}
	mutex_unlock(&mdsc->mutex);
	dout("wait_unsafe_requests done\n");
}

void ceph_mdsc_sync(struct ceph_mds_client *mdsc)
{
	u64 want_tid, want_flush;

	if (ACCESS_ONCE(mdsc->fsc->mount_state) == CEPH_MOUNT_SHUTDOWN)
		return;

	dout("sync\n");
	mutex_lock(&mdsc->mutex);
	want_tid = mdsc->last_tid;
	mutex_unlock(&mdsc->mutex);

	ceph_flush_dirty_caps(mdsc);
	spin_lock(&mdsc->cap_dirty_lock);
	want_flush = mdsc->last_cap_flush_tid;
	if (!list_empty(&mdsc->cap_flush_list)) {
		struct ceph_cap_flush *cf =
			list_last_entry(&mdsc->cap_flush_list,
					struct ceph_cap_flush, g_list);
		cf->wake = true;
	}
	spin_unlock(&mdsc->cap_dirty_lock);

	dout("sync want tid %lld flush_seq %lld\n",
	     want_tid, want_flush);

	wait_unsafe_requests(mdsc, want_tid);
	wait_caps_flush(mdsc, want_flush);
}

/*
 * true if all sessions are closed, or we force unmount
 */
static bool done_closing_sessions(struct ceph_mds_client *mdsc, int skipped)
{
	if (ACCESS_ONCE(mdsc->fsc->mount_state) == CEPH_MOUNT_SHUTDOWN)
		return true;
	return atomic_read(&mdsc->num_sessions) <= skipped;
}

/*
 * called after sb is ro.
 */
void ceph_mdsc_close_sessions(struct ceph_mds_client *mdsc)
{
	struct ceph_options *opts = mdsc->fsc->client->options;
	struct ceph_mds_session *session;
	int i;
	int skipped = 0;

	dout("close_sessions\n");

	/* close sessions */
	mutex_lock(&mdsc->mutex);
	for (i = 0; i < mdsc->max_sessions; i++) {
		session = __ceph_lookup_mds_session(mdsc, i);
		if (!session)
			continue;
		mutex_unlock(&mdsc->mutex);
		mutex_lock(&session->s_mutex);
		if (__close_session(mdsc, session) <= 0)
			skipped++;
		mutex_unlock(&session->s_mutex);
		ceph_put_mds_session(session);
		mutex_lock(&mdsc->mutex);
	}
	mutex_unlock(&mdsc->mutex);

	dout("waiting for sessions to close\n");
	wait_event_timeout(mdsc->session_close_wq,
			   done_closing_sessions(mdsc, skipped),
			   ceph_timeout_jiffies(opts->mount_timeout));

	/* tear down remaining sessions */
	mutex_lock(&mdsc->mutex);
	for (i = 0; i < mdsc->max_sessions; i++) {
		if (mdsc->sessions[i]) {
			session = get_session(mdsc->sessions[i]);
			__unregister_session(mdsc, session);
			mutex_unlock(&mdsc->mutex);
			mutex_lock(&session->s_mutex);
			remove_session_caps(session);
			mutex_unlock(&session->s_mutex);
			ceph_put_mds_session(session);
			mutex_lock(&mdsc->mutex);
		}
	}
	WARN_ON(!list_empty(&mdsc->cap_delay_list));
	mutex_unlock(&mdsc->mutex);

	ceph_cleanup_empty_realms(mdsc);

	cancel_delayed_work_sync(&mdsc->delayed_work); /* cancel timer */

	dout("stopped\n");
}

void ceph_mdsc_force_umount(struct ceph_mds_client *mdsc)
{
	struct ceph_mds_session *session;
	int mds;

	dout("force umount\n");

	mutex_lock(&mdsc->mutex);
	for (mds = 0; mds < mdsc->max_sessions; mds++) {
		session = __ceph_lookup_mds_session(mdsc, mds);
		if (!session)
			continue;
		mutex_unlock(&mdsc->mutex);
		mutex_lock(&session->s_mutex);
		__close_session(mdsc, session);
		if (session->s_state == CEPH_MDS_SESSION_CLOSING) {
			cleanup_session_requests(mdsc, session);
			remove_session_caps(session);
		}
		mutex_unlock(&session->s_mutex);
		ceph_put_mds_session(session);
		mutex_lock(&mdsc->mutex);
		kick_requests(mdsc, mds);
	}
	__wake_requests(mdsc, &mdsc->waiting_for_map);
	mutex_unlock(&mdsc->mutex);
}

static void ceph_mdsc_stop(struct ceph_mds_client *mdsc)
{
	dout("stop\n");
	cancel_delayed_work_sync(&mdsc->delayed_work); /* cancel timer */
	if (mdsc->mdsmap)
		ceph_mdsmap_destroy(mdsc->mdsmap);
	kfree(mdsc->sessions);
	ceph_caps_finalize(mdsc);
	ceph_pool_perm_destroy(mdsc);
}

void ceph_mdsc_destroy(struct ceph_fs_client *fsc)
{
	struct ceph_mds_client *mdsc = fsc->mdsc;

	dout("mdsc_destroy %p\n", mdsc);
	ceph_mdsc_stop(mdsc);

	/* flush out any connection work with references to us */
	ceph_msgr_flush();

	fsc->mdsc = NULL;
	kfree(mdsc);
	dout("mdsc_destroy %p done\n", mdsc);
}

void ceph_mdsc_handle_fsmap(struct ceph_mds_client *mdsc, struct ceph_msg *msg)
{
	struct ceph_fs_client *fsc = mdsc->fsc;
	const char *mds_namespace = fsc->mount_options->mds_namespace;
	void *p = msg->front.iov_base;
	void *end = p + msg->front.iov_len;
	u32 epoch;
	u32 map_len;
	u32 num_fs;
	u32 mount_fscid = (u32)-1;
	u8 struct_v, struct_cv;
	int err = -EINVAL;

	ceph_decode_need(&p, end, sizeof(u32), bad);
	epoch = ceph_decode_32(&p);

	dout("handle_fsmap epoch %u\n", epoch);

	ceph_decode_need(&p, end, 2 + sizeof(u32), bad);
	struct_v = ceph_decode_8(&p);
	struct_cv = ceph_decode_8(&p);
	map_len = ceph_decode_32(&p);

	ceph_decode_need(&p, end, sizeof(u32) * 3, bad);
	p += sizeof(u32) * 2; /* skip epoch and legacy_client_fscid */

	num_fs = ceph_decode_32(&p);
	while (num_fs-- > 0) {
		void *info_p, *info_end;
		u32 info_len;
		u8 info_v, info_cv;
		u32 fscid, namelen;

		ceph_decode_need(&p, end, 2 + sizeof(u32), bad);
		info_v = ceph_decode_8(&p);
		info_cv = ceph_decode_8(&p);
		info_len = ceph_decode_32(&p);
		ceph_decode_need(&p, end, info_len, bad);
		info_p = p;
		info_end = p + info_len;
		p = info_end;

		ceph_decode_need(&info_p, info_end, sizeof(u32) * 2, bad);
		fscid = ceph_decode_32(&info_p);
		namelen = ceph_decode_32(&info_p);
		ceph_decode_need(&info_p, info_end, namelen, bad);

		if (mds_namespace &&
		    strlen(mds_namespace) == namelen &&
		    !strncmp(mds_namespace, (char *)info_p, namelen)) {
			mount_fscid = fscid;
			break;
		}
	}

	ceph_monc_got_map(&fsc->client->monc, CEPH_SUB_FSMAP, epoch);
	if (mount_fscid != (u32)-1) {
		fsc->client->monc.fs_cluster_id = mount_fscid;
		ceph_monc_want_map(&fsc->client->monc, CEPH_SUB_MDSMAP,
				   0, true);
		ceph_monc_renew_subs(&fsc->client->monc);
	} else {
		err = -ENOENT;
		goto err_out;
	}
	return;
bad:
	pr_err("error decoding fsmap\n");
err_out:
	mutex_lock(&mdsc->mutex);
	mdsc->mdsmap_err = -ENOENT;
	__wake_requests(mdsc, &mdsc->waiting_for_map);
	mutex_unlock(&mdsc->mutex);
	return;
}

/*
 * handle mds map update.
 */
void ceph_mdsc_handle_mdsmap(struct ceph_mds_client *mdsc, struct ceph_msg *msg)
{
	u32 epoch;
	u32 maplen;
	void *p = msg->front.iov_base;
	void *end = p + msg->front.iov_len;
	struct ceph_mdsmap *newmap, *oldmap;
	struct ceph_fsid fsid;
	int err = -EINVAL;

	ceph_decode_need(&p, end, sizeof(fsid)+2*sizeof(u32), bad);
	ceph_decode_copy(&p, &fsid, sizeof(fsid));
	if (ceph_check_fsid(mdsc->fsc->client, &fsid) < 0)
		return;
	epoch = ceph_decode_32(&p);
	maplen = ceph_decode_32(&p);
	dout("handle_map epoch %u len %d\n", epoch, (int)maplen);

	/* do we need it? */
	mutex_lock(&mdsc->mutex);
	if (mdsc->mdsmap && epoch <= mdsc->mdsmap->m_epoch) {
		dout("handle_map epoch %u <= our %u\n",
		     epoch, mdsc->mdsmap->m_epoch);
		mutex_unlock(&mdsc->mutex);
		return;
	}

	newmap = ceph_mdsmap_decode(&p, end);
	if (IS_ERR(newmap)) {
		err = PTR_ERR(newmap);
		goto bad_unlock;
	}

	/* swap into place */
	if (mdsc->mdsmap) {
		oldmap = mdsc->mdsmap;
		mdsc->mdsmap = newmap;
		check_new_map(mdsc, newmap, oldmap);
		ceph_mdsmap_destroy(oldmap);
	} else {
		mdsc->mdsmap = newmap;  /* first mds map */
	}
	mdsc->fsc->sb->s_maxbytes = mdsc->mdsmap->m_max_file_size;

	__wake_requests(mdsc, &mdsc->waiting_for_map);
	ceph_monc_got_map(&mdsc->fsc->client->monc, CEPH_SUB_MDSMAP,
			  mdsc->mdsmap->m_epoch);

	mutex_unlock(&mdsc->mutex);
	schedule_delayed(mdsc);
	return;

bad_unlock:
	mutex_unlock(&mdsc->mutex);
bad:
	pr_err("error decoding mdsmap %d\n", err);
	return;
}

static struct ceph_connection *con_get(struct ceph_connection *con)
{
	struct ceph_mds_session *s = con->private;

	if (get_session(s)) {
		dout("mdsc con_get %p ok (%d)\n", s, atomic_read(&s->s_ref));
		return con;
	}
	dout("mdsc con_get %p FAIL\n", s);
	return NULL;
}

static void con_put(struct ceph_connection *con)
{
	struct ceph_mds_session *s = con->private;

	dout("mdsc con_put %p (%d)\n", s, atomic_read(&s->s_ref) - 1);
	ceph_put_mds_session(s);
}

/*
 * if the client is unresponsive for long enough, the mds will kill
 * the session entirely.
 */
static void peer_reset(struct ceph_connection *con)
{
	struct ceph_mds_session *s = con->private;
	struct ceph_mds_client *mdsc = s->s_mdsc;

	pr_warn("mds%d closed our session\n", s->s_mds);
	send_mds_reconnect(mdsc, s);
}

static void dispatch(struct ceph_connection *con, struct ceph_msg *msg)
{
	struct ceph_mds_session *s = con->private;
	struct ceph_mds_client *mdsc = s->s_mdsc;
	int type = le16_to_cpu(msg->hdr.type);

	mutex_lock(&mdsc->mutex);
	if (__verify_registered_session(mdsc, s) < 0) {
		mutex_unlock(&mdsc->mutex);
		goto out;
	}
	mutex_unlock(&mdsc->mutex);

	switch (type) {
	case CEPH_MSG_MDS_MAP:
		ceph_mdsc_handle_mdsmap(mdsc, msg);
		break;
	case CEPH_MSG_FS_MAP_USER:
		ceph_mdsc_handle_fsmap(mdsc, msg);
		break;
	case CEPH_MSG_CLIENT_SESSION:
		handle_session(s, msg);
		break;
	case CEPH_MSG_CLIENT_REPLY:
		handle_reply(s, msg);
		break;
	case CEPH_MSG_CLIENT_REQUEST_FORWARD:
		handle_forward(mdsc, s, msg);
		break;
	case CEPH_MSG_CLIENT_CAPS:
		ceph_handle_caps(s, msg);
		break;
	case CEPH_MSG_CLIENT_SNAP:
		ceph_handle_snap(mdsc, s, msg);
		break;
	case CEPH_MSG_CLIENT_LEASE:
		handle_lease(mdsc, s, msg);
		break;

	default:
		pr_err("received unknown message type %d %s\n", type,
		       ceph_msg_type_name(type));
	}
out:
	ceph_msg_put(msg);
}

/*
 * authentication
 */

/*
 * Note: returned pointer is the address of a structure that's
 * managed separately.  Caller must *not* attempt to free it.
 */
static struct ceph_auth_handshake *get_authorizer(struct ceph_connection *con,
					int *proto, int force_new)
{
	struct ceph_mds_session *s = con->private;
	struct ceph_mds_client *mdsc = s->s_mdsc;
	struct ceph_auth_client *ac = mdsc->fsc->client->monc.auth;
	struct ceph_auth_handshake *auth = &s->s_auth;

	if (force_new && auth->authorizer) {
		ceph_auth_destroy_authorizer(auth->authorizer);
		auth->authorizer = NULL;
	}
	if (!auth->authorizer) {
		int ret = ceph_auth_create_authorizer(ac, CEPH_ENTITY_TYPE_MDS,
						      auth);
		if (ret)
			return ERR_PTR(ret);
	} else {
		int ret = ceph_auth_update_authorizer(ac, CEPH_ENTITY_TYPE_MDS,
						      auth);
		if (ret)
			return ERR_PTR(ret);
	}
	*proto = ac->protocol;

	return auth;
}


static int verify_authorizer_reply(struct ceph_connection *con)
{
	struct ceph_mds_session *s = con->private;
	struct ceph_mds_client *mdsc = s->s_mdsc;
	struct ceph_auth_client *ac = mdsc->fsc->client->monc.auth;

	return ceph_auth_verify_authorizer_reply(ac, s->s_auth.authorizer);
}

static int invalidate_authorizer(struct ceph_connection *con)
{
	struct ceph_mds_session *s = con->private;
	struct ceph_mds_client *mdsc = s->s_mdsc;
	struct ceph_auth_client *ac = mdsc->fsc->client->monc.auth;

	ceph_auth_invalidate_authorizer(ac, CEPH_ENTITY_TYPE_MDS);

	return ceph_monc_validate_auth(&mdsc->fsc->client->monc);
}

static struct ceph_msg *mds_alloc_msg(struct ceph_connection *con,
				struct ceph_msg_header *hdr, int *skip)
{
	struct ceph_msg *msg;
	int type = (int) le16_to_cpu(hdr->type);
	int front_len = (int) le32_to_cpu(hdr->front_len);

	if (con->in_msg)
		return con->in_msg;

	*skip = 0;
	msg = ceph_msg_new(type, front_len, GFP_NOFS, false);
	if (!msg) {
		pr_err("unable to allocate msg type %d len %d\n",
		       type, front_len);
		return NULL;
	}

	return msg;
}

static int mds_sign_message(struct ceph_msg *msg)
{
       struct ceph_mds_session *s = msg->con->private;
       struct ceph_auth_handshake *auth = &s->s_auth;

       return ceph_auth_sign_message(auth, msg);
}

static int mds_check_message_signature(struct ceph_msg *msg)
{
       struct ceph_mds_session *s = msg->con->private;
       struct ceph_auth_handshake *auth = &s->s_auth;

       return ceph_auth_check_message_signature(auth, msg);
}

static const struct ceph_connection_operations mds_con_ops = {
	.get = con_get,
	.put = con_put,
	.dispatch = dispatch,
	.get_authorizer = get_authorizer,
	.verify_authorizer_reply = verify_authorizer_reply,
	.invalidate_authorizer = invalidate_authorizer,
	.peer_reset = peer_reset,
	.alloc_msg = mds_alloc_msg,
	.sign_message = mds_sign_message,
	.check_message_signature = mds_check_message_signature,
};

/* eof */
