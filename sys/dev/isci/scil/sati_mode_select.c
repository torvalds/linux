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
 *        translate the SCSI mode select (6 and 10-byte) commands with 5
 *        supported mode parameter pages (0x01, 0x02, 0x08, 0x0A, 0x1C).
 */

#if !defined(DISABLE_SATI_MODE_SELECT)

#include <dev/isci/scil/sati_mode_select.h>
#include <dev/isci/scil/sati_mode_pages.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/sci_object.h>
#include <dev/isci/scil/sati_translator_sequence.h>
#include <dev/isci/scil/sati_util.h>

//******************************************************************************
//* P R I V A T E   M E T H O D S
//******************************************************************************

/**
 * @brief This method will get medium type parameter field per CDB size.
 *
 * @param[in] scsi_io This parameter specifies the user's SCSI IO object
 *            for which to calculate the mode page header.
 * @param[in] cdb_size This parameter specifies the number of bytes
 *            associated with the CDB for which to calculate the header.
 *
 * @return This method returns the medium type for the mode page header.
 */
static
U8 sati_mode_select_get_medium_type(
   U8 * mode_parameters,
   U32  cdb_size
)
{
   U8  medium_type =0xFF;
   SCSI_MODE_SELECT_MODE_PARAMETER_HEADER_6_T * mode_parameters_6;
   SCSI_MODE_SELECT_MODE_PARAMETER_HEADER_10_T * mode_parameters_10;

   if(cdb_size == 6)
   {
      mode_parameters_6 = (SCSI_MODE_SELECT_MODE_PARAMETER_HEADER_6_T *) mode_parameters;
      medium_type = mode_parameters_6->medium_type;
   }
   else if(cdb_size == 10)
   {
      mode_parameters_10 = (SCSI_MODE_SELECT_MODE_PARAMETER_HEADER_10_T *) mode_parameters;
      medium_type = mode_parameters_10->medium_type;
   }

   return medium_type;
}

/**
 * @brief This method will retrieve Block Descriptor Length.
 *
 * @param[in] mode_parameters This parameter contains the address to the mode parameters.
 * @param[in] cdb_size This parameter specifies the number of bytes
 *            associated with the CDB for which to process the block descriptor.
 *
 * @return This method returns the size, in bytes, for the mode parameter block descriptor.
 */
static
U32 sati_mode_select_get_mode_block_descriptor_length(
   U8 * mode_parameters,
   U32  cdb_size
)
{
   U32 mode_block_descriptor_length = 0;
   SCSI_MODE_SELECT_MODE_PARAMETER_HEADER_6_T * mode_parameters_6;
   SCSI_MODE_SELECT_MODE_PARAMETER_HEADER_10_T * mode_parameters_10;

   if(cdb_size == 6)
   {
      mode_parameters_6 = (SCSI_MODE_SELECT_MODE_PARAMETER_HEADER_6_T *) mode_parameters;
      mode_block_descriptor_length = mode_parameters_6->block_descriptor_length;
   }
   else if(cdb_size == 10)
   {
      mode_parameters_10 = (SCSI_MODE_SELECT_MODE_PARAMETER_HEADER_10_T *) mode_parameters;
      //Long LBA bit is the bit0 of the byte
      //Spec says another way to get the block descriptor length to multiply the block number
      //   with block length (8 or 16), but we can get it directly.
      mode_block_descriptor_length =(((U16)mode_parameters_10->block_descriptor_length[0]) << 8) +
         mode_parameters_10->block_descriptor_length[1];

   }

   return mode_block_descriptor_length;

}

/**
 * @brief This method will find the starting byte location for a page.
 *
 * @param[in] block_descriptor_length This parameter passes in the length of
 *            block descriptor.
 * @param[in] cdb_size This parameter specifies the number of bytes
 *            associated with the CDB for which to calculate the header.
 *
 * @return This method returns the offset, for the mode page.
 */
static
U32 sati_mode_select_get_mode_page_offset(
    U32 block_descriptor_length,
    U32 cdb_size
    )
{
   U32 mode_page_offset;

   if(cdb_size == 6)
   {
      mode_page_offset =  sizeof(SCSI_MODE_SELECT_MODE_PARAMETER_HEADER_6_T) +  block_descriptor_length;
   }
   else if(cdb_size == 10)
   {
      mode_page_offset =  sizeof(SCSI_MODE_SELECT_MODE_PARAMETER_HEADER_10_T) +  block_descriptor_length;
   }
   else
   {
      mode_page_offset = 0;
   }

   return mode_page_offset;
}

/**
 * @brief This method will set the initial Mode Select processing state.
 */
