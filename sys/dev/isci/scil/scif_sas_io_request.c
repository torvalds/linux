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
 * @brief This file contains the implementation of the SCIF_SAS_IO_REQUEST
 *        object.
 */


#include <dev/isci/scil/scic_io_request.h>
#include <dev/isci/scil/scic_remote_device.h>
#include <dev/isci/scil/scic_user_callback.h>
#include <dev/isci/scil/scic_controller.h>
#include <dev/isci/scil/scif_user_callback.h>

#include <dev/isci/scil/scif_sas_controller.h>
#include <dev/isci/scil/scif_sas_domain.h>
#include <dev/isci/scil/scif_sas_remote_device.h>
#include <dev/isci/scil/scif_sas_io_request.h>
#include <dev/isci/scil/scif_sas_task_request.h>
#include <dev/isci/scil/scif_sas_stp_io_request.h>
#include <dev/isci/scil/scif_sas_logger.h>
#include <dev/isci/scil/scif_sas_smp_io_request.h>
#include <dev/isci/scil/sci_fast_list.h>
#include <dev/isci/scil/sati.h>
#include <dev/isci/scil/intel_sat.h>
#include <dev/isci/scil/sati_translator_sequence.h>

/**
 * @brief This method represents common functionality for the
 *        scif_io_request_construct() and scif_sas_io_request_continue()
 *        methods.
 *
 * @return This method returns an indication as to whether the
 *         construction succeeded.
 */
static
SCI_STATUS scif_sas_io_request_construct(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCIF_SAS_IO_REQUEST_T    * fw_io,
   U16                        io_tag,
   void                     * user_io_request_object,
   SCI_IO_REQUEST_HANDLE_T  * scif_io_request,
   BOOL                       is_initial_construction
)
{
   SCI_STATUS                         status;
   SMP_DISCOVER_RESPONSE_PROTOCOLS_T  dev_protocols;

   scic_remote_device_get_protocols(fw_device->core_object, &dev_protocols);

   //Currently, all the io requests sent to smp target are internal.
   //so we fail all the external io toward to it.
   //Todo: is there a better way to handle external io to smp target?
   if (dev_protocols.u.bits.attached_smp_target)
      return SCI_FAILURE_INVALID_REMOTE_DEVICE;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_io_request_construct(0x%x,0x%x,0x%x,0x%x,0x%x,0x%x) enter\n",
      fw_device, fw_io, io_tag, user_io_request_object, scif_io_request,
      is_initial_construction
   ));

   // Initialize the users handle to the framework IO request.
   *scif_io_request = fw_io;

   // Construct the parent object first in order to ensure logging can
   // function.
   scif_sas_request_construct(
      &fw_io->parent,
      fw_device,
      sci_base_object_get_logger(fw_device),
      scif_sas_io_request_state_table
   );

   status = scic_io_request_construct(
               fw_device->domain->controller->core_object,
               fw_device->core_object,
               io_tag,
               fw_io,
               ((U8 *)fw_io) + sizeof(SCIF_SAS_IO_REQUEST_T),
               &fw_io->parent.core_object
            );

   if (status == SCI_SUCCESS)
   {
      // These associations must be set early for the core io request
      // object construction to complete correctly as there will be
      // callbacks into the user driver framework during core construction
      sci_object_set_association(fw_io, user_io_request_object);
      sci_object_set_association(fw_io->parent.core_object, fw_io);

      // Perform protocol specific core IO request construction.
      if (dev_protocols.u.bits.attached_ssp_target)
         status = scic_io_request_construct_basic_ssp(fw_io->parent.core_object);
      else if (dev_protocols.u.bits.attached_stp_target)
      {
         if (is_initial_construction == TRUE)
               sati_sequence_construct(&fw_io->parent.stp.sequence);

#if !defined(DISABLE_ATAPI)
         if (!scic_remote_device_is_atapi(fw_device->core_object))
         {
#endif
            status = scif_sas_stp_io_request_construct(fw_io);

#if !defined(DISABLE_ATAPI)
         }
         else
            status = scif_sas_stp_packet_io_request_construct(fw_io);
#endif
      }

      sci_base_state_machine_logger_initialize(
         &fw_io->parent.parent.state_machine_logger,
         &fw_io->parent.parent.state_machine,
         &fw_io->parent.parent.parent,
         scif_cb_logger_log_states,
         "SCIF_IO_REQUEST_T", "base_state_machine",
         SCIF_LOG_OBJECT_IO_REQUEST
      );
   }

   return status;
}

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

