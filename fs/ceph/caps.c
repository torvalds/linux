#include <linux/ceph/ceph_debug.h>

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/writeback.h>

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

void ceph_adjust_min_caps(struct ceph_mds_client *mdsc, int delta)
{
	spin_lock(&mdsc->caps_list_lock);
	mdsc->caps_min_count += delta;
	BUG_ON(mdsc->caps_min_count < 0);
	spin_unlock(&mdsc->caps_list_lock);
}

void ceph_reserve_caps(struct ceph_mds_client *mdsc,
		      struct ceph_cap_reservation *ctx, int need)
{
	int i;
	struct ceph_cap *cap;
	int have;
	int alloc = 0;
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

	for (i = have; i < need; i++) {
		cap = kmem_cache_alloc(ceph_cap_cachep, GFP_NOFS);
		if (!cap)
			break;
		list_add(&cap->caps_item, &newcaps);
		alloc++;
	}
	/* we didn't manage to reserve as much as we needed */
	if (have + alloc != need)
		pr_warn("reserve caps ctx=%p ENOMEM need=%d got=%d\n",
			ctx, need, have + alloc);

	spin_lock(&mdsc->caps_list_lock);
	mdsc->caps_total_count += alloc;
	mdsc->caps_reserve_count += alloc;
	list_splice(&newcaps, &mdsc->caps_list);

	BUG_ON(mdsc->caps_total_count != mdsc->caps_use_count +
					 mdsc->caps_reserve_count +
					 mdsc->caps_avail_count);
	spin_unlock(&mdsc->caps_list_lock);

	ctx->count = need;
	dout("reserve caps ctx=%p %d = %d used + %d resv + %d avail\n",
	     ctx, mdsc->caps_total_count, mdsc->caps_use_count,
	     mdsc->caps_reserve_count, mdsc->caps_avail_count);
}

