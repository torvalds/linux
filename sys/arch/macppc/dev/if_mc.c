/*	$OpenBSD: if_mc.c,v 1.35 2024/09/06 10:54:08 jsg Exp $	*/
/*	$NetBSD: if_mc.c,v 1.9.16.1 2006/06/21 14:53:13 yamt Exp $	*/

/*-
 * Copyright (c) 1997 David Huang <khym@bga.com>
 * All rights reserved.
 *
 * Portions of this code are based on code by Denton Gentry <denny1@home.com>
 * and Yanagisawa Takeshi <yanagisw@aa.ap.titech.ac.jp>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * AMD AM79C940 (MACE) driver with DBDMA bus attachment and DMA routines 
 * for onboard ethernet found on most old world macs. 
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/buf.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include "bpfilter.h"
#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <dev/ofw/openfirm.h>
#include <machine/pio.h>
#include <machine/bus.h>
#include <machine/autoconf.h>

#include <macppc/dev/dbdma.h>

#define MC_REGSPACING   	16
#define MC_REGSIZE      	MACE_NREGS * MC_REGSPACING
#define MACE_REG(x)     	((x)*MC_REGSPACING)
#define MACE_BUFLEN		2048
#define MACE_TXBUFS		2
#define MACE_RXBUFS		8

#define MC_RXDMABUFS		4

#define MACE_BUFSZ       	((MACE_RXBUFS + MACE_TXBUFS + 2) * MACE_BUFLEN)

#define NIC_GET(sc, reg)	(in8rb(sc->sc_reg + MACE_REG(reg)))

#define NIC_PUT(sc, reg, val)   (out8rb(sc->sc_reg + MACE_REG(reg), (val)))

/*
 * AMD MACE (Am79C940) register definitions
 */
#define	MACE_RCVFIFO		0   /* Receive FIFO [15-00] (read only) */
#define	MACE_XMTFIFO		1   /* Transmit FIFO [15-00] (write only) */
#define	MACE_XMTFC		2   /* Transmit Frame Control (read/write) */
#define	MACE_XMTFS		3   /* Transmit Frame Status (read only) */
#define	MACE_XMTRC		4   /* Transmit Retry Count (read only) */
#define	MACE_RCVFC		5   /* Receive Frame Control (read/write) */
#define	MACE_RCVFS		6   /* Receive Frame Status (4 bytes) (read only) */
#define	MACE_FIFOFC		7   /* FIFO Frame Count (read only) */
#define	MACE_IR			8   /* Interrupt Register (read only) */
#define	MACE_IMR		9   /* Interrupt Mask Register (read/write) */
#define	MACE_PR			10  /* Poll Register (read only) */
#define	MACE_BIUCC		11  /* BIU Configuration Control (read/write) */
#define	MACE_FIFOCC		12  /* FIFO Configuration Control (read/write) */
#define	MACE_MACCC		13  /* MAC Configuration Control (read/write) */
#define	MACE_PLSCC		14  /* PLS Configuration Control (read/write) */
#define	MACE_PHYCC		15  /* PHY Configuration Control (read/write) */
#define	MACE_CHIPIDL		16  /* Chip ID Register [07-00] (read only) */
#define	MACE_CHIPIDH		17  /* Chip ID Register [15-08] (read only) */
#define	MACE_IAC		18  /* Internal Address Configuration (read/write) */
/*	RESERVED		19     Reserved (read/write as 0) */
#define	MACE_LADRF		20  /* Logical Address Filter (8 bytes) (read/write) */
#define	MACE_PADR		21  /* Physical Address (6 bytes) (read/write) */
/*	RESERVED		22     Reserved (read/write as 0) */
/*	RESERVED		23     Reserved (read/write as 0) */
#define	MACE_MPC		24  /* Missed Packet Count (read only) */
/*	RESERVED		25     Reserved (read/write as 0) */
#define	MACE_RNTPC		26  /* Runt Packet Count (read only) */
#define	MACE_RCVCC		27  /* Receive Collision Count (read only) */
/*	RESERVED		28     Reserved (read/write as 0) */
#define	MACE_UTR		29  /* User Test Register (read/write) */
#define	MACE_RTR1		30  /* Reserved Test Register 1 (read/write as 0) */
#define	MACE_RTR2		31  /* Reserved Test Register 2 (read/write as 0) */

#define	MACE_NREGS		32

/* 2: Transmit Frame Control (XMTFC) */
#define	DRTRY			0x80	/* Disable Retry */
#define	DXMTFCS			0x08	/* Disable Transmit FCS */
#define	APADXMT			0x01	/* Auto Pad Transmit */

/* 3: Transmit Frame Status (XMTFS) */
#define	XMTSV			0x80	/* Transmit Status Valid */
#define	UFLO			0x40	/* Underflow */
#define	LCOL			0x20	/* Late Collision */
#define	MORE			0x10	/* More than one retry needed */
#define	ONE			0x08	/* Exactly one retry needed */
#define	DEFER			0x04	/* Transmission deferred */
#define	LCAR			0x02	/* Loss of Carrier */
#define	RTRY			0x01	/* Retry Error */

