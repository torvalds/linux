// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014 Christoph Hellwig.
 */
#include <linux/blkdev.h>
#include <linux/kmod.h>
#include <linux/file.h>
#include <linux/jhash.h>
#include <linux/sched.h>
#include <linux/sunrpc/addr.h>

#include "pnfs.h"
#include "netns.h"
#include "trace.h"

#define NFSDDBG_FACILITY                NFSDDBG_PNFS

struct nfs4_layout {
	struct list_head		lo_perstate;
	struct nfs4_layout_stateid	*lo_state;
	struct nfsd4_layout_seg		lo_seg;
};

static struct kmem_cache *nfs4_layout_cache;
static struct kmem_cache *nfs4_layout_stateid_cache;

static const struct nfsd4_callback_ops nfsd4_cb_layout_ops;
static const struct lock_manager_operations nfsd4_layouts_lm_ops;

const struct nfsd4_layout_ops *nfsd4_layout_ops[LAYOUT_TYPE_MAX] =  {
#ifdef CONFIG_NFSD_FLEXFILELAYOUT
	[LAYOUT_FLEX_FILES]	= &ff_layout_ops,
#endif
#ifdef CONFIG_NFSD_BLOCKLAYOUT
	[LAYOUT_BLOCK_VOLUME]	= &bl_layout_ops,
#endif
#ifdef CONFIG_NFSD_SCSILAYOUT
	[LAYOUT_SCSI]		= &scsi_layout_ops,
#endif
};

/* pNFS device ID to export fsid mapping */
#define DEVID_HASH_BITS	8
#define DEVID_HASH_SIZE	(1 << DEVID_HASH_BITS)
#define DEVID_HASH_MASK	(DEVID_HASH_SIZE - 1)
static u64 nfsd_devid_seq = 1;
static struct list_head nfsd_devid_hash[DEVID_HASH_SIZE];
static DEFINE_SPINLOCK(nfsd_devid_lock);

static inline u32 devid_hashfn(u64 idx)
{
	return jhash_2words(idx, idx >> 32, 0) & DEVID_HASH_MASK;
}

static void
nfsd4_alloc_devid_map(const struct svc_fh *fhp)
{
	const struct knfsd_fh *fh = &fhp->fh_handle;
	size_t fsid_len = key_len(fh->fh_fsid_type);
	struct nfsd4_deviceid_map *map, *old;
	int i;

	map = kzalloc(sizeof(*map) + fsid_len, GFP_KERNEL);
	if (!map)
		return;

	map->fsid_type = fh->fh_fsid_type;
	memcpy(&map->fsid, fh->fh_fsid, fsid_len);

	spin_lock(&nfsd_devid_lock);
	if (fhp->fh_export->ex_devid_map)
		goto out_unlock;

	for (i = 0; i < DEVID_HASH_SIZE; i++) {
		list_for_each_entry(old, &nfsd_devid_hash[i], hash) {
			if (old->fsid_type != fh->fh_fsid_type)
				continue;
			if (memcmp(old->fsid, fh->fh_fsid,
					key_len(old->fsid_type)))
				continue;

			fhp->fh_export->ex_devid_map = old;
			goto out_unlock;
		}
	}

	map->idx = nfsd_devid_seq++;
	list_add_tail_rcu(&map->hash, &nfsd_devid_hash[devid_hashfn(map->idx)]);
	fhp->fh_export->ex_devid_map = map;
	map = NULL;

out_unlock:
	spin_unlock(&nfsd_devid_lock);
	kfree(map);
}

struct nfsd4_deviceid_map *
nfsd4_find_devid_map(int idx)
{
	struct nfsd4_deviceid_map *map, *ret = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(map, &nfsd_devid_hash[devid_hashfn(idx)], hash)
		if (map->idx == idx)
			ret = map;
	rcu_read_unlock();

	return ret;
}

