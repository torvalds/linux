/*	$OpenBSD: if_xge.c,v 1.84 2024/05/24 06:02:57 jsg Exp $	*/
/*	$NetBSD: if_xge.c,v 1.1 2005/09/09 10:30:27 ragge Exp $	*/

/*
 * Copyright (c) 2004, SUNET, Swedish University Computer Network.
 * All rights reserved.
 *
 * Written by Anders Magnusson for SUNET, Swedish University Computer Network.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed for the NetBSD Project by
 *      SUNET, Swedish University Computer Network.
 * 4. The name of SUNET may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY SUNET ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL SUNET
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Driver for the Neterion Xframe Ten Gigabit Ethernet controller.
 */

#include "bpfilter.h"
#include "vlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/device.h>
#include <sys/endian.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_xgereg.h>

/* Xframe chipset revisions */
#define XGE_TYPE_XENA		1	/* Xframe */
#define XGE_TYPE_HERC		2	/* Xframe-II */

#define XGE_PCISIZE_XENA	26
#define XGE_PCISIZE_HERC	64

/*
 * Some tunable constants, tune with care!
 */
#define RX_MODE		RX_MODE_1  /* Receive mode (buffer usage, see below) */
#define NRXDESCS	1016	   /* # of receive descriptors (requested) */
#define NTXDESCS	2048	   /* Number of transmit descriptors */
#define NTXFRAGS	100	   /* Max fragments per packet */

/*
 * Receive buffer modes; 1, 3 or 5 buffers.
 */
#define RX_MODE_1 1
#define RX_MODE_3 3
#define RX_MODE_5 5

/*
 * Use clever macros to avoid a bunch of #ifdef's.
 */
#define XCONCAT3(x,y,z) x ## y ## z
#define CONCAT3(x,y,z) XCONCAT3(x,y,z)
#define NDESC_BUFMODE CONCAT3(NDESC_,RX_MODE,BUFMODE)
#define rxd_4k CONCAT3(rxd,RX_MODE,_4k)
/* XXX */
#if 0
#define rxdesc ___CONCAT(rxd,RX_MODE)
#endif
#define rxdesc rxd1

#define NEXTTX(x)	(((x)+1) % NTXDESCS)
#define NRXFRAGS	RX_MODE /* hardware imposed frags */
#define NRXPAGES	((NRXDESCS/NDESC_BUFMODE)+1)
#define NRXREAL		(NRXPAGES*NDESC_BUFMODE)
#define RXMAPSZ		(NRXPAGES*PAGE_SIZE)

/*
 * Magic to fix a bug when the MAC address cannot be read correctly.
 * This came from the Linux driver.
 */
static const uint64_t xge_fix_mac[] = {
	0x0060000000000000ULL, 0x0060600000000000ULL,
	0x0040600000000000ULL, 0x0000600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0000600000000000ULL,
	0x0040600000000000ULL, 0x0060600000000000ULL,
};

/*
 * Constants to be programmed into Hercules's registers, to configure
 * the XGXS transceiver.
 */
static const uint64_t xge_herc_dtx_cfg[] = {
	0x8000051536750000ULL, 0x80000515367500E0ULL,
	0x8000051536750004ULL, 0x80000515367500E4ULL,

	0x80010515003F0000ULL, 0x80010515003F00E0ULL,
	0x80010515003F0004ULL, 0x80010515003F00E4ULL,

	0x801205150D440000ULL, 0x801205150D4400E0ULL,
	0x801205150D440004ULL, 0x801205150D4400E4ULL,

	0x80020515F2100000ULL, 0x80020515F21000E0ULL,
	0x80020515F2100004ULL, 0x80020515F21000E4ULL,
};

static const uint64_t xge_xena_dtx_cfg[] = {
	0x8000051500000000ULL, 0x80000515000000E0ULL,
	0x80000515D9350004ULL, 0x80000515D93500E4ULL,

	0x8001051500000000ULL, 0x80010515000000E0ULL,
	0x80010515001E0004ULL, 0x80010515001E00E4ULL,

	0x8002051500000000ULL, 0x80020515000000E0ULL,
	0x80020515F2100004ULL, 0x80020515F21000E4ULL,
 };

struct xge_softc {
	struct device		sc_dev;
	struct arpcom		sc_arpcom;
	struct ifmedia		xena_media;

	void			*sc_ih;

	bus_dma_tag_t		sc_dmat;
	bus_space_tag_t		sc_st;
	bus_space_handle_t	sc_sh;
	bus_space_tag_t		sc_txt;
	bus_space_handle_t	sc_txh;

	pcireg_t		sc_pciregs[16];

	int			xge_type; /* chip type */
	int			xge_if_flags;

	/* Transmit structures */
	struct txd		*sc_txd[NTXDESCS]; /* transmit frags array */
	bus_addr_t		sc_txdp[NTXDESCS]; /* dva of transmit frags */
	bus_dmamap_t		sc_txm[NTXDESCS]; /* transmit frags map */
	struct mbuf		*sc_txb[NTXDESCS]; /* transmit mbuf pointer */
	int			sc_nexttx, sc_lasttx;
	bus_dmamap_t		sc_txmap; /* transmit descriptor map */

	/* Receive data */
	bus_dmamap_t		sc_rxmap; /* receive descriptor map */
	struct rxd_4k		*sc_rxd_4k[NRXPAGES]; /* receive desc pages */
	bus_dmamap_t		sc_rxm[NRXREAL]; /* receive buffer map */
	struct mbuf		*sc_rxb[NRXREAL]; /* mbufs on rx descriptors */
	int			sc_nextrx; /* next descriptor to check */
};

#ifdef XGE_DEBUG
#define DPRINTF(x)	do { if (xgedebug) printf x ; } while (0)
#define DPRINTFN(n,x)	do { if (xgedebug >= (n)) printf x ; } while (0)
int	xgedebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

int xge_match(struct device *, void *, void *);
void xge_attach(struct device *, struct device *, void *);
int xge_alloc_txmem(struct xge_softc *);
int xge_alloc_rxmem(struct xge_softc *);
void xge_start(struct ifnet *);
void xge_stop(struct ifnet *, int);
int xge_add_rxbuf(struct xge_softc *, int);
void xge_setmulti(struct xge_softc *);
void xge_setpromisc(struct xge_softc *);
int xge_setup_xgxs_xena(struct xge_softc *);
int xge_setup_xgxs_herc(struct xge_softc *);
int xge_ioctl(struct ifnet *, u_long, caddr_t);
int xge_init(struct ifnet *);
void xge_ifmedia_status(struct ifnet *, struct ifmediareq *);
int xge_xgmii_mediachange(struct ifnet *);
void xge_enable(struct xge_softc *);
int xge_intr(void  *);

