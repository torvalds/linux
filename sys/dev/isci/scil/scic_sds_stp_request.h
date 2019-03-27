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
#ifndef _SCIC_SDS_STP_REQUEST_T_
#define _SCIC_SDS_STP_REQUEST_T_

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/intel_sata.h>
#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/scic_sds_request.h>

/**
 * @struct
 *
 * @brief This structure represents the additional information that is
 *        required to handle SATA PIO requests.
 */
typedef struct SCIC_SDS_STP_REQUEST
{
   SCIC_SDS_REQUEST_T parent;

   SATA_FIS_REG_D2H_T d2h_reg_fis;

   union
   {
      U32 ncq;

      U32 udma;

      struct
      {
         /**
          * Total transfer for the entire PIO request recorded at request construction
          * time.
          *
          * @todo Should we just decrement this value for each byte of data transitted
          *       or received to elemenate the current_transfer_bytes field?
          */
         U32 total_transfer_bytes;

         /**
          * Total number of bytes received/transmitted in data frames since the start
          * of the IO request.  At the end of the IO request this should equal the
          * total_transfer_bytes.
          */
         U32 current_transfer_bytes;

         /**
          * The number of bytes requested in the in the PIO setup.
          */
         U32 pio_transfer_bytes;

         /**
          * PIO Setup ending status value to tell us if we need to wait for another FIS
          * or if the transfer is complete. On the receipt of a D2H FIS this will be
          * the status field of that FIS.
          */
         U8  ending_status;

         /**
          * On receipt of a D2H FIS this will be the ending error field if the
          * ending_status has the SATA_STATUS_ERR bit set.
          */
         U8  ending_error;

         /**
          * Protocol Type. This is filled in by core during IO Request construction type.
          */
         U8  sat_protocol;

         /**
         * This field keeps track of sgl pair to be retrieved from OS memory for processing.
         */
         U8  sgl_pair_index;

         struct
         {
            SCU_SGL_ELEMENT_PAIR_T * sgl_pair;
            U8                       sgl_set;
            U32                      sgl_offset;
         } request_current;
      } pio;

      struct
      {
         /**
          * The number of bytes requested in the PIO setup before CDB data frame.
          */
         U32 device_preferred_cdb_length;
      } packet;
   } type;

} SCIC_SDS_STP_REQUEST_T;

/**
 * @enum SCIC_SDS_STP_REQUEST_STARTED_UDMA_SUBSTATES
 *
 * @brief This enumeration depicts the various sub-states associated with
 *        a SATA/STP UDMA protocol operation.
 */
enum SCIC_SDS_STP_REQUEST_STARTED_UDMA_SUBSTATES
{
   SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_TC_COMPLETION_SUBSTATE,
   SCIC_SDS_STP_REQUEST_STARTED_UDMA_AWAIT_D2H_REG_FIS_SUBSTATE,

   SCIC_SDS_STP_REQUEST_STARTED_UDMA_MAX_SUBSTATES
};

/**
 * @enum SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_SUBSTATES
 *
 * @brief This enumeration depicts the various sub-states associated with
 *        a SATA/STP non-data protocol operation.
 */
enum SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_SUBSTATES
{
   SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_H2D_COMPLETION_SUBSTATE,
   SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_AWAIT_D2H_SUBSTATE,
   SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_MAX_SUBSTATES
};

/**
 * @enum SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_SUBSTATES
 *
 * @brief THis enumeration depicts the various sub-states associated with a
 *        SATA/STP soft reset operation.
 */
enum SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_SUBSTATES
{
   SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_ASSERTED_COMPLETION_SUBSTATE,
   SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_H2D_DIAGNOSTIC_COMPLETION_SUBSTATE,
   SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_AWAIT_D2H_RESPONSE_FRAME_SUBSTATE,

   SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_MAX_SUBSTATES
};

extern SCIC_SDS_IO_REQUEST_STATE_HANDLER_T
   scic_sds_stp_request_started_udma_substate_handler_table
      [SCIC_SDS_STP_REQUEST_STARTED_UDMA_MAX_SUBSTATES];

extern SCI_BASE_STATE_T
   scic_sds_stp_request_started_udma_substate_table
      [SCIC_SDS_STP_REQUEST_STARTED_UDMA_MAX_SUBSTATES];

extern SCIC_SDS_IO_REQUEST_STATE_HANDLER_T
   scic_sds_stp_request_started_non_data_substate_handler_table
      [SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_MAX_SUBSTATES];

extern SCI_BASE_STATE_T
   scic_sds_stp_request_started_non_data_substate_table
      [SCIC_SDS_STP_REQUEST_STARTED_NON_DATA_MAX_SUBSTATES];


extern SCIC_SDS_IO_REQUEST_STATE_HANDLER_T
   scic_sds_stp_request_started_soft_reset_substate_handler_table
      [SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_MAX_SUBSTATES];

extern SCI_BASE_STATE_T
   scic_sds_stp_request_started_soft_reset_substate_table
      [SCIC_SDS_STP_REQUEST_STARTED_SOFT_RESET_MAX_SUBSTATES];

// ---------------------------------------------------------------------------

U32 scic_sds_stp_request_get_object_size(void);

U32 scic_sds_stp_task_request_get_object_size(void);

void scu_sata_reqeust_construct_task_context(
   SCIC_SDS_REQUEST_T * this_request,
   SCU_TASK_CONTEXT_T * task_context
);

void scic_sds_stp_non_ncq_request_construct(
   SCIC_SDS_REQUEST_T *this_request
);

SCI_STATUS scic_sds_stp_pio_request_construct(
   SCIC_SDS_REQUEST_T  * scic_io_request,
   U8                    sat_protocol,
   BOOL                  copy_rx_frame
);

SCI_STATUS scic_sds_stp_pio_request_construct_pass_through (
   SCIC_SDS_REQUEST_T  * scic_io_request,
   SCIC_STP_PASSTHRU_REQUEST_CALLBACKS_T *passthru_cb
);

SCI_STATUS scic_sds_stp_udma_request_construct(
   SCIC_SDS_REQUEST_T * this_request,
   U32 transfer_length,
   SCI_IO_REQUEST_DATA_DIRECTION data_direction
);

SCI_STATUS scic_sds_stp_non_data_request_construct(
   SCIC_SDS_REQUEST_T * this_request
);

SCI_STATUS scic_sds_stp_soft_reset_request_construct(
   SCIC_SDS_REQUEST_T * this_request
);

SCI_STATUS scic_sds_stp_ncq_request_construct(
   SCIC_SDS_REQUEST_T * this_request,
   U32 transfer_length,
   SCI_IO_REQUEST_DATA_DIRECTION data_direction
);

void scu_stp_raw_request_construct_task_context(
   SCIC_SDS_STP_REQUEST_T * this_request,
   SCU_TASK_CONTEXT_T     * task_context

);

SCI_STATUS scic_sds_io_request_construct_sata(
   SCIC_SDS_REQUEST_T          * this_request,
   U8                            sat_protocol,
   U32                           transfer_length,
   SCI_IO_REQUEST_DATA_DIRECTION data_direction,
   BOOL                          copy_rx_frame,
   BOOL                          do_translate_sgl
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_SDS_STP_REQUEST_T_
