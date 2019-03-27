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
#ifndef _SCIF_SAS_SMP_REMOTE_DEVICE_H_
#define _SCIF_SAS_SMP_REMOTE_DEVICE_H_

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


#include <dev/isci/scil/sci_fast_list.h>
#include <dev/isci/scil/scif_sas_smp_phy.h>

/**
 * @file
 *
 * @brief This file contains the protected interface structures, constants,
 *        and methods for the SCIF_SAS_SMP_REMOTE_DEVICE object.
 */

struct SCIF_SAS_CONTROLLER;
struct SCIF_SAS_REMOTE_DEVICE;
struct SCIF_SAS_INTERNAL_IO_REQUEST;
struct SCIF_SAS_REQUEST;
struct SCIF_SAS_SMP_PHY;

#define SMP_REQUEST_RETRY_WAIT_DURATION   20
#define SMP_SPINUP_HOLD_RELEASE_WAIT_DURATION 100

/**
 * @name SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_CODES
 *
 * These constants depict the various SMP remote device activities.
 */
/*@{*/
#define NOT_IN_SMP_ACTIVITY 0xff
#define SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_NONE         0x0
#define SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_DISCOVER     0x1
#define SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_TARGET_RESET 0x2
#define SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_SATA_SPINUP_HOLD_RELEASE 0x3
#define SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_CONFIG_ROUTE_TABLE 0x4
#define SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_CLEAN_ROUTE_TABLE 0x5
#define SCIF_SAS_SMP_REMOTE_DEVICE_ACTIVITY_CLEAR_AFFILIATION 0x6
/*@}*/



/**
 * @name SCIF_SAS_CONFIG_ROUTE_TABLE_OPTION_CODES
 *
 * These constants depict the various configure route table options.
 */
/*@{*/
#define SCIF_SAS_CONFIG_ROUTE_TABLE_LOWEST_PHY_ONLY   0
#define SCIF_SAS_CONFIG_ROUTE_TABLE_MIDDLE_PHY_ONLY   1
#define SCIF_SAS_CONFIG_ROUTE_TABLE_HIGHEST_PHY_ONLY  2
#define SCIF_SAS_CONFIG_ROUTE_TABLE_ALL_PHYS          3
/*@}*/

/**
 * @struct SCIF_SAS_SMP_REMOTE_DEVICE
 *
 * @brief The SCIF_SAS_SMP_REMOTE_DEVICE stores data for smp remote devices
 *        (expanders) discovering attached targets.
 *
 */
typedef struct SCIF_SAS_SMP_REMOTE_DEVICE
{
   /**
    * This field stores the current SMP request function in the discovering
    * sequence.
    */
   U32 current_smp_request;

   /**
    * This field indicates a smp device is either in the middle of normal discover
    * process or in the middle of resetting a expander attahced remote device.
    */
   U8 current_activity;

   /**
    * This field stores the current expander phy index for sending out SMP
    * DISCOVER request.
    */
   U8 current_activity_phy_index;

   /**
    * This field stores the current route index to config route table for
    * a phy.
    */
   U16 curr_config_route_index;

   /**
    * This field indicates whether a route table of an expander has been cleaned
    * since a DISCOVER process starts.
    */
   BOOL is_route_table_cleaned;

   /**
    * This field stores the smp phy whose route entries are edited by sending
    * CONFIG ROUTE INFO commands.
    */
   struct SCIF_SAS_SMP_PHY * config_route_smp_phy_anchor;

   /*
    * This field stores the current smp phy on a destination device's smp phy list whose
    * attached device's sas address is to be edited into this smp device's route table.
    * When one config route info response is processed, we can find the next smp phy to edit
    * using this field's value.
    */
   struct SCIF_SAS_SMP_PHY * curr_config_route_destination_smp_phy;

   /*
    * This field stores the current smp phy to which a PHY CONTROL (clear affiliation)
    * command is sent out.
    */
   struct SCIF_SAS_SMP_PHY * curr_clear_affiliation_phy;

   /**
    * This field is to indicate a smp activity for this smp device is
    * to be started (not yet). The scheduled activity could be Discover or Config
    * Route Table.
    */
   U8 scheduled_activity;

   /**
    * This timer is used for waiting before retrying a smp request, or before
    * sending Discover request after Phy Control during Target Reset.
    */
   void * smp_activity_timer;

   /**
    * This field save the retry count for internal smp request. Since when
    * an internal smp request gets retried, it has been destructed already.
    */
   U8 io_retry_count;

   /**
    * This field stores the number of phys for expander device found by decoding
    * the SMP REPORT GENERAL response.
    */
   U8  number_of_phys;

   /**
    * This field indicates the maximum number of expander route indexes per phy for
    * this expander device.
    */
   U16 expander_route_indexes;

   /**
    * This field indicates whether an expander device supports table-to-table
    * connection.
    */
   BOOL is_table_to_table_supported;

   /**
    * This field indicates whether an expander device is externally configurable.
    * If it is, it is not self-configuring and is not able to config others.
    */
   BOOL is_externally_configurable;

   /**
    * This field indicates whether an expander device is able to config others.
    */
   BOOL is_able_to_config_others;

   /**
    * This field contains the list of all smp phys that connect to another smp phy.
    */
   SCI_FAST_LIST_T smp_phy_list;

}SCIF_SAS_SMP_REMOTE_DEVICE_T;

