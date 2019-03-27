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
 * Copyright(c) 2008 - 2010 Intel Corporation. All rights reserved.
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
#ifndef _INTEL_SAS_H_
#define _INTEL_SAS_H_

/**
 * @file
 *
 * @brief This file contains all of the definitions relating to structures,
 *        constants, etc. defined by the SAS specification.
 */

#include <dev/isci/types.h>
#include <dev/isci/scil/intel_sata.h>
#include <dev/isci/scil/intel_scsi.h>

/**
 * @struct SCI_SAS_ADDRESS
 * @brief  This structure depicts how a SAS address is represented by SCI.
 */
typedef struct SCI_SAS_ADDRESS
{
   /**
    * This member contains the higher 32-bits of the SAS address.
    */
   U32 high;

   /**
    * This member contains the lower 32-bits of the SAS address.
    */
   U32 low;

} SCI_SAS_ADDRESS_T;

/**
 * @struct SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS
 * @brief  This structure depicts the contents of bytes 2 and 3 in the
 *         SAS IDENTIFY ADDRESS FRAME (IAF).
 *         @note For specific information on each of these
 *               individual fields please reference the SAS specification
 *               Link layer section on address frames.
 */
typedef struct SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS
{
   union
   {
      struct
      {
         U16  restricted1        : 1;
         U16  smp_initiator      : 1;
         U16  stp_initiator      : 1;
         U16  ssp_initiator      : 1;
         U16  reserved3          : 4;
         U16  restricted2        : 1;
         U16  smp_target         : 1;
         U16  stp_target         : 1;
         U16  ssp_target         : 1;
         U16  reserved4          : 4;
      } bits;

      U16 all;
   } u;

} SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T;

/**
 * @struct SCI_SAS_IDENTIFY_ADDRESS_FRAME
 * @brief  This structure depicts the contents of the SAS IDENTIFY ADDRESS
 *         FRAME (IAF).
 *         @note For specific information on each of these
 *               individual fields please reference the SAS specification
 *               Link layer section on address frames.
 */
typedef struct SCI_SAS_IDENTIFY_ADDRESS_FRAME
{
   U16  address_frame_type : 4;
   U16  device_type        : 3;
   U16  reserved1          : 1;
   U16  reason             : 4;
   U16  reserved2          : 4;

   SCI_SAS_IDENTIFY_ADDRESS_FRAME_PROTOCOLS_T protocols;

   SCI_SAS_ADDRESS_T  device_name;
   SCI_SAS_ADDRESS_T  sas_address;

   U32  phy_identifier      : 8;
   U32  break_reply_capable : 1;
   U32  requested_in_zpsds  : 1;
   U32  in_zpsds_persistent : 1;
   U32  reserved5           : 21;

   U32  reserved6[4];

} SCI_SAS_IDENTIFY_ADDRESS_FRAME_T;

/**
 * @struct SAS_CAPABILITIES
 * @brief  This structure depicts the various SAS capabilities supported
 *         by the directly attached target device.  For specific information
 *         on each of these individual fields please reference the SAS
 *         specification Phy layer section on speed negotiation windows.
 */
typedef struct SAS_CAPABILITIES
{
   union
   {
#if defined (SCIC_SDS_4_ENABLED)
      struct
      {
         /**
          * The SAS specification indicates the start bit shall always be set to
          * 1.  This implementation will have the start bit set to 0 if the
          * PHY CAPABILITIES were either not received or speed negotiation failed.
          */
         U32  start                       : 1;
         U32  tx_ssc_type                 : 1;
         U32  reserved1                   : 2;
         U32  requested_logical_link_rate : 4;

         U32  gen1_without_ssc_supported  : 1;
         U32  gen1_with_ssc_supported     : 1;
         U32  gen2_without_ssc_supported  : 1;
         U32  gen2_with_ssc_supported     : 1;
         U32  gen3_without_ssc_supported  : 1;
         U32  gen3_with_ssc_supported     : 1;
         U32  reserved2                   : 17;
         U32  parity                      : 1;
      } bits;
#endif // (SCIC_SDS_4_ENABLED)

      U32 all;
   } u;

} SAS_CAPABILITIES_T;

/**
 * @enum  _SCI_SAS_LINK_RATE
 * @brief This enumeration depicts the SAS specification defined link speeds.
 */
