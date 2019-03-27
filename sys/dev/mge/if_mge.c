/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2008 MARVELL INTERNATIONAL LTD.
 * Copyright (C) 2009-2015 Semihalf
 * Copyright (C) 2015 Stormshield
 * All rights reserved.
 *
 * Developed by Semihalf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of MARVELL nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_device_polling.h"
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/mbuf.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/ethernet.h>
#include <net/bpf.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <sys/sockio.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/mdio/mdio.h>

#include <dev/mge/if_mgevar.h>
#include <arm/mv/mvreg.h>
#include <arm/mv/mvvar.h>

#include "miibus_if.h"
#include "mdio_if.h"

#define	MGE_DELAY(x)	pause("SMI access sleep", (x) / tick_sbt)

static int mge_probe(device_t dev);
static int mge_attach(device_t dev);
static int mge_detach(device_t dev);
static int mge_shutdown(device_t dev);
static int mge_suspend(device_t dev);
static int mge_resume(device_t dev);

static int mge_miibus_readreg(device_t dev, int phy, int reg);
static int mge_miibus_writereg(device_t dev, int phy, int reg, int value);

static int mge_mdio_readreg(device_t dev, int phy, int reg);
static int mge_mdio_writereg(device_t dev, int phy, int reg, int value);

static int mge_ifmedia_upd(struct ifnet *ifp);
static void mge_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr);

static void mge_init(void *arg);
static void mge_init_locked(void *arg);
static void mge_start(struct ifnet *ifp);
static void mge_start_locked(struct ifnet *ifp);
static void mge_watchdog(struct mge_softc *sc);
static int mge_ioctl(struct ifnet *ifp, u_long command, caddr_t data);

static uint32_t mge_tfut_ipg(uint32_t val, int ver);
static uint32_t mge_rx_ipg(uint32_t val, int ver);
static void mge_ver_params(struct mge_softc *sc);

static void mge_intrs_ctrl(struct mge_softc *sc, int enable);
static void mge_intr_rxtx(void *arg);
static void mge_intr_rx(void *arg);
static void mge_intr_rx_check(struct mge_softc *sc, uint32_t int_cause,
    uint32_t int_cause_ext);
static int mge_intr_rx_locked(struct mge_softc *sc, int count);
static void mge_intr_tx(void *arg);
static void mge_intr_tx_locked(struct mge_softc *sc);
static void mge_intr_misc(void *arg);
static void mge_intr_sum(void *arg);
static void mge_intr_err(void *arg);
static void mge_stop(struct mge_softc *sc);
static void mge_tick(void *msc);
static uint32_t mge_set_port_serial_control(uint32_t media);
static void mge_get_mac_address(struct mge_softc *sc, uint8_t *addr);
static void mge_set_mac_address(struct mge_softc *sc);
static void mge_set_ucast_address(struct mge_softc *sc, uint8_t last_byte,
    uint8_t queue);
static void mge_set_prom_mode(struct mge_softc *sc, uint8_t queue);
static int mge_allocate_dma(struct mge_softc *sc);
static int mge_alloc_desc_dma(struct mge_softc *sc,
    struct mge_desc_wrapper* desc_tab, uint32_t size,
    bus_dma_tag_t *buffer_tag);
static int mge_new_rxbuf(bus_dma_tag_t tag, bus_dmamap_t map,
    struct mbuf **mbufp, bus_addr_t *paddr);
static void mge_get_dma_addr(void *arg, bus_dma_segment_t *segs, int nseg,
    int error);
static void mge_free_dma(struct mge_softc *sc);
static void mge_free_desc(struct mge_softc *sc, struct mge_desc_wrapper* tab,
    uint32_t size, bus_dma_tag_t buffer_tag, uint8_t free_mbufs);
static void mge_offload_process_frame(struct ifnet *ifp, struct mbuf *frame,
    uint32_t status, uint16_t bufsize);
static void mge_offload_setup_descriptor(struct mge_softc *sc,
    struct mge_desc_wrapper *dw);
static uint8_t mge_crc8(uint8_t *data, int size);
static void mge_setup_multicast(struct mge_softc *sc);
static void mge_set_rxic(struct mge_softc *sc);
static void mge_set_txic(struct mge_softc *sc);
static void mge_add_sysctls(struct mge_softc *sc);
static int mge_sysctl_ic(SYSCTL_HANDLER_ARGS);

static device_method_t mge_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mge_probe),
	DEVMETHOD(device_attach,	mge_attach),
	DEVMETHOD(device_detach,	mge_detach),
	DEVMETHOD(device_shutdown,	mge_shutdown),
	DEVMETHOD(device_suspend,	mge_suspend),
	DEVMETHOD(device_resume,	mge_resume),
	/* MII interface */
	DEVMETHOD(miibus_readreg,	mge_miibus_readreg),
	DEVMETHOD(miibus_writereg,	mge_miibus_writereg),
	/* MDIO interface */
	DEVMETHOD(mdio_readreg,		mge_mdio_readreg),
	DEVMETHOD(mdio_writereg,	mge_mdio_writereg),
	{ 0, 0 }
};

DEFINE_CLASS_0(mge, mge_driver, mge_methods, sizeof(struct mge_softc));

static devclass_t mge_devclass;
static int switch_attached = 0;

DRIVER_MODULE(mge, simplebus, mge_driver, mge_devclass, 0, 0);
DRIVER_MODULE(miibus, mge, miibus_driver, miibus_devclass, 0, 0);
DRIVER_MODULE(mdio, mge, mdio_driver, mdio_devclass, 0, 0);
MODULE_DEPEND(mge, ether, 1, 1, 1);
MODULE_DEPEND(mge, miibus, 1, 1, 1);
MODULE_DEPEND(mge, mdio, 1, 1, 1);

