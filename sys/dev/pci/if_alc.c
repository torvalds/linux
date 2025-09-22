/*	$OpenBSD: if_alc.c,v 1.59 2024/08/31 16:23:09 deraadt Exp $	*/
/*-
 * Copyright (c) 2009, Pyun YongHyeon <yongari@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Driver for Atheros AR813x/AR815x/AR816x/AR817x PCIe Ethernet. */

#include "bpfilter.h"
#include "vlan.h"

#include <sys/param.h>
#include <sys/endian.h>
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

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_alcreg.h>

int	alc_match(struct device *, void *, void *);
void	alc_attach(struct device *, struct device *, void *);
int	alc_detach(struct device *, int);
int	alc_activate(struct device *, int);

int	alc_init(struct ifnet *);
void	alc_start(struct ifnet *);
int	alc_ioctl(struct ifnet *, u_long, caddr_t);
void	alc_watchdog(struct ifnet *);
int	alc_mediachange(struct ifnet *);
void	alc_mediastatus(struct ifnet *, struct ifmediareq *);

void	alc_aspm(struct alc_softc *, int, uint64_t);
void	alc_aspm_813x(struct alc_softc *, uint64_t);
void	alc_aspm_816x(struct alc_softc *, int);
void	alc_disable_l0s_l1(struct alc_softc *);
int	alc_dma_alloc(struct alc_softc *);
void	alc_dma_free(struct alc_softc *);
int	alc_encap(struct alc_softc *, struct mbuf *);
void	alc_get_macaddr(struct alc_softc *);
void	alc_get_macaddr_813x(struct alc_softc *);
void	alc_get_macaddr_816x(struct alc_softc *);
void	alc_get_macaddr_par(struct alc_softc *);
void	alc_init_cmb(struct alc_softc *);
void	alc_init_rr_ring(struct alc_softc *);
int	alc_init_rx_ring(struct alc_softc *);
void	alc_init_smb(struct alc_softc *);
void	alc_init_tx_ring(struct alc_softc *);
int	alc_intr(void *);
void	alc_mac_config(struct alc_softc *);
int	alc_mii_readreg_813x(struct device *, int, int);
int	alc_mii_readreg_816x(struct device *, int, int);
void	alc_mii_writereg_813x(struct device *, int, int, int);
void	alc_mii_writereg_816x(struct device *, int, int, int);
void	alc_dsp_fixup(struct alc_softc *, int);
int	alc_miibus_readreg(struct device *, int, int);
void	alc_miibus_statchg(struct device *);
void	alc_miibus_writereg(struct device *, int, int, int);
int	alc_miidbg_readreg(struct alc_softc *, int);
void	alc_miidbg_writereg(struct alc_softc *, int, int);
int	alc_miiext_readreg(struct alc_softc *, int, int);
void	alc_miiext_writereg(struct alc_softc *, int, int, int);
void	alc_phy_reset_813x(struct alc_softc *);
void	alc_phy_reset_816x(struct alc_softc *);
int	alc_newbuf(struct alc_softc *, struct alc_rxdesc *);
void	alc_phy_down(struct alc_softc *);
void	alc_phy_reset(struct alc_softc *);
void	alc_reset(struct alc_softc *);
void	alc_rxeof(struct alc_softc *, struct rx_rdesc *);
int	alc_rxintr(struct alc_softc *);
void	alc_iff(struct alc_softc *);
void	alc_rxvlan(struct alc_softc *);
void	alc_start_queue(struct alc_softc *);
void	alc_stats_clear(struct alc_softc *);
void	alc_stats_update(struct alc_softc *);
void	alc_stop(struct alc_softc *);
void	alc_stop_mac(struct alc_softc *);
void	alc_stop_queue(struct alc_softc *);
void	alc_tick(void *);
void	alc_txeof(struct alc_softc *);
void	alc_init_pcie(struct alc_softc *, int);
void	alc_config_msi(struct alc_softc *);
int	alc_dma_alloc(struct alc_softc *);
void	alc_dma_free(struct alc_softc *);
int	alc_encap(struct alc_softc *, struct mbuf *);
void	alc_osc_reset(struct alc_softc *);

uint32_t alc_dma_burst[] = { 128, 256, 512, 1024, 2048, 4096, 0, 0 };

const struct pci_matchid alc_devices[] = {
	{ PCI_VENDOR_ATTANSIC, PCI_PRODUCT_ATTANSIC_L1C },
	{ PCI_VENDOR_ATTANSIC, PCI_PRODUCT_ATTANSIC_L2C },
	{ PCI_VENDOR_ATTANSIC, PCI_PRODUCT_ATTANSIC_L1D },
	{ PCI_VENDOR_ATTANSIC, PCI_PRODUCT_ATTANSIC_L1D_1 },
	{ PCI_VENDOR_ATTANSIC, PCI_PRODUCT_ATTANSIC_L2C_1 },
	{ PCI_VENDOR_ATTANSIC, PCI_PRODUCT_ATTANSIC_L2C_2 },
	{ PCI_VENDOR_ATTANSIC, PCI_PRODUCT_ATTANSIC_AR8161 },
	{ PCI_VENDOR_ATTANSIC, PCI_PRODUCT_ATTANSIC_AR8162 },
	{ PCI_VENDOR_ATTANSIC, PCI_PRODUCT_ATTANSIC_AR8171 },
	{ PCI_VENDOR_ATTANSIC, PCI_PRODUCT_ATTANSIC_AR8172 },
	{ PCI_VENDOR_ATTANSIC, PCI_PRODUCT_ATTANSIC_E2200 },
	{ PCI_VENDOR_ATTANSIC, PCI_PRODUCT_ATTANSIC_E2400 },
	{ PCI_VENDOR_ATTANSIC, PCI_PRODUCT_ATTANSIC_E2500 }
};

const struct cfattach alc_ca = {
	sizeof (struct alc_softc), alc_match, alc_attach, alc_detach,
	alc_activate
};

struct cfdriver alc_cd = {
	NULL, "alc", DV_IFNET
};

int alcdebug = 0;
#define	DPRINTF(x)	do { if (alcdebug) printf x; } while (0)

#define ALC_CSUM_FEATURES	(M_TCP_CSUM_OUT | M_UDP_CSUM_OUT)

int
alc_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct alc_softc *sc = (struct alc_softc *)dev;
	uint32_t v;

	if (phy != sc->alc_phyaddr)
		return (0);

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
		v = alc_mii_readreg_816x(dev, phy, reg);
	else
		v = alc_mii_readreg_813x(dev, phy, reg);

	return (v);
}

int
alc_mii_readreg_813x(struct device *dev, int phy, int reg)
{
	struct alc_softc *sc = (struct alc_softc *)dev;
	uint32_t v;
	int i;

	/*
	 * For AR8132 fast ethernet controller, do not report 1000baseT
	 * capability to mii(4). Even though AR8132 uses the same
	 * model/revision number of F1 gigabit PHY, the PHY has no
	 * ability to establish 1000baseT link.
	 */
	if ((sc->alc_flags & ALC_FLAG_FASTETHER) != 0 &&
	    reg == MII_EXTSR)
		return (0);

	CSR_WRITE_4(sc, ALC_MDIO, MDIO_OP_EXECUTE | MDIO_OP_READ |
	    MDIO_SUP_PREAMBLE | MDIO_CLK_25_4 | MDIO_REG_ADDR(reg));
	for (i = ALC_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		v = CSR_READ_4(sc, ALC_MDIO);
		if ((v & (MDIO_OP_EXECUTE | MDIO_OP_BUSY)) == 0)
			break;
	}

	if (i == 0) {
		printf("%s: phy read timeout: phy %d, reg %d\n",
		    sc->sc_dev.dv_xname, phy, reg);
		return (0);
	}

	return ((v & MDIO_DATA_MASK) >> MDIO_DATA_SHIFT);
}

int
alc_mii_readreg_816x(struct device *dev, int phy, int reg)
{
	struct alc_softc *sc = (struct alc_softc *)dev;
	uint32_t clk, v;
	int i;

	if ((sc->alc_flags & ALC_FLAG_LINK) != 0)
		clk = MDIO_CLK_25_128;
	else
		clk = MDIO_CLK_25_4;
	CSR_WRITE_4(sc, ALC_MDIO, MDIO_OP_EXECUTE | MDIO_OP_READ |
		MDIO_SUP_PREAMBLE | clk | MDIO_REG_ADDR(reg));
	for (i = ALC_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		v = CSR_READ_4(sc, ALC_MDIO);
		if ((v & MDIO_OP_BUSY) == 0)
			break;
	}

	if (i == 0) {
		printf("%s: phy read timeout: phy %d, reg %d\n",
		    sc->sc_dev.dv_xname, phy, reg);
		return (0);
	}

	return ((v & MDIO_DATA_MASK) >> MDIO_DATA_SHIFT);
}

void
alc_miibus_writereg(struct device *dev, int phy, int reg, int val)
{
	struct alc_softc *sc = (struct alc_softc *)dev;

	if (phy != sc->alc_phyaddr)
		return;

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
		alc_mii_writereg_816x(dev, phy, reg, val);
	else
		alc_mii_writereg_813x(dev, phy, reg, val);
}

void
alc_mii_writereg_813x(struct device *dev, int phy, int reg, int val)
{
	struct alc_softc *sc = (struct alc_softc *)dev;
	uint32_t v;
	int i;

	CSR_WRITE_4(sc, ALC_MDIO, MDIO_OP_EXECUTE | MDIO_OP_WRITE |
	    (val & MDIO_DATA_MASK) << MDIO_DATA_SHIFT |
	    MDIO_SUP_PREAMBLE | MDIO_CLK_25_4 | MDIO_REG_ADDR(reg));
	for (i = ALC_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		v = CSR_READ_4(sc, ALC_MDIO);
		if ((v & (MDIO_OP_EXECUTE | MDIO_OP_BUSY)) == 0)
			break;
	}

	if (i == 0)
		printf("%s: phy write timeout: phy %d, reg %d\n",
		    sc->sc_dev.dv_xname, phy, reg);
}

void
alc_mii_writereg_816x(struct device *dev, int phy, int reg, int val)
{
	struct alc_softc *sc = (struct alc_softc *)dev;
	uint32_t clk, v;
	int i;

	if ((sc->alc_flags & ALC_FLAG_LINK) != 0)
		clk = MDIO_CLK_25_128;
	else
		clk = MDIO_CLK_25_4;
	CSR_WRITE_4(sc, ALC_MDIO, MDIO_OP_EXECUTE | MDIO_OP_WRITE |
	    ((val & MDIO_DATA_MASK) << MDIO_DATA_SHIFT) | MDIO_REG_ADDR(reg) |
	    MDIO_SUP_PREAMBLE | clk);
	for (i = ALC_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		v = CSR_READ_4(sc, ALC_MDIO);
		if ((v & MDIO_OP_BUSY) == 0)
			break;
	}

	if (i == 0)
		printf("%s: phy write timeout: phy %d, reg %d\n",
		    sc->sc_dev.dv_xname, phy, reg);
}

void
alc_miibus_statchg(struct device *dev)
{
	struct alc_softc *sc = (struct alc_softc *)dev;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mii_data *mii = &sc->sc_miibus;
	uint32_t reg;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	sc->alc_flags &= ~ALC_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->alc_flags |= ALC_FLAG_LINK;
			break;
		case IFM_1000_T:
			if ((sc->alc_flags & ALC_FLAG_FASTETHER) == 0)
				sc->alc_flags |= ALC_FLAG_LINK;
			break;
		default:
			break;
		}
	}
	/* Stop Rx/Tx MACs. */
	alc_stop_mac(sc);

	/* Program MACs with resolved speed/duplex/flow-control. */
	if ((sc->alc_flags & ALC_FLAG_LINK) != 0) {
		alc_start_queue(sc);
		alc_mac_config(sc);
		/* Re-enable Tx/Rx MACs. */
		reg = CSR_READ_4(sc, ALC_MAC_CFG);
		reg |= MAC_CFG_TX_ENB | MAC_CFG_RX_ENB;
		CSR_WRITE_4(sc, ALC_MAC_CFG, reg);
	}
	alc_aspm(sc, 0, IFM_SUBTYPE(mii->mii_media_active));
	alc_dsp_fixup(sc, IFM_SUBTYPE(mii->mii_media_active));
}

int
alc_miidbg_readreg(struct alc_softc *sc, int reg)
{
	alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr, ALC_MII_DBG_ADDR,
	    reg);
	return (alc_miibus_readreg(&sc->sc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_DATA));
}


void
alc_miidbg_writereg(struct alc_softc *sc, int reg, int val)
{
	alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr, ALC_MII_DBG_ADDR,
	    reg);
	alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr, ALC_MII_DBG_DATA,
	    val);
}

int
alc_miiext_readreg(struct alc_softc *sc, int devaddr, int reg)
{
	uint32_t clk, v;
	int i;

	CSR_WRITE_4(sc, ALC_EXT_MDIO, EXT_MDIO_REG(reg) |
	    EXT_MDIO_DEVADDR(devaddr));
	if ((sc->alc_flags & ALC_FLAG_LINK) != 0)
		clk = MDIO_CLK_25_128;
	else
		clk = MDIO_CLK_25_4;
	CSR_WRITE_4(sc, ALC_MDIO, MDIO_OP_EXECUTE | MDIO_OP_READ |
	    MDIO_SUP_PREAMBLE | clk | MDIO_MODE_EXT);
	for (i = ALC_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		v = CSR_READ_4(sc, ALC_MDIO);
		if ((v & MDIO_OP_BUSY) == 0)
			break;
	}

	if (i == 0) {
		printf("%s: phy ext read timeout: phy %d, reg %d\n",
		    sc->sc_dev.dv_xname, devaddr, reg);
		return (0);
	}

	return ((v & MDIO_DATA_MASK) >> MDIO_DATA_SHIFT);
}

void
alc_miiext_writereg(struct alc_softc *sc, int devaddr, int reg, int val)
{
	uint32_t clk, v;
	int i;

	CSR_WRITE_4(sc, ALC_EXT_MDIO, EXT_MDIO_REG(reg) |
	    EXT_MDIO_DEVADDR(devaddr));
	if ((sc->alc_flags & ALC_FLAG_LINK) != 0)
		clk = MDIO_CLK_25_128;
	else
		clk = MDIO_CLK_25_4;
	CSR_WRITE_4(sc, ALC_MDIO, MDIO_OP_EXECUTE | MDIO_OP_WRITE |
	    ((val & MDIO_DATA_MASK) << MDIO_DATA_SHIFT) |
	    MDIO_SUP_PREAMBLE | clk | MDIO_MODE_EXT);
	for (i = ALC_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		v = CSR_READ_4(sc, ALC_MDIO);
		if ((v & MDIO_OP_BUSY) == 0)
			break;
	}

	if (i == 0)
		printf("%s: phy ext write timeout: phy %d, reg %d\n",
		    sc->sc_dev.dv_xname, devaddr, reg);
}

void
alc_dsp_fixup(struct alc_softc *sc, int media)
{
	uint16_t agc, len, val;

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
		return;
	if (AR816X_REV(sc->alc_rev) >= AR816X_REV_C0)
		return;

	/*
	 * Vendor PHY magic.
	 * 1000BT/AZ, wrong cable length
	 */
	if ((sc->alc_flags & ALC_FLAG_LINK) != 0) {
		len = alc_miiext_readreg(sc, MII_EXT_PCS, MII_EXT_CLDCTL6);
		len = (len >> EXT_CLDCTL6_CAB_LEN_SHIFT) &
			EXT_CLDCTL6_CAB_LEN_MASK;
		agc = alc_miidbg_readreg(sc, MII_DBG_AGC);
		agc = (agc >> DBG_AGC_2_VGA_SHIFT) & DBG_AGC_2_VGA_MASK;
		if ((media == IFM_1000_T && len > EXT_CLDCTL6_CAB_LEN_SHORT1G &&
			agc > DBG_AGC_LONG1G_LIMT) ||
			(media == IFM_100_TX && len > DBG_AGC_LONG100M_LIMT &&
			agc > DBG_AGC_LONG1G_LIMT)) {
				alc_miidbg_writereg(sc, MII_DBG_AZ_ANADECT,
				    DBG_AZ_ANADECT_LONG);
				val = alc_miiext_readreg(sc, MII_EXT_ANEG,
				    MII_EXT_ANEG_AFE);
				val |= ANEG_AFEE_10BT_100M_TH;
				alc_miiext_writereg(sc, MII_EXT_ANEG,
				    MII_EXT_ANEG_AFE, val);
		} else {
			alc_miidbg_writereg(sc, MII_DBG_AZ_ANADECT,
			    DBG_AZ_ANADECT_DEFAULT);
			val = alc_miiext_readreg(sc, MII_EXT_ANEG,
			    MII_EXT_ANEG_AFE);
			val &= ~ANEG_AFEE_10BT_100M_TH;
			alc_miiext_writereg(sc, MII_EXT_ANEG, MII_EXT_ANEG_AFE,
			    val);
		}
		if ((sc->alc_flags & ALC_FLAG_LINK_WAR) != 0 &&
		    AR816X_REV(sc->alc_rev) == AR816X_REV_B0) {
			if (media == IFM_1000_T) {
				/*
				 * Giga link threshold, raise the tolerance of
				 * noise 50%.
				 */
				val = alc_miidbg_readreg(sc, MII_DBG_MSE20DB);
				val &= ~DBG_MSE20DB_TH_MASK;
				val |= (DBG_MSE20DB_TH_HI <<
				    DBG_MSE20DB_TH_SHIFT);
				alc_miidbg_writereg(sc, MII_DBG_MSE20DB, val);
			} else if (media == IFM_100_TX)
				alc_miidbg_writereg(sc, MII_DBG_MSE16DB,
				    DBG_MSE16DB_UP);
		}
	} else {
		val = alc_miiext_readreg(sc, MII_EXT_ANEG, MII_EXT_ANEG_AFE);
		val &= ~ANEG_AFEE_10BT_100M_TH;
		alc_miiext_writereg(sc, MII_EXT_ANEG, MII_EXT_ANEG_AFE, val);
		if ((sc->alc_flags & ALC_FLAG_LINK_WAR) != 0 &&
		    AR816X_REV(sc->alc_rev) == AR816X_REV_B0) {
			alc_miidbg_writereg(sc, MII_DBG_MSE16DB,
			    DBG_MSE16DB_DOWN);
			val = alc_miidbg_readreg(sc, MII_DBG_MSE20DB);
			val &= ~DBG_MSE20DB_TH_MASK;
			val |= (DBG_MSE20DB_TH_DEFAULT << DBG_MSE20DB_TH_SHIFT);
			alc_miidbg_writereg(sc, MII_DBG_MSE20DB, val);
		}
	}
}

