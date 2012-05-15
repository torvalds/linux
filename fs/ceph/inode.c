#include <linux/ceph/ceph_debug.h>

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/namei.h>
#include <linux/writeback.h>
#include <linux/vmalloc.h>

#include "super.h"
#include "mds_client.h"
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

static void ceph_invalidate_work(struct work_struct *work);
static void ceph_writeback_work(struct work_struct *work);
static void ceph_vmtruncate_work(struct work_struct *work);

/*
 * find or create an inode, given the ceph ino number
 */
static int ceph_set_ino_cb(struct inode *inode, void *data)
{
	ceph_inode(inode)->i_vino = *(struct ceph_vino *)data;
	inode->i_ino = ceph_vino_to_ino(*(struct ceph_vino *)data);
	return 0;
}

struct inode *ceph_get_inode(struct super_block *sb, struct ceph_vino vino)
{
	struct inode *inode;
	ino_t t = ceph_vino_to_ino(vino);

	inode = iget5_locked(sb, t, ceph_ino_compare, ceph_set_ino_cb, &vino);
	if (inode == NULL)
		return ERR_PTR(-ENOMEM);
	if (inode->i_state & I_NEW) {
		dout("get_inode created new inode %p %llx.%llx ino %llx\n",
		     inode, ceph_vinop(inode), (u64)inode->i_ino);
		unlock_new_inode(inode);
	}

	dout("get_inode on %lu=%llx.%llx got %p\n", inode->i_ino, vino.ino,
	     vino.snap, inode);
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
	inode->i_op = &ceph_dir_iops;
	inode->i_fop = &ceph_dir_fops;
	ci->i_snap_caps = CEPH_CAP_PIN; /* so we can open */
	ci->i_rbytes = 0;
	return inode;
}

