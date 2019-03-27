/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008, Pyun YongHyeon <yongari@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/rman.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>
#include <machine/in_cksum.h>

#include <dev/jme/if_jmereg.h>
#include <dev/jme/if_jmevar.h>

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

/* Define the following to disable printing Rx errors. */
#undef	JME_SHOW_ERRORS

#define	JME_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)

MODULE_DEPEND(jme, pci, 1, 1, 1);
MODULE_DEPEND(jme, ether, 1, 1, 1);
MODULE_DEPEND(jme, miibus, 1, 1, 1);

/* Tunables. */
static int msi_disable = 0;
static int msix_disable = 0;
TUNABLE_INT("hw.jme.msi_disable", &msi_disable);
TUNABLE_INT("hw.jme.msix_disable", &msix_disable);

/*
 * Devices supported by this driver.
 */
static struct jme_dev {
	uint16_t	jme_vendorid;
	uint16_t	jme_deviceid;
	const char	*jme_name;
} jme_devs[] = {
	{ VENDORID_JMICRON, DEVICEID_JMC250,
	    "JMicron Inc, JMC25x Gigabit Ethernet" },
	{ VENDORID_JMICRON, DEVICEID_JMC260,
	    "JMicron Inc, JMC26x Fast Ethernet" },
};

static int jme_miibus_readreg(device_t, int, int);
static int jme_miibus_writereg(device_t, int, int, int);
static void jme_miibus_statchg(device_t);
static void jme_mediastatus(struct ifnet *, struct ifmediareq *);
static int jme_mediachange(struct ifnet *);
static int jme_probe(device_t);
static int jme_eeprom_read_byte(struct jme_softc *, uint8_t, uint8_t *);
static int jme_eeprom_macaddr(struct jme_softc *);
static int jme_efuse_macaddr(struct jme_softc *);
static void jme_reg_macaddr(struct jme_softc *);
static void jme_set_macaddr(struct jme_softc *, uint8_t *);
static void jme_map_intr_vector(struct jme_softc *);
static int jme_attach(device_t);
static int jme_detach(device_t);
static void jme_sysctl_node(struct jme_softc *);
static void jme_dmamap_cb(void *, bus_dma_segment_t *, int, int);
static int jme_dma_alloc(struct jme_softc *);
static void jme_dma_free(struct jme_softc *);
static int jme_shutdown(device_t);
static void jme_setlinkspeed(struct jme_softc *);
static void jme_setwol(struct jme_softc *);
static int jme_suspend(device_t);
static int jme_resume(device_t);
static int jme_encap(struct jme_softc *, struct mbuf **);
static void jme_start(struct ifnet *);
static void jme_start_locked(struct ifnet *);
static void jme_watchdog(struct jme_softc *);
static int jme_ioctl(struct ifnet *, u_long, caddr_t);
static void jme_mac_config(struct jme_softc *);
static void jme_link_task(void *, int);
static int jme_intr(void *);
static void jme_int_task(void *, int);
static void jme_txeof(struct jme_softc *);
static __inline void jme_discard_rxbuf(struct jme_softc *, int);
static void jme_rxeof(struct jme_softc *);
static int jme_rxintr(struct jme_softc *, int);
static void jme_tick(void *);
static void jme_reset(struct jme_softc *);
static void jme_init(void *);
static void jme_init_locked(struct jme_softc *);
static void jme_stop(struct jme_softc *);
static void jme_stop_tx(struct jme_softc *);
static void jme_stop_rx(struct jme_softc *);
static int jme_init_rx_ring(struct jme_softc *);
static void jme_init_tx_ring(struct jme_softc *);
static void jme_init_ssb(struct jme_softc *);
static int jme_newbuf(struct jme_softc *, struct jme_rxdesc *);
static void jme_set_vlan(struct jme_softc *);
static void jme_set_filter(struct jme_softc *);
static void jme_stats_clear(struct jme_softc *);
static void jme_stats_save(struct jme_softc *);
static void jme_stats_update(struct jme_softc *);
static void jme_phy_down(struct jme_softc *);
static void jme_phy_up(struct jme_softc *);
static int sysctl_int_range(SYSCTL_HANDLER_ARGS, int, int);
static int sysctl_hw_jme_tx_coal_to(SYSCTL_HANDLER_ARGS);
static int sysctl_hw_jme_tx_coal_pkt(SYSCTL_HANDLER_ARGS);
static int sysctl_hw_jme_rx_coal_to(SYSCTL_HANDLER_ARGS);
static int sysctl_hw_jme_rx_coal_pkt(SYSCTL_HANDLER_ARGS);
static int sysctl_hw_jme_proc_limit(SYSCTL_HANDLER_ARGS);


static device_method_t jme_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		jme_probe),
	DEVMETHOD(device_attach,	jme_attach),
	DEVMETHOD(device_detach,	jme_detach),
	DEVMETHOD(device_shutdown,	jme_shutdown),
	DEVMETHOD(device_suspend,	jme_suspend),
	DEVMETHOD(device_resume,	jme_resume),

	/* MII interface. */
	DEVMETHOD(miibus_readreg,	jme_miibus_readreg),
	DEVMETHOD(miibus_writereg,	jme_miibus_writereg),
	DEVMETHOD(miibus_statchg,	jme_miibus_statchg),

	{ NULL, NULL }
};

static driver_t jme_driver = {
	"jme",
	jme_methods,
	sizeof(struct jme_softc)
};

static devclass_t jme_devclass;

DRIVER_MODULE(jme, pci, jme_driver, jme_devclass, 0, 0);
DRIVER_MODULE(miibus, jme, miibus_driver, miibus_devclass, 0, 0);

static struct resource_spec jme_res_spec_mem[] = {
	{ SYS_RES_MEMORY,	PCIR_BAR(0),	RF_ACTIVE },
	{ -1,			0,		0 }
};

static struct resource_spec jme_irq_spec_legacy[] = {
	{ SYS_RES_IRQ,		0,		RF_ACTIVE | RF_SHAREABLE },
	{ -1,			0,		0 }
};

static struct resource_spec jme_irq_spec_msi[] = {
	{ SYS_RES_IRQ,		1,		RF_ACTIVE },
	{ -1,			0,		0 }
};

/*
 *	Read a PHY register on the MII of the JMC250.
 */
static int
jme_miibus_readreg(device_t dev, int phy, int reg)
{
	struct jme_softc *sc;
	uint32_t val;
	int i;

	sc = device_get_softc(dev);

	/* For FPGA version, PHY address 0 should be ignored. */
	if ((sc->jme_flags & JME_FLAG_FPGA) != 0 && phy == 0)
		return (0);

	CSR_WRITE_4(sc, JME_SMI, SMI_OP_READ | SMI_OP_EXECUTE |
	    SMI_PHY_ADDR(phy) | SMI_REG_ADDR(reg));
	for (i = JME_PHY_TIMEOUT; i > 0; i--) {
		DELAY(1);
		if (((val = CSR_READ_4(sc, JME_SMI)) & SMI_OP_EXECUTE) == 0)
			break;
	}

	if (i == 0) {
		device_printf(sc->jme_dev, "phy read timeout : %d\n", reg);
		return (0);
	}

	return ((val & SMI_DATA_MASK) >> SMI_DATA_SHIFT);
}

/*
 *	Write a PHY register on the MII of the JMC250.
 */
static int
jme_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct jme_softc *sc;
	int i;

	sc = device_get_softc(dev);

	/* For FPGA version, PHY address 0 should be ignored. */
	if ((sc->jme_flags & JME_FLAG_FPGA) != 0 && phy == 0)
		return (0);

	CSR_WRITE_4(sc, JME_SMI, SMI_OP_WRITE | SMI_OP_EXECUTE |
	    ((val << SMI_DATA_SHIFT) & SMI_DATA_MASK) |
	    SMI_PHY_ADDR(phy) | SMI_REG_ADDR(reg));
	for (i = JME_PHY_TIMEOUT; i > 0; i--) {
		DELAY(1);
		if (((val = CSR_READ_4(sc, JME_SMI)) & SMI_OP_EXECUTE) == 0)
			break;
	}

	if (i == 0)
		device_printf(sc->jme_dev, "phy write timeout : %d\n", reg);

	return (0);
}

/*
 *	Callback from MII layer when media changes.
 */
static void
jme_miibus_statchg(device_t dev)
{
	struct jme_softc *sc;

	sc = device_get_softc(dev);
	taskqueue_enqueue(taskqueue_swi, &sc->jme_link_task);
}

/*
 *	Get the current interface media status.
 */
static void
jme_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct jme_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	JME_LOCK(sc);
	if ((ifp->if_flags & IFF_UP) == 0) {
		JME_UNLOCK(sc);
		return;
	}
	mii = device_get_softc(sc->jme_miibus);

	mii_pollstat(mii);
	ifmr->ifm_status = mii->mii_media_status;
	ifmr->ifm_active = mii->mii_media_active;
	JME_UNLOCK(sc);
}

/*
 *	Set hardware to newly-selected media.
 */
static int
jme_mediachange(struct ifnet *ifp)
{
	struct jme_softc *sc;
	struct mii_data *mii;
	struct mii_softc *miisc;
	int error;

	sc = ifp->if_softc;
	JME_LOCK(sc);
	mii = device_get_softc(sc->jme_miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	error = mii_mediachg(mii);
	JME_UNLOCK(sc);

	return (error);
}

static int
jme_probe(device_t dev)
{
	struct jme_dev *sp;
	int i;
	uint16_t vendor, devid;

	vendor = pci_get_vendor(dev);
	devid = pci_get_device(dev);
	sp = jme_devs;
	for (i = 0; i < nitems(jme_devs); i++, sp++) {
		if (vendor == sp->jme_vendorid &&
		    devid == sp->jme_deviceid) {
			device_set_desc(dev, sp->jme_name);
			return (BUS_PROBE_DEFAULT);
		}
	}

	return (ENXIO);
}

static int
jme_eeprom_read_byte(struct jme_softc *sc, uint8_t addr, uint8_t *val)
{
	uint32_t reg;
	int i;

	*val = 0;
	for (i = JME_TIMEOUT; i > 0; i--) {
		reg = CSR_READ_4(sc, JME_SMBCSR);
		if ((reg & SMBCSR_HW_BUSY_MASK) == SMBCSR_HW_IDLE)
			break;
		DELAY(1);
	}

	if (i == 0) {
		device_printf(sc->jme_dev, "EEPROM idle timeout!\n");
		return (ETIMEDOUT);
	}

	reg = ((uint32_t)addr << SMBINTF_ADDR_SHIFT) & SMBINTF_ADDR_MASK;
	CSR_WRITE_4(sc, JME_SMBINTF, reg | SMBINTF_RD | SMBINTF_CMD_TRIGGER);
	for (i = JME_TIMEOUT; i > 0; i--) {
		DELAY(1);
		reg = CSR_READ_4(sc, JME_SMBINTF);
		if ((reg & SMBINTF_CMD_TRIGGER) == 0)
			break;
	}

	if (i == 0) {
		device_printf(sc->jme_dev, "EEPROM read timeout!\n");
		return (ETIMEDOUT);
	}

	reg = CSR_READ_4(sc, JME_SMBINTF);
	*val = (reg & SMBINTF_RD_DATA_MASK) >> SMBINTF_RD_DATA_SHIFT;

	return (0);
}

static int
jme_eeprom_macaddr(struct jme_softc *sc)
{
	uint8_t eaddr[ETHER_ADDR_LEN];
	uint8_t fup, reg, val;
	uint32_t offset;
	int match;

	offset = 0;
	if (jme_eeprom_read_byte(sc, offset++, &fup) != 0 ||
	    fup != JME_EEPROM_SIG0)
		return (ENOENT);
	if (jme_eeprom_read_byte(sc, offset++, &fup) != 0 ||
	    fup != JME_EEPROM_SIG1)
		return (ENOENT);
	match = 0;
	do {
		if (jme_eeprom_read_byte(sc, offset, &fup) != 0)
			break;
		if (JME_EEPROM_MKDESC(JME_EEPROM_FUNC0, JME_EEPROM_PAGE_BAR1) ==
		    (fup & (JME_EEPROM_FUNC_MASK | JME_EEPROM_PAGE_MASK))) {
			if (jme_eeprom_read_byte(sc, offset + 1, &reg) != 0)
				break;
			if (reg >= JME_PAR0 &&
			    reg < JME_PAR0 + ETHER_ADDR_LEN) {
				if (jme_eeprom_read_byte(sc, offset + 2,
				    &val) != 0)
					break;
				eaddr[reg - JME_PAR0] = val;
				match++;
			}
		}
		/* Check for the end of EEPROM descriptor. */
		if ((fup & JME_EEPROM_DESC_END) == JME_EEPROM_DESC_END)
			break;
		/* Try next eeprom descriptor. */
		offset += JME_EEPROM_DESC_BYTES;
	} while (match != ETHER_ADDR_LEN && offset < JME_EEPROM_END);

	if (match == ETHER_ADDR_LEN) {
		bcopy(eaddr, sc->jme_eaddr, ETHER_ADDR_LEN);
		return (0);
	}

	return (ENOENT);
}

static int
jme_efuse_macaddr(struct jme_softc *sc)
{
	uint32_t reg;
	int i;

	reg = pci_read_config(sc->jme_dev, JME_EFUSE_CTL1, 4);
	if ((reg & (EFUSE_CTL1_AUTOLOAD_ERR | EFUSE_CTL1_AUTOLAOD_DONE)) !=
	    EFUSE_CTL1_AUTOLAOD_DONE)
		return (ENOENT);
	/* Reset eFuse controller. */
	reg = pci_read_config(sc->jme_dev, JME_EFUSE_CTL2, 4);
	reg |= EFUSE_CTL2_RESET;
	pci_write_config(sc->jme_dev, JME_EFUSE_CTL2, reg, 4);
	reg = pci_read_config(sc->jme_dev, JME_EFUSE_CTL2, 4);
	reg &= ~EFUSE_CTL2_RESET;
	pci_write_config(sc->jme_dev, JME_EFUSE_CTL2, reg, 4);

	/* Have eFuse reload station address to MAC controller. */
	reg = pci_read_config(sc->jme_dev, JME_EFUSE_CTL1, 4);
	reg &= ~EFUSE_CTL1_CMD_MASK;
	reg |= EFUSE_CTL1_CMD_AUTOLOAD | EFUSE_CTL1_EXECUTE;
	pci_write_config(sc->jme_dev, JME_EFUSE_CTL1, reg, 4);

	/*
	 * Verify completion of eFuse autload command.  It should be
	 * completed within 108us.
	 */
	DELAY(110);
	for (i = 10; i > 0; i--) {
		reg = pci_read_config(sc->jme_dev, JME_EFUSE_CTL1, 4);
		if ((reg & (EFUSE_CTL1_AUTOLOAD_ERR |
		    EFUSE_CTL1_AUTOLAOD_DONE)) != EFUSE_CTL1_AUTOLAOD_DONE) {
			DELAY(20);
			continue;
		}
		if ((reg & EFUSE_CTL1_EXECUTE) == 0)
			break;
		/* Station address loading is still in progress. */
		DELAY(20);
	}
	if (i == 0) {
		device_printf(sc->jme_dev, "eFuse autoload timed out.\n");
		return (ETIMEDOUT);
	}

	return (0);
}

static void
jme_reg_macaddr(struct jme_softc *sc)
{
	uint32_t par0, par1;

	/* Read station address. */
	par0 = CSR_READ_4(sc, JME_PAR0);
	par1 = CSR_READ_4(sc, JME_PAR1);
	par1 &= 0xFFFF;
	if ((par0 == 0 && par1 == 0) ||
	    (par0 == 0xFFFFFFFF && par1 == 0xFFFF)) {
		device_printf(sc->jme_dev,
		    "Failed to retrieve Ethernet address.\n");
	} else {
		/*
		 * For controllers that use eFuse, the station address
		 * could also be extracted from JME_PCI_PAR0 and
		 * JME_PCI_PAR1 registers in PCI configuration space.
		 * Each register holds exactly half of station address(24bits)
		 * so use JME_PAR0, JME_PAR1 registers instead.
		 */
		sc->jme_eaddr[0] = (par0 >> 0) & 0xFF;
		sc->jme_eaddr[1] = (par0 >> 8) & 0xFF;
		sc->jme_eaddr[2] = (par0 >> 16) & 0xFF;
		sc->jme_eaddr[3] = (par0 >> 24) & 0xFF;
		sc->jme_eaddr[4] = (par1 >> 0) & 0xFF;
		sc->jme_eaddr[5] = (par1 >> 8) & 0xFF;
	}
}

static void
jme_set_macaddr(struct jme_softc *sc, uint8_t *eaddr)
{
	uint32_t val;
	int i;

	if ((sc->jme_flags & JME_FLAG_EFUSE) != 0) {
		/*
		 * Avoid reprogramming station address if the address
		 * is the same as previous one.  Note, reprogrammed
		 * station address is permanent as if it was written
		 * to EEPROM. So if station address was changed by
		 * admistrator it's possible to lose factory configured
		 * address when driver fails to restore its address.
		 * (e.g. reboot or system crash)
		 */
		if (bcmp(eaddr, sc->jme_eaddr, ETHER_ADDR_LEN) != 0) {
			for (i = 0; i < ETHER_ADDR_LEN; i++) {
				val = JME_EFUSE_EEPROM_FUNC0 <<
				    JME_EFUSE_EEPROM_FUNC_SHIFT;
				val |= JME_EFUSE_EEPROM_PAGE_BAR1 <<
				    JME_EFUSE_EEPROM_PAGE_SHIFT;
				val |= (JME_PAR0 + i) <<
				    JME_EFUSE_EEPROM_ADDR_SHIFT;
				val |= eaddr[i] << JME_EFUSE_EEPROM_DATA_SHIFT;
				pci_write_config(sc->jme_dev, JME_EFUSE_EEPROM,
				    val | JME_EFUSE_EEPROM_WRITE, 4);
			}
		}
	} else {
		CSR_WRITE_4(sc, JME_PAR0,
		    eaddr[3] << 24 | eaddr[2] << 16 | eaddr[1] << 8 | eaddr[0]);
		CSR_WRITE_4(sc, JME_PAR1, eaddr[5] << 8 | eaddr[4]);
	}
}

static void
jme_map_intr_vector(struct jme_softc *sc)
{
	uint32_t map[MSINUM_NUM_INTR_SOURCE / JME_MSI_MESSAGES];

	bzero(map, sizeof(map));

	/* Map Tx interrupts source to MSI/MSIX vector 2. */
	map[MSINUM_REG_INDEX(N_INTR_TXQ0_COMP)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ0_COMP);
	map[MSINUM_REG_INDEX(N_INTR_TXQ1_COMP)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ1_COMP);
	map[MSINUM_REG_INDEX(N_INTR_TXQ2_COMP)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ2_COMP);
	map[MSINUM_REG_INDEX(N_INTR_TXQ3_COMP)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ3_COMP);
	map[MSINUM_REG_INDEX(N_INTR_TXQ4_COMP)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ4_COMP);
	map[MSINUM_REG_INDEX(N_INTR_TXQ4_COMP)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ5_COMP);
	map[MSINUM_REG_INDEX(N_INTR_TXQ6_COMP)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ6_COMP);
	map[MSINUM_REG_INDEX(N_INTR_TXQ7_COMP)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ7_COMP);
	map[MSINUM_REG_INDEX(N_INTR_TXQ_COAL)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ_COAL);
	map[MSINUM_REG_INDEX(N_INTR_TXQ_COAL_TO)] |=
	    MSINUM_INTR_SOURCE(2, N_INTR_TXQ_COAL_TO);

	/* Map Rx interrupts source to MSI/MSIX vector 1. */
	map[MSINUM_REG_INDEX(N_INTR_RXQ0_COMP)] |=
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ0_COMP);
	map[MSINUM_REG_INDEX(N_INTR_RXQ1_COMP)] |=
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ1_COMP);
	map[MSINUM_REG_INDEX(N_INTR_RXQ2_COMP)] |=
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ2_COMP);
	map[MSINUM_REG_INDEX(N_INTR_RXQ3_COMP)] |=
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ3_COMP);
	map[MSINUM_REG_INDEX(N_INTR_RXQ0_DESC_EMPTY)] |=
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ0_DESC_EMPTY);
	map[MSINUM_REG_INDEX(N_INTR_RXQ1_DESC_EMPTY)] |=
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ1_DESC_EMPTY);
	map[MSINUM_REG_INDEX(N_INTR_RXQ2_DESC_EMPTY)] |=
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ2_DESC_EMPTY);
	map[MSINUM_REG_INDEX(N_INTR_RXQ3_DESC_EMPTY)] |=
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ3_DESC_EMPTY);
	map[MSINUM_REG_INDEX(N_INTR_RXQ0_COAL)] |=
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ0_COAL);
	map[MSINUM_REG_INDEX(N_INTR_RXQ1_COAL)] |=
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ1_COAL);
	map[MSINUM_REG_INDEX(N_INTR_RXQ2_COAL)] |=
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ2_COAL);
	map[MSINUM_REG_INDEX(N_INTR_RXQ3_COAL)] |=
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ3_COAL);
	map[MSINUM_REG_INDEX(N_INTR_RXQ0_COAL_TO)] |=
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ0_COAL_TO);
	map[MSINUM_REG_INDEX(N_INTR_RXQ1_COAL_TO)] |=
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ1_COAL_TO);
	map[MSINUM_REG_INDEX(N_INTR_RXQ2_COAL_TO)] |=
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ2_COAL_TO);
	map[MSINUM_REG_INDEX(N_INTR_RXQ3_COAL_TO)] |=
	    MSINUM_INTR_SOURCE(1, N_INTR_RXQ3_COAL_TO);

	/* Map all other interrupts source to MSI/MSIX vector 0. */
	CSR_WRITE_4(sc, JME_MSINUM_BASE + sizeof(uint32_t) * 0, map[0]);
	CSR_WRITE_4(sc, JME_MSINUM_BASE + sizeof(uint32_t) * 1, map[1]);
	CSR_WRITE_4(sc, JME_MSINUM_BASE + sizeof(uint32_t) * 2, map[2]);
	CSR_WRITE_4(sc, JME_MSINUM_BASE + sizeof(uint32_t) * 3, map[3]);
}

