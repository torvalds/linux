/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __DLMCONSTANTS_DOT_H__
#define __DLMCONSTANTS_DOT_H__

/*
 * Constants used by DLM interface.
 */

#define DLM_LOCKSPACE_LEN       64
#define DLM_RESNAME_MAXLEN      64


/*
 * Lock Modes
 */

#define DLM_LOCK_IV		(-1)	/* invalid */
#define DLM_LOCK_NL		0	/* null */
#define DLM_LOCK_CR		1	/* concurrent read */
#define DLM_LOCK_CW		2	/* concurrent write */
#define DLM_LOCK_PR		3	/* protected read */
#define DLM_LOCK_PW		4	/* protected write */
#define DLM_LOCK_EX		5	/* exclusive */


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
 * DLM_LKF_NODLCKWT
 *
 * Do not cancel the lock if it gets into conversion deadlock.
 *
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
 * Acquire an orphan lock.
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
 * DLM_LKF_TIMEOUT
 *
 * This value is deprecated and reserved. DO NOT USE!
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
#define DLM_LKF_TIMEOUT		0x00040000

/*
 * Some return codes that are not in errno.h
 */

#define DLM_ECANCEL		0x10001
#define DLM_EUNLOCK		0x10002

#endif  /* __DLMCONSTANTS_DOT_H__ */
