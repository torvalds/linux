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
#ifndef _SCU_TASK_CONTEXT_H_
#define _SCU_TASK_CONTEXT_H_

/**
 * @file
 *
 * @brief This file contains the structures and constants for the SCU hardware
 *        task context.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>

/**
 * @enum SCU_SSP_TASK_TYPE
 *
 * @brief This enumberation defines the various SSP task types the SCU
 *        hardware will accept.
 *
 * The definition for the various task types the SCU hardware will accept can
 * be found in the DS specification.
 */
typedef enum
{
    SCU_TASK_TYPE_IOREAD,        ///< IO READ direction or no direction
    SCU_TASK_TYPE_IOWRITE,       ///< IO Write direction
    SCU_TASK_TYPE_SMP_REQUEST,   ///< SMP Request type
    SCU_TASK_TYPE_RESPONSE,      ///< Driver generated response frame (targt mode)
    SCU_TASK_TYPE_RAW_FRAME,     ///< Raw frame request type
    SCU_TASK_TYPE_PRIMITIVE      ///< Request for a primitive to be transmitted
} SCU_SSP_TASK_TYPE;

/**
 * @enum SCU_SATA_TASK_TYPE
 *
 * @brief This enumeration defines the various SATA task types the SCU
 *        hardware will accept.
 *
 * The definition for the various task types the SCU hardware will accept can
 * be found in the DS specification.
 */
typedef enum
{
    SCU_TASK_TYPE_DMA_IN,           ///< Read request
    SCU_TASK_TYPE_FPDMAQ_READ,      ///< NCQ read request
    SCU_TASK_TYPE_PACKET_DMA_IN,    ///< Packet read request
    SCU_TASK_TYPE_SATA_RAW_FRAME,   ///< Raw frame request
    RESERVED_4,
    RESERVED_5,
    RESERVED_6,
    RESERVED_7,
    SCU_TASK_TYPE_DMA_OUT,          ///< Write request
    SCU_TASK_TYPE_FPDMAQ_WRITE,     ///< NCQ write Request
    SCU_TASK_TYPE_PACKET_DMA_OUT    ///< Packet write request
} SCU_SATA_TASK_TYPE;


/**
 * @name SCU_CONTEXT_TYPE
 */
/*@{*/
#define SCU_TASK_CONTEXT_TYPE  0
#define SCU_RNC_CONTEXT_TYPE   1
/*@}*/

/**
 * @name SCU_TASK_CONTEXT_VALIDITY
 */
/*@{*/
#define SCU_TASK_CONTEXT_INVALID          0
#define SCU_TASK_CONTEXT_VALID            1
/*@}*/

/**
 * @name SCU_COMMAND_CODE
 */
/*@{*/
#define SCU_COMMAND_CODE_INITIATOR_NEW_TASK   0
#define SCU_COMMAND_CODE_ACTIVE_TASK          1
#define SCU_COMMAND_CODE_PRIMITIVE_SEQ_TASK   2
#define SCU_COMMAND_CODE_TARGET_RAW_FRAMES    3
/*@}*/

/**
 * @name SCU_TASK_PRIORITY
 */
/*@{*/
/**
 * This priority is used when there is no priority request for this request.
 */
#define SCU_TASK_PRIORITY_NORMAL          0

/**
 * This priority indicates that the task should be scheduled to the head
 * of the queue.  The task will NOT be executed if the TX is suspended for
 * the remote node.
 */
#define SCU_TASK_PRIORITY_HEAD_OF_Q       1

/**
 * This priority indicates that the task will be executed before all
 * SCU_TASK_PRIORITY_NORMAL and SCU_TASK_PRIORITY_HEAD_OF_Q tasks.
 * The task WILL be executed if the TX is suspended for the remote node.
 */
#define SCU_TASK_PRIORITY_HIGH            2

/**
 * This task priority is reserved and should not be used.
 */
#define SCU_TASK_PRIORITY_RESERVED        3
/*@}*/

#define SCU_TASK_INITIATOR_MODE           1
#define SCU_TASK_TARGET_MODE              0

#define SCU_TASK_REGULAR                  0
#define SCU_TASK_ABORTED                  1

//direction bit definition
/**
 * @name SATA_DIRECTION
 */
/*@{*/
#define SCU_SATA_WRITE_DATA_DIRECTION     0
#define SCU_SATA_READ_DATA_DIRECTION      1
/*@}*/