typedef enum _SCI_SAS_LINK_RATE
{
   SCI_SAS_NO_LINK_RATE = 0,
   SCI_SATA_SPINUP_HOLD = 0x3,
   SCI_SAS_150_GB = 0x8,
   SCI_SAS_300_GB = 0x9,
   SCI_SAS_600_GB = 0xA
} SCI_SAS_LINK_RATE;

/**
 * @enum  _SCI_SAS_TASK_ATTRIBUTE
 * @brief This enumeration depicts the SAM/SAS specification defined task
 *        attribute values for a command information unit.
 */
typedef enum _SCI_SAS_TASK_ATTRIBUTE
{
   SCI_SAS_SIMPLE_ATTRIBUTE = 0,
   SCI_SAS_HEAD_OF_QUEUE_ATTRIBUTE = 1,
   SCI_SAS_ORDERED_ATTRIBUTE = 2,
   SCI_SAS_ACA_ATTRIBUTE = 4,
} SCI_SAS_TASK_ATTRIBUTE;

/**
 * @enum  _SCI_SAS_TASK_MGMT_FUNCTION
 * @brief This enumeration depicts the SAM/SAS specification defined task
 *        management functions.
 *        @note This HARD_RESET function listed here is not actually defined
 *              as a task management function in the industry standard.
 */
typedef enum _SCI_SAS_TASK_MGMT_FUNCTION
{
   SCI_SAS_ABORT_TASK = SCSI_TASK_REQUEST_ABORT_TASK,
   SCI_SAS_ABORT_TASK_SET = SCSI_TASK_REQUEST_ABORT_TASK_SET,
   SCI_SAS_CLEAR_TASK_SET = SCSI_TASK_REQUEST_CLEAR_TASK_SET,
   SCI_SAS_LOGICAL_UNIT_RESET = SCSI_TASK_REQUEST_LOGICAL_UNIT_RESET,
   SCI_SAS_I_T_NEXUS_RESET = SCSI_TASK_REQUEST_I_T_NEXUS_RESET,
   SCI_SAS_CLEAR_ACA = SCSI_TASK_REQUEST_CLEAR_ACA,
   SCI_SAS_QUERY_TASK = SCSI_TASK_REQUEST_QUERY_TASK,
   SCI_SAS_QUERY_TASK_SET = SCSI_TASK_REQUEST_QUERY_TASK_SET,
   SCI_SAS_QUERY_ASYNCHRONOUS_EVENT = SCSI_TASK_REQUEST_QUERY_UNIT_ATTENTION,
   SCI_SAS_HARD_RESET = 0xFF
} SCI_SAS_TASK_MGMT_FUNCTION_T;


/**
 * @enum  _SCI_SAS_FRAME_TYPE
 * @brief This enumeration depicts the SAS specification defined SSP frame
 *        types.
 */
typedef enum _SCI_SAS_FRAME_TYPE
{
   SCI_SAS_DATA_FRAME = 0x01,
   SCI_SAS_XFER_RDY_FRAME = 0x05,
   SCI_SAS_COMMAND_FRAME = 0x06,
   SCI_SAS_RESPONSE_FRAME = 0x07,
   SCI_SAS_TASK_FRAME = 0x16
} SCI_SAS_FRAME_TYPE_T;


/**
 * @struct SCI_SSP_COMMAND_IU
 * @brief This structure depicts the contents of the SSP COMMAND
 *        INFORMATION UNIT. For specific information on each of these
 *        individual fields please reference the SAS specification SSP
 *        transport layer section.
 */
typedef struct SCI_SSP_COMMAND_IU
{

   U32 lun[2];

   U32 additional_cdb_length  : 6;
   U32 reserved0              : 2;
   U32 reserved1              : 8;
   U32 enable_first_burst     : 1;
   U32 task_priority          : 4;
   U32 task_attribute         : 3;
   U32 reserved2              : 8;

   U32 cdb[4];

} SCI_SSP_COMMAND_IU_T;

/**
 * @struct SCI_SSP_TASK_IU
 * @brief This structure depicts the contents of the SSP TASK INFORMATION
 *        UNIT. For specific information on each of these individual fields
 *        please reference the SAS specification SSP transport layer
 *        section.
 */