U32 scif_io_request_get_object_size(
   void
)
{
   return (sizeof(SCIF_SAS_IO_REQUEST_T) + scic_io_request_get_object_size());
}

// ----------------------------------------------------------------------------
U32 scif_io_request_get_number_of_bytes_transferred(
   SCI_IO_REQUEST_HANDLE_T  scif_io_request
)
{
   SCIF_SAS_IO_REQUEST_T * fw_request = (SCIF_SAS_IO_REQUEST_T*) scif_io_request;

   if(scic_io_request_get_protocol(scif_io_request_get_scic_handle(scif_io_request))
       == SCIC_STP_PROTOCOL)
   {
      U16 sati_data_bytes_set =
             sati_get_number_data_bytes_set(&(fw_request->parent.stp.sequence));

      if (sati_data_bytes_set != 0)
         return sati_data_bytes_set;
      else
      {
#if !defined(DISABLE_ATAPI)
         U8 sat_protocol = fw_request->parent.stp.sequence.protocol;
         if ( sat_protocol & SAT_PROTOCOL_PACKET)
            return
               scif_sas_stp_packet_io_request_get_number_of_bytes_transferred(fw_request);
         else
#endif
            return scic_io_request_get_number_of_bytes_transferred(
                      scif_io_request_get_scic_handle(scif_io_request));
      }
   }
   else
   {
      return scic_io_request_get_number_of_bytes_transferred(
                scif_io_request_get_scic_handle(scif_io_request));
   }
}

// ---------------------------------------------------------------------------

SCI_STATUS scif_io_request_construct(
   SCI_CONTROLLER_HANDLE_T      scif_controller,
   SCI_REMOTE_DEVICE_HANDLE_T   scif_remote_device,
   U16                          io_tag,
   void                       * user_io_request_object,
   void                       * io_request_memory,
   SCI_IO_REQUEST_HANDLE_T    * scif_io_request
)
{
   SCIF_SAS_IO_REQUEST_T    * fw_io     = (SCIF_SAS_IO_REQUEST_T*)
                                          io_request_memory;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                          scif_remote_device;

   return scif_sas_io_request_construct(
             fw_device,
             fw_io,
             io_tag,
             user_io_request_object,
             scif_io_request,
             TRUE
          );
}

// ---------------------------------------------------------------------------

SCI_STATUS scif_request_construct(
   SCI_CONTROLLER_HANDLE_T      scif_controller,
   SCI_REMOTE_DEVICE_HANDLE_T   scif_remote_device,
   U16                          io_tag,
   void                       * user_io_request_object,
   void                       * io_request_memory,
   SCI_IO_REQUEST_HANDLE_T    * scif_io_request
)
{
   SCIF_SAS_IO_REQUEST_T    * fw_io     = (SCIF_SAS_IO_REQUEST_T*)
                                          io_request_memory;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                          scif_remote_device;
   SCI_STATUS                 status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_io_request_construct(0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      scif_controller, scif_remote_device, io_tag, user_io_request_object,
      io_request_memory, scif_io_request
   ));

   // Step 1: Create the scif io request.
   // Initialize the users handle to the framework IO request.
   *scif_io_request = fw_io;

   // Construct the parent object first in order to ensure logging can
   // function.
   scif_sas_request_construct(
      &fw_io->parent,
      fw_device,
      sci_base_object_get_logger(fw_device),
      scif_sas_io_request_state_table
   );

   status = scic_io_request_construct(
               (void *) ((SCIF_SAS_CONTROLLER_T *)scif_controller)->core_object,
               (void *) fw_device->core_object,
               io_tag,
               fw_io,
               (U8 *)io_request_memory + sizeof(SCIF_SAS_IO_REQUEST_T),
               &fw_io->parent.core_object
            );

   if (status == SCI_SUCCESS)
   {
      // These associations must be set early for the core io request
      // object construction to complete correctly as there will be
      // callbacks into the user driver framework during core construction
      sci_object_set_association(fw_io, user_io_request_object);
      sci_object_set_association(fw_io->parent.core_object, fw_io);

      sci_base_state_machine_logger_initialize(
         &fw_io->parent.parent.state_machine_logger,
         &fw_io->parent.parent.state_machine,
         &fw_io->parent.parent.parent,
         scif_cb_logger_log_states,
         "SCIF_IO_REQUEST_T", "base_state_machine",
         SCIF_LOG_OBJECT_IO_REQUEST
      );
   }

   return status;
}

