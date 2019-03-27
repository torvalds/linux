/*-
 * Copyright (c) 2014 Qualcomm Atheros, Inc.
 * Copyright (c) 2016 Adrian Chadd <adrian@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * This implements the MCI bluetooth coexistence handling.
 */
#include "opt_ath.h"
#include "opt_inet.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/errno.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <sys/bus.h>

#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/if_arp.h>
#include <net/ethernet.h>		/* XXX for ether_sprintf */

#include <net80211/ieee80211_var.h>

#include <net/bpf.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <dev/ath/if_athvar.h>
#include <dev/ath/if_ath_debug.h>
#include <dev/ath/if_ath_descdma.h>
#include <dev/ath/if_ath_btcoex.h>

#include <dev/ath/if_ath_btcoex_mci.h>

MALLOC_DECLARE(M_ATHDEV);

#define	ATH_MCI_GPM_MAX_ENTRY		16
#define	ATH_MCI_GPM_BUF_SIZE		(ATH_MCI_GPM_MAX_ENTRY * 16)
#define	ATH_MCI_SCHED_BUF_SIZE		(16 * 16) /* 16 entries, 4 dword each */

static void ath_btcoex_mci_update_wlan_channels(struct ath_softc *sc);

int
ath_btcoex_mci_attach(struct ath_softc *sc)
{
	int buflen, error;

	buflen = ATH_MCI_GPM_BUF_SIZE + ATH_MCI_SCHED_BUF_SIZE;
	error = ath_descdma_alloc_desc(sc, &sc->sc_btcoex.buf, NULL,
	    "MCI bufs", buflen, 1);
	if (error != 0) {
		device_printf(sc->sc_dev, "%s: failed to alloc MCI RAM\n",
		    __func__);
		return (error);
	}

	/* Yes, we're going to do bluetooth MCI coex */
	sc->sc_btcoex_mci = 1;

	/* Initialise the wlan channel mapping */
	sc->sc_btcoex.wlan_channels[0] = 0x00000000;
	sc->sc_btcoex.wlan_channels[1] = 0xffffffff;
	sc->sc_btcoex.wlan_channels[2] = 0xffffffff;
	sc->sc_btcoex.wlan_channels[3] = 0x7fffffff;

	/*
	 * Ok, so the API is a bit odd. It assumes sched_addr is
	 * after gpm_addr, and it does math to figure out the right
	 * sched_buf pointer.
	 *
	 * So, set gpm_addr to buf, sched_addr to gpm_addr + ATH_MCI_GPM_BUF_SIZE,
	 * the HAL call with do (gpm_buf + (sched_addr - gpm_addr)) to
	 * set sched_buf, and we're "golden".
	 *
	 * Note, it passes in 'len' here (gpm_len) as
	 * ATH_MCI_GPM_BUF_SIZE >> 4.  My guess is that it's 16
	 * bytes per entry and we're storing 16 entries.
	 */
	sc->sc_btcoex.gpm_buf = (void *) sc->sc_btcoex.buf.dd_desc;
	sc->sc_btcoex.sched_buf = sc->sc_btcoex.gpm_buf +
	    ATH_MCI_GPM_BUF_SIZE;

	sc->sc_btcoex.gpm_paddr = sc->sc_btcoex.buf.dd_desc_paddr;
	sc->sc_btcoex.sched_paddr = sc->sc_btcoex.gpm_paddr +
	    ATH_MCI_GPM_BUF_SIZE;

	/* memset the gpm buffer with MCI_GPM_RSVD_PATTERN */
	memset(sc->sc_btcoex.gpm_buf, 0xfe, buflen);

	/*
	 * This is an unfortunate x86'ism in the HAL - the
	 * HAL code expects the passed in buffer to be
	 * coherent, and doesn't implement /any/ kind
	 * of buffer sync operations at all.
	 *
	 * So, this code will only work on dma coherent buffers
	 * and will behave poorly on non-coherent systems.
	 * Fixing this would require some HAL surgery so it
	 * actually /did/ the buffer flushing as appropriate.
	 */
	ath_hal_btcoex_mci_setup(sc->sc_ah,
	    sc->sc_btcoex.gpm_paddr,
	    sc->sc_btcoex.gpm_buf,
	    ATH_MCI_GPM_BUF_SIZE >> 4,
	    sc->sc_btcoex.sched_paddr);

	return (0);
}