static struct resource_spec res_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE },
	{ SYS_RES_IRQ, 0, RF_ACTIVE | RF_SHAREABLE },
	{ SYS_RES_IRQ, 1, RF_ACTIVE | RF_SHAREABLE },
	{ SYS_RES_IRQ, 2, RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

static struct {
	driver_intr_t *handler;
	char * description;
} mge_intrs[MGE_INTR_COUNT + 1] = {
	{ mge_intr_rxtx,"GbE aggregated interrupt" },
	{ mge_intr_rx,	"GbE receive interrupt" },
	{ mge_intr_tx,	"GbE transmit interrupt" },
	{ mge_intr_misc,"GbE misc interrupt" },
	{ mge_intr_sum,	"GbE summary interrupt" },
	{ mge_intr_err,	"GbE error interrupt" },
};

/* SMI access interlock */
static struct sx sx_smi;

static uint32_t
mv_read_ge_smi(device_t dev, int phy, int reg)
{
	uint32_t timeout;
	uint32_t ret;
	struct mge_softc *sc;

	sc = device_get_softc(dev);
	KASSERT(sc != NULL, ("NULL softc ptr!"));
	timeout = MGE_SMI_WRITE_RETRIES;

	MGE_SMI_LOCK();
	while (--timeout &&
	    (MGE_READ(sc, MGE_REG_SMI) & MGE_SMI_BUSY))
		MGE_DELAY(MGE_SMI_WRITE_DELAY);

	if (timeout == 0) {
		device_printf(dev, "SMI write timeout.\n");
		ret = ~0U;
		goto out;
	}

	MGE_WRITE(sc, MGE_REG_SMI, MGE_SMI_MASK &
	    (MGE_SMI_READ | (reg << 21) | (phy << 16)));

	/* Wait till finished. */
	timeout = MGE_SMI_WRITE_RETRIES;
	while (--timeout &&
	    !((MGE_READ(sc, MGE_REG_SMI) & MGE_SMI_READVALID)))
		MGE_DELAY(MGE_SMI_WRITE_DELAY);

	if (timeout == 0) {
		device_printf(dev, "SMI write validation timeout.\n");
		ret = ~0U;
		goto out;
	}

	/* Wait for the data to update in the SMI register */
	MGE_DELAY(MGE_SMI_DELAY);
	ret = MGE_READ(sc, MGE_REG_SMI) & MGE_SMI_DATA_MASK;

out:
	MGE_SMI_UNLOCK();
	return (ret);

}

static void
mv_write_ge_smi(device_t dev, int phy, int reg, uint32_t value)
{
	uint32_t timeout;
	struct mge_softc *sc;

	sc = device_get_softc(dev);
	KASSERT(sc != NULL, ("NULL softc ptr!"));

	MGE_SMI_LOCK();
	timeout = MGE_SMI_READ_RETRIES;
	while (--timeout &&
	    (MGE_READ(sc, MGE_REG_SMI) & MGE_SMI_BUSY))
		MGE_DELAY(MGE_SMI_READ_DELAY);

	if (timeout == 0) {
		device_printf(dev, "SMI read timeout.\n");
		goto out;
	}

	MGE_WRITE(sc, MGE_REG_SMI, MGE_SMI_MASK &
	    (MGE_SMI_WRITE | (reg << 21) | (phy << 16) |
	    (value & MGE_SMI_DATA_MASK)));

out:
	MGE_SMI_UNLOCK();
}

static int
mv_read_ext_phy(device_t dev, int phy, int reg)
{
	uint32_t retries;
	struct mge_softc *sc;
	uint32_t ret;

	sc = device_get_softc(dev);

	MGE_SMI_LOCK();
	MGE_WRITE(sc->phy_sc, MGE_REG_SMI, MGE_SMI_MASK &
	    (MGE_SMI_READ | (reg << 21) | (phy << 16)));

	retries = MGE_SMI_READ_RETRIES;
	while (--retries &&
	    !(MGE_READ(sc->phy_sc, MGE_REG_SMI) & MGE_SMI_READVALID))
		DELAY(MGE_SMI_READ_DELAY);

	if (retries == 0)
		device_printf(dev, "Timeout while reading from PHY\n");

	ret = MGE_READ(sc->phy_sc, MGE_REG_SMI) & MGE_SMI_DATA_MASK;
	MGE_SMI_UNLOCK();

	return (ret);
}

static void
mv_write_ext_phy(device_t dev, int phy, int reg, int value)
{
	uint32_t retries;
	struct mge_softc *sc;

	sc = device_get_softc(dev);

	MGE_SMI_LOCK();
	MGE_WRITE(sc->phy_sc, MGE_REG_SMI, MGE_SMI_MASK &
	    (MGE_SMI_WRITE | (reg << 21) | (phy << 16) |
	    (value & MGE_SMI_DATA_MASK)));

	retries = MGE_SMI_WRITE_RETRIES;
	while (--retries && MGE_READ(sc->phy_sc, MGE_REG_SMI) & MGE_SMI_BUSY)
		DELAY(MGE_SMI_WRITE_DELAY);

	if (retries == 0)
		device_printf(dev, "Timeout while writing to PHY\n");
	MGE_SMI_UNLOCK();
}

static void
mge_get_mac_address(struct mge_softc *sc, uint8_t *addr)
{
	uint32_t mac_l, mac_h;
	uint8_t lmac[6];
	int i, valid;

	/*
	 * Retrieve hw address from the device tree.
	 */
	i = OF_getprop(sc->node, "local-mac-address", (void *)lmac, 6);
	if (i == 6) {
		valid = 0;
		for (i = 0; i < 6; i++)
			if (lmac[i] != 0) {
				valid = 1;
				break;
			}

		if (valid) {
			bcopy(lmac, addr, 6);
			return;
		}
	}

	/*
	 * Fall back -- use the currently programmed address.
	 */
	mac_l = MGE_READ(sc, MGE_MAC_ADDR_L);
	mac_h = MGE_READ(sc, MGE_MAC_ADDR_H);

	addr[0] = (mac_h & 0xff000000) >> 24;
	addr[1] = (mac_h & 0x00ff0000) >> 16;
	addr[2] = (mac_h & 0x0000ff00) >> 8;
	addr[3] = (mac_h & 0x000000ff);
	addr[4] = (mac_l & 0x0000ff00) >> 8;
	addr[5] = (mac_l & 0x000000ff);
}

static uint32_t
mge_tfut_ipg(uint32_t val, int ver)
{

	switch (ver) {
	case 1:
		return ((val & 0x3fff) << 4);
	case 2:
	default:
		return ((val & 0xffff) << 4);
	}
}

static uint32_t
mge_rx_ipg(uint32_t val, int ver)
{

	switch (ver) {
	case 1:
		return ((val & 0x3fff) << 8);
	case 2:
	default:
		return (((val & 0x8000) << 10) | ((val & 0x7fff) << 7));
	}
}

static void
mge_ver_params(struct mge_softc *sc)
{
	uint32_t d, r;

	soc_id(&d, &r);
	if (d == MV_DEV_88F6281 || d == MV_DEV_88F6781 ||
	    d == MV_DEV_88F6282 ||
	    d == MV_DEV_MV78100 ||
	    d == MV_DEV_MV78100_Z0 ||
	    (d & MV_DEV_FAMILY_MASK) == MV_DEV_DISCOVERY) {
		sc->mge_ver = 2;
		sc->mge_mtu = 0x4e8;
		sc->mge_tfut_ipg_max = 0xFFFF;
		sc->mge_rx_ipg_max = 0xFFFF;
		sc->mge_tx_arb_cfg = 0xFC0000FF;
		sc->mge_tx_tok_cfg = 0xFFFF7FFF;
		sc->mge_tx_tok_cnt = 0x3FFFFFFF;
	} else {
		sc->mge_ver = 1;
		sc->mge_mtu = 0x458;
		sc->mge_tfut_ipg_max = 0x3FFF;
		sc->mge_rx_ipg_max = 0x3FFF;
		sc->mge_tx_arb_cfg = 0x000000FF;
		sc->mge_tx_tok_cfg = 0x3FFFFFFF;
		sc->mge_tx_tok_cnt = 0x3FFFFFFF;
	}
	if (d == MV_DEV_88RC8180)
		sc->mge_intr_cnt = 1;
	else
		sc->mge_intr_cnt = 2;

	if (d == MV_DEV_MV78160 || d == MV_DEV_MV78260 || d == MV_DEV_MV78460)
		sc->mge_hw_csum = 0;
	else
		sc->mge_hw_csum = 1;
}

static void
mge_set_mac_address(struct mge_softc *sc)
{
	char *if_mac;
	uint32_t mac_l, mac_h;

	MGE_GLOBAL_LOCK_ASSERT(sc);

	if_mac = (char *)IF_LLADDR(sc->ifp);

	mac_l = (if_mac[4] << 8) | (if_mac[5]);
	mac_h = (if_mac[0] << 24)| (if_mac[1] << 16) |
	    (if_mac[2] << 8) | (if_mac[3] << 0);

	MGE_WRITE(sc, MGE_MAC_ADDR_L, mac_l);
	MGE_WRITE(sc, MGE_MAC_ADDR_H, mac_h);

	mge_set_ucast_address(sc, if_mac[5], MGE_RX_DEFAULT_QUEUE);
}

static void
mge_set_ucast_address(struct mge_softc *sc, uint8_t last_byte, uint8_t queue)
{
	uint32_t reg_idx, reg_off, reg_val, i;

	last_byte &= 0xf;
	reg_idx = last_byte / MGE_UCAST_REG_NUMBER;
	reg_off = (last_byte % MGE_UCAST_REG_NUMBER) * 8;
	reg_val = (1 | (queue << 1)) << reg_off;

	for (i = 0; i < MGE_UCAST_REG_NUMBER; i++) {
		if ( i == reg_idx)
			MGE_WRITE(sc, MGE_DA_FILTER_UCAST(i), reg_val);
		else
			MGE_WRITE(sc, MGE_DA_FILTER_UCAST(i), 0);
	}
}

static void
mge_set_prom_mode(struct mge_softc *sc, uint8_t queue)
{
	uint32_t port_config;
	uint32_t reg_val, i;

	/* Enable or disable promiscuous mode as needed */
	if (sc->ifp->if_flags & IFF_PROMISC) {
		port_config = MGE_READ(sc, MGE_PORT_CONFIG);
		port_config |= PORT_CONFIG_UPM;
		MGE_WRITE(sc, MGE_PORT_CONFIG, port_config);

		reg_val = ((1 | (queue << 1)) | (1 | (queue << 1)) << 8 |
		   (1 | (queue << 1)) << 16 | (1 | (queue << 1)) << 24);

		for (i = 0; i < MGE_MCAST_REG_NUMBER; i++) {
			MGE_WRITE(sc, MGE_DA_FILTER_SPEC_MCAST(i), reg_val);
			MGE_WRITE(sc, MGE_DA_FILTER_OTH_MCAST(i), reg_val);
		}

		for (i = 0; i < MGE_UCAST_REG_NUMBER; i++)
			MGE_WRITE(sc, MGE_DA_FILTER_UCAST(i), reg_val);

	} else {
		port_config = MGE_READ(sc, MGE_PORT_CONFIG);
		port_config &= ~PORT_CONFIG_UPM;
		MGE_WRITE(sc, MGE_PORT_CONFIG, port_config);

		for (i = 0; i < MGE_MCAST_REG_NUMBER; i++) {
			MGE_WRITE(sc, MGE_DA_FILTER_SPEC_MCAST(i), 0);
			MGE_WRITE(sc, MGE_DA_FILTER_OTH_MCAST(i), 0);
		}

		mge_set_mac_address(sc);
	}
}

static void
mge_get_dma_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	u_int32_t *paddr;

	KASSERT(nseg == 1, ("wrong number of segments, should be 1"));
	paddr = arg;

	*paddr = segs->ds_addr;
}

