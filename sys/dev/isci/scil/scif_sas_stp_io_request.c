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
 * @brief This file contains the method implementations for the
 *        SCIF_SAS_STP_IO_REQUEST object.  The contents will implement
 *        SATA/STP specific functionality.
 */

#include <dev/isci/scil/scif_sas_stp_io_request.h>
#include <dev/isci/scil/scif_sas_stp_remote_device.h>
#include <dev/isci/scil/scif_sas_logger.h>
#include <dev/isci/scil/scif_sas_controller.h>

#include <dev/isci/scil/sci_status.h>
#include <dev/isci/scil/scic_io_request.h>

#include <dev/isci/scil/sati.h>
#include <dev/isci/scil/sati_atapi.h>
#include <dev/isci/scil/intel_sat.h>
#include <dev/isci/scil/sati_util.h>
#include <dev/isci/scil/sati_callbacks.h>

//******************************************************************************
// P R I V A T E   M E T H O D S
//******************************************************************************

/**
 * @brief This method provides SATA/STP CONSTRUCTED state specific handling
 *        for when the user attempts to start the supplied IO request.  It
 *        will allocate NCQ tags if necessary.
 *
 * @param[in] io_request This parameter specifies the IO request object
 *            to be started.
 *
 * @return This method returns a value indicating if the IO request was
 *         successfully started or not.
 * @retval SCI_SUCCESS This return value indicates successful starting
 *         of the IO request.
 */
static
SCI_STATUS scif_sas_stp_io_request_constructed_start_handler(
   SCI_BASE_REQUEST_T * io_request
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T *) io_request;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(io_request),
      SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_stp_io_request_constructed_start_handler(0x%x) enter\n",
      io_request
   ));

   if (fw_io->parent.stp.sequence.protocol == SAT_PROTOCOL_FPDMA)
   {
      SATA_FIS_REG_H2D_T * fis;

      // For NCQ, we need to attempt to allocate an available tag.
      fw_io->parent.stp.ncq_tag = scif_sas_stp_remote_device_allocate_ncq_tag(
                                     fw_io->parent.device
                                  );

      if (fw_io->parent.stp.ncq_tag == SCIF_SAS_INVALID_NCQ_TAG)
         return SCI_FAILURE_NO_NCQ_TAG_AVAILABLE;

      // Set the NCQ tag in the host to device register FIS (upper 5 bits
      // of the 8-bit sector count register).
      fis = scic_stp_io_request_get_h2d_reg_address(fw_io->parent.core_object);
      fis->sector_count = (fw_io->parent.stp.ncq_tag << 3);

      // The Core also requires that we inform it separately regarding the
      // NCQ tag for this IO.
      scic_stp_io_request_set_ncq_tag(
         fw_io->parent.core_object, fw_io->parent.stp.ncq_tag
      );
   }

   return SCI_SUCCESS;
}

/**
 * @brief This method provides SATA/STP CONSTRUCTED state specific handling
 *        for when the user attempts to complete the supplied IO request.
 *        This method will be invoked in the event the call to start the
 *        core IO request fails for some reason.  In this situation, the
 *        NCQ tag will be freed.
 *
 * @param[in] io_request This parameter specifies the IO request object
 *            to be started.
 *
 * @return This method returns a value indicating if the IO request was
 *         successfully started or not.
 * @retval SCI_SUCCESS This return value indicates successful starting
 *         of the IO request.
 */
static
SCI_STATUS scif_sas_stp_io_request_constructed_complete_handler(
   SCI_BASE_REQUEST_T * io_request
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T *) io_request;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(io_request),
      SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_stp_io_request_constructed_complete_handler(0x%x) enter\n",
      io_request
   ));

   if (fw_io->parent.stp.sequence.protocol == SAT_PROTOCOL_FPDMA)
   {
      // For NCQ, we need to return the tag back to the free pool.
      if (fw_io->parent.stp.ncq_tag != SCIF_SAS_INVALID_NCQ_TAG)
         scif_sas_stp_remote_device_free_ncq_tag(
            fw_io->parent.device, fw_io->parent.stp.ncq_tag
         );
   }

   sati_sequence_terminate(&fw_io->parent.stp.sequence, fw_io, fw_io);

   return SCI_SUCCESS;
}
/**
 * @brief This method provides SATA/STP STARTED state specific handling for
 *        when the user attempts to complete the supplied IO request.
 *        It will perform data/response translation and free NCQ tags
 *        if necessary.
 *
 * @param[in] io_request This parameter specifies the IO request object
 *            to be started.
 *
 * @return This method returns a value indicating if the IO request was
 *         successfully completed or not.
 */
