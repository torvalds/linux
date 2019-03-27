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
/**
 * @file
 * @brief This file defines all of the ATA related constants, enumerations,
 *        and types.  Please note that this file does not necessarily contain
 *        an exhaustive list of all constants, commands, sub-commands, etc.
 */

#ifndef _ATA_H_
#define _ATA_H_

#include <dev/isci/types.h>

/**
 * @name ATA_COMMAND_CODES
 *
 * These constants depict the various ATA command codes defined
 * in the ATA/ATAPI specification.
 */
/*@{*/
#define ATA_IDENTIFY_DEVICE            0xEC
#define ATA_CHECK_POWER_MODE           0xE5
#define ATA_STANDBY                    0xE2
#define ATA_STANDBY_IMMED              0xE0
#define ATA_IDLE_IMMED                 0xE1
#define ATA_IDLE                       0xE3
#define ATA_FLUSH_CACHE                0xE7
#define ATA_FLUSH_CACHE_EXT            0xEA
#define ATA_READ_DMA_EXT               0x25
#define ATA_READ_DMA                   0xC8
#define ATA_READ_SECTORS_EXT           0x24
#define ATA_READ_SECTORS               0x20
#define ATA_WRITE_DMA_EXT              0x35
#define ATA_WRITE_DMA                  0xCA
#define ATA_WRITE_SECTORS_EXT          0x34
#define ATA_WRITE_SECTORS              0x30
#define ATA_WRITE_UNCORRECTABLE        0x45
#define ATA_READ_VERIFY_SECTORS        0x40
#define ATA_READ_VERIFY_SECTORS_EXT    0x42
#define ATA_READ_BUFFER                0xE4
#define ATA_WRITE_BUFFER               0xE8
#define ATA_EXECUTE_DEVICE_DIAG        0x90
#define ATA_SET_FEATURES               0xEF
#define ATA_SMART                      0xB0
#define ATA_PACKET_IDENTIFY            0xA1
#define ATA_PACKET                     0xA0
#define ATA_READ_FPDMA                 0x60
#define ATA_WRITE_FPDMA                0x61
#define ATA_READ_LOG_EXT               0x2F
#define ATA_NOP                        0x00
#define ATA_DEVICE_RESET               0x08
#define ATA_MEDIA_EJECT                0xED
#define ATA_SECURITY_UNLOCK            0xF2
#define ATA_SECURITY_FREEZE_LOCK       0xF5
#define ATA_DATA_SET_MANAGEMENT        0x06
#define ATA_DOWNLOAD_MICROCODE         0x92
#define ATA_WRITE_STREAM_DMA_EXT       0x3A
#define ATA_READ_LOG_DMA_EXT           0x47
#define ATA_READ_STREAM_DMA_EXT        0x2A
#define ATA_WRITE_DMA_FUA              0x3D
#define ATA_WRITE_LOG_DMA_EXT          0x57
#define ATA_READ_DMA_QUEUED            0xC7
#define ATA_READ_DMA_QUEUED_EXT        0x26
#define ATA_WRITE_DMA_QUEUED           0xCC
#define ATA_WRITE_DMA_QUEUED_EXT       0x36
#define ATA_WRITE_DMA_QUEUED_FUA_EXT   0x3E
#define ATA_READ_MULTIPLE              0xC4
#define ATA_READ_MULTIPLE_EXT          0x29
#define ATA_WRITE_MULTIPLE             0xC5
#define ATA_WRITE_MULTIPLE_EXT         0x39
#define ATA_WRITE_MULTIPLE_FUA_EXT     0xCE


/*@}*/

/**
 * @name ATA_SMART_SUB_COMMAND_CODES
 *
 * These constants define the ATA SMART command sub-codes that can be
 * executed.
 */
