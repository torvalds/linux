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
 * @brief This file contains the method implementations required to
 *        translate the SCSI mode sense (6 and 10-byte) commands.
 */

#if !defined(DISABLE_SATI_MODE_SENSE)

#include <dev/isci/scil/sati_mode_sense.h>
#include <dev/isci/scil/sati_mode_pages.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/sati_util.h>
#include <dev/isci/scil/intel_scsi.h>
#include <dev/isci/scil/intel_ata.h>

//******************************************************************************
//* P R I V A T E   M E T H O D S
//******************************************************************************

#define STANDBY_TIMER_DISABLED  0x00
#define STANDBY_TIMER_ENABLED   0x01
#define STANDBY_TIMER_SUPPORTED 0x2000



/**
 * @brief This method indicates if the supplied page control is supported
 *        by this translation implementation.  Currently savable parameters
 *        (i.e. non-volatile) are not supported.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return This method returns an indication of whether the page control
 *         specified in the SCSI CDB is supported.
 * @retval SATI_SUCCESS This value is returned if the page control is
 *         supported.
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if the
 *         page control is not supported.
 */
static
SATI_STATUS sati_mode_sense_is_page_control_supported(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io
)
{
   U8 * cdb = sati_cb_get_cdb_address(scsi_io);

   switch (sati_get_cdb_byte(cdb, 2) >> SCSI_MODE_SENSE_PC_SHIFT)
   {
      case SCSI_MODE_SENSE_PC_CURRENT:
      case SCSI_MODE_SENSE_PC_DEFAULT:
      case SCSI_MODE_SENSE_PC_CHANGEABLE:
         return SATI_SUCCESS;
      break;

      default:
      case SCSI_MODE_SENSE_PC_SAVED:
         sati_scsi_sense_data_construct(
            sequence,
            scsi_io,
            SCSI_STATUS_CHECK_CONDITION,
            SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ASC_SAVING_PARMS_NOT_SUPPORTED,
            SCSI_ASCQ_SAVING_PARMS_NOT_SUPPORTED
         );
         return SATI_FAILURE_CHECK_RESPONSE_DATA;
      break;
   }
}

/**
 * @brief This method indicates if the page code field in the SCSI CDB
 *        is supported by this translation.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @param[in] cdb_length This parameter specifies the length of the SCSI
 *            CDB being translated (e.g. 6-byte, 10-byte, 12-byte, etc.)
 *
 * @return This method returns an indication as to whether the page code
 *         in the CDB is supported.
 * @retval SATI_SUCCESS This value is returned if the page code is
 *         supported.
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if the
 *         page code is not supported.
 */
