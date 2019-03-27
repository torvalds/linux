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
 * Packet buffer defines.
 *
 * <hr>$Revision: 70030 $<hr>
 *
 */

#ifndef __CVMX_PACKET_H__
#define __CVMX_PACKET_H__

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * This structure defines a buffer pointer on Octeon
 */
union cvmx_buf_ptr {
    void*           ptr;
    uint64_t        u64;
    struct
    {
        uint64_t    i    : 1; /**< if set, invert the "free" pick of the overall packet. HW always sets this bit to 0 on inbound packet */
        uint64_t    back : 4; /**< Indicates the amount to back up to get to the buffer start in cache lines. In most cases
                                this is less than one complete cache line, so the value is zero */
        uint64_t    pool : 3; /**< The pool that the buffer came from / goes to */
        uint64_t    size :16; /**< The size of the segment pointed to by addr (in bytes) */
        uint64_t    addr :40; /**< Pointer to the first byte of the data, NOT buffer */
    } s;
};

typedef union cvmx_buf_ptr cvmx_buf_ptr_t;

#ifdef	__cplusplus
}
#endif

#endif /*  __CVMX_PACKET_H__ */

