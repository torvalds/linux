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
 * Interface to the hardware work queue timers.
 *
`* <hr>$Revision: 70030 $<hr>
 */

#ifndef __CVMX_TIM_H__
#define __CVMX_TIM_H__

#include "cvmx-clock.h"
#include "cvmx-fpa.h"
#include "cvmx-wqe.h"

#include "executive-config.h"
#ifdef CVMX_ENABLE_TIMER_FUNCTIONS
#include "cvmx-config.h"
#endif

#ifdef	__cplusplus
extern "C" {
#endif

#define CVMX_TIM_NUM_TIMERS   (OCTEON_IS_MODEL(OCTEON_CN68XX) ? 64 : 16)
#define CVMX_TIM_NUM_BUCKETS  2048

typedef enum
{
    CVMX_TIM_STATUS_SUCCESS = 0,
    CVMX_TIM_STATUS_NO_MEMORY = -1,
    CVMX_TIM_STATUS_TOO_FAR_AWAY = -2,
    CVMX_TIM_STATUS_BUSY = -3
} cvmx_tim_status_t;

/**
 * Each timer bucket contains a list of work queue entries to
 * schedule when the timer fires. The list is implemented as
 * a linked list of blocks. Each block contains an array of
 * work queue entries followed by a next block pointer. Since
 * these blocks are dynamically allocated off of a hardware
 * memory pool, there actual size isn't known compile time.
 * The next block pointer is stored in the last 8 bytes of
 * the memory block.
 */
typedef struct cvmx_tim_entry_chunk
{
    volatile uint64_t entries[0];
} cvmx_tim_entry_chunk_t;

/**
 * Each timer contains an array of buckets. Each bucket
 * represents the list of work queue entries that should be
 * scheduled when the timer fires.  The first 3 entries are used
 * byt the hardware.
 */
typedef struct
{
   volatile uint64_t                first_chunk_addr;
   volatile uint32_t                num_entries;    /**< Zeroed by HW after traversing list */
   volatile uint32_t                chunk_remainder;/**< Zeroed by HW after traversing list */

   // the remaining 16 bytes are not touched by hardware
   volatile cvmx_tim_entry_chunk_t *last_chunk;
   uint64_t                         pad;
} cvmx_tim_bucket_entry_t;

/**
 * Structure representing an individual timer. Each timer has
 * a timer period, a memory management pool, and a list of
 * buckets.
 */
typedef struct
{
    cvmx_tim_bucket_entry_t*bucket;             /**< The timer buckets. Array of [CVMX_TIM_NUM_TIMERS][CVMX_TIM_NUM_BUCKETS] */
    uint64_t                tick_cycles;        /**< How long a bucket represents */
    uint64_t                start_time;         /**< Time the timer started in cycles */
    uint32_t                bucket_shift;       /**< How long a bucket represents in ms */
    uint32_t                num_buckets;        /**< How many buckets per wheel */
    uint32_t                max_ticks;          /**< maximum number of ticks allowed for timer */
} cvmx_tim_t;

/**
 * Structure used to store state information needed to delete
 * an already scheduled timer entry. An instance of this
 * structure must be passed to cvmx_tim_add_entry in order
 * to be able to delete an entry later with
 * cvmx_tim_delete_entry.
 *
 * NOTE: This structure should be considered opaque by the application,
 * and the application should not access its members
 */
typedef struct
{
    uint64_t        commit_cycles;  /**< After this time the timer can't be changed */
    uint64_t *      timer_entry_ptr;/**< Where the work entry is. Zero this
                                            location to delete the entry */
} cvmx_tim_delete_t;

/**
 * Global structure holding the state of all timers.
 */
extern cvmx_tim_t cvmx_tim;




#ifdef CVMX_ENABLE_TIMER_FUNCTIONS
/**
 * Setup a timer for use. Must be called before the timer
 * can be used.
 *
 * @param tick      Time between each bucket in microseconds. This must not be
 *                  smaller than 1024/(clock frequency in MHz).
 * @param max_ticks The maximum number of ticks the timer must be able
 *                  to schedule in the future. There are guaranteed to be enough
 *                  timer buckets such that:
 *                  number of buckets >= max_ticks.
 * @return Zero on success. Negative on error. Failures are possible
 *         if the number of buckets needed is too large or memory
 *         allocation fails for creating the buckets.
 */
int cvmx_tim_setup(uint64_t tick, uint64_t max_ticks);
#endif

/**
 * Start the hardware timer processing
 */
extern void cvmx_tim_start(void);


/**
 * Stop the hardware timer processing. Timers stay configured.
 */
extern void cvmx_tim_stop(void);


/**
 * Stop the timer. After this the timer must be setup again
 * before use.
 */
#ifdef CVMX_ENABLE_TIMER_FUNCTIONS
extern void cvmx_tim_shutdown(void);
#endif

#ifdef CVMX_ENABLE_TIMER_FUNCTIONS
/**
 * Add a work queue entry to the timer.
 *
 * @param work_entry Work queue entry to add.
 * @param ticks_from_now
 * @param delete_info
 *                   Optional pointer where to store information needed to
 *                   delete the timer entry. If non NULL information needed
 *                   to delete the timer entry before it fires is stored here.
 *                   If you don't need to be able to delete the timer, pass
 *                   NULL.
 * @return Result return code
 */
static inline cvmx_tim_status_t cvmx_tim_add_entry(cvmx_wqe_t *work_entry, uint64_t ticks_from_now, cvmx_tim_delete_t *delete_info)
{
    cvmx_tim_bucket_entry_t*    work_bucket_ptr;
    uint64_t                    work_bucket;
    volatile uint64_t         * tim_entry_ptr;  /* pointer to wqe address in timer chunk */
    uint64_t                    entries_per_chunk;

    const uint64_t  cycles  = cvmx_clock_get_count(CVMX_CLOCK_TIM); /* Get our reference time early for accuracy */
    const uint64_t  core_num    = cvmx_get_core_num();  /* One timer per processor, so use this to select */

    /* Make sure the specified time won't wrap our bucket list */
    if (ticks_from_now > cvmx_tim.max_ticks)
    {
        cvmx_dprintf("cvmx_tim_add_entry: Tried to schedule work too far away.\n");
        return CVMX_TIM_STATUS_TOO_FAR_AWAY;
    }

    /* Since we have no way to synchronize, we can't update a timer that is
        being used by the hardware. Two buckets forward should be safe */
    if (ticks_from_now < 2)
    {
        cvmx_dprintf("cvmx_tim_add_entry: Tried to schedule work too soon. Delaying it.\n");
        ticks_from_now = 2;
    }

    /* Get the bucket this work queue entry should be in. Remember the bucket
        array is circular */
    work_bucket = (((ticks_from_now * cvmx_tim.tick_cycles) + cycles - cvmx_tim.start_time)
		   >> cvmx_tim.bucket_shift);

    work_bucket_ptr = cvmx_tim.bucket + core_num * cvmx_tim.num_buckets + (work_bucket & (cvmx_tim.num_buckets - 1));
    entries_per_chunk = (CVMX_FPA_TIMER_POOL_SIZE/8 - 1);

    /* Check if we have room to add this entry into the existing list */
    if (work_bucket_ptr->chunk_remainder)
    {
        /* Adding the work entry to the end of the existing list */
        tim_entry_ptr = &(work_bucket_ptr->last_chunk->entries[entries_per_chunk - work_bucket_ptr->chunk_remainder]);
        *tim_entry_ptr = cvmx_ptr_to_phys(work_entry);
        work_bucket_ptr->chunk_remainder--;
        work_bucket_ptr->num_entries++;
    }
    else
    {
        /* Current list is either completely empty or completely full. We need
            to allocate a new chunk for storing this work entry */
        cvmx_tim_entry_chunk_t *new_chunk = (cvmx_tim_entry_chunk_t *)cvmx_fpa_alloc(CVMX_FPA_TIMER_POOL);
        if (new_chunk == NULL)
        {
            cvmx_dprintf("cvmx_tim_add_entry: Failed to allocate memory for new chunk.\n");
            return CVMX_TIM_STATUS_NO_MEMORY;
        }

        /* Does a chunk currently exist? We have to check num_entries since
            the hardware doesn't NULL out the chunk pointers on free */
        if (work_bucket_ptr->num_entries)
        {
            /* This chunk must be appended to an existing list by putting
            ** its address in the last spot of the existing chunk. */
            work_bucket_ptr->last_chunk->entries[entries_per_chunk] = cvmx_ptr_to_phys(new_chunk);
            work_bucket_ptr->num_entries++;
        }
        else
        {
            /* This is the very first chunk. Add it */
            work_bucket_ptr->first_chunk_addr = cvmx_ptr_to_phys(new_chunk);
            work_bucket_ptr->num_entries = 1;
        }
        work_bucket_ptr->last_chunk = new_chunk;
        work_bucket_ptr->chunk_remainder = entries_per_chunk - 1;
        tim_entry_ptr = &(new_chunk->entries[0]);
        *tim_entry_ptr = cvmx_ptr_to_phys(work_entry);
    }

    /* If the user supplied a delete info structure then fill it in */
    if (delete_info)
    {
        /* It would be very bad to delete a timer entry after, or during the
            timer's processing. During the processing could yield unpredicatable
            results, but after would always be bad. Modifying the entry after
            processing means we would be changing data in a buffer that has been
            freed, and possible allocated again. For this reason we store a
            commit cycle count in the delete structure. If we are after this
            count we will refuse to delete the timer entry. */
        delete_info->commit_cycles = cycles + (ticks_from_now - 2) * cvmx_tim.tick_cycles;
        delete_info->timer_entry_ptr = (uint64_t *)tim_entry_ptr;  /* Cast to non-volatile type */
    }

    CVMX_SYNCWS; /* Make sure the hardware timer unit can access valid data from L2 */

    return CVMX_TIM_STATUS_SUCCESS;
}
#endif


/**
 * Delete a timer entry scheduled using cvmx_tim_add_entry.
 * Deleting a timer will fail if it has already triggered or
 * might be in progress. The actual state of the work queue
 * entry isn't changed. You need to dispose of it properly.
 *
 * @param delete_info
 *               Structure passed to cvmx_tim_add_entry to store the
 *               information needed to delete a timer entry.
 * @return CVMX_TIM_STATUS_BUSY if the timer was not deleted, otherwise
 *         CVMX_TIM_STATUS_SUCCESS.
 */
static inline cvmx_tim_status_t cvmx_tim_delete_entry(cvmx_tim_delete_t *delete_info)
{
    const uint64_t cycles = cvmx_clock_get_count(CVMX_CLOCK_TIM);

    if ((int64_t)(cycles - delete_info->commit_cycles) < 0)
    {
        /* Timer is far enough away. Safe to delete */
        *delete_info->timer_entry_ptr = 0;
        return CVMX_TIM_STATUS_SUCCESS;
    }
    else
    {
        /* Timer is passed the commit time. It cannot be stopped */
        return CVMX_TIM_STATUS_BUSY;
    }
}

#ifdef	__cplusplus
}
#endif

#endif // __CVMX_TIM_H__