/*
 * Detach btcoex from the given interface
 */
int
ath_btcoex_mci_detach(struct ath_softc *sc)
{

	ath_hal_btcoex_mci_detach(sc->sc_ah);
	ath_descdma_cleanup(sc, &sc->sc_btcoex.buf, NULL);
	return (0);
}

/*
 * Configure or disable bluetooth coexistence on the given channel.
 *
 * For MCI, we just use the top-level enable/disable flag, and
 * then the MCI reset / channel update path will configure things
 * appropriately based on the current band.
 */
int
ath_btcoex_mci_enable(struct ath_softc *sc,
    const struct ieee80211_channel *chan)
{

	/*
	 * Always reconfigure stomp-all for now, so wlan wins.
	 *
	 * The default weights still don't allow beacons to win,
	 * so unless you set net.wlan.X.bmiss_max to something higher,
	 * net80211 will disconnect you during a HCI INQUIRY command.
	 *
	 * The longer-term solution is to dynamically adjust whether
	 * bmiss happens based on bluetooth requirements, and look at
	 * making the individual stomp bits configurable.
	 */
	ath_hal_btcoex_set_weights(sc->sc_ah, HAL_BT_COEX_STOMP_ALL);

	/*
	 * update wlan channels so the firmware knows what channels it
	 * can/can't use.
	 */
	ath_btcoex_mci_update_wlan_channels(sc);

	return (0);
}

/*
 * XXX TODO: turn into general btcoex, and then make this
 * the MCI specific bits.
 */
static void
ath_btcoex_mci_event(struct ath_softc *sc, ATH_BT_COEX_EVENT nevent,
    void *param)
{

	if (! sc->sc_btcoex_mci)
		return;

	/*
	 * Check whether we need to flush our local profile cache.
	 * If we do, then at (XXX TODO) we should flush our state,
	 * then wait for the MCI response with the updated profile list.
	 */
	if (ath_hal_btcoex_mci_state(sc->sc_ah,
	    HAL_MCI_STATE_NEED_FLUSH_BT_INFO, NULL) != 0) {
		uint32_t data = 0;

		if (ath_hal_btcoex_mci_state(sc->sc_ah,
		    HAL_MCI_STATE_ENABLE, NULL) != 0) {
			DPRINTF(sc, ATH_DEBUG_BTCOEX,
			    "(MCI) Flush BT profile\n");
			/*
			 * XXX TODO: flush profile state on the ath(4)
			 * driver side; subsequent messages will come
			 * through with the current list of active
			 * profiles.
			 */
			ath_hal_btcoex_mci_state(sc->sc_ah,
			    HAL_MCI_STATE_NEED_FLUSH_BT_INFO, &data);
			ath_hal_btcoex_mci_state(sc->sc_ah,
			    HAL_MCI_STATE_SEND_STATUS_QUERY, NULL);
		}
	}
	if (nevent == ATH_COEX_EVENT_BT_NOOP) {
		DPRINTF(sc, ATH_DEBUG_BTCOEX, "(MCI) BT_NOOP\n");
		return;
	}
}

static void
ath_btcoex_mci_send_gpm(struct ath_softc *sc, uint32_t *payload)
{

	ath_hal_btcoex_mci_send_message(sc->sc_ah, MCI_GPM, 0, payload, 16,
	    AH_FALSE, AH_TRUE);
}

/*
 * This starts a BT calibration.  It requires a chip reset.
 */
static int
ath_btcoex_mci_bt_cal_do(struct ath_softc *sc, int tx_timeout, int rx_timeout)
{

	device_printf(sc->sc_dev, "%s: TODO!\n", __func__);
	return (0);
}