void
alc_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct alc_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_miibus;

	if ((ifp->if_flags & IFF_UP) == 0)
		return;

	mii_pollstat(mii);
	ifmr->ifm_status = mii->mii_media_status;
	ifmr->ifm_active = mii->mii_media_active;
}

int
alc_mediachange(struct ifnet *ifp)
{
	struct alc_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_miibus;
	int error;

	if (mii->mii_instance != 0) {
		struct mii_softc *miisc;

		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	error = mii_mediachg(mii);

	return (error);
}

int
alc_match(struct device *dev, void *match, void *aux)
{
	return pci_matchbyid((struct pci_attach_args *)aux, alc_devices,
	    nitems(alc_devices));
}

void
alc_get_macaddr(struct alc_softc *sc)
{
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
		alc_get_macaddr_816x(sc);
	else
		alc_get_macaddr_813x(sc);
}

void
alc_get_macaddr_813x(struct alc_softc *sc)
{
	uint32_t opt;
	uint16_t val;
	int eeprom, i;

	eeprom = 0;
	opt = CSR_READ_4(sc, ALC_OPT_CFG);
	if ((CSR_READ_4(sc, ALC_MASTER_CFG) & MASTER_OTP_SEL) != 0 &&
	    (CSR_READ_4(sc, ALC_TWSI_DEBUG) & TWSI_DEBUG_DEV_EXIST) != 0) {
		/*
		 * EEPROM found, let TWSI reload EEPROM configuration.
		 * This will set ethernet address of controller.
		 */
		eeprom++;
		switch (sc->sc_product) {
		case PCI_PRODUCT_ATTANSIC_L1C:
		case PCI_PRODUCT_ATTANSIC_L2C:
			if ((opt & OPT_CFG_CLK_ENB) == 0) {
				opt |= OPT_CFG_CLK_ENB;
				CSR_WRITE_4(sc, ALC_OPT_CFG, opt);
				CSR_READ_4(sc, ALC_OPT_CFG);
				DELAY(1000);
			}
			break;
		case PCI_PRODUCT_ATTANSIC_L1D:
		case PCI_PRODUCT_ATTANSIC_L1D_1:
		case PCI_PRODUCT_ATTANSIC_L2C_1:
		case PCI_PRODUCT_ATTANSIC_L2C_2:
			alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_ADDR, 0x00);
			val = alc_miibus_readreg(&sc->sc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_DATA);
			alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_DATA, val & 0xFF7F);
			alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_ADDR, 0x3B);
			val = alc_miibus_readreg(&sc->sc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_DATA);
			alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_DATA, val | 0x0008);
			DELAY(20);
			break;
		}

		CSR_WRITE_4(sc, ALC_LTSSM_ID_CFG,
		    CSR_READ_4(sc, ALC_LTSSM_ID_CFG) & ~LTSSM_ID_WRO_ENB);
		CSR_WRITE_4(sc, ALC_WOL_CFG, 0);
		CSR_READ_4(sc, ALC_WOL_CFG);

		CSR_WRITE_4(sc, ALC_TWSI_CFG, CSR_READ_4(sc, ALC_TWSI_CFG) |
		    TWSI_CFG_SW_LD_START);
		for (i = 100; i > 0; i--) {
			DELAY(1000);
			if ((CSR_READ_4(sc, ALC_TWSI_CFG) &
			    TWSI_CFG_SW_LD_START) == 0)
				break;
		}
		if (i == 0)
			printf("%s: reloading EEPROM timeout!\n", 
			    sc->sc_dev.dv_xname);
	} else {
		if (alcdebug)
			printf("%s: EEPROM not found!\n", sc->sc_dev.dv_xname);
	}
	if (eeprom != 0) {
		switch (sc->sc_product) {
		case PCI_PRODUCT_ATTANSIC_L1C:
		case PCI_PRODUCT_ATTANSIC_L2C:
			if ((opt & OPT_CFG_CLK_ENB) != 0) {
				opt &= ~OPT_CFG_CLK_ENB;
				CSR_WRITE_4(sc, ALC_OPT_CFG, opt);
				CSR_READ_4(sc, ALC_OPT_CFG);
				DELAY(1000);
			}
			break;
		case PCI_PRODUCT_ATTANSIC_L1D:
		case PCI_PRODUCT_ATTANSIC_L1D_1:
		case PCI_PRODUCT_ATTANSIC_L2C_1:
		case PCI_PRODUCT_ATTANSIC_L2C_2:
			alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_ADDR, 0x00);
			val = alc_miibus_readreg(&sc->sc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_DATA);
			alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_DATA, val | 0x0080);
			alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_ADDR, 0x3B);
			val = alc_miibus_readreg(&sc->sc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_DATA);
			alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_DATA, val & 0xFFF7);
			DELAY(20);
			break;
		}
	}

	alc_get_macaddr_par(sc);
}

void
alc_get_macaddr_816x(struct alc_softc *sc)
{
	uint32_t reg;
	int i, reloaded;

	reloaded = 0;
	/* Try to reload station address via TWSI. */
	for (i = 100; i > 0; i--) {
		reg = CSR_READ_4(sc, ALC_SLD);
		if ((reg & (SLD_PROGRESS | SLD_START)) == 0)
			break;
		DELAY(1000);
	}
	if (i != 0) {
		CSR_WRITE_4(sc, ALC_SLD, reg | SLD_START);
		for (i = 100; i > 0; i--) {
			DELAY(1000);
			reg = CSR_READ_4(sc, ALC_SLD);
			if ((reg & SLD_START) == 0)
				break;
		}
		if (i != 0)
			reloaded++;
		else if (alcdebug)
			printf("%s: reloading station address via TWSI timed"
			    "out!\n", sc->sc_dev.dv_xname);
	}

	/* Try to reload station address from EEPROM or FLASH. */
	if (reloaded == 0) {
		reg = CSR_READ_4(sc, ALC_EEPROM_LD);
		if ((reg & (EEPROM_LD_EEPROM_EXIST |
		    EEPROM_LD_FLASH_EXIST)) != 0) {
			for (i = 100; i > 0; i--) {
				reg = CSR_READ_4(sc, ALC_EEPROM_LD);
				if ((reg & (EEPROM_LD_PROGRESS |
				    EEPROM_LD_START)) == 0)
					break;
				DELAY(1000);
			}
			if (i != 0) {
				CSR_WRITE_4(sc, ALC_EEPROM_LD, reg |
				    EEPROM_LD_START);
				for (i = 100; i > 0; i--) {
					DELAY(1000);
					reg = CSR_READ_4(sc, ALC_EEPROM_LD);
					if ((reg & EEPROM_LD_START) == 0)
						break;
				}
			} else if (alcdebug)
				printf("%s: reloading EEPROM/FLASH timed out!\n",
				    sc->sc_dev.dv_xname);
		}
	}

	alc_get_macaddr_par(sc);
}

void
alc_get_macaddr_par(struct alc_softc *sc)
{
	uint32_t ea[2];

	ea[0] = CSR_READ_4(sc, ALC_PAR0);
	ea[1] = CSR_READ_4(sc, ALC_PAR1);
	sc->alc_eaddr[0] = (ea[1] >> 8) & 0xFF;
	sc->alc_eaddr[1] = (ea[1] >> 0) & 0xFF;
	sc->alc_eaddr[2] = (ea[0] >> 24) & 0xFF;
	sc->alc_eaddr[3] = (ea[0] >> 16) & 0xFF;
	sc->alc_eaddr[4] = (ea[0] >> 8) & 0xFF;
	sc->alc_eaddr[5] = (ea[0] >> 0) & 0xFF;
}

void
alc_disable_l0s_l1(struct alc_softc *sc)
{
	uint32_t pmcfg;

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0) {
		/* Another magic from vendor. */
		pmcfg = CSR_READ_4(sc, ALC_PM_CFG);
		pmcfg &= ~(PM_CFG_L1_ENTRY_TIMER_MASK | PM_CFG_CLK_SWH_L1 |
		    PM_CFG_ASPM_L0S_ENB | PM_CFG_ASPM_L1_ENB |
		    PM_CFG_MAC_ASPM_CHK | PM_CFG_SERDES_PD_EX_L1);
		pmcfg |= PM_CFG_SERDES_BUDS_RX_L1_ENB |
		    PM_CFG_SERDES_PLL_L1_ENB | PM_CFG_SERDES_L1_ENB;
		CSR_WRITE_4(sc, ALC_PM_CFG, pmcfg);
	}
}

void
alc_phy_reset(struct alc_softc *sc)
{
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
		alc_phy_reset_816x(sc);
	else
		alc_phy_reset_813x(sc);
}

void
alc_phy_reset_813x(struct alc_softc *sc)
{
	uint16_t data;

	/* Reset magic from Linux. */
	CSR_WRITE_2(sc, ALC_GPHY_CFG, GPHY_CFG_SEL_ANA_RESET);
	CSR_READ_2(sc, ALC_GPHY_CFG);
	DELAY(10 * 1000);

	CSR_WRITE_2(sc, ALC_GPHY_CFG, GPHY_CFG_EXT_RESET |
	    GPHY_CFG_SEL_ANA_RESET);
	CSR_READ_2(sc, ALC_GPHY_CFG);
	DELAY(10 * 1000);

	/* DSP fixup, Vendor magic. */
	if (sc->sc_product == PCI_PRODUCT_ATTANSIC_L2C_1) {
		alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_ADDR, 0x000A);
		data = alc_miibus_readreg(&sc->sc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_DATA);
		alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_DATA, data & 0xDFFF);
	}
	if (sc->sc_product == PCI_PRODUCT_ATTANSIC_L1D ||
	    sc->sc_product == PCI_PRODUCT_ATTANSIC_L1D_1 ||
	    sc->sc_product == PCI_PRODUCT_ATTANSIC_L2C_1 ||
	    sc->sc_product == PCI_PRODUCT_ATTANSIC_L2C_2) {
		alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_ADDR, 0x003B);
		data = alc_miibus_readreg(&sc->sc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_DATA);
		alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_DATA, data & 0xFFF7);
		DELAY(20 * 1000);
	}
	if (sc->sc_product == PCI_PRODUCT_ATTANSIC_L1D) {
		alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_ADDR, 0x0029);
		alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_DATA, 0x929D);
	}
	if (sc->sc_product == PCI_PRODUCT_ATTANSIC_L1C ||
	    sc->sc_product == PCI_PRODUCT_ATTANSIC_L2C ||
	    sc->sc_product == PCI_PRODUCT_ATTANSIC_L1D_1 ||
	    sc->sc_product == PCI_PRODUCT_ATTANSIC_L2C_2) {
		alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_ADDR, 0x0029);
		alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_DATA, 0xB6DD);
	}

	/* Load DSP codes, vendor magic. */
	data = ANA_LOOP_SEL_10BT | ANA_EN_MASK_TB | ANA_EN_10BT_IDLE |
	    ((1 << ANA_INTERVAL_SEL_TIMER_SHIFT) & ANA_INTERVAL_SEL_TIMER_MASK);
	alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_ADDR, MII_ANA_CFG18);
	alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_DATA, data);

	data = ((2 << ANA_SERDES_CDR_BW_SHIFT) & ANA_SERDES_CDR_BW_MASK) |
	    ANA_SERDES_EN_DEEM | ANA_SERDES_SEL_HSP | ANA_SERDES_EN_PLL |
	    ANA_SERDES_EN_LCKDT;
	alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_ADDR, MII_ANA_CFG5);
	alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_DATA, data);

	data = ((44 << ANA_LONG_CABLE_TH_100_SHIFT) &
	    ANA_LONG_CABLE_TH_100_MASK) |
	    ((33 << ANA_SHORT_CABLE_TH_100_SHIFT) &
	    ANA_SHORT_CABLE_TH_100_SHIFT) |
	    ANA_BP_BAD_LINK_ACCUM | ANA_BP_SMALL_BW;
	alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_ADDR, MII_ANA_CFG54);
	alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_DATA, data);

	data = ((11 << ANA_IECHO_ADJ_3_SHIFT) & ANA_IECHO_ADJ_3_MASK) |
	    ((11 << ANA_IECHO_ADJ_2_SHIFT) & ANA_IECHO_ADJ_2_MASK) |
	    ((8 << ANA_IECHO_ADJ_1_SHIFT) & ANA_IECHO_ADJ_1_MASK) |
	    ((8 << ANA_IECHO_ADJ_0_SHIFT) & ANA_IECHO_ADJ_0_MASK);
	alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_ADDR, MII_ANA_CFG4);
	alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_DATA, data);

	data = ((7 & ANA_MANUL_SWICH_ON_SHIFT) & ANA_MANUL_SWICH_ON_MASK) |
	    ANA_RESTART_CAL | ANA_MAN_ENABLE | ANA_SEL_HSP | ANA_EN_HB |
	    ANA_OEN_125M;
	alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_ADDR, MII_ANA_CFG0);
	alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_DATA, data);
	DELAY(1000);

	/* Disable hibernation. */
	alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr, ALC_MII_DBG_ADDR,
	    0x0029);
	data = alc_miibus_readreg(&sc->sc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_DATA);
	data &= ~0x8000;
	alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr, ALC_MII_DBG_DATA,
	    data);

	alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr, ALC_MII_DBG_ADDR,
	    0x000B);
	data = alc_miibus_readreg(&sc->sc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_DATA);
	data &= ~0x8000;
	alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr, ALC_MII_DBG_DATA,
	    data);
}

void
alc_phy_reset_816x(struct alc_softc *sc)
{
	uint32_t val;

	val = CSR_READ_4(sc, ALC_GPHY_CFG);
	val &= ~(GPHY_CFG_EXT_RESET | GPHY_CFG_LED_MODE |
	    GPHY_CFG_GATE_25M_ENB | GPHY_CFG_PHY_IDDQ | GPHY_CFG_PHY_PLL_ON |
	    GPHY_CFG_PWDOWN_HW | GPHY_CFG_100AB_ENB);
	val |= GPHY_CFG_SEL_ANA_RESET;
	/* Disable PHY hibernation. */
	val &= ~(GPHY_CFG_HIB_PULSE | GPHY_CFG_HIB_EN);
	CSR_WRITE_4(sc, ALC_GPHY_CFG, val);
	DELAY(10);
	CSR_WRITE_4(sc, ALC_GPHY_CFG, val | GPHY_CFG_EXT_RESET);
	DELAY(800);
	/* Vendor PHY magic. */
	/* Disable PHY hibernation. */
	alc_miidbg_writereg(sc, MII_DBG_LEGCYPS,
	    DBG_LEGCYPS_DEFAULT & ~DBG_LEGCYPS_ENB);
	alc_miidbg_writereg(sc, MII_DBG_HIBNEG, DBG_HIBNEG_DEFAULT &
	    ~(DBG_HIBNEG_PSHIB_EN | DBG_HIBNEG_HIB_PULSE));
	alc_miidbg_writereg(sc, MII_DBG_GREENCFG, DBG_GREENCFG_DEFAULT);
	/* XXX Disable EEE. */
	val = CSR_READ_4(sc, ALC_LPI_CTL);
	val &= ~LPI_CTL_ENB;
	CSR_WRITE_4(sc, ALC_LPI_CTL, val);
	alc_miiext_writereg(sc, MII_EXT_ANEG, MII_EXT_ANEG_LOCAL_EEEADV, 0);
	/* PHY power saving. */
	alc_miidbg_writereg(sc, MII_DBG_TST10BTCFG, DBG_TST10BTCFG_DEFAULT);
	alc_miidbg_writereg(sc, MII_DBG_SRDSYSMOD, DBG_SRDSYSMOD_DEFAULT);
	alc_miidbg_writereg(sc, MII_DBG_TST100BTCFG, DBG_TST100BTCFG_DEFAULT);
	alc_miidbg_writereg(sc, MII_DBG_ANACTL, DBG_ANACTL_DEFAULT);
	val = alc_miidbg_readreg(sc, MII_DBG_GREENCFG2);
	val &= ~DBG_GREENCFG2_GATE_DFSE_EN;
	alc_miidbg_writereg(sc, MII_DBG_GREENCFG2, val);
	/* RTL8139C, 120m issue. */
	alc_miiext_writereg(sc, MII_EXT_ANEG, MII_EXT_ANEG_NLP78,
	    ANEG_NLP78_120M_DEFAULT);
	alc_miiext_writereg(sc, MII_EXT_ANEG, MII_EXT_ANEG_S3DIG10,
	    ANEG_S3DIG10_DEFAULT);
	if ((sc->alc_flags & ALC_FLAG_LINK_WAR) != 0) {
		/* Turn off half amplitude. */
		val = alc_miiext_readreg(sc, MII_EXT_PCS, MII_EXT_CLDCTL3);
		val |= EXT_CLDCTL3_BP_CABLE1TH_DET_GT;
		alc_miiext_writereg(sc, MII_EXT_PCS, MII_EXT_CLDCTL3, val);
		/* Turn off Green feature. */
		val = alc_miidbg_readreg(sc, MII_DBG_GREENCFG2);
		val |= DBG_GREENCFG2_BP_GREEN;
		alc_miidbg_writereg(sc, MII_DBG_GREENCFG2, val);
		/* Turn off half bias. */
		val = alc_miiext_readreg(sc, MII_EXT_PCS, MII_EXT_CLDCTL5);
		val |= EXT_CLDCTL5_BP_VD_HLFBIAS;
		alc_miiext_writereg(sc, MII_EXT_PCS, MII_EXT_CLDCTL5, val);
	}
}

