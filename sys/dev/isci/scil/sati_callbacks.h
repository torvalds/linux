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
#ifndef _SATI_CALLBACKS_H_
#define _SATI_CALLBACKS_H_

/**
 * @file
 * @brief This file contains the default callback bindings for SATI.  These
 *        must be overridden by the SATI user to ensure successful operation.
 */

#include <dev/isci/scil/sati_types.h>
#include <dev/isci/scil/intel_sas.h>

#ifdef SATI_DEFAULT_DECLARATION

/**
 * @brief This callback method asks the user to provide the address for
 *        the command descriptor block (CDB) associated with this IO request.
 *
 * @param[in] scsi_io This parameter points to the user's IO request object
 *            It is a cookie that allows the user to provide the necessary
 *            information for this callback.
 *
 * @return This method returns the virtual address of the CDB.
 */
void * sati_cb_get_cdb_address(
   void * scsi_io
);

/**
 * @brief This callback method asks the user to provide the length of
 *        the command descriptor block (CDB) associated with this IO request.
 *
 * @param[in] scsi_io This parameter points to the user's IO request object.
 *            It is a cookie that allows the user to provide the necessary
 *            information for this callback.
 *
 * @return This method returns the length of the CDB.
 */
U32 sati_cb_get_cdb_length(
   void * scsi_io
);

/**
 * @brief This callback method asks the user to provide the data transfer
 *        direction of this IO request.
 *
 * @param[in] scsi_io This parameter points to the user's IO request object.
 *            It is a cookie that allows the user to provide the necessary
 *            information for this callback.
 * @param[in] io_direction to return
 * @return This method returns the length of the CDB.
 */
void sati_cb_get_data_direction(
   void * scsi_io,
   U8 * io_direction
);

/**
 * @brief This callback method sets a value into the data buffer associated
 *        with the supplied user SCSI IO request at the supplied byte offset.
 *
 * @note SATI does not manage the user scatter-gather-list.  As a result,
 *       the user must ensure that data is written according to the SGL.
 *
 * @param[in]  scsi_io This parameter specifies the user's SCSI IO request
 *             for which to set the data buffer byte.
 * @param[in]  byte_offset This parameter specifies the offset into the
 *             data buffer at which to set the value.
 * @param[in]  value This parameter specifies the new value to be set into
 *             the data buffer.
 *
 * @return none
 */
void sati_cb_set_data_byte(
   void * scsi_io,
   U32    byte_offset,
   U8     value
);

/**
 * @brief This callback method gets a value from the data buffer associated
 *        with the supplied user SCSI IO request at the supplied byte offset.
 *
 * @note SATI does not manage the user scatter-gather-list.  As a result,
 *       the user must ensure that data is written according to the SGL.
 *
 * @param[in]  scsi_io This parameter specifies the user's SCSI IO request
 *             for which to get the data buffer byte.
 * @param[in]  byte_offset This parameter specifies the offset into the
 *             data buffer at which to get the value.
 * @param[in]  value This parameter specifies the new value to be get into
 *             the data buffer.
 *
 * @return none
 */
void sati_cb_get_data_byte(
   void * scsi_io,
   U32    byte_offset,
   U8   * value
);

/**
 * @brief This callback method gets the task type for the SCSI task
 *        request.
 *
 * @param[in] scsi_task This parameter specifies the user's SCSI Task request.
 *            It is a cookie that allows the user to provide the necessary
 *            information for this callback.
 *
 * @return This method returns one of the enumeration values for
 *         SCSI_TASK_MGMT_REQUEST_CODES
 */
U8 sati_cb_get_task_function(
   void * scsi_task
);

#ifdef SATI_TRANSPORT_SUPPORTS_SAS
/**
 * @brief This callback method retrieves the address of the user's SSP
 *        response IU buffer.
 *
 * @param[in]  scsi_io This parameter specifies the user's SCSI IO request
 *             for which to retrieve the location of the response buffer to
 *             be written.
 *
 * @return This method returns the address of the response data buffer.
 */
