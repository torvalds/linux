/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _SCU_COMPLETION_CODES_HEADER_
#define _SCU_COMPLETION_CODES_HEADER_

/**
 * @file
 *
 * @brief This file contains the constants and macros for the SCU hardware
 *        completion codes.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define SCU_COMPLETION_TYPE_SHIFT      28
#define SCU_COMPLETION_TYPE_MASK       0x70000000

/**
 * This macro constructs an SCU completion type
 */
#define SCU_COMPLETION_TYPE(type) \
   ((U32)(type) << SCU_COMPLETION_TYPE_SHIFT)

/**
 * These macros contain the SCU completion types
 *
 * @name SCU_COMPLETION_TYPE
 */
/*@}*/
#define SCU_COMPLETION_TYPE_TASK       SCU_COMPLETION_TYPE(0)
#define SCU_COMPLETION_TYPE_SDMA       SCU_COMPLETION_TYPE(1)
#define SCU_COMPLETION_TYPE_UFI        SCU_COMPLETION_TYPE(2)
#define SCU_COMPLETION_TYPE_EVENT      SCU_COMPLETION_TYPE(3)
#define SCU_COMPLETION_TYPE_NOTIFY     SCU_COMPLETION_TYPE(4)
/*@}*/

/**
 * These constants provide the shift and mask values for the various parts of
 * an SCU completion code.
 */
#define SCU_COMPLETION_STATUS_MASK       0x0FFC0000
#define SCU_COMPLETION_TL_STATUS_MASK    0x0FC00000
#define SCU_COMPLETION_TL_STATUS_SHIFT   22
#define SCU_COMPLETION_SDMA_STATUS_MASK  0x003C0000
#define SCU_COMPLETION_PEG_MASK          0x00010000
#define SCU_COMPLETION_PORT_MASK         0x00007000
#define SCU_COMPLETION_PE_MASK           SCU_COMPLETION_PORT_MASK
#define SCU_COMPLETION_PE_SHIFT          12
#define SCU_COMPLETION_INDEX_MASK        0x00000FFF

/**
 * This macro returns the SCU completion type.
 */
#define SCU_GET_COMPLETION_TYPE(completion_code) \
   ((completion_code) & SCU_COMPLETION_TYPE_MASK)

/**
 * This macro returns the SCU completion status.
 */
#define SCU_GET_COMPLETION_STATUS(completion_code) \
   ((completion_code) & SCU_COMPLETION_STATUS_MASK)

/**
 * This macro returns the transport layer completion status.
 */
#define SCU_GET_COMPLETION_TL_STATUS(completion_code) \
   ((completion_code) & SCU_COMPLETION_TL_STATUS_MASK)

/**
 * This macro takes a completion code and performs the shift and mask
 * operations to turn it into a completion code that can be compared to a
 * SCU_GET_COMPLETION_TL_STATUS.
 */
#define SCU_MAKE_COMPLETION_STATUS(completion_code) \
   ((U32)(completion_code) << SCU_COMPLETION_TL_STATUS_SHIFT)

/**
 * This macro takes a SCU_GET_COMPLETION_TL_STATUS and normalizes it for a
 * return code.
 */
#define SCU_NORMALIZE_COMPLETION_STATUS(completion_code) \
   ( \
      ((U32)((completion_code) & SCU_COMPLETION_TL_STATUS_MASK)) \
   >> SCU_COMPLETION_TL_STATUS_SHIFT \
   )

/**
 * This macro returns the SDMA completion status.
 */
#define SCU_GET_COMPLETION_SDMA_STATUS(completion_code) \
   ((completion_code) & SCU_COMPLETION_SDMA_STATUS_MASK)

/**
 * This macro returns the Protocol Engine Group from the completion code.
 */
#define SCU_GET_COMPLETION_PEG(completion_code) \
   ((completion_code) & SCU_COMPLETION_PEG_MASK)

/**
 * This macro reuturns the logical port index from the completion code.
 */
#define SCU_GET_COMPLETION_PORT(completion_code) \
   ((completion_code) & SCU_COMPLETION_PORT_MASK)

/**
 * This macro returns the PE index from the completion code.
 */
#define SCU_GET_PROTOCOL_ENGINE_INDEX(completion_code) \
   (((U32)((completion_code) & SCU_COMPLETION_PE_MASK)) >> SCU_COMPLETION_PE_SHIFT)

/**
 * This macro returns the index of the completion which is either a TCi or an
 * RNi depending on the completion type.
 */
#define SCU_GET_COMPLETION_INDEX(completion_code) \
   ((completion_code) & SCU_COMPLETION_INDEX_MASK)

#define SCU_UNSOLICITED_FRAME_MASK     0x0FFF0000
#define SCU_UNSOLICITED_FRAME_SHIFT    16

/**
 * This macro returns a normalized frame index from an unsolicited frame
 * completion.
 */
#define SCU_GET_FRAME_INDEX(completion_code) \
   ( \
        ((U32)((completion_code) & SCU_UNSOLICITED_FRAME_MASK)) \
     >> SCU_UNSOLICITED_FRAME_SHIFT \
   )

#define SCU_UNSOLICITED_FRAME_ERROR_MASK  0x00008000

/**
 * This macro returns a zero (0) value if there is no frame error otherwise
 * it returns non-zero (!0).
 */
#define SCU_GET_FRAME_ERROR(completion_code) \
   ((completion_code) & SCU_UNSOLICITED_FRAME_ERROR_MASK)

