/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmglue.c
 *
 * Code which implements an OCFS2 specific interface to our DLM.
 *
 * Copyright (C) 2003, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/crc32.h>
#include <linux/kthread.h>
#include <linux/pagemap.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <cluster/heartbeat.h>
#include <cluster/nodemanager.h>
#include <cluster/tcp.h>

#include <dlm/dlmapi.h>

#define MLOG_MASK_PREFIX ML_DLM_GLUE
#include <cluster/masklog.h>

#include "ocfs2.h"
#include "ocfs2_lockingver.h"

#include "alloc.h"
#include "dcache.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "file.h"
#include "heartbeat.h"
#include "inode.h"
#include "journal.h"
#include "slot_map.h"
#include "super.h"
#include "uptodate.h"

#include "buffer_head_io.h"

struct ocfs2_mask_waiter {
	struct list_head	mw_item;
	int			mw_status;
	struct completion	mw_complete;
	unsigned long		mw_mask;
	unsigned long		mw_goal;
};

static struct ocfs2_super *ocfs2_get_dentry_osb(struct ocfs2_lock_res *lockres);
static struct ocfs2_super *ocfs2_get_inode_osb(struct ocfs2_lock_res *lockres);
static struct ocfs2_super *ocfs2_get_file_osb(struct ocfs2_lock_res *lockres);

/*
 * Return value from ->downconvert_worker functions.
 *
 * These control the precise actions of ocfs2_unblock_lock()
 * and ocfs2_process_blocked_lock()
 *
 */
enum ocfs2_unblock_action {
	UNBLOCK_CONTINUE	= 0, /* Continue downconvert */
	UNBLOCK_CONTINUE_POST	= 1, /* Continue downconvert, fire
				      * ->post_unlock callback */
	UNBLOCK_STOP_POST	= 2, /* Do not downconvert, fire
				      * ->post_unlock() callback. */
};

struct ocfs2_unblock_ctl {
	int requeue;
	enum ocfs2_unblock_action unblock_action;
};

static int ocfs2_check_meta_downconvert(struct ocfs2_lock_res *lockres,
					int new_level);
static void ocfs2_set_meta_lvb(struct ocfs2_lock_res *lockres);

static int ocfs2_data_convert_worker(struct ocfs2_lock_res *lockres,
				     int blocking);

static int ocfs2_dentry_convert_worker(struct ocfs2_lock_res *lockres,
				       int blocking);

static void ocfs2_dentry_post_unlock(struct ocfs2_super *osb,
				     struct ocfs2_lock_res *lockres);


#define mlog_meta_lvb(__level, __lockres) ocfs2_dump_meta_lvb_info(__level, __PRETTY_FUNCTION__, __LINE__, __lockres)

/* This aids in debugging situations where a bad LVB might be involved. */
static void ocfs2_dump_meta_lvb_info(u64 level,
				     const char *function,
				     unsigned int line,
				     struct ocfs2_lock_res *lockres)
{
	struct ocfs2_meta_lvb *lvb = (struct ocfs2_meta_lvb *) lockres->l_lksb.lvb;

	mlog(level, "LVB information for %s (called from %s:%u):\n",
	     lockres->l_name, function, line);
	mlog(level, "version: %u, clusters: %u, generation: 0x%x\n",
	     lvb->lvb_version, be32_to_cpu(lvb->lvb_iclusters),
	     be32_to_cpu(lvb->lvb_igeneration));
	mlog(level, "size: %llu, uid %u, gid %u, mode 0x%x\n",
	     (unsigned long long)be64_to_cpu(lvb->lvb_isize),
	     be32_to_cpu(lvb->lvb_iuid), be32_to_cpu(lvb->lvb_igid),
	     be16_to_cpu(lvb->lvb_imode));
	mlog(level, "nlink %u, atime_packed 0x%llx, ctime_packed 0x%llx, "
	     "mtime_packed 0x%llx iattr 0x%x\n", be16_to_cpu(lvb->lvb_inlink),
	     (long long)be64_to_cpu(lvb->lvb_iatime_packed),
	     (long long)be64_to_cpu(lvb->lvb_ictime_packed),
	     (long long)be64_to_cpu(lvb->lvb_imtime_packed),
	     be32_to_cpu(lvb->lvb_iattr));
}


/*
 * OCFS2 Lock Resource Operations
 *
 * These fine tune the behavior of the generic dlmglue locking infrastructure.
 *
 * The most basic of lock types can point ->l_priv to their respective
 * struct ocfs2_super and allow the default actions to manage things.
 *
 * Right now, each lock type also needs to implement an init function,
 * and trivial lock/unlock wrappers. ocfs2_simple_drop_lockres()
 * should be called when the lock is no longer needed (i.e., object
 * destruction time).
 */
struct ocfs2_lock_res_ops {
	/*
	 * Translate an ocfs2_lock_res * into an ocfs2_super *. Define
	 * this callback if ->l_priv is not an ocfs2_super pointer
	 */
	struct ocfs2_super * (*get_osb)(struct ocfs2_lock_res *);

	/*
	 * Optionally called in the downconvert thread after a
	 * successful downconvert. The lockres will not be referenced
	 * after this callback is called, so it is safe to free
	 * memory, etc.
	 *
	 * The exact semantics of when this is called are controlled
	 * by ->downconvert_worker()
	 */
	void (*post_unlock)(struct ocfs2_super *, struct ocfs2_lock_res *);

	/*
	 * Allow a lock type to add checks to determine whether it is
	 * safe to downconvert a lock. Return 0 to re-queue the
	 * downconvert at a later time, nonzero to continue.
	 *
	 * For most locks, the default checks that there are no
	 * incompatible holders are sufficient.
	 *
	 * Called with the lockres spinlock held.
	 */
	int (*check_downconvert)(struct ocfs2_lock_res *, int);

	/*
	 * Allows a lock type to populate the lock value block. This
	 * is called on downconvert, and when we drop a lock.
	 *
	 * Locks that want to use this should set LOCK_TYPE_USES_LVB
	 * in the flags field.
	 *
	 * Called with the lockres spinlock held.
	 */
	void (*set_lvb)(struct ocfs2_lock_res *);

	/*
	 * Called from the downconvert thread when it is determined
	 * that a lock will be downconverted. This is called without
	 * any locks held so the function can do work that might
	 * schedule (syncing out data, etc).
	 *
	 * This should return any one of the ocfs2_unblock_action
	 * values, depending on what it wants the thread to do.
	 */
	int (*downconvert_worker)(struct ocfs2_lock_res *, int);

	/*
	 * LOCK_TYPE_* flags which describe the specific requirements
	 * of a lock type. Descriptions of each individual flag follow.
	 */
	int flags;
};

/*
 * Some locks want to "refresh" potentially stale data when a
 * meaningful (PRMODE or EXMODE) lock level is first obtained. If this
 * flag is set, the OCFS2_LOCK_NEEDS_REFRESH flag will be set on the
 * individual lockres l_flags member from the ast function. It is
 * expected that the locking wrapper will clear the
 * OCFS2_LOCK_NEEDS_REFRESH flag when done.
 */
#define LOCK_TYPE_REQUIRES_REFRESH 0x1

/*
 * Indicate that a lock type makes use of the lock value block. The
 * ->set_lvb lock type callback must be defined.
 */
#define LOCK_TYPE_USES_LVB		0x2

static struct ocfs2_lock_res_ops ocfs2_inode_rw_lops = {
	.get_osb	= ocfs2_get_inode_osb,
	.flags		= 0,
};

static struct ocfs2_lock_res_ops ocfs2_inode_inode_lops = {
	.get_osb	= ocfs2_get_inode_osb,
	.check_downconvert = ocfs2_check_meta_downconvert,
	.set_lvb	= ocfs2_set_meta_lvb,
	.downconvert_worker = ocfs2_data_convert_worker,
	.flags		= LOCK_TYPE_REQUIRES_REFRESH|LOCK_TYPE_USES_LVB,
};

static struct ocfs2_lock_res_ops ocfs2_super_lops = {
	.flags		= LOCK_TYPE_REQUIRES_REFRESH,
};

static struct ocfs2_lock_res_ops ocfs2_rename_lops = {
	.flags		= 0,
};

static struct ocfs2_lock_res_ops ocfs2_dentry_lops = {
	.get_osb	= ocfs2_get_dentry_osb,
	.post_unlock	= ocfs2_dentry_post_unlock,
	.downconvert_worker = ocfs2_dentry_convert_worker,
	.flags		= 0,
};

static struct ocfs2_lock_res_ops ocfs2_inode_open_lops = {
	.get_osb	= ocfs2_get_inode_osb,
	.flags		= 0,
};

static struct ocfs2_lock_res_ops ocfs2_flock_lops = {
	.get_osb	= ocfs2_get_file_osb,
	.flags		= 0,
};

/*
 * This is the filesystem locking protocol version.
 *
 * Whenever the filesystem does new things with locks (adds or removes a
 * lock, orders them differently, does different things underneath a lock),
 * the version must be changed.  The protocol is negotiated when joining
 * the dlm domain.  A node may join the domain if its major version is
 * identical to all other nodes and its minor version is greater than
 * or equal to all other nodes.  When its minor version is greater than
 * the other nodes, it will run at the minor version specified by the
 * other nodes.
 *
 * If a locking change is made that will not be compatible with older
 * versions, the major number must be increased and the minor version set
 * to zero.  If a change merely adds a behavior that can be disabled when
 * speaking to older versions, the minor version must be increased.  If a
 * change adds a fully backwards compatible change (eg, LVB changes that
 * are just ignored by older versions), the version does not need to be
 * updated.
 */
const struct dlm_protocol_version ocfs2_locking_protocol = {
	.pv_major = OCFS2_LOCKING_PROTOCOL_MAJOR,
	.pv_minor = OCFS2_LOCKING_PROTOCOL_MINOR,
};

static inline int ocfs2_is_inode_lock(struct ocfs2_lock_res *lockres)
{
	return lockres->l_type == OCFS2_LOCK_TYPE_META ||
		lockres->l_type == OCFS2_LOCK_TYPE_RW ||
		lockres->l_type == OCFS2_LOCK_TYPE_OPEN;
}

static inline struct inode *ocfs2_lock_res_inode(struct ocfs2_lock_res *lockres)
{
	BUG_ON(!ocfs2_is_inode_lock(lockres));

	return (struct inode *) lockres->l_priv;
}

static inline struct ocfs2_dentry_lock *ocfs2_lock_res_dl(struct ocfs2_lock_res *lockres)
{
	BUG_ON(lockres->l_type != OCFS2_LOCK_TYPE_DENTRY);

	return (struct ocfs2_dentry_lock *)lockres->l_priv;
}

static inline struct ocfs2_super *ocfs2_get_lockres_osb(struct ocfs2_lock_res *lockres)
{
	if (lockres->l_ops->get_osb)
		return lockres->l_ops->get_osb(lockres);

	return (struct ocfs2_super *)lockres->l_priv;
}

static int ocfs2_lock_create(struct ocfs2_super *osb,
			     struct ocfs2_lock_res *lockres,
			     int level,
			     int dlm_flags);
static inline int ocfs2_may_continue_on_blocked_lock(struct ocfs2_lock_res *lockres,
						     int wanted);
static void ocfs2_cluster_unlock(struct ocfs2_super *osb,
				 struct ocfs2_lock_res *lockres,
				 int level);
static inline void ocfs2_generic_handle_downconvert_action(struct ocfs2_lock_res *lockres);
static inline void ocfs2_generic_handle_convert_action(struct ocfs2_lock_res *lockres);
static inline void ocfs2_generic_handle_attach_action(struct ocfs2_lock_res *lockres);
static int ocfs2_generic_handle_bast(struct ocfs2_lock_res *lockres, int level);
static void ocfs2_schedule_blocked_lock(struct ocfs2_super *osb,
					struct ocfs2_lock_res *lockres);
static inline void ocfs2_recover_from_dlm_error(struct ocfs2_lock_res *lockres,
						int convert);
#define ocfs2_log_dlm_error(_func, _stat, _lockres) do {	\
	mlog(ML_ERROR, "Dlm error \"%s\" while calling %s on "	\
		"resource %s: %s\n", dlm_errname(_stat), _func,	\
		_lockres->l_name, dlm_errmsg(_stat));		\
} while (0)
static int ocfs2_downconvert_thread(void *arg);
static void ocfs2_downconvert_on_unlock(struct ocfs2_super *osb,
					struct ocfs2_lock_res *lockres);
static int ocfs2_inode_lock_update(struct inode *inode,
				  struct buffer_head **bh);
static void ocfs2_drop_osb_locks(struct ocfs2_super *osb);
static inline int ocfs2_highest_compat_lock_level(int level);
static void ocfs2_prepare_downconvert(struct ocfs2_lock_res *lockres,
				      int new_level);
static int ocfs2_downconvert_lock(struct ocfs2_super *osb,
				  struct ocfs2_lock_res *lockres,
				  int new_level,
				  int lvb);
static int ocfs2_prepare_cancel_convert(struct ocfs2_super *osb,
				        struct ocfs2_lock_res *lockres);
static int ocfs2_cancel_convert(struct ocfs2_super *osb,
				struct ocfs2_lock_res *lockres);


static void ocfs2_build_lock_name(enum ocfs2_lock_type type,
				  u64 blkno,
				  u32 generation,
				  char *name)
{
	int len;

	mlog_entry_void();

	BUG_ON(type >= OCFS2_NUM_LOCK_TYPES);

	len = snprintf(name, OCFS2_LOCK_ID_MAX_LEN, "%c%s%016llx%08x",
		       ocfs2_lock_type_char(type), OCFS2_LOCK_ID_PAD,
		       (long long)blkno, generation);

	BUG_ON(len != (OCFS2_LOCK_ID_MAX_LEN - 1));

	mlog(0, "built lock resource with name: %s\n", name);

	mlog_exit_void();
}

static DEFINE_SPINLOCK(ocfs2_dlm_tracking_lock);

static void ocfs2_add_lockres_tracking(struct ocfs2_lock_res *res,
				       struct ocfs2_dlm_debug *dlm_debug)
{
	mlog(0, "Add tracking for lockres %s\n", res->l_name);

	spin_lock(&ocfs2_dlm_tracking_lock);
	list_add(&res->l_debug_list, &dlm_debug->d_lockres_tracking);
	spin_unlock(&ocfs2_dlm_tracking_lock);
}