static
void  sati_mode_select_initialize_mode_sel_processing_state(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io,
   U32 data_transfer_length,
   U32 mode_page_offset
   )
{
   sequence->command_specific_data.process_state.ata_command_sent_for_cmp = 0;
   sequence->command_specific_data.process_state.mode_page_offset=mode_page_offset;
   sequence->command_specific_data.process_state.mode_pages_size = data_transfer_length  -  mode_page_offset;
   sequence->command_specific_data.process_state.size_of_data_processed = 0;
   sequence->command_specific_data.process_state.current_mode_page_processed = FALSE;
}

/**
 * @brief This method will get mode page size.
 *
 * @param[in] page_code This parameter contains page code for the current mode page.
 *
 * @return This method returns the size of current mode page.
 */
static
U32 sati_mode_select_get_mode_page_size(
   U8 page_code
)
{
   U32 page_size=0;

   switch (page_code)
   {
   case SCSI_MODE_PAGE_READ_WRITE_ERROR:
      page_size=SCSI_MODE_PAGE_01_LENGTH;
      break;

   case SCSI_MODE_PAGE_DISCONNECT_RECONNECT:
      page_size=SCSI_MODE_PAGE_02_LENGTH;
      break;

   case SCSI_MODE_PAGE_CACHING:
      page_size=SCSI_MODE_PAGE_08_LENGTH;
      break;

   case SCSI_MODE_PAGE_CONTROL:
      page_size=SCSI_MODE_PAGE_0A_LENGTH;
      break;

   case SCSI_MODE_PAGE_INFORMATIONAL_EXCP_CONTROL:
      page_size=SCSI_MODE_PAGE_1C_LENGTH;
      break;

   case SCSI_MODE_PAGE_POWER_CONDITION:
      page_size=SCSI_MODE_PAGE_1A_LENGTH;
      break;
   default:
      page_size=0;
      break;
   }

   return page_size;
}

/**
 * @brief This method will check the validity of parameter data of Read Write Error Recovery
 *            page and further processing the page data if necessary.
 *
 * @param[in] page_size This parameter specifies page size of current mode page.
 *
 * @return Indicate if the translation was successful.
 * @retval SATI_SUCCESS
 * @retval SATI_COMPLETE
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA
 */
static
SATI_STATUS sati_mode_select_process_mode_page_read_write_error_recovery(
   SATI_TRANSLATOR_SEQUENCE_T* sequence,
   void     *  scsi_io,
   U32   page_size
   )
{
   SATI_STATUS status = SATI_FAILURE_CHECK_RESPONSE_DATA;

   U8 current_mode_page[SCSI_MODE_PAGE_01_LENGTH]={0,0,0,0,0,0,0,0,0,0,0,0};
   U32 mode_page_offset;

   mode_page_offset = sequence->command_specific_data.process_state.mode_page_offset;

   //Check all the defined bits for this page
   //SPF(0b); Page length 0x0A;AWRE 1; ARRE 0; Error recovery bits 0; RC 0;
   //Recovery time limit last two bytes 0

   sati_get_data_byte(sequence, scsi_io, mode_page_offset,   &current_mode_page[0]);
   sati_get_data_byte(sequence, scsi_io, mode_page_offset+1, &current_mode_page[1]);
   sati_get_data_byte(sequence, scsi_io, mode_page_offset+2, &current_mode_page[2]);
   sati_get_data_byte(sequence, scsi_io, mode_page_offset+10, &current_mode_page[10]);
   sati_get_data_byte(sequence, scsi_io, mode_page_offset+11, &current_mode_page[11]);

   if ( ((current_mode_page[0] & SCSI_MODE_SELECT_MODE_PAGE_SPF_MASK)!= 0) ||
      (current_mode_page[1] != (SCSI_MODE_PAGE_01_LENGTH - 2)) ||
      ((current_mode_page[2] & SCSI_MODE_SELECT_MODE_PAGE_01_AWRE_MASK) == 0) ||
      ((current_mode_page[2] & SCSI_MODE_SELECT_MODE_PAGE_01_ARRE_MASK) != 0) ||
      ((current_mode_page[2] & SCSI_MODE_SELECT_MODE_PAGE_01_RC_ERBITS_MASK) != 0) ||
      (current_mode_page[10] != 0 ) ||
      (current_mode_page[11] != 0 ) )
   {
      status = SATI_FAILURE_CHECK_RESPONSE_DATA;
      return status;
   }

   //no need to send any command
   {
      sequence->command_specific_data.process_state.size_of_data_processed += page_size;
      sequence->command_specific_data.process_state.mode_page_offset += page_size;
      sequence->command_specific_data.process_state.current_mode_page_processed = TRUE;
   }

   status = SATI_COMPLETE;

   return status;
}