/*@{*/
#define ATA_SMART_SUB_CMD_ENABLE        0xD8
#define ATA_SMART_SUB_CMD_DISABLE       0xD9
#define ATA_SMART_SUB_CMD_RETURN_STATUS 0xDA
#define ATA_SMART_SUB_CMD_READ_LOG      0xD5
/*@}*/

/**
 * @name ATA_SET_FEATURES_SUB_COMMAND_CODES
 *
 * These constants define the ATA SET FEATURES command sub-codes that can
 * be executed.
 */
/*@{*/
#define ATA_SET_FEATURES_SUB_CMD_ENABLE_CACHE       0x02
#define ATA_SET_FEATURES_SUB_CMD_DISABLE_CACHE      0x82
#define ATA_SET_FEATURES_SUB_CMD_DISABLE_READ_AHEAD 0x55
#define ATA_SET_FEATURES_SUB_CMD_ENABLE_READ_AHEAD  0xAA
#define ATA_SET_FEATURES_SUB_CMD_SET_TRANSFER_MODE  0x3
/*@}*/

/**
 * @name ATA_READ_LOG_EXT_PAGE_CODES
 *
 * This is a list of log page codes available for use.
 */
/*@{*/
#define ATA_LOG_PAGE_NCQ_ERROR                  0x10
#define ATA_LOG_PAGE_SMART_SELF_TEST            0x06
#define ATA_LOG_PAGE_EXTENDED_SMART_SELF_TEST   0x07
/*@}*/

/**
 * @name ATA_LOG_PAGE_NCQ_ERROR_CONSTANTS
 *
 * These constants define standard values for use when requesting the NCQ
 * error log page.
 */
/*@{*/
#define ATA_LOG_PAGE_NCQ_ERROR_SECTOR        0
#define ATA_LOG_PAGE_NCQ_ERROR_SECTOR_COUNT  1
/*@}*/

/**
 * @name ATA_STATUS_REGISTER_BITS
 *
 * The following are status register bit definitions per ATA/ATAPI-7.
 */
/*@{*/
#define ATA_STATUS_REG_BSY_BIT          0x80
#define ATA_STATUS_REG_DEVICE_FAULT_BIT 0x20
#define ATA_STATUS_REG_ERROR_BIT        0x01
/*@}*/

/**
 * @name ATA_ERROR_REGISTER_BITS
 *
 * The following are error register bit definitions per ATA/ATAPI-7.
 */
/*@{*/
#define ATA_ERROR_REG_NO_MEDIA_BIT              0x02
#define ATA_ERROR_REG_ABORT_BIT                 0x04
#define ATA_ERROR_REG_MEDIA_CHANGE_REQUEST_BIT  0x08
#define ATA_ERROR_REG_ID_NOT_FOUND_BIT          0x10
#define ATA_ERROR_REG_MEDIA_CHANGE_BIT          0x20
#define ATA_ERROR_REG_UNCORRECTABLE_BIT         0x40
#define ATA_ERROR_REG_WRITE_PROTECTED_BIT       0x40
#define ATA_ERROR_REG_ICRC_BIT                  0x80
/*@}*/

/**
 * @name ATA_CONTROL_REGISTER_BITS
 *
 * The following are control register bit definitions per ATA/ATAPI-7
 */
/*@{*/
#define ATA_CONTROL_REG_INTERRUPT_ENABLE_BIT 0x02
#define ATA_CONTROL_REG_SOFT_RESET_BIT       0x04
#define ATA_CONTROL_REG_HIGH_ORDER_BYTE_BIT  0x80
/*@}*/

/**
 * @name ATA_DEVICE_HEAD_REGISTER_BITS
 *
 * The following are device/head register bit definitions per ATA/ATAPI-7.
 */
/*@{*/
#define ATA_DEV_HEAD_REG_LBA_MODE_ENABLE  0x40
#define ATA_DEV_HEAD_REG_FUA_ENABLE       0x80
/*@}*/

