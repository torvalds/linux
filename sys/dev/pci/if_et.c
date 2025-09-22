/*	$OpenBSD: if_et.c,v 1.45 2025/07/14 23:49:08 jsg Exp $	*/
/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/dev/netif/et/if_et.c,v 1.1 2007/10/12 14:12:42 sephe Exp $
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <machine/bus.h>
 
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif
 
#include <dev/mii/miivar.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_etreg.h>

/* XXX temporary porting goop */
#define KKASSERT(cond) if (!(cond)) panic("KKASSERT: %s in %s", #cond, __func__)
#undef KASSERT
#define KASSERT(cond, complaint) if (!(cond)) panic complaint

/* these macros in particular need to die, so gross */
#define __LOWEST_SET_BIT(__mask) ((((__mask) - 1) & (__mask)) ^ (__mask))
#define __SHIFTOUT(__x, __mask) (((__x) & (__mask)) / __LOWEST_SET_BIT(__mask))
#define __SHIFTIN(__x, __mask) ((__x) * __LOWEST_SET_BIT(__mask))
/* XXX end porting goop */

int	et_match(struct device *, void *, void *);
void	et_attach(struct device *, struct device *, void *);
int	et_detach(struct device *, int);

int	et_miibus_readreg(struct device *, int, int);
void	et_miibus_writereg(struct device *, int, int, int);
void	et_miibus_statchg(struct device *);

int	et_init(struct ifnet *);
int	et_ioctl(struct ifnet *, u_long, caddr_t);
void	et_start(struct ifnet *);
void	et_watchdog(struct ifnet *);
int	et_ifmedia_upd(struct ifnet *);
void	et_ifmedia_sts(struct ifnet *, struct ifmediareq *);

int	et_intr(void *);
void	et_enable_intrs(struct et_softc *, uint32_t);
void	et_disable_intrs(struct et_softc *);
void	et_rxeof(struct et_softc *);
void	et_txeof(struct et_softc *);
void	et_txtick(void *);

int	et_dma_alloc(struct et_softc *);
void	et_dma_free(struct et_softc *);
int	et_dma_mem_create(struct et_softc *, bus_size_t,
	    void **, bus_addr_t *, bus_dmamap_t *, bus_dma_segment_t *);
void	et_dma_mem_destroy(struct et_softc *, void *, bus_dmamap_t);
int	et_dma_mbuf_create(struct et_softc *);
void	et_dma_mbuf_destroy(struct et_softc *, int, const int[]);

int	et_init_tx_ring(struct et_softc *);
int	et_init_rx_ring(struct et_softc *);
void	et_free_tx_ring(struct et_softc *);
void	et_free_rx_ring(struct et_softc *);
int	et_encap(struct et_softc *, struct mbuf **);
int	et_newbuf(struct et_rxbuf_data *, int, int, int);
int	et_newbuf_cluster(struct et_rxbuf_data *, int, int);
int	et_newbuf_hdr(struct et_rxbuf_data *, int, int);

void	et_stop(struct et_softc *);
int	et_chip_init(struct et_softc *);
void	et_chip_attach(struct et_softc *);
void	et_init_mac(struct et_softc *);
void	et_init_rxmac(struct et_softc *);
void	et_init_txmac(struct et_softc *);
int	et_init_rxdma(struct et_softc *);
int	et_init_txdma(struct et_softc *);
int	et_start_rxdma(struct et_softc *);
int	et_start_txdma(struct et_softc *);
int	et_stop_rxdma(struct et_softc *);
int	et_stop_txdma(struct et_softc *);
int	et_enable_txrx(struct et_softc *);
void	et_reset(struct et_softc *);
int	et_bus_config(struct et_softc *);
void	et_get_eaddr(struct et_softc *, uint8_t[]);
void	et_setmulti(struct et_softc *);
void	et_tick(void *);

static int	et_rx_intr_npkts = 32;
static int	et_rx_intr_delay = 20;		/* x10 usec */
static int	et_tx_intr_nsegs = 128;
static uint32_t	et_timer = 1000 * 1000 * 1000;	/* nanosec */

struct et_bsize {
	int		bufsize;
	et_newbuf_t	newbuf;
};

static const struct et_bsize	et_bufsize[ET_RX_NRING] = {
	{ .bufsize = 0,	.newbuf = et_newbuf_hdr },
	{ .bufsize = 0,	.newbuf = et_newbuf_cluster },
};

const struct pci_matchid et_devices[] = {
	{ PCI_VENDOR_LUCENT, PCI_PRODUCT_LUCENT_ET1310_FE },
	{ PCI_VENDOR_LUCENT, PCI_PRODUCT_LUCENT_ET1310_GBE }
};

const struct cfattach et_ca = {
	sizeof (struct et_softc), et_match, et_attach, et_detach
};

struct cfdriver et_cd = {
	NULL, "et", DV_IFNET
};

int
et_match(struct device *dev, void *match, void *aux)
{
	return pci_matchbyid((struct pci_attach_args *)aux, et_devices,
	    sizeof (et_devices) / sizeof (et_devices[0]));
}

void
et_attach(struct device *parent, struct device *self, void *aux)
{
	struct et_softc *sc = (struct et_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	pcireg_t memtype;
	int error;

	/*
	 * Initialize tunables
	 */
	sc->sc_rx_intr_npkts = et_rx_intr_npkts;
	sc->sc_rx_intr_delay = et_rx_intr_delay;
	sc->sc_tx_intr_nsegs = et_tx_intr_nsegs;
	sc->sc_timer = et_timer;

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, ET_PCIR_BAR);
	if (pci_mapreg_map(pa, ET_PCIR_BAR, memtype, 0, &sc->sc_mem_bt,
	    &sc->sc_mem_bh, NULL, &sc->sc_mem_size, 0)) {
		printf(": can't map mem space\n");
		return;
	}

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": can't map interrupt\n");
		return;
	}

	intrstr = pci_intr_string(pc, ih);
	sc->sc_irq_handle = pci_intr_establish(pc, ih, IPL_NET, et_intr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_irq_handle == NULL) {
		printf(": could not establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s", intrstr);

	sc->sc_dmat = pa->pa_dmat;
	sc->sc_pct = pa->pa_pc;
	sc->sc_pcitag = pa->pa_tag;

	error = et_bus_config(sc);
	if (error)
		return;

	et_get_eaddr(sc, sc->sc_arpcom.ac_enaddr);

	printf(", address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));

	CSR_WRITE_4(sc, ET_PM,
		    ET_PM_SYSCLK_GATE | ET_PM_TXCLK_GATE | ET_PM_RXCLK_GATE);

	et_reset(sc);

	et_disable_intrs(sc);

	error = et_dma_alloc(sc);
	if (error)
		return;

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = et_ioctl;
	ifp->if_start = et_start;
	ifp->if_watchdog = et_watchdog;
	ifq_init_maxlen(&ifp->if_snd, ET_TX_NDESC);
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	et_chip_attach(sc);

	sc->sc_miibus.mii_ifp = ifp;
	sc->sc_miibus.mii_readreg = et_miibus_readreg;
	sc->sc_miibus.mii_writereg = et_miibus_writereg;
	sc->sc_miibus.mii_statchg = et_miibus_statchg;

	ifmedia_init(&sc->sc_miibus.mii_media, 0, et_ifmedia_upd,
	    et_ifmedia_sts);
	mii_attach(self, &sc->sc_miibus, 0xffffffff, MII_PHY_ANY,
	    MII_OFFSET_ANY, 0);
	if (LIST_FIRST(&sc->sc_miibus.mii_phys) == NULL) {
		printf("%s: no PHY found!\n", sc->sc_dev.dv_xname);
		ifmedia_add(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_MANUAL,
		    0, NULL);
		ifmedia_set(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_MANUAL);
	} else
		ifmedia_set(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->sc_tick, et_tick, sc);
	timeout_set(&sc->sc_txtick, et_txtick, sc);
}

int
et_detach(struct device *self, int flags)
{
	struct et_softc *sc = (struct et_softc *)self;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int s;

	s = splnet();
	et_stop(sc);
	splx(s);

	mii_detach(&sc->sc_miibus, MII_PHY_ANY, MII_OFFSET_ANY);

	/* Delete all remaining media. */
	ifmedia_delete_instance(&sc->sc_miibus.mii_media, IFM_INST_ANY);

	ether_ifdetach(ifp);
	if_detach(ifp);
	et_dma_free(sc);

	if (sc->sc_irq_handle != NULL) {
		pci_intr_disestablish(sc->sc_pct, sc->sc_irq_handle);
		sc->sc_irq_handle = NULL;
	}

	bus_space_unmap(sc->sc_mem_bt, sc->sc_mem_bh, sc->sc_mem_size);

	return 0;
}

int
et_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct et_softc *sc = (struct et_softc *)dev;
	uint32_t val;
	int i, ret;

	/* Stop any pending operations */
	CSR_WRITE_4(sc, ET_MII_CMD, 0);

	val = __SHIFTIN(phy, ET_MII_ADDR_PHY) |
	      __SHIFTIN(reg, ET_MII_ADDR_REG);
	CSR_WRITE_4(sc, ET_MII_ADDR, val);

	/* Start reading */
	CSR_WRITE_4(sc, ET_MII_CMD, ET_MII_CMD_READ);

#define NRETRY	50

	for (i = 0; i < NRETRY; ++i) {
		val = CSR_READ_4(sc, ET_MII_IND);
		if ((val & (ET_MII_IND_BUSY | ET_MII_IND_INVALID)) == 0)
			break;
		DELAY(50);
	}
	if (i == NRETRY) {
		printf("%s: read phy %d, reg %d timed out\n",
		    sc->sc_dev.dv_xname, phy, reg);
		ret = 0;
		goto back;
	}

#undef NRETRY

	val = CSR_READ_4(sc, ET_MII_STAT);
	ret = __SHIFTOUT(val, ET_MII_STAT_VALUE);

back:
	/* Make sure that the current operation is stopped */
	CSR_WRITE_4(sc, ET_MII_CMD, 0);
	return ret;
}