/* 4: Transmit Retry Count (XMTRC) */
#define	EXDEF			0x80	/* Excessive Defer */
#define	XMTRC			0x0f	/* Transmit Retry Count */

/* 5: Receive Frame Control (RCVFC) */
#define	LLRCV			0x08	/* Low Latency Receive */
#define	MR			0x04	/* Match/Reject */
#define	ASTRPRCV		0x01	/* Auto Strip Receive */

/* 6: Receive Frame Status (RCVFS) */
/* 4 byte register; read 4 times to get all of the bytes */
/* Read 1: RFS0 - Receive Message Byte Count [7-0] (RCVCNT) */

/* Read 2: RFS1 - Receive Status (RCVSTS) */
#define	OFLO			0x80	/* Overflow flag */
#define	CLSN			0x40	/* Collision flag */
#define	FRAM			0x20	/* Framing Error flag */
#define	FCS			0x10	/* FCS Error flag */
#define	RCVCNT			0x0f	/* Receive Message Byte Count [11-8] */

/* Read 3: RFS2 - Runt Packet Count (RNTPC) [7-0] */

/* Read 4: RFS3 - Receive Collision Count (RCVCC) [7-0] */

/* 7: FIFO Frame Count (FIFOFC) */
#define	RCVFC			0xf0	/* Receive Frame Count */
#define	XMTFC			0x0f	/* Transmit Frame Count */

/* 8: Interrupt Register (IR) */
#define	JAB			0x80	/* Jabber Error */
#define	BABL			0x40	/* Babble Error */
#define	CERR			0x20	/* Collision Error */
#define	RCVCCO			0x10	/* Receive Collision Count Overflow */
#define	RNTPCO			0x08	/* Runt Packet Count Overflow */
#define	MPCO			0x04	/* Missed Packet Count Overflow */
#define	RCVINT			0x02	/* Receive Interrupt */
#define	XMTINT			0x01	/* Transmit Interrupt */

/* 9: Interrupt Mask Register (IMR) */
#define	JABM			0x80	/* Jabber Error Mask */
#define	BABLM			0x40	/* Babble Error Mask */
#define	CERRM			0x20	/* Collision Error Mask */
#define	RCVCCOM			0x10	/* Receive Collision Count Overflow Mask */
#define	RNTPCOM			0x08	/* Runt Packet Count Overflow Mask */
#define	MPCOM			0x04	/* Missed Packet Count Overflow Mask */
#define	RCVINTM			0x02	/* Receive Interrupt Mask */
#define	XMTINTM			0x01	/* Transmit Interrupt Mask */

/* 10: Poll Register (PR) */
#define	XMTSV			0x80	/* Transmit Status Valid */
#define	TDTREQ			0x40	/* Transmit Data Transfer Request */
#define	RDTREQ			0x20	/* Receive Data Transfer Request */

/* 11: BIU Configuration Control (BIUCC) */
#define	BSWP			0x40	/* Byte Swap */
#define	XMTSP			0x30	/* Transmit Start Point */
#define	XMTSP_4			0x00	/* 4 bytes */
#define	XMTSP_16		0x10	/* 16 bytes */
#define	XMTSP_64		0x20	/* 64 bytes */
#define	XMTSP_112		0x30	/* 112 bytes */
#define	SWRST			0x01	/* Software Reset */

/* 12: FIFO Configuration Control (FIFOCC) */
#define	XMTFW			0xc0	/* Transmit FIFO Watermark */
#define	XMTFW_8			0x00	/* 8 write cycles */
#define	XMTFW_16		0x40	/* 16 write cycles */
#define	XMTFW_32		0x80	/* 32 write cycles */
#define	RCVFW			0x30	/* Receive FIFO Watermark */
#define	RCVFW_16		0x00	/* 16 bytes */
#define	RCVFW_32		0x10	/* 32 bytes */
#define	RCVFW_64		0x20	/* 64 bytes */
#define	XMTFWU			0x08	/* Transmit FIFO Watermark Update */
#define	RCVFWU			0x04	/* Receive FIFO Watermark Update */
#define	XMTBRST			0x02	/* Transmit Burst */
#define	RCVBRST			0x01	/* Receive Burst */

/* 13: MAC Configuration (MACCC) */
#define	PROM			0x80	/* Promiscuous */
#define	DXMT2PD			0x40	/* Disable Transmit Two Part Deferral */
#define	EMBA			0x20	/* Enable Modified Back-off Algorithm */
#define	DRCVPA			0x08	/* Disable Receive Physical Address */
#define	DRCVBC			0x04	/* Disable Receive Broadcast */
#define	ENXMT			0x02	/* Enable Transmit */
#define	ENRCV			0x01	/* Enable Receive */

