/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
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
 * DEC "tulip" clone ethernet driver. Supports the DEC/Intel 21143
 * series chips and several workalikes including the following:
 *
 * Macronix 98713/98715/98725/98727/98732 PMAC (www.macronix.com)
 * Macronix/Lite-On 82c115 PNIC II (www.macronix.com)
 * Lite-On 82c168/82c169 PNIC (www.litecom.com)
 * ASIX Electronics AX88140A (www.asix.com.tw)
 * ASIX Electronics AX88141 (www.asix.com.tw)
 * ADMtek AL981 (www.admtek.com.tw)
 * ADMtek AN983 (www.admtek.com.tw)
 * ADMtek CardBus AN985 (www.admtek.com.tw)
 * Netgear FA511 (www.netgear.com) Appears to be rebadged ADMTek CardBus AN985
 * Davicom DM9100, DM9102, DM9102A (www.davicom8.com)
 * Accton EN1217 (www.accton.com)
 * Xircom X3201 (www.xircom.com)
 * Abocom FE2500
 * Conexant LANfinity (www.conexant.com)
 * 3Com OfficeConnect 10/100B 3CSOHO100B (www.3com.com)
 *
 * Datasheets for the 21143 are available at developer.intel.com.
 * Datasheets for the clone parts can be found at their respective sites.
 * (Except for the PNIC; see www.freebsd.org/~wpaul/PNIC/pnic.ps.gz.)
 * The PNIC II is essentially a Macronix 98715A chip; the only difference
 * worth noting is that its multicast hash table is only 128 bits wide
 * instead of 512.
 *
 * Written by Bill Paul <wpaul@ee.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */
/*
 * The Intel 21143 is the successor to the DEC 21140. It is basically
 * the same as the 21140 but with a few new features. The 21143 supports
 * three kinds of media attachments:
 *
 * o MII port, for 10Mbps and 100Mbps support and NWAY
 *   autonegotiation provided by an external PHY.
 * o SYM port, for symbol mode 100Mbps support.
 * o 10baseT port.
 * o AUI/BNC port.
 *
 * The 100Mbps SYM port and 10baseT port can be used together in
 * combination with the internal NWAY support to create a 10/100
 * autosensing configuration.
 *
 * Note that not all tulip workalikes are handled in this driver: we only
 * deal with those which are relatively well behaved. The Winbond is
 * handled separately due to its different register offsets and the
 * special handling needed for its various bugs. The PNIC is handled
 * here, but I'm not thrilled about it.
 *
 * All of the workalike chips use some form of MII transceiver support
 * with the exception of the Macronix chips, which also have a SYM port.
 * The ASIX AX88140A is also documented to have a SYM port, but all
 * the cards I've seen use an MII transceiver, probably because the
 * AX88140A doesn't support internal NWAY.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_device_polling.h"
#endif

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <net/bpf.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/mii/mii.h>
#include <dev/mii/mii_bitbang.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#define	DC_USEIOSPACE

#include <dev/dc/if_dcreg.h>

#ifdef __sparc64__
#include <dev/ofw/openfirm.h>
#include <machine/ofw_machdep.h>
#endif

MODULE_DEPEND(dc, pci, 1, 1, 1);
MODULE_DEPEND(dc, ether, 1, 1, 1);
MODULE_DEPEND(dc, miibus, 1, 1, 1);

/*
 * "device miibus" is required in kernel config.  See GENERIC if you get
 * errors here.
 */
#include "miibus_if.h"

/*
 * Various supported device vendors/types and their names.
 */
static const struct dc_type dc_devs[] = {
	{ DC_DEVID(DC_VENDORID_DEC, DC_DEVICEID_21143), 0,
		"Intel 21143 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_DAVICOM, DC_DEVICEID_DM9009), 0,
		"Davicom DM9009 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_DAVICOM, DC_DEVICEID_DM9100), 0,
		"Davicom DM9100 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_DAVICOM, DC_DEVICEID_DM9102), DC_REVISION_DM9102A,
		"Davicom DM9102A 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_DAVICOM, DC_DEVICEID_DM9102), 0,
		"Davicom DM9102 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_ADMTEK, DC_DEVICEID_AL981), 0,
		"ADMtek AL981 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_ADMTEK, DC_DEVICEID_AN983), 0,
		"ADMtek AN983 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_ADMTEK, DC_DEVICEID_AN985), 0,
		"ADMtek AN985 CardBus 10/100BaseTX or clone" },
	{ DC_DEVID(DC_VENDORID_ADMTEK, DC_DEVICEID_ADM9511), 0,
		"ADMtek ADM9511 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_ADMTEK, DC_DEVICEID_ADM9513), 0,
		"ADMtek ADM9513 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_ASIX, DC_DEVICEID_AX88140A), DC_REVISION_88141,
		"ASIX AX88141 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_ASIX, DC_DEVICEID_AX88140A), 0,
		"ASIX AX88140A 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_MX, DC_DEVICEID_98713), DC_REVISION_98713A,
		"Macronix 98713A 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_MX, DC_DEVICEID_98713), 0,
		"Macronix 98713 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_CP, DC_DEVICEID_98713_CP), DC_REVISION_98713A,
		"Compex RL100-TX 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_CP, DC_DEVICEID_98713_CP), 0,
		"Compex RL100-TX 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_MX, DC_DEVICEID_987x5), DC_REVISION_98725,
		"Macronix 98725 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_MX, DC_DEVICEID_987x5), DC_REVISION_98715AEC_C,
		"Macronix 98715AEC-C 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_MX, DC_DEVICEID_987x5), 0,
		"Macronix 98715/98715A 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_MX, DC_DEVICEID_98727), 0,
		"Macronix 98727/98732 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_LO, DC_DEVICEID_82C115), 0,
		"LC82C115 PNIC II 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_LO, DC_DEVICEID_82C168), DC_REVISION_82C169,
		"82c169 PNIC 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_LO, DC_DEVICEID_82C168), 0,
		"82c168 PNIC 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_ACCTON, DC_DEVICEID_EN1217), 0,
		"Accton EN1217 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_ACCTON, DC_DEVICEID_EN2242), 0,
		"Accton EN2242 MiniPCI 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_XIRCOM, DC_DEVICEID_X3201), 0,
		"Xircom X3201 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_DLINK, DC_DEVICEID_DRP32TXD), 0,
		"Neteasy DRP-32TXD Cardbus 10/100" },
	{ DC_DEVID(DC_VENDORID_ABOCOM, DC_DEVICEID_FE2500), 0,
		"Abocom FE2500 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_ABOCOM, DC_DEVICEID_FE2500MX), 0,
		"Abocom FE2500MX 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_CONEXANT, DC_DEVICEID_RS7112), 0,
		"Conexant LANfinity MiniPCI 10/100BaseTX" },
	{ DC_DEVID(DC_VENDORID_HAWKING, DC_DEVICEID_HAWKING_PN672TX), 0,
		"Hawking CB102 CardBus 10/100" },
	{ DC_DEVID(DC_VENDORID_PLANEX, DC_DEVICEID_FNW3602T), 0,
		"PlaneX FNW-3602-T CardBus 10/100" },
	{ DC_DEVID(DC_VENDORID_3COM, DC_DEVICEID_3CSOHOB), 0,
		"3Com OfficeConnect 10/100B" },
	{ DC_DEVID(DC_VENDORID_MICROSOFT, DC_DEVICEID_MSMN120), 0,
		"Microsoft MN-120 CardBus 10/100" },
	{ DC_DEVID(DC_VENDORID_MICROSOFT, DC_DEVICEID_MSMN130), 0,
		"Microsoft MN-130 10/100" },
	{ DC_DEVID(DC_VENDORID_LINKSYS, DC_DEVICEID_PCMPC200_AB08), 0,
		"Linksys PCMPC200 CardBus 10/100" },
	{ DC_DEVID(DC_VENDORID_LINKSYS, DC_DEVICEID_PCMPC200_AB09), 0,
		"Linksys PCMPC200 CardBus 10/100" },
	{ DC_DEVID(DC_VENDORID_ULI, DC_DEVICEID_M5261), 0,
		"ULi M5261 FastEthernet" },
	{ DC_DEVID(DC_VENDORID_ULI, DC_DEVICEID_M5263), 0,
		"ULi M5263 FastEthernet" },
	{ 0, 0, NULL }
};

static int dc_probe(device_t);
static int dc_attach(device_t);
static int dc_detach(device_t);
static int dc_suspend(device_t);
static int dc_resume(device_t);
static const struct dc_type *dc_devtype(device_t);
static void dc_discard_rxbuf(struct dc_softc *, int);
static int dc_newbuf(struct dc_softc *, int);
static int dc_encap(struct dc_softc *, struct mbuf **);
static void dc_pnic_rx_bug_war(struct dc_softc *, int);
static int dc_rx_resync(struct dc_softc *);
static int dc_rxeof(struct dc_softc *);
static void dc_txeof(struct dc_softc *);
static void dc_tick(void *);
static void dc_tx_underrun(struct dc_softc *);
static void dc_intr(void *);
static void dc_start(struct ifnet *);
static void dc_start_locked(struct ifnet *);
static int dc_ioctl(struct ifnet *, u_long, caddr_t);
static void dc_init(void *);
static void dc_init_locked(struct dc_softc *);
static void dc_stop(struct dc_softc *);
static void dc_watchdog(void *);
static int dc_shutdown(device_t);
static int dc_ifmedia_upd(struct ifnet *);
static int dc_ifmedia_upd_locked(struct dc_softc *);
static void dc_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static int dc_dma_alloc(struct dc_softc *);
static void dc_dma_free(struct dc_softc *);
static void dc_dma_map_addr(void *, bus_dma_segment_t *, int, int);

static void dc_delay(struct dc_softc *);
static void dc_eeprom_idle(struct dc_softc *);
static void dc_eeprom_putbyte(struct dc_softc *, int);
static void dc_eeprom_getword(struct dc_softc *, int, uint16_t *);
static void dc_eeprom_getword_pnic(struct dc_softc *, int, uint16_t *);
static void dc_eeprom_getword_xircom(struct dc_softc *, int, uint16_t *);
static void dc_eeprom_width(struct dc_softc *);
static void dc_read_eeprom(struct dc_softc *, caddr_t, int, int, int);

static int dc_miibus_readreg(device_t, int, int);
static int dc_miibus_writereg(device_t, int, int, int);
static void dc_miibus_statchg(device_t);
static void dc_miibus_mediainit(device_t);

static void dc_setcfg(struct dc_softc *, int);
static void dc_netcfg_wait(struct dc_softc *);
static uint32_t dc_mchash_le(struct dc_softc *, const uint8_t *);
static uint32_t dc_mchash_be(const uint8_t *);
static void dc_setfilt_21143(struct dc_softc *);
static void dc_setfilt_asix(struct dc_softc *);
static void dc_setfilt_admtek(struct dc_softc *);
static void dc_setfilt_uli(struct dc_softc *);
static void dc_setfilt_xircom(struct dc_softc *);

static void dc_setfilt(struct dc_softc *);

static void dc_reset(struct dc_softc *);
static int dc_list_rx_init(struct dc_softc *);
static int dc_list_tx_init(struct dc_softc *);

static int dc_read_srom(struct dc_softc *, int);
static int dc_parse_21143_srom(struct dc_softc *);
static int dc_decode_leaf_sia(struct dc_softc *, struct dc_eblock_sia *);
static int dc_decode_leaf_mii(struct dc_softc *, struct dc_eblock_mii *);
static int dc_decode_leaf_sym(struct dc_softc *, struct dc_eblock_sym *);
static void dc_apply_fixup(struct dc_softc *, int);
static int dc_check_multiport(struct dc_softc *);

/*
 * MII bit-bang glue
 */
static uint32_t dc_mii_bitbang_read(device_t);
static void dc_mii_bitbang_write(device_t, uint32_t);

static const struct mii_bitbang_ops dc_mii_bitbang_ops = {
	dc_mii_bitbang_read,
	dc_mii_bitbang_write,
	{
		DC_SIO_MII_DATAOUT,	/* MII_BIT_MDO */
		DC_SIO_MII_DATAIN,	/* MII_BIT_MDI */
		DC_SIO_MII_CLK,		/* MII_BIT_MDC */
		0,			/* MII_BIT_DIR_HOST_PHY */
		DC_SIO_MII_DIR,		/* MII_BIT_DIR_PHY_HOST */
	}
};

#ifdef DC_USEIOSPACE
#define	DC_RES			SYS_RES_IOPORT
#define	DC_RID			DC_PCI_CFBIO
#else
#define	DC_RES			SYS_RES_MEMORY
#define	DC_RID			DC_PCI_CFBMA
#endif

static device_method_t dc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		dc_probe),
	DEVMETHOD(device_attach,	dc_attach),
	DEVMETHOD(device_detach,	dc_detach),
	DEVMETHOD(device_suspend,	dc_suspend),
	DEVMETHOD(device_resume,	dc_resume),
	DEVMETHOD(device_shutdown,	dc_shutdown),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	dc_miibus_readreg),
	DEVMETHOD(miibus_writereg,	dc_miibus_writereg),
	DEVMETHOD(miibus_statchg,	dc_miibus_statchg),
	DEVMETHOD(miibus_mediainit,	dc_miibus_mediainit),

	DEVMETHOD_END
};

static driver_t dc_driver = {
	"dc",
	dc_methods,
	sizeof(struct dc_softc)
};

static devclass_t dc_devclass;

DRIVER_MODULE_ORDERED(dc, pci, dc_driver, dc_devclass, NULL, NULL,
    SI_ORDER_ANY);
MODULE_PNP_INFO("W32:vendor/device;U8:revision;D:#", pci, dc, dc_devs,
    nitems(dc_devs) - 1);
DRIVER_MODULE(miibus, dc, miibus_driver, miibus_devclass, NULL, NULL);

#define	DC_SETBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) | (x))

#define	DC_CLRBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) & ~(x))

#define	SIO_SET(x)	DC_SETBIT(sc, DC_SIO, (x))
#define	SIO_CLR(x)	DC_CLRBIT(sc, DC_SIO, (x))

static void
dc_delay(struct dc_softc *sc)
{
	int idx;

	for (idx = (300 / 33) + 1; idx > 0; idx--)
		CSR_READ_4(sc, DC_BUSCTL);
}

static void
dc_eeprom_width(struct dc_softc *sc)
{
	int i;

	/* Force EEPROM to idle state. */
	dc_eeprom_idle(sc);

	/* Enter EEPROM access mode. */
	CSR_WRITE_4(sc, DC_SIO, DC_SIO_EESEL);
	dc_delay(sc);
	DC_SETBIT(sc, DC_SIO, DC_SIO_ROMCTL_READ);
	dc_delay(sc);
	DC_CLRBIT(sc, DC_SIO, DC_SIO_EE_CLK);
	dc_delay(sc);
	DC_SETBIT(sc, DC_SIO, DC_SIO_EE_CS);
	dc_delay(sc);

	for (i = 3; i--;) {
		if (6 & (1 << i))
			DC_SETBIT(sc, DC_SIO, DC_SIO_EE_DATAIN);
		else
			DC_CLRBIT(sc, DC_SIO, DC_SIO_EE_DATAIN);
		dc_delay(sc);
		DC_SETBIT(sc, DC_SIO, DC_SIO_EE_CLK);
		dc_delay(sc);
		DC_CLRBIT(sc, DC_SIO, DC_SIO_EE_CLK);
		dc_delay(sc);
	}

	for (i = 1; i <= 12; i++) {
		DC_SETBIT(sc, DC_SIO, DC_SIO_EE_CLK);
		dc_delay(sc);
		if (!(CSR_READ_4(sc, DC_SIO) & DC_SIO_EE_DATAOUT)) {
			DC_CLRBIT(sc, DC_SIO, DC_SIO_EE_CLK);
			dc_delay(sc);
			break;
		}
		DC_CLRBIT(sc, DC_SIO, DC_SIO_EE_CLK);
		dc_delay(sc);
	}

	/* Turn off EEPROM access mode. */
	dc_eeprom_idle(sc);

	if (i < 4 || i > 12)
		sc->dc_romwidth = 6;
	else
		sc->dc_romwidth = i;

	/* Enter EEPROM access mode. */
	CSR_WRITE_4(sc, DC_SIO, DC_SIO_EESEL);
	dc_delay(sc);
	DC_SETBIT(sc, DC_SIO, DC_SIO_ROMCTL_READ);
	dc_delay(sc);
	DC_CLRBIT(sc, DC_SIO, DC_SIO_EE_CLK);
	dc_delay(sc);
	DC_SETBIT(sc, DC_SIO, DC_SIO_EE_CS);
	dc_delay(sc);

	/* Turn off EEPROM access mode. */
	dc_eeprom_idle(sc);
}

static void
dc_eeprom_idle(struct dc_softc *sc)
{
	int i;

	CSR_WRITE_4(sc, DC_SIO, DC_SIO_EESEL);
	dc_delay(sc);
	DC_SETBIT(sc, DC_SIO, DC_SIO_ROMCTL_READ);
	dc_delay(sc);
	DC_CLRBIT(sc, DC_SIO, DC_SIO_EE_CLK);
	dc_delay(sc);
	DC_SETBIT(sc, DC_SIO, DC_SIO_EE_CS);
	dc_delay(sc);

	for (i = 0; i < 25; i++) {
		DC_CLRBIT(sc, DC_SIO, DC_SIO_EE_CLK);
		dc_delay(sc);
		DC_SETBIT(sc, DC_SIO, DC_SIO_EE_CLK);
		dc_delay(sc);
	}

	DC_CLRBIT(sc, DC_SIO, DC_SIO_EE_CLK);
	dc_delay(sc);
	DC_CLRBIT(sc, DC_SIO, DC_SIO_EE_CS);
	dc_delay(sc);
	CSR_WRITE_4(sc, DC_SIO, 0x00000000);
}

/*
 * Send a read command and address to the EEPROM, check for ACK.
 */
