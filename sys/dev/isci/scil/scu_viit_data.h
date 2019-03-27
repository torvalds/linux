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
#ifndef _SCU_VIIT_DATA_HEADER_
#define _SCU_VIIT_DATA_HEADER_

/**
 * @file
 *
 * @brief This file contains the constants and structures for the SCU hardware
 *        VIIT table entries.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>

#define SCU_VIIT_ENTRY_ID_MASK         (0xC0000000UL)
#define SCU_VIIT_ENTRY_ID_SHIFT        (30UL)

#define SCU_VIIT_ENTRY_FUNCTION_MASK   (0x0FF00000UL)
#define SCU_VIIT_ENTRY_FUNCTION_SHIFT  (20UL)

#define SCU_VIIT_ENTRY_IPPTMODE_MASK   (0x0001F800UL)
#define SCU_VIIT_ENTRY_IPPTMODE_SHIFT  (12UL)

#define SCU_VIIT_ENTRY_LPVIE_MASK      (0x00000F00UL)
#define SCU_VIIT_ENTRY_LPVIE_SHIFT     (8UL)

#define SCU_VIIT_ENTRY_STATUS_MASK     (0x000000FFUL)
#define SCU_VIIT_ENTRY_STATUS_SHIFT    (0UL)

#define SCU_VIIT_ENTRY_ID_INVALID   (0UL << SCU_VIIT_ENTRY_ID_SHIFT)
#define SCU_VIIT_ENTRY_ID_VIIT      (1UL << SCU_VIIT_ENTRY_ID_SHIFT)
#define SCU_VIIT_ENTRY_ID_IIT       (2UL << SCU_VIIT_ENTRY_ID_SHIFT)
#define SCU_VIIT_ENTRY_ID_VIRT_EXP  (3UL << SCU_VIIT_ENTRY_ID_SHIFT)

#define SCU_VIIT_IPPT_SSP_INITIATOR (0x01UL << SCU_VIIT_ENTRY_IPPTMODE_SHIFT)
#define SCU_VIIT_IPPT_SMP_INITIATOR (0x02UL << SCU_VIIT_ENTRY_IPPTMODE_SHIFT)
#define SCU_VIIT_IPPT_STP_INITIATOR (0x04UL << SCU_VIIT_ENTRY_IPPTMODE_SHIFT)
#define SCU_VIIT_IPPT_INITIATOR     \
   (                                \
       SCU_VIIT_IPPT_SSP_INITIATOR  \
     | SCU_VIIT_IPPT_SMP_INITIATOR  \
     | SCU_VIIT_IPPT_STP_INITIATOR  \
   )

#define SCU_VIIT_STATUS_RNC_VALID      (0x01UL << SCU_VIIT_ENTRY_STATUS_SHIFT)
#define SCU_VIIT_STATUS_ADDRESS_VALID  (0x02UL << SCU_VIIT_ENTRY_STATUS_SHIFT)
#define SCU_VIIT_STATUS_RNI_VALID      (0x04UL << SCU_VIIT_ENTRY_STATUS_SHIFT)
#define SCU_VIIT_STATUS_ALL_VALID      \
   (                                   \
       SCU_VIIT_STATUS_RNC_VALID       \
     | SCU_VIIT_STATUS_ADDRESS_VALID   \
     | SCU_VIIT_STATUS_RNI_VALID       \
   )

#define SCU_VIIT_IPPT_SMP_TARGET    (0x10UL << SCU_VIIT_ENTRY_IPPTMODE_SHIFT)

/**
 * @struct SCU_VIIT_ENTRY
 *
 * @brief This is the SCU Virtual Initiator Table Entry
 */
typedef struct SCU_VIIT_ENTRY
{
   /**
    * This must be encoded as to the type of initiator that is being constructed
    * for this port.
    */
   U32  status;

   /**
    * Virtual initiator high SAS Address
    */
   U32  initiator_sas_address_hi;

   /**
    * Virtual initiator low SAS Address
    */
   U32  initiator_sas_address_lo;

   /**
    * This must be 0
    */
   U32  reserved;

} SCU_VIIT_ENTRY_T;


// IIT Status Defines
#define SCU_IIT_ENTRY_ID_MASK                (0xC0000000UL)
#define SCU_IIT_ENTRY_ID_SHIFT               (30UL)

#define SCU_IIT_ENTRY_STATUS_UPDATE_MASK     (0x20000000UL)
#define SCU_IIT_ENTRY_STATUS_UPDATE_SHIFT    (29UL)

#define SCU_IIT_ENTRY_LPI_MASK               (0x00000F00UL)
#define SCU_IIT_ENTRY_LPI_SHIFT              (8UL)

#define SCU_IIT_ENTRY_STATUS_MASK            (0x000000FFUL)
#define SCU_IIT_ENTRY_STATUS_SHIFT           (0UL)

// IIT Remote Initiator Defines
#define SCU_IIT_ENTRY_REMOTE_TAG_MASK  (0x0000FFFFUL)
#define SCU_IIT_ENTRY_REMOTE_TAG_SHIFT (0UL)

#define SCU_IIT_ENTRY_REMOTE_RNC_MASK  (0x0FFF0000UL)
#define SCU_IIT_ENTRY_REMOTE_RNC_SHIFT (16UL)

#define SCU_IIT_ENTRY_ID_INVALID   (0UL << SCU_IIT_ENTRY_ID_SHIFT)
#define SCU_IIT_ENTRY_ID_VIIT      (1UL << SCU_IIT_ENTRY_ID_SHIFT)
#define SCU_IIT_ENTRY_ID_IIT       (2UL << SCU_IIT_ENTRY_ID_SHIFT)
#define SCU_IIT_ENTRY_ID_VIRT_EXP  (3UL << SCU_IIT_ENTRY_ID_SHIFT)

/**
 * @struct SCU_IIT_ENTRY
 *
 * @brief This will be implemented later when we support virtual functions
 */
typedef struct SCU_IIT_ENTRY
{
   U32  status;
   U32  remote_initiator_sas_address_hi;
   U32  remote_initiator_sas_address_lo;
   U32  remote_initiator;

} SCU_IIT_ENTRY_T;

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCU_VIIT_DATA_HEADER_
