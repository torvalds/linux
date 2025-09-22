/* $OpenBSD: if_fec.c,v 1.14 2022/01/09 05:42:37 jsg Exp $ */
/*
 * Copyright (c) 2012-2013,2019 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/evcount.h>
#include <sys/socket.h>
#include <sys/timeout.h>
#include <sys/mbuf.h>
#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include "bpfilter.h"

#include <net/if.h>
#include <net/if_media.h>
#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/miidevs.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

/* configuration registers */
#define ENET_EIR		0x004
#define ENET_EIMR		0x008
#define ENET_RDAR		0x010
#define ENET_TDAR		0x014
#define ENET_ECR		0x024
#define ENET_MMFR		0x040
#define ENET_MSCR		0x044
#define ENET_MIBC		0x064
#define ENET_RCR		0x084
#define ENET_TCR		0x0C4
#define ENET_PALR		0x0E4
#define ENET_PAUR		0x0E8
#define ENET_OPD		0x0EC
#define ENET_IAUR		0x118
#define ENET_IALR		0x11C
#define ENET_GAUR		0x120
#define ENET_GALR		0x124
#define ENET_TFWR		0x144
#define ENET_RDSR		0x180
#define ENET_TDSR		0x184
#define ENET_MRBR		0x188
#define ENET_RSFL		0x190
#define ENET_RSEM		0x194
#define ENET_RAEM		0x198
#define ENET_RAFL		0x19C
#define ENET_TSEM		0x1A0
#define ENET_TAEM		0x1A4
#define ENET_TAFL		0x1A8
#define ENET_TIPG		0x1AC
#define ENET_FTRL		0x1B0
#define ENET_TACC		0x1C0
#define ENET_RACC		0x1C4

#define ENET_RDAR_RDAR		(1 << 24)
#define ENET_TDAR_TDAR		(1 << 24)
#define ENET_ECR_RESET		(1 << 0)
#define ENET_ECR_ETHEREN	(1 << 1)
#define ENET_ECR_EN1588		(1 << 4)
#define ENET_ECR_SPEED		(1 << 5)
#define ENET_ECR_DBSWP		(1 << 8)
#define ENET_MMFR_TA		(2 << 16)
#define ENET_MMFR_RA_SHIFT	18
#define ENET_MMFR_PA_SHIFT	23
#define ENET_MMFR_OP_WR		(1 << 28)
#define ENET_MMFR_OP_RD		(2 << 28)
#define ENET_MMFR_ST		(1 << 30)
#define ENET_RCR_MII_MODE	(1 << 2)
#define ENET_RCR_PROM		(1 << 3)
#define ENET_RCR_FCE		(1 << 5)
#define ENET_RCR_RGMII_MODE	(1 << 6)
#define ENET_RCR_RMII_10T	(1 << 9)
#define ENET_RCR_MAX_FL(x)	(((x) & 0x3fff) << 16)
#define ENET_TCR_FDEN		(1 << 2)
#define ENET_EIR_MII		(1 << 23)
#define ENET_EIR_RXF		(1 << 25)
#define ENET_EIR_TXF		(1 << 27)
#define ENET_TFWR_STRFWD	(1 << 8)
#define ENET_RACC_SHIFT16	(1 << 7)

/* statistics counters */

/* 1588 control */
#define ENET_ATCR		0x400
#define ENET_ATVR		0x404
#define ENET_ATOFF		0x408
#define ENET_ATPER		0x40C
#define ENET_ATCOR		0x410
#define ENET_ATINC		0x414
#define ENET_ATSTMP		0x418

/* capture / compare block */
#define ENET_TGSR		0x604
#define ENET_TCSR0		0x608
#define ENET_TCCR0		0x60C
#define ENET_TCSR1		0x610
#define ENET_TCCR1		0x614
#define ENET_TCSR2		0x618
#define ENET_TCCR2		0x61C
#define ENET_TCSR3		0x620
#define ENET_TCCR3		0x624

#define ENET_MII_CLK		2500000
#define ENET_ALIGNMENT		16

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

#define ENET_MAX_BUF_SIZE	1522
#define ENET_MAX_PKT_SIZE	1536

#define ENET_ROUNDUP(size, unit) (((size) + (unit) - 1) & ~((unit) - 1))

/* buffer descriptor status bits */
#define ENET_RXD_EMPTY		(1 << 15)
#define ENET_RXD_WRAP		(1 << 13)
#define ENET_RXD_INTR		(1 << 12)
#define ENET_RXD_LAST		(1 << 11)
#define ENET_RXD_MISS		(1 << 8)
#define ENET_RXD_BC		(1 << 7)
#define ENET_RXD_MC		(1 << 6)
#define ENET_RXD_LG		(1 << 5)
#define ENET_RXD_NO		(1 << 4)
#define ENET_RXD_CR		(1 << 2)
#define ENET_RXD_OV		(1 << 1)
#define ENET_RXD_TR		(1 << 0)

#define ENET_TXD_READY		(1 << 15)
#define ENET_TXD_WRAP		(1 << 13)
#define ENET_TXD_INTR		(1 << 12)
#define ENET_TXD_LAST		(1 << 11)
#define ENET_TXD_TC		(1 << 10)
#define ENET_TXD_ABC		(1 << 9)
#define ENET_TXD_STATUS_MASK	0x3ff

#ifdef ENET_ENHANCED_BD
/* enhanced */
#define ENET_RXD_INT		(1 << 23)

#define ENET_TXD_INT		(1 << 30)
#endif

struct fec_buf {
	bus_dmamap_t	 fb_map;
	struct mbuf	*fb_m;
	struct mbuf	*fb_m0;
};

/* what should we use? */
#define ENET_NTXDESC	256
#define ENET_NTXSEGS	16
#define ENET_NRXDESC	256