/**
 * @name SCU_COMMAND_CONTEXT_MACROS
 *
 * These macros provide the mask and shift operations to construct the various
 * SCU commands
 */
/*@{*/
#define SCU_CONTEXT_COMMAND_REQUEST_TYPE_SHIFT           21UL
#define SCU_CONTEXT_COMMAND_REQUEST_TYPE_MASK            0x00E00000UL
#define scu_get_command_request_type(x) \
               ((x) & SCU_CONTEXT_COMMAND_REQUEST_TYPE_MASK)

#define SCU_CONTEXT_COMMAND_REQUEST_SUBTYPE_SHIFT        18UL
#define SCU_CONTEXT_COMMAND_REQUEST_SUBTYPE_MASK         0x001C0000UL
#define scu_get_command_request_subtype(x) \
               ((x) & SCU_CONTEXT_COMMAND_REQUEST_SUBTYPE_MASK)

#define SCU_CONTEXT_COMMAND_REQUEST_FULLTYPE_MASK        \
   (                                                     \
       SCU_CONTEXT_COMMAND_REQUEST_TYPE_MASK             \
     | SCU_CONTEXT_COMMAND_REQUEST_SUBTYPE_MASK          \
   )
#define scu_get_command_request_full_type(x) \
               ((x) & SCU_CONTEXT_COMMAND_REQUEST_FULLTYPE_MASK)

#define SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_SHIFT  16UL
#define SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_MASK   0x00010000UL
#define scu_get_command_protocl_engine_group(x) \
               ((x) & SCU_CONTEXT_COMMAND_PROTOCOL_ENGINE_GROUP_MASK)

#define SCU_CONTEXT_COMMAND_LOGICAL_PORT_SHIFT           12UL
#define SCU_CONTEXT_COMMAND_LOGICAL_PORT_MASK            0x00007000UL
#define scu_get_command_reqeust_logical_port(x) \
               ((x) & SCU_CONTEXT_COMMAND_LOGICAL_PORT_MASK)


#define MAKE_SCU_CONTEXT_COMMAND_TYPE(type) \
   ((U32)(type) << SCU_CONTEXT_COMMAND_REQUEST_TYPE_SHIFT)
/*@}*/

/**
 * @name SCU_COMMAND_TYPES
 *
 * These constants provide the grouping of the different SCU command types.
 */
/*@{*/
#define SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC    MAKE_SCU_CONTEXT_COMMAND_TYPE(0UL)
#define SCU_CONTEXT_COMMAND_REQUEST_TYPE_DUMP_TC    MAKE_SCU_CONTEXT_COMMAND_TYPE(1UL)
#define SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_RNC   MAKE_SCU_CONTEXT_COMMAND_TYPE(2UL)
#define SCU_CONTEXT_COMMAND_REQUEST_TYPE_DUMP_RNC   MAKE_SCU_CONTEXT_COMMAND_TYPE(3UL)
#define SCU_CONTEXT_COMMAND_REQUEST_TYPE_OTHER_RNC  MAKE_SCU_CONTEXT_COMMAND_TYPE(6UL)
/*@}*/

#define MAKE_SCU_CONTEXT_COMMAND_REQUEST(type, command) \
   ((type) | (((U32)(command)) << SCU_CONTEXT_COMMAND_REQUEST_SUBTYPE_SHIFT))

/**
 * @name SCU_REQUEST_TYPES
 *
 * These constants are the various request types that can be posted to the SCU
 * hardware.
 */
/*@{*/
#define SCU_CONTEXT_COMMAND_REQUST_POST_TC \
   (MAKE_SCU_CONTEXT_COMMAND_REQUEST(SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC, 0))

#define SCU_CONTEXT_COMMAND_REQUEST_POST_TC_ABORT \
   (MAKE_SCU_CONTEXT_COMMAND_REQUEST(SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_TC, 1))

#define SCU_CONTEXT_COMMAND_REQUST_DUMP_TC \
   (MAKE_SCU_CONTEXT_COMMAND_REQUEST(SCU_CONTEXT_COMMAND_REQUEST_TYPE_DUMP_TC, 0))

#define SCU_CONTEXT_COMMAND_POST_RNC_32 \
   (MAKE_SCU_CONTEXT_COMMAND_REQUEST(SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_RNC, 0))