/* 14: PLS Configuration Control (PLSCC) */
#define	XMTSEL			0x08	/* Transmit Mode Select */
#define	PORTSEL			0x06	/* Port Select */
#define	PORTSEL_AUI		0x00	/* Select AUI */
#define	PORTSEL_10BT		0x02	/* Select 10BASE-T */
#define	PORTSEL_DAI		0x04	/* Select DAI port */
#define	PORTSEL_GPSI		0x06	/* Select GPSI */
#define	ENPLSIO			0x01	/* Enable PLS I/O */

/* 15: PHY Configuration (PHYCC) */
#define	LNKFL			0x80	/* Link Fail */
#define	DLNKTST			0x40	/* Disable Link Test */
#define	REVPOL			0x20	/* Reversed Polarity */
#define	DAPC			0x10	/* Disable Auto Polarity Correction */
#define	LRT			0x08	/* Low Receive Threshold */
#define	ASEL			0x04	/* Auto Select */
#define	RWAKE			0x02	/* Remote Wake */
#define	AWAKE			0x01	/* Auto Wake */

/* 18: Internal Address Configuration (IAC) */
#define	ADDRCHG			0x80	/* Address Change */
#define	PHYADDR			0x04	/* Physical Address Reset */
#define	LOGADDR			0x02	/* Logical Address Reset */

/* 28: User Test Register (UTR) */
#define	RTRE			0x80	/* Reserved Test Register Enable */
#define	RTRD			0x40	/* Reserved Test Register Disable */
#define	RPA			0x20	/* Run Packet Accept */
#define	FCOLL			0x10	/* Force Collision */
#define	RCVFCSE			0x08	/* Receive FCS Enable */
#define	LOOP			0x06	/* Loopback Control */
#define	LOOP_NONE		0x00	/* No Loopback */
#define	LOOP_EXT		0x02	/* External Loopback */
#define	LOOP_INT		0x04	/* Internal Loopback, excludes MENDEC */
#define	LOOP_INT_MENDEC		0x06	/* Internal Loopback, includes MENDEC */

struct mc_rxframe {
	u_int8_t		rx_rcvcnt;
	u_int8_t		rx_rcvsts;
	u_int8_t		rx_rntpc;
	u_int8_t		rx_rcvcc;
	u_char			*rx_frame;
};

struct mc_softc {
	struct device	   	sc_dev;		/* base device glue */
	struct arpcom	   	sc_arpcom;	/* Ethernet common part */
	struct timeout	  	sc_tick_ch;

	struct mc_rxframe       sc_rxframe;
	u_int8_t		sc_biucc;
	u_int8_t		sc_fifocc;
	u_int8_t		sc_plscc;
	u_int8_t		sc_enaddr[6];
	u_int8_t		sc_pad[2];
	int			sc_havecarrier; /* carrier status */

	char			*sc_reg;
	bus_dma_tag_t		sc_dmat;
	bus_dmamap_t		sc_bufmap;
	bus_dma_segment_t       sc_bufseg[1];

	dbdma_regmap_t		*sc_txdma;
	dbdma_regmap_t		*sc_rxdma;
	dbdma_command_t		*sc_txdmacmd;
	dbdma_command_t		*sc_rxdmacmd;
	dbdma_t			sc_txdbdma;
	dbdma_t			sc_rxdbdma;

	caddr_t			sc_txbuf;
	caddr_t			sc_rxbuf;
	paddr_t			sc_txbuf_pa;
	paddr_t			sc_rxbuf_pa;
	int			sc_tail;
	int			sc_rxset;
	int			sc_txset;
	int			sc_txseti;
};

int	mc_match(struct device *, void *, void *);
void	mc_attach(struct device *, struct device *, void *);

const struct cfattach mc_ca = {
	sizeof(struct mc_softc), mc_match, mc_attach
};

struct cfdriver mc_cd = {
	NULL, "mc", DV_IFNET
};

void	mc_init(struct mc_softc *sc);
int	mc_dmaintr(void *arg);
void	mc_reset_rxdma(struct mc_softc *sc);
void	mc_reset_txdma(struct mc_softc *sc);
int     mc_stop(struct mc_softc *sc);
int     mc_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data);
void    mc_start(struct ifnet *ifp);
void    mc_reset(struct mc_softc *sc);
void    mc_tint(struct mc_softc *sc);
void	mc_rint(struct mc_softc *sc);
int	mc_intr(void *);
void	mc_watchdog(struct ifnet *ifp);

u_int   maceput(struct mc_softc *sc, struct mbuf *);
void    mace_read(struct mc_softc *, caddr_t, int);
struct mbuf *mace_get(struct mc_softc *, caddr_t, int);
static void mace_calcladrf(struct mc_softc *, u_int8_t *);
void	mc_putpacket(struct mc_softc *, u_int);