static void
ath_btcoex_mci_cal_msg(struct ath_softc *sc, uint8_t opcode,
    uint8_t *rx_payload)
{
	uint32_t payload[4] = {0, 0, 0, 0};

	switch (opcode) {
	case MCI_GPM_BT_CAL_REQ:
		DPRINTF(sc, ATH_DEBUG_BTCOEX, "(MCI) receive BT_CAL_REQ\n");
		if (ath_hal_btcoex_mci_state(sc->sc_ah, HAL_MCI_STATE_BT,
		    NULL) == MCI_BT_AWAKE) {
			ath_hal_btcoex_mci_state(sc->sc_ah,
			    HAL_MCI_STATE_SET_BT_CAL_START, NULL);
			ath_btcoex_mci_bt_cal_do(sc, 1000, 1000);
		} else {
			DPRINTF(sc, ATH_DEBUG_BTCOEX,
			    "(MCI) State mismatches: %d\n",
			    ath_hal_btcoex_mci_state(sc->sc_ah,
			    HAL_MCI_STATE_BT, NULL));
		}
		break;
	case MCI_GPM_BT_CAL_DONE:
		DPRINTF(sc, ATH_DEBUG_BTCOEX, "(MCI) receive BT_CAL_DONE\n");
		if (ath_hal_btcoex_mci_state(sc->sc_ah, HAL_MCI_STATE_BT,
		    NULL) == MCI_BT_CAL) {
			DPRINTF(sc, ATH_DEBUG_BTCOEX,
			    "(MCI) ERROR ILLEGAL!\n");
		} else {
			DPRINTF(sc, ATH_DEBUG_BTCOEX,
			    "(MCI) BT not in CAL state.\n");
		}
		break;
	case MCI_GPM_BT_CAL_GRANT:
		DPRINTF(sc, ATH_DEBUG_BTCOEX, "(MCI) receive BT_CAL_GRANT\n");
		/* Send WLAN_CAL_DONE for now */
		DPRINTF(sc, ATH_DEBUG_BTCOEX, "(MCI) Send WLAN_CAL_DONE\n");
		MCI_GPM_SET_CAL_TYPE(payload, MCI_GPM_WLAN_CAL_DONE);
		ath_btcoex_mci_send_gpm(sc, &payload[0]);
		break;
	default:
		DPRINTF(sc, ATH_DEBUG_BTCOEX,
		    "(MCI) Unknown GPM CAL message.\n");
		break;
	}
}

/*
 * Update the bluetooth channel map.
 *
 * This map tells the bluetooth device which bluetooth channels
 * are available for data.
 *
 * For 5GHz, all channels are available.
 * For 2GHz, the current wifi channel range is blocked out,
 *   and the rest are available.
 *
 * This narrows which frequencies are used by the device when
 * it initiates a transfer, thus hopefully reducing the chances
 * of collisions (both hopefully on the current device and
 * other devices in the same channel.)
 */
static void
ath_btcoex_mci_update_wlan_channels(struct ath_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *chan = ic->ic_curchan;
	uint32_t channel_info[4] =
	    { 0x00000000, 0xffffffff, 0xffffffff, 0x7fffffff };
	int32_t wl_chan, bt_chan, bt_start = 0, bt_end = 79;

	/* BT channel frequency is 2402 + k, k = 0 ~ 78 */
	if (IEEE80211_IS_CHAN_2GHZ(chan)) {
		wl_chan = chan->ic_freq - 2402;
		if (IEEE80211_IS_CHAN_HT40U(chan)) {
			bt_start = wl_chan - 10;
			bt_end = wl_chan + 30;
		} else if (IEEE80211_IS_CHAN_HT40D(chan)) {
			bt_start = wl_chan - 30;
			bt_end = wl_chan + 10;
		} else {
			/* Assume 20MHz */
			bt_start = wl_chan - 10;
			bt_end = wl_chan + 10;
		}

		bt_start -= 7;
		bt_end += 7;

		if (bt_start < 0) {
			bt_start = 0;
		}
		if (bt_end > MCI_NUM_BT_CHANNELS) {
			bt_end = MCI_NUM_BT_CHANNELS;
		}
		DPRINTF(sc, ATH_DEBUG_BTCOEX, "(MCI) WLAN use channel %d\n",
		    chan->ic_freq);
		DPRINTF(sc, ATH_DEBUG_BTCOEX,
		    "(MCI) mask BT channel %d - %d\n", bt_start, bt_end);
		for (bt_chan = bt_start; bt_chan < bt_end; bt_chan++) {
			MCI_GPM_CLR_CHANNEL_BIT(&channel_info[0], bt_chan);
		}
	} else {
		DPRINTF(sc, ATH_DEBUG_BTCOEX,
		    "(MCI) WLAN not use any 2G channel, unmask all for BT\n");
	}
	ath_hal_btcoex_mci_state(sc->sc_ah, HAL_MCI_STATE_SEND_WLAN_CHANNELS,
	    &channel_info[0]);
}

