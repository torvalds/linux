// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/writeback.h>
#include <linux/iversion.h>

#include "super.h"
#include "mds_client.h"
#include "cache.h"
#include <linux/ceph/decode.h>
#include <linux/ceph/messenger.h>

/*
 * Capability management
 *
 * The Ceph metadata servers control client access to inode metadata
 * and file data by issuing capabilities, granting clients permission
 * to read and/or write both inode field and file data to OSDs
 * (storage nodes).  Each capability consists of a set of bits
 * indicating which operations are allowed.
 *
 * If the client holds a *_SHARED cap, the client has a coherent value
 * that can be safely read from the cached inode.
 *
 * In the case of a *_EXCL (exclusive) or FILE_WR capabilities, the
 * client is allowed to change inode attributes (e.g., file size,
 * mtime), note its dirty state in the ceph_cap, and asynchronously
 * flush that metadata change to the MDS.
 *
 * In the event of a conflicting operation (perhaps by another
 * client), the MDS will revoke the conflicting client capabilities.
 *
 * In order for a client to cache an inode, it must hold a capability
 * with at least one MDS server.  When inodes are released, release
 * notifications are batched and periodically sent en masse to the MDS
 * cluster to release server state.
 */

static u64 __get_oldest_flush_tid(struct ceph_mds_client *mdsc);
static void __kick_flushing_caps(struct ceph_mds_client *mdsc,
				 struct ceph_mds_session *session,
				 struct ceph_inode_info *ci,
				 u64 oldest_flush_tid);

/*
 * Generate readable cap strings for debugging output.
 */
#define MAX_CAP_STR 20
static char cap_str[MAX_CAP_STR][40];
static DEFINE_SPINLOCK(cap_str_lock);
static int last_cap_str;

static char *gcap_string(char *s, int c)
{
	if (c & CEPH_CAP_GSHARED)
		*s++ = 's';
	if (c & CEPH_CAP_GEXCL)
		*s++ = 'x';
	if (c & CEPH_CAP_GCACHE)
		*s++ = 'c';
	if (c & CEPH_CAP_GRD)
		*s++ = 'r';
	if (c & CEPH_CAP_GWR)
		*s++ = 'w';
	if (c & CEPH_CAP_GBUFFER)
		*s++ = 'b';
	if (c & CEPH_CAP_GWREXTEND)
		*s++ = 'a';
	if (c & CEPH_CAP_GLAZYIO)
		*s++ = 'l';
	return s;
}

const char *ceph_cap_string(int caps)
{
	int i;
	char *s;
	int c;

	spin_lock(&cap_str_lock);
	i = last_cap_str++;
	if (last_cap_str == MAX_CAP_STR)
		last_cap_str = 0;
	spin_unlock(&cap_str_lock);

	s = cap_str[i];

	if (caps & CEPH_CAP_PIN)
		*s++ = 'p';

	c = (caps >> CEPH_CAP_SAUTH) & 3;
	if (c) {
		*s++ = 'A';
		s = gcap_string(s, c);
	}

	c = (caps >> CEPH_CAP_SLINK) & 3;
	if (c) {
		*s++ = 'L';
		s = gcap_string(s, c);
	}

	c = (caps >> CEPH_CAP_SXATTR) & 3;
	if (c) {
		*s++ = 'X';
		s = gcap_string(s, c);
	}

	c = caps >> CEPH_CAP_SFILE;
	if (c) {
		*s++ = 'F';
		s = gcap_string(s, c);
	}

	if (s == cap_str[i])
		*s++ = '-';
	*s = 0;
	return cap_str[i];
}

void ceph_caps_init(struct ceph_mds_client *mdsc)
{
	INIT_LIST_HEAD(&mdsc->caps_list);
	spin_lock_init(&mdsc->caps_list_lock);
}

void ceph_caps_finalize(struct ceph_mds_client *mdsc)
{
	struct ceph_cap *cap;

	spin_lock(&mdsc->caps_list_lock);
	while (!list_empty(&mdsc->caps_list)) {
		cap = list_first_entry(&mdsc->caps_list,
				       struct ceph_cap, caps_item);
		list_del(&cap->caps_item);
		kmem_cache_free(ceph_cap_cachep, cap);
	}
	mdsc->caps_total_count = 0;
	mdsc->caps_avail_count = 0;
	mdsc->caps_use_count = 0;
	mdsc->caps_reserve_count = 0;
	mdsc->caps_min_count = 0;
	spin_unlock(&mdsc->caps_list_lock);
}

void ceph_adjust_caps_max_min(struct ceph_mds_client *mdsc,
			      struct ceph_mount_options *fsopt)
{
	spin_lock(&mdsc->caps_list_lock);
	mdsc->caps_min_count = fsopt->max_readdir;
	if (mdsc->caps_min_count < 1024)
		mdsc->caps_min_count = 1024;
	mdsc->caps_use_max = fsopt->caps_max;
	if (mdsc->caps_use_max > 0 &&
	    mdsc->caps_use_max < mdsc->caps_min_count)
		mdsc->caps_use_max = mdsc->caps_min_count;
	spin_unlock(&mdsc->caps_list_lock);
}

static void __ceph_unreserve_caps(struct ceph_mds_client *mdsc, int nr_caps)
{
	struct ceph_cap *cap;
	int i;

	if (nr_caps) {
		BUG_ON(mdsc->caps_reserve_count < nr_caps);
		mdsc->caps_reserve_count -= nr_caps;
		if (mdsc->caps_avail_count >=
		    mdsc->caps_reserve_count + mdsc->caps_min_count) {
			mdsc->caps_total_count -= nr_caps;
			for (i = 0; i < nr_caps; i++) {
				cap = list_first_entry(&mdsc->caps_list,
					struct ceph_cap, caps_item);
				list_del(&cap->caps_item);
				kmem_cache_free(ceph_cap_cachep, cap);
			}
		} else {
			mdsc->caps_avail_count += nr_caps;
		}

		dout("%s: caps %d = %d used + %d resv + %d avail\n",
		     __func__,
		     mdsc->caps_total_count, mdsc->caps_use_count,
		     mdsc->caps_reserve_count, mdsc->caps_avail_count);
		BUG_ON(mdsc->caps_total_count != mdsc->caps_use_count +
						 mdsc->caps_reserve_count +
						 mdsc->caps_avail_count);
	}
}

/*
 * Called under mdsc->mutex.
 */
int ceph_reserve_caps(struct ceph_mds_client *mdsc,
		      struct ceph_cap_reservation *ctx, int need)
{
	int i, j;
	struct ceph_cap *cap;
	int have;
	int alloc = 0;
	int max_caps;
	int err = 0;
	bool trimmed = false;
	struct ceph_mds_session *s;
	LIST_HEAD(newcaps);

	dout("reserve caps ctx=%p need=%d\n", ctx, need);

	/* first reserve any caps that are already allocated */
	spin_lock(&mdsc->caps_list_lock);
	if (mdsc->caps_avail_count >= need)
		have = need;
	else
		have = mdsc->caps_avail_count;
	mdsc->caps_avail_count -= have;
	mdsc->caps_reserve_count += have;
	BUG_ON(mdsc->caps_total_count != mdsc->caps_use_count +
					 mdsc->caps_reserve_count +
					 mdsc->caps_avail_count);
	spin_unlock(&mdsc->caps_list_lock);

	for (i = have; i < need; ) {
		cap = kmem_cache_alloc(ceph_cap_cachep, GFP_NOFS);
		if (cap) {
			list_add(&cap->caps_item, &newcaps);
			alloc++;
			i++;
			continue;
		}

		if (!trimmed) {
			for (j = 0; j < mdsc->max_sessions; j++) {
				s = __ceph_lookup_mds_session(mdsc, j);
				if (!s)
					continue;
				mutex_unlock(&mdsc->mutex);

				mutex_lock(&s->s_mutex);
				max_caps = s->s_nr_caps - (need - i);
				ceph_trim_caps(mdsc, s, max_caps);
				mutex_unlock(&s->s_mutex);

				ceph_put_mds_session(s);
				mutex_lock(&mdsc->mutex);
			}
			trimmed = true;

			spin_lock(&mdsc->caps_list_lock);
			if (mdsc->caps_avail_count) {
				int more_have;
				if (mdsc->caps_avail_count >= need - i)
					more_have = need - i;
				else
					more_have = mdsc->caps_avail_count;

				i += more_have;
				have += more_have;
				mdsc->caps_avail_count -= more_have;
				mdsc->caps_reserve_count += more_have;

			}
			spin_unlock(&mdsc->caps_list_lock);

			continue;
		}

		pr_warn("reserve caps ctx=%p ENOMEM need=%d got=%d\n",
			ctx, need, have + alloc);
		err = -ENOMEM;
		break;
	}

	if (!err) {
		BUG_ON(have + alloc != need);
		ctx->count = need;
		ctx->used = 0;
	}

	spin_lock(&mdsc->caps_list_lock);
	mdsc->caps_total_count += alloc;
	mdsc->caps_reserve_count += alloc;
	list_splice(&newcaps, &mdsc->caps_list);

	BUG_ON(mdsc->caps_total_count != mdsc->caps_use_count +
					 mdsc->caps_reserve_count +
					 mdsc->caps_avail_count);

	if (err)
		__ceph_unreserve_caps(mdsc, have + alloc);

	spin_unlock(&mdsc->caps_list_lock);

	dout("reserve caps ctx=%p %d = %d used + %d resv + %d avail\n",
	     ctx, mdsc->caps_total_count, mdsc->caps_use_count,
	     mdsc->caps_reserve_count, mdsc->caps_avail_count);
	return err;
}

void ceph_unreserve_caps(struct ceph_mds_client *mdsc,
			 struct ceph_cap_reservation *ctx)
{
	bool reclaim = false;
	if (!ctx->count)
		return;

	dout("unreserve caps ctx=%p count=%d\n", ctx, ctx->count);
	spin_lock(&mdsc->caps_list_lock);
	__ceph_unreserve_caps(mdsc, ctx->count);
	ctx->count = 0;

	if (mdsc->caps_use_max > 0 &&
	    mdsc->caps_use_count > mdsc->caps_use_max)
		reclaim = true;
	spin_unlock(&mdsc->caps_list_lock);

	if (reclaim)
		ceph_reclaim_caps_nr(mdsc, ctx->used);
}

struct ceph_cap *ceph_get_cap(struct ceph_mds_client *mdsc,
			      struct ceph_cap_reservation *ctx)
{
	struct ceph_cap *cap = NULL;

	/* temporary, until we do something about cap import/export */
	if (!ctx) {
		cap = kmem_cache_alloc(ceph_cap_cachep, GFP_NOFS);
		if (cap) {
			spin_lock(&mdsc->caps_list_lock);
			mdsc->caps_use_count++;
			mdsc->caps_total_count++;
			spin_unlock(&mdsc->caps_list_lock);
		} else {
			spin_lock(&mdsc->caps_list_lock);
			if (mdsc->caps_avail_count) {
				BUG_ON(list_empty(&mdsc->caps_list));

				mdsc->caps_avail_count--;
				mdsc->caps_use_count++;
				cap = list_first_entry(&mdsc->caps_list,
						struct ceph_cap, caps_item);
				list_del(&cap->caps_item);

				BUG_ON(mdsc->caps_total_count != mdsc->caps_use_count +
				       mdsc->caps_reserve_count + mdsc->caps_avail_count);
			}
			spin_unlock(&mdsc->caps_list_lock);
		}

		return cap;
	}

	spin_lock(&mdsc->caps_list_lock);
	dout("get_cap ctx=%p (%d) %d = %d used + %d resv + %d avail\n",
	     ctx, ctx->count, mdsc->caps_total_count, mdsc->caps_use_count,
	     mdsc->caps_reserve_count, mdsc->caps_avail_count);
	BUG_ON(!ctx->count);
	BUG_ON(ctx->count > mdsc->caps_reserve_count);
	BUG_ON(list_empty(&mdsc->caps_list));

	ctx->count--;
	ctx->used++;
	mdsc->caps_reserve_count--;
	mdsc->caps_use_count++;

	cap = list_first_entry(&mdsc->caps_list, struct ceph_cap, caps_item);
	list_del(&cap->caps_item);

	BUG_ON(mdsc->caps_total_count != mdsc->caps_use_count +
	       mdsc->caps_reserve_count + mdsc->caps_avail_count);
	spin_unlock(&mdsc->caps_list_lock);
	return cap;
}

void ceph_put_cap(struct ceph_mds_client *mdsc, struct ceph_cap *cap)
{
	spin_lock(&mdsc->caps_list_lock);
	dout("put_cap %p %d = %d used + %d resv + %d avail\n",
	     cap, mdsc->caps_total_count, mdsc->caps_use_count,
	     mdsc->caps_reserve_count, mdsc->caps_avail_count);
	mdsc->caps_use_count--;
	/*
	 * Keep some preallocated caps around (ceph_min_count), to
	 * avoid lots of free/alloc churn.
	 */
	if (mdsc->caps_avail_count >= mdsc->caps_reserve_count +
				      mdsc->caps_min_count) {
		mdsc->caps_total_count--;
		kmem_cache_free(ceph_cap_cachep, cap);
	} else {
		mdsc->caps_avail_count++;
		list_add(&cap->caps_item, &mdsc->caps_list);
	}

	BUG_ON(mdsc->caps_total_count != mdsc->caps_use_count +
	       mdsc->caps_reserve_count + mdsc->caps_avail_count);
	spin_unlock(&mdsc->caps_list_lock);
}

void ceph_reservation_status(struct ceph_fs_client *fsc,
			     int *total, int *avail, int *used, int *reserved,
			     int *min)
{
	struct ceph_mds_client *mdsc = fsc->mdsc;

	spin_lock(&mdsc->caps_list_lock);

	if (total)
		*total = mdsc->caps_total_count;
	if (avail)
		*avail = mdsc->caps_avail_count;
	if (used)
		*used = mdsc->caps_use_count;
	if (reserved)
		*reserved = mdsc->caps_reserve_count;
	if (min)
		*min = mdsc->caps_min_count;

	spin_unlock(&mdsc->caps_list_lock);
}

/*
 * Find ceph_cap for given mds, if any.
 *
 * Called with i_ceph_lock held.
 */
static struct ceph_cap *__get_cap_for_mds(struct ceph_inode_info *ci, int mds)
{
	struct ceph_cap *cap;
	struct rb_node *n = ci->i_caps.rb_node;

	while (n) {
		cap = rb_entry(n, struct ceph_cap, ci_node);
		if (mds < cap->mds)
			n = n->rb_left;
		else if (mds > cap->mds)
			n = n->rb_right;
		else
			return cap;
	}
	return NULL;
}

struct ceph_cap *ceph_get_cap_for_mds(struct ceph_inode_info *ci, int mds)
{
	struct ceph_cap *cap;

	spin_lock(&ci->i_ceph_lock);
	cap = __get_cap_for_mds(ci, mds);
	spin_unlock(&ci->i_ceph_lock);
	return cap;
}

/*
 * Called under i_ceph_lock.
 */
static void __insert_cap_node(struct ceph_inode_info *ci,
			      struct ceph_cap *new)
{
	struct rb_node **p = &ci->i_caps.rb_node;
	struct rb_node *parent = NULL;
	struct ceph_cap *cap = NULL;

	while (*p) {
		parent = *p;
		cap = rb_entry(parent, struct ceph_cap, ci_node);
		if (new->mds < cap->mds)
			p = &(*p)->rb_left;
		else if (new->mds > cap->mds)
			p = &(*p)->rb_right;
		else
			BUG();
	}

	rb_link_node(&new->ci_node, parent, p);
	rb_insert_color(&new->ci_node, &ci->i_caps);
}

/*
 * (re)set cap hold timeouts, which control the delayed release
 * of unused caps back to the MDS.  Should be called on cap use.
 */
static void __cap_set_timeouts(struct ceph_mds_client *mdsc,
			       struct ceph_inode_info *ci)
{
	struct ceph_mount_options *opt = mdsc->fsc->mount_options;
	ci->i_hold_caps_max = round_jiffies(jiffies +
					    opt->caps_wanted_delay_max * HZ);
	dout("__cap_set_timeouts %p %lu\n", &ci->vfs_inode,
	     ci->i_hold_caps_max - jiffies);
}

/*
 * (Re)queue cap at the end of the delayed cap release list.
 *
 * If I_FLUSH is set, leave the inode at the front of the list.
 *
 * Caller holds i_ceph_lock
 *    -> we take mdsc->cap_delay_lock
 */
static void __cap_delay_requeue(struct ceph_mds_client *mdsc,
				struct ceph_inode_info *ci)
{
	dout("__cap_delay_requeue %p flags 0x%lx at %lu\n", &ci->vfs_inode,
	     ci->i_ceph_flags, ci->i_hold_caps_max);
	if (!mdsc->stopping) {
		spin_lock(&mdsc->cap_delay_lock);
		if (!list_empty(&ci->i_cap_delay_list)) {
			if (ci->i_ceph_flags & CEPH_I_FLUSH)
				goto no_change;
			list_del_init(&ci->i_cap_delay_list);
		}
		__cap_set_timeouts(mdsc, ci);
		list_add_tail(&ci->i_cap_delay_list, &mdsc->cap_delay_list);
no_change:
		spin_unlock(&mdsc->cap_delay_lock);
	}
}

/*
 * Queue an inode for immediate writeback.  Mark inode with I_FLUSH,
 * indicating we should send a cap message to flush dirty metadata
 * asap, and move to the front of the delayed cap list.
 */
static void __cap_delay_requeue_front(struct ceph_mds_client *mdsc,
				      struct ceph_inode_info *ci)
{
	dout("__cap_delay_requeue_front %p\n", &ci->vfs_inode);
	spin_lock(&mdsc->cap_delay_lock);
	ci->i_ceph_flags |= CEPH_I_FLUSH;
	if (!list_empty(&ci->i_cap_delay_list))
		list_del_init(&ci->i_cap_delay_list);
	list_add(&ci->i_cap_delay_list, &mdsc->cap_delay_list);
	spin_unlock(&mdsc->cap_delay_lock);
}

/*
 * Cancel delayed work on cap.
 *
 * Caller must hold i_ceph_lock.
 */
static void __cap_delay_cancel(struct ceph_mds_client *mdsc,
			       struct ceph_inode_info *ci)
{
	dout("__cap_delay_cancel %p\n", &ci->vfs_inode);
	if (list_empty(&ci->i_cap_delay_list))
		return;
	spin_lock(&mdsc->cap_delay_lock);
	list_del_init(&ci->i_cap_delay_list);
	spin_unlock(&mdsc->cap_delay_lock);
}

/* Common issue checks for add_cap, handle_cap_grant. */
static void __check_cap_issue(struct ceph_inode_info *ci, struct ceph_cap *cap,
			      unsigned issued)
{
	unsigned had = __ceph_caps_issued(ci, NULL);

	lockdep_assert_held(&ci->i_ceph_lock);

	/*
	 * Each time we receive FILE_CACHE anew, we increment
	 * i_rdcache_gen.
	 */
	if (S_ISREG(ci->vfs_inode.i_mode) &&
	    (issued & (CEPH_CAP_FILE_CACHE|CEPH_CAP_FILE_LAZYIO)) &&
	    (had & (CEPH_CAP_FILE_CACHE|CEPH_CAP_FILE_LAZYIO)) == 0) {
		ci->i_rdcache_gen++;
	}

	/*
	 * If FILE_SHARED is newly issued, mark dir not complete. We don't
	 * know what happened to this directory while we didn't have the cap.
	 * If FILE_SHARED is being revoked, also mark dir not complete. It
	 * stops on-going cached readdir.
	 */
	if ((issued & CEPH_CAP_FILE_SHARED) != (had & CEPH_CAP_FILE_SHARED)) {
		if (issued & CEPH_CAP_FILE_SHARED)
			atomic_inc(&ci->i_shared_gen);
		if (S_ISDIR(ci->vfs_inode.i_mode)) {
			dout(" marking %p NOT complete\n", &ci->vfs_inode);
			__ceph_dir_clear_complete(ci);
		}
	}

	/* Wipe saved layout if we're losing DIR_CREATE caps */
	if (S_ISDIR(ci->vfs_inode.i_mode) && (had & CEPH_CAP_DIR_CREATE) &&
		!(issued & CEPH_CAP_DIR_CREATE)) {
	     ceph_put_string(rcu_dereference_raw(ci->i_cached_layout.pool_ns));
	     memset(&ci->i_cached_layout, 0, sizeof(ci->i_cached_layout));
	}
}