int ceph_unreserve_caps(struct ceph_mds_client *mdsc,
			struct ceph_cap_reservation *ctx)
{
	dout("unreserve caps ctx=%p count=%d\n", ctx, ctx->count);
	if (ctx->count) {
		spin_lock(&mdsc->caps_list_lock);
		BUG_ON(mdsc->caps_reserve_count < ctx->count);
		mdsc->caps_reserve_count -= ctx->count;
		mdsc->caps_avail_count += ctx->count;
		ctx->count = 0;
		dout("unreserve caps %d = %d used + %d resv + %d avail\n",
		     mdsc->caps_total_count, mdsc->caps_use_count,
		     mdsc->caps_reserve_count, mdsc->caps_avail_count);
		BUG_ON(mdsc->caps_total_count != mdsc->caps_use_count +
						 mdsc->caps_reserve_count +
						 mdsc->caps_avail_count);
		spin_unlock(&mdsc->caps_list_lock);
	}
	return 0;
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
 * Return id of any MDS with a cap, preferably FILE_WR|BUFFER|EXCL, else -1.
 */
static int __ceph_get_cap_mds(struct ceph_inode_info *ci)
{
	struct ceph_cap *cap;
	int mds = -1;
	struct rb_node *p;

	/* prefer mds with WR|BUFFER|EXCL caps */
	for (p = rb_first(&ci->i_caps); p; p = rb_next(p)) {
		cap = rb_entry(p, struct ceph_cap, ci_node);
		mds = cap->mds;
		if (cap->issued & (CEPH_CAP_FILE_WR |
				   CEPH_CAP_FILE_BUFFER |
				   CEPH_CAP_FILE_EXCL))
			break;
	}
	return mds;
}

int ceph_get_cap_mds(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int mds;
	spin_lock(&ci->i_ceph_lock);
	mds = __ceph_get_cap_mds(ceph_inode(inode));
	spin_unlock(&ci->i_ceph_lock);
	return mds;
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
	struct ceph_mount_options *ma = mdsc->fsc->mount_options;

	ci->i_hold_caps_min = round_jiffies(jiffies +
					    ma->caps_wanted_delay_min * HZ);
	ci->i_hold_caps_max = round_jiffies(jiffies +
					    ma->caps_wanted_delay_max * HZ);
	dout("__cap_set_timeouts %p min %lu max %lu\n", &ci->vfs_inode,
	     ci->i_hold_caps_min - jiffies, ci->i_hold_caps_max - jiffies);
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
	__cap_set_timeouts(mdsc, ci);
	dout("__cap_delay_requeue %p flags %d at %lu\n", &ci->vfs_inode,
	     ci->i_ceph_flags, ci->i_hold_caps_max);
	if (!mdsc->stopping) {
		spin_lock(&mdsc->cap_delay_lock);
		if (!list_empty(&ci->i_cap_delay_list)) {
			if (ci->i_ceph_flags & CEPH_I_FLUSH)
				goto no_change;
			list_del_init(&ci->i_cap_delay_list);
		}
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

/*
 * Common issue checks for add_cap, handle_cap_grant.
 */
static void __check_cap_issue(struct ceph_inode_info *ci, struct ceph_cap *cap,
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
	 * if we are newly issued FILE_SHARED, mark dir not complete; we
	 * don't know what happened to this directory while we didn't
	 * have the cap.
	 */
	if ((issued & CEPH_CAP_FILE_SHARED) &&
	    (had & CEPH_CAP_FILE_SHARED) == 0) {
		ci->i_shared_gen++;
		if (S_ISDIR(ci->vfs_inode.i_mode)) {
			dout(" marking %p NOT complete\n", &ci->vfs_inode);
			__ceph_dir_clear_complete(ci);
		}
	}
}

/*
 * Add a capability under the given MDS session.
 *
 * Caller should hold session snap_rwsem (read) and s_mutex.
 *
 * @fmode is the open file mode, if we are opening a file, otherwise
 * it is < 0.  (This is so we can atomically add the cap and add an
 * open file reference to it.)
 */
void ceph_add_cap(struct inode *inode,
		  struct ceph_mds_session *session, u64 cap_id,
		  int fmode, unsigned issued, unsigned wanted,
		  unsigned seq, unsigned mseq, u64 realmino, int flags,
		  struct ceph_cap **new_cap)
{
	struct ceph_mds_client *mdsc = ceph_inode_to_client(inode)->mdsc;
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_cap *cap;
	int mds = session->s_mds;
	int actual_wanted;

	dout("add_cap %p mds%d cap %llx %s seq %d\n", inode,
	     session->s_mds, cap_id, ceph_cap_string(issued), seq);

	/*
	 * If we are opening the file, include file mode wanted bits
	 * in wanted.
	 */
	if (fmode >= 0)
		wanted |= ceph_caps_for_mode(fmode);

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
		spin_unlock(&session->s_cap_lock);
	} else {
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

	if (!ci->i_snap_realm) {
		/*
		 * add this inode to the appropriate snap realm
		 */
		struct ceph_snap_realm *realm = ceph_lookup_snap_realm(mdsc,
							       realmino);
		if (realm) {
			spin_lock(&realm->inodes_with_caps_lock);
			ci->i_snap_realm = realm;
			list_add(&ci->i_snap_realm_item,
				 &realm->inodes_with_caps);
			spin_unlock(&realm->inodes_with_caps_lock);
		} else {
			pr_err("ceph_add_cap: couldn't find snap realm %llx\n",
			       realmino);
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
		__cap_delay_requeue(mdsc, ci);
	}

	if (flags & CEPH_CAP_FLAG_AUTH) {
		if (ci->i_auth_cap == NULL ||
		    ceph_seq_cmp(ci->i_auth_cap->mseq, mseq) < 0) {
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
	cap->cap_gen = session->s_cap_gen;

	if (fmode >= 0)
		__ceph_get_fmode(ci, fmode);
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

	spin_lock(&cap->session->s_gen_ttl_lock);
	gen = cap->session->s_cap_gen;
	ttl = cap->session->s_cap_ttl;
	spin_unlock(&cap->session->s_gen_ttl_lock);

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
	if (s->s_cap_iterator == NULL) {
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
		dout("__ceph_caps_issued_mask %p snap issued %s"
		     " (mask %s)\n", &ci->vfs_inode,
		     ceph_cap_string(have),
		     ceph_cap_string(mask));
		return 1;
	}

	for (p = rb_first(&ci->i_caps); p; p = rb_next(p)) {
		cap = rb_entry(p, struct ceph_cap, ci_node);
		if (!__cap_is_valid(cap))
			continue;
		if ((cap->issued & mask) == mask) {
			dout("__ceph_caps_issued_mask %p cap %p issued %s"
			     " (mask %s)\n", &ci->vfs_inode, cap,
			     ceph_cap_string(cap->issued),
			     ceph_cap_string(mask));
			if (touch)
				__touch_cap(cap);
			return 1;
		}

		/* does a combination of caps satisfy mask? */
		have |= cap->issued;
		if ((have & mask) == mask) {
			dout("__ceph_caps_issued_mask %p combo issued %s"
			     " (mask %s)\n", &ci->vfs_inode,
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
	    (!S_ISDIR(ci->vfs_inode.i_mode) && /* ignore readdir cache */
	     ci->vfs_inode.i_data.nrpages))
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
int __ceph_caps_file_wanted(struct ceph_inode_info *ci)
{
	int want = 0;
	int mode;
	for (mode = 0; mode < CEPH_FILE_MODE_NUM; mode++)
		if (ci->i_nr_by_mode[mode])
			want |= ceph_caps_for_mode(mode);
	return want;
}

/*
 * Return caps we have registered with the MDS(s) as 'wanted'.
 */
int __ceph_caps_mds_wanted(struct ceph_inode_info *ci)
{
	struct ceph_cap *cap;
	struct rb_node *p;
	int mds_wanted = 0;

	for (p = rb_first(&ci->i_caps); p; p = rb_next(p)) {
		cap = rb_entry(p, struct ceph_cap, ci_node);
		if (!__cap_is_valid(cap))
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
static int __ceph_is_any_caps(struct ceph_inode_info *ci)
{
	return !RB_EMPTY_ROOT(&ci->i_caps);
}

int ceph_is_any_caps(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int ret;

	spin_lock(&ci->i_ceph_lock);
	ret = __ceph_is_any_caps(ci);
	spin_unlock(&ci->i_ceph_lock);

	return ret;
}

static void drop_inode_snap_realm(struct ceph_inode_info *ci)
{
	struct ceph_snap_realm *realm = ci->i_snap_realm;
	spin_lock(&realm->inodes_with_caps_lock);
	list_del_init(&ci->i_snap_realm_item);
	ci->i_snap_realm_counter++;
	ci->i_snap_realm = NULL;
	spin_unlock(&realm->inodes_with_caps_lock);
	ceph_put_snap_realm(ceph_sb_to_client(ci->vfs_inode.i_sb)->mdsc,
			    realm);
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
	struct ceph_mds_client *mdsc =
		ceph_sb_to_client(ci->vfs_inode.i_sb)->mdsc;
	int removed = 0;

	dout("__ceph_remove_cap %p from %p\n", cap, &ci->vfs_inode);

	/* remove from session list */
	spin_lock(&session->s_cap_lock);
	if (session->s_cap_iterator == cap) {
		/* not yet, we are iterating over this very cap */
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
	 * s_cap_reconnect is protected by s_cap_lock. no one changes
	 * s_cap_gen while session is in the reconnect state.
	 */
	if (queue_release &&
	    (!session->s_cap_reconnect || cap->cap_gen == session->s_cap_gen)) {
		cap->queue_release = 1;
		if (removed) {
			list_add_tail(&cap->session_caps,
				      &session->s_cap_releases);
			session->s_num_cap_releases++;
			removed = 0;
		}
	} else {
		cap->queue_release = 0;
	}
	cap->cap_ino = ci->i_vino.ino;

	spin_unlock(&session->s_cap_lock);

	/* remove from inode list */
	rb_erase(&cap->ci_node, &ci->i_caps);
	if (ci->i_auth_cap == cap)
		ci->i_auth_cap = NULL;

	if (removed)
		ceph_put_cap(mdsc, cap);

	/* when reconnect denied, we remove session caps forcibly,
	 * i_wr_ref can be non-zero. If there are ongoing write,
	 * keep i_snap_realm.
	 */
	if (!__ceph_is_any_caps(ci) && ci->i_wr_ref == 0 && ci->i_snap_realm)
		drop_inode_snap_realm(ci);

	if (!__ceph_is_any_real_caps(ci))
		__cap_delay_cancel(mdsc, ci);
}

/*
 * Build and send a cap message to the given MDS.
 *
 * Caller should be holding s_mutex.
 */
static int send_cap_msg(struct ceph_mds_session *session,
			u64 ino, u64 cid, int op,
			int caps, int wanted, int dirty,
			u32 seq, u64 flush_tid, u64 oldest_flush_tid,
			u32 issue_seq, u32 mseq, u64 size, u64 max_size,
			struct timespec *mtime, struct timespec *atime,
			u64 time_warp_seq,
			kuid_t uid, kgid_t gid, umode_t mode,
			u64 xattr_version,
			struct ceph_buffer *xattrs_buf,
			u64 follows, bool inline_data)
{
	struct ceph_mds_caps *fc;
	struct ceph_msg *msg;
	void *p;
	size_t extra_len;

	dout("send_cap_msg %s %llx %llx caps %s wanted %s dirty %s"
	     " seq %u/%u tid %llu/%llu mseq %u follows %lld size %llu/%llu"
	     " xattr_ver %llu xattr_len %d\n", ceph_cap_op_name(op),
	     cid, ino, ceph_cap_string(caps), ceph_cap_string(wanted),
	     ceph_cap_string(dirty),
	     seq, issue_seq, flush_tid, oldest_flush_tid,
	     mseq, follows, size, max_size,
	     xattr_version, xattrs_buf ? (int)xattrs_buf->vec.iov_len : 0);

	/* flock buffer size + inline version + inline data size +
	 * osd_epoch_barrier + oldest_flush_tid */
	extra_len = 4 + 8 + 4 + 4 + 8;
	msg = ceph_msg_new(CEPH_MSG_CLIENT_CAPS, sizeof(*fc) + extra_len,
			   GFP_NOFS, false);
	if (!msg)
		return -ENOMEM;

	msg->hdr.version = cpu_to_le16(6);
	msg->hdr.tid = cpu_to_le64(flush_tid);

	fc = msg->front.iov_base;
	memset(fc, 0, sizeof(*fc));

	fc->cap_id = cpu_to_le64(cid);
	fc->op = cpu_to_le32(op);
	fc->seq = cpu_to_le32(seq);
	fc->issue_seq = cpu_to_le32(issue_seq);
	fc->migrate_seq = cpu_to_le32(mseq);
	fc->caps = cpu_to_le32(caps);
	fc->wanted = cpu_to_le32(wanted);
	fc->dirty = cpu_to_le32(dirty);
	fc->ino = cpu_to_le64(ino);
	fc->snap_follows = cpu_to_le64(follows);

	fc->size = cpu_to_le64(size);
	fc->max_size = cpu_to_le64(max_size);
	if (mtime)
		ceph_encode_timespec(&fc->mtime, mtime);
	if (atime)
		ceph_encode_timespec(&fc->atime, atime);
	fc->time_warp_seq = cpu_to_le32(time_warp_seq);

	fc->uid = cpu_to_le32(from_kuid(&init_user_ns, uid));
	fc->gid = cpu_to_le32(from_kgid(&init_user_ns, gid));
	fc->mode = cpu_to_le32(mode);

	p = fc + 1;
	/* flock buffer size */
	ceph_encode_32(&p, 0);
	/* inline version */
	ceph_encode_64(&p, inline_data ? 0 : CEPH_INLINE_NONE);
	/* inline data size */
	ceph_encode_32(&p, 0);
	/* osd_epoch_barrier */
	ceph_encode_32(&p, 0);
	/* oldest_flush_tid */
	ceph_encode_64(&p, oldest_flush_tid);

	fc->xattr_version = cpu_to_le64(xattr_version);
	if (xattrs_buf) {
		msg->middle = ceph_buffer_get(xattrs_buf);
		fc->xattr_len = cpu_to_le32(xattrs_buf->vec.iov_len);
		msg->hdr.middle_len = cpu_to_le32(xattrs_buf->vec.iov_len);
	}

	ceph_con_send(&session->s_con, msg);
	return 0;
}

/*
 * Queue cap releases when an inode is dropped from our cache.  Since
 * inode is about to be destroyed, there is no need for i_ceph_lock.
 */
void ceph_queue_caps_release(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct rb_node *p;

	p = rb_first(&ci->i_caps);
	while (p) {
		struct ceph_cap *cap = rb_entry(p, struct ceph_cap, ci_node);
		p = rb_next(p);
		__ceph_remove_cap(cap, true);
	}
}

/*
 * Send a cap msg on the given inode.  Update our caps state, then
 * drop i_ceph_lock and send the message.
 *
 * Make note of max_size reported/requested from mds, revoked caps
 * that have now been implemented.
 *
 * Make half-hearted attempt ot to invalidate page cache if we are
 * dropping RDCACHE.  Note that this will leave behind locked pages
 * that we'll then need to deal with elsewhere.
 *
 * Return non-zero if delayed release, or we experienced an error
 * such that the caller should requeue + retry later.
 *
 * called with i_ceph_lock, then drops it.
 * caller should hold snap_rwsem (read), s_mutex.
 */
static int __send_cap(struct ceph_mds_client *mdsc, struct ceph_cap *cap,
		      int op, int used, int want, int retain, int flushing,
		      u64 flush_tid, u64 oldest_flush_tid)
	__releases(cap->ci->i_ceph_lock)
{
	struct ceph_inode_info *ci = cap->ci;
	struct inode *inode = &ci->vfs_inode;
	u64 cap_id = cap->cap_id;
	int held, revoking, dropping, keep;
	u64 seq, issue_seq, mseq, time_warp_seq, follows;
	u64 size, max_size;
	struct timespec mtime, atime;
	int wake = 0;
	umode_t mode;
	kuid_t uid;
	kgid_t gid;
	struct ceph_mds_session *session;
	u64 xattr_version = 0;
	struct ceph_buffer *xattr_blob = NULL;
	int delayed = 0;
	int ret;
	bool inline_data;

	held = cap->issued | cap->implemented;
	revoking = cap->implemented & ~cap->issued;
	retain &= ~revoking;
	dropping = cap->issued & ~retain;

	dout("__send_cap %p cap %p session %p %s -> %s (revoking %s)\n",
	     inode, cap, cap->session,
	     ceph_cap_string(held), ceph_cap_string(held & retain),
	     ceph_cap_string(revoking));
	BUG_ON((retain & CEPH_CAP_PIN) == 0);

	session = cap->session;

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

	follows = flushing ? ci->i_head_snapc->seq : 0;

	keep = cap->implemented;
	seq = cap->seq;
	issue_seq = cap->issue_seq;
	mseq = cap->mseq;
	size = inode->i_size;
	ci->i_reported_size = size;
	max_size = ci->i_wanted_max_size;
	ci->i_requested_max_size = max_size;
	mtime = inode->i_mtime;
	atime = inode->i_atime;
	time_warp_seq = ci->i_time_warp_seq;
	uid = inode->i_uid;
	gid = inode->i_gid;
	mode = inode->i_mode;

	if (flushing & CEPH_CAP_XATTR_EXCL) {
		__ceph_build_xattrs_blob(ci);
		xattr_blob = ci->i_xattrs.blob;
		xattr_version = ci->i_xattrs.version;
	}

	inline_data = ci->i_inline_version != CEPH_INLINE_NONE;

	spin_unlock(&ci->i_ceph_lock);

	ret = send_cap_msg(session, ceph_vino(inode).ino, cap_id,
		op, keep, want, flushing, seq,
		flush_tid, oldest_flush_tid, issue_seq, mseq,
		size, max_size, &mtime, &atime, time_warp_seq,
		uid, gid, mode, xattr_version, xattr_blob,
		follows, inline_data);
	if (ret < 0) {
		dout("error sending cap msg, must requeue %p\n", inode);
		delayed = 1;
	}

	if (wake)
		wake_up_all(&ci->i_cap_wq);

	return delayed;
}

/*
 * When a snapshot is taken, clients accumulate dirty metadata on
 * inodes with capabilities in ceph_cap_snaps to describe the file
 * state at the time the snapshot was taken.  This must be flushed
 * asynchronously back to the MDS once sync writes complete and dirty
 * data is written out.
 *
 * Unless @kick is true, skip cap_snaps that were already sent to
 * the MDS (i.e., during this session).
 *
 * Called under i_ceph_lock.  Takes s_mutex as needed.
 */
void __ceph_flush_snaps(struct ceph_inode_info *ci,
			struct ceph_mds_session **psession,
			int kick)
		__releases(ci->i_ceph_lock)
		__acquires(ci->i_ceph_lock)
{
	struct inode *inode = &ci->vfs_inode;
	int mds;
	struct ceph_cap_snap *capsnap;
	u32 mseq;
	struct ceph_mds_client *mdsc = ceph_inode_to_client(inode)->mdsc;
	struct ceph_mds_session *session = NULL; /* if session != NULL, we hold
						    session->s_mutex */
	u64 next_follows = 0;  /* keep track of how far we've gotten through the
			     i_cap_snaps list, and skip these entries next time
			     around to avoid an infinite loop */

	if (psession)
		session = *psession;

	dout("__flush_snaps %p\n", inode);
retry:
	list_for_each_entry(capsnap, &ci->i_cap_snaps, ci_item) {
		/* avoid an infiniute loop after retry */
		if (capsnap->follows < next_follows)
			continue;
		/*
		 * we need to wait for sync writes to complete and for dirty
		 * pages to be written out.
		 */
		if (capsnap->dirty_pages || capsnap->writing)
			break;

		/* should be removed by ceph_try_drop_cap_snap() */
		BUG_ON(!capsnap->need_flush);

		/* pick mds, take s_mutex */
		if (ci->i_auth_cap == NULL) {
			dout("no auth cap (migrating?), doing nothing\n");
			goto out;
		}

		/* only flush each capsnap once */
		if (!kick && !list_empty(&capsnap->flushing_item)) {
			dout("already flushed %p, skipping\n", capsnap);
			continue;
		}

		mds = ci->i_auth_cap->session->s_mds;
		mseq = ci->i_auth_cap->mseq;

		if (session && session->s_mds != mds) {
			dout("oops, wrong session %p mutex\n", session);
			if (kick)
				goto out;

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
				dout("inverting session/ino locks on %p\n",
				     session);
				mutex_lock(&session->s_mutex);
			}
			/*
			 * if session == NULL, we raced against a cap
			 * deletion or migration.  retry, and we'll
			 * get a better @mds value next time.
			 */
			spin_lock(&ci->i_ceph_lock);
			goto retry;
		}

		spin_lock(&mdsc->cap_dirty_lock);
		capsnap->flush_tid = ++mdsc->last_cap_flush_tid;
		spin_unlock(&mdsc->cap_dirty_lock);

		atomic_inc(&capsnap->nref);
		if (list_empty(&capsnap->flushing_item))
			list_add_tail(&capsnap->flushing_item,
				      &session->s_cap_snaps_flushing);
		spin_unlock(&ci->i_ceph_lock);

		dout("flush_snaps %p cap_snap %p follows %lld tid %llu\n",
		     inode, capsnap, capsnap->follows, capsnap->flush_tid);
		send_cap_msg(session, ceph_vino(inode).ino, 0,
			     CEPH_CAP_OP_FLUSHSNAP, capsnap->issued, 0,
			     capsnap->dirty, 0, capsnap->flush_tid, 0,
			     0, mseq, capsnap->size, 0,
			     &capsnap->mtime, &capsnap->atime,
			     capsnap->time_warp_seq,
			     capsnap->uid, capsnap->gid, capsnap->mode,
			     capsnap->xattr_version, capsnap->xattr_blob,
			     capsnap->follows, capsnap->inline_data);

		next_follows = capsnap->follows + 1;
		ceph_put_cap_snap(capsnap);

		spin_lock(&ci->i_ceph_lock);
		goto retry;
	}

	/* we flushed them all; remove this inode from the queue */
	spin_lock(&mdsc->snap_flush_lock);
	list_del_init(&ci->i_snap_flush_item);
	spin_unlock(&mdsc->snap_flush_lock);

out:
	if (psession)
		*psession = session;
	else if (session) {
		mutex_unlock(&session->s_mutex);
		ceph_put_mds_session(session);
	}
}

static void ceph_flush_snaps(struct ceph_inode_info *ci)
{
	spin_lock(&ci->i_ceph_lock);
	__ceph_flush_snaps(ci, NULL, 0);
	spin_unlock(&ci->i_ceph_lock);
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
		list_add(&ci->i_dirty_item, &mdsc->cap_dirty);
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

static void __add_cap_flushing_to_inode(struct ceph_inode_info *ci,
					struct ceph_cap_flush *cf)
{
	struct rb_node **p = &ci->i_cap_flush_tree.rb_node;
	struct rb_node *parent = NULL;
	struct ceph_cap_flush *other = NULL;

	while (*p) {
		parent = *p;
		other = rb_entry(parent, struct ceph_cap_flush, i_node);

		if (cf->tid < other->tid)
			p = &(*p)->rb_left;
		else if (cf->tid > other->tid)
			p = &(*p)->rb_right;
		else
			BUG();
	}

	rb_link_node(&cf->i_node, parent, p);
	rb_insert_color(&cf->i_node, &ci->i_cap_flush_tree);
}

static void __add_cap_flushing_to_mdsc(struct ceph_mds_client *mdsc,
				       struct ceph_cap_flush *cf)
{
	struct rb_node **p = &mdsc->cap_flush_tree.rb_node;
	struct rb_node *parent = NULL;
	struct ceph_cap_flush *other = NULL;

	while (*p) {
		parent = *p;
		other = rb_entry(parent, struct ceph_cap_flush, g_node);

		if (cf->tid < other->tid)
			p = &(*p)->rb_left;
		else if (cf->tid > other->tid)
			p = &(*p)->rb_right;
		else
			BUG();
	}

	rb_link_node(&cf->g_node, parent, p);
	rb_insert_color(&cf->g_node, &mdsc->cap_flush_tree);
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
	struct rb_node *n = rb_first(&mdsc->cap_flush_tree);
	if (n) {
		struct ceph_cap_flush *cf =
			rb_entry(n, struct ceph_cap_flush, g_node);
		return cf->tid;
	}
	return 0;
}

/*
 * Add dirty inode to the flushing list.  Assigned a seq number so we
 * can wait for caps to flush without starving.
 *
 * Called under i_ceph_lock.
 */
static int __mark_caps_flushing(struct inode *inode,
				struct ceph_mds_session *session,
				u64 *flush_tid, u64 *oldest_flush_tid)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;
	struct ceph_inode_info *ci = ceph_inode(inode);
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
	dout(" inode %p now !dirty\n", inode);

	swap(cf, ci->i_prealloc_cap_flush);
	cf->caps = flushing;

	spin_lock(&mdsc->cap_dirty_lock);
	list_del_init(&ci->i_dirty_item);

	cf->tid = ++mdsc->last_cap_flush_tid;
	__add_cap_flushing_to_mdsc(mdsc, cf);
	*oldest_flush_tid = __get_oldest_flush_tid(mdsc);

	if (list_empty(&ci->i_flushing_item)) {
		list_add_tail(&ci->i_flushing_item, &session->s_cap_flushing);
		mdsc->num_cap_flushing++;
		dout(" inode %p now flushing tid %llu\n", inode, cf->tid);
	} else {
		list_move_tail(&ci->i_flushing_item, &session->s_cap_flushing);
		dout(" inode %p now flushing (more) tid %llu\n",
		     inode, cf->tid);
	}
	spin_unlock(&mdsc->cap_dirty_lock);

	__add_cap_flushing_to_inode(ci, cf);

	*flush_tid = cf->tid;
	return flushing;
}

/*
 * try to invalidate mapping pages without blocking.
 */
static int try_nonblocking_invalidate(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	u32 invalidating_gen = ci->i_rdcache_gen;

	spin_unlock(&ci->i_ceph_lock);
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

/*
 * Swiss army knife function to examine currently used and wanted
 * versus held caps.  Release, flush, ack revoked caps to mds as
 * appropriate.
 *
 *  CHECK_CAPS_NODELAY - caller is delayed work and we should not delay
 *    cap release further.
 *  CHECK_CAPS_AUTHONLY - we should only check the auth cap
 *  CHECK_CAPS_FLUSH - we should flush any dirty caps immediately, without
 *    further delay.
 */
void ceph_check_caps(struct ceph_inode_info *ci, int flags,
		     struct ceph_mds_session *session)
{
	struct ceph_fs_client *fsc = ceph_inode_to_client(&ci->vfs_inode);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct inode *inode = &ci->vfs_inode;
	struct ceph_cap *cap;
	u64 flush_tid, oldest_flush_tid;
	int file_wanted, used, cap_used;
	int took_snap_rwsem = 0;             /* true if mdsc->snap_rwsem held */
	int issued, implemented, want, retain, revoking, flushing = 0;
	int mds = -1;   /* keep track of how far we've gone through i_caps list
			   to avoid an infinite loop on retry */
	struct rb_node *p;
	int tried_invalidate = 0;
	int delayed = 0, sent = 0, force_requeue = 0, num;
	int queue_invalidate = 0;
	int is_delayed = flags & CHECK_CAPS_NODELAY;

	/* if we are unmounting, flush any unused caps immediately. */
	if (mdsc->stopping)
		is_delayed = 1;

	spin_lock(&ci->i_ceph_lock);

	if (ci->i_ceph_flags & CEPH_I_FLUSH)
		flags |= CHECK_CAPS_FLUSH;

	/* flush snaps first time around only */
	if (!list_empty(&ci->i_cap_snaps))
		__ceph_flush_snaps(ci, &session, 0);
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
	     " issued %s revoking %s retain %s %s%s%s\n", inode,
	     ceph_cap_string(file_wanted),
	     ceph_cap_string(used), ceph_cap_string(ci->i_dirty_caps),
	     ceph_cap_string(ci->i_flushing_caps),
	     ceph_cap_string(issued), ceph_cap_string(revoking),
	     ceph_cap_string(retain),
	     (flags & CHECK_CAPS_AUTHONLY) ? " AUTHONLY" : "",
	     (flags & CHECK_CAPS_NODELAY) ? " NODELAY" : "",
	     (flags & CHECK_CAPS_FLUSH) ? " FLUSH" : "");

	/*
	 * If we no longer need to hold onto old our caps, and we may
	 * have cached pages, but don't want them, then try to invalidate.
	 * If we fail, it's because pages are locked.... try again later.
	 */
	if ((!is_delayed || mdsc->stopping) &&
	    !S_ISDIR(inode->i_mode) &&		/* ignore readdir cache */
	    ci->i_wrbuffer_ref == 0 &&		/* no dirty pages... */
	    inode->i_data.nrpages &&		/* have cached pages */
	    (file_wanted == 0 ||		/* no open files */
	     (revoking & (CEPH_CAP_FILE_CACHE|
			  CEPH_CAP_FILE_LAZYIO))) && /*  or revoking cache */
	    !tried_invalidate) {
		dout("check_caps trying to invalidate on %p\n", inode);
		if (try_nonblocking_invalidate(inode) < 0) {
			if (revoking & (CEPH_CAP_FILE_CACHE|
					CEPH_CAP_FILE_LAZYIO)) {
				dout("check_caps queuing invalidate\n");
				queue_invalidate = 1;
				ci->i_rdcache_revoking = ci->i_rdcache_gen;
			} else {
				dout("check_caps failed to invalidate pages\n");
				/* we failed to invalidate pages.  check these
				   caps again later. */
				force_requeue = 1;
				__cap_set_timeouts(mdsc, ci);
			}
		}
		tried_invalidate = 1;
		goto retry_locked;
	}

	num = 0;
	for (p = rb_first(&ci->i_caps); p; p = rb_next(p)) {
		cap = rb_entry(p, struct ceph_cap, ci_node);
		num++;

		/* avoid looping forever */
		if (mds >= cap->mds ||
		    ((flags & CHECK_CAPS_AUTHONLY) && cap != ci->i_auth_cap))
			continue;

		/* NOTE: no side-effects allowed, until we take s_mutex */

		cap_used = used;
		if (ci->i_auth_cap && cap != ci->i_auth_cap)
			cap_used &= ~ci->i_auth_cap->issued;

		revoking = cap->implemented & ~cap->issued;
		dout(" mds%d cap %p used %s issued %s implemented %s revoking %s\n",
		     cap->mds, cap, ceph_cap_string(cap->issued),
		     ceph_cap_string(cap_used),
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
			if ((inode->i_size << 1) >= ci->i_max_size &&
			    (ci->i_reported_size << 1) < ci->i_max_size) {
				dout("i_size approaching max_size\n");
				goto ack;
			}
		}
		/* flush anything dirty? */
		if (cap == ci->i_auth_cap && (flags & CHECK_CAPS_FLUSH) &&
		    ci->i_dirty_caps) {
			dout("flushing dirty caps\n");
			goto ack;
		}

		/* completed revocation? going down and there are no caps? */
		if (revoking && (revoking & cap_used) == 0) {
			dout("completed revocation of %s\n",
			     ceph_cap_string(cap->implemented & ~cap->issued));
			goto ack;
		}

		/* want more caps from mds? */
		if (want & ~(cap->mds_wanted | cap->issued))
			goto ack;

		/* things we might delay */
		if ((cap->issued & ~retain) == 0 &&
		    cap->mds_wanted == want)
			continue;     /* nope, all good */

		if (is_delayed)
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
		if (ci->i_ceph_flags & CEPH_I_NOFLUSH) {
			dout(" skipping %p I_NOFLUSH set\n", inode);
			continue;
		}

		if (session && session != cap->session) {
			dout("oops, wrong session %p mutex\n", session);
			mutex_unlock(&session->s_mutex);
			session = NULL;
		}
		if (!session) {
			session = cap->session;
			if (mutex_trylock(&session->s_mutex) == 0) {
				dout("inverting session/ino locks on %p\n",
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
		/* take snap_rwsem after session mutex */
		if (!took_snap_rwsem) {
			if (down_read_trylock(&mdsc->snap_rwsem) == 0) {
				dout("inverting snap/in locks on %p\n",
				     inode);
				spin_unlock(&ci->i_ceph_lock);
				down_read(&mdsc->snap_rwsem);
				took_snap_rwsem = 1;
				goto retry;
			}
			took_snap_rwsem = 1;
		}

		if (cap == ci->i_auth_cap && ci->i_dirty_caps) {
			flushing = __mark_caps_flushing(inode, session,
							&flush_tid,
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
		delayed += __send_cap(mdsc, cap, CEPH_CAP_OP_UPDATE, cap_used,
				      want, retain, flushing,
				      flush_tid, oldest_flush_tid);
		goto retry; /* retake i_ceph_lock and restart our cap scan. */
	}

	/*
	 * Reschedule delayed caps release if we delayed anything,
	 * otherwise cancel.
	 */
	if (delayed && is_delayed)
		force_requeue = 1;   /* __send_cap delayed release; requeue */
	if (!delayed && !is_delayed)
		__cap_delay_cancel(mdsc, ci);
	else if (!is_delayed || force_requeue)
		__cap_delay_requeue(mdsc, ci);

	spin_unlock(&ci->i_ceph_lock);

	if (queue_invalidate)
		ceph_queue_invalidate(inode);

	if (session)
		mutex_unlock(&session->s_mutex);
	if (took_snap_rwsem)
		up_read(&mdsc->snap_rwsem);
}

/*
 * Try to flush dirty caps back to the auth mds.
 */
static int try_flush_caps(struct inode *inode, u64 *ptid)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_mds_session *session = NULL;
	int flushing = 0;
	u64 flush_tid = 0, oldest_flush_tid = 0;

retry:
	spin_lock(&ci->i_ceph_lock);
	if (ci->i_ceph_flags & CEPH_I_NOFLUSH) {
		dout("try_flush_caps skipping %p I_NOFLUSH set\n", inode);
		goto out;
	}
	if (ci->i_dirty_caps && ci->i_auth_cap) {
		struct ceph_cap *cap = ci->i_auth_cap;
		int used = __ceph_caps_used(ci);
		int want = __ceph_caps_wanted(ci);
		int delayed;

		if (!session || session != cap->session) {
			spin_unlock(&ci->i_ceph_lock);
			if (session)
				mutex_unlock(&session->s_mutex);
			session = cap->session;
			mutex_lock(&session->s_mutex);
			goto retry;
		}
		if (cap->session->s_state < CEPH_MDS_SESSION_OPEN)
			goto out;

		flushing = __mark_caps_flushing(inode, session, &flush_tid,
						&oldest_flush_tid);

		/* __send_cap drops i_ceph_lock */
		delayed = __send_cap(mdsc, cap, CEPH_CAP_OP_FLUSH, used, want,
				     (cap->issued | cap->implemented),
				     flushing, flush_tid, oldest_flush_tid);

		if (delayed) {
			spin_lock(&ci->i_ceph_lock);
			__cap_delay_requeue(mdsc, ci);
			spin_unlock(&ci->i_ceph_lock);
		}
	} else {
		struct rb_node *n = rb_last(&ci->i_cap_flush_tree);
		if (n) {
			struct ceph_cap_flush *cf =
				rb_entry(n, struct ceph_cap_flush, i_node);
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
static int caps_are_flushed(struct inode *inode, u64 flush_tid)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_cap_flush *cf;
	struct rb_node *n;
	int ret = 1;

	spin_lock(&ci->i_ceph_lock);
	n = rb_first(&ci->i_cap_flush_tree);
	if (n) {
		cf = rb_entry(n, struct ceph_cap_flush, i_node);
		if (cf->tid <= flush_tid)
			ret = 0;
	}
	spin_unlock(&ci->i_ceph_lock);
	return ret;
}

/*
 * Wait on any unsafe replies for the given inode.  First wait on the
 * newest request, and make that the upper bound.  Then, if there are
 * more requests, keep waiting on the oldest as long as it is still older
 * than the original request.
 */
static void sync_write_wait(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct list_head *head = &ci->i_unsafe_writes;
	struct ceph_osd_request *req;
	u64 last_tid;

	if (!S_ISREG(inode->i_mode))
		return;

	spin_lock(&ci->i_unsafe_lock);
	if (list_empty(head))
		goto out;

	/* set upper bound as _last_ entry in chain */
	req = list_last_entry(head, struct ceph_osd_request,
			      r_unsafe_item);
	last_tid = req->r_tid;

	do {
		ceph_osdc_get_request(req);
		spin_unlock(&ci->i_unsafe_lock);
		dout("sync_write_wait on tid %llu (until %llu)\n",
		     req->r_tid, last_tid);
		wait_for_completion(&req->r_safe_completion);
		spin_lock(&ci->i_unsafe_lock);
		ceph_osdc_put_request(req);

		/*
		 * from here on look at first entry in chain, since we
		 * only want to wait for anything older than last_tid
		 */
		if (list_empty(head))
			break;
		req = list_first_entry(head, struct ceph_osd_request,
				       r_unsafe_item);
	} while (req->r_tid < last_tid);
out:
	spin_unlock(&ci->i_unsafe_lock);
}

/*
 * wait for any uncommitted directory operations to commit.
 */
static int unsafe_dirop_wait(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct list_head *head = &ci->i_unsafe_dirops;
	struct ceph_mds_request *req;
	u64 last_tid;
	int ret = 0;

	if (!S_ISDIR(inode->i_mode))
		return 0;

	spin_lock(&ci->i_unsafe_lock);
	if (list_empty(head))
		goto out;

	req = list_last_entry(head, struct ceph_mds_request,
			      r_unsafe_dir_item);
	last_tid = req->r_tid;

	do {
		ceph_mdsc_get_request(req);
		spin_unlock(&ci->i_unsafe_lock);

		dout("unsafe_dirop_wait %p wait on tid %llu (until %llu)\n",
		     inode, req->r_tid, last_tid);
		ret = !wait_for_completion_timeout(&req->r_safe_completion,
					ceph_timeout_jiffies(req->r_timeout));
		if (ret)
			ret = -EIO;  /* timed out */

		ceph_mdsc_put_request(req);

		spin_lock(&ci->i_unsafe_lock);
		if (ret || list_empty(head))
			break;
		req = list_first_entry(head, struct ceph_mds_request,
				       r_unsafe_dir_item);
	} while (req->r_tid < last_tid);
out:
	spin_unlock(&ci->i_unsafe_lock);
	return ret;
}

int ceph_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct inode *inode = file->f_mapping->host;
	struct ceph_inode_info *ci = ceph_inode(inode);
	u64 flush_tid;
	int ret;
	int dirty;

	dout("fsync %p%s\n", inode, datasync ? " datasync" : "");
	sync_write_wait(inode);

	ret = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (ret < 0)
		goto out;

	if (datasync)
		goto out;

	mutex_lock(&inode->i_mutex);

	dirty = try_flush_caps(inode, &flush_tid);
	dout("fsync dirty caps are %s\n", ceph_cap_string(dirty));

	ret = unsafe_dirop_wait(inode);

	/*
	 * only wait on non-file metadata writeback (the mds
	 * can recover size and mtime, so we don't need to
	 * wait for that)
	 */
	if (!ret && (dirty & ~CEPH_CAP_ANY_FILE_WR)) {
		ret = wait_event_interruptible(ci->i_cap_wq,
					caps_are_flushed(inode, flush_tid));
	}
	mutex_unlock(&inode->i_mutex);
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
	int wait = wbc->sync_mode == WB_SYNC_ALL;

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

/*
 * After a recovering MDS goes active, we need to resend any caps
 * we were flushing.
 *
 * Caller holds session->s_mutex.
 */
static void kick_flushing_capsnaps(struct ceph_mds_client *mdsc,
				   struct ceph_mds_session *session)
{
	struct ceph_cap_snap *capsnap;

	dout("kick_flushing_capsnaps mds%d\n", session->s_mds);
	list_for_each_entry(capsnap, &session->s_cap_snaps_flushing,
			    flushing_item) {
		struct ceph_inode_info *ci = capsnap->ci;
		struct inode *inode = &ci->vfs_inode;
		struct ceph_cap *cap;

		spin_lock(&ci->i_ceph_lock);
		cap = ci->i_auth_cap;
		if (cap && cap->session == session) {
			dout("kick_flushing_caps %p cap %p capsnap %p\n", inode,
			     cap, capsnap);
			__ceph_flush_snaps(ci, &session, 1);
		} else {
			pr_err("%p auth cap %p not mds%d ???\n", inode,
			       cap, session->s_mds);
		}
		spin_unlock(&ci->i_ceph_lock);
	}
}

static int __kick_flushing_caps(struct ceph_mds_client *mdsc,
				struct ceph_mds_session *session,
				struct ceph_inode_info *ci)
{
	struct inode *inode = &ci->vfs_inode;
	struct ceph_cap *cap;
	struct ceph_cap_flush *cf;
	struct rb_node *n;
	int delayed = 0;
	u64 first_tid = 0;
	u64 oldest_flush_tid;

	spin_lock(&mdsc->cap_dirty_lock);
	oldest_flush_tid = __get_oldest_flush_tid(mdsc);
	spin_unlock(&mdsc->cap_dirty_lock);

	while (true) {
		spin_lock(&ci->i_ceph_lock);
		cap = ci->i_auth_cap;
		if (!(cap && cap->session == session)) {
			pr_err("%p auth cap %p not mds%d ???\n", inode,
					cap, session->s_mds);
			spin_unlock(&ci->i_ceph_lock);
			break;
		}

		for (n = rb_first(&ci->i_cap_flush_tree); n; n = rb_next(n)) {
			cf = rb_entry(n, struct ceph_cap_flush, i_node);
			if (cf->tid >= first_tid)
				break;
		}
		if (!n) {
			spin_unlock(&ci->i_ceph_lock);
			break;
		}

		cf = rb_entry(n, struct ceph_cap_flush, i_node);

		first_tid = cf->tid + 1;

		dout("kick_flushing_caps %p cap %p tid %llu %s\n", inode,
		     cap, cf->tid, ceph_cap_string(cf->caps));
		delayed |= __send_cap(mdsc, cap, CEPH_CAP_OP_FLUSH,
				      __ceph_caps_used(ci),
				      __ceph_caps_wanted(ci),
				      cap->issued | cap->implemented,
				      cf->caps, cf->tid, oldest_flush_tid);
	}
	return delayed;
}

void ceph_early_kick_flushing_caps(struct ceph_mds_client *mdsc,
				   struct ceph_mds_session *session)
{
	struct ceph_inode_info *ci;
	struct ceph_cap *cap;

	dout("early_kick_flushing_caps mds%d\n", session->s_mds);
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
			spin_unlock(&ci->i_ceph_lock);
			if (!__kick_flushing_caps(mdsc, session, ci))
				continue;
			spin_lock(&ci->i_ceph_lock);
		}

		spin_unlock(&ci->i_ceph_lock);
	}
}

void ceph_kick_flushing_caps(struct ceph_mds_client *mdsc,
			     struct ceph_mds_session *session)
{
	struct ceph_inode_info *ci;

	kick_flushing_capsnaps(mdsc, session);

	dout("kick_flushing_caps mds%d\n", session->s_mds);
	list_for_each_entry(ci, &session->s_cap_flushing, i_flushing_item) {
		int delayed = __kick_flushing_caps(mdsc, session, ci);
		if (delayed) {
			spin_lock(&ci->i_ceph_lock);
			__cap_delay_requeue(mdsc, ci);
			spin_unlock(&ci->i_ceph_lock);
		}
	}
}

static void kick_flushing_inode_caps(struct ceph_mds_client *mdsc,
				     struct ceph_mds_session *session,
				     struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_cap *cap;

	spin_lock(&ci->i_ceph_lock);
	cap = ci->i_auth_cap;
	dout("kick_flushing_inode_caps %p flushing %s\n", inode,
	     ceph_cap_string(ci->i_flushing_caps));

	__ceph_flush_snaps(ci, &session, 1);

	if (ci->i_flushing_caps) {
		int delayed;

		spin_lock(&mdsc->cap_dirty_lock);
		list_move_tail(&ci->i_flushing_item,
			       &cap->session->s_cap_flushing);
		spin_unlock(&mdsc->cap_dirty_lock);

		spin_unlock(&ci->i_ceph_lock);

		delayed = __kick_flushing_caps(mdsc, session, ci);
		if (delayed) {
			spin_lock(&ci->i_ceph_lock);
			__cap_delay_requeue(mdsc, ci);
			spin_unlock(&ci->i_ceph_lock);
		}
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
static void __take_cap_refs(struct ceph_inode_info *ci, int got,
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
			ihold(&ci->vfs_inode);
		ci->i_wb_ref++;
		dout("__take_cap_refs %p wb %d -> %d (?)\n",
		     &ci->vfs_inode, ci->i_wb_ref-1, ci->i_wb_ref);
	}
}

/*
 * Try to grab cap references.  Specify those refs we @want, and the
 * minimal set we @need.  Also include the larger offset we are writing
 * to (when applicable), and check against max_size here as well.
 * Note that caller is responsible for ensuring max_size increases are
 * requested from the MDS.
 */
static int try_get_cap_refs(struct ceph_inode_info *ci, int need, int want,
			    loff_t endoff, bool nonblock, int *got, int *err)
{
	struct inode *inode = &ci->vfs_inode;
	struct ceph_mds_client *mdsc = ceph_inode_to_client(inode)->mdsc;
	int ret = 0;
	int have, implemented;
	int file_wanted;
	bool snap_rwsem_locked = false;

	dout("get_cap_refs %p need %s want %s\n", inode,
	     ceph_cap_string(need), ceph_cap_string(want));

again:
	spin_lock(&ci->i_ceph_lock);

	/* make sure file is actually open */
	file_wanted = __ceph_caps_file_wanted(ci);
	if ((file_wanted & need) == 0) {
		dout("try_get_cap_refs need %s file_wanted %s, EBADF\n",
		     ceph_cap_string(need), ceph_cap_string(file_wanted));
		*err = -EBADF;
		ret = 1;
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
			if (endoff > ci->i_requested_max_size) {
				*err = -EAGAIN;
				ret = 1;
			}
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
					if (nonblock) {
						*err = -EAGAIN;
						ret = 1;
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
			     inode, ceph_cap_string(need), ci->i_auth_cap->mds);
			*err = -EROFS;
			ret = 1;
			goto out_unlock;
		}

		if (!__ceph_is_any_caps(ci) &&
		    ACCESS_ONCE(mdsc->fsc->mount_state) == CEPH_MOUNT_SHUTDOWN) {
			dout("get_cap_refs %p forced umount\n", inode);
			*err = -EIO;
			ret = 1;
			goto out_unlock;
		}

		dout("get_cap_refs %p have %s needed %s\n", inode,
		     ceph_cap_string(have), ceph_cap_string(need));
	}
out_unlock:
	spin_unlock(&ci->i_ceph_lock);
	if (snap_rwsem_locked)
		up_read(&mdsc->snap_rwsem);

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

/*
 * Wait for caps, and take cap references.  If we can't get a WR cap
 * due to a small max_size, make sure we check_max_size (and possibly
 * ask the mds) so we don't get hung up indefinitely.
 */
int ceph_get_caps(struct ceph_inode_info *ci, int need, int want,
		  loff_t endoff, int *got, struct page **pinned_page)
{
	int _got, ret, err = 0;

	ret = ceph_pool_perm_check(ci, need);
	if (ret < 0)
		return ret;

	while (true) {
		if (endoff > 0)
			check_max_size(&ci->vfs_inode, endoff);

		err = 0;
		_got = 0;
		ret = try_get_cap_refs(ci, need, want, endoff,
				       false, &_got, &err);
		if (ret) {
			if (err == -EAGAIN)
				continue;
			if (err < 0)
				return err;
		} else {
			ret = wait_event_interruptible(ci->i_cap_wq,
					try_get_cap_refs(ci, need, want, endoff,
							 true, &_got, &err));
			if (err == -EAGAIN)
				continue;
			if (err < 0)
				ret = err;
			if (ret < 0)
				return ret;
		}

		if (ci->i_inline_version != CEPH_INLINE_NONE &&
		    (_got & (CEPH_CAP_FILE_CACHE|CEPH_CAP_FILE_LAZYIO)) &&
		    i_size_read(&ci->vfs_inode) > 0) {
			struct page *page =
				find_get_page(ci->vfs_inode.i_mapping, 0);
			if (page) {
				if (PageUptodate(page)) {
					*pinned_page = page;
					break;
				}
				page_cache_release(page);
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
			ret = __ceph_do_getattr(&ci->vfs_inode, NULL,
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
	__take_cap_refs(ci, caps, false);
	spin_unlock(&ci->i_ceph_lock);
}


/*
 * drop cap_snap that is not associated with any snapshot.
 * we don't need to send FLUSHSNAP message for it.
 */
static int ceph_try_drop_cap_snap(struct ceph_cap_snap *capsnap)
{
	if (!capsnap->need_flush &&
	    !capsnap->writing && !capsnap->dirty_pages) {

		dout("dropping cap_snap %p follows %llu\n",
		     capsnap, capsnap->follows);
		ceph_put_snap_context(capsnap->context);
		list_del(&capsnap->ci_item);
		list_del(&capsnap->flushing_item);
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
void ceph_put_cap_refs(struct ceph_inode_info *ci, int had)
{
	struct inode *inode = &ci->vfs_inode;
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
		     inode, ci->i_wb_ref+1, ci->i_wb_ref);
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
				if (ceph_try_drop_cap_snap(capsnap))
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
			if (!__ceph_is_any_caps(ci) && ci->i_snap_realm)
				drop_inode_snap_realm(ci);
		}
	spin_unlock(&ci->i_ceph_lock);

	dout("put_cap_refs %p had %s%s%s\n", inode, ceph_cap_string(had),
	     last ? " last" : "", put ? " put" : "");

	if (last && !flushsnaps)
		ceph_check_caps(ci, 0, NULL);
	else if (flushsnaps)
		ceph_flush_snaps(ci);
	if (wake)
		wake_up_all(&ci->i_cap_wq);
	while (put-- > 0)
		iput(inode);
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
	int last = 0;
	int complete_capsnap = 0;
	int drop_capsnap = 0;
	int found = 0;
	struct ceph_cap_snap *capsnap = NULL;

	spin_lock(&ci->i_ceph_lock);
	ci->i_wrbuffer_ref -= nr;
	last = !ci->i_wrbuffer_ref;

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
				found = 1;
				break;
			}
		}
		BUG_ON(!found);
		capsnap->dirty_pages -= nr;
		if (capsnap->dirty_pages == 0) {
			complete_capsnap = 1;
			drop_capsnap = ceph_try_drop_cap_snap(capsnap);
		}
		dout("put_wrbuffer_cap_refs on %p cap_snap %p "
		     " snap %lld %d/%d -> %d/%d %s%s\n",
		     inode, capsnap, capsnap->context->seq,
		     ci->i_wrbuffer_ref+nr, capsnap->dirty_pages + nr,
		     ci->i_wrbuffer_ref, capsnap->dirty_pages,
		     last ? " (wrbuffer last)" : "",
		     complete_capsnap ? " (complete capsnap)" : "");
	}

	spin_unlock(&ci->i_ceph_lock);

	if (last) {
		ceph_check_caps(ci, CHECK_CAPS_AUTHONLY, NULL);
		iput(inode);
	} else if (complete_capsnap) {
		ceph_flush_snaps(ci);
		wake_up_all(&ci->i_cap_wq);
	}
	if (drop_capsnap)
		iput(inode);
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

/*
 * Handle a cap GRANT message from the MDS.  (Note that a GRANT may
 * actually be a revocation if it specifies a smaller cap set.)
 *
 * caller holds s_mutex and i_ceph_lock, we drop both.
 */
static void handle_cap_grant(struct ceph_mds_client *mdsc,
			     struct inode *inode, struct ceph_mds_caps *grant,
			     u64 inline_version,
			     void *inline_data, int inline_len,
			     struct ceph_buffer *xattr_buf,
			     struct ceph_mds_session *session,
			     struct ceph_cap *cap, int issued)
	__releases(ci->i_ceph_lock)
	__releases(mdsc->snap_rwsem)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int mds = session->s_mds;
	int seq = le32_to_cpu(grant->seq);
	int newcaps = le32_to_cpu(grant->caps);
	int used, wanted, dirty;
	u64 size = le64_to_cpu(grant->size);
	u64 max_size = le64_to_cpu(grant->max_size);
	struct timespec mtime, atime, ctime;
	int check_caps = 0;
	bool wake = false;
	bool writeback = false;
	bool queue_trunc = false;
	bool queue_invalidate = false;
	bool queue_revalidate = false;
	bool deleted_inode = false;
	bool fill_inline = false;

	dout("handle_cap_grant inode %p cap %p mds%d seq %d %s\n",
	     inode, cap, mds, seq, ceph_cap_string(newcaps));
	dout(" size %llu max_size %llu, i_size %llu\n", size, max_size,
		inode->i_size);


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

	/*
	 * If CACHE is being revoked, and we have no dirty buffers,
	 * try to invalidate (once).  (If there are dirty buffers, we
	 * will invalidate _after_ writeback.)
	 */
	if (!S_ISDIR(inode->i_mode) && /* don't invalidate readdir cache */
	    ((cap->issued & ~newcaps) & CEPH_CAP_FILE_CACHE) &&
	    (newcaps & CEPH_CAP_FILE_LAZYIO) == 0 &&
	    !ci->i_wrbuffer_ref) {
		if (try_nonblocking_invalidate(inode)) {
			/* there were locked pages.. invalidate later
			   in a separate thread. */
			if (ci->i_rdcache_revoking != ci->i_rdcache_gen) {
				queue_invalidate = true;
				ci->i_rdcache_revoking = ci->i_rdcache_gen;
			}
		}

		ceph_fscache_invalidate(inode);
	}

	/* side effects now are allowed */
	cap->cap_gen = session->s_cap_gen;
	cap->seq = seq;

	__check_cap_issue(ci, cap, newcaps);

	if ((newcaps & CEPH_CAP_AUTH_SHARED) &&
	    (issued & CEPH_CAP_AUTH_EXCL) == 0) {
		inode->i_mode = le32_to_cpu(grant->mode);
		inode->i_uid = make_kuid(&init_user_ns, le32_to_cpu(grant->uid));
		inode->i_gid = make_kgid(&init_user_ns, le32_to_cpu(grant->gid));
		dout("%p mode 0%o uid.gid %d.%d\n", inode, inode->i_mode,
		     from_kuid(&init_user_ns, inode->i_uid),
		     from_kgid(&init_user_ns, inode->i_gid));
	}

	if ((newcaps & CEPH_CAP_AUTH_SHARED) &&
	    (issued & CEPH_CAP_LINK_EXCL) == 0) {
		set_nlink(inode, le32_to_cpu(grant->nlink));
		if (inode->i_nlink == 0 &&
		    (newcaps & (CEPH_CAP_LINK_SHARED | CEPH_CAP_LINK_EXCL)))
			deleted_inode = true;
	}

	if ((issued & CEPH_CAP_XATTR_EXCL) == 0 && grant->xattr_len) {
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
		}
	}

	/* Do we need to revalidate our fscache cookie. Don't bother on the
	 * first cache cap as we already validate at cookie creation time. */
	if ((issued & CEPH_CAP_FILE_CACHE) && ci->i_rdcache_gen > 1)
		queue_revalidate = true;

	if (newcaps & CEPH_CAP_ANY_RD) {
		/* ctime/mtime/atime? */
		ceph_decode_timespec(&mtime, &grant->mtime);
		ceph_decode_timespec(&atime, &grant->atime);
		ceph_decode_timespec(&ctime, &grant->ctime);
		ceph_fill_file_time(inode, issued,
				    le32_to_cpu(grant->time_warp_seq),
				    &ctime, &mtime, &atime);
	}

	if (newcaps & (CEPH_CAP_ANY_FILE_RD | CEPH_CAP_ANY_FILE_WR)) {
		/* file layout may have changed */
		ci->i_layout = grant->layout;
		/* size/truncate_seq? */
		queue_trunc = ceph_fill_file_size(inode, issued,
					le32_to_cpu(grant->truncate_seq),
					le64_to_cpu(grant->truncate_size),
					size);
		/* max size increase? */
		if (ci->i_auth_cap == cap && max_size != ci->i_max_size) {
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
	if (wanted != le32_to_cpu(grant->wanted)) {
		dout("mds wanted %s -> %s\n",
		     ceph_cap_string(le32_to_cpu(grant->wanted)),
		     ceph_cap_string(wanted));
		/* imported cap may not have correct mds_wanted */
		if (le32_to_cpu(grant->op) == CEPH_CAP_OP_IMPORT)
			check_caps = 1;
	}

	/* revocation, grant, or no-op? */
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

	if (inline_version > 0 && inline_version >= ci->i_inline_version) {
		ci->i_inline_version = inline_version;
		if (ci->i_inline_version != CEPH_INLINE_NONE &&
		    (newcaps & (CEPH_CAP_FILE_CACHE|CEPH_CAP_FILE_LAZYIO)))
			fill_inline = true;
	}

	spin_unlock(&ci->i_ceph_lock);

	if (le32_to_cpu(grant->op) == CEPH_CAP_OP_IMPORT) {
		kick_flushing_inode_caps(mdsc, session, inode);
		up_read(&mdsc->snap_rwsem);
		if (newcaps & ~issued)
			wake = true;
	}

	if (fill_inline)
		ceph_fill_inline_data(inode, NULL, inline_data, inline_len);

	if (queue_trunc) {
		ceph_queue_vmtruncate(inode);
		ceph_queue_revalidate(inode);
	} else if (queue_revalidate)
		ceph_queue_revalidate(inode);

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
static void handle_cap_flush_ack(struct inode *inode, u64 flush_tid,
				 struct ceph_mds_caps *m,
				 struct ceph_mds_session *session,
				 struct ceph_cap *cap)
	__releases(ci->i_ceph_lock)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;
	struct ceph_cap_flush *cf;
	struct rb_node *n;
	LIST_HEAD(to_remove);
	unsigned seq = le32_to_cpu(m->seq);
	int dirty = le32_to_cpu(m->dirty);
	int cleaned = 0;
	int drop = 0;

	n = rb_first(&ci->i_cap_flush_tree);
	while (n) {
		cf = rb_entry(n, struct ceph_cap_flush, i_node);
		n = rb_next(&cf->i_node);
		if (cf->tid == flush_tid)
			cleaned = cf->caps;
		if (cf->tid <= flush_tid) {
			rb_erase(&cf->i_node, &ci->i_cap_flush_tree);
			list_add_tail(&cf->list, &to_remove);
		} else {
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

	if (!list_empty(&to_remove)) {
		list_for_each_entry(cf, &to_remove, list)
			rb_erase(&cf->g_node, &mdsc->cap_flush_tree);

		n = rb_first(&mdsc->cap_flush_tree);
		cf = n ? rb_entry(n, struct ceph_cap_flush, g_node) : NULL;
		if (!cf || cf->tid > flush_tid)
			wake_up_all(&mdsc->cap_flushing_wq);
	}

	if (ci->i_flushing_caps == 0) {
		list_del_init(&ci->i_flushing_item);
		if (!list_empty(&session->s_cap_flushing))
			dout(" mds%d still flushing cap on %p\n",
			     session->s_mds,
			     &list_entry(session->s_cap_flushing.next,
					 struct ceph_inode_info,
					 i_flushing_item)->vfs_inode);
		mdsc->num_cap_flushing--;
		dout(" inode %p now !flushing\n", inode);

		if (ci->i_dirty_caps == 0) {
			dout(" inode %p now clean\n", inode);
			BUG_ON(!list_empty(&ci->i_dirty_item));
			drop = 1;
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
	wake_up_all(&ci->i_cap_wq);

out:
	spin_unlock(&ci->i_ceph_lock);

	while (!list_empty(&to_remove)) {
		cf = list_first_entry(&to_remove,
				      struct ceph_cap_flush, list);
		list_del(&cf->list);
		ceph_free_cap_flush(cf);
	}
	if (drop)
		iput(inode);
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
	int drop = 0;

	dout("handle_cap_flushsnap_ack inode %p ci %p mds%d follows %lld\n",
	     inode, ci, session->s_mds, follows);

	spin_lock(&ci->i_ceph_lock);
	list_for_each_entry(capsnap, &ci->i_cap_snaps, ci_item) {
		if (capsnap->follows == follows) {
			if (capsnap->flush_tid != flush_tid) {
				dout(" cap_snap %p follows %lld tid %lld !="
				     " %lld\n", capsnap, follows,
				     flush_tid, capsnap->flush_tid);
				break;
			}
			WARN_ON(capsnap->dirty_pages || capsnap->writing);
			dout(" removing %p cap_snap %p follows %lld\n",
			     inode, capsnap, follows);
			ceph_put_snap_context(capsnap->context);
			list_del(&capsnap->ci_item);
			list_del(&capsnap->flushing_item);
			ceph_put_cap_snap(capsnap);
			wake_up_all(&mdsc->cap_flushing_wq);
			drop = 1;
			break;
		} else {
			dout(" skipping cap_snap %p follows %lld\n",
			     capsnap, capsnap->follows);
		}
	}
	spin_unlock(&ci->i_ceph_lock);
	if (drop)
		iput(inode);
}

/*
 * Handle TRUNC from MDS, indicating file truncation.
 *
 * caller hold s_mutex.
 */
static void handle_cap_trunc(struct inode *inode,
			     struct ceph_mds_caps *trunc,
			     struct ceph_mds_session *session)
	__releases(ci->i_ceph_lock)
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
	int queue_trunc = 0;

	issued |= implemented | dirty;

	dout("handle_cap_trunc inode %p mds%d seq %d to %lld seq %d\n",
	     inode, mds, seq, truncate_size, truncate_seq);
	queue_trunc = ceph_fill_file_size(inode, issued,
					  truncate_seq, truncate_size, size);
	spin_unlock(&ci->i_ceph_lock);

	if (queue_trunc) {
		ceph_queue_vmtruncate(inode);
		ceph_fscache_invalidate(inode);
	}
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
		__ceph_remove_cap(cap, false);
		goto out_unlock;
	}

	/*
	 * now we know we haven't received the cap import message yet
	 * because the exported cap still exist.
	 */

	issued = cap->issued;
	WARN_ON(issued != cap->implemented);

	tcap = __get_cap_for_mds(ci, target);
	if (tcap) {
		/* already have caps from the target */
		if (tcap->cap_id != t_cap_id ||
		    ceph_seq_cmp(tcap->seq, t_seq) < 0) {
			dout(" updating import cap %p mds%d\n", tcap, target);
			tcap->cap_id = t_cap_id;
			tcap->seq = t_seq - 1;
			tcap->issue_seq = t_seq - 1;
			tcap->mseq = t_mseq;
			tcap->issued |= issued;
			tcap->implemented |= issued;
			if (cap == ci->i_auth_cap)
				ci->i_auth_cap = tcap;
			if (ci->i_flushing_caps && ci->i_auth_cap == tcap) {
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
		ceph_add_cap(inode, tsession, t_cap_id, -1, issued, 0,
			     t_seq - 1, t_mseq, (u64)-1, flag, &new_cap);

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
			      struct inode *inode, struct ceph_mds_caps *im,
			      struct ceph_mds_cap_peer *ph,
			      struct ceph_mds_session *session,
			      struct ceph_cap **target_cap, int *old_issued)
	__acquires(ci->i_ceph_lock)
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

	ceph_add_cap(inode, session, cap_id, -1, caps, wanted, seq, mseq,
		     realmino, CEPH_CAP_FLAG_AUTH, &new_cap);

	ocap = peer >= 0 ? __get_cap_for_mds(ci, peer) : NULL;
	if (ocap && ocap->cap_id == p_cap_id) {
		dout(" remove export cap %p mds%d flags %d\n",
		     ocap, peer, ph->flags);
		if ((ph->flags & CEPH_CAP_FLAG_AUTH) &&
		    (ocap->seq != le32_to_cpu(ph->seq) ||
		     ocap->mseq != le32_to_cpu(ph->mseq))) {
			pr_err("handle_cap_import: mismatched seq/mseq: "
			       "ino (%llx.%llx) mds%d seq %d mseq %d "
			       "importer mds%d has peer seq %d mseq %d\n",
			       ceph_vinop(inode), peer, ocap->seq,
			       ocap->mseq, mds, le32_to_cpu(ph->seq),
			       le32_to_cpu(ph->mseq));
		}
		__ceph_remove_cap(ocap, (ph->flags & CEPH_CAP_FLAG_RELEASE));
	}

	/* make sure we re-request max_size, if necessary */
	ci->i_wanted_max_size = 0;
	ci->i_requested_max_size = 0;

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
	struct super_block *sb = mdsc->fsc->sb;
	struct inode *inode;
	struct ceph_inode_info *ci;
	struct ceph_cap *cap;
	struct ceph_mds_caps *h;
	struct ceph_mds_cap_peer *peer = NULL;
	struct ceph_snap_realm *realm;
	int mds = session->s_mds;
	int op, issued;
	u32 seq, mseq;
	struct ceph_vino vino;
	u64 cap_id;
	u64 size, max_size;
	u64 tid;
	u64 inline_version = 0;
	void *inline_data = NULL;
	u32  inline_len = 0;
	void *snaptrace;
	size_t snaptrace_len;
	void *p, *end;

	dout("handle_caps from mds%d\n", mds);

	/* decode */
	end = msg->front.iov_base + msg->front.iov_len;
	tid = le64_to_cpu(msg->hdr.tid);
	if (msg->front.iov_len < sizeof(*h))
		goto bad;
	h = msg->front.iov_base;
	op = le32_to_cpu(h->op);
	vino.ino = le64_to_cpu(h->ino);
	vino.snap = CEPH_NOSNAP;
	cap_id = le64_to_cpu(h->cap_id);
	seq = le32_to_cpu(h->seq);
	mseq = le32_to_cpu(h->migrate_seq);
	size = le64_to_cpu(h->size);
	max_size = le64_to_cpu(h->max_size);

	snaptrace = h + 1;
	snaptrace_len = le32_to_cpu(h->snap_trace_len);
	p = snaptrace + snaptrace_len;

	if (le16_to_cpu(msg->hdr.version) >= 2) {
		u32 flock_len;
		ceph_decode_32_safe(&p, end, flock_len, bad);
		if (p + flock_len > end)
			goto bad;
		p += flock_len;
	}

	if (le16_to_cpu(msg->hdr.version) >= 3) {
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

	if (le16_to_cpu(msg->hdr.version) >= 4) {
		ceph_decode_64_safe(&p, end, inline_version, bad);
		ceph_decode_32_safe(&p, end, inline_len, bad);
		if (p + inline_len > end)
			goto bad;
		inline_data = p;
		p += inline_len;
	}

	/* lookup ino */
	inode = ceph_find_inode(sb, vino);
	ci = ceph_inode(inode);
	dout(" op %s ino %llx.%llx inode %p\n", ceph_cap_op_name(op), vino.ino,
	     vino.snap, inode);

	mutex_lock(&session->s_mutex);
	session->s_seq++;
	dout(" mds%d seq %lld cap seq %u\n", session->s_mds, session->s_seq,
	     (unsigned)seq);

	if (!inode) {
		dout(" i don't have ino %llx\n", vino.ino);

		if (op == CEPH_CAP_OP_IMPORT) {
			cap = ceph_get_cap(mdsc, NULL);
			cap->cap_ino = vino.ino;
			cap->queue_release = 1;
			cap->cap_id = cap_id;
			cap->mseq = mseq;
			cap->seq = seq;
			spin_lock(&session->s_cap_lock);
			list_add_tail(&cap->session_caps,
					&session->s_cap_releases);
			session->s_num_cap_releases++;
			spin_unlock(&session->s_cap_lock);
		}
		goto flush_cap_releases;
	}

	/* these will work even if we don't have a cap yet */
	switch (op) {
	case CEPH_CAP_OP_FLUSHSNAP_ACK:
		handle_cap_flushsnap_ack(inode, tid, h, session);
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
		handle_cap_import(mdsc, inode, h, peer, session,
				  &cap, &issued);
		handle_cap_grant(mdsc, inode, h,
				 inline_version, inline_data, inline_len,
				 msg->middle, session, cap, issued);
		if (realm)
			ceph_put_snap_realm(mdsc, realm);
		goto done_unlocked;
	}

	/* the rest require a cap */
	spin_lock(&ci->i_ceph_lock);
	cap = __get_cap_for_mds(ceph_inode(inode), mds);
	if (!cap) {
		dout(" no cap on %p ino %llx.%llx from mds%d\n",
		     inode, ceph_ino(inode), ceph_snap(inode), mds);
		spin_unlock(&ci->i_ceph_lock);
		goto flush_cap_releases;
	}

	/* note that each of these drops i_ceph_lock for us */
	switch (op) {
	case CEPH_CAP_OP_REVOKE:
	case CEPH_CAP_OP_GRANT:
		__ceph_caps_issued(ci, &issued);
		issued |= __ceph_caps_dirty(ci);
		handle_cap_grant(mdsc, inode, h,
				 inline_version, inline_data, inline_len,
				 msg->middle, session, cap, issued);
		goto done_unlocked;

	case CEPH_CAP_OP_FLUSH_ACK:
		handle_cap_flush_ack(inode, tid, h, session, cap);
		break;

	case CEPH_CAP_OP_TRUNC:
		handle_cap_trunc(inode, h, session);
		break;

	default:
		spin_unlock(&ci->i_ceph_lock);
		pr_err("ceph_handle_caps: unknown cap op %d %s\n", op,
		       ceph_cap_op_name(op));
	}

	goto done;

flush_cap_releases:
	/*
	 * send any cap release message to try to move things
	 * along for the mds (who clearly thinks we still have this
	 * cap).
	 */
	ceph_send_cap_releases(mdsc, session);

done:
	mutex_unlock(&session->s_mutex);
done_unlocked:
	iput(inode);
	return;

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
	struct ceph_inode_info *ci;
	int flags = CHECK_CAPS_NODELAY;

	dout("check_delayed_caps\n");
	while (1) {
		spin_lock(&mdsc->cap_delay_lock);
		if (list_empty(&mdsc->cap_delay_list))
			break;
		ci = list_first_entry(&mdsc->cap_delay_list,
				      struct ceph_inode_info,
				      i_cap_delay_list);
		if ((ci->i_ceph_flags & CEPH_I_FLUSH) == 0 &&
		    time_before(jiffies, ci->i_hold_caps_max))
			break;
		list_del_init(&ci->i_cap_delay_list);
		spin_unlock(&mdsc->cap_delay_lock);
		dout("check_delayed_caps on %p\n", &ci->vfs_inode);
		ceph_check_caps(ci, flags, NULL);
	}
	spin_unlock(&mdsc->cap_delay_lock);
}

/*
 * Flush all dirty caps to the mds
 */
void ceph_flush_dirty_caps(struct ceph_mds_client *mdsc)
{
	struct ceph_inode_info *ci;
	struct inode *inode;

	dout("flush_dirty_caps\n");
	spin_lock(&mdsc->cap_dirty_lock);
	while (!list_empty(&mdsc->cap_dirty)) {
		ci = list_first_entry(&mdsc->cap_dirty, struct ceph_inode_info,
				      i_dirty_item);
		inode = &ci->vfs_inode;
		ihold(inode);
		dout("flush_dirty_caps %p\n", inode);
		spin_unlock(&mdsc->cap_dirty_lock);
		ceph_check_caps(ci, CHECK_CAPS_NODELAY|CHECK_CAPS_FLUSH, NULL);
		iput(inode);
		spin_lock(&mdsc->cap_dirty_lock);
	}
	spin_unlock(&mdsc->cap_dirty_lock);
	dout("flush_dirty_caps done\n");
}

/*
 * Drop open file reference.  If we were the last open file,
 * we may need to release capabilities to the MDS (or schedule
 * their delayed release).
 */
void ceph_put_fmode(struct ceph_inode_info *ci, int fmode)
{
	struct inode *inode = &ci->vfs_inode;
	int last = 0;

	spin_lock(&ci->i_ceph_lock);
	dout("put_fmode %p fmode %d %d -> %d\n", inode, fmode,
	     ci->i_nr_by_mode[fmode], ci->i_nr_by_mode[fmode]-1);
	BUG_ON(ci->i_nr_by_mode[fmode] == 0);
	if (--ci->i_nr_by_mode[fmode] == 0)
		last++;
	spin_unlock(&ci->i_ceph_lock);

	if (last && ci->i_vino.snap == CEPH_NOSNAP)
		ceph_check_caps(ci, 0, NULL);
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
		if (force ||
		    ((cap->issued & drop) &&
		     (cap->issued & unless) == 0)) {
			if ((cap->issued & drop) &&
			    (cap->issued & unless) == 0) {
				int wanted = __ceph_caps_wanted(ci);
				if ((ci->i_ceph_flags & CEPH_I_NODELAY) == 0)
					wanted |= cap->mds_wanted;
				dout("encode_inode_release %p cap %p "
				     "%s -> %s, wanted %s -> %s\n", inode, cap,
				     ceph_cap_string(cap->issued),
				     ceph_cap_string(cap->issued & ~drop),
				     ceph_cap_string(cap->mds_wanted),
				     ceph_cap_string(wanted));

				cap->issued &= ~drop;
				cap->implemented &= ~drop;
				cap->mds_wanted = wanted;
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
			dout("encode_inode_release %p cap %p %s\n",
			     inode, cap, ceph_cap_string(cap->issued));
		}
	}
	spin_unlock(&ci->i_ceph_lock);
	return ret;
}

int ceph_encode_dentry_release(void **p, struct dentry *dentry,
			       int mds, int drop, int unless)
{
	struct inode *dir = d_inode(dentry->d_parent);
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
	spin_unlock(&dentry->d_lock);

	ret = ceph_encode_inode_release(p, dir, mds, drop, unless, force);

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