void
alc_phy_down(struct alc_softc *sc)
{
	uint32_t gphy;

	switch (sc->sc_product) {
	case PCI_PRODUCT_ATTANSIC_AR8161:
	case PCI_PRODUCT_ATTANSIC_E2200:
	case PCI_PRODUCT_ATTANSIC_E2400:
	case PCI_PRODUCT_ATTANSIC_E2500:
	case PCI_PRODUCT_ATTANSIC_AR8162:
	case PCI_PRODUCT_ATTANSIC_AR8171:
	case PCI_PRODUCT_ATTANSIC_AR8172:
		gphy = CSR_READ_4(sc, ALC_GPHY_CFG);
		gphy &= ~(GPHY_CFG_EXT_RESET | GPHY_CFG_LED_MODE |
		    GPHY_CFG_100AB_ENB | GPHY_CFG_PHY_PLL_ON);
		gphy |= GPHY_CFG_HIB_EN | GPHY_CFG_HIB_PULSE |
		    GPHY_CFG_SEL_ANA_RESET;
		gphy |= GPHY_CFG_PHY_IDDQ | GPHY_CFG_PWDOWN_HW;
		CSR_WRITE_4(sc, ALC_GPHY_CFG, gphy);
		break;
	case PCI_PRODUCT_ATTANSIC_L1D:
	case PCI_PRODUCT_ATTANSIC_L1D_1:
	case PCI_PRODUCT_ATTANSIC_L2C_1:
	case PCI_PRODUCT_ATTANSIC_L2C_2:
		/*
		 * GPHY power down caused more problems on AR8151 v2.0.
		 * When driver is reloaded after GPHY power down,
		 * accesses to PHY/MAC registers hung the system. Only
		 * cold boot recovered from it.  I'm not sure whether
		 * AR8151 v1.0 also requires this one though.  I don't
		 * have AR8151 v1.0 controller in hand.
		 * The only option left is to isolate the PHY and
		 * initiates power down the PHY which in turn saves
		 * more power when driver is unloaded.
		 */
		alc_miibus_writereg(&sc->sc_dev, sc->alc_phyaddr,
		    MII_BMCR, BMCR_ISO | BMCR_PDOWN);
		break;
	default:
		/* Force PHY down. */
		CSR_WRITE_2(sc, ALC_GPHY_CFG, GPHY_CFG_EXT_RESET |
		    GPHY_CFG_SEL_ANA_RESET | GPHY_CFG_PHY_IDDQ | 
		    GPHY_CFG_PWDOWN_HW);
		DELAY(1000);
		break;
	}
}

void
alc_aspm(struct alc_softc *sc, int init, uint64_t media)
{
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
		alc_aspm_816x(sc, init);
	else
		alc_aspm_813x(sc, media);
}

void
alc_aspm_813x(struct alc_softc *sc, uint64_t media)
{
	uint32_t pmcfg;
	uint16_t linkcfg;

	pmcfg = CSR_READ_4(sc, ALC_PM_CFG);
	if ((sc->alc_flags & (ALC_FLAG_APS | ALC_FLAG_PCIE)) ==
	    (ALC_FLAG_APS | ALC_FLAG_PCIE))
		linkcfg = CSR_READ_2(sc, sc->alc_expcap + PCI_PCIE_LCSR);
	else
		linkcfg = 0;
	pmcfg &= ~PM_CFG_SERDES_PD_EX_L1;
	pmcfg &= ~(PM_CFG_L1_ENTRY_TIMER_MASK | PM_CFG_LCKDET_TIMER_MASK);
	pmcfg |= PM_CFG_MAC_ASPM_CHK;
	pmcfg |= (PM_CFG_LCKDET_TIMER_DEFAULT << PM_CFG_LCKDET_TIMER_SHIFT);
	pmcfg &= ~(PM_CFG_ASPM_L1_ENB | PM_CFG_ASPM_L0S_ENB);

	if ((sc->alc_flags & ALC_FLAG_APS) != 0) {
		/* Disable extended sync except AR8152 B v1.0 */
		linkcfg &= ~0x80;
		if (sc->sc_product == PCI_PRODUCT_ATTANSIC_L2C_1 &&
		    sc->alc_rev == ATHEROS_AR8152_B_V10)
			linkcfg |= 0x80;
		CSR_WRITE_2(sc, sc->alc_expcap + PCI_PCIE_LCSR, linkcfg);
		pmcfg &= ~(PM_CFG_EN_BUFS_RX_L0S | PM_CFG_SA_DLY_ENB |
		    PM_CFG_HOTRST);
		pmcfg |= (PM_CFG_L1_ENTRY_TIMER_DEFAULT <<
		    PM_CFG_L1_ENTRY_TIMER_SHIFT);
		pmcfg &= ~PM_CFG_PM_REQ_TIMER_MASK;
		pmcfg |= (PM_CFG_PM_REQ_TIMER_DEFAULT <<
		    PM_CFG_PM_REQ_TIMER_SHIFT);
		pmcfg |= PM_CFG_SERDES_PD_EX_L1 | PM_CFG_PCIE_RECV;
	}

	if ((sc->alc_flags & ALC_FLAG_LINK) != 0) {
		if ((sc->alc_flags & ALC_FLAG_L0S) != 0)
			pmcfg |= PM_CFG_ASPM_L0S_ENB;
		if ((sc->alc_flags & ALC_FLAG_L1S) != 0)
			pmcfg |= PM_CFG_ASPM_L1_ENB;
		if ((sc->alc_flags & ALC_FLAG_APS) != 0) {
			if (sc->sc_product == PCI_PRODUCT_ATTANSIC_L2C_1)
				pmcfg &= ~PM_CFG_ASPM_L0S_ENB;
			pmcfg &= ~(PM_CFG_SERDES_L1_ENB |
			    PM_CFG_SERDES_PLL_L1_ENB |
			    PM_CFG_SERDES_BUDS_RX_L1_ENB);
			pmcfg |= PM_CFG_CLK_SWH_L1;
			if (media == IFM_100_TX || media == IFM_1000_T) {
				pmcfg &= ~PM_CFG_L1_ENTRY_TIMER_MASK;
				switch (sc->sc_product) {
				case PCI_PRODUCT_ATTANSIC_L2C_1:
					pmcfg |= (7 <<
					    PM_CFG_L1_ENTRY_TIMER_SHIFT);
					break;
				case PCI_PRODUCT_ATTANSIC_L1D_1:
				case PCI_PRODUCT_ATTANSIC_L2C_2:
					pmcfg |= (4 <<
					    PM_CFG_L1_ENTRY_TIMER_SHIFT);
					break;
				default:
					pmcfg |= (15 <<
					    PM_CFG_L1_ENTRY_TIMER_SHIFT);
					break;
				}
			}
		} else {
			pmcfg |= PM_CFG_SERDES_L1_ENB |
			    PM_CFG_SERDES_PLL_L1_ENB |
			    PM_CFG_SERDES_BUDS_RX_L1_ENB;
			pmcfg &= ~(PM_CFG_CLK_SWH_L1 |
			    PM_CFG_ASPM_L1_ENB | PM_CFG_ASPM_L0S_ENB);
		}
	} else {
		pmcfg &= ~(PM_CFG_SERDES_BUDS_RX_L1_ENB | PM_CFG_SERDES_L1_ENB |
		    PM_CFG_SERDES_PLL_L1_ENB);
		pmcfg |= PM_CFG_CLK_SWH_L1;
		if ((sc->alc_flags & ALC_FLAG_L1S) != 0)
			pmcfg |= PM_CFG_ASPM_L1_ENB;
	}
	CSR_WRITE_4(sc, ALC_PM_CFG, pmcfg);
}

void
alc_aspm_816x(struct alc_softc *sc, int init)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint32_t pmcfg;

	pmcfg = CSR_READ_4(sc, ALC_PM_CFG);
	pmcfg &= ~PM_CFG_L1_ENTRY_TIMER_816X_MASK;
	pmcfg |= PM_CFG_L1_ENTRY_TIMER_816X_DEFAULT;
	pmcfg &= ~PM_CFG_PM_REQ_TIMER_MASK;
	pmcfg |= PM_CFG_PM_REQ_TIMER_816X_DEFAULT;
	pmcfg &= ~PM_CFG_LCKDET_TIMER_MASK;
	pmcfg |= PM_CFG_LCKDET_TIMER_DEFAULT;
	pmcfg |= PM_CFG_SERDES_PD_EX_L1 | PM_CFG_CLK_SWH_L1 | PM_CFG_PCIE_RECV;
	pmcfg &= ~(PM_CFG_RX_L1_AFTER_L0S | PM_CFG_TX_L1_AFTER_L0S |
	    PM_CFG_ASPM_L1_ENB | PM_CFG_ASPM_L0S_ENB |
	    PM_CFG_SERDES_L1_ENB | PM_CFG_SERDES_PLL_L1_ENB |
	    PM_CFG_SERDES_BUDS_RX_L1_ENB | PM_CFG_SA_DLY_ENB |
	    PM_CFG_MAC_ASPM_CHK | PM_CFG_HOTRST);
	if (AR816X_REV(sc->alc_rev) <= AR816X_REV_A1 &&
	    (sc->alc_rev & 0x01) != 0)
		pmcfg |= PM_CFG_SERDES_L1_ENB | PM_CFG_SERDES_PLL_L1_ENB;
	if ((sc->alc_flags & ALC_FLAG_LINK) != 0) {
		/* Link up, enable both L0s, L1s. */
		pmcfg |= PM_CFG_ASPM_L0S_ENB | PM_CFG_ASPM_L1_ENB |
		    PM_CFG_MAC_ASPM_CHK;
	} else {
		if (init != 0)
			pmcfg |= PM_CFG_ASPM_L0S_ENB | PM_CFG_ASPM_L1_ENB |
			    PM_CFG_MAC_ASPM_CHK;
		else if ((ifp->if_flags & IFF_RUNNING) != 0)
			pmcfg |= PM_CFG_ASPM_L1_ENB | PM_CFG_MAC_ASPM_CHK;
	}
	CSR_WRITE_4(sc, ALC_PM_CFG, pmcfg);
}

void
alc_init_pcie(struct alc_softc *sc, int base)
{
	const char *aspm_state[] = { "L0s/L1", "L0s", "L1", "L0s/L1" };
	uint32_t cap, ctl, val;
	int state;

	/* Clear data link and flow-control protocol error. */
	val = CSR_READ_4(sc, ALC_PEX_UNC_ERR_SEV);
	val &= ~(PEX_UNC_ERR_SEV_DLP | PEX_UNC_ERR_SEV_FCP);
	CSR_WRITE_4(sc, ALC_PEX_UNC_ERR_SEV, val);

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0) {
		CSR_WRITE_4(sc, ALC_LTSSM_ID_CFG,
		    CSR_READ_4(sc, ALC_LTSSM_ID_CFG) & ~LTSSM_ID_WRO_ENB);
		CSR_WRITE_4(sc, ALC_PCIE_PHYMISC,
		    CSR_READ_4(sc, ALC_PCIE_PHYMISC) |
		    PCIE_PHYMISC_FORCE_RCV_DET);
		if (sc->sc_product == PCI_PRODUCT_ATTANSIC_L2C_1 &&
		    sc->alc_rev == ATHEROS_AR8152_B_V10) {
			val = CSR_READ_4(sc, ALC_PCIE_PHYMISC2);
			val &= ~(PCIE_PHYMISC2_SERDES_CDR_MASK |
			    PCIE_PHYMISC2_SERDES_TH_MASK);
			val |= 3 << PCIE_PHYMISC2_SERDES_CDR_SHIFT;
			val |= 3 << PCIE_PHYMISC2_SERDES_TH_SHIFT;
			CSR_WRITE_4(sc, ALC_PCIE_PHYMISC2, val);
		}
		/* Disable ASPM L0S and L1. */
		cap = pci_conf_read(sc->sc_pct, sc->sc_pcitag,
		    base + PCI_PCIE_LCAP) >> 16;
		if ((cap & 0x00000c00) != 0) {
			ctl = pci_conf_read(sc->sc_pct, sc->sc_pcitag,
			    base + PCI_PCIE_LCSR) >> 16;
			if ((ctl & 0x08) != 0)
				sc->alc_rcb = DMA_CFG_RCB_128;
			if (alcdebug)
				printf("%s: RCB %u bytes\n",
				    sc->sc_dev.dv_xname,
				    sc->alc_rcb == DMA_CFG_RCB_64 ? 64 : 128);
			state = ctl & 0x03;
			if (state & 0x01)
				sc->alc_flags |= ALC_FLAG_L0S;
			if (state & 0x02)
				sc->alc_flags |= ALC_FLAG_L1S;
			if (alcdebug)
				printf("%s: ASPM %s %s\n",
				    sc->sc_dev.dv_xname,
				    aspm_state[state],
				    state == 0 ? "disabled" : "enabled");
			alc_disable_l0s_l1(sc);
		}
	} else {
		val = CSR_READ_4(sc, ALC_PDLL_TRNS1);
		val &= ~PDLL_TRNS1_D3PLLOFF_ENB;
		CSR_WRITE_4(sc, ALC_PDLL_TRNS1, val);
		val = CSR_READ_4(sc, ALC_MASTER_CFG);
		if (AR816X_REV(sc->alc_rev) <= AR816X_REV_A1 &&
		    (sc->alc_rev & 0x01) != 0) {
			if ((val & MASTER_WAKEN_25M) == 0 ||
			    (val & MASTER_CLK_SEL_DIS) == 0) {
				val |= MASTER_WAKEN_25M | MASTER_CLK_SEL_DIS;
				CSR_WRITE_4(sc, ALC_MASTER_CFG, val);
			}
		} else {
			if ((val & MASTER_WAKEN_25M) == 0 ||
			    (val & MASTER_CLK_SEL_DIS) != 0) {
				val |= MASTER_WAKEN_25M;
				val &= ~MASTER_CLK_SEL_DIS;
				CSR_WRITE_4(sc, ALC_MASTER_CFG, val);
			}
		}
	}
}

void
alc_config_msi(struct alc_softc *sc)
{
	uint32_t ctl, mod;

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		/*
		 * It seems interrupt moderation is controlled by
		 * ALC_MSI_RETRANS_TIMER register if MSI/MSIX is active.
		 * Driver uses RX interrupt moderation parameter to
		 * program ALC_MSI_RETRANS_TIMER register.
		 */
		ctl = CSR_READ_4(sc, ALC_MSI_RETRANS_TIMER);
		ctl &= ~MSI_RETRANS_TIMER_MASK;
		ctl &= ~MSI_RETRANS_MASK_SEL_LINE;
		mod = ALC_USECS(sc->alc_int_rx_mod);
		if (mod == 0)
			mod = 1;
		ctl |= mod;
		if ((sc->alc_flags & ALC_FLAG_MSI) != 0)
			CSR_WRITE_4(sc, ALC_MSI_RETRANS_TIMER, ctl |
			    MSI_RETRANS_MASK_SEL_LINE);
		else
			CSR_WRITE_4(sc, ALC_MSI_RETRANS_TIMER, 0);
	}
}

