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
 * Support functions for managing command queues used for
 * various hardware blocks.
 *
 * <hr>$Revision: 70030 $<hr>
 */
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <linux/module.h>
#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-bootmem.h>
#include <asm/octeon/cvmx-npei-defs.h>
#include <asm/octeon/cvmx-pexp-defs.h>
#include <asm/octeon/cvmx-dpi-defs.h>
#include <asm/octeon/cvmx-pko-defs.h>
#include <asm/octeon/cvmx-config.h>
#include <asm/octeon/cvmx-fpa.h>
#include <asm/octeon/cvmx-cmd-queue.h>
#else
#include "cvmx.h"
#include "cvmx-bootmem.h"
#if !defined(__FreeBSD__) || !defined(_KERNEL)
#include "cvmx-config.h"
#endif
#include "cvmx-fpa.h"
#include "cvmx-cmd-queue.h"
#endif


/**
 * This application uses this pointer to access the global queue
 * state. It points to a bootmem named block.
 */
CVMX_SHARED __cvmx_cmd_queue_all_state_t *__cvmx_cmd_queue_state_ptr;
#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
EXPORT_SYMBOL(__cvmx_cmd_queue_state_ptr);
#endif

/**
 * @INTERNAL
 * Initialize the Global queue state pointer.
 *
 * @return CVMX_CMD_QUEUE_SUCCESS or a failure code
 */
static cvmx_cmd_queue_result_t __cvmx_cmd_queue_init_state_ptr(void)
{
    char *alloc_name = "cvmx_cmd_queues";
#if defined(CONFIG_CAVIUM_RESERVE32) && CONFIG_CAVIUM_RESERVE32
    extern uint64_t octeon_reserve32_memory;
#endif

    if (cvmx_likely(__cvmx_cmd_queue_state_ptr))
        return CVMX_CMD_QUEUE_SUCCESS;

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#if defined(CONFIG_CAVIUM_RESERVE32) && CONFIG_CAVIUM_RESERVE32
    if (octeon_reserve32_memory)
        __cvmx_cmd_queue_state_ptr = cvmx_bootmem_alloc_named_range(sizeof(*__cvmx_cmd_queue_state_ptr),
                                                              octeon_reserve32_memory,
                                                              octeon_reserve32_memory + (CONFIG_CAVIUM_RESERVE32<<20) - 1,
                                                              128, alloc_name);
    else
#endif
        __cvmx_cmd_queue_state_ptr = cvmx_bootmem_alloc_named(sizeof(*__cvmx_cmd_queue_state_ptr), 128, alloc_name);
#else
    __cvmx_cmd_queue_state_ptr = cvmx_bootmem_alloc_named(sizeof(*__cvmx_cmd_queue_state_ptr), 128, alloc_name);
#endif
    if (__cvmx_cmd_queue_state_ptr)
        memset(__cvmx_cmd_queue_state_ptr, 0, sizeof(*__cvmx_cmd_queue_state_ptr));
    else
    {
        const cvmx_bootmem_named_block_desc_t *block_desc = cvmx_bootmem_find_named_block(alloc_name);
        if (block_desc)
            __cvmx_cmd_queue_state_ptr = cvmx_phys_to_ptr(block_desc->base_addr);
        else
        {
            cvmx_dprintf("ERROR: cvmx_cmd_queue_initialize: Unable to get named block %s.\n", alloc_name);
            return CVMX_CMD_QUEUE_NO_MEMORY;
        }
    }
    return CVMX_CMD_QUEUE_SUCCESS;
}


/**
 * Initialize a command queue for use. The initial FPA buffer is
 * allocated and the hardware unit is configured to point to the
 * new command queue.
 *
 * @param queue_id  Hardware command queue to initialize.
 * @param max_depth Maximum outstanding commands that can be queued.
 * @param fpa_pool  FPA pool the command queues should come from.
 * @param pool_size Size of each buffer in the FPA pool (bytes)
 *
 * @return CVMX_CMD_QUEUE_SUCCESS or a failure code
 */