#define SCU_CONTEXT_COMMAND_POST_RNC_96 \
   (MAKE_SCU_CONTEXT_COMMAND_REQUEST(SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_RNC, 1))

#define SCU_CONTEXT_COMMAND_POST_RNC_INVALIDATE \
   (MAKE_SCU_CONTEXT_COMMAND_REQUEST(SCU_CONTEXT_COMMAND_REQUEST_TYPE_POST_RNC, 2))

#define SCU_CONTEXT_COMMAND_DUMP_RNC_32 \
   (MAKE_SCU_CONTEXT_COMMAND_REQUEST(SCU_CONTEXT_COMMAND_REQUEST_TYPE_DUMP_RNC, 0))

#define SCU_CONTEXT_COMMAND_DUMP_RNC_96 \
   (MAKE_SCU_CONTEXT_COMMAND_REQUEST(SCU_CONTEXT_COMMAND_REQUEST_TYPE_DUMP_RNC, 1))

#define SCU_CONTEXT_COMMAND_POST_RNC_SUSPEND_TX \
   (MAKE_SCU_CONTEXT_COMMAND_REQUEST(SCU_CONTEXT_COMMAND_REQUEST_TYPE_OTHER_RNC, 0))

#define SCU_CONTEXT_COMMAND_POST_RNC_SUSPEND_TX_RX \
   (MAKE_SCU_CONTEXT_COMMAND_REQUEST(SCU_CONTEXT_COMMAND_REQUEST_TYPE_OTHER_RNC, 1))

#define SCU_CONTEXT_COMMAND_POST_RNC_RESUME \
   (MAKE_SCU_CONTEXT_COMMAND_REQUEST(SCU_CONTEXT_COMMAND_REQUEST_TYPE_OTHER_RNC, 2))

#define SCU_CONTEXT_IT_NEXUS_LOSS_TIMER_ENABLE \
   (MAKE_SCU_CONTEXT_COMMAND_REQUEST(SCU_CONTEXT_COMMAND_REQUEST_TYPE_OTHER_RNC, 3))

#define SCU_CONTEXT_IT_NEXUS_LOSS_TIMER_DISABLE \
   (MAKE_SCU_CONTEXT_COMMAND_REQUEST(SCU_CONTEXT_COMMAND_REQUEST_TYPE_OTHER_RNC, 4))
/*@}*/

/**
 * @name SCU_TASK_CONTEXT_PROTOCOL
 * SCU Task context protocol types this is uesd to program the SCU Task
 * context protocol field in word 0x00.
 */
/*@{*/
#define SCU_TASK_CONTEXT_PROTOCOL_SMP    0x00
#define SCU_TASK_CONTEXT_PROTOCOL_SSP    0x01
#define SCU_TASK_CONTEXT_PROTOCOL_STP    0x02
#define SCU_TASK_CONTEXT_PROTOCOL_NONE   0x07
/*@}*/

/**
 * @struct SSP_TASK_CONTEXT
 *
 * @brief This is the SCU hardware definition for an SSP request.
 */
struct SSP_TASK_CONTEXT
{
   // OFFSET 0x18
   U32  reserved00              : 24;
   U32  frame_type              : 8;

   // OFFSET 0x1C
   U32  reserved01;

   // OFFSET 0x20
   U32  fill_bytes              : 2;
   U32  reserved02              : 6;
   U32  changing_data_pointer   : 1;
   U32  retransmit              : 1;
   U32  retry_data_frame        : 1;
   U32  tlr_control             : 2;
   U32  reserved03              : 19;

   // OFFSET 0x24
   U32  uiRsvd4;

   // OFFSET 0x28
   U32 target_port_transfer_tag : 16;
   U32 tag                      : 16;

   // OFFSET 0x2C
   U32 data_offset;
};

/**
 * @struct STP_TASK_CONTEXT
 *
 * @brief This is the SCU hardware definition for an STP request.
 */
struct STP_TASK_CONTEXT
{
   // OFFSET 0x18
   U32 fis_type    : 8;
   U32 pm_port     : 4;
   U32 reserved0   : 3;
   U32 control     : 1;
   U32 command     : 8;
   U32 features    : 8;

   // OFFSET 0x1C
   U32 reserved1;

   // OFFSET 0x20
   U32 reserved2;

   // OFFSET 0x24
   U32 reserved3;