void
alc_attach(struct device *parent, struct device *self, void *aux)
{
	struct alc_softc *sc = (struct alc_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr;
	struct ifnet *ifp;
	pcireg_t memtype;
	uint16_t burst;
	int base, error = 0;

	/* Set PHY address. */
	sc->alc_phyaddr = ALC_PHY_ADDR;

	/* Get PCI and chip id/revision. */
	sc->sc_product = PCI_PRODUCT(pa->pa_id);
	sc->alc_rev = PCI_REVISION(pa->pa_class);

	/*
	 * One odd thing is AR8132 uses the same PHY hardware(F1
	 * gigabit PHY) of AR8131. So atphy(4) of AR8132 reports
	 * the PHY supports 1000Mbps but that's not true. The PHY
	 * used in AR8132 can't establish gigabit link even if it
	 * shows the same PHY model/revision number of AR8131.
	 */
	switch (sc->sc_product) {
	case PCI_PRODUCT_ATTANSIC_E2200:
	case PCI_PRODUCT_ATTANSIC_E2400:
	case PCI_PRODUCT_ATTANSIC_E2500:
		sc->alc_flags |= ALC_FLAG_E2X00;
		/* FALLTHROUGH */
	case PCI_PRODUCT_ATTANSIC_AR8161:
		if (AR816X_REV(sc->alc_rev) == 0)
			sc->alc_flags |= ALC_FLAG_LINK_WAR;
		/* FALLTHROUGH */
	case PCI_PRODUCT_ATTANSIC_AR8171:
		sc->alc_flags |= ALC_FLAG_AR816X_FAMILY;
		break;
	case PCI_PRODUCT_ATTANSIC_AR8162:
	case PCI_PRODUCT_ATTANSIC_AR8172:
		sc->alc_flags |= ALC_FLAG_FASTETHER | ALC_FLAG_AR816X_FAMILY;
		break;
	case PCI_PRODUCT_ATTANSIC_L2C_1:
	case PCI_PRODUCT_ATTANSIC_L2C_2:
		sc->alc_flags |= ALC_FLAG_APS;
		/* FALLTHROUGH */
	case PCI_PRODUCT_ATTANSIC_L2C:
		sc->alc_flags |= ALC_FLAG_FASTETHER;
		break;
	case PCI_PRODUCT_ATTANSIC_L1D:
	case PCI_PRODUCT_ATTANSIC_L1D_1:
		sc->alc_flags |= ALC_FLAG_APS;
		/* FALLTHROUGH */
	default:
		break;
	}
	sc->alc_flags |= ALC_FLAG_JUMBO;

	/*
	 * Allocate IO memory
	 */
	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, ALC_PCIR_BAR);
	if (pci_mapreg_map(pa, ALC_PCIR_BAR, memtype, 0, &sc->sc_mem_bt,
	    &sc->sc_mem_bh, NULL, &sc->sc_mem_size, 0)) {
		printf(": can't map mem space\n");
		return;
	}

	sc->alc_flags |= ALC_FLAG_MSI;
	if (pci_intr_map_msi(pa, &ih) != 0) {
		if (pci_intr_map(pa, &ih) != 0) {
			printf(": can't map interrupt\n");
			goto fail;
		}
		sc->alc_flags &= ~ALC_FLAG_MSI;
	}

	/*
	 * Allocate IRQ
	 */
	intrstr = pci_intr_string(pc, ih);
	sc->sc_irq_handle = pci_intr_establish(pc, ih, IPL_NET, alc_intr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_irq_handle == NULL) {
		printf(": could not establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail;
	}
	printf(": %s", intrstr);

	alc_config_msi(sc);

	sc->sc_dmat = pa->pa_dmat;
	sc->sc_pct = pa->pa_pc;
	sc->sc_pcitag = pa->pa_tag;

	switch (sc->sc_product) {
	case PCI_PRODUCT_ATTANSIC_L1D:
	case PCI_PRODUCT_ATTANSIC_L1D_1:
	case PCI_PRODUCT_ATTANSIC_L2C_1:
	case PCI_PRODUCT_ATTANSIC_L2C_2:
		sc->alc_max_framelen = 6 * 1024;
		break;
	default:
		sc->alc_max_framelen = 9 * 1024;
		break;
	}

	/*
	 * It seems that AR813x/AR815x has silicon bug for SMB. In
	 * addition, Atheros said that enabling SMB wouldn't improve
	 * performance. However I think it's bad to access lots of
	 * registers to extract MAC statistics.
	 */
	sc->alc_flags |= ALC_FLAG_SMB_BUG;
	/*
	 * Don't use Tx CMB. It is known to have silicon bug.
	 */
	sc->alc_flags |= ALC_FLAG_CMB_BUG;
	sc->alc_chip_rev = CSR_READ_4(sc, ALC_MASTER_CFG) >>
	    MASTER_CHIP_REV_SHIFT;
	if (alcdebug) {
		printf("%s: PCI device revision : 0x%04x\n",
		    sc->sc_dev.dv_xname, sc->alc_rev);
		printf("%s: Chip id/revision : 0x%04x\n",
		    sc->sc_dev.dv_xname, sc->alc_chip_rev);
		printf("%s: %u Tx FIFO, %u Rx FIFO\n", sc->sc_dev.dv_xname,
		    CSR_READ_4(sc, ALC_SRAM_TX_FIFO_LEN) * 8,
		    CSR_READ_4(sc, ALC_SRAM_RX_FIFO_LEN) * 8);
	}

	/* Initialize DMA parameters. */
	sc->alc_dma_rd_burst = 0;
	sc->alc_dma_wr_burst = 0;
	sc->alc_rcb = DMA_CFG_RCB_64;
	if (pci_get_capability(pc, pa->pa_tag, PCI_CAP_PCIEXPRESS,
	    &base, NULL)) {
		sc->alc_flags |= ALC_FLAG_PCIE;
		sc->alc_expcap = base;
		burst = CSR_READ_2(sc, base + PCI_PCIE_DCSR);
		sc->alc_dma_rd_burst = (burst & 0x7000) >> 12;
		sc->alc_dma_wr_burst = (burst & 0x00e0) >> 5;
		if (alcdebug) {
			printf("%s: Read request size : %u bytes.\n",
			    sc->sc_dev.dv_xname, 
			    alc_dma_burst[sc->alc_dma_rd_burst]);
			printf("%s: TLP payload size : %u bytes.\n",
			    sc->sc_dev.dv_xname,
			    alc_dma_burst[sc->alc_dma_wr_burst]);
		}
		if (alc_dma_burst[sc->alc_dma_rd_burst] > 1024)
			sc->alc_dma_rd_burst = 3;
		if (alc_dma_burst[sc->alc_dma_wr_burst] > 1024)
			sc->alc_dma_wr_burst = 3;
		/*
		 * Force maximum payload size to 128 bytes for
		 * E2200/E2400/E2500/AR8162/AR8171/AR8172.
		 * Otherwise it triggers DMA write error.
		 */
		if ((sc->alc_flags &
		    (ALC_FLAG_E2X00 | ALC_FLAG_AR816X_FAMILY)) != 0)
			sc->alc_dma_wr_burst = 0;
		alc_init_pcie(sc, base);
	}

	/* Reset PHY. */
	alc_phy_reset(sc);

	/* Reset the ethernet controller. */
	alc_stop_mac(sc);
	alc_reset(sc);

	error = alc_dma_alloc(sc);
	if (error)
		goto fail;

	/* Load station address. */
	alc_get_macaddr(sc);

	ifp = &sc->sc_arpcom.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = alc_ioctl;
	ifp->if_start = alc_start;
	ifp->if_watchdog = alc_watchdog;
	ifq_init_maxlen(&ifp->if_snd, ALC_TX_RING_CNT - 1);
	bcopy(sc->alc_eaddr, sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

#ifdef ALC_CHECKSUM
	ifp->if_capabilities |= IFCAP_CSUM_IPv4 | IFCAP_CSUM_TCPv4 |
	    IFCAP_CSUM_UDPv4;
#endif

#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif

	printf(", address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));

	/* Set up MII bus. */
	sc->sc_miibus.mii_ifp = ifp;
	sc->sc_miibus.mii_readreg = alc_miibus_readreg;
	sc->sc_miibus.mii_writereg = alc_miibus_writereg;
	sc->sc_miibus.mii_statchg = alc_miibus_statchg;

	ifmedia_init(&sc->sc_miibus.mii_media, 0, alc_mediachange,
	    alc_mediastatus);
	mii_attach(self, &sc->sc_miibus, 0xffffffff, MII_PHY_ANY,
	    MII_OFFSET_ANY, MIIF_DOPAUSE);

	if (LIST_FIRST(&sc->sc_miibus.mii_phys) == NULL) {
		printf("%s: no PHY found!\n", sc->sc_dev.dv_xname);
		ifmedia_add(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_MANUAL,
		    0, NULL);
		ifmedia_set(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_MANUAL);
	} else 
		ifmedia_set(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->alc_tick_ch, alc_tick, sc);

	return;
fail:
	alc_dma_free(sc);
	if (sc->sc_irq_handle != NULL)
		pci_intr_disestablish(pc, sc->sc_irq_handle);
	if (sc->sc_mem_size)
		bus_space_unmap(sc->sc_mem_bt, sc->sc_mem_bh, sc->sc_mem_size);
}

int
alc_detach(struct device *self, int flags)
{
	struct alc_softc *sc = (struct alc_softc *)self;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int s;

	s = splnet();
	alc_stop(sc);
	splx(s);

	mii_detach(&sc->sc_miibus, MII_PHY_ANY, MII_OFFSET_ANY);

	/* Delete all remaining media. */
	ifmedia_delete_instance(&sc->sc_miibus.mii_media, IFM_INST_ANY);

	ether_ifdetach(ifp);
	if_detach(ifp);
	alc_dma_free(sc);

	alc_phy_down(sc);
	if (sc->sc_irq_handle != NULL) {
		pci_intr_disestablish(sc->sc_pct, sc->sc_irq_handle);
		sc->sc_irq_handle = NULL;
	}

	return (0);
}

int
alc_activate(struct device *self, int act)
{
	struct alc_softc *sc = (struct alc_softc *)self;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			alc_stop(sc);
		break;
	case DVACT_RESUME:
		if (ifp->if_flags & IFF_UP)
			alc_init(ifp);
		break;
	}
	return (0);
}

int
alc_dma_alloc(struct alc_softc *sc)
{
	struct alc_txdesc *txd;
	struct alc_rxdesc *rxd;
	int nsegs, error, i;

	/*
	 * Create DMA stuffs for TX ring
	 */
	error = bus_dmamap_create(sc->sc_dmat, ALC_TX_RING_SZ, 1,
	    ALC_TX_RING_SZ, 0, BUS_DMA_NOWAIT, &sc->alc_cdata.alc_tx_ring_map);
	if (error)
		return (ENOBUFS);

	/* Allocate DMA'able memory for TX ring */
	error = bus_dmamem_alloc(sc->sc_dmat, ALC_TX_RING_SZ,
	    ETHER_ALIGN, 0, &sc->alc_rdata.alc_tx_ring_seg, 1,
	    &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error) {
		printf("%s: could not allocate DMA'able memory for Tx ring.\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->alc_rdata.alc_tx_ring_seg,
	    nsegs, ALC_TX_RING_SZ, (caddr_t *)&sc->alc_rdata.alc_tx_ring,
	    BUS_DMA_NOWAIT);
	if (error)
		return (ENOBUFS);

	/* Load the DMA map for Tx ring. */
	error = bus_dmamap_load(sc->sc_dmat, sc->alc_cdata.alc_tx_ring_map,
	    sc->alc_rdata.alc_tx_ring, ALC_TX_RING_SZ, NULL, BUS_DMA_WAITOK);
	if (error) {
		printf("%s: could not load DMA'able memory for Tx ring.\n",
		    sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat, 
		    (bus_dma_segment_t *)&sc->alc_rdata.alc_tx_ring, 1);
		return (error);
	}

	sc->alc_rdata.alc_tx_ring_paddr = 
	    sc->alc_cdata.alc_tx_ring_map->dm_segs[0].ds_addr;

	/*
	 * Create DMA stuffs for RX ring
	 */
	error = bus_dmamap_create(sc->sc_dmat, ALC_RX_RING_SZ, 1,
	    ALC_RX_RING_SZ, 0, BUS_DMA_NOWAIT, &sc->alc_cdata.alc_rx_ring_map);
	if (error)
		return (ENOBUFS);
	
	/* Allocate DMA'able memory for RX ring */
	error = bus_dmamem_alloc(sc->sc_dmat, ALC_RX_RING_SZ,
	    ETHER_ALIGN, 0, &sc->alc_rdata.alc_rx_ring_seg, 1,
	    &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error) {
		printf("%s: could not allocate DMA'able memory for Rx ring.\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->alc_rdata.alc_rx_ring_seg,
	    nsegs, ALC_RX_RING_SZ, (caddr_t *)&sc->alc_rdata.alc_rx_ring,
	    BUS_DMA_NOWAIT);
	if (error)
		return (ENOBUFS);

	/* Load the DMA map for Rx ring. */
	error = bus_dmamap_load(sc->sc_dmat, sc->alc_cdata.alc_rx_ring_map,
	    sc->alc_rdata.alc_rx_ring, ALC_RX_RING_SZ, NULL, BUS_DMA_WAITOK);
	if (error) {
		printf("%s: could not load DMA'able memory for Rx ring.\n",
		    sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)sc->alc_rdata.alc_rx_ring, 1);
		return (error);
	}

	sc->alc_rdata.alc_rx_ring_paddr =
	    sc->alc_cdata.alc_rx_ring_map->dm_segs[0].ds_addr;

	/*
	 * Create DMA stuffs for RX return ring
	 */
	error = bus_dmamap_create(sc->sc_dmat, ALC_RR_RING_SZ, 1, 
	    ALC_RR_RING_SZ, 0, BUS_DMA_NOWAIT, &sc->alc_cdata.alc_rr_ring_map);
	if (error)
		return (ENOBUFS);

	/* Allocate DMA'able memory for RX return ring */
	error = bus_dmamem_alloc(sc->sc_dmat, ALC_RR_RING_SZ, 
	    ETHER_ALIGN, 0, &sc->alc_rdata.alc_rr_ring_seg, 1, 
	    &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error) {
		printf("%s: could not allocate DMA'able memory for Rx "
		    "return ring.\n", sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->alc_rdata.alc_rr_ring_seg,
	    nsegs, ALC_RR_RING_SZ, (caddr_t *)&sc->alc_rdata.alc_rr_ring,
	    BUS_DMA_NOWAIT);
	if (error)
		return (ENOBUFS);

	/*  Load the DMA map for Rx return ring. */
	error = bus_dmamap_load(sc->sc_dmat, sc->alc_cdata.alc_rr_ring_map,
	    sc->alc_rdata.alc_rr_ring, ALC_RR_RING_SZ, NULL, BUS_DMA_WAITOK);
	if (error) {
		printf("%s: could not load DMA'able memory for Rx return ring."
		    "\n", sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)&sc->alc_rdata.alc_rr_ring, 1);
		return (error);
	}

	sc->alc_rdata.alc_rr_ring_paddr = 
	    sc->alc_cdata.alc_rr_ring_map->dm_segs[0].ds_addr;

	/*
	 * Create DMA stuffs for CMB block 
	 */
	error = bus_dmamap_create(sc->sc_dmat, ALC_CMB_SZ, 1, 
	    ALC_CMB_SZ, 0, BUS_DMA_NOWAIT, 
	    &sc->alc_cdata.alc_cmb_map);
	if (error) 
		return (ENOBUFS);

	/* Allocate DMA'able memory for CMB block */
	error = bus_dmamem_alloc(sc->sc_dmat, ALC_CMB_SZ, 
	    ETHER_ALIGN, 0, &sc->alc_rdata.alc_cmb_seg, 1, 
	    &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error) {
		printf("%s: could not allocate DMA'able memory for "
		    "CMB block\n", sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->alc_rdata.alc_cmb_seg,
	    nsegs, ALC_CMB_SZ, (caddr_t *)&sc->alc_rdata.alc_cmb,
	    BUS_DMA_NOWAIT);
	if (error)
		return (ENOBUFS);

	/*  Load the DMA map for CMB block. */
	error = bus_dmamap_load(sc->sc_dmat, sc->alc_cdata.alc_cmb_map,
	    sc->alc_rdata.alc_cmb, ALC_CMB_SZ, NULL, 
	    BUS_DMA_WAITOK);
	if (error) {
		printf("%s: could not load DMA'able memory for CMB block\n",
		    sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)&sc->alc_rdata.alc_cmb, 1);
		return (error);
	}

	sc->alc_rdata.alc_cmb_paddr = 
	    sc->alc_cdata.alc_cmb_map->dm_segs[0].ds_addr;

	/*
	 * Create DMA stuffs for SMB block
	 */
	error = bus_dmamap_create(sc->sc_dmat, ALC_SMB_SZ, 1, 
	    ALC_SMB_SZ, 0, BUS_DMA_NOWAIT, 
	    &sc->alc_cdata.alc_smb_map);
	if (error)
		return (ENOBUFS);

	/* Allocate DMA'able memory for SMB block */
	error = bus_dmamem_alloc(sc->sc_dmat, ALC_SMB_SZ, 
	    ETHER_ALIGN, 0, &sc->alc_rdata.alc_smb_seg, 1, 
	    &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error) {
		printf("%s: could not allocate DMA'able memory for "
		    "SMB block\n", sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->alc_rdata.alc_smb_seg,
	    nsegs, ALC_SMB_SZ, (caddr_t *)&sc->alc_rdata.alc_smb,
	    BUS_DMA_NOWAIT);
	if (error)
		return (ENOBUFS);

	/*  Load the DMA map for SMB block */
	error = bus_dmamap_load(sc->sc_dmat, sc->alc_cdata.alc_smb_map,
	    sc->alc_rdata.alc_smb, ALC_SMB_SZ, NULL, 
	    BUS_DMA_WAITOK);
	if (error) {
		printf("%s: could not load DMA'able memory for SMB block\n",
		    sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)&sc->alc_rdata.alc_smb, 1);
		return (error);
	}

	sc->alc_rdata.alc_smb_paddr = 
	    sc->alc_cdata.alc_smb_map->dm_segs[0].ds_addr;


	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < ALC_TX_RING_CNT; i++) {
		txd = &sc->alc_cdata.alc_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_dmamap = NULL;
		error = bus_dmamap_create(sc->sc_dmat, ALC_TSO_MAXSIZE,
		    ALC_MAXTXSEGS, ALC_TSO_MAXSEGSIZE, 0, BUS_DMA_NOWAIT,
		    &txd->tx_dmamap);
		if (error) {
			printf("%s: could not create Tx dmamap.\n",
			    sc->sc_dev.dv_xname);
			return (error);
		}
	}

	/* Create DMA maps for Rx buffers. */
	error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES, 0,
	    BUS_DMA_NOWAIT, &sc->alc_cdata.alc_rx_sparemap);
	if (error) {
		printf("%s: could not create spare Rx dmamap.\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	for (i = 0; i < ALC_RX_RING_CNT; i++) {
		rxd = &sc->alc_cdata.alc_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_dmamap = NULL;
		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, BUS_DMA_NOWAIT, &rxd->rx_dmamap);
		if (error) {
			printf("%s: could not create Rx dmamap.\n",
			    sc->sc_dev.dv_xname);
			return (error);
		}
	}

	return (0);
}

void
alc_dma_free(struct alc_softc *sc)
{
	struct alc_txdesc *txd;
	struct alc_rxdesc *rxd;
	int i;

	/* Tx buffers */
	for (i = 0; i < ALC_TX_RING_CNT; i++) {
		txd = &sc->alc_cdata.alc_txdesc[i];
		if (txd->tx_dmamap != NULL) {
			bus_dmamap_destroy(sc->sc_dmat, txd->tx_dmamap);
			txd->tx_dmamap = NULL;
		}
	}
	/* Rx buffers */
	for (i = 0; i < ALC_RX_RING_CNT; i++) {
		rxd = &sc->alc_cdata.alc_rxdesc[i];
		if (rxd->rx_dmamap != NULL) {
			bus_dmamap_destroy(sc->sc_dmat, rxd->rx_dmamap);
			rxd->rx_dmamap = NULL;
		}
	}
	if (sc->alc_cdata.alc_rx_sparemap != NULL) {
		bus_dmamap_destroy(sc->sc_dmat, sc->alc_cdata.alc_rx_sparemap);
		sc->alc_cdata.alc_rx_sparemap = NULL;
	}

	/* Tx ring. */
	if (sc->alc_cdata.alc_tx_ring_map != NULL)
		bus_dmamap_unload(sc->sc_dmat, sc->alc_cdata.alc_tx_ring_map);
	if (sc->alc_cdata.alc_tx_ring_map != NULL &&
	    sc->alc_rdata.alc_tx_ring != NULL)
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)sc->alc_rdata.alc_tx_ring, 1);
	sc->alc_rdata.alc_tx_ring = NULL;
	sc->alc_cdata.alc_tx_ring_map = NULL;

	/* Rx ring. */
	if (sc->alc_cdata.alc_rx_ring_map != NULL) 
		bus_dmamap_unload(sc->sc_dmat, sc->alc_cdata.alc_rx_ring_map);
	if (sc->alc_cdata.alc_rx_ring_map != NULL &&
	    sc->alc_rdata.alc_rx_ring != NULL)
		bus_dmamem_free(sc->sc_dmat, 
		    (bus_dma_segment_t *)sc->alc_rdata.alc_rx_ring, 1);
	sc->alc_rdata.alc_rx_ring = NULL;
	sc->alc_cdata.alc_rx_ring_map = NULL;

	/* Rx return ring. */
	if (sc->alc_cdata.alc_rr_ring_map != NULL)
		bus_dmamap_unload(sc->sc_dmat, sc->alc_cdata.alc_rr_ring_map);
	if (sc->alc_cdata.alc_rr_ring_map != NULL &&
	    sc->alc_rdata.alc_rr_ring != NULL)
		bus_dmamem_free(sc->sc_dmat, 
		    (bus_dma_segment_t *)sc->alc_rdata.alc_rr_ring, 1);
	sc->alc_rdata.alc_rr_ring = NULL;
	sc->alc_cdata.alc_rr_ring_map = NULL;

	/* CMB block */
	if (sc->alc_cdata.alc_cmb_map != NULL)
		bus_dmamap_unload(sc->sc_dmat, sc->alc_cdata.alc_cmb_map);
	if (sc->alc_cdata.alc_cmb_map != NULL &&
	    sc->alc_rdata.alc_cmb != NULL)
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)sc->alc_rdata.alc_cmb, 1);
	sc->alc_rdata.alc_cmb = NULL;
	sc->alc_cdata.alc_cmb_map = NULL;

	/* SMB block */
	if (sc->alc_cdata.alc_smb_map != NULL)
		bus_dmamap_unload(sc->sc_dmat, sc->alc_cdata.alc_smb_map);
	if (sc->alc_cdata.alc_smb_map != NULL &&
	    sc->alc_rdata.alc_smb != NULL)
		bus_dmamem_free(sc->sc_dmat, 
		    (bus_dma_segment_t *)sc->alc_rdata.alc_smb, 1);
	sc->alc_rdata.alc_smb = NULL;
	sc->alc_cdata.alc_smb_map = NULL;
}

