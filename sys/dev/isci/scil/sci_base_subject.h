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
#ifndef _SCI_BASE_SUBJECT_H_
#define _SCI_BASE_SUBJECT_H_

/**
 * @file
 *
 * @brief This file contains all of the structures, constants, and methods
 *        common to all subjects object definitions.  A subject is a
 *        participant in the observer pattern.  A subject represents the
 *        object being observed.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>

#if defined(SCI_LOGGING)

struct SCI_BASE_OBSERVER;

/**
 * @struct SCI_BASE_SUBJECT
 *
 * @brief This structure defines the fields common to all subjects that
 *        participate in the observer design pattern
 */
typedef struct SCI_BASE_SUBJECT
{
   struct SCI_BASE_OBSERVER *observer_list;

} SCI_BASE_SUBJECT_T;


/**
 * @brief This method acts as the basic constructor for the subject.
 *
 * @param[in] this_subject This fields specifies the subject being
 *            constructed.
 *
 * @return none
 */
void sci_base_subject_construct(
   SCI_BASE_SUBJECT_T  *this_subject
);

/**
 * @brief This method will call the update method for all
 *        observers attached to this subject.
 *
 * @param[in] this_subject This parameter specifies the subject for
 *            which to notify participating observers.
 *
 * @return none
 */
void sci_base_subject_notify(
   SCI_BASE_SUBJECT_T  *this_subject
);

/**
 * @brief This method will add an observer to the subject.
 *
 * @param[in] this_subject This parameter specifies the subject for which
 *            an observer is being added.
 * @param[in] observer This parameter specifies the observer that wishes
 *            it listen for notifications for the supplied subject.
 *
 * @return none
 */
void sci_base_subject_attach_observer(
   SCI_BASE_SUBJECT_T        *this_subject,
   struct SCI_BASE_OBSERVER  *observer
);

/**
 * @brief This method will remove the observer from the subject.
 *
 * @param[in] this_subject
 * @param[in] my_observer
 *
 * @return none
 */
void sci_base_subject_detach_observer(
   SCI_BASE_SUBJECT_T        *this_subject,
   struct SCI_BASE_OBSERVER  *my_observer
);

#else // defined(SCI_LOGGING)

typedef U8 SCI_BASE_SUBJECT_T;

#define sci_base_subject_construct
#define sci_base_subject_notify
#define sci_base_subject_attach_observer
#define sci_base_subject_detach_observer

#endif // defined(SCI_LOGGING)

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCI_BASE_SUBJECT_H_
