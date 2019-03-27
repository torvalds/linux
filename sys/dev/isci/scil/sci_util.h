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
#ifndef _SCI_UTIL_H_
#define _SCI_UTIL_H_

#include <sys/param.h>

#include <dev/isci/scil/sci_types.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/**
 * Normal byte swap macro
 */
#define SCIC_SWAP_DWORD(x) \
   ( \
       (((x) >> 24) & 0x000000FF) \
     | (((x) >>  8) & 0x0000FF00) \
     | (((x) <<  8) & 0x00FF0000) \
     | (((x) << 24) & 0xFF000000) \
   )

#define SCIC_BUILD_DWORD(char_buffer) \
   ( \
     ((char_buffer)[0] << 24) \
   | ((char_buffer)[1] << 16) \
   | ((char_buffer)[2] <<  8) \
   | ((char_buffer)[3]) \
   )

#define SCI_FIELD_OFFSET(type, field)   ((POINTER_UINT)&(((type *)0)->field))

//This macro counts how many bits being set in a mask.
#define SCI_GET_BITS_SET_COUNT(mask, set_bit_count)     \
{                                                  \
   U8 index;                                       \
   set_bit_count = 0;                              \
   for (index = 0; index < sizeof(mask)*8; index++)            \
   {                                               \
      if( mask & (1<<index) )                      \
         set_bit_count++;                          \
   }                                               \
}

/**
 * This macro simply performs addition on an SCI_PHYSICAL_ADDRESS
 * type.  The lower U32 value is "clipped" or "wrapped" back through
 * 0.  When this occurs the upper 32-bits are incremented by 1.
 */
#define sci_physical_address_add(physical_address, value) \
{ \
   U32 lower = sci_cb_physical_address_lower((physical_address)); \
   U32 upper = sci_cb_physical_address_upper((physical_address)); \
 \
   if (lower + (value) < lower) \
      upper += 1; \
 \
   lower += (value); \
   sci_cb_make_physical_address(physical_address, upper, lower); \
}

/**
 * This macro simply performs subtraction on an SCI_PHYSICAL_ADDRESS
 * type.  The lower U32 value is "clipped" or "wrapped" back through
 * 0.  When this occurs the upper 32-bits are decremented by 1.
 */
#define sci_physical_address_subtract(physical_address, value) \
{ \
   U32 lower = sci_cb_physical_address_lower((physical_address)); \
   U32 upper = sci_cb_physical_address_upper((physical_address)); \
 \
   if (lower - (value) > lower) \
      upper -= 1; \
 \
   lower -= (value); \
   sci_cb_make_physical_address(physical_address, upper, lower); \
}

/**
 * @brief Copy the data from source to destination and swap the
 *        bytes during the copy.
 *
 * @param[in] destination This parameter specifies the destination address
 *            to which the data is to be copied.
 * @param[in] source This parameter specifies the source address from
 *            which data is to be copied.
 * @param[in] word_count This parameter specifies the number of 32-bit words
 *            to copy and byte swap.
 *
 * @return none
 */
void scic_word_copy_with_swap(
   U32 *destination,
   U32 *source,
   U32 word_count
);


#define sci_ssp_get_sense_data_length(sense_data_length_buffer) \
   SCIC_BUILD_DWORD(sense_data_length_buffer)

#define sci_ssp_get_response_data_length(response_data_length_buffer) \
   SCIC_BUILD_DWORD(response_data_length_buffer)

#endif // _SCI_UTIL_H_
