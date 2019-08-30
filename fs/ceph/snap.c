// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>

#include <linux/sort.h>
#include <linux/slab.h>
#include "super.h"
#include "mds_client.h"
#include <linux/ceph/decode.h>

/* unused map expires after 5 minutes */
#define CEPH_SNAPID_MAP_TIMEOUT	(5 * 60 * HZ)

/*
 * Snapshots in ceph are driven in large part by cooperation from the
 * client.  In contrast to local file systems or file servers that
 * implement snapshots at a single point in the system, ceph's
 * distributed access to storage requires clients to help decide
 * whether a write logically occurs before or after a recently created
 * snapshot.
 *
 * This provides a perfect instantanous client-wide snapshot.  Between
 * clients, however, snapshots may appear to be applied at slightly
 * different points in time, depending on delays in delivering the
 * snapshot notification.
 *
 * Snapshots are _not_ file system-wide.  Instead, each snapshot
 * applies to the subdirectory nested beneath some directory.  This
 * effectively divides the hierarchy into multiple "realms," where all
 * of the files contained by each realm share the same set of
 * snapshots.  An individual realm's snap set contains snapshots
 * explicitly created on that realm, as well as any snaps in its
 * parent's snap set _after_ the point at which the parent became it's
 * parent (due to, say, a rename).  Similarly, snaps from prior parents
 * during the time intervals during which they were the parent are included.
 *
 * The client is spared most of this detail, fortunately... it must only
 * maintains a hierarchy of realms reflecting the current parent/child
 * realm relationship, and for each realm has an explicit list of snaps
 * inherited from prior parents.
 *
 * A snap_realm struct is maintained for realms containing every inode
 * with an open cap in the system.  (The needed snap realm information is
 * provided by the MDS whenever a cap is issued, i.e., on open.)  A 'seq'
 * version number is used to ensure that as realm parameters change (new
 * snapshot, new parent, etc.) the client's realm hierarchy is updated.
 *
 * The realm hierarchy drives the generation of a 'snap context' for each
 * realm, which simply lists the resulting set of snaps for the realm.  This
 * is attached to any writes sent to OSDs.
 */
/*
 * Unfortunately error handling is a bit mixed here.  If we get a snap
 * update, but don't have enough memory to update our realm hierarchy,
 * it's not clear what we can do about it (besides complaining to the
 * console).
 */


/*
 * increase ref count for the realm
 *
 * caller must hold snap_rwsem for write.
 */
void ceph_get_snap_realm(struct ceph_mds_client *mdsc,
			 struct ceph_snap_realm *realm)
{
	dout("get_realm %p %d -> %d\n", realm,
	     atomic_read(&realm->nref), atomic_read(&realm->nref)+1);
	/*
	 * since we _only_ increment realm refs or empty the empty
	 * list with snap_rwsem held, adjusting the empty list here is
	 * safe.  we do need to protect against concurrent empty list
	 * additions, however.
	 */
	if (atomic_inc_return(&realm->nref) == 1) {
		spin_lock(&mdsc->snap_empty_lock);
		list_del_init(&realm->empty_item);
		spin_unlock(&mdsc->snap_empty_lock);
	}
}

static void __insert_snap_realm(struct rb_root *root,
				struct ceph_snap_realm *new)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct ceph_snap_realm *r = NULL;

	while (*p) {
		parent = *p;
		r = rb_entry(parent, struct ceph_snap_realm, node);
		if (new->ino < r->ino)
			p = &(*p)->rb_left;
		else if (new->ino > r->ino)
			p = &(*p)->rb_right;
		else
			BUG();
	}

	rb_link_node(&new->node, parent, p);
	rb_insert_color(&new->node, root);
}

/*
 * create and get the realm rooted at @ino and bump its ref count.
 *
 * caller must hold snap_rwsem for write.
 */
static struct ceph_snap_realm *ceph_create_snap_realm(
	struct ceph_mds_client *mdsc,
	u64 ino)
{
	struct ceph_snap_realm *realm;

	realm = kzalloc(sizeof(*realm), GFP_NOFS);
	if (!realm)
		return ERR_PTR(-ENOMEM);

	atomic_set(&realm->nref, 1);    /* for caller */
	realm->ino = ino;
	INIT_LIST_HEAD(&realm->children);
	INIT_LIST_HEAD(&realm->child_item);
	INIT_LIST_HEAD(&realm->empty_item);
	INIT_LIST_HEAD(&realm->dirty_item);
	INIT_LIST_HEAD(&realm->inodes_with_caps);
	spin_lock_init(&realm->inodes_with_caps_lock);
	__insert_snap_realm(&mdsc->snap_realms, realm);
	mdsc->num_snap_realms++;

	dout("create_snap_realm %llx %p\n", realm->ino, realm);
	return realm;
}

/*
 * lookup the realm rooted at @ino.
 *
 * caller must hold snap_rwsem for write.
 */