/**
 * @brief This method will check the validity of parameter data of Disconnect Reconnect mode
 *            page and further processing the page data if necessary.
 *
 * @param[in] page_size This parameter specifies page size of current mode page.
 *
 * @return Indicate if the translation was successful.
 * @retval SATI_SUCCESS
 * @retval SATI_COMPLETE
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA
 */
static
SATI_STATUS sati_mode_select_process_mode_page_disconnect_reconnect(
   SATI_MODE_SELECT_PROCESSING_STATE_T * mode_select_process_state,
   U32 page_size
   )
{
   SATI_STATUS status = SATI_FAILURE_CHECK_RESPONSE_DATA;

   // No need to check data for valid or invalid this page (undefined)
   // No ata command to send
   {
      mode_select_process_state->size_of_data_processed += page_size;
      mode_select_process_state->mode_page_offset += page_size;
      mode_select_process_state->current_mode_page_processed = TRUE;
   }

   // No further interaction with remote devices
   status = SATI_COMPLETE;

   return status;
}

/**
 * @brief This method will check the validity of parameter data of Caching mode
 *            page and issue multiple ATA set feature commands to complete the translation.
 *
 * @param[in] page_size This parameter specifies page size of current mode page.
 *
 * @return Indicate if the translation was successful.
 * @retval SATI_SUCCESS
 * @retval SATI_COMPLETE
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA
 */
static
SATI_STATUS sati_mode_select_process_mode_page_caching(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void * scsi_io,
   void * ata_io,
   U32 page_size
   )
{
   SATI_STATUS status = SATI_FAILURE_CHECK_RESPONSE_DATA;

   //SCSI_MODE_PAGE_08_LENGTH 0x14= 20
   U8 current_mode_page[SCSI_MODE_PAGE_08_LENGTH] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
   U32 mode_page_offset;
   U32 index;

   mode_page_offset = sequence->command_specific_data.process_state.mode_page_offset;
   sequence->type = SATI_SEQUENCE_MODE_SELECT_MODE_PAGE_CACHING;

   for(index = 0; index < SCSI_MODE_PAGE_08_LENGTH; index++)
   {
      sati_get_data_byte(sequence, scsi_io, mode_page_offset+index, &current_mode_page[index]);
   }

   //Check for data validity
   //SPF(0b); Page length 0x12;Byte2 to Byte15 all 0 with exception DRA and WCE changeable

   if (((current_mode_page[0] & SCSI_MODE_SELECT_MODE_PAGE_SPF_MASK)!= 0) ||
      (current_mode_page[1] != (SCSI_MODE_PAGE_08_LENGTH-2)) ||
      ((current_mode_page[2] | SCSI_MODE_PAGE_CACHE_PAGE_WCE_BIT)!=SCSI_MODE_PAGE_CACHE_PAGE_WCE_BIT) ||
      (current_mode_page[3] != 0 ) ||
      (current_mode_page[4] != 0 ) ||
      (current_mode_page[5] != 0 ) ||
      (current_mode_page[6] != 0 ) ||
      (current_mode_page[7] != 0 ) ||
      (current_mode_page[8] != 0 ) ||
      (current_mode_page[9] != 0 ) ||
      (current_mode_page[10] != 0 ) ||
      (current_mode_page[11] != 0 ) ||
      ((current_mode_page[12] & SCSI_MODE_SELECT_MODE_PAGE_08_FSW_LBCSS_NVDIS) != 0) ||
      (current_mode_page[13] != 0 ) ||
      (current_mode_page[14] != 0 ) ||
      (current_mode_page[15] != 0 ))
   {
      //parameter data passed in containing data that doesn't meet the SAT-2 requirement
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }

   if(sequence->command_specific_data.process_state.ata_command_sent_for_cmp == 0)
   {
      //byte2 bit2 WCE==0 disable write cache WCE==1 enable write cache
      //SCSI_MODE_PAGE_CACHE_PAGE_WCE_BIT ==0x4,

      if ( (current_mode_page[2] & SCSI_MODE_PAGE_CACHE_PAGE_WCE_BIT) == 0)
         sati_ata_set_features_construct(ata_io, sequence, ATA_SET_FEATURES_SUB_CMD_DISABLE_CACHE);
      else
         sati_ata_set_features_construct(ata_io, sequence, ATA_SET_FEATURES_SUB_CMD_ENABLE_CACHE);

   }
   else if(sequence->command_specific_data.process_state.ata_command_sent_for_cmp == 1)
   {
      // DRA bit is set to 0, enable Read look ahead AAh;
      // DRA bit is set to 1, disable with set feature command 55h
      // SCSI_MODE_PAGE_CACHE_PAGE_DRA_BIT== 0x20

      if ( (current_mode_page[12] & SCSI_MODE_PAGE_CACHE_PAGE_DRA_BIT) == 0)
         sati_ata_set_features_construct(ata_io, sequence,ATA_SET_FEATURES_SUB_CMD_ENABLE_READ_AHEAD);
      else
         sati_ata_set_features_construct(ata_io, sequence,ATA_SET_FEATURES_SUB_CMD_DISABLE_READ_AHEAD);

      sequence->command_specific_data.process_state.size_of_data_processed += page_size;
      sequence->command_specific_data.process_state.mode_page_offset += page_size;
      sequence->command_specific_data.process_state.current_mode_page_processed = TRUE;


   }
   // No more ata commands to send

   sequence->command_specific_data.process_state.ata_command_sent_for_cmp++;

   status = SATI_SUCCESS;

   return status;
}

