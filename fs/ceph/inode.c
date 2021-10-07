// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/writeback.h>
#include <linux/vmalloc.h>
#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include <linux/random.h>
#include <linux/sort.h>
#include <linux/iversion.h>

#include "super.h"
#include "mds_client.h"
#include "cache.h"
#include <linux/ceph/decode.h>

/*
 * Ceph inode operations
 *
 * Implement basic inode helpers (get, alloc) and inode ops (getattr,
 * setattr, etc.), xattr helpers, and helpers for assimilating
 * metadata returned by the MDS into our cache.
 *
 * Also define helpers for doing asynchronous writeback, invalidation,
 * and truncation for the benefit of those who can't afford to block
 * (typically because they are in the message handler path).
 */

static const struct inode_operations ceph_symlink_iops;

static void ceph_inode_work(struct work_struct *work);

/*
 * find or create an inode, given the ceph ino number
 */
static int ceph_set_ino_cb(struct inode *inode, void *data)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(inode->i_sb);

	ci->i_vino = *(struct ceph_vino *)data;
	inode->i_ino = ceph_vino_to_ino_t(ci->i_vino);
	inode_set_iversion_raw(inode, 0);
	percpu_counter_inc(&mdsc->metric.total_inodes);

	return 0;
}

struct inode *ceph_get_inode(struct super_block *sb, struct ceph_vino vino)
{
	struct inode *inode;

	if (ceph_vino_is_reserved(vino))
		return ERR_PTR(-EREMOTEIO);

	inode = iget5_locked(sb, (unsigned long)vino.ino, ceph_ino_compare,
			     ceph_set_ino_cb, &vino);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	dout("get_inode on %llu=%llx.%llx got %p new %d\n", ceph_present_inode(inode),
	     ceph_vinop(inode), inode, !!(inode->i_state & I_NEW));
	return inode;
}

/*
 * get/constuct snapdir inode for a given directory
 */
struct inode *ceph_get_snapdir(struct inode *parent)
{
	struct ceph_vino vino = {
		.ino = ceph_ino(parent),
		.snap = CEPH_SNAPDIR,
	};
	struct inode *inode = ceph_get_inode(parent->i_sb, vino);
	struct ceph_inode_info *ci = ceph_inode(inode);

	BUG_ON(!S_ISDIR(parent->i_mode));
	if (IS_ERR(inode))
		return inode;
	inode->i_mode = parent->i_mode;
	inode->i_uid = parent->i_uid;
	inode->i_gid = parent->i_gid;
	inode->i_mtime = parent->i_mtime;
	inode->i_ctime = parent->i_ctime;
	inode->i_atime = parent->i_atime;
	ci->i_rbytes = 0;
	ci->i_btime = ceph_inode(parent)->i_btime;

	if (inode->i_state & I_NEW) {
		inode->i_op = &ceph_snapdir_iops;
		inode->i_fop = &ceph_snapdir_fops;
		ci->i_snap_caps = CEPH_CAP_PIN; /* so we can open */
		unlock_new_inode(inode);
	}

	return inode;
}

const struct inode_operations ceph_file_iops = {
	.permission = ceph_permission,
	.setattr = ceph_setattr,
	.getattr = ceph_getattr,
	.listxattr = ceph_listxattr,
	.get_acl = ceph_get_acl,
	.set_acl = ceph_set_acl,
};


/*
 * We use a 'frag tree' to keep track of the MDS's directory fragments
 * for a given inode (usually there is just a single fragment).  We
 * need to know when a child frag is delegated to a new MDS, or when
 * it is flagged as replicated, so we can direct our requests
 * accordingly.
 */

/*
 * find/create a frag in the tree
 */
static struct ceph_inode_frag *__get_or_create_frag(struct ceph_inode_info *ci,
						    u32 f)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct ceph_inode_frag *frag;
	int c;

	p = &ci->i_fragtree.rb_node;
	while (*p) {
		parent = *p;
		frag = rb_entry(parent, struct ceph_inode_frag, node);
		c = ceph_frag_compare(f, frag->frag);
		if (c < 0)
			p = &(*p)->rb_left;
		else if (c > 0)
			p = &(*p)->rb_right;
		else
			return frag;
	}

	frag = kmalloc(sizeof(*frag), GFP_NOFS);
	if (!frag)
		return ERR_PTR(-ENOMEM);

	frag->frag = f;
	frag->split_by = 0;
	frag->mds = -1;
	frag->ndist = 0;

	rb_link_node(&frag->node, parent, p);
	rb_insert_color(&frag->node, &ci->i_fragtree);

	dout("get_or_create_frag added %llx.%llx frag %x\n",
	     ceph_vinop(&ci->vfs_inode), f);
	return frag;
}

/*
 * find a specific frag @f
 */
struct ceph_inode_frag *__ceph_find_frag(struct ceph_inode_info *ci, u32 f)
{
	struct rb_node *n = ci->i_fragtree.rb_node;

	while (n) {
		struct ceph_inode_frag *frag =
			rb_entry(n, struct ceph_inode_frag, node);
		int c = ceph_frag_compare(f, frag->frag);
		if (c < 0)
			n = n->rb_left;
		else if (c > 0)
			n = n->rb_right;
		else
			return frag;
	}
	return NULL;
}

/*
 * Choose frag containing the given value @v.  If @pfrag is
 * specified, copy the frag delegation info to the caller if
 * it is present.
 */
static u32 __ceph_choose_frag(struct ceph_inode_info *ci, u32 v,
			      struct ceph_inode_frag *pfrag, int *found)
{
	u32 t = ceph_frag_make(0, 0);
	struct ceph_inode_frag *frag;
	unsigned nway, i;
	u32 n;

	if (found)
		*found = 0;

	while (1) {
		WARN_ON(!ceph_frag_contains_value(t, v));
		frag = __ceph_find_frag(ci, t);
		if (!frag)
			break; /* t is a leaf */
		if (frag->split_by == 0) {
			if (pfrag)
				memcpy(pfrag, frag, sizeof(*pfrag));
			if (found)
				*found = 1;
			break;
		}

		/* choose child */
		nway = 1 << frag->split_by;
		dout("choose_frag(%x) %x splits by %d (%d ways)\n", v, t,
		     frag->split_by, nway);
		for (i = 0; i < nway; i++) {
			n = ceph_frag_make_child(t, frag->split_by, i);
			if (ceph_frag_contains_value(n, v)) {
				t = n;
				break;
			}
		}
		BUG_ON(i == nway);
	}
	dout("choose_frag(%x) = %x\n", v, t);

	return t;
}

u32 ceph_choose_frag(struct ceph_inode_info *ci, u32 v,
		     struct ceph_inode_frag *pfrag, int *found)
{
	u32 ret;
	mutex_lock(&ci->i_fragtree_mutex);
	ret = __ceph_choose_frag(ci, v, pfrag, found);
	mutex_unlock(&ci->i_fragtree_mutex);
	return ret;
}

/*
 * Process dirfrag (delegation) info from the mds.  Include leaf
 * fragment in tree ONLY if ndist > 0.  Otherwise, only
 * branches/splits are included in i_fragtree)
 */
static int ceph_fill_dirfrag(struct inode *inode,
			     struct ceph_mds_reply_dirfrag *dirinfo)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_inode_frag *frag;
	u32 id = le32_to_cpu(dirinfo->frag);
	int mds = le32_to_cpu(dirinfo->auth);
	int ndist = le32_to_cpu(dirinfo->ndist);
	int diri_auth = -1;
	int i;
	int err = 0;

	spin_lock(&ci->i_ceph_lock);
	if (ci->i_auth_cap)
		diri_auth = ci->i_auth_cap->mds;
	spin_unlock(&ci->i_ceph_lock);

	if (mds == -1) /* CDIR_AUTH_PARENT */
		mds = diri_auth;

	mutex_lock(&ci->i_fragtree_mutex);
	if (ndist == 0 && mds == diri_auth) {
		/* no delegation info needed. */
		frag = __ceph_find_frag(ci, id);
		if (!frag)
			goto out;
		if (frag->split_by == 0) {
			/* tree leaf, remove */
			dout("fill_dirfrag removed %llx.%llx frag %x"
			     " (no ref)\n", ceph_vinop(inode), id);
			rb_erase(&frag->node, &ci->i_fragtree);
			kfree(frag);
		} else {
			/* tree branch, keep and clear */
			dout("fill_dirfrag cleared %llx.%llx frag %x"
			     " referral\n", ceph_vinop(inode), id);
			frag->mds = -1;
			frag->ndist = 0;
		}
		goto out;
	}


	/* find/add this frag to store mds delegation info */
	frag = __get_or_create_frag(ci, id);
	if (IS_ERR(frag)) {
		/* this is not the end of the world; we can continue
		   with bad/inaccurate delegation info */
		pr_err("fill_dirfrag ENOMEM on mds ref %llx.%llx fg %x\n",
		       ceph_vinop(inode), le32_to_cpu(dirinfo->frag));
		err = -ENOMEM;
		goto out;
	}

	frag->mds = mds;
	frag->ndist = min_t(u32, ndist, CEPH_MAX_DIRFRAG_REP);
	for (i = 0; i < frag->ndist; i++)
		frag->dist[i] = le32_to_cpu(dirinfo->dist[i]);
	dout("fill_dirfrag %llx.%llx frag %x ndist=%d\n",
	     ceph_vinop(inode), frag->frag, frag->ndist);

out:
	mutex_unlock(&ci->i_fragtree_mutex);
	return err;
}

static int frag_tree_split_cmp(const void *l, const void *r)
{
	struct ceph_frag_tree_split *ls = (struct ceph_frag_tree_split*)l;
	struct ceph_frag_tree_split *rs = (struct ceph_frag_tree_split*)r;
	return ceph_frag_compare(le32_to_cpu(ls->frag),
				 le32_to_cpu(rs->frag));
}

static bool is_frag_child(u32 f, struct ceph_inode_frag *frag)
{
	if (!frag)
		return f == ceph_frag_make(0, 0);
	if (ceph_frag_bits(f) != ceph_frag_bits(frag->frag) + frag->split_by)
		return false;
	return ceph_frag_contains_value(frag->frag, ceph_frag_value(f));
}