int
alc_encap(struct alc_softc *sc, struct mbuf *m)
{
	struct alc_txdesc *txd, *txd_last;
	struct tx_desc *desc;
	bus_dmamap_t map;
	uint32_t cflags, poff, vtag;
	int error, idx, prod;

	cflags = vtag = 0;
	poff = 0;

	prod = sc->alc_cdata.alc_tx_prod;
	txd = &sc->alc_cdata.alc_txdesc[prod];
	txd_last = txd;
	map = txd->tx_dmamap;

	error = bus_dmamap_load_mbuf(sc->sc_dmat, map, m, BUS_DMA_NOWAIT);
	if (error != 0 && error != EFBIG)
		goto drop;
	if (error != 0) {
		if (m_defrag(m, M_DONTWAIT)) {
			error = ENOBUFS;
			goto drop;
		}
		error = bus_dmamap_load_mbuf(sc->sc_dmat, map, m,
		    BUS_DMA_NOWAIT);
		if (error != 0)
			goto drop;
	}

	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	desc = NULL;
	idx = 0;
#if NVLAN > 0
	/* Configure VLAN hardware tag insertion. */
	if (m->m_flags & M_VLANTAG) {
		vtag = htons(m->m_pkthdr.ether_vtag);
		vtag = (vtag << TD_VLAN_SHIFT) & TD_VLAN_MASK;
		cflags |= TD_INS_VLAN_TAG;
	}
#endif
	/* Configure Tx checksum offload. */
	if ((m->m_pkthdr.csum_flags & ALC_CSUM_FEATURES) != 0) {
		cflags |= TD_CUSTOM_CSUM;
		/* Set checksum start offset. */
		cflags |= ((poff >> 1) << TD_PLOAD_OFFSET_SHIFT) &
		    TD_PLOAD_OFFSET_MASK;
	}

	for (; idx < map->dm_nsegs; idx++) {
		desc = &sc->alc_rdata.alc_tx_ring[prod];
		desc->len =
		    htole32(TX_BYTES(map->dm_segs[idx].ds_len) | vtag);
		desc->flags = htole32(cflags);
		desc->addr = htole64(map->dm_segs[idx].ds_addr);
		sc->alc_cdata.alc_tx_cnt++;
		ALC_DESC_INC(prod, ALC_TX_RING_CNT);
	}

	/* Update producer index. */
	sc->alc_cdata.alc_tx_prod = prod;

	/* Finally set EOP on the last descriptor. */
	prod = (prod + ALC_TX_RING_CNT - 1) % ALC_TX_RING_CNT;
	desc = &sc->alc_rdata.alc_tx_ring[prod];
	desc->flags |= htole32(TD_EOP);

	/* Swap dmamap of the first and the last. */
	txd = &sc->alc_cdata.alc_txdesc[prod];
	map = txd_last->tx_dmamap;
	txd_last->tx_dmamap = txd->tx_dmamap;
	txd->tx_dmamap = map;
	txd->tx_m = m;

	return (0);

drop:
	m_freem(m);
	return (error);
}

void
alc_start(struct ifnet *ifp)
{
	struct alc_softc *sc = ifp->if_softc;
	struct mbuf *m;
	int enq = 0;

	/* Reclaim transmitted frames. */
	if (sc->alc_cdata.alc_tx_cnt >= ALC_TX_DESC_HIWAT)
		alc_txeof(sc);

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;
	if ((sc->alc_flags & ALC_FLAG_LINK) == 0)
		return;
	if (ifq_empty(&ifp->if_snd))
		return;

	for (;;) {
		if (sc->alc_cdata.alc_tx_cnt + ALC_MAXTXSEGS >=
		    ALC_TX_RING_CNT - 3) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;

		if (alc_encap(sc, m) != 0) {
			ifp->if_oerrors++;
			continue;
		}
		enq++;
		
#if NBPFILTER > 0
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		if (ifp->if_bpf != NULL)
			bpf_mtap_ether(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
	}

	if (enq > 0) {
		/* Sync descriptors. */
		bus_dmamap_sync(sc->sc_dmat, sc->alc_cdata.alc_tx_ring_map, 0,
		    sc->alc_cdata.alc_tx_ring_map->dm_mapsize, 
		    BUS_DMASYNC_PREWRITE);
		/* Kick. Assume we're using normal Tx priority queue. */
		if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
			CSR_WRITE_2(sc, ALC_MBOX_TD_PRI0_PROD_IDX,
			    (uint16_t)sc->alc_cdata.alc_tx_prod);
		else
			CSR_WRITE_4(sc, ALC_MBOX_TD_PROD_IDX,
			    (sc->alc_cdata.alc_tx_prod <<
			    MBOX_TD_PROD_LO_IDX_SHIFT) &
			    MBOX_TD_PROD_LO_IDX_MASK);
		/* Set a timeout in case the chip goes out to lunch. */
		ifp->if_timer = ALC_TX_TIMEOUT;
	}
}

void
alc_watchdog(struct ifnet *ifp)
{
	struct alc_softc *sc = ifp->if_softc;

	if ((sc->alc_flags & ALC_FLAG_LINK) == 0) {
		printf("%s: watchdog timeout (missed link)\n",
		    sc->sc_dev.dv_xname);
		ifp->if_oerrors++;
		alc_init(ifp);
		return;
	}

	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);
	ifp->if_oerrors++;
	alc_init(ifp);
	alc_start(ifp);
}

int
alc_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct alc_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_miibus;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			alc_init(ifp);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				alc_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				alc_stop(sc);
		}
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			alc_iff(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
alc_mac_config(struct alc_softc *sc)
{
	struct mii_data *mii;
	uint32_t reg;

	mii = &sc->sc_miibus;
	reg = CSR_READ_4(sc, ALC_MAC_CFG);
	reg &= ~(MAC_CFG_FULL_DUPLEX | MAC_CFG_TX_FC | MAC_CFG_RX_FC |
	    MAC_CFG_SPEED_MASK);
	if ((sc->sc_product == PCI_PRODUCT_ATTANSIC_L1D ||
	    sc->sc_product == PCI_PRODUCT_ATTANSIC_L1D_1 ||
	    sc->sc_product == PCI_PRODUCT_ATTANSIC_L2C_2 ||
	    sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
		reg |= MAC_CFG_HASH_ALG_CRC32 | MAC_CFG_SPEED_MODE_SW;
	/* Reprogram MAC with resolved speed/duplex. */
	switch (IFM_SUBTYPE(mii->mii_media_active)) {
	case IFM_10_T:
	case IFM_100_TX:
		reg |= MAC_CFG_SPEED_10_100;
		break;
	case IFM_1000_T:
		reg |= MAC_CFG_SPEED_1000;
		break;
	}
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		reg |= MAC_CFG_FULL_DUPLEX;
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_TXPAUSE) != 0)
			reg |= MAC_CFG_TX_FC;
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_RXPAUSE) != 0)
			reg |= MAC_CFG_RX_FC;
	}
	CSR_WRITE_4(sc, ALC_MAC_CFG, reg);
}