/*
 * Helpers to address registers.
 */
#define PIF_WCSR(csr, val)	pif_wcsr(sc, csr, val)
#define PIF_RCSR(csr)		pif_rcsr(sc, csr)
#define TXP_WCSR(csr, val)	txp_wcsr(sc, csr, val)
#define PIF_WKEY(csr, val)	pif_wkey(sc, csr, val)

static inline void
pif_wcsr(struct xge_softc *sc, bus_size_t csr, uint64_t val)
{
#if defined(__LP64__)
	bus_space_write_raw_8(sc->sc_st, sc->sc_sh, csr, val);
#else
	uint32_t lval, hval;

	lval = val&0xffffffff;
	hval = val>>32;

#if BYTE_ORDER == LITTLE_ENDIAN
	bus_space_write_raw_4(sc->sc_st, sc->sc_sh, csr, lval);
	bus_space_write_raw_4(sc->sc_st, sc->sc_sh, csr+4, hval);
#else
	bus_space_write_raw_4(sc->sc_st, sc->sc_sh, csr+4, lval);
	bus_space_write_raw_4(sc->sc_st, sc->sc_sh, csr, hval);
#endif
#endif
}

static inline uint64_t
pif_rcsr(struct xge_softc *sc, bus_size_t csr)
{
	uint64_t val;
#if defined(__LP64__)
	val = bus_space_read_raw_8(sc->sc_st, sc->sc_sh, csr);
#else
	uint64_t val2;

	val = bus_space_read_raw_4(sc->sc_st, sc->sc_sh, csr);
	val2 = bus_space_read_raw_4(sc->sc_st, sc->sc_sh, csr+4);
#if BYTE_ORDER == LITTLE_ENDIAN
	val |= (val2 << 32);
#else
	val = (val << 32 | val2);
#endif
#endif
	return (val);
}

static inline void
txp_wcsr(struct xge_softc *sc, bus_size_t csr, uint64_t val)
{
#if defined(__LP64__)
	bus_space_write_raw_8(sc->sc_txt, sc->sc_txh, csr, val);
#else
	uint32_t lval, hval;

	lval = val&0xffffffff;
	hval = val>>32;

#if BYTE_ORDER == LITTLE_ENDIAN
	bus_space_write_raw_4(sc->sc_txt, sc->sc_txh, csr, lval);
	bus_space_write_raw_4(sc->sc_txt, sc->sc_txh, csr+4, hval);
#else
	bus_space_write_raw_4(sc->sc_txt, sc->sc_txh, csr, hval);
	bus_space_write_raw_4(sc->sc_txt, sc->sc_txh, csr+4, lval);
#endif
#endif
}

static inline void
pif_wkey(struct xge_softc *sc, bus_size_t csr, uint64_t val)
{
#if defined(__LP64__)
	if (sc->xge_type == XGE_TYPE_XENA)
		PIF_WCSR(RMAC_CFG_KEY, RMAC_KEY_VALUE);

	bus_space_write_raw_8(sc->sc_st, sc->sc_sh, csr, val);
#else
	uint32_t lval, hval;

	lval = val&0xffffffff;
	hval = val>>32;

	if (sc->xge_type == XGE_TYPE_XENA)
		PIF_WCSR(RMAC_CFG_KEY, RMAC_KEY_VALUE);

#if BYTE_ORDER == LITTLE_ENDIAN
	bus_space_write_raw_4(sc->sc_st, sc->sc_sh, csr, lval);
#else
	bus_space_write_raw_4(sc->sc_st, sc->sc_sh, csr, hval);
#endif

	if (sc->xge_type == XGE_TYPE_XENA)
		PIF_WCSR(RMAC_CFG_KEY, RMAC_KEY_VALUE);
#if BYTE_ORDER == LITTLE_ENDIAN
	bus_space_write_raw_4(sc->sc_st, sc->sc_sh, csr+4, hval);
#else
	bus_space_write_raw_4(sc->sc_st, sc->sc_sh, csr+4, lval);
#endif
#endif
}

const struct cfattach xge_ca = {
	sizeof(struct xge_softc), xge_match, xge_attach
};

struct cfdriver xge_cd = {
	NULL, "xge", DV_IFNET
};

#define XNAME sc->sc_dev.dv_xname

#define XGE_RXSYNC(desc, what) \
	bus_dmamap_sync(sc->sc_dmat, sc->sc_rxmap, \
	(desc/NDESC_BUFMODE) * XGE_PAGE + sizeof(struct rxdesc) * \
	(desc%NDESC_BUFMODE), sizeof(struct rxdesc), what)
#define XGE_RXD(desc)	&sc->sc_rxd_4k[desc/NDESC_BUFMODE]-> \
	r4_rxd[desc%NDESC_BUFMODE]

/*
 * Non-tunable constants.
 */
#define XGE_MAX_FRAMELEN	9622
#define XGE_MAX_MTU		(XGE_MAX_FRAMELEN - ETHER_HDR_LEN - \
				 ETHER_CRC_LEN - ETHER_VLAN_ENCAP_LEN)

const struct pci_matchid xge_devices[] = {
	{ PCI_VENDOR_NETERION, PCI_PRODUCT_NETERION_XFRAME },
	{ PCI_VENDOR_NETERION, PCI_PRODUCT_NETERION_XFRAME_2 }
};

int
xge_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, xge_devices,
	    nitems(xge_devices)));
}

void
xge_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	struct xge_softc *sc;
	struct ifnet *ifp;
	pcireg_t memtype;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	pci_chipset_tag_t pc = pa->pa_pc;
	uint8_t enaddr[ETHER_ADDR_LEN];
	uint64_t val;
	int i;

	sc = (struct xge_softc *)self;

	sc->sc_dmat = pa->pa_dmat;

	if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_NETERION_XFRAME)
		sc->xge_type = XGE_TYPE_XENA;
	else
		sc->xge_type = XGE_TYPE_HERC;

	/* Get BAR0 address */
	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, XGE_PIF_BAR);
	if (pci_mapreg_map(pa, XGE_PIF_BAR, memtype, 0,
	    &sc->sc_st, &sc->sc_sh, 0, 0, 0)) {
		printf(": unable to map PIF BAR registers\n");
		return;
	}

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, XGE_TXP_BAR);
	if (pci_mapreg_map(pa, XGE_TXP_BAR, memtype, 0,
	    &sc->sc_txt, &sc->sc_txh, 0, 0, 0)) {
		printf(": unable to map TXP BAR registers\n");
		return;
	}

	if (sc->xge_type == XGE_TYPE_XENA) {
		/* Save PCI config space */
		for (i = 0; i < XGE_PCISIZE_XENA; i += 4)
			sc->sc_pciregs[i/4] = pci_conf_read(pa->pa_pc, pa->pa_tag, i);
	}

