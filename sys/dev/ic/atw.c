/*	$OpenBSD: atw.c,v 1.102 2024/10/06 01:12:15 jsg Exp $	*/
/*	$NetBSD: atw.c,v 1.69 2004/07/23 07:07:55 dyoung Exp $	*/

/*-
 * Copyright (c) 1998, 1999, 2000, 2002, 2003, 2004 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by David Young, by Jason R. Thorpe, and by Charles M. Hannum.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Device driver for the ADMtek ADM8211 802.11 MAC/BBP.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/time.h>
#include <sys/endian.h>

#include <net/if.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/ic/atwreg.h>
#include <dev/ic/rf3000reg.h>
#include <dev/ic/si4136reg.h>
#include <dev/ic/atwvar.h>
#include <dev/ic/smc93cx6var.h>

/* XXX TBD open questions
 *
 *
 * When should I set DSSS PAD in reg 0x15 of RF3000? In 1-2Mbps
 * modes only, or all modes (5.5-11 Mbps CCK modes, too?) Does the MAC
 * handle this for me?
 *
 */
/* device attachment
 *
 *    print TOFS[012]
 *
 * device initialization
 *
 *    clear ATW_FRCTL_MAXPSP to disable max power saving
 *    set ATW_TXBR_ALCUPDATE to enable ALC
 *    set TOFS[012]? (hope not)
 *    disable rx/tx
 *    set ATW_PAR_SWR (software reset)
 *    wait for ATW_PAR_SWR clear
 *    disable interrupts
 *    ack status register
 *    enable interrupts
 *
 * rx/tx initialization
 *
 *    disable rx/tx w/ ATW_NAR_SR, ATW_NAR_ST
 *    allocate and init descriptor rings
 *    write ATW_PAR_DSL (descriptor skip length)
 *    write descriptor base addrs: ATW_TDBD, ATW_TDBP, write ATW_RDB
 *    write ATW_NAR_SQ for one/both transmit descriptor rings
 *    write ATW_NAR_SQ for one/both transmit descriptor rings
 *    enable rx/tx w/ ATW_NAR_SR, ATW_NAR_ST
 *
 * rx/tx end
 *
 *    stop DMA
 *    disable rx/tx w/ ATW_NAR_SR, ATW_NAR_ST
 *    flush tx w/ ATW_NAR_HF
 *
 * scan
 *
 *    initialize rx/tx
 *
 * BSS join: (re)association response
 *
 *    set ATW_FRCTL_AID
 *
 * optimizations ???
 *
 */

#define ATW_REFSLAVE	/* slavishly do what the reference driver does */

int atw_bbp_io_enable_delay = 20 * 1000;
int atw_bbp_io_disable_delay = 2 * 1000;
int atw_writewep_delay = 1000;
int atw_dwelltime = 200;
int atw_xindiv2 = 0;

#ifdef ATW_DEBUG
int atw_debug = 0;

#define ATW_DPRINTF(x)	if (atw_debug > 0) printf x
#define ATW_DPRINTF2(x)	if (atw_debug > 1) printf x
#define ATW_DPRINTF3(x)	if (atw_debug > 2) printf x
#define	DPRINTF(sc, x)	if ((sc)->sc_ic.ic_if.if_flags & IFF_DEBUG) printf x
#define	DPRINTF2(sc, x)	if ((sc)->sc_ic.ic_if.if_flags & IFF_DEBUG) ATW_DPRINTF2(x)
#define	DPRINTF3(sc, x)	if ((sc)->sc_ic.ic_if.if_flags & IFF_DEBUG) ATW_DPRINTF3(x)
void atw_print_regs(struct atw_softc *, const char *);
void atw_dump_pkt(struct ifnet *, struct mbuf *);

/* Note well: I never got atw_rf3000_read or atw_si4126_read to work. */
#	ifdef ATW_BBPDEBUG
int atw_rf3000_read(struct atw_softc *sc, u_int, u_int *);
void atw_rf3000_print(struct atw_softc *);
#	endif /* ATW_BBPDEBUG */

#	ifdef ATW_SYNDEBUG
int atw_si4126_read(struct atw_softc *, u_int, u_int *);
void atw_si4126_print(struct atw_softc *);
#	endif /* ATW_SYNDEBUG */

#else
#define ATW_DPRINTF(x)
#define ATW_DPRINTF2(x)
#define ATW_DPRINTF3(x)
#define	DPRINTF(sc, x)	/* nothing */
#define	DPRINTF2(sc, x)	/* nothing */
#define	DPRINTF3(sc, x)	/* nothing */
#endif

const char *atw_printmac(u_int8_t);

/* ifnet methods */
void	atw_start(struct ifnet *);
void	atw_watchdog(struct ifnet *);
int	atw_ioctl(struct ifnet *, u_long, caddr_t);
int	atw_init(struct ifnet *);
void	atw_stop(struct ifnet *, int);

/* Rx/Tx process */
void	atw_rxdrain(struct atw_softc *);
void	atw_txdrain(struct atw_softc *);
int	atw_add_rxbuf(struct atw_softc *, int);
void	atw_idle(struct atw_softc *, u_int32_t);

/* Device (de)activation and power state */
void	atw_disable(struct atw_softc *);
void	atw_reset(struct atw_softc *);

/* Interrupt handlers */
void	atw_rxintr(struct atw_softc *);
void	atw_txintr(struct atw_softc *);
void	atw_linkintr(struct atw_softc *, u_int32_t);

/* 802.11 state machine */
int	atw_newstate(struct ieee80211com *, enum ieee80211_state, int);
int	atw_tune(struct atw_softc *);
#ifndef IEEE80211_STA_ONLY
void	atw_recv_mgmt(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *, struct ieee80211_rxinfo *, int);
#endif
void	atw_next_scan(void *);

/* Device initialization */
void	atw_wcsr_init(struct atw_softc *);
void	atw_cmdr_init(struct atw_softc *);
void	atw_tofs2_init(struct atw_softc *);
void	atw_txlmt_init(struct atw_softc *);
void	atw_test1_init(struct atw_softc *);
void	atw_rf_reset(struct atw_softc *);
void	atw_cfp_init(struct atw_softc *);
void	atw_tofs0_init(struct atw_softc *);
void	atw_ifs_init(struct atw_softc *);
void	atw_response_times_init(struct atw_softc *);
void	atw_bbp_io_init(struct atw_softc *);
void	atw_nar_init(struct atw_softc *);

/* RAM/ROM utilities */
void	atw_clear_sram(struct atw_softc *);
void	atw_write_sram(struct atw_softc *, u_int, u_int8_t *, u_int);
int	atw_read_srom(struct atw_softc *);

/* BSS setup */
void	atw_predict_beacon(struct atw_softc *sc);
void	atw_start_beacon(struct atw_softc *, int);
void	atw_write_bssid(struct atw_softc *);
void	atw_write_ssid(struct atw_softc *);
void	atw_write_sup_rates(struct atw_softc *);
void	atw_write_wep(struct atw_softc *);

/* Media */
int	atw_media_change(struct ifnet *);
void	atw_media_status(struct ifnet *, struct ifmediareq *);

void	atw_filter_setup(struct atw_softc *);

/* 802.11 utilities */
struct	ieee80211_node *atw_node_alloc(struct ieee80211com *);
void	atw_node_free(struct ieee80211com *, struct ieee80211_node *);
static	__inline uint32_t atw_last_even_tsft(uint32_t, uint32_t, uint32_t);
uint64_t atw_get_tsft(struct atw_softc *sc);
void	atw_change_ibss(struct atw_softc *);
int	atw_compute_duration1(int, int, uint32_t, int, struct atw_duration *);
int	atw_compute_duration(struct ieee80211_frame *, int, uint32_t, int,
	    int, struct atw_duration *, struct atw_duration *, int *, int);

/*
 * Tuner/transceiver/modem
 */
void	atw_bbp_io_enable(struct atw_softc *, int);

/* RFMD RF3000 Baseband Processor */
int	atw_rf3000_init(struct atw_softc *);
int	atw_rf3000_tune(struct atw_softc *, u_int);
int	atw_rf3000_write(struct atw_softc *, u_int, u_int);

/* Silicon Laboratories Si4126 RF/IF Synthesizer */
void	atw_si4126_tune(struct atw_softc *, u_int);
void	atw_si4126_write(struct atw_softc *, u_int, u_int);
void	atw_si4126_init(struct atw_softc *);

const struct atw_txthresh_tab atw_txthresh_tab_lo[] = {
	{ ATW_NAR_TR_L64,	"64 bytes" },
	{ ATW_NAR_TR_L160,	"160 bytes" },
	{ ATW_NAR_TR_L192,	"192 bytes" },
	{ ATW_NAR_SF,		"store and forward" },
	{ 0,			NULL }
};
const struct atw_txthresh_tab atw_txthresh_tab_hi[] = {
	{ ATW_NAR_TR_H96,	"96 bytes" },
	{ ATW_NAR_TR_H288,	"288 bytes" },
	{ ATW_NAR_TR_H544,	"544 bytes" },
	{ ATW_NAR_SF,		"store and forward" },
	{ 0,			NULL }
};

struct cfdriver atw_cd = {
    NULL, "atw", DV_IFNET
};

static const u_int atw_rfmd2958_ifn[] = {
	0x22bd, 0x22d2, 0x22e8, 0x22fe, 0x2314, 0x232a, 0x2340,
	0x2355, 0x236b, 0x2381, 0x2397, 0x23ad, 0x23c2, 0x23f7
};

static const u_int atw_rfmd2958_rf1r[] = {
	0x05d17, 0x3a2e8, 0x2e8ba, 0x22e8b, 0x1745d, 0x0ba2e, 0x00000,
	0x345d1, 0x28ba2, 0x1d174, 0x11745, 0x05d17, 0x3a2e8, 0x11745
};


#ifdef ATW_DEBUG

const char *atw_tx_state[] = {
	"STOPPED",
	"RUNNING - read descriptor",
	"RUNNING - transmitting",
	"RUNNING - filling fifo",	/* XXX */
	"SUSPENDED",
	"RUNNING -- write descriptor",
	"RUNNING -- write last descriptor",
	"RUNNING - fifo full"
};

const char *atw_rx_state[] = {
	"STOPPED",
	"RUNNING - read descriptor",
	"RUNNING - check this packet, pre-fetch next",
	"RUNNING - wait for reception",
	"SUSPENDED",
	"RUNNING - write descriptor",
	"RUNNING - flush fifo",
	"RUNNING - fifo drain"
};

#endif

/*
 * atw_enable:
 *
 *	Enable the ADM8211 chip.
 */
int
atw_enable(struct atw_softc *sc)
{

	if (ATW_IS_ENABLED(sc) == 0) {
		if (sc->sc_enable != NULL && (*sc->sc_enable)(sc) != 0) {
			printf("%s: device enable failed\n",
			    sc->sc_dev.dv_xname);
			return (EIO);
		}
		sc->sc_flags |= ATWF_ENABLED;
	}
	return (0);
}

/*
 * atw_disable:
 *
 *	Disable the ADM8211 chip.
 */
void
atw_disable(struct atw_softc *sc)
{
	if (!ATW_IS_ENABLED(sc))
		return;
	if (sc->sc_disable != NULL)
		(*sc->sc_disable)(sc);
	sc->sc_flags &= ~ATWF_ENABLED;
}

/* Returns -1 on failure. */
int
atw_read_srom(struct atw_softc *sc)
{
	struct seeprom_descriptor sd;
	u_int32_t test0, fail_bits;

	(void)memset(&sd, 0, sizeof(sd));

	test0 = ATW_READ(sc, ATW_TEST0);

	switch (sc->sc_rev) {
	case ATW_REVISION_BA:
	case ATW_REVISION_CA:
		fail_bits = ATW_TEST0_EPNE;
		break;
	default:
		fail_bits = ATW_TEST0_EPNE|ATW_TEST0_EPSNM;
		break;
	}
	if ((test0 & fail_bits) != 0) {
		printf("%s: bad or missing/bad SROM\n", sc->sc_dev.dv_xname);
		return -1;
	}

	switch (test0 & ATW_TEST0_EPTYP_MASK) {
	case ATW_TEST0_EPTYP_93c66:
		ATW_DPRINTF(("%s: 93c66 SROM\n", sc->sc_dev.dv_xname));
		sc->sc_sromsz = 512;
		sd.sd_chip = C56_66;
		break;
	case ATW_TEST0_EPTYP_93c46:
		ATW_DPRINTF(("%s: 93c46 SROM\n", sc->sc_dev.dv_xname));
		sc->sc_sromsz = 128;
		sd.sd_chip = C46;
		break;
	default:
		printf("%s: unknown SROM type %d\n", sc->sc_dev.dv_xname,
		    MASK_AND_RSHIFT(test0, ATW_TEST0_EPTYP_MASK));
		return -1;
	}

	sc->sc_srom = malloc(sc->sc_sromsz, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->sc_srom == NULL) {
		printf("%s: unable to allocate SROM buffer\n",
		    sc->sc_dev.dv_xname);
		return -1;
	}

	/*
	 * ADM8211 has a single 32-bit register for controlling the
	 * 93cx6 SROM.  Bit SRS enables the serial port. There is no
	 * "ready" bit. The ADM8211 input/output sense is the reverse
	 * of read_seeprom's.
	 */
	sd.sd_tag = sc->sc_st;
	sd.sd_bsh = sc->sc_sh;
	sd.sd_regsize = 4;
	sd.sd_control_offset = ATW_SPR;
	sd.sd_status_offset = ATW_SPR;
	sd.sd_dataout_offset = ATW_SPR;
	sd.sd_CK = ATW_SPR_SCLK;
	sd.sd_CS = ATW_SPR_SCS;
	sd.sd_DI = ATW_SPR_SDO;
	sd.sd_DO = ATW_SPR_SDI;
	sd.sd_MS = ATW_SPR_SRS;
	sd.sd_RDY = 0;

	if (!read_seeprom(&sd, sc->sc_srom, 0, sc->sc_sromsz/2)) {
		printf("%s: could not read SROM\n", sc->sc_dev.dv_xname);
		free(sc->sc_srom, M_DEVBUF, sc->sc_sromsz);
		return -1;
	}
#ifdef ATW_DEBUG
	{
		int i;
		ATW_DPRINTF(("\nSerial EEPROM:\n\t"));
		for (i = 0; i < sc->sc_sromsz/2; i = i + 1) {
			if (((i % 8) == 0) && (i != 0)) {
				ATW_DPRINTF(("\n\t"));
			}
			ATW_DPRINTF((" 0x%x", sc->sc_srom[i]));
		}
		ATW_DPRINTF(("\n"));
	}
#endif /* ATW_DEBUG */
	return 0;
}

#ifdef ATW_DEBUG
void
atw_print_regs(struct atw_softc *sc, const char *where)
{
#define PRINTREG(sc, reg) \
	ATW_DPRINTF2(("%s: reg[ " #reg " / %03x ] = %08x\n", \
	    sc->sc_dev.dv_xname, reg, ATW_READ(sc, reg)))

	ATW_DPRINTF2(("%s: %s\n", sc->sc_dev.dv_xname, where));

	PRINTREG(sc, ATW_PAR);
	PRINTREG(sc, ATW_FRCTL);
	PRINTREG(sc, ATW_TDR);
	PRINTREG(sc, ATW_WTDP);
	PRINTREG(sc, ATW_RDR);
	PRINTREG(sc, ATW_WRDP);
	PRINTREG(sc, ATW_RDB);
	PRINTREG(sc, ATW_CSR3A);
	PRINTREG(sc, ATW_TDBD);
	PRINTREG(sc, ATW_TDBP);
	PRINTREG(sc, ATW_STSR);
	PRINTREG(sc, ATW_CSR5A);
	PRINTREG(sc, ATW_NAR);
	PRINTREG(sc, ATW_CSR6A);
	PRINTREG(sc, ATW_IER);
	PRINTREG(sc, ATW_CSR7A);
	PRINTREG(sc, ATW_LPC);
	PRINTREG(sc, ATW_TEST1);
	PRINTREG(sc, ATW_SPR);
	PRINTREG(sc, ATW_TEST0);
	PRINTREG(sc, ATW_WCSR);
	PRINTREG(sc, ATW_WPDR);
	PRINTREG(sc, ATW_GPTMR);
	PRINTREG(sc, ATW_GPIO);
	PRINTREG(sc, ATW_BBPCTL);
	PRINTREG(sc, ATW_SYNCTL);
	PRINTREG(sc, ATW_PLCPHD);
	PRINTREG(sc, ATW_MMIWADDR);
	PRINTREG(sc, ATW_MMIRADDR1);
	PRINTREG(sc, ATW_MMIRADDR2);
	PRINTREG(sc, ATW_TXBR);
	PRINTREG(sc, ATW_CSR15A);
	PRINTREG(sc, ATW_ALCSTAT);
	PRINTREG(sc, ATW_TOFS2);
	PRINTREG(sc, ATW_CMDR);
	PRINTREG(sc, ATW_PCIC);
	PRINTREG(sc, ATW_PMCSR);
	PRINTREG(sc, ATW_PAR0);
	PRINTREG(sc, ATW_PAR1);
	PRINTREG(sc, ATW_MAR0);
	PRINTREG(sc, ATW_MAR1);
	PRINTREG(sc, ATW_ATIMDA0);
	PRINTREG(sc, ATW_ABDA1);
	PRINTREG(sc, ATW_BSSID0);
	PRINTREG(sc, ATW_TXLMT);
	PRINTREG(sc, ATW_MIBCNT);
	PRINTREG(sc, ATW_BCNT);
	PRINTREG(sc, ATW_TSFTH);
	PRINTREG(sc, ATW_TSC);
	PRINTREG(sc, ATW_SYNRF);
	PRINTREG(sc, ATW_BPLI);
	PRINTREG(sc, ATW_CAP0);
	PRINTREG(sc, ATW_CAP1);
	PRINTREG(sc, ATW_RMD);
	PRINTREG(sc, ATW_CFPP);
	PRINTREG(sc, ATW_TOFS0);
	PRINTREG(sc, ATW_TOFS1);
	PRINTREG(sc, ATW_IFST);
	PRINTREG(sc, ATW_RSPT);
	PRINTREG(sc, ATW_TSFTL);
	PRINTREG(sc, ATW_WEPCTL);
	PRINTREG(sc, ATW_WESK);
	PRINTREG(sc, ATW_WEPCNT);
	PRINTREG(sc, ATW_MACTEST);
	PRINTREG(sc, ATW_FER);
	PRINTREG(sc, ATW_FEMR);
	PRINTREG(sc, ATW_FPSR);
	PRINTREG(sc, ATW_FFER);
#undef PRINTREG
}
#endif /* ATW_DEBUG */