void
et_miibus_writereg(struct device *dev, int phy, int reg, int val0)
{
	struct et_softc *sc = (struct et_softc *)dev;
	uint32_t val;
	int i;

	/* Stop any pending operations */
	CSR_WRITE_4(sc, ET_MII_CMD, 0);

	val = __SHIFTIN(phy, ET_MII_ADDR_PHY) |
	      __SHIFTIN(reg, ET_MII_ADDR_REG);
	CSR_WRITE_4(sc, ET_MII_ADDR, val);

	/* Start writing */
	CSR_WRITE_4(sc, ET_MII_CTRL, __SHIFTIN(val0, ET_MII_CTRL_VALUE));

#define NRETRY 100

	for (i = 0; i < NRETRY; ++i) {
		val = CSR_READ_4(sc, ET_MII_IND);
		if ((val & ET_MII_IND_BUSY) == 0)
			break;
		DELAY(50);
	}
	if (i == NRETRY) {
		printf("%s: write phy %d, reg %d timed out\n",
		    sc->sc_dev.dv_xname,  phy, reg);
		et_miibus_readreg(dev, phy, reg);
	}

#undef NRETRY

	/* Make sure that the current operation is stopped */
	CSR_WRITE_4(sc, ET_MII_CMD, 0);
}

void
et_miibus_statchg(struct device *dev)
{
	struct et_softc *sc = (struct et_softc *)dev;
	struct mii_data *mii = &sc->sc_miibus;
	uint32_t cfg2, ctrl;

	cfg2 = CSR_READ_4(sc, ET_MAC_CFG2);
	cfg2 &= ~(ET_MAC_CFG2_MODE_MII | ET_MAC_CFG2_MODE_GMII |
		  ET_MAC_CFG2_FDX | ET_MAC_CFG2_BIGFRM);
	cfg2 |= ET_MAC_CFG2_LENCHK | ET_MAC_CFG2_CRC | ET_MAC_CFG2_PADCRC |
		__SHIFTIN(7, ET_MAC_CFG2_PREAMBLE_LEN);

	ctrl = CSR_READ_4(sc, ET_MAC_CTRL);
	ctrl &= ~(ET_MAC_CTRL_GHDX | ET_MAC_CTRL_MODE_MII);

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T) {
		cfg2 |= ET_MAC_CFG2_MODE_GMII;
	} else {
		cfg2 |= ET_MAC_CFG2_MODE_MII;
		ctrl |= ET_MAC_CTRL_MODE_MII;
	}

	if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX)
		cfg2 |= ET_MAC_CFG2_FDX;
	else
		ctrl |= ET_MAC_CTRL_GHDX;

	CSR_WRITE_4(sc, ET_MAC_CTRL, ctrl);
	CSR_WRITE_4(sc, ET_MAC_CFG2, cfg2);
}

