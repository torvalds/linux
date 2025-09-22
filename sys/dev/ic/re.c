/*	$OpenBSD: re.c,v 1.220 2025/05/10 11:08:26 kettenis Exp $	*/
/*	$FreeBSD: if_re.c,v 1.31 2004/09/04 07:54:05 ru Exp $	*/
/*
 * Copyright (c) 1997, 1998-2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
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

/*
 * Realtek 8139C+/8169/8169S/8110S PCI NIC driver
 *
 * Written by Bill Paul <wpaul@windriver.com>
 * Senior Networking Software Engineer
 * Wind River Systems
 */

/*
 * This driver is designed to support Realtek's next generation of
 * 10/100 and 10/100/1000 PCI ethernet controllers. There are currently
 * seven devices in this family: the RTL8139C+, the RTL8169, the RTL8169S,
 * RTL8110S, the RTL8168, the RTL8111 and the RTL8101E.
 *
 * The 8139C+ is a 10/100 ethernet chip. It is backwards compatible
 * with the older 8139 family, however it also supports a special
 * C+ mode of operation that provides several new performance enhancing
 * features. These include:
 *
 *	o Descriptor based DMA mechanism. Each descriptor represents
 *	  a single packet fragment. Data buffers may be aligned on
 *	  any byte boundary.
 *
 *	o 64-bit DMA
 *
 *	o TCP/IP checksum offload for both RX and TX
 *
 *	o High and normal priority transmit DMA rings
 *
 *	o VLAN tag insertion and extraction
 *
 *	o TCP large send (segmentation offload)
 *
 * Like the 8139, the 8139C+ also has a built-in 10/100 PHY. The C+
 * programming API is fairly straightforward. The RX filtering, EEPROM
 * access and PHY access is the same as it is on the older 8139 series
 * chips.
 *
 * The 8169 is a 64-bit 10/100/1000 gigabit ethernet MAC. It has almost the
 * same programming API and feature set as the 8139C+ with the following
 * differences and additions:
 *
 *	o 1000Mbps mode
 *
 *	o Jumbo frames
 *
 * 	o GMII and TBI ports/registers for interfacing with copper
 *	  or fiber PHYs
 *
 *      o RX and TX DMA rings can have up to 1024 descriptors
 *        (the 8139C+ allows a maximum of 64)
 *
 *	o Slight differences in register layout from the 8139C+
 *
 * The TX start and timer interrupt registers are at different locations
 * on the 8169 than they are on the 8139C+. Also, the status word in the
 * RX descriptor has a slightly different bit layout. The 8169 does not
 * have a built-in PHY. Most reference boards use a Marvell 88E1000 'Alaska'
 * copper gigE PHY.
 *
 * The 8169S/8110S 10/100/1000 devices have built-in copper gigE PHYs
 * (the 'S' stands for 'single-chip'). These devices have the same
 * programming API as the older 8169, but also have some vendor-specific
 * registers for the on-board PHY. The 8110S is a LAN-on-motherboard
 * part designed to be pin-compatible with the Realtek 8100 10/100 chip.
 *
 * This driver takes advantage of the RX and TX checksum offload and
 * VLAN tag insertion/extraction features. It also implements TX
 * interrupt moderation using the timer interrupt registers, which
 * significantly reduces TX interrupt load. There is also support
 * for jumbo frames, however the 8169/8169S/8110S can not transmit
 * jumbo frames larger than 7440, so the max MTU possible with this
 * driver is 7422 bytes.
 */

#include "bpfilter.h"
#include "vlan.h"
#include "kstat.h"

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/timeout.h>
#include <sys/socket.h>
#include <sys/atomic.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#if NKSTAT > 0
#include <sys/kstat.h>
#endif

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcidevs.h>

#include <dev/ic/rtl81x9reg.h>
#include <dev/ic/revar.h>

#ifdef RE_DEBUG
int redebug = 0;
#define DPRINTF(x)	do { if (redebug) printf x; } while (0)
#else
#define DPRINTF(x)
#endif

static inline void re_set_bufaddr(struct rl_desc *, bus_addr_t);

int	re_encap(struct rl_softc *, unsigned int, struct mbuf *);

int	re_newbuf(struct rl_softc *);
int	re_rx_list_init(struct rl_softc *);
void	re_rx_list_fill(struct rl_softc *);
int	re_tx_list_init(struct rl_softc *);
int	re_rxeof(struct rl_softc *);
int	re_txeof(struct rl_softc *);
void	re_tick(void *);
void	re_start(struct ifqueue *);
void	re_txstart(void *);
int	re_ioctl(struct ifnet *, u_long, caddr_t);
void	re_watchdog(struct ifnet *);
int	re_ifmedia_upd(struct ifnet *);
void	re_ifmedia_sts(struct ifnet *, struct ifmediareq *);

void	re_set_jumbo(struct rl_softc *);

void	re_eeprom_putbyte(struct rl_softc *, int);
void	re_eeprom_getword(struct rl_softc *, int, u_int16_t *);
void	re_read_eeprom(struct rl_softc *, caddr_t, int, int);

int	re_gmii_readreg(struct device *, int, int);
void	re_gmii_writereg(struct device *, int, int, int);

int	re_miibus_readreg(struct device *, int, int);
void	re_miibus_writereg(struct device *, int, int, int);
void	re_miibus_statchg(struct device *);

void	re_iff(struct rl_softc *);

void	re_setup_hw_im(struct rl_softc *);
void	re_setup_sim_im(struct rl_softc *);
void	re_disable_hw_im(struct rl_softc *);
void	re_disable_sim_im(struct rl_softc *);
void	re_config_imtype(struct rl_softc *, int);
void	re_setup_intr(struct rl_softc *, int, int);
#ifndef SMALL_KERNEL
int	re_wol(struct ifnet*, int);
#endif
#if NKSTAT > 0
void	re_kstat_attach(struct rl_softc *);
void	re_kstat_detach(struct rl_softc *);
#endif

void	in_delayed_cksum(struct mbuf *);

struct cfdriver re_cd = {
	NULL, "re", DV_IFNET
};

#define EE_SET(x)					\
	CSR_WRITE_1(sc, RL_EECMD,			\
		CSR_READ_1(sc, RL_EECMD) | x)

#define EE_CLR(x)					\
	CSR_WRITE_1(sc, RL_EECMD,			\
		CSR_READ_1(sc, RL_EECMD) & ~x)

#define RL_FRAMELEN(mtu)				\
	(mtu + ETHER_HDR_LEN + ETHER_CRC_LEN +		\
		ETHER_VLAN_ENCAP_LEN)

static const struct re_revision {
	u_int32_t		re_chipid;
	const char		*re_name;
} re_revisions[] = {
	{ RL_HWREV_8100,	"RTL8100" },
	{ RL_HWREV_8100E,	"RTL8100E" },
	{ RL_HWREV_8100E_SPIN2, "RTL8100E 2" },
	{ RL_HWREV_8101,	"RTL8101" },
	{ RL_HWREV_8101E,	"RTL8101E" },
	{ RL_HWREV_8102E,	"RTL8102E" },
	{ RL_HWREV_8106E,	"RTL8106E" },
	{ RL_HWREV_8401E,	"RTL8401E" },
	{ RL_HWREV_8402,	"RTL8402" },
	{ RL_HWREV_8411,	"RTL8411" },
	{ RL_HWREV_8411B,	"RTL8411B" },
	{ RL_HWREV_8102EL,	"RTL8102EL" },
	{ RL_HWREV_8102EL_SPIN1, "RTL8102EL 1" },
	{ RL_HWREV_8103E,       "RTL8103E" },
	{ RL_HWREV_8110S,	"RTL8110S" },
	{ RL_HWREV_8139CPLUS,	"RTL8139C+" },
	{ RL_HWREV_8168B_SPIN1,	"RTL8168 1" },
	{ RL_HWREV_8168B_SPIN2,	"RTL8168 2" },
	{ RL_HWREV_8168B_SPIN3,	"RTL8168 3" },
	{ RL_HWREV_8168C,	"RTL8168C/8111C" },
	{ RL_HWREV_8168C_SPIN2,	"RTL8168C/8111C" },
	{ RL_HWREV_8168CP,	"RTL8168CP/8111CP" },
	{ RL_HWREV_8168F,	"RTL8168F/8111F" },
	{ RL_HWREV_8168G,	"RTL8168G/8111G" },
	{ RL_HWREV_8168GU,	"RTL8168GU/8111GU" },
	{ RL_HWREV_8168H,	"RTL8168H/8111H" },
	{ RL_HWREV_8105E,	"RTL8105E" },
	{ RL_HWREV_8105E_SPIN1,	"RTL8105E" },
	{ RL_HWREV_8168D,	"RTL8168D/8111D" },
	{ RL_HWREV_8168DP,      "RTL8168DP/8111DP" },
	{ RL_HWREV_8168E,       "RTL8168E/8111E" },
	{ RL_HWREV_8168E_VL,	"RTL8168E/8111E-VL" },
	{ RL_HWREV_8168EP,	"RTL8168EP/8111EP" },
	{ RL_HWREV_8168FP,	"RTL8168FP/8111FP" },
	{ RL_HWREV_8169,	"RTL8169" },
	{ RL_HWREV_8169_8110SB,	"RTL8169/8110SB" },
	{ RL_HWREV_8169_8110SBL, "RTL8169SBL" },
	{ RL_HWREV_8169_8110SCd, "RTL8169/8110SCd" },
	{ RL_HWREV_8169_8110SCe, "RTL8169/8110SCe" },
	{ RL_HWREV_8169S,	"RTL8169S" },

	{ 0, NULL }
};


static inline void
re_set_bufaddr(struct rl_desc *d, bus_addr_t addr)
{
	d->rl_bufaddr_lo = htole32((uint32_t)addr);
	if (sizeof(bus_addr_t) == sizeof(uint64_t))
		d->rl_bufaddr_hi = htole32((uint64_t)addr >> 32);
	else
		d->rl_bufaddr_hi = 0;
}

/*
 * Send a read command and address to the EEPROM, check for ACK.
 */
void
re_eeprom_putbyte(struct rl_softc *sc, int addr)
{
	int	d, i;

	d = addr | (RL_9346_READ << sc->rl_eewidth);

	/*
	 * Feed in each bit and strobe the clock.
	 */

	for (i = 1 << (sc->rl_eewidth + 3); i; i >>= 1) {
		if (d & i)
			EE_SET(RL_EE_DATAIN);
		else
			EE_CLR(RL_EE_DATAIN);
		DELAY(100);
		EE_SET(RL_EE_CLK);
		DELAY(150);
		EE_CLR(RL_EE_CLK);
		DELAY(100);
	}
}

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
void
re_eeprom_getword(struct rl_softc *sc, int addr, u_int16_t *dest)
{
	int		i;
	u_int16_t	word = 0;

	/*
	 * Send address of word we want to read.
	 */
	re_eeprom_putbyte(sc, addr);

	/*
	 * Start reading bits from EEPROM.
	 */
	for (i = 0x8000; i; i >>= 1) {
		EE_SET(RL_EE_CLK);
		DELAY(100);
		if (CSR_READ_1(sc, RL_EECMD) & RL_EE_DATAOUT)
			word |= i;
		EE_CLR(RL_EE_CLK);
		DELAY(100);
	}

	*dest = word;
}

/*
 * Read a sequence of words from the EEPROM.
 */
void
re_read_eeprom(struct rl_softc *sc, caddr_t dest, int off, int cnt)
{
	int		i;
	u_int16_t	word = 0, *ptr;

	CSR_SETBIT_1(sc, RL_EECMD, RL_EEMODE_PROGRAM);

	DELAY(100);

	for (i = 0; i < cnt; i++) {
		CSR_SETBIT_1(sc, RL_EECMD, RL_EE_SEL);
		re_eeprom_getword(sc, off + i, &word);
		CSR_CLRBIT_1(sc, RL_EECMD, RL_EE_SEL);
		ptr = (u_int16_t *)(dest + (i * 2));
		*ptr = word;
	}

	CSR_CLRBIT_1(sc, RL_EECMD, RL_EEMODE_PROGRAM);
}