struct fec_dmamem {
	bus_dmamap_t		 fdm_map;
	bus_dma_segment_t	 fdm_seg;
	size_t			 fdm_size;
	caddr_t			 fdm_kva;
};
#define ENET_DMA_MAP(_fdm)	((_fdm)->fdm_map)
#define ENET_DMA_LEN(_fdm)	((_fdm)->fdm_size)
#define ENET_DMA_DVA(_fdm)	((_fdm)->fdm_map->dm_segs[0].ds_addr)
#define ENET_DMA_KVA(_fdm)	((void *)(_fdm)->fdm_kva)

struct fec_desc {
	uint16_t fd_len;		/* payload's length in bytes */
	uint16_t fd_status;		/* BD's status (see datasheet) */
	uint32_t fd_addr;		/* payload's buffer address */
#ifdef ENET_ENHANCED_BD
	uint32_t fd_enhanced_status;	/* enhanced status with IEEE 1588 */
	uint32_t fd_reserved0;		/* reserved */
	uint32_t fd_update_done;	/* buffer descriptor update done */
	uint32_t fd_timestamp;		/* IEEE 1588 timestamp */
	uint32_t fd_reserved1[2];	/* reserved */
#endif
};

struct fec_softc {
	struct device		sc_dev;
	struct arpcom		sc_ac;
	struct mii_data		sc_mii;
	int			sc_node;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	void			*sc_ih[3]; /* Interrupt handler */
	bus_dma_tag_t		sc_dmat;

	struct fec_dmamem	*sc_txring;
	struct fec_buf		*sc_txbuf;
	struct fec_desc		*sc_txdesc;
	int			 sc_tx_prod;
	int			 sc_tx_cnt;
	int			 sc_tx_cons;
	int			 sc_tx_bounce;

	struct fec_dmamem	*sc_rxring;
	struct fec_buf		*sc_rxbuf;
	struct fec_desc		*sc_rxdesc;
	int			 sc_rx_prod;
	struct if_rxring	 sc_rx_ring;
	int			 sc_rx_cons;

	struct timeout		sc_tick;
	uint32_t		sc_phy_speed;
};

struct fec_softc *fec_sc;

int fec_match(struct device *, void *, void *);
void fec_attach(struct device *, struct device *, void *);
void fec_phy_init(struct fec_softc *, struct mii_softc *);
int fec_ioctl(struct ifnet *, u_long, caddr_t);
void fec_start(struct ifnet *);
int fec_encap(struct fec_softc *, struct mbuf *, int *);
void fec_init_txd(struct fec_softc *);
void fec_init_rxd(struct fec_softc *);
void fec_init(struct fec_softc *);
void fec_stop(struct fec_softc *);
void fec_iff(struct fec_softc *);
int fec_intr(void *);
void fec_tx_proc(struct fec_softc *);
void fec_rx_proc(struct fec_softc *);
void fec_tick(void *);
int fec_miibus_readreg(struct device *, int, int);
void fec_miibus_writereg(struct device *, int, int, int);
void fec_miibus_statchg(struct device *);
int fec_ifmedia_upd(struct ifnet *);
void fec_ifmedia_sts(struct ifnet *, struct ifmediareq *);
struct fec_dmamem *fec_dmamem_alloc(struct fec_softc *, bus_size_t, bus_size_t);
void fec_dmamem_free(struct fec_softc *, struct fec_dmamem *);
struct mbuf *fec_alloc_mbuf(struct fec_softc *, bus_dmamap_t);
void fec_fill_rx_ring(struct fec_softc *);

const struct cfattach fec_ca = {
	sizeof (struct fec_softc), fec_match, fec_attach
};

struct cfdriver fec_cd = {
	NULL, "fec", DV_IFNET
};

int
fec_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "fsl,imx6q-fec") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx6sx-fec") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx8mq-fec"));
}