int
mc_match(struct device *parent, void *arg, void *aux)
{
	struct confargs *ca = aux;

	if (strcmp(ca->ca_name, "mace") != 0)
		return 0;

	/* requires 6 regs */
	if (ca->ca_nreg / sizeof(int) != 6)
		return 0;

	/* requires 3 intrs */
	if (ca->ca_nintr / sizeof(int) != 3)
		return 0;

	return 1;
}

void
mc_attach(struct device *parent, struct device *self, void *aux)
{
	struct confargs *ca = aux;
	struct mc_softc *sc = (struct mc_softc *)self;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	u_int8_t lladdr[ETHER_ADDR_LEN];
	int nseg, error;

	if (OF_getprop(ca->ca_node, "local-mac-address", lladdr,
	    ETHER_ADDR_LEN) != ETHER_ADDR_LEN) {
		printf(": failed to get MAC address.\n");
		return;
	}

	ca->ca_reg[0] += ca->ca_baseaddr;
	ca->ca_reg[2] += ca->ca_baseaddr;
	ca->ca_reg[4] += ca->ca_baseaddr;

	if ((sc->sc_reg = mapiodev(ca->ca_reg[0], ca->ca_reg[1])) == NULL) {
		printf(": cannot map registers\n");
		return;
	}

	sc->sc_dmat = ca->ca_dmat;
	sc->sc_tail = 0;

	if ((sc->sc_txdma = mapiodev(ca->ca_reg[2], ca->ca_reg[3])) == NULL) {
		printf(": cannot map TX DMA registers\n");
		goto notxdma;
	}
	if ((sc->sc_rxdma = mapiodev(ca->ca_reg[4], ca->ca_reg[5])) == NULL) {
		printf(": cannot map RX DMA registers\n");
		goto norxdma;
	}
	if ((sc->sc_txdbdma = dbdma_alloc(sc->sc_dmat, 2)) == NULL) {
		printf(": cannot alloc TX DMA descriptors\n");
		goto notxdbdma;
	}
	sc->sc_txdmacmd = sc->sc_txdbdma->d_addr;

	if ((sc->sc_rxdbdma = dbdma_alloc(sc->sc_dmat, 8 + 1)) == NULL) {
		printf(": cannot alloc RX DMA descriptors\n");
		goto norxdbdma;
	}
	sc->sc_rxdmacmd = sc->sc_rxdbdma->d_addr;

	if ((error = bus_dmamem_alloc(sc->sc_dmat, MACE_BUFSZ, PAGE_SIZE, 0,
	    sc->sc_bufseg, 1, &nseg, BUS_DMA_NOWAIT))) {
		printf(": cannot allocate DMA mem (%d)\n", error);
		goto nodmamem;
	}

	if ((error = bus_dmamem_map(sc->sc_dmat, sc->sc_bufseg, nseg,
	    MACE_BUFSZ, &sc->sc_txbuf, BUS_DMA_NOWAIT))) {
		printf(": cannot map DMA mem (%d)\n", error);
		goto nodmamap;
	}

	if ((error = bus_dmamap_create(sc->sc_dmat, MACE_BUFSZ, 1, MACE_BUFSZ,
	    0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &sc->sc_bufmap))) {
		printf(": cannot create DMA map (%d)\n", error);
		goto nodmacreate;
	}

	if ((error = bus_dmamap_load(sc->sc_dmat, sc->sc_bufmap, sc->sc_txbuf,
	    MACE_BUFSZ, NULL, BUS_DMA_NOWAIT))) {
		printf(": cannot load DMA map (%d)\n", error);
		goto nodmaload;
	}

	sc->sc_txbuf_pa = sc->sc_bufmap->dm_segs->ds_addr;
	sc->sc_rxbuf = sc->sc_txbuf + MACE_BUFLEN * MACE_TXBUFS;
	sc->sc_rxbuf_pa = sc->sc_txbuf_pa + MACE_BUFLEN * MACE_TXBUFS;

	printf(": irq %d,%d,%d", ca->ca_intr[0], ca->ca_intr[1],
	    ca->ca_intr[2]);

	/* disable receive DMA */
	dbdma_reset(sc->sc_rxdma);

	/* disable transmit DMA */
	dbdma_reset(sc->sc_txdma);

	/* install interrupt handlers */
	mac_intr_establish(parent, ca->ca_intr[2], IST_LEVEL, IPL_NET,
	    mc_dmaintr, sc, sc->sc_dev.dv_xname);
	mac_intr_establish(parent, ca->ca_intr[0],  IST_LEVEL, IPL_NET,
	    mc_intr, sc, sc->sc_dev.dv_xname);

	sc->sc_biucc = XMTSP_64;
	sc->sc_fifocc = XMTFW_16 | RCVFW_64 | XMTFWU | RCVFWU |
	    XMTBRST | RCVBRST;
	sc->sc_plscc = PORTSEL_GPSI | ENPLSIO;

	/* reset the chip and disable all interrupts */
	NIC_PUT(sc, MACE_BIUCC, SWRST);
	DELAY(100);

	NIC_PUT(sc, MACE_IMR, ~0);

	bcopy(lladdr, sc->sc_enaddr, ETHER_ADDR_LEN);
	bcopy(sc->sc_enaddr, sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN);
	printf(": address %s\n", ether_sprintf(lladdr));

	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_ioctl = mc_ioctl;
	ifp->if_start = mc_start;
	ifp->if_flags =
		IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_watchdog = mc_watchdog;
	ifp->if_timer = 0;

	if_attach(ifp);
	ether_ifattach(ifp);

	return;
nodmaload:
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_bufmap);
nodmacreate:
	bus_dmamem_unmap(sc->sc_dmat, sc->sc_txbuf, MACE_BUFSZ);