int
re_gmii_readreg(struct device *self, int phy, int reg)
{
	struct rl_softc	*sc = (struct rl_softc *)self;
	u_int32_t	rval;
	int		i;

	if (phy != 7)
		return (0);

	/* Let the rgephy driver read the GMEDIASTAT register */

	if (reg == RL_GMEDIASTAT) {
		rval = CSR_READ_1(sc, RL_GMEDIASTAT);
		return (rval);
	}

	CSR_WRITE_4(sc, RL_PHYAR, reg << 16);

	for (i = 0; i < RL_PHY_TIMEOUT; i++) {
		rval = CSR_READ_4(sc, RL_PHYAR);
		if (rval & RL_PHYAR_BUSY)
			break;
		DELAY(25);
	}

	if (i == RL_PHY_TIMEOUT) {
		printf ("%s: PHY read failed\n", sc->sc_dev.dv_xname);
		return (0);
	}

	DELAY(20);

	return (rval & RL_PHYAR_PHYDATA);
}

void
re_gmii_writereg(struct device *dev, int phy, int reg, int data)
{
	struct rl_softc	*sc = (struct rl_softc *)dev;
	u_int32_t	rval;
	int		i;

	CSR_WRITE_4(sc, RL_PHYAR, (reg << 16) |
	    (data & RL_PHYAR_PHYDATA) | RL_PHYAR_BUSY);

	for (i = 0; i < RL_PHY_TIMEOUT; i++) {
		rval = CSR_READ_4(sc, RL_PHYAR);
		if (!(rval & RL_PHYAR_BUSY))
			break;
		DELAY(25);
	}

	if (i == RL_PHY_TIMEOUT)
		printf ("%s: PHY write failed\n", sc->sc_dev.dv_xname);

	DELAY(20);
}

int
re_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct rl_softc	*sc = (struct rl_softc *)dev;
	u_int16_t	rval = 0;
	u_int16_t	re8139_reg = 0;
	int		s;

	s = splnet();

	if (sc->sc_hwrev != RL_HWREV_8139CPLUS) {
		rval = re_gmii_readreg(dev, phy, reg);
		splx(s);
		return (rval);
	}

	/* Pretend the internal PHY is only at address 0 */
	if (phy) {
		splx(s);
		return (0);
	}
	switch(reg) {
	case MII_BMCR:
		re8139_reg = RL_BMCR;
		break;
	case MII_BMSR:
		re8139_reg = RL_BMSR;
		break;
	case MII_ANAR:
		re8139_reg = RL_ANAR;
		break;
	case MII_ANER:
		re8139_reg = RL_ANER;
		break;
	case MII_ANLPAR:
		re8139_reg = RL_LPAR;
		break;
	case MII_PHYIDR1:
	case MII_PHYIDR2:
		splx(s);
		return (0);
	/*
	 * Allow the rlphy driver to read the media status
	 * register. If we have a link partner which does not
	 * support NWAY, this is the register which will tell
	 * us the results of parallel detection.
	 */
	case RL_MEDIASTAT:
		rval = CSR_READ_1(sc, RL_MEDIASTAT);
		splx(s);
		return (rval);
	default:
		printf("%s: bad phy register %x\n", sc->sc_dev.dv_xname, reg);
		splx(s);
		return (0);
	}
	rval = CSR_READ_2(sc, re8139_reg);
	if (re8139_reg == RL_BMCR) {
		/* 8139C+ has different bit layout. */
		rval &= ~(BMCR_LOOP | BMCR_ISO);
	}
	splx(s);
	return (rval);
}

void
re_miibus_writereg(struct device *dev, int phy, int reg, int data)
{
	struct rl_softc	*sc = (struct rl_softc *)dev;
	u_int16_t	re8139_reg = 0;
	int		s;

	s = splnet();

	if (sc->sc_hwrev != RL_HWREV_8139CPLUS) {
		re_gmii_writereg(dev, phy, reg, data);
		splx(s);
		return;
	}

	/* Pretend the internal PHY is only at address 0 */
	if (phy) {
		splx(s);
		return;
	}
	switch(reg) {
	case MII_BMCR:
		re8139_reg = RL_BMCR;
		/* 8139C+ has different bit layout. */
		data &= ~(BMCR_LOOP | BMCR_ISO);
		break;
	case MII_BMSR:
		re8139_reg = RL_BMSR;
		break;
	case MII_ANAR:
		re8139_reg = RL_ANAR;
		break;
	case MII_ANER:
		re8139_reg = RL_ANER;
		break;
	case MII_ANLPAR:
		re8139_reg = RL_LPAR;
		break;
	case MII_PHYIDR1:
	case MII_PHYIDR2:
		splx(s);
		return;
		break;
	default:
		printf("%s: bad phy register %x\n", sc->sc_dev.dv_xname, reg);
		splx(s);
		return;
	}
	CSR_WRITE_2(sc, re8139_reg, data);
	splx(s);
}

void
re_miibus_statchg(struct device *dev)
{
	struct rl_softc		*sc = (struct rl_softc *)dev;
	struct ifnet		*ifp = &sc->sc_arpcom.ac_if;
	struct mii_data		*mii = &sc->sc_mii;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	sc->rl_flags &= ~RL_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->rl_flags |= RL_FLAG_LINK;
			break;
		case IFM_1000_T:
			if ((sc->rl_flags & RL_FLAG_FASTETHER) != 0)
				break;
			sc->rl_flags |= RL_FLAG_LINK;
			break;
		default:
			break;
		}
	}

	/*
	 * Realtek controllers do not provide an interface to
	 * Tx/Rx MACs for resolved speed, duplex and flow-control
	 * parameters.
	 */
}

void
re_iff(struct rl_softc *sc)
{
	struct ifnet		*ifp = &sc->sc_arpcom.ac_if;
	int			h = 0;
	u_int32_t		hashes[2];
	u_int32_t		rxfilt;
	struct arpcom		*ac = &sc->sc_arpcom;
	struct ether_multi	*enm;
	struct ether_multistep	step;

	rxfilt = CSR_READ_4(sc, RL_RXCFG);
	rxfilt &= ~(RL_RXCFG_RX_ALLPHYS | RL_RXCFG_RX_BROAD |
	    RL_RXCFG_RX_INDIV | RL_RXCFG_RX_MULTI);
	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Always accept frames destined to our station address.
	 * Always accept broadcast frames.
	 */
	rxfilt |= RL_RXCFG_RX_INDIV | RL_RXCFG_RX_BROAD;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		rxfilt |= RL_RXCFG_RX_MULTI;
		if (ifp->if_flags & IFF_PROMISC)
			rxfilt |= RL_RXCFG_RX_ALLPHYS;
		hashes[0] = hashes[1] = 0xFFFFFFFF;
	} else {
		rxfilt |= RL_RXCFG_RX_MULTI;
		/* Program new filter. */
		bzero(hashes, sizeof(hashes));

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			h = ether_crc32_be(enm->enm_addrlo,
			    ETHER_ADDR_LEN) >> 26;

			if (h < 32)
				hashes[0] |= (1 << h);
			else
				hashes[1] |= (1 << (h - 32));

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	/*
	 * For some unfathomable reason, Realtek decided to reverse
	 * the order of the multicast hash registers in the PCI Express
	 * parts. This means we have to write the hash pattern in reverse
	 * order for those devices.
	 */
	if (sc->rl_flags & RL_FLAG_PCIE) {
		CSR_WRITE_4(sc, RL_MAR0, swap32(hashes[1]));
		CSR_WRITE_4(sc, RL_MAR4, swap32(hashes[0]));
	} else {
		CSR_WRITE_4(sc, RL_MAR0, hashes[0]);
		CSR_WRITE_4(sc, RL_MAR4, hashes[1]);
	}

	CSR_WRITE_4(sc, RL_RXCFG, rxfilt);
}

void
re_reset(struct rl_softc *sc)
{
	int	i;

	CSR_WRITE_1(sc, RL_COMMAND, RL_CMD_RESET);

	for (i = 0; i < RL_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_1(sc, RL_COMMAND) & RL_CMD_RESET))
			break;
	}
	if (i == RL_TIMEOUT)
		printf("%s: reset never completed!\n", sc->sc_dev.dv_xname);

	if (sc->rl_flags & RL_FLAG_MACRESET)
		CSR_WRITE_1(sc, RL_LDPS, 1);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