static int
mge_new_rxbuf(bus_dma_tag_t tag, bus_dmamap_t map, struct mbuf **mbufp,
    bus_addr_t *paddr)
{
	struct mbuf *new_mbuf;
	bus_dma_segment_t seg[1];
	int error;
	int nsegs;

	KASSERT(mbufp != NULL, ("NULL mbuf pointer!"));

	new_mbuf = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (new_mbuf == NULL)
		return (ENOBUFS);
	new_mbuf->m_len = new_mbuf->m_pkthdr.len = new_mbuf->m_ext.ext_size;

	if (*mbufp) {
		bus_dmamap_sync(tag, map, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(tag, map);
	}

	error = bus_dmamap_load_mbuf_sg(tag, map, new_mbuf, seg, &nsegs,
	    BUS_DMA_NOWAIT);
	KASSERT(nsegs == 1, ("Too many segments returned!"));
	if (nsegs != 1 || error)
		panic("mge_new_rxbuf(): nsegs(%d), error(%d)", nsegs, error);

	bus_dmamap_sync(tag, map, BUS_DMASYNC_PREREAD);

	(*mbufp) = new_mbuf;
	(*paddr) = seg->ds_addr;
	return (0);
}

static int
mge_alloc_desc_dma(struct mge_softc *sc, struct mge_desc_wrapper* tab,
    uint32_t size, bus_dma_tag_t *buffer_tag)
{
	struct mge_desc_wrapper *dw;
	bus_addr_t desc_paddr;
	int i, error;

	desc_paddr = 0;
	for (i = size - 1; i >= 0; i--) {
		dw = &(tab[i]);
		error = bus_dmamem_alloc(sc->mge_desc_dtag,
		    (void**)&(dw->mge_desc),
		    BUS_DMA_NOWAIT | BUS_DMA_ZERO | BUS_DMA_COHERENT,
		    &(dw->desc_dmap));

		if (error) {
			if_printf(sc->ifp, "failed to allocate DMA memory\n");
			dw->mge_desc = NULL;
			return (ENXIO);
		}

		error = bus_dmamap_load(sc->mge_desc_dtag, dw->desc_dmap,
		    dw->mge_desc, sizeof(struct mge_desc), mge_get_dma_addr,
		    &(dw->mge_desc_paddr), BUS_DMA_NOWAIT);

		if (error) {
			if_printf(sc->ifp, "can't load descriptor\n");
			bus_dmamem_free(sc->mge_desc_dtag, dw->mge_desc,
			    dw->desc_dmap);
			dw->mge_desc = NULL;
			return (ENXIO);
		}

		/* Chain descriptors */
		dw->mge_desc->next_desc = desc_paddr;
		desc_paddr = dw->mge_desc_paddr;
	}
	tab[size - 1].mge_desc->next_desc = desc_paddr;

	/* Allocate a busdma tag for mbufs. */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->dev),	/* parent */
	    1, 0,				/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,		/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filtfunc, filtfuncarg */
	    MCLBYTES, 1,			/* maxsize, nsegments */
	    MCLBYTES, 0,			/* maxsegsz, flags */
	    NULL, NULL,				/* lockfunc, lockfuncarg */
	    buffer_tag);			/* dmat */
	if (error) {
		if_printf(sc->ifp, "failed to create busdma tag for mbufs\n");
		return (ENXIO);
	}

	/* Create TX busdma maps */
	for (i = 0; i < size; i++) {
		dw = &(tab[i]);
		error = bus_dmamap_create(*buffer_tag, 0, &dw->buffer_dmap);
		if (error) {
			if_printf(sc->ifp, "failed to create map for mbuf\n");
			return (ENXIO);
		}

		dw->buffer = (struct mbuf*)NULL;
		dw->mge_desc->buffer = (bus_addr_t)NULL;
	}

	return (0);
}

static int
mge_allocate_dma(struct mge_softc *sc)
{
	int error;
	struct mge_desc_wrapper *dw;
	int i;

	/* Allocate a busdma tag and DMA safe memory for TX/RX descriptors. */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->dev),	/* parent */
	    16, 0,				/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,		/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filtfunc, filtfuncarg */
	    sizeof(struct mge_desc), 1,		/* maxsize, nsegments */
	    sizeof(struct mge_desc), 0,		/* maxsegsz, flags */
	    NULL, NULL,				/* lockfunc, lockfuncarg */
	    &sc->mge_desc_dtag);		/* dmat */


	mge_alloc_desc_dma(sc, sc->mge_tx_desc, MGE_TX_DESC_NUM,
	    &sc->mge_tx_dtag);
	mge_alloc_desc_dma(sc, sc->mge_rx_desc, MGE_RX_DESC_NUM,
	    &sc->mge_rx_dtag);

	for (i = 0; i < MGE_RX_DESC_NUM; i++) {
		dw = &(sc->mge_rx_desc[i]);
		mge_new_rxbuf(sc->mge_rx_dtag, dw->buffer_dmap, &dw->buffer,
		    &dw->mge_desc->buffer);
	}

	sc->tx_desc_start = sc->mge_tx_desc[0].mge_desc_paddr;
	sc->rx_desc_start = sc->mge_rx_desc[0].mge_desc_paddr;

	return (0);
}

static void
mge_free_desc(struct mge_softc *sc, struct mge_desc_wrapper* tab,
    uint32_t size, bus_dma_tag_t buffer_tag, uint8_t free_mbufs)
{
	struct mge_desc_wrapper *dw;
	int i;

	for (i = 0; i < size; i++) {
		/* Free RX mbuf */
		dw = &(tab[i]);

		if (dw->buffer_dmap) {
			if (free_mbufs) {
				bus_dmamap_sync(buffer_tag, dw->buffer_dmap,
				    BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(buffer_tag, dw->buffer_dmap);
			}
			bus_dmamap_destroy(buffer_tag, dw->buffer_dmap);
			if (free_mbufs)
				m_freem(dw->buffer);
		}
		/* Free RX descriptors */
		if (dw->desc_dmap) {
			bus_dmamap_sync(sc->mge_desc_dtag, dw->desc_dmap,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->mge_desc_dtag, dw->desc_dmap);
			bus_dmamem_free(sc->mge_desc_dtag, dw->mge_desc,
			    dw->desc_dmap);
		}
	}
}

static void
mge_free_dma(struct mge_softc *sc)
{

	/* Free desciptors and mbufs */
	mge_free_desc(sc, sc->mge_rx_desc, MGE_RX_DESC_NUM, sc->mge_rx_dtag, 1);
	mge_free_desc(sc, sc->mge_tx_desc, MGE_TX_DESC_NUM, sc->mge_tx_dtag, 0);

	/* Destroy mbuf dma tag */
	bus_dma_tag_destroy(sc->mge_tx_dtag);
	bus_dma_tag_destroy(sc->mge_rx_dtag);
	/* Destroy descriptors tag */
	bus_dma_tag_destroy(sc->mge_desc_dtag);
}

static void
mge_reinit_rx(struct mge_softc *sc)
{
	struct mge_desc_wrapper *dw;
	int i;

	MGE_RECEIVE_LOCK_ASSERT(sc);

	mge_free_desc(sc, sc->mge_rx_desc, MGE_RX_DESC_NUM, sc->mge_rx_dtag, 1);

	mge_alloc_desc_dma(sc, sc->mge_rx_desc, MGE_RX_DESC_NUM,
	    &sc->mge_rx_dtag);

	for (i = 0; i < MGE_RX_DESC_NUM; i++) {
		dw = &(sc->mge_rx_desc[i]);
		mge_new_rxbuf(sc->mge_rx_dtag, dw->buffer_dmap, &dw->buffer,
		&dw->mge_desc->buffer);
	}

	sc->rx_desc_start = sc->mge_rx_desc[0].mge_desc_paddr;
	sc->rx_desc_curr = 0;

	MGE_WRITE(sc, MGE_RX_CUR_DESC_PTR(MGE_RX_DEFAULT_QUEUE),
	    sc->rx_desc_start);

	/* Enable RX queue */
	MGE_WRITE(sc, MGE_RX_QUEUE_CMD, MGE_ENABLE_RXQ(MGE_RX_DEFAULT_QUEUE));
}

#ifdef DEVICE_POLLING
static poll_handler_t mge_poll;

static int
mge_poll(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct mge_softc *sc = ifp->if_softc;
	uint32_t int_cause, int_cause_ext;
	int rx_npkts = 0;

	MGE_RECEIVE_LOCK(sc);

	if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
		MGE_RECEIVE_UNLOCK(sc);
		return (rx_npkts);
	}

	if (cmd == POLL_AND_CHECK_STATUS) {
		int_cause = MGE_READ(sc, MGE_PORT_INT_CAUSE);
		int_cause_ext = MGE_READ(sc, MGE_PORT_INT_CAUSE_EXT);

		/* Check for resource error */
		if (int_cause & MGE_PORT_INT_RXERRQ0)
			mge_reinit_rx(sc);

		if (int_cause || int_cause_ext) {
			MGE_WRITE(sc, MGE_PORT_INT_CAUSE, ~int_cause);
			MGE_WRITE(sc, MGE_PORT_INT_CAUSE_EXT, ~int_cause_ext);
		}
	}


	rx_npkts = mge_intr_rx_locked(sc, count);

	MGE_RECEIVE_UNLOCK(sc);
	MGE_TRANSMIT_LOCK(sc);
	mge_intr_tx_locked(sc);
	MGE_TRANSMIT_UNLOCK(sc);
	return (rx_npkts);
}
#endif /* DEVICE_POLLING */