int
nfsd4_set_deviceid(struct nfsd4_deviceid *id, const struct svc_fh *fhp,
		u32 device_generation)
{
	if (!fhp->fh_export->ex_devid_map) {
		nfsd4_alloc_devid_map(fhp);
		if (!fhp->fh_export->ex_devid_map)
			return -ENOMEM;
	}

	id->fsid_idx = fhp->fh_export->ex_devid_map->idx;
	id->generation = device_generation;
	id->pad = 0;
	return 0;
}

void nfsd4_setup_layout_type(struct svc_export *exp)
{
#if defined(CONFIG_NFSD_BLOCKLAYOUT) || defined(CONFIG_NFSD_SCSILAYOUT)
	struct super_block *sb = exp->ex_path.mnt->mnt_sb;
#endif

	if (!(exp->ex_flags & NFSEXP_PNFS))
		return;

#ifdef CONFIG_NFSD_FLEXFILELAYOUT
	exp->ex_layout_types |= 1 << LAYOUT_FLEX_FILES;
#endif
#ifdef CONFIG_NFSD_BLOCKLAYOUT
	if (sb->s_export_op->get_uuid &&
	    sb->s_export_op->map_blocks &&
	    sb->s_export_op->commit_blocks)
		exp->ex_layout_types |= 1 << LAYOUT_BLOCK_VOLUME;
#endif
#ifdef CONFIG_NFSD_SCSILAYOUT
	if (sb->s_export_op->map_blocks &&
	    sb->s_export_op->commit_blocks &&
	    sb->s_bdev &&
	    sb->s_bdev->bd_disk->fops->pr_ops &&
	    sb->s_bdev->bd_disk->fops->get_unique_id)
		exp->ex_layout_types |= 1 << LAYOUT_SCSI;
#endif
}

static void
nfsd4_free_layout_stateid(struct nfs4_stid *stid)
{
	struct nfs4_layout_stateid *ls = layoutstateid(stid);
	struct nfs4_client *clp = ls->ls_stid.sc_client;
	struct nfs4_file *fp = ls->ls_stid.sc_file;

	trace_nfsd_layoutstate_free(&ls->ls_stid.sc_stateid);

	spin_lock(&clp->cl_lock);
	list_del_init(&ls->ls_perclnt);
	spin_unlock(&clp->cl_lock);

	spin_lock(&fp->fi_lock);
	list_del_init(&ls->ls_perfile);
	spin_unlock(&fp->fi_lock);

	if (!nfsd4_layout_ops[ls->ls_layout_type]->disable_recalls)
		vfs_setlease(ls->ls_file->nf_file, F_UNLCK, NULL, (void **)&ls);
	nfsd_file_put(ls->ls_file);

	if (ls->ls_recalled)
		atomic_dec(&ls->ls_stid.sc_file->fi_lo_recalls);

	kmem_cache_free(nfs4_layout_stateid_cache, ls);
}

static int
nfsd4_layout_setlease(struct nfs4_layout_stateid *ls)
{
	struct file_lock *fl;
	int status;

	if (nfsd4_layout_ops[ls->ls_layout_type]->disable_recalls)
		return 0;

	fl = locks_alloc_lock();
	if (!fl)
		return -ENOMEM;
	locks_init_lock(fl);
	fl->fl_lmops = &nfsd4_layouts_lm_ops;
	fl->fl_flags = FL_LAYOUT;
	fl->fl_type = F_RDLCK;
	fl->fl_end = OFFSET_MAX;
	fl->fl_owner = ls;
	fl->fl_pid = current->tgid;
	fl->fl_file = ls->ls_file->nf_file;

	status = vfs_setlease(fl->fl_file, fl->fl_type, &fl, NULL);
	if (status) {
		locks_free_lock(fl);
		return status;
	}
	BUG_ON(fl != NULL);
	return 0;
}

static struct nfs4_layout_stateid *
nfsd4_alloc_layout_stateid(struct nfsd4_compound_state *cstate,
		struct nfs4_stid *parent, u32 layout_type)
{
	struct nfs4_client *clp = cstate->clp;
	struct nfs4_file *fp = parent->sc_file;
	struct nfs4_layout_stateid *ls;
	struct nfs4_stid *stp;