int
re_attach(struct rl_softc *sc, const char *intrstr)
{
	u_char		eaddr[ETHER_ADDR_LEN];
	u_int16_t	as[ETHER_ADDR_LEN / 2];
	struct ifnet	*ifp;
	u_int16_t	re_did = 0;
	int		error = 0, i;
	const struct re_revision *rr;
	const char	*re_name = NULL;

	sc->sc_hwrev = CSR_READ_4(sc, RL_TXCFG) & RL_TXCFG_HWREV;

	switch (sc->sc_hwrev) {
	case RL_HWREV_8139CPLUS:
		sc->rl_flags |= RL_FLAG_FASTETHER | RL_FLAG_AUTOPAD;
		sc->rl_max_mtu = RL_MTU;
		break;
	case RL_HWREV_8100E:
	case RL_HWREV_8100E_SPIN2:
	case RL_HWREV_8101E:
		sc->rl_flags |= RL_FLAG_PHYWAKE | RL_FLAG_FASTETHER;
		sc->rl_max_mtu = RL_MTU;
		break;
	case RL_HWREV_8103E:
		sc->rl_flags |= RL_FLAG_MACSLEEP;
		/* FALLTHROUGH */
	case RL_HWREV_8102E:
	case RL_HWREV_8102EL:
	case RL_HWREV_8102EL_SPIN1:
		sc->rl_flags |= RL_FLAG_PHYWAKE | RL_FLAG_PAR |
		    RL_FLAG_DESCV2 | RL_FLAG_MACSTAT | RL_FLAG_FASTETHER |
		    RL_FLAG_CMDSTOP | RL_FLAG_AUTOPAD;
		sc->rl_max_mtu = RL_MTU;
		break;
	case RL_HWREV_8401E:
	case RL_HWREV_8105E:
	case RL_HWREV_8105E_SPIN1:
	case RL_HWREV_8106E:
		sc->rl_flags |= RL_FLAG_PHYWAKE | RL_FLAG_PHYWAKE_PM |
		    RL_FLAG_PAR | RL_FLAG_DESCV2 | RL_FLAG_MACSTAT |
		    RL_FLAG_FASTETHER | RL_FLAG_CMDSTOP | RL_FLAG_AUTOPAD;
		sc->rl_max_mtu = RL_MTU;
		break;
	case RL_HWREV_8402:
		sc->rl_flags |= RL_FLAG_PHYWAKE | RL_FLAG_PHYWAKE_PM |
		    RL_FLAG_PAR | RL_FLAG_DESCV2 | RL_FLAG_MACSTAT |
		    RL_FLAG_FASTETHER | RL_FLAG_CMDSTOP | RL_FLAG_AUTOPAD |
		    RL_FLAG_CMDSTOP_WAIT_TXQ;
		sc->rl_max_mtu = RL_MTU;
		break;
	case RL_HWREV_8168B_SPIN1:
	case RL_HWREV_8168B_SPIN2:
		sc->rl_flags |= RL_FLAG_WOLRXENB;
		/* FALLTHROUGH */
	case RL_HWREV_8168B_SPIN3:
		sc->rl_flags |= RL_FLAG_PHYWAKE | RL_FLAG_MACSTAT;
		sc->rl_max_mtu = RL_MTU;
		break;
	case RL_HWREV_8168C_SPIN2:
		sc->rl_flags |= RL_FLAG_MACSLEEP;
		/* FALLTHROUGH */
	case RL_HWREV_8168C:
	case RL_HWREV_8168CP:
		sc->rl_flags |= RL_FLAG_PHYWAKE | RL_FLAG_PAR |
		    RL_FLAG_DESCV2 | RL_FLAG_MACSTAT | RL_FLAG_CMDSTOP |
		    RL_FLAG_AUTOPAD | RL_FLAG_JUMBOV2 | RL_FLAG_WOL_MANLINK;
		sc->rl_max_mtu = RL_JUMBO_MTU_6K;
		break;
	case RL_HWREV_8168D:
		sc->rl_flags |= RL_FLAG_PHYWAKE | RL_FLAG_PHYWAKE_PM |
		    RL_FLAG_PAR | RL_FLAG_DESCV2 | RL_FLAG_MACSTAT |
		    RL_FLAG_CMDSTOP | RL_FLAG_AUTOPAD | RL_FLAG_JUMBOV2 |
		    RL_FLAG_WOL_MANLINK;
		sc->rl_max_mtu = RL_JUMBO_MTU_9K;
		break;
	case RL_HWREV_8168DP:
		sc->rl_flags |= RL_FLAG_PHYWAKE | RL_FLAG_PAR |
		    RL_FLAG_DESCV2 | RL_FLAG_MACSTAT | RL_FLAG_AUTOPAD |
		    RL_FLAG_JUMBOV2 | RL_FLAG_WAIT_TXPOLL | RL_FLAG_WOL_MANLINK;
		sc->rl_max_mtu = RL_JUMBO_MTU_9K;
		break;
	case RL_HWREV_8168E:
		sc->rl_flags |= RL_FLAG_PHYWAKE | RL_FLAG_PHYWAKE_PM |
		    RL_FLAG_PAR | RL_FLAG_DESCV2 | RL_FLAG_MACSTAT |
		    RL_FLAG_CMDSTOP | RL_FLAG_AUTOPAD | RL_FLAG_JUMBOV2 |
		    RL_FLAG_WOL_MANLINK;
		sc->rl_max_mtu = RL_JUMBO_MTU_9K;
		break;
	case RL_HWREV_8168E_VL:
		sc->rl_flags |= RL_FLAG_EARLYOFF | RL_FLAG_PHYWAKE | RL_FLAG_PAR |
		    RL_FLAG_DESCV2 | RL_FLAG_MACSTAT | RL_FLAG_CMDSTOP |
		    RL_FLAG_AUTOPAD | RL_FLAG_JUMBOV2 | RL_FLAG_CMDSTOP_WAIT_TXQ |
		    RL_FLAG_WOL_MANLINK;
		sc->rl_max_mtu = RL_JUMBO_MTU_6K;
		break;
	case RL_HWREV_8168F:
		sc->rl_flags |= RL_FLAG_EARLYOFF;
		/* FALLTHROUGH */
	case RL_HWREV_8411:
		sc->rl_flags |= RL_FLAG_PHYWAKE | RL_FLAG_PAR |
		    RL_FLAG_DESCV2 | RL_FLAG_MACSTAT | RL_FLAG_CMDSTOP |
		    RL_FLAG_AUTOPAD | RL_FLAG_JUMBOV2 | RL_FLAG_CMDSTOP_WAIT_TXQ |
		    RL_FLAG_WOL_MANLINK;
		sc->rl_max_mtu = RL_JUMBO_MTU_9K;
		break;
	case RL_HWREV_8168EP:
	case RL_HWREV_8168FP:
	case RL_HWREV_8168G:
	case RL_HWREV_8168GU:
	case RL_HWREV_8168H:
	case RL_HWREV_8411B:
		if (sc->sc_product == PCI_PRODUCT_REALTEK_RT8101E) {
			/* RTL8106EUS */
			sc->rl_flags |= RL_FLAG_FASTETHER;
			sc->rl_max_mtu = RL_MTU;
		} else {
			sc->rl_flags |= RL_FLAG_JUMBOV2 | RL_FLAG_WOL_MANLINK;
			sc->rl_max_mtu = RL_JUMBO_MTU_9K;
		}

		sc->rl_flags |= RL_FLAG_PHYWAKE | RL_FLAG_PAR |
		    RL_FLAG_DESCV2 | RL_FLAG_MACSTAT | RL_FLAG_CMDSTOP |
		    RL_FLAG_AUTOPAD | RL_FLAG_CMDSTOP_WAIT_TXQ |
		    RL_FLAG_EARLYOFFV2 | RL_FLAG_RXDV_GATED;
		break;
	case RL_HWREV_8169_8110SB:
	case RL_HWREV_8169_8110SBL:
	case RL_HWREV_8169_8110SCd:
	case RL_HWREV_8169_8110SCe:
		sc->rl_flags |= RL_FLAG_PHYWAKE;
		/* FALLTHROUGH */
	case RL_HWREV_8169:
	case RL_HWREV_8169S:
	case RL_HWREV_8110S:
		sc->rl_flags |= RL_FLAG_MACRESET;
		sc->rl_max_mtu = RL_JUMBO_MTU_7K;
		break;
	default:
		break;
	}

	if (sc->sc_hwrev == RL_HWREV_8139CPLUS) {
		sc->rl_cfg0 = RL_8139_CFG0;
		sc->rl_cfg1 = RL_8139_CFG1;
		sc->rl_cfg2 = 0;
		sc->rl_cfg3 = RL_8139_CFG3;
		sc->rl_cfg4 = RL_8139_CFG4;
		sc->rl_cfg5 = RL_8139_CFG5;
	} else {
		sc->rl_cfg0 = RL_CFG0;
		sc->rl_cfg1 = RL_CFG1;
		sc->rl_cfg2 = RL_CFG2;
		sc->rl_cfg3 = RL_CFG3;
		sc->rl_cfg4 = RL_CFG4;
		sc->rl_cfg5 = RL_CFG5;
	}

	/* Reset the adapter. */
	re_reset(sc);

	sc->rl_tx_time = 5;		/* 125us */
	sc->rl_rx_time = 2;		/* 50us */
	if (sc->rl_flags & RL_FLAG_PCIE)
		sc->rl_sim_time = 75;	/* 75us */
	else
		sc->rl_sim_time = 125;	/* 125us */
	sc->rl_imtype = RL_IMTYPE_SIM;	/* simulated interrupt moderation */

	if (sc->sc_hwrev == RL_HWREV_8139CPLUS)
		sc->rl_bus_speed = 33; /* XXX */
	else if (sc->rl_flags & RL_FLAG_PCIE)
		sc->rl_bus_speed = 125;
	else {
		u_int8_t cfg2;

		cfg2 = CSR_READ_1(sc, sc->rl_cfg2);
		switch (cfg2 & RL_CFG2_PCI_MASK) {
		case RL_CFG2_PCI_33MHZ:
			sc->rl_bus_speed = 33;
			break;
		case RL_CFG2_PCI_66MHZ:
			sc->rl_bus_speed = 66;
			break;
		default:
			printf("%s: unknown bus speed, assume 33MHz\n",
			    sc->sc_dev.dv_xname);
			sc->rl_bus_speed = 33;
			break;
		}

		if (cfg2 & RL_CFG2_PCI_64BIT)
			sc->rl_flags |= RL_FLAG_PCI64;
	}

	re_config_imtype(sc, sc->rl_imtype);

	if (sc->rl_flags & RL_FLAG_PAR) {
		/*
		 * XXX Should have a better way to extract station
		 * address from EEPROM.
		 */
		for (i = 0; i < ETHER_ADDR_LEN; i++)
			eaddr[i] = CSR_READ_1(sc, RL_IDR0 + i);
	} else {
		sc->rl_eewidth = RL_9356_ADDR_LEN;
		re_read_eeprom(sc, (caddr_t)&re_did, 0, 1);
		if (re_did != 0x8129)
			sc->rl_eewidth = RL_9346_ADDR_LEN;

		/*
		 * Get station address from the EEPROM.
		 */
		re_read_eeprom(sc, (caddr_t)as, RL_EE_EADDR, 3);
		for (i = 0; i < ETHER_ADDR_LEN / 2; i++)
			as[i] = letoh16(as[i]);
		bcopy(as, eaddr, ETHER_ADDR_LEN);
	}

	/*
	 * Set RX length mask, TX poll request register
	 * and descriptor count.
	 */
	if (sc->sc_hwrev == RL_HWREV_8139CPLUS) {
		sc->rl_rxlenmask = RL_RDESC_STAT_FRAGLEN;
		sc->rl_txstart = RL_TXSTART;
		sc->rl_ldata.rl_tx_desc_cnt = RL_8139_TX_DESC_CNT;
		sc->rl_ldata.rl_rx_desc_cnt = RL_8139_RX_DESC_CNT;
		sc->rl_ldata.rl_tx_ndescs = RL_8139_NTXSEGS;
	} else {
		sc->rl_rxlenmask = RL_RDESC_STAT_GFRAGLEN;
		sc->rl_txstart = RL_GTXSTART;
		sc->rl_ldata.rl_tx_desc_cnt = RL_8169_TX_DESC_CNT;
		sc->rl_ldata.rl_rx_desc_cnt = RL_8169_RX_DESC_CNT;
		sc->rl_ldata.rl_tx_ndescs = RL_8169_NTXSEGS;
	}

	bcopy(eaddr, (char *)&sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN);

	for (rr = re_revisions; rr->re_name != NULL; rr++) {
		if (rr->re_chipid == sc->sc_hwrev)
			re_name = rr->re_name;
	}

	if (re_name == NULL)
		printf(": unknown ASIC (0x%04x)", sc->sc_hwrev >> 16);
	else
		printf(": %s (0x%04x)", re_name, sc->sc_hwrev >> 16);

	printf(", %s, address %s\n", intrstr,
	    ether_sprintf(sc->sc_arpcom.ac_enaddr));

	/* Allocate DMA'able memory for the TX ring */
	if ((error = bus_dmamem_alloc(sc->sc_dmat, RL_TX_LIST_SZ(sc),
		    RL_RING_ALIGN, 0, &sc->rl_ldata.rl_tx_listseg, 1,
		    &sc->rl_ldata.rl_tx_listnseg, BUS_DMA_NOWAIT |
		    BUS_DMA_ZERO)) != 0) {
		printf("%s: can't allocate tx listseg, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_0;
	}

	/* Load the map for the TX ring. */
	if ((error = bus_dmamem_map(sc->sc_dmat, &sc->rl_ldata.rl_tx_listseg,
		    sc->rl_ldata.rl_tx_listnseg, RL_TX_LIST_SZ(sc),
		    (caddr_t *)&sc->rl_ldata.rl_tx_list,
		    BUS_DMA_COHERENT | BUS_DMA_NOWAIT)) != 0) {
		printf("%s: can't map tx list, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_1;
	}

	if ((error = bus_dmamap_create(sc->sc_dmat, RL_TX_LIST_SZ(sc), 1,
		    RL_TX_LIST_SZ(sc), 0, 0,
		    &sc->rl_ldata.rl_tx_list_map)) != 0) {
		printf("%s: can't create tx list map, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_2;
	}

	if ((error = bus_dmamap_load(sc->sc_dmat,
		    sc->rl_ldata.rl_tx_list_map, sc->rl_ldata.rl_tx_list,
		    RL_TX_LIST_SZ(sc), NULL, BUS_DMA_NOWAIT)) != 0) {
		printf("%s: can't load tx list, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_3;
	}

	/* Create DMA maps for TX buffers */
	for (i = 0; i < sc->rl_ldata.rl_tx_desc_cnt; i++) {
		error = bus_dmamap_create(sc->sc_dmat,
		    RL_JUMBO_FRAMELEN, sc->rl_ldata.rl_tx_ndescs,
		    RL_JUMBO_FRAMELEN, 0, 0,
		    &sc->rl_ldata.rl_txq[i].txq_dmamap);
		if (error) {
			printf("%s: can't create DMA map for TX\n",
			    sc->sc_dev.dv_xname);
			goto fail_4;
		}
	}

        /* Allocate DMA'able memory for the RX ring */
	if ((error = bus_dmamem_alloc(sc->sc_dmat, RL_RX_DMAMEM_SZ(sc),
		    RL_RING_ALIGN, 0, &sc->rl_ldata.rl_rx_listseg, 1,
		    &sc->rl_ldata.rl_rx_listnseg, BUS_DMA_NOWAIT |
		    BUS_DMA_ZERO)) != 0) {
		printf("%s: can't allocate rx listnseg, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_4;
	}

        /* Load the map for the RX ring. */
	if ((error = bus_dmamem_map(sc->sc_dmat, &sc->rl_ldata.rl_rx_listseg,
		    sc->rl_ldata.rl_rx_listnseg, RL_RX_DMAMEM_SZ(sc),
		    (caddr_t *)&sc->rl_ldata.rl_rx_list,
		    BUS_DMA_COHERENT | BUS_DMA_NOWAIT)) != 0) {
		printf("%s: can't map rx list, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_5;

	}

	if ((error = bus_dmamap_create(sc->sc_dmat, RL_RX_DMAMEM_SZ(sc), 1,
		    RL_RX_DMAMEM_SZ(sc), 0, 0,
		    &sc->rl_ldata.rl_rx_list_map)) != 0) {
		printf("%s: can't create rx list map, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_6;
	}

	if ((error = bus_dmamap_load(sc->sc_dmat,
		    sc->rl_ldata.rl_rx_list_map, sc->rl_ldata.rl_rx_list,
		    RL_RX_DMAMEM_SZ(sc), NULL, BUS_DMA_NOWAIT)) != 0) {
		printf("%s: can't load rx list, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_7;
	}

	/* Create DMA maps for RX buffers */
	for (i = 0; i < sc->rl_ldata.rl_rx_desc_cnt; i++) {
		error = bus_dmamap_create(sc->sc_dmat,
		    RL_FRAMELEN(sc->rl_max_mtu), 1,
		    RL_FRAMELEN(sc->rl_max_mtu), 0, 0,
		    &sc->rl_ldata.rl_rxsoft[i].rxs_dmamap);
		if (error) {
			printf("%s: can't create DMA map for RX\n",
			    sc->sc_dev.dv_xname);
			goto fail_8;
		}
	}

	ifp = &sc->sc_arpcom.ac_if;
	ifp->if_softc = sc;
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_MPSAFE;
	ifp->if_ioctl = re_ioctl;
	ifp->if_qstart = re_start;
	ifp->if_watchdog = re_watchdog;
	ifp->if_hardmtu = sc->rl_max_mtu;
	ifq_init_maxlen(&ifp->if_snd, sc->rl_ldata.rl_tx_desc_cnt);

	ifp->if_capabilities = IFCAP_VLAN_MTU | IFCAP_CSUM_TCPv4 |
	    IFCAP_CSUM_UDPv4;

	/*
	 * RTL8168/8111C generates wrong IP checksummed frame if the
	 * packet has IP options so disable TX IP checksum offloading.
	 */
	switch (sc->sc_hwrev) {
	case RL_HWREV_8168C:
	case RL_HWREV_8168C_SPIN2:
	case RL_HWREV_8168CP:
		break;
	default:
		ifp->if_capabilities |= IFCAP_CSUM_IPv4;
	}

#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif

#ifndef SMALL_KERNEL
	ifp->if_capabilities |= IFCAP_WOL;
	ifp->if_wol = re_wol;
	re_wol(ifp, 0);
#endif
	timeout_set(&sc->timer_handle, re_tick, sc);
	task_set(&sc->rl_start, re_txstart, sc);

	/* Take PHY out of power down mode. */
	if (sc->rl_flags & RL_FLAG_PHYWAKE_PM) {
		CSR_WRITE_1(sc, RL_PMCH, CSR_READ_1(sc, RL_PMCH) | 0x80);
		if (sc->sc_hwrev == RL_HWREV_8401E)
			CSR_WRITE_1(sc, 0xD1, CSR_READ_1(sc, 0xD1) & ~0x08);
	}
	if (sc->rl_flags & RL_FLAG_PHYWAKE) {
		re_gmii_writereg((struct device *)sc, 1, 0x1f, 0);
		re_gmii_writereg((struct device *)sc, 1, 0x0e, 0);
	}

	/* Do MII setup */
	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = re_miibus_readreg;
	sc->sc_mii.mii_writereg = re_miibus_writereg;
	sc->sc_mii.mii_statchg = re_miibus_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, IFM_IMASK, re_ifmedia_upd,
	    re_ifmedia_sts);
	mii_attach(&sc->sc_dev, &sc->sc_mii, 0xffffffff, MII_PHY_ANY,
	    MII_OFFSET_ANY, MIIF_DOPAUSE);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		printf("%s: no PHY found!\n", sc->sc_dev.dv_xname);
		ifmedia_add(&sc->sc_mii.mii_media,
		    IFM_ETHER|IFM_NONE, 0, NULL);
		ifmedia_set(&sc->sc_mii.mii_media,
		    IFM_ETHER|IFM_NONE);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);

	/*
	 * Call MI attach routine.
	 */
	if_attach(ifp);
	ether_ifattach(ifp);

#if NKSTAT > 0
	re_kstat_attach(sc);
#endif

	return (0);

fail_8:
	/* Destroy DMA maps for RX buffers. */
	for (i = 0; i < sc->rl_ldata.rl_rx_desc_cnt; i++) {
		if (sc->rl_ldata.rl_rxsoft[i].rxs_dmamap != NULL)
			bus_dmamap_destroy(sc->sc_dmat,
			    sc->rl_ldata.rl_rxsoft[i].rxs_dmamap);
	}

	/* Free DMA'able memory for the RX ring. */
	bus_dmamap_unload(sc->sc_dmat, sc->rl_ldata.rl_rx_list_map);
fail_7:
	bus_dmamap_destroy(sc->sc_dmat, sc->rl_ldata.rl_rx_list_map);
fail_6:
	bus_dmamem_unmap(sc->sc_dmat,
	    (caddr_t)sc->rl_ldata.rl_rx_list, RL_RX_DMAMEM_SZ(sc));
fail_5:
	bus_dmamem_free(sc->sc_dmat,
	    &sc->rl_ldata.rl_rx_listseg, sc->rl_ldata.rl_rx_listnseg);

fail_4:
	/* Destroy DMA maps for TX buffers. */
	for (i = 0; i < sc->rl_ldata.rl_tx_desc_cnt; i++) {
		if (sc->rl_ldata.rl_txq[i].txq_dmamap != NULL)
			bus_dmamap_destroy(sc->sc_dmat,
			    sc->rl_ldata.rl_txq[i].txq_dmamap);
	}

	/* Free DMA'able memory for the TX ring. */
	bus_dmamap_unload(sc->sc_dmat, sc->rl_ldata.rl_tx_list_map);
fail_3:
	bus_dmamap_destroy(sc->sc_dmat, sc->rl_ldata.rl_tx_list_map);
fail_2:
	bus_dmamem_unmap(sc->sc_dmat,
	    (caddr_t)sc->rl_ldata.rl_tx_list, RL_TX_LIST_SZ(sc));
fail_1:
	bus_dmamem_free(sc->sc_dmat,
	    &sc->rl_ldata.rl_tx_listseg, sc->rl_ldata.rl_tx_listnseg);
fail_0:
	return (1);
}

void
re_detach(struct rl_softc *sc)
{
	struct ifnet	*ifp = &sc->sc_arpcom.ac_if;

#if NKSTAT > 0
	re_kstat_detach(sc);
#endif

	/* Remove timeout handler */
	timeout_del(&sc->timer_handle);

	/* Detach PHY */
	if (LIST_FIRST(&sc->sc_mii.mii_phys) != NULL)
		mii_detach(&sc->sc_mii, MII_PHY_ANY, MII_OFFSET_ANY);

	/* Delete media stuff */
	ifmedia_delete_instance(&sc->sc_mii.mii_media, IFM_INST_ANY);
	ether_ifdetach(ifp);
	if_detach(ifp);
}

int
re_newbuf(struct rl_softc *sc)
{
	struct mbuf	*m;
	bus_dmamap_t	map;
	struct rl_desc	*d;
	struct rl_rxsoft *rxs;
	u_int32_t	cmdstat;
	int		error, idx;

	m = MCLGETL(NULL, M_DONTWAIT, RL_FRAMELEN(sc->rl_max_mtu));
	if (!m)
		return (ENOBUFS);

	/*
	 * Initialize mbuf length fields and fixup
	 * alignment so that the frame payload is
	 * longword aligned on strict alignment archs.
	 */
	m->m_len = m->m_pkthdr.len = RL_FRAMELEN(sc->rl_max_mtu);
	m->m_data += RE_ETHER_ALIGN;

	idx = sc->rl_ldata.rl_rx_prodidx;
	rxs = &sc->rl_ldata.rl_rxsoft[idx];
	map = rxs->rxs_dmamap;
	error = bus_dmamap_load_mbuf(sc->sc_dmat, map, m,
	    BUS_DMA_READ|BUS_DMA_NOWAIT);
	if (error) {
		m_freem(m);
		return (ENOBUFS);
	}

	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREREAD);

	d = &sc->rl_ldata.rl_rx_list[idx];
	RL_RXDESCSYNC(sc, idx, BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
	cmdstat = letoh32(d->rl_cmdstat);
	RL_RXDESCSYNC(sc, idx, BUS_DMASYNC_PREREAD);
	if (cmdstat & RL_RDESC_STAT_OWN) {
		printf("%s: tried to map busy RX descriptor\n",
		    sc->sc_dev.dv_xname);
		m_freem(m);
		return (ENOBUFS);
	}

	rxs->rxs_mbuf = m;

	d->rl_vlanctl = 0;
	cmdstat = map->dm_segs[0].ds_len;
	if (idx == sc->rl_ldata.rl_rx_desc_cnt - 1)
		cmdstat |= RL_RDESC_CMD_EOR;
	re_set_bufaddr(d, map->dm_segs[0].ds_addr);
	d->rl_cmdstat = htole32(cmdstat);
	RL_RXDESCSYNC(sc, idx, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
	cmdstat |= RL_RDESC_CMD_OWN;
	d->rl_cmdstat = htole32(cmdstat);
	RL_RXDESCSYNC(sc, idx, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	sc->rl_ldata.rl_rx_prodidx = RL_NEXT_RX_DESC(sc, idx);

	return (0);
}


int
re_tx_list_init(struct rl_softc *sc)
{
	int i;

	memset(sc->rl_ldata.rl_tx_list, 0, RL_TX_LIST_SZ(sc));
	for (i = 0; i < sc->rl_ldata.rl_tx_desc_cnt; i++) {
		sc->rl_ldata.rl_txq[i].txq_mbuf = NULL;
	}

	bus_dmamap_sync(sc->sc_dmat,
	    sc->rl_ldata.rl_tx_list_map, 0,
	    sc->rl_ldata.rl_tx_list_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
	sc->rl_ldata.rl_txq_prodidx = 0;
	sc->rl_ldata.rl_txq_considx = 0;
	sc->rl_ldata.rl_tx_free = sc->rl_ldata.rl_tx_desc_cnt;
	sc->rl_ldata.rl_tx_nextfree = 0;

	return (0);
}

int
re_rx_list_init(struct rl_softc *sc)
{
	bzero(sc->rl_ldata.rl_rx_list, RL_RX_LIST_SZ(sc));

	sc->rl_ldata.rl_rx_prodidx = 0;
	sc->rl_ldata.rl_rx_considx = 0;
	sc->rl_head = sc->rl_tail = NULL;

	if_rxr_init(&sc->rl_ldata.rl_rx_ring, 2,
	    sc->rl_ldata.rl_rx_desc_cnt - 1);
	re_rx_list_fill(sc);

	return (0);
}

void
re_rx_list_fill(struct rl_softc *sc)
{
	u_int slots;

	for (slots = if_rxr_get(&sc->rl_ldata.rl_rx_ring,
	    sc->rl_ldata.rl_rx_desc_cnt);
	    slots > 0; slots--) {
		if (re_newbuf(sc) == ENOBUFS)
			break;
	}
	if_rxr_put(&sc->rl_ldata.rl_rx_ring, slots);
}

/*
 * RX handler for C+ and 8169. For the gigE chips, we support
 * the reception of jumbo frames that have been fragmented
 * across multiple 2K mbuf cluster buffers.
 */
int
re_rxeof(struct rl_softc *sc)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf	*m;
	struct ifnet	*ifp;
	int		i, total_len, rx = 0;
	struct rl_desc	*cur_rx;
	struct rl_rxsoft *rxs;
	u_int32_t	rxstat, rxvlan;

	ifp = &sc->sc_arpcom.ac_if;

	for (i = sc->rl_ldata.rl_rx_considx;
	    if_rxr_inuse(&sc->rl_ldata.rl_rx_ring) > 0;
	     i = RL_NEXT_RX_DESC(sc, i)) {
		cur_rx = &sc->rl_ldata.rl_rx_list[i];
		RL_RXDESCSYNC(sc, i,
		    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
		rxstat = letoh32(cur_rx->rl_cmdstat);
		rxvlan = letoh32(cur_rx->rl_vlanctl);
		RL_RXDESCSYNC(sc, i, BUS_DMASYNC_PREREAD);
		if ((rxstat & RL_RDESC_STAT_OWN) != 0)
			break;
		total_len = rxstat & sc->rl_rxlenmask;
		rxs = &sc->rl_ldata.rl_rxsoft[i];
		m = rxs->rxs_mbuf;
		rxs->rxs_mbuf = NULL;
		if_rxr_put(&sc->rl_ldata.rl_rx_ring, 1);
		rx = 1;

		/* Invalidate the RX mbuf and unload its map */

		bus_dmamap_sync(sc->sc_dmat,
		    rxs->rxs_dmamap, 0, rxs->rxs_dmamap->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, rxs->rxs_dmamap);

		if ((sc->rl_flags & RL_FLAG_JUMBOV2) != 0 &&
		    (rxstat & (RL_RDESC_STAT_SOF | RL_RDESC_STAT_EOF)) !=
		    (RL_RDESC_STAT_SOF | RL_RDESC_STAT_EOF)) {
			ifp->if_ierrors++;
			m_freem(m);
			continue;
		} else if (!(rxstat & RL_RDESC_STAT_EOF)) {
			m->m_len = RL_FRAMELEN(sc->rl_max_mtu);
			if (sc->rl_head == NULL)
				sc->rl_head = sc->rl_tail = m;
			else {
				m->m_flags &= ~M_PKTHDR;
				sc->rl_tail->m_next = m;
				sc->rl_tail = m;
			}
			continue;
		}

		/*
		 * NOTE: for the 8139C+, the frame length field
		 * is always 12 bits in size, but for the gigE chips,
		 * it is 13 bits (since the max RX frame length is 16K).
		 * Unfortunately, all 32 bits in the status word
		 * were already used, so to make room for the extra
		 * length bit, Realtek took out the 'frame alignment
		 * error' bit and shifted the other status bits
		 * over one slot. The OWN, EOR, FS and LS bits are
		 * still in the same places. We have already extracted
		 * the frame length and checked the OWN bit, so rather
		 * than using an alternate bit mapping, we shift the
		 * status bits one space to the right so we can evaluate
		 * them using the 8169 status as though it was in the
		 * same format as that of the 8139C+.
		 */
		if (sc->sc_hwrev != RL_HWREV_8139CPLUS)
			rxstat >>= 1;

		/*
		 * if total_len > 2^13-1, both _RXERRSUM and _GIANT will be
		 * set, but if CRC is clear, it will still be a valid frame.
		 */
		if ((rxstat & RL_RDESC_STAT_RXERRSUM) != 0 &&
		    !(rxstat & RL_RDESC_STAT_RXERRSUM && !(total_len > 8191 &&
		    (rxstat & RL_RDESC_STAT_ERRS) == RL_RDESC_STAT_GIANT))) {
			ifp->if_ierrors++;
			/*
			 * If this is part of a multi-fragment packet,
			 * discard all the pieces.
			 */
			if (sc->rl_head != NULL) {
				m_freem(sc->rl_head);
				sc->rl_head = sc->rl_tail = NULL;
			}
			m_freem(m);
			continue;
		}

		if (sc->rl_head != NULL) {
			m->m_len = total_len % RL_FRAMELEN(sc->rl_max_mtu);
			if (m->m_len == 0)
				m->m_len = RL_FRAMELEN(sc->rl_max_mtu);
			/*
			 * Special case: if there's 4 bytes or less
			 * in this buffer, the mbuf can be discarded:
			 * the last 4 bytes is the CRC, which we don't
			 * care about anyway.
			 */
			if (m->m_len <= ETHER_CRC_LEN) {
				sc->rl_tail->m_len -=
				    (ETHER_CRC_LEN - m->m_len);
				m_freem(m);
			} else {
				m->m_len -= ETHER_CRC_LEN;
				m->m_flags &= ~M_PKTHDR;
				sc->rl_tail->m_next = m;
			}
			m = sc->rl_head;
			sc->rl_head = sc->rl_tail = NULL;
			m->m_pkthdr.len = total_len - ETHER_CRC_LEN;
		} else
			m->m_pkthdr.len = m->m_len =
			    (total_len - ETHER_CRC_LEN);

		/* Do RX checksumming */

		if (sc->rl_flags & RL_FLAG_DESCV2) {
			/* Check IP header checksum */
			if ((rxvlan & RL_RDESC_IPV4) &&
			    !(rxstat & RL_RDESC_STAT_IPSUMBAD))
				m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;

			/* Check TCP/UDP checksum */
			if ((rxvlan & (RL_RDESC_IPV4|RL_RDESC_IPV6)) &&
			    (((rxstat & RL_RDESC_STAT_TCP) &&
			    !(rxstat & RL_RDESC_STAT_TCPSUMBAD)) ||
			    ((rxstat & RL_RDESC_STAT_UDP) &&
			    !(rxstat & RL_RDESC_STAT_UDPSUMBAD))))
				m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_OK |
				    M_UDP_CSUM_IN_OK;
		} else {
			/* Check IP header checksum */
			if ((rxstat & RL_RDESC_STAT_PROTOID) &&
			    !(rxstat & RL_RDESC_STAT_IPSUMBAD))
				m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;

			/* Check TCP/UDP checksum */
			if ((RL_TCPPKT(rxstat) &&
			    !(rxstat & RL_RDESC_STAT_TCPSUMBAD)) ||
			    (RL_UDPPKT(rxstat) &&
			    !(rxstat & RL_RDESC_STAT_UDPSUMBAD)))
				m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_OK |
				    M_UDP_CSUM_IN_OK;
		}
#if NVLAN > 0
		if (rxvlan & RL_RDESC_VLANCTL_TAG) {
			m->m_pkthdr.ether_vtag =
			    ntohs((rxvlan & RL_RDESC_VLANCTL_DATA));
			m->m_flags |= M_VLANTAG;
		}
#endif

		ml_enqueue(&ml, m);
	}

	if (ifiq_input(&ifp->if_rcv, &ml))
		if_rxr_livelocked(&sc->rl_ldata.rl_rx_ring);

	sc->rl_ldata.rl_rx_considx = i;
	re_rx_list_fill(sc);


	return (rx);
}

int
re_txeof(struct rl_softc *sc)
{
	struct ifnet	*ifp = &sc->sc_arpcom.ac_if;
	struct rl_txq	*txq;
	uint32_t	txstat;
	unsigned int	prod, cons;
	unsigned int	idx;
	int		free = 0;

	prod = sc->rl_ldata.rl_txq_prodidx;
	cons = sc->rl_ldata.rl_txq_considx;

	while (prod != cons) {
		txq = &sc->rl_ldata.rl_txq[cons];

		idx = txq->txq_descidx;
		RL_TXDESCSYNC(sc, idx, BUS_DMASYNC_POSTREAD);
		txstat = letoh32(sc->rl_ldata.rl_tx_list[idx].rl_cmdstat);
		RL_TXDESCSYNC(sc, idx, BUS_DMASYNC_PREREAD);
		if (ISSET(txstat, RL_TDESC_CMD_OWN)) {
			free = 2;
			break;
		}

		bus_dmamap_sync(sc->sc_dmat, txq->txq_dmamap,
		    0, txq->txq_dmamap->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, txq->txq_dmamap);
		m_freem(txq->txq_mbuf);
		txq->txq_mbuf = NULL;

		if (txstat & (RL_TDESC_STAT_EXCESSCOL | RL_TDESC_STAT_COLCNT))
			ifp->if_collisions++;
		if (txstat & RL_TDESC_STAT_TXERRSUM)
			ifp->if_oerrors++;

		cons = RL_NEXT_TX_DESC(sc, idx);
		free = 1;
	}

	if (free == 0)
		return (0);

	sc->rl_ldata.rl_txq_considx = cons;

	/*
	 * Some chips will ignore a second TX request issued while an
	 * existing transmission is in progress. If the transmitter goes
	 * idle but there are still packets waiting to be sent, we need
	 * to restart the channel here to flush them out. This only
	 * seems to be required with the PCIe devices.
	 */
	if (ifq_is_oactive(&ifp->if_snd))
		ifq_restart(&ifp->if_snd);
	else if (free == 2)
		ifq_serialize(&ifp->if_snd, &sc->rl_start);
	else
		ifp->if_timer = 0;

	return (1);
}

void
re_tick(void *xsc)
{
	struct rl_softc	*sc = xsc;
	struct mii_data	*mii;
	int s;

	mii = &sc->sc_mii;

	s = splnet();

	mii_tick(mii);

	if ((sc->rl_flags & RL_FLAG_LINK) == 0)
		re_miibus_statchg(&sc->sc_dev);

	splx(s);

	timeout_add_sec(&sc->timer_handle, 1);
}

int
re_intr(void *arg)
{
	struct rl_softc	*sc = arg;
	struct ifnet	*ifp;
	u_int16_t	status;
	int		claimed = 0, rx, tx;

	ifp = &sc->sc_arpcom.ac_if;

	if (!(ifp->if_flags & IFF_RUNNING))
		return (0);

	/* Disable interrupts. */
	CSR_WRITE_2(sc, RL_IMR, 0);

	rx = tx = 0;
	status = CSR_READ_2(sc, RL_ISR);
	/* If the card has gone away the read returns 0xffff. */
	if (status == 0xffff)
		return (0);
	if (status)
		CSR_WRITE_2(sc, RL_ISR, status);

	if (status & RL_ISR_TIMEOUT_EXPIRED)
		claimed = 1;

	if (status & RL_INTRS_CPLUS) {
		if (status &
		    (sc->rl_rx_ack | RL_ISR_RX_ERR | RL_ISR_FIFO_OFLOW)) {
			rx |= re_rxeof(sc);
			claimed = 1;
		}

		if (status & (sc->rl_tx_ack | RL_ISR_TX_ERR)) {
			tx |= re_txeof(sc);
			claimed = 1;
		}

		if (status & RL_ISR_SYSTEM_ERR) {
			KERNEL_LOCK();
			re_init(ifp);
			KERNEL_UNLOCK();
			claimed = 1;
		}
	}

	if (sc->rl_imtype == RL_IMTYPE_SIM) {
		if (sc->rl_timerintr) {
			if ((tx | rx) == 0) {
				/*
				 * Nothing needs to be processed, fallback
				 * to use TX/RX interrupts.
				 */
				re_setup_intr(sc, 1, RL_IMTYPE_NONE);

				/*
				 * Recollect, mainly to avoid the possible
				 * race introduced by changing interrupt
				 * masks.
				 */
				re_rxeof(sc);
				re_txeof(sc);
			} else
				CSR_WRITE_4(sc, RL_TIMERCNT, 1); /* reload */
		} else if (tx | rx) {
			/*
			 * Assume that using simulated interrupt moderation
			 * (hardware timer based) could reduce the interrupt
			 * rate.
			 */
			re_setup_intr(sc, 1, RL_IMTYPE_SIM);
		}
	}

	CSR_WRITE_2(sc, RL_IMR, sc->rl_intrs);

	return (claimed);
}

int
re_encap(struct rl_softc *sc, unsigned int idx, struct mbuf *m)
{
	struct rl_txq	*txq;
	bus_dmamap_t	map;
	int		error, seg, nsegs, curidx, lastidx, pad;
	int		off;
	struct ip	*ip;
	struct rl_desc	*d;
	u_int32_t	cmdstat, vlanctl = 0, csum_flags = 0;

	/*
	 * Set up checksum offload. Note: checksum offload bits must
	 * appear in all descriptors of a multi-descriptor transmit
	 * attempt. This is according to testing done with an 8169
	 * chip. This is a requirement.
	 */

	/*
	 * Set RL_TDESC_CMD_IPCSUM if any checksum offloading
	 * is requested.  Otherwise, RL_TDESC_CMD_TCPCSUM/
	 * RL_TDESC_CMD_UDPCSUM does not take affect.
	 */

	if ((sc->rl_flags & RL_FLAG_JUMBOV2) &&
	    m->m_pkthdr.len > RL_MTU &&
	    (m->m_pkthdr.csum_flags &
	    (M_IPV4_CSUM_OUT|M_TCP_CSUM_OUT|M_UDP_CSUM_OUT)) != 0) {
		struct mbuf mh, *mp;

		mp = m_getptr(m, ETHER_HDR_LEN, &off);
		mh.m_flags = 0;
		mh.m_data = mtod(mp, caddr_t) + off;
		mh.m_next = mp->m_next;
		mh.m_pkthdr.len = mp->m_pkthdr.len - ETHER_HDR_LEN;
		mh.m_len = mp->m_len - off;
		ip = (struct ip *)mh.m_data;

		if (m->m_pkthdr.csum_flags & M_IPV4_CSUM_OUT)
			ip->ip_sum = in_cksum(&mh, sizeof(struct ip));
		if (m->m_pkthdr.csum_flags & (M_TCP_CSUM_OUT|M_UDP_CSUM_OUT))
			in_delayed_cksum(&mh);

		m->m_pkthdr.csum_flags &=
		    ~(M_IPV4_CSUM_OUT|M_TCP_CSUM_OUT|M_UDP_CSUM_OUT);
	}

	if ((m->m_pkthdr.csum_flags &
	    (M_IPV4_CSUM_OUT|M_TCP_CSUM_OUT|M_UDP_CSUM_OUT)) != 0) {
		if (sc->rl_flags & RL_FLAG_DESCV2) {
			vlanctl |= RL_TDESC_CMD_IPCSUMV2;
			if (m->m_pkthdr.csum_flags & M_TCP_CSUM_OUT)
				vlanctl |= RL_TDESC_CMD_TCPCSUMV2;
			if (m->m_pkthdr.csum_flags & M_UDP_CSUM_OUT)
				vlanctl |= RL_TDESC_CMD_UDPCSUMV2;
		} else {
			csum_flags |= RL_TDESC_CMD_IPCSUM;
			if (m->m_pkthdr.csum_flags & M_TCP_CSUM_OUT)
				csum_flags |= RL_TDESC_CMD_TCPCSUM;
			if (m->m_pkthdr.csum_flags & M_UDP_CSUM_OUT)
				csum_flags |= RL_TDESC_CMD_UDPCSUM;
		}
	}

	txq = &sc->rl_ldata.rl_txq[idx];
	map = txq->txq_dmamap;

	error = bus_dmamap_load_mbuf(sc->sc_dmat, map, m,
	    BUS_DMA_WRITE|BUS_DMA_NOWAIT);
	switch (error) {
	case 0:
		break;

	case EFBIG:
		if (m_defrag(m, M_DONTWAIT) == 0 &&
		    bus_dmamap_load_mbuf(sc->sc_dmat, map, m,
		    BUS_DMA_WRITE|BUS_DMA_NOWAIT) == 0)
			break;

		/* FALLTHROUGH */
	default:
		return (0);
	}

	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	nsegs = map->dm_nsegs;
	pad = 0;

	/*
	 * With some of the Realtek chips, using the checksum offload
	 * support in conjunction with the autopadding feature results
	 * in the transmission of corrupt frames. For example, if we
	 * need to send a really small IP fragment that's less than 60
	 * bytes in size, and IP header checksumming is enabled, the
	 * resulting ethernet frame that appears on the wire will
	 * have garbled payload. To work around this, if TX IP checksum
	 * offload is enabled, we always manually pad short frames out
	 * to the minimum ethernet frame size.
	 */
	if ((sc->rl_flags & RL_FLAG_AUTOPAD) == 0 &&
	    m->m_pkthdr.len < RL_IP4CSUMTX_PADLEN &&
	    (m->m_pkthdr.csum_flags & M_IPV4_CSUM_OUT) != 0) {
		pad = 1;
		nsegs++;
	}

	/*
	 * Set up hardware VLAN tagging. Note: vlan tag info must
	 * appear in all descriptors of a multi-descriptor
	 * transmission attempt.
	 */
#if NVLAN > 0
	if (m->m_flags & M_VLANTAG)
		vlanctl |= swap16(m->m_pkthdr.ether_vtag) |
		    RL_TDESC_VLANCTL_TAG;
#endif

	/*
	 * Map the segment array into descriptors. Note that we set the
	 * start-of-frame and end-of-frame markers for either TX or RX, but
	 * they really only have meaning in the TX case. (In the RX case,
	 * it's the chip that tells us where packets begin and end.)
	 * We also keep track of the end of the ring and set the
	 * end-of-ring bits as needed, and we set the ownership bits
	 * in all except the very first descriptor. (The caller will
	 * set this descriptor later when it start transmission or
	 * reception.)
	 */
	curidx = idx;
	cmdstat = RL_TDESC_CMD_SOF;

	for (seg = 0; seg < map->dm_nsegs; seg++) {
		d = &sc->rl_ldata.rl_tx_list[curidx];

		RL_TXDESCSYNC(sc, curidx, BUS_DMASYNC_POSTWRITE);

		d->rl_vlanctl = htole32(vlanctl);
		re_set_bufaddr(d, map->dm_segs[seg].ds_addr);
		cmdstat |= csum_flags | map->dm_segs[seg].ds_len;

		if (curidx == sc->rl_ldata.rl_tx_desc_cnt - 1)
			cmdstat |= RL_TDESC_CMD_EOR;

		d->rl_cmdstat = htole32(cmdstat);

		RL_TXDESCSYNC(sc, curidx, BUS_DMASYNC_PREWRITE);

		lastidx = curidx;
		cmdstat = RL_TDESC_CMD_OWN;
		curidx = RL_NEXT_TX_DESC(sc, curidx);
	}

	if (pad) {
		d = &sc->rl_ldata.rl_tx_list[curidx];

		RL_TXDESCSYNC(sc, curidx, BUS_DMASYNC_POSTWRITE);

		d->rl_vlanctl = htole32(vlanctl);
		re_set_bufaddr(d, RL_TXPADDADDR(sc));
		cmdstat = csum_flags |
		    RL_TDESC_CMD_OWN | RL_TDESC_CMD_EOF |
		    (RL_IP4CSUMTX_PADLEN + 1 - m->m_pkthdr.len);

		if (curidx == sc->rl_ldata.rl_tx_desc_cnt - 1)
			cmdstat |= RL_TDESC_CMD_EOR;

		d->rl_cmdstat = htole32(cmdstat);

		RL_TXDESCSYNC(sc, curidx, BUS_DMASYNC_PREWRITE);

		lastidx = curidx;
	}

	/* d is already pointing at the last descriptor */
	d->rl_cmdstat |= htole32(RL_TDESC_CMD_EOF);

	/* Transfer ownership of packet to the chip. */
	d = &sc->rl_ldata.rl_tx_list[idx];

	RL_TXDESCSYNC(sc, curidx, BUS_DMASYNC_POSTWRITE);
	d->rl_cmdstat |= htole32(RL_TDESC_CMD_OWN);
	RL_TXDESCSYNC(sc, curidx, BUS_DMASYNC_PREWRITE);

	/* update info of TX queue and descriptors */
	txq->txq_mbuf = m;
	txq->txq_descidx = lastidx;

	return (nsegs);
}

void
re_txstart(void *xsc)
{
	struct rl_softc *sc = xsc;

	CSR_WRITE_1(sc, sc->rl_txstart, RL_TXSTART_START);
}

/*
 * Main transmit routine for C+ and gigE NICs.
 */

void
re_start(struct ifqueue *ifq)
{
	struct ifnet	*ifp = ifq->ifq_if;
	struct rl_softc	*sc = ifp->if_softc;
	struct mbuf	*m;
	unsigned int	idx;
	unsigned int	free, used;
	int		post = 0;

	if (!ISSET(sc->rl_flags, RL_FLAG_LINK)) {
		ifq_purge(ifq);
		return;
	}

	free = sc->rl_ldata.rl_txq_considx;
	idx = sc->rl_ldata.rl_txq_prodidx;
	if (free <= idx)
		free += sc->rl_ldata.rl_tx_desc_cnt;
	free -= idx;

	for (;;) {
		if (free < sc->rl_ldata.rl_tx_ndescs + 2) {
			ifq_set_oactive(ifq);
			break;
		}

		m = ifq_dequeue(ifq);
		if (m == NULL)
			break;

		used = re_encap(sc, idx, m);
		if (used == 0) {
			m_freem(m);
			continue;
		}

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap_ether(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		KASSERT(used <= free);
		free -= used;

		idx += used;
		if (idx >= sc->rl_ldata.rl_tx_desc_cnt)
			idx -= sc->rl_ldata.rl_tx_desc_cnt;

		post = 1;
	}

	if (post == 0)
		return;

	ifp->if_timer = 5;
	sc->rl_ldata.rl_txq_prodidx = idx;
	ifq_serialize(ifq, &sc->rl_start);
}

int
re_init(struct ifnet *ifp)
{
	struct rl_softc *sc = ifp->if_softc;
	u_int16_t	cfg;
	uint32_t	rxcfg;
	int		s;
	union {
		u_int32_t align_dummy;
		u_char eaddr[ETHER_ADDR_LEN];
	} eaddr;

	s = splnet();

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	re_stop(ifp);

	/* Put controller into known state. */
	re_reset(sc);

	/*
	 * Enable C+ RX and TX mode, as well as VLAN stripping and
	 * RX checksum offload. We must configure the C+ register
	 * before all others.
	 */
	cfg = RL_CPLUSCMD_TXENB | RL_CPLUSCMD_PCI_MRW |
	    RL_CPLUSCMD_RXCSUM_ENB;

	if (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING)
		cfg |= RL_CPLUSCMD_VLANSTRIP;

	if (sc->rl_flags & RL_FLAG_MACSTAT)
		cfg |= RL_CPLUSCMD_MACSTAT_DIS;
	else
		cfg |= RL_CPLUSCMD_RXENB;

	CSR_WRITE_2(sc, RL_CPLUS_CMD, cfg);

	/*
	 * Init our MAC address.  Even though the chipset
	 * documentation doesn't mention it, we need to enter "Config
	 * register write enable" mode to modify the ID registers.
	 */
	bcopy(sc->sc_arpcom.ac_enaddr, eaddr.eaddr, ETHER_ADDR_LEN);
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_WRITECFG);
	CSR_WRITE_4(sc, RL_IDR4,
	    htole32(*(u_int32_t *)(&eaddr.eaddr[4])));
	CSR_WRITE_4(sc, RL_IDR0,
	    htole32(*(u_int32_t *)(&eaddr.eaddr[0])));
	/*
	 * Default on PC Engines APU1 is to have all LEDs off unless
	 * there is network activity. Override to provide a link status
	 * LED.
	 */
	if (sc->sc_hwrev == RL_HWREV_8168E &&
	    hw_vendor != NULL && hw_prod != NULL &&
	    strcmp(hw_vendor, "PC Engines") == 0 &&
	    strcmp(hw_prod, "APU") == 0) {
		CSR_SETBIT_1(sc, RL_CFG4, RL_CFG4_CUSTOM_LED);
		CSR_WRITE_1(sc, RL_LEDSEL, RL_LED_LINK | RL_LED_ACT << 4);
	}
	/*
	 * Protect config register again
	 */
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_OFF);

	if ((sc->rl_flags & RL_FLAG_JUMBOV2) != 0)
		re_set_jumbo(sc);

	/*
	 * For C+ mode, initialize the RX descriptors and mbufs.
	 */
	re_rx_list_init(sc);
	re_tx_list_init(sc);

	/*
	 * Load the addresses of the RX and TX lists into the chip.
	 */
	CSR_WRITE_4(sc, RL_RXLIST_ADDR_HI,
	    RL_ADDR_HI(sc->rl_ldata.rl_rx_list_map->dm_segs[0].ds_addr));
	CSR_WRITE_4(sc, RL_RXLIST_ADDR_LO,
	    RL_ADDR_LO(sc->rl_ldata.rl_rx_list_map->dm_segs[0].ds_addr));

	CSR_WRITE_4(sc, RL_TXLIST_ADDR_HI,
	    RL_ADDR_HI(sc->rl_ldata.rl_tx_list_map->dm_segs[0].ds_addr));
	CSR_WRITE_4(sc, RL_TXLIST_ADDR_LO,
	    RL_ADDR_LO(sc->rl_ldata.rl_tx_list_map->dm_segs[0].ds_addr));

	if (sc->rl_flags & RL_FLAG_RXDV_GATED)
		CSR_WRITE_4(sc, RL_MISC, CSR_READ_4(sc, RL_MISC) &
		    ~0x00080000);

	/*
	 * Set the initial TX and RX configuration.
	 */
	CSR_WRITE_4(sc, RL_TXCFG, RL_TXCFG_CONFIG);

	CSR_WRITE_1(sc, RL_EARLY_TX_THRESH, 16);

	rxcfg = RL_RXCFG_CONFIG;
	if (sc->rl_flags & RL_FLAG_EARLYOFF)
		rxcfg |= RL_RXCFG_EARLYOFF;
	else if (sc->rl_flags & RL_FLAG_EARLYOFFV2)
		rxcfg |= RL_RXCFG_EARLYOFFV2;
	CSR_WRITE_4(sc, RL_RXCFG, rxcfg);

	/*
	 * Enable transmit and receive.
	 */
	CSR_WRITE_1(sc, RL_COMMAND, RL_CMD_TX_ENB | RL_CMD_RX_ENB);

	/* Program promiscuous mode and multicast filters. */
	re_iff(sc);

	/*
	 * Enable interrupts.
	 */
	re_setup_intr(sc, 1, sc->rl_imtype);
	CSR_WRITE_2(sc, RL_ISR, sc->rl_intrs);

	/* Start RX/TX process. */
	CSR_WRITE_4(sc, RL_MISSEDPKT, 0);

	/*
	 * For 8169 gigE NICs, set the max allowed RX packet
	 * size so we can receive jumbo frames.
	 */
	if (sc->sc_hwrev != RL_HWREV_8139CPLUS) {
		if (sc->rl_flags & RL_FLAG_PCIE &&
		    (sc->rl_flags & RL_FLAG_JUMBOV2) == 0)
			CSR_WRITE_2(sc, RL_MAXRXPKTLEN, RE_RX_DESC_BUFLEN);
		else
			CSR_WRITE_2(sc, RL_MAXRXPKTLEN, 16383);
	}

	CSR_WRITE_1(sc, sc->rl_cfg1, CSR_READ_1(sc, sc->rl_cfg1) |
	    RL_CFG1_DRVLOAD);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);

	sc->rl_flags &= ~RL_FLAG_LINK;
	mii_mediachg(&sc->sc_mii);

	timeout_add_sec(&sc->timer_handle, 1);

	return (0);
}

/*
 * Set media options.
 */
int
re_ifmedia_upd(struct ifnet *ifp)
{
	struct rl_softc	*sc;

	sc = ifp->if_softc;

	return (mii_mediachg(&sc->sc_mii));
}

/*
 * Report current media status.
 */
void
re_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct rl_softc	*sc;

	sc = ifp->if_softc;

	mii_pollstat(&sc->sc_mii);
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
	ifmr->ifm_status = sc->sc_mii.mii_media_status;
}

int
re_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct rl_softc	*sc = ifp->if_softc;
	struct ifreq	*ifr = (struct ifreq *) data;
	int		s, error = 0;

	s = splnet();

	switch(command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			re_init(ifp);
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				re_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				re_stop(ifp);
		}
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, command);
		break;
	case SIOCGIFRXR:
		error = if_rxr_ioctl((struct if_rxrinfo *)ifr->ifr_data,
		    NULL, RL_FRAMELEN(sc->rl_max_mtu), &sc->rl_ldata.rl_rx_ring);
		break;
	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, command, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			re_iff(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
re_watchdog(struct ifnet *ifp)
{
	struct rl_softc	*sc;
	int	s;

	sc = ifp->if_softc;
	s = splnet();
	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);

	re_init(ifp);

	splx(s);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void
re_stop(struct ifnet *ifp)
{
	struct rl_softc *sc;
	int	i;

	sc = ifp->if_softc;

	ifp->if_timer = 0;
	sc->rl_flags &= ~RL_FLAG_LINK;
	sc->rl_timerintr = 0;

	timeout_del(&sc->timer_handle);
	ifp->if_flags &= ~IFF_RUNNING;

	/*
	 * Disable accepting frames to put RX MAC into idle state.
	 * Otherwise it's possible to get frames while stop command
	 * execution is in progress and controller can DMA the frame
	 * to already freed RX buffer during that period.
	 */
	CSR_WRITE_4(sc, RL_RXCFG, CSR_READ_4(sc, RL_RXCFG) &
	    ~(RL_RXCFG_RX_ALLPHYS | RL_RXCFG_RX_BROAD | RL_RXCFG_RX_INDIV |
	    RL_RXCFG_RX_MULTI));

	if (sc->rl_flags & RL_FLAG_WAIT_TXPOLL) {
		for (i = RL_TIMEOUT; i > 0; i--) {
			if ((CSR_READ_1(sc, sc->rl_txstart) &
			    RL_TXSTART_START) == 0)
				break;
			DELAY(20);
		}
		if (i == 0)
			printf("%s: stopping TX poll timed out!\n",
			    sc->sc_dev.dv_xname);
		CSR_WRITE_1(sc, RL_COMMAND, 0x00);
	} else if (sc->rl_flags & RL_FLAG_CMDSTOP) {
		CSR_WRITE_1(sc, RL_COMMAND, RL_CMD_STOPREQ | RL_CMD_TX_ENB |
		    RL_CMD_RX_ENB);
		if (sc->rl_flags & RL_FLAG_CMDSTOP_WAIT_TXQ) {
			for (i = RL_TIMEOUT; i > 0; i--) {
				if ((CSR_READ_4(sc, RL_TXCFG) &
				    RL_TXCFG_QUEUE_EMPTY) != 0)
					break;
				DELAY(100);
			}
			if (i == 0)
				printf("%s: stopping TXQ timed out!\n",
				    sc->sc_dev.dv_xname);
		}
	} else
		CSR_WRITE_1(sc, RL_COMMAND, 0x00);
	DELAY(1000);
	CSR_WRITE_2(sc, RL_IMR, 0x0000);
	CSR_WRITE_2(sc, RL_ISR, 0xFFFF);

	intr_barrier(sc->sc_ih);
	ifq_barrier(&ifp->if_snd);

	ifq_clr_oactive(&ifp->if_snd);
	mii_down(&sc->sc_mii);

	if (sc->rl_head != NULL) {
		m_freem(sc->rl_head);
		sc->rl_head = sc->rl_tail = NULL;
	}

	/* Free the TX list buffers. */
	for (i = 0; i < sc->rl_ldata.rl_tx_desc_cnt; i++) {
		if (sc->rl_ldata.rl_txq[i].txq_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat,
			    sc->rl_ldata.rl_txq[i].txq_dmamap);
			m_freem(sc->rl_ldata.rl_txq[i].txq_mbuf);
			sc->rl_ldata.rl_txq[i].txq_mbuf = NULL;
		}
	}

	/* Free the RX list buffers. */
	for (i = 0; i < sc->rl_ldata.rl_rx_desc_cnt; i++) {
		if (sc->rl_ldata.rl_rxsoft[i].rxs_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat,
			    sc->rl_ldata.rl_rxsoft[i].rxs_dmamap);
			m_freem(sc->rl_ldata.rl_rxsoft[i].rxs_mbuf);
			sc->rl_ldata.rl_rxsoft[i].rxs_mbuf = NULL;
		}
	}
}

void
re_setup_hw_im(struct rl_softc *sc)
{
	KASSERT(sc->rl_flags & RL_FLAG_HWIM);

	/*
	 * Interrupt moderation
	 *
	 * 0xABCD
	 * A - unknown (maybe TX related)
	 * B - TX timer (unit: 25us)
	 * C - unknown (maybe RX related)
	 * D - RX timer (unit: 25us)
	 *
	 *
	 * re(4)'s interrupt moderation is actually controlled by
	 * two variables, like most other NICs (bge, bnx etc.)
	 * o  timer
	 * o  number of packets [P]
	 *
	 * The logic relationship between these two variables is
	 * similar to other NICs too:
	 * if (timer expire || packets > [P])
	 *     Interrupt is delivered
	 *
	 * Currently we only know how to set 'timer', but not
	 * 'number of packets', which should be ~30, as far as I
	 * tested (sink ~900Kpps, interrupt rate is 30KHz)
	 */
	CSR_WRITE_2(sc, RL_IM,
		    RL_IM_RXTIME(sc->rl_rx_time) |
		    RL_IM_TXTIME(sc->rl_tx_time) |
		    RL_IM_MAGIC);
}

void
re_disable_hw_im(struct rl_softc *sc)
{
	if (sc->rl_flags & RL_FLAG_HWIM)
		CSR_WRITE_2(sc, RL_IM, 0);
}

void
re_setup_sim_im(struct rl_softc *sc)
{
	if (sc->sc_hwrev == RL_HWREV_8139CPLUS)
		CSR_WRITE_4(sc, RL_TIMERINT, 0x400); /* XXX */
	else {
		u_int32_t nticks;

		/*
		 * Datasheet says tick decreases at bus speed,
		 * but it seems the clock runs a little bit
		 * faster, so we do some compensation here.
		 */
		nticks = (sc->rl_sim_time * sc->rl_bus_speed * 8) / 5;
		CSR_WRITE_4(sc, RL_TIMERINT_8169, nticks);
	}
	CSR_WRITE_4(sc, RL_TIMERCNT, 1); /* reload */
	sc->rl_timerintr = 1;
}

void
re_disable_sim_im(struct rl_softc *sc)
{
	if (sc->sc_hwrev == RL_HWREV_8139CPLUS)
		CSR_WRITE_4(sc, RL_TIMERINT, 0);
	else
		CSR_WRITE_4(sc, RL_TIMERINT_8169, 0);
	sc->rl_timerintr = 0;
}

void
re_config_imtype(struct rl_softc *sc, int imtype)
{
	switch (imtype) {
	case RL_IMTYPE_HW:
		KASSERT(sc->rl_flags & RL_FLAG_HWIM);
		/* FALLTHROUGH */
	case RL_IMTYPE_NONE:
		sc->rl_intrs = RL_INTRS_CPLUS;
		sc->rl_rx_ack = RL_ISR_RX_OK | RL_ISR_FIFO_OFLOW |
				RL_ISR_RX_OVERRUN;
		sc->rl_tx_ack = RL_ISR_TX_OK;
		break;

	case RL_IMTYPE_SIM:
		sc->rl_intrs = RL_INTRS_TIMER;
		sc->rl_rx_ack = RL_ISR_TIMEOUT_EXPIRED;
		sc->rl_tx_ack = RL_ISR_TIMEOUT_EXPIRED;
		break;

	default:
		panic("%s: unknown imtype %d",
		      sc->sc_dev.dv_xname, imtype);
	}
}

void
re_set_jumbo(struct rl_softc *sc)
{
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_WRITECFG);
	CSR_WRITE_1(sc, RL_CFG3, CSR_READ_1(sc, RL_CFG3) |
	    RL_CFG3_JUMBO_EN0);

	switch (sc->sc_hwrev) {
	case RL_HWREV_8168DP:
		break;
	case RL_HWREV_8168E:
		CSR_WRITE_1(sc, RL_CFG4, CSR_READ_1(sc, RL_CFG4) |
		    RL_CFG4_8168E_JUMBO_EN1);
		break;
	default:
		CSR_WRITE_1(sc, RL_CFG4, CSR_READ_1(sc, RL_CFG4) |
		    RL_CFG4_JUMBO_EN1);
		break;
	}

	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_OFF);
}

void
re_setup_intr(struct rl_softc *sc, int enable_intrs, int imtype)
{
	re_config_imtype(sc, imtype);

	if (enable_intrs)
		CSR_WRITE_2(sc, RL_IMR, sc->rl_intrs);
	else
		CSR_WRITE_2(sc, RL_IMR, 0);

	switch (imtype) {
	case RL_IMTYPE_NONE:
		re_disable_sim_im(sc);
		re_disable_hw_im(sc);
		break;

	case RL_IMTYPE_HW:
		KASSERT(sc->rl_flags & RL_FLAG_HWIM);
		re_disable_sim_im(sc);
		re_setup_hw_im(sc);
		break;

	case RL_IMTYPE_SIM:
		re_disable_hw_im(sc);
		re_setup_sim_im(sc);
		break;

	default:
		panic("%s: unknown imtype %d",
		      sc->sc_dev.dv_xname, imtype);
	}
}

#ifndef SMALL_KERNEL
int
re_wol(struct ifnet *ifp, int enable)
{
	struct rl_softc *sc = ifp->if_softc;
	u_int8_t val;

	if (enable) {
		if ((CSR_READ_1(sc, sc->rl_cfg1) & RL_CFG1_PME) == 0) {
			printf("%s: power management is disabled, "
			    "cannot do WOL\n", sc->sc_dev.dv_xname);
			return (ENOTSUP);
		}
		if ((CSR_READ_1(sc, sc->rl_cfg2) & RL_CFG2_AUXPWR) == 0)
			printf("%s: no auxiliary power, cannot do WOL from D3 "
			    "(power-off) state\n", sc->sc_dev.dv_xname);
	}

	re_iff(sc);

	/* Temporarily enable write to configuration registers. */
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_WRITECFG);

	/* Always disable all wake events except magic packet. */
	if (enable) {
		val = CSR_READ_1(sc, sc->rl_cfg5);
		val &= ~(RL_CFG5_WOL_UCAST | RL_CFG5_WOL_MCAST |
		    RL_CFG5_WOL_BCAST);
		CSR_WRITE_1(sc, sc->rl_cfg5, val);

		val = CSR_READ_1(sc, sc->rl_cfg3);
		val |= RL_CFG3_WOL_MAGIC;
		val &= ~RL_CFG3_WOL_LINK;
		CSR_WRITE_1(sc, sc->rl_cfg3, val);
	} else {
		val = CSR_READ_1(sc, sc->rl_cfg5);
		val &= ~(RL_CFG5_WOL_UCAST | RL_CFG5_WOL_MCAST |
		    RL_CFG5_WOL_BCAST);
		CSR_WRITE_1(sc, sc->rl_cfg5, val);

		val = CSR_READ_1(sc, sc->rl_cfg3);
		val &= ~(RL_CFG3_WOL_MAGIC | RL_CFG3_WOL_LINK);
		CSR_WRITE_1(sc, sc->rl_cfg3, val);
	}

	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_OFF);

	return (0);
}
#endif