/**
 * @name ATA_IDENTIFY_DEVICE_FIELD_LENGTHS
 *
 * The following constants define the number of bytes contained in various
 * fields found in the IDENTIFY DEVICE data structure.
 */
/*@{*/
#define ATA_IDENTIFY_SERIAL_NUMBER_LEN        20
#define ATA_IDENTIFY_MODEL_NUMBER_LEN         40
#define ATA_IDENTIFY_FW_REVISION_LEN          8
#define ATA_IDENTIFY_48_LBA_LEN               8
#define ATA_IDENTIFY_MEDIA_SERIAL_NUMBER_LEN  30
#define ATA_IDENTIFY_WWN_LEN                  8
/*@}*/

/**
 * @name ATA_IDENTIFY_DEVICE_FIELD_MASKS
 *
 * The following constants define bit masks utilized to determine if a
 * feature is supported/enabled or if a bit is simply set inside of the
 * IDENTIFY DEVICE data structre.
 */
/*@{*/
#define ATA_IDENTIFY_REMOVABLE_MEDIA_ENABLE              0x0080
#define ATA_IDENTIFY_CAPABILITIES1_NORMAL_DMA_ENABLE     0x0100
#define ATA_IDENTIFY_CAPABILITIES1_STANDBY_ENABLE        0x2000
#define ATA_IDENTIFY_COMMAND_SET_SUPPORTED0_SMART_ENABLE 0x0001
#define ATA_IDENTIFY_COMMAND_SET_SUPPORTED1_48BIT_ENABLE 0x0400
#define ATA_IDENTIFY_COMMAND_SET_WWN_SUPPORT_ENABLE      0x0100
#define ATA_IDENTIFY_COMMAND_SET_ENABLED0_SMART_ENABLE   0x0001
#define ATA_IDENTIFY_SATA_CAPABILITIES_NCQ_ENABLE        0x0100
#define ATA_IDENTIFY_NCQ_QUEUE_DEPTH_ENABLE              0x001F
#define ATA_IDENTIFY_SECTOR_LARGER_THEN_512_ENABLE       0x0100
#define ATA_IDENTIFY_LOGICAL_SECTOR_PER_PHYSICAL_SECTOR_MASK   0x000F
#define ATA_IDENTIFY_LOGICAL_SECTOR_PER_PHYSICAL_SECTOR_ENABLE 0x2000
#define ATA_IDENTIFY_WRITE_UNCORRECTABLE_SUPPORT         0x0004
#define ATA_IDENTIFY_COMMAND_SET_SMART_SELF_TEST_SUPPORTED     0x0002
#define ATA_IDENTIFY_COMMAND_SET_DSM_TRIM_SUPPORTED            0x0001
#define ATA_IDENTIFY_COMMAND_ADDL_SUPPORTED_DETERMINISTIC_READ 0x4000
#define ATA_IDENTIFY_COMMAND_ADDL_SUPPORTED_READ_ZERO          0x0020
/*@}*/

/**
 * @name ATAPI_IDENTIFY_DEVICE_FIELD_MASKS
 *
 * These constants define the various bit definitions for the
 * fields in the PACKET IDENTIFY DEVICE data structure.
 */
/*@{*/
#define ATAPI_IDENTIFY_16BYTE_CMD_PCKT_ENABLE       0x01
/*@}*/

/**
 * @name ATA_PACKET_FEATURE_BITS
 *
 * These constants define the various bit definitions for the
 * ATA PACKET feature register.
 */
/*@{*/
#define ATA_PACKET_FEATURE_DMA     0x01
#define ATA_PACKET_FEATURE_OVL     0x02
#define ATA_PACKET_FEATURE_DMADIR  0x04
/*@}*/

/**
 * @name ATA_Device_Power_Mode_Values
 *
 * These constants define the power mode values returned by
 * ATA_Check_Power_Mode
 */
