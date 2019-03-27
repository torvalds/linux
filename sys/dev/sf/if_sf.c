/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Adaptec AIC-6915 "Starfire" PCI fast ethernet driver for FreeBSD.
 * Programming manual is available from:
 * http://download.adaptec.com/pdfs/user_guides/aic6915_pg.pdf.
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Department of Electical Engineering
 * Columbia University, New York City
 */
/*
 * The Adaptec AIC-6915 "Starfire" is a 64-bit 10/100 PCI ethernet
 * controller designed with flexibility and reducing CPU load in mind.
 * The Starfire offers high and low priority buffer queues, a
 * producer/consumer index mechanism and several different buffer
 * queue and completion queue descriptor types. Any one of a number
 * of different driver designs can be used, depending on system and
 * OS requirements. This driver makes use of type2 transmit frame
 * descriptors to take full advantage of fragmented packets buffers
 * and two RX buffer queues prioritized on size (one queue for small
 * frames that will fit into a single mbuf, another with full size
 * mbuf clusters for everything else). The producer/consumer indexes
 * and completion queues are also used.
 *
 * One downside to the Starfire has to do with alignment: buffer
 * queues must be aligned on 256-byte boundaries, and receive buffers
 * must be aligned on longword boundaries. The receive buffer alignment
 * causes problems on the strict alignment architecture, where the
 * packet payload should be longword aligned. There is no simple way
 * around this.
 *
 * For receive filtering, the Starfire offers 16 perfect filter slots
 * and a 512-bit hash table.
 *
 * The Starfire has no internal transceiver, relying instead on an
 * external MII-based transceiver. Accessing registers on external
 * PHYs is done through a special register map rather than with the
 * usual bitbang MDIO method.
 *
 * Acesssing the registers on the Starfire is a little tricky. The
 * Starfire has a 512K internal register space. When programmed for
 * PCI memory mapped mode, the entire register space can be accessed
 * directly. However in I/O space mode, only 256 bytes are directly
 * mapped into PCI I/O space. The other registers can be accessed
 * indirectly using the SF_INDIRECTIO_ADDR and SF_INDIRECTIO_DATA
 * registers inside the 256-byte I/O window.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_device_polling.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/rman.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>

#include <dev/sf/if_sfreg.h>
#include <dev/sf/starfire_rx.h>
#include <dev/sf/starfire_tx.h>

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

MODULE_DEPEND(sf, pci, 1, 1, 1);
MODULE_DEPEND(sf, ether, 1, 1, 1);
MODULE_DEPEND(sf, miibus, 1, 1, 1);

#undef	SF_GFP_DEBUG
#define	SF_CSUM_FEATURES	(CSUM_TCP | CSUM_UDP)
/* Define this to activate partial TCP/UDP checksum offload. */
#undef	SF_PARTIAL_CSUM_SUPPORT

static struct sf_type sf_devs[] = {
	{ AD_VENDORID, AD_DEVICEID_STARFIRE, "Adaptec AIC-6915 10/100BaseTX",
	    AD_SUBSYSID_62011_REV0, "Adaptec ANA-62011 (rev 0) 10/100BaseTX" },
	{ AD_VENDORID, AD_DEVICEID_STARFIRE, "Adaptec AIC-6915 10/100BaseTX",
	    AD_SUBSYSID_62011_REV1, "Adaptec ANA-62011 (rev 1) 10/100BaseTX" },
	{ AD_VENDORID, AD_DEVICEID_STARFIRE, "Adaptec AIC-6915 10/100BaseTX",
	    AD_SUBSYSID_62022, "Adaptec ANA-62022 10/100BaseTX" },
	{ AD_VENDORID, AD_DEVICEID_STARFIRE, "Adaptec AIC-6915 10/100BaseTX",
	    AD_SUBSYSID_62044_REV0, "Adaptec ANA-62044 (rev 0) 10/100BaseTX" },
	{ AD_VENDORID, AD_DEVICEID_STARFIRE, "Adaptec AIC-6915 10/100BaseTX",
	    AD_SUBSYSID_62044_REV1, "Adaptec ANA-62044 (rev 1) 10/100BaseTX" },
	{ AD_VENDORID, AD_DEVICEID_STARFIRE, "Adaptec AIC-6915 10/100BaseTX",
	    AD_SUBSYSID_62020, "Adaptec ANA-62020 10/100BaseFX" },
	{ AD_VENDORID, AD_DEVICEID_STARFIRE, "Adaptec AIC-6915 10/100BaseTX",
	    AD_SUBSYSID_69011, "Adaptec ANA-69011 10/100BaseTX" },
};

static int sf_probe(device_t);
static int sf_attach(device_t);
static int sf_detach(device_t);
static int sf_shutdown(device_t);
static int sf_suspend(device_t);
static int sf_resume(device_t);
static void sf_intr(void *);
static void sf_tick(void *);
static void sf_stats_update(struct sf_softc *);
#ifndef __NO_STRICT_ALIGNMENT
static __inline void sf_fixup_rx(struct mbuf *);
#endif
static int sf_rxeof(struct sf_softc *);
static void sf_txeof(struct sf_softc *);
static int sf_encap(struct sf_softc *, struct mbuf **);
static void sf_start(struct ifnet *);
static void sf_start_locked(struct ifnet *);
static int sf_ioctl(struct ifnet *, u_long, caddr_t);
static void sf_download_fw(struct sf_softc *);
static void sf_init(void *);
static void sf_init_locked(struct sf_softc *);
static void sf_stop(struct sf_softc *);
static void sf_watchdog(struct sf_softc *);
static int sf_ifmedia_upd(struct ifnet *);
static int sf_ifmedia_upd_locked(struct ifnet *);
static void sf_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static void sf_reset(struct sf_softc *);
static int sf_dma_alloc(struct sf_softc *);
static void sf_dma_free(struct sf_softc *);
static int sf_init_rx_ring(struct sf_softc *);
static void sf_init_tx_ring(struct sf_softc *);
static int sf_newbuf(struct sf_softc *, int);
static void sf_rxfilter(struct sf_softc *);
static int sf_setperf(struct sf_softc *, int, uint8_t *);
static int sf_sethash(struct sf_softc *, caddr_t, int);
#ifdef notdef
static int sf_setvlan(struct sf_softc *, int, uint32_t);
#endif

static uint8_t sf_read_eeprom(struct sf_softc *, int);

static int sf_miibus_readreg(device_t, int, int);
static int sf_miibus_writereg(device_t, int, int, int);
static void sf_miibus_statchg(device_t);
#ifdef DEVICE_POLLING
static int sf_poll(struct ifnet *ifp, enum poll_cmd cmd, int count);
#endif

static uint32_t csr_read_4(struct sf_softc *, int);
static void csr_write_4(struct sf_softc *, int, uint32_t);
static void sf_txthresh_adjust(struct sf_softc *);
static int sf_sysctl_stats(SYSCTL_HANDLER_ARGS);
static int sysctl_int_range(SYSCTL_HANDLER_ARGS, int, int);
static int sysctl_hw_sf_int_mod(SYSCTL_HANDLER_ARGS);

static device_method_t sf_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sf_probe),
	DEVMETHOD(device_attach,	sf_attach),
	DEVMETHOD(device_detach,	sf_detach),
	DEVMETHOD(device_shutdown,	sf_shutdown),
	DEVMETHOD(device_suspend,	sf_suspend),
	DEVMETHOD(device_resume,	sf_resume),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	sf_miibus_readreg),
	DEVMETHOD(miibus_writereg,	sf_miibus_writereg),
	DEVMETHOD(miibus_statchg,	sf_miibus_statchg),

	DEVMETHOD_END
};

static driver_t sf_driver = {
	"sf",
	sf_methods,
	sizeof(struct sf_softc),
};

static devclass_t sf_devclass;

DRIVER_MODULE(sf, pci, sf_driver, sf_devclass, 0, 0);
DRIVER_MODULE(miibus, sf, miibus_driver, miibus_devclass, 0, 0);

#define SF_SETBIT(sc, reg, x)	\
	csr_write_4(sc, reg, csr_read_4(sc, reg) | (x))

#define SF_CLRBIT(sc, reg, x)				\
	csr_write_4(sc, reg, csr_read_4(sc, reg) & ~(x))

static uint32_t
csr_read_4(struct sf_softc *sc, int reg)
{
	uint32_t		val;

	if (sc->sf_restype == SYS_RES_MEMORY)
		val = CSR_READ_4(sc, (reg + SF_RMAP_INTREG_BASE));
	else {
		CSR_WRITE_4(sc, SF_INDIRECTIO_ADDR, reg + SF_RMAP_INTREG_BASE);
		val = CSR_READ_4(sc, SF_INDIRECTIO_DATA);
	}

	return (val);
}

static uint8_t
sf_read_eeprom(struct sf_softc *sc, int reg)
{
	uint8_t		val;

	val = (csr_read_4(sc, SF_EEADDR_BASE +
	    (reg & 0xFFFFFFFC)) >> (8 * (reg & 3))) & 0xFF;

	return (val);
}

static void
csr_write_4(struct sf_softc *sc, int reg, uint32_t val)
{

	if (sc->sf_restype == SYS_RES_MEMORY)
		CSR_WRITE_4(sc, (reg + SF_RMAP_INTREG_BASE), val);
	else {
		CSR_WRITE_4(sc, SF_INDIRECTIO_ADDR, reg + SF_RMAP_INTREG_BASE);
		CSR_WRITE_4(sc, SF_INDIRECTIO_DATA, val);
	}
}

/*
 * Copy the address 'mac' into the perfect RX filter entry at
 * offset 'idx.' The perfect filter only has 16 entries so do
 * some sanity tests.
 */
static int
sf_setperf(struct sf_softc *sc, int idx, uint8_t *mac)
{

	if (idx < 0 || idx > SF_RXFILT_PERFECT_CNT)
		return (EINVAL);

	if (mac == NULL)
		return (EINVAL);

	csr_write_4(sc, SF_RXFILT_PERFECT_BASE +
	    (idx * SF_RXFILT_PERFECT_SKIP) + 0, mac[5] | (mac[4] << 8));
	csr_write_4(sc, SF_RXFILT_PERFECT_BASE +
	    (idx * SF_RXFILT_PERFECT_SKIP) + 4, mac[3] | (mac[2] << 8));
	csr_write_4(sc, SF_RXFILT_PERFECT_BASE +
	    (idx * SF_RXFILT_PERFECT_SKIP) + 8, mac[1] | (mac[0] << 8));

	return (0);
}

/*
 * Set the bit in the 512-bit hash table that corresponds to the
 * specified mac address 'mac.' If 'prio' is nonzero, update the
 * priority hash table instead of the filter hash table.
 */
static int
sf_sethash(struct sf_softc *sc, caddr_t	mac, int prio)
{
	uint32_t		h;

	if (mac == NULL)
		return (EINVAL);

	h = ether_crc32_be(mac, ETHER_ADDR_LEN) >> 23;

	if (prio) {
		SF_SETBIT(sc, SF_RXFILT_HASH_BASE + SF_RXFILT_HASH_PRIOOFF +
		    (SF_RXFILT_HASH_SKIP * (h >> 4)), (1 << (h & 0xF)));
	} else {
		SF_SETBIT(sc, SF_RXFILT_HASH_BASE + SF_RXFILT_HASH_ADDROFF +
		    (SF_RXFILT_HASH_SKIP * (h >> 4)), (1 << (h & 0xF)));
	}

	return (0);
}

#ifdef notdef
/*
 * Set a VLAN tag in the receive filter.
 */
static int
sf_setvlan(struct sf_softc *sc, int idx, uint32_t vlan)
{

	if (idx < 0 || idx >> SF_RXFILT_HASH_CNT)
		return (EINVAL);

	csr_write_4(sc, SF_RXFILT_HASH_BASE +
	    (idx * SF_RXFILT_HASH_SKIP) + SF_RXFILT_HASH_VLANOFF, vlan);

	return (0);
}
#endif

