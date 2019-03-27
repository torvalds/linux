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
 * @brief This file contains the methods for the SCIF_SAS_SMP_REMOTE_DEVICE object.
 */
#include <dev/isci/scil/sci_controller.h>
#include <dev/isci/scil/scif_sas_controller.h>
#include <dev/isci/scil/scif_sas_remote_device.h>
#include <dev/isci/scil/scif_sas_logger.h>

#include <dev/isci/scil/scif_sas_smp_remote_device.h>
#include <dev/isci/scil/scif_sas_smp_io_request.h>
#include <dev/isci/scil/intel_sas.h>
#include <dev/isci/scil/scic_io_request.h>
#include <dev/isci/scil/scic_remote_device.h>
#include <dev/isci/scil/scif_sas_smp_phy.h>


/**
 * @brief This method resets all fields for a smp remote device. This is a
 *        private method.
 *
 * @param[in] fw_device the framework SMP device that is being
 *            constructed.
 *
 * @return none
 */
void scif_sas_smp_remote_device_clear(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   //reset all fields in smp_device, indicate that the smp device is not
   //in discovery process.
   fw_device->protocol_device.smp_device.current_activity =
      SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_NONE;

   fw_device->protocol_device.smp_device.current_smp_request =
      NOT_IN_SMP_ACTIVITY;

   fw_device->protocol_device.smp_device.current_activity_phy_index = 0;

   fw_device->protocol_device.smp_device.curr_config_route_index = 0;

   fw_device->protocol_device.smp_device.config_route_smp_phy_anchor = NULL;

   fw_device->protocol_device.smp_device.is_route_table_cleaned = FALSE;

   fw_device->protocol_device.smp_device.curr_config_route_destination_smp_phy = NULL;

   fw_device->protocol_device.smp_device.scheduled_activity =
      SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_NONE;

   fw_device->protocol_device.smp_device.io_retry_count = 0;

   fw_device->protocol_device.smp_device.curr_clear_affiliation_phy = NULL;

   if (fw_device->protocol_device.smp_device.smp_activity_timer != NULL)
   {
      //stop the timer
      scif_cb_timer_stop(
         fw_device->domain->controller,
         fw_device->protocol_device.smp_device.smp_activity_timer
      );

      //destroy the timer
      scif_cb_timer_destroy(
         fw_device->domain->controller,
         fw_device->protocol_device.smp_device.smp_activity_timer
      );

      fw_device->protocol_device.smp_device.smp_activity_timer = NULL;
   }
}


/**
 * @brief This method intializes a smp remote device.
 *
 * @param[in] fw_device the framework SMP device that is being
 *            constructed.
 *
 * @return none
 */
void scif_sas_smp_remote_device_construct(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "scif_sas_smp_remote_device_construct(0x%x) enter\n",
      fw_device
   ));

   fw_device->protocol_device.smp_device.number_of_phys = 0;
   fw_device->protocol_device.smp_device.expander_route_indexes = 0;
   fw_device->protocol_device.smp_device.is_table_to_table_supported = FALSE;
   fw_device->protocol_device.smp_device.is_externally_configurable  = FALSE;
   fw_device->protocol_device.smp_device.is_able_to_config_others    = FALSE;

   sci_fast_list_init(&fw_device->protocol_device.smp_device.smp_phy_list);

   scif_sas_smp_remote_device_clear(fw_device);
}


/**
 * @brief This method decodes a smp response to this smp device and then
 *        continue the smp discover process.
 *
 * @param[in] fw_device The framework device that a SMP response targets to.
 * @param[in] fw_request The pointer to an smp request whose response
 *       is to be decoded.
 * @param[in] response_data The response data passed in.
 *
 * @return none
 */
SCI_STATUS scif_sas_smp_remote_device_decode_smp_response(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCIF_SAS_REQUEST_T       * fw_request,
   void                     * response_data,
   SCI_IO_STATUS              completion_status
)
{
   SMP_RESPONSE_T * smp_response = (SMP_RESPONSE_T *)response_data;
   SCI_STATUS       status       = SCI_FAILURE_UNSUPPORTED_INFORMATION_TYPE;

   if (fw_device->protocol_device.smp_device.smp_activity_timer != NULL)
   {
      //if there is a timer being used, recycle it now. Since we may
      //use the timer for other purpose next.
      scif_cb_timer_destroy(
         fw_device->domain->controller,
         fw_device->protocol_device.smp_device.smp_activity_timer
      );

      fw_device->protocol_device.smp_device.smp_activity_timer = NULL;
   }

   //if Core set the status of this io to be RETRY_REQUIRED, we should
   //retry the IO without even decode the response.
   if (completion_status == SCI_FAILURE_RETRY_REQUIRED)
   {
      scif_sas_smp_remote_device_continue_current_activity(
         fw_device, fw_request, SCI_FAILURE_RETRY_REQUIRED
      );

      return SCI_FAILURE_RETRY_REQUIRED;
   }

   //check the current smp request, decide what's next smp request to issue.
   switch (fw_device->protocol_device.smp_device.current_smp_request)
   {
      case SMP_FUNCTION_REPORT_GENERAL:
      {
         //interpret REPORT GENERAL response.
         status = scif_sas_smp_remote_device_decode_report_general_response(
            fw_device, smp_response
         );

         break;
      }

      case SMP_FUNCTION_REPORT_MANUFACTURER_INFORMATION:
      {
         // No need to perform any parsing.  Just want to see
         // the information in a trace if necessary.
         status = SCI_SUCCESS;
         break;
      }

      case SMP_FUNCTION_DISCOVER:
      {
         if (fw_device->protocol_device.smp_device.current_activity ==
                SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_DISCOVER)
         {
            //decode discover response
            status = scif_sas_smp_remote_device_decode_initial_discover_response(
                        fw_device, smp_response
                     );
         }
         else if (fw_device->protocol_device.smp_device.current_activity ==
                  SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_TARGET_RESET)
         {
            //decode discover response as a polling result for a remote device
            //target reset.
            status =
               scif_sas_smp_remote_device_decode_target_reset_discover_response(
                  fw_device, smp_response
               );
         }
         else if (fw_device->protocol_device.smp_device.current_activity ==
                SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_SATA_SPINUP_HOLD_RELEASE)
         {
            //decode discover response
            status =
               scif_sas_smp_remote_device_decode_spinup_hold_release_discover_response(
                  fw_device, smp_response
               );
         }
         else
            ASSERT(0);
         break;
      }

      case SMP_FUNCTION_REPORT_PHY_SATA:
      {
         //decode the report phy sata response.
         status = scif_sas_smp_remote_device_decode_report_phy_sata_response(
            fw_device, smp_response
         );

         break;
      }

      case SMP_FUNCTION_PHY_CONTROL:
      {
         if (fw_device->protocol_device.smp_device.current_activity ==
                SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_DISCOVER)
         {
            //decode the phy control response.
            status = scif_sas_smp_remote_device_decode_discover_phy_control_response(
                        fw_device, smp_response
                     );
         }
         else if (fw_device->protocol_device.smp_device.current_activity ==
                     SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_TARGET_RESET)
         {
            //decode discover response as a polling result for a remote device
            //target reset.
            status = scif_sas_smp_remote_device_decode_target_reset_phy_control_response(
                        fw_device, smp_response
                     );
         }
         else if (fw_device->protocol_device.smp_device.current_activity ==
                     SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_CLEAR_AFFILIATION)
         {
            //currently don't care about the status.
            status = SCI_SUCCESS;
         }
         else
            ASSERT(0);
         break;
      }

      case SMP_FUNCTION_CONFIGURE_ROUTE_INFORMATION:
      {
         //Note, currently we don't expect any abnormal status from config route info response,
         //but there is a possibility that we exceed the maximum route index. We will take care
         //of errors later.
         status = scif_sas_smp_remote_device_decode_config_route_info_response(
                     fw_device, smp_response
                  );
         break;
      }

      default:
         //unsupported case, TBD
         status = SCI_FAILURE_UNSUPPORTED_INFORMATION_TYPE;
         break;
   } //end of switch

   //Continue current activity based on response's decoding status.
   scif_sas_smp_remote_device_continue_current_activity(
      fw_device, fw_request, status
   );

   return status;
}


/**
 * @brief This method decodes a smp Report Genernal response to this smp device
 *        and then continue the smp discover process.
 *
 * @param[in] fw_device The framework device that the REPORT GENERAL command
 *       targets to.
 * @param[in] report_general_response The pointer to a report general response
 *
 * @return none
 */
SCI_STATUS scif_sas_smp_remote_device_decode_report_general_response(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SMP_RESPONSE_T           * smp_response
)
{
   SMP_RESPONSE_REPORT_GENERAL_T * report_general_response =
      &smp_response->response.report_general;

   SMP_RESPONSE_HEADER_T * response_header = &smp_response->header;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_decode_report_general_response(0x%x, 0x%x) enter\n",
      fw_device, smp_response
   ));

   if (response_header->function_result != SMP_RESULT_FUNCTION_ACCEPTED)
   {
      /// @todo: more decoding work needed when the function_result is not
      /// SMP_RESULT_FUNCTION_ACCEPTED. Retry might be the option for some
      /// function result.
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
         "Report General function result(0x%x)\n",
         response_header->function_result
      ));

      return SCI_FAILURE;
   }

   //get info from report general response.
   fw_device->protocol_device.smp_device.number_of_phys =
      (U8)report_general_response->number_of_phys;

   //currently there is byte swap issue in U16 data.
   fw_device->protocol_device.smp_device.expander_route_indexes =
      ((report_general_response->expander_route_indexes & 0xff) << 8) |
      ((report_general_response->expander_route_indexes & 0xff00) >> 8);

   fw_device->protocol_device.smp_device.is_table_to_table_supported =
      (BOOL)report_general_response->table_to_table_supported;

   fw_device->protocol_device.smp_device.is_externally_configurable =
      (BOOL)report_general_response->configurable_route_table;

   fw_device->protocol_device.smp_device.is_able_to_config_others =
      (BOOL)report_general_response->configures_others;

   //If the top level expander of a domain is able to configure others,
   //no config route table is needed in the domain. Or else,
   //we'll let all the externally configurable expanders in the damain
   //configure route table.
   if (fw_device->containing_device == NULL
       && ! fw_device->protocol_device.smp_device.is_able_to_config_others)
      fw_device->domain->is_config_route_table_needed = TRUE;

   //knowing number of phys this expander has, we can allocate all the smp phys for
   //this expander now if it is not done already.
   if (fw_device->protocol_device.smp_device.smp_phy_list.element_count == 0)
      scif_sas_smp_remote_device_populate_smp_phy_list(fw_device);

   if (report_general_response->configuring)
      return SCI_FAILURE_RETRY_REQUIRED;

   return SCI_SUCCESS;
}