nodmamap:
	bus_dmamem_free(sc->sc_dmat, sc->sc_bufseg, 1);
nodmamem:
	dbdma_free(sc->sc_rxdbdma);
norxdbdma:
	dbdma_free(sc->sc_txdbdma);
notxdbdma:
	unmapiodev((void *)sc->sc_rxdma, ca->ca_reg[5]);
norxdma:
	unmapiodev((void *)sc->sc_txdma, ca->ca_reg[3]);
notxdma:
	unmapiodev(sc->sc_reg, ca->ca_reg[1]);
}

int
mc_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct mc_softc *sc = ifp->if_softc;
	int s, err = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			mc_init(sc);
		break;

	case SIOCSIFFLAGS:
		if ((ifp->if_flags & IFF_UP) == 0 &&
		    (ifp->if_flags & IFF_RUNNING) != 0) {
			/*
			 * If interface is marked down and it is running,
			 * then stop it.
			 */
			mc_stop(sc);
		} else if ((ifp->if_flags & IFF_UP) != 0 &&
		    (ifp->if_flags & IFF_RUNNING) == 0) {
			/*
			 * If interface is marked up and it is stopped,
			 * then start it.
			 */
			mc_init(sc);
		} else {
			/*
			 * reset the interface to pick up any other changes
			 * in flags
			 */
			mc_reset(sc);
			mc_start(ifp);
		}
		break;

	default:
		err = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (err == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			mc_reset(sc);
		err = 0;
	}

	splx(s);
	return (err);
}

/*
 * Encapsulate a packet of type family for the local net.
 */
void
mc_start(struct ifnet *ifp)
{
	struct mc_softc	*sc = ifp->if_softc;
	struct mbuf	*m;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	while (1) {
		if (ifq_is_oactive(&ifp->if_snd))
			return;

		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			return;

#if NBPFILTER > 0
		/*
		 * If bpf is listening on this interface, let it
		 * see the packet before we commit it to the wire.
		 */
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		/*
		 * Copy the mbuf chain into the transmit buffer.
		 */
		ifq_set_oactive(&ifp->if_snd);
		maceput(sc, m);
	}
}

/*
 * reset and restart the MACE.  Called in case of fatal
 * hardware/software errors.
 */
void
mc_reset(struct mc_softc *sc)
{
	mc_stop(sc);
	mc_init(sc);
}

void
mc_init(struct mc_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	u_int8_t maccc, ladrf[8];
	int s, i;

	s = splnet();

	NIC_PUT(sc, MACE_BIUCC, sc->sc_biucc);
	NIC_PUT(sc, MACE_FIFOCC, sc->sc_fifocc);
	NIC_PUT(sc, MACE_IMR, ~0); /* disable all interrupts */
	NIC_PUT(sc, MACE_PLSCC, sc->sc_plscc);

	NIC_PUT(sc, MACE_UTR, RTRD); /* disable reserved test registers */

	/* set MAC address */
	NIC_PUT(sc, MACE_IAC, ADDRCHG);
	while (NIC_GET(sc, MACE_IAC) & ADDRCHG)
		;
	NIC_PUT(sc, MACE_IAC, PHYADDR);
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		out8rb(sc->sc_reg + MACE_REG(MACE_PADR) + i,
		    sc->sc_enaddr[i]);

	/* set logical address filter */
	mace_calcladrf(sc, ladrf);

	NIC_PUT(sc, MACE_IAC, ADDRCHG);
	while (NIC_GET(sc, MACE_IAC) & ADDRCHG)
		;
	NIC_PUT(sc, MACE_IAC, LOGADDR);
	for (i = 0; i < 8; i++)
		out8rb(sc->sc_reg + MACE_REG(MACE_LADRF) + i,
		    ladrf[i]);

	NIC_PUT(sc, MACE_XMTFC, APADXMT);
	/*
	* No need to autostrip padding on receive... Ethernet frames
	* don't have a length field, unlike 802.3 frames, so the MACE
	* can't figure out the length of the packet anyways.
	*/
	NIC_PUT(sc, MACE_RCVFC, 0);

	maccc = ENXMT | ENRCV;
	if (ifp->if_flags & IFF_PROMISC)
		maccc |= PROM;

	NIC_PUT(sc, MACE_MACCC, maccc);

	mc_reset_rxdma(sc);
	mc_reset_txdma(sc);
	/*
	* Enable all interrupts except receive, since we use the DMA
	* completion interrupt for that.
	*/
	NIC_PUT(sc, MACE_IMR, RCVINTM);

	/* flag interface as "running" */
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);
}