const char*
atw_printmac(u_int8_t rev) {
	switch (rev) {
	case ATW_REVISION_AB:
		return "ADM8211AB";
	case ATW_REVISION_AF:
		return "ADM8211AF";
	case ATW_REVISION_BA:
		return "ADM8211BA";
	case ATW_REVISION_CA:
		return "ADM8211CA";
	default:
		return "unknown";
	}
}

/*
 * Finish attaching an ADMtek ADM8211 MAC.  Called by bus-specific front-end.
 */
void
atw_attach(struct atw_softc *sc)
{
	static const u_int8_t empty_macaddr[IEEE80211_ADDR_LEN] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int country_code, error, i, srom_major;
	u_int32_t reg;
	static const char *type_strings[] = {"Intersil (not supported)",
	    "RFMD", "Marvel (not supported)"};

	sc->sc_txth = atw_txthresh_tab_lo;

	SIMPLEQ_INIT(&sc->sc_txfreeq);
	SIMPLEQ_INIT(&sc->sc_txdirtyq);

#ifdef ATW_DEBUG
	atw_print_regs(sc, "atw_attach");
#endif /* ATW_DEBUG */

	/*
	 * Allocate the control data structures, and create and load the
	 * DMA map for it.
	 */
	if ((error = bus_dmamem_alloc(sc->sc_dmat,
	    sizeof(struct atw_control_data), PAGE_SIZE, 0, &sc->sc_cdseg,
	    1, &sc->sc_cdnseg, 0)) != 0) {
		printf("%s: unable to allocate control data, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_0;
	}

	if ((error = bus_dmamem_map(sc->sc_dmat, &sc->sc_cdseg, sc->sc_cdnseg,
	    sizeof(struct atw_control_data), (caddr_t *)&sc->sc_control_data,
	    BUS_DMA_COHERENT)) != 0) {
		printf("%s: unable to map control data, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_1;
	}

	if ((error = bus_dmamap_create(sc->sc_dmat,
	    sizeof(struct atw_control_data), 1,
	    sizeof(struct atw_control_data), 0, 0, &sc->sc_cddmamap)) != 0) {
		printf("%s: unable to create control data DMA map, "
		    "error = %d\n", sc->sc_dev.dv_xname, error);
		goto fail_2;
	}

	if ((error = bus_dmamap_load(sc->sc_dmat, sc->sc_cddmamap,
	    sc->sc_control_data, sizeof(struct atw_control_data), NULL,
	    0)) != 0) {
		printf("%s: unable to load control data DMA map, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_3;
	}

	/*
	 * Create the transmit buffer DMA maps.
	 */
	sc->sc_ntxsegs = ATW_NTXSEGS;
	for (i = 0; i < ATW_TXQUEUELEN; i++) {
		if ((error = bus_dmamap_create(sc->sc_dmat, MCLBYTES,
		    sc->sc_ntxsegs, MCLBYTES, 0, 0,
		    &sc->sc_txsoft[i].txs_dmamap)) != 0) {
			printf("%s: unable to create tx DMA map %d, "
			    "error = %d\n", sc->sc_dev.dv_xname, i, error);
			goto fail_4;
		}
	}

	/*
	 * Create the receive buffer DMA maps.
	 */
	for (i = 0; i < ATW_NRXDESC; i++) {
		if ((error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, 0, &sc->sc_rxsoft[i].rxs_dmamap)) != 0) {
			printf("%s: unable to create rx DMA map %d, "
			    "error = %d\n", sc->sc_dev.dv_xname, i, error);
			goto fail_5;
		}
	}
	for (i = 0; i < ATW_NRXDESC; i++) {
		sc->sc_rxsoft[i].rxs_mbuf = NULL;
	}

	switch (sc->sc_rev) {
	case ATW_REVISION_AB:
	case ATW_REVISION_AF:
		sc->sc_sramlen = ATW_SRAM_A_SIZE;
		break;
	case ATW_REVISION_BA:
	case ATW_REVISION_CA:
		sc->sc_sramlen = ATW_SRAM_B_SIZE;
		break;
	}

	/* Reset the chip to a known state. */
	atw_reset(sc);

	if (atw_read_srom(sc) == -1)
		return;

	sc->sc_rftype = MASK_AND_RSHIFT(sc->sc_srom[ATW_SR_CSR20],
	    ATW_SR_RFTYPE_MASK);

	sc->sc_bbptype = MASK_AND_RSHIFT(sc->sc_srom[ATW_SR_CSR20],
	    ATW_SR_BBPTYPE_MASK);

	if (sc->sc_rftype >= nitems(type_strings)) {
		printf("%s: unknown RF\n", sc->sc_dev.dv_xname);
		return;
	}
	if (sc->sc_bbptype >= nitems(type_strings)) {
		printf("%s: unknown BBP\n", sc->sc_dev.dv_xname);
		return;
	}

	printf("%s: MAC %s, BBP %s, RF %s", sc->sc_dev.dv_xname,
	    atw_printmac(sc->sc_rev), type_strings[sc->sc_bbptype],
	    type_strings[sc->sc_rftype]);

	/* XXX There exists a Linux driver which seems to use RFType = 0 for
	 * MARVEL. My bug, or theirs?
	 */

	reg = LSHIFT(sc->sc_rftype, ATW_SYNCTL_RFTYPE_MASK);

	switch (sc->sc_rftype) {
	case ATW_RFTYPE_INTERSIL:
		reg |= ATW_SYNCTL_CS1;
		break;
	case ATW_RFTYPE_RFMD:
		reg |= ATW_SYNCTL_CS0;
		break;
	case ATW_RFTYPE_MARVEL:
		break;
	}

	sc->sc_synctl_rd = reg | ATW_SYNCTL_RD;
	sc->sc_synctl_wr = reg | ATW_SYNCTL_WR;

	reg = LSHIFT(sc->sc_bbptype, ATW_BBPCTL_TYPE_MASK);

	switch (sc->sc_bbptype) {
	case ATW_BBPTYPE_INTERSIL:
		reg |= ATW_BBPCTL_TWI;
		break;
	case ATW_BBPTYPE_RFMD:
		reg |= ATW_BBPCTL_RF3KADDR_ADDR | ATW_BBPCTL_NEGEDGE_DO |
		    ATW_BBPCTL_CCA_ACTLO;
		break;
	case ATW_BBPTYPE_MARVEL:
		break;
	case ATW_C_BBPTYPE_RFMD:
		printf("%s: ADM8211C MAC/RFMD BBP not supported yet.\n",
		    sc->sc_dev.dv_xname);
		break;
	}

	sc->sc_bbpctl_wr = reg | ATW_BBPCTL_WR;
	sc->sc_bbpctl_rd = reg | ATW_BBPCTL_RD;

	/*
	 * From this point forward, the attachment cannot fail.  A failure
	 * before this point releases all resources that may have been
	 * allocated.
	 */
	sc->sc_flags |= ATWF_ATTACHED /* | ATWF_RTSCTS */;

	ATW_DPRINTF((" SROM MAC %04x%04x%04x",
	    htole16(sc->sc_srom[ATW_SR_MAC00]),
	    htole16(sc->sc_srom[ATW_SR_MAC01]),
	    htole16(sc->sc_srom[ATW_SR_MAC10])));

	srom_major = MASK_AND_RSHIFT(sc->sc_srom[ATW_SR_FORMAT_VERSION],
	    ATW_SR_MAJOR_MASK);

	if (srom_major < 2)
		sc->sc_rf3000_options1 = 0;
	else if (sc->sc_rev == ATW_REVISION_BA) {
		sc->sc_rf3000_options1 =
		    MASK_AND_RSHIFT(sc->sc_srom[ATW_SR_CR28_CR03],
		    ATW_SR_CR28_MASK);
	} else
		sc->sc_rf3000_options1 = 0;

	sc->sc_rf3000_options2 = MASK_AND_RSHIFT(sc->sc_srom[ATW_SR_CTRY_CR29],
	    ATW_SR_CR29_MASK);

	country_code = MASK_AND_RSHIFT(sc->sc_srom[ATW_SR_CTRY_CR29],
	    ATW_SR_CTRY_MASK);

#define ADD_CHANNEL(_ic, _chan) do {					\
	_ic->ic_channels[_chan].ic_flags = IEEE80211_CHAN_B;		\
	_ic->ic_channels[_chan].ic_freq =				\
	    ieee80211_ieee2mhz(_chan, _ic->ic_channels[_chan].ic_flags);\
} while (0)

	/* Find available channels */
	switch (country_code) {
	case COUNTRY_MMK2:	/* 1-14 */
		ADD_CHANNEL(ic, 14);
		/*FALLTHROUGH*/
	case COUNTRY_ETSI:	/* 1-13 */
		for (i = 1; i <= 13; i++)
			ADD_CHANNEL(ic, i);
		break;
	case COUNTRY_FCC:	/* 1-11 */
	case COUNTRY_IC:	/* 1-11 */
		for (i = 1; i <= 11; i++)
			ADD_CHANNEL(ic, i);
		break;
	case COUNTRY_MMK:	/* 14 */
		ADD_CHANNEL(ic, 14);
		break;
	case COUNTRY_FRANCE:	/* 10-13 */
		for (i = 10; i <= 13; i++)
			ADD_CHANNEL(ic, i);
		break;
	default:	/* assume channels 10-11 */
	case COUNTRY_SPAIN:	/* 10-11 */
		for (i = 10; i <= 11; i++)
			ADD_CHANNEL(ic, i);
		break;
	}

	/* Read the MAC address. */
	reg = ATW_READ(sc, ATW_PAR0);
	ic->ic_myaddr[0] = MASK_AND_RSHIFT(reg, ATW_PAR0_PAB0_MASK);
	ic->ic_myaddr[1] = MASK_AND_RSHIFT(reg, ATW_PAR0_PAB1_MASK);
	ic->ic_myaddr[2] = MASK_AND_RSHIFT(reg, ATW_PAR0_PAB2_MASK);
	ic->ic_myaddr[3] = MASK_AND_RSHIFT(reg, ATW_PAR0_PAB3_MASK);
	reg = ATW_READ(sc, ATW_PAR1);
	ic->ic_myaddr[4] = MASK_AND_RSHIFT(reg, ATW_PAR1_PAB4_MASK);
	ic->ic_myaddr[5] = MASK_AND_RSHIFT(reg, ATW_PAR1_PAB5_MASK);

	if (IEEE80211_ADDR_EQ(ic->ic_myaddr, empty_macaddr)) {
		printf(" could not get mac address, attach failed\n");
		return;
	}

	printf(", address %s\n", ether_sprintf(ic->ic_myaddr));

	memcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST | IFF_MULTICAST;
	ifp->if_ioctl = atw_ioctl;
	ifp->if_start = atw_start;
	ifp->if_watchdog = atw_watchdog;

	ic->ic_phytype = IEEE80211_T_DS;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_caps = IEEE80211_C_PMGT | IEEE80211_C_MONITOR | IEEE80211_C_WEP;
#ifndef IEEE80211_STA_ONLY
	ic->ic_caps |= IEEE80211_C_IBSS;
#endif
	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;	

	/*
	 * Call MI attach routines.
	 */

	if_attach(ifp);
	ieee80211_ifattach(ifp);

	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = atw_newstate;

#ifndef IEEE80211_STA_ONLY
	sc->sc_recv_mgmt = ic->ic_recv_mgmt;
	ic->ic_recv_mgmt = atw_recv_mgmt;
#endif

	sc->sc_node_free = ic->ic_node_free;
	ic->ic_node_free = atw_node_free;

	sc->sc_node_alloc = ic->ic_node_alloc;
	ic->ic_node_alloc = atw_node_alloc;

	/* possibly we should fill in our own sc_send_prresp, since
	 * the ADM8211 is probably sending probe responses in ad hoc
	 * mode.
	 */

	/* complete initialization */
	ieee80211_media_init(ifp, atw_media_change, atw_media_status);
	timeout_set(&sc->sc_scan_to, atw_next_scan, sc);

#if NBPFILTER > 0
	bpfattach(&sc->sc_radiobpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof(struct ieee80211_frame) + 64);
#endif

	memset(&sc->sc_rxtapu, 0, sizeof(sc->sc_rxtapu));
	sc->sc_rxtap.ar_ihdr.it_len = sizeof(sc->sc_rxtapu);
	sc->sc_rxtap.ar_ihdr.it_present = ATW_RX_RADIOTAP_PRESENT;

	memset(&sc->sc_txtapu, 0, sizeof(sc->sc_txtapu));
	sc->sc_txtap.at_ihdr.it_len = sizeof(sc->sc_txtapu);
	sc->sc_txtap.at_ihdr.it_present = ATW_TX_RADIOTAP_PRESENT;

	return;

	/*
	 * Free any resources we've allocated during the failed attach
	 * attempt.  Do this in reverse order and fall through.
	 */
 fail_5:
	for (i = 0; i < ATW_NRXDESC; i++) {
		if (sc->sc_rxsoft[i].rxs_dmamap == NULL)
			continue;
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_rxsoft[i].rxs_dmamap);
	}
 fail_4:
	for (i = 0; i < ATW_TXQUEUELEN; i++) {
		if (sc->sc_txsoft[i].txs_dmamap == NULL)
			continue;
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_txsoft[i].txs_dmamap);
	}
	bus_dmamap_unload(sc->sc_dmat, sc->sc_cddmamap);
 fail_3:
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_cddmamap);
 fail_2:
	bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->sc_control_data,
	    sizeof(struct atw_control_data));
 fail_1:
	bus_dmamem_free(sc->sc_dmat, &sc->sc_cdseg, sc->sc_cdnseg);
 fail_0:
	return;
}

struct ieee80211_node *
atw_node_alloc(struct ieee80211com *ic)
{
	struct atw_softc *sc = (struct atw_softc *)ic->ic_if.if_softc;
	struct ieee80211_node *ni = (*sc->sc_node_alloc)(ic);

	DPRINTF(sc, ("%s: alloc node %p\n", sc->sc_dev.dv_xname, ni));
	return ni;
}

void
atw_node_free(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct atw_softc *sc = (struct atw_softc *)ic->ic_if.if_softc;

	DPRINTF(sc, ("%s: freeing node %p %s\n", sc->sc_dev.dv_xname, ni,
	    ether_sprintf(ni->ni_bssid)));
	(*sc->sc_node_free)(ic, ni);
}


static void
atw_test1_reset(struct atw_softc *sc)
{
	switch (sc->sc_rev) {
	case ATW_REVISION_BA:
		if (1 /* XXX condition on transceiver type */) {
			ATW_SET(sc, ATW_TEST1, ATW_TEST1_TESTMODE_MONITOR);
		}
		break;
	case ATW_REVISION_CA:
		ATW_CLR(sc, ATW_TEST1, ATW_TEST1_TESTMODE_MASK);
		break;
	default:
		break;
	}
}

/*
 * atw_reset:
 *
 *	Perform a soft reset on the ADM8211.
 */
void
atw_reset(struct atw_softc *sc)
{
	int i;
	uint32_t lpc;

	ATW_WRITE(sc, ATW_NAR, 0x0);
	DELAY(20 * 1000);

	/* Reference driver has a cryptic remark indicating that this might
	 * power-on the chip.  I know that it turns off power-saving....
	 */
	ATW_WRITE(sc, ATW_FRCTL, 0x0);

	ATW_WRITE(sc, ATW_PAR, ATW_PAR_SWR);

	for (i = 0; i < 50; i++) {
		if (ATW_READ(sc, ATW_PAR) == 0)
			break;
		DELAY(1000);
	}

	/* ... and then pause 100ms longer for good measure. */
	DELAY(100 * 1000);

	DPRINTF2(sc, ("%s: atw_reset %d iterations\n", sc->sc_dev.dv_xname, i));

	if (ATW_ISSET(sc, ATW_PAR, ATW_PAR_SWR))
		printf("%s: reset failed to complete\n", sc->sc_dev.dv_xname);

	atw_test1_reset(sc);
	/*
	 * Initialize the PCI Access Register.
	 */
	sc->sc_busmode = ATW_PAR_PBL_8DW;

	ATW_WRITE(sc, ATW_PAR, sc->sc_busmode);
	DPRINTF(sc, ("%s: ATW_PAR %08x busmode %08x\n", sc->sc_dev.dv_xname,
	    ATW_READ(sc, ATW_PAR), sc->sc_busmode));

	/* Turn off maximum power saving, etc.
	 *
	 * XXX Following example of reference driver, should I set
	 * an AID of 1?  It didn't seem to help....
	 */
	ATW_WRITE(sc, ATW_FRCTL, 0x0);

	DELAY(100 * 1000);

	/* Recall EEPROM. */
	ATW_SET(sc, ATW_TEST0, ATW_TEST0_EPRLD);

	DELAY(10 * 1000);

	lpc = ATW_READ(sc, ATW_LPC);

	DPRINTF(sc, ("%s: ATW_LPC %#08x\n", __func__, lpc));

	/* A reset seems to affect the SRAM contents, so put them into
	 * a known state.
	 */
	atw_clear_sram(sc);

	memset(sc->sc_bssid, 0xff, sizeof(sc->sc_bssid));
}

void
atw_clear_sram(struct atw_softc *sc)
{
	memset(sc->sc_sram, 0, sizeof(sc->sc_sram));
	/* XXX not for revision 0x20. */
	atw_write_sram(sc, 0, sc->sc_sram, sc->sc_sramlen);
}

/* TBD atw_init
 *
 * set MAC based on ic->ic_bss->myaddr
 * write WEP keys
 * set TX rate
 */

/* Tell the ADM8211 to raise ATW_INTR_LINKOFF if 7 beacon intervals pass
 * without receiving a beacon with the preferred BSSID & SSID.
 * atw_write_bssid & atw_write_ssid set the BSSID & SSID.
 */
void
atw_wcsr_init(struct atw_softc *sc)
{
	uint32_t wcsr;

	wcsr = ATW_READ(sc, ATW_WCSR);
	wcsr &= ~(ATW_WCSR_BLN_MASK|ATW_WCSR_LSOE|ATW_WCSR_MPRE|ATW_WCSR_LSOE);
	wcsr |= LSHIFT(7, ATW_WCSR_BLN_MASK);
	ATW_WRITE(sc, ATW_WCSR, wcsr);	/* XXX resets wake-up status bits */

	DPRINTF(sc, ("%s: %s reg[WCSR] = %08x\n",
	    sc->sc_dev.dv_xname, __func__, ATW_READ(sc, ATW_WCSR)));
}