static int
sf_miibus_readreg(device_t dev, int phy, int reg)
{
	struct sf_softc		*sc;
	int			i;
	uint32_t		val = 0;

	sc = device_get_softc(dev);

	for (i = 0; i < SF_TIMEOUT; i++) {
		val = csr_read_4(sc, SF_PHY_REG(phy, reg));
		if ((val & SF_MII_DATAVALID) != 0)
			break;
	}

	if (i == SF_TIMEOUT)
		return (0);

	val &= SF_MII_DATAPORT;
	if (val == 0xffff)
		return (0);

	return (val);
}

static int
sf_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct sf_softc		*sc;
	int			i;
	int			busy;

	sc = device_get_softc(dev);

	csr_write_4(sc, SF_PHY_REG(phy, reg), val);

	for (i = 0; i < SF_TIMEOUT; i++) {
		busy = csr_read_4(sc, SF_PHY_REG(phy, reg));
		if ((busy & SF_MII_BUSY) == 0)
			break;
	}

	return (0);
}

static void
sf_miibus_statchg(device_t dev)
{
	struct sf_softc		*sc;
	struct mii_data		*mii;
	struct ifnet		*ifp;
	uint32_t		val;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->sf_miibus);
	ifp = sc->sf_ifp;
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	sc->sf_link = 0;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
		case IFM_100_FX:
			sc->sf_link = 1;
			break;
		}
	}
	if (sc->sf_link == 0)
		return;

	val = csr_read_4(sc, SF_MACCFG_1);
	val &= ~SF_MACCFG1_FULLDUPLEX;
	val &= ~(SF_MACCFG1_RX_FLOWENB | SF_MACCFG1_TX_FLOWENB);
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		val |= SF_MACCFG1_FULLDUPLEX;
		csr_write_4(sc, SF_BKTOBKIPG, SF_IPGT_FDX);
#ifdef notyet
		/* Configure flow-control bits. */
		if ((IFM_OPTIONS(sc->sc_mii->mii_media_active) &
		    IFM_ETH_RXPAUSE) != 0)
			val |= SF_MACCFG1_RX_FLOWENB;
		if ((IFM_OPTIONS(sc->sc_mii->mii_media_active) &
		    IFM_ETH_TXPAUSE) != 0)
			val |= SF_MACCFG1_TX_FLOWENB;
#endif
	} else
		csr_write_4(sc, SF_BKTOBKIPG, SF_IPGT_HDX);

	/* Make sure to reset MAC to take changes effect. */
	csr_write_4(sc, SF_MACCFG_1, val | SF_MACCFG1_SOFTRESET);
	DELAY(1000);
	csr_write_4(sc, SF_MACCFG_1, val);

	val = csr_read_4(sc, SF_TIMER_CTL);
	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_100_TX)
		val |= SF_TIMER_TIMES_TEN;
	else
		val &= ~SF_TIMER_TIMES_TEN;
	csr_write_4(sc, SF_TIMER_CTL, val);
}

static void
sf_rxfilter(struct sf_softc *sc)
{
	struct ifnet		*ifp;
	int			i;
	struct ifmultiaddr	*ifma;
	uint8_t			dummy[ETHER_ADDR_LEN] = { 0, 0, 0, 0, 0, 0 };
	uint32_t		rxfilt;

	ifp = sc->sf_ifp;

	/* First zot all the existing filters. */
	for (i = 1; i < SF_RXFILT_PERFECT_CNT; i++)
		sf_setperf(sc, i, dummy);
	for (i = SF_RXFILT_HASH_BASE; i < (SF_RXFILT_HASH_MAX + 1);
	    i += sizeof(uint32_t))
		csr_write_4(sc, i, 0);

	rxfilt = csr_read_4(sc, SF_RXFILT);
	rxfilt &= ~(SF_RXFILT_PROMISC | SF_RXFILT_ALLMULTI | SF_RXFILT_BROAD);
	if ((ifp->if_flags & IFF_BROADCAST) != 0)
		rxfilt |= SF_RXFILT_BROAD;
	if ((ifp->if_flags & IFF_ALLMULTI) != 0 ||
	    (ifp->if_flags & IFF_PROMISC) != 0) {
		if ((ifp->if_flags & IFF_PROMISC) != 0)
			rxfilt |= SF_RXFILT_PROMISC;
		if ((ifp->if_flags & IFF_ALLMULTI) != 0)
			rxfilt |= SF_RXFILT_ALLMULTI;
		goto done;
	}

	/* Now program new ones. */
	i = 1;
	/* XXX how do we maintain reverse semantics without impl */
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs,
	    ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		/*
		 * Program the first 15 multicast groups
		 * into the perfect filter. For all others,
		 * use the hash table.
		 */
		if (i < SF_RXFILT_PERFECT_CNT) {
			sf_setperf(sc, i,
			    LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
			i++;
			continue;
		}

		sf_sethash(sc,
		    LLADDR((struct sockaddr_dl *)ifma->ifma_addr), 0);
	}
	if_maddr_runlock(ifp);

done:
	csr_write_4(sc, SF_RXFILT, rxfilt);
}

/*
 * Set media options.
 */
static int
sf_ifmedia_upd(struct ifnet *ifp)
{
	struct sf_softc		*sc;
	int			error;

	sc = ifp->if_softc;
	SF_LOCK(sc);
	error = sf_ifmedia_upd_locked(ifp);
	SF_UNLOCK(sc);
	return (error);
}

static int
sf_ifmedia_upd_locked(struct ifnet *ifp)
{
	struct sf_softc		*sc;
	struct mii_data		*mii;
	struct mii_softc        *miisc;

	sc = ifp->if_softc;
	mii = device_get_softc(sc->sf_miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	return (mii_mediachg(mii));
}

/*
 * Report current media status.
 */
static void
sf_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct sf_softc		*sc;
	struct mii_data		*mii;

	sc = ifp->if_softc;
	SF_LOCK(sc);
	if ((ifp->if_flags & IFF_UP) == 0) {
		SF_UNLOCK(sc);
		return;
	}

	mii = device_get_softc(sc->sf_miibus);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	SF_UNLOCK(sc);
}

static int
sf_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct sf_softc		*sc;
	struct ifreq		*ifr;
	struct mii_data		*mii;
	int			error, mask;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;
	error = 0;

	switch (command) {
	case SIOCSIFFLAGS:
		SF_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				if ((ifp->if_flags ^ sc->sf_if_flags) &
				    (IFF_PROMISC | IFF_ALLMULTI))
					sf_rxfilter(sc);
			} else {
				if (sc->sf_detach == 0)
					sf_init_locked(sc);
			}
		} else {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
				sf_stop(sc);
		}
		sc->sf_if_flags = ifp->if_flags;
		SF_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		SF_LOCK(sc);
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			sf_rxfilter(sc);
		SF_UNLOCK(sc);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc->sf_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	case SIOCSIFCAP:
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
#ifdef DEVICE_POLLING
		if ((mask & IFCAP_POLLING) != 0) {
			if ((ifr->ifr_reqcap & IFCAP_POLLING) != 0) {
				error = ether_poll_register(sf_poll, ifp);
				if (error != 0)
					break;
				SF_LOCK(sc);
				/* Disable interrupts. */
				csr_write_4(sc, SF_IMR, 0);
				ifp->if_capenable |= IFCAP_POLLING;
				SF_UNLOCK(sc);
			} else {
				error = ether_poll_deregister(ifp);
				/* Enable interrupts. */
				SF_LOCK(sc);
				csr_write_4(sc, SF_IMR, SF_INTRS);
				ifp->if_capenable &= ~IFCAP_POLLING;
				SF_UNLOCK(sc);
			}
		}
#endif /* DEVICE_POLLING */
		if ((mask & IFCAP_TXCSUM) != 0) {
			if ((IFCAP_TXCSUM & ifp->if_capabilities) != 0) {
				SF_LOCK(sc);
				ifp->if_capenable ^= IFCAP_TXCSUM;
				if ((IFCAP_TXCSUM & ifp->if_capenable) != 0) {
					ifp->if_hwassist |= SF_CSUM_FEATURES;
					SF_SETBIT(sc, SF_GEN_ETH_CTL,
					    SF_ETHCTL_TXGFP_ENB);
				} else {
					ifp->if_hwassist &= ~SF_CSUM_FEATURES;
					SF_CLRBIT(sc, SF_GEN_ETH_CTL,
					    SF_ETHCTL_TXGFP_ENB);
				}
				SF_UNLOCK(sc);
			}
		}
		if ((mask & IFCAP_RXCSUM) != 0) {
			if ((IFCAP_RXCSUM & ifp->if_capabilities) != 0) {
				SF_LOCK(sc);
				ifp->if_capenable ^= IFCAP_RXCSUM;
				if ((IFCAP_RXCSUM & ifp->if_capenable) != 0)
					SF_SETBIT(sc, SF_GEN_ETH_CTL,
					    SF_ETHCTL_RXGFP_ENB);
				else
					SF_CLRBIT(sc, SF_GEN_ETH_CTL,
					    SF_ETHCTL_RXGFP_ENB);
				SF_UNLOCK(sc);
			}
		}
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static void
sf_reset(struct sf_softc *sc)
{
	int		i;

	csr_write_4(sc, SF_GEN_ETH_CTL, 0);
	SF_SETBIT(sc, SF_MACCFG_1, SF_MACCFG1_SOFTRESET);
	DELAY(1000);
	SF_CLRBIT(sc, SF_MACCFG_1, SF_MACCFG1_SOFTRESET);

	SF_SETBIT(sc, SF_PCI_DEVCFG, SF_PCIDEVCFG_RESET);

	for (i = 0; i < SF_TIMEOUT; i++) {
		DELAY(10);
		if (!(csr_read_4(sc, SF_PCI_DEVCFG) & SF_PCIDEVCFG_RESET))
			break;
	}

	if (i == SF_TIMEOUT)
		device_printf(sc->sf_dev, "reset never completed!\n");

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);
}

/*
 * Probe for an Adaptec AIC-6915 chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 * We also check the subsystem ID so that we can identify exactly which
 * NIC has been found, if possible.
 */
