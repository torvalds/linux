/*	$OpenBSD: if_iwm.c,v 1.39 2015/03/23 00:35:19 jsg Exp $	*/

/*
 * Copyright (c) 2014 genua mbh <info@genua.de>
 * Copyright (c) 2014 Fixup Software Ltd.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 * Driver version we are currently based off of is
 * Linux 3.14.3 (tag id a2df521e42b1d9a23f620ac79dbfe8655a8391dd)
 *
 ***********************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2013 Intel Corporation. All rights reserved.
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
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2013 Intel Corporation. All rights reserved.
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
 */

/*-
 * Copyright (c) 2007-2010 Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"
#include "opt_iwm.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/firmware.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/linker.h>

#include <machine/bus.h>
#include <machine/endian.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <net/bpf.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_ratectl.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/iwm/if_iwmreg.h>
#include <dev/iwm/if_iwmvar.h>
#include <dev/iwm/if_iwm_debug.h>
#include <dev/iwm/if_iwm_util.h>
#include <dev/iwm/if_iwm_notif_wait.h>
#include <dev/iwm/if_iwm_pcie_trans.h>
#include <dev/iwm/if_iwm_time_event.h>

#define TU_TO_HZ(tu)	(((uint64_t)(tu) * 1024 * hz) / 1000000)

static void
iwm_mvm_te_clear_data(struct iwm_softc *sc)
{
	sc->sc_time_event_uid = 0;
	sc->sc_time_event_duration = 0;
	sc->sc_time_event_end_ticks = 0;
	sc->sc_flags &= ~IWM_FLAG_TE_ACTIVE;
}

/*
 * Handles a FW notification for an event that is known to the driver.
 *
 * @mvm: the mvm component
 * @te_data: the time event data
 * @notif: the notification data corresponding the time event data.
 */
static void
iwm_mvm_te_handle_notif(struct iwm_softc *sc,
    struct iwm_time_event_notif *notif)
{
	IWM_DPRINTF(sc, IWM_DEBUG_TE,
	    "Handle time event notif - UID = 0x%x action %d\n",
	    le32toh(notif->unique_id),
	    le32toh(notif->action));

	if (!le32toh(notif->status)) {
		const char *msg;

		if (notif->action & htole32(IWM_TE_V2_NOTIF_HOST_EVENT_START))
			msg = "Time Event start notification failure";
		else
			msg = "Time Event end notification failure";

		IWM_DPRINTF(sc, IWM_DEBUG_TE, "%s\n", msg);
	}

	if (le32toh(notif->action) & IWM_TE_V2_NOTIF_HOST_EVENT_END) {
		IWM_DPRINTF(sc, IWM_DEBUG_TE,
		    "TE ended - current time %d, estimated end %d\n",
		    ticks, sc->sc_time_event_end_ticks);

		iwm_mvm_te_clear_data(sc);
	} else if (le32toh(notif->action) & IWM_TE_V2_NOTIF_HOST_EVENT_START) {
		sc->sc_time_event_end_ticks =
		    ticks + TU_TO_HZ(sc->sc_time_event_duration);
	} else {
		device_printf(sc->sc_dev, "Got TE with unknown action\n");
	}
}

/*
 * The Rx handler for time event notifications
 */
void
iwm_mvm_rx_time_event_notif(struct iwm_softc *sc, struct iwm_rx_packet *pkt)
{
	struct iwm_time_event_notif *notif = (void *)pkt->data;

	IWM_DPRINTF(sc, IWM_DEBUG_TE,
	    "Time event notification - UID = 0x%x action %d\n",
	    le32toh(notif->unique_id),
	    le32toh(notif->action));

	iwm_mvm_te_handle_notif(sc, notif);
}

