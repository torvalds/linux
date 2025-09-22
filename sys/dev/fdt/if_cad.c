/*	$OpenBSD: if_cad.c,v 1.16 2025/09/17 09:17:12 kettenis Exp $	*/

/*
 * Copyright (c) 2021-2022 Visa Hankala
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

/*
 * Driver for Cadence 10/100/Gigabit Ethernet device.
 */

#include "bpfilter.h"
#include "kstat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/mutex.h>
#include <sys/kstat.h>
#include <sys/rwlock.h>
#include <sys/task.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/if_media.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/miidevs.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>

#define GEM_NETCTL			0x0000
#define  GEM_NETCTL_DPRAM			(1 << 18)
#define  GEM_NETCTL_STARTTX			(1 << 9)
#define  GEM_NETCTL_STATCLR			(1 << 5)
#define  GEM_NETCTL_MDEN			(1 << 4)
#define  GEM_NETCTL_TXEN			(1 << 3)
#define  GEM_NETCTL_RXEN			(1 << 2)
#define GEM_NETCFG			0x0004
#define  GEM_NETCFG_SGMIIEN			(1 << 27)
#define  GEM_NETCFG_RXCSUMEN			(1 << 24)
#define  GEM_NETCFG_MDCCLKDIV_MASK		(0x7 << 18)
#define  GEM_NETCFG_MDCCLKDIV_SHIFT		18
#define  GEM_NETCFG_FCSREM			(1 << 17)
#define  GEM_NETCFG_RXOFFS_MASK			(0x3 << 14)
#define  GEM_NETCFG_RXOFFS_SHIFT		14
#define  GEM_NETCFG_PCSSEL			(1 << 11)
#define  GEM_NETCFG_1000			(1 << 10)
#define  GEM_NETCFG_1536RXEN			(1 << 8)
#define  GEM_NETCFG_UCASTHASHEN			(1 << 7)
#define  GEM_NETCFG_MCASTHASHEN			(1 << 6)
#define  GEM_NETCFG_BCASTDI			(1 << 5)
#define  GEM_NETCFG_COPYALL			(1 << 4)
#define  GEM_NETCFG_FDEN			(1 << 1)
#define  GEM_NETCFG_100				(1 << 0)
#define GEM_NETSR			0x0008
#define  GEM_NETSR_PHY_MGMT_IDLE		(1 << 2)
#define GEM_DMACR			0x0010
#define  GEM_DMACR_DMA64			(1 << 30)
#define  GEM_DMACR_AHBDISC			(1 << 24)
#define  GEM_DMACR_RXBUF_MASK			(0xff << 16)
#define  GEM_DMACR_RXBUF_SHIFT			16
#define  GEM_DMACR_TXCSUMEN			(1 << 11)
#define  GEM_DMACR_TXSIZE			(1 << 10)
#define  GEM_DMACR_RXSIZE_MASK			(0x3 << 8)
#define  GEM_DMACR_RXSIZE_8K			(0x3 << 8)
#define  GEM_DMACR_ES_PDATA			(1 << 7)
#define  GEM_DMACR_ES_DESCR			(1 << 6)
#define  GEM_DMACR_BLEN_MASK			(0x1f << 0)
#define  GEM_DMACR_BLEN_16			(0x10 << 0)
#define GEM_TXSR			0x0014
#define  GEM_TXSR_TXGO				(1 << 3)
#define GEM_RXQBASE			0x0018
#define GEM_TXQBASE			0x001c
#define GEM_RXSR			0x0020
#define  GEM_RXSR_RXOVR				(1 << 2)
#define GEM_ISR				0x0024
#define GEM_IER				0x0028
#define GEM_IDR				0x002c
#define  GEM_IXR_HRESP				(1 << 11)
#define  GEM_IXR_RXOVR				(1 << 10)
#define  GEM_IXR_TXDONE				(1 << 7)
#define  GEM_IXR_TXURUN				(1 << 6)
#define  GEM_IXR_RETRY				(1 << 5)
#define  GEM_IXR_TXUSED				(1 << 3)
#define  GEM_IXR_RXUSED				(1 << 2)
#define  GEM_IXR_RXDONE				(1 << 1)
#define GEM_PHYMNTNC			0x0034
#define  GEM_PHYMNTNC_CLAUSE_22			(1 << 30)
#define  GEM_PHYMNTNC_OP_READ			(0x2 << 28)
#define  GEM_PHYMNTNC_OP_WRITE			(0x1 << 28)
#define  GEM_PHYMNTNC_ADDR_MASK			(0x1f << 23)
#define  GEM_PHYMNTNC_ADDR_SHIFT		23
#define  GEM_PHYMNTNC_REG_MASK			(0x1f << 18)
#define  GEM_PHYMNTNC_REG_SHIFT			18
#define  GEM_PHYMNTNC_MUST_10			(0x2 << 16)
#define  GEM_PHYMNTNC_DATA_MASK			0xffff
#define GEM_HASHL			0x0080
#define GEM_HASHH			0x0084
#define GEM_LADDRL(i)			(0x0088 + (i) * 8)
#define GEM_LADDRH(i)			(0x008c + (i) * 8)
#define GEM_LADDRNUM			4
#define GEM_MID				0x00fc
#define  GEM_MID_VERSION_MASK			(0xfff << 16)
#define  GEM_MID_VERSION_SHIFT			16
#define GEM_OCTTXL			0x0100
#define GEM_OCTTXH			0x0104
#define GEM_TXCNT			0x0108
#define GEM_TXBCCNT			0x010c
#define GEM_TXMCCNT			0x0110
#define GEM_TXPAUSECNT			0x0114
#define GEM_TX64CNT			0x0118
#define GEM_TX65CNT			0x011c
#define GEM_TX128CNT			0x0120
#define GEM_TX256CNT			0x0124
#define GEM_TX512CNT			0x0128
#define GEM_TX1024CNT			0x012c
#define GEM_TXURUNCNT			0x0134
#define GEM_SNGLCOLLCNT			0x0138
#define GEM_MULTICOLLCNT		0x013c
#define GEM_EXCESSCOLLCNT		0x0140
#define GEM_LATECOLLCNT			0x0144
#define GEM_TXDEFERCNT			0x0148
#define GEM_TXCSENSECNT			0x014c
#define GEM_OCTRXL			0x0150
#define GEM_OCTRXH			0x0154
#define GEM_RXCNT			0x0158
#define GEM_RXBROADCNT			0x015c
#define GEM_RXMULTICNT			0x0160
#define GEM_RXPAUSECNT			0x0164
#define GEM_RX64CNT			0x0168
#define GEM_RX65CNT			0x016c
#define GEM_RX128CNT			0x0170
#define GEM_RX256CNT			0x0174
#define GEM_RX512CNT			0x0178
#define GEM_RX1024CNT			0x017c
#define GEM_RXUNDRCNT			0x0184
#define GEM_RXOVRCNT			0x0188
#define GEM_RXJABCNT			0x018c
#define GEM_RXFCSCNT			0x0190
#define GEM_RXLENGTHCNT			0x0194
#define GEM_RXSYMBCNT			0x0198
#define GEM_RXALIGNCNT			0x019c
#define GEM_RXRESERRCNT			0x01a0
#define GEM_RXORCNT			0x01a4
#define GEM_RXIPCCNT			0x01a8
#define GEM_RXTCPCCNT			0x01ac
#define GEM_RXUDPCCNT			0x01b0
#define GEM_CFG6			0x0294
#define  GEM_CFG6_DMA64				(1 << 23)
#define  GEM_CFG6_PRIQ_MASK(x)			((x) & 0xffff)
#define GEM_CFG8			0x029c
#define  GEM_CFG8_NUM_TYPE1_SCR(x)		(((x) >> 24) & 0xff)
#define  GEM_CFG8_NUM_TYPE2_SCR(x)		(((x) >> 16) & 0xff)
#define GEM_TXQ1BASE(i)			(0x0440 + (i) * 4)
#define  GEM_TXQ1BASE_DISABLE			(1 << 0)
#define GEM_RXQ1BASE(i)			(0x0480 + (i) * 4)
#define  GEM_RXQ1BASE_DISABLE			(1 << 0)
#define GEM_TXQBASEHI			0x04c8
#define GEM_RXQBASEHI			0x04d4
#define GEM_SCR_TYPE1(i)		(0x0500 + (i) * 4)
#define GEM_SCR_TYPE2(i)		(0x0540 + (i) * 4)
#define GEM_RXQ8BASE(i)			(0x05c0 + (i) * 4)
#define  GEM_RXQ8BASE_DISABLE			(1 << 0)

#define GEM_MAX_PRIQ		16

#define GEM_CLK_TX		"tx_clk"

struct cad_buf {
	bus_dmamap_t		bf_map;
	struct mbuf		*bf_m;
};

struct cad_dmamem {
	bus_dmamap_t		cdm_map;
	bus_dma_segment_t	cdm_seg;
	size_t			cdm_size;
	caddr_t			cdm_kva;
};

struct cad_desc32 {
	uint32_t		d_addr;
	uint32_t		d_status;
};

struct cad_desc64 {
	uint32_t		d_addrlo;
	uint32_t		d_status;
	uint32_t		d_addrhi;
	uint32_t		d_unused;
};

#define GEM_RXD_ADDR_WRAP	(1 << 1)
#define GEM_RXD_ADDR_USED	(1 << 0)