static int
sf_probe(device_t dev)
{
	struct sf_type		*t;
	uint16_t		vid;
	uint16_t		did;
	uint16_t		sdid;
	int			i;

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);
	sdid = pci_get_subdevice(dev);

	t = sf_devs;
	for (i = 0; i < nitems(sf_devs); i++, t++) {
		if (vid == t->sf_vid && did == t->sf_did) {
			if (sdid == t->sf_sdid) {
				device_set_desc(dev, t->sf_sname);
				return (BUS_PROBE_DEFAULT);
			}
		}
	}

	if (vid == AD_VENDORID && did == AD_DEVICEID_STARFIRE) {
		/* unknown subdevice */
		device_set_desc(dev, sf_devs[0].sf_name);
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
sf_attach(device_t dev)
{
	int			i;
	struct sf_softc		*sc;
	struct ifnet		*ifp;
	uint32_t		reg;
	int			rid, error = 0;
	uint8_t			eaddr[ETHER_ADDR_LEN];

	sc = device_get_softc(dev);
	sc->sf_dev = dev;

	mtx_init(&sc->sf_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->sf_co, &sc->sf_mtx, 0);

	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

	/*
	 * Prefer memory space register mapping over I/O space as the
	 * hardware requires lots of register access to get various
	 * producer/consumer index during Tx/Rx operation. However this
	 * requires large memory space(512K) to map the entire register
	 * space.
	 */
	sc->sf_rid = PCIR_BAR(0);
	sc->sf_restype = SYS_RES_MEMORY;
	sc->sf_res = bus_alloc_resource_any(dev, sc->sf_restype, &sc->sf_rid,
	    RF_ACTIVE);
	if (sc->sf_res == NULL) {
		reg = pci_read_config(dev, PCIR_BAR(0), 4);
		if ((reg & PCIM_BAR_MEM_64) == PCIM_BAR_MEM_64)
			sc->sf_rid = PCIR_BAR(2);
		else
			sc->sf_rid = PCIR_BAR(1);
		sc->sf_restype = SYS_RES_IOPORT;
		sc->sf_res = bus_alloc_resource_any(dev, sc->sf_restype,
		    &sc->sf_rid, RF_ACTIVE);
		if (sc->sf_res == NULL) {
			device_printf(dev, "couldn't allocate resources\n");
			mtx_destroy(&sc->sf_mtx);
			return (ENXIO);
		}
	}
	if (bootverbose)
		device_printf(dev, "using %s space register mapping\n",
		    sc->sf_restype == SYS_RES_MEMORY ? "memory" : "I/O");

	reg = pci_read_config(dev, PCIR_CACHELNSZ, 1);
	if (reg == 0) {
		/*
		 * If cache line size is 0, MWI is not used at all, so set
		 * reasonable default. AIC-6915 supports 0, 4, 8, 16, 32
		 * and 64.
		 */
		reg = 16;
		device_printf(dev, "setting PCI cache line size to %u\n", reg);
		pci_write_config(dev, PCIR_CACHELNSZ, reg, 1);
	} else {
		if (bootverbose)
			device_printf(dev, "PCI cache line size : %u\n", reg);
	}
	/* Enable MWI. */
	reg = pci_read_config(dev, PCIR_COMMAND, 2);
	reg |= PCIM_CMD_MWRICEN;
	pci_write_config(dev, PCIR_COMMAND, reg, 2);

	/* Allocate interrupt. */
	rid = 0;
	sc->sf_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->sf_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "stats", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
	    sf_sysctl_stats, "I", "Statistics");

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
		SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		OID_AUTO, "int_mod", CTLTYPE_INT | CTLFLAG_RW,
		&sc->sf_int_mod, 0, sysctl_hw_sf_int_mod, "I",
		"sf interrupt moderation");
	/* Pull in device tunables. */
	sc->sf_int_mod = SF_IM_DEFAULT;
	error = resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "int_mod", &sc->sf_int_mod);
	if (error == 0) {
		if (sc->sf_int_mod < SF_IM_MIN ||
		    sc->sf_int_mod > SF_IM_MAX) {
			device_printf(dev, "int_mod value out of range; "
			    "using default: %d\n", SF_IM_DEFAULT);
			sc->sf_int_mod = SF_IM_DEFAULT;
		}
	}

	/* Reset the adapter. */
	sf_reset(sc);

	/*
	 * Get station address from the EEPROM.
	 */
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		eaddr[i] =
		    sf_read_eeprom(sc, SF_EE_NODEADDR + ETHER_ADDR_LEN - i);

	/* Allocate DMA resources. */
	if (sf_dma_alloc(sc) != 0) {
		error = ENOSPC;
		goto fail;
	}

	sc->sf_txthresh = SF_MIN_TX_THRESHOLD;

	ifp = sc->sf_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not allocate ifnet structure\n");
		error = ENOSPC;
		goto fail;
	}

	/* Do MII setup. */
	error = mii_attach(dev, &sc->sf_miibus, ifp, sf_ifmedia_upd,
	    sf_ifmedia_sts, BMSR_DEFCAPMASK, MII_PHY_ANY, MII_OFFSET_ANY, 0);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = sf_ioctl;
	ifp->if_start = sf_start;
	ifp->if_init = sf_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, SF_TX_DLIST_CNT - 1);
	ifp->if_snd.ifq_drv_maxlen = SF_TX_DLIST_CNT - 1;
	IFQ_SET_READY(&ifp->if_snd);
	/*
	 * With the help of firmware, AIC-6915 supports
	 * Tx/Rx TCP/UDP checksum offload.
	 */
	ifp->if_hwassist = SF_CSUM_FEATURES;
	ifp->if_capabilities = IFCAP_HWCSUM;

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, eaddr);

	/* VLAN capability setup. */
	ifp->if_capabilities |= IFCAP_VLAN_MTU;
	ifp->if_capenable = ifp->if_capabilities;
#ifdef DEVICE_POLLING
	ifp->if_capabilities |= IFCAP_POLLING;
#endif
	/*
	 * Tell the upper layer(s) we support long frames.
	 * Must appear after the call to ether_ifattach() because
	 * ether_ifattach() sets ifi_hdrlen to the default value.
	 */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	/* Hook interrupt last to avoid having to lock softc */
	error = bus_setup_intr(dev, sc->sf_irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, sf_intr, sc, &sc->sf_intrhand);

	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
		goto fail;
	}

	gone_by_fcp101_dev(dev);

fail:
	if (error)
		sf_detach(dev);

	return (error);
}

/*
 * Shutdown hardware and free up resources. This can be called any
 * time after the mutex has been initialized. It is called in both
 * the error case in attach and the normal detach case so it needs
 * to be careful about only freeing resources that have actually been
 * allocated.
 */
static int
sf_detach(device_t dev)
{
	struct sf_softc		*sc;
	struct ifnet		*ifp;

	sc = device_get_softc(dev);
	ifp = sc->sf_ifp;

#ifdef DEVICE_POLLING
	if (ifp != NULL && ifp->if_capenable & IFCAP_POLLING)
		ether_poll_deregister(ifp);
#endif

	/* These should only be active if attach succeeded */
	if (device_is_attached(dev)) {
		SF_LOCK(sc);
		sc->sf_detach = 1;
		sf_stop(sc);
		SF_UNLOCK(sc);
		callout_drain(&sc->sf_co);
		if (ifp != NULL)
			ether_ifdetach(ifp);
	}
	if (sc->sf_miibus) {
		device_delete_child(dev, sc->sf_miibus);
		sc->sf_miibus = NULL;
	}
	bus_generic_detach(dev);

	if (sc->sf_intrhand != NULL)
		bus_teardown_intr(dev, sc->sf_irq, sc->sf_intrhand);
	if (sc->sf_irq != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sf_irq);
	if (sc->sf_res != NULL)
		bus_release_resource(dev, sc->sf_restype, sc->sf_rid,
		    sc->sf_res);

	sf_dma_free(sc);
	if (ifp != NULL)
		if_free(ifp);

	mtx_destroy(&sc->sf_mtx);

	return (0);
}

struct sf_dmamap_arg {
	bus_addr_t		sf_busaddr;
};

static void
sf_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct sf_dmamap_arg	*ctx;

	if (error != 0)
		return;
	ctx = arg;
	ctx->sf_busaddr = segs[0].ds_addr;
}

static int
sf_dma_alloc(struct sf_softc *sc)
{
	struct sf_dmamap_arg	ctx;
	struct sf_txdesc	*txd;
	struct sf_rxdesc	*rxd;
	bus_addr_t		lowaddr;
	bus_addr_t		rx_ring_end, rx_cring_end;
	bus_addr_t		tx_ring_end, tx_cring_end;
	int			error, i;

	lowaddr = BUS_SPACE_MAXADDR;

again:
	/* Create parent DMA tag. */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->sf_dev),	/* parent */
	    1, 0,			/* alignment, boundary */
	    lowaddr,			/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    0,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->sf_cdata.sf_parent_tag);
	if (error != 0) {
		device_printf(sc->sf_dev, "failed to create parent DMA tag\n");
		goto fail;
	}
	/* Create tag for Tx ring. */
	error = bus_dma_tag_create(sc->sf_cdata.sf_parent_tag,/* parent */
	    SF_RING_ALIGN, 0, 		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    SF_TX_DLIST_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    SF_TX_DLIST_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->sf_cdata.sf_tx_ring_tag);
	if (error != 0) {
		device_printf(sc->sf_dev, "failed to create Tx ring DMA tag\n");
		goto fail;
	}

	/* Create tag for Tx completion ring. */
	error = bus_dma_tag_create(sc->sf_cdata.sf_parent_tag,/* parent */
	    SF_RING_ALIGN, 0, 		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    SF_TX_CLIST_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    SF_TX_CLIST_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->sf_cdata.sf_tx_cring_tag);
	if (error != 0) {
		device_printf(sc->sf_dev,
		    "failed to create Tx completion ring DMA tag\n");
		goto fail;
	}

	/* Create tag for Rx ring. */
	error = bus_dma_tag_create(sc->sf_cdata.sf_parent_tag,/* parent */
	    SF_RING_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    SF_RX_DLIST_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    SF_RX_DLIST_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->sf_cdata.sf_rx_ring_tag);
	if (error != 0) {
		device_printf(sc->sf_dev,
		    "failed to create Rx ring DMA tag\n");
		goto fail;
	}

	/* Create tag for Rx completion ring. */
	error = bus_dma_tag_create(sc->sf_cdata.sf_parent_tag,/* parent */
	    SF_RING_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    SF_RX_CLIST_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    SF_RX_CLIST_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->sf_cdata.sf_rx_cring_tag);
	if (error != 0) {
		device_printf(sc->sf_dev,
		    "failed to create Rx completion ring DMA tag\n");
		goto fail;
	}

	/* Create tag for Tx buffers. */
	error = bus_dma_tag_create(sc->sf_cdata.sf_parent_tag,/* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES * SF_MAXTXSEGS,	/* maxsize */
	    SF_MAXTXSEGS,		/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->sf_cdata.sf_tx_tag);
	if (error != 0) {
		device_printf(sc->sf_dev, "failed to create Tx DMA tag\n");
		goto fail;
	}

	/* Create tag for Rx buffers. */
	error = bus_dma_tag_create(sc->sf_cdata.sf_parent_tag,/* parent */
	    SF_RX_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES,			/* maxsize */
	    1,				/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->sf_cdata.sf_rx_tag);
	if (error != 0) {
		device_printf(sc->sf_dev, "failed to create Rx DMA tag\n");
		goto fail;
	}

	/* Allocate DMA'able memory and load the DMA map for Tx ring. */
	error = bus_dmamem_alloc(sc->sf_cdata.sf_tx_ring_tag,
	    (void **)&sc->sf_rdata.sf_tx_ring, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->sf_cdata.sf_tx_ring_map);
	if (error != 0) {
		device_printf(sc->sf_dev,
		    "failed to allocate DMA'able memory for Tx ring\n");
		goto fail;
	}

	ctx.sf_busaddr = 0;
	error = bus_dmamap_load(sc->sf_cdata.sf_tx_ring_tag,
	    sc->sf_cdata.sf_tx_ring_map, sc->sf_rdata.sf_tx_ring,
	    SF_TX_DLIST_SIZE, sf_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.sf_busaddr == 0) {
		device_printf(sc->sf_dev,
		    "failed to load DMA'able memory for Tx ring\n");
		goto fail;
	}
	sc->sf_rdata.sf_tx_ring_paddr = ctx.sf_busaddr;

	/*
	 * Allocate DMA'able memory and load the DMA map for Tx completion ring.
	 */
	error = bus_dmamem_alloc(sc->sf_cdata.sf_tx_cring_tag,
	    (void **)&sc->sf_rdata.sf_tx_cring, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->sf_cdata.sf_tx_cring_map);
	if (error != 0) {
		device_printf(sc->sf_dev,
		    "failed to allocate DMA'able memory for "
		    "Tx completion ring\n");
		goto fail;
	}

	ctx.sf_busaddr = 0;
	error = bus_dmamap_load(sc->sf_cdata.sf_tx_cring_tag,
	    sc->sf_cdata.sf_tx_cring_map, sc->sf_rdata.sf_tx_cring,
	    SF_TX_CLIST_SIZE, sf_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.sf_busaddr == 0) {
		device_printf(sc->sf_dev,
		    "failed to load DMA'able memory for Tx completion ring\n");
		goto fail;
	}
	sc->sf_rdata.sf_tx_cring_paddr = ctx.sf_busaddr;

	/* Allocate DMA'able memory and load the DMA map for Rx ring. */
	error = bus_dmamem_alloc(sc->sf_cdata.sf_rx_ring_tag,
	    (void **)&sc->sf_rdata.sf_rx_ring, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->sf_cdata.sf_rx_ring_map);
	if (error != 0) {
		device_printf(sc->sf_dev,
		    "failed to allocate DMA'able memory for Rx ring\n");
		goto fail;
	}

	ctx.sf_busaddr = 0;
	error = bus_dmamap_load(sc->sf_cdata.sf_rx_ring_tag,
	    sc->sf_cdata.sf_rx_ring_map, sc->sf_rdata.sf_rx_ring,
	    SF_RX_DLIST_SIZE, sf_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.sf_busaddr == 0) {
		device_printf(sc->sf_dev,
		    "failed to load DMA'able memory for Rx ring\n");
		goto fail;
	}
	sc->sf_rdata.sf_rx_ring_paddr = ctx.sf_busaddr;

	/*
	 * Allocate DMA'able memory and load the DMA map for Rx completion ring.
	 */
	error = bus_dmamem_alloc(sc->sf_cdata.sf_rx_cring_tag,
	    (void **)&sc->sf_rdata.sf_rx_cring, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->sf_cdata.sf_rx_cring_map);
	if (error != 0) {
		device_printf(sc->sf_dev,
		    "failed to allocate DMA'able memory for "
		    "Rx completion ring\n");
		goto fail;
	}

	ctx.sf_busaddr = 0;
	error = bus_dmamap_load(sc->sf_cdata.sf_rx_cring_tag,
	    sc->sf_cdata.sf_rx_cring_map, sc->sf_rdata.sf_rx_cring,
	    SF_RX_CLIST_SIZE, sf_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.sf_busaddr == 0) {
		device_printf(sc->sf_dev,
		    "failed to load DMA'able memory for Rx completion ring\n");
		goto fail;
	}
	sc->sf_rdata.sf_rx_cring_paddr = ctx.sf_busaddr;

	/*
	 * Tx desciptor ring and Tx completion ring should be addressed in
	 * the same 4GB space. The same rule applys to Rx ring and Rx
	 * completion ring. Unfortunately there is no way to specify this
	 * boundary restriction with bus_dma(9). So just try to allocate
	 * without the restriction and check the restriction was satisfied.
	 * If not, fall back to 32bit dma addressing mode which always
	 * guarantees the restriction.
	 */
	tx_ring_end = sc->sf_rdata.sf_tx_ring_paddr + SF_TX_DLIST_SIZE;
	tx_cring_end = sc->sf_rdata.sf_tx_cring_paddr + SF_TX_CLIST_SIZE;
	rx_ring_end = sc->sf_rdata.sf_rx_ring_paddr + SF_RX_DLIST_SIZE;
	rx_cring_end = sc->sf_rdata.sf_rx_cring_paddr + SF_RX_CLIST_SIZE;
	if ((SF_ADDR_HI(sc->sf_rdata.sf_tx_ring_paddr) !=
	    SF_ADDR_HI(tx_cring_end)) ||
	    (SF_ADDR_HI(sc->sf_rdata.sf_tx_cring_paddr) !=
	    SF_ADDR_HI(tx_ring_end)) ||
	    (SF_ADDR_HI(sc->sf_rdata.sf_rx_ring_paddr) !=
	    SF_ADDR_HI(rx_cring_end)) ||
	    (SF_ADDR_HI(sc->sf_rdata.sf_rx_cring_paddr) !=
	    SF_ADDR_HI(rx_ring_end))) {
		device_printf(sc->sf_dev,
		    "switching to 32bit DMA mode\n");
		sf_dma_free(sc);
		/* Limit DMA address space to 32bit and try again. */
		lowaddr = BUS_SPACE_MAXADDR_32BIT;
		goto again;
	}

	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < SF_TX_DLIST_CNT; i++) {
		txd = &sc->sf_cdata.sf_txdesc[i];
		txd->tx_m = NULL;
		txd->ndesc = 0;
		txd->tx_dmamap = NULL;
		error = bus_dmamap_create(sc->sf_cdata.sf_tx_tag, 0,
		    &txd->tx_dmamap);
		if (error != 0) {
			device_printf(sc->sf_dev,
			    "failed to create Tx dmamap\n");
			goto fail;
		}
	}
	/* Create DMA maps for Rx buffers. */
	if ((error = bus_dmamap_create(sc->sf_cdata.sf_rx_tag, 0,
	    &sc->sf_cdata.sf_rx_sparemap)) != 0) {
		device_printf(sc->sf_dev,
		    "failed to create spare Rx dmamap\n");
		goto fail;
	}
	for (i = 0; i < SF_RX_DLIST_CNT; i++) {
		rxd = &sc->sf_cdata.sf_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_dmamap = NULL;
		error = bus_dmamap_create(sc->sf_cdata.sf_rx_tag, 0,
		    &rxd->rx_dmamap);
		if (error != 0) {
			device_printf(sc->sf_dev,
			    "failed to create Rx dmamap\n");
			goto fail;
		}
	}