static int
mge_attach(device_t dev)
{
	struct mge_softc *sc;
	struct mii_softc *miisc;
	struct ifnet *ifp;
	uint8_t hwaddr[ETHER_ADDR_LEN];
	int i, error, phy;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->node = ofw_bus_get_node(dev);
	phy = 0;

	if (fdt_get_phyaddr(sc->node, sc->dev, &phy, (void **)&sc->phy_sc) == 0) {
		device_printf(dev, "PHY%i attached, phy_sc points to %s\n", phy,
		    device_get_nameunit(sc->phy_sc->dev));
		sc->phy_attached = 1;
	} else {
		device_printf(dev, "PHY not attached.\n");
		sc->phy_attached = 0;
		sc->phy_sc = sc;
	}

	if (fdt_find_compatible(sc->node, "mrvl,sw", 1) != 0) {
		device_printf(dev, "Switch attached.\n");
		sc->switch_attached = 1;
		/* additional variable available across instances */
		switch_attached = 1;
	} else {
		sc->switch_attached = 0;
	}

	if (device_get_unit(dev) == 0) {
		sx_init(&sx_smi, "mge_tick() SMI access threads interlock");
	}

	/* Set chip version-dependent parameters */
	mge_ver_params(sc);

	/* Initialize mutexes */
	mtx_init(&sc->transmit_lock, device_get_nameunit(dev), "mge TX lock",
	    MTX_DEF);
	mtx_init(&sc->receive_lock, device_get_nameunit(dev), "mge RX lock",
	    MTX_DEF);

	/* Allocate IO and IRQ resources */
	error = bus_alloc_resources(dev, res_spec, sc->res);
	if (error) {
		device_printf(dev, "could not allocate resources\n");
		mge_detach(dev);
		return (ENXIO);
	}

	/* Allocate DMA, buffers, buffer descriptors */
	error = mge_allocate_dma(sc);
	if (error) {
		mge_detach(dev);
		return (ENXIO);
	}

	sc->tx_desc_curr = 0;
	sc->rx_desc_curr = 0;
	sc->tx_desc_used_idx = 0;
	sc->tx_desc_used_count = 0;

	/* Configure defaults for interrupts coalescing */
	sc->rx_ic_time = 768;
	sc->tx_ic_time = 768;
	mge_add_sysctls(sc);

	/* Allocate network interface */
	ifp = sc->ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "if_alloc() failed\n");
		mge_detach(dev);
		return (ENOMEM);
	}

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_softc = sc;
	ifp->if_flags = IFF_SIMPLEX | IFF_MULTICAST | IFF_BROADCAST;
	ifp->if_capabilities = IFCAP_VLAN_MTU;
	if (sc->mge_hw_csum) {
		ifp->if_capabilities |= IFCAP_HWCSUM;
		ifp->if_hwassist = MGE_CHECKSUM_FEATURES;
	}
	ifp->if_capenable = ifp->if_capabilities;

#ifdef DEVICE_POLLING
	/* Advertise that polling is supported */
	ifp->if_capabilities |= IFCAP_POLLING;
#endif

	ifp->if_init = mge_init;
	ifp->if_start = mge_start;
	ifp->if_ioctl = mge_ioctl;

	ifp->if_snd.ifq_drv_maxlen = MGE_TX_DESC_NUM - 1;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifp->if_snd.ifq_drv_maxlen);
	IFQ_SET_READY(&ifp->if_snd);

	mge_get_mac_address(sc, hwaddr);
	ether_ifattach(ifp, hwaddr);
	callout_init(&sc->wd_callout, 0);

	/* Attach PHY(s) */
	if (sc->phy_attached) {
		error = mii_attach(dev, &sc->miibus, ifp, mge_ifmedia_upd,
		    mge_ifmedia_sts, BMSR_DEFCAPMASK, phy, MII_OFFSET_ANY, 0);
		if (error) {
			device_printf(dev, "MII failed to find PHY\n");
			if_free(ifp);
			sc->ifp = NULL;
			mge_detach(dev);
			return (error);
		}
		sc->mii = device_get_softc(sc->miibus);

		/* Tell the MAC where to find the PHY so autoneg works */
		miisc = LIST_FIRST(&sc->mii->mii_phys);
		MGE_WRITE(sc, MGE_REG_PHYDEV, miisc->mii_phy);
	} else {
		/* no PHY, so use hard-coded values */
		ifmedia_init(&sc->mge_ifmedia, 0,
		    mge_ifmedia_upd,
		    mge_ifmedia_sts);
		ifmedia_add(&sc->mge_ifmedia,
		    IFM_ETHER | IFM_1000_T | IFM_FDX,
		    0, NULL);
		ifmedia_set(&sc->mge_ifmedia,
		    IFM_ETHER | IFM_1000_T | IFM_FDX);
	}

	/* Attach interrupt handlers */
	/* TODO: review flags, in part. mark RX as INTR_ENTROPY ? */
	for (i = 1; i <= sc->mge_intr_cnt; ++i) {
		error = bus_setup_intr(dev, sc->res[i],
		    INTR_TYPE_NET | INTR_MPSAFE,
		    NULL, *mge_intrs[(sc->mge_intr_cnt == 1 ? 0 : i)].handler,
		    sc, &sc->ih_cookie[i - 1]);
		if (error) {
			device_printf(dev, "could not setup %s\n",
			    mge_intrs[(sc->mge_intr_cnt == 1 ? 0 : i)].description);
			mge_detach(dev);
			return (error);
		}
	}

	if (sc->switch_attached) {
		device_t child;
		MGE_WRITE(sc, MGE_REG_PHYDEV, MGE_SWITCH_PHYDEV);
		child = device_add_child(dev, "mdio", -1);
		bus_generic_attach(dev);
	}

	return (0);
}

static int
mge_detach(device_t dev)
{
	struct mge_softc *sc;
	int error,i;

	sc = device_get_softc(dev);

	/* Stop controller and free TX queue */
	if (sc->ifp)
		mge_shutdown(dev);

	/* Wait for stopping ticks */
        callout_drain(&sc->wd_callout);

	/* Stop and release all interrupts */
	for (i = 0; i < sc->mge_intr_cnt; ++i) {
		if (!sc->ih_cookie[i])
			continue;

		error = bus_teardown_intr(dev, sc->res[1 + i],
		    sc->ih_cookie[i]);
		if (error)
			device_printf(dev, "could not release %s\n",
			    mge_intrs[(sc->mge_intr_cnt == 1 ? 0 : i + 1)].description);
	}

	/* Detach network interface */
	if (sc->ifp) {
		ether_ifdetach(sc->ifp);
		if_free(sc->ifp);
	}

	/* Free DMA resources */
	mge_free_dma(sc);

	/* Free IO memory handler */
	bus_release_resources(dev, res_spec, sc->res);

	/* Destroy mutexes */
	mtx_destroy(&sc->receive_lock);
	mtx_destroy(&sc->transmit_lock);

	if (device_get_unit(dev) == 0)
		sx_destroy(&sx_smi);

	return (0);
}

static void
mge_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct mge_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	MGE_GLOBAL_LOCK(sc);

	if (!sc->phy_attached) {
		ifmr->ifm_active = IFM_1000_T | IFM_FDX | IFM_ETHER;
		ifmr->ifm_status = IFM_AVALID | IFM_ACTIVE;
		goto out_unlock;
	}

	mii = sc->mii;
	mii_pollstat(mii);

	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;

out_unlock:
	MGE_GLOBAL_UNLOCK(sc);
}

static uint32_t
mge_set_port_serial_control(uint32_t media)
{
	uint32_t port_config;

	port_config = PORT_SERIAL_RES_BIT9 | PORT_SERIAL_FORCE_LINK_FAIL |
	    PORT_SERIAL_MRU(PORT_SERIAL_MRU_1552);

	if (IFM_TYPE(media) == IFM_ETHER) {
		switch(IFM_SUBTYPE(media)) {
			case IFM_AUTO:
				break;
			case IFM_1000_T:
				port_config  |= (PORT_SERIAL_GMII_SPEED_1000 |
				    PORT_SERIAL_AUTONEG | PORT_SERIAL_AUTONEG_FC
				    | PORT_SERIAL_SPEED_AUTONEG);
				break;
			case IFM_100_TX:
				port_config  |= (PORT_SERIAL_MII_SPEED_100 |
				    PORT_SERIAL_AUTONEG | PORT_SERIAL_AUTONEG_FC
				    | PORT_SERIAL_SPEED_AUTONEG);
				break;
			case IFM_10_T:
				port_config  |= (PORT_SERIAL_AUTONEG |
				    PORT_SERIAL_AUTONEG_FC |
				    PORT_SERIAL_SPEED_AUTONEG);
				break;
		}
		if (media & IFM_FDX)
			port_config |= PORT_SERIAL_FULL_DUPLEX;
	}
	return (port_config);
}