static int
jme_attach(device_t dev)
{
	struct jme_softc *sc;
	struct ifnet *ifp;
	struct mii_softc *miisc;
	struct mii_data *mii;
	uint32_t reg;
	uint16_t burst;
	int error, i, mii_flags, msic, msixc, pmc;

	error = 0;
	sc = device_get_softc(dev);
	sc->jme_dev = dev;

	mtx_init(&sc->jme_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->jme_tick_ch, &sc->jme_mtx, 0);
	TASK_INIT(&sc->jme_int_task, 0, jme_int_task, sc);
	TASK_INIT(&sc->jme_link_task, 0, jme_link_task, sc);

	/*
	 * Map the device. JMC250 supports both memory mapped and I/O
	 * register space access. Because I/O register access should
	 * use different BARs to access registers it's waste of time
	 * to use I/O register spce access. JMC250 uses 16K to map
	 * entire memory space.
	 */
	pci_enable_busmaster(dev);
	sc->jme_res_spec = jme_res_spec_mem;
	sc->jme_irq_spec = jme_irq_spec_legacy;
	error = bus_alloc_resources(dev, sc->jme_res_spec, sc->jme_res);
	if (error != 0) {
		device_printf(dev, "cannot allocate memory resources.\n");
		goto fail;
	}

	/* Allocate IRQ resources. */
	msixc = pci_msix_count(dev);
	msic = pci_msi_count(dev);
	if (bootverbose) {
		device_printf(dev, "MSIX count : %d\n", msixc);
		device_printf(dev, "MSI count : %d\n", msic);
	}

	/* Use 1 MSI/MSI-X. */
	if (msixc > 1)
		msixc = 1;
	if (msic > 1)
		msic = 1;
	/* Prefer MSIX over MSI. */
	if (msix_disable == 0 || msi_disable == 0) {
		if (msix_disable == 0 && msixc > 0 &&
		    pci_alloc_msix(dev, &msixc) == 0) {
			if (msixc == 1) {
				device_printf(dev, "Using %d MSIX messages.\n",
				    msixc);
				sc->jme_flags |= JME_FLAG_MSIX;
				sc->jme_irq_spec = jme_irq_spec_msi;
			} else
				pci_release_msi(dev);
		}
		if (msi_disable == 0 && (sc->jme_flags & JME_FLAG_MSIX) == 0 &&
		    msic > 0 && pci_alloc_msi(dev, &msic) == 0) {
			if (msic == 1) {
				device_printf(dev, "Using %d MSI messages.\n",
				    msic);
				sc->jme_flags |= JME_FLAG_MSI;
				sc->jme_irq_spec = jme_irq_spec_msi;
			} else
				pci_release_msi(dev);
		}
		/* Map interrupt vector 0, 1 and 2. */
		if ((sc->jme_flags & JME_FLAG_MSI) != 0 ||
		    (sc->jme_flags & JME_FLAG_MSIX) != 0)
			jme_map_intr_vector(sc);
	}

	error = bus_alloc_resources(dev, sc->jme_irq_spec, sc->jme_irq);
	if (error != 0) {
		device_printf(dev, "cannot allocate IRQ resources.\n");
		goto fail;
	}

	sc->jme_rev = pci_get_device(dev);
	if ((sc->jme_rev & DEVICEID_JMC2XX_MASK) == DEVICEID_JMC260) {
		sc->jme_flags |= JME_FLAG_FASTETH;
		sc->jme_flags |= JME_FLAG_NOJUMBO;
	}
	reg = CSR_READ_4(sc, JME_CHIPMODE);
	sc->jme_chip_rev = (reg & CHIPMODE_REV_MASK) >> CHIPMODE_REV_SHIFT;
	if (((reg & CHIPMODE_FPGA_REV_MASK) >> CHIPMODE_FPGA_REV_SHIFT) !=
	    CHIPMODE_NOT_FPGA)
		sc->jme_flags |= JME_FLAG_FPGA;
	if (bootverbose) {
		device_printf(dev, "PCI device revision : 0x%04x\n",
		    sc->jme_rev);
		device_printf(dev, "Chip revision : 0x%02x\n",
		    sc->jme_chip_rev);
		if ((sc->jme_flags & JME_FLAG_FPGA) != 0)
			device_printf(dev, "FPGA revision : 0x%04x\n",
			    (reg & CHIPMODE_FPGA_REV_MASK) >>
			    CHIPMODE_FPGA_REV_SHIFT);
	}
	if (sc->jme_chip_rev == 0xFF) {
		device_printf(dev, "Unknown chip revision : 0x%02x\n",
		    sc->jme_rev);
		error = ENXIO;
		goto fail;
	}

	/* Identify controller features and bugs. */
	if (CHIPMODE_REVFM(sc->jme_chip_rev) >= 2) {
		if ((sc->jme_rev & DEVICEID_JMC2XX_MASK) == DEVICEID_JMC260 &&
		    CHIPMODE_REVFM(sc->jme_chip_rev) == 2)
			sc->jme_flags |= JME_FLAG_DMA32BIT;
		if (CHIPMODE_REVFM(sc->jme_chip_rev) >= 5)
			sc->jme_flags |= JME_FLAG_EFUSE | JME_FLAG_PCCPCD;
		sc->jme_flags |= JME_FLAG_TXCLK | JME_FLAG_RXCLK;
		sc->jme_flags |= JME_FLAG_HWMIB;
	}

	/* Reset the ethernet controller. */
	jme_reset(sc);

	/* Get station address. */
	if ((sc->jme_flags & JME_FLAG_EFUSE) != 0) {
		error = jme_efuse_macaddr(sc);
		if (error == 0)
			jme_reg_macaddr(sc);
	} else {
		error = ENOENT;
		reg = CSR_READ_4(sc, JME_SMBCSR);
		if ((reg & SMBCSR_EEPROM_PRESENT) != 0)
			error = jme_eeprom_macaddr(sc);
		if (error != 0 && bootverbose)
			device_printf(sc->jme_dev,
			    "ethernet hardware address not found in EEPROM.\n");
		if (error != 0)
			jme_reg_macaddr(sc);
	}

	/*
	 * Save PHY address.
	 * Integrated JR0211 has fixed PHY address whereas FPGA version
	 * requires PHY probing to get correct PHY address.
	 */
	if ((sc->jme_flags & JME_FLAG_FPGA) == 0) {
		sc->jme_phyaddr = CSR_READ_4(sc, JME_GPREG0) &
		    GPREG0_PHY_ADDR_MASK;
		if (bootverbose)
			device_printf(dev, "PHY is at address %d.\n",
			    sc->jme_phyaddr);
	} else
		sc->jme_phyaddr = 0;

	/* Set max allowable DMA size. */
	if (pci_find_cap(dev, PCIY_EXPRESS, &i) == 0) {
		sc->jme_flags |= JME_FLAG_PCIE;
		burst = pci_read_config(dev, i + PCIER_DEVICE_CTL, 2);
		if (bootverbose) {
			device_printf(dev, "Read request size : %d bytes.\n",
			    128 << ((burst >> 12) & 0x07));
			device_printf(dev, "TLP payload size : %d bytes.\n",
			    128 << ((burst >> 5) & 0x07));
		}
		switch ((burst >> 12) & 0x07) {
		case 0:
			sc->jme_tx_dma_size = TXCSR_DMA_SIZE_128;
			break;
		case 1:
			sc->jme_tx_dma_size = TXCSR_DMA_SIZE_256;
			break;
		default:
			sc->jme_tx_dma_size = TXCSR_DMA_SIZE_512;
			break;
		}
		sc->jme_rx_dma_size = RXCSR_DMA_SIZE_128;
	} else {
		sc->jme_tx_dma_size = TXCSR_DMA_SIZE_512;
		sc->jme_rx_dma_size = RXCSR_DMA_SIZE_128;
	}
	/* Create coalescing sysctl node. */
	jme_sysctl_node(sc);
	if ((error = jme_dma_alloc(sc)) != 0)
		goto fail;

	ifp = sc->jme_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "cannot allocate ifnet structure.\n");
		error = ENXIO;
		goto fail;
	}

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = jme_ioctl;
	ifp->if_start = jme_start;
	ifp->if_init = jme_init;
	ifp->if_snd.ifq_drv_maxlen = JME_TX_RING_CNT - 1;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifp->if_snd.ifq_drv_maxlen);
	IFQ_SET_READY(&ifp->if_snd);
	/* JMC250 supports Tx/Rx checksum offload as well as TSO. */
	ifp->if_capabilities = IFCAP_HWCSUM | IFCAP_TSO4;
	ifp->if_hwassist = JME_CSUM_FEATURES | CSUM_TSO;
	if (pci_find_cap(dev, PCIY_PMG, &pmc) == 0) {
		sc->jme_flags |= JME_FLAG_PMCAP;
		ifp->if_capabilities |= IFCAP_WOL_MAGIC;
	}
	ifp->if_capenable = ifp->if_capabilities;

	/* Wakeup PHY. */
	jme_phy_up(sc);
	mii_flags = MIIF_DOPAUSE;
	/* Ask PHY calibration to PHY driver. */
	if (CHIPMODE_REVFM(sc->jme_chip_rev) >= 5)
		mii_flags |= MIIF_MACPRIV0;
	/* Set up MII bus. */
	error = mii_attach(dev, &sc->jme_miibus, ifp, jme_mediachange,
	    jme_mediastatus, BMSR_DEFCAPMASK,
	    sc->jme_flags & JME_FLAG_FPGA ? MII_PHY_ANY : sc->jme_phyaddr,
	    MII_OFFSET_ANY, mii_flags);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}

	/*
	 * Force PHY to FPGA mode.
	 */
	if ((sc->jme_flags & JME_FLAG_FPGA) != 0) {
		mii = device_get_softc(sc->jme_miibus);
		if (mii->mii_instance != 0) {
			LIST_FOREACH(miisc, &mii->mii_phys, mii_list) {
				if (miisc->mii_phy != 0) {
					sc->jme_phyaddr = miisc->mii_phy;
					break;
				}
			}
			if (sc->jme_phyaddr != 0) {
				device_printf(sc->jme_dev,
				    "FPGA PHY is at %d\n", sc->jme_phyaddr);
				/* vendor magic. */
				jme_miibus_writereg(dev, sc->jme_phyaddr, 27,
				    0x0004);
			}
		}
	}

	ether_ifattach(ifp, sc->jme_eaddr);

	/* VLAN capability setup */
	ifp->if_capabilities |= IFCAP_VLAN_MTU | IFCAP_VLAN_HWTAGGING |
	    IFCAP_VLAN_HWCSUM | IFCAP_VLAN_HWTSO;
	ifp->if_capenable = ifp->if_capabilities;

	/* Tell the upper layer(s) we support long frames. */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	/* Create local taskq. */
	sc->jme_tq = taskqueue_create_fast("jme_taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->jme_tq);
	if (sc->jme_tq == NULL) {
		device_printf(dev, "could not create taskqueue.\n");
		ether_ifdetach(ifp);
		error = ENXIO;
		goto fail;
	}
	taskqueue_start_threads(&sc->jme_tq, 1, PI_NET, "%s taskq",
	    device_get_nameunit(sc->jme_dev));

	for (i = 0; i < 1; i++) {
		error = bus_setup_intr(dev, sc->jme_irq[i],
		    INTR_TYPE_NET | INTR_MPSAFE, jme_intr, NULL, sc,
		    &sc->jme_intrhand[i]);
		if (error != 0)
			break;
	}

	if (error != 0) {
		device_printf(dev, "could not set up interrupt handler.\n");
		taskqueue_free(sc->jme_tq);
		sc->jme_tq = NULL;
		ether_ifdetach(ifp);
		goto fail;
	}

fail:
	if (error != 0)
		jme_detach(dev);

	return (error);
}

