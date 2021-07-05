// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>

#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/ratelimit.h>
#include <linux/bits.h>
#include <linux/ktime.h>

#include "super.h"
#include "mds_client.h"

#include <linux/ceph/ceph_features.h>
#include <linux/ceph/messenger.h>
#include <linux/ceph/decode.h>
#include <linux/ceph/pagelist.h>
#include <linux/ceph/auth.h>
#include <linux/ceph/debugfs.h>

#define RECONNECT_MAX_SIZE (INT_MAX - PAGE_SIZE)

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
	struct ceph_mds_session *session;
	int nr_caps, nr_realms;
	struct ceph_pagelist *pagelist;
	unsigned msg_version;
	bool allow_multi;
};

static void __wake_requests(struct ceph_mds_client *mdsc,
			    struct list_head *head);
static void ceph_cap_release_work(struct work_struct *work);
static void ceph_cap_reclaim_work(struct work_struct *work);

static const struct ceph_connection_operations mds_con_ops;


/*
 * mds reply parsing
 */

static int parse_reply_info_quota(void **p, void *end,
				  struct ceph_mds_reply_info_in *info)
{
	u8 struct_v, struct_compat;
	u32 struct_len;

	ceph_decode_8_safe(p, end, struct_v, bad);
	ceph_decode_8_safe(p, end, struct_compat, bad);
	/* struct_v is expected to be >= 1. we only
	 * understand encoding with struct_compat == 1. */
	if (!struct_v || struct_compat != 1)
		goto bad;
	ceph_decode_32_safe(p, end, struct_len, bad);
	ceph_decode_need(p, end, struct_len, bad);
	end = *p + struct_len;
	ceph_decode_64_safe(p, end, info->max_bytes, bad);
	ceph_decode_64_safe(p, end, info->max_files, bad);
	*p = end;
	return 0;
bad:
	return -EIO;
}

/*
 * parse individual inode info
 */
static int parse_reply_info_in(void **p, void *end,
			       struct ceph_mds_reply_info_in *info,
			       u64 features)
{
	int err = 0;
	u8 struct_v = 0;

	if (features == (u64)-1) {
		u32 struct_len;
		u8 struct_compat;
		ceph_decode_8_safe(p, end, struct_v, bad);
		ceph_decode_8_safe(p, end, struct_compat, bad);
		/* struct_v is expected to be >= 1. we only understand
		 * encoding with struct_compat == 1. */
		if (!struct_v || struct_compat != 1)
			goto bad;
		ceph_decode_32_safe(p, end, struct_len, bad);
		ceph_decode_need(p, end, struct_len, bad);
		end = *p + struct_len;
	}

	ceph_decode_need(p, end, sizeof(struct ceph_mds_reply_inode), bad);
	info->in = *p;
	*p += sizeof(struct ceph_mds_reply_inode) +
		sizeof(*info->in->fragtree.splits) *
		le32_to_cpu(info->in->fragtree.nsplits);

	ceph_decode_32_safe(p, end, info->symlink_len, bad);
	ceph_decode_need(p, end, info->symlink_len, bad);
	info->symlink = *p;
	*p += info->symlink_len;

	ceph_decode_copy_safe(p, end, &info->dir_layout,
			      sizeof(info->dir_layout), bad);
	ceph_decode_32_safe(p, end, info->xattr_len, bad);
	ceph_decode_need(p, end, info->xattr_len, bad);
	info->xattr_data = *p;
	*p += info->xattr_len;

	if (features == (u64)-1) {
		/* inline data */
		ceph_decode_64_safe(p, end, info->inline_version, bad);
		ceph_decode_32_safe(p, end, info->inline_len, bad);
		ceph_decode_need(p, end, info->inline_len, bad);
		info->inline_data = *p;
		*p += info->inline_len;
		/* quota */
		err = parse_reply_info_quota(p, end, info);
		if (err < 0)
			goto out_bad;
		/* pool namespace */
		ceph_decode_32_safe(p, end, info->pool_ns_len, bad);
		if (info->pool_ns_len > 0) {
			ceph_decode_need(p, end, info->pool_ns_len, bad);
			info->pool_ns_data = *p;
			*p += info->pool_ns_len;
		}

		/* btime */
		ceph_decode_need(p, end, sizeof(info->btime), bad);
		ceph_decode_copy(p, &info->btime, sizeof(info->btime));

		/* change attribute */
		ceph_decode_64_safe(p, end, info->change_attr, bad);

		/* dir pin */
		if (struct_v >= 2) {
			ceph_decode_32_safe(p, end, info->dir_pin, bad);
		} else {
			info->dir_pin = -ENODATA;
		}

		/* snapshot birth time, remains zero for v<=2 */
		if (struct_v >= 3) {
			ceph_decode_need(p, end, sizeof(info->snap_btime), bad);
			ceph_decode_copy(p, &info->snap_btime,
					 sizeof(info->snap_btime));
		} else {
			memset(&info->snap_btime, 0, sizeof(info->snap_btime));
		}

		*p = end;
	} else {
		if (features & CEPH_FEATURE_MDS_INLINE_DATA) {
			ceph_decode_64_safe(p, end, info->inline_version, bad);
			ceph_decode_32_safe(p, end, info->inline_len, bad);
			ceph_decode_need(p, end, info->inline_len, bad);
			info->inline_data = *p;
			*p += info->inline_len;
		} else
			info->inline_version = CEPH_INLINE_NONE;

		if (features & CEPH_FEATURE_MDS_QUOTA) {
			err = parse_reply_info_quota(p, end, info);
			if (err < 0)
				goto out_bad;
		} else {
			info->max_bytes = 0;
			info->max_files = 0;
		}

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

		if (features & CEPH_FEATURE_FS_BTIME) {
			ceph_decode_need(p, end, sizeof(info->btime), bad);
			ceph_decode_copy(p, &info->btime, sizeof(info->btime));
			ceph_decode_64_safe(p, end, info->change_attr, bad);
		}

		info->dir_pin = -ENODATA;
		/* info->snap_btime remains zero */
	}
	return 0;
bad:
	err = -EIO;
out_bad:
	return err;
}

static int parse_reply_info_dir(void **p, void *end,
				struct ceph_mds_reply_dirfrag **dirfrag,
				u64 features)
{
	if (features == (u64)-1) {
		u8 struct_v, struct_compat;
		u32 struct_len;
		ceph_decode_8_safe(p, end, struct_v, bad);
		ceph_decode_8_safe(p, end, struct_compat, bad);
		/* struct_v is expected to be >= 1. we only understand
		 * encoding whose struct_compat == 1. */
		if (!struct_v || struct_compat != 1)
			goto bad;
		ceph_decode_32_safe(p, end, struct_len, bad);
		ceph_decode_need(p, end, struct_len, bad);
		end = *p + struct_len;
	}

	ceph_decode_need(p, end, sizeof(**dirfrag), bad);
	*dirfrag = *p;
	*p += sizeof(**dirfrag) + sizeof(u32) * le32_to_cpu((*dirfrag)->ndist);
	if (unlikely(*p > end))
		goto bad;
	if (features == (u64)-1)
		*p = end;
	return 0;
bad:
	return -EIO;
}