void * sati_cb_get_response_iu_address(
   void * scsi_io
);

#else // SATI_TRANSPORT_SUPPORTS_SAS

/**
 * @brief This callback method retrieves the address of the user's sense data
 *        buffer.
 *
 * @param[in]  scsi_io This parameter specifies the user's SCSI IO request
 *             for which to retrieve the location of the sense buffer to
 *             be written.
 *
 * @return This method returns the address of the sense data buffer.
 */
U8* sati_cb_get_sense_data_address(
   void * scsi_io
);

/**
 * @brief This callback method retrieves the length of the user's sense data
 *        buffer.
 *
 * @param[in]  scsi_io This parameter specifies the user's SCSI IO request
 *             for which to retrieve the location of the sense buffer to
 *             be written.
 *
 * @return This method returns the length of the sense data buffer.
 */
U32 sati_cb_get_sense_data_length(
   void * scsi_io
);

/**
 * @brief This callback method sets the SCSI status to be associated with
 *        the supplied user's SCSI IO request.
 *
 * @param[in]  scsi_io This parameter specifies the user's SCSI IO request
 *             for which to set the SCSI status.
 * @param[in]  status This parameter specifies the SCSI status to be
 *             associated with the supplied user's SCSI IO request.
 *
 * @return none
 */
void sati_cb_set_scsi_status(
   void * scsi_io,
   U8     status
);

#endif // SATI_TRANSPORT_SUPPORTS_SAS

/**
 * @brief This method retrieves the ATA task file (register FIS) relating to
 *        the host to device command values.
 *
 * @param[in] ata_io This parameter specifies the user's ATA IO request
 *            from which to retrieve the h2d register FIS address.
 *
 * @return This method returns the address for the host to device register
 *         FIS.
 */
U8 * sati_cb_get_h2d_register_fis_address(
   void * ata_io
);

/**
 * @brief This method retrieves the ATA task file (register FIS) relating to
 *        the device to host response values.
 *
 * @param[in] ata_io This parameter specifies the user's ATA IO request
 *            from which to retrieve the d2h register FIS address.
 *
 * @return This method returns the address for the device to host register
 *         FIS.
 */
U8 * sati_cb_get_d2h_register_fis_address(
   void * ata_io
);

/**
 * @brief This method retrieves the address where the ATA data received
 *        from the device is stored.
 *
 * @param[in] ata_io This parameter specifies the user's ATA IO request
 *            from which to retrieve the received data address.
 *
 * @return This method returns the address for the data received from
 *         the remote device.
 */
void * sati_cb_get_ata_data_address(
   void * ata_io
);

/**
 * @brief This method allocates a DMA buffer
 *        that can be utilized for small (<=4K) DMA sequences.
 *        This is utilized to translate SCSI UNMAP requests.
 *
 * @param[in]  scsi_io This parameter specifies the user's SCSI IO request
 *             for which to set the SCSI status.
 * @param[in] length in bytes of the buffer to be allocated
 * @param[in] virtual address of the allocated DMA buffer.
 * @param[in] low 32 bits of the physical DMA address.
 * @param[in] high 32 bits of the physical DMA address.
 *
 * @return This method returns the virtual and physical address
 *         of the allocated DMA buffer.
 */
void sati_cb_allocate_dma_buffer(
   void *  scsi_io,
   U32     length,
   void ** virt_address,
   U32  *  phys_address_low,
   U32  *  phys_address_high
);

/**
 * @brief This method frees a previously allocated DMA buffer
 *
 * @param[in]  scsi_io This parameter specifies the user's SCSI IO request
 *             for which to set the SCSI status.
 * @param[in] address - write buffer address being freed
 *
 * @return This method returns the address for the data received from
 *         the remote device.
 */
void sati_cb_free_dma_buffer(
   void * scsi_io,
   void * virt_address
);

