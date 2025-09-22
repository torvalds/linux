/*	$OpenBSD: if_bm.c,v 1.44 2022/03/13 12:33:01 mpi Exp $	*/
/*	$NetBSD: if_bm.c,v 1.1 1999/01/01 01:27:52 tsubai Exp $	*/

/*-
 * Copyright (C) 1998, 1999 Tsubai Masanari.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/kernel.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <uvm/uvm_extern.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/mii_bitbang.h>

#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/autoconf.h>

#include <macppc/dev/dbdma.h>
#include <macppc/dev/if_bmreg.h>

#define BMAC_TXBUFS	2
#define BMAC_RXBUFS	16
#define BMAC_BUFLEN	2048
#define	BMAC_BUFSZ	((BMAC_RXBUFS + BMAC_TXBUFS + 2) * BMAC_BUFLEN)

struct bmac_softc {
	struct device sc_dev;
	struct arpcom arpcom;	/* per-instance network data */
	struct timeout sc_tick_ch;
	vaddr_t sc_regs;
	bus_dma_tag_t sc_dmat;
	bus_dmamap_t sc_bufmap;
	bus_dma_segment_t sc_bufseg[1];
	dbdma_regmap_t *sc_txdma, *sc_rxdma;
	dbdma_command_t *sc_txcmd, *sc_rxcmd;
	dbdma_t sc_rxdbdma, sc_txdbdma;
	caddr_t sc_txbuf;
	paddr_t sc_txbuf_pa;
	caddr_t sc_rxbuf;
	paddr_t sc_rxbuf_pa;
	int sc_rxlast;
	int sc_flags;
	int sc_debug;
	int txcnt_outstanding;
	struct mii_data sc_mii;
};

#define BMAC_BMACPLUS	0x01

extern u_int *heathrow_FCR;

static __inline int bmac_read_reg(struct bmac_softc *, int);
static __inline void bmac_write_reg(struct bmac_softc *, int, int);
static __inline void bmac_set_bits(struct bmac_softc *, int, int);
static __inline void bmac_reset_bits(struct bmac_softc *, int, int);

static int bmac_match(struct device *, void *, void *);
static void bmac_attach(struct device *, struct device *, void *);
static void bmac_reset_chip(struct bmac_softc *);
static void bmac_init(struct bmac_softc *);
static void bmac_init_dma(struct bmac_softc *);
static int bmac_intr(void *);
static int bmac_rint(void *);
static void bmac_reset(struct bmac_softc *);
static void bmac_stop(struct bmac_softc *);
static void bmac_start(struct ifnet *);
static void bmac_transmit_packet(struct bmac_softc *, paddr_t, int);
static int bmac_put(struct bmac_softc *, caddr_t, struct mbuf *);
static struct mbuf *bmac_get(struct bmac_softc *, caddr_t, int);
static void bmac_watchdog(struct ifnet *);
static int bmac_ioctl(struct ifnet *, u_long, caddr_t);
static int bmac_mediachange(struct ifnet *);
static void bmac_mediastatus(struct ifnet *, struct ifmediareq *);
static void bmac_setladrf(struct bmac_softc *);

int bmac_mii_readreg(struct device *, int, int);
void bmac_mii_writereg(struct device *, int, int, int);
void bmac_mii_statchg(struct device *);
void bmac_mii_tick(void *);
u_int32_t bmac_mbo_read(struct device *);
void bmac_mbo_write(struct device *, u_int32_t);

const struct cfattach bm_ca = {
	sizeof(struct bmac_softc), bmac_match, bmac_attach
};

struct mii_bitbang_ops bmac_mbo = {
	bmac_mbo_read, bmac_mbo_write,
	{ MIFDO, MIFDI, MIFDC, MIFDIR, 0 }
};

struct cfdriver bm_cd = {
	NULL, "bm", DV_IFNET
};

int
bmac_read_reg(struct bmac_softc *sc, int off)
{
	return in16rb(sc->sc_regs + off);
}

void
bmac_write_reg(struct bmac_softc *sc, int off, int val)
{
	out16rb(sc->sc_regs + off, val);
}

