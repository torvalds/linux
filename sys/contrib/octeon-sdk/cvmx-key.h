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
 * Interface to the on chip key memory. Key memory is
 * 8k on chip that is inaccessible from off chip. It can
 * also be cleared using an external hardware pin.
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */

#ifndef __CVMX_KEY_H__
#define __CVMX_KEY_H__

#ifdef	__cplusplus
extern "C" {
#endif

#define CVMX_KEY_MEM_SIZE 8192  /* Size in bytes */


/**
 * Read from KEY memory
 *
 * @param address Address (byte) in key memory to read
 *                0 <= address < CVMX_KEY_MEM_SIZE
 * @return Value from key memory
 */
static inline uint64_t cvmx_key_read(uint64_t address)
{
    cvmx_addr_t ptr;

    ptr.u64 = 0;
    ptr.sio.mem_region  = CVMX_IO_SEG;
    ptr.sio.is_io       = 1;
    ptr.sio.did         = CVMX_OCT_DID_KEY_RW;
    ptr.sio.offset      = address;

    return cvmx_read_csr(ptr.u64);
}


/**
 * Write to KEY memory
 *
 * @param address Address (byte) in key memory to write
 *                0 <= address < CVMX_KEY_MEM_SIZE
 * @param value   Value to write to key memory
 */
static inline void cvmx_key_write(uint64_t address, uint64_t value)
{
    cvmx_addr_t ptr;

    ptr.u64 = 0;
    ptr.sio.mem_region  = CVMX_IO_SEG;
    ptr.sio.is_io       = 1;
    ptr.sio.did         = CVMX_OCT_DID_KEY_RW;
    ptr.sio.offset      = address;

    cvmx_write_io(ptr.u64, value);
}


/* CSR typedefs have been moved to cvmx-key-defs.h */

#ifdef	__cplusplus
}
#endif

#endif /*  __CVMX_KEY_H__ */