#if NKSTAT > 0

#define RE_DTCCR_CMD		(1U << 3)
#define RE_DTCCR_LO		0x10
#define RE_DTCCR_HI		0x14

struct re_kstats {
	struct kstat_kv		tx_ok;
	struct kstat_kv		rx_ok;
	struct kstat_kv		tx_er;
	struct kstat_kv		rx_er;
	struct kstat_kv		miss_pkt;
	struct kstat_kv		fae;
	struct kstat_kv		tx_1col;
	struct kstat_kv		tx_mcol;
	struct kstat_kv		rx_ok_phy;
	struct kstat_kv		rx_ok_brd;
	struct kstat_kv		rx_ok_mul;
	struct kstat_kv		tx_abt;
	struct kstat_kv		tx_undrn;
};

static const struct re_kstats re_kstats_tpl = {
	.tx_ok =	KSTAT_KV_UNIT_INITIALIZER("TxOk",
			    KSTAT_KV_T_COUNTER64, KSTAT_KV_U_PACKETS),
	.rx_ok =	KSTAT_KV_UNIT_INITIALIZER("RxOk",
			    KSTAT_KV_T_COUNTER64, KSTAT_KV_U_PACKETS),
	.tx_er =	KSTAT_KV_UNIT_INITIALIZER("TxEr",
			    KSTAT_KV_T_COUNTER64, KSTAT_KV_U_PACKETS),
	.rx_er =	KSTAT_KV_UNIT_INITIALIZER("RxEr",
			    KSTAT_KV_T_COUNTER32, KSTAT_KV_U_PACKETS),
	.miss_pkt =	KSTAT_KV_UNIT_INITIALIZER("MissPkt",
			    KSTAT_KV_T_COUNTER16, KSTAT_KV_U_PACKETS),
	.fae =		KSTAT_KV_UNIT_INITIALIZER("FAE",
			    KSTAT_KV_T_COUNTER16, KSTAT_KV_U_PACKETS),
	.tx_1col =	KSTAT_KV_UNIT_INITIALIZER("Tx1Col",
			    KSTAT_KV_T_COUNTER32, KSTAT_KV_U_PACKETS),
	.tx_mcol =	KSTAT_KV_UNIT_INITIALIZER("TxMCol",
			    KSTAT_KV_T_COUNTER32, KSTAT_KV_U_PACKETS),
	.rx_ok_phy =	KSTAT_KV_UNIT_INITIALIZER("RxOkPhy",
			    KSTAT_KV_T_COUNTER64, KSTAT_KV_U_PACKETS),
	.rx_ok_brd =	KSTAT_KV_UNIT_INITIALIZER("RxOkBrd",
			    KSTAT_KV_T_COUNTER64, KSTAT_KV_U_PACKETS),
	.rx_ok_mul =	KSTAT_KV_UNIT_INITIALIZER("RxOkMul",
			    KSTAT_KV_T_COUNTER32, KSTAT_KV_U_PACKETS),
	.tx_abt =	KSTAT_KV_UNIT_INITIALIZER("TxAbt",
			    KSTAT_KV_T_COUNTER16, KSTAT_KV_U_PACKETS),
	.tx_undrn =	KSTAT_KV_UNIT_INITIALIZER("TxUndrn",
			    KSTAT_KV_T_COUNTER16, KSTAT_KV_U_PACKETS),
};