fail:
	return (error);
}

static void
sf_dma_free(struct sf_softc *sc)
{
	struct sf_txdesc	*txd;
	struct sf_rxdesc	*rxd;
	int			i;

	/* Tx ring. */
	if (sc->sf_cdata.sf_tx_ring_tag) {
		if (sc->sf_rdata.sf_tx_ring_paddr)
			bus_dmamap_unload(sc->sf_cdata.sf_tx_ring_tag,
			    sc->sf_cdata.sf_tx_ring_map);
		if (sc->sf_rdata.sf_tx_ring)
			bus_dmamem_free(sc->sf_cdata.sf_tx_ring_tag,
			    sc->sf_rdata.sf_tx_ring,
			    sc->sf_cdata.sf_tx_ring_map);
		sc->sf_rdata.sf_tx_ring = NULL;
		sc->sf_rdata.sf_tx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->sf_cdata.sf_tx_ring_tag);
		sc->sf_cdata.sf_tx_ring_tag = NULL;
	}
	/* Tx completion ring. */
	if (sc->sf_cdata.sf_tx_cring_tag) {
		if (sc->sf_rdata.sf_tx_cring_paddr)
			bus_dmamap_unload(sc->sf_cdata.sf_tx_cring_tag,
			    sc->sf_cdata.sf_tx_cring_map);
		if (sc->sf_rdata.sf_tx_cring)
			bus_dmamem_free(sc->sf_cdata.sf_tx_cring_tag,
			    sc->sf_rdata.sf_tx_cring,
			    sc->sf_cdata.sf_tx_cring_map);
		sc->sf_rdata.sf_tx_cring = NULL;
		sc->sf_rdata.sf_tx_cring_paddr = 0;
		bus_dma_tag_destroy(sc->sf_cdata.sf_tx_cring_tag);
		sc->sf_cdata.sf_tx_cring_tag = NULL;
	}
	/* Rx ring. */
	if (sc->sf_cdata.sf_rx_ring_tag) {
		if (sc->sf_rdata.sf_rx_ring_paddr)
			bus_dmamap_unload(sc->sf_cdata.sf_rx_ring_tag,
			    sc->sf_cdata.sf_rx_ring_map);
		if (sc->sf_rdata.sf_rx_ring)
			bus_dmamem_free(sc->sf_cdata.sf_rx_ring_tag,
			    sc->sf_rdata.sf_rx_ring,
			    sc->sf_cdata.sf_rx_ring_map);
		sc->sf_rdata.sf_rx_ring = NULL;
		sc->sf_rdata.sf_rx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->sf_cdata.sf_rx_ring_tag);
		sc->sf_cdata.sf_rx_ring_tag = NULL;
	}
	/* Rx completion ring. */
	if (sc->sf_cdata.sf_rx_cring_tag) {
		if (sc->sf_rdata.sf_rx_cring_paddr)
			bus_dmamap_unload(sc->sf_cdata.sf_rx_cring_tag,
			    sc->sf_cdata.sf_rx_cring_map);
		if (sc->sf_rdata.sf_rx_cring)
			bus_dmamem_free(sc->sf_cdata.sf_rx_cring_tag,
			    sc->sf_rdata.sf_rx_cring,
			    sc->sf_cdata.sf_rx_cring_map);
		sc->sf_rdata.sf_rx_cring = NULL;
		sc->sf_rdata.sf_rx_cring_paddr = 0;
		bus_dma_tag_destroy(sc->sf_cdata.sf_rx_cring_tag);
		sc->sf_cdata.sf_rx_cring_tag = NULL;
	}
	/* Tx buffers. */
	if (sc->sf_cdata.sf_tx_tag) {
		for (i = 0; i < SF_TX_DLIST_CNT; i++) {
			txd = &sc->sf_cdata.sf_txdesc[i];
			if (txd->tx_dmamap) {
				bus_dmamap_destroy(sc->sf_cdata.sf_tx_tag,
				    txd->tx_dmamap);
				txd->tx_dmamap = NULL;
			}
		}
		bus_dma_tag_destroy(sc->sf_cdata.sf_tx_tag);
		sc->sf_cdata.sf_tx_tag = NULL;
	}
	/* Rx buffers. */
	if (sc->sf_cdata.sf_rx_tag) {
		for (i = 0; i < SF_RX_DLIST_CNT; i++) {
			rxd = &sc->sf_cdata.sf_rxdesc[i];
			if (rxd->rx_dmamap) {
				bus_dmamap_destroy(sc->sf_cdata.sf_rx_tag,
				    rxd->rx_dmamap);
				rxd->rx_dmamap = NULL;
			}
		}
		if (sc->sf_cdata.sf_rx_sparemap) {
			bus_dmamap_destroy(sc->sf_cdata.sf_rx_tag,
			    sc->sf_cdata.sf_rx_sparemap);
			sc->sf_cdata.sf_rx_sparemap = 0;
		}
		bus_dma_tag_destroy(sc->sf_cdata.sf_rx_tag);
		sc->sf_cdata.sf_rx_tag = NULL;
	}

	if (sc->sf_cdata.sf_parent_tag) {
		bus_dma_tag_destroy(sc->sf_cdata.sf_parent_tag);
		sc->sf_cdata.sf_parent_tag = NULL;
	}
}

