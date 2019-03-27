/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for the Atheros Wireless LAN controller.
 *
 * This software is derived from work of Atsushi Onoe; his contribution
 * is greatly appreciated.
 */

#include "opt_inet.h"
#include "opt_ath.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/errno.h>
#include <sys/callout.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kthread.h>
#include <sys/taskqueue.h>
#include <sys/priv.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_llc.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_regdomain.h>
#ifdef IEEE80211_SUPPORT_SUPERG
#include <net80211/ieee80211_superg.h>
#endif
#ifdef IEEE80211_SUPPORT_TDMA
#include <net80211/ieee80211_tdma.h>
#endif

#include <net/bpf.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <dev/ath/if_athvar.h>
#include <dev/ath/ath_hal/ah_devid.h>		/* XXX for softled */
#include <dev/ath/ath_hal/ah_diagcodes.h>

#include <dev/ath/if_ath_debug.h>
#include <dev/ath/if_ath_led.h>
#include <dev/ath/if_ath_misc.h>
#include <dev/ath/if_ath_tx.h>
#include <dev/ath/if_ath_sysctl.h>

#ifdef ATH_TX99_DIAG
#include <dev/ath/ath_tx99/ath_tx99.h>
#endif

#ifdef	ATH_DEBUG_ALQ
#include <dev/ath/if_ath_alq.h>
#endif

static int
ath_sysctl_slottime(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	u_int slottime;
	int error;

	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	slottime = ath_hal_getslottime(sc->sc_ah);
	ATH_UNLOCK(sc);

	error = sysctl_handle_int(oidp, &slottime, 0, req);
	if (error || !req->newptr)
		goto finish;

	error = !ath_hal_setslottime(sc->sc_ah, slottime) ? EINVAL : 0;

finish:
	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

	return error;
}

static int
ath_sysctl_acktimeout(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	u_int acktimeout;
	int error;

	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	acktimeout = ath_hal_getacktimeout(sc->sc_ah);
	ATH_UNLOCK(sc);

	error = sysctl_handle_int(oidp, &acktimeout, 0, req);
	if (error || !req->newptr)
		goto finish;

	error = !ath_hal_setacktimeout(sc->sc_ah, acktimeout) ? EINVAL : 0;

finish:
	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

	return (error);
}

static int
ath_sysctl_ctstimeout(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	u_int ctstimeout;
	int error;

	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	ctstimeout = ath_hal_getctstimeout(sc->sc_ah);
	ATH_UNLOCK(sc);

	error = sysctl_handle_int(oidp, &ctstimeout, 0, req);
	if (error || !req->newptr)
		goto finish;

	error = !ath_hal_setctstimeout(sc->sc_ah, ctstimeout) ? EINVAL : 0;

finish:
	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

	return (error);
}

static int
ath_sysctl_softled(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	int softled = sc->sc_softled;
	int error;

	error = sysctl_handle_int(oidp, &softled, 0, req);
	if (error || !req->newptr)
		return error;
	softled = (softled != 0);
	if (softled != sc->sc_softled) {
		if (softled) {
			/* NB: handle any sc_ledpin change */
			ath_led_config(sc);
		}
		sc->sc_softled = softled;
	}
	return 0;
}

static int
ath_sysctl_ledpin(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	int ledpin = sc->sc_ledpin;
	int error;

	error = sysctl_handle_int(oidp, &ledpin, 0, req);
	if (error || !req->newptr)
		return error;
	if (ledpin != sc->sc_ledpin) {
		sc->sc_ledpin = ledpin;
		if (sc->sc_softled) {
			ath_led_config(sc);
		}
	}
	return 0;
}

static int
ath_sysctl_hardled(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	int hardled = sc->sc_hardled;
	int error;

	error = sysctl_handle_int(oidp, &hardled, 0, req);
	if (error || !req->newptr)
		return error;
	hardled = (hardled != 0);
	if (hardled != sc->sc_hardled) {
		if (hardled) {
			/* NB: handle any sc_ledpin change */
			ath_led_config(sc);
		}
		sc->sc_hardled = hardled;
	}
	return 0;
}

static int
ath_sysctl_txantenna(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	u_int txantenna;
	int error;

	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	ATH_UNLOCK(sc);

	txantenna = ath_hal_getantennaswitch(sc->sc_ah);

	error = sysctl_handle_int(oidp, &txantenna, 0, req);
	if (!error && req->newptr) {
		/* XXX assumes 2 antenna ports */
		if (txantenna < HAL_ANT_VARIABLE || txantenna > HAL_ANT_FIXED_B) {
			error = EINVAL;
			goto finish;
		}
		ath_hal_setantennaswitch(sc->sc_ah, txantenna);
		/*
		 * NB: with the switch locked this isn't meaningful,
		 *     but set it anyway so things like radiotap get
		 *     consistent info in their data.
		 */
		sc->sc_txantenna = txantenna;
	}

finish:
	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

	return (error);
}