static int ceph_fill_fragtree(struct inode *inode,
			      struct ceph_frag_tree_head *fragtree,
			      struct ceph_mds_reply_dirfrag *dirinfo)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_inode_frag *frag, *prev_frag = NULL;
	struct rb_node *rb_node;
	unsigned i, split_by, nsplits;
	u32 id;
	bool update = false;

	mutex_lock(&ci->i_fragtree_mutex);
	nsplits = le32_to_cpu(fragtree->nsplits);
	if (nsplits != ci->i_fragtree_nsplits) {
		update = true;
	} else if (nsplits) {
		i = prandom_u32() % nsplits;
		id = le32_to_cpu(fragtree->splits[i].frag);
		if (!__ceph_find_frag(ci, id))
			update = true;
	} else if (!RB_EMPTY_ROOT(&ci->i_fragtree)) {
		rb_node = rb_first(&ci->i_fragtree);
		frag = rb_entry(rb_node, struct ceph_inode_frag, node);
		if (frag->frag != ceph_frag_make(0, 0) || rb_next(rb_node))
			update = true;
	}
	if (!update && dirinfo) {
		id = le32_to_cpu(dirinfo->frag);
		if (id != __ceph_choose_frag(ci, id, NULL, NULL))
			update = true;
	}
	if (!update)
		goto out_unlock;

	if (nsplits > 1) {
		sort(fragtree->splits, nsplits, sizeof(fragtree->splits[0]),
		     frag_tree_split_cmp, NULL);
	}

	dout("fill_fragtree %llx.%llx\n", ceph_vinop(inode));
	rb_node = rb_first(&ci->i_fragtree);
	for (i = 0; i < nsplits; i++) {
		id = le32_to_cpu(fragtree->splits[i].frag);
		split_by = le32_to_cpu(fragtree->splits[i].by);
		if (split_by == 0 || ceph_frag_bits(id) + split_by > 24) {
			pr_err("fill_fragtree %llx.%llx invalid split %d/%u, "
			       "frag %x split by %d\n", ceph_vinop(inode),
			       i, nsplits, id, split_by);
			continue;
		}
		frag = NULL;
		while (rb_node) {
			frag = rb_entry(rb_node, struct ceph_inode_frag, node);
			if (ceph_frag_compare(frag->frag, id) >= 0) {
				if (frag->frag != id)
					frag = NULL;
				else
					rb_node = rb_next(rb_node);
				break;
			}
			rb_node = rb_next(rb_node);
			/* delete stale split/leaf node */
			if (frag->split_by > 0 ||
			    !is_frag_child(frag->frag, prev_frag)) {
				rb_erase(&frag->node, &ci->i_fragtree);
				if (frag->split_by > 0)
					ci->i_fragtree_nsplits--;
				kfree(frag);
			}
			frag = NULL;
		}
		if (!frag) {
			frag = __get_or_create_frag(ci, id);
			if (IS_ERR(frag))
				continue;
		}
		if (frag->split_by == 0)
			ci->i_fragtree_nsplits++;
		frag->split_by = split_by;
		dout(" frag %x split by %d\n", frag->frag, frag->split_by);
		prev_frag = frag;
	}
	while (rb_node) {
		frag = rb_entry(rb_node, struct ceph_inode_frag, node);
		rb_node = rb_next(rb_node);
		/* delete stale split/leaf node */
		if (frag->split_by > 0 ||
		    !is_frag_child(frag->frag, prev_frag)) {
			rb_erase(&frag->node, &ci->i_fragtree);
			if (frag->split_by > 0)
				ci->i_fragtree_nsplits--;
			kfree(frag);
		}
	}
out_unlock:
	mutex_unlock(&ci->i_fragtree_mutex);
	return 0;
}

/*
 * initialize a newly allocated inode.
 */
struct inode *ceph_alloc_inode(struct super_block *sb)
{
	struct ceph_inode_info *ci;
	int i;

	ci = kmem_cache_alloc(ceph_inode_cachep, GFP_NOFS);
	if (!ci)
		return NULL;

	dout("alloc_inode %p\n", &ci->vfs_inode);

	spin_lock_init(&ci->i_ceph_lock);

	ci->i_version = 0;
	ci->i_inline_version = 0;
	ci->i_time_warp_seq = 0;
	ci->i_ceph_flags = 0;
	atomic64_set(&ci->i_ordered_count, 1);
	atomic64_set(&ci->i_release_count, 1);
	atomic64_set(&ci->i_complete_seq[0], 0);
	atomic64_set(&ci->i_complete_seq[1], 0);
	ci->i_symlink = NULL;

	ci->i_max_bytes = 0;
	ci->i_max_files = 0;

	memset(&ci->i_dir_layout, 0, sizeof(ci->i_dir_layout));
	memset(&ci->i_cached_layout, 0, sizeof(ci->i_cached_layout));
	RCU_INIT_POINTER(ci->i_layout.pool_ns, NULL);

	ci->i_fragtree = RB_ROOT;
	mutex_init(&ci->i_fragtree_mutex);

	ci->i_xattrs.blob = NULL;
	ci->i_xattrs.prealloc_blob = NULL;
	ci->i_xattrs.dirty = false;
	ci->i_xattrs.index = RB_ROOT;
	ci->i_xattrs.count = 0;
	ci->i_xattrs.names_size = 0;
	ci->i_xattrs.vals_size = 0;
	ci->i_xattrs.version = 0;
	ci->i_xattrs.index_version = 0;

	ci->i_caps = RB_ROOT;
	ci->i_auth_cap = NULL;
	ci->i_dirty_caps = 0;
	ci->i_flushing_caps = 0;
	INIT_LIST_HEAD(&ci->i_dirty_item);
	INIT_LIST_HEAD(&ci->i_flushing_item);
	ci->i_prealloc_cap_flush = NULL;
	INIT_LIST_HEAD(&ci->i_cap_flush_list);
	init_waitqueue_head(&ci->i_cap_wq);
	ci->i_hold_caps_max = 0;
	INIT_LIST_HEAD(&ci->i_cap_delay_list);
	INIT_LIST_HEAD(&ci->i_cap_snaps);
	ci->i_head_snapc = NULL;
	ci->i_snap_caps = 0;

	ci->i_last_rd = ci->i_last_wr = jiffies - 3600 * HZ;
	for (i = 0; i < CEPH_FILE_MODE_BITS; i++)
		ci->i_nr_by_mode[i] = 0;

	mutex_init(&ci->i_truncate_mutex);
	ci->i_truncate_seq = 0;
	ci->i_truncate_size = 0;
	ci->i_truncate_pending = 0;

	ci->i_max_size = 0;
	ci->i_reported_size = 0;
	ci->i_wanted_max_size = 0;
	ci->i_requested_max_size = 0;

	ci->i_pin_ref = 0;
	ci->i_rd_ref = 0;
	ci->i_rdcache_ref = 0;
	ci->i_wr_ref = 0;
	ci->i_wb_ref = 0;
	ci->i_fx_ref = 0;
	ci->i_wrbuffer_ref = 0;
	ci->i_wrbuffer_ref_head = 0;
	atomic_set(&ci->i_filelock_ref, 0);
	atomic_set(&ci->i_shared_gen, 1);
	ci->i_rdcache_gen = 0;
	ci->i_rdcache_revoking = 0;

	INIT_LIST_HEAD(&ci->i_unsafe_dirops);
	INIT_LIST_HEAD(&ci->i_unsafe_iops);
	spin_lock_init(&ci->i_unsafe_lock);

	ci->i_snap_realm = NULL;
	INIT_LIST_HEAD(&ci->i_snap_realm_item);
	INIT_LIST_HEAD(&ci->i_snap_flush_item);

	INIT_WORK(&ci->i_work, ceph_inode_work);
	ci->i_work_mask = 0;
	memset(&ci->i_btime, '\0', sizeof(ci->i_btime));

	ceph_fscache_inode_init(ci);

	return &ci->vfs_inode;
}

void ceph_free_inode(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	kfree(ci->i_symlink);
	kmem_cache_free(ceph_inode_cachep, ci);
}

void ceph_evict_inode(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(inode->i_sb);
	struct ceph_inode_frag *frag;
	struct rb_node *n;

	dout("evict_inode %p ino %llx.%llx\n", inode, ceph_vinop(inode));

	percpu_counter_dec(&mdsc->metric.total_inodes);

	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);

	ceph_fscache_unregister_inode_cookie(ci);

	__ceph_remove_caps(ci);

	if (__ceph_has_any_quota(ci))
		ceph_adjust_quota_realms_count(inode, false);

	/*
	 * we may still have a snap_realm reference if there are stray
	 * caps in i_snap_caps.
	 */
	if (ci->i_snap_realm) {
		if (ceph_snap(inode) == CEPH_NOSNAP) {
			struct ceph_snap_realm *realm = ci->i_snap_realm;
			dout(" dropping residual ref to snap realm %p\n",
			     realm);
			spin_lock(&realm->inodes_with_caps_lock);
			list_del_init(&ci->i_snap_realm_item);
			ci->i_snap_realm = NULL;
			if (realm->ino == ci->i_vino.ino)
				realm->inode = NULL;
			spin_unlock(&realm->inodes_with_caps_lock);
			ceph_put_snap_realm(mdsc, realm);
		} else {
			ceph_put_snapid_map(mdsc, ci->i_snapid_map);
			ci->i_snap_realm = NULL;
		}
	}

	while ((n = rb_first(&ci->i_fragtree)) != NULL) {
		frag = rb_entry(n, struct ceph_inode_frag, node);
		rb_erase(n, &ci->i_fragtree);
		kfree(frag);
	}
	ci->i_fragtree_nsplits = 0;

	__ceph_destroy_xattrs(ci);
	if (ci->i_xattrs.blob)
		ceph_buffer_put(ci->i_xattrs.blob);
	if (ci->i_xattrs.prealloc_blob)
		ceph_buffer_put(ci->i_xattrs.prealloc_blob);

	ceph_put_string(rcu_dereference_raw(ci->i_layout.pool_ns));
	ceph_put_string(rcu_dereference_raw(ci->i_cached_layout.pool_ns));
}

static inline blkcnt_t calc_inode_blocks(u64 size)
{
	return (size + (1<<9) - 1) >> 9;
}

/*
 * Helpers to fill in size, ctime, mtime, and atime.  We have to be
 * careful because either the client or MDS may have more up to date
 * info, depending on which capabilities are held, and whether
 * time_warp_seq or truncate_seq have increased.  (Ordinarily, mtime
 * and size are monotonically increasing, except when utimes() or
 * truncate() increments the corresponding _seq values.)
 */
int ceph_fill_file_size(struct inode *inode, int issued,
			u32 truncate_seq, u64 truncate_size, u64 size)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int queue_trunc = 0;

	if (ceph_seq_cmp(truncate_seq, ci->i_truncate_seq) > 0 ||
	    (truncate_seq == ci->i_truncate_seq && size > inode->i_size)) {
		dout("size %lld -> %llu\n", inode->i_size, size);
		if (size > 0 && S_ISDIR(inode->i_mode)) {
			pr_err("fill_file_size non-zero size for directory\n");
			size = 0;
		}
		i_size_write(inode, size);
		inode->i_blocks = calc_inode_blocks(size);
		ci->i_reported_size = size;
		if (truncate_seq != ci->i_truncate_seq) {
			dout("truncate_seq %u -> %u\n",
			     ci->i_truncate_seq, truncate_seq);
			ci->i_truncate_seq = truncate_seq;

			/* the MDS should have revoked these caps */
			WARN_ON_ONCE(issued & (CEPH_CAP_FILE_EXCL |
					       CEPH_CAP_FILE_RD |
					       CEPH_CAP_FILE_WR |
					       CEPH_CAP_FILE_LAZYIO));
			/*
			 * If we hold relevant caps, or in the case where we're
			 * not the only client referencing this file and we
			 * don't hold those caps, then we need to check whether
			 * the file is either opened or mmaped
			 */
			if ((issued & (CEPH_CAP_FILE_CACHE|
				       CEPH_CAP_FILE_BUFFER)) ||
			    mapping_mapped(inode->i_mapping) ||
			    __ceph_is_file_opened(ci)) {
				ci->i_truncate_pending++;
				queue_trunc = 1;
			}
		}
	}
	if (ceph_seq_cmp(truncate_seq, ci->i_truncate_seq) >= 0 &&
	    ci->i_truncate_size != truncate_size) {
		dout("truncate_size %lld -> %llu\n", ci->i_truncate_size,
		     truncate_size);
		ci->i_truncate_size = truncate_size;
	}

	if (queue_trunc)
		ceph_fscache_invalidate(inode);

	return queue_trunc;
}

