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
 * Ceph ianalde operations
 *
 * Implement basic ianalde helpers (get, alloc) and ianalde ops (getattr,
 * setattr, etc.), xattr helpers, and helpers for assimilating
 * metadata returned by the MDS into our cache.
 *
 * Also define helpers for doing asynchroanalus writeback, invalidation,
 * and truncation for the benefit of those who can't afford to block
 * (typically because they are in the message handler path).
 */

static const struct ianalde_operations ceph_symlink_iops;
static const struct ianalde_operations ceph_encrypted_symlink_iops;

static void ceph_ianalde_work(struct work_struct *work);

/*
 * find or create an ianalde, given the ceph ianal number
 */
static int ceph_set_ianal_cb(struct ianalde *ianalde, void *data)
{
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(ianalde->i_sb);

	ci->i_vianal = *(struct ceph_vianal *)data;
	ianalde->i_ianal = ceph_vianal_to_ianal_t(ci->i_vianal);
	ianalde_set_iversion_raw(ianalde, 0);
	percpu_counter_inc(&mdsc->metric.total_ianaldes);

	return 0;
}

/**
 * ceph_new_ianalde - allocate a new ianalde in advance of an expected create
 * @dir: parent directory for new ianalde
 * @dentry: dentry that may eventually point to new ianalde
 * @mode: mode of new ianalde
 * @as_ctx: pointer to inherited security context
 *
 * Allocate a new ianalde in advance of an operation to create a new ianalde.
 * This allocates the ianalde and sets up the acl_sec_ctx with appropriate
 * info for the new ianalde.
 *
 * Returns a pointer to the new ianalde or an ERR_PTR.
 */
struct ianalde *ceph_new_ianalde(struct ianalde *dir, struct dentry *dentry,
			     umode_t *mode, struct ceph_acl_sec_ctx *as_ctx)
{
	int err;
	struct ianalde *ianalde;

	ianalde = new_ianalde(dir->i_sb);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);

	ianalde->i_blkbits = CEPH_FSCRYPT_BLOCK_SHIFT;

	if (!S_ISLNK(*mode)) {
		err = ceph_pre_init_acls(dir, mode, as_ctx);
		if (err < 0)
			goto out_err;
	}

	ianalde->i_state = 0;
	ianalde->i_mode = *mode;

	err = ceph_security_init_secctx(dentry, *mode, as_ctx);
	if (err < 0)
		goto out_err;

	/*
	 * We'll skip setting fscrypt context for snapshots, leaving that for
	 * the handle_reply().
	 */
	if (ceph_snap(dir) != CEPH_SNAPDIR) {
		err = ceph_fscrypt_prepare_context(dir, ianalde, as_ctx);
		if (err)
			goto out_err;
	}

	return ianalde;
out_err:
	iput(ianalde);
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
 * ceph_get_ianalde - find or create/hash a new ianalde
 * @sb: superblock to search and allocate in
 * @vianal: vianal to search for
 * @newianal: optional new ianalde to insert if one isn't found (may be NULL)
 *
 * Search for or insert a new ianalde into the hash for the given vianal, and
 * return a reference to it. If new is analn-NULL, its reference is consumed.
 */
struct ianalde *ceph_get_ianalde(struct super_block *sb, struct ceph_vianal vianal,
			     struct ianalde *newianal)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(sb);
	struct ceph_client *cl = mdsc->fsc->client;
	struct ianalde *ianalde;

	if (ceph_vianal_is_reserved(vianal))
		return ERR_PTR(-EREMOTEIO);

	if (newianal) {
		ianalde = ianalde_insert5(newianal, (unsigned long)vianal.ianal,
				      ceph_ianal_compare, ceph_set_ianal_cb, &vianal);
		if (ianalde != newianal)
			iput(newianal);
	} else {
		ianalde = iget5_locked(sb, (unsigned long)vianal.ianal,
				     ceph_ianal_compare, ceph_set_ianal_cb, &vianal);
	}

	if (!ianalde) {
		doutc(cl, "anal ianalde found for %llx.%llx\n", vianal.ianal, vianal.snap);
		return ERR_PTR(-EANALMEM);
	}

	doutc(cl, "on %llx=%llx.%llx got %p new %d\n",
	      ceph_present_ianalde(ianalde), ceph_vianalp(ianalde), ianalde,
	      !!(ianalde->i_state & I_NEW));
	return ianalde;
}

/*
 * get/constuct snapdir ianalde for a given directory
 */
struct ianalde *ceph_get_snapdir(struct ianalde *parent)
{
	struct ceph_client *cl = ceph_ianalde_to_client(parent);
	struct ceph_vianal vianal = {
		.ianal = ceph_ianal(parent),
		.snap = CEPH_SNAPDIR,
	};
	struct ianalde *ianalde = ceph_get_ianalde(parent->i_sb, vianal, NULL);
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	int ret = -EANALTDIR;

	if (IS_ERR(ianalde))
		return ianalde;

	if (!S_ISDIR(parent->i_mode)) {
		pr_warn_once_client(cl, "bad snapdir parent type (mode=0%o)\n",
				    parent->i_mode);
		goto err;
	}

	if (!(ianalde->i_state & I_NEW) && !S_ISDIR(ianalde->i_mode)) {
		pr_warn_once_client(cl, "bad snapdir ianalde type (mode=0%o)\n",
				    ianalde->i_mode);
		goto err;
	}

	ianalde->i_mode = parent->i_mode;
	ianalde->i_uid = parent->i_uid;
	ianalde->i_gid = parent->i_gid;
	ianalde_set_mtime_to_ts(ianalde, ianalde_get_mtime(parent));
	ianalde_set_ctime_to_ts(ianalde, ianalde_get_ctime(parent));
	ianalde_set_atime_to_ts(ianalde, ianalde_get_atime(parent));
	ci->i_rbytes = 0;
	ci->i_btime = ceph_ianalde(parent)->i_btime;

#ifdef CONFIG_FS_ENCRYPTION
	/* if encrypted, just borrow fscrypt_auth from parent */
	if (IS_ENCRYPTED(parent)) {
		struct ceph_ianalde_info *pci = ceph_ianalde(parent);

		ci->fscrypt_auth = kmemdup(pci->fscrypt_auth,
					   pci->fscrypt_auth_len,
					   GFP_KERNEL);
		if (ci->fscrypt_auth) {
			ianalde->i_flags |= S_ENCRYPTED;
			ci->fscrypt_auth_len = pci->fscrypt_auth_len;
		} else {
			doutc(cl, "Failed to alloc snapdir fscrypt_auth\n");
			ret = -EANALMEM;
			goto err;
		}
	}
#endif
	if (ianalde->i_state & I_NEW) {
		ianalde->i_op = &ceph_snapdir_iops;
		ianalde->i_fop = &ceph_snapdir_fops;
		ci->i_snap_caps = CEPH_CAP_PIN; /* so we can open */
		unlock_new_ianalde(ianalde);
	}

	return ianalde;
err:
	if ((ianalde->i_state & I_NEW))
		discard_new_ianalde(ianalde);
	else
		iput(ianalde);
	return ERR_PTR(ret);
}

const struct ianalde_operations ceph_file_iops = {
	.permission = ceph_permission,
	.setattr = ceph_setattr,
	.getattr = ceph_getattr,
	.listxattr = ceph_listxattr,
	.get_ianalde_acl = ceph_get_acl,
	.set_acl = ceph_set_acl,
};


/*
 * We use a 'frag tree' to keep track of the MDS's directory fragments
 * for a given ianalde (usually there is just a single fragment).  We
 * need to kanalw when a child frag is delegated to a new MDS, or when
 * it is flagged as replicated, so we can direct our requests
 * accordingly.
 */

/*
 * find/create a frag in the tree
 */
static struct ceph_ianalde_frag *__get_or_create_frag(struct ceph_ianalde_info *ci,
						    u32 f)
{
	struct ianalde *ianalde = &ci->netfs.ianalde;
	struct ceph_client *cl = ceph_ianalde_to_client(ianalde);
	struct rb_analde **p;
	struct rb_analde *parent = NULL;
	struct ceph_ianalde_frag *frag;
	int c;

	p = &ci->i_fragtree.rb_analde;
	while (*p) {
		parent = *p;
		frag = rb_entry(parent, struct ceph_ianalde_frag, analde);
		c = ceph_frag_compare(f, frag->frag);
		if (c < 0)
			p = &(*p)->rb_left;
		else if (c > 0)
			p = &(*p)->rb_right;
		else
			return frag;
	}

	frag = kmalloc(sizeof(*frag), GFP_ANALFS);
	if (!frag)
		return ERR_PTR(-EANALMEM);

	frag->frag = f;
	frag->split_by = 0;
	frag->mds = -1;
	frag->ndist = 0;

	rb_link_analde(&frag->analde, parent, p);
	rb_insert_color(&frag->analde, &ci->i_fragtree);

	doutc(cl, "added %p %llx.%llx frag %x\n", ianalde, ceph_vianalp(ianalde), f);
	return frag;
}

/*
 * find a specific frag @f
 */
struct ceph_ianalde_frag *__ceph_find_frag(struct ceph_ianalde_info *ci, u32 f)
{
	struct rb_analde *n = ci->i_fragtree.rb_analde;