static
SCI_STATUS scif_sas_stp_core_cb_io_request_complete_handler(
   SCIF_SAS_CONTROLLER_T    * fw_controller,
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCIF_SAS_REQUEST_T       * fw_request,
   SCI_STATUS               * completion_status
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T *) fw_request;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_stp_core_cb_io_request_complete_handler(0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      fw_controller, fw_device, fw_request, *completion_status
   ));

   if (fw_io->parent.stp.sequence.protocol == SAT_PROTOCOL_FPDMA)
      scif_sas_stp_remote_device_free_ncq_tag(
         fw_request->device, fw_io->parent.stp.ncq_tag
      );

   // Translating the response is only necessary if:
   // - some sort of error occurred resulting in having the error bit
   //   set in the ATA status register and values to decode in the
   //   ATA error register.
   // - the command returns information in the register FIS itself,
   //   which requires translation.
   // - the request completed ok but the sequence requires a callback
   //   to possibly continue the translation
   if ((*completion_status == SCI_FAILURE_IO_RESPONSE_VALID) ||
       ((sati_cb_do_translate_response(fw_request)) &&
        (*completion_status != SCI_FAILURE_IO_TERMINATED)))
   {
      SATI_STATUS sati_status = sati_translate_command_response(
                                   &fw_io->parent.stp.sequence, fw_io, fw_io
                                );
      if (sati_status == SATI_COMPLETE)
         *completion_status = SCI_SUCCESS;
      else if (sati_status == SATI_FAILURE_CHECK_RESPONSE_DATA)
         *completion_status = SCI_FAILURE_IO_RESPONSE_VALID;
      else if (sati_status == SATI_SEQUENCE_INCOMPLETE)
      {
         // The translation indicates that additional SATA requests are
         // necessary to finish the original SCSI request.  As a result,
         // do not complete the IO and begin the next stage of the
         // translation.
         return SCI_WARNING_SEQUENCE_INCOMPLETE;
      }
      else if (sati_status == SATI_COMPLETE_IO_DONE_EARLY)
         *completion_status = SCI_SUCCESS_IO_DONE_EARLY;
      else
      {
         // Something unexpected occurred during translation.  Fail the
         // IO request to the user.
         *completion_status = SCI_FAILURE;
      }
   }
   else if (*completion_status != SCI_SUCCESS)
   {
      SCIF_LOG_INFO((
         sci_base_object_get_logger(fw_controller),
         SCIF_LOG_OBJECT_IO_REQUEST,
         "Sequence Terminated(0x%x, 0x%x, 0x%x)\n",
         fw_controller, fw_device, fw_request
      ));

      sati_sequence_terminate(&fw_io->parent.stp.sequence, fw_io, fw_io);
   }

   return SCI_SUCCESS;
}

#if !defined(DISABLE_ATAPI)
/**
 * @brief This method provides STP PACKET io request STARTED state specific handling for
 *        when the user attempts to complete the supplied IO request.
 *        It will perform data/response translation.
 *
 * @param[in] io_request This parameter specifies the IO request object
 *            to be started.
 *
 * @return This method returns a value indicating if the IO request was
 *         successfully completed or not.
 */
