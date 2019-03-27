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
#ifndef _SCI_TYPES_H_
#define _SCI_TYPES_H_

/**
 * @file
 *
 * @brief This file contains all of the basic data types utilized by an
 *        SCI user or implementor.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/types.h>

#ifndef sci_cb_physical_address_upper
#error "sci_cb_physical_address_upper needs to be defined in appropriate environment.h"
#endif

#ifndef sci_cb_physical_address_lower
#error "sci_cb_physical_address_lower needs to be defined in appropriate environment.h"
#endif

#ifndef sci_cb_make_physical_address
#error "sci_cb_make_physical_address needs to be defined in appropriate environment.h"
#endif

#ifndef ASSERT
#error "ASSERT needs to be defined in appropriate environment.h or system"
#endif


/**
 * This constant defines the value utilized by SCI Components to indicate
 * an invalid handle.
 */
#define SCI_INVALID_HANDLE 0x0

/**
 * @typedef SCI_OBJECT_HANDLE_T
 * @brief   This typedef just provides an opaque handle for all SCI
 *          objects.
 */
typedef void* SCI_OBJECT_HANDLE_T;

/**
 * @typedef SCI_LOGGER_HANDLE_T
 * @brief   This typedef just provides an opaque handle for all SCI
 *          Logger objects.
 */
typedef void* SCI_LOGGER_HANDLE_T;

/**
 * @typedef SCI_IO_REQUEST_HANDLE_T
 * @brief   The SCI_IO_REQUEST_HANDLE_T will be utilized by SCI users as an
 *          opaque handle for the various SCI IO Request objects.
 */
typedef void * SCI_IO_REQUEST_HANDLE_T;

/**
 * @typedef SCI_TASK_REQUEST_HANDLE_T
 * @brief   The SCI_TASK_REQUEST_HANDLE_T will be utilized by SCI users as an
 *          opaque handle for the various SCI Task Management Request objects.
 */
typedef void * SCI_TASK_REQUEST_HANDLE_T;

/**
 * @typedef SCI_PHY_HANDLE_T
 * @brief   This typedef just provides an opaque handle for all SCI
 *          Phy objects.
 */
typedef void * SCI_PHY_HANDLE_T;

/**
 * @typedef SCI_REMOTE_DEVICE_HANDLE_T
 * @brief   The SCI_REMOTE_DEVICE_HANDLE_T will be utilized by SCI users as
 *          an opaque handle for the SCI remote device object.
 */
typedef void * SCI_REMOTE_DEVICE_HANDLE_T;

/**
 * @typedef SCI_DOMAIN_HANDLE_T
 * @brief   This typedef just provides an opaque handle for all SCI
 *          Domain objects.
 */
typedef void* SCI_DOMAIN_HANDLE_T;

/**
 * @typedef SCI_PORT_HANDLE_T
 * @brief   This typedef just provides an opaque handle for all SCI
 *          SAS or SATA Port objects.
 */
typedef void * SCI_PORT_HANDLE_T;

/**
 * @typedef SCI_MEMORY_DESCRIPTOR_LIST_HANDLE_T
 * @brief   The SCI_MEMORY_DESCRIPTOR_LIST_HANDLE_T will be utilized by SCI
 *          users as an opaque handle for the SCI MEMORY DESCRIPTOR LIST object.
 */
typedef void * SCI_MEMORY_DESCRIPTOR_LIST_HANDLE_T;

/**
 * @typedef SCI_LOCK_HANDLE_T
 * @brief   The SCI_LOCK_HANDLE_T will be utilized by SCI users as an
 *          opaque handle for the SCI LOCK object.  A lock denotes a
 *          critical code section of some form.
 */
typedef void * SCI_LOCK_HANDLE_T;

/**
 * @typedef SCI_CONTROLLER_HANDLE_T
 * @brief   The SCI_CONTROLLER_HANDLE_T will be utilized by SCI users as an
 *          opaque handle for all SCI Controller objects.
 */
typedef void * SCI_CONTROLLER_HANDLE_T;

/**
 * @typedef SCI_LIBRARY_HANDLE_T
 * @brief   The SCI_LIBRARY_HANDLE_T will be utilized by SCI users as an
 *          opaque handle for the SCI Library object.
 */
typedef void * SCI_LIBRARY_HANDLE_T;

/**
 * @typedef SCI_ITERATOR_HANDLE_T
 * @brief   The SCI_ITERATOR_T will be utilized by SCI users as an
 *          opaque handle for the SCI Iterator object.
 */
typedef void * SCI_ITERATOR_HANDLE_T;

/**
 * @typedef SCI_TIMER_CALLBACK_T
 * @brief   This callback defines the format of all other timer callback
 *          methods that are to be implemented by an SCI user, including
 *          the method that will be invoked as a result of timer expiration.
 *
 *          Parameters:
 *          - The void* value passed into the callback represents the cookie
 *            supplied by the SCI component when the timer was created.
 *
 *          Return:
 *          - None
 */
typedef void (*SCI_TIMER_CALLBACK_T)(void*);

/**
 * @brief This enumeration is provided so the SCI User can communicate the
 *        data direction for an IO request.
 */
typedef enum
{
   /**
    * The data direction for the request is in (a read operation)
    * This is also the value to use for an io request that has no specific
    * data direction.
    */
   SCI_IO_REQUEST_DATA_IN = 0,

   /**
    * The data direction for the request is out (a write operation)
    */
   SCI_IO_REQUEST_DATA_OUT,

   /**
    * There is no data transfer for the associated request.
    */
   SCI_IO_REQUEST_NO_DATA

} SCI_IO_REQUEST_DATA_DIRECTION;

/**
 * @enum  SCI_LOCK_LEVEL
 * @brief This enumeration defines the various lock levels utilized by
 *        the SCI component.  These lock levels help inform users, of the
 *        library, about what APIs must be protected from other APIs.
 *        The higher the lock level the more restricted the access.  For
 *        example, APIs specifying lock level 5 are allowed to be executed
 *        while an API of lock level 4 is on-going, but the converse is
 *        not true.
 */
typedef enum
{
   /**
    * This value indicates there is no lock level required.  This is
    * primarily utilized for situations in which there is a true critical
    * code section that merely needs to protect against access to a
    * region of memory.
    */
   SCI_LOCK_LEVEL_NONE,

   SCI_LOCK_LEVEL_1,
   SCI_LOCK_LEVEL_2,
   SCI_LOCK_LEVEL_3,
   SCI_LOCK_LEVEL_4,
   SCI_LOCK_LEVEL_5

} SCI_LOCK_LEVEL;

/**
 * @enum _SCI_CONTROLLER_MODE
 * @brief This enumeration is utilized to indicate the operating mode
 *        in which the SCI component should function.
 */
typedef enum _SCI_CONTROLLER_MODE
{
   /**
    * This enumerant specifies that the SCI component be optimized to
    * perform as fast as possible without concern for the amount of
    * memory being utilized.
    */
   SCI_MODE_SPEED,

   /**
    * This enumerant specifies that the SCI component be optimized to
    * save memory space without concern for performance of the system.
    */
   SCI_MODE_SIZE

} SCI_CONTROLLER_MODE;

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCI_TYPES_H_