static int
sf_init_rx_ring(struct sf_softc *sc)
{
	struct sf_ring_data	*rd;
	int			i;

	sc->sf_cdata.sf_rxc_cons = 0;

	rd = &sc->sf_rdata;
	bzero(rd->sf_rx_ring, SF_RX_DLIST_SIZE);
	bzero(rd->sf_rx_cring, SF_RX_CLIST_SIZE);

	for (i = 0; i < SF_RX_DLIST_CNT; i++) {
		if (sf_newbuf(sc, i) != 0)
			return (ENOBUFS);
	}

	bus_dmamap_sync(sc->sf_cdata.sf_rx_cring_tag,
	    sc->sf_cdata.sf_rx_cring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sf_cdata.sf_rx_ring_tag,
	    sc->sf_cdata.sf_rx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

static void
sf_init_tx_ring(struct sf_softc *sc)
{
	struct sf_ring_data	*rd;
	int			i;

	sc->sf_cdata.sf_tx_prod = 0;
	sc->sf_cdata.sf_tx_cnt = 0;
	sc->sf_cdata.sf_txc_cons = 0;

	rd = &sc->sf_rdata;
	bzero(rd->sf_tx_ring, SF_TX_DLIST_SIZE);
	bzero(rd->sf_tx_cring, SF_TX_CLIST_SIZE);
	for (i = 0; i < SF_TX_DLIST_CNT; i++) {
		rd->sf_tx_ring[i].sf_tx_ctrl = htole32(SF_TX_DESC_ID);
		sc->sf_cdata.sf_txdesc[i].tx_m = NULL;
		sc->sf_cdata.sf_txdesc[i].ndesc = 0;
	}
	rd->sf_tx_ring[i].sf_tx_ctrl |= htole32(SF_TX_DESC_END);

	bus_dmamap_sync(sc->sf_cdata.sf_tx_ring_tag,
	    sc->sf_cdata.sf_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sf_cdata.sf_tx_cring_tag,
	    sc->sf_cdata.sf_tx_cring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
static int
sf_newbuf(struct sf_softc *sc, int idx)
{
	struct sf_rx_rdesc	*desc;
	struct sf_rxdesc	*rxd;
	struct mbuf		*m;
	bus_dma_segment_t	segs[1];
	bus_dmamap_t		map;
	int			nsegs;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, sizeof(uint32_t));

	if (bus_dmamap_load_mbuf_sg(sc->sf_cdata.sf_rx_tag,
	    sc->sf_cdata.sf_rx_sparemap, m, segs, &nsegs, 0) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	rxd = &sc->sf_cdata.sf_rxdesc[idx];
	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(sc->sf_cdata.sf_rx_tag, rxd->rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sf_cdata.sf_rx_tag, rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc->sf_cdata.sf_rx_sparemap;
	sc->sf_cdata.sf_rx_sparemap = map;
	bus_dmamap_sync(sc->sf_cdata.sf_rx_tag, rxd->rx_dmamap,
	    BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;
	desc = &sc->sf_rdata.sf_rx_ring[idx];
	desc->sf_addr = htole64(segs[0].ds_addr);

	return (0);
}

#ifndef __NO_STRICT_ALIGNMENT
static __inline void
sf_fixup_rx(struct mbuf *m)
{
        int			i;
        uint16_t		*src, *dst;

	src = mtod(m, uint16_t *);
	dst = src - 1;

	for (i = 0; i < (m->m_len / sizeof(uint16_t) + 1); i++)
		*dst++ = *src++;

	m->m_data -= ETHER_ALIGN;
}
#endif

/*
 * The starfire is programmed to use 'normal' mode for packet reception,
 * which means we use the consumer/producer model for both the buffer
 * descriptor queue and the completion descriptor queue. The only problem
 * with this is that it involves a lot of register accesses: we have to
 * read the RX completion consumer and producer indexes and the RX buffer
 * producer index, plus the RX completion consumer and RX buffer producer
 * indexes have to be updated. It would have been easier if Adaptec had
 * put each index in a separate register, especially given that the damn
 * NIC has a 512K register space.
 *
 * In spite of all the lovely features that Adaptec crammed into the 6915,
 * it is marred by one truly stupid design flaw, which is that receive
 * buffer addresses must be aligned on a longword boundary. This forces
 * the packet payload to be unaligned, which is suboptimal on the x86 and
 * completely unusable on the Alpha. Our only recourse is to copy received
 * packets into properly aligned buffers before handing them off.
 */
static int
sf_rxeof(struct sf_softc *sc)
{
	struct mbuf		*m;
	struct ifnet		*ifp;
	struct sf_rxdesc	*rxd;
	struct sf_rx_rcdesc	*cur_cmp;
	int			cons, eidx, prog, rx_npkts;
	uint32_t		status, status2;

	SF_LOCK_ASSERT(sc);

	ifp = sc->sf_ifp;
	rx_npkts = 0;

	bus_dmamap_sync(sc->sf_cdata.sf_rx_ring_tag,
	    sc->sf_cdata.sf_rx_ring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(sc->sf_cdata.sf_rx_cring_tag,
	    sc->sf_cdata.sf_rx_cring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	/*
	 * To reduce register access, directly read Receive completion
	 * queue entry.
	 */
	eidx = 0;
	prog = 0;
	for (cons = sc->sf_cdata.sf_rxc_cons;
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0;
	    SF_INC(cons, SF_RX_CLIST_CNT)) {
		cur_cmp = &sc->sf_rdata.sf_rx_cring[cons];
		status = le32toh(cur_cmp->sf_rx_status1);
		if (status == 0)
			break;
#ifdef DEVICE_POLLING
		if ((ifp->if_capenable & IFCAP_POLLING) != 0) {
			if (sc->rxcycles <= 0)
				break;
			sc->rxcycles--;
		}
#endif
		prog++;
		eidx = (status & SF_RX_CMPDESC_EIDX) >> 16;
		rxd = &sc->sf_cdata.sf_rxdesc[eidx];
		m = rxd->rx_m;

		/*
		 * Note, IFCOUNTER_IPACKETS and IFCOUNTER_IERRORS
		 * are handled in sf_stats_update().
		 */
		if ((status & SF_RXSTAT1_OK) == 0) {
			cur_cmp->sf_rx_status1 = 0;
			continue;
		}

		if (sf_newbuf(sc, eidx) != 0) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			cur_cmp->sf_rx_status1 = 0;
			continue;
		}

		/* AIC-6915 supports TCP/UDP checksum offload. */
		if ((ifp->if_capenable & IFCAP_RXCSUM) != 0) {
			status2 = le32toh(cur_cmp->sf_rx_status2);
			/*
			 * Sometimes AIC-6915 generates an interrupt to
			 * warn RxGFP stall with bad checksum bit set
			 * in status word. I'm not sure what conditioan
			 * triggers it but recevied packet's checksum
			 * was correct even though AIC-6915 does not
			 * agree on this. This may be an indication of
			 * firmware bug. To fix the issue, do not rely
			 * on bad checksum bit in status word and let
			 * upper layer verify integrity of received
			 * frame.
			 * Another nice feature of AIC-6915 is hardware
			 * assistance of checksum calculation by
			 * providing partial checksum value for received
			 * frame. The partial checksum value can be used
			 * to accelerate checksum computation for
			 * fragmented TCP/UDP packets. Upper network
			 * stack already takes advantage of the partial
			 * checksum value in IP reassembly stage. But
			 * I'm not sure the correctness of the partial
			 * hardware checksum assistance as frequent
			 * RxGFP stalls are seen on non-fragmented
			 * frames. Due to the nature of the complexity
			 * of checksum computation code in firmware it's
			 * possible to see another bug in RxGFP so
			 * ignore checksum assistance for fragmented
			 * frames. This can be changed in future.
			 */
			if ((status2 & SF_RXSTAT2_FRAG) == 0) {
				if ((status2 & (SF_RXSTAT2_TCP |
				    SF_RXSTAT2_UDP)) != 0) {
					if ((status2 & SF_RXSTAT2_CSUM_OK)) {
						m->m_pkthdr.csum_flags =
						    CSUM_DATA_VALID |
						    CSUM_PSEUDO_HDR;
						m->m_pkthdr.csum_data = 0xffff;
					}
				}
			}
#ifdef SF_PARTIAL_CSUM_SUPPORT
			else if ((status2 & SF_RXSTAT2_FRAG) != 0) {
				if ((status2 & (SF_RXSTAT2_TCP |
				    SF_RXSTAT2_UDP)) != 0) {
					if ((status2 & SF_RXSTAT2_PCSUM_OK)) {
						m->m_pkthdr.csum_flags =
						    CSUM_DATA_VALID;
						m->m_pkthdr.csum_data =
						    (status &
						    SF_RX_CMPDESC_CSUM2);
					}
				}
			}
#endif
		}

		m->m_pkthdr.len = m->m_len = status & SF_RX_CMPDESC_LEN;
#ifndef	__NO_STRICT_ALIGNMENT
		sf_fixup_rx(m);
#endif
		m->m_pkthdr.rcvif = ifp;

		SF_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		SF_LOCK(sc);
		rx_npkts++;

		/* Clear completion status. */
		cur_cmp->sf_rx_status1 = 0;
	}

	if (prog > 0) {
		sc->sf_cdata.sf_rxc_cons = cons;
		bus_dmamap_sync(sc->sf_cdata.sf_rx_ring_tag,
		    sc->sf_cdata.sf_rx_ring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		bus_dmamap_sync(sc->sf_cdata.sf_rx_cring_tag,
		    sc->sf_cdata.sf_rx_cring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		/* Update Rx completion Q1 consumer index. */
		csr_write_4(sc, SF_CQ_CONSIDX,
		    (csr_read_4(sc, SF_CQ_CONSIDX) & ~SF_CQ_CONSIDX_RXQ1) |
		    (cons & SF_CQ_CONSIDX_RXQ1));
		/* Update Rx descriptor Q1 ptr. */
		csr_write_4(sc, SF_RXDQ_PTR_Q1,
		    (csr_read_4(sc, SF_RXDQ_PTR_Q1) & ~SF_RXDQ_PRODIDX) |
		    (eidx & SF_RXDQ_PRODIDX));
	}
	return (rx_npkts);
}

/*
 * Read the transmit status from the completion queue and release
 * mbufs. Note that the buffer descriptor index in the completion
 * descriptor is an offset from the start of the transmit buffer
 * descriptor list in bytes. This is important because the manual
 * gives the impression that it should match the producer/consumer
 * index, which is the offset in 8 byte blocks.
 */
static void
sf_txeof(struct sf_softc *sc)
{
	struct sf_txdesc	*txd;
	struct sf_tx_rcdesc	*cur_cmp;
	struct ifnet		*ifp;
	uint32_t		status;
	int			cons, idx, prod;

	SF_LOCK_ASSERT(sc);

	ifp = sc->sf_ifp;

	bus_dmamap_sync(sc->sf_cdata.sf_tx_cring_tag,
	    sc->sf_cdata.sf_tx_cring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	cons = sc->sf_cdata.sf_txc_cons;
	prod = (csr_read_4(sc, SF_CQ_PRODIDX) & SF_TXDQ_PRODIDX_HIPRIO) >> 16;
	if (prod == cons)
		return;

	for (; cons != prod; SF_INC(cons, SF_TX_CLIST_CNT)) {
		cur_cmp = &sc->sf_rdata.sf_tx_cring[cons];
		status = le32toh(cur_cmp->sf_tx_status1);
		if (status == 0)
			break;
		switch (status & SF_TX_CMPDESC_TYPE) {
		case SF_TXCMPTYPE_TX:
			/* Tx complete entry. */
			break;
		case SF_TXCMPTYPE_DMA:
			/* DMA complete entry. */
			idx = status & SF_TX_CMPDESC_IDX;
			idx = idx / sizeof(struct sf_tx_rdesc);
			/*
			 * We don't need to check Tx status here.
			 * SF_ISR_TX_LOFIFO intr would handle this.
			 * Note, IFCOUNTER_OPACKETS, IFCOUNTER_COLLISIONS
			 * and IFCOUNTER_OERROR are handled in
			 * sf_stats_update().
			 */
			txd = &sc->sf_cdata.sf_txdesc[idx];
			if (txd->tx_m != NULL) {
				bus_dmamap_sync(sc->sf_cdata.sf_tx_tag,
				    txd->tx_dmamap,
				    BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(sc->sf_cdata.sf_tx_tag,
				    txd->tx_dmamap);
				m_freem(txd->tx_m);
				txd->tx_m = NULL;
			}
			sc->sf_cdata.sf_tx_cnt -= txd->ndesc;
			KASSERT(sc->sf_cdata.sf_tx_cnt >= 0,
			    ("%s: Active Tx desc counter was garbled\n",
			    __func__));
			txd->ndesc = 0;
			ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
			break;
		default:
			/* It should not happen. */
			device_printf(sc->sf_dev,
			    "unknown Tx completion type : 0x%08x : %d : %d\n",
			    status, cons, prod);
			break;
		}
		cur_cmp->sf_tx_status1 = 0;
	}

	sc->sf_cdata.sf_txc_cons = cons;
	bus_dmamap_sync(sc->sf_cdata.sf_tx_cring_tag,
	    sc->sf_cdata.sf_tx_cring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	if (sc->sf_cdata.sf_tx_cnt == 0)
		sc->sf_watchdog_timer = 0;

	/* Update Tx completion consumer index. */
	csr_write_4(sc, SF_CQ_CONSIDX,
	    (csr_read_4(sc, SF_CQ_CONSIDX) & 0xffff) |
	    ((cons << 16) & 0xffff0000));
}

static void
sf_txthresh_adjust(struct sf_softc *sc)
{
	uint32_t		txfctl;

	device_printf(sc->sf_dev, "Tx underrun -- ");
	if (sc->sf_txthresh < SF_MAX_TX_THRESHOLD) {
		txfctl = csr_read_4(sc, SF_TX_FRAMCTL);
		/* Increase Tx threshold 256 bytes. */
		sc->sf_txthresh += 16;
		if (sc->sf_txthresh > SF_MAX_TX_THRESHOLD)
			sc->sf_txthresh = SF_MAX_TX_THRESHOLD;
		txfctl &= ~SF_TXFRMCTL_TXTHRESH;
		txfctl |= sc->sf_txthresh;
		printf("increasing Tx threshold to %d bytes\n",
		    sc->sf_txthresh * SF_TX_THRESHOLD_UNIT);
		csr_write_4(sc, SF_TX_FRAMCTL, txfctl);
	} else
		printf("\n");
}

#ifdef DEVICE_POLLING
static int
sf_poll(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct sf_softc		*sc;
	uint32_t		status;
	int			rx_npkts;

	sc = ifp->if_softc;
	rx_npkts = 0;
	SF_LOCK(sc);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		SF_UNLOCK(sc);
		return (rx_npkts);
	}

	sc->rxcycles = count;
	rx_npkts = sf_rxeof(sc);
	sf_txeof(sc);
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		sf_start_locked(ifp);

	if (cmd == POLL_AND_CHECK_STATUS) {
		/* Reading the ISR register clears all interrrupts. */
		status = csr_read_4(sc, SF_ISR);

		if ((status & SF_ISR_ABNORMALINTR) != 0) {
			if ((status & SF_ISR_STATSOFLOW) != 0)
				sf_stats_update(sc);
			else if ((status & SF_ISR_TX_LOFIFO) != 0)
				sf_txthresh_adjust(sc);
			else if ((status & SF_ISR_DMAERR) != 0) {
				device_printf(sc->sf_dev,
				    "DMA error, resetting\n");
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				sf_init_locked(sc);
				SF_UNLOCK(sc);
				return (rx_npkts);
			} else if ((status & SF_ISR_NO_TX_CSUM) != 0) {
				sc->sf_statistics.sf_tx_gfp_stall++;
#ifdef	SF_GFP_DEBUG
				device_printf(sc->sf_dev,
				    "TxGFP is not responding!\n");
#endif
			} else if ((status & SF_ISR_RXGFP_NORESP) != 0) {
				sc->sf_statistics.sf_rx_gfp_stall++;
#ifdef	SF_GFP_DEBUG
				device_printf(sc->sf_dev,
				    "RxGFP is not responding!\n");
#endif
			}
		}
	}

	SF_UNLOCK(sc);
	return (rx_npkts);
}
#endif /* DEVICE_POLLING */

static void
sf_intr(void *arg)
{
	struct sf_softc		*sc;
	struct ifnet		*ifp;
	uint32_t		status;
	int			cnt;

	sc = (struct sf_softc *)arg;
	SF_LOCK(sc);

	if (sc->sf_suspended != 0)
		goto done_locked;

	/* Reading the ISR register clears all interrrupts. */
	status = csr_read_4(sc, SF_ISR);
	if (status == 0 || status == 0xffffffff ||
	    (status & SF_ISR_PCIINT_ASSERTED) == 0)
		goto done_locked;

	ifp = sc->sf_ifp;
#ifdef DEVICE_POLLING
	if ((ifp->if_capenable & IFCAP_POLLING) != 0)
		goto done_locked;
#endif

	/* Disable interrupts. */
	csr_write_4(sc, SF_IMR, 0x00000000);

	for (cnt = 32; (status & SF_INTRS) != 0;) {
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
			break;
		if ((status & SF_ISR_RXDQ1_DMADONE) != 0)
			sf_rxeof(sc);

		if ((status & (SF_ISR_TX_TXDONE | SF_ISR_TX_DMADONE |
		    SF_ISR_TX_QUEUEDONE)) != 0)
			sf_txeof(sc);

		if ((status & SF_ISR_ABNORMALINTR) != 0) {
			if ((status & SF_ISR_STATSOFLOW) != 0)
				sf_stats_update(sc);
			else if ((status & SF_ISR_TX_LOFIFO) != 0)
				sf_txthresh_adjust(sc);
			else if ((status & SF_ISR_DMAERR) != 0) {
				device_printf(sc->sf_dev,
				    "DMA error, resetting\n");
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				sf_init_locked(sc);
				SF_UNLOCK(sc);
				return;
			} else if ((status & SF_ISR_NO_TX_CSUM) != 0) {
				sc->sf_statistics.sf_tx_gfp_stall++;
#ifdef	SF_GFP_DEBUG
				device_printf(sc->sf_dev,
				    "TxGFP is not responding!\n");
#endif
			}
			else if ((status & SF_ISR_RXGFP_NORESP) != 0) {
				sc->sf_statistics.sf_rx_gfp_stall++;
#ifdef	SF_GFP_DEBUG
				device_printf(sc->sf_dev,
				    "RxGFP is not responding!\n");
#endif
			}
		}
		if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			sf_start_locked(ifp);
		if (--cnt <= 0)
			break;
		/* Reading the ISR register clears all interrrupts. */
		status = csr_read_4(sc, SF_ISR);
	}

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
		/* Re-enable interrupts. */
		csr_write_4(sc, SF_IMR, SF_INTRS);
	}

done_locked:
	SF_UNLOCK(sc);
}

static void
sf_download_fw(struct sf_softc *sc)
{
	uint32_t gfpinst;
	int i, ndx;
	uint8_t *p;

	/*
	 * A FP instruction is composed of 48bits so we have to
	 * write it with two parts.
	 */
	p = txfwdata;
	ndx = 0;
	for (i = 0; i < sizeof(txfwdata) / SF_GFP_INST_BYTES; i++) {
		gfpinst = p[2] << 24 | p[3] << 16 | p[4] << 8 | p[5];
		csr_write_4(sc, SF_TXGFP_MEM_BASE + ndx * 4, gfpinst);
		gfpinst = p[0] << 8 | p[1];
		csr_write_4(sc, SF_TXGFP_MEM_BASE + (ndx + 1) * 4, gfpinst);
		p += SF_GFP_INST_BYTES;
		ndx += 2;
	}
	if (bootverbose)
		device_printf(sc->sf_dev, "%d Tx instructions downloaded\n", i);

	p = rxfwdata;
	ndx = 0;
	for (i = 0; i < sizeof(rxfwdata) / SF_GFP_INST_BYTES; i++) {
		gfpinst = p[2] << 24 | p[3] << 16 | p[4] << 8 | p[5];
		csr_write_4(sc, SF_RXGFP_MEM_BASE + (ndx * 4), gfpinst);
		gfpinst = p[0] << 8 | p[1];
		csr_write_4(sc, SF_RXGFP_MEM_BASE + (ndx + 1) * 4, gfpinst);
		p += SF_GFP_INST_BYTES;
		ndx += 2;
	}
	if (bootverbose)
		device_printf(sc->sf_dev, "%d Rx instructions downloaded\n", i);
}

static void
sf_init(void *xsc)
{
	struct sf_softc		*sc;

	sc = (struct sf_softc *)xsc;
	SF_LOCK(sc);
	sf_init_locked(sc);
	SF_UNLOCK(sc);
}

static void
sf_init_locked(struct sf_softc *sc)
{
	struct ifnet		*ifp;
	uint8_t			eaddr[ETHER_ADDR_LEN];
	bus_addr_t		addr;
	int			i;

	SF_LOCK_ASSERT(sc);
	ifp = sc->sf_ifp;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	sf_stop(sc);
	/* Reset the hardware to a known state. */
	sf_reset(sc);

	/* Init all the receive filter registers */
	for (i = SF_RXFILT_PERFECT_BASE;
	    i < (SF_RXFILT_HASH_MAX + 1); i += sizeof(uint32_t))
		csr_write_4(sc, i, 0);

	/* Empty stats counter registers. */
	for (i = SF_STATS_BASE; i < (SF_STATS_END + 1); i += sizeof(uint32_t))
		csr_write_4(sc, i, 0);

	/* Init our MAC address. */
	bcopy(IF_LLADDR(sc->sf_ifp), eaddr, sizeof(eaddr));
	csr_write_4(sc, SF_PAR0,
	    eaddr[2] << 24 | eaddr[3] << 16 | eaddr[4] << 8 | eaddr[5]);
	csr_write_4(sc, SF_PAR1, eaddr[0] << 8 | eaddr[1]);
	sf_setperf(sc, 0, eaddr);

	if (sf_init_rx_ring(sc) == ENOBUFS) {
		device_printf(sc->sf_dev,
		    "initialization failed: no memory for rx buffers\n");
		sf_stop(sc);
		return;
	}

	sf_init_tx_ring(sc);

	/*
	 * 16 perfect address filtering.
	 * Hash only multicast destination address, Accept matching
	 * frames regardless of VLAN ID.
	 */
	csr_write_4(sc, SF_RXFILT, SF_PERFMODE_NORMAL | SF_HASHMODE_ANYVLAN);

	/*
	 * Set Rx filter.
	 */
	sf_rxfilter(sc);

	/* Init the completion queue indexes. */
	csr_write_4(sc, SF_CQ_CONSIDX, 0);
	csr_write_4(sc, SF_CQ_PRODIDX, 0);

	/* Init the RX completion queue. */
	addr = sc->sf_rdata.sf_rx_cring_paddr;
	csr_write_4(sc, SF_CQ_ADDR_HI, SF_ADDR_HI(addr));
	csr_write_4(sc, SF_RXCQ_CTL_1, SF_ADDR_LO(addr) & SF_RXCQ_ADDR);
	if (SF_ADDR_HI(addr) != 0)
		SF_SETBIT(sc, SF_RXCQ_CTL_1, SF_RXCQ_USE_64BIT);
	/* Set RX completion queue type 2. */
	SF_SETBIT(sc, SF_RXCQ_CTL_1, SF_RXCQTYPE_2);
	csr_write_4(sc, SF_RXCQ_CTL_2, 0);

	/*
	 * Init RX DMA control.
	 * default RxHighPriority Threshold,
	 * default RxBurstSize, 128bytes.
	 */
	SF_SETBIT(sc, SF_RXDMA_CTL,
	    SF_RXDMA_REPORTBADPKTS |
	    (SF_RXDMA_HIGHPRIO_THRESH << 8) |
	    SF_RXDMA_BURST);

	/* Init the RX buffer descriptor queue. */
	addr = sc->sf_rdata.sf_rx_ring_paddr;
	csr_write_4(sc, SF_RXDQ_ADDR_HI, SF_ADDR_HI(addr));
	csr_write_4(sc, SF_RXDQ_ADDR_Q1, SF_ADDR_LO(addr));

	/* Set RX queue buffer length. */
	csr_write_4(sc, SF_RXDQ_CTL_1,
	    ((MCLBYTES  - sizeof(uint32_t)) << 16) |
	    SF_RXDQCTL_64BITBADDR | SF_RXDQCTL_VARIABLE);

	if (SF_ADDR_HI(addr) != 0)
		SF_SETBIT(sc, SF_RXDQ_CTL_1, SF_RXDQCTL_64BITDADDR);
	csr_write_4(sc, SF_RXDQ_PTR_Q1, SF_RX_DLIST_CNT - 1);
	csr_write_4(sc, SF_RXDQ_CTL_2, 0);

	/* Init the TX completion queue */
	addr = sc->sf_rdata.sf_tx_cring_paddr;
	csr_write_4(sc, SF_TXCQ_CTL, SF_ADDR_LO(addr) & SF_TXCQ_ADDR);
	if (SF_ADDR_HI(addr) != 0)
		SF_SETBIT(sc, SF_TXCQ_CTL, SF_TXCQ_USE_64BIT);

	/* Init the TX buffer descriptor queue. */
	addr = sc->sf_rdata.sf_tx_ring_paddr;
	csr_write_4(sc, SF_TXDQ_ADDR_HI, SF_ADDR_HI(addr));
	csr_write_4(sc, SF_TXDQ_ADDR_HIPRIO, 0);
	csr_write_4(sc, SF_TXDQ_ADDR_LOPRIO, SF_ADDR_LO(addr));
	csr_write_4(sc, SF_TX_FRAMCTL,
	    SF_TXFRMCTL_CPLAFTERTX | sc->sf_txthresh);
	csr_write_4(sc, SF_TXDQ_CTL,
	    SF_TXDMA_HIPRIO_THRESH << 24 |
	    SF_TXSKIPLEN_0BYTES << 16 |
	    SF_TXDDMA_BURST << 8 |
	    SF_TXBUFDESC_TYPE2 | SF_TXMINSPACE_UNLIMIT);
	if (SF_ADDR_HI(addr) != 0)
		SF_SETBIT(sc, SF_TXDQ_CTL, SF_TXDQCTL_64BITADDR);

	/* Set VLAN Type register. */
	csr_write_4(sc, SF_VLANTYPE, ETHERTYPE_VLAN);

	/* Set TxPause Timer. */
	csr_write_4(sc, SF_TXPAUSETIMER, 0xffff);

	/* Enable autopadding of short TX frames. */
	SF_SETBIT(sc, SF_MACCFG_1, SF_MACCFG1_AUTOPAD);
	SF_SETBIT(sc, SF_MACCFG_2, SF_MACCFG2_AUTOVLANPAD);
	/* Make sure to reset MAC to take changes effect. */
	SF_SETBIT(sc, SF_MACCFG_1, SF_MACCFG1_SOFTRESET);
	DELAY(1000);
	SF_CLRBIT(sc, SF_MACCFG_1, SF_MACCFG1_SOFTRESET);

	/* Enable PCI bus master. */
	SF_SETBIT(sc, SF_PCI_DEVCFG, SF_PCIDEVCFG_PCIMEN);

	/* Load StarFire firmware. */
	sf_download_fw(sc);

	/* Intialize interrupt moderation. */
	csr_write_4(sc, SF_TIMER_CTL, SF_TIMER_IMASK_MODE | SF_TIMER_TIMES_TEN |
	    (sc->sf_int_mod & SF_TIMER_IMASK_INTERVAL));

#ifdef DEVICE_POLLING
	/* Disable interrupts if we are polling. */
	if ((ifp->if_capenable & IFCAP_POLLING) != 0)
		csr_write_4(sc, SF_IMR, 0x00000000);
	else
#endif
	/* Enable interrupts. */
	csr_write_4(sc, SF_IMR, SF_INTRS);
	SF_SETBIT(sc, SF_PCI_DEVCFG, SF_PCIDEVCFG_INTR_ENB);

	/* Enable the RX and TX engines. */
	csr_write_4(sc, SF_GEN_ETH_CTL,
	    SF_ETHCTL_RX_ENB | SF_ETHCTL_RXDMA_ENB |
	    SF_ETHCTL_TX_ENB | SF_ETHCTL_TXDMA_ENB);

	if ((ifp->if_capenable & IFCAP_TXCSUM) != 0)
		SF_SETBIT(sc, SF_GEN_ETH_CTL, SF_ETHCTL_TXGFP_ENB);
	else
		SF_CLRBIT(sc, SF_GEN_ETH_CTL, SF_ETHCTL_TXGFP_ENB);
	if ((ifp->if_capenable & IFCAP_RXCSUM) != 0)
		SF_SETBIT(sc, SF_GEN_ETH_CTL, SF_ETHCTL_RXGFP_ENB);
	else
		SF_CLRBIT(sc, SF_GEN_ETH_CTL, SF_ETHCTL_RXGFP_ENB);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	sc->sf_link = 0;
	sf_ifmedia_upd_locked(ifp);

	callout_reset(&sc->sf_co, hz, sf_tick, sc);
}

static int
sf_encap(struct sf_softc *sc, struct mbuf **m_head)
{
	struct sf_txdesc	*txd;
	struct sf_tx_rdesc	*desc;
	struct mbuf		*m;
	bus_dmamap_t		map;
	bus_dma_segment_t	txsegs[SF_MAXTXSEGS];
	int			error, i, nsegs, prod, si;
	int			avail, nskip;

	SF_LOCK_ASSERT(sc);

	m = *m_head;
	prod = sc->sf_cdata.sf_tx_prod;
	txd = &sc->sf_cdata.sf_txdesc[prod];
	map = txd->tx_dmamap;
	error = bus_dmamap_load_mbuf_sg(sc->sf_cdata.sf_tx_tag, map,
	    *m_head, txsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error == EFBIG) {
		m = m_collapse(*m_head, M_NOWAIT, SF_MAXTXSEGS);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->sf_cdata.sf_tx_tag,
		    map, *m_head, txsegs, &nsegs, BUS_DMA_NOWAIT);
		if (error != 0) {
			m_freem(*m_head);
			*m_head = NULL;
			return (error);
		}
	} else if (error != 0)
		return (error);
	if (nsegs == 0) {
		m_freem(*m_head);
		*m_head = NULL;
		return (EIO);
	}

	/* Check number of available descriptors. */
	avail = (SF_TX_DLIST_CNT - 1) - sc->sf_cdata.sf_tx_cnt;
	if (avail < nsegs) {
		bus_dmamap_unload(sc->sf_cdata.sf_tx_tag, map);
		return (ENOBUFS);
	}
	nskip = 0;
	if (prod + nsegs >= SF_TX_DLIST_CNT) {
		nskip = SF_TX_DLIST_CNT - prod - 1;
		if (avail < nsegs + nskip) {
			bus_dmamap_unload(sc->sf_cdata.sf_tx_tag, map);
			return (ENOBUFS);
		}
	}

	bus_dmamap_sync(sc->sf_cdata.sf_tx_tag, map, BUS_DMASYNC_PREWRITE);

	si = prod;
	for (i = 0; i < nsegs; i++) {
		desc = &sc->sf_rdata.sf_tx_ring[prod];
		desc->sf_tx_ctrl = htole32(SF_TX_DESC_ID |
		    (txsegs[i].ds_len & SF_TX_DESC_FRAGLEN));
		desc->sf_tx_reserved = 0;
		desc->sf_addr = htole64(txsegs[i].ds_addr);
		if (i == 0 && prod + nsegs >= SF_TX_DLIST_CNT) {
			/* Queue wraps! */
			desc->sf_tx_ctrl |= htole32(SF_TX_DESC_END);
			prod = 0;
		} else
			SF_INC(prod, SF_TX_DLIST_CNT);
	}
	/* Update producer index. */
	sc->sf_cdata.sf_tx_prod = prod;
	sc->sf_cdata.sf_tx_cnt += nsegs + nskip;

	desc = &sc->sf_rdata.sf_tx_ring[si];
	/* Check TDP/UDP checksum offload request. */
	if ((m->m_pkthdr.csum_flags & SF_CSUM_FEATURES) != 0)
		desc->sf_tx_ctrl |= htole32(SF_TX_DESC_CALTCP);
	desc->sf_tx_ctrl |=
	    htole32(SF_TX_DESC_CRCEN | SF_TX_DESC_INTR | (nsegs << 16));

	txd->tx_dmamap = map;
	txd->tx_m = m;
	txd->ndesc = nsegs + nskip;

	return (0);
}

static void
sf_start(struct ifnet *ifp)
{
	struct sf_softc		*sc;

	sc = ifp->if_softc;
	SF_LOCK(sc);
	sf_start_locked(ifp);
	SF_UNLOCK(sc);
}

static void
sf_start_locked(struct ifnet *ifp)
{
	struct sf_softc		*sc;
	struct mbuf		*m_head;
	int			enq;

	sc = ifp->if_softc;
	SF_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || sc->sf_link == 0)
		return;

	/*
	 * Since we don't know when descriptor wrap occurrs in advance
	 * limit available number of active Tx descriptor counter to be
	 * higher than maximum number of DMA segments allowed in driver.
	 */
	for (enq = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd) &&
	    sc->sf_cdata.sf_tx_cnt < SF_TX_DLIST_CNT - SF_MAXTXSEGS; ) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;
		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (sf_encap(sc, &m_head)) {
			if (m_head == NULL)
				break;
			IFQ_DRV_PREPEND(&ifp->if_snd, m_head);
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		enq++;
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		ETHER_BPF_MTAP(ifp, m_head);
	}

	if (enq > 0) {
		bus_dmamap_sync(sc->sf_cdata.sf_tx_ring_tag,
		    sc->sf_cdata.sf_tx_ring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		/* Kick transmit. */
		csr_write_4(sc, SF_TXDQ_PRODIDX,
		    sc->sf_cdata.sf_tx_prod * (sizeof(struct sf_tx_rdesc) / 8));

		/* Set a timeout in case the chip goes out to lunch. */
		sc->sf_watchdog_timer = 5;
	}
}

static void
sf_stop(struct sf_softc *sc)
{
	struct sf_txdesc	*txd;
	struct sf_rxdesc	*rxd;
	struct ifnet		*ifp;
	int			i;

	SF_LOCK_ASSERT(sc);

	ifp = sc->sf_ifp;

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->sf_link = 0;
	callout_stop(&sc->sf_co);
	sc->sf_watchdog_timer = 0;

	/* Reading the ISR register clears all interrrupts. */
	csr_read_4(sc, SF_ISR);
	/* Disable further interrupts. */
	csr_write_4(sc, SF_IMR, 0);

	/* Disable Tx/Rx egine. */
	csr_write_4(sc, SF_GEN_ETH_CTL, 0);

	/* Give hardware chance to drain active DMA cycles. */
	DELAY(1000);

	csr_write_4(sc, SF_CQ_CONSIDX, 0);
	csr_write_4(sc, SF_CQ_PRODIDX, 0);
	csr_write_4(sc, SF_RXDQ_ADDR_Q1, 0);
	csr_write_4(sc, SF_RXDQ_CTL_1, 0);
	csr_write_4(sc, SF_RXDQ_PTR_Q1, 0);
	csr_write_4(sc, SF_TXCQ_CTL, 0);
	csr_write_4(sc, SF_TXDQ_ADDR_HIPRIO, 0);
	csr_write_4(sc, SF_TXDQ_CTL, 0);

	/*
	 * Free RX and TX mbufs still in the queues.
	 */
	for (i = 0; i < SF_RX_DLIST_CNT; i++) {
		rxd = &sc->sf_cdata.sf_rxdesc[i];
		if (rxd->rx_m != NULL) {
			bus_dmamap_sync(sc->sf_cdata.sf_rx_tag,
			    rxd->rx_dmamap, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sf_cdata.sf_rx_tag,
			    rxd->rx_dmamap);
			m_freem(rxd->rx_m);
			rxd->rx_m = NULL;
		}
        }
	for (i = 0; i < SF_TX_DLIST_CNT; i++) {
		txd = &sc->sf_cdata.sf_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_sync(sc->sf_cdata.sf_tx_tag,
			    txd->tx_dmamap, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sf_cdata.sf_tx_tag,
			    txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
			txd->ndesc = 0;
		}
        }
}

static void
sf_tick(void *xsc)
{
	struct sf_softc		*sc;
	struct mii_data		*mii;

	sc = xsc;
	SF_LOCK_ASSERT(sc);
	mii = device_get_softc(sc->sf_miibus);
	mii_tick(mii);
	sf_stats_update(sc);
	sf_watchdog(sc);
	callout_reset(&sc->sf_co, hz, sf_tick, sc);
}

/*
 * Note: it is important that this function not be interrupted. We
 * use a two-stage register access scheme: if we are interrupted in
 * between setting the indirect address register and reading from the
 * indirect data register, the contents of the address register could
 * be changed out from under us.
 */
static void
sf_stats_update(struct sf_softc *sc)
{
	struct ifnet		*ifp;
	struct sf_stats		now, *stats, *nstats;
	int			i;

	SF_LOCK_ASSERT(sc);

	ifp = sc->sf_ifp;
	stats = &now;

	stats->sf_tx_frames =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_TX_FRAMES);
	stats->sf_tx_single_colls =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_TX_SINGLE_COL);
	stats->sf_tx_multi_colls =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_TX_MULTI_COL);
	stats->sf_tx_crcerrs =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_TX_CRC_ERRS);
	stats->sf_tx_bytes =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_TX_BYTES);
	stats->sf_tx_deferred =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_TX_DEFERRED);
	stats->sf_tx_late_colls =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_TX_LATE_COL);
	stats->sf_tx_pause_frames =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_TX_PAUSE);
	stats->sf_tx_control_frames =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_TX_CTL_FRAME);
	stats->sf_tx_excess_colls =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_TX_EXCESS_COL);
	stats->sf_tx_excess_defer =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_TX_EXCESS_DEF);
	stats->sf_tx_mcast_frames =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_TX_MULTI);
	stats->sf_tx_bcast_frames =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_TX_BCAST);
	stats->sf_tx_frames_lost =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_TX_FRAME_LOST);
	stats->sf_rx_frames =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_FRAMES);
	stats->sf_rx_crcerrs =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_CRC_ERRS);
	stats->sf_rx_alignerrs =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_ALIGN_ERRS);
	stats->sf_rx_bytes =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_BYTES);
	stats->sf_rx_pause_frames =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_PAUSE);
	stats->sf_rx_control_frames =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_CTL_FRAME);
	stats->sf_rx_unsup_control_frames =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_UNSUP_FRAME);
	stats->sf_rx_giants =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_GIANTS);
	stats->sf_rx_runts =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_RUNTS);
	stats->sf_rx_jabbererrs =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_JABBER);
	stats->sf_rx_fragments =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_FRAGMENTS);
	stats->sf_rx_pkts_64 =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_64);
	stats->sf_rx_pkts_65_127 =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_65_127);
	stats->sf_rx_pkts_128_255 =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_128_255);
	stats->sf_rx_pkts_256_511 =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_256_511);
	stats->sf_rx_pkts_512_1023 =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_512_1023);
	stats->sf_rx_pkts_1024_1518 =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_1024_1518);
	stats->sf_rx_frames_lost =
	    csr_read_4(sc, SF_STATS_BASE + SF_STATS_RX_FRAME_LOST);
	/* Lower 16bits are valid. */
	stats->sf_tx_underruns =
	    (csr_read_4(sc, SF_STATS_BASE + SF_STATS_TX_UNDERRUN) & 0xffff);

	/* Empty stats counter registers. */
	for (i = SF_STATS_BASE; i < (SF_STATS_END + 1); i += sizeof(uint32_t))
		csr_write_4(sc, i, 0);

	if_inc_counter(ifp, IFCOUNTER_OPACKETS, (u_long)stats->sf_tx_frames);

	if_inc_counter(ifp, IFCOUNTER_COLLISIONS,
	    (u_long)stats->sf_tx_single_colls +
	    (u_long)stats->sf_tx_multi_colls);

	if_inc_counter(ifp, IFCOUNTER_OERRORS,
	    (u_long)stats->sf_tx_excess_colls +
	    (u_long)stats->sf_tx_excess_defer +
	    (u_long)stats->sf_tx_frames_lost);

	if_inc_counter(ifp, IFCOUNTER_IPACKETS, (u_long)stats->sf_rx_frames);

	if_inc_counter(ifp, IFCOUNTER_IERRORS,
	    (u_long)stats->sf_rx_crcerrs +
	    (u_long)stats->sf_rx_alignerrs +
	    (u_long)stats->sf_rx_giants +
	    (u_long)stats->sf_rx_runts +
	    (u_long)stats->sf_rx_jabbererrs +
	    (u_long)stats->sf_rx_frames_lost);

	nstats = &sc->sf_statistics;

	nstats->sf_tx_frames += stats->sf_tx_frames;
	nstats->sf_tx_single_colls += stats->sf_tx_single_colls;
	nstats->sf_tx_multi_colls += stats->sf_tx_multi_colls;
	nstats->sf_tx_crcerrs += stats->sf_tx_crcerrs;
	nstats->sf_tx_bytes += stats->sf_tx_bytes;
	nstats->sf_tx_deferred += stats->sf_tx_deferred;
	nstats->sf_tx_late_colls += stats->sf_tx_late_colls;
	nstats->sf_tx_pause_frames += stats->sf_tx_pause_frames;
	nstats->sf_tx_control_frames += stats->sf_tx_control_frames;
	nstats->sf_tx_excess_colls += stats->sf_tx_excess_colls;
	nstats->sf_tx_excess_defer += stats->sf_tx_excess_defer;
	nstats->sf_tx_mcast_frames += stats->sf_tx_mcast_frames;
	nstats->sf_tx_bcast_frames += stats->sf_tx_bcast_frames;
	nstats->sf_tx_frames_lost += stats->sf_tx_frames_lost;
	nstats->sf_rx_frames += stats->sf_rx_frames;
	nstats->sf_rx_crcerrs += stats->sf_rx_crcerrs;
	nstats->sf_rx_alignerrs += stats->sf_rx_alignerrs;
	nstats->sf_rx_bytes += stats->sf_rx_bytes;
	nstats->sf_rx_pause_frames += stats->sf_rx_pause_frames;
	nstats->sf_rx_control_frames += stats->sf_rx_control_frames;
	nstats->sf_rx_unsup_control_frames += stats->sf_rx_unsup_control_frames;
	nstats->sf_rx_giants += stats->sf_rx_giants;
	nstats->sf_rx_runts += stats->sf_rx_runts;
	nstats->sf_rx_jabbererrs += stats->sf_rx_jabbererrs;
	nstats->sf_rx_fragments += stats->sf_rx_fragments;
	nstats->sf_rx_pkts_64 += stats->sf_rx_pkts_64;
	nstats->sf_rx_pkts_65_127 += stats->sf_rx_pkts_65_127;
	nstats->sf_rx_pkts_128_255 += stats->sf_rx_pkts_128_255;
	nstats->sf_rx_pkts_256_511 += stats->sf_rx_pkts_256_511;
	nstats->sf_rx_pkts_512_1023 += stats->sf_rx_pkts_512_1023;
	nstats->sf_rx_pkts_1024_1518 += stats->sf_rx_pkts_1024_1518;
	nstats->sf_rx_frames_lost += stats->sf_rx_frames_lost;
	nstats->sf_tx_underruns += stats->sf_tx_underruns;
}