/**
 * @brief This method will check the validity of parameter data of Control mode
 *            page and further processing the page data if necessary.
 *
 * @param[in] mode_select_process_state This parameter points to the processing state fields
 *            of current mode page.
 * @param[in] page_size This parameter specifies page size of current mode page.
 *
 * @return Indicate if the translation was successful.
 * @retval SATI_SUCCESS
 * @retval SATI_COMPLETE
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA
 */
static
SATI_STATUS sati_mode_select_process_mode_page_control(
         SATI_TRANSLATOR_SEQUENCE_T* sequence,
         void     *  scsi_io,
         void     *  ata_io,
         U32 page_size
      )
{
   SATI_STATUS status = SATI_FAILURE_CHECK_RESPONSE_DATA;

   //SCSI_MODE_PAGE_0A_LENGTH 12
   U8 current_mode_page[SCSI_MODE_PAGE_0A_LENGTH]={0,0,0,0,0,0,0,0,0,0};
   U32 mode_page_offset;
   U32 index;

   mode_page_offset = sequence->command_specific_data.process_state.mode_page_offset;

   for(index = 0; index < SCSI_MODE_PAGE_0A_LENGTH; index++)
   {
      sati_get_data_byte(sequence, scsi_io, mode_page_offset+index, &current_mode_page[index]);
   }

   //bit 1 and 2 of byte3 Qerr full task management model etc. then both bits 0
   //byte 8 and 9 busy time out period variable if not ffff setable?
   //check for page data validity
   //Byte2: 0000???0b  Byte3: Queued Algorithm Modifier should be set to 1 QErr?
   //Byte4: ??000???   Byte5: ?0???000

   if (((current_mode_page[0] & SCSI_MODE_SELECT_MODE_PAGE_SPF_MASK)!= 0) ||
      (current_mode_page[1] != (SCSI_MODE_PAGE_0A_LENGTH - 2)) ||
      ((current_mode_page[2] & SCSI_MODE_SELECT_MODE_PAGE_0A_TST_TMF_RLEC) != 0) ||
      ((current_mode_page[3] & SCSI_MODE_SELECT_MODE_PAGE_0A_MODIFIER) != 0) ||
      ((current_mode_page[4] & SCSI_MODE_SELECT_MODE_PAGE_0A_UA_SWP ) != 0) ||
      ((current_mode_page[5] & SCSI_MODE_SELECT_MODE_PAGE_0A_TAS_AUTO ) != 0 ) )
   {
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }

   if ((current_mode_page[2] & SCSI_MODE_SELECT_MODE_PAGE_D_SENSE) != 0)
       sequence->device->descriptor_sense_enable = SCSI_MODE_PAGE_CONTROL_D_SENSE_ENABLE;
   else
       sequence->device->descriptor_sense_enable = SCSI_MODE_PAGE_CONTROL_D_SENSE_DISABLE;

   // no ata command need to be comfirmed
   {
      sequence->command_specific_data.process_state.size_of_data_processed += page_size;
      sequence->command_specific_data.process_state.mode_page_offset += page_size;
      sequence->command_specific_data.process_state.current_mode_page_processed = TRUE;
   }

   status = SATI_COMPLETE;

   return status;
}

/**
 * @brief This method will check the validity of parameter data of Information Exception Control
 *            mode page and further processing the page data if necessary.
 *
 * @param[in] page_size This parameter specifies page size of current mode page.
 *
 * @return Indicate if the translation was successful.
 * @retval SATI_SUCCESS
 * @retval SATI_COMPLETE
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA
 */
