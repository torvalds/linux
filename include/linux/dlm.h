/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __DLM_DOT_H__
#define __DLM_DOT_H__

/*
 * Interface to Distributed Lock Manager (DLM)
 * routines and structures to use DLM lockspaces
 */

/*
 * Lock Modes
 */

#define DLM_LOCK_IV		-1	/* invalid */
#define DLM_LOCK_NL		0	/* null */
#define DLM_LOCK_CR		1	/* concurrent read */
#define DLM_LOCK_CW		2	/* concurrent write */
#define DLM_LOCK_PR		3	/* protected read */
#define DLM_LOCK_PW		4	/* protected write */
#define DLM_LOCK_EX		5	/* exclusive */

/*
 * Maximum size in bytes of a dlm_lock name
 */

#define DLM_RESNAME_MAXLEN	64

/*
 * Flags to dlm_lock
 *
 * DLM_LKF_NOQUEUE
 *
 * Do not queue the lock request on the wait queue if it cannot be granted
 * immediately.  If the lock cannot be granted because of this flag, DLM will
 * either return -EAGAIN from the dlm_lock call or will return 0 from
 * dlm_lock and -EAGAIN in the lock status block when the AST is executed.
 *
 * DLM_LKF_CANCEL
 *
 * Used to cancel a pending lock request or conversion.  A converting lock is
 * returned to its previously granted mode.
 *
 * DLM_LKF_CONVERT
 *
 * Indicates a lock conversion request.  For conversions the name and namelen
 * are ignored and the lock ID in the LKSB is used to identify the lock.
 *
 * DLM_LKF_VALBLK
 *
 * Requests DLM to return the current contents of the lock value block in the
 * lock status block.  When this flag is set in a lock conversion from PW or EX
 * modes, DLM assigns the value specified in the lock status block to the lock
 * value block of the lock resource.  The LVB is a DLM_LVB_LEN size array
 * containing application-specific information.
 *
 * DLM_LKF_QUECVT
 *
 * Force a conversion request to be queued, even if it is compatible with
 * the granted modes of other locks on the same resource.
 *
 * DLM_LKF_IVVALBLK
 *
 * Invalidate the lock value block.
 *
 * DLM_LKF_CONVDEADLK
 *
 * Allows the dlm to resolve conversion deadlocks internally by demoting the
 * granted mode of a converting lock to NL.  The DLM_SBF_DEMOTED flag is
 * returned for a conversion that's been effected by this.
 *
 * DLM_LKF_PERSISTENT
 *
 * Only relevant to locks originating in userspace.  A persistent lock will not
 * be removed if the process holding the lock exits.
 *
 * DLM_LKF_NODLKWT
 * DLM_LKF_NODLCKBLK
 *
 * net yet implemented
 *
 * DLM_LKF_EXPEDITE
 *
 * Used only with new requests for NL mode locks.  Tells the lock manager
 * to grant the lock, ignoring other locks in convert and wait queues.
 *
 * DLM_LKF_NOQUEUEBAST
 *
 * Send blocking AST's before returning -EAGAIN to the caller.  It is only
 * used along with the NOQUEUE flag.  Blocking AST's are not sent for failed
 * NOQUEUE requests otherwise.
 *
 * DLM_LKF_HEADQUE
 *
 * Add a lock to the head of the convert or wait queue rather than the tail.
 *
 * DLM_LKF_NOORDER
 *
 * Disregard the standard grant order rules and grant a lock as soon as it
 * is compatible with other granted locks.
 *
 * DLM_LKF_ORPHAN
 *
 * not yet implemented
 *
 * DLM_LKF_ALTPR
 *
 * If the requested mode cannot be granted immediately, try to grant the lock
 * in PR mode instead.  If this alternate mode is granted instead of the
 * requested mode, DLM_SBF_ALTMODE is returned in the lksb.
 *
 * DLM_LKF_ALTCW
 *
 * The same as ALTPR, but the alternate mode is CW.
 *
 * DLM_LKF_FORCEUNLOCK
 *
 * Unlock the lock even if it is converting or waiting or has sublocks.
 * Only really for use by the userland device.c code.
 *
 */

#define DLM_LKF_NOQUEUE		0x00000001
#define DLM_LKF_CANCEL		0x00000002
#define DLM_LKF_CONVERT		0x00000004
#define DLM_LKF_VALBLK		0x00000008
#define DLM_LKF_QUECVT		0x00000010
#define DLM_LKF_IVVALBLK	0x00000020
#define DLM_LKF_CONVDEADLK	0x00000040
#define DLM_LKF_PERSISTENT	0x00000080
#define DLM_LKF_NODLCKWT	0x00000100
#define DLM_LKF_NODLCKBLK	0x00000200
#define DLM_LKF_EXPEDITE	0x00000400
#define DLM_LKF_NOQUEUEBAST	0x00000800
#define DLM_LKF_HEADQUE		0x00001000
#define DLM_LKF_NOORDER		0x00002000
#define DLM_LKF_ORPHAN		0x00004000
#define DLM_LKF_ALTPR		0x00008000
#define DLM_LKF_ALTCW		0x00010000
#define DLM_LKF_FORCEUNLOCK	0x00020000

/*
 * Some return codes that are not in errno.h
 */

#define DLM_ECANCEL		0x10001
#define DLM_EUNLOCK		0x10002

typedef void dlm_lockspace_t;

/*
 * Lock status block
 *
 * Use this structure to specify the contents of the lock value block.  For a
 * conversion request, this structure is used to specify the lock ID of the
 * lock.  DLM writes the status of the lock request and the lock ID assigned
 * to the request in the lock status block.
 *
 * sb_lkid: the returned lock ID.  It is set on new (non-conversion) requests.
 * It is available when dlm_lock returns.
 *
 * sb_lvbptr: saves or returns the contents of the lock's LVB according to rules
 * shown for the DLM_LKF_VALBLK flag.
 *
 * sb_flags: DLM_SBF_DEMOTED is returned if in the process of promoting a lock,
 * it was first demoted to NL to avoid conversion deadlock.
 * DLM_SBF_VALNOTVALID is returned if the resource's LVB is marked invalid.
 *
 * sb_status: the returned status of the lock request set prior to AST
 * execution.  Possible return values:
 *
 * 0 if lock request was successful
 * -EAGAIN if request would block and is flagged DLM_LKF_NOQUEUE
 * -ENOMEM if there is no memory to process request
 * -EINVAL if there are invalid parameters
 * -DLM_EUNLOCK if unlock request was successful
 * -DLM_ECANCEL if a cancel completed successfully
 */

#define DLM_SBF_DEMOTED		0x01
#define DLM_SBF_VALNOTVALID	0x02
#define DLM_SBF_ALTMODE		0x04

struct dlm_lksb {
	int 	 sb_status;
	uint32_t sb_lkid;
	char 	 sb_flags;
	char *	 sb_lvbptr;
};


#ifdef __KERNEL__

#define DLM_LSFL_NODIR		0x00000001

/*
 * dlm_new_lockspace
 *
 * Starts a lockspace with the given name.  If the named lockspace exists in
 * the cluster, the calling node joins it.
 */

int dlm_new_lockspace(char *name, int namelen, dlm_lockspace_t **lockspace,
		      uint32_t flags, int lvblen);

/*
 * dlm_release_lockspace
 *
 * Stop a lockspace.
 */

int dlm_release_lockspace(dlm_lockspace_t *lockspace, int force);

/*
 * dlm_lock
 *
 * Make an asyncronous request to acquire or convert a lock on a named
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

#endif				/* __KERNEL__ */

#endif				/* __DLM_DOT_H__ */