   // OFFSET 0x28
   U32 ncq_tag     : 5;
   U32 reserved4   : 27;

   // OFFSET 0x2C
   U32 data_offset;    // TODO: What is this used for?
};

/**
 * @struct SMP_TASK_CONTEXT
 *
 * @brief This is the SCU hardware definition for an SMP request.
 */
struct SMP_TASK_CONTEXT
{
   // OFFSET 0x18
   U32 response_length    : 8;
   U32 function_result    : 8;
   U32 function           : 8;
   U32 frame_type         : 8;

   // OFFSET 0x1C
   U32 smp_response_ufi   : 12;
   U32 reserved1          : 20;

   // OFFSET 0x20
   U32 reserved2;

   // OFFSET 0x24
   U32 reserved3;

   // OFFSET 0x28
   U32 reserved4;

   // OFFSET 0x2C
   U32 reserved5;
};

/**
 * @struct PRIMITIVE_TASK_CONTEXT
 *
 * @brief This is the SCU hardware definition used when the driver wants to
 *        send a primitive on the link.
 */
struct PRIMITIVE_TASK_CONTEXT
{
   // OFFSET 0x18
   /**
    * This field is the control word and it must be 0.
    */
   U32 control;  ///< must be set to 0

   // OFFSET 0x1C
   /**
    * This field specifies the primitive that is to be transmitted.
    */
   U32 sequence;

   // OFFSET 0x20
   U32 reserved0;

   // OFFSET 0x24
   U32 reserved1;

   // OFFSET 0x28
   U32 reserved2;

   // OFFSET 0x2C
   U32 reserved3;
};

/**
 * @union PROTOCOL_CONTEXT
 *
 * @brief The union of the protocols that can be selected in the SCU task
 *        context field.
 */
union PROTOCOL_CONTEXT
{
    struct SSP_TASK_CONTEXT         ssp;
    struct STP_TASK_CONTEXT         stp;
    struct SMP_TASK_CONTEXT         smp;
    struct PRIMITIVE_TASK_CONTEXT   primitive;
    U32                             words[6];
};

/**
 * @struct SCU_SGL_ELEMENT
 * @typedef SCU_SGL_ELEMENT_T
 *
 * @brief This structure represents a single SCU defined SGL element.
 *
 * SCU SGLs contain a 64 bit address with the maximum data transfer being 24
 * bits in size.  The SGL can not cross a 4GB boundary.
 */
typedef struct SCU_SGL_ELEMENT
{
   /**
    * This field is the upper 32 bits of the 64 bit physical address.
    */
   U32    address_upper;

   /**
    * This field is the lower 32 bits of the 64 bit physical address.
    */
   U32    address_lower;

   /**
    * This field is the number of bytes to transfer.
    */
   U32    length: 24;

   /**
    * This field is the address modifier to be used when a virtual function is
    * requesting a data transfer.
    */
   U32    address_modifier: 8;

} SCU_SGL_ELEMENT_T;

#define SCU_SGL_ELEMENT_PAIR_A   0
#define SCU_SGL_ELEMENT_PAIR_B   1

/**
 * @struct SCU_SGL_ELEMENT_PAIR
 *
 * @brief This structure is the SCU hardware definition of a pair of SGL
 *        elements.
 *
 * The SCU hardware always works on SGL pairs.  They are referred to in the DS
 * specification as SGL A and SGL B.  Each SGL pair is followed by the address
 * of the next pair.
 */
typedef struct SCU_SGL_ELEMENT_PAIR
{
   // OFFSET 0x60-0x68
   /**
    * This field is the SGL element A of the SGL pair.
    */
   SCU_SGL_ELEMENT_T A;

   // OFFSET 0x6C-0x74
   /**
    * This field is the SGL element B of the SGL pair.
    */
   SCU_SGL_ELEMENT_T B;

   // OFFSET 0x78-0x7C
   /**
    * This field is the upper 32 bits of the 64 bit address to the next SGL
    * element pair.
    */
   U32 next_pair_upper;

   /**
    * This field is the lower 32 bits of the 64 bit address to the next SGL
    * element pair.
    */
   U32 next_pair_lower;

} SCU_SGL_ELEMENT_PAIR_T;

/**
 * @struct TRANSPORT_SNAPSHOT
 *
 * @brief This structure is the SCU hardware scratch area for the task
 *        context.
 *
 * This is set to 0 by the driver but can be read by issuing a dump TC request
 * to the SCU.
 */