void ceph_fill_file_time(struct inode *inode, int issued,
			 u64 time_warp_seq, struct timespec64 *ctime,
			 struct timespec64 *mtime, struct timespec64 *atime)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int warn = 0;

	if (issued & (CEPH_CAP_FILE_EXCL|
		      CEPH_CAP_FILE_WR|
		      CEPH_CAP_FILE_BUFFER|
		      CEPH_CAP_AUTH_EXCL|
		      CEPH_CAP_XATTR_EXCL)) {
		if (ci->i_version == 0 ||
		    timespec64_compare(ctime, &inode->i_ctime) > 0) {
			dout("ctime %lld.%09ld -> %lld.%09ld inc w/ cap\n",
			     inode->i_ctime.tv_sec, inode->i_ctime.tv_nsec,
			     ctime->tv_sec, ctime->tv_nsec);
			inode->i_ctime = *ctime;
		}
		if (ci->i_version == 0 ||
		    ceph_seq_cmp(time_warp_seq, ci->i_time_warp_seq) > 0) {
			/* the MDS did a utimes() */
			dout("mtime %lld.%09ld -> %lld.%09ld "
			     "tw %d -> %d\n",
			     inode->i_mtime.tv_sec, inode->i_mtime.tv_nsec,
			     mtime->tv_sec, mtime->tv_nsec,
			     ci->i_time_warp_seq, (int)time_warp_seq);

			inode->i_mtime = *mtime;
			inode->i_atime = *atime;
			ci->i_time_warp_seq = time_warp_seq;
		} else if (time_warp_seq == ci->i_time_warp_seq) {
			/* nobody did utimes(); take the max */
			if (timespec64_compare(mtime, &inode->i_mtime) > 0) {
				dout("mtime %lld.%09ld -> %lld.%09ld inc\n",
				     inode->i_mtime.tv_sec,
				     inode->i_mtime.tv_nsec,
				     mtime->tv_sec, mtime->tv_nsec);
				inode->i_mtime = *mtime;
			}
			if (timespec64_compare(atime, &inode->i_atime) > 0) {
				dout("atime %lld.%09ld -> %lld.%09ld inc\n",
				     inode->i_atime.tv_sec,
				     inode->i_atime.tv_nsec,
				     atime->tv_sec, atime->tv_nsec);
				inode->i_atime = *atime;
			}
		} else if (issued & CEPH_CAP_FILE_EXCL) {
			/* we did a utimes(); ignore mds values */
		} else {
			warn = 1;
		}
	} else {
		/* we have no write|excl caps; whatever the MDS says is true */
		if (ceph_seq_cmp(time_warp_seq, ci->i_time_warp_seq) >= 0) {
			inode->i_ctime = *ctime;
			inode->i_mtime = *mtime;
			inode->i_atime = *atime;
			ci->i_time_warp_seq = time_warp_seq;
		} else {
			warn = 1;
		}
	}
	if (warn) /* time_warp_seq shouldn't go backwards */
		dout("%p mds time_warp_seq %llu < %u\n",
		     inode, time_warp_seq, ci->i_time_warp_seq);
}

/*
 * Populate an inode based on info from mds.  May be called on new or
 * existing inodes.
 */
int ceph_fill_inode(struct inode *inode, struct page *locked_page,
		    struct ceph_mds_reply_info_in *iinfo,
		    struct ceph_mds_reply_dirfrag *dirinfo,
		    struct ceph_mds_session *session, int cap_fmode,
		    struct ceph_cap_reservation *caps_reservation)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(inode->i_sb);
	struct ceph_mds_reply_inode *info = iinfo->in;
	struct ceph_inode_info *ci = ceph_inode(inode);
	int issued, new_issued, info_caps;
	struct timespec64 mtime, atime, ctime;
	struct ceph_buffer *xattr_blob = NULL;
	struct ceph_buffer *old_blob = NULL;
	struct ceph_string *pool_ns = NULL;
	struct ceph_cap *new_cap = NULL;
	int err = 0;
	bool wake = false;
	bool queue_trunc = false;
	bool new_version = false;
	bool fill_inline = false;

	lockdep_assert_held(&mdsc->snap_rwsem);

	dout("%s %p ino %llx.%llx v %llu had %llu\n", __func__,
	     inode, ceph_vinop(inode), le64_to_cpu(info->version),
	     ci->i_version);

	info_caps = le32_to_cpu(info->cap.caps);

	/* prealloc new cap struct */
	if (info_caps && ceph_snap(inode) == CEPH_NOSNAP) {
		new_cap = ceph_get_cap(mdsc, caps_reservation);
		if (!new_cap)
			return -ENOMEM;
	}

	/*
	 * prealloc xattr data, if it looks like we'll need it.  only
	 * if len > 4 (meaning there are actually xattrs; the first 4
	 * bytes are the xattr count).
	 */
	if (iinfo->xattr_len > 4) {
		xattr_blob = ceph_buffer_new(iinfo->xattr_len, GFP_NOFS);
		if (!xattr_blob)
			pr_err("%s ENOMEM xattr blob %d bytes\n", __func__,
			       iinfo->xattr_len);
	}

	if (iinfo->pool_ns_len > 0)
		pool_ns = ceph_find_or_create_string(iinfo->pool_ns_data,
						     iinfo->pool_ns_len);

	if (ceph_snap(inode) != CEPH_NOSNAP && !ci->i_snapid_map)
		ci->i_snapid_map = ceph_get_snapid_map(mdsc, ceph_snap(inode));

	spin_lock(&ci->i_ceph_lock);

	/*
	 * provided version will be odd if inode value is projected,
	 * even if stable.  skip the update if we have newer stable
	 * info (ours>=theirs, e.g. due to racing mds replies), unless
	 * we are getting projected (unstable) info (in which case the
	 * version is odd, and we want ours>theirs).
	 *   us   them
	 *   2    2     skip
	 *   3    2     skip
	 *   3    3     update
	 */
	if (ci->i_version == 0 ||
	    ((info->cap.flags & CEPH_CAP_FLAG_AUTH) &&
	     le64_to_cpu(info->version) > (ci->i_version & ~1)))
		new_version = true;

	/* Update change_attribute */
	inode_set_max_iversion_raw(inode, iinfo->change_attr);

	__ceph_caps_issued(ci, &issued);
	issued |= __ceph_caps_dirty(ci);
	new_issued = ~issued & info_caps;

	/* update inode */
	inode->i_rdev = le32_to_cpu(info->rdev);
	/* directories have fl_stripe_unit set to zero */
	if (le32_to_cpu(info->layout.fl_stripe_unit))
		inode->i_blkbits =
			fls(le32_to_cpu(info->layout.fl_stripe_unit)) - 1;
	else
		inode->i_blkbits = CEPH_BLOCK_SHIFT;

	__ceph_update_quota(ci, iinfo->max_bytes, iinfo->max_files);

	if ((new_version || (new_issued & CEPH_CAP_AUTH_SHARED)) &&
	    (issued & CEPH_CAP_AUTH_EXCL) == 0) {
		inode->i_mode = le32_to_cpu(info->mode);
		inode->i_uid = make_kuid(&init_user_ns, le32_to_cpu(info->uid));
		inode->i_gid = make_kgid(&init_user_ns, le32_to_cpu(info->gid));
		dout("%p mode 0%o uid.gid %d.%d\n", inode, inode->i_mode,
		     from_kuid(&init_user_ns, inode->i_uid),
		     from_kgid(&init_user_ns, inode->i_gid));
		ceph_decode_timespec64(&ci->i_btime, &iinfo->btime);
		ceph_decode_timespec64(&ci->i_snap_btime, &iinfo->snap_btime);
	}

	if ((new_version || (new_issued & CEPH_CAP_LINK_SHARED)) &&
	    (issued & CEPH_CAP_LINK_EXCL) == 0)
		set_nlink(inode, le32_to_cpu(info->nlink));

	if (new_version || (new_issued & CEPH_CAP_ANY_RD)) {
		/* be careful with mtime, atime, size */
		ceph_decode_timespec64(&atime, &info->atime);
		ceph_decode_timespec64(&mtime, &info->mtime);
		ceph_decode_timespec64(&ctime, &info->ctime);
		ceph_fill_file_time(inode, issued,
				le32_to_cpu(info->time_warp_seq),
				&ctime, &mtime, &atime);
	}

	if (new_version || (info_caps & CEPH_CAP_FILE_SHARED)) {
		ci->i_files = le64_to_cpu(info->files);
		ci->i_subdirs = le64_to_cpu(info->subdirs);
	}

	if (new_version ||
	    (new_issued & (CEPH_CAP_ANY_FILE_RD | CEPH_CAP_ANY_FILE_WR))) {
		s64 old_pool = ci->i_layout.pool_id;
		struct ceph_string *old_ns;

		ceph_file_layout_from_legacy(&ci->i_layout, &info->layout);
		old_ns = rcu_dereference_protected(ci->i_layout.pool_ns,
					lockdep_is_held(&ci->i_ceph_lock));
		rcu_assign_pointer(ci->i_layout.pool_ns, pool_ns);

		if (ci->i_layout.pool_id != old_pool || pool_ns != old_ns)
			ci->i_ceph_flags &= ~CEPH_I_POOL_PERM;

		pool_ns = old_ns;

		queue_trunc = ceph_fill_file_size(inode, issued,
					le32_to_cpu(info->truncate_seq),
					le64_to_cpu(info->truncate_size),
					le64_to_cpu(info->size));
		/* only update max_size on auth cap */
		if ((info->cap.flags & CEPH_CAP_FLAG_AUTH) &&
		    ci->i_max_size != le64_to_cpu(info->max_size)) {
			dout("max_size %lld -> %llu\n", ci->i_max_size,
					le64_to_cpu(info->max_size));
			ci->i_max_size = le64_to_cpu(info->max_size);
		}
	}

	/* layout and rstat are not tracked by capability, update them if
	 * the inode info is from auth mds */
	if (new_version || (info->cap.flags & CEPH_CAP_FLAG_AUTH)) {
		if (S_ISDIR(inode->i_mode)) {
			ci->i_dir_layout = iinfo->dir_layout;
			ci->i_rbytes = le64_to_cpu(info->rbytes);
			ci->i_rfiles = le64_to_cpu(info->rfiles);
			ci->i_rsubdirs = le64_to_cpu(info->rsubdirs);
			ci->i_dir_pin = iinfo->dir_pin;
			ceph_decode_timespec64(&ci->i_rctime, &info->rctime);
		}
	}

	/* xattrs */
	/* note that if i_xattrs.len <= 4, i_xattrs.data will still be NULL. */
	if ((ci->i_xattrs.version == 0 || !(issued & CEPH_CAP_XATTR_EXCL))  &&
	    le64_to_cpu(info->xattr_version) > ci->i_xattrs.version) {
		if (ci->i_xattrs.blob)
			old_blob = ci->i_xattrs.blob;
		ci->i_xattrs.blob = xattr_blob;
		if (xattr_blob)
			memcpy(ci->i_xattrs.blob->vec.iov_base,
			       iinfo->xattr_data, iinfo->xattr_len);
		ci->i_xattrs.version = le64_to_cpu(info->xattr_version);
		ceph_forget_all_cached_acls(inode);
		ceph_security_invalidate_secctx(inode);
		xattr_blob = NULL;
	}

	/* finally update i_version */
	if (le64_to_cpu(info->version) > ci->i_version)
		ci->i_version = le64_to_cpu(info->version);

	inode->i_mapping->a_ops = &ceph_aops;

	switch (inode->i_mode & S_IFMT) {
	case S_IFIFO:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFSOCK:
		inode->i_blkbits = PAGE_SHIFT;
		init_special_inode(inode, inode->i_mode, inode->i_rdev);
		inode->i_op = &ceph_file_iops;
		break;
	case S_IFREG:
		inode->i_op = &ceph_file_iops;
		inode->i_fop = &ceph_file_fops;
		break;
	case S_IFLNK:
		inode->i_op = &ceph_symlink_iops;
		if (!ci->i_symlink) {
			u32 symlen = iinfo->symlink_len;
			char *sym;

			spin_unlock(&ci->i_ceph_lock);

			if (symlen != i_size_read(inode)) {
				pr_err("%s %llx.%llx BAD symlink "
					"size %lld\n", __func__,
					ceph_vinop(inode),
					i_size_read(inode));
				i_size_write(inode, symlen);
				inode->i_blocks = calc_inode_blocks(symlen);
			}

			err = -ENOMEM;
			sym = kstrndup(iinfo->symlink, symlen, GFP_NOFS);
			if (!sym)
				goto out;

			spin_lock(&ci->i_ceph_lock);
			if (!ci->i_symlink)
				ci->i_symlink = sym;
			else
				kfree(sym); /* lost a race */
		}
		inode->i_link = ci->i_symlink;
		break;
	case S_IFDIR:
		inode->i_op = &ceph_dir_iops;
		inode->i_fop = &ceph_dir_fops;
		break;
	default:
		pr_err("%s %llx.%llx BAD mode 0%o\n", __func__,
		       ceph_vinop(inode), inode->i_mode);
	}

	/* were we issued a capability? */
	if (info_caps) {
		if (ceph_snap(inode) == CEPH_NOSNAP) {
			ceph_add_cap(inode, session,
				     le64_to_cpu(info->cap.cap_id),
				     info_caps,
				     le32_to_cpu(info->cap.wanted),
				     le32_to_cpu(info->cap.seq),
				     le32_to_cpu(info->cap.mseq),
				     le64_to_cpu(info->cap.realm),
				     info->cap.flags, &new_cap);

			/* set dir completion flag? */
			if (S_ISDIR(inode->i_mode) &&
			    ci->i_files == 0 && ci->i_subdirs == 0 &&
			    (info_caps & CEPH_CAP_FILE_SHARED) &&
			    (issued & CEPH_CAP_FILE_EXCL) == 0 &&
			    !__ceph_dir_is_complete(ci)) {
				dout(" marking %p complete (empty)\n", inode);
				i_size_write(inode, 0);
				__ceph_dir_set_complete(ci,
					atomic64_read(&ci->i_release_count),
					atomic64_read(&ci->i_ordered_count));
			}

			wake = true;
		} else {
			dout(" %p got snap_caps %s\n", inode,
			     ceph_cap_string(info_caps));
			ci->i_snap_caps |= info_caps;
		}
	}

	if (iinfo->inline_version > 0 &&
	    iinfo->inline_version >= ci->i_inline_version) {
		int cache_caps = CEPH_CAP_FILE_CACHE | CEPH_CAP_FILE_LAZYIO;
		ci->i_inline_version = iinfo->inline_version;
		if (ci->i_inline_version != CEPH_INLINE_NONE &&
		    (locked_page || (info_caps & cache_caps)))
			fill_inline = true;
	}

	if (cap_fmode >= 0) {
		if (!info_caps)
			pr_warn("mds issued no caps on %llx.%llx\n",
				ceph_vinop(inode));
		__ceph_touch_fmode(ci, mdsc, cap_fmode);
	}

	spin_unlock(&ci->i_ceph_lock);

	if (fill_inline)
		ceph_fill_inline_data(inode, locked_page,
				      iinfo->inline_data, iinfo->inline_len);

	if (wake)
		wake_up_all(&ci->i_cap_wq);

	/* queue truncate if we saw i_size decrease */
	if (queue_trunc)
		ceph_queue_vmtruncate(inode);

	/* populate frag tree */
	if (S_ISDIR(inode->i_mode))
		ceph_fill_fragtree(inode, &info->fragtree, dirinfo);

	/* update delegation info? */
	if (dirinfo)
		ceph_fill_dirfrag(inode, dirinfo);

	err = 0;