static
SCI_STATUS scif_sas_stp_core_cb_packet_io_request_complete_handler(
   SCIF_SAS_CONTROLLER_T    * fw_controller,
   SCIF_SAS_REMOTE_DEVICE_T * fw_device,
   SCIF_SAS_REQUEST_T       * fw_request,
   SCI_STATUS               * completion_status
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T *) fw_request;
   SATI_STATUS sati_status;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_controller),
      SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_stp_packet_core_cb_io_request_complete_handler(0x%x, 0x%x, 0x%x, 0x%x) enter\n",
      fw_controller, fw_device, fw_request, *completion_status
   ));

   if (*completion_status == SCI_FAILURE_IO_RESPONSE_VALID)
   {
      sati_status = sati_atapi_translate_command_response(
                       &fw_io->parent.stp.sequence, fw_io, fw_io
                    );

      if (sati_status == SATI_COMPLETE)
         *completion_status = SCI_SUCCESS;
      else if (sati_status == SATI_FAILURE_CHECK_RESPONSE_DATA)
         *completion_status = SCI_FAILURE_IO_RESPONSE_VALID;
      else if (sati_status == SATI_SEQUENCE_INCOMPLETE)
      {
         // The translation indicates that additional REQUEST SENSE command is
         // necessary to finish the original SCSI request.  As a result,
         // do not complete the IO and begin the next stage of the IO.
         return SCI_WARNING_SEQUENCE_INCOMPLETE;
      }
      else
      {
         // Something unexpected occurred during translation.  Fail the
         // IO request to the user.
         *completion_status = SCI_FAILURE;
      }
   }
   else if (*completion_status == SCI_SUCCESS &&
        fw_request->stp.sequence.state == SATI_SEQUENCE_STATE_INCOMPLETE)
   {
      //The internal Request Sense command is completed successfully.
      sati_atapi_translate_request_sense_response(
         &fw_io->parent.stp.sequence, fw_io, fw_io
      );

      *completion_status = SCI_FAILURE_IO_RESPONSE_VALID;
   }

   return SCI_SUCCESS;
}
#endif // !defined(DISABLE_ATAPI)

//******************************************************************************
// P R O T E C T E D   M E T H O D S
//******************************************************************************

/**
 * @brief This method will construct the SATA/STP specific IO request
 *        object utilizing the SATI.
 *
 * @pre The scif_sas_request_construct() method should be invoked before
 *      calling this method.
 *
 * @param[in,out] stp_io_request This parameter specifies the stp_io_request
 *                to be constructed.
 *
 * @return Indicate if the construction was successful.
 * @return SCI_FAILURE_NO_NCQ_TAG_AVAILABLE
 * @return SCI_SUCCESS_IO_COMPLETE_BEFORE_START
 * @return SCI_FAILURE_IO_RESPONSE_VALID
 * @return SCI_FAILURE This return value indicates a change in the translator
 *         where a new return code has been given, but is not yet understood
 *         by this routine.
 */
SCI_STATUS scif_sas_stp_io_request_construct(
   SCIF_SAS_IO_REQUEST_T * fw_io
)
{
   SATI_STATUS                sati_status;
   SCI_STATUS                 sci_status = SCI_FAILURE;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device  = fw_io->parent.device;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_io),
      SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_stp_io_request_construct(0x%x) enter\n",
      fw_io
   ));

   // The translator will indirectly invoke core methods to set the fields
   // of the ATA register FIS inside of this method.
   sati_status = sati_translate_command(
                    &fw_io->parent.stp.sequence,
                    &fw_device->protocol_device.stp_device.sati_device,
                    fw_io,
                    fw_io
                 );

   if (sati_status == SATI_SUCCESS)
   {
      // Allow the core to finish construction of the IO request.
      sci_status = scic_io_request_construct_basic_sata(fw_io->parent.core_object);
      fw_io->parent.state_handlers = &stp_io_request_constructed_handlers;
      fw_io->parent.protocol_complete_handler
         = scif_sas_stp_core_cb_io_request_complete_handler;
   }
   else if (sati_status == SATI_SUCCESS_SGL_TRANSLATED)
   {
      SCIC_IO_SATA_PARAMETERS_T parms;
      parms.do_translate_sgl = FALSE;

      // The translation actually already caused translation of the
      // scatter gather list.  So, call into the core through an API
      // that will not attempt to translate the SGL.
      scic_io_request_construct_advanced_sata(
                      fw_io->parent.core_object, &parms
                   );
      fw_io->parent.state_handlers = &stp_io_request_constructed_handlers;
      fw_io->parent.protocol_complete_handler
         = scif_sas_stp_core_cb_io_request_complete_handler;
      // Done with translation
      sci_status = SCI_SUCCESS;
   }
   else if (sati_status == SATI_COMPLETE)
      sci_status = SCI_SUCCESS_IO_COMPLETE_BEFORE_START;
   else if (sati_status == SATI_FAILURE_CHECK_RESPONSE_DATA)
      sci_status = SCI_FAILURE_IO_RESPONSE_VALID;
   else
   {
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_io),
         SCIF_LOG_OBJECT_IO_REQUEST,
         "Unexpected SAT translation failure 0x%x\n",
         fw_io
      ));
   }

   return sci_status;
}