static void
dc_eeprom_putbyte(struct dc_softc *sc, int addr)
{
	int d, i;

	d = DC_EECMD_READ >> 6;
	for (i = 3; i--; ) {
		if (d & (1 << i))
			DC_SETBIT(sc, DC_SIO, DC_SIO_EE_DATAIN);
		else
			DC_CLRBIT(sc, DC_SIO, DC_SIO_EE_DATAIN);
		dc_delay(sc);
		DC_SETBIT(sc, DC_SIO, DC_SIO_EE_CLK);
		dc_delay(sc);
		DC_CLRBIT(sc, DC_SIO, DC_SIO_EE_CLK);
		dc_delay(sc);
	}

	/*
	 * Feed in each bit and strobe the clock.
	 */
	for (i = sc->dc_romwidth; i--;) {
		if (addr & (1 << i)) {
			SIO_SET(DC_SIO_EE_DATAIN);
		} else {
			SIO_CLR(DC_SIO_EE_DATAIN);
		}
		dc_delay(sc);
		SIO_SET(DC_SIO_EE_CLK);
		dc_delay(sc);
		SIO_CLR(DC_SIO_EE_CLK);
		dc_delay(sc);
	}
}

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 * The PNIC 82c168/82c169 has its own non-standard way to read
 * the EEPROM.
 */
static void
dc_eeprom_getword_pnic(struct dc_softc *sc, int addr, uint16_t *dest)
{
	int i;
	uint32_t r;

	CSR_WRITE_4(sc, DC_PN_SIOCTL, DC_PN_EEOPCODE_READ | addr);

	for (i = 0; i < DC_TIMEOUT; i++) {
		DELAY(1);
		r = CSR_READ_4(sc, DC_SIO);
		if (!(r & DC_PN_SIOCTL_BUSY)) {
			*dest = (uint16_t)(r & 0xFFFF);
			return;
		}
	}
}

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 * The Xircom X3201 has its own non-standard way to read
 * the EEPROM, too.
 */
static void
dc_eeprom_getword_xircom(struct dc_softc *sc, int addr, uint16_t *dest)
{

	SIO_SET(DC_SIO_ROMSEL | DC_SIO_ROMCTL_READ);

	addr *= 2;
	CSR_WRITE_4(sc, DC_ROM, addr | 0x160);
	*dest = (uint16_t)CSR_READ_4(sc, DC_SIO) & 0xff;
	addr += 1;
	CSR_WRITE_4(sc, DC_ROM, addr | 0x160);
	*dest |= ((uint16_t)CSR_READ_4(sc, DC_SIO) & 0xff) << 8;

	SIO_CLR(DC_SIO_ROMSEL | DC_SIO_ROMCTL_READ);
}

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
static void
dc_eeprom_getword(struct dc_softc *sc, int addr, uint16_t *dest)
{
	int i;
	uint16_t word = 0;

	/* Force EEPROM to idle state. */
	dc_eeprom_idle(sc);

	/* Enter EEPROM access mode. */
	CSR_WRITE_4(sc, DC_SIO, DC_SIO_EESEL);
	dc_delay(sc);
	DC_SETBIT(sc, DC_SIO,  DC_SIO_ROMCTL_READ);
	dc_delay(sc);
	DC_CLRBIT(sc, DC_SIO, DC_SIO_EE_CLK);
	dc_delay(sc);
	DC_SETBIT(sc, DC_SIO, DC_SIO_EE_CS);
	dc_delay(sc);

	/*
	 * Send address of word we want to read.
	 */
	dc_eeprom_putbyte(sc, addr);

	/*
	 * Start reading bits from EEPROM.
	 */
	for (i = 0x8000; i; i >>= 1) {
		SIO_SET(DC_SIO_EE_CLK);
		dc_delay(sc);
		if (CSR_READ_4(sc, DC_SIO) & DC_SIO_EE_DATAOUT)
			word |= i;
		dc_delay(sc);
		SIO_CLR(DC_SIO_EE_CLK);
		dc_delay(sc);
	}

	/* Turn off EEPROM access mode. */
	dc_eeprom_idle(sc);

	*dest = word;
}

/*
 * Read a sequence of words from the EEPROM.
 */
static void
dc_read_eeprom(struct dc_softc *sc, caddr_t dest, int off, int cnt, int be)
{
	int i;
	uint16_t word = 0, *ptr;

	for (i = 0; i < cnt; i++) {
		if (DC_IS_PNIC(sc))
			dc_eeprom_getword_pnic(sc, off + i, &word);
		else if (DC_IS_XIRCOM(sc))
			dc_eeprom_getword_xircom(sc, off + i, &word);
		else
			dc_eeprom_getword(sc, off + i, &word);
		ptr = (uint16_t *)(dest + (i * 2));
		if (be)
			*ptr = be16toh(word);
		else
			*ptr = le16toh(word);
	}
}

/*
 * Write the MII serial port for the MII bit-bang module.
 */
static void
dc_mii_bitbang_write(device_t dev, uint32_t val)
{
	struct dc_softc *sc;

	sc = device_get_softc(dev);

	CSR_WRITE_4(sc, DC_SIO, val);
	CSR_BARRIER_4(sc, DC_SIO,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

/*
 * Read the MII serial port for the MII bit-bang module.
 */
static uint32_t
dc_mii_bitbang_read(device_t dev)
{
	struct dc_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);

	val = CSR_READ_4(sc, DC_SIO);
	CSR_BARRIER_4(sc, DC_SIO,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	return (val);
}

static int
dc_miibus_readreg(device_t dev, int phy, int reg)
{
	struct dc_softc *sc;
	int i, rval, phy_reg = 0;

	sc = device_get_softc(dev);

	if (sc->dc_pmode != DC_PMODE_MII) {
		if (phy == (MII_NPHY - 1)) {
			switch (reg) {
			case MII_BMSR:
			/*
			 * Fake something to make the probe
			 * code think there's a PHY here.
			 */
				return (BMSR_MEDIAMASK);
			case MII_PHYIDR1:
				if (DC_IS_PNIC(sc))
					return (DC_VENDORID_LO);
				return (DC_VENDORID_DEC);
			case MII_PHYIDR2:
				if (DC_IS_PNIC(sc))
					return (DC_DEVICEID_82C168);
				return (DC_DEVICEID_21143);
			default:
				return (0);
			}
		} else
			return (0);
	}

	if (DC_IS_PNIC(sc)) {
		CSR_WRITE_4(sc, DC_PN_MII, DC_PN_MIIOPCODE_READ |
		    (phy << 23) | (reg << 18));
		for (i = 0; i < DC_TIMEOUT; i++) {
			DELAY(1);
			rval = CSR_READ_4(sc, DC_PN_MII);
			if (!(rval & DC_PN_MII_BUSY)) {
				rval &= 0xFFFF;
				return (rval == 0xFFFF ? 0 : rval);
			}
		}
		return (0);
	}

	if (sc->dc_type == DC_TYPE_ULI_M5263) {
		CSR_WRITE_4(sc, DC_ROM,
		    ((phy << DC_ULI_PHY_ADDR_SHIFT) & DC_ULI_PHY_ADDR_MASK) |
		    ((reg << DC_ULI_PHY_REG_SHIFT) & DC_ULI_PHY_REG_MASK) |
		    DC_ULI_PHY_OP_READ);
		for (i = 0; i < DC_TIMEOUT; i++) {
			DELAY(1);
			rval = CSR_READ_4(sc, DC_ROM);
			if ((rval & DC_ULI_PHY_OP_DONE) != 0) {
				return (rval & DC_ULI_PHY_DATA_MASK);
			}
		}
		if (i == DC_TIMEOUT)
			device_printf(dev, "phy read timed out\n");
		return (0);
	}

	if (DC_IS_COMET(sc)) {
		switch (reg) {
		case MII_BMCR:
			phy_reg = DC_AL_BMCR;
			break;
		case MII_BMSR:
			phy_reg = DC_AL_BMSR;
			break;
		case MII_PHYIDR1:
			phy_reg = DC_AL_VENID;
			break;
		case MII_PHYIDR2:
			phy_reg = DC_AL_DEVID;
			break;
		case MII_ANAR:
			phy_reg = DC_AL_ANAR;
			break;
		case MII_ANLPAR:
			phy_reg = DC_AL_LPAR;
			break;
		case MII_ANER:
			phy_reg = DC_AL_ANER;
			break;
		default:
			device_printf(dev, "phy_read: bad phy register %x\n",
			    reg);
			return (0);
		}

		rval = CSR_READ_4(sc, phy_reg) & 0x0000FFFF;
		if (rval == 0xFFFF)
			return (0);
		return (rval);
	}

	if (sc->dc_type == DC_TYPE_98713) {
		phy_reg = CSR_READ_4(sc, DC_NETCFG);
		CSR_WRITE_4(sc, DC_NETCFG, phy_reg & ~DC_NETCFG_PORTSEL);
	}
	rval = mii_bitbang_readreg(dev, &dc_mii_bitbang_ops, phy, reg);
	if (sc->dc_type == DC_TYPE_98713)
		CSR_WRITE_4(sc, DC_NETCFG, phy_reg);

	return (rval);
}

static int
dc_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct dc_softc *sc;
	int i, phy_reg = 0;

	sc = device_get_softc(dev);

	if (DC_IS_PNIC(sc)) {
		CSR_WRITE_4(sc, DC_PN_MII, DC_PN_MIIOPCODE_WRITE |
		    (phy << 23) | (reg << 10) | data);
		for (i = 0; i < DC_TIMEOUT; i++) {
			if (!(CSR_READ_4(sc, DC_PN_MII) & DC_PN_MII_BUSY))
				break;
		}
		return (0);
	}

	if (sc->dc_type == DC_TYPE_ULI_M5263) {
		CSR_WRITE_4(sc, DC_ROM,
		    ((phy << DC_ULI_PHY_ADDR_SHIFT) & DC_ULI_PHY_ADDR_MASK) |
		    ((reg << DC_ULI_PHY_REG_SHIFT) & DC_ULI_PHY_REG_MASK) |
		    ((data << DC_ULI_PHY_DATA_SHIFT) & DC_ULI_PHY_DATA_MASK) |
		    DC_ULI_PHY_OP_WRITE);
		DELAY(1);
		return (0);
	}

	if (DC_IS_COMET(sc)) {
		switch (reg) {
		case MII_BMCR:
			phy_reg = DC_AL_BMCR;
			break;
		case MII_BMSR:
			phy_reg = DC_AL_BMSR;
			break;
		case MII_PHYIDR1:
			phy_reg = DC_AL_VENID;
			break;
		case MII_PHYIDR2:
			phy_reg = DC_AL_DEVID;
			break;
		case MII_ANAR:
			phy_reg = DC_AL_ANAR;
			break;
		case MII_ANLPAR:
			phy_reg = DC_AL_LPAR;
			break;
		case MII_ANER:
			phy_reg = DC_AL_ANER;
			break;
		default:
			device_printf(dev, "phy_write: bad phy register %x\n",
			    reg);
			return (0);
			break;
		}

		CSR_WRITE_4(sc, phy_reg, data);
		return (0);
	}

	if (sc->dc_type == DC_TYPE_98713) {
		phy_reg = CSR_READ_4(sc, DC_NETCFG);
		CSR_WRITE_4(sc, DC_NETCFG, phy_reg & ~DC_NETCFG_PORTSEL);
	}
	mii_bitbang_writereg(dev, &dc_mii_bitbang_ops, phy, reg, data);
	if (sc->dc_type == DC_TYPE_98713)
		CSR_WRITE_4(sc, DC_NETCFG, phy_reg);

	return (0);
}

static void
dc_miibus_statchg(device_t dev)
{
	struct dc_softc *sc;
	struct ifnet *ifp;
	struct mii_data *mii;
	struct ifmedia *ifm;

	sc = device_get_softc(dev);

	mii = device_get_softc(sc->dc_miibus);
	ifp = sc->dc_ifp;
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	ifm = &mii->mii_media;
	if (DC_IS_DAVICOM(sc) && IFM_SUBTYPE(ifm->ifm_media) == IFM_HPNA_1) {
		dc_setcfg(sc, ifm->ifm_media);
		return;
	} else if (!DC_IS_ADMTEK(sc))
		dc_setcfg(sc, mii->mii_media_active);

	sc->dc_link = 0;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->dc_link = 1;
			break;
		}
	}
}

/*
 * Special support for DM9102A cards with HomePNA PHYs. Note:
 * with the Davicom DM9102A/DM9801 eval board that I have, it seems
 * to be impossible to talk to the management interface of the DM9801
 * PHY (its MDIO pin is not connected to anything). Consequently,
 * the driver has to just 'know' about the additional mode and deal
 * with it itself. *sigh*
 */
static void
dc_miibus_mediainit(device_t dev)
{
	struct dc_softc *sc;
	struct mii_data *mii;
	struct ifmedia *ifm;
	int rev;

	rev = pci_get_revid(dev);

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->dc_miibus);
	ifm = &mii->mii_media;

	if (DC_IS_DAVICOM(sc) && rev >= DC_REVISION_DM9102A)
		ifmedia_add(ifm, IFM_ETHER | IFM_HPNA_1, 0, NULL);
}

#define	DC_BITS_512	9
#define	DC_BITS_128	7
#define	DC_BITS_64	6

static uint32_t
dc_mchash_le(struct dc_softc *sc, const uint8_t *addr)
{
	uint32_t crc;

	/* Compute CRC for the address value. */
	crc = ether_crc32_le(addr, ETHER_ADDR_LEN);

	/*
	 * The hash table on the PNIC II and the MX98715AEC-C/D/E
	 * chips is only 128 bits wide.
	 */
	if (sc->dc_flags & DC_128BIT_HASH)
		return (crc & ((1 << DC_BITS_128) - 1));

	/* The hash table on the MX98715BEC is only 64 bits wide. */
	if (sc->dc_flags & DC_64BIT_HASH)
		return (crc & ((1 << DC_BITS_64) - 1));

	/* Xircom's hash filtering table is different (read: weird) */
	/* Xircom uses the LEAST significant bits */
	if (DC_IS_XIRCOM(sc)) {
		if ((crc & 0x180) == 0x180)
			return ((crc & 0x0F) + (crc & 0x70) * 3 + (14 << 4));
		else
			return ((crc & 0x1F) + ((crc >> 1) & 0xF0) * 3 +
			    (12 << 4));
	}

	return (crc & ((1 << DC_BITS_512) - 1));
}

/*
 * Calculate CRC of a multicast group address, return the lower 6 bits.
 */
static uint32_t
dc_mchash_be(const uint8_t *addr)
{
	uint32_t crc;

	/* Compute CRC for the address value. */
	crc = ether_crc32_be(addr, ETHER_ADDR_LEN);

	/* Return the filter bit position. */
	return ((crc >> 26) & 0x0000003F);
}

/*
 * 21143-style RX filter setup routine. Filter programming is done by
 * downloading a special setup frame into the TX engine. 21143, Macronix,
 * PNIC, PNIC II and Davicom chips are programmed this way.
 *
 * We always program the chip using 'hash perfect' mode, i.e. one perfect
 * address (our node address) and a 512-bit hash filter for multicast
 * frames. We also sneak the broadcast address into the hash filter since
 * we need that too.
 */
static void
dc_setfilt_21143(struct dc_softc *sc)
{
	uint16_t eaddr[(ETHER_ADDR_LEN+1)/2];
	struct dc_desc *sframe;
	uint32_t h, *sp;
	struct ifmultiaddr *ifma;
	struct ifnet *ifp;
	int i;

	ifp = sc->dc_ifp;

	i = sc->dc_cdata.dc_tx_prod;
	DC_INC(sc->dc_cdata.dc_tx_prod, DC_TX_LIST_CNT);
	sc->dc_cdata.dc_tx_cnt++;
	sframe = &sc->dc_ldata.dc_tx_list[i];
	sp = sc->dc_cdata.dc_sbuf;
	bzero(sp, DC_SFRAME_LEN);

	sframe->dc_data = htole32(DC_ADDR_LO(sc->dc_saddr));
	sframe->dc_ctl = htole32(DC_SFRAME_LEN | DC_TXCTL_SETUP |
	    DC_TXCTL_TLINK | DC_FILTER_HASHPERF | DC_TXCTL_FINT);

	sc->dc_cdata.dc_tx_chain[i] = (struct mbuf *)sc->dc_cdata.dc_sbuf;

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC)
		DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_RX_PROMISC);
	else
		DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_RX_PROMISC);

	if (ifp->if_flags & IFF_ALLMULTI)
		DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_RX_ALLMULTI);
	else
		DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_RX_ALLMULTI);

	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = dc_mchash_le(sc,
		    LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
		sp[h >> 4] |= htole32(1 << (h & 0xF));
	}
	if_maddr_runlock(ifp);

	if (ifp->if_flags & IFF_BROADCAST) {
		h = dc_mchash_le(sc, ifp->if_broadcastaddr);
		sp[h >> 4] |= htole32(1 << (h & 0xF));
	}

	/* Set our MAC address. */
	bcopy(IF_LLADDR(sc->dc_ifp), eaddr, ETHER_ADDR_LEN);
	sp[39] = DC_SP_MAC(eaddr[0]);
	sp[40] = DC_SP_MAC(eaddr[1]);
	sp[41] = DC_SP_MAC(eaddr[2]);

	sframe->dc_status = htole32(DC_TXSTAT_OWN);
	bus_dmamap_sync(sc->dc_tx_ltag, sc->dc_tx_lmap, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->dc_stag, sc->dc_smap, BUS_DMASYNC_PREWRITE);
	CSR_WRITE_4(sc, DC_TXSTART, 0xFFFFFFFF);

	/*
	 * The PNIC takes an exceedingly long time to process its
	 * setup frame; wait 10ms after posting the setup frame
	 * before proceeding, just so it has time to swallow its
	 * medicine.
	 */
	DELAY(10000);

	sc->dc_wdog_timer = 5;
}

static void
dc_setfilt_admtek(struct dc_softc *sc)
{
	uint8_t eaddr[ETHER_ADDR_LEN];
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	int h = 0;
	uint32_t hashes[2] = { 0, 0 };

	ifp = sc->dc_ifp;

	/* Init our MAC address. */
	bcopy(IF_LLADDR(sc->dc_ifp), eaddr, ETHER_ADDR_LEN);
	CSR_WRITE_4(sc, DC_AL_PAR0, eaddr[3] << 24 | eaddr[2] << 16 |
	    eaddr[1] << 8 | eaddr[0]);
	CSR_WRITE_4(sc, DC_AL_PAR1, eaddr[5] << 8 | eaddr[4]);

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC)
		DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_RX_PROMISC);
	else
		DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_RX_PROMISC);

	if (ifp->if_flags & IFF_ALLMULTI)
		DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_RX_ALLMULTI);
	else
		DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_RX_ALLMULTI);

	/* First, zot all the existing hash bits. */
	CSR_WRITE_4(sc, DC_AL_MAR0, 0);
	CSR_WRITE_4(sc, DC_AL_MAR1, 0);

	/*
	 * If we're already in promisc or allmulti mode, we
	 * don't have to bother programming the multicast filter.
	 */
	if (ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI))
		return;

	/* Now program new ones. */
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		if (DC_IS_CENTAUR(sc))
			h = dc_mchash_le(sc,
			    LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
		else
			h = dc_mchash_be(
			    LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
		if (h < 32)
			hashes[0] |= (1 << h);
		else
			hashes[1] |= (1 << (h - 32));
	}
	if_maddr_runlock(ifp);

	CSR_WRITE_4(sc, DC_AL_MAR0, hashes[0]);
	CSR_WRITE_4(sc, DC_AL_MAR1, hashes[1]);
}