/*@{*/
#define ATA_STANDBY_POWER_MODE    0x00
#define ATA_IDLE_POWER_MODE       0x80
#define ATA_ACTIVE_POWER_MODE     0xFF
/*@}*/

/**
 * @name ATA_WRITE_UNCORRECTABLE feature field values
 *
 * These constants define the Write Uncorrectable feature values
 * used with the SATI translation.
 */
/*@{*/
#define ATA_WRITE_UNCORRECTABLE_PSEUDO    0x55
#define ATA_WRITE_UNCORRECTABLE_FLAGGED   0xAA
/*@}*/



/**
 * @name ATA_SECURITY_STATUS field values
 *
 * These constants define the mask of the securityStatus field and the various bits within it
 */
/*@{*/
#define ATA_SECURITY_STATUS_SUPPORTED      0x0001
#define ATA_SECURITY_STATUS_ENABLED        0x0002
#define ATA_SECURITY_STATUS_LOCKED         0x0004
#define ATA_SECURITY_STATUS_FROZEN         0x0008
#define ATA_SECURITY_STATUS_EXPIRED        0x0010
#define ATA_SECURITY_STATUS_ERASESUPPORTED 0x0020
#define ATA_SECURITY_STATUS_RESERVED       0xFEC0
#define ATA_SECURITY_STATUS_SECURITYLEVEL  0x0100
/*@}*/

/**
 * @struct ATA_IDENTIFY_DEVICE
 *
 * @brief This structure depicts the ATA IDENTIFY DEVICE data format.
 */