#define GEM_RXD_BCAST		(1U << 31)
#define GEM_RXD_MCAST		(1 << 30)
#define GEM_RXD_UCAST		(1 << 29)
#define GEM_RXD_SPEC		(1 << 27)
#define GEM_RXD_SPEC_MASK	(0x3 << 25)
#define GEM_RXD_CSUM_MASK	(0x3 << 22)
#define GEM_RXD_CSUM_UDP_OK	(0x3 << 22)
#define GEM_RXD_CSUM_TCP_OK	(0x2 << 22)
#define GEM_RXD_CSUM_IP_OK	(0x1 << 22)
#define GEM_RXD_VLANTAG		(1 << 21)
#define GEM_RXD_PRIOTAG		(1 << 20)
#define GEM_RXD_CFI		(1 << 16)
#define GEM_RXD_EOF		(1 << 15)
#define GEM_RXD_SOF		(1 << 14)
#define GEM_RXD_BADFCS		(1 << 13)
#define GEM_RXD_LEN_MASK	0x1fff

#define GEM_TXD_USED		(1U << 31)
#define GEM_TXD_WRAP		(1 << 30)
#define GEM_TXD_RLIMIT		(1 << 29)
#define GEM_TXD_CORRUPT		(1 << 27)
#define GEM_TXD_LCOLL		(1 << 26)
#define GEM_TXD_CSUMERR_MASK	(0x7 << 20)
#define GEM_TXD_NOFCS		(1 << 16)
#define GEM_TXD_LAST		(1 << 15)
#define GEM_TXD_LEN_MASK	0x3fff

#define CAD_NRXDESC		256

#define CAD_NTXDESC		256
#define CAD_NTXSEGS		16

enum cad_phy_mode {
	CAD_PHY_MODE_GMII,
	CAD_PHY_MODE_RGMII,
	CAD_PHY_MODE_RGMII_ID,
	CAD_PHY_MODE_RGMII_RXID,
	CAD_PHY_MODE_RGMII_TXID,
	CAD_PHY_MODE_SGMII,
};

struct cad_softc {
	struct device		sc_dev;
	struct arpcom		sc_ac;

	bus_dma_tag_t		sc_dmat;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	void			*sc_ih;
	int			sc_node;
	int			sc_phy_loc;
	enum cad_phy_mode	sc_phy_mode;
	unsigned char		sc_hw_tx_freq;
	unsigned char		sc_rxhang_erratum;
	unsigned char		sc_rxdone;
	unsigned char		sc_dma64;
	size_t			sc_descsize;
	uint32_t		sc_qmask;
	uint8_t			sc_ntype1scr;
	uint8_t			sc_ntype2scr;

	struct mii_data		sc_mii;
#define sc_media	sc_mii.mii_media
	struct timeout		sc_tick;

	struct cad_dmamem	*sc_txring;
	struct cad_buf		*sc_txbuf;
	caddr_t			sc_txdesc;
	unsigned int		sc_tx_prod;
	unsigned int		sc_tx_cons;

	struct if_rxring	sc_rx_ring;
	struct cad_dmamem	*sc_rxring;
	struct cad_buf		*sc_rxbuf;
	caddr_t			sc_rxdesc;
	unsigned int		sc_rx_prod;
	unsigned int		sc_rx_cons;
	uint32_t		sc_netctl;

	struct rwlock		sc_cfg_lock;
	struct task		sc_statchg_task;
	uint32_t		sc_tx_freq;

	struct mutex		sc_kstat_mtx;
	struct kstat		*sc_kstat;
};

#define HREAD4(sc, reg) \
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val) \
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

int	cad_match(struct device *, void *, void *);
void	cad_attach(struct device *, struct device *, void *);

int	cad_ioctl(struct ifnet *, u_long, caddr_t);
void	cad_start(struct ifqueue *);
void	cad_watchdog(struct ifnet *);

void	cad_reset(struct cad_softc *);
int	cad_up(struct cad_softc *);
void	cad_down(struct cad_softc *);
void	cad_iff(struct cad_softc *);
int	cad_intr(void *);
void	cad_tick(void *);
void	cad_statchg_task(void *);

int	cad_media_change(struct ifnet *);
void	cad_media_status(struct ifnet *, struct ifmediareq *);
int	cad_mii_readreg(struct device *, int, int);
void	cad_mii_writereg(struct device *, int, int, int);
void	cad_mii_statchg(struct device *);

struct cad_dmamem *cad_dmamem_alloc(struct cad_softc *, bus_size_t, bus_size_t);
void	cad_dmamem_free(struct cad_softc *, struct cad_dmamem *);
void	cad_rxfill(struct cad_softc *);
void	cad_rxeof(struct cad_softc *);
void	cad_txeof(struct cad_softc *);
unsigned int cad_encap(struct cad_softc *, struct mbuf *);
struct mbuf *cad_alloc_mbuf(struct cad_softc *, bus_dmamap_t);

#if NKSTAT > 0
void	cad_kstat_attach(struct cad_softc *);
int	cad_kstat_read(struct kstat *);
void	cad_kstat_tick(void *);
#endif

#ifdef DDB
struct cad_softc *cad_sc[4];
#endif

const struct cfattach cad_ca = {
	sizeof(struct cad_softc), cad_match, cad_attach
};

struct cfdriver cad_cd = {
	NULL, "cad", DV_IFNET
};

const struct {
	const char		*name;
	enum cad_phy_mode	mode;
} cad_phy_modes[] = {
	{ "gmii",	CAD_PHY_MODE_GMII },
	{ "rgmii",	CAD_PHY_MODE_RGMII },
	{ "rgmii-id",	CAD_PHY_MODE_RGMII_ID },
	{ "rgmii-rxid",	CAD_PHY_MODE_RGMII_RXID },
	{ "rgmii-txid",	CAD_PHY_MODE_RGMII_TXID },
	{ "sgmii",	CAD_PHY_MODE_SGMII },
};

int
cad_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "cdns,gem") ||
	    OF_is_compatible(faa->fa_node, "cdns,macb") ||
	    OF_is_compatible(faa->fa_node, "sifive,fu540-c000-gem") ||
	    OF_is_compatible(faa->fa_node, "sifive,fu740-c000-gem"));
}