/**
 * change_auth_cap_ses - move inode to appropriate lists when auth caps change
 * @ci: inode to be moved
 * @session: new auth caps session
 */
static void change_auth_cap_ses(struct ceph_inode_info *ci,
				struct ceph_mds_session *session)
{
	lockdep_assert_held(&ci->i_ceph_lock);

	if (list_empty(&ci->i_dirty_item) && list_empty(&ci->i_flushing_item))
		return;

	spin_lock(&session->s_mdsc->cap_dirty_lock);
	if (!list_empty(&ci->i_dirty_item))
		list_move(&ci->i_dirty_item, &session->s_cap_dirty);
	if (!list_empty(&ci->i_flushing_item))
		list_move_tail(&ci->i_flushing_item, &session->s_cap_flushing);
	spin_unlock(&session->s_mdsc->cap_dirty_lock);
}

/*
 * Add a capability under the given MDS session.
 *
 * Caller should hold session snap_rwsem (read) and ci->i_ceph_lock
 *
 * @fmode is the open file mode, if we are opening a file, otherwise
 * it is < 0.  (This is so we can atomically add the cap and add an
 * open file reference to it.)
 */
void ceph_add_cap(struct inode *inode,
		  struct ceph_mds_session *session, u64 cap_id,
		  unsigned issued, unsigned wanted,
		  unsigned seq, unsigned mseq, u64 realmino, int flags,
		  struct ceph_cap **new_cap)
{
	struct ceph_mds_client *mdsc = ceph_inode_to_client(inode)->mdsc;
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_cap *cap;
	int mds = session->s_mds;
	int actual_wanted;
	u32 gen;

	lockdep_assert_held(&ci->i_ceph_lock);

	dout("add_cap %p mds%d cap %llx %s seq %d\n", inode,
	     session->s_mds, cap_id, ceph_cap_string(issued), seq);

	gen = atomic_read(&session->s_cap_gen);

	cap = __get_cap_for_mds(ci, mds);
	if (!cap) {
		cap = *new_cap;
		*new_cap = NULL;

		cap->issued = 0;
		cap->implemented = 0;
		cap->mds = mds;
		cap->mds_wanted = 0;
		cap->mseq = 0;

		cap->ci = ci;
		__insert_cap_node(ci, cap);

		/* add to session cap list */
		cap->session = session;
		spin_lock(&session->s_cap_lock);
		list_add_tail(&cap->session_caps, &session->s_caps);
		session->s_nr_caps++;
		atomic64_inc(&mdsc->metric.total_caps);
		spin_unlock(&session->s_cap_lock);
	} else {
		spin_lock(&session->s_cap_lock);
		list_move_tail(&cap->session_caps, &session->s_caps);
		spin_unlock(&session->s_cap_lock);

		if (cap->cap_gen < gen)
			cap->issued = cap->implemented = CEPH_CAP_PIN;

		/*
		 * auth mds of the inode changed. we received the cap export
		 * message, but still haven't received the cap import message.
		 * handle_cap_export() updated the new auth MDS' cap.
		 *
		 * "ceph_seq_cmp(seq, cap->seq) <= 0" means we are processing
		 * a message that was send before the cap import message. So
		 * don't remove caps.
		 */
		if (ceph_seq_cmp(seq, cap->seq) <= 0) {
			WARN_ON(cap != ci->i_auth_cap);
			WARN_ON(cap->cap_id != cap_id);
			seq = cap->seq;
			mseq = cap->mseq;
			issued |= cap->issued;
			flags |= CEPH_CAP_FLAG_AUTH;
		}
	}

	if (!ci->i_snap_realm ||
	    ((flags & CEPH_CAP_FLAG_AUTH) &&
	     realmino != (u64)-1 && ci->i_snap_realm->ino != realmino)) {
		/*
		 * add this inode to the appropriate snap realm
		 */
		struct ceph_snap_realm *realm = ceph_lookup_snap_realm(mdsc,
							       realmino);
		if (realm)
			ceph_change_snap_realm(inode, realm);
		else
			WARN(1, "%s: couldn't find snap realm 0x%llx (ino 0x%llx oldrealm 0x%llx)\n",
			     __func__, realmino, ci->i_vino.ino,
			     ci->i_snap_realm ? ci->i_snap_realm->ino : 0);
	}

	__check_cap_issue(ci, cap, issued);

	/*
	 * If we are issued caps we don't want, or the mds' wanted
	 * value appears to be off, queue a check so we'll release
	 * later and/or update the mds wanted value.
	 */
	actual_wanted = __ceph_caps_wanted(ci);
	if ((wanted & ~actual_wanted) ||
	    (issued & ~actual_wanted & CEPH_CAP_ANY_WR)) {
		dout(" issued %s, mds wanted %s, actual %s, queueing\n",
		     ceph_cap_string(issued), ceph_cap_string(wanted),
		     ceph_cap_string(actual_wanted));
		__cap_delay_requeue(mdsc, ci);
	}

	if (flags & CEPH_CAP_FLAG_AUTH) {
		if (!ci->i_auth_cap ||
		    ceph_seq_cmp(ci->i_auth_cap->mseq, mseq) < 0) {
			if (ci->i_auth_cap &&
			    ci->i_auth_cap->session != cap->session)
				change_auth_cap_ses(ci, cap->session);
			ci->i_auth_cap = cap;
			cap->mds_wanted = wanted;
		}
	} else {
		WARN_ON(ci->i_auth_cap == cap);
	}

	dout("add_cap inode %p (%llx.%llx) cap %p %s now %s seq %d mds%d\n",
	     inode, ceph_vinop(inode), cap, ceph_cap_string(issued),
	     ceph_cap_string(issued|cap->issued), seq, mds);
	cap->cap_id = cap_id;
	cap->issued = issued;
	cap->implemented |= issued;
	if (ceph_seq_cmp(mseq, cap->mseq) > 0)
		cap->mds_wanted = wanted;
	else
		cap->mds_wanted |= wanted;
	cap->seq = seq;
	cap->issue_seq = seq;
	cap->mseq = mseq;
	cap->cap_gen = gen;
}

/*
 * Return true if cap has not timed out and belongs to the current
 * generation of the MDS session (i.e. has not gone 'stale' due to
 * us losing touch with the mds).
 */
static int __cap_is_valid(struct ceph_cap *cap)
{
	unsigned long ttl;
	u32 gen;

	gen = atomic_read(&cap->session->s_cap_gen);
	ttl = cap->session->s_cap_ttl;

	if (cap->cap_gen < gen || time_after_eq(jiffies, ttl)) {
		dout("__cap_is_valid %p cap %p issued %s "
		     "but STALE (gen %u vs %u)\n", &cap->ci->vfs_inode,
		     cap, ceph_cap_string(cap->issued), cap->cap_gen, gen);
		return 0;
	}

	return 1;
}

/*
 * Return set of valid cap bits issued to us.  Note that caps time
 * out, and may be invalidated in bulk if the client session times out
 * and session->s_cap_gen is bumped.
 */
int __ceph_caps_issued(struct ceph_inode_info *ci, int *implemented)
{
	int have = ci->i_snap_caps;
	struct ceph_cap *cap;
	struct rb_node *p;

	if (implemented)
		*implemented = 0;
	for (p = rb_first(&ci->i_caps); p; p = rb_next(p)) {
		cap = rb_entry(p, struct ceph_cap, ci_node);
		if (!__cap_is_valid(cap))
			continue;
		dout("__ceph_caps_issued %p cap %p issued %s\n",
		     &ci->vfs_inode, cap, ceph_cap_string(cap->issued));
		have |= cap->issued;
		if (implemented)
			*implemented |= cap->implemented;
	}
	/*
	 * exclude caps issued by non-auth MDS, but are been revoking
	 * by the auth MDS. The non-auth MDS should be revoking/exporting
	 * these caps, but the message is delayed.
	 */
	if (ci->i_auth_cap) {
		cap = ci->i_auth_cap;
		have &= ~cap->implemented | cap->issued;
	}
	return have;
}

/*
 * Get cap bits issued by caps other than @ocap
 */
int __ceph_caps_issued_other(struct ceph_inode_info *ci, struct ceph_cap *ocap)
{
	int have = ci->i_snap_caps;
	struct ceph_cap *cap;
	struct rb_node *p;

	for (p = rb_first(&ci->i_caps); p; p = rb_next(p)) {
		cap = rb_entry(p, struct ceph_cap, ci_node);
		if (cap == ocap)
			continue;
		if (!__cap_is_valid(cap))
			continue;
		have |= cap->issued;
	}
	return have;
}

/*
 * Move a cap to the end of the LRU (oldest caps at list head, newest
 * at list tail).
 */
static void __touch_cap(struct ceph_cap *cap)
{
	struct ceph_mds_session *s = cap->session;

	spin_lock(&s->s_cap_lock);
	if (!s->s_cap_iterator) {
		dout("__touch_cap %p cap %p mds%d\n", &cap->ci->vfs_inode, cap,
		     s->s_mds);
		list_move_tail(&cap->session_caps, &s->s_caps);
	} else {
		dout("__touch_cap %p cap %p mds%d NOP, iterating over caps\n",
		     &cap->ci->vfs_inode, cap, s->s_mds);
	}
	spin_unlock(&s->s_cap_lock);
}

/*
 * Check if we hold the given mask.  If so, move the cap(s) to the
 * front of their respective LRUs.  (This is the preferred way for
 * callers to check for caps they want.)
 */
int __ceph_caps_issued_mask(struct ceph_inode_info *ci, int mask, int touch)
{
	struct ceph_cap *cap;
	struct rb_node *p;
	int have = ci->i_snap_caps;

	if ((have & mask) == mask) {
		dout("__ceph_caps_issued_mask ino 0x%llx snap issued %s"
		     " (mask %s)\n", ceph_ino(&ci->vfs_inode),
		     ceph_cap_string(have),
		     ceph_cap_string(mask));
		return 1;
	}

	for (p = rb_first(&ci->i_caps); p; p = rb_next(p)) {
		cap = rb_entry(p, struct ceph_cap, ci_node);
		if (!__cap_is_valid(cap))
			continue;
		if ((cap->issued & mask) == mask) {
			dout("__ceph_caps_issued_mask ino 0x%llx cap %p issued %s"
			     " (mask %s)\n", ceph_ino(&ci->vfs_inode), cap,
			     ceph_cap_string(cap->issued),
			     ceph_cap_string(mask));
			if (touch)
				__touch_cap(cap);
			return 1;
		}

		/* does a combination of caps satisfy mask? */
		have |= cap->issued;
		if ((have & mask) == mask) {
			dout("__ceph_caps_issued_mask ino 0x%llx combo issued %s"
			     " (mask %s)\n", ceph_ino(&ci->vfs_inode),
			     ceph_cap_string(cap->issued),
			     ceph_cap_string(mask));
			if (touch) {
				struct rb_node *q;

				/* touch this + preceding caps */
				__touch_cap(cap);
				for (q = rb_first(&ci->i_caps); q != p;
				     q = rb_next(q)) {
					cap = rb_entry(q, struct ceph_cap,
						       ci_node);
					if (!__cap_is_valid(cap))
						continue;
					if (cap->issued & mask)
						__touch_cap(cap);
				}
			}
			return 1;
		}
	}

	return 0;
}

int __ceph_caps_issued_mask_metric(struct ceph_inode_info *ci, int mask,
				   int touch)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(ci->vfs_inode.i_sb);
	int r;

	r = __ceph_caps_issued_mask(ci, mask, touch);
	if (r)
		ceph_update_cap_hit(&fsc->mdsc->metric);
	else
		ceph_update_cap_mis(&fsc->mdsc->metric);
	return r;
}

/*
 * Return true if mask caps are currently being revoked by an MDS.
 */
int __ceph_caps_revoking_other(struct ceph_inode_info *ci,
			       struct ceph_cap *ocap, int mask)
{
	struct ceph_cap *cap;
	struct rb_node *p;

	for (p = rb_first(&ci->i_caps); p; p = rb_next(p)) {
		cap = rb_entry(p, struct ceph_cap, ci_node);
		if (cap != ocap &&
		    (cap->implemented & ~cap->issued & mask))
			return 1;
	}
	return 0;
}

int ceph_caps_revoking(struct ceph_inode_info *ci, int mask)
{
	struct inode *inode = &ci->vfs_inode;
	int ret;

	spin_lock(&ci->i_ceph_lock);
	ret = __ceph_caps_revoking_other(ci, NULL, mask);
	spin_unlock(&ci->i_ceph_lock);
	dout("ceph_caps_revoking %p %s = %d\n", inode,
	     ceph_cap_string(mask), ret);
	return ret;
}

int __ceph_caps_used(struct ceph_inode_info *ci)
{
	int used = 0;
	if (ci->i_pin_ref)
		used |= CEPH_CAP_PIN;
	if (ci->i_rd_ref)
		used |= CEPH_CAP_FILE_RD;
	if (ci->i_rdcache_ref ||
	    (S_ISREG(ci->vfs_inode.i_mode) &&
	     ci->vfs_inode.i_data.nrpages))
		used |= CEPH_CAP_FILE_CACHE;
	if (ci->i_wr_ref)
		used |= CEPH_CAP_FILE_WR;
	if (ci->i_wb_ref || ci->i_wrbuffer_ref)
		used |= CEPH_CAP_FILE_BUFFER;
	if (ci->i_fx_ref)
		used |= CEPH_CAP_FILE_EXCL;
	return used;
}

#define FMODE_WAIT_BIAS 1000

/*
 * wanted, by virtue of open file modes
 */
int __ceph_caps_file_wanted(struct ceph_inode_info *ci)
{
	const int PIN_SHIFT = ffs(CEPH_FILE_MODE_PIN);
	const int RD_SHIFT = ffs(CEPH_FILE_MODE_RD);
	const int WR_SHIFT = ffs(CEPH_FILE_MODE_WR);
	const int LAZY_SHIFT = ffs(CEPH_FILE_MODE_LAZY);
	struct ceph_mount_options *opt =
		ceph_inode_to_client(&ci->vfs_inode)->mount_options;
	unsigned long used_cutoff = jiffies - opt->caps_wanted_delay_max * HZ;
	unsigned long idle_cutoff = jiffies - opt->caps_wanted_delay_min * HZ;

	if (S_ISDIR(ci->vfs_inode.i_mode)) {
		int want = 0;

		/* use used_cutoff here, to keep dir's wanted caps longer */
		if (ci->i_nr_by_mode[RD_SHIFT] > 0 ||
		    time_after(ci->i_last_rd, used_cutoff))
			want |= CEPH_CAP_ANY_SHARED;

		if (ci->i_nr_by_mode[WR_SHIFT] > 0 ||
		    time_after(ci->i_last_wr, used_cutoff)) {
			want |= CEPH_CAP_ANY_SHARED | CEPH_CAP_FILE_EXCL;
			if (opt->flags & CEPH_MOUNT_OPT_ASYNC_DIROPS)
				want |= CEPH_CAP_ANY_DIR_OPS;
		}

		if (want || ci->i_nr_by_mode[PIN_SHIFT] > 0)
			want |= CEPH_CAP_PIN;

		return want;
	} else {
		int bits = 0;

		if (ci->i_nr_by_mode[RD_SHIFT] > 0) {
			if (ci->i_nr_by_mode[RD_SHIFT] >= FMODE_WAIT_BIAS ||
			    time_after(ci->i_last_rd, used_cutoff))
				bits |= 1 << RD_SHIFT;
		} else if (time_after(ci->i_last_rd, idle_cutoff)) {
			bits |= 1 << RD_SHIFT;
		}

		if (ci->i_nr_by_mode[WR_SHIFT] > 0) {
			if (ci->i_nr_by_mode[WR_SHIFT] >= FMODE_WAIT_BIAS ||
			    time_after(ci->i_last_wr, used_cutoff))
				bits |= 1 << WR_SHIFT;
		} else if (time_after(ci->i_last_wr, idle_cutoff)) {
			bits |= 1 << WR_SHIFT;
		}

		/* check lazyio only when read/write is wanted */
		if ((bits & (CEPH_FILE_MODE_RDWR << 1)) &&
		    ci->i_nr_by_mode[LAZY_SHIFT] > 0)
			bits |= 1 << LAZY_SHIFT;

		return bits ? ceph_caps_for_mode(bits >> 1) : 0;
	}
}

/*
 * wanted, by virtue of open file modes AND cap refs (buffered/cached data)
 */
int __ceph_caps_wanted(struct ceph_inode_info *ci)
{
	int w = __ceph_caps_file_wanted(ci) | __ceph_caps_used(ci);
	if (S_ISDIR(ci->vfs_inode.i_mode)) {
		/* we want EXCL if holding caps of dir ops */
		if (w & CEPH_CAP_ANY_DIR_OPS)
			w |= CEPH_CAP_FILE_EXCL;
	} else {
		/* we want EXCL if dirty data */
		if (w & CEPH_CAP_FILE_BUFFER)
			w |= CEPH_CAP_FILE_EXCL;
	}
	return w;
}

/*
 * Return caps we have registered with the MDS(s) as 'wanted'.
 */
int __ceph_caps_mds_wanted(struct ceph_inode_info *ci, bool check)
{
	struct ceph_cap *cap;
	struct rb_node *p;
	int mds_wanted = 0;

	for (p = rb_first(&ci->i_caps); p; p = rb_next(p)) {
		cap = rb_entry(p, struct ceph_cap, ci_node);
		if (check && !__cap_is_valid(cap))
			continue;
		if (cap == ci->i_auth_cap)
			mds_wanted |= cap->mds_wanted;
		else
			mds_wanted |= (cap->mds_wanted & ~CEPH_CAP_ANY_FILE_WR);
	}
	return mds_wanted;
}

int ceph_is_any_caps(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int ret;

	spin_lock(&ci->i_ceph_lock);
	ret = __ceph_is_any_real_caps(ci);
	spin_unlock(&ci->i_ceph_lock);

	return ret;
}

/*
 * Remove a cap.  Take steps to deal with a racing iterate_session_caps.
 *
 * caller should hold i_ceph_lock.
 * caller will not hold session s_mutex if called from destroy_inode.
 */
void __ceph_remove_cap(struct ceph_cap *cap, bool queue_release)
{
	struct ceph_mds_session *session = cap->session;
	struct ceph_inode_info *ci = cap->ci;
	struct ceph_mds_client *mdsc;
	int removed = 0;

	/* 'ci' being NULL means the remove have already occurred */
	if (!ci) {
		dout("%s: cap inode is NULL\n", __func__);
		return;
	}

	lockdep_assert_held(&ci->i_ceph_lock);

	dout("__ceph_remove_cap %p from %p\n", cap, &ci->vfs_inode);

	mdsc = ceph_inode_to_client(&ci->vfs_inode)->mdsc;

	/* remove from inode's cap rbtree, and clear auth cap */
	rb_erase(&cap->ci_node, &ci->i_caps);
	if (ci->i_auth_cap == cap)
		ci->i_auth_cap = NULL;

	/* remove from session list */
	spin_lock(&session->s_cap_lock);
	if (session->s_cap_iterator == cap) {
		/* not yet, we are iterating over this very cap */
		dout("__ceph_remove_cap  delaying %p removal from session %p\n",
		     cap, cap->session);
	} else {
		list_del_init(&cap->session_caps);
		session->s_nr_caps--;
		atomic64_dec(&mdsc->metric.total_caps);
		cap->session = NULL;
		removed = 1;
	}
	/* protect backpointer with s_cap_lock: see iterate_session_caps */
	cap->ci = NULL;

	/*
	 * s_cap_reconnect is protected by s_cap_lock. no one changes
	 * s_cap_gen while session is in the reconnect state.
	 */
	if (queue_release &&
	    (!session->s_cap_reconnect ||
	     cap->cap_gen == atomic_read(&session->s_cap_gen))) {
		cap->queue_release = 1;
		if (removed) {
			__ceph_queue_cap_release(session, cap);
			removed = 0;
		}
	} else {
		cap->queue_release = 0;
	}
	cap->cap_ino = ci->i_vino.ino;

	spin_unlock(&session->s_cap_lock);

	if (removed)
		ceph_put_cap(mdsc, cap);

	if (!__ceph_is_any_real_caps(ci)) {
		/* when reconnect denied, we remove session caps forcibly,
		 * i_wr_ref can be non-zero. If there are ongoing write,
		 * keep i_snap_realm.
		 */
		if (ci->i_wr_ref == 0 && ci->i_snap_realm)
			ceph_change_snap_realm(&ci->vfs_inode, NULL);

		__cap_delay_cancel(mdsc, ci);
	}
}

