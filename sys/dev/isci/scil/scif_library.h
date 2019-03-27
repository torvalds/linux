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
#ifndef _SCIF_LIBRARY_H_
#define _SCIF_LIBRARY_H_

/**
 * @file
 *
 * @brief This file contains all of the interface methods that can be called
 *        by an SCI Framework user on the library object.  The library is
 *        the container of all other objects being managed (i.e. controllers,
 *        target devices, sas ports, etc.) by SCIF.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_status.h>


/**
 * @brief This method will contsruct the SCI framework library based on the
 *        supplied parameter information.  By default, libraries are
 *        considered "ready" as soon as they are constructed.
 *
 * @param[in]  library_memory_p a pointer to the memory at which the
 *             library object is located.
 * @param[in]  max_controller_count the maximum number of controllers that
 *             this library can manage.
 *
 * @return An opaque library handle to be used by the SCI user for all
 *         subsequent library operations.
 */
SCI_LIBRARY_HANDLE_T scif_library_construct(
   void * library_memory_p,
   U8     max_controller_count
);

/**
 * @brief This method returns the size of the framework library object.  The
 *        size of the framework library object includes the associated core
 *        object.
 *
 * @param[in]  max_controller_count the maximum number of controllers that
 *             this library can manage.
 *
 * @return a positive integer value indicating the size (in bytes) of the
 *         library object.
 */
U32 scif_library_get_object_size(
   U8  max_controller_count
);

/**
 * @brief This method will allocate the next available framework controller
 *        object that can be managed by this framework library.
 *
 * @see For additional information please refer to:
 *      scic_library_allocate_controller()
 *
 * @param[in]  library the handle to the library object for which to allocate
 *             a controller.
 * @param[out] new_controller_p This parameter specifies a pointer to the
 *             controller handle that was added to the library.
 *
 * @return Indicate if the controller was successfully allocated or if iti
 *         failed in some way.
 * @retval SCI_SUCCESS if the controller was successfully allocated.
 * @retval SCI_FAILURE_INSUFFICIENT_RESOURCES if the library has no more
 *         available controller objects to allocate.
 */
SCI_STATUS scif_library_allocate_controller(
   SCI_LIBRARY_HANDLE_T      library,
   SCI_CONTROLLER_HANDLE_T * new_controller_p
);

/**
 * @brief This method will attempt to free the supplied controller to the
 *        library.
 *
 * @param[in]  library the handle to the library object for which to free
 *             a controller.
 * @param[in]  controller the handle to the controller object to be freed
 *             from the library.
 *
 * @return Indicate if the controller was successfully freed or if it failed
 *         in some way.
 * @retval SCI_SUCCESS if the controller was successfully freed.
 * @retval SCI_FAILURE_CONTROLLER_NOT_FOUND if the supplied controller is
 *         not managed by the supplied library.
 */
SCI_STATUS scif_library_free_controller(
   SCI_LIBRARY_HANDLE_T     library,
   SCI_CONTROLLER_HANDLE_T  controller
);


/**
 * @brief This method returns the SCI Core library handle
 *        associated with this library.
 *
 * @param[in]  scif_library the handle to the library
 *             object for which to retrieve the core specific
 *             library handle
 *
 * @return Return the SCI core library handle associated with
 *         the supplied framework library.
 */
SCI_LIBRARY_HANDLE_T scif_library_get_scic_handle(
   SCI_LIBRARY_HANDLE_T   scif_library
);


/**
 * @brief This method returns the minimum number of timers needed.  If the
 *        user supplies timers less then the number specified via this
 *        call, then the user runs the risk of improper operation.  This
 *        call includes the minimum number of timers needed by the core.
 *
 * @return This method returns a value representing the minimum number of
 *         timers required by this framework implementation
 */
U16 scif_library_get_min_timer_count(
   void
);

/**
 * @brief This method returns the maximum number of timers that could
 *        be ever be in use by this component at a given time.
 *
 * @return This method returns a value representing the minimum number of
 *         timers required by this framework implementation
 */
U16 scif_library_get_max_timer_count(
   void
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIF_LIBRARY_H_

