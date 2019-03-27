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
* @brief This file contains the method implementations to translate
*        SCSI Write and Verify command based of the SAT spec.
*/

#if !defined(DISABLE_SATI_WRITE_AND_VERIFY)

#include <dev/isci/scil/sati_write_and_verify.h>
#include <dev/isci/scil/sati_write.h>
#include <dev/isci/scil/sati_verify.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/sati_util.h>

#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/intel_scsi.h>

/**
* @brief This function translates a SCSI Write and Verify 10 command
*        into both ATA write and ATA read verify commands. This
*        happens by passing the SCSI IO, ATA IO, and Sequence pointers
*        to both the sati_write_10_translate_command and the
*        sati_verify_10_translate_command.
*
* @return Indicate if the command translation succeeded.
* @retval SCI_SUCCESS This is returned if the command translation was
*         successful.
* @retval SATI_FAILURE_CHECK_RESPONSE_DATA is returned if there was
*         a problem with the translation of write long.
* @retval SATI_FAILURE is returned if there the sequence is out of
*         state for a sati_write_and_verify_10 translation.
*
*/
SATI_STATUS sati_write_and_verify_10_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_STATUS status;

   if(sequence->state == SATI_SEQUENCE_STATE_INITIAL)
   {
      status = sati_write_10_translate_command(sequence, scsi_io, ata_io);
      sequence->state = SATI_SEQUENCE_STATE_INCOMPLETE;
      sequence->is_translate_response_required = TRUE;
   }
   else if(sequence->state == SATI_SEQUENCE_STATE_INCOMPLETE)
   {
      status = sati_verify_10_translate_command(sequence, scsi_io, ata_io);
      sequence->state = SATI_SEQUENCE_STATE_AWAIT_RESPONSE;
   }
   else
   {
      //SATI sequence is in the wrong state
      return SATI_FAILURE;
   }

   sequence->type = SATI_SEQUENCE_WRITE_AND_VERIFY;
   return status;
}

/**
* @brief This function translates a SCSI Write and Verify 12 command
*        into both ATA write and ATA read verify commands. This
*        happens by passing the SCSI IO, ATA IO, and Sequence pointers
*        to both the sati_write_12_translate_command and the
*        sati_verify_12_translate_command.
*
* @return Indicate if the command translation succeeded.
* @retval SCI_SUCCESS This is returned if the command translation was
*         successful.
* @retval SATI_FAILURE_CHECK_RESPONSE_DATA is returned if there was
*         a problem with the translation of write long.
* @retval SATI_FAILURE is returned if there the sequence is out of
*         state for a sati_write_and_verify_12 translation.
*
*/
SATI_STATUS sati_write_and_verify_12_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_STATUS status;

   if(sequence->state == SATI_SEQUENCE_STATE_INITIAL)
   {
      status = sati_write_12_translate_command(sequence, scsi_io, ata_io);
      sequence->state = SATI_SEQUENCE_STATE_INCOMPLETE;
      sequence->is_translate_response_required = TRUE;
   }
   else if(sequence->state == SATI_SEQUENCE_STATE_INCOMPLETE)
   {
      status = sati_verify_12_translate_command(sequence, scsi_io, ata_io);
      sequence->state = SATI_SEQUENCE_STATE_AWAIT_RESPONSE;
   }
   else
   {
      //SATI sequence is in the wrong state
      return SATI_FAILURE;
   }

   sequence->type = SATI_SEQUENCE_WRITE_AND_VERIFY;
   return status;
}

/**
* @brief This function translates a SCSI Write and Verify 16 command
*        into both ATA write and ATA read verify commands. This
*        happens by passing the SCSI IO, ATA IO, and Sequence pointers
*        to both the sati_write_16_translate_command and the
*        sati_verify_16_translate_command.
*
* @return Indicate if the command translation succeeded.
* @retval SCI_SUCCESS This is returned if the command translation was
*         successful.
* @retval SATI_FAILURE_CHECK_RESPONSE_DATA is returned if there was
*         a problem with the translation of write long.
* @retval SATI_FAILURE is returned if there the sequence is out of
*         state for a sati_write_and_verify_16 translation.
*
*/
SATI_STATUS sati_write_and_verify_16_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   SATI_STATUS status;

   if(sequence->state == SATI_SEQUENCE_STATE_INITIAL)
   {
      status = sati_write_16_translate_command(sequence, scsi_io, ata_io);
      sequence->state = SATI_SEQUENCE_STATE_INCOMPLETE;
      sequence->is_translate_response_required = TRUE;
   }
   else if(sequence->state == SATI_SEQUENCE_STATE_INCOMPLETE)
   {
      status = sati_verify_16_translate_command(sequence, scsi_io, ata_io);
      sequence->state = SATI_SEQUENCE_STATE_AWAIT_RESPONSE;
   }
   else
   {
      //SATI sequence is in the wrong state
      return SATI_FAILURE;
   }

   sequence->type = SATI_SEQUENCE_WRITE_AND_VERIFY;
   return status;
}

/**
* @brief This function is the response to a sati_write_and_verify
         translation. Since no response translation is required
         this function will only check the sequence state and return
         status.
*
* @return Indicate if the command response translation succeeded.
* @retval SCI_COMPLETE This is returned if the command translation
          is successful and requires no more work.
* @retval SATI_SEQUENCE_INCOMPLETE This is returned if the command
          translation has finished sending the ATA Write command but
          still needs to complete the Verify portion.
* @retval SATI_FAILURE is returned if there the sequence is out of
*         state for a sati_write_and_verify translation.
*
*/
SATI_STATUS sati_write_and_verify_translate_response(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   if(sequence->state == SATI_SEQUENCE_STATE_INCOMPLETE)
   {
      return SATI_SEQUENCE_INCOMPLETE;
   }
   else if(sequence->state == SATI_SEQUENCE_STATE_AWAIT_RESPONSE)
   {
      sequence->state = SATI_SEQUENCE_STATE_FINAL;
      return SATI_COMPLETE;
   }

   return SATI_FAILURE;
}

#endif //!defined(DISABLE_SATI_WRITE_AND_VERIFY)