static int parse_reply_info_lease(void **p, void *end,
				  struct ceph_mds_reply_lease **lease,
				  u64 features)
{
	if (features == (u64)-1) {
		u8 struct_v, struct_compat;
		u32 struct_len;
		ceph_decode_8_safe(p, end, struct_v, bad);
		ceph_decode_8_safe(p, end, struct_compat, bad);
		/* struct_v is expected to be >= 1. we only understand
		 * encoding whose struct_compat == 1. */
		if (!struct_v || struct_compat != 1)
			goto bad;
		ceph_decode_32_safe(p, end, struct_len, bad);
		ceph_decode_need(p, end, struct_len, bad);
		end = *p + struct_len;
	}

	ceph_decode_need(p, end, sizeof(**lease), bad);
	*lease = *p;
	*p += sizeof(**lease);
	if (features == (u64)-1)
		*p = end;
	return 0;
bad:
	return -EIO;
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

		err = parse_reply_info_dir(p, end, &info->dirfrag, features);
		if (err < 0)
			goto out_bad;

		ceph_decode_32_safe(p, end, info->dname_len, bad);
		ceph_decode_need(p, end, info->dname_len, bad);
		info->dname = *p;
		*p += info->dname_len;

		err = parse_reply_info_lease(p, end, &info->dlease, features);
		if (err < 0)
			goto out_bad;
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
static int parse_reply_info_readdir(void **p, void *end,
				struct ceph_mds_reply_info_parsed *info,
				u64 features)
{
	u32 num, i = 0;
	int err;

	err = parse_reply_info_dir(p, end, &info->dir_dir, features);
	if (err < 0)
		goto out_bad;

	ceph_decode_need(p, end, sizeof(num) + 2, bad);
	num = ceph_decode_32(p);
	{
		u16 flags = ceph_decode_16(p);
		info->dir_end = !!(flags & CEPH_READDIR_FRAG_END);
		info->dir_complete = !!(flags & CEPH_READDIR_FRAG_COMPLETE);
		info->hash_order = !!(flags & CEPH_READDIR_HASH_ORDER);
		info->offset_hash = !!(flags & CEPH_READDIR_OFFSET_HASH);
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
		ceph_decode_32_safe(p, end, rde->name_len, bad);
		ceph_decode_need(p, end, rde->name_len, bad);
		rde->name = *p;
		*p += rde->name_len;
		dout("parsed dir dname '%.*s'\n", rde->name_len, rde->name);

		/* dentry lease */
		err = parse_reply_info_lease(p, end, &rde->lease, features);
		if (err)
			goto out_bad;
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
	/* Skip over any unrecognized fields */
	*p = end;
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

	/* Skip over any unrecognized fields */
	*p = end;
	return 0;
bad:
	return -EIO;
}


#if BITS_PER_LONG == 64

#define DELEGATED_INO_AVAILABLE		xa_mk_value(1)

static int ceph_parse_deleg_inos(void **p, void *end,
				 struct ceph_mds_session *s)
{
	u32 sets;

	ceph_decode_32_safe(p, end, sets, bad);
	dout("got %u sets of delegated inodes\n", sets);
	while (sets--) {
		u64 start, len, ino;

		ceph_decode_64_safe(p, end, start, bad);
		ceph_decode_64_safe(p, end, len, bad);

		/* Don't accept a delegation of system inodes */
		if (start < CEPH_INO_SYSTEM_BASE) {
			pr_warn_ratelimited("ceph: ignoring reserved inode range delegation (start=0x%llx len=0x%llx)\n",
					start, len);
			continue;
		}
		while (len--) {
			int err = xa_insert(&s->s_delegated_inos, ino = start++,
					    DELEGATED_INO_AVAILABLE,
					    GFP_KERNEL);
			if (!err) {
				dout("added delegated inode 0x%llx\n",
				     start - 1);
			} else if (err == -EBUSY) {
				pr_warn("ceph: MDS delegated inode 0x%llx more than once.\n",
					start - 1);
			} else {
				return err;
			}
		}
	}
	return 0;
bad:
	return -EIO;
}

u64 ceph_get_deleg_ino(struct ceph_mds_session *s)
{
	unsigned long ino;
	void *val;

	xa_for_each(&s->s_delegated_inos, ino, val) {
		val = xa_erase(&s->s_delegated_inos, ino);
		if (val == DELEGATED_INO_AVAILABLE)
			return ino;
	}
	return 0;
}

int ceph_restore_deleg_ino(struct ceph_mds_session *s, u64 ino)
{
	return xa_insert(&s->s_delegated_inos, ino, DELEGATED_INO_AVAILABLE,
			 GFP_KERNEL);
}
#else /* BITS_PER_LONG == 64 */
/*
 * FIXME: xarrays can't handle 64-bit indexes on a 32-bit arch. For now, just
 * ignore delegated_inos on 32 bit arch. Maybe eventually add xarrays for top
 * and bottom words?
 */
static int ceph_parse_deleg_inos(void **p, void *end,
				 struct ceph_mds_session *s)
{
	u32 sets;

	ceph_decode_32_safe(p, end, sets, bad);
	if (sets)
		ceph_decode_skip_n(p, end, sets * 2 * sizeof(__le64), bad);
	return 0;
bad:
	return -EIO;
}

u64 ceph_get_deleg_ino(struct ceph_mds_session *s)
{
	return 0;
}

int ceph_restore_deleg_ino(struct ceph_mds_session *s, u64 ino)
{
	return 0;
}
#endif /* BITS_PER_LONG == 64 */

/*
 * parse create results
 */
static int parse_reply_info_create(void **p, void *end,
				  struct ceph_mds_reply_info_parsed *info,
				  u64 features, struct ceph_mds_session *s)
{
	int ret;

	if (features == (u64)-1 ||
	    (features & CEPH_FEATURE_REPLY_CREATE_INODE)) {
		if (*p == end) {
			/* Malformed reply? */
			info->has_create_ino = false;
		} else if (test_bit(CEPHFS_FEATURE_DELEG_INO, &s->s_features)) {
			u8 struct_v, struct_compat;
			u32 len;

			info->has_create_ino = true;
			ceph_decode_8_safe(p, end, struct_v, bad);
			ceph_decode_8_safe(p, end, struct_compat, bad);
			ceph_decode_32_safe(p, end, len, bad);
			ceph_decode_64_safe(p, end, info->ino, bad);
			ret = ceph_parse_deleg_inos(p, end, s);
			if (ret)
				return ret;
		} else {
			/* legacy */
			ceph_decode_64_safe(p, end, info->ino, bad);
			info->has_create_ino = true;
		}
	} else {
		if (*p != end)
			goto bad;
	}

	/* Skip over any unrecognized fields */
	*p = end;
	return 0;
bad:
	return -EIO;
}

/*
 * parse extra results
 */
static int parse_reply_info_extra(void **p, void *end,
				  struct ceph_mds_reply_info_parsed *info,
				  u64 features, struct ceph_mds_session *s)
{
	u32 op = le32_to_cpu(info->head->op);

	if (op == CEPH_MDS_OP_GETFILELOCK)
		return parse_reply_info_filelock(p, end, info, features);
	else if (op == CEPH_MDS_OP_READDIR || op == CEPH_MDS_OP_LSSNAP)
		return parse_reply_info_readdir(p, end, info, features);
	else if (op == CEPH_MDS_OP_CREATE)
		return parse_reply_info_create(p, end, info, features, s);
	else
		return -EIO;
}

/*
 * parse entire mds reply
 */
static int parse_reply_info(struct ceph_mds_session *s, struct ceph_msg *msg,
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
		err = parse_reply_info_extra(&p, p+len, info, features, s);
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
	case CEPH_MDS_SESSION_CLOSED: return "closed";
	case CEPH_MDS_SESSION_RESTARTING: return "restarting";
	case CEPH_MDS_SESSION_RECONNECTING: return "reconnecting";
	case CEPH_MDS_SESSION_REJECTED: return "rejected";
	default: return "???";
	}
}

struct ceph_mds_session *ceph_get_mds_session(struct ceph_mds_session *s)
{
	if (refcount_inc_not_zero(&s->s_ref)) {
		dout("mdsc get_session %p %d -> %d\n", s,
		     refcount_read(&s->s_ref)-1, refcount_read(&s->s_ref));
		return s;
	} else {
		dout("mdsc get_session %p 0 -- FAIL\n", s);
		return NULL;
	}
}

void ceph_put_mds_session(struct ceph_mds_session *s)
{
	if (IS_ERR_OR_NULL(s))
		return;

	dout("mdsc put_session %p %d -> %d\n", s,
	     refcount_read(&s->s_ref), refcount_read(&s->s_ref)-1);
	if (refcount_dec_and_test(&s->s_ref)) {
		if (s->s_auth.authorizer)
			ceph_auth_destroy_authorizer(s->s_auth.authorizer);
		WARN_ON(mutex_is_locked(&s->s_mutex));
		xa_destroy(&s->s_delegated_inos);
		kfree(s);
	}
}

/*
 * called under mdsc->mutex
 */
struct ceph_mds_session *__ceph_lookup_mds_session(struct ceph_mds_client *mdsc,
						   int mds)
{
	if (mds >= mdsc->max_sessions || !mdsc->sessions[mds])
		return NULL;
	return ceph_get_mds_session(mdsc->sessions[mds]);
}

static bool __have_session(struct ceph_mds_client *mdsc, int mds)
{
	if (mds >= mdsc->max_sessions || !mdsc->sessions[mds])
		return false;
	else
		return true;
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

	if (mds >= mdsc->mdsmap->possible_max_rank)
		return ERR_PTR(-EINVAL);

	s = kzalloc(sizeof(*s), GFP_NOFS);
	if (!s)
		return ERR_PTR(-ENOMEM);

	if (mds >= mdsc->max_sessions) {
		int newmax = 1 << get_count_order(mds + 1);
		struct ceph_mds_session **sa;

		dout("%s: realloc to %d\n", __func__, newmax);
		sa = kcalloc(newmax, sizeof(void *), GFP_NOFS);
		if (!sa)
			goto fail_realloc;
		if (mdsc->sessions) {
			memcpy(sa, mdsc->sessions,
			       mdsc->max_sessions * sizeof(void *));
			kfree(mdsc->sessions);
		}
		mdsc->sessions = sa;
		mdsc->max_sessions = newmax;
	}

	dout("%s: mds%d\n", __func__, mds);
	s->s_mdsc = mdsc;
	s->s_mds = mds;
	s->s_state = CEPH_MDS_SESSION_NEW;
	s->s_ttl = 0;
	s->s_seq = 0;
	mutex_init(&s->s_mutex);

	ceph_con_init(&s->s_con, s, &mds_con_ops, &mdsc->fsc->client->msgr);

	spin_lock_init(&s->s_gen_ttl_lock);
	s->s_cap_gen = 1;
	s->s_cap_ttl = jiffies - 1;

	spin_lock_init(&s->s_cap_lock);
	s->s_renew_requested = 0;
	s->s_renew_seq = 0;
	INIT_LIST_HEAD(&s->s_caps);
	s->s_nr_caps = 0;
	refcount_set(&s->s_ref, 1);
	INIT_LIST_HEAD(&s->s_waiting);
	INIT_LIST_HEAD(&s->s_unsafe);
	xa_init(&s->s_delegated_inos);
	s->s_num_cap_releases = 0;
	s->s_cap_reconnect = 0;
	s->s_cap_iterator = NULL;
	INIT_LIST_HEAD(&s->s_cap_releases);
	INIT_WORK(&s->s_cap_release_work, ceph_cap_release_work);

	INIT_LIST_HEAD(&s->s_cap_dirty);
	INIT_LIST_HEAD(&s->s_cap_flushing);

	mdsc->sessions[mds] = s;
	atomic_inc(&mdsc->num_sessions);
	refcount_inc(&s->s_ref);  /* one ref to sessions[], one to caller */

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

void ceph_mdsc_iterate_sessions(struct ceph_mds_client *mdsc,
				void (*cb)(struct ceph_mds_session *),
				bool check_state)
{
	int mds;

	mutex_lock(&mdsc->mutex);
	for (mds = 0; mds < mdsc->max_sessions; ++mds) {
		struct ceph_mds_session *s;

		s = __ceph_lookup_mds_session(mdsc, mds);
		if (!s)
			continue;

		if (check_state && !check_session_state(s)) {
			ceph_put_mds_session(s);
			continue;
		}

		mutex_unlock(&mdsc->mutex);
		cb(s);
		ceph_put_mds_session(s);
		mutex_lock(&mdsc->mutex);
	}
	mutex_unlock(&mdsc->mutex);
}

void ceph_mdsc_release_request(struct kref *kref)
{
	struct ceph_mds_request *req = container_of(kref,
						    struct ceph_mds_request,
						    r_kref);
	ceph_mdsc_release_dir_caps_no_check(req);
	destroy_reply_info(&req->r_reply_info);
	if (req->r_request)
		ceph_msg_put(req->r_request);
	if (req->r_reply)
		ceph_msg_put(req->r_reply);
	if (req->r_inode) {
		ceph_put_cap_refs(ceph_inode(req->r_inode), CEPH_CAP_PIN);
		/* avoid calling iput_final() in mds dispatch threads */
		ceph_async_iput(req->r_inode);
	}
	if (req->r_parent) {
		ceph_put_cap_refs(ceph_inode(req->r_parent), CEPH_CAP_PIN);
		ceph_async_iput(req->r_parent);
	}
	ceph_async_iput(req->r_target_inode);
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
		ceph_async_iput(req->r_old_dentry_dir);
	}
	kfree(req->r_path1);
	kfree(req->r_path2);
	if (req->r_pagelist)
		ceph_pagelist_release(req->r_pagelist);
	put_request_session(req);
	ceph_unreserve_caps(req->r_mdsc, &req->r_caps_reservation);
	WARN_ON_ONCE(!list_empty(&req->r_wait));
	kmem_cache_free(ceph_mds_request_cachep, req);
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
	int ret = 0;

	req->r_tid = ++mdsc->last_tid;
	if (req->r_num_caps) {
		ret = ceph_reserve_caps(mdsc, &req->r_caps_reservation,
					req->r_num_caps);
		if (ret < 0) {
			pr_err("__register_request %p "
			       "failed to reserve caps: %d\n", req, ret);
			/* set req->r_err to fail early from __do_request */
			req->r_err = ret;
			return;
		}
	}
	dout("__register_request %p tid %lld\n", req, req->r_tid);
	ceph_mdsc_get_request(req);
	insert_request(&mdsc->request_tree, req);

	req->r_uid = current_fsuid();
	req->r_gid = current_fsgid();

	if (mdsc->oldest_tid == 0 && req->r_op != CEPH_MDS_OP_SETFILELOCK)
		mdsc->oldest_tid = req->r_tid;

	if (dir) {
		struct ceph_inode_info *ci = ceph_inode(dir);

		ihold(dir);
		req->r_unsafe_dir = dir;
		spin_lock(&ci->i_unsafe_lock);
		list_add_tail(&req->r_unsafe_dir_item, &ci->i_unsafe_dirops);
		spin_unlock(&ci->i_unsafe_lock);
	}
}

static void __unregister_request(struct ceph_mds_client *mdsc,
				 struct ceph_mds_request *req)
{
	dout("__unregister_request %p tid %lld\n", req, req->r_tid);

	/* Never leave an unregistered request on an unsafe list! */
	list_del_init(&req->r_unsafe_item);

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

	if (req->r_unsafe_dir) {
		struct ceph_inode_info *ci = ceph_inode(req->r_unsafe_dir);
		spin_lock(&ci->i_unsafe_lock);
		list_del_init(&req->r_unsafe_dir_item);
		spin_unlock(&ci->i_unsafe_lock);
	}
	if (req->r_target_inode &&
	    test_bit(CEPH_MDS_R_GOT_UNSAFE, &req->r_req_flags)) {
		struct ceph_inode_info *ci = ceph_inode(req->r_target_inode);
		spin_lock(&ci->i_unsafe_lock);
		list_del_init(&req->r_unsafe_target_item);
		spin_unlock(&ci->i_unsafe_lock);
	}

	if (req->r_unsafe_dir) {
		/* avoid calling iput_final() in mds dispatch threads */
		ceph_async_iput(req->r_unsafe_dir);
		req->r_unsafe_dir = NULL;
	}

	complete_all(&req->r_safe_completion);

	ceph_mdsc_put_request(req);
}

/*
 * Walk back up the dentry tree until we hit a dentry representing a
 * non-snapshot inode. We do this using the rcu_read_lock (which must be held
 * when calling this) to ensure that the objects won't disappear while we're
 * working with them. Once we hit a candidate dentry, we attempt to take a
 * reference to it, and return that as the result.
 */
static struct inode *get_nonsnap_parent(struct dentry *dentry)
{
	struct inode *inode = NULL;

	while (dentry && !IS_ROOT(dentry)) {
		inode = d_inode_rcu(dentry);
		if (!inode || ceph_snap(inode) == CEPH_NOSNAP)
			break;
		dentry = dentry->d_parent;
	}
	if (inode)
		inode = igrab(inode);
	return inode;
}

/*
 * Choose mds to send request to next.  If there is a hint set in the
 * request (e.g., due to a prior forward hint from the mds), use that.
 * Otherwise, consult frag tree and/or caps to identify the
 * appropriate mds.  If all else fails, choose randomly.
 *
 * Called under mdsc->mutex.
 */
static int __choose_mds(struct ceph_mds_client *mdsc,
			struct ceph_mds_request *req,
			bool *random)
{
	struct inode *inode;
	struct ceph_inode_info *ci;
	struct ceph_cap *cap;
	int mode = req->r_direct_mode;
	int mds = -1;
	u32 hash = req->r_direct_hash;
	bool is_hash = test_bit(CEPH_MDS_R_DIRECT_IS_HASH, &req->r_req_flags);

	if (random)
		*random = false;

	/*
	 * is there a specific mds we should try?  ignore hint if we have
	 * no session and the mds is not up (active or recovering).
	 */
	if (req->r_resend_mds >= 0 &&
	    (__have_session(mdsc, req->r_resend_mds) ||
	     ceph_mdsmap_get_state(mdsc->mdsmap, req->r_resend_mds) > 0)) {
		dout("%s using resend_mds mds%d\n", __func__,
		     req->r_resend_mds);
		return req->r_resend_mds;
	}

	if (mode == USE_RANDOM_MDS)
		goto random;

	inode = NULL;
	if (req->r_inode) {
		if (ceph_snap(req->r_inode) != CEPH_SNAPDIR) {
			inode = req->r_inode;
			ihold(inode);
		} else {
			/* req->r_dentry is non-null for LSSNAP request */
			rcu_read_lock();
			inode = get_nonsnap_parent(req->r_dentry);
			rcu_read_unlock();
			dout("%s using snapdir's parent %p\n", __func__, inode);
		}
	} else if (req->r_dentry) {
		/* ignore race with rename; old or new d_parent is okay */
		struct dentry *parent;
		struct inode *dir;

		rcu_read_lock();
		parent = READ_ONCE(req->r_dentry->d_parent);
		dir = req->r_parent ? : d_inode_rcu(parent);

		if (!dir || dir->i_sb != mdsc->fsc->sb) {
			/*  not this fs or parent went negative */
			inode = d_inode(req->r_dentry);
			if (inode)
				ihold(inode);
		} else if (ceph_snap(dir) != CEPH_NOSNAP) {
			/* direct snapped/virtual snapdir requests
			 * based on parent dir inode */
			inode = get_nonsnap_parent(parent);
			dout("%s using nonsnap parent %p\n", __func__, inode);
		} else {
			/* dentry target */
			inode = d_inode(req->r_dentry);
			if (!inode || mode == USE_AUTH_MDS) {
				/* dir + name */
				inode = igrab(dir);
				hash = ceph_dentry_hash(dir, req->r_dentry);
				is_hash = true;
			} else {
				ihold(inode);
			}
		}
		rcu_read_unlock();
	}

	dout("%s %p is_hash=%d (0x%x) mode %d\n", __func__, inode, (int)is_hash,
	     hash, mode);
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
				dout("%s %p %llx.%llx frag %u mds%d (%d/%d)\n",
				     __func__, inode, ceph_vinop(inode),
				     frag.frag, mds, (int)r, frag.ndist);
				if (ceph_mdsmap_get_state(mdsc->mdsmap, mds) >=
				    CEPH_MDS_STATE_ACTIVE &&
				    !ceph_mdsmap_is_laggy(mdsc->mdsmap, mds))
					goto out;
			}

			/* since this file/dir wasn't known to be
			 * replicated, then we want to look for the
			 * authoritative mds. */
			if (frag.mds >= 0) {
				/* choose auth mds */
				mds = frag.mds;
				dout("%s %p %llx.%llx frag %u mds%d (auth)\n",
				     __func__, inode, ceph_vinop(inode),
				     frag.frag, mds);
				if (ceph_mdsmap_get_state(mdsc->mdsmap, mds) >=
				    CEPH_MDS_STATE_ACTIVE) {
					if (!ceph_mdsmap_is_laggy(mdsc->mdsmap,
								  mds))
						goto out;
				}
			}
			mode = USE_AUTH_MDS;
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
		ceph_async_iput(inode);
		goto random;
	}
	mds = cap->session->s_mds;
	dout("%s %p %llx.%llx mds%d (%scap %p)\n", __func__,
	     inode, ceph_vinop(inode), mds,
	     cap == ci->i_auth_cap ? "auth " : "", cap);
	spin_unlock(&ci->i_ceph_lock);