#if !defined(DISABLE_ATAPI)
/**
 * @brief This method will construct the STP PACKET protocol specific IO
 *        request object.
 *
 * @pre The scif_sas_request_construct() method should be invoked before
 *      calling this method.
 *
 * @param[in,out] fw_io This parameter specifies the stp packet io request
 *                to be constructed.
 *
 * @return Indicate if the construction was successful.
 * @return SCI_SUCCESS_IO_COMPLETE_BEFORE_START
 * @return SCI_FAILURE_IO_RESPONSE_VALID
 * @return SCI_FAILURE This return value indicates a change in the translator
 *         where a new return code has been given, but is not yet understood
 *         by this routine.
 */
SCI_STATUS scif_sas_stp_packet_io_request_construct(
   SCIF_SAS_IO_REQUEST_T * fw_io
)
{
   SATI_STATUS                sati_status;
   SCI_STATUS                 sci_status = SCI_FAILURE;
   SCIF_SAS_REMOTE_DEVICE_T * fw_device  = fw_io->parent.device;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_io),
      SCIF_LOG_OBJECT_IO_REQUEST,
      "scif_sas_stp_packet_io_request_construct(0x%x) enter\n",
      fw_io
   ));

   sati_status = sati_atapi_translate_command(
                    &fw_io->parent.stp.sequence,
                    &fw_device->protocol_device.stp_device.sati_device,
                    fw_io,
                    fw_io
                 );

   if (sati_status == SATI_SUCCESS)
   {
      // Allow the core to finish construction of the IO request.
      sci_status = scic_io_request_construct_basic_sata(fw_io->parent.core_object);

      fw_io->parent.protocol_complete_handler
         = scif_sas_stp_core_cb_packet_io_request_complete_handler;
   }
   else if (sati_status == SATI_COMPLETE)
      sci_status = SCI_SUCCESS_IO_COMPLETE_BEFORE_START;
   else if (sati_status == SATI_FAILURE_CHECK_RESPONSE_DATA)
      sci_status = SCI_FAILURE_IO_RESPONSE_VALID;
   else
   {
      SCIF_LOG_ERROR((
         sci_base_object_get_logger(fw_io),
         SCIF_LOG_OBJECT_IO_REQUEST,
         "Unexpected SAT ATAPI translation failure 0x%x\n",
         fw_io
      ));
   }

   return sci_status;
}
#endif


#if !defined(DISABLE_ATAPI)
/**
 * @brief This method will get the number of bytes transferred in an packet IO.
 *
 * @param[in] fw_io This parameter specifies the stp packet io request whose
 *                     actual transferred length is to be retrieved.
 *
 * @return Actual length of transferred data.
 */