/* Turn off power management.  Set Rx store-and-forward mode. */
void
atw_cmdr_init(struct atw_softc *sc)
{
	uint32_t cmdr;
	cmdr = ATW_READ(sc, ATW_CMDR);
	cmdr &= ~ATW_CMDR_APM;
	cmdr |= ATW_CMDR_RTE;
	cmdr &= ~ATW_CMDR_DRT_MASK;
	cmdr |= ATW_CMDR_DRT_SF;

	ATW_WRITE(sc, ATW_CMDR, cmdr);
}

void
atw_tofs2_init(struct atw_softc *sc)
{
	uint32_t tofs2;
	/* XXX this magic can probably be figured out from the RFMD docs */
#ifndef ATW_REFSLAVE
	tofs2 = LSHIFT(4, ATW_TOFS2_PWR1UP_MASK)    | /* 8 ms = 4 * 2 ms */
	      LSHIFT(13, ATW_TOFS2_PWR0PAPE_MASK) | /* 13 us */
	      LSHIFT(8, ATW_TOFS2_PWR1PAPE_MASK)  | /* 8 us */
	      LSHIFT(5, ATW_TOFS2_PWR0TRSW_MASK)  | /* 5 us */
	      LSHIFT(12, ATW_TOFS2_PWR1TRSW_MASK) | /* 12 us */
	      LSHIFT(13, ATW_TOFS2_PWR0PE2_MASK)  | /* 13 us */
	      LSHIFT(4, ATW_TOFS2_PWR1PE2_MASK)   | /* 4 us */
	      LSHIFT(5, ATW_TOFS2_PWR0TXPE_MASK);  /* 5 us */
#else
	/* XXX new magic from reference driver source */
	tofs2 = LSHIFT(8, ATW_TOFS2_PWR1UP_MASK)    | /* 8 ms = 4 * 2 ms */
	      LSHIFT(8, ATW_TOFS2_PWR0PAPE_MASK) | /* 13 us */
	      LSHIFT(1, ATW_TOFS2_PWR1PAPE_MASK)  | /* 8 us */
	      LSHIFT(5, ATW_TOFS2_PWR0TRSW_MASK)  | /* 5 us */
	      LSHIFT(12, ATW_TOFS2_PWR1TRSW_MASK) | /* 12 us */
	      LSHIFT(13, ATW_TOFS2_PWR0PE2_MASK)  | /* 13 us */
	      LSHIFT(1, ATW_TOFS2_PWR1PE2_MASK)   | /* 4 us */
	      LSHIFT(8, ATW_TOFS2_PWR0TXPE_MASK);  /* 5 us */
#endif
	ATW_WRITE(sc, ATW_TOFS2, tofs2);
}

void
atw_nar_init(struct atw_softc *sc)
{
	ATW_WRITE(sc, ATW_NAR, ATW_NAR_SF|ATW_NAR_PB);
}

void
atw_txlmt_init(struct atw_softc *sc)
{
	ATW_WRITE(sc, ATW_TXLMT, LSHIFT(512, ATW_TXLMT_MTMLT_MASK) |
	                         LSHIFT(1, ATW_TXLMT_SRTYLIM_MASK));
}

void
atw_test1_init(struct atw_softc *sc)
{
	uint32_t test1;

	test1 = ATW_READ(sc, ATW_TEST1);
	test1 &= ~(ATW_TEST1_DBGREAD_MASK|ATW_TEST1_CONTROL);
	/* XXX magic 0x1 */
	test1 |= LSHIFT(0x1, ATW_TEST1_DBGREAD_MASK) | ATW_TEST1_CONTROL;
	ATW_WRITE(sc, ATW_TEST1, test1);
}

void
atw_rf_reset(struct atw_softc *sc)
{
	/* XXX this resets an Intersil RF front-end? */
	/* TBD condition on Intersil RFType? */
	ATW_WRITE(sc, ATW_SYNRF, ATW_SYNRF_INTERSIL_EN);
	DELAY(10 * 1000);
	ATW_WRITE(sc, ATW_SYNRF, 0);
	DELAY(5 * 1000);
}

/* Set 16 TU max duration for the contention-free period (CFP). */
void
atw_cfp_init(struct atw_softc *sc)
{
	uint32_t cfpp;

	cfpp = ATW_READ(sc, ATW_CFPP);
	cfpp &= ~ATW_CFPP_CFPMD;
	cfpp |= LSHIFT(16, ATW_CFPP_CFPMD);
	ATW_WRITE(sc, ATW_CFPP, cfpp);
}

void
atw_tofs0_init(struct atw_softc *sc)
{
	/* XXX I guess that the Cardbus clock is 22MHz?
	 * I am assuming that the role of ATW_TOFS0_USCNT is
	 * to divide the bus clock to get a 1MHz clock---the datasheet is not
	 * very clear on this point. It says in the datasheet that it is
	 * possible for the ADM8211 to accommodate bus speeds between 22MHz
	 * and 33MHz; maybe this is the way? I see a binary-only driver write
	 * these values. These values are also the power-on default.
	 */
	ATW_WRITE(sc, ATW_TOFS0,
	    LSHIFT(22, ATW_TOFS0_USCNT_MASK) |
	    ATW_TOFS0_TUCNT_MASK /* set all bits in TUCNT */);
}

/* Initialize interframe spacing: 802.11b slot time, SIFS, DIFS, EIFS. */
void
atw_ifs_init(struct atw_softc *sc)
{
	uint32_t ifst;
	/* XXX EIFS=0x64, SIFS=110 are used by the reference driver.
	 * Go figure.
	 */
	ifst = LSHIFT(IEEE80211_DUR_DS_SLOT, ATW_IFST_SLOT_MASK) |
	    LSHIFT(22 * 5 /* IEEE80211_DUR_DS_SIFS */ /* # of 22MHz cycles */,
		   ATW_IFST_SIFS_MASK) |
	    LSHIFT(IEEE80211_DUR_DS_DIFS, ATW_IFST_DIFS_MASK) |
	    LSHIFT(0x64 /* IEEE80211_DUR_DS_EIFS */, ATW_IFST_EIFS_MASK);

	ATW_WRITE(sc, ATW_IFST, ifst);
}

void
atw_response_times_init(struct atw_softc *sc)
{
	/* XXX More magic. Relates to ACK timing?  The datasheet seems to
	 * indicate that the MAC expects at least SIFS + MIRT microseconds
	 * to pass after it transmits a frame that requires a response;
	 * it waits at most SIFS + MART microseconds for the response.
	 * Surely this is not the ACK timeout?
	 */
	ATW_WRITE(sc, ATW_RSPT, LSHIFT(0xffff, ATW_RSPT_MART_MASK) |
	    LSHIFT(0xff, ATW_RSPT_MIRT_MASK));
}

/* Set up the MMI read/write addresses for the baseband. The Tx/Rx
 * engines read and write baseband registers after Rx and before
 * Tx, respectively.
 */
void
atw_bbp_io_init(struct atw_softc *sc)
{
	uint32_t mmiraddr2;

	/* XXX The reference driver does this, but is it *really*
	 * necessary?
	 */
	switch (sc->sc_rev) {
	case ATW_REVISION_AB:
	case ATW_REVISION_AF:
		mmiraddr2 = 0x0;
		break;
	default:
		mmiraddr2 = ATW_READ(sc, ATW_MMIRADDR2);
		mmiraddr2 &=
		    ~(ATW_MMIRADDR2_PROREXT|ATW_MMIRADDR2_PRORLEN_MASK);
		break;
	}

	switch (sc->sc_bbptype) {
	case ATW_BBPTYPE_INTERSIL:
		ATW_WRITE(sc, ATW_MMIWADDR, ATW_MMIWADDR_INTERSIL);
		ATW_WRITE(sc, ATW_MMIRADDR1, ATW_MMIRADDR1_INTERSIL);
		mmiraddr2 |= ATW_MMIRADDR2_INTERSIL;
		break;
	case ATW_BBPTYPE_MARVEL:
		/* TBD find out the Marvel settings. */
		break;
	case ATW_BBPTYPE_RFMD:
	default:
		ATW_WRITE(sc, ATW_MMIWADDR, ATW_MMIWADDR_RFMD);
		ATW_WRITE(sc, ATW_MMIRADDR1, ATW_MMIRADDR1_RFMD);
		mmiraddr2 |= ATW_MMIRADDR2_RFMD;
		break;
	}
	ATW_WRITE(sc, ATW_MMIRADDR2, mmiraddr2);

	atw_si4126_init(sc);

	ATW_WRITE(sc, ATW_MACTEST, ATW_MACTEST_MMI_USETXCLK);
}

void
atw_si4126_init(struct atw_softc *sc)
{
	switch (sc->sc_rftype) {
	case ATW_RFTYPE_RFMD:
		if (sc->sc_rev >= ATW_REVISION_BA) {
			atw_si4126_write(sc, 0x1f, 0x00000);
			atw_si4126_write(sc, 0x0c, 0x3001f);
			atw_si4126_write(sc, SI4126_GAIN, 0x29c03);
			atw_si4126_write(sc, SI4126_RF1N, 0x1ff6f);
			atw_si4126_write(sc, SI4126_RF2N, 0x29403);
			atw_si4126_write(sc, SI4126_RF2R, 0x1456f);
			atw_si4126_write(sc, 0x09, 0x10050);
			atw_si4126_write(sc, SI4126_IFR, 0x3fff8);
		}
		break;
	default:
		break;
	}
}

/*
 * atw_init:		[ ifnet interface function ]
 *
 *	Initialize the interface.  Must be called at splnet().
 */
int
atw_init(struct ifnet *ifp)
{
	struct atw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct atw_txsoft *txs;
	struct atw_rxsoft *rxs;
	int i, error = 0;

	if ((error = atw_enable(sc)) != 0)
		goto out;

	/*
	 * Cancel any pending I/O. This also resets.
	 */
	atw_stop(ifp, 0);

	ic->ic_bss->ni_chan = ic->ic_ibss_chan;
	DPRINTF(sc, ("%s: channel %d freq %d flags 0x%04x\n",
	    __func__, ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan),
	    ic->ic_bss->ni_chan->ic_freq, ic->ic_bss->ni_chan->ic_flags));

	atw_wcsr_init(sc);

	atw_cmdr_init(sc);

	/* Set data rate for PLCP Signal field, 1Mbps = 10 x 100Kb/s.
	 *
	 * XXX Set transmit power for ATIM, RTS, Beacon.
	 */
	ATW_WRITE(sc, ATW_PLCPHD, LSHIFT(10, ATW_PLCPHD_SIGNAL_MASK) |
	    LSHIFT(0xb0, ATW_PLCPHD_SERVICE_MASK));

	atw_tofs2_init(sc);

	atw_nar_init(sc);

	atw_txlmt_init(sc);

	atw_test1_init(sc);

	atw_rf_reset(sc);

	atw_cfp_init(sc);

	atw_tofs0_init(sc);

	atw_ifs_init(sc);

	/* XXX Fall asleep after one second of inactivity.
	 * XXX A frame may only dribble in for 65536us.
	 */
	ATW_WRITE(sc, ATW_RMD,
	    LSHIFT(1, ATW_RMD_PCNT) | LSHIFT(0xffff, ATW_RMD_RMRD_MASK));

	atw_response_times_init(sc);

	atw_bbp_io_init(sc);

	ATW_WRITE(sc, ATW_STSR, 0xffffffff);

	if ((error = atw_rf3000_init(sc)) != 0)
		goto out;

	ATW_WRITE(sc, ATW_PAR, sc->sc_busmode);
	DPRINTF(sc, ("%s: ATW_PAR %08x busmode %08x\n", sc->sc_dev.dv_xname,
	    ATW_READ(sc, ATW_PAR), sc->sc_busmode));

	/*
	 * Initialize the transmit descriptor ring.
	 */
	memset(sc->sc_txdescs, 0, sizeof(sc->sc_txdescs));
	for (i = 0; i < ATW_NTXDESC; i++) {
		sc->sc_txdescs[i].at_ctl = 0;
		/* no transmit chaining */
		sc->sc_txdescs[i].at_flags = 0 /* ATW_TXFLAG_TCH */;
		sc->sc_txdescs[i].at_buf2 =
		    htole32(ATW_CDTXADDR(sc, ATW_NEXTTX(i)));
	}
	/* use ring mode */
	sc->sc_txdescs[ATW_NTXDESC - 1].at_flags |= htole32(ATW_TXFLAG_TER);
	ATW_CDTXSYNC(sc, 0, ATW_NTXDESC,
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
	sc->sc_txfree = ATW_NTXDESC;
	sc->sc_txnext = 0;

	/*
	 * Initialize the transmit job descriptors.
	 */
	SIMPLEQ_INIT(&sc->sc_txfreeq);
	SIMPLEQ_INIT(&sc->sc_txdirtyq);
	for (i = 0; i < ATW_TXQUEUELEN; i++) {
		txs = &sc->sc_txsoft[i];
		txs->txs_mbuf = NULL;
		SIMPLEQ_INSERT_TAIL(&sc->sc_txfreeq, txs, txs_q);
	}

	/*
	 * Initialize the receive descriptor and receive job
	 * descriptor rings.
	 */
	for (i = 0; i < ATW_NRXDESC; i++) {
		rxs = &sc->sc_rxsoft[i];
		if (rxs->rxs_mbuf == NULL) {
			if ((error = atw_add_rxbuf(sc, i)) != 0) {
				printf("%s: unable to allocate or map rx "
				    "buffer %d, error = %d\n",
				    sc->sc_dev.dv_xname, i, error);
				/*
				 * XXX Should attempt to run with fewer receive
				 * XXX buffers instead of just failing.
				 */
				atw_rxdrain(sc);
				goto out;
			}
		} else
			ATW_INIT_RXDESC(sc, i);
	}
	sc->sc_rxptr = 0;

	/*
	 * Initialize the interrupt mask and enable interrupts.
	 */
	/* normal interrupts */
	sc->sc_inten =  ATW_INTR_TCI | ATW_INTR_TDU | ATW_INTR_RCI |
	    ATW_INTR_NISS | ATW_INTR_LINKON | ATW_INTR_BCNTC;

	/* abnormal interrupts */
	sc->sc_inten |= ATW_INTR_TPS | ATW_INTR_TLT | ATW_INTR_TRT |
	    ATW_INTR_TUF | ATW_INTR_RDU | ATW_INTR_RPS | ATW_INTR_AISS |
	    ATW_INTR_FBE | ATW_INTR_LINKOFF | ATW_INTR_TSFTF | ATW_INTR_TSCZ;

	sc->sc_linkint_mask = ATW_INTR_LINKON | ATW_INTR_LINKOFF |
	    ATW_INTR_BCNTC | ATW_INTR_TSFTF | ATW_INTR_TSCZ;
	sc->sc_rxint_mask = ATW_INTR_RCI | ATW_INTR_RDU;
	sc->sc_txint_mask = ATW_INTR_TCI | ATW_INTR_TUF | ATW_INTR_TLT |
	    ATW_INTR_TRT;

	sc->sc_linkint_mask &= sc->sc_inten;
	sc->sc_rxint_mask &= sc->sc_inten;
	sc->sc_txint_mask &= sc->sc_inten;

	ATW_WRITE(sc, ATW_IER, sc->sc_inten);
	ATW_WRITE(sc, ATW_STSR, 0xffffffff);

	DPRINTF(sc, ("%s: ATW_IER %08x, inten %08x\n",
	    sc->sc_dev.dv_xname, ATW_READ(sc, ATW_IER), sc->sc_inten));

	/*
	 * Give the transmit and receive rings to the ADM8211.
	 */
	ATW_WRITE(sc, ATW_RDB, ATW_CDRXADDR(sc, sc->sc_rxptr));
	ATW_WRITE(sc, ATW_TDBD, ATW_CDTXADDR(sc, sc->sc_txnext));

	sc->sc_txthresh = 0;
	sc->sc_opmode = ATW_NAR_SR | ATW_NAR_ST |
	    sc->sc_txth[sc->sc_txthresh].txth_opmode;

	/* common 802.11 configuration */
	ic->ic_flags &= ~IEEE80211_F_IBSSON;
	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_AHDEMO: /* XXX */
	case IEEE80211_M_IBSS:
		ic->ic_flags |= IEEE80211_F_IBSSON;
		/*FALLTHROUGH*/
#endif
	default: /* XXX */
		break;
	}

#ifndef IEEE80211_STA_ONLY
	switch (ic->ic_opmode) {
	case IEEE80211_M_AHDEMO:
		ic->ic_bss->ni_intval = ic->ic_lintval;
		ic->ic_bss->ni_rssi = 0;
		ic->ic_bss->ni_rstamp = 0;
		break;
	default:					/* XXX */
		break;
	}
#endif
	sc->sc_wepctl = 0;

	atw_write_ssid(sc);
	atw_write_sup_rates(sc);
	if (ic->ic_caps & IEEE80211_C_WEP)
		atw_write_wep(sc);

	ic->ic_state = IEEE80211_S_INIT;

	/*
	 * Set the receive filter.  This will start the transmit and
	 * receive processes.
	 */
	atw_filter_setup(sc);

	/*
	 * Start the receive process.
	 */
	ATW_WRITE(sc, ATW_RDR, 0x1);

	/*
	 * Note that the interface is now running.
	 */
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/* send no beacons, yet. */
	atw_start_beacon(sc, 0);

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		error = ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	else
		error = ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
 out:
	if (error) {
		ifp->if_flags &= ~IFF_RUNNING;
		ifq_clr_oactive(&ifp->if_snd);
		ifp->if_timer = 0;
		printf("%s: interface not running\n", sc->sc_dev.dv_xname);
	}
#ifdef ATW_DEBUG
	atw_print_regs(sc, "end of init");
#endif /* ATW_DEBUG */

	return (error);
}

/* enable == 1: host control of RF3000/Si4126 through ATW_SYNCTL.
 *           0: MAC control of RF3000/Si4126.
 *
 * Applies power, or selects RF front-end? Sets reset condition.
 *
 * TBD support non-RFMD BBP, non-SiLabs synth.
 */
void
atw_bbp_io_enable(struct atw_softc *sc, int enable)
{
	if (enable) {
		ATW_WRITE(sc, ATW_SYNRF,
		    ATW_SYNRF_SELRF|ATW_SYNRF_PE1|ATW_SYNRF_PHYRST);
		DELAY(atw_bbp_io_enable_delay);
	} else {
		ATW_WRITE(sc, ATW_SYNRF, 0);
		DELAY(atw_bbp_io_disable_delay); /* shorter for some reason */
	}
}

