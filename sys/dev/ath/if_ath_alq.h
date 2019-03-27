/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Adrian Chadd
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
#ifndef	__IF_ATH_ALQ_H__
#define	__IF_ATH_ALQ_H__

#define	ATH_ALQ_INIT_STATE		1
struct if_ath_alq_init_state {
	uint32_t	sc_mac_version;
	uint32_t	sc_mac_revision;
	uint32_t	sc_phy_rev;
	uint32_t	sc_hal_magic;
};

#define	ATH_ALQ_EDMA_TXSTATUS		2
#define	ATH_ALQ_EDMA_RXSTATUS		3
#define	ATH_ALQ_EDMA_TXDESC		4

#define	ATH_ALQ_TDMA_BEACON_STATE	5
struct if_ath_alq_tdma_beacon_state {
	uint64_t	rx_tsf;		/* RX TSF of beacon frame */
	uint64_t	beacon_tsf;	/* TSF inside beacon frame */
	uint64_t	tsf64;
	uint64_t	nextslot_tsf;
	uint32_t	nextslot_tu;
	uint32_t	txtime;
};

#define	ATH_ALQ_TDMA_TIMER_CONFIG	6
struct if_ath_alq_tdma_timer_config {
	uint32_t	tdma_slot;
	uint32_t	tdma_slotlen;
	uint32_t	tdma_slotcnt;
	uint32_t	tdma_bintval;
	uint32_t	tdma_guard;
	uint32_t	tdma_scbintval;
	uint32_t	tdma_dbaprep;
};

#define	ATH_ALQ_TDMA_SLOT_CALC		7
struct if_ath_alq_tdma_slot_calc {
	uint64_t	nexttbtt;
	uint64_t	next_slot;
	int32_t		tsfdelta;
	int32_t		avg_plus;
	int32_t		avg_minus;
};

#define	ATH_ALQ_TDMA_TSF_ADJUST		8
struct if_ath_alq_tdma_tsf_adjust {
	uint64_t	tsf64_old;
	uint64_t	tsf64_new;
	int32_t		tsfdelta;
};

#define	ATH_ALQ_TDMA_TIMER_SET		9
struct if_ath_alq_tdma_timer_set {
	uint32_t	bt_intval;
	uint32_t	bt_nexttbtt;
	uint32_t	bt_nextdba;
	uint32_t	bt_nextswba;
	uint32_t	bt_nextatim;
	uint32_t	bt_flags;
	uint32_t	sc_tdmadbaprep;
	uint32_t	sc_tdmaswbaprep;
};

#define	ATH_ALQ_INTR_STATUS		10
struct if_ath_alq_interrupt {
	uint32_t	intr_status;
	uint32_t	intr_state[8];
	uint32_t	intr_syncstate;
};

#define	ATH_ALQ_MIB_COUNTERS		11
struct if_ath_alq_mib_counters {
	uint32_t	valid;
	uint32_t	tx_busy;
	uint32_t	rx_busy;
	uint32_t	chan_busy;
	uint32_t	ext_chan_busy;
	uint32_t	cycle_count;
};

#define	ATH_ALQ_MISSED_BEACON		12
#define	ATH_ALQ_STUCK_BEACON		13
#define	ATH_ALQ_RESUME_BEACON		14

#define	ATH_ALQ_TX_FIFO_PUSH		15
struct if_ath_alq_tx_fifo_push {
	uint32_t	txq;
	uint32_t	nframes;
	uint32_t	fifo_depth;
	uint32_t	frame_cnt;
};

/*
 * These will always be logged, regardless.
 */
#define	ATH_ALQ_LOG_ALWAYS_MASK		0x00000001

#define	ATH_ALQ_FILENAME_LEN	128
#define	ATH_ALQ_DEVNAME_LEN	32

struct if_ath_alq {
	uint32_t	sc_alq_debug;		/* Debug flags to report */
	struct alq *	sc_alq_alq;		/* alq state */
	unsigned int	sc_alq_qsize;		/* queue size */
	unsigned int	sc_alq_numlost;		/* number of "lost" entries */
	int		sc_alq_isactive;
	char		sc_alq_devname[ATH_ALQ_DEVNAME_LEN];
	char		sc_alq_filename[ATH_ALQ_FILENAME_LEN];
	struct if_ath_alq_init_state sc_alq_cfg;
};

/* 128 bytes in total */
#define	ATH_ALQ_PAYLOAD_LEN		112

struct if_ath_alq_hdr {
	uint64_t	threadid;
	uint32_t	tstamp_sec;
	uint32_t	tstamp_usec;
	uint16_t	op;
	uint16_t	len;	/* Length of (optional) payload */
};

struct if_ath_alq_payload {
	struct if_ath_alq_hdr hdr;
	char		payload[];
};

#ifdef	_KERNEL
static inline int
if_ath_alq_checkdebug(struct if_ath_alq *alq, uint16_t op)
{

	return ((alq->sc_alq_debug | ATH_ALQ_LOG_ALWAYS_MASK)
	    & (1 << (op - 1)));
}

extern	void if_ath_alq_init(struct if_ath_alq *alq, const char *devname);
extern	void if_ath_alq_setcfg(struct if_ath_alq *alq, uint32_t macVer,
	    uint32_t macRev, uint32_t phyRev, uint32_t halMagic);
extern	void if_ath_alq_tidyup(struct if_ath_alq *alq);
extern	int if_ath_alq_start(struct if_ath_alq *alq);
extern	int if_ath_alq_stop(struct if_ath_alq *alq);
extern	void if_ath_alq_post(struct if_ath_alq *alq, uint16_t op,
	    uint16_t len, const char *buf);

/* XXX maybe doesn't belong here? */
static inline void
if_ath_alq_post_intr(struct if_ath_alq *alq, uint32_t status,
    uint32_t *state, uint32_t sync_state)
{
	int i;
	struct if_ath_alq_interrupt intr;

	if (! if_ath_alq_checkdebug(alq, ATH_ALQ_INTR_STATUS))
		return;

	intr.intr_status = htobe32(status);
	for (i = 0; i < 8; i++)
		intr.intr_state[i] = htobe32(state[i]);
	intr.intr_syncstate = htobe32(sync_state);

	if_ath_alq_post(alq, ATH_ALQ_INTR_STATUS, sizeof(intr),
	    (const char *) &intr);
}

#endif	/* _KERNEL */

#endif