static void ocfs2_remove_lockres_tracking(struct ocfs2_lock_res *res)
{
	spin_lock(&ocfs2_dlm_tracking_lock);
	if (!list_empty(&res->l_debug_list))
		list_del_init(&res->l_debug_list);
	spin_unlock(&ocfs2_dlm_tracking_lock);
}

static void ocfs2_lock_res_init_common(struct ocfs2_super *osb,
				       struct ocfs2_lock_res *res,
				       enum ocfs2_lock_type type,
				       struct ocfs2_lock_res_ops *ops,
				       void *priv)
{
	res->l_type          = type;
	res->l_ops           = ops;
	res->l_priv          = priv;

	res->l_level         = LKM_IVMODE;
	res->l_requested     = LKM_IVMODE;
	res->l_blocking      = LKM_IVMODE;
	res->l_action        = OCFS2_AST_INVALID;
	res->l_unlock_action = OCFS2_UNLOCK_INVALID;

	res->l_flags         = OCFS2_LOCK_INITIALIZED;

	ocfs2_add_lockres_tracking(res, osb->osb_dlm_debug);
}

void ocfs2_lock_res_init_once(struct ocfs2_lock_res *res)
{
	/* This also clears out the lock status block */
	memset(res, 0, sizeof(struct ocfs2_lock_res));
	spin_lock_init(&res->l_lock);
	init_waitqueue_head(&res->l_event);
	INIT_LIST_HEAD(&res->l_blocked_list);
	INIT_LIST_HEAD(&res->l_mask_waiters);
}

void ocfs2_inode_lock_res_init(struct ocfs2_lock_res *res,
			       enum ocfs2_lock_type type,
			       unsigned int generation,
			       struct inode *inode)
{
	struct ocfs2_lock_res_ops *ops;

	switch(type) {
		case OCFS2_LOCK_TYPE_RW:
			ops = &ocfs2_inode_rw_lops;
			break;
		case OCFS2_LOCK_TYPE_META:
			ops = &ocfs2_inode_inode_lops;
			break;
		case OCFS2_LOCK_TYPE_OPEN:
			ops = &ocfs2_inode_open_lops;
			break;
		default:
			mlog_bug_on_msg(1, "type: %d\n", type);
			ops = NULL; /* thanks, gcc */
			break;
	};

	ocfs2_build_lock_name(type, OCFS2_I(inode)->ip_blkno,
			      generation, res->l_name);
	ocfs2_lock_res_init_common(OCFS2_SB(inode->i_sb), res, type, ops, inode);
}

static struct ocfs2_super *ocfs2_get_inode_osb(struct ocfs2_lock_res *lockres)
{
	struct inode *inode = ocfs2_lock_res_inode(lockres);

	return OCFS2_SB(inode->i_sb);
}

static struct ocfs2_super *ocfs2_get_file_osb(struct ocfs2_lock_res *lockres)
{
	struct ocfs2_file_private *fp = lockres->l_priv;

	return OCFS2_SB(fp->fp_file->f_mapping->host->i_sb);
}

static __u64 ocfs2_get_dentry_lock_ino(struct ocfs2_lock_res *lockres)
{
	__be64 inode_blkno_be;

	memcpy(&inode_blkno_be, &lockres->l_name[OCFS2_DENTRY_LOCK_INO_START],
	       sizeof(__be64));

	return be64_to_cpu(inode_blkno_be);
}

static struct ocfs2_super *ocfs2_get_dentry_osb(struct ocfs2_lock_res *lockres)
{
	struct ocfs2_dentry_lock *dl = lockres->l_priv;

	return OCFS2_SB(dl->dl_inode->i_sb);
}

void ocfs2_dentry_lock_res_init(struct ocfs2_dentry_lock *dl,
				u64 parent, struct inode *inode)
{
	int len;
	u64 inode_blkno = OCFS2_I(inode)->ip_blkno;
	__be64 inode_blkno_be = cpu_to_be64(inode_blkno);
	struct ocfs2_lock_res *lockres = &dl->dl_lockres;

	ocfs2_lock_res_init_once(lockres);

	/*
	 * Unfortunately, the standard lock naming scheme won't work
	 * here because we have two 16 byte values to use. Instead,
	 * we'll stuff the inode number as a binary value. We still
	 * want error prints to show something without garbling the
	 * display, so drop a null byte in there before the inode
	 * number. A future version of OCFS2 will likely use all
	 * binary lock names. The stringified names have been a
	 * tremendous aid in debugging, but now that the debugfs
	 * interface exists, we can mangle things there if need be.
	 *
	 * NOTE: We also drop the standard "pad" value (the total lock
	 * name size stays the same though - the last part is all
	 * zeros due to the memset in ocfs2_lock_res_init_once()
	 */
	len = snprintf(lockres->l_name, OCFS2_DENTRY_LOCK_INO_START,
		       "%c%016llx",
		       ocfs2_lock_type_char(OCFS2_LOCK_TYPE_DENTRY),
		       (long long)parent);

	BUG_ON(len != (OCFS2_DENTRY_LOCK_INO_START - 1));

	memcpy(&lockres->l_name[OCFS2_DENTRY_LOCK_INO_START], &inode_blkno_be,
	       sizeof(__be64));

	ocfs2_lock_res_init_common(OCFS2_SB(inode->i_sb), lockres,
				   OCFS2_LOCK_TYPE_DENTRY, &ocfs2_dentry_lops,
				   dl);
}

static void ocfs2_super_lock_res_init(struct ocfs2_lock_res *res,
				      struct ocfs2_super *osb)
{
	/* Superblock lockres doesn't come from a slab so we call init
	 * once on it manually.  */
	ocfs2_lock_res_init_once(res);
	ocfs2_build_lock_name(OCFS2_LOCK_TYPE_SUPER, OCFS2_SUPER_BLOCK_BLKNO,
			      0, res->l_name);
	ocfs2_lock_res_init_common(osb, res, OCFS2_LOCK_TYPE_SUPER,
				   &ocfs2_super_lops, osb);
}

static void ocfs2_rename_lock_res_init(struct ocfs2_lock_res *res,
				       struct ocfs2_super *osb)
{
	/* Rename lockres doesn't come from a slab so we call init
	 * once on it manually.  */
	ocfs2_lock_res_init_once(res);
	ocfs2_build_lock_name(OCFS2_LOCK_TYPE_RENAME, 0, 0, res->l_name);
	ocfs2_lock_res_init_common(osb, res, OCFS2_LOCK_TYPE_RENAME,
				   &ocfs2_rename_lops, osb);
}

void ocfs2_file_lock_res_init(struct ocfs2_lock_res *lockres,
			      struct ocfs2_file_private *fp)
{
	struct inode *inode = fp->fp_file->f_mapping->host;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	ocfs2_lock_res_init_once(lockres);
	ocfs2_build_lock_name(OCFS2_LOCK_TYPE_FLOCK, oi->ip_blkno,
			      inode->i_generation, lockres->l_name);
	ocfs2_lock_res_init_common(OCFS2_SB(inode->i_sb), lockres,
				   OCFS2_LOCK_TYPE_FLOCK, &ocfs2_flock_lops,
				   fp);
	lockres->l_flags |= OCFS2_LOCK_NOCACHE;
}

void ocfs2_lock_res_free(struct ocfs2_lock_res *res)
{
	mlog_entry_void();

	if (!(res->l_flags & OCFS2_LOCK_INITIALIZED))
		return;

	ocfs2_remove_lockres_tracking(res);

	mlog_bug_on_msg(!list_empty(&res->l_blocked_list),
			"Lockres %s is on the blocked list\n",
			res->l_name);
	mlog_bug_on_msg(!list_empty(&res->l_mask_waiters),
			"Lockres %s has mask waiters pending\n",
			res->l_name);
	mlog_bug_on_msg(spin_is_locked(&res->l_lock),
			"Lockres %s is locked\n",
			res->l_name);
	mlog_bug_on_msg(res->l_ro_holders,
			"Lockres %s has %u ro holders\n",
			res->l_name, res->l_ro_holders);
	mlog_bug_on_msg(res->l_ex_holders,
			"Lockres %s has %u ex holders\n",
			res->l_name, res->l_ex_holders);

	/* Need to clear out the lock status block for the dlm */
	memset(&res->l_lksb, 0, sizeof(res->l_lksb));

	res->l_flags = 0UL;
	mlog_exit_void();
}

static inline void ocfs2_inc_holders(struct ocfs2_lock_res *lockres,
				     int level)
{
	mlog_entry_void();

	BUG_ON(!lockres);

	switch(level) {
	case LKM_EXMODE:
		lockres->l_ex_holders++;
		break;
	case LKM_PRMODE:
		lockres->l_ro_holders++;
		break;
	default:
		BUG();
	}

	mlog_exit_void();
}

static inline void ocfs2_dec_holders(struct ocfs2_lock_res *lockres,
				     int level)
{
	mlog_entry_void();

	BUG_ON(!lockres);

	switch(level) {
	case LKM_EXMODE:
		BUG_ON(!lockres->l_ex_holders);
		lockres->l_ex_holders--;
		break;
	case LKM_PRMODE:
		BUG_ON(!lockres->l_ro_holders);
		lockres->l_ro_holders--;
		break;
	default:
		BUG();
	}
	mlog_exit_void();
}

/* WARNING: This function lives in a world where the only three lock
 * levels are EX, PR, and NL. It *will* have to be adjusted when more
 * lock types are added. */
static inline int ocfs2_highest_compat_lock_level(int level)
{
	int new_level = LKM_EXMODE;

	if (level == LKM_EXMODE)
		new_level = LKM_NLMODE;
	else if (level == LKM_PRMODE)
		new_level = LKM_PRMODE;
	return new_level;
}

static void lockres_set_flags(struct ocfs2_lock_res *lockres,
			      unsigned long newflags)
{
	struct ocfs2_mask_waiter *mw, *tmp;

 	assert_spin_locked(&lockres->l_lock);

	lockres->l_flags = newflags;

	list_for_each_entry_safe(mw, tmp, &lockres->l_mask_waiters, mw_item) {
		if ((lockres->l_flags & mw->mw_mask) != mw->mw_goal)
			continue;

		list_del_init(&mw->mw_item);
		mw->mw_status = 0;
		complete(&mw->mw_complete);
	}
}
static void lockres_or_flags(struct ocfs2_lock_res *lockres, unsigned long or)
{
	lockres_set_flags(lockres, lockres->l_flags | or);
}
static void lockres_clear_flags(struct ocfs2_lock_res *lockres,
				unsigned long clear)
{
	lockres_set_flags(lockres, lockres->l_flags & ~clear);
}

static inline void ocfs2_generic_handle_downconvert_action(struct ocfs2_lock_res *lockres)
{
	mlog_entry_void();

	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_BUSY));
	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_ATTACHED));
	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_BLOCKED));
	BUG_ON(lockres->l_blocking <= LKM_NLMODE);

	lockres->l_level = lockres->l_requested;
	if (lockres->l_level <=
	    ocfs2_highest_compat_lock_level(lockres->l_blocking)) {
		lockres->l_blocking = LKM_NLMODE;
		lockres_clear_flags(lockres, OCFS2_LOCK_BLOCKED);
	}
	lockres_clear_flags(lockres, OCFS2_LOCK_BUSY);

	mlog_exit_void();
}

static inline void ocfs2_generic_handle_convert_action(struct ocfs2_lock_res *lockres)
{
	mlog_entry_void();

	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_BUSY));
	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_ATTACHED));

	/* Convert from RO to EX doesn't really need anything as our
	 * information is already up to data. Convert from NL to
	 * *anything* however should mark ourselves as needing an
	 * update */
	if (lockres->l_level == LKM_NLMODE &&
	    lockres->l_ops->flags & LOCK_TYPE_REQUIRES_REFRESH)
		lockres_or_flags(lockres, OCFS2_LOCK_NEEDS_REFRESH);

	lockres->l_level = lockres->l_requested;
	lockres_clear_flags(lockres, OCFS2_LOCK_BUSY);

	mlog_exit_void();
}

static inline void ocfs2_generic_handle_attach_action(struct ocfs2_lock_res *lockres)
{
	mlog_entry_void();

	BUG_ON((!(lockres->l_flags & OCFS2_LOCK_BUSY)));
	BUG_ON(lockres->l_flags & OCFS2_LOCK_ATTACHED);

	if (lockres->l_requested > LKM_NLMODE &&
	    !(lockres->l_flags & OCFS2_LOCK_LOCAL) &&
	    lockres->l_ops->flags & LOCK_TYPE_REQUIRES_REFRESH)
		lockres_or_flags(lockres, OCFS2_LOCK_NEEDS_REFRESH);

	lockres->l_level = lockres->l_requested;
	lockres_or_flags(lockres, OCFS2_LOCK_ATTACHED);
	lockres_clear_flags(lockres, OCFS2_LOCK_BUSY);

	mlog_exit_void();
}

static int ocfs2_generic_handle_bast(struct ocfs2_lock_res *lockres,
				     int level)
{
	int needs_downconvert = 0;
	mlog_entry_void();

	assert_spin_locked(&lockres->l_lock);

	lockres_or_flags(lockres, OCFS2_LOCK_BLOCKED);

	if (level > lockres->l_blocking) {
		/* only schedule a downconvert if we haven't already scheduled
		 * one that goes low enough to satisfy the level we're
		 * blocking.  this also catches the case where we get
		 * duplicate BASTs */
		if (ocfs2_highest_compat_lock_level(level) <
		    ocfs2_highest_compat_lock_level(lockres->l_blocking))
			needs_downconvert = 1;

		lockres->l_blocking = level;
	}

	mlog_exit(needs_downconvert);
	return needs_downconvert;
}

static void ocfs2_blocking_ast(void *opaque, int level)
{
	struct ocfs2_lock_res *lockres = opaque;
	struct ocfs2_super *osb = ocfs2_get_lockres_osb(lockres);
	int needs_downconvert;
	unsigned long flags;

	BUG_ON(level <= LKM_NLMODE);

	mlog(0, "BAST fired for lockres %s, blocking %d, level %d type %s\n",
	     lockres->l_name, level, lockres->l_level,
	     ocfs2_lock_type_string(lockres->l_type));

	/*
	 * We can skip the bast for locks which don't enable caching -
	 * they'll be dropped at the earliest possible time anyway.
	 */
	if (lockres->l_flags & OCFS2_LOCK_NOCACHE)
		return;

	spin_lock_irqsave(&lockres->l_lock, flags);
	needs_downconvert = ocfs2_generic_handle_bast(lockres, level);
	if (needs_downconvert)
		ocfs2_schedule_blocked_lock(osb, lockres);
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	wake_up(&lockres->l_event);

	ocfs2_wake_downconvert_thread(osb);
}