static int
ath_sysctl_rxantenna(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	u_int defantenna;
	int error;

	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	defantenna = ath_hal_getdefantenna(sc->sc_ah);
	ATH_UNLOCK(sc);

	error = sysctl_handle_int(oidp, &defantenna, 0, req);
	if (!error && req->newptr)
		ath_hal_setdefantenna(sc->sc_ah, defantenna);

	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

	return (error);
}

static int
ath_sysctl_diversity(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	u_int diversity;
	int error;

	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	ATH_UNLOCK(sc);

	diversity = ath_hal_getdiversity(sc->sc_ah);

	error = sysctl_handle_int(oidp, &diversity, 0, req);
	if (error || !req->newptr)
		goto finish;
	if (!ath_hal_setdiversity(sc->sc_ah, diversity)) {
		error = EINVAL;
		goto finish;
	}
	sc->sc_diversity = diversity;
	error = 0;

finish:
	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

	return (error);
}

static int
ath_sysctl_diag(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	u_int32_t diag;
	int error;

	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	ATH_UNLOCK(sc);

	if (!ath_hal_getdiag(sc->sc_ah, &diag)) {
		error = EINVAL;
		goto finish;
	}

	error = sysctl_handle_int(oidp, &diag, 0, req);
	if (error || !req->newptr)
		goto finish;
	error = !ath_hal_setdiag(sc->sc_ah, diag) ? EINVAL : 0;

finish:
	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

	return (error);
}

static int
ath_sysctl_tpscale(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	u_int32_t scale;
	int error;

	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	ATH_UNLOCK(sc);

	(void) ath_hal_gettpscale(sc->sc_ah, &scale);
	error = sysctl_handle_int(oidp, &scale, 0, req);
	if (error || !req->newptr)
		goto finish;

	error = !ath_hal_settpscale(sc->sc_ah, scale) ? EINVAL :
	    (sc->sc_running) ? ath_reset(sc, ATH_RESET_NOLOSS) : 0;

finish:
	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

	return (error);
}

static int
ath_sysctl_tpc(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	u_int tpc;
	int error;

	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	ATH_UNLOCK(sc);

	tpc = ath_hal_gettpc(sc->sc_ah);

	error = sysctl_handle_int(oidp, &tpc, 0, req);
	if (error || !req->newptr)
		goto finish;
	error = !ath_hal_settpc(sc->sc_ah, tpc) ? EINVAL : 0;

finish:
	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

	return (error);
}

static int
ath_sysctl_rfkill(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	struct ath_hal *ah = sc->sc_ah;
	u_int rfkill;
	int error;

	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	ATH_UNLOCK(sc);

	rfkill = ath_hal_getrfkill(ah);

	error = sysctl_handle_int(oidp, &rfkill, 0, req);
	if (error || !req->newptr)
		goto finish;
	if (rfkill == ath_hal_getrfkill(ah)) {	/* unchanged */
		error = 0;
		goto finish;
	}
	if (!ath_hal_setrfkill(ah, rfkill)) {
		error = EINVAL;
		goto finish;
	}
	error = sc->sc_running ? ath_reset(sc, ATH_RESET_FULL) : 0;

finish:
	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

	return (error);
}

