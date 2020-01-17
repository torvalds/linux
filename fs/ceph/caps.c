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
 * The Ceph metadata servers control client access to iyesde metadata
 * and file data by issuing capabilities, granting clients permission
 * to read and/or write both iyesde field and file data to OSDs
 * (storage yesdes).  Each capability consists of a set of bits
 * indicating which operations are allowed.
 *
 * If the client holds a *_SHARED cap, the client has a coherent value
 * that can be safely read from the cached iyesde.
 *
 * In the case of a *_EXCL (exclusive) or FILE_WR capabilities, the
 * client is allowed to change iyesde attributes (e.g., file size,
 * mtime), yeste its dirty state in the ceph_cap, and asynchroyesusly
 * flush that metadata change to the MDS.
 *
 * In the event of a conflicting operation (perhaps by ayesther
 * client), the MDS will revoke the conflicting client capabilities.
 *
 * In order for a client to cache an iyesde, it must hold a capability
 * with at least one MDS server.  When iyesdes are released, release
 * yestifications are batched and periodically sent en masse to the MDS
 * cluster to release server state.
 */

static u64 __get_oldest_flush_tid(struct ceph_mds_client *mdsc);
static void __kick_flushing_caps(struct ceph_mds_client *mdsc,
				 struct ceph_mds_session *session,
				 struct ceph_iyesde_info *ci,
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
static struct ceph_cap *__get_cap_for_mds(struct ceph_iyesde_info *ci, int mds)
{
	struct ceph_cap *cap;
	struct rb_yesde *n = ci->i_caps.rb_yesde;

	while (n) {
		cap = rb_entry(n, struct ceph_cap, ci_yesde);
		if (mds < cap->mds)
			n = n->rb_left;
		else if (mds > cap->mds)
			n = n->rb_right;
		else
			return cap;
	}
	return NULL;
}

struct ceph_cap *ceph_get_cap_for_mds(struct ceph_iyesde_info *ci, int mds)
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
static void __insert_cap_yesde(struct ceph_iyesde_info *ci,
			      struct ceph_cap *new)
{
	struct rb_yesde **p = &ci->i_caps.rb_yesde;
	struct rb_yesde *parent = NULL;
	struct ceph_cap *cap = NULL;

	while (*p) {
		parent = *p;
		cap = rb_entry(parent, struct ceph_cap, ci_yesde);
		if (new->mds < cap->mds)
			p = &(*p)->rb_left;
		else if (new->mds > cap->mds)
			p = &(*p)->rb_right;
		else
			BUG();
	}

	rb_link_yesde(&new->ci_yesde, parent, p);
	rb_insert_color(&new->ci_yesde, &ci->i_caps);
}

/*
 * (re)set cap hold timeouts, which control the delayed release
 * of unused caps back to the MDS.  Should be called on cap use.
 */
static void __cap_set_timeouts(struct ceph_mds_client *mdsc,
			       struct ceph_iyesde_info *ci)
{
	struct ceph_mount_options *opt = mdsc->fsc->mount_options;

	ci->i_hold_caps_min = round_jiffies(jiffies +
					    opt->caps_wanted_delay_min * HZ);
	ci->i_hold_caps_max = round_jiffies(jiffies +
					    opt->caps_wanted_delay_max * HZ);
	dout("__cap_set_timeouts %p min %lu max %lu\n", &ci->vfs_iyesde,
	     ci->i_hold_caps_min - jiffies, ci->i_hold_caps_max - jiffies);
}

/*
 * (Re)queue cap at the end of the delayed cap release list.
 *
 * If I_FLUSH is set, leave the iyesde at the front of the list.
 *
 * Caller holds i_ceph_lock
 *    -> we take mdsc->cap_delay_lock
 */
static void __cap_delay_requeue(struct ceph_mds_client *mdsc,
				struct ceph_iyesde_info *ci,
				bool set_timeout)
{
	dout("__cap_delay_requeue %p flags %d at %lu\n", &ci->vfs_iyesde,
	     ci->i_ceph_flags, ci->i_hold_caps_max);
	if (!mdsc->stopping) {
		spin_lock(&mdsc->cap_delay_lock);
		if (!list_empty(&ci->i_cap_delay_list)) {
			if (ci->i_ceph_flags & CEPH_I_FLUSH)
				goto yes_change;
			list_del_init(&ci->i_cap_delay_list);
		}
		if (set_timeout)
			__cap_set_timeouts(mdsc, ci);
		list_add_tail(&ci->i_cap_delay_list, &mdsc->cap_delay_list);
yes_change:
		spin_unlock(&mdsc->cap_delay_lock);
	}
}

/*
 * Queue an iyesde for immediate writeback.  Mark iyesde with I_FLUSH,
 * indicating we should send a cap message to flush dirty metadata
 * asap, and move to the front of the delayed cap list.
 */
static void __cap_delay_requeue_front(struct ceph_mds_client *mdsc,
				      struct ceph_iyesde_info *ci)
{
	dout("__cap_delay_requeue_front %p\n", &ci->vfs_iyesde);
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
			       struct ceph_iyesde_info *ci)
{
	dout("__cap_delay_cancel %p\n", &ci->vfs_iyesde);
	if (list_empty(&ci->i_cap_delay_list))
		return;
	spin_lock(&mdsc->cap_delay_lock);
	list_del_init(&ci->i_cap_delay_list);
	spin_unlock(&mdsc->cap_delay_lock);
}

/*
 * Common issue checks for add_cap, handle_cap_grant.
 */
static void __check_cap_issue(struct ceph_iyesde_info *ci, struct ceph_cap *cap,
			      unsigned issued)
{
	unsigned had = __ceph_caps_issued(ci, NULL);

	/*
	 * Each time we receive FILE_CACHE anew, we increment
	 * i_rdcache_gen.
	 */
	if ((issued & (CEPH_CAP_FILE_CACHE|CEPH_CAP_FILE_LAZYIO)) &&
	    (had & (CEPH_CAP_FILE_CACHE|CEPH_CAP_FILE_LAZYIO)) == 0) {
		ci->i_rdcache_gen++;
	}

	/*
	 * If FILE_SHARED is newly issued, mark dir yest complete. We don't
	 * kyesw what happened to this directory while we didn't have the cap.
	 * If FILE_SHARED is being revoked, also mark dir yest complete. It
	 * stops on-going cached readdir.
	 */
	if ((issued & CEPH_CAP_FILE_SHARED) != (had & CEPH_CAP_FILE_SHARED)) {
		if (issued & CEPH_CAP_FILE_SHARED)
			atomic_inc(&ci->i_shared_gen);
		if (S_ISDIR(ci->vfs_iyesde.i_mode)) {
			dout(" marking %p NOT complete\n", &ci->vfs_iyesde);
			__ceph_dir_clear_complete(ci);
		}
	}
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
void ceph_add_cap(struct iyesde *iyesde,
		  struct ceph_mds_session *session, u64 cap_id,
		  int fmode, unsigned issued, unsigned wanted,
		  unsigned seq, unsigned mseq, u64 realmiyes, int flags,
		  struct ceph_cap **new_cap)
{
	struct ceph_mds_client *mdsc = ceph_iyesde_to_client(iyesde)->mdsc;
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	struct ceph_cap *cap;
	int mds = session->s_mds;
	int actual_wanted;
	u32 gen;

	lockdep_assert_held(&ci->i_ceph_lock);

	dout("add_cap %p mds%d cap %llx %s seq %d\n", iyesde,
	     session->s_mds, cap_id, ceph_cap_string(issued), seq);

	/*
	 * If we are opening the file, include file mode wanted bits
	 * in wanted.
	 */
	if (fmode >= 0)
		wanted |= ceph_caps_for_mode(fmode);

	spin_lock(&session->s_gen_ttl_lock);
	gen = session->s_cap_gen;
	spin_unlock(&session->s_gen_ttl_lock);

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
		__insert_cap_yesde(ci, cap);

		/* add to session cap list */
		cap->session = session;
		spin_lock(&session->s_cap_lock);
		list_add_tail(&cap->session_caps, &session->s_caps);
		session->s_nr_caps++;
		spin_unlock(&session->s_cap_lock);
	} else {
		spin_lock(&session->s_cap_lock);
		list_move_tail(&cap->session_caps, &session->s_caps);
		spin_unlock(&session->s_cap_lock);

		if (cap->cap_gen < gen)
			cap->issued = cap->implemented = CEPH_CAP_PIN;

		/*
		 * auth mds of the iyesde changed. we received the cap export
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
	     realmiyes != (u64)-1 && ci->i_snap_realm->iyes != realmiyes)) {
		/*
		 * add this iyesde to the appropriate snap realm
		 */
		struct ceph_snap_realm *realm = ceph_lookup_snap_realm(mdsc,
							       realmiyes);
		if (realm) {
			struct ceph_snap_realm *oldrealm = ci->i_snap_realm;
			if (oldrealm) {
				spin_lock(&oldrealm->iyesdes_with_caps_lock);
				list_del_init(&ci->i_snap_realm_item);
				spin_unlock(&oldrealm->iyesdes_with_caps_lock);
			}

			spin_lock(&realm->iyesdes_with_caps_lock);
			list_add(&ci->i_snap_realm_item,
				 &realm->iyesdes_with_caps);
			ci->i_snap_realm = realm;
			if (realm->iyes == ci->i_viyes.iyes)
				realm->iyesde = iyesde;
			spin_unlock(&realm->iyesdes_with_caps_lock);

			if (oldrealm)
				ceph_put_snap_realm(mdsc, oldrealm);
		} else {
			pr_err("ceph_add_cap: couldn't find snap realm %llx\n",
			       realmiyes);
			WARN_ON(!realm);
		}
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
		__cap_delay_requeue(mdsc, ci, true);
	}

	if (flags & CEPH_CAP_FLAG_AUTH) {
		if (!ci->i_auth_cap ||
		    ceph_seq_cmp(ci->i_auth_cap->mseq, mseq) < 0) {
			ci->i_auth_cap = cap;
			cap->mds_wanted = wanted;
		}
	} else {
		WARN_ON(ci->i_auth_cap == cap);
	}

	dout("add_cap iyesde %p (%llx.%llx) cap %p %s yesw %s seq %d mds%d\n",
	     iyesde, ceph_viyesp(iyesde), cap, ceph_cap_string(issued),
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

	if (fmode >= 0)
		__ceph_get_fmode(ci, fmode);
}

/*
 * Return true if cap has yest timed out and belongs to the current
 * generation of the MDS session (i.e. has yest gone 'stale' due to
 * us losing touch with the mds).
 */
static int __cap_is_valid(struct ceph_cap *cap)
{
	unsigned long ttl;
	u32 gen;

	spin_lock(&cap->session->s_gen_ttl_lock);
	gen = cap->session->s_cap_gen;
	ttl = cap->session->s_cap_ttl;
	spin_unlock(&cap->session->s_gen_ttl_lock);

	if (cap->cap_gen < gen || time_after_eq(jiffies, ttl)) {
		dout("__cap_is_valid %p cap %p issued %s "
		     "but STALE (gen %u vs %u)\n", &cap->ci->vfs_iyesde,
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
int __ceph_caps_issued(struct ceph_iyesde_info *ci, int *implemented)
{
	int have = ci->i_snap_caps;
	struct ceph_cap *cap;
	struct rb_yesde *p;

	if (implemented)
		*implemented = 0;
	for (p = rb_first(&ci->i_caps); p; p = rb_next(p)) {
		cap = rb_entry(p, struct ceph_cap, ci_yesde);
		if (!__cap_is_valid(cap))
			continue;
		dout("__ceph_caps_issued %p cap %p issued %s\n",
		     &ci->vfs_iyesde, cap, ceph_cap_string(cap->issued));
		have |= cap->issued;
		if (implemented)
			*implemented |= cap->implemented;
	}
	/*
	 * exclude caps issued by yesn-auth MDS, but are been revoking
	 * by the auth MDS. The yesn-auth MDS should be revoking/exporting
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
int __ceph_caps_issued_other(struct ceph_iyesde_info *ci, struct ceph_cap *ocap)
{
	int have = ci->i_snap_caps;
	struct ceph_cap *cap;
	struct rb_yesde *p;

	for (p = rb_first(&ci->i_caps); p; p = rb_next(p)) {
		cap = rb_entry(p, struct ceph_cap, ci_yesde);
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
		dout("__touch_cap %p cap %p mds%d\n", &cap->ci->vfs_iyesde, cap,
		     s->s_mds);
		list_move_tail(&cap->session_caps, &s->s_caps);
	} else {
		dout("__touch_cap %p cap %p mds%d NOP, iterating over caps\n",
		     &cap->ci->vfs_iyesde, cap, s->s_mds);
	}
	spin_unlock(&s->s_cap_lock);
}

/*
 * Check if we hold the given mask.  If so, move the cap(s) to the
 * front of their respective LRUs.  (This is the preferred way for
 * callers to check for caps they want.)
 */
int __ceph_caps_issued_mask(struct ceph_iyesde_info *ci, int mask, int touch)
{
	struct ceph_cap *cap;
	struct rb_yesde *p;
	int have = ci->i_snap_caps;

	if ((have & mask) == mask) {
		dout("__ceph_caps_issued_mask iyes 0x%lx snap issued %s"
		     " (mask %s)\n", ci->vfs_iyesde.i_iyes,
		     ceph_cap_string(have),
		     ceph_cap_string(mask));
		return 1;
	}

	for (p = rb_first(&ci->i_caps); p; p = rb_next(p)) {
		cap = rb_entry(p, struct ceph_cap, ci_yesde);
		if (!__cap_is_valid(cap))
			continue;
		if ((cap->issued & mask) == mask) {
			dout("__ceph_caps_issued_mask iyes 0x%lx cap %p issued %s"
			     " (mask %s)\n", ci->vfs_iyesde.i_iyes, cap,
			     ceph_cap_string(cap->issued),
			     ceph_cap_string(mask));
			if (touch)
				__touch_cap(cap);
			return 1;
		}

		/* does a combination of caps satisfy mask? */
		have |= cap->issued;
		if ((have & mask) == mask) {
			dout("__ceph_caps_issued_mask iyes 0x%lx combo issued %s"
			     " (mask %s)\n", ci->vfs_iyesde.i_iyes,
			     ceph_cap_string(cap->issued),
			     ceph_cap_string(mask));
			if (touch) {
				struct rb_yesde *q;

				/* touch this + preceding caps */
				__touch_cap(cap);
				for (q = rb_first(&ci->i_caps); q != p;
				     q = rb_next(q)) {
					cap = rb_entry(q, struct ceph_cap,
						       ci_yesde);
					if (!__cap_is_valid(cap))
						continue;
					__touch_cap(cap);
				}
			}
			return 1;
		}
	}

	return 0;
}

/*
 * Return true if mask caps are currently being revoked by an MDS.
 */
int __ceph_caps_revoking_other(struct ceph_iyesde_info *ci,
			       struct ceph_cap *ocap, int mask)
{
	struct ceph_cap *cap;
	struct rb_yesde *p;

	for (p = rb_first(&ci->i_caps); p; p = rb_next(p)) {
		cap = rb_entry(p, struct ceph_cap, ci_yesde);
		if (cap != ocap &&
		    (cap->implemented & ~cap->issued & mask))
			return 1;
	}
	return 0;
}

int ceph_caps_revoking(struct ceph_iyesde_info *ci, int mask)
{
	struct iyesde *iyesde = &ci->vfs_iyesde;
	int ret;

	spin_lock(&ci->i_ceph_lock);
	ret = __ceph_caps_revoking_other(ci, NULL, mask);
	spin_unlock(&ci->i_ceph_lock);
	dout("ceph_caps_revoking %p %s = %d\n", iyesde,
	     ceph_cap_string(mask), ret);
	return ret;
}

int __ceph_caps_used(struct ceph_iyesde_info *ci)
{
	int used = 0;
	if (ci->i_pin_ref)
		used |= CEPH_CAP_PIN;
	if (ci->i_rd_ref)
		used |= CEPH_CAP_FILE_RD;
	if (ci->i_rdcache_ref ||
	    (!S_ISDIR(ci->vfs_iyesde.i_mode) && /* igyesre readdir cache */
	     ci->vfs_iyesde.i_data.nrpages))
		used |= CEPH_CAP_FILE_CACHE;
	if (ci->i_wr_ref)
		used |= CEPH_CAP_FILE_WR;
	if (ci->i_wb_ref || ci->i_wrbuffer_ref)
		used |= CEPH_CAP_FILE_BUFFER;
	return used;
}

/*
 * wanted, by virtue of open file modes
 */
int __ceph_caps_file_wanted(struct ceph_iyesde_info *ci)
{
	int i, bits = 0;
	for (i = 0; i < CEPH_FILE_MODE_BITS; i++) {
		if (ci->i_nr_by_mode[i])
			bits |= 1 << i;
	}
	if (bits == 0)
		return 0;
	return ceph_caps_for_mode(bits >> 1);
}

/*
 * Return caps we have registered with the MDS(s) as 'wanted'.
 */
int __ceph_caps_mds_wanted(struct ceph_iyesde_info *ci, bool check)
{
	struct ceph_cap *cap;
	struct rb_yesde *p;
	int mds_wanted = 0;

	for (p = rb_first(&ci->i_caps); p; p = rb_next(p)) {
		cap = rb_entry(p, struct ceph_cap, ci_yesde);
		if (check && !__cap_is_valid(cap))
			continue;
		if (cap == ci->i_auth_cap)
			mds_wanted |= cap->mds_wanted;
		else
			mds_wanted |= (cap->mds_wanted & ~CEPH_CAP_ANY_FILE_WR);
	}
	return mds_wanted;
}

/*
 * called under i_ceph_lock
 */
static int __ceph_is_single_caps(struct ceph_iyesde_info *ci)
{
	return rb_first(&ci->i_caps) == rb_last(&ci->i_caps);
}

int ceph_is_any_caps(struct iyesde *iyesde)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	int ret;

	spin_lock(&ci->i_ceph_lock);
	ret = __ceph_is_any_real_caps(ci);
	spin_unlock(&ci->i_ceph_lock);

	return ret;
}

static void drop_iyesde_snap_realm(struct ceph_iyesde_info *ci)
{
	struct ceph_snap_realm *realm = ci->i_snap_realm;
	spin_lock(&realm->iyesdes_with_caps_lock);
	list_del_init(&ci->i_snap_realm_item);
	ci->i_snap_realm_counter++;
	ci->i_snap_realm = NULL;
	if (realm->iyes == ci->i_viyes.iyes)
		realm->iyesde = NULL;
	spin_unlock(&realm->iyesdes_with_caps_lock);
	ceph_put_snap_realm(ceph_sb_to_client(ci->vfs_iyesde.i_sb)->mdsc,
			    realm);
}

/*
 * Remove a cap.  Take steps to deal with a racing iterate_session_caps.
 *
 * caller should hold i_ceph_lock.
 * caller will yest hold session s_mutex if called from destroy_iyesde.
 */
void __ceph_remove_cap(struct ceph_cap *cap, bool queue_release)
{
	struct ceph_mds_session *session = cap->session;
	struct ceph_iyesde_info *ci = cap->ci;
	struct ceph_mds_client *mdsc =
		ceph_sb_to_client(ci->vfs_iyesde.i_sb)->mdsc;
	int removed = 0;

	dout("__ceph_remove_cap %p from %p\n", cap, &ci->vfs_iyesde);

	/* remove from iyesde's cap rbtree, and clear auth cap */
	rb_erase(&cap->ci_yesde, &ci->i_caps);
	if (ci->i_auth_cap == cap)
		ci->i_auth_cap = NULL;

	/* remove from session list */
	spin_lock(&session->s_cap_lock);
	if (session->s_cap_iterator == cap) {
		/* yest yet, we are iterating over this very cap */
		dout("__ceph_remove_cap  delaying %p removal from session %p\n",
		     cap, cap->session);
	} else {
		list_del_init(&cap->session_caps);
		session->s_nr_caps--;
		cap->session = NULL;
		removed = 1;
	}
	/* protect backpointer with s_cap_lock: see iterate_session_caps */
	cap->ci = NULL;

	/*
	 * s_cap_reconnect is protected by s_cap_lock. yes one changes
	 * s_cap_gen while session is in the reconnect state.
	 */
	if (queue_release &&
	    (!session->s_cap_reconnect || cap->cap_gen == session->s_cap_gen)) {
		cap->queue_release = 1;
		if (removed) {
			__ceph_queue_cap_release(session, cap);
			removed = 0;
		}
	} else {
		cap->queue_release = 0;
	}
	cap->cap_iyes = ci->i_viyes.iyes;

	spin_unlock(&session->s_cap_lock);

	if (removed)
		ceph_put_cap(mdsc, cap);

	if (!__ceph_is_any_real_caps(ci)) {
		/* when reconnect denied, we remove session caps forcibly,
		 * i_wr_ref can be yesn-zero. If there are ongoing write,
		 * keep i_snap_realm.
		 */
		if (ci->i_wr_ref == 0 && ci->i_snap_realm)
			drop_iyesde_snap_realm(ci);

		__cap_delay_cancel(mdsc, ci);
	}
}

struct cap_msg_args {
	struct ceph_mds_session	*session;
	u64			iyes, cid, follows;
	u64			flush_tid, oldest_flush_tid, size, max_size;
	u64			xattr_version;
	u64			change_attr;
	struct ceph_buffer	*xattr_buf;
	struct timespec64	atime, mtime, ctime, btime;
	int			op, caps, wanted, dirty;
	u32			seq, issue_seq, mseq, time_warp_seq;
	u32			flags;
	kuid_t			uid;
	kgid_t			gid;
	umode_t			mode;
	bool			inline_data;
};

/*
 * Build and send a cap message to the given MDS.
 *
 * Caller should be holding s_mutex.
 */
static int send_cap_msg(struct cap_msg_args *arg)
{
	struct ceph_mds_caps *fc;
	struct ceph_msg *msg;
	void *p;
	size_t extra_len;
	struct ceph_osd_client *osdc = &arg->session->s_mdsc->fsc->client->osdc;

	dout("send_cap_msg %s %llx %llx caps %s wanted %s dirty %s"
	     " seq %u/%u tid %llu/%llu mseq %u follows %lld size %llu/%llu"
	     " xattr_ver %llu xattr_len %d\n", ceph_cap_op_name(arg->op),
	     arg->cid, arg->iyes, ceph_cap_string(arg->caps),
	     ceph_cap_string(arg->wanted), ceph_cap_string(arg->dirty),
	     arg->seq, arg->issue_seq, arg->flush_tid, arg->oldest_flush_tid,
	     arg->mseq, arg->follows, arg->size, arg->max_size,
	     arg->xattr_version,
	     arg->xattr_buf ? (int)arg->xattr_buf->vec.iov_len : 0);

	/* flock buffer size + inline version + inline data size +
	 * osd_epoch_barrier + oldest_flush_tid */
	extra_len = 4 + 8 + 4 + 4 + 8 + 4 + 4 + 4 + 8 + 8 + 4;
	msg = ceph_msg_new(CEPH_MSG_CLIENT_CAPS, sizeof(*fc) + extra_len,
			   GFP_NOFS, false);
	if (!msg)
		return -ENOMEM;

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
	fc->iyes = cpu_to_le64(arg->iyes);
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
	 * last, and force a flush of them when there is a conflict. For yesw,
	 * just set this to 0:0, to emulate how the MDS has worked up to yesw.
	 */
	ceph_encode_32(&p, 0);
	ceph_encode_32(&p, 0);

	/* pool namespace (version 8) (mds always igyesres this) */
	ceph_encode_32(&p, 0);

	/* btime and change_attr (version 9) */
	ceph_encode_timespec64(p, &arg->btime);
	p += sizeof(struct ceph_timespec);
	ceph_encode_64(&p, arg->change_attr);

	/* Advisory flags (version 10) */
	ceph_encode_32(&p, arg->flags);

	ceph_con_send(&arg->session->s_con, msg);
	return 0;
}

/*
 * Queue cap releases when an iyesde is dropped from our cache.
 */
void __ceph_remove_caps(struct ceph_iyesde_info *ci)
{
	struct rb_yesde *p;

	/* lock i_ceph_lock, because ceph_d_revalidate(..., LOOKUP_RCU)
	 * may call __ceph_caps_issued_mask() on a freeing iyesde. */
	spin_lock(&ci->i_ceph_lock);
	p = rb_first(&ci->i_caps);
	while (p) {
		struct ceph_cap *cap = rb_entry(p, struct ceph_cap, ci_yesde);
		p = rb_next(p);
		__ceph_remove_cap(cap, true);
	}
	spin_unlock(&ci->i_ceph_lock);
}

/*
 * Send a cap msg on the given iyesde.  Update our caps state, then
 * drop i_ceph_lock and send the message.
 *
 * Make yeste of max_size reported/requested from mds, revoked caps
 * that have yesw been implemented.
 *
 * Return yesn-zero if delayed release, or we experienced an error
 * such that the caller should requeue + retry later.
 *
 * called with i_ceph_lock, then drops it.
 * caller should hold snap_rwsem (read), s_mutex.
 */
static int __send_cap(struct ceph_mds_client *mdsc, struct ceph_cap *cap,
		      int op, int flags, int used, int want, int retain,
		      int flushing, u64 flush_tid, u64 oldest_flush_tid)
	__releases(cap->ci->i_ceph_lock)
{
	struct ceph_iyesde_info *ci = cap->ci;
	struct iyesde *iyesde = &ci->vfs_iyesde;
	struct ceph_buffer *old_blob = NULL;
	struct cap_msg_args arg;
	int held, revoking;
	int wake = 0;
	int delayed = 0;
	int ret;

	held = cap->issued | cap->implemented;
	revoking = cap->implemented & ~cap->issued;
	retain &= ~revoking;

	dout("__send_cap %p cap %p session %p %s -> %s (revoking %s)\n",
	     iyesde, cap, cap->session,
	     ceph_cap_string(held), ceph_cap_string(held & retain),
	     ceph_cap_string(revoking));
	BUG_ON((retain & CEPH_CAP_PIN) == 0);

	arg.session = cap->session;

	/* don't release wanted unless we've waited a bit. */
	if ((ci->i_ceph_flags & CEPH_I_NODELAY) == 0 &&
	    time_before(jiffies, ci->i_hold_caps_min)) {
		dout(" delaying issued %s -> %s, wanted %s -> %s on send\n",
		     ceph_cap_string(cap->issued),
		     ceph_cap_string(cap->issued & retain),
		     ceph_cap_string(cap->mds_wanted),
		     ceph_cap_string(want));
		want |= cap->mds_wanted;
		retain |= cap->issued;
		delayed = 1;
	}
	ci->i_ceph_flags &= ~(CEPH_I_NODELAY | CEPH_I_FLUSH);
	if (want & ~cap->mds_wanted) {
		/* user space may open/close single file frequently.
		 * This avoids droping mds_wanted immediately after
		 * requesting new mds_wanted.
		 */
		__cap_set_timeouts(mdsc, ci);
	}

	cap->issued &= retain;  /* drop bits we don't want */
	if (cap->implemented & ~cap->issued) {
		/*
		 * Wake up any waiters on wanted -> needed transition.
		 * This is due to the weird transition from buffered
		 * to sync IO... we need to flush dirty pages _before_
		 * allowing sync writes to avoid reordering.
		 */
		wake = 1;
	}
	cap->implemented &= cap->issued | used;
	cap->mds_wanted = want;

	arg.iyes = ceph_viyes(iyesde).iyes;
	arg.cid = cap->cap_id;
	arg.follows = flushing ? ci->i_head_snapc->seq : 0;
	arg.flush_tid = flush_tid;
	arg.oldest_flush_tid = oldest_flush_tid;

	arg.size = iyesde->i_size;
	ci->i_reported_size = arg.size;
	arg.max_size = ci->i_wanted_max_size;
	ci->i_requested_max_size = arg.max_size;

	if (flushing & CEPH_CAP_XATTR_EXCL) {
		old_blob = __ceph_build_xattrs_blob(ci);
		arg.xattr_version = ci->i_xattrs.version;
		arg.xattr_buf = ci->i_xattrs.blob;
	} else {
		arg.xattr_buf = NULL;
	}

	arg.mtime = iyesde->i_mtime;
	arg.atime = iyesde->i_atime;
	arg.ctime = iyesde->i_ctime;
	arg.btime = ci->i_btime;
	arg.change_attr = iyesde_peek_iversion_raw(iyesde);

	arg.op = op;
	arg.caps = cap->implemented;
	arg.wanted = want;
	arg.dirty = flushing;

	arg.seq = cap->seq;
	arg.issue_seq = cap->issue_seq;
	arg.mseq = cap->mseq;
	arg.time_warp_seq = ci->i_time_warp_seq;

	arg.uid = iyesde->i_uid;
	arg.gid = iyesde->i_gid;
	arg.mode = iyesde->i_mode;

	arg.inline_data = ci->i_inline_version != CEPH_INLINE_NONE;
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
	arg.flags = flags;

	spin_unlock(&ci->i_ceph_lock);

	ceph_buffer_put(old_blob);

	ret = send_cap_msg(&arg);
	if (ret < 0) {
		dout("error sending cap msg, must requeue %p\n", iyesde);
		delayed = 1;
	}

	if (wake)
		wake_up_all(&ci->i_cap_wq);

	return delayed;
}

static inline int __send_flush_snap(struct iyesde *iyesde,
				    struct ceph_mds_session *session,
				    struct ceph_cap_snap *capsnap,
				    u32 mseq, u64 oldest_flush_tid)
{
	struct cap_msg_args	arg;

	arg.session = session;
	arg.iyes = ceph_viyes(iyesde).iyes;
	arg.cid = 0;
	arg.follows = capsnap->follows;
	arg.flush_tid = capsnap->cap_flush.tid;
	arg.oldest_flush_tid = oldest_flush_tid;

	arg.size = capsnap->size;
	arg.max_size = 0;
	arg.xattr_version = capsnap->xattr_version;
	arg.xattr_buf = capsnap->xattr_blob;

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

	return send_cap_msg(&arg);
}

/*
 * When a snapshot is taken, clients accumulate dirty metadata on
 * iyesdes with capabilities in ceph_cap_snaps to describe the file
 * state at the time the snapshot was taken.  This must be flushed
 * asynchroyesusly back to the MDS once sync writes complete and dirty
 * data is written out.
 *
 * Called under i_ceph_lock.  Takes s_mutex as needed.
 */
static void __ceph_flush_snaps(struct ceph_iyesde_info *ci,
			       struct ceph_mds_session *session)
		__releases(ci->i_ceph_lock)
		__acquires(ci->i_ceph_lock)
{
	struct iyesde *iyesde = &ci->vfs_iyesde;
	struct ceph_mds_client *mdsc = session->s_mdsc;
	struct ceph_cap_snap *capsnap;
	u64 oldest_flush_tid = 0;
	u64 first_tid = 1, last_tid = 0;

	dout("__flush_snaps %p session %p\n", iyesde, session);

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
			dout("__flush_snaps %p auth cap %p yest mds%d, "
			     "stop\n", iyesde, cap, session->s_mds);
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
		     iyesde, capsnap, cf->tid, ceph_cap_string(capsnap->dirty));

		ret = __send_flush_snap(iyesde, session, capsnap, cap->mseq,
					oldest_flush_tid);
		if (ret < 0) {
			pr_err("__flush_snaps: error sending cap flushsnap, "
			       "iyes (%llx.%llx) tid %llu follows %llu\n",
				ceph_viyesp(iyesde), cf->tid, capsnap->follows);
		}

		ceph_put_cap_snap(capsnap);
		spin_lock(&ci->i_ceph_lock);
	}
}

void ceph_flush_snaps(struct ceph_iyesde_info *ci,
		      struct ceph_mds_session **psession)
{
	struct iyesde *iyesde = &ci->vfs_iyesde;
	struct ceph_mds_client *mdsc = ceph_iyesde_to_client(iyesde)->mdsc;
	struct ceph_mds_session *session = NULL;
	int mds;

	dout("ceph_flush_snaps %p\n", iyesde);
	if (psession)
		session = *psession;
retry:
	spin_lock(&ci->i_ceph_lock);
	if (!(ci->i_ceph_flags & CEPH_I_FLUSH_SNAPS)) {
		dout(" yes capsnap needs flush, doing yesthing\n");
		goto out;
	}
	if (!ci->i_auth_cap) {
		dout(" yes auth cap (migrating?), doing yesthing\n");
		goto out;
	}

	mds = ci->i_auth_cap->session->s_mds;
	if (session && session->s_mds != mds) {
		dout(" oops, wrong session %p mutex\n", session);
		mutex_unlock(&session->s_mutex);
		ceph_put_mds_session(session);
		session = NULL;
	}
	if (!session) {
		spin_unlock(&ci->i_ceph_lock);
		mutex_lock(&mdsc->mutex);
		session = __ceph_lookup_mds_session(mdsc, mds);
		mutex_unlock(&mdsc->mutex);
		if (session) {
			dout(" inverting session/iyes locks on %p\n", session);
			mutex_lock(&session->s_mutex);
		}
		goto retry;
	}

	// make sure flushsnap messages are sent in proper order.
	if (ci->i_ceph_flags & CEPH_I_KICK_FLUSH)
		__kick_flushing_caps(mdsc, session, ci, 0);

	__ceph_flush_snaps(ci, session);
out:
	spin_unlock(&ci->i_ceph_lock);

	if (psession) {
		*psession = session;
	} else if (session) {
		mutex_unlock(&session->s_mutex);
		ceph_put_mds_session(session);
	}
	/* we flushed them all; remove this iyesde from the queue */
	spin_lock(&mdsc->snap_flush_lock);
	list_del_init(&ci->i_snap_flush_item);
	spin_unlock(&mdsc->snap_flush_lock);
}

/*
 * Mark caps dirty.  If iyesde is newly dirty, return the dirty flags.
 * Caller is then responsible for calling __mark_iyesde_dirty with the
 * returned flags value.
 */
int __ceph_mark_dirty_caps(struct ceph_iyesde_info *ci, int mask,
			   struct ceph_cap_flush **pcf)
{
	struct ceph_mds_client *mdsc =
		ceph_sb_to_client(ci->vfs_iyesde.i_sb)->mdsc;
	struct iyesde *iyesde = &ci->vfs_iyesde;
	int was = ci->i_dirty_caps;
	int dirty = 0;

	if (!ci->i_auth_cap) {
		pr_warn("__mark_dirty_caps %p %llx mask %s, "
			"but yes auth cap (session was closed?)\n",
			iyesde, ceph_iyes(iyesde), ceph_cap_string(mask));
		return 0;
	}

	dout("__mark_dirty_caps %p %s dirty %s -> %s\n", &ci->vfs_iyesde,
	     ceph_cap_string(mask), ceph_cap_string(was),
	     ceph_cap_string(was | mask));
	ci->i_dirty_caps |= mask;
	if (was == 0) {
		WARN_ON_ONCE(ci->i_prealloc_cap_flush);
		swap(ci->i_prealloc_cap_flush, *pcf);

		if (!ci->i_head_snapc) {
			WARN_ON_ONCE(!rwsem_is_locked(&mdsc->snap_rwsem));
			ci->i_head_snapc = ceph_get_snap_context(
				ci->i_snap_realm->cached_context);
		}
		dout(" iyesde %p yesw dirty snapc %p auth cap %p\n",
		     &ci->vfs_iyesde, ci->i_head_snapc, ci->i_auth_cap);
		BUG_ON(!list_empty(&ci->i_dirty_item));
		spin_lock(&mdsc->cap_dirty_lock);
		list_add(&ci->i_dirty_item, &mdsc->cap_dirty);
		spin_unlock(&mdsc->cap_dirty_lock);
		if (ci->i_flushing_caps == 0) {
			ihold(iyesde);
			dirty |= I_DIRTY_SYNC;
		}
	} else {
		WARN_ON_ONCE(!ci->i_prealloc_cap_flush);
	}
	BUG_ON(list_empty(&ci->i_dirty_item));
	if (((was | ci->i_flushing_caps) & CEPH_CAP_FILE_BUFFER) &&
	    (mask & CEPH_CAP_FILE_BUFFER))
		dirty |= I_DIRTY_DATASYNC;
	__cap_delay_requeue(mdsc, ci, true);
	return dirty;
}

struct ceph_cap_flush *ceph_alloc_cap_flush(void)
{
	return kmem_cache_alloc(ceph_cap_flush_cachep, GFP_KERNEL);
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
 * Remove cap_flush from the mdsc's or iyesde's flushing cap list.
 * Return true if caller needs to wake up flush waiters.
 */
static bool __finish_cap_flush(struct ceph_mds_client *mdsc,
			       struct ceph_iyesde_info *ci,
			       struct ceph_cap_flush *cf)
{
	struct ceph_cap_flush *prev;
	bool wake = cf->wake;
	if (mdsc) {
		/* are there older pending cap flushes? */
		if (wake && cf->g_list.prev != &mdsc->cap_flush_list) {
			prev = list_prev_entry(cf, g_list);
			prev->wake = true;
			wake = false;
		}
		list_del(&cf->g_list);
	} else if (ci) {
		if (wake && cf->i_list.prev != &ci->i_cap_flush_list) {
			prev = list_prev_entry(cf, i_list);
			prev->wake = true;
			wake = false;
		}
		list_del(&cf->i_list);
	} else {
		BUG_ON(1);
	}
	return wake;
}

/*
 * Add dirty iyesde to the flushing list.  Assigned a seq number so we
 * can wait for caps to flush without starving.
 *
 * Called under i_ceph_lock. Returns the flush tid.
 */
static u64 __mark_caps_flushing(struct iyesde *iyesde,
				struct ceph_mds_session *session, bool wake,
				u64 *oldest_flush_tid)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_client(iyesde->i_sb)->mdsc;
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	struct ceph_cap_flush *cf = NULL;
	int flushing;

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
	dout(" iyesde %p yesw !dirty\n", iyesde);

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
static int try_yesnblocking_invalidate(struct iyesde *iyesde)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	u32 invalidating_gen = ci->i_rdcache_gen;

	spin_unlock(&ci->i_ceph_lock);
	invalidate_mapping_pages(&iyesde->i_data, 0, -1);
	spin_lock(&ci->i_ceph_lock);

	if (iyesde->i_data.nrpages == 0 &&
	    invalidating_gen == ci->i_rdcache_gen) {
		/* success. */
		dout("try_yesnblocking_invalidate %p success\n", iyesde);
		/* save any racing async invalidate some trouble */
		ci->i_rdcache_revoking = ci->i_rdcache_gen - 1;
		return 0;
	}
	dout("try_yesnblocking_invalidate %p failed\n", iyesde);
	return -1;
}

bool __ceph_should_report_size(struct ceph_iyesde_info *ci)
{
	loff_t size = ci->vfs_iyesde.i_size;
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
 *  CHECK_CAPS_NODELAY - caller is delayed work and we should yest delay
 *    cap release further.
 *  CHECK_CAPS_AUTHONLY - we should only check the auth cap
 *  CHECK_CAPS_FLUSH - we should flush any dirty caps immediately, without
 *    further delay.
 */
void ceph_check_caps(struct ceph_iyesde_info *ci, int flags,
		     struct ceph_mds_session *session)
{
	struct ceph_fs_client *fsc = ceph_iyesde_to_client(&ci->vfs_iyesde);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct iyesde *iyesde = &ci->vfs_iyesde;
	struct ceph_cap *cap;
	u64 flush_tid, oldest_flush_tid;
	int file_wanted, used, cap_used;
	int took_snap_rwsem = 0;             /* true if mdsc->snap_rwsem held */
	int issued, implemented, want, retain, revoking, flushing = 0;
	int mds = -1;   /* keep track of how far we've gone through i_caps list
			   to avoid an infinite loop on retry */
	struct rb_yesde *p;
	int delayed = 0, sent = 0;
	bool yes_delay = flags & CHECK_CAPS_NODELAY;
	bool queue_invalidate = false;
	bool tried_invalidate = false;

	/* if we are unmounting, flush any unused caps immediately. */
	if (mdsc->stopping)
		yes_delay = true;

	spin_lock(&ci->i_ceph_lock);

	if (ci->i_ceph_flags & CEPH_I_FLUSH)
		flags |= CHECK_CAPS_FLUSH;

	if (!(flags & CHECK_CAPS_AUTHONLY) ||
	    (ci->i_auth_cap && __ceph_is_single_caps(ci)))
		__cap_delay_cancel(mdsc, ci);

	goto retry_locked;
retry:
	spin_lock(&ci->i_ceph_lock);
retry_locked:
	file_wanted = __ceph_caps_file_wanted(ci);
	used = __ceph_caps_used(ci);
	issued = __ceph_caps_issued(ci, &implemented);
	revoking = implemented & ~issued;

	want = file_wanted;
	retain = file_wanted | used | CEPH_CAP_PIN;
	if (!mdsc->stopping && iyesde->i_nlink > 0) {
		if (file_wanted) {
			retain |= CEPH_CAP_ANY;       /* be greedy */
		} else if (S_ISDIR(iyesde->i_mode) &&
			   (issued & CEPH_CAP_FILE_SHARED) &&
			   __ceph_dir_is_complete(ci)) {
			/*
			 * If a directory is complete, we want to keep
			 * the exclusive cap. So that MDS does yest end up
			 * revoking the shared cap on every create/unlink
			 * operation.
			 */
			if (IS_RDONLY(iyesde))
				want = CEPH_CAP_ANY_SHARED;
			else
				want = CEPH_CAP_ANY_SHARED | CEPH_CAP_FILE_EXCL;
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
	     " issued %s revoking %s retain %s %s%s%s\n", iyesde,
	     ceph_cap_string(file_wanted),
	     ceph_cap_string(used), ceph_cap_string(ci->i_dirty_caps),
	     ceph_cap_string(ci->i_flushing_caps),
	     ceph_cap_string(issued), ceph_cap_string(revoking),
	     ceph_cap_string(retain),
	     (flags & CHECK_CAPS_AUTHONLY) ? " AUTHONLY" : "",
	     (flags & CHECK_CAPS_NODELAY) ? " NODELAY" : "",
	     (flags & CHECK_CAPS_FLUSH) ? " FLUSH" : "");

	/*
	 * If we yes longer need to hold onto old our caps, and we may
	 * have cached pages, but don't want them, then try to invalidate.
	 * If we fail, it's because pages are locked.... try again later.
	 */
	if ((!yes_delay || mdsc->stopping) &&
	    !S_ISDIR(iyesde->i_mode) &&		/* igyesre readdir cache */
	    !(ci->i_wb_ref || ci->i_wrbuffer_ref) &&   /* yes dirty pages... */
	    iyesde->i_data.nrpages &&		/* have cached pages */
	    (revoking & (CEPH_CAP_FILE_CACHE|
			 CEPH_CAP_FILE_LAZYIO)) && /*  or revoking cache */
	    !tried_invalidate) {
		dout("check_caps trying to invalidate on %p\n", iyesde);
		if (try_yesnblocking_invalidate(iyesde) < 0) {
			dout("check_caps queuing invalidate\n");
			queue_invalidate = true;
			ci->i_rdcache_revoking = ci->i_rdcache_gen;
		}
		tried_invalidate = true;
		goto retry_locked;
	}

	for (p = rb_first(&ci->i_caps); p; p = rb_next(p)) {
		cap = rb_entry(p, struct ceph_cap, ci_yesde);

		/* avoid looping forever */
		if (mds >= cap->mds ||
		    ((flags & CHECK_CAPS_AUTHONLY) && cap != ci->i_auth_cap))
			continue;

		/* NOTE: yes side-effects allowed, until we take s_mutex */

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

		/* completed revocation? going down and there are yes caps? */
		if (revoking && (revoking & cap_used) == 0) {
			dout("completed revocation of %s\n",
			     ceph_cap_string(cap->implemented & ~cap->issued));
			goto ack;
		}

		/* want more caps from mds? */
		if (want & ~(cap->mds_wanted | cap->issued))
			goto ack;

		/* things we might delay */
		if ((cap->issued & ~retain) == 0)
			continue;     /* yespe, all good */

		if (yes_delay)
			goto ack;

		/* delay? */
		if ((ci->i_ceph_flags & CEPH_I_NODELAY) == 0 &&
		    time_before(jiffies, ci->i_hold_caps_max)) {
			dout(" delaying issued %s -> %s, wanted %s -> %s\n",
			     ceph_cap_string(cap->issued),
			     ceph_cap_string(cap->issued & retain),
			     ceph_cap_string(cap->mds_wanted),
			     ceph_cap_string(want));
			delayed++;
			continue;
		}

ack:
		if (session && session != cap->session) {
			dout("oops, wrong session %p mutex\n", session);
			mutex_unlock(&session->s_mutex);
			session = NULL;
		}
		if (!session) {
			session = cap->session;
			if (mutex_trylock(&session->s_mutex) == 0) {
				dout("inverting session/iyes locks on %p\n",
				     session);
				spin_unlock(&ci->i_ceph_lock);
				if (took_snap_rwsem) {
					up_read(&mdsc->snap_rwsem);
					took_snap_rwsem = 0;
				}
				mutex_lock(&session->s_mutex);
				goto retry;
			}
		}

		/* kick flushing and flush snaps before sending yesrmal
		 * cap message */
		if (cap == ci->i_auth_cap &&
		    (ci->i_ceph_flags &
		     (CEPH_I_KICK_FLUSH | CEPH_I_FLUSH_SNAPS))) {
			if (ci->i_ceph_flags & CEPH_I_KICK_FLUSH)
				__kick_flushing_caps(mdsc, session, ci, 0);
			if (ci->i_ceph_flags & CEPH_I_FLUSH_SNAPS)
				__ceph_flush_snaps(ci, session);

			goto retry_locked;
		}

		/* take snap_rwsem after session mutex */
		if (!took_snap_rwsem) {
			if (down_read_trylock(&mdsc->snap_rwsem) == 0) {
				dout("inverting snap/in locks on %p\n",
				     iyesde);
				spin_unlock(&ci->i_ceph_lock);
				down_read(&mdsc->snap_rwsem);
				took_snap_rwsem = 1;
				goto retry;
			}
			took_snap_rwsem = 1;
		}

		if (cap == ci->i_auth_cap && ci->i_dirty_caps) {
			flushing = ci->i_dirty_caps;
			flush_tid = __mark_caps_flushing(iyesde, session, false,
							 &oldest_flush_tid);
		} else {
			flushing = 0;
			flush_tid = 0;
			spin_lock(&mdsc->cap_dirty_lock);
			oldest_flush_tid = __get_oldest_flush_tid(mdsc);
			spin_unlock(&mdsc->cap_dirty_lock);
		}

		mds = cap->mds;  /* remember mds, so we don't repeat */
		sent++;

		/* __send_cap drops i_ceph_lock */
		delayed += __send_cap(mdsc, cap, CEPH_CAP_OP_UPDATE, 0,
				cap_used, want, retain, flushing,
				flush_tid, oldest_flush_tid);
		goto retry; /* retake i_ceph_lock and restart our cap scan. */
	}

	/* Reschedule delayed caps release if we delayed anything */
	if (delayed)
		__cap_delay_requeue(mdsc, ci, false);

	spin_unlock(&ci->i_ceph_lock);

	if (queue_invalidate)
		ceph_queue_invalidate(iyesde);

	if (session)
		mutex_unlock(&session->s_mutex);
	if (took_snap_rwsem)
		up_read(&mdsc->snap_rwsem);
}

/*
 * Try to flush dirty caps back to the auth mds.
 */
static int try_flush_caps(struct iyesde *iyesde, u64 *ptid)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_client(iyesde->i_sb)->mdsc;
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	struct ceph_mds_session *session = NULL;
	int flushing = 0;
	u64 flush_tid = 0, oldest_flush_tid = 0;

retry:
	spin_lock(&ci->i_ceph_lock);
retry_locked:
	if (ci->i_dirty_caps && ci->i_auth_cap) {
		struct ceph_cap *cap = ci->i_auth_cap;
		int delayed;

		if (session != cap->session) {
			spin_unlock(&ci->i_ceph_lock);
			if (session)
				mutex_unlock(&session->s_mutex);
			session = cap->session;
			mutex_lock(&session->s_mutex);
			goto retry;
		}
		if (cap->session->s_state < CEPH_MDS_SESSION_OPEN) {
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
		flush_tid = __mark_caps_flushing(iyesde, session, true,
						 &oldest_flush_tid);

		/* __send_cap drops i_ceph_lock */
		delayed = __send_cap(mdsc, cap, CEPH_CAP_OP_FLUSH,
				     CEPH_CLIENT_CAPS_SYNC,
				     __ceph_caps_used(ci),
				     __ceph_caps_wanted(ci),
				     (cap->issued | cap->implemented),
				     flushing, flush_tid, oldest_flush_tid);

		if (delayed) {
			spin_lock(&ci->i_ceph_lock);
			__cap_delay_requeue(mdsc, ci, true);
			spin_unlock(&ci->i_ceph_lock);
		}
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
	if (session)
		mutex_unlock(&session->s_mutex);

	*ptid = flush_tid;
	return flushing;
}

/*
 * Return true if we've flushed caps through the given flush_tid.
 */
static int caps_are_flushed(struct iyesde *iyesde, u64 flush_tid)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
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
static int unsafe_request_wait(struct iyesde *iyesde)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	struct ceph_mds_request *req1 = NULL, *req2 = NULL;
	int ret, err = 0;

	spin_lock(&ci->i_unsafe_lock);
	if (S_ISDIR(iyesde->i_mode) && !list_empty(&ci->i_unsafe_dirops)) {
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

	dout("unsafe_request_wait %p wait on tid %llu %llu\n",
	     iyesde, req1 ? req1->r_tid : 0ULL, req2 ? req2->r_tid : 0ULL);
	if (req1) {
		ret = !wait_for_completion_timeout(&req1->r_safe_completion,
					ceph_timeout_jiffies(req1->r_timeout));
		if (ret)
			err = -EIO;
		ceph_mdsc_put_request(req1);
	}
	if (req2) {
		ret = !wait_for_completion_timeout(&req2->r_safe_completion,
					ceph_timeout_jiffies(req2->r_timeout));
		if (ret)
			err = -EIO;
		ceph_mdsc_put_request(req2);
	}
	return err;
}

int ceph_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct ceph_file_info *fi = file->private_data;
	struct iyesde *iyesde = file->f_mapping->host;
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	u64 flush_tid;
	int ret, err;
	int dirty;

	dout("fsync %p%s\n", iyesde, datasync ? " datasync" : "");

	ret = file_write_and_wait_range(file, start, end);
	if (datasync)
		goto out;

	dirty = try_flush_caps(iyesde, &flush_tid);
	dout("fsync dirty caps are %s\n", ceph_cap_string(dirty));

	err = unsafe_request_wait(iyesde);

	/*
	 * only wait on yesn-file metadata writeback (the mds
	 * can recover size and mtime, so we don't need to
	 * wait for that)
	 */
	if (!err && (dirty & ~CEPH_CAP_ANY_FILE_WR)) {
		err = wait_event_interruptible(ci->i_cap_wq,
					caps_are_flushed(iyesde, flush_tid));
	}

	if (err < 0)
		ret = err;

	if (errseq_check(&ci->i_meta_err, READ_ONCE(fi->meta_err))) {
		spin_lock(&file->f_lock);
		err = errseq_check_and_advance(&ci->i_meta_err,
					       &fi->meta_err);
		spin_unlock(&file->f_lock);
		if (err < 0)
			ret = err;
	}
out:
	dout("fsync %p%s result=%d\n", iyesde, datasync ? " datasync" : "", ret);
	return ret;
}

/*
 * Flush any dirty caps back to the mds.  If we aren't asked to wait,
 * queue iyesde for flush but don't do so immediately, because we can
 * get by with fewer MDS messages if we wait for data writeback to
 * complete first.
 */
int ceph_write_iyesde(struct iyesde *iyesde, struct writeback_control *wbc)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	u64 flush_tid;
	int err = 0;
	int dirty;
	int wait = (wbc->sync_mode == WB_SYNC_ALL && !wbc->for_sync);

	dout("write_iyesde %p wait=%d\n", iyesde, wait);
	if (wait) {
		dirty = try_flush_caps(iyesde, &flush_tid);
		if (dirty)
			err = wait_event_interruptible(ci->i_cap_wq,
				       caps_are_flushed(iyesde, flush_tid));
	} else {
		struct ceph_mds_client *mdsc =
			ceph_sb_to_client(iyesde->i_sb)->mdsc;

		spin_lock(&ci->i_ceph_lock);
		if (__ceph_caps_dirty(ci))
			__cap_delay_requeue_front(mdsc, ci);
		spin_unlock(&ci->i_ceph_lock);
	}
	return err;
}

static void __kick_flushing_caps(struct ceph_mds_client *mdsc,
				 struct ceph_mds_session *session,
				 struct ceph_iyesde_info *ci,
				 u64 oldest_flush_tid)
	__releases(ci->i_ceph_lock)
	__acquires(ci->i_ceph_lock)
{
	struct iyesde *iyesde = &ci->vfs_iyesde;
	struct ceph_cap *cap;
	struct ceph_cap_flush *cf;
	int ret;
	u64 first_tid = 0;
	u64 last_snap_flush = 0;

	ci->i_ceph_flags &= ~CEPH_I_KICK_FLUSH;

	list_for_each_entry_reverse(cf, &ci->i_cap_flush_list, i_list) {
		if (!cf->caps) {
			last_snap_flush = cf->tid;
			break;
		}
	}

	list_for_each_entry(cf, &ci->i_cap_flush_list, i_list) {
		if (cf->tid < first_tid)
			continue;

		cap = ci->i_auth_cap;
		if (!(cap && cap->session == session)) {
			pr_err("%p auth cap %p yest mds%d ???\n",
			       iyesde, cap, session->s_mds);
			break;
		}

		first_tid = cf->tid + 1;

		if (cf->caps) {
			dout("kick_flushing_caps %p cap %p tid %llu %s\n",
			     iyesde, cap, cf->tid, ceph_cap_string(cf->caps));
			ci->i_ceph_flags |= CEPH_I_NODELAY;

			ret = __send_cap(mdsc, cap, CEPH_CAP_OP_FLUSH,
					 (cf->tid < last_snap_flush ?
					  CEPH_CLIENT_CAPS_PENDING_CAPSNAP : 0),
					  __ceph_caps_used(ci),
					  __ceph_caps_wanted(ci),
					  (cap->issued | cap->implemented),
					  cf->caps, cf->tid, oldest_flush_tid);
			if (ret) {
				pr_err("kick_flushing_caps: error sending "
					"cap flush, iyes (%llx.%llx) "
					"tid %llu flushing %s\n",
					ceph_viyesp(iyesde), cf->tid,
					ceph_cap_string(cf->caps));
			}
		} else {
			struct ceph_cap_snap *capsnap =
					container_of(cf, struct ceph_cap_snap,
						    cap_flush);
			dout("kick_flushing_caps %p capsnap %p tid %llu %s\n",
			     iyesde, capsnap, cf->tid,
			     ceph_cap_string(capsnap->dirty));

			refcount_inc(&capsnap->nref);
			spin_unlock(&ci->i_ceph_lock);

			ret = __send_flush_snap(iyesde, session, capsnap, cap->mseq,
						oldest_flush_tid);
			if (ret < 0) {
				pr_err("kick_flushing_caps: error sending "
					"cap flushsnap, iyes (%llx.%llx) "
					"tid %llu follows %llu\n",
					ceph_viyesp(iyesde), cf->tid,
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
	struct ceph_iyesde_info *ci;
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
			pr_err("%p auth cap %p yest mds%d ???\n",
				&ci->vfs_iyesde, cap, session->s_mds);
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
	struct ceph_iyesde_info *ci;
	struct ceph_cap *cap;
	u64 oldest_flush_tid;

	dout("kick_flushing_caps mds%d\n", session->s_mds);

	spin_lock(&mdsc->cap_dirty_lock);
	oldest_flush_tid = __get_oldest_flush_tid(mdsc);
	spin_unlock(&mdsc->cap_dirty_lock);

	list_for_each_entry(ci, &session->s_cap_flushing, i_flushing_item) {
		spin_lock(&ci->i_ceph_lock);
		cap = ci->i_auth_cap;
		if (!(cap && cap->session == session)) {
			pr_err("%p auth cap %p yest mds%d ???\n",
				&ci->vfs_iyesde, cap, session->s_mds);
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

static void kick_flushing_iyesde_caps(struct ceph_mds_client *mdsc,
				     struct ceph_mds_session *session,
				     struct iyesde *iyesde)
	__releases(ci->i_ceph_lock)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	struct ceph_cap *cap;

	cap = ci->i_auth_cap;
	dout("kick_flushing_iyesde_caps %p flushing %s\n", iyesde,
	     ceph_cap_string(ci->i_flushing_caps));

	if (!list_empty(&ci->i_cap_flush_list)) {
		u64 oldest_flush_tid;
		spin_lock(&mdsc->cap_dirty_lock);
		list_move_tail(&ci->i_flushing_item,
			       &cap->session->s_cap_flushing);
		oldest_flush_tid = __get_oldest_flush_tid(mdsc);
		spin_unlock(&mdsc->cap_dirty_lock);

		__kick_flushing_caps(mdsc, session, ci, oldest_flush_tid);
		spin_unlock(&ci->i_ceph_lock);
	} else {
		spin_unlock(&ci->i_ceph_lock);
	}
}


/*
 * Take references to capabilities we hold, so that we don't release
 * them to the MDS prematurely.
 *
 * Protected by i_ceph_lock.
 */
static void __take_cap_refs(struct ceph_iyesde_info *ci, int got,
			    bool snap_rwsem_locked)
{
	if (got & CEPH_CAP_PIN)
		ci->i_pin_ref++;
	if (got & CEPH_CAP_FILE_RD)
		ci->i_rd_ref++;
	if (got & CEPH_CAP_FILE_CACHE)
		ci->i_rdcache_ref++;
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
			ihold(&ci->vfs_iyesde);
		ci->i_wb_ref++;
		dout("__take_cap_refs %p wb %d -> %d (?)\n",
		     &ci->vfs_iyesde, ci->i_wb_ref-1, ci->i_wb_ref);
	}
}

/*
 * Try to grab cap references.  Specify those refs we @want, and the
 * minimal set we @need.  Also include the larger offset we are writing
 * to (when applicable), and check against max_size here as well.
 * Note that caller is responsible for ensuring max_size increases are
 * requested from the MDS.
 *
 * Returns 0 if caps were yest able to be acquired (yet), a 1 if they were,
 * or a negative error code.
 *
 * FIXME: how does a 0 return differ from -EAGAIN?
 */
enum {
	NON_BLOCKING	= 1,
	CHECK_FILELOCK	= 2,
};

static int try_get_cap_refs(struct iyesde *iyesde, int need, int want,
			    loff_t endoff, int flags, int *got)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	struct ceph_mds_client *mdsc = ceph_iyesde_to_client(iyesde)->mdsc;
	int ret = 0;
	int have, implemented;
	int file_wanted;
	bool snap_rwsem_locked = false;

	dout("get_cap_refs %p need %s want %s\n", iyesde,
	     ceph_cap_string(need), ceph_cap_string(want));

again:
	spin_lock(&ci->i_ceph_lock);

	if ((flags & CHECK_FILELOCK) &&
	    (ci->i_ceph_flags & CEPH_I_ERROR_FILELOCK)) {
		dout("try_get_cap_refs %p error filelock\n", iyesde);
		ret = -EIO;
		goto out_unlock;
	}

	/* make sure file is actually open */
	file_wanted = __ceph_caps_file_wanted(ci);
	if ((file_wanted & need) != need) {
		dout("try_get_cap_refs need %s file_wanted %s, EBADF\n",
		     ceph_cap_string(need), ceph_cap_string(file_wanted));
		ret = -EBADF;
		goto out_unlock;
	}

	/* finish pending truncate */
	while (ci->i_truncate_pending) {
		spin_unlock(&ci->i_ceph_lock);
		if (snap_rwsem_locked) {
			up_read(&mdsc->snap_rwsem);
			snap_rwsem_locked = false;
		}
		__ceph_do_pending_vmtruncate(iyesde);
		spin_lock(&ci->i_ceph_lock);
	}

	have = __ceph_caps_issued(ci, &implemented);

	if (have & need & CEPH_CAP_FILE_WR) {
		if (endoff >= 0 && endoff > (loff_t)ci->i_max_size) {
			dout("get_cap_refs %p endoff %llu > maxsize %llu\n",
			     iyesde, endoff, ci->i_max_size);
			if (endoff > ci->i_requested_max_size)
				ret = -EAGAIN;
			goto out_unlock;
		}
		/*
		 * If a sync write is in progress, we must wait, so that we
		 * can get a final snapshot value for size+mtime.
		 */
		if (__ceph_have_pending_cap_snap(ci)) {
			dout("get_cap_refs %p cap_snap_pending\n", iyesde);
			goto out_unlock;
		}
	}

	if ((have & need) == need) {
		/*
		 * Look at (implemented & ~have & yest) so that we keep waiting
		 * on transition from wanted -> needed caps.  This is needed
		 * for WRBUFFER|WR -> WR to avoid a new WR sync write from
		 * going before a prior buffered writeback happens.
		 */
		int yest = want & ~(have & need);
		int revoking = implemented & ~have;
		dout("get_cap_refs %p have %s but yest %s (revoking %s)\n",
		     iyesde, ceph_cap_string(have), ceph_cap_string(yest),
		     ceph_cap_string(revoking));
		if ((revoking & yest) == 0) {
			if (!snap_rwsem_locked &&
			    !ci->i_head_snapc &&
			    (need & CEPH_CAP_FILE_WR)) {
				if (!down_read_trylock(&mdsc->snap_rwsem)) {
					/*
					 * we can yest call down_read() when
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
			*got = need | (have & want);
			if ((need & CEPH_CAP_FILE_RD) &&
			    !(*got & CEPH_CAP_FILE_CACHE))
				ceph_disable_fscache_readpage(ci);
			__take_cap_refs(ci, *got, true);
			ret = 1;
		}
	} else {
		int session_readonly = false;
		if ((need & CEPH_CAP_FILE_WR) && ci->i_auth_cap) {
			struct ceph_mds_session *s = ci->i_auth_cap->session;
			spin_lock(&s->s_cap_lock);
			session_readonly = s->s_readonly;
			spin_unlock(&s->s_cap_lock);
		}
		if (session_readonly) {
			dout("get_cap_refs %p needed %s but mds%d readonly\n",
			     iyesde, ceph_cap_string(need), ci->i_auth_cap->mds);
			ret = -EROFS;
			goto out_unlock;
		}

		if (ci->i_ceph_flags & CEPH_I_CAP_DROPPED) {
			int mds_wanted;
			if (READ_ONCE(mdsc->fsc->mount_state) ==
			    CEPH_MOUNT_SHUTDOWN) {
				dout("get_cap_refs %p forced umount\n", iyesde);
				ret = -EIO;
				goto out_unlock;
			}
			mds_wanted = __ceph_caps_mds_wanted(ci, false);
			if (need & ~(mds_wanted & need)) {
				dout("get_cap_refs %p caps were dropped"
				     " (session killed?)\n", iyesde);
				ret = -ESTALE;
				goto out_unlock;
			}
			if (!(file_wanted & ~mds_wanted))
				ci->i_ceph_flags &= ~CEPH_I_CAP_DROPPED;
		}

		dout("get_cap_refs %p have %s needed %s\n", iyesde,
		     ceph_cap_string(have), ceph_cap_string(need));
	}
out_unlock:
	spin_unlock(&ci->i_ceph_lock);
	if (snap_rwsem_locked)
		up_read(&mdsc->snap_rwsem);

	dout("get_cap_refs %p ret %d got %s\n", iyesde,
	     ret, ceph_cap_string(*got));
	return ret;
}

/*
 * Check the offset we are writing up to against our current
 * max_size.  If necessary, tell the MDS we want to write to
 * a larger offset.
 */
static void check_max_size(struct iyesde *iyesde, loff_t endoff)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	int check = 0;

	/* do we need to explicitly request a larger max_size? */
	spin_lock(&ci->i_ceph_lock);
	if (endoff >= ci->i_max_size && endoff > ci->i_wanted_max_size) {
		dout("write %p at large endoff %llu, req max_size\n",
		     iyesde, endoff);
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

int ceph_try_get_caps(struct iyesde *iyesde, int need, int want,
		      bool yesnblock, int *got)
{
	int ret;

	BUG_ON(need & ~CEPH_CAP_FILE_RD);
	BUG_ON(want & ~(CEPH_CAP_FILE_CACHE|CEPH_CAP_FILE_LAZYIO|CEPH_CAP_FILE_SHARED));
	ret = ceph_pool_perm_check(iyesde, need);
	if (ret < 0)
		return ret;

	ret = try_get_cap_refs(iyesde, need, want, 0,
			       (yesnblock ? NON_BLOCKING : 0), got);
	return ret == -EAGAIN ? 0 : ret;
}

/*
 * Wait for caps, and take cap references.  If we can't get a WR cap
 * due to a small max_size, make sure we check_max_size (and possibly
 * ask the mds) so we don't get hung up indefinitely.
 */
int ceph_get_caps(struct file *filp, int need, int want,
		  loff_t endoff, int *got, struct page **pinned_page)
{
	struct ceph_file_info *fi = filp->private_data;
	struct iyesde *iyesde = file_iyesde(filp);
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	struct ceph_fs_client *fsc = ceph_iyesde_to_client(iyesde);
	int ret, _got, flags;

	ret = ceph_pool_perm_check(iyesde, need);
	if (ret < 0)
		return ret;

	if ((fi->fmode & CEPH_FILE_MODE_WR) &&
	    fi->filp_gen != READ_ONCE(fsc->filp_gen))
		return -EBADF;

	while (true) {
		if (endoff > 0)
			check_max_size(iyesde, endoff);

		flags = atomic_read(&fi->num_locks) ? CHECK_FILELOCK : 0;
		_got = 0;
		ret = try_get_cap_refs(iyesde, need, want, endoff,
				       flags, &_got);
		if (ret == -EAGAIN)
			continue;
		if (!ret) {
			struct ceph_mds_client *mdsc = fsc->mdsc;
			struct cap_wait cw;
			DEFINE_WAIT_FUNC(wait, woken_wake_function);

			cw.iyes = iyesde->i_iyes;
			cw.tgid = current->tgid;
			cw.need = need;
			cw.want = want;

			spin_lock(&mdsc->caps_list_lock);
			list_add(&cw.list, &mdsc->cap_wait_list);
			spin_unlock(&mdsc->caps_list_lock);

			add_wait_queue(&ci->i_cap_wq, &wait);

			flags |= NON_BLOCKING;
			while (!(ret = try_get_cap_refs(iyesde, need, want,
							endoff, flags, &_got))) {
				if (signal_pending(current)) {
					ret = -ERESTARTSYS;
					break;
				}
				wait_woken(&wait, TASK_INTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
			}

			remove_wait_queue(&ci->i_cap_wq, &wait);

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
			if (ret == -ESTALE) {
				/* session was killed, try renew caps */
				ret = ceph_renew_caps(iyesde);
				if (ret == 0)
					continue;
			}
			return ret;
		}

		if (ci->i_inline_version != CEPH_INLINE_NONE &&
		    (_got & (CEPH_CAP_FILE_CACHE|CEPH_CAP_FILE_LAZYIO)) &&
		    i_size_read(iyesde) > 0) {
			struct page *page =
				find_get_page(iyesde->i_mapping, 0);
			if (page) {
				if (PageUptodate(page)) {
					*pinned_page = page;
					break;
				}
				put_page(page);
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
			ret = __ceph_do_getattr(iyesde, NULL,
						CEPH_STAT_CAP_INLINE_DATA,
						true);
			if (ret < 0)
				return ret;
			continue;
		}
		break;
	}

	if ((_got & CEPH_CAP_FILE_RD) && (_got & CEPH_CAP_FILE_CACHE))
		ceph_fscache_revalidate_cookie(ci);

	*got = _got;
	return 0;
}

/*
 * Take cap refs.  Caller must already kyesw we hold at least one ref
 * on the caps in question or we don't kyesw this is safe.
 */
void ceph_get_cap_refs(struct ceph_iyesde_info *ci, int caps)
{
	spin_lock(&ci->i_ceph_lock);
	__take_cap_refs(ci, caps, false);
	spin_unlock(&ci->i_ceph_lock);
}


/*
 * drop cap_snap that is yest associated with any snapshot.
 * we don't need to send FLUSHSNAP message for it.
 */
static int ceph_try_drop_cap_snap(struct ceph_iyesde_info *ci,
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

/*
 * Release cap refs.
 *
 * If we released the last ref on any given cap, call ceph_check_caps
 * to release (or schedule a release).
 *
 * If we are releasing a WR cap (from a sync write), finalize any affected
 * cap_snap, and wake up any waiters.
 */
void ceph_put_cap_refs(struct ceph_iyesde_info *ci, int had)
{
	struct iyesde *iyesde = &ci->vfs_iyesde;
	int last = 0, put = 0, flushsnaps = 0, wake = 0;

	spin_lock(&ci->i_ceph_lock);
	if (had & CEPH_CAP_PIN)
		--ci->i_pin_ref;
	if (had & CEPH_CAP_FILE_RD)
		if (--ci->i_rd_ref == 0)
			last++;
	if (had & CEPH_CAP_FILE_CACHE)
		if (--ci->i_rdcache_ref == 0)
			last++;
	if (had & CEPH_CAP_FILE_BUFFER) {
		if (--ci->i_wb_ref == 0) {
			last++;
			put++;
		}
		dout("put_cap_refs %p wb %d -> %d (?)\n",
		     iyesde, ci->i_wb_ref+1, ci->i_wb_ref);
	}
	if (had & CEPH_CAP_FILE_WR)
		if (--ci->i_wr_ref == 0) {
			last++;
			if (__ceph_have_pending_cap_snap(ci)) {
				struct ceph_cap_snap *capsnap =
					list_last_entry(&ci->i_cap_snaps,
							struct ceph_cap_snap,
							ci_item);
				capsnap->writing = 0;
				if (ceph_try_drop_cap_snap(ci, capsnap))
					put++;
				else if (__ceph_finish_cap_snap(ci, capsnap))
					flushsnaps = 1;
				wake = 1;
			}
			if (ci->i_wrbuffer_ref_head == 0 &&
			    ci->i_dirty_caps == 0 &&
			    ci->i_flushing_caps == 0) {
				BUG_ON(!ci->i_head_snapc);
				ceph_put_snap_context(ci->i_head_snapc);
				ci->i_head_snapc = NULL;
			}
			/* see comment in __ceph_remove_cap() */
			if (!__ceph_is_any_real_caps(ci) && ci->i_snap_realm)
				drop_iyesde_snap_realm(ci);
		}
	spin_unlock(&ci->i_ceph_lock);

	dout("put_cap_refs %p had %s%s%s\n", iyesde, ceph_cap_string(had),
	     last ? " last" : "", put ? " put" : "");

	if (last && !flushsnaps)
		ceph_check_caps(ci, 0, NULL);
	else if (flushsnaps)
		ceph_flush_snaps(ci, NULL);
	if (wake)
		wake_up_all(&ci->i_cap_wq);
	while (put-- > 0)
		iput(iyesde);
}

/*
 * Release @nr WRBUFFER refs on dirty pages for the given @snapc snap
 * context.  Adjust per-snap dirty page accounting as appropriate.
 * Once all dirty data for a cap_snap is flushed, flush snapped file
 * metadata back to the MDS.  If we dropped the last ref, call
 * ceph_check_caps.
 */
void ceph_put_wrbuffer_cap_refs(struct ceph_iyesde_info *ci, int nr,
				struct ceph_snap_context *snapc)
{
	struct iyesde *iyesde = &ci->vfs_iyesde;
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
		     iyesde,
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
		BUG_ON(!found);
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
		     iyesde, capsnap, capsnap->context->seq,
		     ci->i_wrbuffer_ref+nr, capsnap->dirty_pages + nr,
		     ci->i_wrbuffer_ref, capsnap->dirty_pages,
		     last ? " (wrbuffer last)" : "",
		     complete_capsnap ? " (complete capsnap)" : "");
	}

	spin_unlock(&ci->i_ceph_lock);

	if (last) {
		ceph_check_caps(ci, CHECK_CAPS_AUTHONLY, NULL);
	} else if (flush_snaps) {
		ceph_flush_snaps(ci, NULL);
	}
	if (complete_capsnap)
		wake_up_all(&ci->i_cap_wq);
	while (put-- > 0) {
		/* avoid calling iput_final() in osd dispatch threads */
		ceph_async_iput(iyesde);
	}
}

/*
 * Invalidate unlinked iyesde's aliases, so we can drop the iyesde ASAP.
 */
static void invalidate_aliases(struct iyesde *iyesde)
{
	struct dentry *dn, *prev = NULL;

	dout("invalidate_aliases iyesde %p\n", iyesde);
	d_prune_aliases(iyesde);
	/*
	 * For yesn-directory iyesde, d_find_alias() only returns
	 * hashed dentry. After calling d_invalidate(), the
	 * dentry becomes unhashed.
	 *
	 * For directory iyesde, d_find_alias() can return
	 * unhashed dentry. But directory iyesde should have
	 * one alias at most.
	 */
	while ((dn = d_find_alias(iyesde))) {
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
static void handle_cap_grant(struct iyesde *iyesde,
			     struct ceph_mds_session *session,
			     struct ceph_cap *cap,
			     struct ceph_mds_caps *grant,
			     struct ceph_buffer *xattr_buf,
			     struct cap_extra_info *extra_info)
	__releases(ci->i_ceph_lock)
	__releases(session->s_mdsc->snap_rwsem)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	int seq = le32_to_cpu(grant->seq);
	int newcaps = le32_to_cpu(grant->caps);
	int used, wanted, dirty;
	u64 size = le64_to_cpu(grant->size);
	u64 max_size = le64_to_cpu(grant->max_size);
	unsigned char check_caps = 0;
	bool was_stale = cap->cap_gen < session->s_cap_gen;
	bool wake = false;
	bool writeback = false;
	bool queue_trunc = false;
	bool queue_invalidate = false;
	bool deleted_iyesde = false;
	bool fill_inline = false;

	dout("handle_cap_grant iyesde %p cap %p mds%d seq %d %s\n",
	     iyesde, cap, session->s_mds, seq, ceph_cap_string(newcaps));
	dout(" size %llu max_size %llu, i_size %llu\n", size, max_size,
		iyesde->i_size);


	/*
	 * If CACHE is being revoked, and we have yes dirty buffers,
	 * try to invalidate (once).  (If there are dirty buffers, we
	 * will invalidate _after_ writeback.)
	 */
	if (!S_ISDIR(iyesde->i_mode) && /* don't invalidate readdir cache */
	    ((cap->issued & ~newcaps) & CEPH_CAP_FILE_CACHE) &&
	    (newcaps & CEPH_CAP_FILE_LAZYIO) == 0 &&
	    !(ci->i_wrbuffer_ref || ci->i_wb_ref)) {
		if (try_yesnblocking_invalidate(iyesde)) {
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
	 * auth mds of the iyesde changed. we received the cap export message,
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

	/* side effects yesw are allowed */
	cap->cap_gen = session->s_cap_gen;
	cap->seq = seq;

	__check_cap_issue(ci, cap, newcaps);

	iyesde_set_max_iversion_raw(iyesde, extra_info->change_attr);

	if ((newcaps & CEPH_CAP_AUTH_SHARED) &&
	    (extra_info->issued & CEPH_CAP_AUTH_EXCL) == 0) {
		iyesde->i_mode = le32_to_cpu(grant->mode);
		iyesde->i_uid = make_kuid(&init_user_ns, le32_to_cpu(grant->uid));
		iyesde->i_gid = make_kgid(&init_user_ns, le32_to_cpu(grant->gid));
		ci->i_btime = extra_info->btime;
		dout("%p mode 0%o uid.gid %d.%d\n", iyesde, iyesde->i_mode,
		     from_kuid(&init_user_ns, iyesde->i_uid),
		     from_kgid(&init_user_ns, iyesde->i_gid));
	}

	if ((newcaps & CEPH_CAP_LINK_SHARED) &&
	    (extra_info->issued & CEPH_CAP_LINK_EXCL) == 0) {
		set_nlink(iyesde, le32_to_cpu(grant->nlink));
		if (iyesde->i_nlink == 0 &&
		    (newcaps & (CEPH_CAP_LINK_SHARED | CEPH_CAP_LINK_EXCL)))
			deleted_iyesde = true;
	}

	if ((extra_info->issued & CEPH_CAP_XATTR_EXCL) == 0 &&
	    grant->xattr_len) {
		int len = le32_to_cpu(grant->xattr_len);
		u64 version = le64_to_cpu(grant->xattr_version);

		if (version > ci->i_xattrs.version) {
			dout(" got new xattrs v%llu on %p len %d\n",
			     version, iyesde, len);
			if (ci->i_xattrs.blob)
				ceph_buffer_put(ci->i_xattrs.blob);
			ci->i_xattrs.blob = ceph_buffer_get(xattr_buf);
			ci->i_xattrs.version = version;
			ceph_forget_all_cached_acls(iyesde);
			ceph_security_invalidate_secctx(iyesde);
		}
	}

	if (newcaps & CEPH_CAP_ANY_RD) {
		struct timespec64 mtime, atime, ctime;
		/* ctime/mtime/atime? */
		ceph_decode_timespec64(&mtime, &grant->mtime);
		ceph_decode_timespec64(&atime, &grant->atime);
		ceph_decode_timespec64(&ctime, &grant->ctime);
		ceph_fill_file_time(iyesde, extra_info->issued,
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
		queue_trunc = ceph_fill_file_size(iyesde, extra_info->issued,
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
		} else if (ci->i_wanted_max_size > ci->i_max_size &&
			   ci->i_wanted_max_size > ci->i_requested_max_size) {
			/* CEPH_CAP_OP_IMPORT */
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

	/* revocation, grant, or yes-op? */
	if (cap->issued & ~newcaps) {
		int revoking = cap->issued & ~newcaps;

		dout("revocation: %s -> %s (revoking %s)\n",
		     ceph_cap_string(cap->issued),
		     ceph_cap_string(newcaps),
		     ceph_cap_string(revoking));
		if (revoking & used & CEPH_CAP_FILE_BUFFER)
			writeback = true;  /* initiate writeback; will delay ack */
		else if (revoking == CEPH_CAP_FILE_CACHE &&
			 (newcaps & CEPH_CAP_FILE_LAZYIO) == 0 &&
			 queue_invalidate)
			; /* do yesthing yet, invalidation will be queued */
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
		/* yesn-auth MDS is revoking the newly grant caps ? */
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

	if (le32_to_cpu(grant->op) == CEPH_CAP_OP_IMPORT) {
		if (newcaps & ~extra_info->issued)
			wake = true;
		kick_flushing_iyesde_caps(session->s_mdsc, session, iyesde);
		up_read(&session->s_mdsc->snap_rwsem);
	} else {
		spin_unlock(&ci->i_ceph_lock);
	}

	if (fill_inline)
		ceph_fill_inline_data(iyesde, NULL, extra_info->inline_data,
				      extra_info->inline_len);

	if (queue_trunc)
		ceph_queue_vmtruncate(iyesde);

	if (writeback)
		/*
		 * queue iyesde for writeback: we can't actually call
		 * filemap_write_and_wait, etc. from message handler
		 * context.
		 */
		ceph_queue_writeback(iyesde);
	if (queue_invalidate)
		ceph_queue_invalidate(iyesde);
	if (deleted_iyesde)
		invalidate_aliases(iyesde);
	if (wake)
		wake_up_all(&ci->i_cap_wq);

	if (check_caps == 1)
		ceph_check_caps(ci, CHECK_CAPS_NODELAY|CHECK_CAPS_AUTHONLY,
				session);
	else if (check_caps == 2)
		ceph_check_caps(ci, CHECK_CAPS_NODELAY, session);
	else
		mutex_unlock(&session->s_mutex);
}

/*
 * Handle FLUSH_ACK from MDS, indicating that metadata we sent to the
 * MDS has been safely committed.
 */
static void handle_cap_flush_ack(struct iyesde *iyesde, u64 flush_tid,
				 struct ceph_mds_caps *m,
				 struct ceph_mds_session *session,
				 struct ceph_cap *cap)
	__releases(ci->i_ceph_lock)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	struct ceph_mds_client *mdsc = ceph_sb_to_client(iyesde->i_sb)->mdsc;
	struct ceph_cap_flush *cf, *tmp_cf;
	LIST_HEAD(to_remove);
	unsigned seq = le32_to_cpu(m->seq);
	int dirty = le32_to_cpu(m->dirty);
	int cleaned = 0;
	bool drop = false;
	bool wake_ci = false;
	bool wake_mdsc = false;

	list_for_each_entry_safe(cf, tmp_cf, &ci->i_cap_flush_list, i_list) {
		if (cf->tid == flush_tid)
			cleaned = cf->caps;
		if (cf->caps == 0) /* capsnap */
			continue;
		if (cf->tid <= flush_tid) {
			if (__finish_cap_flush(NULL, ci, cf))
				wake_ci = true;
			list_add_tail(&cf->i_list, &to_remove);
		} else {
			cleaned &= ~cf->caps;
			if (!cleaned)
				break;
		}
	}

	dout("handle_cap_flush_ack iyesde %p mds%d seq %d on %s cleaned %s,"
	     " flushing %s -> %s\n",
	     iyesde, session->s_mds, seq, ceph_cap_string(dirty),
	     ceph_cap_string(cleaned), ceph_cap_string(ci->i_flushing_caps),
	     ceph_cap_string(ci->i_flushing_caps & ~cleaned));

	if (list_empty(&to_remove) && !cleaned)
		goto out;

	ci->i_flushing_caps &= ~cleaned;

	spin_lock(&mdsc->cap_dirty_lock);

	list_for_each_entry(cf, &to_remove, i_list) {
		if (__finish_cap_flush(mdsc, NULL, cf))
			wake_mdsc = true;
	}

	if (ci->i_flushing_caps == 0) {
		if (list_empty(&ci->i_cap_flush_list)) {
			list_del_init(&ci->i_flushing_item);
			if (!list_empty(&session->s_cap_flushing)) {
				dout(" mds%d still flushing cap on %p\n",
				     session->s_mds,
				     &list_first_entry(&session->s_cap_flushing,
						struct ceph_iyesde_info,
						i_flushing_item)->vfs_iyesde);
			}
		}
		mdsc->num_cap_flushing--;
		dout(" iyesde %p yesw !flushing\n", iyesde);

		if (ci->i_dirty_caps == 0) {
			dout(" iyesde %p yesw clean\n", iyesde);
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
		list_del(&cf->i_list);
		ceph_free_cap_flush(cf);
	}

	if (wake_ci)
		wake_up_all(&ci->i_cap_wq);
	if (wake_mdsc)
		wake_up_all(&mdsc->cap_flushing_wq);
	if (drop)
		iput(iyesde);
}

/*
 * Handle FLUSHSNAP_ACK.  MDS has flushed snap data to disk and we can
 * throw away our cap_snap.
 *
 * Caller hold s_mutex.
 */
static void handle_cap_flushsnap_ack(struct iyesde *iyesde, u64 flush_tid,
				     struct ceph_mds_caps *m,
				     struct ceph_mds_session *session)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	struct ceph_mds_client *mdsc = ceph_sb_to_client(iyesde->i_sb)->mdsc;
	u64 follows = le64_to_cpu(m->snap_follows);
	struct ceph_cap_snap *capsnap;
	bool flushed = false;
	bool wake_ci = false;
	bool wake_mdsc = false;

	dout("handle_cap_flushsnap_ack iyesde %p ci %p mds%d follows %lld\n",
	     iyesde, ci, session->s_mds, follows);

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
	if (flushed) {
		WARN_ON(capsnap->dirty_pages || capsnap->writing);
		dout(" removing %p cap_snap %p follows %lld\n",
		     iyesde, capsnap, follows);
		list_del(&capsnap->ci_item);
		if (__finish_cap_flush(NULL, ci, &capsnap->cap_flush))
			wake_ci = true;

		spin_lock(&mdsc->cap_dirty_lock);

		if (list_empty(&ci->i_cap_flush_list))
			list_del_init(&ci->i_flushing_item);

		if (__finish_cap_flush(mdsc, NULL, &capsnap->cap_flush))
			wake_mdsc = true;

		spin_unlock(&mdsc->cap_dirty_lock);
	}
	spin_unlock(&ci->i_ceph_lock);
	if (flushed) {
		ceph_put_snap_context(capsnap->context);
		ceph_put_cap_snap(capsnap);
		if (wake_ci)
			wake_up_all(&ci->i_cap_wq);
		if (wake_mdsc)
			wake_up_all(&mdsc->cap_flushing_wq);
		iput(iyesde);
	}
}

/*
 * Handle TRUNC from MDS, indicating file truncation.
 *
 * caller hold s_mutex.
 */
static void handle_cap_trunc(struct iyesde *iyesde,
			     struct ceph_mds_caps *trunc,
			     struct ceph_mds_session *session)
	__releases(ci->i_ceph_lock)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	int mds = session->s_mds;
	int seq = le32_to_cpu(trunc->seq);
	u32 truncate_seq = le32_to_cpu(trunc->truncate_seq);
	u64 truncate_size = le64_to_cpu(trunc->truncate_size);
	u64 size = le64_to_cpu(trunc->size);
	int implemented = 0;
	int dirty = __ceph_caps_dirty(ci);
	int issued = __ceph_caps_issued(ceph_iyesde(iyesde), &implemented);
	int queue_trunc = 0;

	issued |= implemented | dirty;

	dout("handle_cap_trunc iyesde %p mds%d seq %d to %lld seq %d\n",
	     iyesde, mds, seq, truncate_size, truncate_seq);
	queue_trunc = ceph_fill_file_size(iyesde, issued,
					  truncate_seq, truncate_size, size);
	spin_unlock(&ci->i_ceph_lock);

	if (queue_trunc)
		ceph_queue_vmtruncate(iyesde);
}

/*
 * Handle EXPORT from MDS.  Cap is being migrated _from_ this mds to a
 * different one.  If we are the most recent migration we've seen (as
 * indicated by mseq), make yeste of the migrating cap bits for the
 * duration (until we see the corresponding IMPORT).
 *
 * caller holds s_mutex
 */
static void handle_cap_export(struct iyesde *iyesde, struct ceph_mds_caps *ex,
			      struct ceph_mds_cap_peer *ph,
			      struct ceph_mds_session *session)
{
	struct ceph_mds_client *mdsc = ceph_iyesde_to_client(iyesde)->mdsc;
	struct ceph_mds_session *tsession = NULL;
	struct ceph_cap *cap, *tcap, *new_cap = NULL;
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
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

	dout("handle_cap_export iyesde %p ci %p mds%d mseq %d target %d\n",
	     iyesde, ci, mds, mseq, target);
retry:
	spin_lock(&ci->i_ceph_lock);
	cap = __get_cap_for_mds(ci, mds);
	if (!cap || cap->cap_id != le64_to_cpu(ex->cap_id))
		goto out_unlock;

	if (target < 0) {
		if (cap->mds_wanted | cap->issued)
			ci->i_ceph_flags |= CEPH_I_CAP_DROPPED;
		__ceph_remove_cap(cap, false);
		goto out_unlock;
	}

	/*
	 * yesw we kyesw we haven't received the cap import message yet
	 * because the exported cap still exist.
	 */

	issued = cap->issued;
	if (issued != cap->implemented)
		pr_err_ratelimited("handle_cap_export: issued != implemented: "
				"iyes (%llx.%llx) mds%d seq %d mseq %d "
				"issued %s implemented %s\n",
				ceph_viyesp(iyesde), mds, cap->seq, cap->mseq,
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
			if (cap == ci->i_auth_cap)
				ci->i_auth_cap = tcap;

			if (!list_empty(&ci->i_cap_flush_list) &&
			    ci->i_auth_cap == tcap) {
				spin_lock(&mdsc->cap_dirty_lock);
				list_move_tail(&ci->i_flushing_item,
					       &tcap->session->s_cap_flushing);
				spin_unlock(&mdsc->cap_dirty_lock);
			}
		}
		__ceph_remove_cap(cap, false);
		goto out_unlock;
	} else if (tsession) {
		/* add placeholder for the export tagert */
		int flag = (cap == ci->i_auth_cap) ? CEPH_CAP_FLAG_AUTH : 0;
		tcap = new_cap;
		ceph_add_cap(iyesde, tsession, t_cap_id, -1, issued, 0,
			     t_seq - 1, t_mseq, (u64)-1, flag, &new_cap);

		if (!list_empty(&ci->i_cap_flush_list) &&
		    ci->i_auth_cap == tcap) {
			spin_lock(&mdsc->cap_dirty_lock);
			list_move_tail(&ci->i_flushing_item,
				       &tcap->session->s_cap_flushing);
			spin_unlock(&mdsc->cap_dirty_lock);
		}

		__ceph_remove_cap(cap, false);
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
			      struct iyesde *iyesde, struct ceph_mds_caps *im,
			      struct ceph_mds_cap_peer *ph,
			      struct ceph_mds_session *session,
			      struct ceph_cap **target_cap, int *old_issued)
	__acquires(ci->i_ceph_lock)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	struct ceph_cap *cap, *ocap, *new_cap = NULL;
	int mds = session->s_mds;
	int issued;
	unsigned caps = le32_to_cpu(im->caps);
	unsigned wanted = le32_to_cpu(im->wanted);
	unsigned seq = le32_to_cpu(im->seq);
	unsigned mseq = le32_to_cpu(im->migrate_seq);
	u64 realmiyes = le64_to_cpu(im->realm);
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

	dout("handle_cap_import iyesde %p ci %p mds%d mseq %d peer %d\n",
	     iyesde, ci, mds, mseq, peer);

retry:
	spin_lock(&ci->i_ceph_lock);
	cap = __get_cap_for_mds(ci, mds);
	if (!cap) {
		if (!new_cap) {
			spin_unlock(&ci->i_ceph_lock);
			new_cap = ceph_get_cap(mdsc, NULL);
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

	ceph_add_cap(iyesde, session, cap_id, -1, caps, wanted, seq, mseq,
		     realmiyes, CEPH_CAP_FLAG_AUTH, &new_cap);

	ocap = peer >= 0 ? __get_cap_for_mds(ci, peer) : NULL;
	if (ocap && ocap->cap_id == p_cap_id) {
		dout(" remove export cap %p mds%d flags %d\n",
		     ocap, peer, ph->flags);
		if ((ph->flags & CEPH_CAP_FLAG_AUTH) &&
		    (ocap->seq != le32_to_cpu(ph->seq) ||
		     ocap->mseq != le32_to_cpu(ph->mseq))) {
			pr_err_ratelimited("handle_cap_import: "
					"mismatched seq/mseq: iyes (%llx.%llx) "
					"mds%d seq %d mseq %d importer mds%d "
					"has peer seq %d mseq %d\n",
					ceph_viyesp(iyesde), peer, ocap->seq,
					ocap->mseq, mds, le32_to_cpu(ph->seq),
					le32_to_cpu(ph->mseq));
		}
		__ceph_remove_cap(ocap, (ph->flags & CEPH_CAP_FLAG_RELEASE));
	}

	/* make sure we re-request max_size, if necessary */
	ci->i_requested_max_size = 0;

	*old_issued = issued;
	*target_cap = cap;
}

/*
 * Handle a caps message from the MDS.
 *
 * Identify the appropriate session, iyesde, and call the right handler
 * based on the cap op.
 */
void ceph_handle_caps(struct ceph_mds_session *session,
		      struct ceph_msg *msg)
{
	struct ceph_mds_client *mdsc = session->s_mdsc;
	struct iyesde *iyesde;
	struct ceph_iyesde_info *ci;
	struct ceph_cap *cap;
	struct ceph_mds_caps *h;
	struct ceph_mds_cap_peer *peer = NULL;
	struct ceph_snap_realm *realm = NULL;
	int op;
	int msg_version = le16_to_cpu(msg->hdr.version);
	u32 seq, mseq;
	struct ceph_viyes viyes;
	void *snaptrace;
	size_t snaptrace_len;
	void *p, *end;
	struct cap_extra_info extra_info = {};

	dout("handle_caps from mds%d\n", session->s_mds);

	/* decode */
	end = msg->front.iov_base + msg->front.iov_len;
	if (msg->front.iov_len < sizeof(*h))
		goto bad;
	h = msg->front.iov_base;
	op = le32_to_cpu(h->op);
	viyes.iyes = le64_to_cpu(h->iyes);
	viyes.snap = CEPH_NOSNAP;
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
		u64 flush_tid;
		u32 caller_uid, caller_gid;
		u32 pool_ns_len;

		/* version >= 6 */
		ceph_decode_64_safe(&p, end, flush_tid, bad);
		/* version >= 7 */
		ceph_decode_32_safe(&p, end, caller_uid, bad);
		ceph_decode_32_safe(&p, end, caller_gid, bad);
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
		u32 flags;
		/* version >= 10 */
		ceph_decode_32_safe(&p, end, flags, bad);
		/* version >= 11 */
		extra_info.dirstat_valid = true;
		ceph_decode_64_safe(&p, end, extra_info.nfiles, bad);
		ceph_decode_64_safe(&p, end, extra_info.nsubdirs, bad);
	}

	/* lookup iyes */
	iyesde = ceph_find_iyesde(mdsc->fsc->sb, viyes);
	ci = ceph_iyesde(iyesde);
	dout(" op %s iyes %llx.%llx iyesde %p\n", ceph_cap_op_name(op), viyes.iyes,
	     viyes.snap, iyesde);

	mutex_lock(&session->s_mutex);
	session->s_seq++;
	dout(" mds%d seq %lld cap seq %u\n", session->s_mds, session->s_seq,
	     (unsigned)seq);

	if (!iyesde) {
		dout(" i don't have iyes %llx\n", viyes.iyes);

		if (op == CEPH_CAP_OP_IMPORT) {
			cap = ceph_get_cap(mdsc, NULL);
			cap->cap_iyes = viyes.iyes;
			cap->queue_release = 1;
			cap->cap_id = le64_to_cpu(h->cap_id);
			cap->mseq = mseq;
			cap->seq = seq;
			cap->issue_seq = seq;
			spin_lock(&session->s_cap_lock);
			__ceph_queue_cap_release(session, cap);
			spin_unlock(&session->s_cap_lock);
		}
		goto done;
	}

	/* these will work even if we don't have a cap yet */
	switch (op) {
	case CEPH_CAP_OP_FLUSHSNAP_ACK:
		handle_cap_flushsnap_ack(iyesde, le64_to_cpu(msg->hdr.tid),
					 h, session);
		goto done;

	case CEPH_CAP_OP_EXPORT:
		handle_cap_export(iyesde, h, peer, session);
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
		handle_cap_import(mdsc, iyesde, h, peer, session,
				  &cap, &extra_info.issued);
		handle_cap_grant(iyesde, session, cap,
				 h, msg->middle, &extra_info);
		if (realm)
			ceph_put_snap_realm(mdsc, realm);
		goto done_unlocked;
	}

	/* the rest require a cap */
	spin_lock(&ci->i_ceph_lock);
	cap = __get_cap_for_mds(ceph_iyesde(iyesde), session->s_mds);
	if (!cap) {
		dout(" yes cap on %p iyes %llx.%llx from mds%d\n",
		     iyesde, ceph_iyes(iyesde), ceph_snap(iyesde),
		     session->s_mds);
		spin_unlock(&ci->i_ceph_lock);
		goto flush_cap_releases;
	}

	/* yeste that each of these drops i_ceph_lock for us */
	switch (op) {
	case CEPH_CAP_OP_REVOKE:
	case CEPH_CAP_OP_GRANT:
		__ceph_caps_issued(ci, &extra_info.issued);
		extra_info.issued |= __ceph_caps_dirty(ci);
		handle_cap_grant(iyesde, session, cap,
				 h, msg->middle, &extra_info);
		goto done_unlocked;

	case CEPH_CAP_OP_FLUSH_ACK:
		handle_cap_flush_ack(iyesde, le64_to_cpu(msg->hdr.tid),
				     h, session, cap);
		break;

	case CEPH_CAP_OP_TRUNC:
		handle_cap_trunc(iyesde, h, session);
		break;

	default:
		spin_unlock(&ci->i_ceph_lock);
		pr_err("ceph_handle_caps: unkyeswn cap op %d %s\n", op,
		       ceph_cap_op_name(op));
	}

done:
	mutex_unlock(&session->s_mutex);
done_unlocked:
	ceph_put_string(extra_info.pool_ns);
	/* avoid calling iput_final() in mds dispatch threads */
	ceph_async_iput(iyesde);
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
	return;
}

/*
 * Delayed work handler to process end of delayed cap release LRU list.
 */
void ceph_check_delayed_caps(struct ceph_mds_client *mdsc)
{
	struct iyesde *iyesde;
	struct ceph_iyesde_info *ci;
	int flags = CHECK_CAPS_NODELAY;

	dout("check_delayed_caps\n");
	while (1) {
		spin_lock(&mdsc->cap_delay_lock);
		if (list_empty(&mdsc->cap_delay_list))
			break;
		ci = list_first_entry(&mdsc->cap_delay_list,
				      struct ceph_iyesde_info,
				      i_cap_delay_list);
		if ((ci->i_ceph_flags & CEPH_I_FLUSH) == 0 &&
		    time_before(jiffies, ci->i_hold_caps_max))
			break;
		list_del_init(&ci->i_cap_delay_list);

		iyesde = igrab(&ci->vfs_iyesde);
		spin_unlock(&mdsc->cap_delay_lock);

		if (iyesde) {
			dout("check_delayed_caps on %p\n", iyesde);
			ceph_check_caps(ci, flags, NULL);
			/* avoid calling iput_final() in tick thread */
			ceph_async_iput(iyesde);
		}
	}
	spin_unlock(&mdsc->cap_delay_lock);
}

/*
 * Flush all dirty caps to the mds
 */
void ceph_flush_dirty_caps(struct ceph_mds_client *mdsc)
{
	struct ceph_iyesde_info *ci;
	struct iyesde *iyesde;

	dout("flush_dirty_caps\n");
	spin_lock(&mdsc->cap_dirty_lock);
	while (!list_empty(&mdsc->cap_dirty)) {
		ci = list_first_entry(&mdsc->cap_dirty, struct ceph_iyesde_info,
				      i_dirty_item);
		iyesde = &ci->vfs_iyesde;
		ihold(iyesde);
		dout("flush_dirty_caps %p\n", iyesde);
		spin_unlock(&mdsc->cap_dirty_lock);
		ceph_check_caps(ci, CHECK_CAPS_NODELAY|CHECK_CAPS_FLUSH, NULL);
		iput(iyesde);
		spin_lock(&mdsc->cap_dirty_lock);
	}
	spin_unlock(&mdsc->cap_dirty_lock);
	dout("flush_dirty_caps done\n");
}

void __ceph_get_fmode(struct ceph_iyesde_info *ci, int fmode)
{
	int i;
	int bits = (fmode << 1) | 1;
	for (i = 0; i < CEPH_FILE_MODE_BITS; i++) {
		if (bits & (1 << i))
			ci->i_nr_by_mode[i]++;
	}
}

/*
 * Drop open file reference.  If we were the last open file,
 * we may need to release capabilities to the MDS (or schedule
 * their delayed release).
 */
void ceph_put_fmode(struct ceph_iyesde_info *ci, int fmode)
{
	int i, last = 0;
	int bits = (fmode << 1) | 1;
	spin_lock(&ci->i_ceph_lock);
	for (i = 0; i < CEPH_FILE_MODE_BITS; i++) {
		if (bits & (1 << i)) {
			BUG_ON(ci->i_nr_by_mode[i] == 0);
			if (--ci->i_nr_by_mode[i] == 0)
				last++;
		}
	}
	dout("put_fmode %p fmode %d {%d,%d,%d,%d}\n",
	     &ci->vfs_iyesde, fmode,
	     ci->i_nr_by_mode[0], ci->i_nr_by_mode[1],
	     ci->i_nr_by_mode[2], ci->i_nr_by_mode[3]);
	spin_unlock(&ci->i_ceph_lock);

	if (last && ci->i_viyes.snap == CEPH_NOSNAP)
		ceph_check_caps(ci, 0, NULL);
}

/*
 * For a soon-to-be unlinked file, drop the LINK caps. If it
 * looks like the link count will hit 0, drop any other caps (other
 * than PIN) we don't specifically want (due to the file still being
 * open).
 */
int ceph_drop_caps_for_unlink(struct iyesde *iyesde)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	int drop = CEPH_CAP_LINK_SHARED | CEPH_CAP_LINK_EXCL;

	spin_lock(&ci->i_ceph_lock);
	if (iyesde->i_nlink == 1) {
		drop |= ~(__ceph_caps_wanted(ci) | CEPH_CAP_PIN);

		ci->i_ceph_flags |= CEPH_I_NODELAY;
		if (__ceph_caps_dirty(ci)) {
			struct ceph_mds_client *mdsc =
				ceph_iyesde_to_client(iyesde)->mdsc;
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
 * record for the directory iyesde, even when there aren't any caps to
 * drop.
 */
int ceph_encode_iyesde_release(void **p, struct iyesde *iyesde,
			      int mds, int drop, int unless, int force)
{
	struct ceph_iyesde_info *ci = ceph_iyesde(iyesde);
	struct ceph_cap *cap;
	struct ceph_mds_request_release *rel = *p;
	int used, dirty;
	int ret = 0;

	spin_lock(&ci->i_ceph_lock);
	used = __ceph_caps_used(ci);
	dirty = __ceph_caps_dirty(ci);

	dout("encode_iyesde_release %p mds%d used|dirty %s drop %s unless %s\n",
	     iyesde, mds, ceph_cap_string(used|dirty), ceph_cap_string(drop),
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
				if ((ci->i_ceph_flags & CEPH_I_NODELAY) == 0)
					wanted |= cap->mds_wanted;
				dout("encode_iyesde_release %p cap %p "
				     "%s -> %s, wanted %s -> %s\n", iyesde, cap,
				     ceph_cap_string(cap->issued),
				     ceph_cap_string(cap->issued & ~drop),
				     ceph_cap_string(cap->mds_wanted),
				     ceph_cap_string(wanted));

				cap->issued &= ~drop;
				cap->implemented &= ~drop;
				cap->mds_wanted = wanted;
			} else {
				dout("encode_iyesde_release %p cap %p %s"
				     " (force)\n", iyesde, cap,
				     ceph_cap_string(cap->issued));
			}

			rel->iyes = cpu_to_le64(ceph_iyes(iyesde));
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
			dout("encode_iyesde_release %p cap %p %s (yesop)\n",
			     iyesde, cap, ceph_cap_string(cap->issued));
		}
	}
	spin_unlock(&ci->i_ceph_lock);
	return ret;
}

int ceph_encode_dentry_release(void **p, struct dentry *dentry,
			       struct iyesde *dir,
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
		dir = d_iyesde(parent);
	}
	spin_unlock(&dentry->d_lock);

	ret = ceph_encode_iyesde_release(p, dir, mds, drop, unless, force);
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
