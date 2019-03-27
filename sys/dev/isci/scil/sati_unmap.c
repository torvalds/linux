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
 *        translate the SCSI unmap command.
 */

#if !defined(DISABLE_SATI_UNMAP)

#include <dev/isci/scil/sati_unmap.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/sati_translator_sequence.h>
#include <dev/isci/scil/sati_util.h>
#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/intel_scsi.h>
#include <dev/isci/scil/intel_sat.h>

//******************************************************************************
//* P R I V A T E   M E T H O D S
//******************************************************************************

/**
 * @brief This method translates a given number of DSM
 *        requests into DSM blocks based on the devices logical block size
 *
 * @return Number of DSM blocks required for the DSM descriptor count
 */
U32 sati_unmap_calculate_dsm_blocks(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   U32                          dsm_descriptor_count
)
{
   U32 blocks = (dsm_descriptor_count * sizeof(TRIM_PAIR))/sequence->device->logical_block_size;
   if ((dsm_descriptor_count * sizeof(TRIM_PAIR)) % sequence->device->logical_block_size)
   {
       blocks++;
   }
   return blocks;
}

/**
 * @brief This method performs the SCSI Unmap command translation
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
SATI_STATUS sati_unmap_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io,
   U32                          sector_count
)
{
   U8 * h2d_register_fis = sati_cb_get_h2d_register_fis_address(ata_io);
   U8 * d2h_register_fis = sati_cb_get_d2h_register_fis_address(ata_io);

   sati_set_ata_command(h2d_register_fis, ATA_DATA_SET_MANAGEMENT);
   sati_set_ata_features(h2d_register_fis, 0x01);
   sati_set_ata_sector_count(h2d_register_fis, (U8)sector_count);
   sati_set_ata_device_head(h2d_register_fis, ATA_DEV_HEAD_REG_LBA_MODE_ENABLE);

   // Set the completion status since the core will not do that for
   // the udma fast path.
   sati_set_ata_status(d2h_register_fis, 0x00);

   // Set up the direction and protocol for SCIC
   sequence->data_direction                 = SATI_DATA_DIRECTION_OUT;
   sequence->protocol                       = SAT_PROTOCOL_UDMA_DATA_OUT;
   // The UNMAP translation will always require a callback
   // on every response so it can free memory if an error
   // occurs.
   sequence->is_translate_response_required = TRUE;

   ASSERT(sector_count < 0x100);

   return SATI_SUCCESS;
}

/**
 * @brief This method updates the unmap sequence state to the next
 *        unmap descriptor
 *
 * @return Indicate if the method was successfully completed.
 * @retval SATI_SUCCESS This is returned in all other cases.
 */
SATI_STATUS sati_unmap_load_next_descriptor(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io
)
{
   SATI_UNMAP_PROCESSING_STATE_T * unmap_process_state;
   U32                             index;
   U8                              unmap_block_descriptor[16];

   unmap_process_state = &sequence->command_specific_data.unmap_process_state;

   // Load the next descriptor
   for(index = unmap_process_state->current_unmap_block_descriptor_index;
       index < unmap_process_state->current_unmap_block_descriptor_index +
               SATI_UNMAP_SIZEOF_SCSI_UNMAP_BLOCK_DESCRIPTOR;
       index++)
   {
      sati_get_data_byte(sequence,
         scsi_io,
         index,
         &unmap_block_descriptor[index-unmap_process_state->current_unmap_block_descriptor_index]);
   }

   // Update the internal state for the next translation pass
   unmap_process_state->current_lba_count = (unmap_block_descriptor[8] << 24) |
                                            (unmap_block_descriptor[9] << 16) |
                                            (unmap_block_descriptor[10] << 8) |
                                            (unmap_block_descriptor[11]);
   unmap_process_state->current_lba       = ((SATI_LBA)(unmap_block_descriptor[0]) << 56) |
                                            ((SATI_LBA)(unmap_block_descriptor[1]) << 48) |
                                            ((SATI_LBA)(unmap_block_descriptor[2]) << 40) |
                                            ((SATI_LBA)(unmap_block_descriptor[3]) << 32) |
                                            ((SATI_LBA)(unmap_block_descriptor[4]) << 24) |
                                            ((SATI_LBA)(unmap_block_descriptor[5]) << 16) |
                                            ((SATI_LBA)(unmap_block_descriptor[6]) << 8) |
                                            ((SATI_LBA)(unmap_block_descriptor[7]));
   unmap_process_state->next_lba          = 0;

   // Update the index for the next descriptor to translate
   unmap_process_state->current_unmap_block_descriptor_index += SATI_UNMAP_SIZEOF_SCSI_UNMAP_BLOCK_DESCRIPTOR;

   return SATI_SUCCESS;
}