U32 scif_sas_stp_packet_io_request_get_number_of_bytes_transferred(
   SCIF_SAS_IO_REQUEST_T * fw_io
)
{
   SCI_IO_REQUEST_HANDLE_T scic_io = scif_io_request_get_scic_handle(fw_io);
   SCI_IO_STATUS io_status = scic_request_get_sci_status (scic_io);
   U32 actual_data_length;

   if (io_status == SCI_IO_FAILURE_RESPONSE_VALID)
       actual_data_length = 0;
   else if (io_status == SCI_IO_SUCCESS_IO_DONE_EARLY)
   {
      actual_data_length = sati_atapi_translate_number_of_bytes_transferred(
         &fw_io->parent.stp.sequence, fw_io, fw_io);

      if (actual_data_length == 0)
         actual_data_length =
            scic_io_request_get_number_of_bytes_transferred(scic_io);
   }
   else
      actual_data_length =
         scic_io_request_get_number_of_bytes_transferred(scic_io);

   return actual_data_length;
}
#endif


//******************************************************************************
// P U B L I C   M E T H O D S
//******************************************************************************

BOOL scic_cb_io_request_do_copy_rx_frames(
   void * scic_user_io_request
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T*) scic_user_io_request;

   SCIF_LOG_TRACE((
      sci_base_object_get_logger(fw_io),
      SCIF_LOG_OBJECT_IO_REQUEST,
      "scic_cb_io_request_do_copy_rx_frames(0x%x) enter\n",
      fw_io
   ));

   // If the translation was a PIO DATA IN (i.e. read) and the request
   // was actually a READ payload operation, then copy the data, since
   // there will be SGL space allocated for the transfer.
   if (fw_io->parent.stp.sequence.protocol == SAT_PROTOCOL_PIO_DATA_IN)
   {
      if (
            (fw_io->parent.stp.sequence.type ==  SATI_SEQUENCE_ATA_PASSTHROUGH_12)
         || (fw_io->parent.stp.sequence.type ==  SATI_SEQUENCE_ATA_PASSTHROUGH_16)
         || (
               (fw_io->parent.stp.sequence.type >= SATI_SEQUENCE_TYPE_READ_MIN)
            && (fw_io->parent.stp.sequence.type <= SATI_SEQUENCE_TYPE_READ_MAX)
            )
         )
      {
           SCIF_LOG_TRACE((
                 sci_base_object_get_logger(fw_io),
                 SCIF_LOG_OBJECT_IO_REQUEST,
                 "scic_cb_io_request_do_copy_rx_frames(0x%x) TRUE\n",
                 fw_io
              ));
           return TRUE;
      }
   }

   // For all other requests we leave the data in the core buffers.
   // This allows the translation to translate without having to have
   // separate space allocated into which to copy the data.
   return FALSE;
}

// ---------------------------------------------------------------------------

U8 scic_cb_request_get_sat_protocol(
   void * scic_user_io_request
)
{
   SCIF_SAS_IO_REQUEST_T * fw_io = (SCIF_SAS_IO_REQUEST_T*) scic_user_io_request;

   return fw_io->parent.stp.sequence.protocol;
}

U8 *scic_cb_io_request_get_virtual_address_from_sgl(
   void * scic_user_io_request,
   U32    byte_offset
)
{
   SCIF_SAS_REQUEST_T *fw_request =
      (SCIF_SAS_REQUEST_T *) sci_object_get_association(scic_user_io_request);

   return scif_cb_io_request_get_virtual_address_from_sgl(
             sci_object_get_association(fw_request),
             byte_offset
          );
}

#ifdef ENABLE_OSSL_COPY_BUFFER
void scic_cb_io_request_copy_buffer(
   void * scic_user_io_request,
   U8    *source_addr,
   U32   offset,
   U32   length
)
{
   SCIF_SAS_REQUEST_T *fw_request =
      (SCIF_SAS_REQUEST_T *)sci_object_get_association(scic_user_io_request);

   return scif_cb_io_request_copy_buffer(
             sci_object_get_association(fw_request),
             source_addr,
             offset,
             length
          );
}
#endif
// ---------------------------------------------------------------------------

SCI_BASE_REQUEST_STATE_HANDLER_T stp_io_request_constructed_handlers =
{
   scif_sas_stp_io_request_constructed_start_handler,
   scif_sas_io_request_constructed_abort_handler,
   scif_sas_stp_io_request_constructed_complete_handler,
   scif_sas_io_request_default_destruct_handler
};