out:
	if (new_cap)
		ceph_put_cap(mdsc, new_cap);
	ceph_buffer_put(old_blob);
	ceph_buffer_put(xattr_blob);
	ceph_put_string(pool_ns);
	return err;
}

/*
 * caller should hold session s_mutex and dentry->d_lock.
 */
static void __update_dentry_lease(struct inode *dir, struct dentry *dentry,
				  struct ceph_mds_reply_lease *lease,
				  struct ceph_mds_session *session,
				  unsigned long from_time,
				  struct ceph_mds_session **old_lease_session)
{
	struct ceph_dentry_info *di = ceph_dentry(dentry);
	unsigned mask = le16_to_cpu(lease->mask);
	long unsigned duration = le32_to_cpu(lease->duration_ms);
	long unsigned ttl = from_time + (duration * HZ) / 1000;
	long unsigned half_ttl = from_time + (duration * HZ / 2) / 1000;

	dout("update_dentry_lease %p duration %lu ms ttl %lu\n",
	     dentry, duration, ttl);

	/* only track leases on regular dentries */
	if (ceph_snap(dir) != CEPH_NOSNAP)
		return;

	if (mask & CEPH_LEASE_PRIMARY_LINK)
		di->flags |= CEPH_DENTRY_PRIMARY_LINK;
	else
		di->flags &= ~CEPH_DENTRY_PRIMARY_LINK;

	di->lease_shared_gen = atomic_read(&ceph_inode(dir)->i_shared_gen);
	if (!(mask & CEPH_LEASE_VALID)) {
		__ceph_dentry_dir_lease_touch(di);
		return;
	}

	if (di->lease_gen == session->s_cap_gen &&
	    time_before(ttl, di->time))
		return;  /* we already have a newer lease. */

	if (di->lease_session && di->lease_session != session) {
		*old_lease_session = di->lease_session;
		di->lease_session = NULL;
	}

	if (!di->lease_session)
		di->lease_session = ceph_get_mds_session(session);
	di->lease_gen = session->s_cap_gen;
	di->lease_seq = le32_to_cpu(lease->seq);
	di->lease_renew_after = half_ttl;
	di->lease_renew_from = 0;
	di->time = ttl;

	__ceph_dentry_lease_touch(di);
}

static inline void update_dentry_lease(struct inode *dir, struct dentry *dentry,
					struct ceph_mds_reply_lease *lease,
					struct ceph_mds_session *session,
					unsigned long from_time)
{
	struct ceph_mds_session *old_lease_session = NULL;
	spin_lock(&dentry->d_lock);
	__update_dentry_lease(dir, dentry, lease, session, from_time,
			      &old_lease_session);
	spin_unlock(&dentry->d_lock);
	ceph_put_mds_session(old_lease_session);
}

/*
 * update dentry lease without having parent inode locked
 */
static void update_dentry_lease_careful(struct dentry *dentry,
					struct ceph_mds_reply_lease *lease,
					struct ceph_mds_session *session,
					unsigned long from_time,
					char *dname, u32 dname_len,
					struct ceph_vino *pdvino,
					struct ceph_vino *ptvino)

{
	struct inode *dir;
	struct ceph_mds_session *old_lease_session = NULL;

	spin_lock(&dentry->d_lock);
	/* make sure dentry's name matches target */
	if (dentry->d_name.len != dname_len ||
	    memcmp(dentry->d_name.name, dname, dname_len))
		goto out_unlock;

	dir = d_inode(dentry->d_parent);
	/* make sure parent matches dvino */
	if (!ceph_ino_compare(dir, pdvino))
		goto out_unlock;

	/* make sure dentry's inode matches target. NULL ptvino means that
	 * we expect a negative dentry */
	if (ptvino) {
		if (d_really_is_negative(dentry))
			goto out_unlock;
		if (!ceph_ino_compare(d_inode(dentry), ptvino))
			goto out_unlock;
	} else {
		if (d_really_is_positive(dentry))
			goto out_unlock;
	}

	__update_dentry_lease(dir, dentry, lease, session,
			      from_time, &old_lease_session);
out_unlock:
	spin_unlock(&dentry->d_lock);
	ceph_put_mds_session(old_lease_session);
}

/*
 * splice a dentry to an inode.
 * caller must hold directory i_mutex for this to be safe.
 */
static int splice_dentry(struct dentry **pdn, struct inode *in)
{
	struct dentry *dn = *pdn;
	struct dentry *realdn;

	BUG_ON(d_inode(dn));

	if (S_ISDIR(in->i_mode)) {
		/* If inode is directory, d_splice_alias() below will remove
		 * 'realdn' from its origin parent. We need to ensure that
		 * origin parent's readdir cache will not reference 'realdn'
		 */
		realdn = d_find_any_alias(in);
		if (realdn) {
			struct ceph_dentry_info *di = ceph_dentry(realdn);
			spin_lock(&realdn->d_lock);

			realdn->d_op->d_prune(realdn);

			di->time = jiffies;
			di->lease_shared_gen = 0;
			di->offset = 0;

			spin_unlock(&realdn->d_lock);
			dput(realdn);
		}
	}

	/* dn must be unhashed */
	if (!d_unhashed(dn))
		d_drop(dn);
	realdn = d_splice_alias(in, dn);
	if (IS_ERR(realdn)) {
		pr_err("splice_dentry error %ld %p inode %p ino %llx.%llx\n",
		       PTR_ERR(realdn), dn, in, ceph_vinop(in));
		return PTR_ERR(realdn);
	}

	if (realdn) {
		dout("dn %p (%d) spliced with %p (%d) "
		     "inode %p ino %llx.%llx\n",
		     dn, d_count(dn),
		     realdn, d_count(realdn),
		     d_inode(realdn), ceph_vinop(d_inode(realdn)));
		dput(dn);
		*pdn = realdn;
	} else {
		BUG_ON(!ceph_dentry(dn));
		dout("dn %p attached to %p ino %llx.%llx\n",
		     dn, d_inode(dn), ceph_vinop(d_inode(dn)));
	}
	return 0;
}