/**
 * @brief This method determines the max number of blocks of DSM data
 *        that can be satisfied by the device and the SW
 *
 * @return Number of blocks supported
 * @retval Number of blocks supported
 */
U32 sati_unmap_get_max_buffer_size_in_blocks(
   SATI_TRANSLATOR_SEQUENCE_T * sequence
)
{
   // Currently this SATI implementation only supports a single
   // 4k block of memory for the DMA write operation for simplicity
   // (no need to handle more than one SG element).
   // Since most run time UNMAP requests use 1K or less buffer space,
   // there is no performance degradation with only supporting a
   // single physical page.  For best results allocate the maximum
   // amount of memory the device can handle up to the maximum of 4K.
   return MIN(SATI_DSM_MAX_BUFFER_SIZE/sequence->device->logical_block_size,
              sequence->device->max_lba_range_entry_blocks);
}

/**
 * @brief This method will be called before starting the first unmap translation
 *
 * @return Indicate if the translation was successful.
 * @retval SATI_SUCCESS This is returned if the command translation was
 *         successful and no further processing.
 * @retval SATI_COMPLETE - The initial processing was completed successfully
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA - Failed the initial processing
 */
SATI_STATUS sati_unmap_initial_processing(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_UNMAP_PROCESSING_STATE_T * unmap_process_state;
   U8 * cdb;
   U16 unmap_length;
   U32 descriptor_length;
   U32 index;
   U32 max_dsm_blocks;
   U8  unmap_param_list[8];

   unmap_process_state = &sequence->command_specific_data.unmap_process_state;

   // Set up the sequence type for unmap translation
   sequence->type = SATI_SEQUENCE_UNMAP;

   // Make sure the device is TRIM capable
   if ((sequence->device->capabilities & SATI_DEVICE_CAP_DSM_TRIM_SUPPORT)
       != SATI_DEVICE_CAP_DSM_TRIM_SUPPORT)
   {
      // Can't send TRIM request to device that does not support it
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ILLEGAL_REQUEST,
         SCSI_ASC_INVALID_FIELD_IN_CDB,
         SCSI_ASCQ_INVALID_FIELD_IN_CDB
      );
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }

   // get the amount of data being sent from the cdb
   cdb = sati_cb_get_cdb_address(scsi_io);
   unmap_length = (sati_get_cdb_byte(cdb, 7) << 8) | sati_get_cdb_byte(cdb, 8);

   // If nothing has been requested return success now.
   if (unmap_length == 0)
   {
       // SAT: This is not an error
       return SATI_SUCCESS;
   }
   if (unmap_length < SATI_UNMAP_SIZEOF_SCSI_UNMAP_PARAMETER_LIST)
   {
      // Not enough length specified in the CDB
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ILLEGAL_REQUEST,
         SCSI_ASC_INVALID_FIELD_IN_CDB,
         SCSI_ASCQ_INVALID_FIELD_IN_CDB
      );
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }

   sequence->allocation_length = unmap_length;

   // Get the unmap parameter header
   for(index = 0; index < SATI_UNMAP_SIZEOF_SCSI_UNMAP_PARAMETER_LIST; index++)
   {
      sati_get_data_byte(sequence, scsi_io, index,   &unmap_param_list[index]);
   }
   descriptor_length = (unmap_param_list[2] << 8) | unmap_param_list[3];

   // Check length again
   if (descriptor_length == 0)
   {
       // SAT: This is not an error
       return SATI_SUCCESS;
   }

   if ((U32)(unmap_length - SATI_UNMAP_SIZEOF_SCSI_UNMAP_PARAMETER_LIST) < descriptor_length)
   {
      // Not enough length specified in the CDB
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ILLEGAL_REQUEST,
         SCSI_ASC_INVALID_FIELD_IN_CDB,
         SCSI_ASCQ_INVALID_FIELD_IN_CDB
      );
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }

   // Save the maximum unmap block descriptors in this request
   unmap_process_state->max_unmap_block_descriptors =
       descriptor_length/SATI_UNMAP_SIZEOF_SCSI_UNMAP_BLOCK_DESCRIPTOR;

   // Determine the maximum size of the write buffer that will be required
   // for the translation in terms of number of blocks
   max_dsm_blocks = sati_unmap_get_max_buffer_size_in_blocks(sequence);

   // Save the maximum number of DSM descriptors we can send during the translation
   unmap_process_state->max_lba_range_entries =
       (max_dsm_blocks*sequence->device->logical_block_size)/sizeof(TRIM_PAIR);

   // Get the write buffer for the translation
   sati_cb_allocate_dma_buffer(
      scsi_io,
      max_dsm_blocks*sequence->device->logical_block_size,
      &(unmap_process_state->virtual_unmap_buffer),
      &(unmap_process_state->physical_unmap_buffer_low),
      &(unmap_process_state->physical_unmap_buffer_high));

   // Makes sure we have a buffer
   if (unmap_process_state->virtual_unmap_buffer == NULL)
   {
      // Resource failure
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_BUSY,
         SCSI_SENSE_NO_SENSE,
         SCSI_ASC_NO_ADDITIONAL_SENSE,
         SCSI_ASCQ_NO_ADDITIONAL_SENSE
      );
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }

   // Get the first SGL entry.  This code will only use one 4K page so will
   // only utilize the first sge.
   sati_cb_sgl_next_sge(scsi_io,
                        ata_io,
                        NULL,
                        &(unmap_process_state->unmap_buffer_sgl_pair));

   // Load the first descriptor to start the translation loop
   unmap_process_state->current_unmap_block_descriptor_index =
      SATI_UNMAP_SIZEOF_SCSI_UNMAP_PARAMETER_LIST;
   sati_unmap_load_next_descriptor(sequence,scsi_io);

   // Next state will be incomplete since translation
   // will require a callback and possibly more requests.
   sequence->state = SATI_SEQUENCE_STATE_INCOMPLETE;

   return SATI_COMPLETE;
}