static
SATI_STATUS sati_mode_sense_is_page_code_supported(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U8                           cdb_length
)
{
   U8 * cdb = sati_cb_get_cdb_address(scsi_io);

   switch (sati_get_cdb_byte(cdb, 2) & SCSI_MODE_SENSE_PAGE_CODE_ENABLE)
   {
      case SCSI_MODE_PAGE_CACHING:
         if (sati_get_cdb_byte(cdb, 0) == SCSI_MODE_SENSE_6)
            sequence->type = SATI_SEQUENCE_MODE_SENSE_6_CACHING;
         else
            sequence->type = SATI_SEQUENCE_MODE_SENSE_10_CACHING;
      break;

      case SCSI_MODE_PAGE_ALL_PAGES:
         if (sati_get_cdb_byte(cdb, 0) == SCSI_MODE_SENSE_6)
            sequence->type = SATI_SEQUENCE_MODE_SENSE_6_ALL_PAGES;
         else
            sequence->type = SATI_SEQUENCE_MODE_SENSE_10_ALL_PAGES;
      break;

      case SCSI_MODE_PAGE_READ_WRITE_ERROR:
         if (sati_get_cdb_byte(cdb, 0) == SCSI_MODE_SENSE_6)
            sequence->type = SATI_SEQUENCE_MODE_SENSE_6_READ_WRITE_ERROR;
         else
            sequence->type = SATI_SEQUENCE_MODE_SENSE_10_READ_WRITE_ERROR;
      break;

      case SCSI_MODE_PAGE_DISCONNECT_RECONNECT:
         if (sati_get_cdb_byte(cdb, 0) == SCSI_MODE_SENSE_6)
            sequence->type = SATI_SEQUENCE_MODE_SENSE_6_DISCONNECT_RECONNECT;
         else
            sequence->type = SATI_SEQUENCE_MODE_SENSE_10_DISCONNECT_RECONNECT;
      break;

      case SCSI_MODE_PAGE_CONTROL:
         if (sati_get_cdb_byte(cdb, 0) == SCSI_MODE_SENSE_6)
            sequence->type = SATI_SEQUENCE_MODE_SENSE_6_CONTROL;
         else
            sequence->type = SATI_SEQUENCE_MODE_SENSE_10_CONTROL;
      break;

      case SCSI_MODE_PAGE_POWER_CONDITION:
         if (sati_get_cdb_byte(cdb, 0) == SCSI_MODE_SENSE_6)
            sequence->type = SATI_SEQUENCE_MODE_SENSE_6_POWER_CONDITION;
         else
            sequence->type = SATI_SEQUENCE_MODE_SENSE_10_POWER_CONDITION;
      break;

      case SCSI_MODE_PAGE_INFORMATIONAL_EXCP_CONTROL:
         // The informational exceptions control page is only useful
         // if SMART is supported.
         if ((sequence->device->capabilities | SATI_DEVICE_CAP_SMART_SUPPORT)
             == 0)
         {
            // For a MODE SENSE, utilize INVALID FIELD IN CDB,
            // For a MODE SELECT, utilize INVALID FIELD IN PARAMETER LIST.
            if (sati_get_cdb_byte(cdb, 0) == SCSI_MODE_SENSE_6)
            {
               sati_scsi_sense_data_construct(
                  sequence,
                  scsi_io,
                  SCSI_STATUS_CHECK_CONDITION,
                  SCSI_SENSE_ILLEGAL_REQUEST,
                  SCSI_ASC_INVALID_FIELD_IN_CDB,
                  SCSI_ASCQ_INVALID_FIELD_IN_CDB
               );
            }
            else
            {
               sati_scsi_sense_data_construct(
                  sequence,
                  scsi_io,
                  SCSI_STATUS_CHECK_CONDITION,
                  SCSI_SENSE_ILLEGAL_REQUEST,
                  SCSI_ASC_INVALID_FIELD_IN_PARM_LIST,
                  SCSI_ASCQ_INVALID_FIELD_IN_PARM_LIST
               );
            }

            return SATI_FAILURE_CHECK_RESPONSE_DATA;
         }

         if (sati_get_cdb_byte(cdb, 0) == SCSI_MODE_SENSE_6)
            sequence->type = SATI_SEQUENCE_MODE_SENSE_6_INFORMATIONAL_EXCP_CONTROL;
         else
            sequence->type = SATI_SEQUENCE_MODE_SENSE_10_INFORMATIONAL_EXCP_CONTROL;
      break;

      default:
         sati_scsi_sense_data_construct(
            sequence,
            scsi_io,
            SCSI_STATUS_CHECK_CONDITION,
            SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ASC_INVALID_FIELD_IN_CDB,
            SCSI_ASCQ_INVALID_FIELD_IN_CDB
         );
         return SATI_FAILURE_CHECK_RESPONSE_DATA;
      break;
   }

   return SATI_SUCCESS;
}

//******************************************************************************
//* P R O T E C T E D   M E T H O D S
//******************************************************************************

/**
 * @brief This method will calculate the size of the mode sense data header.
 *        This includes the block descriptor if one is requested.
 *
 * @param[in] scsi_io This parameter specifies the user's SCSI IO object
 *            for which to calculate the mode page header.
 * @param[in] cdb_size This parameter specifies the number of bytes
 *            associated with the CDB for which to calculate the header.
 *
 * @return This method returns the size, in bytes, for the mode page header.
 */
U16 sati_mode_sense_calculate_page_header(
   void * scsi_io,
   U8     cdb_size
)
{
   U8 * cdb         = sati_cb_get_cdb_address(scsi_io);
   U16  page_length = 0;

   // The Mode page header length is different for 6-byte vs. 10-byte CDBs.
   if (cdb_size == 6)
      page_length += SCSI_MODE_SENSE_6_HEADER_LENGTH;
   else
      page_length += SCSI_MODE_SENSE_10_HEADER_LENGTH;

   // Are block descriptors disabled (DBD)?  0 indicates they are enabled.
   if ((sati_get_cdb_byte(cdb, 1) & SCSI_MODE_SENSE_DBD_ENABLE) == 0)
   {
      // The LLBAA bit is not defined for 6-byte mode sense requests.
      if (  (cdb_size == 10)
         && (sati_get_cdb_byte(cdb, 1) & SCSI_MODE_SENSE_LLBAA_ENABLE) )
         page_length += SCSI_MODE_SENSE_LLBA_BLOCK_DESCRIPTOR_LENGTH;
      else
         page_length += SCSI_MODE_SENSE_STD_BLOCK_DESCRIPTOR_LENGTH;
   }

   return page_length;
}

