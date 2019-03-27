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
/**
 * @file
 *
 * @brief This file contains all of the unsolicited frame related
 *        management for the address table, the headers, and actual
 *        payload buffers.
 */

#ifndef _SCIC_SDS_UNSOLICITED_FRAME_CONTROL_H_
#define _SCIC_SDS_UNSOLICITED_FRAME_CONTROL_H_

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/types.h>
#include <dev/isci/scil/scu_unsolicited_frame.h>
#include <dev/isci/scil/sci_memory_descriptor_list.h>
#include <dev/isci/scil/scu_constants.h>
#include <dev/isci/scil/sci_status.h>

/**
 * @enum UNSOLICITED_FRAME_STATE
 *
 * This enumeration represents the current unsolicited frame state.  The
 * controller object can not updtate the hardware unsolicited frame put
 * pointer unless it has already processed the priror unsolicited frames.
 */
enum UNSOLICITED_FRAME_STATE
{
   /**
    * This state is when the frame is empty and not in use.  It is
    * different from the released state in that the hardware could DMA
    * data to this frame buffer.
    */
   UNSOLICITED_FRAME_EMPTY,

   /**
    * This state is set when the frame buffer is in use by by some
    * object in the system.
    */
   UNSOLICITED_FRAME_IN_USE,

   /**
    * This state is set when the frame is returned to the free pool
    * but one or more frames prior to this one are still in use.
    * Once all of the frame before this one are freed it will go to
    * the empty state.
    */
   UNSOLICITED_FRAME_RELEASED,

   UNSOLICITED_FRAME_MAX_STATES
};

/**
 * @struct SCIC_SDS_UNSOLICITED_FRAME
 *
 * This is the unsolicited frame data structure it acts as the container for
 * the current frame state, frame header and frame buffer.
 */
typedef struct SCIC_SDS_UNSOLICITED_FRAME
{
   /**
    * This field contains the current frame state
    */
   enum UNSOLICITED_FRAME_STATE state;

   /**
    * This field points to the frame header data.
    */
   SCU_UNSOLICITED_FRAME_HEADER_T *header;

   /**
    * This field points to the frame buffer data.
    */
   void *buffer;

} SCIC_SDS_UNSOLICITED_FRAME_T;

/**
 * @struct SCIC_SDS_UF_HEADER_ARRAY
 *
 * This structure contains all of the unsolicited frame header
 * information.
 */
typedef struct SCIC_SDS_UF_HEADER_ARRAY
{
   /**
    * This field is represents a virtual pointer to the start
    * address of the UF address table.  The table contains
    * 64-bit pointers as required by the hardware.
    */
   SCU_UNSOLICITED_FRAME_HEADER_T *array;

   /**
    * This field specifies the physical address location for the UF
    * buffer array.
    */
   SCI_PHYSICAL_ADDRESS physical_address;

} SCIC_SDS_UF_HEADER_ARRAY_T;

// Determine the size of the unsolicited frame array including
// unused buffers.
#if SCU_UNSOLICITED_FRAME_COUNT <= SCU_MIN_UF_TABLE_ENTRIES
#define SCU_UNSOLICITED_FRAME_CONTROL_ARRAY_SIZE SCU_MIN_UF_TABLE_ENTRIES
#else
#define SCU_UNSOLICITED_FRAME_CONTROL_ARRAY_SIZE SCU_MAX_UNSOLICITED_FRAMES
#endif // SCU_UNSOLICITED_FRAME_COUNT <= SCU_MIN_UF_TABLE_ENTRIES

/**
 * @struct SCIC_SDS_UF_BUFFER_ARRAY
 *
 * This structure contains all of the unsolicited frame buffer (actual
 * payload) information.
 */
typedef struct SCIC_SDS_UF_BUFFER_ARRAY
{
   /**
    * This field is the minimum number of unsolicited frames supported by the
    * hardware and the number of unsolicited frames requested by the software.
    */
   U32 count;

   /**
    * This field is the SCIC_UNSOLICITED_FRAME data its used to manage
    * the data for the unsolicited frame requests.  It also represents
    * the virtual address location that corresponds to the
    * physical_address field.
    */
   SCIC_SDS_UNSOLICITED_FRAME_T array[SCU_UNSOLICITED_FRAME_CONTROL_ARRAY_SIZE];

   /**
    * This field specifies the physical address location for the UF
    * buffer array.
    */
   SCI_PHYSICAL_ADDRESS physical_address;

} SCIC_SDS_UF_BUFFER_ARRAY_T;

