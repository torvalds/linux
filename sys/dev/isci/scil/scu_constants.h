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
#ifndef _SCU_CONSTANTS_H_
#define _SCU_CONSTANTS_H_

/**
 * @file
 *
 * @brief This file contains the SCU hardware constants.
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <dev/isci/scil/sci_controller_constants.h>

/**
 * 2 indicates the maximum number of UFs that can occur for a given IO
 * request.  The hardware handles reception of additional unsolicited
 * frames while all UFs are in use, by holding off the transmitting
 * device.  This number could be theoretically reduced to 1, but 2
 * provides for more reliable operation.  During SATA PIO operation,
 * it is possible under some conditions for there to be 3 separate
 * FISes received, back to back to back (PIO Setup, Data, D2H Register).
 * It is unlikely to have all 3 pending all at once without some of
 * them already being processed.
 */
#define SCU_MIN_UNSOLICITED_FRAMES        (8)
#define SCU_MIN_CRITICAL_NOTIFICATIONS    (19)
#define SCU_MIN_EVENTS                    (4)
#define SCU_MIN_COMPLETION_QUEUE_SCRATCH  (0)
#define SCU_MIN_COMPLETION_QUEUE_ENTRIES  ( SCU_MIN_CRITICAL_NOTIFICATIONS \
                                          + SCU_MIN_EVENTS \
                                          + SCU_MIN_UNSOLICITED_FRAMES \
                                          + SCI_MIN_IO_REQUESTS \
                                          + SCU_MIN_COMPLETION_QUEUE_SCRATCH )

#define SCU_MAX_CRITICAL_NOTIFICATIONS    (384)
#define SCU_MAX_EVENTS                    (128)
#define SCU_MAX_UNSOLICITED_FRAMES        (128)
#define SCU_MAX_COMPLETION_QUEUE_SCRATCH  (128)
#define SCU_MAX_COMPLETION_QUEUE_ENTRIES  ( SCU_MAX_CRITICAL_NOTIFICATIONS \
                                          + SCU_MAX_EVENTS \
                                          + SCU_MAX_UNSOLICITED_FRAMES \
                                          + SCI_MAX_IO_REQUESTS \
                                          + SCU_MAX_COMPLETION_QUEUE_SCRATCH )

#if !defined(ENABLE_MINIMUM_MEMORY_MODE)
#define SCU_UNSOLICITED_FRAME_COUNT      SCU_MAX_UNSOLICITED_FRAMES
#define SCU_CRITICAL_NOTIFICATION_COUNT  SCU_MAX_CRITICAL_NOTIFICATIONS
#define SCU_EVENT_COUNT                  SCU_MAX_EVENTS
#define SCU_COMPLETION_QUEUE_SCRATCH     SCU_MAX_COMPLETION_QUEUE_SCRATCH
#define SCU_IO_REQUEST_COUNT             SCI_MAX_IO_REQUESTS
#define SCU_IO_REQUEST_SGE_COUNT         SCI_MAX_SCATTER_GATHER_ELEMENTS
#define SCU_COMPLETION_QUEUE_COUNT       SCU_MAX_COMPLETION_QUEUE_ENTRIES
#else
#define SCU_UNSOLICITED_FRAME_COUNT      SCU_MIN_UNSOLICITED_FRAMES
#define SCU_CRITICAL_NOTIFICATION_COUNT  SCU_MIN_CRITICAL_NOTIFICATIONS
#define SCU_EVENT_COUNT                  SCU_MIN_EVENTS
#define SCU_COMPLETION_QUEUE_SCRATCH     SCU_MIN_COMPLETION_QUEUE_SCRATCH
#define SCU_IO_REQUEST_COUNT             SCI_MIN_IO_REQUESTS
#define SCU_IO_REQUEST_SGE_COUNT         SCI_MIN_SCATTER_GATHER_ELEMENTS
#define SCU_COMPLETION_QUEUE_COUNT       SCU_MIN_COMPLETION_QUEUE_ENTRIES
#endif // !defined(ENABLE_MINIMUM_MEMORY_OPERATION)

/**
 * The SCU_COMPLETION_QUEUE_COUNT constant indicates the size
 * of the completion queue into which the hardware DMAs 32-bit
 * quantas (completion entries).
 */

/**
 * This queue must be programmed to a power of 2 size (e.g. 32, 64,
 * 1024, etc.).
 */
#if (SCU_COMPLETION_QUEUE_COUNT != 16)  && \
    (SCU_COMPLETION_QUEUE_COUNT != 32)  && \
    (SCU_COMPLETION_QUEUE_COUNT != 64)  && \
    (SCU_COMPLETION_QUEUE_COUNT != 128) && \
    (SCU_COMPLETION_QUEUE_COUNT != 256) && \
    (SCU_COMPLETION_QUEUE_COUNT != 512) && \
    (SCU_COMPLETION_QUEUE_COUNT != 1024)
#error "SCU_COMPLETION_QUEUE_COUNT must be set to a power of 2."
#endif

#if SCU_MIN_UNSOLICITED_FRAMES > SCU_MAX_UNSOLICITED_FRAMES
#error "Invalid configuration of unsolicited frame constants"
#endif // SCU_MIN_UNSOLICITED_FRAMES > SCU_MAX_UNSOLICITED_FRAMES

#define SCU_MIN_UF_TABLE_ENTRIES            (8)
#define SCU_ABSOLUTE_MAX_UNSOLICITED_FRAMES (4096)
#define SCU_UNSOLICITED_FRAME_BUFFER_SIZE   (1024)
#define SCU_INVALID_FRAME_INDEX             (0xFFFF)

#define SCU_IO_REQUEST_MAX_SGE_SIZE         (0x00FFFFFF)
#define SCU_IO_REQUEST_MAX_TRANSFER_LENGTH  (0x00FFFFFF)

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _SCU_CONSTANTS_H_