struct re_kstat_softc {
	struct re_stats		*re_ks_sc_stats;

	bus_dmamap_t		 re_ks_sc_map;
	bus_dma_segment_t	 re_ks_sc_seg;
	int			 re_ks_sc_nsegs;

	struct rwlock		 re_ks_sc_rwl;
};

static int
re_kstat_read(struct kstat *ks)
{
	struct rl_softc *sc = ks->ks_softc;
	struct re_kstat_softc *re_ks_sc = ks->ks_ptr;
	bus_dmamap_t map;
	bus_addr_t addr;
	uint32_t reg;
	uint8_t command;
	int tmo;

	command = CSR_READ_1(sc, RL_COMMAND);
	if (!ISSET(command, RL_CMD_RX_ENB) || command == 0xff)
		return (ENETDOWN);

	map = re_ks_sc->re_ks_sc_map;
	addr = map->dm_segs[0].ds_addr;

	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREREAD);

	CSR_WRITE_4(sc, RE_DTCCR_HI, RL_ADDR_HI(addr));
	bus_space_barrier(sc->rl_btag, sc->rl_bhandle, RE_DTCCR_HI, 4,
	    BUS_SPACE_BARRIER_WRITE);
	CSR_READ_1(sc, RL_COMMAND);
	CSR_WRITE_4(sc, RE_DTCCR_LO, RL_ADDR_LO(addr));
	bus_space_barrier(sc->rl_btag, sc->rl_bhandle, RE_DTCCR_LO, 4,
	    BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_4(sc, RE_DTCCR_LO, RL_ADDR_LO(addr) | RE_DTCCR_CMD);
	bus_space_barrier(sc->rl_btag, sc->rl_bhandle, RE_DTCCR_LO, 4,
	    BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE);

	tmo = 1000;
	do {
		reg = CSR_READ_4(sc, RE_DTCCR_LO);
		if (!ISSET(reg, RE_DTCCR_CMD))
			break;

		delay(10);
		bus_space_barrier(sc->rl_btag, sc->rl_bhandle, RE_DTCCR_LO, 4,
		    BUS_SPACE_BARRIER_READ);
	} while (--tmo);

	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD);

	if (ISSET(reg, RE_DTCCR_CMD))
		return (EIO);

	nanouptime(&ks->ks_updated);

	return (0);
}

