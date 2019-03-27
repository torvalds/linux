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
#ifndef _SCIC_SDS_LIBRARY_H_
#define _SCIC_SDS_LIBRARY_H_

/**
 * @file
 *
 * @brief This file contains the structures used by the core library object.
 *        All of the functionality for the core library is in the
 *        sci_base_library.h file.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_base_library.h>
#include <dev/isci/scil/scic_sds_pci.h>

// Forward declar the controllers
struct SCIC_SDS_CONTROLLER;

/**
 * @struct SCIC_SDS_LIBRARY
 *
 * This structure contains data used by the core library.
 */
typedef struct SCIC_SDS_LIBRARY
{
  /*
   * The SCI_BASE_LIBRARY is the parent object for the SCIC_SDS_LIBRARY
   * object.
   */
   SCI_BASE_LIBRARY_T parent;

   /**
    * This is the count of the maximum number of controllers that this library
    * can contain.
    */
   U32 max_controller_count;

   /**
    * The PCI header for this library object all libraries must have the same
    * pci device id.
    */
   U16 pci_device;
   U8  pci_revision;

   /**
    * This field is the array of controllers that are contained within the
    * library object.
    */
   struct SCIC_SDS_CONTROLLER *controllers;

} SCIC_SDS_LIBRARY_T;

U8 scic_sds_library_get_controller_index(
   struct SCIC_SDS_LIBRARY    * library,
   struct SCIC_SDS_CONTROLLER * controller
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_SDS_LIBRARY_H_