static void ocfs2_locking_ast(void *opaque)
{
	struct ocfs2_lock_res *lockres = opaque;
	struct dlm_lockstatus *lksb = &lockres->l_lksb;
	unsigned long flags;

	spin_lock_irqsave(&lockres->l_lock, flags);

	if (lksb->status != DLM_NORMAL) {
		mlog(ML_ERROR, "lockres %s: lksb status value of %u!\n",
		     lockres->l_name, lksb->status);
		spin_unlock_irqrestore(&lockres->l_lock, flags);
		return;
	}

	switch(lockres->l_action) {
	case OCFS2_AST_ATTACH:
		ocfs2_generic_handle_attach_action(lockres);
		lockres_clear_flags(lockres, OCFS2_LOCK_LOCAL);
		break;
	case OCFS2_AST_CONVERT:
		ocfs2_generic_handle_convert_action(lockres);
		break;
	case OCFS2_AST_DOWNCONVERT:
		ocfs2_generic_handle_downconvert_action(lockres);
		break;
	default:
		mlog(ML_ERROR, "lockres %s: ast fired with invalid action: %u "
		     "lockres flags = 0x%lx, unlock action: %u\n",
		     lockres->l_name, lockres->l_action, lockres->l_flags,
		     lockres->l_unlock_action);
		BUG();
	}

	/* set it to something invalid so if we get called again we
	 * can catch it. */
	lockres->l_action = OCFS2_AST_INVALID;

	wake_up(&lockres->l_event);
	spin_unlock_irqrestore(&lockres->l_lock, flags);
}

static inline void ocfs2_recover_from_dlm_error(struct ocfs2_lock_res *lockres,
						int convert)
{
	unsigned long flags;

	mlog_entry_void();
	spin_lock_irqsave(&lockres->l_lock, flags);
	lockres_clear_flags(lockres, OCFS2_LOCK_BUSY);
	if (convert)
		lockres->l_action = OCFS2_AST_INVALID;
	else
		lockres->l_unlock_action = OCFS2_UNLOCK_INVALID;
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	wake_up(&lockres->l_event);
	mlog_exit_void();
}

/* Note: If we detect another process working on the lock (i.e.,
 * OCFS2_LOCK_BUSY), we'll bail out returning 0. It's up to the caller
 * to do the right thing in that case.
 */
static int ocfs2_lock_create(struct ocfs2_super *osb,
			     struct ocfs2_lock_res *lockres,
			     int level,
			     int dlm_flags)
{
	int ret = 0;
	enum dlm_status status = DLM_NORMAL;
	unsigned long flags;

	mlog_entry_void();

	mlog(0, "lock %s, level = %d, flags = %d\n", lockres->l_name, level,
	     dlm_flags);

	spin_lock_irqsave(&lockres->l_lock, flags);
	if ((lockres->l_flags & OCFS2_LOCK_ATTACHED) ||
	    (lockres->l_flags & OCFS2_LOCK_BUSY)) {
		spin_unlock_irqrestore(&lockres->l_lock, flags);
		goto bail;
	}

	lockres->l_action = OCFS2_AST_ATTACH;
	lockres->l_requested = level;
	lockres_or_flags(lockres, OCFS2_LOCK_BUSY);
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	status = dlmlock(osb->dlm,
			 level,
			 &lockres->l_lksb,
			 dlm_flags,
			 lockres->l_name,
			 OCFS2_LOCK_ID_MAX_LEN - 1,
			 ocfs2_locking_ast,
			 lockres,
			 ocfs2_blocking_ast);
	if (status != DLM_NORMAL) {
		ocfs2_log_dlm_error("dlmlock", status, lockres);
		ret = -EINVAL;
		ocfs2_recover_from_dlm_error(lockres, 1);
	}

	mlog(0, "lock %s, successfull return from dlmlock\n", lockres->l_name);

bail:
	mlog_exit(ret);
	return ret;
}

static inline int ocfs2_check_wait_flag(struct ocfs2_lock_res *lockres,
					int flag)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&lockres->l_lock, flags);
	ret = lockres->l_flags & flag;
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	return ret;
}

static inline void ocfs2_wait_on_busy_lock(struct ocfs2_lock_res *lockres)

{
	wait_event(lockres->l_event,
		   !ocfs2_check_wait_flag(lockres, OCFS2_LOCK_BUSY));
}

static inline void ocfs2_wait_on_refreshing_lock(struct ocfs2_lock_res *lockres)

{
	wait_event(lockres->l_event,
		   !ocfs2_check_wait_flag(lockres, OCFS2_LOCK_REFRESHING));
}

/* predict what lock level we'll be dropping down to on behalf
 * of another node, and return true if the currently wanted
 * level will be compatible with it. */
static inline int ocfs2_may_continue_on_blocked_lock(struct ocfs2_lock_res *lockres,
						     int wanted)
{
	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_BLOCKED));

	return wanted <= ocfs2_highest_compat_lock_level(lockres->l_blocking);
}

static void ocfs2_init_mask_waiter(struct ocfs2_mask_waiter *mw)
{
	INIT_LIST_HEAD(&mw->mw_item);
	init_completion(&mw->mw_complete);
}

static int ocfs2_wait_for_mask(struct ocfs2_mask_waiter *mw)
{
	wait_for_completion(&mw->mw_complete);
	/* Re-arm the completion in case we want to wait on it again */
	INIT_COMPLETION(mw->mw_complete);
	return mw->mw_status;
}

static void lockres_add_mask_waiter(struct ocfs2_lock_res *lockres,
				    struct ocfs2_mask_waiter *mw,
				    unsigned long mask,
				    unsigned long goal)
{
	BUG_ON(!list_empty(&mw->mw_item));

	assert_spin_locked(&lockres->l_lock);

	list_add_tail(&mw->mw_item, &lockres->l_mask_waiters);
	mw->mw_mask = mask;
	mw->mw_goal = goal;
}

/* returns 0 if the mw that was removed was already satisfied, -EBUSY
 * if the mask still hadn't reached its goal */
static int lockres_remove_mask_waiter(struct ocfs2_lock_res *lockres,
				      struct ocfs2_mask_waiter *mw)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&lockres->l_lock, flags);
	if (!list_empty(&mw->mw_item)) {
		if ((lockres->l_flags & mw->mw_mask) != mw->mw_goal)
			ret = -EBUSY;

		list_del_init(&mw->mw_item);
		init_completion(&mw->mw_complete);
	}
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	return ret;

}

static int ocfs2_wait_for_mask_interruptible(struct ocfs2_mask_waiter *mw,
					     struct ocfs2_lock_res *lockres)
{
	int ret;

	ret = wait_for_completion_interruptible(&mw->mw_complete);
	if (ret)
		lockres_remove_mask_waiter(lockres, mw);
	else
		ret = mw->mw_status;
	/* Re-arm the completion in case we want to wait on it again */
	INIT_COMPLETION(mw->mw_complete);
	return ret;
}

static int ocfs2_cluster_lock(struct ocfs2_super *osb,
			      struct ocfs2_lock_res *lockres,
			      int level,
			      int lkm_flags,
			      int arg_flags)
{
	struct ocfs2_mask_waiter mw;
	enum dlm_status status;
	int wait, catch_signals = !(osb->s_mount_opt & OCFS2_MOUNT_NOINTR);
	int ret = 0; /* gcc doesn't realize wait = 1 guarantees ret is set */
	unsigned long flags;

	mlog_entry_void();

	ocfs2_init_mask_waiter(&mw);

	if (lockres->l_ops->flags & LOCK_TYPE_USES_LVB)
		lkm_flags |= LKM_VALBLK;

again:
	wait = 0;

	if (catch_signals && signal_pending(current)) {
		ret = -ERESTARTSYS;
		goto out;
	}

	spin_lock_irqsave(&lockres->l_lock, flags);

	mlog_bug_on_msg(lockres->l_flags & OCFS2_LOCK_FREEING,
			"Cluster lock called on freeing lockres %s! flags "
			"0x%lx\n", lockres->l_name, lockres->l_flags);

	/* We only compare against the currently granted level
	 * here. If the lock is blocked waiting on a downconvert,
	 * we'll get caught below. */
	if (lockres->l_flags & OCFS2_LOCK_BUSY &&
	    level > lockres->l_level) {
		/* is someone sitting in dlm_lock? If so, wait on
		 * them. */
		lockres_add_mask_waiter(lockres, &mw, OCFS2_LOCK_BUSY, 0);
		wait = 1;
		goto unlock;
	}

	if (lockres->l_flags & OCFS2_LOCK_BLOCKED &&
	    !ocfs2_may_continue_on_blocked_lock(lockres, level)) {
		/* is the lock is currently blocked on behalf of
		 * another node */
		lockres_add_mask_waiter(lockres, &mw, OCFS2_LOCK_BLOCKED, 0);
		wait = 1;
		goto unlock;
	}

	if (level > lockres->l_level) {
		if (lockres->l_action != OCFS2_AST_INVALID)
			mlog(ML_ERROR, "lockres %s has action %u pending\n",
			     lockres->l_name, lockres->l_action);

		if (!(lockres->l_flags & OCFS2_LOCK_ATTACHED)) {
			lockres->l_action = OCFS2_AST_ATTACH;
			lkm_flags &= ~LKM_CONVERT;
		} else {
			lockres->l_action = OCFS2_AST_CONVERT;
			lkm_flags |= LKM_CONVERT;
		}

		lockres->l_requested = level;
		lockres_or_flags(lockres, OCFS2_LOCK_BUSY);
		spin_unlock_irqrestore(&lockres->l_lock, flags);

		BUG_ON(level == LKM_IVMODE);
		BUG_ON(level == LKM_NLMODE);

		mlog(0, "lock %s, convert from %d to level = %d\n",
		     lockres->l_name, lockres->l_level, level);

		/* call dlm_lock to upgrade lock now */
		status = dlmlock(osb->dlm,
				 level,
				 &lockres->l_lksb,
				 lkm_flags,
				 lockres->l_name,
				 OCFS2_LOCK_ID_MAX_LEN - 1,
				 ocfs2_locking_ast,
				 lockres,
				 ocfs2_blocking_ast);
		if (status != DLM_NORMAL) {
			if ((lkm_flags & LKM_NOQUEUE) &&
			    (status == DLM_NOTQUEUED))
				ret = -EAGAIN;
			else {
				ocfs2_log_dlm_error("dlmlock", status,
						    lockres);
				ret = -EINVAL;
			}
			ocfs2_recover_from_dlm_error(lockres, 1);
			goto out;
		}

		mlog(0, "lock %s, successfull return from dlmlock\n",
		     lockres->l_name);

		/* At this point we've gone inside the dlm and need to
		 * complete our work regardless. */
		catch_signals = 0;

		/* wait for busy to clear and carry on */
		goto again;
	}

	/* Ok, if we get here then we're good to go. */
	ocfs2_inc_holders(lockres, level);

	ret = 0;
unlock:
	spin_unlock_irqrestore(&lockres->l_lock, flags);
out:
	/*
	 * This is helping work around a lock inversion between the page lock
	 * and dlm locks.  One path holds the page lock while calling aops
	 * which block acquiring dlm locks.  The voting thread holds dlm
	 * locks while acquiring page locks while down converting data locks.
	 * This block is helping an aop path notice the inversion and back
	 * off to unlock its page lock before trying the dlm lock again.
	 */
	if (wait && arg_flags & OCFS2_LOCK_NONBLOCK &&
	    mw.mw_mask & (OCFS2_LOCK_BUSY|OCFS2_LOCK_BLOCKED)) {
		wait = 0;
		if (lockres_remove_mask_waiter(lockres, &mw))
			ret = -EAGAIN;
		else
			goto again;
	}
	if (wait) {
		ret = ocfs2_wait_for_mask(&mw);
		if (ret == 0)
			goto again;
		mlog_errno(ret);
	}

	mlog_exit(ret);
	return ret;
}

static void ocfs2_cluster_unlock(struct ocfs2_super *osb,
				 struct ocfs2_lock_res *lockres,
				 int level)
{
	unsigned long flags;

	mlog_entry_void();
	spin_lock_irqsave(&lockres->l_lock, flags);
	ocfs2_dec_holders(lockres, level);
	ocfs2_downconvert_on_unlock(osb, lockres);
	spin_unlock_irqrestore(&lockres->l_lock, flags);
	mlog_exit_void();
}

static int ocfs2_create_new_lock(struct ocfs2_super *osb,
				 struct ocfs2_lock_res *lockres,
				 int ex,
				 int local)
{
	int level =  ex ? LKM_EXMODE : LKM_PRMODE;
	unsigned long flags;
	int lkm_flags = local ? LKM_LOCAL : 0;

	spin_lock_irqsave(&lockres->l_lock, flags);
	BUG_ON(lockres->l_flags & OCFS2_LOCK_ATTACHED);
	lockres_or_flags(lockres, OCFS2_LOCK_LOCAL);
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	return ocfs2_lock_create(osb, lockres, level, lkm_flags);
}

/* Grants us an EX lock on the data and metadata resources, skipping
 * the normal cluster directory lookup. Use this ONLY on newly created
 * inodes which other nodes can't possibly see, and which haven't been
 * hashed in the inode hash yet. This can give us a good performance
 * increase as it'll skip the network broadcast normally associated
 * with creating a new lock resource. */