void
fec_attach(struct device *parent, struct device *self, void *aux)
{
	struct fec_softc *sc = (struct fec_softc *) self;
	struct fdt_attach_args *faa = aux;
	struct fec_buf *txb, *rxb;
	struct mii_data *mii;
	struct mii_softc *child;
	struct ifnet *ifp;
	uint32_t phy_reset_gpio[3];
	uint32_t phy_reset_duration;
	int i, s;

	if (faa->fa_nreg < 1)
		return;

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("fec_attach: bus_space_map failed!");

	sc->sc_dmat = faa->fa_dmat;

	pinctrl_byname(faa->fa_node, "default");

	/* power it up */
	clock_enable_all(faa->fa_node);

	/* reset PHY */
	if (OF_getpropintarray(faa->fa_node, "phy-reset-gpios", phy_reset_gpio,
	    sizeof(phy_reset_gpio)) == sizeof(phy_reset_gpio)) {
		phy_reset_duration = OF_getpropint(faa->fa_node,
		    "phy-reset-duration", 1);
		if (phy_reset_duration > 1000)
			phy_reset_duration = 1;

		/*
		 * The Linux people really screwed the pooch here.
		 * The Linux kernel always treats the gpio as
		 * active-low, even if it is marked as active-high in
		 * the device tree.  As a result the device tree for
		 * many boards incorrectly marks the gpio as
		 * active-high.  
		 */
		phy_reset_gpio[2] = GPIO_ACTIVE_LOW;
		gpio_controller_config_pin(phy_reset_gpio, GPIO_CONFIG_OUTPUT);

		/*
		 * On some Cubox-i machines we need to hold the PHY in
		 * reset a little bit longer than specified.
		 */
		gpio_controller_set_pin(phy_reset_gpio, 1);
		delay((phy_reset_duration + 1) * 1000);
		gpio_controller_set_pin(phy_reset_gpio, 0);
		delay(1000);
	}
	printf("\n");

	/* Figure out the hardware address. Must happen before reset. */
	OF_getprop(faa->fa_node, "local-mac-address", sc->sc_ac.ac_enaddr,
	    sizeof(sc->sc_ac.ac_enaddr));

	/* reset the controller */
	HSET4(sc, ENET_ECR, ENET_ECR_RESET);
	while (HREAD4(sc, ENET_ECR) & ENET_ECR_ETHEREN)
		continue;

	HWRITE4(sc, ENET_EIMR, 0);
	HWRITE4(sc, ENET_EIR, 0xffffffff);

	sc->sc_ih[0] = fdt_intr_establish_idx(faa->fa_node, 0, IPL_NET,
	    fec_intr, sc, sc->sc_dev.dv_xname);
	sc->sc_ih[1] = fdt_intr_establish_idx(faa->fa_node, 1, IPL_NET,
	    fec_intr, sc, sc->sc_dev.dv_xname);
	sc->sc_ih[2] = fdt_intr_establish_idx(faa->fa_node, 2, IPL_NET,
	    fec_intr, sc, sc->sc_dev.dv_xname);

	/* Tx bounce buffer to align to 16. */
	if (OF_is_compatible(faa->fa_node, "fsl,imx6q-fec"))
		sc->sc_tx_bounce = 1;

	/* Allocate Tx descriptor ring. */
	sc->sc_txring = fec_dmamem_alloc(sc,
	    ENET_NTXDESC * sizeof(struct fec_desc), 64);
	if (sc->sc_txring == NULL) {
		printf("%s: could not allocate Tx descriptor ring\n",
		    sc->sc_dev.dv_xname);
		goto bad;
	}
	sc->sc_txdesc = ENET_DMA_KVA(sc->sc_txring);

	/* Allocate Tx descriptors. */
	sc->sc_txbuf = malloc(sizeof(struct fec_buf) * ENET_NTXDESC,
	    M_DEVBUF, M_WAITOK);
	for (i = 0; i < ENET_NTXDESC; i++) {
		txb = &sc->sc_txbuf[i];
		bus_dmamap_create(sc->sc_dmat, MCLBYTES, ENET_NTXSEGS,
		    MCLBYTES, 0, BUS_DMA_WAITOK, &txb->fb_map);
		txb->fb_m = txb->fb_m0 = NULL;
	}

	/* Allocate Rx descriptor ring. */
	sc->sc_rxring = fec_dmamem_alloc(sc,
	    ENET_NRXDESC * sizeof(struct fec_desc), 64);
	if (sc->sc_rxring == NULL) {
		printf("%s: could not allocate Rx descriptor ring\n",
		    sc->sc_dev.dv_xname);
		for (i = 0; i < ENET_NTXDESC; i++) {
			txb = &sc->sc_txbuf[i];
			bus_dmamap_destroy(sc->sc_dmat, txb->fb_map);
		}
		free(sc->sc_txbuf, M_DEVBUF,
		    sizeof(struct fec_buf) * ENET_NTXDESC);
		fec_dmamem_free(sc, sc->sc_txring);
		goto bad;
	}
	sc->sc_rxdesc = ENET_DMA_KVA(sc->sc_rxring);

	/* Allocate Rx descriptors. */
	sc->sc_rxbuf = malloc(sizeof(struct fec_buf) * ENET_NRXDESC,
	    M_DEVBUF, M_WAITOK);
	for (i = 0; i < ENET_NRXDESC; i++) {
		rxb = &sc->sc_rxbuf[i];
		bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, BUS_DMA_WAITOK, &rxb->fb_map);
		rxb->fb_m = NULL;
	}

	s = splnet();

	ifp = &sc->sc_ac.ac_if;
	ifp->if_softc = sc;
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = fec_ioctl;
	ifp->if_start = fec_start;
	ifp->if_capabilities = IFCAP_VLAN_MTU;

	printf("%s: address %s\n", sc->sc_dev.dv_xname,
	    ether_sprintf(sc->sc_ac.ac_enaddr));

	/*
	 * Initialize the MII clock.  The formula is:
	 *
	 * ENET_MII_CLK = ref_freq / ((phy_speed + 1) x 2)
	 * phy_speed = (((ref_freq / ENET_MII_CLK) / 2) - 1)
	 */
	sc->sc_phy_speed = clock_get_frequency(sc->sc_node, "ipg");
	sc->sc_phy_speed = (sc->sc_phy_speed + (ENET_MII_CLK - 1)) / ENET_MII_CLK;
	sc->sc_phy_speed = (sc->sc_phy_speed / 2) - 1;
	HWRITE4(sc, ENET_MSCR, (sc->sc_phy_speed << 1) | 0x100);

	/* Initialize MII/media info. */
	mii = &sc->sc_mii;
	mii->mii_ifp = ifp;
	mii->mii_readreg = fec_miibus_readreg;
	mii->mii_writereg = fec_miibus_writereg;
	mii->mii_statchg = fec_miibus_statchg;

	ifmedia_init(&mii->mii_media, 0, fec_ifmedia_upd, fec_ifmedia_sts);
	mii_attach(self, mii, 0xffffffff, MII_PHY_ANY, MII_OFFSET_ANY, 0);

	child = LIST_FIRST(&mii->mii_phys);
	if (child)
		fec_phy_init(sc, child);

	if (LIST_FIRST(&mii->mii_phys) == NULL) {
		ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_NONE, 0, NULL);
		ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_NONE);
	} else
		ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);
	splx(s);

	timeout_set(&sc->sc_tick, fec_tick, sc);

	fec_sc = sc;
	return;

bad:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, faa->fa_reg[0].size);
}