int
et_ifmedia_upd(struct ifnet *ifp)
{
	struct et_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_miibus;

	if (mii->mii_instance != 0) {
		struct mii_softc *miisc;

		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	mii_mediachg(mii);

	return 0;
}

void
et_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct et_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_miibus;

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

void
et_stop(struct et_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	timeout_del(&sc->sc_tick);
	timeout_del(&sc->sc_txtick);

	et_stop_rxdma(sc);
	et_stop_txdma(sc);

	et_disable_intrs(sc);

	et_free_tx_ring(sc);
	et_free_rx_ring(sc);

	et_reset(sc);

	sc->sc_tx = 0;
	sc->sc_tx_intr = 0;

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
}

int
et_bus_config(struct et_softc *sc)
{
	uint32_t val; //, max_plsz;
//	uint16_t ack_latency, replay_timer;

	/*
	 * Test whether EEPROM is valid
	 * NOTE: Read twice to get the correct value
	 */
	pci_conf_read(sc->sc_pct, sc->sc_pcitag, ET_PCIR_EEPROM_MISC);
	val = pci_conf_read(sc->sc_pct, sc->sc_pcitag, ET_PCIR_EEPROM_MISC);
	
	if (val & ET_PCIM_EEPROM_STATUS_ERROR) {
		printf("%s: EEPROM status error 0x%02x\n",
		    sc->sc_dev.dv_xname, val);
		return ENXIO;
	}

	/* TODO: LED */
#if 0
	/*
	 * Configure ACK latency and replay timer according to
	 * max payload size
	 */
	val = pci_conf_read(sc->sc_pct, sc->sc_pcitag, ET_PCIR_DEVICE_CAPS);
	max_plsz = val & ET_PCIM_DEVICE_CAPS_MAX_PLSZ;

	switch (max_plsz) {
	case ET_PCIV_DEVICE_CAPS_PLSZ_128:
		ack_latency = ET_PCIV_ACK_LATENCY_128;
		replay_timer = ET_PCIV_REPLAY_TIMER_128;
		break;

	case ET_PCIV_DEVICE_CAPS_PLSZ_256:
		ack_latency = ET_PCIV_ACK_LATENCY_256;
		replay_timer = ET_PCIV_REPLAY_TIMER_256;
		break;

	default:
		ack_latency = pci_conf_read(sc->sc_pct, sc->sc_pcitag,
		    ET_PCIR_ACK_LATENCY) >> 16;
		replay_timer = pci_conf_read(sc->sc_pct, sc->sc_pcitag,
		    ET_PCIR_REPLAY_TIMER) >> 16;
		printf("%s: ack latency %u, replay timer %u\n",
		    sc->sc_dev.dv_xname, ack_latency, replay_timer);
		break;
	}
	if (ack_latency != 0) {
		pci_conf_write(sc->sc_pct, sc->sc_pcitag,
		    ET_PCIR_ACK_LATENCY, ack_latency << 16);
		pci_conf_write(sc->sc_pct, sc->sc_pcitag,
		    ET_PCIR_REPLAY_TIMER, replay_timer << 16);
	}

	/*
	 * Set L0s and L1 latency timer to 2us
	 */
	val = ET_PCIV_L0S_LATENCY(2) | ET_PCIV_L1_LATENCY(2);
	pci_conf_write(sc->sc_pct, sc->sc_pcitag, ET_PCIR_L0S_L1_LATENCY,
	    val << 24);

	/*
	 * Set max read request size to 2048 bytes
	 */
	val = pci_conf_read(sc->sc_pct, sc->sc_pcitag,
	    ET_PCIR_DEVICE_CTRL) >> 16;
	val &= ~ET_PCIM_DEVICE_CTRL_MAX_RRSZ;
	val |= ET_PCIV_DEVICE_CTRL_RRSZ_2K;
	pci_conf_write(sc->sc_pct, sc->sc_pcitag, ET_PCIR_DEVICE_CTRL,
	    val << 16);
#endif

	return 0;
}

void
et_get_eaddr(struct et_softc *sc, uint8_t eaddr[])
{
	uint32_t r;

	r = pci_conf_read(sc->sc_pct, sc->sc_pcitag, ET_PCIR_MACADDR_LO);
	eaddr[0] = r & 0xff;
	eaddr[1] = (r >> 8) & 0xff;
	eaddr[2] = (r >> 16) & 0xff;
	eaddr[3] = (r >> 24) & 0xff;
	r = pci_conf_read(sc->sc_pct, sc->sc_pcitag, ET_PCIR_MACADDR_HI);
	eaddr[4] = r & 0xff;
	eaddr[5] = (r >> 8) & 0xff;
}

void
et_reset(struct et_softc *sc)
{
	CSR_WRITE_4(sc, ET_MAC_CFG1,
		    ET_MAC_CFG1_RST_TXFUNC | ET_MAC_CFG1_RST_RXFUNC |
		    ET_MAC_CFG1_RST_TXMC | ET_MAC_CFG1_RST_RXMC |
		    ET_MAC_CFG1_SIM_RST | ET_MAC_CFG1_SOFT_RST);

	CSR_WRITE_4(sc, ET_SWRST,
		    ET_SWRST_TXDMA | ET_SWRST_RXDMA |
		    ET_SWRST_TXMAC | ET_SWRST_RXMAC |
		    ET_SWRST_MAC | ET_SWRST_MAC_STAT | ET_SWRST_MMC);

	CSR_WRITE_4(sc, ET_MAC_CFG1,
		    ET_MAC_CFG1_RST_TXFUNC | ET_MAC_CFG1_RST_RXFUNC |
		    ET_MAC_CFG1_RST_TXMC | ET_MAC_CFG1_RST_RXMC);
	CSR_WRITE_4(sc, ET_MAC_CFG1, 0);
}

void
et_disable_intrs(struct et_softc *sc)
{
	CSR_WRITE_4(sc, ET_INTR_MASK, 0xffffffff);
}

void
et_enable_intrs(struct et_softc *sc, uint32_t intrs)
{
	CSR_WRITE_4(sc, ET_INTR_MASK, ~intrs);
}

int
et_dma_alloc(struct et_softc *sc)
{
	struct et_txdesc_ring *tx_ring = &sc->sc_tx_ring;
	struct et_txstatus_data *txsd = &sc->sc_tx_status;
	struct et_rxstat_ring *rxst_ring = &sc->sc_rxstat_ring;
	struct et_rxstatus_data *rxsd = &sc->sc_rx_status;
	int i, error;

	/*
	 * Create TX ring DMA stuffs
	 */
	error = et_dma_mem_create(sc, ET_TX_RING_SIZE,
	    (void **)&tx_ring->tr_desc, &tx_ring->tr_paddr, &tx_ring->tr_dmap,
	    &tx_ring->tr_seg);
	if (error) {
		printf("%s: can't create TX ring DMA stuffs\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	/*
	 * Create TX status DMA stuffs
	 */
	error = et_dma_mem_create(sc, sizeof(uint32_t),
	    (void **)&txsd->txsd_status,
	    &txsd->txsd_paddr, &txsd->txsd_dmap, &txsd->txsd_seg);
	if (error) {
		printf("%s: can't create TX status DMA stuffs\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	/*
	 * Create DMA stuffs for RX rings
	 */
	for (i = 0; i < ET_RX_NRING; ++i) {
		static const uint32_t rx_ring_posreg[ET_RX_NRING] =
		{ ET_RX_RING0_POS, ET_RX_RING1_POS };

		struct et_rxdesc_ring *rx_ring = &sc->sc_rx_ring[i];

		error = et_dma_mem_create(sc, ET_RX_RING_SIZE,
		    (void **)&rx_ring->rr_desc,
		    &rx_ring->rr_paddr, &rx_ring->rr_dmap, &rx_ring->rr_seg);
		if (error) {
			printf("%s: can't create DMA stuffs for "
			    "the %d RX ring\n", sc->sc_dev.dv_xname, i);
			return error;
		}
		rx_ring->rr_posreg = rx_ring_posreg[i];
	}

	/*
	 * Create RX stat ring DMA stuffs
	 */
	error = et_dma_mem_create(sc, ET_RXSTAT_RING_SIZE,
	    (void **)&rxst_ring->rsr_stat,
	    &rxst_ring->rsr_paddr, &rxst_ring->rsr_dmap, &rxst_ring->rsr_seg);
	if (error) {
		printf("%s: can't create RX stat ring DMA stuffs\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	/*
	 * Create RX status DMA stuffs
	 */
	error = et_dma_mem_create(sc, sizeof(struct et_rxstatus),
	    (void **)&rxsd->rxsd_status,
	    &rxsd->rxsd_paddr, &rxsd->rxsd_dmap, &rxsd->rxsd_seg);
	if (error) {
		printf("%s: can't create RX status DMA stuffs\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	/*
	 * Create mbuf DMA stuffs
	 */
	error = et_dma_mbuf_create(sc);
	if (error)
		return error;

	return 0;
}

void
et_dma_free(struct et_softc *sc)
{
	struct et_txdesc_ring *tx_ring = &sc->sc_tx_ring;
	struct et_txstatus_data *txsd = &sc->sc_tx_status;
	struct et_rxstat_ring *rxst_ring = &sc->sc_rxstat_ring;
	struct et_rxstatus_data *rxsd = &sc->sc_rx_status;
	int i, rx_done[ET_RX_NRING];

	/*
	 * Destroy TX ring DMA stuffs
	 */
	et_dma_mem_destroy(sc, tx_ring->tr_desc, tx_ring->tr_dmap);

	/*
	 * Destroy TX status DMA stuffs
	 */
	et_dma_mem_destroy(sc, txsd->txsd_status, txsd->txsd_dmap);

	/*
	 * Destroy DMA stuffs for RX rings
	 */
	for (i = 0; i < ET_RX_NRING; ++i) {
		struct et_rxdesc_ring *rx_ring = &sc->sc_rx_ring[i];

		et_dma_mem_destroy(sc, rx_ring->rr_desc, rx_ring->rr_dmap);
	}

	/*
	 * Destroy RX stat ring DMA stuffs
	 */
	et_dma_mem_destroy(sc, rxst_ring->rsr_stat, rxst_ring->rsr_dmap);
			   
	/*
	 * Destroy RX status DMA stuffs
	 */
	et_dma_mem_destroy(sc, rxsd->rxsd_status, rxsd->rxsd_dmap);

	/*
	 * Destroy mbuf DMA stuffs
	 */
	for (i = 0; i < ET_RX_NRING; ++i)
		rx_done[i] = ET_RX_NDESC;
	et_dma_mbuf_destroy(sc, ET_TX_NDESC, rx_done);
}

int
et_dma_mbuf_create(struct et_softc *sc)
{
	struct et_txbuf_data *tbd = &sc->sc_tx_data;
	int i, error, rx_done[ET_RX_NRING];

	/*
	 * Create spare DMA map for RX mbufs
	 */
	error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES, 0,
	    BUS_DMA_NOWAIT, &sc->sc_mbuf_tmp_dmap);
	if (error) {
		printf("%s: can't create spare mbuf DMA map\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	/*
	 * Create DMA maps for RX mbufs
	 */
	bzero(rx_done, sizeof(rx_done));
	for (i = 0; i < ET_RX_NRING; ++i) {
		struct et_rxbuf_data *rbd = &sc->sc_rx_data[i];
		int j;

		for (j = 0; j < ET_RX_NDESC; ++j) {
			error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
			    MCLBYTES, 0, BUS_DMA_NOWAIT,
			    &rbd->rbd_buf[j].rb_dmap);
			if (error) {
				printf("%s: can't create %d RX mbuf "
				    "for %d RX ring\n", sc->sc_dev.dv_xname,
				    j, i);
				rx_done[i] = j;
				et_dma_mbuf_destroy(sc, 0, rx_done);
				return error;
			}
		}
		rx_done[i] = ET_RX_NDESC;

		rbd->rbd_softc = sc;
		rbd->rbd_ring = &sc->sc_rx_ring[i];
	}

	/*
	 * Create DMA maps for TX mbufs
	 */
	for (i = 0; i < ET_TX_NDESC; ++i) {
		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES,
		    0, BUS_DMA_NOWAIT, &tbd->tbd_buf[i].tb_dmap);
		if (error) {
			printf("%s: can't create %d TX mbuf "
			    "DMA map\n", sc->sc_dev.dv_xname, i);
			et_dma_mbuf_destroy(sc, i, rx_done);
			return error;
		}
	}

	return 0;
}

void
et_dma_mbuf_destroy(struct et_softc *sc, int tx_done, const int rx_done[])
{
	struct et_txbuf_data *tbd = &sc->sc_tx_data;
	int i;

	/*
	 * Destroy DMA maps for RX mbufs
	 */
	for (i = 0; i < ET_RX_NRING; ++i) {
		struct et_rxbuf_data *rbd = &sc->sc_rx_data[i];
		int j;

		for (j = 0; j < rx_done[i]; ++j) {
			struct et_rxbuf *rb = &rbd->rbd_buf[j];

			KASSERT(rb->rb_mbuf == NULL,
			    ("RX mbuf in %d RX ring is not freed yet\n", i));
			bus_dmamap_destroy(sc->sc_dmat, rb->rb_dmap); 
		}
	}

	/*
	 * Destroy DMA maps for TX mbufs
	 */
	for (i = 0; i < tx_done; ++i) {
		struct et_txbuf *tb = &tbd->tbd_buf[i];

		KASSERT(tb->tb_mbuf == NULL, ("TX mbuf is not freed yet\n"));
		bus_dmamap_destroy(sc->sc_dmat, tb->tb_dmap);
	}

	/*
	 * Destroy spare mbuf DMA map
	 */
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_mbuf_tmp_dmap);
}

int
et_dma_mem_create(struct et_softc *sc, bus_size_t size,
    void **addr, bus_addr_t *paddr, bus_dmamap_t *dmap, bus_dma_segment_t *seg)
{
	int error, nsegs;

	error = bus_dmamap_create(sc->sc_dmat, size, 1, size, 0, BUS_DMA_NOWAIT,
	    dmap);
	if (error) {
		printf("%s: can't create DMA map\n", sc->sc_dev.dv_xname);
		return error;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, size, ET_ALIGN, 0, seg,
	    1, &nsegs, BUS_DMA_WAITOK | BUS_DMA_ZERO);
	if (error) {
		printf("%s: can't allocate DMA mem\n", sc->sc_dev.dv_xname);
		return error;
	}

	error = bus_dmamem_map(sc->sc_dmat, seg, nsegs,
	    size, (caddr_t *)addr, BUS_DMA_NOWAIT);
	if (error) {
		printf("%s: can't map DMA mem\n", sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamap_load(sc->sc_dmat, *dmap, *addr, size, NULL,
	    BUS_DMA_WAITOK);
	if (error) {
		printf("%s: can't load DMA mem\n", sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat, (bus_dma_segment_t *)addr, 1);
		return error;
	}

	*paddr = (*dmap)->dm_segs[0].ds_addr;

	return 0;
}

void
et_dma_mem_destroy(struct et_softc *sc, void *addr, bus_dmamap_t dmap)
{
	bus_dmamap_unload(sc->sc_dmat, dmap);
	bus_dmamem_free(sc->sc_dmat, (bus_dma_segment_t *)&addr, 1);
}

void
et_chip_attach(struct et_softc *sc)
{
	uint32_t val;

	/*
	 * Perform minimal initialization
	 */

	/* Disable loopback */
	CSR_WRITE_4(sc, ET_LOOPBACK, 0);

	/* Reset MAC */
	CSR_WRITE_4(sc, ET_MAC_CFG1,
		    ET_MAC_CFG1_RST_TXFUNC | ET_MAC_CFG1_RST_RXFUNC |
		    ET_MAC_CFG1_RST_TXMC | ET_MAC_CFG1_RST_RXMC |
		    ET_MAC_CFG1_SIM_RST | ET_MAC_CFG1_SOFT_RST);

	/*
	 * Setup half duplex mode
	 */
	val = __SHIFTIN(10, ET_MAC_HDX_ALT_BEB_TRUNC) |
	      __SHIFTIN(15, ET_MAC_HDX_REXMIT_MAX) |
	      __SHIFTIN(55, ET_MAC_HDX_COLLWIN) |
	      ET_MAC_HDX_EXC_DEFER;
	CSR_WRITE_4(sc, ET_MAC_HDX, val);

	/* Clear MAC control */
	CSR_WRITE_4(sc, ET_MAC_CTRL, 0);

	/* Reset MII */
	CSR_WRITE_4(sc, ET_MII_CFG, ET_MII_CFG_CLKRST);

	/* Bring MAC out of reset state */
	CSR_WRITE_4(sc, ET_MAC_CFG1, 0);

	/* Enable memory controllers */
	CSR_WRITE_4(sc, ET_MMC_CTRL, ET_MMC_CTRL_ENABLE);
}

int
et_intr(void *xsc)
{
	struct et_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint32_t intrs;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return (0);

	intrs = CSR_READ_4(sc, ET_INTR_STATUS);
	if (intrs == 0 || intrs == 0xffffffff)
		return (0);

	et_disable_intrs(sc);
	intrs &= ET_INTRS;
	if (intrs == 0)	/* Not interested */
		goto back;

	if (intrs & ET_INTR_RXEOF)
		et_rxeof(sc);
	if (intrs & (ET_INTR_TXEOF | ET_INTR_TIMER))
		et_txeof(sc);
	if (intrs & ET_INTR_TIMER)
		CSR_WRITE_4(sc, ET_TIMER, sc->sc_timer);
back:
	et_enable_intrs(sc, ET_INTRS);

	return (1);
}

int
et_init(struct ifnet *ifp)
{
	struct et_softc *sc = ifp->if_softc;
	int error, i, s;

	s = splnet();

	et_stop(sc);

	for (i = 0; i < ET_RX_NRING; ++i) {
		sc->sc_rx_data[i].rbd_bufsize = et_bufsize[i].bufsize;
		sc->sc_rx_data[i].rbd_newbuf = et_bufsize[i].newbuf;
	}

	error = et_init_tx_ring(sc);
	if (error)
		goto back;

	error = et_init_rx_ring(sc);
	if (error)
		goto back;

	error = et_chip_init(sc);
	if (error)
		goto back;

	error = et_enable_txrx(sc);
	if (error)
		goto back;

	error = et_start_rxdma(sc);
	if (error)
		goto back;

	error = et_start_txdma(sc);
	if (error)
		goto back;

	et_enable_intrs(sc, ET_INTRS);

	timeout_add_sec(&sc->sc_tick, 1);

	CSR_WRITE_4(sc, ET_TIMER, sc->sc_timer);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
back:
	if (error)
		et_stop(sc);

	splx(s);

	return (0);
}

int
et_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct et_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			et_init(ifp);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			/*
			 * If only the PROMISC or ALLMULTI flag changes, then
			 * don't do a full re-init of the chip, just update
			 * the Rx filter.
			 */
			if ((ifp->if_flags & IFF_RUNNING) &&
			    ((ifp->if_flags ^ sc->sc_if_flags) &
			     (IFF_ALLMULTI | IFF_PROMISC)) != 0) {
				et_setmulti(sc);
			} else {
				if (!(ifp->if_flags & IFF_RUNNING))
					et_init(ifp);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				et_stop(sc);
		}
		sc->sc_if_flags = ifp->if_flags;
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_miibus.mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			et_setmulti(sc);
		error = 0;
	}

	splx(s);
	return error;
}

void
et_start(struct ifnet *ifp)
{
	struct et_softc *sc = ifp->if_softc;
	struct et_txbuf_data *tbd = &sc->sc_tx_data;
	int trans;
	struct mbuf *m;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	trans = 0;
	for (;;) {
		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;

		if ((tbd->tbd_used + ET_NSEG_SPARE) > ET_TX_NDESC) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		if (et_encap(sc, &m)) {
			ifp->if_oerrors++;
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		trans = 1;

#if NBPFILTER > 0
		if (ifp->if_bpf != NULL)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
	}

	if (trans) {
		timeout_add_sec(&sc->sc_txtick, 1);
		ifp->if_timer = 5;
	}
}

void
et_watchdog(struct ifnet *ifp)
{
	struct et_softc *sc = ifp->if_softc;
	printf("%s: watchdog timed out\n", sc->sc_dev.dv_xname);

	et_init(ifp);
	et_start(ifp);
}

int
et_stop_rxdma(struct et_softc *sc)
{
	CSR_WRITE_4(sc, ET_RXDMA_CTRL,
		    ET_RXDMA_CTRL_HALT | ET_RXDMA_CTRL_RING1_ENABLE);

	DELAY(5);
	if ((CSR_READ_4(sc, ET_RXDMA_CTRL) & ET_RXDMA_CTRL_HALTED) == 0) {
		printf("%s: can't stop RX DMA engine\n", sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}
	return 0;
}

int
et_stop_txdma(struct et_softc *sc)
{
	CSR_WRITE_4(sc, ET_TXDMA_CTRL,
		    ET_TXDMA_CTRL_HALT | ET_TXDMA_CTRL_SINGLE_EPKT);
	return 0;
}

void
et_free_tx_ring(struct et_softc *sc)
{
	struct et_txbuf_data *tbd = &sc->sc_tx_data;
	struct et_txdesc_ring *tx_ring = &sc->sc_tx_ring;
	int i;

	for (i = 0; i < ET_TX_NDESC; ++i) {
		struct et_txbuf *tb = &tbd->tbd_buf[i];

		if (tb->tb_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat, tb->tb_dmap);
			m_freem(tb->tb_mbuf);
			tb->tb_mbuf = NULL;
		}
	}

	bzero(tx_ring->tr_desc, ET_TX_RING_SIZE);
	bus_dmamap_sync(sc->sc_dmat, tx_ring->tr_dmap, 0,
	    tx_ring->tr_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);
}

void
et_free_rx_ring(struct et_softc *sc)
{
	int n;

	for (n = 0; n < ET_RX_NRING; ++n) {
		struct et_rxbuf_data *rbd = &sc->sc_rx_data[n];
		struct et_rxdesc_ring *rx_ring = &sc->sc_rx_ring[n];
		int i;

		for (i = 0; i < ET_RX_NDESC; ++i) {
			struct et_rxbuf *rb = &rbd->rbd_buf[i];

			if (rb->rb_mbuf != NULL) {
				bus_dmamap_unload(sc->sc_dmat, rb->rb_dmap);
				m_freem(rb->rb_mbuf);
				rb->rb_mbuf = NULL;
			}
		}

		bzero(rx_ring->rr_desc, ET_RX_RING_SIZE);
		bus_dmamap_sync(sc->sc_dmat, rx_ring->rr_dmap, 0,
		    rx_ring->rr_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);
	}
}

void
et_setmulti(struct et_softc *sc)
{
	struct arpcom *ac = &sc->sc_arpcom;
	struct ifnet *ifp = &ac->ac_if;
	uint32_t hash[4] = { 0, 0, 0, 0 };
	uint32_t rxmac_ctrl, pktfilt;
	struct ether_multi *enm;
	struct ether_multistep step;
	int i, count;

	pktfilt = CSR_READ_4(sc, ET_PKTFILT);
	rxmac_ctrl = CSR_READ_4(sc, ET_RXMAC_CTRL);

	pktfilt &= ~(ET_PKTFILT_BCAST | ET_PKTFILT_MCAST | ET_PKTFILT_UCAST);
	if (ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI)) {
		rxmac_ctrl |= ET_RXMAC_CTRL_NO_PKTFILT;
		goto back;
	}

	count = 0;
	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		uint32_t *hp, h;

		h = ether_crc32_be(enm->enm_addrlo, ETHER_ADDR_LEN);
		h = (h & 0x3f800000) >> 23;

		hp = &hash[0];
		if (h >= 32 && h < 64) {
			h -= 32;
			hp = &hash[1];
		} else if (h >= 64 && h < 96) {
			h -= 64;
			hp = &hash[2];
		} else if (h >= 96) {
			h -= 96;
			hp = &hash[3];
		}
		*hp |= (1 << h);

		++count;
		ETHER_NEXT_MULTI(step, enm);
	}

	for (i = 0; i < 4; ++i)
		CSR_WRITE_4(sc, ET_MULTI_HASH + (i * 4), hash[i]);

	if (count > 0)
		pktfilt |= ET_PKTFILT_MCAST;
	rxmac_ctrl &= ~ET_RXMAC_CTRL_NO_PKTFILT;
back:
	CSR_WRITE_4(sc, ET_PKTFILT, pktfilt);
	CSR_WRITE_4(sc, ET_RXMAC_CTRL, rxmac_ctrl);
}

int
et_chip_init(struct et_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint32_t rxq_end;
	int error;

	/*
	 * Split internal memory between TX and RX according to MTU
	 */
	if (ifp->if_hardmtu < 2048)
		rxq_end = 0x2bc;
	else if (ifp->if_hardmtu < 8192)
		rxq_end = 0x1ff;
	else
		rxq_end = 0x1b3;
	CSR_WRITE_4(sc, ET_RXQ_START, 0);
	CSR_WRITE_4(sc, ET_RXQ_END, rxq_end);
	CSR_WRITE_4(sc, ET_TXQ_START, rxq_end + 1);
	CSR_WRITE_4(sc, ET_TXQ_END, ET_INTERN_MEM_END);

	/* No loopback */
	CSR_WRITE_4(sc, ET_LOOPBACK, 0);

	/* Clear MSI configure */
	CSR_WRITE_4(sc, ET_MSI_CFG, 0);

	/* Disable timer */
	CSR_WRITE_4(sc, ET_TIMER, 0);

	/* Initialize MAC */
	et_init_mac(sc);

	/* Enable memory controllers */
	CSR_WRITE_4(sc, ET_MMC_CTRL, ET_MMC_CTRL_ENABLE);

	/* Initialize RX MAC */
	et_init_rxmac(sc);

	/* Initialize TX MAC */
	et_init_txmac(sc);

	/* Initialize RX DMA engine */
	error = et_init_rxdma(sc);
	if (error)
		return error;

	/* Initialize TX DMA engine */
	error = et_init_txdma(sc);
	if (error)
		return error;

	return 0;
}

int
et_init_tx_ring(struct et_softc *sc)
{
	struct et_txdesc_ring *tx_ring = &sc->sc_tx_ring;
	struct et_txstatus_data *txsd = &sc->sc_tx_status;
	struct et_txbuf_data *tbd = &sc->sc_tx_data;

	bzero(tx_ring->tr_desc, ET_TX_RING_SIZE);
	bus_dmamap_sync(sc->sc_dmat, tx_ring->tr_dmap, 0,
	    tx_ring->tr_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	tbd->tbd_start_index = 0;
	tbd->tbd_start_wrap = 0;
	tbd->tbd_used = 0;

	bzero(txsd->txsd_status, sizeof(uint32_t));
	bus_dmamap_sync(sc->sc_dmat, txsd->txsd_dmap, 0,
	    txsd->txsd_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);
	return 0;
}

int
et_init_rx_ring(struct et_softc *sc)
{
	struct et_rxstatus_data *rxsd = &sc->sc_rx_status;
	struct et_rxstat_ring *rxst_ring = &sc->sc_rxstat_ring;
	int n;

	for (n = 0; n < ET_RX_NRING; ++n) {
		struct et_rxbuf_data *rbd = &sc->sc_rx_data[n];
		int i, error;

		for (i = 0; i < ET_RX_NDESC; ++i) {
			error = rbd->rbd_newbuf(rbd, i, 1);
			if (error) {
				printf("%s: %d ring %d buf, newbuf failed: "
				    "%d\n", sc->sc_dev.dv_xname, n, i, error);
				return error;
			}
		}
	}

	bzero(rxsd->rxsd_status, sizeof(struct et_rxstatus));
	bus_dmamap_sync(sc->sc_dmat, rxsd->rxsd_dmap, 0,
	    rxsd->rxsd_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	bzero(rxst_ring->rsr_stat, ET_RXSTAT_RING_SIZE);
	bus_dmamap_sync(sc->sc_dmat, rxst_ring->rsr_dmap, 0,
	    rxst_ring->rsr_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	return 0;
}

int
et_init_rxdma(struct et_softc *sc)
{
	struct et_rxstatus_data *rxsd = &sc->sc_rx_status;
	struct et_rxstat_ring *rxst_ring = &sc->sc_rxstat_ring;
	struct et_rxdesc_ring *rx_ring;
	int error;

	error = et_stop_rxdma(sc);
	if (error) {
		printf("%s: can't init RX DMA engine\n", sc->sc_dev.dv_xname);
		return error;
	}

	/*
	 * Install RX status
	 */
	CSR_WRITE_4(sc, ET_RX_STATUS_HI, ET_ADDR_HI(rxsd->rxsd_paddr));
	CSR_WRITE_4(sc, ET_RX_STATUS_LO, ET_ADDR_LO(rxsd->rxsd_paddr));

	/*
	 * Install RX stat ring
	 */
	CSR_WRITE_4(sc, ET_RXSTAT_HI, ET_ADDR_HI(rxst_ring->rsr_paddr));
	CSR_WRITE_4(sc, ET_RXSTAT_LO, ET_ADDR_LO(rxst_ring->rsr_paddr));
	CSR_WRITE_4(sc, ET_RXSTAT_CNT, ET_RX_NSTAT - 1);
	CSR_WRITE_4(sc, ET_RXSTAT_POS, 0);
	CSR_WRITE_4(sc, ET_RXSTAT_MINCNT, ((ET_RX_NSTAT * 15) / 100) - 1);

	/* Match ET_RXSTAT_POS */
	rxst_ring->rsr_index = 0;
	rxst_ring->rsr_wrap = 0;

	/*
	 * Install the 2nd RX descriptor ring
	 */
	rx_ring = &sc->sc_rx_ring[1];
	CSR_WRITE_4(sc, ET_RX_RING1_HI, ET_ADDR_HI(rx_ring->rr_paddr));
	CSR_WRITE_4(sc, ET_RX_RING1_LO, ET_ADDR_LO(rx_ring->rr_paddr));
	CSR_WRITE_4(sc, ET_RX_RING1_CNT, ET_RX_NDESC - 1);
	CSR_WRITE_4(sc, ET_RX_RING1_POS, ET_RX_RING1_POS_WRAP);
	CSR_WRITE_4(sc, ET_RX_RING1_MINCNT, ((ET_RX_NDESC * 15) / 100) - 1);

	/* Match ET_RX_RING1_POS */
	rx_ring->rr_index = 0;
	rx_ring->rr_wrap = 1;

	/*
	 * Install the 1st RX descriptor ring
	 */
	rx_ring = &sc->sc_rx_ring[0];
	CSR_WRITE_4(sc, ET_RX_RING0_HI, ET_ADDR_HI(rx_ring->rr_paddr));
	CSR_WRITE_4(sc, ET_RX_RING0_LO, ET_ADDR_LO(rx_ring->rr_paddr));
	CSR_WRITE_4(sc, ET_RX_RING0_CNT, ET_RX_NDESC - 1);
	CSR_WRITE_4(sc, ET_RX_RING0_POS, ET_RX_RING0_POS_WRAP);
	CSR_WRITE_4(sc, ET_RX_RING0_MINCNT, ((ET_RX_NDESC * 15) / 100) - 1);

	/* Match ET_RX_RING0_POS */
	rx_ring->rr_index = 0;
	rx_ring->rr_wrap = 1;

	/*
	 * RX intr moderation
	 */
	CSR_WRITE_4(sc, ET_RX_INTR_NPKTS, sc->sc_rx_intr_npkts);
	CSR_WRITE_4(sc, ET_RX_INTR_DELAY, sc->sc_rx_intr_delay);

	return 0;
}

int
et_init_txdma(struct et_softc *sc)
{
	struct et_txdesc_ring *tx_ring = &sc->sc_tx_ring;
	struct et_txstatus_data *txsd = &sc->sc_tx_status;
	int error;

	error = et_stop_txdma(sc);
	if (error) {
		printf("%s: can't init TX DMA engine\n", sc->sc_dev.dv_xname);
		return error;
	}

	/*
	 * Install TX descriptor ring
	 */
	CSR_WRITE_4(sc, ET_TX_RING_HI, ET_ADDR_HI(tx_ring->tr_paddr));
	CSR_WRITE_4(sc, ET_TX_RING_LO, ET_ADDR_LO(tx_ring->tr_paddr));
	CSR_WRITE_4(sc, ET_TX_RING_CNT, ET_TX_NDESC - 1);

	/*
	 * Install TX status
	 */
	CSR_WRITE_4(sc, ET_TX_STATUS_HI, ET_ADDR_HI(txsd->txsd_paddr));
	CSR_WRITE_4(sc, ET_TX_STATUS_LO, ET_ADDR_LO(txsd->txsd_paddr));

	CSR_WRITE_4(sc, ET_TX_READY_POS, 0);

	/* Match ET_TX_READY_POS */
	tx_ring->tr_ready_index = 0;
	tx_ring->tr_ready_wrap = 0;

	return 0;
}

void
et_init_mac(struct et_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	const uint8_t *eaddr = LLADDR(ifp->if_sadl);
	uint32_t val;

	/* Reset MAC */
	CSR_WRITE_4(sc, ET_MAC_CFG1,
		    ET_MAC_CFG1_RST_TXFUNC | ET_MAC_CFG1_RST_RXFUNC |
		    ET_MAC_CFG1_RST_TXMC | ET_MAC_CFG1_RST_RXMC |
		    ET_MAC_CFG1_SIM_RST | ET_MAC_CFG1_SOFT_RST);

	/*
	 * Setup inter packet gap
	 */
	val = __SHIFTIN(56, ET_IPG_NONB2B_1) |
	      __SHIFTIN(88, ET_IPG_NONB2B_2) |
	      __SHIFTIN(80, ET_IPG_MINIFG) |
	      __SHIFTIN(96, ET_IPG_B2B);
	CSR_WRITE_4(sc, ET_IPG, val);

	/*
	 * Setup half duplex mode
	 */
	val = __SHIFTIN(10, ET_MAC_HDX_ALT_BEB_TRUNC) |
	      __SHIFTIN(15, ET_MAC_HDX_REXMIT_MAX) |
	      __SHIFTIN(55, ET_MAC_HDX_COLLWIN) |
	      ET_MAC_HDX_EXC_DEFER;
	CSR_WRITE_4(sc, ET_MAC_HDX, val);

	/* Clear MAC control */
	CSR_WRITE_4(sc, ET_MAC_CTRL, 0);

	/* Reset MII */
	CSR_WRITE_4(sc, ET_MII_CFG, ET_MII_CFG_CLKRST);

	/*
	 * Set MAC address
	 */
	val = eaddr[2] | (eaddr[3] << 8) | (eaddr[4] << 16) | (eaddr[5] << 24);
	CSR_WRITE_4(sc, ET_MAC_ADDR1, val);
	val = (eaddr[0] << 16) | (eaddr[1] << 24);
	CSR_WRITE_4(sc, ET_MAC_ADDR2, val);

	/* Set max frame length */
	CSR_WRITE_4(sc, ET_MAX_FRMLEN, ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN);

	/* Bring MAC out of reset state */
	CSR_WRITE_4(sc, ET_MAC_CFG1, 0);
}

void
et_init_rxmac(struct et_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	const uint8_t *eaddr = LLADDR(ifp->if_sadl);
	uint32_t val;
	int i;

	/* Disable RX MAC and WOL */
	CSR_WRITE_4(sc, ET_RXMAC_CTRL, ET_RXMAC_CTRL_WOL_DISABLE);

	/*
	 * Clear all WOL related registers
	 */
	for (i = 0; i < 3; ++i)
		CSR_WRITE_4(sc, ET_WOL_CRC + (i * 4), 0);
	for (i = 0; i < 20; ++i)
		CSR_WRITE_4(sc, ET_WOL_MASK + (i * 4), 0);

	/*
	 * Set WOL source address.  XXX is this necessary?
	 */
	val = (eaddr[2] << 24) | (eaddr[3] << 16) | (eaddr[4] << 8) | eaddr[5];
	CSR_WRITE_4(sc, ET_WOL_SA_LO, val);
	val = (eaddr[0] << 8) | eaddr[1];
	CSR_WRITE_4(sc, ET_WOL_SA_HI, val);

	/* Clear packet filters */
	CSR_WRITE_4(sc, ET_PKTFILT, 0);

	/* No ucast filtering */
	CSR_WRITE_4(sc, ET_UCAST_FILTADDR1, 0);
	CSR_WRITE_4(sc, ET_UCAST_FILTADDR2, 0);
	CSR_WRITE_4(sc, ET_UCAST_FILTADDR3, 0);

	if (ifp->if_hardmtu > 8192) {
		/*
		 * In order to transmit jumbo packets greater than 8k,
		 * the FIFO between RX MAC and RX DMA needs to be reduced
		 * in size to (16k - MTU).  In order to implement this, we
		 * must use "cut through" mode in the RX MAC, which chops
		 * packets down into segments which are (max_size * 16).
		 * In this case we selected 256 bytes, since this is the
		 * size of the PCI-Express TLP's that the 1310 uses.
		 */
		val = __SHIFTIN(16, ET_RXMAC_MC_SEGSZ_MAX) |
		      ET_RXMAC_MC_SEGSZ_ENABLE;
	} else {
		val = 0;
	}
	CSR_WRITE_4(sc, ET_RXMAC_MC_SEGSZ, val);

	CSR_WRITE_4(sc, ET_RXMAC_MC_WATERMARK, 0);

	/* Initialize RX MAC management register */
	CSR_WRITE_4(sc, ET_RXMAC_MGT, 0);

	CSR_WRITE_4(sc, ET_RXMAC_SPACE_AVL, 0);

	CSR_WRITE_4(sc, ET_RXMAC_MGT,
		    ET_RXMAC_MGT_PASS_ECRC |
		    ET_RXMAC_MGT_PASS_ELEN |
		    ET_RXMAC_MGT_PASS_ETRUNC |
		    ET_RXMAC_MGT_CHECK_PKT);

	/*
	 * Configure runt filtering (may not work on certain chip generation)
	 */
	val = __SHIFTIN(ETHER_MIN_LEN, ET_PKTFILT_MINLEN) | ET_PKTFILT_FRAG;
	CSR_WRITE_4(sc, ET_PKTFILT, val);

	/* Enable RX MAC but leave WOL disabled */
	CSR_WRITE_4(sc, ET_RXMAC_CTRL,
		    ET_RXMAC_CTRL_WOL_DISABLE | ET_RXMAC_CTRL_ENABLE);

	/*
	 * Setup multicast hash and allmulti/promisc mode
	 */
	et_setmulti(sc);
}

void
et_init_txmac(struct et_softc *sc)
{
	/* Disable TX MAC and FC(?) */
	CSR_WRITE_4(sc, ET_TXMAC_CTRL, ET_TXMAC_CTRL_FC_DISABLE);

	/* No flow control yet */
	CSR_WRITE_4(sc, ET_TXMAC_FLOWCTRL, 0);

	/* Enable TX MAC but leave FC(?) disabled */
	CSR_WRITE_4(sc, ET_TXMAC_CTRL,
		    ET_TXMAC_CTRL_ENABLE | ET_TXMAC_CTRL_FC_DISABLE);
}

int
et_start_rxdma(struct et_softc *sc)
{
	uint32_t val = 0;

	val |= __SHIFTIN(sc->sc_rx_data[0].rbd_bufsize,
			 ET_RXDMA_CTRL_RING0_SIZE) |
	       ET_RXDMA_CTRL_RING0_ENABLE;
	val |= __SHIFTIN(sc->sc_rx_data[1].rbd_bufsize,
			 ET_RXDMA_CTRL_RING1_SIZE) |
	       ET_RXDMA_CTRL_RING1_ENABLE;

	CSR_WRITE_4(sc, ET_RXDMA_CTRL, val);

	DELAY(5);

	if (CSR_READ_4(sc, ET_RXDMA_CTRL) & ET_RXDMA_CTRL_HALTED) {
		printf("%s: can't start RX DMA engine\n", sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}
	return 0;
}

int
et_start_txdma(struct et_softc *sc)
{
	CSR_WRITE_4(sc, ET_TXDMA_CTRL, ET_TXDMA_CTRL_SINGLE_EPKT);
	return 0;
}

int
et_enable_txrx(struct et_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint32_t val;
	int i;

	val = CSR_READ_4(sc, ET_MAC_CFG1);
	val |= ET_MAC_CFG1_TXEN | ET_MAC_CFG1_RXEN;
	val &= ~(ET_MAC_CFG1_TXFLOW | ET_MAC_CFG1_RXFLOW |
		 ET_MAC_CFG1_LOOPBACK);
	CSR_WRITE_4(sc, ET_MAC_CFG1, val);

	et_ifmedia_upd(ifp);

#define NRETRY	100

	for (i = 0; i < NRETRY; ++i) {
		val = CSR_READ_4(sc, ET_MAC_CFG1);
		if ((val & (ET_MAC_CFG1_SYNC_TXEN | ET_MAC_CFG1_SYNC_RXEN)) ==
		    (ET_MAC_CFG1_SYNC_TXEN | ET_MAC_CFG1_SYNC_RXEN))
			break;

		DELAY(10);
	}
	if (i == NRETRY) {
		printf("%s: can't enable RX/TX\n", sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}

#undef NRETRY
	return 0;
}

void
et_rxeof(struct et_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct et_rxstatus_data *rxsd = &sc->sc_rx_status;
	struct et_rxstat_ring *rxst_ring = &sc->sc_rxstat_ring;
	uint32_t rxs_stat_ring;
	int rxst_wrap, rxst_index;

	bus_dmamap_sync(sc->sc_dmat, rxsd->rxsd_dmap, 0,
	    rxsd->rxsd_dmap->dm_mapsize, BUS_DMASYNC_POSTREAD);
	bus_dmamap_sync(sc->sc_dmat, rxst_ring->rsr_dmap, 0,
	    rxst_ring->rsr_dmap->dm_mapsize, BUS_DMASYNC_POSTREAD);

	rxs_stat_ring = rxsd->rxsd_status->rxs_stat_ring;
	rxst_wrap = (rxs_stat_ring & ET_RXS_STATRING_WRAP) ? 1 : 0;
	rxst_index = __SHIFTOUT(rxs_stat_ring, ET_RXS_STATRING_INDEX);

	while (rxst_index != rxst_ring->rsr_index ||
	       rxst_wrap != rxst_ring->rsr_wrap) {
		struct et_rxbuf_data *rbd;
		struct et_rxdesc_ring *rx_ring;
		struct et_rxstat *st;
		struct et_rxbuf *rb;
		struct mbuf *m;
		int buflen, buf_idx, ring_idx;
		uint32_t rxstat_pos, rxring_pos;

		KKASSERT(rxst_ring->rsr_index < ET_RX_NSTAT);
		st = &rxst_ring->rsr_stat[rxst_ring->rsr_index];

		buflen = __SHIFTOUT(st->rxst_info2, ET_RXST_INFO2_LEN);
		buf_idx = __SHIFTOUT(st->rxst_info2, ET_RXST_INFO2_BUFIDX);
		ring_idx = __SHIFTOUT(st->rxst_info2, ET_RXST_INFO2_RINGIDX);

		if (++rxst_ring->rsr_index == ET_RX_NSTAT) {
			rxst_ring->rsr_index = 0;
			rxst_ring->rsr_wrap ^= 1;
		}
		rxstat_pos = __SHIFTIN(rxst_ring->rsr_index,
				       ET_RXSTAT_POS_INDEX);
		if (rxst_ring->rsr_wrap)
			rxstat_pos |= ET_RXSTAT_POS_WRAP;
		CSR_WRITE_4(sc, ET_RXSTAT_POS, rxstat_pos);

		if (ring_idx >= ET_RX_NRING) {
			ifp->if_ierrors++;
			printf("%s: invalid ring index %d\n",
			    sc->sc_dev.dv_xname, ring_idx);
			continue;
		}
		if (buf_idx >= ET_RX_NDESC) {
			ifp->if_ierrors++;
			printf("%s: invalid buf index %d\n",
			    sc->sc_dev.dv_xname, buf_idx);
			continue;
		}

		rbd = &sc->sc_rx_data[ring_idx];
		rb = &rbd->rbd_buf[buf_idx];
		m = rb->rb_mbuf;
		bus_dmamap_sync(sc->sc_dmat, rb->rb_dmap, 0,
		    rb->rb_dmap->dm_mapsize, BUS_DMASYNC_POSTREAD);

		if (rbd->rbd_newbuf(rbd, buf_idx, 0) == 0) {
			if (buflen < ETHER_CRC_LEN) {
				m_freem(m);
				ifp->if_ierrors++;
			} else {
				m->m_pkthdr.len = m->m_len = buflen -
				    ETHER_CRC_LEN;
				ml_enqueue(&ml, m);
			}
		} else {
			ifp->if_ierrors++;
		}

		rx_ring = &sc->sc_rx_ring[ring_idx];

		if (buf_idx != rx_ring->rr_index) {
			printf("%s: WARNING!! ring %d, "
			    "buf_idx %d, rr_idx %d\n", sc->sc_dev.dv_xname, 
			    ring_idx, buf_idx, rx_ring->rr_index);
		}

		KKASSERT(rx_ring->rr_index < ET_RX_NDESC);
		if (++rx_ring->rr_index == ET_RX_NDESC) {
			rx_ring->rr_index = 0;
			rx_ring->rr_wrap ^= 1;
		}
		rxring_pos = __SHIFTIN(rx_ring->rr_index, ET_RX_RING_POS_INDEX);
		if (rx_ring->rr_wrap)
			rxring_pos |= ET_RX_RING_POS_WRAP;
		CSR_WRITE_4(sc, rx_ring->rr_posreg, rxring_pos);
	}

	if_input(ifp, &ml);
}

int
et_encap(struct et_softc *sc, struct mbuf **m0)
{
	struct mbuf *m = *m0;
	struct et_txdesc_ring *tx_ring = &sc->sc_tx_ring;
	struct et_txbuf_data *tbd = &sc->sc_tx_data;
	struct et_txdesc *td;
	bus_dmamap_t map;
	int error, maxsegs, first_idx, last_idx, i;
	uint32_t tx_ready_pos, last_td_ctrl2;

	maxsegs = ET_TX_NDESC - tbd->tbd_used;
	if (maxsegs > ET_NSEG_MAX)
		maxsegs = ET_NSEG_MAX;
	KASSERT(maxsegs >= ET_NSEG_SPARE,
		("not enough spare TX desc (%d)\n", maxsegs));

	KKASSERT(tx_ring->tr_ready_index < ET_TX_NDESC);
	first_idx = tx_ring->tr_ready_index;
	map = tbd->tbd_buf[first_idx].tb_dmap;

	error = bus_dmamap_load_mbuf(sc->sc_dmat, map, m,
	    BUS_DMA_NOWAIT);
	if (!error && map->dm_nsegs == 0) {
		bus_dmamap_unload(sc->sc_dmat, map);
		error = EFBIG;
	}
	if (error && error != EFBIG) {
		printf("%s: can't load TX mbuf", sc->sc_dev.dv_xname);
		goto back;
	}
	if (error) {	/* error == EFBIG */
		if (m_defrag(m, M_DONTWAIT)) {
			printf("%s: can't defrag TX mbuf\n",
			    sc->sc_dev.dv_xname);
			error = ENOBUFS;
			goto back;
		}
		error = bus_dmamap_load_mbuf(sc->sc_dmat, map, m,
					     BUS_DMA_NOWAIT);
		if (error || map->dm_nsegs == 0) {
			if (map->dm_nsegs == 0) {
				bus_dmamap_unload(sc->sc_dmat, map);
				error = EFBIG;
			}
			printf("%s: can't load defraged TX mbuf\n",
			    sc->sc_dev.dv_xname);
			goto back;
		}
	}

	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	last_td_ctrl2 = ET_TDCTRL2_LAST_FRAG;
	sc->sc_tx += map->dm_nsegs;
	if (sc->sc_tx / sc->sc_tx_intr_nsegs != sc->sc_tx_intr) {
		sc->sc_tx_intr = sc->sc_tx / sc->sc_tx_intr_nsegs;
		last_td_ctrl2 |= ET_TDCTRL2_INTR;
	}

	last_idx = -1;
	for (i = 0; i < map->dm_nsegs; ++i) {
		int idx;

		idx = (first_idx + i) % ET_TX_NDESC;
		td = &tx_ring->tr_desc[idx];
		td->td_addr_hi = ET_ADDR_HI(map->dm_segs[i].ds_addr);
		td->td_addr_lo = ET_ADDR_LO(map->dm_segs[i].ds_addr);
		td->td_ctrl1 =
		    __SHIFTIN(map->dm_segs[i].ds_len, ET_TDCTRL1_LEN);

		if (i == map->dm_nsegs - 1) {	/* Last frag */
			td->td_ctrl2 = last_td_ctrl2;
			last_idx = idx;
		}

		KKASSERT(tx_ring->tr_ready_index < ET_TX_NDESC);
		if (++tx_ring->tr_ready_index == ET_TX_NDESC) {
			tx_ring->tr_ready_index = 0;
			tx_ring->tr_ready_wrap ^= 1;
		}
	}
	td = &tx_ring->tr_desc[first_idx];
	td->td_ctrl2 |= ET_TDCTRL2_FIRST_FRAG;	/* First frag */

	KKASSERT(last_idx >= 0);
	tbd->tbd_buf[first_idx].tb_dmap = tbd->tbd_buf[last_idx].tb_dmap;
	tbd->tbd_buf[last_idx].tb_dmap = map;
	tbd->tbd_buf[last_idx].tb_mbuf = m;

	tbd->tbd_used += map->dm_nsegs;
	KKASSERT(tbd->tbd_used <= ET_TX_NDESC);

	bus_dmamap_sync(sc->sc_dmat, tx_ring->tr_dmap, 0,
	    tx_ring->tr_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);
			

	tx_ready_pos = __SHIFTIN(tx_ring->tr_ready_index,
		       ET_TX_READY_POS_INDEX);
	if (tx_ring->tr_ready_wrap)
		tx_ready_pos |= ET_TX_READY_POS_WRAP;
	CSR_WRITE_4(sc, ET_TX_READY_POS, tx_ready_pos);

	error = 0;
back:
	if (error) {
		m_freem(m);
		*m0 = NULL;
	}
	return error;
}

void
et_txeof(struct et_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct et_txdesc_ring *tx_ring = &sc->sc_tx_ring;
	struct et_txbuf_data *tbd = &sc->sc_tx_data;
	uint32_t tx_done;
	int end, wrap;

	if (tbd->tbd_used == 0)
		return;

	tx_done = CSR_READ_4(sc, ET_TX_DONE_POS);
	end = __SHIFTOUT(tx_done, ET_TX_DONE_POS_INDEX);
	wrap = (tx_done & ET_TX_DONE_POS_WRAP) ? 1 : 0;

	while (tbd->tbd_start_index != end || tbd->tbd_start_wrap != wrap) {
		struct et_txbuf *tb;

		KKASSERT(tbd->tbd_start_index < ET_TX_NDESC);
		tb = &tbd->tbd_buf[tbd->tbd_start_index];

		bzero(&tx_ring->tr_desc[tbd->tbd_start_index],
		      sizeof(struct et_txdesc));
		bus_dmamap_sync(sc->sc_dmat, tx_ring->tr_dmap, 0,
		    tx_ring->tr_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);

		if (tb->tb_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat, tb->tb_dmap);
			m_freem(tb->tb_mbuf);
			tb->tb_mbuf = NULL;
		}

		if (++tbd->tbd_start_index == ET_TX_NDESC) {
			tbd->tbd_start_index = 0;
			tbd->tbd_start_wrap ^= 1;
		}

		KKASSERT(tbd->tbd_used > 0);
		tbd->tbd_used--;
	}

	if (tbd->tbd_used == 0) {
		timeout_del(&sc->sc_txtick);
		ifp->if_timer = 0;
	}
	if (tbd->tbd_used + ET_NSEG_SPARE <= ET_TX_NDESC)
		ifq_clr_oactive(&ifp->if_snd);

	et_start(ifp);
}

void
et_txtick(void *xsc)
{
	struct et_softc *sc = xsc;
	int s;

	s = splnet();
	et_txeof(sc);
	splx(s);
}

void
et_tick(void *xsc)
{
	struct et_softc *sc = xsc;
	int s;

	s = splnet();
	mii_tick(&sc->sc_miibus);
	timeout_add_sec(&sc->sc_tick, 1);
	splx(s);
}

int
et_newbuf_cluster(struct et_rxbuf_data *rbd, int buf_idx, int init)
{
	return et_newbuf(rbd, buf_idx, init, MCLBYTES);
}

int
et_newbuf_hdr(struct et_rxbuf_data *rbd, int buf_idx, int init)
{
	return et_newbuf(rbd, buf_idx, init, MHLEN);
}

int
et_newbuf(struct et_rxbuf_data *rbd, int buf_idx, int init, int len0)
{
	struct et_softc *sc = rbd->rbd_softc;
	struct et_rxdesc_ring *rx_ring;
	struct et_rxdesc *desc;
	struct et_rxbuf *rb;
	struct mbuf *m;
	bus_dmamap_t dmap;
	int error, len;

	KKASSERT(buf_idx < ET_RX_NDESC);
	rb = &rbd->rbd_buf[buf_idx];

	if (len0 >= MINCLSIZE) {
		MGETHDR(m, init ? M_WAITOK : M_DONTWAIT, MT_DATA);
		if (m == NULL)
			return (ENOBUFS);
		MCLGET(m, init ? M_WAITOK : M_DONTWAIT);
		if ((m->m_flags & M_EXT) == 0) {
			m_freem(m);
			return (ENOBUFS);
		}
		len = MCLBYTES;
	} else {
		MGETHDR(m, init ? M_WAITOK : M_DONTWAIT, MT_DATA);
		len = MHLEN;
	}

	if (m == NULL) {
		error = ENOBUFS;

		/* XXX for debug */
		printf("%s: M_CLGET failed, size %d\n", sc->sc_dev.dv_xname,
		    len0);
		if (init) {
			return error;
		} else {
			goto back;
		}
	}
	m->m_len = m->m_pkthdr.len = len;

	/*
	 * Try load RX mbuf into temporary DMA tag
	 */
	error = bus_dmamap_load_mbuf(sc->sc_dmat, sc->sc_mbuf_tmp_dmap, m,
				     init ? BUS_DMA_WAITOK : BUS_DMA_NOWAIT);
	if (error) {
		if (!error) {
			bus_dmamap_unload(sc->sc_dmat, sc->sc_mbuf_tmp_dmap);
			error = EFBIG;
			printf("%s: too many segments?!\n",
			    sc->sc_dev.dv_xname);
		}
		m_freem(m);

		/* XXX for debug */
		printf("%s: can't load RX mbuf\n", sc->sc_dev.dv_xname);
		if (init) {
			return error;
		} else {
			goto back;
		}
	}

	if (!init)
		bus_dmamap_unload(sc->sc_dmat, rb->rb_dmap);
	rb->rb_mbuf = m;

	/*
	 * Swap RX buf's DMA map with the loaded temporary one
	 */
	dmap = rb->rb_dmap;
	rb->rb_dmap = sc->sc_mbuf_tmp_dmap;
	rb->rb_paddr = rb->rb_dmap->dm_segs[0].ds_addr;
	sc->sc_mbuf_tmp_dmap = dmap;

	error = 0;
back:
	rx_ring = rbd->rbd_ring;
	desc = &rx_ring->rr_desc[buf_idx];

	desc->rd_addr_hi = ET_ADDR_HI(rb->rb_paddr);
	desc->rd_addr_lo = ET_ADDR_LO(rb->rb_paddr);
	desc->rd_ctrl = __SHIFTIN(buf_idx, ET_RDCTRL_BUFIDX);

	bus_dmamap_sync(sc->sc_dmat, rx_ring->rr_dmap, 0,
	    rx_ring->rr_dmap->dm_mapsize, BUS_DMASYNC_PREWRITE);
	return error;
}