/**
 * @struct SCIC_SDS_UF_ADDRESS_TABLE_ARRAY
 *
 * This object maintains all of the unsolicited frame address
 * table specific data.  The address table is a collection of
 * 64-bit pointers that point to 1KB buffers into which
 * the silicon will DMA unsolicited frames.
 */
typedef struct SCIC_SDS_UF_ADDRESS_TABLE_ARRAY
{
   /**
    * This field specifies the actual programmed size of the
    * unsolicited frame buffer address table.  The size of the table
    * can be larger than the actual number of UF buffers, but it must
    * be a power of 2 and the last entry in the table is not allowed
    * to be NULL.
    */
   U32 count;

   /**
    * This field represents a virtual pointer that refers to the
    * starting address of the UF address table.
    * 64-bit pointers are required by the hardware.
    */
   SCI_PHYSICAL_ADDRESS * array;

   /**
    * This field specifies the physical address location for the UF
    * address table.
    */
   SCI_PHYSICAL_ADDRESS physical_address;

} SCIC_SDS_UF_ADDRESS_TABLE_ARRAY_T;

/**
 * @struct SCIC_SDS_UNSOLICITED_FRAME_CONTROL
 *
 * This object contains all of the data necessary to handle
 * unsolicited frames.
 */
typedef struct SCIC_SDS_UNSOLICITED_FRAME_CONTROL
{
   /**
    * This field is the software copy of the unsolicited frame queue
    * get pointer.  The controller object writes this value to the
    * hardware to let the hardware put more unsolicited frame entries.
    */
   U32 get;

   /**
    * This field contains all of the unsolicited frame header
    * specific fields.
    */
   SCIC_SDS_UF_HEADER_ARRAY_T headers;

   /**
    * This field contains all of the unsolicited frame buffer
    * specific fields.
    */
   SCIC_SDS_UF_BUFFER_ARRAY_T buffers;

   /**
    * This field contains all of the unsolicited frame address table
    * specific fields.
    */
   SCIC_SDS_UF_ADDRESS_TABLE_ARRAY_T address_table;

} SCIC_SDS_UNSOLICITED_FRAME_CONTROL_T;

void scic_sds_unsolicited_frame_control_set_address_table_count(
   SCIC_SDS_UNSOLICITED_FRAME_CONTROL_T *uf_control
);

struct SCIC_SDS_CONTROLLER;
void scic_sds_unsolicited_frame_control_construct(
   SCIC_SDS_UNSOLICITED_FRAME_CONTROL_T *uf_control,
   SCI_PHYSICAL_MEMORY_DESCRIPTOR_T     *mde,
   struct SCIC_SDS_CONTROLLER           *this_controller
);

SCI_STATUS scic_sds_unsolicited_frame_control_get_header(
   SCIC_SDS_UNSOLICITED_FRAME_CONTROL_T *uf_control,
   U32                                   frame_index,
   void                                **frame_header
);

SCI_STATUS scic_sds_unsolicited_frame_control_get_buffer(
   SCIC_SDS_UNSOLICITED_FRAME_CONTROL_T *uf_control,
   U32                                   frame_index,
   void                                **frame_buffer
);

BOOL scic_sds_unsolicited_frame_control_release_frame(
   SCIC_SDS_UNSOLICITED_FRAME_CONTROL_T *uf_control,
   U32                                   frame_index
);

/**
 * This macro simply calculates the size of the memory descriptor
 * entry that relates to unsolicited frames and the surrounding
 * silicon memory required to utilize it.
 */
#define scic_sds_unsolicited_frame_control_get_mde_size(uf_control) \
   ( ((uf_control).buffers.count * SCU_UNSOLICITED_FRAME_BUFFER_SIZE) \
   + ((uf_control).address_table.count * sizeof(SCI_PHYSICAL_ADDRESS)) \
   + ((uf_control).buffers.count * sizeof(SCU_UNSOLICITED_FRAME_HEADER_T)) )

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_SDS_UNSOLICITED_FRAME_CONTROL_H_