static void
sf_watchdog(struct sf_softc *sc)
{
	struct ifnet		*ifp;

	SF_LOCK_ASSERT(sc);

	if (sc->sf_watchdog_timer == 0 || --sc->sf_watchdog_timer)
		return;

	ifp = sc->sf_ifp;

	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	if (sc->sf_link == 0) {
		if (bootverbose)
			if_printf(sc->sf_ifp, "watchdog timeout "
			   "(missed link)\n");
	} else
		if_printf(ifp, "watchdog timeout, %d Tx descs are active\n",
		    sc->sf_cdata.sf_tx_cnt);

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	sf_init_locked(sc);

	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		sf_start_locked(ifp);
}

static int
sf_shutdown(device_t dev)
{
	struct sf_softc		*sc;

	sc = device_get_softc(dev);

	SF_LOCK(sc);
	sf_stop(sc);
	SF_UNLOCK(sc);

	return (0);
}

static int
sf_suspend(device_t dev)
{
	struct sf_softc		*sc;

	sc = device_get_softc(dev);

	SF_LOCK(sc);
	sf_stop(sc);
	sc->sf_suspended = 1;
	bus_generic_suspend(dev);
	SF_UNLOCK(sc);

	return (0);
}

static int
sf_resume(device_t dev)
{
	struct sf_softc		*sc;
	struct ifnet		*ifp;

	sc = device_get_softc(dev);

	SF_LOCK(sc);
	bus_generic_resume(dev);
	ifp = sc->sf_ifp;
	if ((ifp->if_flags & IFF_UP) != 0)
		sf_init_locked(sc);

	sc->sf_suspended = 0;
	SF_UNLOCK(sc);

	return (0);
}