/**
 * @brief This method will process each unmap sequence.
 *
 * @return Indicate if the translation was successful.
 * @retval SATI_SUCCESS
 */
SATI_STATUS sati_unmap_process(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_UNMAP_PROCESSING_STATE_T * unmap_process_state;
   SATI_LBA dsm_descriptor_lba_count;
   U32 dsm_descriptor;
   U32 dsm_bytes;
   U32 dsm_remainder_bytes;
   U32 dsm_blocks;
   U32 max_dsm_blocks;

   unmap_process_state = &sequence->command_specific_data.unmap_process_state;

   // Set up the starting address of the buffer for this portion of the translation
   unmap_process_state->current_dsm_descriptor = unmap_process_state->virtual_unmap_buffer;
   dsm_descriptor = 0;

   // Translate as much as we can
   while ((dsm_descriptor < unmap_process_state->max_lba_range_entries) &&
          (unmap_process_state->current_lba_count > 0)) {
      // See if the LBA count will fit in to a single descriptor
      if (unmap_process_state->current_lba_count > SATI_DSM_MAX_SECTOR_COUNT) {
         // Can't fit all of the lbas for this descriptor in to
         // one DSM request.  Adjust the current LbaCount and total
         // remaining for the next descriptor
         dsm_descriptor_lba_count = SATI_DSM_MAX_SECTOR_COUNT;
         unmap_process_state->current_lba_count -= SATI_DSM_MAX_SECTOR_COUNT;
         unmap_process_state->next_lba =
             unmap_process_state->current_lba + SATI_DSM_MAX_SECTOR_COUNT;
      } else {
         // It all fits in to one descriptor
         dsm_descriptor_lba_count = unmap_process_state->current_lba_count;
         unmap_process_state->current_lba_count = 0;
      }

      // Fill in the ATA DSM descriptor
      ((PTRIM_PAIR)(unmap_process_state->current_dsm_descriptor))->sector_address =
          unmap_process_state->current_lba;
      ((PTRIM_PAIR)(unmap_process_state->current_dsm_descriptor))->sector_count =
          dsm_descriptor_lba_count;

      // See if we can move on to the next descriptor
      if (unmap_process_state->current_lba_count == 0) {
         // See if there is another descriptor
         --unmap_process_state->max_unmap_block_descriptors;
         if (unmap_process_state->max_unmap_block_descriptors > 0) {
            // Move on to the next descriptor
            sati_unmap_load_next_descriptor(sequence,scsi_io);
         }
      } else {
         // Move to the next LBA in this descriptor
         unmap_process_state->current_lba = unmap_process_state->next_lba;
      }

      // Make sure the LBA does not exceed 48 bits...
      ASSERT(unmap_process_state->current_lba <= SATI_DSM_MAX_SECTOR_ADDRESS);

      // Increment the number of descriptors used and point to the next entry
      dsm_descriptor++;
      unmap_process_state->current_dsm_descriptor =
          (U8 *)(unmap_process_state->current_dsm_descriptor) + sizeof(TRIM_PAIR);
   }

   // Calculate number of blocks we have filled in
   dsm_blocks     = sati_unmap_calculate_dsm_blocks(sequence,dsm_descriptor);
   dsm_bytes      = dsm_blocks * sequence->device->logical_block_size;
   max_dsm_blocks = sati_unmap_get_max_buffer_size_in_blocks(sequence);

   // The current_dsm_descriptor points to the next location in the buffer
   // Get the remaining bytes from the last translated descriptor
   // to the end of the 4k buffer.
   dsm_remainder_bytes = sequence->device->logical_block_size;
   dsm_remainder_bytes -= (U32)((POINTER_UINT)unmap_process_state->current_dsm_descriptor &
                                (sequence->device->logical_block_size-1));

   // If there was no remainder, the complete buffer was filled in.
   if (dsm_remainder_bytes != sequence->device->logical_block_size)
   {
       // Add on the remaining unfilled blocks
       dsm_remainder_bytes += (sequence->device->logical_block_size * (max_dsm_blocks - dsm_blocks));

       // According to ATA-8, if the DSM buffer is not completely filled with
       // valid DSM descriptor data, the remaining portion of the
       // buffer must be filled in with zeros.
       memset((U8 *)unmap_process_state->current_dsm_descriptor, 0, dsm_remainder_bytes);
   }

   // Tell scic to utilize this sgl pair for write DMA processing of
   // the SCSI UNMAP translation with the total number of bytes for this transfer
   sati_cb_sge_write(unmap_process_state->unmap_buffer_sgl_pair,
                     unmap_process_state->physical_unmap_buffer_low,
                     unmap_process_state->physical_unmap_buffer_high,
                     dsm_bytes);

   // Construct the unmap ATA request
   sati_unmap_construct(sequence,
                        scsi_io,
                        ata_io,
                        dsm_blocks);

   // Determine sequence next state based on whether there is more translation
   // to complete
   if (unmap_process_state->current_lba_count == 0)
   {
       // used for completion routine to determine if there is more processing
       sequence->state = SATI_SEQUENCE_STATE_FINAL;
   }
   // This requests has already translated the SGL, have SCIC skip SGL translataion
   return SATI_SUCCESS_SGL_TRANSLATED;
}

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