static
SATI_STATUS sati_mode_select_process_mode_page_informational_exception_control(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void     *  scsi_io,
   void     *  ata_io,
   U32 page_size
   )
{
   SATI_STATUS status = SATI_FAILURE_CHECK_RESPONSE_DATA;

   //SCSI_MODE_PAGE_1C_LENGTH 12
   U8 current_mode_page[SCSI_MODE_PAGE_1C_LENGTH]={0,0,0,0,0,0,0,0,0,0,0,0};
   U32 mode_page_offset;
   U32 index;

   mode_page_offset = sequence->command_specific_data.process_state.mode_page_offset;
   sequence->type = SATI_SEQUENCE_MODE_SELECT_MODE_INFORMATION_EXCEPT_CONTROL;

   for(index = 0; index < 4; index++)
   {
      sati_get_data_byte(sequence, scsi_io, mode_page_offset+index, &current_mode_page[index]);
   }

   //Check for data validity
   //SPF(0b); Page length 0x0A; Byte2 0????0?? Byte3: ????1100
   //SCSI_MODE_SELECT_MODE_PAGE_MRIE_BYTE same as REPORT_INFO_EXCEPTION_CONDITION_ON_REQUEST 0x6
   //SCSI_MODE_PAGE_DEXCPT_ENABLE

   if (((current_mode_page[0] & SCSI_MODE_SELECT_MODE_PAGE_SPF_MASK)!= 0) ||
      (current_mode_page[1] != (SCSI_MODE_PAGE_1C_LENGTH - 2)) ||
      ((current_mode_page[2] & SCSI_MODE_SELECT_MODE_PAGE_1C_PERF_TEST)!= 0 ) ||
      ((current_mode_page[3] & SCSI_MODE_SELECT_MODE_PAGE_MRIE_MASK) !=
      SCSI_MODE_SELECT_MODE_PAGE_MRIE_BYTE ))
   {
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }

   // DEXCPT bit is set to 0, enable SMART reporting D8h;
   // DEXCPT bit is set to 1, disable SMART reporting D9h
   // SCSI_MODE_PAGE_DEXCPT_ENABLE== 0x08

   if ( (current_mode_page[2] & SCSI_MODE_PAGE_DEXCPT_ENABLE) == 0)
      sati_ata_smart_return_status_construct(ata_io, sequence, ATA_SMART_SUB_CMD_ENABLE);
   else
      sati_ata_smart_return_status_construct(ata_io, sequence, ATA_SMART_SUB_CMD_DISABLE);

   sequence->command_specific_data.process_state.size_of_data_processed += page_size;
   sequence->command_specific_data.process_state.mode_page_offset += page_size;
   sequence->command_specific_data.process_state.current_mode_page_processed = TRUE;
   // No more ata commands to send

   status = SATI_SUCCESS;

   return status;
}

/**
 * @brief This method will check the validity of parameter data of Power Condition mode
 *            page and issue multiple ATA set feature commands to complete the translation.
 *
 * @param[in] mode_select_process_state This parameter points to the processing state fields
 *            of current mode page.
 * @param[in] page_size This parameter specifies page size of current mode page.
 *
 * @return Indicate if the translation was successful.
 * @retval SATI_SUCCESS
 * @retval SATI_COMPLETE
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA
 */