static int
mge_ifmedia_upd(struct ifnet *ifp)
{
	struct mge_softc *sc = ifp->if_softc;

	/*
	 * Do not do anything for switch here, as updating media between
	 * MGE MAC and switch MAC is hardcoded in PCB. Changing it here would
	 * break the link.
	 */
	if (sc->phy_attached) {
		MGE_GLOBAL_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			sc->mge_media_status = sc->mii->mii_media.ifm_media;
			mii_mediachg(sc->mii);

			/* MGE MAC needs to be reinitialized. */
			mge_init_locked(sc);

		}
		MGE_GLOBAL_UNLOCK(sc);
	}

	return (0);
}

static void
mge_init(void *arg)
{
	struct mge_softc *sc;

	sc = arg;
	MGE_GLOBAL_LOCK(sc);

	mge_init_locked(arg);

	MGE_GLOBAL_UNLOCK(sc);
}

static void
mge_init_locked(void *arg)
{
	struct mge_softc *sc = arg;
	struct mge_desc_wrapper *dw;
	volatile uint32_t reg_val;
	int i, count;
	uint32_t media_status;


	MGE_GLOBAL_LOCK_ASSERT(sc);

	/* Stop interface */
	mge_stop(sc);

	/* Disable interrupts */
	mge_intrs_ctrl(sc, 0);

	/* Set MAC address */
	mge_set_mac_address(sc);

	/* Setup multicast filters */
	mge_setup_multicast(sc);

	if (sc->mge_ver == 2) {
		MGE_WRITE(sc, MGE_PORT_SERIAL_CTRL1, MGE_RGMII_EN);
		MGE_WRITE(sc, MGE_FIXED_PRIO_CONF, MGE_FIXED_PRIO_EN(0));
	}

	/* Initialize TX queue configuration registers */
	MGE_WRITE(sc, MGE_TX_TOKEN_COUNT(0), sc->mge_tx_tok_cnt);
	MGE_WRITE(sc, MGE_TX_TOKEN_CONF(0), sc->mge_tx_tok_cfg);
	MGE_WRITE(sc, MGE_TX_ARBITER_CONF(0), sc->mge_tx_arb_cfg);

	/* Clear TX queue configuration registers for unused queues */
	for (i = 1; i < 7; i++) {
		MGE_WRITE(sc, MGE_TX_TOKEN_COUNT(i), 0);
		MGE_WRITE(sc, MGE_TX_TOKEN_CONF(i), 0);
		MGE_WRITE(sc, MGE_TX_ARBITER_CONF(i), 0);
	}

	/* Set default MTU */
	MGE_WRITE(sc, sc->mge_mtu, 0);

	/* Port configuration */
	MGE_WRITE(sc, MGE_PORT_CONFIG,
	    PORT_CONFIG_RXCS | PORT_CONFIG_DFLT_RXQ(0) |
	    PORT_CONFIG_ARO_RXQ(0));
	MGE_WRITE(sc, MGE_PORT_EXT_CONFIG , 0x0);

	/* Configure promisc mode */
	mge_set_prom_mode(sc, MGE_RX_DEFAULT_QUEUE);

	media_status = sc->mge_media_status;
	if (sc->switch_attached) {
		media_status &= ~IFM_TMASK;
		media_status |= IFM_1000_T;
	}

	/* Setup port configuration */
	reg_val = mge_set_port_serial_control(media_status);
	MGE_WRITE(sc, MGE_PORT_SERIAL_CTRL, reg_val);

	/* Setup SDMA configuration */
	MGE_WRITE(sc, MGE_SDMA_CONFIG , MGE_SDMA_RX_BYTE_SWAP |
	    MGE_SDMA_TX_BYTE_SWAP |
	    MGE_SDMA_RX_BURST_SIZE(MGE_SDMA_BURST_16_WORD) |
	    MGE_SDMA_TX_BURST_SIZE(MGE_SDMA_BURST_16_WORD));

	MGE_WRITE(sc, MGE_TX_FIFO_URGENT_TRSH, 0x0);

	MGE_WRITE(sc, MGE_TX_CUR_DESC_PTR, sc->tx_desc_start);
	MGE_WRITE(sc, MGE_RX_CUR_DESC_PTR(MGE_RX_DEFAULT_QUEUE),
	    sc->rx_desc_start);

	/* Reset descriptor indexes */
	sc->tx_desc_curr = 0;
	sc->rx_desc_curr = 0;
	sc->tx_desc_used_idx = 0;
	sc->tx_desc_used_count = 0;

	/* Enable RX descriptors */
	for (i = 0; i < MGE_RX_DESC_NUM; i++) {
		dw = &sc->mge_rx_desc[i];
		dw->mge_desc->cmd_status = MGE_RX_ENABLE_INT | MGE_DMA_OWNED;
		dw->mge_desc->buff_size = MCLBYTES;
		bus_dmamap_sync(sc->mge_desc_dtag, dw->desc_dmap,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}

	/* Enable RX queue */
	MGE_WRITE(sc, MGE_RX_QUEUE_CMD, MGE_ENABLE_RXQ(MGE_RX_DEFAULT_QUEUE));

	/* Enable port */
	reg_val = MGE_READ(sc, MGE_PORT_SERIAL_CTRL);
	reg_val |= PORT_SERIAL_ENABLE;
	MGE_WRITE(sc, MGE_PORT_SERIAL_CTRL, reg_val);
	count = 0x100000;
	for (;;) {
		reg_val = MGE_READ(sc, MGE_PORT_STATUS);
		if (reg_val & MGE_STATUS_LINKUP)
			break;
		DELAY(100);
		if (--count == 0) {
			if_printf(sc->ifp, "Timeout on link-up\n");
			break;
		}
	}

	/* Setup interrupts coalescing */
	mge_set_rxic(sc);
	mge_set_txic(sc);

	/* Enable interrupts */
#ifdef DEVICE_POLLING
        /*
	 * * ...only if polling is not turned on. Disable interrupts explicitly
	 * if polling is enabled.
	 */
	if (sc->ifp->if_capenable & IFCAP_POLLING)
		mge_intrs_ctrl(sc, 0);
	else
#endif /* DEVICE_POLLING */
	mge_intrs_ctrl(sc, 1);

	/* Activate network interface */
	sc->ifp->if_drv_flags |= IFF_DRV_RUNNING;
	sc->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	sc->wd_timer = 0;

	/* Schedule watchdog timeout */
	if (sc->phy_attached)
		callout_reset(&sc->wd_callout, hz, mge_tick, sc);
}

static void
mge_intr_rxtx(void *arg)
{
	struct mge_softc *sc;
	uint32_t int_cause, int_cause_ext;

	sc = arg;
	MGE_GLOBAL_LOCK(sc);

#ifdef DEVICE_POLLING
	if (sc->ifp->if_capenable & IFCAP_POLLING) {
		MGE_GLOBAL_UNLOCK(sc);
		return;
	}
#endif

	/* Get interrupt cause */
	int_cause = MGE_READ(sc, MGE_PORT_INT_CAUSE);
	int_cause_ext = MGE_READ(sc, MGE_PORT_INT_CAUSE_EXT);

	/* Check for Transmit interrupt */
	if (int_cause_ext & (MGE_PORT_INT_EXT_TXBUF0 |
	    MGE_PORT_INT_EXT_TXUR)) {
		MGE_WRITE(sc, MGE_PORT_INT_CAUSE_EXT, ~(int_cause_ext &
		    (MGE_PORT_INT_EXT_TXBUF0 | MGE_PORT_INT_EXT_TXUR)));
		mge_intr_tx_locked(sc);
	}

	MGE_TRANSMIT_UNLOCK(sc);

	/* Check for Receive interrupt */
	mge_intr_rx_check(sc, int_cause, int_cause_ext);

	MGE_RECEIVE_UNLOCK(sc);
}

static void
mge_intr_err(void *arg)
{
	struct mge_softc *sc;
	struct ifnet *ifp;

	sc = arg;
	ifp = sc->ifp;
	if_printf(ifp, "%s\n", __FUNCTION__);
}

static void
mge_intr_misc(void *arg)
{
	struct mge_softc *sc;
	struct ifnet *ifp;

	sc = arg;
	ifp = sc->ifp;
	if_printf(ifp, "%s\n", __FUNCTION__);
}

static void
mge_intr_rx(void *arg) {
	struct mge_softc *sc;
	uint32_t int_cause, int_cause_ext;

	sc = arg;
	MGE_RECEIVE_LOCK(sc);

#ifdef DEVICE_POLLING
	if (sc->ifp->if_capenable & IFCAP_POLLING) {
		MGE_RECEIVE_UNLOCK(sc);
		return;
	}
#endif

	/* Get interrupt cause */
	int_cause = MGE_READ(sc, MGE_PORT_INT_CAUSE);
	int_cause_ext = MGE_READ(sc, MGE_PORT_INT_CAUSE_EXT);

	mge_intr_rx_check(sc, int_cause, int_cause_ext);

	MGE_RECEIVE_UNLOCK(sc);
}

static void
mge_intr_rx_check(struct mge_softc *sc, uint32_t int_cause,
    uint32_t int_cause_ext)
{
	/* Check for resource error */
	if (int_cause & MGE_PORT_INT_RXERRQ0) {
		mge_reinit_rx(sc);
		MGE_WRITE(sc, MGE_PORT_INT_CAUSE,
		    ~(int_cause & MGE_PORT_INT_RXERRQ0));
	}

	int_cause &= MGE_PORT_INT_RXQ0;
	int_cause_ext &= MGE_PORT_INT_EXT_RXOR;

	if (int_cause || int_cause_ext) {
		MGE_WRITE(sc, MGE_PORT_INT_CAUSE, ~int_cause);
		MGE_WRITE(sc, MGE_PORT_INT_CAUSE_EXT, ~int_cause_ext);
		mge_intr_rx_locked(sc, -1);
	}
}

static int
mge_intr_rx_locked(struct mge_softc *sc, int count)
{
	struct ifnet *ifp = sc->ifp;
	uint32_t status;
	uint16_t bufsize;
	struct mge_desc_wrapper* dw;
	struct mbuf *mb;
	int rx_npkts = 0;

	MGE_RECEIVE_LOCK_ASSERT(sc);

	while (count != 0) {
		dw = &sc->mge_rx_desc[sc->rx_desc_curr];
		bus_dmamap_sync(sc->mge_desc_dtag, dw->desc_dmap,
		    BUS_DMASYNC_POSTREAD);

		/* Get status */
		status = dw->mge_desc->cmd_status;
		bufsize = dw->mge_desc->buff_size;
		if ((status & MGE_DMA_OWNED) != 0)
			break;

		if (dw->mge_desc->byte_count &&
		    ~(status & MGE_ERR_SUMMARY)) {

			bus_dmamap_sync(sc->mge_rx_dtag, dw->buffer_dmap,
			    BUS_DMASYNC_POSTREAD);

			mb = m_devget(dw->buffer->m_data,
			    dw->mge_desc->byte_count - ETHER_CRC_LEN,
			    0, ifp, NULL);

			if (mb == NULL)
				/* Give up if no mbufs */
				break;

			mb->m_len -= 2;
			mb->m_pkthdr.len -= 2;
			mb->m_data += 2;

			mb->m_pkthdr.rcvif = ifp;

			mge_offload_process_frame(ifp, mb, status,
			    bufsize);

			MGE_RECEIVE_UNLOCK(sc);
			(*ifp->if_input)(ifp, mb);
			MGE_RECEIVE_LOCK(sc);
			rx_npkts++;
		}

		dw->mge_desc->byte_count = 0;
		dw->mge_desc->cmd_status = MGE_RX_ENABLE_INT | MGE_DMA_OWNED;
		sc->rx_desc_curr = (++sc->rx_desc_curr % MGE_RX_DESC_NUM);
		bus_dmamap_sync(sc->mge_desc_dtag, dw->desc_dmap,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		if (count > 0)
			count -= 1;
	}

	if_inc_counter(ifp, IFCOUNTER_IPACKETS, rx_npkts);

	return (rx_npkts);
}

static void
mge_intr_sum(void *arg)
{
	struct mge_softc *sc = arg;
	struct ifnet *ifp;

	ifp = sc->ifp;
	if_printf(ifp, "%s\n", __FUNCTION__);
}

static void
mge_intr_tx(void *arg)
{
	struct mge_softc *sc = arg;
	uint32_t int_cause_ext;

	MGE_TRANSMIT_LOCK(sc);

#ifdef DEVICE_POLLING
	if (sc->ifp->if_capenable & IFCAP_POLLING) {
		MGE_TRANSMIT_UNLOCK(sc);
		return;
	}
#endif

	/* Ack the interrupt */
	int_cause_ext = MGE_READ(sc, MGE_PORT_INT_CAUSE_EXT);
	MGE_WRITE(sc, MGE_PORT_INT_CAUSE_EXT, ~(int_cause_ext &
	    (MGE_PORT_INT_EXT_TXBUF0 | MGE_PORT_INT_EXT_TXUR)));

	mge_intr_tx_locked(sc);

	MGE_TRANSMIT_UNLOCK(sc);
}

static void
mge_intr_tx_locked(struct mge_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	struct mge_desc_wrapper *dw;
	struct mge_desc *desc;
	uint32_t status;
	int send = 0;

	MGE_TRANSMIT_LOCK_ASSERT(sc);

	/* Disable watchdog */
	sc->wd_timer = 0;

	while (sc->tx_desc_used_count) {
		/* Get the descriptor */
		dw = &sc->mge_tx_desc[sc->tx_desc_used_idx];
		desc = dw->mge_desc;
		bus_dmamap_sync(sc->mge_desc_dtag, dw->desc_dmap,
		    BUS_DMASYNC_POSTREAD);

		/* Get descriptor status */
		status = desc->cmd_status;

		if (status & MGE_DMA_OWNED)
			break;

		sc->tx_desc_used_idx =
			(++sc->tx_desc_used_idx) % MGE_TX_DESC_NUM;
		sc->tx_desc_used_count--;

		/* Update collision statistics */
		if (status & MGE_ERR_SUMMARY) {
			if ((status & MGE_ERR_MASK) == MGE_TX_ERROR_LC)
				if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
			if ((status & MGE_ERR_MASK) == MGE_TX_ERROR_RL)
				if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 16);
		}

		bus_dmamap_sync(sc->mge_tx_dtag, dw->buffer_dmap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->mge_tx_dtag, dw->buffer_dmap);
		m_freem(dw->buffer);
		dw->buffer = (struct mbuf*)NULL;
		send++;

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	}

	if (send) {
		/* Now send anything that was pending */
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		mge_start_locked(ifp);
	}
}
static int
mge_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct mge_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int mask, error;
	uint32_t flags;

	error = 0;

	switch (command) {
	case SIOCSIFFLAGS:
		MGE_GLOBAL_LOCK(sc);

		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				flags = ifp->if_flags ^ sc->mge_if_flags;
				if (flags & IFF_PROMISC)
					mge_set_prom_mode(sc,
					    MGE_RX_DEFAULT_QUEUE);

				if (flags & IFF_ALLMULTI)
					mge_setup_multicast(sc);
			} else
				mge_init_locked(sc);
		}
		else if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			mge_stop(sc);

		sc->mge_if_flags = ifp->if_flags;
		MGE_GLOBAL_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			MGE_GLOBAL_LOCK(sc);
			mge_setup_multicast(sc);
			MGE_GLOBAL_UNLOCK(sc);
		}
		break;
	case SIOCSIFCAP:
		mask = ifp->if_capenable ^ ifr->ifr_reqcap;
		if (mask & IFCAP_HWCSUM) {
			ifp->if_capenable &= ~IFCAP_HWCSUM;
			ifp->if_capenable |= IFCAP_HWCSUM & ifr->ifr_reqcap;
			if (ifp->if_capenable & IFCAP_TXCSUM)
				ifp->if_hwassist = MGE_CHECKSUM_FEATURES;
			else
				ifp->if_hwassist = 0;
		}
