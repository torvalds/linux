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
#ifndef _SCIC_SDS_SATA_PIO_REQUEST_H_
#define _SCIC_SDS_SATA_PIO_REQUEST_H_

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_base_state.h>
#include <dev/isci/scil/scic_sds_request.h>
#include <dev/isci/scil/scu_task_context.h>

/**
 * @file
 *
 * @brief This file contains the structures and constants for SATA PIO
 *        requests.
 */


/**
 * @enum
 *
 * This is the enumeration of the SATA PIO DATA IN started substate machine.
 */
enum _SCIC_SDS_STP_REQUEST_STARTED_PIO_SUBSTATES
{
   /**
    * While in this state the IO request object is waiting for the TC completion
    * notification for the H2D Register FIS
    */
   SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_H2D_COMPLETION_SUBSTATE,

   /**
    * While in this state the IO request object is waiting for either a PIO Setup
    * FIS or a D2H register FIS.  The type of frame received is based on the
    * result of the prior frame and line conditions.
    */
   SCIC_SDS_STP_REQUEST_STARTED_PIO_AWAIT_FRAME_SUBSTATE,

   /**
    * While in this state the IO request object is waiting for a DATA frame from
    * the device.
    */
   SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_IN_AWAIT_DATA_SUBSTATE,

   /**
    * While in this state the IO request object is waiting to transmit the next data
    * frame to the device.
    */
   SCIC_SDS_STP_REQUEST_STARTED_PIO_DATA_OUT_TRANSMIT_DATA_SUBSTATE,

   SCIC_SDS_STP_REQUEST_STARTED_PIO_MAX_SUBSTATES
};




// ---------------------------------------------------------------------------

extern SCIC_SDS_IO_REQUEST_STATE_HANDLER_T
   scic_sds_stp_request_started_pio_substate_handler_table[
      SCIC_SDS_STP_REQUEST_STARTED_PIO_MAX_SUBSTATES];

extern SCI_BASE_STATE_T
   scic_sds_stp_request_started_pio_substate_table[
      SCIC_SDS_STP_REQUEST_STARTED_PIO_MAX_SUBSTATES];

// ---------------------------------------------------------------------------

SCU_SGL_ELEMENT_T * scic_sds_stp_request_pio_get_next_sgl(
   SCIC_SDS_STP_REQUEST_T * this_request
);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif   // _SCIC_SDS_SATA_PIO_REQUEST_H_