static void
ath_btcoex_mci_coex_msg(struct ath_softc *sc, uint8_t opcode,
    uint8_t *rx_payload)
{
	uint32_t version;
	uint8_t major;
	uint8_t minor;
	uint32_t seq_num;

	switch (opcode) {
	case MCI_GPM_COEX_VERSION_QUERY:
		DPRINTF(sc, ATH_DEBUG_BTCOEX,
		    "(MCI) Recv GPM COEX Version Query.\n");
		version = ath_hal_btcoex_mci_state(sc->sc_ah,
		    HAL_MCI_STATE_SEND_WLAN_COEX_VERSION, NULL);
		break;

	case MCI_GPM_COEX_VERSION_RESPONSE:
		DPRINTF(sc, ATH_DEBUG_BTCOEX,
		    "(MCI) Recv GPM COEX Version Response.\n");
		major = *(rx_payload + MCI_GPM_COEX_B_MAJOR_VERSION);
		minor = *(rx_payload + MCI_GPM_COEX_B_MINOR_VERSION);
		DPRINTF(sc, ATH_DEBUG_BTCOEX,
		    "(MCI) BT Coex version: %d.%d\n", major, minor);
		version = (major << 8) + minor;
		version = ath_hal_btcoex_mci_state(sc->sc_ah,
		    HAL_MCI_STATE_SET_BT_COEX_VERSION, &version);
		break;

	case MCI_GPM_COEX_STATUS_QUERY:
		DPRINTF(sc, ATH_DEBUG_BTCOEX,
		    "(MCI) Recv GPM COEX Status Query = 0x%02x.\n",
		    *(rx_payload + MCI_GPM_COEX_B_WLAN_BITMAP));
		ath_hal_btcoex_mci_state(sc->sc_ah,
		    HAL_MCI_STATE_SEND_WLAN_CHANNELS, NULL);
		break;

	case MCI_GPM_COEX_BT_PROFILE_INFO:
		/*
		 * XXX TODO: here is where we'd parse active profile
		 * info and make driver/stack choices as appropriate.
		 */
		DPRINTF(sc, ATH_DEBUG_BTCOEX,
		    "(MCI) TODO: Recv GPM COEX BT_Profile_Info.\n");
		break;

	case MCI_GPM_COEX_BT_STATUS_UPDATE:
		seq_num = *((uint32_t *)(rx_payload + 12));
		DPRINTF(sc, ATH_DEBUG_BTCOEX,
		    "(MCI) Recv GPM COEX BT_Status_Update: SEQ=%d\n",
		    seq_num);
		break;

	default:
		DPRINTF(sc, ATH_DEBUG_BTCOEX,
		    "(MCI) Unknown GPM COEX message = 0x%02x\n", opcode);
		break;
	}
}