#ifdef DEVICE_POLLING
		if (mask & IFCAP_POLLING) {
			if (ifr->ifr_reqcap & IFCAP_POLLING) {
				error = ether_poll_register(mge_poll, ifp);
				if (error)
					return(error);

				MGE_GLOBAL_LOCK(sc);
				mge_intrs_ctrl(sc, 0);
				ifp->if_capenable |= IFCAP_POLLING;
				MGE_GLOBAL_UNLOCK(sc);
			} else {
				error = ether_poll_deregister(ifp);
				MGE_GLOBAL_LOCK(sc);
				mge_intrs_ctrl(sc, 1);
				ifp->if_capenable &= ~IFCAP_POLLING;
				MGE_GLOBAL_UNLOCK(sc);
			}
		}
#endif
		break;
	case SIOCGIFMEDIA: /* fall through */
	case SIOCSIFMEDIA:
		/*
		 * Setting up media type via ioctls is *not* supported for MAC
		 * which is connected to switch. Use etherswitchcfg.
		 */
		if (!sc->phy_attached && (command == SIOCSIFMEDIA))
			return (0);
		else if (!sc->phy_attached) {
			error = ifmedia_ioctl(ifp, ifr, &sc->mge_ifmedia,
			    command);
			break;
		}

		if (IFM_SUBTYPE(ifr->ifr_media) == IFM_1000_T
		    && !(ifr->ifr_media & IFM_FDX)) {
			device_printf(sc->dev,
			    "1000baseTX half-duplex unsupported\n");
			return 0;
		}
		error = ifmedia_ioctl(ifp, ifr, &sc->mii->mii_media, command);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
	}
	return (error);
}