/*
 * Incorporate results into the local cache.  This is either just
 * one inode, or a directory, dentry, and possibly linked-to inode (e.g.,
 * after a lookup).
 *
 * A reply may contain
 *         a directory inode along with a dentry.
 *  and/or a target inode
 *
 * Called with snap_rwsem (read).
 */
int ceph_fill_trace(struct super_block *sb, struct ceph_mds_request *req)
{
	struct ceph_mds_session *session = req->r_session;
	struct ceph_mds_reply_info_parsed *rinfo = &req->r_reply_info;
	struct inode *in = NULL;
	struct ceph_vino tvino, dvino;
	struct ceph_fs_client *fsc = ceph_sb_to_client(sb);
	int err = 0;

	dout("fill_trace %p is_dentry %d is_target %d\n", req,
	     rinfo->head->is_dentry, rinfo->head->is_target);

	if (!rinfo->head->is_target && !rinfo->head->is_dentry) {
		dout("fill_trace reply is empty!\n");
		if (rinfo->head->result == 0 && req->r_parent)
			ceph_invalidate_dir_request(req);
		return 0;
	}

	if (rinfo->head->is_dentry) {
		struct inode *dir = req->r_parent;

		if (dir) {
			err = ceph_fill_inode(dir, NULL, &rinfo->diri,
					      rinfo->dirfrag, session, -1,
					      &req->r_caps_reservation);
			if (err < 0)
				goto done;
		} else {
			WARN_ON_ONCE(1);
		}

		if (dir && req->r_op == CEPH_MDS_OP_LOOKUPNAME &&
		    test_bit(CEPH_MDS_R_PARENT_LOCKED, &req->r_req_flags) &&
		    !test_bit(CEPH_MDS_R_ABORTED, &req->r_req_flags)) {
			struct qstr dname;
			struct dentry *dn, *parent;

			BUG_ON(!rinfo->head->is_target);
			BUG_ON(req->r_dentry);

			parent = d_find_any_alias(dir);
			BUG_ON(!parent);

			dname.name = rinfo->dname;
			dname.len = rinfo->dname_len;
			dname.hash = full_name_hash(parent, dname.name, dname.len);
			tvino.ino = le64_to_cpu(rinfo->targeti.in->ino);
			tvino.snap = le64_to_cpu(rinfo->targeti.in->snapid);
retry_lookup:
			dn = d_lookup(parent, &dname);
			dout("d_lookup on parent=%p name=%.*s got %p\n",
			     parent, dname.len, dname.name, dn);

			if (!dn) {
				dn = d_alloc(parent, &dname);
				dout("d_alloc %p '%.*s' = %p\n", parent,
				     dname.len, dname.name, dn);
				if (!dn) {
					dput(parent);
					err = -ENOMEM;
					goto done;
				}
				err = 0;
			} else if (d_really_is_positive(dn) &&
				   (ceph_ino(d_inode(dn)) != tvino.ino ||
				    ceph_snap(d_inode(dn)) != tvino.snap)) {
				dout(" dn %p points to wrong inode %p\n",
				     dn, d_inode(dn));
				ceph_dir_clear_ordered(dir);
				d_delete(dn);
				dput(dn);
				goto retry_lookup;
			}

			req->r_dentry = dn;
			dput(parent);
		}
	}

	if (rinfo->head->is_target) {
		tvino.ino = le64_to_cpu(rinfo->targeti.in->ino);
		tvino.snap = le64_to_cpu(rinfo->targeti.in->snapid);

		in = ceph_get_inode(sb, tvino);
		if (IS_ERR(in)) {
			err = PTR_ERR(in);
			goto done;
		}

		err = ceph_fill_inode(in, req->r_locked_page, &rinfo->targeti,
				NULL, session,
				(!test_bit(CEPH_MDS_R_ABORTED, &req->r_req_flags) &&
				 !test_bit(CEPH_MDS_R_ASYNC, &req->r_req_flags) &&
				 rinfo->head->result == 0) ?  req->r_fmode : -1,
				&req->r_caps_reservation);
		if (err < 0) {
			pr_err("ceph_fill_inode badness %p %llx.%llx\n",
				in, ceph_vinop(in));
			if (in->i_state & I_NEW)
				discard_new_inode(in);
			else
				iput(in);
			goto done;
		}
		req->r_target_inode = in;
		if (in->i_state & I_NEW)
			unlock_new_inode(in);
	}

	/*
	 * ignore null lease/binding on snapdir ENOENT, or else we
	 * will have trouble splicing in the virtual snapdir later
	 */
	if (rinfo->head->is_dentry &&
            !test_bit(CEPH_MDS_R_ABORTED, &req->r_req_flags) &&
	    test_bit(CEPH_MDS_R_PARENT_LOCKED, &req->r_req_flags) &&
	    (rinfo->head->is_target || strncmp(req->r_dentry->d_name.name,
					       fsc->mount_options->snapdir_name,
					       req->r_dentry->d_name.len))) {
		/*
		 * lookup link rename   : null -> possibly existing inode
		 * mknod symlink mkdir  : null -> new inode
		 * unlink               : linked -> null
		 */
		struct inode *dir = req->r_parent;
		struct dentry *dn = req->r_dentry;
		bool have_dir_cap, have_lease;

		BUG_ON(!dn);
		BUG_ON(!dir);
		BUG_ON(d_inode(dn->d_parent) != dir);

		dvino.ino = le64_to_cpu(rinfo->diri.in->ino);
		dvino.snap = le64_to_cpu(rinfo->diri.in->snapid);

		BUG_ON(ceph_ino(dir) != dvino.ino);
		BUG_ON(ceph_snap(dir) != dvino.snap);

		/* do we have a lease on the whole dir? */
		have_dir_cap =
			(le32_to_cpu(rinfo->diri.in->cap.caps) &
			 CEPH_CAP_FILE_SHARED);

		/* do we have a dn lease? */
		have_lease = have_dir_cap ||
			le32_to_cpu(rinfo->dlease->duration_ms);
		if (!have_lease)
			dout("fill_trace  no dentry lease or dir cap\n");

		/* rename? */
		if (req->r_old_dentry && req->r_op == CEPH_MDS_OP_RENAME) {
			struct inode *olddir = req->r_old_dentry_dir;
			BUG_ON(!olddir);

			dout(" src %p '%pd' dst %p '%pd'\n",
			     req->r_old_dentry,
			     req->r_old_dentry,
			     dn, dn);
			dout("fill_trace doing d_move %p -> %p\n",
			     req->r_old_dentry, dn);

			/* d_move screws up sibling dentries' offsets */
			ceph_dir_clear_ordered(dir);
			ceph_dir_clear_ordered(olddir);

			d_move(req->r_old_dentry, dn);
			dout(" src %p '%pd' dst %p '%pd'\n",
			     req->r_old_dentry,
			     req->r_old_dentry,
			     dn, dn);

			/* ensure target dentry is invalidated, despite
			   rehashing bug in vfs_rename_dir */
			ceph_invalidate_dentry_lease(dn);

			dout("dn %p gets new offset %lld\n", req->r_old_dentry,
			     ceph_dentry(req->r_old_dentry)->offset);

			/* swap r_dentry and r_old_dentry in case that
			 * splice_dentry() gets called later. This is safe
			 * because no other place will use them */
			req->r_dentry = req->r_old_dentry;
			req->r_old_dentry = dn;
			dn = req->r_dentry;
		}

		/* null dentry? */
		if (!rinfo->head->is_target) {
			dout("fill_trace null dentry\n");
			if (d_really_is_positive(dn)) {
				dout("d_delete %p\n", dn);
				ceph_dir_clear_ordered(dir);
				d_delete(dn);
			} else if (have_lease) {
				if (d_unhashed(dn))
					d_add(dn, NULL);
				update_dentry_lease(dir, dn,
						    rinfo->dlease, session,
						    req->r_request_started);
			}
			goto done;
		}

		/* attach proper inode */
		if (d_really_is_negative(dn)) {
			ceph_dir_clear_ordered(dir);
			ihold(in);
			err = splice_dentry(&req->r_dentry, in);
			if (err < 0)
				goto done;
			dn = req->r_dentry;  /* may have spliced */
		} else if (d_really_is_positive(dn) && d_inode(dn) != in) {
			dout(" %p links to %p %llx.%llx, not %llx.%llx\n",
			     dn, d_inode(dn), ceph_vinop(d_inode(dn)),
			     ceph_vinop(in));
			d_invalidate(dn);
			have_lease = false;
		}

		if (have_lease) {
			update_dentry_lease(dir, dn,
					    rinfo->dlease, session,
					    req->r_request_started);
		}
		dout(" final dn %p\n", dn);
	} else if ((req->r_op == CEPH_MDS_OP_LOOKUPSNAP ||
		    req->r_op == CEPH_MDS_OP_MKSNAP) &&
	           test_bit(CEPH_MDS_R_PARENT_LOCKED, &req->r_req_flags) &&
		   !test_bit(CEPH_MDS_R_ABORTED, &req->r_req_flags)) {
		struct inode *dir = req->r_parent;

		/* fill out a snapdir LOOKUPSNAP dentry */
		BUG_ON(!dir);
		BUG_ON(ceph_snap(dir) != CEPH_SNAPDIR);
		BUG_ON(!req->r_dentry);
		dout(" linking snapped dir %p to dn %p\n", in, req->r_dentry);
		ceph_dir_clear_ordered(dir);
		ihold(in);
		err = splice_dentry(&req->r_dentry, in);
		if (err < 0)
			goto done;
	} else if (rinfo->head->is_dentry && req->r_dentry) {
		/* parent inode is not locked, be carefull */
		struct ceph_vino *ptvino = NULL;
		dvino.ino = le64_to_cpu(rinfo->diri.in->ino);
		dvino.snap = le64_to_cpu(rinfo->diri.in->snapid);
		if (rinfo->head->is_target) {
			tvino.ino = le64_to_cpu(rinfo->targeti.in->ino);
			tvino.snap = le64_to_cpu(rinfo->targeti.in->snapid);
			ptvino = &tvino;
		}
		update_dentry_lease_careful(req->r_dentry, rinfo->dlease,
					    session, req->r_request_started,
					    rinfo->dname, rinfo->dname_len,
					    &dvino, ptvino);
	}
done:
	dout("fill_trace done err=%d\n", err);
	return err;
}

/*
 * Prepopulate our cache with readdir results, leases, etc.
 */
static int readdir_prepopulate_inodes_only(struct ceph_mds_request *req,
					   struct ceph_mds_session *session)
{
	struct ceph_mds_reply_info_parsed *rinfo = &req->r_reply_info;
	int i, err = 0;

