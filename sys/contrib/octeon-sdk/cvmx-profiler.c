/***********************license start***************
 * Copyright (c) 2011  Cavium Inc. (support@cavium.com). All rights
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
 *
 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.
 *
 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.
 *
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
 ************************license end**************************************/

/**
 * @file
 *
 * Interface to event profiler.
 *
 */

#include "cvmx-config.h"
#include "cvmx.h"
#include "cvmx-interrupt.h"
#include "cvmx-sysinfo.h"
#include "cvmx-coremask.h"
#include "cvmx-spinlock.h"
#include "cvmx-atomic.h"
#if !defined(CVMX_BUILD_FOR_FREEBSD_KERNEL)
#include "cvmx-error.h"
#endif
#include "cvmx-asm.h"
#include "cvmx-bootmem.h"
#include "cvmx-profiler.h"

#ifdef PROFILER_DEBUG
#define PRINTF(fmt, args...)    cvmx_safe_printf(fmt, ##args)
#else
#define PRINTF(fmt, args...)
#endif

CVMX_SHARED static event_counter_control_block_t eccb;
cvmx_config_block_t *pcpu_cfg_blk;

int read_percpu_block = 1;

/**
 * Set Interrupt IRQ line for Performance Counter
 *
 */
void cvmx_update_perfcnt_irq(void)
{
    uint64_t cvmctl;
   
    /* Clear CvmCtl[IPPCI] bit and move the Performance Counter
     * interrupt to IRQ 6
     */
    CVMX_MF_COP0(cvmctl, COP0_CVMCTL);
    cvmctl &= ~(7 << 7);
    cvmctl |= 6 << 7;
    CVMX_MT_COP0(cvmctl, COP0_CVMCTL);
}

/**
 * @INTERNAL
 * Return the baseaddress of the namedblock 
 * @param buf_name  Name of Namedblock
 *
 * @return baseaddress of block on Success, NULL on failure.
 */
static
void *cvmx_get_memory_addr(const char* buf_name)
{
    void *buffer_ptr = NULL;
    const struct cvmx_bootmem_named_block_desc *block_desc = cvmx_bootmem_find_named_block(buf_name);
    if (block_desc)
        buffer_ptr = cvmx_phys_to_ptr(block_desc->base_addr);
    assert (buffer_ptr != NULL);

    return buffer_ptr;
}

/**
 * @INTERNAL
 * Initialize the cpu block metadata. 
 * 
 * @param cpu	core no
 * @param size	size of per cpu memory in named block
 *
 */
static
void cvmx_init_pcpu_block(int cpu, int size)
{
    eccb.cfg_blk.pcpu_base_addr[cpu] = (char *)cvmx_get_memory_addr(EVENT_BUFFER_BLOCK) + (size * cpu); 
    assert (eccb.cfg_blk.pcpu_base_addr[cpu] != NULL);

    cvmx_ringbuf_t  *cpu_buf = (cvmx_ringbuf_t *) eccb.cfg_blk.pcpu_base_addr[cpu];

    cpu_buf->pcpu_blk_info.size = size;
    cpu_buf->pcpu_blk_info.max_samples = ((size - sizeof(cvmx_cpu_event_block_t)) / sizeof(cvmx_sample_entry_t));
    cpu_buf->pcpu_blk_info.sample_count = 0;
    cpu_buf->pcpu_blk_info.sample_read = 0;
    cpu_buf->pcpu_blk_info.data = eccb.cfg_blk.pcpu_base_addr[cpu] + sizeof(cvmx_cpu_event_block_t) + PADBYTES;
    cpu_buf->pcpu_blk_info.head = cpu_buf->pcpu_blk_info.tail = \
       cpu_buf->pcpu_data = cpu_buf->pcpu_blk_info.data;
    cpu_buf->pcpu_blk_info.end = eccb.cfg_blk.pcpu_base_addr[cpu] + size;

    cvmx_atomic_set32(&read_percpu_block, 0);

    /*
     * Write per cpu mem base address info in to 'event config' named block,
     * This info is needed by oct-remote-profile to get Per cpu memory 
     * base address of each core of the named block.
     */
    pcpu_cfg_blk = (cvmx_config_block_t *) eccb.config_blk_base_addr;
    pcpu_cfg_blk->pcpu_base_addr[cpu] = eccb.cfg_blk.pcpu_base_addr[cpu];
}