typedef struct SCI_SSP_TASK_IU
{
   U32 lun_upper;
   U32 lun_lower;

   U32 reserved0     : 8;
   U32 task_function : 8;
   U32 reserved1     : 8;
   U32 reserved2     : 8;

   U32 reserved3     : 16;
   U32 task_tag      : 16;

   U32 reserved4[3];

} SCI_SSP_TASK_IU_T;

#define SSP_RESPONSE_IU_MAX_DATA 64

#define SCI_SSP_RESPONSE_IU_DATA_PRESENT_MASK   (0x03)

/**
 * @struct SCI_SSP_RESPONSE_IU
 * @brief This structure depicts the contents of the SSP RESPONSE
 *        INFORMATION UNIT. For specific information on each of these
 *        individual fields please reference the SAS specification SSP
 *        transport layer section.
 */
typedef struct SCI_SSP_RESPONSE_IU
{
   U8  reserved0[8];

   U8  retry_delay_timer[2];
   U8  data_present;
   U8  status;

   U8  reserved1[4];
   U8  sense_data_length[4];
   U8  response_data_length[4];

   U32 data[SSP_RESPONSE_IU_MAX_DATA];

} SCI_SSP_RESPONSE_IU_T;

/**
 * @enum  _SCI_SAS_DATA_PRESENT_TYPE
 * @brief This enumeration depicts the SAS specification defined SSP data present
 *        types in SCI_SSP_RESPONSE_IU.
 */
typedef enum _SCI_SSP_RESPONSE_IU_DATA_PRESENT_TYPE
{
   SCI_SSP_RESPONSE_IU_NO_DATA = 0x00,
   SCI_SSP_RESPONSE_IU_RESPONSE_DATA = 0x01,
   SCI_SSP_RESPONSE_IU_SENSE_DATA = 0x02
} SCI_SSP_RESPONSE_IU_DATA_PRESENT_TYPE_T;

/**
 * @struct SCI_SSP_FRAME_HEADER
 *
 * @brief This structure depicts the contents of an SSP frame header.  For
 *        specific information on the individual fields please reference
 *        the SAS specification transport layer SSP frame format.
 */
typedef struct SCI_SSP_FRAME_HEADER
{
   // Word 0
   U32 hashed_destination_address  :24;
   U32 frame_type                  : 8;

   // Word 1
   U32 hashed_source_address       :24;
   U32 reserved1_0                 : 8;

   // Word 2
   U32 reserved2_2                 : 6;
   U32 fill_bytes                  : 2;
   U32 reserved2_1                 : 3;
   U32 tlr_control                 : 2;
   U32 retry_data_frames           : 1;
   U32 retransmit                  : 1;
   U32 changing_data_pointer       : 1;
   U32 reserved2_0                 :16;

   // Word 3
   U32 uiResv4;

   // Word 4
   U16 target_port_transfer_tag;
   U16 tag;

   // Word 5
   U32 data_offset;

} SCI_SSP_FRAME_HEADER_T;

/**
 * @struct SMP_REQUEST_HEADER
 * @brief  This structure defines the contents of an SMP Request header.
 *         @note For specific information on each of these
 *               individual fields please reference the SAS specification.
 */
typedef struct SMP_REQUEST_HEADER
{
   U8 smp_frame_type;                // byte 0
   U8 function;                      // byte 1
   U8 allocated_response_length;     // byte 2
   U8 request_length;                // byte 3
} SMP_REQUEST_HEADER_T;

/**
 * @struct SMP_RESPONSE_HEADER
 * @brief  This structure depicts the contents of the SAS SMP DISCOVER
 *         RESPONSE frame.  For specific information on each of these
 *         individual fields please reference the SAS specification Link
 *         layer section on address frames.
 */
typedef struct SMP_RESPONSE_HEADER
{
   U8 smp_frame_type;      // byte 0
   U8 function;            // byte 1
   U8 function_result;     // byte 2
   U8 response_length;     // byte 3
} SMP_RESPONSE_HEADER_T;

/**
 * @struct SMP_REQUEST_GENERAL
 * @brief  This structure defines the contents of an SMP Request that
 *         is comprised of the SMP_REQUEST_HEADER and a CRC.
 *         @note For specific information on each of these
 *               individual fields please reference the SAS specification.
 */