static int
ath_sysctl_txagg(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	int i, t, param = 0;
	int error;
	struct ath_buf *bf;

	error = sysctl_handle_int(oidp, &param, 0, req);
	if (error || !req->newptr)
		return error;

	if (param != 1)
		return 0;

	printf("no tx bufs (empty list): %d\n", sc->sc_stats.ast_tx_getnobuf);
	printf("no tx bufs (was busy): %d\n", sc->sc_stats.ast_tx_getbusybuf);

	printf("aggr single packet: %d\n",
	    sc->sc_aggr_stats.aggr_single_pkt);
	printf("aggr single packet w/ BAW closed: %d\n",
	    sc->sc_aggr_stats.aggr_baw_closed_single_pkt);
	printf("aggr non-baw packet: %d\n",
	    sc->sc_aggr_stats.aggr_nonbaw_pkt);
	printf("aggr aggregate packet: %d\n",
	    sc->sc_aggr_stats.aggr_aggr_pkt);
	printf("aggr single packet low hwq: %d\n",
	    sc->sc_aggr_stats.aggr_low_hwq_single_pkt);
	printf("aggr single packet RTS aggr limited: %d\n",
	    sc->sc_aggr_stats.aggr_rts_aggr_limited);
	printf("aggr sched, no work: %d\n",
	    sc->sc_aggr_stats.aggr_sched_nopkt);
	for (i = 0; i < 64; i++) {
		printf("%2d: %10d ", i, sc->sc_aggr_stats.aggr_pkts[i]);
		if (i % 4 == 3)
			printf("\n");
	}
	printf("\n");

	for (i = 0; i < HAL_NUM_TX_QUEUES; i++) {
		if (ATH_TXQ_SETUP(sc, i)) {
			printf("HW TXQ %d: axq_depth=%d, axq_aggr_depth=%d, "
			    "axq_fifo_depth=%d, holdingbf=%p\n",
			    i,
			    sc->sc_txq[i].axq_depth,
			    sc->sc_txq[i].axq_aggr_depth,
			    sc->sc_txq[i].axq_fifo_depth,
			    sc->sc_txq[i].axq_holdingbf);
		}
	}

	i = t = 0;
	ATH_TXBUF_LOCK(sc);
	TAILQ_FOREACH(bf, &sc->sc_txbuf, bf_list) {
		if (bf->bf_flags & ATH_BUF_BUSY) {
			printf("Busy: %d\n", t);
			i++;
		}
		t++;
	}
	ATH_TXBUF_UNLOCK(sc);
	printf("Total TX buffers: %d; Total TX buffers busy: %d (%d)\n",
	    t, i, sc->sc_txbuf_cnt);

	i = t = 0;
	ATH_TXBUF_LOCK(sc);
	TAILQ_FOREACH(bf, &sc->sc_txbuf_mgmt, bf_list) {
		if (bf->bf_flags & ATH_BUF_BUSY) {
			printf("Busy: %d\n", t);
			i++;
		}
		t++;
	}
	ATH_TXBUF_UNLOCK(sc);
	printf("Total mgmt TX buffers: %d; Total mgmt TX buffers busy: %d\n",
	    t, i);

	ATH_RX_LOCK(sc);
	for (i = 0; i < 2; i++) {
		printf("%d: fifolen: %d/%d; head=%d; tail=%d; m_pending=%p, m_holdbf=%p\n",
		    i,
		    sc->sc_rxedma[i].m_fifo_depth,
		    sc->sc_rxedma[i].m_fifolen,
		    sc->sc_rxedma[i].m_fifo_head,
		    sc->sc_rxedma[i].m_fifo_tail,
		    sc->sc_rxedma[i].m_rxpending,
		    sc->sc_rxedma[i].m_holdbf);
	}
	i = 0;
	TAILQ_FOREACH(bf, &sc->sc_rxbuf, bf_list) {
		i++;
	}
	printf("Total RX buffers in free list: %d buffers\n",
	    i);
	ATH_RX_UNLOCK(sc);

	return 0;
}

static int
ath_sysctl_rfsilent(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	u_int rfsilent;
	int error;

	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	ATH_UNLOCK(sc);

	(void) ath_hal_getrfsilent(sc->sc_ah, &rfsilent);
	error = sysctl_handle_int(oidp, &rfsilent, 0, req);
	if (error || !req->newptr)
		goto finish;
	if (!ath_hal_setrfsilent(sc->sc_ah, rfsilent)) {
		error = EINVAL;
		goto finish;
	}
	/*
	 * Earlier chips (< AR5212) have up to 8 GPIO
	 * pins exposed.
	 *
	 * AR5416 and later chips have many more GPIO
	 * pins (up to 16) so the mask is expanded to
	 * four bits.
	 */
	sc->sc_rfsilentpin = rfsilent & 0x3c;
	sc->sc_rfsilentpol = (rfsilent & 0x2) != 0;
	error = 0;

finish:
	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

	return (error);
}

static int
ath_sysctl_tpack(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	u_int32_t tpack;
	int error;

	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	ATH_UNLOCK(sc);

	(void) ath_hal_gettpack(sc->sc_ah, &tpack);
	error = sysctl_handle_int(oidp, &tpack, 0, req);
	if (error || !req->newptr)
		goto finish;
	error = !ath_hal_settpack(sc->sc_ah, tpack) ? EINVAL : 0;

finish:
	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

	return (error);
}

static int
ath_sysctl_tpcts(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	u_int32_t tpcts;
	int error;

	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	ATH_UNLOCK(sc);

	(void) ath_hal_gettpcts(sc->sc_ah, &tpcts);
	error = sysctl_handle_int(oidp, &tpcts, 0, req);
	if (error || !req->newptr)
		goto finish;

	error = !ath_hal_settpcts(sc->sc_ah, tpcts) ? EINVAL : 0;

finish:
	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

	return (error);
}

static int
ath_sysctl_intmit(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	int intmit, error;

	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	ATH_UNLOCK(sc);

	intmit = ath_hal_getintmit(sc->sc_ah);
	error = sysctl_handle_int(oidp, &intmit, 0, req);
	if (error || !req->newptr)
		goto finish;

	/* reusing error; 1 here means "good"; 0 means "fail" */
	error = ath_hal_setintmit(sc->sc_ah, intmit);
	if (! error) {
		error = EINVAL;
		goto finish;
	}

	/*
	 * Reset the hardware here - disabling ANI in the HAL
	 * doesn't reset ANI related registers, so it'll leave
	 * things in an inconsistent state.
	 */
	if (sc->sc_running)
		ath_reset(sc, ATH_RESET_NOLOSS);

	error = 0;

finish:
	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

	return (error);
}

#ifdef IEEE80211_SUPPORT_TDMA
static int
ath_sysctl_setcca(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	int setcca, error;

	setcca = sc->sc_setcca;
	error = sysctl_handle_int(oidp, &setcca, 0, req);
	if (error || !req->newptr)
		return error;
	sc->sc_setcca = (setcca != 0);
	return 0;
}
#endif /* IEEE80211_SUPPORT_TDMA */