static struct ceph_snap_realm *__lookup_snap_realm(struct ceph_mds_client *mdsc,
						   u64 ino)
{
	struct rb_node *n = mdsc->snap_realms.rb_node;
	struct ceph_snap_realm *r;

	while (n) {
		r = rb_entry(n, struct ceph_snap_realm, node);
		if (ino < r->ino)
			n = n->rb_left;
		else if (ino > r->ino)
			n = n->rb_right;
		else {
			dout("lookup_snap_realm %llx %p\n", r->ino, r);
			return r;
		}
	}
	return NULL;
}

struct ceph_snap_realm *ceph_lookup_snap_realm(struct ceph_mds_client *mdsc,
					       u64 ino)
{
	struct ceph_snap_realm *r;
	r = __lookup_snap_realm(mdsc, ino);
	if (r)
		ceph_get_snap_realm(mdsc, r);
	return r;
}

static void __put_snap_realm(struct ceph_mds_client *mdsc,
			     struct ceph_snap_realm *realm);

/*
 * called with snap_rwsem (write)
 */
static void __destroy_snap_realm(struct ceph_mds_client *mdsc,
				 struct ceph_snap_realm *realm)
{
	dout("__destroy_snap_realm %p %llx\n", realm, realm->ino);

	rb_erase(&realm->node, &mdsc->snap_realms);
	mdsc->num_snap_realms--;

	if (realm->parent) {
		list_del_init(&realm->child_item);
		__put_snap_realm(mdsc, realm->parent);
	}

	kfree(realm->prior_parent_snaps);
	kfree(realm->snaps);
	ceph_put_snap_context(realm->cached_context);
	kfree(realm);
}

/*
 * caller holds snap_rwsem (write)
 */
static void __put_snap_realm(struct ceph_mds_client *mdsc,
			     struct ceph_snap_realm *realm)
{
	dout("__put_snap_realm %llx %p %d -> %d\n", realm->ino, realm,
	     atomic_read(&realm->nref), atomic_read(&realm->nref)-1);
	if (atomic_dec_and_test(&realm->nref))
		__destroy_snap_realm(mdsc, realm);
}

/*
 * caller needn't hold any locks
 */
void ceph_put_snap_realm(struct ceph_mds_client *mdsc,
			 struct ceph_snap_realm *realm)
{
	dout("put_snap_realm %llx %p %d -> %d\n", realm->ino, realm,
	     atomic_read(&realm->nref), atomic_read(&realm->nref)-1);
	if (!atomic_dec_and_test(&realm->nref))
		return;

	if (down_write_trylock(&mdsc->snap_rwsem)) {
		__destroy_snap_realm(mdsc, realm);
		up_write(&mdsc->snap_rwsem);
	} else {
		spin_lock(&mdsc->snap_empty_lock);
		list_add(&realm->empty_item, &mdsc->snap_empty);
		spin_unlock(&mdsc->snap_empty_lock);
	}
}

/*
 * Clean up any realms whose ref counts have dropped to zero.  Note
 * that this does not include realms who were created but not yet
 * used.
 *
 * Called under snap_rwsem (write)
 */
static void __cleanup_empty_realms(struct ceph_mds_client *mdsc)
{
	struct ceph_snap_realm *realm;

	spin_lock(&mdsc->snap_empty_lock);
	while (!list_empty(&mdsc->snap_empty)) {
		realm = list_first_entry(&mdsc->snap_empty,
				   struct ceph_snap_realm, empty_item);
		list_del(&realm->empty_item);
		spin_unlock(&mdsc->snap_empty_lock);
		__destroy_snap_realm(mdsc, realm);
		spin_lock(&mdsc->snap_empty_lock);
	}
	spin_unlock(&mdsc->snap_empty_lock);
}

void ceph_cleanup_empty_realms(struct ceph_mds_client *mdsc)
{
	down_write(&mdsc->snap_rwsem);
	__cleanup_empty_realms(mdsc);
	up_write(&mdsc->snap_rwsem);
}

/*
 * adjust the parent realm of a given @realm.  adjust child list, and parent
 * pointers, and ref counts appropriately.
 *
 * return true if parent was changed, 0 if unchanged, <0 on error.
 *
 * caller must hold snap_rwsem for write.
 */
static int adjust_snap_realm_parent(struct ceph_mds_client *mdsc,
				    struct ceph_snap_realm *realm,
				    u64 parentino)
{
	struct ceph_snap_realm *parent;

	if (realm->parent_ino == parentino)
		return 0;

	parent = ceph_lookup_snap_realm(mdsc, parentino);
	if (!parent) {
		parent = ceph_create_snap_realm(mdsc, parentino);
		if (IS_ERR(parent))
			return PTR_ERR(parent);
	}
	dout("adjust_snap_realm_parent %llx %p: %llx %p -> %llx %p\n",
	     realm->ino, realm, realm->parent_ino, realm->parent,
	     parentino, parent);
	if (realm->parent) {
		list_del_init(&realm->child_item);
		ceph_put_snap_realm(mdsc, realm->parent);
	}
	realm->parent_ino = parentino;
	realm->parent = parent;
	list_add(&realm->child_item, &parent->children);
	return 1;
}