void ceph_remove_cap(struct ceph_cap *cap, bool queue_release)
{
	struct ceph_inode_info *ci = cap->ci;
	struct ceph_fs_client *fsc;

	/* 'ci' being NULL means the remove have already occurred */
	if (!ci) {
		dout("%s: cap inode is NULL\n", __func__);
		return;
	}

	lockdep_assert_held(&ci->i_ceph_lock);

	fsc = ceph_sb_to_client(ci->vfs_inode.i_sb);
	WARN_ON_ONCE(ci->i_auth_cap == cap &&
		     !list_empty(&ci->i_dirty_item) &&
		     !fsc->blocklisted &&
		     READ_ONCE(fsc->mount_state) != CEPH_MOUNT_SHUTDOWN);

	__ceph_remove_cap(cap, queue_release);
}

struct cap_msg_args {
	struct ceph_mds_session	*session;
	u64			ino, cid, follows;
	u64			flush_tid, oldest_flush_tid, size, max_size;
	u64			xattr_version;
	u64			change_attr;
	struct ceph_buffer	*xattr_buf;
	struct ceph_buffer	*old_xattr_buf;
	struct timespec64	atime, mtime, ctime, btime;
	int			op, caps, wanted, dirty;
	u32			seq, issue_seq, mseq, time_warp_seq;
	u32			flags;
	kuid_t			uid;
	kgid_t			gid;
	umode_t			mode;
	bool			inline_data;
	bool			wake;
};

/*
 * cap struct size + flock buffer size + inline version + inline data size +
 * osd_epoch_barrier + oldest_flush_tid
 */
#define CAP_MSG_SIZE (sizeof(struct ceph_mds_caps) + \
		      4 + 8 + 4 + 4 + 8 + 4 + 4 + 4 + 8 + 8 + 4)

/* Marshal up the cap msg to the MDS */
static void encode_cap_msg(struct ceph_msg *msg, struct cap_msg_args *arg)
{
	struct ceph_mds_caps *fc;
	void *p;
	struct ceph_osd_client *osdc = &arg->session->s_mdsc->fsc->client->osdc;

	dout("%s %s %llx %llx caps %s wanted %s dirty %s seq %u/%u tid %llu/%llu mseq %u follows %lld size %llu/%llu xattr_ver %llu xattr_len %d\n",
	     __func__, ceph_cap_op_name(arg->op), arg->cid, arg->ino,
	     ceph_cap_string(arg->caps), ceph_cap_string(arg->wanted),
	     ceph_cap_string(arg->dirty), arg->seq, arg->issue_seq,
	     arg->flush_tid, arg->oldest_flush_tid, arg->mseq, arg->follows,
	     arg->size, arg->max_size, arg->xattr_version,
	     arg->xattr_buf ? (int)arg->xattr_buf->vec.iov_len : 0);

	msg->hdr.version = cpu_to_le16(10);
	msg->hdr.tid = cpu_to_le64(arg->flush_tid);

	fc = msg->front.iov_base;
	memset(fc, 0, sizeof(*fc));

	fc->cap_id = cpu_to_le64(arg->cid);
	fc->op = cpu_to_le32(arg->op);
	fc->seq = cpu_to_le32(arg->seq);
	fc->issue_seq = cpu_to_le32(arg->issue_seq);
	fc->migrate_seq = cpu_to_le32(arg->mseq);
	fc->caps = cpu_to_le32(arg->caps);
	fc->wanted = cpu_to_le32(arg->wanted);
	fc->dirty = cpu_to_le32(arg->dirty);
	fc->ino = cpu_to_le64(arg->ino);
	fc->snap_follows = cpu_to_le64(arg->follows);

	fc->size = cpu_to_le64(arg->size);
	fc->max_size = cpu_to_le64(arg->max_size);
	ceph_encode_timespec64(&fc->mtime, &arg->mtime);
	ceph_encode_timespec64(&fc->atime, &arg->atime);
	ceph_encode_timespec64(&fc->ctime, &arg->ctime);
	fc->time_warp_seq = cpu_to_le32(arg->time_warp_seq);

	fc->uid = cpu_to_le32(from_kuid(&init_user_ns, arg->uid));
	fc->gid = cpu_to_le32(from_kgid(&init_user_ns, arg->gid));
	fc->mode = cpu_to_le32(arg->mode);

	fc->xattr_version = cpu_to_le64(arg->xattr_version);
	if (arg->xattr_buf) {
		msg->middle = ceph_buffer_get(arg->xattr_buf);
		fc->xattr_len = cpu_to_le32(arg->xattr_buf->vec.iov_len);
		msg->hdr.middle_len = cpu_to_le32(arg->xattr_buf->vec.iov_len);
	}

	p = fc + 1;
	/* flock buffer size (version 2) */
	ceph_encode_32(&p, 0);
	/* inline version (version 4) */
	ceph_encode_64(&p, arg->inline_data ? 0 : CEPH_INLINE_NONE);
	/* inline data size */
	ceph_encode_32(&p, 0);
	/*
	 * osd_epoch_barrier (version 5)
	 * The epoch_barrier is protected osdc->lock, so READ_ONCE here in
	 * case it was recently changed
	 */
	ceph_encode_32(&p, READ_ONCE(osdc->epoch_barrier));
	/* oldest_flush_tid (version 6) */
	ceph_encode_64(&p, arg->oldest_flush_tid);

	/*
	 * caller_uid/caller_gid (version 7)
	 *
	 * Currently, we don't properly track which caller dirtied the caps
	 * last, and force a flush of them when there is a conflict. For now,
	 * just set this to 0:0, to emulate how the MDS has worked up to now.
	 */
	ceph_encode_32(&p, 0);
	ceph_encode_32(&p, 0);

	/* pool namespace (version 8) (mds always ignores this) */
	ceph_encode_32(&p, 0);

	/* btime and change_attr (version 9) */
	ceph_encode_timespec64(p, &arg->btime);
	p += sizeof(struct ceph_timespec);
	ceph_encode_64(&p, arg->change_attr);

	/* Advisory flags (version 10) */
	ceph_encode_32(&p, arg->flags);
}

/*
 * Queue cap releases when an inode is dropped from our cache.
 */
void __ceph_remove_caps(struct ceph_inode_info *ci)
{
	struct rb_node *p;

	/* lock i_ceph_lock, because ceph_d_revalidate(..., LOOKUP_RCU)
	 * may call __ceph_caps_issued_mask() on a freeing inode. */
	spin_lock(&ci->i_ceph_lock);
	p = rb_first(&ci->i_caps);
	while (p) {
		struct ceph_cap *cap = rb_entry(p, struct ceph_cap, ci_node);
		p = rb_next(p);
		ceph_remove_cap(cap, true);
	}
	spin_unlock(&ci->i_ceph_lock);
}

/*
 * Prepare to send a cap message to an MDS. Update the cap state, and populate
 * the arg struct with the parameters that will need to be sent. This should
 * be done under the i_ceph_lock to guard against changes to cap state.
 *
 * Make note of max_size reported/requested from mds, revoked caps
 * that have now been implemented.
 */
static void __prep_cap(struct cap_msg_args *arg, struct ceph_cap *cap,
		       int op, int flags, int used, int want, int retain,
		       int flushing, u64 flush_tid, u64 oldest_flush_tid)
{
	struct ceph_inode_info *ci = cap->ci;
	struct inode *inode = &ci->vfs_inode;
	int held, revoking;

	lockdep_assert_held(&ci->i_ceph_lock);

	held = cap->issued | cap->implemented;
	revoking = cap->implemented & ~cap->issued;
	retain &= ~revoking;

	dout("%s %p cap %p session %p %s -> %s (revoking %s)\n",
	     __func__, inode, cap, cap->session,
	     ceph_cap_string(held), ceph_cap_string(held & retain),
	     ceph_cap_string(revoking));
	BUG_ON((retain & CEPH_CAP_PIN) == 0);

	ci->i_ceph_flags &= ~CEPH_I_FLUSH;

	cap->issued &= retain;  /* drop bits we don't want */
	/*
	 * Wake up any waiters on wanted -> needed transition. This is due to
	 * the weird transition from buffered to sync IO... we need to flush
	 * dirty pages _before_ allowing sync writes to avoid reordering.
	 */
	arg->wake = cap->implemented & ~cap->issued;
	cap->implemented &= cap->issued | used;
	cap->mds_wanted = want;

	arg->session = cap->session;
	arg->ino = ceph_vino(inode).ino;
	arg->cid = cap->cap_id;
	arg->follows = flushing ? ci->i_head_snapc->seq : 0;
	arg->flush_tid = flush_tid;
	arg->oldest_flush_tid = oldest_flush_tid;

	arg->size = i_size_read(inode);
	ci->i_reported_size = arg->size;
	arg->max_size = ci->i_wanted_max_size;
	if (cap == ci->i_auth_cap) {
		if (want & CEPH_CAP_ANY_FILE_WR)
			ci->i_requested_max_size = arg->max_size;
		else
			ci->i_requested_max_size = 0;
	}

	if (flushing & CEPH_CAP_XATTR_EXCL) {
		arg->old_xattr_buf = __ceph_build_xattrs_blob(ci);
		arg->xattr_version = ci->i_xattrs.version;
		arg->xattr_buf = ci->i_xattrs.blob;
	} else {
		arg->xattr_buf = NULL;
		arg->old_xattr_buf = NULL;
	}

	arg->mtime = inode->i_mtime;
	arg->atime = inode->i_atime;
	arg->ctime = inode->i_ctime;
	arg->btime = ci->i_btime;
	arg->change_attr = inode_peek_iversion_raw(inode);

	arg->op = op;
	arg->caps = cap->implemented;
	arg->wanted = want;
	arg->dirty = flushing;

	arg->seq = cap->seq;
	arg->issue_seq = cap->issue_seq;
	arg->mseq = cap->mseq;
	arg->time_warp_seq = ci->i_time_warp_seq;

	arg->uid = inode->i_uid;
	arg->gid = inode->i_gid;
	arg->mode = inode->i_mode;

	arg->inline_data = ci->i_inline_version != CEPH_INLINE_NONE;
	if (!(flags & CEPH_CLIENT_CAPS_PENDING_CAPSNAP) &&
	    !list_empty(&ci->i_cap_snaps)) {
		struct ceph_cap_snap *capsnap;
		list_for_each_entry_reverse(capsnap, &ci->i_cap_snaps, ci_item) {
			if (capsnap->cap_flush.tid)
				break;
			if (capsnap->need_flush) {
				flags |= CEPH_CLIENT_CAPS_PENDING_CAPSNAP;
				break;
			}
		}
	}
	arg->flags = flags;
}

/*
 * Send a cap msg on the given inode.
 *
 * Caller should hold snap_rwsem (read), s_mutex.
 */
static void __send_cap(struct cap_msg_args *arg, struct ceph_inode_info *ci)
{
	struct ceph_msg *msg;
	struct inode *inode = &ci->vfs_inode;

	msg = ceph_msg_new(CEPH_MSG_CLIENT_CAPS, CAP_MSG_SIZE, GFP_NOFS, false);
	if (!msg) {
		pr_err("error allocating cap msg: ino (%llx.%llx) flushing %s tid %llu, requeuing cap.\n",
		       ceph_vinop(inode), ceph_cap_string(arg->dirty),
		       arg->flush_tid);
		spin_lock(&ci->i_ceph_lock);
		__cap_delay_requeue(arg->session->s_mdsc, ci);
		spin_unlock(&ci->i_ceph_lock);
		return;
	}

	encode_cap_msg(msg, arg);
	ceph_con_send(&arg->session->s_con, msg);
	ceph_buffer_put(arg->old_xattr_buf);
	if (arg->wake)
		wake_up_all(&ci->i_cap_wq);
}

static inline int __send_flush_snap(struct inode *inode,
				    struct ceph_mds_session *session,
				    struct ceph_cap_snap *capsnap,
				    u32 mseq, u64 oldest_flush_tid)
{
	struct cap_msg_args	arg;
	struct ceph_msg		*msg;

	msg = ceph_msg_new(CEPH_MSG_CLIENT_CAPS, CAP_MSG_SIZE, GFP_NOFS, false);
	if (!msg)
		return -ENOMEM;

	arg.session = session;
	arg.ino = ceph_vino(inode).ino;
	arg.cid = 0;
	arg.follows = capsnap->follows;
	arg.flush_tid = capsnap->cap_flush.tid;
	arg.oldest_flush_tid = oldest_flush_tid;

	arg.size = capsnap->size;
	arg.max_size = 0;
	arg.xattr_version = capsnap->xattr_version;
	arg.xattr_buf = capsnap->xattr_blob;
	arg.old_xattr_buf = NULL;

	arg.atime = capsnap->atime;
	arg.mtime = capsnap->mtime;
	arg.ctime = capsnap->ctime;
	arg.btime = capsnap->btime;
	arg.change_attr = capsnap->change_attr;

	arg.op = CEPH_CAP_OP_FLUSHSNAP;
	arg.caps = capsnap->issued;
	arg.wanted = 0;
	arg.dirty = capsnap->dirty;

	arg.seq = 0;
	arg.issue_seq = 0;
	arg.mseq = mseq;
	arg.time_warp_seq = capsnap->time_warp_seq;

	arg.uid = capsnap->uid;
	arg.gid = capsnap->gid;
	arg.mode = capsnap->mode;

	arg.inline_data = capsnap->inline_data;
	arg.flags = 0;
	arg.wake = false;

	encode_cap_msg(msg, &arg);
	ceph_con_send(&arg.session->s_con, msg);
	return 0;
}

/*
 * When a snapshot is taken, clients accumulate dirty metadata on
 * inodes with capabilities in ceph_cap_snaps to describe the file
 * state at the time the snapshot was taken.  This must be flushed
 * asynchronously back to the MDS once sync writes complete and dirty
 * data is written out.
 *
 * Called under i_ceph_lock.
 */
static void __ceph_flush_snaps(struct ceph_inode_info *ci,
			       struct ceph_mds_session *session)
		__releases(ci->i_ceph_lock)
		__acquires(ci->i_ceph_lock)
{
	struct inode *inode = &ci->vfs_inode;
	struct ceph_mds_client *mdsc = session->s_mdsc;
	struct ceph_cap_snap *capsnap;
	u64 oldest_flush_tid = 0;
	u64 first_tid = 1, last_tid = 0;

	dout("__flush_snaps %p session %p\n", inode, session);

	list_for_each_entry(capsnap, &ci->i_cap_snaps, ci_item) {
		/*
		 * we need to wait for sync writes to complete and for dirty
		 * pages to be written out.
		 */
		if (capsnap->dirty_pages || capsnap->writing)
			break;

		/* should be removed by ceph_try_drop_cap_snap() */
		BUG_ON(!capsnap->need_flush);

		/* only flush each capsnap once */
		if (capsnap->cap_flush.tid > 0) {
			dout(" already flushed %p, skipping\n", capsnap);
			continue;
		}

		spin_lock(&mdsc->cap_dirty_lock);
		capsnap->cap_flush.tid = ++mdsc->last_cap_flush_tid;
		list_add_tail(&capsnap->cap_flush.g_list,
			      &mdsc->cap_flush_list);
		if (oldest_flush_tid == 0)
			oldest_flush_tid = __get_oldest_flush_tid(mdsc);
		if (list_empty(&ci->i_flushing_item)) {
			list_add_tail(&ci->i_flushing_item,
				      &session->s_cap_flushing);
		}
		spin_unlock(&mdsc->cap_dirty_lock);

		list_add_tail(&capsnap->cap_flush.i_list,
			      &ci->i_cap_flush_list);

		if (first_tid == 1)
			first_tid = capsnap->cap_flush.tid;
		last_tid = capsnap->cap_flush.tid;
	}

	ci->i_ceph_flags &= ~CEPH_I_FLUSH_SNAPS;

	while (first_tid <= last_tid) {
		struct ceph_cap *cap = ci->i_auth_cap;
		struct ceph_cap_flush *cf;
		int ret;

		if (!(cap && cap->session == session)) {
			dout("__flush_snaps %p auth cap %p not mds%d, "
			     "stop\n", inode, cap, session->s_mds);
			break;
		}

		ret = -ENOENT;
		list_for_each_entry(cf, &ci->i_cap_flush_list, i_list) {
			if (cf->tid >= first_tid) {
				ret = 0;
				break;
			}
		}
		if (ret < 0)
			break;

		first_tid = cf->tid + 1;

		capsnap = container_of(cf, struct ceph_cap_snap, cap_flush);
		refcount_inc(&capsnap->nref);
		spin_unlock(&ci->i_ceph_lock);

		dout("__flush_snaps %p capsnap %p tid %llu %s\n",
		     inode, capsnap, cf->tid, ceph_cap_string(capsnap->dirty));

		ret = __send_flush_snap(inode, session, capsnap, cap->mseq,
					oldest_flush_tid);
		if (ret < 0) {
			pr_err("__flush_snaps: error sending cap flushsnap, "
			       "ino (%llx.%llx) tid %llu follows %llu\n",
				ceph_vinop(inode), cf->tid, capsnap->follows);
		}

		ceph_put_cap_snap(capsnap);
		spin_lock(&ci->i_ceph_lock);
	}
}

void ceph_flush_snaps(struct ceph_inode_info *ci,
		      struct ceph_mds_session **psession)
{
	struct inode *inode = &ci->vfs_inode;
	struct ceph_mds_client *mdsc = ceph_inode_to_client(inode)->mdsc;
	struct ceph_mds_session *session = NULL;
	int mds;

	dout("ceph_flush_snaps %p\n", inode);
	if (psession)
		session = *psession;
retry:
	spin_lock(&ci->i_ceph_lock);
	if (!(ci->i_ceph_flags & CEPH_I_FLUSH_SNAPS)) {
		dout(" no capsnap needs flush, doing nothing\n");
		goto out;
	}
	if (!ci->i_auth_cap) {
		dout(" no auth cap (migrating?), doing nothing\n");
		goto out;
	}

	mds = ci->i_auth_cap->session->s_mds;
	if (session && session->s_mds != mds) {
		dout(" oops, wrong session %p mutex\n", session);
		ceph_put_mds_session(session);
		session = NULL;
	}
	if (!session) {
		spin_unlock(&ci->i_ceph_lock);
		mutex_lock(&mdsc->mutex);
		session = __ceph_lookup_mds_session(mdsc, mds);
		mutex_unlock(&mdsc->mutex);
		goto retry;
	}

	// make sure flushsnap messages are sent in proper order.
	if (ci->i_ceph_flags & CEPH_I_KICK_FLUSH)
		__kick_flushing_caps(mdsc, session, ci, 0);

	__ceph_flush_snaps(ci, session);
out:
	spin_unlock(&ci->i_ceph_lock);

	if (psession)
		*psession = session;
	else
		ceph_put_mds_session(session);
	/* we flushed them all; remove this inode from the queue */
	spin_lock(&mdsc->snap_flush_lock);
	list_del_init(&ci->i_snap_flush_item);
	spin_unlock(&mdsc->snap_flush_lock);
}

/*
 * Mark caps dirty.  If inode is newly dirty, return the dirty flags.
 * Caller is then responsible for calling __mark_inode_dirty with the
 * returned flags value.
 */
int __ceph_mark_dirty_caps(struct ceph_inode_info *ci, int mask,
			   struct ceph_cap_flush **pcf)
{
	struct ceph_mds_client *mdsc =
		ceph_sb_to_client(ci->vfs_inode.i_sb)->mdsc;
	struct inode *inode = &ci->vfs_inode;
	int was = ci->i_dirty_caps;
	int dirty = 0;

	lockdep_assert_held(&ci->i_ceph_lock);

	if (!ci->i_auth_cap) {
		pr_warn("__mark_dirty_caps %p %llx mask %s, "
			"but no auth cap (session was closed?)\n",
			inode, ceph_ino(inode), ceph_cap_string(mask));
		return 0;
	}