/**
 * @brief This method performs command translation common to all mode sense
 *        requests (6 or 10 byte).
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @param[in] cdb_length This parameter specifies the number of bytes
 *            in the CDB (6 or 10).
 *
 * @return This method returns an indication as to whether the translation
 *         succeeded.
 * @retval SCI_SUCCESS This value is returned if translation succeeded.
 * @see sati_mode_sense_is_page_control_supported() or
 *      sati_mode_sense_is_page_code_supported() for more information.
 */
SATI_STATUS sati_mode_sense_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io,
   U8                           cdb_length
)
{
   SATI_STATUS   status;

   /**
    * Validate that the supplied page control (PC) field is supported.
    */
   status = sati_mode_sense_is_page_control_supported(sequence, scsi_io);
   if (status != SATI_SUCCESS)
      return status;

   /**
    * Validate that the supplied page code is supported.
    */
   status = sati_mode_sense_is_page_code_supported(sequence,scsi_io,cdb_length);
   if (status != SATI_SUCCESS)
      return status;

   sati_ata_identify_device_construct(ata_io, sequence);

   return SATI_SUCCESS;
}

/**
 * @brief This method will build the standard block descriptor for a MODE
 *        SENSE 6 or 10 byte request.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @param[in] identify This parameter specifies the IDENTIFY DEVICE data
 *            associated with the SCSI IO.
 * @param[in] offset This parameter specifies the offset into the data
 *            buffer at which to build the block descriptor.
 *
 * @return This method returns the size of the block descriptor built.
 */
U32 sati_mode_sense_build_std_block_descriptor(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                          offset
)
{
   U32  lba_low     = 0;
   U32  lba_high    = 0;
   U32  sector_size = 0;

   // Extract the sector information (sector size, logical blocks) from
   // the retrieved ATA identify device data.
   sati_ata_identify_device_get_sector_info(
      identify, &lba_high, &lba_low, &sector_size
   );

   // Fill in the 4-byte logical block address field.
   sati_set_data_byte(sequence, scsi_io, offset,   (U8)((lba_low>>24) & 0xFF));
   sati_set_data_byte(sequence, scsi_io, offset+1, (U8)((lba_low>>16) & 0xFF));
   sati_set_data_byte(sequence, scsi_io, offset+2, (U8)((lba_low>>8)  & 0xFF));
   sati_set_data_byte(sequence, scsi_io, offset+3, (U8)(lba_low & 0xFF));

   // Clear the reserved field.
   sati_set_data_byte(sequence, scsi_io, offset+4, 0);

   // Fill in the three byte Block Length field
   sati_set_data_byte(sequence,scsi_io, offset+5, (U8)((sector_size>>16) & 0xFF));
   sati_set_data_byte(sequence,scsi_io, offset+6, (U8)((sector_size>>8)  & 0xFF));
   sati_set_data_byte(sequence,scsi_io, offset+7, (U8)(sector_size & 0xFF));

   return SCSI_MODE_SENSE_STD_BLOCK_DESCRIPTOR_LENGTH;
}

/**
 * @brief This method simply copies the mode sense data into the buffer
 *        at the location specified by page_start.  The buffer copied is
 *        determined by page_control (e.g. current, default, or changeable
 *        values).
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @param[in] page_start This parameter specifies the starting offset at
 *            which to copy the mode page data.
 * @param[in] page_control This parameter specifies the page control
 *            indicating the source buffer to be copied.
 * @param[in] page_code This specifies the mode sense page to copy.
 *
 * @return This method returns the size of the mode page data being copied.
 */
U32 sati_mode_sense_copy_initial_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U32                          page_start,
   U8                           page_control,
   U8                           page_code
)
{
   U16 page_index  = sati_mode_page_get_page_index(page_code);
   U32 page_length = sat_mode_page_sizes[page_index];

   // Find out if the current values are requested or if the default
   // values are being requested.
   if (page_control == SCSI_MODE_SENSE_PC_CHANGEABLE)
   {
      // Copy the changeable mode page information.
      sati_copy_data(
         sequence,
         scsi_io,
         page_start,
         sat_changeable_mode_pages[page_index],
         page_length
      );
   }
   else
   {
      // Copy the default static values template to the user data area.
      sati_copy_data(
         sequence,
         scsi_io,
         page_start,
         sat_default_mode_pages[page_index],
         page_length
      );
   }

   return page_length;
}