int
atw_tune(struct atw_softc *sc)
{
	int rc;
	u_int chan;
	struct ieee80211com *ic = &sc->sc_ic;

	chan = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
	if (chan == 0 || chan == IEEE80211_CHAN_ANY)
		return 0;

	if (chan == sc->sc_cur_chan)
		return 0;

	DPRINTF(sc, ("%s: chan %d -> %d\n", sc->sc_dev.dv_xname,
	    sc->sc_cur_chan, chan));

	atw_idle(sc, ATW_NAR_SR|ATW_NAR_ST);

	atw_si4126_tune(sc, chan);
	if ((rc = atw_rf3000_tune(sc, chan)) != 0)
		printf("%s: failed to tune channel %d\n", sc->sc_dev.dv_xname,
		    chan);

	ATW_WRITE(sc, ATW_NAR, sc->sc_opmode);
	DELAY(20 * 1000);
	ATW_WRITE(sc, ATW_RDR, 0x1);

	if (rc == 0)
		sc->sc_cur_chan = chan;

	return rc;
}

#ifdef ATW_SYNDEBUG
void
atw_si4126_print(struct atw_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	u_int addr, val;

	if (atw_debug < 3 || (ifp->if_flags & IFF_DEBUG) == 0)
		return;

	for (addr = 0; addr <= 8; addr++) {
		printf("%s: synth[%d] = ", sc->sc_dev.dv_xname, addr);
		if (atw_si4126_read(sc, addr, &val) == 0) {
			printf("<unknown> (quitting print-out)\n");
			break;
		}
		printf("%05x\n", val);
	}
}
#endif /* ATW_SYNDEBUG */

/* Tune to channel chan by adjusting the Si4126 RF/IF synthesizer.
 *
 * The RF/IF synthesizer produces two reference frequencies for
 * the RF2948B transceiver.  The first frequency the RF2948B requires
 * is two times the so-called "intermediate frequency" (IF). Since
 * a SAW filter on the radio fixes the IF at 374MHz, I program the
 * Si4126 to generate IF LO = 374MHz x 2 = 748MHz.  The second
 * frequency required by the transceiver is the radio frequency
 * (RF). This is a superheterodyne transceiver; for f(chan) the
 * center frequency of the channel we are tuning, RF = f(chan) -
 * IF.
 *
 * XXX I am told by SiLabs that the Si4126 will accept a broader range
 * of XIN than the 2-25MHz mentioned by the datasheet, even *without*
 * XINDIV2 = 1.  I've tried this (it is necessary to double R) and it
 * works, but I have still programmed for XINDIV2 = 1 to be safe.
 */
void
atw_si4126_tune(struct atw_softc *sc, u_int chan)
{
	u_int mhz;
	u_int R;
	u_int32_t gpio;
	u_int16_t gain;

#ifdef ATW_SYNDEBUG
	atw_si4126_print(sc);
#endif /* ATW_SYNDEBUG */

	if (sc->sc_rev >= ATW_REVISION_BA) {
		atw_si4126_write(sc, SI4126_MAIN, 0x04007);
		atw_si4126_write(sc, SI4126_POWER, 0x00033);
		atw_si4126_write(sc, SI4126_IFN,
		    atw_rfmd2958_ifn[chan - 1]);
		atw_si4126_write(sc, SI4126_RF1R,
		    atw_rfmd2958_rf1r[chan - 1]);
#ifdef NOTYET
		/* set TX POWER? */
		atw_si4126_write(sc, 0x0a,
		    (sc->sc_srom[ATW_SR_CSR20] & mask) |
		    power << 9);
#endif
		/* set TX GAIN */
		atw_si4126_write(sc, 0x09, 0x00050 |
		    sc->sc_srom[ATW_SR_TXPOWER(chan - 1)]);
		/* wait 100us from power-up for RF, IF to settle */
		DELAY(100);

		return;
	}

	if (chan == 14)
		mhz = 2484;
	else
		mhz = 2412 + 5 * (chan - 1);

	/* Tune IF to 748MHz to suit the IF LO input of the
	 * RF2494B, which is 2 x IF. No need to set an IF divider
         * because an IF in 526MHz - 952MHz is allowed.
	 *
	 * XIN is 44.000MHz, so divide it by two to get allowable
	 * range of 2-25MHz. SiLabs tells me that this is not
	 * strictly necessary.
	 */

	if (atw_xindiv2)
		R = 44;
	else
		R = 88;

	/* Power-up RF, IF synthesizers. */
	atw_si4126_write(sc, SI4126_POWER,
	    SI4126_POWER_PDIB|SI4126_POWER_PDRB);

	/* set LPWR, too? */
	atw_si4126_write(sc, SI4126_MAIN,
	    (atw_xindiv2) ? SI4126_MAIN_XINDIV2 : 0);

	/* Set the phase-locked loop gain.  If RF2 N > 2047, then
	 * set KP2 to 1.
	 *
	 * REFDIF This is different from the reference driver, which
	 * always sets SI4126_GAIN to 0.
	 */
	gain = LSHIFT(((mhz - 374) > 2047) ? 1 : 0, SI4126_GAIN_KP2_MASK);

	atw_si4126_write(sc, SI4126_GAIN, gain);

	/* XIN = 44MHz.
	 *
	 * If XINDIV2 = 1, IF = N/(2 * R) * XIN.  I choose N = 1496,
	 * R = 44 so that 1496/(2 * 44) * 44MHz = 748MHz.
	 *
	 * If XINDIV2 = 0, IF = N/R * XIN.  I choose N = 1496, R = 88
	 * so that 1496/88 * 44MHz = 748MHz.
	 */
	atw_si4126_write(sc, SI4126_IFN, 1496);

	atw_si4126_write(sc, SI4126_IFR, R);

#ifndef ATW_REFSLAVE
	/* Set RF1 arbitrarily. DO NOT configure RF1 after RF2, because
	 * then RF1 becomes the active RF synthesizer, even on the Si4126,
	 * which has no RF1!
	 */
	atw_si4126_write(sc, SI4126_RF1R, R);

	atw_si4126_write(sc, SI4126_RF1N, mhz - 374);
#endif

	/* N/R * XIN = RF. XIN = 44MHz. We desire RF = mhz - IF,
	 * where IF = 374MHz.  Let's divide XIN to 1MHz. So R = 44.
	 * Now let's multiply it to mhz. So mhz - IF = N.
	 */
	atw_si4126_write(sc, SI4126_RF2R, R);

	atw_si4126_write(sc, SI4126_RF2N, mhz - 374);

	/* wait 100us from power-up for RF, IF to settle */
	DELAY(100);

	gpio = ATW_READ(sc, ATW_GPIO);
	gpio &= ~(ATW_GPIO_EN_MASK|ATW_GPIO_O_MASK|ATW_GPIO_I_MASK);
	gpio |= LSHIFT(1, ATW_GPIO_EN_MASK);

	if ((sc->sc_if.if_flags & IFF_LINK1) != 0 && chan != 14) {
		/* Set a Prism RF front-end to a special mode for channel 14?
		 *
		 * Apparently the SMC2635W needs this, although I don't think
		 * it has a Prism RF.
		 */
		gpio |= LSHIFT(1, ATW_GPIO_O_MASK);
	}
	ATW_WRITE(sc, ATW_GPIO, gpio);

#ifdef ATW_SYNDEBUG
	atw_si4126_print(sc);
#endif /* ATW_SYNDEBUG */
}

/* Baseline initialization of RF3000 BBP: set CCA mode and enable antenna
 * diversity.
 *
 * !!!
 * !!! Call this w/ Tx/Rx suspended, atw_idle(, ATW_NAR_ST|ATW_NAR_SR).
 * !!!
 */
int
atw_rf3000_init(struct atw_softc *sc)
{
	int rc = 0;

	atw_bbp_io_enable(sc, 1);

	/* CCA is acquisition sensitive */
	rc = atw_rf3000_write(sc, RF3000_CCACTL,
	    LSHIFT(RF3000_CCACTL_MODE_BOTH, RF3000_CCACTL_MODE_MASK));

	if (rc != 0)
		goto out;

	/* enable diversity */
	rc = atw_rf3000_write(sc, RF3000_DIVCTL, RF3000_DIVCTL_ENABLE);

	if (rc != 0)
		goto out;

	/* sensible setting from a binary-only driver */
	rc = atw_rf3000_write(sc, RF3000_GAINCTL,
	    LSHIFT(0x1d, RF3000_GAINCTL_TXVGC_MASK));

	if (rc != 0)
		goto out;

	/* magic from a binary-only driver */
	rc = atw_rf3000_write(sc, RF3000_LOGAINCAL,
	    LSHIFT(0x38, RF3000_LOGAINCAL_CAL_MASK));

	if (rc != 0)
		goto out;

	rc = atw_rf3000_write(sc, RF3000_HIGAINCAL, RF3000_HIGAINCAL_DSSSPAD);

	if (rc != 0)
		goto out;

	/*
	 * XXX Reference driver remarks that Abocom sets this to 50.
	 * Meaning 0x50, I think....  50 = 0x32, which would set a bit
	 * in the "reserved" area of register RF3000_OPTIONS1.
	 */
	rc = atw_rf3000_write(sc, RF3000_OPTIONS1, sc->sc_rf3000_options1);

	if (rc != 0)
		goto out;

	rc = atw_rf3000_write(sc, RF3000_OPTIONS2, sc->sc_rf3000_options2);

	if (rc != 0)
		goto out;

out:
	atw_bbp_io_enable(sc, 0);
	return rc;
}

#ifdef ATW_BBPDEBUG
void
atw_rf3000_print(struct atw_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	u_int addr, val;

	if (atw_debug < 3 || (ifp->if_flags & IFF_DEBUG) == 0)
		return;

	for (addr = 0x01; addr <= 0x15; addr++) {
		printf("%s: bbp[%d] = \n", sc->sc_dev.dv_xname, addr);
		if (atw_rf3000_read(sc, addr, &val) != 0) {
			printf("<unknown> (quitting print-out)\n");
			break;
		}
		printf("%08x\n", val);
	}
}
#endif /* ATW_BBPDEBUG */

/* Set the power settings on the BBP for channel `chan'. */
int
atw_rf3000_tune(struct atw_softc *sc, u_int chan)
{
	int rc = 0;
	u_int32_t reg;
	u_int16_t txpower, lpf_cutoff, lna_gs_thresh;

	txpower = sc->sc_srom[ATW_SR_TXPOWER(chan)];
	lpf_cutoff = sc->sc_srom[ATW_SR_LPF_CUTOFF(chan)];
	lna_gs_thresh = sc->sc_srom[ATW_SR_LNA_GS_THRESH(chan)];

	/* odd channels: LSB, even channels: MSB */
	if (chan % 2 == 1) {
		txpower &= 0xFF;
		lpf_cutoff &= 0xFF;
		lna_gs_thresh &= 0xFF;
	} else {
		txpower >>= 8;
		lpf_cutoff >>= 8;
		lna_gs_thresh >>= 8;
	}

#ifdef ATW_BBPDEBUG
	atw_rf3000_print(sc);
#endif /* ATW_BBPDEBUG */

	DPRINTF(sc, ("%s: chan %d txpower %02x, lpf_cutoff %02x, "
	    "lna_gs_thresh %02x\n",
	    sc->sc_dev.dv_xname, chan, txpower, lpf_cutoff, lna_gs_thresh));

	atw_bbp_io_enable(sc, 1);

	if ((rc = atw_rf3000_write(sc, RF3000_GAINCTL,
	    LSHIFT(txpower, RF3000_GAINCTL_TXVGC_MASK))) != 0)
		goto out;

	if ((rc = atw_rf3000_write(sc, RF3000_LOGAINCAL, lpf_cutoff)) != 0)
		goto out;

	if ((rc = atw_rf3000_write(sc, RF3000_HIGAINCAL, lna_gs_thresh)) != 0)
		goto out;

	if ((rc = atw_rf3000_write(sc, RF3000_OPTIONS1, 0x0)) != 0)
		goto out;

	rc = atw_rf3000_write(sc, RF3000_OPTIONS2, RF3000_OPTIONS2_LNAGS_DELAY);
	if (rc != 0)
		goto out;

#ifdef ATW_BBPDEBUG
	atw_rf3000_print(sc);
#endif /* ATW_BBPDEBUG */

out:
	atw_bbp_io_enable(sc, 0);

	/* set beacon, rts, atim transmit power */
	reg = ATW_READ(sc, ATW_PLCPHD);
	reg &= ~ATW_PLCPHD_SERVICE_MASK;
	reg |= LSHIFT(LSHIFT(txpower, RF3000_GAINCTL_TXVGC_MASK),
	    ATW_PLCPHD_SERVICE_MASK);
	ATW_WRITE(sc, ATW_PLCPHD, reg);
	DELAY(2 * 1000);

	return rc;
}

/* Write a register on the RF3000 baseband processor using the
 * registers provided by the ADM8211 for this purpose.
 *
 * Return 0 on success.
 */