// ----------------------------------------------------------------------------

SCI_STATUS scif_io_request_construct_with_core (
   SCI_CONTROLLER_HANDLE_T      scif_controller,
   SCI_REMOTE_DEVICE_HANDLE_T   scif_remote_device,
   void                       * scic_io_request,
   void                       * user_io_request_object,
   void                       * io_request_memory,
   SCI_IO_REQUEST_HANDLE_T    * scif_io_request
)
{
   SCIF_SAS_IO_REQUEST_T    * fw_io     = (SCIF_SAS_IO_REQUEST_T*)
                                          io_request_memory;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                          scif_remote_device;
   SCI_STATUS                 status = SCI_SUCCESS;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_io_request_construct_pass_through(0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      scif_remote_device, user_io_request_object,
      io_request_memory, scif_io_request
   ));

   // Initialize the users handle to the framework IO request.
   *scif_io_request = fw_io;

   // Construct the parent object first in order to ensure logging can
   // function.
   scif_sas_request_construct(
      &fw_io->parent,
      fw_device,
      sci_base_object_get_logger(fw_device),
      scif_sas_io_request_state_table
   );

   fw_io->parent.core_object = scic_io_request;

   //set association
   sci_object_set_association(fw_io, user_io_request_object);
   sci_object_set_association(fw_io->parent.core_object, fw_io);


   sci_base_state_machine_logger_initialize(
      &fw_io->parent.parent.state_machine_logger,
      &fw_io->parent.parent.state_machine,
      &fw_io->parent.parent.parent,
      scif_cb_logger_log_states,
      "SCIF_IO_REQUEST_T", "base_state_machine",
      SCIF_LOG_OBJECT_IO_REQUEST
   );

   return status;
}

// ---------------------------------------------------------------------------

void * scif_io_request_get_response_iu_address(
   SCI_IO_REQUEST_HANDLE_T scif_io_request
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T*)scif_io_request;

   return (scic_io_request_get_response_iu_address(fw_io->parent.core_object ));
}

// ---------------------------------------------------------------------------

SCI_IO_REQUEST_HANDLE_T scif_io_request_get_scic_handle(
   SCI_IO_REQUEST_HANDLE_T scif_io_request
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T*) scif_io_request;
   return fw_io->parent.core_object;
}

// ---------------------------------------------------------------------------