void scif_sas_smp_remote_device_clear(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

void scif_sas_smp_remote_device_construct(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

SCI_STATUS scif_sas_smp_remote_device_decode_smp_response(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   struct SCIF_SAS_REQUEST       * fw_request,
   void                          * response_data,
   SCI_IO_STATUS                   completion_status
);

SCI_STATUS scif_sas_smp_remote_device_decode_report_general_response(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   SMP_RESPONSE_T                * smp_response
);

SCI_STATUS scif_sas_smp_remote_device_decode_initial_discover_response(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   SMP_RESPONSE_T                * smp_response
);

SCI_STATUS scif_sas_smp_remote_device_decode_report_phy_sata_response(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   SMP_RESPONSE_T                * smp_response
);

SCI_STATUS scif_sas_smp_remote_device_decode_target_reset_phy_control_response(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   SMP_RESPONSE_T           * smp_response
);

SCI_STATUS scif_sas_smp_remote_device_decode_discover_phy_control_response(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   SMP_RESPONSE_T           * smp_response
);

SCI_STATUS scif_sas_smp_remote_device_decode_target_reset_discover_response(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   SMP_RESPONSE_T           * smp_response
);

SCI_STATUS scif_sas_smp_remote_device_decode_spinup_hold_release_discover_response(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   SMP_RESPONSE_T           * smp_response
);

SCI_STATUS scif_sas_smp_remote_device_decode_config_route_info_response(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   SMP_RESPONSE_T           * smp_response
);

void scif_sas_smp_remote_device_start_discover(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

void scif_sas_smp_remote_device_continue_discover(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

void scif_sas_smp_remote_device_finish_initial_discover(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

void scif_sas_smp_remote_device_finish_discover(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

void scif_sas_smp_remote_device_continue_target_reset(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   struct SCIF_SAS_REQUEST       * fw_request
);

void scif_sas_smp_remote_device_fail_discover(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

void scif_sas_smp_remote_device_fail_target_reset(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   struct SCIF_SAS_REQUEST       * fw_request
);

void scif_sas_smp_remote_device_continue_current_activity(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   struct SCIF_SAS_REQUEST       * fw_request,
   SCI_STATUS                      status
);

void scif_sas_smp_remote_device_target_reset_poll(
   struct SCIF_SAS_REQUEST       * fw_request
);

void scif_sas_smp_remote_device_sata_spinup_hold_release(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

void scif_sas_smp_remote_device_fail_target_spinup_hold_release(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   struct SCIF_SAS_REMOTE_DEVICE * target_device
);

void scif_sas_smp_remote_device_retry_internal_io(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   U8                         io_retry_count,
   U32                        delay
);

BOOL scif_sas_smp_remote_device_is_in_activity(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

SCIF_SAS_SMP_PHY_T * scif_sas_smp_remote_device_find_smp_phy_by_id(
   U8                                  phy_identifier,
   struct SCIF_SAS_SMP_REMOTE_DEVICE * smp_remote_device
);

void scif_sas_smp_remote_device_removed(
   struct SCIF_SAS_REMOTE_DEVICE * this_device
);

void scif_sas_smp_remote_device_terminated_request_handler(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   struct SCIF_SAS_REQUEST       * fw_request
);

void scif_sas_smp_remote_device_populate_smp_phy_list(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

SCI_STATUS scif_sas_smp_remote_device_save_smp_phy_info(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device,
   SMP_RESPONSE_DISCOVER_T       * discover_response
);

#ifdef SCI_SMP_PHY_LIST_DEBUG_PRINT
void scif_sas_smp_remote_device_print_smp_phy_list(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);
#endif

void scif_sas_smp_remote_device_configure_upstream_expander_route_info(
   struct SCIF_SAS_REMOTE_DEVICE * this_device
);

struct SCIF_SAS_REMOTE_DEVICE * scif_sas_remote_device_find_upstream_expander(
   struct SCIF_SAS_REMOTE_DEVICE * this_device
);

struct SCIF_SAS_REMOTE_DEVICE * scif_sas_remote_device_find_downstream_expander(
   struct SCIF_SAS_REMOTE_DEVICE * this_device
);

BOOL scif_sas_smp_remote_device_do_config_route_info(
   struct SCIF_SAS_REMOTE_DEVICE * device_being_config,
   struct SCIF_SAS_SMP_PHY       * destination_smp_phy
);

void scif_sas_smp_remote_device_configure_route_table(
   struct SCIF_SAS_REMOTE_DEVICE * device_being_config
);

void scif_sas_smp_remote_device_clean_route_table(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

void scif_sas_smp_remote_device_clean_route_table_entry(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

void scif_sas_smp_remote_device_cancel_config_route_table_activity(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

void scif_sas_smp_remote_device_cancel_smp_activity(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

U8 scif_sas_smp_remote_device_get_config_route_table_method(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

void scif_sas_smp_remote_device_start_clear_affiliation(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

void scif_sas_smp_remote_device_continue_clear_affiliation(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

void scif_sas_smp_remote_device_finish_clear_affiliation(
   struct SCIF_SAS_REMOTE_DEVICE * fw_device
);

void scif_sas_smp_remote_device_start_target_reset(
   struct SCIF_SAS_REMOTE_DEVICE * expander_device,
   struct SCIF_SAS_REMOTE_DEVICE * target_device,
   struct SCIF_SAS_REQUEST       * fw_request
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIF_SAS_SMP_REMOTE_DEVICE_H_