static int
jme_detach(device_t dev)
{
	struct jme_softc *sc;
	struct ifnet *ifp;
	int i;

	sc = device_get_softc(dev);

	ifp = sc->jme_ifp;
	if (device_is_attached(dev)) {
		JME_LOCK(sc);
		sc->jme_flags |= JME_FLAG_DETACH;
		jme_stop(sc);
		JME_UNLOCK(sc);
		callout_drain(&sc->jme_tick_ch);
		taskqueue_drain(sc->jme_tq, &sc->jme_int_task);
		taskqueue_drain(taskqueue_swi, &sc->jme_link_task);
		/* Restore possibly modified station address. */
		if ((sc->jme_flags & JME_FLAG_EFUSE) != 0)
			jme_set_macaddr(sc, sc->jme_eaddr);
		ether_ifdetach(ifp);
	}

	if (sc->jme_tq != NULL) {
		taskqueue_drain(sc->jme_tq, &sc->jme_int_task);
		taskqueue_free(sc->jme_tq);
		sc->jme_tq = NULL;
	}

	if (sc->jme_miibus != NULL) {
		device_delete_child(dev, sc->jme_miibus);
		sc->jme_miibus = NULL;
	}
	bus_generic_detach(dev);
	jme_dma_free(sc);

	if (ifp != NULL) {
		if_free(ifp);
		sc->jme_ifp = NULL;
	}

	for (i = 0; i < 1; i++) {
		if (sc->jme_intrhand[i] != NULL) {
			bus_teardown_intr(dev, sc->jme_irq[i],
			    sc->jme_intrhand[i]);
			sc->jme_intrhand[i] = NULL;
		}
	}

	if (sc->jme_irq[0] != NULL)
		bus_release_resources(dev, sc->jme_irq_spec, sc->jme_irq);
	if ((sc->jme_flags & (JME_FLAG_MSIX | JME_FLAG_MSI)) != 0)
		pci_release_msi(dev);
	if (sc->jme_res[0] != NULL)
		bus_release_resources(dev, sc->jme_res_spec, sc->jme_res);
	mtx_destroy(&sc->jme_mtx);

	return (0);
}

#define	JME_SYSCTL_STAT_ADD32(c, h, n, p, d)	\
	    SYSCTL_ADD_UINT(c, h, OID_AUTO, n, CTLFLAG_RD, p, 0, d)

static void
jme_sysctl_node(struct jme_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *child, *parent;
	struct sysctl_oid *tree;
	struct jme_hw_stats *stats;
	int error;

	stats = &sc->jme_stats;
	ctx = device_get_sysctl_ctx(sc->jme_dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->jme_dev));

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "tx_coal_to",
	    CTLTYPE_INT | CTLFLAG_RW, &sc->jme_tx_coal_to, 0,
	    sysctl_hw_jme_tx_coal_to, "I", "jme tx coalescing timeout");

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "tx_coal_pkt",
	    CTLTYPE_INT | CTLFLAG_RW, &sc->jme_tx_coal_pkt, 0,
	    sysctl_hw_jme_tx_coal_pkt, "I", "jme tx coalescing packet");

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "rx_coal_to",
	    CTLTYPE_INT | CTLFLAG_RW, &sc->jme_rx_coal_to, 0,
	    sysctl_hw_jme_rx_coal_to, "I", "jme rx coalescing timeout");

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "rx_coal_pkt",
	    CTLTYPE_INT | CTLFLAG_RW, &sc->jme_rx_coal_pkt, 0,
	    sysctl_hw_jme_rx_coal_pkt, "I", "jme rx coalescing packet");

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "process_limit",
	    CTLTYPE_INT | CTLFLAG_RW, &sc->jme_process_limit, 0,
	    sysctl_hw_jme_proc_limit, "I",
	    "max number of Rx events to process");

	/* Pull in device tunables. */
	sc->jme_process_limit = JME_PROC_DEFAULT;
	error = resource_int_value(device_get_name(sc->jme_dev),
	    device_get_unit(sc->jme_dev), "process_limit",
	    &sc->jme_process_limit);
	if (error == 0) {
		if (sc->jme_process_limit < JME_PROC_MIN ||
		    sc->jme_process_limit > JME_PROC_MAX) {
			device_printf(sc->jme_dev,
			    "process_limit value out of range; "
			    "using default: %d\n", JME_PROC_DEFAULT);
			sc->jme_process_limit = JME_PROC_DEFAULT;
		}
	}

	sc->jme_tx_coal_to = PCCTX_COAL_TO_DEFAULT;
	error = resource_int_value(device_get_name(sc->jme_dev),
	    device_get_unit(sc->jme_dev), "tx_coal_to", &sc->jme_tx_coal_to);
	if (error == 0) {
		if (sc->jme_tx_coal_to < PCCTX_COAL_TO_MIN ||
		    sc->jme_tx_coal_to > PCCTX_COAL_TO_MAX) {
			device_printf(sc->jme_dev,
			    "tx_coal_to value out of range; "
			    "using default: %d\n", PCCTX_COAL_TO_DEFAULT);
			sc->jme_tx_coal_to = PCCTX_COAL_TO_DEFAULT;
		}
	}

	sc->jme_tx_coal_pkt = PCCTX_COAL_PKT_DEFAULT;
	error = resource_int_value(device_get_name(sc->jme_dev),
	    device_get_unit(sc->jme_dev), "tx_coal_pkt", &sc->jme_tx_coal_to);
	if (error == 0) {
		if (sc->jme_tx_coal_pkt < PCCTX_COAL_PKT_MIN ||
		    sc->jme_tx_coal_pkt > PCCTX_COAL_PKT_MAX) {
			device_printf(sc->jme_dev,
			    "tx_coal_pkt value out of range; "
			    "using default: %d\n", PCCTX_COAL_PKT_DEFAULT);
			sc->jme_tx_coal_pkt = PCCTX_COAL_PKT_DEFAULT;
		}
	}

	sc->jme_rx_coal_to = PCCRX_COAL_TO_DEFAULT;
	error = resource_int_value(device_get_name(sc->jme_dev),
	    device_get_unit(sc->jme_dev), "rx_coal_to", &sc->jme_rx_coal_to);
	if (error == 0) {
		if (sc->jme_rx_coal_to < PCCRX_COAL_TO_MIN ||
		    sc->jme_rx_coal_to > PCCRX_COAL_TO_MAX) {
			device_printf(sc->jme_dev,
			    "rx_coal_to value out of range; "
			    "using default: %d\n", PCCRX_COAL_TO_DEFAULT);
			sc->jme_rx_coal_to = PCCRX_COAL_TO_DEFAULT;
		}
	}

	sc->jme_rx_coal_pkt = PCCRX_COAL_PKT_DEFAULT;
	error = resource_int_value(device_get_name(sc->jme_dev),
	    device_get_unit(sc->jme_dev), "rx_coal_pkt", &sc->jme_rx_coal_to);
	if (error == 0) {
		if (sc->jme_rx_coal_pkt < PCCRX_COAL_PKT_MIN ||
		    sc->jme_rx_coal_pkt > PCCRX_COAL_PKT_MAX) {
			device_printf(sc->jme_dev,
			    "tx_coal_pkt value out of range; "
			    "using default: %d\n", PCCRX_COAL_PKT_DEFAULT);
			sc->jme_rx_coal_pkt = PCCRX_COAL_PKT_DEFAULT;
		}
	}

	if ((sc->jme_flags & JME_FLAG_HWMIB) == 0)
		return;

	tree = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "stats", CTLFLAG_RD,
	    NULL, "JME statistics");
	parent = SYSCTL_CHILDREN(tree);

	/* Rx statistics. */
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "rx", CTLFLAG_RD,
	    NULL, "Rx MAC statistics");
	child = SYSCTL_CHILDREN(tree);
	JME_SYSCTL_STAT_ADD32(ctx, child, "good_frames",
	    &stats->rx_good_frames, "Good frames");
	JME_SYSCTL_STAT_ADD32(ctx, child, "crc_errs",
	    &stats->rx_crc_errs, "CRC errors");
	JME_SYSCTL_STAT_ADD32(ctx, child, "mii_errs",
	    &stats->rx_mii_errs, "MII errors");
	JME_SYSCTL_STAT_ADD32(ctx, child, "fifo_oflows",
	    &stats->rx_fifo_oflows, "FIFO overflows");
	JME_SYSCTL_STAT_ADD32(ctx, child, "desc_empty",
	    &stats->rx_desc_empty, "Descriptor empty");
	JME_SYSCTL_STAT_ADD32(ctx, child, "bad_frames",
	    &stats->rx_bad_frames, "Bad frames");

	/* Tx statistics. */
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "tx", CTLFLAG_RD,
	    NULL, "Tx MAC statistics");
	child = SYSCTL_CHILDREN(tree);
	JME_SYSCTL_STAT_ADD32(ctx, child, "good_frames",
	    &stats->tx_good_frames, "Good frames");
	JME_SYSCTL_STAT_ADD32(ctx, child, "bad_frames",
	    &stats->tx_bad_frames, "Bad frames");
}

#undef	JME_SYSCTL_STAT_ADD32

struct jme_dmamap_arg {
	bus_addr_t	jme_busaddr;
};

static void
jme_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct jme_dmamap_arg *ctx;

	if (error != 0)
		return;

	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	ctx = (struct jme_dmamap_arg *)arg;
	ctx->jme_busaddr = segs[0].ds_addr;
}

static int
jme_dma_alloc(struct jme_softc *sc)
{
	struct jme_dmamap_arg ctx;
	struct jme_txdesc *txd;
	struct jme_rxdesc *rxd;
	bus_addr_t lowaddr, rx_ring_end, tx_ring_end;
	int error, i;

	lowaddr = BUS_SPACE_MAXADDR;
	if ((sc->jme_flags & JME_FLAG_DMA32BIT) != 0)
		lowaddr = BUS_SPACE_MAXADDR_32BIT;

again:
	/* Create parent ring tag. */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->jme_dev),/* parent */
	    1, 0,			/* algnmnt, boundary */
	    lowaddr,			/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    0,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->jme_cdata.jme_ring_tag);
	if (error != 0) {
		device_printf(sc->jme_dev,
		    "could not create parent ring DMA tag.\n");
		goto fail;
	}
	/* Create tag for Tx ring. */
	error = bus_dma_tag_create(sc->jme_cdata.jme_ring_tag,/* parent */
	    JME_TX_RING_ALIGN, 0,	/* algnmnt, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    JME_TX_RING_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    JME_TX_RING_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->jme_cdata.jme_tx_ring_tag);
	if (error != 0) {
		device_printf(sc->jme_dev,
		    "could not allocate Tx ring DMA tag.\n");
		goto fail;
	}

	/* Create tag for Rx ring. */
	error = bus_dma_tag_create(sc->jme_cdata.jme_ring_tag,/* parent */
	    JME_RX_RING_ALIGN, 0,	/* algnmnt, boundary */
	    lowaddr,			/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    JME_RX_RING_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    JME_RX_RING_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->jme_cdata.jme_rx_ring_tag);
	if (error != 0) {
		device_printf(sc->jme_dev,
		    "could not allocate Rx ring DMA tag.\n");
		goto fail;
	}

	/* Allocate DMA'able memory and load the DMA map for Tx ring. */
	error = bus_dmamem_alloc(sc->jme_cdata.jme_tx_ring_tag,
	    (void **)&sc->jme_rdata.jme_tx_ring,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->jme_cdata.jme_tx_ring_map);
	if (error != 0) {
		device_printf(sc->jme_dev,
		    "could not allocate DMA'able memory for Tx ring.\n");
		goto fail;
	}

	ctx.jme_busaddr = 0;
	error = bus_dmamap_load(sc->jme_cdata.jme_tx_ring_tag,
	    sc->jme_cdata.jme_tx_ring_map, sc->jme_rdata.jme_tx_ring,
	    JME_TX_RING_SIZE, jme_dmamap_cb, &ctx, BUS_DMA_NOWAIT);
	if (error != 0 || ctx.jme_busaddr == 0) {
		device_printf(sc->jme_dev,
		    "could not load DMA'able memory for Tx ring.\n");
		goto fail;
	}
	sc->jme_rdata.jme_tx_ring_paddr = ctx.jme_busaddr;

	/* Allocate DMA'able memory and load the DMA map for Rx ring. */
	error = bus_dmamem_alloc(sc->jme_cdata.jme_rx_ring_tag,
	    (void **)&sc->jme_rdata.jme_rx_ring,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->jme_cdata.jme_rx_ring_map);
	if (error != 0) {
		device_printf(sc->jme_dev,
		    "could not allocate DMA'able memory for Rx ring.\n");
		goto fail;
	}

	ctx.jme_busaddr = 0;
	error = bus_dmamap_load(sc->jme_cdata.jme_rx_ring_tag,
	    sc->jme_cdata.jme_rx_ring_map, sc->jme_rdata.jme_rx_ring,
	    JME_RX_RING_SIZE, jme_dmamap_cb, &ctx, BUS_DMA_NOWAIT);
	if (error != 0 || ctx.jme_busaddr == 0) {
		device_printf(sc->jme_dev,
		    "could not load DMA'able memory for Rx ring.\n");
		goto fail;
	}
	sc->jme_rdata.jme_rx_ring_paddr = ctx.jme_busaddr;

	if (lowaddr != BUS_SPACE_MAXADDR_32BIT) {
		/* Tx/Rx descriptor queue should reside within 4GB boundary. */
		tx_ring_end = sc->jme_rdata.jme_tx_ring_paddr +
		    JME_TX_RING_SIZE;
		rx_ring_end = sc->jme_rdata.jme_rx_ring_paddr +
		    JME_RX_RING_SIZE;
		if ((JME_ADDR_HI(tx_ring_end) !=
		    JME_ADDR_HI(sc->jme_rdata.jme_tx_ring_paddr)) ||
		    (JME_ADDR_HI(rx_ring_end) !=
		     JME_ADDR_HI(sc->jme_rdata.jme_rx_ring_paddr))) {
			device_printf(sc->jme_dev, "4GB boundary crossed, "
			    "switching to 32bit DMA address mode.\n");
			jme_dma_free(sc);
			/* Limit DMA address space to 32bit and try again. */
			lowaddr = BUS_SPACE_MAXADDR_32BIT;
			goto again;
		}
	}

	lowaddr = BUS_SPACE_MAXADDR;
	if ((sc->jme_flags & JME_FLAG_DMA32BIT) != 0)
		lowaddr = BUS_SPACE_MAXADDR_32BIT;
	/* Create parent buffer tag. */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->jme_dev),/* parent */
	    1, 0,			/* algnmnt, boundary */
	    lowaddr,			/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    0,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->jme_cdata.jme_buffer_tag);
	if (error != 0) {
		device_printf(sc->jme_dev,
		    "could not create parent buffer DMA tag.\n");
		goto fail;
	}

	/* Create shadow status block tag. */
	error = bus_dma_tag_create(sc->jme_cdata.jme_buffer_tag,/* parent */
	    JME_SSB_ALIGN, 0,		/* algnmnt, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    JME_SSB_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    JME_SSB_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->jme_cdata.jme_ssb_tag);
	if (error != 0) {
		device_printf(sc->jme_dev,
		    "could not create shared status block DMA tag.\n");
		goto fail;
	}

	/* Create tag for Tx buffers. */
	error = bus_dma_tag_create(sc->jme_cdata.jme_buffer_tag,/* parent */
	    1, 0,			/* algnmnt, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    JME_TSO_MAXSIZE,		/* maxsize */
	    JME_MAXTXSEGS,		/* nsegments */
	    JME_TSO_MAXSEGSIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->jme_cdata.jme_tx_tag);
	if (error != 0) {
		device_printf(sc->jme_dev, "could not create Tx DMA tag.\n");
		goto fail;
	}

	/* Create tag for Rx buffers. */
	error = bus_dma_tag_create(sc->jme_cdata.jme_buffer_tag,/* parent */
	    JME_RX_BUF_ALIGN, 0,	/* algnmnt, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES,			/* maxsize */
	    1,				/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->jme_cdata.jme_rx_tag);
	if (error != 0) {
		device_printf(sc->jme_dev, "could not create Rx DMA tag.\n");
		goto fail;
	}

	/*
	 * Allocate DMA'able memory and load the DMA map for shared
	 * status block.
	 */
	error = bus_dmamem_alloc(sc->jme_cdata.jme_ssb_tag,
	    (void **)&sc->jme_rdata.jme_ssb_block,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->jme_cdata.jme_ssb_map);
	if (error != 0) {
		device_printf(sc->jme_dev, "could not allocate DMA'able "
		    "memory for shared status block.\n");
		goto fail;
	}

	ctx.jme_busaddr = 0;
	error = bus_dmamap_load(sc->jme_cdata.jme_ssb_tag,
	    sc->jme_cdata.jme_ssb_map, sc->jme_rdata.jme_ssb_block,
	    JME_SSB_SIZE, jme_dmamap_cb, &ctx, BUS_DMA_NOWAIT);
	if (error != 0 || ctx.jme_busaddr == 0) {
		device_printf(sc->jme_dev, "could not load DMA'able memory "
		    "for shared status block.\n");
		goto fail;
	}
	sc->jme_rdata.jme_ssb_block_paddr = ctx.jme_busaddr;

	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < JME_TX_RING_CNT; i++) {
		txd = &sc->jme_cdata.jme_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_dmamap = NULL;
		error = bus_dmamap_create(sc->jme_cdata.jme_tx_tag, 0,
		    &txd->tx_dmamap);
		if (error != 0) {
			device_printf(sc->jme_dev,
			    "could not create Tx dmamap.\n");
			goto fail;
		}
	}
	/* Create DMA maps for Rx buffers. */
	if ((error = bus_dmamap_create(sc->jme_cdata.jme_rx_tag, 0,
	    &sc->jme_cdata.jme_rx_sparemap)) != 0) {
		device_printf(sc->jme_dev,
		    "could not create spare Rx dmamap.\n");
		goto fail;
	}
	for (i = 0; i < JME_RX_RING_CNT; i++) {
		rxd = &sc->jme_cdata.jme_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_dmamap = NULL;
		error = bus_dmamap_create(sc->jme_cdata.jme_rx_tag, 0,
		    &rxd->rx_dmamap);
		if (error != 0) {
			device_printf(sc->jme_dev,
			    "could not create Rx dmamap.\n");
			goto fail;
		}
	}