static int
re_kstat_copy(struct kstat *ks, void *dst)
{
	struct re_kstat_softc *re_ks_sc = ks->ks_ptr;
	struct re_stats *rs = re_ks_sc->re_ks_sc_stats;
	struct re_kstats *kvs = dst;

	*kvs = re_kstats_tpl;
	kstat_kv_u64(&kvs->tx_ok) = lemtoh64(&rs->re_tx_ok);
	kstat_kv_u64(&kvs->rx_ok) = lemtoh64(&rs->re_rx_ok);
	kstat_kv_u64(&kvs->tx_er) = lemtoh64(&rs->re_tx_er);
	kstat_kv_u32(&kvs->rx_er) = lemtoh32(&rs->re_rx_er);
	kstat_kv_u16(&kvs->miss_pkt) = lemtoh16(&rs->re_miss_pkt);
	kstat_kv_u16(&kvs->fae) = lemtoh16(&rs->re_fae);
	kstat_kv_u32(&kvs->tx_1col) = lemtoh32(&rs->re_tx_1col);
	kstat_kv_u32(&kvs->tx_mcol) = lemtoh32(&rs->re_tx_mcol);
	kstat_kv_u64(&kvs->rx_ok_phy) = lemtoh64(&rs->re_rx_ok_phy);
	kstat_kv_u64(&kvs->rx_ok_brd) = lemtoh64(&rs->re_rx_ok_brd);
	kstat_kv_u32(&kvs->rx_ok_mul) = lemtoh32(&rs->re_rx_ok_mul);
	kstat_kv_u16(&kvs->tx_abt) = lemtoh16(&rs->re_tx_abt);
	kstat_kv_u16(&kvs->tx_undrn) = lemtoh16(&rs->re_tx_undrn);

	return (0);
}