void
alc_stats_clear(struct alc_softc *sc)
{
	struct smb sb, *smb;
	uint32_t *reg;
	int i;

	if ((sc->alc_flags & ALC_FLAG_SMB_BUG) == 0) {
		bus_dmamap_sync(sc->sc_dmat, sc->alc_cdata.alc_smb_map, 0,
		    sc->alc_cdata.alc_smb_map->dm_mapsize, 
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		smb = sc->alc_rdata.alc_smb;
		/* Update done, clear. */
		smb->updated = 0;
		bus_dmamap_sync(sc->sc_dmat, sc->alc_cdata.alc_smb_map, 0,
		    sc->alc_cdata.alc_smb_map->dm_mapsize, 
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	} else {
		for (reg = &sb.rx_frames, i = 0; reg <= &sb.rx_pkts_filtered;
		    reg++) {
			CSR_READ_4(sc, ALC_RX_MIB_BASE + i);
			i += sizeof(uint32_t);
		}
		/* Read Tx statistics. */
		for (reg = &sb.tx_frames, i = 0; reg <= &sb.tx_mcast_bytes;
		    reg++) {
			CSR_READ_4(sc, ALC_TX_MIB_BASE + i);
			i += sizeof(uint32_t);
		}
	}
}

void
alc_stats_update(struct alc_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct alc_hw_stats *stat;
	struct smb sb, *smb;
	uint32_t *reg;
	int i;

	stat = &sc->alc_stats;
	if ((sc->alc_flags & ALC_FLAG_SMB_BUG) == 0) {
		bus_dmamap_sync(sc->sc_dmat, sc->alc_cdata.alc_smb_map, 0,
		    sc->alc_cdata.alc_smb_map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		smb = sc->alc_rdata.alc_smb;
		if (smb->updated == 0)
			return;
	} else {
		smb = &sb;
		/* Read Rx statistics. */
		for (reg = &sb.rx_frames, i = 0; reg <= &sb.rx_pkts_filtered;
		    reg++) {
			*reg = CSR_READ_4(sc, ALC_RX_MIB_BASE + i);
			i += sizeof(uint32_t);
		}
		/* Read Tx statistics. */
		for (reg = &sb.tx_frames, i = 0; reg <= &sb.tx_mcast_bytes;
		    reg++) {
			*reg = CSR_READ_4(sc, ALC_TX_MIB_BASE + i);
			i += sizeof(uint32_t);
		}
	}

	/* Rx stats. */
	stat->rx_frames += smb->rx_frames;
	stat->rx_bcast_frames += smb->rx_bcast_frames;
	stat->rx_mcast_frames += smb->rx_mcast_frames;
	stat->rx_pause_frames += smb->rx_pause_frames;
	stat->rx_control_frames += smb->rx_control_frames;
	stat->rx_crcerrs += smb->rx_crcerrs;
	stat->rx_lenerrs += smb->rx_lenerrs;
	stat->rx_bytes += smb->rx_bytes;
	stat->rx_runts += smb->rx_runts;
	stat->rx_fragments += smb->rx_fragments;
	stat->rx_pkts_64 += smb->rx_pkts_64;
	stat->rx_pkts_65_127 += smb->rx_pkts_65_127;
	stat->rx_pkts_128_255 += smb->rx_pkts_128_255;
	stat->rx_pkts_256_511 += smb->rx_pkts_256_511;
	stat->rx_pkts_512_1023 += smb->rx_pkts_512_1023;
	stat->rx_pkts_1024_1518 += smb->rx_pkts_1024_1518;
	stat->rx_pkts_1519_max += smb->rx_pkts_1519_max;
	stat->rx_pkts_truncated += smb->rx_pkts_truncated;
	stat->rx_fifo_oflows += smb->rx_fifo_oflows;
	stat->rx_rrs_errs += smb->rx_rrs_errs;
	stat->rx_alignerrs += smb->rx_alignerrs;
	stat->rx_bcast_bytes += smb->rx_bcast_bytes;
	stat->rx_mcast_bytes += smb->rx_mcast_bytes;
	stat->rx_pkts_filtered += smb->rx_pkts_filtered;

	/* Tx stats. */
	stat->tx_frames += smb->tx_frames;
	stat->tx_bcast_frames += smb->tx_bcast_frames;
	stat->tx_mcast_frames += smb->tx_mcast_frames;
	stat->tx_pause_frames += smb->tx_pause_frames;
	stat->tx_excess_defer += smb->tx_excess_defer;
	stat->tx_control_frames += smb->tx_control_frames;
	stat->tx_deferred += smb->tx_deferred;
	stat->tx_bytes += smb->tx_bytes;
	stat->tx_pkts_64 += smb->tx_pkts_64;
	stat->tx_pkts_65_127 += smb->tx_pkts_65_127;
	stat->tx_pkts_128_255 += smb->tx_pkts_128_255;
	stat->tx_pkts_256_511 += smb->tx_pkts_256_511;
	stat->tx_pkts_512_1023 += smb->tx_pkts_512_1023;
	stat->tx_pkts_1024_1518 += smb->tx_pkts_1024_1518;
	stat->tx_pkts_1519_max += smb->tx_pkts_1519_max;
	stat->tx_single_colls += smb->tx_single_colls;
	stat->tx_multi_colls += smb->tx_multi_colls;
	stat->tx_late_colls += smb->tx_late_colls;
	stat->tx_excess_colls += smb->tx_excess_colls;
	stat->tx_underrun += smb->tx_underrun;
	stat->tx_desc_underrun += smb->tx_desc_underrun;
	stat->tx_lenerrs += smb->tx_lenerrs;
	stat->tx_pkts_truncated += smb->tx_pkts_truncated;
	stat->tx_bcast_bytes += smb->tx_bcast_bytes;
	stat->tx_mcast_bytes += smb->tx_mcast_bytes;

	ifp->if_collisions += smb->tx_single_colls +
	    smb->tx_multi_colls * 2 + smb->tx_late_colls +
	    smb->tx_excess_colls * HDPX_CFG_RETRY_DEFAULT;

	ifp->if_oerrors += smb->tx_late_colls + smb->tx_excess_colls +
	    smb->tx_underrun + smb->tx_pkts_truncated;

	ifp->if_ierrors += smb->rx_crcerrs + smb->rx_lenerrs +
	    smb->rx_runts + smb->rx_pkts_truncated +
	    smb->rx_fifo_oflows + smb->rx_rrs_errs +
	    smb->rx_alignerrs;

	if ((sc->alc_flags & ALC_FLAG_SMB_BUG) == 0) {
		/* Update done, clear. */
		smb->updated = 0;
		bus_dmamap_sync(sc->sc_dmat, sc->alc_cdata.alc_smb_map, 0,
		    sc->alc_cdata.alc_smb_map->dm_mapsize,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}
}

int
alc_intr(void *arg)
{
	struct alc_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint32_t status;
	int claimed = 0;

	status = CSR_READ_4(sc, ALC_INTR_STATUS);
	if ((status & ALC_INTRS) == 0)
		return (0);

	/* Disable interrupts. */
	CSR_WRITE_4(sc, ALC_INTR_STATUS, INTR_DIS_INT);

	status = CSR_READ_4(sc, ALC_INTR_STATUS);
	if ((status & ALC_INTRS) == 0)
		goto back;

	/* Acknowledge and disable interrupts. */
	CSR_WRITE_4(sc, ALC_INTR_STATUS, status | INTR_DIS_INT);

	if (ifp->if_flags & IFF_RUNNING) {
		int error = 0;

		if (status & INTR_RX_PKT) {
			error = alc_rxintr(sc);
			if (error) {
				alc_init(ifp);
				return (0);
			}
		}
		if (status & (INTR_DMA_RD_TO_RST | INTR_DMA_WR_TO_RST |
		    INTR_TXQ_TO_RST)) {
			if (status & INTR_DMA_RD_TO_RST)
				printf("%s: DMA read error! -- resetting\n",
				    sc->sc_dev.dv_xname);
			if (status & INTR_DMA_WR_TO_RST)
				printf("%s: DMA write error! -- resetting\n",
				    sc->sc_dev.dv_xname);
			if (status & INTR_TXQ_TO_RST)
				printf("%s: TxQ reset! -- resetting\n",
				    sc->sc_dev.dv_xname);
			alc_init(ifp);
			return (0);
		}

		alc_txeof(sc);
		alc_start(ifp);
	}

	claimed = 1;
back:
	/* Re-enable interrupts. */
	CSR_WRITE_4(sc, ALC_INTR_STATUS, 0x7FFFFFFF);
	return (claimed);
}

void
alc_txeof(struct alc_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct alc_txdesc *txd;
	uint32_t cons, prod;
	int prog;

	if (sc->alc_cdata.alc_tx_cnt == 0)
		return;
	bus_dmamap_sync(sc->sc_dmat, sc->alc_cdata.alc_tx_ring_map, 0,
	    sc->alc_cdata.alc_tx_ring_map->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);
	if ((sc->alc_flags & ALC_FLAG_CMB_BUG) == 0) {
		bus_dmamap_sync(sc->sc_dmat, sc->alc_cdata.alc_cmb_map, 0,
		    sc->alc_cdata.alc_cmb_map->dm_mapsize, 
		    BUS_DMASYNC_POSTREAD);
		prod = sc->alc_rdata.alc_cmb->cons;
	} else {
		if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
			prod = CSR_READ_2(sc, ALC_MBOX_TD_PRI0_CONS_IDX);
		else {
			prod = CSR_READ_4(sc, ALC_MBOX_TD_CONS_IDX);
			/* Assume we're using normal Tx priority queue. */
			prod = (prod & MBOX_TD_CONS_LO_IDX_MASK) >>
			    MBOX_TD_CONS_LO_IDX_SHIFT;
		}
	}
	cons = sc->alc_cdata.alc_tx_cons;
	/*
	 * Go through our Tx list and free mbufs for those
	 * frames which have been transmitted.
	 */
	for (prog = 0; cons != prod; prog++,
	    ALC_DESC_INC(cons, ALC_TX_RING_CNT)) {
		if (sc->alc_cdata.alc_tx_cnt <= 0)
			break;
		prog++;
		ifq_clr_oactive(&ifp->if_snd);
		sc->alc_cdata.alc_tx_cnt--;
		txd = &sc->alc_cdata.alc_txdesc[cons];
		if (txd->tx_m != NULL) {
			/* Reclaim transmitted mbufs. */
			bus_dmamap_sync(sc->sc_dmat, txd->tx_dmamap, 0,
			    txd->tx_dmamap->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
		}
	}

	if ((sc->alc_flags & ALC_FLAG_CMB_BUG) == 0)
	    bus_dmamap_sync(sc->sc_dmat, sc->alc_cdata.alc_cmb_map, 0,
	        sc->alc_cdata.alc_cmb_map->dm_mapsize, BUS_DMASYNC_PREREAD);
	sc->alc_cdata.alc_tx_cons = cons;
	/*
	 * Unarm watchdog timer only when there is no pending
	 * frames in Tx queue.
	 */
	if (sc->alc_cdata.alc_tx_cnt == 0)
		ifp->if_timer = 0;
}

int
alc_newbuf(struct alc_softc *sc, struct alc_rxdesc *rxd)
{
	struct mbuf *m;
	bus_dmamap_t map;
	int error;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);
	MCLGET(m, M_DONTWAIT);
	if (!(m->m_flags & M_EXT)) {
		m_freem(m);
		return (ENOBUFS);
	}

	m->m_len = m->m_pkthdr.len = RX_BUF_SIZE_MAX;

	error = bus_dmamap_load_mbuf(sc->sc_dmat,
	    sc->alc_cdata.alc_rx_sparemap, m, BUS_DMA_NOWAIT);

	if (error != 0) {
		m_freem(m);
		printf("%s: can't load RX mbuf\n", sc->sc_dev.dv_xname);
		return (error);
	}

	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(sc->sc_dmat, rxd->rx_dmamap, 0,
		    rxd->rx_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc->alc_cdata.alc_rx_sparemap;
	sc->alc_cdata.alc_rx_sparemap = map;
	bus_dmamap_sync(sc->sc_dmat, rxd->rx_dmamap, 0, rxd->rx_dmamap->dm_mapsize,
	    BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;
	rxd->rx_desc->addr = htole64(rxd->rx_dmamap->dm_segs[0].ds_addr);
	return (0);
}

int
alc_rxintr(struct alc_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct rx_rdesc *rrd;
	uint32_t nsegs, status;
	int rr_cons, prog;

	bus_dmamap_sync(sc->sc_dmat, sc->alc_cdata.alc_rr_ring_map, 0,
	    sc->alc_cdata.alc_rr_ring_map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(sc->sc_dmat, sc->alc_cdata.alc_rx_ring_map, 0,
	    sc->alc_cdata.alc_rx_ring_map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	rr_cons = sc->alc_cdata.alc_rr_cons;
	for (prog = 0; (ifp->if_flags & IFF_RUNNING) != 0;) {
		rrd = &sc->alc_rdata.alc_rr_ring[rr_cons];
		status = letoh32(rrd->status);
		if ((status & RRD_VALID) == 0)
			break;
		nsegs = RRD_RD_CNT(letoh32(rrd->rdinfo));
		if (nsegs == 0) {
			/* This should not happen! */
			if (alcdebug)
				printf("%s: unexpected segment count -- "
				    "resetting\n", sc->sc_dev.dv_xname);
			return (EIO);
		}
		alc_rxeof(sc, rrd);
		/* Clear Rx return status. */
		rrd->status = 0;
		ALC_DESC_INC(rr_cons, ALC_RR_RING_CNT);
		sc->alc_cdata.alc_rx_cons += nsegs;
		sc->alc_cdata.alc_rx_cons %= ALC_RR_RING_CNT;
		prog += nsegs;
	}

	if (prog > 0) {
		/* Update the consumer index. */
		sc->alc_cdata.alc_rr_cons = rr_cons;
		/* Sync Rx return descriptors. */
		bus_dmamap_sync(sc->sc_dmat, sc->alc_cdata.alc_rr_ring_map, 0,
		    sc->alc_cdata.alc_rr_ring_map->dm_mapsize,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		/*
		 * Sync updated Rx descriptors such that controller see
		 * modified buffer addresses.
		 */
		bus_dmamap_sync(sc->sc_dmat, sc->alc_cdata.alc_rx_ring_map, 0,
		    sc->alc_cdata.alc_rx_ring_map->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);
		/*
		 * Let controller know availability of new Rx buffers.
		 * Since alc(4) use RXQ_CFG_RD_BURST_DEFAULT descriptors
		 * it may be possible to update ALC_MBOX_RD0_PROD_IDX
		 * only when Rx buffer pre-fetching is required. In
		 * addition we already set ALC_RX_RD_FREE_THRESH to
		 * RX_RD_FREE_THRESH_LO_DEFAULT descriptors. However
		 * it still seems that pre-fetching needs more
		 * experimentation.
		 */
		if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
			CSR_WRITE_2(sc, ALC_MBOX_RD0_PROD_IDX,
			    (uint16_t)sc->alc_cdata.alc_rx_cons);
		else
			CSR_WRITE_4(sc, ALC_MBOX_RD0_PROD_IDX,
			    sc->alc_cdata.alc_rx_cons);
	}

	return (0);
}

/* Receive a frame. */
void
alc_rxeof(struct alc_softc *sc, struct rx_rdesc *rrd)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct alc_rxdesc *rxd;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *mp, *m;
	uint32_t rdinfo, status;
	int count, nsegs, rx_cons;

	status = letoh32(rrd->status);
	rdinfo = letoh32(rrd->rdinfo);
	rx_cons = RRD_RD_IDX(rdinfo);
	nsegs = RRD_RD_CNT(rdinfo);

	sc->alc_cdata.alc_rxlen = RRD_BYTES(status);
	if (status & (RRD_ERR_SUM | RRD_ERR_LENGTH)) {
		/*
		 * We want to pass the following frames to upper
		 * layer regardless of error status of Rx return
		 * ring.
		 *
		 *  o IP/TCP/UDP checksum is bad.
		 *  o frame length and protocol specific length
		 *     does not match.
		 *
		 *  Force network stack compute checksum for
		 *  errored frames.
		 */
		if ((status & (RRD_ERR_CRC | RRD_ERR_ALIGN |
		    RRD_ERR_TRUNC | RRD_ERR_RUNT)) != 0)
			return;
	}

	for (count = 0; count < nsegs; count++,
	    ALC_DESC_INC(rx_cons, ALC_RX_RING_CNT)) {
		rxd = &sc->alc_cdata.alc_rxdesc[rx_cons];
		mp = rxd->rx_m;
		/* Add a new receive buffer to the ring. */
		if (alc_newbuf(sc, rxd) != 0) {
			ifp->if_iqdrops++;
			/* Reuse Rx buffers. */
			m_freem(sc->alc_cdata.alc_rxhead);
			break;
		}

		/*
		 * Assume we've received a full sized frame.
		 * Actual size is fixed when we encounter the end of
		 * multi-segmented frame.
		 */
		mp->m_len = sc->alc_buf_size;

		/* Chain received mbufs. */
		if (sc->alc_cdata.alc_rxhead == NULL) {
			sc->alc_cdata.alc_rxhead = mp;
			sc->alc_cdata.alc_rxtail = mp;
		} else {
			mp->m_flags &= ~M_PKTHDR;
			sc->alc_cdata.alc_rxprev_tail =
			    sc->alc_cdata.alc_rxtail;
			sc->alc_cdata.alc_rxtail->m_next = mp;
			sc->alc_cdata.alc_rxtail = mp;
		}

		if (count == nsegs - 1) {
			/* Last desc. for this frame. */
			m = sc->alc_cdata.alc_rxhead;
			m->m_flags |= M_PKTHDR;
			/*
			 * It seems that L1C/L2C controller has no way
			 * to tell hardware to strip CRC bytes.
			 */
			m->m_pkthdr.len =
			    sc->alc_cdata.alc_rxlen - ETHER_CRC_LEN;
			if (nsegs > 1) {
				/* Set last mbuf size. */
				mp->m_len = sc->alc_cdata.alc_rxlen -
				    (nsegs - 1) * sc->alc_buf_size;
				/* Remove the CRC bytes in chained mbufs. */
				if (mp->m_len <= ETHER_CRC_LEN) {
					sc->alc_cdata.alc_rxtail =
					    sc->alc_cdata.alc_rxprev_tail;
					sc->alc_cdata.alc_rxtail->m_len -=
					    (ETHER_CRC_LEN - mp->m_len);
					sc->alc_cdata.alc_rxtail->m_next = NULL;
					m_freem(mp);
				} else {
					mp->m_len -= ETHER_CRC_LEN;
				}
			} else
				m->m_len = m->m_pkthdr.len;
			/*
			 * Due to hardware bugs, Rx checksum offloading
			 * was intentionally disabled.
			 */
#if NVLAN > 0
			if (status & RRD_VLAN_TAG) {
				u_int32_t vtag = RRD_VLAN(letoh32(rrd->vtag));
				m->m_pkthdr.ether_vtag = ntohs(vtag);
				m->m_flags |= M_VLANTAG;
			}
#endif


			ml_enqueue(&ml, m);
		}
	}
	if_input(ifp, &ml);

	/* Reset mbuf chains. */
	ALC_RXCHAIN_RESET(sc);
}

void
alc_tick(void *xsc)
{
	struct alc_softc *sc = xsc;
	struct mii_data *mii = &sc->sc_miibus;
	int s;

	s = splnet();
	mii_tick(mii);
	alc_stats_update(sc);

	timeout_add_sec(&sc->alc_tick_ch, 1);
	splx(s);
}

void
alc_osc_reset(struct alc_softc *sc)
{
	uint32_t reg;

	reg = CSR_READ_4(sc, ALC_MISC3);
	reg &= ~MISC3_25M_BY_SW;
	reg |= MISC3_25M_NOTO_INTNL;
	CSR_WRITE_4(sc, ALC_MISC3, reg);
	reg = CSR_READ_4(sc, ALC_MISC);
	if (AR816X_REV(sc->alc_rev) >= AR816X_REV_B0) {
		/*
		 * Restore over-current protection default value.
		 * This value could be reset by MAC reset.
		 */
		reg &= ~MISC_PSW_OCP_MASK;
		reg |= (MISC_PSW_OCP_DEFAULT << MISC_PSW_OCP_SHIFT);
		reg &= ~MISC_INTNLOSC_OPEN;
		CSR_WRITE_4(sc, ALC_MISC, reg);
		CSR_WRITE_4(sc, ALC_MISC, reg | MISC_INTNLOSC_OPEN);
		reg = CSR_READ_4(sc, ALC_MISC2);
		reg &= ~MISC2_CALB_START;
		CSR_WRITE_4(sc, ALC_MISC2, reg);
		CSR_WRITE_4(sc, ALC_MISC2, reg | MISC2_CALB_START);
	} else {
		reg &= ~MISC_INTNLOSC_OPEN;
		/* Disable isolate for revision A devices. */
		if (AR816X_REV(sc->alc_rev) <= AR816X_REV_A1)
			reg &= ~MISC_ISO_ENB;
		CSR_WRITE_4(sc, ALC_MISC, reg | MISC_INTNLOSC_OPEN);
		CSR_WRITE_4(sc, ALC_MISC, reg);
	}
	DELAY(20);
}

void
alc_reset(struct alc_softc *sc)
{
	uint32_t reg, pmcfg = 0;
	int i;

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		/* Reset workaround. */
		CSR_WRITE_4(sc, ALC_MBOX_RD0_PROD_IDX, 1);
		if (AR816X_REV(sc->alc_rev) <= AR816X_REV_A1 &&
		    (sc->alc_rev & 0x01) != 0) {
			/* Disable L0s/L1s before reset. */
			pmcfg = CSR_READ_4(sc, ALC_PM_CFG);
			if ((pmcfg & (PM_CFG_ASPM_L0S_ENB |
			    PM_CFG_ASPM_L1_ENB))!= 0) {
				pmcfg &= ~(PM_CFG_ASPM_L0S_ENB |
				    PM_CFG_ASPM_L1_ENB);
				CSR_WRITE_4(sc, ALC_PM_CFG, pmcfg);
			}
		}
	}
	reg = CSR_READ_4(sc, ALC_MASTER_CFG);
	reg |= MASTER_OOB_DIS_OFF | MASTER_RESET;
	CSR_WRITE_4(sc, ALC_MASTER_CFG, reg);

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		for (i = ALC_RESET_TIMEOUT; i > 0; i--) {
			DELAY(10);
			if (CSR_READ_4(sc, ALC_MBOX_RD0_PROD_IDX) == 0)
				break;
		}
		if (i == 0)
			printf("MAC reset timeout!\n");
	}
	for (i = ALC_RESET_TIMEOUT; i > 0; i--) {
		DELAY(10);
		if ((CSR_READ_4(sc, ALC_MASTER_CFG) & MASTER_RESET) == 0)
			break;
	}
	if (i == 0)
		printf("%s: master reset timeout!\n", sc->sc_dev.dv_xname);

	for (i = ALC_RESET_TIMEOUT; i > 0; i--) {
		reg = CSR_READ_4(sc, ALC_IDLE_STATUS);
		if ((reg & (IDLE_STATUS_RXMAC | IDLE_STATUS_TXMAC |
		    IDLE_STATUS_RXQ | IDLE_STATUS_TXQ)) == 0)
			break;
		DELAY(10);
	}

	if (i == 0)
		printf("%s: reset timeout(0x%08x)!\n", sc->sc_dev.dv_xname, 
		    reg);

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		if (AR816X_REV(sc->alc_rev) <= AR816X_REV_A1 &&
		    (sc->alc_rev & 0x01) != 0) {
			reg = CSR_READ_4(sc, ALC_MASTER_CFG);
			reg |= MASTER_CLK_SEL_DIS;
			CSR_WRITE_4(sc, ALC_MASTER_CFG, reg);
			/* Restore L0s/L1s config. */
			if ((pmcfg & (PM_CFG_ASPM_L0S_ENB |
			    PM_CFG_ASPM_L1_ENB)) != 0)
				CSR_WRITE_4(sc, ALC_PM_CFG, pmcfg);
		}
		alc_osc_reset(sc);
		reg = CSR_READ_4(sc, ALC_MISC3);
		reg &= ~MISC3_25M_BY_SW;
		reg |= MISC3_25M_NOTO_INTNL;
		CSR_WRITE_4(sc, ALC_MISC3, reg);
		reg = CSR_READ_4(sc, ALC_MISC);
		reg &= ~MISC_INTNLOSC_OPEN;
		if (AR816X_REV(sc->alc_rev) <= AR816X_REV_A1)
			reg &= ~MISC_ISO_ENB;
		CSR_WRITE_4(sc, ALC_MISC, reg);
		DELAY(20);
	}
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0 ||
	    sc->sc_product ==  PCI_PRODUCT_ATTANSIC_L2C_1 ||
	    sc->sc_product == PCI_PRODUCT_ATTANSIC_L2C_2)
		CSR_WRITE_4(sc, ALC_SERDES_LOCK,
		    CSR_READ_4(sc, ALC_SERDES_LOCK) |
		    SERDES_MAC_CLK_SLOWDOWN | SERDES_PHY_CLK_SLOWDOWN);
}

int
alc_init(struct ifnet *ifp)
{
	struct alc_softc *sc = ifp->if_softc;
	uint8_t eaddr[ETHER_ADDR_LEN];
	bus_addr_t paddr;
	uint32_t reg, rxf_hi, rxf_lo;
	int error;

	/*
	 * Cancel any pending I/O.
	 */
	alc_stop(sc);
	/*
	 * Reset the chip to a known state.
	 */
	alc_reset(sc);

	/* Initialize Rx descriptors. */
	error = alc_init_rx_ring(sc);
	if (error != 0) {
		printf("%s: no memory for Rx buffers.\n", sc->sc_dev.dv_xname);
		alc_stop(sc);
		return (error);
	}
	alc_init_rr_ring(sc);
	alc_init_tx_ring(sc);
	alc_init_cmb(sc);
	alc_init_smb(sc);

	/* Enable all clocks. */
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		CSR_WRITE_4(sc, ALC_CLK_GATING_CFG, CLK_GATING_DMAW_ENB |
		    CLK_GATING_DMAR_ENB | CLK_GATING_TXQ_ENB |
		    CLK_GATING_RXQ_ENB | CLK_GATING_TXMAC_ENB |
		    CLK_GATING_RXMAC_ENB);
		if (AR816X_REV(sc->alc_rev) >= AR816X_REV_B0)
			CSR_WRITE_4(sc, ALC_IDLE_DECISN_TIMER,
			    IDLE_DECISN_TIMER_DEFAULT_1MS);
	} else
		CSR_WRITE_4(sc, ALC_CLK_GATING_CFG, 0);

	/* Reprogram the station address. */
	bcopy(LLADDR(ifp->if_sadl), eaddr, ETHER_ADDR_LEN);
	CSR_WRITE_4(sc, ALC_PAR0,
	    eaddr[2] << 24 | eaddr[3] << 16 | eaddr[4] << 8 | eaddr[5]);
	CSR_WRITE_4(sc, ALC_PAR1, eaddr[0] << 8 | eaddr[1]);
	/*
	 * Clear WOL status and disable all WOL feature as WOL
	 * would interfere Rx operation under normal environments.
	 */
	CSR_READ_4(sc, ALC_WOL_CFG);
	CSR_WRITE_4(sc, ALC_WOL_CFG, 0);
	/* Set Tx descriptor base addresses. */
	paddr = sc->alc_rdata.alc_tx_ring_paddr;
	CSR_WRITE_4(sc, ALC_TX_BASE_ADDR_HI, ALC_ADDR_HI(paddr));
	CSR_WRITE_4(sc, ALC_TDL_HEAD_ADDR_LO, ALC_ADDR_LO(paddr));
	/* We don't use high priority ring. */
	CSR_WRITE_4(sc, ALC_TDH_HEAD_ADDR_LO, 0);
	/* Set Tx descriptor counter. */
	CSR_WRITE_4(sc, ALC_TD_RING_CNT,
	    (ALC_TX_RING_CNT << TD_RING_CNT_SHIFT) & TD_RING_CNT_MASK);
	/* Set Rx descriptor base addresses. */
	paddr = sc->alc_rdata.alc_rx_ring_paddr;
	CSR_WRITE_4(sc, ALC_RX_BASE_ADDR_HI, ALC_ADDR_HI(paddr));
	CSR_WRITE_4(sc, ALC_RD0_HEAD_ADDR_LO, ALC_ADDR_LO(paddr));
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0) {
		/* We use one Rx ring. */
		CSR_WRITE_4(sc, ALC_RD1_HEAD_ADDR_LO, 0);
		CSR_WRITE_4(sc, ALC_RD2_HEAD_ADDR_LO, 0);
		CSR_WRITE_4(sc, ALC_RD3_HEAD_ADDR_LO, 0);
	}
	/* Set Rx descriptor counter. */
	CSR_WRITE_4(sc, ALC_RD_RING_CNT,
	    (ALC_RX_RING_CNT << RD_RING_CNT_SHIFT) & RD_RING_CNT_MASK);

	/*
	 * Let hardware split jumbo frames into alc_max_buf_sized chunks.
	 * if it do not fit the buffer size. Rx return descriptor holds
	 * a counter that indicates how many fragments were made by the
	 * hardware. The buffer size should be multiple of 8 bytes.
	 * Since hardware has limit on the size of buffer size, always
	 * use the maximum value.
	 * For strict-alignment architectures make sure to reduce buffer
	 * size by 8 bytes to make room for alignment fixup.
	 */
	sc->alc_buf_size = RX_BUF_SIZE_MAX;
	CSR_WRITE_4(sc, ALC_RX_BUF_SIZE, sc->alc_buf_size);

	paddr = sc->alc_rdata.alc_rr_ring_paddr;
	/* Set Rx return descriptor base addresses. */
	CSR_WRITE_4(sc, ALC_RRD0_HEAD_ADDR_LO, ALC_ADDR_LO(paddr));
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0) {
		/* We use one Rx return ring. */
		CSR_WRITE_4(sc, ALC_RRD1_HEAD_ADDR_LO, 0);
		CSR_WRITE_4(sc, ALC_RRD2_HEAD_ADDR_LO, 0);
		CSR_WRITE_4(sc, ALC_RRD3_HEAD_ADDR_LO, 0);
	}
	/* Set Rx return descriptor counter. */
	CSR_WRITE_4(sc, ALC_RRD_RING_CNT,
	    (ALC_RR_RING_CNT << RRD_RING_CNT_SHIFT) & RRD_RING_CNT_MASK);
	paddr = sc->alc_rdata.alc_cmb_paddr;
	CSR_WRITE_4(sc, ALC_CMB_BASE_ADDR_LO, ALC_ADDR_LO(paddr));
	paddr = sc->alc_rdata.alc_smb_paddr;
	CSR_WRITE_4(sc, ALC_SMB_BASE_ADDR_HI, ALC_ADDR_HI(paddr));
	CSR_WRITE_4(sc, ALC_SMB_BASE_ADDR_LO, ALC_ADDR_LO(paddr));

	if (sc->sc_product == PCI_PRODUCT_ATTANSIC_L2C_1) {
		/* Reconfigure SRAM - Vendor magic. */
		CSR_WRITE_4(sc, ALC_SRAM_RX_FIFO_LEN, 0x000002A0);
		CSR_WRITE_4(sc, ALC_SRAM_TX_FIFO_LEN, 0x00000100);
		CSR_WRITE_4(sc, ALC_SRAM_RX_FIFO_ADDR, 0x029F0000);
		CSR_WRITE_4(sc, ALC_SRAM_RD0_ADDR, 0x02BF02A0);
		CSR_WRITE_4(sc, ALC_SRAM_TX_FIFO_ADDR, 0x03BF02C0);
		CSR_WRITE_4(sc, ALC_SRAM_TD_ADDR, 0x03DF03C0);
		CSR_WRITE_4(sc, ALC_TXF_WATER_MARK, 0x00000000);
		CSR_WRITE_4(sc, ALC_RD_DMA_CFG, 0x00000000);
	}

	/* Tell hardware that we're ready to load DMA blocks. */
	CSR_WRITE_4(sc, ALC_DMA_BLOCK, DMA_BLOCK_LOAD);

	/* Configure interrupt moderation timer. */
	sc->alc_int_rx_mod = ALC_IM_RX_TIMER_DEFAULT;
	sc->alc_int_tx_mod = ALC_IM_TX_TIMER_DEFAULT;
	reg = ALC_USECS(sc->alc_int_rx_mod) << IM_TIMER_RX_SHIFT;
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0)
	    reg |= ALC_USECS(sc->alc_int_tx_mod) << IM_TIMER_TX_SHIFT;
	CSR_WRITE_4(sc, ALC_IM_TIMER, reg);
	/*
	 * We don't want to automatic interrupt clear as task queue
	 * for the interrupt should know interrupt status.
	 */
	reg = CSR_READ_4(sc, ALC_MASTER_CFG);
	reg &= ~(MASTER_IM_RX_TIMER_ENB | MASTER_IM_TX_TIMER_ENB);
	reg |= MASTER_SA_TIMER_ENB;
	if (ALC_USECS(sc->alc_int_rx_mod) != 0)
		reg |= MASTER_IM_RX_TIMER_ENB;
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0 &&
	    ALC_USECS(sc->alc_int_tx_mod) != 0)
		reg |= MASTER_IM_TX_TIMER_ENB;
	CSR_WRITE_4(sc, ALC_MASTER_CFG, reg);
	/*
	 * Disable interrupt re-trigger timer. We don't want automatic
	 * re-triggering of un-ACKed interrupts.
	 */
	CSR_WRITE_4(sc, ALC_INTR_RETRIG_TIMER, ALC_USECS(0));
	/* Configure CMB. */
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		CSR_WRITE_4(sc, ALC_CMB_TD_THRESH, ALC_TX_RING_CNT / 3);
		CSR_WRITE_4(sc, ALC_CMB_TX_TIMER,
		    ALC_USECS(sc->alc_int_tx_mod));
	} else {
		if ((sc->alc_flags & ALC_FLAG_CMB_BUG) == 0) {
			CSR_WRITE_4(sc, ALC_CMB_TD_THRESH, 4);
			CSR_WRITE_4(sc, ALC_CMB_TX_TIMER, ALC_USECS(5000));
		} else
			CSR_WRITE_4(sc, ALC_CMB_TX_TIMER, ALC_USECS(0));
	}
	/*
	 * Hardware can be configured to issue SMB interrupt based
	 * on programmed interval. Since there is a callout that is
	 * invoked for every hz in driver we use that instead of
	 * relying on periodic SMB interrupt.
	 */
	CSR_WRITE_4(sc, ALC_SMB_STAT_TIMER, ALC_USECS(0));
	/* Clear MAC statistics. */
	alc_stats_clear(sc);

	/*
	 * Always use maximum frame size that controller can support.
	 * Otherwise received frames that has larger frame length
	 * than alc(4) MTU would be silently dropped in hardware. This
	 * would make path-MTU discovery hard as sender wouldn't get
	 * any responses from receiver. alc(4) supports
	 * multi-fragmented frames on Rx path so it has no issue on
	 * assembling fragmented frames. Using maximum frame size also
	 * removes the need to reinitialize hardware when interface
	 * MTU configuration was changed.
	 *
	 * Be conservative in what you do, be liberal in what you
	 * accept from others - RFC 793.
	 */
	CSR_WRITE_4(sc, ALC_FRAME_SIZE, sc->alc_max_framelen);

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0) {
		/* Disable header split(?) */
		CSR_WRITE_4(sc, ALC_HDS_CFG, 0);
		/* Configure IPG/IFG parameters. */
		CSR_WRITE_4(sc, ALC_IPG_IFG_CFG,
		    ((IPG_IFG_IPGT_DEFAULT << IPG_IFG_IPGT_SHIFT) &
		    IPG_IFG_IPGT_MASK) |
		    ((IPG_IFG_MIFG_DEFAULT << IPG_IFG_MIFG_SHIFT) &
		    IPG_IFG_MIFG_MASK) |
		    ((IPG_IFG_IPG1_DEFAULT << IPG_IFG_IPG1_SHIFT) &
		    IPG_IFG_IPG1_MASK) |
		    ((IPG_IFG_IPG2_DEFAULT << IPG_IFG_IPG2_SHIFT) &
		    IPG_IFG_IPG2_MASK));
		/* Set parameters for half-duplex media. */
		CSR_WRITE_4(sc, ALC_HDPX_CFG,
		    ((HDPX_CFG_LCOL_DEFAULT << HDPX_CFG_LCOL_SHIFT) &
		    HDPX_CFG_LCOL_MASK) |
		    ((HDPX_CFG_RETRY_DEFAULT << HDPX_CFG_RETRY_SHIFT) &
		    HDPX_CFG_RETRY_MASK) | HDPX_CFG_EXC_DEF_EN |
		    ((HDPX_CFG_ABEBT_DEFAULT << HDPX_CFG_ABEBT_SHIFT) &
		    HDPX_CFG_ABEBT_MASK) |
		    ((HDPX_CFG_JAMIPG_DEFAULT << HDPX_CFG_JAMIPG_SHIFT) &
		    HDPX_CFG_JAMIPG_MASK));
	}

	/*
	 * Set TSO/checksum offload threshold. For frames that is
	 * larger than this threshold, hardware wouldn't do
	 * TSO/checksum offloading.
	 */
	reg = (sc->alc_max_framelen >> TSO_OFFLOAD_THRESH_UNIT_SHIFT) &
	    TSO_OFFLOAD_THRESH_MASK;
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
		reg |= TSO_OFFLOAD_ERRLGPKT_DROP_ENB;
	CSR_WRITE_4(sc, ALC_TSO_OFFLOAD_THRESH, reg);
	/* Configure TxQ. */
	reg = (alc_dma_burst[sc->alc_dma_rd_burst] <<
	    TXQ_CFG_TX_FIFO_BURST_SHIFT) & TXQ_CFG_TX_FIFO_BURST_MASK;
	if (sc->sc_product == PCI_PRODUCT_ATTANSIC_L2C_1 ||
	    sc->sc_product == PCI_PRODUCT_ATTANSIC_L2C_2)
		reg >>= 1;
	reg |= (TXQ_CFG_TD_BURST_DEFAULT << TXQ_CFG_TD_BURST_SHIFT) &
	    TXQ_CFG_TD_BURST_MASK;
	reg |= TXQ_CFG_IP_OPTION_ENB | TXQ_CFG_8023_ENB;
	CSR_WRITE_4(sc, ALC_TXQ_CFG, reg | TXQ_CFG_ENHANCED_MODE);
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		reg = (TXQ_CFG_TD_BURST_DEFAULT << HQTD_CFG_Q1_BURST_SHIFT |
		    TXQ_CFG_TD_BURST_DEFAULT << HQTD_CFG_Q2_BURST_SHIFT |
		    TXQ_CFG_TD_BURST_DEFAULT << HQTD_CFG_Q3_BURST_SHIFT |
		    HQTD_CFG_BURST_ENB);
		CSR_WRITE_4(sc, ALC_HQTD_CFG, reg);
		reg = WRR_PRI_RESTRICT_NONE;
		reg |= (WRR_PRI_DEFAULT << WRR_PRI0_SHIFT |
		    WRR_PRI_DEFAULT << WRR_PRI1_SHIFT |
		    WRR_PRI_DEFAULT << WRR_PRI2_SHIFT |
		    WRR_PRI_DEFAULT << WRR_PRI3_SHIFT);
		CSR_WRITE_4(sc, ALC_WRR, reg);
	} else {
		/* Configure Rx free descriptor pre-fetching. */
		CSR_WRITE_4(sc, ALC_RX_RD_FREE_THRESH,
		    ((RX_RD_FREE_THRESH_HI_DEFAULT <<
		    RX_RD_FREE_THRESH_HI_SHIFT) & RX_RD_FREE_THRESH_HI_MASK) |
		    ((RX_RD_FREE_THRESH_LO_DEFAULT <<
		    RX_RD_FREE_THRESH_LO_SHIFT) & RX_RD_FREE_THRESH_LO_MASK));
	}

	/*
	 * Configure flow control parameters.
	 * XON  : 80% of Rx FIFO
	 * XOFF : 30% of Rx FIFO
	 */
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		reg = CSR_READ_4(sc, ALC_SRAM_RX_FIFO_LEN);
		reg &= SRAM_RX_FIFO_LEN_MASK;
		reg *= 8;
		if (reg > 8 * 1024)
			reg -= RX_FIFO_PAUSE_816X_RSVD;
		else
		    reg -= RX_BUF_SIZE_MAX;
		reg /= 8;
		CSR_WRITE_4(sc, ALC_RX_FIFO_PAUSE_THRESH,
		    ((reg << RX_FIFO_PAUSE_THRESH_LO_SHIFT) &
		    RX_FIFO_PAUSE_THRESH_LO_MASK) |
		    (((RX_FIFO_PAUSE_816X_RSVD / 8) <<
		    RX_FIFO_PAUSE_THRESH_HI_SHIFT) &
		    RX_FIFO_PAUSE_THRESH_HI_MASK));
	} else if (sc->sc_product == PCI_PRODUCT_ATTANSIC_L1C||
	    sc->sc_product == PCI_PRODUCT_ATTANSIC_L2C) {
		reg = CSR_READ_4(sc, ALC_SRAM_RX_FIFO_LEN);
		rxf_hi = (reg * 8) / 10;
		rxf_lo = (reg * 3) / 10;
		CSR_WRITE_4(sc, ALC_RX_FIFO_PAUSE_THRESH,
		    ((rxf_lo << RX_FIFO_PAUSE_THRESH_LO_SHIFT) &
		    RX_FIFO_PAUSE_THRESH_LO_MASK) |
		    ((rxf_hi << RX_FIFO_PAUSE_THRESH_HI_SHIFT) &
		    RX_FIFO_PAUSE_THRESH_HI_MASK));
	}

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0) {
		/* Disable RSS until I understand L1C/L2C's RSS logic. */
		CSR_WRITE_4(sc, ALC_RSS_IDT_TABLE0, 0);
		CSR_WRITE_4(sc, ALC_RSS_CPU, 0);
	}

	/* Configure RxQ. */
	reg = (RXQ_CFG_RD_BURST_DEFAULT << RXQ_CFG_RD_BURST_SHIFT) &
	    RXQ_CFG_RD_BURST_MASK;
	reg |= RXQ_CFG_RSS_MODE_DIS;
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		reg |= (RXQ_CFG_816X_IDT_TBL_SIZE_DEFAULT <<
		    RXQ_CFG_816X_IDT_TBL_SIZE_SHIFT) &
		    RXQ_CFG_816X_IDT_TBL_SIZE_MASK;
		if ((sc->alc_flags & ALC_FLAG_FASTETHER) == 0)
			reg |= RXQ_CFG_ASPM_THROUGHPUT_LIMIT_100M;
	} else {
		if ((sc->alc_flags & ALC_FLAG_FASTETHER) == 0 &&
		    sc->sc_product != PCI_PRODUCT_ATTANSIC_L1D_1)
			reg |= RXQ_CFG_ASPM_THROUGHPUT_LIMIT_100M;
	}
	CSR_WRITE_4(sc, ALC_RXQ_CFG, reg);

	/* Configure DMA parameters. */
	reg = DMA_CFG_OUT_ORDER | DMA_CFG_RD_REQ_PRI;
	reg |= sc->alc_rcb;
	if ((sc->alc_flags & ALC_FLAG_CMB_BUG) == 0)
		reg |= DMA_CFG_CMB_ENB;
	if ((sc->alc_flags & ALC_FLAG_SMB_BUG) == 0)
		reg |= DMA_CFG_SMB_ENB;
	else
		reg |= DMA_CFG_SMB_DIS;
	reg |= (sc->alc_dma_rd_burst & DMA_CFG_RD_BURST_MASK) <<
	    DMA_CFG_RD_BURST_SHIFT;
	reg |= (sc->alc_dma_wr_burst & DMA_CFG_WR_BURST_MASK) <<
	    DMA_CFG_WR_BURST_SHIFT;
	reg |= (DMA_CFG_RD_DELAY_CNT_DEFAULT << DMA_CFG_RD_DELAY_CNT_SHIFT) &
	    DMA_CFG_RD_DELAY_CNT_MASK;
	reg |= (DMA_CFG_WR_DELAY_CNT_DEFAULT << DMA_CFG_WR_DELAY_CNT_SHIFT) &
	    DMA_CFG_WR_DELAY_CNT_MASK;
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		switch (AR816X_REV(sc->alc_rev)) {
		case AR816X_REV_A0:
		case AR816X_REV_A1:
			reg |= DMA_CFG_RD_CHNL_SEL_2;
			break;
		case AR816X_REV_B0:
			/* FALLTHROUGH */
		default:
			reg |= DMA_CFG_RD_CHNL_SEL_4;
			break;
		}
	}
	CSR_WRITE_4(sc, ALC_DMA_CFG, reg);

	/*
	 * Configure Tx/Rx MACs.
	 *  - Auto-padding for short frames.
	 *  - Enable CRC generation.
	 *  Actual reconfiguration of MAC for resolved speed/duplex
	 *  is followed after detection of link establishment.
	 *  AR813x/AR815x always does checksum computation regardless
	 *  of MAC_CFG_RXCSUM_ENB bit. Also the controller is known to
	 *  have bug in protocol field in Rx return structure so
	 *  these controllers can't handle fragmented frames. Disable
	 *  Rx checksum offloading until there is a newer controller
	 *  that has sane implementation.
	 */
	reg = MAC_CFG_TX_CRC_ENB | MAC_CFG_TX_AUTO_PAD | MAC_CFG_FULL_DUPLEX |
	    ((MAC_CFG_PREAMBLE_DEFAULT << MAC_CFG_PREAMBLE_SHIFT) &
	    MAC_CFG_PREAMBLE_MASK);
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0 ||
	    sc->sc_product == PCI_PRODUCT_ATTANSIC_L1D ||
	    sc->sc_product == PCI_PRODUCT_ATTANSIC_L1D_1 ||
	    sc->sc_product == PCI_PRODUCT_ATTANSIC_L2C_2)
		reg |= MAC_CFG_HASH_ALG_CRC32 | MAC_CFG_SPEED_MODE_SW;
	if ((sc->alc_flags & ALC_FLAG_FASTETHER) != 0)
		reg |= MAC_CFG_SPEED_10_100;
	else
		reg |= MAC_CFG_SPEED_1000;
	CSR_WRITE_4(sc, ALC_MAC_CFG, reg);

	/* Set up the receive filter. */
	alc_iff(sc);

	alc_rxvlan(sc);

	/* Acknowledge all pending interrupts and clear it. */
	CSR_WRITE_4(sc, ALC_INTR_MASK, ALC_INTRS);
	CSR_WRITE_4(sc, ALC_INTR_STATUS, 0xFFFFFFFF);
	CSR_WRITE_4(sc, ALC_INTR_STATUS, 0);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	sc->alc_flags &= ~ALC_FLAG_LINK;
	/* Switch to the current media. */
	alc_mediachange(ifp);

	timeout_add_sec(&sc->alc_tick_ch, 1);

	return (0);
}

