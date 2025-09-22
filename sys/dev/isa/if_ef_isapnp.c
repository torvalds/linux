/*	$OpenBSD: if_ef_isapnp.c,v 1.42 2023/09/11 08:41:26 mvs Exp $	*/

/*
 * Copyright (c) 1999 Jason L. Wright (jason@thought.net)
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/kernel.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/cpu.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/isa/isavar.h>
#include <dev/isa/isadmavar.h>
#include <dev/ic/elink3reg.h>

#undef EF_DEBUG

struct ef_softc {
	struct device		sc_dv;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	struct arpcom		sc_arpcom;
	struct mii_data		sc_mii;
	struct timeout		sc_tick_tmo;
	void *			sc_ih;
	int			sc_tx_start_thresh;
	int			sc_tx_succ_ok;
	int			sc_busmaster;
};

#define	EF_W0_EEPROM_COMMAND	0x200a
#define    EF_EEPROM_BUSY	(1 << 9)
#define    EF_EEPROM_READ	(1 << 7)
#define	EF_W0_EEPROM_DATA	0x200c

#define	EF_W1_TX_PIO_WR_1	0x10
#define	EF_W1_RX_PIO_RR_1	0x10
#define	EF_W1_RX_ERRORS		0x14
#define	EF_W1_RX_STATUS		0x18
#define	EF_W1_TX_STATUS		0x1b
#define	EF_W1_FREE_TX		0x1c

#define	EF_W4_MEDIA		0x0a
#define    EF_MEDIA_SQE		0x0008		/* sqe error for aui */
#define	   EF_MEDIA_TP		0x00c0		/* link/jabber, 10baseT */
#define	   EF_MEDIA_LNK		0x0080		/* linkbeat, 100baseTX/FX */
#define	   EF_MEDIA_LNKBEAT	0x0800

/* Window 4: EP_W4_CTRLR_STATUS: mii manipulation */
#define	EF_MII_CLK		0x01		/* clock bit */
#define	EF_MII_DATA		0x02		/* data bit */
#define	EF_MII_DIR		0x04		/* direction */

int ef_isapnp_match(struct device *, void *, void *);
void ef_isapnp_attach(struct device *, struct device *, void *);

void efstart(struct ifnet *);
int efioctl(struct ifnet *, u_long, caddr_t);
void efwatchdog(struct ifnet *);
void efreset(struct ef_softc *);
void efstop(struct ef_softc *);
void efsetmulti(struct ef_softc *);
int efbusyeeprom(struct ef_softc *);
int efintr(void *);
void efinit(struct ef_softc *);
void efcompletecmd(struct ef_softc *, u_int, u_int);
void eftxstat(struct ef_softc *);
void efread(struct ef_softc *);
struct mbuf *efget(struct ef_softc *, int totlen);

void ef_miibus_writereg(struct device *, int, int, int);
void ef_miibus_statchg(struct device *);
int ef_miibus_readreg(struct device *, int, int);
void ef_mii_writeb(struct ef_softc *, int);
void ef_mii_sync(struct ef_softc *);
int ef_ifmedia_upd(struct ifnet *);
void ef_ifmedia_sts(struct ifnet *, struct ifmediareq *);
void ef_tick(void *);

struct cfdriver ef_cd = {
	NULL, "ef", DV_IFNET
};

const struct cfattach ef_isapnp_ca = {
	sizeof(struct ef_softc), ef_isapnp_match, ef_isapnp_attach
};

int
ef_isapnp_match(struct device *parent, void *match, void *aux)
{
	return (1);
}

