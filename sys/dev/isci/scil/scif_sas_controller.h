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
#ifndef _SCIF_SAS_CONTROLLER_H_
#define _SCIF_SAS_CONTROLLER_H_

/**
 * @file
 *
 * @brief This file contains the protected interface structures, constants,
 *        and methods for the SCIF_SAS_CONTROLLER object.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_abstract_list.h>
#include <dev/isci/scil/sci_controller_constants.h>
#include <dev/isci/scil/sci_memory_descriptor_list.h>
#include <dev/isci/scil/sci_base_controller.h>
#include <dev/isci/scil/scif_controller.h>
#include <dev/isci/scil/scif_config_parameters.h>
#include <dev/isci/scil/scif_sas_domain.h>
#include <dev/isci/scil/scif_sas_io_request.h>
#include <dev/isci/scil/scif_sas_task_request.h>
#include <dev/isci/scil/scif_sas_constants.h>
#include <dev/isci/scil/sci_pool.h>
#include <dev/isci/scil/scif_sas_internal_io_request.h>
#include <dev/isci/scil/scif_sas_high_priority_request_queue.h>
#include <dev/isci/scil/scif_sas_smp_phy.h>


// Currently there is only a need for 1 memory descriptor.  This descriptor
// describes the internal IO request memory.
#define SCIF_SAS_MAX_MEMORY_DESCRIPTORS 1

enum _SCIF_SAS_MAX_MEMORY_DESCRIPTORS
{
   SCIF_SAS_MDE_INTERNAL_IO = 0

};

/**
 * @struct SCIF_SAS_CONTROLLER
 *
 * @brief The SCI SAS Framework controller object abstracts storage controller
 *        level behavior for the framework component.
 */
typedef struct SCIF_SAS_CONTROLLER
{
   /**
    * The SCI_BASE_CONTROLLER is the parent object for the SCIF_SAS_CONTROLLER
    * object.
    */
   SCI_BASE_CONTROLLER_T  parent;

   /**
    * This field contains the handle for the SCI Core controller object that
    * is managed by this framework controller.
    */
   SCI_CONTROLLER_HANDLE_T  core_object;

   /**
    * This field references the list of state specific handler methods to
    * be utilized for this controller instance.
    */
   SCI_BASE_CONTROLLER_STATE_HANDLER_T * state_handlers;

   /**
    * This field contains the memory desciptors defining the physical
    * memory requirements for this controller.
    */
   SCI_PHYSICAL_MEMORY_DESCRIPTOR_T mdes[SCIF_SAS_MAX_MEMORY_DESCRIPTORS];

   /**
    * This field contains the SAS domain objects managed by this controller.
    */
   SCIF_SAS_DOMAIN_T  domains[SCI_MAX_DOMAINS];

   /**
    * This field represents the pool of available remote device objects
    * supported by the controller.
    */
   SCI_ABSTRACT_ELEMENT_POOL_T  free_remote_device_pool;

   /**
    * This field contains the maximum number of abstract elements that
    * can be placed in the pool.
    */
   SCI_ABSTRACT_ELEMENT_T  remote_device_pool_elements[SCI_MAX_REMOTE_DEVICES];

   /**
    * This field provides the controller object a scratch area to indicate
    * status of an ongoing operation.
    */
   SCI_STATUS  operation_status;

   /**
    * This field will contain an user specified parameter information
    * to be utilized by the framework.
    */
   SCIF_USER_PARAMETERS_T user_parameters;

   /**
    * This field records the index for the current domain to clear affiliation
    * EA SATA remote devices, during the controller stop process.
    */
   U8 current_domain_to_clear_affiliation;

   U32 internal_request_entries;

   /**
    * This field provides a pool to manage the memory resource for all internal
    * requests.
    * requests.
    */
   SCI_POOL_CREATE(
      internal_request_memory_pool,
      POINTER_UINT,
      SCIF_SAS_MAX_INTERNAL_REQUEST_COUNT
   );

   /**
    * This field provides a queue for built internal requests waiting to be
    * started.
    */
   SCIF_SAS_HIGH_PRIORITY_REQUEST_QUEUE_T  hprq;

   /**
    * This represents the number of available SMP phy objects that can
    * be managed by the framework.
    */
   SCIF_SAS_SMP_PHY_T smp_phy_array[SCIF_SAS_SMP_PHY_COUNT];

   /**
    * This field provides a list to manage the memory resource for all
    * smp_phy objects.
    */
   SCI_FAST_LIST_T smp_phy_memory_list;

#if !defined(DISABLE_INTERRUPTS)
   /**
    * This field saves the interrupt coalescing count before changing interrupt
    * coalescence.
    */
   U16 saved_interrupt_coalesce_number;

   /**
    * This field saves the interrupt coalescing timeout values in micorseconds
    * before changing interrupt coalescence.
    */
   U32 saved_interrupt_coalesce_timeout;
#endif // !defined(DISABLE_INTERRUPTS)

} SCIF_SAS_CONTROLLER_T;