static void
dc_setfilt_asix(struct dc_softc *sc)
{
	uint32_t eaddr[(ETHER_ADDR_LEN+3)/4];
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	int h = 0;
	uint32_t hashes[2] = { 0, 0 };

	ifp = sc->dc_ifp;

	/* Init our MAC address. */
	bcopy(IF_LLADDR(sc->dc_ifp), eaddr, ETHER_ADDR_LEN);
	CSR_WRITE_4(sc, DC_AX_FILTIDX, DC_AX_FILTIDX_PAR0);
	CSR_WRITE_4(sc, DC_AX_FILTDATA, eaddr[0]);
	CSR_WRITE_4(sc, DC_AX_FILTIDX, DC_AX_FILTIDX_PAR1);
	CSR_WRITE_4(sc, DC_AX_FILTDATA, eaddr[1]);

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC)
		DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_RX_PROMISC);
	else
		DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_RX_PROMISC);

	if (ifp->if_flags & IFF_ALLMULTI)
		DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_RX_ALLMULTI);
	else
		DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_RX_ALLMULTI);

	/*
	 * The ASIX chip has a special bit to enable reception
	 * of broadcast frames.
	 */
	if (ifp->if_flags & IFF_BROADCAST)
		DC_SETBIT(sc, DC_NETCFG, DC_AX_NETCFG_RX_BROAD);
	else
		DC_CLRBIT(sc, DC_NETCFG, DC_AX_NETCFG_RX_BROAD);

	/* first, zot all the existing hash bits */
	CSR_WRITE_4(sc, DC_AX_FILTIDX, DC_AX_FILTIDX_MAR0);
	CSR_WRITE_4(sc, DC_AX_FILTDATA, 0);
	CSR_WRITE_4(sc, DC_AX_FILTIDX, DC_AX_FILTIDX_MAR1);
	CSR_WRITE_4(sc, DC_AX_FILTDATA, 0);

	/*
	 * If we're already in promisc or allmulti mode, we
	 * don't have to bother programming the multicast filter.
	 */
	if (ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI))
		return;

	/* now program new ones */
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = dc_mchash_be(LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
		if (h < 32)
			hashes[0] |= (1 << h);
		else
			hashes[1] |= (1 << (h - 32));
	}
	if_maddr_runlock(ifp);

	CSR_WRITE_4(sc, DC_AX_FILTIDX, DC_AX_FILTIDX_MAR0);
	CSR_WRITE_4(sc, DC_AX_FILTDATA, hashes[0]);
	CSR_WRITE_4(sc, DC_AX_FILTIDX, DC_AX_FILTIDX_MAR1);
	CSR_WRITE_4(sc, DC_AX_FILTDATA, hashes[1]);
}

static void
dc_setfilt_uli(struct dc_softc *sc)
{
	uint8_t eaddr[ETHER_ADDR_LEN];
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	struct dc_desc *sframe;
	uint32_t filter, *sp;
	uint8_t *ma;
	int i, mcnt;

	ifp = sc->dc_ifp;

	i = sc->dc_cdata.dc_tx_prod;
	DC_INC(sc->dc_cdata.dc_tx_prod, DC_TX_LIST_CNT);
	sc->dc_cdata.dc_tx_cnt++;
	sframe = &sc->dc_ldata.dc_tx_list[i];
	sp = sc->dc_cdata.dc_sbuf;
	bzero(sp, DC_SFRAME_LEN);

	sframe->dc_data = htole32(DC_ADDR_LO(sc->dc_saddr));
	sframe->dc_ctl = htole32(DC_SFRAME_LEN | DC_TXCTL_SETUP |
	    DC_TXCTL_TLINK | DC_FILTER_PERFECT | DC_TXCTL_FINT);

	sc->dc_cdata.dc_tx_chain[i] = (struct mbuf *)sc->dc_cdata.dc_sbuf;

	/* Set station address. */
	bcopy(IF_LLADDR(sc->dc_ifp), eaddr, ETHER_ADDR_LEN);
	*sp++ = DC_SP_MAC(eaddr[1] << 8 | eaddr[0]);
	*sp++ = DC_SP_MAC(eaddr[3] << 8 | eaddr[2]);
	*sp++ = DC_SP_MAC(eaddr[5] << 8 | eaddr[4]);

	/* Set broadcast address. */
	*sp++ = DC_SP_MAC(0xFFFF);
	*sp++ = DC_SP_MAC(0xFFFF);
	*sp++ = DC_SP_MAC(0xFFFF);

	/* Extract current filter configuration. */
	filter = CSR_READ_4(sc, DC_NETCFG);
	filter &= ~(DC_NETCFG_RX_PROMISC | DC_NETCFG_RX_ALLMULTI);

	/* Now build perfect filters. */
	mcnt = 0;
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		if (mcnt >= DC_ULI_FILTER_NPERF) {
			filter |= DC_NETCFG_RX_ALLMULTI;
			break;
		}
		ma = LLADDR((struct sockaddr_dl *)ifma->ifma_addr);
		*sp++ = DC_SP_MAC(ma[1] << 8 | ma[0]);
		*sp++ = DC_SP_MAC(ma[3] << 8 | ma[2]);
		*sp++ = DC_SP_MAC(ma[5] << 8 | ma[4]);
		mcnt++;
	}
	if_maddr_runlock(ifp);

	for (; mcnt < DC_ULI_FILTER_NPERF; mcnt++) {
		*sp++ = DC_SP_MAC(0xFFFF);
		*sp++ = DC_SP_MAC(0xFFFF);
		*sp++ = DC_SP_MAC(0xFFFF);
	}

	if (filter & (DC_NETCFG_TX_ON | DC_NETCFG_RX_ON))
		CSR_WRITE_4(sc, DC_NETCFG,
		    filter & ~(DC_NETCFG_TX_ON | DC_NETCFG_RX_ON));
	if (ifp->if_flags & IFF_PROMISC)
		filter |= DC_NETCFG_RX_PROMISC | DC_NETCFG_RX_ALLMULTI;
	if (ifp->if_flags & IFF_ALLMULTI)
		filter |= DC_NETCFG_RX_ALLMULTI;
	CSR_WRITE_4(sc, DC_NETCFG,
	    filter & ~(DC_NETCFG_TX_ON | DC_NETCFG_RX_ON));
	if (filter & (DC_NETCFG_TX_ON | DC_NETCFG_RX_ON))
		CSR_WRITE_4(sc, DC_NETCFG, filter);

	sframe->dc_status = htole32(DC_TXSTAT_OWN);
	bus_dmamap_sync(sc->dc_tx_ltag, sc->dc_tx_lmap, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->dc_stag, sc->dc_smap, BUS_DMASYNC_PREWRITE);
	CSR_WRITE_4(sc, DC_TXSTART, 0xFFFFFFFF);

	/*
	 * Wait some time...
	 */
	DELAY(1000);

	sc->dc_wdog_timer = 5;
}

static void
dc_setfilt_xircom(struct dc_softc *sc)
{
	uint16_t eaddr[(ETHER_ADDR_LEN+1)/2];
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	struct dc_desc *sframe;
	uint32_t h, *sp;
	int i;

	ifp = sc->dc_ifp;
	DC_CLRBIT(sc, DC_NETCFG, (DC_NETCFG_TX_ON | DC_NETCFG_RX_ON));

	i = sc->dc_cdata.dc_tx_prod;
	DC_INC(sc->dc_cdata.dc_tx_prod, DC_TX_LIST_CNT);
	sc->dc_cdata.dc_tx_cnt++;
	sframe = &sc->dc_ldata.dc_tx_list[i];
	sp = sc->dc_cdata.dc_sbuf;
	bzero(sp, DC_SFRAME_LEN);

	sframe->dc_data = htole32(DC_ADDR_LO(sc->dc_saddr));
	sframe->dc_ctl = htole32(DC_SFRAME_LEN | DC_TXCTL_SETUP |
	    DC_TXCTL_TLINK | DC_FILTER_HASHPERF | DC_TXCTL_FINT);

	sc->dc_cdata.dc_tx_chain[i] = (struct mbuf *)sc->dc_cdata.dc_sbuf;

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC)
		DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_RX_PROMISC);
	else
		DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_RX_PROMISC);

	if (ifp->if_flags & IFF_ALLMULTI)
		DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_RX_ALLMULTI);
	else
		DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_RX_ALLMULTI);

	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = dc_mchash_le(sc,
		    LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
		sp[h >> 4] |= htole32(1 << (h & 0xF));
	}
	if_maddr_runlock(ifp);

	if (ifp->if_flags & IFF_BROADCAST) {
		h = dc_mchash_le(sc, ifp->if_broadcastaddr);
		sp[h >> 4] |= htole32(1 << (h & 0xF));
	}

	/* Set our MAC address. */
	bcopy(IF_LLADDR(sc->dc_ifp), eaddr, ETHER_ADDR_LEN);
	sp[0] = DC_SP_MAC(eaddr[0]);
	sp[1] = DC_SP_MAC(eaddr[1]);
	sp[2] = DC_SP_MAC(eaddr[2]);

	DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_TX_ON);
	DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_RX_ON);
	sframe->dc_status = htole32(DC_TXSTAT_OWN);
	bus_dmamap_sync(sc->dc_tx_ltag, sc->dc_tx_lmap, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->dc_stag, sc->dc_smap, BUS_DMASYNC_PREWRITE);
	CSR_WRITE_4(sc, DC_TXSTART, 0xFFFFFFFF);

	/*
	 * Wait some time...
	 */
	DELAY(1000);

	sc->dc_wdog_timer = 5;
}

static void
dc_setfilt(struct dc_softc *sc)
{

	if (DC_IS_INTEL(sc) || DC_IS_MACRONIX(sc) || DC_IS_PNIC(sc) ||
	    DC_IS_PNICII(sc) || DC_IS_DAVICOM(sc) || DC_IS_CONEXANT(sc))
		dc_setfilt_21143(sc);

	if (DC_IS_ASIX(sc))
		dc_setfilt_asix(sc);

	if (DC_IS_ADMTEK(sc))
		dc_setfilt_admtek(sc);

	if (DC_IS_ULI(sc))
		dc_setfilt_uli(sc);

	if (DC_IS_XIRCOM(sc))
		dc_setfilt_xircom(sc);
}

static void
dc_netcfg_wait(struct dc_softc *sc)
{
	uint32_t isr;
	int i;

	for (i = 0; i < DC_TIMEOUT; i++) {
		isr = CSR_READ_4(sc, DC_ISR);
		if (isr & DC_ISR_TX_IDLE &&
		    ((isr & DC_ISR_RX_STATE) == DC_RXSTATE_STOPPED ||
		    (isr & DC_ISR_RX_STATE) == DC_RXSTATE_WAIT))
			break;
		DELAY(10);
	}
	if (i == DC_TIMEOUT && bus_child_present(sc->dc_dev)) {
		if (!(isr & DC_ISR_TX_IDLE) && !DC_IS_ASIX(sc))
			device_printf(sc->dc_dev,
			    "%s: failed to force tx to idle state\n", __func__);
		if (!((isr & DC_ISR_RX_STATE) == DC_RXSTATE_STOPPED ||
		    (isr & DC_ISR_RX_STATE) == DC_RXSTATE_WAIT) &&
		    !DC_HAS_BROKEN_RXSTATE(sc))
			device_printf(sc->dc_dev,
			    "%s: failed to force rx to idle state\n", __func__);
	}
}

/*
 * In order to fiddle with the 'full-duplex' and '100Mbps' bits in
 * the netconfig register, we first have to put the transmit and/or
 * receive logic in the idle state.
 */
static void
dc_setcfg(struct dc_softc *sc, int media)
{
	int restart = 0, watchdogreg;

	if (IFM_SUBTYPE(media) == IFM_NONE)
		return;

	if (CSR_READ_4(sc, DC_NETCFG) & (DC_NETCFG_TX_ON | DC_NETCFG_RX_ON)) {
		restart = 1;
		DC_CLRBIT(sc, DC_NETCFG, (DC_NETCFG_TX_ON | DC_NETCFG_RX_ON));
		dc_netcfg_wait(sc);
	}

	if (IFM_SUBTYPE(media) == IFM_100_TX) {
		DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_SPEEDSEL);
		DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_HEARTBEAT);
		if (sc->dc_pmode == DC_PMODE_MII) {
			if (DC_IS_INTEL(sc)) {
			/* There's a write enable bit here that reads as 1. */
				watchdogreg = CSR_READ_4(sc, DC_WATCHDOG);
				watchdogreg &= ~DC_WDOG_CTLWREN;
				watchdogreg |= DC_WDOG_JABBERDIS;
				CSR_WRITE_4(sc, DC_WATCHDOG, watchdogreg);
			} else {
				DC_SETBIT(sc, DC_WATCHDOG, DC_WDOG_JABBERDIS);
			}
			DC_CLRBIT(sc, DC_NETCFG, (DC_NETCFG_PCS |
			    DC_NETCFG_PORTSEL | DC_NETCFG_SCRAMBLER));
			if (sc->dc_type == DC_TYPE_98713)
				DC_SETBIT(sc, DC_NETCFG, (DC_NETCFG_PCS |
				    DC_NETCFG_SCRAMBLER));
			if (!DC_IS_DAVICOM(sc))
				DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_PORTSEL);
			DC_CLRBIT(sc, DC_10BTCTRL, 0xFFFF);
		} else {
			if (DC_IS_PNIC(sc)) {
				DC_PN_GPIO_SETBIT(sc, DC_PN_GPIO_SPEEDSEL);
				DC_PN_GPIO_SETBIT(sc, DC_PN_GPIO_100TX_LOOP);
				DC_SETBIT(sc, DC_PN_NWAY, DC_PN_NWAY_SPEEDSEL);
			}
			DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_PORTSEL);
			DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_PCS);
			DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_SCRAMBLER);
		}
	}

	if (IFM_SUBTYPE(media) == IFM_10_T) {
		DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_SPEEDSEL);
		DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_HEARTBEAT);
		if (sc->dc_pmode == DC_PMODE_MII) {
			/* There's a write enable bit here that reads as 1. */
			if (DC_IS_INTEL(sc)) {
				watchdogreg = CSR_READ_4(sc, DC_WATCHDOG);
				watchdogreg &= ~DC_WDOG_CTLWREN;
				watchdogreg |= DC_WDOG_JABBERDIS;
				CSR_WRITE_4(sc, DC_WATCHDOG, watchdogreg);
			} else {
				DC_SETBIT(sc, DC_WATCHDOG, DC_WDOG_JABBERDIS);
			}
			DC_CLRBIT(sc, DC_NETCFG, (DC_NETCFG_PCS |
			    DC_NETCFG_PORTSEL | DC_NETCFG_SCRAMBLER));
			if (sc->dc_type == DC_TYPE_98713)
				DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_PCS);
			if (!DC_IS_DAVICOM(sc))
				DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_PORTSEL);
			DC_CLRBIT(sc, DC_10BTCTRL, 0xFFFF);
		} else {
			if (DC_IS_PNIC(sc)) {
				DC_PN_GPIO_CLRBIT(sc, DC_PN_GPIO_SPEEDSEL);
				DC_PN_GPIO_SETBIT(sc, DC_PN_GPIO_100TX_LOOP);
				DC_CLRBIT(sc, DC_PN_NWAY, DC_PN_NWAY_SPEEDSEL);
			}
			DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_PORTSEL);
			DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_PCS);
			DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_SCRAMBLER);
			if (DC_IS_INTEL(sc)) {
				DC_CLRBIT(sc, DC_SIARESET, DC_SIA_RESET);
				DC_CLRBIT(sc, DC_10BTCTRL, 0xFFFF);
				if ((media & IFM_GMASK) == IFM_FDX)
					DC_SETBIT(sc, DC_10BTCTRL, 0x7F3D);
				else
					DC_SETBIT(sc, DC_10BTCTRL, 0x7F3F);
				DC_SETBIT(sc, DC_SIARESET, DC_SIA_RESET);
				DC_CLRBIT(sc, DC_10BTCTRL,
				    DC_TCTL_AUTONEGENBL);
				DELAY(20000);
			}
		}
	}

	/*
	 * If this is a Davicom DM9102A card with a DM9801 HomePNA
	 * PHY and we want HomePNA mode, set the portsel bit to turn
	 * on the external MII port.
	 */
	if (DC_IS_DAVICOM(sc)) {
		if (IFM_SUBTYPE(media) == IFM_HPNA_1) {
			DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_PORTSEL);
			sc->dc_link = 1;
		} else {
			DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_PORTSEL);
		}
	}

	if ((media & IFM_GMASK) == IFM_FDX) {
		DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_FULLDUPLEX);
		if (sc->dc_pmode == DC_PMODE_SYM && DC_IS_PNIC(sc))
			DC_SETBIT(sc, DC_PN_NWAY, DC_PN_NWAY_DUPLEX);
	} else {
		DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_FULLDUPLEX);
		if (sc->dc_pmode == DC_PMODE_SYM && DC_IS_PNIC(sc))
			DC_CLRBIT(sc, DC_PN_NWAY, DC_PN_NWAY_DUPLEX);
	}

	if (restart)
		DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_TX_ON | DC_NETCFG_RX_ON);
}