static int
ath_sysctl_forcebstuck(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	int val = 0;
	int error;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr)
		return error;
	if (val == 0)
		return 0;

	taskqueue_enqueue(sc->sc_tq, &sc->sc_bstucktask);
	val = 0;
	return 0;
}

static int
ath_sysctl_hangcheck(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	int val = 0;
	int error;
	uint32_t mask = 0xffffffff;
	uint32_t *sp;
	uint32_t rsize;
	struct ath_hal *ah = sc->sc_ah;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr)
		return error;
	if (val == 0)
		return 0;

	ATH_LOCK(sc);
	ath_power_set_power_state(sc, HAL_PM_AWAKE);
	ATH_UNLOCK(sc);

	/* Do a hang check */
	if (!ath_hal_getdiagstate(ah, HAL_DIAG_CHECK_HANGS,
	    &mask, sizeof(mask),
	    (void *) &sp, &rsize)) {
		error = 0;
		goto finish;
	}

	device_printf(sc->sc_dev, "%s: sp=0x%08x\n", __func__, *sp);

	val = 0;
	error = 0;
finish:
	ATH_LOCK(sc);
	ath_power_restore_power_state(sc);
	ATH_UNLOCK(sc);

	return (error);
}

#ifdef ATH_DEBUG_ALQ
static int
ath_sysctl_alq_log(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	int error, enable;

	enable = (sc->sc_alq.sc_alq_isactive);

	error = sysctl_handle_int(oidp, &enable, 0, req);
	if (error || !req->newptr)
		return (error);
	else if (enable)
		error = if_ath_alq_start(&sc->sc_alq);
	else
		error = if_ath_alq_stop(&sc->sc_alq);
	return (error);
}

/*
 * Attach the ALQ debugging if required.
 */
static void
ath_sysctl_alq_attach(struct ath_softc *sc)
{
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->sc_dev);
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->sc_dev);
	struct sysctl_oid_list *child = SYSCTL_CHILDREN(tree);

	tree = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "alq", CTLFLAG_RD,
	    NULL, "Atheros ALQ logging parameters");
	child = SYSCTL_CHILDREN(tree);

	SYSCTL_ADD_STRING(ctx, child, OID_AUTO, "filename",
	    CTLFLAG_RW, sc->sc_alq.sc_alq_filename, 0, "ALQ filename");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"enable", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		ath_sysctl_alq_log, "I", "");

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"debugmask", CTLFLAG_RW, &sc->sc_alq.sc_alq_debug, 0,
		"ALQ debug mask");

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"numlost", CTLFLAG_RW, &sc->sc_alq.sc_alq_numlost, 0,
		"number lost");
}
#endif /* ATH_DEBUG_ALQ */

void
ath_sysctlattach(struct ath_softc *sc)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->sc_dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->sc_dev);
	struct ath_hal *ah = sc->sc_ah;

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"countrycode", CTLFLAG_RD, &sc->sc_eecc, 0,
		"EEPROM country code");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"regdomain", CTLFLAG_RD, &sc->sc_eerd, 0,
		"EEPROM regdomain code");
#ifdef	ATH_DEBUG
	SYSCTL_ADD_QUAD(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"debug", CTLFLAG_RW, &sc->sc_debug,
		"control debugging printfs");
#endif
#ifdef	ATH_DEBUG_ALQ
	SYSCTL_ADD_QUAD(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"ktrdebug", CTLFLAG_RW, &sc->sc_ktrdebug,
		"control debugging KTR");