void
ef_isapnp_attach(struct device *parent, struct device *self, void *aux)
{
	struct ef_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	int i;
	u_int16_t x;
	u_int32_t cfg;

	sc->sc_iot = iot = ia->ia_iot;
	sc->sc_ioh = ioh = ia->ipa_io[0].h;

	efcompletecmd(sc, EP_COMMAND, GLOBAL_RESET);
	DELAY(1500);

	for (i = 0; i < 3; i++) {
		if (efbusyeeprom(sc))
			return;

		bus_space_write_2(iot, ioh, EF_W0_EEPROM_COMMAND,
		    EF_EEPROM_READ | i);

		if (efbusyeeprom(sc))
			return;

		x = bus_space_read_2(iot, ioh, EF_W0_EEPROM_DATA);

		sc->sc_arpcom.ac_enaddr[(i << 1)] = x >> 8;
		sc->sc_arpcom.ac_enaddr[(i << 1) + 1] = x;
	}

	printf(": address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));

	GO_WINDOW(3);
	cfg = bus_space_read_4(iot, ioh, EP_W3_INTERNAL_CONFIG);
	cfg &= ~(0x00f00000);
	cfg |= (0x06 << 20);
	bus_space_write_4(iot, ioh, EP_W3_INTERNAL_CONFIG, cfg);

	sc->sc_ih = isa_intr_establish(ia->ia_ic, ia->ia_irq, IST_EDGE,
	    IPL_NET, efintr, sc, sc->sc_dv.dv_xname);

	if (ia->ia_drq != DRQUNK)
		isadma_cascade(ia->ia_drq);

	timeout_set(&sc->sc_tick_tmo, ef_tick, sc);

	bcopy(sc->sc_dv.dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_start = efstart;
	ifp->if_ioctl = efioctl;
	ifp->if_watchdog = efwatchdog;
	ifp->if_flags =
	    IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = ef_miibus_readreg;
	sc->sc_mii.mii_writereg = ef_miibus_writereg;
	sc->sc_mii.mii_statchg = ef_miibus_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, 0, ef_ifmedia_upd, ef_ifmedia_sts);
	mii_attach(self, &sc->sc_mii, 0xffffffff, MII_PHY_ANY, MII_OFFSET_ANY,
	    0);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE, 0, NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	sc->sc_tx_start_thresh = 20;

	efcompletecmd(sc, EP_COMMAND, RX_RESET);
	efcompletecmd(sc, EP_COMMAND, TX_RESET);
}

void
efstart(struct ifnet *ifp)
{
	struct ef_softc *sc = ifp->if_softc;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	struct mbuf *m, *m0;
	int s, len, pad, i;
	int fillcnt = 0;
	u_int32_t filler = 0;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

startagain:
	m0 = ifq_deq_begin(&ifp->if_snd);
	if (m0 == NULL)
		return;

	if ((m0->m_flags & M_PKTHDR) == 0)
		panic("efstart: no header mbuf");
	len = m0->m_pkthdr.len;
	pad = (4 - len) & 3;

	if (len + pad > ETHER_MAX_LEN) {
		ifp->if_oerrors++;
		ifq_deq_commit(&ifp->if_snd, m0);
		m_freem(m0);
		goto startagain;
	}

	if (bus_space_read_2(iot, ioh, EF_W1_FREE_TX) < len + pad + 4) {
		bus_space_write_2(iot, ioh, EP_COMMAND,
		    SET_TX_AVAIL_THRESH | ((len + pad) >> 2));
		ifq_deq_rollback(&ifp->if_snd, m0);
		ifq_set_oactive(&ifp->if_snd);
		return;
	} else {
		bus_space_write_2(iot, ioh, EP_COMMAND,
		    SET_TX_AVAIL_THRESH | EP_THRESH_DISABLE);
	}

	bus_space_write_2(iot, ioh, EP_COMMAND, SET_TX_START_THRESH |
	    ((len / 4 + sc->sc_tx_start_thresh)));

#if NBPFILTER
	if (ifp->if_bpf)
		bpf_mtap(ifp->if_bpf, m0, BPF_DIRECTION_OUT);
#endif

	ifq_deq_commit(&ifp->if_snd, m0);
	if (m0 == NULL) /* XXX not needed */
		return;

	s = splhigh();

	bus_space_write_4(iot, ioh, EF_W1_TX_PIO_WR_1, len);
	for (m = m0; m; ) {
		if (fillcnt) {
			while (m->m_len && fillcnt < 4) {
				fillcnt++;
				filler >>= 8;
				filler |= m->m_data[0] << 24;
				m->m_data++;
				m->m_len--;
			}
			if (fillcnt == 4) {
				bus_space_write_4(iot, ioh,
				    EF_W1_TX_PIO_WR_1, filler);
				filler = 0;
				fillcnt = 0;
			}
		}

		if (m->m_len & ~3)
			bus_space_write_multi_4(iot, ioh,
			    EF_W1_TX_PIO_WR_1, (u_int32_t *)m->m_data,
			    m->m_len >> 2);
		for (i = 0; i < (m->m_len & 3); i++) {
			fillcnt++;
			filler >>= 8;
			filler |= m->m_data[(m->m_len & ~3) + i] << 24;
		}
		m0 = m_free(m);
		m = m0;
	}

	if (fillcnt) {
		bus_space_write_4(iot, ioh, EF_W1_TX_PIO_WR_1,
		    filler >> (32 - (8 * fillcnt)));
		fillcnt = 0;
		filler = 0;
	}

	splx(s);

	goto startagain;
}

int
efioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ef_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		efinit(sc);
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;
	case SIOCSIFFLAGS:
		if ((ifp->if_flags & IFF_UP) == 0 &&
		    (ifp->if_flags & IFF_RUNNING) != 0) {
			efstop(sc);
			ifp->if_flags &= ~IFF_RUNNING;
		} else if ((ifp->if_flags & IFF_UP) != 0 &&
			   (ifp->if_flags & IFF_RUNNING) == 0) {
			efinit(sc);
		}
		efsetmulti(sc);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING) {
			efreset(sc);
			efsetmulti(sc);
		}
		error = 0;
	}

	splx(s);
	return (error);
}