void
cad_attach(struct device *parent, struct device *self, void *aux)
{
	char phy_mode[16];
	struct fdt_attach_args *faa = aux;
	struct cad_softc *sc = (struct cad_softc *)self;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint32_t phy_reset_gpio[3];
	uint32_t phy_reset_duration;
	uint32_t hi, lo;
	uint32_t rev, ver;
	uint32_t val;
	unsigned int i;
	int node, phy;
	int mii_flags;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_node = faa->fa_node;
	sc->sc_dmat = faa->fa_dmat;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh) != 0) {
		printf(": can't map registers\n");
		return;
	}

	if (OF_getprop(faa->fa_node, "local-mac-address", sc->sc_ac.ac_enaddr,
	    sizeof(sc->sc_ac.ac_enaddr)) != sizeof(sc->sc_ac.ac_enaddr)) {
		for (i = 0; i < GEM_LADDRNUM; i++) {
			lo = HREAD4(sc, GEM_LADDRL(i));
			hi = HREAD4(sc, GEM_LADDRH(i));
			if (lo != 0 || hi != 0) {
				sc->sc_ac.ac_enaddr[0] = lo;
				sc->sc_ac.ac_enaddr[1] = lo >> 8;
				sc->sc_ac.ac_enaddr[2] = lo >> 16;
				sc->sc_ac.ac_enaddr[3] = lo >> 24;
				sc->sc_ac.ac_enaddr[4] = hi;
				sc->sc_ac.ac_enaddr[5] = hi >> 8;
				break;
			}
		}
		if (i == GEM_LADDRNUM)
			ether_fakeaddr(ifp);
	}

	if (OF_getpropintarray(faa->fa_node, "phy-reset-gpios", phy_reset_gpio,
	    sizeof(phy_reset_gpio)) == sizeof(phy_reset_gpio)) {
		phy_reset_duration = OF_getpropint(faa->fa_node,
		    "phy-reset-duration", 1);
		if (phy_reset_duration > 1000)
			phy_reset_duration = 1;

		gpio_controller_config_pin(phy_reset_gpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(phy_reset_gpio, 1);
		delay((phy_reset_duration + 1) * 1000);
		gpio_controller_set_pin(phy_reset_gpio, 0);
		delay(1000);
	}

	phy = OF_getpropint(faa->fa_node, "phy-handle", 0);
	node = OF_getnodebyphandle(phy);
	if (node != 0)
		sc->sc_phy_loc = OF_getpropint(node, "reg", MII_PHY_ANY);
	else
		sc->sc_phy_loc = MII_PHY_ANY;

	sc->sc_phy_mode = CAD_PHY_MODE_RGMII_ID;
	OF_getprop(faa->fa_node, "phy-mode", phy_mode, sizeof(phy_mode));
	for (i = 0; i < nitems(cad_phy_modes); i++) {
		if (strcmp(phy_mode, cad_phy_modes[i].name) == 0) {
			sc->sc_phy_mode = cad_phy_modes[i].mode;
			break;
		}
	}

	rev = HREAD4(sc, GEM_MID);
	ver = (rev & GEM_MID_VERSION_MASK) >> GEM_MID_VERSION_SHIFT;

	sc->sc_descsize = sizeof(struct cad_desc32);
	/* Queue 0 is always present. */
	sc->sc_qmask = 0x1;
	/*
	 * Registers CFG1 and CFG6-10 are not present
	 * on Zynq-7000 / GEM version 0x2.
	 */
	if (ver >= 0x7) {
		val = HREAD4(sc, GEM_CFG6);
		if (val & GEM_CFG6_DMA64) {
			sc->sc_descsize = sizeof(struct cad_desc64);
			sc->sc_dma64 = 1;
		}
		sc->sc_qmask |= GEM_CFG6_PRIQ_MASK(val);

		val = HREAD4(sc, GEM_CFG8);
		sc->sc_ntype1scr = GEM_CFG8_NUM_TYPE1_SCR(val);
		sc->sc_ntype2scr = GEM_CFG8_NUM_TYPE2_SCR(val);
	}

	if (OF_is_compatible(faa->fa_node, "cdns,zynq-gem"))
		sc->sc_rxhang_erratum = 1;
	if (OF_is_compatible(faa->fa_node, "raspberrypi,rp1-gem"))
		sc->sc_hw_tx_freq = 1;

	rw_init(&sc->sc_cfg_lock, "cadcfg");
	timeout_set(&sc->sc_tick, cad_tick, sc);
	task_set(&sc->sc_statchg_task, cad_statchg_task, sc);

	rw_enter_write(&sc->sc_cfg_lock);
	cad_reset(sc);
	rw_exit_write(&sc->sc_cfg_lock);

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_NET | IPL_MPSAFE,
	    cad_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto fail;
	}

	ifp->if_softc = sc;
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags |= IFXF_MPSAFE;
	ifp->if_ioctl = cad_ioctl;
	ifp->if_qstart = cad_start;
	ifp->if_watchdog = cad_watchdog;
	ifp->if_hardmtu = ETHER_MAX_DIX_LEN - ETHER_HDR_LEN - ETHER_CRC_LEN;
	ifp->if_capabilities = IFCAP_VLAN_MTU;

	/*
	 * Enable transmit checksum offload only on reliable hardware.
	 * At least Zynq-7000 appears to generate bad UDP header checksum if
	 * the checksum field has not been initialized to zero and
	 * UDP payload size is less than three octets.
	 */
	if (0) {
		ifp->if_capabilities |= IFCAP_CSUM_IPv4 |
		    IFCAP_CSUM_TCPv4 | IFCAP_CSUM_UDPv4 |
		    IFCAP_CSUM_TCPv6 | IFCAP_CSUM_UDPv6;
	}

	printf(": rev 0x%x, address %s\n", rev,
	    ether_sprintf(sc->sc_ac.ac_enaddr));

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = cad_mii_readreg;
	sc->sc_mii.mii_writereg = cad_mii_writereg;
	sc->sc_mii.mii_statchg = cad_mii_statchg;
	ifmedia_init(&sc->sc_media, 0, cad_media_change, cad_media_status);

	switch (sc->sc_phy_mode) {
	case CAD_PHY_MODE_RGMII:
		mii_flags = MIIF_SETDELAY;
		break;
	case CAD_PHY_MODE_RGMII_RXID:
		mii_flags = MIIF_SETDELAY | MIIF_RXID;
		break;
	case CAD_PHY_MODE_RGMII_TXID:
		mii_flags = MIIF_SETDELAY | MIIF_TXID;
		break;
	case CAD_PHY_MODE_RGMII_ID:
		mii_flags = MIIF_SETDELAY | MIIF_RXID | MIIF_TXID;
		break;
	default:
		mii_flags = 0;
		break;
	}

	mii_attach(&sc->sc_dev, &sc->sc_mii, 0xffffffff, sc->sc_phy_loc,
	    MII_OFFSET_ANY, MIIF_NOISOLATE | mii_flags);

	if (LIST_EMPTY(&sc->sc_mii.mii_phys)) {
		printf("%s: no PHY found\n", sc->sc_dev.dv_xname);
		ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_MANUAL, 0, NULL);
		ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_MANUAL);
	} else {
		ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);
	}

	if_attach(ifp);
	ether_ifattach(ifp);

#if NKSTAT > 0
	cad_kstat_attach(sc);
#endif

#ifdef DDB
	if (sc->sc_dev.dv_unit < nitems(cad_sc))
		cad_sc[sc->sc_dev.dv_unit] = sc;
#endif

	return;

fail:
	if (sc->sc_ioh != 0)
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, faa->fa_reg[0].size);
}

int
cad_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct cad_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0, netlock_held = 1;
	int s;

	switch (cmd) {
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
	case SIOCGIFSFFPAGE:
		netlock_held = 0;
		break;
	}

	if (netlock_held)
		NET_UNLOCK();
	rw_enter_write(&sc->sc_cfg_lock);
	if (netlock_held)
		NET_LOCK();
	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */

	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = ENETRESET;
			else
				error = cad_up(sc);
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				cad_down(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;

	case SIOCGIFRXR:
		error = if_rxr_ioctl((struct if_rxrinfo *)ifr->ifr_data,
		    NULL, MCLBYTES, &sc->sc_rx_ring);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			cad_iff(sc);
		error = 0;
	}

	splx(s);
	rw_exit_write(&sc->sc_cfg_lock);

	return error;
}

void
cad_reset(struct cad_softc *sc)
{
	static const unsigned int mdcclk_divs[] = {
		8, 16, 32, 48, 64, 96, 128, 224
	};
	unsigned int freq, i;
	uint32_t div, netcfg;

	rw_assert_wrlock(&sc->sc_cfg_lock);

	HWRITE4(sc, GEM_NETCTL, 0);
	HWRITE4(sc, GEM_IDR, ~0U);
	HWRITE4(sc, GEM_RXSR, 0);
	HWRITE4(sc, GEM_TXSR, 0);
	if (sc->sc_dma64) {
		HWRITE4(sc, GEM_RXQBASEHI, 0);
		HWRITE4(sc, GEM_TXQBASEHI, 0);
	}
	HWRITE4(sc, GEM_RXQBASE, 0);
	HWRITE4(sc, GEM_TXQBASE, 0);

	for (i = 1; i < GEM_MAX_PRIQ; i++) {
		if (sc->sc_qmask & (1U << i)) {
			if (i < 8)
				HWRITE4(sc, GEM_RXQ1BASE(i - 1), 0);
			else
				HWRITE4(sc, GEM_RXQ8BASE(i - 8), 0);
			HWRITE4(sc, GEM_TXQ1BASE(i - 1), 0);
		}
	}

	/* Disable all screeners so that Rx goes through queue 0. */
	for (i = 0; i < sc->sc_ntype1scr; i++)
		HWRITE4(sc, GEM_SCR_TYPE1(i), 0);
	for (i = 0; i < sc->sc_ntype2scr; i++)
		HWRITE4(sc, GEM_SCR_TYPE2(i), 0);

	/* MDIO clock rate must not exceed 2.5 MHz. */
	freq = clock_get_frequency(sc->sc_node, "pclk");
	for (div = 0; div < nitems(mdcclk_divs) - 1; div++) {
		if (freq / mdcclk_divs[div] <= 2500000)
			break;
	}
	KASSERT(div < nitems(mdcclk_divs));

	netcfg = HREAD4(sc, GEM_NETCFG);
	netcfg &= ~GEM_NETCFG_MDCCLKDIV_MASK;
	netcfg |= div << GEM_NETCFG_MDCCLKDIV_SHIFT;
	HWRITE4(sc, GEM_NETCFG, netcfg);

	/* Enable MDIO bus. */
	sc->sc_netctl = GEM_NETCTL_MDEN;
	HWRITE4(sc, GEM_NETCTL, sc->sc_netctl);
}

