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
 *        translate the SCSI test unit ready command.
 */

#if !defined(DISABLE_SATI_TEST_UNIT_READY)

#include <dev/isci/scil/sati_test_unit_ready.h>
#include <dev/isci/scil/sati_util.h>
#include <dev/isci/scil/sati_callbacks.h>
#include <dev/isci/scil/intel_ata.h>
#include <dev/isci/scil/intel_scsi.h>

/**
 * @brief This method will translate the test unit ready SCSI command into
 *        an ATA CHECK POWER MODE command.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_command().
 *
 * @return Indicate if the command translation succeeded.
 * @retval SCI_SUCCESS This is returned if the command translation was
 *         successful.
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA This value is returned if the
 *         LBA field is not 0, the PMI bit is not 0.
 */
SATI_STATUS sati_test_unit_ready_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   /**
    * SAT dictates:
    * - the device should be in a state to receive commands
    * - a stopped device should cause sense data.
    * - a format unit in progresss should cause sense data.
    * - a self-test in progress should cause sense data.
    * - a device fault occurred on previous request should cause sense data.
    * - handling the removable media feature set isn't supported according to
    *   SAT specifications.
    */
   if (sequence->device->state == SATI_DEVICE_STATE_STOPPED)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_NOT_READY,
         SCSI_ASC_INITIALIZING_COMMAND_REQUIRED,
         SCSI_ASCQ_INITIALIZING_COMMAND_REQUIRED
      );
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }
   else if (sequence->device->state
            == SATI_DEVICE_STATE_SELF_TEST_IN_PROGRESS)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_NOT_READY,
         SCSI_ASC_LUN_SELF_TEST_IN_PROGRESS,
         SCSI_ASCQ_LUN_SELF_TEST_IN_PROGRESS
      );
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }
   else if (sequence->device->state
            == SATI_DEVICE_STATE_FORMAT_UNIT_IN_PROGRESS)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_NOT_READY,
         SCSI_ASC_LUN_FORMAT_IN_PROGRESS,
         SCSI_ASCQ_LUN_FORMAT_IN_PROGRESS
      );
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }

   // The CDB is properly formed and the device is ready.
   sequence->type = SATI_SEQUENCE_TEST_UNIT_READY;

   sati_ata_check_power_mode_construct(ata_io, sequence);
   return SATI_SUCCESS;
}

/**
 * @brief This method will translate the ATA CHECK POWER MODE register FIS
 *        response into an appropriate SCSI response.
 *        For more information on the parameters passed to this method,
 *        please reference sati_translate_response().
 *
 * @return Indicate if the response translation succeeded.
 * @retval SCI_SUCCESS This is returned if the data translation was
 *         successful.
 */
SATI_STATUS sati_test_unit_ready_translate_response(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
)
{
   U8 * register_fis = sati_cb_get_d2h_register_fis_address(ata_io);

   /**
    * SAT dictates:
    * - If the ATA CHECK POWER MODE command returns an error, then
    *   return sense data indicating the LOGICAL UNIT DOES NOT RESPONSE
    *   TO SELECTION.
    * - All other cases are considered successful.
    */
   if (sati_get_ata_status(register_fis) & ATA_STATUS_REG_ERROR_BIT)
   {
      sati_scsi_sense_data_construct(
         sequence,
         scsi_io,
         SCSI_STATUS_CHECK_CONDITION,
         SCSI_SENSE_NOT_READY,
         SCSI_ASC_LUN_NOT_RESPOND_TO_SELECTION,
         SCSI_ASCQ_LUN_NOT_RESPOND_TO_SELECTION
      );
      return SATI_FAILURE_CHECK_RESPONSE_DATA;
   }

   return SATI_COMPLETE;
}

#endif // !defined(DISABLE_SATI_TEST_UNIT_READY)