	stp = nfs4_alloc_stid(cstate->clp, nfs4_layout_stateid_cache,
					nfsd4_free_layout_stateid);
	if (!stp)
		return NULL;

	get_nfs4_file(fp);
	stp->sc_file = fp;

	ls = layoutstateid(stp);
	INIT_LIST_HEAD(&ls->ls_perclnt);
	INIT_LIST_HEAD(&ls->ls_perfile);
	spin_lock_init(&ls->ls_lock);
	INIT_LIST_HEAD(&ls->ls_layouts);
	mutex_init(&ls->ls_mutex);
	ls->ls_layout_type = layout_type;
	nfsd4_init_cb(&ls->ls_recall, clp, &nfsd4_cb_layout_ops,
			NFSPROC4_CLNT_CB_LAYOUT);

	if (parent->sc_type == NFS4_DELEG_STID)
		ls->ls_file = nfsd_file_get(fp->fi_deleg_file);
	else
		ls->ls_file = find_any_file(fp);
	BUG_ON(!ls->ls_file);

	if (nfsd4_layout_setlease(ls)) {
		nfsd_file_put(ls->ls_file);
		put_nfs4_file(fp);
		kmem_cache_free(nfs4_layout_stateid_cache, ls);
		return NULL;
	}

	spin_lock(&clp->cl_lock);
	stp->sc_type = NFS4_LAYOUT_STID;
	list_add(&ls->ls_perclnt, &clp->cl_lo_states);
	spin_unlock(&clp->cl_lock);

	spin_lock(&fp->fi_lock);
	list_add(&ls->ls_perfile, &fp->fi_lo_states);
	spin_unlock(&fp->fi_lock);

	trace_nfsd_layoutstate_alloc(&ls->ls_stid.sc_stateid);
	return ls;
}

__be32
nfsd4_preprocess_layout_stateid(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate, stateid_t *stateid,
		bool create, u32 layout_type, struct nfs4_layout_stateid **lsp)
{
	struct nfs4_layout_stateid *ls;
	struct nfs4_stid *stid;
	unsigned char typemask = NFS4_LAYOUT_STID;
	__be32 status;

	if (create)
		typemask |= (NFS4_OPEN_STID | NFS4_LOCK_STID | NFS4_DELEG_STID);

	status = nfsd4_lookup_stateid(cstate, stateid, typemask, &stid,
			net_generic(SVC_NET(rqstp), nfsd_net_id));
	if (status)
		goto out;

	if (!fh_match(&cstate->current_fh.fh_handle,
		      &stid->sc_file->fi_fhandle)) {
		status = nfserr_bad_stateid;
		goto out_put_stid;
	}

	if (stid->sc_type != NFS4_LAYOUT_STID) {
		ls = nfsd4_alloc_layout_stateid(cstate, stid, layout_type);
		nfs4_put_stid(stid);

		status = nfserr_jukebox;
		if (!ls)
			goto out;
		mutex_lock(&ls->ls_mutex);
	} else {
		ls = container_of(stid, struct nfs4_layout_stateid, ls_stid);

		status = nfserr_bad_stateid;
		mutex_lock(&ls->ls_mutex);
		if (nfsd4_stateid_generation_after(stateid, &stid->sc_stateid))
			goto out_unlock_stid;
		if (layout_type != ls->ls_layout_type)
			goto out_unlock_stid;
	}

	*lsp = ls;
	return 0;

out_unlock_stid:
	mutex_unlock(&ls->ls_mutex);
out_put_stid:
	nfs4_put_stid(stid);
out:
	return status;
}