int
atw_rf3000_write(struct atw_softc *sc, u_int addr, u_int val)
{
	u_int32_t reg;
	int i;

	reg = sc->sc_bbpctl_wr |
	     LSHIFT(val & 0xff, ATW_BBPCTL_DATA_MASK) |
	     LSHIFT(addr & 0x7f, ATW_BBPCTL_ADDR_MASK);

	for (i = 10; --i >= 0; ) {
		ATW_WRITE(sc, ATW_BBPCTL, reg);
		DELAY(2000);
		if (ATW_ISSET(sc, ATW_BBPCTL, ATW_BBPCTL_WR) == 0)
			break;
	}

	if (i < 0) {
		printf("%s: BBPCTL still busy\n", sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}
	return 0;
}

/* Read a register on the RF3000 baseband processor using the registers
 * the ADM8211 provides for this purpose.
 *
 * The 7-bit register address is addr.  Record the 8-bit data in the register
 * in *val.
 *
 * Return 0 on success.
 *
 * XXX This does not seem to work. The ADM8211 must require more or
 * different magic to read the chip than to write it. Possibly some
 * of the magic I have derived from a binary-only driver concerns
 * the "chip address" (see the RF3000 manual).
 */
#ifdef ATW_BBPDEBUG
int
atw_rf3000_read(struct atw_softc *sc, u_int addr, u_int *val)
{
	u_int32_t reg;
	int i;

	for (i = 1000; --i >= 0; ) {
		if (ATW_ISSET(sc, ATW_BBPCTL, ATW_BBPCTL_RD|ATW_BBPCTL_WR) == 0)
			break;
		DELAY(100);
	}

	if (i < 0) {
		printf("%s: start atw_rf3000_read, BBPCTL busy\n",
		    sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}

	reg = sc->sc_bbpctl_rd | LSHIFT(addr & 0x7f, ATW_BBPCTL_ADDR_MASK);

	ATW_WRITE(sc, ATW_BBPCTL, reg);

	for (i = 1000; --i >= 0; ) {
		DELAY(100);
		if (ATW_ISSET(sc, ATW_BBPCTL, ATW_BBPCTL_RD) == 0)
			break;
	}

	ATW_CLR(sc, ATW_BBPCTL, ATW_BBPCTL_RD);

	if (i < 0) {
		printf("%s: atw_rf3000_read wrote %08x; BBPCTL still busy\n",
		    sc->sc_dev.dv_xname, reg);
		return ETIMEDOUT;
	}
	if (val != NULL)
		*val = MASK_AND_RSHIFT(reg, ATW_BBPCTL_DATA_MASK);
	return 0;
}
#endif /* ATW_BBPDEBUG */

/* Write a register on the Si4126 RF/IF synthesizer using the registers
 * provided by the ADM8211 for that purpose.
 *
 * val is 18 bits of data, and val is the 4-bit address of the register.
 *
 * Return 0 on success.
 */
void
atw_si4126_write(struct atw_softc *sc, u_int addr, u_int val)
{
	uint32_t bits, mask, reg;
	int nbits;

	if (sc->sc_rev >= ATW_REVISION_BA) {
		nbits = 24;

		val &= 0x3ffff;
		addr &= 0x1f;
		bits = val | (addr << 18);
	} else {
		nbits = 22;

		KASSERT((addr & ~PRESHIFT(SI4126_TWI_ADDR_MASK)) == 0);
		KASSERT((val & ~PRESHIFT(SI4126_TWI_DATA_MASK)) == 0);

		bits = LSHIFT(val, SI4126_TWI_DATA_MASK) |
		    LSHIFT(addr, SI4126_TWI_ADDR_MASK);
	}

	reg = ATW_SYNRF_SELSYN;
	/* reference driver: reset Si4126 serial bus to initial
	 * conditions?
	 */
	ATW_WRITE(sc, ATW_SYNRF, reg | ATW_SYNRF_LEIF);
	ATW_WRITE(sc, ATW_SYNRF, reg);

	for (mask = (1 << (nbits - 1)); mask != 0; mask >>= 1) {
		if ((bits & mask) != 0)
			reg |= ATW_SYNRF_SYNDATA;
		else
			reg &= ~ATW_SYNRF_SYNDATA;
		ATW_WRITE(sc, ATW_SYNRF, reg);
		ATW_WRITE(sc, ATW_SYNRF, reg | ATW_SYNRF_SYNCLK);
		ATW_WRITE(sc, ATW_SYNRF, reg);
	}
	ATW_WRITE(sc, ATW_SYNRF, reg | ATW_SYNRF_LEIF);
	ATW_WRITE(sc, ATW_SYNRF, 0x0);
}

/* Read 18-bit data from the 4-bit address addr in Si4126
 * RF synthesizer and write the data to *val. Return 0 on success.
 *
 * XXX This does not seem to work. The ADM8211 must require more or
 * different magic to read the chip than to write it.
 */
#ifdef ATW_SYNDEBUG
int
atw_si4126_read(struct atw_softc *sc, u_int addr, u_int *val)
{
	u_int32_t reg;
	int i;

	KASSERT((addr & ~PRESHIFT(SI4126_TWI_ADDR_MASK)) == 0);

	for (i = 1000; --i >= 0; ) {
		if (ATW_ISSET(sc, ATW_SYNCTL, ATW_SYNCTL_RD|ATW_SYNCTL_WR) == 0)
			break;
		DELAY(100);
	}

	if (i < 0) {
		printf("%s: start atw_si4126_read, SYNCTL busy\n",
		    sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}

	reg = sc->sc_synctl_rd | LSHIFT(addr, ATW_SYNCTL_DATA_MASK);

	ATW_WRITE(sc, ATW_SYNCTL, reg);

	for (i = 1000; --i >= 0; ) {
		DELAY(100);
		if (ATW_ISSET(sc, ATW_SYNCTL, ATW_SYNCTL_RD) == 0)
			break;
	}

	ATW_CLR(sc, ATW_SYNCTL, ATW_SYNCTL_RD);

	if (i < 0) {
		printf("%s: atw_si4126_read wrote %#08x, SYNCTL still busy\n",
		    sc->sc_dev.dv_xname, reg);
		return ETIMEDOUT;
	}
	if (val != NULL)
		*val = MASK_AND_RSHIFT(ATW_READ(sc, ATW_SYNCTL),
		                       ATW_SYNCTL_DATA_MASK);
	return 0;
}
#endif /* ATW_SYNDEBUG */

/* XXX is the endianness correct? test. */
#define	atw_calchash(addr) \
	(ether_crc32_le((addr), IEEE80211_ADDR_LEN) & 0x3f)

/*
 * atw_filter_setup:
 *
 *	Set the ADM8211's receive filter.
 */
void
atw_filter_setup(struct atw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct arpcom *ac = &ic->ic_ac;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int hash;
	u_int32_t hashes[2];
	struct ether_multi *enm;
	struct ether_multistep step;

	/* According to comments in tlp_al981_filter_setup
	 * (dev/ic/tulip.c) the ADMtek AL981 does not like for its
	 * multicast filter to be set while it is running.  Hopefully
	 * the ADM8211 is not the same!
	 */
	if ((ifp->if_flags & IFF_RUNNING) != 0)
		atw_idle(sc, ATW_NAR_SR);

	sc->sc_opmode &= ~(ATW_NAR_PR|ATW_NAR_MM);

	/* XXX in scan mode, do not filter packets.  Maybe this is
	 * unnecessary.
	 */
	if (ic->ic_state == IEEE80211_S_SCAN ||
	    (ifp->if_flags & IFF_PROMISC) != 0) {
		sc->sc_opmode |= ATW_NAR_PR;
		goto allmulti;
	}

	hashes[0] = hashes[1] = 0x0;

	if (ac->ac_multirangecnt > 0)
		goto allmulti;

	/*
	 * Program the 64-bit multicast hash filter.
	 */
	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		hash = atw_calchash(enm->enm_addrlo);
		hashes[hash >> 5] |= 1 << (hash & 0x1f);
		ETHER_NEXT_MULTI(step, enm);
		sc->sc_opmode |= ATW_NAR_MM;
	}
	ifp->if_flags &= ~IFF_ALLMULTI;
	goto setit;

allmulti:
	sc->sc_opmode |= ATW_NAR_MM;
	ifp->if_flags |= IFF_ALLMULTI;
	hashes[0] = hashes[1] = 0xffffffff;

setit:
	ATW_WRITE(sc, ATW_MAR0, hashes[0]);
	ATW_WRITE(sc, ATW_MAR1, hashes[1]);
	ATW_WRITE(sc, ATW_NAR, sc->sc_opmode);
	DELAY(20 * 1000);
	ATW_WRITE(sc, ATW_RDR, 0x1);

	DPRINTF(sc, ("%s: ATW_NAR %08x opmode %08x\n", sc->sc_dev.dv_xname,
	    ATW_READ(sc, ATW_NAR), sc->sc_opmode));
}

/* Tell the ADM8211 our preferred BSSID. The ADM8211 must match
 * a beacon's BSSID and SSID against the preferred BSSID and SSID
 * before it will raise ATW_INTR_LINKON. When the ADM8211 receives
 * no beacon with the preferred BSSID and SSID in the number of
 * beacon intervals given in ATW_BPLI, then it raises ATW_INTR_LINKOFF.
 */
void
atw_write_bssid(struct atw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	u_int8_t *bssid;

	bssid = ic->ic_bss->ni_bssid;

	ATW_WRITE(sc, ATW_BSSID0,
	    LSHIFT(bssid[0], ATW_BSSID0_BSSIDB0_MASK) |
	    LSHIFT(bssid[1], ATW_BSSID0_BSSIDB1_MASK) |
	    LSHIFT(bssid[2], ATW_BSSID0_BSSIDB2_MASK) |
	    LSHIFT(bssid[3], ATW_BSSID0_BSSIDB3_MASK));

	ATW_WRITE(sc, ATW_ABDA1,
	    (ATW_READ(sc, ATW_ABDA1) &
	    ~(ATW_ABDA1_BSSIDB4_MASK|ATW_ABDA1_BSSIDB5_MASK)) |
	    LSHIFT(bssid[4], ATW_ABDA1_BSSIDB4_MASK) |
	    LSHIFT(bssid[5], ATW_ABDA1_BSSIDB5_MASK));

	DPRINTF(sc, ("%s: BSSID %s -> ", sc->sc_dev.dv_xname,
	    ether_sprintf(sc->sc_bssid)));
	DPRINTF(sc, ("%s\n", ether_sprintf(bssid)));

	memcpy(sc->sc_bssid, bssid, sizeof(sc->sc_bssid));
}

/* Write buflen bytes from buf to SRAM starting at the SRAM's ofs'th
 * 16-bit word.
 */
void
atw_write_sram(struct atw_softc *sc, u_int ofs, u_int8_t *buf, u_int buflen)
{
	u_int i;
	u_int8_t *ptr;

	memcpy(&sc->sc_sram[ofs], buf, buflen);

	KASSERT(ofs % 2 == 0 && buflen % 2 == 0);

	KASSERT(buflen + ofs <= sc->sc_sramlen);

	ptr = &sc->sc_sram[ofs];

	for (i = 0; i < buflen; i += 2) {
		ATW_WRITE(sc, ATW_WEPCTL, ATW_WEPCTL_WR |
		    LSHIFT((ofs + i) / 2, ATW_WEPCTL_TBLADD_MASK));
		DELAY(atw_writewep_delay);

		ATW_WRITE(sc, ATW_WESK,
		    LSHIFT((ptr[i + 1] << 8) | ptr[i], ATW_WESK_DATA_MASK));
		DELAY(atw_writewep_delay);
	}
	ATW_WRITE(sc, ATW_WEPCTL, sc->sc_wepctl); /* restore WEP condition */

	if (sc->sc_if.if_flags & IFF_DEBUG) {
		int n_octets = 0;
		printf("%s: wrote %d bytes at 0x%x wepctl 0x%08x\n",
		    sc->sc_dev.dv_xname, buflen, ofs, sc->sc_wepctl);
		for (i = 0; i < buflen; i++) {
			printf(" %02x", ptr[i]);
			if (++n_octets % 24 == 0)
				printf("\n");
		}
		if (n_octets % 24 != 0)
			printf("\n");
	}
}

/* Write WEP keys from the ieee80211com to the ADM8211's SRAM. */
void
atw_write_wep(struct atw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
#if 0
	u_int32_t reg;
	int i;
#endif
	/* SRAM shared-key record format: key0 flags key1 ... key12 */
	u_int8_t buf[IEEE80211_WEP_NKID]
	            [1 /* key[0] */ + 1 /* flags */ + 12 /* key[1 .. 12] */];

	sc->sc_wepctl = 0;
	ATW_WRITE(sc, ATW_WEPCTL, sc->sc_wepctl);

	if ((ic->ic_flags & IEEE80211_F_WEPON) == 0)
		return;

	memset(&buf[0][0], 0, sizeof(buf));

#if 0
	for (i = 0; i < IEEE80211_WEP_NKID; i++) {
		if (ic->ic_nw_keys[i].k_len > 5) {
			buf[i][1] = ATW_WEP_ENABLED | ATW_WEP_104BIT;
		} else if (ic->ic_nw_keys[i].k_len != 0) {
			buf[i][1] = ATW_WEP_ENABLED;
		} else {
			buf[i][1] = 0;
			continue;
		}
		buf[i][0] = ic->ic_nw_keys[i].k_key[0];
		memcpy(&buf[i][2], &ic->ic_nw_keys[i].k_key[1],
		    ic->ic_nw_keys[i].k_len - 1);
	}

	reg = ATW_READ(sc, ATW_MACTEST);
	reg |= ATW_MACTEST_MMI_USETXCLK | ATW_MACTEST_FORCE_KEYID;
	reg &= ~ATW_MACTEST_KEYID_MASK;
	reg |= LSHIFT(ic->ic_wep_txkey, ATW_MACTEST_KEYID_MASK);
	ATW_WRITE(sc, ATW_MACTEST, reg);

	sc->sc_wepctl = ATW_WEPCTL_WEPENABLE;

	switch (sc->sc_rev) {
	case ATW_REVISION_AB:
	case ATW_REVISION_AF:
		/* Bypass WEP on Rx. */
		sc->sc_wepctl |= ATW_WEPCTL_WEPRXBYP;
		break;
	default:
		break;
	}
#endif

	atw_write_sram(sc, ATW_SRAM_ADDR_SHARED_KEY, (u_int8_t*)&buf[0][0],
	    sizeof(buf));
}

void
atw_change_ibss(struct atw_softc *sc)
{
	atw_predict_beacon(sc);
	atw_write_bssid(sc);
	atw_start_beacon(sc, 1);
}

#ifndef IEEE80211_STA_ONLY
void
atw_recv_mgmt(struct ieee80211com *ic, struct mbuf *m,
    struct ieee80211_node *ni, struct ieee80211_rxinfo *rxi, int subtype)
{
	struct atw_softc *sc = (struct atw_softc*)ic->ic_softc;

	/* The ADM8211A answers probe requests. */
	if (subtype == IEEE80211_FC0_SUBTYPE_PROBE_REQ &&
	    sc->sc_rev < ATW_REVISION_BA)
		return;

	(*sc->sc_recv_mgmt)(ic, m, ni, rxi, subtype);

	switch (subtype) {
	case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
	case IEEE80211_FC0_SUBTYPE_BEACON:
		if (ic->ic_opmode != IEEE80211_M_IBSS ||
		    ic->ic_state != IEEE80211_S_RUN)
			break;
		if (ieee80211_ibss_merge(ic, ni, atw_get_tsft(sc)) == ENETRESET)
			atw_change_ibss(sc);
		break;
	default:
		break;
	}
	return;
}
#endif

/* Write the SSID in the ieee80211com to the SRAM on the ADM8211.
 * In ad hoc mode, the SSID is written to the beacons sent by the
 * ADM8211. In both ad hoc and infrastructure mode, beacons received
 * with matching SSID affect ATW_INTR_LINKON/ATW_INTR_LINKOFF
 * indications.
 */
void
atw_write_ssid(struct atw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	/* 34 bytes are reserved in ADM8211 SRAM for the SSID, but
	 * it only expects the element length, not its ID.
	 */
	u_int8_t buf[roundup(1 /* length */ + IEEE80211_NWID_LEN, 2)];

	memset(buf, 0, sizeof(buf));
	buf[0] = ic->ic_bss->ni_esslen;
	memcpy(&buf[1], ic->ic_bss->ni_essid, ic->ic_bss->ni_esslen);

	atw_write_sram(sc, ATW_SRAM_ADDR_SSID, buf,
	    roundup(1 + ic->ic_bss->ni_esslen, 2));
}

/* Write the supported rates in the ieee80211com to the SRAM of the ADM8211.
 * In ad hoc mode, the supported rates are written to beacons sent by the
 * ADM8211.
 */
void
atw_write_sup_rates(struct atw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	/*
	 * There is not enough space in the ADM8211 SRAM for the
	 * full IEEE80211_RATE_MAXSIZE
	 */
	u_int8_t buf[12];
	u_int8_t nrates;

	memset(buf, 0, sizeof(buf));
	if (ic->ic_bss->ni_rates.rs_nrates > sizeof(buf) - 1)
		nrates = sizeof(buf) - 1;
	else
		nrates = ic->ic_bss->ni_rates.rs_nrates;
	buf[0] = nrates;
	memcpy(&buf[1], ic->ic_bss->ni_rates.rs_rates, nrates);

	/* XXX deal with rev BA bug linux driver talks of? */

	atw_write_sram(sc, ATW_SRAM_ADDR_SUPRATES, buf, sizeof(buf));
}

/* Start/stop sending beacons. */
void
atw_start_beacon(struct atw_softc *sc, int start)
{
	struct ieee80211com *ic = &sc->sc_ic;
#ifndef IEEE80211_STA_ONLY
	uint16_t chan;
	uint32_t bpli;
#endif
	uint32_t bcnt, cap0, cap1, capinfo;
	size_t len;

	if (ATW_IS_ENABLED(sc) == 0)
		return;

	/* start beacons */
	len = sizeof(struct ieee80211_frame) +
	    8 /* timestamp */ + 2 /* beacon interval */ +
	    2 /* capability info */ +
	    2 + ic->ic_bss->ni_esslen /* SSID element */ +
	    2 + ic->ic_bss->ni_rates.rs_nrates /* rates element */ +
	    3 /* DS parameters */ +
	    IEEE80211_CRC_LEN;

	bcnt = ATW_READ(sc, ATW_BCNT) & ~ATW_BCNT_BCNT_MASK;
	cap0 = ATW_READ(sc, ATW_CAP0) & ~ATW_CAP0_CHN_MASK;
	cap1 = ATW_READ(sc, ATW_CAP1) & ~ATW_CAP1_CAPI_MASK;

	ATW_WRITE(sc, ATW_BCNT, bcnt);
	ATW_WRITE(sc, ATW_CAP1, cap1);

	if (!start)
		return;

	/* TBD use ni_capinfo */

	capinfo = 0;
	if (sc->sc_flags & ATWF_SHORT_PREAMBLE)
		capinfo |= IEEE80211_CAPINFO_SHORT_PREAMBLE;
	if (ic->ic_flags & IEEE80211_F_WEPON)
		capinfo |= IEEE80211_CAPINFO_PRIVACY;

#ifndef IEEE80211_STA_ONLY
	switch (ic->ic_opmode) {
	case IEEE80211_M_IBSS:
		len += 4; /* IBSS parameters */
		capinfo |= IEEE80211_CAPINFO_IBSS;
		break;
	default:
		return;
	}

	/* set listen interval
	 * XXX do software units agree w/ hardware?
	 */
	bpli = LSHIFT(ic->ic_bss->ni_intval, ATW_BPLI_BP_MASK) |
	    LSHIFT(ic->ic_lintval / ic->ic_bss->ni_intval, ATW_BPLI_LI_MASK);

	chan = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);

	bcnt |= LSHIFT(len, ATW_BCNT_BCNT_MASK);
	cap0 |= LSHIFT(chan, ATW_CAP0_CHN_MASK);
	cap1 |= LSHIFT(capinfo, ATW_CAP1_CAPI_MASK);

	ATW_WRITE(sc, ATW_BCNT, bcnt);
	ATW_WRITE(sc, ATW_BPLI, bpli);
	ATW_WRITE(sc, ATW_CAP0, cap0);
	ATW_WRITE(sc, ATW_CAP1, cap1);

	DPRINTF(sc, ("%s: atw_start_beacon reg[ATW_BCNT] = %08x\n",
	    sc->sc_dev.dv_xname, bcnt));
	DPRINTF(sc, ("%s: atw_start_beacon reg[ATW_CAP1] = %08x\n",
	    sc->sc_dev.dv_xname, cap1));
#endif
}

/* Return the 32 lsb of the last TSFT divisible by ival. */
static __inline uint32_t
atw_last_even_tsft(uint32_t tsfth, uint32_t tsftl, uint32_t ival)
{
	/* Following the reference driver's lead, I compute
	 *
	 *   (uint32_t)((((uint64_t)tsfth << 32) | tsftl) % ival)
	 *
	 * without using 64-bit arithmetic, using the following
	 * relationship:
	 *
	 *     (0x100000000 * H + L) % m
	 *   = ((0x100000000 % m) * H + L) % m
	 *   = (((0xffffffff + 1) % m) * H + L) % m
	 *   = ((0xffffffff % m + 1 % m) * H + L) % m
	 *   = ((0xffffffff % m + 1) * H + L) % m
	 */
	return ((0xFFFFFFFF % ival + 1) * tsfth + tsftl) % ival;
}

uint64_t
atw_get_tsft(struct atw_softc *sc)
{
	int i;
	uint32_t tsfth, tsftl;
	for (i = 0; i < 2; i++) {
		tsfth = ATW_READ(sc, ATW_TSFTH);
		tsftl = ATW_READ(sc, ATW_TSFTL);
		if (ATW_READ(sc, ATW_TSFTH) == tsfth)
			break;
	}
	return ((uint64_t)tsfth << 32) | tsftl;
}

/* If we've created an IBSS, write the TSF time in the ADM8211 to
 * the ieee80211com.
 *
 * Predict the next target beacon transmission time (TBTT) and
 * write it to the ADM8211.
 */
void
atw_predict_beacon(struct atw_softc *sc)
{
#define TBTTOFS 20 /* TU */

	struct ieee80211com *ic = &sc->sc_ic;
	uint64_t tsft;
	uint32_t ival, past_even, tbtt, tsfth, tsftl;
	union {
		uint64_t	word;
		uint8_t		tstamp[8];
	} u;

#ifndef IEEE80211_STA_ONLY
	if ((ic->ic_opmode == IEEE80211_M_IBSS) &&
	    (ic->ic_flags & IEEE80211_F_SIBSS)) {
		tsft = atw_get_tsft(sc);
		u.word = htole64(tsft);
		(void)memcpy(&ic->ic_bss->ni_tstamp[0], &u.tstamp[0],
		    sizeof(ic->ic_bss->ni_tstamp));
	} else
#endif
	{
		(void)memcpy(&u, &ic->ic_bss->ni_tstamp[0], sizeof(u));
		tsft = letoh64(u.word);
	}

	ival = ic->ic_bss->ni_intval * IEEE80211_DUR_TU;

	tsftl = tsft & 0xFFFFFFFF;
	tsfth = tsft >> 32;

	/* We sent/received the last beacon `past' microseconds
	 * after the interval divided the TSF timer.
	 */
	past_even = tsftl - atw_last_even_tsft(tsfth, tsftl, ival);

	/* Skip ten beacons so that the TBTT cannot pass before
	 * we've programmed it.  Ten is an arbitrary number.
	 */
	tbtt = past_even + ival * 10;

	ATW_WRITE(sc, ATW_TOFS1,
	    LSHIFT(1, ATW_TOFS1_TSFTOFSR_MASK) |
	    LSHIFT(TBTTOFS, ATW_TOFS1_TBTTOFS_MASK) |
	    LSHIFT(MASK_AND_RSHIFT(tbtt - TBTTOFS * IEEE80211_DUR_TU,
	        ATW_TBTTPRE_MASK), ATW_TOFS1_TBTTPRE_MASK));
#undef TBTTOFS
}

void
atw_next_scan(void *arg)
{
	struct atw_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	int s;

	/* don't call atw_start w/o network interrupts blocked */
	s = splnet();
	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(ifp);
	splx(s);
}

/* Synchronize the hardware state with the software state. */
int
atw_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct ifnet *ifp = &ic->ic_if;
	struct atw_softc *sc = ifp->if_softc;
	enum ieee80211_state ostate = ic->ic_state;
	int error;

	if (nstate == IEEE80211_S_INIT) {
		timeout_del(&sc->sc_scan_to);
		sc->sc_cur_chan = IEEE80211_CHAN_ANY;
		atw_start_beacon(sc, 0);
		return (*sc->sc_newstate)(ic, nstate, arg);
	}

	if ((error = atw_tune(sc)) != 0)
		return error;

	switch (nstate) {
	case IEEE80211_S_ASSOC:
		break;
	case IEEE80211_S_INIT:
		panic("%s: unexpected state IEEE80211_S_INIT", __func__);
		break;
	case IEEE80211_S_SCAN:
		timeout_add_msec(&sc->sc_scan_to, atw_dwelltime);
		break;
	case IEEE80211_S_RUN:
		if (ic->ic_opmode == IEEE80211_M_STA)
			break;
		/*FALLTHROUGH*/
	case IEEE80211_S_AUTH:
		atw_write_bssid(sc);
		atw_write_ssid(sc);
		atw_write_sup_rates(sc);

		if (
#ifndef IEEE80211_STA_ONLY
		    ic->ic_opmode == IEEE80211_M_AHDEMO ||
#endif
		    ic->ic_opmode == IEEE80211_M_MONITOR)
			break;

		/* set listen interval
		 * XXX do software units agree w/ hardware?
		 */
		ATW_WRITE(sc, ATW_BPLI,
		    LSHIFT(ic->ic_bss->ni_intval, ATW_BPLI_BP_MASK) |
		    LSHIFT(ic->ic_lintval / ic->ic_bss->ni_intval,
			   ATW_BPLI_LI_MASK));

		DPRINTF(sc, ("%s: reg[ATW_BPLI] = %08x\n",
		    sc->sc_dev.dv_xname, ATW_READ(sc, ATW_BPLI)));

		atw_predict_beacon(sc);
		break;
	}

	if (nstate != IEEE80211_S_SCAN)
		timeout_del(&sc->sc_scan_to);

#ifndef IEEE80211_STA_ONLY
	if (nstate == IEEE80211_S_RUN &&
	    ic->ic_opmode == IEEE80211_M_IBSS)
		atw_start_beacon(sc, 1);
	else
#endif
		atw_start_beacon(sc, 0);

	error = (*sc->sc_newstate)(ic, nstate, arg);

	if (ostate == IEEE80211_S_INIT && nstate == IEEE80211_S_SCAN)
		atw_write_bssid(sc);

	return error;
}