out:
	/* avoid calling iput_final() while holding mdsc->mutex or
	 * in mds dispatch threads */
	ceph_async_iput(inode);
	return mds;

random:
	if (random)
		*random = true;

	mds = ceph_mdsmap_get_random_mds(mdsc->mdsmap);
	dout("%s chose random mds%d\n", __func__, mds);
	return mds;
}


/*
 * session messages
 */
struct ceph_msg *ceph_create_session_msg(u32 op, u64 seq)
{
	struct ceph_msg *msg;
	struct ceph_mds_session_head *h;

	msg = ceph_msg_new(CEPH_MSG_CLIENT_SESSION, sizeof(*h), GFP_NOFS,
			   false);
	if (!msg) {
		pr_err("ENOMEM creating session %s msg\n",
		       ceph_session_op_name(op));
		return NULL;
	}
	h = msg->front.iov_base;
	h->op = cpu_to_le32(op);
	h->seq = cpu_to_le64(seq);

	return msg;
}

static const unsigned char feature_bits[] = CEPHFS_FEATURES_CLIENT_SUPPORTED;
#define FEATURE_BYTES(c) (DIV_ROUND_UP((size_t)feature_bits[c - 1] + 1, 64) * 8)
static int encode_supported_features(void **p, void *end)
{
	static const size_t count = ARRAY_SIZE(feature_bits);

	if (count > 0) {
		size_t i;
		size_t size = FEATURE_BYTES(count);
		unsigned long bit;

		if (WARN_ON_ONCE(*p + 4 + size > end))
			return -ERANGE;

		ceph_encode_32(p, size);
		memset(*p, 0, size);
		for (i = 0; i < count; i++) {
			bit = feature_bits[i];
			((unsigned char *)(*p))[bit / 8] |= BIT(bit % 8);
		}
		*p += size;
	} else {
		if (WARN_ON_ONCE(*p + 4 > end))
			return -ERANGE;

		ceph_encode_32(p, 0);
	}

	return 0;
}

static const unsigned char metric_bits[] = CEPHFS_METRIC_SPEC_CLIENT_SUPPORTED;
#define METRIC_BYTES(cnt) (DIV_ROUND_UP((size_t)metric_bits[cnt - 1] + 1, 64) * 8)
static int encode_metric_spec(void **p, void *end)
{
	static const size_t count = ARRAY_SIZE(metric_bits);

	/* header */
	if (WARN_ON_ONCE(*p + 2 > end))
		return -ERANGE;

	ceph_encode_8(p, 1); /* version */
	ceph_encode_8(p, 1); /* compat */

	if (count > 0) {
		size_t i;
		size_t size = METRIC_BYTES(count);

		if (WARN_ON_ONCE(*p + 4 + 4 + size > end))
			return -ERANGE;

		/* metric spec info length */
		ceph_encode_32(p, 4 + size);

		/* metric spec */
		ceph_encode_32(p, size);
		memset(*p, 0, size);
		for (i = 0; i < count; i++)
			((unsigned char *)(*p))[i / 8] |= BIT(metric_bits[i] % 8);
		*p += size;
	} else {
		if (WARN_ON_ONCE(*p + 4 + 4 > end))
			return -ERANGE;

		/* metric spec info length */
		ceph_encode_32(p, 4);
		/* metric spec */
		ceph_encode_32(p, 0);
	}

	return 0;
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
	int extra_bytes = 0;
	int metadata_key_count = 0;
	struct ceph_options *opt = mdsc->fsc->client->options;
	struct ceph_mount_options *fsopt = mdsc->fsc->mount_options;
	size_t size, count;
	void *p, *end;
	int ret;

	const char* metadata[][2] = {
		{"hostname", mdsc->nodename},
		{"kernel_version", init_utsname()->release},
		{"entity_id", opt->name ? : ""},
		{"root", fsopt->server_path ? : "/"},
		{NULL, NULL}
	};

	/* Calculate serialized length of metadata */
	extra_bytes = 4;  /* map length */
	for (i = 0; metadata[i][0]; ++i) {
		extra_bytes += 8 + strlen(metadata[i][0]) +
			strlen(metadata[i][1]);
		metadata_key_count++;
	}

	/* supported feature */
	size = 0;
	count = ARRAY_SIZE(feature_bits);
	if (count > 0)
		size = FEATURE_BYTES(count);
	extra_bytes += 4 + size;

	/* metric spec */
	size = 0;
	count = ARRAY_SIZE(metric_bits);
	if (count > 0)
		size = METRIC_BYTES(count);
	extra_bytes += 2 + 4 + 4 + size;

	/* Allocate the message */
	msg = ceph_msg_new(CEPH_MSG_CLIENT_SESSION, sizeof(*h) + extra_bytes,
			   GFP_NOFS, false);
	if (!msg) {
		pr_err("ENOMEM creating session open msg\n");
		return ERR_PTR(-ENOMEM);
	}
	p = msg->front.iov_base;
	end = p + msg->front.iov_len;

	h = p;
	h->op = cpu_to_le32(CEPH_SESSION_REQUEST_OPEN);
	h->seq = cpu_to_le64(seq);

	/*
	 * Serialize client metadata into waiting buffer space, using
	 * the format that userspace expects for map<string, string>
	 *
	 * ClientSession messages with metadata are v4
	 */
	msg->hdr.version = cpu_to_le16(4);
	msg->hdr.compat_version = cpu_to_le16(1);

	/* The write pointer, following the session_head structure */
	p += sizeof(*h);

	/* Number of entries in the map */
	ceph_encode_32(&p, metadata_key_count);

	/* Two length-prefixed strings for each entry in the map */
	for (i = 0; metadata[i][0]; ++i) {
		size_t const key_len = strlen(metadata[i][0]);
		size_t const val_len = strlen(metadata[i][1]);

		ceph_encode_32(&p, key_len);
		memcpy(p, metadata[i][0], key_len);
		p += key_len;
		ceph_encode_32(&p, val_len);
		memcpy(p, metadata[i][1], val_len);
		p += val_len;
	}

	ret = encode_supported_features(&p, end);
	if (ret) {
		pr_err("encode_supported_features failed!\n");
		ceph_msg_put(msg);
		return ERR_PTR(ret);
	}

	ret = encode_metric_spec(&p, end);
	if (ret) {
		pr_err("encode_metric_spec failed!\n");
		ceph_msg_put(msg);
		return ERR_PTR(ret);
	}

	msg->front.iov_len = p - msg->front.iov_base;
	msg->hdr.front_len = cpu_to_le32(msg->front.iov_len);

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
	if (IS_ERR(msg))
		return PTR_ERR(msg);
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
	int ret;

	session = __ceph_lookup_mds_session(mdsc, target);
	if (!session) {
		session = register_session(mdsc, target);
		if (IS_ERR(session))
			return session;
	}
	if (session->s_state == CEPH_MDS_SESSION_NEW ||
	    session->s_state == CEPH_MDS_SESSION_CLOSING) {
		ret = __open_session(mdsc, session);
		if (ret)
			return ERR_PTR(ret);
	}

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

	if (mds >= mdsc->mdsmap->possible_max_rank)
		return;

	mi = &mdsc->mdsmap->m_info[mds];
	dout("open_export_target_sessions for mds%d (%d targets)\n",
	     session->s_mds, mi->num_export_targets);

	for (i = 0; i < mi->num_export_targets; i++) {
		ts = __open_export_target_session(mdsc, mi->export_targets[i]);
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

static void detach_cap_releases(struct ceph_mds_session *session,
				struct list_head *target)
{
	lockdep_assert_held(&session->s_cap_lock);

	list_splice_init(&session->s_cap_releases, target);
	session->s_num_cap_releases = 0;
	dout("dispose_cap_releases mds%d\n", session->s_mds);
}

static void dispose_cap_releases(struct ceph_mds_client *mdsc,
				 struct list_head *dispose)
{
	while (!list_empty(dispose)) {
		struct ceph_cap *cap;
		/* zero out the in-progress message */
		cap = list_first_entry(dispose, struct ceph_cap, session_caps);
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
		pr_warn_ratelimited(" dropping unsafe request %llu\n",
				    req->r_tid);
		if (req->r_target_inode)
			mapping_set_error(req->r_target_inode->i_mapping, -EIO);
		if (req->r_unsafe_dir)
			mapping_set_error(req->r_unsafe_dir->i_mapping, -EIO);
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
int ceph_iterate_session_caps(struct ceph_mds_session *session,
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
			/* avoid calling iput_final() while holding
			 * s_mutex or in mds dispatch threads */
			ceph_async_iput(last_inode);
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
		if (!cap->ci) {
			dout("iterate_session_caps  finishing cap %p removal\n",
			     cap);
			BUG_ON(cap->session != session);
			cap->session = NULL;
			list_del_init(&cap->session_caps);
			session->s_nr_caps--;
			atomic64_dec(&session->s_mdsc->metric.total_caps);
			if (cap->queue_release)
				__ceph_queue_cap_release(session, cap);
			else
				old_cap = cap;  /* put_cap it w/o locks held */
		}
		if (ret < 0)
			goto out;
	}
	ret = 0;
out:
	session->s_cap_iterator = NULL;
	spin_unlock(&session->s_cap_lock);

	ceph_async_iput(last_inode);
	if (old_cap)
		ceph_put_cap(session->s_mdsc, old_cap);

	return ret;
}

static int remove_capsnaps(struct ceph_mds_client *mdsc, struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_cap_snap *capsnap;
	int capsnap_release = 0;

	lockdep_assert_held(&ci->i_ceph_lock);

	dout("removing capsnaps, ci is %p, inode is %p\n", ci, inode);

	while (!list_empty(&ci->i_cap_snaps)) {
		capsnap = list_first_entry(&ci->i_cap_snaps,
					   struct ceph_cap_snap, ci_item);
		__ceph_remove_capsnap(inode, capsnap, NULL, NULL);
		ceph_put_snap_context(capsnap->context);
		ceph_put_cap_snap(capsnap);
		capsnap_release++;
	}
	wake_up_all(&ci->i_cap_wq);
	wake_up_all(&mdsc->cap_flushing_wq);
	return capsnap_release;
}

static int remove_session_caps_cb(struct inode *inode, struct ceph_cap *cap,
				  void *arg)
{
	struct ceph_fs_client *fsc = (struct ceph_fs_client *)arg;
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_inode_info *ci = ceph_inode(inode);
	LIST_HEAD(to_remove);
	bool dirty_dropped = false;
	bool invalidate = false;
	int capsnap_release = 0;

	dout("removing cap %p, ci is %p, inode is %p\n",
	     cap, ci, &ci->vfs_inode);
	spin_lock(&ci->i_ceph_lock);
	__ceph_remove_cap(cap, false);
	if (!ci->i_auth_cap) {
		struct ceph_cap_flush *cf;

		if (READ_ONCE(fsc->mount_state) == CEPH_MOUNT_SHUTDOWN) {
			if (inode->i_data.nrpages > 0)
				invalidate = true;
			if (ci->i_wrbuffer_ref > 0)
				mapping_set_error(&inode->i_data, -EIO);
		}

		while (!list_empty(&ci->i_cap_flush_list)) {
			cf = list_first_entry(&ci->i_cap_flush_list,
					      struct ceph_cap_flush, i_list);
			list_move(&cf->i_list, &to_remove);
		}

		spin_lock(&mdsc->cap_dirty_lock);

		list_for_each_entry(cf, &to_remove, i_list)
			list_del_init(&cf->g_list);

		if (!list_empty(&ci->i_dirty_item)) {
			pr_warn_ratelimited(
				" dropping dirty %s state for %p %lld\n",
				ceph_cap_string(ci->i_dirty_caps),
				inode, ceph_ino(inode));
			ci->i_dirty_caps = 0;
			list_del_init(&ci->i_dirty_item);
			dirty_dropped = true;
		}
		if (!list_empty(&ci->i_flushing_item)) {
			pr_warn_ratelimited(
				" dropping dirty+flushing %s state for %p %lld\n",
				ceph_cap_string(ci->i_flushing_caps),
				inode, ceph_ino(inode));
			ci->i_flushing_caps = 0;
			list_del_init(&ci->i_flushing_item);
			mdsc->num_cap_flushing--;
			dirty_dropped = true;
		}
		spin_unlock(&mdsc->cap_dirty_lock);

		if (dirty_dropped) {
			mapping_set_error(inode->i_mapping, -EIO);

			if (ci->i_wrbuffer_ref_head == 0 &&
			    ci->i_wr_ref == 0 &&
			    ci->i_dirty_caps == 0 &&
			    ci->i_flushing_caps == 0) {
				ceph_put_snap_context(ci->i_head_snapc);
				ci->i_head_snapc = NULL;
			}
		}

		if (atomic_read(&ci->i_filelock_ref) > 0) {
			/* make further file lock syscall return -EIO */
			ci->i_ceph_flags |= CEPH_I_ERROR_FILELOCK;
			pr_warn_ratelimited(" dropping file locks for %p %lld\n",
					    inode, ceph_ino(inode));
		}

		if (!ci->i_dirty_caps && ci->i_prealloc_cap_flush) {
			list_add(&ci->i_prealloc_cap_flush->i_list, &to_remove);
			ci->i_prealloc_cap_flush = NULL;
		}

		if (!list_empty(&ci->i_cap_snaps))
			capsnap_release = remove_capsnaps(mdsc, inode);
	}
	spin_unlock(&ci->i_ceph_lock);
	while (!list_empty(&to_remove)) {
		struct ceph_cap_flush *cf;
		cf = list_first_entry(&to_remove,
				      struct ceph_cap_flush, i_list);
		list_del_init(&cf->i_list);
		if (!cf->is_capsnap)
			ceph_free_cap_flush(cf);
	}

	wake_up_all(&ci->i_cap_wq);
	if (invalidate)
		ceph_queue_invalidate(inode);
	if (dirty_dropped)
		iput(inode);
	while (capsnap_release--)
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
	LIST_HEAD(dispose);

	dout("remove_session_caps on %p\n", session);
	ceph_iterate_session_caps(session, remove_session_caps_cb, fsc);

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
			 /* avoid calling iput_final() while holding s_mutex */
			ceph_async_iput(inode);

			spin_lock(&session->s_cap_lock);
		}
	}

	// drop cap expires and unlock s_cap_lock
	detach_cap_releases(session, &dispose);

	BUG_ON(session->s_nr_caps > 0);
	BUG_ON(!list_empty(&session->s_cap_flushing));
	spin_unlock(&session->s_cap_lock);
	dispose_cap_releases(session->s_mdsc, &dispose);
}

enum {
	RECONNECT,
	RENEWCAPS,
	FORCE_RO,
};

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
	unsigned long ev = (unsigned long)arg;

	if (ev == RECONNECT) {
		spin_lock(&ci->i_ceph_lock);
		ci->i_wanted_max_size = 0;
		ci->i_requested_max_size = 0;
		spin_unlock(&ci->i_ceph_lock);
	} else if (ev == RENEWCAPS) {
		if (cap->cap_gen < cap->session->s_cap_gen) {
			/* mds did not re-issue stale cap */
			spin_lock(&ci->i_ceph_lock);
			cap->issued = cap->implemented = CEPH_CAP_PIN;
			spin_unlock(&ci->i_ceph_lock);
		}
	} else if (ev == FORCE_RO) {
	}
	wake_up_all(&ci->i_cap_wq);
	return 0;
}