static int cmpu64_rev(const void *a, const void *b)
{
	if (*(u64 *)a < *(u64 *)b)
		return 1;
	if (*(u64 *)a > *(u64 *)b)
		return -1;
	return 0;
}


/*
 * build the snap context for a given realm.
 */
static int build_snap_context(struct ceph_snap_realm *realm,
			      struct list_head* dirty_realms)
{
	struct ceph_snap_realm *parent = realm->parent;
	struct ceph_snap_context *snapc;
	int err = 0;
	u32 num = realm->num_prior_parent_snaps + realm->num_snaps;

	/*
	 * build parent context, if it hasn't been built.
	 * conservatively estimate that all parent snaps might be
	 * included by us.
	 */
	if (parent) {
		if (!parent->cached_context) {
			err = build_snap_context(parent, dirty_realms);
			if (err)
				goto fail;
		}
		num += parent->cached_context->num_snaps;
	}

	/* do i actually need to update?  not if my context seq
	   matches realm seq, and my parents' does to.  (this works
	   because we rebuild_snap_realms() works _downward_ in
	   hierarchy after each update.) */
	if (realm->cached_context &&
	    realm->cached_context->seq == realm->seq &&
	    (!parent ||
	     realm->cached_context->seq >= parent->cached_context->seq)) {
		dout("build_snap_context %llx %p: %p seq %lld (%u snaps)"
		     " (unchanged)\n",
		     realm->ino, realm, realm->cached_context,
		     realm->cached_context->seq,
		     (unsigned int)realm->cached_context->num_snaps);
		return 0;
	}

	/* alloc new snap context */
	err = -ENOMEM;
	if (num > (SIZE_MAX - sizeof(*snapc)) / sizeof(u64))
		goto fail;
	snapc = ceph_create_snap_context(num, GFP_NOFS);
	if (!snapc)
		goto fail;

	/* build (reverse sorted) snap vector */
	num = 0;
	snapc->seq = realm->seq;
	if (parent) {
		u32 i;

		/* include any of parent's snaps occurring _after_ my
		   parent became my parent */
		for (i = 0; i < parent->cached_context->num_snaps; i++)
			if (parent->cached_context->snaps[i] >=
			    realm->parent_since)
				snapc->snaps[num++] =
					parent->cached_context->snaps[i];
		if (parent->cached_context->seq > snapc->seq)
			snapc->seq = parent->cached_context->seq;
	}
	memcpy(snapc->snaps + num, realm->snaps,
	       sizeof(u64)*realm->num_snaps);
	num += realm->num_snaps;
	memcpy(snapc->snaps + num, realm->prior_parent_snaps,
	       sizeof(u64)*realm->num_prior_parent_snaps);
	num += realm->num_prior_parent_snaps;

	sort(snapc->snaps, num, sizeof(u64), cmpu64_rev, NULL);
	snapc->num_snaps = num;
	dout("build_snap_context %llx %p: %p seq %lld (%u snaps)\n",
	     realm->ino, realm, snapc, snapc->seq,
	     (unsigned int) snapc->num_snaps);

	ceph_put_snap_context(realm->cached_context);
	realm->cached_context = snapc;
	/* queue realm for cap_snap creation */
	list_add_tail(&realm->dirty_item, dirty_realms);
	return 0;

fail:
	/*
	 * if we fail, clear old (incorrect) cached_context... hopefully
	 * we'll have better luck building it later
	 */
	if (realm->cached_context) {
		ceph_put_snap_context(realm->cached_context);
		realm->cached_context = NULL;
	}
	pr_err("build_snap_context %llx %p fail %d\n", realm->ino,
	       realm, err);
	return err;
}

/*
 * rebuild snap context for the given realm and all of its children.
 */
static void rebuild_snap_realms(struct ceph_snap_realm *realm,
				struct list_head *dirty_realms)
{
	struct ceph_snap_realm *child;

	dout("rebuild_snap_realms %llx %p\n", realm->ino, realm);
	build_snap_context(realm, dirty_realms);

	list_for_each_entry(child, &realm->children, child_item)
		rebuild_snap_realms(child, dirty_realms);
}


/*
 * helper to allocate and decode an array of snapids.  free prior
 * instance, if any.
 */
static int dup_array(u64 **dst, __le64 *src, u32 num)
{
	u32 i;

	kfree(*dst);
	if (num) {
		*dst = kcalloc(num, sizeof(u64), GFP_NOFS);
		if (!*dst)
			return -ENOMEM;
		for (i = 0; i < num; i++)
			(*dst)[i] = get_unaligned_le64(src + i);
	} else {
		*dst = NULL;
	}
	return 0;
}

static bool has_new_snaps(struct ceph_snap_context *o,
			  struct ceph_snap_context *n)
{
	if (n->num_snaps == 0)
		return false;
	/* snaps are in descending order */
	return n->snaps[0] > o->seq;
}

/*
 * When a snapshot is applied, the size/mtime inode metadata is queued
 * in a ceph_cap_snap (one for each snapshot) until writeback
 * completes and the metadata can be flushed back to the MDS.
 *
 * However, if a (sync) write is currently in-progress when we apply
 * the snapshot, we have to wait until the write succeeds or fails
 * (and a final size/mtime is known).  In this case the
 * cap_snap->writing = 1, and is said to be "pending."  When the write
 * finishes, we __ceph_finish_cap_snap().
 *
 * Caller must hold snap_rwsem for read (i.e., the realm topology won't
 * change).
 */
