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
#ifndef _SCIF_SAS_DOMAIN_H_
#define _SCIF_SAS_DOMAIN_H_

/**
 * @file
 *
 * @brief This file contains the protected interface structures, constants,
 *        and methods for the SCIF_SAS_DOMAIN object.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_abstract_list.h>
#include <dev/isci/scil/sci_fast_list.h>
#include <dev/isci/scil/scif_domain.h>

#include <dev/isci/scil/sci_base_domain.h>
#include <dev/isci/scil/scif_sas_request.h>
#include <dev/isci/scil/scif_sas_task_request.h>
#include <dev/isci/scil/scif_sas_remote_device.h>


extern SCI_BASE_DOMAIN_STATE_HANDLER_T scif_sas_domain_state_handler_table[];
extern SCI_BASE_STATE_T scif_sas_domain_state_table[];

#define PORT_HARD_RESET_TIMEOUT 1000 //1000 miliseconds

#define SCIF_DOMAIN_DISCOVER_TIMEOUT 20000 // miliseconds

/**
 * @struct SCIF_SAS_DOMAIN
 *
 * @brief The SCI SAS Framework domain object abstracts the SAS domain
 *        level behavior for the framework component.  Additionally,
 *        it provides a higher level of abstraction for the core port
 *        object.  There is a 1:1 correspondance between core ports and
 *        framework domain objects.  Essentially, each core port provides
 *        the access to the remote devices in the domain.
 */
typedef struct SCIF_SAS_DOMAIN
{
   /**
    * The SCI_BASE_DOMAIN is the parent object for the SCIF_SAS_DOMAIN
    * object.
    */
   SCI_BASE_DOMAIN_T  parent;

   /**
    * This field contains the handle for the SCI Core port object that
    * is managed by this framework domain object.
    */
   SCI_PORT_HANDLE_T  core_object;

   /**
    * This field specifies the controller containing this domain object.
    */
   struct SCIF_SAS_CONTROLLER * controller;

   /**
    * This field references the list of state specific handler methods to
    * be utilized for this domain instance.
    */
   SCI_BASE_DOMAIN_STATE_HANDLER_T * state_handlers;

   /**
    * This field contains references to all of the devices contained in
    * this domain.
    */
   SCI_ABSTRACT_LIST_T  remote_device_list;

   /**
    * This field contains the list of all outstanding request (IO or
    * management) in this domain.
    */
   SCI_FAST_LIST_T  request_list;

   /**
    * This field indicates whether the core port object is in a ready state
    * or not.
    */
   BOOL  is_port_ready;

   /**
    * This field indicates the number of remote devices that have been
    * started in this domain.
    */
   U32  device_start_count;

   /**
    * This field indicates the number of remote devices that are currently
    * in the process of becoming ready.  This field is utilized to gate
    * the transition back to the READY state for the domain.
    */
   U32  device_start_in_progress_count;

   /**
    * This field records how many broadcast change primitve are
    * received and not processed yet.
    */
   U32  broadcast_change_count;

   /**
    * This fields indicates whether the expanders in this domain need to
    * have there config route table configured by our driver. For expample,
    * if we found the top level expander is a self-configuring expander and
    * it is able to config others, all the expanders in this domain don't
    * need to configure route table.
    */
   BOOL is_config_route_table_needed;

   struct
   {
      /**
       * This field provides the domain object a scratch area to indicate
       * status of an ongoing operation.
       */
      SCI_STATUS  status;

      /**
       * This is the timer handle that is utilized to time the discovery
       * or domain reset operations.
       */
      void * timer;

      /**
       * This field specifies the timeout value, in milliseconds, for the
       * entire operation (discovery or reset).
       */
      U32 timeout;

      /**
       * This field specifies the timeout value, in milliseconds, for a
       * single device.
       */
      U32 device_timeout;

   } operation;

} SCIF_SAS_DOMAIN_T;

void scif_sas_domain_construct(
   SCIF_SAS_DOMAIN_T          * fw_domain,
   U8                           domain_id,
   struct SCIF_SAS_CONTROLLER * fw_controller
);

void scif_sas_domain_terminate_requests(
   SCIF_SAS_DOMAIN_T        * fw_domain,
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCIF_SAS_REQUEST_T       * fw_request,
   SCIF_SAS_TASK_REQUEST_T  * fw_requestor
);