static int
mge_miibus_readreg(device_t dev, int phy, int reg)
{
	struct mge_softc *sc;
	sc = device_get_softc(dev);

	KASSERT(!switch_attached, ("miibus used with switch attached"));

	return (mv_read_ext_phy(dev, phy, reg));
}

static int
mge_miibus_writereg(device_t dev, int phy, int reg, int value)
{
	struct mge_softc *sc;
	sc = device_get_softc(dev);

	KASSERT(!switch_attached, ("miibus used with switch attached"));

	mv_write_ext_phy(dev, phy, reg, value);

	return (0);
}

static int
mge_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "mrvl,ge"))
		return (ENXIO);

	device_set_desc(dev, "Marvell Gigabit Ethernet controller");
	return (BUS_PROBE_DEFAULT);
}

static int
mge_resume(device_t dev)
{

	device_printf(dev, "%s\n", __FUNCTION__);
	return (0);
}

static int
mge_shutdown(device_t dev)
{
	struct mge_softc *sc = device_get_softc(dev);

	MGE_GLOBAL_LOCK(sc);

#ifdef DEVICE_POLLING
        if (sc->ifp->if_capenable & IFCAP_POLLING)
		ether_poll_deregister(sc->ifp);
#endif

	mge_stop(sc);

	MGE_GLOBAL_UNLOCK(sc);

	return (0);
}

static int
mge_encap(struct mge_softc *sc, struct mbuf *m0)
{
	struct mge_desc_wrapper *dw = NULL;
	struct ifnet *ifp;
	bus_dma_segment_t segs[MGE_TX_DESC_NUM];
	bus_dmamap_t mapp;
	int error;
	int seg, nsegs;
	int desc_no;

	ifp = sc->ifp;

	/* Fetch unused map */
	desc_no = sc->tx_desc_curr;
	dw = &sc->mge_tx_desc[desc_no];
	mapp = dw->buffer_dmap;

	/* Create mapping in DMA memory */
	error = bus_dmamap_load_mbuf_sg(sc->mge_tx_dtag, mapp, m0, segs, &nsegs,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		m_freem(m0);
		return (error);
	}

	/* Only one segment is supported. */
	if (nsegs != 1) {
		bus_dmamap_unload(sc->mge_tx_dtag, mapp);
		m_freem(m0);
		return (-1);
	}

	bus_dmamap_sync(sc->mge_tx_dtag, mapp, BUS_DMASYNC_PREWRITE);

	/* Everything is ok, now we can send buffers */
	for (seg = 0; seg < nsegs; seg++) {
		dw->mge_desc->byte_count = segs[seg].ds_len;
		dw->mge_desc->buffer = segs[seg].ds_addr;
		dw->buffer = m0;
		dw->mge_desc->cmd_status = 0;
		if (seg == 0)
			mge_offload_setup_descriptor(sc, dw);
		dw->mge_desc->cmd_status |= MGE_TX_LAST | MGE_TX_FIRST |
		    MGE_TX_ETH_CRC | MGE_TX_EN_INT | MGE_TX_PADDING |
		    MGE_DMA_OWNED;
	}

	bus_dmamap_sync(sc->mge_desc_dtag, dw->desc_dmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	sc->tx_desc_curr = (++sc->tx_desc_curr) % MGE_TX_DESC_NUM;
	sc->tx_desc_used_count++;
	return (0);
}

static void
mge_tick(void *msc)
{
	struct mge_softc *sc = msc;

	KASSERT(sc->phy_attached == 1, ("mge_tick while PHY not attached"));

	MGE_GLOBAL_LOCK(sc);

	/* Check for TX timeout */
	mge_watchdog(sc);

	mii_tick(sc->mii);

	/* Check for media type change */
	if(sc->mge_media_status != sc->mii->mii_media.ifm_media)
		mge_ifmedia_upd(sc->ifp);

	MGE_GLOBAL_UNLOCK(sc);

	/* Schedule another timeout one second from now */
	callout_reset(&sc->wd_callout, hz, mge_tick, sc);

	return;
}

static void
mge_watchdog(struct mge_softc *sc)
{
	struct ifnet *ifp;

	ifp = sc->ifp;

	if (sc->wd_timer == 0 || --sc->wd_timer) {
		return;
	}

	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	if_printf(ifp, "watchdog timeout\n");

	mge_stop(sc);
	mge_init_locked(sc);
}

static void
mge_start(struct ifnet *ifp)
{
	struct mge_softc *sc = ifp->if_softc;

	MGE_TRANSMIT_LOCK(sc);

	mge_start_locked(ifp);

	MGE_TRANSMIT_UNLOCK(sc);
}

static void
mge_start_locked(struct ifnet *ifp)
{
	struct mge_softc *sc;
	struct mbuf *m0, *mtmp;
	uint32_t reg_val, queued = 0;

	sc = ifp->if_softc;

	MGE_TRANSMIT_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING)
		return;

	for (;;) {
		/* Get packet from the queue */
		IF_DEQUEUE(&ifp->if_snd, m0);
		if (m0 == NULL)
			break;

		if (m0->m_pkthdr.csum_flags & (CSUM_IP|CSUM_TCP|CSUM_UDP) ||
		    m0->m_flags & M_VLANTAG) {
			if (M_WRITABLE(m0) == 0) {
				mtmp = m_dup(m0, M_NOWAIT);
				m_freem(m0);
				if (mtmp == NULL)
					continue;
				m0 = mtmp;
			}
		}
		/* The driver support only one DMA fragment. */
		if (m0->m_next != NULL) {
			mtmp = m_defrag(m0, M_NOWAIT);
			if (mtmp != NULL)
				m0 = mtmp;
		}

		/* Check for free descriptors */
		if (sc->tx_desc_used_count + 1 >= MGE_TX_DESC_NUM) {
			IF_PREPEND(&ifp->if_snd, m0);
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		if (mge_encap(sc, m0) != 0)
			break;

		queued++;
		BPF_MTAP(ifp, m0);
	}

	if (queued) {
		/* Enable transmitter and watchdog timer */
		reg_val = MGE_READ(sc, MGE_TX_QUEUE_CMD);
		MGE_WRITE(sc, MGE_TX_QUEUE_CMD, reg_val | MGE_ENABLE_TXQ);
		sc->wd_timer = 5;
	}
}

static void
mge_stop(struct mge_softc *sc)
{
	struct ifnet *ifp;
	volatile uint32_t reg_val, status;
	struct mge_desc_wrapper *dw;
	struct mge_desc *desc;
	int count;

	ifp = sc->ifp;

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	/* Stop tick engine */
	callout_stop(&sc->wd_callout);

	/* Disable interface */
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->wd_timer = 0;

	/* Disable interrupts */
	mge_intrs_ctrl(sc, 0);

	/* Disable Rx and Tx */
	reg_val = MGE_READ(sc, MGE_TX_QUEUE_CMD);
	MGE_WRITE(sc, MGE_TX_QUEUE_CMD, reg_val | MGE_DISABLE_TXQ);
	MGE_WRITE(sc, MGE_RX_QUEUE_CMD, MGE_DISABLE_RXQ_ALL);

	/* Remove pending data from TX queue */
	while (sc->tx_desc_used_idx != sc->tx_desc_curr &&
	    sc->tx_desc_used_count) {
		/* Get the descriptor */
		dw = &sc->mge_tx_desc[sc->tx_desc_used_idx];
		desc = dw->mge_desc;
		bus_dmamap_sync(sc->mge_desc_dtag, dw->desc_dmap,
		    BUS_DMASYNC_POSTREAD);

		/* Get descriptor status */
		status = desc->cmd_status;

		if (status & MGE_DMA_OWNED)
			break;

		sc->tx_desc_used_idx = (++sc->tx_desc_used_idx) %
		    MGE_TX_DESC_NUM;
		sc->tx_desc_used_count--;

		bus_dmamap_sync(sc->mge_tx_dtag, dw->buffer_dmap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->mge_tx_dtag, dw->buffer_dmap);

		m_freem(dw->buffer);
		dw->buffer = (struct mbuf*)NULL;
	}

	/* Wait for end of transmission */
	count = 0x100000;
	while (count--) {
		reg_val = MGE_READ(sc, MGE_PORT_STATUS);
		if ( !(reg_val & MGE_STATUS_TX_IN_PROG) &&
		    (reg_val & MGE_STATUS_TX_FIFO_EMPTY))
			break;
		DELAY(100);
	}

	if (count == 0)
		if_printf(ifp,
		    "%s: timeout while waiting for end of transmission\n",
		    __FUNCTION__);

	reg_val = MGE_READ(sc, MGE_PORT_SERIAL_CTRL);
	reg_val &= ~(PORT_SERIAL_ENABLE);
	MGE_WRITE(sc, MGE_PORT_SERIAL_CTRL ,reg_val);
}

static int
mge_suspend(device_t dev)
{

	device_printf(dev, "%s\n", __FUNCTION__);
	return (0);
}

static void
mge_offload_process_frame(struct ifnet *ifp, struct mbuf *frame,
    uint32_t status, uint16_t bufsize)
{
	int csum_flags = 0;

	if (ifp->if_capenable & IFCAP_RXCSUM) {
		if ((status & MGE_RX_L3_IS_IP) && (status & MGE_RX_IP_OK))
			csum_flags |= CSUM_IP_CHECKED | CSUM_IP_VALID;

		if ((bufsize & MGE_RX_IP_FRAGMENT) == 0 &&
		    (MGE_RX_L4_IS_TCP(status) || MGE_RX_L4_IS_UDP(status)) &&
		    (status & MGE_RX_L4_CSUM_OK)) {
			csum_flags |= CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
			frame->m_pkthdr.csum_data = 0xFFFF;
		}

		frame->m_pkthdr.csum_flags = csum_flags;
	}
}

static void
mge_offload_setup_descriptor(struct mge_softc *sc, struct mge_desc_wrapper *dw)
{
	struct mbuf *m0 = dw->buffer;
	struct ether_vlan_header *eh = mtod(m0, struct ether_vlan_header *);
	int csum_flags = m0->m_pkthdr.csum_flags;
	int cmd_status = 0;
	struct ip *ip;
	int ehlen, etype;

	if (csum_flags != 0) {
		if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
			etype = ntohs(eh->evl_proto);
			ehlen = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
			csum_flags |= MGE_TX_VLAN_TAGGED;
		} else {
			etype = ntohs(eh->evl_encap_proto);
			ehlen = ETHER_HDR_LEN;
		}

		if (etype != ETHERTYPE_IP) {
			if_printf(sc->ifp,
			    "TCP/IP Offload enabled for unsupported "
			    "protocol!\n");
			return;
		}

		ip = (struct ip *)(m0->m_data + ehlen);
		cmd_status |= MGE_TX_IP_HDR_SIZE(ip->ip_hl);
		cmd_status |= MGE_TX_NOT_FRAGMENT;
	}

	if (csum_flags & CSUM_IP)
		cmd_status |= MGE_TX_GEN_IP_CSUM;

	if (csum_flags & CSUM_TCP)
		cmd_status |= MGE_TX_GEN_L4_CSUM;

	if (csum_flags & CSUM_UDP)
		cmd_status |= MGE_TX_GEN_L4_CSUM | MGE_TX_UDP;

	dw->mge_desc->cmd_status |= cmd_status;
}