/*
 * Close down an interface and free its buffers.
 * Called on final close of device, or if mcinit() fails
 * part way through.
 */
int
mc_stop(struct mc_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int s;

	s = splnet();

	NIC_PUT(sc, MACE_BIUCC, SWRST);
	DELAY(100);

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);
	return (0);
}

/*
 * Called if any Tx packets remain unsent after 5 seconds,
 * In all cases we just reset the chip, and any retransmission
 * will be handled by higher level protocol timeouts.
 */
void
mc_watchdog(struct ifnet *ifp)
{
	struct mc_softc *sc = ifp->if_softc;

	printf("mcwatchdog: resetting chip\n");
	mc_reset(sc);
}

int
mc_intr(void *arg)
{
	struct mc_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	u_int8_t ir;

	ir = NIC_GET(sc, MACE_IR) & ~NIC_GET(sc, MACE_IMR);

	if (ir & JAB) {
#ifdef MCDEBUG
		printf("%s: jabber error\n", sc->sc_dev.dv_xname);
#endif
		ifp->if_oerrors++;
	}

	if (ir & BABL) {
#ifdef MCDEBUG
		printf("%s: babble\n", sc->sc_dev.dv_xname);
#endif
		ifp->if_oerrors++;
	 }

	if (ir & CERR) {
#ifdef MCDEBUG
		printf("%s: collision error\n", sc->sc_dev.dv_xname);
#endif
		ifp->if_collisions++;
	 }

	/*
	 * Pretend we have carrier; if we don't this will be cleared
	 * shortly.
	 */
	sc->sc_havecarrier = 1;

	if (ir & XMTINT)
		mc_tint(sc);

	if (ir & RCVINT)
		mc_rint(sc);

	return(1);
}

void
mc_tint(struct mc_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	u_int8_t xmtrc, xmtfs;

	xmtrc = NIC_GET(sc, MACE_XMTRC);
	xmtfs = NIC_GET(sc, MACE_XMTFS);

	if ((xmtfs & XMTSV) == 0)
		return;

	if (xmtfs & UFLO) {
		printf("%s: underflow\n", sc->sc_dev.dv_xname);
		mc_reset(sc);
		return;
	}

	if (xmtfs & LCOL) {
		printf("%s: late collision\n", sc->sc_dev.dv_xname);
		ifp->if_oerrors++;
		ifp->if_collisions++;
	}

	if (xmtfs & MORE)
		/* Real number is unknown. */
		ifp->if_collisions += 2;
	else if (xmtfs & ONE)
		ifp->if_collisions++;
	else if (xmtfs & RTRY) {
		printf("%s: excessive collisions\n", sc->sc_dev.dv_xname);
		ifp->if_collisions += 16;
		ifp->if_oerrors++;
	}

	if (xmtfs & LCAR) {
		sc->sc_havecarrier = 0;
		printf("%s: lost carrier\n", sc->sc_dev.dv_xname);
		ifp->if_oerrors++;
	}

	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;
	mc_start(ifp);
}

void
mc_rint(struct mc_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
#define rxf	sc->sc_rxframe
	u_int len;

	len = (rxf.rx_rcvcnt | ((rxf.rx_rcvsts & 0xf) << 8)) - 4;

#ifdef MCDEBUG
	if (rxf.rx_rcvsts & 0xf0)
		printf("%s: rcvcnt %02x rcvsts %02x rntpc 0x%02x rcvcc 0x%02x\n",
		    sc->sc_dev.dv_xname, rxf.rx_rcvcnt, rxf.rx_rcvsts,
		    rxf.rx_rntpc, rxf.rx_rcvcc);
#endif

	if (rxf.rx_rcvsts & OFLO) {
#ifdef MCDEBUG
		printf("%s: receive FIFO overflow\n", sc->sc_dev.dv_xname);
#endif
		ifp->if_ierrors++;
		return;
	}

	if (rxf.rx_rcvsts & CLSN)
		ifp->if_collisions++;

	if (rxf.rx_rcvsts & FRAM) {
#ifdef MCDEBUG
		printf("%s: framing error\n", sc->sc_dev.dv_xname);
#endif
		ifp->if_ierrors++;
		return;
	}

	if (rxf.rx_rcvsts & FCS) {
#ifdef MCDEBUG
		printf("%s: frame control checksum error\n", sc->sc_dev.dv_xname);
#endif
		ifp->if_ierrors++;
		return;
	}

	mace_read(sc, rxf.rx_frame, len);
#undef  rxf
}
/*
 * stuff packet into MACE (at splnet)
 */