fail:
	return (error);
}

static void
jme_dma_free(struct jme_softc *sc)
{
	struct jme_txdesc *txd;
	struct jme_rxdesc *rxd;
	int i;

	/* Tx ring */
	if (sc->jme_cdata.jme_tx_ring_tag != NULL) {
		if (sc->jme_rdata.jme_tx_ring_paddr)
			bus_dmamap_unload(sc->jme_cdata.jme_tx_ring_tag,
			    sc->jme_cdata.jme_tx_ring_map);
		if (sc->jme_rdata.jme_tx_ring)
			bus_dmamem_free(sc->jme_cdata.jme_tx_ring_tag,
			    sc->jme_rdata.jme_tx_ring,
			    sc->jme_cdata.jme_tx_ring_map);
		sc->jme_rdata.jme_tx_ring = NULL;
		sc->jme_rdata.jme_tx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->jme_cdata.jme_tx_ring_tag);
		sc->jme_cdata.jme_tx_ring_tag = NULL;
	}
	/* Rx ring */
	if (sc->jme_cdata.jme_rx_ring_tag != NULL) {
		if (sc->jme_rdata.jme_rx_ring_paddr)
			bus_dmamap_unload(sc->jme_cdata.jme_rx_ring_tag,
			    sc->jme_cdata.jme_rx_ring_map);
		if (sc->jme_rdata.jme_rx_ring)
			bus_dmamem_free(sc->jme_cdata.jme_rx_ring_tag,
			    sc->jme_rdata.jme_rx_ring,
			    sc->jme_cdata.jme_rx_ring_map);
		sc->jme_rdata.jme_rx_ring = NULL;
		sc->jme_rdata.jme_rx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->jme_cdata.jme_rx_ring_tag);
		sc->jme_cdata.jme_rx_ring_tag = NULL;
	}
	/* Tx buffers */
	if (sc->jme_cdata.jme_tx_tag != NULL) {
		for (i = 0; i < JME_TX_RING_CNT; i++) {
			txd = &sc->jme_cdata.jme_txdesc[i];
			if (txd->tx_dmamap != NULL) {
				bus_dmamap_destroy(sc->jme_cdata.jme_tx_tag,
				    txd->tx_dmamap);
				txd->tx_dmamap = NULL;
			}
		}
		bus_dma_tag_destroy(sc->jme_cdata.jme_tx_tag);
		sc->jme_cdata.jme_tx_tag = NULL;
	}
	/* Rx buffers */
	if (sc->jme_cdata.jme_rx_tag != NULL) {
		for (i = 0; i < JME_RX_RING_CNT; i++) {
			rxd = &sc->jme_cdata.jme_rxdesc[i];
			if (rxd->rx_dmamap != NULL) {
				bus_dmamap_destroy(sc->jme_cdata.jme_rx_tag,
				    rxd->rx_dmamap);
				rxd->rx_dmamap = NULL;
			}
		}
		if (sc->jme_cdata.jme_rx_sparemap != NULL) {
			bus_dmamap_destroy(sc->jme_cdata.jme_rx_tag,
			    sc->jme_cdata.jme_rx_sparemap);
			sc->jme_cdata.jme_rx_sparemap = NULL;
		}
		bus_dma_tag_destroy(sc->jme_cdata.jme_rx_tag);
		sc->jme_cdata.jme_rx_tag = NULL;
	}

	/* Shared status block. */
	if (sc->jme_cdata.jme_ssb_tag != NULL) {
		if (sc->jme_rdata.jme_ssb_block_paddr)
			bus_dmamap_unload(sc->jme_cdata.jme_ssb_tag,
			    sc->jme_cdata.jme_ssb_map);
		if (sc->jme_rdata.jme_ssb_block)
			bus_dmamem_free(sc->jme_cdata.jme_ssb_tag,
			    sc->jme_rdata.jme_ssb_block,
			    sc->jme_cdata.jme_ssb_map);
		sc->jme_rdata.jme_ssb_block = NULL;
		sc->jme_rdata.jme_ssb_block_paddr = 0;
		bus_dma_tag_destroy(sc->jme_cdata.jme_ssb_tag);
		sc->jme_cdata.jme_ssb_tag = NULL;
	}

	if (sc->jme_cdata.jme_buffer_tag != NULL) {
		bus_dma_tag_destroy(sc->jme_cdata.jme_buffer_tag);
		sc->jme_cdata.jme_buffer_tag = NULL;
	}
	if (sc->jme_cdata.jme_ring_tag != NULL) {
		bus_dma_tag_destroy(sc->jme_cdata.jme_ring_tag);
		sc->jme_cdata.jme_ring_tag = NULL;
	}
}

/*
 *	Make sure the interface is stopped at reboot time.
 */
static int
jme_shutdown(device_t dev)
{

	return (jme_suspend(dev));
}

/*
 * Unlike other ethernet controllers, JMC250 requires
 * explicit resetting link speed to 10/100Mbps as gigabit
 * link will cunsume more power than 375mA.
 * Note, we reset the link speed to 10/100Mbps with
 * auto-negotiation but we don't know whether that operation
 * would succeed or not as we have no control after powering
 * off. If the renegotiation fail WOL may not work. Running
 * at 1Gbps draws more power than 375mA at 3.3V which is
 * specified in PCI specification and that would result in
 * complete shutdowning power to ethernet controller.
 *
 * TODO
 *  Save current negotiated media speed/duplex/flow-control
 *  to softc and restore the same link again after resuming.
 *  PHY handling such as power down/resetting to 100Mbps
 *  may be better handled in suspend method in phy driver.
 */
static void
jme_setlinkspeed(struct jme_softc *sc)
{
	struct mii_data *mii;
	int aneg, i;

	JME_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->jme_miibus);
	mii_pollstat(mii);
	aneg = 0;
	if ((mii->mii_media_status & IFM_AVALID) != 0) {
		switch IFM_SUBTYPE(mii->mii_media_active) {
		case IFM_10_T:
		case IFM_100_TX:
			return;
		case IFM_1000_T:
			aneg++;
		default:
			break;
		}
	}
	jme_miibus_writereg(sc->jme_dev, sc->jme_phyaddr, MII_100T2CR, 0);
	jme_miibus_writereg(sc->jme_dev, sc->jme_phyaddr, MII_ANAR,
	    ANAR_TX_FD | ANAR_TX | ANAR_10_FD | ANAR_10 | ANAR_CSMA);
	jme_miibus_writereg(sc->jme_dev, sc->jme_phyaddr, MII_BMCR,
	    BMCR_AUTOEN | BMCR_STARTNEG);
	DELAY(1000);
	if (aneg != 0) {
		/* Poll link state until jme(4) get a 10/100 link. */
		for (i = 0; i < MII_ANEGTICKS_GIGE; i++) {
			mii_pollstat(mii);
			if ((mii->mii_media_status & IFM_AVALID) != 0) {
				switch (IFM_SUBTYPE(mii->mii_media_active)) {
				case IFM_10_T:
				case IFM_100_TX:
					jme_mac_config(sc);
					return;
				default:
					break;
				}
			}
			JME_UNLOCK(sc);
			pause("jmelnk", hz);
			JME_LOCK(sc);
		}
		if (i == MII_ANEGTICKS_GIGE)
			device_printf(sc->jme_dev, "establishing link failed, "
			    "WOL may not work!");
	}
	/*
	 * No link, force MAC to have 100Mbps, full-duplex link.
	 * This is the last resort and may/may not work.
	 */
	mii->mii_media_status = IFM_AVALID | IFM_ACTIVE;
	mii->mii_media_active = IFM_ETHER | IFM_100_TX | IFM_FDX;
	jme_mac_config(sc);
}

static void
jme_setwol(struct jme_softc *sc)
{
	struct ifnet *ifp;
	uint32_t gpr, pmcs;
	uint16_t pmstat;
	int pmc;

	JME_LOCK_ASSERT(sc);

	if (pci_find_cap(sc->jme_dev, PCIY_PMG, &pmc) != 0) {
		/* Remove Tx MAC/offload clock to save more power. */
		if ((sc->jme_flags & JME_FLAG_TXCLK) != 0)
			CSR_WRITE_4(sc, JME_GHC, CSR_READ_4(sc, JME_GHC) &
			    ~(GHC_TX_OFFLD_CLK_100 | GHC_TX_MAC_CLK_100 |
			    GHC_TX_OFFLD_CLK_1000 | GHC_TX_MAC_CLK_1000));
		if ((sc->jme_flags & JME_FLAG_RXCLK) != 0)
			CSR_WRITE_4(sc, JME_GPREG1,
			    CSR_READ_4(sc, JME_GPREG1) | GPREG1_RX_MAC_CLK_DIS);
		/* No PME capability, PHY power down. */
		jme_phy_down(sc);
		return;
	}

	ifp = sc->jme_ifp;
	gpr = CSR_READ_4(sc, JME_GPREG0) & ~GPREG0_PME_ENB;
	pmcs = CSR_READ_4(sc, JME_PMCS);
	pmcs &= ~PMCS_WOL_ENB_MASK;
	if ((ifp->if_capenable & IFCAP_WOL_MAGIC) != 0) {
		pmcs |= PMCS_MAGIC_FRAME | PMCS_MAGIC_FRAME_ENB;
		/* Enable PME message. */
		gpr |= GPREG0_PME_ENB;
		/* For gigabit controllers, reset link speed to 10/100. */
		if ((sc->jme_flags & JME_FLAG_FASTETH) == 0)
			jme_setlinkspeed(sc);
	}

	CSR_WRITE_4(sc, JME_PMCS, pmcs);
	CSR_WRITE_4(sc, JME_GPREG0, gpr);
	/* Remove Tx MAC/offload clock to save more power. */
	if ((sc->jme_flags & JME_FLAG_TXCLK) != 0)
		CSR_WRITE_4(sc, JME_GHC, CSR_READ_4(sc, JME_GHC) &
		    ~(GHC_TX_OFFLD_CLK_100 | GHC_TX_MAC_CLK_100 |
		    GHC_TX_OFFLD_CLK_1000 | GHC_TX_MAC_CLK_1000));
	/* Request PME. */
	pmstat = pci_read_config(sc->jme_dev, pmc + PCIR_POWER_STATUS, 2);
	pmstat &= ~(PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE);
	if ((ifp->if_capenable & IFCAP_WOL) != 0)
		pmstat |= PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE;
	pci_write_config(sc->jme_dev, pmc + PCIR_POWER_STATUS, pmstat, 2);
	if ((ifp->if_capenable & IFCAP_WOL) == 0) {
		/* No WOL, PHY power down. */
		jme_phy_down(sc);
	}
}

static int
jme_suspend(device_t dev)
{
	struct jme_softc *sc;

	sc = device_get_softc(dev);

	JME_LOCK(sc);
	jme_stop(sc);
	jme_setwol(sc);
	JME_UNLOCK(sc);

	return (0);
}

static int
jme_resume(device_t dev)
{
	struct jme_softc *sc;
	struct ifnet *ifp;
	uint16_t pmstat;
	int pmc;

	sc = device_get_softc(dev);

	JME_LOCK(sc);
	if (pci_find_cap(sc->jme_dev, PCIY_PMG, &pmc) == 0) {
		pmstat = pci_read_config(sc->jme_dev,
		    pmc + PCIR_POWER_STATUS, 2);
		/* Disable PME clear PME status. */
		pmstat &= ~PCIM_PSTAT_PMEENABLE;
		pci_write_config(sc->jme_dev,
		    pmc + PCIR_POWER_STATUS, pmstat, 2);
	}
	/* Wakeup PHY. */
	jme_phy_up(sc);
	ifp = sc->jme_ifp;
	if ((ifp->if_flags & IFF_UP) != 0) {
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		jme_init_locked(sc);
	}

	JME_UNLOCK(sc);

	return (0);
}