void
fec_phy_init(struct fec_softc *sc, struct mii_softc *child)
{
	struct device *dev = (struct device *)sc;
	int phy = child->mii_phy;
	uint32_t reg;

	if (child->mii_oui == MII_OUI_ATHEROS &&
	    child->mii_model == MII_MODEL_ATHEROS_AR8035) {
		/* disable SmartEEE */
		fec_miibus_writereg(dev, phy, 0x0d, 0x0003);
		fec_miibus_writereg(dev, phy, 0x0e, 0x805d);
		fec_miibus_writereg(dev, phy, 0x0d, 0x4003);
		reg = fec_miibus_readreg(dev, phy, 0x0e);
		fec_miibus_writereg(dev, phy, 0x0e, reg & ~0x0100);

		/* enable 125MHz clk output */
		fec_miibus_writereg(dev, phy, 0x0d, 0x0007);
		fec_miibus_writereg(dev, phy, 0x0e, 0x8016);
		fec_miibus_writereg(dev, phy, 0x0d, 0x4007);

		reg = fec_miibus_readreg(dev, phy, 0x0e) & 0xffe3;
		fec_miibus_writereg(dev, phy, 0x0e, reg | 0x18);

		/* tx clock delay */
		fec_miibus_writereg(dev, phy, 0x1d, 0x0005);
		reg = fec_miibus_readreg(dev, phy, 0x1e);
		fec_miibus_writereg(dev, phy, 0x1e, reg | 0x0100);

		PHY_RESET(child);
	}

	if (child->mii_oui == MII_OUI_MICREL &&
	    child->mii_model == MII_MODEL_MICREL_KSZ9021) {
		uint32_t rxc, rxdv, txc, txen;
		uint32_t rxd0, rxd1, rxd2, rxd3;
		uint32_t txd0, txd1, txd2, txd3;
		uint32_t val, phy;
		int node;

		node = sc->sc_node;
		phy = OF_getpropint(sc->sc_node, "phy-handle", 0);
		if (phy)
			node = OF_getnodebyphandle(phy);
		rxc = OF_getpropint(node, "rxc-skew-ps", 1400) / 200;
		rxdv = OF_getpropint(node, "rxdv-skew-ps", 1400) / 200;
		txc = OF_getpropint(node, "txc-skew-ps", 1400) / 200;
		txen = OF_getpropint(node, "txen-skew-ps", 1400) / 200;
		rxd0 = OF_getpropint(node, "rxd0-skew-ps", 1400) / 200;
		rxd1 = OF_getpropint(node, "rxd1-skew-ps", 1400) / 200;
		rxd2 = OF_getpropint(node, "rxd2-skew-ps", 1400) / 200;
		rxd3 = OF_getpropint(node, "rxd3-skew-ps", 1400) / 200;
		txd0 = OF_getpropint(node, "txd0-skew-ps", 1400) / 200;
		txd1 = OF_getpropint(node, "txd1-skew-ps", 1400) / 200;
		txd2 = OF_getpropint(node, "txd2-skew-ps", 1400) / 200;
		txd3 = OF_getpropint(node, "txd3-skew-ps", 1400) / 200;

		val = ((rxc & 0xf) << 12) | ((rxdv & 0xf) << 8) |
		    ((txc & 0xf) << 4) | ((txen & 0xf) << 0);
		fec_miibus_writereg(dev, phy, 0x0b, 0x8104);
		fec_miibus_writereg(dev, phy, 0x0c, val);

		val = ((rxd3 & 0xf) << 12) | ((rxd2 & 0xf) << 8) |
		    ((rxd1 & 0xf) << 4) | ((rxd0 & 0xf) << 0);
		fec_miibus_writereg(dev, phy, 0x0b, 0x8105);
		fec_miibus_writereg(dev, phy, 0x0c, val);

		val = ((txd3 & 0xf) << 12) | ((txd2 & 0xf) << 8) |
		    ((txd1 & 0xf) << 4) | ((txd0 & 0xf) << 0);
		fec_miibus_writereg(dev, phy, 0x0b, 0x8106);
		fec_miibus_writereg(dev, phy, 0x0c, val);
	}

	if (child->mii_oui == MII_OUI_MICREL &&
	    child->mii_model == MII_MODEL_MICREL_KSZ9031) {
		uint32_t rxc, rxdv, txc, txen;
		uint32_t rxd0, rxd1, rxd2, rxd3;
		uint32_t txd0, txd1, txd2, txd3;
		uint32_t val, phy;
		int node;

		node = sc->sc_node;
		phy = OF_getpropint(sc->sc_node, "phy-handle", 0);
		if (phy)
			node = OF_getnodebyphandle(phy);
		rxc = OF_getpropint(node, "rxc-skew-ps", 900) / 60;
		rxdv = OF_getpropint(node, "rxdv-skew-ps", 420) / 60;
		txc = OF_getpropint(node, "txc-skew-ps", 900) / 60;
		txen = OF_getpropint(node, "txen-skew-ps", 420) / 60;
		rxd0 = OF_getpropint(node, "rxd0-skew-ps", 420) / 60;
		rxd1 = OF_getpropint(node, "rxd1-skew-ps", 420) / 60;
		rxd2 = OF_getpropint(node, "rxd2-skew-ps", 420) / 60;
		rxd3 = OF_getpropint(node, "rxd3-skew-ps", 420) / 60;
		txd0 = OF_getpropint(node, "txd0-skew-ps", 420) / 60;
		txd1 = OF_getpropint(node, "txd1-skew-ps", 420) / 60;
		txd2 = OF_getpropint(node, "txd2-skew-ps", 420) / 60;
		txd3 = OF_getpropint(node, "txd3-skew-ps", 420) / 60;

		val = ((rxdv & 0xf) << 4) || ((txen & 0xf) << 0);
		fec_miibus_writereg(dev, phy, 0x0d, 0x0002);
		fec_miibus_writereg(dev, phy, 0x0e, 0x0004);
		fec_miibus_writereg(dev, phy, 0x0d, 0x4002);
		fec_miibus_writereg(dev, phy, 0x0e, val);

		val = ((rxd3 & 0xf) << 12) | ((rxd2 & 0xf) << 8) |
		    ((rxd1 & 0xf) << 4) | ((rxd0 & 0xf) << 0);
		fec_miibus_writereg(dev, phy, 0x0d, 0x0002);
		fec_miibus_writereg(dev, phy, 0x0e, 0x0005);
		fec_miibus_writereg(dev, phy, 0x0d, 0x4002);
		fec_miibus_writereg(dev, phy, 0x0e, val);

		val = ((txd3 & 0xf) << 12) | ((txd2 & 0xf) << 8) |
		    ((txd1 & 0xf) << 4) | ((txd0 & 0xf) << 0);
		fec_miibus_writereg(dev, phy, 0x0d, 0x0002);
		fec_miibus_writereg(dev, phy, 0x0e, 0x0006);
		fec_miibus_writereg(dev, phy, 0x0d, 0x4002);
		fec_miibus_writereg(dev, phy, 0x0e, val);

		val = ((txc & 0x1f) << 5) || ((rxc & 0x1f) << 0);
		fec_miibus_writereg(dev, phy, 0x0d, 0x0002);
		fec_miibus_writereg(dev, phy, 0x0e, 0x0008);
		fec_miibus_writereg(dev, phy, 0x0d, 0x4002);
		fec_miibus_writereg(dev, phy, 0x0e, val);
	}
}