static int
iwm_mvm_te_notif(struct iwm_softc *sc, struct iwm_rx_packet *pkt,
    void *data)
{
	struct iwm_time_event_notif *resp;
	int resp_len = iwm_rx_packet_payload_len(pkt);

	if (pkt->hdr.code != IWM_TIME_EVENT_NOTIFICATION ||
	    resp_len != sizeof(*resp)) {
		IWM_DPRINTF(sc, IWM_DEBUG_TE,
		    "Invalid TIME_EVENT_NOTIFICATION response\n");
		return 1;
	}

	resp = (void *)pkt->data;

	/* te_data->uid is already set in the TIME_EVENT_CMD response */
	if (le32toh(resp->unique_id) != sc->sc_time_event_uid)
		return false;

	IWM_DPRINTF(sc, IWM_DEBUG_TE,
	    "TIME_EVENT_NOTIFICATION response - UID = 0x%x\n",
	    sc->sc_time_event_uid);
	if (!resp->status) {
		IWM_DPRINTF(sc, IWM_DEBUG_TE,
		    "TIME_EVENT_NOTIFICATION received but not executed\n");
	}

	return 1;
}

static int
iwm_mvm_time_event_response(struct iwm_softc *sc, struct iwm_rx_packet *pkt,
    void *data)
{
	struct iwm_time_event_resp *resp;
	int resp_len = iwm_rx_packet_payload_len(pkt);

	if (pkt->hdr.code != IWM_TIME_EVENT_CMD ||
	    resp_len != sizeof(*resp)) {
		IWM_DPRINTF(sc, IWM_DEBUG_TE,
		    "Invalid TIME_EVENT_CMD response\n");
		return 1;
	}

	resp = (void *)pkt->data;

	/* we should never get a response to another TIME_EVENT_CMD here */
	if (le32toh(resp->id) != IWM_TE_BSS_STA_AGGRESSIVE_ASSOC) {
		IWM_DPRINTF(sc, IWM_DEBUG_TE,
		    "Got TIME_EVENT_CMD response with wrong id: %d\n",
		    le32toh(resp->id));
		return 0;
	}

	sc->sc_time_event_uid = le32toh(resp->unique_id);
	IWM_DPRINTF(sc, IWM_DEBUG_TE,
	    "TIME_EVENT_CMD response - UID = 0x%x\n", sc->sc_time_event_uid);
	return 1;
}


/* XXX Use the te_data function argument properly, like in iwlwifi's code. */

static int
iwm_mvm_time_event_send_add(struct iwm_softc *sc, struct iwm_vap *ivp,
	void *te_data, struct iwm_time_event_cmd *te_cmd)
{
	static const uint16_t time_event_response[] = { IWM_TIME_EVENT_CMD };
	struct iwm_notification_wait wait_time_event;
	int ret;

	IWM_DPRINTF(sc, IWM_DEBUG_TE,
	    "Add new TE, duration %d TU\n", le32toh(te_cmd->duration));

	sc->sc_time_event_duration = le32toh(te_cmd->duration);

	/*
	 * Use a notification wait, which really just processes the
	 * command response and doesn't wait for anything, in order
	 * to be able to process the response and get the UID inside
	 * the RX path. Using CMD_WANT_SKB doesn't work because it
	 * stores the buffer and then wakes up this thread, by which
	 * time another notification (that the time event started)
	 * might already be processed unsuccessfully.
	 */
	iwm_init_notification_wait(sc->sc_notif_wait, &wait_time_event,
				   time_event_response,
				   nitems(time_event_response),
				   iwm_mvm_time_event_response, /*te_data*/NULL);

	ret = iwm_mvm_send_cmd_pdu(sc, IWM_TIME_EVENT_CMD, 0, sizeof(*te_cmd),
	    te_cmd);
	if (ret) {
		IWM_DPRINTF(sc, IWM_DEBUG_TE,
		    "%s: Couldn't send IWM_TIME_EVENT_CMD: %d\n",
		    __func__, ret);
		iwm_remove_notification(sc->sc_notif_wait, &wait_time_event);
		return ret;
	}

	/* No need to wait for anything, so just pass 1 (0 isn't valid) */
	IWM_UNLOCK(sc);
	ret = iwm_wait_notification(sc->sc_notif_wait, &wait_time_event, 1);
	IWM_LOCK(sc);
	/* should never fail */
	if (ret) {
		IWM_DPRINTF(sc, IWM_DEBUG_TE,
		    "%s: Failed to get response for IWM_TIME_EVENT_CMD: %d\n",
		    __func__, ret);
	}

	return ret;
}