/*
 * atw_add_rxbuf:
 *
 *	Add a receive buffer to the indicated descriptor.
 */
int
atw_add_rxbuf(struct atw_softc *sc, int idx)
{
	struct atw_rxsoft *rxs = &sc->sc_rxsoft[idx];
	struct mbuf *m;
	int error;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);

	MCLGET(m, M_DONTWAIT);
	if ((m->m_flags & M_EXT) == 0) {
		m_freem(m);
		return (ENOBUFS);
	}

	if (rxs->rxs_mbuf != NULL)
		bus_dmamap_unload(sc->sc_dmat, rxs->rxs_dmamap);

	rxs->rxs_mbuf = m;

	error = bus_dmamap_load(sc->sc_dmat, rxs->rxs_dmamap,
	    m->m_ext.ext_buf, m->m_ext.ext_size, NULL,
	    BUS_DMA_READ|BUS_DMA_NOWAIT);
	if (error) {
		printf("%s: can't load rx DMA map %d, error = %d\n",
		    sc->sc_dev.dv_xname, idx, error);
		panic("atw_add_rxbuf");	/* XXX */
	}

	bus_dmamap_sync(sc->sc_dmat, rxs->rxs_dmamap, 0,
	    rxs->rxs_dmamap->dm_mapsize, BUS_DMASYNC_PREREAD);

	ATW_INIT_RXDESC(sc, idx);

	return (0);
}

/*
 * Release any queued transmit buffers.
 */
void
atw_txdrain(struct atw_softc *sc)
{
	struct atw_txsoft *txs;

	while ((txs = SIMPLEQ_FIRST(&sc->sc_txdirtyq)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&sc->sc_txdirtyq, txs_q);
		if (txs->txs_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat, txs->txs_dmamap);
			m_freem(txs->txs_mbuf);
			txs->txs_mbuf = NULL;
		}
		SIMPLEQ_INSERT_TAIL(&sc->sc_txfreeq, txs, txs_q);
	}
	sc->sc_tx_timer = 0;
}

/*
 * atw_stop:		[ ifnet interface function ]
 *
 *	Stop transmission on the interface.
 */
void
atw_stop(struct ifnet *ifp, int disable)
{
	struct atw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);

	/*
	 * Mark the interface down and cancel the watchdog timer.
	*/
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	/* Disable interrupts. */
	ATW_WRITE(sc, ATW_IER, 0);

	/* Stop the transmit and receive processes. */
	sc->sc_opmode = 0;
	ATW_WRITE(sc, ATW_NAR, 0);
	DELAY(20 * 1000);
	ATW_WRITE(sc, ATW_TDBD, 0);
	ATW_WRITE(sc, ATW_TDBP, 0);
	ATW_WRITE(sc, ATW_RDB, 0);

	atw_txdrain(sc);

	if (disable) {
		atw_rxdrain(sc);
		atw_disable(sc);
	}

	if (!disable)
		atw_reset(sc);
}

/*
 * atw_rxdrain:
 *
 *	Drain the receive queue.
 */
void
atw_rxdrain(struct atw_softc *sc)
{
	struct atw_rxsoft *rxs;
	int i;

	for (i = 0; i < ATW_NRXDESC; i++) {
		rxs = &sc->sc_rxsoft[i];
		if (rxs->rxs_mbuf == NULL)
			continue;
		bus_dmamap_unload(sc->sc_dmat, rxs->rxs_dmamap);
		m_freem(rxs->rxs_mbuf);
		rxs->rxs_mbuf = NULL;
	}
}

/*
 * atw_detach:
 *
 *	Detach an ADM8211 interface.
 */
int
atw_detach(struct atw_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct atw_rxsoft *rxs;
	struct atw_txsoft *txs;
	int i;

	/*
	 * Succeed now if there isn't any work to do.
	 */
	if ((sc->sc_flags & ATWF_ATTACHED) == 0)
		return (0);

	timeout_del(&sc->sc_scan_to);

	ieee80211_ifdetach(ifp);
	if_detach(ifp);

	for (i = 0; i < ATW_NRXDESC; i++) {
		rxs = &sc->sc_rxsoft[i];
		if (rxs->rxs_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat, rxs->rxs_dmamap);
			m_freem(rxs->rxs_mbuf);
			rxs->rxs_mbuf = NULL;
		}
		bus_dmamap_destroy(sc->sc_dmat, rxs->rxs_dmamap);
	}
	for (i = 0; i < ATW_TXQUEUELEN; i++) {
		txs = &sc->sc_txsoft[i];
		if (txs->txs_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat, txs->txs_dmamap);
			m_freem(txs->txs_mbuf);
			txs->txs_mbuf = NULL;
		}
		bus_dmamap_destroy(sc->sc_dmat, txs->txs_dmamap);
	}
	bus_dmamap_unload(sc->sc_dmat, sc->sc_cddmamap);
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_cddmamap);
	bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->sc_control_data,
	    sizeof(struct atw_control_data));
	bus_dmamem_free(sc->sc_dmat, &sc->sc_cdseg, sc->sc_cdnseg);

	if (sc->sc_srom)
		free(sc->sc_srom, M_DEVBUF, sc->sc_sromsz);

	return (0);
}

int
atw_intr(void *arg)
{
	struct atw_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	u_int32_t status, rxstatus, txstatus, linkstatus;
	int handled = 0, txthresh;

#ifdef DEBUG
	if (ATW_IS_ENABLED(sc) == 0)
		panic("%s: atw_intr: not enabled", sc->sc_dev.dv_xname);
#endif

	/*
	 * If the interface isn't running, the interrupt couldn't
	 * possibly have come from us.
	 */
	if ((ifp->if_flags & IFF_RUNNING) == 0 ||
	    (sc->sc_dev.dv_flags & DVF_ACTIVE) == 0)
		return (0);

	for (;;) {
		status = ATW_READ(sc, ATW_STSR);

		if (status)
			ATW_WRITE(sc, ATW_STSR, status);

#ifdef ATW_DEBUG
#define PRINTINTR(flag) do { \
	if ((status & flag) != 0) { \
		printf("%s" #flag, delim); \
		delim = ","; \
	} \
} while (0)

		if (atw_debug > 1 && status) {
			const char *delim = "<";

			printf("%s: reg[STSR] = %x",
			    sc->sc_dev.dv_xname, status);

			PRINTINTR(ATW_INTR_FBE);
			PRINTINTR(ATW_INTR_LINKOFF);
			PRINTINTR(ATW_INTR_LINKON);
			PRINTINTR(ATW_INTR_RCI);
			PRINTINTR(ATW_INTR_RDU);
			PRINTINTR(ATW_INTR_REIS);
			PRINTINTR(ATW_INTR_RPS);
			PRINTINTR(ATW_INTR_TCI);
			PRINTINTR(ATW_INTR_TDU);
			PRINTINTR(ATW_INTR_TLT);
			PRINTINTR(ATW_INTR_TPS);
			PRINTINTR(ATW_INTR_TRT);
			PRINTINTR(ATW_INTR_TUF);
			PRINTINTR(ATW_INTR_BCNTC);
			PRINTINTR(ATW_INTR_ATIME);
			PRINTINTR(ATW_INTR_TBTT);
			PRINTINTR(ATW_INTR_TSCZ);
			PRINTINTR(ATW_INTR_TSFTF);
			printf(">\n");
		}
#undef PRINTINTR
#endif /* ATW_DEBUG */

		if ((status & sc->sc_inten) == 0)
			break;

		handled = 1;

		rxstatus = status & sc->sc_rxint_mask;
		txstatus = status & sc->sc_txint_mask;
		linkstatus = status & sc->sc_linkint_mask;

		if (linkstatus) {
			atw_linkintr(sc, linkstatus);
		}

		if (rxstatus) {
			/* Grab any new packets. */
			atw_rxintr(sc);

			if (rxstatus & ATW_INTR_RDU) {
				printf("%s: receive ring overrun\n",
				    sc->sc_dev.dv_xname);
				/* Get the receive process going again. */
				ATW_WRITE(sc, ATW_RDR, 0x1);
				break;
			}
		}

		if (txstatus) {
			/* Sweep up transmit descriptors. */
			atw_txintr(sc);

			if (txstatus & ATW_INTR_TLT)
				DPRINTF(sc, ("%s: tx lifetime exceeded\n",
				    sc->sc_dev.dv_xname));

			if (txstatus & ATW_INTR_TRT)
				DPRINTF(sc, ("%s: tx retry limit exceeded\n",
				    sc->sc_dev.dv_xname));

			/* If Tx under-run, increase our transmit threshold
			 * if another is available.
			 */
			txthresh = sc->sc_txthresh + 1;
			if ((txstatus & ATW_INTR_TUF) &&
			    sc->sc_txth[txthresh].txth_name != NULL) {
				/* Idle the transmit process. */
				atw_idle(sc, ATW_NAR_ST);

				sc->sc_txthresh = txthresh;
				sc->sc_opmode &= ~(ATW_NAR_TR_MASK|ATW_NAR_SF);
				sc->sc_opmode |=
				    sc->sc_txth[txthresh].txth_opmode;
				printf("%s: transmit underrun; new "
				    "threshold: %s\n", sc->sc_dev.dv_xname,
				    sc->sc_txth[txthresh].txth_name);

				/* Set the new threshold and restart
				 * the transmit process.
				 */
				ATW_WRITE(sc, ATW_NAR, sc->sc_opmode);
				DELAY(20 * 1000);
				ATW_WRITE(sc, ATW_RDR, 0x1);
				/* XXX Log every Nth underrun from
				 * XXX now on?
				 */
			}
		}

		if (status & (ATW_INTR_TPS|ATW_INTR_RPS)) {
			if (status & ATW_INTR_TPS)
				printf("%s: transmit process stopped\n",
				    sc->sc_dev.dv_xname);
			if (status & ATW_INTR_RPS)
				printf("%s: receive process stopped\n",
				    sc->sc_dev.dv_xname);
			(void)atw_init(ifp);
			break;
		}

		if (status & ATW_INTR_FBE) {
			printf("%s: fatal bus error\n", sc->sc_dev.dv_xname);
			(void)atw_init(ifp);
			break;
		}

		/*
		 * Not handled:
		 *
		 *	Transmit buffer unavailable -- normal
		 *	condition, nothing to do, really.
		 *
		 *	Early receive interrupt -- not available on
		 *	all chips, we just use RI.  We also only
		 *	use single-segment receive DMA, so this
		 *	is mostly useless.
		 *
		 *      TBD others
		 */
	}

	/* Try to get more packets going. */
	atw_start(ifp);

	return (handled);
}

/*
 * atw_idle:
 *
 *	Cause the transmit and/or receive processes to go idle.
 *
 *      XXX It seems that the ADM8211 will not signal the end of the Rx/Tx
 *	process in STSR if I clear SR or ST after the process has already
 *	ceased. Fair enough. But the Rx process status bits in ATW_TEST0
 *      do not seem to be too reliable. Perhaps I have the sense of the
 *	Rx bits switched with the Tx bits?
 */
void
atw_idle(struct atw_softc *sc, u_int32_t bits)
{
	u_int32_t ackmask = 0, opmode, stsr, test0;
	int i, s;

	s = splnet();

	opmode = sc->sc_opmode & ~bits;

	if (bits & ATW_NAR_SR)
		ackmask |= ATW_INTR_RPS;

	if (bits & ATW_NAR_ST) {
		ackmask |= ATW_INTR_TPS;
		/* set ATW_NAR_HF to flush TX FIFO. */
		opmode |= ATW_NAR_HF;
	}

	ATW_WRITE(sc, ATW_NAR, opmode);
	DELAY(20 * 1000);

	for (i = 0; i < 10; i++) {
		stsr = ATW_READ(sc, ATW_STSR);
		if ((stsr & ackmask) == ackmask)
			break;
		DELAY(1000);
	}

	ATW_WRITE(sc, ATW_STSR, stsr & ackmask);

	if ((stsr & ackmask) == ackmask)
		goto out;

	test0 = ATW_READ(sc, ATW_TEST0);

	if ((bits & ATW_NAR_ST) != 0 && (stsr & ATW_INTR_TPS) == 0 &&
	    (test0 & ATW_TEST0_TS_MASK) != ATW_TEST0_TS_STOPPED) {
		DPRINTF2(sc, ("%s: transmit process not idle [%s]\n",
		    sc->sc_dev.dv_xname,
		    atw_tx_state[MASK_AND_RSHIFT(test0, ATW_TEST0_TS_MASK)]));
		DPRINTF2(sc, ("%s: bits %08x test0 %08x stsr %08x\n",
		    sc->sc_dev.dv_xname, bits, test0, stsr));
	}

	if ((bits & ATW_NAR_SR) != 0 && (stsr & ATW_INTR_RPS) == 0 &&
	    (test0 & ATW_TEST0_RS_MASK) != ATW_TEST0_RS_STOPPED) {
		DPRINTF2(sc, ("%s: receive process not idle [%s]\n",
		    sc->sc_dev.dv_xname,
		    atw_rx_state[MASK_AND_RSHIFT(test0, ATW_TEST0_RS_MASK)]));
		DPRINTF2(sc, ("%s: bits %08x test0 %08x stsr %08x\n",
		    sc->sc_dev.dv_xname, bits, test0, stsr));
	}
out:
	if ((bits & ATW_NAR_ST) != 0)
		atw_txdrain(sc);
	splx(s);
	return;
}

/*
 * atw_linkintr:
 *
 *	Helper; handle link-status interrupts.
 */
void
atw_linkintr(struct atw_softc *sc, u_int32_t linkstatus)
{
	struct ieee80211com *ic = &sc->sc_ic;

	if (ic->ic_state != IEEE80211_S_RUN)
		return;

	if (linkstatus & ATW_INTR_LINKON) {
		DPRINTF(sc, ("%s: link on\n", sc->sc_dev.dv_xname));
		sc->sc_rescan_timer = 0;
	} else if (linkstatus & ATW_INTR_LINKOFF) {
		DPRINTF(sc, ("%s: link off\n", sc->sc_dev.dv_xname));
		if (ic->ic_opmode != IEEE80211_M_STA)
			return;
		sc->sc_rescan_timer = 3;
		ic->ic_if.if_timer = 1;
	}
}

#if 0
static __inline int
atw_hw_decrypted(struct atw_softc *sc, struct ieee80211_frame *wh)
{
	if ((sc->sc_ic.ic_flags & IEEE80211_F_WEPON) == 0)
		return 0;
	if ((wh->i_fc[1] & IEEE80211_FC1_WEP) == 0)
		return 0;
	return (sc->sc_wepctl & ATW_WEPCTL_WEPRXBYP) == 0;
}
#endif

/*
 * atw_rxintr:
 *
 *	Helper; handle receive interrupts.
 */
