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
 *        translate the SCSI reassign blocks command.
 */

#if !defined(DISABLE_SATI_REASSIGN_BLOCKS)

#include <dev/isci/scil/sati_reassign_blocks.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/sati_move.h>
#include <dev/isci/scil/sati_write.h>
#include <dev/isci/scil/sati_translator_sequence.h>
#include <dev/isci/scil/sati_util.h>
#include <dev/isci/scil/intel_scsi.h>


//******************************************************************************
//* P R I V A T E   M E T H O D S
//******************************************************************************
// static SATI_REASSIGN_BLOCKS_PROCESSING_STATE_T reassign_blocks_processing_state;

/**
 * @brief This method copies short 24bits LBA bytes to the command register
 * @return Indicate if the method was successfully completed.
 * @retval SATI_SUCCESS This is returned in all other cases.
 */
static
void set_current_lba(U8 * lba, void  * ata_io)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_lba_low(register_fis, lba[0]);
   sati_set_ata_lba_mid(register_fis, lba[1]);
   sati_set_ata_lba_high(register_fis, lba[2]);
   sati_set_ata_device_head(register_fis, ATA_DEV_HEAD_REG_LBA_MODE_ENABLE | (lba[3] & 0x0F));


}

/**
 * @brief This method copies short 48bits LBA bytes to the command register
 * @return Indicate if the method was successfully completed.
 * @retval SATI_SUCCESS This is returned in all other cases.
 */
static
void set_current_long_lba(U8 * lba, void  * ata_io)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_set_ata_lba_low(register_fis, lba[0]);
   sati_set_ata_lba_mid(register_fis, lba[1]);
   sati_set_ata_lba_high(register_fis, lba[2]);
   sati_set_ata_lba_low_exp(register_fis, lba[3]);
   sati_set_ata_lba_mid_exp(register_fis, lba[4]);
   sati_set_ata_lba_high_exp(register_fis, lba[5]);
}

/**
 * @brief This method performs the SCSI VERIFY command translation
 *        functionality.
 *        This includes:
 *        - setting the command register
 *        - setting the device head register
 *        - filling in fields in the SATI_TRANSLATOR_SEQUENCE object.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the method was successfully completed.
 * @retval SATI_SUCCESS This is returned in all other cases.
 */
static
SATI_STATUS sati_reassign_blocks_verify_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_ata_non_data_command(ata_io, sequence);

   // Ensure the device supports the 48 bit feature set.
   if (sequence->device->capabilities & SATI_DEVICE_CAP_48BIT_ENABLE)
      sati_set_ata_command(register_fis, ATA_READ_VERIFY_SECTORS_EXT);
   else
      sati_set_ata_command(register_fis, ATA_READ_VERIFY_SECTORS);

   return SATI_SUCCESS;
}

/**
 * @brief This method performs the SCSI Write sector command translation
 *        functionality.
 *        This includes:
 *        - setting the command register
 *        - setting the device head register
 *        - filling in fields in the SATI_TRANSLATOR_SEQUENCE object.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the method was successfully completed.
 * @retval SATI_SUCCESS This is returned in all other cases.
 */
static
SATI_STATUS sati_reassign_blocks_write_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * register_fis = sati_cb_get_h2d_register_fis_address(ata_io);

   sati_ata_non_data_command(ata_io, sequence);
   sequence->data_direction = SATI_DATA_DIRECTION_OUT;

//   sati_set_ata_sector_count(register_fis, 1);
//   status=sati_move_set_sector_count(sequence,scsi_io,ata_io,1,0);

   // Ensure the device supports the 48 bit feature set.
   if (sequence->device->capabilities & SATI_DEVICE_CAP_48BIT_ENABLE)
      sati_set_ata_command(register_fis, ATA_WRITE_DMA_EXT);
   else
      sati_set_ata_command(register_fis, ATA_WRITE_DMA);

   return SATI_SUCCESS; //sati_move_set_sector_count(sequence,scsi_io,ata_io,1,0);
}

/**
 * @brief This method performs the retrieving of parameter LBA praparation and setting
 *        processing flags before/after calling SCSI Verify sector command.
 * @return Indicate if the method was successfully completed.
 * @retval SATI_SUCCESS This is returned in all other cases.
 */