static int
sf_sysctl_stats(SYSCTL_HANDLER_ARGS)
{
	struct sf_softc		*sc;
	struct sf_stats		*stats;
	int			error;
	int			result;

	result = -1;
	error = sysctl_handle_int(oidp, &result, 0, req);

	if (error != 0 || req->newptr == NULL)
		return (error);

	if (result != 1)
		return (error);

	sc = (struct sf_softc *)arg1;
	stats = &sc->sf_statistics;

	printf("%s statistics:\n", device_get_nameunit(sc->sf_dev));
	printf("Transmit good frames : %ju\n",
	    (uintmax_t)stats->sf_tx_frames);
	printf("Transmit good octets : %ju\n",
	    (uintmax_t)stats->sf_tx_bytes);
	printf("Transmit single collisions : %u\n",
	    stats->sf_tx_single_colls);
	printf("Transmit multiple collisions : %u\n",
	    stats->sf_tx_multi_colls);
	printf("Transmit late collisions : %u\n",
	    stats->sf_tx_late_colls);
	printf("Transmit abort due to excessive collisions : %u\n",
	    stats->sf_tx_excess_colls);
	printf("Transmit CRC errors : %u\n",
	    stats->sf_tx_crcerrs);
	printf("Transmit deferrals : %u\n",
	    stats->sf_tx_deferred);
	printf("Transmit abort due to excessive deferrals : %u\n",
	    stats->sf_tx_excess_defer);
	printf("Transmit pause control frames : %u\n",
	    stats->sf_tx_pause_frames);
	printf("Transmit control frames : %u\n",
	    stats->sf_tx_control_frames);
	printf("Transmit good multicast frames : %u\n",
	    stats->sf_tx_mcast_frames);
	printf("Transmit good broadcast frames : %u\n",
	    stats->sf_tx_bcast_frames);
	printf("Transmit frames lost due to internal transmit errors : %u\n",
	    stats->sf_tx_frames_lost);
	printf("Transmit FIFO underflows : %u\n",
	    stats->sf_tx_underruns);
	printf("Transmit GFP stalls : %u\n", stats->sf_tx_gfp_stall);
	printf("Receive good frames : %ju\n",
	    (uint64_t)stats->sf_rx_frames);
	printf("Receive good octets : %ju\n",
	    (uint64_t)stats->sf_rx_bytes);
	printf("Receive CRC errors : %u\n",
	    stats->sf_rx_crcerrs);
	printf("Receive alignment errors : %u\n",
	    stats->sf_rx_alignerrs);
	printf("Receive pause frames : %u\n",
	    stats->sf_rx_pause_frames);
	printf("Receive control frames : %u\n",
	    stats->sf_rx_control_frames);
	printf("Receive control frames with unsupported opcode : %u\n",
	    stats->sf_rx_unsup_control_frames);
	printf("Receive frames too long : %u\n",
	    stats->sf_rx_giants);
	printf("Receive frames too short : %u\n",
	    stats->sf_rx_runts);
	printf("Receive frames jabber errors : %u\n",
	    stats->sf_rx_jabbererrs);
	printf("Receive frames fragments : %u\n",
	    stats->sf_rx_fragments);
	printf("Receive packets 64 bytes : %ju\n",
	    (uint64_t)stats->sf_rx_pkts_64);
	printf("Receive packets 65 to 127 bytes : %ju\n",
	    (uint64_t)stats->sf_rx_pkts_65_127);
	printf("Receive packets 128 to 255 bytes : %ju\n",
	    (uint64_t)stats->sf_rx_pkts_128_255);
	printf("Receive packets 256 to 511 bytes : %ju\n",
	    (uint64_t)stats->sf_rx_pkts_256_511);
	printf("Receive packets 512 to 1023 bytes : %ju\n",
	    (uint64_t)stats->sf_rx_pkts_512_1023);
	printf("Receive packets 1024 to 1518 bytes : %ju\n",
	    (uint64_t)stats->sf_rx_pkts_1024_1518);
	printf("Receive frames lost due to internal receive errors : %u\n",
	    stats->sf_rx_frames_lost);
	printf("Receive GFP stalls : %u\n", stats->sf_rx_gfp_stall);

	return (error);
}

static int
sysctl_int_range(SYSCTL_HANDLER_ARGS, int low, int high)
{
	int error, value;

	if (!arg1)
		return (EINVAL);
	value = *(int *)arg1;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error || !req->newptr)
		return (error);
	if (value < low || value > high)
		return (EINVAL);
	*(int *)arg1 = value;

	return (0);
}

static int
sysctl_hw_sf_int_mod(SYSCTL_HANDLER_ARGS)
{

	return (sysctl_int_range(oidp, arg1, arg2, req, SF_IM_MIN, SF_IM_MAX));
}