void
atw_rxintr(struct atw_softc *sc)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	static int rate_tbl[] = {2, 4, 11, 22, 44};
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_node *ni;
	struct ieee80211_frame *wh;
	struct ifnet *ifp = &ic->ic_if;
	struct atw_rxsoft *rxs;
	struct mbuf *m;
	u_int32_t rxstat;
	int i, len, rate, rate0;
	u_int32_t rssi, rssi0;

	for (i = sc->sc_rxptr;; i = ATW_NEXTRX(i)) {
		rxs = &sc->sc_rxsoft[i];

		ATW_CDRXSYNC(sc, i, BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

		rxstat = letoh32(sc->sc_rxdescs[i].ar_stat);
		rssi0 = letoh32(sc->sc_rxdescs[i].ar_rssi);
		rate0 = MASK_AND_RSHIFT(rxstat, ATW_RXSTAT_RXDR_MASK);

		if (rxstat & ATW_RXSTAT_OWN)
			break; /* We have processed all receive buffers. */

		DPRINTF3(sc,
		    ("%s: rx stat %08x rssi0 %08x buf1 %08x buf2 %08x\n",
		    sc->sc_dev.dv_xname,
		    rxstat, rssi0,
		    letoh32(sc->sc_rxdescs[i].ar_buf1),
		    letoh32(sc->sc_rxdescs[i].ar_buf2)));

		/*
		 * Make sure the packet fits in one buffer.  This should
		 * always be the case.
		 */
		if ((rxstat & (ATW_RXSTAT_FS|ATW_RXSTAT_LS)) !=
		    (ATW_RXSTAT_FS|ATW_RXSTAT_LS)) {
			printf("%s: incoming packet spilled, resetting\n",
			    sc->sc_dev.dv_xname);
			(void)atw_init(ifp);
			return;
		}

		/*
		 * If an error occurred, update stats, clear the status
		 * word, and leave the packet buffer in place.  It will
		 * simply be reused the next time the ring comes around.
	 	 * If 802.1Q VLAN MTU is enabled, ignore the Frame Too Long
		 * error.
		 */

		if ((rxstat & ATW_RXSTAT_ES) != 0 &&
		    ((sc->sc_ic.ic_if.if_capabilities & IFCAP_VLAN_MTU) == 0 ||
		     (rxstat & (ATW_RXSTAT_DE | ATW_RXSTAT_SFDE |
		                ATW_RXSTAT_SIGE | ATW_RXSTAT_CRC16E |
				ATW_RXSTAT_RXTOE | ATW_RXSTAT_CRC32E |
				ATW_RXSTAT_ICVE)) != 0)) {
#define	PRINTERR(bit, str)						\
			if (rxstat & (bit))				\
				printf("%s: receive error: %s\n",	\
				    sc->sc_dev.dv_xname, str)
			ifp->if_ierrors++;
			PRINTERR(ATW_RXSTAT_DE, "descriptor error");
			PRINTERR(ATW_RXSTAT_SFDE, "PLCP SFD error");
			PRINTERR(ATW_RXSTAT_SIGE, "PLCP signal error");
			PRINTERR(ATW_RXSTAT_CRC16E, "PLCP CRC16 error");
			PRINTERR(ATW_RXSTAT_RXTOE, "time-out");
			PRINTERR(ATW_RXSTAT_CRC32E, "FCS error");
			PRINTERR(ATW_RXSTAT_ICVE, "WEP ICV error");
#undef PRINTERR
			ATW_INIT_RXDESC(sc, i);
			continue;
		}

		bus_dmamap_sync(sc->sc_dmat, rxs->rxs_dmamap, 0,
		    rxs->rxs_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);

		/*
		 * No errors; receive the packet.  Note the ADM8211
		 * includes the CRC in promiscuous mode.
		 */
		len = MASK_AND_RSHIFT(rxstat, ATW_RXSTAT_FL_MASK);

		/*
		 * Allocate a new mbuf cluster.  If that fails, we are
		 * out of memory, and must drop the packet and recycle
		 * the buffer that's already attached to this descriptor.
		 */
		m = rxs->rxs_mbuf;
		if (atw_add_rxbuf(sc, i) != 0) {
			ifp->if_ierrors++;
			ATW_INIT_RXDESC(sc, i);
			bus_dmamap_sync(sc->sc_dmat, rxs->rxs_dmamap, 0,
			    rxs->rxs_dmamap->dm_mapsize, BUS_DMASYNC_PREREAD);
			continue;
		}

		if (sc->sc_opmode & ATW_NAR_PR)
			len -= IEEE80211_CRC_LEN;
		m->m_pkthdr.len = m->m_len = MIN(m->m_ext.ext_size, len);

		if (rate0 >= sizeof(rate_tbl) / sizeof(rate_tbl[0]))
			rate = 0;
		else
			rate = rate_tbl[rate0];

		/* The RSSI comes straight from a register in the
		 * baseband processor.  I know that for the RF3000,
		 * the RSSI register also contains the antenna-selection
		 * bits.  Mask those off.
		 *
		 * TBD Treat other basebands.
		 */
		if (sc->sc_bbptype == ATW_BBPTYPE_RFMD)
			rssi = rssi0 & RF3000_RSSI_MASK;
		else
			rssi = rssi0;

#if NBPFILTER > 0
		/* Pass this up to any BPF listeners. */
		if (sc->sc_radiobpf != NULL) {
			struct mbuf mb;

			struct atw_rx_radiotap_header *tap = &sc->sc_rxtap;

			tap->ar_rate = rate;
			tap->ar_chan_freq = ic->ic_bss->ni_chan->ic_freq;
			tap->ar_chan_flags = ic->ic_bss->ni_chan->ic_flags;

			/* TBD verify units are dB */
			tap->ar_antsignal = (int)rssi;
			/* TBD tap->ar_flags */

			mb.m_data = (caddr_t)tap;
			mb.m_len = tap->ar_ihdr.it_len;
			mb.m_next = m;
			mb.m_nextpkt = NULL;
			mb.m_type = 0;
			mb.m_flags = 0;
			bpf_mtap(sc->sc_radiobpf, &mb, BPF_DIRECTION_IN);
 		}
#endif /* NBPFILTER > 0 */

		wh = mtod(m, struct ieee80211_frame *);
		ni = ieee80211_find_rxnode(ic, wh);
		memset(&rxi, 0, sizeof(rxi));
#if 0
		if (atw_hw_decrypted(sc, wh)) {
			wh->i_fc[1] &= ~IEEE80211_FC1_WEP;
			rxi.rxi_flags |= IEEE80211_RXI_HWDEC;
		}
#endif
		rxi.rxi_rssi = (int)rssi;
		ieee80211_inputm(ifp, m, ni, &rxi, &ml);
		/*
		 * The frame may have caused the node to be marked for
		 * reclamation (e.g. in response to a DEAUTH message)
		 * so use release_node here instead of unref_node.
		 */
		ieee80211_release_node(ic, ni);
	}
	if_input(ifp, &ml);

	/* Update the receive pointer. */
	sc->sc_rxptr = i;
}

/*
 * atw_txintr:
 *
 *	Helper; handle transmit interrupts.
 */
void
atw_txintr(struct atw_softc *sc)
{
#define TXSTAT_ERRMASK (ATW_TXSTAT_TUF | ATW_TXSTAT_TLT | ATW_TXSTAT_TRT | \
    ATW_TXSTAT_TRO | ATW_TXSTAT_SOFBR)
#define TXSTAT_FMT "\20\31ATW_TXSTAT_SOFBR\32ATW_TXSTAT_TRO\33ATW_TXSTAT_TUF" \
    "\34ATW_TXSTAT_TRT\35ATW_TXSTAT_TLT"
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct atw_txsoft *txs;
	u_int32_t txstat;

	DPRINTF3(sc, ("%s: atw_txintr: sc_flags 0x%08x\n",
	    sc->sc_dev.dv_xname, sc->sc_flags));

	ifq_clr_oactive(&ifp->if_snd);

	/*
	 * Go through our Tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	while ((txs = SIMPLEQ_FIRST(&sc->sc_txdirtyq)) != NULL) {
		ATW_CDTXSYNC(sc, txs->txs_lastdesc, 1,
		    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

#ifdef ATW_DEBUG
		if ((ifp->if_flags & IFF_DEBUG) != 0 && atw_debug > 2) {
			int i;
			printf("    txsoft %p transmit chain:\n", txs);
			ATW_CDTXSYNC(sc, txs->txs_firstdesc,
			    txs->txs_ndescs - 1,
			    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
			for (i = txs->txs_firstdesc;; i = ATW_NEXTTX(i)) {
				printf("     descriptor %d:\n", i);
				printf("       at_status:   0x%08x\n",
				    letoh32(sc->sc_txdescs[i].at_stat));
				printf("       at_flags:      0x%08x\n",
				    letoh32(sc->sc_txdescs[i].at_flags));
				printf("       at_buf1: 0x%08x\n",
				    letoh32(sc->sc_txdescs[i].at_buf1));
				printf("       at_buf2: 0x%08x\n",
				    letoh32(sc->sc_txdescs[i].at_buf2));
				if (i == txs->txs_lastdesc)
					break;
			}
		}
#endif

		txstat = letoh32(sc->sc_txdescs[txs->txs_lastdesc].at_stat);
		if (txstat & ATW_TXSTAT_OWN)
			break;

		SIMPLEQ_REMOVE_HEAD(&sc->sc_txdirtyq, txs_q);

		sc->sc_txfree += txs->txs_ndescs;

		bus_dmamap_sync(sc->sc_dmat, txs->txs_dmamap,
		    0, txs->txs_dmamap->dm_mapsize,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, txs->txs_dmamap);
		m_freem(txs->txs_mbuf);
		txs->txs_mbuf = NULL;

		SIMPLEQ_INSERT_TAIL(&sc->sc_txfreeq, txs, txs_q);

		if ((ifp->if_flags & IFF_DEBUG) != 0 &&
		    (txstat & TXSTAT_ERRMASK) != 0) {
			printf("%s: txstat %b %d\n", sc->sc_dev.dv_xname,
			    txstat & TXSTAT_ERRMASK, TXSTAT_FMT,
			    MASK_AND_RSHIFT(txstat, ATW_TXSTAT_ARC_MASK));
		}

		/*
		 * Check for errors and collisions.
		 */
		if (txstat & ATW_TXSTAT_TUF)
			sc->sc_stats.ts_tx_tuf++;
		if (txstat & ATW_TXSTAT_TLT)
			sc->sc_stats.ts_tx_tlt++;
		if (txstat & ATW_TXSTAT_TRT)
			sc->sc_stats.ts_tx_trt++;
		if (txstat & ATW_TXSTAT_TRO)
			sc->sc_stats.ts_tx_tro++;
		if (txstat & ATW_TXSTAT_SOFBR) {
			sc->sc_stats.ts_tx_sofbr++;
		}

		if ((txstat & ATW_TXSTAT_ES) == 0)
			ifp->if_collisions +=
			    MASK_AND_RSHIFT(txstat, ATW_TXSTAT_ARC_MASK);
		else
			ifp->if_oerrors++;
	}

	/*
	 * If there are no more pending transmissions, cancel the watchdog
	 * timer.
	 */
	if (txs == NULL)
		sc->sc_tx_timer = 0;
#undef TXSTAT_ERRMASK
#undef TXSTAT_FMT
}

/*
 * atw_watchdog:	[ifnet interface function]
 *
 *	Watchdog timer handler.
 */
void
atw_watchdog(struct ifnet *ifp)
{
	struct atw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t test1, rra, rwa;

	ifp->if_timer = 0;
	if (ATW_IS_ENABLED(sc) == 0)
		return;

	if (sc->sc_rescan_timer) {
		if (--sc->sc_rescan_timer == 0)
			(void)ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	}
	if (sc->sc_tx_timer) {
		if (--sc->sc_tx_timer == 0 &&
		    !SIMPLEQ_EMPTY(&sc->sc_txdirtyq)) {
			printf("%s: transmit timeout\n", ifp->if_xname);
			ifp->if_oerrors++;
			(void)atw_init(ifp);
			atw_start(ifp);
		}
	}
	if (sc->sc_tx_timer != 0 || sc->sc_rescan_timer != 0)
		ifp->if_timer = 1;

	/*
	 * ADM8211B seems to stall every so often, check for this.
	 * These bits are what the Linux driver checks, they don't
	 * seem to be documented by ADMTek/Infineon?
	 */
	if (sc->sc_rev == ATW_REVISION_BA) {
		test1 = ATW_READ(sc, ATW_TEST1);
		rra = (test1 >> 12) & 0x1ff;
		rwa = (test1 >> 2) & 0x1ff;

		if ((rra != rwa) && !(test1 & 0x2)) {
			atw_init(ifp);
			atw_start(ifp);
		}
	}

	ieee80211_watchdog(ifp);
}

/*
 * Arguments in:
 *
 * paylen:  payload length (no FCS, no WEP header)
 *
 * hdrlen:  header length
 *
 * rate:    MSDU speed, units 500kb/s
 *
 * flags:   IEEE80211_F_SHPREAMBLE (use short preamble),
 *          IEEE80211_F_SHSLOT (use short slot length)
 *
 * Arguments out:
 *
 * d:       802.11 Duration field for RTS,
 *          802.11 Duration field for data frame,
 *          PLCP Length for data frame,
 *          residual octets at end of data slot
 */
int
atw_compute_duration1(int len, int use_ack, uint32_t flags, int rate,
    struct atw_duration *d)
{
	int pre, ctsrate;
	int ack, bitlen, data_dur, remainder;

	/* RTS reserves medium for SIFS | CTS | SIFS | (DATA) | SIFS | ACK
	 * DATA reserves medium for SIFS | ACK
	 *
	 * XXXMYC: no ACK on multicast/broadcast or control packets
	 */

	bitlen = len * 8;

	pre = IEEE80211_DUR_DS_SIFS;
	if ((flags & IEEE80211_F_SHPREAMBLE) != 0)
		pre += IEEE80211_DUR_DS_SHORT_PREAMBLE +
		    IEEE80211_DUR_DS_FAST_PLCPHDR;
	else
		pre += IEEE80211_DUR_DS_LONG_PREAMBLE +
		    IEEE80211_DUR_DS_SLOW_PLCPHDR;

	d->d_residue = 0;
	data_dur = (bitlen * 2) / rate;
	remainder = (bitlen * 2) % rate;
	if (remainder != 0) {
		d->d_residue = (rate - remainder) / 16;
		data_dur++;
	}

	switch (rate) {
	case 2:		/* 1 Mb/s */
	case 4:		/* 2 Mb/s */
		/* 1 - 2 Mb/s WLAN: send ACK/CTS at 1 Mb/s */
		ctsrate = 2;
		break;
	case 11:	/* 5.5 Mb/s */
	case 22:	/* 11  Mb/s */
	case 44:	/* 22  Mb/s */
		/* 5.5 - 11 Mb/s WLAN: send ACK/CTS at 2 Mb/s */
		ctsrate = 4;
		break;
	default:
		/* TBD */
		return -1;
	}

	d->d_plcp_len = data_dur;

	ack = (use_ack) ? pre + (IEEE80211_DUR_DS_SLOW_ACK * 2) / ctsrate : 0;

	d->d_rts_dur =
	    pre + (IEEE80211_DUR_DS_SLOW_CTS * 2) / ctsrate +
	    pre + data_dur +
	    ack;

	d->d_data_dur = ack;

	return 0;
}

/*
 * Arguments in:
 *
 * wh:      802.11 header
 *
 * len: packet length 
 *
 * rate:    MSDU speed, units 500kb/s
 *
 * fraglen: fragment length, set to maximum (or higher) for no
 *          fragmentation
 *
 * flags:   IEEE80211_F_WEPON (hardware adds WEP),
 *          IEEE80211_F_SHPREAMBLE (use short preamble),
 *          IEEE80211_F_SHSLOT (use short slot length)
 *
 * Arguments out:
 *
 * d0: 802.11 Duration fields (RTS/Data), PLCP Length, Service fields
 *     of first/only fragment
 *
 * dn: 802.11 Duration fields (RTS/Data), PLCP Length, Service fields
 *     of first/only fragment
 */