static void
nfsd4_recall_file_layout(struct nfs4_layout_stateid *ls)
{
	spin_lock(&ls->ls_lock);
	if (ls->ls_recalled)
		goto out_unlock;

	ls->ls_recalled = true;
	atomic_inc(&ls->ls_stid.sc_file->fi_lo_recalls);
	if (list_empty(&ls->ls_layouts))
		goto out_unlock;

	trace_nfsd_layout_recall(&ls->ls_stid.sc_stateid);

	refcount_inc(&ls->ls_stid.sc_count);
	nfsd4_run_cb(&ls->ls_recall);

out_unlock:
	spin_unlock(&ls->ls_lock);
}

static inline u64
layout_end(struct nfsd4_layout_seg *seg)
{
	u64 end = seg->offset + seg->length;
	return end >= seg->offset ? end : NFS4_MAX_UINT64;
}

static void
layout_update_len(struct nfsd4_layout_seg *lo, u64 end)
{
	if (end == NFS4_MAX_UINT64)
		lo->length = NFS4_MAX_UINT64;
	else
		lo->length = end - lo->offset;
}

static bool
layouts_overlapping(struct nfs4_layout *lo, struct nfsd4_layout_seg *s)
{
	if (s->iomode != IOMODE_ANY && s->iomode != lo->lo_seg.iomode)
		return false;
	if (layout_end(&lo->lo_seg) <= s->offset)
		return false;
	if (layout_end(s) <= lo->lo_seg.offset)
		return false;
	return true;
}

static bool
layouts_try_merge(struct nfsd4_layout_seg *lo, struct nfsd4_layout_seg *new)
{
	if (lo->iomode != new->iomode)
		return false;
	if (layout_end(new) < lo->offset)
		return false;
	if (layout_end(lo) < new->offset)
		return false;

	lo->offset = min(lo->offset, new->offset);
	layout_update_len(lo, max(layout_end(lo), layout_end(new)));
	return true;
}

static __be32
nfsd4_recall_conflict(struct nfs4_layout_stateid *ls)
{
	struct nfs4_file *fp = ls->ls_stid.sc_file;
	struct nfs4_layout_stateid *l, *n;
	__be32 nfserr = nfs_ok;

	assert_spin_locked(&fp->fi_lock);

	list_for_each_entry_safe(l, n, &fp->fi_lo_states, ls_perfile) {
		if (l != ls) {
			nfsd4_recall_file_layout(l);
			nfserr = nfserr_recallconflict;
		}
	}

	return nfserr;
}

__be32
nfsd4_insert_layout(struct nfsd4_layoutget *lgp, struct nfs4_layout_stateid *ls)
{
	struct nfsd4_layout_seg *seg = &lgp->lg_seg;
	struct nfs4_file *fp = ls->ls_stid.sc_file;
	struct nfs4_layout *lp, *new = NULL;
	__be32 nfserr;

	spin_lock(&fp->fi_lock);
	nfserr = nfsd4_recall_conflict(ls);
	if (nfserr)
		goto out;
	spin_lock(&ls->ls_lock);
	list_for_each_entry(lp, &ls->ls_layouts, lo_perstate) {
		if (layouts_try_merge(&lp->lo_seg, seg))
			goto done;
	}
	spin_unlock(&ls->ls_lock);
	spin_unlock(&fp->fi_lock);

	new = kmem_cache_alloc(nfs4_layout_cache, GFP_KERNEL);
	if (!new)
		return nfserr_jukebox;
	memcpy(&new->lo_seg, seg, sizeof(new->lo_seg));
	new->lo_state = ls;

	spin_lock(&fp->fi_lock);
	nfserr = nfsd4_recall_conflict(ls);
	if (nfserr)
		goto out;
	spin_lock(&ls->ls_lock);
	list_for_each_entry(lp, &ls->ls_layouts, lo_perstate) {
		if (layouts_try_merge(&lp->lo_seg, seg))
			goto done;
	}

	refcount_inc(&ls->ls_stid.sc_count);
	list_add_tail(&new->lo_perstate, &ls->ls_layouts);
	new = NULL;
done:
	nfs4_inc_and_copy_stateid(&lgp->lg_sid, &ls->ls_stid);
	spin_unlock(&ls->ls_lock);
out:
	spin_unlock(&fp->fi_lock);
	if (new)
		kmem_cache_free(nfs4_layout_cache, new);
	return nfserr;
}

