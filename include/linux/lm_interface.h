/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __LM_INTERFACE_DOT_H__
#define __LM_INTERFACE_DOT_H__


typedef void (*lm_callback_t) (void *ptr, unsigned int type, void *data);

/*
 * lm_mount() flags
 *
 * LM_MFLAG_SPECTATOR
 * GFS is asking to join the filesystem's lockspace, but it doesn't want to
 * modify the filesystem.  The lock module shouldn't assign a journal to the FS
 * mount.  It shouldn't send recovery callbacks to the FS mount.  If the node
 * dies or withdraws, all locks can be wiped immediately.
 */

#define LM_MFLAG_SPECTATOR	0x00000001

/*
 * lm_lockstruct flags
 *
 * LM_LSFLAG_LOCAL
 * The lock_nolock module returns LM_LSFLAG_LOCAL to GFS, indicating that GFS
 * can make single-node optimizations.
 */

#define LM_LSFLAG_LOCAL		0x00000001

/*
 * lm_lockname types
 */

#define LM_TYPE_RESERVED	0x00
#define LM_TYPE_NONDISK		0x01
#define LM_TYPE_INODE		0x02
#define LM_TYPE_RGRP		0x03
#define LM_TYPE_META		0x04
#define LM_TYPE_IOPEN		0x05
#define LM_TYPE_FLOCK		0x06
#define LM_TYPE_PLOCK		0x07
#define LM_TYPE_QUOTA		0x08
#define LM_TYPE_JOURNAL		0x09

/*
 * lm_lock() states
 *
 * SHARED is compatible with SHARED, not with DEFERRED or EX.
 * DEFERRED is compatible with DEFERRED, not with SHARED or EX.
 */

#define LM_ST_UNLOCKED		0
#define LM_ST_EXCLUSIVE		1
#define LM_ST_DEFERRED		2
#define LM_ST_SHARED		3

/*
 * lm_lock() flags
 *
 * LM_FLAG_TRY
 * Don't wait to acquire the lock if it can't be granted immediately.
 *
 * LM_FLAG_TRY_1CB
 * Send one blocking callback if TRY is set and the lock is not granted.
 *
 * LM_FLAG_NOEXP
 * GFS sets this flag on lock requests it makes while doing journal recovery.
 * These special requests should not be blocked due to the recovery like
 * ordinary locks would be.
 *
 * LM_FLAG_ANY
 * A SHARED request may also be granted in DEFERRED, or a DEFERRED request may
 * also be granted in SHARED.  The preferred state is whichever is compatible
 * with other granted locks, or the specified state if no other locks exist.
 *
 * LM_FLAG_PRIORITY
 * Override fairness considerations.  Suppose a lock is held in a shared state
 * and there is a pending request for the deferred state.  A shared lock
 * request with the priority flag would be allowed to bypass the deferred
 * request and directly join the other shared lock.  A shared lock request
 * without the priority flag might be forced to wait until the deferred
 * requested had acquired and released the lock.
 */

#define LM_FLAG_TRY		0x00000001
#define LM_FLAG_TRY_1CB		0x00000002
#define LM_FLAG_NOEXP		0x00000004
#define LM_FLAG_ANY		0x00000008
#define LM_FLAG_PRIORITY	0x00000010

/*
 * lm_lock() and lm_async_cb return flags
 *
 * LM_OUT_ST_MASK
 * Masks the lower two bits of lock state in the returned value.
 *
 * LM_OUT_CACHEABLE
 * The lock hasn't been released so GFS can continue to cache data for it.
 *
 * LM_OUT_CANCELED
 * The lock request was canceled.
 *
 * LM_OUT_ASYNC
 * The result of the request will be returned in an LM_CB_ASYNC callback.
 */

#define LM_OUT_ST_MASK		0x00000003
#define LM_OUT_CACHEABLE	0x00000004
#define LM_OUT_CANCELED		0x00000008
#define LM_OUT_ASYNC		0x00000080
#define LM_OUT_ERROR		0x00000100