static int
jme_encap(struct jme_softc *sc, struct mbuf **m_head)
{
	struct jme_txdesc *txd;
	struct jme_desc *desc;
	struct mbuf *m;
	bus_dma_segment_t txsegs[JME_MAXTXSEGS];
	int error, i, nsegs, prod;
	uint32_t cflags, tsosegsz;

	JME_LOCK_ASSERT(sc);

	M_ASSERTPKTHDR((*m_head));

	if (((*m_head)->m_pkthdr.csum_flags & CSUM_TSO) != 0) {
		/*
		 * Due to the adherence to NDIS specification JMC250
		 * assumes upper stack computed TCP pseudo checksum
		 * without including payload length. This breaks
		 * checksum offload for TSO case so recompute TCP
		 * pseudo checksum for JMC250. Hopefully this wouldn't
		 * be much burden on modern CPUs.
		 */
		struct ether_header *eh;
		struct ip *ip;
		struct tcphdr *tcp;
		uint32_t ip_off, poff;

		if (M_WRITABLE(*m_head) == 0) {
			/* Get a writable copy. */
			m = m_dup(*m_head, M_NOWAIT);
			m_freem(*m_head);
			if (m == NULL) {
				*m_head = NULL;
				return (ENOBUFS);
			}
			*m_head = m;
		}
		ip_off = sizeof(struct ether_header);
		m = m_pullup(*m_head, ip_off);
		if (m == NULL) {
			*m_head = NULL;
			return (ENOBUFS);
		}
		eh = mtod(m, struct ether_header *);
		/* Check the existence of VLAN tag. */
		if (eh->ether_type == htons(ETHERTYPE_VLAN)) {
			ip_off = sizeof(struct ether_vlan_header);
			m = m_pullup(m, ip_off);
			if (m == NULL) {
				*m_head = NULL;
				return (ENOBUFS);
			}
		}
		m = m_pullup(m, ip_off + sizeof(struct ip));
		if (m == NULL) {
			*m_head = NULL;
			return (ENOBUFS);
		}
		ip = (struct ip *)(mtod(m, char *) + ip_off);
		poff = ip_off + (ip->ip_hl << 2);
		m = m_pullup(m, poff + sizeof(struct tcphdr));
		if (m == NULL) {
			*m_head = NULL;
			return (ENOBUFS);
		}
		/*
		 * Reset IP checksum and recompute TCP pseudo
		 * checksum that NDIS specification requires.
		 */
		ip = (struct ip *)(mtod(m, char *) + ip_off);
		tcp = (struct tcphdr *)(mtod(m, char *) + poff);
		ip->ip_sum = 0;
		if (poff + (tcp->th_off << 2) == m->m_pkthdr.len) {
			tcp->th_sum = in_pseudo(ip->ip_src.s_addr,
			    ip->ip_dst.s_addr,
			    htons((tcp->th_off << 2) + IPPROTO_TCP));
			/* No need to TSO, force IP checksum offload. */
			(*m_head)->m_pkthdr.csum_flags &= ~CSUM_TSO;
			(*m_head)->m_pkthdr.csum_flags |= CSUM_IP;
		} else
			tcp->th_sum = in_pseudo(ip->ip_src.s_addr,
			    ip->ip_dst.s_addr, htons(IPPROTO_TCP));
		*m_head = m;
	}

	prod = sc->jme_cdata.jme_tx_prod;
	txd = &sc->jme_cdata.jme_txdesc[prod];

	error = bus_dmamap_load_mbuf_sg(sc->jme_cdata.jme_tx_tag,
	    txd->tx_dmamap, *m_head, txsegs, &nsegs, 0);
	if (error == EFBIG) {
		m = m_collapse(*m_head, M_NOWAIT, JME_MAXTXSEGS);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOMEM);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->jme_cdata.jme_tx_tag,
		    txd->tx_dmamap, *m_head, txsegs, &nsegs, 0);
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

	/*
	 * Check descriptor overrun. Leave one free descriptor.
	 * Since we always use 64bit address mode for transmitting,
	 * each Tx request requires one more dummy descriptor.
	 */
	if (sc->jme_cdata.jme_tx_cnt + nsegs + 1 > JME_TX_RING_CNT - 1) {
		bus_dmamap_unload(sc->jme_cdata.jme_tx_tag, txd->tx_dmamap);
		return (ENOBUFS);
	}

	m = *m_head;
	cflags = 0;
	tsosegsz = 0;
	/* Configure checksum offload and TSO. */
	if ((m->m_pkthdr.csum_flags & CSUM_TSO) != 0) {
		tsosegsz = (uint32_t)m->m_pkthdr.tso_segsz <<
		    JME_TD_MSS_SHIFT;
		cflags |= JME_TD_TSO;
	} else {
		if ((m->m_pkthdr.csum_flags & CSUM_IP) != 0)
			cflags |= JME_TD_IPCSUM;
		if ((m->m_pkthdr.csum_flags & CSUM_TCP) != 0)
			cflags |= JME_TD_TCPCSUM;
		if ((m->m_pkthdr.csum_flags & CSUM_UDP) != 0)
			cflags |= JME_TD_UDPCSUM;
	}
	/* Configure VLAN. */
	if ((m->m_flags & M_VLANTAG) != 0) {
		cflags |= (m->m_pkthdr.ether_vtag & JME_TD_VLAN_MASK);
		cflags |= JME_TD_VLAN_TAG;
	}

	desc = &sc->jme_rdata.jme_tx_ring[prod];
	desc->flags = htole32(cflags);
	desc->buflen = htole32(tsosegsz);
	desc->addr_hi = htole32(m->m_pkthdr.len);
	desc->addr_lo = 0;
	sc->jme_cdata.jme_tx_cnt++;
	JME_DESC_INC(prod, JME_TX_RING_CNT);
	for (i = 0; i < nsegs; i++) {
		desc = &sc->jme_rdata.jme_tx_ring[prod];
		desc->flags = htole32(JME_TD_OWN | JME_TD_64BIT);
		desc->buflen = htole32(txsegs[i].ds_len);
		desc->addr_hi = htole32(JME_ADDR_HI(txsegs[i].ds_addr));
		desc->addr_lo = htole32(JME_ADDR_LO(txsegs[i].ds_addr));
		sc->jme_cdata.jme_tx_cnt++;
		JME_DESC_INC(prod, JME_TX_RING_CNT);
	}

	/* Update producer index. */
	sc->jme_cdata.jme_tx_prod = prod;
	/*
	 * Finally request interrupt and give the first descriptor
	 * owenership to hardware.
	 */
	desc = txd->tx_desc;
	desc->flags |= htole32(JME_TD_OWN | JME_TD_INTR);

	txd->tx_m = m;
	txd->tx_ndesc = nsegs + 1;

	/* Sync descriptors. */
	bus_dmamap_sync(sc->jme_cdata.jme_tx_tag, txd->tx_dmamap,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->jme_cdata.jme_tx_ring_tag,
	    sc->jme_cdata.jme_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

static void
jme_start(struct ifnet *ifp)
{
        struct jme_softc *sc;

	sc = ifp->if_softc;
	JME_LOCK(sc);
	jme_start_locked(ifp);
	JME_UNLOCK(sc);
}

static void
jme_start_locked(struct ifnet *ifp)
{
        struct jme_softc *sc;
        struct mbuf *m_head;
	int enq;

	sc = ifp->if_softc;

	JME_LOCK_ASSERT(sc);

	if (sc->jme_cdata.jme_tx_cnt >= JME_TX_DESC_HIWAT)
		jme_txeof(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || (sc->jme_flags & JME_FLAG_LINK) == 0)
		return;

	for (enq = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd); ) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;
		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (jme_encap(sc, &m_head)) {
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
		/*
		 * Reading TXCSR takes very long time under heavy load
		 * so cache TXCSR value and writes the ORed value with
		 * the kick command to the TXCSR. This saves one register
		 * access cycle.
		 */
		CSR_WRITE_4(sc, JME_TXCSR, sc->jme_txcsr | TXCSR_TX_ENB |
		    TXCSR_TXQ_N_START(TXCSR_TXQ0));
		/* Set a timeout in case the chip goes out to lunch. */
		sc->jme_watchdog_timer = JME_TX_TIMEOUT;
	}
}

static void
jme_watchdog(struct jme_softc *sc)
{
	struct ifnet *ifp;

	JME_LOCK_ASSERT(sc);

	if (sc->jme_watchdog_timer == 0 || --sc->jme_watchdog_timer)
		return;

	ifp = sc->jme_ifp;
	if ((sc->jme_flags & JME_FLAG_LINK) == 0) {
		if_printf(sc->jme_ifp, "watchdog timeout (missed link)\n");
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		jme_init_locked(sc);
		return;
	}
	jme_txeof(sc);
	if (sc->jme_cdata.jme_tx_cnt == 0) {
		if_printf(sc->jme_ifp,
		    "watchdog timeout (missed Tx interrupts) -- recovering\n");
		if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			jme_start_locked(ifp);
		return;
	}

	if_printf(sc->jme_ifp, "watchdog timeout\n");
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	jme_init_locked(sc);
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		jme_start_locked(ifp);
}

static int
jme_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct jme_softc *sc;
	struct ifreq *ifr;
	struct mii_data *mii;
	uint32_t reg;
	int error, mask;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;
	error = 0;
	switch (cmd) {
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > JME_JUMBO_MTU ||
		    ((sc->jme_flags & JME_FLAG_NOJUMBO) != 0 &&
		    ifr->ifr_mtu > JME_MAX_MTU)) {
			error = EINVAL;
			break;
		}

		if (ifp->if_mtu != ifr->ifr_mtu) {
			/*
			 * No special configuration is required when interface
			 * MTU is changed but availability of TSO/Tx checksum
			 * offload should be chcked against new MTU size as
			 * FIFO size is just 2K.
			 */
			JME_LOCK(sc);
			if (ifr->ifr_mtu >= JME_TX_FIFO_SIZE) {
				ifp->if_capenable &=
				    ~(IFCAP_TXCSUM | IFCAP_TSO4);
				ifp->if_hwassist &=
				    ~(JME_CSUM_FEATURES | CSUM_TSO);
				VLAN_CAPABILITIES(ifp);
			}
			ifp->if_mtu = ifr->ifr_mtu;
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				jme_init_locked(sc);
			}
			JME_UNLOCK(sc);
		}
		break;
	case SIOCSIFFLAGS:
		JME_LOCK(sc);
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				if (((ifp->if_flags ^ sc->jme_if_flags)
				    & (IFF_PROMISC | IFF_ALLMULTI)) != 0)
					jme_set_filter(sc);
			} else {
				if ((sc->jme_flags & JME_FLAG_DETACH) == 0)
					jme_init_locked(sc);
			}
		} else {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
				jme_stop(sc);
		}
		sc->jme_if_flags = ifp->if_flags;
		JME_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		JME_LOCK(sc);
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			jme_set_filter(sc);
		JME_UNLOCK(sc);
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		mii = device_get_softc(sc->jme_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;
	case SIOCSIFCAP:
		JME_LOCK(sc);
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if ((mask & IFCAP_TXCSUM) != 0 &&
		    ifp->if_mtu < JME_TX_FIFO_SIZE) {
			if ((IFCAP_TXCSUM & ifp->if_capabilities) != 0) {
				ifp->if_capenable ^= IFCAP_TXCSUM;
				if ((IFCAP_TXCSUM & ifp->if_capenable) != 0)
					ifp->if_hwassist |= JME_CSUM_FEATURES;
				else
					ifp->if_hwassist &= ~JME_CSUM_FEATURES;
			}
		}
		if ((mask & IFCAP_RXCSUM) != 0 &&
		    (IFCAP_RXCSUM & ifp->if_capabilities) != 0) {
			ifp->if_capenable ^= IFCAP_RXCSUM;
			reg = CSR_READ_4(sc, JME_RXMAC);
			reg &= ~RXMAC_CSUM_ENB;
			if ((ifp->if_capenable & IFCAP_RXCSUM) != 0)
				reg |= RXMAC_CSUM_ENB;
			CSR_WRITE_4(sc, JME_RXMAC, reg);
		}
		if ((mask & IFCAP_TSO4) != 0 &&
		    ifp->if_mtu < JME_TX_FIFO_SIZE) {
			if ((IFCAP_TSO4 & ifp->if_capabilities) != 0) {
				ifp->if_capenable ^= IFCAP_TSO4;
				if ((IFCAP_TSO4 & ifp->if_capenable) != 0)
					ifp->if_hwassist |= CSUM_TSO;
				else
					ifp->if_hwassist &= ~CSUM_TSO;
			}
		}
		if ((mask & IFCAP_WOL_MAGIC) != 0 &&
		    (IFCAP_WOL_MAGIC & ifp->if_capabilities) != 0)
			ifp->if_capenable ^= IFCAP_WOL_MAGIC;
		if ((mask & IFCAP_VLAN_HWCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWCSUM) != 0)
			ifp->if_capenable ^= IFCAP_VLAN_HWCSUM;
		if ((mask & IFCAP_VLAN_HWTSO) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWTSO) != 0)
			ifp->if_capenable ^= IFCAP_VLAN_HWTSO;
		if ((mask & IFCAP_VLAN_HWTAGGING) != 0 &&
		    (IFCAP_VLAN_HWTAGGING & ifp->if_capabilities) != 0) {
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
			jme_set_vlan(sc);
		}
		JME_UNLOCK(sc);
		VLAN_CAPABILITIES(ifp);
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}

static void
jme_mac_config(struct jme_softc *sc)
{
	struct mii_data *mii;
	uint32_t ghc, gpreg, rxmac, txmac, txpause;
	uint32_t txclk;

	JME_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->jme_miibus);

	CSR_WRITE_4(sc, JME_GHC, GHC_RESET);
	DELAY(10);
	CSR_WRITE_4(sc, JME_GHC, 0);
	ghc = 0;
	txclk = 0;
	rxmac = CSR_READ_4(sc, JME_RXMAC);
	rxmac &= ~RXMAC_FC_ENB;
	txmac = CSR_READ_4(sc, JME_TXMAC);
	txmac &= ~(TXMAC_CARRIER_EXT | TXMAC_FRAME_BURST);
	txpause = CSR_READ_4(sc, JME_TXPFC);
	txpause &= ~TXPFC_PAUSE_ENB;
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		ghc |= GHC_FULL_DUPLEX;
		rxmac &= ~RXMAC_COLL_DET_ENB;
		txmac &= ~(TXMAC_COLL_ENB | TXMAC_CARRIER_SENSE |
		    TXMAC_BACKOFF | TXMAC_CARRIER_EXT |
		    TXMAC_FRAME_BURST);
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_TXPAUSE) != 0)
			txpause |= TXPFC_PAUSE_ENB;
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_RXPAUSE) != 0)
			rxmac |= RXMAC_FC_ENB;
		/* Disable retry transmit timer/retry limit. */
		CSR_WRITE_4(sc, JME_TXTRHD, CSR_READ_4(sc, JME_TXTRHD) &
		    ~(TXTRHD_RT_PERIOD_ENB | TXTRHD_RT_LIMIT_ENB));
	} else {
		rxmac |= RXMAC_COLL_DET_ENB;
		txmac |= TXMAC_COLL_ENB | TXMAC_CARRIER_SENSE | TXMAC_BACKOFF;
		/* Enable retry transmit timer/retry limit. */
		CSR_WRITE_4(sc, JME_TXTRHD, CSR_READ_4(sc, JME_TXTRHD) |
		    TXTRHD_RT_PERIOD_ENB | TXTRHD_RT_LIMIT_ENB);
	}
		/* Reprogram Tx/Rx MACs with resolved speed/duplex. */
	switch (IFM_SUBTYPE(mii->mii_media_active)) {
	case IFM_10_T:
		ghc |= GHC_SPEED_10;
		txclk |= GHC_TX_OFFLD_CLK_100 | GHC_TX_MAC_CLK_100;
		break;
	case IFM_100_TX:
		ghc |= GHC_SPEED_100;
		txclk |= GHC_TX_OFFLD_CLK_100 | GHC_TX_MAC_CLK_100;
		break;
	case IFM_1000_T:
		if ((sc->jme_flags & JME_FLAG_FASTETH) != 0)
			break;
		ghc |= GHC_SPEED_1000;
		txclk |= GHC_TX_OFFLD_CLK_1000 | GHC_TX_MAC_CLK_1000;
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) == 0)
			txmac |= TXMAC_CARRIER_EXT | TXMAC_FRAME_BURST;
		break;
	default:
		break;
	}
	if (sc->jme_rev == DEVICEID_JMC250 &&
	    sc->jme_chip_rev == DEVICEREVID_JMC250_A2) {
		/*
		 * Workaround occasional packet loss issue of JMC250 A2
		 * when it runs on half-duplex media.
		 */
		gpreg = CSR_READ_4(sc, JME_GPREG1);
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0)
			gpreg &= ~GPREG1_HDPX_FIX;
		else
			gpreg |= GPREG1_HDPX_FIX;
		CSR_WRITE_4(sc, JME_GPREG1, gpreg);
		/* Workaround CRC errors at 100Mbps on JMC250 A2. */
		if (IFM_SUBTYPE(mii->mii_media_active) == IFM_100_TX) {
			/* Extend interface FIFO depth. */
			jme_miibus_writereg(sc->jme_dev, sc->jme_phyaddr,
			    0x1B, 0x0000);
		} else {
			/* Select default interface FIFO depth. */
			jme_miibus_writereg(sc->jme_dev, sc->jme_phyaddr,
			    0x1B, 0x0004);
		}
	}
	if ((sc->jme_flags & JME_FLAG_TXCLK) != 0)
		ghc |= txclk;
	CSR_WRITE_4(sc, JME_GHC, ghc);
	CSR_WRITE_4(sc, JME_RXMAC, rxmac);
	CSR_WRITE_4(sc, JME_TXMAC, txmac);
	CSR_WRITE_4(sc, JME_TXPFC, txpause);
}

