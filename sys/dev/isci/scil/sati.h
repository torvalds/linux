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
 *
 * $FreeBSD$
 */
#ifndef _SATI_H_
#define _SATI_H_

/**
 * @file
 * @brief This file contains all of the interface methods, macros, structures
 *        that can be utilized by a user to perform SCSI-to-ATA Translation.
 *        SATI adheres to the www.t10.org SAT specification.
 *
 * For specific compliance information please refer to:
 * - sati_get_sat_compliance_version() and
 * - sati_get_sat_compliance_version_revision()
 *
 * For situations where compliance is not observed, the SATI will
 * return an error indication (most likely INVALID FIELD IN CDB sense).
 */

#include <dev/isci/scil/sati_types.h>
#include <dev/isci/scil/sati_translator_sequence.h>

/**
 * @brief This method will be called by the core to indicate the start of a
 *        new translation sequence.  Set up the initial sequence state.
 *
 * @return None
 */
void sati_sequence_construct(
   SATI_TRANSLATOR_SEQUENCE_T * sequence
);

/**
 * @brief This method will be called by the core to request the current translation
 *        sequence be terminated.
 *
 * @param[in]  sequence This parameter specifies the sequence
 *             data associated with the translation.
 * @param[in,out] scsi_io This parameter specifies the user's SCSI IO request
 *                object.  SATI expects that the user can access the SCSI CDB,
 *                response, and data from this pointer.  For example, if there
 *                is a failure in translation resulting in sense data, then
 *                SATI will call sati_cb_set_status() and pass the scsi_io
 *                pointer as a parameter.
 * @param[out] ata_io This parameter specifies the location of the
 *             ATA register FIS into which the translator can write the
 *             resultant ATA command if translation is successful.  This
 *             parameter is passed back to the user through the
 *             SATI_SATA_CALLBACKS when it is necessary to write fields
 *             in the ata_io.
 *
 * @return None
 */
void sati_sequence_terminate(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
);

/**
 * @brief This method translates the supplied SCSI command into a
 *        corresponding ATA command.
 *
 * @param[in]  sequence This parameter specifies the sequence
 *             data associated with the translation.
 * @param[in]  sati_device This parameter specifies the remote device
 *             for which the translated request is destined.
 * @param[in,out] scsi_io This parameter specifies the user's SCSI IO request
 *                object.  SATI expects that the user can access the SCSI CDB,
 *                response, and data from this pointer.  For example, if there
 *                is a failure in translation resulting in sense data, then
 *                SATI will call sati_cb_set_status() and pass the scsi_io
 *                pointer as a parameter.
 * @param[out] ata_io This parameter specifies the location of the
 *             ATA register FIS into which the translator can write the
 *             resultant ATA command if translation is successful.  This
 *             parameter is passed back to the user through the
 *             SATI_SATA_CALLBACKS when it is necessary to write fields
 *             in the ata_io.
 *
 * @return Indicate if the translation was successful.
 * @retval SATI_SUCCESS
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA
 */
SATI_STATUS sati_translate_command(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   SATI_DEVICE_T              * sati_device,
   void                       * scsi_io,
   void                       * ata_io
);

/**
 * @brief This method translates the supplied SCSI task management request
 *        into a corresponding ATA command/control FIS.
 *
 * @param[in]  sequence This parameter specifies the sequence
 *             data associated with the translation.
 * @param[in]  sati_device This parameter specifies the remote device
 *             for which the translated request is destined.
 * @param[in,out] scsi_io This parameter specifies the user's SCSI IO request
 *                object.  SATI expects that the user can access the SCSI CDB,
 *                response, and data from this pointer.  For example, if there
 *                is a failure in translation resulting in sense data, then
 *                SATI will call sati_cb_set_status() and pass the scsi_io
 *                pointer as a parameter.
 * @param[out] ata_io This parameter specifies the location of the
 *             ATA register FIS into which the translator can write the
 *             resultant ATA command if translation is successful.  This
 *             parameter is passed back to the user through the
 *             SATI_SATA_CALLBACKS when it is necessary to write fields
 *             in the ata_io.
 *
 * @return Indicate if the translation was successful.
 * @retval SATI_SUCCESS
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA
 */
SATI_STATUS sati_translate_task_management(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   SATI_DEVICE_T              * sati_device,
   void                       * scsi_task,
   void                       * ata_io
);

/**
 * @brief This method translates the supplied ATA response into the
 *        corresponding SCSI command response.
 *
 * @param[in]  sequence This parameter specifies the sequence
 *             data associated with the translation.
 * @param[out] scsi_io This parameter specifies the user's SCSI IO request
 *             object.  SATI expects that the user can access the SCSI CDB,
 *             response, and data from this pointer.  For example, if there
 *             is a failure in translation resulting in sense data, then
 *             SATI will call sati_cb_set_status() and pass the scsi_io
 *             pointer as a parameter.
 * @param[out] ata_io This parameter specifies the location of the
 *             ATA IO request (e.g. register FIS, PIO Setup etc.) from which
 *             the translator can read the received ATA status and error
 *             fields.
 *
 * @return Indicate if the translation was successful.
 * @retval SATI_SUCCESS
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA
 */
SATI_STATUS sati_translate_command_response(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
);

/**
 * @brief This method translates the supplied ATA response into the
 *        corresponding SCSI task response.
 *
 * @param[in]  sequence This parameter specifies the sequence
 *             data associated with the translation.
 * @param[out] scsi_io This parameter specifies the user's SCSI IO request
 *             object.  SATI expects that the user can access the SCSI CDB,
 *             response, and data from this pointer.  For example, if there
 *             is a failure in translation resulting in sense data, then
 *             SATI will call sati_cb_set_status() and pass the scsi_io
 *             pointer as a parameter.
 * @param[out] ata_io This parameter specifies the location of the
 *             ATA IO request (e.g. register FIS, PIO Setup etc.) from which
 *             the translator can read the received ATA status and error
 *             fields.
 *
 * @return Indicate if the translation was successful.
 * @retval SATI_FAILURE_CHECK_RESPONSE_DATA
 */
SATI_STATUS sati_translate_task_response(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   void                       * ata_io
);

/**
 * @brief This method simply returns the SAT major compliance version number
 *        for which the implementation is compliant.
 *
 * @return Return the specification identifier with which this translator
 *         is compliant.
 */
U32 sati_get_sat_compliance_version(
   void
);

/**
 * @brief This method simply returns the SAT version revision identifier
 *        for which the implementation is compliant.
 *
 * @return Return the specification revision identifier with which this
 *         translator is compliant.
 */
U32 sati_get_sat_compliance_version_revision(
   void
);

/**
* @brief This method returns the number of data bytes written
*        by the translation sequence.
*
* @param[in] sequence This parameter specifies the sequence data
*            associated with the translation.
*
* @return Return the U16 number_data_bytes_set in the
*         SATI_TRANSLATOR_SEQUENCE.
*/
U16 sati_get_number_data_bytes_set(
   SATI_TRANSLATOR_SEQUENCE_T * sequence
);

void sati_translate_error(
   SATI_TRANSLATOR_SEQUENCE_T * sequence,
   void                       * scsi_io,
   U8                           error
);

#endif // _SATI_H_

