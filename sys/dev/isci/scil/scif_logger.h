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
#ifndef _SCIF_LOGGER_H_
#define _SCIF_LOGGER_H_

/**
 * @file
 *
 * @brief This file contains all of the SCI Framework specific logger object
 *        constant definitions.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_logger.h>


/* The following is a list of log objects for which log information can */
/* be enabled or disabled.                                              */

/** Enables/disables logging specific to the library. */
#define SCIF_LOG_OBJECT_LIBRARY                 0x00000001

/** Enables/disables logging specific to the controller. */
#define SCIF_LOG_OBJECT_CONTROLLER              0x00000002

/** Enables/disables logging specific to the sas port. */
#define SCIF_LOG_OBJECT_DOMAIN                  0x00000004

/** Enables/disables logging specific to the domain discovery process. */
#define SCIF_LOG_OBJECT_DOMAIN_DISCOVERY        0x00000008

/** Enables/disables logging specific to the remote devices. */
#define SCIF_LOG_OBJECT_REMOTE_DEVICE           0x00000010

/** Enables/disables logging specific to remote device configuration. */
#define SCIF_LOG_OBJECT_REMOTE_DEVICE_CONFIG    0x00000020

/** Enables/disables logging specific to performing task management. */
#define SCIF_LOG_OBJECT_TASK_MANAGEMENT         0x00000040

/** Enables/disables logging specific to SCSI to SATA command translation. */
#define SCIF_LOG_OBJECT_COMMAND_TRANSLATION     0x00000080

/** Enables/disables logging specific to SCSI to SATA response translation. */
#define SCIF_LOG_OBJECT_RESPONSE_TRANSLATION    0x00000100

/** Enables/disables logging specific to framework initialization. */
#define SCIF_LOG_OBJECT_INITIALIZATION          0x00000200

/** Enables/disables logging specific to framework shutdown. */
#define SCIF_LOG_OBJECT_SHUTDOWN                0x00000400

/** Enables/disables logging specific to all IO requests. */
#define SCIF_LOG_OBJECT_IO_REQUEST              0x00000800

/** Enables/disables logging specific to all IO requests. */
#define SCIF_LOG_OBJECT_CONTROLLER_RESET        0x00001000

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCIF_LOGGER_H_