	dout("__mark_dirty_caps %p %s dirty %s -> %s\n", &ci->vfs_inode,
	     ceph_cap_string(mask), ceph_cap_string(was),
	     ceph_cap_string(was | mask));
	ci->i_dirty_caps |= mask;
	if (was == 0) {
		struct ceph_mds_session *session = ci->i_auth_cap->session;

		WARN_ON_ONCE(ci->i_prealloc_cap_flush);
		swap(ci->i_prealloc_cap_flush, *pcf);

		if (!ci->i_head_snapc) {
			WARN_ON_ONCE(!rwsem_is_locked(&mdsc->snap_rwsem));
			ci->i_head_snapc = ceph_get_snap_context(
				ci->i_snap_realm->cached_context);
		}
		dout(" inode %p now dirty snapc %p auth cap %p\n",
		     &ci->vfs_inode, ci->i_head_snapc, ci->i_auth_cap);
		BUG_ON(!list_empty(&ci->i_dirty_item));
		spin_lock(&mdsc->cap_dirty_lock);
		list_add(&ci->i_dirty_item, &session->s_cap_dirty);
		spin_unlock(&mdsc->cap_dirty_lock);
		if (ci->i_flushing_caps == 0) {
			ihold(inode);
			dirty |= I_DIRTY_SYNC;
		}
	} else {
		WARN_ON_ONCE(!ci->i_prealloc_cap_flush);
	}
	BUG_ON(list_empty(&ci->i_dirty_item));
	if (((was | ci->i_flushing_caps) & CEPH_CAP_FILE_BUFFER) &&
	    (mask & CEPH_CAP_FILE_BUFFER))
		dirty |= I_DIRTY_DATASYNC;
	__cap_delay_requeue(mdsc, ci);
	return dirty;
}

struct ceph_cap_flush *ceph_alloc_cap_flush(void)
{
	struct ceph_cap_flush *cf;

	cf = kmem_cache_alloc(ceph_cap_flush_cachep, GFP_KERNEL);
	if (!cf)
		return NULL;

	cf->is_capsnap = false;
	return cf;
}

void ceph_free_cap_flush(struct ceph_cap_flush *cf)
{
	if (cf)
		kmem_cache_free(ceph_cap_flush_cachep, cf);
}

static u64 __get_oldest_flush_tid(struct ceph_mds_client *mdsc)
{
	if (!list_empty(&mdsc->cap_flush_list)) {
		struct ceph_cap_flush *cf =
			list_first_entry(&mdsc->cap_flush_list,
					 struct ceph_cap_flush, g_list);
		return cf->tid;
	}
	return 0;
}

/*
 * Remove cap_flush from the mdsc's or inode's flushing cap list.
 * Return true if caller needs to wake up flush waiters.
 */
static bool __detach_cap_flush_from_mdsc(struct ceph_mds_client *mdsc,
					 struct ceph_cap_flush *cf)
{
	struct ceph_cap_flush *prev;
	bool wake = cf->wake;

	if (wake && cf->g_list.prev != &mdsc->cap_flush_list) {
		prev = list_prev_entry(cf, g_list);
		prev->wake = true;
		wake = false;
	}
	list_del_init(&cf->g_list);
	return wake;
}

static bool __detach_cap_flush_from_ci(struct ceph_inode_info *ci,
				       struct ceph_cap_flush *cf)
{
	struct ceph_cap_flush *prev;
	bool wake = cf->wake;

	if (wake && cf->i_list.prev != &ci->i_cap_flush_list) {
		prev = list_prev_entry(cf, i_list);
		prev->wake = true;
		wake = false;
	}
	list_del_init(&cf->i_list);
	return wake;
}

/*
 * Add dirty inode to the flushing list.  Assigned a seq number so we
 * can wait for caps to flush without starving.
 *
 * Called under i_ceph_lock. Returns the flush tid.
 */
static u64 __mark_caps_flushing(struct inode *inode,
				struct ceph_mds_session *session, bool wake,
				u64 *oldest_flush_tid)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_cap_flush *cf = NULL;
	int flushing;

	lockdep_assert_held(&ci->i_ceph_lock);
	BUG_ON(ci->i_dirty_caps == 0);
	BUG_ON(list_empty(&ci->i_dirty_item));
	BUG_ON(!ci->i_prealloc_cap_flush);

	flushing = ci->i_dirty_caps;
	dout("__mark_caps_flushing flushing %s, flushing_caps %s -> %s\n",
	     ceph_cap_string(flushing),
	     ceph_cap_string(ci->i_flushing_caps),
	     ceph_cap_string(ci->i_flushing_caps | flushing));
	ci->i_flushing_caps |= flushing;
	ci->i_dirty_caps = 0;
	dout(" inode %p now !dirty\n", inode);

	swap(cf, ci->i_prealloc_cap_flush);
	cf->caps = flushing;
	cf->wake = wake;

	spin_lock(&mdsc->cap_dirty_lock);
	list_del_init(&ci->i_dirty_item);

	cf->tid = ++mdsc->last_cap_flush_tid;
	list_add_tail(&cf->g_list, &mdsc->cap_flush_list);
	*oldest_flush_tid = __get_oldest_flush_tid(mdsc);

	if (list_empty(&ci->i_flushing_item)) {
		list_add_tail(&ci->i_flushing_item, &session->s_cap_flushing);
		mdsc->num_cap_flushing++;
	}
	spin_unlock(&mdsc->cap_dirty_lock);

	list_add_tail(&cf->i_list, &ci->i_cap_flush_list);

	return cf->tid;
}

/*
 * try to invalidate mapping pages without blocking.
 */
static int try_nonblocking_invalidate(struct inode *inode)
	__releases(ci->i_ceph_lock)
	__acquires(ci->i_ceph_lock)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	u32 invalidating_gen = ci->i_rdcache_gen;

	spin_unlock(&ci->i_ceph_lock);
	ceph_fscache_invalidate(inode);
	invalidate_mapping_pages(&inode->i_data, 0, -1);
	spin_lock(&ci->i_ceph_lock);

	if (inode->i_data.nrpages == 0 &&
	    invalidating_gen == ci->i_rdcache_gen) {
		/* success. */
		dout("try_nonblocking_invalidate %p success\n", inode);
		/* save any racing async invalidate some trouble */
		ci->i_rdcache_revoking = ci->i_rdcache_gen - 1;
		return 0;
	}
	dout("try_nonblocking_invalidate %p failed\n", inode);
	return -1;
}

bool __ceph_should_report_size(struct ceph_inode_info *ci)
{
	loff_t size = i_size_read(&ci->vfs_inode);
	/* mds will adjust max size according to the reported size */
	if (ci->i_flushing_caps & CEPH_CAP_FILE_WR)
		return false;
	if (size >= ci->i_max_size)
		return true;
	/* half of previous max_size increment has been used */
	if (ci->i_max_size > ci->i_reported_size &&
	    (size << 1) >= ci->i_max_size + ci->i_reported_size)
		return true;
	return false;
}

/*
 * Swiss army knife function to examine currently used and wanted
 * versus held caps.  Release, flush, ack revoked caps to mds as
 * appropriate.
 *
 *  CHECK_CAPS_AUTHONLY - we should only check the auth cap
 *  CHECK_CAPS_FLUSH - we should flush any dirty caps immediately, without
 *    further delay.
 */
void ceph_check_caps(struct ceph_inode_info *ci, int flags,
		     struct ceph_mds_session *session)
{
	struct inode *inode = &ci->vfs_inode;
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(inode->i_sb);
	struct ceph_cap *cap;
	u64 flush_tid, oldest_flush_tid;
	int file_wanted, used, cap_used;
	int issued, implemented, want, retain, revoking, flushing = 0;
	int mds = -1;   /* keep track of how far we've gone through i_caps list
			   to avoid an infinite loop on retry */
	struct rb_node *p;
	bool queue_invalidate = false;
	bool tried_invalidate = false;

	if (session)
		ceph_get_mds_session(session);

	spin_lock(&ci->i_ceph_lock);
	if (ci->i_ceph_flags & CEPH_I_FLUSH)
		flags |= CHECK_CAPS_FLUSH;
retry:
	/* Caps wanted by virtue of active open files. */
	file_wanted = __ceph_caps_file_wanted(ci);

	/* Caps which have active references against them */
	used = __ceph_caps_used(ci);

	/*
	 * "issued" represents the current caps that the MDS wants us to have.
	 * "implemented" is the set that we have been granted, and includes the
	 * ones that have not yet been returned to the MDS (the "revoking" set,
	 * usually because they have outstanding references).
	 */
	issued = __ceph_caps_issued(ci, &implemented);
	revoking = implemented & ~issued;

	want = file_wanted;

	/* The ones we currently want to retain (may be adjusted below) */
	retain = file_wanted | used | CEPH_CAP_PIN;
	if (!mdsc->stopping && inode->i_nlink > 0) {
		if (file_wanted) {
			retain |= CEPH_CAP_ANY;       /* be greedy */
		} else if (S_ISDIR(inode->i_mode) &&
			   (issued & CEPH_CAP_FILE_SHARED) &&
			   __ceph_dir_is_complete(ci)) {
			/*
			 * If a directory is complete, we want to keep
			 * the exclusive cap. So that MDS does not end up
			 * revoking the shared cap on every create/unlink
			 * operation.
			 */
			if (IS_RDONLY(inode)) {
				want = CEPH_CAP_ANY_SHARED;
			} else {
				want |= CEPH_CAP_ANY_SHARED | CEPH_CAP_FILE_EXCL;
			}
			retain |= want;
		} else {

			retain |= CEPH_CAP_ANY_SHARED;
			/*
			 * keep RD only if we didn't have the file open RW,
			 * because then the mds would revoke it anyway to
			 * journal max_size=0.
			 */
			if (ci->i_max_size == 0)
				retain |= CEPH_CAP_ANY_RD;
		}
	}

	dout("check_caps %p file_want %s used %s dirty %s flushing %s"
	     " issued %s revoking %s retain %s %s%s\n", inode,
	     ceph_cap_string(file_wanted),
	     ceph_cap_string(used), ceph_cap_string(ci->i_dirty_caps),
	     ceph_cap_string(ci->i_flushing_caps),
	     ceph_cap_string(issued), ceph_cap_string(revoking),
	     ceph_cap_string(retain),
	     (flags & CHECK_CAPS_AUTHONLY) ? " AUTHONLY" : "",
	     (flags & CHECK_CAPS_FLUSH) ? " FLUSH" : "");

	/*
	 * If we no longer need to hold onto old our caps, and we may
	 * have cached pages, but don't want them, then try to invalidate.
	 * If we fail, it's because pages are locked.... try again later.
	 */
	if ((!(flags & CHECK_CAPS_NOINVAL) || mdsc->stopping) &&
	    S_ISREG(inode->i_mode) &&
	    !(ci->i_wb_ref || ci->i_wrbuffer_ref) &&   /* no dirty pages... */
	    inode->i_data.nrpages &&		/* have cached pages */
	    (revoking & (CEPH_CAP_FILE_CACHE|
			 CEPH_CAP_FILE_LAZYIO)) && /*  or revoking cache */
	    !tried_invalidate) {
		dout("check_caps trying to invalidate on %p\n", inode);
		if (try_nonblocking_invalidate(inode) < 0) {
			dout("check_caps queuing invalidate\n");
			queue_invalidate = true;
			ci->i_rdcache_revoking = ci->i_rdcache_gen;
		}
		tried_invalidate = true;
		goto retry;
	}

	for (p = rb_first(&ci->i_caps); p; p = rb_next(p)) {
		int mflags = 0;
		struct cap_msg_args arg;

		cap = rb_entry(p, struct ceph_cap, ci_node);

		/* avoid looping forever */
		if (mds >= cap->mds ||
		    ((flags & CHECK_CAPS_AUTHONLY) && cap != ci->i_auth_cap))
			continue;

		/*
		 * If we have an auth cap, we don't need to consider any
		 * overlapping caps as used.
		 */
		cap_used = used;
		if (ci->i_auth_cap && cap != ci->i_auth_cap)
			cap_used &= ~ci->i_auth_cap->issued;

		revoking = cap->implemented & ~cap->issued;
		dout(" mds%d cap %p used %s issued %s implemented %s revoking %s\n",
		     cap->mds, cap, ceph_cap_string(cap_used),
		     ceph_cap_string(cap->issued),
		     ceph_cap_string(cap->implemented),
		     ceph_cap_string(revoking));

		if (cap == ci->i_auth_cap &&
		    (cap->issued & CEPH_CAP_FILE_WR)) {
			/* request larger max_size from MDS? */
			if (ci->i_wanted_max_size > ci->i_max_size &&
			    ci->i_wanted_max_size > ci->i_requested_max_size) {
				dout("requesting new max_size\n");
				goto ack;
			}

			/* approaching file_max? */
			if (__ceph_should_report_size(ci)) {
				dout("i_size approaching max_size\n");
				goto ack;
			}
		}
		/* flush anything dirty? */
		if (cap == ci->i_auth_cap) {
			if ((flags & CHECK_CAPS_FLUSH) && ci->i_dirty_caps) {
				dout("flushing dirty caps\n");
				goto ack;
			}
			if (ci->i_ceph_flags & CEPH_I_FLUSH_SNAPS) {
				dout("flushing snap caps\n");
				goto ack;
			}
		}

		/* completed revocation? going down and there are no caps? */
		if (revoking && (revoking & cap_used) == 0) {
			dout("completed revocation of %s\n",
			     ceph_cap_string(cap->implemented & ~cap->issued));
			goto ack;
		}

		/* want more caps from mds? */
		if (want & ~cap->mds_wanted) {
			if (want & ~(cap->mds_wanted | cap->issued))
				goto ack;
			if (!__cap_is_valid(cap))
				goto ack;
		}

		/* things we might delay */
		if ((cap->issued & ~retain) == 0)
			continue;     /* nope, all good */

ack:
		ceph_put_mds_session(session);
		session = ceph_get_mds_session(cap->session);

		/* kick flushing and flush snaps before sending normal
		 * cap message */
		if (cap == ci->i_auth_cap &&
		    (ci->i_ceph_flags &
		     (CEPH_I_KICK_FLUSH | CEPH_I_FLUSH_SNAPS))) {
			if (ci->i_ceph_flags & CEPH_I_KICK_FLUSH)
				__kick_flushing_caps(mdsc, session, ci, 0);
			if (ci->i_ceph_flags & CEPH_I_FLUSH_SNAPS)
				__ceph_flush_snaps(ci, session);

			goto retry;
		}

		if (cap == ci->i_auth_cap && ci->i_dirty_caps) {
			flushing = ci->i_dirty_caps;
			flush_tid = __mark_caps_flushing(inode, session, false,
							 &oldest_flush_tid);
			if (flags & CHECK_CAPS_FLUSH &&
			    list_empty(&session->s_cap_dirty))
				mflags |= CEPH_CLIENT_CAPS_SYNC;
		} else {
			flushing = 0;
			flush_tid = 0;
			spin_lock(&mdsc->cap_dirty_lock);
			oldest_flush_tid = __get_oldest_flush_tid(mdsc);
			spin_unlock(&mdsc->cap_dirty_lock);
		}

		mds = cap->mds;  /* remember mds, so we don't repeat */

		__prep_cap(&arg, cap, CEPH_CAP_OP_UPDATE, mflags, cap_used,
			   want, retain, flushing, flush_tid, oldest_flush_tid);

		spin_unlock(&ci->i_ceph_lock);
		__send_cap(&arg, ci);
		spin_lock(&ci->i_ceph_lock);

		goto retry; /* retake i_ceph_lock and restart our cap scan. */
	}

	/* periodically re-calculate caps wanted by open files */
	if (__ceph_is_any_real_caps(ci) &&
	    list_empty(&ci->i_cap_delay_list) &&
	    (file_wanted & ~CEPH_CAP_PIN) &&
	    !(used & (CEPH_CAP_FILE_RD | CEPH_CAP_ANY_FILE_WR))) {
		__cap_delay_requeue(mdsc, ci);
	}

	spin_unlock(&ci->i_ceph_lock);

	ceph_put_mds_session(session);
	if (queue_invalidate)
		ceph_queue_invalidate(inode);
}

/*
 * Try to flush dirty caps back to the auth mds.
 */
static int try_flush_caps(struct inode *inode, u64 *ptid)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;
	struct ceph_inode_info *ci = ceph_inode(inode);
	int flushing = 0;
	u64 flush_tid = 0, oldest_flush_tid = 0;

	spin_lock(&ci->i_ceph_lock);
retry_locked:
	if (ci->i_dirty_caps && ci->i_auth_cap) {
		struct ceph_cap *cap = ci->i_auth_cap;
		struct cap_msg_args arg;
		struct ceph_mds_session *session = cap->session;

		if (session->s_state < CEPH_MDS_SESSION_OPEN) {
			spin_unlock(&ci->i_ceph_lock);
			goto out;
		}

		if (ci->i_ceph_flags &
		    (CEPH_I_KICK_FLUSH | CEPH_I_FLUSH_SNAPS)) {
			if (ci->i_ceph_flags & CEPH_I_KICK_FLUSH)
				__kick_flushing_caps(mdsc, session, ci, 0);
			if (ci->i_ceph_flags & CEPH_I_FLUSH_SNAPS)
				__ceph_flush_snaps(ci, session);
			goto retry_locked;
		}

		flushing = ci->i_dirty_caps;
		flush_tid = __mark_caps_flushing(inode, session, true,
						 &oldest_flush_tid);

		__prep_cap(&arg, cap, CEPH_CAP_OP_FLUSH, CEPH_CLIENT_CAPS_SYNC,
			   __ceph_caps_used(ci), __ceph_caps_wanted(ci),
			   (cap->issued | cap->implemented),
			   flushing, flush_tid, oldest_flush_tid);
		spin_unlock(&ci->i_ceph_lock);

		__send_cap(&arg, ci);
	} else {
		if (!list_empty(&ci->i_cap_flush_list)) {
			struct ceph_cap_flush *cf =
				list_last_entry(&ci->i_cap_flush_list,
						struct ceph_cap_flush, i_list);
			cf->wake = true;
			flush_tid = cf->tid;
		}
		flushing = ci->i_flushing_caps;
		spin_unlock(&ci->i_ceph_lock);
	}
out:
	*ptid = flush_tid;
	return flushing;
}

/*
 * Return true if we've flushed caps through the given flush_tid.
 */
static int caps_are_flushed(struct inode *inode, u64 flush_tid)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int ret = 1;

	spin_lock(&ci->i_ceph_lock);
	if (!list_empty(&ci->i_cap_flush_list)) {
		struct ceph_cap_flush * cf =
			list_first_entry(&ci->i_cap_flush_list,
					 struct ceph_cap_flush, i_list);
		if (cf->tid <= flush_tid)
			ret = 0;
	}
	spin_unlock(&ci->i_ceph_lock);
	return ret;
}

/*
 * wait for any unsafe requests to complete.
 */
static int unsafe_request_wait(struct inode *inode)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_mds_request *req1 = NULL, *req2 = NULL;
	unsigned int max_sessions;
	int ret, err = 0;

	spin_lock(&ci->i_unsafe_lock);
	if (S_ISDIR(inode->i_mode) && !list_empty(&ci->i_unsafe_dirops)) {
		req1 = list_last_entry(&ci->i_unsafe_dirops,
					struct ceph_mds_request,
					r_unsafe_dir_item);
		ceph_mdsc_get_request(req1);
	}
	if (!list_empty(&ci->i_unsafe_iops)) {
		req2 = list_last_entry(&ci->i_unsafe_iops,
					struct ceph_mds_request,
					r_unsafe_target_item);
		ceph_mdsc_get_request(req2);
	}
	spin_unlock(&ci->i_unsafe_lock);

	/*
	 * The mdsc->max_sessions is unlikely to be changed
	 * mostly, here we will retry it by reallocating the
	 * sessions array memory to get rid of the mdsc->mutex
	 * lock.
	 */