const struct inode_operations ceph_file_iops = {
	.permission = ceph_permission,
	.setattr = ceph_setattr,
	.getattr = ceph_getattr,
	.setxattr = ceph_setxattr,
	.getxattr = ceph_getxattr,
	.listxattr = ceph_listxattr,
	.removexattr = ceph_removexattr,
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
	if (!frag) {
		pr_err("__get_or_create_frag ENOMEM on %p %llx.%llx "
		       "frag %x\n", &ci->vfs_inode,
		       ceph_vinop(&ci->vfs_inode), f);
		return ERR_PTR(-ENOMEM);
	}
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
u32 ceph_choose_frag(struct ceph_inode_info *ci, u32 v,
		     struct ceph_inode_frag *pfrag,
		     int *found)
{
	u32 t = ceph_frag_make(0, 0);
	struct ceph_inode_frag *frag;
	unsigned nway, i;
	u32 n;

	if (found)
		*found = 0;

	mutex_lock(&ci->i_fragtree_mutex);
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

	mutex_unlock(&ci->i_fragtree_mutex);
	return t;
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
	int i;
	int err = 0;

	mutex_lock(&ci->i_fragtree_mutex);
	if (ndist == 0) {
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
	ci->i_time_warp_seq = 0;
	ci->i_ceph_flags = 0;
	ci->i_release_count = 0;
	ci->i_symlink = NULL;

	memset(&ci->i_dir_layout, 0, sizeof(ci->i_dir_layout));

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
	ci->i_cap_flush_seq = 0;
	ci->i_cap_flush_last_tid = 0;
	memset(&ci->i_cap_flush_tid, 0, sizeof(ci->i_cap_flush_tid));
	init_waitqueue_head(&ci->i_cap_wq);
	ci->i_hold_caps_min = 0;
	ci->i_hold_caps_max = 0;
	INIT_LIST_HEAD(&ci->i_cap_delay_list);
	ci->i_cap_exporting_mds = 0;
	ci->i_cap_exporting_mseq = 0;
	ci->i_cap_exporting_issued = 0;
	INIT_LIST_HEAD(&ci->i_cap_snaps);
	ci->i_head_snapc = NULL;
	ci->i_snap_caps = 0;

	for (i = 0; i < CEPH_FILE_MODE_NUM; i++)
		ci->i_nr_by_mode[i] = 0;

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
	ci->i_wrbuffer_ref = 0;
	ci->i_wrbuffer_ref_head = 0;
	ci->i_shared_gen = 0;
	ci->i_rdcache_gen = 0;
	ci->i_rdcache_revoking = 0;

	INIT_LIST_HEAD(&ci->i_unsafe_writes);
	INIT_LIST_HEAD(&ci->i_unsafe_dirops);
	spin_lock_init(&ci->i_unsafe_lock);

	ci->i_snap_realm = NULL;
	INIT_LIST_HEAD(&ci->i_snap_realm_item);
	INIT_LIST_HEAD(&ci->i_snap_flush_item);

	INIT_WORK(&ci->i_wb_work, ceph_writeback_work);
	INIT_WORK(&ci->i_pg_inv_work, ceph_invalidate_work);

	INIT_WORK(&ci->i_vmtruncate_work, ceph_vmtruncate_work);

	return &ci->vfs_inode;
}

static void ceph_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	struct ceph_inode_info *ci = ceph_inode(inode);

	kmem_cache_free(ceph_inode_cachep, ci);
}

void ceph_destroy_inode(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_inode_frag *frag;
	struct rb_node *n;

	dout("destroy_inode %p ino %llx.%llx\n", inode, ceph_vinop(inode));

	ceph_queue_caps_release(inode);

	/*
	 * we may still have a snap_realm reference if there are stray
	 * caps in i_cap_exporting_issued or i_snap_caps.
	 */
	if (ci->i_snap_realm) {
		struct ceph_mds_client *mdsc =
			ceph_sb_to_client(ci->vfs_inode.i_sb)->mdsc;
		struct ceph_snap_realm *realm = ci->i_snap_realm;

		dout(" dropping residual ref to snap realm %p\n", realm);
		spin_lock(&realm->inodes_with_caps_lock);
		list_del_init(&ci->i_snap_realm_item);
		spin_unlock(&realm->inodes_with_caps_lock);
		ceph_put_snap_realm(mdsc, realm);
	}

	kfree(ci->i_symlink);
	while ((n = rb_first(&ci->i_fragtree)) != NULL) {
		frag = rb_entry(n, struct ceph_inode_frag, node);
		rb_erase(n, &ci->i_fragtree);
		kfree(frag);
	}

	__ceph_destroy_xattrs(ci);
	if (ci->i_xattrs.blob)
		ceph_buffer_put(ci->i_xattrs.blob);
	if (ci->i_xattrs.prealloc_blob)
		ceph_buffer_put(ci->i_xattrs.prealloc_blob);

	call_rcu(&inode->i_rcu, ceph_i_callback);
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
		inode->i_size = size;
		inode->i_blocks = (size + (1<<9) - 1) >> 9;
		ci->i_reported_size = size;
		if (truncate_seq != ci->i_truncate_seq) {
			dout("truncate_seq %u -> %u\n",
			     ci->i_truncate_seq, truncate_seq);
			ci->i_truncate_seq = truncate_seq;
			/*
			 * If we hold relevant caps, or in the case where we're
			 * not the only client referencing this file and we
			 * don't hold those caps, then we need to check whether
			 * the file is either opened or mmaped
			 */
			if ((issued & (CEPH_CAP_FILE_CACHE|CEPH_CAP_FILE_RD|
				       CEPH_CAP_FILE_WR|CEPH_CAP_FILE_BUFFER|
				       CEPH_CAP_FILE_EXCL|
				       CEPH_CAP_FILE_LAZYIO)) ||
			    mapping_mapped(inode->i_mapping) ||
			    __ceph_caps_file_wanted(ci)) {
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
	return queue_trunc;
}

void ceph_fill_file_time(struct inode *inode, int issued,
			 u64 time_warp_seq, struct timespec *ctime,
			 struct timespec *mtime, struct timespec *atime)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int warn = 0;

	if (issued & (CEPH_CAP_FILE_EXCL|
		      CEPH_CAP_FILE_WR|
		      CEPH_CAP_FILE_BUFFER|
		      CEPH_CAP_AUTH_EXCL|
		      CEPH_CAP_XATTR_EXCL)) {
		if (timespec_compare(ctime, &inode->i_ctime) > 0) {
			dout("ctime %ld.%09ld -> %ld.%09ld inc w/ cap\n",
			     inode->i_ctime.tv_sec, inode->i_ctime.tv_nsec,
			     ctime->tv_sec, ctime->tv_nsec);
			inode->i_ctime = *ctime;
		}
		if (ceph_seq_cmp(time_warp_seq, ci->i_time_warp_seq) > 0) {
			/* the MDS did a utimes() */
			dout("mtime %ld.%09ld -> %ld.%09ld "
			     "tw %d -> %d\n",
			     inode->i_mtime.tv_sec, inode->i_mtime.tv_nsec,
			     mtime->tv_sec, mtime->tv_nsec,
			     ci->i_time_warp_seq, (int)time_warp_seq);

			inode->i_mtime = *mtime;
			inode->i_atime = *atime;
			ci->i_time_warp_seq = time_warp_seq;
		} else if (time_warp_seq == ci->i_time_warp_seq) {
			/* nobody did utimes(); take the max */
			if (timespec_compare(mtime, &inode->i_mtime) > 0) {
				dout("mtime %ld.%09ld -> %ld.%09ld inc\n",
				     inode->i_mtime.tv_sec,
				     inode->i_mtime.tv_nsec,
				     mtime->tv_sec, mtime->tv_nsec);
				inode->i_mtime = *mtime;
			}
			if (timespec_compare(atime, &inode->i_atime) > 0) {
				dout("atime %ld.%09ld -> %ld.%09ld inc\n",
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
static int fill_inode(struct inode *inode,
		      struct ceph_mds_reply_info_in *iinfo,
		      struct ceph_mds_reply_dirfrag *dirinfo,
		      struct ceph_mds_session *session,
		      unsigned long ttl_from, int cap_fmode,
		      struct ceph_cap_reservation *caps_reservation)
{
	struct ceph_mds_reply_inode *info = iinfo->in;
	struct ceph_inode_info *ci = ceph_inode(inode);
	int i;
	int issued = 0, implemented;
	int updating_inode = 0;
	struct timespec mtime, atime, ctime;
	u32 nsplits;
	struct ceph_buffer *xattr_blob = NULL;
	int err = 0;
	int queue_trunc = 0;

	dout("fill_inode %p ino %llx.%llx v %llu had %llu\n",
	     inode, ceph_vinop(inode), le64_to_cpu(info->version),
	     ci->i_version);

	/*
	 * prealloc xattr data, if it looks like we'll need it.  only
	 * if len > 4 (meaning there are actually xattrs; the first 4
	 * bytes are the xattr count).
	 */
	if (iinfo->xattr_len > 4) {
		xattr_blob = ceph_buffer_new(iinfo->xattr_len, GFP_NOFS);
		if (!xattr_blob)
			pr_err("fill_inode ENOMEM xattr blob %d bytes\n",
			       iinfo->xattr_len);
	}

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
	if (le64_to_cpu(info->version) > 0 &&
	    (ci->i_version & ~1) >= le64_to_cpu(info->version))
		goto no_change;
	
	updating_inode = 1;
	issued = __ceph_caps_issued(ci, &implemented);
	issued |= implemented | __ceph_caps_dirty(ci);

	/* update inode */
	ci->i_version = le64_to_cpu(info->version);
	inode->i_version++;
	inode->i_rdev = le32_to_cpu(info->rdev);

	if ((issued & CEPH_CAP_AUTH_EXCL) == 0) {
		inode->i_mode = le32_to_cpu(info->mode);
		inode->i_uid = le32_to_cpu(info->uid);
		inode->i_gid = le32_to_cpu(info->gid);
		dout("%p mode 0%o uid.gid %d.%d\n", inode, inode->i_mode,
		     inode->i_uid, inode->i_gid);
	}

	if ((issued & CEPH_CAP_LINK_EXCL) == 0)
		set_nlink(inode, le32_to_cpu(info->nlink));

	/* be careful with mtime, atime, size */
	ceph_decode_timespec(&atime, &info->atime);
	ceph_decode_timespec(&mtime, &info->mtime);
	ceph_decode_timespec(&ctime, &info->ctime);
	queue_trunc = ceph_fill_file_size(inode, issued,
					  le32_to_cpu(info->truncate_seq),
					  le64_to_cpu(info->truncate_size),
					  le64_to_cpu(info->size));
	ceph_fill_file_time(inode, issued,
			    le32_to_cpu(info->time_warp_seq),
			    &ctime, &mtime, &atime);

	/* only update max_size on auth cap */
	if ((info->cap.flags & CEPH_CAP_FLAG_AUTH) &&
	    ci->i_max_size != le64_to_cpu(info->max_size)) {
		dout("max_size %lld -> %llu\n", ci->i_max_size,
		     le64_to_cpu(info->max_size));
		ci->i_max_size = le64_to_cpu(info->max_size);
	}

	ci->i_layout = info->layout;
	inode->i_blkbits = fls(le32_to_cpu(info->layout.fl_stripe_unit)) - 1;

	/* xattrs */
	/* note that if i_xattrs.len <= 4, i_xattrs.data will still be NULL. */
	if ((issued & CEPH_CAP_XATTR_EXCL) == 0 &&
	    le64_to_cpu(info->xattr_version) > ci->i_xattrs.version) {
		if (ci->i_xattrs.blob)
			ceph_buffer_put(ci->i_xattrs.blob);
		ci->i_xattrs.blob = xattr_blob;
		if (xattr_blob)
			memcpy(ci->i_xattrs.blob->vec.iov_base,
			       iinfo->xattr_data, iinfo->xattr_len);
		ci->i_xattrs.version = le64_to_cpu(info->xattr_version);
		xattr_blob = NULL;
	}

	inode->i_mapping->a_ops = &ceph_aops;
	inode->i_mapping->backing_dev_info =
		&ceph_sb_to_client(inode->i_sb)->backing_dev_info;

	switch (inode->i_mode & S_IFMT) {
	case S_IFIFO:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFSOCK:
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

			err = -EINVAL;
			if (WARN_ON(symlen != inode->i_size))
				goto out;

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
		break;
	case S_IFDIR:
		inode->i_op = &ceph_dir_iops;
		inode->i_fop = &ceph_dir_fops;

		ci->i_dir_layout = iinfo->dir_layout;

		ci->i_files = le64_to_cpu(info->files);
		ci->i_subdirs = le64_to_cpu(info->subdirs);
		ci->i_rbytes = le64_to_cpu(info->rbytes);
		ci->i_rfiles = le64_to_cpu(info->rfiles);
		ci->i_rsubdirs = le64_to_cpu(info->rsubdirs);
		ceph_decode_timespec(&ci->i_rctime, &info->rctime);
		break;
	default:
		pr_err("fill_inode %llx.%llx BAD mode 0%o\n",
		       ceph_vinop(inode), inode->i_mode);
	}

no_change:
	spin_unlock(&ci->i_ceph_lock);

	/* queue truncate if we saw i_size decrease */
	if (queue_trunc)
		ceph_queue_vmtruncate(inode);

	/* populate frag tree */
	/* FIXME: move me up, if/when version reflects fragtree changes */
	nsplits = le32_to_cpu(info->fragtree.nsplits);
	mutex_lock(&ci->i_fragtree_mutex);
	for (i = 0; i < nsplits; i++) {
		u32 id = le32_to_cpu(info->fragtree.splits[i].frag);
		struct ceph_inode_frag *frag = __get_or_create_frag(ci, id);

		if (IS_ERR(frag))
			continue;
		frag->split_by = le32_to_cpu(info->fragtree.splits[i].by);
		dout(" frag %x split by %d\n", frag->frag, frag->split_by);
	}
	mutex_unlock(&ci->i_fragtree_mutex);

	/* were we issued a capability? */
	if (info->cap.caps) {
		if (ceph_snap(inode) == CEPH_NOSNAP) {
			ceph_add_cap(inode, session,
				     le64_to_cpu(info->cap.cap_id),
				     cap_fmode,
				     le32_to_cpu(info->cap.caps),
				     le32_to_cpu(info->cap.wanted),
				     le32_to_cpu(info->cap.seq),
				     le32_to_cpu(info->cap.mseq),
				     le64_to_cpu(info->cap.realm),
				     info->cap.flags,
				     caps_reservation);
		} else {
			spin_lock(&ci->i_ceph_lock);
			dout(" %p got snap_caps %s\n", inode,
			     ceph_cap_string(le32_to_cpu(info->cap.caps)));
			ci->i_snap_caps |= le32_to_cpu(info->cap.caps);
			if (cap_fmode >= 0)
				__ceph_get_fmode(ci, cap_fmode);
			spin_unlock(&ci->i_ceph_lock);
		}
	} else if (cap_fmode >= 0) {
		pr_warning("mds issued no caps on %llx.%llx\n",
			   ceph_vinop(inode));
		__ceph_get_fmode(ci, cap_fmode);
	}

	/* set dir completion flag? */
	if (S_ISDIR(inode->i_mode) &&
	    updating_inode &&                 /* didn't jump to no_change */
	    ci->i_files == 0 && ci->i_subdirs == 0 &&
	    ceph_snap(inode) == CEPH_NOSNAP &&
	    (le32_to_cpu(info->cap.caps) & CEPH_CAP_FILE_SHARED) &&
	    (issued & CEPH_CAP_FILE_EXCL) == 0 &&
	    !ceph_dir_test_complete(inode)) {
		dout(" marking %p complete (empty)\n", inode);
		ceph_dir_set_complete(inode);
		ci->i_max_offset = 2;
	}

	/* update delegation info? */
	if (dirinfo)
		ceph_fill_dirfrag(inode, dirinfo);

	err = 0;

out:
	if (xattr_blob)
		ceph_buffer_put(xattr_blob);
	return err;
}

/*
 * caller should hold session s_mutex.
 */
static void update_dentry_lease(struct dentry *dentry,
				struct ceph_mds_reply_lease *lease,
				struct ceph_mds_session *session,
				unsigned long from_time)
{
	struct ceph_dentry_info *di = ceph_dentry(dentry);
	long unsigned duration = le32_to_cpu(lease->duration_ms);
	long unsigned ttl = from_time + (duration * HZ) / 1000;
	long unsigned half_ttl = from_time + (duration * HZ / 2) / 1000;
	struct inode *dir;

	/* only track leases on regular dentries */
	if (dentry->d_op != &ceph_dentry_ops)
		return;

	spin_lock(&dentry->d_lock);
	dout("update_dentry_lease %p duration %lu ms ttl %lu\n",
	     dentry, duration, ttl);

	/* make lease_rdcache_gen match directory */
	dir = dentry->d_parent->d_inode;
	di->lease_shared_gen = ceph_inode(dir)->i_shared_gen;

	if (duration == 0)
		goto out_unlock;

	if (di->lease_gen == session->s_cap_gen &&
	    time_before(ttl, dentry->d_time))
		goto out_unlock;  /* we already have a newer lease. */

	if (di->lease_session && di->lease_session != session)
		goto out_unlock;

	ceph_dentry_lru_touch(dentry);

	if (!di->lease_session)
		di->lease_session = ceph_get_mds_session(session);
	di->lease_gen = session->s_cap_gen;
	di->lease_seq = le32_to_cpu(lease->seq);
	di->lease_renew_after = half_ttl;
	di->lease_renew_from = 0;
	dentry->d_time = ttl;
out_unlock:
	spin_unlock(&dentry->d_lock);
	return;
}

/*
 * Set dentry's directory position based on the current dir's max, and
 * order it in d_subdirs, so that dcache_readdir behaves.
 *
 * Always called under directory's i_mutex.
 */
static void ceph_set_dentry_offset(struct dentry *dn)
{
	struct dentry *dir = dn->d_parent;
	struct inode *inode = dir->d_inode;
	struct ceph_inode_info *ci;
	struct ceph_dentry_info *di;

	BUG_ON(!inode);

	ci = ceph_inode(inode);
	di = ceph_dentry(dn);

	spin_lock(&ci->i_ceph_lock);
	if (!ceph_dir_test_complete(inode)) {
		spin_unlock(&ci->i_ceph_lock);
		return;
	}
	di->offset = ceph_inode(inode)->i_max_offset++;
	spin_unlock(&ci->i_ceph_lock);

	spin_lock(&dir->d_lock);
	spin_lock_nested(&dn->d_lock, DENTRY_D_LOCK_NESTED);
	list_move(&dn->d_u.d_child, &dir->d_subdirs);
	dout("set_dentry_offset %p %lld (%p %p)\n", dn, di->offset,
	     dn->d_u.d_child.prev, dn->d_u.d_child.next);
	spin_unlock(&dn->d_lock);
	spin_unlock(&dir->d_lock);
}

/*
 * splice a dentry to an inode.
 * caller must hold directory i_mutex for this to be safe.
 *
 * we will only rehash the resulting dentry if @prehash is
 * true; @prehash will be set to false (for the benefit of
 * the caller) if we fail.
 */
static struct dentry *splice_dentry(struct dentry *dn, struct inode *in,
				    bool *prehash, bool set_offset)
{
	struct dentry *realdn;

	BUG_ON(dn->d_inode);

	/* dn must be unhashed */
	if (!d_unhashed(dn))
		d_drop(dn);
	realdn = d_materialise_unique(dn, in);
	if (IS_ERR(realdn)) {
		pr_err("splice_dentry error %ld %p inode %p ino %llx.%llx\n",
		       PTR_ERR(realdn), dn, in, ceph_vinop(in));
		if (prehash)
			*prehash = false; /* don't rehash on error */
		dn = realdn; /* note realdn contains the error */
		goto out;
	} else if (realdn) {
		dout("dn %p (%d) spliced with %p (%d) "
		     "inode %p ino %llx.%llx\n",
		     dn, dn->d_count,
		     realdn, realdn->d_count,
		     realdn->d_inode, ceph_vinop(realdn->d_inode));
		dput(dn);
		dn = realdn;
	} else {
		BUG_ON(!ceph_dentry(dn));
		dout("dn %p attached to %p ino %llx.%llx\n",
		     dn, dn->d_inode, ceph_vinop(dn->d_inode));
	}
	if ((!prehash || *prehash) && d_unhashed(dn))
		d_rehash(dn);
	if (set_offset)
		ceph_set_dentry_offset(dn);
out:
	return dn;
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
int ceph_fill_trace(struct super_block *sb, struct ceph_mds_request *req,
		    struct ceph_mds_session *session)
{
	struct ceph_mds_reply_info_parsed *rinfo = &req->r_reply_info;
	struct inode *in = NULL;
	struct ceph_mds_reply_inode *ininfo;
	struct ceph_vino vino;
	struct ceph_fs_client *fsc = ceph_sb_to_client(sb);
	int i = 0;
	int err = 0;

	dout("fill_trace %p is_dentry %d is_target %d\n", req,
	     rinfo->head->is_dentry, rinfo->head->is_target);

#if 0
	/*
	 * Debugging hook:
	 *
	 * If we resend completed ops to a recovering mds, we get no
	 * trace.  Since that is very rare, pretend this is the case
	 * to ensure the 'no trace' handlers in the callers behave.
	 *
	 * Fill in inodes unconditionally to avoid breaking cap
	 * invariants.
	 */
	if (rinfo->head->op & CEPH_MDS_OP_WRITE) {
		pr_info("fill_trace faking empty trace on %lld %s\n",
			req->r_tid, ceph_mds_op_name(rinfo->head->op));
		if (rinfo->head->is_dentry) {
			rinfo->head->is_dentry = 0;
			err = fill_inode(req->r_locked_dir,
					 &rinfo->diri, rinfo->dirfrag,
					 session, req->r_request_started, -1);
		}
		if (rinfo->head->is_target) {
			rinfo->head->is_target = 0;
			ininfo = rinfo->targeti.in;
			vino.ino = le64_to_cpu(ininfo->ino);
			vino.snap = le64_to_cpu(ininfo->snapid);
			in = ceph_get_inode(sb, vino);
			err = fill_inode(in, &rinfo->targeti, NULL,
					 session, req->r_request_started,
					 req->r_fmode);
			iput(in);
		}
	}
#endif

	if (!rinfo->head->is_target && !rinfo->head->is_dentry) {
		dout("fill_trace reply is empty!\n");
		if (rinfo->head->result == 0 && req->r_locked_dir)
			ceph_invalidate_dir_request(req);
		return 0;
	}

	if (rinfo->head->is_dentry) {
		struct inode *dir = req->r_locked_dir;

		err = fill_inode(dir, &rinfo->diri, rinfo->dirfrag,
				 session, req->r_request_started, -1,
				 &req->r_caps_reservation);
		if (err < 0)
			return err;
	}

	/*
	 * ignore null lease/binding on snapdir ENOENT, or else we
	 * will have trouble splicing in the virtual snapdir later
	 */
	if (rinfo->head->is_dentry && !req->r_aborted &&
	    (rinfo->head->is_target || strncmp(req->r_dentry->d_name.name,
					       fsc->mount_options->snapdir_name,
					       req->r_dentry->d_name.len))) {
		/*
		 * lookup link rename   : null -> possibly existing inode
		 * mknod symlink mkdir  : null -> new inode
		 * unlink               : linked -> null
		 */
		struct inode *dir = req->r_locked_dir;
		struct dentry *dn = req->r_dentry;
		bool have_dir_cap, have_lease;

		BUG_ON(!dn);
		BUG_ON(!dir);
		BUG_ON(dn->d_parent->d_inode != dir);
		BUG_ON(ceph_ino(dir) !=
		       le64_to_cpu(rinfo->diri.in->ino));
		BUG_ON(ceph_snap(dir) !=
		       le64_to_cpu(rinfo->diri.in->snapid));

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
			dout(" src %p '%.*s' dst %p '%.*s'\n",
			     req->r_old_dentry,
			     req->r_old_dentry->d_name.len,
			     req->r_old_dentry->d_name.name,
			     dn, dn->d_name.len, dn->d_name.name);
			dout("fill_trace doing d_move %p -> %p\n",
			     req->r_old_dentry, dn);

			d_move(req->r_old_dentry, dn);
			dout(" src %p '%.*s' dst %p '%.*s'\n",
			     req->r_old_dentry,
			     req->r_old_dentry->d_name.len,
			     req->r_old_dentry->d_name.name,
			     dn, dn->d_name.len, dn->d_name.name);

			/* ensure target dentry is invalidated, despite
			   rehashing bug in vfs_rename_dir */
			ceph_invalidate_dentry_lease(dn);

			/*
			 * d_move() puts the renamed dentry at the end of
			 * d_subdirs.  We need to assign it an appropriate
			 * directory offset so we can behave when holding
			 * D_COMPLETE.
			 */
			ceph_set_dentry_offset(req->r_old_dentry);
			dout("dn %p gets new offset %lld\n", req->r_old_dentry, 
			     ceph_dentry(req->r_old_dentry)->offset);

			dn = req->r_old_dentry;  /* use old_dentry */
			in = dn->d_inode;
		}

		/* null dentry? */
		if (!rinfo->head->is_target) {
			dout("fill_trace null dentry\n");
			if (dn->d_inode) {
				dout("d_delete %p\n", dn);
				d_delete(dn);
			} else {
				dout("d_instantiate %p NULL\n", dn);
				d_instantiate(dn, NULL);
				if (have_lease && d_unhashed(dn))
					d_rehash(dn);
				update_dentry_lease(dn, rinfo->dlease,
						    session,
						    req->r_request_started);
			}
			goto done;
		}

		/* attach proper inode */
		ininfo = rinfo->targeti.in;
		vino.ino = le64_to_cpu(ininfo->ino);
		vino.snap = le64_to_cpu(ininfo->snapid);
		in = dn->d_inode;
		if (!in) {
			in = ceph_get_inode(sb, vino);
			if (IS_ERR(in)) {
				pr_err("fill_trace bad get_inode "
				       "%llx.%llx\n", vino.ino, vino.snap);
				err = PTR_ERR(in);
				d_delete(dn);
				goto done;
			}
			dn = splice_dentry(dn, in, &have_lease, true);
			if (IS_ERR(dn)) {
				err = PTR_ERR(dn);
				goto done;
			}
			req->r_dentry = dn;  /* may have spliced */
			ihold(in);
		} else if (ceph_ino(in) == vino.ino &&
			   ceph_snap(in) == vino.snap) {
			ihold(in);
		} else {
			dout(" %p links to %p %llx.%llx, not %llx.%llx\n",
			     dn, in, ceph_ino(in), ceph_snap(in),
			     vino.ino, vino.snap);
			have_lease = false;
			in = NULL;
		}

		if (have_lease)
			update_dentry_lease(dn, rinfo->dlease, session,
					    req->r_request_started);
		dout(" final dn %p\n", dn);
		i++;
	} else if (req->r_op == CEPH_MDS_OP_LOOKUPSNAP ||
		   req->r_op == CEPH_MDS_OP_MKSNAP) {
		struct dentry *dn = req->r_dentry;

		/* fill out a snapdir LOOKUPSNAP dentry */
		BUG_ON(!dn);
		BUG_ON(!req->r_locked_dir);
		BUG_ON(ceph_snap(req->r_locked_dir) != CEPH_SNAPDIR);
		ininfo = rinfo->targeti.in;
		vino.ino = le64_to_cpu(ininfo->ino);
		vino.snap = le64_to_cpu(ininfo->snapid);
		in = ceph_get_inode(sb, vino);
		if (IS_ERR(in)) {
			pr_err("fill_inode get_inode badness %llx.%llx\n",
			       vino.ino, vino.snap);
			err = PTR_ERR(in);
			d_delete(dn);
			goto done;
		}
		dout(" linking snapped dir %p to dn %p\n", in, dn);
		dn = splice_dentry(dn, in, NULL, true);
		if (IS_ERR(dn)) {
			err = PTR_ERR(dn);
			goto done;
		}
		req->r_dentry = dn;  /* may have spliced */
		ihold(in);
		rinfo->head->is_dentry = 1;  /* fool notrace handlers */
	}

	if (rinfo->head->is_target) {
		vino.ino = le64_to_cpu(rinfo->targeti.in->ino);
		vino.snap = le64_to_cpu(rinfo->targeti.in->snapid);

		if (in == NULL || ceph_ino(in) != vino.ino ||
		    ceph_snap(in) != vino.snap) {
			in = ceph_get_inode(sb, vino);
			if (IS_ERR(in)) {
				err = PTR_ERR(in);
				goto done;
			}
		}
		req->r_target_inode = in;

		err = fill_inode(in,
				 &rinfo->targeti, NULL,
				 session, req->r_request_started,
				 (le32_to_cpu(rinfo->head->result) == 0) ?
				 req->r_fmode : -1,
				 &req->r_caps_reservation);
		if (err < 0) {
			pr_err("fill_inode badness %p %llx.%llx\n",
			       in, ceph_vinop(in));
			goto done;
		}
	}

done:
	dout("fill_trace done err=%d\n", err);
	return err;
}

/*
 * Prepopulate our cache with readdir results, leases, etc.
 */
int ceph_readdir_prepopulate(struct ceph_mds_request *req,
			     struct ceph_mds_session *session)
{
	struct dentry *parent = req->r_dentry;
	struct ceph_mds_reply_info_parsed *rinfo = &req->r_reply_info;
	struct qstr dname;
	struct dentry *dn;
	struct inode *in;
	int err = 0, i;
	struct inode *snapdir = NULL;
	struct ceph_mds_request_head *rhead = req->r_request->front.iov_base;
	u64 frag = le32_to_cpu(rhead->args.readdir.frag);
	struct ceph_dentry_info *di;

	if (le32_to_cpu(rinfo->head->op) == CEPH_MDS_OP_LSSNAP) {
		snapdir = ceph_get_snapdir(parent->d_inode);
		parent = d_find_alias(snapdir);
		dout("readdir_prepopulate %d items under SNAPDIR dn %p\n",
		     rinfo->dir_nr, parent);
	} else {
		dout("readdir_prepopulate %d items under dn %p\n",
		     rinfo->dir_nr, parent);
		if (rinfo->dir_dir)
			ceph_fill_dirfrag(parent->d_inode, rinfo->dir_dir);
	}

	for (i = 0; i < rinfo->dir_nr; i++) {
		struct ceph_vino vino;

		dname.name = rinfo->dir_dname[i];
		dname.len = rinfo->dir_dname_len[i];
		dname.hash = full_name_hash(dname.name, dname.len);

		vino.ino = le64_to_cpu(rinfo->dir_in[i].in->ino);
		vino.snap = le64_to_cpu(rinfo->dir_in[i].in->snapid);

retry_lookup:
		dn = d_lookup(parent, &dname);
		dout("d_lookup on parent=%p name=%.*s got %p\n",
		     parent, dname.len, dname.name, dn);

		if (!dn) {
			dn = d_alloc(parent, &dname);
			dout("d_alloc %p '%.*s' = %p\n", parent,
			     dname.len, dname.name, dn);
			if (dn == NULL) {
				dout("d_alloc badness\n");
				err = -ENOMEM;
				goto out;
			}
			err = ceph_init_dentry(dn);
			if (err < 0) {
				dput(dn);
				goto out;
			}
		} else if (dn->d_inode &&
			   (ceph_ino(dn->d_inode) != vino.ino ||
			    ceph_snap(dn->d_inode) != vino.snap)) {
			dout(" dn %p points to wrong inode %p\n",
			     dn, dn->d_inode);
			d_delete(dn);
			dput(dn);
			goto retry_lookup;
		} else {
			/* reorder parent's d_subdirs */
			spin_lock(&parent->d_lock);
			spin_lock_nested(&dn->d_lock, DENTRY_D_LOCK_NESTED);
			list_move(&dn->d_u.d_child, &parent->d_subdirs);
			spin_unlock(&dn->d_lock);
			spin_unlock(&parent->d_lock);
		}

		di = dn->d_fsdata;
		di->offset = ceph_make_fpos(frag, i + req->r_readdir_offset);

		/* inode */
		if (dn->d_inode) {
			in = dn->d_inode;
		} else {
			in = ceph_get_inode(parent->d_sb, vino);
			if (IS_ERR(in)) {
				dout("new_inode badness\n");
				d_delete(dn);
				dput(dn);
				err = PTR_ERR(in);
				goto out;
			}
			dn = splice_dentry(dn, in, NULL, false);
			if (IS_ERR(dn))
				dn = NULL;
		}

		if (fill_inode(in, &rinfo->dir_in[i], NULL, session,
			       req->r_request_started, -1,
			       &req->r_caps_reservation) < 0) {
			pr_err("fill_inode badness on %p\n", in);
			goto next_item;
		}
		if (dn)
			update_dentry_lease(dn, rinfo->dir_dlease[i],
					    req->r_session,
					    req->r_request_started);
next_item:
		if (dn)
			dput(dn);
	}
	req->r_did_prepopulate = true;

out:
	if (snapdir) {
		iput(snapdir);
		dput(parent);
	}
	dout("readdir_prepopulate done\n");
	return err;
}

int ceph_inode_set_size(struct inode *inode, loff_t size)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	int ret = 0;

	spin_lock(&ci->i_ceph_lock);
	dout("set_size %p %llu -> %llu\n", inode, inode->i_size, size);
	inode->i_size = size;
	inode->i_blocks = (size + (1 << 9) - 1) >> 9;

	/* tell the MDS if we are approaching max_size */
	if ((size << 1) >= ci->i_max_size &&
	    (ci->i_reported_size << 1) < ci->i_max_size)
		ret = 1;

	spin_unlock(&ci->i_ceph_lock);
	return ret;
}

/*
 * Write back inode data in a worker thread.  (This can't be done
 * in the message handler context.)
 */
void ceph_queue_writeback(struct inode *inode)
{
	ihold(inode);
	if (queue_work(ceph_inode_to_client(inode)->wb_wq,
		       &ceph_inode(inode)->i_wb_work)) {
		dout("ceph_queue_writeback %p\n", inode);
	} else {
		dout("ceph_queue_writeback %p failed\n", inode);
		iput(inode);
	}
}

static void ceph_writeback_work(struct work_struct *work)
{
	struct ceph_inode_info *ci = container_of(work, struct ceph_inode_info,
						  i_wb_work);
	struct inode *inode = &ci->vfs_inode;

	dout("writeback %p\n", inode);
	filemap_fdatawrite(&inode->i_data);
	iput(inode);
}

/*
 * queue an async invalidation
 */
void ceph_queue_invalidate(struct inode *inode)
{
	ihold(inode);
	if (queue_work(ceph_inode_to_client(inode)->pg_inv_wq,
		       &ceph_inode(inode)->i_pg_inv_work)) {
		dout("ceph_queue_invalidate %p\n", inode);
	} else {
		dout("ceph_queue_invalidate %p failed\n", inode);
		iput(inode);
	}
}

/*
 * Invalidate inode pages in a worker thread.  (This can't be done
 * in the message handler context.)
 */
static void ceph_invalidate_work(struct work_struct *work)
{
	struct ceph_inode_info *ci = container_of(work, struct ceph_inode_info,
						  i_pg_inv_work);
	struct inode *inode = &ci->vfs_inode;
	u32 orig_gen;
	int check = 0;

	spin_lock(&ci->i_ceph_lock);
	dout("invalidate_pages %p gen %d revoking %d\n", inode,
	     ci->i_rdcache_gen, ci->i_rdcache_revoking);
	if (ci->i_rdcache_revoking != ci->i_rdcache_gen) {
		/* nevermind! */
		spin_unlock(&ci->i_ceph_lock);
		goto out;
	}
	orig_gen = ci->i_rdcache_gen;
	spin_unlock(&ci->i_ceph_lock);

	truncate_inode_pages(&inode->i_data, 0);

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
	}
	spin_unlock(&ci->i_ceph_lock);

	if (check)
		ceph_check_caps(ci, 0, NULL);
out:
	iput(inode);
}


/*
 * called by trunc_wq; take i_mutex ourselves
 *
 * We also truncate in a separate thread as well.
 */
static void ceph_vmtruncate_work(struct work_struct *work)
{
	struct ceph_inode_info *ci = container_of(work, struct ceph_inode_info,
						  i_vmtruncate_work);
	struct inode *inode = &ci->vfs_inode;

	dout("vmtruncate_work %p\n", inode);
	mutex_lock(&inode->i_mutex);
	__ceph_do_pending_vmtruncate(inode);
	mutex_unlock(&inode->i_mutex);
	iput(inode);
}

/*
 * Queue an async vmtruncate.  If we fail to queue work, we will handle
 * the truncation the next time we call __ceph_do_pending_vmtruncate.
 */
void ceph_queue_vmtruncate(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	ihold(inode);
	if (queue_work(ceph_sb_to_client(inode->i_sb)->trunc_wq,
		       &ci->i_vmtruncate_work)) {
		dout("ceph_queue_vmtruncate %p\n", inode);
	} else {
		dout("ceph_queue_vmtruncate %p failed, pending=%d\n",
		     inode, ci->i_truncate_pending);
		iput(inode);
	}
}

/*
 * called with i_mutex held.
 *
 * Make sure any pending truncation is applied before doing anything
 * that may depend on it.
 */
void __ceph_do_pending_vmtruncate(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	u64 to;
	int wrbuffer_refs, wake = 0;

retry:
	spin_lock(&ci->i_ceph_lock);
	if (ci->i_truncate_pending == 0) {
		dout("__do_pending_vmtruncate %p none pending\n", inode);
		spin_unlock(&ci->i_ceph_lock);
		return;
	}

	/*
	 * make sure any dirty snapped pages are flushed before we
	 * possibly truncate them.. so write AND block!
	 */
	if (ci->i_wrbuffer_ref_head < ci->i_wrbuffer_ref) {
		dout("__do_pending_vmtruncate %p flushing snaps first\n",
		     inode);
		spin_unlock(&ci->i_ceph_lock);
		filemap_write_and_wait_range(&inode->i_data, 0,
					     inode->i_sb->s_maxbytes);
		goto retry;
	}

	to = ci->i_truncate_size;
	wrbuffer_refs = ci->i_wrbuffer_ref;
	dout("__do_pending_vmtruncate %p (%d) to %lld\n", inode,
	     ci->i_truncate_pending, to);
	spin_unlock(&ci->i_ceph_lock);

	truncate_inode_pages(inode->i_mapping, to);

	spin_lock(&ci->i_ceph_lock);
	ci->i_truncate_pending--;
	if (ci->i_truncate_pending == 0)
		wake = 1;
	spin_unlock(&ci->i_ceph_lock);

	if (wrbuffer_refs == 0)
		ceph_check_caps(ci, CHECK_CAPS_AUTHONLY, NULL);
	if (wake)
		wake_up_all(&ci->i_cap_wq);
}


/*
 * symlinks
 */
static void *ceph_sym_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct ceph_inode_info *ci = ceph_inode(dentry->d_inode);
	nd_set_link(nd, ci->i_symlink);
	return NULL;
}

static const struct inode_operations ceph_symlink_iops = {
	.readlink = generic_readlink,
	.follow_link = ceph_sym_follow_link,
};

/*
 * setattr
 */
int ceph_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct inode *parent_inode;
	const unsigned int ia_valid = attr->ia_valid;
	struct ceph_mds_request *req;
	struct ceph_mds_client *mdsc = ceph_sb_to_client(dentry->d_sb)->mdsc;
	int issued;
	int release = 0, dirtied = 0;
	int mask = 0;
	int err = 0;
	int inode_dirty_flags = 0;

	if (ceph_snap(inode) != CEPH_NOSNAP)
		return -EROFS;

	__ceph_do_pending_vmtruncate(inode);

	err = inode_change_ok(inode, attr);
	if (err != 0)
		return err;

	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_SETATTR,
				       USE_AUTH_MDS);
	if (IS_ERR(req))
		return PTR_ERR(req);

	spin_lock(&ci->i_ceph_lock);
	issued = __ceph_caps_issued(ci, NULL);
	dout("setattr %p issued %s\n", inode, ceph_cap_string(issued));

	if (ia_valid & ATTR_UID) {
		dout("setattr %p uid %d -> %d\n", inode,
		     inode->i_uid, attr->ia_uid);
		if (issued & CEPH_CAP_AUTH_EXCL) {
			inode->i_uid = attr->ia_uid;
			dirtied |= CEPH_CAP_AUTH_EXCL;
		} else if ((issued & CEPH_CAP_AUTH_SHARED) == 0 ||
			   attr->ia_uid != inode->i_uid) {
			req->r_args.setattr.uid = cpu_to_le32(attr->ia_uid);
			mask |= CEPH_SETATTR_UID;
			release |= CEPH_CAP_AUTH_SHARED;
		}
	}
	if (ia_valid & ATTR_GID) {
		dout("setattr %p gid %d -> %d\n", inode,
		     inode->i_gid, attr->ia_gid);
		if (issued & CEPH_CAP_AUTH_EXCL) {
			inode->i_gid = attr->ia_gid;
			dirtied |= CEPH_CAP_AUTH_EXCL;
		} else if ((issued & CEPH_CAP_AUTH_SHARED) == 0 ||
			   attr->ia_gid != inode->i_gid) {
			req->r_args.setattr.gid = cpu_to_le32(attr->ia_gid);
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
			req->r_args.setattr.mode = cpu_to_le32(attr->ia_mode);
			mask |= CEPH_SETATTR_MODE;
			release |= CEPH_CAP_AUTH_SHARED;
		}
	}

	if (ia_valid & ATTR_ATIME) {
		dout("setattr %p atime %ld.%ld -> %ld.%ld\n", inode,
		     inode->i_atime.tv_sec, inode->i_atime.tv_nsec,
		     attr->ia_atime.tv_sec, attr->ia_atime.tv_nsec);
		if (issued & CEPH_CAP_FILE_EXCL) {
			ci->i_time_warp_seq++;
			inode->i_atime = attr->ia_atime;
			dirtied |= CEPH_CAP_FILE_EXCL;
		} else if ((issued & CEPH_CAP_FILE_WR) &&
			   timespec_compare(&inode->i_atime,
					    &attr->ia_atime) < 0) {
			inode->i_atime = attr->ia_atime;
			dirtied |= CEPH_CAP_FILE_WR;
		} else if ((issued & CEPH_CAP_FILE_SHARED) == 0 ||
			   !timespec_equal(&inode->i_atime, &attr->ia_atime)) {
			ceph_encode_timespec(&req->r_args.setattr.atime,
					     &attr->ia_atime);
			mask |= CEPH_SETATTR_ATIME;
			release |= CEPH_CAP_FILE_CACHE | CEPH_CAP_FILE_RD |
				CEPH_CAP_FILE_WR;
		}
	}
	if (ia_valid & ATTR_MTIME) {
		dout("setattr %p mtime %ld.%ld -> %ld.%ld\n", inode,
		     inode->i_mtime.tv_sec, inode->i_mtime.tv_nsec,
		     attr->ia_mtime.tv_sec, attr->ia_mtime.tv_nsec);
		if (issued & CEPH_CAP_FILE_EXCL) {
			ci->i_time_warp_seq++;
			inode->i_mtime = attr->ia_mtime;
			dirtied |= CEPH_CAP_FILE_EXCL;
		} else if ((issued & CEPH_CAP_FILE_WR) &&
			   timespec_compare(&inode->i_mtime,
					    &attr->ia_mtime) < 0) {
			inode->i_mtime = attr->ia_mtime;
			dirtied |= CEPH_CAP_FILE_WR;
		} else if ((issued & CEPH_CAP_FILE_SHARED) == 0 ||
			   !timespec_equal(&inode->i_mtime, &attr->ia_mtime)) {
			ceph_encode_timespec(&req->r_args.setattr.mtime,
					     &attr->ia_mtime);
			mask |= CEPH_SETATTR_MTIME;
			release |= CEPH_CAP_FILE_SHARED | CEPH_CAP_FILE_RD |
				CEPH_CAP_FILE_WR;
		}
	}
	if (ia_valid & ATTR_SIZE) {
		dout("setattr %p size %lld -> %lld\n", inode,
		     inode->i_size, attr->ia_size);
		if (attr->ia_size > inode->i_sb->s_maxbytes) {
			err = -EINVAL;
			goto out;
		}
		if ((issued & CEPH_CAP_FILE_EXCL) &&
		    attr->ia_size > inode->i_size) {
			inode->i_size = attr->ia_size;
			inode->i_blocks =
				(attr->ia_size + (1 << 9) - 1) >> 9;
			inode->i_ctime = attr->ia_ctime;
			ci->i_reported_size = attr->ia_size;
			dirtied |= CEPH_CAP_FILE_EXCL;
		} else if ((issued & CEPH_CAP_FILE_SHARED) == 0 ||
			   attr->ia_size != inode->i_size) {
			req->r_args.setattr.size = cpu_to_le64(attr->ia_size);
			req->r_args.setattr.old_size =
				cpu_to_le64(inode->i_size);
			mask |= CEPH_SETATTR_SIZE;
			release |= CEPH_CAP_FILE_SHARED | CEPH_CAP_FILE_RD |
				CEPH_CAP_FILE_WR;
		}
	}

	/* these do nothing */
	if (ia_valid & ATTR_CTIME) {
		bool only = (ia_valid & (ATTR_SIZE|ATTR_MTIME|ATTR_ATIME|
					 ATTR_MODE|ATTR_UID|ATTR_GID)) == 0;
		dout("setattr %p ctime %ld.%ld -> %ld.%ld (%s)\n", inode,
		     inode->i_ctime.tv_sec, inode->i_ctime.tv_nsec,
		     attr->ia_ctime.tv_sec, attr->ia_ctime.tv_nsec,
		     only ? "ctime only" : "ignored");
		inode->i_ctime = attr->ia_ctime;
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
		inode_dirty_flags = __ceph_mark_dirty_caps(ci, dirtied);
		inode->i_ctime = CURRENT_TIME;
	}

	release &= issued;
	spin_unlock(&ci->i_ceph_lock);

	if (inode_dirty_flags)
		__mark_inode_dirty(inode, inode_dirty_flags);

	if (mask) {
		req->r_inode = inode;
		ihold(inode);
		req->r_inode_drop = release;
		req->r_args.setattr.mask = cpu_to_le32(mask);
		req->r_num_caps = 1;
		parent_inode = ceph_get_dentry_parent_inode(dentry);
		err = ceph_mdsc_do_request(mdsc, parent_inode, req);
		iput(parent_inode);
	}
	dout("setattr %p result=%d (%s locally, %d remote)\n", inode, err,
	     ceph_cap_string(dirtied), mask);

	ceph_mdsc_put_request(req);
	__ceph_do_pending_vmtruncate(inode);
	return err;
out:
	spin_unlock(&ci->i_ceph_lock);
	ceph_mdsc_put_request(req);
	return err;
}

/*
 * Verify that we have a lease on the given mask.  If not,
 * do a getattr against an mds.
 */
int ceph_do_getattr(struct inode *inode, int mask)
{
	struct ceph_fs_client *fsc = ceph_sb_to_client(inode->i_sb);
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	int err;

	if (ceph_snap(inode) == CEPH_SNAPDIR) {
		dout("do_getattr inode %p SNAPDIR\n", inode);
		return 0;
	}

	dout("do_getattr inode %p mask %s mode 0%o\n", inode, ceph_cap_string(mask), inode->i_mode);
	if (ceph_caps_issued_mask(ceph_inode(inode), mask, 1))
		return 0;

	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_GETATTR, USE_ANY_MDS);
	if (IS_ERR(req))
		return PTR_ERR(req);
	req->r_inode = inode;
	ihold(inode);
	req->r_num_caps = 1;
	req->r_args.getattr.mask = cpu_to_le32(mask);
	err = ceph_mdsc_do_request(mdsc, NULL, req);
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

	err = ceph_do_getattr(inode, CEPH_CAP_AUTH_SHARED);

	if (!err)
		err = generic_permission(inode, mask);
	return err;
}

/*
 * Get all attributes.  Hopefully somedata we'll have a statlite()
 * and can limit the fields we require to be accurate.
 */
int ceph_getattr(struct vfsmount *mnt, struct dentry *dentry,
		 struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	struct ceph_inode_info *ci = ceph_inode(inode);
	int err;

	err = ceph_do_getattr(inode, CEPH_STAT_CAP_INODE_ALL);
	if (!err) {
		generic_fillattr(inode, stat);
		stat->ino = ceph_translate_ino(inode->i_sb, inode->i_ino);
		if (ceph_snap(inode) != CEPH_NOSNAP)
			stat->dev = ceph_snap(inode);
		else
			stat->dev = 0;
		if (S_ISDIR(inode->i_mode)) {
			if (ceph_test_mount_opt(ceph_sb_to_client(inode->i_sb),
						RBYTES))
				stat->size = ci->i_rbytes;
			else
				stat->size = ci->i_files + ci->i_subdirs;
			stat->blocks = 0;
			stat->blksize = 65536;
		}
	}
	return err;
}
