/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2004 Video54 Technologies, Inc.
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
#ifndef _ATH_RATECTRL_H_
#define _ATH_RATECTRL_H_

/*
 * Interface definitions for transmit rate control modules for the
 * Atheros driver.
 *
 * A rate control module is responsible for choosing the transmit rate
 * for each data frame.  Management+control frames are always sent at
 * a fixed rate.
 *
 * Only one module may be present at a time; the driver references
 * rate control interfaces by symbol name.  If multiple modules are
 * to be supported we'll need to switch to a registration-based scheme
 * as is currently done, for example, for authentication modules.
 *
 * An instance of the rate control module is attached to each device
 * at attach time and detached when the device is destroyed.  The module
 * may associate data with each device and each node (station).  Both
 * sets of storage are opaque except for the size of the per-node storage
 * which must be provided when the module is attached.
 *
 * The rate control module is notified for each state transition and
 * station association/reassociation.  Otherwise it is queried for a
 * rate for each outgoing frame and provided status from each transmitted
 * frame.  Any ancillary processing is the responsibility of the module
 * (e.g. if periodic processing is required then the module should setup
 * it's own timer).
 *
 * In addition to the transmit rate for each frame the module must also
 * indicate the number of attempts to make at the specified rate.  If this
 * number is != ATH_TXMAXTRY then an additional callback is made to setup
 * additional transmit state.  The rate control code is assumed to write
 * this additional data directly to the transmit descriptor.
 */
struct ath_softc;
struct ath_node;
struct ath_desc;

struct ath_ratectrl {
	size_t	arc_space;	/* space required for per-node state */
};
/*
 * Attach/detach a rate control module.
 */
struct ath_ratectrl *ath_rate_attach(struct ath_softc *);
void	ath_rate_detach(struct ath_ratectrl *);

#define	ATH_RC_NUM		4

#define	ATH_RC_DS_FLAG		0x01	/* dual-stream rate */
#define	ATH_RC_CW40_FLAG	0x02	/* use HT40 */
#define	ATH_RC_SGI_FLAG		0x04	/* use short-GI */
#define	ATH_RC_HT_FLAG		0x08	/* use HT */
#define	ATH_RC_RTSCTS_FLAG	0x10	/* enable RTS/CTS protection */
#define	ATH_RC_STBC_FLAG	0x20	/* enable STBC */
#define	ATH_RC_TS_FLAG		0x40	/* triple-stream rate */

struct ath_rc_series {
	uint8_t rix;		/* ratetable index, not rate code */
	uint8_t ratecode;	/* hardware rate code */
	uint8_t tries;
	uint8_t tx_power_cap;
	uint16_t flags;
	uint16_t max4msframelen;
};

/*
 * State storage handling.
 */
/*
 * Initialize per-node state already allocated for the specified
 * node; this space can be assumed initialized to zero.
 */
void	ath_rate_node_init(struct ath_softc *, struct ath_node *);
/*
 * Cleanup any per-node state prior to the node being reclaimed.
 */
void	ath_rate_node_cleanup(struct ath_softc *, struct ath_node *);
/*
 * Update rate control state on station associate/reassociate 
 * (when operating as an ap or for nodes discovered when operating
 * in ibss mode).
 */
void	ath_rate_newassoc(struct ath_softc *, struct ath_node *,
		int isNewAssociation);

/*
 * Transmit handling.
 */
/*
 * Return the four TX rate index and try counts for the current data packet.
 */
void	ath_rate_getxtxrates(struct ath_softc *sc, struct ath_node *an,
		uint8_t rix0, struct ath_rc_series *rc);

/*
 * Return the transmit info for a data packet.  If multi-rate state
 * is to be setup then try0 should contain a value other than ATH_TXMATRY
 * and ath_rate_setupxtxdesc will be called after deciding if the frame
 * can be transmitted with multi-rate retry.
 */
void	ath_rate_findrate(struct ath_softc *, struct ath_node *,
		int shortPreamble, size_t frameLen,
		u_int8_t *rix, int *try0, u_int8_t *txrate);
/*
 * Setup any extended (multi-rate) descriptor state for a data packet.
 * The rate index returned by ath_rate_findrate is passed back in.
 */
void	ath_rate_setupxtxdesc(struct ath_softc *, struct ath_node *,
		struct ath_desc *, int shortPreamble, u_int8_t rix);
/*
 * Update rate control state for a packet associated with the
 * supplied transmit descriptor.  The routine is invoked both
 * for packets that were successfully sent and for those that
 * failed (consult the descriptor for details).
 *
 * For A-MPDU frames, nframes and nbad indicate how many frames
 * were in the aggregate, and how many failed.
 */
struct ath_buf;
void	ath_rate_tx_complete(struct ath_softc *, struct ath_node *,
		const struct ath_rc_series *, const struct ath_tx_status *,
		int pktlen, int nframes, int nbad);

/*
 * Fetch the global rate control statistics.
 */
int	ath_rate_fetch_stats(struct ath_softc *sc, struct ath_rateioctl *rs);

/*
 * Fetch the per-node statistics.
 */
int	ath_rate_fetch_node_stats(struct ath_softc *sc, struct ath_node *an,
		struct ath_rateioctl *rs);

#endif /* _ATH_RATECTRL_H_ */