typedef struct ATA_IDENTIFY_DEVICE_DATA
{
   U16   general_config_bits;                             // word  00
   U16   obsolete0;                                       // word  01 (num cylinders)
   U16   vendor_specific_config_bits;                     // word  02
   U16   obsolete1;                                       // word  03 (num heads)
   U16   retired1[2];                                     // words 04-05
   U16   obsolete2;                                       // word  06 (sectors / track)
   U16   reserved_for_compact_flash1[2];                  // words 07-08
   U16   retired0;                                        // word  09
   U8    serial_number[ATA_IDENTIFY_SERIAL_NUMBER_LEN];   // word 10-19
   U16   retired2[2];                                     // words 20-21
   U16   obsolete4;                                       // word  22
   U8    firmware_revision[ATA_IDENTIFY_FW_REVISION_LEN]; // words 23-26
   U8    model_number[ATA_IDENTIFY_MODEL_NUMBER_LEN];     // words 27-46
   U16   max_sectors_per_multiple;                        // word  47
   U16   reserved0;                                       // word  48
   U16   capabilities1;                                   // word  49
   U16   capabilities2;                                   // word  50
   U16   obsolete5[2];                                    // words 51-52
   U16   validity_bits;                                   // word  53
   U16   obsolete6[5];                                    // words 54-58 Used to be:
                                                          // current cylinders,
                                                          // current heads,
                                                          // current sectors/Track,
                                                          // current capacity
   U16   current_max_sectors_per_multiple;                // word  59
   U8    total_num_sectors[4];                            // words 60-61
   U16   obsolete7;                                       // word  62
   U16   multi_word_dma_mode;                             // word  63
   U16   pio_modes_supported;                             // word  64
   U16   min_multiword_dma_transfer_cycle;                // word  65
   U16   rec_min_multiword_dma_transfer_cycle;            // word  66
   U16   min_pio_transfer_no_flow_ctrl;                   // word  67
   U16   min_pio_transfer_with_flow_ctrl;                 // word  68
   U16   additional_supported;                            // word  69
   U16   reserved1;                                       // word  70
   U16   reserved2[4];                                    // words 71-74
   U16   queue_depth;                                     // word  75
   U16   serial_ata_capabilities;                         // word  76
   U16   serial_ata_reserved;                             // word  77
   U16   serial_ata_features_supported;                   // word  78
   U16   serial_ata_features_enabled;                     // word  79
   U16   major_version_number;                            // word  80
   U16   minor_version_number;                            // word  81
   U16   command_set_supported0;                          // word  82
   U16   command_set_supported1;                          // word  83
   U16   command_set_supported_extention;                 // word  84
   U16   command_set_enabled0;                            // word  85
   U16   command_set_enabled1;                            // word  86
   U16   command_set_default;                             // word  87
   U16   ultra_dma_mode;                                  // word  88
   U16   security_erase_completion_time;                  // word  89
   U16   enhanced_security_erase_time;                    // word  90
   U16   current_power_mgmt_value;                        // word  91
   U16   master_password_revision;                        // word  92
   U16   hardware_reset_result;                           // word  93
   U16   current_acoustic_management_value;               // word  94
   U16   stream_min_request_size;                         // word  95
   U16   stream_transfer_time;                            // word  96
   U16   stream_access_latency;                           // word  97
   U16   stream_performance_granularity[2];               // words 98-99
   U8    max_48bit_lba[ATA_IDENTIFY_48_LBA_LEN];          // words 100-103
   U16   streaming_transfer_time;                         // word  104
   U16   max_lba_range_entry_blocks;                      // word  105
   U16   physical_logical_sector_info;                    // word  106
   U16   acoustic_test_interseek_delay;                   // word  107
   U8    world_wide_name[ATA_IDENTIFY_WWN_LEN];           // words 108-111
   U8    reserved_for_wwn_extention[ATA_IDENTIFY_WWN_LEN];// words 112-115
   U16   reserved4;                                       // word  116
   U8    words_per_logical_sector[4];                     // words 117-118
   U16   command_set_supported2;                          // word  119
   U16   reserved5[7];                                    // words 120-126
   U16   removable_media_status;                          // word  127
   U16   security_status;                                 // word  128
   U16   vendor_specific1[31];                            // words 129-159
   U16   cfa_power_mode1;                                 // word  160
   U16   reserved_for_compact_flash2[7];                  // words 161-167
   U16   device_nominal_form_factor;                      // word  168
   U16   data_set_management;                             // word  169
   U16   reserved_for_compact_flash3[6];                  // words 170-175
   U16   current_media_serial_number[ATA_IDENTIFY_MEDIA_SERIAL_NUMBER_LEN];//words 176-205
   U16   reserved6[3];                                    // words 206-208
   U16   logical_sector_alignment;                        // words 209
   U16   reserved7[7];                                    // words 210-216
   U16   nominal_media_rotation_rate;                     // word  217
   U16   reserved8[16];                                   // words 218-233
   U16   min_num_blocks_per_microcode;                    // word  234
   U16   max_num_blocks_per_microcode;                    // word  235
   U16   reserved9[19];                                   // words 236-254
   U16   integrity_word;                                  // word  255

} ATA_IDENTIFY_DEVICE_DATA_T;

#define ATA_IDENTIFY_DEVICE_GET_OFFSET(field_name) \
   ((POINTER_UINT)&(((ATA_IDENTIFY_DEVICE_DATA_T*)0)->field_name))
#define ATA_IDENTIFY_DEVICE_WCE_ENABLE  0x20
#define ATA_IDENTIFY_DEVICE_RA_ENABLE   0x40

/**
 * @struct ATAPI_IDENTIFY_PACKET_DATA
 *
 * @brief The following structure depicts the ATA-ATAPI 7 version of the
 *        IDENTIFY PACKET DEVICE data structure.
 */