void
bmac_set_bits(struct bmac_softc *sc, int off, int val)
{
	val |= bmac_read_reg(sc, off);
	bmac_write_reg(sc, off, val);
}

void
bmac_reset_bits(struct bmac_softc *sc, int off, int val)
{
	bmac_write_reg(sc, off, bmac_read_reg(sc, off) & ~val);
}

int
bmac_match(struct device *parent, void *cf, void *aux)
{
	struct confargs *ca = aux;

	if (ca->ca_nreg < 24 || ca->ca_nintr < 12)
		return (0);

	if (strcmp(ca->ca_name, "bmac") == 0)		/* bmac */
		return (1);
	if (strcmp(ca->ca_name, "ethernet") == 0)	/* bmac+ */
		return (1);

	return (0);
}

void
bmac_attach(struct device *parent, struct device *self, void *aux)
{
	struct confargs *ca = aux;
	struct bmac_softc *sc = (void *)self;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mii_data *mii = &sc->sc_mii;
	u_char laddr[6];
	int nseg, error;

	timeout_set(&sc->sc_tick_ch, bmac_mii_tick, sc);

	sc->sc_flags =0;
	if (strcmp(ca->ca_name, "ethernet") == 0) {
		sc->sc_flags |= BMAC_BMACPLUS;
	}

	ca->ca_reg[0] += ca->ca_baseaddr;
	ca->ca_reg[2] += ca->ca_baseaddr;
	ca->ca_reg[4] += ca->ca_baseaddr;

	sc->sc_regs = (vaddr_t)mapiodev(ca->ca_reg[0], NBPG);

	bmac_write_reg(sc, INTDISABLE, NoEventsMask);

	if (OF_getprop(ca->ca_node, "local-mac-address", laddr, 6) == -1 &&
	    OF_getprop(ca->ca_node, "mac-address", laddr, 6) == -1) {
		printf(": cannot get mac-address\n");
		return;
	}
	bcopy(laddr, sc->arpcom.ac_enaddr, 6);

	sc->sc_dmat = ca->ca_dmat;
	sc->sc_txdma = mapiodev(ca->ca_reg[2], 0x100);
	sc->sc_rxdma = mapiodev(ca->ca_reg[4], 0x100);
	sc->sc_txdbdma = dbdma_alloc(sc->sc_dmat, BMAC_TXBUFS);
	sc->sc_txcmd = sc->sc_txdbdma->d_addr;
	sc->sc_rxdbdma = dbdma_alloc(sc->sc_dmat, BMAC_RXBUFS + 1);
	sc->sc_rxcmd = sc->sc_rxdbdma->d_addr;

	error = bus_dmamem_alloc(sc->sc_dmat, BMAC_BUFSZ,
	    PAGE_SIZE, 0, sc->sc_bufseg, 1, &nseg, BUS_DMA_NOWAIT);
	if (error) {
		printf(": cannot allocate buffers (%d)\n", error);
		return;
	}

	error = bus_dmamem_map(sc->sc_dmat, sc->sc_bufseg, nseg,
	    BMAC_BUFSZ, &sc->sc_txbuf, BUS_DMA_NOWAIT);
	if (error) {
		printf(": cannot map buffers (%d)\n", error);
		bus_dmamem_free(sc->sc_dmat, sc->sc_bufseg, 1);
		return;
	}

	error = bus_dmamap_create(sc->sc_dmat, BMAC_BUFSZ, 1, BMAC_BUFSZ, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &sc->sc_bufmap);
	if (error) {
		printf(": cannot create buffer dmamap (%d)\n", error);
		bus_dmamem_unmap(sc->sc_dmat, sc->sc_txbuf, BMAC_BUFSZ);
		bus_dmamem_free(sc->sc_dmat, sc->sc_bufseg, 1);
		return;
	}

	error = bus_dmamap_load(sc->sc_dmat, sc->sc_bufmap, sc->sc_txbuf,
	    BMAC_BUFSZ, NULL, BUS_DMA_NOWAIT);
	if (error) {
		printf(": cannot load buffers dmamap (%d)\n", error);
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_bufmap);
		bus_dmamem_unmap(sc->sc_dmat, sc->sc_txbuf, BMAC_BUFSZ);
		bus_dmamem_free(sc->sc_dmat, sc->sc_bufseg, nseg);
		return;
	}

	sc->sc_txbuf_pa = sc->sc_bufmap->dm_segs->ds_addr;
	sc->sc_rxbuf = sc->sc_txbuf + BMAC_BUFLEN * BMAC_TXBUFS;
	sc->sc_rxbuf_pa = sc->sc_txbuf_pa + BMAC_BUFLEN * BMAC_TXBUFS;

	printf(" irq %d,%d: address %s\n", ca->ca_intr[0], ca->ca_intr[2],
		ether_sprintf(laddr));

	mac_intr_establish(parent, ca->ca_intr[0], IST_LEVEL, IPL_NET,
	    bmac_intr, sc, sc->sc_dev.dv_xname);
	mac_intr_establish(parent, ca->ca_intr[2], IST_LEVEL, IPL_NET,
	    bmac_rint, sc, sc->sc_dev.dv_xname);

	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_ioctl = bmac_ioctl;
	ifp->if_start = bmac_start;
	ifp->if_flags =
		IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_watchdog = bmac_watchdog;

	mii->mii_ifp = ifp;
	mii->mii_readreg = bmac_mii_readreg;
	mii->mii_writereg = bmac_mii_writereg;
	mii->mii_statchg = bmac_mii_statchg;

	ifmedia_init(&mii->mii_media, 0, bmac_mediachange, bmac_mediastatus);
	mii_attach(&sc->sc_dev, mii, 0xffffffff, MII_PHY_ANY,
	    MII_OFFSET_ANY, 0);

	/* Choose a default media. */
	if (LIST_FIRST(&mii->mii_phys) == NULL) {
		ifmedia_add(&mii->mii_media, IFM_ETHER|IFM_10_T, 0, NULL);
		ifmedia_set(&mii->mii_media, IFM_ETHER|IFM_10_T);
	} else
		ifmedia_set(&mii->mii_media, IFM_ETHER|IFM_AUTO);

	bmac_reset_chip(sc);

	if_attach(ifp);
	ether_ifattach(ifp);
}