static void wake_up_session_caps(struct ceph_mds_session *session, int ev)
{
	dout("wake_up_session_caps %p mds%d\n", session, session->s_mds);
	ceph_iterate_session_caps(session, wake_up_session_cb,
				  (void *)(unsigned long)ev);
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
	msg = ceph_create_session_msg(CEPH_SESSION_REQUEST_RENEWCAPS,
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
	msg = ceph_create_session_msg(CEPH_SESSION_FLUSHMSG_ACK, seq);
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
		wake_up_session_caps(session, RENEWCAPS);
}

/*
 * send a session close request
 */
static int request_close_session(struct ceph_mds_session *session)
{
	struct ceph_msg *msg;

	dout("request_close_session mds%d state %s seq %lld\n",
	     session->s_mds, ceph_session_state_name(session->s_state),
	     session->s_seq);
	msg = ceph_create_session_msg(CEPH_SESSION_REQUEST_CLOSE,
				      session->s_seq);
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
	return request_close_session(session);
}

static bool drop_negative_children(struct dentry *dentry)
{
	struct dentry *child;
	bool all_negative = true;

	if (!d_is_dir(dentry))
		goto out;

	spin_lock(&dentry->d_lock);
	list_for_each_entry(child, &dentry->d_subdirs, d_child) {
		if (d_really_is_positive(child)) {
			all_negative = false;
			break;
		}
	}
	spin_unlock(&dentry->d_lock);

	if (all_negative)
		shrink_dcache_parent(dentry);
out:
	return all_negative;
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
	int *remaining = arg;
	struct ceph_inode_info *ci = ceph_inode(inode);
	int used, wanted, oissued, mine;

	if (*remaining <= 0)
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
		/* Note: it's possible that i_filelock_ref becomes non-zero
		 * after dropping auth caps. It doesn't hurt because reply
		 * of lock mds request will re-add auth caps. */
		if (atomic_read(&ci->i_filelock_ref) > 0)
			goto out;
	}
	/* The inode has cached pages, but it's no longer used.
	 * we can safely drop it */
	if (S_ISREG(inode->i_mode) &&
	    wanted == 0 && used == CEPH_CAP_FILE_CACHE &&
	    !(oissued & CEPH_CAP_FILE_CACHE)) {
	  used = 0;
	  oissued = 0;
	}
	if ((used | wanted) & ~oissued & mine)
		goto out;   /* we need these caps */

	if (oissued) {
		/* we aren't the only cap.. just remove us */
		__ceph_remove_cap(cap, true);
		(*remaining)--;
	} else {
		struct dentry *dentry;
		/* try dropping referring dentries */
		spin_unlock(&ci->i_ceph_lock);
		dentry = d_find_any_alias(inode);
		if (dentry && drop_negative_children(dentry)) {
			int count;
			dput(dentry);
			d_prune_aliases(inode);
			count = atomic_read(&inode->i_count);
			if (count == 1)
				(*remaining)--;
			dout("trim_caps_cb %p cap %p pruned, count now %d\n",
			     inode, cap, count);
		} else {
			dput(dentry);
		}
		return 0;
	}

out:
	spin_unlock(&ci->i_ceph_lock);
	return 0;
}

/*
 * Trim session cap count down to some max number.
 */