void
efinit(struct ef_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int i, s;

	s = splnet();

	efstop(sc);

	while (bus_space_read_2(iot, ioh, EP_STATUS) & S_COMMAND_IN_PROGRESS)
		;

	GO_WINDOW(2);
	for (i = 0; i < 6; i++)
		bus_space_write_1(iot, ioh, EP_W2_ADDR_0 + i,
		    sc->sc_arpcom.ac_enaddr[i]);
	for (i = 0; i < 3; i += 2)
		bus_space_write_2(iot, ioh, EP_W2_RECVMASK_0 + (i * 2), 0);

	efcompletecmd(sc, EP_COMMAND, RX_RESET);
	efcompletecmd(sc, EP_COMMAND, TX_RESET);

	bus_space_write_2(iot, ioh, EP_COMMAND,
	    SET_TX_AVAIL_THRESH | (ETHER_MAX_DIX_LEN >> 2));

	efsetmulti(sc);

	bus_space_write_2(iot, ioh, EP_COMMAND, STATUS_ENABLE | 0);

	GO_WINDOW(6);
	for (i = 0; i < 10; i++)
		(void)bus_space_read_1(iot, ioh, i);
	(void)bus_space_read_2(iot, ioh, 10);
	(void)bus_space_read_2(iot, ioh, 12);
	GO_WINDOW(4);
	(void)bus_space_read_1(iot, ioh, 12);
	bus_space_write_2(iot, ioh, EP_W4_NET_DIAG, 0x0040);

	GO_WINDOW(7);

	efsetmulti(sc);

	bus_space_write_2(iot, ioh, EP_COMMAND, RX_ENABLE);
	bus_space_write_2(iot, ioh, EP_COMMAND, TX_ENABLE);

	bus_space_write_2(iot, ioh, EP_COMMAND, STATUS_ENABLE |
	    S_CARD_FAILURE | S_INT_RQD | S_UPD_STATS | S_TX_COMPLETE |
	    S_TX_AVAIL | S_RX_COMPLETE |
	    (sc->sc_busmaster ? S_DMA_DONE : 0));
	bus_space_write_2(iot, ioh, EP_COMMAND, ACK_INTR |
	    S_INTR_LATCH | S_TX_AVAIL | S_RX_EARLY | S_INT_RQD);
	bus_space_write_2(iot, ioh, EP_COMMAND, SET_INTR_MASK |
	    S_INTR_LATCH | S_TX_AVAIL | S_RX_COMPLETE | S_UPD_STATS |
	    (sc->sc_busmaster ? S_DMA_DONE : 0) | S_UP_COMPLETE |
	    S_DOWN_COMPLETE | S_CARD_FAILURE | S_TX_COMPLETE);

	mii_mediachg(&sc->sc_mii);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);

	timeout_add_sec(&sc->sc_tick_tmo, 1);

	efstart(ifp);
}