/*
 * Reset and enable bmac by heathrow FCR.
 */
void
bmac_reset_chip(struct bmac_softc *sc)
{
	u_int v;

	dbdma_reset(sc->sc_txdma);
	dbdma_reset(sc->sc_rxdma);

	v = in32rb(heathrow_FCR);

	v |= EnetEnable;
	out32rb(heathrow_FCR, v);
	delay(50000);

	/* assert reset */
	v |= ResetEnetCell;
	out32rb(heathrow_FCR, v);
	delay(50000);

	/* deassert reset */
	v &= ~ResetEnetCell;
	out32rb(heathrow_FCR, v);
	delay(50000);

	/* enable */
	v |= EnetEnable;
	out32rb(heathrow_FCR, v);
	delay(50000);

	/* make certain they stay set? */
	out32rb(heathrow_FCR, v);
	v = in32rb(heathrow_FCR);
}

void
bmac_init(struct bmac_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ether_header *eh;
	caddr_t data;
	int tb;
	int i, bmcr;
	u_short *p;

	bmac_reset_chip(sc);

	/* XXX */
	bmcr = bmac_mii_readreg((struct device *)sc, 0, MII_BMCR);
	bmcr &= ~BMCR_ISO;
	bmac_mii_writereg((struct device *)sc, 0, MII_BMCR, bmcr);

	bmac_write_reg(sc, RXRST, RxResetValue);
	bmac_write_reg(sc, TXRST, TxResetBit);

	/* Wait for reset completion. */
	for (i = 1000; i > 0; i -= 10) {
		if ((bmac_read_reg(sc, TXRST) & TxResetBit) == 0)
			break;
		delay(10);
	}
	if (i <= 0)
		printf("%s: reset timeout\n", ifp->if_xname);

	if (! (sc->sc_flags & BMAC_BMACPLUS))
		bmac_set_bits(sc, XCVRIF, ClkBit|SerialMode|COLActiveLow);

	tb = ppc_mftbl();
	bmac_write_reg(sc, RSEED, tb);
	bmac_set_bits(sc, XIFC, TxOutputEnable);
	bmac_read_reg(sc, PAREG);

	/* Reset various counters. */
	bmac_write_reg(sc, NCCNT, 0);
	bmac_write_reg(sc, NTCNT, 0);
	bmac_write_reg(sc, EXCNT, 0);
	bmac_write_reg(sc, LTCNT, 0);
	bmac_write_reg(sc, FRCNT, 0);
	bmac_write_reg(sc, LECNT, 0);
	bmac_write_reg(sc, AECNT, 0);
	bmac_write_reg(sc, FECNT, 0);
	bmac_write_reg(sc, RXCV, 0);

	/* Set tx fifo information. */
	bmac_write_reg(sc, TXTH, 4);	/* 4 octets before tx starts */

	bmac_write_reg(sc, TXFIFOCSR, 0);
	bmac_write_reg(sc, TXFIFOCSR, TxFIFOEnable);

	/* Set rx fifo information. */
	bmac_write_reg(sc, RXFIFOCSR, 0);
	bmac_write_reg(sc, RXFIFOCSR, RxFIFOEnable);

	/* Clear status register. */
	bmac_read_reg(sc, STATUS);

	bmac_write_reg(sc, HASH3, 0);
	bmac_write_reg(sc, HASH2, 0);
	bmac_write_reg(sc, HASH1, 0);
	bmac_write_reg(sc, HASH0, 0);

	/* Set MAC address. */
	p = (u_short *)sc->arpcom.ac_enaddr;
	bmac_write_reg(sc, MADD0, *p++);
	bmac_write_reg(sc, MADD1, *p++);
	bmac_write_reg(sc, MADD2, *p);

	bmac_write_reg(sc, RXCFG,
		RxCRCEnable | RxHashFilterEnable | RxRejectOwnPackets);

	if (ifp->if_flags & IFF_PROMISC)
		bmac_set_bits(sc, RXCFG, RxPromiscEnable);

	bmac_init_dma(sc);

	/* Configure Media. */
	mii_mediachg(&sc->sc_mii);

	/* Enable TX/RX */
	bmac_set_bits(sc, RXCFG, RxMACEnable);
	bmac_set_bits(sc, TXCFG, TxMACEnable);

	bmac_write_reg(sc, INTDISABLE, NormalIntEvents);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	data = sc->sc_txbuf;
	eh = (struct ether_header *)data;

	bzero(data, sizeof(*eh) + ETHERMIN);
	bcopy(sc->arpcom.ac_enaddr, eh->ether_dhost, ETHER_ADDR_LEN);
	bcopy(sc->arpcom.ac_enaddr, eh->ether_shost, ETHER_ADDR_LEN);
	bmac_transmit_packet(sc, sc->sc_txbuf_pa, sizeof(*eh) + ETHERMIN);

	bmac_start(ifp);

	timeout_add_sec(&sc->sc_tick_ch, 1);
}

