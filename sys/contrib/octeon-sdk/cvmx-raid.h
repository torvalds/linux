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
 * Interface to RAID block. This is not available on all chips.
 *
 * <hr>$Revision: 70030 $<hr>
 */

#ifndef __CVMX_RAID_H__
#define __CVMX_RAID_H__

#ifdef CVMX_BUILD_FOR_LINUX_KERNEL
#include <asm/octeon/cvmx-rad-defs.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * This structure defines the type of command words the RAID block
 * will accept.
 */
typedef union
{
    uint64_t u64;
    struct
    {
        uint64_t    reserved_37_63  : 27;   /**< Must be zero */
        uint64_t    q_cmp           :  1;   /**< Indicates whether the Q pipe is in normal mode (CWORD[Q_CMP]=0) or in non-zero
                                                byte detect mode (CWORD[Q_CMP]=1).
                                                In non-zero byte detect mode, the Q OWORD[PTR] result is the non-zero detect
                                                result, which indicates the position of the first non-zero byte in the pipe result bytes.
                                                CWORD[Q_CMP] must not be set when CWORD[QOUT]=0, and must not be set
                                                when CWORD[Q_XOR] is set. */
        uint64_t    p_cmp           :  1;   /**< Indicates whether the P pipe is in normal mode (CWORD[P_CMP]=0) or in non-zero
                                                byte detect mode (CWORD[P_CMP]=1).
                                                In non-zero byte detect mode, the P OWORD[PTR] result is the non-zero detect
                                                result, which indicates the position of the first non-zero byte in the pipe result bytes.
                                                CWORD[P_CMP] must not be set when CWORD[POUT]=0, and must not be set
                                                when CWORD[P_XOR] is set. */
        uint64_t    q_xor           :  1;   /**< Indicates whether the Q output buffer bytes are the normal Q pipe result or the
                                                normal Q pipe result exclusive-OR'ed with the P pipe result.
                                                When CWORD[Q_XOR]=0 (and CWORD[Q_CMP]=0), the Q output buffer bytes are
                                                the normal Q pipe result, which does not include the P pipe result in any way.
                                                When CWORD[Q_XOR]=1, the Q output buffer bytes are the normal Q pipe result
                                                exclusive-OR'ed with the P pipe result, as if the P pipe result were another Q IWORD
                                                for the Q pipe with QMULT=1.
                                                CWORD[Q_XOR] must not be set unless both CWORD[POUT,QOUT] are set, and
                                                must not be set when CWORD[Q_CMP] is set. */
        uint64_t    p_xor           :  1;   /**< Indicates whether the P output buffer bytes are the normal P pipe result or the
                                                normal P pipe result exclusive-OR'ed with the Q pipe result.
                                                When CWORD[P_XOR]=0 (and CWORD[P_CMP]=0), the P output buffer bytes are
                                                the normal P pipe result, which does not include the Q pipe result in any way.
                                                When CWORD[P_XOR]=1, the P output buffer bytes are the normal P pipe result
                                                exclusive-OR'ed with the Q pipe result, as if the Q pipe result were another P
                                                IWORD for the P pipe.
                                                CWORD[P_XOR] must not be set unless both CWORD[POUT,QOUT] are set, and
                                                must not be set when CWORD[P_CMP] is set. */
        uint64_t    wqe             :  1;   /**< Indicates whether RAD submits a work queue entry or writes an L2/DRAM byte to
                                                zero after completing the instruction.
                                                When CWORD[WQE] is set and RESP[PTR]!=0, RAD adds the work queue entry
                                                indicated by RESP[PTR] to the selected POW input queue after completing the
                                                instruction.
                                                When CWORD[WQE] is clear and RESP[PTR]!=0, RAD writes the L2/DRAM byte
                                                indicated by RESP[PTR] to zero after completing the instruction. */
        uint64_t    qout            :  1;   /**< Indicates whether the Q pipe is used by this instruction.
                                                If CWORD[QOUT] is set, IWORD[QEN] must be set for at least one IWORD.
                                                At least one of CWORD[QOUT,POUT] must be set. */
        uint64_t    pout            :  1;   /**< Indicates whether the P pipe is used by this instruction.
                                                If CWORD[POUT] is set, IWORD[PEN] must be set for at least one IWORD.
                                                At least one of CWORD[QOUT,POUT] must be set. */
        uint64_t    iword           :  6;   /**< Indicates the number of input buffers used.
                                                1 <= CWORD[IWORD] <= 32. */
        uint64_t    size            : 24;   /**< Indicates the size in bytes of all input buffers. When CWORD[Q_CMP,P_CMP]=0,
                                                also indicates the size of the Q/P output buffers.
                                                CWORD[SIZE] must be a multiple of 8B (i.e. <2:0> must be zero). */
    } cword;
    struct
    {
        uint64_t    reserved_58_63  :  6;   /**< Must be zero */
        uint64_t    fw              :  1;   /**< When set, indicates that RAD can modify any byte in any (128B) cache line touched
                                                by L2/DRAM addresses OWORD[PTR] through OWORD[PTR]+CWORD[SIZE]-1.
                                                Setting OWORD[FW] can improve hardware performance, as some DRAM loads can
                                                be avoided on L2 cache misses. The Q OWORD[FW] must not be set when
                                                CWORD[Q_CMP] is set, and the P OWORD[FW] must not be set when
                                                CWORD[P_CMP] is set. */
        uint64_t    nc              :  1;   /**< When set, indicates that RAD should not allocate L2 cache space for the P/Q data on
                                                L2 cache misses.
                                                OWORD[NC] should typically be clear, though setting OWORD[NC] can improve
                                                performance in some circumstances, as the L2 cache will not be polluted by P/Q data.
                                                The Q OWORD[NC] must not be set when CWORD[Q_CMP] is set, and the P
                                                OWORD[NC] must not be set when CWORD[P_CMP] is set. */
        uint64_t    reserved_40_55  : 16;   /**< Must be zero */
        uint64_t    addr            : 40;   /**< When CWORD[P_CMP,Q_CMP]=0, OWORD[PTR] indicates the starting address of
                                                the L2/DRAM buffer that will receive the P/Q data. In the non-compare mode, the
                                                output buffer receives all of the output buffer bytes.
                                                When CWORD[P_CMP,Q_CMP]=1, the corresponding P/Q pipe is in compare mode,
                                                and the only output of the pipe is the non-zero detect result. In this case,
                                                OWORD[PTR] indicates the 8-byte location of the non-zero detect result. */
    } oword;
    struct
    {
        uint64_t    reserved_57_63  :  7;   /**< Must be zero */
        uint64_t    nc              :  1;   /**< When set, indicates that RAD should not allocate L2 cache space for this input buffer
                                                data on L2 cache misses.
                                                Setting IWORD[NC] may improve performance in some circumstances, as the L2
                                                cache may not be polluted with input buffer data. */
        uint64_t    reserved_50_55  :  6;   /**< Must be zero */
        uint64_t    qen             :  1;   /**< Indicates that this input buffer data should participate in the Q pipe result.
                                                The Q pipe hardware multiplies each participating input byte by IWORD[QMULT]
                                                before accumulating them by exclusive-OR'ing.
                                                IWORD[QEN] must not be set when CWORD[QOUT] is not set.
                                                If CWORD[QOUT] is set, IWORD[QEN] must be set for at least one IWORD. */
        uint64_t    pen             :  1;   /**< Indicates that this input buffer data should participate in the P pipe result.
                                                The P pipe hardware accumulates each participating input byte by bit-wise
                                                exclusive-OR'ing it.
                                                IWORD[PEN] must not be set when CWORD[POUT] is not set.
                                                If CWORD[POUT] is set, IWORD[PEN] must be set for at least one IWORD. */
        uint64_t    qmult           :  8;   /**< The Q pipe multiplier for the input buffer. Section 26.1 above describes the GF(28)
                                                multiplication algorithm.
                                                IWORD[QMULT] must be zero when IWORD[QEN] is not set.
                                                IWORD[QMULT] must not be zero when IWORD[QEN] is set.
                                                When IWORD[QMULT] is 1, the multiplication simplifies to the identity function,
                                                and the Q pipe performs the same XOR function as the P pipe. */
        uint64_t    addr            : 40;   /**< The starting address of the input buffer in L2/DRAM.
                                                IWORD[PTR] must be naturally-aligned on an 8 byte boundary (i.e. <2:0> must be
                                                zero). */
    } iword;
} cvmx_raid_word_t;

/**
 * Initialize the RAID block
 *
 * @param polynomial Coefficients for the RAID polynomial
 *
 * @return Zero on success, negative on failure
 */
int cvmx_raid_initialize(cvmx_rad_reg_polynomial_t polynomial);

/**
 * Shutdown the RAID block. RAID must be idle when
 * this function is called.
 *
 * @return Zero on success, negative on failure
 */
int cvmx_raid_shutdown(void);

/**
 * Submit a command to the RAID block
 *
 * @param num_words Number of command words to submit
 * @param words     Command words
 *
 * @return Zero on success, negative on failure
 */
int cvmx_raid_submit(int num_words, cvmx_raid_word_t words[]);

#ifdef	__cplusplus
}
#endif

#endif // __CVMX_CMD_QUEUE_H__