static
SATI_STATUS sati_reassign_blocks_verify_condition(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void     * scsi_io,
   void     * ata_io
)
{
   U8 current_lba_bytes[8] = {0,0,0,0,0,0,0,0};
   U32 lba_offset;
   U8 page_size;
   U32 index;
   SATI_STATUS status = SATI_FAILURE_CHECK_RESPONSE_DATA;

   lba_offset = sequence->command_specific_data.reassign_blocks_process_state.lba_offset;
   page_size = sequence->command_specific_data.reassign_blocks_process_state.lba_size;

   for(index = 0; index < page_size; index++)
   {
      sati_get_data_byte(sequence, scsi_io, lba_offset+index,   &current_lba_bytes[index]);
   }

   if (page_size == 4)
      set_current_lba(current_lba_bytes, ata_io);
   else
      set_current_long_lba(current_lba_bytes, ata_io);

   status = sati_reassign_blocks_verify_command(sequence, scsi_io, ata_io);
   sequence->command_specific_data.reassign_blocks_process_state.ata_command_sent_for_current_lba++;
   sequence->command_specific_data.reassign_blocks_process_state.ata_command_status = SATI_REASSIGN_BLOCKS_READY_TO_SEND;
   return  status;
}

/**
 * @brief This method performs the retrieving of parameter LBA praparation and setting
 *        processing flags before/after calling SCSI Write sector command.
 * @return Indicate if the method was successfully completed.
 * @retval SATI_SUCCESS This is returned in all other cases.
 */
static
SATI_STATUS sati_reassign_blocks_write_condition(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void     * scsi_io,
   void     * ata_io
)
{
   U8 current_lba_bytes[8] = {0,0,0,0,0,0,0,0};
   U32 lba_offset;
   U8 page_size;
   U32 index;
   SATI_STATUS status = SATI_FAILURE_CHECK_RESPONSE_DATA;

   lba_offset = sequence->command_specific_data.reassign_blocks_process_state.lba_offset;
   page_size = sequence->command_specific_data.reassign_blocks_process_state.lba_size;

   for(index = 0; index < page_size; index++)
   {
      sati_get_data_byte(sequence, scsi_io, lba_offset+index,   &current_lba_bytes[index]);
   }

   if (page_size == 4)
      set_current_lba(current_lba_bytes, ata_io);
   else
      set_current_long_lba(current_lba_bytes, ata_io);

   status = sati_reassign_blocks_write_command(sequence, scsi_io, ata_io);
   sequence->command_specific_data.reassign_blocks_process_state.ata_command_sent_for_current_lba++;
   sequence->command_specific_data.reassign_blocks_process_state.ata_command_status = SATI_REASSIGN_BLOCKS_READY_TO_SEND;
   return  status ;
}


/**
 * @brief This method will perform the pre-processing of Reassign Blocks command and parameter.
 */
static
void  sati_reassign_blocks_initial_processing(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U32 index;
   U8 long_lba_bit;
   U8 long_list_bit;
   U8 lba_offset;
   U8  page_size;
   U32 data_transfer_length;
   U8 header_bytes[4]={0,0,0,0};

   U8 * cdb = sati_cb_get_cdb_address(scsi_io);

   //A long LBA (LONGLBA) bit set to zero specifies that the REASSIGN BLOCKS defective LBA list contains four-byte LBAs.
   //A LONGLBA bit set to one specifies that the REASSIGN BLOCKS defective LBA list contains eight-byte LBAs.
   if ((sati_get_cdb_byte(cdb, 1) & SCSI_REASSIGN_BLOCKS_LONGLBA_BIT) == 0)
   {
      long_lba_bit=0;
      page_size = 4; //beginning of lba list
   }
   else
   {
      long_lba_bit=1;
      page_size = 8;
   }

   //The long list (LONGLIST) bit specifies which parameter list header
   if ((sati_get_cdb_byte(cdb, 1) & SCSI_REASSIGN_BLOCKS_LONGLIST_BIT) == 0)
   {
      long_list_bit=0;
   }
   else
   {
      long_list_bit=1;
   }

   sequence->allocation_length = 4; //Pre-set allocation_length so that the header can be retrieved

   //Get 4 bytes for headers (byte 2 & byte 3 for short header; long header all 4 bytes)
   for(index = 0; index < 4; index++)
   {
      sati_get_data_byte(sequence, scsi_io, index,   &header_bytes[index]);
   }

   lba_offset = 4; //beginning of lba list

   if (long_list_bit==0)
   {
      //Header byte 2 and 3 is the parameter list length
      data_transfer_length = (header_bytes[2]<<8) + header_bytes[3] + lba_offset;
   }
   else
   {
      //Header byte 0, 1, 2 and 3 contain the parameter list length
      data_transfer_length = (header_bytes[0]<<24) + (header_bytes[1]<<16) +
         (header_bytes[2]<<8) + header_bytes[3] + lba_offset;
   }

   sequence->allocation_length = data_transfer_length;

   //Initialized the global processing state
   sequence->command_specific_data.reassign_blocks_process_state.lba_size   =     page_size;
   sequence->command_specific_data.reassign_blocks_process_state.lba_offset =     lba_offset;
   sequence->command_specific_data.reassign_blocks_process_state.ata_command_sent_for_current_lba = 0;
   sequence->command_specific_data.reassign_blocks_process_state.block_lists_size       = data_transfer_length  -  lba_offset;
   sequence->command_specific_data.reassign_blocks_process_state.size_of_data_processed = 0;
   sequence->command_specific_data.reassign_blocks_process_state.current_lba_processed  = FALSE;
   sequence->command_specific_data.reassign_blocks_process_state.ata_command_status     = SATI_REASSIGN_BLOCKS_COMMAND_FAIL;
}

