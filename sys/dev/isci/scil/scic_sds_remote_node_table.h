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
#ifndef _SCIC_SDS_REMOTE_NODE_TABLE_H_
#define _SCIC_SDS_REMOTE_NODE_TABLE_H_

/**
 * @file
 *
 * @brief This file contains the structures, constants and prototypes used for
 *        the remote node table.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_controller_constants.h>

/**
 * Remote node sets are sets of remote node index in the remtoe node table
 * The SCU hardware requires that STP remote node entries take three
 * consecutive remote node index so the table is arranged in sets of three.
 * The bits are used as 0111 0111 to make a byte and the bits define the set
 * of three remote nodes to use as a sequence.
 */
#define SCIC_SDS_REMOTE_NODE_SETS_PER_BYTE 2

/**
 * Since the remote node table is organized as DWORDS take the remote node
 * sets in bytes and represent them in DWORDs. The lowest ordered bits are the
 * ones used in case full DWORD is not being used.
 *
 * i.e. 0000 0000 0000 0000 0111 0111 0111 0111 // if only a single WORD is in
 * use in the DWORD.
 */
#define SCIC_SDS_REMOTE_NODE_SETS_PER_DWORD \
   (sizeof(U32) * SCIC_SDS_REMOTE_NODE_SETS_PER_BYTE)
/**
 * This is a count of the numeber of remote nodes that can be represented in
 * a byte
 */
#define SCIC_SDS_REMOTE_NODES_PER_BYTE  \
   (SCU_STP_REMOTE_NODE_COUNT * SCIC_SDS_REMOTE_NODE_SETS_PER_BYTE)

/**
 * This is a count of the number of remote nodes that can be represented in a
 * DWROD
 */
#define SCIC_SDS_REMOTE_NODES_PER_DWORD \
   (sizeof(U32) * SCIC_SDS_REMOTE_NODES_PER_BYTE)

/**
 * This is the number of bits in a remote node group
 */
#define SCIC_SDS_REMOTE_NODES_BITS_PER_GROUP   4

#define SCIC_SDS_REMOTE_NODE_TABLE_INVALID_INDEX      (0xFFFFFFFF)
#define SCIC_SDS_REMOTE_NODE_TABLE_FULL_SLOT_VALUE    (0x07)
#define SCIC_SDS_REMOTE_NODE_TABLE_EMPTY_SLOT_VALUE   (0x00)

/**
 * Expander attached sata remote node count
 */
#define SCU_STP_REMOTE_NODE_COUNT        3

/**
 * Expander or direct attached ssp remote node count
 */
#define SCU_SSP_REMOTE_NODE_COUNT        1

/**
 * Direct attached STP remote node count
 */
#define SCU_SATA_REMOTE_NODE_COUNT       1

/**
 * @struct SCIC_REMOTE_NODE_TABLE
 */
typedef struct SCIC_REMOTE_NODE_TABLE
{
   /**
    * This field contains the array size in dwords
    */
   U16 available_nodes_array_size;

   /**
    * This field contains the array size of the
    */
   U16 group_array_size;

   /**
    * This field is the array of available remote node entries in bits.
    * Because of the way STP remote node data is allocated on the SCU hardware
    * the remote nodes must occupy three consecutive remote node context
    * entries.  For ease of allocation and de-allocation we have broken the
    * sets of three into a single nibble.  When the STP RNi is allocated all
    * of the bits in the nibble are cleared.  This math results in a table size
    * of MAX_REMOTE_NODES / CONSECUTIVE RNi ENTRIES for STP / 2 entries per byte.
    */
   U32 available_remote_nodes[
        (SCI_MAX_REMOTE_DEVICES / SCIC_SDS_REMOTE_NODES_PER_DWORD)
      + ((SCI_MAX_REMOTE_DEVICES % SCIC_SDS_REMOTE_NODES_PER_DWORD) != 0)];

   /**
    * This field is the nibble selector for the above table.  There are three
    * possible selectors each for fast lookup when trying to find one, two or
    * three remote node entries.
    */
   U32 remote_node_groups[
      SCU_STP_REMOTE_NODE_COUNT][
           (SCI_MAX_REMOTE_DEVICES / (32 * SCU_STP_REMOTE_NODE_COUNT))
         + ((SCI_MAX_REMOTE_DEVICES % (32 * SCU_STP_REMOTE_NODE_COUNT)) != 0)];

} SCIC_REMOTE_NODE_TABLE_T;

// ---------------------------------------------------------------------------

void scic_sds_remote_node_table_initialize(
   SCIC_REMOTE_NODE_TABLE_T * remote_node_table,
   U32                        remote_node_entries
);

U16 scic_sds_remote_node_table_allocate_remote_node(
   SCIC_REMOTE_NODE_TABLE_T * remote_node_table,
   U32                        remote_node_count
);

void scic_sds_remote_node_table_release_remote_node_index(
   SCIC_REMOTE_NODE_TABLE_T * remote_node_table,
   U32                        remote_node_count,
   U16                        remote_node_index
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_SDS_REMOTE_NODE_TABLE_H_