int ocfs2_create_new_inode_locks(struct inode *inode)
{
	int ret;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	BUG_ON(!inode);
	BUG_ON(!ocfs2_inode_is_new(inode));

	mlog_entry_void();

	mlog(0, "Inode %llu\n", (unsigned long long)OCFS2_I(inode)->ip_blkno);

	/* NOTE: That we don't increment any of the holder counts, nor
	 * do we add anything to a journal handle. Since this is
	 * supposed to be a new inode which the cluster doesn't know
	 * about yet, there is no need to.  As far as the LVB handling
	 * is concerned, this is basically like acquiring an EX lock
	 * on a resource which has an invalid one -- we'll set it
	 * valid when we release the EX. */

	ret = ocfs2_create_new_lock(osb, &OCFS2_I(inode)->ip_rw_lockres, 1, 1);
	if (ret) {
		mlog_errno(ret);
		goto bail;
	}

	/*
	 * We don't want to use LKM_LOCAL on a meta data lock as they
	 * don't use a generation in their lock names.
	 */
	ret = ocfs2_create_new_lock(osb, &OCFS2_I(inode)->ip_inode_lockres, 1, 0);
	if (ret) {
		mlog_errno(ret);
		goto bail;
	}

	ret = ocfs2_create_new_lock(osb, &OCFS2_I(inode)->ip_open_lockres, 0, 0);
	if (ret) {
		mlog_errno(ret);
		goto bail;
	}

bail:
	mlog_exit(ret);
	return ret;
}

int ocfs2_rw_lock(struct inode *inode, int write)
{
	int status, level;
	struct ocfs2_lock_res *lockres;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	BUG_ON(!inode);

	mlog_entry_void();

	mlog(0, "inode %llu take %s RW lock\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno,
	     write ? "EXMODE" : "PRMODE");

	if (ocfs2_mount_local(osb))
		return 0;

	lockres = &OCFS2_I(inode)->ip_rw_lockres;

	level = write ? LKM_EXMODE : LKM_PRMODE;

	status = ocfs2_cluster_lock(OCFS2_SB(inode->i_sb), lockres, level, 0,
				    0);
	if (status < 0)
		mlog_errno(status);

	mlog_exit(status);
	return status;
}

void ocfs2_rw_unlock(struct inode *inode, int write)
{
	int level = write ? LKM_EXMODE : LKM_PRMODE;
	struct ocfs2_lock_res *lockres = &OCFS2_I(inode)->ip_rw_lockres;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	mlog_entry_void();

	mlog(0, "inode %llu drop %s RW lock\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno,
	     write ? "EXMODE" : "PRMODE");

	if (!ocfs2_mount_local(osb))
		ocfs2_cluster_unlock(OCFS2_SB(inode->i_sb), lockres, level);

	mlog_exit_void();
}

/*
 * ocfs2_open_lock always get PR mode lock.
 */
int ocfs2_open_lock(struct inode *inode)
{
	int status = 0;
	struct ocfs2_lock_res *lockres;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	BUG_ON(!inode);

	mlog_entry_void();

	mlog(0, "inode %llu take PRMODE open lock\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno);

	if (ocfs2_mount_local(osb))
		goto out;

	lockres = &OCFS2_I(inode)->ip_open_lockres;

	status = ocfs2_cluster_lock(OCFS2_SB(inode->i_sb), lockres,
				    LKM_PRMODE, 0, 0);
	if (status < 0)
		mlog_errno(status);

out:
	mlog_exit(status);
	return status;
}

int ocfs2_try_open_lock(struct inode *inode, int write)
{
	int status = 0, level;
	struct ocfs2_lock_res *lockres;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	BUG_ON(!inode);

	mlog_entry_void();

	mlog(0, "inode %llu try to take %s open lock\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno,
	     write ? "EXMODE" : "PRMODE");

	if (ocfs2_mount_local(osb))
		goto out;

	lockres = &OCFS2_I(inode)->ip_open_lockres;

	level = write ? LKM_EXMODE : LKM_PRMODE;

	/*
	 * The file system may already holding a PRMODE/EXMODE open lock.
	 * Since we pass LKM_NOQUEUE, the request won't block waiting on
	 * other nodes and the -EAGAIN will indicate to the caller that
	 * this inode is still in use.
	 */
	status = ocfs2_cluster_lock(OCFS2_SB(inode->i_sb), lockres,
				    level, LKM_NOQUEUE, 0);

out:
	mlog_exit(status);
	return status;
}

/*
 * ocfs2_open_unlock unlock PR and EX mode open locks.
 */
void ocfs2_open_unlock(struct inode *inode)
{
	struct ocfs2_lock_res *lockres = &OCFS2_I(inode)->ip_open_lockres;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	mlog_entry_void();

	mlog(0, "inode %llu drop open lock\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno);

	if (ocfs2_mount_local(osb))
		goto out;

	if(lockres->l_ro_holders)
		ocfs2_cluster_unlock(OCFS2_SB(inode->i_sb), lockres,
				     LKM_PRMODE);
	if(lockres->l_ex_holders)
		ocfs2_cluster_unlock(OCFS2_SB(inode->i_sb), lockres,
				     LKM_EXMODE);

out:
	mlog_exit_void();
}

static int ocfs2_flock_handle_signal(struct ocfs2_lock_res *lockres,
				     int level)
{
	int ret;
	struct ocfs2_super *osb = ocfs2_get_lockres_osb(lockres);
	unsigned long flags;
	struct ocfs2_mask_waiter mw;

	ocfs2_init_mask_waiter(&mw);

retry_cancel:
	spin_lock_irqsave(&lockres->l_lock, flags);
	if (lockres->l_flags & OCFS2_LOCK_BUSY) {
		ret = ocfs2_prepare_cancel_convert(osb, lockres);
		if (ret) {
			spin_unlock_irqrestore(&lockres->l_lock, flags);
			ret = ocfs2_cancel_convert(osb, lockres);
			if (ret < 0) {
				mlog_errno(ret);
				goto out;
			}
			goto retry_cancel;
		}
		lockres_add_mask_waiter(lockres, &mw, OCFS2_LOCK_BUSY, 0);
		spin_unlock_irqrestore(&lockres->l_lock, flags);

		ocfs2_wait_for_mask(&mw);
		goto retry_cancel;
	}

	ret = -ERESTARTSYS;
	/*
	 * We may still have gotten the lock, in which case there's no
	 * point to restarting the syscall.
	 */
	if (lockres->l_level == level)
		ret = 0;

	mlog(0, "Cancel returning %d. flags: 0x%lx, level: %d, act: %d\n", ret,
	     lockres->l_flags, lockres->l_level, lockres->l_action);

	spin_unlock_irqrestore(&lockres->l_lock, flags);

out:
	return ret;
}

/*
 * ocfs2_file_lock() and ocfs2_file_unlock() map to a single pair of
 * flock() calls. The locking approach this requires is sufficiently
 * different from all other cluster lock types that we implement a
 * seperate path to the "low-level" dlm calls. In particular:
 *
 * - No optimization of lock levels is done - we take at exactly
 *   what's been requested.
 *
 * - No lock caching is employed. We immediately downconvert to
 *   no-lock at unlock time. This also means flock locks never go on
 *   the blocking list).
 *
 * - Since userspace can trivially deadlock itself with flock, we make
 *   sure to allow cancellation of a misbehaving applications flock()
 *   request.
 *
 * - Access to any flock lockres doesn't require concurrency, so we
 *   can simplify the code by requiring the caller to guarantee
 *   serialization of dlmglue flock calls.
 */
int ocfs2_file_lock(struct file *file, int ex, int trylock)
{
	int ret, level = ex ? LKM_EXMODE : LKM_PRMODE;
	unsigned int lkm_flags = trylock ? LKM_NOQUEUE : 0;
	unsigned long flags;
	struct ocfs2_file_private *fp = file->private_data;
	struct ocfs2_lock_res *lockres = &fp->fp_flock;
	struct ocfs2_super *osb = OCFS2_SB(file->f_mapping->host->i_sb);
	struct ocfs2_mask_waiter mw;

	ocfs2_init_mask_waiter(&mw);

	if ((lockres->l_flags & OCFS2_LOCK_BUSY) ||
	    (lockres->l_level > LKM_NLMODE)) {
		mlog(ML_ERROR,
		     "File lock \"%s\" has busy or locked state: flags: 0x%lx, "
		     "level: %u\n", lockres->l_name, lockres->l_flags,
		     lockres->l_level);
		return -EINVAL;
	}

	spin_lock_irqsave(&lockres->l_lock, flags);
	if (!(lockres->l_flags & OCFS2_LOCK_ATTACHED)) {
		lockres_add_mask_waiter(lockres, &mw, OCFS2_LOCK_BUSY, 0);
		spin_unlock_irqrestore(&lockres->l_lock, flags);

		/*
		 * Get the lock at NLMODE to start - that way we
		 * can cancel the upconvert request if need be.
		 */
		ret = ocfs2_lock_create(osb, lockres, LKM_NLMODE, 0);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_wait_for_mask(&mw);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
		spin_lock_irqsave(&lockres->l_lock, flags);
	}

	lockres->l_action = OCFS2_AST_CONVERT;
	lkm_flags |= LKM_CONVERT;
	lockres->l_requested = level;
	lockres_or_flags(lockres, OCFS2_LOCK_BUSY);

	lockres_add_mask_waiter(lockres, &mw, OCFS2_LOCK_BUSY, 0);
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	ret = dlmlock(osb->dlm, level, &lockres->l_lksb, lkm_flags,
		      lockres->l_name, OCFS2_LOCK_ID_MAX_LEN - 1,
		      ocfs2_locking_ast, lockres, ocfs2_blocking_ast);
	if (ret != DLM_NORMAL) {
		if (trylock && ret == DLM_NOTQUEUED)
			ret = -EAGAIN;
		else {
			ocfs2_log_dlm_error("dlmlock", ret, lockres);
			ret = -EINVAL;
		}

		ocfs2_recover_from_dlm_error(lockres, 1);
		lockres_remove_mask_waiter(lockres, &mw);
		goto out;
	}

	ret = ocfs2_wait_for_mask_interruptible(&mw, lockres);
	if (ret == -ERESTARTSYS) {
		/*
		 * Userspace can cause deadlock itself with
		 * flock(). Current behavior locally is to allow the
		 * deadlock, but abort the system call if a signal is
		 * received. We follow this example, otherwise a
		 * poorly written program could sit in kernel until
		 * reboot.
		 *
		 * Handling this is a bit more complicated for Ocfs2
		 * though. We can't exit this function with an
		 * outstanding lock request, so a cancel convert is
		 * required. We intentionally overwrite 'ret' - if the
		 * cancel fails and the lock was granted, it's easier
		 * to just bubble sucess back up to the user.
		 */
		ret = ocfs2_flock_handle_signal(lockres, level);
	}

out:

	mlog(0, "Lock: \"%s\" ex: %d, trylock: %d, returns: %d\n",
	     lockres->l_name, ex, trylock, ret);
	return ret;
}

void ocfs2_file_unlock(struct file *file)
{
	int ret;
	unsigned long flags;
	struct ocfs2_file_private *fp = file->private_data;
	struct ocfs2_lock_res *lockres = &fp->fp_flock;
	struct ocfs2_super *osb = OCFS2_SB(file->f_mapping->host->i_sb);
	struct ocfs2_mask_waiter mw;

	ocfs2_init_mask_waiter(&mw);

	if (!(lockres->l_flags & OCFS2_LOCK_ATTACHED))
		return;

	if (lockres->l_level == LKM_NLMODE)
		return;

	mlog(0, "Unlock: \"%s\" flags: 0x%lx, level: %d, act: %d\n",
	     lockres->l_name, lockres->l_flags, lockres->l_level,
	     lockres->l_action);

	spin_lock_irqsave(&lockres->l_lock, flags);
	/*
	 * Fake a blocking ast for the downconvert code.
	 */
	lockres_or_flags(lockres, OCFS2_LOCK_BLOCKED);
	lockres->l_blocking = LKM_EXMODE;

	ocfs2_prepare_downconvert(lockres, LKM_NLMODE);
	lockres_add_mask_waiter(lockres, &mw, OCFS2_LOCK_BUSY, 0);
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	ret = ocfs2_downconvert_lock(osb, lockres, LKM_NLMODE, 0);
	if (ret) {
		mlog_errno(ret);
		return;
	}

	ret = ocfs2_wait_for_mask(&mw);
	if (ret)
		mlog_errno(ret);
}

static void ocfs2_downconvert_on_unlock(struct ocfs2_super *osb,
					struct ocfs2_lock_res *lockres)
{
	int kick = 0;

	mlog_entry_void();

	/* If we know that another node is waiting on our lock, kick
	 * the downconvert thread * pre-emptively when we reach a release
	 * condition. */
	if (lockres->l_flags & OCFS2_LOCK_BLOCKED) {
		switch(lockres->l_blocking) {
		case LKM_EXMODE:
			if (!lockres->l_ex_holders && !lockres->l_ro_holders)
				kick = 1;
			break;
		case LKM_PRMODE:
			if (!lockres->l_ex_holders)
				kick = 1;
			break;
		default:
			BUG();
		}
	}

	if (kick)
		ocfs2_wake_downconvert_thread(osb);

	mlog_exit_void();
}

#define OCFS2_SEC_BITS   34
#define OCFS2_SEC_SHIFT  (64 - 34)
#define OCFS2_NSEC_MASK  ((1ULL << OCFS2_SEC_SHIFT) - 1)

/* LVB only has room for 64 bits of time here so we pack it for
 * now. */
static u64 ocfs2_pack_timespec(struct timespec *spec)
{
	u64 res;
	u64 sec = spec->tv_sec;
	u32 nsec = spec->tv_nsec;

	res = (sec << OCFS2_SEC_SHIFT) | (nsec & OCFS2_NSEC_MASK);

	return res;
}

/* Call this with the lockres locked. I am reasonably sure we don't
 * need ip_lock in this function as anyone who would be changing those
 * values is supposed to be blocked in ocfs2_inode_lock right now. */
static void __ocfs2_stuff_meta_lvb(struct inode *inode)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_lock_res *lockres = &oi->ip_inode_lockres;
	struct ocfs2_meta_lvb *lvb;

	mlog_entry_void();

	lvb = (struct ocfs2_meta_lvb *) lockres->l_lksb.lvb;

	/*
	 * Invalidate the LVB of a deleted inode - this way other
	 * nodes are forced to go to disk and discover the new inode
	 * status.
	 */
	if (oi->ip_flags & OCFS2_INODE_DELETED) {
		lvb->lvb_version = 0;
		goto out;
	}

	lvb->lvb_version   = OCFS2_LVB_VERSION;
	lvb->lvb_isize	   = cpu_to_be64(i_size_read(inode));
	lvb->lvb_iclusters = cpu_to_be32(oi->ip_clusters);
	lvb->lvb_iuid      = cpu_to_be32(inode->i_uid);
	lvb->lvb_igid      = cpu_to_be32(inode->i_gid);
	lvb->lvb_imode     = cpu_to_be16(inode->i_mode);
	lvb->lvb_inlink    = cpu_to_be16(inode->i_nlink);
	lvb->lvb_iatime_packed  =
		cpu_to_be64(ocfs2_pack_timespec(&inode->i_atime));
	lvb->lvb_ictime_packed =
		cpu_to_be64(ocfs2_pack_timespec(&inode->i_ctime));
	lvb->lvb_imtime_packed =
		cpu_to_be64(ocfs2_pack_timespec(&inode->i_mtime));
	lvb->lvb_iattr    = cpu_to_be32(oi->ip_attr);
	lvb->lvb_idynfeatures = cpu_to_be16(oi->ip_dyn_features);
	lvb->lvb_igeneration = cpu_to_be32(inode->i_generation);

out:
	mlog_meta_lvb(0, lockres);

	mlog_exit_void();
}

static void ocfs2_unpack_timespec(struct timespec *spec,
				  u64 packed_time)
{
	spec->tv_sec = packed_time >> OCFS2_SEC_SHIFT;
	spec->tv_nsec = packed_time & OCFS2_NSEC_MASK;
}

static void ocfs2_refresh_inode_from_lvb(struct inode *inode)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_lock_res *lockres = &oi->ip_inode_lockres;
	struct ocfs2_meta_lvb *lvb;

	mlog_entry_void();

	mlog_meta_lvb(0, lockres);

	lvb = (struct ocfs2_meta_lvb *) lockres->l_lksb.lvb;

	/* We're safe here without the lockres lock... */
	spin_lock(&oi->ip_lock);
	oi->ip_clusters = be32_to_cpu(lvb->lvb_iclusters);
	i_size_write(inode, be64_to_cpu(lvb->lvb_isize));

	oi->ip_attr = be32_to_cpu(lvb->lvb_iattr);
	oi->ip_dyn_features = be16_to_cpu(lvb->lvb_idynfeatures);
	ocfs2_set_inode_flags(inode);

	/* fast-symlinks are a special case */
	if (S_ISLNK(inode->i_mode) && !oi->ip_clusters)
		inode->i_blocks = 0;
	else
		inode->i_blocks = ocfs2_inode_sector_count(inode);

	inode->i_uid     = be32_to_cpu(lvb->lvb_iuid);
	inode->i_gid     = be32_to_cpu(lvb->lvb_igid);
	inode->i_mode    = be16_to_cpu(lvb->lvb_imode);
	inode->i_nlink   = be16_to_cpu(lvb->lvb_inlink);
	ocfs2_unpack_timespec(&inode->i_atime,
			      be64_to_cpu(lvb->lvb_iatime_packed));
	ocfs2_unpack_timespec(&inode->i_mtime,
			      be64_to_cpu(lvb->lvb_imtime_packed));
	ocfs2_unpack_timespec(&inode->i_ctime,
			      be64_to_cpu(lvb->lvb_ictime_packed));
	spin_unlock(&oi->ip_lock);

	mlog_exit_void();
}