static void
dc_reset(struct dc_softc *sc)
{
	int i;

	DC_SETBIT(sc, DC_BUSCTL, DC_BUSCTL_RESET);

	for (i = 0; i < DC_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_4(sc, DC_BUSCTL) & DC_BUSCTL_RESET))
			break;
	}

	if (DC_IS_ASIX(sc) || DC_IS_ADMTEK(sc) || DC_IS_CONEXANT(sc) ||
	    DC_IS_XIRCOM(sc) || DC_IS_INTEL(sc) || DC_IS_ULI(sc)) {
		DELAY(10000);
		DC_CLRBIT(sc, DC_BUSCTL, DC_BUSCTL_RESET);
		i = 0;
	}

	if (i == DC_TIMEOUT)
		device_printf(sc->dc_dev, "reset never completed!\n");

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);

	CSR_WRITE_4(sc, DC_IMR, 0x00000000);
	CSR_WRITE_4(sc, DC_BUSCTL, 0x00000000);
	CSR_WRITE_4(sc, DC_NETCFG, 0x00000000);

	/*
	 * Bring the SIA out of reset. In some cases, it looks
	 * like failing to unreset the SIA soon enough gets it
	 * into a state where it will never come out of reset
	 * until we reset the whole chip again.
	 */
	if (DC_IS_INTEL(sc)) {
		DC_SETBIT(sc, DC_SIARESET, DC_SIA_RESET);
		CSR_WRITE_4(sc, DC_10BTCTRL, 0xFFFFFFFF);
		CSR_WRITE_4(sc, DC_WATCHDOG, 0);
	}
}

static const struct dc_type *
dc_devtype(device_t dev)
{
	const struct dc_type *t;
	uint32_t devid;
	uint8_t rev;

	t = dc_devs;
	devid = pci_get_devid(dev);
	rev = pci_get_revid(dev);

	while (t->dc_name != NULL) {
		if (devid == t->dc_devid && rev >= t->dc_minrev)
			return (t);
		t++;
	}

	return (NULL);
}

/*
 * Probe for a 21143 or clone chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 * We do a little bit of extra work to identify the exact type of
 * chip. The MX98713 and MX98713A have the same PCI vendor/device ID,
 * but different revision IDs. The same is true for 98715/98715A
 * chips and the 98725, as well as the ASIX and ADMtek chips. In some
 * cases, the exact chip revision affects driver behavior.
 */
