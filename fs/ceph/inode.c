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
#include <linux/fscrypt.h>

#include "super.h"
#include "mds_client.h"
#include "cache.h"
#include "crypto.h"
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
static const struct inode_operations ceph_encrypted_symlink_iops;

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

/**
 * ceph_new_inode - allocate a new inode in advance of an expected create
 * @dir: parent directory for new inode
 * @dentry: dentry that may eventually point to new inode
 * @mode: mode of new inode
 * @as_ctx: pointer to inherited security context
 *
 * Allocate a new inode in advance of an operation to create a new inode.
 * This allocates the inode and sets up the acl_sec_ctx with appropriate
 * info for the new inode.
 *
 * Returns a pointer to the new inode or an ERR_PTR.
 */
struct inode *ceph_new_inode(struct inode *dir, struct dentry *dentry,
			     umode_t *mode, struct ceph_acl_sec_ctx *as_ctx)
{
	int err;
	struct inode *inode;

	inode = new_inode(dir->i_sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	inode->i_blkbits = CEPH_FSCRYPT_BLOCK_SHIFT;

	if (!S_ISLNK(*mode)) {
		err = ceph_pre_init_acls(dir, mode, as_ctx);
		if (err < 0)
			goto out_err;
	}

	inode->i_state = 0;
	inode->i_mode = *mode;

	err = ceph_security_init_secctx(dentry, *mode, as_ctx);
	if (err < 0)
		goto out_err;

	/*
	 * We'll skip setting fscrypt context for snapshots, leaving that for
	 * the handle_reply().
	 */
	if (ceph_snap(dir) != CEPH_SNAPDIR) {
		err = ceph_fscrypt_prepare_context(dir, inode, as_ctx);
		if (err)
			goto out_err;
	}

	return inode;
out_err:
	iput(inode);
	return ERR_PTR(err);
}

void ceph_as_ctx_to_req(struct ceph_mds_request *req,
			struct ceph_acl_sec_ctx *as_ctx)
{
	if (as_ctx->pagelist) {
		req->r_pagelist = as_ctx->pagelist;
		as_ctx->pagelist = NULL;
	}
	ceph_fscrypt_as_ctx_to_req(req, as_ctx);
}

/**
 * ceph_get_inode - find or create/hash a new inode
 * @sb: superblock to search and allocate in
 * @vino: vino to search for
 * @newino: optional new inode to insert if one isn't found (may be NULL)
 *
 * Search for or insert a new inode into the hash for the given vino, and
 * return a reference to it. If new is non-NULL, its reference is consumed.
 */
struct inode *ceph_get_inode(struct super_block *sb, struct ceph_vino vino,
			     struct inode *newino)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(sb);
	struct ceph_client *cl = mdsc->fsc->client;
	struct inode *inode;

	if (ceph_vino_is_reserved(vino))
		return ERR_PTR(-EREMOTEIO);

	if (newino) {
		inode = inode_insert5(newino, (unsigned long)vino.ino,
				      ceph_ino_compare, ceph_set_ino_cb, &vino);
		if (inode != newino)
			iput(newino);
	} else {
		inode = iget5_locked(sb, (unsigned long)vino.ino,
				     ceph_ino_compare, ceph_set_ino_cb, &vino);
	}

	if (!inode) {
		doutc(cl, "no inode found for %llx.%llx\n", vino.ino, vino.snap);
		return ERR_PTR(-ENOMEM);
	}

	doutc(cl, "on %llx=%llx.%llx got %p new %d\n",
	      ceph_present_inode(inode), ceph_vinop(inode), inode,
	      !!(inode->i_state & I_NEW));
	return inode;
}

/*
 * get/constuct snapdir inode for a given directory
 */
struct inode *ceph_get_snapdir(struct inode *parent)
{
	struct ceph_client *cl = ceph_inode_to_client(parent);
	struct ceph_vino vino = {
		.ino = ceph_ino(parent),
		.snap = CEPH_SNAPDIR,
	};
	struct inode *inode = ceph_get_inode(parent->i_sb, vino, NULL);
	struct ceph_inode_info *ci = ceph_inode(inode);
	int ret = -ENOTDIR;

	if (IS_ERR(inode))
		return inode;

	if (!S_ISDIR(parent->i_mode)) {
		pr_warn_once_client(cl, "bad snapdir parent type (mode=0%o)\n",
				    parent->i_mode);
		goto err;
	}

	if (!(inode->i_state & I_NEW) && !S_ISDIR(inode->i_mode)) {
		pr_warn_once_client(cl, "bad snapdir inode type (mode=0%o)\n",
				    inode->i_mode);
		goto err;
	}

	inode->i_mode = parent->i_mode;
	inode->i_uid = parent->i_uid;
	inode->i_gid = parent->i_gid;
	inode_set_mtime_to_ts(inode, inode_get_mtime(parent));
	inode_set_ctime_to_ts(inode, inode_get_ctime(parent));
	inode_set_atime_to_ts(inode, inode_get_atime(parent));
	ci->i_rbytes = 0;
	ci->i_btime = ceph_inode(parent)->i_btime;

#ifdef CONFIG_FS_ENCRYPTION
	/* if encrypted, just borrow fscrypt_auth from parent */
	if (IS_ENCRYPTED(parent)) {
		struct ceph_inode_info *pci = ceph_inode(parent);

		ci->fscrypt_auth = kmemdup(pci->fscrypt_auth,
					   pci->fscrypt_auth_len,
					   GFP_KERNEL);
		if (ci->fscrypt_auth) {
			inode->i_flags |= S_ENCRYPTED;
			ci->fscrypt_auth_len = pci->fscrypt_auth_len;
		} else {
			doutc(cl, "Failed to alloc snapdir fscrypt_auth\n");
			ret = -ENOMEM;
			goto err;
		}
	}
#endif
	if (inode->i_state & I_NEW) {
		inode->i_op = &ceph_snapdir_iops;
		inode->i_fop = &ceph_snapdir_fops;
		ci->i_snap_caps = CEPH_CAP_PIN; /* so we can open */
		unlock_new_inode(inode);
	}

	return inode;
err:
	if ((inode->i_state & I_NEW))
		discard_new_inode(inode);
	else
		iput(inode);
	return ERR_PTR(ret);
}

