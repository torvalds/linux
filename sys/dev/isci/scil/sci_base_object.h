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
#ifndef _SCI_BASE_OBJECT_H_
#define _SCI_BASE_OBJECT_H_

/**
 * @file
 *
 * @brief This file contains all of the method and constants associated with
 *        the SCI base object.  The SCI base object is the class from which
 *        all other objects derive in the Storage Controller Interface.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_object.h>

// Forward declare the logger object
struct SCI_BASE_LOGGER;

/**
 * @struct SCI_BASE_OBJECT
 *
 * @brief The SCI_BASE_OBJECT object represents the data and functionality
 *        that is common to all SCI objects.  It is the base class.
 */
typedef struct SCI_BASE_OBJECT
{
   /**
    * This field represents an association created by the user for this
    * object.  The association can be whatever the user wishes.  Think of
    * it as a cookie.
    */
   void * associated_object;

   /**
    * This field simply contains a handle to the logger object to be
    * utilized when utilizing the logger interface.
    */
   struct SCI_BASE_LOGGER * logger;

} SCI_BASE_OBJECT_T;


/**
 * @brief This method constructs the sci base object.
 *
 * @param[in]  base_object This parameter specifies the SCI base
 *              object which we whish to construct.
 * @param[in]  logger This parameter specifies the logger object to be
 *             saved and utilized for this base object.
 *
 * @return none
 */
void sci_base_object_construct(
   SCI_BASE_OBJECT_T      * base_object,
   struct SCI_BASE_LOGGER * logger
);

#if defined(SCI_LOGGING)
/**
 * @brief This method returns the logger to which a previous
 *         association was created.
 *
 * @param[in]  base_object This parameter specifies the SCI base object for
 *             which to retrieve the logger.
 *
 * @return This method returns a pointer to the logger that was
 *          previously associated to the supplied base_object
 *          parameter.
 * @retval NULL This value is returned when there is no logger
 *         association for the supplied base_object instance.
 */
#define sci_base_object_get_logger(this_object) \
   (((SCI_BASE_OBJECT_T *)(this_object))->logger)

#else // defined(SCI_LOGGING)

#define sci_base_object_get_logger(this_object) NULL

#endif // defined(SCI_LOGGING)

#ifdef __cplusplus
}
#endif // __cplusplus


#endif // _SCI_BASE_OBJECT_H_