static int
dc_probe(device_t dev)
{
	const struct dc_type *t;

	t = dc_devtype(dev);

	if (t != NULL) {
		device_set_desc(dev, t->dc_name);
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static void
dc_apply_fixup(struct dc_softc *sc, int media)
{
	struct dc_mediainfo *m;
	uint8_t *p;
	int i;
	uint32_t reg;

	m = sc->dc_mi;

	while (m != NULL) {
		if (m->dc_media == media)
			break;
		m = m->dc_next;
	}

	if (m == NULL)
		return;

	for (i = 0, p = m->dc_reset_ptr; i < m->dc_reset_len; i++, p += 2) {
		reg = (p[0] | (p[1] << 8)) << 16;
		CSR_WRITE_4(sc, DC_WATCHDOG, reg);
	}

	for (i = 0, p = m->dc_gp_ptr; i < m->dc_gp_len; i++, p += 2) {
		reg = (p[0] | (p[1] << 8)) << 16;
		CSR_WRITE_4(sc, DC_WATCHDOG, reg);
	}
}

static int
dc_decode_leaf_sia(struct dc_softc *sc, struct dc_eblock_sia *l)
{
	struct dc_mediainfo *m;

	m = malloc(sizeof(struct dc_mediainfo), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (m == NULL) {
		device_printf(sc->dc_dev, "Could not allocate mediainfo\n");
		return (ENOMEM);
	}
	switch (l->dc_sia_code & ~DC_SIA_CODE_EXT) {
	case DC_SIA_CODE_10BT:
		m->dc_media = IFM_10_T;
		break;
	case DC_SIA_CODE_10BT_FDX:
		m->dc_media = IFM_10_T | IFM_FDX;
		break;
	case DC_SIA_CODE_10B2:
		m->dc_media = IFM_10_2;
		break;
	case DC_SIA_CODE_10B5:
		m->dc_media = IFM_10_5;
		break;
	default:
		break;
	}

	/*
	 * We need to ignore CSR13, CSR14, CSR15 for SIA mode.
	 * Things apparently already work for cards that do
	 * supply Media Specific Data.
	 */
	if (l->dc_sia_code & DC_SIA_CODE_EXT) {
		m->dc_gp_len = 2;
		m->dc_gp_ptr =
		(uint8_t *)&l->dc_un.dc_sia_ext.dc_sia_gpio_ctl;
	} else {
		m->dc_gp_len = 2;
		m->dc_gp_ptr =
		(uint8_t *)&l->dc_un.dc_sia_noext.dc_sia_gpio_ctl;
	}

	m->dc_next = sc->dc_mi;
	sc->dc_mi = m;

	sc->dc_pmode = DC_PMODE_SIA;
	return (0);
}

static int
dc_decode_leaf_sym(struct dc_softc *sc, struct dc_eblock_sym *l)
{
	struct dc_mediainfo *m;

	m = malloc(sizeof(struct dc_mediainfo), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (m == NULL) {
		device_printf(sc->dc_dev, "Could not allocate mediainfo\n");
		return (ENOMEM);
	}
	if (l->dc_sym_code == DC_SYM_CODE_100BT)
		m->dc_media = IFM_100_TX;

	if (l->dc_sym_code == DC_SYM_CODE_100BT_FDX)
		m->dc_media = IFM_100_TX | IFM_FDX;

	m->dc_gp_len = 2;
	m->dc_gp_ptr = (uint8_t *)&l->dc_sym_gpio_ctl;

	m->dc_next = sc->dc_mi;
	sc->dc_mi = m;

	sc->dc_pmode = DC_PMODE_SYM;
	return (0);
}

static int
dc_decode_leaf_mii(struct dc_softc *sc, struct dc_eblock_mii *l)
{
	struct dc_mediainfo *m;
	uint8_t *p;

	m = malloc(sizeof(struct dc_mediainfo), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (m == NULL) {
		device_printf(sc->dc_dev, "Could not allocate mediainfo\n");
		return (ENOMEM);
	}
	/* We abuse IFM_AUTO to represent MII. */
	m->dc_media = IFM_AUTO;
	m->dc_gp_len = l->dc_gpr_len;

	p = (uint8_t *)l;
	p += sizeof(struct dc_eblock_mii);
	m->dc_gp_ptr = p;
	p += 2 * l->dc_gpr_len;
	m->dc_reset_len = *p;
	p++;
	m->dc_reset_ptr = p;

	m->dc_next = sc->dc_mi;
	sc->dc_mi = m;
	return (0);
}

static int
dc_read_srom(struct dc_softc *sc, int bits)
{
	int size;

	size = DC_ROM_SIZE(bits);
	sc->dc_srom = malloc(size, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->dc_srom == NULL) {
		device_printf(sc->dc_dev, "Could not allocate SROM buffer\n");
		return (ENOMEM);
	}
	dc_read_eeprom(sc, (caddr_t)sc->dc_srom, 0, (size / 2), 0);
	return (0);
}

static int
dc_parse_21143_srom(struct dc_softc *sc)
{
	struct dc_leaf_hdr *lhdr;
	struct dc_eblock_hdr *hdr;
	int error, have_mii, i, loff;
	char *ptr;

	have_mii = 0;
	loff = sc->dc_srom[27];
	lhdr = (struct dc_leaf_hdr *)&(sc->dc_srom[loff]);

	ptr = (char *)lhdr;
	ptr += sizeof(struct dc_leaf_hdr) - 1;
	/*
	 * Look if we got a MII media block.
	 */
	for (i = 0; i < lhdr->dc_mcnt; i++) {
		hdr = (struct dc_eblock_hdr *)ptr;
		if (hdr->dc_type == DC_EBLOCK_MII)
		    have_mii++;

		ptr += (hdr->dc_len & 0x7F);
		ptr++;
	}

	/*
	 * Do the same thing again. Only use SIA and SYM media
	 * blocks if no MII media block is available.
	 */
	ptr = (char *)lhdr;
	ptr += sizeof(struct dc_leaf_hdr) - 1;
	error = 0;
	for (i = 0; i < lhdr->dc_mcnt; i++) {
		hdr = (struct dc_eblock_hdr *)ptr;
		switch (hdr->dc_type) {
		case DC_EBLOCK_MII:
			error = dc_decode_leaf_mii(sc, (struct dc_eblock_mii *)hdr);
			break;
		case DC_EBLOCK_SIA:
			if (! have_mii)
				error = dc_decode_leaf_sia(sc,
				    (struct dc_eblock_sia *)hdr);
			break;
		case DC_EBLOCK_SYM:
			if (! have_mii)
				error = dc_decode_leaf_sym(sc,
				    (struct dc_eblock_sym *)hdr);
			break;
		default:
			/* Don't care. Yet. */
			break;
		}
		ptr += (hdr->dc_len & 0x7F);
		ptr++;
	}
	return (error);
}

static void
dc_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *paddr;

	KASSERT(nseg == 1,
	    ("%s: wrong number of segments (%d)", __func__, nseg));
	paddr = arg;
	*paddr = segs->ds_addr;
}

static int
dc_dma_alloc(struct dc_softc *sc)
{
	int error, i;

	error = bus_dma_tag_create(bus_get_dma_tag(sc->dc_dev), 1, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    BUS_SPACE_MAXSIZE_32BIT, 0, BUS_SPACE_MAXSIZE_32BIT, 0,
	    NULL, NULL, &sc->dc_ptag);
	if (error) {
		device_printf(sc->dc_dev,
		    "failed to allocate parent DMA tag\n");
		goto fail;
	}

	/* Allocate a busdma tag and DMA safe memory for TX/RX descriptors. */
	error = bus_dma_tag_create(sc->dc_ptag, DC_LIST_ALIGN, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL, DC_RX_LIST_SZ, 1,
	    DC_RX_LIST_SZ, 0, NULL, NULL, &sc->dc_rx_ltag);
	if (error) {
		device_printf(sc->dc_dev, "failed to create RX list DMA tag\n");
		goto fail;
	}

	error = bus_dma_tag_create(sc->dc_ptag, DC_LIST_ALIGN, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL, DC_TX_LIST_SZ, 1,
	    DC_TX_LIST_SZ, 0, NULL, NULL, &sc->dc_tx_ltag);
	if (error) {
		device_printf(sc->dc_dev, "failed to create TX list DMA tag\n");
		goto fail;
	}

	/* RX descriptor list. */
	error = bus_dmamem_alloc(sc->dc_rx_ltag,
	    (void **)&sc->dc_ldata.dc_rx_list, BUS_DMA_NOWAIT |
	    BUS_DMA_ZERO | BUS_DMA_COHERENT, &sc->dc_rx_lmap);
	if (error) {
		device_printf(sc->dc_dev,
		    "failed to allocate DMA'able memory for RX list\n");
		goto fail;
	}
	error = bus_dmamap_load(sc->dc_rx_ltag, sc->dc_rx_lmap,
	    sc->dc_ldata.dc_rx_list, DC_RX_LIST_SZ, dc_dma_map_addr,
	    &sc->dc_ldata.dc_rx_list_paddr, BUS_DMA_NOWAIT);
	if (error) {
		device_printf(sc->dc_dev,
		    "failed to load DMA'able memory for RX list\n");
		goto fail;
	}
	/* TX descriptor list. */
	error = bus_dmamem_alloc(sc->dc_tx_ltag,
	    (void **)&sc->dc_ldata.dc_tx_list, BUS_DMA_NOWAIT |
	    BUS_DMA_ZERO | BUS_DMA_COHERENT, &sc->dc_tx_lmap);
	if (error) {
		device_printf(sc->dc_dev,
		    "failed to allocate DMA'able memory for TX list\n");
		goto fail;
	}
	error = bus_dmamap_load(sc->dc_tx_ltag, sc->dc_tx_lmap,
	    sc->dc_ldata.dc_tx_list, DC_TX_LIST_SZ, dc_dma_map_addr,
	    &sc->dc_ldata.dc_tx_list_paddr, BUS_DMA_NOWAIT);
	if (error) {
		device_printf(sc->dc_dev,
		    "cannot load DMA'able memory for TX list\n");
		goto fail;
	}

	/*
	 * Allocate a busdma tag and DMA safe memory for the multicast
	 * setup frame.
	 */
	error = bus_dma_tag_create(sc->dc_ptag, DC_LIST_ALIGN, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    DC_SFRAME_LEN + DC_MIN_FRAMELEN, 1, DC_SFRAME_LEN + DC_MIN_FRAMELEN,
	    0, NULL, NULL, &sc->dc_stag);
	if (error) {
		device_printf(sc->dc_dev,
		    "failed to create DMA tag for setup frame\n");
		goto fail;
	}
	error = bus_dmamem_alloc(sc->dc_stag, (void **)&sc->dc_cdata.dc_sbuf,
	    BUS_DMA_NOWAIT, &sc->dc_smap);
	if (error) {
		device_printf(sc->dc_dev,
		    "failed to allocate DMA'able memory for setup frame\n");
		goto fail;
	}
	error = bus_dmamap_load(sc->dc_stag, sc->dc_smap, sc->dc_cdata.dc_sbuf,
	    DC_SFRAME_LEN, dc_dma_map_addr, &sc->dc_saddr, BUS_DMA_NOWAIT);
	if (error) {
		device_printf(sc->dc_dev,
		    "cannot load DMA'able memory for setup frame\n");
		goto fail;
	}

	/* Allocate a busdma tag for RX mbufs. */
	error = bus_dma_tag_create(sc->dc_ptag, DC_RXBUF_ALIGN, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    MCLBYTES, 1, MCLBYTES, 0, NULL, NULL, &sc->dc_rx_mtag);
	if (error) {
		device_printf(sc->dc_dev, "failed to create RX mbuf tag\n");
		goto fail;
	}

	/* Allocate a busdma tag for TX mbufs. */
	error = bus_dma_tag_create(sc->dc_ptag, 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    MCLBYTES * DC_MAXFRAGS, DC_MAXFRAGS, MCLBYTES,
	    0, NULL, NULL, &sc->dc_tx_mtag);
	if (error) {
		device_printf(sc->dc_dev, "failed to create TX mbuf tag\n");
		goto fail;
	}

	/* Create the TX/RX busdma maps. */
	for (i = 0; i < DC_TX_LIST_CNT; i++) {
		error = bus_dmamap_create(sc->dc_tx_mtag, 0,
		    &sc->dc_cdata.dc_tx_map[i]);
		if (error) {
			device_printf(sc->dc_dev,
			    "failed to create TX mbuf dmamap\n");
			goto fail;
		}
	}
	for (i = 0; i < DC_RX_LIST_CNT; i++) {
		error = bus_dmamap_create(sc->dc_rx_mtag, 0,
		    &sc->dc_cdata.dc_rx_map[i]);
		if (error) {
			device_printf(sc->dc_dev,
			    "failed to create RX mbuf dmamap\n");
			goto fail;
		}
	}
	error = bus_dmamap_create(sc->dc_rx_mtag, 0, &sc->dc_sparemap);
	if (error) {
		device_printf(sc->dc_dev,
		    "failed to create spare RX mbuf dmamap\n");
		goto fail;
	}

fail:
	return (error);
}

static void
dc_dma_free(struct dc_softc *sc)
{
	int i;

	/* RX buffers. */
	if (sc->dc_rx_mtag != NULL) {
		for (i = 0; i < DC_RX_LIST_CNT; i++) {
			if (sc->dc_cdata.dc_rx_map[i] != NULL)
				bus_dmamap_destroy(sc->dc_rx_mtag,
				    sc->dc_cdata.dc_rx_map[i]);
		}
		if (sc->dc_sparemap != NULL)
			bus_dmamap_destroy(sc->dc_rx_mtag, sc->dc_sparemap);
		bus_dma_tag_destroy(sc->dc_rx_mtag);
	}

	/* TX buffers. */
	if (sc->dc_rx_mtag != NULL) {
		for (i = 0; i < DC_TX_LIST_CNT; i++) {
			if (sc->dc_cdata.dc_tx_map[i] != NULL)
				bus_dmamap_destroy(sc->dc_tx_mtag,
				    sc->dc_cdata.dc_tx_map[i]);
		}
		bus_dma_tag_destroy(sc->dc_tx_mtag);
	}

	/* RX descriptor list. */
	if (sc->dc_rx_ltag) {
		if (sc->dc_ldata.dc_rx_list_paddr != 0)
			bus_dmamap_unload(sc->dc_rx_ltag, sc->dc_rx_lmap);
		if (sc->dc_ldata.dc_rx_list != NULL)
			bus_dmamem_free(sc->dc_rx_ltag, sc->dc_ldata.dc_rx_list,
			    sc->dc_rx_lmap);
		bus_dma_tag_destroy(sc->dc_rx_ltag);
	}

	/* TX descriptor list. */
	if (sc->dc_tx_ltag) {
		if (sc->dc_ldata.dc_tx_list_paddr != 0)
			bus_dmamap_unload(sc->dc_tx_ltag, sc->dc_tx_lmap);
		if (sc->dc_ldata.dc_tx_list != NULL)
			bus_dmamem_free(sc->dc_tx_ltag, sc->dc_ldata.dc_tx_list,
			    sc->dc_tx_lmap);
		bus_dma_tag_destroy(sc->dc_tx_ltag);
	}

	/* multicast setup frame. */
	if (sc->dc_stag) {
		if (sc->dc_saddr != 0)
			bus_dmamap_unload(sc->dc_stag, sc->dc_smap);
		if (sc->dc_cdata.dc_sbuf != NULL)
			bus_dmamem_free(sc->dc_stag, sc->dc_cdata.dc_sbuf,
			    sc->dc_smap);
		bus_dma_tag_destroy(sc->dc_stag);
	}
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
dc_attach(device_t dev)
{
	uint32_t eaddr[(ETHER_ADDR_LEN+3)/4];
	uint32_t command;
	struct dc_softc *sc;
	struct ifnet *ifp;
	struct dc_mediainfo *m;
	uint32_t reg, revision;
	uint16_t *srom;
	int error, mac_offset, n, phy, rid, tmp;
	uint8_t *mac;

	sc = device_get_softc(dev);
	sc->dc_dev = dev;

	mtx_init(&sc->dc_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);

	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

	rid = DC_RID;
	sc->dc_res = bus_alloc_resource_any(dev, DC_RES, &rid, RF_ACTIVE);

	if (sc->dc_res == NULL) {
		device_printf(dev, "couldn't map ports/memory\n");
		error = ENXIO;
		goto fail;
	}

	sc->dc_btag = rman_get_bustag(sc->dc_res);
	sc->dc_bhandle = rman_get_bushandle(sc->dc_res);

	/* Allocate interrupt. */
	rid = 0;
	sc->dc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->dc_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	/* Need this info to decide on a chip type. */
	sc->dc_info = dc_devtype(dev);
	revision = pci_get_revid(dev);

	error = 0;
	/* Get the eeprom width, but PNIC and XIRCOM have diff eeprom */
	if (sc->dc_info->dc_devid !=
	    DC_DEVID(DC_VENDORID_LO, DC_DEVICEID_82C168) &&
	    sc->dc_info->dc_devid !=
	    DC_DEVID(DC_VENDORID_XIRCOM, DC_DEVICEID_X3201))
		dc_eeprom_width(sc);

	switch (sc->dc_info->dc_devid) {
	case DC_DEVID(DC_VENDORID_DEC, DC_DEVICEID_21143):
		sc->dc_type = DC_TYPE_21143;
		sc->dc_flags |= DC_TX_POLL | DC_TX_USE_TX_INTR;
		sc->dc_flags |= DC_REDUCED_MII_POLL;
		/* Save EEPROM contents so we can parse them later. */
		error = dc_read_srom(sc, sc->dc_romwidth);
		if (error != 0)
			goto fail;
		break;
	case DC_DEVID(DC_VENDORID_DAVICOM, DC_DEVICEID_DM9009):
	case DC_DEVID(DC_VENDORID_DAVICOM, DC_DEVICEID_DM9100):
	case DC_DEVID(DC_VENDORID_DAVICOM, DC_DEVICEID_DM9102):
		sc->dc_type = DC_TYPE_DM9102;
		sc->dc_flags |= DC_TX_COALESCE | DC_TX_INTR_ALWAYS;
		sc->dc_flags |= DC_REDUCED_MII_POLL | DC_TX_STORENFWD;
		sc->dc_flags |= DC_TX_ALIGN;
		sc->dc_pmode = DC_PMODE_MII;

		/* Increase the latency timer value. */
		pci_write_config(dev, PCIR_LATTIMER, 0x80, 1);
		break;
	case DC_DEVID(DC_VENDORID_ADMTEK, DC_DEVICEID_AL981):
		sc->dc_type = DC_TYPE_AL981;
		sc->dc_flags |= DC_TX_USE_TX_INTR;
		sc->dc_flags |= DC_TX_ADMTEK_WAR;
		sc->dc_pmode = DC_PMODE_MII;
		error = dc_read_srom(sc, sc->dc_romwidth);
		if (error != 0)
			goto fail;
		break;
	case DC_DEVID(DC_VENDORID_ADMTEK, DC_DEVICEID_AN983):
	case DC_DEVID(DC_VENDORID_ADMTEK, DC_DEVICEID_AN985):
	case DC_DEVID(DC_VENDORID_ADMTEK, DC_DEVICEID_ADM9511):
	case DC_DEVID(DC_VENDORID_ADMTEK, DC_DEVICEID_ADM9513):
	case DC_DEVID(DC_VENDORID_DLINK, DC_DEVICEID_DRP32TXD):
	case DC_DEVID(DC_VENDORID_ABOCOM, DC_DEVICEID_FE2500):
	case DC_DEVID(DC_VENDORID_ABOCOM, DC_DEVICEID_FE2500MX):
	case DC_DEVID(DC_VENDORID_ACCTON, DC_DEVICEID_EN2242):
	case DC_DEVID(DC_VENDORID_HAWKING, DC_DEVICEID_HAWKING_PN672TX):
	case DC_DEVID(DC_VENDORID_PLANEX, DC_DEVICEID_FNW3602T):
	case DC_DEVID(DC_VENDORID_3COM, DC_DEVICEID_3CSOHOB):
	case DC_DEVID(DC_VENDORID_MICROSOFT, DC_DEVICEID_MSMN120):
	case DC_DEVID(DC_VENDORID_MICROSOFT, DC_DEVICEID_MSMN130):
	case DC_DEVID(DC_VENDORID_LINKSYS, DC_DEVICEID_PCMPC200_AB08):
	case DC_DEVID(DC_VENDORID_LINKSYS, DC_DEVICEID_PCMPC200_AB09):
		sc->dc_type = DC_TYPE_AN983;
		sc->dc_flags |= DC_64BIT_HASH;
		sc->dc_flags |= DC_TX_USE_TX_INTR;
		sc->dc_flags |= DC_TX_ADMTEK_WAR;
		sc->dc_pmode = DC_PMODE_MII;
		/* Don't read SROM for - auto-loaded on reset */
		break;
	case DC_DEVID(DC_VENDORID_MX, DC_DEVICEID_98713):
	case DC_DEVID(DC_VENDORID_CP, DC_DEVICEID_98713_CP):
		if (revision < DC_REVISION_98713A) {
			sc->dc_type = DC_TYPE_98713;
		}
		if (revision >= DC_REVISION_98713A) {
			sc->dc_type = DC_TYPE_98713A;
			sc->dc_flags |= DC_21143_NWAY;
		}
		sc->dc_flags |= DC_REDUCED_MII_POLL;
		sc->dc_flags |= DC_TX_POLL | DC_TX_USE_TX_INTR;
		break;
	case DC_DEVID(DC_VENDORID_MX, DC_DEVICEID_987x5):
	case DC_DEVID(DC_VENDORID_ACCTON, DC_DEVICEID_EN1217):
		/*
		 * Macronix MX98715AEC-C/D/E parts have only a
		 * 128-bit hash table. We need to deal with these
		 * in the same manner as the PNIC II so that we
		 * get the right number of bits out of the
		 * CRC routine.
		 */
		if (revision >= DC_REVISION_98715AEC_C &&
		    revision < DC_REVISION_98725)
			sc->dc_flags |= DC_128BIT_HASH;
		sc->dc_type = DC_TYPE_987x5;
		sc->dc_flags |= DC_TX_POLL | DC_TX_USE_TX_INTR;
		sc->dc_flags |= DC_REDUCED_MII_POLL | DC_21143_NWAY;
		break;
	case DC_DEVID(DC_VENDORID_MX, DC_DEVICEID_98727):
		sc->dc_type = DC_TYPE_987x5;
		sc->dc_flags |= DC_TX_POLL | DC_TX_USE_TX_INTR;
		sc->dc_flags |= DC_REDUCED_MII_POLL | DC_21143_NWAY;
		break;
	case DC_DEVID(DC_VENDORID_LO, DC_DEVICEID_82C115):
		sc->dc_type = DC_TYPE_PNICII;
		sc->dc_flags |= DC_TX_POLL | DC_TX_USE_TX_INTR | DC_128BIT_HASH;
		sc->dc_flags |= DC_REDUCED_MII_POLL | DC_21143_NWAY;
		break;
	case DC_DEVID(DC_VENDORID_LO, DC_DEVICEID_82C168):
		sc->dc_type = DC_TYPE_PNIC;
		sc->dc_flags |= DC_TX_STORENFWD | DC_TX_INTR_ALWAYS;
		sc->dc_flags |= DC_PNIC_RX_BUG_WAR;
		sc->dc_pnic_rx_buf = malloc(DC_RXLEN * 5, M_DEVBUF, M_NOWAIT);
		if (sc->dc_pnic_rx_buf == NULL) {
			device_printf(sc->dc_dev,
			    "Could not allocate PNIC RX buffer\n");
			error = ENOMEM;
			goto fail;
		}
		if (revision < DC_REVISION_82C169)
			sc->dc_pmode = DC_PMODE_SYM;
		break;
	case DC_DEVID(DC_VENDORID_ASIX, DC_DEVICEID_AX88140A):
		sc->dc_type = DC_TYPE_ASIX;
		sc->dc_flags |= DC_TX_USE_TX_INTR | DC_TX_INTR_FIRSTFRAG;
		sc->dc_flags |= DC_REDUCED_MII_POLL;
		sc->dc_pmode = DC_PMODE_MII;
		break;
	case DC_DEVID(DC_VENDORID_XIRCOM, DC_DEVICEID_X3201):
		sc->dc_type = DC_TYPE_XIRCOM;
		sc->dc_flags |= DC_TX_INTR_ALWAYS | DC_TX_COALESCE |
				DC_TX_ALIGN;
		/*
		 * We don't actually need to coalesce, but we're doing
		 * it to obtain a double word aligned buffer.
		 * The DC_TX_COALESCE flag is required.
		 */
		sc->dc_pmode = DC_PMODE_MII;
		break;
	case DC_DEVID(DC_VENDORID_CONEXANT, DC_DEVICEID_RS7112):
		sc->dc_type = DC_TYPE_CONEXANT;
		sc->dc_flags |= DC_TX_INTR_ALWAYS;
		sc->dc_flags |= DC_REDUCED_MII_POLL;
		sc->dc_pmode = DC_PMODE_MII;
		error = dc_read_srom(sc, sc->dc_romwidth);
		if (error != 0)
			goto fail;
		break;
	case DC_DEVID(DC_VENDORID_ULI, DC_DEVICEID_M5261):
	case DC_DEVID(DC_VENDORID_ULI, DC_DEVICEID_M5263):
		if (sc->dc_info->dc_devid ==
		    DC_DEVID(DC_VENDORID_ULI, DC_DEVICEID_M5261))
			sc->dc_type = DC_TYPE_ULI_M5261;
		else
			sc->dc_type = DC_TYPE_ULI_M5263;
		/* TX buffers should be aligned on 4 byte boundary. */
		sc->dc_flags |= DC_TX_INTR_ALWAYS | DC_TX_COALESCE |
		    DC_TX_ALIGN;
		sc->dc_pmode = DC_PMODE_MII;
		error = dc_read_srom(sc, sc->dc_romwidth);
		if (error != 0)
			goto fail;
		break;
	default:
		device_printf(dev, "unknown device: %x\n",
		    sc->dc_info->dc_devid);
		break;
	}

	/* Save the cache line size. */
	if (DC_IS_DAVICOM(sc))
		sc->dc_cachesize = 0;
	else
		sc->dc_cachesize = pci_get_cachelnsz(dev);

	/* Reset the adapter. */
	dc_reset(sc);

	/* Take 21143 out of snooze mode */
	if (DC_IS_INTEL(sc) || DC_IS_XIRCOM(sc)) {
		command = pci_read_config(dev, DC_PCI_CFDD, 4);
		command &= ~(DC_CFDD_SNOOZE_MODE | DC_CFDD_SLEEP_MODE);
		pci_write_config(dev, DC_PCI_CFDD, command, 4);
	}

	/*
	 * Try to learn something about the supported media.
	 * We know that ASIX and ADMtek and Davicom devices
	 * will *always* be using MII media, so that's a no-brainer.
	 * The tricky ones are the Macronix/PNIC II and the
	 * Intel 21143.
	 */
	if (DC_IS_INTEL(sc)) {
		error = dc_parse_21143_srom(sc);
		if (error != 0)
			goto fail;
	} else if (DC_IS_MACRONIX(sc) || DC_IS_PNICII(sc)) {
		if (sc->dc_type == DC_TYPE_98713)
			sc->dc_pmode = DC_PMODE_MII;
		else
			sc->dc_pmode = DC_PMODE_SYM;
	} else if (!sc->dc_pmode)
		sc->dc_pmode = DC_PMODE_MII;

	/*
	 * Get station address from the EEPROM.
	 */
	switch(sc->dc_type) {
	case DC_TYPE_98713:
	case DC_TYPE_98713A:
	case DC_TYPE_987x5:
	case DC_TYPE_PNICII:
		dc_read_eeprom(sc, (caddr_t)&mac_offset,
		    (DC_EE_NODEADDR_OFFSET / 2), 1, 0);
		dc_read_eeprom(sc, (caddr_t)&eaddr, (mac_offset / 2), 3, 0);
		break;
	case DC_TYPE_PNIC:
		dc_read_eeprom(sc, (caddr_t)&eaddr, 0, 3, 1);
		break;
	case DC_TYPE_DM9102:
		dc_read_eeprom(sc, (caddr_t)&eaddr, DC_EE_NODEADDR, 3, 0);
#ifdef __sparc64__
		/*
		 * If this is an onboard dc(4) the station address read from
		 * the EEPROM is all zero and we have to get it from the FCode.
		 */
		if (eaddr[0] == 0 && (eaddr[1] & ~0xffff) == 0)
			OF_getetheraddr(dev, (caddr_t)&eaddr);
#endif
		break;
	case DC_TYPE_21143:
	case DC_TYPE_ASIX:
		dc_read_eeprom(sc, (caddr_t)&eaddr, DC_EE_NODEADDR, 3, 0);
		break;
	case DC_TYPE_AL981:
	case DC_TYPE_AN983:
		reg = CSR_READ_4(sc, DC_AL_PAR0);
		mac = (uint8_t *)&eaddr[0];
		mac[0] = (reg >> 0) & 0xff;
		mac[1] = (reg >> 8) & 0xff;
		mac[2] = (reg >> 16) & 0xff;
		mac[3] = (reg >> 24) & 0xff;
		reg = CSR_READ_4(sc, DC_AL_PAR1);
		mac[4] = (reg >> 0) & 0xff;
		mac[5] = (reg >> 8) & 0xff;
		break;
	case DC_TYPE_CONEXANT:
		bcopy(sc->dc_srom + DC_CONEXANT_EE_NODEADDR, &eaddr,
		    ETHER_ADDR_LEN);
		break;
	case DC_TYPE_XIRCOM:
		/* The MAC comes from the CIS. */
		mac = pci_get_ether(dev);
		if (!mac) {
			device_printf(dev, "No station address in CIS!\n");
			error = ENXIO;
			goto fail;
		}
		bcopy(mac, eaddr, ETHER_ADDR_LEN);
		break;
	case DC_TYPE_ULI_M5261:
	case DC_TYPE_ULI_M5263:
		srom = (uint16_t *)sc->dc_srom;
		if (srom == NULL || *srom == 0xFFFF || *srom == 0) {
			/*
			 * No valid SROM present, read station address
			 * from ID Table.
			 */
			device_printf(dev,
			    "Reading station address from ID Table.\n");
			CSR_WRITE_4(sc, DC_BUSCTL, 0x10000);
			CSR_WRITE_4(sc, DC_SIARESET, 0x01C0);
			CSR_WRITE_4(sc, DC_10BTCTRL, 0x0000);
			CSR_WRITE_4(sc, DC_10BTCTRL, 0x0010);
			CSR_WRITE_4(sc, DC_10BTCTRL, 0x0000);
			CSR_WRITE_4(sc, DC_SIARESET, 0x0000);
			CSR_WRITE_4(sc, DC_SIARESET, 0x01B0);
			mac = (uint8_t *)eaddr;
			for (n = 0; n < ETHER_ADDR_LEN; n++)
				mac[n] = (uint8_t)CSR_READ_4(sc, DC_10BTCTRL);
			CSR_WRITE_4(sc, DC_SIARESET, 0x0000);
			CSR_WRITE_4(sc, DC_BUSCTL, 0x0000);
			DELAY(10);
		} else
			dc_read_eeprom(sc, (caddr_t)&eaddr, DC_EE_NODEADDR, 3,
			    0);
		break;
	default:
		dc_read_eeprom(sc, (caddr_t)&eaddr, DC_EE_NODEADDR, 3, 0);
		break;
	}

	bcopy(eaddr, sc->dc_eaddr, sizeof(eaddr));
	/*
	 * If we still have invalid station address, see whether we can
	 * find station address for chip 0.  Some multi-port controllers
	 * just store station address for chip 0 if they have a shared
	 * SROM.
	 */
	if ((sc->dc_eaddr[0] == 0 && (sc->dc_eaddr[1] & ~0xffff) == 0) ||
	    (sc->dc_eaddr[0] == 0xffffffff &&
	    (sc->dc_eaddr[1] & 0xffff) == 0xffff)) {
		error = dc_check_multiport(sc);
		if (error == 0) {
			bcopy(sc->dc_eaddr, eaddr, sizeof(eaddr));
			/* Extract media information. */
			if (DC_IS_INTEL(sc) && sc->dc_srom != NULL) {
				while (sc->dc_mi != NULL) {
					m = sc->dc_mi->dc_next;
					free(sc->dc_mi, M_DEVBUF);
					sc->dc_mi = m;
				}
				error = dc_parse_21143_srom(sc);
				if (error != 0)
					goto fail;
			}
		} else if (error == ENOMEM)
			goto fail;
		else
			error = 0;
	}

	if ((error = dc_dma_alloc(sc)) != 0)
		goto fail;

	ifp = sc->dc_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_alloc()\n");
		error = ENOSPC;
		goto fail;
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = dc_ioctl;
	ifp->if_start = dc_start;
	ifp->if_init = dc_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, DC_TX_LIST_CNT - 1);
	ifp->if_snd.ifq_drv_maxlen = DC_TX_LIST_CNT - 1;
	IFQ_SET_READY(&ifp->if_snd);

	/*
	 * Do MII setup. If this is a 21143, check for a PHY on the
	 * MII bus after applying any necessary fixups to twiddle the
	 * GPIO bits. If we don't end up finding a PHY, restore the
	 * old selection (SIA only or SIA/SYM) and attach the dcphy
	 * driver instead.
	 */
	tmp = 0;
	if (DC_IS_INTEL(sc)) {
		dc_apply_fixup(sc, IFM_AUTO);
		tmp = sc->dc_pmode;
		sc->dc_pmode = DC_PMODE_MII;
	}

	/*
	 * Setup General Purpose port mode and data so the tulip can talk
	 * to the MII.  This needs to be done before mii_attach so that
	 * we can actually see them.
	 */
	if (DC_IS_XIRCOM(sc)) {
		CSR_WRITE_4(sc, DC_SIAGP, DC_SIAGP_WRITE_EN | DC_SIAGP_INT1_EN |
		    DC_SIAGP_MD_GP2_OUTPUT | DC_SIAGP_MD_GP0_OUTPUT);
		DELAY(10);
		CSR_WRITE_4(sc, DC_SIAGP, DC_SIAGP_INT1_EN |
		    DC_SIAGP_MD_GP2_OUTPUT | DC_SIAGP_MD_GP0_OUTPUT);
		DELAY(10);
	}

	phy = MII_PHY_ANY;
	/*
	 * Note: both the AL981 and AN983 have internal PHYs, however the
	 * AL981 provides direct access to the PHY registers while the AN983
	 * uses a serial MII interface. The AN983's MII interface is also
	 * buggy in that you can read from any MII address (0 to 31), but
	 * only address 1 behaves normally. To deal with both cases, we
	 * pretend that the PHY is at MII address 1.
	 */
	if (DC_IS_ADMTEK(sc))
		phy = DC_ADMTEK_PHYADDR;

	/*
	 * Note: the ukphy probes of the RS7112 report a PHY at MII address
	 * 0 (possibly HomePNA?) and 1 (ethernet) so we only respond to the
	 * correct one.
	 */
	if (DC_IS_CONEXANT(sc))
		phy = DC_CONEXANT_PHYADDR;

	error = mii_attach(dev, &sc->dc_miibus, ifp, dc_ifmedia_upd,
	    dc_ifmedia_sts, BMSR_DEFCAPMASK, phy, MII_OFFSET_ANY, 0);

	if (error && DC_IS_INTEL(sc)) {
		sc->dc_pmode = tmp;
		if (sc->dc_pmode != DC_PMODE_SIA)
			sc->dc_pmode = DC_PMODE_SYM;
		sc->dc_flags |= DC_21143_NWAY;
		/*
		 * For non-MII cards, we need to have the 21143
		 * drive the LEDs. Except there are some systems
		 * like the NEC VersaPro NoteBook PC which have no
		 * LEDs, and twiddling these bits has adverse effects
		 * on them. (I.e. you suddenly can't get a link.)
		 */
		if (!(pci_get_subvendor(dev) == 0x1033 &&
		    pci_get_subdevice(dev) == 0x8028))
			sc->dc_flags |= DC_TULIP_LEDS;
		error = mii_attach(dev, &sc->dc_miibus, ifp, dc_ifmedia_upd,
		    dc_ifmedia_sts, BMSR_DEFCAPMASK, MII_PHY_ANY,
		    MII_OFFSET_ANY, 0);
	}

	if (error) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}

	if (DC_IS_ADMTEK(sc)) {
		/*
		 * Set automatic TX underrun recovery for the ADMtek chips
		 */
		DC_SETBIT(sc, DC_AL_CR, DC_AL_CR_ATUR);
	}

	/*
	 * Tell the upper layer(s) we support long frames.
	 */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);
	ifp->if_capabilities |= IFCAP_VLAN_MTU;
	ifp->if_capenable = ifp->if_capabilities;
#ifdef DEVICE_POLLING
	ifp->if_capabilities |= IFCAP_POLLING;
#endif

	callout_init_mtx(&sc->dc_stat_ch, &sc->dc_mtx, 0);
	callout_init_mtx(&sc->dc_wdog_ch, &sc->dc_mtx, 0);

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, (caddr_t)eaddr);

	/* Hook interrupt last to avoid having to lock softc */
	error = bus_setup_intr(dev, sc->dc_irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, dc_intr, sc, &sc->dc_intrhand);

	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
		goto fail;
	}

fail:
	if (error)
		dc_detach(dev);
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
dc_detach(device_t dev)
{
	struct dc_softc *sc;
	struct ifnet *ifp;
	struct dc_mediainfo *m;

	sc = device_get_softc(dev);
	KASSERT(mtx_initialized(&sc->dc_mtx), ("dc mutex not initialized"));

	ifp = sc->dc_ifp;

#ifdef DEVICE_POLLING
	if (ifp != NULL && ifp->if_capenable & IFCAP_POLLING)
		ether_poll_deregister(ifp);
#endif

	/* These should only be active if attach succeeded */
	if (device_is_attached(dev)) {
		DC_LOCK(sc);
		dc_stop(sc);
		DC_UNLOCK(sc);
		callout_drain(&sc->dc_stat_ch);
		callout_drain(&sc->dc_wdog_ch);
		ether_ifdetach(ifp);
	}
	if (sc->dc_miibus)
		device_delete_child(dev, sc->dc_miibus);
	bus_generic_detach(dev);

	if (sc->dc_intrhand)
		bus_teardown_intr(dev, sc->dc_irq, sc->dc_intrhand);
	if (sc->dc_irq)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->dc_irq);
	if (sc->dc_res)
		bus_release_resource(dev, DC_RES, DC_RID, sc->dc_res);

	if (ifp != NULL)
		if_free(ifp);

	dc_dma_free(sc);

	free(sc->dc_pnic_rx_buf, M_DEVBUF);

	while (sc->dc_mi != NULL) {
		m = sc->dc_mi->dc_next;
		free(sc->dc_mi, M_DEVBUF);
		sc->dc_mi = m;
	}
	free(sc->dc_srom, M_DEVBUF);

	mtx_destroy(&sc->dc_mtx);

	return (0);
}

