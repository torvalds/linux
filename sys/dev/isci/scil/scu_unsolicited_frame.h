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
 * @brief This field defines the SCU format of an unsolicited frame (UF).  A
 *        UF is a frame received by the SCU for which there is no known
 *        corresponding task context (TC).
 */

#ifndef _SCU_UNSOLICITED_FRAME_H_
#define _SCU_UNSOLICITED_FRAME_H_

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>

/**
 * This constant defines the number of DWORDS found the unsolicited frame
 * header data member.
 */
#define SCU_UNSOLICITED_FRAME_HEADER_DATA_DWORDS 15

/**
 * @struct SCU_UNSOLICITED_FRAME_HEADER
 *
 * This structure delineates the format of an unsolicited frame header.
 * The first DWORD are UF attributes defined by the silicon architecture.
 * The data depicts actual header information received on the link.
 */
typedef struct SCU_UNSOLICITED_FRAME_HEADER
{
   /**
    * This field indicates if there is an Initiator Index Table entry with
    * which this header is associated.
    */
   U32 iit_exists : 1;

   /**
    * This field simply indicates the protocol type (i.e. SSP, STP, SMP).
    */
   U32 protocol_type : 3;

   /**
    * This field indicates if the frame is an address frame (IAF or OAF)
    * or if it is a information unit frame.
    */
   U32 is_address_frame : 1;

   /**
    * This field simply indicates the connection rate at which the frame
    * was received.
    */
   U32 connection_rate : 4;

   U32 reserved : 23;

   /**
    * This field represents the actual header data received on the link.
    */
   U32 data[SCU_UNSOLICITED_FRAME_HEADER_DATA_DWORDS];

} SCU_UNSOLICITED_FRAME_HEADER_T;

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCU_UNSOLICITED_FRAME_H_