typedef struct SMP_REQUEST_GENERAL
{
  U32 crc;            // bytes 4-7

} SMP_REQUEST_GENERAL_T;

/**
 * @struct SMP_REQUEST_PHY_IDENTIFIER
 * @brief  This structure defines the contents of an SMP Request that
 *         is comprised of the SMP_REQUEST_HEADER and a phy identifier.
 *         Examples: SMP_REQUEST_DISCOVER, SMP_REQUEST_REPORT_PHY_SATA.
 *         @note For specific information on each of these
 *               individual fields please reference the SAS specification.
 */
typedef struct SMP_REQUEST_PHY_IDENTIFIER
{
  U32 reserved_byte4_7;      // bytes 4-7

  U32 ignore_zone_group:1;    // byte 8
  U32 reserved_byte8:7;

  U32 phy_identifier:8;       // byte 9
  U32 reserved_byte10:8;      // byte 10
  U32 reserved_byte11:8;      // byte 11

} SMP_REQUEST_PHY_IDENTIFIER_T;

/**
 * @struct SMP_REQUEST_CONFIGURE_ROUTE_INFORMATION
 * @brief  This structure defines the contents of an SMP Configure Route
 *         Information request.
 *         @note For specific information on each of these
 *               individual fields please reference the SAS specification.
 */
typedef struct SMP_REQUEST_CONFIGURE_ROUTE_INFORMATION
{
  U32 expected_expander_change_count:16;    // bytes 4-5
  U32 expander_route_index_high:8;
  U32 expander_route_index:8;              // bytes 6-7

  U32 reserved_byte8:8;           // bytes 8
  U32 phy_identifier:8;           // bytes 9
  U32 reserved_byte_10_11:16;     // bytes 10-11

  U32 reserved_byte_12_bit_0_6:7;
  U32 disable_route_entry:1;    // byte 12
  U32 reserved_byte_13_15:24;   // bytes 13-15

  U32 routed_sas_address[2];    // bytes 16-23
  U8 reserved_byte_24_39[16];    // bytes 24-39

} SMP_REQUEST_CONFIGURE_ROUTE_INFORMATION_T;

/**
 * @struct SMP_REQUEST_PHY_CONTROL
 * @brief  This structure defines the contents of an SMP Phy Controller
 *         request.
 *         @note For specific information on each of these
 *               individual fields please reference the SAS specification.
 */
typedef struct SMP_REQUEST_PHY_CONTROL
{
  U16 expected_expander_change_count;   // byte 4-5

  U16 reserved_byte_6_7;   // byte 6-7
  U8 reserved_byte_8;      // byte 8

  U8 phy_identifier;       // byte 9
  U8 phy_operation;        // byte 10

  U8 update_partial_pathway_timeout_value:1;
  U8 reserved_byte_11_bit_1_7:7;   // byte 11

  U8 reserved_byte_12_23[12];      // byte 12-23

  U8 attached_device_name[8];      // byte 24-31

  U8 reserved_byte_32_bit_3_0:4;   // byte 32
  U8 programmed_minimum_physical_link_rate:4;

  U8 reserved_byte_33_bit_3_0:4;   // byte 33
  U8 programmed_maximum_physical_link_rate:4;

  U16 reserved_byte_34_35;      // byte 34-35

  U8 partial_pathway_timeout_value:4;
  U8 reserved_byte_36_bit_4_7:4;  // byte 36

  U16 reserved_byte_37_38;  // byte 37-38
  U8 reserved_byte_39;      // byte 39

} SMP_REQUEST_PHY_CONTROL_T;

/**
 * @struct SMP_REQUEST_VENDOR_SPECIFIC
 * @brief  This structure depicts the vendor specific space for SMP request.
 */
 #define SMP_REQUEST_VENDOR_SPECIFIC_MAX_LENGTH 1016
typedef struct SMP_REQUEST_VENDOR_SPECIFIC
{
   U8 request_bytes[SMP_REQUEST_VENDOR_SPECIFIC_MAX_LENGTH];
}SMP_REQUEST_VENDOR_SPECIFIC_T;

/**
 * @struct SMP_REQUEST
 * @brief  This structure simply unionizes the existing request
 *         structures into a common request type.
 */