#endif /* ATH_DEBUG_ALQ */
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"slottime", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		ath_sysctl_slottime, "I", "802.11 slot time (us)");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"acktimeout", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		ath_sysctl_acktimeout, "I", "802.11 ACK timeout (us)");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"ctstimeout", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		ath_sysctl_ctstimeout, "I", "802.11 CTS timeout (us)");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"softled", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		ath_sysctl_softled, "I", "enable/disable software LED support");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"ledpin", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		ath_sysctl_ledpin, "I", "GPIO pin connected to LED");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"ledon", CTLFLAG_RW, &sc->sc_ledon, 0,
		"setting to turn LED on");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"ledidle", CTLFLAG_RW, &sc->sc_ledidle, 0,
		"idle time for inactivity LED (ticks)");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"hardled", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		ath_sysctl_hardled, "I", "enable/disable hardware LED support");
	/* XXX Laziness - configure pins, then flip hardled off/on */
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"led_net_pin", CTLFLAG_RW, &sc->sc_led_net_pin, 0,
		"MAC Network LED pin, or -1 to disable");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"led_pwr_pin", CTLFLAG_RW, &sc->sc_led_pwr_pin, 0,
		"MAC Power LED pin, or -1 to disable");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"txantenna", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		ath_sysctl_txantenna, "I", "antenna switch");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"rxantenna", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		ath_sysctl_rxantenna, "I", "default/rx antenna");
	if (ath_hal_hasdiversity(ah))
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"diversity", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
			ath_sysctl_diversity, "I", "antenna diversity");
	sc->sc_txintrperiod = ATH_TXINTR_PERIOD;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"txintrperiod", CTLFLAG_RW, &sc->sc_txintrperiod, 0,
		"tx descriptor batching");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"diag", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		ath_sysctl_diag, "I", "h/w diagnostic control");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"tpscale", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		ath_sysctl_tpscale, "I", "tx power scaling");
	if (ath_hal_hastpc(ah)) {
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"tpc", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
			ath_sysctl_tpc, "I", "enable/disable per-packet TPC");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"tpack", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
			ath_sysctl_tpack, "I", "tx power for ack frames");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"tpcts", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
			ath_sysctl_tpcts, "I", "tx power for cts frames");
	}
	if (ath_hal_hasrfsilent(ah)) {
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"rfsilent", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
			ath_sysctl_rfsilent, "I", "h/w RF silent config");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"rfkill", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
			ath_sysctl_rfkill, "I", "enable/disable RF kill switch");
	}

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"txagg", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		ath_sysctl_txagg, "I", "");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"forcebstuck", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		ath_sysctl_forcebstuck, "I", "");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"hangcheck", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		ath_sysctl_hangcheck, "I", "");

	if (ath_hal_hasintmit(ah)) {
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"intmit", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
			ath_sysctl_intmit, "I", "interference mitigation");
	}
	sc->sc_monpass = HAL_RXERR_DECRYPT | HAL_RXERR_MIC;
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"monpass", CTLFLAG_RW, &sc->sc_monpass, 0,
		"mask of error frames to pass when monitoring");

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"hwq_limit_nonaggr", CTLFLAG_RW, &sc->sc_hwq_limit_nonaggr, 0,
		"Hardware non-AMPDU queue depth before software-queuing TX frames");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"hwq_limit_aggr", CTLFLAG_RW, &sc->sc_hwq_limit_aggr, 0,
		"Hardware AMPDU queue depth before software-queuing TX frames");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"tid_hwq_lo", CTLFLAG_RW, &sc->sc_tid_hwq_lo, 0,
		"");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"tid_hwq_hi", CTLFLAG_RW, &sc->sc_tid_hwq_hi, 0,
		"");

	/* Aggregate length twiddles */
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"aggr_limit", CTLFLAG_RW, &sc->sc_aggr_limit, 0,
		"Maximum A-MPDU size, or 0 for 'default'");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"rts_aggr_limit", CTLFLAG_RW, &sc->sc_rts_aggr_limit, 0,
		"Maximum A-MPDU size for RTS-protected frames, or '0' "
		"for default");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"delim_min_pad", CTLFLAG_RW, &sc->sc_delim_min_pad, 0,
		"Enforce a minimum number of delimiters per A-MPDU "
		" sub-frame");

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"txq_data_minfree", CTLFLAG_RW, &sc->sc_txq_data_minfree,
		0, "Minimum free buffers before adding a data frame"
		" to the TX queue");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"txq_mcastq_maxdepth", CTLFLAG_RW,
		&sc->sc_txq_mcastq_maxdepth, 0,
		"Maximum buffer depth for multicast/broadcast frames");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"txq_node_maxdepth", CTLFLAG_RW,
		&sc->sc_txq_node_maxdepth, 0,
		"Maximum buffer depth for a single node");

#if 0
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"cabq_enable", CTLFLAG_RW,
		&sc->sc_cabq_enable, 0,
		"Whether to transmit on the CABQ or not");
#endif

#ifdef IEEE80211_SUPPORT_TDMA
	if (ath_hal_macversion(ah) > 0x78) {
		sc->sc_tdmadbaprep = 2;
		SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"dbaprep", CTLFLAG_RW, &sc->sc_tdmadbaprep, 0,
			"TDMA DBA preparation time");
		sc->sc_tdmaswbaprep = 10;
		SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"swbaprep", CTLFLAG_RW, &sc->sc_tdmaswbaprep, 0,
			"TDMA SWBA preparation time");
		SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"guardtime", CTLFLAG_RW, &sc->sc_tdmaguard, 0,
			"TDMA slot guard time");
		SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"superframe", CTLFLAG_RD, &sc->sc_tdmabintval, 0,
			"TDMA calculated super frame");
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"setcca", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
			ath_sysctl_setcca, "I", "enable CCA control");
	}
#endif

#ifdef	ATH_DEBUG_ALQ
	ath_sysctl_alq_attach(sc);
#endif
}

static int
ath_sysctl_clearstats(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	int val = 0;
	int error;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr)
		return error;
	if (val == 0)
		return 0;       /* Not clearing the stats is still valid */
	memset(&sc->sc_stats, 0, sizeof(sc->sc_stats));
	memset(&sc->sc_aggr_stats, 0, sizeof(sc->sc_aggr_stats));
	memset(&sc->sc_intr_stats, 0, sizeof(sc->sc_intr_stats));

	val = 0;
	return 0;
}