void ceph_queue_cap_snap(struct ceph_inode_info *ci)
{
	struct inode *inode = &ci->vfs_inode;
	struct ceph_cap_snap *capsnap;
	struct ceph_snap_context *old_snapc, *new_snapc;
	int used, dirty;

	capsnap = kzalloc(sizeof(*capsnap), GFP_NOFS);
	if (!capsnap) {
		pr_err("ENOMEM allocating ceph_cap_snap on %p\n", inode);
		return;
	}

	spin_lock(&ci->i_ceph_lock);
	used = __ceph_caps_used(ci);
	dirty = __ceph_caps_dirty(ci);

	old_snapc = ci->i_head_snapc;
	new_snapc = ci->i_snap_realm->cached_context;

	/*
	 * If there is a write in progress, treat that as a dirty Fw,
	 * even though it hasn't completed yet; by the time we finish
	 * up this capsnap it will be.
	 */
	if (used & CEPH_CAP_FILE_WR)
		dirty |= CEPH_CAP_FILE_WR;

	if (__ceph_have_pending_cap_snap(ci)) {
		/* there is no point in queuing multiple "pending" cap_snaps,
		   as no new writes are allowed to start when pending, so any
		   writes in progress now were started before the previous
		   cap_snap.  lucky us. */
		dout("queue_cap_snap %p already pending\n", inode);
		goto update_snapc;
	}
	if (ci->i_wrbuffer_ref_head == 0 &&
	    !(dirty & (CEPH_CAP_ANY_EXCL|CEPH_CAP_FILE_WR))) {
		dout("queue_cap_snap %p nothing dirty|writing\n", inode);
		goto update_snapc;
	}

	BUG_ON(!old_snapc);

	/*
	 * There is no need to send FLUSHSNAP message to MDS if there is
	 * no new snapshot. But when there is dirty pages or on-going
	 * writes, we still need to create cap_snap. cap_snap is needed
	 * by the write path and page writeback path.
	 *
	 * also see ceph_try_drop_cap_snap()
	 */
	if (has_new_snaps(old_snapc, new_snapc)) {
		if (dirty & (CEPH_CAP_ANY_EXCL|CEPH_CAP_FILE_WR))
			capsnap->need_flush = true;
	} else {
		if (!(used & CEPH_CAP_FILE_WR) &&
		    ci->i_wrbuffer_ref_head == 0) {
			dout("queue_cap_snap %p "
			     "no new_snap|dirty_page|writing\n", inode);
			goto update_snapc;
		}
	}

	dout("queue_cap_snap %p cap_snap %p queuing under %p %s %s\n",
	     inode, capsnap, old_snapc, ceph_cap_string(dirty),
	     capsnap->need_flush ? "" : "no_flush");
	ihold(inode);

	refcount_set(&capsnap->nref, 1);
	INIT_LIST_HEAD(&capsnap->ci_item);

	capsnap->follows = old_snapc->seq;
	capsnap->issued = __ceph_caps_issued(ci, NULL);
	capsnap->dirty = dirty;

	capsnap->mode = inode->i_mode;
	capsnap->uid = inode->i_uid;
	capsnap->gid = inode->i_gid;

	if (dirty & CEPH_CAP_XATTR_EXCL) {
		__ceph_build_xattrs_blob(ci);
		capsnap->xattr_blob =
			ceph_buffer_get(ci->i_xattrs.blob);
		capsnap->xattr_version = ci->i_xattrs.version;
	} else {
		capsnap->xattr_blob = NULL;
		capsnap->xattr_version = 0;
	}

	capsnap->inline_data = ci->i_inline_version != CEPH_INLINE_NONE;

	/* dirty page count moved from _head to this cap_snap;
	   all subsequent writes page dirties occur _after_ this
	   snapshot. */
	capsnap->dirty_pages = ci->i_wrbuffer_ref_head;
	ci->i_wrbuffer_ref_head = 0;
	capsnap->context = old_snapc;
	list_add_tail(&capsnap->ci_item, &ci->i_cap_snaps);

	if (used & CEPH_CAP_FILE_WR) {
		dout("queue_cap_snap %p cap_snap %p snapc %p"
		     " seq %llu used WR, now pending\n", inode,
		     capsnap, old_snapc, old_snapc->seq);
		capsnap->writing = 1;
	} else {
		/* note mtime, size NOW. */
		__ceph_finish_cap_snap(ci, capsnap);
	}
	capsnap = NULL;
	old_snapc = NULL;

update_snapc:
       if (ci->i_wrbuffer_ref_head == 0 &&
           ci->i_wr_ref == 0 &&
           ci->i_dirty_caps == 0 &&
           ci->i_flushing_caps == 0) {
               ci->i_head_snapc = NULL;
       } else {
		ci->i_head_snapc = ceph_get_snap_context(new_snapc);
		dout(" new snapc is %p\n", new_snapc);
	}
	spin_unlock(&ci->i_ceph_lock);

	kfree(capsnap);
	ceph_put_snap_context(old_snapc);
}