	for (i = 0; i < rinfo->dir_nr; i++) {
		struct ceph_mds_reply_dir_entry *rde = rinfo->dir_entries + i;
		struct ceph_vino vino;
		struct inode *in;
		int rc;

		vino.ino = le64_to_cpu(rde->inode.in->ino);
		vino.snap = le64_to_cpu(rde->inode.in->snapid);

		in = ceph_get_inode(req->r_dentry->d_sb, vino);
		if (IS_ERR(in)) {
			err = PTR_ERR(in);
			dout("new_inode badness got %d\n", err);
			continue;
		}
		rc = ceph_fill_inode(in, NULL, &rde->inode, NULL, session,
				     -1, &req->r_caps_reservation);
		if (rc < 0) {
			pr_err("ceph_fill_inode badness on %p got %d\n",
			       in, rc);
			err = rc;
			if (in->i_state & I_NEW) {
				ihold(in);
				discard_new_inode(in);
			}
		} else if (in->i_state & I_NEW) {
			unlock_new_inode(in);
		}

		/* avoid calling iput_final() in mds dispatch threads */
		ceph_async_iput(in);
	}

	return err;
}

void ceph_readdir_cache_release(struct ceph_readdir_cache_control *ctl)
{
	if (ctl->page) {
		kunmap(ctl->page);
		put_page(ctl->page);
		ctl->page = NULL;
	}
}

static int fill_readdir_cache(struct inode *dir, struct dentry *dn,
			      struct ceph_readdir_cache_control *ctl,
			      struct ceph_mds_request *req)
{
	struct ceph_inode_info *ci = ceph_inode(dir);
	unsigned nsize = PAGE_SIZE / sizeof(struct dentry*);
	unsigned idx = ctl->index % nsize;
	pgoff_t pgoff = ctl->index / nsize;

	if (!ctl->page || pgoff != page_index(ctl->page)) {
		ceph_readdir_cache_release(ctl);
		if (idx == 0)
			ctl->page = grab_cache_page(&dir->i_data, pgoff);
		else
			ctl->page = find_lock_page(&dir->i_data, pgoff);
		if (!ctl->page) {
			ctl->index = -1;
			return idx == 0 ? -ENOMEM : 0;
		}
		/* reading/filling the cache are serialized by
		 * i_mutex, no need to use page lock */
		unlock_page(ctl->page);
		ctl->dentries = kmap(ctl->page);
		if (idx == 0)
			memset(ctl->dentries, 0, PAGE_SIZE);
	}

	if (req->r_dir_release_cnt == atomic64_read(&ci->i_release_count) &&
	    req->r_dir_ordered_cnt == atomic64_read(&ci->i_ordered_count)) {
		dout("readdir cache dn %p idx %d\n", dn, ctl->index);
		ctl->dentries[idx] = dn;
		ctl->index++;
	} else {
		dout("disable readdir cache\n");
		ctl->index = -1;
	}
	return 0;
}

int ceph_readdir_prepopulate(struct ceph_mds_request *req,
			     struct ceph_mds_session *session)
{
	struct dentry *parent = req->r_dentry;
	struct ceph_inode_info *ci = ceph_inode(d_inode(parent));
	struct ceph_mds_reply_info_parsed *rinfo = &req->r_reply_info;
	struct qstr dname;
	struct dentry *dn;
	struct inode *in;
	int err = 0, skipped = 0, ret, i;
	struct ceph_mds_request_head *rhead = req->r_request->front.iov_base;
	u32 frag = le32_to_cpu(rhead->args.readdir.frag);
	u32 last_hash = 0;
	u32 fpos_offset;
	struct ceph_readdir_cache_control cache_ctl = {};

	if (test_bit(CEPH_MDS_R_ABORTED, &req->r_req_flags))
		return readdir_prepopulate_inodes_only(req, session);

	if (rinfo->hash_order) {
		if (req->r_path2) {
			last_hash = ceph_str_hash(ci->i_dir_layout.dl_dir_hash,
						  req->r_path2,
						  strlen(req->r_path2));
			last_hash = ceph_frag_value(last_hash);
		} else if (rinfo->offset_hash) {
			/* mds understands offset_hash */
			WARN_ON_ONCE(req->r_readdir_offset != 2);
			last_hash = le32_to_cpu(rhead->args.readdir.offset_hash);
		}
	}

	if (rinfo->dir_dir &&
	    le32_to_cpu(rinfo->dir_dir->frag) != frag) {
		dout("readdir_prepopulate got new frag %x -> %x\n",
		     frag, le32_to_cpu(rinfo->dir_dir->frag));
		frag = le32_to_cpu(rinfo->dir_dir->frag);
		if (!rinfo->hash_order)
			req->r_readdir_offset = 2;
	}

	if (le32_to_cpu(rinfo->head->op) == CEPH_MDS_OP_LSSNAP) {
		dout("readdir_prepopulate %d items under SNAPDIR dn %p\n",
		     rinfo->dir_nr, parent);
	} else {
		dout("readdir_prepopulate %d items under dn %p\n",
		     rinfo->dir_nr, parent);
		if (rinfo->dir_dir)
			ceph_fill_dirfrag(d_inode(parent), rinfo->dir_dir);

		if (ceph_frag_is_leftmost(frag) &&
		    req->r_readdir_offset == 2 &&
		    !(rinfo->hash_order && last_hash)) {
			/* note dir version at start of readdir so we can
			 * tell if any dentries get dropped */
			req->r_dir_release_cnt =
				atomic64_read(&ci->i_release_count);
			req->r_dir_ordered_cnt =
				atomic64_read(&ci->i_ordered_count);
			req->r_readdir_cache_idx = 0;
		}
	}

	cache_ctl.index = req->r_readdir_cache_idx;
	fpos_offset = req->r_readdir_offset;

	/* FIXME: release caps/leases if error occurs */
	for (i = 0; i < rinfo->dir_nr; i++) {
		struct ceph_mds_reply_dir_entry *rde = rinfo->dir_entries + i;
		struct ceph_vino tvino;

		dname.name = rde->name;
		dname.len = rde->name_len;
		dname.hash = full_name_hash(parent, dname.name, dname.len);

		tvino.ino = le64_to_cpu(rde->inode.in->ino);
		tvino.snap = le64_to_cpu(rde->inode.in->snapid);

		if (rinfo->hash_order) {
			u32 hash = ceph_str_hash(ci->i_dir_layout.dl_dir_hash,
						 rde->name, rde->name_len);
			hash = ceph_frag_value(hash);
			if (hash != last_hash)
				fpos_offset = 2;
			last_hash = hash;
			rde->offset = ceph_make_fpos(hash, fpos_offset++, true);
		} else {
			rde->offset = ceph_make_fpos(frag, fpos_offset++, false);
		}

retry_lookup:
		dn = d_lookup(parent, &dname);
		dout("d_lookup on parent=%p name=%.*s got %p\n",
		     parent, dname.len, dname.name, dn);

		if (!dn) {
			dn = d_alloc(parent, &dname);
			dout("d_alloc %p '%.*s' = %p\n", parent,
			     dname.len, dname.name, dn);
			if (!dn) {
				dout("d_alloc badness\n");
				err = -ENOMEM;
				goto out;
			}
		} else if (d_really_is_positive(dn) &&
			   (ceph_ino(d_inode(dn)) != tvino.ino ||
			    ceph_snap(d_inode(dn)) != tvino.snap)) {
			struct ceph_dentry_info *di = ceph_dentry(dn);
			dout(" dn %p points to wrong inode %p\n",
			     dn, d_inode(dn));

			spin_lock(&dn->d_lock);
			if (di->offset > 0 &&
			    di->lease_shared_gen ==
			    atomic_read(&ci->i_shared_gen)) {
				__ceph_dir_clear_ordered(ci);
				di->offset = 0;
			}
			spin_unlock(&dn->d_lock);

			d_delete(dn);
			dput(dn);
			goto retry_lookup;
		}

		/* inode */
		if (d_really_is_positive(dn)) {
			in = d_inode(dn);
		} else {
			in = ceph_get_inode(parent->d_sb, tvino);
			if (IS_ERR(in)) {
				dout("new_inode badness\n");
				d_drop(dn);
				dput(dn);
				err = PTR_ERR(in);
				goto out;
			}
		}

		ret = ceph_fill_inode(in, NULL, &rde->inode, NULL, session,
				      -1, &req->r_caps_reservation);
		if (ret < 0) {
			pr_err("ceph_fill_inode badness on %p\n", in);
			if (d_really_is_negative(dn)) {
				/* avoid calling iput_final() in mds
				 * dispatch threads */
				if (in->i_state & I_NEW) {
					ihold(in);
					discard_new_inode(in);
				}
				ceph_async_iput(in);
			}
			d_drop(dn);
			err = ret;
			goto next_item;
		}
		if (in->i_state & I_NEW)
			unlock_new_inode(in);

		if (d_really_is_negative(dn)) {
			if (ceph_security_xattr_deadlock(in)) {
				dout(" skip splicing dn %p to inode %p"
				     " (security xattr deadlock)\n", dn, in);
				ceph_async_iput(in);
				skipped++;
				goto next_item;
			}

			err = splice_dentry(&dn, in);
			if (err < 0)
				goto next_item;
		}

		ceph_dentry(dn)->offset = rde->offset;

		update_dentry_lease(d_inode(parent), dn,
				    rde->lease, req->r_session,
				    req->r_request_started);

		if (err == 0 && skipped == 0 && cache_ctl.index >= 0) {
			ret = fill_readdir_cache(d_inode(parent), dn,
						 &cache_ctl, req);
			if (ret < 0)
				err = ret;
		}
next_item:
		dput(dn);
	}
out:
	if (err == 0 && skipped == 0) {
		set_bit(CEPH_MDS_R_DID_PREPOPULATE, &req->r_req_flags);
		req->r_readdir_cache_idx = cache_ctl.index;
	}
	ceph_readdir_cache_release(&cache_ctl);
	dout("readdir_prepopulate done\n");
	return err;
}

bool ceph_inode_set_size(struct inode *inode, loff_t size)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	bool ret;

	spin_lock(&ci->i_ceph_lock);
	dout("set_size %p %llu -> %llu\n", inode, inode->i_size, size);
	i_size_write(inode, size);
	inode->i_blocks = calc_inode_blocks(size);

	ret = __ceph_should_report_size(ci);

	spin_unlock(&ci->i_ceph_lock);
	return ret;
}

/*
 * Put reference to inode, but avoid calling iput_final() in current thread.
 * iput_final() may wait for reahahead pages. The wait can cause deadlock in
 * some contexts.
 */
void ceph_async_iput(struct inode *inode)
{
	if (!inode)
		return;
	for (;;) {
		if (atomic_add_unless(&inode->i_count, -1, 1))
			break;
		if (queue_work(ceph_inode_to_client(inode)->inode_wq,
			       &ceph_inode(inode)->i_work))
			break;
		/* queue work failed, i_count must be at least 2 */
	}
}

/*
 * Write back inode data in a worker thread.  (This can't be done
 * in the message handler context.)
 */
