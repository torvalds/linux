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
 *************************license end**************************************/

/**
 * @file
 *
 * Header file for the event Profiler.
 *
 */

#ifndef __CVMX_PROFILER_H__
#define __CVMX_PROFILER_H__

#ifdef  __cplusplus
extern "C" {
#endif

#define EVENT_PERCPU_BUFFER_SIZE	8192
#define PADBYTES			24

#define	EVENT_BUFFER_BLOCK		"event_block"
#define EVENT_BUFFER_SIZE		EVENT_PERCPU_BUFFER_SIZE * (cvmx_octeon_num_cores() + 1)

#define EVENT_BUFFER_CONFIG_BLOCK	"event_config_block"
#define EBC_BLOCK_SIZE			256 

typedef struct {
    int core; 
    uint32_t pc;
} cvmx_sample_entry_t;

typedef struct cpu_event_block {
    int size;
    int sample_read; 
    int64_t max_samples;
    int64_t sample_count;
    char *head;
    char *tail;
    char *end;
    char *data;
} cvmx_cpu_event_block_t;

typedef struct {
    cvmx_cpu_event_block_t pcpu_blk_info;
    char *pcpu_data;
} cvmx_ringbuf_t;

typedef struct config_block {
    int64_t sample_count;
    uint64_t events;
    char *pcpu_base_addr[CVMX_MAX_CORES];
} cvmx_config_block_t;

typedef struct event_counter_control_block {
    int32_t read_cfg_blk;
    char *config_blk_base_addr;
    cvmx_config_block_t cfg_blk;        
} event_counter_control_block_t;

extern void cvmx_update_perfcnt_irq(void);
extern void cvmx_collect_sample(void);

#ifdef  __cplusplus
}
#endif

#endif	/*  __CVMX_PROFILER_H__ */