u_int
maceput(struct mc_softc *sc, struct mbuf *m)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mbuf *n;
	u_int len, totlen = 0;
	u_char *buff;

	buff = sc->sc_txbuf;

	for (; m; m = n) {
		u_char *data = mtod(m, u_char *);
		len = m->m_len;
		totlen += len;
		bcopy(data, buff, len);
		buff += len;
		n = m_free(m);
	}

	if (totlen > PAGE_SIZE)
		panic("%s: maceput: packet overflow", sc->sc_dev.dv_xname);

#if 0
	if (totlen < ETHERMIN + sizeof(struct ether_header)) {
		int pad = ETHERMIN + sizeof(struct ether_header) - totlen;
		bzero(sc->sc_txbuf + totlen, pad);
		totlen = ETHERMIN + sizeof(struct ether_header);
	}
#endif


	/* 5 seconds to watch for failing to transmit */
	ifp->if_timer = 5;
	mc_putpacket(sc, totlen);
	return (totlen);
}

void
mace_read(struct mc_softc *sc, caddr_t pkt, int len)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;

	if (len <= sizeof(struct ether_header) ||
	    len > ETHERMTU + sizeof(struct ether_header)) {
#ifdef MCDEBUG
		printf("%s: invalid packet size %d; dropping\n",
		    sc->sc_dev.dv_xname, len);
#endif
		ifp->if_ierrors++;
		return;
	}

	m = mace_get(sc, pkt, len);
	if (m == NULL) {
		ifp->if_ierrors++;
		return;
	}

	ml_enqueue(&ml, m);
	if_input(ifp, &ml);
}

/*
 * Pull data off an interface.
 * Len is length of data, with local net header stripped.
 * We copy the data into mbufs.  When full cluster sized units are present
 * we copy into clusters.
 */