typedef struct _SMP_REQUEST
{
  SMP_REQUEST_HEADER_T header;

  union
  {            // bytes 4-N
    SMP_REQUEST_GENERAL_T                       report_general;
    SMP_REQUEST_PHY_IDENTIFIER_T                discover;
    SMP_REQUEST_GENERAL_T                       report_manufacturer_information;
    SMP_REQUEST_PHY_IDENTIFIER_T                report_phy_sata;
    SMP_REQUEST_PHY_CONTROL_T                   phy_control;
    SMP_REQUEST_PHY_IDENTIFIER_T                report_phy_error_log;
    SMP_REQUEST_PHY_IDENTIFIER_T                report_route_information;
    SMP_REQUEST_CONFIGURE_ROUTE_INFORMATION_T   configure_route_information;
    SMP_REQUEST_VENDOR_SPECIFIC_T               vendor_specific_request;
  } request;

} SMP_REQUEST_T;


/**
 * @struct SMP_RESPONSE_REPORT_GENERAL
 * @brief  This structure depicts the SMP Report General for
 *         expander devices.  It adheres to the SAS-2.1 specification.
 *         @note For specific information on each of these
 *               individual fields please reference the SAS specification
 *               Application layer section on SMP.
 */
typedef struct SMP_RESPONSE_REPORT_GENERAL
{
  U16 expander_change_count;  //byte 4-5
  U16 expander_route_indexes; //byte 6-7

  U32 reserved_byte8:7;        //byte 8 bit 0-6
  U32 long_response:1;         //byte 8 bit 7

  U32 number_of_phys:8;        //byte 9

  U32 configurable_route_table:1; //byte 10
  U32 configuring:1;
  U32 configures_others:1;
  U32 open_reject_retry_supported:1;
  U32 stp_continue_awt:1;
  U32 self_configuring:1;
  U32 zone_configuring:1;
  U32 table_to_table_supported:1;

  U32 reserved_byte11:8;       //byte 11

  U32 enclosure_logical_identifier_high; //byte 12-15
  U32 enclosure_logical_identifier_low;  //byte 16-19

  U32 reserved_byte20_23;
  U32 reserved_byte24_27;

} SMP_RESPONSE_REPORT_GENERAL_T;

typedef struct SMP_RESPONSE_REPORT_GENERAL_LONG
{
   SMP_RESPONSE_REPORT_GENERAL_T sas1_1;

   struct
   {
      U16 reserved1;
      U16 stp_bus_inactivity_time_limit;
      U16 stp_max_connect_time_limit;
      U16 stp_smp_i_t_nexus_loss_time;

      U32 zoning_enabled                         : 1;
      U32 zoning_supported                       : 1;
      U32 physicaL_presence_asserted             : 1;
      U32 zone_locked                            : 1;
      U32 reserved2                              : 1;
      U32 num_zone_groups                        : 3;
      U32 saving_zoning_enabled_supported        : 3;
      U32 saving_zone_perms_table_supported      : 1;
      U32 saving_zone_phy_info_supported         : 1;
      U32 saving_zone_manager_password_supported : 1;
      U32 saving                                 : 1;
      U32 reserved3                              : 1;
      U32 max_number_routed_sas_addresses        : 16;

      SCI_SAS_ADDRESS_T active_zone_manager_sas_address;

      U16 zone_lock_inactivity_time_limit;
      U16 reserved4;

      U8 reserved5;
      U8 first_enclosure_connector_element_index;
      U8 number_of_enclosure_connector_element_indices;
      U8 reserved6;

      U32 reserved7                            : 7;
      U32 reduced_functionality                : 1;
      U32 time_to_reduce_functionality         : 8;
      U32 initial_time_to_reduce_functionality : 8;
      U8  max_reduced_functionality_time;

      U16 last_self_config_status_descriptor_index;
      U16 max_number_of_stored_self_config_status_descriptors;

      U16 last_phy_event_list_descriptor_index;
      U16 max_number_of_stored_phy_event_list_descriptors;
   } sas2;

} SMP_RESPONSE_REPORT_GENERAL_LONG_T;

/**
 * @struct SMP_RESPONSE_REPORT_MANUFACTURER_INFORMATION
 * @brief  This structure depicts the SMP report manufacturer
 *         information for expander devices.  It adheres to the
 *         SAS-2.1 specification.
 *         @note For specific information on each of these
 *               individual fields please reference the SAS specification
 *               Application layer section on SMP.
 */
