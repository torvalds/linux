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
 *****************************************************************************
 *  Solaris Porting Layer (SPL) Reader/Writer Lock Implementation.
\*****************************************************************************/

#include <sys/rwlock.h>

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_RWLOCK

#if defined(CONFIG_RWSEM_GENERIC_SPINLOCK)
static int
__rwsem_tryupgrade(struct rw_semaphore *rwsem)
{
	int ret = 0;
	unsigned long flags;
	spl_rwsem_lock_irqsave(&rwsem->wait_lock, flags);
	if (RWSEM_COUNT(rwsem) == SPL_RWSEM_SINGLE_READER_VALUE &&
	    list_empty(&rwsem->wait_list)) {
		ret = 1;
		RWSEM_COUNT(rwsem) = SPL_RWSEM_SINGLE_WRITER_VALUE;
	}
	spl_rwsem_unlock_irqrestore(&rwsem->wait_lock, flags);
	return (ret);
}
#elif defined(HAVE_RWSEM_ATOMIC_LONG_COUNT)
static int
__rwsem_tryupgrade(struct rw_semaphore *rwsem)
{
	long val;
	val = atomic_long_cmpxchg(&rwsem->count, SPL_RWSEM_SINGLE_READER_VALUE,
	    SPL_RWSEM_SINGLE_WRITER_VALUE);
	return (val == SPL_RWSEM_SINGLE_READER_VALUE);
}
#else
static int
__rwsem_tryupgrade(struct rw_semaphore *rwsem)
{
	typeof (rwsem->count) val;
	val = cmpxchg(&rwsem->count, SPL_RWSEM_SINGLE_READER_VALUE,
	    SPL_RWSEM_SINGLE_WRITER_VALUE);
	return (val == SPL_RWSEM_SINGLE_READER_VALUE);
}
#endif

int
rwsem_tryupgrade(struct rw_semaphore *rwsem)
{
	if (__rwsem_tryupgrade(rwsem)) {
		rwsem_release(&rwsem->dep_map, 1, _RET_IP_);
		rwsem_acquire(&rwsem->dep_map, 0, 1, _RET_IP_);
#ifdef CONFIG_RWSEM_SPIN_ON_OWNER
		rwsem->owner = current;
#endif
		return (1);
	}
	return (0);
}
EXPORT_SYMBOL(rwsem_tryupgrade);

int spl_rw_init(void) { return 0; }
void spl_rw_fini(void) { }