struct mbuf *
mace_get(struct mc_softc *sc, caddr_t pkt, int totlen)
{
	 struct mbuf *m;
	 struct mbuf *top, **mp;
	 int len;

	 MGETHDR(m, M_DONTWAIT, MT_DATA);
	 if (m == NULL)
		  return (NULL);

	 m->m_pkthdr.len = totlen;
	 len = MHLEN;
	 top = 0;
	 mp = &top;

	 while (totlen > 0) {
		  if (top) {
			   MGET(m, M_DONTWAIT, MT_DATA);
			   if (m == NULL) {
				    m_freem(top);
				    return (NULL);
			   }
			   len = MLEN;
		  }
		  if (totlen >= MINCLSIZE) {
			   MCLGET(m, M_DONTWAIT);
			   if ((m->m_flags & M_EXT) == 0) {
				    m_free(m);
				    m_freem(top);
				    return (NULL);
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
mc_putpacket(struct mc_softc *sc, u_int len)
{
	dbdma_command_t *cmd = sc->sc_txdmacmd;

	DBDMA_BUILD(cmd, DBDMA_CMD_OUT_LAST, 0, len, sc->sc_txbuf_pa,
	   DBDMA_INT_NEVER, DBDMA_WAIT_NEVER, DBDMA_BRANCH_NEVER);
	cmd++;
	DBDMA_BUILD(cmd, DBDMA_CMD_STOP, 0, 0, 0, DBDMA_INT_ALWAYS,
	   DBDMA_WAIT_NEVER, DBDMA_BRANCH_NEVER);

	dbdma_start(sc->sc_txdma, sc->sc_txdbdma);
}

/*
 * Interrupt handler for the MACE DMA completion interrupts
 */
int
mc_dmaintr(void *arg)
{
	struct mc_softc *sc = arg;
	int status, offset, statoff;
	int datalen, resid;
	int i, n, count;
	dbdma_command_t *cmd;

	/* We've received some packets from the MACE */
	/* Loop through, processing each of the packets */
	i = sc->sc_tail;
	for (n = 0; n < MC_RXDMABUFS; n++, i++) {
		if (i == MC_RXDMABUFS)
			i = 0;

		cmd = &sc->sc_rxdmacmd[i];
		status = dbdma_ld16(&cmd->d_status);
		resid = dbdma_ld16(&cmd->d_resid);

		if ((status & DBDMA_CNTRL_ACTIVE) == 0) {
			continue;
		}

		count = dbdma_ld16(&cmd->d_count);
		datalen = count - resid;
		datalen -= 4; /* 4 == status bytes */

		if (datalen < 4 + sizeof(struct ether_header)) {
			printf("short packet len=%d\n", datalen);
			/* continue; */
			goto next;
		}
		DBDMA_BUILD_CMD(cmd, DBDMA_CMD_STOP, 0, 0, 0, 0);

		offset = i * MACE_BUFLEN;
		statoff = offset + datalen;
		sc->sc_rxframe.rx_rcvcnt = sc->sc_rxbuf[statoff + 0];
		sc->sc_rxframe.rx_rcvsts = sc->sc_rxbuf[statoff + 1];
		sc->sc_rxframe.rx_rntpc  = sc->sc_rxbuf[statoff + 2];
		sc->sc_rxframe.rx_rcvcc  = sc->sc_rxbuf[statoff + 3];
		sc->sc_rxframe.rx_frame  = sc->sc_rxbuf + offset;

		mc_rint(sc);

next:
		DBDMA_BUILD_CMD(cmd, DBDMA_CMD_IN_LAST, 0, DBDMA_INT_ALWAYS,
		    DBDMA_WAIT_NEVER, DBDMA_BRANCH_NEVER);

		cmd->d_status = 0;
		cmd->d_resid = 0;
		sc->sc_tail = i + 1;
	}

	dbdma_continue(sc->sc_rxdma);

	return 1;
}

void
mc_reset_rxdma(struct mc_softc *sc)
{
	dbdma_command_t *cmd = sc->sc_rxdmacmd;
	int i;
	u_int8_t maccc;

	/* Disable receiver, reset the DMA channels */
	maccc = NIC_GET(sc, MACE_MACCC);
	NIC_PUT(sc, MACE_MACCC, maccc & ~ENRCV);

	dbdma_reset(sc->sc_rxdma);

	bzero(sc->sc_rxdmacmd, 8 * sizeof(dbdma_command_t));
	for (i = 0; i < MC_RXDMABUFS; i++) {
		DBDMA_BUILD(cmd, DBDMA_CMD_IN_LAST, 0, MACE_BUFLEN,
		    sc->sc_rxbuf_pa + MACE_BUFLEN * i, DBDMA_INT_ALWAYS,
		    DBDMA_WAIT_NEVER, DBDMA_BRANCH_NEVER);
		cmd++;
	}

	DBDMA_BUILD(cmd, DBDMA_CMD_NOP, 0, 0, 0,
	    DBDMA_INT_NEVER, DBDMA_WAIT_NEVER, DBDMA_BRANCH_ALWAYS);
	dbdma_st32(&cmd->d_cmddep, sc->sc_rxdbdma->d_paddr);
	cmd++;

	sc->sc_tail = 0;

	dbdma_start(sc->sc_rxdma, sc->sc_rxdbdma);
	/* Reenable receiver, reenable DMA */
	NIC_PUT(sc, MACE_MACCC, maccc);
}

void
mc_reset_txdma(struct mc_softc *sc)
{
	dbdma_command_t *cmd = sc->sc_txdmacmd;
	dbdma_regmap_t *dmareg = sc->sc_txdma;
	u_int8_t maccc;

	/* disable transmitter */
	maccc = NIC_GET(sc, MACE_MACCC);
	NIC_PUT(sc, MACE_MACCC, maccc & ~ENXMT);

	dbdma_reset(sc->sc_txdma);

	bzero(sc->sc_txdmacmd, 2 * sizeof(dbdma_command_t));
	DBDMA_BUILD(cmd, DBDMA_CMD_OUT_LAST, 0, 0, sc->sc_txbuf_pa,
	    DBDMA_INT_NEVER, DBDMA_WAIT_NEVER, DBDMA_BRANCH_NEVER);
	cmd++;
	DBDMA_BUILD(cmd, DBDMA_CMD_STOP, 0, 0, 0,
	    DBDMA_INT_NEVER, DBDMA_WAIT_NEVER, DBDMA_BRANCH_NEVER);

	out32rb(&dmareg->d_cmdptrhi, 0);
	out32rb(&dmareg->d_cmdptrlo, sc->sc_txdbdma->d_paddr);

	/* restore old value */
	NIC_PUT(sc, MACE_MACCC, maccc);
}

/*
 * Go through the list of multicast addresses and calculate the logical
 * address filter.
 */
void
mace_calcladrf(struct mc_softc *sc, u_int8_t *af)
{
	struct ether_multi *enm;
	u_int32_t crc;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct arpcom *ac = &sc->sc_arpcom;
	struct ether_multistep step;
	/*
	 * Set up multicast address filter by passing all multicast addresses
	 * through a crc generator, and then using the high order 6 bits as an
	 * index into the 64 bit logical address filter.  The high order bit
	 * selects the word, while the rest of the bits select the bit within
	 * the word.
	 */

	if (ac->ac_multirangecnt > 0)
		goto allmulti;

	*((u_int32_t *)af) = *((u_int32_t *)af + 1) = 0;
	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		crc = ether_crc32_le(enm->enm_addrlo, sizeof(enm->enm_addrlo));

		/* Just want the 6 most significant bits. */
		crc >>= 26;

		/* Set the corresponding bit in the filter. */
		af[crc >> 3] |= 1 << (crc & 7);

		ETHER_NEXT_MULTI(step, enm);
	}
	ifp->if_flags &= ~IFF_ALLMULTI;
	return;

allmulti:
	ifp->if_flags |= IFF_ALLMULTI;
	*((u_int32_t *)af) = *((u_int32_t *)af + 1) = 0xffffffff;
}
