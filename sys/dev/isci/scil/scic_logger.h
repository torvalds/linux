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
#ifndef _SCIC_LOGGER_H_
#define _SCIC_LOGGER_H_

/**
 * @file
 *
 * @brief This file contains all of the SCI Core specific logger object
 *        constant definitions.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_logger.h>


/* The following is a list of log objects for which log information can */
/* be enabled or disabled.                                              */

/** Enables/disables logging specific to the library. */
#define SCIC_LOG_OBJECT_LIBRARY                 0x00000001

/** Enables/disables logging specific to the controller. */
#define SCIC_LOG_OBJECT_CONTROLLER              0x00000002

/** Enables/disables logging specific to the sas port. */
#define SCIC_LOG_OBJECT_PORT                    0x00000004

/** Enables/disables logging specific to the SAS phy. */
#define SCIC_LOG_OBJECT_PHY                     0x00000008

/** Enables/disables logging specific to the SSP remote target. */
#define SCIC_LOG_OBJECT_SSP_REMOTE_TARGET       0x00000010

/** Enables/disables logging specific to the STP remote target. */
#define SCIC_LOG_OBJECT_STP_REMOTE_TARGET       0x00000020

/** Enables/disables logging specific to the SMP remote target. */
#define SCIC_LOG_OBJECT_SMP_REMOTE_TARGET       0x00000040

/** Enables/disables logging specific to the SMP remote initiator. */
#define SCIC_LOG_OBJECT_SMP_REMOTE_INITIATOR    0x00000080

/** Enables/disables logging specific to the SSP IO requests. */
#define SCIC_LOG_OBJECT_SSP_IO_REQUEST          0x00000100

/** Enables/disables logging specific to the STP IO requests. */
#define SCIC_LOG_OBJECT_STP_IO_REQUEST          0x00000200

/** Enables/disables logging specific to the SMP IO requests. */
#define SCIC_LOG_OBJECT_SMP_IO_REQUEST          0x00000400

/** Enables/disables logging specific to the SMP IO response. */
#define SCIC_LOG_OBJECT_SMP_IO_RESPONSE         0x00000800

/** Enables/disables logging specific to the initialization. */
#define SCIC_LOG_OBJECT_INITIALIZATION          0x00001000

/** Enables/disables logging specific to the SGPIO. */
#define SCIC_LOG_OBJECT_SGPIO                   0x00002000

/** Enables/disables logging specific to staggered spin up. */
#define SCIC_LOG_OBJECT_STAGGERED_SPIN_UP       0x00004000

/** Enables/disables logging specific to the controller unsolicited frames. */
#define SCIC_LOG_OBJECT_UNSOLICITED_FRAMES      0x00008000

/** Enables/disables logging specific to the received controller events. */
#define SCIC_LOG_OBJECT_RECEIVED_EVENTS         0x00010000

/** Enables/disables logging specific to the controller completion queue */
#define SCIC_LOG_OBJECT_COMPLETION_QUEUE        0x00020000

/** Enables/disables logging specific to the task management requests. */
#define SCIC_LOG_OBJECT_TASK_MANAGEMENT         0x00040000

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIC_LOGGER_H_