/*
 * Finalize the size, mtime for a cap_snap.. that is, settle on final values
 * to be used for the snapshot, to be flushed back to the mds.
 *
 * If capsnap can now be flushed, add to snap_flush list, and return 1.
 *
 * Caller must hold i_ceph_lock.
 */
int __ceph_finish_cap_snap(struct ceph_inode_info *ci,
			    struct ceph_cap_snap *capsnap)
{
	struct inode *inode = &ci->vfs_inode;
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;

	BUG_ON(capsnap->writing);
	capsnap->size = inode->i_size;
	capsnap->mtime = inode->i_mtime;
	capsnap->atime = inode->i_atime;
	capsnap->ctime = inode->i_ctime;
	capsnap->time_warp_seq = ci->i_time_warp_seq;
	capsnap->truncate_size = ci->i_truncate_size;
	capsnap->truncate_seq = ci->i_truncate_seq;
	if (capsnap->dirty_pages) {
		dout("finish_cap_snap %p cap_snap %p snapc %p %llu %s s=%llu "
		     "still has %d dirty pages\n", inode, capsnap,
		     capsnap->context, capsnap->context->seq,
		     ceph_cap_string(capsnap->dirty), capsnap->size,
		     capsnap->dirty_pages);
		return 0;
	}

	ci->i_ceph_flags |= CEPH_I_FLUSH_SNAPS;
	dout("finish_cap_snap %p cap_snap %p snapc %p %llu %s s=%llu\n",
	     inode, capsnap, capsnap->context,
	     capsnap->context->seq, ceph_cap_string(capsnap->dirty),
	     capsnap->size);

	spin_lock(&mdsc->snap_flush_lock);
	if (list_empty(&ci->i_snap_flush_item))
		list_add_tail(&ci->i_snap_flush_item, &mdsc->snap_flush_list);
	spin_unlock(&mdsc->snap_flush_lock);
	return 1;  /* caller may want to ceph_flush_snaps */
}

/*
 * Queue cap_snaps for snap writeback for this realm and its children.
 * Called under snap_rwsem, so realm topology won't change.
 */
static void queue_realm_cap_snaps(struct ceph_snap_realm *realm)
{
	struct ceph_inode_info *ci;
	struct inode *lastinode = NULL;

	dout("queue_realm_cap_snaps %p %llx inodes\n", realm, realm->ino);

	spin_lock(&realm->inodes_with_caps_lock);
	list_for_each_entry(ci, &realm->inodes_with_caps, i_snap_realm_item) {
		struct inode *inode = igrab(&ci->vfs_inode);
		if (!inode)
			continue;
		spin_unlock(&realm->inodes_with_caps_lock);
		/* avoid calling iput_final() while holding
		 * mdsc->snap_rwsem or in mds dispatch threads */
		ceph_async_iput(lastinode);
		lastinode = inode;
		ceph_queue_cap_snap(ci);
		spin_lock(&realm->inodes_with_caps_lock);
	}
	spin_unlock(&realm->inodes_with_caps_lock);
	ceph_async_iput(lastinode);

	dout("queue_realm_cap_snaps %p %llx done\n", realm, realm->ino);
}

/*
 * Parse and apply a snapblob "snap trace" from the MDS.  This specifies
 * the snap realm parameters from a given realm and all of its ancestors,
 * up to the root.
 *
 * Caller must hold snap_rwsem for write.
 */
int ceph_update_snap_trace(struct ceph_mds_client *mdsc,
			   void *p, void *e, bool deletion,
			   struct ceph_snap_realm **realm_ret)
{
	struct ceph_mds_snap_realm *ri;    /* encoded */
	__le64 *snaps;                     /* encoded */
	__le64 *prior_parent_snaps;        /* encoded */
	struct ceph_snap_realm *realm = NULL;
	struct ceph_snap_realm *first_realm = NULL;
	int invalidate = 0;
	int err = -ENOMEM;
	LIST_HEAD(dirty_realms);

	dout("update_snap_trace deletion=%d\n", deletion);
more:
	ceph_decode_need(&p, e, sizeof(*ri), bad);
	ri = p;
	p += sizeof(*ri);
	ceph_decode_need(&p, e, sizeof(u64)*(le32_to_cpu(ri->num_snaps) +
			    le32_to_cpu(ri->num_prior_parent_snaps)), bad);
	snaps = p;
	p += sizeof(u64) * le32_to_cpu(ri->num_snaps);
	prior_parent_snaps = p;
	p += sizeof(u64) * le32_to_cpu(ri->num_prior_parent_snaps);

	realm = ceph_lookup_snap_realm(mdsc, le64_to_cpu(ri->ino));
	if (!realm) {
		realm = ceph_create_snap_realm(mdsc, le64_to_cpu(ri->ino));
		if (IS_ERR(realm)) {
			err = PTR_ERR(realm);
			goto fail;
		}
	}

