/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 ******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2015 Intel Deutschland GmbH
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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
 *****************************************************************************/

/* $FreeBSD$ */

#ifndef __IF_IWN_NOTIF_WAIT_H__
#define __IF_IWN_NOTIF_WAIT_H__

#include <sys/queue.h>

#define IWM_MAX_NOTIF_CMDS	5

struct iwm_rx_packet;
struct iwm_softc;

/**
 * struct iwm_notification_wait - notification wait entry
 * @entry: link for global list
 * @fn: Function called with the notification. If the function
 *      returns true, the wait is over, if it returns false then
 *      the waiter stays blocked. If no function is given, any
 *      of the listed commands will unblock the waiter.
 * @cmds: command IDs
 * @n_cmds: number of command IDs
 * @triggered: waiter should be woken up
 * @aborted: wait was aborted
 *
 * This structure is not used directly, to wait for a
 * notification declare it on the stack, and call
 * iwm_init_notification_wait() with appropriate
 * parameters. Then do whatever will cause the ucode
 * to notify the driver, and to wait for that then
 * call iwm_wait_notification().
 *
 * Each notification is one-shot. If at some point we
 * need to support multi-shot notifications (which
 * can't be allocated on the stack) we need to modify
 * the code for them.
 */
struct iwm_notification_wait {
	STAILQ_ENTRY(iwm_notification_wait) entry;

	int (*fn)(struct iwm_softc *sc, struct iwm_rx_packet *pkt, void *data);
	void *fn_data;

	uint16_t cmds[IWM_MAX_NOTIF_CMDS];
	uint8_t n_cmds;
	int triggered, aborted;
};

/* caller functions */
extern	struct iwm_notif_wait_data *iwm_notification_wait_init(
		struct iwm_softc *sc);
extern	void iwm_notification_wait_free(struct iwm_notif_wait_data *notif_data);
extern	void iwm_notification_wait_notify(
		struct iwm_notif_wait_data *notif_data, uint16_t cmd,
		struct iwm_rx_packet *pkt);
extern	void iwm_abort_notification_waits(
		struct iwm_notif_wait_data *notif_data);

/* user functions */
extern	void iwm_init_notification_wait(struct iwm_notif_wait_data *notif_data,
		struct iwm_notification_wait *wait_entry,
		const uint16_t *cmds, int n_cmds,
		int (*fn)(struct iwm_softc *sc,
			  struct iwm_rx_packet *pkt, void *data),
		void *fn_data);
extern	int iwm_wait_notification(struct iwm_notif_wait_data *notif_data,
		struct iwm_notification_wait *wait_entry, int timeout);
extern	void iwm_remove_notification(struct iwm_notif_wait_data *notif_data,
		struct iwm_notification_wait *wait_entry);

#endif  /* __IF_IWN_NOTIF_WAIT_H__ */