void
alc_stop(struct alc_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct alc_txdesc *txd;
	struct alc_rxdesc *rxd;
	uint32_t reg;
	int i;

	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	timeout_del(&sc->alc_tick_ch);
	sc->alc_flags &= ~ALC_FLAG_LINK;

	alc_stats_update(sc);

	/* Disable interrupts. */
	CSR_WRITE_4(sc, ALC_INTR_MASK, 0);
	CSR_WRITE_4(sc, ALC_INTR_STATUS, 0xFFFFFFFF);

	/* Disable DMA. */
	reg = CSR_READ_4(sc, ALC_DMA_CFG);
	reg &= ~(DMA_CFG_CMB_ENB | DMA_CFG_SMB_ENB);
	reg |= DMA_CFG_SMB_DIS;
	CSR_WRITE_4(sc, ALC_DMA_CFG, reg);
	DELAY(1000);

	/* Stop Rx/Tx MACs. */
	alc_stop_mac(sc);

	/* Disable interrupts which might be touched in taskq handler. */
	CSR_WRITE_4(sc, ALC_INTR_STATUS, 0xFFFFFFFF);

	/* Disable L0s/L1s */
	reg = CSR_READ_4(sc, ALC_PM_CFG);
	if ((reg & (PM_CFG_ASPM_L0S_ENB | PM_CFG_ASPM_L1_ENB))!= 0) {
		reg &= ~(PM_CFG_ASPM_L0S_ENB | PM_CFG_ASPM_L1_ENB);
		CSR_WRITE_4(sc, ALC_PM_CFG, reg);
	}

	/* Reclaim Rx buffers that have been processed. */
	m_freem(sc->alc_cdata.alc_rxhead);
	ALC_RXCHAIN_RESET(sc);
	/*
	 * Free Tx/Rx mbufs still in the queues.
	 */
	for (i = 0; i < ALC_RX_RING_CNT; i++) {
		rxd = &sc->alc_cdata.alc_rxdesc[i];
		if (rxd->rx_m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, rxd->rx_dmamap, 0,
			    rxd->rx_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmat, rxd->rx_dmamap);
			m_freem(rxd->rx_m);
			rxd->rx_m = NULL;
		}
	}
	for (i = 0; i < ALC_TX_RING_CNT; i++) {
		txd = &sc->alc_cdata.alc_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, txd->tx_dmamap, 0,
			    txd->tx_dmamap->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
		}
	}
}