void
efreset(struct ef_softc *sc)
{
	int s;

	s = splnet();
	efstop(sc);
	efinit(sc);
	splx(s);
}

void
efstop(struct ef_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	timeout_del(&sc->sc_tick_tmo);

	bus_space_write_2(iot, ioh, EP_COMMAND, RX_DISABLE);
	efcompletecmd(sc, EP_COMMAND, RX_DISCARD_TOP_PACK);

	bus_space_write_2(iot, ioh, EP_COMMAND, TX_DISABLE);
	bus_space_write_2(iot, ioh, EP_COMMAND, STOP_TRANSCEIVER);

	efcompletecmd(sc, EP_COMMAND, RX_RESET);
	efcompletecmd(sc, EP_COMMAND, TX_RESET);

	bus_space_write_2(iot, ioh, EP_COMMAND, C_INTR_LATCH);
	bus_space_write_2(iot, ioh, EP_COMMAND, SET_RD_0_MASK);
	bus_space_write_2(iot, ioh, EP_COMMAND, SET_INTR_MASK);
	bus_space_write_2(iot, ioh, EP_COMMAND, SET_RX_FILTER);
}

void
efcompletecmd(struct ef_softc *sc, u_int cmd, u_int arg)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	bus_space_write_2(iot, ioh, cmd, arg);
	while (bus_space_read_2(iot, ioh, EP_STATUS) & S_COMMAND_IN_PROGRESS)
		;
}

int
efintr(void *vsc)
{
	struct ef_softc *sc = vsc;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	u_int16_t status;
	int r = 0;

	status = bus_space_read_2(iot, ioh, EP_STATUS);

	do {
		if (status & S_RX_COMPLETE) {
			r = 1;
			bus_space_write_2(iot, ioh, EP_STATUS, C_RX_COMPLETE);
			efread(sc);
		}
		if (status & S_TX_AVAIL) {
			bus_space_write_2(iot, ioh, EP_STATUS, C_TX_AVAIL);
			r = 1;
			ifq_clr_oactive(&sc->sc_arpcom.ac_if.if_snd);
			efstart(&sc->sc_arpcom.ac_if);
		}
		if (status & S_CARD_FAILURE) {
			r = 1;
			efreset(sc);
			printf("%s: adapter failure (%x)\n",
			   sc->sc_dv.dv_xname, status);
			bus_space_write_2(iot, ioh, EP_COMMAND,
					  C_CARD_FAILURE);
			return (1);
		}
		if (status & S_TX_COMPLETE) {
			r = 1;
			eftxstat(sc);
			efstart(ifp);
		}
		bus_space_write_2(iot, ioh, EP_COMMAND,
		    C_INTR_LATCH | C_INT_RQD);
	} while ((status = bus_space_read_2(iot, ioh, EP_STATUS)) &
	    (S_INT_RQD | S_RX_COMPLETE));

	return (r);
}