/**
 * @brief This method decodes a smp Discover response to this smp device
 *        and then continue the smp discover process. This is only ever
 *        called for the very first discover stage during a given domain
 *        discovery process.
 *
 * @param[in] fw_device The framework device that the DISCOVER command
 *       targets to.
 * @param[in] discover_response The pointer to a DISCOVER response
 *
 * @return none
 */
SCI_STATUS scif_sas_smp_remote_device_decode_initial_discover_response(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SMP_RESPONSE_T           * smp_response
)
{
   SCIF_SAS_DOMAIN_T        * fw_domain = fw_device->domain;
   SCI_SAS_ADDRESS_T          attached_device_address;
   SCIF_SAS_REMOTE_DEVICE_T * attached_remote_device;
   SMP_RESPONSE_DISCOVER_T  * discover_response =
      &smp_response->response.discover;
   SMP_RESPONSE_HEADER_T    * response_header = &smp_response->header;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_decode_initial_discover_response(0x%x, 0x%x) enter\n",
      fw_device, smp_response
   ));

   if (response_header->function_result == SMP_RESULT_PHY_VACANT)
   {
      return SCI_SUCCESS;
   }
   else if (response_header->function_result != SMP_RESULT_FUNCTION_ACCEPTED)
   {
      /// @todo: more decoding work needed when the function_result is not
      /// SMP_RESULT_FUNCTION_ACCEPTED. Retry might be the option for some
      /// function result.
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
         "Discover function result(0x%x)\n",
         response_header->function_result
      ));

      return SCI_FAILURE;
   }

   //only if there is target device attached. We don't add device that is
   //initiator only.
   if ( ( discover_response->u2.sas1_1.attached_device_type
             != SMP_NO_DEVICE_ATTACHED )
       && ( discover_response->protocols.u.bits.attached_ssp_target
           || discover_response->protocols.u.bits.attached_stp_target
           || discover_response->protocols.u.bits.attached_smp_target
           || discover_response->protocols.u.bits.attached_sata_device ) )
   {
      attached_device_address = discover_response->attached_sas_address;

      attached_remote_device = (SCIF_SAS_REMOTE_DEVICE_T *)
         scif_domain_get_device_by_sas_address(
            fw_domain, &attached_device_address
         );

      //need to check if the device already existed in the domian.
      if (attached_remote_device != SCI_INVALID_HANDLE)
      {
#if !defined(DISABLE_WIDE_PORTED_TARGETS)
         if ( attached_remote_device->is_currently_discovered == TRUE
             && attached_remote_device != fw_device->containing_device )
         {
            //a downstream wide port target is found.
            attached_remote_device->device_port_width++;
         }
         else
#endif //#if !defined(DISABLE_WIDE_PORTED_TARGETS)
         {
            //The device already existed. Mark the device as discovered.
            attached_remote_device->is_currently_discovered = TRUE;
         }

#if !defined(DISABLE_WIDE_PORTED_TARGETS)
         if (attached_remote_device->device_port_width !=
                scic_remote_device_get_port_width(attached_remote_device->core_object)
             && discover_response->protocols.u.bits.attached_ssp_target
            )
         {
            scif_sas_remote_device_update_port_width(
               attached_remote_device, attached_remote_device->device_port_width);
         }
#endif //#if !defined(DISABLE_WIDE_PORTED_TARGETS)

         if ( discover_response->protocols.u.bits.attached_smp_target
             && attached_remote_device != fw_device->containing_device)
         {
            //another expander device is discovered. Its own smp discover will starts after
            //this discover finishes.
            attached_remote_device->protocol_device.smp_device.scheduled_activity =
               SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_DISCOVER;
         }
      }
      else
      {
         //report the discovery of a disk for all types of end device.
         scif_cb_domain_ea_device_added(
            fw_domain->controller, fw_domain, fw_device, discover_response
         );

         //get info from discover response to see what we found. And do
         //extra work according to end device's protocol type.
         if ( discover_response->protocols.u.bits.attached_ssp_target
             || discover_response->protocols.u.bits.attached_smp_target)
         {
            //for SSP or SMP target, no extra work.
            ;
         }
         else if (  (discover_response->protocols.u.bits.attached_stp_target)
                 || (discover_response->protocols.u.bits.attached_sata_device) )
         {
            // We treat a SATA Device bit the same as an attached STP
            // target.
            discover_response->protocols.u.bits.attached_stp_target = 1;

            //kick off REPORT PHY SATA to the same phy.
            fw_device->protocol_device.smp_device.current_smp_request =
               SMP_FUNCTION_REPORT_PHY_SATA;
         }
      }
   }
   else if( (discover_response->u2.sas1_1.negotiated_physical_link_rate == SCI_SATA_SPINUP_HOLD
             || discover_response->u4.sas2.negotiated_physical_link_rate == SCI_SATA_SPINUP_HOLD)
          &&(discover_response->protocols.u.bits.attached_stp_target
             || discover_response->protocols.u.bits.attached_sata_device)
          )
   {
      attached_remote_device = scif_sas_domain_get_device_by_containing_device(
                                  fw_domain,
                                  fw_device,
                                  discover_response->phy_identifier
                               );

      if (attached_remote_device != SCI_INVALID_HANDLE)
      {
         //Here, the only reason a device already existed in domain but
         //the initial discover rersponse shows it in SPINUP_HOLD, is that
         //a device has been removed and coming back in SPINUP_HOLD before
         //we detected. The possibility of this situation is very very rare.
         //we need to remove the device then add it back using the new
         //discover response.
         scif_cb_domain_device_removed(
            fw_domain->controller, fw_domain, attached_remote_device
         );
      }

      discover_response->protocols.u.bits.attached_stp_target = 1;

      //still report ea_device_added(). But this device will not be
      //started during scif_remote_device_ea_construct().
      scif_cb_domain_ea_device_added(
         fw_domain->controller, fw_domain, fw_device, discover_response
      );

      //need to send Phy Control (RESET) to release the phy from spinup hold
      //condition.
      fw_device->protocol_device.smp_device.current_smp_request =
         SMP_FUNCTION_PHY_CONTROL;
   }

   //update the smp phy info based on this DISCOVER response.
   return scif_sas_smp_remote_device_save_smp_phy_info(
             fw_device, discover_response);
}


/**
 * @brief This method decodes a smp Report Phy Sata response to this
 *        smp device and then continue the smp discover process.
 *
 * @param[in] fw_device The framework device that the REPORT PHY SATA
 *       command targets to.
 * @param[in] report_phy_sata_response The pointer to a REPORT PHY
 *       SATA response
 *
 * @return none
 */
SCI_STATUS scif_sas_smp_remote_device_decode_report_phy_sata_response(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SMP_RESPONSE_T           * smp_response
)
{
   SMP_RESPONSE_REPORT_PHY_SATA_T * report_phy_sata_response =
      &smp_response->response.report_phy_sata;

   SMP_RESPONSE_HEADER_T * response_header = &smp_response->header;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_decode_report_phy_sata_response(0x%x, 0x%x) enter\n",
      fw_device, smp_response
   ));

   if (response_header->function_result != SMP_RESULT_FUNCTION_ACCEPTED)
   {
      /// @todo: more decoding work needed when the function_result is not
      /// SMP_RESULT_FUNCTION_ACCEPTED. Retry might be the option for some
      /// function result.
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
         "Report Phy Sata function result(0x%x)\n",
         response_header->function_result
      ));

      return SCI_FAILURE;
   }

   scif_sas_remote_device_save_report_phy_sata_information(
      report_phy_sata_response
   );

   // continue the discover process.
   fw_device->protocol_device.smp_device.current_smp_request =
      SMP_FUNCTION_DISCOVER;

   return SCI_SUCCESS;
}


/**
 * @brief This method decodes a smp Phy Control response to this smp device and
 *        then continue the smp TARGET RESET process.
 *
 * @param[in] fw_device The framework device that the Phy Control command
 *       targets to.
 * @param[in] smp_response The pointer to a Phy Control response
 * @param[in] fw_io The scif IO request that associates to this smp response.
 *
 * @return none
 */
SCI_STATUS scif_sas_smp_remote_device_decode_target_reset_phy_control_response(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SMP_RESPONSE_T           * smp_response
)
{
   SMP_RESPONSE_HEADER_T * response_header = &smp_response->header;

   SCI_STATUS status = SCI_SUCCESS;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_decode_target_reset_phy_control_response(0x%x, 0x%x) enter\n",
      fw_device, smp_response
   ));

   if (response_header->function_result != SMP_RESULT_FUNCTION_ACCEPTED)
   {
      /// @todo: more decoding work needed when the function_result is not
      /// SMP_RESULT_FUNCTION_ACCEPTED. Retry might be the option for some
      /// function result.
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
         "Phy Control function unaccepted result(0x%x)\n",
         response_header->function_result
      ));

      status = SCI_FAILURE_RETRY_REQUIRED;
   }

   // phy Control succeeded.
   return status;
}

/**
 * @brief This method decodes a smp Phy Control response to this smp device and
 *        then continue the smp DISCOVER process.
 *
 * @param[in] fw_device The framework device that the Phy Control command
 *       targets to.
 * @param[in] smp_response The pointer to a Phy Control response
 *
 * @return Almost always SCI_SUCCESS
 */
SCI_STATUS scif_sas_smp_remote_device_decode_discover_phy_control_response(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SMP_RESPONSE_T           * smp_response
)
{
   SMP_RESPONSE_HEADER_T * response_header = &smp_response->header;

   SCI_STATUS status = SCI_SUCCESS;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_decode_discover_phy_control_response(0x%x, 0x%x) enter\n",
      fw_device, smp_response
   ));

   if (response_header->function_result != SMP_RESULT_FUNCTION_ACCEPTED)
   {
      /// @todo: more decoding work needed when the function_result is not
      /// SMP_RESULT_FUNCTION_ACCEPTED. Retry might be the option for some
      /// function result.
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
         "Phy Control function unaccepted result(0x%x)\n",
         response_header->function_result
      ));

      return SCI_FAILURE_RETRY_REQUIRED;
   }

   // continue the discover process.
   fw_device->protocol_device.smp_device.current_smp_request =
      SMP_FUNCTION_DISCOVER;

   // phy Control succeeded.
   return status;
}