void
fec_init_rxd(struct fec_softc *sc)
{
	struct fec_desc *rxd;

	sc->sc_rx_prod = sc->sc_rx_cons = 0;

	memset(sc->sc_rxdesc, 0, ENET_DMA_LEN(sc->sc_rxring));
	rxd = &sc->sc_rxdesc[ENET_NRXDESC - 1];
	rxd->fd_status = ENET_RXD_WRAP;
}

void
fec_init_txd(struct fec_softc *sc)
{
	struct fec_desc *txd;

	sc->sc_tx_prod = sc->sc_tx_cons = 0;
	sc->sc_tx_cnt = 0;

	memset(sc->sc_txdesc, 0, ENET_DMA_LEN(sc->sc_txring));
	txd = &sc->sc_txdesc[ENET_NTXDESC - 1];
	txd->fd_status = ENET_TXD_WRAP;
}

void
fec_init(struct fec_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int speed = 0;

	/* reset the controller */
	HSET4(sc, ENET_ECR, ENET_ECR_RESET);
	while (HREAD4(sc, ENET_ECR) & ENET_ECR_ETHEREN)
		continue;

	/* set hw address */
	HWRITE4(sc, ENET_PALR,
	    (sc->sc_ac.ac_enaddr[0] << 24) |
	    (sc->sc_ac.ac_enaddr[1] << 16) |
	    (sc->sc_ac.ac_enaddr[2] << 8) |
	     sc->sc_ac.ac_enaddr[3]);
	HWRITE4(sc, ENET_PAUR,
	    (sc->sc_ac.ac_enaddr[4] << 24) |
	    (sc->sc_ac.ac_enaddr[5] << 16));

	/* clear outstanding interrupts */
	HWRITE4(sc, ENET_EIR, 0xffffffff);

	/* set max receive buffer size, 3-0 bits always zero for alignment */
	HWRITE4(sc, ENET_MRBR, ENET_MAX_PKT_SIZE);

	/* init descriptor */
	fec_init_txd(sc);
	fec_init_rxd(sc);

	/* fill RX ring */
	if_rxr_init(&sc->sc_rx_ring, 2, ENET_NRXDESC);
	fec_fill_rx_ring(sc);

	bus_dmamap_sync(sc->sc_dmat, ENET_DMA_MAP(sc->sc_txring),
	    0, ENET_DMA_LEN(sc->sc_txring), BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, ENET_DMA_MAP(sc->sc_rxring),
	    0, ENET_DMA_LEN(sc->sc_rxring), BUS_DMASYNC_PREWRITE);

	/* set descriptor */
	HWRITE4(sc, ENET_TDSR, ENET_DMA_DVA(sc->sc_txring));
	HWRITE4(sc, ENET_RDSR, ENET_DMA_DVA(sc->sc_rxring));

	/* set it to full-duplex */
	HWRITE4(sc, ENET_TCR, ENET_TCR_FDEN);

	/*
	 * Set max frame length to 1518 or 1522 with VLANs,
	 * pause frames and promisc mode.
	 * XXX: RGMII mode - phy dependant
	 */
	HWRITE4(sc, ENET_RCR,
	    ENET_RCR_MAX_FL(1522) | ENET_RCR_RGMII_MODE | ENET_RCR_MII_MODE |
	    ENET_RCR_FCE);

	HWRITE4(sc, ENET_MSCR, (sc->sc_phy_speed << 1) | 0x100);

	HWRITE4(sc, ENET_RACC, ENET_RACC_SHIFT16);
	HWRITE4(sc, ENET_FTRL, ENET_MAX_BUF_SIZE);

	/* RX FIFO threshold and pause */
	HWRITE4(sc, ENET_RSEM, 0x84);
	HWRITE4(sc, ENET_RSFL, 16);
	HWRITE4(sc, ENET_RAEM, 8);
	HWRITE4(sc, ENET_RAFL, 8);
	HWRITE4(sc, ENET_OPD, 0xFFF0);

	/* do store and forward, only i.MX6, needs to be set correctly else */
	HWRITE4(sc, ENET_TFWR, ENET_TFWR_STRFWD);

	/* enable gigabit-ethernet and set it to support little-endian */
	switch (IFM_SUBTYPE(sc->sc_mii.mii_media_active)) {
	case IFM_1000_T:  /* Gigabit */
		speed |= ENET_ECR_SPEED;
		break;
	default:
		speed &= ~ENET_ECR_SPEED;
	}
	HWRITE4(sc, ENET_ECR, ENET_ECR_ETHEREN | speed | ENET_ECR_DBSWP);

#ifdef ENET_ENHANCED_BD
	HSET4(sc, ENET_ECR, ENET_ECR_EN1588);
#endif

	/* rx descriptors are ready */
	HWRITE4(sc, ENET_RDAR, ENET_RDAR_RDAR);

	/* program promiscuous mode and multicast filters */
	fec_iff(sc);

	timeout_add_sec(&sc->sc_tick, 1);

	/* Indicate we are up and running. */
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/* enable interrupts for tx/rx */
	HWRITE4(sc, ENET_EIMR, ENET_EIR_TXF | ENET_EIR_RXF);

	fec_start(ifp);
}