void ceph_queue_writeback(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	set_bit(CEPH_I_WORK_WRITEBACK, &ci->i_work_mask);

	ihold(inode);
	if (queue_work(ceph_inode_to_client(inode)->inode_wq,
		       &ci->i_work)) {
		dout("ceph_queue_writeback %p\n", inode);
	} else {
		dout("ceph_queue_writeback %p already queued, mask=%lx\n",
		     inode, ci->i_work_mask);
		iput(inode);
	}
}

/*
 * queue an async invalidation
 */
void ceph_queue_invalidate(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	set_bit(CEPH_I_WORK_INVALIDATE_PAGES, &ci->i_work_mask);

	ihold(inode);
	if (queue_work(ceph_inode_to_client(inode)->inode_wq,
		       &ceph_inode(inode)->i_work)) {
		dout("ceph_queue_invalidate %p\n", inode);
	} else {
		dout("ceph_queue_invalidate %p already queued, mask=%lx\n",
		     inode, ci->i_work_mask);
		iput(inode);
	}
}

/*
 * Queue an async vmtruncate.  If we fail to queue work, we will handle
 * the truncation the next time we call __ceph_do_pending_vmtruncate.
 */
void ceph_queue_vmtruncate(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	set_bit(CEPH_I_WORK_VMTRUNCATE, &ci->i_work_mask);

	ihold(inode);
	if (queue_work(ceph_inode_to_client(inode)->inode_wq,
		       &ci->i_work)) {
		dout("ceph_queue_vmtruncate %p\n", inode);
	} else {
		dout("ceph_queue_vmtruncate %p already queued, mask=%lx\n",
		     inode, ci->i_work_mask);
		iput(inode);
	}
}

static void ceph_do_invalidate_pages(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);
	u32 orig_gen;
	int check = 0;

	mutex_lock(&ci->i_truncate_mutex);

	if (READ_ONCE(fsc->mount_state) == CEPH_MOUNT_SHUTDOWN) {
		pr_warn_ratelimited("invalidate_pages %p %lld forced umount\n",
				    inode, ceph_ino(inode));
		mapping_set_error(inode->i_mapping, -EIO);
		truncate_pagecache(inode, 0);
		mutex_unlock(&ci->i_truncate_mutex);
		goto out;
	}

	spin_lock(&ci->i_ceph_lock);
	dout("invalidate_pages %p gen %d revoking %d\n", inode,
	     ci->i_rdcache_gen, ci->i_rdcache_revoking);
	if (ci->i_rdcache_revoking != ci->i_rdcache_gen) {
		if (__ceph_caps_revoking_other(ci, NULL, CEPH_CAP_FILE_CACHE))
			check = 1;
		spin_unlock(&ci->i_ceph_lock);
		mutex_unlock(&ci->i_truncate_mutex);
		goto out;
	}
	orig_gen = ci->i_rdcache_gen;
	spin_unlock(&ci->i_ceph_lock);

	ceph_fscache_invalidate(inode);
	if (invalidate_inode_pages2(inode->i_mapping) < 0) {
		pr_err("invalidate_pages %p fails\n", inode);
	}

	spin_lock(&ci->i_ceph_lock);
	if (orig_gen == ci->i_rdcache_gen &&
	    orig_gen == ci->i_rdcache_revoking) {
		dout("invalidate_pages %p gen %d successful\n", inode,
		     ci->i_rdcache_gen);
		ci->i_rdcache_revoking--;
		check = 1;
	} else {
		dout("invalidate_pages %p gen %d raced, now %d revoking %d\n",
		     inode, orig_gen, ci->i_rdcache_gen,
		     ci->i_rdcache_revoking);
		if (__ceph_caps_revoking_other(ci, NULL, CEPH_CAP_FILE_CACHE))
			check = 1;
	}
	spin_unlock(&ci->i_ceph_lock);
	mutex_unlock(&ci->i_truncate_mutex);
out:
	if (check)
		ceph_check_caps(ci, 0, NULL);
}

/*
 * Make sure any pending truncation is applied before doing anything
 * that may depend on it.
 */
void __ceph_do_pending_vmtruncate(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	u64 to;
	int wrbuffer_refs, finish = 0;

	mutex_lock(&ci->i_truncate_mutex);
retry:
	spin_lock(&ci->i_ceph_lock);
	if (ci->i_truncate_pending == 0) {
		dout("__do_pending_vmtruncate %p none pending\n", inode);
		spin_unlock(&ci->i_ceph_lock);
		mutex_unlock(&ci->i_truncate_mutex);
		return;
	}

	/*
	 * make sure any dirty snapped pages are flushed before we
	 * possibly truncate them.. so write AND block!
	 */
	if (ci->i_wrbuffer_ref_head < ci->i_wrbuffer_ref) {
		spin_unlock(&ci->i_ceph_lock);
		dout("__do_pending_vmtruncate %p flushing snaps first\n",
		     inode);
		filemap_write_and_wait_range(&inode->i_data, 0,
					     inode->i_sb->s_maxbytes);
		goto retry;
	}

	/* there should be no reader or writer */
	WARN_ON_ONCE(ci->i_rd_ref || ci->i_wr_ref);

	to = ci->i_truncate_size;
	wrbuffer_refs = ci->i_wrbuffer_ref;
	dout("__do_pending_vmtruncate %p (%d) to %lld\n", inode,
	     ci->i_truncate_pending, to);
	spin_unlock(&ci->i_ceph_lock);

	truncate_pagecache(inode, to);

	spin_lock(&ci->i_ceph_lock);
	if (to == ci->i_truncate_size) {
		ci->i_truncate_pending = 0;
		finish = 1;
	}
	spin_unlock(&ci->i_ceph_lock);
	if (!finish)
		goto retry;

	mutex_unlock(&ci->i_truncate_mutex);

	if (wrbuffer_refs == 0)
		ceph_check_caps(ci, 0, NULL);

	wake_up_all(&ci->i_cap_wq);
}

static void ceph_inode_work(struct work_struct *work)
{
	struct ceph_inode_info *ci = container_of(work, struct ceph_inode_info,
						 i_work);
	struct inode *inode = &ci->vfs_inode;

	if (test_and_clear_bit(CEPH_I_WORK_WRITEBACK, &ci->i_work_mask)) {
		dout("writeback %p\n", inode);
		filemap_fdatawrite(&inode->i_data);
	}
	if (test_and_clear_bit(CEPH_I_WORK_INVALIDATE_PAGES, &ci->i_work_mask))
		ceph_do_invalidate_pages(inode);

	if (test_and_clear_bit(CEPH_I_WORK_VMTRUNCATE, &ci->i_work_mask))
		__ceph_do_pending_vmtruncate(inode);

	iput(inode);
}

/*
 * symlinks
 */
static const struct inode_operations ceph_symlink_iops = {
	.get_link = simple_get_link,
	.setattr = ceph_setattr,
	.getattr = ceph_getattr,
	.listxattr = ceph_listxattr,
};