/**
 * @brief This method will handle termination of the
 *        SCSI unmap translation and frees previously allocated
 *        dma buffer.
 *
 * @return None
 */
void sati_unmap_terminate(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_UNMAP_PROCESSING_STATE_T * unmap_process_state;
   unmap_process_state = &sequence->command_specific_data.unmap_process_state;

   if (unmap_process_state->virtual_unmap_buffer != NULL)
   {
      sati_cb_free_dma_buffer(scsi_io, unmap_process_state->virtual_unmap_buffer);
      unmap_process_state->virtual_unmap_buffer = NULL;
   }
}

/**
 * @brief This method will translate the SCSI Unmap command
 *        into corresponding ATA commands.  Depending upon the capabilities
 *        supported by the target different ATA commands can be selected.
 *        Additionally, in some cases more than a single ATA command may
 *        be required.
 *
 * @return Indicate if the command translation succeeded.
 * @retval SATI_SUCCESS This is returned if the command translation was
 *         successful.
 * @retval SATI_COMPLETE This is returned if the command translation was
 *         successful and no ATA commands need to be set.
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if
 *         sense data has been created as a result of something specified
 *         in the parameter data fields.
 */
SATI_STATUS sati_unmap_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_STATUS status = SATI_FAILURE_CHECK_RESPONSE_DATA;
   SATI_UNMAP_PROCESSING_STATE_T * unmap_process_state;

   unmap_process_state = &sequence->command_specific_data.unmap_process_state;

   // Determine if this is the first step in the unmap sequence
   if ( sequence->state == SATI_SEQUENCE_STATE_INITIAL )
   {
       status = sati_unmap_initial_processing(sequence,scsi_io,ata_io);
       if (status != SATI_COMPLETE)
       {
          return status;
       }
   }
   // Translate the next portion of the UNMAP request
   return sati_unmap_process(sequence, scsi_io, ata_io);
}