retry:
	max_sessions = mdsc->max_sessions;

	/*
	 * Trigger to flush the journal logs in all the relevant MDSes
	 * manually, or in the worst case we must wait at most 5 seconds
	 * to wait the journal logs to be flushed by the MDSes periodically.
	 */
	if ((req1 || req2) && likely(max_sessions)) {
		struct ceph_mds_session **sessions = NULL;
		struct ceph_mds_session *s;
		struct ceph_mds_request *req;
		int i;

		sessions = kzalloc(max_sessions * sizeof(s), GFP_KERNEL);
		if (!sessions) {
			err = -ENOMEM;
			goto out;
		}

		spin_lock(&ci->i_unsafe_lock);
		if (req1) {
			list_for_each_entry(req, &ci->i_unsafe_dirops,
					    r_unsafe_dir_item) {
				s = req->r_session;
				if (!s)
					continue;
				if (unlikely(s->s_mds >= max_sessions)) {
					spin_unlock(&ci->i_unsafe_lock);
					for (i = 0; i < max_sessions; i++) {
						s = sessions[i];
						if (s)
							ceph_put_mds_session(s);
					}
					kfree(sessions);
					goto retry;
				}
				if (!sessions[s->s_mds]) {
					s = ceph_get_mds_session(s);
					sessions[s->s_mds] = s;
				}
			}
		}
		if (req2) {
			list_for_each_entry(req, &ci->i_unsafe_iops,
					    r_unsafe_target_item) {
				s = req->r_session;
				if (!s)
					continue;
				if (unlikely(s->s_mds >= max_sessions)) {
					spin_unlock(&ci->i_unsafe_lock);
					for (i = 0; i < max_sessions; i++) {
						s = sessions[i];
						if (s)
							ceph_put_mds_session(s);
					}
					kfree(sessions);
					goto retry;
				}
				if (!sessions[s->s_mds]) {
					s = ceph_get_mds_session(s);
					sessions[s->s_mds] = s;
				}
			}
		}
		spin_unlock(&ci->i_unsafe_lock);

		/* the auth MDS */
		spin_lock(&ci->i_ceph_lock);
		if (ci->i_auth_cap) {
		      s = ci->i_auth_cap->session;
		      if (!sessions[s->s_mds])
			      sessions[s->s_mds] = ceph_get_mds_session(s);
		}
		spin_unlock(&ci->i_ceph_lock);

		/* send flush mdlog request to MDSes */
		for (i = 0; i < max_sessions; i++) {
			s = sessions[i];
			if (s) {
				send_flush_mdlog(s);
				ceph_put_mds_session(s);
			}
		}
		kfree(sessions);
	}

	dout("unsafe_request_wait %p wait on tid %llu %llu\n",
	     inode, req1 ? req1->r_tid : 0ULL, req2 ? req2->r_tid : 0ULL);
	if (req1) {
		ret = !wait_for_completion_timeout(&req1->r_safe_completion,
					ceph_timeout_jiffies(req1->r_timeout));
		if (ret)
			err = -EIO;
	}
	if (req2) {
		ret = !wait_for_completion_timeout(&req2->r_safe_completion,
					ceph_timeout_jiffies(req2->r_timeout));
		if (ret)
			err = -EIO;
	}

out:
	if (req1)
		ceph_mdsc_put_request(req1);
	if (req2)
		ceph_mdsc_put_request(req2);
	return err;
}

int ceph_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = file->f_mapping->host;
	struct ceph_inode_info *ci = ceph_inode(inode);
	u64 flush_tid;
	int ret, err;
	int dirty;

	dout("fsync %p%s\n", inode, datasync ? " datasync" : "");

	ret = file_write_and_wait_range(file, start, end);
	if (datasync)
		goto out;

	ret = ceph_wait_on_async_create(inode);
	if (ret)
		goto out;

	dirty = try_flush_caps(inode, &flush_tid);
	dout("fsync dirty caps are %s\n", ceph_cap_string(dirty));

	err = unsafe_request_wait(inode);

	/*
	 * only wait on non-file metadata writeback (the mds
	 * can recover size and mtime, so we don't need to
	 * wait for that)
	 */
	if (!err && (dirty & ~CEPH_CAP_ANY_FILE_WR)) {
		err = wait_event_interruptible(ci->i_cap_wq,
					caps_are_flushed(inode, flush_tid));
	}

	if (err < 0)
		ret = err;

	err = file_check_and_advance_wb_err(file);
	if (err < 0)
		ret = err;
out:
	dout("fsync %p%s result=%d\n", inode, datasync ? " datasync" : "", ret);
	return ret;
}

/*
 * Flush any dirty caps back to the mds.  If we aren't asked to wait,
 * queue inode for flush but don't do so immediately, because we can
 * get by with fewer MDS messages if we wait for data writeback to
 * complete first.
 */
int ceph_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	u64 flush_tid;
	int err = 0;
	int dirty;
	int wait = (wbc->sync_mode == WB_SYNC_ALL && !wbc->for_sync);

	dout("write_inode %p wait=%d\n", inode, wait);
	if (wait) {
		dirty = try_flush_caps(inode, &flush_tid);
		if (dirty)
			err = wait_event_interruptible(ci->i_cap_wq,
				       caps_are_flushed(inode, flush_tid));
	} else {
		struct ceph_mds_client *mdsc =
			ceph_sb_to_client(inode->i_sb)->mdsc;

		spin_lock(&ci->i_ceph_lock);
		if (__ceph_caps_dirty(ci))
			__cap_delay_requeue_front(mdsc, ci);
		spin_unlock(&ci->i_ceph_lock);
	}
	return err;
}

static void __kick_flushing_caps(struct ceph_mds_client *mdsc,
				 struct ceph_mds_session *session,
				 struct ceph_inode_info *ci,
				 u64 oldest_flush_tid)
	__releases(ci->i_ceph_lock)
	__acquires(ci->i_ceph_lock)
{
	struct inode *inode = &ci->vfs_inode;
	struct ceph_cap *cap;
	struct ceph_cap_flush *cf;
	int ret;
	u64 first_tid = 0;
	u64 last_snap_flush = 0;

	ci->i_ceph_flags &= ~CEPH_I_KICK_FLUSH;

	list_for_each_entry_reverse(cf, &ci->i_cap_flush_list, i_list) {
		if (cf->is_capsnap) {
			last_snap_flush = cf->tid;
			break;
		}
	}

	list_for_each_entry(cf, &ci->i_cap_flush_list, i_list) {
		if (cf->tid < first_tid)
			continue;

		cap = ci->i_auth_cap;
		if (!(cap && cap->session == session)) {
			pr_err("%p auth cap %p not mds%d ???\n",
			       inode, cap, session->s_mds);
			break;
		}

		first_tid = cf->tid + 1;

		if (!cf->is_capsnap) {
			struct cap_msg_args arg;

			dout("kick_flushing_caps %p cap %p tid %llu %s\n",
			     inode, cap, cf->tid, ceph_cap_string(cf->caps));
			__prep_cap(&arg, cap, CEPH_CAP_OP_FLUSH,
					 (cf->tid < last_snap_flush ?
					  CEPH_CLIENT_CAPS_PENDING_CAPSNAP : 0),
					  __ceph_caps_used(ci),
					  __ceph_caps_wanted(ci),
					  (cap->issued | cap->implemented),
					  cf->caps, cf->tid, oldest_flush_tid);
			spin_unlock(&ci->i_ceph_lock);
			__send_cap(&arg, ci);
		} else {
			struct ceph_cap_snap *capsnap =
					container_of(cf, struct ceph_cap_snap,
						    cap_flush);
			dout("kick_flushing_caps %p capsnap %p tid %llu %s\n",
			     inode, capsnap, cf->tid,
			     ceph_cap_string(capsnap->dirty));

			refcount_inc(&capsnap->nref);
			spin_unlock(&ci->i_ceph_lock);

			ret = __send_flush_snap(inode, session, capsnap, cap->mseq,
						oldest_flush_tid);
			if (ret < 0) {
				pr_err("kick_flushing_caps: error sending "
					"cap flushsnap, ino (%llx.%llx) "
					"tid %llu follows %llu\n",
					ceph_vinop(inode), cf->tid,
					capsnap->follows);
			}

			ceph_put_cap_snap(capsnap);
		}

		spin_lock(&ci->i_ceph_lock);
	}
}

void ceph_early_kick_flushing_caps(struct ceph_mds_client *mdsc,
				   struct ceph_mds_session *session)
{
	struct ceph_inode_info *ci;
	struct ceph_cap *cap;
	u64 oldest_flush_tid;

	dout("early_kick_flushing_caps mds%d\n", session->s_mds);

	spin_lock(&mdsc->cap_dirty_lock);
	oldest_flush_tid = __get_oldest_flush_tid(mdsc);
	spin_unlock(&mdsc->cap_dirty_lock);

	list_for_each_entry(ci, &session->s_cap_flushing, i_flushing_item) {
		spin_lock(&ci->i_ceph_lock);
		cap = ci->i_auth_cap;
		if (!(cap && cap->session == session)) {
			pr_err("%p auth cap %p not mds%d ???\n",
				&ci->vfs_inode, cap, session->s_mds);
			spin_unlock(&ci->i_ceph_lock);
			continue;
		}


		/*
		 * if flushing caps were revoked, we re-send the cap flush
		 * in client reconnect stage. This guarantees MDS * processes
		 * the cap flush message before issuing the flushing caps to
		 * other client.
		 */
		if ((cap->issued & ci->i_flushing_caps) !=
		    ci->i_flushing_caps) {
			/* encode_caps_cb() also will reset these sequence
			 * numbers. make sure sequence numbers in cap flush
			 * message match later reconnect message */
			cap->seq = 0;
			cap->issue_seq = 0;
			cap->mseq = 0;
			__kick_flushing_caps(mdsc, session, ci,
					     oldest_flush_tid);
		} else {
			ci->i_ceph_flags |= CEPH_I_KICK_FLUSH;
		}

		spin_unlock(&ci->i_ceph_lock);
	}
}

void ceph_kick_flushing_caps(struct ceph_mds_client *mdsc,
			     struct ceph_mds_session *session)
{
	struct ceph_inode_info *ci;
	struct ceph_cap *cap;
	u64 oldest_flush_tid;

	lockdep_assert_held(&session->s_mutex);

	dout("kick_flushing_caps mds%d\n", session->s_mds);

	spin_lock(&mdsc->cap_dirty_lock);
	oldest_flush_tid = __get_oldest_flush_tid(mdsc);
	spin_unlock(&mdsc->cap_dirty_lock);

	list_for_each_entry(ci, &session->s_cap_flushing, i_flushing_item) {
		spin_lock(&ci->i_ceph_lock);
		cap = ci->i_auth_cap;
		if (!(cap && cap->session == session)) {
			pr_err("%p auth cap %p not mds%d ???\n",
				&ci->vfs_inode, cap, session->s_mds);
			spin_unlock(&ci->i_ceph_lock);
			continue;
		}
		if (ci->i_ceph_flags & CEPH_I_KICK_FLUSH) {
			__kick_flushing_caps(mdsc, session, ci,
					     oldest_flush_tid);
		}
		spin_unlock(&ci->i_ceph_lock);
	}
}

void ceph_kick_flushing_inode_caps(struct ceph_mds_session *session,
				   struct ceph_inode_info *ci)
{
	struct ceph_mds_client *mdsc = session->s_mdsc;
	struct ceph_cap *cap = ci->i_auth_cap;

	lockdep_assert_held(&ci->i_ceph_lock);

	dout("%s %p flushing %s\n", __func__, &ci->vfs_inode,
	     ceph_cap_string(ci->i_flushing_caps));

	if (!list_empty(&ci->i_cap_flush_list)) {
		u64 oldest_flush_tid;
		spin_lock(&mdsc->cap_dirty_lock);
		list_move_tail(&ci->i_flushing_item,
			       &cap->session->s_cap_flushing);
		oldest_flush_tid = __get_oldest_flush_tid(mdsc);
		spin_unlock(&mdsc->cap_dirty_lock);

		__kick_flushing_caps(mdsc, session, ci, oldest_flush_tid);
	}
}


/*
 * Take references to capabilities we hold, so that we don't release
 * them to the MDS prematurely.
 */
void ceph_take_cap_refs(struct ceph_inode_info *ci, int got,
			    bool snap_rwsem_locked)
{
	lockdep_assert_held(&ci->i_ceph_lock);

	if (got & CEPH_CAP_PIN)
		ci->i_pin_ref++;
	if (got & CEPH_CAP_FILE_RD)
		ci->i_rd_ref++;
	if (got & CEPH_CAP_FILE_CACHE)
		ci->i_rdcache_ref++;
	if (got & CEPH_CAP_FILE_EXCL)
		ci->i_fx_ref++;
	if (got & CEPH_CAP_FILE_WR) {
		if (ci->i_wr_ref == 0 && !ci->i_head_snapc) {
			BUG_ON(!snap_rwsem_locked);
			ci->i_head_snapc = ceph_get_snap_context(
					ci->i_snap_realm->cached_context);
		}
		ci->i_wr_ref++;
	}
	if (got & CEPH_CAP_FILE_BUFFER) {
		if (ci->i_wb_ref == 0)
			ihold(&ci->vfs_inode);
		ci->i_wb_ref++;
		dout("%s %p wb %d -> %d (?)\n", __func__,
		     &ci->vfs_inode, ci->i_wb_ref-1, ci->i_wb_ref);
	}
}

/*
 * Try to grab cap references.  Specify those refs we @want, and the
 * minimal set we @need.  Also include the larger offset we are writing
 * to (when applicable), and check against max_size here as well.
 * Note that caller is responsible for ensuring max_size increases are
 * requested from the MDS.
 *
 * Returns 0 if caps were not able to be acquired (yet), 1 if succeed,
 * or a negative error code. There are 3 speical error codes:
 *  -EAGAIN: need to sleep but non-blocking is specified
 *  -EFBIG:  ask caller to call check_max_size() and try again.
 *  -ESTALE: ask caller to call ceph_renew_caps() and try again.
 */
enum {
	/* first 8 bits are reserved for CEPH_FILE_MODE_FOO */
	NON_BLOCKING	= (1 << 8),
	CHECK_FILELOCK	= (1 << 9),
};

static int try_get_cap_refs(struct inode *inode, int need, int want,
			    loff_t endoff, int flags, int *got)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_mds_client *mdsc = ceph_inode_to_client(inode)->mdsc;
	int ret = 0;
	int have, implemented;
	bool snap_rwsem_locked = false;

	dout("get_cap_refs %p need %s want %s\n", inode,
	     ceph_cap_string(need), ceph_cap_string(want));

again:
	spin_lock(&ci->i_ceph_lock);

	if ((flags & CHECK_FILELOCK) &&
	    (ci->i_ceph_flags & CEPH_I_ERROR_FILELOCK)) {
		dout("try_get_cap_refs %p error filelock\n", inode);
		ret = -EIO;
		goto out_unlock;
	}

	/* finish pending truncate */
	while (ci->i_truncate_pending) {
		spin_unlock(&ci->i_ceph_lock);
		if (snap_rwsem_locked) {
			up_read(&mdsc->snap_rwsem);
			snap_rwsem_locked = false;
		}
		__ceph_do_pending_vmtruncate(inode);
		spin_lock(&ci->i_ceph_lock);
	}

	have = __ceph_caps_issued(ci, &implemented);

	if (have & need & CEPH_CAP_FILE_WR) {
		if (endoff >= 0 && endoff > (loff_t)ci->i_max_size) {
			dout("get_cap_refs %p endoff %llu > maxsize %llu\n",
			     inode, endoff, ci->i_max_size);
			if (endoff > ci->i_requested_max_size)
				ret = ci->i_auth_cap ? -EFBIG : -ESTALE;
			goto out_unlock;
		}
		/*
		 * If a sync write is in progress, we must wait, so that we
		 * can get a final snapshot value for size+mtime.
		 */
		if (__ceph_have_pending_cap_snap(ci)) {
			dout("get_cap_refs %p cap_snap_pending\n", inode);
			goto out_unlock;
		}
	}

	if ((have & need) == need) {
		/*
		 * Look at (implemented & ~have & not) so that we keep waiting
		 * on transition from wanted -> needed caps.  This is needed
		 * for WRBUFFER|WR -> WR to avoid a new WR sync write from
		 * going before a prior buffered writeback happens.
		 */
		int not = want & ~(have & need);
		int revoking = implemented & ~have;
		dout("get_cap_refs %p have %s but not %s (revoking %s)\n",
		     inode, ceph_cap_string(have), ceph_cap_string(not),
		     ceph_cap_string(revoking));
		if ((revoking & not) == 0) {
			if (!snap_rwsem_locked &&
			    !ci->i_head_snapc &&
			    (need & CEPH_CAP_FILE_WR)) {
				if (!down_read_trylock(&mdsc->snap_rwsem)) {
					/*
					 * we can not call down_read() when
					 * task isn't in TASK_RUNNING state
					 */
					if (flags & NON_BLOCKING) {
						ret = -EAGAIN;
						goto out_unlock;
					}

					spin_unlock(&ci->i_ceph_lock);
					down_read(&mdsc->snap_rwsem);
					snap_rwsem_locked = true;
					goto again;
				}
				snap_rwsem_locked = true;
			}
			if ((have & want) == want)
				*got = need | want;
			else
				*got = need;
			ceph_take_cap_refs(ci, *got, true);
			ret = 1;
		}
	} else {
		int session_readonly = false;
		int mds_wanted;
		if (ci->i_auth_cap &&
		    (need & (CEPH_CAP_FILE_WR | CEPH_CAP_FILE_EXCL))) {
			struct ceph_mds_session *s = ci->i_auth_cap->session;
			spin_lock(&s->s_cap_lock);
			session_readonly = s->s_readonly;
			spin_unlock(&s->s_cap_lock);
		}
		if (session_readonly) {
			dout("get_cap_refs %p need %s but mds%d readonly\n",
			     inode, ceph_cap_string(need), ci->i_auth_cap->mds);
			ret = -EROFS;
			goto out_unlock;
		}

		if (READ_ONCE(mdsc->fsc->mount_state) >= CEPH_MOUNT_SHUTDOWN) {
			dout("get_cap_refs %p forced umount\n", inode);
			ret = -EIO;
			goto out_unlock;
		}
		mds_wanted = __ceph_caps_mds_wanted(ci, false);
		if (need & ~mds_wanted) {
			dout("get_cap_refs %p need %s > mds_wanted %s\n",
			     inode, ceph_cap_string(need),
			     ceph_cap_string(mds_wanted));
			ret = -ESTALE;
			goto out_unlock;
		}

		dout("get_cap_refs %p have %s need %s\n", inode,
		     ceph_cap_string(have), ceph_cap_string(need));
	}
out_unlock:

	__ceph_touch_fmode(ci, mdsc, flags);

	spin_unlock(&ci->i_ceph_lock);
	if (snap_rwsem_locked)
		up_read(&mdsc->snap_rwsem);

	if (!ret)
		ceph_update_cap_mis(&mdsc->metric);
	else if (ret == 1)
		ceph_update_cap_hit(&mdsc->metric);

	dout("get_cap_refs %p ret %d got %s\n", inode,
	     ret, ceph_cap_string(*got));
	return ret;
}

/*
 * Check the offset we are writing up to against our current
 * max_size.  If necessary, tell the MDS we want to write to
 * a larger offset.
 */
static void check_max_size(struct inode *inode, loff_t endoff)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int check = 0;

	/* do we need to explicitly request a larger max_size? */
	spin_lock(&ci->i_ceph_lock);
	if (endoff >= ci->i_max_size && endoff > ci->i_wanted_max_size) {
		dout("write %p at large endoff %llu, req max_size\n",
		     inode, endoff);
		ci->i_wanted_max_size = endoff;
	}
	/* duplicate ceph_check_caps()'s logic */
	if (ci->i_auth_cap &&
	    (ci->i_auth_cap->issued & CEPH_CAP_FILE_WR) &&
	    ci->i_wanted_max_size > ci->i_max_size &&
	    ci->i_wanted_max_size > ci->i_requested_max_size)
		check = 1;
	spin_unlock(&ci->i_ceph_lock);
	if (check)
		ceph_check_caps(ci, CHECK_CAPS_AUTHONLY, NULL);
}

static inline int get_used_fmode(int caps)
{
	int fmode = 0;
	if (caps & CEPH_CAP_FILE_RD)
		fmode |= CEPH_FILE_MODE_RD;
	if (caps & CEPH_CAP_FILE_WR)
		fmode |= CEPH_FILE_MODE_WR;
	return fmode;
}

int ceph_try_get_caps(struct inode *inode, int need, int want,
		      bool nonblock, int *got)
{
	int ret, flags;

	BUG_ON(need & ~CEPH_CAP_FILE_RD);
	BUG_ON(want & ~(CEPH_CAP_FILE_CACHE | CEPH_CAP_FILE_LAZYIO |
			CEPH_CAP_FILE_SHARED | CEPH_CAP_FILE_EXCL |
			CEPH_CAP_ANY_DIR_OPS));
	if (need) {
		ret = ceph_pool_perm_check(inode, need);
		if (ret < 0)
			return ret;
	}

	flags = get_used_fmode(need | want);
	if (nonblock)
		flags |= NON_BLOCKING;

	ret = try_get_cap_refs(inode, need, want, 0, flags, got);
	/* three special error codes */
	if (ret == -EAGAIN || ret == -EFBIG || ret == -ESTALE)
		ret = 0;
	return ret;
}