int
cad_up(struct cad_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct cad_buf *rxb, *txb;
	struct cad_desc32 *desc32;
	struct cad_desc64 *desc64;
	uint64_t addr;
	int flags = BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW;
	unsigned int i, nrxd, ntxd;
	uint32_t val;

	rw_assert_wrlock(&sc->sc_cfg_lock);

	/* Release lock for memory allocation. */
	NET_UNLOCK();

	if (sc->sc_dma64)
		flags |= BUS_DMA_64BIT;

	ntxd = CAD_NTXDESC;
	nrxd = CAD_NRXDESC;

	/*
	 * Allocate a dummy descriptor for unused priority queues.
	 * This is necessary with GEM revisions that have no option
	 * to disable queues.
	 */
	if (sc->sc_qmask & ~1U) {
		ntxd++;
		nrxd++;
	}

	/*
	 * Set up Tx descriptor ring.
	 */

	sc->sc_txring = cad_dmamem_alloc(sc,
	    ntxd * sc->sc_descsize, sc->sc_descsize);
	sc->sc_txdesc = sc->sc_txring->cdm_kva;

	desc32 = (struct cad_desc32 *)sc->sc_txdesc;
	desc64 = (struct cad_desc64 *)sc->sc_txdesc;

	sc->sc_txbuf = malloc(sizeof(*sc->sc_txbuf) * CAD_NTXDESC,
	    M_DEVBUF, M_WAITOK);
	for (i = 0; i < CAD_NTXDESC; i++) {
		txb = &sc->sc_txbuf[i];
		bus_dmamap_create(sc->sc_dmat, MCLBYTES, CAD_NTXSEGS,
		    MCLBYTES, 0, flags, &txb->bf_map);
		txb->bf_m = NULL;

		if (sc->sc_dma64) {
			desc64[i].d_addrhi = 0;
			desc64[i].d_addrlo = 0;
			desc64[i].d_status = GEM_TXD_USED;
			if (i == CAD_NTXDESC - 1)
				desc64[i].d_status |= GEM_TXD_WRAP;
		} else {
			desc32[i].d_addr = 0;
			desc32[i].d_status = GEM_TXD_USED;
			if (i == CAD_NTXDESC - 1)
				desc32[i].d_status |= GEM_TXD_WRAP;
		}
	}

	/* The remaining descriptors are dummies. */
	for (; i < ntxd; i++) {
		if (sc->sc_dma64) {
			desc64[i].d_addrhi = 0;
			desc64[i].d_addrlo = 0;
			desc64[i].d_status = GEM_TXD_USED | GEM_TXD_WRAP;
		} else {
			desc32[i].d_addr = 0;
			desc32[i].d_status = GEM_TXD_USED | GEM_TXD_WRAP;
		}
	}

	sc->sc_tx_prod = 0;
	sc->sc_tx_cons = 0;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_txring->cdm_map,
	    0, sc->sc_txring->cdm_size,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	addr = sc->sc_txring->cdm_map->dm_segs[0].ds_addr;
	if (sc->sc_dma64)
		HWRITE4(sc, GEM_TXQBASEHI, addr >> 32);
	HWRITE4(sc, GEM_TXQBASE, addr);

	/* Initialize unused queues. Disable them if possible. */
	addr += CAD_NTXDESC * sc->sc_descsize;
	for (i = 1; i < GEM_MAX_PRIQ; i++) {
		if (sc->sc_qmask & (1U << i)) {
			HWRITE4(sc, GEM_TXQ1BASE(i - 1),
			    addr | GEM_TXQ1BASE_DISABLE);
		}
	}

	/*
	 * Set up Rx descriptor ring.
	 */

	sc->sc_rxring = cad_dmamem_alloc(sc,
	    nrxd * sc->sc_descsize, sc->sc_descsize);
	sc->sc_rxdesc = sc->sc_rxring->cdm_kva;

	desc32 = (struct cad_desc32 *)sc->sc_rxdesc;
	desc64 = (struct cad_desc64 *)sc->sc_rxdesc;

	sc->sc_rxbuf = malloc(sizeof(struct cad_buf) * CAD_NRXDESC,
	    M_DEVBUF, M_WAITOK);
	for (i = 0; i < CAD_NRXDESC; i++) {
		rxb = &sc->sc_rxbuf[i];
		bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, flags, &rxb->bf_map);
		rxb->bf_m = NULL;

		/* Mark all descriptors as used so that driver owns them. */
		if (sc->sc_dma64) {
			desc64[i].d_addrhi = 0;
			desc64[i].d_addrlo = GEM_RXD_ADDR_USED;
			if (i == CAD_NRXDESC - 1)
				desc64[i].d_addrlo |= GEM_RXD_ADDR_WRAP;
		} else {
			desc32[i].d_addr = GEM_RXD_ADDR_USED;
			if (i == CAD_NRXDESC - 1)
				desc32[i].d_addr |= GEM_RXD_ADDR_WRAP;
		}
	}

	/* The remaining descriptors are dummies. */
	for (; i < nrxd; i++) {
		if (sc->sc_dma64) {
			desc64[i].d_addrhi = 0;
			desc64[i].d_addrlo =
			    GEM_RXD_ADDR_USED | GEM_RXD_ADDR_WRAP;
		} else {
			desc32[i].d_addr =
			    GEM_RXD_ADDR_USED | GEM_RXD_ADDR_WRAP;
		}
	}

	if_rxr_init(&sc->sc_rx_ring, 2, CAD_NRXDESC);

	sc->sc_rx_prod = 0;
	sc->sc_rx_cons = 0;
	cad_rxfill(sc);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_rxring->cdm_map,
	    0, sc->sc_rxring->cdm_size,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	addr = sc->sc_rxring->cdm_map->dm_segs[0].ds_addr;
	if (sc->sc_dma64)
		HWRITE4(sc, GEM_RXQBASEHI, addr >> 32);
	HWRITE4(sc, GEM_RXQBASE, addr);

	/* Initialize unused queues. Disable them if possible. */
	addr += sc->sc_descsize * CAD_NRXDESC;
	for (i = 1; i < GEM_MAX_PRIQ; i++) {
		if (sc->sc_qmask & (1U << i)) {
			if (i < 8) {
				HWRITE4(sc, GEM_RXQ1BASE(i - 1),
				    addr | GEM_RXQ1BASE_DISABLE);
			} else {
				HWRITE4(sc, GEM_RXQ8BASE(i - 8),
				    addr | GEM_RXQ8BASE_DISABLE);
			}
		}
	}

	NET_LOCK();

	/*
	 * Set MAC address filters.
	 */

	HWRITE4(sc, GEM_LADDRL(0), sc->sc_ac.ac_enaddr[0] |
	    ((uint32_t)sc->sc_ac.ac_enaddr[1] << 8) |
	    ((uint32_t)sc->sc_ac.ac_enaddr[2] << 16) |
	    ((uint32_t)sc->sc_ac.ac_enaddr[3] << 24));
	HWRITE4(sc, GEM_LADDRH(0), sc->sc_ac.ac_enaddr[4] |
	    ((uint32_t)sc->sc_ac.ac_enaddr[5] << 8));

	for (i = 1; i < GEM_LADDRNUM; i++) {
		HWRITE4(sc, GEM_LADDRL(i), 0);
		HWRITE4(sc, GEM_LADDRH(i), 0);
	}

	cad_iff(sc);

	if (!sc->sc_hw_tx_freq)
		clock_set_frequency(sc->sc_node, GEM_CLK_TX, 2500000);
	clock_enable(sc->sc_node, GEM_CLK_TX);
	delay(1000);

	val = HREAD4(sc, GEM_NETCFG);

	val |= GEM_NETCFG_FCSREM | GEM_NETCFG_RXCSUMEN | GEM_NETCFG_1000 |
	    GEM_NETCFG_100 | GEM_NETCFG_FDEN | GEM_NETCFG_1536RXEN;
	val &= ~GEM_NETCFG_RXOFFS_MASK;
	val |= ETHER_ALIGN << GEM_NETCFG_RXOFFS_SHIFT;
	val &= ~GEM_NETCFG_BCASTDI;

	if (sc->sc_phy_mode == CAD_PHY_MODE_SGMII)
		val |= GEM_NETCFG_SGMIIEN | GEM_NETCFG_PCSSEL;
	else
		val &= ~(GEM_NETCFG_SGMIIEN | GEM_NETCFG_PCSSEL);

	HWRITE4(sc, GEM_NETCFG, val);

	val = HREAD4(sc, GEM_DMACR);

	if (sc->sc_dma64)
		val |= GEM_DMACR_DMA64;
	else
		val &= ~GEM_DMACR_DMA64;
	/* Use CPU's native byte order with descriptor words. */
#if BYTE_ORDER == BIG_ENDIAN
	val |= GEM_DMACR_ES_DESCR;
#else
	val &= ~GEM_DMACR_ES_DESCR;
#endif
	val &= ~GEM_DMACR_ES_PDATA;
	val |= GEM_DMACR_AHBDISC | GEM_DMACR_TXSIZE;
	val &= ~GEM_DMACR_RXSIZE_MASK;
	val |= GEM_DMACR_RXSIZE_8K;
	val &= ~GEM_DMACR_RXBUF_MASK;
	val |= (MCLBYTES / 64) << GEM_DMACR_RXBUF_SHIFT;
	val &= ~GEM_DMACR_BLEN_MASK;
	val |= GEM_DMACR_BLEN_16;

	if (ifp->if_capabilities & IFCAP_CSUM_IPv4)
		val |= GEM_DMACR_TXCSUMEN;

	HWRITE4(sc, GEM_DMACR, val);

	/* Clear statistics. */
	HWRITE4(sc, GEM_NETCTL, sc->sc_netctl | GEM_NETCTL_STATCLR);

	/* Enable Rx and Tx. */
	sc->sc_netctl |= GEM_NETCTL_RXEN | GEM_NETCTL_TXEN;
	HWRITE4(sc, GEM_NETCTL, sc->sc_netctl);

	/* Enable interrupts. */
	HWRITE4(sc, GEM_IER, GEM_IXR_HRESP | GEM_IXR_RXOVR | GEM_IXR_RXDONE |
	    GEM_IXR_TXDONE);

	if (sc->sc_rxhang_erratum)
		HWRITE4(sc, GEM_IER, GEM_IXR_RXUSED);

	if (!LIST_EMPTY(&sc->sc_mii.mii_phys))
		mii_mediachg(&sc->sc_mii);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	timeout_add_sec(&sc->sc_tick, 1);

	return 0;
}