extern SCI_BASE_STATE_T scif_sas_controller_state_table[];
extern SCI_BASE_CONTROLLER_STATE_HANDLER_T
   scif_sas_controller_state_handler_table[];

SCI_STATUS scif_sas_controller_continue_io(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_IO_REQUEST_HANDLE_T     io_request
);

void scif_sas_controller_destruct(
   SCIF_SAS_CONTROLLER_T * fw_controller
);

void * scif_sas_controller_allocate_internal_request(
   SCIF_SAS_CONTROLLER_T * fw_controller
);

void scif_sas_controller_free_internal_request(
   SCIF_SAS_CONTROLLER_T * fw_controller,
   void                  * fw_internal_request_buffer
);

void scif_sas_controller_start_high_priority_io(
   SCIF_SAS_CONTROLLER_T * fw_controller
);

BOOL scif_sas_controller_sufficient_resource(
   SCIF_SAS_CONTROLLER_T *fw_controller
);

SCI_STATUS scif_sas_controller_complete_high_priority_io(
   SCIF_SAS_CONTROLLER_T    * fw_controller,
   SCIF_SAS_REMOTE_DEVICE_T * remote_device,
   SCIF_SAS_REQUEST_T       * io_request
);

SCIF_SAS_SMP_PHY_T * scif_sas_controller_allocate_smp_phy(
   SCIF_SAS_CONTROLLER_T * fw_controller
);

void scif_sas_controller_free_smp_phy(
   SCIF_SAS_CONTROLLER_T * fw_controller,
   SCIF_SAS_SMP_PHY_T    * smp_phy
);

SCI_STATUS scif_sas_controller_clear_affiliation(
   SCIF_SAS_CONTROLLER_T * fw_controller
);

SCI_STATUS scif_sas_controller_continue_to_stop(
   SCIF_SAS_CONTROLLER_T * fw_controller
);

void scif_sas_controller_set_default_config_parameters(
   SCIF_SAS_CONTROLLER_T * this_controller
);

SCI_STATUS scif_sas_controller_release_resource(
   SCIF_SAS_CONTROLLER_T * fw_controller
);

void scif_sas_controller_build_mdl(
   SCIF_SAS_CONTROLLER_T * fw_controller
);

#if !defined(DISABLE_INTERRUPTS)

void scif_sas_controller_save_interrupt_coalescence(
   SCIF_SAS_CONTROLLER_T * fw_controller
);

void scif_sas_controller_restore_interrupt_coalescence(
   SCIF_SAS_CONTROLLER_T * fw_controller
);

#else // !defined(DISABLE_INTERRUPTS)
#define scif_sas_controller_save_interrupt_coalescence(controller)
#define scif_sas_controller_restore_interrupt_coalescence(controller)
#endif // !defined(DISABLE_INTERRUPTS)

#ifdef SCI_LOGGING
void scif_sas_controller_initialize_state_logging(
   SCIF_SAS_CONTROLLER_T *this_controller
);

void scif_sas_controller_deinitialize_state_logging(
   SCIF_SAS_CONTROLLER_T *this_controller
);
#else // SCI_LOGGING
#define scif_sas_controller_initialize_state_logging(x)
#define scif_sas_controller_deinitialize_state_logging(x)
#endif // SCI_LOGGING

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIF_SAS_CONTROLLER_H_