/*
 * Wait for caps, and take cap references.  If we can't get a WR cap
 * due to a small max_size, make sure we check_max_size (and possibly
 * ask the mds) so we don't get hung up indefinitely.
 */
int ceph_get_caps(struct file *filp, int need, int want, loff_t endoff, int *got)
{
	struct ceph_file_info *fi = filp->private_data;
	struct inode *inode = file_inode(filp);
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);
	int ret, _got, flags;

	ret = ceph_pool_perm_check(inode, need);
	if (ret < 0)
		return ret;

	if ((fi->fmode & CEPH_FILE_MODE_WR) &&
	    fi->filp_gen != READ_ONCE(fsc->filp_gen))
		return -EBADF;

	flags = get_used_fmode(need | want);

	while (true) {
		flags &= CEPH_FILE_MODE_MASK;
		if (atomic_read(&fi->num_locks))
			flags |= CHECK_FILELOCK;
		_got = 0;
		ret = try_get_cap_refs(inode, need, want, endoff,
				       flags, &_got);
		WARN_ON_ONCE(ret == -EAGAIN);
		if (!ret) {
			struct ceph_mds_client *mdsc = fsc->mdsc;
			struct cap_wait cw;
			DEFINE_WAIT_FUNC(wait, woken_wake_function);

			cw.ino = ceph_ino(inode);
			cw.tgid = current->tgid;
			cw.need = need;
			cw.want = want;

			spin_lock(&mdsc->caps_list_lock);
			list_add(&cw.list, &mdsc->cap_wait_list);
			spin_unlock(&mdsc->caps_list_lock);

			/* make sure used fmode not timeout */
			ceph_get_fmode(ci, flags, FMODE_WAIT_BIAS);
			add_wait_queue(&ci->i_cap_wq, &wait);

			flags |= NON_BLOCKING;
			while (!(ret = try_get_cap_refs(inode, need, want,
							endoff, flags, &_got))) {
				if (signal_pending(current)) {
					ret = -ERESTARTSYS;
					break;
				}
				wait_woken(&wait, TASK_INTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
			}

			remove_wait_queue(&ci->i_cap_wq, &wait);
			ceph_put_fmode(ci, flags, FMODE_WAIT_BIAS);

			spin_lock(&mdsc->caps_list_lock);
			list_del(&cw.list);
			spin_unlock(&mdsc->caps_list_lock);

			if (ret == -EAGAIN)
				continue;
		}

		if ((fi->fmode & CEPH_FILE_MODE_WR) &&
		    fi->filp_gen != READ_ONCE(fsc->filp_gen)) {
			if (ret >= 0 && _got)
				ceph_put_cap_refs(ci, _got);
			return -EBADF;
		}

		if (ret < 0) {
			if (ret == -EFBIG || ret == -ESTALE) {
				int ret2 = ceph_wait_on_async_create(inode);
				if (ret2 < 0)
					return ret2;
			}
			if (ret == -EFBIG) {
				check_max_size(inode, endoff);
				continue;
			}
			if (ret == -ESTALE) {
				/* session was killed, try renew caps */
				ret = ceph_renew_caps(inode, flags);
				if (ret == 0)
					continue;
			}
			return ret;
		}

		if (S_ISREG(ci->vfs_inode.i_mode) &&
		    ci->i_inline_version != CEPH_INLINE_NONE &&
		    (_got & (CEPH_CAP_FILE_CACHE|CEPH_CAP_FILE_LAZYIO)) &&
		    i_size_read(inode) > 0) {
			struct page *page =
				find_get_page(inode->i_mapping, 0);
			if (page) {
				bool uptodate = PageUptodate(page);

				put_page(page);
				if (uptodate)
					break;
			}
			/*
			 * drop cap refs first because getattr while
			 * holding * caps refs can cause deadlock.
			 */
			ceph_put_cap_refs(ci, _got);
			_got = 0;

			/*
			 * getattr request will bring inline data into
			 * page cache
			 */
			ret = __ceph_do_getattr(inode, NULL,
						CEPH_STAT_CAP_INLINE_DATA,
						true);
			if (ret < 0)
				return ret;
			continue;
		}
		break;
	}
	*got = _got;
	return 0;
}

/*
 * Take cap refs.  Caller must already know we hold at least one ref
 * on the caps in question or we don't know this is safe.
 */
void ceph_get_cap_refs(struct ceph_inode_info *ci, int caps)
{
	spin_lock(&ci->i_ceph_lock);
	ceph_take_cap_refs(ci, caps, false);
	spin_unlock(&ci->i_ceph_lock);
}


/*
 * drop cap_snap that is not associated with any snapshot.
 * we don't need to send FLUSHSNAP message for it.
 */
static int ceph_try_drop_cap_snap(struct ceph_inode_info *ci,
				  struct ceph_cap_snap *capsnap)
{
	if (!capsnap->need_flush &&
	    !capsnap->writing && !capsnap->dirty_pages) {
		dout("dropping cap_snap %p follows %llu\n",
		     capsnap, capsnap->follows);
		BUG_ON(capsnap->cap_flush.tid > 0);
		ceph_put_snap_context(capsnap->context);
		if (!list_is_last(&capsnap->ci_item, &ci->i_cap_snaps))
			ci->i_ceph_flags |= CEPH_I_FLUSH_SNAPS;

		list_del(&capsnap->ci_item);
		ceph_put_cap_snap(capsnap);
		return 1;
	}
	return 0;
}

enum put_cap_refs_mode {
	PUT_CAP_REFS_SYNC = 0,
	PUT_CAP_REFS_NO_CHECK,
	PUT_CAP_REFS_ASYNC,
};

/*
 * Release cap refs.
 *
 * If we released the last ref on any given cap, call ceph_check_caps
 * to release (or schedule a release).
 *
 * If we are releasing a WR cap (from a sync write), finalize any affected
 * cap_snap, and wake up any waiters.
 */
static void __ceph_put_cap_refs(struct ceph_inode_info *ci, int had,
				enum put_cap_refs_mode mode)
{
	struct inode *inode = &ci->vfs_inode;
	int last = 0, put = 0, flushsnaps = 0, wake = 0;
	bool check_flushsnaps = false;

	spin_lock(&ci->i_ceph_lock);
	if (had & CEPH_CAP_PIN)
		--ci->i_pin_ref;
	if (had & CEPH_CAP_FILE_RD)
		if (--ci->i_rd_ref == 0)
			last++;
	if (had & CEPH_CAP_FILE_CACHE)
		if (--ci->i_rdcache_ref == 0)
			last++;
	if (had & CEPH_CAP_FILE_EXCL)
		if (--ci->i_fx_ref == 0)
			last++;
	if (had & CEPH_CAP_FILE_BUFFER) {
		if (--ci->i_wb_ref == 0) {
			last++;
			/* put the ref held by ceph_take_cap_refs() */
			put++;
			check_flushsnaps = true;
		}
		dout("put_cap_refs %p wb %d -> %d (?)\n",
		     inode, ci->i_wb_ref+1, ci->i_wb_ref);
	}
	if (had & CEPH_CAP_FILE_WR) {
		if (--ci->i_wr_ref == 0) {
			last++;
			check_flushsnaps = true;
			if (ci->i_wrbuffer_ref_head == 0 &&
			    ci->i_dirty_caps == 0 &&
			    ci->i_flushing_caps == 0) {
				BUG_ON(!ci->i_head_snapc);
				ceph_put_snap_context(ci->i_head_snapc);
				ci->i_head_snapc = NULL;
			}
			/* see comment in __ceph_remove_cap() */
			if (!__ceph_is_any_real_caps(ci) && ci->i_snap_realm)
				ceph_change_snap_realm(inode, NULL);
		}
	}
	if (check_flushsnaps && __ceph_have_pending_cap_snap(ci)) {
		struct ceph_cap_snap *capsnap =
			list_last_entry(&ci->i_cap_snaps,
					struct ceph_cap_snap,
					ci_item);

		capsnap->writing = 0;
		if (ceph_try_drop_cap_snap(ci, capsnap))
			/* put the ref held by ceph_queue_cap_snap() */
			put++;
		else if (__ceph_finish_cap_snap(ci, capsnap))
			flushsnaps = 1;
		wake = 1;
	}
	spin_unlock(&ci->i_ceph_lock);

	dout("put_cap_refs %p had %s%s%s\n", inode, ceph_cap_string(had),
	     last ? " last" : "", put ? " put" : "");

	switch (mode) {
	case PUT_CAP_REFS_SYNC:
		if (last)
			ceph_check_caps(ci, 0, NULL);
		else if (flushsnaps)
			ceph_flush_snaps(ci, NULL);
		break;
	case PUT_CAP_REFS_ASYNC:
		if (last)
			ceph_queue_check_caps(inode);
		else if (flushsnaps)
			ceph_queue_flush_snaps(inode);
		break;
	default:
		break;
	}
	if (wake)
		wake_up_all(&ci->i_cap_wq);
	while (put-- > 0)
		iput(inode);
}

void ceph_put_cap_refs(struct ceph_inode_info *ci, int had)
{
	__ceph_put_cap_refs(ci, had, PUT_CAP_REFS_SYNC);
}

void ceph_put_cap_refs_async(struct ceph_inode_info *ci, int had)
{
	__ceph_put_cap_refs(ci, had, PUT_CAP_REFS_ASYNC);
}

void ceph_put_cap_refs_no_check_caps(struct ceph_inode_info *ci, int had)
{
	__ceph_put_cap_refs(ci, had, PUT_CAP_REFS_NO_CHECK);
}

/*
 * Release @nr WRBUFFER refs on dirty pages for the given @snapc snap
 * context.  Adjust per-snap dirty page accounting as appropriate.
 * Once all dirty data for a cap_snap is flushed, flush snapped file
 * metadata back to the MDS.  If we dropped the last ref, call
 * ceph_check_caps.
 */
void ceph_put_wrbuffer_cap_refs(struct ceph_inode_info *ci, int nr,
				struct ceph_snap_context *snapc)
{
	struct inode *inode = &ci->vfs_inode;
	struct ceph_cap_snap *capsnap = NULL;
	int put = 0;
	bool last = false;
	bool found = false;
	bool flush_snaps = false;
	bool complete_capsnap = false;

	spin_lock(&ci->i_ceph_lock);
	ci->i_wrbuffer_ref -= nr;
	if (ci->i_wrbuffer_ref == 0) {
		last = true;
		put++;
	}

	if (ci->i_head_snapc == snapc) {
		ci->i_wrbuffer_ref_head -= nr;
		if (ci->i_wrbuffer_ref_head == 0 &&
		    ci->i_wr_ref == 0 &&
		    ci->i_dirty_caps == 0 &&
		    ci->i_flushing_caps == 0) {
			BUG_ON(!ci->i_head_snapc);
			ceph_put_snap_context(ci->i_head_snapc);
			ci->i_head_snapc = NULL;
		}
		dout("put_wrbuffer_cap_refs on %p head %d/%d -> %d/%d %s\n",
		     inode,
		     ci->i_wrbuffer_ref+nr, ci->i_wrbuffer_ref_head+nr,
		     ci->i_wrbuffer_ref, ci->i_wrbuffer_ref_head,
		     last ? " LAST" : "");
	} else {
		list_for_each_entry(capsnap, &ci->i_cap_snaps, ci_item) {
			if (capsnap->context == snapc) {
				found = true;
				break;
			}
		}

		if (!found) {
			/*
			 * The capsnap should already be removed when removing
			 * auth cap in the case of a forced unmount.
			 */
			WARN_ON_ONCE(ci->i_auth_cap);
			goto unlock;
		}

		capsnap->dirty_pages -= nr;
		if (capsnap->dirty_pages == 0) {
			complete_capsnap = true;
			if (!capsnap->writing) {
				if (ceph_try_drop_cap_snap(ci, capsnap)) {
					put++;
				} else {
					ci->i_ceph_flags |= CEPH_I_FLUSH_SNAPS;
					flush_snaps = true;
				}
			}
		}
		dout("put_wrbuffer_cap_refs on %p cap_snap %p "
		     " snap %lld %d/%d -> %d/%d %s%s\n",
		     inode, capsnap, capsnap->context->seq,
		     ci->i_wrbuffer_ref+nr, capsnap->dirty_pages + nr,
		     ci->i_wrbuffer_ref, capsnap->dirty_pages,
		     last ? " (wrbuffer last)" : "",
		     complete_capsnap ? " (complete capsnap)" : "");
	}

unlock:
	spin_unlock(&ci->i_ceph_lock);

	if (last) {
		ceph_check_caps(ci, 0, NULL);
	} else if (flush_snaps) {
		ceph_flush_snaps(ci, NULL);
	}
	if (complete_capsnap)
		wake_up_all(&ci->i_cap_wq);
	while (put-- > 0) {
		iput(inode);
	}
}

/*
 * Invalidate unlinked inode's aliases, so we can drop the inode ASAP.
 */
static void invalidate_aliases(struct inode *inode)
{
	struct dentry *dn, *prev = NULL;

	dout("invalidate_aliases inode %p\n", inode);
	d_prune_aliases(inode);
	/*
	 * For non-directory inode, d_find_alias() only returns
	 * hashed dentry. After calling d_invalidate(), the
	 * dentry becomes unhashed.
	 *
	 * For directory inode, d_find_alias() can return
	 * unhashed dentry. But directory inode should have
	 * one alias at most.
	 */
	while ((dn = d_find_alias(inode))) {
		if (dn == prev) {
			dput(dn);
			break;
		}
		d_invalidate(dn);
		if (prev)
			dput(prev);
		prev = dn;
	}
	if (prev)
		dput(prev);
}

struct cap_extra_info {
	struct ceph_string *pool_ns;
	/* inline data */
	u64 inline_version;
	void *inline_data;
	u32 inline_len;
	/* dirstat */
	bool dirstat_valid;
	u64 nfiles;
	u64 nsubdirs;
	u64 change_attr;
	/* currently issued */
	int issued;
	struct timespec64 btime;
};

/*
 * Handle a cap GRANT message from the MDS.  (Note that a GRANT may
 * actually be a revocation if it specifies a smaller cap set.)
 *
 * caller holds s_mutex and i_ceph_lock, we drop both.
 */
static void handle_cap_grant(struct inode *inode,
			     struct ceph_mds_session *session,
			     struct ceph_cap *cap,
			     struct ceph_mds_caps *grant,
			     struct ceph_buffer *xattr_buf,
			     struct cap_extra_info *extra_info)
	__releases(ci->i_ceph_lock)
	__releases(session->s_mdsc->snap_rwsem)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int seq = le32_to_cpu(grant->seq);
	int newcaps = le32_to_cpu(grant->caps);
	int used, wanted, dirty;
	u64 size = le64_to_cpu(grant->size);
	u64 max_size = le64_to_cpu(grant->max_size);
	unsigned char check_caps = 0;
	bool was_stale = cap->cap_gen < atomic_read(&session->s_cap_gen);
	bool wake = false;
	bool writeback = false;
	bool queue_trunc = false;
	bool queue_invalidate = false;
	bool deleted_inode = false;
	bool fill_inline = false;

	dout("handle_cap_grant inode %p cap %p mds%d seq %d %s\n",
	     inode, cap, session->s_mds, seq, ceph_cap_string(newcaps));
	dout(" size %llu max_size %llu, i_size %llu\n", size, max_size,
		i_size_read(inode));


	/*
	 * If CACHE is being revoked, and we have no dirty buffers,
	 * try to invalidate (once).  (If there are dirty buffers, we
	 * will invalidate _after_ writeback.)
	 */
	if (S_ISREG(inode->i_mode) && /* don't invalidate readdir cache */
	    ((cap->issued & ~newcaps) & CEPH_CAP_FILE_CACHE) &&
	    (newcaps & CEPH_CAP_FILE_LAZYIO) == 0 &&
	    !(ci->i_wrbuffer_ref || ci->i_wb_ref)) {
		if (try_nonblocking_invalidate(inode)) {
			/* there were locked pages.. invalidate later
			   in a separate thread. */
			if (ci->i_rdcache_revoking != ci->i_rdcache_gen) {
				queue_invalidate = true;
				ci->i_rdcache_revoking = ci->i_rdcache_gen;
			}
		}
	}

	if (was_stale)
		cap->issued = cap->implemented = CEPH_CAP_PIN;

	/*
	 * auth mds of the inode changed. we received the cap export message,
	 * but still haven't received the cap import message. handle_cap_export
	 * updated the new auth MDS' cap.
	 *
	 * "ceph_seq_cmp(seq, cap->seq) <= 0" means we are processing a message
	 * that was sent before the cap import message. So don't remove caps.
	 */
	if (ceph_seq_cmp(seq, cap->seq) <= 0) {
		WARN_ON(cap != ci->i_auth_cap);
		WARN_ON(cap->cap_id != le64_to_cpu(grant->cap_id));
		seq = cap->seq;
		newcaps |= cap->issued;
	}

	/* side effects now are allowed */
	cap->cap_gen = atomic_read(&session->s_cap_gen);
	cap->seq = seq;

	__check_cap_issue(ci, cap, newcaps);

	inode_set_max_iversion_raw(inode, extra_info->change_attr);

	if ((newcaps & CEPH_CAP_AUTH_SHARED) &&
	    (extra_info->issued & CEPH_CAP_AUTH_EXCL) == 0) {
		umode_t mode = le32_to_cpu(grant->mode);

		if (inode_wrong_type(inode, mode))
			pr_warn_once("inode type changed! (ino %llx.%llx is 0%o, mds says 0%o)\n",
				     ceph_vinop(inode), inode->i_mode, mode);
		else
			inode->i_mode = mode;
		inode->i_uid = make_kuid(&init_user_ns, le32_to_cpu(grant->uid));
		inode->i_gid = make_kgid(&init_user_ns, le32_to_cpu(grant->gid));
		ci->i_btime = extra_info->btime;
		dout("%p mode 0%o uid.gid %d.%d\n", inode, inode->i_mode,
		     from_kuid(&init_user_ns, inode->i_uid),
		     from_kgid(&init_user_ns, inode->i_gid));
	}

	if ((newcaps & CEPH_CAP_LINK_SHARED) &&
	    (extra_info->issued & CEPH_CAP_LINK_EXCL) == 0) {
		set_nlink(inode, le32_to_cpu(grant->nlink));
		if (inode->i_nlink == 0 &&
		    (newcaps & (CEPH_CAP_LINK_SHARED | CEPH_CAP_LINK_EXCL)))
			deleted_inode = true;
	}

	if ((extra_info->issued & CEPH_CAP_XATTR_EXCL) == 0 &&
	    grant->xattr_len) {
		int len = le32_to_cpu(grant->xattr_len);
		u64 version = le64_to_cpu(grant->xattr_version);

		if (version > ci->i_xattrs.version) {
			dout(" got new xattrs v%llu on %p len %d\n",
			     version, inode, len);
			if (ci->i_xattrs.blob)
				ceph_buffer_put(ci->i_xattrs.blob);
			ci->i_xattrs.blob = ceph_buffer_get(xattr_buf);
			ci->i_xattrs.version = version;
			ceph_forget_all_cached_acls(inode);
			ceph_security_invalidate_secctx(inode);
		}
	}

	if (newcaps & CEPH_CAP_ANY_RD) {
		struct timespec64 mtime, atime, ctime;
		/* ctime/mtime/atime? */
		ceph_decode_timespec64(&mtime, &grant->mtime);
		ceph_decode_timespec64(&atime, &grant->atime);
		ceph_decode_timespec64(&ctime, &grant->ctime);
		ceph_fill_file_time(inode, extra_info->issued,
				    le32_to_cpu(grant->time_warp_seq),
				    &ctime, &mtime, &atime);
	}

	if ((newcaps & CEPH_CAP_FILE_SHARED) && extra_info->dirstat_valid) {
		ci->i_files = extra_info->nfiles;
		ci->i_subdirs = extra_info->nsubdirs;
	}

	if (newcaps & (CEPH_CAP_ANY_FILE_RD | CEPH_CAP_ANY_FILE_WR)) {
		/* file layout may have changed */
		s64 old_pool = ci->i_layout.pool_id;
		struct ceph_string *old_ns;

		ceph_file_layout_from_legacy(&ci->i_layout, &grant->layout);
		old_ns = rcu_dereference_protected(ci->i_layout.pool_ns,
					lockdep_is_held(&ci->i_ceph_lock));
		rcu_assign_pointer(ci->i_layout.pool_ns, extra_info->pool_ns);

		if (ci->i_layout.pool_id != old_pool ||
		    extra_info->pool_ns != old_ns)
			ci->i_ceph_flags &= ~CEPH_I_POOL_PERM;

		extra_info->pool_ns = old_ns;

		/* size/truncate_seq? */
		queue_trunc = ceph_fill_file_size(inode, extra_info->issued,
					le32_to_cpu(grant->truncate_seq),
					le64_to_cpu(grant->truncate_size),
					size);
	}

	if (ci->i_auth_cap == cap && (newcaps & CEPH_CAP_ANY_FILE_WR)) {
		if (max_size != ci->i_max_size) {
			dout("max_size %lld -> %llu\n",
			     ci->i_max_size, max_size);
			ci->i_max_size = max_size;
			if (max_size >= ci->i_wanted_max_size) {
				ci->i_wanted_max_size = 0;  /* reset */
				ci->i_requested_max_size = 0;
			}
			wake = true;
		}
	}

	/* check cap bits */
	wanted = __ceph_caps_wanted(ci);
	used = __ceph_caps_used(ci);
	dirty = __ceph_caps_dirty(ci);
	dout(" my wanted = %s, used = %s, dirty %s\n",
	     ceph_cap_string(wanted),
	     ceph_cap_string(used),
	     ceph_cap_string(dirty));

	if ((was_stale || le32_to_cpu(grant->op) == CEPH_CAP_OP_IMPORT) &&
	    (wanted & ~(cap->mds_wanted | newcaps))) {
		/*
		 * If mds is importing cap, prior cap messages that update
		 * 'wanted' may get dropped by mds (migrate seq mismatch).
		 *
		 * We don't send cap message to update 'wanted' if what we
		 * want are already issued. If mds revokes caps, cap message
		 * that releases caps also tells mds what we want. But if
		 * caps got revoked by mds forcedly (session stale). We may
		 * haven't told mds what we want.
		 */
		check_caps = 1;
	}

	/* revocation, grant, or no-op? */
	if (cap->issued & ~newcaps) {
		int revoking = cap->issued & ~newcaps;

		dout("revocation: %s -> %s (revoking %s)\n",
		     ceph_cap_string(cap->issued),
		     ceph_cap_string(newcaps),
		     ceph_cap_string(revoking));
		if (S_ISREG(inode->i_mode) &&
		    (revoking & used & CEPH_CAP_FILE_BUFFER))
			writeback = true;  /* initiate writeback; will delay ack */
		else if (queue_invalidate &&
			 revoking == CEPH_CAP_FILE_CACHE &&
			 (newcaps & CEPH_CAP_FILE_LAZYIO) == 0)
			; /* do nothing yet, invalidation will be queued */
		else if (cap == ci->i_auth_cap)
			check_caps = 1; /* check auth cap only */
		else
			check_caps = 2; /* check all caps */
		cap->issued = newcaps;
		cap->implemented |= newcaps;
	} else if (cap->issued == newcaps) {
		dout("caps unchanged: %s -> %s\n",
		     ceph_cap_string(cap->issued), ceph_cap_string(newcaps));
	} else {
		dout("grant: %s -> %s\n", ceph_cap_string(cap->issued),
		     ceph_cap_string(newcaps));
		/* non-auth MDS is revoking the newly grant caps ? */
		if (cap == ci->i_auth_cap &&
		    __ceph_caps_revoking_other(ci, cap, newcaps))
		    check_caps = 2;

		cap->issued = newcaps;
		cap->implemented |= newcaps; /* add bits only, to
					      * avoid stepping on a
					      * pending revocation */
		wake = true;
	}
	BUG_ON(cap->issued & ~cap->implemented);

	if (extra_info->inline_version > 0 &&
	    extra_info->inline_version >= ci->i_inline_version) {
		ci->i_inline_version = extra_info->inline_version;
		if (ci->i_inline_version != CEPH_INLINE_NONE &&
		    (newcaps & (CEPH_CAP_FILE_CACHE|CEPH_CAP_FILE_LAZYIO)))
			fill_inline = true;
	}

	if (ci->i_auth_cap == cap &&
	    le32_to_cpu(grant->op) == CEPH_CAP_OP_IMPORT) {
		if (newcaps & ~extra_info->issued)
			wake = true;

		if (ci->i_requested_max_size > max_size ||
		    !(le32_to_cpu(grant->wanted) & CEPH_CAP_ANY_FILE_WR)) {
			/* re-request max_size if necessary */
			ci->i_requested_max_size = 0;
			wake = true;
		}

		ceph_kick_flushing_inode_caps(session, ci);
		spin_unlock(&ci->i_ceph_lock);
		up_read(&session->s_mdsc->snap_rwsem);
	} else {
		spin_unlock(&ci->i_ceph_lock);
	}

	if (fill_inline)
		ceph_fill_inline_data(inode, NULL, extra_info->inline_data,
				      extra_info->inline_len);

	if (queue_trunc)
		ceph_queue_vmtruncate(inode);

	if (writeback)
		/*
		 * queue inode for writeback: we can't actually call
		 * filemap_write_and_wait, etc. from message handler
		 * context.
		 */
		ceph_queue_writeback(inode);
	if (queue_invalidate)
		ceph_queue_invalidate(inode);
	if (deleted_inode)
		invalidate_aliases(inode);
	if (wake)
		wake_up_all(&ci->i_cap_wq);

	mutex_unlock(&session->s_mutex);
	if (check_caps == 1)
		ceph_check_caps(ci, CHECK_CAPS_AUTHONLY | CHECK_CAPS_NOINVAL,
				session);
	else if (check_caps == 2)
		ceph_check_caps(ci, CHECK_CAPS_NOINVAL, session);
}