	/* ensure the parent is correct */
	err = adjust_snap_realm_parent(mdsc, realm, le64_to_cpu(ri->parent));
	if (err < 0)
		goto fail;
	invalidate += err;

	if (le64_to_cpu(ri->seq) > realm->seq) {
		dout("update_snap_trace updating %llx %p %lld -> %lld\n",
		     realm->ino, realm, realm->seq, le64_to_cpu(ri->seq));
		/* update realm parameters, snap lists */
		realm->seq = le64_to_cpu(ri->seq);
		realm->created = le64_to_cpu(ri->created);
		realm->parent_since = le64_to_cpu(ri->parent_since);

		realm->num_snaps = le32_to_cpu(ri->num_snaps);
		err = dup_array(&realm->snaps, snaps, realm->num_snaps);
		if (err < 0)
			goto fail;

		realm->num_prior_parent_snaps =
			le32_to_cpu(ri->num_prior_parent_snaps);
		err = dup_array(&realm->prior_parent_snaps, prior_parent_snaps,
				realm->num_prior_parent_snaps);
		if (err < 0)
			goto fail;

		if (realm->seq > mdsc->last_snap_seq)
			mdsc->last_snap_seq = realm->seq;

		invalidate = 1;
	} else if (!realm->cached_context) {
		dout("update_snap_trace %llx %p seq %lld new\n",
		     realm->ino, realm, realm->seq);
		invalidate = 1;
	} else {
		dout("update_snap_trace %llx %p seq %lld unchanged\n",
		     realm->ino, realm, realm->seq);
	}

	dout("done with %llx %p, invalidated=%d, %p %p\n", realm->ino,
	     realm, invalidate, p, e);

	/* invalidate when we reach the _end_ (root) of the trace */
	if (invalidate && p >= e)
		rebuild_snap_realms(realm, &dirty_realms);

	if (!first_realm)
		first_realm = realm;
	else
		ceph_put_snap_realm(mdsc, realm);

	if (p < e)
		goto more;

	/*
	 * queue cap snaps _after_ we've built the new snap contexts,
	 * so that i_head_snapc can be set appropriately.
	 */
	while (!list_empty(&dirty_realms)) {
		realm = list_first_entry(&dirty_realms, struct ceph_snap_realm,
					 dirty_item);
		list_del_init(&realm->dirty_item);
		queue_realm_cap_snaps(realm);
	}

	if (realm_ret)
		*realm_ret = first_realm;
	else
		ceph_put_snap_realm(mdsc, first_realm);

	__cleanup_empty_realms(mdsc);
	return 0;

bad:
	err = -EINVAL;
fail:
	if (realm && !IS_ERR(realm))
		ceph_put_snap_realm(mdsc, realm);
	if (first_realm)
		ceph_put_snap_realm(mdsc, first_realm);
	pr_err("update_snap_trace error %d\n", err);
	return err;
}


/*
 * Send any cap_snaps that are queued for flush.  Try to carry
 * s_mutex across multiple snap flushes to avoid locking overhead.
 *
 * Caller holds no locks.
 */
static void flush_snaps(struct ceph_mds_client *mdsc)
{
	struct ceph_inode_info *ci;
	struct inode *inode;
	struct ceph_mds_session *session = NULL;

	dout("flush_snaps\n");
	spin_lock(&mdsc->snap_flush_lock);
	while (!list_empty(&mdsc->snap_flush_list)) {
		ci = list_first_entry(&mdsc->snap_flush_list,
				struct ceph_inode_info, i_snap_flush_item);
		inode = &ci->vfs_inode;
		ihold(inode);
		spin_unlock(&mdsc->snap_flush_lock);
		ceph_flush_snaps(ci, &session);
		/* avoid calling iput_final() while holding
		 * session->s_mutex or in mds dispatch threads */
		ceph_async_iput(inode);
		spin_lock(&mdsc->snap_flush_lock);
	}
	spin_unlock(&mdsc->snap_flush_lock);

	if (session) {
		mutex_unlock(&session->s_mutex);
		ceph_put_mds_session(session);
	}
	dout("flush_snaps done\n");
}


/*
 * Handle a snap notification from the MDS.
 *
 * This can take two basic forms: the simplest is just a snap creation
 * or deletion notification on an existing realm.  This should update the
 * realm and its children.
 *
 * The more difficult case is realm creation, due to snap creation at a
 * new point in the file hierarchy, or due to a rename that moves a file or
 * directory into another realm.
 */
void ceph_handle_snap(struct ceph_mds_client *mdsc,
		      struct ceph_mds_session *session,
		      struct ceph_msg *msg)
{
	struct super_block *sb = mdsc->fsc->sb;
	int mds = session->s_mds;
	u64 split;
	int op;
	int trace_len;
	struct ceph_snap_realm *realm = NULL;
	void *p = msg->front.iov_base;
	void *e = p + msg->front.iov_len;
	struct ceph_mds_snap_head *h;
	int num_split_inos, num_split_realms;
	__le64 *split_inos = NULL, *split_realms = NULL;
	int i;
	int locked_rwsem = 0;