void
re_kstat_attach(struct rl_softc *sc)
{
	struct re_kstat_softc *re_ks_sc;
	struct kstat *ks;

	re_ks_sc = malloc(sizeof(*re_ks_sc), M_DEVBUF, M_NOWAIT);
	if (re_ks_sc == NULL) {
		printf("%s: cannot allocate kstat softc\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	if (bus_dmamap_create(sc->sc_dmat,
	    sizeof(struct re_stats), 1, sizeof(struct re_stats), 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
	    &re_ks_sc->re_ks_sc_map) != 0) {
		printf("%s: cannot create counter dma memory map\n",
		    sc->sc_dev.dv_xname);
		goto free;
	}

	if (bus_dmamem_alloc(sc->sc_dmat,
	    sizeof(struct re_stats), RE_STATS_ALIGNMENT, 0,
	    &re_ks_sc->re_ks_sc_seg, 1, &re_ks_sc->re_ks_sc_nsegs,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO) != 0) {
		printf("%s: cannot allocate counter dma memory\n",
		    sc->sc_dev.dv_xname);
		goto destroy;
	}

	if (bus_dmamem_map(sc->sc_dmat,
	    &re_ks_sc->re_ks_sc_seg, re_ks_sc->re_ks_sc_nsegs,
	    sizeof(struct re_stats), (caddr_t *)&re_ks_sc->re_ks_sc_stats,
	    BUS_DMA_NOWAIT) != 0) {
		printf("%s: cannot map counter dma memory\n",
		    sc->sc_dev.dv_xname);
		goto freedma;
	}

	if (bus_dmamap_load(sc->sc_dmat, re_ks_sc->re_ks_sc_map,
	    (caddr_t)re_ks_sc->re_ks_sc_stats, sizeof(struct re_stats),
	    NULL, BUS_DMA_NOWAIT) != 0) {
		printf("%s: cannot load counter dma memory\n",
		    sc->sc_dev.dv_xname);
		goto unmap;
	}

	ks = kstat_create(sc->sc_dev.dv_xname, 0, "re-stats", 0,
	    KSTAT_T_KV, 0);
	if (ks == NULL) {
		printf("%s: cannot create re-stats kstat\n",
		    sc->sc_dev.dv_xname);
		goto unload;
	}

	ks->ks_datalen = sizeof(re_kstats_tpl);

	rw_init(&re_ks_sc->re_ks_sc_rwl, "restats");
	kstat_set_wlock(ks, &re_ks_sc->re_ks_sc_rwl);
	ks->ks_softc = sc;
	ks->ks_ptr = re_ks_sc;
	ks->ks_read = re_kstat_read;
	ks->ks_copy = re_kstat_copy;

	kstat_install(ks);

	sc->rl_kstat = ks;

	return;

unload:
	bus_dmamap_unload(sc->sc_dmat, re_ks_sc->re_ks_sc_map);
unmap:
	bus_dmamem_unmap(sc->sc_dmat,
	    (caddr_t)re_ks_sc->re_ks_sc_stats, sizeof(struct re_stats));
freedma:
	bus_dmamem_free(sc->sc_dmat, &re_ks_sc->re_ks_sc_seg,
	    re_ks_sc->re_ks_sc_nsegs);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, re_ks_sc->re_ks_sc_map);
free:
	free(re_ks_sc, M_DEVBUF, sizeof(*re_ks_sc));
}

void
re_kstat_detach(struct rl_softc *sc)
{
	struct kstat *ks = sc->rl_kstat;
	struct re_kstat_softc *re_ks_sc;

	if (ks == NULL)
		return;

	kstat_remove(ks);
	re_ks_sc = ks->ks_ptr;
	kstat_destroy(ks);

	bus_dmamap_unload(sc->sc_dmat, re_ks_sc->re_ks_sc_map);
	bus_dmamem_unmap(sc->sc_dmat,
	    (caddr_t)re_ks_sc->re_ks_sc_stats, sizeof(struct re_stats));
	bus_dmamem_free(sc->sc_dmat, &re_ks_sc->re_ks_sc_seg,
	    re_ks_sc->re_ks_sc_nsegs);
	bus_dmamap_destroy(sc->sc_dmat, re_ks_sc->re_ks_sc_map);
	free(re_ks_sc, M_DEVBUF, sizeof(*re_ks_sc));
}
#endif /* NKSTAT > 0 */