/*
 * Handle FLUSH_ACK from MDS, indicating that metadata we sent to the
 * MDS has been safely committed.
 */
static void handle_cap_flush_ack(struct inode *inode, u64 flush_tid,
				 struct ceph_mds_caps *m,
				 struct ceph_mds_session *session,
				 struct ceph_cap *cap)
	__releases(ci->i_ceph_lock)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;
	struct ceph_cap_flush *cf, *tmp_cf;
	LIST_HEAD(to_remove);
	unsigned seq = le32_to_cpu(m->seq);
	int dirty = le32_to_cpu(m->dirty);
	int cleaned = 0;
	bool drop = false;
	bool wake_ci = false;
	bool wake_mdsc = false;

	list_for_each_entry_safe(cf, tmp_cf, &ci->i_cap_flush_list, i_list) {
		/* Is this the one that was flushed? */
		if (cf->tid == flush_tid)
			cleaned = cf->caps;

		/* Is this a capsnap? */
		if (cf->is_capsnap)
			continue;

		if (cf->tid <= flush_tid) {
			/*
			 * An earlier or current tid. The FLUSH_ACK should
			 * represent a superset of this flush's caps.
			 */
			wake_ci |= __detach_cap_flush_from_ci(ci, cf);
			list_add_tail(&cf->i_list, &to_remove);
		} else {
			/*
			 * This is a later one. Any caps in it are still dirty
			 * so don't count them as cleaned.
			 */
			cleaned &= ~cf->caps;
			if (!cleaned)
				break;
		}
	}

	dout("handle_cap_flush_ack inode %p mds%d seq %d on %s cleaned %s,"
	     " flushing %s -> %s\n",
	     inode, session->s_mds, seq, ceph_cap_string(dirty),
	     ceph_cap_string(cleaned), ceph_cap_string(ci->i_flushing_caps),
	     ceph_cap_string(ci->i_flushing_caps & ~cleaned));

	if (list_empty(&to_remove) && !cleaned)
		goto out;

	ci->i_flushing_caps &= ~cleaned;

	spin_lock(&mdsc->cap_dirty_lock);

	list_for_each_entry(cf, &to_remove, i_list)
		wake_mdsc |= __detach_cap_flush_from_mdsc(mdsc, cf);

	if (ci->i_flushing_caps == 0) {
		if (list_empty(&ci->i_cap_flush_list)) {
			list_del_init(&ci->i_flushing_item);
			if (!list_empty(&session->s_cap_flushing)) {
				dout(" mds%d still flushing cap on %p\n",
				     session->s_mds,
				     &list_first_entry(&session->s_cap_flushing,
						struct ceph_inode_info,
						i_flushing_item)->vfs_inode);
			}
		}
		mdsc->num_cap_flushing--;
		dout(" inode %p now !flushing\n", inode);

		if (ci->i_dirty_caps == 0) {
			dout(" inode %p now clean\n", inode);
			BUG_ON(!list_empty(&ci->i_dirty_item));
			drop = true;
			if (ci->i_wr_ref == 0 &&
			    ci->i_wrbuffer_ref_head == 0) {
				BUG_ON(!ci->i_head_snapc);
				ceph_put_snap_context(ci->i_head_snapc);
				ci->i_head_snapc = NULL;
			}
		} else {
			BUG_ON(list_empty(&ci->i_dirty_item));
		}
	}
	spin_unlock(&mdsc->cap_dirty_lock);

out:
	spin_unlock(&ci->i_ceph_lock);

	while (!list_empty(&to_remove)) {
		cf = list_first_entry(&to_remove,
				      struct ceph_cap_flush, i_list);
		list_del_init(&cf->i_list);
		if (!cf->is_capsnap)
			ceph_free_cap_flush(cf);
	}

	if (wake_ci)
		wake_up_all(&ci->i_cap_wq);
	if (wake_mdsc)
		wake_up_all(&mdsc->cap_flushing_wq);
	if (drop)
		iput(inode);
}

void __ceph_remove_capsnap(struct inode *inode, struct ceph_cap_snap *capsnap,
			   bool *wake_ci, bool *wake_mdsc)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;
	bool ret;

	lockdep_assert_held(&ci->i_ceph_lock);

	dout("removing capsnap %p, inode %p ci %p\n", capsnap, inode, ci);

	list_del_init(&capsnap->ci_item);
	ret = __detach_cap_flush_from_ci(ci, &capsnap->cap_flush);
	if (wake_ci)
		*wake_ci = ret;

	spin_lock(&mdsc->cap_dirty_lock);
	if (list_empty(&ci->i_cap_flush_list))
		list_del_init(&ci->i_flushing_item);

	ret = __detach_cap_flush_from_mdsc(mdsc, &capsnap->cap_flush);
	if (wake_mdsc)
		*wake_mdsc = ret;
	spin_unlock(&mdsc->cap_dirty_lock);
}

void ceph_remove_capsnap(struct inode *inode, struct ceph_cap_snap *capsnap,
			 bool *wake_ci, bool *wake_mdsc)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	lockdep_assert_held(&ci->i_ceph_lock);

	WARN_ON_ONCE(capsnap->dirty_pages || capsnap->writing);
	__ceph_remove_capsnap(inode, capsnap, wake_ci, wake_mdsc);
}

/*
 * Handle FLUSHSNAP_ACK.  MDS has flushed snap data to disk and we can
 * throw away our cap_snap.
 *
 * Caller hold s_mutex.
 */
static void handle_cap_flushsnap_ack(struct inode *inode, u64 flush_tid,
				     struct ceph_mds_caps *m,
				     struct ceph_mds_session *session)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;
	u64 follows = le64_to_cpu(m->snap_follows);
	struct ceph_cap_snap *capsnap;
	bool flushed = false;
	bool wake_ci = false;
	bool wake_mdsc = false;

	dout("handle_cap_flushsnap_ack inode %p ci %p mds%d follows %lld\n",
	     inode, ci, session->s_mds, follows);

	spin_lock(&ci->i_ceph_lock);
	list_for_each_entry(capsnap, &ci->i_cap_snaps, ci_item) {
		if (capsnap->follows == follows) {
			if (capsnap->cap_flush.tid != flush_tid) {
				dout(" cap_snap %p follows %lld tid %lld !="
				     " %lld\n", capsnap, follows,
				     flush_tid, capsnap->cap_flush.tid);
				break;
			}
			flushed = true;
			break;
		} else {
			dout(" skipping cap_snap %p follows %lld\n",
			     capsnap, capsnap->follows);
		}
	}
	if (flushed)
		ceph_remove_capsnap(inode, capsnap, &wake_ci, &wake_mdsc);
	spin_unlock(&ci->i_ceph_lock);

	if (flushed) {
		ceph_put_snap_context(capsnap->context);
		ceph_put_cap_snap(capsnap);
		if (wake_ci)
			wake_up_all(&ci->i_cap_wq);
		if (wake_mdsc)
			wake_up_all(&mdsc->cap_flushing_wq);
		iput(inode);
	}
}

/*
 * Handle TRUNC from MDS, indicating file truncation.
 *
 * caller hold s_mutex.
 */
static bool handle_cap_trunc(struct inode *inode,
			     struct ceph_mds_caps *trunc,
			     struct ceph_mds_session *session)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int mds = session->s_mds;
	int seq = le32_to_cpu(trunc->seq);
	u32 truncate_seq = le32_to_cpu(trunc->truncate_seq);
	u64 truncate_size = le64_to_cpu(trunc->truncate_size);
	u64 size = le64_to_cpu(trunc->size);
	int implemented = 0;
	int dirty = __ceph_caps_dirty(ci);
	int issued = __ceph_caps_issued(ceph_inode(inode), &implemented);
	bool queue_trunc = false;

	lockdep_assert_held(&ci->i_ceph_lock);

	issued |= implemented | dirty;

	dout("handle_cap_trunc inode %p mds%d seq %d to %lld seq %d\n",
	     inode, mds, seq, truncate_size, truncate_seq);
	queue_trunc = ceph_fill_file_size(inode, issued,
					  truncate_seq, truncate_size, size);
	return queue_trunc;
}

/*
 * Handle EXPORT from MDS.  Cap is being migrated _from_ this mds to a
 * different one.  If we are the most recent migration we've seen (as
 * indicated by mseq), make note of the migrating cap bits for the
 * duration (until we see the corresponding IMPORT).
 *
 * caller holds s_mutex
 */
static void handle_cap_export(struct inode *inode, struct ceph_mds_caps *ex,
			      struct ceph_mds_cap_peer *ph,
			      struct ceph_mds_session *session)
{
	struct ceph_mds_client *mdsc = ceph_inode_to_client(inode)->mdsc;
	struct ceph_mds_session *tsession = NULL;
	struct ceph_cap *cap, *tcap, *new_cap = NULL;
	struct ceph_inode_info *ci = ceph_inode(inode);
	u64 t_cap_id;
	unsigned mseq = le32_to_cpu(ex->migrate_seq);
	unsigned t_seq, t_mseq;
	int target, issued;
	int mds = session->s_mds;

	if (ph) {
		t_cap_id = le64_to_cpu(ph->cap_id);
		t_seq = le32_to_cpu(ph->seq);
		t_mseq = le32_to_cpu(ph->mseq);
		target = le32_to_cpu(ph->mds);
	} else {
		t_cap_id = t_seq = t_mseq = 0;
		target = -1;
	}

	dout("handle_cap_export inode %p ci %p mds%d mseq %d target %d\n",
	     inode, ci, mds, mseq, target);
retry:
	spin_lock(&ci->i_ceph_lock);
	cap = __get_cap_for_mds(ci, mds);
	if (!cap || cap->cap_id != le64_to_cpu(ex->cap_id))
		goto out_unlock;

	if (target < 0) {
		ceph_remove_cap(cap, false);
		goto out_unlock;
	}

	/*
	 * now we know we haven't received the cap import message yet
	 * because the exported cap still exist.
	 */

	issued = cap->issued;
	if (issued != cap->implemented)
		pr_err_ratelimited("handle_cap_export: issued != implemented: "
				"ino (%llx.%llx) mds%d seq %d mseq %d "
				"issued %s implemented %s\n",
				ceph_vinop(inode), mds, cap->seq, cap->mseq,
				ceph_cap_string(issued),
				ceph_cap_string(cap->implemented));


	tcap = __get_cap_for_mds(ci, target);
	if (tcap) {
		/* already have caps from the target */
		if (tcap->cap_id == t_cap_id &&
		    ceph_seq_cmp(tcap->seq, t_seq) < 0) {
			dout(" updating import cap %p mds%d\n", tcap, target);
			tcap->cap_id = t_cap_id;
			tcap->seq = t_seq - 1;
			tcap->issue_seq = t_seq - 1;
			tcap->issued |= issued;
			tcap->implemented |= issued;
			if (cap == ci->i_auth_cap) {
				ci->i_auth_cap = tcap;
				change_auth_cap_ses(ci, tcap->session);
			}
		}
		ceph_remove_cap(cap, false);
		goto out_unlock;
	} else if (tsession) {
		/* add placeholder for the export tagert */
		int flag = (cap == ci->i_auth_cap) ? CEPH_CAP_FLAG_AUTH : 0;
		tcap = new_cap;
		ceph_add_cap(inode, tsession, t_cap_id, issued, 0,
			     t_seq - 1, t_mseq, (u64)-1, flag, &new_cap);

		if (!list_empty(&ci->i_cap_flush_list) &&
		    ci->i_auth_cap == tcap) {
			spin_lock(&mdsc->cap_dirty_lock);
			list_move_tail(&ci->i_flushing_item,
				       &tcap->session->s_cap_flushing);
			spin_unlock(&mdsc->cap_dirty_lock);
		}

		ceph_remove_cap(cap, false);
		goto out_unlock;
	}

	spin_unlock(&ci->i_ceph_lock);
	mutex_unlock(&session->s_mutex);

	/* open target session */
	tsession = ceph_mdsc_open_export_target_session(mdsc, target);
	if (!IS_ERR(tsession)) {
		if (mds > target) {
			mutex_lock(&session->s_mutex);
			mutex_lock_nested(&tsession->s_mutex,
					  SINGLE_DEPTH_NESTING);
		} else {
			mutex_lock(&tsession->s_mutex);
			mutex_lock_nested(&session->s_mutex,
					  SINGLE_DEPTH_NESTING);
		}
		new_cap = ceph_get_cap(mdsc, NULL);
	} else {
		WARN_ON(1);
		tsession = NULL;
		target = -1;
		mutex_lock(&session->s_mutex);
	}
	goto retry;

out_unlock:
	spin_unlock(&ci->i_ceph_lock);
	mutex_unlock(&session->s_mutex);
	if (tsession) {
		mutex_unlock(&tsession->s_mutex);
		ceph_put_mds_session(tsession);
	}
	if (new_cap)
		ceph_put_cap(mdsc, new_cap);
}

/*
 * Handle cap IMPORT.
 *
 * caller holds s_mutex. acquires i_ceph_lock
 */
static void handle_cap_import(struct ceph_mds_client *mdsc,
			      struct inode *inode, struct ceph_mds_caps *im,
			      struct ceph_mds_cap_peer *ph,
			      struct ceph_mds_session *session,
			      struct ceph_cap **target_cap, int *old_issued)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_cap *cap, *ocap, *new_cap = NULL;
	int mds = session->s_mds;
	int issued;
	unsigned caps = le32_to_cpu(im->caps);
	unsigned wanted = le32_to_cpu(im->wanted);
	unsigned seq = le32_to_cpu(im->seq);
	unsigned mseq = le32_to_cpu(im->migrate_seq);
	u64 realmino = le64_to_cpu(im->realm);
	u64 cap_id = le64_to_cpu(im->cap_id);
	u64 p_cap_id;
	int peer;

	if (ph) {
		p_cap_id = le64_to_cpu(ph->cap_id);
		peer = le32_to_cpu(ph->mds);
	} else {
		p_cap_id = 0;
		peer = -1;
	}

	dout("handle_cap_import inode %p ci %p mds%d mseq %d peer %d\n",
	     inode, ci, mds, mseq, peer);