void
bmac_init_dma(struct bmac_softc *sc)
{
	dbdma_command_t *cmd = sc->sc_rxcmd;
	int i;

	dbdma_reset(sc->sc_txdma);
	dbdma_reset(sc->sc_rxdma);

	bzero(sc->sc_txcmd, BMAC_TXBUFS * sizeof(dbdma_command_t));
	bzero(sc->sc_rxcmd, (BMAC_RXBUFS + 1) * sizeof(dbdma_command_t));

	for (i = 0; i < BMAC_RXBUFS; i++) {
		DBDMA_BUILD(cmd, DBDMA_CMD_IN_LAST, 0, BMAC_BUFLEN,
			sc->sc_rxbuf_pa + BMAC_BUFLEN * i,
			DBDMA_INT_ALWAYS, DBDMA_WAIT_NEVER, DBDMA_BRANCH_NEVER);
		cmd++;
	}
	DBDMA_BUILD(cmd, DBDMA_CMD_NOP, 0, 0, 0,
		DBDMA_INT_NEVER, DBDMA_WAIT_NEVER, DBDMA_BRANCH_ALWAYS);
	dbdma_st32(&cmd->d_cmddep, sc->sc_rxdbdma->d_paddr);

	sc->sc_rxlast = 0;

	dbdma_start(sc->sc_rxdma, sc->sc_rxdbdma);
}