static void
jme_link_task(void *arg, int pending)
{
	struct jme_softc *sc;
	struct mii_data *mii;
	struct ifnet *ifp;
	struct jme_txdesc *txd;
	bus_addr_t paddr;
	int i;

	sc = (struct jme_softc *)arg;

	JME_LOCK(sc);
	mii = device_get_softc(sc->jme_miibus);
	ifp = sc->jme_ifp;
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		JME_UNLOCK(sc);
		return;
	}

	sc->jme_flags &= ~JME_FLAG_LINK;
	if ((mii->mii_media_status & IFM_AVALID) != 0) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->jme_flags |= JME_FLAG_LINK;
			break;
		case IFM_1000_T:
			if ((sc->jme_flags & JME_FLAG_FASTETH) != 0)
				break;
			sc->jme_flags |= JME_FLAG_LINK;
			break;
		default:
			break;
		}
	}

	/*
	 * Disabling Rx/Tx MACs have a side-effect of resetting
	 * JME_TXNDA/JME_RXNDA register to the first address of
	 * Tx/Rx descriptor address. So driver should reset its
	 * internal procucer/consumer pointer and reclaim any
	 * allocated resources. Note, just saving the value of
	 * JME_TXNDA and JME_RXNDA registers before stopping MAC
	 * and restoring JME_TXNDA/JME_RXNDA register is not
	 * sufficient to make sure correct MAC state because
	 * stopping MAC operation can take a while and hardware
	 * might have updated JME_TXNDA/JME_RXNDA registers
	 * during the stop operation.
	 */
	/* Block execution of task. */
	taskqueue_block(sc->jme_tq);
	/* Disable interrupts and stop driver. */
	CSR_WRITE_4(sc, JME_INTR_MASK_CLR, JME_INTRS);
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	callout_stop(&sc->jme_tick_ch);
	sc->jme_watchdog_timer = 0;

	/* Stop receiver/transmitter. */
	jme_stop_rx(sc);
	jme_stop_tx(sc);

	/* XXX Drain all queued tasks. */
	JME_UNLOCK(sc);
	taskqueue_drain(sc->jme_tq, &sc->jme_int_task);
	JME_LOCK(sc);

	if (sc->jme_cdata.jme_rxhead != NULL)
		m_freem(sc->jme_cdata.jme_rxhead);
	JME_RXCHAIN_RESET(sc);
	jme_txeof(sc);
	if (sc->jme_cdata.jme_tx_cnt != 0) {
		/* Remove queued packets for transmit. */
		for (i = 0; i < JME_TX_RING_CNT; i++) {
			txd = &sc->jme_cdata.jme_txdesc[i];
			if (txd->tx_m != NULL) {
				bus_dmamap_sync(
				    sc->jme_cdata.jme_tx_tag,
				    txd->tx_dmamap,
				    BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(
				    sc->jme_cdata.jme_tx_tag,
				    txd->tx_dmamap);
				m_freem(txd->tx_m);
				txd->tx_m = NULL;
				txd->tx_ndesc = 0;
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			}
		}
	}

	/*
	 * Reuse configured Rx descriptors and reset
	 * producer/consumer index.
	 */
	sc->jme_cdata.jme_rx_cons = 0;
	sc->jme_morework = 0;
	jme_init_tx_ring(sc);
	/* Initialize shadow status block. */
	jme_init_ssb(sc);

	/* Program MAC with resolved speed/duplex/flow-control. */
	if ((sc->jme_flags & JME_FLAG_LINK) != 0) {
		jme_mac_config(sc);
		jme_stats_clear(sc);

		CSR_WRITE_4(sc, JME_RXCSR, sc->jme_rxcsr);
		CSR_WRITE_4(sc, JME_TXCSR, sc->jme_txcsr);

		/* Set Tx ring address to the hardware. */
		paddr = JME_TX_RING_ADDR(sc, 0);
		CSR_WRITE_4(sc, JME_TXDBA_HI, JME_ADDR_HI(paddr));
		CSR_WRITE_4(sc, JME_TXDBA_LO, JME_ADDR_LO(paddr));

		/* Set Rx ring address to the hardware. */
		paddr = JME_RX_RING_ADDR(sc, 0);
		CSR_WRITE_4(sc, JME_RXDBA_HI, JME_ADDR_HI(paddr));
		CSR_WRITE_4(sc, JME_RXDBA_LO, JME_ADDR_LO(paddr));

		/* Restart receiver/transmitter. */
		CSR_WRITE_4(sc, JME_RXCSR, sc->jme_rxcsr | RXCSR_RX_ENB |
		    RXCSR_RXQ_START);
		CSR_WRITE_4(sc, JME_TXCSR, sc->jme_txcsr | TXCSR_TX_ENB);
		/* Lastly enable TX/RX clock. */
		if ((sc->jme_flags & JME_FLAG_TXCLK) != 0)
			CSR_WRITE_4(sc, JME_GHC,
			    CSR_READ_4(sc, JME_GHC) & ~GHC_TX_MAC_CLK_DIS);
		if ((sc->jme_flags & JME_FLAG_RXCLK) != 0)
			CSR_WRITE_4(sc, JME_GPREG1,
			    CSR_READ_4(sc, JME_GPREG1) & ~GPREG1_RX_MAC_CLK_DIS);
	}

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	callout_reset(&sc->jme_tick_ch, hz, jme_tick, sc);
	/* Unblock execution of task. */
	taskqueue_unblock(sc->jme_tq);
	/* Reenable interrupts. */
	CSR_WRITE_4(sc, JME_INTR_MASK_SET, JME_INTRS);

	JME_UNLOCK(sc);
}

static int
jme_intr(void *arg)
{
	struct jme_softc *sc;
	uint32_t status;

	sc = (struct jme_softc *)arg;

	status = CSR_READ_4(sc, JME_INTR_REQ_STATUS);
	if (status == 0 || status == 0xFFFFFFFF)
		return (FILTER_STRAY);
	/* Disable interrupts. */
	CSR_WRITE_4(sc, JME_INTR_MASK_CLR, JME_INTRS);
	taskqueue_enqueue(sc->jme_tq, &sc->jme_int_task);

	return (FILTER_HANDLED);
}

static void
jme_int_task(void *arg, int pending)
{
	struct jme_softc *sc;
	struct ifnet *ifp;
	uint32_t status;
	int more;

	sc = (struct jme_softc *)arg;
	ifp = sc->jme_ifp;

	JME_LOCK(sc);
	status = CSR_READ_4(sc, JME_INTR_STATUS);
	if (sc->jme_morework != 0) {
		sc->jme_morework = 0;
		status |= INTR_RXQ_COAL | INTR_RXQ_COAL_TO;
	}
	if ((status & JME_INTRS) == 0 || status == 0xFFFFFFFF)
		goto done;
	/* Reset PCC counter/timer and Ack interrupts. */
	status &= ~(INTR_TXQ_COMP | INTR_RXQ_COMP);
	if ((status & (INTR_TXQ_COAL | INTR_TXQ_COAL_TO)) != 0)
		status |= INTR_TXQ_COAL | INTR_TXQ_COAL_TO | INTR_TXQ_COMP;
	if ((status & (INTR_RXQ_COAL | INTR_RXQ_COAL_TO)) != 0)
		status |= INTR_RXQ_COAL | INTR_RXQ_COAL_TO | INTR_RXQ_COMP;
	CSR_WRITE_4(sc, JME_INTR_STATUS, status);
	more = 0;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
		if ((status & (INTR_RXQ_COAL | INTR_RXQ_COAL_TO)) != 0) {
			more = jme_rxintr(sc, sc->jme_process_limit);
			if (more != 0)
				sc->jme_morework = 1;
		}
		if ((status & INTR_RXQ_DESC_EMPTY) != 0) {
			/*
			 * Notify hardware availability of new Rx
			 * buffers.
			 * Reading RXCSR takes very long time under
			 * heavy load so cache RXCSR value and writes
			 * the ORed value with the kick command to
			 * the RXCSR. This saves one register access
			 * cycle.
			 */
			CSR_WRITE_4(sc, JME_RXCSR, sc->jme_rxcsr |
			    RXCSR_RX_ENB | RXCSR_RXQ_START);
		}
		if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			jme_start_locked(ifp);
	}

	if (more != 0 || (CSR_READ_4(sc, JME_INTR_STATUS) & JME_INTRS) != 0) {
		taskqueue_enqueue(sc->jme_tq, &sc->jme_int_task);
		JME_UNLOCK(sc);
		return;
	}
done:
	JME_UNLOCK(sc);

	/* Reenable interrupts. */
	CSR_WRITE_4(sc, JME_INTR_MASK_SET, JME_INTRS);
}