retry:
	cap = __get_cap_for_mds(ci, mds);
	if (!cap) {
		if (!new_cap) {
			spin_unlock(&ci->i_ceph_lock);
			new_cap = ceph_get_cap(mdsc, NULL);
			spin_lock(&ci->i_ceph_lock);
			goto retry;
		}
		cap = new_cap;
	} else {
		if (new_cap) {
			ceph_put_cap(mdsc, new_cap);
			new_cap = NULL;
		}
	}

	__ceph_caps_issued(ci, &issued);
	issued |= __ceph_caps_dirty(ci);

	ceph_add_cap(inode, session, cap_id, caps, wanted, seq, mseq,
		     realmino, CEPH_CAP_FLAG_AUTH, &new_cap);

	ocap = peer >= 0 ? __get_cap_for_mds(ci, peer) : NULL;
	if (ocap && ocap->cap_id == p_cap_id) {
		dout(" remove export cap %p mds%d flags %d\n",
		     ocap, peer, ph->flags);
		if ((ph->flags & CEPH_CAP_FLAG_AUTH) &&
		    (ocap->seq != le32_to_cpu(ph->seq) ||
		     ocap->mseq != le32_to_cpu(ph->mseq))) {
			pr_err_ratelimited("handle_cap_import: "
					"mismatched seq/mseq: ino (%llx.%llx) "
					"mds%d seq %d mseq %d importer mds%d "
					"has peer seq %d mseq %d\n",
					ceph_vinop(inode), peer, ocap->seq,
					ocap->mseq, mds, le32_to_cpu(ph->seq),
					le32_to_cpu(ph->mseq));
		}
		ceph_remove_cap(ocap, (ph->flags & CEPH_CAP_FLAG_RELEASE));
	}

	*old_issued = issued;
	*target_cap = cap;
}

/*
 * Handle a caps message from the MDS.
 *
 * Identify the appropriate session, inode, and call the right handler
 * based on the cap op.
 */
void ceph_handle_caps(struct ceph_mds_session *session,
		      struct ceph_msg *msg)
{
	struct ceph_mds_client *mdsc = session->s_mdsc;
	struct inode *inode;
	struct ceph_inode_info *ci;
	struct ceph_cap *cap;
	struct ceph_mds_caps *h;
	struct ceph_mds_cap_peer *peer = NULL;
	struct ceph_snap_realm *realm = NULL;
	int op;
	int msg_version = le16_to_cpu(msg->hdr.version);
	u32 seq, mseq;
	struct ceph_vino vino;
	void *snaptrace;
	size_t snaptrace_len;
	void *p, *end;
	struct cap_extra_info extra_info = {};
	bool queue_trunc;

	dout("handle_caps from mds%d\n", session->s_mds);

	/* decode */
	end = msg->front.iov_base + msg->front.iov_len;
	if (msg->front.iov_len < sizeof(*h))
		goto bad;
	h = msg->front.iov_base;
	op = le32_to_cpu(h->op);
	vino.ino = le64_to_cpu(h->ino);
	vino.snap = CEPH_NOSNAP;
	seq = le32_to_cpu(h->seq);
	mseq = le32_to_cpu(h->migrate_seq);

	snaptrace = h + 1;
	snaptrace_len = le32_to_cpu(h->snap_trace_len);
	p = snaptrace + snaptrace_len;

	if (msg_version >= 2) {
		u32 flock_len;
		ceph_decode_32_safe(&p, end, flock_len, bad);
		if (p + flock_len > end)
			goto bad;
		p += flock_len;
	}

	if (msg_version >= 3) {
		if (op == CEPH_CAP_OP_IMPORT) {
			if (p + sizeof(*peer) > end)
				goto bad;
			peer = p;
			p += sizeof(*peer);
		} else if (op == CEPH_CAP_OP_EXPORT) {
			/* recorded in unused fields */
			peer = (void *)&h->size;
		}
	}

	if (msg_version >= 4) {
		ceph_decode_64_safe(&p, end, extra_info.inline_version, bad);
		ceph_decode_32_safe(&p, end, extra_info.inline_len, bad);
		if (p + extra_info.inline_len > end)
			goto bad;
		extra_info.inline_data = p;
		p += extra_info.inline_len;
	}

	if (msg_version >= 5) {
		struct ceph_osd_client	*osdc = &mdsc->fsc->client->osdc;
		u32			epoch_barrier;

		ceph_decode_32_safe(&p, end, epoch_barrier, bad);
		ceph_osdc_update_epoch_barrier(osdc, epoch_barrier);
	}

	if (msg_version >= 8) {
		u32 pool_ns_len;

		/* version >= 6 */
		ceph_decode_skip_64(&p, end, bad);	// flush_tid
		/* version >= 7 */
		ceph_decode_skip_32(&p, end, bad);	// caller_uid
		ceph_decode_skip_32(&p, end, bad);	// caller_gid
		/* version >= 8 */
		ceph_decode_32_safe(&p, end, pool_ns_len, bad);
		if (pool_ns_len > 0) {
			ceph_decode_need(&p, end, pool_ns_len, bad);
			extra_info.pool_ns =
				ceph_find_or_create_string(p, pool_ns_len);
			p += pool_ns_len;
		}
	}

	if (msg_version >= 9) {
		struct ceph_timespec *btime;

		if (p + sizeof(*btime) > end)
			goto bad;
		btime = p;
		ceph_decode_timespec64(&extra_info.btime, btime);
		p += sizeof(*btime);
		ceph_decode_64_safe(&p, end, extra_info.change_attr, bad);
	}

	if (msg_version >= 11) {
		/* version >= 10 */
		ceph_decode_skip_32(&p, end, bad); // flags
		/* version >= 11 */
		extra_info.dirstat_valid = true;
		ceph_decode_64_safe(&p, end, extra_info.nfiles, bad);
		ceph_decode_64_safe(&p, end, extra_info.nsubdirs, bad);
	}

	/* lookup ino */
	inode = ceph_find_inode(mdsc->fsc->sb, vino);
	ci = ceph_inode(inode);
	dout(" op %s ino %llx.%llx inode %p\n", ceph_cap_op_name(op), vino.ino,
	     vino.snap, inode);

	mutex_lock(&session->s_mutex);
	inc_session_sequence(session);
	dout(" mds%d seq %lld cap seq %u\n", session->s_mds, session->s_seq,
	     (unsigned)seq);

	if (!inode) {
		dout(" i don't have ino %llx\n", vino.ino);

		if (op == CEPH_CAP_OP_IMPORT) {
			cap = ceph_get_cap(mdsc, NULL);
			cap->cap_ino = vino.ino;
			cap->queue_release = 1;
			cap->cap_id = le64_to_cpu(h->cap_id);
			cap->mseq = mseq;
			cap->seq = seq;
			cap->issue_seq = seq;
			spin_lock(&session->s_cap_lock);
			__ceph_queue_cap_release(session, cap);
			spin_unlock(&session->s_cap_lock);
		}
		goto flush_cap_releases;
	}

	/* these will work even if we don't have a cap yet */
	switch (op) {
	case CEPH_CAP_OP_FLUSHSNAP_ACK:
		handle_cap_flushsnap_ack(inode, le64_to_cpu(msg->hdr.tid),
					 h, session);
		goto done;

	case CEPH_CAP_OP_EXPORT:
		handle_cap_export(inode, h, peer, session);
		goto done_unlocked;

	case CEPH_CAP_OP_IMPORT:
		realm = NULL;
		if (snaptrace_len) {
			down_write(&mdsc->snap_rwsem);
			ceph_update_snap_trace(mdsc, snaptrace,
					       snaptrace + snaptrace_len,
					       false, &realm);
			downgrade_write(&mdsc->snap_rwsem);
		} else {
			down_read(&mdsc->snap_rwsem);
		}
		spin_lock(&ci->i_ceph_lock);
		handle_cap_import(mdsc, inode, h, peer, session,
				  &cap, &extra_info.issued);
		handle_cap_grant(inode, session, cap,
				 h, msg->middle, &extra_info);
		if (realm)
			ceph_put_snap_realm(mdsc, realm);
		goto done_unlocked;
	}

	/* the rest require a cap */
	spin_lock(&ci->i_ceph_lock);
	cap = __get_cap_for_mds(ceph_inode(inode), session->s_mds);
	if (!cap) {
		dout(" no cap on %p ino %llx.%llx from mds%d\n",
		     inode, ceph_ino(inode), ceph_snap(inode),
		     session->s_mds);
		spin_unlock(&ci->i_ceph_lock);
		goto flush_cap_releases;
	}

	/* note that each of these drops i_ceph_lock for us */
	switch (op) {
	case CEPH_CAP_OP_REVOKE:
	case CEPH_CAP_OP_GRANT:
		__ceph_caps_issued(ci, &extra_info.issued);
		extra_info.issued |= __ceph_caps_dirty(ci);
		handle_cap_grant(inode, session, cap,
				 h, msg->middle, &extra_info);
		goto done_unlocked;

	case CEPH_CAP_OP_FLUSH_ACK:
		handle_cap_flush_ack(inode, le64_to_cpu(msg->hdr.tid),
				     h, session, cap);
		break;

	case CEPH_CAP_OP_TRUNC:
		queue_trunc = handle_cap_trunc(inode, h, session);
		spin_unlock(&ci->i_ceph_lock);
		if (queue_trunc)
			ceph_queue_vmtruncate(inode);
		break;

	default:
		spin_unlock(&ci->i_ceph_lock);
		pr_err("ceph_handle_caps: unknown cap op %d %s\n", op,
		       ceph_cap_op_name(op));
	}

done:
	mutex_unlock(&session->s_mutex);
done_unlocked:
	iput(inode);
out:
	ceph_put_string(extra_info.pool_ns);
	return;

flush_cap_releases:
	/*
	 * send any cap release message to try to move things
	 * along for the mds (who clearly thinks we still have this
	 * cap).
	 */
	ceph_flush_cap_releases(mdsc, session);
	goto done;

bad:
	pr_err("ceph_handle_caps: corrupt message\n");
	ceph_msg_dump(msg);
	goto out;
}

/*
 * Delayed work handler to process end of delayed cap release LRU list.
 *
 * If new caps are added to the list while processing it, these won't get
 * processed in this run.  In this case, the ci->i_hold_caps_max will be
 * returned so that the work can be scheduled accordingly.
 */
unsigned long ceph_check_delayed_caps(struct ceph_mds_client *mdsc)
{
	struct inode *inode;
	struct ceph_inode_info *ci;
	struct ceph_mount_options *opt = mdsc->fsc->mount_options;
	unsigned long delay_max = opt->caps_wanted_delay_max * HZ;
	unsigned long loop_start = jiffies;
	unsigned long delay = 0;

	dout("check_delayed_caps\n");
	spin_lock(&mdsc->cap_delay_lock);
	while (!list_empty(&mdsc->cap_delay_list)) {
		ci = list_first_entry(&mdsc->cap_delay_list,
				      struct ceph_inode_info,
				      i_cap_delay_list);
		if (time_before(loop_start, ci->i_hold_caps_max - delay_max)) {
			dout("%s caps added recently.  Exiting loop", __func__);
			delay = ci->i_hold_caps_max;
			break;
		}
		if ((ci->i_ceph_flags & CEPH_I_FLUSH) == 0 &&
		    time_before(jiffies, ci->i_hold_caps_max))
			break;
		list_del_init(&ci->i_cap_delay_list);

		inode = igrab(&ci->vfs_inode);
		if (inode) {
			spin_unlock(&mdsc->cap_delay_lock);
			dout("check_delayed_caps on %p\n", inode);
			ceph_check_caps(ci, 0, NULL);
			iput(inode);
			spin_lock(&mdsc->cap_delay_lock);
		}
	}
	spin_unlock(&mdsc->cap_delay_lock);

	return delay;
}

/*
 * Flush all dirty caps to the mds
 */
static void flush_dirty_session_caps(struct ceph_mds_session *s)
{
	struct ceph_mds_client *mdsc = s->s_mdsc;
	struct ceph_inode_info *ci;
	struct inode *inode;

	dout("flush_dirty_caps\n");
	spin_lock(&mdsc->cap_dirty_lock);
	while (!list_empty(&s->s_cap_dirty)) {
		ci = list_first_entry(&s->s_cap_dirty, struct ceph_inode_info,
				      i_dirty_item);
		inode = &ci->vfs_inode;
		ihold(inode);
		dout("flush_dirty_caps %p\n", inode);
		spin_unlock(&mdsc->cap_dirty_lock);
		ceph_check_caps(ci, CHECK_CAPS_FLUSH, NULL);
		iput(inode);
		spin_lock(&mdsc->cap_dirty_lock);
	}
	spin_unlock(&mdsc->cap_dirty_lock);
	dout("flush_dirty_caps done\n");
}

void ceph_flush_dirty_caps(struct ceph_mds_client *mdsc)
{
	ceph_mdsc_iterate_sessions(mdsc, flush_dirty_session_caps, true);
}

void __ceph_touch_fmode(struct ceph_inode_info *ci,
			struct ceph_mds_client *mdsc, int fmode)
{
	unsigned long now = jiffies;
	if (fmode & CEPH_FILE_MODE_RD)
		ci->i_last_rd = now;
	if (fmode & CEPH_FILE_MODE_WR)
		ci->i_last_wr = now;
	/* queue periodic check */
	if (fmode &&
	    __ceph_is_any_real_caps(ci) &&
	    list_empty(&ci->i_cap_delay_list))
		__cap_delay_requeue(mdsc, ci);
}

void ceph_get_fmode(struct ceph_inode_info *ci, int fmode, int count)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(ci->vfs_inode.i_sb);
	int bits = (fmode << 1) | 1;
	bool already_opened = false;
	int i;

	if (count == 1)
		atomic64_inc(&mdsc->metric.opened_files);

	spin_lock(&ci->i_ceph_lock);
	for (i = 0; i < CEPH_FILE_MODE_BITS; i++) {
		/*
		 * If any of the mode ref is larger than 0,
		 * that means it has been already opened by
		 * others. Just skip checking the PIN ref.
		 */
		if (i && ci->i_nr_by_mode[i])
			already_opened = true;

		if (bits & (1 << i))
			ci->i_nr_by_mode[i] += count;
	}

	if (!already_opened)
		percpu_counter_inc(&mdsc->metric.opened_inodes);
	spin_unlock(&ci->i_ceph_lock);
}

/*
 * Drop open file reference.  If we were the last open file,
 * we may need to release capabilities to the MDS (or schedule
 * their delayed release).
 */
void ceph_put_fmode(struct ceph_inode_info *ci, int fmode, int count)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(ci->vfs_inode.i_sb);
	int bits = (fmode << 1) | 1;
	bool is_closed = true;
	int i;

	if (count == 1)
		atomic64_dec(&mdsc->metric.opened_files);

	spin_lock(&ci->i_ceph_lock);
	for (i = 0; i < CEPH_FILE_MODE_BITS; i++) {
		if (bits & (1 << i)) {
			BUG_ON(ci->i_nr_by_mode[i] < count);
			ci->i_nr_by_mode[i] -= count;
		}

		/*
		 * If any of the mode ref is not 0 after
		 * decreased, that means it is still opened
		 * by others. Just skip checking the PIN ref.
		 */
		if (i && ci->i_nr_by_mode[i])
			is_closed = false;
	}

	if (is_closed)
		percpu_counter_dec(&mdsc->metric.opened_inodes);
	spin_unlock(&ci->i_ceph_lock);
}

/*
 * For a soon-to-be unlinked file, drop the LINK caps. If it
 * looks like the link count will hit 0, drop any other caps (other
 * than PIN) we don't specifically want (due to the file still being
 * open).
 */
int ceph_drop_caps_for_unlink(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int drop = CEPH_CAP_LINK_SHARED | CEPH_CAP_LINK_EXCL;

	spin_lock(&ci->i_ceph_lock);
	if (inode->i_nlink == 1) {
		drop |= ~(__ceph_caps_wanted(ci) | CEPH_CAP_PIN);

		if (__ceph_caps_dirty(ci)) {
			struct ceph_mds_client *mdsc =
				ceph_inode_to_client(inode)->mdsc;
			__cap_delay_requeue_front(mdsc, ci);
		}
	}
	spin_unlock(&ci->i_ceph_lock);
	return drop;
}

/*
 * Helpers for embedding cap and dentry lease releases into mds
 * requests.
 *
 * @force is used by dentry_release (below) to force inclusion of a
 * record for the directory inode, even when there aren't any caps to
 * drop.
 */
int ceph_encode_inode_release(void **p, struct inode *inode,
			      int mds, int drop, int unless, int force)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_cap *cap;
	struct ceph_mds_request_release *rel = *p;
	int used, dirty;
	int ret = 0;

	spin_lock(&ci->i_ceph_lock);
	used = __ceph_caps_used(ci);
	dirty = __ceph_caps_dirty(ci);

	dout("encode_inode_release %p mds%d used|dirty %s drop %s unless %s\n",
	     inode, mds, ceph_cap_string(used|dirty), ceph_cap_string(drop),
	     ceph_cap_string(unless));

	/* only drop unused, clean caps */
	drop &= ~(used | dirty);

	cap = __get_cap_for_mds(ci, mds);
	if (cap && __cap_is_valid(cap)) {
		unless &= cap->issued;
		if (unless) {
			if (unless & CEPH_CAP_AUTH_EXCL)
				drop &= ~CEPH_CAP_AUTH_SHARED;
			if (unless & CEPH_CAP_LINK_EXCL)
				drop &= ~CEPH_CAP_LINK_SHARED;
			if (unless & CEPH_CAP_XATTR_EXCL)
				drop &= ~CEPH_CAP_XATTR_SHARED;
			if (unless & CEPH_CAP_FILE_EXCL)
				drop &= ~CEPH_CAP_FILE_SHARED;
		}

		if (force || (cap->issued & drop)) {
			if (cap->issued & drop) {
				int wanted = __ceph_caps_wanted(ci);
				dout("encode_inode_release %p cap %p "
				     "%s -> %s, wanted %s -> %s\n", inode, cap,
				     ceph_cap_string(cap->issued),
				     ceph_cap_string(cap->issued & ~drop),
				     ceph_cap_string(cap->mds_wanted),
				     ceph_cap_string(wanted));

				cap->issued &= ~drop;
				cap->implemented &= ~drop;
				cap->mds_wanted = wanted;
				if (cap == ci->i_auth_cap &&
				    !(wanted & CEPH_CAP_ANY_FILE_WR))
					ci->i_requested_max_size = 0;
			} else {
				dout("encode_inode_release %p cap %p %s"
				     " (force)\n", inode, cap,
				     ceph_cap_string(cap->issued));
			}

			rel->ino = cpu_to_le64(ceph_ino(inode));
			rel->cap_id = cpu_to_le64(cap->cap_id);
			rel->seq = cpu_to_le32(cap->seq);
			rel->issue_seq = cpu_to_le32(cap->issue_seq);
			rel->mseq = cpu_to_le32(cap->mseq);
			rel->caps = cpu_to_le32(cap->implemented);
			rel->wanted = cpu_to_le32(cap->mds_wanted);
			rel->dname_len = 0;
			rel->dname_seq = 0;
			*p += sizeof(*rel);
			ret = 1;
		} else {
			dout("encode_inode_release %p cap %p %s (noop)\n",
			     inode, cap, ceph_cap_string(cap->issued));
		}
	}
	spin_unlock(&ci->i_ceph_lock);
	return ret;
}

int ceph_encode_dentry_release(void **p, struct dentry *dentry,
			       struct inode *dir,
			       int mds, int drop, int unless)
{
	struct dentry *parent = NULL;
	struct ceph_mds_request_release *rel = *p;
	struct ceph_dentry_info *di = ceph_dentry(dentry);
	int force = 0;
	int ret;

	/*
	 * force an record for the directory caps if we have a dentry lease.
	 * this is racy (can't take i_ceph_lock and d_lock together), but it
	 * doesn't have to be perfect; the mds will revoke anything we don't
	 * release.
	 */
	spin_lock(&dentry->d_lock);
	if (di->lease_session && di->lease_session->s_mds == mds)
		force = 1;
	if (!dir) {
		parent = dget(dentry->d_parent);
		dir = d_inode(parent);
	}
	spin_unlock(&dentry->d_lock);

	ret = ceph_encode_inode_release(p, dir, mds, drop, unless, force);
	dput(parent);

	spin_lock(&dentry->d_lock);
	if (ret && di->lease_session && di->lease_session->s_mds == mds) {
		dout("encode_dentry_release %p mds%d seq %d\n",
		     dentry, mds, (int)di->lease_seq);
		rel->dname_len = cpu_to_le32(dentry->d_name.len);
		memcpy(*p, dentry->d_name.name, dentry->d_name.len);
		*p += dentry->d_name.len;
		rel->dname_seq = cpu_to_le32(di->lease_seq);
		__ceph_mdsc_drop_dentry_lease(dentry);
	}
	spin_unlock(&dentry->d_lock);
	return ret;
}