#if BYTE_ORDER == LITTLE_ENDIAN
	val = (uint64_t)0xFFFFFFFFFFFFFFFFULL;
	val &= ~(TxF_R_SE|RxF_W_SE);
	PIF_WCSR(SWAPPER_CTRL, val);
	PIF_WCSR(SWAPPER_CTRL, val);
#endif
	if ((val = PIF_RCSR(PIF_RD_SWAPPER_Fb)) != SWAPPER_MAGIC) {
		printf(": failed configuring endian (read), %llx != %llx!\n",
		    (unsigned long long)val, SWAPPER_MAGIC);
	}

	PIF_WCSR(XMSI_ADDRESS, SWAPPER_MAGIC);
	if ((val = PIF_RCSR(XMSI_ADDRESS)) != SWAPPER_MAGIC) {
		printf(": failed configuring endian (write), %llx != %llx!\n",
			(unsigned long long)val, SWAPPER_MAGIC);
	} 

	/*
	 * Fix for all "FFs" MAC address problems observed on
	 * Alpha platforms. Not needed for Herc.
	 */
	if (sc->xge_type == XGE_TYPE_XENA) {
		/*
		 * The MAC addr may be all FF's, which is not good.
		 * Resolve it by writing some magics to GPIO_CONTROL and
		 * force a chip reset to read in the serial eeprom again.
		 */
		for (i = 0; i < nitems(xge_fix_mac); i++) {
			PIF_WCSR(GPIO_CONTROL, xge_fix_mac[i]);
			PIF_RCSR(GPIO_CONTROL);
		}

		/*
		 * Reset the chip and restore the PCI registers.
		 */
		PIF_WCSR(SW_RESET, 0xa5a5a50000000000ULL);
		DELAY(500000);
		for (i = 0; i < XGE_PCISIZE_XENA; i += 4)
			pci_conf_write(pa->pa_pc, pa->pa_tag, i, sc->sc_pciregs[i/4]);

		/*
		 * Restore the byte order registers.
		 */
#if BYTE_ORDER == LITTLE_ENDIAN
		val = (uint64_t)0xFFFFFFFFFFFFFFFFULL;
		val &= ~(TxF_R_SE|RxF_W_SE);
		PIF_WCSR(SWAPPER_CTRL, val);
		PIF_WCSR(SWAPPER_CTRL, val);
#endif

		if ((val = PIF_RCSR(PIF_RD_SWAPPER_Fb)) != SWAPPER_MAGIC) {
			printf(": failed configuring endian2 (read), %llx != %llx!\n",
			    (unsigned long long)val, SWAPPER_MAGIC);
			return;
		}

		PIF_WCSR(XMSI_ADDRESS, SWAPPER_MAGIC);
		if ((val = PIF_RCSR(XMSI_ADDRESS)) != SWAPPER_MAGIC) {
			printf(": failed configuring endian2 (write), %llx != %llx!\n",
			    (unsigned long long)val, SWAPPER_MAGIC);
			return;
		}
	}

	/*
	 * XGXS initialization.
	 */

	/*
	 * For Herc, bring EOI out of reset before XGXS.
	 */
	if (sc->xge_type == XGE_TYPE_HERC) {
		val = PIF_RCSR(SW_RESET);
		val &= 0xffff00ffffffffffULL;
		PIF_WCSR(SW_RESET,val);
		delay(1000*1000);	/* wait for 1 sec */
	}

	/* 29, Bring adapter out of reset */
	val = PIF_RCSR(SW_RESET);
	val &= 0xffffff00ffffffffULL;
	PIF_WCSR(SW_RESET, val);
	DELAY(500000);

	/* Ensure that it's safe to access registers by checking
	 * RIC_RUNNING bit is reset. Check is valid only for XframeII.
	 */
	if (sc->xge_type == XGE_TYPE_HERC){
		for (i = 0; i < 50; i++) {
			val = PIF_RCSR(ADAPTER_STATUS);
			if (!(val & RIC_RUNNING))
				break;
			delay(20*1000);
		}

		if (i == 50) {
			printf(": not safe to access registers\n");
			return;
		}
	}

	/* 30, configure XGXS transceiver */
	if (sc->xge_type == XGE_TYPE_XENA)
		xge_setup_xgxs_xena(sc);
	else if(sc->xge_type == XGE_TYPE_HERC)
		xge_setup_xgxs_herc(sc);

	/* 33, program MAC address (not needed here) */
	/* Get ethernet address */
	PIF_WCSR(RMAC_ADDR_CMD_MEM,
	    RMAC_ADDR_CMD_MEM_STR|RMAC_ADDR_CMD_MEM_OFF(0));
	while (PIF_RCSR(RMAC_ADDR_CMD_MEM) & RMAC_ADDR_CMD_MEM_STR)
		;
	val = PIF_RCSR(RMAC_ADDR_DATA0_MEM);
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		enaddr[i] = (uint8_t)(val >> (56 - (8*i)));

	/*
	 * Get memory for transmit descriptor lists.
	 */
	if (xge_alloc_txmem(sc)) {
		printf(": failed allocating txmem.\n");
		return;
	}

	/* 9 and 10 - set FIFO number/prio */
	PIF_WCSR(TX_FIFO_P0, TX_FIFO_LEN0(NTXDESCS));
	PIF_WCSR(TX_FIFO_P1, 0ULL);
	PIF_WCSR(TX_FIFO_P2, 0ULL);
	PIF_WCSR(TX_FIFO_P3, 0ULL);

	/* 11, XXX set round-robin prio? */

	/* 12, enable transmit FIFO */
	val = PIF_RCSR(TX_FIFO_P0);
	val |= TX_FIFO_ENABLE;
	PIF_WCSR(TX_FIFO_P0, val);

	/* 13, disable some error checks */
	PIF_WCSR(TX_PA_CFG,
	    TX_PA_CFG_IFR|TX_PA_CFG_ISO|TX_PA_CFG_ILC|TX_PA_CFG_ILE);

	/* Create transmit DMA maps */
	for (i = 0; i < NTXDESCS; i++) {
		if (bus_dmamap_create(sc->sc_dmat, XGE_MAX_FRAMELEN,
		    NTXFRAGS, XGE_MAX_FRAMELEN, 0, BUS_DMA_NOWAIT,
		    &sc->sc_txm[i])) {
			printf(": cannot create TX DMA maps\n");
			return;
		}
	}

	sc->sc_lasttx = NTXDESCS-1;

	/*
	 * RxDMA initialization.
	 * Only use one out of 8 possible receive queues.
	 */
	/* allocate rx descriptor memory */
	if (xge_alloc_rxmem(sc)) {
		printf(": failed allocating rxmem\n");
		return;
	}

	/* Create receive buffer DMA maps */
	for (i = 0; i < NRXREAL; i++) {
		if (bus_dmamap_create(sc->sc_dmat, XGE_MAX_FRAMELEN,
		    NRXFRAGS, XGE_MAX_FRAMELEN, 0, BUS_DMA_NOWAIT,
		    &sc->sc_rxm[i])) {
			printf(": cannot create RX DMA maps\n");
			return;
		}
	}

	/* allocate mbufs to receive descriptors */
	for (i = 0; i < NRXREAL; i++)
		if (xge_add_rxbuf(sc, i))
			panic("out of mbufs too early");

	/* 14, setup receive ring priority */
	PIF_WCSR(RX_QUEUE_PRIORITY, 0ULL); /* only use one ring */

	/* 15, setup receive ring round-robin calendar */
	PIF_WCSR(RX_W_ROUND_ROBIN_0, 0ULL); /* only use one ring */
	PIF_WCSR(RX_W_ROUND_ROBIN_1, 0ULL);
	PIF_WCSR(RX_W_ROUND_ROBIN_2, 0ULL);
	PIF_WCSR(RX_W_ROUND_ROBIN_3, 0ULL);
	PIF_WCSR(RX_W_ROUND_ROBIN_4, 0ULL);

	/* 16, write receive ring start address */
	PIF_WCSR(PRC_RXD0_0, (uint64_t)sc->sc_rxmap->dm_segs[0].ds_addr);
	/* PRC_RXD0_[1-7] are not used */

	/* 17, Setup alarm registers */
	PIF_WCSR(PRC_ALARM_ACTION, 0ULL); /* Default everything to retry */

	/* 18, init receive ring controller */