	/* decode */
	if (msg->front.iov_len < sizeof(*h))
		goto bad;
	h = p;
	op = le32_to_cpu(h->op);
	split = le64_to_cpu(h->split);   /* non-zero if we are splitting an
					  * existing realm */
	num_split_inos = le32_to_cpu(h->num_split_inos);
	num_split_realms = le32_to_cpu(h->num_split_realms);
	trace_len = le32_to_cpu(h->trace_len);
	p += sizeof(*h);

	dout("handle_snap from mds%d op %s split %llx tracelen %d\n", mds,
	     ceph_snap_op_name(op), split, trace_len);

	mutex_lock(&session->s_mutex);
	session->s_seq++;
	mutex_unlock(&session->s_mutex);

	down_write(&mdsc->snap_rwsem);
	locked_rwsem = 1;

	if (op == CEPH_SNAP_OP_SPLIT) {
		struct ceph_mds_snap_realm *ri;

		/*
		 * A "split" breaks part of an existing realm off into
		 * a new realm.  The MDS provides a list of inodes
		 * (with caps) and child realms that belong to the new
		 * child.
		 */
		split_inos = p;
		p += sizeof(u64) * num_split_inos;
		split_realms = p;
		p += sizeof(u64) * num_split_realms;
		ceph_decode_need(&p, e, sizeof(*ri), bad);
		/* we will peek at realm info here, but will _not_
		 * advance p, as the realm update will occur below in
		 * ceph_update_snap_trace. */
		ri = p;

		realm = ceph_lookup_snap_realm(mdsc, split);
		if (!realm) {
			realm = ceph_create_snap_realm(mdsc, split);
			if (IS_ERR(realm))
				goto out;
		}

		dout("splitting snap_realm %llx %p\n", realm->ino, realm);
		for (i = 0; i < num_split_inos; i++) {
			struct ceph_vino vino = {
				.ino = le64_to_cpu(split_inos[i]),
				.snap = CEPH_NOSNAP,
			};
			struct inode *inode = ceph_find_inode(sb, vino);
			struct ceph_inode_info *ci;
			struct ceph_snap_realm *oldrealm;

			if (!inode)
				continue;
			ci = ceph_inode(inode);

			spin_lock(&ci->i_ceph_lock);
			if (!ci->i_snap_realm)
				goto skip_inode;
			/*
			 * If this inode belongs to a realm that was
			 * created after our new realm, we experienced
			 * a race (due to another split notifications
			 * arriving from a different MDS).  So skip
			 * this inode.
			 */
			if (ci->i_snap_realm->created >
			    le64_to_cpu(ri->created)) {
				dout(" leaving %p in newer realm %llx %p\n",
				     inode, ci->i_snap_realm->ino,
				     ci->i_snap_realm);
				goto skip_inode;
			}
			dout(" will move %p to split realm %llx %p\n",
			     inode, realm->ino, realm);
			/*
			 * Move the inode to the new realm
			 */
			oldrealm = ci->i_snap_realm;
			spin_lock(&oldrealm->inodes_with_caps_lock);
			list_del_init(&ci->i_snap_realm_item);
			spin_unlock(&oldrealm->inodes_with_caps_lock);

			spin_lock(&realm->inodes_with_caps_lock);
			list_add(&ci->i_snap_realm_item,
				 &realm->inodes_with_caps);
			ci->i_snap_realm = realm;
			if (realm->ino == ci->i_vino.ino)
                                realm->inode = inode;
			spin_unlock(&realm->inodes_with_caps_lock);

			spin_unlock(&ci->i_ceph_lock);

			ceph_get_snap_realm(mdsc, realm);
			ceph_put_snap_realm(mdsc, oldrealm);

			/* avoid calling iput_final() while holding
			 * mdsc->snap_rwsem or mds in dispatch threads */
			ceph_async_iput(inode);
			continue;

skip_inode:
			spin_unlock(&ci->i_ceph_lock);
			ceph_async_iput(inode);
		}

		/* we may have taken some of the old realm's children. */
		for (i = 0; i < num_split_realms; i++) {
			struct ceph_snap_realm *child =
				__lookup_snap_realm(mdsc,
					   le64_to_cpu(split_realms[i]));
			if (!child)
				continue;
			adjust_snap_realm_parent(mdsc, child, realm->ino);
		}
	}

	/*
	 * update using the provided snap trace. if we are deleting a
	 * snap, we can avoid queueing cap_snaps.
	 */
	ceph_update_snap_trace(mdsc, p, e,
			       op == CEPH_SNAP_OP_DESTROY, NULL);

	if (op == CEPH_SNAP_OP_SPLIT)
		/* we took a reference when we created the realm, above */
		ceph_put_snap_realm(mdsc, realm);

	__cleanup_empty_realms(mdsc);

	up_write(&mdsc->snap_rwsem);

	flush_snaps(mdsc);
	return;

bad:
	pr_err("corrupt snap message from mds%d\n", mds);
	ceph_msg_dump(msg);
out:
	if (locked_rwsem)
		up_write(&mdsc->snap_rwsem);
	return;
}