typedef struct SMP_RESPONSE_REPORT_MANUFACTURER_INFORMATION
{
  U32 expander_change_count : 16;    // bytes 4-5
  U32 reserved1             : 16;

  U32 sas1_1_format         : 1;
  U32 reserved2             : 31;

  U8  vendor_id[8];
  U8  product_id[16];
  U8  product_revision_level[4];
  U8  component_vendor_id[8];
  U8  component_id[2];
  U8  component_revision_level;
  U8  reserved3;
  U8  vendor_specific[8];

} SMP_RESPONSE_REPORT_MANUFACTURER_INFORMATION_T;

#define SMP_RESPONSE_DISCOVER_FORMAT_1_1_SIZE 52
#define SMP_RESPONSE_DISCOVER_FORMAT_2_SIZE   116

/**
 * @struct SMP_DISCOVER_RESPONSE_PROTOCOLS
 * @brief  This structure depicts the discover response where the
 *         supported protocols by the remote phy are specified.
 *         @note For specific information on each of these
 *               individual fields please reference the SAS specification
 *               Link layer section on address frames.
 */
typedef struct SMP_DISCOVER_RESPONSE_PROTOCOLS
{
   union
   {
      struct
      {
         U16  attached_sata_host           : 1;
         U16  attached_smp_initiator       : 1;
         U16  attached_stp_initiator       : 1;
         U16  attached_ssp_initiator       : 1;
         U16  reserved3                    : 4;
         U16  attached_sata_device         : 1;
         U16  attached_smp_target          : 1;
         U16  attached_stp_target          : 1;
         U16  attached_ssp_target          : 1;
         U16  reserved4                    : 3;
         U16  attached_sata_port_selector  : 1;
      } bits;

      U16 all;
   } u;

} SMP_DISCOVER_RESPONSE_PROTOCOLS_T;

/**
 * @struct SMP_RESPONSE_DISCOVER_FORMAT
 * @brief  This structure defines the SMP phy discover response format.
 *         It handles both SAS1.1 and SAS 2 definitions.  The unions
 *         indicate locations where the SAS specification versions
 *         differ from one another.
 */