static inline int ocfs2_meta_lvb_is_trustable(struct inode *inode,
					      struct ocfs2_lock_res *lockres)
{
	struct ocfs2_meta_lvb *lvb = (struct ocfs2_meta_lvb *) lockres->l_lksb.lvb;

	if (lvb->lvb_version == OCFS2_LVB_VERSION
	    && be32_to_cpu(lvb->lvb_igeneration) == inode->i_generation)
		return 1;
	return 0;
}

/* Determine whether a lock resource needs to be refreshed, and
 * arbitrate who gets to refresh it.
 *
 *   0 means no refresh needed.
 *
 *   > 0 means you need to refresh this and you MUST call
 *   ocfs2_complete_lock_res_refresh afterwards. */
static int ocfs2_should_refresh_lock_res(struct ocfs2_lock_res *lockres)
{
	unsigned long flags;
	int status = 0;

	mlog_entry_void();

refresh_check:
	spin_lock_irqsave(&lockres->l_lock, flags);
	if (!(lockres->l_flags & OCFS2_LOCK_NEEDS_REFRESH)) {
		spin_unlock_irqrestore(&lockres->l_lock, flags);
		goto bail;
	}

	if (lockres->l_flags & OCFS2_LOCK_REFRESHING) {
		spin_unlock_irqrestore(&lockres->l_lock, flags);

		ocfs2_wait_on_refreshing_lock(lockres);
		goto refresh_check;
	}

	/* Ok, I'll be the one to refresh this lock. */
	lockres_or_flags(lockres, OCFS2_LOCK_REFRESHING);
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	status = 1;
bail:
	mlog_exit(status);
	return status;
}

/* If status is non zero, I'll mark it as not being in refresh
 * anymroe, but i won't clear the needs refresh flag. */
static inline void ocfs2_complete_lock_res_refresh(struct ocfs2_lock_res *lockres,
						   int status)
{
	unsigned long flags;
	mlog_entry_void();

	spin_lock_irqsave(&lockres->l_lock, flags);
	lockres_clear_flags(lockres, OCFS2_LOCK_REFRESHING);
	if (!status)
		lockres_clear_flags(lockres, OCFS2_LOCK_NEEDS_REFRESH);
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	wake_up(&lockres->l_event);

	mlog_exit_void();
}

/* may or may not return a bh if it went to disk. */
static int ocfs2_inode_lock_update(struct inode *inode,
				  struct buffer_head **bh)
{
	int status = 0;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_lock_res *lockres = &oi->ip_inode_lockres;
	struct ocfs2_dinode *fe;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	mlog_entry_void();

	if (ocfs2_mount_local(osb))
		goto bail;

	spin_lock(&oi->ip_lock);
	if (oi->ip_flags & OCFS2_INODE_DELETED) {
		mlog(0, "Orphaned inode %llu was deleted while we "
		     "were waiting on a lock. ip_flags = 0x%x\n",
		     (unsigned long long)oi->ip_blkno, oi->ip_flags);
		spin_unlock(&oi->ip_lock);
		status = -ENOENT;
		goto bail;
	}
	spin_unlock(&oi->ip_lock);

	if (!ocfs2_should_refresh_lock_res(lockres))
		goto bail;

	/* This will discard any caching information we might have had
	 * for the inode metadata. */
	ocfs2_metadata_cache_purge(inode);

	ocfs2_extent_map_trunc(inode, 0);

	if (ocfs2_meta_lvb_is_trustable(inode, lockres)) {
		mlog(0, "Trusting LVB on inode %llu\n",
		     (unsigned long long)oi->ip_blkno);
		ocfs2_refresh_inode_from_lvb(inode);
	} else {
		/* Boo, we have to go to disk. */
		/* read bh, cast, ocfs2_refresh_inode */
		status = ocfs2_read_block(OCFS2_SB(inode->i_sb), oi->ip_blkno,
					  bh, OCFS2_BH_CACHED, inode);
		if (status < 0) {
			mlog_errno(status);
			goto bail_refresh;
		}
		fe = (struct ocfs2_dinode *) (*bh)->b_data;

		/* This is a good chance to make sure we're not
		 * locking an invalid object.
		 *
		 * We bug on a stale inode here because we checked
		 * above whether it was wiped from disk. The wiping
		 * node provides a guarantee that we receive that
		 * message and can mark the inode before dropping any
		 * locks associated with it. */
		if (!OCFS2_IS_VALID_DINODE(fe)) {
			OCFS2_RO_ON_INVALID_DINODE(inode->i_sb, fe);
			status = -EIO;
			goto bail_refresh;
		}
		mlog_bug_on_msg(inode->i_generation !=
				le32_to_cpu(fe->i_generation),
				"Invalid dinode %llu disk generation: %u "
				"inode->i_generation: %u\n",
				(unsigned long long)oi->ip_blkno,
				le32_to_cpu(fe->i_generation),
				inode->i_generation);
		mlog_bug_on_msg(le64_to_cpu(fe->i_dtime) ||
				!(fe->i_flags & cpu_to_le32(OCFS2_VALID_FL)),
				"Stale dinode %llu dtime: %llu flags: 0x%x\n",
				(unsigned long long)oi->ip_blkno,
				(unsigned long long)le64_to_cpu(fe->i_dtime),
				le32_to_cpu(fe->i_flags));

		ocfs2_refresh_inode(inode, fe);
	}

	status = 0;
bail_refresh:
	ocfs2_complete_lock_res_refresh(lockres, status);
bail:
	mlog_exit(status);
	return status;
}

static int ocfs2_assign_bh(struct inode *inode,
			   struct buffer_head **ret_bh,
			   struct buffer_head *passed_bh)
{
	int status;

	if (passed_bh) {
		/* Ok, the update went to disk for us, use the
		 * returned bh. */
		*ret_bh = passed_bh;
		get_bh(*ret_bh);

		return 0;
	}

	status = ocfs2_read_block(OCFS2_SB(inode->i_sb),
				  OCFS2_I(inode)->ip_blkno,
				  ret_bh,
				  OCFS2_BH_CACHED,
				  inode);
	if (status < 0)
		mlog_errno(status);

	return status;
}

/*
 * returns < 0 error if the callback will never be called, otherwise
 * the result of the lock will be communicated via the callback.
 */
int ocfs2_inode_lock_full(struct inode *inode,
			 struct buffer_head **ret_bh,
			 int ex,
			 int arg_flags)
{
	int status, level, dlm_flags, acquired;
	struct ocfs2_lock_res *lockres = NULL;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct buffer_head *local_bh = NULL;

	BUG_ON(!inode);

	mlog_entry_void();

	mlog(0, "inode %llu, take %s META lock\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno,
	     ex ? "EXMODE" : "PRMODE");

	status = 0;
	acquired = 0;
	/* We'll allow faking a readonly metadata lock for
	 * rodevices. */
	if (ocfs2_is_hard_readonly(osb)) {
		if (ex)
			status = -EROFS;
		goto bail;
	}

	if (ocfs2_mount_local(osb))
		goto local;

	if (!(arg_flags & OCFS2_META_LOCK_RECOVERY))
		wait_event(osb->recovery_event,
			   ocfs2_node_map_is_empty(osb, &osb->recovery_map));

	lockres = &OCFS2_I(inode)->ip_inode_lockres;
	level = ex ? LKM_EXMODE : LKM_PRMODE;
	dlm_flags = 0;
	if (arg_flags & OCFS2_META_LOCK_NOQUEUE)
		dlm_flags |= LKM_NOQUEUE;

	status = ocfs2_cluster_lock(osb, lockres, level, dlm_flags, arg_flags);
	if (status < 0) {
		if (status != -EAGAIN && status != -EIOCBRETRY)
			mlog_errno(status);
		goto bail;
	}

	/* Notify the error cleanup path to drop the cluster lock. */
	acquired = 1;

	/* We wait twice because a node may have died while we were in
	 * the lower dlm layers. The second time though, we've
	 * committed to owning this lock so we don't allow signals to
	 * abort the operation. */
	if (!(arg_flags & OCFS2_META_LOCK_RECOVERY))
		wait_event(osb->recovery_event,
			   ocfs2_node_map_is_empty(osb, &osb->recovery_map));

local:
	/*
	 * We only see this flag if we're being called from
	 * ocfs2_read_locked_inode(). It means we're locking an inode
	 * which hasn't been populated yet, so clear the refresh flag
	 * and let the caller handle it.
	 */
	if (inode->i_state & I_NEW) {
		status = 0;
		if (lockres)
			ocfs2_complete_lock_res_refresh(lockres, 0);
		goto bail;
	}

	/* This is fun. The caller may want a bh back, or it may
	 * not. ocfs2_inode_lock_update definitely wants one in, but
	 * may or may not read one, depending on what's in the
	 * LVB. The result of all of this is that we've *only* gone to
	 * disk if we have to, so the complexity is worthwhile. */
	status = ocfs2_inode_lock_update(inode, &local_bh);
	if (status < 0) {
		if (status != -ENOENT)
			mlog_errno(status);
		goto bail;
	}

	if (ret_bh) {
		status = ocfs2_assign_bh(inode, ret_bh, local_bh);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

bail:
	if (status < 0) {
		if (ret_bh && (*ret_bh)) {
			brelse(*ret_bh);
			*ret_bh = NULL;
		}
		if (acquired)
			ocfs2_inode_unlock(inode, ex);
	}

	if (local_bh)
		brelse(local_bh);

	mlog_exit(status);
	return status;
}

/*
 * This is working around a lock inversion between tasks acquiring DLM
 * locks while holding a page lock and the downconvert thread which
 * blocks dlm lock acquiry while acquiring page locks.
 *
 * ** These _with_page variantes are only intended to be called from aop
 * methods that hold page locks and return a very specific *positive* error
 * code that aop methods pass up to the VFS -- test for errors with != 0. **
 *
 * The DLM is called such that it returns -EAGAIN if it would have
 * blocked waiting for the downconvert thread.  In that case we unlock
 * our page so the downconvert thread can make progress.  Once we've
 * done this we have to return AOP_TRUNCATED_PAGE so the aop method
 * that called us can bubble that back up into the VFS who will then
 * immediately retry the aop call.
 *
 * We do a blocking lock and immediate unlock before returning, though, so that
 * the lock has a great chance of being cached on this node by the time the VFS
 * calls back to retry the aop.    This has a potential to livelock as nodes
 * ping locks back and forth, but that's a risk we're willing to take to avoid
 * the lock inversion simply.
 */
int ocfs2_inode_lock_with_page(struct inode *inode,
			      struct buffer_head **ret_bh,
			      int ex,
			      struct page *page)
{
	int ret;

	ret = ocfs2_inode_lock_full(inode, ret_bh, ex, OCFS2_LOCK_NONBLOCK);
	if (ret == -EAGAIN) {
		unlock_page(page);
		if (ocfs2_inode_lock(inode, ret_bh, ex) == 0)
			ocfs2_inode_unlock(inode, ex);
		ret = AOP_TRUNCATED_PAGE;
	}

	return ret;
}

int ocfs2_inode_lock_atime(struct inode *inode,
			  struct vfsmount *vfsmnt,
			  int *level)
{
	int ret;

	mlog_entry_void();
	ret = ocfs2_inode_lock(inode, NULL, 0);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}

	/*
	 * If we should update atime, we will get EX lock,
	 * otherwise we just get PR lock.
	 */
	if (ocfs2_should_update_atime(inode, vfsmnt)) {
		struct buffer_head *bh = NULL;

		ocfs2_inode_unlock(inode, 0);
		ret = ocfs2_inode_lock(inode, &bh, 1);
		if (ret < 0) {
			mlog_errno(ret);
			return ret;
		}
		*level = 1;
		if (ocfs2_should_update_atime(inode, vfsmnt))
			ocfs2_update_inode_atime(inode, bh);
		if (bh)
			brelse(bh);
	} else
		*level = 0;

	mlog_exit(ret);
	return ret;
}