/**
 * These constants represent normalized completion codes which must be shifted
 * 18 bits to match it with the hardware completion code. In a 16-bit compiler,
 * immediate constants are 16-bit values (the size of an int). If we shift those
 * by 18 bits, we completely lose the value. To ensure the value is a 32-bit
 * value like we want, each immediate value must be cast to a U32.
 */
#define SCU_TASK_DONE_GOOD                                  ((U32)0x00)
#define SCU_TASK_DONE_CRC_ERR                               ((U32)0x14)
#define SCU_TASK_DONE_CHECK_RESPONSE                        ((U32)0x14)
#define SCU_TASK_DONE_GEN_RESPONSE                          ((U32)0x15)
#define SCU_TASK_DONE_NAK_CMD_ERR                           ((U32)0x16)
#define SCU_TASK_DONE_CMD_LL_R_ERR                          ((U32)0x16)
#define SCU_TASK_DONE_LL_R_ERR                              ((U32)0x17)
#define SCU_TASK_DONE_ACK_NAK_TO                            ((U32)0x17)
#define SCU_TASK_DONE_LL_PERR                               ((U32)0x18)
#define SCU_TASK_DONE_LL_SY_TERM                            ((U32)0x19)
#define SCU_TASK_DONE_NAK_ERR                               ((U32)0x19)
#define SCU_TASK_DONE_LL_LF_TERM                            ((U32)0x1A)
#define SCU_TASK_DONE_DATA_LEN_ERR                          ((U32)0x1A)
#define SCU_TASK_DONE_LL_CL_TERM                            ((U32)0x1B)
#define SCU_TASK_DONE_LL_ABORT_ERR                          ((U32)0x1B)
#define SCU_TASK_DONE_SEQ_INV_TYPE                          ((U32)0x1C)
#define SCU_TASK_DONE_UNEXP_XR                              ((U32)0x1C)
#define SCU_TASK_DONE_INV_FIS_TYPE                          ((U32)0x1D)
#define SCU_TASK_DONE_XR_IU_LEN_ERR                         ((U32)0x1D)
#define SCU_TASK_DONE_INV_FIS_LEN                           ((U32)0x1E)
#define SCU_TASK_DONE_XR_WD_LEN                             ((U32)0x1E)
#define SCU_TASK_DONE_SDMA_ERR                              ((U32)0x1F)
#define SCU_TASK_DONE_OFFSET_ERR                            ((U32)0x20)
#define SCU_TASK_DONE_MAX_PLD_ERR                           ((U32)0x21)
#define SCU_TASK_DONE_EXCESS_DATA                           ((U32)0x22)
#define SCU_TASK_DONE_LF_ERR                                ((U32)0x23)
#define SCU_TASK_DONE_UNEXP_FIS                             ((U32)0x24)
#define SCU_TASK_DONE_UNEXP_RESP                            ((U32)0x24)
#define SCU_TASK_DONE_EARLY_RESP                            ((U32)0x25)
#define SCU_TASK_DONE_SMP_RESP_TO_ERR                       ((U32)0x26)
#define SCU_TASK_DONE_DMASETUP_DIRERR                       ((U32)0x27)
#define SCU_TASK_DONE_SMP_UFI_ERR                           ((U32)0x27)
#define SCU_TASK_DONE_XFERCNT_ERR                           ((U32)0x28)
#define SCU_TASK_DONE_SMP_FRM_TYPE_ERR                      ((U32)0x28)
#define SCU_TASK_DONE_SMP_LL_RX_ERR                         ((U32)0x29)
#define SCU_TASK_DONE_RESP_LEN_ERR                          ((U32)0x2A)
#define SCU_TASK_DONE_UNEXP_DATA                            ((U32)0x2B)
#define SCU_TASK_DONE_OPEN_FAIL                             ((U32)0x2C)
#define SCU_TASK_DONE_UNEXP_SDBFIS                          ((U32)0x2D)
#define SCU_TASK_DONE_REG_ERR                               ((U32)0x2E)
#define SCU_TASK_DONE_SDB_ERR                               ((U32)0x2F)
#define SCU_TASK_DONE_TASK_ABORT                            ((U32)0x30)
#if defined(PBG_HBA_BETA_BUILD)
#define SCU_TASK_DONE_CMD_SDMA_ERR                          ((U32)0x32)
#define SCU_TASK_DONE_CMD_LL_ABORT_ERR                      ((U32)0x33)
#endif
#define SCU_TASK_OPEN_REJECT_WRONG_DESTINATION              ((U32)0x34)
#define SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_1             ((U32)0x35)
#define SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_2             ((U32)0x36)
#define SCU_TASK_OPEN_REJECT_RESERVED_ABANDON_3             ((U32)0x37)
#define SCU_TASK_OPEN_REJECT_BAD_DESTINATION                ((U32)0x38)
#define SCU_TASK_OPEN_REJECT_ZONE_VIOLATION                 ((U32)0x39)
#define SCU_TASK_DONE_VIIT_ENTRY_NV                         ((U32)0x3A)
#define SCU_TASK_DONE_IIT_ENTRY_NV                          ((U32)0x3B)
#define SCU_TASK_DONE_RNCNV_OUTBOUND                        ((U32)0x3C)
#define SCU_TASK_OPEN_REJECT_STP_RESOURCES_BUSY             ((U32)0x3D)
#define SCU_TASK_OPEN_REJECT_PROTOCOL_NOT_SUPPORTED         ((U32)0x3E)
#define SCU_TASK_OPEN_REJECT_CONNECTION_RATE_NOT_SUPPORTED  ((U32)0x3F)

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCU_COMPLETION_CODES_HEADER_