/*
 * Initialize the transmit descriptors.
 */
static int
dc_list_tx_init(struct dc_softc *sc)
{
	struct dc_chain_data *cd;
	struct dc_list_data *ld;
	int i, nexti;

	cd = &sc->dc_cdata;
	ld = &sc->dc_ldata;
	for (i = 0; i < DC_TX_LIST_CNT; i++) {
		if (i == DC_TX_LIST_CNT - 1)
			nexti = 0;
		else
			nexti = i + 1;
		ld->dc_tx_list[i].dc_status = 0;
		ld->dc_tx_list[i].dc_ctl = 0;
		ld->dc_tx_list[i].dc_data = 0;
		ld->dc_tx_list[i].dc_next = htole32(DC_TXDESC(sc, nexti));
		cd->dc_tx_chain[i] = NULL;
	}

	cd->dc_tx_prod = cd->dc_tx_cons = cd->dc_tx_cnt = 0;
	cd->dc_tx_pkts = 0;
	bus_dmamap_sync(sc->dc_tx_ltag, sc->dc_tx_lmap,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	return (0);
}

/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
static int
dc_list_rx_init(struct dc_softc *sc)
{
	struct dc_chain_data *cd;
	struct dc_list_data *ld;
	int i, nexti;

	cd = &sc->dc_cdata;
	ld = &sc->dc_ldata;

	for (i = 0; i < DC_RX_LIST_CNT; i++) {
		if (dc_newbuf(sc, i) != 0)
			return (ENOBUFS);
		if (i == DC_RX_LIST_CNT - 1)
			nexti = 0;
		else
			nexti = i + 1;
		ld->dc_rx_list[i].dc_next = htole32(DC_RXDESC(sc, nexti));
	}

	cd->dc_rx_prod = 0;
	bus_dmamap_sync(sc->dc_rx_ltag, sc->dc_rx_lmap,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	return (0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
static int
dc_newbuf(struct dc_softc *sc, int i)
{
	struct mbuf *m;
	bus_dmamap_t map;
	bus_dma_segment_t segs[1];
	int error, nseg;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, sizeof(u_int64_t));

	/*
	 * If this is a PNIC chip, zero the buffer. This is part
	 * of the workaround for the receive bug in the 82c168 and
	 * 82c169 chips.
	 */
	if (sc->dc_flags & DC_PNIC_RX_BUG_WAR)
		bzero(mtod(m, char *), m->m_len);

	error = bus_dmamap_load_mbuf_sg(sc->dc_rx_mtag, sc->dc_sparemap,
	    m, segs, &nseg, 0);
	if (error) {
		m_freem(m);
		return (error);
	}
	KASSERT(nseg == 1, ("%s: wrong number of segments (%d)", __func__,
	    nseg));
	if (sc->dc_cdata.dc_rx_chain[i] != NULL)
		bus_dmamap_unload(sc->dc_rx_mtag, sc->dc_cdata.dc_rx_map[i]);

	map = sc->dc_cdata.dc_rx_map[i];
	sc->dc_cdata.dc_rx_map[i] = sc->dc_sparemap;
	sc->dc_sparemap = map;
	sc->dc_cdata.dc_rx_chain[i] = m;
	bus_dmamap_sync(sc->dc_rx_mtag, sc->dc_cdata.dc_rx_map[i],
	    BUS_DMASYNC_PREREAD);

	sc->dc_ldata.dc_rx_list[i].dc_ctl = htole32(DC_RXCTL_RLINK | DC_RXLEN);
	sc->dc_ldata.dc_rx_list[i].dc_data =
	    htole32(DC_ADDR_LO(segs[0].ds_addr));
	sc->dc_ldata.dc_rx_list[i].dc_status = htole32(DC_RXSTAT_OWN);
	bus_dmamap_sync(sc->dc_rx_ltag, sc->dc_rx_lmap,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	return (0);
}

/*
 * Grrrrr.
 * The PNIC chip has a terrible bug in it that manifests itself during
 * periods of heavy activity. The exact mode of failure if difficult to
 * pinpoint: sometimes it only happens in promiscuous mode, sometimes it
 * will happen on slow machines. The bug is that sometimes instead of
 * uploading one complete frame during reception, it uploads what looks
 * like the entire contents of its FIFO memory. The frame we want is at
 * the end of the whole mess, but we never know exactly how much data has
 * been uploaded, so salvaging the frame is hard.
 *
 * There is only one way to do it reliably, and it's disgusting.
 * Here's what we know:
 *
 * - We know there will always be somewhere between one and three extra
 *   descriptors uploaded.
 *
 * - We know the desired received frame will always be at the end of the
 *   total data upload.
 *
 * - We know the size of the desired received frame because it will be
 *   provided in the length field of the status word in the last descriptor.
 *
 * Here's what we do:
 *
 * - When we allocate buffers for the receive ring, we bzero() them.
 *   This means that we know that the buffer contents should be all
 *   zeros, except for data uploaded by the chip.
 *
 * - We also force the PNIC chip to upload frames that include the
 *   ethernet CRC at the end.
 *
 * - We gather all of the bogus frame data into a single buffer.
 *
 * - We then position a pointer at the end of this buffer and scan
 *   backwards until we encounter the first non-zero byte of data.
 *   This is the end of the received frame. We know we will encounter
 *   some data at the end of the frame because the CRC will always be
 *   there, so even if the sender transmits a packet of all zeros,
 *   we won't be fooled.
 *
 * - We know the size of the actual received frame, so we subtract
 *   that value from the current pointer location. This brings us
 *   to the start of the actual received packet.
 *
 * - We copy this into an mbuf and pass it on, along with the actual
 *   frame length.
 *
 * The performance hit is tremendous, but it beats dropping frames all
 * the time.
 */

#define	DC_WHOLEFRAME	(DC_RXSTAT_FIRSTFRAG | DC_RXSTAT_LASTFRAG)
static void
dc_pnic_rx_bug_war(struct dc_softc *sc, int idx)
{
	struct dc_desc *cur_rx;
	struct dc_desc *c = NULL;
	struct mbuf *m = NULL;
	unsigned char *ptr;
	int i, total_len;
	uint32_t rxstat = 0;

	i = sc->dc_pnic_rx_bug_save;
	cur_rx = &sc->dc_ldata.dc_rx_list[idx];
	ptr = sc->dc_pnic_rx_buf;
	bzero(ptr, DC_RXLEN * 5);

	/* Copy all the bytes from the bogus buffers. */
	while (1) {
		c = &sc->dc_ldata.dc_rx_list[i];
		rxstat = le32toh(c->dc_status);
		m = sc->dc_cdata.dc_rx_chain[i];
		bcopy(mtod(m, char *), ptr, DC_RXLEN);
		ptr += DC_RXLEN;
		/* If this is the last buffer, break out. */
		if (i == idx || rxstat & DC_RXSTAT_LASTFRAG)
			break;
		dc_discard_rxbuf(sc, i);
		DC_INC(i, DC_RX_LIST_CNT);
	}

	/* Find the length of the actual receive frame. */
	total_len = DC_RXBYTES(rxstat);

	/* Scan backwards until we hit a non-zero byte. */
	while (*ptr == 0x00)
		ptr--;

	/* Round off. */
	if ((uintptr_t)(ptr) & 0x3)
		ptr -= 1;

	/* Now find the start of the frame. */
	ptr -= total_len;
	if (ptr < sc->dc_pnic_rx_buf)
		ptr = sc->dc_pnic_rx_buf;

	/*
	 * Now copy the salvaged frame to the last mbuf and fake up
	 * the status word to make it look like a successful
	 * frame reception.
	 */
	bcopy(ptr, mtod(m, char *), total_len);
	cur_rx->dc_status = htole32(rxstat | DC_RXSTAT_FIRSTFRAG);
}

/*
 * This routine searches the RX ring for dirty descriptors in the
 * event that the rxeof routine falls out of sync with the chip's
 * current descriptor pointer. This may happen sometimes as a result
 * of a "no RX buffer available" condition that happens when the chip
 * consumes all of the RX buffers before the driver has a chance to
 * process the RX ring. This routine may need to be called more than
 * once to bring the driver back in sync with the chip, however we
 * should still be getting RX DONE interrupts to drive the search
 * for new packets in the RX ring, so we should catch up eventually.
 */
static int
dc_rx_resync(struct dc_softc *sc)
{
	struct dc_desc *cur_rx;
	int i, pos;

	pos = sc->dc_cdata.dc_rx_prod;

	for (i = 0; i < DC_RX_LIST_CNT; i++) {
		cur_rx = &sc->dc_ldata.dc_rx_list[pos];
		if (!(le32toh(cur_rx->dc_status) & DC_RXSTAT_OWN))
			break;
		DC_INC(pos, DC_RX_LIST_CNT);
	}

	/* If the ring really is empty, then just return. */
	if (i == DC_RX_LIST_CNT)
		return (0);

	/* We've fallen behing the chip: catch it. */
	sc->dc_cdata.dc_rx_prod = pos;

	return (EAGAIN);
}

static void
dc_discard_rxbuf(struct dc_softc *sc, int i)
{
	struct mbuf *m;

	if (sc->dc_flags & DC_PNIC_RX_BUG_WAR) {
		m = sc->dc_cdata.dc_rx_chain[i];
		bzero(mtod(m, char *), m->m_len);
	}

	sc->dc_ldata.dc_rx_list[i].dc_ctl = htole32(DC_RXCTL_RLINK | DC_RXLEN);
	sc->dc_ldata.dc_rx_list[i].dc_status = htole32(DC_RXSTAT_OWN);
	bus_dmamap_sync(sc->dc_rx_ltag, sc->dc_rx_lmap, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
static int
dc_rxeof(struct dc_softc *sc)
{
	struct mbuf *m;
	struct ifnet *ifp;
	struct dc_desc *cur_rx;
	int i, total_len, rx_npkts;
	uint32_t rxstat;

	DC_LOCK_ASSERT(sc);

	ifp = sc->dc_ifp;
	rx_npkts = 0;

	bus_dmamap_sync(sc->dc_rx_ltag, sc->dc_rx_lmap, BUS_DMASYNC_POSTREAD |
	    BUS_DMASYNC_POSTWRITE);
	for (i = sc->dc_cdata.dc_rx_prod;
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0;
	    DC_INC(i, DC_RX_LIST_CNT)) {
#ifdef DEVICE_POLLING
		if (ifp->if_capenable & IFCAP_POLLING) {
			if (sc->rxcycles <= 0)
				break;
			sc->rxcycles--;
		}
#endif
		cur_rx = &sc->dc_ldata.dc_rx_list[i];
		rxstat = le32toh(cur_rx->dc_status);
		if ((rxstat & DC_RXSTAT_OWN) != 0)
			break;
		m = sc->dc_cdata.dc_rx_chain[i];
		bus_dmamap_sync(sc->dc_rx_mtag, sc->dc_cdata.dc_rx_map[i],
		    BUS_DMASYNC_POSTREAD);
		total_len = DC_RXBYTES(rxstat);
		rx_npkts++;

		if (sc->dc_flags & DC_PNIC_RX_BUG_WAR) {
			if ((rxstat & DC_WHOLEFRAME) != DC_WHOLEFRAME) {
				if (rxstat & DC_RXSTAT_FIRSTFRAG)
					sc->dc_pnic_rx_bug_save = i;
				if ((rxstat & DC_RXSTAT_LASTFRAG) == 0)
					continue;
				dc_pnic_rx_bug_war(sc, i);
				rxstat = le32toh(cur_rx->dc_status);
				total_len = DC_RXBYTES(rxstat);
			}
		}

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
		 * comes up in the ring.  However, don't report long
		 * frames as errors since they could be vlans.
		 */
		if ((rxstat & DC_RXSTAT_RXERR)) {
			if (!(rxstat & DC_RXSTAT_GIANT) ||
			    (rxstat & (DC_RXSTAT_CRCERR | DC_RXSTAT_DRIBBLE |
				       DC_RXSTAT_MIIERE | DC_RXSTAT_COLLSEEN |
				       DC_RXSTAT_RUNT   | DC_RXSTAT_DE))) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				if (rxstat & DC_RXSTAT_COLLSEEN)
					if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
				dc_discard_rxbuf(sc, i);
				if (rxstat & DC_RXSTAT_CRCERR)
					continue;
				else {
					ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
					dc_init_locked(sc);
					return (rx_npkts);
				}
			}
		}

		/* No errors; receive the packet. */
		total_len -= ETHER_CRC_LEN;
#ifdef __NO_STRICT_ALIGNMENT
		/*
		 * On architectures without alignment problems we try to
		 * allocate a new buffer for the receive ring, and pass up
		 * the one where the packet is already, saving the expensive
		 * copy done in m_devget().
		 * If we are on an architecture with alignment problems, or
		 * if the allocation fails, then use m_devget and leave the
		 * existing buffer in the receive ring.
		 */
		if (dc_newbuf(sc, i) != 0) {
			dc_discard_rxbuf(sc, i);
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			continue;
		}
		m->m_pkthdr.rcvif = ifp;
		m->m_pkthdr.len = m->m_len = total_len;
#else
		{
			struct mbuf *m0;

			m0 = m_devget(mtod(m, char *), total_len,
				ETHER_ALIGN, ifp, NULL);
			dc_discard_rxbuf(sc, i);
			if (m0 == NULL) {
				if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
				continue;
			}
			m = m0;
		}
#endif

		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		DC_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		DC_LOCK(sc);
	}

	sc->dc_cdata.dc_rx_prod = i;
	return (rx_npkts);
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
static void
dc_txeof(struct dc_softc *sc)
{
	struct dc_desc *cur_tx;
	struct ifnet *ifp;
	int idx, setup;
	uint32_t ctl, txstat;

	if (sc->dc_cdata.dc_tx_cnt == 0)
		return;

	ifp = sc->dc_ifp;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	bus_dmamap_sync(sc->dc_tx_ltag, sc->dc_tx_lmap, BUS_DMASYNC_POSTREAD |
	    BUS_DMASYNC_POSTWRITE);
	setup = 0;
	for (idx = sc->dc_cdata.dc_tx_cons; idx != sc->dc_cdata.dc_tx_prod;
	    DC_INC(idx, DC_TX_LIST_CNT), sc->dc_cdata.dc_tx_cnt--) {
		cur_tx = &sc->dc_ldata.dc_tx_list[idx];
		txstat = le32toh(cur_tx->dc_status);
		ctl = le32toh(cur_tx->dc_ctl);

		if (txstat & DC_TXSTAT_OWN)
			break;

		if (sc->dc_cdata.dc_tx_chain[idx] == NULL)
			continue;

		if (ctl & DC_TXCTL_SETUP) {
			cur_tx->dc_ctl = htole32(ctl & ~DC_TXCTL_SETUP);
			setup++;
			bus_dmamap_sync(sc->dc_stag, sc->dc_smap,
			    BUS_DMASYNC_POSTWRITE);
			/*
			 * Yes, the PNIC is so brain damaged
			 * that it will sometimes generate a TX
			 * underrun error while DMAing the RX
			 * filter setup frame. If we detect this,
			 * we have to send the setup frame again,
			 * or else the filter won't be programmed
			 * correctly.
			 */
			if (DC_IS_PNIC(sc)) {
				if (txstat & DC_TXSTAT_ERRSUM)
					dc_setfilt(sc);
			}
			sc->dc_cdata.dc_tx_chain[idx] = NULL;
			continue;
		}

		if (DC_IS_XIRCOM(sc) || DC_IS_CONEXANT(sc)) {
			/*
			 * XXX: Why does my Xircom taunt me so?
			 * For some reason it likes setting the CARRLOST flag
			 * even when the carrier is there. wtf?!?
			 * Who knows, but Conexant chips have the
			 * same problem. Maybe they took lessons
			 * from Xircom.
			 */
			if (/*sc->dc_type == DC_TYPE_21143 &&*/
			    sc->dc_pmode == DC_PMODE_MII &&
			    ((txstat & 0xFFFF) & ~(DC_TXSTAT_ERRSUM |
			    DC_TXSTAT_NOCARRIER)))
				txstat &= ~DC_TXSTAT_ERRSUM;
		} else {
			if (/*sc->dc_type == DC_TYPE_21143 &&*/
			    sc->dc_pmode == DC_PMODE_MII &&
			    ((txstat & 0xFFFF) & ~(DC_TXSTAT_ERRSUM |
			    DC_TXSTAT_NOCARRIER | DC_TXSTAT_CARRLOST)))
				txstat &= ~DC_TXSTAT_ERRSUM;
		}

		if (txstat & DC_TXSTAT_ERRSUM) {
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			if (txstat & DC_TXSTAT_EXCESSCOLL)
				if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
			if (txstat & DC_TXSTAT_LATECOLL)
				if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
			if (!(txstat & DC_TXSTAT_UNDERRUN)) {
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				dc_init_locked(sc);
				return;
			}
		} else
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		if_inc_counter(ifp, IFCOUNTER_COLLISIONS, (txstat & DC_TXSTAT_COLLCNT) >> 3);

		bus_dmamap_sync(sc->dc_tx_mtag, sc->dc_cdata.dc_tx_map[idx],
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->dc_tx_mtag, sc->dc_cdata.dc_tx_map[idx]);
		m_freem(sc->dc_cdata.dc_tx_chain[idx]);
		sc->dc_cdata.dc_tx_chain[idx] = NULL;
	}
	sc->dc_cdata.dc_tx_cons = idx;

	if (sc->dc_cdata.dc_tx_cnt <= DC_TX_LIST_CNT - DC_TX_LIST_RSVD) {
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		if (sc->dc_cdata.dc_tx_cnt == 0)
			sc->dc_wdog_timer = 0;
	}
	if (setup > 0)
		bus_dmamap_sync(sc->dc_tx_ltag, sc->dc_tx_lmap,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static void
dc_tick(void *xsc)
{
	struct dc_softc *sc;
	struct mii_data *mii;
	struct ifnet *ifp;
	uint32_t r;

	sc = xsc;
	DC_LOCK_ASSERT(sc);
	ifp = sc->dc_ifp;
	mii = device_get_softc(sc->dc_miibus);

	/*
	 * Reclaim transmitted frames for controllers that do
	 * not generate TX completion interrupt for every frame.
	 */
	if (sc->dc_flags & DC_TX_USE_TX_INTR)
		dc_txeof(sc);

	if (sc->dc_flags & DC_REDUCED_MII_POLL) {
		if (sc->dc_flags & DC_21143_NWAY) {
			r = CSR_READ_4(sc, DC_10BTSTAT);
			if (IFM_SUBTYPE(mii->mii_media_active) ==
			    IFM_100_TX && (r & DC_TSTAT_LS100)) {
				sc->dc_link = 0;
				mii_mediachg(mii);
			}
			if (IFM_SUBTYPE(mii->mii_media_active) ==
			    IFM_10_T && (r & DC_TSTAT_LS10)) {
				sc->dc_link = 0;
				mii_mediachg(mii);
			}
			if (sc->dc_link == 0)
				mii_tick(mii);
		} else {
			/*
			 * For NICs which never report DC_RXSTATE_WAIT, we
			 * have to bite the bullet...
			 */
			if ((DC_HAS_BROKEN_RXSTATE(sc) || (CSR_READ_4(sc,
			    DC_ISR) & DC_ISR_RX_STATE) == DC_RXSTATE_WAIT) &&
			    sc->dc_cdata.dc_tx_cnt == 0)
				mii_tick(mii);
		}
	} else
		mii_tick(mii);

	/*
	 * When the init routine completes, we expect to be able to send
	 * packets right away, and in fact the network code will send a
	 * gratuitous ARP the moment the init routine marks the interface
	 * as running. However, even though the MAC may have been initialized,
	 * there may be a delay of a few seconds before the PHY completes
	 * autonegotiation and the link is brought up. Any transmissions
	 * made during that delay will be lost. Dealing with this is tricky:
	 * we can't just pause in the init routine while waiting for the
	 * PHY to come ready since that would bring the whole system to
	 * a screeching halt for several seconds.
	 *
	 * What we do here is prevent the TX start routine from sending
	 * any packets until a link has been established. After the
	 * interface has been initialized, the tick routine will poll
	 * the state of the PHY until the IFM_ACTIVE flag is set. Until
	 * that time, packets will stay in the send queue, and once the
	 * link comes up, they will be flushed out to the wire.
	 */
	if (sc->dc_link != 0 && !IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		dc_start_locked(ifp);

	if (sc->dc_flags & DC_21143_NWAY && !sc->dc_link)
		callout_reset(&sc->dc_stat_ch, hz/10, dc_tick, sc);
	else
		callout_reset(&sc->dc_stat_ch, hz, dc_tick, sc);
}

/*
 * A transmit underrun has occurred.  Back off the transmit threshold,
 * or switch to store and forward mode if we have to.
 */
static void
dc_tx_underrun(struct dc_softc *sc)
{
	uint32_t netcfg, isr;
	int i, reinit;

	reinit = 0;
	netcfg = CSR_READ_4(sc, DC_NETCFG);
	device_printf(sc->dc_dev, "TX underrun -- ");
	if ((sc->dc_flags & DC_TX_STORENFWD) == 0) {
		if (sc->dc_txthresh + DC_TXTHRESH_INC > DC_TXTHRESH_MAX) {
			printf("using store and forward mode\n");
			netcfg |= DC_NETCFG_STORENFWD;
		} else {
			printf("increasing TX threshold\n");
			sc->dc_txthresh += DC_TXTHRESH_INC;
			netcfg &= ~DC_NETCFG_TX_THRESH;
			netcfg |= sc->dc_txthresh;
		}

		if (DC_IS_INTEL(sc)) {
			/*
			 * The real 21143 requires that the transmitter be idle
			 * in order to change the transmit threshold or store
			 * and forward state.
			 */
			CSR_WRITE_4(sc, DC_NETCFG, netcfg & ~DC_NETCFG_TX_ON);

			for (i = 0; i < DC_TIMEOUT; i++) {
				isr = CSR_READ_4(sc, DC_ISR);
				if (isr & DC_ISR_TX_IDLE)
					break;
				DELAY(10);
			}
			if (i == DC_TIMEOUT) {
				device_printf(sc->dc_dev,
				    "%s: failed to force tx to idle state\n",
				    __func__);
				reinit++;
			}
		}
	} else {
		printf("resetting\n");
		reinit++;
	}

	if (reinit == 0) {
		CSR_WRITE_4(sc, DC_NETCFG, netcfg);
		if (DC_IS_INTEL(sc))
			CSR_WRITE_4(sc, DC_NETCFG, netcfg | DC_NETCFG_TX_ON);
	} else {
		sc->dc_ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		dc_init_locked(sc);
	}
}

#ifdef DEVICE_POLLING
static poll_handler_t dc_poll;

static int
dc_poll(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct dc_softc *sc = ifp->if_softc;
	int rx_npkts = 0;

	DC_LOCK(sc);

	if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
		DC_UNLOCK(sc);
		return (rx_npkts);
	}

	sc->rxcycles = count;
	rx_npkts = dc_rxeof(sc);
	dc_txeof(sc);
	if (!IFQ_IS_EMPTY(&ifp->if_snd) &&
	    !(ifp->if_drv_flags & IFF_DRV_OACTIVE))
		dc_start_locked(ifp);

	if (cmd == POLL_AND_CHECK_STATUS) { /* also check status register */
		uint32_t	status;

		status = CSR_READ_4(sc, DC_ISR);
		status &= (DC_ISR_RX_WATDOGTIMEO | DC_ISR_RX_NOBUF |
			DC_ISR_TX_NOBUF | DC_ISR_TX_IDLE | DC_ISR_TX_UNDERRUN |
			DC_ISR_BUS_ERR);
		if (!status) {
			DC_UNLOCK(sc);
			return (rx_npkts);
		}
		/* ack what we have */
		CSR_WRITE_4(sc, DC_ISR, status);

		if (status & (DC_ISR_RX_WATDOGTIMEO | DC_ISR_RX_NOBUF)) {
			uint32_t r = CSR_READ_4(sc, DC_FRAMESDISCARDED);
			if_inc_counter(ifp, IFCOUNTER_IERRORS, (r & 0xffff) + ((r >> 17) & 0x7ff));

			if (dc_rx_resync(sc))
				dc_rxeof(sc);
		}
		/* restart transmit unit if necessary */
		if (status & DC_ISR_TX_IDLE && sc->dc_cdata.dc_tx_cnt)
			CSR_WRITE_4(sc, DC_TXSTART, 0xFFFFFFFF);

		if (status & DC_ISR_TX_UNDERRUN)
			dc_tx_underrun(sc);

		if (status & DC_ISR_BUS_ERR) {
			if_printf(ifp, "%s: bus error\n", __func__);
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			dc_init_locked(sc);
		}
	}
	DC_UNLOCK(sc);
	return (rx_npkts);
}
#endif /* DEVICE_POLLING */

static void
dc_intr(void *arg)
{
	struct dc_softc *sc;
	struct ifnet *ifp;
	uint32_t r, status;
	int n;

	sc = arg;

	if (sc->suspended)
		return;

	DC_LOCK(sc);
	status = CSR_READ_4(sc, DC_ISR);
	if (status == 0xFFFFFFFF || (status & DC_INTRS) == 0) {
		DC_UNLOCK(sc);
		return;
	}
	ifp = sc->dc_ifp;
#ifdef DEVICE_POLLING
	if (ifp->if_capenable & IFCAP_POLLING) {
		DC_UNLOCK(sc);
		return;
	}
#endif
	/* Disable interrupts. */
	CSR_WRITE_4(sc, DC_IMR, 0x00000000);

	for (n = 16; n > 0; n--) {
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
			break;
		/* Ack interrupts. */
		CSR_WRITE_4(sc, DC_ISR, status);

		if (status & DC_ISR_RX_OK) {
			if (dc_rxeof(sc) == 0) {
				while (dc_rx_resync(sc))
					dc_rxeof(sc);
			}
		}

		if (status & (DC_ISR_TX_OK | DC_ISR_TX_NOBUF))
			dc_txeof(sc);

		if (status & DC_ISR_TX_IDLE) {
			dc_txeof(sc);
			if (sc->dc_cdata.dc_tx_cnt) {
				DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_TX_ON);
				CSR_WRITE_4(sc, DC_TXSTART, 0xFFFFFFFF);
			}
		}

		if (status & DC_ISR_TX_UNDERRUN)
			dc_tx_underrun(sc);

		if ((status & DC_ISR_RX_WATDOGTIMEO)
		    || (status & DC_ISR_RX_NOBUF)) {
			r = CSR_READ_4(sc, DC_FRAMESDISCARDED);
			if_inc_counter(ifp, IFCOUNTER_IERRORS, (r & 0xffff) + ((r >> 17) & 0x7ff));
			if (dc_rxeof(sc) == 0) {
				while (dc_rx_resync(sc))
					dc_rxeof(sc);
			}
		}

		if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			dc_start_locked(ifp);

		if (status & DC_ISR_BUS_ERR) {
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			dc_init_locked(sc);
			DC_UNLOCK(sc);
			return;
		}
		status = CSR_READ_4(sc, DC_ISR);
		if (status == 0xFFFFFFFF || (status & DC_INTRS) == 0)
			break;
	}

	/* Re-enable interrupts. */
	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		CSR_WRITE_4(sc, DC_IMR, DC_INTRS);

	DC_UNLOCK(sc);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int
dc_encap(struct dc_softc *sc, struct mbuf **m_head)
{
	bus_dma_segment_t segs[DC_MAXFRAGS];
	bus_dmamap_t map;
	struct dc_desc *f;
	struct mbuf *m;
	int cur, defragged, error, first, frag, i, idx, nseg;

	m = NULL;
	defragged = 0;
	if (sc->dc_flags & DC_TX_COALESCE &&
	    ((*m_head)->m_next != NULL || sc->dc_flags & DC_TX_ALIGN)) {
		m = m_defrag(*m_head, M_NOWAIT);
		defragged = 1;
	} else {
		/*
		 * Count the number of frags in this chain to see if we
		 * need to m_collapse.  Since the descriptor list is shared
		 * by all packets, we'll m_collapse long chains so that they
		 * do not use up the entire list, even if they would fit.
		 */
		i = 0;
		for (m = *m_head; m != NULL; m = m->m_next)
			i++;
		if (i > DC_TX_LIST_CNT / 4 ||
		    DC_TX_LIST_CNT - i + sc->dc_cdata.dc_tx_cnt <=
		    DC_TX_LIST_RSVD) {
			m = m_collapse(*m_head, M_NOWAIT, DC_MAXFRAGS);
			defragged = 1;
		}
	}
	if (defragged != 0) {
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
		*m_head = m;
	}

	idx = sc->dc_cdata.dc_tx_prod;
	error = bus_dmamap_load_mbuf_sg(sc->dc_tx_mtag,
	    sc->dc_cdata.dc_tx_map[idx], *m_head, segs, &nseg, 0);
	if (error == EFBIG) {
		if (defragged != 0 || (m = m_collapse(*m_head, M_NOWAIT,
		    DC_MAXFRAGS)) == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (defragged != 0 ? error : ENOBUFS);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->dc_tx_mtag,
		    sc->dc_cdata.dc_tx_map[idx], *m_head, segs, &nseg, 0);
		if (error != 0) {
			m_freem(*m_head);
			*m_head = NULL;
			return (error);
		}
	} else if (error != 0)
		return (error);
	KASSERT(nseg <= DC_MAXFRAGS,
	    ("%s: wrong number of segments (%d)", __func__, nseg));
	if (nseg == 0) {
		m_freem(*m_head);
		*m_head = NULL;
		return (EIO);
	}

	/* Check descriptor overruns. */
	if (sc->dc_cdata.dc_tx_cnt + nseg > DC_TX_LIST_CNT - DC_TX_LIST_RSVD) {
		bus_dmamap_unload(sc->dc_tx_mtag, sc->dc_cdata.dc_tx_map[idx]);
		return (ENOBUFS);
	}
	bus_dmamap_sync(sc->dc_tx_mtag, sc->dc_cdata.dc_tx_map[idx],
	    BUS_DMASYNC_PREWRITE);

	first = cur = frag = sc->dc_cdata.dc_tx_prod;
	for (i = 0; i < nseg; i++) {
		if ((sc->dc_flags & DC_TX_ADMTEK_WAR) &&
		    (frag == (DC_TX_LIST_CNT - 1)) &&
		    (first != sc->dc_cdata.dc_tx_first)) {
			bus_dmamap_unload(sc->dc_tx_mtag,
			    sc->dc_cdata.dc_tx_map[first]);
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}

		f = &sc->dc_ldata.dc_tx_list[frag];
		f->dc_ctl = htole32(DC_TXCTL_TLINK | segs[i].ds_len);
		if (i == 0) {
			f->dc_status = 0;
			f->dc_ctl |= htole32(DC_TXCTL_FIRSTFRAG);
		} else
			f->dc_status = htole32(DC_TXSTAT_OWN);
		f->dc_data = htole32(DC_ADDR_LO(segs[i].ds_addr));
		cur = frag;
		DC_INC(frag, DC_TX_LIST_CNT);
	}

	sc->dc_cdata.dc_tx_prod = frag;
	sc->dc_cdata.dc_tx_cnt += nseg;
	sc->dc_cdata.dc_tx_chain[cur] = *m_head;
	sc->dc_ldata.dc_tx_list[cur].dc_ctl |= htole32(DC_TXCTL_LASTFRAG);
	if (sc->dc_flags & DC_TX_INTR_FIRSTFRAG)
		sc->dc_ldata.dc_tx_list[first].dc_ctl |=
		    htole32(DC_TXCTL_FINT);
	if (sc->dc_flags & DC_TX_INTR_ALWAYS)
		sc->dc_ldata.dc_tx_list[cur].dc_ctl |= htole32(DC_TXCTL_FINT);
	if (sc->dc_flags & DC_TX_USE_TX_INTR &&
	    ++sc->dc_cdata.dc_tx_pkts >= 8) {
		sc->dc_cdata.dc_tx_pkts = 0;
		sc->dc_ldata.dc_tx_list[cur].dc_ctl |= htole32(DC_TXCTL_FINT);
	}
	sc->dc_ldata.dc_tx_list[first].dc_status = htole32(DC_TXSTAT_OWN);

	bus_dmamap_sync(sc->dc_tx_ltag, sc->dc_tx_lmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	/*
	 * Swap the last and the first dmamaps to ensure the map for
	 * this transmission is placed at the last descriptor.
	 */
	map = sc->dc_cdata.dc_tx_map[cur];
	sc->dc_cdata.dc_tx_map[cur] = sc->dc_cdata.dc_tx_map[first];
	sc->dc_cdata.dc_tx_map[first] = map;

	return (0);
}

static void
dc_start(struct ifnet *ifp)
{
	struct dc_softc *sc;

	sc = ifp->if_softc;
	DC_LOCK(sc);
	dc_start_locked(ifp);
	DC_UNLOCK(sc);
}

/*
 * Main transmit routine
 * To avoid having to do mbuf copies, we put pointers to the mbuf data
 * regions directly in the transmit lists.  We also save a copy of the
 * pointers since the transmit list fragment pointers are physical
 * addresses.
 */
static void
dc_start_locked(struct ifnet *ifp)
{
	struct dc_softc *sc;
	struct mbuf *m_head;
	int queued;

	sc = ifp->if_softc;

	DC_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || sc->dc_link == 0)
		return;

	sc->dc_cdata.dc_tx_first = sc->dc_cdata.dc_tx_prod;

	for (queued = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd); ) {
		/*
		 * If there's no way we can send any packets, return now.
		 */
		if (sc->dc_cdata.dc_tx_cnt > DC_TX_LIST_CNT - DC_TX_LIST_RSVD) {
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		if (dc_encap(sc, &m_head)) {
			if (m_head == NULL)
				break;
			IFQ_DRV_PREPEND(&ifp->if_snd, m_head);
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		queued++;
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		BPF_MTAP(ifp, m_head);
	}

	if (queued > 0) {
		/* Transmit */
		if (!(sc->dc_flags & DC_TX_POLL))
			CSR_WRITE_4(sc, DC_TXSTART, 0xFFFFFFFF);

		/*
		 * Set a timeout in case the chip goes out to lunch.
		 */
		sc->dc_wdog_timer = 5;
	}
}

static void
dc_init(void *xsc)
{
	struct dc_softc *sc = xsc;

	DC_LOCK(sc);
	dc_init_locked(sc);
	DC_UNLOCK(sc);
}

static void
dc_init_locked(struct dc_softc *sc)
{
	struct ifnet *ifp = sc->dc_ifp;
	struct mii_data *mii;
	struct ifmedia *ifm;

	DC_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	mii = device_get_softc(sc->dc_miibus);

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	dc_stop(sc);
	dc_reset(sc);
	if (DC_IS_INTEL(sc)) {
		ifm = &mii->mii_media;
		dc_apply_fixup(sc, ifm->ifm_media);
	}

	/*
	 * Set cache alignment and burst length.
	 */
	if (DC_IS_ASIX(sc) || DC_IS_DAVICOM(sc) || DC_IS_ULI(sc))
		CSR_WRITE_4(sc, DC_BUSCTL, 0);
	else
		CSR_WRITE_4(sc, DC_BUSCTL, DC_BUSCTL_MRME | DC_BUSCTL_MRLE);
	/*
	 * Evenly share the bus between receive and transmit process.
	 */
	if (DC_IS_INTEL(sc))
		DC_SETBIT(sc, DC_BUSCTL, DC_BUSCTL_ARBITRATION);
	if (DC_IS_DAVICOM(sc) || DC_IS_INTEL(sc)) {
		DC_SETBIT(sc, DC_BUSCTL, DC_BURSTLEN_USECA);
	} else {
		DC_SETBIT(sc, DC_BUSCTL, DC_BURSTLEN_16LONG);
	}
	if (sc->dc_flags & DC_TX_POLL)
		DC_SETBIT(sc, DC_BUSCTL, DC_TXPOLL_1);
	switch(sc->dc_cachesize) {
	case 32:
		DC_SETBIT(sc, DC_BUSCTL, DC_CACHEALIGN_32LONG);
		break;
	case 16:
		DC_SETBIT(sc, DC_BUSCTL, DC_CACHEALIGN_16LONG);
		break;
	case 8:
		DC_SETBIT(sc, DC_BUSCTL, DC_CACHEALIGN_8LONG);
		break;
	case 0:
	default:
		DC_SETBIT(sc, DC_BUSCTL, DC_CACHEALIGN_NONE);
		break;
	}

	if (sc->dc_flags & DC_TX_STORENFWD)
		DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_STORENFWD);
	else {
		if (sc->dc_txthresh > DC_TXTHRESH_MAX) {
			DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_STORENFWD);
		} else {
			DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_STORENFWD);
			DC_SETBIT(sc, DC_NETCFG, sc->dc_txthresh);
		}
	}

	DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_NO_RXCRC);
	DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_TX_BACKOFF);

	if (DC_IS_MACRONIX(sc) || DC_IS_PNICII(sc)) {
		/*
		 * The app notes for the 98713 and 98715A say that
		 * in order to have the chips operate properly, a magic
		 * number must be written to CSR16. Macronix does not
		 * document the meaning of these bits so there's no way
		 * to know exactly what they do. The 98713 has a magic
		 * number all its own; the rest all use a different one.
		 */
		DC_CLRBIT(sc, DC_MX_MAGICPACKET, 0xFFFF0000);
		if (sc->dc_type == DC_TYPE_98713)
			DC_SETBIT(sc, DC_MX_MAGICPACKET, DC_MX_MAGIC_98713);
		else
			DC_SETBIT(sc, DC_MX_MAGICPACKET, DC_MX_MAGIC_98715);
	}

	if (DC_IS_XIRCOM(sc)) {
		/*
		 * setup General Purpose Port mode and data so the tulip
		 * can talk to the MII.
		 */
		CSR_WRITE_4(sc, DC_SIAGP, DC_SIAGP_WRITE_EN | DC_SIAGP_INT1_EN |
			   DC_SIAGP_MD_GP2_OUTPUT | DC_SIAGP_MD_GP0_OUTPUT);
		DELAY(10);
		CSR_WRITE_4(sc, DC_SIAGP, DC_SIAGP_INT1_EN |
			   DC_SIAGP_MD_GP2_OUTPUT | DC_SIAGP_MD_GP0_OUTPUT);
		DELAY(10);
	}

	DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_TX_THRESH);
	DC_SETBIT(sc, DC_NETCFG, DC_TXTHRESH_MIN);

	/* Init circular RX list. */
	if (dc_list_rx_init(sc) == ENOBUFS) {
		device_printf(sc->dc_dev,
		    "initialization failed: no memory for rx buffers\n");
		dc_stop(sc);
		return;
	}

	/*
	 * Init TX descriptors.
	 */
	dc_list_tx_init(sc);

	/*
	 * Load the address of the RX list.
	 */
	CSR_WRITE_4(sc, DC_RXADDR, DC_RXDESC(sc, 0));
	CSR_WRITE_4(sc, DC_TXADDR, DC_TXDESC(sc, 0));

	/*
	 * Enable interrupts.
	 */
#ifdef DEVICE_POLLING
	/*
	 * ... but only if we are not polling, and make sure they are off in
	 * the case of polling. Some cards (e.g. fxp) turn interrupts on
	 * after a reset.
	 */
	if (ifp->if_capenable & IFCAP_POLLING)
		CSR_WRITE_4(sc, DC_IMR, 0x00000000);
	else
#endif
	CSR_WRITE_4(sc, DC_IMR, DC_INTRS);
	CSR_WRITE_4(sc, DC_ISR, 0xFFFFFFFF);

	/* Initialize TX jabber and RX watchdog timer. */
	if (DC_IS_ULI(sc))
		CSR_WRITE_4(sc, DC_WATCHDOG, DC_WDOG_JABBERCLK |
		    DC_WDOG_HOSTUNJAB);

	/* Enable transmitter. */
	DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_TX_ON);

	/*
	 * If this is an Intel 21143 and we're not using the
	 * MII port, program the LED control pins so we get
	 * link and activity indications.
	 */
	if (sc->dc_flags & DC_TULIP_LEDS) {
		CSR_WRITE_4(sc, DC_WATCHDOG,
		    DC_WDOG_CTLWREN | DC_WDOG_LINK | DC_WDOG_ACTIVITY);
		CSR_WRITE_4(sc, DC_WATCHDOG, 0);
	}

	/*
	 * Load the RX/multicast filter. We do this sort of late
	 * because the filter programming scheme on the 21143 and
	 * some clones requires DMAing a setup frame via the TX
	 * engine, and we need the transmitter enabled for that.
	 */
	dc_setfilt(sc);

	/* Enable receiver. */
	DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_RX_ON);
	CSR_WRITE_4(sc, DC_RXSTART, 0xFFFFFFFF);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	dc_ifmedia_upd_locked(sc);

	/* Clear missed frames and overflow counter. */
	CSR_READ_4(sc, DC_FRAMESDISCARDED);

	/* Don't start the ticker if this is a homePNA link. */
	if (IFM_SUBTYPE(mii->mii_media.ifm_media) == IFM_HPNA_1)
		sc->dc_link = 1;
	else {
		if (sc->dc_flags & DC_21143_NWAY)
			callout_reset(&sc->dc_stat_ch, hz/10, dc_tick, sc);
		else
			callout_reset(&sc->dc_stat_ch, hz, dc_tick, sc);
	}

	sc->dc_wdog_timer = 0;
	callout_reset(&sc->dc_wdog_ch, hz, dc_watchdog, sc);
}

/*
 * Set media options.
 */
static int
dc_ifmedia_upd(struct ifnet *ifp)
{
	struct dc_softc *sc;
	int error;

	sc = ifp->if_softc;
	DC_LOCK(sc);
	error = dc_ifmedia_upd_locked(sc);
	DC_UNLOCK(sc);
	return (error);
}

static int
dc_ifmedia_upd_locked(struct dc_softc *sc)
{
	struct mii_data *mii;
	struct ifmedia *ifm;
	int error;

	DC_LOCK_ASSERT(sc);

	sc->dc_link = 0;
	mii = device_get_softc(sc->dc_miibus);
	error = mii_mediachg(mii);
	if (error == 0) {
		ifm = &mii->mii_media;
		if (DC_IS_INTEL(sc))
			dc_setcfg(sc, ifm->ifm_media);
		else if (DC_IS_DAVICOM(sc) &&
		    IFM_SUBTYPE(ifm->ifm_media) == IFM_HPNA_1)
			dc_setcfg(sc, ifm->ifm_media);
	}

	return (error);
}

/*
 * Report current media status.
 */
static void
dc_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct dc_softc *sc;
	struct mii_data *mii;
	struct ifmedia *ifm;

	sc = ifp->if_softc;
	mii = device_get_softc(sc->dc_miibus);
	DC_LOCK(sc);
	mii_pollstat(mii);
	ifm = &mii->mii_media;
	if (DC_IS_DAVICOM(sc)) {
		if (IFM_SUBTYPE(ifm->ifm_media) == IFM_HPNA_1) {
			ifmr->ifm_active = ifm->ifm_media;
			ifmr->ifm_status = 0;
			DC_UNLOCK(sc);
			return;
		}
	}
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	DC_UNLOCK(sc);
}