static void
ath_sysctl_stats_attach_rxphyerr(struct ath_softc *sc, struct sysctl_oid_list *parent)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->sc_dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->sc_dev);
	struct sysctl_oid_list *child = SYSCTL_CHILDREN(tree);
	int i;
	char sn[8];

	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "rx_phy_err", CTLFLAG_RD, NULL, "Per-code RX PHY Errors");
	child = SYSCTL_CHILDREN(tree);
	for (i = 0; i < 64; i++) {
		snprintf(sn, sizeof(sn), "%d", i);
		SYSCTL_ADD_UINT(ctx, child, OID_AUTO, sn, CTLFLAG_RD, &sc->sc_stats.ast_rx_phy[i], 0, "");
	}
}

static void
ath_sysctl_stats_attach_intr(struct ath_softc *sc,
    struct sysctl_oid_list *parent)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->sc_dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->sc_dev);
	struct sysctl_oid_list *child = SYSCTL_CHILDREN(tree);
	int i;
	char sn[8];

	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "sync_intr",
	    CTLFLAG_RD, NULL, "Sync interrupt statistics");
	child = SYSCTL_CHILDREN(tree);
	for (i = 0; i < 32; i++) {
		snprintf(sn, sizeof(sn), "%d", i);
		SYSCTL_ADD_UINT(ctx, child, OID_AUTO, sn, CTLFLAG_RD,
		    &sc->sc_intr_stats.sync_intr[i], 0, "");
	}
}