static void
jme_txeof(struct jme_softc *sc)
{
	struct ifnet *ifp;
	struct jme_txdesc *txd;
	uint32_t status;
	int cons, nsegs;

	JME_LOCK_ASSERT(sc);

	ifp = sc->jme_ifp;

	cons = sc->jme_cdata.jme_tx_cons;
	if (cons == sc->jme_cdata.jme_tx_prod)
		return;

	bus_dmamap_sync(sc->jme_cdata.jme_tx_ring_tag,
	    sc->jme_cdata.jme_tx_ring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	/*
	 * Go through our Tx list and free mbufs for those
	 * frames which have been transmitted.
	 */
	for (; cons != sc->jme_cdata.jme_tx_prod;) {
		txd = &sc->jme_cdata.jme_txdesc[cons];
		status = le32toh(txd->tx_desc->flags);
		if ((status & JME_TD_OWN) == JME_TD_OWN)
			break;

		if ((status & (JME_TD_TMOUT | JME_TD_RETRY_EXP)) != 0)
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		else {
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
			if ((status & JME_TD_COLLISION) != 0)
				if_inc_counter(ifp, IFCOUNTER_COLLISIONS,
				    le32toh(txd->tx_desc->buflen) &
				    JME_TD_BUF_LEN_MASK);
		}
		/*
		 * Only the first descriptor of multi-descriptor
		 * transmission is updated so driver have to skip entire
		 * chained buffers for the transmiited frame. In other
		 * words, JME_TD_OWN bit is valid only at the first
		 * descriptor of a multi-descriptor transmission.
		 */
		for (nsegs = 0; nsegs < txd->tx_ndesc; nsegs++) {
			sc->jme_rdata.jme_tx_ring[cons].flags = 0;
			JME_DESC_INC(cons, JME_TX_RING_CNT);
		}

		/* Reclaim transferred mbufs. */
		bus_dmamap_sync(sc->jme_cdata.jme_tx_tag, txd->tx_dmamap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->jme_cdata.jme_tx_tag, txd->tx_dmamap);

		KASSERT(txd->tx_m != NULL,
		    ("%s: freeing NULL mbuf!\n", __func__));
		m_freem(txd->tx_m);
		txd->tx_m = NULL;
		sc->jme_cdata.jme_tx_cnt -= txd->tx_ndesc;
		KASSERT(sc->jme_cdata.jme_tx_cnt >= 0,
		    ("%s: Active Tx desc counter was garbled\n", __func__));
		txd->tx_ndesc = 0;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	}
	sc->jme_cdata.jme_tx_cons = cons;
	/* Unarm watchog timer when there is no pending descriptors in queue. */
	if (sc->jme_cdata.jme_tx_cnt == 0)
		sc->jme_watchdog_timer = 0;

	bus_dmamap_sync(sc->jme_cdata.jme_tx_ring_tag,
	    sc->jme_cdata.jme_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static __inline void
jme_discard_rxbuf(struct jme_softc *sc, int cons)
{
	struct jme_desc *desc;

	desc = &sc->jme_rdata.jme_rx_ring[cons];
	desc->flags = htole32(JME_RD_OWN | JME_RD_INTR | JME_RD_64BIT);
	desc->buflen = htole32(MCLBYTES);
}

/* Receive a frame. */
static void
jme_rxeof(struct jme_softc *sc)
{
	struct ifnet *ifp;
	struct jme_desc *desc;
	struct jme_rxdesc *rxd;
	struct mbuf *mp, *m;
	uint32_t flags, status;
	int cons, count, nsegs;

	JME_LOCK_ASSERT(sc);

	ifp = sc->jme_ifp;

	cons = sc->jme_cdata.jme_rx_cons;
	desc = &sc->jme_rdata.jme_rx_ring[cons];
	flags = le32toh(desc->flags);
	status = le32toh(desc->buflen);
	nsegs = JME_RX_NSEGS(status);
	sc->jme_cdata.jme_rxlen = JME_RX_BYTES(status) - JME_RX_PAD_BYTES;
	if ((status & JME_RX_ERR_STAT) != 0) {
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		jme_discard_rxbuf(sc, sc->jme_cdata.jme_rx_cons);
#ifdef JME_SHOW_ERRORS
		device_printf(sc->jme_dev, "%s : receive error = 0x%b\n",
		    __func__, JME_RX_ERR(status), JME_RX_ERR_BITS);
#endif
		sc->jme_cdata.jme_rx_cons += nsegs;
		sc->jme_cdata.jme_rx_cons %= JME_RX_RING_CNT;
		return;
	}

	for (count = 0; count < nsegs; count++,
	    JME_DESC_INC(cons, JME_RX_RING_CNT)) {
		rxd = &sc->jme_cdata.jme_rxdesc[cons];
		mp = rxd->rx_m;
		/* Add a new receive buffer to the ring. */
		if (jme_newbuf(sc, rxd) != 0) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			/* Reuse buffer. */
			for (; count < nsegs; count++) {
				jme_discard_rxbuf(sc, cons);
				JME_DESC_INC(cons, JME_RX_RING_CNT);
			}
			if (sc->jme_cdata.jme_rxhead != NULL) {
				m_freem(sc->jme_cdata.jme_rxhead);
				JME_RXCHAIN_RESET(sc);
			}
			break;
		}

		/*
		 * Assume we've received a full sized frame.
		 * Actual size is fixed when we encounter the end of
		 * multi-segmented frame.
		 */
		mp->m_len = MCLBYTES;

		/* Chain received mbufs. */
		if (sc->jme_cdata.jme_rxhead == NULL) {
			sc->jme_cdata.jme_rxhead = mp;
			sc->jme_cdata.jme_rxtail = mp;
		} else {
			/*
			 * Receive processor can receive a maximum frame
			 * size of 65535 bytes.
			 */
			mp->m_flags &= ~M_PKTHDR;
			sc->jme_cdata.jme_rxtail->m_next = mp;
			sc->jme_cdata.jme_rxtail = mp;
		}

		if (count == nsegs - 1) {
			/* Last desc. for this frame. */
			m = sc->jme_cdata.jme_rxhead;
			m->m_flags |= M_PKTHDR;
			m->m_pkthdr.len = sc->jme_cdata.jme_rxlen;
			if (nsegs > 1) {
				/* Set first mbuf size. */
				m->m_len = MCLBYTES - JME_RX_PAD_BYTES;
				/* Set last mbuf size. */
				mp->m_len = sc->jme_cdata.jme_rxlen -
				    ((MCLBYTES - JME_RX_PAD_BYTES) +
				    (MCLBYTES * (nsegs - 2)));
			} else
				m->m_len = sc->jme_cdata.jme_rxlen;
			m->m_pkthdr.rcvif = ifp;

			/*
			 * Account for 10bytes auto padding which is used
			 * to align IP header on 32bit boundary. Also note,
			 * CRC bytes is automatically removed by the
			 * hardware.
			 */
			m->m_data += JME_RX_PAD_BYTES;

			/* Set checksum information. */
			if ((ifp->if_capenable & IFCAP_RXCSUM) != 0 &&
			    (flags & JME_RD_IPV4) != 0) {
				m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
				if ((flags & JME_RD_IPCSUM) != 0)
					m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
				if (((flags & JME_RD_MORE_FRAG) == 0) &&
				    ((flags & (JME_RD_TCP | JME_RD_TCPCSUM)) ==
				    (JME_RD_TCP | JME_RD_TCPCSUM) ||
				    (flags & (JME_RD_UDP | JME_RD_UDPCSUM)) ==
				    (JME_RD_UDP | JME_RD_UDPCSUM))) {
					m->m_pkthdr.csum_flags |=
					    CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
					m->m_pkthdr.csum_data = 0xffff;
				}
			}

			/* Check for VLAN tagged packets. */
			if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0 &&
			    (flags & JME_RD_VLAN_TAG) != 0) {
				m->m_pkthdr.ether_vtag =
				    flags & JME_RD_VLAN_MASK;
				m->m_flags |= M_VLANTAG;
			}

			if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
			/* Pass it on. */
			JME_UNLOCK(sc);
			(*ifp->if_input)(ifp, m);
			JME_LOCK(sc);

			/* Reset mbuf chains. */
			JME_RXCHAIN_RESET(sc);
		}
	}

	sc->jme_cdata.jme_rx_cons += nsegs;
	sc->jme_cdata.jme_rx_cons %= JME_RX_RING_CNT;
}

static int
jme_rxintr(struct jme_softc *sc, int count)
{
	struct jme_desc *desc;
	int nsegs, prog, pktlen;

	bus_dmamap_sync(sc->jme_cdata.jme_rx_ring_tag,
	    sc->jme_cdata.jme_rx_ring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (prog = 0; count > 0; prog++) {
		desc = &sc->jme_rdata.jme_rx_ring[sc->jme_cdata.jme_rx_cons];
		if ((le32toh(desc->flags) & JME_RD_OWN) == JME_RD_OWN)
			break;
		if ((le32toh(desc->buflen) & JME_RD_VALID) == 0)
			break;
		nsegs = JME_RX_NSEGS(le32toh(desc->buflen));
		/*
		 * Check number of segments against received bytes.
		 * Non-matching value would indicate that hardware
		 * is still trying to update Rx descriptors. I'm not
		 * sure whether this check is needed.
		 */
		pktlen = JME_RX_BYTES(le32toh(desc->buflen));
		if (nsegs != howmany(pktlen, MCLBYTES))
			break;
		prog++;
		/* Received a frame. */
		jme_rxeof(sc);
		count -= nsegs;
	}

	if (prog > 0)
		bus_dmamap_sync(sc->jme_cdata.jme_rx_ring_tag,
		    sc->jme_cdata.jme_rx_ring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (count > 0 ? 0 : EAGAIN);
}

static void
jme_tick(void *arg)
{
	struct jme_softc *sc;
	struct mii_data *mii;

	sc = (struct jme_softc *)arg;

	JME_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->jme_miibus);
	mii_tick(mii);
	/*
	 * Reclaim Tx buffers that have been completed. It's not
	 * needed here but it would release allocated mbuf chains
	 * faster and limit the maximum delay to a hz.
	 */
	jme_txeof(sc);
	jme_stats_update(sc);
	jme_watchdog(sc);
	callout_reset(&sc->jme_tick_ch, hz, jme_tick, sc);
}

static void
jme_reset(struct jme_softc *sc)
{
	uint32_t ghc, gpreg;

	/* Stop receiver, transmitter. */
	jme_stop_rx(sc);
	jme_stop_tx(sc);

	/* Reset controller. */
	CSR_WRITE_4(sc, JME_GHC, GHC_RESET);
	CSR_READ_4(sc, JME_GHC);
	DELAY(10);
	/*
	 * Workaround Rx FIFO overruns seen under certain conditions.
	 * Explicitly synchorize TX/RX clock.  TX/RX clock should be
	 * enabled only after enabling TX/RX MACs.
	 */
	if ((sc->jme_flags & (JME_FLAG_TXCLK | JME_FLAG_RXCLK)) != 0) {
		/* Disable TX clock. */
		CSR_WRITE_4(sc, JME_GHC, GHC_RESET | GHC_TX_MAC_CLK_DIS);
		/* Disable RX clock. */
		gpreg = CSR_READ_4(sc, JME_GPREG1);
		CSR_WRITE_4(sc, JME_GPREG1, gpreg | GPREG1_RX_MAC_CLK_DIS);
		gpreg = CSR_READ_4(sc, JME_GPREG1);
		/* De-assert RESET but still disable TX clock. */
		CSR_WRITE_4(sc, JME_GHC, GHC_TX_MAC_CLK_DIS);
		ghc = CSR_READ_4(sc, JME_GHC);

		/* Enable TX clock. */
		CSR_WRITE_4(sc, JME_GHC, ghc & ~GHC_TX_MAC_CLK_DIS);
		/* Enable RX clock. */
		CSR_WRITE_4(sc, JME_GPREG1, gpreg & ~GPREG1_RX_MAC_CLK_DIS);
		CSR_READ_4(sc, JME_GPREG1);

		/* Disable TX/RX clock again. */
		CSR_WRITE_4(sc, JME_GHC, GHC_TX_MAC_CLK_DIS);
		CSR_WRITE_4(sc, JME_GPREG1, gpreg | GPREG1_RX_MAC_CLK_DIS);
	} else
		CSR_WRITE_4(sc, JME_GHC, 0);
	CSR_READ_4(sc, JME_GHC);
	DELAY(10);
}

static void
jme_init(void *xsc)
{
	struct jme_softc *sc;

	sc = (struct jme_softc *)xsc;
	JME_LOCK(sc);
	jme_init_locked(sc);
	JME_UNLOCK(sc);
}

static void
jme_init_locked(struct jme_softc *sc)
{
	struct ifnet *ifp;
	struct mii_data *mii;
	bus_addr_t paddr;
	uint32_t reg;
	int error;

	JME_LOCK_ASSERT(sc);

	ifp = sc->jme_ifp;
	mii = device_get_softc(sc->jme_miibus);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;
	/*
	 * Cancel any pending I/O.
	 */
	jme_stop(sc);

	/*
	 * Reset the chip to a known state.
	 */
	jme_reset(sc);

	/* Init descriptors. */
	error = jme_init_rx_ring(sc);
        if (error != 0) {
                device_printf(sc->jme_dev,
                    "%s: initialization failed: no memory for Rx buffers.\n",
		    __func__);
                jme_stop(sc);
		return;
        }
	jme_init_tx_ring(sc);
	/* Initialize shadow status block. */
	jme_init_ssb(sc);

	/* Reprogram the station address. */
	jme_set_macaddr(sc, IF_LLADDR(sc->jme_ifp));

	/*
	 * Configure Tx queue.
	 *  Tx priority queue weight value : 0
	 *  Tx FIFO threshold for processing next packet : 16QW
	 *  Maximum Tx DMA length : 512
	 *  Allow Tx DMA burst.
	 */
	sc->jme_txcsr = TXCSR_TXQ_N_SEL(TXCSR_TXQ0);
	sc->jme_txcsr |= TXCSR_TXQ_WEIGHT(TXCSR_TXQ_WEIGHT_MIN);
	sc->jme_txcsr |= TXCSR_FIFO_THRESH_16QW;
	sc->jme_txcsr |= sc->jme_tx_dma_size;
	sc->jme_txcsr |= TXCSR_DMA_BURST;
	CSR_WRITE_4(sc, JME_TXCSR, sc->jme_txcsr);

	/* Set Tx descriptor counter. */
	CSR_WRITE_4(sc, JME_TXQDC, JME_TX_RING_CNT);

	/* Set Tx ring address to the hardware. */
	paddr = JME_TX_RING_ADDR(sc, 0);
	CSR_WRITE_4(sc, JME_TXDBA_HI, JME_ADDR_HI(paddr));
	CSR_WRITE_4(sc, JME_TXDBA_LO, JME_ADDR_LO(paddr));

	/* Configure TxMAC parameters. */
	reg = TXMAC_IFG1_DEFAULT | TXMAC_IFG2_DEFAULT | TXMAC_IFG_ENB;
	reg |= TXMAC_THRESH_1_PKT;
	reg |= TXMAC_CRC_ENB | TXMAC_PAD_ENB;
	CSR_WRITE_4(sc, JME_TXMAC, reg);

	/*
	 * Configure Rx queue.
	 *  FIFO full threshold for transmitting Tx pause packet : 128T
	 *  FIFO threshold for processing next packet : 128QW
	 *  Rx queue 0 select
	 *  Max Rx DMA length : 128
	 *  Rx descriptor retry : 32
	 *  Rx descriptor retry time gap : 256ns
	 *  Don't receive runt/bad frame.
	 */
	sc->jme_rxcsr = RXCSR_FIFO_FTHRESH_128T;
	/*
	 * Since Rx FIFO size is 4K bytes, receiving frames larger
	 * than 4K bytes will suffer from Rx FIFO overruns. So
	 * decrease FIFO threshold to reduce the FIFO overruns for
	 * frames larger than 4000 bytes.
	 * For best performance of standard MTU sized frames use
	 * maximum allowable FIFO threshold, 128QW. Note these do
	 * not hold on chip full mask verion >=2. For these
	 * controllers 64QW and 128QW are not valid value.
	 */
	if (CHIPMODE_REVFM(sc->jme_chip_rev) >= 2)
		sc->jme_rxcsr |= RXCSR_FIFO_THRESH_16QW;
	else {
		if ((ifp->if_mtu + ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN +
		    ETHER_CRC_LEN) > JME_RX_FIFO_SIZE)
			sc->jme_rxcsr |= RXCSR_FIFO_THRESH_16QW;
		else
			sc->jme_rxcsr |= RXCSR_FIFO_THRESH_128QW;
	}
	sc->jme_rxcsr |= sc->jme_rx_dma_size | RXCSR_RXQ_N_SEL(RXCSR_RXQ0);
	sc->jme_rxcsr |= RXCSR_DESC_RT_CNT(RXCSR_DESC_RT_CNT_DEFAULT);
	sc->jme_rxcsr |= RXCSR_DESC_RT_GAP_256 & RXCSR_DESC_RT_GAP_MASK;
	CSR_WRITE_4(sc, JME_RXCSR, sc->jme_rxcsr);

	/* Set Rx descriptor counter. */
	CSR_WRITE_4(sc, JME_RXQDC, JME_RX_RING_CNT);

	/* Set Rx ring address to the hardware. */
	paddr = JME_RX_RING_ADDR(sc, 0);
	CSR_WRITE_4(sc, JME_RXDBA_HI, JME_ADDR_HI(paddr));
	CSR_WRITE_4(sc, JME_RXDBA_LO, JME_ADDR_LO(paddr));

	/* Clear receive filter. */
	CSR_WRITE_4(sc, JME_RXMAC, 0);
	/* Set up the receive filter. */
	jme_set_filter(sc);
	jme_set_vlan(sc);

	/*
	 * Disable all WOL bits as WOL can interfere normal Rx
	 * operation. Also clear WOL detection status bits.
	 */
	reg = CSR_READ_4(sc, JME_PMCS);
	reg &= ~PMCS_WOL_ENB_MASK;
	CSR_WRITE_4(sc, JME_PMCS, reg);

	reg = CSR_READ_4(sc, JME_RXMAC);
	/*
	 * Pad 10bytes right before received frame. This will greatly
	 * help Rx performance on strict-alignment architectures as
	 * it does not need to copy the frame to align the payload.
	 */
	reg |= RXMAC_PAD_10BYTES;
	if ((ifp->if_capenable & IFCAP_RXCSUM) != 0)
		reg |= RXMAC_CSUM_ENB;
	CSR_WRITE_4(sc, JME_RXMAC, reg);

	/* Configure general purpose reg0 */
	reg = CSR_READ_4(sc, JME_GPREG0);
	reg &= ~GPREG0_PCC_UNIT_MASK;
	/* Set PCC timer resolution to micro-seconds unit. */
	reg |= GPREG0_PCC_UNIT_US;
	/*
	 * Disable all shadow register posting as we have to read
	 * JME_INTR_STATUS register in jme_int_task. Also it seems
	 * that it's hard to synchronize interrupt status between
	 * hardware and software with shadow posting due to
	 * requirements of bus_dmamap_sync(9).
	 */
	reg |= GPREG0_SH_POST_DW7_DIS | GPREG0_SH_POST_DW6_DIS |
	    GPREG0_SH_POST_DW5_DIS | GPREG0_SH_POST_DW4_DIS |
	    GPREG0_SH_POST_DW3_DIS | GPREG0_SH_POST_DW2_DIS |
	    GPREG0_SH_POST_DW1_DIS | GPREG0_SH_POST_DW0_DIS;
	/* Disable posting of DW0. */
	reg &= ~GPREG0_POST_DW0_ENB;
	/* Clear PME message. */
	reg &= ~GPREG0_PME_ENB;
	/* Set PHY address. */
	reg &= ~GPREG0_PHY_ADDR_MASK;
	reg |= sc->jme_phyaddr;
	CSR_WRITE_4(sc, JME_GPREG0, reg);

	/* Configure Tx queue 0 packet completion coalescing. */
	reg = (sc->jme_tx_coal_to << PCCTX_COAL_TO_SHIFT) &
	    PCCTX_COAL_TO_MASK;
	reg |= (sc->jme_tx_coal_pkt << PCCTX_COAL_PKT_SHIFT) &
	    PCCTX_COAL_PKT_MASK;
	reg |= PCCTX_COAL_TXQ0;
	CSR_WRITE_4(sc, JME_PCCTX, reg);

	/* Configure Rx queue 0 packet completion coalescing. */
	reg = (sc->jme_rx_coal_to << PCCRX_COAL_TO_SHIFT) &
	    PCCRX_COAL_TO_MASK;
	reg |= (sc->jme_rx_coal_pkt << PCCRX_COAL_PKT_SHIFT) &
	    PCCRX_COAL_PKT_MASK;
	CSR_WRITE_4(sc, JME_PCCRX0, reg);

	/*
	 * Configure PCD(Packet Completion Deferring).  It seems PCD
	 * generates an interrupt when the time interval between two
	 * back-to-back incoming/outgoing packet is long enough for
	 * it to reach its timer value 0. The arrival of new packets
	 * after timer has started causes the PCD timer to restart.
	 * Unfortunately, it's not clear how PCD is useful at this
	 * moment, so just use the same of PCC parameters.
	 */
	if ((sc->jme_flags & JME_FLAG_PCCPCD) != 0) {
		sc->jme_rx_pcd_to = sc->jme_rx_coal_to;
		if (sc->jme_rx_coal_to > PCDRX_TO_MAX)
			sc->jme_rx_pcd_to = PCDRX_TO_MAX;
		sc->jme_tx_pcd_to = sc->jme_tx_coal_to;
		if (sc->jme_tx_coal_to > PCDTX_TO_MAX)
			sc->jme_tx_pcd_to = PCDTX_TO_MAX;
		reg = sc->jme_rx_pcd_to << PCDRX0_TO_THROTTLE_SHIFT;
		reg |= sc->jme_rx_pcd_to << PCDRX0_TO_SHIFT;
		CSR_WRITE_4(sc, PCDRX_REG(0), reg);
		reg = sc->jme_tx_pcd_to << PCDTX_TO_THROTTLE_SHIFT;
		reg |= sc->jme_tx_pcd_to << PCDTX_TO_SHIFT;
		CSR_WRITE_4(sc, JME_PCDTX, reg);
	}

	/* Configure shadow status block but don't enable posting. */
	paddr = sc->jme_rdata.jme_ssb_block_paddr;
	CSR_WRITE_4(sc, JME_SHBASE_ADDR_HI, JME_ADDR_HI(paddr));
	CSR_WRITE_4(sc, JME_SHBASE_ADDR_LO, JME_ADDR_LO(paddr));

	/* Disable Timer 1 and Timer 2. */
	CSR_WRITE_4(sc, JME_TIMER1, 0);
	CSR_WRITE_4(sc, JME_TIMER2, 0);

	/* Configure retry transmit period, retry limit value. */
	CSR_WRITE_4(sc, JME_TXTRHD,
	    ((TXTRHD_RT_PERIOD_DEFAULT << TXTRHD_RT_PERIOD_SHIFT) &
	    TXTRHD_RT_PERIOD_MASK) |
	    ((TXTRHD_RT_LIMIT_DEFAULT << TXTRHD_RT_LIMIT_SHIFT) &
	    TXTRHD_RT_LIMIT_SHIFT));

	/* Disable RSS. */
	CSR_WRITE_4(sc, JME_RSSC, RSSC_DIS_RSS);

	/* Initialize the interrupt mask. */
	CSR_WRITE_4(sc, JME_INTR_MASK_SET, JME_INTRS);
	CSR_WRITE_4(sc, JME_INTR_STATUS, 0xFFFFFFFF);

	/*
	 * Enabling Tx/Rx DMA engines and Rx queue processing is
	 * done after detection of valid link in jme_link_task.
	 */

	sc->jme_flags &= ~JME_FLAG_LINK;
	/* Set the current media. */
	mii_mediachg(mii);

	callout_reset(&sc->jme_tick_ch, hz, jme_tick, sc);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
}

static void
jme_stop(struct jme_softc *sc)
{
	struct ifnet *ifp;
	struct jme_txdesc *txd;
	struct jme_rxdesc *rxd;
	int i;

	JME_LOCK_ASSERT(sc);
	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp = sc->jme_ifp;
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->jme_flags &= ~JME_FLAG_LINK;
	callout_stop(&sc->jme_tick_ch);
	sc->jme_watchdog_timer = 0;

	/*
	 * Disable interrupts.
	 */
	CSR_WRITE_4(sc, JME_INTR_MASK_CLR, JME_INTRS);
	CSR_WRITE_4(sc, JME_INTR_STATUS, 0xFFFFFFFF);

	/* Disable updating shadow status block. */
	CSR_WRITE_4(sc, JME_SHBASE_ADDR_LO,
	    CSR_READ_4(sc, JME_SHBASE_ADDR_LO) & ~SHBASE_POST_ENB);

	/* Stop receiver, transmitter. */
	jme_stop_rx(sc);
	jme_stop_tx(sc);

	 /* Reclaim Rx/Tx buffers that have been completed. */
	jme_rxintr(sc, JME_RX_RING_CNT);
	if (sc->jme_cdata.jme_rxhead != NULL)
		m_freem(sc->jme_cdata.jme_rxhead);
	JME_RXCHAIN_RESET(sc);
	jme_txeof(sc);
	/*
	 * Free RX and TX mbufs still in the queues.
	 */
	for (i = 0; i < JME_RX_RING_CNT; i++) {
		rxd = &sc->jme_cdata.jme_rxdesc[i];
		if (rxd->rx_m != NULL) {
			bus_dmamap_sync(sc->jme_cdata.jme_rx_tag,
			    rxd->rx_dmamap, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->jme_cdata.jme_rx_tag,
			    rxd->rx_dmamap);
			m_freem(rxd->rx_m);
			rxd->rx_m = NULL;
		}
        }
	for (i = 0; i < JME_TX_RING_CNT; i++) {
		txd = &sc->jme_cdata.jme_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_sync(sc->jme_cdata.jme_tx_tag,
			    txd->tx_dmamap, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->jme_cdata.jme_tx_tag,
			    txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
			txd->tx_ndesc = 0;
		}
        }
	jme_stats_update(sc);
	jme_stats_save(sc);
}

static void
jme_stop_tx(struct jme_softc *sc)
{
	uint32_t reg;
	int i;

	reg = CSR_READ_4(sc, JME_TXCSR);
	if ((reg & TXCSR_TX_ENB) == 0)
		return;
	reg &= ~TXCSR_TX_ENB;
	CSR_WRITE_4(sc, JME_TXCSR, reg);
	for (i = JME_TIMEOUT; i > 0; i--) {
		DELAY(1);
		if ((CSR_READ_4(sc, JME_TXCSR) & TXCSR_TX_ENB) == 0)
			break;
	}
	if (i == 0)
		device_printf(sc->jme_dev, "stopping transmitter timeout!\n");
}

static void
jme_stop_rx(struct jme_softc *sc)
{
	uint32_t reg;
	int i;

	reg = CSR_READ_4(sc, JME_RXCSR);
	if ((reg & RXCSR_RX_ENB) == 0)
		return;
	reg &= ~RXCSR_RX_ENB;
	CSR_WRITE_4(sc, JME_RXCSR, reg);
	for (i = JME_TIMEOUT; i > 0; i--) {
		DELAY(1);
		if ((CSR_READ_4(sc, JME_RXCSR) & RXCSR_RX_ENB) == 0)
			break;
	}
	if (i == 0)
		device_printf(sc->jme_dev, "stopping recevier timeout!\n");
}

static void
jme_init_tx_ring(struct jme_softc *sc)
{
	struct jme_ring_data *rd;
	struct jme_txdesc *txd;
	int i;

	sc->jme_cdata.jme_tx_prod = 0;
	sc->jme_cdata.jme_tx_cons = 0;
	sc->jme_cdata.jme_tx_cnt = 0;

	rd = &sc->jme_rdata;
	bzero(rd->jme_tx_ring, JME_TX_RING_SIZE);
	for (i = 0; i < JME_TX_RING_CNT; i++) {
		txd = &sc->jme_cdata.jme_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_desc = &rd->jme_tx_ring[i];
		txd->tx_ndesc = 0;
	}

	bus_dmamap_sync(sc->jme_cdata.jme_tx_ring_tag,
	    sc->jme_cdata.jme_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static void
jme_init_ssb(struct jme_softc *sc)
{
	struct jme_ring_data *rd;

	rd = &sc->jme_rdata;
	bzero(rd->jme_ssb_block, JME_SSB_SIZE);
	bus_dmamap_sync(sc->jme_cdata.jme_ssb_tag, sc->jme_cdata.jme_ssb_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static int
jme_init_rx_ring(struct jme_softc *sc)
{
	struct jme_ring_data *rd;
	struct jme_rxdesc *rxd;
	int i;

	sc->jme_cdata.jme_rx_cons = 0;
	JME_RXCHAIN_RESET(sc);
	sc->jme_morework = 0;

	rd = &sc->jme_rdata;
	bzero(rd->jme_rx_ring, JME_RX_RING_SIZE);
	for (i = 0; i < JME_RX_RING_CNT; i++) {
		rxd = &sc->jme_cdata.jme_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_desc = &rd->jme_rx_ring[i];
		if (jme_newbuf(sc, rxd) != 0)
			return (ENOBUFS);
	}

	bus_dmamap_sync(sc->jme_cdata.jme_rx_ring_tag,
	    sc->jme_cdata.jme_rx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

static int
jme_newbuf(struct jme_softc *sc, struct jme_rxdesc *rxd)
{
	struct jme_desc *desc;
	struct mbuf *m;
	bus_dma_segment_t segs[1];
	bus_dmamap_t map;
	int nsegs;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	/*
	 * JMC250 has 64bit boundary alignment limitation so jme(4)
	 * takes advantage of 10 bytes padding feature of hardware
	 * in order not to copy entire frame to align IP header on
	 * 32bit boundary.
	 */
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	if (bus_dmamap_load_mbuf_sg(sc->jme_cdata.jme_rx_tag,
	    sc->jme_cdata.jme_rx_sparemap, m, segs, &nsegs, 0) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(sc->jme_cdata.jme_rx_tag, rxd->rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->jme_cdata.jme_rx_tag, rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc->jme_cdata.jme_rx_sparemap;
	sc->jme_cdata.jme_rx_sparemap = map;
	bus_dmamap_sync(sc->jme_cdata.jme_rx_tag, rxd->rx_dmamap,
	    BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;

	desc = rxd->rx_desc;
	desc->buflen = htole32(segs[0].ds_len);
	desc->addr_lo = htole32(JME_ADDR_LO(segs[0].ds_addr));
	desc->addr_hi = htole32(JME_ADDR_HI(segs[0].ds_addr));
	desc->flags = htole32(JME_RD_OWN | JME_RD_INTR | JME_RD_64BIT);

	return (0);
}

static void
jme_set_vlan(struct jme_softc *sc)
{
	struct ifnet *ifp;
	uint32_t reg;

	JME_LOCK_ASSERT(sc);

	ifp = sc->jme_ifp;
	reg = CSR_READ_4(sc, JME_RXMAC);
	reg &= ~RXMAC_VLAN_ENB;
	if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0)
		reg |= RXMAC_VLAN_ENB;
	CSR_WRITE_4(sc, JME_RXMAC, reg);
}

static void
jme_set_filter(struct jme_softc *sc)
{
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	uint32_t crc;
	uint32_t mchash[2];
	uint32_t rxcfg;

	JME_LOCK_ASSERT(sc);

	ifp = sc->jme_ifp;

	rxcfg = CSR_READ_4(sc, JME_RXMAC);
	rxcfg &= ~ (RXMAC_BROADCAST | RXMAC_PROMISC | RXMAC_MULTICAST |
	    RXMAC_ALLMULTI);
	/* Always accept frames destined to our station address. */
	rxcfg |= RXMAC_UNICAST;
	if ((ifp->if_flags & IFF_BROADCAST) != 0)
		rxcfg |= RXMAC_BROADCAST;
	if ((ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI)) != 0) {
		if ((ifp->if_flags & IFF_PROMISC) != 0)
			rxcfg |= RXMAC_PROMISC;
		if ((ifp->if_flags & IFF_ALLMULTI) != 0)
			rxcfg |= RXMAC_ALLMULTI;
		CSR_WRITE_4(sc, JME_MAR0, 0xFFFFFFFF);
		CSR_WRITE_4(sc, JME_MAR1, 0xFFFFFFFF);
		CSR_WRITE_4(sc, JME_RXMAC, rxcfg);
		return;
	}

	/*
	 * Set up the multicast address filter by passing all multicast
	 * addresses through a CRC generator, and then using the low-order
	 * 6 bits as an index into the 64 bit multicast hash table.  The
	 * high order bits select the register, while the rest of the bits
	 * select the bit within the register.
	 */
	rxcfg |= RXMAC_MULTICAST;
	bzero(mchash, sizeof(mchash));

	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &sc->jme_ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		crc = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN);

		/* Just want the 6 least significant bits. */
		crc &= 0x3f;

		/* Set the corresponding bit in the hash table. */
		mchash[crc >> 5] |= 1 << (crc & 0x1f);
	}
	if_maddr_runlock(ifp);

	CSR_WRITE_4(sc, JME_MAR0, mchash[0]);
	CSR_WRITE_4(sc, JME_MAR1, mchash[1]);
	CSR_WRITE_4(sc, JME_RXMAC, rxcfg);
}

static void
jme_stats_clear(struct jme_softc *sc)
{

	JME_LOCK_ASSERT(sc);

	if ((sc->jme_flags & JME_FLAG_HWMIB) == 0)
		return;

	/* Disable and clear counters. */
	CSR_WRITE_4(sc, JME_STATCSR, 0xFFFFFFFF);
	/* Activate hw counters. */
	CSR_WRITE_4(sc, JME_STATCSR, 0);
	CSR_READ_4(sc, JME_STATCSR);
	bzero(&sc->jme_stats, sizeof(struct jme_hw_stats));
}

static void
jme_stats_save(struct jme_softc *sc)
{

	JME_LOCK_ASSERT(sc);

	if ((sc->jme_flags & JME_FLAG_HWMIB) == 0)
		return;
	/* Save current counters. */
	bcopy(&sc->jme_stats, &sc->jme_ostats, sizeof(struct jme_hw_stats));
	/* Disable and clear counters. */
	CSR_WRITE_4(sc, JME_STATCSR, 0xFFFFFFFF);
}

static void
jme_stats_update(struct jme_softc *sc)
{
	struct jme_hw_stats *stat, *ostat;
	uint32_t reg;

	JME_LOCK_ASSERT(sc);

	if ((sc->jme_flags & JME_FLAG_HWMIB) == 0)
		return;
	stat = &sc->jme_stats;
	ostat = &sc->jme_ostats;
	stat->tx_good_frames = CSR_READ_4(sc, JME_STAT_TXGOOD);
	stat->rx_good_frames = CSR_READ_4(sc, JME_STAT_RXGOOD);
	reg = CSR_READ_4(sc, JME_STAT_CRCMII);
	stat->rx_crc_errs = (reg & STAT_RX_CRC_ERR_MASK) >>
	    STAT_RX_CRC_ERR_SHIFT;
	stat->rx_mii_errs = (reg & STAT_RX_MII_ERR_MASK) >>
	    STAT_RX_MII_ERR_SHIFT;
	reg = CSR_READ_4(sc, JME_STAT_RXERR);
	stat->rx_fifo_oflows = (reg & STAT_RXERR_OFLOW_MASK) >>
	    STAT_RXERR_OFLOW_SHIFT;
	stat->rx_desc_empty = (reg & STAT_RXERR_MPTY_MASK) >>
	    STAT_RXERR_MPTY_SHIFT;
	reg = CSR_READ_4(sc, JME_STAT_FAIL);
	stat->rx_bad_frames = (reg & STAT_FAIL_RX_MASK) >> STAT_FAIL_RX_SHIFT;
	stat->tx_bad_frames = (reg & STAT_FAIL_TX_MASK) >> STAT_FAIL_TX_SHIFT;

	/* Account for previous counters. */
	stat->rx_good_frames += ostat->rx_good_frames;
	stat->rx_crc_errs += ostat->rx_crc_errs;
	stat->rx_mii_errs += ostat->rx_mii_errs;
	stat->rx_fifo_oflows += ostat->rx_fifo_oflows;
	stat->rx_desc_empty += ostat->rx_desc_empty;
	stat->rx_bad_frames += ostat->rx_bad_frames;
	stat->tx_good_frames += ostat->tx_good_frames;
	stat->tx_bad_frames += ostat->tx_bad_frames;
}

static void
jme_phy_down(struct jme_softc *sc)
{
	uint32_t reg;

	jme_miibus_writereg(sc->jme_dev, sc->jme_phyaddr, MII_BMCR, BMCR_PDOWN);
	if (CHIPMODE_REVFM(sc->jme_chip_rev) >= 5) {
		reg = CSR_READ_4(sc, JME_PHYPOWDN);
		reg |= 0x0000000F;
		CSR_WRITE_4(sc, JME_PHYPOWDN, reg);
		reg = pci_read_config(sc->jme_dev, JME_PCI_PE1, 4);
		reg &= ~PE1_GIGA_PDOWN_MASK;
		reg |= PE1_GIGA_PDOWN_D3;
		pci_write_config(sc->jme_dev, JME_PCI_PE1, reg, 4);
	}
}

static void
jme_phy_up(struct jme_softc *sc)
{
	uint32_t reg;
	uint16_t bmcr;

	bmcr = jme_miibus_readreg(sc->jme_dev, sc->jme_phyaddr, MII_BMCR);
	bmcr &= ~BMCR_PDOWN;
	jme_miibus_writereg(sc->jme_dev, sc->jme_phyaddr, MII_BMCR, bmcr);
	if (CHIPMODE_REVFM(sc->jme_chip_rev) >= 5) {
		reg = CSR_READ_4(sc, JME_PHYPOWDN);
		reg &= ~0x0000000F;
		CSR_WRITE_4(sc, JME_PHYPOWDN, reg);
		reg = pci_read_config(sc->jme_dev, JME_PCI_PE1, 4);
		reg &= ~PE1_GIGA_PDOWN_MASK;
		reg |= PE1_GIGA_PDOWN_DIS;
		pci_write_config(sc->jme_dev, JME_PCI_PE1, reg, 4);
	}
}

static int
sysctl_int_range(SYSCTL_HANDLER_ARGS, int low, int high)
{
	int error, value;

	if (arg1 == NULL)
		return (EINVAL);
	value = *(int *)arg1;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (value < low || value > high)
		return (EINVAL);
        *(int *)arg1 = value;

        return (0);
}

static int
sysctl_hw_jme_tx_coal_to(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_int_range(oidp, arg1, arg2, req,
	    PCCTX_COAL_TO_MIN, PCCTX_COAL_TO_MAX));
}

static int
sysctl_hw_jme_tx_coal_pkt(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_int_range(oidp, arg1, arg2, req,
	    PCCTX_COAL_PKT_MIN, PCCTX_COAL_PKT_MAX));
}

static int
sysctl_hw_jme_rx_coal_to(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_int_range(oidp, arg1, arg2, req,
	    PCCRX_COAL_TO_MIN, PCCRX_COAL_TO_MAX));
}

static int
sysctl_hw_jme_rx_coal_pkt(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_int_range(oidp, arg1, arg2, req,
	    PCCRX_COAL_PKT_MIN, PCCRX_COAL_PKT_MAX));
}

static int
sysctl_hw_jme_proc_limit(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_int_range(oidp, arg1, arg2, req,
	    JME_PROC_MIN, JME_PROC_MAX));
}