static int
dc_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct dc_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	struct mii_data *mii;
	int error = 0;

	switch (command) {
	case SIOCSIFFLAGS:
		DC_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			int need_setfilt = (ifp->if_flags ^ sc->dc_if_flags) &
				(IFF_PROMISC | IFF_ALLMULTI);

			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				if (need_setfilt)
					dc_setfilt(sc);
			} else {
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				dc_init_locked(sc);
			}
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				dc_stop(sc);
		}
		sc->dc_if_flags = ifp->if_flags;
		DC_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		DC_LOCK(sc);
		if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			dc_setfilt(sc);
		DC_UNLOCK(sc);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc->dc_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	case SIOCSIFCAP:
#ifdef DEVICE_POLLING
		if (ifr->ifr_reqcap & IFCAP_POLLING &&
		    !(ifp->if_capenable & IFCAP_POLLING)) {
			error = ether_poll_register(dc_poll, ifp);
			if (error)
				return(error);
			DC_LOCK(sc);
			/* Disable interrupts */
			CSR_WRITE_4(sc, DC_IMR, 0x00000000);
			ifp->if_capenable |= IFCAP_POLLING;
			DC_UNLOCK(sc);
			return (error);
		}
		if (!(ifr->ifr_reqcap & IFCAP_POLLING) &&
		    ifp->if_capenable & IFCAP_POLLING) {
			error = ether_poll_deregister(ifp);
			/* Enable interrupts. */
			DC_LOCK(sc);
			CSR_WRITE_4(sc, DC_IMR, DC_INTRS);
			ifp->if_capenable &= ~IFCAP_POLLING;
			DC_UNLOCK(sc);
			return (error);
		}