#if RX_MODE == RX_MODE_1
	val = RING_MODE_1;
#elif RX_MODE == RX_MODE_3
	val = RING_MODE_3;
#else /* RX_MODE == RX_MODE_5 */
	val = RING_MODE_5;
#endif
	PIF_WCSR(PRC_CTRL_0, RC_IN_SVC|val);
	/* leave 1-7 disabled */
	/* XXXX snoop configuration? */

	/* 19, set chip memory assigned to the queue */
	if (sc->xge_type == XGE_TYPE_XENA) {
		/* all 64M to queue 0 */
		PIF_WCSR(RX_QUEUE_CFG, MC_QUEUE(0, 64));
	} else {
		/* all 32M to queue 0 */
		PIF_WCSR(RX_QUEUE_CFG, MC_QUEUE(0, 32));
	}

	/* 20, setup RLDRAM parameters */
	/* do not touch it for now */

	/* 21, setup pause frame thresholds */
	/* so not touch the defaults */
	/* XXX - must 0xff be written as stated in the manual? */

	/* 22, configure RED */
	/* we do not want to drop packets, so ignore */

	/* 23, initiate RLDRAM */
	val = PIF_RCSR(MC_RLDRAM_MRS);
	val |= MC_QUEUE_SIZE_ENABLE|MC_RLDRAM_MRS_ENABLE;
	PIF_WCSR(MC_RLDRAM_MRS, val);
	DELAY(1000);

	/*
	 * Setup interrupt policies.
	 */
	/* 40, Transmit interrupts */
	PIF_WCSR(TTI_DATA1_MEM, TX_TIMER_VAL(0x1ff) | TX_TIMER_AC |
	    TX_URNG_A(5) | TX_URNG_B(20) | TX_URNG_C(48));
	PIF_WCSR(TTI_DATA2_MEM,
	    TX_UFC_A(25) | TX_UFC_B(64) | TX_UFC_C(128) | TX_UFC_D(512));
	PIF_WCSR(TTI_COMMAND_MEM, TTI_CMD_MEM_WE | TTI_CMD_MEM_STROBE);
	while (PIF_RCSR(TTI_COMMAND_MEM) & TTI_CMD_MEM_STROBE)
		;

	/* 41, Receive interrupts */
	PIF_WCSR(RTI_DATA1_MEM, RX_TIMER_VAL(0x800) | RX_TIMER_AC |
	    RX_URNG_A(5) | RX_URNG_B(20) | RX_URNG_C(50));
	PIF_WCSR(RTI_DATA2_MEM,
	    RX_UFC_A(64) | RX_UFC_B(128) | RX_UFC_C(256) | RX_UFC_D(512));
	PIF_WCSR(RTI_COMMAND_MEM, RTI_CMD_MEM_WE | RTI_CMD_MEM_STROBE);
	while (PIF_RCSR(RTI_COMMAND_MEM) & RTI_CMD_MEM_STROBE)
		;

	/*
	 * Setup media stuff.
	 */
	ifmedia_init(&sc->xena_media, IFM_IMASK, xge_xgmii_mediachange,
	    xge_ifmedia_status);
	ifmedia_add(&sc->xena_media, IFM_ETHER|IFM_10G_SR, 0, NULL);
	ifmedia_set(&sc->xena_media, IFM_ETHER|IFM_10G_SR);

	ifp = &sc->sc_arpcom.ac_if;
	strlcpy(ifp->if_xname, XNAME, IFNAMSIZ);
	memcpy(sc->sc_arpcom.ac_enaddr, enaddr, ETHER_ADDR_LEN);
	ifp->if_baudrate = IF_Gbps(10);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = xge_ioctl;
	ifp->if_start = xge_start;
	ifp->if_hardmtu = XGE_MAX_MTU;
	ifq_init_maxlen(&ifp->if_snd, NTXDESCS - 1);

	ifp->if_capabilities = IFCAP_VLAN_MTU | IFCAP_CSUM_IPv4 |
	    IFCAP_CSUM_TCPv4 | IFCAP_CSUM_UDPv4;