/**
 * @brief This method decodes a smp Discover response to this smp device
 *        and then continue the smp discover process.
 *
 * @param[in] fw_device The framework device that the DISCOVER command
 *       targets to.
 * @param[in] discover_response The pointer to a DISCOVER response
 *
 * @return none
 */
SCI_STATUS scif_sas_smp_remote_device_decode_target_reset_discover_response(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SMP_RESPONSE_T           * smp_response
)
{
   SCIF_SAS_DOMAIN_T  * fw_domain;
   SCI_SAS_ADDRESS_T attached_device_address;
   SMP_RESPONSE_DISCOVER_T * discover_response =
      &smp_response->response.discover;

   SMP_RESPONSE_HEADER_T * response_header = &smp_response->header;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_decode_target_reset_discover_response(0x%x, 0x%x) enter\n",
      fw_device, smp_response
   ));

   if (response_header->function_result != SMP_RESULT_FUNCTION_ACCEPTED)
   {
      /// @todo: more decoding work needed when the function_result is not
      /// SMP_RESULT_FUNCTION_ACCEPTED. Retry might be the option for some
      /// function result.
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
         "Discover function result(0x%x)\n",
         response_header->function_result
      ));

      return SCI_FAILURE_RETRY_REQUIRED;
   }

   //only if there is device attached.
   if ( discover_response->u2.sas1_1.attached_device_type != SMP_NO_DEVICE_ATTACHED )
   {
      fw_domain = fw_device->domain;
      attached_device_address = discover_response->attached_sas_address;

      // the device should have already existed in the domian.
      ASSERT(scif_domain_get_device_by_sas_address(
                fw_domain,
                &attached_device_address
             ) != SCI_INVALID_HANDLE);
      return SCI_SUCCESS;
   }
   else
      return SCI_FAILURE_RETRY_REQUIRED;
}

/**
 * @brief This method decodes a smp Discover response to this smp device
 *        for SPINUP_HOLD_RELEASE activity. If a DISCOVER response says
 *        SATA DEVICE ATTACHED and has a valid NPL value, we call fw_device's
 *        start_handler(). But if a DISCOVER response still shows SPINUP
 *        in NPL state, we need to return retry_required status
 *
 * @param[in] fw_device The framework device that the DISCOVER command
 *       targets to.
 * @param[in] discover_response The pointer to a DISCOVER response
 *
 * @return SCI_SUCCESS
 *         SCI_FAILURE_RETRY_REQUIRED
 */
SCI_STATUS scif_sas_smp_remote_device_decode_spinup_hold_release_discover_response(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SMP_RESPONSE_T           * smp_response
)
{
   SMP_RESPONSE_DISCOVER_T * discover_response = &smp_response->response.discover;

   SMP_RESPONSE_HEADER_T * response_header = &smp_response->header;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_decode_spinup_hold_release_discover_response(0x%x, 0x%x) enter\n",
      fw_device, smp_response
   ));

   if (response_header->function_result != SMP_RESULT_FUNCTION_ACCEPTED)
   {
      /// @todo: more decoding work needed when the function_result is not
      /// SMP_RESULT_FUNCTION_ACCEPTED. Retry might be the option for some
      /// function result.
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
         "Discover function result(0x%x)\n",
         response_header->function_result
      ));

      return SCI_FAILURE;
   }

   if ( discover_response->u2.sas1_1.attached_device_type != SMP_NO_DEVICE_ATTACHED )
   {
      if (discover_response->u2.sas1_1.negotiated_physical_link_rate != SCI_SATA_SPINUP_HOLD
          && discover_response->u4.sas2.negotiated_physical_link_rate != SCI_SATA_SPINUP_HOLD
          && ( discover_response->protocols.u.bits.attached_stp_target
             ||discover_response->protocols.u.bits.attached_sata_device )
         )
      {
         SCIF_SAS_REMOTE_DEVICE_T * target_device =
            scif_sas_domain_get_device_by_containing_device(
               fw_device->domain,
               fw_device,
               fw_device->protocol_device.smp_device.current_activity_phy_index
            );

         //Need to update the device's connection rate. Its connection rate was SPINIP_HOLD.
         scic_remote_device_set_max_connection_rate(
            target_device->core_object,
            discover_response->u2.sas1_1.negotiated_physical_link_rate
         );

         //Need to update the smp phy info too.
         scif_sas_smp_remote_device_save_smp_phy_info(
             fw_device, discover_response);

         //This device has already constructed, only need to call start_handler
         //of this device here.
         return target_device->state_handlers->parent.start_handler(
                   &target_device->parent );
      }
      else
         return SCI_FAILURE_RETRY_REQUIRED;
   }
   else
      return SCI_FAILURE_RETRY_REQUIRED;
}


/**
 * @brief This method decodes a smp CONFIG ROUTE INFO response to this smp
 *        device and then continue to config route table.
 *
 * @param[in] fw_device The framework device that the CONFIG ROUTE INFO command
 *       targets to.
 * @param[in] smp_response The pointer to a CONFIG ROUTE INFO response
 *
 * @return none
 */
SCI_STATUS scif_sas_smp_remote_device_decode_config_route_info_response(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SMP_RESPONSE_T           * smp_response
)
{
   SMP_RESPONSE_HEADER_T * response_header = &smp_response->header;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_decode_config_route_info_response(0x%x, 0x%x) enter\n",
      fw_device, smp_response
   ));

   if (response_header->function_result == SMP_RESULT_INDEX_DOES_NOT_EXIST)
   {
      //case of exceeding max route index. We need to remove the devices that are not
      //able to be edit to route table. The destination config route smp phy
      //is used to remove devices.
      scif_sas_smp_remote_device_cancel_config_route_table_activity(fw_device);

      return SCI_FAILURE_EXCEED_MAX_ROUTE_INDEX;
   }
   else if (response_header->function_result != SMP_RESULT_FUNCTION_ACCEPTED)
   {
      /// @todo: more decoding work needed when the function_result is not
      /// SMP_RESULT_FUNCTION_ACCEPTED. Retry might be the option for some
      /// function result.
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
         "Discover function result(0x%x)\n",
         response_header->function_result
      ));

      return SCI_FAILURE;
   }

   return SCI_SUCCESS;
}


/**
 * @brief This method starts the smp Discover process for an expander by
 *        sending Report General request.
 *
 * @param[in] fw_device The framework smp device that a  command
 *       targets to.
 *
 * @return none
 */
void scif_sas_smp_remote_device_start_discover(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = fw_device->domain->controller;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_start_discover(0x%x) enter\n",
      fw_device
   ));

   //For safety, clear the device again, there may be some config route table
   //related info are not cleared yet.
   scif_sas_smp_remote_device_clear(fw_device);

   //set current activity
   fw_device->protocol_device.smp_device.current_activity =
      SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_DISCOVER;

   //Set current_smp_request to REPORT GENERAL.
   fw_device->protocol_device.smp_device.current_smp_request =
      SMP_FUNCTION_REPORT_GENERAL;

   //reset discover_to_start flag.
   fw_device->protocol_device.smp_device.scheduled_activity =
      SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_NONE;

   //build the first smp request Report Genernal.
   scif_sas_smp_request_construct_report_general(fw_controller, fw_device);

   //issue DPC to start this request.
   scif_cb_start_internal_io_task_schedule(
      fw_controller,
      scif_sas_controller_start_high_priority_io,
      fw_controller
   );
}


/**
 * @brief This method continues the smp Discover process.
 *
 * @param[in] fw_device The framework smp device that a DISCOVER command
 *       targets to.
 * @param[in] fw_request The pointer to an smp request whose response
 *       has been decoded.
 * @param[in] status The decoding status of the smp request's response
 *
 * @return none
 */