typedef struct SMP_RESPONSE_DISCOVER
{

   union
   {
      struct
      {
         U8 reserved[2];
      } sas1_1;

      struct
      {
         U16  expander_change_count;
      } sas2;

   } u1;

   U8   reserved1[3];
   U8   phy_identifier;
   U8   reserved2[2];

   union
   {
      struct
      {
         U16  reserved1                     : 4;
         U16  attached_device_type          : 3;
         U16  reserved2                     : 1;
         U16  negotiated_physical_link_rate : 4;
         U16  reserved3                     : 4;
      } sas1_1;

      struct
      {
         U16  attached_reason              : 4;
         U16  attached_device_type         : 3;
         U16  reserved2                    : 1;
         U16  negotiated_logical_link_rate : 4;
         U16  reserved3                    : 4;
      } sas2;

   } u2;

   SMP_DISCOVER_RESPONSE_PROTOCOLS_T protocols;
   SCI_SAS_ADDRESS_T  sas_address;
   SCI_SAS_ADDRESS_T  attached_sas_address;

   U8   attached_phy_identifier;

   union
   {
      struct
      {
         U8   reserved;
      } sas1_1;

      struct
      {
         U8   attached_break_reply_capable     : 1;
         U8   attached_requested_inside_zpsds  : 1;
         U8   attached_inside_zpsds_persistent : 1;
         U8   reserved1                        : 5;
      } sas2;

   } u3;

   U8   reserved_for_identify[6];

   U32  hardware_min_physical_link_rate   : 4;
   U32  programmed_min_physical_link_rate : 4;
   U32  hardware_max_physical_link_rate   : 4;
   U32  programmed_max_physical_link_rate : 4;
   U32  phy_change_count                  : 8;
   U32  partial_pathway_timeout_value     : 4;
   U32  reserved5                         : 3;
   U32  virtual_phy                       : 1;

   U32  routing_attribute                 : 4;
   U32  reserved6                         : 4;
   U32  connector_type                    : 7;
   U32  reserved7                         : 1;
   U32  connector_element_index           : 8;
   U32  connector_physical_link           : 8;

   U16  reserved8;
   U16  vendor_specific;

   union
   {
      struct
      {
         /**
          * In the SAS 1.1 specification this structure ends after 52 bytes.
          * As a result, the contents of this field should never have a
          * real value.  It is undefined.
          */
         U8 undefined[SMP_RESPONSE_DISCOVER_FORMAT_2_SIZE
                      - SMP_RESPONSE_DISCOVER_FORMAT_1_1_SIZE];
      } sas1_1;

      struct
      {
         SCI_SAS_ADDRESS_T attached_device_name;

         U32  zoning_enabled                             : 1;
         U32  inside_zpsds                               : 1;
         U32  zone_group_persistent                      : 1;
         U32  reserved1                                  : 1;
         U32  requested_inside_zpsds                     : 1;
         U32  inside_zpsds_persistent                    : 1;
         U32  requested_inside_zpsds_changed_by_expander : 1;
         U32  reserved2                                  : 1;
         U32  reserved_for_zoning_fields                 : 16;
         U32  zone_group                                 : 8;

         U8   self_configuration_status;
         U8   self_configuration_levels_completed;
         U16  reserved_for_self_config_fields;

         SCI_SAS_ADDRESS_T self_configuration_sas_address;

         U32  programmed_phy_capabilities;
         U32  current_phy_capabilities;
         U32  attached_phy_capabilities;

         U32  reserved3;

         U32  reserved4                     : 16;
         U32  negotiated_physical_link_rate : 4;
         U32  reason                        : 4;
         U32  hardware_muxing_supported     : 1;
         U32  negotiated_ssc                : 1;
         U32  reserved5                     : 6;

         U32  default_zoning_enabled          : 1;
         U32  reserved6                       : 1;
         U32  default_zone_group_persistent   : 1;
         U32  reserved7                       : 1;
         U32  default_requested_inside_zpsds  : 1;
         U32  default_inside_zpsds_persistent : 1;
         U32  reserved8                       : 2;
         U32  reserved9                       : 16;
         U32  default_zone_group              : 8;

         U32  saved_zoning_enabled          : 1;
         U32  reserved10                    : 1;
         U32  saved_zone_group_persistent   : 1;
         U32  reserved11                    : 1;
         U32  saved_requested_inside_zpsds  : 1;
         U32  saved_inside_zpsds_persistent : 1;
         U32  reserved12                    : 18;
         U32  saved_zone_group              : 8;

         U32  reserved14                     : 2;
         U32  shadow_zone_group_persistent   : 1;
         U32  reserved15                     : 1;
         U32  shadow_requested_inside_zpsds  : 1;
         U32  shadow_inside_zpsds_persistent : 1;
         U32  reserved16                     : 18;
         U32  shadow_zone_group              : 8;

         U8   device_slot_number;
         U8   device_slot_group_number;
         U8   device_slot_group_output_connector[6];
      } sas2;

   } u4;

} SMP_RESPONSE_DISCOVER_T;

/**
 * @struct SMP_RESPONSE_REPORT_PHY_SATA
 * @brief  This structure depicts the contents of the SAS SMP REPORT
 *         PHY SATA frame.  For specific information on each of these
 *         individual fields please reference the SAS specification Link
 *         layer section on address frames.
 */
typedef struct SMP_RESPONSE_REPORT_PHY_SATA
{
  U32 ignored_byte_4_7;       // bytes 4-7

  U32  affiliations_valid:1;
  U32  affiliations_supported:1;
  U32  reserved_byte11:6;     // byte 11
  U32  ignored_byte10:8;      // byte 10
  U32  phy_identifier:8;      // byte  9
  U32  reserved_byte_8:8;     // byte  8

  U32  reserved_12_15;
  U32  stp_sas_address[2];
  U8   device_to_host_fis[20];
  U32  reserved_44_47;
  U32  affiliated_stp_initiator_sas_address[2];

} SMP_RESPONSE_REPORT_PHY_SATA_T;

typedef struct SMP_RESPONSE_VENDOR_SPECIFIC
{
   U8 response_bytes[SMP_REQUEST_VENDOR_SPECIFIC_MAX_LENGTH];
}SMP_RESPONSE_VENDOR_SPECIFIC_T;