void
ath_sysctl_stats_attach(struct ath_softc *sc)
{
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->sc_dev);
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->sc_dev);
	struct sysctl_oid_list *child = SYSCTL_CHILDREN(tree);
 
	/* Create "clear" node */
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "clear_stats", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
	    ath_sysctl_clearstats, "I", "clear stats");

	/* Create stats node */
	tree = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "stats", CTLFLAG_RD,
	    NULL, "Statistics");
	child = SYSCTL_CHILDREN(tree);

	/* This was generated from if_athioctl.h */

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_watchdog", CTLFLAG_RD,
	    &sc->sc_stats.ast_watchdog, 0, "device reset by watchdog");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_hardware", CTLFLAG_RD,
	    &sc->sc_stats.ast_hardware, 0, "fatal hardware error interrupts");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_bmiss", CTLFLAG_RD,
	    &sc->sc_stats.ast_bmiss, 0, "beacon miss interrupts");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_bmiss_phantom", CTLFLAG_RD,
	    &sc->sc_stats.ast_bmiss_phantom, 0, "beacon miss interrupts");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_bstuck", CTLFLAG_RD,
	    &sc->sc_stats.ast_bstuck, 0, "beacon stuck interrupts");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rxorn", CTLFLAG_RD,
	    &sc->sc_stats.ast_rxorn, 0, "rx overrun interrupts");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rxeol", CTLFLAG_RD,
	    &sc->sc_stats.ast_rxeol, 0, "rx eol interrupts");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_txurn", CTLFLAG_RD,
	    &sc->sc_stats.ast_txurn, 0, "tx underrun interrupts");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_mib", CTLFLAG_RD,
	    &sc->sc_stats.ast_mib, 0, "mib interrupts");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_intrcoal", CTLFLAG_RD,
	    &sc->sc_stats.ast_intrcoal, 0, "interrupts coalesced");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_packets", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_packets, 0, "packet sent on the interface");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_mgmt", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_mgmt, 0, "management frames transmitted");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_discard", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_discard, 0, "frames discarded prior to assoc");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_qstop", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_qstop, 0, "output stopped 'cuz no buffer");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_encap", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_encap, 0, "tx encapsulation failed");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_nonode", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_nonode, 0, "tx failed 'cuz no node");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_nombuf", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_nombuf, 0, "tx failed 'cuz no mbuf");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_nomcl", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_nomcl, 0, "tx failed 'cuz no cluster");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_linear", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_linear, 0, "tx linearized to cluster");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_nodata", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_nodata, 0, "tx discarded empty frame");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_busdma", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_busdma, 0, "tx failed for dma resrcs");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_xretries", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_xretries, 0, "tx failed 'cuz too many retries");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_fifoerr", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_fifoerr, 0, "tx failed 'cuz FIFO underrun");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_filtered", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_filtered, 0, "tx failed 'cuz xmit filtered");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_shortretry", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_shortretry, 0, "tx on-chip retries (short)");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_longretry", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_longretry, 0, "tx on-chip retries (long)");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_badrate", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_badrate, 0, "tx failed 'cuz bogus xmit rate");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_noack", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_noack, 0, "tx frames with no ack marked");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_rts", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_rts, 0, "tx frames with rts enabled");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_cts", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_cts, 0, "tx frames with cts enabled");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_shortpre", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_shortpre, 0, "tx frames with short preamble");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_altrate", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_altrate, 0, "tx frames with alternate rate");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_protect", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_protect, 0, "tx frames with protection");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_ctsburst", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_ctsburst, 0, "tx frames with cts and bursting");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_ctsext", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_ctsext, 0, "tx frames with cts extension");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_nombuf", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_nombuf, 0, "rx setup failed 'cuz no mbuf");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_busdma", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_busdma, 0, "rx setup failed for dma resrcs");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_orn", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_orn, 0, "rx failed 'cuz of desc overrun");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_crcerr", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_crcerr, 0, "rx failed 'cuz of bad CRC");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_fifoerr", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_fifoerr, 0, "rx failed 'cuz of FIFO overrun");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_badcrypt", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_badcrypt, 0, "rx failed 'cuz decryption");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_badmic", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_badmic, 0, "rx failed 'cuz MIC failure");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_phyerr", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_phyerr, 0, "rx failed 'cuz of PHY err");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_tooshort", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_tooshort, 0, "rx discarded 'cuz frame too short");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_toobig", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_toobig, 0, "rx discarded 'cuz frame too large");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_packets", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_packets, 0, "packet recv on the interface");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_mgt", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_mgt, 0, "management frames received");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_ctl", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_ctl, 0, "rx discarded 'cuz ctl frame");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_be_xmit", CTLFLAG_RD,
	    &sc->sc_stats.ast_be_xmit, 0, "beacons transmitted");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_be_nombuf", CTLFLAG_RD,
	    &sc->sc_stats.ast_be_nombuf, 0, "beacon setup failed 'cuz no mbuf");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_per_cal", CTLFLAG_RD,
	    &sc->sc_stats.ast_per_cal, 0, "periodic calibration calls");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_per_calfail", CTLFLAG_RD,
	    &sc->sc_stats.ast_per_calfail, 0, "periodic calibration failed");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_per_rfgain", CTLFLAG_RD,
	    &sc->sc_stats.ast_per_rfgain, 0, "periodic calibration rfgain reset");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rate_calls", CTLFLAG_RD,
	    &sc->sc_stats.ast_rate_calls, 0, "rate control checks");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rate_raise", CTLFLAG_RD,
	    &sc->sc_stats.ast_rate_raise, 0, "rate control raised xmit rate");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rate_drop", CTLFLAG_RD,
	    &sc->sc_stats.ast_rate_drop, 0, "rate control dropped xmit rate");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_ant_defswitch", CTLFLAG_RD,
	    &sc->sc_stats.ast_ant_defswitch, 0, "rx/default antenna switches");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_ant_txswitch", CTLFLAG_RD,
	    &sc->sc_stats.ast_ant_txswitch, 0, "tx antenna switches");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_cabq_xmit", CTLFLAG_RD,
	    &sc->sc_stats.ast_cabq_xmit, 0, "cabq frames transmitted");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_cabq_busy", CTLFLAG_RD,
	    &sc->sc_stats.ast_cabq_busy, 0, "cabq found busy");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_raw", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_raw, 0, "tx frames through raw api");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_ff_txok", CTLFLAG_RD,
	    &sc->sc_stats.ast_ff_txok, 0, "fast frames tx'd successfully");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_ff_txerr", CTLFLAG_RD,
	    &sc->sc_stats.ast_ff_txerr, 0, "fast frames tx'd w/ error");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_ff_rx", CTLFLAG_RD,
	    &sc->sc_stats.ast_ff_rx, 0, "fast frames rx'd");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_ff_flush", CTLFLAG_RD,
	    &sc->sc_stats.ast_ff_flush, 0, "fast frames flushed from staging q");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_qfull", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_qfull, 0, "tx dropped 'cuz of queue limit");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_nobuf", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_nobuf, 0, "tx dropped 'cuz no ath buffer");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tdma_update", CTLFLAG_RD,
	    &sc->sc_stats.ast_tdma_update, 0, "TDMA slot timing updates");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tdma_timers", CTLFLAG_RD,
	    &sc->sc_stats.ast_tdma_timers, 0, "TDMA slot update set beacon timers");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tdma_tsf", CTLFLAG_RD,
	    &sc->sc_stats.ast_tdma_tsf, 0, "TDMA slot update set TSF");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tdma_ack", CTLFLAG_RD,
	    &sc->sc_stats.ast_tdma_ack, 0, "TDMA tx failed 'cuz ACK required");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_raw_fail", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_raw_fail, 0, "raw tx failed 'cuz h/w down");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_nofrag", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_nofrag, 0, "tx dropped 'cuz no ath frag buffer");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_be_missed", CTLFLAG_RD,
	    &sc->sc_stats.ast_be_missed, 0, "number of -missed- beacons");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_ani_cal", CTLFLAG_RD,
	    &sc->sc_stats.ast_ani_cal, 0, "number of ANI polls");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_agg", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_agg, 0, "number of aggregate frames received");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_halfgi", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_halfgi, 0, "number of frames received with half-GI");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_2040", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_2040, 0, "number of HT/40 frames received");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_pre_crc_err", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_pre_crc_err, 0, "number of delimeter-CRC errors detected");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_post_crc_err", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_post_crc_err, 0, "number of post-delimiter CRC errors detected");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_decrypt_busy_err", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_decrypt_busy_err, 0, "number of frames received w/ busy decrypt engine");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_hi_rx_chain", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_hi_rx_chain, 0, "");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_htprotect", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_htprotect, 0, "HT tx frames with protection");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_hitqueueend", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_hitqueueend, 0, "RX hit queue end");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_timeout", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_timeout, 0, "TX Global Timeout");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_cst", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_cst, 0, "TX Carrier Sense Timeout");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_xtxop", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_xtxop, 0, "TX exceeded TXOP");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_timerexpired", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_timerexpired, 0, "TX exceeded TX_TIMER register");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_desccfgerr", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_desccfgerr, 0, "TX Descriptor Cfg Error");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_swretries", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_swretries, 0, "TX software retry count");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_swretrymax", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_swretrymax, 0, "TX software retry max reached");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_data_underrun", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_data_underrun, 0, "");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_delim_underrun", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_delim_underrun, 0, "");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_aggr_failall", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_aggr_failall, 0,
	    "Number of aggregate TX failures (whole frame)");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_aggr_ok", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_aggr_ok, 0,
	    "Number of aggregate TX OK completions (subframe)");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_aggr_fail", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_aggr_fail, 0,
	    "Number of aggregate TX failures (subframe)");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_intr", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_intr, 0, "RX interrupts");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_intr", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_intr, 0, "TX interrupts");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_mcastq_overflow",
	    CTLFLAG_RD, &sc->sc_stats.ast_tx_mcastq_overflow, 0,
	    "Number of multicast frames exceeding maximum mcast queue depth");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_keymiss", CTLFLAG_RD,
	    &sc->sc_stats.ast_rx_keymiss, 0, "");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_swfiltered", CTLFLAG_RD,
	    &sc->sc_stats.ast_tx_swfiltered, 0, "");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_nodeq_overflow",
	    CTLFLAG_RD, &sc->sc_stats.ast_tx_nodeq_overflow, 0,
	    "tx dropped 'cuz nodeq overflow");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_rx_stbc",
	    CTLFLAG_RD, &sc->sc_stats.ast_rx_stbc, 0,
	    "Number of STBC frames received");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_stbc",
	    CTLFLAG_RD, &sc->sc_stats.ast_tx_stbc, 0,
	    "Number of STBC frames transmitted");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "ast_tx_ldpc",
	    CTLFLAG_RD, &sc->sc_stats.ast_tx_ldpc, 0,
	    "Number of LDPC frames transmitted");
	
	/* Attach the RX phy error array */
	ath_sysctl_stats_attach_rxphyerr(sc, child);

	/* Attach the interrupt statistics array */
	ath_sysctl_stats_attach_intr(sc, child);
}