/**
 * @brief This method will get the data size of not yet processed data.
 *
 * @param[in] lba_process_state This parameter points to the processing state fields
 *            of current block lba.
 *
 * @return This method returns the sizeof not yet processed data.
 */
static
U32 sati_reassign_blocks_unprocessed_data_size(
   SATI_REASSIGN_BLOCKS_PROCESSING_STATE_T * lba_process_state
)
{
   U32 unprocessed_data_size;

   if(lba_process_state->block_lists_size >= lba_process_state->size_of_data_processed)
   {
      unprocessed_data_size = lba_process_state->block_lists_size -
         lba_process_state->size_of_data_processed;
   }
   else
   {
      unprocessed_data_size = 0;
   }

   return unprocessed_data_size;
}


/**
 * @brief This method will check verify the sector and issue multiple ATA set feature commands to complete the translation.
 *
 * @param[in] reassign_blocks_process_state This parameter points to the processing state fields
 *            of current lba block.
 *
 *
 * @return Indicate if the translation was successful.
 * @retval SATI_SUCCESS
 * @retval SATI_COMPLETE
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA
 */
static
SATI_STATUS sati_reassign_blocks_process_each_lba(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
   )
{
   SATI_STATUS status = SATI_FAILURE_CHECK_RESPONSE_DATA;
   SATI_REASSIGN_BLOCKS_PROCESSING_STATE_T * reassign_blocks_process_state;

   reassign_blocks_process_state = &sequence->command_specific_data.reassign_blocks_process_state;

   if((reassign_blocks_process_state->ata_command_sent_for_current_lba == 0)&&
      (reassign_blocks_process_state->ata_command_status == SATI_REASSIGN_BLOCKS_COMMAND_FAIL))
   {
      reassign_blocks_process_state->size_of_data_processed += reassign_blocks_process_state->lba_size;
      status = sati_reassign_blocks_verify_condition(sequence, scsi_io, ata_io);
   }
   else if((reassign_blocks_process_state->ata_command_sent_for_current_lba == 0)&&
      (reassign_blocks_process_state->ata_command_status == SATI_REASSIGN_BLOCKS_COMMAND_SUCCESS))
   {
      // point to next lba
      reassign_blocks_process_state->size_of_data_processed += reassign_blocks_process_state->lba_size;
      reassign_blocks_process_state->lba_offset += reassign_blocks_process_state->lba_size;
      status = sati_reassign_blocks_verify_condition(sequence, scsi_io, ata_io);
   }
   else if((reassign_blocks_process_state->ata_command_sent_for_current_lba == 1)&&
      (reassign_blocks_process_state->ata_command_status == SATI_REASSIGN_BLOCKS_COMMAND_FAIL))
   {
      reassign_blocks_process_state->size_of_data_processed += reassign_blocks_process_state->lba_size;
      status = sati_reassign_blocks_write_condition(sequence, scsi_io, ata_io);
   }
   else if((reassign_blocks_process_state->ata_command_sent_for_current_lba == 2) &&
      (reassign_blocks_process_state->ata_command_status == SATI_REASSIGN_BLOCKS_COMMAND_SUCCESS))
   {
      reassign_blocks_process_state->size_of_data_processed += reassign_blocks_process_state->lba_size;
      status = sati_reassign_blocks_verify_condition(sequence, scsi_io, ata_io);
   }
   else //commands sent is 2; SATI_REASSIGN_BLOCKS_COMMAND_FAIL
   {
      status = SATI_FAILURE_CHECK_RESPONSE_DATA;
   }

   return status;
}

/**
 * @brief This method will process the each lba.
 *
 * @param[in] reassign_blocks_process_state This parameter points to the processing state fields
 *            of current lba.
 *
 * @return Indicate if the translation was successful.
 * @retval SATI_SUCCESS
 * @retval SATI_COMPLETE
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA
 */
