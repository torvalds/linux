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
 * @brief This file contains all of the defintions for the SATI remote
 *        device object.  Some translations require information to be
 *        remembered on a per device basis.  This information is stored
 *        in the object defined in this file.
 */

#include <dev/isci/scil/sati_device.h>
#include <dev/isci/scil/sci_util.h>  // Move this file.
#include <dev/isci/scil/sati_unmap.h>
#include <dev/isci/scil/intel_scsi.h>

/**
 * @brief This method simply initializes the data members in the device
 *        object to their appropriate values.
 *
 * @param[in] device This parameter specifies the device for which to
 *            initialize the data members.
 * @param[in] is_ncq_enabled This parameter specifies if NCQ is to be
 *            utilized for this particular SATI device.
 * @param[in] max_ncq_depth This parameter specifies the maximum desired
 *            NCQ depth.  Once this value is set it can never be increased.
 * @param[in] ignore_fua This parameter specifies FUA is to be ignored and not
 *            sent to the end device. Some OS (Windows) has quirky behaviors with FUA
 *            and recommend driver developers ignore the bit.
 *
 * @return none
 */
void sati_device_construct(
   SATI_DEVICE_T * device,
   BOOL            is_ncq_enabled,
   U8              max_ncq_depth,
   BOOL            ignore_fua
)
{
   device->state                   = SATI_DEVICE_STATE_OPERATIONAL;
   device->capabilities            = 0;
   device->descriptor_sense_enable = SCSI_MODE_PAGE_CONTROL_D_SENSE_DISABLE;

   // The user requested that NCQ be utilized if it is supported by
   // the device.
   if (is_ncq_enabled == TRUE)
      device->capabilities |= SATI_DEVICE_CAP_NCQ_REQUESTED_ENABLE;

   device->ncq_depth      = max_ncq_depth;

   // The user requested that FUA is ignored (windows performance issue)
   if (ignore_fua == TRUE)
      device->capabilities |= SATI_DEVICE_CAP_IGNORE_FUA;

}

/**
 * @brief This method will update the SATI_DEVICE capabilities based on
 *        the supplied ATA_IDENTIFY_DEVICE_DATA.
 *
 * @param[in] device This parameter specifies the device for which to update
 *            the supported capabilities.
 * @param[in] identify This parameter specifies the ata identify device
 *            information from which to extract the capabilities of the
 *            device.
 *
 * @return none
 */