cvmx_cmd_queue_result_t cvmx_cmd_queue_initialize(cvmx_cmd_queue_id_t queue_id, int max_depth, int fpa_pool, int pool_size)
{
    __cvmx_cmd_queue_state_t *qstate;
    cvmx_cmd_queue_result_t result = __cvmx_cmd_queue_init_state_ptr();
    if (result != CVMX_CMD_QUEUE_SUCCESS)
        return result;

    qstate = __cvmx_cmd_queue_get_state(queue_id);
    if (qstate == NULL)
        return CVMX_CMD_QUEUE_INVALID_PARAM;

    /* We artificially limit max_depth to 1<<20 words. It is an arbitrary limit */
    if (CVMX_CMD_QUEUE_ENABLE_MAX_DEPTH)
    {
        if ((max_depth < 0) || (max_depth > 1<<20))
            return CVMX_CMD_QUEUE_INVALID_PARAM;
    }
    else if (max_depth != 0)
        return CVMX_CMD_QUEUE_INVALID_PARAM;

    if ((fpa_pool < 0) || (fpa_pool > 7))
        return CVMX_CMD_QUEUE_INVALID_PARAM;
    if ((pool_size < 128) || (pool_size > 65536))
        return CVMX_CMD_QUEUE_INVALID_PARAM;

    /* See if someone else has already initialized the queue */
    if (qstate->base_ptr_div128)
    {
        if (max_depth != (int)qstate->max_depth)
        {
            cvmx_dprintf("ERROR: cvmx_cmd_queue_initialize: Queue already initialized with different max_depth (%d).\n", (int)qstate->max_depth);
            return CVMX_CMD_QUEUE_INVALID_PARAM;
        }
        if (fpa_pool != qstate->fpa_pool)
        {
            cvmx_dprintf("ERROR: cvmx_cmd_queue_initialize: Queue already initialized with different FPA pool (%u).\n", qstate->fpa_pool);
            return CVMX_CMD_QUEUE_INVALID_PARAM;
        }
        if ((pool_size>>3)-1 != qstate->pool_size_m1)
        {
            cvmx_dprintf("ERROR: cvmx_cmd_queue_initialize: Queue already initialized with different FPA pool size (%u).\n", (qstate->pool_size_m1+1)<<3);
            return CVMX_CMD_QUEUE_INVALID_PARAM;
        }
        CVMX_SYNCWS;
        return CVMX_CMD_QUEUE_ALREADY_SETUP;
    }
    else
    {
        cvmx_fpa_ctl_status_t status;
        void *buffer;

        status.u64 = cvmx_read_csr(CVMX_FPA_CTL_STATUS);
        if (!status.s.enb)
        {
            cvmx_dprintf("ERROR: cvmx_cmd_queue_initialize: FPA is not enabled.\n");
            return CVMX_CMD_QUEUE_NO_MEMORY;
        }
        buffer = cvmx_fpa_alloc(fpa_pool);
        if (buffer == NULL)
        {
            cvmx_dprintf("ERROR: cvmx_cmd_queue_initialize: Unable to allocate initial buffer.\n");
            return CVMX_CMD_QUEUE_NO_MEMORY;
        }

        memset(qstate, 0, sizeof(*qstate));
        qstate->max_depth = max_depth;
        qstate->fpa_pool = fpa_pool;
        qstate->pool_size_m1 = (pool_size>>3)-1;
        qstate->base_ptr_div128 = cvmx_ptr_to_phys(buffer) / 128;
        /* We zeroed the now serving field so we need to also zero the ticket */
        __cvmx_cmd_queue_state_ptr->ticket[__cvmx_cmd_queue_get_index(queue_id)] = 0;
        CVMX_SYNCWS;
        return CVMX_CMD_QUEUE_SUCCESS;
    }
}


/**
 * Shutdown a queue a free it's command buffers to the FPA. The
 * hardware connected to the queue must be stopped before this
 * function is called.
 *
 * @param queue_id Queue to shutdown
 *
 * @return CVMX_CMD_QUEUE_SUCCESS or a failure code
 */
