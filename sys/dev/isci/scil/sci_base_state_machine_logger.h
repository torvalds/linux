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
#ifndef _SCI_BASE_STATE_MACHINE_LOGGER_H_
#define _SCI_BASE_STATE_MACHINE_LOGGER_H_

/**
 * @file
 *
 * @brief This file provides the structures and function prototypes for the
 *        state machine logger.  The functions provided are only implemented
 *        if the SCI_LOGGING flag is enabled.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_base_object.h>
#include <dev/isci/scil/sci_base_state_machine.h>
#include <dev/isci/scil/sci_base_state_machine_observer.h>

/**
 * This type is defined so we can pass either a core or framework logging
 * function to the state machine logger since both have the same prototypes
 * and this base state machine logger does not actually know which component
 * will be doing the logging.
 */
typedef void (*SCI_BASE_STATE_MACHINE_LOGGER_LOG_HANDLER_T)(
   SCI_LOGGER_HANDLE_T, U32, char *, ...
);

#if defined(SCI_LOGGING)

typedef struct SCI_BASE_STATE_MACHINE_LOGGER
{
   SCI_BASE_STATE_MACHINE_OBSERVER_T parent;

   SCI_BASE_OBJECT_T                           * log_object;
   SCI_BASE_STATE_MACHINE_LOGGER_LOG_HANDLER_T   log_function;
   char                                        * log_object_name;
   char                                        * log_state_machine_name;
   U32                                           log_mask;

} SCI_BASE_STATE_MACHINE_LOGGER_T;


void sci_base_state_machine_logger_construct(
   SCI_BASE_STATE_MACHINE_LOGGER_T             * this_observer,
   SCI_BASE_OBJECT_T                           * the_object,
   SCI_BASE_STATE_MACHINE_LOGGER_LOG_HANDLER_T   the_log_function,
   char                                        * log_object_name,
   char                                        * log_state_machine_name,
   U32                                           log_object_mask
);

void sci_base_state_machine_logger_initialize(
   SCI_BASE_STATE_MACHINE_LOGGER_T             * this_observer,
   SCI_BASE_STATE_MACHINE_T                    * the_state_machine,
   SCI_BASE_OBJECT_T                           * the_object,
   SCI_BASE_STATE_MACHINE_LOGGER_LOG_HANDLER_T   the_log_function,
   char                                        * log_object_name,
   char                                        * log_state_machine_name,
   U32                                           log_object_mask
);

void sci_base_state_machine_logger_deinitialize(
   SCI_BASE_STATE_MACHINE_LOGGER_T * this_observer,
   SCI_BASE_STATE_MACHINE_T        * the_state_machine
);

#else // SCI_LOGGING

typedef U8 SCI_BASE_STATE_MACHINE_LOGGER_T;

#define sci_base_state_machine_logger_construct(this_observer,the_object,the_log_function,log_object_name,log_state_machine_name,log_object_mask)
#define sci_base_state_machine_logger_initialize(this_observer,the_state_machine,the_object,the_log_function,log_object_name,log_state_machine_name,log_object_mask)
#define sci_base_state_machine_logger_deinitialize(this_observer, the_state_machine)

#endif // SCI_LOGGING

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCI_BASE_STATE_MACHINE_LOGGER_H_