void
eftxstat(struct ef_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int i;

	while ((i = bus_space_read_1(iot, ioh, EF_W1_TX_STATUS)) &
	   TXS_COMPLETE) {
		bus_space_write_1(iot, ioh, EF_W1_TX_STATUS, 0);

		if (i & TXS_JABBER) {
			sc->sc_arpcom.ac_if.if_oerrors++;
#ifdef EF_DEBUG
			if (sc->sc_arpcom.ac_if.if_flags & IFF_DEBUG)
				printf("%s: jabber (%x)\n",
				    sc->sc_dv.dv_xname, i);
#endif
			efreset(sc);
		}
		else if (i & TXS_UNDERRUN) {
			sc->sc_arpcom.ac_if.if_oerrors++;
#ifdef EF_DEBUG
			if (sc->sc_arpcom.ac_if.if_flags & IFF_DEBUG)
				printf("%s: fifo underrun (%x) @%d\n",
				    sc->sc_dv.dv_xname, i,
				    sc->sc_tx_start_thresh);
#endif
			if (sc->sc_tx_succ_ok < 100)
				sc->sc_tx_start_thresh = min(ETHER_MAX_LEN,
				    sc->sc_tx_start_thresh + 20);
			sc->sc_tx_succ_ok = 0;
			efreset(sc);
		}
		else if (i & TXS_MAX_COLLISION) {
			sc->sc_arpcom.ac_if.if_collisions++;
			bus_space_write_2(iot, ioh, EP_COMMAND, TX_ENABLE);
			ifq_clr_oactive(&sc->sc_arpcom.ac_if.if_snd);
		}
		else
			sc->sc_tx_succ_ok = (sc->sc_tx_succ_ok + 1) & 127;
	}
}

int
efbusyeeprom(struct ef_softc *sc)
{
	int i = 100, j;

	while (i--) {
		j = bus_space_read_2(sc->sc_iot, sc->sc_ioh,
				     EF_W0_EEPROM_COMMAND);
		if (j & EF_EEPROM_BUSY)
			delay(100);
		else
			break;
	}
	if (i == 0) {
		printf("%s: eeprom failed to come ready\n",
		   sc->sc_dv.dv_xname);
		return (1);
	}

	return (0);
}

void
efwatchdog(struct ifnet *ifp)
{
	struct ef_softc *sc = ifp->if_softc;

	printf("%s: device timeout\n", sc->sc_dv.dv_xname);
	sc->sc_arpcom.ac_if.if_oerrors++;
	efreset(sc);
}

void
efsetmulti(struct ef_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct arpcom *ac = &sc->sc_arpcom;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	struct ether_multi *enm;
	struct ether_multistep step;
	u_int16_t cmd = SET_RX_FILTER | FIL_INDIVIDUAL | FIL_BRDCST;
	int mcnt = 0;

	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		mcnt++;
		ETHER_NEXT_MULTI(step, enm);
	}
	if (mcnt || ifp->if_flags & IFF_ALLMULTI)
		cmd |= FIL_MULTICAST;

	if (ifp->if_flags & IFF_PROMISC)
		cmd |= FIL_PROMISC;

	bus_space_write_2(iot, ioh, EP_COMMAND, cmd);
}

void
efread(struct ef_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	int len;

	len = bus_space_read_2(iot, ioh, EF_W1_RX_STATUS);

#ifdef EF_DEBUG
	if (ifp->if_flags & IFF_DEBUG) {
		int err = len & ERR_MASK;
		char *s = NULL;

		if (len & ERR_INCOMPLETE)
			s = "incomplete packet";
		else if (err == ERR_OVERRUN)
			s = "packet overrun";
		else if (err == ERR_RUNT)
			s = "runt packet";
		else if (err == ERR_ALIGNMENT)
			s = "bad alignment";
		else if (err == ERR_CRC)
			s = "bad crc";
		else if (err == ERR_OVERSIZE)
			s = "oversized packet";
		else if (err == ERR_DRIBBLE)
			s = "dribble bits";

		if (s)
			printf("%s: %s\n", sc->sc_dv.dv_xname, s);
	}
#endif

	if (len & ERR_INCOMPLETE)
		return;

	if (len & ERR_RX) {
		ifp->if_ierrors++;
		efcompletecmd(sc, EP_COMMAND, RX_DISCARD_TOP_PACK);
		return;
	}

	len &= RX_BYTES_MASK;
	m = efget(sc, len);
	if (m == NULL) {
		ifp->if_ierrors++;
		efcompletecmd(sc, EP_COMMAND, RX_DISCARD_TOP_PACK);
		return;
	}

	ml_enqueue(&ml, m);
	if_input(ifp, &ml);
}