cvmx_cmd_queue_result_t cvmx_cmd_queue_shutdown(cvmx_cmd_queue_id_t queue_id)
{
    __cvmx_cmd_queue_state_t *qptr = __cvmx_cmd_queue_get_state(queue_id);
    if (qptr == NULL)
    {
        cvmx_dprintf("ERROR: cvmx_cmd_queue_shutdown: Unable to get queue information.\n");
        return CVMX_CMD_QUEUE_INVALID_PARAM;
    }

    if (cvmx_cmd_queue_length(queue_id) > 0)
    {
        cvmx_dprintf("ERROR: cvmx_cmd_queue_shutdown: Queue still has data in it.\n");
        return CVMX_CMD_QUEUE_FULL;
    }

    __cvmx_cmd_queue_lock(queue_id, qptr);
    if (qptr->base_ptr_div128)
    {
        cvmx_fpa_free(cvmx_phys_to_ptr((uint64_t)qptr->base_ptr_div128<<7), qptr->fpa_pool, 0);
        qptr->base_ptr_div128 = 0;
    }
    __cvmx_cmd_queue_unlock(qptr);

    return CVMX_CMD_QUEUE_SUCCESS;
}


/**
 * Return the number of command words pending in the queue. This
 * function may be relatively slow for some hardware units.
 *
 * @param queue_id Hardware command queue to query
 *
 * @return Number of outstanding commands
 */
int cvmx_cmd_queue_length(cvmx_cmd_queue_id_t queue_id)
{
    if (CVMX_ENABLE_PARAMETER_CHECKING)
    {
        if (__cvmx_cmd_queue_get_state(queue_id) == NULL)
            return CVMX_CMD_QUEUE_INVALID_PARAM;
    }

    /* The cast is here so gcc with check that all values in the
        cvmx_cmd_queue_id_t enumeration are here */
    switch ((cvmx_cmd_queue_id_t)(queue_id & 0xff0000))
    {
        case CVMX_CMD_QUEUE_PKO_BASE:
            /* FIXME: Need atomic lock on CVMX_PKO_REG_READ_IDX. Right now we
                are normally called with the queue lock, so that is a SLIGHT
                amount of protection */
            cvmx_write_csr(CVMX_PKO_REG_READ_IDX, queue_id & 0xffff);
            if (OCTEON_IS_MODEL(OCTEON_CN3XXX))
            {
                cvmx_pko_mem_debug9_t debug9;
                debug9.u64 = cvmx_read_csr(CVMX_PKO_MEM_DEBUG9);
                return debug9.cn38xx.doorbell;
            }
            else
            {
                cvmx_pko_mem_debug8_t debug8;
                debug8.u64 = cvmx_read_csr(CVMX_PKO_MEM_DEBUG8);
                if (octeon_has_feature(OCTEON_FEATURE_PKND))
                    return debug8.cn68xx.doorbell;
                else
                    return debug8.cn58xx.doorbell;
            }
        case CVMX_CMD_QUEUE_ZIP:
        case CVMX_CMD_QUEUE_DFA:
        case CVMX_CMD_QUEUE_RAID:
            // FIXME: Implement other lengths
            return 0;
        case CVMX_CMD_QUEUE_DMA_BASE:
            if (octeon_has_feature(OCTEON_FEATURE_NPEI))
            {
                cvmx_npei_dmax_counts_t dmax_counts;
                dmax_counts.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_DMAX_COUNTS(queue_id & 0x7));
                return dmax_counts.s.dbell;
            }
            else
            {
                cvmx_dpi_dmax_counts_t dmax_counts;
                dmax_counts.u64 = cvmx_read_csr(CVMX_DPI_DMAX_COUNTS(queue_id & 0x7));
                return dmax_counts.s.dbell;
            }
        case CVMX_CMD_QUEUE_END:
            return CVMX_CMD_QUEUE_INVALID_PARAM;
    }
    return CVMX_CMD_QUEUE_INVALID_PARAM;
}


/**
 * Return the command buffer to be written to. The purpose of this
 * function is to allow CVMX routine access to the low level buffer
 * for initial hardware setup. User applications should not call this
 * function directly.
 *
 * @param queue_id Command queue to query
 *
 * @return Command buffer or NULL on failure
 */
void *cvmx_cmd_queue_buffer(cvmx_cmd_queue_id_t queue_id)
{
    __cvmx_cmd_queue_state_t *qptr = __cvmx_cmd_queue_get_state(queue_id);
    if (qptr && qptr->base_ptr_div128)
        return cvmx_phys_to_ptr((uint64_t)qptr->base_ptr_div128<<7);
    else
        return NULL;
}