void
fec_stop(struct fec_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct fec_buf *txb, *rxb;
	int i;

	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp->if_flags &= ~IFF_RUNNING;
	ifp->if_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);

	timeout_del(&sc->sc_tick);

	/* reset the controller */
	HSET4(sc, ENET_ECR, ENET_ECR_RESET);
	while (HREAD4(sc, ENET_ECR) & ENET_ECR_ETHEREN)
		continue;

	HWRITE4(sc, ENET_MSCR, (sc->sc_phy_speed << 1) | 0x100);

	for (i = 0; i < ENET_NTXDESC; i++) {
		txb = &sc->sc_txbuf[i];
		if (txb->fb_m == NULL)
			continue;
		bus_dmamap_sync(sc->sc_dmat, txb->fb_map, 0,
		    txb->fb_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, txb->fb_map);
		m_freem(txb->fb_m);
		m_freem(txb->fb_m0);
		txb->fb_m = txb->fb_m0 = NULL;
	}
	for (i = 0; i < ENET_NRXDESC; i++) {
		rxb = &sc->sc_rxbuf[i];
		if (rxb->fb_m == NULL)
			continue;
		bus_dmamap_sync(sc->sc_dmat, rxb->fb_map, 0,
		    rxb->fb_map->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, rxb->fb_map);
		if_rxr_put(&sc->sc_rx_ring, 1);
		rxb->fb_m = NULL;
	}
}

void
fec_iff(struct fec_softc *sc)
{
	struct arpcom *ac = &sc->sc_ac;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint64_t ghash = 0, ihash = 0;
	uint32_t h;

	ifp->if_flags &= ~IFF_ALLMULTI;

	if (ifp->if_flags & IFF_PROMISC) {
		ifp->if_flags |= IFF_ALLMULTI;
		ihash = 0xffffffffffffffffLLU;
	} else if (ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		ghash = 0xffffffffffffffffLLU;
	} else {
		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			h = ether_crc32_le(enm->enm_addrlo, ETHER_ADDR_LEN);

			ghash |= 1LLU << (((uint8_t *)&h)[3] >> 2);

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	HWRITE4(sc, ENET_GAUR, (uint32_t)(ghash >> 32));
	HWRITE4(sc, ENET_GALR, (uint32_t)ghash);

	HWRITE4(sc, ENET_IAUR, (uint32_t)(ihash >> 32));
	HWRITE4(sc, ENET_IALR, (uint32_t)ihash);
}

int
fec_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct fec_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			fec_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				fec_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				fec_stop(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			fec_iff(sc);
		error = 0;
	}

	splx(s);
	return(error);
}

void
fec_start(struct ifnet *ifp)
{
	struct fec_softc *sc = ifp->if_softc;
	struct mbuf *m = NULL;
	int error, idx;

	if (!(ifp->if_flags & IFF_RUNNING))
		return;
	if (ifq_is_oactive(&ifp->if_snd))
		return;
	if (ifq_empty(&ifp->if_snd))
		return;

	idx = sc->sc_tx_prod;
	while ((sc->sc_txdesc[idx].fd_status & ENET_TXD_READY) == 0) {
		m = ifq_deq_begin(&ifp->if_snd);
		if (m == NULL)
			break;

		error = fec_encap(sc, m, &idx);
		if (error == ENOBUFS) {
			ifq_deq_rollback(&ifp->if_snd, m);
			ifq_set_oactive(&ifp->if_snd);
			break;
		}
		if (error == EFBIG) {
			ifq_deq_commit(&ifp->if_snd, m);
			m_freem(m); /* give up: drop it */
			ifp->if_oerrors++;
			continue;
		}

		ifq_deq_commit(&ifp->if_snd, m);

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
	}

	if (sc->sc_tx_prod != idx) {
		sc->sc_tx_prod = idx;

		/* Set a timeout in case the chip goes out to lunch. */
		ifp->if_timer = 5;
	}
}

int
fec_encap(struct fec_softc *sc, struct mbuf *m0, int *idx)
{
	struct fec_desc *txd, *txd_start;
	bus_dmamap_t map;
	struct mbuf *m;
	int cur, frag, i;
	int ret;

	m = m0;
	cur = frag = *idx;
	map = sc->sc_txbuf[cur].fb_map;

	if (sc->sc_tx_bounce) {
		m = m_dup_pkt(m0, 0, M_DONTWAIT);
		if (m == NULL) {
			ret = ENOBUFS;
			goto fail;
		}
	}

	if (bus_dmamap_load_mbuf(sc->sc_dmat, map, m, BUS_DMA_NOWAIT)) {
		if (m_defrag(m, M_DONTWAIT)) {
			ret = EFBIG;
			goto fail;
		}
		if (bus_dmamap_load_mbuf(sc->sc_dmat, map, m, BUS_DMA_NOWAIT)) {
			ret = EFBIG;
			goto fail;
		}
	}

	if (map->dm_nsegs > (ENET_NTXDESC - sc->sc_tx_cnt - 2)) {
		bus_dmamap_unload(sc->sc_dmat, map);
		ret = ENOBUFS;
		goto fail;
	}

	/* Sync the DMA map. */
	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	txd = txd_start = &sc->sc_txdesc[frag];
	for (i = 0; i < map->dm_nsegs; i++) {
		txd->fd_addr = map->dm_segs[i].ds_addr;
		txd->fd_len = map->dm_segs[i].ds_len;
		txd->fd_status &= ENET_TXD_WRAP;
		if (i == (map->dm_nsegs - 1))
			txd->fd_status |= ENET_TXD_LAST | ENET_TXD_TC;
		if (i != 0)
			txd->fd_status |= ENET_TXD_READY;

		bus_dmamap_sync(sc->sc_dmat, ENET_DMA_MAP(sc->sc_txring),
		    frag * sizeof(*txd), sizeof(*txd), BUS_DMASYNC_PREWRITE);

		cur = frag;
		if (frag == (ENET_NTXDESC - 1)) {
			txd = &sc->sc_txdesc[0];
			frag = 0;
		} else {
			txd++;
			frag++;
		}
		KASSERT(frag != sc->sc_tx_cons);
	}

	txd_start->fd_status |= ENET_TXD_READY;
	bus_dmamap_sync(sc->sc_dmat, ENET_DMA_MAP(sc->sc_txring),
	    *idx * sizeof(*txd), sizeof(*txd), BUS_DMASYNC_PREWRITE);

	HWRITE4(sc, ENET_TDAR, ENET_TDAR_TDAR);

	KASSERT(sc->sc_txbuf[cur].fb_m == NULL);
	KASSERT(sc->sc_txbuf[cur].fb_m0 == NULL);
	sc->sc_txbuf[*idx].fb_map = sc->sc_txbuf[cur].fb_map;
	sc->sc_txbuf[cur].fb_map = map;
	sc->sc_txbuf[cur].fb_m = m;
	if (m != m0)
		sc->sc_txbuf[cur].fb_m0 = m0;

	sc->sc_tx_cnt += map->dm_nsegs;
	*idx = frag;

	return (0);

fail:
	if (m != m0)
		m_freem(m);
	return (ret);
}