static void
mge_intrs_ctrl(struct mge_softc *sc, int enable)
{

	if (enable) {
		MGE_WRITE(sc, MGE_PORT_INT_MASK , MGE_PORT_INT_RXQ0 |
		    MGE_PORT_INT_EXTEND | MGE_PORT_INT_RXERRQ0);
		MGE_WRITE(sc, MGE_PORT_INT_MASK_EXT , MGE_PORT_INT_EXT_TXERR0 |
		    MGE_PORT_INT_EXT_RXOR | MGE_PORT_INT_EXT_TXUR |
		    MGE_PORT_INT_EXT_TXBUF0);
	} else {
		MGE_WRITE(sc, MGE_INT_CAUSE, 0x0);
		MGE_WRITE(sc, MGE_INT_MASK, 0x0);

		MGE_WRITE(sc, MGE_PORT_INT_CAUSE, 0x0);
		MGE_WRITE(sc, MGE_PORT_INT_CAUSE_EXT, 0x0);

		MGE_WRITE(sc, MGE_PORT_INT_MASK, 0x0);
		MGE_WRITE(sc, MGE_PORT_INT_MASK_EXT, 0x0);
	}
}

static uint8_t
mge_crc8(uint8_t *data, int size)
{
	uint8_t crc = 0;
	static const uint8_t ct[256] = {
		0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
		0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
		0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
		0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
		0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
		0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
		0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
		0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
		0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
		0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
		0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
		0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
		0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
		0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
		0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
		0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
		0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
		0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
		0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
		0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
		0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
		0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
		0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
		0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
		0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
		0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
		0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
		0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
		0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
		0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
		0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
		0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
	};

	while(size--)
		crc = ct[crc ^ *(data++)];

	return(crc);
}

static void
mge_setup_multicast(struct mge_softc *sc)
{
	uint8_t special[5] = { 0x01, 0x00, 0x5E, 0x00, 0x00 };
	uint8_t v = (MGE_RX_DEFAULT_QUEUE << 1) | 1;
	uint32_t smt[MGE_MCAST_REG_NUMBER];
	uint32_t omt[MGE_MCAST_REG_NUMBER];
	struct ifnet *ifp = sc->ifp;
	struct ifmultiaddr *ifma;
	uint8_t *mac;
	int i;

	if (ifp->if_flags & IFF_ALLMULTI) {
		for (i = 0; i < MGE_MCAST_REG_NUMBER; i++)
			smt[i] = omt[i] = (v << 24) | (v << 16) | (v << 8) | v;
	} else {
		memset(smt, 0, sizeof(smt));
		memset(omt, 0, sizeof(omt));

		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;

			mac = LLADDR((struct sockaddr_dl *)ifma->ifma_addr);
			if (memcmp(mac, special, sizeof(special)) == 0) {
				i = mac[5];
				smt[i >> 2] |= v << ((i & 0x03) << 3);
			} else {
				i = mge_crc8(mac, ETHER_ADDR_LEN);
				omt[i >> 2] |= v << ((i & 0x03) << 3);
			}
		}
		if_maddr_runlock(ifp);
	}

	for (i = 0; i < MGE_MCAST_REG_NUMBER; i++) {
		MGE_WRITE(sc, MGE_DA_FILTER_SPEC_MCAST(i), smt[i]);
		MGE_WRITE(sc, MGE_DA_FILTER_OTH_MCAST(i), omt[i]);
	}
}

static void
mge_set_rxic(struct mge_softc *sc)
{
	uint32_t reg;

	if (sc->rx_ic_time > sc->mge_rx_ipg_max)
		sc->rx_ic_time = sc->mge_rx_ipg_max;

	reg = MGE_READ(sc, MGE_SDMA_CONFIG);
	reg &= ~mge_rx_ipg(sc->mge_rx_ipg_max, sc->mge_ver);
	reg |= mge_rx_ipg(sc->rx_ic_time, sc->mge_ver);
	MGE_WRITE(sc, MGE_SDMA_CONFIG, reg);
}

static void
mge_set_txic(struct mge_softc *sc)
{
	uint32_t reg;

	if (sc->tx_ic_time > sc->mge_tfut_ipg_max)
		sc->tx_ic_time = sc->mge_tfut_ipg_max;

	reg = MGE_READ(sc, MGE_TX_FIFO_URGENT_TRSH);
	reg &= ~mge_tfut_ipg(sc->mge_tfut_ipg_max, sc->mge_ver);
	reg |= mge_tfut_ipg(sc->tx_ic_time, sc->mge_ver);
	MGE_WRITE(sc, MGE_TX_FIFO_URGENT_TRSH, reg);
}

static int
mge_sysctl_ic(SYSCTL_HANDLER_ARGS)
{
	struct mge_softc *sc = (struct mge_softc *)arg1;
	uint32_t time;
	int error;

	time = (arg2 == MGE_IC_RX) ? sc->rx_ic_time : sc->tx_ic_time; 
	error = sysctl_handle_int(oidp, &time, 0, req);
	if (error != 0)
		return(error);

	MGE_GLOBAL_LOCK(sc);
	if (arg2 == MGE_IC_RX) {
		sc->rx_ic_time = time;
		mge_set_rxic(sc);
	} else {
		sc->tx_ic_time = time;
		mge_set_txic(sc);
	}
	MGE_GLOBAL_UNLOCK(sc);

	return(0);
}

static void
mge_add_sysctls(struct mge_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *children;
	struct sysctl_oid *tree;

	ctx = device_get_sysctl_ctx(sc->dev);
	children = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev));
	tree = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "int_coal",
	    CTLFLAG_RD, 0, "MGE Interrupts coalescing");
	children = SYSCTL_CHILDREN(tree);

	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "rx_time",
	    CTLTYPE_UINT | CTLFLAG_RW, sc, MGE_IC_RX, mge_sysctl_ic,
	    "I", "IC RX time threshold");
	SYSCTL_ADD_PROC(ctx, children, OID_AUTO, "tx_time",
	    CTLTYPE_UINT | CTLFLAG_RW, sc, MGE_IC_TX, mge_sysctl_ic,
	    "I", "IC TX time threshold");
}

static int
mge_mdio_writereg(device_t dev, int phy, int reg, int value)
{

	mv_write_ge_smi(dev, phy, reg, value);

	return (0);
}


static int
mge_mdio_readreg(device_t dev, int phy, int reg)
{
	int ret;

	ret = mv_read_ge_smi(dev, phy, reg);

	return (ret);
}