struct mbuf *
efget(struct ef_softc *sc, int totlen)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	struct mbuf *top, **mp, *m;
	int len, pad, s;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (NULL);
	m->m_pkthdr.len = totlen;
	pad = ALIGN(sizeof(struct ether_header)) - sizeof(struct ether_header);
	m->m_data += pad;
	len = MHLEN -pad;
	top = 0;
	mp = &top;

	s = splhigh();

	while (totlen > 0) {
		if (top) {
			MGET(m, M_DONTWAIT, MT_DATA);
			if (m == NULL) {
				m_freem(top);
				splx(s);
				return (NULL);
			}
			len = MLEN;
		}
		if (top && totlen >= MINCLSIZE) {
			MCLGET(m, M_DONTWAIT);
			if (m->m_flags & M_EXT)
				len = MCLBYTES;
		}
		len = min(totlen, len);
		if (len > 1) {
			len &= ~1;
			bus_space_read_raw_multi_2(iot, ioh,
			    EF_W1_RX_PIO_RR_1, mtod(m, u_int8_t *),
			    len);
		} else
			*(mtod(m, u_int8_t *)) =
			    bus_space_read_1(iot, ioh, EF_W1_RX_PIO_RR_1);

		m->m_len = len;
		totlen -= len;
		*mp = m;
		mp = &m->m_next;
	}

	efcompletecmd(sc, EP_COMMAND, RX_DISCARD_TOP_PACK);

	splx(s);

	return (top);
}

#define MII_SET(sc, x) \
	bus_space_write_2((sc)->sc_iot, (sc)->sc_ioh, EP_W4_CTRLR_STATUS, \
	    bus_space_read_2((sc)->sc_iot, (sc)->sc_ioh, EP_W4_CTRLR_STATUS) \
	    | (x))

#define MII_CLR(sc, x) \
	bus_space_write_2((sc)->sc_iot, (sc)->sc_ioh, EP_W4_CTRLR_STATUS, \
	    bus_space_read_2((sc)->sc_iot, (sc)->sc_ioh, EP_W4_CTRLR_STATUS) \
	    & (~(x)))

void
ef_mii_writeb(struct ef_softc *sc, int b)
{
	MII_CLR(sc, EF_MII_CLK);

	if (b)
		MII_SET(sc, EF_MII_DATA);
	else
		MII_CLR(sc, EF_MII_DATA);

	MII_CLR(sc, EF_MII_CLK);
	DELAY(1);
	MII_SET(sc, EF_MII_CLK);
	DELAY(1);
}

void
ef_mii_sync(struct ef_softc *sc)
{
	int i;

	for (i = 0; i < 32; i++)
		ef_mii_writeb(sc, 1);
}