/*
 * Established by attachment driver at interrupt priority IPL_NET.
 */
int
fec_intr(void *arg)
{
	struct fec_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	u_int32_t status;

	/* Find out which interrupts are pending. */
	status = HREAD4(sc, ENET_EIR);

	/* Acknowledge the interrupts we are about to handle. */
	status &= (ENET_EIR_RXF | ENET_EIR_TXF);
	HWRITE4(sc, ENET_EIR, status);

	/*
	 * Handle incoming packets.
	 */
	if (ISSET(status, ENET_EIR_RXF))
		fec_rx_proc(sc);

	/*
	 * Handle transmitted packets.
	 */
	if (ISSET(status, ENET_EIR_TXF))
		fec_tx_proc(sc);

	/* Try to transmit. */
	if (ifp->if_flags & IFF_RUNNING && !ifq_empty(&ifp->if_snd))
		fec_start(ifp);

	return 1;
}

void
fec_tx_proc(struct fec_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct fec_desc *txd;
	struct fec_buf *txb;
	int idx;

	bus_dmamap_sync(sc->sc_dmat, ENET_DMA_MAP(sc->sc_txring), 0,
	    ENET_DMA_LEN(sc->sc_txring),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	while (sc->sc_tx_cnt > 0) {
		idx = sc->sc_tx_cons;
		KASSERT(idx < ENET_NTXDESC);

		txd = &sc->sc_txdesc[idx];
		if (txd->fd_status & ENET_TXD_READY)
			break;

		txb = &sc->sc_txbuf[idx];
		if (txb->fb_m) {
			bus_dmamap_sync(sc->sc_dmat, txb->fb_map, 0,
			    txb->fb_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, txb->fb_map);

			m_freem(txb->fb_m);
			m_freem(txb->fb_m0);
			txb->fb_m = txb->fb_m0 = NULL;
		}

		ifq_clr_oactive(&ifp->if_snd);

		sc->sc_tx_cnt--;

		if (sc->sc_tx_cons == (ENET_NTXDESC - 1))
			sc->sc_tx_cons = 0;
		else
			sc->sc_tx_cons++;

		txd->fd_status &= ENET_TXD_WRAP;
	}

	if (sc->sc_tx_cnt == 0)
		ifp->if_timer = 0;
	else /* ERR006358 */
		HWRITE4(sc, ENET_TDAR, ENET_TDAR_TDAR);
}

void
fec_rx_proc(struct fec_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct fec_desc *rxd;
	struct fec_buf *rxb;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	int idx, len;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	bus_dmamap_sync(sc->sc_dmat, ENET_DMA_MAP(sc->sc_rxring), 0,
	    ENET_DMA_LEN(sc->sc_rxring),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	while (if_rxr_inuse(&sc->sc_rx_ring) > 0) {
		idx = sc->sc_rx_cons;
		KASSERT(idx < ENET_NRXDESC);

		rxd = &sc->sc_rxdesc[idx];
		if (rxd->fd_status & ENET_RXD_EMPTY)
			break;

		len = rxd->fd_len;
		rxb = &sc->sc_rxbuf[idx];
		KASSERT(rxb->fb_m);

		bus_dmamap_sync(sc->sc_dmat, rxb->fb_map, 0,
		    len, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, rxb->fb_map);

		/* Strip off CRC. */
		len -= ETHER_CRC_LEN;
		KASSERT(len > 0);

		m = rxb->fb_m;
		rxb->fb_m = NULL;

		m_adj(m, ETHER_ALIGN);
		m->m_pkthdr.len = m->m_len = len;

		ml_enqueue(&ml, m);

		if_rxr_put(&sc->sc_rx_ring, 1);
		if (sc->sc_rx_cons == (ENET_NRXDESC - 1))
			sc->sc_rx_cons = 0;
		else
			sc->sc_rx_cons++;
	}

	if (ifiq_input(&ifp->if_rcv, &ml))
		if_rxr_livelocked(&sc->sc_rx_ring);

	fec_fill_rx_ring(sc);

	bus_dmamap_sync(sc->sc_dmat, ENET_DMA_MAP(sc->sc_rxring), 0,
	    ENET_DMA_LEN(sc->sc_rxring),
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	/* rx descriptors are ready */
	HWRITE4(sc, ENET_RDAR, ENET_RDAR_RDAR);
}

void
fec_tick(void *arg)
{
	struct fec_softc *sc = arg;
	int s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	splx(s);

	timeout_add_sec(&sc->sc_tick, 1);
}

/*
 * MII
 * Interrupts need ENET_ECR_ETHEREN to be set,
 * so we just read the interrupt status registers.
 */
int
fec_miibus_readreg(struct device *dev, int phy, int reg)
{
	int r = 0;
	struct fec_softc *sc = (struct fec_softc *)dev;

	HWRITE4(sc, ENET_EIR, ENET_EIR_MII);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ENET_MMFR,
	    ENET_MMFR_ST | ENET_MMFR_OP_RD | ENET_MMFR_TA |
	    phy << ENET_MMFR_PA_SHIFT | reg << ENET_MMFR_RA_SHIFT);

	while(!(HREAD4(sc, ENET_EIR) & ENET_EIR_MII));

	r = bus_space_read_4(sc->sc_iot, sc->sc_ioh, ENET_MMFR);

	return (r & 0xffff);
}

void
fec_miibus_writereg(struct device *dev, int phy, int reg, int val)
{
	struct fec_softc *sc = (struct fec_softc *)dev;

	HWRITE4(sc, ENET_EIR, ENET_EIR_MII);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ENET_MMFR,
	    ENET_MMFR_ST | ENET_MMFR_OP_WR | ENET_MMFR_TA |
	    phy << ENET_MMFR_PA_SHIFT | reg << ENET_MMFR_RA_SHIFT |
	    (val & 0xffff));

	while(!(HREAD4(sc, ENET_EIR) & ENET_EIR_MII));

	return;
}

void
fec_miibus_statchg(struct device *dev)
{
	struct fec_softc *sc = (struct fec_softc *)dev;
	uint32_t ecr, rcr;

	ecr = HREAD4(sc, ENET_ECR) & ~ENET_ECR_SPEED;
	rcr = HREAD4(sc, ENET_RCR) & ~ENET_RCR_RMII_10T;
	switch (IFM_SUBTYPE(sc->sc_mii.mii_media_active)) {
	case IFM_1000_T:  /* Gigabit */
		ecr |= ENET_ECR_SPEED;
		break;
	case IFM_100_TX:
		break;
	case IFM_10_T:
		rcr |= ENET_RCR_RMII_10T;
		break;
	}
	HWRITE4(sc, ENET_ECR, ecr);
	HWRITE4(sc, ENET_RCR, rcr);

	return;
}

int
fec_ifmedia_upd(struct ifnet *ifp)
{
	struct fec_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_mii;
	int err;
	if (mii->mii_instance) {
		struct mii_softc *miisc;

		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	err = mii_mediachg(mii);
	return (err);
}

void
fec_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct fec_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_mii;

	mii_pollstat(mii);

	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

/*
 * Manage DMA'able memory.
 */
struct fec_dmamem *
fec_dmamem_alloc(struct fec_softc *sc, bus_size_t size, bus_size_t align)
{
	struct fec_dmamem *fdm;
	int nsegs;

	fdm = malloc(sizeof(*fdm), M_DEVBUF, M_WAITOK | M_ZERO);
	fdm->fdm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, &fdm->fdm_map) != 0)
		goto fdmfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, align, 0, &fdm->fdm_seg, 1,
	    &nsegs, BUS_DMA_WAITOK) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &fdm->fdm_seg, nsegs, size,
	    &fdm->fdm_kva, BUS_DMA_WAITOK | BUS_DMA_COHERENT) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, fdm->fdm_map, fdm->fdm_kva, size,
	    NULL, BUS_DMA_WAITOK) != 0)
		goto unmap;

	return (fdm);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, fdm->fdm_kva, size);