/**
 * @INTERNAL
 * Retrieve the info from the 'event_config' named block.
 *
 * Here events value is read(as passed to oct-remote-profile) to reset perf 
 * counters on every Perf counter overflow.
 *
 */
static
void cvmx_read_config_blk(void)
{
    eccb.config_blk_base_addr = (char *)cvmx_get_memory_addr(EVENT_BUFFER_CONFIG_BLOCK);
    memcpy(&(eccb.cfg_blk.events), eccb.config_blk_base_addr + \
       offsetof(cvmx_config_block_t, events), sizeof(int64_t));

    cvmx_atomic_set32(&eccb.read_cfg_blk,1);
    PRINTF("cfg_blk.events=%lu, sample_count=%ld\n", eccb.cfg_blk.events, eccb.cfg_blk.sample_count);
}

/**
 * @INTERNAL
 * Add new sample to the buffer and increment the head pointer and 
 * global sample count(i.e sum total of samples collected on all cores) 
 *
 */
static
void cvmx_add_sample_to_buffer(void)
{
    uint32_t epc;
    int cpu = cvmx_get_core_num();
    CVMX_MF_COP0(epc, COP0_EPC);

    cvmx_ringbuf_t  *cpu_buf = (cvmx_ringbuf_t *) eccb.cfg_blk.pcpu_base_addr[cpu];

    /* 
     * head/tail pointer can be NULL, and this case arises when oct-remote-profile is
     * invoked afresh. To keep memory sane for current instance, we clear namedblock off 
     * previous data and this is accomplished by octeon_remote_write_mem from host.
     */
    if (cvmx_unlikely(!cpu_buf->pcpu_blk_info.head && !cpu_buf->pcpu_blk_info.end)) {
       /* Reread the event count as a different threshold val could be 
        * passed with profiler alongside --events flag */
        cvmx_read_config_blk();
        cvmx_init_pcpu_block(cpu, EVENT_PERCPU_BUFFER_SIZE);
    }

    /* In case of hitting end of buffer, reset head,data ptr to start */
    if (cpu_buf->pcpu_blk_info.head == cpu_buf->pcpu_blk_info.end)
        cpu_buf->pcpu_blk_info.head = cpu_buf->pcpu_blk_info.data = cpu_buf->pcpu_data; 

    /* Store the pc, respective core no.*/
    cvmx_sample_entry_t *sample = (cvmx_sample_entry_t *) cpu_buf->pcpu_blk_info.data;
    sample->pc = epc;
    sample->core = cpu;
  
    /* Update Per CPU stats */
    cpu_buf->pcpu_blk_info.sample_count++;
    cpu_buf->pcpu_blk_info.data += sizeof(cvmx_sample_entry_t);
    cpu_buf->pcpu_blk_info.head = cpu_buf->pcpu_blk_info.data;

    /* Increment the global sample count i.e sum total of samples on all cores*/
    cvmx_atomic_add64(&(pcpu_cfg_blk->sample_count), 1);

    PRINTF("the core%d:pc 0x%016lx, sample_count=%ld\n", cpu, sample->pc, cpu_buf->pcpu_blk_info.sample_count);
}

/**
 * @INTERNAL
 * Reset performance counters
 *
 * @param pf     The performance counter Number (0, 1)
 * @param events The threshold value for which interrupt has to be asserted
 */
static
void cvmx_reset_perf_counter(int pf, uint64_t events)
{
    uint64_t pfc;
    pfc = (1ull << 63) - events;
 	
    if (!pf) {
        CVMX_MT_COP0(pfc, COP0_PERFVALUE0);
    } else
        CVMX_MT_COP0(pfc, COP0_PERFVALUE1);
}

void cvmx_collect_sample(void)
{
    if (!eccb.read_cfg_blk)
        cvmx_read_config_blk();

    if (read_percpu_block)
        cvmx_init_pcpu_block(cvmx_get_core_num(), EVENT_PERCPU_BUFFER_SIZE);

    cvmx_add_sample_to_buffer();
    cvmx_reset_perf_counter(0, eccb.cfg_blk.events);
}