int
ef_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct ef_softc *sc = (struct ef_softc *)dev;
	int i, ack, s, val = 0;

	s = splnet();

	GO_WINDOW(4);
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, EP_W4_CTRLR_STATUS, 0);

	/* Turn on xmit */
	MII_SET(sc, EF_MII_DIR);
	MII_CLR(sc, EF_MII_CLK);

	ef_mii_sync(sc);

	/* Transmit start sequence */
	ef_mii_writeb(sc, 0);
	ef_mii_writeb(sc, 1);

	/* Transmit read sequence */
	ef_mii_writeb(sc, 1);
	ef_mii_writeb(sc, 0);

	/* Transmit phy addr */
	for (i = 0x10; i; i >>= 1)
		ef_mii_writeb(sc, (phy & i) ? 1 : 0);

	/* Transmit reg addr */
	for (i = 0x10; i; i >>= 1)
		ef_mii_writeb(sc, (reg & i) ? 1 : 0);

	/* First cycle of turnaround */
	MII_CLR(sc, EF_MII_CLK | EF_MII_DATA);
	DELAY(1);
	MII_SET(sc, EF_MII_CLK);
	DELAY(1);

	/* Turn off xmit */
	MII_CLR(sc, EF_MII_DIR);

	/* Second cycle of turnaround */
	MII_CLR(sc, EF_MII_CLK);
	DELAY(1);
	MII_SET(sc, EF_MII_CLK);
	DELAY(1);
	ack = bus_space_read_2(sc->sc_iot, sc->sc_ioh, EP_W4_CTRLR_STATUS) &
	    EF_MII_DATA;

	/* Read 16bit data */
	for (i = 0x8000; i; i >>= 1) {
		MII_CLR(sc, EF_MII_CLK);
		DELAY(1);
		if (bus_space_read_2(sc->sc_iot, sc->sc_ioh,
				     EP_W4_CTRLR_STATUS) & EF_MII_DATA)
			val |= i;
		MII_SET(sc, EF_MII_CLK);
		DELAY(1);
	}

	MII_CLR(sc, EF_MII_CLK);
	DELAY(1);
	MII_SET(sc, EF_MII_CLK);
	DELAY(1);

	splx(s);

	return (val);
}

void
ef_miibus_writereg(struct device *dev, int phy, int reg, int val)
{
	struct ef_softc *sc = (struct ef_softc *)dev;
	int s, i;

	s = splnet();

	GO_WINDOW(4);
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, EP_W4_CTRLR_STATUS, 0);

	/* Turn on xmit */
	MII_SET(sc, EF_MII_DIR);

	ef_mii_sync(sc);

	ef_mii_writeb(sc, 0);
	ef_mii_writeb(sc, 1);
	ef_mii_writeb(sc, 0);
	ef_mii_writeb(sc, 1);

	for (i = 0x10; i; i >>= 1)
		ef_mii_writeb(sc, (phy & i) ? 1 : 0);

	for (i = 0x10; i; i >>= 1)
		ef_mii_writeb(sc, (reg & i) ? 1 : 0);

	ef_mii_writeb(sc, 1);
	ef_mii_writeb(sc, 0);

	for (i = 0x8000; i; i >>= 1)
		ef_mii_writeb(sc, (val & i) ? 1 : 0);

	splx(s);
}

int
ef_ifmedia_upd(struct ifnet *ifp)
{
	struct ef_softc *sc = ifp->if_softc;

	mii_mediachg(&sc->sc_mii);
	return (0);
}

void
ef_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct ef_softc *sc = ifp->if_softc;

	mii_pollstat(&sc->sc_mii);
	ifmr->ifm_status = sc->sc_mii.mii_media_status;
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
}

void
ef_miibus_statchg(struct device *self)
{
	struct ef_softc *sc = (struct ef_softc *)self;
	int s;

	s = splnet();
	GO_WINDOW(3);
	/* Set duplex bit appropriately */
	if ((sc->sc_mii.mii_media_active & IFM_GMASK) == IFM_FDX)
		bus_space_write_1(sc->sc_iot, sc->sc_ioh,
		    EP_W3_MAC_CONTROL, 0x20);
	else
		bus_space_write_1(sc->sc_iot, sc->sc_ioh,
		    EP_W3_MAC_CONTROL, 0x00);
	GO_WINDOW(7);
	splx(s);
}

void
ef_tick(void *v)
{
	struct ef_softc *sc = v;
	int s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	splx(s);
	timeout_add_sec(&sc->sc_tick_tmo, 1);
}