void
cad_down(struct cad_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct cad_buf *rxb, *txb;
	unsigned int i, timeout;

	rw_assert_wrlock(&sc->sc_cfg_lock);

	ifp->if_flags &= ~IFF_RUNNING;

	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	/* Avoid lock order issues with barriers. */
	NET_UNLOCK();

	timeout_del_barrier(&sc->sc_tick);

	/* Disable data transfer. */
	sc->sc_netctl &= ~(GEM_NETCTL_TXEN | GEM_NETCTL_RXEN);
	HWRITE4(sc, GEM_NETCTL, sc->sc_netctl);

	/* Disable all interrupts. */
	HWRITE4(sc, GEM_IDR, ~0U);

	/* Wait for transmitter to become idle. */
	for (timeout = 1000; timeout > 0; timeout--) {
		if ((HREAD4(sc, GEM_TXSR) & GEM_TXSR_TXGO) == 0)
			break;
		delay(10);
	}
	if (timeout == 0)
		printf("%s: transmitter not idle\n", sc->sc_dev.dv_xname);

	mii_down(&sc->sc_mii);

	/* Wait for activity to cease. */
	intr_barrier(sc->sc_ih);
	ifq_barrier(&ifp->if_snd);
	taskq_del_barrier(systq, &sc->sc_statchg_task);

	/* Disable the packet clock as it is not needed any longer. */
	clock_disable(sc->sc_node, GEM_CLK_TX);

	cad_reset(sc);

	/*
	 * Tear down the Tx descriptor ring.
	 */

	for (i = 0; i < CAD_NTXDESC; i++) {
		txb = &sc->sc_txbuf[i];
		if (txb->bf_m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, txb->bf_map, 0,
			    txb->bf_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, txb->bf_map);
			m_freem(txb->bf_m);
		}
		bus_dmamap_destroy(sc->sc_dmat, txb->bf_map);
	}
	free(sc->sc_txbuf, M_DEVBUF, sizeof(*sc->sc_txbuf) * CAD_NTXDESC);
	sc->sc_txbuf = NULL;

	cad_dmamem_free(sc, sc->sc_txring);
	sc->sc_txring = NULL;
	sc->sc_txdesc = NULL;

	/*
	 * Tear down the Rx descriptor ring.
	 */

	for (i = 0; i < CAD_NRXDESC; i++) {
		rxb = &sc->sc_rxbuf[i];
		if (rxb->bf_m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, rxb->bf_map, 0,
			    rxb->bf_map->dm_mapsize, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmat, rxb->bf_map);
			m_freem(rxb->bf_m);
		}
		bus_dmamap_destroy(sc->sc_dmat, rxb->bf_map);
	}
	free(sc->sc_rxbuf, M_DEVBUF, sizeof(*sc->sc_txbuf) * CAD_NRXDESC);
	sc->sc_rxbuf = NULL;

	cad_dmamem_free(sc, sc->sc_rxring);
	sc->sc_rxring = NULL;
	sc->sc_rxdesc = NULL;

	NET_LOCK();
}

uint8_t
cad_hash_mac(const uint8_t *eaddr)
{
	uint64_t val = 0;
	int i;
	uint8_t hash = 0;

	for (i = ETHER_ADDR_LEN - 1; i >= 0; i--)
		val = (val << 8) | eaddr[i];

	for (i = 0; i < 8; i++) {
		hash ^= val;
		val >>= 6;
	}

	return hash & 0x3f;
}

void
cad_iff(struct cad_softc *sc)
{
	struct arpcom *ac = &sc->sc_ac;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint64_t hash;
	uint32_t netcfg;

	rw_assert_wrlock(&sc->sc_cfg_lock);

	netcfg = HREAD4(sc, GEM_NETCFG);
	netcfg &= ~GEM_NETCFG_UCASTHASHEN;

	ifp->if_flags &= ~IFF_ALLMULTI;

	if (ifp->if_flags & IFF_PROMISC) {
		netcfg |= GEM_NETCFG_COPYALL;
		netcfg &= ~GEM_NETCFG_MCASTHASHEN;
	} else {
		netcfg &= ~GEM_NETCFG_COPYALL;
		netcfg |= GEM_NETCFG_MCASTHASHEN;

		if (ac->ac_multirangecnt > 0)
			ifp->if_flags |= IFF_ALLMULTI;

		if (ifp->if_flags & IFF_ALLMULTI) {
			hash = ~0ULL;
		} else {
			hash = 0;
			ETHER_FIRST_MULTI(step, ac, enm);
			while (enm != NULL) {
				hash |= 1ULL << cad_hash_mac(enm->enm_addrlo);
				ETHER_NEXT_MULTI(step, enm);
			}
		}

		HWRITE4(sc, GEM_HASHL, hash);
		HWRITE4(sc, GEM_HASHH, hash >> 32);
	}

	HWRITE4(sc, GEM_NETCFG, netcfg);
}

void
cad_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct cad_softc *sc = ifp->if_softc;
	struct mbuf *m;
	unsigned int free, head, used;

	free = sc->sc_tx_cons;
	head = sc->sc_tx_prod;
	if (free <= head)
		free += CAD_NTXDESC;
	free -= head;

	for (;;) {
		if (free <= CAD_NTXSEGS) {
			ifq_set_oactive(ifq);
			break;
		}

		m = ifq_dequeue(ifq);
		if (m == NULL)
			break;

		used = cad_encap(sc, m);
		if (used == 0) {
			m_freem(m);
			continue;
		}

#if NBPFILTER > 0
		if (ifp->if_bpf != NULL)
			bpf_mtap_ether(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		ifp->if_timer = 5;

		KASSERT(free >= used);
		free -= used;
	}

	HWRITE4(sc, GEM_NETCTL, sc->sc_netctl | GEM_NETCTL_STARTTX);
}

void
cad_watchdog(struct ifnet *ifp)
{
	struct cad_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	if (sc->sc_tx_cons == sc->sc_tx_prod)
		return;

	/* XXX */
	HWRITE4(sc, GEM_NETCTL, sc->sc_netctl | GEM_NETCTL_STARTTX);
}