static void
nfsd4_free_layouts(struct list_head *reaplist)
{
	while (!list_empty(reaplist)) {
		struct nfs4_layout *lp = list_first_entry(reaplist,
				struct nfs4_layout, lo_perstate);

		list_del(&lp->lo_perstate);
		nfs4_put_stid(&lp->lo_state->ls_stid);
		kmem_cache_free(nfs4_layout_cache, lp);
	}
}

static void
nfsd4_return_file_layout(struct nfs4_layout *lp, struct nfsd4_layout_seg *seg,
		struct list_head *reaplist)
{
	struct nfsd4_layout_seg *lo = &lp->lo_seg;
	u64 end = layout_end(lo);

	if (seg->offset <= lo->offset) {
		if (layout_end(seg) >= end) {
			list_move_tail(&lp->lo_perstate, reaplist);
			return;
		}
		lo->offset = layout_end(seg);
	} else {
		/* retain the whole layout segment on a split. */
		if (layout_end(seg) < end) {
			dprintk("%s: split not supported\n", __func__);
			return;
		}
		end = seg->offset;
	}

	layout_update_len(lo, end);
}

__be32
nfsd4_return_file_layouts(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate,
		struct nfsd4_layoutreturn *lrp)
{
	struct nfs4_layout_stateid *ls;
	struct nfs4_layout *lp, *n;
	LIST_HEAD(reaplist);
	__be32 nfserr;
	int found = 0;

	nfserr = nfsd4_preprocess_layout_stateid(rqstp, cstate, &lrp->lr_sid,
						false, lrp->lr_layout_type,
						&ls);
	if (nfserr) {
		trace_nfsd_layout_return_lookup_fail(&lrp->lr_sid);
		return nfserr;
	}

	spin_lock(&ls->ls_lock);
	list_for_each_entry_safe(lp, n, &ls->ls_layouts, lo_perstate) {
		if (layouts_overlapping(lp, &lrp->lr_seg)) {
			nfsd4_return_file_layout(lp, &lrp->lr_seg, &reaplist);
			found++;
		}
	}
	if (!list_empty(&ls->ls_layouts)) {
		if (found)
			nfs4_inc_and_copy_stateid(&lrp->lr_sid, &ls->ls_stid);
		lrp->lrs_present = 1;
	} else {
		trace_nfsd_layoutstate_unhash(&ls->ls_stid.sc_stateid);
		nfs4_unhash_stid(&ls->ls_stid);
		lrp->lrs_present = 0;
	}
	spin_unlock(&ls->ls_lock);

	mutex_unlock(&ls->ls_mutex);
	nfs4_put_stid(&ls->ls_stid);
	nfsd4_free_layouts(&reaplist);
	return nfs_ok;
}

__be32
nfsd4_return_client_layouts(struct svc_rqst *rqstp,
		struct nfsd4_compound_state *cstate,
		struct nfsd4_layoutreturn *lrp)
{
	struct nfs4_layout_stateid *ls, *n;
	struct nfs4_client *clp = cstate->clp;
	struct nfs4_layout *lp, *t;
	LIST_HEAD(reaplist);

	lrp->lrs_present = 0;

	spin_lock(&clp->cl_lock);
	list_for_each_entry_safe(ls, n, &clp->cl_lo_states, ls_perclnt) {
		if (ls->ls_layout_type != lrp->lr_layout_type)
			continue;

		if (lrp->lr_return_type == RETURN_FSID &&
		    !fh_fsid_match(&ls->ls_stid.sc_file->fi_fhandle,
				   &cstate->current_fh.fh_handle))
			continue;

		spin_lock(&ls->ls_lock);
		list_for_each_entry_safe(lp, t, &ls->ls_layouts, lo_perstate) {
			if (lrp->lr_seg.iomode == IOMODE_ANY ||
			    lrp->lr_seg.iomode == lp->lo_seg.iomode)
				list_move_tail(&lp->lo_perstate, &reaplist);
		}
		spin_unlock(&ls->ls_lock);
	}
	spin_unlock(&clp->cl_lock);