/*
 * lm_callback_t types
 *
 * LM_CB_NEED_E LM_CB_NEED_D LM_CB_NEED_S
 * Blocking callback, a remote node is requesting the given lock in
 * EXCLUSIVE, DEFERRED, or SHARED.
 *
 * LM_CB_NEED_RECOVERY
 * The given journal needs to be recovered.
 *
 * LM_CB_DROPLOCKS
 * Reduce the number of cached locks.
 *
 * LM_CB_ASYNC
 * The given lock has been granted.
 */

#define LM_CB_NEED_E		257
#define LM_CB_NEED_D		258
#define LM_CB_NEED_S		259
#define LM_CB_NEED_RECOVERY	260
#define LM_CB_DROPLOCKS		261
#define LM_CB_ASYNC		262

/*
 * lm_recovery_done() messages
 */

#define LM_RD_GAVEUP		308
#define LM_RD_SUCCESS		309


struct lm_lockname {
	u64 ln_number;
	unsigned int ln_type;
};

#define lm_name_equal(name1, name2) \
	(((name1)->ln_number == (name2)->ln_number) && \
	 ((name1)->ln_type == (name2)->ln_type)) \

struct lm_async_cb {
	struct lm_lockname lc_name;
	int lc_ret;
};

struct lm_lockstruct;

struct lm_lockops {
	const char *lm_proto_name;

	/*
	 * Mount/Unmount
	 */

	int (*lm_mount) (char *table_name, char *host_data,
			 lm_callback_t cb, void *cb_data,
			 unsigned int min_lvb_size, int flags,
			 struct lm_lockstruct *lockstruct,
			 struct kobject *fskobj);

	void (*lm_others_may_mount) (void *lockspace);

	void (*lm_unmount) (void *lockspace);

	void (*lm_withdraw) (void *lockspace);

	/*
	 * Lock oriented operations
	 */

	int (*lm_get_lock) (void *lockspace, struct lm_lockname *name, void **lockp);

	void (*lm_put_lock) (void *lock);

	unsigned int (*lm_lock) (void *lock, unsigned int cur_state,
				 unsigned int req_state, unsigned int flags);

	unsigned int (*lm_unlock) (void *lock, unsigned int cur_state);

	void (*lm_cancel) (void *lock);

	int (*lm_hold_lvb) (void *lock, char **lvbp);
	void (*lm_unhold_lvb) (void *lock, char *lvb);

	/*
	 * Posix Lock oriented operations
	 */

	int (*lm_plock_get) (void *lockspace, struct lm_lockname *name,
			     struct file *file, struct file_lock *fl);

	int (*lm_plock) (void *lockspace, struct lm_lockname *name,
			 struct file *file, int cmd, struct file_lock *fl);

	int (*lm_punlock) (void *lockspace, struct lm_lockname *name,
			   struct file *file, struct file_lock *fl);

	/*
	 * Client oriented operations
	 */

	void (*lm_recovery_done) (void *lockspace, unsigned int jid,
				  unsigned int message);

	struct module *lm_owner;
};

/*
 * lm_mount() return values
 *
 * ls_jid - the journal ID this node should use
 * ls_first - this node is the first to mount the file system
 * ls_lvb_size - size in bytes of lock value blocks
 * ls_lockspace - lock module's context for this file system
 * ls_ops - lock module's functions
 * ls_flags - lock module features
 */

struct lm_lockstruct {
	unsigned int ls_jid;
	unsigned int ls_first;
	unsigned int ls_lvb_size;
	void *ls_lockspace;
	const struct lm_lockops *ls_ops;
	int ls_flags;
};

/*
 * Lock module bottom interface.  A lock module makes itself available to GFS
 * with these functions.
 */

int gfs2_register_lockproto(const struct lm_lockops *proto);
void gfs2_unregister_lockproto(const struct lm_lockops *proto);

/*
 * Lock module top interface.  GFS calls these functions when mounting or
 * unmounting a file system.
 */

int gfs2_mount_lockproto(char *proto_name, char *table_name, char *host_data,
			 lm_callback_t cb, void *cb_data,
			 unsigned int min_lvb_size, int flags,
			 struct lm_lockstruct *lockstruct,
			 struct kobject *fskobj);

void gfs2_unmount_lockproto(struct lm_lockstruct *lockstruct);

void gfs2_withdraw_lockproto(struct lm_lockstruct *lockstruct);

#endif /* __LM_INTERFACE_DOT_H__ */