unsigned int
cad_encap(struct cad_softc *sc, struct mbuf *m)
{
	bus_dmamap_t map;
	struct cad_buf *txb;
	struct cad_desc32 *desc32 = (struct cad_desc32 *)sc->sc_txdesc;
	struct cad_desc64 *desc64 = (struct cad_desc64 *)sc->sc_txdesc;
	unsigned int head, idx, nsegs;
	uint32_t status;
	int i;

	head = sc->sc_tx_prod;

	txb = &sc->sc_txbuf[head];
	map = txb->bf_map;

	switch (bus_dmamap_load_mbuf(sc->sc_dmat, map, m, BUS_DMA_NOWAIT)) {
	case 0:
		break;
	case EFBIG:
		if (m_defrag(m, M_DONTWAIT) != 0)
			return 0;
		if (bus_dmamap_load_mbuf(sc->sc_dmat, map, m,
		    BUS_DMA_NOWAIT) != 0)
			return 0;
		break;
	default:
		return 0;
	}

	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	nsegs = map->dm_nsegs;
	KASSERT(nsegs > 0);

	txb->bf_m = m;

	/*
	 * Fill descriptors in reverse order so that all the descriptors
	 * are ready when the first descriptor's GEM_TXD_USED bit is cleared.
	 */
	for (i = nsegs - 1; i >= 0; i--) {
		idx = (head + i) % CAD_NTXDESC;

		status = map->dm_segs[i].ds_len & GEM_TXD_LEN_MASK;
		if (i == nsegs - 1)
			status |= GEM_TXD_LAST;
		if (idx == CAD_NTXDESC - 1)
			status |= GEM_TXD_WRAP;

		if (sc->sc_dma64) {
			uint64_t addr = map->dm_segs[i].ds_addr;

			desc64[idx].d_addrlo = addr;
			desc64[idx].d_addrhi = addr >> 32;
		} else {
			desc32[idx].d_addr = map->dm_segs[i].ds_addr;
		}

		/* Make d_addr visible before GEM_TXD_USED is cleared
		 * in d_status. */
		bus_dmamap_sync(sc->sc_dmat, sc->sc_txring->cdm_map,
		    idx * sc->sc_descsize, sc->sc_descsize,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		if (sc->sc_dma64)
			desc64[idx].d_status = status;
		else
			desc32[idx].d_status = status;

		bus_dmamap_sync(sc->sc_dmat, sc->sc_txring->cdm_map,
		    idx * sc->sc_descsize, sc->sc_descsize,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}

	sc->sc_tx_prod = (head + nsegs) % CAD_NTXDESC;

	return nsegs;
}

int
cad_intr(void *arg)
{
	struct cad_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint32_t isr;

	isr = HREAD4(sc, GEM_ISR);
	HWRITE4(sc, GEM_ISR, isr);

	if (isr & GEM_IXR_RXDONE)
		cad_rxeof(sc);
	if (isr & GEM_IXR_TXDONE)
		cad_txeof(sc);

	if (isr & GEM_IXR_RXOVR)
		ifp->if_ierrors++;

	if (sc->sc_rxhang_erratum && (isr & GEM_IXR_RXUSED)) {
		/*
		 * Try to flush a packet from the Rx SRAM to avoid triggering
		 * the Rx hang.
		 */
		HWRITE4(sc, GEM_NETCTL, sc->sc_netctl | GEM_NETCTL_DPRAM);
		cad_rxfill(sc);
	}

	/* If there has been a DMA error, stop the interface to limit damage. */
	if (isr & GEM_IXR_HRESP) {
		sc->sc_netctl &= ~(GEM_NETCTL_TXEN | GEM_NETCTL_RXEN);
		HWRITE4(sc, GEM_NETCTL, sc->sc_netctl);
		HWRITE4(sc, GEM_IDR, ~0U);

		printf("%s: hresp error, interface stopped\n",
		    sc->sc_dev.dv_xname);
	}

	return 1;
}

void
cad_rxeof(struct cad_softc *sc)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct mbuf *m;
	struct cad_buf *rxb;
	struct cad_desc32 *desc32 = (struct cad_desc32 *)sc->sc_rxdesc;
	struct cad_desc64 *desc64 = (struct cad_desc64 *)sc->sc_rxdesc;
	size_t len;
	unsigned int idx;
	uint32_t addr, status;

	idx = sc->sc_rx_cons;

	while (if_rxr_inuse(&sc->sc_rx_ring) > 0) {
		bus_dmamap_sync(sc->sc_dmat, sc->sc_rxring->cdm_map,
		    idx * sc->sc_descsize, sc->sc_descsize,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

		if (sc->sc_dma64)
			addr = desc64[idx].d_addrlo;
		else
			addr = desc32[idx].d_addr;
		if ((addr & GEM_RXD_ADDR_USED) == 0)
			break;

		/* Prevent premature read of d_status. */
		bus_dmamap_sync(sc->sc_dmat, sc->sc_rxring->cdm_map,
		    idx * sc->sc_descsize, sc->sc_descsize,
		    BUS_DMASYNC_POSTREAD);

		if (sc->sc_dma64)
			status = desc64[idx].d_status;
		else
			status = desc32[idx].d_status;
		len = status & GEM_RXD_LEN_MASK;

		rxb = &sc->sc_rxbuf[idx];

		bus_dmamap_sync(sc->sc_dmat, rxb->bf_map, ETHER_ALIGN, len,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, rxb->bf_map);

		m = rxb->bf_m;
		rxb->bf_m = NULL;
		KASSERT(m != NULL);

		if_rxr_put(&sc->sc_rx_ring, 1);
		idx = (idx + 1) % CAD_NRXDESC;

		if ((status & (GEM_RXD_SOF | GEM_RXD_EOF)) !=
		    (GEM_RXD_SOF | GEM_RXD_EOF)) {
			m_freem(m);
			ifp->if_ierrors++;
			continue;
		}

		m_adj(m, ETHER_ALIGN);
		m->m_len = m->m_pkthdr.len = len;

		m->m_pkthdr.csum_flags = 0;
		switch (status & GEM_RXD_CSUM_MASK) {
		case GEM_RXD_CSUM_IP_OK:
			m->m_pkthdr.csum_flags = M_IPV4_CSUM_IN_OK;
			break;
		case GEM_RXD_CSUM_TCP_OK:
		case GEM_RXD_CSUM_UDP_OK:
			m->m_pkthdr.csum_flags = M_IPV4_CSUM_IN_OK |
			    M_TCP_CSUM_IN_OK | M_UDP_CSUM_IN_OK;
			break;
		}

		ml_enqueue(&ml, m);

		sc->sc_rxdone = 1;
	}

	sc->sc_rx_cons = idx;

	cad_rxfill(sc);

	if (ifiq_input(&ifp->if_rcv, &ml))
		if_rxr_livelocked(&sc->sc_rx_ring);
}

void
cad_rxfill(struct cad_softc *sc)
{
	struct cad_buf *rxb;
	struct cad_desc32 *desc32 = (struct cad_desc32 *)sc->sc_rxdesc;
	struct cad_desc64 *desc64 = (struct cad_desc64 *)sc->sc_rxdesc;
	uint64_t addr;
	unsigned int idx;
	u_int slots;

	idx = sc->sc_rx_prod;

	for (slots = if_rxr_get(&sc->sc_rx_ring, CAD_NRXDESC);
	    slots > 0; slots--) {
		rxb = &sc->sc_rxbuf[idx];
		rxb->bf_m = cad_alloc_mbuf(sc, rxb->bf_map);
		if (rxb->bf_m == NULL)
			break;

		addr = rxb->bf_map->dm_segs[0].ds_addr;
		KASSERT((addr & (GEM_RXD_ADDR_WRAP | GEM_RXD_ADDR_USED)) == 0);
		if (idx == CAD_NRXDESC - 1)
			addr |= GEM_RXD_ADDR_WRAP;

		if (sc->sc_dma64) {
			desc64[idx].d_addrhi = addr >> 32;
			desc64[idx].d_status = 0;
		} else {
			desc32[idx].d_status = 0;
		}

		/* Make d_addrhi and d_status visible before clearing
		 * GEM_RXD_ADDR_USED in d_addr or d_addrlo. */
		bus_dmamap_sync(sc->sc_dmat, sc->sc_rxring->cdm_map,
		    idx * sc->sc_descsize, sc->sc_descsize,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		if (sc->sc_dma64)
			desc64[idx].d_addrlo = addr;
		else
			desc32[idx].d_addr = addr;

		bus_dmamap_sync(sc->sc_dmat, sc->sc_rxring->cdm_map,
		    idx * sc->sc_descsize, sc->sc_descsize,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		idx = (idx + 1) % CAD_NRXDESC;
	}
	if_rxr_put(&sc->sc_rx_ring, slots);

	sc->sc_rx_prod = idx;
}

void
cad_txeof(struct cad_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct cad_buf *txb;
	struct cad_desc32 *desc32 = (struct cad_desc32 *)sc->sc_txdesc;
	struct cad_desc64 *desc64 = (struct cad_desc64 *)sc->sc_txdesc;
	unsigned int free = 0;
	unsigned int idx, nsegs;
	uint32_t status;

	idx = sc->sc_tx_cons;

	while (idx != sc->sc_tx_prod) {
		bus_dmamap_sync(sc->sc_dmat, sc->sc_txring->cdm_map,
		    idx * sc->sc_descsize, sc->sc_descsize,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

		if (sc->sc_dma64)
			status = desc64[idx].d_status;
		else
			status = desc32[idx].d_status;
		if ((status & GEM_TXD_USED) == 0)
			break;

		if (status & (GEM_TXD_RLIMIT | GEM_TXD_CORRUPT |
		    GEM_TXD_LCOLL | GEM_TXD_CSUMERR_MASK))
			ifp->if_oerrors++;

		txb = &sc->sc_txbuf[idx];
		nsegs = txb->bf_map->dm_nsegs;
		KASSERT(nsegs > 0);

		bus_dmamap_sync(sc->sc_dmat, txb->bf_map, 0,
		    txb->bf_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, txb->bf_map);

		m_freem(txb->bf_m);
		txb->bf_m = NULL;

		for (;;) {
			idx = (idx + 1) % CAD_NTXDESC;

			nsegs--;
			if (nsegs == 0)
				break;

			/*
			 * The controller marks only the initial segment used.
			 * Mark the remaining segments used manually, so that
			 * the controller will not accidentally use them later.
			 *
			 * This could be done lazily on the Tx ring producer
			 * side by ensuring that the subsequent descriptor
			 * after the actual segments is marked used.
			 * However, this would make the ring trickier to debug.
			 */

			bus_dmamap_sync(sc->sc_dmat, sc->sc_txring->cdm_map,
			    idx * sc->sc_descsize, sc->sc_descsize,
			    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

			if (sc->sc_dma64)
				desc64[idx].d_status |= GEM_TXD_USED;
			else
				desc32[idx].d_status |= GEM_TXD_USED;

			bus_dmamap_sync(sc->sc_dmat, sc->sc_txring->cdm_map,
			    idx * sc->sc_descsize, sc->sc_descsize,
			    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		}

		free++;
	}

	if (free == 0)
		return;

	sc->sc_tx_cons = idx;

	if (ifq_is_oactive(&ifp->if_snd))
		ifq_restart(&ifp->if_snd);
}

void
cad_tick(void *arg)
{
	struct cad_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int s;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	s = splnet();

	mii_tick(&sc->sc_mii);

	/*
	 * If there has been no Rx for a moment, Rx DMA might be stuck.
	 * Try to recover by restarting the receiver.
	 */
	if (sc->sc_rxhang_erratum && !sc->sc_rxdone) {
		HWRITE4(sc, GEM_NETCTL, sc->sc_netctl & ~GEM_NETCTL_RXEN);
		(void)HREAD4(sc, GEM_NETCTL);
		HWRITE4(sc, GEM_NETCTL, sc->sc_netctl);
	}
	sc->sc_rxdone = 0;

	splx(s);

	timeout_add_sec(&sc->sc_tick, 1);
}

int
cad_media_change(struct ifnet *ifp)
{
	struct cad_softc *sc = ifp->if_softc;

	if (!LIST_EMPTY(&sc->sc_mii.mii_phys))
		mii_mediachg(&sc->sc_mii);

	return 0;
}

void
cad_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct cad_softc *sc = ifp->if_softc;

	if (!LIST_EMPTY(&sc->sc_mii.mii_phys)) {
		mii_pollstat(&sc->sc_mii);
		imr->ifm_active = sc->sc_mii.mii_media_active;
		imr->ifm_status = sc->sc_mii.mii_media_status;
	}
}

int
cad_mii_wait(struct cad_softc *sc)
{
	int timeout;

	for (timeout = 10000; timeout > 0; timeout--) {
		if (HREAD4(sc, GEM_NETSR) & GEM_NETSR_PHY_MGMT_IDLE)
			break;
		delay(10);
	}
	if (timeout == 0)
		return ETIMEDOUT;
	return 0;
}

void
cad_mii_oper(struct cad_softc *sc, int phy_no, int reg, uint32_t oper)
{
	oper |= (phy_no << GEM_PHYMNTNC_ADDR_SHIFT) & GEM_PHYMNTNC_ADDR_MASK;
	oper |= (reg << GEM_PHYMNTNC_REG_SHIFT) & GEM_PHYMNTNC_REG_MASK;
	oper |= GEM_PHYMNTNC_CLAUSE_22 | GEM_PHYMNTNC_MUST_10;

	if (cad_mii_wait(sc) != 0) {
		printf("%s: MII bus idle timeout\n", sc->sc_dev.dv_xname);
		return;
	}

	HWRITE4(sc, GEM_PHYMNTNC, oper);

	if (cad_mii_wait(sc) != 0) {
		printf("%s: MII bus operation timeout\n", sc->sc_dev.dv_xname);
		return;
	}
}

int
cad_mii_readreg(struct device *self, int phy_no, int reg)
{
	struct cad_softc *sc = (struct cad_softc *)self;
	int val;

	cad_mii_oper(sc, phy_no, reg, GEM_PHYMNTNC_OP_READ);

	val = HREAD4(sc, GEM_PHYMNTNC) & GEM_PHYMNTNC_DATA_MASK;

	/* The MAC does not handle 1000baseT in half duplex mode. */
	if (reg == MII_EXTSR)
		val &= ~EXTSR_1000THDX;

	return val;
}

void
cad_mii_writereg(struct device *self, int phy_no, int reg, int val)
{
	struct cad_softc *sc = (struct cad_softc *)self;

	cad_mii_oper(sc, phy_no, reg, GEM_PHYMNTNC_OP_WRITE |
	    (val & GEM_PHYMNTNC_DATA_MASK));
}

void
cad_mii_statchg(struct device *self)
{
	struct cad_softc *sc = (struct cad_softc *)self;
	uint32_t netcfg;

	netcfg = HREAD4(sc, GEM_NETCFG);
	if (sc->sc_mii.mii_media_active & IFM_FDX)
		netcfg |= GEM_NETCFG_FDEN;
	else
		netcfg &= ~GEM_NETCFG_FDEN;

	netcfg &= ~(GEM_NETCFG_100 | GEM_NETCFG_1000);
	switch (IFM_SUBTYPE(sc->sc_mii.mii_media_active)) {
	default:
		sc->sc_tx_freq = 2500000;
		break;
	case IFM_100_TX:
		netcfg |= GEM_NETCFG_100;
		sc->sc_tx_freq = 25000000;
		break;
	case IFM_1000_T:
		netcfg |= GEM_NETCFG_100 | GEM_NETCFG_1000;
		sc->sc_tx_freq = 125000000;
		break;
	}

	HWRITE4(sc, GEM_NETCFG, netcfg);

	/* Defer clock setting because it allocates memory with M_WAITOK. */
	if (!sc->sc_hw_tx_freq)
		task_add(systq, &sc->sc_statchg_task);
}

void
cad_statchg_task(void *arg)
{
	struct cad_softc *sc = arg;

	clock_set_frequency(sc->sc_node, GEM_CLK_TX, sc->sc_tx_freq);
}

struct cad_dmamem *
cad_dmamem_alloc(struct cad_softc *sc, bus_size_t size, bus_size_t align)
{
	struct cad_dmamem *cdm;
	bus_size_t boundary = 0;
	int flags = BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW;
	int nsegs;

	cdm = malloc(sizeof(*cdm), M_DEVBUF, M_WAITOK | M_ZERO);
	cdm->cdm_size = size;

	if (sc->sc_dma64) {
		/*
		 * The segment contains an actual ring and possibly
		 * a dummy ring for unused priority queues.
		 * The segment must not cross a 32-bit boundary so that
		 * the rings have the same base address bits 63:32.
		 */
		boundary = 1ULL << 32;
		flags |= BUS_DMA_64BIT;
	}

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, boundary,
	    flags, &cdm->cdm_map) != 0)
		goto cdmfree;
	if (bus_dmamem_alloc(sc->sc_dmat, size, align, boundary,
	    &cdm->cdm_seg, 1, &nsegs, BUS_DMA_WAITOK) != 0)
		goto destroy;
	if (bus_dmamem_map(sc->sc_dmat, &cdm->cdm_seg, nsegs, size,
	    &cdm->cdm_kva, BUS_DMA_WAITOK | BUS_DMA_COHERENT) != 0)
		goto free;
	if (bus_dmamap_load(sc->sc_dmat, cdm->cdm_map, cdm->cdm_kva, size,
	    NULL, BUS_DMA_WAITOK) != 0)
		goto unmap;
	memset(cdm->cdm_kva, 0, size);
	return cdm;

unmap:
	bus_dmamem_unmap(sc->sc_dmat, cdm->cdm_kva, size);
free:
	bus_dmamem_free(sc->sc_dmat, &cdm->cdm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, cdm->cdm_map);
cdmfree:
	free(cdm, M_DEVBUF, sizeof(*cdm));
	return NULL;
}

void
cad_dmamem_free(struct cad_softc *sc, struct cad_dmamem *cdm)
{
	bus_dmamem_unmap(sc->sc_dmat, cdm->cdm_kva, cdm->cdm_size);
	bus_dmamem_free(sc->sc_dmat, &cdm->cdm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, cdm->cdm_map);
	free(cdm, M_DEVBUF, sizeof(*cdm));
}

struct mbuf *
cad_alloc_mbuf(struct cad_softc *sc, bus_dmamap_t map)
{
	struct mbuf *m;

	m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
	if (m == NULL)
		return NULL;
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	if (bus_dmamap_load_mbuf(sc->sc_dmat, map, m, BUS_DMA_NOWAIT) != 0) {
		m_freem(m);
		return NULL;
	}

	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREREAD);

	return m;
}

#if NKSTAT > 0
enum cad_stat {
	cad_stat_tx_toto,
	cad_stat_tx_totp,
	cad_stat_tx_bcast,
	cad_stat_tx_mcast,
	cad_stat_tx_pause,
	cad_stat_tx_h64,
	cad_stat_tx_h65,
	cad_stat_tx_h128,
	cad_stat_tx_h256,
	cad_stat_tx_h512,
	cad_stat_tx_h1024,
	cad_stat_tx_underrun,
	cad_stat_tx_scoll,
	cad_stat_tx_mcoll,
	cad_stat_tx_ecoll,
	cad_stat_tx_lcoll,
	cad_stat_tx_defer,
	cad_stat_tx_sense,
	cad_stat_rx_toto,
	cad_stat_rx_totp,
	cad_stat_rx_bcast,
	cad_stat_rx_mcast,
	cad_stat_rx_pause,
	cad_stat_rx_h64,
	cad_stat_rx_h65,
	cad_stat_rx_h128,
	cad_stat_rx_h256,
	cad_stat_rx_h512,
	cad_stat_rx_h1024,
	cad_stat_rx_undersz,
	cad_stat_rx_oversz,
	cad_stat_rx_jabber,
	cad_stat_rx_fcs,
	cad_stat_rx_symberr,
	cad_stat_rx_align,
	cad_stat_rx_reserr,
	cad_stat_rx_overrun,
	cad_stat_rx_ipcsum,
	cad_stat_rx_tcpcsum,
	cad_stat_rx_udpcsum,
	cad_stat_count
};

struct cad_counter {
	const char		*c_name;
	enum kstat_kv_unit	c_unit;
	uint32_t		c_reg;
};

const struct cad_counter cad_counters[cad_stat_count] = {
	[cad_stat_tx_toto] =
	    { "tx total",	KSTAT_KV_U_BYTES, 0 },
	[cad_stat_tx_totp] =
	    { "tx total",	KSTAT_KV_U_PACKETS, GEM_TXCNT },
	[cad_stat_tx_bcast] =
	    { "tx bcast",	KSTAT_KV_U_PACKETS, GEM_TXBCCNT },
	[cad_stat_tx_mcast] =
	    { "tx mcast",	KSTAT_KV_U_PACKETS, GEM_TXMCCNT },
	[cad_stat_tx_pause] =
	    { "tx pause",	KSTAT_KV_U_PACKETS, GEM_TXPAUSECNT },
	[cad_stat_tx_h64] =
	    { "tx 64B",		KSTAT_KV_U_PACKETS, GEM_TX64CNT },
	[cad_stat_tx_h65] =
	    { "tx 65-127B",	KSTAT_KV_U_PACKETS, GEM_TX65CNT },
	[cad_stat_tx_h128] =
	    { "tx 128-255B",	KSTAT_KV_U_PACKETS, GEM_TX128CNT },
	[cad_stat_tx_h256] =
	    { "tx 256-511B",	KSTAT_KV_U_PACKETS, GEM_TX256CNT },
	[cad_stat_tx_h512] =
	    { "tx 512-1023B",	KSTAT_KV_U_PACKETS, GEM_TX512CNT },
	[cad_stat_tx_h1024] =
	    { "tx 1024-1518B",	KSTAT_KV_U_PACKETS, GEM_TX1024CNT },
	[cad_stat_tx_underrun] =
	    { "tx underrun",	KSTAT_KV_U_PACKETS, GEM_TXURUNCNT },
	[cad_stat_tx_scoll] =
	    { "tx scoll",	KSTAT_KV_U_PACKETS, GEM_SNGLCOLLCNT },
	[cad_stat_tx_mcoll] =
	    { "tx mcoll",	KSTAT_KV_U_PACKETS, GEM_MULTICOLLCNT },
	[cad_stat_tx_ecoll] =
	    { "tx excess coll",	KSTAT_KV_U_PACKETS, GEM_EXCESSCOLLCNT },
	[cad_stat_tx_lcoll] =
	    { "tx late coll",	KSTAT_KV_U_PACKETS, GEM_LATECOLLCNT },
	[cad_stat_tx_defer] =
	    { "tx defer",	KSTAT_KV_U_PACKETS, GEM_TXDEFERCNT },
	[cad_stat_tx_sense] =
	    { "tx csense",	KSTAT_KV_U_PACKETS, GEM_TXCSENSECNT },
	[cad_stat_rx_toto] =
	    { "rx total",	KSTAT_KV_U_BYTES, 0 },
	[cad_stat_rx_totp] =
	    { "rx total",	KSTAT_KV_U_PACKETS, GEM_RXCNT },
	[cad_stat_rx_bcast] =
	    { "rx bcast",	KSTAT_KV_U_PACKETS, GEM_RXBROADCNT },
	[cad_stat_rx_mcast] =
	    { "rx mcast",	KSTAT_KV_U_PACKETS, GEM_RXMULTICNT },
	[cad_stat_rx_pause] =
	    { "rx pause",	KSTAT_KV_U_PACKETS, GEM_RXPAUSECNT },
	[cad_stat_rx_h64] =
	    { "rx 64B",		KSTAT_KV_U_PACKETS, GEM_RX64CNT },
	[cad_stat_rx_h65] =
	    { "rx 65-127B",	KSTAT_KV_U_PACKETS, GEM_RX65CNT },
	[cad_stat_rx_h128] =
	    { "rx 128-255B",	KSTAT_KV_U_PACKETS, GEM_RX128CNT },
	[cad_stat_rx_h256] =
	    { "rx 256-511B",	KSTAT_KV_U_PACKETS, GEM_RX256CNT },
	[cad_stat_rx_h512] =
	    { "rx 512-1023B",	KSTAT_KV_U_PACKETS, GEM_RX512CNT },
	[cad_stat_rx_h1024] =
	    { "rx 1024-1518B",	KSTAT_KV_U_PACKETS, GEM_RX1024CNT },
	[cad_stat_rx_undersz] =
	    { "rx undersz",	KSTAT_KV_U_PACKETS, GEM_RXUNDRCNT },
	[cad_stat_rx_oversz] =
	    { "rx oversz",	KSTAT_KV_U_PACKETS, GEM_RXOVRCNT },
	[cad_stat_rx_jabber] =
	    { "rx jabber",	KSTAT_KV_U_PACKETS, GEM_RXJABCNT },
	[cad_stat_rx_fcs] =
	    { "rx fcs",		KSTAT_KV_U_PACKETS, GEM_RXFCSCNT },
	[cad_stat_rx_symberr] =
	    { "rx symberr",	KSTAT_KV_U_PACKETS, GEM_RXSYMBCNT },
	[cad_stat_rx_align] =
	    { "rx align",	KSTAT_KV_U_PACKETS, GEM_RXALIGNCNT },
	[cad_stat_rx_reserr] =
	    { "rx reserr",	KSTAT_KV_U_PACKETS, GEM_RXRESERRCNT },
	[cad_stat_rx_overrun] =
	    { "rx overrun",	KSTAT_KV_U_PACKETS, GEM_RXORCNT },
	[cad_stat_rx_ipcsum] =
	    { "rx ip csum",	KSTAT_KV_U_PACKETS, GEM_RXIPCCNT },
	[cad_stat_rx_tcpcsum] =
	    { "rx tcp csum",	KSTAT_KV_U_PACKETS, GEM_RXTCPCCNT },
	[cad_stat_rx_udpcsum] =
	    { "rx udp csum",	KSTAT_KV_U_PACKETS, GEM_RXUDPCCNT },
};

void
cad_kstat_attach(struct cad_softc *sc)
{
	const struct cad_counter *c;
	struct kstat *ks;
	struct kstat_kv *kvs;
	int i;

	mtx_init(&sc->sc_kstat_mtx, IPL_SOFTCLOCK);

	ks = kstat_create(sc->sc_dev.dv_xname, 0, "cad-stats", 0,
	    KSTAT_T_KV, 0);
	if (ks == NULL)
		return;

	kvs = mallocarray(nitems(cad_counters), sizeof(*kvs),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	for (i = 0; i < nitems(cad_counters); i++) {
		c = &cad_counters[i];
		kstat_kv_unit_init(&kvs[i], c->c_name, KSTAT_KV_T_COUNTER64,
		    c->c_unit);
	}

	kstat_set_mutex(ks, &sc->sc_kstat_mtx);
	ks->ks_softc = sc;
	ks->ks_data = kvs;
	ks->ks_datalen = nitems(cad_counters) * sizeof(*kvs);
	ks->ks_read = cad_kstat_read;

	sc->sc_kstat = ks;
	kstat_install(ks);
}

int
cad_kstat_read(struct kstat *ks)
{
	const struct cad_counter *c;
	struct kstat_kv *kvs = ks->ks_data;
	struct cad_softc *sc = ks->ks_softc;
	uint64_t v64;
	int i;

	v64 = HREAD4(sc, GEM_OCTTXL);
	v64 |= (uint64_t)HREAD4(sc, GEM_OCTTXH) << 32;
	kstat_kv_u64(&kvs[cad_stat_tx_toto]) += v64;

	v64 = HREAD4(sc, GEM_OCTRXL);
	v64 |= (uint64_t)HREAD4(sc, GEM_OCTRXH) << 32;
	kstat_kv_u64(&kvs[cad_stat_rx_toto]) += v64;

	for (i = 0; i < nitems(cad_counters); i++) {
		c = &cad_counters[i];
		if (c->c_reg == 0)
			continue;
		kstat_kv_u64(&kvs[i]) += HREAD4(sc, c->c_reg);
	}

	getnanouptime(&ks->ks_updated);

	return 0;
}

void
cad_kstat_tick(void *arg)
{
	struct cad_softc *sc = arg;

	if (mtx_enter_try(&sc->sc_kstat_mtx)) {
		cad_kstat_read(sc->sc_kstat);
		mtx_leave(&sc->sc_kstat_mtx);
	}
}
#endif /* NKSTAT > 0 */

#ifdef DDB
void
cad_dump(struct cad_softc *sc)
{
	struct cad_buf *rxb, *txb;
	struct cad_desc32 *desc32;
	struct cad_desc64 *desc64;
	int i;

	printf("isr 0x%x txsr 0x%x rxsr 0x%x\n", HREAD4(sc, GEM_ISR),
	    HREAD4(sc, GEM_TXSR), HREAD4(sc, GEM_RXSR));

	if (sc->sc_dma64) {
		printf("tx q 0x%08x%08x\n",
		    HREAD4(sc, GEM_TXQBASEHI),
		    HREAD4(sc, GEM_TXQBASE));
	} else {
		printf("tx q 0x%08x\n",
		    HREAD4(sc, GEM_TXQBASE));
	}
	desc32 = (struct cad_desc32 *)sc->sc_txdesc;
	desc64 = (struct cad_desc64 *)sc->sc_txdesc;
	if (sc->sc_txbuf != NULL) {
		for (i = 0; i < CAD_NTXDESC; i++) {
			txb = &sc->sc_txbuf[i];
			if (sc->sc_dma64) {
				printf(" %3i %p 0x%08x%08x 0x%08x %s%s "
				    "m %p\n", i,
				    &desc64[i],
				    desc64[i].d_addrhi, desc64[i].d_addrlo,
				    desc64[i].d_status,
				    sc->sc_tx_cons == i ? ">" : " ",
				    sc->sc_tx_prod == i ? "<" : " ",
				    txb->bf_m);
			} else {
				printf(" %3i %p 0x%08x 0x%08x %s%s m %p\n", i,
				    &desc32[i],
				    desc32[i].d_addr,
				    desc32[i].d_status,
				    sc->sc_tx_cons == i ? ">" : " ",
				    sc->sc_tx_prod == i ? "<" : " ",
				    txb->bf_m);
			}
		}
	}
	for (i = 1; i < GEM_MAX_PRIQ; i++) {
		if (sc->sc_qmask & (1U << i)) {
			printf("tx q%d 0x%08x\n", i,
			    HREAD4(sc, GEM_TXQ1BASE(i - 1)));
		}
	}

	if (sc->sc_dma64) {
		printf("rx q 0x%08x%08x\n",
		    HREAD4(sc, GEM_RXQBASEHI),
		    HREAD4(sc, GEM_RXQBASE));
	} else {
		printf("rx q 0x%08x\n",
		    HREAD4(sc, GEM_RXQBASE));
	}
	desc32 = (struct cad_desc32 *)sc->sc_rxdesc;
	desc64 = (struct cad_desc64 *)sc->sc_rxdesc;
	if (sc->sc_rxbuf != NULL) {
		for (i = 0; i < CAD_NRXDESC; i++) {
			rxb = &sc->sc_rxbuf[i];
			if (sc->sc_dma64) {
				printf(" %3i %p 0x%08x%08x 0x%08x %s%s "
				    "m %p\n", i,
				    &desc64[i],
				    desc64[i].d_addrhi, desc64[i].d_addrlo,
				    desc64[i].d_status,
				    sc->sc_rx_cons == i ? ">" : " ",
				    sc->sc_rx_prod == i ? "<" : " ",
				    rxb->bf_m);
			} else {
				printf(" %3i %p 0x%08x 0x%08x %s%s m %p\n", i,
				    &desc32[i],
				    desc32[i].d_addr,
				    desc32[i].d_status,
				    sc->sc_rx_cons == i ? ">" : " ",
				    sc->sc_rx_prod == i ? "<" : " ",
				    rxb->bf_m);
			}
		}
	}
	for (i = 1; i < GEM_MAX_PRIQ; i++) {
		if (sc->sc_qmask & (1U << i)) {
			printf("rx q%d 0x%08x\n", i,
			    HREAD4(sc, (i < 8) ? GEM_RXQ1BASE(i - 1)
			      : GEM_RXQ8BASE(i - 8)));
		}
	}
}
#endif