void scic_cb_io_request_complete(
   SCI_CONTROLLER_HANDLE_T     controller,
   SCI_REMOTE_DEVICE_HANDLE_T  remote_device,
   SCI_IO_REQUEST_HANDLE_T     io_request,
   SCI_IO_STATUS               completion_status
)
{
   SCI_STATUS                 status;
   SCIF_SAS_CONTROLLER_T    * fw_controller = (SCIF_SAS_CONTROLLER_T*)
                                      sci_object_get_association(controller);
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = (SCIF_SAS_REMOTE_DEVICE_T*)
                                      sci_object_get_association(remote_device);
   SCIF_SAS_REQUEST_T       * fw_request = (SCIF_SAS_REQUEST_T*)
                                      sci_object_get_association(io_request);

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(controller),
      SCIF_LOG_OBJECT_IO_REQUEST,
      "scic_cb_io_request_complete(0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      controller, remote_device, io_request, completion_status
   ));

   // Invoke the common completion handler routine.
   // A non-successful return indicates we are not in a correct state to
   // receive a completion notification for this request.
   status = fw_request->state_handlers->complete_handler(&fw_request->parent);

   // If the status indicates the completion handler was successful, then
   // allow protocol specific completion processing to occur.
   if (status == SCI_SUCCESS)
   {
      if (fw_request->protocol_complete_handler != NULL)
      {
         status = fw_request->protocol_complete_handler(
                     fw_controller, fw_device, fw_request, (SCI_STATUS *)&completion_status
                  );
      }

      // If this isn't an internal framework IO request, then simply pass the
      // notification up to the SCIF user.
      if ( status == SCI_SUCCESS )
      {
         if (fw_request->is_high_priority == FALSE)
         {
            if (fw_request->is_waiting_for_abort_task_set == FALSE)
            {
               scif_cb_io_request_complete(
                  fw_controller, fw_device, fw_request, completion_status);
            }
            else
            {
               // do nothing - will complete the I/O when the abort task
               //  set completes
            }
         }
         else
            scif_sas_controller_complete_high_priority_io(
               fw_controller, fw_device, fw_request);
      }
      else if ( status == SCI_WARNING_SEQUENCE_INCOMPLETE )
      {
         scif_sas_io_request_continue(fw_controller, fw_device, fw_request);
      }
   }
}

// ---------------------------------------------------------------------------

U32 scic_cb_io_request_get_transfer_length(
   void * scic_user_io_request
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T*)
                                   scic_user_io_request;

   return (scif_cb_io_request_get_transfer_length(
             fw_io->parent.parent.parent.associated_object
           ));
}

// ---------------------------------------------------------------------------

SCI_IO_REQUEST_DATA_DIRECTION scic_cb_io_request_get_data_direction(
   void * scic_user_io_request
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T*)
                                   scic_user_io_request;

   return (scif_cb_io_request_get_data_direction(
             fw_io->parent.parent.parent.associated_object
          ));
}

// ---------------------------------------------------------------------------
#ifndef SCI_SGL_OPTIMIZATION_ENABLED
void scic_cb_io_request_get_next_sge(
   void * scic_user_io_request,
   void * current_sge_address,
   void **next_sge
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T*)
                                   scic_user_io_request;

   scif_cb_io_request_get_next_sge(
      fw_io->parent.parent.parent.associated_object,
      current_sge_address,
      next_sge
   );
}
#endif

// ---------------------------------------------------------------------------

SCI_PHYSICAL_ADDRESS scic_cb_sge_get_address_field(
   void * scic_user_io_request,
   void * sge_address
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T*)
                                   scic_user_io_request;
   return scif_cb_sge_get_address_field(
             fw_io->parent.parent.parent.associated_object, sge_address
          );
}

// ---------------------------------------------------------------------------

U32 scic_cb_sge_get_length_field(
   void * scic_user_io_request,
   void * sge_address
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T*)
                                   scic_user_io_request;

   return scif_cb_sge_get_length_field(
             fw_io->parent.parent.parent.associated_object,
             sge_address
          );
}

// ---------------------------------------------------------------------------

void * scic_cb_ssp_io_request_get_cdb_address(
   void * scic_user_io_request
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T*)
                                   scic_user_io_request;

   return scif_cb_io_request_get_cdb_address(
             fw_io->parent.parent.parent.associated_object
          );
}

// ---------------------------------------------------------------------------

