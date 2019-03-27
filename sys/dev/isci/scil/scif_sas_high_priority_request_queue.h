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
#ifndef _SCIF_SAS_HIGH_PRIORITY_REQUEST_QUEUE_H_
#define _SCIF_SAS_HIGH_PRIORITY_REQUEST_QUEUE_H_

/**
 * @file
 *
 * @brief This file contains all of method prototypes and type
 *        definitions specific to the high priority request queue (HPRQ).
 *        The HPRQ is the mechanism through which internal requests or
 *        other important requests created by the framework are stored.
 *        The HPRQ is checked during the scif_controller_start_io() path
 *        and is given precedence over user supplied IO requests.
 *        Additionally, when requests are created there is an attempt to
 *        start them quickly.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_base_object.h>
#include <dev/isci/scil/sci_pool.h>
#include <dev/isci/scil/scif_sas_domain.h>
#include <dev/isci/scil/scif_sas_internal_io_request.h>

typedef struct SCIF_SAS_LOCK
{
   SCI_BASE_OBJECT_T parent;

   SCI_LOCK_LEVEL    level;

} SCIF_SAS_LOCK_T;

/**
 * @struct SCIF_SAS_HIGH_PRIORITY_REQUEST_QUEUE_T
 *
 * @brief This structure depicts the fields contain in the high
 *        priority request queue (HPRQ) object.  The HPRQ is used
 *        to store IO or task requests that need to be completed
 *        in short order.
 */
typedef struct SCIF_SAS_HIGH_PRIORITY_REQUEST_QUEUE
{
   /**
    * This field specifies the necessary lock information (e.g. level)
    * that must be taken before items are added or removed from the
    * queue.
    */
   SCIF_SAS_LOCK_T  lock;

   SCI_POOL_CREATE(pool, POINTER_UINT, SCIF_SAS_MAX_INTERNAL_REQUEST_COUNT);

} SCIF_SAS_HIGH_PRIORITY_REQUEST_QUEUE_T;

void scif_sas_high_priority_request_queue_construct(
   SCIF_SAS_HIGH_PRIORITY_REQUEST_QUEUE_T * fw_hprq,
   SCI_BASE_LOGGER_T                      * logger
);

void scif_sas_high_priority_request_queue_purge_domain(
   SCIF_SAS_HIGH_PRIORITY_REQUEST_QUEUE_T * fw_hprq,
   SCIF_SAS_DOMAIN_T                      * fw_domain
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIF_SAS_HIGH_PRIORITY_REQUEST_QUEUE_H_