free:
	bus_dmamem_free(sc->sc_dmat, &fdm->fdm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, fdm->fdm_map);
fdmfree:
	free(fdm, M_DEVBUF, sizeof(*fdm));

	return (NULL);
}

void
fec_dmamem_free(struct fec_softc *sc, struct fec_dmamem *fdm)
{
	bus_dmamem_unmap(sc->sc_dmat, fdm->fdm_kva, fdm->fdm_size);
	bus_dmamem_free(sc->sc_dmat, &fdm->fdm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, fdm->fdm_map);
	free(fdm, M_DEVBUF, sizeof(*fdm));
}

struct mbuf *
fec_alloc_mbuf(struct fec_softc *sc, bus_dmamap_t map)
{
	struct mbuf *m = NULL;

	m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
	if (!m)
		return (NULL);
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	if (bus_dmamap_load_mbuf(sc->sc_dmat, map, m, BUS_DMA_NOWAIT) != 0) {
		printf("%s: could not load mbuf DMA map",
		    sc->sc_dev.dv_xname);
		m_freem(m);
		return (NULL);
	}

	bus_dmamap_sync(sc->sc_dmat, map, 0,
	    m->m_pkthdr.len, BUS_DMASYNC_PREREAD);

	return (m);
}

void
fec_fill_rx_ring(struct fec_softc *sc)
{
	struct fec_desc *rxd;
	struct fec_buf *rxb;
	u_int slots;

	for (slots = if_rxr_get(&sc->sc_rx_ring, ENET_NRXDESC);
	    slots > 0; slots--) {
		rxb = &sc->sc_rxbuf[sc->sc_rx_prod];
		rxb->fb_m = fec_alloc_mbuf(sc, rxb->fb_map);
		if (rxb->fb_m == NULL)
			break;
		rxd = &sc->sc_rxdesc[sc->sc_rx_prod];
		rxd->fd_len = rxb->fb_map->dm_segs[0].ds_len - 1;
		rxd->fd_addr = rxb->fb_map->dm_segs[0].ds_addr;
		rxd->fd_status &= ENET_RXD_WRAP;
		rxd->fd_status |= ENET_RXD_EMPTY;

		if (sc->sc_rx_prod == (ENET_NRXDESC - 1))
			sc->sc_rx_prod = 0;
		else
			sc->sc_rx_prod++;
	}
	if_rxr_put(&sc->sc_rx_ring, slots);
}