const struct inode_operations ceph_file_iops = {
	.permission = ceph_permission,
	.setattr = ceph_setattr,
	.getattr = ceph_getattr,
	.listxattr = ceph_listxattr,
	.get_inode_acl = ceph_get_acl,
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
	struct inode *inode = &ci->netfs.inode;
	struct ceph_client *cl = ceph_inode_to_client(inode);
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

	doutc(cl, "added %p %llx.%llx frag %x\n", inode, ceph_vinop(inode), f);
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
	struct ceph_client *cl = ceph_inode_to_client(&ci->netfs.inode);
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
		doutc(cl, "frag(%x) %x splits by %d (%d ways)\n", v, t,
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
	doutc(cl, "frag(%x) = %x\n", v, t);

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
	struct ceph_client *cl = ceph_inode_to_client(inode);
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
			doutc(cl, "removed %p %llx.%llx frag %x (no ref)\n",
			      inode, ceph_vinop(inode), id);
			rb_erase(&frag->node, &ci->i_fragtree);
			kfree(frag);
		} else {
			/* tree branch, keep and clear */
			doutc(cl, "cleared %p %llx.%llx frag %x referral\n",
			      inode, ceph_vinop(inode), id);
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
		pr_err_client(cl, "ENOMEM on mds ref %p %llx.%llx fg %x\n",
			      inode, ceph_vinop(inode),
			      le32_to_cpu(dirinfo->frag));
		err = -ENOMEM;
		goto out;
	}

	frag->mds = mds;
	frag->ndist = min_t(u32, ndist, CEPH_MAX_DIRFRAG_REP);
	for (i = 0; i < frag->ndist; i++)
		frag->dist[i] = le32_to_cpu(dirinfo->dist[i]);
	doutc(cl, "%p %llx.%llx frag %x ndist=%d\n", inode,
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
	struct ceph_client *cl = ceph_inode_to_client(inode);
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
		i = get_random_u32_below(nsplits);
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

	doutc(cl, "%p %llx.%llx\n", inode, ceph_vinop(inode));
	rb_node = rb_first(&ci->i_fragtree);
	for (i = 0; i < nsplits; i++) {
		id = le32_to_cpu(fragtree->splits[i].frag);
		split_by = le32_to_cpu(fragtree->splits[i].by);
		if (split_by == 0 || ceph_frag_bits(id) + split_by > 24) {
			pr_err_client(cl, "%p %llx.%llx invalid split %d/%u, "
			       "frag %x split by %d\n", inode,
			       ceph_vinop(inode), i, nsplits, id, split_by);
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
		doutc(cl, " frag %x split by %d\n", frag->frag, frag->split_by);
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
	struct ceph_fs_client *fsc = ceph_sb_to_fs_client(sb);
	struct ceph_inode_info *ci;
	int i;

	ci = alloc_inode_sb(sb, ceph_inode_cachep, GFP_NOFS);
	if (!ci)
		return NULL;

	doutc(fsc->client, "%p\n", &ci->netfs.inode);

	/* Set parameters for the netfs library */
	netfs_inode_init(&ci->netfs, &ceph_netfs_ops, false);

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
	ci->i_truncate_pagecache_size = 0;

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
#ifdef CONFIG_FS_ENCRYPTION
	ci->fscrypt_auth = NULL;
	ci->fscrypt_auth_len = 0;
#endif
	return &ci->netfs.inode;
}

void ceph_free_inode(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	kfree(ci->i_symlink);
#ifdef CONFIG_FS_ENCRYPTION
	kfree(ci->fscrypt_auth);
#endif
	fscrypt_free_inode(inode);
	kmem_cache_free(ceph_inode_cachep, ci);
}

void ceph_evict_inode(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(inode->i_sb);
	struct ceph_client *cl = ceph_inode_to_client(inode);
	struct ceph_inode_frag *frag;
	struct rb_node *n;

	doutc(cl, "%p ino %llx.%llx\n", inode, ceph_vinop(inode));

	percpu_counter_dec(&mdsc->metric.total_inodes);

	netfs_wait_for_outstanding_io(inode);
	truncate_inode_pages_final(&inode->i_data);
	if (inode->i_state & I_PINNING_NETFS_WB)
		ceph_fscache_unuse_cookie(inode, true);
	clear_inode(inode);

	ceph_fscache_unregister_inode_cookie(ci);
	fscrypt_put_encryption_info(inode);

	__ceph_remove_caps(ci);

	if (__ceph_has_quota(ci, QUOTA_GET_ANY))
		ceph_adjust_quota_realms_count(inode, false);

	/*
	 * we may still have a snap_realm reference if there are stray
	 * caps in i_snap_caps.
	 */
	if (ci->i_snap_realm) {
		if (ceph_snap(inode) == CEPH_NOSNAP) {
			doutc(cl, " dropping residual ref to snap realm %p\n",
			      ci->i_snap_realm);
			ceph_change_snap_realm(inode, NULL);
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
	struct ceph_client *cl = ceph_inode_to_client(inode);
	struct ceph_inode_info *ci = ceph_inode(inode);
	int queue_trunc = 0;
	loff_t isize = i_size_read(inode);

	if (ceph_seq_cmp(truncate_seq, ci->i_truncate_seq) > 0 ||
	    (truncate_seq == ci->i_truncate_seq && size > isize)) {
		doutc(cl, "size %lld -> %llu\n", isize, size);
		if (size > 0 && S_ISDIR(inode->i_mode)) {
			pr_err_client(cl, "non-zero size for directory\n");
			size = 0;
		}
		i_size_write(inode, size);
		inode->i_blocks = calc_inode_blocks(size);
		/*
		 * If we're expanding, then we should be able to just update
		 * the existing cookie.
		 */
		if (size > isize)
			ceph_fscache_update(inode);
		ci->i_reported_size = size;
		if (truncate_seq != ci->i_truncate_seq) {
			doutc(cl, "truncate_seq %u -> %u\n",
			      ci->i_truncate_seq, truncate_seq);
			ci->i_truncate_seq = truncate_seq;

			/* the MDS should have revoked these caps */
			WARN_ON_ONCE(issued & (CEPH_CAP_FILE_RD |
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

	/*
	 * It's possible that the new sizes of the two consecutive
	 * size truncations will be in the same fscrypt last block,
	 * and we need to truncate the corresponding page caches
	 * anyway.
	 */
	if (ceph_seq_cmp(truncate_seq, ci->i_truncate_seq) >= 0) {
		doutc(cl, "truncate_size %lld -> %llu, encrypted %d\n",
		      ci->i_truncate_size, truncate_size,
		      !!IS_ENCRYPTED(inode));

		ci->i_truncate_size = truncate_size;

		if (IS_ENCRYPTED(inode)) {
			doutc(cl, "truncate_pagecache_size %lld -> %llu\n",
			      ci->i_truncate_pagecache_size, size);
			ci->i_truncate_pagecache_size = size;
		} else {
			ci->i_truncate_pagecache_size = truncate_size;
		}
	}
	return queue_trunc;
}

void ceph_fill_file_time(struct inode *inode, int issued,
			 u64 time_warp_seq, struct timespec64 *ctime,
			 struct timespec64 *mtime, struct timespec64 *atime)
{
	struct ceph_client *cl = ceph_inode_to_client(inode);
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct timespec64 ictime = inode_get_ctime(inode);
	int warn = 0;

	if (issued & (CEPH_CAP_FILE_EXCL|
		      CEPH_CAP_FILE_WR|
		      CEPH_CAP_FILE_BUFFER|
		      CEPH_CAP_AUTH_EXCL|
		      CEPH_CAP_XATTR_EXCL)) {
		if (ci->i_version == 0 ||
		    timespec64_compare(ctime, &ictime) > 0) {
			doutc(cl, "ctime %lld.%09ld -> %lld.%09ld inc w/ cap\n",
			     ictime.tv_sec, ictime.tv_nsec,
			     ctime->tv_sec, ctime->tv_nsec);
			inode_set_ctime_to_ts(inode, *ctime);
		}
		if (ci->i_version == 0 ||
		    ceph_seq_cmp(time_warp_seq, ci->i_time_warp_seq) > 0) {
			/* the MDS did a utimes() */
			doutc(cl, "mtime %lld.%09ld -> %lld.%09ld tw %d -> %d\n",
			     inode_get_mtime_sec(inode),
			     inode_get_mtime_nsec(inode),
			     mtime->tv_sec, mtime->tv_nsec,
			     ci->i_time_warp_seq, (int)time_warp_seq);

			inode_set_mtime_to_ts(inode, *mtime);
			inode_set_atime_to_ts(inode, *atime);
			ci->i_time_warp_seq = time_warp_seq;
		} else if (time_warp_seq == ci->i_time_warp_seq) {
			struct timespec64	ts;

			/* nobody did utimes(); take the max */
			ts = inode_get_mtime(inode);
			if (timespec64_compare(mtime, &ts) > 0) {
				doutc(cl, "mtime %lld.%09ld -> %lld.%09ld inc\n",
				     ts.tv_sec, ts.tv_nsec,
				     mtime->tv_sec, mtime->tv_nsec);
				inode_set_mtime_to_ts(inode, *mtime);
			}
			ts = inode_get_atime(inode);
			if (timespec64_compare(atime, &ts) > 0) {
				doutc(cl, "atime %lld.%09ld -> %lld.%09ld inc\n",
				     ts.tv_sec, ts.tv_nsec,
				     atime->tv_sec, atime->tv_nsec);
				inode_set_atime_to_ts(inode, *atime);
			}
		} else if (issued & CEPH_CAP_FILE_EXCL) {
			/* we did a utimes(); ignore mds values */
		} else {
			warn = 1;
		}
	} else {
		/* we have no write|excl caps; whatever the MDS says is true */
		if (ceph_seq_cmp(time_warp_seq, ci->i_time_warp_seq) >= 0) {
			inode_set_ctime_to_ts(inode, *ctime);
			inode_set_mtime_to_ts(inode, *mtime);
			inode_set_atime_to_ts(inode, *atime);
			ci->i_time_warp_seq = time_warp_seq;
		} else {
			warn = 1;
		}
	}
	if (warn) /* time_warp_seq shouldn't go backwards */
		doutc(cl, "%p mds time_warp_seq %llu < %u\n", inode,
		      time_warp_seq, ci->i_time_warp_seq);
}

#if IS_ENABLED(CONFIG_FS_ENCRYPTION)
static int decode_encrypted_symlink(struct ceph_mds_client *mdsc,
				    const char *encsym,
				    int enclen, u8 **decsym)
{
	struct ceph_client *cl = mdsc->fsc->client;
	int declen;
	u8 *sym;

	sym = kmalloc(enclen + 1, GFP_NOFS);
	if (!sym)
		return -ENOMEM;

	declen = ceph_base64_decode(encsym, enclen, sym);
	if (declen < 0) {
		pr_err_client(cl,
			"can't decode symlink (%d). Content: %.*s\n",
			declen, enclen, encsym);
		kfree(sym);
		return -EIO;
	}
	sym[declen + 1] = '\0';
	*decsym = sym;
	return declen;
}
#else
static int decode_encrypted_symlink(struct ceph_mds_client *mdsc,
				    const char *encsym,
				    int symlen, u8 **decsym)
{
	return -EOPNOTSUPP;
}
#endif

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
	struct ceph_client *cl = mdsc->fsc->client;
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
	umode_t mode = le32_to_cpu(info->mode);
	dev_t rdev = le32_to_cpu(info->rdev);

	lockdep_assert_held(&mdsc->snap_rwsem);

	doutc(cl, "%p ino %llx.%llx v %llu had %llu\n", inode, ceph_vinop(inode),
	      le64_to_cpu(info->version), ci->i_version);

	/* Once I_NEW is cleared, we can't change type or dev numbers */
	if (inode->i_state & I_NEW) {
		inode->i_mode = mode;
	} else {
		if (inode_wrong_type(inode, mode)) {
			pr_warn_once_client(cl,
				"inode type changed! (ino %llx.%llx is 0%o, mds says 0%o)\n",
				ceph_vinop(inode), inode->i_mode, mode);
			return -ESTALE;
		}

		if ((S_ISCHR(mode) || S_ISBLK(mode)) && inode->i_rdev != rdev) {
			pr_warn_once_client(cl,
				"dev inode rdev changed! (ino %llx.%llx is %u:%u, mds says %u:%u)\n",
				ceph_vinop(inode), MAJOR(inode->i_rdev),
				MINOR(inode->i_rdev), MAJOR(rdev),
				MINOR(rdev));
			return -ESTALE;
		}
	}

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
			pr_err_client(cl, "ENOMEM xattr blob %d bytes\n",
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

	__ceph_update_quota(ci, iinfo->max_bytes, iinfo->max_files);

#ifdef CONFIG_FS_ENCRYPTION
	if (iinfo->fscrypt_auth_len &&
	    ((inode->i_state & I_NEW) || (ci->fscrypt_auth_len == 0))) {
		kfree(ci->fscrypt_auth);
		ci->fscrypt_auth_len = iinfo->fscrypt_auth_len;
		ci->fscrypt_auth = iinfo->fscrypt_auth;
		iinfo->fscrypt_auth = NULL;
		iinfo->fscrypt_auth_len = 0;
		inode_set_flags(inode, S_ENCRYPTED, S_ENCRYPTED);
	}
#endif

	if ((new_version || (new_issued & CEPH_CAP_AUTH_SHARED)) &&
	    (issued & CEPH_CAP_AUTH_EXCL) == 0) {
		inode->i_mode = mode;
		inode->i_uid = make_kuid(&init_user_ns, le32_to_cpu(info->uid));
		inode->i_gid = make_kgid(&init_user_ns, le32_to_cpu(info->gid));
		doutc(cl, "%p %llx.%llx mode 0%o uid.gid %d.%d\n", inode,
		      ceph_vinop(inode), inode->i_mode,
		      from_kuid(&init_user_ns, inode->i_uid),
		      from_kgid(&init_user_ns, inode->i_gid));
		ceph_decode_timespec64(&ci->i_btime, &iinfo->btime);
		ceph_decode_timespec64(&ci->i_snap_btime, &iinfo->snap_btime);
	}

	/* directories have fl_stripe_unit set to zero */
	if (IS_ENCRYPTED(inode))
		inode->i_blkbits = CEPH_FSCRYPT_BLOCK_SHIFT;
	else if (le32_to_cpu(info->layout.fl_stripe_unit))
		inode->i_blkbits =
			fls(le32_to_cpu(info->layout.fl_stripe_unit)) - 1;
	else
		inode->i_blkbits = CEPH_BLOCK_SHIFT;

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
		u64 size = le64_to_cpu(info->size);
		s64 old_pool = ci->i_layout.pool_id;
		struct ceph_string *old_ns;

		ceph_file_layout_from_legacy(&ci->i_layout, &info->layout);
		old_ns = rcu_dereference_protected(ci->i_layout.pool_ns,
					lockdep_is_held(&ci->i_ceph_lock));
		rcu_assign_pointer(ci->i_layout.pool_ns, pool_ns);

		if (ci->i_layout.pool_id != old_pool || pool_ns != old_ns)
			ci->i_ceph_flags &= ~CEPH_I_POOL_PERM;

		pool_ns = old_ns;

		if (IS_ENCRYPTED(inode) && size &&
		    iinfo->fscrypt_file_len == sizeof(__le64)) {
			u64 fsize = __le64_to_cpu(*(__le64 *)iinfo->fscrypt_file);

			if (size == round_up(fsize, CEPH_FSCRYPT_BLOCK_SIZE)) {
				size = fsize;
			} else {
				pr_warn_client(cl,
					"fscrypt size mismatch: size=%llu fscrypt_file=%llu, discarding fscrypt_file size.\n",
					info->size, size);
			}
		}

		queue_trunc = ceph_fill_file_size(inode, issued,
					le32_to_cpu(info->truncate_seq),
					le64_to_cpu(info->truncate_size),
					size);
		/* only update max_size on auth cap */
		if ((info->cap.flags & CEPH_CAP_FLAG_AUTH) &&
		    ci->i_max_size != le64_to_cpu(info->max_size)) {
			doutc(cl, "max_size %lld -> %llu\n",
			    ci->i_max_size, le64_to_cpu(info->max_size));
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
			ci->i_rsnaps = iinfo->rsnaps;
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
		init_special_inode(inode, inode->i_mode, rdev);
		inode->i_op = &ceph_file_iops;
		break;
	case S_IFREG:
		inode->i_op = &ceph_file_iops;
		inode->i_fop = &ceph_file_fops;
		break;
	case S_IFLNK:
		if (!ci->i_symlink) {
			u32 symlen = iinfo->symlink_len;
			char *sym;

			spin_unlock(&ci->i_ceph_lock);

			if (IS_ENCRYPTED(inode)) {
				if (symlen != i_size_read(inode))
					pr_err_client(cl,
						"%p %llx.%llx BAD symlink size %lld\n",
						inode, ceph_vinop(inode),
						i_size_read(inode));

				err = decode_encrypted_symlink(mdsc, iinfo->symlink,
							       symlen, (u8 **)&sym);
				if (err < 0) {
					pr_err_client(cl,
						"decoding encrypted symlink failed: %d\n",
						err);
					goto out;
				}
				symlen = err;
				i_size_write(inode, symlen);
				inode->i_blocks = calc_inode_blocks(symlen);
			} else {
				if (symlen != i_size_read(inode)) {
					pr_err_client(cl,
						"%p %llx.%llx BAD symlink size %lld\n",
						inode, ceph_vinop(inode),
						i_size_read(inode));
					i_size_write(inode, symlen);
					inode->i_blocks = calc_inode_blocks(symlen);
				}

				err = -ENOMEM;
				sym = kstrndup(iinfo->symlink, symlen, GFP_NOFS);
				if (!sym)
					goto out;
			}

			spin_lock(&ci->i_ceph_lock);
			if (!ci->i_symlink)
				ci->i_symlink = sym;
			else
				kfree(sym); /* lost a race */
		}

		if (IS_ENCRYPTED(inode)) {
			/*
			 * Encrypted symlinks need to be decrypted before we can
			 * cache their targets in i_link. Don't touch it here.
			 */
			inode->i_op = &ceph_encrypted_symlink_iops;
		} else {
			inode->i_link = ci->i_symlink;
			inode->i_op = &ceph_symlink_iops;
		}
		break;
	case S_IFDIR:
		inode->i_op = &ceph_dir_iops;
		inode->i_fop = &ceph_dir_fops;
		break;
	default:
		pr_err_client(cl, "%p %llx.%llx BAD mode 0%o\n", inode,
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
				doutc(cl, " marking %p complete (empty)\n",
				      inode);
				i_size_write(inode, 0);
				__ceph_dir_set_complete(ci,
					atomic64_read(&ci->i_release_count),
					atomic64_read(&ci->i_ordered_count));
			}

			wake = true;
		} else {
			doutc(cl, " %p got snap_caps %s\n", inode,
			      ceph_cap_string(info_caps));
			ci->i_snap_caps |= info_caps;
		}
	}

	if (iinfo->inline_version > 0 &&
	    iinfo->inline_version >= ci->i_inline_version) {
		int cache_caps = CEPH_CAP_FILE_CACHE | CEPH_CAP_FILE_LAZYIO;
		ci->i_inline_version = iinfo->inline_version;
		if (ceph_has_inline_data(ci) &&
		    (locked_page || (info_caps & cache_caps)))
			fill_inline = true;
	}

	if (cap_fmode >= 0) {
		if (!info_caps)
			pr_warn_client(cl, "mds issued no caps on %llx.%llx\n",
				       ceph_vinop(inode));
		__ceph_touch_fmode(ci, mdsc, cap_fmode);
	}

	spin_unlock(&ci->i_ceph_lock);

	ceph_fscache_register_inode_cookie(inode);

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
	struct ceph_client *cl = ceph_inode_to_client(dir);
	struct ceph_dentry_info *di = ceph_dentry(dentry);
	unsigned mask = le16_to_cpu(lease->mask);
	long unsigned duration = le32_to_cpu(lease->duration_ms);
	long unsigned ttl = from_time + (duration * HZ) / 1000;
	long unsigned half_ttl = from_time + (duration * HZ / 2) / 1000;

	doutc(cl, "%p duration %lu ms ttl %lu\n", dentry, duration, ttl);

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

	if (di->lease_gen == atomic_read(&session->s_cap_gen) &&
	    time_before(ttl, di->time))
		return;  /* we already have a newer lease. */

	if (di->lease_session && di->lease_session != session) {
		*old_lease_session = di->lease_session;
		di->lease_session = NULL;
	}

	if (!di->lease_session)
		di->lease_session = ceph_get_mds_session(session);
	di->lease_gen = atomic_read(&session->s_cap_gen);
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
 * caller must hold directory i_rwsem for this to be safe.
 */
static int splice_dentry(struct dentry **pdn, struct inode *in)
{
	struct ceph_client *cl = ceph_inode_to_client(in);
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
		pr_err_client(cl, "error %ld %p inode %p ino %llx.%llx\n",
			      PTR_ERR(realdn), dn, in, ceph_vinop(in));
		return PTR_ERR(realdn);
	}

	if (realdn) {
		doutc(cl, "dn %p (%d) spliced with %p (%d) inode %p ino %llx.%llx\n",
		      dn, d_count(dn), realdn, d_count(realdn),
		      d_inode(realdn), ceph_vinop(d_inode(realdn)));
		dput(dn);
		*pdn = realdn;
	} else {
		BUG_ON(!ceph_dentry(dn));
		doutc(cl, "dn %p attached to %p ino %llx.%llx\n", dn,
		      d_inode(dn), ceph_vinop(d_inode(dn)));
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
	struct ceph_fs_client *fsc = ceph_sb_to_fs_client(sb);
	struct ceph_client *cl = fsc->client;
	int err = 0;

	doutc(cl, "%p is_dentry %d is_target %d\n", req,
	      rinfo->head->is_dentry, rinfo->head->is_target);

	if (!rinfo->head->is_target && !rinfo->head->is_dentry) {
		doutc(cl, "reply is empty!\n");
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
			bool is_nokey = false;
			struct qstr dname;
			struct dentry *dn, *parent;
			struct fscrypt_str oname = FSTR_INIT(NULL, 0);
			struct ceph_fname fname = { .dir	= dir,
						    .name	= rinfo->dname,
						    .ctext	= rinfo->altname,
						    .name_len	= rinfo->dname_len,
						    .ctext_len	= rinfo->altname_len };

			BUG_ON(!rinfo->head->is_target);
			BUG_ON(req->r_dentry);

			parent = d_find_any_alias(dir);
			BUG_ON(!parent);

			err = ceph_fname_alloc_buffer(dir, &oname);
			if (err < 0) {
				dput(parent);
				goto done;
			}

			err = ceph_fname_to_usr(&fname, NULL, &oname, &is_nokey);
			if (err < 0) {
				dput(parent);
				ceph_fname_free_buffer(dir, &oname);
				goto done;
			}
			dname.name = oname.name;
			dname.len = oname.len;
			dname.hash = full_name_hash(parent, dname.name, dname.len);
			tvino.ino = le64_to_cpu(rinfo->targeti.in->ino);
			tvino.snap = le64_to_cpu(rinfo->targeti.in->snapid);
retry_lookup:
			dn = d_lookup(parent, &dname);
			doutc(cl, "d_lookup on parent=%p name=%.*s got %p\n",
			      parent, dname.len, dname.name, dn);

			if (!dn) {
				dn = d_alloc(parent, &dname);
				doutc(cl, "d_alloc %p '%.*s' = %p\n", parent,
				      dname.len, dname.name, dn);
				if (!dn) {
					dput(parent);
					ceph_fname_free_buffer(dir, &oname);
					err = -ENOMEM;
					goto done;
				}
				if (is_nokey) {
					spin_lock(&dn->d_lock);
					dn->d_flags |= DCACHE_NOKEY_NAME;
					spin_unlock(&dn->d_lock);
				}
				err = 0;
			} else if (d_really_is_positive(dn) &&
				   (ceph_ino(d_inode(dn)) != tvino.ino ||
				    ceph_snap(d_inode(dn)) != tvino.snap)) {
				doutc(cl, " dn %p points to wrong inode %p\n",
				      dn, d_inode(dn));
				ceph_dir_clear_ordered(dir);
				d_delete(dn);
				dput(dn);
				goto retry_lookup;
			}
			ceph_fname_free_buffer(dir, &oname);

			req->r_dentry = dn;
			dput(parent);
		}
	}

	if (rinfo->head->is_target) {
		/* Should be filled in by handle_reply */
		BUG_ON(!req->r_target_inode);

		in = req->r_target_inode;
		err = ceph_fill_inode(in, req->r_locked_page, &rinfo->targeti,
				NULL, session,
				(!test_bit(CEPH_MDS_R_ABORTED, &req->r_req_flags) &&
				 !test_bit(CEPH_MDS_R_ASYNC, &req->r_req_flags) &&
				 rinfo->head->result == 0) ?  req->r_fmode : -1,
				&req->r_caps_reservation);
		if (err < 0) {
			pr_err_client(cl, "badness %p %llx.%llx\n", in,
				      ceph_vinop(in));
			req->r_target_inode = NULL;
			if (in->i_state & I_NEW)
				discard_new_inode(in);
			else
				iput(in);
			goto done;
		}
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
			doutc(cl, "no dentry lease or dir cap\n");

		/* rename? */
		if (req->r_old_dentry && req->r_op == CEPH_MDS_OP_RENAME) {
			struct inode *olddir = req->r_old_dentry_dir;
			BUG_ON(!olddir);

			doutc(cl, " src %p '%pd' dst %p '%pd'\n",
			      req->r_old_dentry, req->r_old_dentry, dn, dn);
			doutc(cl, "doing d_move %p -> %p\n", req->r_old_dentry, dn);

			/* d_move screws up sibling dentries' offsets */
			ceph_dir_clear_ordered(dir);
			ceph_dir_clear_ordered(olddir);

			d_move(req->r_old_dentry, dn);
			doutc(cl, " src %p '%pd' dst %p '%pd'\n",
			      req->r_old_dentry, req->r_old_dentry, dn, dn);

			/* ensure target dentry is invalidated, despite
			   rehashing bug in vfs_rename_dir */
			ceph_invalidate_dentry_lease(dn);

			doutc(cl, "dn %p gets new offset %lld\n",
			      req->r_old_dentry,
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
			doutc(cl, "null dentry\n");
			if (d_really_is_positive(dn)) {
				doutc(cl, "d_delete %p\n", dn);
				ceph_dir_clear_ordered(dir);
				d_delete(dn);
			} else if (have_lease) {
				if (d_unhashed(dn))
					d_add(dn, NULL);
			}

			if (!d_unhashed(dn) && have_lease)
				update_dentry_lease(dir, dn,
						    rinfo->dlease, session,
						    req->r_request_started);
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
			doutc(cl, " %p links to %p %llx.%llx, not %llx.%llx\n",
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
		doutc(cl, " final dn %p\n", dn);
	} else if ((req->r_op == CEPH_MDS_OP_LOOKUPSNAP ||
		    req->r_op == CEPH_MDS_OP_MKSNAP) &&
	           test_bit(CEPH_MDS_R_PARENT_LOCKED, &req->r_req_flags) &&
		   !test_bit(CEPH_MDS_R_ABORTED, &req->r_req_flags)) {
		struct inode *dir = req->r_parent;

		/* fill out a snapdir LOOKUPSNAP dentry */
		BUG_ON(!dir);
		BUG_ON(ceph_snap(dir) != CEPH_SNAPDIR);
		BUG_ON(!req->r_dentry);
		doutc(cl, " linking snapped dir %p to dn %p\n", in,
		      req->r_dentry);
		ceph_dir_clear_ordered(dir);
		ihold(in);
		err = splice_dentry(&req->r_dentry, in);
		if (err < 0)
			goto done;
	} else if (rinfo->head->is_dentry && req->r_dentry) {
		/* parent inode is not locked, be careful */
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
	doutc(cl, "done err=%d\n", err);
	return err;
}

/*
 * Prepopulate our cache with readdir results, leases, etc.
 */
static int readdir_prepopulate_inodes_only(struct ceph_mds_request *req,
					   struct ceph_mds_session *session)
{
	struct ceph_mds_reply_info_parsed *rinfo = &req->r_reply_info;
	struct ceph_client *cl = session->s_mdsc->fsc->client;
	int i, err = 0;

	for (i = 0; i < rinfo->dir_nr; i++) {
		struct ceph_mds_reply_dir_entry *rde = rinfo->dir_entries + i;
		struct ceph_vino vino;
		struct inode *in;
		int rc;

		vino.ino = le64_to_cpu(rde->inode.in->ino);
		vino.snap = le64_to_cpu(rde->inode.in->snapid);

		in = ceph_get_inode(req->r_dentry->d_sb, vino, NULL);
		if (IS_ERR(in)) {
			err = PTR_ERR(in);
			doutc(cl, "badness got %d\n", err);
			continue;
		}
		rc = ceph_fill_inode(in, NULL, &rde->inode, NULL, session,
				     -1, &req->r_caps_reservation);
		if (rc < 0) {
			pr_err_client(cl, "inode badness on %p got %d\n", in,
				      rc);
			err = rc;
			if (in->i_state & I_NEW) {
				ihold(in);
				discard_new_inode(in);
			}
		} else if (in->i_state & I_NEW) {
			unlock_new_inode(in);
		}

		iput(in);
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
	struct ceph_client *cl = ceph_inode_to_client(dir);
	struct ceph_inode_info *ci = ceph_inode(dir);
	unsigned nsize = PAGE_SIZE / sizeof(struct dentry*);
	unsigned idx = ctl->index % nsize;
	pgoff_t pgoff = ctl->index / nsize;

	if (!ctl->page || pgoff != ctl->page->index) {
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
		 * i_rwsem, no need to use page lock */
		unlock_page(ctl->page);
		ctl->dentries = kmap(ctl->page);
		if (idx == 0)
			memset(ctl->dentries, 0, PAGE_SIZE);
	}

	if (req->r_dir_release_cnt == atomic64_read(&ci->i_release_count) &&
	    req->r_dir_ordered_cnt == atomic64_read(&ci->i_ordered_count)) {
		doutc(cl, "dn %p idx %d\n", dn, ctl->index);
		ctl->dentries[idx] = dn;
		ctl->index++;
	} else {
		doutc(cl, "disable readdir cache\n");
		ctl->index = -1;
	}
	return 0;
}

int ceph_readdir_prepopulate(struct ceph_mds_request *req,
			     struct ceph_mds_session *session)
{
	struct dentry *parent = req->r_dentry;
	struct inode *inode = d_inode(parent);
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct ceph_mds_reply_info_parsed *rinfo = &req->r_reply_info;
	struct ceph_client *cl = session->s_mdsc->fsc->client;
	struct qstr dname;
	struct dentry *dn;
	struct inode *in;
	int err = 0, skipped = 0, ret, i;
	u32 frag = le32_to_cpu(req->r_args.readdir.frag);
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
			last_hash = le32_to_cpu(req->r_args.readdir.offset_hash);
		}
	}

	if (rinfo->dir_dir &&
	    le32_to_cpu(rinfo->dir_dir->frag) != frag) {
		doutc(cl, "got new frag %x -> %x\n", frag,
			    le32_to_cpu(rinfo->dir_dir->frag));
		frag = le32_to_cpu(rinfo->dir_dir->frag);
		if (!rinfo->hash_order)
			req->r_readdir_offset = 2;
	}

	if (le32_to_cpu(rinfo->head->op) == CEPH_MDS_OP_LSSNAP) {
		doutc(cl, "%d items under SNAPDIR dn %p\n",
		      rinfo->dir_nr, parent);
	} else {
		doutc(cl, "%d items under dn %p\n", rinfo->dir_nr, parent);
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
			u32 hash = ceph_frag_value(rde->raw_hash);
			if (hash != last_hash)
				fpos_offset = 2;
			last_hash = hash;
			rde->offset = ceph_make_fpos(hash, fpos_offset++, true);
		} else {
			rde->offset = ceph_make_fpos(frag, fpos_offset++, false);
		}

retry_lookup:
		dn = d_lookup(parent, &dname);
		doutc(cl, "d_lookup on parent=%p name=%.*s got %p\n",
		      parent, dname.len, dname.name, dn);

		if (!dn) {
			dn = d_alloc(parent, &dname);
			doutc(cl, "d_alloc %p '%.*s' = %p\n", parent,
			      dname.len, dname.name, dn);
			if (!dn) {
				doutc(cl, "d_alloc badness\n");
				err = -ENOMEM;
				goto out;
			}
			if (rde->is_nokey) {
				spin_lock(&dn->d_lock);
				dn->d_flags |= DCACHE_NOKEY_NAME;
				spin_unlock(&dn->d_lock);
			}
		} else if (d_really_is_positive(dn) &&
			   (ceph_ino(d_inode(dn)) != tvino.ino ||
			    ceph_snap(d_inode(dn)) != tvino.snap)) {
			struct ceph_dentry_info *di = ceph_dentry(dn);
			doutc(cl, " dn %p points to wrong inode %p\n",
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
			in = ceph_get_inode(parent->d_sb, tvino, NULL);
			if (IS_ERR(in)) {
				doutc(cl, "new_inode badness\n");
				d_drop(dn);
				dput(dn);
				err = PTR_ERR(in);
				goto out;
			}
		}

		ret = ceph_fill_inode(in, NULL, &rde->inode, NULL, session,
				      -1, &req->r_caps_reservation);
		if (ret < 0) {
			pr_err_client(cl, "badness on %p %llx.%llx\n", in,
				      ceph_vinop(in));
			if (d_really_is_negative(dn)) {
				if (in->i_state & I_NEW) {
					ihold(in);
					discard_new_inode(in);
				}
				iput(in);
			}
			d_drop(dn);
			err = ret;
			goto next_item;
		}
		if (in->i_state & I_NEW)
			unlock_new_inode(in);

		if (d_really_is_negative(dn)) {
			if (ceph_security_xattr_deadlock(in)) {
				doutc(cl, " skip splicing dn %p to inode %p"
				      " (security xattr deadlock)\n", dn, in);
				iput(in);
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
	doutc(cl, "done\n");
	return err;
}

bool ceph_inode_set_size(struct inode *inode, loff_t size)
{
	struct ceph_client *cl = ceph_inode_to_client(inode);
	struct ceph_inode_info *ci = ceph_inode(inode);
	bool ret;

	spin_lock(&ci->i_ceph_lock);
	doutc(cl, "set_size %p %llu -> %llu\n", inode, i_size_read(inode), size);
	i_size_write(inode, size);
	ceph_fscache_update(inode);
	inode->i_blocks = calc_inode_blocks(size);

	ret = __ceph_should_report_size(ci);

	spin_unlock(&ci->i_ceph_lock);

	return ret;
}

void ceph_queue_inode_work(struct inode *inode, int work_bit)
{
	struct ceph_fs_client *fsc = ceph_inode_to_fs_client(inode);
	struct ceph_client *cl = fsc->client;
	struct ceph_inode_info *ci = ceph_inode(inode);
	set_bit(work_bit, &ci->i_work_mask);

	ihold(inode);
	if (queue_work(fsc->inode_wq, &ci->i_work)) {
		doutc(cl, "%p %llx.%llx mask=%lx\n", inode,
		      ceph_vinop(inode), ci->i_work_mask);
	} else {
		doutc(cl, "%p %llx.%llx already queued, mask=%lx\n",
		      inode, ceph_vinop(inode), ci->i_work_mask);
		iput(inode);
	}
}

static void ceph_do_invalidate_pages(struct inode *inode)
{
	struct ceph_client *cl = ceph_inode_to_client(inode);
	struct ceph_inode_info *ci = ceph_inode(inode);
	u32 orig_gen;
	int check = 0;

	ceph_fscache_invalidate(inode, false);

	mutex_lock(&ci->i_truncate_mutex);

	if (ceph_inode_is_shutdown(inode)) {
		pr_warn_ratelimited_client(cl,
			"%p %llx.%llx is shut down\n", inode,
			ceph_vinop(inode));
		mapping_set_error(inode->i_mapping, -EIO);
		truncate_pagecache(inode, 0);
		mutex_unlock(&ci->i_truncate_mutex);
		goto out;
	}

	spin_lock(&ci->i_ceph_lock);
	doutc(cl, "%p %llx.%llx gen %d revoking %d\n", inode,
	      ceph_vinop(inode), ci->i_rdcache_gen, ci->i_rdcache_revoking);
	if (ci->i_rdcache_revoking != ci->i_rdcache_gen) {
		if (__ceph_caps_revoking_other(ci, NULL, CEPH_CAP_FILE_CACHE))
			check = 1;
		spin_unlock(&ci->i_ceph_lock);
		mutex_unlock(&ci->i_truncate_mutex);
		goto out;
	}
	orig_gen = ci->i_rdcache_gen;
	spin_unlock(&ci->i_ceph_lock);

	if (invalidate_inode_pages2(inode->i_mapping) < 0) {
		pr_err_client(cl, "invalidate_inode_pages2 %llx.%llx failed\n",
			      ceph_vinop(inode));
	}

	spin_lock(&ci->i_ceph_lock);
	if (orig_gen == ci->i_rdcache_gen &&
	    orig_gen == ci->i_rdcache_revoking) {
		doutc(cl, "%p %llx.%llx gen %d successful\n", inode,
		      ceph_vinop(inode), ci->i_rdcache_gen);
		ci->i_rdcache_revoking--;
		check = 1;
	} else {
		doutc(cl, "%p %llx.%llx gen %d raced, now %d revoking %d\n",
		      inode, ceph_vinop(inode), orig_gen, ci->i_rdcache_gen,
		      ci->i_rdcache_revoking);
		if (__ceph_caps_revoking_other(ci, NULL, CEPH_CAP_FILE_CACHE))
			check = 1;
	}
	spin_unlock(&ci->i_ceph_lock);
	mutex_unlock(&ci->i_truncate_mutex);
out:
	if (check)
		ceph_check_caps(ci, 0);
}

/*
 * Make sure any pending truncation is applied before doing anything
 * that may depend on it.
 */
void __ceph_do_pending_vmtruncate(struct inode *inode)
{
	struct ceph_client *cl = ceph_inode_to_client(inode);
	struct ceph_inode_info *ci = ceph_inode(inode);
	u64 to;
	int wrbuffer_refs, finish = 0;

	mutex_lock(&ci->i_truncate_mutex);
retry:
	spin_lock(&ci->i_ceph_lock);
	if (ci->i_truncate_pending == 0) {
		doutc(cl, "%p %llx.%llx none pending\n", inode,
		      ceph_vinop(inode));
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
		doutc(cl, "%p %llx.%llx flushing snaps first\n", inode,
		      ceph_vinop(inode));
		filemap_write_and_wait_range(&inode->i_data, 0,
					     inode->i_sb->s_maxbytes);
		goto retry;
	}

	/* there should be no reader or writer */
	WARN_ON_ONCE(ci->i_rd_ref || ci->i_wr_ref);

	to = ci->i_truncate_pagecache_size;
	wrbuffer_refs = ci->i_wrbuffer_ref;
	doutc(cl, "%p %llx.%llx (%d) to %lld\n", inode, ceph_vinop(inode),
	      ci->i_truncate_pending, to);
	spin_unlock(&ci->i_ceph_lock);

	ceph_fscache_resize(inode, to);
	truncate_pagecache(inode, to);

	spin_lock(&ci->i_ceph_lock);
	if (to == ci->i_truncate_pagecache_size) {
		ci->i_truncate_pending = 0;
		finish = 1;
	}
	spin_unlock(&ci->i_ceph_lock);
	if (!finish)
		goto retry;

	mutex_unlock(&ci->i_truncate_mutex);

	if (wrbuffer_refs == 0)
		ceph_check_caps(ci, 0);

	wake_up_all(&ci->i_cap_wq);
}

static void ceph_inode_work(struct work_struct *work)
{
	struct ceph_inode_info *ci = container_of(work, struct ceph_inode_info,
						 i_work);
	struct inode *inode = &ci->netfs.inode;
	struct ceph_client *cl = ceph_inode_to_client(inode);

	if (test_and_clear_bit(CEPH_I_WORK_WRITEBACK, &ci->i_work_mask)) {
		doutc(cl, "writeback %p %llx.%llx\n", inode, ceph_vinop(inode));
		filemap_fdatawrite(&inode->i_data);
	}
	if (test_and_clear_bit(CEPH_I_WORK_INVALIDATE_PAGES, &ci->i_work_mask))
		ceph_do_invalidate_pages(inode);

	if (test_and_clear_bit(CEPH_I_WORK_VMTRUNCATE, &ci->i_work_mask))
		__ceph_do_pending_vmtruncate(inode);

	if (test_and_clear_bit(CEPH_I_WORK_CHECK_CAPS, &ci->i_work_mask))
		ceph_check_caps(ci, 0);

	if (test_and_clear_bit(CEPH_I_WORK_FLUSH_SNAPS, &ci->i_work_mask))
		ceph_flush_snaps(ci, NULL);

	iput(inode);
}

static const char *ceph_encrypted_get_link(struct dentry *dentry,
					   struct inode *inode,
					   struct delayed_call *done)
{
	struct ceph_inode_info *ci = ceph_inode(inode);

	if (!dentry)
		return ERR_PTR(-ECHILD);

	return fscrypt_get_symlink(inode, ci->i_symlink, i_size_read(inode),
				   done);
}

static int ceph_encrypted_symlink_getattr(struct mnt_idmap *idmap,
					  const struct path *path,
					  struct kstat *stat, u32 request_mask,
					  unsigned int query_flags)
{
	int ret;

	ret = ceph_getattr(idmap, path, stat, request_mask, query_flags);
	if (ret)
		return ret;
	return fscrypt_symlink_getattr(path, stat);
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

static const struct inode_operations ceph_encrypted_symlink_iops = {
	.get_link = ceph_encrypted_get_link,
	.setattr = ceph_setattr,
	.getattr = ceph_encrypted_symlink_getattr,
	.listxattr = ceph_listxattr,
};

/*
 * Transfer the encrypted last block to the MDS and the MDS
 * will help update it when truncating a smaller size.
 *
 * We don't support a PAGE_SIZE that is smaller than the
 * CEPH_FSCRYPT_BLOCK_SIZE.
 */
static int fill_fscrypt_truncate(struct inode *inode,
				 struct ceph_mds_request *req,
				 struct iattr *attr)
{
	struct ceph_client *cl = ceph_inode_to_client(inode);
	struct ceph_inode_info *ci = ceph_inode(inode);
	int boff = attr->ia_size % CEPH_FSCRYPT_BLOCK_SIZE;
	loff_t pos, orig_pos = round_down(attr->ia_size,
					  CEPH_FSCRYPT_BLOCK_SIZE);
	u64 block = orig_pos >> CEPH_FSCRYPT_BLOCK_SHIFT;
	struct ceph_pagelist *pagelist = NULL;
	struct kvec iov = {0};
	struct iov_iter iter;
	struct page *page = NULL;
	struct ceph_fscrypt_truncate_size_header header;
	int retry_op = 0;
	int len = CEPH_FSCRYPT_BLOCK_SIZE;
	loff_t i_size = i_size_read(inode);
	int got, ret, issued;
	u64 objver;

	ret = __ceph_get_caps(inode, NULL, CEPH_CAP_FILE_RD, 0, -1, &got);
	if (ret < 0)
		return ret;

	issued = __ceph_caps_issued(ci, NULL);

	doutc(cl, "size %lld -> %lld got cap refs on %s, issued %s\n",
	      i_size, attr->ia_size, ceph_cap_string(got),
	      ceph_cap_string(issued));

	/* Try to writeback the dirty pagecaches */
	if (issued & (CEPH_CAP_FILE_BUFFER)) {
		loff_t lend = orig_pos + CEPH_FSCRYPT_BLOCK_SHIFT - 1;

		ret = filemap_write_and_wait_range(inode->i_mapping,
						   orig_pos, lend);
		if (ret < 0)
			goto out;
	}

	page = __page_cache_alloc(GFP_KERNEL);
	if (page == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	pagelist = ceph_pagelist_alloc(GFP_KERNEL);
	if (!pagelist) {
		ret = -ENOMEM;
		goto out;
	}

	iov.iov_base = kmap_local_page(page);
	iov.iov_len = len;
	iov_iter_kvec(&iter, READ, &iov, 1, len);

	pos = orig_pos;
	ret = __ceph_sync_read(inode, &pos, &iter, &retry_op, &objver);
	if (ret < 0)
		goto out;

	/* Insert the header first */
	header.ver = 1;
	header.compat = 1;
	header.change_attr = cpu_to_le64(inode_peek_iversion_raw(inode));

	/*
	 * Always set the block_size to CEPH_FSCRYPT_BLOCK_SIZE,
	 * because in MDS it may need this to do the truncate.
	 */
	header.block_size = cpu_to_le32(CEPH_FSCRYPT_BLOCK_SIZE);

	/*
	 * If we hit a hole here, we should just skip filling
	 * the fscrypt for the request, because once the fscrypt
	 * is enabled, the file will be split into many blocks
	 * with the size of CEPH_FSCRYPT_BLOCK_SIZE, if there
	 * has a hole, the hole size should be multiple of block
	 * size.
	 *
	 * If the Rados object doesn't exist, it will be set to 0.
	 */
	if (!objver) {
		doutc(cl, "hit hole, ppos %lld < size %lld\n", pos, i_size);

		header.data_len = cpu_to_le32(8 + 8 + 4);
		header.file_offset = 0;
		ret = 0;
	} else {
		header.data_len = cpu_to_le32(8 + 8 + 4 + CEPH_FSCRYPT_BLOCK_SIZE);
		header.file_offset = cpu_to_le64(orig_pos);

		doutc(cl, "encrypt block boff/bsize %d/%lu\n", boff,
		      CEPH_FSCRYPT_BLOCK_SIZE);

		/* truncate and zero out the extra contents for the last block */
		memset(iov.iov_base + boff, 0, PAGE_SIZE - boff);

		/* encrypt the last block */
		ret = ceph_fscrypt_encrypt_block_inplace(inode, page,
						    CEPH_FSCRYPT_BLOCK_SIZE,
						    0, block,
						    GFP_KERNEL);
		if (ret)
			goto out;
	}

	/* Insert the header */
	ret = ceph_pagelist_append(pagelist, &header, sizeof(header));
	if (ret)
		goto out;

	if (header.block_size) {
		/* Append the last block contents to pagelist */
		ret = ceph_pagelist_append(pagelist, iov.iov_base,
					   CEPH_FSCRYPT_BLOCK_SIZE);
		if (ret)
			goto out;
	}
	req->r_pagelist = pagelist;
out:
	doutc(cl, "%p %llx.%llx size dropping cap refs on %s\n", inode,
	      ceph_vinop(inode), ceph_cap_string(got));
	ceph_put_cap_refs(ci, got);
	if (iov.iov_base)
		kunmap_local(iov.iov_base);
	if (page)
		__free_pages(page, 0);
	if (ret && pagelist)
		ceph_pagelist_release(pagelist);
	return ret;
}

int __ceph_setattr(struct mnt_idmap *idmap, struct inode *inode,
		   struct iattr *attr, struct ceph_iattr *cia)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	unsigned int ia_valid = attr->ia_valid;
	struct ceph_mds_request *req;
	struct ceph_mds_client *mdsc = ceph_sb_to_fs_client(inode->i_sb)->mdsc;
	struct ceph_client *cl = ceph_inode_to_client(inode);
	struct ceph_cap_flush *prealloc_cf;
	loff_t isize = i_size_read(inode);
	int issued;
	int release = 0, dirtied = 0;
	int mask = 0;
	int err = 0;
	int inode_dirty_flags = 0;
	bool lock_snap_rwsem = false;
	bool fill_fscrypt;
	int truncate_retry = 20; /* The RMW will take around 50ms */
	struct dentry *dentry;
	char *path;
	int pathlen;
	u64 pathbase;
	bool do_sync = false;

	dentry = d_find_alias(inode);
	if (!dentry) {
		do_sync = true;
	} else {
		path = ceph_mdsc_build_path(mdsc, dentry, &pathlen, &pathbase, 0);
		if (IS_ERR(path)) {
			do_sync = true;
			err = 0;
		} else {
			err = ceph_mds_check_access(mdsc, path, MAY_WRITE);
		}
		ceph_mdsc_free_path(path, pathlen);
		dput(dentry);

		/* For none EACCES cases will let the MDS do the mds auth check */
		if (err == -EACCES) {
			return err;
		} else if (err < 0) {
			do_sync = true;
			err = 0;
		}
	}

retry:
	prealloc_cf = ceph_alloc_cap_flush();
	if (!prealloc_cf)
		return -ENOMEM;

	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_SETATTR,
				       USE_AUTH_MDS);
	if (IS_ERR(req)) {
		ceph_free_cap_flush(prealloc_cf);
		return PTR_ERR(req);
	}

	fill_fscrypt = false;
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

	doutc(cl, "%p %llx.%llx issued %s\n", inode, ceph_vinop(inode),
	      ceph_cap_string(issued));
#if IS_ENABLED(CONFIG_FS_ENCRYPTION)
	if (cia && cia->fscrypt_auth) {
		u32 len = ceph_fscrypt_auth_len(cia->fscrypt_auth);

		if (len > sizeof(*cia->fscrypt_auth)) {
			err = -EINVAL;
			spin_unlock(&ci->i_ceph_lock);
			goto out;
		}

		doutc(cl, "%p %llx.%llx fscrypt_auth len %u to %u)\n", inode,
		      ceph_vinop(inode), ci->fscrypt_auth_len, len);

		/* It should never be re-set once set */
		WARN_ON_ONCE(ci->fscrypt_auth);

		if (!do_sync && (issued & CEPH_CAP_AUTH_EXCL)) {
			dirtied |= CEPH_CAP_AUTH_EXCL;
			kfree(ci->fscrypt_auth);
			ci->fscrypt_auth = (u8 *)cia->fscrypt_auth;
			ci->fscrypt_auth_len = len;
		} else if ((issued & CEPH_CAP_AUTH_SHARED) == 0 ||
			   ci->fscrypt_auth_len != len ||
			   memcmp(ci->fscrypt_auth, cia->fscrypt_auth, len)) {
			req->r_fscrypt_auth = cia->fscrypt_auth;
			mask |= CEPH_SETATTR_FSCRYPT_AUTH;
			release |= CEPH_CAP_AUTH_SHARED;
		}
		cia->fscrypt_auth = NULL;
	}
#else
	if (cia && cia->fscrypt_auth) {
		err = -EINVAL;
		spin_unlock(&ci->i_ceph_lock);
		goto out;
	}
#endif /* CONFIG_FS_ENCRYPTION */

	if (ia_valid & ATTR_UID) {
		kuid_t fsuid = from_vfsuid(idmap, i_user_ns(inode), attr->ia_vfsuid);

		doutc(cl, "%p %llx.%llx uid %d -> %d\n", inode,
		      ceph_vinop(inode),
		      from_kuid(&init_user_ns, inode->i_uid),
		      from_kuid(&init_user_ns, attr->ia_uid));
		if (!do_sync && (issued & CEPH_CAP_AUTH_EXCL)) {
			inode->i_uid = fsuid;
			dirtied |= CEPH_CAP_AUTH_EXCL;
		} else if ((issued & CEPH_CAP_AUTH_SHARED) == 0 ||
			   !uid_eq(fsuid, inode->i_uid)) {
			req->r_args.setattr.uid = cpu_to_le32(
				from_kuid(&init_user_ns, fsuid));
			mask |= CEPH_SETATTR_UID;
			release |= CEPH_CAP_AUTH_SHARED;
		}
	}
	if (ia_valid & ATTR_GID) {
		kgid_t fsgid = from_vfsgid(idmap, i_user_ns(inode), attr->ia_vfsgid);

		doutc(cl, "%p %llx.%llx gid %d -> %d\n", inode,
		      ceph_vinop(inode),
		      from_kgid(&init_user_ns, inode->i_gid),
		      from_kgid(&init_user_ns, attr->ia_gid));
		if (!do_sync && (issued & CEPH_CAP_AUTH_EXCL)) {
			inode->i_gid = fsgid;
			dirtied |= CEPH_CAP_AUTH_EXCL;
		} else if ((issued & CEPH_CAP_AUTH_SHARED) == 0 ||
			   !gid_eq(fsgid, inode->i_gid)) {
			req->r_args.setattr.gid = cpu_to_le32(
				from_kgid(&init_user_ns, fsgid));
			mask |= CEPH_SETATTR_GID;
			release |= CEPH_CAP_AUTH_SHARED;
		}
	}
	if (ia_valid & ATTR_MODE) {
		doutc(cl, "%p %llx.%llx mode 0%o -> 0%o\n", inode,
		      ceph_vinop(inode), inode->i_mode, attr->ia_mode);
		if (!do_sync && (issued & CEPH_CAP_AUTH_EXCL)) {
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
		struct timespec64 atime = inode_get_atime(inode);

		doutc(cl, "%p %llx.%llx atime %lld.%09ld -> %lld.%09ld\n",
		      inode, ceph_vinop(inode),
		      atime.tv_sec, atime.tv_nsec,
		      attr->ia_atime.tv_sec, attr->ia_atime.tv_nsec);
		if (!do_sync && (issued & CEPH_CAP_FILE_EXCL)) {
			ci->i_time_warp_seq++;
			inode_set_atime_to_ts(inode, attr->ia_atime);
			dirtied |= CEPH_CAP_FILE_EXCL;
		} else if (!do_sync && (issued & CEPH_CAP_FILE_WR) &&
			   timespec64_compare(&atime,
					      &attr->ia_atime) < 0) {
			inode_set_atime_to_ts(inode, attr->ia_atime);
			dirtied |= CEPH_CAP_FILE_WR;
		} else if ((issued & CEPH_CAP_FILE_SHARED) == 0 ||
			   !timespec64_equal(&atime, &attr->ia_atime)) {
			ceph_encode_timespec64(&req->r_args.setattr.atime,
					       &attr->ia_atime);
			mask |= CEPH_SETATTR_ATIME;
			release |= CEPH_CAP_FILE_SHARED |
				   CEPH_CAP_FILE_RD | CEPH_CAP_FILE_WR;
		}
	}
	if (ia_valid & ATTR_SIZE) {
		doutc(cl, "%p %llx.%llx size %lld -> %lld\n", inode,
		      ceph_vinop(inode), isize, attr->ia_size);
		/*
		 * Only when the new size is smaller and not aligned to
		 * CEPH_FSCRYPT_BLOCK_SIZE will the RMW is needed.
		 */
		if (IS_ENCRYPTED(inode) && attr->ia_size < isize &&
		    (attr->ia_size % CEPH_FSCRYPT_BLOCK_SIZE)) {
			mask |= CEPH_SETATTR_SIZE;
			release |= CEPH_CAP_FILE_SHARED | CEPH_CAP_FILE_EXCL |
				   CEPH_CAP_FILE_RD | CEPH_CAP_FILE_WR;
			set_bit(CEPH_MDS_R_FSCRYPT_FILE, &req->r_req_flags);
			mask |= CEPH_SETATTR_FSCRYPT_FILE;
			req->r_args.setattr.size =
				cpu_to_le64(round_up(attr->ia_size,
						     CEPH_FSCRYPT_BLOCK_SIZE));
			req->r_args.setattr.old_size =
				cpu_to_le64(round_up(isize,
						     CEPH_FSCRYPT_BLOCK_SIZE));
			req->r_fscrypt_file = attr->ia_size;
			fill_fscrypt = true;
		} else if (!do_sync && (issued & CEPH_CAP_FILE_EXCL) && attr->ia_size >= isize) {
			if (attr->ia_size > isize) {
				i_size_write(inode, attr->ia_size);
				inode->i_blocks = calc_inode_blocks(attr->ia_size);
				ci->i_reported_size = attr->ia_size;
				dirtied |= CEPH_CAP_FILE_EXCL;
				ia_valid |= ATTR_MTIME;
			}
		} else if ((issued & CEPH_CAP_FILE_SHARED) == 0 ||
			   attr->ia_size != isize) {
			mask |= CEPH_SETATTR_SIZE;
			release |= CEPH_CAP_FILE_SHARED | CEPH_CAP_FILE_EXCL |
				   CEPH_CAP_FILE_RD | CEPH_CAP_FILE_WR;
			if (IS_ENCRYPTED(inode) && attr->ia_size) {
				set_bit(CEPH_MDS_R_FSCRYPT_FILE, &req->r_req_flags);
				mask |= CEPH_SETATTR_FSCRYPT_FILE;
				req->r_args.setattr.size =
					cpu_to_le64(round_up(attr->ia_size,
							     CEPH_FSCRYPT_BLOCK_SIZE));
				req->r_args.setattr.old_size =
					cpu_to_le64(round_up(isize,
							     CEPH_FSCRYPT_BLOCK_SIZE));
				req->r_fscrypt_file = attr->ia_size;
			} else {
				req->r_args.setattr.size = cpu_to_le64(attr->ia_size);
				req->r_args.setattr.old_size = cpu_to_le64(isize);
				req->r_fscrypt_file = 0;
			}
		}
	}
	if (ia_valid & ATTR_MTIME) {
		struct timespec64 mtime = inode_get_mtime(inode);

		doutc(cl, "%p %llx.%llx mtime %lld.%09ld -> %lld.%09ld\n",
		      inode, ceph_vinop(inode),
		      mtime.tv_sec, mtime.tv_nsec,
		      attr->ia_mtime.tv_sec, attr->ia_mtime.tv_nsec);
		if (!do_sync && (issued & CEPH_CAP_FILE_EXCL)) {
			ci->i_time_warp_seq++;
			inode_set_mtime_to_ts(inode, attr->ia_mtime);
			dirtied |= CEPH_CAP_FILE_EXCL;
		} else if (!do_sync && (issued & CEPH_CAP_FILE_WR) &&
			   timespec64_compare(&mtime, &attr->ia_mtime) < 0) {
			inode_set_mtime_to_ts(inode, attr->ia_mtime);
			dirtied |= CEPH_CAP_FILE_WR;
		} else if ((issued & CEPH_CAP_FILE_SHARED) == 0 ||
			   !timespec64_equal(&mtime, &attr->ia_mtime)) {
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
		doutc(cl, "%p %llx.%llx ctime %lld.%09ld -> %lld.%09ld (%s)\n",
		      inode, ceph_vinop(inode),
		      inode_get_ctime_sec(inode),
		      inode_get_ctime_nsec(inode),
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
		doutc(cl, "%p %llx.%llx ATTR_FILE ... hrm!\n", inode,
		      ceph_vinop(inode));

	if (dirtied) {
		inode_dirty_flags = __ceph_mark_dirty_caps(ci, dirtied,
							   &prealloc_cf);
		inode_set_ctime_to_ts(inode, attr->ia_ctime);
		inode_inc_iversion_raw(inode);
	}

	release &= issued;
	spin_unlock(&ci->i_ceph_lock);
	if (lock_snap_rwsem) {
		up_read(&mdsc->snap_rwsem);
		lock_snap_rwsem = false;
	}

	if (inode_dirty_flags)
		__mark_inode_dirty(inode, inode_dirty_flags);

	if (mask) {
		req->r_inode = inode;
		ihold(inode);
		req->r_inode_drop = release;
		req->r_args.setattr.mask = cpu_to_le32(mask);
		req->r_num_caps = 1;
		req->r_stamp = attr->ia_ctime;
		if (fill_fscrypt) {
			err = fill_fscrypt_truncate(inode, req, attr);
			if (err)
				goto out;
		}

		/*
		 * The truncate request will return -EAGAIN when the
		 * last block has been updated just before the MDS
		 * successfully gets the xlock for the FILE lock. To
		 * avoid corrupting the file contents we need to retry
		 * it.
		 */
		err = ceph_mdsc_do_request(mdsc, NULL, req);
		if (err == -EAGAIN && truncate_retry--) {
			doutc(cl, "%p %llx.%llx result=%d (%s locally, %d remote), retry it!\n",
			      inode, ceph_vinop(inode), err,
			      ceph_cap_string(dirtied), mask);
			ceph_mdsc_put_request(req);
			ceph_free_cap_flush(prealloc_cf);
			goto retry;
		}
	}
out:
	doutc(cl, "%p %llx.%llx result=%d (%s locally, %d remote)\n", inode,
	      ceph_vinop(inode), err, ceph_cap_string(dirtied), mask);

	ceph_mdsc_put_request(req);
	ceph_free_cap_flush(prealloc_cf);

	if (err >= 0 && (mask & CEPH_SETATTR_SIZE))
		__ceph_do_pending_vmtruncate(inode);

	return err;
}

/*
 * setattr
 */
int ceph_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		 struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct ceph_fs_client *fsc = ceph_inode_to_fs_client(inode);
	int err;

	if (ceph_snap(inode) != CEPH_NOSNAP)
		return -EROFS;

	if (ceph_inode_is_shutdown(inode))
		return -ESTALE;

	err = fscrypt_prepare_setattr(dentry, attr);
	if (err)
		return err;

	err = setattr_prepare(idmap, dentry, attr);
	if (err != 0)
		return err;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size > max(i_size_read(inode), fsc->max_file_size))
		return -EFBIG;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    ceph_quota_is_max_bytes_exceeded(inode, attr->ia_size))
		return -EDQUOT;

	err = __ceph_setattr(idmap, inode, attr, NULL);

	if (err >= 0 && (attr->ia_valid & ATTR_MODE))
		err = posix_acl_chmod(idmap, dentry, attr->ia_mode);

	return err;
}

int ceph_try_to_choose_auth_mds(struct inode *inode, int mask)
{
	int issued = ceph_caps_issued(ceph_inode(inode));

	/*
	 * If any 'x' caps is issued we can just choose the auth MDS
	 * instead of the random replica MDSes. Because only when the
	 * Locker is in LOCK_EXEC state will the loner client could
	 * get the 'x' caps. And if we send the getattr requests to
	 * any replica MDS it must auth pin and tries to rdlock from
	 * the auth MDS, and then the auth MDS need to do the Locker
	 * state transition to LOCK_SYNC. And after that the lock state
	 * will change back.
	 *
	 * This cost much when doing the Locker state transition and
	 * usually will need to revoke caps from clients.
	 *
	 * And for the 'Xs' caps for getxattr we will also choose the
	 * auth MDS, because the MDS side code is buggy due to setxattr
	 * won't notify the replica MDSes when the values changed and
	 * the replica MDS will return the old values. Though we will
	 * fix it in MDS code, but this still makes sense for old ceph.
	 */
	if (((mask & CEPH_CAP_ANY_SHARED) && (issued & CEPH_CAP_ANY_EXCL))
	    || (mask & (CEPH_STAT_RSTAT | CEPH_STAT_CAP_XATTR)))
		return USE_AUTH_MDS;
	else
		return USE_ANY_MDS;
}

/*
 * Verify that we have a lease on the given mask.  If not,
 * do a getattr against an mds.
 */
int __ceph_do_getattr(struct inode *inode, struct page *locked_page,
		      int mask, bool force)
{
	struct ceph_fs_client *fsc = ceph_sb_to_fs_client(inode->i_sb);
	struct ceph_client *cl = fsc->client;
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	int mode;
	int err;

	if (ceph_snap(inode) == CEPH_SNAPDIR) {
		doutc(cl, "inode %p %llx.%llx SNAPDIR\n", inode,
		      ceph_vinop(inode));
		return 0;
	}

	doutc(cl, "inode %p %llx.%llx mask %s mode 0%o\n", inode,
	      ceph_vinop(inode), ceph_cap_string(mask), inode->i_mode);
	if (!force && ceph_caps_issued_mask_metric(ceph_inode(inode), mask, 1))
			return 0;

	mode = ceph_try_to_choose_auth_mds(inode, mask);
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
		} else if (inline_version == CEPH_INLINE_NONE ||
			   inline_version == 1) {
			err = -ENODATA;
		} else {
			err = req->r_reply_info.targeti.inline_len;
		}
	}
	ceph_mdsc_put_request(req);
	doutc(cl, "result=%d\n", err);
	return err;
}

int ceph_do_getvxattr(struct inode *inode, const char *name, void *value,
		      size_t size)
{
	struct ceph_fs_client *fsc = ceph_sb_to_fs_client(inode->i_sb);
	struct ceph_client *cl = fsc->client;
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	int mode = USE_AUTH_MDS;
	int err;
	char *xattr_value;
	size_t xattr_value_len;

	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_GETVXATTR, mode);
	if (IS_ERR(req)) {
		err = -ENOMEM;
		goto out;
	}

	req->r_feature_needed = CEPHFS_FEATURE_OP_GETVXATTR;
	req->r_path2 = kstrdup(name, GFP_NOFS);
	if (!req->r_path2) {
		err = -ENOMEM;
		goto put;
	}

	ihold(inode);
	req->r_inode = inode;
	err = ceph_mdsc_do_request(mdsc, NULL, req);
	if (err < 0)
		goto put;

	xattr_value = req->r_reply_info.xattr_info.xattr_value;
	xattr_value_len = req->r_reply_info.xattr_info.xattr_value_len;

	doutc(cl, "xattr_value_len:%zu, size:%zu\n", xattr_value_len, size);

	err = (int)xattr_value_len;
	if (size == 0)
		goto put;

	if (xattr_value_len > size) {
		err = -ERANGE;
		goto put;
	}

	memcpy(value, xattr_value, xattr_value_len);
put:
	ceph_mdsc_put_request(req);
out:
	doutc(cl, "result=%d\n", err);
	return err;
}


/*
 * Check inode permissions.  We verify we have a valid value for
 * the AUTH cap, then call the generic handler.
 */
int ceph_permission(struct mnt_idmap *idmap, struct inode *inode,
		    int mask)
{
	int err;

	if (mask & MAY_NOT_BLOCK)
		return -ECHILD;

	err = ceph_do_getattr(inode, CEPH_CAP_AUTH_SHARED, false);

	if (!err)
		err = generic_permission(idmap, inode, mask);
	return err;
}

/* Craft a mask of needed caps given a set of requested statx attrs. */
static int statx_to_caps(u32 want, umode_t mode)
{
	int mask = 0;

	if (want & (STATX_MODE|STATX_UID|STATX_GID|STATX_CTIME|STATX_BTIME|STATX_CHANGE_COOKIE))
		mask |= CEPH_CAP_AUTH_SHARED;

	if (want & (STATX_NLINK|STATX_CTIME|STATX_CHANGE_COOKIE)) {
		/*
		 * The link count for directories depends on inode->i_subdirs,
		 * and that is only updated when Fs caps are held.
		 */
		if (S_ISDIR(mode))
			mask |= CEPH_CAP_FILE_SHARED;
		else
			mask |= CEPH_CAP_LINK_SHARED;
	}

	if (want & (STATX_ATIME|STATX_MTIME|STATX_CTIME|STATX_SIZE|STATX_BLOCKS|STATX_CHANGE_COOKIE))
		mask |= CEPH_CAP_FILE_SHARED;

	if (want & (STATX_CTIME|STATX_CHANGE_COOKIE))
		mask |= CEPH_CAP_XATTR_SHARED;

	return mask;
}

/*
 * Get all the attributes. If we have sufficient caps for the requested attrs,
 * then we can avoid talking to the MDS at all.
 */
int ceph_getattr(struct mnt_idmap *idmap, const struct path *path,
		 struct kstat *stat, u32 request_mask, unsigned int flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct super_block *sb = inode->i_sb;
	struct ceph_inode_info *ci = ceph_inode(inode);
	u32 valid_mask = STATX_BASIC_STATS;
	int err = 0;

	if (ceph_inode_is_shutdown(inode))
		return -ESTALE;

	/* Skip the getattr altogether if we're asked not to sync */
	if ((flags & AT_STATX_SYNC_TYPE) != AT_STATX_DONT_SYNC) {
		err = ceph_do_getattr(inode,
				statx_to_caps(request_mask, inode->i_mode),
				flags & AT_STATX_FORCE_SYNC);
		if (err)
			return err;
	}

	generic_fillattr(idmap, request_mask, inode, stat);
	stat->ino = ceph_present_inode(inode);

	/*
	 * btime on newly-allocated inodes is 0, so if this is still set to
	 * that, then assume that it's not valid.
	 */
	if (ci->i_btime.tv_sec || ci->i_btime.tv_nsec) {
		stat->btime = ci->i_btime;
		valid_mask |= STATX_BTIME;
	}

	if (request_mask & STATX_CHANGE_COOKIE) {
		stat->change_cookie = inode_peek_iversion_raw(inode);
		valid_mask |= STATX_CHANGE_COOKIE;
	}

	if (ceph_snap(inode) == CEPH_NOSNAP)
		stat->dev = sb->s_dev;
	else
		stat->dev = ci->i_snapid_map ? ci->i_snapid_map->dev : 0;

	if (S_ISDIR(inode->i_mode)) {
		if (ceph_test_mount_opt(ceph_sb_to_fs_client(sb), RBYTES)) {
			stat->size = ci->i_rbytes;
		} else if (ceph_snap(inode) == CEPH_SNAPDIR) {
			struct ceph_inode_info *pci;
			struct ceph_snap_realm *realm;
			struct inode *parent;

			parent = ceph_lookup_inode(sb, ceph_ino(inode));
			if (IS_ERR(parent))
				return PTR_ERR(parent);

			pci = ceph_inode(parent);
			spin_lock(&pci->i_ceph_lock);
			realm = pci->i_snap_realm;
			if (realm)
				stat->size = realm->num_snaps;
			else
				stat->size = 0;
			spin_unlock(&pci->i_ceph_lock);
			iput(parent);
		} else {
			stat->size = ci->i_files + ci->i_subdirs;
		}
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

	stat->attributes |= STATX_ATTR_CHANGE_MONOTONIC;
	if (IS_ENCRYPTED(inode))
		stat->attributes |= STATX_ATTR_ENCRYPTED;
	stat->attributes_mask |= (STATX_ATTR_CHANGE_MONOTONIC |
				  STATX_ATTR_ENCRYPTED);

	stat->result_mask = request_mask & valid_mask;
	return err;
}

void ceph_inode_shutdown(struct inode *inode)
{
	struct ceph_inode_info *ci = ceph_inode(inode);
	struct rb_node *p;
	int iputs = 0;
	bool invalidate = false;

	spin_lock(&ci->i_ceph_lock);
	ci->i_ceph_flags |= CEPH_I_SHUTDOWN;
	p = rb_first(&ci->i_caps);
	while (p) {
		struct ceph_cap *cap = rb_entry(p, struct ceph_cap, ci_node);

		p = rb_next(p);
		iputs += ceph_purge_inode_cap(inode, cap, &invalidate);
	}
	spin_unlock(&ci->i_ceph_lock);

	if (invalidate)
		ceph_queue_invalidate(inode);
	while (iputs--)
		iput(inode);
}