int
bmac_intr(void *v)
{
	struct bmac_softc *sc = v;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int stat;

#ifdef BMAC_DEBUG
	printf("bmac_intr called\n");
#endif
	stat = bmac_read_reg(sc, STATUS);
	if (stat == 0)
		return (0);

#ifdef BMAC_DEBUG
	printf("bmac_intr status = 0x%x\n", stat);
#endif

	if (stat & IntFrameSent) {
		ifq_clr_oactive(&ifp->if_snd);
		ifp->if_timer = 0;
		bmac_start(ifp);
	}

	/* XXX should do more! */

	return (1);
}

int
bmac_rint(void *v)
{
	struct bmac_softc *sc = v;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	dbdma_command_t *cmd;
	int status, resid, count, datalen;
	int i, n;
	void *data;
#ifdef BMAC_DEBUG
	printf("bmac_rint() called\n");
#endif

	i = sc->sc_rxlast;
	for (n = 0; n < BMAC_RXBUFS; n++, i++) {
		if (i == BMAC_RXBUFS)
			i = 0;
		cmd = &sc->sc_rxcmd[i];
		status = dbdma_ld16(&cmd->d_status);
		resid = dbdma_ld16(&cmd->d_resid);

#ifdef BMAC_DEBUG
		if (status != 0 && status != 0x8440 && status != 0x9440)
			printf("bmac_rint status = 0x%x\n", status);
#endif

		if ((status & DBDMA_CNTRL_ACTIVE) == 0)	/* 0x9440 | 0x8440 */
			continue;
		count = dbdma_ld16(&cmd->d_count);
		datalen = count - resid;		/* 2 == framelen */
		if (datalen < sizeof(struct ether_header)) {
			printf("%s: short packet len = %d\n",
				ifp->if_xname, datalen);
			goto next;
		}
		DBDMA_BUILD_CMD(cmd, DBDMA_CMD_STOP, 0, 0, 0, 0);
		data = sc->sc_rxbuf + BMAC_BUFLEN * i;

		/* XXX Sometimes bmac reads one extra byte. */
		if (datalen == ETHER_MAX_LEN + 1)
			datalen--;

		/* Trim the CRC. */
		datalen -= ETHER_CRC_LEN;

		m = bmac_get(sc, data, datalen);
		if (m == NULL) {
			ifp->if_ierrors++;
			goto next;
		}

		ml_enqueue(&ml, m);

next:
		DBDMA_BUILD_CMD(cmd, DBDMA_CMD_IN_LAST, 0, DBDMA_INT_ALWAYS,
			DBDMA_WAIT_NEVER, DBDMA_BRANCH_NEVER);

		cmd->d_status = 0;
		cmd->d_resid = 0;
		sc->sc_rxlast = i + 1;
	}
	bmac_mediachange(ifp);

	dbdma_continue(sc->sc_rxdma);

	if_input(ifp, &ml);
	return (1);
}

void
bmac_reset(struct bmac_softc *sc)
{
	int s;

	s = splnet();
	bmac_init(sc);
	splx(s);
}

void
bmac_stop(struct bmac_softc *sc)
{
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int s;

	s = splnet();

	/* timeout */
	timeout_del(&sc->sc_tick_ch);
	mii_down(&sc->sc_mii);

	/* Disable TX/RX. */
	bmac_reset_bits(sc, TXCFG, TxMACEnable);
	bmac_reset_bits(sc, RXCFG, RxMACEnable);

	/* Disable all interrupts. */
	bmac_write_reg(sc, INTDISABLE, NoEventsMask);

	dbdma_stop(sc->sc_txdma);
	dbdma_stop(sc->sc_rxdma);

	ifp->if_flags &= ~IFF_RUNNING;
	ifp->if_timer = 0;

	splx(s);
}

