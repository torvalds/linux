/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SPL_CONDVAR_H
#define	_SPL_CONDVAR_H

#include <linux/module.h>
#include <linux/wait.h>
#include <linux/delay_compat.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/callo.h>

/*
 * The kcondvar_t struct is protected by mutex taken externally before
 * calling any of the wait/signal funs, and passed into the wait funs.
 */
#define	CV_MAGIC			0x346545f4
#define	CV_DESTROY			0x346545f5

typedef struct {
	int cv_magic;
	wait_queue_head_t cv_event;
	wait_queue_head_t cv_destroy;
	atomic_t cv_refs;
	atomic_t cv_waiters;
	kmutex_t *cv_mutex;
} kcondvar_t;

typedef enum { CV_DEFAULT = 0, CV_DRIVER } kcv_type_t;

extern void __cv_init(kcondvar_t *, char *, kcv_type_t, void *);
extern void __cv_destroy(kcondvar_t *);
extern void __cv_wait(kcondvar_t *, kmutex_t *);
extern void __cv_wait_io(kcondvar_t *, kmutex_t *);
extern void __cv_wait_sig(kcondvar_t *, kmutex_t *);
extern clock_t __cv_timedwait(kcondvar_t *, kmutex_t *, clock_t);
extern clock_t __cv_timedwait_sig(kcondvar_t *, kmutex_t *, clock_t);
extern clock_t cv_timedwait_hires(kcondvar_t *, kmutex_t *, hrtime_t,
    hrtime_t res, int flag);
extern void __cv_signal(kcondvar_t *);
extern void __cv_broadcast(kcondvar_t *c);

#define	cv_init(cvp, name, type, arg)		__cv_init(cvp, name, type, arg)
#define	cv_destroy(cvp)				__cv_destroy(cvp)
#define	cv_wait(cvp, mp)			__cv_wait(cvp, mp)
#define	cv_wait_io(cvp, mp)			__cv_wait_io(cvp, mp)
#define	cv_wait_sig(cvp, mp)			__cv_wait_sig(cvp, mp)
#define	cv_wait_interruptible(cvp, mp)		cv_wait_sig(cvp, mp)
#define	cv_timedwait(cvp, mp, t)		__cv_timedwait(cvp, mp, t)
#define	cv_timedwait_sig(cvp, mp, t)		__cv_timedwait_sig(cvp, mp, t)
#define	cv_timedwait_interruptible(cvp, mp, t)	cv_timedwait_sig(cvp, mp, t)
#define	cv_signal(cvp)				__cv_signal(cvp)
#define	cv_broadcast(cvp)			__cv_broadcast(cvp)

#endif /* _SPL_CONDVAR_H */