static
SATI_STATUS sati_mode_select_process_mode_page_power_condition(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void * scsi_io,
   void * ata_io,
   U32 page_size
   )
{
   SATI_STATUS status = SATI_FAILURE_CHECK_RESPONSE_DATA;

   //SCSI_MODE_PAGE_1A_LENGTH 10
   U8 current_mode_page[SCSI_MODE_PAGE_1A_LENGTH] = {0,0,0,0,0,0,0,0,0,0};
   U32 mode_page_offset;
   U32 index;

   U32 timer = 0;
   U16 count = 0;

   mode_page_offset = sequence->command_specific_data.process_state.mode_page_offset;

   sequence->type = SATI_SEQUENCE_MODE_SELECT_MODE_POWER_CONDITION;

   for(index = 0; index < SCSI_MODE_PAGE_1A_LENGTH; index++)
   {
      sati_get_data_byte(sequence, scsi_io, mode_page_offset+index, &current_mode_page[index]);
   }

   //Check for data validity
   //SPF(0b); Page length 0x0A;

   if (((current_mode_page[0] & SCSI_MODE_SELECT_MODE_PAGE_SPF_MASK)!= 0) ||
      (current_mode_page[1] != (SCSI_MODE_PAGE_1A_LENGTH - 2) ) ||
      ((current_mode_page[3] & SCSI_MODE_PAGE_POWER_CONDITION_IDLE)!= 0)
      )
   {
      //parameter data passed in containing data that doesn't meet the SAT-2 requirement
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }

   // STANDBY bit is set to 0, do nothing since the standby timer can't be set;
   // STANDBY bit is set to 1, translate the standby timer
   // SCSI_MODE_PAGE_POWER_CONDITION_STANDBY== 0x01
   if (current_mode_page[3] & SCSI_MODE_PAGE_POWER_CONDITION_STANDBY)
   {
      timer = (current_mode_page[8]<<24) + (current_mode_page[9]<<16) + (current_mode_page[10]<<8) + current_mode_page[11];

      //If the ATA IDENTIFY DEVICE data word 49, bit 13 is set to one,
      if (sequence->device->capabilities & SATI_DEVICE_CAP_STANDBY_ENABLE)
      {
         if (timer == 0)
         {
            //TPV=0 send ATA STANDBY_IMMEDIATE
            sati_ata_standby_immediate_construct(ata_io, sequence);
            sequence->command_specific_data.translated_command = ATA_STANDBY_IMMED;
         }
         else if ((timer > 0) && (timer <= 12000))
         {
            //1 to 12 000 INT((z - 1) / 50) + 1
            count = (U16)((timer -1) / 50) + 1;
            sati_ata_standby_construct(ata_io, sequence, count);
         }
         else if ((timer > 12000) && (timer <= 12600))
         {
            //12 001 to 12 600 FCh
            sati_ata_standby_construct(ata_io, sequence, 0xFC);
         }
         else if ((timer > 12600) && (timer <= 12750))
         {
            //12 601 to 12 750 FFh
            sati_ata_standby_construct(ata_io, sequence, 0xFF);
         }
         else if ((timer > 12750) && (timer < 18000))
         {
            //12 751 to 17 999 F1h
            sati_ata_standby_construct(ata_io, sequence, 0xF1);
         }
         else if ((timer >= 18000) && (timer <= 198000))
         {
            //18 000 to 198 000 INT(z / 18 000) + 240
            count = (U16)(timer / 18000) + 240;
            sati_ata_standby_construct(ata_io, sequence, count);
         }
         else
         {
            //All other values FDh
            sati_ata_standby_construct(ata_io, sequence, 0xFD);
         }
         status = SATI_SUCCESS ;
      }
      else
      {
         status = SATI_FAILURE_CHECK_RESPONSE_DATA;
         //If the ATA IDENTIFY DEVICE data word 49, bit 13 is set to 0
      }
   }
   else
   {
      status = SATI_COMPLETE;
   }

   sequence->command_specific_data.process_state.size_of_data_processed += page_size;
   sequence->command_specific_data.process_state.mode_page_offset += page_size;
   sequence->command_specific_data.process_state.current_mode_page_processed = TRUE;

   return status;
}

/**
 * @brief This method will process the mode page.
 *
 *
 * @return Indicate if the translation was successful.
 * @retval SATI_SUCCESS
 * @retval SATI_COMPLETE
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA
 */
static
SATI_STATUS sati_mode_select_process_mode_page(
   SATI_TRANSLATOR_SEQUENCE_T* sequence,
   void                      * scsi_io,
   void                      * ata_io
)
{
   SATI_STATUS status = SATI_FAILURE_CHECK_RESPONSE_DATA;

   U8 page_code;
   U32 page_size = 0; // in bytes
   U32 size_of_data_to_be_processed;

   U8 page_code_byte;
   U32 mode_page_offset;

   mode_page_offset = sequence->command_specific_data.process_state.mode_page_offset;

   sati_get_data_byte(sequence, scsi_io, mode_page_offset, &page_code_byte);

   // No more pages.
   if(sequence->command_specific_data.process_state.mode_pages_size >
      sequence->command_specific_data.process_state.size_of_data_processed)
   {
      //SCSI_MODE_SENSE_PAGE_CODE_ENABLE==0x3f same for Mode Select
      page_code = page_code_byte & SCSI_MODE_SENSE_PAGE_CODE_ENABLE;
      page_size = sati_mode_select_get_mode_page_size(page_code);
      size_of_data_to_be_processed = sequence->command_specific_data.process_state.mode_pages_size
         - sequence->command_specific_data.process_state.size_of_data_processed;

      if( page_size == 0 )
      {
         status = SATI_FAILURE_CHECK_RESPONSE_DATA;
      }
      else
      {
         // process mode page
         switch(page_code)
         {
         case SCSI_MODE_PAGE_READ_WRITE_ERROR:
            status = sati_mode_select_process_mode_page_read_write_error_recovery(
                        sequence,
                        scsi_io,
                        page_size
                     );
            break;

         case SCSI_MODE_PAGE_DISCONNECT_RECONNECT:
            status = sati_mode_select_process_mode_page_disconnect_reconnect(
                        &sequence->command_specific_data.process_state,
                        page_size
                     );
            break;

         case SCSI_MODE_PAGE_CACHING:
            status = sati_mode_select_process_mode_page_caching(
                        sequence,
                        scsi_io,
                        ata_io,
                        page_size
                     );
            break;

         case SCSI_MODE_PAGE_CONTROL:
            status = sati_mode_select_process_mode_page_control(
                        sequence,
                        scsi_io,
                        ata_io,
                        page_size
                     );
            break;

         case SCSI_MODE_PAGE_INFORMATIONAL_EXCP_CONTROL:
            status = sati_mode_select_process_mode_page_informational_exception_control(
                        sequence,
                        scsi_io,
                        ata_io,
                        page_size
                     );
            break;

         case SCSI_MODE_PAGE_POWER_CONDITION:
            status = sati_mode_select_process_mode_page_power_condition(
                        sequence,
                        scsi_io,
                        ata_io,
                        page_size
                     );

            break;

         default:
            break;
         }

      }
   }

   return status;
}

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