void scif_sas_smp_remote_device_continue_current_activity(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCIF_SAS_REQUEST_T       * fw_request,
   SCI_STATUS                 status
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T *)fw_request;
   // save the retry count.
   U8 io_retry_count = fw_io->retry_count;

   if (fw_request->is_internal)
   {
      // Complete this internal io request now. We want to free this io before
      // we create another SMP request, which is going to happen soon.
      scif_sas_internal_io_request_complete(
         fw_device->domain->controller,
         (SCIF_SAS_INTERNAL_IO_REQUEST_T *)fw_request,
         SCI_SUCCESS
      );
   }

   if (fw_device->protocol_device.smp_device.current_activity ==
      SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_DISCOVER)
   {
      if (status == SCI_SUCCESS)
      {   //continue the discover process.
         scif_sas_smp_remote_device_continue_discover(fw_device);
      }
      else if (status == SCI_FAILURE_RETRY_REQUIRED)
      {
         //Retry the smp request. Since we are in the middle of Discover
         //process, all the smp requests are internal. A new smp request
         //will be created for retry.
         U32 retry_wait_duration = (SCIF_DOMAIN_DISCOVER_TIMEOUT / 2) / SCIF_SAS_IO_RETRY_LIMIT;

         if (io_retry_count < SCIF_SAS_IO_RETRY_LIMIT)
            scif_sas_smp_remote_device_retry_internal_io (
               fw_device, io_retry_count, retry_wait_duration);
         else
            scif_sas_smp_remote_device_fail_discover(fw_device);
      }
      else if (status == SCI_FAILURE_ILLEGAL_ROUTING_ATTRIBUTE_CONFIGURATION)
      {
         //remove this expander device and its child devices. No need to
         //continue the discover on this device.
         scif_sas_domain_remove_expander_device(fw_device->domain, fw_device);

         //continue the domain's smp discover.
         scif_sas_domain_continue_discover(fw_device->domain);
      }
      else
      {  //terminate the discover process.
         scif_sas_smp_remote_device_fail_discover(fw_device);
      }
   }
   else if (fw_device->protocol_device.smp_device.current_activity ==
      SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_TARGET_RESET)
   {
      if (status == SCI_SUCCESS)
      {   //continue the target reset process.
         scif_sas_smp_remote_device_continue_target_reset(
            fw_device, fw_request);
      }
      else if (status == SCI_FAILURE_RETRY_REQUIRED)
      {
         //Retry the same smp request. Since we are in the middle of Target
         //reset process, all the smp requests are using external resource.
         //We will use the exactly same memory to retry.
         if (io_retry_count < SCIF_SAS_IO_RETRY_LIMIT)
         {
            if (fw_device->protocol_device.smp_device.smp_activity_timer == NULL)
            {
               //create the timer to wait before retry.
               fw_device->protocol_device.smp_device.smp_activity_timer =
                  scif_cb_timer_create(
                  (SCI_CONTROLLER_HANDLE_T *)fw_device->domain->controller,
                  (SCI_TIMER_CALLBACK_T)scif_sas_smp_external_request_retry,
                  (void*)fw_request
               );
            }
            else
            {
               ASSERT(0);
            }

            //start the timer to wait
            scif_cb_timer_start(
               (SCI_CONTROLLER_HANDLE_T)fw_device->domain->controller,
               fw_device->protocol_device.smp_device.smp_activity_timer,
               SMP_REQUEST_RETRY_WAIT_DURATION  //20 miliseconds
            );
         }
         else
            scif_sas_smp_remote_device_fail_target_reset(fw_device, fw_request);
      }
      else
         //terminate the discover process.
         scif_sas_smp_remote_device_fail_target_reset(fw_device, fw_request);
   }
   else if (fw_device->protocol_device.smp_device.current_activity ==
      SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_SATA_SPINUP_HOLD_RELEASE)
   {
      SCIF_SAS_REMOTE_DEVICE_T * target_device =
         scif_sas_domain_get_device_by_containing_device(
            fw_device->domain,
            fw_device,
            fw_device->protocol_device.smp_device.current_activity_phy_index
         );

      if (status == SCI_SUCCESS)
      {
         //move on to next round of SPINUP_HOLD_REALSE activity.
         scif_sas_smp_remote_device_sata_spinup_hold_release(fw_device);
      }
      else if (status == SCI_FAILURE_RETRY_REQUIRED)
      {
         U32 delay =
            (scic_remote_device_get_suggested_reset_timeout(target_device->core_object) /
                SCIF_SAS_IO_RETRY_LIMIT);

         //Retry the smp request. Since we are in the middle of Discover
         //process, all the smp requests are internal. A new smp request
         //will be created for retry.
         if (io_retry_count < SCIF_SAS_IO_RETRY_LIMIT)
         {
            scif_sas_smp_remote_device_retry_internal_io(
               fw_device, io_retry_count, delay);
         }
         else //give up on this target device.
         {
            scif_sas_smp_remote_device_fail_target_spinup_hold_release(
               fw_device , target_device);
         }
      }
      else //give up on this target device.
        scif_sas_smp_remote_device_fail_target_spinup_hold_release(
           fw_device, target_device);
   }
   else if (fw_device->protocol_device.smp_device.current_activity ==
      SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_CONFIG_ROUTE_TABLE)
   {
      SCI_FAST_LIST_ELEMENT_T * next_phy_element = sci_fast_list_get_next(
         &(fw_device->protocol_device.smp_device.curr_config_route_destination_smp_phy->list_element) );

      SCI_FAST_LIST_T * destination_smp_phy_list =
          fw_device->protocol_device.smp_device.curr_config_route_destination_smp_phy->list_element.owning_list;

      SCIF_SAS_SMP_PHY_T * next_phy_in_wide_port = NULL;

      if (next_phy_element != NULL
          && status != SCI_FAILURE_EXCEED_MAX_ROUTE_INDEX)
      {
         fw_device->protocol_device.smp_device.curr_config_route_index++;

         fw_device->protocol_device.smp_device.curr_config_route_destination_smp_phy =
            (SCIF_SAS_SMP_PHY_T *)sci_fast_list_get_object(next_phy_element);

         // Update the anchor for config route index.
         fw_device->protocol_device.smp_device.config_route_smp_phy_anchor->config_route_table_index_anchor =
            fw_device->protocol_device.smp_device.curr_config_route_index;

         scif_sas_smp_remote_device_configure_route_table(fw_device);
      }
      else if ( scif_sas_smp_remote_device_get_config_route_table_method(fw_device)
                   == SCIF_SAS_CONFIG_ROUTE_TABLE_ALL_PHYS
                && (next_phy_in_wide_port = scif_sas_smp_phy_find_next_phy_in_wide_port(
                       fw_device->protocol_device.smp_device.config_route_smp_phy_anchor)
                   )!= NULL
              )
      {
         //config the other phy in the same wide port
         fw_device->protocol_device.smp_device.config_route_smp_phy_anchor =
            next_phy_in_wide_port;

         fw_device->protocol_device.smp_device.current_activity_phy_index =
            fw_device->protocol_device.smp_device.config_route_smp_phy_anchor->phy_identifier;

         fw_device->protocol_device.smp_device.curr_config_route_destination_smp_phy =
            sci_fast_list_get_head(destination_smp_phy_list);

         if (fw_device->protocol_device.smp_device.config_route_smp_phy_anchor->config_route_table_index_anchor != 0)
            fw_device->protocol_device.smp_device.curr_config_route_index =
               fw_device->protocol_device.smp_device.config_route_smp_phy_anchor->config_route_table_index_anchor + 1;
         else
            fw_device->protocol_device.smp_device.curr_config_route_index = 0;

         scif_sas_smp_remote_device_configure_route_table(fw_device);
      }
      else if ( fw_device->protocol_device.smp_device.is_route_table_cleaned == FALSE)
      {
         fw_device->protocol_device.smp_device.current_activity =
            SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_CLEAN_ROUTE_TABLE;

         scif_sas_smp_remote_device_clean_route_table(fw_device);
      }
      else
      {
         //set this device's activity to NON.
         fw_device->protocol_device.smp_device.current_activity =
            SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_NONE;

         //we need to notify domain that this device finished config route table, domain
         //may pick up other activities (i.e. Discover) for other expanders.
         scif_sas_domain_continue_discover(fw_device->domain);
      }
   }
   else if (fw_device->protocol_device.smp_device.current_activity ==
               SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_CLEAN_ROUTE_TABLE)
   {
      scif_sas_smp_remote_device_clean_route_table(fw_device);
   }
   else if (fw_device->protocol_device.smp_device.current_activity ==
               SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_CLEAR_AFFILIATION)
   {
      scif_sas_smp_remote_device_continue_clear_affiliation(fw_device);
   }
}


/**
 * @brief This method continues the smp Discover process.
 *
 * @param[in] fw_device The framework smp device that a DISCOVER command
 *       targets to.
 *
 * @return none
 */
void scif_sas_smp_remote_device_continue_discover(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = fw_device->domain;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_continue_discover(0x%x) enter\n",
      fw_device
   ));

   switch (fw_device->protocol_device.smp_device.current_smp_request)
   {
      case SMP_FUNCTION_REPORT_GENERAL:
         // send the REPORT MANUFACTURER_INFO request
         fw_device->protocol_device.smp_device.current_smp_request =
            SMP_FUNCTION_REPORT_MANUFACTURER_INFORMATION;

         scif_sas_smp_request_construct_report_manufacturer_info(
            fw_domain->controller, fw_device
         );

         break;

      case SMP_FUNCTION_REPORT_MANUFACTURER_INFORMATION:
         //send the first SMP DISCOVER request.
         fw_device->protocol_device.smp_device.current_activity_phy_index = 0;
         fw_device->protocol_device.smp_device.current_smp_request =
            SMP_FUNCTION_DISCOVER;

         scif_sas_smp_request_construct_discover(
            fw_domain->controller,
            fw_device,
            fw_device->protocol_device.smp_device.current_activity_phy_index,
            NULL, NULL
         );
         break;


      case SMP_FUNCTION_DISCOVER:
         fw_device->protocol_device.smp_device.current_activity_phy_index++;

         if ( (fw_device->protocol_device.smp_device.current_activity_phy_index <
                  fw_device->protocol_device.smp_device.number_of_phys) )
         {
            scif_sas_smp_request_construct_discover(
               fw_domain->controller,
               fw_device,
               fw_device->protocol_device.smp_device.current_activity_phy_index,
               NULL, NULL
            );
         }
         else
            scif_sas_smp_remote_device_finish_initial_discover(fw_device);
         break;


      case SMP_FUNCTION_REPORT_PHY_SATA:
         scif_sas_smp_request_construct_report_phy_sata(
            fw_device->domain->controller,
            fw_device,
            fw_device->protocol_device.smp_device.current_activity_phy_index
         );

         break;


      case SMP_FUNCTION_PHY_CONTROL:
         scif_sas_smp_request_construct_phy_control(
            fw_device->domain->controller,
            fw_device,
            PHY_OPERATION_HARD_RESET,
            fw_device->protocol_device.smp_device.current_activity_phy_index,
            NULL,
            NULL
         );

         break;

      default:
         break;
   }
}

/**
 * @brief This method finishes the initial smp DISCOVER process. There
 *        may be a spinup_hold release phase following of initial discover,
 *        depending on whether there are SATA device in the domain
 *        in SATA_SPINUP_HOLD condition.
 *
 * @param[in] fw_device The framework smp device that finishes all the
 *       DISCOVER requests.
 *
 * @return none
 */
void scif_sas_smp_remote_device_finish_initial_discover(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SCIF_SAS_REMOTE_DEVICE_T * device_in_sata_spinup_hold =
      scif_sas_domain_find_device_in_spinup_hold(fw_device->domain);

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_finish_initial_discover(0x%x) enter\n",
      fw_device
   ));

   if ( device_in_sata_spinup_hold != NULL )
   {
     //call the common private routine to reset all fields of this smp device.
     scif_sas_smp_remote_device_clear(fw_device);

     //Move on to next activity SPINUP_HOLD_RELEASE
     fw_device->protocol_device.smp_device.current_activity =
        SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_SATA_SPINUP_HOLD_RELEASE;

      //create the timer to delay a little bit before going to
      //sata spinup hold release activity.
      if (fw_device->protocol_device.smp_device.smp_activity_timer == NULL)
      {
      fw_device->protocol_device.smp_device.smp_activity_timer =
         scif_cb_timer_create(
            (SCI_CONTROLLER_HANDLE_T *)fw_device->domain->controller,
            (SCI_TIMER_CALLBACK_T)scif_sas_smp_remote_device_sata_spinup_hold_release,
            (void*)fw_device
         );
      }
      else
      {
         ASSERT (0);
      }

      scif_cb_timer_start(
         (SCI_CONTROLLER_HANDLE_T)fw_device->domain->controller,
         fw_device->protocol_device.smp_device.smp_activity_timer,
         SMP_SPINUP_HOLD_RELEASE_WAIT_DURATION
      );
   }
   else
      scif_sas_smp_remote_device_finish_discover(fw_device);
}


/**
 * @brief This method finishes the smp DISCOVER process.
 *
 * @param[in] fw_device The framework smp device that finishes all the
 *       DISCOVER requests.
 *
 * @return none
 */