#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif

	/*
	 * Attach the interface.
	 */
	if_attach(ifp);
	ether_ifattach(ifp);

	/*
	 * Setup interrupt vector before initializing.
	 */
	if (pci_intr_map(pa, &ih)) {
		printf(": unable to map interrupt\n");
		return;
	}
	intrstr = pci_intr_string(pc, ih);
	if ((sc->sc_ih =
	    pci_intr_establish(pc, ih, IPL_NET, xge_intr, sc, XNAME)) == NULL) {
		printf(": unable to establish interrupt at %s\n",
		    intrstr ? intrstr : "<unknown>");
		return;
	    }
	printf(": %s, address %s\n", intrstr, ether_sprintf(enaddr));
}

void
xge_ifmedia_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct xge_softc *sc = ifp->if_softc;
	uint64_t reg;

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER|IFM_10G_SR;

	reg = PIF_RCSR(ADAPTER_STATUS);
	if ((reg & (RMAC_REMOTE_FAULT|RMAC_LOCAL_FAULT)) == 0)
		ifmr->ifm_status |= IFM_ACTIVE;
}

int
xge_xgmii_mediachange(struct ifnet *ifp)
{
	return (0);
}

void
xge_enable(struct xge_softc *sc)
{
	uint64_t val;

	/* 2, enable adapter */
	val = PIF_RCSR(ADAPTER_CONTROL);
	val |= ADAPTER_EN;
	PIF_WCSR(ADAPTER_CONTROL, val);

	/* 3, light the card enable led */
	val = PIF_RCSR(ADAPTER_CONTROL);
	val |= LED_ON;
	PIF_WCSR(ADAPTER_CONTROL, val);
#ifdef XGE_DEBUG
	printf("%s: link up\n", XNAME);
#endif
}

int
xge_init(struct ifnet *ifp)
{
	struct xge_softc *sc = ifp->if_softc;
	uint64_t val;
	int s;

	s = splnet();

	/*
	 * Cancel any pending I/O
	 */
	xge_stop(ifp, 0);

	/* 31+32, setup MAC config */
	PIF_WKEY(MAC_CFG, TMAC_EN|RMAC_EN|TMAC_APPEND_PAD|RMAC_STRIP_FCS|
	    RMAC_BCAST_EN|RMAC_DISCARD_PFRM);

	DELAY(1000);

	/* 54, ensure that the adapter is 'quiescent' */
	val = PIF_RCSR(ADAPTER_STATUS);
	if ((val & QUIESCENT) != QUIESCENT) {
#if 0
		char buf[200];
#endif
		printf("%s: adapter not quiescent, aborting\n", XNAME);
		val = (val & QUIESCENT) ^ QUIESCENT;
#if 0
		bitmask_snprintf(val, QUIESCENT_BMSK, buf, sizeof buf);
		printf("%s: ADAPTER_STATUS missing bits %s\n", XNAME, buf);
#endif
		splx(s);
		return (1);
	}

	if (!(ifp->if_capabilities & IFCAP_VLAN_HWTAGGING)) {
		/* disable VLAN tag stripping */
		val = PIF_RCSR(RX_PA_CFG);
		val &= ~STRIP_VLAN_TAG;
		PIF_WCSR(RX_PA_CFG, val);
	}

	/* set MRU */
	PIF_WCSR(RMAC_MAX_PYLD_LEN, RMAC_PYLD_LEN(XGE_MAX_FRAMELEN));

	/* 56, enable the transmit laser */
	val = PIF_RCSR(ADAPTER_CONTROL);
	val |= EOI_TX_ON;
	PIF_WCSR(ADAPTER_CONTROL, val);

	xge_enable(sc);

	/*
	 * Enable all interrupts
	 */
	PIF_WCSR(TX_TRAFFIC_MASK, 0);
	PIF_WCSR(RX_TRAFFIC_MASK, 0);
	PIF_WCSR(TXPIC_INT_MASK, 0);
	PIF_WCSR(RXPIC_INT_MASK, 0);

	PIF_WCSR(MAC_INT_MASK, MAC_TMAC_INT); /* only from RMAC */
	PIF_WCSR(MAC_RMAC_ERR_REG, RMAC_LINK_STATE_CHANGE_INT);
	PIF_WCSR(MAC_RMAC_ERR_MASK, ~RMAC_LINK_STATE_CHANGE_INT);
	PIF_WCSR(GENERAL_INT_MASK, 0);

	xge_setpromisc(sc);

	xge_setmulti(sc);

	/* Done... */
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);

	return (0);
}

void
xge_stop(struct ifnet *ifp, int disable)
{
	struct xge_softc *sc = ifp->if_softc;
	uint64_t val;

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	val = PIF_RCSR(ADAPTER_CONTROL);
	val &= ~ADAPTER_EN;
	PIF_WCSR(ADAPTER_CONTROL, val);

	while ((PIF_RCSR(ADAPTER_STATUS) & QUIESCENT) != QUIESCENT)
		;
}