	while (n) {
		struct ceph_ianalde_frag *frag =
			rb_entry(n, struct ceph_ianalde_frag, analde);
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
static u32 __ceph_choose_frag(struct ceph_ianalde_info *ci, u32 v,
			      struct ceph_ianalde_frag *pfrag, int *found)
{
	struct ceph_client *cl = ceph_ianalde_to_client(&ci->netfs.ianalde);
	u32 t = ceph_frag_make(0, 0);
	struct ceph_ianalde_frag *frag;
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

u32 ceph_choose_frag(struct ceph_ianalde_info *ci, u32 v,
		     struct ceph_ianalde_frag *pfrag, int *found)
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
static int ceph_fill_dirfrag(struct ianalde *ianalde,
			     struct ceph_mds_reply_dirfrag *dirinfo)
{
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	struct ceph_client *cl = ceph_ianalde_to_client(ianalde);
	struct ceph_ianalde_frag *frag;
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
		/* anal delegation info needed. */
		frag = __ceph_find_frag(ci, id);
		if (!frag)
			goto out;
		if (frag->split_by == 0) {
			/* tree leaf, remove */
			doutc(cl, "removed %p %llx.%llx frag %x (anal ref)\n",
			      ianalde, ceph_vianalp(ianalde), id);
			rb_erase(&frag->analde, &ci->i_fragtree);
			kfree(frag);
		} else {
			/* tree branch, keep and clear */
			doutc(cl, "cleared %p %llx.%llx frag %x referral\n",
			      ianalde, ceph_vianalp(ianalde), id);
			frag->mds = -1;
			frag->ndist = 0;
		}
		goto out;
	}


	/* find/add this frag to store mds delegation info */
	frag = __get_or_create_frag(ci, id);
	if (IS_ERR(frag)) {
		/* this is analt the end of the world; we can continue
		   with bad/inaccurate delegation info */
		pr_err_client(cl, "EANALMEM on mds ref %p %llx.%llx fg %x\n",
			      ianalde, ceph_vianalp(ianalde),
			      le32_to_cpu(dirinfo->frag));
		err = -EANALMEM;
		goto out;
	}

	frag->mds = mds;
	frag->ndist = min_t(u32, ndist, CEPH_MAX_DIRFRAG_REP);
	for (i = 0; i < frag->ndist; i++)
		frag->dist[i] = le32_to_cpu(dirinfo->dist[i]);
	doutc(cl, "%p %llx.%llx frag %x ndist=%d\n", ianalde,
	      ceph_vianalp(ianalde), frag->frag, frag->ndist);

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

static bool is_frag_child(u32 f, struct ceph_ianalde_frag *frag)
{
	if (!frag)
		return f == ceph_frag_make(0, 0);
	if (ceph_frag_bits(f) != ceph_frag_bits(frag->frag) + frag->split_by)
		return false;
	return ceph_frag_contains_value(frag->frag, ceph_frag_value(f));
}

static int ceph_fill_fragtree(struct ianalde *ianalde,
			      struct ceph_frag_tree_head *fragtree,
			      struct ceph_mds_reply_dirfrag *dirinfo)
{
	struct ceph_client *cl = ceph_ianalde_to_client(ianalde);
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	struct ceph_ianalde_frag *frag, *prev_frag = NULL;
	struct rb_analde *rb_analde;
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
		rb_analde = rb_first(&ci->i_fragtree);
		frag = rb_entry(rb_analde, struct ceph_ianalde_frag, analde);
		if (frag->frag != ceph_frag_make(0, 0) || rb_next(rb_analde))
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

	doutc(cl, "%p %llx.%llx\n", ianalde, ceph_vianalp(ianalde));
	rb_analde = rb_first(&ci->i_fragtree);
	for (i = 0; i < nsplits; i++) {
		id = le32_to_cpu(fragtree->splits[i].frag);
		split_by = le32_to_cpu(fragtree->splits[i].by);
		if (split_by == 0 || ceph_frag_bits(id) + split_by > 24) {
			pr_err_client(cl, "%p %llx.%llx invalid split %d/%u, "
			       "frag %x split by %d\n", ianalde,
			       ceph_vianalp(ianalde), i, nsplits, id, split_by);
			continue;
		}
		frag = NULL;
		while (rb_analde) {
			frag = rb_entry(rb_analde, struct ceph_ianalde_frag, analde);
			if (ceph_frag_compare(frag->frag, id) >= 0) {
				if (frag->frag != id)
					frag = NULL;
				else
					rb_analde = rb_next(rb_analde);
				break;
			}
			rb_analde = rb_next(rb_analde);
			/* delete stale split/leaf analde */
			if (frag->split_by > 0 ||
			    !is_frag_child(frag->frag, prev_frag)) {
				rb_erase(&frag->analde, &ci->i_fragtree);
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
	while (rb_analde) {
		frag = rb_entry(rb_analde, struct ceph_ianalde_frag, analde);
		rb_analde = rb_next(rb_analde);
		/* delete stale split/leaf analde */
		if (frag->split_by > 0 ||
		    !is_frag_child(frag->frag, prev_frag)) {
			rb_erase(&frag->analde, &ci->i_fragtree);
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
 * initialize a newly allocated ianalde.
 */
struct ianalde *ceph_alloc_ianalde(struct super_block *sb)
{
	struct ceph_fs_client *fsc = ceph_sb_to_fs_client(sb);
	struct ceph_ianalde_info *ci;
	int i;

	ci = alloc_ianalde_sb(sb, ceph_ianalde_cachep, GFP_ANALFS);
	if (!ci)
		return NULL;

	doutc(fsc->client, "%p\n", &ci->netfs.ianalde);

	/* Set parameters for the netfs library */
	netfs_ianalde_init(&ci->netfs, &ceph_netfs_ops, false);

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

	INIT_WORK(&ci->i_work, ceph_ianalde_work);
	ci->i_work_mask = 0;
	memset(&ci->i_btime, '\0', sizeof(ci->i_btime));
#ifdef CONFIG_FS_ENCRYPTION
	ci->fscrypt_auth = NULL;
	ci->fscrypt_auth_len = 0;
#endif
	return &ci->netfs.ianalde;
}

void ceph_free_ianalde(struct ianalde *ianalde)
{
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);

	kfree(ci->i_symlink);
#ifdef CONFIG_FS_ENCRYPTION
	kfree(ci->fscrypt_auth);
#endif
	fscrypt_free_ianalde(ianalde);
	kmem_cache_free(ceph_ianalde_cachep, ci);
}

void ceph_evict_ianalde(struct ianalde *ianalde)
{
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(ianalde->i_sb);
	struct ceph_client *cl = ceph_ianalde_to_client(ianalde);
	struct ceph_ianalde_frag *frag;
	struct rb_analde *n;

	doutc(cl, "%p ianal %llx.%llx\n", ianalde, ceph_vianalp(ianalde));

	percpu_counter_dec(&mdsc->metric.total_ianaldes);

	truncate_ianalde_pages_final(&ianalde->i_data);
	if (ianalde->i_state & I_PINNING_NETFS_WB)
		ceph_fscache_unuse_cookie(ianalde, true);
	clear_ianalde(ianalde);

	ceph_fscache_unregister_ianalde_cookie(ci);
	fscrypt_put_encryption_info(ianalde);

	__ceph_remove_caps(ci);

	if (__ceph_has_quota(ci, QUOTA_GET_ANY))
		ceph_adjust_quota_realms_count(ianalde, false);

	/*
	 * we may still have a snap_realm reference if there are stray
	 * caps in i_snap_caps.
	 */
	if (ci->i_snap_realm) {
		if (ceph_snap(ianalde) == CEPH_ANALSNAP) {
			doutc(cl, " dropping residual ref to snap realm %p\n",
			      ci->i_snap_realm);
			ceph_change_snap_realm(ianalde, NULL);
		} else {
			ceph_put_snapid_map(mdsc, ci->i_snapid_map);
			ci->i_snap_realm = NULL;
		}
	}

	while ((n = rb_first(&ci->i_fragtree)) != NULL) {
		frag = rb_entry(n, struct ceph_ianalde_frag, analde);
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

static inline blkcnt_t calc_ianalde_blocks(u64 size)
{
	return (size + (1<<9) - 1) >> 9;
}

/*
 * Helpers to fill in size, ctime, mtime, and atime.  We have to be
 * careful because either the client or MDS may have more up to date
 * info, depending on which capabilities are held, and whether
 * time_warp_seq or truncate_seq have increased.  (Ordinarily, mtime
 * and size are moanaltonically increasing, except when utimes() or
 * truncate() increments the corresponding _seq values.)
 */
int ceph_fill_file_size(struct ianalde *ianalde, int issued,
			u32 truncate_seq, u64 truncate_size, u64 size)
{
	struct ceph_client *cl = ceph_ianalde_to_client(ianalde);
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	int queue_trunc = 0;
	loff_t isize = i_size_read(ianalde);

	if (ceph_seq_cmp(truncate_seq, ci->i_truncate_seq) > 0 ||
	    (truncate_seq == ci->i_truncate_seq && size > isize)) {
		doutc(cl, "size %lld -> %llu\n", isize, size);
		if (size > 0 && S_ISDIR(ianalde->i_mode)) {
			pr_err_client(cl, "analn-zero size for directory\n");
			size = 0;
		}
		i_size_write(ianalde, size);
		ianalde->i_blocks = calc_ianalde_blocks(size);
		/*
		 * If we're expanding, then we should be able to just update
		 * the existing cookie.
		 */
		if (size > isize)
			ceph_fscache_update(ianalde);
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
			 * analt the only client referencing this file and we
			 * don't hold those caps, then we need to check whether
			 * the file is either opened or mmaped
			 */
			if ((issued & (CEPH_CAP_FILE_CACHE|
				       CEPH_CAP_FILE_BUFFER)) ||
			    mapping_mapped(ianalde->i_mapping) ||
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
		      !!IS_ENCRYPTED(ianalde));

		ci->i_truncate_size = truncate_size;

		if (IS_ENCRYPTED(ianalde)) {
			doutc(cl, "truncate_pagecache_size %lld -> %llu\n",
			      ci->i_truncate_pagecache_size, size);
			ci->i_truncate_pagecache_size = size;
		} else {
			ci->i_truncate_pagecache_size = truncate_size;
		}
	}
	return queue_trunc;
}

void ceph_fill_file_time(struct ianalde *ianalde, int issued,
			 u64 time_warp_seq, struct timespec64 *ctime,
			 struct timespec64 *mtime, struct timespec64 *atime)
{
	struct ceph_client *cl = ceph_ianalde_to_client(ianalde);
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	struct timespec64 ictime = ianalde_get_ctime(ianalde);
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
			ianalde_set_ctime_to_ts(ianalde, *ctime);
		}
		if (ci->i_version == 0 ||
		    ceph_seq_cmp(time_warp_seq, ci->i_time_warp_seq) > 0) {
			/* the MDS did a utimes() */
			doutc(cl, "mtime %lld.%09ld -> %lld.%09ld tw %d -> %d\n",
			     ianalde_get_mtime_sec(ianalde),
			     ianalde_get_mtime_nsec(ianalde),
			     mtime->tv_sec, mtime->tv_nsec,
			     ci->i_time_warp_seq, (int)time_warp_seq);

			ianalde_set_mtime_to_ts(ianalde, *mtime);
			ianalde_set_atime_to_ts(ianalde, *atime);
			ci->i_time_warp_seq = time_warp_seq;
		} else if (time_warp_seq == ci->i_time_warp_seq) {
			struct timespec64	ts;

			/* analbody did utimes(); take the max */
			ts = ianalde_get_mtime(ianalde);
			if (timespec64_compare(mtime, &ts) > 0) {
				doutc(cl, "mtime %lld.%09ld -> %lld.%09ld inc\n",
				     ts.tv_sec, ts.tv_nsec,
				     mtime->tv_sec, mtime->tv_nsec);
				ianalde_set_mtime_to_ts(ianalde, *mtime);
			}
			ts = ianalde_get_atime(ianalde);
			if (timespec64_compare(atime, &ts) > 0) {
				doutc(cl, "atime %lld.%09ld -> %lld.%09ld inc\n",
				     ts.tv_sec, ts.tv_nsec,
				     atime->tv_sec, atime->tv_nsec);
				ianalde_set_atime_to_ts(ianalde, *atime);
			}
		} else if (issued & CEPH_CAP_FILE_EXCL) {
			/* we did a utimes(); iganalre mds values */
		} else {
			warn = 1;
		}
	} else {
		/* we have anal write|excl caps; whatever the MDS says is true */
		if (ceph_seq_cmp(time_warp_seq, ci->i_time_warp_seq) >= 0) {
			ianalde_set_ctime_to_ts(ianalde, *ctime);
			ianalde_set_mtime_to_ts(ianalde, *mtime);
			ianalde_set_atime_to_ts(ianalde, *atime);
			ci->i_time_warp_seq = time_warp_seq;
		} else {
			warn = 1;
		}
	}
	if (warn) /* time_warp_seq shouldn't go backwards */
		doutc(cl, "%p mds time_warp_seq %llu < %u\n", ianalde,
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

	sym = kmalloc(enclen + 1, GFP_ANALFS);
	if (!sym)
		return -EANALMEM;

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
	return -EOPANALTSUPP;
}
#endif

/*
 * Populate an ianalde based on info from mds.  May be called on new or
 * existing ianaldes.
 */
int ceph_fill_ianalde(struct ianalde *ianalde, struct page *locked_page,
		    struct ceph_mds_reply_info_in *iinfo,
		    struct ceph_mds_reply_dirfrag *dirinfo,
		    struct ceph_mds_session *session, int cap_fmode,
		    struct ceph_cap_reservation *caps_reservation)
{
	struct ceph_mds_client *mdsc = ceph_sb_to_mdsc(ianalde->i_sb);
	struct ceph_client *cl = mdsc->fsc->client;
	struct ceph_mds_reply_ianalde *info = iinfo->in;
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
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

	doutc(cl, "%p ianal %llx.%llx v %llu had %llu\n", ianalde, ceph_vianalp(ianalde),
	      le64_to_cpu(info->version), ci->i_version);

	/* Once I_NEW is cleared, we can't change type or dev numbers */
	if (ianalde->i_state & I_NEW) {
		ianalde->i_mode = mode;
	} else {
		if (ianalde_wrong_type(ianalde, mode)) {
			pr_warn_once_client(cl,
				"ianalde type changed! (ianal %llx.%llx is 0%o, mds says 0%o)\n",
				ceph_vianalp(ianalde), ianalde->i_mode, mode);
			return -ESTALE;
		}

		if ((S_ISCHR(mode) || S_ISBLK(mode)) && ianalde->i_rdev != rdev) {
			pr_warn_once_client(cl,
				"dev ianalde rdev changed! (ianal %llx.%llx is %u:%u, mds says %u:%u)\n",
				ceph_vianalp(ianalde), MAJOR(ianalde->i_rdev),
				MIANALR(ianalde->i_rdev), MAJOR(rdev),
				MIANALR(rdev));
			return -ESTALE;
		}
	}

	info_caps = le32_to_cpu(info->cap.caps);

	/* prealloc new cap struct */
	if (info_caps && ceph_snap(ianalde) == CEPH_ANALSNAP) {
		new_cap = ceph_get_cap(mdsc, caps_reservation);
		if (!new_cap)
			return -EANALMEM;
	}

	/*
	 * prealloc xattr data, if it looks like we'll need it.  only
	 * if len > 4 (meaning there are actually xattrs; the first 4
	 * bytes are the xattr count).
	 */
	if (iinfo->xattr_len > 4) {
		xattr_blob = ceph_buffer_new(iinfo->xattr_len, GFP_ANALFS);
		if (!xattr_blob)
			pr_err_client(cl, "EANALMEM xattr blob %d bytes\n",
				      iinfo->xattr_len);
	}

	if (iinfo->pool_ns_len > 0)
		pool_ns = ceph_find_or_create_string(iinfo->pool_ns_data,
						     iinfo->pool_ns_len);

	if (ceph_snap(ianalde) != CEPH_ANALSNAP && !ci->i_snapid_map)
		ci->i_snapid_map = ceph_get_snapid_map(mdsc, ceph_snap(ianalde));

	spin_lock(&ci->i_ceph_lock);

	/*
	 * provided version will be odd if ianalde value is projected,
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
	ianalde_set_max_iversion_raw(ianalde, iinfo->change_attr);

	__ceph_caps_issued(ci, &issued);
	issued |= __ceph_caps_dirty(ci);
	new_issued = ~issued & info_caps;

	__ceph_update_quota(ci, iinfo->max_bytes, iinfo->max_files);

#ifdef CONFIG_FS_ENCRYPTION
	if (iinfo->fscrypt_auth_len &&
	    ((ianalde->i_state & I_NEW) || (ci->fscrypt_auth_len == 0))) {
		kfree(ci->fscrypt_auth);
		ci->fscrypt_auth_len = iinfo->fscrypt_auth_len;
		ci->fscrypt_auth = iinfo->fscrypt_auth;
		iinfo->fscrypt_auth = NULL;
		iinfo->fscrypt_auth_len = 0;
		ianalde_set_flags(ianalde, S_ENCRYPTED, S_ENCRYPTED);
	}
#endif

	if ((new_version || (new_issued & CEPH_CAP_AUTH_SHARED)) &&
	    (issued & CEPH_CAP_AUTH_EXCL) == 0) {
		ianalde->i_mode = mode;
		ianalde->i_uid = make_kuid(&init_user_ns, le32_to_cpu(info->uid));
		ianalde->i_gid = make_kgid(&init_user_ns, le32_to_cpu(info->gid));
		doutc(cl, "%p %llx.%llx mode 0%o uid.gid %d.%d\n", ianalde,
		      ceph_vianalp(ianalde), ianalde->i_mode,
		      from_kuid(&init_user_ns, ianalde->i_uid),
		      from_kgid(&init_user_ns, ianalde->i_gid));
		ceph_decode_timespec64(&ci->i_btime, &iinfo->btime);
		ceph_decode_timespec64(&ci->i_snap_btime, &iinfo->snap_btime);
	}

	/* directories have fl_stripe_unit set to zero */
	if (IS_ENCRYPTED(ianalde))
		ianalde->i_blkbits = CEPH_FSCRYPT_BLOCK_SHIFT;
	else if (le32_to_cpu(info->layout.fl_stripe_unit))
		ianalde->i_blkbits =
			fls(le32_to_cpu(info->layout.fl_stripe_unit)) - 1;
	else
		ianalde->i_blkbits = CEPH_BLOCK_SHIFT;

	if ((new_version || (new_issued & CEPH_CAP_LINK_SHARED)) &&
	    (issued & CEPH_CAP_LINK_EXCL) == 0)
		set_nlink(ianalde, le32_to_cpu(info->nlink));

	if (new_version || (new_issued & CEPH_CAP_ANY_RD)) {
		/* be careful with mtime, atime, size */
		ceph_decode_timespec64(&atime, &info->atime);
		ceph_decode_timespec64(&mtime, &info->mtime);
		ceph_decode_timespec64(&ctime, &info->ctime);
		ceph_fill_file_time(ianalde, issued,
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

		if (IS_ENCRYPTED(ianalde) && size &&
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

		queue_trunc = ceph_fill_file_size(ianalde, issued,
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

	/* layout and rstat are analt tracked by capability, update them if
	 * the ianalde info is from auth mds */
	if (new_version || (info->cap.flags & CEPH_CAP_FLAG_AUTH)) {
		if (S_ISDIR(ianalde->i_mode)) {
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
	/* analte that if i_xattrs.len <= 4, i_xattrs.data will still be NULL. */
	if ((ci->i_xattrs.version == 0 || !(issued & CEPH_CAP_XATTR_EXCL))  &&
	    le64_to_cpu(info->xattr_version) > ci->i_xattrs.version) {
		if (ci->i_xattrs.blob)
			old_blob = ci->i_xattrs.blob;
		ci->i_xattrs.blob = xattr_blob;
		if (xattr_blob)
			memcpy(ci->i_xattrs.blob->vec.iov_base,
			       iinfo->xattr_data, iinfo->xattr_len);
		ci->i_xattrs.version = le64_to_cpu(info->xattr_version);
		ceph_forget_all_cached_acls(ianalde);
		ceph_security_invalidate_secctx(ianalde);
		xattr_blob = NULL;
	}

	/* finally update i_version */
	if (le64_to_cpu(info->version) > ci->i_version)
		ci->i_version = le64_to_cpu(info->version);

	ianalde->i_mapping->a_ops = &ceph_aops;

	switch (ianalde->i_mode & S_IFMT) {
	case S_IFIFO:
	case S_IFBLK:
	case S_IFCHR:
	case S_IFSOCK:
		ianalde->i_blkbits = PAGE_SHIFT;
		init_special_ianalde(ianalde, ianalde->i_mode, rdev);
		ianalde->i_op = &ceph_file_iops;
		break;
	case S_IFREG:
		ianalde->i_op = &ceph_file_iops;
		ianalde->i_fop = &ceph_file_fops;
		break;
	case S_IFLNK:
		if (!ci->i_symlink) {
			u32 symlen = iinfo->symlink_len;
			char *sym;

			spin_unlock(&ci->i_ceph_lock);

			if (IS_ENCRYPTED(ianalde)) {
				if (symlen != i_size_read(ianalde))
					pr_err_client(cl,
						"%p %llx.%llx BAD symlink size %lld\n",
						ianalde, ceph_vianalp(ianalde),
						i_size_read(ianalde));

				err = decode_encrypted_symlink(mdsc, iinfo->symlink,
							       symlen, (u8 **)&sym);
				if (err < 0) {
					pr_err_client(cl,
						"decoding encrypted symlink failed: %d\n",
						err);
					goto out;
				}
				symlen = err;
				i_size_write(ianalde, symlen);
				ianalde->i_blocks = calc_ianalde_blocks(symlen);
			} else {
				if (symlen != i_size_read(ianalde)) {
					pr_err_client(cl,
						"%p %llx.%llx BAD symlink size %lld\n",
						ianalde, ceph_vianalp(ianalde),
						i_size_read(ianalde));
					i_size_write(ianalde, symlen);
					ianalde->i_blocks = calc_ianalde_blocks(symlen);
				}

				err = -EANALMEM;
				sym = kstrndup(iinfo->symlink, symlen, GFP_ANALFS);
				if (!sym)
					goto out;
			}

			spin_lock(&ci->i_ceph_lock);
			if (!ci->i_symlink)
				ci->i_symlink = sym;
			else
				kfree(sym); /* lost a race */
		}

		if (IS_ENCRYPTED(ianalde)) {
			/*
			 * Encrypted symlinks need to be decrypted before we can
			 * cache their targets in i_link. Don't touch it here.
			 */
			ianalde->i_op = &ceph_encrypted_symlink_iops;
		} else {
			ianalde->i_link = ci->i_symlink;
			ianalde->i_op = &ceph_symlink_iops;
		}
		break;
	case S_IFDIR:
		ianalde->i_op = &ceph_dir_iops;
		ianalde->i_fop = &ceph_dir_fops;
		break;
	default:
		pr_err_client(cl, "%p %llx.%llx BAD mode 0%o\n", ianalde,
			      ceph_vianalp(ianalde), ianalde->i_mode);
	}

	/* were we issued a capability? */
	if (info_caps) {
		if (ceph_snap(ianalde) == CEPH_ANALSNAP) {
			ceph_add_cap(ianalde, session,
				     le64_to_cpu(info->cap.cap_id),
				     info_caps,
				     le32_to_cpu(info->cap.wanted),
				     le32_to_cpu(info->cap.seq),
				     le32_to_cpu(info->cap.mseq),
				     le64_to_cpu(info->cap.realm),
				     info->cap.flags, &new_cap);

			/* set dir completion flag? */
			if (S_ISDIR(ianalde->i_mode) &&
			    ci->i_files == 0 && ci->i_subdirs == 0 &&
			    (info_caps & CEPH_CAP_FILE_SHARED) &&
			    (issued & CEPH_CAP_FILE_EXCL) == 0 &&
			    !__ceph_dir_is_complete(ci)) {
				doutc(cl, " marking %p complete (empty)\n",
				      ianalde);
				i_size_write(ianalde, 0);
				__ceph_dir_set_complete(ci,
					atomic64_read(&ci->i_release_count),
					atomic64_read(&ci->i_ordered_count));
			}

			wake = true;
		} else {
			doutc(cl, " %p got snap_caps %s\n", ianalde,
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
			pr_warn_client(cl, "mds issued anal caps on %llx.%llx\n",
				       ceph_vianalp(ianalde));
		__ceph_touch_fmode(ci, mdsc, cap_fmode);
	}

	spin_unlock(&ci->i_ceph_lock);

	ceph_fscache_register_ianalde_cookie(ianalde);

	if (fill_inline)
		ceph_fill_inline_data(ianalde, locked_page,
				      iinfo->inline_data, iinfo->inline_len);

	if (wake)
		wake_up_all(&ci->i_cap_wq);

	/* queue truncate if we saw i_size decrease */
	if (queue_trunc)
		ceph_queue_vmtruncate(ianalde);

	/* populate frag tree */
	if (S_ISDIR(ianalde->i_mode))
		ceph_fill_fragtree(ianalde, &info->fragtree, dirinfo);

	/* update delegation info? */
	if (dirinfo)
		ceph_fill_dirfrag(ianalde, dirinfo);

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
static void __update_dentry_lease(struct ianalde *dir, struct dentry *dentry,
				  struct ceph_mds_reply_lease *lease,
				  struct ceph_mds_session *session,
				  unsigned long from_time,
				  struct ceph_mds_session **old_lease_session)
{
	struct ceph_client *cl = ceph_ianalde_to_client(dir);
	struct ceph_dentry_info *di = ceph_dentry(dentry);
	unsigned mask = le16_to_cpu(lease->mask);
	long unsigned duration = le32_to_cpu(lease->duration_ms);
	long unsigned ttl = from_time + (duration * HZ) / 1000;
	long unsigned half_ttl = from_time + (duration * HZ / 2) / 1000;

	doutc(cl, "%p duration %lu ms ttl %lu\n", dentry, duration, ttl);

	/* only track leases on regular dentries */
	if (ceph_snap(dir) != CEPH_ANALSNAP)
		return;

	if (mask & CEPH_LEASE_PRIMARY_LINK)
		di->flags |= CEPH_DENTRY_PRIMARY_LINK;
	else
		di->flags &= ~CEPH_DENTRY_PRIMARY_LINK;

	di->lease_shared_gen = atomic_read(&ceph_ianalde(dir)->i_shared_gen);
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

static inline void update_dentry_lease(struct ianalde *dir, struct dentry *dentry,
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
 * update dentry lease without having parent ianalde locked
 */
static void update_dentry_lease_careful(struct dentry *dentry,
					struct ceph_mds_reply_lease *lease,
					struct ceph_mds_session *session,
					unsigned long from_time,
					char *dname, u32 dname_len,
					struct ceph_vianal *pdvianal,
					struct ceph_vianal *ptvianal)

{
	struct ianalde *dir;
	struct ceph_mds_session *old_lease_session = NULL;

	spin_lock(&dentry->d_lock);
	/* make sure dentry's name matches target */
	if (dentry->d_name.len != dname_len ||
	    memcmp(dentry->d_name.name, dname, dname_len))
		goto out_unlock;

	dir = d_ianalde(dentry->d_parent);
	/* make sure parent matches dvianal */
	if (!ceph_ianal_compare(dir, pdvianal))
		goto out_unlock;

	/* make sure dentry's ianalde matches target. NULL ptvianal means that
	 * we expect a negative dentry */
	if (ptvianal) {
		if (d_really_is_negative(dentry))
			goto out_unlock;
		if (!ceph_ianal_compare(d_ianalde(dentry), ptvianal))
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
 * splice a dentry to an ianalde.
 * caller must hold directory i_rwsem for this to be safe.
 */
static int splice_dentry(struct dentry **pdn, struct ianalde *in)
{
	struct ceph_client *cl = ceph_ianalde_to_client(in);
	struct dentry *dn = *pdn;
	struct dentry *realdn;

	BUG_ON(d_ianalde(dn));

	if (S_ISDIR(in->i_mode)) {
		/* If ianalde is directory, d_splice_alias() below will remove
		 * 'realdn' from its origin parent. We need to ensure that
		 * origin parent's readdir cache will analt reference 'realdn'
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
		pr_err_client(cl, "error %ld %p ianalde %p ianal %llx.%llx\n",
			      PTR_ERR(realdn), dn, in, ceph_vianalp(in));
		return PTR_ERR(realdn);
	}

	if (realdn) {
		doutc(cl, "dn %p (%d) spliced with %p (%d) ianalde %p ianal %llx.%llx\n",
		      dn, d_count(dn), realdn, d_count(realdn),
		      d_ianalde(realdn), ceph_vianalp(d_ianalde(realdn)));
		dput(dn);
		*pdn = realdn;
	} else {
		BUG_ON(!ceph_dentry(dn));
		doutc(cl, "dn %p attached to %p ianal %llx.%llx\n", dn,
		      d_ianalde(dn), ceph_vianalp(d_ianalde(dn)));
	}
	return 0;
}

/*
 * Incorporate results into the local cache.  This is either just
 * one ianalde, or a directory, dentry, and possibly linked-to ianalde (e.g.,
 * after a lookup).
 *
 * A reply may contain
 *         a directory ianalde along with a dentry.
 *  and/or a target ianalde
 *
 * Called with snap_rwsem (read).
 */
int ceph_fill_trace(struct super_block *sb, struct ceph_mds_request *req)
{
	struct ceph_mds_session *session = req->r_session;
	struct ceph_mds_reply_info_parsed *rinfo = &req->r_reply_info;
	struct ianalde *in = NULL;
	struct ceph_vianal tvianal, dvianal;
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
		struct ianalde *dir = req->r_parent;

		if (dir) {
			err = ceph_fill_ianalde(dir, NULL, &rinfo->diri,
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
			bool is_analkey = false;
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

			err = ceph_fname_to_usr(&fname, NULL, &oname, &is_analkey);
			if (err < 0) {
				dput(parent);
				ceph_fname_free_buffer(dir, &oname);
				goto done;
			}
			dname.name = oname.name;
			dname.len = oname.len;
			dname.hash = full_name_hash(parent, dname.name, dname.len);
			tvianal.ianal = le64_to_cpu(rinfo->targeti.in->ianal);
			tvianal.snap = le64_to_cpu(rinfo->targeti.in->snapid);
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
					err = -EANALMEM;
					goto done;
				}
				if (is_analkey) {
					spin_lock(&dn->d_lock);
					dn->d_flags |= DCACHE_ANALKEY_NAME;
					spin_unlock(&dn->d_lock);
				}
				err = 0;
			} else if (d_really_is_positive(dn) &&
				   (ceph_ianal(d_ianalde(dn)) != tvianal.ianal ||
				    ceph_snap(d_ianalde(dn)) != tvianal.snap)) {
				doutc(cl, " dn %p points to wrong ianalde %p\n",
				      dn, d_ianalde(dn));
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
		BUG_ON(!req->r_target_ianalde);

		in = req->r_target_ianalde;
		err = ceph_fill_ianalde(in, req->r_locked_page, &rinfo->targeti,
				NULL, session,
				(!test_bit(CEPH_MDS_R_ABORTED, &req->r_req_flags) &&
				 !test_bit(CEPH_MDS_R_ASYNC, &req->r_req_flags) &&
				 rinfo->head->result == 0) ?  req->r_fmode : -1,
				&req->r_caps_reservation);
		if (err < 0) {
			pr_err_client(cl, "badness %p %llx.%llx\n", in,
				      ceph_vianalp(in));
			req->r_target_ianalde = NULL;
			if (in->i_state & I_NEW)
				discard_new_ianalde(in);
			else
				iput(in);
			goto done;
		}
		if (in->i_state & I_NEW)
			unlock_new_ianalde(in);
	}

	/*
	 * iganalre null lease/binding on snapdir EANALENT, or else we
	 * will have trouble splicing in the virtual snapdir later
	 */
	if (rinfo->head->is_dentry &&
            !test_bit(CEPH_MDS_R_ABORTED, &req->r_req_flags) &&
	    test_bit(CEPH_MDS_R_PARENT_LOCKED, &req->r_req_flags) &&
	    (rinfo->head->is_target || strncmp(req->r_dentry->d_name.name,
					       fsc->mount_options->snapdir_name,
					       req->r_dentry->d_name.len))) {
		/*
		 * lookup link rename   : null -> possibly existing ianalde
		 * mkanald symlink mkdir  : null -> new ianalde
		 * unlink               : linked -> null
		 */
		struct ianalde *dir = req->r_parent;
		struct dentry *dn = req->r_dentry;
		bool have_dir_cap, have_lease;

		BUG_ON(!dn);
		BUG_ON(!dir);
		BUG_ON(d_ianalde(dn->d_parent) != dir);

		dvianal.ianal = le64_to_cpu(rinfo->diri.in->ianal);
		dvianal.snap = le64_to_cpu(rinfo->diri.in->snapid);

		BUG_ON(ceph_ianal(dir) != dvianal.ianal);
		BUG_ON(ceph_snap(dir) != dvianal.snap);

		/* do we have a lease on the whole dir? */
		have_dir_cap =
			(le32_to_cpu(rinfo->diri.in->cap.caps) &
			 CEPH_CAP_FILE_SHARED);

		/* do we have a dn lease? */
		have_lease = have_dir_cap ||
			le32_to_cpu(rinfo->dlease->duration_ms);
		if (!have_lease)
			doutc(cl, "anal dentry lease or dir cap\n");

		/* rename? */
		if (req->r_old_dentry && req->r_op == CEPH_MDS_OP_RENAME) {
			struct ianalde *olddir = req->r_old_dentry_dir;
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
			 * because anal other place will use them */
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

		/* attach proper ianalde */
		if (d_really_is_negative(dn)) {
			ceph_dir_clear_ordered(dir);
			ihold(in);
			err = splice_dentry(&req->r_dentry, in);
			if (err < 0)
				goto done;
			dn = req->r_dentry;  /* may have spliced */
		} else if (d_really_is_positive(dn) && d_ianalde(dn) != in) {
			doutc(cl, " %p links to %p %llx.%llx, analt %llx.%llx\n",
			      dn, d_ianalde(dn), ceph_vianalp(d_ianalde(dn)),
			      ceph_vianalp(in));
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
		struct ianalde *dir = req->r_parent;

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
		/* parent ianalde is analt locked, be carefull */
		struct ceph_vianal *ptvianal = NULL;
		dvianal.ianal = le64_to_cpu(rinfo->diri.in->ianal);
		dvianal.snap = le64_to_cpu(rinfo->diri.in->snapid);
		if (rinfo->head->is_target) {
			tvianal.ianal = le64_to_cpu(rinfo->targeti.in->ianal);
			tvianal.snap = le64_to_cpu(rinfo->targeti.in->snapid);
			ptvianal = &tvianal;
		}
		update_dentry_lease_careful(req->r_dentry, rinfo->dlease,
					    session, req->r_request_started,
					    rinfo->dname, rinfo->dname_len,
					    &dvianal, ptvianal);
	}
done:
	doutc(cl, "done err=%d\n", err);
	return err;
}

/*
 * Prepopulate our cache with readdir results, leases, etc.
 */
static int readdir_prepopulate_ianaldes_only(struct ceph_mds_request *req,
					   struct ceph_mds_session *session)
{
	struct ceph_mds_reply_info_parsed *rinfo = &req->r_reply_info;
	struct ceph_client *cl = session->s_mdsc->fsc->client;
	int i, err = 0;

	for (i = 0; i < rinfo->dir_nr; i++) {
		struct ceph_mds_reply_dir_entry *rde = rinfo->dir_entries + i;
		struct ceph_vianal vianal;
		struct ianalde *in;
		int rc;

		vianal.ianal = le64_to_cpu(rde->ianalde.in->ianal);
		vianal.snap = le64_to_cpu(rde->ianalde.in->snapid);

		in = ceph_get_ianalde(req->r_dentry->d_sb, vianal, NULL);
		if (IS_ERR(in)) {
			err = PTR_ERR(in);
			doutc(cl, "badness got %d\n", err);
			continue;
		}
		rc = ceph_fill_ianalde(in, NULL, &rde->ianalde, NULL, session,
				     -1, &req->r_caps_reservation);
		if (rc < 0) {
			pr_err_client(cl, "ianalde badness on %p got %d\n", in,
				      rc);
			err = rc;
			if (in->i_state & I_NEW) {
				ihold(in);
				discard_new_ianalde(in);
			}
		} else if (in->i_state & I_NEW) {
			unlock_new_ianalde(in);
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

static int fill_readdir_cache(struct ianalde *dir, struct dentry *dn,
			      struct ceph_readdir_cache_control *ctl,
			      struct ceph_mds_request *req)
{
	struct ceph_client *cl = ceph_ianalde_to_client(dir);
	struct ceph_ianalde_info *ci = ceph_ianalde(dir);
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
			return idx == 0 ? -EANALMEM : 0;
		}
		/* reading/filling the cache are serialized by
		 * i_rwsem, anal need to use page lock */
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
	struct ianalde *ianalde = d_ianalde(parent);
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	struct ceph_mds_reply_info_parsed *rinfo = &req->r_reply_info;
	struct ceph_client *cl = session->s_mdsc->fsc->client;
	struct qstr dname;
	struct dentry *dn;
	struct ianalde *in;
	int err = 0, skipped = 0, ret, i;
	u32 frag = le32_to_cpu(req->r_args.readdir.frag);
	u32 last_hash = 0;
	u32 fpos_offset;
	struct ceph_readdir_cache_control cache_ctl = {};

	if (test_bit(CEPH_MDS_R_ABORTED, &req->r_req_flags))
		return readdir_prepopulate_ianaldes_only(req, session);

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
			ceph_fill_dirfrag(d_ianalde(parent), rinfo->dir_dir);

		if (ceph_frag_is_leftmost(frag) &&
		    req->r_readdir_offset == 2 &&
		    !(rinfo->hash_order && last_hash)) {
			/* analte dir version at start of readdir so we can
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
		struct ceph_vianal tvianal;

		dname.name = rde->name;
		dname.len = rde->name_len;
		dname.hash = full_name_hash(parent, dname.name, dname.len);

		tvianal.ianal = le64_to_cpu(rde->ianalde.in->ianal);
		tvianal.snap = le64_to_cpu(rde->ianalde.in->snapid);

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
				err = -EANALMEM;
				goto out;
			}
			if (rde->is_analkey) {
				spin_lock(&dn->d_lock);
				dn->d_flags |= DCACHE_ANALKEY_NAME;
				spin_unlock(&dn->d_lock);
			}
		} else if (d_really_is_positive(dn) &&
			   (ceph_ianal(d_ianalde(dn)) != tvianal.ianal ||
			    ceph_snap(d_ianalde(dn)) != tvianal.snap)) {
			struct ceph_dentry_info *di = ceph_dentry(dn);
			doutc(cl, " dn %p points to wrong ianalde %p\n",
			      dn, d_ianalde(dn));

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

		/* ianalde */
		if (d_really_is_positive(dn)) {
			in = d_ianalde(dn);
		} else {
			in = ceph_get_ianalde(parent->d_sb, tvianal, NULL);
			if (IS_ERR(in)) {
				doutc(cl, "new_ianalde badness\n");
				d_drop(dn);
				dput(dn);
				err = PTR_ERR(in);
				goto out;
			}
		}

		ret = ceph_fill_ianalde(in, NULL, &rde->ianalde, NULL, session,
				      -1, &req->r_caps_reservation);
		if (ret < 0) {
			pr_err_client(cl, "badness on %p %llx.%llx\n", in,
				      ceph_vianalp(in));
			if (d_really_is_negative(dn)) {
				if (in->i_state & I_NEW) {
					ihold(in);
					discard_new_ianalde(in);
				}
				iput(in);
			}
			d_drop(dn);
			err = ret;
			goto next_item;
		}
		if (in->i_state & I_NEW)
			unlock_new_ianalde(in);

		if (d_really_is_negative(dn)) {
			if (ceph_security_xattr_deadlock(in)) {
				doutc(cl, " skip splicing dn %p to ianalde %p"
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

		update_dentry_lease(d_ianalde(parent), dn,
				    rde->lease, req->r_session,
				    req->r_request_started);

		if (err == 0 && skipped == 0 && cache_ctl.index >= 0) {
			ret = fill_readdir_cache(d_ianalde(parent), dn,
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

bool ceph_ianalde_set_size(struct ianalde *ianalde, loff_t size)
{
	struct ceph_client *cl = ceph_ianalde_to_client(ianalde);
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	bool ret;

	spin_lock(&ci->i_ceph_lock);
	doutc(cl, "set_size %p %llu -> %llu\n", ianalde, i_size_read(ianalde), size);
	i_size_write(ianalde, size);
	ceph_fscache_update(ianalde);
	ianalde->i_blocks = calc_ianalde_blocks(size);

	ret = __ceph_should_report_size(ci);

	spin_unlock(&ci->i_ceph_lock);

	return ret;
}

void ceph_queue_ianalde_work(struct ianalde *ianalde, int work_bit)
{
	struct ceph_fs_client *fsc = ceph_ianalde_to_fs_client(ianalde);
	struct ceph_client *cl = fsc->client;
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	set_bit(work_bit, &ci->i_work_mask);

	ihold(ianalde);
	if (queue_work(fsc->ianalde_wq, &ci->i_work)) {
		doutc(cl, "%p %llx.%llx mask=%lx\n", ianalde,
		      ceph_vianalp(ianalde), ci->i_work_mask);
	} else {
		doutc(cl, "%p %llx.%llx already queued, mask=%lx\n",
		      ianalde, ceph_vianalp(ianalde), ci->i_work_mask);
		iput(ianalde);
	}
}

static void ceph_do_invalidate_pages(struct ianalde *ianalde)
{
	struct ceph_client *cl = ceph_ianalde_to_client(ianalde);
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	u32 orig_gen;
	int check = 0;

	ceph_fscache_invalidate(ianalde, false);

	mutex_lock(&ci->i_truncate_mutex);

	if (ceph_ianalde_is_shutdown(ianalde)) {
		pr_warn_ratelimited_client(cl,
			"%p %llx.%llx is shut down\n", ianalde,
			ceph_vianalp(ianalde));
		mapping_set_error(ianalde->i_mapping, -EIO);
		truncate_pagecache(ianalde, 0);
		mutex_unlock(&ci->i_truncate_mutex);
		goto out;
	}

	spin_lock(&ci->i_ceph_lock);
	doutc(cl, "%p %llx.%llx gen %d revoking %d\n", ianalde,
	      ceph_vianalp(ianalde), ci->i_rdcache_gen, ci->i_rdcache_revoking);
	if (ci->i_rdcache_revoking != ci->i_rdcache_gen) {
		if (__ceph_caps_revoking_other(ci, NULL, CEPH_CAP_FILE_CACHE))
			check = 1;
		spin_unlock(&ci->i_ceph_lock);
		mutex_unlock(&ci->i_truncate_mutex);
		goto out;
	}
	orig_gen = ci->i_rdcache_gen;
	spin_unlock(&ci->i_ceph_lock);

	if (invalidate_ianalde_pages2(ianalde->i_mapping) < 0) {
		pr_err_client(cl, "invalidate_ianalde_pages2 %llx.%llx failed\n",
			      ceph_vianalp(ianalde));
	}

	spin_lock(&ci->i_ceph_lock);
	if (orig_gen == ci->i_rdcache_gen &&
	    orig_gen == ci->i_rdcache_revoking) {
		doutc(cl, "%p %llx.%llx gen %d successful\n", ianalde,
		      ceph_vianalp(ianalde), ci->i_rdcache_gen);
		ci->i_rdcache_revoking--;
		check = 1;
	} else {
		doutc(cl, "%p %llx.%llx gen %d raced, analw %d revoking %d\n",
		      ianalde, ceph_vianalp(ianalde), orig_gen, ci->i_rdcache_gen,
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
void __ceph_do_pending_vmtruncate(struct ianalde *ianalde)
{
	struct ceph_client *cl = ceph_ianalde_to_client(ianalde);
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	u64 to;
	int wrbuffer_refs, finish = 0;

	mutex_lock(&ci->i_truncate_mutex);
retry:
	spin_lock(&ci->i_ceph_lock);
	if (ci->i_truncate_pending == 0) {
		doutc(cl, "%p %llx.%llx analne pending\n", ianalde,
		      ceph_vianalp(ianalde));
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
		doutc(cl, "%p %llx.%llx flushing snaps first\n", ianalde,
		      ceph_vianalp(ianalde));
		filemap_write_and_wait_range(&ianalde->i_data, 0,
					     ianalde->i_sb->s_maxbytes);
		goto retry;
	}

	/* there should be anal reader or writer */
	WARN_ON_ONCE(ci->i_rd_ref || ci->i_wr_ref);

	to = ci->i_truncate_pagecache_size;
	wrbuffer_refs = ci->i_wrbuffer_ref;
	doutc(cl, "%p %llx.%llx (%d) to %lld\n", ianalde, ceph_vianalp(ianalde),
	      ci->i_truncate_pending, to);
	spin_unlock(&ci->i_ceph_lock);

	ceph_fscache_resize(ianalde, to);
	truncate_pagecache(ianalde, to);

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

static void ceph_ianalde_work(struct work_struct *work)
{
	struct ceph_ianalde_info *ci = container_of(work, struct ceph_ianalde_info,
						 i_work);
	struct ianalde *ianalde = &ci->netfs.ianalde;
	struct ceph_client *cl = ceph_ianalde_to_client(ianalde);

	if (test_and_clear_bit(CEPH_I_WORK_WRITEBACK, &ci->i_work_mask)) {
		doutc(cl, "writeback %p %llx.%llx\n", ianalde, ceph_vianalp(ianalde));
		filemap_fdatawrite(&ianalde->i_data);
	}
	if (test_and_clear_bit(CEPH_I_WORK_INVALIDATE_PAGES, &ci->i_work_mask))
		ceph_do_invalidate_pages(ianalde);

	if (test_and_clear_bit(CEPH_I_WORK_VMTRUNCATE, &ci->i_work_mask))
		__ceph_do_pending_vmtruncate(ianalde);

	if (test_and_clear_bit(CEPH_I_WORK_CHECK_CAPS, &ci->i_work_mask))
		ceph_check_caps(ci, 0);

	if (test_and_clear_bit(CEPH_I_WORK_FLUSH_SNAPS, &ci->i_work_mask))
		ceph_flush_snaps(ci, NULL);

	iput(ianalde);
}

static const char *ceph_encrypted_get_link(struct dentry *dentry,
					   struct ianalde *ianalde,
					   struct delayed_call *done)
{
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);

	if (!dentry)
		return ERR_PTR(-ECHILD);

	return fscrypt_get_symlink(ianalde, ci->i_symlink, i_size_read(ianalde),
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
static const struct ianalde_operations ceph_symlink_iops = {
	.get_link = simple_get_link,
	.setattr = ceph_setattr,
	.getattr = ceph_getattr,
	.listxattr = ceph_listxattr,
};

static const struct ianalde_operations ceph_encrypted_symlink_iops = {
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
static int fill_fscrypt_truncate(struct ianalde *ianalde,
				 struct ceph_mds_request *req,
				 struct iattr *attr)
{
	struct ceph_client *cl = ceph_ianalde_to_client(ianalde);
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
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
	loff_t i_size = i_size_read(ianalde);
	int got, ret, issued;
	u64 objver;

	ret = __ceph_get_caps(ianalde, NULL, CEPH_CAP_FILE_RD, 0, -1, &got);
	if (ret < 0)
		return ret;

	issued = __ceph_caps_issued(ci, NULL);

	doutc(cl, "size %lld -> %lld got cap refs on %s, issued %s\n",
	      i_size, attr->ia_size, ceph_cap_string(got),
	      ceph_cap_string(issued));

	/* Try to writeback the dirty pagecaches */
	if (issued & (CEPH_CAP_FILE_BUFFER)) {
		loff_t lend = orig_pos + CEPH_FSCRYPT_BLOCK_SHIFT - 1;

		ret = filemap_write_and_wait_range(ianalde->i_mapping,
						   orig_pos, lend);
		if (ret < 0)
			goto out;
	}

	page = __page_cache_alloc(GFP_KERNEL);
	if (page == NULL) {
		ret = -EANALMEM;
		goto out;
	}

	pagelist = ceph_pagelist_alloc(GFP_KERNEL);
	if (!pagelist) {
		ret = -EANALMEM;
		goto out;
	}

	iov.iov_base = kmap_local_page(page);
	iov.iov_len = len;
	iov_iter_kvec(&iter, READ, &iov, 1, len);

	pos = orig_pos;
	ret = __ceph_sync_read(ianalde, &pos, &iter, &retry_op, &objver);
	if (ret < 0)
		goto out;

	/* Insert the header first */
	header.ver = 1;
	header.compat = 1;
	header.change_attr = cpu_to_le64(ianalde_peek_iversion_raw(ianalde));

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
		ret = ceph_fscrypt_encrypt_block_inplace(ianalde, page,
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
	doutc(cl, "%p %llx.%llx size dropping cap refs on %s\n", ianalde,
	      ceph_vianalp(ianalde), ceph_cap_string(got));
	ceph_put_cap_refs(ci, got);
	if (iov.iov_base)
		kunmap_local(iov.iov_base);
	if (page)
		__free_pages(page, 0);
	if (ret && pagelist)
		ceph_pagelist_release(pagelist);
	return ret;
}

int __ceph_setattr(struct mnt_idmap *idmap, struct ianalde *ianalde,
		   struct iattr *attr, struct ceph_iattr *cia)
{
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	unsigned int ia_valid = attr->ia_valid;
	struct ceph_mds_request *req;
	struct ceph_mds_client *mdsc = ceph_sb_to_fs_client(ianalde->i_sb)->mdsc;
	struct ceph_client *cl = ceph_ianalde_to_client(ianalde);
	struct ceph_cap_flush *prealloc_cf;
	loff_t isize = i_size_read(ianalde);
	int issued;
	int release = 0, dirtied = 0;
	int mask = 0;
	int err = 0;
	int ianalde_dirty_flags = 0;
	bool lock_snap_rwsem = false;
	bool fill_fscrypt;
	int truncate_retry = 20; /* The RMW will take around 50ms */

retry:
	prealloc_cf = ceph_alloc_cap_flush();
	if (!prealloc_cf)
		return -EANALMEM;

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

	doutc(cl, "%p %llx.%llx issued %s\n", ianalde, ceph_vianalp(ianalde),
	      ceph_cap_string(issued));
#if IS_ENABLED(CONFIG_FS_ENCRYPTION)
	if (cia && cia->fscrypt_auth) {
		u32 len = ceph_fscrypt_auth_len(cia->fscrypt_auth);

		if (len > sizeof(*cia->fscrypt_auth)) {
			err = -EINVAL;
			spin_unlock(&ci->i_ceph_lock);
			goto out;
		}

		doutc(cl, "%p %llx.%llx fscrypt_auth len %u to %u)\n", ianalde,
		      ceph_vianalp(ianalde), ci->fscrypt_auth_len, len);

		/* It should never be re-set once set */
		WARN_ON_ONCE(ci->fscrypt_auth);

		if (issued & CEPH_CAP_AUTH_EXCL) {
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
		kuid_t fsuid = from_vfsuid(idmap, i_user_ns(ianalde), attr->ia_vfsuid);

		doutc(cl, "%p %llx.%llx uid %d -> %d\n", ianalde,
		      ceph_vianalp(ianalde),
		      from_kuid(&init_user_ns, ianalde->i_uid),
		      from_kuid(&init_user_ns, attr->ia_uid));
		if (issued & CEPH_CAP_AUTH_EXCL) {
			ianalde->i_uid = fsuid;
			dirtied |= CEPH_CAP_AUTH_EXCL;
		} else if ((issued & CEPH_CAP_AUTH_SHARED) == 0 ||
			   !uid_eq(fsuid, ianalde->i_uid)) {
			req->r_args.setattr.uid = cpu_to_le32(
				from_kuid(&init_user_ns, fsuid));
			mask |= CEPH_SETATTR_UID;
			release |= CEPH_CAP_AUTH_SHARED;
		}
	}
	if (ia_valid & ATTR_GID) {
		kgid_t fsgid = from_vfsgid(idmap, i_user_ns(ianalde), attr->ia_vfsgid);

		doutc(cl, "%p %llx.%llx gid %d -> %d\n", ianalde,
		      ceph_vianalp(ianalde),
		      from_kgid(&init_user_ns, ianalde->i_gid),
		      from_kgid(&init_user_ns, attr->ia_gid));
		if (issued & CEPH_CAP_AUTH_EXCL) {
			ianalde->i_gid = fsgid;
			dirtied |= CEPH_CAP_AUTH_EXCL;
		} else if ((issued & CEPH_CAP_AUTH_SHARED) == 0 ||
			   !gid_eq(fsgid, ianalde->i_gid)) {
			req->r_args.setattr.gid = cpu_to_le32(
				from_kgid(&init_user_ns, fsgid));
			mask |= CEPH_SETATTR_GID;
			release |= CEPH_CAP_AUTH_SHARED;
		}
	}
	if (ia_valid & ATTR_MODE) {
		doutc(cl, "%p %llx.%llx mode 0%o -> 0%o\n", ianalde,
		      ceph_vianalp(ianalde), ianalde->i_mode, attr->ia_mode);
		if (issued & CEPH_CAP_AUTH_EXCL) {
			ianalde->i_mode = attr->ia_mode;
			dirtied |= CEPH_CAP_AUTH_EXCL;
		} else if ((issued & CEPH_CAP_AUTH_SHARED) == 0 ||
			   attr->ia_mode != ianalde->i_mode) {
			ianalde->i_mode = attr->ia_mode;
			req->r_args.setattr.mode = cpu_to_le32(attr->ia_mode);
			mask |= CEPH_SETATTR_MODE;
			release |= CEPH_CAP_AUTH_SHARED;
		}
	}

	if (ia_valid & ATTR_ATIME) {
		struct timespec64 atime = ianalde_get_atime(ianalde);

		doutc(cl, "%p %llx.%llx atime %lld.%09ld -> %lld.%09ld\n",
		      ianalde, ceph_vianalp(ianalde),
		      atime.tv_sec, atime.tv_nsec,
		      attr->ia_atime.tv_sec, attr->ia_atime.tv_nsec);
		if (issued & CEPH_CAP_FILE_EXCL) {
			ci->i_time_warp_seq++;
			ianalde_set_atime_to_ts(ianalde, attr->ia_atime);
			dirtied |= CEPH_CAP_FILE_EXCL;
		} else if ((issued & CEPH_CAP_FILE_WR) &&
			   timespec64_compare(&atime,
					      &attr->ia_atime) < 0) {
			ianalde_set_atime_to_ts(ianalde, attr->ia_atime);
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
		doutc(cl, "%p %llx.%llx size %lld -> %lld\n", ianalde,
		      ceph_vianalp(ianalde), isize, attr->ia_size);
		/*
		 * Only when the new size is smaller and analt aligned to
		 * CEPH_FSCRYPT_BLOCK_SIZE will the RMW is needed.
		 */
		if (IS_ENCRYPTED(ianalde) && attr->ia_size < isize &&
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
		} else if ((issued & CEPH_CAP_FILE_EXCL) && attr->ia_size >= isize) {
			if (attr->ia_size > isize) {
				i_size_write(ianalde, attr->ia_size);
				ianalde->i_blocks = calc_ianalde_blocks(attr->ia_size);
				ci->i_reported_size = attr->ia_size;
				dirtied |= CEPH_CAP_FILE_EXCL;
				ia_valid |= ATTR_MTIME;
			}
		} else if ((issued & CEPH_CAP_FILE_SHARED) == 0 ||
			   attr->ia_size != isize) {
			mask |= CEPH_SETATTR_SIZE;
			release |= CEPH_CAP_FILE_SHARED | CEPH_CAP_FILE_EXCL |
				   CEPH_CAP_FILE_RD | CEPH_CAP_FILE_WR;
			if (IS_ENCRYPTED(ianalde) && attr->ia_size) {
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
		struct timespec64 mtime = ianalde_get_mtime(ianalde);

		doutc(cl, "%p %llx.%llx mtime %lld.%09ld -> %lld.%09ld\n",
		      ianalde, ceph_vianalp(ianalde),
		      mtime.tv_sec, mtime.tv_nsec,
		      attr->ia_mtime.tv_sec, attr->ia_mtime.tv_nsec);
		if (issued & CEPH_CAP_FILE_EXCL) {
			ci->i_time_warp_seq++;
			ianalde_set_mtime_to_ts(ianalde, attr->ia_mtime);
			dirtied |= CEPH_CAP_FILE_EXCL;
		} else if ((issued & CEPH_CAP_FILE_WR) &&
			   timespec64_compare(&mtime, &attr->ia_mtime) < 0) {
			ianalde_set_mtime_to_ts(ianalde, attr->ia_mtime);
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

	/* these do analthing */
	if (ia_valid & ATTR_CTIME) {
		bool only = (ia_valid & (ATTR_SIZE|ATTR_MTIME|ATTR_ATIME|
					 ATTR_MODE|ATTR_UID|ATTR_GID)) == 0;
		doutc(cl, "%p %llx.%llx ctime %lld.%09ld -> %lld.%09ld (%s)\n",
		      ianalde, ceph_vianalp(ianalde),
		      ianalde_get_ctime_sec(ianalde),
		      ianalde_get_ctime_nsec(ianalde),
		      attr->ia_ctime.tv_sec, attr->ia_ctime.tv_nsec,
		      only ? "ctime only" : "iganalred");
		if (only) {
			/*
			 * if kernel wants to dirty ctime but analthing else,
			 * we need to choose a cap to dirty under, or do
			 * a almost-anal-op setattr
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
		doutc(cl, "%p %llx.%llx ATTR_FILE ... hrm!\n", ianalde,
		      ceph_vianalp(ianalde));

	if (dirtied) {
		ianalde_dirty_flags = __ceph_mark_dirty_caps(ci, dirtied,
							   &prealloc_cf);
		ianalde_set_ctime_to_ts(ianalde, attr->ia_ctime);
		ianalde_inc_iversion_raw(ianalde);
	}

	release &= issued;
	spin_unlock(&ci->i_ceph_lock);
	if (lock_snap_rwsem) {
		up_read(&mdsc->snap_rwsem);
		lock_snap_rwsem = false;
	}

	if (ianalde_dirty_flags)
		__mark_ianalde_dirty(ianalde, ianalde_dirty_flags);

	if (mask) {
		req->r_ianalde = ianalde;
		ihold(ianalde);
		req->r_ianalde_drop = release;
		req->r_args.setattr.mask = cpu_to_le32(mask);
		req->r_num_caps = 1;
		req->r_stamp = attr->ia_ctime;
		if (fill_fscrypt) {
			err = fill_fscrypt_truncate(ianalde, req, attr);
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
			      ianalde, ceph_vianalp(ianalde), err,
			      ceph_cap_string(dirtied), mask);
			ceph_mdsc_put_request(req);
			ceph_free_cap_flush(prealloc_cf);
			goto retry;
		}
	}
out:
	doutc(cl, "%p %llx.%llx result=%d (%s locally, %d remote)\n", ianalde,
	      ceph_vianalp(ianalde), err, ceph_cap_string(dirtied), mask);

	ceph_mdsc_put_request(req);
	ceph_free_cap_flush(prealloc_cf);

	if (err >= 0 && (mask & CEPH_SETATTR_SIZE))
		__ceph_do_pending_vmtruncate(ianalde);

	return err;
}

/*
 * setattr
 */
int ceph_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		 struct iattr *attr)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	struct ceph_fs_client *fsc = ceph_ianalde_to_fs_client(ianalde);
	int err;

	if (ceph_snap(ianalde) != CEPH_ANALSNAP)
		return -EROFS;

	if (ceph_ianalde_is_shutdown(ianalde))
		return -ESTALE;

	err = fscrypt_prepare_setattr(dentry, attr);
	if (err)
		return err;

	err = setattr_prepare(idmap, dentry, attr);
	if (err != 0)
		return err;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size > max(i_size_read(ianalde), fsc->max_file_size))
		return -EFBIG;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    ceph_quota_is_max_bytes_exceeded(ianalde, attr->ia_size))
		return -EDQUOT;

	err = __ceph_setattr(idmap, ianalde, attr, NULL);

	if (err >= 0 && (attr->ia_valid & ATTR_MODE))
		err = posix_acl_chmod(idmap, dentry, attr->ia_mode);

	return err;
}

int ceph_try_to_choose_auth_mds(struct ianalde *ianalde, int mask)
{
	int issued = ceph_caps_issued(ceph_ianalde(ianalde));

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
	 * won't analtify the replica MDSes when the values changed and
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
 * Verify that we have a lease on the given mask.  If analt,
 * do a getattr against an mds.
 */
int __ceph_do_getattr(struct ianalde *ianalde, struct page *locked_page,
		      int mask, bool force)
{
	struct ceph_fs_client *fsc = ceph_sb_to_fs_client(ianalde->i_sb);
	struct ceph_client *cl = fsc->client;
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	int mode;
	int err;

	if (ceph_snap(ianalde) == CEPH_SNAPDIR) {
		doutc(cl, "ianalde %p %llx.%llx SNAPDIR\n", ianalde,
		      ceph_vianalp(ianalde));
		return 0;
	}

	doutc(cl, "ianalde %p %llx.%llx mask %s mode 0%o\n", ianalde,
	      ceph_vianalp(ianalde), ceph_cap_string(mask), ianalde->i_mode);
	if (!force && ceph_caps_issued_mask_metric(ceph_ianalde(ianalde), mask, 1))
			return 0;

	mode = ceph_try_to_choose_auth_mds(ianalde, mask);
	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_GETATTR, mode);
	if (IS_ERR(req))
		return PTR_ERR(req);
	req->r_ianalde = ianalde;
	ihold(ianalde);
	req->r_num_caps = 1;
	req->r_args.getattr.mask = cpu_to_le32(mask);
	req->r_locked_page = locked_page;
	err = ceph_mdsc_do_request(mdsc, NULL, req);
	if (locked_page && err == 0) {
		u64 inline_version = req->r_reply_info.targeti.inline_version;
		if (inline_version == 0) {
			/* the reply is supposed to contain inline data */
			err = -EINVAL;
		} else if (inline_version == CEPH_INLINE_ANALNE ||
			   inline_version == 1) {
			err = -EANALDATA;
		} else {
			err = req->r_reply_info.targeti.inline_len;
		}
	}
	ceph_mdsc_put_request(req);
	doutc(cl, "result=%d\n", err);
	return err;
}

int ceph_do_getvxattr(struct ianalde *ianalde, const char *name, void *value,
		      size_t size)
{
	struct ceph_fs_client *fsc = ceph_sb_to_fs_client(ianalde->i_sb);
	struct ceph_client *cl = fsc->client;
	struct ceph_mds_client *mdsc = fsc->mdsc;
	struct ceph_mds_request *req;
	int mode = USE_AUTH_MDS;
	int err;
	char *xattr_value;
	size_t xattr_value_len;

	req = ceph_mdsc_create_request(mdsc, CEPH_MDS_OP_GETVXATTR, mode);
	if (IS_ERR(req)) {
		err = -EANALMEM;
		goto out;
	}

	req->r_feature_needed = CEPHFS_FEATURE_OP_GETVXATTR;
	req->r_path2 = kstrdup(name, GFP_ANALFS);
	if (!req->r_path2) {
		err = -EANALMEM;
		goto put;
	}

	ihold(ianalde);
	req->r_ianalde = ianalde;
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
 * Check ianalde permissions.  We verify we have a valid value for
 * the AUTH cap, then call the generic handler.
 */
int ceph_permission(struct mnt_idmap *idmap, struct ianalde *ianalde,
		    int mask)
{
	int err;

	if (mask & MAY_ANALT_BLOCK)
		return -ECHILD;

	err = ceph_do_getattr(ianalde, CEPH_CAP_AUTH_SHARED, false);

	if (!err)
		err = generic_permission(idmap, ianalde, mask);
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
		 * The link count for directories depends on ianalde->i_subdirs,
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
	struct ianalde *ianalde = d_ianalde(path->dentry);
	struct super_block *sb = ianalde->i_sb;
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	u32 valid_mask = STATX_BASIC_STATS;
	int err = 0;

	if (ceph_ianalde_is_shutdown(ianalde))
		return -ESTALE;

	/* Skip the getattr altogether if we're asked analt to sync */
	if ((flags & AT_STATX_SYNC_TYPE) != AT_STATX_DONT_SYNC) {
		err = ceph_do_getattr(ianalde,
				statx_to_caps(request_mask, ianalde->i_mode),
				flags & AT_STATX_FORCE_SYNC);
		if (err)
			return err;
	}

	generic_fillattr(idmap, request_mask, ianalde, stat);
	stat->ianal = ceph_present_ianalde(ianalde);

	/*
	 * btime on newly-allocated ianaldes is 0, so if this is still set to
	 * that, then assume that it's analt valid.
	 */
	if (ci->i_btime.tv_sec || ci->i_btime.tv_nsec) {
		stat->btime = ci->i_btime;
		valid_mask |= STATX_BTIME;
	}

	if (request_mask & STATX_CHANGE_COOKIE) {
		stat->change_cookie = ianalde_peek_iversion_raw(ianalde);
		valid_mask |= STATX_CHANGE_COOKIE;
	}

	if (ceph_snap(ianalde) == CEPH_ANALSNAP)
		stat->dev = sb->s_dev;
	else
		stat->dev = ci->i_snapid_map ? ci->i_snapid_map->dev : 0;

	if (S_ISDIR(ianalde->i_mode)) {
		if (ceph_test_mount_opt(ceph_sb_to_fs_client(sb), RBYTES)) {
			stat->size = ci->i_rbytes;
		} else if (ceph_snap(ianalde) == CEPH_SNAPDIR) {
			struct ceph_ianalde_info *pci;
			struct ceph_snap_realm *realm;
			struct ianalde *parent;

			parent = ceph_lookup_ianalde(sb, ceph_ianal(ianalde));
			if (IS_ERR(parent))
				return PTR_ERR(parent);

			pci = ceph_ianalde(parent);
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

	stat->attributes |= STATX_ATTR_CHANGE_MOANALTONIC;
	if (IS_ENCRYPTED(ianalde))
		stat->attributes |= STATX_ATTR_ENCRYPTED;
	stat->attributes_mask |= (STATX_ATTR_CHANGE_MOANALTONIC |
				  STATX_ATTR_ENCRYPTED);

	stat->result_mask = request_mask & valid_mask;
	return err;
}

void ceph_ianalde_shutdown(struct ianalde *ianalde)
{
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	struct rb_analde *p;
	int iputs = 0;
	bool invalidate = false;

	spin_lock(&ci->i_ceph_lock);
	ci->i_ceph_flags |= CEPH_I_SHUTDOWN;
	p = rb_first(&ci->i_caps);
	while (p) {
		struct ceph_cap *cap = rb_entry(p, struct ceph_cap, ci_analde);

		p = rb_next(p);
		iputs += ceph_purge_ianalde_cap(ianalde, cap, &invalidate);
	}
	spin_unlock(&ci->i_ceph_lock);

	if (invalidate)
		ceph_queue_invalidate(ianalde);
	while (iputs--)
		iput(ianalde);
}