/**
 * @brief This method will translate the SCSI Mode Select 6 byte or 10 byte command
 *        into corresponding ATA commands.  Depending upon the capabilities
 *        supported by the target different ATA commands can be selected.
 *        Additionally, in some cases more than a single ATA command may
 *        be required.
 *
 * @return Indicate if the command translation succeeded.
 * @retval SCI_SUCCESS This is returned if the command translation was
 *         successful.
 * @retval SCI_COMPLETE This is returned if the command translation was
 *         successful and no ATA commands need to be set.
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if
 *         sense data has been created as a result of something specified
 *         in the parameter data fields.
 */
static
SATI_STATUS sati_mode_select_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T   * sequence,
   void                         * scsi_io,
   void                         * ata_io,
   U32                          cdb_size
)
{
   SATI_STATUS status = SATI_FAILURE_CHECK_RESPONSE_DATA;
   U32 mode_page_offset;
   U32 block_descriptor_length;
   U32 index;
   U16 data_transfer_length;
   U8 current_mode_parameters[8]={0,0,0,0,0,0,0,0};
   U8 * cdb = sati_cb_get_cdb_address(scsi_io);

   // cdb_size must be 6 or 10
   if(FALSE == (cdb_size == 6 || cdb_size == 10))
   {
      return status;
   }

   if(sequence->state == SATI_SEQUENCE_STATE_INITIAL)
   {
      sequence->command_specific_data.process_state.ata_command_sent_for_cmp = 0;
      sequence->state = SATI_SEQUENCE_STATE_TRANSLATE_DATA;
   }

   //First, initializes mode_sel_processing_state
   if ( sequence->command_specific_data.process_state.ata_command_sent_for_cmp == 0 )
   {
      if (cdb_size == 6)
      {
         //CDB byte 4 is the parameter length
         data_transfer_length = sati_get_cdb_byte(cdb, 4);
      }
      else
      {
         //CDB byte 7 and 8 for Mode Select 10
         data_transfer_length = (sati_get_cdb_byte(cdb, 7) << 8) + sati_get_cdb_byte(cdb, 8);
      }

      sequence->allocation_length = data_transfer_length;

      //Get 8 bytes for headers (4 bytes for Mode Select 6 and 8 bytes for Mode Select 10)
      for( index = 0; index < 8; index++ )
      {
         sati_get_data_byte(sequence, scsi_io, index, &current_mode_parameters[index]);
      }

      //medium type should be 0
      if ( sati_mode_select_get_medium_type(current_mode_parameters, cdb_size) != 0 )
      {
         sati_scsi_sense_data_construct(
            sequence,
            scsi_io,
            SCSI_STATUS_CHECK_CONDITION,
            SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ASC_INVALID_FIELD_IN_PARM_LIST,
            SCSI_ASCQ_INVALID_FIELD_IN_PARM_LIST
         );
         return status;
      }

      block_descriptor_length = sati_mode_select_get_mode_block_descriptor_length(
                                   current_mode_parameters,
                                   cdb_size
                                );

      mode_page_offset = sati_mode_select_get_mode_page_offset(
                            block_descriptor_length,
                            cdb_size
                         );

      if(mode_page_offset > data_transfer_length)
      {
         sequence->state = SATI_SEQUENCE_STATE_FINAL;
         status = SATI_FAILURE_CHECK_RESPONSE_DATA;
      }
      else
      {
         sati_mode_select_initialize_mode_sel_processing_state(
            sequence,
            scsi_io,
            ata_io,
            data_transfer_length,
            mode_page_offset
         );

      }
    }

   // move to next mode page
   if(sequence->command_specific_data.process_state.current_mode_page_processed)
   {
      sequence->command_specific_data.process_state.ata_command_sent_for_cmp = 0;
      sequence->command_specific_data.process_state.current_mode_page_processed = FALSE;
   }

   status = sati_mode_select_process_mode_page(sequence, scsi_io, ata_io);

   if(sequence->command_specific_data.process_state.current_mode_page_processed != FALSE)
   {
      // Done this page
      sequence->state = SATI_SEQUENCE_STATE_FINAL;
   }
   else
   {
      sequence->state = SATI_SEQUENCE_STATE_INCOMPLETE;
   }

   if(status == SATI_FAILURE_CHECK_RESPONSE_DATA)
   {
      sequence->state = SATI_SEQUENCE_STATE_FINAL;
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ILLEGAL_REQUEST,
         SCSI_ASC_INVALID_FIELD_IN_PARM_LIST,
         SCSI_ASCQ_INVALID_FIELD_IN_PARM_LIST
      );
   }

   return status;
}

