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
 * @brief This file contains the method implementations required to
 *        translate the SCSI abort task set command.
 */

#if !defined(DISABLE_SATI_TASK_MANAGEMENT)

#include <dev/isci/scil/sati_abort_task_set.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/sati_util.h>
#include <dev/isci/scil/sati.h>
#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/intel_scsi.h>
#include <dev/isci/scil/intel_sat.h>

//******************************************************************************
//* P U B L I C   M E T H O D S
//******************************************************************************

#if !defined(DISABLE_SATI_ABORT_TASK_SET)

/**
 * @brief This method will translate the abort task set SCSI task request into an
 *        ATA READ LOG EXT command. For more information on the parameters
 *        passed to this method, please reference sati_translate_command().
 *
 * @return Indicate if the command translation succeeded.
 * @retval SCI_SUCCESS This is returned if the command translation was
 *         successful.
 */
SATI_STATUS sati_abort_task_set_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * register_fis;

   //ATA Read Log Ext with log address set to 0x10
   sati_ata_read_log_ext_construct(
      ata_io,
      sequence,
      ATA_LOG_PAGE_NCQ_ERROR,
      sizeof(ATA_NCQ_COMMAND_ERROR_LOG_T)
   );

   register_fis = sati_cb_get_h2d_register_fis_address(ata_io);
   sati_set_sata_command_flag(register_fis);

   sequence->type                = SATI_SEQUENCE_ABORT_TASK_SET;
   sequence->state               = SATI_SEQUENCE_STATE_AWAIT_RESPONSE;

   return SATI_SUCCESS;
}

SATI_STATUS sati_abort_task_set_translate_data(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * ata_input_data,
   void                       * scsi_task
)
{
   ATA_NCQ_COMMAND_ERROR_LOG_T * log =
      (ATA_NCQ_COMMAND_ERROR_LOG_T *)ata_input_data;
   U8 tag_index;

   sequence->state = SATI_SEQUENCE_STATE_TRANSLATE_DATA;

   for (tag_index = 0; tag_index < 32; tag_index++)
   {
      void *        matching_command;
      SCI_IO_STATUS completion_status;
      sati_cb_device_get_request_by_ncq_tag(
         scsi_task,
         tag_index,
         matching_command
      );

      if (matching_command != NULL)
      {
         if (
              (log->ncq_tag == tag_index) &&
              (log->nq == 0) // nq==1 means a non-queued command
                             //  caused this failure
            )
         {
            sati_translate_error(sequence, matching_command, log->error);
            completion_status = SCI_IO_FAILURE_RESPONSE_VALID;

            if(sequence->state == SATI_SEQUENCE_STATE_READ_ERROR)
            {
               //Uncorrectable read error, return additional sense data
               sati_scsi_read_ncq_error_sense_construct(
                  sequence,
                  matching_command,
                  ata_input_data,
                  SCSI_STATUS_CHECK_CONDITION,
                  SCSI_SENSE_MEDIUM_ERROR,
                  SCSI_ASC_UNRECOVERED_READ_ERROR,
                  SCSI_ASCQ_UNRECOVERED_READ_ERROR
               );
            }
         }
         else
         {
            completion_status = SCI_IO_FAILURE_TERMINATED;
         }

         sati_cb_io_request_complete(matching_command, completion_status);
      }
   }

   sequence->state = SATI_SEQUENCE_STATE_FINAL;

   return SATI_COMPLETE;
}

#endif // !defined(DISABLE_SATI_ABORT_TASK_SET)

#endif // !defined(DISABLE_SATI_TASK_MANAGEMENT)