void scif_sas_smp_remote_device_finish_discover(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = fw_device->domain;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_finish_discover(0x%x) enter\n",
      fw_device
   ));

   if ( fw_domain->is_config_route_table_needed
       && fw_device->protocol_device.smp_device.smp_phy_list.list_head != NULL)
      scif_sas_smp_remote_device_configure_upstream_expander_route_info(fw_device);

   //call the common private routine to reset all fields of this smp device.
   scif_sas_smp_remote_device_clear(fw_device);

#ifdef SCI_SMP_PHY_LIST_DEBUG_PRINT
   scif_sas_smp_remote_device_print_smp_phy_list(fw_device);
#endif

   //notify domain this smp device's discover finishes, it's up to domain
   //to continue the discover process in a bigger scope.
   scif_sas_domain_continue_discover(fw_domain);
}


/**
 * @brief This method continues the smp Target Reset (Phy Control) process.
 *
 * @param[in] fw_device The framework smp device that a smp reset targets to.
 *
 * @return none
 */
void scif_sas_smp_remote_device_continue_target_reset(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCIF_SAS_REQUEST_T       * fw_request
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = fw_device->domain->controller;
   SCIF_SAS_REMOTE_DEVICE_T * target_device =
      scif_sas_domain_get_device_by_containing_device(
         fw_device->domain,
         fw_device,
         fw_device->protocol_device.smp_device.current_activity_phy_index
      );

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_continue_target_reset(0x%x, 0x%x) enter\n",
      fw_device, fw_request
   ));

   if (fw_device->protocol_device.smp_device.current_smp_request ==
          SMP_FUNCTION_PHY_CONTROL)
   {
      //query the core remote device to get suggested reset timeout value
      //then scale down by factor of 8 to get the duration of the pause
      //before sending out Discover command to poll.
      U32 delay =
         (scic_remote_device_get_suggested_reset_timeout(target_device->core_object)/8);

      //create the timer to send Discover command polling target device's
      //coming back.
      if (fw_device->protocol_device.smp_device.smp_activity_timer == NULL)
      {
         fw_device->protocol_device.smp_device.smp_activity_timer =
            scif_cb_timer_create(
               (SCI_CONTROLLER_HANDLE_T *)fw_controller,
               (SCI_TIMER_CALLBACK_T)scif_sas_smp_remote_device_target_reset_poll,
               (void*)fw_request
            );
      }
      else
      {
         ASSERT(0);
      }

      //start the timer
      scif_cb_timer_start(
         (SCI_CONTROLLER_HANDLE_T)fw_controller,
         fw_device->protocol_device.smp_device.smp_activity_timer,
         delay
      );
   }
   else if (fw_device->protocol_device.smp_device.current_smp_request ==
          SMP_FUNCTION_DISCOVER)
   {
      //tell target reset successful
      scif_sas_remote_device_target_reset_complete(
         target_device, fw_request, SCI_SUCCESS);
   }
}

/**
 * @brief This routine is invoked by timer or when 2 BCN are received
 *        after Phy Control command. This routine will construct a
 *        Discover command to the same expander phy to poll the target
 *        device's coming back. This new request is then put into
 *        high priority queue and will be started by a DPC soon.
 *
 * @param[in] fw_request The scif request for smp activities.
 */
void scif_sas_smp_remote_device_target_reset_poll(
   SCIF_SAS_REQUEST_T       * fw_request
)
{
   SCIF_SAS_REMOTE_DEVICE_T * fw_device = fw_request->device;
   SCIF_SAS_CONTROLLER_T * fw_controller = fw_device->domain->controller;
   void * new_command_handle;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_target_reset_poll(0x%x) enter\n",
      fw_request
   ));

   // Before we construct new io using the same memory, we need to
   // remove the IO from the list of outstanding requests on the domain
   // so that we don't damage the domain's fast list of request.
   sci_fast_list_remove_element(&fw_request->list_element);

   fw_device->protocol_device.smp_device.current_smp_request =
      SMP_FUNCTION_DISCOVER;

   //sent smp discover request to poll on remote device's coming back.
   //construct Discover command using the same memory as fw_request.
   new_command_handle = scif_sas_smp_request_construct_discover(
      fw_device->domain->controller,
      fw_device,
      fw_device->protocol_device.smp_device.current_activity_phy_index,
      (void *)sci_object_get_association(fw_request),
      (void *)fw_request
   );

   //put into the high priority queue.
   sci_pool_put(fw_controller->hprq.pool, (POINTER_UINT) new_command_handle);

   //schedule the DPC to start new Discover command.
   scif_cb_start_internal_io_task_schedule(
      fw_controller, scif_sas_controller_start_high_priority_io, fw_controller
   );
}


/**
 * @brief This method fails discover process.
 *
 * @param[in] fw_device The framework smp device that failed at current
 *       activity.
 *
 * @return none
 */
void scif_sas_smp_remote_device_fail_discover(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_fail_discover(0x%x) enter\n",
      fw_device
   ));

   switch (fw_device->protocol_device.smp_device.current_smp_request)
   {
      case SMP_FUNCTION_REPORT_GENERAL:
      case SMP_FUNCTION_REPORT_MANUFACTURER_INFORMATION:
         scif_sas_smp_remote_device_finish_discover(fw_device);
         break;

      case SMP_FUNCTION_DISCOVER:
      case SMP_FUNCTION_REPORT_PHY_SATA:
         //Retry limit reached, we will continue to send DISCOVER to next phy.
         fw_device->protocol_device.smp_device.current_smp_request =
            SMP_FUNCTION_DISCOVER;

         scif_sas_smp_remote_device_continue_discover(fw_device);
         break;

      default:
         break;
   }
}


/**
 * @brief This method fails Target Reset.
 *
 * @param[in] fw_device The framework smp device that failed at current
 *       activity.
 * @param[in] fw_request The smp request created for target reset
 *       using external resource.
 *
 * @return none
 */
void scif_sas_smp_remote_device_fail_target_reset(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCIF_SAS_REQUEST_T       * fw_request
)
{
   SCIF_SAS_REMOTE_DEVICE_T * target_device =
      scif_sas_domain_get_device_by_containing_device(
         fw_device->domain,
         fw_device,
         fw_device->protocol_device.smp_device.current_activity_phy_index
      );

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_fail_target_reset(0x%x, 0x%x, 0x%x) enter\n",
      fw_device, target_device, fw_request
   ));

   //tell target reset failed
   scif_sas_remote_device_target_reset_complete(
      target_device, fw_request, SCI_FAILURE);
}

/**
 * @brief This method init or continue the SATA SPINUP_HOLD RELEASE activity.
 * This function searches domain's device list, find a device in STOPPED STATE
 * and its connection_rate is SPINIP, then send DISCOVER command to its expander
 * phy id to poll. But if searching the domain's device list for SATA devices on
 * SPINUP_HOLD finds no device, the activity SPINUP_HOLD_RELEASE is finished.
 * We then call fw_domain->device_start_complete_handler() for this smp-device.
 *
 * @param[in] fw_device The framework smp device that is on SATA SPINUP_HOLD_RELEASE
 *       activity.
 *
 * @return none
 */
void scif_sas_smp_remote_device_sata_spinup_hold_release(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SCIF_SAS_DOMAIN_T        * fw_domain = fw_device->domain;
   SCIF_SAS_CONTROLLER_T    * fw_controller = fw_domain->controller;
   SCIF_SAS_REMOTE_DEVICE_T * device_to_poll = NULL;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_sata_spinup_hold_release(0x%x) enter\n",
      fw_device
   ));

   //search throught domain's device list to find a sata device on spinup_hold
   //state to poll.
   device_to_poll = scif_sas_domain_find_device_in_spinup_hold(fw_domain);

   if (device_to_poll != NULL)
   {
      //send DISCOVER command to this device's expaner phy.
      fw_device->protocol_device.smp_device.current_smp_request =
         SMP_FUNCTION_DISCOVER;

      fw_device->protocol_device.smp_device.current_activity_phy_index =
        device_to_poll->expander_phy_identifier;

      scif_sas_smp_request_construct_discover(
         fw_domain->controller,
         fw_device,
         fw_device->protocol_device.smp_device.current_activity_phy_index,
         NULL, NULL
      );

      //schedule the DPC to start new Discover command.
      scif_cb_start_internal_io_task_schedule(
         fw_controller, scif_sas_controller_start_high_priority_io, fw_controller
      );
   }
   else //SATA SPINUP HOLD RELEASE activity is done.
      scif_sas_smp_remote_device_finish_discover (fw_device);
}


/**
 * @brief This method fail an action of SATA SPINUP_HOLD RELEASE on a single EA
 *        SATA device. It will remove a remote_device object for a sata device
 *        that fails to come out of spinup_hold.
 *
 * @param[in] fw_device The framework smp device that is on SATA SPINUP_HOLD_RELEASE
 *       activity.
 * @param[in] target_device The expander attached device failed being brought out
 *       of SPINUP_HOLD state.
 *
 * @return none
 */
void scif_sas_smp_remote_device_fail_target_spinup_hold_release(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCIF_SAS_REMOTE_DEVICE_T * target_device
)
{
   SCIF_SAS_DOMAIN_T * fw_domain = fw_device->domain;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_fail_target_spinup_hold_release(0x%x, 0x%x) enter\n",
      fw_device, target_device
   ));

   //need to remove the device, since we have to give up on spinup_hold_release
   //activity on this device.
   scif_cb_domain_device_removed(
      fw_domain->controller, fw_domain, target_device
   );

   //move on to next round of SPINUP_HOLD_REALSE activity.
   scif_sas_smp_remote_device_sata_spinup_hold_release(fw_device);
}


/**
 * @brief This method retry only internal IO for the smp device.
 *
 * @param[in] fw_device The framework smp device that has an smp request to retry.
 * @param[in] io_retry_count current count for times the IO being retried.
 * @param[in] delay The time delay before the io gets retried.
 *
 * @return none
 */
void scif_sas_smp_remote_device_retry_internal_io(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   U8                         io_retry_count,
   U32                        delay
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_retry_internal_io(0x%x, 0x%x, 0x%x) enter\n",
      fw_device, io_retry_count, delay
   ));

   fw_device->protocol_device.smp_device.io_retry_count =
      io_retry_count;

   //create the timer for poll target device's coming back.
   if (fw_device->protocol_device.smp_device.smp_activity_timer == NULL)
   {
      fw_device->protocol_device.smp_device.smp_activity_timer =
         scif_cb_timer_create(
            (SCI_CONTROLLER_HANDLE_T *)fw_device->domain->controller,
            (SCI_TIMER_CALLBACK_T)scif_sas_smp_internal_request_retry,
            (void*)fw_device
         );
   }
   else
   {
      ASSERT(0);
   }
   //start the timer for a purpose of waiting.
   scif_cb_timer_start(
      (SCI_CONTROLLER_HANDLE_T)fw_device->domain->controller,
      fw_device->protocol_device.smp_device.smp_activity_timer,
      delay
   );
}