/**
 * @brief This method will call Mode Select 6 Translation command
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the command translation succeeded.
 * @retval SCI_SUCCESS This is returned if the command translation was
 *         successful.
 * @retval SCI_COMPLETE This is returned if the command translation was
 *         successful and no ATA commands need to be set.
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if
 *         sense data has been created as a result of something specified
 *         in the parameter data fields.
 */
SATI_STATUS sati_mode_select_6_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_STATUS status=SATI_FAILURE;
   U8 * cdb = sati_cb_get_cdb_address(scsi_io);

   //PF bit needs to be 1 byte1 bit ???1????
   if ((sati_get_cdb_byte(cdb, 1) & SCSI_MODE_SELECT_PF_MASK) == !SCSI_MODE_SELECT_PF_BIT)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ILLEGAL_REQUEST,
         SCSI_ASC_INVALID_FIELD_IN_CDB,
         SCSI_ASCQ_INVALID_FIELD_IN_CDB
      );
      status = SATI_FAILURE_CHECK_RESPONSE_DATA;
      return status;
   }

   status=sati_mode_select_translate_command(
             sequence,
             scsi_io,
             ata_io,
             6
          );

   if(status == SATI_FAILURE_CHECK_RESPONSE_DATA)
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
   return status;

}

/**
 * @brief This method will call Mode Select 10 translation command
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the command translation succeeded.
 * @retval SCI_SUCCESS This is returned if the command translation was
 *         successful.
 * @retval SCI_COMPLETE This is returned if the command translation was
 *         successful and no ATA commands need to be set.
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if
 *         sense data has been created as a result of something specified
 *         in the parameter data fields.
 */
SATI_STATUS sati_mode_select_10_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_STATUS status=SATI_FAILURE;
   U8 * cdb = sati_cb_get_cdb_address(scsi_io);

   //PF bit needs to be 1 byte1 bit ???1????
   if ((sati_get_cdb_byte(cdb, 1) & SCSI_MODE_SELECT_PF_MASK) == !SCSI_MODE_SELECT_PF_BIT)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ILLEGAL_REQUEST,
         SCSI_ASC_INVALID_FIELD_IN_CDB,
         SCSI_ASCQ_INVALID_FIELD_IN_CDB
      );
      status = SATI_FAILURE_CHECK_RESPONSE_DATA;
      return status;
   }

   status=sati_mode_select_translate_command(
             sequence,
             scsi_io,
             ata_io,
             10
          );

   if(status == SATI_FAILURE_CHECK_RESPONSE_DATA)
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
   return status;
}

/**
* @brief This method will conduct error handling for the ATA Set Features command
*        that is issued during a Mode Select translation for the Caching Mode
*        page.
*
*
* @return Indicate if the command translation succeeded.
*
* @retval SCI_COMPLETE This is returned if the command translation was
*         successful and no additional ATA commands need to be set.
* @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if
*         sense data has been created as a result of an error returned
*/
SATI_STATUS sati_mode_select_translate_response(
SATI_TRANSLATOR_SEQUENCE_T * sequence,
void                       * scsi_io,
void                       * ata_io
)
{
   U8 * register_fis = sati_cb_get_d2h_register_fis_address(ata_io);
   SATI_STATUS status = SATI_FAILURE;

   if(sati_get_ata_status(register_fis) & ATA_STATUS_REG_ERROR_BIT)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ABORTED_COMMAND,
         SCSI_ASC_NO_ADDITIONAL_SENSE,
         SCSI_ASCQ_NO_ADDITIONAL_SENSE
      );
      status = SATI_FAILURE_CHECK_RESPONSE_DATA;
   }
   else
   {
      if (sequence->state == SATI_SEQUENCE_STATE_INCOMPLETE)
      {
         status = SATI_SEQUENCE_INCOMPLETE;
      }
      else
      {
         status = SATI_COMPLETE;
      }
   }
   return status;
}

#endif // !defined(DISABLE_SATI_MODE_SELECT)
