/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/







/**
 * @file
 *
 * This file provides reader/writer locks.
 *
 * <hr>$Revision: 70030 $<hr>
 *
 *
 */


#ifndef __CVMX_RWLOCK_H__
#define __CVMX_RWLOCK_H__

/* include to get atomic compare and store */
#include "cvmx-atomic.h"

#ifdef	__cplusplus
extern "C" {
#endif

/* Flags for lock value in rw lock structure */
#define CVMX_RWLOCK_WRITE_FLAG     0x1
#define CVMX_RWLOCK_READ_INC       0x2


/* Writer preference locks (wp).  Can be starved by writers.  When a writer
 * is waiting, no readers are given the lock until all writers are done.
 */
typedef struct
{
    volatile uint32_t lock;
    volatile uint32_t write_req;
    volatile uint32_t write_comp;
} cvmx_rwlock_wp_lock_t;

/**
 * Initialize a reader/writer lock.  This must be done
 * by a single core before used.
 *
 * @param lock   pointer to rwlock structure
 */
static inline void cvmx_rwlock_wp_init(cvmx_rwlock_wp_lock_t *lock)
{
    lock->lock = 0;
    lock->write_req = 0;
    lock->write_comp = 0;
}

/**
 * Perform a reader lock.  If a writer is pending, this
 * will wait for that writer to complete before locking.
 *
 * NOTE: Each thread/process must only lock any rwlock
 * once, or else a deadlock may result.
 *
 * @param lock   pointer to rwlock structure
 */
static inline void cvmx_rwlock_wp_read_lock(cvmx_rwlock_wp_lock_t *lock)
{

    /* Wait for outstanding write requests to be serviced */
    while (lock->write_req != lock->write_comp)
        ;
    /* Add ourselves to interested reader count */
    cvmx_atomic_add32_nosync((int32_t *)&(lock->lock), CVMX_RWLOCK_READ_INC);
    /* Wait for writer to finish.  No writer will start again
    ** until after we are done since we have already incremented
    ** the reader count
    */
    while (lock->lock & CVMX_RWLOCK_WRITE_FLAG)
        ;

}

/**
 * Perform a reader unlock.
 *
 * @param lock   pointer to rwlock structure
 */
static inline void cvmx_rwlock_wp_read_unlock(cvmx_rwlock_wp_lock_t *lock)
{
    /* Remove ourselves to reader count */
    cvmx_atomic_add32_nosync((int32_t *)&(lock->lock), -CVMX_RWLOCK_READ_INC);
}

/**
 * Perform a writer lock.  Any readers that attempt
 * to get a lock while there are any pending write locks
 * will wait until all writers have completed.  Starvation
 * of readers by writers is possible and must be avoided
 * by the application.
 *
 * @param lock   pointer to rwlock structure
 */
static inline void cvmx_rwlock_wp_write_lock(cvmx_rwlock_wp_lock_t *lock)
{
    /* Get previous value of write requests */
    uint32_t prev_writers = ((uint32_t)cvmx_atomic_fetch_and_add32((int32_t *)&(lock->write_req), 1));
    /* Spin until our turn */
    while (prev_writers != lock->write_comp)
        ;
    /* Spin until no other readers or writers, then set write flag */
    while (!cvmx_atomic_compare_and_store32((uint32_t *)&(lock->lock), 0, CVMX_RWLOCK_WRITE_FLAG))
        ;

}
/**
 * Perform a writer unlock.
 *
 * @param lock   pointer to rwlock structure
 */
static inline void cvmx_rwlock_wp_write_unlock(cvmx_rwlock_wp_lock_t *lock)
{
    /* Remove our writer flag */
    CVMX_SYNCWS;  /* Make sure all writes in protected region are visible before unlock */
    cvmx_atomic_add32_nosync((int32_t *)&(lock->lock), -CVMX_RWLOCK_WRITE_FLAG);
    cvmx_atomic_add32_nosync((int32_t *)&(lock->write_comp), 1);
    CVMX_SYNCWS;  /* push unlock writes out, but don't stall */
}

#ifdef	__cplusplus
}
#endif

#endif /* __CVMX_RWLOCK_H__ */