typedef struct ATAPI_IDENTIFY_PACKET_DEVICE
{
   U16   generalConfigBits;                      // word  00
   U16   reserved0;                              // word  01 (num cylinders)
   U16   uniqueConfigBits;                       // word  02
   U16   reserved1[7];                           // words 03 - 09
   U8    serialNumber[ATA_IDENTIFY_SERIAL_NUMBER_LEN];  // word 10-19
   U16   reserved2[3];                           // words 20-22
   U8    firmwareRevision[ATA_IDENTIFY_FW_REVISION_LEN];// words 23-26
   U8    modelNumber[ATA_IDENTIFY_MODEL_NUMBER_LEN];    // words 27-46
   U16   reserved4[2];                           // words 47-48
   U16   capabilities1;                          // word  49
   U16   capabilities2;                          // word  50
   U16   obsolete0[2];                           // words 51-52
   U16   validityBits;                           // word  53
   U16   reserved[8];                            // words 54-61

   U16   DMADIRBitRequired;                      // word  62, page2
   U16   multiWordDmaMode;                       // word  63
   U16   pioModesSupported;                      // word  64
   U16   minMultiwordDmaTransferCycle;           // word  65
   U16   recMinMultiwordDmaTransferCycle;        // word  66
   U16   minPioTransferNoFlowCtrl;               // word  67
   U16   minPioTransferWithFlowCtrl;             // word  68
   U16   reserved6[2];                           // words 69-70
   U16   nsFromPACKETReceiptToBusRelease;        // word  71
   U16   nsFromSERVICEReceiptToBSYreset;         // wore  72
   U16   reserved7[2];                           // words 73-74
   U16   queueDepth;                             // word  75
   U16   serialAtaCapabilities;                  // word  76
   U16   serialAtaReserved;                      // word  77
   U16   serialAtaFeaturesSupported;             // word  78
   U16   serialAtaFeaturesEnabled;               // word  79

   U16   majorVersionNumber;                     // word  80, page3
   U16   minorVersionNumber;                     // word  81
   U16   commandSetSupported0;                   // word  82
   U16   commandSetSupported1;                   // word  83

   U16   commandSetSupportedExtention;           // word  84, page4
   U16   commandSetEnabled0;                     // word  85
   U16   commandSetEnabled1;                     // word  86
   U16   commandSetDefault;                      // word  87

   U16   ultraDmaMode;                           // word  88, page5
   U16   reserved8[4];                           // words 89 - 92

   U16   hardwareResetResult;                    // word  93, page6
   U16   currentAcousticManagementValue;         // word  94
   U16   reserved9[30];                          // words 95-124
   U16   ATAPIByteCount0Behavior;                // word  125
   U16   obsolete1;                              // word  126
   U16   removableMediaStatus;                   // word  127,

   U16   securityStatus;                         // word  128, page7
   U16   vendorSpecific1[31];                    // words 129-159
   U16   reservedForCompactFlash[16];            // words 160-175
   U16   reserved10[79];                         // words 176-254
   U16   integrityWord;                          // word  255
} ATAPI_IDENTIFY_PACKET_DEVICE_T;

/**
* @struct ATA_EXTENDED_SMART_SELF_TEST_LOG
*
* @brief The following structure depicts the ATA-8 version of the
*        Extended SMART self test log page descriptor entry.
*/
typedef union ATA_DESCRIPTOR_ENTRY
{
      struct DESCRIPTOR_ENTRY
      {
         U8 lba_field;
         U8 status_byte;
         U8 time_stamp_low;
         U8 time_stamp_high;
         U8 checkpoint_byte;
         U8 failing_lba_low;
         U8 failing_lba_mid;
         U8 failing_lba_high;
         U8 failing_lba_low_ext;
         U8 failing_lba_mid_ext;
         U8 failing_lba_high_ext;

         U8 vendor_specific1;
         U8 vendor_specific2;
         U8 vendor_specific3;
         U8 vendor_specific4;
         U8 vendor_specific5;
         U8 vendor_specific6;
         U8 vendor_specific7;
         U8 vendor_specific8;
         U8 vendor_specific9;
         U8 vendor_specific10;
         U8 vendor_specific11;
         U8 vendor_specific12;
         U8 vendor_specific13;
         U8 vendor_specific14;
         U8 vendor_specific15;
      } DESCRIPTOR_ENTRY;

      U8 descriptor_entry[26];

} ATA_DESCRIPTOR_ENTRY_T;