U32 scic_cb_ssp_io_request_get_cdb_length(
   void * scic_user_io_request
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T*)
                                   scic_user_io_request;

   return scif_cb_io_request_get_cdb_length(
             fw_io->parent.parent.parent.associated_object
          );
}

// ---------------------------------------------------------------------------

#if !defined(DISABLE_ATAPI)
void * scic_cb_stp_packet_io_request_get_cdb_address(
   void * scic_user_io_request
)
{
   SCIF_SAS_REQUEST_T * fw_request = (SCIF_SAS_REQUEST_T*)scic_user_io_request;

   SATI_TRANSLATOR_SEQUENCE_T * sati_sequence = &fw_request->stp.sequence;

   if (sati_sequence->state != SATI_SEQUENCE_STATE_INCOMPLETE)
      return scif_cb_io_request_get_cdb_address(
                fw_request->parent.parent.associated_object
             );
   else
      return
      &(sati_sequence->command_specific_data.sati_atapi_data.request_sense_cdb);
}
#endif

// ---------------------------------------------------------------------------

#if !defined(DISABLE_ATAPI)
U32 scic_cb_stp_packet_io_request_get_cdb_length(
   void * scic_user_io_request
)
{
   SCIF_SAS_REQUEST_T * fw_request = (SCIF_SAS_REQUEST_T*)
                                   scic_user_io_request;

   SATI_TRANSLATOR_SEQUENCE_T * sati_sequence = &fw_request->stp.sequence;

   if (sati_sequence->state != SATI_SEQUENCE_STATE_INCOMPLETE)
      return scif_cb_io_request_get_cdb_length(
                fw_request->parent.parent.associated_object
             );
   else
      return SATI_ATAPI_REQUEST_SENSE_CDB_LENGTH;
}
#endif

// ---------------------------------------------------------------------------

U32 scic_cb_ssp_io_request_get_lun(
   void * scic_user_io_request
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T*)
                                   scic_user_io_request;

   return scif_cb_io_request_get_lun(
             fw_io->parent.parent.parent.associated_object
          );
}

// ---------------------------------------------------------------------------

U32 scic_cb_ssp_io_request_get_task_attribute(
   void * scic_user_io_request
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T*)
                                   scic_user_io_request;

   return scif_cb_io_request_get_task_attribute(
             fw_io->parent.parent.parent.associated_object
          );
}

// ---------------------------------------------------------------------------

U32 scic_cb_ssp_io_request_get_command_priority(
   void * scic_user_io_request
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T*)
                                   scic_user_io_request;

   return scif_cb_io_request_get_command_priority(
             fw_io->parent.parent.parent.associated_object
          );
}

// ---------------------------------------------------------------------------

BOOL scic_cb_request_is_initial_construction(
   void * scic_user_io_request
)
{
   SCIF_SAS_REQUEST_T * fw_request = (SCIF_SAS_REQUEST_T*)
                                   scic_user_io_request;
   SCIF_SAS_REMOTE_DEVICE_T* fw_device = fw_request->device;

   SMP_DISCOVER_RESPONSE_PROTOCOLS_T  dev_protocols;
   scic_remote_device_get_protocols(fw_device->core_object, &dev_protocols);

   if (dev_protocols.u.bits.attached_stp_target
       && fw_request->stp.sequence.state == SATI_SEQUENCE_STATE_INCOMPLETE)
      return FALSE;

   return TRUE;
}


//******************************************************************************
//* P R O T E C T E D   M E T H O D S
//******************************************************************************
/**
 * @brief This method constructs an scif sas smp request.
 *
 * @param[in] fw_controller The framework controller
 * @param[in] fw_device The smp device that the smp request targets to.
 * @param[in] fw_io_memory The memory space for the smp request.
 * @param[in] core_io_memory The memory space for the core request.
 * @param[in] io_tag The io tag for the internl io to be constructed.
 * @param[in] smp_command A pointer to the smp request data structure according
 *       to SAS protocol.
 *
 * @return Indicate if the internal io was successfully constructed.
 * @retval SCI_SUCCESS This value is returned if the internal io was
 *         successfully constructed.
 * @retval SCI_FAILURE This value is returned if the internal io was failed to
 *         be constructed.
 */