#endif /* DEVICE_POLLING */
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static void
dc_watchdog(void *xsc)
{
	struct dc_softc *sc = xsc;
	struct ifnet *ifp;

	DC_LOCK_ASSERT(sc);

	if (sc->dc_wdog_timer == 0 || --sc->dc_wdog_timer != 0) {
		callout_reset(&sc->dc_wdog_ch, hz, dc_watchdog, sc);
		return;
	}

	ifp = sc->dc_ifp;
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	device_printf(sc->dc_dev, "watchdog timeout\n");

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	dc_init_locked(sc);

	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		dc_start_locked(ifp);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
dc_stop(struct dc_softc *sc)
{
	struct ifnet *ifp;
	struct dc_list_data *ld;
	struct dc_chain_data *cd;
	int i;
	uint32_t ctl, netcfg;

	DC_LOCK_ASSERT(sc);

	ifp = sc->dc_ifp;
	ld = &sc->dc_ldata;
	cd = &sc->dc_cdata;

	callout_stop(&sc->dc_stat_ch);
	callout_stop(&sc->dc_wdog_ch);
	sc->dc_wdog_timer = 0;
	sc->dc_link = 0;

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);

	netcfg = CSR_READ_4(sc, DC_NETCFG);
	if (netcfg & (DC_NETCFG_RX_ON | DC_NETCFG_TX_ON))
		CSR_WRITE_4(sc, DC_NETCFG,
		   netcfg & ~(DC_NETCFG_RX_ON | DC_NETCFG_TX_ON));
	CSR_WRITE_4(sc, DC_IMR, 0x00000000);
	/* Wait the completion of TX/RX SM. */
	if (netcfg & (DC_NETCFG_RX_ON | DC_NETCFG_TX_ON))
		dc_netcfg_wait(sc);

	CSR_WRITE_4(sc, DC_TXADDR, 0x00000000);
	CSR_WRITE_4(sc, DC_RXADDR, 0x00000000);

	/*
	 * Free data in the RX lists.
	 */
	for (i = 0; i < DC_RX_LIST_CNT; i++) {
		if (cd->dc_rx_chain[i] != NULL) {
			bus_dmamap_sync(sc->dc_rx_mtag,
			    cd->dc_rx_map[i], BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->dc_rx_mtag,
			    cd->dc_rx_map[i]);
			m_freem(cd->dc_rx_chain[i]);
			cd->dc_rx_chain[i] = NULL;
		}
	}
	bzero(ld->dc_rx_list, DC_RX_LIST_SZ);
	bus_dmamap_sync(sc->dc_rx_ltag, sc->dc_rx_lmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < DC_TX_LIST_CNT; i++) {
		if (cd->dc_tx_chain[i] != NULL) {
			ctl = le32toh(ld->dc_tx_list[i].dc_ctl);
			if (ctl & DC_TXCTL_SETUP) {
				bus_dmamap_sync(sc->dc_stag, sc->dc_smap,
				    BUS_DMASYNC_POSTWRITE);
			} else {
				bus_dmamap_sync(sc->dc_tx_mtag,
				    cd->dc_tx_map[i], BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(sc->dc_tx_mtag,
				    cd->dc_tx_map[i]);
				m_freem(cd->dc_tx_chain[i]);
			}
			cd->dc_tx_chain[i] = NULL;
		}
	}
	bzero(ld->dc_tx_list, DC_TX_LIST_SZ);
	bus_dmamap_sync(sc->dc_tx_ltag, sc->dc_tx_lmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

/*
 * Device suspend routine.  Stop the interface and save some PCI
 * settings in case the BIOS doesn't restore them properly on
 * resume.
 */
static int
dc_suspend(device_t dev)
{
	struct dc_softc *sc;

	sc = device_get_softc(dev);
	DC_LOCK(sc);
	dc_stop(sc);
	sc->suspended = 1;
	DC_UNLOCK(sc);

	return (0);
}

/*
 * Device resume routine.  Restore some PCI settings in case the BIOS
 * doesn't, re-enable busmastering, and restart the interface if
 * appropriate.
 */
static int
dc_resume(device_t dev)
{
	struct dc_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	ifp = sc->dc_ifp;

	/* reinitialize interface if necessary */
	DC_LOCK(sc);
	if (ifp->if_flags & IFF_UP)
		dc_init_locked(sc);

	sc->suspended = 0;
	DC_UNLOCK(sc);

	return (0);
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static int
dc_shutdown(device_t dev)
{
	struct dc_softc *sc;

	sc = device_get_softc(dev);

	DC_LOCK(sc);
	dc_stop(sc);
	DC_UNLOCK(sc);

	return (0);
}

static int
dc_check_multiport(struct dc_softc *sc)
{
	struct dc_softc *dsc;
	devclass_t dc;
	device_t child;
	uint8_t *eaddr;
	int unit;

	dc = devclass_find("dc");
	for (unit = 0; unit < devclass_get_maxunit(dc); unit++) {
		child = devclass_get_device(dc, unit);
		if (child == NULL)
			continue;
		if (child == sc->dc_dev)
			continue;
		if (device_get_parent(child) != device_get_parent(sc->dc_dev))
			continue;
		if (unit > device_get_unit(sc->dc_dev))
			continue;
		if (device_is_attached(child) == 0)
			continue;
		dsc = device_get_softc(child);
		device_printf(sc->dc_dev,
		    "Using station address of %s as base\n",
		    device_get_nameunit(child));
		bcopy(dsc->dc_eaddr, sc->dc_eaddr, ETHER_ADDR_LEN);
		eaddr = (uint8_t *)sc->dc_eaddr;
		eaddr[5]++;
		/* Prepare SROM to parse again. */
		if (DC_IS_INTEL(sc) && dsc->dc_srom != NULL &&
		    sc->dc_romwidth != 0) {
			free(sc->dc_srom, M_DEVBUF);
			sc->dc_romwidth = dsc->dc_romwidth;
			sc->dc_srom = malloc(DC_ROM_SIZE(sc->dc_romwidth),
			    M_DEVBUF, M_NOWAIT);
			if (sc->dc_srom == NULL) {
				device_printf(sc->dc_dev,
				    "Could not allocate SROM buffer\n");
				return (ENOMEM);
			}
			bcopy(dsc->dc_srom, sc->dc_srom,
			    DC_ROM_SIZE(sc->dc_romwidth));
		}
		return (0);
	}
	return (ENOENT);
}