/**
 * @brief This method retrieves a pointer to the next scatter gather
 *        list element.
 *
 * @param[in] scsi_io This parameter specifies the user's SCSI IO request
 *            from which to retrieve the scatter gather list.
 * @param[in] ata_io This parameter specifies the user's ATA IO request
 *            from which to retrieve the scatter gather list.
 * @param[in] current_sge This parameter specifies the current SG element
 *            being pointed to.  If retrieving the first element,
 *            then this value should be NULL.
 * @param[in] next_sge This parameter is the returned SGL element
 *            based on current_sge.
 *
 * @return This method returns a pointer to the scatter gather element.
 */
void sati_cb_sgl_next_sge(
   void * scsi_io,
   void * ata_io,
   void * current_sge,
   void ** next_sge
);

/**
 * @brief This method will set the next scatter-gather elements address
 *        low field.
 *
 * @param[in] current_sge This parameter specifies the current SG element
 *            being pointed to.
 * @param[in] address_low This parameter specifies the lower 32-bits
 *            of address to be programmed into the SG element.
 * @param[in] address_high This parameter specifies the upper 32-bits
 *            of address to be programmed into the SG element.
 * @param[in] length This parameter specifies the number of bytes
 *            to be programmed into the SG element.
 *
 * @return none
 */
void sati_cb_sge_write(
   void * current_sge,
   U32    phys_address_low,
   U32    phys_address_high,
   U32    byte_length
);

/**
 * @brief This method will check to see if the translation requires
 *        a translation response callback.  Some translations need to be alerted on all
 *        failures so sequence cleanup can be completed for halting the translation.
 *
 * @param[in] the current SCIC request under going translation.
 *
 * @return TRUE A response callback will be required to complete this translation sequence.
 */
BOOL sati_cb_do_translate_response(
   void * request
);

/**
 * @brief This method retrieves the SAS address for the device associated
 *        with the supplied SCSI IO request.  This method assumes that the
 *        associated device is contained in a SAS Domain.
 *
 * @param[in]  scsi_io This parameter specifies the user's SCSI IO request
 *             for which to retrieve the SAS address of the device.
 * @param[out] sas_address This parameter specifies the SAS address memory
 *             to be contain the retrieved value.
 *
 * @return none
 */
void sati_cb_device_get_sas_address(
   void              * scsi_io,
   SCI_SAS_ADDRESS_T * sas_address
);

/**
 * @brief In this method the user is expected to log the supplied
 *        error information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is an error from the core.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void sati_cb_logger_log_error(
   void                * logger_object,
   U32                   log_object_mask,
   char                * log_message,
   ...
);

/**
 * @brief In this method the user is expected to log the supplied warning
 *        information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is a warning from the core.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void sati_cb_logger_log_warning(
   void                * logger_object,
   U32                   log_object_mask,
   char                * log_message,
   ...
);

/**
 * @brief In this method the user is expected to log the supplied debug
 *        information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is a debug message from the core.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void sati_cb_logger_log_info(
   void                * logger_object,
   U32                   log_object_mask,
   char                * log_message,
   ...
);

/**
 * @brief In this method the user is expected to log the supplied function
 *        trace information.  The user must be capable of handling variable
 *        length argument lists and should consider prepending the fact
 *        that this is a function trace (i.e. entry/exit) message from the
 *        core.
 *
 * @param[in]  logger_object This parameter specifies the logger object
 *             associated with this message.
 * @param[in]  log_object_mask This parameter specifies the log objects
 *             for which this message is being generated.
 * @param[in]  log_message This parameter specifies the message to be logged.
 *
 * @return none
 */
void sati_cb_logger_log_trace(
   void                * logger_object,
   U32                   log_object_mask,
   char                * log_message,
   ...
);

#include <dev/isci/scil/sati_callbacks_implementation.h>

#else // SATI_DEFAULT_DECLARATION

#include <dev/isci/scil/scif_sas_sati_binding.h>
#endif // SATI_DEFAULT_DECLARATION

#endif // _SATI_CALLBACKS_H_