/**
 * @brief This method indicates whether an expander device is in Discover
 *        process.
 *
 * @param[in] fw_device The framework smp device.
 *
 * @return Whether an expander device is in the middle of discovery process.
 */
BOOL scif_sas_smp_remote_device_is_in_activity(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   return(fw_device->protocol_device.smp_device.current_activity
          != SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_NONE);
}

/**
 * @brief This method search through the smp phy list of an expander to
 *        find a smp phy by its phy id of the expander.
 *
 * @param[in] phy_identifier The search criteria.
 * @param[in] smp_remote_device The expander that owns the smp phy list.
 *
 * @return The found smp phy or a NULL pointer to indicate no smp phy is found.
 */
SCIF_SAS_SMP_PHY_T * scif_sas_smp_remote_device_find_smp_phy_by_id(
   U8                             phy_identifier,
   SCIF_SAS_SMP_REMOTE_DEVICE_T * smp_remote_device
)
{
   SCI_FAST_LIST_ELEMENT_T  * element = smp_remote_device->smp_phy_list.list_head;
   SCIF_SAS_SMP_PHY_T * curr_smp_phy = NULL;

   ASSERT(phy_identifier < smp_remote_device->smp_phy_list.number_of_phys);

   while (element != NULL)
   {
      curr_smp_phy = (SCIF_SAS_SMP_PHY_T*) sci_fast_list_get_object(element);
      element = sci_fast_list_get_next(element);

      if (curr_smp_phy->phy_identifier == phy_identifier)
         return curr_smp_phy;
   }

   return NULL;
}

/**
 * @brief This method takes care of removing smp phy list of a smp devcie, which is
 *           about to be removed.
 *
 * @param[in] fw_device The expander device that is about to be removed.
 *
 * @return none.
 */
void scif_sas_smp_remote_device_removed(
   SCIF_SAS_REMOTE_DEVICE_T * this_device
)
{
   SCIF_SAS_SMP_REMOTE_DEVICE_T * smp_remote_device =
      &this_device->protocol_device.smp_device;

   SCI_FAST_LIST_ELEMENT_T  * element = smp_remote_device->smp_phy_list.list_head;
   SCIF_SAS_SMP_PHY_T * curr_smp_phy = NULL;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_removed(0x%x) enter\n",
      this_device
   ));

   //remove all the smp phys in this device's smp_phy_list, and the conterpart smp phys
   //in phy connections.
   while (element != NULL)
   {
      curr_smp_phy = (SCIF_SAS_SMP_PHY_T*) sci_fast_list_get_object(element);
      element = sci_fast_list_get_next(element);

      scif_sas_smp_phy_destruct(curr_smp_phy);
   }

   this_device->protocol_device.smp_device.number_of_phys = 0;
   this_device->protocol_device.smp_device.expander_route_indexes = 0;
   this_device->protocol_device.smp_device.is_table_to_table_supported = FALSE;
   this_device->protocol_device.smp_device.is_externally_configurable  = FALSE;
   this_device->protocol_device.smp_device.is_able_to_config_others    = FALSE;

   scif_sas_smp_remote_device_clear(this_device);
}


/**
 * @brief This method takes care of terminated smp request to a smp device. The
 *        terminated smp request is most likely timeout and being aborted. A timeout
 *        maybe due to OPEN REJECT (NO DESTINATION).
 *
 * @param[in] fw_device The expander device that a timed out smp request towards to.
 * @param[in] fw_request A failed smp request that is terminated by scic.
 *
 * @return none.
 */
void scif_sas_smp_remote_device_terminated_request_handler(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCIF_SAS_REQUEST_T       * fw_request
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_terminated_request_handler(0x%x, 0x%x) enter\n",
      fw_device, fw_request
   ));

   scif_sas_smp_remote_device_decode_smp_response(
      fw_device, fw_request, NULL, SCI_IO_FAILURE_RETRY_REQUIRED
   );
}


/**
 * @brief This method allocates and populates the smp phy list of a expander device.
 *
 * @param[in] fw_device The expander device, whose smp phy list is to be populated after
 *                      getting REPORT GENERAL response.
 *
 * @return none.
 */
void scif_sas_smp_remote_device_populate_smp_phy_list(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SCIF_SAS_SMP_PHY_T * this_smp_phy = NULL;
   U8                   expander_phy_id = 0;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_populate_smp_phy_list(0x%x) enter\n",
      fw_device
   ));

   for ( expander_phy_id = 0;
         expander_phy_id < fw_device->protocol_device.smp_device.number_of_phys;
         expander_phy_id++ )
   {
      this_smp_phy =
         scif_sas_controller_allocate_smp_phy(fw_device->domain->controller);

      ASSERT( this_smp_phy != NULL );

      if ( this_smp_phy != NULL )
         scif_sas_smp_phy_construct(this_smp_phy, fw_device, expander_phy_id);
   }
}


/**
 * @brief This method updates a smp phy of a expander device based on DISCOVER response.
 *
 * @param[in] fw_device The expander device, one of whose smp phys is to be updated.
 * @param[in] discover_response The smp DISCOVER response.
 *
 * @return SCI_STATUS If a smp phy pair between expanders has invalid routing attribute,
 *                    return SCI_FAILURE_ILLEGAL_ROUTING_ATTRIBUTE_CONFIGURATION, otherwise,
 *                    return SCI_SUCCESS
 */
SCI_STATUS scif_sas_smp_remote_device_save_smp_phy_info(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SMP_RESPONSE_DISCOVER_T  * discover_response
)
{
   SCI_STATUS status = SCI_SUCCESS;
   SCIF_SAS_SMP_PHY_T * smp_phy = NULL;
   SCIF_SAS_REMOTE_DEVICE_T * attached_device = NULL;

    SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_save_smp_phy_info(0x%x, 0x%x) enter\n",
      fw_device, discover_response
   ));

   smp_phy = scif_sas_smp_remote_device_find_smp_phy_by_id(
                discover_response->phy_identifier,
                &fw_device->protocol_device.smp_device
             );

   ASSERT( smp_phy != NULL );

   //Note, attached_device could be NULL, not all the smp phy have to connected to a device.
   attached_device = (SCIF_SAS_REMOTE_DEVICE_T *)
      scif_domain_get_device_by_sas_address(
         fw_device->domain, &discover_response->attached_sas_address);

   scif_sas_smp_phy_save_information(
      smp_phy, attached_device, discover_response);

   //handle the special case of smp phys between expanders.
   if ( discover_response->protocols.u.bits.attached_smp_target )
   {
       //this fw_device is a child expander, just found its parent expander.
       //And there is no smp_phy constructed yet, record this phy connection.
       if ( attached_device != NULL
           && attached_device == fw_device->containing_device )
       {
          //record the smp phy info, for this phy connects to a upstream smp device.
          //the connection of a pair of smp phys are completed.
          status = scif_sas_smp_phy_set_attached_phy(
                      smp_phy,
                      discover_response->attached_phy_identifier,
                      attached_device
                   );

          if (status == SCI_SUCCESS)
          {
             //check the routing attribute for this phy and its containing device's
             //expander_phy_routing_attribute.
             if ( scif_sas_smp_phy_verify_routing_attribute(
                     smp_phy, smp_phy->u.attached_phy) != SCI_SUCCESS )
                return SCI_FAILURE_ILLEGAL_ROUTING_ATTRIBUTE_CONFIGURATION;
          }
       }
    }

    return status;
}

#ifdef SCI_SMP_PHY_LIST_DEBUG_PRINT
void scif_sas_smp_remote_device_print_smp_phy_list(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SCIF_SAS_SMP_REMOTE_DEVICE_T * smp_remote_device = &fw_device->protocol_device.smp_device;
   SCI_FAST_LIST_ELEMENT_T  * element = smp_remote_device->smp_phy_list.list_head;
   SCIF_SAS_SMP_PHY_T * curr_smp_phy = NULL;

   SCIF_LOG_ERROR((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE,
      "==========EXPANDER DEVICE (0x%x) smp phy list========== \n",
      fw_device
   ));

   while (element != NULL)
   {
      curr_smp_phy = (SCIF_SAS_SMP_PHY_T*) sci_fast_list_get_object(element);
      element = sci_fast_list_get_next(element);

      //print every thing about a smp phy
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_device),
         SCIF_LOG_OBJECT_REMOTE_DEVICE,
         "SMP_PHY_%d (0x%x), attached device(0x%x), attached_sas_address(%x%x) attached_device_type(%d), routing_attribute(%d)\n",
         curr_smp_phy->phy_identifier, curr_smp_phy,
         curr_smp_phy->u.end_device,
         curr_smp_phy->attached_sas_address.high, curr_smp_phy->attached_sas_address.low,
         curr_smp_phy->attached_device_type,
         curr_smp_phy->routing_attribute
      ));
   }
}
#endif


/**
 * @brief This method configure upstream expander(s)' (if there is any) route info.
 *
 * @param[in] this_device The expander device that is currently in discover process.
 *
 * @return none.
 */