SCIF_SAS_REQUEST_T * scif_sas_domain_get_request_by_io_tag(
   SCIF_SAS_DOMAIN_T * fw_domain,
   U16                 io_tag
);

void scif_sas_domain_transition_to_stopped_state(
   SCIF_SAS_DOMAIN_T * fw_domain
);

void scif_sas_domain_initialize(
   SCIF_SAS_DOMAIN_T * fw_domain
);

void scif_sas_domain_remote_device_start_complete(
   SCIF_SAS_DOMAIN_T        * fw_domain,
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
);

BOOL scif_sas_domain_is_in_smp_activity(
   SCIF_SAS_DOMAIN_T        * fw_domain
);

SCIF_SAS_REMOTE_DEVICE_T * scif_sas_domain_get_device_by_containing_device(
   SCIF_SAS_DOMAIN_T        * fw_domain,
   SCIF_SAS_REMOTE_DEVICE_T * containing_device,
   U8                         expander_phy_id
);

SCIF_SAS_REMOTE_DEVICE_T * scif_sas_domain_find_device_in_spinup_hold(
   SCIF_SAS_DOMAIN_T        * fw_domain
);

SCIF_SAS_REMOTE_DEVICE_T * scif_sas_domain_find_device_has_scheduled_activity(
   SCIF_SAS_DOMAIN_T        * fw_domain,
   U8                         smp_activity
);

void scif_sas_domain_start_smp_activity(
  SCIF_SAS_DOMAIN_T        * fw_domain
);

void scif_sas_domain_remove_expander_device(
   SCIF_SAS_DOMAIN_T        * fw_domain,
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
);

void scif_sas_domain_start_smp_discover(
   SCIF_SAS_DOMAIN_T        * fw_domain,
   SCIF_SAS_REMOTE_DEVICE_T * top_expander
);

void scif_sas_domain_continue_discover(
   SCIF_SAS_DOMAIN_T * fw_domain
);

void scif_sas_domain_finish_discover(
   SCIF_SAS_DOMAIN_T * fw_domain
);

void scif_sas_domain_transition_to_discovering_state(
   SCIF_SAS_DOMAIN_T * fw_domain
);

void scif_sas_domain_cancel_smp_activities(
   SCIF_SAS_DOMAIN_T * fw_domain
);

U8 scif_sas_domain_get_smp_request_count(
   SCIF_SAS_DOMAIN_T * fw_domain
);

void scif_sas_domain_schedule_clear_affiliation(
   SCIF_SAS_DOMAIN_T * fw_domain
);

void scif_sas_domain_start_clear_affiliation(
   SCIF_SAS_DOMAIN_T * fw_domain
);

void scif_sas_domain_continue_clear_affiliation(
   SCIF_SAS_DOMAIN_T * fw_domain
);

void scif_sas_domain_release_resource(
   struct SCIF_SAS_CONTROLLER * fw_controller,
   SCIF_SAS_DOMAIN_T     * fw_domain
);

SCIF_SAS_REMOTE_DEVICE_T * scif_sas_domain_find_next_ea_target_reset(
   SCIF_SAS_DOMAIN_T * fw_domain
);

#if !defined(DISABLE_WIDE_PORTED_TARGETS)
void scif_sas_domain_update_device_port_width(
   SCIF_SAS_DOMAIN_T     * fw_domain,
   SCI_PORT_HANDLE_T      port
);
#else  //!defined(DISABLE_WIDE_PORTED_TARGETS)
#define scif_sas_domain_update_device_port_width(domain, port)
#endif //!defined(DISABLE_WIDE_PORTED_TARGETS)

#ifdef SCI_LOGGING
void scif_sas_domain_initialize_state_logging(
   SCIF_SAS_DOMAIN_T *fw_domain
);

void scif_sas_domain_deinitialize_state_logging(
   SCIF_SAS_DOMAIN_T *fw_domain
);
#else // SCI_LOGGING
#define scif_sas_domain_initialize_state_logging(x)
#define scif_sas_domain_deinitialize_state_logging(x)
#endif // SCI_LOGGING

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIF_SAS_DOMAIN_H_

