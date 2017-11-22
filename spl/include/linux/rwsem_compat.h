/*****************************************************************************\
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
\*****************************************************************************/

#ifndef _SPL_RWSEM_COMPAT_H
#define _SPL_RWSEM_COMPAT_H

#include <linux/rwsem.h>

#if defined(CONFIG_PREEMPT_RT_FULL)
#define	SPL_RWSEM_SINGLE_READER_VALUE	(1)
#define	SPL_RWSEM_SINGLE_WRITER_VALUE	(0)
#elif defined(CONFIG_RWSEM_GENERIC_SPINLOCK)
#define	SPL_RWSEM_SINGLE_READER_VALUE	(1)
#define	SPL_RWSEM_SINGLE_WRITER_VALUE	(-1)
#else
#define	SPL_RWSEM_SINGLE_READER_VALUE	(RWSEM_ACTIVE_READ_BIAS)
#define	SPL_RWSEM_SINGLE_WRITER_VALUE	(RWSEM_ACTIVE_WRITE_BIAS)
#endif

/* Linux 3.16 changed activity to count for rwsem-spinlock */
#if defined(CONFIG_PREEMPT_RT_FULL)
#define	RWSEM_COUNT(sem)	sem->read_depth
#elif defined(HAVE_RWSEM_ACTIVITY)
#define	RWSEM_COUNT(sem)	sem->activity
/* Linux 4.8 changed count to an atomic_long_t for !rwsem-spinlock */
#elif defined(HAVE_RWSEM_ATOMIC_LONG_COUNT)
#define	RWSEM_COUNT(sem)	atomic_long_read(&(sem)->count)
#else
#define	RWSEM_COUNT(sem)	sem->count
#endif

int rwsem_tryupgrade(struct rw_semaphore *rwsem);

#if defined(RWSEM_SPINLOCK_IS_RAW)
#define spl_rwsem_lock_irqsave(lk, fl)       raw_spin_lock_irqsave(lk, fl)
#define spl_rwsem_unlock_irqrestore(lk, fl)  raw_spin_unlock_irqrestore(lk, fl)
#define spl_rwsem_trylock_irqsave(lk, fl)    raw_spin_trylock_irqsave(lk, fl)
#else
#define spl_rwsem_lock_irqsave(lk, fl)       spin_lock_irqsave(lk, fl)
#define spl_rwsem_unlock_irqrestore(lk, fl)  spin_unlock_irqrestore(lk, fl)
#define spl_rwsem_trylock_irqsave(lk, fl)    spin_trylock_irqsave(lk, fl)
#endif /* RWSEM_SPINLOCK_IS_RAW */

#define spl_rwsem_is_locked(rwsem)           rwsem_is_locked(rwsem)

#endif /* _SPL_RWSEM_COMPAT_H */