int
atw_compute_duration(struct ieee80211_frame *wh, int len, uint32_t flags,
    int fraglen, int rate, struct atw_duration *d0, struct atw_duration *dn,
    int *npktp, int debug)
{
	int ack, rc;
	int firstlen, hdrlen, lastlen, lastlen0, npkt, overlen, paylen;

	if (ieee80211_has_addr4(wh))
		hdrlen = sizeof(struct ieee80211_frame_addr4);
	else
		hdrlen = sizeof(struct ieee80211_frame);

	paylen = len - hdrlen;

	if ((flags & IEEE80211_F_WEPON) != 0)
		overlen = IEEE80211_WEP_TOTLEN + IEEE80211_CRC_LEN;
	else
		overlen = IEEE80211_CRC_LEN;

	npkt = paylen / fraglen;
	lastlen0 = paylen % fraglen;

	if (npkt == 0)			/* no fragments */
		lastlen = paylen + overlen;
	else if (lastlen0 != 0) {	/* a short "tail" fragment */
		lastlen = lastlen0 + overlen;
		npkt++;
	} else				/* full-length "tail" fragment */
		lastlen = fraglen + overlen;

	if (npktp != NULL)
		*npktp = npkt;

	if (npkt > 1)
		firstlen = fraglen + overlen;
	else
		firstlen = paylen + overlen;

	if (debug) {
		printf("%s: npkt %d firstlen %d lastlen0 %d lastlen %d "
		    "fraglen %d overlen %d len %d rate %d flags %08x\n",
		    __func__, npkt, firstlen, lastlen0, lastlen, fraglen,
		    overlen, len, rate, flags);
	}

	ack = !IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    (wh->i_fc[1] & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_CTL;

	rc = atw_compute_duration1(firstlen + hdrlen, ack, flags, rate, d0);
	if (rc == -1)
		return rc;

	if (npkt <= 1) {
		*dn = *d0;
		return 0;
	}
	return atw_compute_duration1(lastlen + hdrlen, ack, flags, rate, dn);
}

#ifdef ATW_DEBUG
void
atw_dump_pkt(struct ifnet *ifp, struct mbuf *m0)
{
	struct atw_softc *sc = ifp->if_softc;
	struct mbuf *m;
	int i, noctets = 0;

	printf("%s: %d-byte packet\n", sc->sc_dev.dv_xname,
	    m0->m_pkthdr.len);

	for (m = m0; m; m = m->m_next) {
		if (m->m_len == 0)
			continue;
		for (i = 0; i < m->m_len; i++) {
			printf(" %02x", ((u_int8_t*)m->m_data)[i]);
			if (++noctets % 24 == 0)
				printf("\n");
		}
	}
	printf("%s%s: %d bytes emitted\n",
	    (noctets % 24 != 0) ? "\n" : "", sc->sc_dev.dv_xname, noctets);
}
#endif /* ATW_DEBUG */

/*
 * atw_start:		[ifnet interface function]
 *
 *	Start packet transmission on the interface.
 */
void
atw_start(struct ifnet *ifp)
{
	struct atw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	struct atw_frame *hh;
	struct mbuf *m0, *m;
	struct atw_txsoft *txs, *last_txs;
	struct atw_txdesc *txd;
	int do_encrypt, npkt, rate;
	bus_dmamap_t dmamap;
	int ctl, error, firsttx, nexttx, lasttx = -1, first, ofree, seg;

	DPRINTF2(sc, ("%s: atw_start: sc_flags 0x%08x, if_flags 0x%08x\n",
	    sc->sc_dev.dv_xname, sc->sc_flags, ifp->if_flags));

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	/*
	 * Remember the previous number of free descriptors and
	 * the first descriptor we'll use.
	 */
	ofree = sc->sc_txfree;
	firsttx = sc->sc_txnext;

	DPRINTF2(sc, ("%s: atw_start: txfree %d, txnext %d\n",
	    sc->sc_dev.dv_xname, ofree, firsttx));

	/*
	 * Loop through the send queue, setting up transmit descriptors
	 * until we drain the queue, or use up all available transmit
	 * descriptors.
	 */
	while ((txs = SIMPLEQ_FIRST(&sc->sc_txfreeq)) != NULL &&
	       sc->sc_txfree != 0) {

		/*
		 * Grab a packet off the management queue, if it
		 * is not empty. Otherwise, from the data queue.
		 */
		m0 = mq_dequeue(&ic->ic_mgtq);
		if (m0 != NULL) {
			ni = m0->m_pkthdr.ph_cookie;
		} else {
			/* send no data packets until we are associated */
			if (ic->ic_state != IEEE80211_S_RUN)
				break;
			m0 = ifq_dequeue(&ifp->if_snd);
			if (m0 == NULL)
				break;
#if NBPFILTER > 0
			if (ifp->if_bpf != NULL)
				bpf_mtap(ifp->if_bpf, m0, BPF_DIRECTION_OUT);
#endif /* NBPFILTER > 0 */
			if ((m0 = ieee80211_encap(ifp, m0, &ni)) == NULL) {
				ifp->if_oerrors++;
				break;
			}

			if (ic->ic_flags & IEEE80211_F_WEPON) {
				wh = mtod(m0, struct ieee80211_frame *);
				k = ieee80211_get_txkey(ic, wh, ni);
				m0 = ieee80211_encrypt(ic, m0, k);
				if (m0 == NULL) {
					ifp->if_oerrors++;
					break;	
				}
			}
		}

		wh = mtod(m0, struct ieee80211_frame *);

		/* XXX do real rate control */
		if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
		    IEEE80211_FC0_TYPE_MGT)
			rate = 2;
		else
			rate = MAX(2, ieee80211_get_rate(ic));

		if (atw_compute_duration(wh, m0->m_pkthdr.len,
		    ic->ic_flags & ~IEEE80211_F_WEPON, ic->ic_fragthreshold,
		    rate, &txs->txs_d0, &txs->txs_dn, &npkt,
		    (sc->sc_if.if_flags & (IFF_DEBUG|IFF_LINK2)) ==
		    (IFF_DEBUG|IFF_LINK2)) == -1) {
			DPRINTF2(sc, ("%s: fail compute duration\n", __func__));
			m_freem(m0);
			break;
		}

		/*
		 * XXX Misleading if fragmentation is enabled.  Better
		 * to fragment in software?
		 */
		*(uint16_t *)wh->i_dur = htole16(txs->txs_d0.d_rts_dur);

#if NBPFILTER > 0
		/*
		 * Pass the packet to any BPF listeners.
		 */
		if (ic->ic_rawbpf != NULL)
			bpf_mtap(ic->ic_rawbpf, m0, BPF_DIRECTION_OUT);

		if (sc->sc_radiobpf != NULL) {
			struct mbuf mb;
			struct atw_tx_radiotap_header *tap = &sc->sc_txtap;

			tap->at_rate = rate;
			tap->at_chan_freq = ic->ic_bss->ni_chan->ic_freq;
			tap->at_chan_flags = ic->ic_bss->ni_chan->ic_flags;

			/* TBD tap->at_flags */

			mb.m_data = (caddr_t)tap;
			mb.m_len = tap->at_ihdr.it_len;
			mb.m_next = m0;
			mb.m_nextpkt = NULL;
			mb.m_type = 0;
			mb.m_flags = 0;
			bpf_mtap(sc->sc_radiobpf, &mb, BPF_DIRECTION_OUT);
		}
#endif /* NBPFILTER > 0 */

		M_PREPEND(m0, offsetof(struct atw_frame, atw_ihdr), M_DONTWAIT);

		if (ni != NULL)
			ieee80211_release_node(ic, ni);

		if (m0 == NULL) {
			ifp->if_oerrors++;
			break;
		}

		/* just to make sure. */
		m0 = m_pullup(m0, sizeof(struct atw_frame));

		if (m0 == NULL) {
			ifp->if_oerrors++;
			break;
		}

		hh = mtod(m0, struct atw_frame *);
		wh = &hh->atw_ihdr;

		do_encrypt = ((wh->i_fc[1] & IEEE80211_FC1_WEP) != 0) ? 1 : 0;

		/* Copy everything we need from the 802.11 header:
		 * Frame Control; address 1, address 3, or addresses
		 * 3 and 4. NIC fills in BSSID, SA.
		 */
		if (wh->i_fc[1] & IEEE80211_FC1_DIR_TODS) {
			if (wh->i_fc[1] & IEEE80211_FC1_DIR_FROMDS)
				panic("%s: illegal WDS frame",
				    sc->sc_dev.dv_xname);
			memcpy(hh->atw_dst, wh->i_addr3, IEEE80211_ADDR_LEN);
		} else
			memcpy(hh->atw_dst, wh->i_addr1, IEEE80211_ADDR_LEN);

		*(u_int16_t*)hh->atw_fc = *(u_int16_t*)wh->i_fc;

		/* initialize remaining Tx parameters */
		memset(&hh->u, 0, sizeof(hh->u));

		hh->atw_rate = rate * 5;
		/* XXX this could be incorrect if M_FCS. _encap should
		 * probably strip FCS just in case it sticks around in
		 * bridged packets.
		 */
		hh->atw_service = IEEE80211_PLCP_SERVICE; /* XXX guess */
		hh->atw_paylen = htole16(m0->m_pkthdr.len -
		    sizeof(struct atw_frame));

		hh->atw_fragthr = htole16(ATW_FRAGTHR_FRAGTHR_MASK);
		hh->atw_rtylmt = 3;
		hh->atw_hdrctl = htole16(ATW_HDRCTL_UNKNOWN1);
#if 0
		if (do_encrypt) {
			hh->atw_hdrctl |= htole16(ATW_HDRCTL_WEP);
			hh->atw_keyid = ic->ic_wep_txkey;
		}
#endif

		hh->atw_head_plcplen = htole16(txs->txs_d0.d_plcp_len);
		hh->atw_tail_plcplen = htole16(txs->txs_dn.d_plcp_len);
		if (txs->txs_d0.d_residue)
			hh->atw_head_plcplen |= htole16(0x8000);
		if (txs->txs_dn.d_residue)
			hh->atw_tail_plcplen |= htole16(0x8000);
		hh->atw_head_dur = htole16(txs->txs_d0.d_rts_dur);
		hh->atw_tail_dur = htole16(txs->txs_dn.d_rts_dur);

		/* never fragment multicast frames */
		if (IEEE80211_IS_MULTICAST(hh->atw_dst)) {
			hh->atw_fragthr = htole16(ATW_FRAGTHR_FRAGTHR_MASK);
		} else if (sc->sc_flags & ATWF_RTSCTS) {
			hh->atw_hdrctl |= htole16(ATW_HDRCTL_RTSCTS);
		}

#ifdef ATW_DEBUG
		hh->atw_fragnum = 0;

		if ((ifp->if_flags & IFF_DEBUG) != 0 && atw_debug > 2) {
			printf("%s: dst = %s, rate = 0x%02x, "
			    "service = 0x%02x, paylen = 0x%04x\n",
			    sc->sc_dev.dv_xname, ether_sprintf(hh->atw_dst),
			    hh->atw_rate, hh->atw_service, hh->atw_paylen);

			printf("%s: fc[0] = 0x%02x, fc[1] = 0x%02x, "
			    "dur1 = 0x%04x, dur2 = 0x%04x, "
			    "dur3 = 0x%04x, rts_dur = 0x%04x\n",
			    sc->sc_dev.dv_xname, hh->atw_fc[0], hh->atw_fc[1],
			    hh->atw_tail_plcplen, hh->atw_head_plcplen,
			    hh->atw_tail_dur, hh->atw_head_dur);

			printf("%s: hdrctl = 0x%04x, fragthr = 0x%04x, "
			    "fragnum = 0x%02x, rtylmt = 0x%04x\n",
			    sc->sc_dev.dv_xname, hh->atw_hdrctl,
			    hh->atw_fragthr, hh->atw_fragnum, hh->atw_rtylmt);

			printf("%s: keyid = %d\n",
			    sc->sc_dev.dv_xname, hh->atw_keyid);

			atw_dump_pkt(ifp, m0);
		}
#endif /* ATW_DEBUG */

		dmamap = txs->txs_dmamap;

		/*
		 * Load the DMA map.  Copy and try (once) again if the packet
		 * didn't fit in the allotted number of segments.
		 */
		for (first = 1;
		     (error = bus_dmamap_load_mbuf(sc->sc_dmat, dmamap, m0,
		                  BUS_DMA_WRITE|BUS_DMA_NOWAIT)) != 0 && first;
		     first = 0) {
			MGETHDR(m, M_DONTWAIT, MT_DATA);
			if (m == NULL) {
				printf("%s: unable to allocate Tx mbuf\n",
				    sc->sc_dev.dv_xname);
				break;
			}
			if (m0->m_pkthdr.len > MHLEN) {
				MCLGET(m, M_DONTWAIT);
				if ((m->m_flags & M_EXT) == 0) {
					printf("%s: unable to allocate Tx "
					    "cluster\n", sc->sc_dev.dv_xname);
					m_freem(m);
					break;
				}
			}
			m_copydata(m0, 0, m0->m_pkthdr.len, mtod(m, caddr_t));
			m->m_pkthdr.len = m->m_len = m0->m_pkthdr.len;
			m_freem(m0);
			m0 = m;
			m = NULL;
		}
		if (error != 0) {
			printf("%s: unable to load Tx buffer, "
			    "error = %d\n", sc->sc_dev.dv_xname, error);
			m_freem(m0);
			break;
		}

		/*
		 * Ensure we have enough descriptors free to describe
		 * the packet.
		 */
		if (dmamap->dm_nsegs > sc->sc_txfree) {
			/*
			 * Not enough free descriptors to transmit
			 * this packet.  Unload the DMA map and
			 * drop the packet.  Notify the upper layer
			 * that there are no more slots left.
			 *
			 * XXX We could allocate an mbuf and copy, but
			 * XXX it is worth it?
			 */
			ifq_set_oactive(&ifp->if_snd);
			bus_dmamap_unload(sc->sc_dmat, dmamap);
			m_freem(m0);
			break;
		}

		/*
		 * WE ARE NOW COMMITTED TO TRANSMITTING THE PACKET.
		 */

		/* Sync the DMA map. */
		bus_dmamap_sync(sc->sc_dmat, dmamap, 0, dmamap->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);

		/* XXX arbitrary retry limit; 8 because I have seen it in
		 * use already and maybe 0 means "no tries" !
		 */
		ctl = htole32(LSHIFT(8, ATW_TXCTL_TL_MASK));

		DPRINTF2(sc, ("%s: TXDR <- max(10, %d)\n",
		    sc->sc_dev.dv_xname, rate * 5));
		ctl |= htole32(LSHIFT(MAX(10, rate * 5), ATW_TXCTL_TXDR_MASK));

		/*
		 * Initialize the transmit descriptors.
		 */
		for (nexttx = sc->sc_txnext, seg = 0;
		     seg < dmamap->dm_nsegs;
		     seg++, nexttx = ATW_NEXTTX(nexttx)) {
			/*
			 * If this is the first descriptor we're
			 * enqueueing, don't set the OWN bit just
			 * yet.  That could cause a race condition.
			 * We'll do it below.
			 */
			txd = &sc->sc_txdescs[nexttx];
			txd->at_ctl = ctl |
			    ((nexttx == firsttx) ? 0 : htole32(ATW_TXCTL_OWN));

			txd->at_buf1 = htole32(dmamap->dm_segs[seg].ds_addr);
			txd->at_flags =
			    htole32(LSHIFT(dmamap->dm_segs[seg].ds_len,
			                   ATW_TXFLAG_TBS1_MASK)) |
			    ((nexttx == (ATW_NTXDESC - 1))
			        ? htole32(ATW_TXFLAG_TER) : 0);
			lasttx = nexttx;
		}

		if (lasttx == -1)
			panic("%s: bad lastx", ifp->if_xname);
		/* Set `first segment' and `last segment' appropriately. */
		sc->sc_txdescs[sc->sc_txnext].at_flags |=
		    htole32(ATW_TXFLAG_FS);
		sc->sc_txdescs[lasttx].at_flags |= htole32(ATW_TXFLAG_LS);

#ifdef ATW_DEBUG
		if ((ifp->if_flags & IFF_DEBUG) != 0 && atw_debug > 2) {
			printf("     txsoft %p transmit chain:\n", txs);
			for (seg = sc->sc_txnext;; seg = ATW_NEXTTX(seg)) {
				printf("     descriptor %d:\n", seg);
				printf("       at_ctl:   0x%08x\n",
				    letoh32(sc->sc_txdescs[seg].at_ctl));
				printf("       at_flags:      0x%08x\n",
				    letoh32(sc->sc_txdescs[seg].at_flags));
				printf("       at_buf1: 0x%08x\n",
				    letoh32(sc->sc_txdescs[seg].at_buf1));
				printf("       at_buf2: 0x%08x\n",
				    letoh32(sc->sc_txdescs[seg].at_buf2));
				if (seg == lasttx)
					break;
			}
		}
#endif

		/* Sync the descriptors we're using. */
		ATW_CDTXSYNC(sc, sc->sc_txnext, dmamap->dm_nsegs,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

		/*
		 * Store a pointer to the packet so we can free it later,
		 * and remember what txdirty will be once the packet is
		 * done.
		 */
		txs->txs_mbuf = m0;
		txs->txs_firstdesc = sc->sc_txnext;
		txs->txs_lastdesc = lasttx;
		txs->txs_ndescs = dmamap->dm_nsegs;

		/* Advance the tx pointer. */
		sc->sc_txfree -= dmamap->dm_nsegs;
		sc->sc_txnext = nexttx;

		SIMPLEQ_REMOVE_HEAD(&sc->sc_txfreeq, txs_q);
		SIMPLEQ_INSERT_TAIL(&sc->sc_txdirtyq, txs, txs_q);

		last_txs = txs;
	}

	if (txs == NULL || sc->sc_txfree == 0) {
		/* No more slots left; notify upper layer. */
		ifq_set_oactive(&ifp->if_snd);
	}

	if (sc->sc_txfree != ofree) {
		DPRINTF2(sc, ("%s: packets enqueued, IC on %d, OWN on %d\n",
		    sc->sc_dev.dv_xname, lasttx, firsttx));
		/*
		 * Cause a transmit interrupt to happen on the
		 * last packet we enqueued.
		 */
		sc->sc_txdescs[lasttx].at_flags |= htole32(ATW_TXFLAG_IC);
		ATW_CDTXSYNC(sc, lasttx, 1,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

		/*
		 * The entire packet chain is set up.  Give the
		 * first descriptor to the chip now.
		 */
		sc->sc_txdescs[firsttx].at_ctl |= htole32(ATW_TXCTL_OWN);
		ATW_CDTXSYNC(sc, firsttx, 1,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

		/* Wake up the transmitter. */
		ATW_WRITE(sc, ATW_TDR, 0x1);

		/* Set a watchdog timer in case the chip flakes out. */
		sc->sc_tx_timer = 5;
		ifp->if_timer = 1;
	}
}

int
atw_activate(struct device *self, int act)
{
	struct atw_softc *sc = (struct atw_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			atw_stop(ifp, 1);
		if (sc->sc_power != NULL)
			(*sc->sc_power)(sc, act);
		break;
	case DVACT_WAKEUP:
		atw_wakeup(sc);
		break;
	}
	return 0;
}

void
atw_wakeup(struct atw_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	if (sc->sc_power != NULL)
		(*sc->sc_power)(sc, DVACT_RESUME);
	if (ifp->if_flags & IFF_UP)
		atw_init(ifp);
}

/*
 * atw_ioctl:		[ifnet interface function]
 *
 *	Handle control requests from the operator.
 */
int
atw_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct atw_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	/* XXX monkey see, monkey do. comes from wi_ioctl. */
	if ((sc->sc_dev.dv_flags & DVF_ACTIVE) == 0)
		return ENXIO;

	s = splnet();

	switch (cmd) {
        case SIOCSIFADDR:
                ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ATW_IS_ENABLED(sc)) {
				/*
				 * To avoid rescanning another access point,
				 * do not call atw_init() here.  Instead,
				 * only reflect media settings.
				 */
				atw_filter_setup(sc);
			} else
				error = atw_init(ifp);
		} else if (ATW_IS_ENABLED(sc))
			atw_stop(ifp, 1);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		error = (cmd == SIOCADDMULTI) ?
		    ether_addmulti(ifr, &sc->sc_ic.ic_ac) :
		    ether_delmulti(ifr, &sc->sc_ic.ic_ac);

		if (error == ENETRESET) {
			if (ifp->if_flags & IFF_RUNNING)
				atw_filter_setup(sc); /* do not rescan */
			error = 0;
		}
		break;

	default:
		error = ieee80211_ioctl(ifp, cmd, data);
		if (error == ENETRESET) {
			if (ATW_IS_ENABLED(sc))
				error = atw_init(ifp);
			else
				error = 0;
		}
		break;
	}

	/* Try to get more packets going. */
	if (ATW_IS_ENABLED(sc))
		atw_start(ifp);

	splx(s);
	return (error);
}

int
atw_media_change(struct ifnet *ifp)
{
	int error;

	error = ieee80211_media_change(ifp);
	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_RUNNING|IFF_UP)) ==
		    (IFF_RUNNING|IFF_UP))
			atw_init(ifp);		/* XXX lose error */
		error = 0;
	}
	return error;
}

void
atw_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct atw_softc *sc = ifp->if_softc;

	if (ATW_IS_ENABLED(sc) == 0) {
		imr->ifm_active = IFM_IEEE80211 | IFM_NONE;
		imr->ifm_status = 0;
		return;
	}
	ieee80211_media_status(ifp, imr);
}
