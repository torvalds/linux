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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/**
 * @file
 *
 * @brief This file contains all of the method implementations for the
 *        SCI_BASE_OBJECT object.
 */

#include <dev/isci/scil/sci_status.h>
#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_base_object.h>

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

#if defined(SCI_OBJECT_USE_ASSOCIATION_FUNCTIONS)
void * sci_object_get_association(
   SCI_OBJECT_HANDLE_T object
)
{
   return ((SCI_BASE_OBJECT_T *) object)->associated_object;
}
#endif

// ---------------------------------------------------------------------------

#if defined(SCI_OBJECT_USE_ASSOCIATION_FUNCTIONS)
SCI_STATUS sci_object_set_association(
   SCI_OBJECT_HANDLE_T   object,
   void                * associated_object
)
{
   ((SCI_BASE_OBJECT_T *)object)->associated_object = associated_object;
   return SCI_SUCCESS;
}
#endif

// ---------------------------------------------------------------------------

void sci_base_object_construct(
   SCI_BASE_OBJECT_T      * base_object,
   struct SCI_BASE_LOGGER * logger
)
{
#if defined(SCI_LOGGING)
   base_object->logger = logger;
#endif // defined(SCI_LOGGING)
   base_object->associated_object = NULL;
}

// ---------------------------------------------------------------------------

SCI_LOGGER_HANDLE_T sci_object_get_logger(
   SCI_OBJECT_HANDLE_T object
)
{
#if defined(SCI_LOGGING)
   return sci_base_object_get_logger(object);
#else // defined(SCI_LOGGING)
   return NULL;
#endif // defined(SCI_LOGGING)
}