/**
 * @brief This method performs the read/write error recovery mode page
 *        specific data translation based upon the contents of the remote
 *        device IDENTIFY DEVICE data.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @param[in] identify This parameter specifies the remote device's
 *            IDENTIFY DEVICE data received as part of the IO request.
 * @param[in] offset This parameter specifies the offset into the data
 *            buffer where the translated data is to be written.
 *
 * @return This method returns the size of the mode page data that was
 *         translated.
 */
U32 sati_mode_sense_read_write_error_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                          offset
)
{
   U8 * cdb          = sati_cb_get_cdb_address(scsi_io);
   U8   page_control = sati_get_cdb_byte(cdb, 2) >> SCSI_MODE_SENSE_PC_SHIFT;
   U32  page_length;

   page_length = sati_mode_sense_copy_initial_data(
                    sequence,
                    scsi_io,
                    offset,
                    page_control,
                    SCSI_MODE_PAGE_READ_WRITE_ERROR
                 );

   // Currently we do not override any bits in this mode page from the
   // identify data.

   return page_length;
}

/**
 * @brief This method performs the disconnect/reconnect mode page
 *        specific data translation based upon the contents of the remote
 *        device IDENTIFY DEVICE data.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @param[in] identify This parameter specifies the remote device's
 *            IDENTIFY DEVICE data received as part of the IO request.
 * @param[in] offset This parameter specifies the offset into the data
 *            buffer where the translated data is to be written.
 *
 * @return This method returns the size of the mode page data that was
 *         translated.
 */
U32 sati_mode_sense_disconnect_reconnect_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                          offset
)
{
   U8 * cdb          = sati_cb_get_cdb_address(scsi_io);
   U8   page_control = sati_get_cdb_byte(cdb, 2) >> SCSI_MODE_SENSE_PC_SHIFT;
   U32  page_length;

   page_length = sati_mode_sense_copy_initial_data(
                    sequence,
                    scsi_io,
                    offset,
                    page_control,
                    SCSI_MODE_PAGE_DISCONNECT_RECONNECT
                 );

   // Currently we do not override any bits in this mode page from the
   // identify data.

   return page_length;
}

/**
 * @brief This method performs the caching mode page specific data
 *        translation based upon the contents of the remote device IDENTIFY
 *        DEVICE data.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @param[in] identify This parameter specifies the remote device's
 *            IDENTIFY DEVICE data received as part of the IO request.
 * @param[in] offset This parameter specifies the offset into the data
 *            buffer where the translated data is to be written.
 *
 * @return This method returns the size of the mode page data that was
 *         translated.
 */
U32 sati_mode_sense_caching_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                          offset
)
{
   U8 * cdb          = sati_cb_get_cdb_address(scsi_io);
   U8   page_control = sati_get_cdb_byte(cdb, 2) >> SCSI_MODE_SENSE_PC_SHIFT;
   U32  page_length;

   page_length = sati_mode_sense_copy_initial_data(
                    sequence,
                    scsi_io,
                    offset,
                    page_control,
                    SCSI_MODE_PAGE_CACHING
                 );

   // If the request queried for the current values, then
   // we need to translate the data from the IDENTIFY DEVICE request.
   if (page_control == SCSI_MODE_SENSE_PC_CURRENT)
   {
      U8  value;

      // Update the Write Cache Enabled (WCE) bit in the mode page data
      // buffer based on the identify response.
      if ((identify->command_set_enabled0 & ATA_IDENTIFY_DEVICE_WCE_ENABLE) != 0)
      {
         sati_get_data_byte(sequence, scsi_io, offset+2, &value);
         value |= SCSI_MODE_PAGE_CACHE_PAGE_WCE_BIT;
         sati_set_data_byte(sequence, scsi_io, offset+2, value);
         //This byte has been set twice and needs to be decremented
         sequence->number_data_bytes_set--;
      }

      // Update the Disable Read Ahead (DRA) bit in the mode page data
      // buffer based on the identify response.
      if ((identify->command_set_enabled0 & ATA_IDENTIFY_DEVICE_RA_ENABLE) == 0)
      {
         // In SATA the polarity of the bits is inverse.
         // - SCSI = Disable Read Ahead
         // - ATA = Read Ahead
         sati_get_data_byte(sequence, scsi_io, offset+12, &value);
         value |= SCSI_MODE_PAGE_CACHE_PAGE_DRA_BIT;
         sati_set_data_byte(sequence, scsi_io, offset+12, value);

         //This byte has been set twice, the first time in
         //sati_mode_sense_copy_initial_data. number_data_bytes_set
         //needs to be decremented
         sequence->number_data_bytes_set--;
      }
   }

   return page_length;
}

