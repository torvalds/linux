/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2011 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/
#ifndef __DLM_DOT_H__
#define __DLM_DOT_H__

#include <uapi/linux/dlm.h>


struct dlm_slot {
	int nodeid; /* 1 to MAX_INT */
	int slot;   /* 1 to MAX_INT */
};

/*
 * recover_prep: called before the dlm begins lock recovery.
 *   Notfies lockspace user that locks from failed members will be granted.
 * recover_slot: called after recover_prep and before recover_done.
 *   Identifies a failed lockspace member.
 * recover_done: called after the dlm completes lock recovery.
 *   Identifies lockspace members and lockspace generation number.
 */

struct dlm_lockspace_ops {
	void (*recover_prep) (void *ops_arg);
	void (*recover_slot) (void *ops_arg, struct dlm_slot *slot);
	void (*recover_done) (void *ops_arg, struct dlm_slot *slots,
			      int num_slots, int our_slot, uint32_t generation);
};

/*
 * dlm_new_lockspace
 *
 * Create/join a lockspace.
 *
 * name: lockspace name, null terminated, up to DLM_LOCKSPACE_LEN (not
 *   including terminating null).
 *
 * cluster: cluster name, null terminated, up to DLM_LOCKSPACE_LEN (not
 *   including terminating null).  Optional.  When cluster is null, it
 *   is not used.  When set, dlm_new_lockspace() returns -EBADR if cluster
 *   is not equal to the dlm cluster name.
 *
 * flags:
 * DLM_LSFL_NODIR
 *   The dlm should not use a resource directory, but statically assign
 *   resource mastery to nodes based on the name hash that is otherwise
 *   used to select the directory node.  Must be the same on all nodes.
 * DLM_LSFL_TIMEWARN
 *   The dlm should emit netlink messages if locks have been waiting
 *   for a configurable amount of time.  (Unused.)
 * DLM_LSFL_FS
 *   The lockspace user is in the kernel (i.e. filesystem).  Enables
 *   direct bast/cast callbacks.
 * DLM_LSFL_NEWEXCL
 *   dlm_new_lockspace() should return -EEXIST if the lockspace exists.
 *
 * lvblen: length of lvb in bytes.  Must be multiple of 8.
 *   dlm_new_lockspace() returns an error if this does not match
 *   what other nodes are using.
 *
 * ops: callbacks that indicate lockspace recovery points so the
 *   caller can coordinate its recovery and know lockspace members.
 *   This is only used by the initial dlm_new_lockspace() call.
 *   Optional.
 *
 * ops_arg: arg for ops callbacks.
 *
 * ops_result: tells caller if the ops callbacks (if provided) will
 *   be used or not.  0: will be used, -EXXX will not be used.
 *   -EOPNOTSUPP: the dlm does not have recovery_callbacks enabled.
 *
 * lockspace: handle for dlm functions
 */

int dlm_new_lockspace(const char *name, const char *cluster,
		      uint32_t flags, int lvblen,
		      const struct dlm_lockspace_ops *ops, void *ops_arg,
		      int *ops_result, dlm_lockspace_t **lockspace);

/*
 * dlm_release_lockspace
 *
 * Stop a lockspace.
 */

int dlm_release_lockspace(dlm_lockspace_t *lockspace, int force);

/*
 * dlm_lock
 *
 * Make an asynchronous request to acquire or convert a lock on a named
 * resource.
 *
 * lockspace: context for the request
 * mode: the requested mode of the lock (DLM_LOCK_)
 * lksb: lock status block for input and async return values
 * flags: input flags (DLM_LKF_)
 * name: name of the resource to lock, can be binary
 * namelen: the length in bytes of the resource name (MAX_RESNAME_LEN)
 * parent: the lock ID of a parent lock or 0 if none
 * lockast: function DLM executes when it completes processing the request
 * astarg: argument passed to lockast and bast functions
 * bast: function DLM executes when this lock later blocks another request
 *
 * Returns:
 * 0 if request is successfully queued for processing
 * -EINVAL if any input parameters are invalid
 * -EAGAIN if request would block and is flagged DLM_LKF_NOQUEUE
 * -ENOMEM if there is no memory to process request
 * -ENOTCONN if there is a communication error
 *
 * If the call to dlm_lock returns an error then the operation has failed and
 * the AST routine will not be called.  If dlm_lock returns 0 it is still
 * possible that the lock operation will fail. The AST routine will be called
 * when the locking is complete and the status is returned in the lksb.
 *
 * If the AST routines or parameter are passed to a conversion operation then
 * they will overwrite those values that were passed to a previous dlm_lock
 * call.
 *
 * AST routines should not block (at least not for long), but may make
 * any locking calls they please.
 */

int dlm_lock(dlm_lockspace_t *lockspace,
	     int mode,
	     struct dlm_lksb *lksb,
	     uint32_t flags,
	     void *name,
	     unsigned int namelen,
	     uint32_t parent_lkid,
	     void (*lockast) (void *astarg),
	     void *astarg,
	     void (*bast) (void *astarg, int mode));

/*
 * dlm_unlock
 *
 * Asynchronously release a lock on a resource.  The AST routine is called
 * when the resource is successfully unlocked.
 *
 * lockspace: context for the request
 * lkid: the lock ID as returned in the lksb
 * flags: input flags (DLM_LKF_)
 * lksb: if NULL the lksb parameter passed to last lock request is used
 * astarg: the arg used with the completion ast for the unlock
 *
 * Returns:
 * 0 if request is successfully queued for processing
 * -EINVAL if any input parameters are invalid
 * -ENOTEMPTY if the lock still has sublocks
 * -EBUSY if the lock is waiting for a remote lock operation
 * -ENOTCONN if there is a communication error
 */

int dlm_unlock(dlm_lockspace_t *lockspace,
	       uint32_t lkid,
	       uint32_t flags,
	       struct dlm_lksb *lksb,
	       void *astarg);

#endif				/* __DLM_DOT_H__ */