int __ceph_setattr(struct inode *inode, struct iattr *attr)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	unsigned int ia_valid = attr->ia_valid;
	struct ceph_mds_request *req;
	struct ceph_mds_client *mdsc = ceph_sb_to_client(inode->i_sb)->mdsc;
	struct ceph_cap_flush *prealloc_cf;
	int issued;
	int release = 0, dirtied = 0;
	int mask = 0;
	int err = 0;
	int inode_dirty_flags = 0;
	bool lock_snap_rwsem = false;

	prealloc_cf = ceph_alloc_cap_flush();
	if (!prealloc_cf)
		return -ENOMEM;

	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_SETATTR,
				       USE_AUTH_MDS);
	if (IS_ERR(req)) {
		ceph_free_cap_flush(prealloc_cf);
		return PTR_ERR(req);
	}

	spin_lock(&ci->i_ceph_lock);
	issued = __ceph_caps_issued(ci, NULL);

	if (!ci->i_head_snapc &&
	    (issued & (CEPH_CAP_ANY_EXCL | CEPH_CAP_FILE_WR))) {
		lock_snap_rwsem = true;
		if (!down_read_trylock(&mdsc->snap_rwsem)) {
			spin_unlock(&ci->i_ceph_lock);
			down_read(&mdsc->snap_rwsem);
			spin_lock(&ci->i_ceph_lock);
			issued = __ceph_caps_issued(ci, NULL);
		}
	}

	dout("setattr %p issued %s\n", inode, ceph_cap_string(issued));

	if (ia_valid & ATTR_UID) {
		dout("setattr %p uid %d -> %d\n", inode,
		     from_kuid(&init_user_ns, inode->i_uid),
		     from_kuid(&init_user_ns, attr->ia_uid));
		if (issued & CEPH_CAP_AUTH_EXCL) {
			inode->i_uid = attr->ia_uid;
			dirtied |= CEPH_CAP_AUTH_EXCL;
		} else if ((issued & CEPH_CAP_AUTH_SHARED) == 0 ||
			   !uid_eq(attr->ia_uid, inode->i_uid)) {
			req->r_args.setattr.uid = cpu_to_le32(
				from_kuid(&init_user_ns, attr->ia_uid));
			mask |= CEPH_SETATTR_UID;
			release |= CEPH_CAP_AUTH_SHARED;
		}
	}
	if (ia_valid & ATTR_GID) {
		dout("setattr %p gid %d -> %d\n", inode,
		     from_kgid(&init_user_ns, inode->i_gid),
		     from_kgid(&init_user_ns, attr->ia_gid));
		if (issued & CEPH_CAP_AUTH_EXCL) {
			inode->i_gid = attr->ia_gid;
			dirtied |= CEPH_CAP_AUTH_EXCL;
		} else if ((issued & CEPH_CAP_AUTH_SHARED) == 0 ||
			   !gid_eq(attr->ia_gid, inode->i_gid)) {
			req->r_args.setattr.gid = cpu_to_le32(
				from_kgid(&init_user_ns, attr->ia_gid));
			mask |= CEPH_SETATTR_GID;
			release |= CEPH_CAP_AUTH_SHARED;
		}
	}
	if (ia_valid & ATTR_MODE) {
		dout("setattr %p mode 0%o -> 0%o\n", inode, inode->i_mode,
		     attr->ia_mode);
		if (issued & CEPH_CAP_AUTH_EXCL) {
			inode->i_mode = attr->ia_mode;
			dirtied |= CEPH_CAP_AUTH_EXCL;
		} else if ((issued & CEPH_CAP_AUTH_SHARED) == 0 ||
			   attr->ia_mode != inode->i_mode) {
			inode->i_mode = attr->ia_mode;
			req->r_args.setattr.mode = cpu_to_le32(attr->ia_mode);
			mask |= CEPH_SETATTR_MODE;
			release |= CEPH_CAP_AUTH_SHARED;
		}
	}

	if (ia_valid & ATTR_ATIME) {
		dout("setattr %p atime %lld.%ld -> %lld.%ld\n", inode,
		     inode->i_atime.tv_sec, inode->i_atime.tv_nsec,
		     attr->ia_atime.tv_sec, attr->ia_atime.tv_nsec);
		if (issued & CEPH_CAP_FILE_EXCL) {
			ci->i_time_warp_seq++;
			inode->i_atime = attr->ia_atime;
			dirtied |= CEPH_CAP_FILE_EXCL;
		} else if ((issued & CEPH_CAP_FILE_WR) &&
			   timespec64_compare(&inode->i_atime,
					    &attr->ia_atime) < 0) {
			inode->i_atime = attr->ia_atime;
			dirtied |= CEPH_CAP_FILE_WR;
		} else if ((issued & CEPH_CAP_FILE_SHARED) == 0 ||
			   !timespec64_equal(&inode->i_atime, &attr->ia_atime)) {
			ceph_encode_timespec64(&req->r_args.setattr.atime,
					       &attr->ia_atime);
			mask |= CEPH_SETATTR_ATIME;
			release |= CEPH_CAP_FILE_SHARED |
				   CEPH_CAP_FILE_RD | CEPH_CAP_FILE_WR;
		}
	}
	if (ia_valid & ATTR_SIZE) {
		dout("setattr %p size %lld -> %lld\n", inode,
		     inode->i_size, attr->ia_size);
		if ((issued & CEPH_CAP_FILE_EXCL) &&
		    attr->ia_size > inode->i_size) {
			i_size_write(inode, attr->ia_size);
			inode->i_blocks = calc_inode_blocks(attr->ia_size);
			ci->i_reported_size = attr->ia_size;
			dirtied |= CEPH_CAP_FILE_EXCL;
			ia_valid |= ATTR_MTIME;
		} else if ((issued & CEPH_CAP_FILE_SHARED) == 0 ||
			   attr->ia_size != inode->i_size) {
			req->r_args.setattr.size = cpu_to_le64(attr->ia_size);
			req->r_args.setattr.old_size =
				cpu_to_le64(inode->i_size);
			mask |= CEPH_SETATTR_SIZE;
			release |= CEPH_CAP_FILE_SHARED | CEPH_CAP_FILE_EXCL |
				   CEPH_CAP_FILE_RD | CEPH_CAP_FILE_WR;
		}
	}
	if (ia_valid & ATTR_MTIME) {
		dout("setattr %p mtime %lld.%ld -> %lld.%ld\n", inode,
		     inode->i_mtime.tv_sec, inode->i_mtime.tv_nsec,
		     attr->ia_mtime.tv_sec, attr->ia_mtime.tv_nsec);
		if (issued & CEPH_CAP_FILE_EXCL) {
			ci->i_time_warp_seq++;
			inode->i_mtime = attr->ia_mtime;
			dirtied |= CEPH_CAP_FILE_EXCL;
		} else if ((issued & CEPH_CAP_FILE_WR) &&
			   timespec64_compare(&inode->i_mtime,
					    &attr->ia_mtime) < 0) {
			inode->i_mtime = attr->ia_mtime;
			dirtied |= CEPH_CAP_FILE_WR;
		} else if ((issued & CEPH_CAP_FILE_SHARED) == 0 ||
			   !timespec64_equal(&inode->i_mtime, &attr->ia_mtime)) {
			ceph_encode_timespec64(&req->r_args.setattr.mtime,
					       &attr->ia_mtime);
			mask |= CEPH_SETATTR_MTIME;
			release |= CEPH_CAP_FILE_SHARED |
				   CEPH_CAP_FILE_RD | CEPH_CAP_FILE_WR;
		}
	}

	/* these do nothing */
	if (ia_valid & ATTR_CTIME) {
		bool only = (ia_valid & (ATTR_SIZE|ATTR_MTIME|ATTR_ATIME|
					 ATTR_MODE|ATTR_UID|ATTR_GID)) == 0;
		dout("setattr %p ctime %lld.%ld -> %lld.%ld (%s)\n", inode,
		     inode->i_ctime.tv_sec, inode->i_ctime.tv_nsec,
		     attr->ia_ctime.tv_sec, attr->ia_ctime.tv_nsec,
		     only ? "ctime only" : "ignored");
		if (only) {
			/*
			 * if kernel wants to dirty ctime but nothing else,
			 * we need to choose a cap to dirty under, or do
			 * a almost-no-op setattr
			 */
			if (issued & CEPH_CAP_AUTH_EXCL)
				dirtied |= CEPH_CAP_AUTH_EXCL;
			else if (issued & CEPH_CAP_FILE_EXCL)
				dirtied |= CEPH_CAP_FILE_EXCL;
			else if (issued & CEPH_CAP_XATTR_EXCL)
				dirtied |= CEPH_CAP_XATTR_EXCL;
			else
				mask |= CEPH_SETATTR_CTIME;
		}
	}
	if (ia_valid & ATTR_FILE)
		dout("setattr %p ATTR_FILE ... hrm!\n", inode);

	if (dirtied) {
		inode_dirty_flags = __ceph_mark_dirty_caps(ci, dirtied,
							   &prealloc_cf);
		inode->i_ctime = attr->ia_ctime;
	}

	release &= issued;
	spin_unlock(&ci->i_ceph_lock);
	if (lock_snap_rwsem)
		up_read(&mdsc->snap_rwsem);

	if (inode_dirty_flags)
		__mark_inode_dirty(inode, inode_dirty_flags);


	if (mask) {
		req->r_inode = inode;
		ihold(inode);
		req->r_inode_drop = release;
		req->r_args.setattr.mask = cpu_to_le32(mask);
		req->r_num_caps = 1;
		req->r_stamp = attr->ia_ctime;
		err = ceph_mdsc_do_request(mdsc, NULL, req);
	}
	dout("setattr %p result=%d (%s locally, %d remote)\n", inode, err,
	     ceph_cap_string(dirtied), mask);

	ceph_mdsc_put_request(req);
	ceph_free_cap_flush(prealloc_cf);

	if (err >= 0 && (mask & CEPH_SETATTR_SIZE))
		__ceph_do_pending_vmtruncate(inode);

	return err;
}

/*
 * setattr
 */
int ceph_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct ceph_fs_client *fsc = ceph_inode_to_client(inode);
	int err;

	if (ceph_snap(inode) != CEPH_NOSNAP)
		return -EROFS;

	err = setattr_prepare(dentry, attr);
	if (err != 0)
		return err;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size > max(inode->i_size, fsc->max_file_size))
		return -EFBIG;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    ceph_quota_is_max_bytes_exceeded(inode, attr->ia_size))
		return -EDQUOT;

	err = __ceph_setattr(inode, attr);

	if (err >= 0 && (attr->ia_valid & ATTR_MODE))
		err = posix_acl_chmod(inode, attr->ia_mode);

	return err;
}

/*
 * Verify that we have a lease on the given mask.  If not,
 * do a getattr against an mds.
 */
int __ceph_do_getattr(struct inode *inode, struct page *locked_page,
		      int mask, bool force)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(inode->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	int mode;
	int err;

	if (ceph_snap(inode) == CEPH_SNAPDIR) {
		dout("do_getattr inode %p SNAPDIR\n", inode);
		return 0;
	}

	dout("do_getattr inode %p mask %s mode 0%o\n",
	     inode, ceph_cap_string(mask), inode->i_mode);
	if (!force && ceph_caps_issued_mask_metric(ceph_inode(inode), mask, 1))
			return 0;

	mode = (mask & CEPH_STAT_RSTAT) ? USE_AUTH_MDS : USE_ANY_MDS;
	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_GETATTR, mode);
	if (IS_ERR(req))
		return PTR_ERR(req);
	req->r_inode = inode;
	ihold(inode);
	req->r_num_caps = 1;
	req->r_args.getattr.mask = cpu_to_le32(mask);
	req->r_locked_page = locked_page;
	err = ceph_mdsc_do_request(mdsc, NULL, req);
	if (locked_page && err == 0) {
		u64 inline_version = req->r_reply_info.targeti.inline_version;
		if (inline_version == 0) {
			/* the reply is supposed to contain inline data */
			err = -EINVAL;
		} else if (inline_version == CEPH_INLINE_NONE) {
			err = -ENODATA;
		} else {
			err = req->r_reply_info.targeti.inline_len;
		}
	}
	ceph_mdsc_put_request(req);
	dout("do_getattr result=%d\n", err);
	return err;
}


/*
 * Check inode permissions.  We verify we have a valid value for
 * the AUTH cap, then call the generic handler.
 */
int ceph_permission(struct inode *inode, int mask)
{
	int err;

	if (mask & MAY_NOT_BLOCK)
		return -ECHILD;

	err = ceph_do_getattr(inode, CEPH_CAP_AUTH_SHARED, false);

	if (!err)
		err = generic_permission(inode, mask);
	return err;
}

/* Craft a mask of needed caps given a set of requested statx attrs. */
static int statx_to_caps(u32 want)
{
	int mask = 0;

	if (want & (STATX_MODE|STATX_UID|STATX_GID|STATX_CTIME|STATX_BTIME))
		mask |= CEPH_CAP_AUTH_SHARED;

	if (want & (STATX_NLINK|STATX_CTIME))
		mask |= CEPH_CAP_LINK_SHARED;

	if (want & (STATX_ATIME|STATX_MTIME|STATX_CTIME|STATX_SIZE|
		    STATX_BLOCKS))
		mask |= CEPH_CAP_FILE_SHARED;

	if (want & (STATX_CTIME))
		mask |= CEPH_CAP_XATTR_SHARED;

	return mask;
}

/*
 * Get all the attributes. If we have sufficient caps for the requested attrs,
 * then we can avoid talking to the MDS at all.
 */
int ceph_getattr(const struct path *path, struct kstat *stat,
		 u32 request_mask, unsigned int flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct ceph_inode_info *ci = ceph_inode(inode);
	u32 valid_mask = STATX_BASIC_STATS;
	int err = 0;

	/* Skip the getattr altogether if we're asked not to sync */
	if (!(flags & AT_STATX_DONT_SYNC)) {
		err = ceph_do_getattr(inode, statx_to_caps(request_mask),
				      flags & AT_STATX_FORCE_SYNC);
		if (err)
			return err;
	}

	generic_fillattr(inode, stat);
	stat->ino = ceph_present_inode(inode);

	/*
	 * btime on newly-allocated inodes is 0, so if this is still set to
	 * that, then assume that it's not valid.
	 */
	if (ci->i_btime.tv_sec || ci->i_btime.tv_nsec) {
		stat->btime = ci->i_btime;
		valid_mask |= STATX_BTIME;
	}

	if (ceph_snap(inode) == CEPH_NOSNAP)
		stat->dev = inode->i_sb->s_dev;
	else
		stat->dev = ci->i_snapid_map ? ci->i_snapid_map->dev : 0;

	if (S_ISDIR(inode->i_mode)) {
		if (ceph_test_mount_opt(ceph_sb_to_client(inode->i_sb),
					RBYTES))
			stat->size = ci->i_rbytes;
		else
			stat->size = ci->i_files + ci->i_subdirs;
		stat->blocks = 0;
		stat->blksize = 65536;
		/*
		 * Some applications rely on the number of st_nlink
		 * value on directories to be either 0 (if unlinked)
		 * or 2 + number of subdirectories.
		 */
		if (stat->nlink == 1)
			/* '.' + '..' + subdirs */
			stat->nlink = 1 + 1 + ci->i_subdirs;
	}

	stat->result_mask = request_mask & valid_mask;
	return err;
}