void scif_sas_smp_remote_device_configure_upstream_expander_route_info(
   SCIF_SAS_REMOTE_DEVICE_T * this_device
)
{
   SCIF_SAS_REMOTE_DEVICE_T * curr_child_expander = this_device;
   SCIF_SAS_REMOTE_DEVICE_T * curr_parent_expander =
      scif_sas_remote_device_find_upstream_expander(this_device);

   SCIF_SAS_REMOTE_DEVICE_T * curr_config_route_info_expander = NULL;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_configure_upstream_expander_route_info(0x%x) enter\n",
      this_device
   ));

   //traverse back to find root device.
   while(curr_parent_expander != NULL )
   {
      //must set destination_smp_phy outside of find_upstream_expander() using the device
      //that is just about to finish the discovery.
      curr_parent_expander->protocol_device.smp_device.curr_config_route_destination_smp_phy =
         (SCIF_SAS_SMP_PHY_T*)sci_fast_list_get_object(
             this_device->protocol_device.smp_device.smp_phy_list.list_head);

      curr_child_expander = curr_parent_expander;
      curr_parent_expander = scif_sas_remote_device_find_upstream_expander(curr_child_expander);
   }

   //found the root device: curr_child_expander. configure it and its downstream expander(s) till
   //this_device or a self-configuring expander that configures others;
   curr_config_route_info_expander = curr_child_expander;

   while ( curr_config_route_info_expander != NULL
          && curr_config_route_info_expander != this_device
          && curr_config_route_info_expander->protocol_device.smp_device.scheduled_activity
                == SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_NONE
         )
   {
      if (curr_config_route_info_expander->protocol_device.smp_device.is_externally_configurable)
      {
         SCIF_SAS_SMP_PHY_T * phy_being_config =
            curr_config_route_info_expander->protocol_device.smp_device.config_route_smp_phy_anchor;

         curr_config_route_info_expander->protocol_device.smp_device.curr_config_route_index =
            phy_being_config->config_route_table_index_anchor;

         if (curr_config_route_info_expander->protocol_device.smp_device.curr_config_route_index != 0)
            curr_config_route_info_expander->protocol_device.smp_device.curr_config_route_index++;

         curr_config_route_info_expander->protocol_device.smp_device.scheduled_activity =
            SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_CONFIG_ROUTE_TABLE;

         //Find a downstream expander that has curr_config_route_destination_smp_phy.owning device
         //same as curr_config_route_info_expander.
         curr_config_route_info_expander = scif_sas_remote_device_find_downstream_expander(
            curr_config_route_info_expander);
      }
      else if (curr_config_route_info_expander->protocol_device.smp_device.is_able_to_config_others)
      {
         //no need to config route table to this expander and its children.
         //find its downstream expander and clear the planned config route table activity.
         SCIF_SAS_REMOTE_DEVICE_T * curr_downstream_expander =
            scif_sas_remote_device_find_downstream_expander(
               curr_config_route_info_expander);

         scif_sas_smp_remote_device_clear(curr_config_route_info_expander);

         while ( curr_downstream_expander != NULL
                && curr_downstream_expander != this_device )
         {
            scif_sas_smp_remote_device_clear(curr_downstream_expander);
            curr_downstream_expander =
               scif_sas_remote_device_find_downstream_expander(
                  curr_config_route_info_expander);
         }

         break;
      }
      else
      {
         // current expander is a self-configuring expander, which is not externally
         // configurable, and doesn't config others. we need to simply skip this expander.
         curr_config_route_info_expander = scif_sas_remote_device_find_downstream_expander(
            curr_config_route_info_expander);
      }
   }
}

/**
 * @brief This method finds the immediate upstream expander of a given expander device.
 *
 * @param[in] this_device The given expander device, whose upstream expander is to be found.
 *
 * @return The immediate upstream expander. Or a NULL pointer if this_device is root already.
 */
SCIF_SAS_REMOTE_DEVICE_T * scif_sas_remote_device_find_upstream_expander(
   SCIF_SAS_REMOTE_DEVICE_T * this_device
)
{
   SCIF_SAS_SMP_REMOTE_DEVICE_T * smp_remote_device =
      &this_device->protocol_device.smp_device;

   SCIF_SAS_REMOTE_DEVICE_T    * upstream_expander = NULL;

   SCI_FAST_LIST_ELEMENT_T     * element = smp_remote_device->smp_phy_list.list_head;
   SCIF_SAS_SMP_PHY_T          * curr_smp_phy = NULL;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_configure_upstream_expander_route_info(0x%x) enter\n",
      this_device
   ));

   while (element != NULL)
   {
      curr_smp_phy = (SCIF_SAS_SMP_PHY_T*) sci_fast_list_get_object(element);
      element = sci_fast_list_get_next(element);

      if ( curr_smp_phy->routing_attribute == SUBTRACTIVE_ROUTING_ATTRIBUTE
          && ( curr_smp_phy->attached_device_type == SMP_EDGE_EXPANDER_DEVICE
              || curr_smp_phy->attached_device_type == SMP_FANOUT_EXPANDER_DEVICE)
          && curr_smp_phy->u.attached_phy != NULL
          && curr_smp_phy->u.attached_phy->routing_attribute == TABLE_ROUTING_ATTRIBUTE )
      {
         //set the current_activity and current_config_route_index for that
         //upstream expander.
         upstream_expander = curr_smp_phy->u.attached_phy->owning_device;

         upstream_expander->protocol_device.smp_device.current_smp_request =
            SMP_FUNCTION_CONFIGURE_ROUTE_INFORMATION;

         //if the upstream_expander's config route table method is config phy0 only or
         //config all phys, the current activity phy is found.
         upstream_expander->protocol_device.smp_device.config_route_smp_phy_anchor =
            scif_sas_smp_remote_device_find_smp_phy_by_id(
               curr_smp_phy->u.attached_phy->phy_identifier,
               &(curr_smp_phy->u.attached_phy->owning_device->protocol_device.smp_device)
            );

         //if the upstream_expander's config route table method is config middle phy only
         //config highest phy only, the current activity phy needs a update.
         if ( scif_sas_smp_remote_device_get_config_route_table_method(upstream_expander)
                 == SCIF_SAS_CONFIG_ROUTE_TABLE_MIDDLE_PHY_ONLY )
         {
            upstream_expander->protocol_device.smp_device.config_route_smp_phy_anchor =
               scif_sas_smp_phy_find_middle_phy_in_wide_port (
                  upstream_expander->protocol_device.smp_device.config_route_smp_phy_anchor
               );
         }
         else if ( scif_sas_smp_remote_device_get_config_route_table_method(upstream_expander)
                      == SCIF_SAS_CONFIG_ROUTE_TABLE_HIGHEST_PHY_ONLY )
         {
            upstream_expander->protocol_device.smp_device.config_route_smp_phy_anchor =
               scif_sas_smp_phy_find_highest_phy_in_wide_port (
                  upstream_expander->protocol_device.smp_device.config_route_smp_phy_anchor
               );
         }

         upstream_expander->protocol_device.smp_device.current_activity_phy_index =
            upstream_expander->protocol_device.smp_device.config_route_smp_phy_anchor->phy_identifier;

         return upstream_expander;
      }
   }

   return NULL;
}


/**
 * @brief This method finds the immediate downstream expander of a given expander device.
 *
 * @param[in] this_device The given expander device, whose downstream expander is to be found.
 *
 * @return The immediate downstream expander. Or a NULL pointer if there is none.
 */
SCIF_SAS_REMOTE_DEVICE_T * scif_sas_remote_device_find_downstream_expander(
   SCIF_SAS_REMOTE_DEVICE_T * this_device
)
{
   SCIF_SAS_SMP_REMOTE_DEVICE_T * this_smp_remote_device =
      &this_device->protocol_device.smp_device;

   SCIF_SAS_REMOTE_DEVICE_T    * downstream_expander = NULL;

   SCI_FAST_LIST_ELEMENT_T     * element = this_smp_remote_device->smp_phy_list.list_head;
   SCIF_SAS_SMP_PHY_T          * curr_smp_phy = NULL;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(this_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_remote_device_find_downstream_expander(0x%x) enter\n",
      this_device
   ));

   while (element != NULL)
   {
      curr_smp_phy = (SCIF_SAS_SMP_PHY_T*) sci_fast_list_get_object(element);
      element = sci_fast_list_get_next(element);

      if ( curr_smp_phy->routing_attribute == TABLE_ROUTING_ATTRIBUTE
          && curr_smp_phy->attached_device_type == SMP_EDGE_EXPANDER_DEVICE
          && curr_smp_phy->u.attached_phy != NULL)
      {
         //set the current_activity and current_config_route_index for that
         //upstream expander.
         downstream_expander = curr_smp_phy->u.attached_phy->owning_device;

         if ( downstream_expander->protocol_device.smp_device.curr_config_route_destination_smp_phy != NULL
             && downstream_expander->protocol_device.smp_device.curr_config_route_destination_smp_phy->owning_device ==
                this_smp_remote_device->curr_config_route_destination_smp_phy->owning_device )
            return downstream_expander;
      }
   }

   return NULL;
}


/**
 * @brief This method follows route table optimization rule to check if a destination_device
 *        should be recorded in the device_being_config's route table
 *
 * @param[in] device_being_config The upstream expander device, whose route table is being configured.
 * @param[in] destination_smp_phy A smp phy whose attached device is potentially to be
 *               recorded in route table.
 *
 * @return BOOL This method returns TRUE if a destination_device should be recorded in route table.
 *              This method returns FALSE if a destination_device need not to be recorded
 *              in route table.
 */
BOOL scif_sas_smp_remote_device_do_config_route_info(
   SCIF_SAS_REMOTE_DEVICE_T * device_being_config,
   SCIF_SAS_SMP_PHY_T       * destination_smp_phy
)
{
   SCI_SAS_ADDRESS_T device_being_config_sas_address;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(device_being_config),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_do_config_route_info(0x%x, 0x%x) enter\n",
      device_being_config, destination_smp_phy
   ));

   scic_remote_device_get_sas_address(
      device_being_config->core_object, &device_being_config_sas_address
   );

   //refer to SAS-2 spec 4.8.3, rule (b)
   if ((destination_smp_phy->attached_sas_address.low == 0
        && destination_smp_phy->attached_sas_address.high == 0)
       && (destination_smp_phy->attached_device_type == SMP_NO_DEVICE_ATTACHED))
   {
      return FALSE;
   }

   //refer to SAS-2 spec 4.8.3, rule (c), self-referencing.
   if (destination_smp_phy->attached_sas_address.high ==
          device_being_config_sas_address.high
       && destination_smp_phy->attached_sas_address.low ==
             device_being_config_sas_address.low)
   {
      return FALSE;
   }

   //There will be no cases that falling into rule (a), (d), (e) to be excluded,
   //based on our current mechanism of cofig route table.

   return TRUE;
}


/**
 * @brief This method configures device_being_config's route table for all the enclosed devices in
 *           a downstream smp device, destination_device.
 *
 * @param[in] device_being_config The upstream expander device, whose route table is being configured.
 *
 * @return None
 */