int ceph_trim_caps(struct ceph_mds_client *mdsc,
		   struct ceph_mds_session *session,
		   int max_caps)
{
	int trim_caps = session->s_nr_caps - max_caps;

	dout("trim_caps mds%d start: %d / %d, trim %d\n",
	     session->s_mds, session->s_nr_caps, max_caps, trim_caps);
	if (trim_caps > 0) {
		int remaining = trim_caps;

		ceph_iterate_session_caps(session, trim_caps_cb, &remaining);
		dout("trim_caps mds%d done: %d / %d, trimmed %d\n",
		     session->s_mds, session->s_nr_caps, max_caps,
			trim_caps - remaining);
	}

	ceph_flush_cap_releases(mdsc, session);
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
static void ceph_send_cap_releases(struct ceph_mds_client *mdsc,
				   struct ceph_mds_session *session)
{
	struct ceph_msg *msg = NULL;
	struct ceph_mds_cap_release *head;
	struct ceph_mds_cap_item *item;
	struct ceph_osd_client *osdc = &mdsc->fsc->client->osdc;
	struct ceph_cap *cap;
	LIST_HEAD(tmp_list);
	int num_cap_releases;
	__le32	barrier, *cap_barrier;

	down_read(&osdc->lock);
	barrier = cpu_to_le32(osdc->epoch_barrier);
	up_read(&osdc->lock);

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

			msg->hdr.version = cpu_to_le16(2);
			msg->hdr.compat_version = cpu_to_le16(1);
		}

		cap = list_first_entry(&tmp_list, struct ceph_cap,
					session_caps);
		list_del(&cap->session_caps);
		num_cap_releases--;

		head = msg->front.iov_base;
		put_unaligned_le32(get_unaligned_le32(&head->num) + 1,
				   &head->num);
		item = msg->front.iov_base + msg->front.iov_len;
		item->ino = cpu_to_le64(cap->cap_ino);
		item->cap_id = cpu_to_le64(cap->cap_id);
		item->migrate_seq = cpu_to_le32(cap->mseq);
		item->seq = cpu_to_le32(cap->issue_seq);
		msg->front.iov_len += sizeof(*item);

		ceph_put_cap(mdsc, cap);

		if (le32_to_cpu(head->num) == CEPH_CAPS_PER_RELEASE) {
			// Append cap_barrier field
			cap_barrier = msg->front.iov_base + msg->front.iov_len;
			*cap_barrier = barrier;
			msg->front.iov_len += sizeof(*cap_barrier);

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
		// Append cap_barrier field
		cap_barrier = msg->front.iov_base + msg->front.iov_len;
		*cap_barrier = barrier;
		msg->front.iov_len += sizeof(*cap_barrier);

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

static void ceph_cap_release_work(struct work_struct *work)
{
	struct ceph_mds_session *session =
		container_of(work, struct ceph_mds_session, s_cap_release_work);

	mutex_lock(&session->s_mutex);
	if (session->s_state == CEPH_MDS_SESSION_OPEN ||
	    session->s_state == CEPH_MDS_SESSION_HUNG)
		ceph_send_cap_releases(session->s_mdsc, session);
	mutex_unlock(&session->s_mutex);
	ceph_put_mds_session(session);
}

void ceph_flush_cap_releases(struct ceph_mds_client *mdsc,
		             struct ceph_mds_session *session)
{
	if (mdsc->stopping)
		return;

	ceph_get_mds_session(session);
	if (queue_work(mdsc->fsc->cap_wq,
		       &session->s_cap_release_work)) {
		dout("cap release work queued\n");
	} else {
		ceph_put_mds_session(session);
		dout("failed to queue cap release work\n");
	}
}

/*
 * caller holds session->s_cap_lock
 */
void __ceph_queue_cap_release(struct ceph_mds_session *session,
			      struct ceph_cap *cap)
{
	list_add_tail(&cap->session_caps, &session->s_cap_releases);
	session->s_num_cap_releases++;

	if (!(session->s_num_cap_releases % CEPH_CAPS_PER_RELEASE))
		ceph_flush_cap_releases(session->s_mdsc, session);
}

static void ceph_cap_reclaim_work(struct work_struct *work)
{
	struct ceph_mds_client *mdsc =
		container_of(work, struct ceph_mds_client, cap_reclaim_work);
	int ret = ceph_trim_dentries(mdsc);
	if (ret == -EAGAIN)
		ceph_queue_cap_reclaim_work(mdsc);
}

void ceph_queue_cap_reclaim_work(struct ceph_mds_client *mdsc)
{
	if (mdsc->stopping)
		return;

        if (queue_work(mdsc->fsc->cap_wq, &mdsc->cap_reclaim_work)) {
                dout("caps reclaim work queued\n");
        } else {
                dout("failed to queue caps release work\n");
        }
}

void ceph_reclaim_caps_nr(struct ceph_mds_client *mdsc, int nr)
{
	int val;
	if (!nr)
		return;
	val = atomic_add_return(nr, &mdsc->cap_reclaim_pending);
	if ((val % CEPH_CAPS_PER_RELEASE) < nr) {
		atomic_set(&mdsc->cap_reclaim_pending, 0);
		ceph_queue_cap_reclaim_work(mdsc);
	}
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
	unsigned int num_entries;
	int order;

	spin_lock(&ci->i_ceph_lock);
	num_entries = ci->i_files + ci->i_subdirs;
	spin_unlock(&ci->i_ceph_lock);
	num_entries = max(num_entries, 1U);
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
	struct ceph_mds_request *req;

	req = kmem_cache_zalloc(ceph_mds_request_cachep, GFP_NOFS);
	if (!req)
		return ERR_PTR(-ENOMEM);

	mutex_init(&req->r_fill_mutex);
	req->r_mdsc = mdsc;
	req->r_started = jiffies;
	req->r_start_latency = ktime_get();
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

	ktime_get_coarse_real_ts64(&req->r_stamp);

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
char *ceph_mdsc_build_path(struct dentry *dentry, int *plen, u64 *pbase,
			   int stop_on_nosnap)
{
	struct dentry *temp;
	char *path;
	int pos;
	unsigned seq;
	u64 base;

	if (!dentry)
		return ERR_PTR(-EINVAL);

	path = __getname();
	if (!path)
		return ERR_PTR(-ENOMEM);
retry:
	pos = PATH_MAX - 1;
	path[pos] = '\0';

	seq = read_seqbegin(&rename_lock);
	rcu_read_lock();
	temp = dentry;
	for (;;) {
		struct inode *inode;

		spin_lock(&temp->d_lock);
		inode = d_inode(temp);
		if (inode && ceph_snap(inode) == CEPH_SNAPDIR) {
			dout("build_path path+%d: %p SNAPDIR\n",
			     pos, temp);
		} else if (stop_on_nosnap && inode && dentry != temp &&
			   ceph_snap(inode) == CEPH_NOSNAP) {
			spin_unlock(&temp->d_lock);
			pos++; /* get rid of any prepended '/' */
			break;
		} else {
			pos -= temp->d_name.len;
			if (pos < 0) {
				spin_unlock(&temp->d_lock);
				break;
			}
			memcpy(path + pos, temp->d_name.name, temp->d_name.len);
		}
		spin_unlock(&temp->d_lock);
		temp = READ_ONCE(temp->d_parent);

		/* Are we at the root? */
		if (IS_ROOT(temp))
			break;

		/* Are we out of buffer? */
		if (--pos < 0)
			break;

		path[pos] = '/';
	}
	base = ceph_ino(d_inode(temp));
	rcu_read_unlock();

	if (read_seqretry(&rename_lock, seq))
		goto retry;

	if (pos < 0) {
		/*
		 * A rename didn't occur, but somehow we didn't end up where
		 * we thought we would. Throw a warning and try again.
		 */
		pr_warn("build_path did not end path lookup where "
			"expected, pos is %d\n", pos);
		goto retry;
	}

	*pbase = base;
	*plen = PATH_MAX - 1 - pos;
	dout("build_path on %p %d built %llx '%.*s'\n",
	     dentry, d_count(dentry), base, *plen, path + pos);
	return path + pos;
}

static int build_dentry_path(struct dentry *dentry, struct inode *dir,
			     const char **ppath, int *ppathlen, u64 *pino,
			     bool *pfreepath, bool parent_locked)
{
	char *path;

	rcu_read_lock();
	if (!dir)
		dir = d_inode_rcu(dentry->d_parent);
	if (dir && parent_locked && ceph_snap(dir) == CEPH_NOSNAP) {
		*pino = ceph_ino(dir);
		rcu_read_unlock();
		*ppath = dentry->d_name.name;
		*ppathlen = dentry->d_name.len;
		return 0;
	}
	rcu_read_unlock();
	path = ceph_mdsc_build_path(dentry, ppathlen, pino, 1);
	if (IS_ERR(path))
		return PTR_ERR(path);
	*ppath = path;
	*pfreepath = true;
	return 0;
}

static int build_inode_path(struct inode *inode,
			    const char **ppath, int *ppathlen, u64 *pino,
			    bool *pfreepath)
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
	*pfreepath = true;
	return 0;
}

/*
 * request arguments may be specified via an inode *, a dentry *, or
 * an explicit ino+path.
 */
static int set_request_path_attr(struct inode *rinode, struct dentry *rdentry,
				  struct inode *rdiri, const char *rpath,
				  u64 rino, const char **ppath, int *pathlen,
				  u64 *ino, bool *freepath, bool parent_locked)
{
	int r = 0;

	if (rinode) {
		r = build_inode_path(rinode, ppath, pathlen, ino, freepath);
		dout(" inode %p %llx.%llx\n", rinode, ceph_ino(rinode),
		     ceph_snap(rinode));
	} else if (rdentry) {
		r = build_dentry_path(rdentry, rdiri, ppath, pathlen, ino,
					freepath, parent_locked);
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
	bool freepath1 = false, freepath2 = false;
	int len;
	u16 releases;
	void *p, *end;
	int ret;

	ret = set_request_path_attr(req->r_inode, req->r_dentry,
			      req->r_parent, req->r_path1, req->r_ino1.ino,
			      &path1, &pathlen1, &ino1, &freepath1,
			      test_bit(CEPH_MDS_R_PARENT_LOCKED,
					&req->r_req_flags));
	if (ret < 0) {
		msg = ERR_PTR(ret);
		goto out;
	}

	/* If r_old_dentry is set, then assume that its parent is locked */
	ret = set_request_path_attr(NULL, req->r_old_dentry,
			      req->r_old_dentry_dir,
			      req->r_path2, req->r_ino2.ino,
			      &path2, &pathlen2, &ino2, &freepath2, true);
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
		len += pathlen1;
	if (req->r_old_dentry_drop)
		len += pathlen2;

	msg = ceph_msg_new2(CEPH_MSG_CLIENT_REQUEST, len, 1, GFP_NOFS, false);
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
	head->ino = cpu_to_le64(req->r_deleg_ino);
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
		      mds, req->r_inode_drop, req->r_inode_unless,
		      req->r_op == CEPH_MDS_OP_READDIR);
	if (req->r_dentry_drop)
		releases += ceph_encode_dentry_release(&p, req->r_dentry,
				req->r_parent, mds, req->r_dentry_drop,
				req->r_dentry_unless);
	if (req->r_old_dentry_drop)
		releases += ceph_encode_dentry_release(&p, req->r_old_dentry,
				req->r_old_dentry_dir, mds,
				req->r_old_dentry_drop,
				req->r_old_dentry_unless);
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
		ceph_encode_timespec64(&ts, &req->r_stamp);
		ceph_encode_copy(&p, &ts, sizeof(ts));
	}

	if (WARN_ON_ONCE(p > end)) {
		ceph_msg_put(msg);
		msg = ERR_PTR(-ERANGE);
		goto out_free2;
	}

	msg->front.iov_len = p - msg->front.iov_base;
	msg->hdr.front_len = cpu_to_le32(msg->front.iov_len);

	if (req->r_pagelist) {
		struct ceph_pagelist *pagelist = req->r_pagelist;
		ceph_msg_data_add_pagelist(msg, pagelist);
		msg->hdr.data_len = cpu_to_le32(pagelist->length);
	} else {
		msg->hdr.data_len = 0;
	}

	msg->hdr.data_off = cpu_to_le16(0);

out_free2:
	if (freepath2)
		ceph_mdsc_free_path((char *)path2, pathlen2);
out_free1:
	if (freepath1)
		ceph_mdsc_free_path((char *)path1, pathlen1);
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
	req->r_end_latency = ktime_get();

	if (req->r_callback)
		req->r_callback(mdsc, req);
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

	if (test_bit(CEPH_MDS_R_GOT_UNSAFE, &req->r_req_flags)) {
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
			ceph_encode_timespec64(&ts, &req->r_stamp);
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
	if (test_bit(CEPH_MDS_R_GOT_UNSAFE, &req->r_req_flags))
		flags |= CEPH_MDS_FLAG_REPLAY;
	if (test_bit(CEPH_MDS_R_ASYNC, &req->r_req_flags))
		flags |= CEPH_MDS_FLAG_ASYNC;
	if (req->r_parent)
		flags |= CEPH_MDS_FLAG_WANT_DENTRY;
	rhead->flags = cpu_to_le32(flags);
	rhead->num_fwd = req->r_num_fwd;
	rhead->num_retry = req->r_attempts - 1;

	dout(" r_parent = %p\n", req->r_parent);
	return 0;
}

/*
 * called under mdsc->mutex
 */
static int __send_request(struct ceph_mds_client *mdsc,
			  struct ceph_mds_session *session,
			  struct ceph_mds_request *req,
			  bool drop_cap_releases)
{
	int err;

	err = __prepare_send_request(mdsc, req, session->s_mds,
				     drop_cap_releases);
	if (!err) {
		ceph_msg_get(req->r_request);
		ceph_con_send(&session->s_con, req->r_request);
	}

	return err;
}

/*
 * send request, or put it on the appropriate wait list.
 */
static void __do_request(struct ceph_mds_client *mdsc,
			struct ceph_mds_request *req)
{
	struct ceph_mds_session *session = NULL;
	int mds = -1;
	int err = 0;
	bool random;

	if (req->r_err || test_bit(CEPH_MDS_R_GOT_RESULT, &req->r_req_flags)) {
		if (test_bit(CEPH_MDS_R_ABORTED, &req->r_req_flags))
			__unregister_request(mdsc, req);
		return;
	}

	if (req->r_timeout &&
	    time_after_eq(jiffies, req->r_started + req->r_timeout)) {
		dout("do_request timed out\n");
		err = -ETIMEDOUT;
		goto finish;
	}
	if (READ_ONCE(mdsc->fsc->mount_state) == CEPH_MOUNT_SHUTDOWN) {
		dout("do_request forced umount\n");
		err = -EIO;
		goto finish;
	}
	if (READ_ONCE(mdsc->fsc->mount_state) == CEPH_MOUNT_MOUNTING) {
		if (mdsc->mdsmap_err) {
			err = mdsc->mdsmap_err;
			dout("do_request mdsmap err %d\n", err);
			goto finish;
		}
		if (mdsc->mdsmap->m_epoch == 0) {
			dout("do_request no mdsmap, waiting for map\n");
			list_add(&req->r_wait, &mdsc->waiting_for_map);
			return;
		}
		if (!(mdsc->fsc->mount_options->flags &
		      CEPH_MOUNT_OPT_MOUNTWAIT) &&
		    !ceph_mdsmap_is_cluster_available(mdsc->mdsmap)) {
			err = -EHOSTUNREACH;
			goto finish;
		}
	}

	put_request_session(req);

	mds = __choose_mds(mdsc, req, &random);
	if (mds < 0 ||
	    ceph_mdsmap_get_state(mdsc->mdsmap, mds) < CEPH_MDS_STATE_ACTIVE) {
		if (test_bit(CEPH_MDS_R_ASYNC, &req->r_req_flags)) {
			err = -EJUKEBOX;
			goto finish;
		}
		dout("do_request no mds or not active, waiting for map\n");
		list_add(&req->r_wait, &mdsc->waiting_for_map);
		return;
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
	req->r_session = ceph_get_mds_session(session);

	dout("do_request mds%d session %p state %s\n", mds, session,
	     ceph_session_state_name(session->s_state));
	if (session->s_state != CEPH_MDS_SESSION_OPEN &&
	    session->s_state != CEPH_MDS_SESSION_HUNG) {
		if (session->s_state == CEPH_MDS_SESSION_REJECTED) {
			err = -EACCES;
			goto out_session;
		}
		/*
		 * We cannot queue async requests since the caps and delegated
		 * inodes are bound to the session. Just return -EJUKEBOX and
		 * let the caller retry a sync request in that case.
		 */
		if (test_bit(CEPH_MDS_R_ASYNC, &req->r_req_flags)) {
			err = -EJUKEBOX;
			goto out_session;
		}
		if (session->s_state == CEPH_MDS_SESSION_NEW ||
		    session->s_state == CEPH_MDS_SESSION_CLOSING) {
			err = __open_session(mdsc, session);
			if (err)
				goto out_session;
			/* retry the same mds later */
			if (random)
				req->r_resend_mds = mds;
		}
		list_add(&req->r_wait, &session->s_waiting);
		goto out_session;
	}

	/* send request */
	req->r_resend_mds = -1;   /* forget any previous mds hint */

	if (req->r_request_started == 0)   /* note request start time */
		req->r_request_started = jiffies;

	err = __send_request(mdsc, session, req, false);

out_session:
	ceph_put_mds_session(session);
finish:
	if (err) {
		dout("__do_request early error %d\n", err);
		req->r_err = err;
		complete_request(mdsc, req);
		__unregister_request(mdsc, req);
	}
	return;
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
		if (test_bit(CEPH_MDS_R_GOT_UNSAFE, &req->r_req_flags))
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

int ceph_mdsc_submit_request(struct ceph_mds_client *mdsc, struct inode *dir,
			      struct ceph_mds_request *req)
{
	int err = 0;

	/* take CAP_PIN refs for r_inode, r_parent, r_old_dentry */
	if (req->r_inode)
		ceph_get_cap_refs(ceph_inode(req->r_inode), CEPH_CAP_PIN);
	if (req->r_parent) {
		struct ceph_inode_info *ci = ceph_inode(req->r_parent);
		int fmode = (req->r_op & CEPH_MDS_OP_WRITE) ?
			    CEPH_FILE_MODE_WR : CEPH_FILE_MODE_RD;
		spin_lock(&ci->i_ceph_lock);
		ceph_take_cap_refs(ci, CEPH_CAP_PIN, false);
		__ceph_touch_fmode(ci, mdsc, fmode);
		spin_unlock(&ci->i_ceph_lock);
		ihold(req->r_parent);
	}
	if (req->r_old_dentry_dir)
		ceph_get_cap_refs(ceph_inode(req->r_old_dentry_dir),
				  CEPH_CAP_PIN);

	if (req->r_inode) {
		err = ceph_wait_on_async_create(req->r_inode);
		if (err) {
			dout("%s: wait for async create returned: %d\n",
			     __func__, err);
			return err;
		}
	}

	if (!err && req->r_old_inode) {
		err = ceph_wait_on_async_create(req->r_old_inode);
		if (err) {
			dout("%s: wait for async create returned: %d\n",
			     __func__, err);
			return err;
		}
	}

	dout("submit_request on %p for inode %p\n", req, dir);
	mutex_lock(&mdsc->mutex);
	__register_request(mdsc, req, dir);
	__do_request(mdsc, req);
	err = req->r_err;
	mutex_unlock(&mdsc->mutex);
	return err;
}

static int ceph_mdsc_wait_request(struct ceph_mds_client *mdsc,
				  struct ceph_mds_request *req)
{
	int err;

	/* wait */
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
			err = -ETIMEDOUT;  /* timed out */
		else
			err = timeleft;  /* killed */
	}
	dout("do_request waited, got %d\n", err);
	mutex_lock(&mdsc->mutex);

	/* only abort if we didn't race with a real reply */
	if (test_bit(CEPH_MDS_R_GOT_RESULT, &req->r_req_flags)) {
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
		set_bit(CEPH_MDS_R_ABORTED, &req->r_req_flags);
		mutex_unlock(&req->r_fill_mutex);

		if (req->r_parent &&
		    (req->r_op & CEPH_MDS_OP_WRITE))
			ceph_invalidate_dir_request(req);
	} else {
		err = req->r_err;
	}

	mutex_unlock(&mdsc->mutex);
	return err;
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

	/* issue */
	err = ceph_mdsc_submit_request(mdsc, dir, req);
	if (!err)
		err = ceph_mdsc_wait_request(mdsc, req);
	dout("do_request %p done, result %d\n", req, err);
	return err;
}

/*
 * Invalidate dir's completeness, dentry lease state on an aborted MDS
 * namespace request.
 */
void ceph_invalidate_dir_request(struct ceph_mds_request *req)
{
	struct inode *dir = req->r_parent;
	struct inode *old_dir = req->r_old_dentry_dir;

	dout("invalidate_dir_request %p %p (complete, lease(s))\n", dir, old_dir);

	ceph_dir_clear_complete(dir);
	if (old_dir)
		ceph_dir_clear_complete(old_dir);
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
	if ((test_bit(CEPH_MDS_R_GOT_UNSAFE, &req->r_req_flags) && !head->safe) ||
	    (test_bit(CEPH_MDS_R_GOT_SAFE, &req->r_req_flags) && head->safe)) {
		pr_warn("got a dup %s reply on %llu from mds%d\n",
			   head->safe ? "safe" : "unsafe", tid, mds);
		mutex_unlock(&mdsc->mutex);
		goto out;
	}
	if (test_bit(CEPH_MDS_R_GOT_SAFE, &req->r_req_flags)) {
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
		dout("got ESTALE on request %llu\n", req->r_tid);
		req->r_resend_mds = -1;
		if (req->r_direct_mode != USE_AUTH_MDS) {
			dout("not using auth, setting for that now\n");
			req->r_direct_mode = USE_AUTH_MDS;
			__do_request(mdsc, req);
			mutex_unlock(&mdsc->mutex);
			goto out;
		} else  {
			int mds = __choose_mds(mdsc, req, NULL);
			if (mds >= 0 && mds != req->r_session->s_mds) {
				dout("but auth changed, so resending\n");
				__do_request(mdsc, req);
				mutex_unlock(&mdsc->mutex);
				goto out;
			}
		}
		dout("have to return ESTALE on request %llu\n", req->r_tid);
	}


	if (head->safe) {
		set_bit(CEPH_MDS_R_GOT_SAFE, &req->r_req_flags);
		__unregister_request(mdsc, req);

		/* last request during umount? */
		if (mdsc->stopping && !__get_oldest_req(mdsc))
			complete_all(&mdsc->safe_umount_waiters);

		if (test_bit(CEPH_MDS_R_GOT_UNSAFE, &req->r_req_flags)) {
			/*
			 * We already handled the unsafe response, now do the
			 * cleanup.  No need to examine the response; the MDS
			 * doesn't include any result info in the safe
			 * response.  And even if it did, there is nothing
			 * useful we could do with a revised return value.
			 */
			dout("got safe reply %llu, mds%d\n", tid, mds);

			mutex_unlock(&mdsc->mutex);
			goto out;
		}
	} else {
		set_bit(CEPH_MDS_R_GOT_UNSAFE, &req->r_req_flags);
		list_add_tail(&req->r_unsafe_item, &req->r_session->s_unsafe);
	}

	dout("handle_reply tid %lld result %d\n", tid, result);
	rinfo = &req->r_reply_info;
	if (test_bit(CEPHFS_FEATURE_REPLY_ENCODING, &session->s_features))
		err = parse_reply_info(session, msg, rinfo, (u64)-1);
	else
		err = parse_reply_info(session, msg, rinfo, session->s_con.peer_features);
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
	err = ceph_fill_trace(mdsc->fsc->sb, req);
	if (err == 0) {
		if (result == 0 && (req->r_op == CEPH_MDS_OP_READDIR ||
				    req->r_op == CEPH_MDS_OP_LSSNAP))
			ceph_readdir_prepopulate(req, req->r_session);
	}
	current->journal_info = NULL;
	mutex_unlock(&req->r_fill_mutex);

	up_read(&mdsc->snap_rwsem);
	if (realm)
		ceph_put_snap_realm(mdsc, realm);

	if (err == 0) {
		if (req->r_target_inode &&
		    test_bit(CEPH_MDS_R_GOT_UNSAFE, &req->r_req_flags)) {
			struct ceph_inode_info *ci =
				ceph_inode(req->r_target_inode);
			spin_lock(&ci->i_unsafe_lock);
			list_add_tail(&req->r_unsafe_target_item,
				      &ci->i_unsafe_iops);
			spin_unlock(&ci->i_unsafe_lock);
		}

		ceph_unreserve_caps(mdsc, &req->r_caps_reservation);
	}
out_err:
	mutex_lock(&mdsc->mutex);
	if (!test_bit(CEPH_MDS_R_ABORTED, &req->r_req_flags)) {
		if (err) {
			req->r_err = err;
		} else {
			req->r_reply =  ceph_msg_get(msg);
			set_bit(CEPH_MDS_R_GOT_RESULT, &req->r_req_flags);
		}
	} else {
		dout("reply arrived after request %lld was aborted\n", tid);
	}
	mutex_unlock(&mdsc->mutex);

	mutex_unlock(&session->s_mutex);

	/* kick calling process */
	complete_request(mdsc, req);

	ceph_update_metadata_latency(&mdsc->metric, req->r_start_latency,
				     req->r_end_latency, err);
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

	if (test_bit(CEPH_MDS_R_ABORTED, &req->r_req_flags)) {
		dout("forward tid %llu aborted, unregistering\n", tid);
		__unregister_request(mdsc, req);
	} else if (fwd_seq <= req->r_num_fwd) {
		dout("forward tid %llu to mds%d - old seq %d <= %d\n",
		     tid, next_mds, req->r_num_fwd, fwd_seq);
	} else {
		/* resend. forward race not possible; mds would drop */
		dout("forward tid %llu to mds%d (we resend)\n", tid, next_mds);
		BUG_ON(req->r_err);
		BUG_ON(test_bit(CEPH_MDS_R_GOT_RESULT, &req->r_req_flags));
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

static int __decode_session_metadata(void **p, void *end,
				     bool *blocklisted)
{
	/* map<string,string> */
	u32 n;
	bool err_str;
	ceph_decode_32_safe(p, end, n, bad);
	while (n-- > 0) {
		u32 len;
		ceph_decode_32_safe(p, end, len, bad);
		ceph_decode_need(p, end, len, bad);
		err_str = !strncmp(*p, "error_string", len);
		*p += len;
		ceph_decode_32_safe(p, end, len, bad);
		ceph_decode_need(p, end, len, bad);
		/*
		 * Match "blocklisted (blacklisted)" from newer MDSes,
		 * or "blacklisted" from older MDSes.
		 */
		if (err_str && strnstr(*p, "blacklisted", len))
			*blocklisted = true;
		*p += len;
	}
	return 0;
bad:
	return -1;
}

/*
 * handle a mds session control message
 */
static void handle_session(struct ceph_mds_session *session,
			   struct ceph_msg *msg)
{
	struct ceph_mds_client *mdsc = session->s_mdsc;
	int mds = session->s_mds;
	int msg_version = le16_to_cpu(msg->hdr.version);
	void *p = msg->front.iov_base;
	void *end = p + msg->front.iov_len;
	struct ceph_mds_session_head *h;
	u32 op;
	u64 seq, features = 0;
	int wake = 0;
	bool blocklisted = false;

	/* decode */
	ceph_decode_need(&p, end, sizeof(*h), bad);
	h = p;
	p += sizeof(*h);

	op = le32_to_cpu(h->op);
	seq = le64_to_cpu(h->seq);

	if (msg_version >= 3) {
		u32 len;
		/* version >= 2, metadata */
		if (__decode_session_metadata(&p, end, &blocklisted) < 0)
			goto bad;
		/* version >= 3, feature bits */
		ceph_decode_32_safe(&p, end, len, bad);
		if (len) {
			ceph_decode_64_safe(&p, end, features, bad);
			p += len - sizeof(features);
		}
	}

	mutex_lock(&mdsc->mutex);
	if (op == CEPH_SESSION_CLOSE) {
		ceph_get_mds_session(session);
		__unregister_session(mdsc, session);
	}
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
		session->s_features = features;
		renewed_caps(mdsc, session, 0);
		if (test_bit(CEPHFS_FEATURE_METRIC_COLLECT, &session->s_features))
			metric_schedule_delayed(&mdsc->metric);
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
		session->s_state = CEPH_MDS_SESSION_CLOSED;
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
		ceph_trim_caps(mdsc, session, le32_to_cpu(h->max_caps));
		break;

	case CEPH_SESSION_FLUSHMSG:
		send_flushmsg_ack(mdsc, session, seq);
		break;

	case CEPH_SESSION_FORCE_RO:
		dout("force_session_readonly %p\n", session);
		spin_lock(&session->s_cap_lock);
		session->s_readonly = true;
		spin_unlock(&session->s_cap_lock);
		wake_up_session_caps(session, FORCE_RO);
		break;

	case CEPH_SESSION_REJECT:
		WARN_ON(session->s_state != CEPH_MDS_SESSION_OPENING);
		pr_info("mds%d rejected session\n", session->s_mds);
		session->s_state = CEPH_MDS_SESSION_REJECTED;
		cleanup_session_requests(mdsc, session);
		remove_session_caps(session);
		if (blocklisted)
			mdsc->fsc->blocklisted = true;
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
	if (op == CEPH_SESSION_CLOSE)
		ceph_put_mds_session(session);
	return;

bad:
	pr_err("mdsc_handle_session corrupt message mds%d len %d\n", mds,
	       (int)msg->front.iov_len);
	ceph_msg_dump(msg);
	return;
}

void ceph_mdsc_release_dir_caps(struct ceph_mds_request *req)
{
	int dcaps;

	dcaps = xchg(&req->r_dir_caps, 0);
	if (dcaps) {
		dout("releasing r_dir_caps=%s\n", ceph_cap_string(dcaps));
		ceph_put_cap_refs(ceph_inode(req->r_parent), dcaps);
	}
}

void ceph_mdsc_release_dir_caps_no_check(struct ceph_mds_request *req)
{
	int dcaps;

	dcaps = xchg(&req->r_dir_caps, 0);
	if (dcaps) {
		dout("releasing r_dir_caps=%s\n", ceph_cap_string(dcaps));
		ceph_put_cap_refs_no_check_caps(ceph_inode(req->r_parent),
						dcaps);
	}
}

/*
 * called under session->mutex.
 */
static void replay_unsafe_requests(struct ceph_mds_client *mdsc,
				   struct ceph_mds_session *session)
{
	struct ceph_mds_request *req, *nreq;
	struct rb_node *p;

	dout("replay_unsafe_requests mds%d\n", session->s_mds);

	mutex_lock(&mdsc->mutex);
	list_for_each_entry_safe(req, nreq, &session->s_unsafe, r_unsafe_item)
		__send_request(mdsc, session, req, true);

	/*
	 * also re-send old requests when MDS enters reconnect stage. So that MDS
	 * can process completed request in clientreplay stage.
	 */
	p = rb_first(&mdsc->request_tree);
	while (p) {
		req = rb_entry(p, struct ceph_mds_request, r_node);
		p = rb_next(p);
		if (test_bit(CEPH_MDS_R_GOT_UNSAFE, &req->r_req_flags))
			continue;
		if (req->r_attempts == 0)
			continue; /* only old requests */
		if (!req->r_session)
			continue;
		if (req->r_session->s_mds != session->s_mds)
			continue;

		ceph_mdsc_release_dir_caps_no_check(req);

		__send_request(mdsc, session, req, true);
	}
	mutex_unlock(&mdsc->mutex);
}

static int send_reconnect_partial(struct ceph_reconnect_state *recon_state)
{
	struct ceph_msg *reply;
	struct ceph_pagelist *_pagelist;
	struct page *page;
	__le32 *addr;
	int err = -ENOMEM;

	if (!recon_state->allow_multi)
		return -ENOSPC;

	/* can't handle message that contains both caps and realm */
	BUG_ON(!recon_state->nr_caps == !recon_state->nr_realms);

	/* pre-allocate new pagelist */
	_pagelist = ceph_pagelist_alloc(GFP_NOFS);
	if (!_pagelist)
		return -ENOMEM;

	reply = ceph_msg_new2(CEPH_MSG_CLIENT_RECONNECT, 0, 1, GFP_NOFS, false);
	if (!reply)
		goto fail_msg;

	/* placeholder for nr_caps */
	err = ceph_pagelist_encode_32(_pagelist, 0);
	if (err < 0)
		goto fail;

	if (recon_state->nr_caps) {
		/* currently encoding caps */
		err = ceph_pagelist_encode_32(recon_state->pagelist, 0);
		if (err)
			goto fail;
	} else {
		/* placeholder for nr_realms (currently encoding relams) */
		err = ceph_pagelist_encode_32(_pagelist, 0);
		if (err < 0)
			goto fail;
	}

	err = ceph_pagelist_encode_8(recon_state->pagelist, 1);
	if (err)
		goto fail;

	page = list_first_entry(&recon_state->pagelist->head, struct page, lru);
	addr = kmap_atomic(page);
	if (recon_state->nr_caps) {
		/* currently encoding caps */
		*addr = cpu_to_le32(recon_state->nr_caps);
	} else {
		/* currently encoding relams */
		*(addr + 1) = cpu_to_le32(recon_state->nr_realms);
	}
	kunmap_atomic(addr);

	reply->hdr.version = cpu_to_le16(5);
	reply->hdr.compat_version = cpu_to_le16(4);

	reply->hdr.data_len = cpu_to_le32(recon_state->pagelist->length);
	ceph_msg_data_add_pagelist(reply, recon_state->pagelist);

	ceph_con_send(&recon_state->session->s_con, reply);
	ceph_pagelist_release(recon_state->pagelist);

	recon_state->pagelist = _pagelist;
	recon_state->nr_caps = 0;
	recon_state->nr_realms = 0;
	recon_state->msg_version = 5;
	return 0;
fail:
	ceph_msg_put(reply);
fail_msg:
	ceph_pagelist_release(_pagelist);
	return err;
}

static struct dentry* d_find_primary(struct inode *inode)
{
	struct dentry *alias, *dn = NULL;

	if (hlist_empty(&inode->i_dentry))
		return NULL;

	spin_lock(&inode->i_lock);
	if (hlist_empty(&inode->i_dentry))
		goto out_unlock;

	if (S_ISDIR(inode->i_mode)) {
		alias = hlist_entry(inode->i_dentry.first, struct dentry, d_u.d_alias);
		if (!IS_ROOT(alias))
			dn = dget(alias);
		goto out_unlock;
	}

	hlist_for_each_entry(alias, &inode->i_dentry, d_u.d_alias) {
		spin_lock(&alias->d_lock);
		if (!d_unhashed(alias) &&
		    (ceph_dentry(alias)->flags & CEPH_DENTRY_PRIMARY_LINK)) {
			dn = dget_dlock(alias);
		}
		spin_unlock(&alias->d_lock);
		if (dn)
			break;
	}
out_unlock:
	spin_unlock(&inode->i_lock);
	return dn;
}

/*
 * Encode information about a cap for a reconnect with the MDS.
 */
static int reconnect_caps_cb(struct inode *inode, struct ceph_cap *cap,
			  void *arg)
{
	union {
		struct ceph_mds_cap_reconnect v2;
		struct ceph_mds_cap_reconnect_v1 v1;
	} rec;
	struct ceph_inode_info *ci = cap->ci;
	struct ceph_reconnect_state *recon_state = arg;
	struct ceph_pagelist *pagelist = recon_state->pagelist;
	struct dentry *dentry;
	char *path;
	int pathlen = 0, err;
	u64 pathbase;
	u64 snap_follows;

	dout(" adding %p ino %llx.%llx cap %p %lld %s\n",
	     inode, ceph_vinop(inode), cap, cap->cap_id,
	     ceph_cap_string(cap->issued));

	dentry = d_find_primary(inode);
	if (dentry) {
		/* set pathbase to parent dir when msg_version >= 2 */
		path = ceph_mdsc_build_path(dentry, &pathlen, &pathbase,
					    recon_state->msg_version >= 2);
		dput(dentry);
		if (IS_ERR(path)) {
			err = PTR_ERR(path);
			goto out_err;
		}
	} else {
		path = NULL;
		pathbase = 0;
	}

	spin_lock(&ci->i_ceph_lock);
	cap->seq = 0;        /* reset cap seq */
	cap->issue_seq = 0;  /* and issue_seq */
	cap->mseq = 0;       /* and migrate_seq */
	cap->cap_gen = cap->session->s_cap_gen;

	/* These are lost when the session goes away */
	if (S_ISDIR(inode->i_mode)) {
		if (cap->issued & CEPH_CAP_DIR_CREATE) {
			ceph_put_string(rcu_dereference_raw(ci->i_cached_layout.pool_ns));
			memset(&ci->i_cached_layout, 0, sizeof(ci->i_cached_layout));
		}
		cap->issued &= ~CEPH_CAP_ANY_DIR_OPS;
	}

	if (recon_state->msg_version >= 2) {
		rec.v2.cap_id = cpu_to_le64(cap->cap_id);
		rec.v2.wanted = cpu_to_le32(__ceph_caps_wanted(ci));
		rec.v2.issued = cpu_to_le32(cap->issued);
		rec.v2.snaprealm = cpu_to_le64(ci->i_snap_realm->ino);
		rec.v2.pathbase = cpu_to_le64(pathbase);
		rec.v2.flock_len = (__force __le32)
			((ci->i_ceph_flags & CEPH_I_ERROR_FILELOCK) ? 0 : 1);
	} else {
		rec.v1.cap_id = cpu_to_le64(cap->cap_id);
		rec.v1.wanted = cpu_to_le32(__ceph_caps_wanted(ci));
		rec.v1.issued = cpu_to_le32(cap->issued);
		rec.v1.size = cpu_to_le64(inode->i_size);
		ceph_encode_timespec64(&rec.v1.mtime, &inode->i_mtime);
		ceph_encode_timespec64(&rec.v1.atime, &inode->i_atime);
		rec.v1.snaprealm = cpu_to_le64(ci->i_snap_realm->ino);
		rec.v1.pathbase = cpu_to_le64(pathbase);
	}

	if (list_empty(&ci->i_cap_snaps)) {
		snap_follows = ci->i_head_snapc ? ci->i_head_snapc->seq : 0;
	} else {
		struct ceph_cap_snap *capsnap =
			list_first_entry(&ci->i_cap_snaps,
					 struct ceph_cap_snap, ci_item);
		snap_follows = capsnap->follows;
	}
	spin_unlock(&ci->i_ceph_lock);

	if (recon_state->msg_version >= 2) {
		int num_fcntl_locks, num_flock_locks;
		struct ceph_filelock *flocks = NULL;
		size_t struct_len, total_len = sizeof(u64);
		u8 struct_v = 0;

encode_again:
		if (rec.v2.flock_len) {
			ceph_count_locks(inode, &num_fcntl_locks, &num_flock_locks);
		} else {
			num_fcntl_locks = 0;
			num_flock_locks = 0;
		}
		if (num_fcntl_locks + num_flock_locks > 0) {
			flocks = kmalloc_array(num_fcntl_locks + num_flock_locks,
					       sizeof(struct ceph_filelock),
					       GFP_NOFS);
			if (!flocks) {
				err = -ENOMEM;
				goto out_err;
			}
			err = ceph_encode_locks_to_buffer(inode, flocks,
							  num_fcntl_locks,
							  num_flock_locks);
			if (err) {
				kfree(flocks);
				flocks = NULL;
				if (err == -ENOSPC)
					goto encode_again;
				goto out_err;
			}
		} else {
			kfree(flocks);
			flocks = NULL;
		}

		if (recon_state->msg_version >= 3) {
			/* version, compat_version and struct_len */
			total_len += 2 * sizeof(u8) + sizeof(u32);
			struct_v = 2;
		}
		/*
		 * number of encoded locks is stable, so copy to pagelist
		 */
		struct_len = 2 * sizeof(u32) +
			    (num_fcntl_locks + num_flock_locks) *
			    sizeof(struct ceph_filelock);
		rec.v2.flock_len = cpu_to_le32(struct_len);

		struct_len += sizeof(u32) + pathlen + sizeof(rec.v2);

		if (struct_v >= 2)
			struct_len += sizeof(u64); /* snap_follows */

		total_len += struct_len;

		if (pagelist->length + total_len > RECONNECT_MAX_SIZE) {
			err = send_reconnect_partial(recon_state);
			if (err)
				goto out_freeflocks;
			pagelist = recon_state->pagelist;
		}

		err = ceph_pagelist_reserve(pagelist, total_len);
		if (err)
			goto out_freeflocks;

		ceph_pagelist_encode_64(pagelist, ceph_ino(inode));
		if (recon_state->msg_version >= 3) {
			ceph_pagelist_encode_8(pagelist, struct_v);
			ceph_pagelist_encode_8(pagelist, 1);
			ceph_pagelist_encode_32(pagelist, struct_len);
		}
		ceph_pagelist_encode_string(pagelist, path, pathlen);
		ceph_pagelist_append(pagelist, &rec, sizeof(rec.v2));
		ceph_locks_to_pagelist(flocks, pagelist,
				       num_fcntl_locks, num_flock_locks);
		if (struct_v >= 2)
			ceph_pagelist_encode_64(pagelist, snap_follows);
out_freeflocks:
		kfree(flocks);
	} else {
		err = ceph_pagelist_reserve(pagelist,
					    sizeof(u64) + sizeof(u32) +
					    pathlen + sizeof(rec.v1));
		if (err)
			goto out_err;

		ceph_pagelist_encode_64(pagelist, ceph_ino(inode));
		ceph_pagelist_encode_string(pagelist, path, pathlen);
		ceph_pagelist_append(pagelist, &rec, sizeof(rec.v1));
	}

out_err:
	ceph_mdsc_free_path(path, pathlen);
	if (!err)
		recon_state->nr_caps++;
	return err;
}

static int encode_snap_realms(struct ceph_mds_client *mdsc,
			      struct ceph_reconnect_state *recon_state)
{
	struct rb_node *p;
	struct ceph_pagelist *pagelist = recon_state->pagelist;
	int err = 0;

	if (recon_state->msg_version >= 4) {
		err = ceph_pagelist_encode_32(pagelist, mdsc->num_snap_realms);
		if (err < 0)
			goto fail;
	}

	/*
	 * snaprealms.  we provide mds with the ino, seq (version), and
	 * parent for all of our realms.  If the mds has any newer info,
	 * it will tell us.
	 */
	for (p = rb_first(&mdsc->snap_realms); p; p = rb_next(p)) {
		struct ceph_snap_realm *realm =
		       rb_entry(p, struct ceph_snap_realm, node);
		struct ceph_mds_snaprealm_reconnect sr_rec;

		if (recon_state->msg_version >= 4) {
			size_t need = sizeof(u8) * 2 + sizeof(u32) +
				      sizeof(sr_rec);

			if (pagelist->length + need > RECONNECT_MAX_SIZE) {
				err = send_reconnect_partial(recon_state);
				if (err)
					goto fail;
				pagelist = recon_state->pagelist;
			}

			err = ceph_pagelist_reserve(pagelist, need);
			if (err)
				goto fail;

			ceph_pagelist_encode_8(pagelist, 1);
			ceph_pagelist_encode_8(pagelist, 1);
			ceph_pagelist_encode_32(pagelist, sizeof(sr_rec));
		}

		dout(" adding snap realm %llx seq %lld parent %llx\n",
		     realm->ino, realm->seq, realm->parent_ino);
		sr_rec.ino = cpu_to_le64(realm->ino);
		sr_rec.seq = cpu_to_le64(realm->seq);
		sr_rec.parent = cpu_to_le64(realm->parent_ino);

		err = ceph_pagelist_append(pagelist, &sr_rec, sizeof(sr_rec));
		if (err)
			goto fail;

		recon_state->nr_realms++;
	}
fail:
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
 */
static void send_mds_reconnect(struct ceph_mds_client *mdsc,
			       struct ceph_mds_session *session)
{
	struct ceph_msg *reply;
	int mds = session->s_mds;
	int err = -ENOMEM;
	struct ceph_reconnect_state recon_state = {
		.session = session,
	};
	LIST_HEAD(dispose);

	pr_info("mds%d reconnect start\n", mds);

	recon_state.pagelist = ceph_pagelist_alloc(GFP_NOFS);
	if (!recon_state.pagelist)
		goto fail_nopagelist;

	reply = ceph_msg_new2(CEPH_MSG_CLIENT_RECONNECT, 0, 1, GFP_NOFS, false);
	if (!reply)
		goto fail_nomsg;

	xa_destroy(&session->s_delegated_inos);

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
	detach_cap_releases(session, &dispose);
	spin_unlock(&session->s_cap_lock);
	dispose_cap_releases(mdsc, &dispose);

	/* trim unused caps to reduce MDS's cache rejoin time */
	if (mdsc->fsc->sb->s_root)
		shrink_dcache_parent(mdsc->fsc->sb->s_root);

	ceph_con_close(&session->s_con);
	ceph_con_open(&session->s_con,
		      CEPH_ENTITY_TYPE_MDS, mds,
		      ceph_mdsmap_get_addr(mdsc->mdsmap, mds));

	/* replay unsafe requests */
	replay_unsafe_requests(mdsc, session);

	ceph_early_kick_flushing_caps(mdsc, session);

	down_read(&mdsc->snap_rwsem);

	/* placeholder for nr_caps */
	err = ceph_pagelist_encode_32(recon_state.pagelist, 0);
	if (err)
		goto fail;

	if (test_bit(CEPHFS_FEATURE_MULTI_RECONNECT, &session->s_features)) {
		recon_state.msg_version = 3;
		recon_state.allow_multi = true;
	} else if (session->s_con.peer_features & CEPH_FEATURE_MDSENC) {
		recon_state.msg_version = 3;
	} else {
		recon_state.msg_version = 2;
	}
	/* trsaverse this session's caps */
	err = ceph_iterate_session_caps(session, reconnect_caps_cb, &recon_state);

	spin_lock(&session->s_cap_lock);
	session->s_cap_reconnect = 0;
	spin_unlock(&session->s_cap_lock);

	if (err < 0)
		goto fail;

	/* check if all realms can be encoded into current message */
	if (mdsc->num_snap_realms) {
		size_t total_len =
			recon_state.pagelist->length +
			mdsc->num_snap_realms *
			sizeof(struct ceph_mds_snaprealm_reconnect);
		if (recon_state.msg_version >= 4) {
			/* number of realms */
			total_len += sizeof(u32);
			/* version, compat_version and struct_len */
			total_len += mdsc->num_snap_realms *
				     (2 * sizeof(u8) + sizeof(u32));
		}
		if (total_len > RECONNECT_MAX_SIZE) {
			if (!recon_state.allow_multi) {
				err = -ENOSPC;
				goto fail;
			}
			if (recon_state.nr_caps) {
				err = send_reconnect_partial(&recon_state);
				if (err)
					goto fail;
			}
			recon_state.msg_version = 5;
		}
	}

	err = encode_snap_realms(mdsc, &recon_state);
	if (err < 0)
		goto fail;

	if (recon_state.msg_version >= 5) {
		err = ceph_pagelist_encode_8(recon_state.pagelist, 0);
		if (err < 0)
			goto fail;
	}

	if (recon_state.nr_caps || recon_state.nr_realms) {
		struct page *page =
			list_first_entry(&recon_state.pagelist->head,
					struct page, lru);
		__le32 *addr = kmap_atomic(page);
		if (recon_state.nr_caps) {
			WARN_ON(recon_state.nr_realms != mdsc->num_snap_realms);
			*addr = cpu_to_le32(recon_state.nr_caps);
		} else if (recon_state.msg_version >= 4) {
			*(addr + 1) = cpu_to_le32(recon_state.nr_realms);
		}
		kunmap_atomic(addr);
	}

	reply->hdr.version = cpu_to_le16(recon_state.msg_version);
	if (recon_state.msg_version >= 4)
		reply->hdr.compat_version = cpu_to_le16(4);

	reply->hdr.data_len = cpu_to_le32(recon_state.pagelist->length);
	ceph_msg_data_add_pagelist(reply, recon_state.pagelist);

	ceph_con_send(&session->s_con, reply);

	mutex_unlock(&session->s_mutex);

	mutex_lock(&mdsc->mutex);
	__wake_requests(mdsc, &session->s_waiting);
	mutex_unlock(&mdsc->mutex);

	up_read(&mdsc->snap_rwsem);
	ceph_pagelist_release(recon_state.pagelist);
	return;

fail:
	ceph_msg_put(reply);
	up_read(&mdsc->snap_rwsem);
	mutex_unlock(&session->s_mutex);
fail_nomsg:
	ceph_pagelist_release(recon_state.pagelist);
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

	for (i = 0; i < oldmap->possible_max_rank && i < mdsc->max_sessions; i++) {
		if (!mdsc->sessions[i])
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

		if (i >= newmap->possible_max_rank) {
			/* force close session for stopped mds */
			ceph_get_mds_session(s);
			__unregister_session(mdsc, s);
			__wake_requests(mdsc, &s->s_waiting);
			mutex_unlock(&mdsc->mutex);

			mutex_lock(&s->s_mutex);
			cleanup_session_requests(mdsc, s);
			remove_session_caps(s);
			mutex_unlock(&s->s_mutex);

			ceph_put_mds_session(s);

			mutex_lock(&mdsc->mutex);
			kick_requests(mdsc, i);
			continue;
		}

		if (memcmp(ceph_mdsmap_get_addr(oldmap, i),
			   ceph_mdsmap_get_addr(newmap, i),
			   sizeof(struct ceph_entity_addr))) {
			/* just close it */
			mutex_unlock(&mdsc->mutex);
			mutex_lock(&s->s_mutex);
			mutex_lock(&mdsc->mutex);
			ceph_con_close(&s->s_con);
			mutex_unlock(&s->s_mutex);
			s->s_state = CEPH_MDS_SESSION_RESTARTING;
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
			mutex_unlock(&mdsc->mutex);
			mutex_lock(&s->s_mutex);
			mutex_lock(&mdsc->mutex);
			ceph_kick_flushing_caps(mdsc, s);
			mutex_unlock(&s->s_mutex);
			wake_up_session_caps(s, RECONNECT);
		}
	}

	for (i = 0; i < newmap->possible_max_rank && i < mdsc->max_sessions; i++) {
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
	dname.len = get_unaligned_le32(h + 1);
	if (msg->front.iov_len < sizeof(*h) + sizeof(u32) + dname.len)
		goto bad;
	dname.name = (void *)(h + 1) + sizeof(u32);

	/* lookup inode */
	inode = ceph_find_inode(sb, vino);
	dout("handle_lease %s, ino %llx %p %.*s\n",
	     ceph_lease_op_name(h->action), vino.ino, inode,
	     dname.len, dname.name);

	mutex_lock(&session->s_mutex);
	inc_session_sequence(session);

	if (!inode) {
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
	mutex_unlock(&session->s_mutex);
	/* avoid calling iput_final() in mds dispatch threads */
	ceph_async_iput(inode);
	return;

bad:
	pr_err("corrupt lease message\n");
	ceph_msg_dump(msg);
}

void ceph_mdsc_lease_send_msg(struct ceph_mds_session *session,
			      struct dentry *dentry, char action,
			      u32 seq)
{
	struct ceph_msg *msg;
	struct ceph_mds_lease *lease;
	struct inode *dir;
	int len = sizeof(*lease) + sizeof(u32) + NAME_MAX;

	dout("lease_send_msg identry %p %s to mds%d\n",
	     dentry, ceph_lease_op_name(action), session->s_mds);

	msg = ceph_msg_new(CEPH_MSG_CLIENT_LEASE, len, GFP_NOFS, false);
	if (!msg)
		return;
	lease = msg->front.iov_base;
	lease->action = action;
	lease->seq = cpu_to_le32(seq);

	spin_lock(&dentry->d_lock);
	dir = d_inode(dentry->d_parent);
	lease->ino = cpu_to_le64(ceph_ino(dir));
	lease->first = lease->last = cpu_to_le64(ceph_snap(dir));

	put_unaligned_le32(dentry->d_name.len, lease + 1);
	memcpy((void *)(lease + 1) + 4,
	       dentry->d_name.name, dentry->d_name.len);
	spin_unlock(&dentry->d_lock);
	/*
	 * if this is a preemptive lease RELEASE, no need to
	 * flush request stream, since the actual request will
	 * soon follow.
	 */
	msg->more_to_follow = (action == CEPH_MDS_LEASE_RELEASE);

	ceph_con_send(&session->s_con, msg);
}

/*
 * lock unlock the session, to wait ongoing session activities
 */
static void lock_unlock_session(struct ceph_mds_session *s)
{
	mutex_lock(&s->s_mutex);
	mutex_unlock(&s->s_mutex);
}

static void maybe_recover_session(struct ceph_mds_client *mdsc)
{
	struct ceph_fs_client *fsc = mdsc->fsc;

	if (!ceph_test_mount_opt(fsc, CLEANRECOVER))
		return;

	if (READ_ONCE(fsc->mount_state) != CEPH_MOUNT_MOUNTED)
		return;

	if (!READ_ONCE(fsc->blocklisted))
		return;

	if (fsc->last_auto_reconnect &&
	    time_before(jiffies, fsc->last_auto_reconnect + HZ * 60 * 30))
		return;

	pr_info("auto reconnect after blocklisted\n");
	fsc->last_auto_reconnect = jiffies;
	ceph_force_reconnect(fsc->sb);
}

bool check_session_state(struct ceph_mds_session *s)
{
	switch (s->s_state) {
	case CEPH_MDS_SESSION_OPEN:
		if (s->s_ttl && time_after(jiffies, s->s_ttl)) {
			s->s_state = CEPH_MDS_SESSION_HUNG;
			pr_info("mds%d hung\n", s->s_mds);
		}
		break;
	case CEPH_MDS_SESSION_CLOSING:
		/* Should never reach this when we're unmounting */
		WARN_ON_ONCE(s->s_ttl);
		fallthrough;
	case CEPH_MDS_SESSION_NEW:
	case CEPH_MDS_SESSION_RESTARTING:
	case CEPH_MDS_SESSION_CLOSED:
	case CEPH_MDS_SESSION_REJECTED:
		return false;
	}

	return true;
}

/*
 * If the sequence is incremented while we're waiting on a REQUEST_CLOSE reply,
 * then we need to retransmit that request.
 */
void inc_session_sequence(struct ceph_mds_session *s)
{
	lockdep_assert_held(&s->s_mutex);

	s->s_seq++;

	if (s->s_state == CEPH_MDS_SESSION_CLOSING) {
		int ret;

		dout("resending session close request for mds%d\n", s->s_mds);
		ret = request_close_session(s);
		if (ret < 0)
			pr_err("unable to close session to mds%d: %d\n",
			       s->s_mds, ret);
	}
}

/*
 * delayed work -- periodically trim expired leases, renew caps with mds.  If
 * the @delay parameter is set to 0 or if it's more than 5 secs, the default
 * workqueue delay value of 5 secs will be used.
 */
static void schedule_delayed(struct ceph_mds_client *mdsc, unsigned long delay)
{
	unsigned long max_delay = HZ * 5;

	/* 5 secs default delay */
	if (!delay || (delay > max_delay))
		delay = max_delay;
	schedule_delayed_work(&mdsc->delayed_work,
			      round_jiffies_relative(delay));
}

static void delayed_work(struct work_struct *work)
{
	struct ceph_mds_client *mdsc =
		container_of(work, struct ceph_mds_client, delayed_work.work);
	unsigned long delay;
	int renew_interval;
	int renew_caps;
	int i;

	dout("mdsc delayed_work\n");

	if (mdsc->stopping)
		return;

	mutex_lock(&mdsc->mutex);
	renew_interval = mdsc->mdsmap->m_session_timeout >> 2;
	renew_caps = time_after_eq(jiffies, HZ*renew_interval +
				   mdsc->last_renew_caps);
	if (renew_caps)
		mdsc->last_renew_caps = jiffies;

	for (i = 0; i < mdsc->max_sessions; i++) {
		struct ceph_mds_session *s = __ceph_lookup_mds_session(mdsc, i);
		if (!s)
			continue;

		if (!check_session_state(s)) {
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

	delay = ceph_check_delayed_caps(mdsc);

	ceph_queue_cap_reclaim_work(mdsc);

	ceph_trim_snapid_map(mdsc);

	maybe_recover_session(mdsc);

	schedule_delayed(mdsc, delay);
}

int ceph_mdsc_init(struct ceph_fs_client *fsc)

{
	struct ceph_mds_client *mdsc;
	int err;

	mdsc = kzalloc(sizeof(struct ceph_mds_client), GFP_NOFS);
	if (!mdsc)
		return -ENOMEM;
	mdsc->fsc = fsc;
	mutex_init(&mdsc->mutex);
	mdsc->mdsmap = kzalloc(sizeof(*mdsc->mdsmap), GFP_NOFS);
	if (!mdsc->mdsmap) {
		err = -ENOMEM;
		goto err_mdsc;
	}

	init_completion(&mdsc->safe_umount_waiters);
	init_waitqueue_head(&mdsc->session_close_wq);
	INIT_LIST_HEAD(&mdsc->waiting_for_map);
	mdsc->sessions = NULL;
	atomic_set(&mdsc->num_sessions, 0);
	mdsc->max_sessions = 0;
	mdsc->stopping = 0;
	atomic64_set(&mdsc->quotarealms_count, 0);
	mdsc->quotarealms_inodes = RB_ROOT;
	mutex_init(&mdsc->quotarealms_inodes_mutex);
	mdsc->last_snap_seq = 0;
	init_rwsem(&mdsc->snap_rwsem);
	mdsc->snap_realms = RB_ROOT;
	INIT_LIST_HEAD(&mdsc->snap_empty);
	mdsc->num_snap_realms = 0;
	spin_lock_init(&mdsc->snap_empty_lock);
	mdsc->last_tid = 0;
	mdsc->oldest_tid = 0;
	mdsc->request_tree = RB_ROOT;
	INIT_DELAYED_WORK(&mdsc->delayed_work, delayed_work);
	mdsc->last_renew_caps = jiffies;
	INIT_LIST_HEAD(&mdsc->cap_delay_list);
	INIT_LIST_HEAD(&mdsc->cap_wait_list);
	spin_lock_init(&mdsc->cap_delay_lock);
	INIT_LIST_HEAD(&mdsc->snap_flush_list);
	spin_lock_init(&mdsc->snap_flush_lock);
	mdsc->last_cap_flush_tid = 1;
	INIT_LIST_HEAD(&mdsc->cap_flush_list);
	INIT_LIST_HEAD(&mdsc->cap_dirty_migrating);
	mdsc->num_cap_flushing = 0;
	spin_lock_init(&mdsc->cap_dirty_lock);
	init_waitqueue_head(&mdsc->cap_flushing_wq);
	INIT_WORK(&mdsc->cap_reclaim_work, ceph_cap_reclaim_work);
	atomic_set(&mdsc->cap_reclaim_pending, 0);
	err = ceph_metric_init(&mdsc->metric);
	if (err)
		goto err_mdsmap;

	spin_lock_init(&mdsc->dentry_list_lock);
	INIT_LIST_HEAD(&mdsc->dentry_leases);
	INIT_LIST_HEAD(&mdsc->dentry_dir_leases);

	ceph_caps_init(mdsc);
	ceph_adjust_caps_max_min(mdsc, fsc->mount_options);

	spin_lock_init(&mdsc->snapid_map_lock);
	mdsc->snapid_map_tree = RB_ROOT;
	INIT_LIST_HEAD(&mdsc->snapid_map_lru);

	init_rwsem(&mdsc->pool_perm_rwsem);
	mdsc->pool_perm_tree = RB_ROOT;

	strscpy(mdsc->nodename, utsname()->nodename,
		sizeof(mdsc->nodename));

	fsc->mdsc = mdsc;
	return 0;

err_mdsmap:
	kfree(mdsc->mdsmap);
err_mdsc:
	kfree(mdsc);
	return err;
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
			list_del_init(&req->r_wait);
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

	ceph_mdsc_iterate_sessions(mdsc, lock_unlock_session, false);
	ceph_flush_dirty_caps(mdsc);
	wait_requests(mdsc);

	/*
	 * wait for reply handlers to drop their request refs and
	 * their inode/dcache refs
	 */
	ceph_msgr_flush();

	ceph_cleanup_quotarealms_inodes(mdsc);
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

	if (READ_ONCE(mdsc->fsc->mount_state) == CEPH_MOUNT_SHUTDOWN)
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
	if (READ_ONCE(mdsc->fsc->mount_state) == CEPH_MOUNT_SHUTDOWN)
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
			session = ceph_get_mds_session(mdsc->sessions[i]);
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

	ceph_cleanup_snapid_map(mdsc);
	ceph_cleanup_empty_realms(mdsc);

	cancel_work_sync(&mdsc->cap_reclaim_work);
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

		if (session->s_state == CEPH_MDS_SESSION_REJECTED)
			__unregister_session(mdsc, session);
		__wake_requests(mdsc, &session->s_waiting);
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
	/*
	 * Make sure the delayed work stopped before releasing
	 * the resources.
	 *
	 * Because the cancel_delayed_work_sync() will only
	 * guarantee that the work finishes executing. But the
	 * delayed work will re-arm itself again after that.
	 */
	flush_delayed_work(&mdsc->delayed_work);

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

	if (!mdsc)
		return;

	/* flush out any connection work with references to us */
	ceph_msgr_flush();

	ceph_mdsc_stop(mdsc);

	ceph_metric_destroy(&mdsc->metric);

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
	mdsc->mdsmap_err = err;
	__wake_requests(mdsc, &mdsc->waiting_for_map);
	mutex_unlock(&mdsc->mutex);
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
	mdsc->fsc->max_file_size = min((loff_t)mdsc->mdsmap->m_max_file_size,
					MAX_LFS_FILESIZE);

	__wake_requests(mdsc, &mdsc->waiting_for_map);
	ceph_monc_got_map(&mdsc->fsc->client->monc, CEPH_SUB_MDSMAP,
			  mdsc->mdsmap->m_epoch);

	mutex_unlock(&mdsc->mutex);
	schedule_delayed(mdsc, 0);
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

	if (ceph_get_mds_session(s))
		return con;
	return NULL;
}

static void con_put(struct ceph_connection *con)
{
	struct ceph_mds_session *s = con->private;

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
	case CEPH_MSG_CLIENT_QUOTA:
		ceph_handle_quota(mdsc, s, msg);
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

static int add_authorizer_challenge(struct ceph_connection *con,
				    void *challenge_buf, int challenge_buf_len)
{
	struct ceph_mds_session *s = con->private;
	struct ceph_mds_client *mdsc = s->s_mdsc;
	struct ceph_auth_client *ac = mdsc->fsc->client->monc.auth;

	return ceph_auth_add_authorizer_challenge(ac, s->s_auth.authorizer,
					    challenge_buf, challenge_buf_len);
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
	.add_authorizer_challenge = add_authorizer_challenge,
	.verify_authorizer_reply = verify_authorizer_reply,
	.invalidate_authorizer = invalidate_authorizer,
	.peer_reset = peer_reset,
	.alloc_msg = mds_alloc_msg,
	.sign_message = mds_sign_message,
	.check_message_signature = mds_check_message_signature,
};

/* eof */