	nfsd4_free_layouts(&reaplist);
	return 0;
}

static void
nfsd4_return_all_layouts(struct nfs4_layout_stateid *ls,
		struct list_head *reaplist)
{
	spin_lock(&ls->ls_lock);
	list_splice_init(&ls->ls_layouts, reaplist);
	spin_unlock(&ls->ls_lock);
}

void
nfsd4_return_all_client_layouts(struct nfs4_client *clp)
{
	struct nfs4_layout_stateid *ls, *n;
	LIST_HEAD(reaplist);

	spin_lock(&clp->cl_lock);
	list_for_each_entry_safe(ls, n, &clp->cl_lo_states, ls_perclnt)
		nfsd4_return_all_layouts(ls, &reaplist);
	spin_unlock(&clp->cl_lock);

	nfsd4_free_layouts(&reaplist);
}

void
nfsd4_return_all_file_layouts(struct nfs4_client *clp, struct nfs4_file *fp)
{
	struct nfs4_layout_stateid *ls, *n;
	LIST_HEAD(reaplist);

	spin_lock(&fp->fi_lock);
	list_for_each_entry_safe(ls, n, &fp->fi_lo_states, ls_perfile) {
		if (ls->ls_stid.sc_client == clp)
			nfsd4_return_all_layouts(ls, &reaplist);
	}
	spin_unlock(&fp->fi_lock);

	nfsd4_free_layouts(&reaplist);
}

static void
nfsd4_cb_layout_fail(struct nfs4_layout_stateid *ls)
{
	struct nfs4_client *clp = ls->ls_stid.sc_client;
	char addr_str[INET6_ADDRSTRLEN];
	static char const nfsd_recall_failed[] = "/sbin/nfsd-recall-failed";
	static char *envp[] = {
		"HOME=/",
		"TERM=linux",
		"PATH=/sbin:/usr/sbin:/bin:/usr/bin",
		NULL
	};
	char *argv[8];
	int error;

	rpc_ntop((struct sockaddr *)&clp->cl_addr, addr_str, sizeof(addr_str));

	printk(KERN_WARNING
		"nfsd: client %s failed to respond to layout recall. "
		"  Fencing..\n", addr_str);

	argv[0] = (char *)nfsd_recall_failed;
	argv[1] = addr_str;
	argv[2] = ls->ls_file->nf_file->f_path.mnt->mnt_sb->s_id;
	argv[3] = NULL;

	error = call_usermodehelper(nfsd_recall_failed, argv, envp,
				    UMH_WAIT_PROC);
	if (error) {
		printk(KERN_ERR "nfsd: fence failed for client %s: %d!\n",
			addr_str, error);
	}
}

static void
nfsd4_cb_layout_prepare(struct nfsd4_callback *cb)
{
	struct nfs4_layout_stateid *ls =
		container_of(cb, struct nfs4_layout_stateid, ls_recall);

	mutex_lock(&ls->ls_mutex);
	nfs4_inc_and_copy_stateid(&ls->ls_recall_sid, &ls->ls_stid);
	mutex_unlock(&ls->ls_mutex);
}