static
SATI_STATUS sati_reassign_blocks_process(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_STATUS status = SATI_FAILURE_CHECK_RESPONSE_DATA;

   U32 page_size = 0; // in bytes
   U32 size_of_data_to_be_processed;
   U32 lba_offset;
   SATI_REASSIGN_BLOCKS_PROCESSING_STATE_T * reassign_blocks_process_state;

   reassign_blocks_process_state = &sequence->command_specific_data.reassign_blocks_process_state;

   lba_offset = reassign_blocks_process_state->lba_offset;
   page_size  = reassign_blocks_process_state->lba_size;


   if(sati_reassign_blocks_unprocessed_data_size(reassign_blocks_process_state) < page_size)
   {
      return status;
   }

   // Any more lba blocks? If not, done.
   if(reassign_blocks_process_state->block_lists_size ==
      reassign_blocks_process_state->size_of_data_processed)
   {
      sequence->state = SATI_SEQUENCE_STATE_FINAL;
      status = SATI_COMPLETE;
   }
   //start processing next lba
   else
   {
      size_of_data_to_be_processed = reassign_blocks_process_state->block_lists_size
         - reassign_blocks_process_state->size_of_data_processed;

      status = sati_reassign_blocks_process_each_lba(sequence, scsi_io, ata_io);

   }

   return status;
}

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

/**
 * @brief This method will translate the SCSI Reassign Blocks command
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
SATI_STATUS sati_reassign_blocks_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_STATUS status = SATI_FAILURE_CHECK_RESPONSE_DATA;
   SATI_REASSIGN_BLOCKS_PROCESSING_STATE_T * reassign_blocks_process_state;

   reassign_blocks_process_state = &sequence->command_specific_data.reassign_blocks_process_state;

   sequence->type = SATI_SEQUENCE_REASSIGN_BLOCKS;

   //Initial processing if
   if ( sequence->state != SATI_SEQUENCE_STATE_INCOMPLETE )
   {
      sati_reassign_blocks_initial_processing(
         sequence,
         scsi_io,
         ata_io
      );
   }

   // start processing current lba
   if(reassign_blocks_process_state->current_lba_processed)
   {
      reassign_blocks_process_state->ata_command_sent_for_current_lba = 0;
      reassign_blocks_process_state->current_lba_processed = FALSE;
   }

   status = sati_reassign_blocks_process(sequence, scsi_io, ata_io);

   if(reassign_blocks_process_state->block_lists_size ==
      reassign_blocks_process_state->size_of_data_processed)
   {
      // Done this lba
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
         SCSI_SENSE_MEDIUM_ERROR,
         SCSI_ASC_UNRECOVERED_READ_ERROR,
         SCSI_ASCQ_UNRECOVERED_READ_ERROR_AUTO_REALLOCATE_FAIL
      );
   }

   return status;
}

/**
 * @brief This method will translate the ATA command register FIS
 *        response into an appropriate SCSI response for Reassign Blocks
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_response().
 *
 * @return Indicate if the response translation succeeded.
 * @retval SCI_SUCCESS This is returned if the data translation was
 *         successful.
 */
SATI_STATUS sati_reassign_blocks_translate_response(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * register_fis = sati_cb_get_d2h_register_fis_address(ata_io);
   SATI_REASSIGN_BLOCKS_PROCESSING_STATE_T * reassign_blocks_process_state;

   reassign_blocks_process_state = &sequence->command_specific_data.reassign_blocks_process_state;

   if (sati_get_ata_status(register_fis) & ATA_STATUS_REG_ERROR_BIT)
   {
      reassign_blocks_process_state->ata_command_status = SATI_REASSIGN_BLOCKS_COMMAND_FAIL;

      //Checking for the number of ATA commands attempted on current LBA, stop
      //the seaquence after 2 commands have returned errors.
      if(reassign_blocks_process_state->ata_command_sent_for_current_lba < 2)
      {
         sequence->state = SATI_SEQUENCE_STATE_INCOMPLETE;
         reassign_blocks_process_state->size_of_data_processed -= reassign_blocks_process_state->lba_size;
         return SATI_SEQUENCE_INCOMPLETE;
      }
      else
      {
         sati_scsi_sense_data_construct(
            sequence,
            scsi_io,
            SCSI_STATUS_CHECK_CONDITION,
            SCSI_SENSE_MEDIUM_ERROR,
            SCSI_ASC_UNRECOVERED_READ_ERROR,
            SCSI_ASCQ_UNRECOVERED_READ_ERROR_AUTO_REALLOCATE_FAIL
         );
      }

      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }
   else
   {
      reassign_blocks_process_state->ata_command_status = SATI_REASSIGN_BLOCKS_COMMAND_SUCCESS;
      if (reassign_blocks_process_state->ata_command_sent_for_current_lba != 2)
         reassign_blocks_process_state->current_lba_processed = TRUE;

      if (sequence->state == SATI_SEQUENCE_STATE_INCOMPLETE)
      {
         return SATI_SEQUENCE_INCOMPLETE;
      }
   }
   return SATI_COMPLETE;
}

#endif // !defined(DISABLE_SATI_REASSIGN_BLOCKS)