void
iwm_mvm_protect_session(struct iwm_softc *sc, struct iwm_vap *ivp,
	uint32_t duration, uint32_t max_delay, boolean_t wait_for_notif)
{
	const uint16_t te_notif_response[] = { IWM_TIME_EVENT_NOTIFICATION };
	struct iwm_notification_wait wait_te_notif;
	struct iwm_time_event_cmd time_cmd = {};

	/* Do nothing if a time event is already scheduled. */
	if (sc->sc_flags & IWM_FLAG_TE_ACTIVE)
		return;

	time_cmd.action = htole32(IWM_FW_CTXT_ACTION_ADD);
	time_cmd.id_and_color =
	    htole32(IWM_FW_CMD_ID_AND_COLOR(ivp->id, ivp->color));
	time_cmd.id = htole32(IWM_TE_BSS_STA_AGGRESSIVE_ASSOC);

	time_cmd.apply_time = htole32(0);

	time_cmd.max_frags = IWM_TE_V2_FRAG_NONE;
	time_cmd.max_delay = htole32(max_delay);
	/* TODO: why do we need to interval = bi if it is not periodic? */
	time_cmd.interval = htole32(1);
	time_cmd.duration = htole32(duration);
	time_cmd.repeat = 1;
	time_cmd.policy
	    = htole16(IWM_TE_V2_NOTIF_HOST_EVENT_START |
	        IWM_TE_V2_NOTIF_HOST_EVENT_END |
		IWM_T2_V2_START_IMMEDIATELY);

	if (!wait_for_notif) {
		iwm_mvm_time_event_send_add(sc, ivp, /*te_data*/NULL, &time_cmd);
		DELAY(100);
		sc->sc_flags |= IWM_FLAG_TE_ACTIVE;
		return;
	}

	/*
	 * Create notification_wait for the TIME_EVENT_NOTIFICATION to use
	 * right after we send the time event
	 */
	iwm_init_notification_wait(sc->sc_notif_wait, &wait_te_notif,
	    te_notif_response, nitems(te_notif_response),
	    iwm_mvm_te_notif, /*te_data*/NULL);

	/* If TE was sent OK - wait for the notification that started */
	if (iwm_mvm_time_event_send_add(sc, ivp, /*te_data*/NULL, &time_cmd)) {
		IWM_DPRINTF(sc, IWM_DEBUG_TE,
		    "%s: Failed to add TE to protect session\n", __func__);
		iwm_remove_notification(sc->sc_notif_wait, &wait_te_notif);
	} else {
		sc->sc_flags |= IWM_FLAG_TE_ACTIVE;
		IWM_UNLOCK(sc);
		if (iwm_wait_notification(sc->sc_notif_wait, &wait_te_notif,
		    TU_TO_HZ(max_delay))) {
			IWM_DPRINTF(sc, IWM_DEBUG_TE,
			    "%s: Failed to protect session until TE\n",
			    __func__);
		}
		IWM_LOCK(sc);
	}
}

void
iwm_mvm_stop_session_protection(struct iwm_softc *sc, struct iwm_vap *ivp)
{
	struct iwm_time_event_cmd time_cmd = {};

	/* Do nothing if the time event has already ended. */
	if ((sc->sc_flags & IWM_FLAG_TE_ACTIVE) == 0)
		return;

	time_cmd.id = htole32(sc->sc_time_event_uid);
	time_cmd.action = htole32(IWM_FW_CTXT_ACTION_REMOVE);
	time_cmd.id_and_color =
	    htole32(IWM_FW_CMD_ID_AND_COLOR(ivp->id, ivp->color));

	IWM_DPRINTF(sc, IWM_DEBUG_TE,
	    "%s: Removing TE 0x%x\n", __func__, le32toh(time_cmd.id));
	if (iwm_mvm_send_cmd_pdu(sc, IWM_TIME_EVENT_CMD, 0, sizeof(time_cmd),
	    &time_cmd) == 0)
		iwm_mvm_te_clear_data(sc);

	DELAY(100);
}