/*
 * This doesn't necessarily belong here (because it's HAL related, not
 * driver related).
 */
void
ath_sysctl_hal_attach(struct ath_softc *sc)
{
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->sc_dev);
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->sc_dev);
	struct sysctl_oid_list *child = SYSCTL_CHILDREN(tree);

	tree = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "hal", CTLFLAG_RD,
	    NULL, "Atheros HAL parameters");
	child = SYSCTL_CHILDREN(tree);

	sc->sc_ah->ah_config.ah_debug = 0;
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "debug", CTLFLAG_RW,
	    &sc->sc_ah->ah_config.ah_debug, 0, "Atheros HAL debugging printfs");

	sc->sc_ah->ah_config.ah_ar5416_biasadj = 0;
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "ar5416_biasadj", CTLFLAG_RW,
	    &sc->sc_ah->ah_config.ah_ar5416_biasadj, 0,
	    "Enable 2GHz AR5416 direction sensitivity bias adjust");

	sc->sc_ah->ah_config.ah_dma_beacon_response_time = 2;
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "dma_brt", CTLFLAG_RW,
	    &sc->sc_ah->ah_config.ah_dma_beacon_response_time, 0,
	    "Atheros HAL DMA beacon response time");

	sc->sc_ah->ah_config.ah_sw_beacon_response_time = 10;
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "sw_brt", CTLFLAG_RW,
	    &sc->sc_ah->ah_config.ah_sw_beacon_response_time, 0,
	    "Atheros HAL software beacon response time");

	sc->sc_ah->ah_config.ah_additional_swba_backoff = 0;
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "swba_backoff", CTLFLAG_RW,
	    &sc->sc_ah->ah_config.ah_additional_swba_backoff, 0,
	    "Atheros HAL additional SWBA backoff time");

	sc->sc_ah->ah_config.ah_force_full_reset = 0;
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "force_full_reset", CTLFLAG_RW,
	    &sc->sc_ah->ah_config.ah_force_full_reset, 0,
	    "Force full chip reset rather than a warm reset");

	/*
	 * This is initialised by the driver.
	 */
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "serialise_reg_war", CTLFLAG_RW,
	    &sc->sc_ah->ah_config.ah_serialise_reg_war, 0,
	    "Force register access serialisation");
}