void ocfs2_inode_unlock(struct inode *inode,
		       int ex)
{
	int level = ex ? LKM_EXMODE : LKM_PRMODE;
	struct ocfs2_lock_res *lockres = &OCFS2_I(inode)->ip_inode_lockres;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	mlog_entry_void();

	mlog(0, "inode %llu drop %s META lock\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno,
	     ex ? "EXMODE" : "PRMODE");

	if (!ocfs2_is_hard_readonly(OCFS2_SB(inode->i_sb)) &&
	    !ocfs2_mount_local(osb))
		ocfs2_cluster_unlock(OCFS2_SB(inode->i_sb), lockres, level);

	mlog_exit_void();
}

int ocfs2_super_lock(struct ocfs2_super *osb,
		     int ex)
{
	int status = 0;
	int level = ex ? LKM_EXMODE : LKM_PRMODE;
	struct ocfs2_lock_res *lockres = &osb->osb_super_lockres;
	struct buffer_head *bh;
	struct ocfs2_slot_info *si = osb->slot_info;

	mlog_entry_void();

	if (ocfs2_is_hard_readonly(osb))
		return -EROFS;

	if (ocfs2_mount_local(osb))
		goto bail;

	status = ocfs2_cluster_lock(osb, lockres, level, 0, 0);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	/* The super block lock path is really in the best position to
	 * know when resources covered by the lock need to be
	 * refreshed, so we do it here. Of course, making sense of
	 * everything is up to the caller :) */
	status = ocfs2_should_refresh_lock_res(lockres);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	if (status) {
		bh = si->si_bh;
		status = ocfs2_read_block(osb, bh->b_blocknr, &bh, 0,
					  si->si_inode);
		if (status == 0)
			ocfs2_update_slot_info(si);

		ocfs2_complete_lock_res_refresh(lockres, status);

		if (status < 0)
			mlog_errno(status);
	}
bail:
	mlog_exit(status);
	return status;
}

void ocfs2_super_unlock(struct ocfs2_super *osb,
			int ex)
{
	int level = ex ? LKM_EXMODE : LKM_PRMODE;
	struct ocfs2_lock_res *lockres = &osb->osb_super_lockres;

	if (!ocfs2_mount_local(osb))
		ocfs2_cluster_unlock(osb, lockres, level);
}

int ocfs2_rename_lock(struct ocfs2_super *osb)
{
	int status;
	struct ocfs2_lock_res *lockres = &osb->osb_rename_lockres;

	if (ocfs2_is_hard_readonly(osb))
		return -EROFS;

	if (ocfs2_mount_local(osb))
		return 0;

	status = ocfs2_cluster_lock(osb, lockres, LKM_EXMODE, 0, 0);
	if (status < 0)
		mlog_errno(status);

	return status;
}

void ocfs2_rename_unlock(struct ocfs2_super *osb)
{
	struct ocfs2_lock_res *lockres = &osb->osb_rename_lockres;

	if (!ocfs2_mount_local(osb))
		ocfs2_cluster_unlock(osb, lockres, LKM_EXMODE);
}

int ocfs2_dentry_lock(struct dentry *dentry, int ex)
{
	int ret;
	int level = ex ? LKM_EXMODE : LKM_PRMODE;
	struct ocfs2_dentry_lock *dl = dentry->d_fsdata;
	struct ocfs2_super *osb = OCFS2_SB(dentry->d_sb);

	BUG_ON(!dl);

	if (ocfs2_is_hard_readonly(osb))
		return -EROFS;

	if (ocfs2_mount_local(osb))
		return 0;

	ret = ocfs2_cluster_lock(osb, &dl->dl_lockres, level, 0, 0);
	if (ret < 0)
		mlog_errno(ret);

	return ret;
}

void ocfs2_dentry_unlock(struct dentry *dentry, int ex)
{
	int level = ex ? LKM_EXMODE : LKM_PRMODE;
	struct ocfs2_dentry_lock *dl = dentry->d_fsdata;
	struct ocfs2_super *osb = OCFS2_SB(dentry->d_sb);

	if (!ocfs2_mount_local(osb))
		ocfs2_cluster_unlock(osb, &dl->dl_lockres, level);
}

/* Reference counting of the dlm debug structure. We want this because
 * open references on the debug inodes can live on after a mount, so
 * we can't rely on the ocfs2_super to always exist. */
static void ocfs2_dlm_debug_free(struct kref *kref)
{
	struct ocfs2_dlm_debug *dlm_debug;

	dlm_debug = container_of(kref, struct ocfs2_dlm_debug, d_refcnt);

	kfree(dlm_debug);
}

void ocfs2_put_dlm_debug(struct ocfs2_dlm_debug *dlm_debug)
{
	if (dlm_debug)
		kref_put(&dlm_debug->d_refcnt, ocfs2_dlm_debug_free);
}

static void ocfs2_get_dlm_debug(struct ocfs2_dlm_debug *debug)
{
	kref_get(&debug->d_refcnt);
}

struct ocfs2_dlm_debug *ocfs2_new_dlm_debug(void)
{
	struct ocfs2_dlm_debug *dlm_debug;

	dlm_debug = kmalloc(sizeof(struct ocfs2_dlm_debug), GFP_KERNEL);
	if (!dlm_debug) {
		mlog_errno(-ENOMEM);
		goto out;
	}

	kref_init(&dlm_debug->d_refcnt);
	INIT_LIST_HEAD(&dlm_debug->d_lockres_tracking);
	dlm_debug->d_locking_state = NULL;
out:
	return dlm_debug;
}

/* Access to this is arbitrated for us via seq_file->sem. */
struct ocfs2_dlm_seq_priv {
	struct ocfs2_dlm_debug *p_dlm_debug;
	struct ocfs2_lock_res p_iter_res;
	struct ocfs2_lock_res p_tmp_res;
};

static struct ocfs2_lock_res *ocfs2_dlm_next_res(struct ocfs2_lock_res *start,
						 struct ocfs2_dlm_seq_priv *priv)
{
	struct ocfs2_lock_res *iter, *ret = NULL;
	struct ocfs2_dlm_debug *dlm_debug = priv->p_dlm_debug;

	assert_spin_locked(&ocfs2_dlm_tracking_lock);

	list_for_each_entry(iter, &start->l_debug_list, l_debug_list) {
		/* discover the head of the list */
		if (&iter->l_debug_list == &dlm_debug->d_lockres_tracking) {
			mlog(0, "End of list found, %p\n", ret);
			break;
		}

		/* We track our "dummy" iteration lockres' by a NULL
		 * l_ops field. */
		if (iter->l_ops != NULL) {
			ret = iter;
			break;
		}
	}

	return ret;
}

static void *ocfs2_dlm_seq_start(struct seq_file *m, loff_t *pos)
{
	struct ocfs2_dlm_seq_priv *priv = m->private;
	struct ocfs2_lock_res *iter;

	spin_lock(&ocfs2_dlm_tracking_lock);
	iter = ocfs2_dlm_next_res(&priv->p_iter_res, priv);
	if (iter) {
		/* Since lockres' have the lifetime of their container
		 * (which can be inodes, ocfs2_supers, etc) we want to
		 * copy this out to a temporary lockres while still
		 * under the spinlock. Obviously after this we can't
		 * trust any pointers on the copy returned, but that's
		 * ok as the information we want isn't typically held
		 * in them. */
		priv->p_tmp_res = *iter;
		iter = &priv->p_tmp_res;
	}
	spin_unlock(&ocfs2_dlm_tracking_lock);

	return iter;
}

static void ocfs2_dlm_seq_stop(struct seq_file *m, void *v)
{
}

static void *ocfs2_dlm_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct ocfs2_dlm_seq_priv *priv = m->private;
	struct ocfs2_lock_res *iter = v;
	struct ocfs2_lock_res *dummy = &priv->p_iter_res;

	spin_lock(&ocfs2_dlm_tracking_lock);
	iter = ocfs2_dlm_next_res(iter, priv);
	list_del_init(&dummy->l_debug_list);
	if (iter) {
		list_add(&dummy->l_debug_list, &iter->l_debug_list);
		priv->p_tmp_res = *iter;
		iter = &priv->p_tmp_res;
	}
	spin_unlock(&ocfs2_dlm_tracking_lock);

	return iter;
}

/* So that debugfs.ocfs2 can determine which format is being used */
#define OCFS2_DLM_DEBUG_STR_VERSION 1
static int ocfs2_dlm_seq_show(struct seq_file *m, void *v)
{
	int i;
	char *lvb;
	struct ocfs2_lock_res *lockres = v;

	if (!lockres)
		return -EINVAL;

	seq_printf(m, "0x%x\t", OCFS2_DLM_DEBUG_STR_VERSION);

	if (lockres->l_type == OCFS2_LOCK_TYPE_DENTRY)
		seq_printf(m, "%.*s%08x\t", OCFS2_DENTRY_LOCK_INO_START - 1,
			   lockres->l_name,
			   (unsigned int)ocfs2_get_dentry_lock_ino(lockres));
	else
		seq_printf(m, "%.*s\t", OCFS2_LOCK_ID_MAX_LEN, lockres->l_name);

	seq_printf(m, "%d\t"
		   "0x%lx\t"
		   "0x%x\t"
		   "0x%x\t"
		   "%u\t"
		   "%u\t"
		   "%d\t"
		   "%d\t",
		   lockres->l_level,
		   lockres->l_flags,
		   lockres->l_action,
		   lockres->l_unlock_action,
		   lockres->l_ro_holders,
		   lockres->l_ex_holders,
		   lockres->l_requested,
		   lockres->l_blocking);

	/* Dump the raw LVB */
	lvb = lockres->l_lksb.lvb;
	for(i = 0; i < DLM_LVB_LEN; i++)
		seq_printf(m, "0x%x\t", lvb[i]);

	/* End the line */
	seq_printf(m, "\n");
	return 0;
}

static struct seq_operations ocfs2_dlm_seq_ops = {
	.start =	ocfs2_dlm_seq_start,
	.stop =		ocfs2_dlm_seq_stop,
	.next =		ocfs2_dlm_seq_next,
	.show =		ocfs2_dlm_seq_show,
};

static int ocfs2_dlm_debug_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = (struct seq_file *) file->private_data;
	struct ocfs2_dlm_seq_priv *priv = seq->private;
	struct ocfs2_lock_res *res = &priv->p_iter_res;

	ocfs2_remove_lockres_tracking(res);
	ocfs2_put_dlm_debug(priv->p_dlm_debug);
	return seq_release_private(inode, file);
}