/**
 * @brief This method performs the control mode page specific data
 *        translation based upon the contents of the remote device
 *        IDENTIFY DEVICE data.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @param[in] identify This parameter specifies the remote device's
 *            IDENTIFY DEVICE data received as part of the IO request.
 * @param[in] offset This parameter specifies the offset into the data
 *            buffer where the translated data is to be written.
 *
 * @return This method returns the size of the mode page data that was
 *         translated.
 */
U32 sati_mode_sense_control_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                          offset
)
{
   U8 * cdb          = sati_cb_get_cdb_address(scsi_io);
   U8   page_control = sati_get_cdb_byte(cdb, 2) >> SCSI_MODE_SENSE_PC_SHIFT;
   U32  page_length;
   U8   value;

   page_length = sati_mode_sense_copy_initial_data(
                    sequence,
                    scsi_io,
                    offset,
                    page_control,
                    SCSI_MODE_PAGE_CONTROL
                 );

   if (sequence->device->descriptor_sense_enable)
   {
       sati_get_data_byte(sequence, scsi_io, offset+2,
               &value);

       sati_set_data_byte(sequence, scsi_io, offset+2,
               value | SCSI_MODE_SELECT_MODE_PAGE_D_SENSE);
   }

   return page_length;
}

/**
 * @brief This method performs the informational exceptions control mode
 *        page specific data translation based upon the contents of the
 *        remote device IDENTIFY DEVICE data.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @param[in] identify This parameter specifies the remote device's
 *            IDENTIFY DEVICE data received as part of the IO request.
 * @param[in] offset This parameter specifies the offset into the data
 *            buffer where the translated data is to be written.
 *
 * @return This method returns the size of the mode page data that was
 *         translated.
 */
U32 sati_mode_sense_informational_excp_control_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                          offset
)
{
   U8 * cdb          = sati_cb_get_cdb_address(scsi_io);
   U8   page_control = sati_get_cdb_byte(cdb, 2) >> SCSI_MODE_SENSE_PC_SHIFT;
   U32  page_length;

   page_length = sati_mode_sense_copy_initial_data(
                    sequence,
                    scsi_io,
                    offset,
                    page_control,
                    SCSI_MODE_PAGE_INFORMATIONAL_EXCP_CONTROL
                 );

   // If the request queried for the current values, then
   // we need to translate the data from the IDENTIFY DEVICE request.
   if (page_control == SCSI_MODE_SENSE_PC_CURRENT)
   {
      U8 value;

      sati_get_data_byte(sequence, scsi_io, offset+2, &value);

      // Determine if the SMART feature set is supported and enabled.
      if (  (identify->command_set_supported0
                & ATA_IDENTIFY_COMMAND_SET_SUPPORTED0_SMART_ENABLE)
         && (identify->command_set_enabled0
                & ATA_IDENTIFY_COMMAND_SET_ENABLED0_SMART_ENABLE) )
      {
         // Clear the DXCPT field since the SMART feature is supported/enabled.
         value &= ~SCSI_MODE_PAGE_INFORMATIONAL_EXCP_DXCPT_ENABLE;
      }
      else
      {
         // Set the Disable Exception Control (DXCPT) field since the SMART
         // feature is not supported or enabled.
         value |= SCSI_MODE_PAGE_INFORMATIONAL_EXCP_DXCPT_ENABLE;
      }

      sati_set_data_byte(sequence, scsi_io, offset+2, value);

      //This byte has been set twice, the first time in
      //sati_mode_sense_copy_initial_data. number_data_bytes_set
      //needs to be decremented
      sequence->number_data_bytes_set--;
   }

   return page_length;
}

