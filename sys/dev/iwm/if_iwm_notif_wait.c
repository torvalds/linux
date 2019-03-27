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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"
#include "opt_iwm.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/queue.h>

#include <dev/iwm/if_iwm_notif_wait.h>

#define	IWM_WAIT_LOCK_INIT(_n, _s) \
	mtx_init(&(_n)->lk_mtx, (_s), "iwm wait_notif", MTX_DEF);
#define	IWM_WAIT_LOCK(_n)		mtx_lock(&(_n)->lk_mtx)
#define	IWM_WAIT_UNLOCK(_n)		mtx_unlock(&(_n)->lk_mtx)
#define	IWM_WAIT_LOCK_DESTROY(_n)	mtx_destroy(&(_n)->lk_mtx)

struct iwm_notif_wait_data {
	struct mtx lk_mtx;
	char lk_buf[32];
	STAILQ_HEAD(, iwm_notification_wait) list;
	struct iwm_softc *sc;
};

struct iwm_notif_wait_data *
iwm_notification_wait_init(struct iwm_softc *sc)
{
	struct iwm_notif_wait_data *data;

	data = malloc(sizeof(*data), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (data != NULL) {
		snprintf(data->lk_buf, 32, "iwm wait_notif");
		IWM_WAIT_LOCK_INIT(data, data->lk_buf);
		STAILQ_INIT(&data->list);
		data->sc = sc;
	}

	return data;
}

void
iwm_notification_wait_free(struct iwm_notif_wait_data *notif_data)
{
	KASSERT(STAILQ_EMPTY(&notif_data->list), ("notif list isn't empty"));
	IWM_WAIT_LOCK_DESTROY(notif_data);
	free(notif_data, M_DEVBUF);
}

/* XXX Get rid of separate cmd argument, like in iwlwifi's code */
void
iwm_notification_wait_notify(struct iwm_notif_wait_data *notif_data,
    uint16_t cmd, struct iwm_rx_packet *pkt)
{
	struct iwm_notification_wait *wait_entry;

	IWM_WAIT_LOCK(notif_data);
	STAILQ_FOREACH(wait_entry, &notif_data->list, entry) {
		int found = FALSE;
		int i;

		/*
		 * If it already finished (triggered) or has been
		 * aborted then don't evaluate it again to avoid races,
		 * Otherwise the function could be called again even
		 * though it returned true before
		 */
		if (wait_entry->triggered || wait_entry->aborted)
			continue;

		for (i = 0; i < wait_entry->n_cmds; i++) {
			if (cmd == wait_entry->cmds[i]) {
				found = TRUE;
				break;
			}
		}
		if (!found)
			continue;

		if (!wait_entry->fn ||
		    wait_entry->fn(notif_data->sc, pkt, wait_entry->fn_data)) {
			wait_entry->triggered = 1;
			wakeup(wait_entry);
		}
	}
	IWM_WAIT_UNLOCK(notif_data);
}

void
iwm_abort_notification_waits(struct iwm_notif_wait_data *notif_data)
{
	struct iwm_notification_wait *wait_entry;

	IWM_WAIT_LOCK(notif_data);
	STAILQ_FOREACH(wait_entry, &notif_data->list, entry) {
		wait_entry->aborted = 1;
		wakeup(wait_entry);
	}
	IWM_WAIT_UNLOCK(notif_data);
}

void
iwm_init_notification_wait(struct iwm_notif_wait_data *notif_data,
    struct iwm_notification_wait *wait_entry, const uint16_t *cmds, int n_cmds,
    int (*fn)(struct iwm_softc *sc, struct iwm_rx_packet *pkt, void *data),
    void *fn_data)
{
	KASSERT(n_cmds <= IWM_MAX_NOTIF_CMDS,
	    ("n_cmds %d is too large", n_cmds));
	wait_entry->fn = fn;
	wait_entry->fn_data = fn_data;
	wait_entry->n_cmds = n_cmds;
	memcpy(wait_entry->cmds, cmds, n_cmds * sizeof(uint16_t));
	wait_entry->triggered = 0;
	wait_entry->aborted = 0;

	IWM_WAIT_LOCK(notif_data);
	STAILQ_INSERT_TAIL(&notif_data->list, wait_entry, entry);
	IWM_WAIT_UNLOCK(notif_data);
}

int
iwm_wait_notification(struct iwm_notif_wait_data *notif_data,
    struct iwm_notification_wait *wait_entry, int timeout)
{
	int ret = 0;

	IWM_WAIT_LOCK(notif_data);
	if (!wait_entry->triggered && !wait_entry->aborted) {
		ret = msleep(wait_entry, &notif_data->lk_mtx, 0, "iwm_notif",
		    timeout);
	}
	STAILQ_REMOVE(&notif_data->list, wait_entry, iwm_notification_wait,
	    entry);
	IWM_WAIT_UNLOCK(notif_data);

	return ret;
}

void
iwm_remove_notification(struct iwm_notif_wait_data *notif_data,
    struct iwm_notification_wait *wait_entry)
{
	IWM_WAIT_LOCK(notif_data);
	STAILQ_REMOVE(&notif_data->list, wait_entry, iwm_notification_wait,
	    entry);
	IWM_WAIT_UNLOCK(notif_data);
}