struct TRANSPORT_SNAPSHOT
{
   // OFFSET 0x48
   U32  xfer_rdy_write_data_length;

   // OFFSET 0x4C
   U32  data_offset;

   // OFFSET 0x50
   U32  data_transfer_size   : 24;
   U32  reserved_50_0        : 8;

   // OFFSET 0x54
   U32  next_initiator_write_data_offset;

   // OFFSET 0x58
   U32  next_initiator_write_data_xfer_size : 24;
   U32  reserved_58_0                       : 8;
};

/**
 * @struct SCU_TASK_CONTEXT
 *
 * @brief This structure defines the contents of the SCU silicon task context.
 *        It lays out all of the fields according to the expected order and
 *        location for the Storage Controller unit.
 */
typedef struct SCU_TASK_CONTEXT
{
   // OFFSET 0x00 ------
   /**
    * This field must be encoded to one of the valid SCU task priority values
    *    - SCU_TASK_PRIORITY_NORMAL
    *    - SCU_TASK_PRIORITY_HEAD_OF_Q
    *    - SCU_TASK_PRIORITY_HIGH
    */
   U32    priority              : 2;

   /**
    * This field must be set to TRUE if this is an initiator generated request.
    * Until target mode is supported all task requests are initiator requests.
    */
   U32    initiator_request     : 1;

   /**
    * This field must be set to one of the valid connection rates valid values
    * are 0x8, 0x9, and 0xA.
    */
   U32    connection_rate       : 4;

   /**
    * This field muse be programed when generating an SMP response since the SMP
    * connection remains open until the SMP response is generated.
    */
   U32    protocol_engine_index : 3;

   /**
    * This field must contain the logical port for the task request.
    */
   U32    logical_port_index    : 3;

   /**
    * This field must be set to one of the SCU_TASK_CONTEXT_PROTOCOL values
    *    - SCU_TASK_CONTEXT_PROTOCOL_SMP
    *    - SCU_TASK_CONTEXT_PROTOCOL_SSP
    *    - SCU_TASK_CONTEXT_PROTOCOL_STP
    *    - SCU_TASK_CONTEXT_PROTOCOL_NONE
    */
   U32    protocol_type         : 3;

   /**
    * This filed must be set to the TCi allocated for this task
    */
   U32    task_index            : 12;

   /**
    * This field is reserved and must be set to 0x00
    */
   U32    reserved_00_0         : 1;

   /**
    * For a normal task request this must be set to 0.  If this is an abort of
    * this task request it must be set to 1.
    */
   U32    abort                 : 1;

   /**
    * This field must be set to TRUE for the SCU hardware to process the task.
    */
   U32    valid                 : 1;

   /**
    * This field must be set to SCU_TASK_CONTEXT_TYPE
    */
   U32    context_type          : 1;

   // OFFSET 0x04
   /**
    * This field contains the RNi that is the target of this request.
    */
   U32    remote_node_index     : 12;

   /**
    * This field is programmed if this is a mirrored request, which we are not
    * using, in which case it is the RNi for the mirrored target.
    */
   U32    mirrored_node_index   : 12;

   /**
    * This field is programmed with the direction of the SATA reqeust
    *    - SCU_SATA_WRITE_DATA_DIRECTION
    *    - SCU_SATA_READ_DATA_DIRECTION
    */
   U32    sata_direction        : 1;

   /**
    * This field is programmsed with one of the following SCU_COMMAND_CODE
    *    - SCU_COMMAND_CODE_INITIATOR_NEW_TASK
    *    - SCU_COMMAND_CODE_ACTIVE_TASK
    *    - SCU_COMMAND_CODE_PRIMITIVE_SEQ_TASK
    *    - SCU_COMMAND_CODE_TARGET_RAW_FRAMES
    */
   U32    command_code          : 2;

   /**
    * This field is set to TRUE if the remote node should be suspended.
    * This bit is only valid for SSP & SMP target devices.
    */
   U32    suspend_node          : 1;

   /**
    * This field is programmed with one of the following command type codes
    *
    * For SAS requests use the SCU_SSP_TASK_TYPE
    *    - SCU_TASK_TYPE_IOREAD
    *    - SCU_TASK_TYPE_IOWRITE
    *    - SCU_TASK_TYPE_SMP_REQUEST
    *    - SCU_TASK_TYPE_RESPONSE
    *    - SCU_TASK_TYPE_RAW_FRAME
    *    - SCU_TASK_TYPE_PRIMITIVE
    *
    * For SATA requests use the SCU_SATA_TASK_TYPE
    *    - SCU_TASK_TYPE_DMA_IN
    *    - SCU_TASK_TYPE_FPDMAQ_READ
    *    - SCU_TASK_TYPE_PACKET_DMA_IN
    *    - SCU_TASK_TYPE_SATA_RAW_FRAME
    *    - SCU_TASK_TYPE_DMA_OUT
    *    - SCU_TASK_TYPE_FPDMAQ_WRITE
    *    - SCU_TASK_TYPE_PACKET_DMA_OUT
    */
   U32    task_type             : 4;

   // OFFSET 0x08
   /**
    * This field is reserved and the must be set to 0x00
    */
   U32    link_layer_control          : 8;  // presently all reserved

   /**
    * This field is set to TRUE when TLR is to be enabled
    */
   U32    ssp_tlr_enable              : 1;

   /**
    * This is field specifies if the SCU DMAs a response frame to host
    * memory for good response frames when operating in target mode.
    */
   U32    dma_ssp_target_good_response : 1;

   /**
    * This field indicates if the SCU should DMA the response frame to
    * host memory.
    */
   U32    do_not_dma_ssp_good_response : 1;

   /**
    * This field is set to TRUE when strict ordering is to be enabled
    */
   U32    strict_ordering              : 1;

   /**
    * This field indicates the type of endianness to be utilized for the
    * frame.  command, task, and response frames utilized control_frame
    * set to 1.
    */
   U32    control_frame               : 1;

   /**
    * This field is reserved and the driver should set to 0x00
    */
   U32    tl_control_reserved         : 3;

   /**
    * This field is set to TRUE when the SCU hardware task timeout control is to
    * be enabled
    */
   U32    timeout_enable              : 1;

   /**
    * This field is reserved and the driver should set it to 0x00
    */
   U32    pts_control_reserved        : 7;

   /**
    * This field should be set to TRUE when block guard is to be enabled
    */
   U32    block_guard_enable          : 1;

   /**
    * This field is reserved and the driver should set to 0x00
    */
   U32    sdma_control_reserved       : 7;

   // OFFSET 0x0C
   /**
    * This field is the address modifier for this io request it should be
    * programmed with the virtual function that is making the request.
    */
   U32    address_modifier            : 16;

   /**
    * @todo What we support mirrored SMP response frame?
    */
   U32    mirrored_protocol_engine    : 3;    // mirrored protocol Engine Index

   /**
    * If this is a mirrored request the logical port index for the mirrored RNi
    * must be programmed.
    */
   U32    mirrored_logical_port       : 4;    // mirrored local port index

   /**
    * This field is reserved and the driver must set it to 0x00
    */
   U32    reserved_0C_0               : 8;

   /**
    * This field must be set to TRUE if the mirrored request processing is to be
    * enabled.
    */
   U32    mirror_request_enable       : 1;    // Mirrored request Enable

   // OFFSET 0x10
   /**
    * This field is the command iu length in dwords
    */
   U32    ssp_command_iu_length       : 8;

   /**
    * This is the target TLR enable bit it must be set to 0 when creatning the
    * task context.
    */
   U32    xfer_ready_tlr_enable       : 1;

   /**
    * This field is reserved and the driver must set it to 0x00
    */
   U32    reserved_10_0               : 7;

   /**
    * This is the maximum burst size that the SCU hardware will send in one
    * connection its value is (N x 512) and N must be a multiple of 2.  If the
    * value is 0x00 then maximum burst size is disabled.
    */
   U32    ssp_max_burst_size          : 16;

   // OFFSET 0x14
   /**
    * This filed is set to the number of bytes to be transferred in the request.
    */
   U32    transfer_length_bytes       : 24;   // In terms of bytes

   /**
    * This field is reserved and the driver should set it to 0x00
    */
   U32    reserved_14_0               : 8;

   // OFFSET 0x18-0x2C
   /**
    * This union provides for the protocol specific part of the SCU Task Context.
    */
   union PROTOCOL_CONTEXT  type;

   // OFFSET 0x30-0x34
   /**
    * This field is the upper 32 bits of the 64 bit physical address of the
    * command iu buffer
    */
   U32  command_iu_upper;

   /**
    * This field is the lower 32 bits of the 64 bit physical address of the
    * command iu buffer
    */
   U32  command_iu_lower;

   // OFFSET 0x38-0x3C
   /**
    * This field is the upper 32 bits of the 64 bit physical address of the
    * response iu buffer
    */
   U32  response_iu_upper;

   /**
    * This field is the lower 32 bits of the 64 bit physical address of the
    * response iu buffer
    */
   U32  response_iu_lower;

   // OFFSET 0x40
   /**
    * This field is set to the task phase of the SCU hardware. The driver must
    * set this to 0x01
    */
   U32  task_phase            : 8;

   /**
    * This field is set to the transport layer task status.  The driver must set
    * this to 0x00
    */
   U32  task_status           : 8;

   /**
    * This field is used during initiator write TLR
    */
   U32  previous_extended_tag : 4;

   /**
    * This field is set the maximum number of retries for a STP non-data FIS
    */
   U32  stp_retry_count       : 2;

   /**
    * This field is reserved and the driver must set it to 0x00
    */
   U32  reserved_40_1         : 2;

   /**
    * This field is used by the SCU TL to determine when to take a snapshot when
    * tranmitting read data frames.
    *    - 0x00 The entire IO
    *    - 0x01 32k
    *    - 0x02 64k
    *    - 0x04 128k
    *    - 0x08 256k
    */
   U32  ssp_tlr_threshold     : 4;

   /**
    * This field is reserved and the driver must set it to 0x00
    */
   U32  reserved_40_2         : 4;

   // OFFSET 0x44
   U32  write_data_length;   // read only set to 0

   // OFFSET 0x48-0x58
   struct TRANSPORT_SNAPSHOT snapshot;  // read only set to 0

   // OFFSET 0x5C
   U32  block_protection_enable       : 1;
   U32  block_size                    : 2;
   U32  block_protection_function     : 2;
   U32  reserved_5C_0                 : 9;
   U32  active_sgl_element            : 2;   // read only set to 0
   U32  sgl_exhausted                 : 1;   // read only set to 0
   U32  payload_data_transfer_error   : 4;   // read only set to 0
   U32  frame_buffer_offset           : 11;  // read only set to 0

   // OFFSET 0x60-0x7C
   /**
    * This field is the first SGL element pair found in the TC data structure.
    */
   SCU_SGL_ELEMENT_PAIR_T sgl_pair_ab;
   // OFFSET 0x80-0x9C
   /**
    * This field is the second SGL element pair found in the TC data structure.
    */
   SCU_SGL_ELEMENT_PAIR_T sgl_pair_cd;

   // OFFSET 0xA0-BC
   SCU_SGL_ELEMENT_PAIR_T sgl_snapshot_ac;

   // OFFSET 0xC0
   U32  active_sgl_element_pair; // read only set to 0

   // OFFSET 0xC4-0xCC
   U32 reserved_C4_CC[3];

   // OFFSET 0xD0
   U32  intermediate_crc_value : 16;
   U32  initial_crc_seed       : 16;

   // OFFSET 0xD4
   U32  application_tag_for_verify    : 16;
   U32  application_tag_for_generate  : 16;

   // OFFSET 0xD8
   U32  reference_tag_seed_for_verify_function;

   // OFFSET 0xDC
   U32  reserved_DC;

   // OFFSET 0xE0
   U32  reserved_E0_0                    : 16;
   U32  application_tag_mask_for_generate: 16;

   // OFFSET 0xE4
   U32  block_protection_control         : 16;
   U32  application_tag_mask_for_verify  : 16;

   // OFFSET 0xE8
   U32  block_protection_error  : 8;
   U32  reserved_E8_0           :24;

   // OFFSET 0xEC
   U32  reference_tag_seed_for_verify;

   // OFFSET 0xF0
   U32  intermediate_crc_valid_snapshot  : 16;
   U32  reserved_F0_0                    : 16;

   // OFFSET 0xF4
   U32  reference_tag_seed_for_verify_function_snapshot;

   // OFFSET 0xF8
   U32  snapshot_of_reserved_dword_DC_of_tc;

   // OFFSET 0xFC
   U32  reference_tag_seed_for_generate_function_snapshot;

} SCU_TASK_CONTEXT_T;

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCU_TASK_CONTEXT_H_