/**
* @brief This method performs the Power Condition mode page
*        specific data translation based upon the contents of the
*        remote device IDENTIFY DEVICE data.
*        For more information on the parameters passed to this method,
*        please reference sati_translate_command().
*
* @param[in] identify This parameter specifies the remote device's
*            IDENTIFY DEVICE data received as part of the IO request.
* @param[in] offset This parameter specifies the offset into the data
*            buffer where the translated data is to be written.
*
* @return This method returns the size of the mode page data that was
*         translated.
*/
U32 sati_mode_sense_power_condition_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                          offset
)
{
   U8 * cdb          = sati_cb_get_cdb_address(scsi_io);
   U8   page_control = sati_get_cdb_byte(cdb, 2) >> SCSI_MODE_SENSE_PC_SHIFT;

   U8 ata_sb_timer;

   //Represents tenths of seconds
   U32 standby_timer = 0x00000000;

   U8 standby_enabled = STANDBY_TIMER_DISABLED;

   if ((page_control == SCSI_MODE_SENSE_PC_CURRENT) &&
       (identify->capabilities1 & STANDBY_TIMER_SUPPORTED))
   {
      standby_enabled = STANDBY_TIMER_ENABLED;

      ata_sb_timer = sequence->device->ata_standby_timer;

      //converting ATA timer values into SCSI timer values
      if(ata_sb_timer <= 0xF0)
      {
         standby_timer = ata_sb_timer * 50;
      }
      else if(ata_sb_timer <= 0xFB)
      {
         standby_timer = ((ata_sb_timer - 240) * 18000);
      }
      else if(ata_sb_timer == 0xFC)
      {
         standby_timer = 12600;
      }
      else if(ata_sb_timer == 0xFD)
      {
         standby_timer = 432000;
      }
      else if(ata_sb_timer == 0xFF)
      {
         standby_timer = 12750;
      }
      else
      {
         standby_timer = 0xFFFFFFFF;
      }
   }

   sati_set_data_byte(sequence, scsi_io, offset, SCSI_MODE_PAGE_POWER_CONDITION);
   sati_set_data_byte(sequence, scsi_io, offset + 1, (SCSI_MODE_PAGE_1A_LENGTH - 2));
   sati_set_data_byte(sequence, scsi_io, offset + 2, 0x00);
   sati_set_data_byte(sequence, scsi_io, offset + 3, standby_enabled);
   sati_set_data_byte(sequence, scsi_io, offset + 4, 0x00);
   sati_set_data_byte(sequence, scsi_io, offset + 5, 0x00);
   sati_set_data_byte(sequence, scsi_io, offset + 6, 0x00);
   sati_set_data_byte(sequence, scsi_io, offset + 7, 0x00);
   sati_set_data_byte(sequence, scsi_io, offset + 8, (U8) (standby_timer >> 24));
   sati_set_data_byte(sequence, scsi_io, offset + 9, (U8) (standby_timer >> 16));
   sati_set_data_byte(sequence, scsi_io, offset + 10, (U8) (standby_timer >> 8));
   sati_set_data_byte(sequence, scsi_io, offset + 11, (U8) standby_timer);

   return SCSI_MODE_PAGE_1A_LENGTH;
}

/**
 * @brief This method performs the all pages mode page specific data
 *        translation based upon the contents of the remote device
 *        IDENTIFY DEVICE data.  The ALL PAGES mode sense request asks
 *        for all of mode pages and sub-pages in a single page.
 *        The mode pages are added in ascending order.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @param[in] identify This parameter specifies the remote device's
 *            IDENTIFY DEVICE data received as part of the IO request.
 * @param[in] offset This parameter specifies the offset into the data
 *            buffer where the translated data is to be written.
 *
 * @return This method returns the size of the mode page data that was
 *         translated.
 */
U32 sati_mode_sense_all_pages_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   ATA_IDENTIFY_DEVICE_DATA_T * identify,
   U32                          offset
)
{
   offset += sati_mode_sense_read_write_error_translate_data(
                sequence, scsi_io, identify, offset
             );

   offset += sati_mode_sense_disconnect_reconnect_translate_data(
                sequence, scsi_io, identify, offset
             );

   offset += sati_mode_sense_caching_translate_data(
                sequence, scsi_io, identify, offset
             );

   offset += sati_mode_sense_control_translate_data(
                sequence, scsi_io, identify, offset
             );

   offset += sati_mode_sense_informational_excp_control_translate_data(
                sequence, scsi_io, identify, offset
             );

   return offset;
}

#endif // !defined(DISABLE_SATI_MODE_SENSE)