int
xge_intr(void *pv)
{
	struct xge_softc *sc = pv;
	struct txd *txd;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	bus_dmamap_t dmp;
	uint64_t val;
	int i, lasttx, plen;

	val = PIF_RCSR(GENERAL_INT_STATUS);
	if (val == 0)
		return (0); /* no interrupt here */

	PIF_WCSR(GENERAL_INT_STATUS, val);

	if ((val = PIF_RCSR(MAC_RMAC_ERR_REG)) & RMAC_LINK_STATE_CHANGE_INT) {
		/* Wait for quiescence */
#ifdef XGE_DEBUG
		printf("%s: link down\n", XNAME);
#endif
		while ((PIF_RCSR(ADAPTER_STATUS) & QUIESCENT) != QUIESCENT)
			;
		PIF_WCSR(MAC_RMAC_ERR_REG, RMAC_LINK_STATE_CHANGE_INT);

		val = PIF_RCSR(ADAPTER_STATUS);
		if ((val & (RMAC_REMOTE_FAULT|RMAC_LOCAL_FAULT)) == 0)
			xge_enable(sc); /* Only if link restored */
	}

	if ((val = PIF_RCSR(TX_TRAFFIC_INT)))
		PIF_WCSR(TX_TRAFFIC_INT, val); /* clear interrupt bits */
	/*
	 * Collect sent packets.
	 */
	lasttx = sc->sc_lasttx;
	while ((i = NEXTTX(sc->sc_lasttx)) != sc->sc_nexttx) {
		txd = sc->sc_txd[i];
		dmp = sc->sc_txm[i];

		bus_dmamap_sync(sc->sc_dmat, dmp, 0,
		    dmp->dm_mapsize,
		    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

		if (txd->txd_control1 & TXD_CTL1_OWN) {
			bus_dmamap_sync(sc->sc_dmat, dmp, 0,
			    dmp->dm_mapsize, BUS_DMASYNC_PREREAD);
			break;
		}
		bus_dmamap_unload(sc->sc_dmat, dmp);
		m_freem(sc->sc_txb[i]);
		sc->sc_lasttx = i;
	}

	if (sc->sc_lasttx != lasttx)
		ifq_clr_oactive(&ifp->if_snd);

	/* Try to get more packets on the wire */
	xge_start(ifp);

	/* clear interrupt bits */
	if ((val = PIF_RCSR(RX_TRAFFIC_INT)))
		PIF_WCSR(RX_TRAFFIC_INT, val);

	for (;;) {
		struct rxdesc *rxd;
		struct mbuf *m;

		XGE_RXSYNC(sc->sc_nextrx,
		    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

		rxd = XGE_RXD(sc->sc_nextrx);
		if (rxd->rxd_control1 & RXD_CTL1_OWN) {
			XGE_RXSYNC(sc->sc_nextrx, BUS_DMASYNC_PREREAD);
			break;
		}

		/* got a packet */
		m = sc->sc_rxb[sc->sc_nextrx];
#if RX_MODE == RX_MODE_1
		plen = m->m_len = RXD_CTL2_BUF0SIZ(rxd->rxd_control2);
#elif RX_MODE == RX_MODE_3
#error Fix rxmodes in xge_intr
#elif RX_MODE == RX_MODE_5
		plen = m->m_len = RXD_CTL2_BUF0SIZ(rxd->rxd_control2);
		plen += m->m_next->m_len = RXD_CTL2_BUF1SIZ(rxd->rxd_control2);
		plen += m->m_next->m_next->m_len =
		    RXD_CTL2_BUF2SIZ(rxd->rxd_control2);
		plen += m->m_next->m_next->m_next->m_len =
		    RXD_CTL3_BUF3SIZ(rxd->rxd_control3);
		plen += m->m_next->m_next->m_next->m_next->m_len =
		    RXD_CTL3_BUF4SIZ(rxd->rxd_control3);
#endif
		m->m_pkthdr.len = plen;

		val = rxd->rxd_control1;

		if (xge_add_rxbuf(sc, sc->sc_nextrx)) {
			/* Failed, recycle this mbuf */
#if RX_MODE == RX_MODE_1
			rxd->rxd_control2 = RXD_MKCTL2(MCLBYTES, 0, 0);
			rxd->rxd_control1 = RXD_CTL1_OWN;
#elif RX_MODE == RX_MODE_3
#elif RX_MODE == RX_MODE_5
#endif
			XGE_RXSYNC(sc->sc_nextrx,
			    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
			ifp->if_ierrors++;
			break;
		}

		if (RXD_CTL1_PROTOS(val) & RXD_CTL1_P_IPv4)
			m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;
		if (RXD_CTL1_PROTOS(val) & RXD_CTL1_P_TCP)
			m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_OK;
		if (RXD_CTL1_PROTOS(val) & RXD_CTL1_P_UDP)
			m->m_pkthdr.csum_flags |= M_UDP_CSUM_IN_OK;

#if NVLAN > 0
		if (RXD_CTL1_PROTOS(val) & RXD_CTL1_P_VLAN) {
			m->m_pkthdr.ether_vtag =
			    RXD_CTL2_VLANTAG(rxd->rxd_control2);
			m->m_flags |= M_VLANTAG;
		}
#endif

		ml_enqueue(&ml, m);

		if (++sc->sc_nextrx == NRXREAL)
			sc->sc_nextrx = 0;
	}

	if_input(ifp, &ml);

	return (1);
}

int
xge_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct xge_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			xge_init(ifp);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING &&
			    (ifp->if_flags ^ sc->xge_if_flags) &
			     IFF_PROMISC) {
				xge_setpromisc(sc);
			} else {
				if (!(ifp->if_flags & IFF_RUNNING))
					xge_init(ifp);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				xge_stop(ifp, 1);
		}
		sc->xge_if_flags = ifp->if_flags;
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->xena_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			xge_setmulti(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
xge_setmulti(struct xge_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct arpcom *ac = &sc->sc_arpcom;
	struct ether_multi *enm;
	struct ether_multistep step;
	int i, numaddr = 1; /* first slot used for card unicast address */
	uint64_t val;

	if (ac->ac_multirangecnt > 0)
		goto allmulti;

	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		if (numaddr == MAX_MCAST_ADDR)
			goto allmulti;
		for (val = 0, i = 0; i < ETHER_ADDR_LEN; i++) {
			val <<= 8;
			val |= enm->enm_addrlo[i];
		}
		PIF_WCSR(RMAC_ADDR_DATA0_MEM, val << 16);
		PIF_WCSR(RMAC_ADDR_DATA1_MEM, 0xFFFFFFFFFFFFFFFFULL);
		PIF_WCSR(RMAC_ADDR_CMD_MEM, RMAC_ADDR_CMD_MEM_WE|
		    RMAC_ADDR_CMD_MEM_STR|RMAC_ADDR_CMD_MEM_OFF(numaddr));
		while (PIF_RCSR(RMAC_ADDR_CMD_MEM) & RMAC_ADDR_CMD_MEM_STR)
			;
		numaddr++;
		ETHER_NEXT_MULTI(step, enm);
	}
	/* set the remaining entries to the broadcast address */
	for (i = numaddr; i < MAX_MCAST_ADDR; i++) {
		PIF_WCSR(RMAC_ADDR_DATA0_MEM, 0xffffffffffff0000ULL);
		PIF_WCSR(RMAC_ADDR_DATA1_MEM, 0xFFFFFFFFFFFFFFFFULL);
		PIF_WCSR(RMAC_ADDR_CMD_MEM, RMAC_ADDR_CMD_MEM_WE|
		    RMAC_ADDR_CMD_MEM_STR|RMAC_ADDR_CMD_MEM_OFF(i));
		while (PIF_RCSR(RMAC_ADDR_CMD_MEM) & RMAC_ADDR_CMD_MEM_STR)
			;
	}
	ifp->if_flags &= ~IFF_ALLMULTI;
	return;

allmulti:
	/* Just receive everything with the multicast bit set */
	ifp->if_flags |= IFF_ALLMULTI;
	PIF_WCSR(RMAC_ADDR_DATA0_MEM, 0x8000000000000000ULL);
	PIF_WCSR(RMAC_ADDR_DATA1_MEM, 0xF000000000000000ULL);
	PIF_WCSR(RMAC_ADDR_CMD_MEM, RMAC_ADDR_CMD_MEM_WE|
	    RMAC_ADDR_CMD_MEM_STR|RMAC_ADDR_CMD_MEM_OFF(1));
	while (PIF_RCSR(RMAC_ADDR_CMD_MEM) & RMAC_ADDR_CMD_MEM_STR)
		;
}

void
xge_setpromisc(struct xge_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint64_t val;

	val = PIF_RCSR(MAC_CFG);

	if (ifp->if_flags & IFF_PROMISC)
		val |= RMAC_PROM_EN;
	else
		val &= ~RMAC_PROM_EN;

	PIF_WCSR(MAC_CFG, val);
}

void
xge_start(struct ifnet *ifp)
{
	struct xge_softc *sc = ifp->if_softc;
	struct txd *txd = NULL; /* XXX - gcc */
	bus_dmamap_t dmp;
	struct	mbuf *m;
	uint64_t par, lcr;
	int nexttx = 0, ntxd, i;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	par = lcr = 0;
	for (;;) {
		if (sc->sc_nexttx == sc->sc_lasttx) {
			ifq_set_oactive(&ifp->if_snd);
			break;	/* No more space */
		}

		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;	/* out of packets */

		nexttx = sc->sc_nexttx;
		dmp = sc->sc_txm[nexttx];

		switch (bus_dmamap_load_mbuf(sc->sc_dmat, dmp, m,
		    BUS_DMA_WRITE|BUS_DMA_NOWAIT)) {
		case 0:
			break;
		case EFBIG:
			if (m_defrag(m, M_DONTWAIT) == 0 &&
			    bus_dmamap_load_mbuf(sc->sc_dmat, dmp, m,
			    BUS_DMA_WRITE|BUS_DMA_NOWAIT) == 0)
				break;
		default:
			m_freem(m);
			continue;
		}

		bus_dmamap_sync(sc->sc_dmat, dmp, 0, dmp->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);

		txd = sc->sc_txd[nexttx];
		sc->sc_txb[nexttx] = m;
		for (i = 0; i < dmp->dm_nsegs; i++) {
			if (dmp->dm_segs[i].ds_len == 0)
				continue;
			txd->txd_control1 = dmp->dm_segs[i].ds_len;
			txd->txd_control2 = 0;
			txd->txd_bufaddr = dmp->dm_segs[i].ds_addr;
			txd++;
		}
		ntxd = txd - sc->sc_txd[nexttx] - 1;
		txd = sc->sc_txd[nexttx];
		txd->txd_control1 |= TXD_CTL1_OWN|TXD_CTL1_GCF;
		txd->txd_control2 = TXD_CTL2_UTIL;

#if NVLAN > 0
		if (m->m_flags & M_VLANTAG) {
			txd->txd_control2 |= TXD_CTL2_VLANE;
			txd->txd_control2 |=
			    TXD_CTL2_VLANT(m->m_pkthdr.ether_vtag);
		}
#endif

		if (m->m_pkthdr.csum_flags & M_IPV4_CSUM_OUT)
			txd->txd_control2 |= TXD_CTL2_CIPv4;
		if (m->m_pkthdr.csum_flags & M_TCP_CSUM_OUT)
			txd->txd_control2 |= TXD_CTL2_CTCP;
		if (m->m_pkthdr.csum_flags & M_UDP_CSUM_OUT)
			txd->txd_control2 |= TXD_CTL2_CUDP;

		txd[ntxd].txd_control1 |= TXD_CTL1_GCL;

		bus_dmamap_sync(sc->sc_dmat, dmp, 0, dmp->dm_mapsize,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

		par = sc->sc_txdp[nexttx];
		lcr = TXDL_NUMTXD(ntxd) | TXDL_LGC_FIRST | TXDL_LGC_LAST;
		TXP_WCSR(TXDL_PAR, par);
		TXP_WCSR(TXDL_LCR, lcr);

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap_ether(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif /* NBPFILTER > 0 */

		sc->sc_nexttx = NEXTTX(nexttx);
	}
}

/*
 * Allocate DMA memory for transmit descriptor fragments.
 * Only one map is used for all descriptors.
 */
int
xge_alloc_txmem(struct xge_softc *sc)
{
	struct txd *txp;
	bus_dma_segment_t seg;
	bus_addr_t txdp;
	caddr_t kva;
	int i, rseg, state;

#define TXMAPSZ (NTXDESCS*NTXFRAGS*sizeof(struct txd))
	state = 0;
	if (bus_dmamem_alloc(sc->sc_dmat, TXMAPSZ, PAGE_SIZE, 0,
	    &seg, 1, &rseg, BUS_DMA_NOWAIT))
		goto err;
	state++;
	if (bus_dmamem_map(sc->sc_dmat, &seg, rseg, TXMAPSZ, &kva,
	    BUS_DMA_NOWAIT))
		goto err;

	state++;
	if (bus_dmamap_create(sc->sc_dmat, TXMAPSZ, 1, TXMAPSZ, 0,
	    BUS_DMA_NOWAIT, &sc->sc_txmap))
		goto err;
	state++;
	if (bus_dmamap_load(sc->sc_dmat, sc->sc_txmap,
	    kva, TXMAPSZ, NULL, BUS_DMA_NOWAIT))
		goto err;

	/* setup transmit array pointers */
	txp = (struct txd *)kva;
	txdp = seg.ds_addr;
	for (i = 0; i < NTXDESCS; i++) {
		sc->sc_txd[i] = txp;
		sc->sc_txdp[i] = txdp;
		txp += NTXFRAGS;
		txdp += (NTXFRAGS * sizeof(struct txd));
	}

	return (0);

err:
	if (state > 2)
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_txmap);
	if (state > 1)
		bus_dmamem_unmap(sc->sc_dmat, kva, TXMAPSZ);
	if (state > 0)
		bus_dmamem_free(sc->sc_dmat, &seg, rseg);
	return (ENOBUFS);
}

/*
 * Allocate DMA memory for receive descriptor,
 * only one map is used for all descriptors.
 * link receive descriptor pages together.
 */
int
xge_alloc_rxmem(struct xge_softc *sc)
{
	struct rxd_4k *rxpp;
	bus_dma_segment_t seg;
	caddr_t kva;
	int i, rseg, state;

	/* sanity check */
	if (sizeof(struct rxd_4k) != XGE_PAGE) {
		printf("bad compiler struct alignment, %d != %d\n",
		    (int)sizeof(struct rxd_4k), XGE_PAGE);
		return (EINVAL);
	}

	state = 0;
	if (bus_dmamem_alloc(sc->sc_dmat, RXMAPSZ, PAGE_SIZE, 0,
	    &seg, 1, &rseg, BUS_DMA_NOWAIT))
		goto err;
	state++;
	if (bus_dmamem_map(sc->sc_dmat, &seg, rseg, RXMAPSZ, &kva,
	    BUS_DMA_NOWAIT))
		goto err;

	state++;
	if (bus_dmamap_create(sc->sc_dmat, RXMAPSZ, 1, RXMAPSZ, 0,
	    BUS_DMA_NOWAIT, &sc->sc_rxmap))
		goto err;
	state++;
	if (bus_dmamap_load(sc->sc_dmat, sc->sc_rxmap,
	    kva, RXMAPSZ, NULL, BUS_DMA_NOWAIT))
		goto err;

	/* setup receive page link pointers */
	for (rxpp = (struct rxd_4k *)kva, i = 0; i < NRXPAGES; i++, rxpp++) {
		sc->sc_rxd_4k[i] = rxpp;
		rxpp->r4_next = (uint64_t)sc->sc_rxmap->dm_segs[0].ds_addr +
		    (i*sizeof(struct rxd_4k)) + sizeof(struct rxd_4k);
	}
	sc->sc_rxd_4k[NRXPAGES-1]->r4_next =
	    (uint64_t)sc->sc_rxmap->dm_segs[0].ds_addr;

	return (0);

err:
	if (state > 2)
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_rxmap);
	if (state > 1)
		bus_dmamem_unmap(sc->sc_dmat, kva, RXMAPSZ);
	if (state > 0)
		bus_dmamem_free(sc->sc_dmat, &seg, rseg);
	return (ENOBUFS);
}


/*
 * Add a new mbuf chain to descriptor id.
 */
int
xge_add_rxbuf(struct xge_softc *sc, int id)
{
	struct rxdesc *rxd;
	struct mbuf *m[5];
	int page, desc, error;
#if RX_MODE == RX_MODE_5
	int i;
#endif

	page = id/NDESC_BUFMODE;
	desc = id%NDESC_BUFMODE;

	rxd = &sc->sc_rxd_4k[page]->r4_rxd[desc];

	/*
	 * Allocate mbufs.
	 * Currently five mbufs and two clusters are used,
	 * the hardware will put (ethernet, ip, tcp/udp) headers in
	 * their own buffer and the clusters are only used for data.
	 */
#if RX_MODE == RX_MODE_1
	MGETHDR(m[0], M_DONTWAIT, MT_DATA);
	if (m[0] == NULL)
		return (ENOBUFS);
	MCLGETL(m[0], M_DONTWAIT, XGE_MAX_FRAMELEN + ETHER_ALIGN);
	if ((m[0]->m_flags & M_EXT) == 0) {
		m_freem(m[0]);
		return (ENOBUFS);
	}
	m[0]->m_len = m[0]->m_pkthdr.len = XGE_MAX_FRAMELEN + ETHER_ALIGN;
#elif RX_MODE == RX_MODE_3
#error missing rxmode 3.
#elif RX_MODE == RX_MODE_5
	MGETHDR(m[0], M_DONTWAIT, MT_DATA);
	for (i = 1; i < 5; i++) {
		MGET(m[i], M_DONTWAIT, MT_DATA);
	}
	if (m[3])
		MCLGET(m[3], M_DONTWAIT);
	if (m[4])
		MCLGET(m[4], M_DONTWAIT);
	if (!m[0] || !m[1] || !m[2] || !m[3] || !m[4] ||
	    ((m[3]->m_flags & M_EXT) == 0) || ((m[4]->m_flags & M_EXT) == 0)) {
		/* Out of something */
		for (i = 0; i < 5; i++)
			m_free(m[i]);
		return (ENOBUFS);
	}
	/* Link'em together */
	m[0]->m_next = m[1];
	m[1]->m_next = m[2];
	m[2]->m_next = m[3];
	m[3]->m_next = m[4];
#else
#error bad mode RX_MODE
#endif

	if (sc->sc_rxb[id])
		bus_dmamap_unload(sc->sc_dmat, sc->sc_rxm[id]);
	sc->sc_rxb[id] = m[0];

	m_adj(m[0], ETHER_ALIGN);

	error = bus_dmamap_load_mbuf(sc->sc_dmat, sc->sc_rxm[id], m[0],
	    BUS_DMA_READ|BUS_DMA_NOWAIT);
	if (error)
		return (error);
	bus_dmamap_sync(sc->sc_dmat, sc->sc_rxm[id], 0,
	    sc->sc_rxm[id]->dm_mapsize, BUS_DMASYNC_PREREAD);

#if RX_MODE == RX_MODE_1
	rxd->rxd_control2 = RXD_MKCTL2(m[0]->m_len, 0, 0);
	rxd->rxd_buf0 = (uint64_t)sc->sc_rxm[id]->dm_segs[0].ds_addr;
	rxd->rxd_control1 = RXD_CTL1_OWN;
#elif RX_MODE == RX_MODE_3
#elif RX_MODE == RX_MODE_5
	rxd->rxd_control3 = RXD_MKCTL3(0, m[3]->m_len, m[4]->m_len);
	rxd->rxd_control2 = RXD_MKCTL2(m[0]->m_len, m[1]->m_len, m[2]->m_len);
	rxd->rxd_buf0 = (uint64_t)sc->sc_rxm[id]->dm_segs[0].ds_addr;
	rxd->rxd_buf1 = (uint64_t)sc->sc_rxm[id]->dm_segs[1].ds_addr;
	rxd->rxd_buf2 = (uint64_t)sc->sc_rxm[id]->dm_segs[2].ds_addr;
	rxd->rxd_buf3 = (uint64_t)sc->sc_rxm[id]->dm_segs[3].ds_addr;
	rxd->rxd_buf4 = (uint64_t)sc->sc_rxm[id]->dm_segs[4].ds_addr;
	rxd->rxd_control1 = RXD_CTL1_OWN;
#endif

	XGE_RXSYNC(id, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
	return (0);
}

/*
 * This magic comes from the FreeBSD driver.
 */
int
xge_setup_xgxs_xena(struct xge_softc *sc)
{
	int i;

	for (i = 0; i < nitems(xge_xena_dtx_cfg); i++) {
		PIF_WCSR(DTX_CONTROL, xge_xena_dtx_cfg[i]);
		DELAY(100);
	}

	return (0);
}

int
xge_setup_xgxs_herc(struct xge_softc *sc)
{
	int i;

	for (i = 0; i < nitems(xge_herc_dtx_cfg); i++) {
		PIF_WCSR(DTX_CONTROL, xge_herc_dtx_cfg[i]);
		DELAY(100);
	}

	return (0);
}