void
ath_btcoex_mci_intr(struct ath_softc *sc)
{
	uint32_t mciInt, mciIntRxMsg;
	uint32_t offset, subtype, opcode;
	uint32_t *pGpm;
	uint32_t more_data = HAL_MCI_GPM_MORE;
	int8_t value_dbm;
	bool skip_gpm = false;

	DPRINTF(sc, ATH_DEBUG_BTCOEX, "%s: called\n", __func__);

	ath_hal_btcoex_mci_get_interrupt(sc->sc_ah, &mciInt, &mciIntRxMsg);

	if (ath_hal_btcoex_mci_state(sc->sc_ah, HAL_MCI_STATE_ENABLE,
	    NULL) == 0) {
		ath_hal_btcoex_mci_state(sc->sc_ah,
		    HAL_MCI_STATE_INIT_GPM_OFFSET, NULL);
		DPRINTF(sc, ATH_DEBUG_BTCOEX,
		    "(MCI) INTR but MCI_disabled\n");
		DPRINTF(sc, ATH_DEBUG_BTCOEX,
		    "(MCI) MCI interrupt: mciInt = 0x%x, mciIntRxMsg = 0x%x\n",
		    mciInt, mciIntRxMsg);
		return;
	}

	if (mciIntRxMsg & HAL_MCI_INTERRUPT_RX_MSG_REQ_WAKE) {
		uint32_t payload4[4] = { 0xffffffff, 0xffffffff, 0xffffffff,
		    0xffffff00};

		/*
		 * The following REMOTE_RESET and SYS_WAKING used to sent
		 * only when BT wake up. Now they are always sent, as a
		 * recovery method to reset BT MCI's RX alignment.
		 */
		DPRINTF(sc, ATH_DEBUG_BTCOEX,
		    "(MCI) 1. INTR Send REMOTE_RESET\n");
		ath_hal_btcoex_mci_send_message(sc->sc_ah,
		    MCI_REMOTE_RESET, 0, payload4, 16, AH_TRUE, AH_FALSE);
		DPRINTF(sc, ATH_DEBUG_BTCOEX,
		    "(MCI) 1. INTR Send SYS_WAKING\n");
		ath_hal_btcoex_mci_send_message(sc->sc_ah,
		    MCI_SYS_WAKING, 0, NULL, 0, AH_TRUE, AH_FALSE);

		mciIntRxMsg &= ~HAL_MCI_INTERRUPT_RX_MSG_REQ_WAKE;
		ath_hal_btcoex_mci_state(sc->sc_ah,
		    HAL_MCI_STATE_RESET_REQ_WAKE, NULL);

		/* always do this for recovery and 2G/5G toggling and LNA_TRANS */
		DPRINTF(sc, ATH_DEBUG_BTCOEX,
		    "(MCI) 1. Set BT state to AWAKE.\n");
		ath_hal_btcoex_mci_state(sc->sc_ah,
		    HAL_MCI_STATE_SET_BT_AWAKE, NULL);
	}

	/* Processing SYS_WAKING/SYS_SLEEPING */
	if (mciIntRxMsg & HAL_MCI_INTERRUPT_RX_MSG_SYS_WAKING) {
		mciIntRxMsg &= ~HAL_MCI_INTERRUPT_RX_MSG_SYS_WAKING;
		if (ath_hal_btcoex_mci_state(sc->sc_ah, HAL_MCI_STATE_BT,
		    NULL) == MCI_BT_SLEEP) {
			if (ath_hal_btcoex_mci_state(sc->sc_ah,
			    HAL_MCI_STATE_REMOTE_SLEEP, NULL) == MCI_BT_SLEEP) {
				DPRINTF(sc, ATH_DEBUG_BTCOEX,
				    "(MCI) 2. BT stays in SLEEP mode.\n");
			} else {
				DPRINTF(sc, ATH_DEBUG_BTCOEX,
				    "(MCI) 2. Set BT state to AWAKE.\n");
				ath_hal_btcoex_mci_state(sc->sc_ah,
				    HAL_MCI_STATE_SET_BT_AWAKE, NULL);
			}
		} else {
			DPRINTF(sc, ATH_DEBUG_BTCOEX,
			    "(MCI) 2. BT stays in AWAKE mode.\n");
		}
	}

	if (mciIntRxMsg & HAL_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING) {
		mciIntRxMsg &= ~HAL_MCI_INTERRUPT_RX_MSG_SYS_SLEEPING;
		if (ath_hal_btcoex_mci_state(sc->sc_ah, HAL_MCI_STATE_BT,
		    NULL) == MCI_BT_AWAKE) {
			if (ath_hal_btcoex_mci_state(sc->sc_ah,
			    HAL_MCI_STATE_REMOTE_SLEEP, NULL) == MCI_BT_AWAKE) {
				DPRINTF(sc, ATH_DEBUG_BTCOEX,
				    "(MCI) 3. BT stays in AWAKE mode.\n");
			} else {
				DPRINTF(sc, ATH_DEBUG_BTCOEX,
				    "(MCI) 3. Set BT state to SLEEP.\n");
				ath_hal_btcoex_mci_state(sc->sc_ah,
				    HAL_MCI_STATE_SET_BT_SLEEP, NULL);
			}
		} else {
			DPRINTF(sc, ATH_DEBUG_BTCOEX,
			    "(MCI) 3. BT stays in SLEEP mode.\n");
		}
	}

	/*
	 * Recover from out-of-order / wrong-offset GPM messages.
	 */
	if ((mciInt & HAL_MCI_INTERRUPT_RX_INVALID_HDR) ||
	    (mciInt & HAL_MCI_INTERRUPT_CONT_INFO_TIMEOUT)) {
		DPRINTF(sc, ATH_DEBUG_BTCOEX,
		    "(MCI) MCI RX broken, skip GPM messages\n");
		ath_hal_btcoex_mci_state(sc->sc_ah,
		    HAL_MCI_STATE_RECOVER_RX, NULL);
		skip_gpm = true;
	}

	if (mciIntRxMsg & HAL_MCI_INTERRUPT_RX_MSG_SCHD_INFO) {
		mciIntRxMsg &= ~HAL_MCI_INTERRUPT_RX_MSG_SCHD_INFO;
		offset = ath_hal_btcoex_mci_state(sc->sc_ah,
		    HAL_MCI_STATE_LAST_SCHD_MSG_OFFSET, NULL);
	}

	/*
	 * Parse GPM messages.
	 */
	if (mciIntRxMsg & HAL_MCI_INTERRUPT_RX_MSG_GPM) {
		mciIntRxMsg &= ~HAL_MCI_INTERRUPT_RX_MSG_GPM;

		while (more_data == HAL_MCI_GPM_MORE) {
			pGpm = (void *) sc->sc_btcoex.gpm_buf;
			offset = ath_hal_btcoex_mci_state(sc->sc_ah,
			    HAL_MCI_STATE_NEXT_GPM_OFFSET, &more_data);

			if (offset == HAL_MCI_GPM_INVALID)
				break;
			pGpm += (offset >> 2);
			/*
			 * The first DWORD is a timer.
			 * The real data starts from the second DWORD.
			 */
			subtype = MCI_GPM_TYPE(pGpm);
			opcode = MCI_GPM_OPCODE(pGpm);

			if (!skip_gpm) {
				if (MCI_GPM_IS_CAL_TYPE(subtype)) {
					ath_btcoex_mci_cal_msg(sc, subtype,
					    (uint8_t*) pGpm);
				} else {
					switch (subtype) {
					case MCI_GPM_COEX_AGENT:
						ath_btcoex_mci_coex_msg(sc,
						    opcode, (uint8_t*) pGpm);
					break;
					case MCI_GPM_BT_DEBUG:
						device_printf(sc->sc_dev,
						    "(MCI) TODO: GPM_BT_DEBUG!\n");
					break;
					default:
						DPRINTF(sc, ATH_DEBUG_BTCOEX,
						    "(MCI) Unknown GPM message.\n");
						break;
					}
				}
			}
			MCI_GPM_RECYCLE(pGpm);
		}
	}

	/*
	 * This is monitoring/management information messages, so the driver
	 * layer can hook in and dynamically adjust things like aggregation
	 * size, expected bluetooth/wifi traffic throughput, etc.
	 *
	 * None of that is done right now; it just passes off the values
	 * to the HAL so it can update its internal state as appropriate.
	 * This code just prints out the values for debugging purposes.
	 */
	if (mciIntRxMsg & HAL_MCI_INTERRUPT_RX_MSG_MONITOR) {
		if (mciIntRxMsg & HAL_MCI_INTERRUPT_RX_MSG_LNA_CONTROL) {
			mciIntRxMsg &= ~HAL_MCI_INTERRUPT_RX_MSG_LNA_CONTROL;
			DPRINTF(sc, ATH_DEBUG_BTCOEX, "(MCI) LNA_CONTROL\n");
		}
		if (mciIntRxMsg & HAL_MCI_INTERRUPT_RX_MSG_LNA_INFO) {
			mciIntRxMsg &= ~HAL_MCI_INTERRUPT_RX_MSG_LNA_INFO;
			DPRINTF(sc, ATH_DEBUG_BTCOEX, "(MCI) LNA_INFO\n");
		}
		if (mciIntRxMsg & HAL_MCI_INTERRUPT_RX_MSG_CONT_INFO) {
			value_dbm = ath_hal_btcoex_mci_state(sc->sc_ah,
			    HAL_MCI_STATE_CONT_RSSI_POWER, NULL);

			mciIntRxMsg &= ~HAL_MCI_INTERRUPT_RX_MSG_CONT_INFO;
			if (ath_hal_btcoex_mci_state(sc->sc_ah,
			    HAL_MCI_STATE_CONT_TXRX, NULL)) {
				DPRINTF(sc, ATH_DEBUG_BTCOEX,
				    "(MCI) CONT_INFO: (tx) pri = %d, pwr = %d dBm\n",
				ath_hal_btcoex_mci_state(sc->sc_ah,
				    HAL_MCI_STATE_CONT_PRIORITY, NULL),
				    value_dbm);
			} else {
				DPRINTF(sc, ATH_DEBUG_BTCOEX,
				    "(MCI) CONT_INFO: (rx) pri = %d, rssi = %d dBm\n",
				ath_hal_btcoex_mci_state(sc->sc_ah,
				    HAL_MCI_STATE_CONT_PRIORITY, NULL),
				    value_dbm);
			}
		}
		if (mciIntRxMsg & HAL_MCI_INTERRUPT_RX_MSG_CONT_NACK) {
			mciIntRxMsg &= ~HAL_MCI_INTERRUPT_RX_MSG_CONT_NACK;
			DPRINTF(sc, ATH_DEBUG_BTCOEX, "(MCI) CONT_NACK\n");
		}
		if (mciIntRxMsg & HAL_MCI_INTERRUPT_RX_MSG_CONT_RST) {
			mciIntRxMsg &= ~HAL_MCI_INTERRUPT_RX_MSG_CONT_RST;
			DPRINTF(sc, ATH_DEBUG_BTCOEX, "(MCI) CONT_RST\n");
		}
	}

	/*
	 * Recover the state engine if we hit an invalid header/timeout.
	 * This is the final part of GPT out-of-sync recovery.
	 */
	if ((mciInt & HAL_MCI_INTERRUPT_RX_INVALID_HDR) ||
	    (mciInt & HAL_MCI_INTERRUPT_CONT_INFO_TIMEOUT)) {
		ath_btcoex_mci_event(sc, ATH_COEX_EVENT_BT_NOOP, NULL);
		mciInt &= ~(HAL_MCI_INTERRUPT_RX_INVALID_HDR |
		    HAL_MCI_INTERRUPT_CONT_INFO_TIMEOUT);
	}

	if (mciIntRxMsg & 0xfffffffe) {
		DPRINTF(sc, ATH_DEBUG_BTCOEX,
		    "(MCI) Not processed IntRxMsg = 0x%x\n", mciIntRxMsg);
	}
}
