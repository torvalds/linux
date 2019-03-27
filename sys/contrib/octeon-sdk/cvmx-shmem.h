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


#ifndef __CVMX_SHMEM_H__
#define __CVMX_SHMEM_H__

/**
 * @file
 *
 * cvmx-shmem provides APIs for setting up shared memory between Linux
 * and simple executive applications.
 *
 * <hr>$Revision: 41586 $<hr>
 */


#ifdef  __cplusplus
extern "C" {
#endif

#include "cvmx-spinlock.h"

#define CVMX_SHMEM_NUM_DSCPTR       8
#define CVMX_SHMEM_DSCPTR_NAME         "SMDR"

#define CVMX_SHMEM_O_RDONLY         0x00
#define CVMX_SHMEM_O_WRONLY         0x01
#define CVMX_SHMEM_O_RDWR           0x02
#define CVMX_SHMEM_O_CREAT          0x04

#define CVMX_SHMEM_MAP_PROT_READ    0x01
#define CVMX_SHMEM_MAP_PROT_WRITE   0x02
#define CVMX_SHMEM_MAP_EXEC         0x04

#define CVMX_SHMEM_OWNER_NONE       0xff

#define CVMX_SHMEM_VADDR64_START    0x500000000ULL
#define CVMX_SHMEM_VADDR64_END      0x600000000ULL

#define CVMX_SHMEM_VADDR32_START    0x10000000
#define CVMX_SHMEM_VADDR32_END      0x18000000

struct cvmx_shmem_dscptr {
    cvmx_spinlock_t lock;
    uint64_t owner:           8;
    uint64_t is_named_block:  1;
    uint64_t p_wronly:        1;
    uint64_t p_rdwr:          1;
    int32_t use_count;	      /* must use atomic operation to maintain count */
    const char *name;
    void *vaddr;
    uint64_t paddr;
    uint32_t size;
    uint64_t alignment;
};

struct cvmx_shmem_smdr {
    cvmx_spinlock_t lock;
    struct cvmx_shmem_dscptr  shmd[CVMX_SHMEM_NUM_DSCPTR];
    void *break64;    /* Keep track of unused 64 bit virtual address space */
};


struct cvmx_shmem_smdr *cvmx_shmem_init(void);

/**
 *  Create a piece memory out of named block
 *
 *  @param name Named block name
 *  @param flag create flag
 */
struct cvmx_shmem_dscptr *cvmx_shmem_named_block_open(char *name, uint32_t size, int oflag);

/**
 *  Update TLB mapping based on the descriptor
 */
void*  cvmx_shmem_map(struct cvmx_shmem_dscptr *desc, int pflag);

/**
 *  Remove the TLB mapping created for the descriptor
 */
void   cvmx_shmem_unmap(struct cvmx_shmem_dscptr *desc);


/**
 *  Close the share memory,
 *
 *  @Param remove  Remove the named block if it is created by the application
 */
int cvmx_shmem_close(struct cvmx_shmem_dscptr *desc, int remove);

/**
 * Debug function, dump all SMDR descriptors
 */
void cvmx_shmem_show(void);


#ifdef  __cplusplus
}
#endif

#endif