void
alc_stop_mac(struct alc_softc *sc)
{
	uint32_t reg;
	int i;

	alc_stop_queue(sc);
	/* Disable Rx/Tx MAC. */
	reg = CSR_READ_4(sc, ALC_MAC_CFG);
	if ((reg & (MAC_CFG_TX_ENB | MAC_CFG_RX_ENB)) != 0) {
		reg &= ~(MAC_CFG_TX_ENB | MAC_CFG_RX_ENB);
		CSR_WRITE_4(sc, ALC_MAC_CFG, reg);
	}
	for (i = ALC_TIMEOUT; i > 0; i--) {
		reg = CSR_READ_4(sc, ALC_IDLE_STATUS);
		if ((reg & (IDLE_STATUS_RXMAC | IDLE_STATUS_TXMAC)) == 0)
			break;
		DELAY(10);
	}
	if (i == 0)
		printf("%s: could not disable Rx/Tx MAC(0x%08x)!\n",
		    sc->sc_dev.dv_xname, reg);
}

void
alc_start_queue(struct alc_softc *sc)
{
	uint32_t qcfg[] = {
		0,
		RXQ_CFG_QUEUE0_ENB,
		RXQ_CFG_QUEUE0_ENB | RXQ_CFG_QUEUE1_ENB,
		RXQ_CFG_QUEUE0_ENB | RXQ_CFG_QUEUE1_ENB | RXQ_CFG_QUEUE2_ENB,
		RXQ_CFG_ENB
	};
	uint32_t cfg;

	/* Enable RxQ. */
	cfg = CSR_READ_4(sc, ALC_RXQ_CFG);
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0) {
		cfg &= ~RXQ_CFG_ENB;
		cfg |= qcfg[1];
	} else
		cfg |= RXQ_CFG_QUEUE0_ENB;

	CSR_WRITE_4(sc, ALC_RXQ_CFG, cfg);
	/* Enable TxQ. */
	cfg = CSR_READ_4(sc, ALC_TXQ_CFG);
	cfg |= TXQ_CFG_ENB;
	CSR_WRITE_4(sc, ALC_TXQ_CFG, cfg);
}

void
alc_stop_queue(struct alc_softc *sc)
{
	uint32_t reg;
	int i;

	/* Disable RxQ. */
	reg = CSR_READ_4(sc, ALC_RXQ_CFG);
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0) {
		if ((reg & RXQ_CFG_ENB) != 0) {
			reg &= ~RXQ_CFG_ENB;
			CSR_WRITE_4(sc, ALC_RXQ_CFG, reg);
		}
	} else {
		if ((reg & RXQ_CFG_QUEUE0_ENB) != 0) {
			reg &= ~RXQ_CFG_QUEUE0_ENB;
			CSR_WRITE_4(sc, ALC_RXQ_CFG, reg);
		}
	}
	/* Disable TxQ. */
	reg = CSR_READ_4(sc, ALC_TXQ_CFG);
	if ((reg & TXQ_CFG_ENB) != 0) {
		reg &= ~TXQ_CFG_ENB;
		CSR_WRITE_4(sc, ALC_TXQ_CFG, reg);
	}
	DELAY(40);
	for (i = ALC_TIMEOUT; i > 0; i--) {
		reg = CSR_READ_4(sc, ALC_IDLE_STATUS);
		if ((reg & (IDLE_STATUS_RXQ | IDLE_STATUS_TXQ)) == 0)
			break;
		DELAY(10);
	}
	if (i == 0)
		printf("%s: could not disable RxQ/TxQ (0x%08x)!\n",
		    sc->sc_dev.dv_xname, reg);
}

void
alc_init_tx_ring(struct alc_softc *sc)
{
	struct alc_ring_data *rd;
	struct alc_txdesc *txd;
	int i;

	sc->alc_cdata.alc_tx_prod = 0;
	sc->alc_cdata.alc_tx_cons = 0;
	sc->alc_cdata.alc_tx_cnt = 0;

	rd = &sc->alc_rdata;
	bzero(rd->alc_tx_ring, ALC_TX_RING_SZ);
	for (i = 0; i < ALC_TX_RING_CNT; i++) {
		txd = &sc->alc_cdata.alc_txdesc[i];
		txd->tx_m = NULL;
	}

	bus_dmamap_sync(sc->sc_dmat, sc->alc_cdata.alc_tx_ring_map, 0,
	    sc->alc_cdata.alc_tx_ring_map->dm_mapsize, BUS_DMASYNC_PREWRITE);
}

int
alc_init_rx_ring(struct alc_softc *sc)
{
	struct alc_ring_data *rd;
	struct alc_rxdesc *rxd;
	int i;

	sc->alc_cdata.alc_rx_cons = ALC_RX_RING_CNT - 1;
	rd = &sc->alc_rdata;
	bzero(rd->alc_rx_ring, ALC_RX_RING_SZ);
	for (i = 0; i < ALC_RX_RING_CNT; i++) {
		rxd = &sc->alc_cdata.alc_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_desc = &rd->alc_rx_ring[i];
		if (alc_newbuf(sc, rxd) != 0)
			return (ENOBUFS);
	}

	/*
	 * Since controller does not update Rx descriptors, driver
	 * does have to read Rx descriptors back so BUS_DMASYNC_PREWRITE
	 * is enough to ensure coherence.
	 */
	bus_dmamap_sync(sc->sc_dmat, sc->alc_cdata.alc_rx_ring_map, 0,
	    sc->alc_cdata.alc_rx_ring_map->dm_mapsize, BUS_DMASYNC_PREWRITE);
	/* Let controller know availability of new Rx buffers. */
	CSR_WRITE_4(sc, ALC_MBOX_RD0_PROD_IDX, sc->alc_cdata.alc_rx_cons);

	return (0);
}

void
alc_init_rr_ring(struct alc_softc *sc)
{
	struct alc_ring_data *rd;

	sc->alc_cdata.alc_rr_cons = 0;
	ALC_RXCHAIN_RESET(sc);

	rd = &sc->alc_rdata;
	bzero(rd->alc_rr_ring, ALC_RR_RING_SZ);
	bus_dmamap_sync(sc->sc_dmat, sc->alc_cdata.alc_rr_ring_map, 0,
	    sc->alc_cdata.alc_rr_ring_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

void
alc_init_cmb(struct alc_softc *sc)
{
	struct alc_ring_data *rd;

	rd = &sc->alc_rdata;
	bzero(rd->alc_cmb, ALC_CMB_SZ);
	bus_dmamap_sync(sc->sc_dmat, sc->alc_cdata.alc_cmb_map, 0,
	    sc->alc_cdata.alc_cmb_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

void
alc_init_smb(struct alc_softc *sc)
{
	struct alc_ring_data *rd;

	rd = &sc->alc_rdata;
	bzero(rd->alc_smb, ALC_SMB_SZ);
	bus_dmamap_sync(sc->sc_dmat, sc->alc_cdata.alc_smb_map, 0,
	    sc->alc_cdata.alc_smb_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

void
alc_rxvlan(struct alc_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint32_t reg;

	reg = CSR_READ_4(sc, ALC_MAC_CFG);
	if (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING)
		reg |= MAC_CFG_VLAN_TAG_STRIP;
	else
		reg &= ~MAC_CFG_VLAN_TAG_STRIP;
	CSR_WRITE_4(sc, ALC_MAC_CFG, reg);
}

void
alc_iff(struct alc_softc *sc)
{
	struct arpcom *ac = &sc->sc_arpcom;
	struct ifnet *ifp = &ac->ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint32_t crc;
	uint32_t mchash[2];
	uint32_t rxcfg;

	rxcfg = CSR_READ_4(sc, ALC_MAC_CFG);
	rxcfg &= ~(MAC_CFG_ALLMULTI | MAC_CFG_BCAST | MAC_CFG_PROMISC);
	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Always accept broadcast frames.
	 */
	rxcfg |= MAC_CFG_BCAST;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			rxcfg |= MAC_CFG_PROMISC;
		else
			rxcfg |= MAC_CFG_ALLMULTI;
		mchash[0] = mchash[1] = 0xFFFFFFFF;
	} else {
		/* Program new filter. */
		bzero(mchash, sizeof(mchash));

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			crc = ether_crc32_be(enm->enm_addrlo, ETHER_ADDR_LEN);

			mchash[crc >> 31] |= 1 << ((crc >> 26) & 0x1f);

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	CSR_WRITE_4(sc, ALC_MAR0, mchash[0]);
	CSR_WRITE_4(sc, ALC_MAR1, mchash[1]);
	CSR_WRITE_4(sc, ALC_MAC_CFG, rxcfg);
}