void scif_sas_smp_remote_device_configure_route_table(
   SCIF_SAS_REMOTE_DEVICE_T * device_being_config
)
{
   //go through the smp phy list of this_device.
   SCI_FAST_LIST_ELEMENT_T     * element =
      &(device_being_config->protocol_device.smp_device.curr_config_route_destination_smp_phy->list_element);
   SCIF_SAS_SMP_PHY_T          * curr_smp_phy = NULL;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(device_being_config),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_configure_route_table(0x%x) enter\n",
      device_being_config
   ));

   device_being_config->protocol_device.smp_device.current_activity =
      SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_CONFIG_ROUTE_TABLE;

   while (element != NULL)
   {
      curr_smp_phy = (SCIF_SAS_SMP_PHY_T*) sci_fast_list_get_object(element);
      element = sci_fast_list_get_next(element);

      //check if this phy needs to be added to the expander's route table.
      if (scif_sas_smp_remote_device_do_config_route_info(
             device_being_config, curr_smp_phy) == TRUE )
      {
         SCIF_SAS_SMP_REMOTE_DEVICE_T * smp_remote_device =
            &device_being_config->protocol_device.smp_device;

         smp_remote_device->curr_config_route_destination_smp_phy =
            curr_smp_phy;

         //Then config this_device's route table entry at the phy and next route_index.
         //send config_route_info using curr_smp_phy.phy_identifier and sas_address.
         scif_sas_smp_request_construct_config_route_info(
            device_being_config->domain->controller,
            device_being_config,
            smp_remote_device->current_activity_phy_index,
            smp_remote_device->curr_config_route_index,
            curr_smp_phy->attached_sas_address,
            FALSE
         );

         //schedule the DPC.
         scif_cb_start_internal_io_task_schedule(
            device_being_config->domain->controller,
            scif_sas_controller_start_high_priority_io,
            device_being_config->domain->controller
         );

         //stop here, we need to wait for config route info's response then send
         //the next one.
         break;
      }
   }
}


/**
 * @brief This method walks through an expander's route table to clean table
 *           attribute phys' route entries. This routine finds one table entry
 *           to clean and will be called repeatly till it finishes cleanning the
 *           whole table.
 *
 * @param[in] fw_device The expander device, whose route table entry is to be cleaned.
 *
 * @return None.
 */
void scif_sas_smp_remote_device_clean_route_table(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SCIF_SAS_SMP_PHY_T * smp_phy_being_config;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_clean_route_table(0x%x) enter\n",
      fw_device
   ));

   //from anchors, start to clean all the other route table entries.
   fw_device->protocol_device.smp_device.curr_config_route_index++;

   if ( fw_device->protocol_device.smp_device.curr_config_route_index >=
           fw_device->protocol_device.smp_device.expander_route_indexes )
   {
      fw_device->protocol_device.smp_device.curr_config_route_index = 0;

      do //find next table attribute PHY.
      {
         fw_device->protocol_device.smp_device.current_activity_phy_index++;
         if (fw_device->protocol_device.smp_device.current_activity_phy_index ==
                fw_device->protocol_device.smp_device.number_of_phys)
            fw_device->protocol_device.smp_device.current_activity_phy_index=0;

         //phy_index changed, so update the smp_phy_being_config.
         smp_phy_being_config =
            scif_sas_smp_remote_device_find_smp_phy_by_id(
               fw_device->protocol_device.smp_device.current_activity_phy_index,
               &(fw_device->protocol_device.smp_device)
            );
      } while( smp_phy_being_config->routing_attribute != TABLE_ROUTING_ATTRIBUTE );

      if ( smp_phy_being_config->phy_identifier !=
              fw_device->protocol_device.smp_device.config_route_smp_phy_anchor->phy_identifier)
      {
         if (smp_phy_being_config->config_route_table_index_anchor != 0)
            fw_device->protocol_device.smp_device.curr_config_route_index =
               smp_phy_being_config->config_route_table_index_anchor + 1;
         else
            fw_device->protocol_device.smp_device.curr_config_route_index = 0;
      }
   }

   if ( !(fw_device->protocol_device.smp_device.current_activity_phy_index ==
             fw_device->protocol_device.smp_device.config_route_smp_phy_anchor->phy_identifier
          && fw_device->protocol_device.smp_device.curr_config_route_index == 0)
      )
   {
      //clean this route entry.
      scif_sas_smp_remote_device_clean_route_table_entry(fw_device);
   }
   else
   {
      fw_device->protocol_device.smp_device.is_route_table_cleaned = TRUE;

      //set this device's activity to NON.
      fw_device->protocol_device.smp_device.current_activity =
         SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_NONE;

      //we need to notify domain that this device finished config route table, domain
      //may pick up other activities (i.e. Discover) for other expanders.
      scif_sas_domain_continue_discover(fw_device->domain);
   }
}

/**
 * @brief This method cleans a device's route table antry.
 *
 * @param[in] fw_device The expander device, whose route table entry is to be cleaned.
 *
 * @return None.
 */
void scif_sas_smp_remote_device_clean_route_table_entry(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SCI_SAS_ADDRESS_T empty_sas_address;
   SCIF_SAS_SMP_REMOTE_DEVICE_T * smp_remote_device =
      &(fw_device->protocol_device.smp_device);

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_clean_route_table(0x%x) enter\n",
      fw_device
   ));

   empty_sas_address.high = 0;
   empty_sas_address.low = 0;

   scif_sas_smp_request_construct_config_route_info(
      fw_device->domain->controller,
      fw_device,
      smp_remote_device->current_activity_phy_index,
      smp_remote_device->curr_config_route_index,
      empty_sas_address,
      TRUE
   );

   //schedule the DPC.
   scif_cb_start_internal_io_task_schedule(
      fw_device->domain->controller,
      scif_sas_controller_start_high_priority_io,
      fw_device->domain->controller
   );
}


/**
 * @brief This method handles the case of exceeding route index when config route table
 *           for a device, by removing the attached device of current config route
 *           destination smp phy and the rest of smp phys in the same smp phy list.
 *
 * @param[in] fw_device The expander device, whose route table to be edited but failed
 *               with a SMP function result of INDEX DOES NOT EXIST.
 *
 * @return None.
 */
void scif_sas_smp_remote_device_cancel_config_route_table_activity(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   //go through the rest of the smp phy list of destination device.
   SCI_FAST_LIST_ELEMENT_T     * element =
      &(fw_device->protocol_device.smp_device.curr_config_route_destination_smp_phy->list_element);
   SCIF_SAS_SMP_PHY_T          * curr_smp_phy = NULL;
   SCIF_SAS_REMOTE_DEVICE_T    * curr_attached_device = NULL;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_cancel_config_route_table_activity(0x%x) enter\n",
      fw_device
   ));

   while (element != NULL)
   {
      curr_smp_phy = (SCIF_SAS_SMP_PHY_T*) sci_fast_list_get_object(element);
      element = sci_fast_list_get_next(element);

      //check if this phy needs to be added to the expander's route table but can't due to
      //exceeding max route index.
      if (scif_sas_smp_remote_device_do_config_route_info(
             fw_device, curr_smp_phy) == TRUE )
      {
         //set the is_currently_discovered to FALSE for attached device. Then when
         //domain finish discover, domain will remove this device.
         curr_attached_device = (SCIF_SAS_REMOTE_DEVICE_T *)
            scif_domain_get_device_by_sas_address(
               fw_device->domain, &(curr_smp_phy->attached_sas_address));

         if (curr_attached_device != NULL)
            curr_attached_device->is_currently_discovered = FALSE;
      }
   }
}


/**
 * @brief This method cancel current activity and terminate the outstanding internal IO
 *           if there is one.
 *
 * @param[in] fw_device The expander device, whose smp activity is to be canceled.
 *
 * @return None.
 */
void scif_sas_smp_remote_device_cancel_smp_activity(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_device),
      SCIF_LOG_OBJECT_REMOTE_DEVICE | SCIF_LOG_OBJECT_DOMAIN_DISCOVERY,
      "scif_sas_smp_remote_device_cancel_smp_activity(0x%x) enter\n",
      fw_device
   ));

   //Terminate all of the requests in the silicon for this device.
   scif_sas_domain_terminate_requests(
      fw_device->domain, fw_device, NULL, NULL
   );

   if (fw_device->protocol_device.smp_device.current_activity ==
          SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_CONFIG_ROUTE_TABLE)
      scif_sas_smp_remote_device_cancel_config_route_table_activity(fw_device);

   //Clear the device to stop the smp sctivity.
   scif_sas_smp_remote_device_clear(fw_device);
}


/**
 * @brief This method tells the way to configure route table for a expander. The
 *          possible ways are: configure phy 0's route table, configure middle
 *          phy's route table, configure highest order phy's route table,
 *          configure all phys.
 *
 * @param[in] fw_device The expander device, whose config route table method is
 *               to be chosen.
 *
 * @return one in 4 possible options.
 */
U8 scif_sas_smp_remote_device_get_config_route_table_method(
   SCIF_SAS_REMOTE_DEVICE_T * fw_device
)
{
   U8 config_route_table_method;

   //config_route_table_method = SCIF_SAS_CONFIG_ROUTE_TABLE_MIDDLE_PHY_ONLY;
   config_route_table_method = SCIF_SAS_CONFIG_ROUTE_TABLE_ALL_PHYS;

   return config_route_table_method;
}


/**
 * @brief This method starts the EA target reset process by constructing
 *           and starting a PHY CONTROL (hard reset) smp request.
 *
 * @param[in] expander_device The expander device, to which a PHY Control smp command is
 *               sent.
 * @param[in] target_device The expander attahced target device, to which the target reset
 *               request is sent.
 * @param[in] fw_request The target reset task request.
 *
 * @return none
 */
void scif_sas_smp_remote_device_start_target_reset(
   SCIF_SAS_REMOTE_DEVICE_T * expander_device,
   SCIF_SAS_REMOTE_DEVICE_T * target_device,
   SCIF_SAS_REQUEST_T       * fw_request
)
{
   SCIF_SAS_CONTROLLER_T * fw_controller = expander_device->domain->controller;

   //set current_activity and current_smp_request to expander device.
   expander_device->protocol_device.smp_device.current_activity =
      SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_TARGET_RESET;
   expander_device->protocol_device.smp_device.current_smp_request =
      SMP_FUNCTION_PHY_CONTROL;
   expander_device->protocol_device.smp_device.current_activity_phy_index =
      target_device->expander_phy_identifier;

   //A Phy Control smp request has been constructed towards parent device.
   //Walk the high priority io path.
   fw_controller->state_handlers->start_high_priority_io_handler(
      (SCI_BASE_CONTROLLER_T*) fw_controller,
      (SCI_BASE_REMOTE_DEVICE_T*) expander_device,
      (SCI_BASE_REQUEST_T*) fw_request,
      SCI_CONTROLLER_INVALID_IO_TAG
   );
}