SCI_STATUS scif_sas_io_request_construct_smp(
   SCIF_SAS_CONTROLLER_T       * fw_controller,
   SCIF_SAS_REMOTE_DEVICE_T    * fw_device,
   void                        * fw_io_memory,
   void                        * core_io_memory,
   U16                           io_tag,
   SMP_REQUEST_T               * smp_command,
   void                        * user_request_object
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io =
     (SCIF_SAS_IO_REQUEST_T*)fw_io_memory;

   SCI_STATUS status = SCI_SUCCESS;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_io_request_construct_smp(0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      fw_controller,
      fw_device,
      fw_io_memory,
      core_io_memory,
      io_tag,
      smp_command,
      user_request_object
   ));

   // Construct the parent object first in order to ensure logging can
   // function.
   scif_sas_request_construct(
      &fw_io->parent,
      fw_device,
      sci_base_object_get_logger(fw_controller),
      scif_sas_io_request_state_table
   );

   status = scic_io_request_construct(
               fw_device->domain->controller->core_object,
               fw_device->core_object,
               io_tag,
               (void*)fw_io,
               (U8 *)core_io_memory,
               &fw_io->parent.core_object
            );

   if (status == SCI_SUCCESS)
   {
      //set object association.
      sci_object_set_association(fw_io, user_request_object);
      sci_object_set_association(fw_io->parent.core_object, fw_io);

      scif_sas_smp_request_construct(&fw_io->parent, smp_command);

      fw_io->parent.is_high_priority = TRUE;

      sci_base_state_machine_logger_initialize(
         &fw_io->parent.parent.state_machine_logger,
         &fw_io->parent.parent.state_machine,
         &fw_io->parent.parent.parent,
         scif_cb_logger_log_states,
         "SCIF_IO_REQUEST_T", "base_state_machine",
         SCIF_LOG_OBJECT_IO_REQUEST
      );
   }

   return status;
}


/**
 * @brief This method continues a scif sas request.
 *
 * @param[in] fw_controller The framework controller
 * @param[in] fw_device The device that the IO request targets to.
 * @param[in] fw_request The IO request to be continued.
 *
 * @return Indicate if the internal io was successfully constructed.
 * @retval SCI_SUCCESS This value is returned if the internal io was
 *         successfully continued.
 * @retval SCI_FAILURE This value is returned if the io was failed to
 *         be continued.
 */
SCI_STATUS scif_sas_io_request_continue(
   SCIF_SAS_CONTROLLER_T       * fw_controller,
   SCIF_SAS_REMOTE_DEVICE_T    * fw_device,
   SCIF_SAS_REQUEST_T          * fw_request
)
{
   SCI_IO_REQUEST_HANDLE_T dummy_handle;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_request),
      SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_io_request_continue(0x%x, 0x%x, 0x%x) enter\n",
      fw_controller,
      fw_device,
      fw_request
   ));

   //complete this io request in framework and core.
   scif_controller_complete_io(fw_controller, fw_device, fw_request);

   //construct next command in the sequence using the same memory. We pass
   //a dummy pointer to let the framework user keep the pointer to this IO
   //request untouched.
   scif_sas_io_request_construct(
      fw_device,
      (SCIF_SAS_IO_REQUEST_T*)fw_request,
      SCI_CONTROLLER_INVALID_IO_TAG,
      (void *)sci_object_get_association(fw_request),
      &dummy_handle,
      FALSE
   );

   //start the new constructed IO.
   return (SCI_STATUS)scif_controller_start_io(
             (SCI_CONTROLLER_HANDLE_T) fw_controller,
             (SCI_REMOTE_DEVICE_HANDLE_T) fw_device,
             (SCI_IO_REQUEST_HANDLE_T) fw_request,
             SCI_CONTROLLER_INVALID_IO_TAG
          );
}