static int ocfs2_dlm_debug_open(struct inode *inode, struct file *file)
{
	int ret;
	struct ocfs2_dlm_seq_priv *priv;
	struct seq_file *seq;
	struct ocfs2_super *osb;

	priv = kzalloc(sizeof(struct ocfs2_dlm_seq_priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}
	osb = inode->i_private;
	ocfs2_get_dlm_debug(osb->osb_dlm_debug);
	priv->p_dlm_debug = osb->osb_dlm_debug;
	INIT_LIST_HEAD(&priv->p_iter_res.l_debug_list);

	ret = seq_open(file, &ocfs2_dlm_seq_ops);
	if (ret) {
		kfree(priv);
		mlog_errno(ret);
		goto out;
	}

	seq = (struct seq_file *) file->private_data;
	seq->private = priv;

	ocfs2_add_lockres_tracking(&priv->p_iter_res,
				   priv->p_dlm_debug);

out:
	return ret;
}

static const struct file_operations ocfs2_dlm_debug_fops = {
	.open =		ocfs2_dlm_debug_open,
	.release =	ocfs2_dlm_debug_release,
	.read =		seq_read,
	.llseek =	seq_lseek,
};

static int ocfs2_dlm_init_debug(struct ocfs2_super *osb)
{
	int ret = 0;
	struct ocfs2_dlm_debug *dlm_debug = osb->osb_dlm_debug;

	dlm_debug->d_locking_state = debugfs_create_file("locking_state",
							 S_IFREG|S_IRUSR,
							 osb->osb_debug_root,
							 osb,
							 &ocfs2_dlm_debug_fops);
	if (!dlm_debug->d_locking_state) {
		ret = -EINVAL;
		mlog(ML_ERROR,
		     "Unable to create locking state debugfs file.\n");
		goto out;
	}

	ocfs2_get_dlm_debug(dlm_debug);
out:
	return ret;
}

static void ocfs2_dlm_shutdown_debug(struct ocfs2_super *osb)
{
	struct ocfs2_dlm_debug *dlm_debug = osb->osb_dlm_debug;

	if (dlm_debug) {
		debugfs_remove(dlm_debug->d_locking_state);
		ocfs2_put_dlm_debug(dlm_debug);
	}
}

int ocfs2_dlm_init(struct ocfs2_super *osb)
{
	int status = 0;
	u32 dlm_key;
	struct dlm_ctxt *dlm = NULL;

	mlog_entry_void();

	if (ocfs2_mount_local(osb))
		goto local;

	status = ocfs2_dlm_init_debug(osb);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	/* launch downconvert thread */
	osb->dc_task = kthread_run(ocfs2_downconvert_thread, osb, "ocfs2dc");
	if (IS_ERR(osb->dc_task)) {
		status = PTR_ERR(osb->dc_task);
		osb->dc_task = NULL;
		mlog_errno(status);
		goto bail;
	}

	/* used by the dlm code to make message headers unique, each
	 * node in this domain must agree on this. */
	dlm_key = crc32_le(0, osb->uuid_str, strlen(osb->uuid_str));

	/* for now, uuid == domain */
	dlm = dlm_register_domain(osb->uuid_str, dlm_key,
				  &osb->osb_locking_proto);
	if (IS_ERR(dlm)) {
		status = PTR_ERR(dlm);
		mlog_errno(status);
		goto bail;
	}

	dlm_register_eviction_cb(dlm, &osb->osb_eviction_cb);

local:
	ocfs2_super_lock_res_init(&osb->osb_super_lockres, osb);
	ocfs2_rename_lock_res_init(&osb->osb_rename_lockres, osb);

	osb->dlm = dlm;

	status = 0;
bail:
	if (status < 0) {
		ocfs2_dlm_shutdown_debug(osb);
		if (osb->dc_task)
			kthread_stop(osb->dc_task);
	}

	mlog_exit(status);
	return status;
}

void ocfs2_dlm_shutdown(struct ocfs2_super *osb)
{
	mlog_entry_void();

	dlm_unregister_eviction_cb(&osb->osb_eviction_cb);

	ocfs2_drop_osb_locks(osb);

	if (osb->dc_task) {
		kthread_stop(osb->dc_task);
		osb->dc_task = NULL;
	}

	ocfs2_lock_res_free(&osb->osb_super_lockres);
	ocfs2_lock_res_free(&osb->osb_rename_lockres);

	dlm_unregister_domain(osb->dlm);
	osb->dlm = NULL;

	ocfs2_dlm_shutdown_debug(osb);

	mlog_exit_void();
}

static void ocfs2_unlock_ast(void *opaque, enum dlm_status status)
{
	struct ocfs2_lock_res *lockres = opaque;
	unsigned long flags;

	mlog_entry_void();

	mlog(0, "UNLOCK AST called on lock %s, action = %d\n", lockres->l_name,
	     lockres->l_unlock_action);

	spin_lock_irqsave(&lockres->l_lock, flags);
	/* We tried to cancel a convert request, but it was already
	 * granted. All we want to do here is clear our unlock
	 * state. The wake_up call done at the bottom is redundant
	 * (ocfs2_prepare_cancel_convert doesn't sleep on this) but doesn't
	 * hurt anything anyway */
	if (status == DLM_CANCELGRANT &&
	    lockres->l_unlock_action == OCFS2_UNLOCK_CANCEL_CONVERT) {
		mlog(0, "Got cancelgrant for %s\n", lockres->l_name);

		/* We don't clear the busy flag in this case as it
		 * should have been cleared by the ast which the dlm
		 * has called. */
		goto complete_unlock;
	}

	if (status != DLM_NORMAL) {
		mlog(ML_ERROR, "Dlm passes status %d for lock %s, "
		     "unlock_action %d\n", status, lockres->l_name,
		     lockres->l_unlock_action);
		spin_unlock_irqrestore(&lockres->l_lock, flags);
		return;
	}

	switch(lockres->l_unlock_action) {
	case OCFS2_UNLOCK_CANCEL_CONVERT:
		mlog(0, "Cancel convert success for %s\n", lockres->l_name);
		lockres->l_action = OCFS2_AST_INVALID;
		break;
	case OCFS2_UNLOCK_DROP_LOCK:
		lockres->l_level = LKM_IVMODE;
		break;
	default:
		BUG();
	}

	lockres_clear_flags(lockres, OCFS2_LOCK_BUSY);
complete_unlock:
	lockres->l_unlock_action = OCFS2_UNLOCK_INVALID;
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	wake_up(&lockres->l_event);

	mlog_exit_void();
}

static int ocfs2_drop_lock(struct ocfs2_super *osb,
			   struct ocfs2_lock_res *lockres)
{
	enum dlm_status status;
	unsigned long flags;
	int lkm_flags = 0;

	/* We didn't get anywhere near actually using this lockres. */
	if (!(lockres->l_flags & OCFS2_LOCK_INITIALIZED))
		goto out;

	if (lockres->l_ops->flags & LOCK_TYPE_USES_LVB)
		lkm_flags |= LKM_VALBLK;

	spin_lock_irqsave(&lockres->l_lock, flags);

	mlog_bug_on_msg(!(lockres->l_flags & OCFS2_LOCK_FREEING),
			"lockres %s, flags 0x%lx\n",
			lockres->l_name, lockres->l_flags);

	while (lockres->l_flags & OCFS2_LOCK_BUSY) {
		mlog(0, "waiting on busy lock \"%s\": flags = %lx, action = "
		     "%u, unlock_action = %u\n",
		     lockres->l_name, lockres->l_flags, lockres->l_action,
		     lockres->l_unlock_action);

		spin_unlock_irqrestore(&lockres->l_lock, flags);

		/* XXX: Today we just wait on any busy
		 * locks... Perhaps we need to cancel converts in the
		 * future? */
		ocfs2_wait_on_busy_lock(lockres);

		spin_lock_irqsave(&lockres->l_lock, flags);
	}

	if (lockres->l_ops->flags & LOCK_TYPE_USES_LVB) {
		if (lockres->l_flags & OCFS2_LOCK_ATTACHED &&
		    lockres->l_level == LKM_EXMODE &&
		    !(lockres->l_flags & OCFS2_LOCK_NEEDS_REFRESH))
			lockres->l_ops->set_lvb(lockres);
	}

	if (lockres->l_flags & OCFS2_LOCK_BUSY)
		mlog(ML_ERROR, "destroying busy lock: \"%s\"\n",
		     lockres->l_name);
	if (lockres->l_flags & OCFS2_LOCK_BLOCKED)
		mlog(0, "destroying blocked lock: \"%s\"\n", lockres->l_name);

	if (!(lockres->l_flags & OCFS2_LOCK_ATTACHED)) {
		spin_unlock_irqrestore(&lockres->l_lock, flags);
		goto out;
	}

	lockres_clear_flags(lockres, OCFS2_LOCK_ATTACHED);

	/* make sure we never get here while waiting for an ast to
	 * fire. */
	BUG_ON(lockres->l_action != OCFS2_AST_INVALID);

	/* is this necessary? */
	lockres_or_flags(lockres, OCFS2_LOCK_BUSY);
	lockres->l_unlock_action = OCFS2_UNLOCK_DROP_LOCK;
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	mlog(0, "lock %s\n", lockres->l_name);

	status = dlmunlock(osb->dlm, &lockres->l_lksb, lkm_flags,
			   ocfs2_unlock_ast, lockres);
	if (status != DLM_NORMAL) {
		ocfs2_log_dlm_error("dlmunlock", status, lockres);
		mlog(ML_ERROR, "lockres flags: %lu\n", lockres->l_flags);
		dlm_print_one_lock(lockres->l_lksb.lockid);
		BUG();
	}
	mlog(0, "lock %s, successfull return from dlmunlock\n",
	     lockres->l_name);

	ocfs2_wait_on_busy_lock(lockres);
out:
	mlog_exit(0);
	return 0;
}

/* Mark the lockres as being dropped. It will no longer be
 * queued if blocking, but we still may have to wait on it
 * being dequeued from the downconvert thread before we can consider
 * it safe to drop. 
 *
 * You can *not* attempt to call cluster_lock on this lockres anymore. */
void ocfs2_mark_lockres_freeing(struct ocfs2_lock_res *lockres)
{
	int status;
	struct ocfs2_mask_waiter mw;
	unsigned long flags;

	ocfs2_init_mask_waiter(&mw);

	spin_lock_irqsave(&lockres->l_lock, flags);
	lockres->l_flags |= OCFS2_LOCK_FREEING;
	while (lockres->l_flags & OCFS2_LOCK_QUEUED) {
		lockres_add_mask_waiter(lockres, &mw, OCFS2_LOCK_QUEUED, 0);
		spin_unlock_irqrestore(&lockres->l_lock, flags);

		mlog(0, "Waiting on lockres %s\n", lockres->l_name);

		status = ocfs2_wait_for_mask(&mw);
		if (status)
			mlog_errno(status);

		spin_lock_irqsave(&lockres->l_lock, flags);
	}
	spin_unlock_irqrestore(&lockres->l_lock, flags);
}

void ocfs2_simple_drop_lockres(struct ocfs2_super *osb,
			       struct ocfs2_lock_res *lockres)
{
	int ret;

	ocfs2_mark_lockres_freeing(lockres);
	ret = ocfs2_drop_lock(osb, lockres);
	if (ret)
		mlog_errno(ret);
}

static void ocfs2_drop_osb_locks(struct ocfs2_super *osb)
{
	ocfs2_simple_drop_lockres(osb, &osb->osb_super_lockres);
	ocfs2_simple_drop_lockres(osb, &osb->osb_rename_lockres);
}

int ocfs2_drop_inode_locks(struct inode *inode)
{
	int status, err;

	mlog_entry_void();

	/* No need to call ocfs2_mark_lockres_freeing here -
	 * ocfs2_clear_inode has done it for us. */

	err = ocfs2_drop_lock(OCFS2_SB(inode->i_sb),
			      &OCFS2_I(inode)->ip_open_lockres);
	if (err < 0)
		mlog_errno(err);

	status = err;

	err = ocfs2_drop_lock(OCFS2_SB(inode->i_sb),
			      &OCFS2_I(inode)->ip_inode_lockres);
	if (err < 0)
		mlog_errno(err);
	if (err < 0 && !status)
		status = err;

	err = ocfs2_drop_lock(OCFS2_SB(inode->i_sb),
			      &OCFS2_I(inode)->ip_rw_lockres);
	if (err < 0)
		mlog_errno(err);
	if (err < 0 && !status)
		status = err;

	mlog_exit(status);
	return status;
}

static void ocfs2_prepare_downconvert(struct ocfs2_lock_res *lockres,
				      int new_level)
{
	assert_spin_locked(&lockres->l_lock);

	BUG_ON(lockres->l_blocking <= LKM_NLMODE);

	if (lockres->l_level <= new_level) {
		mlog(ML_ERROR, "lockres->l_level (%u) <= new_level (%u)\n",
		     lockres->l_level, new_level);
		BUG();
	}

	mlog(0, "lock %s, new_level = %d, l_blocking = %d\n",
	     lockres->l_name, new_level, lockres->l_blocking);

	lockres->l_action = OCFS2_AST_DOWNCONVERT;
	lockres->l_requested = new_level;
	lockres_or_flags(lockres, OCFS2_LOCK_BUSY);
}

static int ocfs2_downconvert_lock(struct ocfs2_super *osb,
				  struct ocfs2_lock_res *lockres,
				  int new_level,
				  int lvb)
{
	int ret, dlm_flags = LKM_CONVERT;
	enum dlm_status status;

	mlog_entry_void();

	if (lvb)
		dlm_flags |= LKM_VALBLK;

	status = dlmlock(osb->dlm,
			 new_level,
			 &lockres->l_lksb,
			 dlm_flags,
			 lockres->l_name,
			 OCFS2_LOCK_ID_MAX_LEN - 1,
			 ocfs2_locking_ast,
			 lockres,
			 ocfs2_blocking_ast);
	if (status != DLM_NORMAL) {
		ocfs2_log_dlm_error("dlmlock", status, lockres);
		ret = -EINVAL;
		ocfs2_recover_from_dlm_error(lockres, 1);
		goto bail;
	}

	ret = 0;
bail:
	mlog_exit(ret);
	return ret;
}

/* returns 1 when the caller should unlock and call dlmunlock */
static int ocfs2_prepare_cancel_convert(struct ocfs2_super *osb,
				        struct ocfs2_lock_res *lockres)
{
	assert_spin_locked(&lockres->l_lock);

	mlog_entry_void();
	mlog(0, "lock %s\n", lockres->l_name);

	if (lockres->l_unlock_action == OCFS2_UNLOCK_CANCEL_CONVERT) {
		/* If we're already trying to cancel a lock conversion
		 * then just drop the spinlock and allow the caller to
		 * requeue this lock. */

		mlog(0, "Lockres %s, skip convert\n", lockres->l_name);
		return 0;
	}

	/* were we in a convert when we got the bast fire? */
	BUG_ON(lockres->l_action != OCFS2_AST_CONVERT &&
	       lockres->l_action != OCFS2_AST_DOWNCONVERT);
	/* set things up for the unlockast to know to just
	 * clear out the ast_action and unset busy, etc. */
	lockres->l_unlock_action = OCFS2_UNLOCK_CANCEL_CONVERT;

	mlog_bug_on_msg(!(lockres->l_flags & OCFS2_LOCK_BUSY),
			"lock %s, invalid flags: 0x%lx\n",
			lockres->l_name, lockres->l_flags);

	return 1;
}

static int ocfs2_cancel_convert(struct ocfs2_super *osb,
				struct ocfs2_lock_res *lockres)
{
	int ret;
	enum dlm_status status;

	mlog_entry_void();
	mlog(0, "lock %s\n", lockres->l_name);

	ret = 0;
	status = dlmunlock(osb->dlm,
			   &lockres->l_lksb,
			   LKM_CANCEL,
			   ocfs2_unlock_ast,
			   lockres);
	if (status != DLM_NORMAL) {
		ocfs2_log_dlm_error("dlmunlock", status, lockres);
		ret = -EINVAL;
		ocfs2_recover_from_dlm_error(lockres, 0);
	}

	mlog(0, "lock %s return from dlmunlock\n", lockres->l_name);

	mlog_exit(ret);
	return ret;
}

static int ocfs2_unblock_lock(struct ocfs2_super *osb,
			      struct ocfs2_lock_res *lockres,
			      struct ocfs2_unblock_ctl *ctl)
{
	unsigned long flags;
	int blocking;
	int new_level;
	int ret = 0;
	int set_lvb = 0;

	mlog_entry_void();

	spin_lock_irqsave(&lockres->l_lock, flags);

	BUG_ON(!(lockres->l_flags & OCFS2_LOCK_BLOCKED));

recheck:
	if (lockres->l_flags & OCFS2_LOCK_BUSY) {
		ctl->requeue = 1;
		ret = ocfs2_prepare_cancel_convert(osb, lockres);
		spin_unlock_irqrestore(&lockres->l_lock, flags);
		if (ret) {
			ret = ocfs2_cancel_convert(osb, lockres);
			if (ret < 0)
				mlog_errno(ret);
		}
		goto leave;
	}

	/* if we're blocking an exclusive and we have *any* holders,
	 * then requeue. */
	if ((lockres->l_blocking == LKM_EXMODE)
	    && (lockres->l_ex_holders || lockres->l_ro_holders))
		goto leave_requeue;

	/* If it's a PR we're blocking, then only
	 * requeue if we've got any EX holders */
	if (lockres->l_blocking == LKM_PRMODE &&
	    lockres->l_ex_holders)
		goto leave_requeue;

	/*
	 * Can we get a lock in this state if the holder counts are
	 * zero? The meta data unblock code used to check this.
	 */
	if ((lockres->l_ops->flags & LOCK_TYPE_REQUIRES_REFRESH)
	    && (lockres->l_flags & OCFS2_LOCK_REFRESHING))
		goto leave_requeue;

	new_level = ocfs2_highest_compat_lock_level(lockres->l_blocking);

	if (lockres->l_ops->check_downconvert
	    && !lockres->l_ops->check_downconvert(lockres, new_level))
		goto leave_requeue;

	/* If we get here, then we know that there are no more
	 * incompatible holders (and anyone asking for an incompatible
	 * lock is blocked). We can now downconvert the lock */
	if (!lockres->l_ops->downconvert_worker)
		goto downconvert;

	/* Some lockres types want to do a bit of work before
	 * downconverting a lock. Allow that here. The worker function
	 * may sleep, so we save off a copy of what we're blocking as
	 * it may change while we're not holding the spin lock. */
	blocking = lockres->l_blocking;
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	ctl->unblock_action = lockres->l_ops->downconvert_worker(lockres, blocking);

	if (ctl->unblock_action == UNBLOCK_STOP_POST)
		goto leave;

	spin_lock_irqsave(&lockres->l_lock, flags);
	if (blocking != lockres->l_blocking) {
		/* If this changed underneath us, then we can't drop
		 * it just yet. */
		goto recheck;
	}

downconvert:
	ctl->requeue = 0;

	if (lockres->l_ops->flags & LOCK_TYPE_USES_LVB) {
		if (lockres->l_level == LKM_EXMODE)
			set_lvb = 1;

		/*
		 * We only set the lvb if the lock has been fully
		 * refreshed - otherwise we risk setting stale
		 * data. Otherwise, there's no need to actually clear
		 * out the lvb here as it's value is still valid.
		 */
		if (set_lvb && !(lockres->l_flags & OCFS2_LOCK_NEEDS_REFRESH))
			lockres->l_ops->set_lvb(lockres);
	}

	ocfs2_prepare_downconvert(lockres, new_level);
	spin_unlock_irqrestore(&lockres->l_lock, flags);
	ret = ocfs2_downconvert_lock(osb, lockres, new_level, set_lvb);
leave:
	mlog_exit(ret);
	return ret;

leave_requeue:
	spin_unlock_irqrestore(&lockres->l_lock, flags);
	ctl->requeue = 1;

	mlog_exit(0);
	return 0;
}

static int ocfs2_data_convert_worker(struct ocfs2_lock_res *lockres,
				     int blocking)
{
	struct inode *inode;
	struct address_space *mapping;

       	inode = ocfs2_lock_res_inode(lockres);
	mapping = inode->i_mapping;

	if (!S_ISREG(inode->i_mode))
		goto out;

	/*
	 * We need this before the filemap_fdatawrite() so that it can
	 * transfer the dirty bit from the PTE to the
	 * page. Unfortunately this means that even for EX->PR
	 * downconverts, we'll lose our mappings and have to build
	 * them up again.
	 */
	unmap_mapping_range(mapping, 0, 0, 0);

	if (filemap_fdatawrite(mapping)) {
		mlog(ML_ERROR, "Could not sync inode %llu for downconvert!",
		     (unsigned long long)OCFS2_I(inode)->ip_blkno);
	}
	sync_mapping_buffers(mapping);
	if (blocking == LKM_EXMODE) {
		truncate_inode_pages(mapping, 0);
	} else {
		/* We only need to wait on the I/O if we're not also
		 * truncating pages because truncate_inode_pages waits
		 * for us above. We don't truncate pages if we're
		 * blocking anything < EXMODE because we want to keep
		 * them around in that case. */
		filemap_fdatawait(mapping);
	}

out:
	return UNBLOCK_CONTINUE;
}

static int ocfs2_check_meta_downconvert(struct ocfs2_lock_res *lockres,
					int new_level)
{
	struct inode *inode = ocfs2_lock_res_inode(lockres);
	int checkpointed = ocfs2_inode_fully_checkpointed(inode);

	BUG_ON(new_level != LKM_NLMODE && new_level != LKM_PRMODE);
	BUG_ON(lockres->l_level != LKM_EXMODE && !checkpointed);

	if (checkpointed)
		return 1;

	ocfs2_start_checkpoint(OCFS2_SB(inode->i_sb));
	return 0;
}

static void ocfs2_set_meta_lvb(struct ocfs2_lock_res *lockres)
{
	struct inode *inode = ocfs2_lock_res_inode(lockres);

	__ocfs2_stuff_meta_lvb(inode);
}

/*
 * Does the final reference drop on our dentry lock. Right now this
 * happens in the downconvert thread, but we could choose to simplify the
 * dlmglue API and push these off to the ocfs2_wq in the future.
 */
static void ocfs2_dentry_post_unlock(struct ocfs2_super *osb,
				     struct ocfs2_lock_res *lockres)
{
	struct ocfs2_dentry_lock *dl = ocfs2_lock_res_dl(lockres);
	ocfs2_dentry_lock_put(osb, dl);
}

/*
 * d_delete() matching dentries before the lock downconvert.
 *
 * At this point, any process waiting to destroy the
 * dentry_lock due to last ref count is stopped by the
 * OCFS2_LOCK_QUEUED flag.
 *
 * We have two potential problems
 *
 * 1) If we do the last reference drop on our dentry_lock (via dput)
 *    we'll wind up in ocfs2_release_dentry_lock(), waiting on
 *    the downconvert to finish. Instead we take an elevated
 *    reference and push the drop until after we've completed our
 *    unblock processing.
 *
 * 2) There might be another process with a final reference,
 *    waiting on us to finish processing. If this is the case, we
 *    detect it and exit out - there's no more dentries anyway.
 */
static int ocfs2_dentry_convert_worker(struct ocfs2_lock_res *lockres,
				       int blocking)
{
	struct ocfs2_dentry_lock *dl = ocfs2_lock_res_dl(lockres);
	struct ocfs2_inode_info *oi = OCFS2_I(dl->dl_inode);
	struct dentry *dentry;
	unsigned long flags;
	int extra_ref = 0;

	/*
	 * This node is blocking another node from getting a read
	 * lock. This happens when we've renamed within a
	 * directory. We've forced the other nodes to d_delete(), but
	 * we never actually dropped our lock because it's still
	 * valid. The downconvert code will retain a PR for this node,
	 * so there's no further work to do.
	 */
	if (blocking == LKM_PRMODE)
		return UNBLOCK_CONTINUE;

	/*
	 * Mark this inode as potentially orphaned. The code in
	 * ocfs2_delete_inode() will figure out whether it actually
	 * needs to be freed or not.
	 */
	spin_lock(&oi->ip_lock);
	oi->ip_flags |= OCFS2_INODE_MAYBE_ORPHANED;
	spin_unlock(&oi->ip_lock);

	/*
	 * Yuck. We need to make sure however that the check of
	 * OCFS2_LOCK_FREEING and the extra reference are atomic with
	 * respect to a reference decrement or the setting of that
	 * flag.
	 */
	spin_lock_irqsave(&lockres->l_lock, flags);
	spin_lock(&dentry_attach_lock);
	if (!(lockres->l_flags & OCFS2_LOCK_FREEING)
	    && dl->dl_count) {
		dl->dl_count++;
		extra_ref = 1;
	}
	spin_unlock(&dentry_attach_lock);
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	mlog(0, "extra_ref = %d\n", extra_ref);

	/*
	 * We have a process waiting on us in ocfs2_dentry_iput(),
	 * which means we can't have any more outstanding
	 * aliases. There's no need to do any more work.
	 */
	if (!extra_ref)
		return UNBLOCK_CONTINUE;

	spin_lock(&dentry_attach_lock);
	while (1) {
		dentry = ocfs2_find_local_alias(dl->dl_inode,
						dl->dl_parent_blkno, 1);
		if (!dentry)
			break;
		spin_unlock(&dentry_attach_lock);

		mlog(0, "d_delete(%.*s);\n", dentry->d_name.len,
		     dentry->d_name.name);

		/*
		 * The following dcache calls may do an
		 * iput(). Normally we don't want that from the
		 * downconverting thread, but in this case it's ok
		 * because the requesting node already has an
		 * exclusive lock on the inode, so it can't be queued
		 * for a downconvert.
		 */
		d_delete(dentry);
		dput(dentry);

		spin_lock(&dentry_attach_lock);
	}
	spin_unlock(&dentry_attach_lock);

	/*
	 * If we are the last holder of this dentry lock, there is no
	 * reason to downconvert so skip straight to the unlock.
	 */
	if (dl->dl_count == 1)
		return UNBLOCK_STOP_POST;

	return UNBLOCK_CONTINUE_POST;
}

static void ocfs2_process_blocked_lock(struct ocfs2_super *osb,
				       struct ocfs2_lock_res *lockres)
{
	int status;
	struct ocfs2_unblock_ctl ctl = {0, 0,};
	unsigned long flags;

	/* Our reference to the lockres in this function can be
	 * considered valid until we remove the OCFS2_LOCK_QUEUED
	 * flag. */

	mlog_entry_void();

	BUG_ON(!lockres);
	BUG_ON(!lockres->l_ops);

	mlog(0, "lockres %s blocked.\n", lockres->l_name);

	/* Detect whether a lock has been marked as going away while
	 * the downconvert thread was processing other things. A lock can
	 * still be marked with OCFS2_LOCK_FREEING after this check,
	 * but short circuiting here will still save us some
	 * performance. */
	spin_lock_irqsave(&lockres->l_lock, flags);
	if (lockres->l_flags & OCFS2_LOCK_FREEING)
		goto unqueue;
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	status = ocfs2_unblock_lock(osb, lockres, &ctl);
	if (status < 0)
		mlog_errno(status);

	spin_lock_irqsave(&lockres->l_lock, flags);
unqueue:
	if (lockres->l_flags & OCFS2_LOCK_FREEING || !ctl.requeue) {
		lockres_clear_flags(lockres, OCFS2_LOCK_QUEUED);
	} else
		ocfs2_schedule_blocked_lock(osb, lockres);

	mlog(0, "lockres %s, requeue = %s.\n", lockres->l_name,
	     ctl.requeue ? "yes" : "no");
	spin_unlock_irqrestore(&lockres->l_lock, flags);

	if (ctl.unblock_action != UNBLOCK_CONTINUE
	    && lockres->l_ops->post_unlock)
		lockres->l_ops->post_unlock(osb, lockres);

	mlog_exit_void();
}

static void ocfs2_schedule_blocked_lock(struct ocfs2_super *osb,
					struct ocfs2_lock_res *lockres)
{
	mlog_entry_void();

	assert_spin_locked(&lockres->l_lock);

	if (lockres->l_flags & OCFS2_LOCK_FREEING) {
		/* Do not schedule a lock for downconvert when it's on
		 * the way to destruction - any nodes wanting access
		 * to the resource will get it soon. */
		mlog(0, "Lockres %s won't be scheduled: flags 0x%lx\n",
		     lockres->l_name, lockres->l_flags);
		return;
	}

	lockres_or_flags(lockres, OCFS2_LOCK_QUEUED);

	spin_lock(&osb->dc_task_lock);
	if (list_empty(&lockres->l_blocked_list)) {
		list_add_tail(&lockres->l_blocked_list,
			      &osb->blocked_lock_list);
		osb->blocked_lock_count++;
	}
	spin_unlock(&osb->dc_task_lock);

	mlog_exit_void();
}

static void ocfs2_downconvert_thread_do_work(struct ocfs2_super *osb)
{
	unsigned long processed;
	struct ocfs2_lock_res *lockres;

	mlog_entry_void();

	spin_lock(&osb->dc_task_lock);
	/* grab this early so we know to try again if a state change and
	 * wake happens part-way through our work  */
	osb->dc_work_sequence = osb->dc_wake_sequence;

	processed = osb->blocked_lock_count;
	while (processed) {
		BUG_ON(list_empty(&osb->blocked_lock_list));

		lockres = list_entry(osb->blocked_lock_list.next,
				     struct ocfs2_lock_res, l_blocked_list);
		list_del_init(&lockres->l_blocked_list);
		osb->blocked_lock_count--;
		spin_unlock(&osb->dc_task_lock);

		BUG_ON(!processed);
		processed--;

		ocfs2_process_blocked_lock(osb, lockres);

		spin_lock(&osb->dc_task_lock);
	}
	spin_unlock(&osb->dc_task_lock);

	mlog_exit_void();
}

static int ocfs2_downconvert_thread_lists_empty(struct ocfs2_super *osb)
{
	int empty = 0;

	spin_lock(&osb->dc_task_lock);
	if (list_empty(&osb->blocked_lock_list))
		empty = 1;

	spin_unlock(&osb->dc_task_lock);
	return empty;
}

static int ocfs2_downconvert_thread_should_wake(struct ocfs2_super *osb)
{
	int should_wake = 0;

	spin_lock(&osb->dc_task_lock);
	if (osb->dc_work_sequence != osb->dc_wake_sequence)
		should_wake = 1;
	spin_unlock(&osb->dc_task_lock);

	return should_wake;
}

static int ocfs2_downconvert_thread(void *arg)
{
	int status = 0;
	struct ocfs2_super *osb = arg;

	/* only quit once we've been asked to stop and there is no more
	 * work available */
	while (!(kthread_should_stop() &&
		ocfs2_downconvert_thread_lists_empty(osb))) {

		wait_event_interruptible(osb->dc_event,
					 ocfs2_downconvert_thread_should_wake(osb) ||
					 kthread_should_stop());

		mlog(0, "downconvert_thread: awoken\n");

		ocfs2_downconvert_thread_do_work(osb);
	}

	osb->dc_task = NULL;
	return status;
}

void ocfs2_wake_downconvert_thread(struct ocfs2_super *osb)
{
	spin_lock(&osb->dc_task_lock);
	/* make sure the voting thread gets a swipe at whatever changes
	 * the caller may have made to the voting state */
	osb->dc_wake_sequence++;
	spin_unlock(&osb->dc_task_lock);
	wake_up(&osb->dc_event);
}