typedef union SMP_RESPONSE_BODY
{
   SMP_RESPONSE_REPORT_GENERAL_T report_general;
   SMP_RESPONSE_REPORT_MANUFACTURER_INFORMATION_T report_manufacturer_information;
   SMP_RESPONSE_DISCOVER_T discover;
   SMP_RESPONSE_REPORT_PHY_SATA_T report_phy_sata;
   SMP_RESPONSE_VENDOR_SPECIFIC_T vendor_specific_response;
} SMP_RESPONSE_BODY_T;

/**
 * @struct SMP_RESPONSE
 * @brief  This structure simply unionizes the existing response
 *         structures into a common response type.
 */
typedef struct _SMP_RESPONSE
{
   SMP_RESPONSE_HEADER_T header;

   SMP_RESPONSE_BODY_T   response;

} SMP_RESPONSE_T;

// SMP Request Functions
#define SMP_FUNCTION_REPORT_GENERAL                   0x00
#define SMP_FUNCTION_REPORT_MANUFACTURER_INFORMATION  0x01
#define SMP_FUNCTION_DISCOVER                         0x10
#define SMP_FUNCTION_REPORT_PHY_ERROR_LOG             0x11
#define SMP_FUNCTION_REPORT_PHY_SATA                  0x12
#define SMP_FUNCTION_REPORT_ROUTE_INFORMATION         0X13
#define SMP_FUNCTION_CONFIGURE_ROUTE_INFORMATION      0X90
#define SMP_FUNCTION_PHY_CONTROL                      0x91
#define SMP_FUNCTION_PHY_TEST                         0x92

#define SMP_FRAME_TYPE_REQUEST          0x40
#define SMP_FRAME_TYPE_RESPONSE         0x41

#define PHY_OPERATION_NOP               0x00
#define PHY_OPERATION_LINK_RESET        0x01
#define PHY_OPERATION_HARD_RESET        0x02
#define PHY_OPERATION_DISABLE           0x03
#define PHY_OPERATION_CLEAR_ERROR_LOG   0x05
#define PHY_OPERATION_CLEAR_AFFILIATION 0x06

#define NPLR_PHY_ENABLED_UNK_LINK_RATE 0x00
#define NPLR_PHY_DISABLED     0x01
#define NPLR_PHY_ENABLED_SPD_NEG_FAILED   0x02
#define NPLR_PHY_ENABLED_SATA_HOLD  0x03
#define NPLR_PHY_ENABLED_1_5G    0x08
#define NPLR_PHY_ENABLED_3_0G    0x09

// SMP Function Result values.
#define SMP_RESULT_FUNCTION_ACCEPTED              0x00
#define SMP_RESULT_UNKNOWN_FUNCTION               0x01
#define SMP_RESULT_FUNCTION_FAILED                0x02
#define SMP_RESULT_INVALID_REQUEST_FRAME_LEN      0x03
#define SMP_RESULT_INAVALID_EXPANDER_CHANGE_COUNT 0x04
#define SMP_RESULT_BUSY                           0x05
#define SMP_RESULT_INCOMPLETE_DESCRIPTOR_LIST     0x06
#define SMP_RESULT_PHY_DOES_NOT_EXIST             0x10
#define SMP_RESULT_INDEX_DOES_NOT_EXIST           0x11
#define SMP_RESULT_PHY_DOES_NOT_SUPPORT_SATA      0x12
#define SMP_RESULT_UNKNOWN_PHY_OPERATION          0x13
#define SMP_RESULT_UNKNOWN_PHY_TEST_FUNCTION      0x14
#define SMP_RESULT_PHY_TEST_IN_PROGRESS           0x15
#define SMP_RESULT_PHY_VACANT                     0x16

/* Attached Device Types */
#define SMP_NO_DEVICE_ATTACHED      0
#define SMP_END_DEVICE_ONLY         1
#define SMP_EDGE_EXPANDER_DEVICE    2
#define SMP_FANOUT_EXPANDER_DEVICE  3

/* Expander phy routine attribute */
#define DIRECT_ROUTING_ATTRIBUTE        0
#define SUBTRACTIVE_ROUTING_ATTRIBUTE   1
#define TABLE_ROUTING_ATTRIBUTE         2

#endif // _INTEL_SAS_H_