static int
nfsd4_cb_layout_done(struct nfsd4_callback *cb, struct rpc_task *task)
{
	struct nfs4_layout_stateid *ls =
		container_of(cb, struct nfs4_layout_stateid, ls_recall);
	struct nfsd_net *nn;
	ktime_t now, cutoff;
	const struct nfsd4_layout_ops *ops;

	trace_nfsd_cb_layout_done(&ls->ls_stid.sc_stateid, task);
	switch (task->tk_status) {
	case 0:
	case -NFS4ERR_DELAY:
		/*
		 * Anything left? If not, then call it done. Note that we don't
		 * take the spinlock since this is an optimization and nothing
		 * should get added until the cb counter goes to zero.
		 */
		if (list_empty(&ls->ls_layouts))
			return 1;

		/* Poll the client until it's done with the layout */
		now = ktime_get();
		nn = net_generic(ls->ls_stid.sc_client->net, nfsd_net_id);

		/* Client gets 2 lease periods to return it */
		cutoff = ktime_add_ns(task->tk_start,
					 (u64)nn->nfsd4_lease * NSEC_PER_SEC * 2);

		if (ktime_before(now, cutoff)) {
			rpc_delay(task, HZ/100); /* 10 mili-seconds */
			return 0;
		}
		fallthrough;
	default:
		/*
		 * Unknown error or non-responding client, we'll need to fence.
		 */
		trace_nfsd_layout_recall_fail(&ls->ls_stid.sc_stateid);

		ops = nfsd4_layout_ops[ls->ls_layout_type];
		if (ops->fence_client)
			ops->fence_client(ls);
		else
			nfsd4_cb_layout_fail(ls);
		return 1;
	case -NFS4ERR_NOMATCHING_LAYOUT:
		trace_nfsd_layout_recall_done(&ls->ls_stid.sc_stateid);
		task->tk_status = 0;
		return 1;
	}
}

static void
nfsd4_cb_layout_release(struct nfsd4_callback *cb)
{
	struct nfs4_layout_stateid *ls =
		container_of(cb, struct nfs4_layout_stateid, ls_recall);
	LIST_HEAD(reaplist);

	trace_nfsd_layout_recall_release(&ls->ls_stid.sc_stateid);

	nfsd4_return_all_layouts(ls, &reaplist);
	nfsd4_free_layouts(&reaplist);
	nfs4_put_stid(&ls->ls_stid);
}

static const struct nfsd4_callback_ops nfsd4_cb_layout_ops = {
	.prepare	= nfsd4_cb_layout_prepare,
	.done		= nfsd4_cb_layout_done,
	.release	= nfsd4_cb_layout_release,
};

static bool
nfsd4_layout_lm_break(struct file_lock *fl)
{
	/*
	 * We don't want the locks code to timeout the lease for us;
	 * we'll remove it ourself if a layout isn't returned
	 * in time:
	 */
	fl->fl_break_time = 0;
	nfsd4_recall_file_layout(fl->fl_owner);
	return false;
}

static int
nfsd4_layout_lm_change(struct file_lock *onlist, int arg,
		struct list_head *dispose)
{
	BUG_ON(!(arg & F_UNLCK));
	return lease_modify(onlist, arg, dispose);
}

static const struct lock_manager_operations nfsd4_layouts_lm_ops = {
	.lm_break	= nfsd4_layout_lm_break,
	.lm_change	= nfsd4_layout_lm_change,
};

int
nfsd4_init_pnfs(void)
{
	int i;

	for (i = 0; i < DEVID_HASH_SIZE; i++)
		INIT_LIST_HEAD(&nfsd_devid_hash[i]);

	nfs4_layout_cache = kmem_cache_create("nfs4_layout",
			sizeof(struct nfs4_layout), 0, 0, NULL);
	if (!nfs4_layout_cache)
		return -ENOMEM;

	nfs4_layout_stateid_cache = kmem_cache_create("nfs4_layout_stateid",
			sizeof(struct nfs4_layout_stateid), 0, 0, NULL);
	if (!nfs4_layout_stateid_cache) {
		kmem_cache_destroy(nfs4_layout_cache);
		return -ENOMEM;
	}
	return 0;
}

void
nfsd4_exit_pnfs(void)
{
	int i;

	kmem_cache_destroy(nfs4_layout_cache);
	kmem_cache_destroy(nfs4_layout_stateid_cache);

	for (i = 0; i < DEVID_HASH_SIZE; i++) {
		struct nfsd4_deviceid_map *map, *n;

		list_for_each_entry_safe(map, n, &nfsd_devid_hash[i], hash)
			kfree(map);
	}
}