struct ceph_snapid_map* ceph_get_snapid_map(struct ceph_mds_client *mdsc,
					    u64 snap)
{
	struct ceph_snapid_map *sm, *exist;
	struct rb_node **p, *parent;
	int ret;

	exist = NULL;
	spin_lock(&mdsc->snapid_map_lock);
	p = &mdsc->snapid_map_tree.rb_node;
	while (*p) {
		exist = rb_entry(*p, struct ceph_snapid_map, node);
		if (snap > exist->snap) {
			p = &(*p)->rb_left;
		} else if (snap < exist->snap) {
			p = &(*p)->rb_right;
		} else {
			if (atomic_inc_return(&exist->ref) == 1)
				list_del_init(&exist->lru);
			break;
		}
		exist = NULL;
	}
	spin_unlock(&mdsc->snapid_map_lock);
	if (exist) {
		dout("found snapid map %llx -> %x\n", exist->snap, exist->dev);
		return exist;
	}

	sm = kmalloc(sizeof(*sm), GFP_NOFS);
	if (!sm)
		return NULL;

	ret = get_anon_bdev(&sm->dev);
	if (ret < 0) {
		kfree(sm);
		return NULL;
	}

	INIT_LIST_HEAD(&sm->lru);
	atomic_set(&sm->ref, 1);
	sm->snap = snap;

	exist = NULL;
	parent = NULL;
	p = &mdsc->snapid_map_tree.rb_node;
	spin_lock(&mdsc->snapid_map_lock);
	while (*p) {
		parent = *p;
		exist = rb_entry(*p, struct ceph_snapid_map, node);
		if (snap > exist->snap)
			p = &(*p)->rb_left;
		else if (snap < exist->snap)
			p = &(*p)->rb_right;
		else
			break;
		exist = NULL;
	}
	if (exist) {
		if (atomic_inc_return(&exist->ref) == 1)
			list_del_init(&exist->lru);
	} else {
		rb_link_node(&sm->node, parent, p);
		rb_insert_color(&sm->node, &mdsc->snapid_map_tree);
	}
	spin_unlock(&mdsc->snapid_map_lock);
	if (exist) {
		free_anon_bdev(sm->dev);
		kfree(sm);
		dout("found snapid map %llx -> %x\n", exist->snap, exist->dev);
		return exist;
	}

	dout("create snapid map %llx -> %x\n", sm->snap, sm->dev);
	return sm;
}

void ceph_put_snapid_map(struct ceph_mds_client* mdsc,
			 struct ceph_snapid_map *sm)
{
	if (!sm)
		return;
	if (atomic_dec_and_lock(&sm->ref, &mdsc->snapid_map_lock)) {
		if (!RB_EMPTY_NODE(&sm->node)) {
			sm->last_used = jiffies;
			list_add_tail(&sm->lru, &mdsc->snapid_map_lru);
			spin_unlock(&mdsc->snapid_map_lock);
		} else {
			/* already cleaned up by
			 * ceph_cleanup_snapid_map() */
			spin_unlock(&mdsc->snapid_map_lock);
			kfree(sm);
		}
	}
}

void ceph_trim_snapid_map(struct ceph_mds_client *mdsc)
{
	struct ceph_snapid_map *sm;
	unsigned long now;
	LIST_HEAD(to_free);

	spin_lock(&mdsc->snapid_map_lock);
	now = jiffies;

	while (!list_empty(&mdsc->snapid_map_lru)) {
		sm = list_first_entry(&mdsc->snapid_map_lru,
				      struct ceph_snapid_map, lru);
		if (time_after(sm->last_used + CEPH_SNAPID_MAP_TIMEOUT, now))
			break;

		rb_erase(&sm->node, &mdsc->snapid_map_tree);
		list_move(&sm->lru, &to_free);
	}
	spin_unlock(&mdsc->snapid_map_lock);

	while (!list_empty(&to_free)) {
		sm = list_first_entry(&to_free, struct ceph_snapid_map, lru);
		list_del(&sm->lru);
		dout("trim snapid map %llx -> %x\n", sm->snap, sm->dev);
		free_anon_bdev(sm->dev);
		kfree(sm);
	}
}

void ceph_cleanup_snapid_map(struct ceph_mds_client *mdsc)
{
	struct ceph_snapid_map *sm;
	struct rb_node *p;
	LIST_HEAD(to_free);

	spin_lock(&mdsc->snapid_map_lock);
	while ((p = rb_first(&mdsc->snapid_map_tree))) {
		sm = rb_entry(p, struct ceph_snapid_map, node);
		rb_erase(p, &mdsc->snapid_map_tree);
		RB_CLEAR_NODE(p);
		list_move(&sm->lru, &to_free);
	}
	spin_unlock(&mdsc->snapid_map_lock);

	while (!list_empty(&to_free)) {
		sm = list_first_entry(&to_free, struct ceph_snapid_map, lru);
		list_del(&sm->lru);
		free_anon_bdev(sm->dev);
		if (WARN_ON_ONCE(atomic_read(&sm->ref))) {
			pr_err("snapid map %llx -> %x still in use\n",
			       sm->snap, sm->dev);
		}
	}
}
