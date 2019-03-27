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
#ifndef _SCI_MEMORY_DESCRIPTOR_LIST_DECORATOR_H_
#define _SCI_MEMORY_DESCRIPTOR_LIST_DECORATOR_H_

/**
 * @file
 *
 * @brief This file contains methods utilized to provide additional
 *        functionality above normal MDL processing.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_memory_descriptor_list.h>

/**
 * @brief This method will determine the amount of memory needed for
 *        memory descriptors with exact matching memory attributes.
 *        If the supplied attributes value is 0, then all MDEs are
 *        included in the calculation.
 *
 * @param[in] mdl This parameter specifies the MDL to search through
 *            for MDEs with matching memory attributes.
 * @param[in] attributes This parameter specifies the attributes to
 *            match.  If this parameter is set to 0, then each MDE
 *            is included in the calculation.
 *
 * @return This method returns the number of bytes, including offsets
 *         to achieve proper alignment, for memory descriptors that
 *         exactly match the supplied memory attributes.
 */
U32 sci_mdl_decorator_get_memory_size(
   SCI_MEMORY_DESCRIPTOR_LIST_HANDLE_T mdl,
   U32                                 attributes
);

/**
 * @brief This method will assign the supplied memory address values
 *        to all of the MDEs in the memory descriptor list with
 *        exact matching attributes as those supplied by parameter.
 *        If the supplied attributes value is 0, then all MDEs will
 *        have their values assigned.
 *
 * @warning It is suggested the user invoke the
 *          sci_mdl_decorator_get_memory_size() method prior to invoking
 *          this method.  This ensures that the user supplies pointers
 *          that refer to memory locations with sufficient space.
 *
 * @param[in,out] mdl This parameter specifies the MDL to iterate through
 *                for MDEs with matching memory attributes.
 * @param[in]     attributes This parameter specifies the attributes to
 *                match.  If this parameter is set to 0, then each
 *                memory descriptor will be filled out.
 * @param[in]     virtual_address This parameter specifies the starting
 *                virtual address to be used when filling out the MDE
 *                virtual address field.
 * @param[in]     sci_physical_address This parameter specifies the starting
 *                physical address to be used when filling out the MDE
 *                physical address field.
 *
 * @return none
 */
void sci_mdl_decorator_assign_memory(
   SCI_MEMORY_DESCRIPTOR_LIST_HANDLE_T mdl,
   U32                                 attributes,
   POINTER_UINT                        virtual_address,
   SCI_PHYSICAL_ADDRESS                sci_physical_address
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCI_MEMORY_DESCRIPTOR_LIST_DECORATOR_H_