void
bmac_start(struct ifnet *ifp)
{
	struct bmac_softc *sc = ifp->if_softc;
	struct mbuf *m;
	int tlen;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	while (1) {
		if (ifq_is_oactive(&ifp->if_snd))
			return;

		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;
#if NBPFILTER > 0
		/*
		 * If BPF is listening on this interface, let it see the
		 * packet before we commit it to the wire.
		 */
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		ifq_set_oactive(&ifp->if_snd);
		tlen = bmac_put(sc, sc->sc_txbuf, m);

		/* 5 seconds to watch for failing to transmit */
		ifp->if_timer = 5;

		bmac_transmit_packet(sc, sc->sc_txbuf_pa, tlen);
	}
}

void
bmac_transmit_packet(struct bmac_softc *sc, paddr_t pa, int len)
{
	dbdma_command_t *cmd = sc->sc_txcmd;

	DBDMA_BUILD(cmd, DBDMA_CMD_OUT_LAST, 0, len, pa,
		DBDMA_INT_NEVER, DBDMA_WAIT_NEVER, DBDMA_BRANCH_NEVER);
	cmd++;
	DBDMA_BUILD(cmd, DBDMA_CMD_STOP, 0, 0, 0,
		DBDMA_INT_ALWAYS, DBDMA_WAIT_NEVER, DBDMA_BRANCH_NEVER);

	dbdma_start(sc->sc_txdma, sc->sc_txdbdma);
}

int
bmac_put(struct bmac_softc *sc, caddr_t buff, struct mbuf *m)
{
	struct mbuf *n;
	int len, tlen = 0;

	for (; m; m = n) {
		len = m->m_len;
		if (len == 0) {
			n = m_free(m);
			continue;
		}
		bcopy(mtod(m, caddr_t), buff, len);
		buff += len;
		tlen += len;
		n = m_free(m);
	}
	if (tlen > NBPG)
		panic("%s: putpacket packet overflow", sc->sc_dev.dv_xname);

	return (tlen);
}

struct mbuf *
bmac_get(struct bmac_softc *sc, caddr_t pkt, int totlen)
{
	struct mbuf *m;
	struct mbuf *top, **mp;
	int len;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (0);
	m->m_pkthdr.len = totlen;
	len = MHLEN;
	top = 0;
	mp = &top;

	while (totlen > 0) {
		if (top) {
			MGET(m, M_DONTWAIT, MT_DATA);
			if (m == NULL) {
				m_freem(top);
				return (0);
			}
			len = MLEN;
		}
		if (totlen >= MINCLSIZE) {
			MCLGET(m, M_DONTWAIT);
			if ((m->m_flags & M_EXT) == 0) {
				m_free(m);
				m_freem(top);
				return (0);
			}
			len = MCLBYTES;
		}
		m->m_len = len = min(totlen, len);
		bcopy(pkt, mtod(m, caddr_t), len);
		pkt += len;
		totlen -= len;
		*mp = m;
		mp = &m->m_next;
	}

	return (top);
}

void
bmac_watchdog(struct ifnet *ifp)
{
	struct bmac_softc *sc = ifp->if_softc;

	bmac_reset_bits(sc, RXCFG, RxMACEnable);
	bmac_reset_bits(sc, TXCFG, TxMACEnable);

	printf("%s: device timeout\n", ifp->if_xname);
	ifp->if_oerrors++;

	bmac_reset(sc);
}

int
bmac_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct bmac_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		bmac_init(sc);
		break;

	case SIOCSIFFLAGS:
		if ((ifp->if_flags & IFF_UP) == 0 &&
		    (ifp->if_flags & IFF_RUNNING) != 0) {
			/*
			 * If interface is marked down and it is running, then
			 * stop it.
			 */
			bmac_stop(sc);
			ifp->if_flags &= ~IFF_RUNNING;
		} else if ((ifp->if_flags & IFF_UP) != 0 &&
		    (ifp->if_flags & IFF_RUNNING) == 0) {
			/*
			 * If interface is marked up and it is stopped, then
			 * start it.
			 */
			bmac_init(sc);
		} else {
			/*
			 * Reset the interface to pick up changes in any other
			 * flags that affect hardware registers.
			 */
			/*bmac_stop(sc);*/
			bmac_init(sc);
		}