/**
* @struct ATA_EXTENDED_SMART_SELF_TEST_LOG
*
* @brief The following structure depicts the ATA-8 version of the
*        SMART self test log page descriptor entry.
*/
typedef union ATA_SMART_DESCRIPTOR_ENTRY
{
      struct SMART_DESCRIPTOR_ENTRY
      {
         U8 lba_field;
         U8 status_byte;
         U8 time_stamp_low;
         U8 time_stamp_high;
         U8 checkpoint_byte;
         U8 failing_lba_low;
         U8 failing_lba_mid;
         U8 failing_lba_high;
         U8 failing_lba_low_ext;

         U8 vendor_specific1;
         U8 vendor_specific2;
         U8 vendor_specific3;
         U8 vendor_specific4;
         U8 vendor_specific5;
         U8 vendor_specific6;
         U8 vendor_specific7;
         U8 vendor_specific8;
         U8 vendor_specific9;
         U8 vendor_specific10;
         U8 vendor_specific11;
         U8 vendor_specific12;
         U8 vendor_specific13;
         U8 vendor_specific14;
         U8 vendor_specific15;
      } SMART_DESCRIPTOR_ENTRY;

      U8 smart_descriptor_entry[24];

} ATA_SMART_DESCRIPTOR_ENTRY_T;

/**
* @struct ATA_EXTENDED_SMART_SELF_TEST_LOG
*
* @brief The following structure depicts the ATA-8 version of the
*        Extended SMART self test log page.
*/
typedef struct ATA_EXTENDED_SMART_SELF_TEST_LOG
{
   U8    self_test_log_data_structure_revision_number;   //byte 0
   U8    reserved0;                                      //byte 1
   U8    self_test_descriptor_index[2];                  //byte 2-3

   ATA_DESCRIPTOR_ENTRY_T descriptor_entrys[19];         //bytes 4-497

   U8    vendor_specific[2];                             //byte 498-499
   U8    reserved1[11];                                  //byte 500-510
   U8    data_structure_checksum;                        //byte 511

} ATA_EXTENDED_SMART_SELF_TEST_LOG_T;

/**
* @struct ATA_EXTENDED_SMART_SELF_TEST_LOG
*
* @brief The following structure depicts the ATA-8 version of the
*        SMART self test log page.
*/
typedef struct ATA_SMART_SELF_TEST_LOG
{
   U8    self_test_log_data_structure_revision_number[2];   //bytes 0-1

   ATA_SMART_DESCRIPTOR_ENTRY_T descriptor_entrys[21];      //bytes 2-505

   U8    vendor_specific[2];                                //byte 506-507
   U8    self_test_index;                                   //byte 508
   U8    reserved1[2];                                      //byte 509-510
   U8    data_structure_checksum;                           //byte 511

} ATA_SMART_SELF_TEST_LOG_T;

/**
* @struct ATA_NCQ_COMMAND_ERROR_LOG
*
* @brief The following structure depicts the ATA-8 version of the
*        NCQ command error log page.
*/
typedef struct ATA_NCQ_COMMAND_ERROR_LOG
{
   U8    ncq_tag   : 5;
   U8    reserved1 : 2;
   U8    nq        : 1;
   U8    reserved2;
   U8    status;
   U8    error;
   U8    lba_7_0;
   U8    lba_15_8;
   U8    lba_23_16;
   U8    device;
   U8    lba_31_24;
   U8    lba_39_32;
   U8    lba_47_40;
   U8    reserved3;
   U8    count_7_0;
   U8    count_15_8;
   U8    reserved4[242];
   U8    vendor_specific[255];
   U8    checksum;
} ATA_NCQ_COMMAND_ERROR_LOG_T;

#endif // _ATA_H_