void sati_device_update_capabilities(
   SATI_DEVICE_T              * device,
   ATA_IDENTIFY_DEVICE_DATA_T * identify
)
{
   U16 capabilities = 0;

   if (identify->capabilities1 & ATA_IDENTIFY_CAPABILITIES1_NORMAL_DMA_ENABLE)
      capabilities |= SATI_DEVICE_CAP_UDMA_ENABLE;

   if (identify->command_set_supported1
       & ATA_IDENTIFY_COMMAND_SET_SUPPORTED1_48BIT_ENABLE)
   {
      capabilities |= SATI_DEVICE_CAP_48BIT_ENABLE;
   }

   if (identify->command_set_supported0
       & ATA_IDENTIFY_COMMAND_SET_SUPPORTED0_SMART_ENABLE)
   {
      capabilities |= SATI_DEVICE_CAP_SMART_SUPPORT;
   }

   if (identify->command_set_enabled0
       & ATA_IDENTIFY_COMMAND_SET_SUPPORTED0_SMART_ENABLE)
   {
       capabilities |= SATI_DEVICE_CAP_SMART_ENABLE;
   }

   // Save the NCQ related capabilities information.
   if (identify->serial_ata_capabilities
       & ATA_IDENTIFY_SATA_CAPABILITIES_NCQ_ENABLE)
   {
      if (device->capabilities & SATI_DEVICE_CAP_NCQ_REQUESTED_ENABLE)
      {
         capabilities      |= SATI_DEVICE_CAP_NCQ_REQUESTED_ENABLE;
         capabilities      |= SATI_DEVICE_CAP_NCQ_SUPPORTED_ENABLE;
         capabilities      |= SATI_DEVICE_CAP_DMA_FUA_ENABLE;
         device->ncq_depth  = MIN(
                                 device->ncq_depth,
                                 (U8) (identify->queue_depth
                                 & ATA_IDENTIFY_NCQ_QUEUE_DEPTH_ENABLE) + 1
                              );
      }
   }

   // if the user requested that FUA is ignored; transfer it so we don't lose on update.
   if (device->capabilities & SATI_DEVICE_CAP_IGNORE_FUA)
	   capabilities |= SATI_DEVICE_CAP_IGNORE_FUA;

   if (identify->general_config_bits & ATA_IDENTIFY_REMOVABLE_MEDIA_ENABLE)
      capabilities |= SATI_DEVICE_CAP_REMOVABLE_MEDIA;

   if(identify->command_set_supported2 & ATA_IDENTIFY_WRITE_UNCORRECTABLE_SUPPORT )
   {
      capabilities |= SATI_DEVICE_CAP_WRITE_UNCORRECTABLE_ENABLE;
   }

   if(identify->physical_logical_sector_info &
      ATA_IDENTIFY_LOGICAL_SECTOR_PER_PHYSICAL_SECTOR_ENABLE)
   {
      capabilities |= SATI_DEVICE_CAP_MULTIPLE_SECTORS_PER_PHYSCIAL_SECTOR;
   }

   if(identify->command_set_supported_extention &
      ATA_IDENTIFY_COMMAND_SET_SMART_SELF_TEST_SUPPORTED)
   {
      capabilities |= SATI_DEVICE_CAP_SMART_SELF_TEST_SUPPORT;
   }

   if (identify->nominal_media_rotation_rate == 1)
   {
       capabilities |= SATI_DEVICE_CAP_SSD;
   }

   // Save off the logical block size reported by the drive
   // See if Word 106 is valid and reports a logical sector size
   if ((identify->physical_logical_sector_info & 0x5000) == 0x5000)
   {
       device->logical_block_size = (identify->words_per_logical_sector[3] << 24) |
                                    (identify->words_per_logical_sector[2] << 16) |
                                    (identify->words_per_logical_sector[1] << 8) |
                                    (identify->words_per_logical_sector[0]);
   }
   else
   {
       device->logical_block_size = 512;
   }

   // Determine DSM TRIM capabilities
   // Defend against SSDs which report TRIM support, but set
   //  max_lba_range_entry_blocks to zero, by disabling TRIM for
   //  those SSDs.
   if (
     (identify->data_set_management & ATA_IDENTIFY_COMMAND_SET_DSM_TRIM_SUPPORTED)
     && (identify->max_lba_range_entry_blocks > 0)
      )
   {
      capabilities |= SATI_DEVICE_CAP_DSM_TRIM_SUPPORT;
      device->max_lba_range_entry_blocks = identify->max_lba_range_entry_blocks;
   }

   if (identify->additional_supported
       & ATA_IDENTIFY_COMMAND_ADDL_SUPPORTED_DETERMINISTIC_READ)
   {
      capabilities |= SATI_DEVICE_CAP_DETERMINISTIC_READ_AFTER_TRIM;
   }

   if (identify->additional_supported
       & ATA_IDENTIFY_COMMAND_ADDL_SUPPORTED_READ_ZERO)
   {
      capabilities |= SATI_DEVICE_CAP_READ_ZERO_AFTER_TRIM;
   }

   if (identify->capabilities1
       & ATA_IDENTIFY_CAPABILITIES1_STANDBY_ENABLE)
   {
       capabilities |= SATI_DEVICE_CAP_STANDBY_ENABLE;
   }

   device->min_blocks_per_microcode_command = identify->min_num_blocks_per_microcode;
   device->max_blocks_per_microcode_command = identify->max_num_blocks_per_microcode;

   device->capabilities = capabilities;
}