#ifdef BMAC_DEBUG
		if (ifp->if_flags & IFF_DEBUG)
			sc->sc_debug = 1;
		else
			sc->sc_debug = 0;
#endif
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING) {
			bmac_init(sc);
			bmac_setladrf(sc);
		}
		error = 0;
	}

	splx(s);
	return (error);
}

int
bmac_mediachange(struct ifnet *ifp)
{
	struct bmac_softc *sc = ifp->if_softc;

	return mii_mediachg(&sc->sc_mii);
}

void
bmac_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct bmac_softc *sc = ifp->if_softc;

	mii_pollstat(&sc->sc_mii);

	ifmr->ifm_status = sc->sc_mii.mii_media_status;
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
}

/*
 * Set up the logical address filter.
 */
void
bmac_setladrf(struct bmac_softc *sc)
{
	struct arpcom *ac = &sc->arpcom;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	u_int32_t crc;
	u_int16_t hash[4];
	int x;

	/*
	 * Set up multicast address filter by passing all multicast addresses
	 * through a crc generator, and then using the high order 6 bits as an
	 * index into the 64 bit logical address filter.  The high order bit
	 * selects the word, while the rest of the bits select the bit within
	 * the word.
	 */

	if (ifp->if_flags & IFF_PROMISC) {
		bmac_set_bits(sc, RXCFG, RxPromiscEnable);
		return;
	}

	if (ac->ac_multirangecnt > 0)
		ifp->if_flags |= IFF_ALLMULTI;

	if (ifp->if_flags & IFF_ALLMULTI) {
		hash[3] = hash[2] = hash[1] = hash[0] = 0xffff;
		goto chipit;
	}

	hash[3] = hash[2] = hash[1] = hash[0] = 0;
	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		crc = ether_crc32_le(enm->enm_addrlo, ETHER_ADDR_LEN);

		/* Just want the 6 most significant bits. */
		crc >>= 26;

		/* Set the corresponding bit in the filter. */
		hash[crc >> 4] |= 1 << (crc & 0xf);

		ETHER_NEXT_MULTI(step, enm);
	}

	ifp->if_flags &= ~IFF_ALLMULTI;

chipit:
	bmac_write_reg(sc, HASH0, hash[0]);
	bmac_write_reg(sc, HASH1, hash[1]);
	bmac_write_reg(sc, HASH2, hash[2]);
	bmac_write_reg(sc, HASH3, hash[3]);
	x = bmac_read_reg(sc, RXCFG);
	x &= ~RxPromiscEnable;
	x |= RxHashFilterEnable;
	bmac_write_reg(sc, RXCFG, x);
}

int
bmac_mii_readreg(struct device *dev, int phy, int reg)
{
	return mii_bitbang_readreg(dev, &bmac_mbo, phy, reg);
}

void
bmac_mii_writereg(struct device *dev, int phy, int reg, int val)
{
	mii_bitbang_writereg(dev, &bmac_mbo, phy, reg, val);
}

u_int32_t
bmac_mbo_read(struct device *dev)
{
	struct bmac_softc *sc = (void *)dev;

	return bmac_read_reg(sc, MIFCSR);
}

void
bmac_mbo_write(struct device *dev, u_int32_t val)
{
	struct bmac_softc *sc = (void *)dev;

	bmac_write_reg(sc, MIFCSR, val);
}

void
bmac_mii_statchg(struct device *dev)
{
	struct bmac_softc *sc = (void *)dev;
	int x;

	/* Update duplex mode in TX configuration */
	x = bmac_read_reg(sc, TXCFG);
	if ((IFM_OPTIONS(sc->sc_mii.mii_media_active) & IFM_FDX) != 0)
		x |= TxFullDuplex;
	else
		x &= ~TxFullDuplex;
	bmac_write_reg(sc, TXCFG, x);

#ifdef BMAC_DEBUG
	printf("bmac_mii_statchg 0x%x\n",
		IFM_OPTIONS(sc->sc_mii.mii_media_active));
#endif
}

void
bmac_mii_tick(void *v)
{
	struct bmac_softc *sc = v;
	int s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	splx(s);

	timeout_add_sec(&sc->sc_tick_ch, 1);
}