/**
 * @brief This method will translate the ATA command register FIS
 *        response into an appropriate SCSI response for Unmap.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_response().
 *
 * @return Indicate if the response translation succeeded.
 * @retval SATI_SUCCESS This is returned if the command translation was
 *         successful.
 * @retval SATI_COMPLETE This is returned if the command translation was
 *         successful and no ATA commands need to be set.
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if
 *         sense data has been created as a result of something specified
 *         in the parameter data fields.
 */
SATI_STATUS sati_unmap_translate_response(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * register_fis = sati_cb_get_d2h_register_fis_address(ata_io);
   SATI_UNMAP_PROCESSING_STATE_T * unmap_process_state;
   SATI_STATUS sati_status = SATI_COMPLETE;

   unmap_process_state = &sequence->command_specific_data.unmap_process_state;

   if (sati_get_ata_status(register_fis) & ATA_STATUS_REG_ERROR_BIT)
   {
      sequence->state = SATI_SEQUENCE_STATE_FINAL;
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_ABORTED_COMMAND,
         SCSI_ASC_NO_ADDITIONAL_SENSE,
         SCSI_ASCQ_NO_ADDITIONAL_SENSE
      );
      // All done, terminate the translation
      sati_unmap_terminate(sequence, scsi_io, ata_io);
   }
   else
   {
      if (sequence->state != SATI_SEQUENCE_STATE_INCOMPLETE)
      {
          // All done, terminate the translation
          sati_unmap_terminate(sequence, scsi_io, ata_io);
      }
      else
      {
          // Still translating
          sati_status = SATI_SEQUENCE_STATE_INCOMPLETE;
      }
   }
   return sati_status;
}

#endif // !defined(DISABLE_SATI_UNMAP)
