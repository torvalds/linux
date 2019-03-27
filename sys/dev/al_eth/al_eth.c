/*-
 * Copyright (c) 2015,2016 Annapurna Labs Ltd. and affiliates
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <machine/atomic.h>

#include "opt_inet.h"
#include "opt_inet6.h"

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <netinet/in.h>
#include <net/if_vlan_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_lro.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#endif

#ifdef INET6
#include <netinet/ip6.h>
#endif

#include <sys/sockio.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <al_hal_common.h>
#include <al_hal_plat_services.h>
#include <al_hal_udma_config.h>
#include <al_hal_udma_iofic.h>
#include <al_hal_udma_debug.h>
#include <al_hal_eth.h>

#include "al_eth.h"
#include "al_init_eth_lm.h"
#include "arm/annapurna/alpine/alpine_serdes.h"

#include "miibus_if.h"

#define	device_printf_dbg(fmt, ...) do {				\
	if (AL_DBG_LEVEL >= AL_DBG_LEVEL_DBG) { AL_DBG_LOCK();		\
	    device_printf(fmt, __VA_ARGS__); AL_DBG_UNLOCK();}		\
	} while (0)

MALLOC_DEFINE(M_IFAL, "if_al_malloc", "All allocated data for AL ETH driver");

/* move out to some pci header file */
#define	PCI_VENDOR_ID_ANNAPURNA_LABS	0x1c36
#define	PCI_DEVICE_ID_AL_ETH		0x0001
#define	PCI_DEVICE_ID_AL_ETH_ADVANCED	0x0002
#define	PCI_DEVICE_ID_AL_ETH_NIC	0x0003
#define	PCI_DEVICE_ID_AL_ETH_FPGA_NIC	0x0030
#define	PCI_DEVICE_ID_AL_CRYPTO		0x0011
#define	PCI_DEVICE_ID_AL_CRYPTO_VF	0x8011
#define	PCI_DEVICE_ID_AL_RAID_DMA	0x0021
#define	PCI_DEVICE_ID_AL_RAID_DMA_VF	0x8021
#define	PCI_DEVICE_ID_AL_USB		0x0041

#define	MAC_ADDR_STR "%02x:%02x:%02x:%02x:%02x:%02x"
#define	MAC_ADDR(addr) addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]

#define	AL_ETH_MAC_TABLE_UNICAST_IDX_BASE	0
#define	AL_ETH_MAC_TABLE_UNICAST_MAX_COUNT	4
#define	AL_ETH_MAC_TABLE_ALL_MULTICAST_IDX	(AL_ETH_MAC_TABLE_UNICAST_IDX_BASE + \
						 AL_ETH_MAC_TABLE_UNICAST_MAX_COUNT)

#define	AL_ETH_MAC_TABLE_DROP_IDX		(AL_ETH_FWD_MAC_NUM - 1)
#define	AL_ETH_MAC_TABLE_BROADCAST_IDX		(AL_ETH_MAC_TABLE_DROP_IDX - 1)

#define	AL_ETH_THASH_UDMA_SHIFT		0
#define	AL_ETH_THASH_UDMA_MASK		(0xF << AL_ETH_THASH_UDMA_SHIFT)

#define	AL_ETH_THASH_Q_SHIFT		4
#define	AL_ETH_THASH_Q_MASK		(0x3 << AL_ETH_THASH_Q_SHIFT)

/* the following defines should be moved to hal */
#define	AL_ETH_FSM_ENTRY_IPV4_TCP		0
#define	AL_ETH_FSM_ENTRY_IPV4_UDP		1
#define	AL_ETH_FSM_ENTRY_IPV6_TCP		2
#define	AL_ETH_FSM_ENTRY_IPV6_UDP		3
#define	AL_ETH_FSM_ENTRY_IPV6_NO_UDP_TCP	4
#define	AL_ETH_FSM_ENTRY_IPV4_NO_UDP_TCP	5

/* FSM DATA format */
#define	AL_ETH_FSM_DATA_OUTER_2_TUPLE	0
#define	AL_ETH_FSM_DATA_OUTER_4_TUPLE	1
#define	AL_ETH_FSM_DATA_INNER_2_TUPLE	2
#define	AL_ETH_FSM_DATA_INNER_4_TUPLE	3

#define	AL_ETH_FSM_DATA_HASH_SEL	(1 << 2)

#define	AL_ETH_FSM_DATA_DEFAULT_Q	0
#define	AL_ETH_FSM_DATA_DEFAULT_UDMA	0

#define	AL_BR_SIZE	512
#define	AL_TSO_SIZE	65500
#define	AL_DEFAULT_MTU	1500

#define	CSUM_OFFLOAD		(CSUM_IP|CSUM_TCP|CSUM_UDP|CSUM_SCTP)

#define	AL_IP_ALIGNMENT_OFFSET	2

#define	SFP_I2C_ADDR		0x50

#define	AL_MASK_GROUP_A_INT	0x7
#define	AL_MASK_GROUP_B_INT	0xF
#define	AL_MASK_GROUP_C_INT	0xF
#define	AL_MASK_GROUP_D_INT	0xFFFFFFFF

#define	AL_REG_OFFSET_FORWARD_INTR	(0x1800000 + 0x1210)
#define	AL_EN_FORWARD_INTR	0x1FFFF
#define	AL_DIS_FORWARD_INTR	0

#define	AL_M2S_MASK_INIT	0x480
#define	AL_S2M_MASK_INIT	0x1E0
#define	AL_M2S_S2M_MASK_NOT_INT	(0x3f << 25)

#define	AL_10BASE_T_SPEED	10
#define	AL_100BASE_TX_SPEED	100
#define	AL_1000BASE_T_SPEED	1000

static devclass_t al_devclass;

#define	AL_RX_LOCK_INIT(_sc)	mtx_init(&((_sc)->if_rx_lock), "ALRXL", "ALRXL", MTX_DEF)
#define	AL_RX_LOCK(_sc)		mtx_lock(&((_sc)->if_rx_lock))
#define	AL_RX_UNLOCK(_sc)	mtx_unlock(&((_sc)->if_rx_lock))

/* helper functions */
static int al_is_device_supported(device_t);

static void al_eth_init_rings(struct al_eth_adapter *);
static void al_eth_flow_ctrl_disable(struct al_eth_adapter *);
int al_eth_fpga_read_pci_config(void *, int, uint32_t *);
int al_eth_fpga_write_pci_config(void *, int, uint32_t);
int al_eth_read_pci_config(void *, int, uint32_t *);
int al_eth_write_pci_config(void *, int, uint32_t);
void al_eth_irq_config(uint32_t *, uint32_t);
void al_eth_forward_int_config(uint32_t *, uint32_t);
static void al_eth_start_xmit(void *, int);
static void al_eth_rx_recv_work(void *, int);
static int al_eth_up(struct al_eth_adapter *);
static void al_eth_down(struct al_eth_adapter *);
static void al_eth_interrupts_unmask(struct al_eth_adapter *);
static void al_eth_interrupts_mask(struct al_eth_adapter *);
static int al_eth_check_mtu(struct al_eth_adapter *, int);
static uint64_t al_get_counter(struct ifnet *, ift_counter);
static void al_eth_req_rx_buff_size(struct al_eth_adapter *, int);
static int al_eth_board_params_init(struct al_eth_adapter *);
static int al_media_update(struct ifnet *);
static void al_media_status(struct ifnet *, struct ifmediareq *);
static int al_eth_function_reset(struct al_eth_adapter *);
static int al_eth_hw_init_adapter(struct al_eth_adapter *);
static void al_eth_serdes_init(struct al_eth_adapter *);
static void al_eth_lm_config(struct al_eth_adapter *);
static int al_eth_hw_init(struct al_eth_adapter *);

static void al_tick_stats(void *);

/* ifnet entry points */
static void al_init(void *);
static int al_mq_start(struct ifnet *, struct mbuf *);
static void al_qflush(struct ifnet *);
static int al_ioctl(struct ifnet * ifp, u_long, caddr_t);

/* bus entry points */
static int al_probe(device_t);
static int al_attach(device_t);
static int al_detach(device_t);
static int al_shutdown(device_t);

/* mii bus support routines */
static int al_miibus_readreg(device_t, int, int);
static int al_miibus_writereg(device_t, int, int, int);
static void al_miibus_statchg(device_t);
static void al_miibus_linkchg(device_t);

struct al_eth_adapter* g_adapters[16];
uint32_t g_adapters_count;

/* flag for napi-like mbuf processing, controlled from sysctl */
static int napi = 0;

static device_method_t al_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		al_probe),
	DEVMETHOD(device_attach,	al_attach),
	DEVMETHOD(device_detach,	al_detach),
	DEVMETHOD(device_shutdown,	al_shutdown),

	DEVMETHOD(miibus_readreg,	al_miibus_readreg),
	DEVMETHOD(miibus_writereg,	al_miibus_writereg),
	DEVMETHOD(miibus_statchg,	al_miibus_statchg),
	DEVMETHOD(miibus_linkchg,	al_miibus_linkchg),
	{ 0, 0 }
};

static driver_t al_driver = {
	"al",
	al_methods,
	sizeof(struct al_eth_adapter),
};

DRIVER_MODULE(al, pci, al_driver, al_devclass, 0, 0);
DRIVER_MODULE(miibus, al, miibus_driver, miibus_devclass, 0, 0);

static int
al_probe(device_t dev)
{
	if ((al_is_device_supported(dev)) != 0) {
		device_set_desc(dev, "al");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
al_attach(device_t dev)
{
	struct al_eth_adapter *adapter;
	struct sysctl_oid_list *child;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	struct ifnet *ifp;
	uint32_t dev_id;
	uint32_t rev_id;
	int bar_udma;
	int bar_mac;
	int bar_ec;
	int err;

	err = 0;
	ifp = NULL;
	dev_id = rev_id = 0;
	ctx = device_get_sysctl_ctx(dev);
	tree = SYSCTL_PARENT(device_get_sysctl_tree(dev));
	child = SYSCTL_CHILDREN(tree);

	if (g_adapters_count == 0) {
		SYSCTL_ADD_INT(ctx, child, OID_AUTO, "napi",
		    CTLFLAG_RW, &napi, 0, "Use pseudo-napi mechanism");
	}
	adapter = device_get_softc(dev);
	adapter->dev = dev;
	adapter->board_type = ALPINE_INTEGRATED;
	snprintf(adapter->name, AL_ETH_NAME_MAX_LEN, "%s",
	    device_get_nameunit(dev));
	AL_RX_LOCK_INIT(adapter);

	g_adapters[g_adapters_count] = adapter;

	bar_udma = PCIR_BAR(AL_ETH_UDMA_BAR);
	adapter->udma_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &bar_udma, RF_ACTIVE);
	if (adapter->udma_res == NULL) {
		device_printf(adapter->dev,
		    "could not allocate memory resources for DMA.\n");
		err = ENOMEM;
		goto err_res_dma;
	}
	adapter->udma_base = al_bus_dma_to_va(rman_get_bustag(adapter->udma_res),
	    rman_get_bushandle(adapter->udma_res));
	bar_mac = PCIR_BAR(AL_ETH_MAC_BAR);
	adapter->mac_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &bar_mac, RF_ACTIVE);
	if (adapter->mac_res == NULL) {
		device_printf(adapter->dev,
		    "could not allocate memory resources for MAC.\n");
		err = ENOMEM;
		goto err_res_mac;
	}
	adapter->mac_base = al_bus_dma_to_va(rman_get_bustag(adapter->mac_res),
	    rman_get_bushandle(adapter->mac_res));

	bar_ec = PCIR_BAR(AL_ETH_EC_BAR);
	adapter->ec_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &bar_ec,
	    RF_ACTIVE);
	if (adapter->ec_res == NULL) {
		device_printf(adapter->dev,
		    "could not allocate memory resources for EC.\n");
		err = ENOMEM;
		goto err_res_ec;
	}
	adapter->ec_base = al_bus_dma_to_va(rman_get_bustag(adapter->ec_res),
	    rman_get_bushandle(adapter->ec_res));

	adapter->netdev = ifp = if_alloc(IFT_ETHER);

	adapter->netdev->if_link_state = LINK_STATE_DOWN;

	ifp->if_softc = adapter;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	ifp->if_flags = ifp->if_drv_flags;
	ifp->if_flags |= IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST | IFF_ALLMULTI;
	ifp->if_transmit = al_mq_start;
	ifp->if_qflush = al_qflush;
	ifp->if_ioctl = al_ioctl;
	ifp->if_init = al_init;
	ifp->if_get_counter = al_get_counter;
	ifp->if_mtu = AL_DEFAULT_MTU;

	adapter->if_flags = ifp->if_flags;

	ifp->if_capabilities = ifp->if_capenable = 0;

	ifp->if_capabilities |= IFCAP_HWCSUM |
	    IFCAP_HWCSUM_IPV6 | IFCAP_TSO |
	    IFCAP_LRO | IFCAP_JUMBO_MTU;

	ifp->if_capenable = ifp->if_capabilities;

	adapter->id_number = g_adapters_count;

	if (adapter->board_type == ALPINE_INTEGRATED) {
		dev_id = pci_get_device(adapter->dev);
		rev_id = pci_get_revid(adapter->dev);
	} else {
		al_eth_fpga_read_pci_config(adapter->internal_pcie_base,
		    PCIR_DEVICE, &dev_id);
		al_eth_fpga_read_pci_config(adapter->internal_pcie_base,
		    PCIR_REVID, &rev_id);
	}

	adapter->dev_id = dev_id;
	adapter->rev_id = rev_id;

	/* set default ring sizes */
	adapter->tx_ring_count = AL_ETH_DEFAULT_TX_SW_DESCS;
	adapter->tx_descs_count = AL_ETH_DEFAULT_TX_HW_DESCS;
	adapter->rx_ring_count = AL_ETH_DEFAULT_RX_DESCS;
	adapter->rx_descs_count = AL_ETH_DEFAULT_RX_DESCS;

	adapter->num_tx_queues = AL_ETH_NUM_QUEUES;
	adapter->num_rx_queues = AL_ETH_NUM_QUEUES;

	adapter->small_copy_len	= AL_ETH_DEFAULT_SMALL_PACKET_LEN;
	adapter->link_poll_interval = AL_ETH_DEFAULT_LINK_POLL_INTERVAL;
	adapter->max_rx_buff_alloc_size = AL_ETH_DEFAULT_MAX_RX_BUFF_ALLOC_SIZE;

	al_eth_req_rx_buff_size(adapter, adapter->netdev->if_mtu);

	adapter->link_config.force_1000_base_x = AL_ETH_DEFAULT_FORCE_1000_BASEX;

	err = al_eth_board_params_init(adapter);
	if (err != 0)
		goto err;

	if (adapter->mac_mode == AL_ETH_MAC_MODE_10GbE_Serial) {
		ifmedia_init(&adapter->media, IFM_IMASK,
		    al_media_update, al_media_status);
		ifmedia_add(&adapter->media, IFM_ETHER | IFM_1000_LX, 0, NULL);
		ifmedia_add(&adapter->media, IFM_ETHER | IFM_10G_LR, 0, NULL);
		ifmedia_add(&adapter->media, IFM_ETHER | IFM_AUTO, 0, NULL);
		ifmedia_set(&adapter->media, IFM_ETHER | IFM_AUTO);
	}

	al_eth_function_reset(adapter);

	err = al_eth_hw_init_adapter(adapter);
	if (err != 0)
		goto err;

	al_eth_init_rings(adapter);
	g_adapters_count++;

	al_eth_lm_config(adapter);
	mtx_init(&adapter->stats_mtx, "AlStatsMtx", NULL, MTX_DEF);
	mtx_init(&adapter->wd_mtx, "AlWdMtx", NULL, MTX_DEF);
	callout_init_mtx(&adapter->stats_callout, &adapter->stats_mtx, 0);
	callout_init_mtx(&adapter->wd_callout, &adapter->wd_mtx, 0);

	ether_ifattach(ifp, adapter->mac_addr);
	ifp->if_mtu = AL_DEFAULT_MTU;

	if (adapter->mac_mode == AL_ETH_MAC_MODE_RGMII) {
		al_eth_hw_init(adapter);

		/* Attach PHY(s) */
		err = mii_attach(adapter->dev, &adapter->miibus, adapter->netdev,
		    al_media_update, al_media_status, BMSR_DEFCAPMASK, 0,
		    MII_OFFSET_ANY, 0);
		if (err != 0) {
			device_printf(adapter->dev, "attaching PHYs failed\n");
			return (err);
		}

		adapter->mii = device_get_softc(adapter->miibus);
	}

	return (err);

err:
	bus_release_resource(dev, SYS_RES_MEMORY, bar_ec, adapter->ec_res);
err_res_ec:
	bus_release_resource(dev, SYS_RES_MEMORY, bar_mac, adapter->mac_res);
err_res_mac:
	bus_release_resource(dev, SYS_RES_MEMORY, bar_udma, adapter->udma_res);
err_res_dma:
	return (err);
}

static int
al_detach(device_t dev)
{
	struct al_eth_adapter *adapter;

	adapter = device_get_softc(dev);
	ether_ifdetach(adapter->netdev);

	mtx_destroy(&adapter->stats_mtx);
	mtx_destroy(&adapter->wd_mtx);

	al_eth_down(adapter);

	bus_release_resource(dev, SYS_RES_IRQ,    0, adapter->irq_res);
	bus_release_resource(dev, SYS_RES_MEMORY, 0, adapter->ec_res);
	bus_release_resource(dev, SYS_RES_MEMORY, 0, adapter->mac_res);
	bus_release_resource(dev, SYS_RES_MEMORY, 0, adapter->udma_res);

	return (0);
}

int
al_eth_fpga_read_pci_config(void *handle, int where, uint32_t *val)
{

	/* handle is the base address of the adapter */
	*val = al_reg_read32((void*)((u_long)handle + where));

	return (0);
}

int
al_eth_fpga_write_pci_config(void *handle, int where, uint32_t val)
{

	/* handle is the base address of the adapter */
	al_reg_write32((void*)((u_long)handle + where), val);
	return (0);
}

int
al_eth_read_pci_config(void *handle, int where, uint32_t *val)
{

	/* handle is a pci_dev */
	*val = pci_read_config((device_t)handle, where, sizeof(*val));
	return (0);
}

int
al_eth_write_pci_config(void *handle, int where, uint32_t val)
{

	/* handle is a pci_dev */
	pci_write_config((device_t)handle, where, val, sizeof(val));
	return (0);
}

void
al_eth_irq_config(uint32_t *offset, uint32_t value)
{

	al_reg_write32_relaxed(offset, value);
}

void
al_eth_forward_int_config(uint32_t *offset, uint32_t value)
{

	al_reg_write32(offset, value);
}

static void
al_eth_serdes_init(struct al_eth_adapter *adapter)
{
	void __iomem	*serdes_base;

	adapter->serdes_init = false;

	serdes_base = alpine_serdes_resource_get(adapter->serdes_grp);
	if (serdes_base == NULL) {
		device_printf(adapter->dev, "serdes_base get failed!\n");
		return;
	}

	serdes_base = al_bus_dma_to_va(serdes_tag, serdes_base);

	al_serdes_handle_grp_init(serdes_base, adapter->serdes_grp,
	    &adapter->serdes_obj);

	adapter->serdes_init = true;
}

static void
al_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *paddr;

	paddr = arg;
	*paddr = segs->ds_addr;
}

static int
al_dma_alloc_coherent(struct device *dev, bus_dma_tag_t *tag, bus_dmamap_t *map,
    bus_addr_t *baddr, void **vaddr, uint32_t size)
{
	int ret;
	uint32_t maxsize = ((size - 1)/PAGE_SIZE + 1) * PAGE_SIZE;

	ret = bus_dma_tag_create(bus_get_dma_tag(dev), 8, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    maxsize, 1, maxsize, BUS_DMA_COHERENT, NULL, NULL, tag);
	if (ret != 0) {
		device_printf(dev,
		    "failed to create bus tag, ret = %d\n", ret);
		return (ret);
	}

	ret = bus_dmamem_alloc(*tag, vaddr,
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, map);
	if (ret != 0) {
		device_printf(dev,
		    "failed to allocate dmamem, ret = %d\n", ret);
		return (ret);
	}

	ret = bus_dmamap_load(*tag, *map, *vaddr,
	    size, al_dma_map_addr, baddr, 0);
	if (ret != 0) {
		device_printf(dev,
		    "failed to allocate bus_dmamap_load, ret = %d\n", ret);
		return (ret);
	}

	return (0);
}

static void
al_dma_free_coherent(bus_dma_tag_t tag, bus_dmamap_t map, void *vaddr)
{

	bus_dmamap_unload(tag, map);
	bus_dmamem_free(tag, vaddr, map);
	bus_dma_tag_destroy(tag);
}

static void
al_eth_mac_table_unicast_add(struct al_eth_adapter *adapter,
    uint8_t idx, uint8_t *addr, uint8_t udma_mask)
{
	struct al_eth_fwd_mac_table_entry entry = { { 0 } };

	memcpy(entry.addr, adapter->mac_addr, sizeof(adapter->mac_addr));

	memset(entry.mask, 0xff, sizeof(entry.mask));
	entry.rx_valid = true;
	entry.tx_valid = false;
	entry.udma_mask = udma_mask;
	entry.filter = false;

	device_printf_dbg(adapter->dev,
	    "%s: [%d]: addr "MAC_ADDR_STR" mask "MAC_ADDR_STR"\n",
	    __func__, idx, MAC_ADDR(entry.addr), MAC_ADDR(entry.mask));

	al_eth_fwd_mac_table_set(&adapter->hal_adapter, idx, &entry);
}

static void
al_eth_mac_table_all_multicast_add(struct al_eth_adapter *adapter, uint8_t idx,
    uint8_t udma_mask)
{
	struct al_eth_fwd_mac_table_entry entry = { { 0 } };

	memset(entry.addr, 0x00, sizeof(entry.addr));
	memset(entry.mask, 0x00, sizeof(entry.mask));
	entry.mask[0] |= 1;
	entry.addr[0] |= 1;

	entry.rx_valid = true;
	entry.tx_valid = false;
	entry.udma_mask = udma_mask;
	entry.filter = false;

	device_printf_dbg(adapter->dev,
	    "%s: [%d]: addr "MAC_ADDR_STR" mask "MAC_ADDR_STR"\n",
	    __func__, idx, MAC_ADDR(entry.addr), MAC_ADDR(entry.mask));

	al_eth_fwd_mac_table_set(&adapter->hal_adapter, idx, &entry);
}

static void
al_eth_mac_table_broadcast_add(struct al_eth_adapter *adapter,
    uint8_t idx, uint8_t udma_mask)
{
	struct al_eth_fwd_mac_table_entry entry = { { 0 } };

	memset(entry.addr, 0xff, sizeof(entry.addr));
	memset(entry.mask, 0xff, sizeof(entry.mask));

	entry.rx_valid = true;
	entry.tx_valid = false;
	entry.udma_mask = udma_mask;
	entry.filter = false;

	device_printf_dbg(adapter->dev,
	    "%s: [%d]: addr "MAC_ADDR_STR" mask "MAC_ADDR_STR"\n",
	    __func__, idx, MAC_ADDR(entry.addr), MAC_ADDR(entry.mask));

	al_eth_fwd_mac_table_set(&adapter->hal_adapter, idx, &entry);
}

static void
al_eth_mac_table_promiscuous_set(struct al_eth_adapter *adapter,
    boolean_t promiscuous)
{
	struct al_eth_fwd_mac_table_entry entry = { { 0 } };

	memset(entry.addr, 0x00, sizeof(entry.addr));
	memset(entry.mask, 0x00, sizeof(entry.mask));

	entry.rx_valid = true;
	entry.tx_valid = false;
	entry.udma_mask = (promiscuous) ? 1 : 0;
	entry.filter = (promiscuous) ? false : true;

	device_printf_dbg(adapter->dev, "%s: %s promiscuous mode\n",
	    __func__, (promiscuous) ? "enter" : "exit");

	al_eth_fwd_mac_table_set(&adapter->hal_adapter,
	    AL_ETH_MAC_TABLE_DROP_IDX, &entry);
}

static void
al_eth_set_thash_table_entry(struct al_eth_adapter *adapter, uint8_t idx,
    uint8_t udma, uint32_t queue)
{

	if (udma != 0)
		panic("only UDMA0 is supporter");

	if (queue >= AL_ETH_NUM_QUEUES)
		panic("invalid queue number");

	al_eth_thash_table_set(&adapter->hal_adapter, idx, udma, queue);
}

/* init FSM, no tunneling supported yet, if packet is tcp/udp over ipv4/ipv6, use 4 tuple hash */
static void
al_eth_fsm_table_init(struct al_eth_adapter *adapter)
{
	uint32_t val;
	int i;

	for (i = 0; i < AL_ETH_RX_FSM_TABLE_SIZE; i++) {
		uint8_t outer_type = AL_ETH_FSM_ENTRY_OUTER(i);
		switch (outer_type) {
		case AL_ETH_FSM_ENTRY_IPV4_TCP:
		case AL_ETH_FSM_ENTRY_IPV4_UDP:
		case AL_ETH_FSM_ENTRY_IPV6_TCP:
		case AL_ETH_FSM_ENTRY_IPV6_UDP:
			val = AL_ETH_FSM_DATA_OUTER_4_TUPLE |
			    AL_ETH_FSM_DATA_HASH_SEL;
			break;
		case AL_ETH_FSM_ENTRY_IPV6_NO_UDP_TCP:
		case AL_ETH_FSM_ENTRY_IPV4_NO_UDP_TCP:
			val = AL_ETH_FSM_DATA_OUTER_2_TUPLE |
			    AL_ETH_FSM_DATA_HASH_SEL;
			break;
		default:
			val = AL_ETH_FSM_DATA_DEFAULT_Q |
			    AL_ETH_FSM_DATA_DEFAULT_UDMA;
		}
		al_eth_fsm_table_set(&adapter->hal_adapter, i, val);
	}
}

static void
al_eth_mac_table_entry_clear(struct al_eth_adapter *adapter,
    uint8_t idx)
{
	struct al_eth_fwd_mac_table_entry entry = { { 0 } };

	device_printf_dbg(adapter->dev, "%s: clear entry %d\n", __func__, idx);

	al_eth_fwd_mac_table_set(&adapter->hal_adapter, idx, &entry);
}

static int
al_eth_hw_init_adapter(struct al_eth_adapter *adapter)
{
	struct al_eth_adapter_params *params = &adapter->eth_hal_params;
	int rc;

	/* params->dev_id = adapter->dev_id; */
	params->rev_id = adapter->rev_id;
	params->udma_id = 0;
	params->enable_rx_parser = 1; /* enable rx epe parser*/
	params->udma_regs_base = adapter->udma_base; /* UDMA register base address */
	params->ec_regs_base = adapter->ec_base; /* Ethernet controller registers base address */
	params->mac_regs_base = adapter->mac_base; /* Ethernet MAC registers base address */
	params->name = adapter->name;
	params->serdes_lane = adapter->serdes_lane;

	rc = al_eth_adapter_init(&adapter->hal_adapter, params);
	if (rc != 0)
		device_printf(adapter->dev, "%s failed at hal init!\n",
		    __func__);

	if ((adapter->board_type == ALPINE_NIC) ||
	    (adapter->board_type == ALPINE_FPGA_NIC)) {
		/* in pcie NIC mode, force eth UDMA to access PCIE0 using the vmid */
		struct al_udma_gen_tgtid_conf conf;
		int i;
		for (i = 0; i < DMA_MAX_Q; i++) {
			conf.tx_q_conf[i].queue_en = AL_TRUE;
			conf.tx_q_conf[i].desc_en = AL_FALSE;
			conf.tx_q_conf[i].tgtid = 0x100; /* for access from PCIE0 */
			conf.rx_q_conf[i].queue_en = AL_TRUE;
			conf.rx_q_conf[i].desc_en = AL_FALSE;
			conf.rx_q_conf[i].tgtid = 0x100; /* for access from PCIE0 */
		}
		al_udma_gen_tgtid_conf_set(adapter->udma_base, &conf);
	}

	return (rc);
}

static void
al_eth_lm_config(struct al_eth_adapter *adapter)
{
	struct al_eth_lm_init_params params = {0};

	params.adapter = &adapter->hal_adapter;
	params.serdes_obj = &adapter->serdes_obj;
	params.lane = adapter->serdes_lane;
	params.sfp_detection = adapter->sfp_detection_needed;
	if (adapter->sfp_detection_needed == true) {
		params.sfp_bus_id = adapter->i2c_adapter_id;
		params.sfp_i2c_addr = SFP_I2C_ADDR;
	}

	if (adapter->sfp_detection_needed == false) {
		switch (adapter->mac_mode) {
		case AL_ETH_MAC_MODE_10GbE_Serial:
			if ((adapter->lt_en != 0) && (adapter->an_en != 0))
				params.default_mode = AL_ETH_LM_MODE_10G_DA;
			else
				params.default_mode = AL_ETH_LM_MODE_10G_OPTIC;
			break;
		case AL_ETH_MAC_MODE_SGMII:
			params.default_mode = AL_ETH_LM_MODE_1G;
			break;
		default:
			params.default_mode = AL_ETH_LM_MODE_10G_DA;
		}
	} else
		params.default_mode = AL_ETH_LM_MODE_10G_DA;

	params.link_training = adapter->lt_en;
	params.rx_equal = true;
	params.static_values = !adapter->dont_override_serdes;
	params.i2c_context = adapter;
	params.kr_fec_enable = false;

	params.retimer_exist = adapter->retimer.exist;
	params.retimer_bus_id = adapter->retimer.bus_id;
	params.retimer_i2c_addr = adapter->retimer.i2c_addr;
	params.retimer_channel = adapter->retimer.channel;

	al_eth_lm_init(&adapter->lm_context, &params);
}

static int
al_eth_board_params_init(struct al_eth_adapter *adapter)
{

	if (adapter->board_type == ALPINE_NIC) {
		adapter->mac_mode = AL_ETH_MAC_MODE_10GbE_Serial;
		adapter->sfp_detection_needed = false;
		adapter->phy_exist = false;
		adapter->an_en = false;
		adapter->lt_en = false;
		adapter->ref_clk_freq = AL_ETH_REF_FREQ_375_MHZ;
		adapter->mdio_freq = AL_ETH_DEFAULT_MDIO_FREQ_KHZ;
	} else if (adapter->board_type == ALPINE_FPGA_NIC) {
		adapter->mac_mode = AL_ETH_MAC_MODE_SGMII;
		adapter->sfp_detection_needed = false;
		adapter->phy_exist = false;
		adapter->an_en = false;
		adapter->lt_en = false;
		adapter->ref_clk_freq = AL_ETH_REF_FREQ_375_MHZ;
		adapter->mdio_freq = AL_ETH_DEFAULT_MDIO_FREQ_KHZ;
	} else {
		struct al_eth_board_params params;
		int rc;

		adapter->auto_speed = false;

		rc = al_eth_board_params_get(adapter->mac_base, &params);
		if (rc != 0) {
			device_printf(adapter->dev,
			    "board info not available\n");
			return (-1);
		}

		adapter->phy_exist = params.phy_exist == TRUE;
		adapter->phy_addr = params.phy_mdio_addr;
		adapter->an_en = params.autoneg_enable;
		adapter->lt_en = params.kr_lt_enable;
		adapter->serdes_grp = params.serdes_grp;
		adapter->serdes_lane = params.serdes_lane;
		adapter->sfp_detection_needed = params.sfp_plus_module_exist;
		adapter->i2c_adapter_id = params.i2c_adapter_id;
		adapter->ref_clk_freq = params.ref_clk_freq;
		adapter->dont_override_serdes = params.dont_override_serdes;
		adapter->link_config.active_duplex = !params.half_duplex;
		adapter->link_config.autoneg = !params.an_disable;
		adapter->link_config.force_1000_base_x = params.force_1000_base_x;
		adapter->retimer.exist = params.retimer_exist;
		adapter->retimer.bus_id = params.retimer_bus_id;
		adapter->retimer.i2c_addr = params.retimer_i2c_addr;
		adapter->retimer.channel = params.retimer_channel;

		switch (params.speed) {
		default:
			device_printf(adapter->dev,
			    "%s: invalid speed (%d)\n", __func__, params.speed);
		case AL_ETH_BOARD_1G_SPEED_1000M:
			adapter->link_config.active_speed = 1000;
			break;
		case AL_ETH_BOARD_1G_SPEED_100M:
			adapter->link_config.active_speed = 100;
			break;
		case AL_ETH_BOARD_1G_SPEED_10M:
			adapter->link_config.active_speed = 10;
			break;
		}

		switch (params.mdio_freq) {
		default:
			device_printf(adapter->dev,
			    "%s: invalid mdio freq (%d)\n", __func__,
			    params.mdio_freq);
		case AL_ETH_BOARD_MDIO_FREQ_2_5_MHZ:
			adapter->mdio_freq = AL_ETH_DEFAULT_MDIO_FREQ_KHZ;
			break;
		case AL_ETH_BOARD_MDIO_FREQ_1_MHZ:
			adapter->mdio_freq = AL_ETH_MDIO_FREQ_1000_KHZ;
			break;
		}

		switch (params.media_type) {
		case AL_ETH_BOARD_MEDIA_TYPE_RGMII:
			if (params.sfp_plus_module_exist == TRUE)
				/* Backward compatibility */
				adapter->mac_mode = AL_ETH_MAC_MODE_SGMII;
			else
				adapter->mac_mode = AL_ETH_MAC_MODE_RGMII;

			adapter->use_lm = false;
			break;
		case AL_ETH_BOARD_MEDIA_TYPE_SGMII:
			adapter->mac_mode = AL_ETH_MAC_MODE_SGMII;
			adapter->use_lm = true;
			break;
		case AL_ETH_BOARD_MEDIA_TYPE_10GBASE_SR:
			adapter->mac_mode = AL_ETH_MAC_MODE_10GbE_Serial;
			adapter->use_lm = true;
			break;
		case AL_ETH_BOARD_MEDIA_TYPE_AUTO_DETECT:
			adapter->sfp_detection_needed = TRUE;
			adapter->auto_speed = false;
			adapter->use_lm = true;
			break;
		case AL_ETH_BOARD_MEDIA_TYPE_AUTO_DETECT_AUTO_SPEED:
			adapter->sfp_detection_needed = TRUE;
			adapter->auto_speed = true;
			adapter->mac_mode_set = false;
			adapter->use_lm = true;

			adapter->mac_mode = AL_ETH_MAC_MODE_10GbE_Serial;
			break;
		default:
			device_printf(adapter->dev,
			    "%s: unsupported media type %d\n",
			    __func__, params.media_type);
			return (-1);
		}

		device_printf(adapter->dev,
		    "Board info: phy exist %s. phy addr %d. mdio freq %u Khz. "
		    "SFP connected %s. media %d\n",
		    params.phy_exist == TRUE ? "Yes" : "No",
		    params.phy_mdio_addr, adapter->mdio_freq,
		    params.sfp_plus_module_exist == TRUE ? "Yes" : "No",
		    params.media_type);
	}

	al_eth_mac_addr_read(adapter->ec_base, 0, adapter->mac_addr);

	return (0);
}

static int
al_eth_function_reset(struct al_eth_adapter *adapter)
{
	struct al_eth_board_params params;
	int rc;

	/* save board params so we restore it after reset */
	al_eth_board_params_get(adapter->mac_base, &params);
	al_eth_mac_addr_read(adapter->ec_base, 0, adapter->mac_addr);
	if (adapter->board_type == ALPINE_INTEGRATED)
		rc = al_eth_flr_rmn(&al_eth_read_pci_config,
		    &al_eth_write_pci_config,
		    adapter->dev, adapter->mac_base);
	else
		rc = al_eth_flr_rmn(&al_eth_fpga_read_pci_config,
		    &al_eth_fpga_write_pci_config,
		    adapter->internal_pcie_base, adapter->mac_base);

	/* restore params */
	al_eth_board_params_set(adapter->mac_base, &params);
	al_eth_mac_addr_store(adapter->ec_base, 0, adapter->mac_addr);

	return (rc);
}

static void
al_eth_init_rings(struct al_eth_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct al_eth_ring *ring = &adapter->tx_ring[i];

		ring->ring_id = i;
		ring->dev = adapter->dev;
		ring->adapter = adapter;
		ring->netdev = adapter->netdev;
		al_udma_q_handle_get(&adapter->hal_adapter.tx_udma, i,
		    &ring->dma_q);
		ring->sw_count = adapter->tx_ring_count;
		ring->hw_count = adapter->tx_descs_count;
		ring->unmask_reg_offset = al_udma_iofic_unmask_offset_get((struct unit_regs *)adapter->udma_base, AL_UDMA_IOFIC_LEVEL_PRIMARY, AL_INT_GROUP_C);
		ring->unmask_val = ~(1 << i);
	}

	for (i = 0; i < adapter->num_rx_queues; i++) {
		struct al_eth_ring *ring = &adapter->rx_ring[i];

		ring->ring_id = i;
		ring->dev = adapter->dev;
		ring->adapter = adapter;
		ring->netdev = adapter->netdev;
		al_udma_q_handle_get(&adapter->hal_adapter.rx_udma, i, &ring->dma_q);
		ring->sw_count = adapter->rx_ring_count;
		ring->hw_count = adapter->rx_descs_count;
		ring->unmask_reg_offset = al_udma_iofic_unmask_offset_get(
		    (struct unit_regs *)adapter->udma_base,
		    AL_UDMA_IOFIC_LEVEL_PRIMARY, AL_INT_GROUP_B);
		ring->unmask_val = ~(1 << i);
	}
}

static void
al_init_locked(void *arg)
{
	struct al_eth_adapter *adapter = arg;
	if_t ifp = adapter->netdev;
	int rc = 0;

	al_eth_down(adapter);
	rc = al_eth_up(adapter);

	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	if (rc == 0)
		ifp->if_drv_flags |= IFF_DRV_RUNNING;
}

static void
al_init(void *arg)
{
	struct al_eth_adapter *adapter = arg;

	al_init_locked(adapter);
}

static inline int
al_eth_alloc_rx_buf(struct al_eth_adapter *adapter,
    struct al_eth_ring *rx_ring,
    struct al_eth_rx_buffer *rx_info)
{
	struct al_buf *al_buf;
	bus_dma_segment_t segs[2];
	int error;
	int nsegs;

	if (rx_info->m != NULL)
		return (0);

	rx_info->data_size = adapter->rx_mbuf_sz;

	AL_RX_LOCK(adapter);

	/* Get mbuf using UMA allocator */
	rx_info->m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR,
	    rx_info->data_size);
	AL_RX_UNLOCK(adapter);

	if (rx_info->m == NULL)
		return (ENOMEM);

	rx_info->m->m_pkthdr.len = rx_info->m->m_len = adapter->rx_mbuf_sz;

	/* Map packets for DMA */
	error = bus_dmamap_load_mbuf_sg(rx_ring->dma_buf_tag, rx_info->dma_map,
	    rx_info->m, segs, &nsegs, BUS_DMA_NOWAIT);
	if (__predict_false(error)) {
		device_printf(rx_ring->dev, "failed to map mbuf, error = %d\n",
		    error);
		m_freem(rx_info->m);
		rx_info->m = NULL;
		return (EFAULT);
	}

	al_buf = &rx_info->al_buf;
	al_buf->addr = segs[0].ds_addr + AL_IP_ALIGNMENT_OFFSET;
	al_buf->len = rx_info->data_size - AL_IP_ALIGNMENT_OFFSET;

	return (0);
}

static int
al_eth_refill_rx_bufs(struct al_eth_adapter *adapter, unsigned int qid,
    unsigned int num)
{
	struct al_eth_ring *rx_ring = &adapter->rx_ring[qid];
	uint16_t next_to_use;
	unsigned int i;

	next_to_use = rx_ring->next_to_use;

	for (i = 0; i < num; i++) {
		int rc;
		struct al_eth_rx_buffer *rx_info =
		    &rx_ring->rx_buffer_info[next_to_use];

		if (__predict_false(al_eth_alloc_rx_buf(adapter,
		    rx_ring, rx_info) < 0)) {
			device_printf(adapter->dev,
			    "failed to alloc buffer for rx queue %d\n", qid);
			break;
		}

		rc = al_eth_rx_buffer_add(rx_ring->dma_q,
		    &rx_info->al_buf, AL_ETH_RX_FLAGS_INT, NULL);
		if (__predict_false(rc)) {
			device_printf(adapter->dev,
			    "failed to add buffer for rx queue %d\n", qid);
			break;
		}

		next_to_use = AL_ETH_RX_RING_IDX_NEXT(rx_ring, next_to_use);
	}

	if (__predict_false(i < num))
		device_printf(adapter->dev,
		    "refilled rx queue %d with %d pages only - available %d\n",
		    qid, i, al_udma_available_get(rx_ring->dma_q));

	if (__predict_true(i))
		al_eth_rx_buffer_action(rx_ring->dma_q, i);

	rx_ring->next_to_use = next_to_use;

	return (i);
}

/*
 * al_eth_refill_all_rx_bufs - allocate all queues Rx buffers
 * @adapter: board private structure
 */
static void
al_eth_refill_all_rx_bufs(struct al_eth_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		al_eth_refill_rx_bufs(adapter, i, AL_ETH_DEFAULT_RX_DESCS - 1);
}

static void
al_eth_tx_do_cleanup(struct al_eth_ring *tx_ring)
{
	unsigned int total_done;
	uint16_t next_to_clean;
	int qid = tx_ring->ring_id;

	total_done = al_eth_comp_tx_get(tx_ring->dma_q);
	device_printf_dbg(tx_ring->dev,
	    "tx_poll: q %d total completed descs %x\n", qid, total_done);
	next_to_clean = tx_ring->next_to_clean;

	while (total_done != 0) {
		struct al_eth_tx_buffer *tx_info;
		struct mbuf *mbuf;

		tx_info = &tx_ring->tx_buffer_info[next_to_clean];
		/* stop if not all descriptors of the packet are completed */
		if (tx_info->tx_descs > total_done)
			break;

		mbuf = tx_info->m;

		tx_info->m = NULL;

		device_printf_dbg(tx_ring->dev,
		    "tx_poll: q %d mbuf %p completed\n", qid, mbuf);

		/* map is no longer required */
		bus_dmamap_unload(tx_ring->dma_buf_tag, tx_info->dma_map);

		m_freem(mbuf);
		total_done -= tx_info->tx_descs;
		next_to_clean = AL_ETH_TX_RING_IDX_NEXT(tx_ring, next_to_clean);
	}

	tx_ring->next_to_clean = next_to_clean;

	device_printf_dbg(tx_ring->dev, "tx_poll: q %d done next to clean %x\n",
	    qid, next_to_clean);

	/*
	 * need to make the rings circular update visible to
	 * al_eth_start_xmit() before checking for netif_queue_stopped().
	 */
	al_smp_data_memory_barrier();
}

static void
al_eth_tx_csum(struct al_eth_ring *tx_ring, struct al_eth_tx_buffer *tx_info,
    struct al_eth_pkt *hal_pkt, struct mbuf *m)
{
	uint32_t mss = m->m_pkthdr.tso_segsz;
	struct ether_vlan_header *eh;
	uint16_t etype;
#ifdef INET
	struct ip *ip;
#endif
#ifdef INET6
	struct ip6_hdr *ip6;
#endif
	struct tcphdr *th = NULL;
	int	ehdrlen, ip_hlen = 0;
	uint8_t	ipproto = 0;
	uint32_t offload = 0;

	if (mss != 0)
		offload = 1;

	if ((m->m_pkthdr.csum_flags & CSUM_TSO) != 0)
		offload = 1;

	if ((m->m_pkthdr.csum_flags & CSUM_OFFLOAD) != 0)
		offload = 1;

	if (offload != 0) {
		struct al_eth_meta_data *meta = &tx_ring->hal_meta;

		if (mss != 0)
			hal_pkt->flags |= (AL_ETH_TX_FLAGS_TSO |
			    AL_ETH_TX_FLAGS_L4_CSUM);
		else
			hal_pkt->flags |= (AL_ETH_TX_FLAGS_L4_CSUM |
			    AL_ETH_TX_FLAGS_L4_PARTIAL_CSUM);

		/*
		 * Determine where frame payload starts.
		 * Jump over vlan headers if already present,
		 * helpful for QinQ too.
		 */
		eh = mtod(m, struct ether_vlan_header *);
		if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
			etype = ntohs(eh->evl_proto);
			ehdrlen = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
		} else {
			etype = ntohs(eh->evl_encap_proto);
			ehdrlen = ETHER_HDR_LEN;
		}

		switch (etype) {
#ifdef INET
		case ETHERTYPE_IP:
			ip = (struct ip *)(m->m_data + ehdrlen);
			ip_hlen = ip->ip_hl << 2;
			ipproto = ip->ip_p;
			hal_pkt->l3_proto_idx = AL_ETH_PROTO_ID_IPv4;
			th = (struct tcphdr *)((caddr_t)ip + ip_hlen);
			if (mss != 0)
				hal_pkt->flags |= AL_ETH_TX_FLAGS_IPV4_L3_CSUM;
			if (ipproto == IPPROTO_TCP)
				hal_pkt->l4_proto_idx = AL_ETH_PROTO_ID_TCP;
			else
				hal_pkt->l4_proto_idx = AL_ETH_PROTO_ID_UDP;
			break;
#endif /* INET */
#ifdef INET6
		case ETHERTYPE_IPV6:
			ip6 = (struct ip6_hdr *)(m->m_data + ehdrlen);
			hal_pkt->l3_proto_idx = AL_ETH_PROTO_ID_IPv6;
			ip_hlen = sizeof(struct ip6_hdr);
			th = (struct tcphdr *)((caddr_t)ip6 + ip_hlen);
			ipproto = ip6->ip6_nxt;
			if (ipproto == IPPROTO_TCP)
				hal_pkt->l4_proto_idx = AL_ETH_PROTO_ID_TCP;
			else
				hal_pkt->l4_proto_idx = AL_ETH_PROTO_ID_UDP;
			break;
#endif /* INET6 */
		default:
			break;
		}

		meta->words_valid = 4;
		meta->l3_header_len = ip_hlen;
		meta->l3_header_offset = ehdrlen;
		if (th != NULL)
			meta->l4_header_len = th->th_off; /* this param needed only for TSO */
		meta->mss_idx_sel = 0;			/* check how to select MSS */
		meta->mss_val = mss;
		hal_pkt->meta = meta;
	} else
		hal_pkt->meta = NULL;
}

#define	XMIT_QUEUE_TIMEOUT	100

static void
al_eth_xmit_mbuf(struct al_eth_ring *tx_ring, struct mbuf *m)
{
	struct al_eth_tx_buffer *tx_info;
	int error;
	int nsegs, a;
	uint16_t next_to_use;
	bus_dma_segment_t segs[AL_ETH_PKT_MAX_BUFS + 1];
	struct al_eth_pkt *hal_pkt;
	struct al_buf *al_buf;
	boolean_t remap;

	/* Check if queue is ready */
	if (unlikely(tx_ring->stall) != 0) {
		for (a = 0; a < XMIT_QUEUE_TIMEOUT; a++) {
			if (al_udma_available_get(tx_ring->dma_q) >=
			    (AL_ETH_DEFAULT_TX_HW_DESCS -
			    AL_ETH_TX_WAKEUP_THRESH)) {
				tx_ring->stall = 0;
				break;
			}
			pause("stall", 1);
		}
		if (a == XMIT_QUEUE_TIMEOUT) {
			device_printf(tx_ring->dev,
			    "timeout waiting for queue %d ready!\n",
			    tx_ring->ring_id);
			return;
		} else {
			device_printf_dbg(tx_ring->dev,
			    "queue %d is ready!\n", tx_ring->ring_id);
		}
	}

	next_to_use = tx_ring->next_to_use;
	tx_info = &tx_ring->tx_buffer_info[next_to_use];
	tx_info->m = m;
	hal_pkt = &tx_info->hal_pkt;

	if (m == NULL) {
		device_printf(tx_ring->dev, "mbuf is NULL\n");
		return;
	}

	remap = TRUE;
	/* Map packets for DMA */
retry:
	error = bus_dmamap_load_mbuf_sg(tx_ring->dma_buf_tag, tx_info->dma_map,
	    m, segs, &nsegs, BUS_DMA_NOWAIT);
	if (__predict_false(error)) {
		struct mbuf *m_new;

		if (error == EFBIG) {
			/* Try it again? - one try */
			if (remap == TRUE) {
				remap = FALSE;
				m_new = m_defrag(m, M_NOWAIT);
				if (m_new == NULL) {
					device_printf(tx_ring->dev,
					    "failed to defrag mbuf\n");
					goto exit;
				}
				m = m_new;
				goto retry;
			} else {
				device_printf(tx_ring->dev,
				    "failed to map mbuf, error %d\n", error);
				goto exit;
			}
		} else {
			device_printf(tx_ring->dev,
			    "failed to map mbuf, error %d\n", error);
			goto exit;
		}
	}

	/* set flags and meta data */
	hal_pkt->flags = AL_ETH_TX_FLAGS_INT;
	al_eth_tx_csum(tx_ring, tx_info, hal_pkt, m);

	al_buf = hal_pkt->bufs;
	for (a = 0; a < nsegs; a++) {
		al_buf->addr = segs[a].ds_addr;
		al_buf->len = segs[a].ds_len;

		al_buf++;
	}

	hal_pkt->num_of_bufs = nsegs;

	/* prepare the packet's descriptors to dma engine */
	tx_info->tx_descs = al_eth_tx_pkt_prepare(tx_ring->dma_q, hal_pkt);

	if (tx_info->tx_descs == 0)
		goto exit;

	/*
	 * stop the queue when no more space available, the packet can have up
	 * to AL_ETH_PKT_MAX_BUFS + 1 buffers and a meta descriptor
	 */
	if (unlikely(al_udma_available_get(tx_ring->dma_q) <
	    (AL_ETH_PKT_MAX_BUFS + 2))) {
		tx_ring->stall = 1;
		device_printf_dbg(tx_ring->dev, "stall, stopping queue %d...\n",
		    tx_ring->ring_id);
		al_data_memory_barrier();
	}

	tx_ring->next_to_use = AL_ETH_TX_RING_IDX_NEXT(tx_ring, next_to_use);

	/* trigger the dma engine */
	al_eth_tx_dma_action(tx_ring->dma_q, tx_info->tx_descs);
	return;

exit:
	m_freem(m);
}

static void
al_eth_tx_cmpl_work(void *arg, int pending)
{
	struct al_eth_ring *tx_ring = arg;

	if (napi != 0) {
		tx_ring->cmpl_is_running = 1;
		al_data_memory_barrier();
	}

	al_eth_tx_do_cleanup(tx_ring);

	if (napi != 0) {
		tx_ring->cmpl_is_running = 0;
		al_data_memory_barrier();
	}
	/* all work done, enable IRQs */
	al_eth_irq_config(tx_ring->unmask_reg_offset, tx_ring->unmask_val);
}

static int
al_eth_tx_cmlp_irq_filter(void *arg)
{
	struct al_eth_ring *tx_ring = arg;

	/* Interrupt should be auto-masked upon arrival */

	device_printf_dbg(tx_ring->dev, "%s for ring ID = %d\n", __func__,
	    tx_ring->ring_id);

	/*
	 * For napi, if work is not running, schedule it. Always schedule
	 * for casual (non-napi) packet handling.
	 */
	if ((napi == 0) || (napi && tx_ring->cmpl_is_running == 0))
		taskqueue_enqueue(tx_ring->cmpl_tq, &tx_ring->cmpl_task);

	/* Do not run bottom half */
	return (FILTER_HANDLED);
}

static int
al_eth_rx_recv_irq_filter(void *arg)
{
	struct al_eth_ring *rx_ring = arg;

	/* Interrupt should be auto-masked upon arrival */

	device_printf_dbg(rx_ring->dev, "%s for ring ID = %d\n", __func__,
	    rx_ring->ring_id);

	/*
	 * For napi, if work is not running, schedule it. Always schedule
	 * for casual (non-napi) packet handling.
	 */
	if ((napi == 0) || (napi && rx_ring->enqueue_is_running == 0))
		taskqueue_enqueue(rx_ring->enqueue_tq, &rx_ring->enqueue_task);

	/* Do not run bottom half */
	return (FILTER_HANDLED);
}

/*
 * al_eth_rx_checksum - indicate in mbuf if hw indicated a good cksum
 * @adapter: structure containing adapter specific data
 * @hal_pkt: HAL structure for the packet
 * @mbuf: mbuf currently being received and modified
 */
static inline void
al_eth_rx_checksum(struct al_eth_adapter *adapter,
    struct al_eth_pkt *hal_pkt, struct mbuf *mbuf)
{

	/* if IPv4 and error */
	if (unlikely((adapter->netdev->if_capenable & IFCAP_RXCSUM) &&
	    (hal_pkt->l3_proto_idx == AL_ETH_PROTO_ID_IPv4) &&
	    (hal_pkt->flags & AL_ETH_RX_FLAGS_L3_CSUM_ERR))) {
		device_printf(adapter->dev,"rx ipv4 header checksum error\n");
		return;
	}

	/* if IPv6 and error */
	if (unlikely((adapter->netdev->if_capenable & IFCAP_RXCSUM_IPV6) &&
	    (hal_pkt->l3_proto_idx == AL_ETH_PROTO_ID_IPv6) &&
	    (hal_pkt->flags & AL_ETH_RX_FLAGS_L3_CSUM_ERR))) {
		device_printf(adapter->dev,"rx ipv6 header checksum error\n");
		return;
	}

	/* if TCP/UDP */
	if (likely((hal_pkt->l4_proto_idx == AL_ETH_PROTO_ID_TCP) ||
	   (hal_pkt->l4_proto_idx == AL_ETH_PROTO_ID_UDP))) {
		if (unlikely(hal_pkt->flags & AL_ETH_RX_FLAGS_L4_CSUM_ERR)) {
			device_printf_dbg(adapter->dev, "rx L4 checksum error\n");

			/* TCP/UDP checksum error */
			mbuf->m_pkthdr.csum_flags = 0;
		} else {
			device_printf_dbg(adapter->dev, "rx checksum correct\n");

			/* IP Checksum Good */
			mbuf->m_pkthdr.csum_flags = CSUM_IP_CHECKED;
			mbuf->m_pkthdr.csum_flags |= CSUM_IP_VALID;
		}
	}
}

static struct mbuf*
al_eth_rx_mbuf(struct al_eth_adapter *adapter,
    struct al_eth_ring *rx_ring, struct al_eth_pkt *hal_pkt,
    unsigned int descs, uint16_t *next_to_clean)
{
	struct mbuf *mbuf;
	struct al_eth_rx_buffer *rx_info =
	    &rx_ring->rx_buffer_info[*next_to_clean];
	unsigned int len;

	len = hal_pkt->bufs[0].len;
	device_printf_dbg(adapter->dev, "rx_info %p data %p\n", rx_info,
	   rx_info->m);

	if (rx_info->m == NULL) {
		*next_to_clean = AL_ETH_RX_RING_IDX_NEXT(rx_ring,
		    *next_to_clean);
		return (NULL);
	}

	mbuf = rx_info->m;
	mbuf->m_pkthdr.len = len;
	mbuf->m_len = len;
	mbuf->m_pkthdr.rcvif = rx_ring->netdev;
	mbuf->m_flags |= M_PKTHDR;

	if (len <= adapter->small_copy_len) {
		struct mbuf *smbuf;
		device_printf_dbg(adapter->dev, "rx small packet. len %d\n", len);

		AL_RX_LOCK(adapter);
		smbuf = m_gethdr(M_NOWAIT, MT_DATA);
		AL_RX_UNLOCK(adapter);
		if (__predict_false(smbuf == NULL)) {
			device_printf(adapter->dev, "smbuf is NULL\n");
			return (NULL);
		}

		smbuf->m_data = smbuf->m_data + AL_IP_ALIGNMENT_OFFSET;
		memcpy(smbuf->m_data, mbuf->m_data + AL_IP_ALIGNMENT_OFFSET, len);

		smbuf->m_len = len;
		smbuf->m_pkthdr.rcvif = rx_ring->netdev;

		/* first desc of a non-ps chain */
		smbuf->m_flags |= M_PKTHDR;
		smbuf->m_pkthdr.len = smbuf->m_len;

		*next_to_clean = AL_ETH_RX_RING_IDX_NEXT(rx_ring,
		    *next_to_clean);

		return (smbuf);
	}
	mbuf->m_data = mbuf->m_data + AL_IP_ALIGNMENT_OFFSET;

	/* Unmap the buffer */
	bus_dmamap_unload(rx_ring->dma_buf_tag, rx_info->dma_map);

	rx_info->m = NULL;
	*next_to_clean = AL_ETH_RX_RING_IDX_NEXT(rx_ring, *next_to_clean);

	return (mbuf);
}

static void
al_eth_rx_recv_work(void *arg, int pending)
{
	struct al_eth_ring *rx_ring = arg;
	struct mbuf *mbuf;
	struct lro_entry *queued;
	unsigned int qid = rx_ring->ring_id;
	struct al_eth_pkt *hal_pkt = &rx_ring->hal_pkt;
	uint16_t next_to_clean = rx_ring->next_to_clean;
	uint32_t refill_required;
	uint32_t refill_actual;
	uint32_t do_if_input;

	if (napi != 0) {
		rx_ring->enqueue_is_running = 1;
		al_data_memory_barrier();
	}

	do {
		unsigned int descs;

		descs = al_eth_pkt_rx(rx_ring->dma_q, hal_pkt);
		if (unlikely(descs == 0))
			break;

		device_printf_dbg(rx_ring->dev, "rx_poll: q %d got packet "
		    "from hal. descs %d\n", qid, descs);
		device_printf_dbg(rx_ring->dev, "rx_poll: q %d flags %x. "
		    "l3 proto %d l4 proto %d\n", qid, hal_pkt->flags,
		    hal_pkt->l3_proto_idx, hal_pkt->l4_proto_idx);

		/* ignore if detected dma or eth controller errors */
		if ((hal_pkt->flags & (AL_ETH_RX_ERROR |
		    AL_UDMA_CDESC_ERROR)) != 0) {
			device_printf(rx_ring->dev, "receive packet with error. "
			    "flags = 0x%x\n", hal_pkt->flags);
			next_to_clean = AL_ETH_RX_RING_IDX_ADD(rx_ring,
			    next_to_clean, descs);
			continue;
		}

		/* allocate mbuf and fill it */
		mbuf = al_eth_rx_mbuf(rx_ring->adapter, rx_ring, hal_pkt, descs,
		    &next_to_clean);

		/* exit if we failed to retrieve a buffer */
		if (unlikely(mbuf == NULL)) {
			next_to_clean = AL_ETH_RX_RING_IDX_ADD(rx_ring,
			    next_to_clean, descs);
			break;
		}

		if (__predict_true(rx_ring->netdev->if_capenable & IFCAP_RXCSUM ||
		    rx_ring->netdev->if_capenable & IFCAP_RXCSUM_IPV6)) {
			al_eth_rx_checksum(rx_ring->adapter, hal_pkt, mbuf);
		}

#if __FreeBSD_version >= 800000
		mbuf->m_pkthdr.flowid = qid;
		M_HASHTYPE_SET(mbuf, M_HASHTYPE_OPAQUE);
#endif

		/*
		 * LRO is only for IP/TCP packets and TCP checksum of the packet
		 * should be computed by hardware.
		 */
		do_if_input = 1;
		if ((rx_ring->lro_enabled != 0) &&
		    ((mbuf->m_pkthdr.csum_flags & CSUM_IP_VALID) != 0) &&
		    hal_pkt->l4_proto_idx == AL_ETH_PROTO_ID_TCP) {
			/*
			 * Send to the stack if:
			 *  - LRO not enabled, or
			 *  - no LRO resources, or
			 *  - lro enqueue fails
			 */
			if (rx_ring->lro.lro_cnt != 0) {
				if (tcp_lro_rx(&rx_ring->lro, mbuf, 0) == 0)
					do_if_input = 0;
			}
		}

		if (do_if_input)
			(*rx_ring->netdev->if_input)(rx_ring->netdev, mbuf);

	} while (1);

	rx_ring->next_to_clean = next_to_clean;

	refill_required = al_udma_available_get(rx_ring->dma_q);
	refill_actual = al_eth_refill_rx_bufs(rx_ring->adapter, qid,
	    refill_required);

	if (unlikely(refill_actual < refill_required)) {
		device_printf_dbg(rx_ring->dev,
		    "%s: not filling rx queue %d\n", __func__, qid);
	}

	while (((queued = LIST_FIRST(&rx_ring->lro.lro_active)) != NULL)) {
		LIST_REMOVE(queued, next);
		tcp_lro_flush(&rx_ring->lro, queued);
	}

	if (napi != 0) {
		rx_ring->enqueue_is_running = 0;
		al_data_memory_barrier();
	}
	/* unmask irq */
	al_eth_irq_config(rx_ring->unmask_reg_offset, rx_ring->unmask_val);
}

static void
al_eth_start_xmit(void *arg, int pending)
{
	struct al_eth_ring *tx_ring = arg;
	struct mbuf *mbuf;

	if (napi != 0) {
		tx_ring->enqueue_is_running = 1;
		al_data_memory_barrier();
	}

	while (1) {
		mtx_lock(&tx_ring->br_mtx);
		mbuf = drbr_dequeue(NULL, tx_ring->br);
		mtx_unlock(&tx_ring->br_mtx);

		if (mbuf == NULL)
			break;

		al_eth_xmit_mbuf(tx_ring, mbuf);
	}

	if (napi != 0) {
		tx_ring->enqueue_is_running = 0;
		al_data_memory_barrier();
		while (1) {
			mtx_lock(&tx_ring->br_mtx);
			mbuf = drbr_dequeue(NULL, tx_ring->br);
			mtx_unlock(&tx_ring->br_mtx);
			if (mbuf == NULL)
				break;
			al_eth_xmit_mbuf(tx_ring, mbuf);
		}
	}
}

static int
al_mq_start(struct ifnet *ifp, struct mbuf *m)
{
	struct al_eth_adapter *adapter = ifp->if_softc;
	struct al_eth_ring *tx_ring;
	int i;
	int ret;

	/* Which queue to use */
	if (M_HASHTYPE_GET(m) != M_HASHTYPE_NONE)
		i = m->m_pkthdr.flowid % adapter->num_tx_queues;
	else
		i = curcpu % adapter->num_tx_queues;

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING|IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING) {
		return (EFAULT);
	}

	tx_ring = &adapter->tx_ring[i];

	device_printf_dbg(adapter->dev, "dgb start() - assuming link is active, "
	    "sending packet to queue %d\n", i);

	ret = drbr_enqueue(ifp, tx_ring->br, m);

	/*
	 * For napi, if work is not running, schedule it. Always schedule
	 * for casual (non-napi) packet handling.
	 */
	if ((napi == 0) || ((napi != 0) && (tx_ring->enqueue_is_running == 0)))
		taskqueue_enqueue(tx_ring->enqueue_tq, &tx_ring->enqueue_task);

	return (ret);
}

static void
al_qflush(struct ifnet * ifp)
{

	/* unused */
}

static inline void
al_eth_flow_ctrl_init(struct al_eth_adapter *adapter)
{
	uint8_t default_flow_ctrl;

	default_flow_ctrl = AL_ETH_FLOW_CTRL_TX_PAUSE;
	default_flow_ctrl |= AL_ETH_FLOW_CTRL_RX_PAUSE;

	adapter->link_config.flow_ctrl_supported = default_flow_ctrl;
}

static int
al_eth_flow_ctrl_config(struct al_eth_adapter *adapter)
{
	struct al_eth_flow_control_params *flow_ctrl_params;
	uint8_t active = adapter->link_config.flow_ctrl_active;
	int i;

	flow_ctrl_params = &adapter->flow_ctrl_params;

	flow_ctrl_params->type = AL_ETH_FLOW_CONTROL_TYPE_LINK_PAUSE;
	flow_ctrl_params->obay_enable =
	    ((active & AL_ETH_FLOW_CTRL_RX_PAUSE) != 0);
	flow_ctrl_params->gen_enable =
	    ((active & AL_ETH_FLOW_CTRL_TX_PAUSE) != 0);

	flow_ctrl_params->rx_fifo_th_high = AL_ETH_FLOW_CTRL_RX_FIFO_TH_HIGH;
	flow_ctrl_params->rx_fifo_th_low = AL_ETH_FLOW_CTRL_RX_FIFO_TH_LOW;
	flow_ctrl_params->quanta = AL_ETH_FLOW_CTRL_QUANTA;
	flow_ctrl_params->quanta_th = AL_ETH_FLOW_CTRL_QUANTA_TH;

	/* map priority to queue index, queue id = priority/2 */
	for (i = 0; i < AL_ETH_FWD_PRIO_TABLE_NUM; i++)
		flow_ctrl_params->prio_q_map[0][i] =  1 << (i >> 1);

	al_eth_flow_control_config(&adapter->hal_adapter, flow_ctrl_params);

	return (0);
}

static void
al_eth_flow_ctrl_enable(struct al_eth_adapter *adapter)
{

	/*
	 * change the active configuration to the default / force by ethtool
	 * and call to configure
	 */
	adapter->link_config.flow_ctrl_active =
	    adapter->link_config.flow_ctrl_supported;

	al_eth_flow_ctrl_config(adapter);
}

static void
al_eth_flow_ctrl_disable(struct al_eth_adapter *adapter)
{

	adapter->link_config.flow_ctrl_active = 0;
	al_eth_flow_ctrl_config(adapter);
}

static int
al_eth_hw_init(struct al_eth_adapter *adapter)
{
	int rc;

	rc = al_eth_hw_init_adapter(adapter);
	if (rc != 0)
		return (rc);

	rc = al_eth_mac_config(&adapter->hal_adapter, adapter->mac_mode);
	if (rc < 0) {
		device_printf(adapter->dev, "%s failed to configure mac!\n",
		    __func__);
		return (rc);
	}

	if ((adapter->mac_mode == AL_ETH_MAC_MODE_SGMII) ||
	    (adapter->mac_mode == AL_ETH_MAC_MODE_RGMII &&
	     adapter->phy_exist == FALSE)) {
		rc = al_eth_mac_link_config(&adapter->hal_adapter,
		    adapter->link_config.force_1000_base_x,
		    adapter->link_config.autoneg,
		    adapter->link_config.active_speed,
		    adapter->link_config.active_duplex);
		if (rc != 0) {
			device_printf(adapter->dev,
			    "%s failed to configure link parameters!\n",
			    __func__);
			return (rc);
		}
	}

	rc = al_eth_mdio_config(&adapter->hal_adapter,
	    AL_ETH_MDIO_TYPE_CLAUSE_22, TRUE /* shared_mdio_if */,
	    adapter->ref_clk_freq, adapter->mdio_freq);
	if (rc != 0) {
		device_printf(adapter->dev, "%s failed at mdio config!\n",
		    __func__);
		return (rc);
	}

	al_eth_flow_ctrl_init(adapter);

	return (rc);
}

static int
al_eth_hw_stop(struct al_eth_adapter *adapter)
{

	al_eth_mac_stop(&adapter->hal_adapter);

	/*
	 * wait till pending rx packets written and UDMA becomes idle,
	 * the MAC has ~10KB fifo, 10us should be enought time for the
	 * UDMA to write to the memory
	 */
	DELAY(10);

	al_eth_adapter_stop(&adapter->hal_adapter);

	adapter->flags |= AL_ETH_FLAG_RESET_REQUESTED;

	/* disable flow ctrl to avoid pause packets*/
	al_eth_flow_ctrl_disable(adapter);

	return (0);
}

/*
 * al_eth_intr_intx_all - Legacy Interrupt Handler for all interrupts
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 */
static int
al_eth_intr_intx_all(void *data)
{
	struct al_eth_adapter *adapter = data;

	struct unit_regs __iomem *regs_base =
	    (struct unit_regs __iomem *)adapter->udma_base;
	uint32_t reg;

	reg = al_udma_iofic_read_cause(regs_base, AL_UDMA_IOFIC_LEVEL_PRIMARY,
	    AL_INT_GROUP_A);
	if (likely(reg))
		device_printf_dbg(adapter->dev, "%s group A cause %x\n",
		    __func__, reg);

	if (unlikely(reg & AL_INT_GROUP_A_GROUP_D_SUM)) {
		struct al_iofic_grp_ctrl __iomem *sec_ints_base;
		uint32_t cause_d =  al_udma_iofic_read_cause(regs_base,
		    AL_UDMA_IOFIC_LEVEL_PRIMARY, AL_INT_GROUP_D);

		sec_ints_base =
		    &regs_base->gen.interrupt_regs.secondary_iofic_ctrl[0];
		if (cause_d != 0) {
			device_printf_dbg(adapter->dev,
			    "got interrupt from group D. cause %x\n", cause_d);

			cause_d = al_iofic_read_cause(sec_ints_base,
			    AL_INT_GROUP_A);
			device_printf(adapter->dev,
			    "secondary A cause %x\n", cause_d);

			cause_d = al_iofic_read_cause(sec_ints_base,
			    AL_INT_GROUP_B);

			device_printf_dbg(adapter->dev,
			    "secondary B cause %x\n", cause_d);
		}
	}
	if ((reg & AL_INT_GROUP_A_GROUP_B_SUM) != 0 ) {
		uint32_t cause_b = al_udma_iofic_read_cause(regs_base,
		    AL_UDMA_IOFIC_LEVEL_PRIMARY, AL_INT_GROUP_B);
		int qid;
		device_printf_dbg(adapter->dev, "secondary B cause %x\n",
		    cause_b);
		for (qid = 0; qid < adapter->num_rx_queues; qid++) {
			if (cause_b & (1 << qid)) {
				/* mask */
				al_udma_iofic_mask(
				    (struct unit_regs __iomem *)adapter->udma_base,
				    AL_UDMA_IOFIC_LEVEL_PRIMARY,
				    AL_INT_GROUP_B, 1 << qid);
			}
		}
	}
	if ((reg & AL_INT_GROUP_A_GROUP_C_SUM) != 0) {
		uint32_t cause_c = al_udma_iofic_read_cause(regs_base,
		    AL_UDMA_IOFIC_LEVEL_PRIMARY, AL_INT_GROUP_C);
		int qid;
		device_printf_dbg(adapter->dev, "secondary C cause %x\n", cause_c);
		for (qid = 0; qid < adapter->num_tx_queues; qid++) {
			if ((cause_c & (1 << qid)) != 0) {
				al_udma_iofic_mask(
				    (struct unit_regs __iomem *)adapter->udma_base,
				    AL_UDMA_IOFIC_LEVEL_PRIMARY,
				    AL_INT_GROUP_C, 1 << qid);
			}
		}
	}

	al_eth_tx_cmlp_irq_filter(adapter->tx_ring);

	return (0);
}

static int
al_eth_intr_msix_all(void *data)
{
	struct al_eth_adapter *adapter = data;

	device_printf_dbg(adapter->dev, "%s\n", __func__);
	return (0);
}

static int
al_eth_intr_msix_mgmt(void *data)
{
	struct al_eth_adapter *adapter = data;

	device_printf_dbg(adapter->dev, "%s\n", __func__);
	return (0);
}

static int
al_eth_enable_msix(struct al_eth_adapter *adapter)
{
	int i, msix_vecs, rc, count;

	device_printf_dbg(adapter->dev, "%s\n", __func__);
	msix_vecs = 1 + adapter->num_rx_queues + adapter->num_tx_queues;

	device_printf_dbg(adapter->dev,
	    "Try to enable MSIX, vector numbers = %d\n", msix_vecs);

	adapter->msix_entries = malloc(msix_vecs*sizeof(*adapter->msix_entries),
	    M_IFAL, M_ZERO | M_WAITOK);

	if (adapter->msix_entries == NULL) {
		device_printf_dbg(adapter->dev, "failed to allocate"
		    " msix_entries %d\n", msix_vecs);
		rc = ENOMEM;
		goto exit;
	}

	/* management vector (GROUP_A) @2*/
	adapter->msix_entries[AL_ETH_MGMT_IRQ_IDX].entry = 2;
	adapter->msix_entries[AL_ETH_MGMT_IRQ_IDX].vector = 0;

	/* rx queues start @3 */
	for (i = 0; i < adapter->num_rx_queues; i++) {
		int irq_idx = AL_ETH_RXQ_IRQ_IDX(adapter, i);

		adapter->msix_entries[irq_idx].entry = 3 + i;
		adapter->msix_entries[irq_idx].vector = 0;
	}
	/* tx queues start @7 */
	for (i = 0; i < adapter->num_tx_queues; i++) {
		int irq_idx = AL_ETH_TXQ_IRQ_IDX(adapter, i);

		adapter->msix_entries[irq_idx].entry = 3 +
		    AL_ETH_MAX_HW_QUEUES + i;
		adapter->msix_entries[irq_idx].vector = 0;
	}

	count = msix_vecs + 2; /* entries start from 2 */
	rc = pci_alloc_msix(adapter->dev, &count);

	if (rc != 0) {
		device_printf_dbg(adapter->dev, "failed to allocate MSIX "
		    "vectors %d\n", msix_vecs+2);
		device_printf_dbg(adapter->dev, "ret = %d\n", rc);
		goto msix_entries_exit;
	}

	if (count != msix_vecs + 2) {
		device_printf_dbg(adapter->dev, "failed to allocate all MSIX "
		    "vectors %d, allocated %d\n", msix_vecs+2, count);
		rc = ENOSPC;
		goto msix_entries_exit;
	}

	for (i = 0; i < msix_vecs; i++)
	    adapter->msix_entries[i].vector = 2 + 1 + i;

	device_printf_dbg(adapter->dev, "successfully enabled MSIX,"
	    " vectors %d\n", msix_vecs);

	adapter->msix_vecs = msix_vecs;
	adapter->flags |= AL_ETH_FLAG_MSIX_ENABLED;
	goto exit;

msix_entries_exit:
	adapter->msix_vecs = 0;
	free(adapter->msix_entries, M_IFAL);
	adapter->msix_entries = NULL;

exit:
	return (rc);
}

static int
al_eth_setup_int_mode(struct al_eth_adapter *adapter)
{
	int i, rc;

	rc = al_eth_enable_msix(adapter);
	if (rc != 0) {
		device_printf(adapter->dev, "Failed to enable MSIX mode.\n");
		return (rc);
	}

	adapter->irq_vecs = max(1, adapter->msix_vecs);
	/* single INTX mode */
	if (adapter->msix_vecs == 0) {
		snprintf(adapter->irq_tbl[AL_ETH_MGMT_IRQ_IDX].name,
		    AL_ETH_IRQNAME_SIZE, "al-eth-intx-all@pci:%s",
		    device_get_name(adapter->dev));
		adapter->irq_tbl[AL_ETH_MGMT_IRQ_IDX].handler =
		    al_eth_intr_intx_all;
		/* IRQ vector will be resolved from device resources */
		adapter->irq_tbl[AL_ETH_MGMT_IRQ_IDX].vector = 0;
		adapter->irq_tbl[AL_ETH_MGMT_IRQ_IDX].data = adapter;

		device_printf(adapter->dev, "%s and vector %d \n", __func__,
		    adapter->irq_tbl[AL_ETH_MGMT_IRQ_IDX].vector);

		return (0);
	}
	/* single MSI-X mode */
	if (adapter->msix_vecs == 1) {
		snprintf(adapter->irq_tbl[AL_ETH_MGMT_IRQ_IDX].name,
		    AL_ETH_IRQNAME_SIZE, "al-eth-msix-all@pci:%s",
		    device_get_name(adapter->dev));
		adapter->irq_tbl[AL_ETH_MGMT_IRQ_IDX].handler =
		    al_eth_intr_msix_all;
		adapter->irq_tbl[AL_ETH_MGMT_IRQ_IDX].vector =
		    adapter->msix_entries[AL_ETH_MGMT_IRQ_IDX].vector;
		adapter->irq_tbl[AL_ETH_MGMT_IRQ_IDX].data = adapter;

		return (0);
	}
	/* MSI-X per queue */
	snprintf(adapter->irq_tbl[AL_ETH_MGMT_IRQ_IDX].name, AL_ETH_IRQNAME_SIZE,
	    "al-eth-msix-mgmt@pci:%s", device_get_name(adapter->dev));
	adapter->irq_tbl[AL_ETH_MGMT_IRQ_IDX].handler = al_eth_intr_msix_mgmt;

	adapter->irq_tbl[AL_ETH_MGMT_IRQ_IDX].data = adapter;
	adapter->irq_tbl[AL_ETH_MGMT_IRQ_IDX].vector =
	    adapter->msix_entries[AL_ETH_MGMT_IRQ_IDX].vector;

	for (i = 0; i < adapter->num_rx_queues; i++) {
		int irq_idx = AL_ETH_RXQ_IRQ_IDX(adapter, i);

		snprintf(adapter->irq_tbl[irq_idx].name, AL_ETH_IRQNAME_SIZE,
		    "al-eth-rx-comp-%d@pci:%s", i,
		    device_get_name(adapter->dev));
		adapter->irq_tbl[irq_idx].handler = al_eth_rx_recv_irq_filter;
		adapter->irq_tbl[irq_idx].data = &adapter->rx_ring[i];
		adapter->irq_tbl[irq_idx].vector =
		    adapter->msix_entries[irq_idx].vector;
	}

	for (i = 0; i < adapter->num_tx_queues; i++) {
		int irq_idx = AL_ETH_TXQ_IRQ_IDX(adapter, i);

		snprintf(adapter->irq_tbl[irq_idx].name,
		    AL_ETH_IRQNAME_SIZE, "al-eth-tx-comp-%d@pci:%s", i,
		    device_get_name(adapter->dev));
		adapter->irq_tbl[irq_idx].handler = al_eth_tx_cmlp_irq_filter;
		adapter->irq_tbl[irq_idx].data = &adapter->tx_ring[i];
		adapter->irq_tbl[irq_idx].vector =
		    adapter->msix_entries[irq_idx].vector;
	}

	return (0);
}

static void
__al_eth_free_irq(struct al_eth_adapter *adapter)
{
	struct al_eth_irq *irq;
	int i, rc;

	for (i = 0; i < adapter->irq_vecs; i++) {
		irq = &adapter->irq_tbl[i];
		if (irq->requested != 0) {
			device_printf_dbg(adapter->dev, "tear down irq: %d\n",
			    irq->vector);
			rc = bus_teardown_intr(adapter->dev, irq->res,
			    irq->cookie);
			if (rc != 0)
				device_printf(adapter->dev, "failed to tear "
				    "down irq: %d\n", irq->vector);

		}
		irq->requested = 0;
	}
}

static void
al_eth_free_irq(struct al_eth_adapter *adapter)
{
	struct al_eth_irq *irq;
	int i, rc;
#ifdef CONFIG_RFS_ACCEL
	if (adapter->msix_vecs >= 1) {
		free_irq_cpu_rmap(adapter->netdev->rx_cpu_rmap);
		adapter->netdev->rx_cpu_rmap = NULL;
	}
#endif

	__al_eth_free_irq(adapter);

	for (i = 0; i < adapter->irq_vecs; i++) {
		irq = &adapter->irq_tbl[i];
		if (irq->res == NULL)
			continue;
		device_printf_dbg(adapter->dev, "release resource irq: %d\n",
		    irq->vector);
		rc = bus_release_resource(adapter->dev, SYS_RES_IRQ, irq->vector,
		    irq->res);
		irq->res = NULL;
		if (rc != 0)
			device_printf(adapter->dev, "dev has no parent while "
			    "releasing res for irq: %d\n", irq->vector);
	}

	pci_release_msi(adapter->dev);

	adapter->flags &= ~AL_ETH_FLAG_MSIX_ENABLED;

	adapter->msix_vecs = 0;
	free(adapter->msix_entries, M_IFAL);
	adapter->msix_entries = NULL;
}

static int
al_eth_request_irq(struct al_eth_adapter *adapter)
{
	unsigned long flags;
	struct al_eth_irq *irq;
	int rc = 0, i, v;

	if ((adapter->flags & AL_ETH_FLAG_MSIX_ENABLED) != 0)
		flags = RF_ACTIVE;
	else
		flags = RF_ACTIVE | RF_SHAREABLE;

	for (i = 0; i < adapter->irq_vecs; i++) {
		irq = &adapter->irq_tbl[i];

		if (irq->requested != 0)
			continue;

		irq->res = bus_alloc_resource_any(adapter->dev, SYS_RES_IRQ,
		    &irq->vector, flags);
		if (irq->res == NULL) {
			device_printf(adapter->dev, "could not allocate "
			    "irq vector=%d\n", irq->vector);
			rc = ENXIO;
			goto exit_res;
		}

		if ((rc = bus_setup_intr(adapter->dev, irq->res,
		    INTR_TYPE_NET | INTR_MPSAFE, irq->handler,
		    NULL, irq->data, &irq->cookie)) != 0) {
			device_printf(adapter->dev, "failed to register "
			    "interrupt handler for irq %ju: %d\n",
			    (uintmax_t)rman_get_start(irq->res), rc);
			goto exit_intr;
		}
		irq->requested = 1;
	}
	goto exit;

exit_intr:
	v = i - 1; /* -1 because we omit the operation that failed */
	while (v-- >= 0) {
		int bti;
		irq = &adapter->irq_tbl[v];
		bti = bus_teardown_intr(adapter->dev, irq->res, irq->cookie);
		if (bti != 0) {
			device_printf(adapter->dev, "failed to tear "
			    "down irq: %d\n", irq->vector);
		}

		irq->requested = 0;
		device_printf_dbg(adapter->dev, "exit_intr: releasing irq %d\n",
		    irq->vector);
	}

exit_res:
	v = i - 1; /* -1 because we omit the operation that failed */
	while (v-- >= 0) {
		int brr;
		irq = &adapter->irq_tbl[v];
		device_printf_dbg(adapter->dev, "exit_res: releasing resource"
		    " for irq %d\n", irq->vector);
		brr = bus_release_resource(adapter->dev, SYS_RES_IRQ,
		    irq->vector, irq->res);
		if (brr != 0)
			device_printf(adapter->dev, "dev has no parent while "
			    "releasing res for irq: %d\n", irq->vector);
		irq->res = NULL;
	}

exit:
	return (rc);
}

/**
 * al_eth_setup_tx_resources - allocate Tx resources (Descriptors)
 * @adapter: network interface device structure
 * @qid: queue index
 *
 * Return 0 on success, negative on failure
 **/
static int
al_eth_setup_tx_resources(struct al_eth_adapter *adapter, int qid)
{
	struct al_eth_ring *tx_ring = &adapter->tx_ring[qid];
	struct device *dev = tx_ring->dev;
	struct al_udma_q_params *q_params = &tx_ring->q_params;
	int size;
	int ret;

	if (adapter->up)
		return (0);

	size = sizeof(struct al_eth_tx_buffer) * tx_ring->sw_count;

	tx_ring->tx_buffer_info = malloc(size, M_IFAL, M_ZERO | M_WAITOK);
	if (tx_ring->tx_buffer_info == NULL)
		return (ENOMEM);

	tx_ring->descs_size = tx_ring->hw_count * sizeof(union al_udma_desc);
	q_params->size = tx_ring->hw_count;

	ret = al_dma_alloc_coherent(dev, &q_params->desc_phy_base_tag,
	    (bus_dmamap_t *)&q_params->desc_phy_base_map,
	    (bus_addr_t *)&q_params->desc_phy_base,
	    (void**)&q_params->desc_base, tx_ring->descs_size);
	if (ret != 0) {
		device_printf(dev, "failed to al_dma_alloc_coherent,"
		    " ret = %d\n", ret);
		return (ENOMEM);
	}

	if (q_params->desc_base == NULL)
		return (ENOMEM);

	device_printf_dbg(dev, "Initializing ring queues %d\n", qid);

	/* Allocate Ring Queue */
	mtx_init(&tx_ring->br_mtx, "AlRingMtx", NULL, MTX_DEF);
	tx_ring->br = buf_ring_alloc(AL_BR_SIZE, M_DEVBUF, M_WAITOK,
	    &tx_ring->br_mtx);
	if (tx_ring->br == NULL) {
		device_printf(dev, "Critical Failure setting up buf ring\n");
		return (ENOMEM);
	}

	/* Allocate taskqueues */
	TASK_INIT(&tx_ring->enqueue_task, 0, al_eth_start_xmit, tx_ring);
	tx_ring->enqueue_tq = taskqueue_create_fast("al_tx_enque", M_NOWAIT,
	    taskqueue_thread_enqueue, &tx_ring->enqueue_tq);
	taskqueue_start_threads(&tx_ring->enqueue_tq, 1, PI_NET, "%s txeq",
	    device_get_nameunit(adapter->dev));
	TASK_INIT(&tx_ring->cmpl_task, 0, al_eth_tx_cmpl_work, tx_ring);
	tx_ring->cmpl_tq = taskqueue_create_fast("al_tx_cmpl", M_NOWAIT,
	    taskqueue_thread_enqueue, &tx_ring->cmpl_tq);
	taskqueue_start_threads(&tx_ring->cmpl_tq, 1, PI_REALTIME, "%s txcq",
	    device_get_nameunit(adapter->dev));

	/* Setup DMA descriptor areas. */
	ret = bus_dma_tag_create(bus_get_dma_tag(dev),
	    1, 0,			/* alignment, bounds */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    AL_TSO_SIZE,		/* maxsize */
	    AL_ETH_PKT_MAX_BUFS,	/* nsegments */
	    PAGE_SIZE,			/* maxsegsize */
	    0,				/* flags */
	    NULL,			/* lockfunc */
	    NULL,			/* lockfuncarg */
	    &tx_ring->dma_buf_tag);

	if (ret != 0) {
		device_printf(dev,"Unable to allocate dma_buf_tag, ret = %d\n",
		    ret);
		return (ret);
	}

	for (size = 0; size < tx_ring->sw_count; size++) {
		ret = bus_dmamap_create(tx_ring->dma_buf_tag, 0,
		    &tx_ring->tx_buffer_info[size].dma_map);
		if (ret != 0) {
			device_printf(dev, "Unable to map DMA TX "
			    "buffer memory [iter=%d]\n", size);
			return (ret);
		}
	}

	/* completion queue not used for tx */
	q_params->cdesc_base = NULL;
	/* size in bytes of the udma completion ring descriptor */
	q_params->cdesc_size = 8;
	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;

	return (0);
}

/*
 * al_eth_free_tx_resources - Free Tx Resources per Queue
 * @adapter: network interface device structure
 * @qid: queue index
 *
 * Free all transmit software resources
 */
static void
al_eth_free_tx_resources(struct al_eth_adapter *adapter, int qid)
{
	struct al_eth_ring *tx_ring = &adapter->tx_ring[qid];
	struct al_udma_q_params *q_params = &tx_ring->q_params;
	int size;

	/* At this point interrupts' handlers must be deactivated */
	while (taskqueue_cancel(tx_ring->cmpl_tq, &tx_ring->cmpl_task, NULL))
		taskqueue_drain(tx_ring->cmpl_tq, &tx_ring->cmpl_task);

	taskqueue_free(tx_ring->cmpl_tq);
	while (taskqueue_cancel(tx_ring->enqueue_tq,
	    &tx_ring->enqueue_task, NULL)) {
		taskqueue_drain(tx_ring->enqueue_tq, &tx_ring->enqueue_task);
	}

	taskqueue_free(tx_ring->enqueue_tq);

	if (tx_ring->br != NULL) {
		drbr_flush(adapter->netdev, tx_ring->br);
		buf_ring_free(tx_ring->br, M_DEVBUF);
	}

	for (size = 0; size < tx_ring->sw_count; size++) {
		m_freem(tx_ring->tx_buffer_info[size].m);
		tx_ring->tx_buffer_info[size].m = NULL;

		bus_dmamap_unload(tx_ring->dma_buf_tag,
		    tx_ring->tx_buffer_info[size].dma_map);
		bus_dmamap_destroy(tx_ring->dma_buf_tag,
		    tx_ring->tx_buffer_info[size].dma_map);
	}
	bus_dma_tag_destroy(tx_ring->dma_buf_tag);

	free(tx_ring->tx_buffer_info, M_IFAL);
	tx_ring->tx_buffer_info = NULL;

	mtx_destroy(&tx_ring->br_mtx);

	/* if not set, then don't free */
	if (q_params->desc_base == NULL)
		return;

	al_dma_free_coherent(q_params->desc_phy_base_tag,
	    q_params->desc_phy_base_map, q_params->desc_base);

	q_params->desc_base = NULL;
}

/*
 * al_eth_free_all_tx_resources - Free Tx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all transmit software resources
 */
static void
al_eth_free_all_tx_resources(struct al_eth_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		if (adapter->tx_ring[i].q_params.desc_base)
			al_eth_free_tx_resources(adapter, i);
}

/*
 * al_eth_setup_rx_resources - allocate Rx resources (Descriptors)
 * @adapter: network interface device structure
 * @qid: queue index
 *
 * Returns 0 on success, negative on failure
 */
static int
al_eth_setup_rx_resources(struct al_eth_adapter *adapter, unsigned int qid)
{
	struct al_eth_ring *rx_ring = &adapter->rx_ring[qid];
	struct device *dev = rx_ring->dev;
	struct al_udma_q_params *q_params = &rx_ring->q_params;
	int size;
	int ret;

	size = sizeof(struct al_eth_rx_buffer) * rx_ring->sw_count;

	/* alloc extra element so in rx path we can always prefetch rx_info + 1 */
	size += 1;

	rx_ring->rx_buffer_info = malloc(size, M_IFAL, M_ZERO | M_WAITOK);
	if (rx_ring->rx_buffer_info == NULL)
		return (ENOMEM);

	rx_ring->descs_size = rx_ring->hw_count * sizeof(union al_udma_desc);
	q_params->size = rx_ring->hw_count;

	ret = al_dma_alloc_coherent(dev, &q_params->desc_phy_base_tag,
	    &q_params->desc_phy_base_map,
	    (bus_addr_t *)&q_params->desc_phy_base,
	    (void**)&q_params->desc_base, rx_ring->descs_size);

	if ((q_params->desc_base == NULL) || (ret != 0))
		return (ENOMEM);

	/* size in bytes of the udma completion ring descriptor */
	q_params->cdesc_size = 16;
	rx_ring->cdescs_size = rx_ring->hw_count * q_params->cdesc_size;
	ret = al_dma_alloc_coherent(dev, &q_params->cdesc_phy_base_tag,
	    &q_params->cdesc_phy_base_map,
	    (bus_addr_t *)&q_params->cdesc_phy_base,
	    (void**)&q_params->cdesc_base, rx_ring->cdescs_size);

	if ((q_params->cdesc_base == NULL) || (ret != 0))
		return (ENOMEM);

	/* Allocate taskqueues */
	TASK_INIT(&rx_ring->enqueue_task, 0, al_eth_rx_recv_work, rx_ring);
	rx_ring->enqueue_tq = taskqueue_create_fast("al_rx_enque", M_NOWAIT,
	    taskqueue_thread_enqueue, &rx_ring->enqueue_tq);
	taskqueue_start_threads(&rx_ring->enqueue_tq, 1, PI_NET, "%s rxeq",
	    device_get_nameunit(adapter->dev));

	/* Setup DMA descriptor areas. */
	ret = bus_dma_tag_create(bus_get_dma_tag(dev),
	    1, 0,			/* alignment, bounds */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    AL_TSO_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    AL_TSO_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL,			/* lockfunc */
	    NULL,			/* lockfuncarg */
	    &rx_ring->dma_buf_tag);

	if (ret != 0) {
		device_printf(dev,"Unable to allocate RX dma_buf_tag\n");
		return (ret);
	}

	for (size = 0; size < rx_ring->sw_count; size++) {
		ret = bus_dmamap_create(rx_ring->dma_buf_tag, 0,
		    &rx_ring->rx_buffer_info[size].dma_map);
		if (ret != 0) {
			device_printf(dev,"Unable to map DMA RX buffer memory\n");
			return (ret);
		}
	}

	/* Zero out the descriptor ring */
	memset(q_params->cdesc_base, 0, rx_ring->cdescs_size);

	/* Create LRO for the ring */
	if ((adapter->netdev->if_capenable & IFCAP_LRO) != 0) {
		int err = tcp_lro_init(&rx_ring->lro);
		if (err != 0) {
			device_printf(adapter->dev,
			    "LRO[%d] Initialization failed!\n", qid);
		} else {
			device_printf_dbg(adapter->dev,
			    "RX Soft LRO[%d] Initialized\n", qid);
			rx_ring->lro_enabled = TRUE;
			rx_ring->lro.ifp = adapter->netdev;
		}
	}

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;

	return (0);
}

/*
 * al_eth_free_rx_resources - Free Rx Resources
 * @adapter: network interface device structure
 * @qid: queue index
 *
 * Free all receive software resources
 */
static void
al_eth_free_rx_resources(struct al_eth_adapter *adapter, unsigned int qid)
{
	struct al_eth_ring *rx_ring = &adapter->rx_ring[qid];
	struct al_udma_q_params *q_params = &rx_ring->q_params;
	int size;

	/* At this point interrupts' handlers must be deactivated */
	while (taskqueue_cancel(rx_ring->enqueue_tq,
	    &rx_ring->enqueue_task, NULL)) {
		taskqueue_drain(rx_ring->enqueue_tq, &rx_ring->enqueue_task);
	}

	taskqueue_free(rx_ring->enqueue_tq);

	for (size = 0; size < rx_ring->sw_count; size++) {
		m_freem(rx_ring->rx_buffer_info[size].m);
		rx_ring->rx_buffer_info[size].m = NULL;
		bus_dmamap_unload(rx_ring->dma_buf_tag,
		    rx_ring->rx_buffer_info[size].dma_map);
		bus_dmamap_destroy(rx_ring->dma_buf_tag,
		    rx_ring->rx_buffer_info[size].dma_map);
	}
	bus_dma_tag_destroy(rx_ring->dma_buf_tag);

	free(rx_ring->rx_buffer_info, M_IFAL);
	rx_ring->rx_buffer_info = NULL;

	/* if not set, then don't free */
	if (q_params->desc_base == NULL)
		return;

	al_dma_free_coherent(q_params->desc_phy_base_tag,
	    q_params->desc_phy_base_map, q_params->desc_base);

	q_params->desc_base = NULL;

	/* if not set, then don't free */
	if (q_params->cdesc_base == NULL)
		return;

	al_dma_free_coherent(q_params->cdesc_phy_base_tag,
	    q_params->cdesc_phy_base_map, q_params->cdesc_base);

	q_params->cdesc_phy_base = 0;

	/* Free LRO resources */
	tcp_lro_free(&rx_ring->lro);
}

/*
 * al_eth_free_all_rx_resources - Free Rx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all receive software resources
 */
static void
al_eth_free_all_rx_resources(struct al_eth_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		if (adapter->rx_ring[i].q_params.desc_base != 0)
			al_eth_free_rx_resources(adapter, i);
}

/*
 * al_eth_setup_all_rx_resources - allocate all queues Rx resources
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 */
static int
al_eth_setup_all_rx_resources(struct al_eth_adapter *adapter)
{
	int i, rc = 0;

	for (i = 0; i < adapter->num_rx_queues; i++) {
		rc = al_eth_setup_rx_resources(adapter, i);
		if (rc == 0)
			continue;

		device_printf(adapter->dev, "Allocation for Rx Queue %u failed\n", i);
		goto err_setup_rx;
	}
	return (0);

err_setup_rx:
	/* rewind the index freeing the rings as we go */
	while (i--)
		al_eth_free_rx_resources(adapter, i);
	return (rc);
}

/*
 * al_eth_setup_all_tx_resources - allocate all queues Tx resources
 * @adapter: private structure
 *
 * Return 0 on success, negative on failure
 */
static int
al_eth_setup_all_tx_resources(struct al_eth_adapter *adapter)
{
	int i, rc = 0;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		rc = al_eth_setup_tx_resources(adapter, i);
		if (rc == 0)
			continue;

		device_printf(adapter->dev,
		    "Allocation for Tx Queue %u failed\n", i);
		goto err_setup_tx;
	}

	return (0);

err_setup_tx:
	/* rewind the index freeing the rings as we go */
	while (i--)
		al_eth_free_tx_resources(adapter, i);

	return (rc);
}

static void
al_eth_disable_int_sync(struct al_eth_adapter *adapter)
{

	/* disable forwarding interrupts from eth through pci end point */
	if ((adapter->board_type == ALPINE_FPGA_NIC) ||
	    (adapter->board_type == ALPINE_NIC)) {
		al_eth_forward_int_config((uint32_t*)adapter->internal_pcie_base +
		    AL_REG_OFFSET_FORWARD_INTR, AL_DIS_FORWARD_INTR);
	}

	/* mask hw interrupts */
	al_eth_interrupts_mask(adapter);
}

static void
al_eth_interrupts_unmask(struct al_eth_adapter *adapter)
{
	uint32_t group_a_mask = AL_INT_GROUP_A_GROUP_D_SUM; /* enable group D summery */
	uint32_t group_b_mask = (1 << adapter->num_rx_queues) - 1;/* bit per Rx q*/
	uint32_t group_c_mask = (1 << adapter->num_tx_queues) - 1;/* bit per Tx q*/
	uint32_t group_d_mask = 3 << 8;
	struct unit_regs __iomem *regs_base =
	    (struct unit_regs __iomem *)adapter->udma_base;

	if (adapter->int_mode == AL_IOFIC_MODE_LEGACY)
		group_a_mask |= AL_INT_GROUP_A_GROUP_B_SUM |
		    AL_INT_GROUP_A_GROUP_C_SUM |
		    AL_INT_GROUP_A_GROUP_D_SUM;

	al_udma_iofic_unmask(regs_base, AL_UDMA_IOFIC_LEVEL_PRIMARY,
	    AL_INT_GROUP_A, group_a_mask);
	al_udma_iofic_unmask(regs_base, AL_UDMA_IOFIC_LEVEL_PRIMARY,
	    AL_INT_GROUP_B, group_b_mask);
	al_udma_iofic_unmask(regs_base, AL_UDMA_IOFIC_LEVEL_PRIMARY,
	    AL_INT_GROUP_C, group_c_mask);
	al_udma_iofic_unmask(regs_base, AL_UDMA_IOFIC_LEVEL_PRIMARY,
	    AL_INT_GROUP_D, group_d_mask);
}

static void
al_eth_interrupts_mask(struct al_eth_adapter *adapter)
{
	struct unit_regs __iomem *regs_base =
	    (struct unit_regs __iomem *)adapter->udma_base;

	/* mask all interrupts */
	al_udma_iofic_mask(regs_base, AL_UDMA_IOFIC_LEVEL_PRIMARY,
	    AL_INT_GROUP_A, AL_MASK_GROUP_A_INT);
	al_udma_iofic_mask(regs_base, AL_UDMA_IOFIC_LEVEL_PRIMARY,
	    AL_INT_GROUP_B, AL_MASK_GROUP_B_INT);
	al_udma_iofic_mask(regs_base, AL_UDMA_IOFIC_LEVEL_PRIMARY,
	    AL_INT_GROUP_C, AL_MASK_GROUP_C_INT);
	al_udma_iofic_mask(regs_base, AL_UDMA_IOFIC_LEVEL_PRIMARY,
	    AL_INT_GROUP_D, AL_MASK_GROUP_D_INT);
}

static int
al_eth_configure_int_mode(struct al_eth_adapter *adapter)
{
	enum al_iofic_mode int_mode;
	uint32_t m2s_errors_disable = AL_M2S_MASK_INIT;
	uint32_t m2s_aborts_disable = AL_M2S_MASK_INIT;
	uint32_t s2m_errors_disable = AL_S2M_MASK_INIT;
	uint32_t s2m_aborts_disable = AL_S2M_MASK_INIT;

	/* single INTX mode */
	if (adapter->msix_vecs == 0)
		int_mode = AL_IOFIC_MODE_LEGACY;
	else if (adapter->msix_vecs > 1)
		int_mode = AL_IOFIC_MODE_MSIX_PER_Q;
	else {
		device_printf(adapter->dev,
		    "udma doesn't support single MSI-X mode yet.\n");
		return (EIO);
	}

	if (adapter->board_type != ALPINE_INTEGRATED) {
		m2s_errors_disable |= AL_M2S_S2M_MASK_NOT_INT;
		m2s_errors_disable |= AL_M2S_S2M_MASK_NOT_INT;
		s2m_aborts_disable |= AL_M2S_S2M_MASK_NOT_INT;
		s2m_aborts_disable |= AL_M2S_S2M_MASK_NOT_INT;
	}

	if (al_udma_iofic_config((struct unit_regs __iomem *)adapter->udma_base,
	    int_mode, m2s_errors_disable, m2s_aborts_disable,
	    s2m_errors_disable, s2m_aborts_disable)) {
		device_printf(adapter->dev,
		    "al_udma_unit_int_config failed!.\n");
		return (EIO);
	}
	adapter->int_mode = int_mode;
	device_printf_dbg(adapter->dev, "using %s interrupt mode\n",
	    int_mode == AL_IOFIC_MODE_LEGACY ? "INTx" :
	    int_mode == AL_IOFIC_MODE_MSIX_PER_Q ? "MSI-X per Queue" : "Unknown");
	/* set interrupt moderation resolution to 15us */
	al_iofic_moder_res_config(&((struct unit_regs *)(adapter->udma_base))->gen.interrupt_regs.main_iofic, AL_INT_GROUP_B, 15);
	al_iofic_moder_res_config(&((struct unit_regs *)(adapter->udma_base))->gen.interrupt_regs.main_iofic, AL_INT_GROUP_C, 15);
	/* by default interrupt coalescing is disabled */
	adapter->tx_usecs = 0;
	adapter->rx_usecs = 0;

	return (0);
}

/*
 * ethtool_rxfh_indir_default - get default value for RX flow hash indirection
 * @index: Index in RX flow hash indirection table
 * @n_rx_rings: Number of RX rings to use
 *
 * This function provides the default policy for RX flow hash indirection.
 */
static inline uint32_t
ethtool_rxfh_indir_default(uint32_t index, uint32_t n_rx_rings)
{

	return (index % n_rx_rings);
}

static void*
al_eth_update_stats(struct al_eth_adapter *adapter)
{
	struct al_eth_mac_stats *mac_stats = &adapter->mac_stats;

	if (adapter->up == 0)
		return (NULL);

	al_eth_mac_stats_get(&adapter->hal_adapter, mac_stats);

	return (NULL);
}

static uint64_t
al_get_counter(struct ifnet *ifp, ift_counter cnt)
{
	struct al_eth_adapter *adapter;
	struct al_eth_mac_stats *mac_stats;
	uint64_t rv;

	adapter = if_getsoftc(ifp);
	mac_stats = &adapter->mac_stats;

	switch (cnt) {
	case IFCOUNTER_IPACKETS:
		return (mac_stats->aFramesReceivedOK); /* including pause frames */
	case IFCOUNTER_OPACKETS:
		return (mac_stats->aFramesTransmittedOK);
	case IFCOUNTER_IBYTES:
		return (mac_stats->aOctetsReceivedOK);
	case IFCOUNTER_OBYTES:
		return (mac_stats->aOctetsTransmittedOK);
	case IFCOUNTER_IMCASTS:
		return (mac_stats->ifInMulticastPkts);
	case IFCOUNTER_OMCASTS:
		return (mac_stats->ifOutMulticastPkts);
	case IFCOUNTER_COLLISIONS:
		return (0);
	case IFCOUNTER_IQDROPS:
		return (mac_stats->etherStatsDropEvents);
	case IFCOUNTER_IERRORS:
		rv = mac_stats->ifInErrors +
		    mac_stats->etherStatsUndersizePkts + /* good but short */
		    mac_stats->etherStatsFragments + /* short and bad*/
		    mac_stats->etherStatsJabbers + /* with crc errors */
		    mac_stats->etherStatsOversizePkts +
		    mac_stats->aFrameCheckSequenceErrors +
		    mac_stats->aAlignmentErrors;
		return (rv);
	case IFCOUNTER_OERRORS:
		return (mac_stats->ifOutErrors);
	default:
		return (if_get_counter_default(ifp, cnt));
	}
}

/*
 *  Unicast, Multicast and Promiscuous mode set
 *
 *  The set_rx_mode entry point is called whenever the unicast or multicast
 *  address lists or the network interface flags are updated.  This routine is
 *  responsible for configuring the hardware for proper unicast, multicast,
 *  promiscuous mode, and all-multi behavior.
 */
#define	MAX_NUM_MULTICAST_ADDRESSES 32
#define	MAX_NUM_ADDRESSES           32

static void
al_eth_set_rx_mode(struct al_eth_adapter *adapter)
{
	struct ifnet *ifp = adapter->netdev;
	struct ifmultiaddr *ifma; /* multicast addresses configured */
	struct ifaddr *ifua; /* unicast address */
	int mc = 0;
	int uc = 0;
	uint8_t i;
	unsigned char *mac;

	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		if (mc == MAX_NUM_MULTICAST_ADDRESSES)
			break;

		mac = LLADDR((struct sockaddr_dl *) ifma->ifma_addr);
		/* default mc address inside mac address */
		if (mac[3] != 0 && mac[4] != 0 && mac[5] != 1)
			mc++;
	}
	if_maddr_runlock(ifp);

	if_addr_rlock(ifp);
	CK_STAILQ_FOREACH(ifua, &ifp->if_addrhead, ifa_link) {
		if (ifua->ifa_addr->sa_family != AF_LINK)
			continue;
		if (uc == MAX_NUM_ADDRESSES)
			break;
		uc++;
	}
	if_addr_runlock(ifp);

	if ((ifp->if_flags & IFF_PROMISC) != 0) {
		al_eth_mac_table_promiscuous_set(adapter, true);
	} else {
		if ((ifp->if_flags & IFF_ALLMULTI) != 0) {
			/* This interface is in all-multicasts mode (used by multicast routers). */
			al_eth_mac_table_all_multicast_add(adapter,
			    AL_ETH_MAC_TABLE_ALL_MULTICAST_IDX, 1);
		} else {
			if (mc == 0) {
				al_eth_mac_table_entry_clear(adapter,
				    AL_ETH_MAC_TABLE_ALL_MULTICAST_IDX);
			} else {
				al_eth_mac_table_all_multicast_add(adapter,
				    AL_ETH_MAC_TABLE_ALL_MULTICAST_IDX, 1);
			}
		}
		if (uc != 0) {
			i = AL_ETH_MAC_TABLE_UNICAST_IDX_BASE + 1;
			if (uc > AL_ETH_MAC_TABLE_UNICAST_MAX_COUNT) {
				/*
				 * In this case there are more addresses then
				 * entries in the mac table - set promiscuous
				 */
				al_eth_mac_table_promiscuous_set(adapter, true);
				return;
			}

			/* clear the last configuration */
			while (i < (AL_ETH_MAC_TABLE_UNICAST_IDX_BASE +
				    AL_ETH_MAC_TABLE_UNICAST_MAX_COUNT)) {
				al_eth_mac_table_entry_clear(adapter, i);
				i++;
			}

			/* set new addresses */
			i = AL_ETH_MAC_TABLE_UNICAST_IDX_BASE + 1;
			if_addr_rlock(ifp);
			CK_STAILQ_FOREACH(ifua, &ifp->if_addrhead, ifa_link) {
				if (ifua->ifa_addr->sa_family != AF_LINK) {
					continue;
				}
				al_eth_mac_table_unicast_add(adapter, i,
				    (unsigned char *)ifua->ifa_addr, 1);
				i++;
			}
			if_addr_runlock(ifp);

		}
		al_eth_mac_table_promiscuous_set(adapter, false);
	}
}

static void
al_eth_config_rx_fwd(struct al_eth_adapter *adapter)
{
	struct al_eth_fwd_ctrl_table_entry entry;
	int i;

	/* let priority be equal to pbits */
	for (i = 0; i < AL_ETH_FWD_PBITS_TABLE_NUM; i++)
		al_eth_fwd_pbits_table_set(&adapter->hal_adapter, i, i);

	/* map priority to queue index, queue id = priority/2 */
	for (i = 0; i < AL_ETH_FWD_PRIO_TABLE_NUM; i++)
		al_eth_fwd_priority_table_set(&adapter->hal_adapter, i, i >> 1);

	entry.prio_sel = AL_ETH_CTRL_TABLE_PRIO_SEL_VAL_0;
	entry.queue_sel_1 = AL_ETH_CTRL_TABLE_QUEUE_SEL_1_THASH_TABLE;
	entry.queue_sel_2 = AL_ETH_CTRL_TABLE_QUEUE_SEL_2_NO_PRIO;
	entry.udma_sel = AL_ETH_CTRL_TABLE_UDMA_SEL_MAC_TABLE;
	entry.filter = FALSE;

	al_eth_ctrl_table_def_set(&adapter->hal_adapter, FALSE, &entry);

	/*
	 * By default set the mac table to forward all unicast packets to our
	 * MAC address and all broadcast. all the rest will be dropped.
	 */
	al_eth_mac_table_unicast_add(adapter, AL_ETH_MAC_TABLE_UNICAST_IDX_BASE,
	    adapter->mac_addr, 1);
	al_eth_mac_table_broadcast_add(adapter, AL_ETH_MAC_TABLE_BROADCAST_IDX, 1);
	al_eth_mac_table_promiscuous_set(adapter, false);

	/* set toeplitz hash keys */
	for (i = 0; i < sizeof(adapter->toeplitz_hash_key); i++)
		*((uint8_t*)adapter->toeplitz_hash_key + i) = (uint8_t)random();

	for (i = 0; i < AL_ETH_RX_HASH_KEY_NUM; i++)
		al_eth_hash_key_set(&adapter->hal_adapter, i,
		    htonl(adapter->toeplitz_hash_key[i]));

	for (i = 0; i < AL_ETH_RX_RSS_TABLE_SIZE; i++) {
		adapter->rss_ind_tbl[i] = ethtool_rxfh_indir_default(i,
		    AL_ETH_NUM_QUEUES);
		al_eth_set_thash_table_entry(adapter, i, 0,
		    adapter->rss_ind_tbl[i]);
	}

	al_eth_fsm_table_init(adapter);
}

static void
al_eth_req_rx_buff_size(struct al_eth_adapter *adapter, int size)
{

	/*
	* Determine the correct mbuf pool
	* for doing jumbo frames
	* Try from the smallest up to maximum supported
	*/
	adapter->rx_mbuf_sz = MCLBYTES;
	if (size > 2048) {
		if (adapter->max_rx_buff_alloc_size > 2048)
			adapter->rx_mbuf_sz = MJUMPAGESIZE;
		else
			return;
	}
	if (size > 4096) {
		if (adapter->max_rx_buff_alloc_size > 4096)
			adapter->rx_mbuf_sz = MJUM9BYTES;
		else
			return;
	}
	if (size > 9216) {
		if (adapter->max_rx_buff_alloc_size > 9216)
			adapter->rx_mbuf_sz = MJUM16BYTES;
		else
			return;
	}
}

static int
al_eth_change_mtu(struct al_eth_adapter *adapter, int new_mtu)
{
	int max_frame = new_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN +
	    ETHER_VLAN_ENCAP_LEN;

	al_eth_req_rx_buff_size(adapter, new_mtu);

	device_printf_dbg(adapter->dev, "set MTU to %d\n", new_mtu);
	al_eth_rx_pkt_limit_config(&adapter->hal_adapter,
	    AL_ETH_MIN_FRAME_LEN, max_frame);

	al_eth_tso_mss_config(&adapter->hal_adapter, 0, new_mtu - 100);

	return (0);
}

static int
al_eth_check_mtu(struct al_eth_adapter *adapter, int new_mtu)
{
	int max_frame = new_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN + ETHER_VLAN_ENCAP_LEN;

	if ((new_mtu < AL_ETH_MIN_FRAME_LEN) ||
	    (max_frame > AL_ETH_MAX_FRAME_LEN)) {
		return (EINVAL);
	}

	return (0);
}

static int
al_eth_udma_queue_enable(struct al_eth_adapter *adapter, enum al_udma_type type,
    int qid)
{
	int rc = 0;
	char *name = (type == UDMA_TX) ? "Tx" : "Rx";
	struct al_udma_q_params *q_params;

	if (type == UDMA_TX)
		q_params = &adapter->tx_ring[qid].q_params;
	else
		q_params = &adapter->rx_ring[qid].q_params;

	rc = al_eth_queue_config(&adapter->hal_adapter, type, qid, q_params);
	if (rc < 0) {
		device_printf(adapter->dev, "config %s queue %u failed\n", name,
		    qid);
		return (rc);
	}
	return (rc);
}

static int
al_eth_udma_queues_enable_all(struct al_eth_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		al_eth_udma_queue_enable(adapter, UDMA_TX, i);

	for (i = 0; i < adapter->num_rx_queues; i++)
		al_eth_udma_queue_enable(adapter, UDMA_RX, i);

	return (0);
}

static void
al_eth_up_complete(struct al_eth_adapter *adapter)
{

	al_eth_configure_int_mode(adapter);
	al_eth_config_rx_fwd(adapter);
	al_eth_change_mtu(adapter, adapter->netdev->if_mtu);
	al_eth_udma_queues_enable_all(adapter);
	al_eth_refill_all_rx_bufs(adapter);
	al_eth_interrupts_unmask(adapter);

	/* enable forwarding interrupts from eth through pci end point */
	if ((adapter->board_type == ALPINE_FPGA_NIC) ||
	    (adapter->board_type == ALPINE_NIC)) {
		al_eth_forward_int_config((uint32_t*)adapter->internal_pcie_base +
		    AL_REG_OFFSET_FORWARD_INTR, AL_EN_FORWARD_INTR);
	}

	al_eth_flow_ctrl_enable(adapter);

	mtx_lock(&adapter->stats_mtx);
	callout_reset(&adapter->stats_callout, hz, al_tick_stats, (void*)adapter);
	mtx_unlock(&adapter->stats_mtx);

	al_eth_mac_start(&adapter->hal_adapter);
}

static int
al_media_update(struct ifnet *ifp)
{
	struct al_eth_adapter *adapter = ifp->if_softc;

	if ((ifp->if_flags & IFF_UP) != 0)
		mii_mediachg(adapter->mii);

	return (0);
}

static void
al_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct al_eth_adapter *sc = ifp->if_softc;
	struct mii_data *mii;

	if (sc->mii == NULL) {
		ifmr->ifm_active = IFM_ETHER | IFM_NONE;
		ifmr->ifm_status = 0;

		return;
	}

	mii = sc->mii;
	mii_pollstat(mii);

	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

static void
al_tick(void *arg)
{
	struct al_eth_adapter *adapter = arg;

	mii_tick(adapter->mii);

	/* Schedule another timeout one second from now */
	callout_schedule(&adapter->wd_callout, hz);
}

static void
al_tick_stats(void *arg)
{
	struct al_eth_adapter *adapter = arg;

	al_eth_update_stats(adapter);

	callout_schedule(&adapter->stats_callout, hz);
}

static int
al_eth_up(struct al_eth_adapter *adapter)
{
	struct ifnet *ifp = adapter->netdev;
	int rc;

	if (adapter->up)
		return (0);

	if ((adapter->flags & AL_ETH_FLAG_RESET_REQUESTED) != 0) {
		al_eth_function_reset(adapter);
		adapter->flags &= ~AL_ETH_FLAG_RESET_REQUESTED;
	}

	ifp->if_hwassist = 0;
	if ((ifp->if_capenable & IFCAP_TSO) != 0)
		ifp->if_hwassist |= CSUM_TSO;
	if ((ifp->if_capenable & IFCAP_TXCSUM) != 0)
		ifp->if_hwassist |= (CSUM_TCP | CSUM_UDP);
	if ((ifp->if_capenable & IFCAP_TXCSUM_IPV6) != 0)
		ifp->if_hwassist |= (CSUM_TCP_IPV6 | CSUM_UDP_IPV6);

	al_eth_serdes_init(adapter);

	rc = al_eth_hw_init(adapter);
	if (rc != 0)
		goto err_hw_init_open;

	rc = al_eth_setup_int_mode(adapter);
	if (rc != 0) {
		device_printf(adapter->dev,
		    "%s failed at setup interrupt mode!\n", __func__);
		goto err_setup_int;
	}

	/* allocate transmit descriptors */
	rc = al_eth_setup_all_tx_resources(adapter);
	if (rc != 0)
		goto err_setup_tx;

	/* allocate receive descriptors */
	rc = al_eth_setup_all_rx_resources(adapter);
	if (rc != 0)
		goto err_setup_rx;

	rc = al_eth_request_irq(adapter);
	if (rc != 0)
		goto err_req_irq;

	al_eth_up_complete(adapter);

	adapter->up = true;

	if (adapter->mac_mode == AL_ETH_MAC_MODE_10GbE_Serial)
		adapter->netdev->if_link_state = LINK_STATE_UP;

	if (adapter->mac_mode == AL_ETH_MAC_MODE_RGMII) {
		mii_mediachg(adapter->mii);

		/* Schedule watchdog timeout */
		mtx_lock(&adapter->wd_mtx);
		callout_reset(&adapter->wd_callout, hz, al_tick, adapter);
		mtx_unlock(&adapter->wd_mtx);

		mii_pollstat(adapter->mii);
	}

	return (rc);

err_req_irq:
	al_eth_free_all_rx_resources(adapter);
err_setup_rx:
	al_eth_free_all_tx_resources(adapter);
err_setup_tx:
	al_eth_free_irq(adapter);
err_setup_int:
	al_eth_hw_stop(adapter);
err_hw_init_open:
	al_eth_function_reset(adapter);

	return (rc);
}

static int
al_shutdown(device_t dev)
{
	struct al_eth_adapter *adapter = device_get_softc(dev);

	al_eth_down(adapter);

	return (0);
}

static void
al_eth_down(struct al_eth_adapter *adapter)
{

	device_printf_dbg(adapter->dev, "al_eth_down: begin\n");

	adapter->up = false;

	mtx_lock(&adapter->wd_mtx);
	callout_stop(&adapter->wd_callout);
	mtx_unlock(&adapter->wd_mtx);

	al_eth_disable_int_sync(adapter);

	mtx_lock(&adapter->stats_mtx);
	callout_stop(&adapter->stats_callout);
	mtx_unlock(&adapter->stats_mtx);

	al_eth_free_irq(adapter);
	al_eth_hw_stop(adapter);

	al_eth_free_all_tx_resources(adapter);
	al_eth_free_all_rx_resources(adapter);
}

static int
al_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct al_eth_adapter	*adapter = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *)data;
	int			error = 0;

	switch (command) {
	case SIOCSIFMTU:
	{
		error = al_eth_check_mtu(adapter, ifr->ifr_mtu);
		if (error != 0) {
			device_printf(adapter->dev, "ioctl wrong mtu %u\n",
			    adapter->netdev->if_mtu);
			break;
		}

		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		adapter->netdev->if_mtu = ifr->ifr_mtu;
		al_init(adapter);
		break;
	}
	case SIOCSIFFLAGS:
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				if (((ifp->if_flags ^ adapter->if_flags) &
				    (IFF_PROMISC | IFF_ALLMULTI)) != 0) {
					device_printf_dbg(adapter->dev,
					    "ioctl promisc/allmulti\n");
					al_eth_set_rx_mode(adapter);
				}
			} else {
				error = al_eth_up(adapter);
				if (error == 0)
					ifp->if_drv_flags |= IFF_DRV_RUNNING;
			}
		} else {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				al_eth_down(adapter);
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			}
		}

		adapter->if_flags = ifp->if_flags;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
			device_printf_dbg(adapter->dev,
			    "ioctl add/del multi before\n");
			al_eth_set_rx_mode(adapter);
#ifdef DEVICE_POLLING
			if ((ifp->if_capenable & IFCAP_POLLING) == 0)
#endif
		}
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		if (adapter->mii != NULL)
			error = ifmedia_ioctl(ifp, ifr,
			    &adapter->mii->mii_media, command);
		else
			error = ifmedia_ioctl(ifp, ifr,
			    &adapter->media, command);
		break;
	case SIOCSIFCAP:
	    {
		int mask, reinit;

		reinit = 0;
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
#ifdef DEVICE_POLLING
		if ((mask & IFCAP_POLLING) != 0) {
			if ((ifr->ifr_reqcap & IFCAP_POLLING) != 0) {
				if (error != 0)
					return (error);
				ifp->if_capenable |= IFCAP_POLLING;
			} else {
				error = ether_poll_deregister(ifp);
				/* Enable interrupt even in error case */
				ifp->if_capenable &= ~IFCAP_POLLING;
			}
		}
#endif
		if ((mask & IFCAP_HWCSUM) != 0) {
			/* apply to both rx and tx */
			ifp->if_capenable ^= IFCAP_HWCSUM;
			reinit = 1;
		}
		if ((mask & IFCAP_HWCSUM_IPV6) != 0) {
			ifp->if_capenable ^= IFCAP_HWCSUM_IPV6;
			reinit = 1;
		}
		if ((mask & IFCAP_TSO) != 0) {
			ifp->if_capenable ^= IFCAP_TSO;
			reinit = 1;
		}
		if ((mask & IFCAP_LRO) != 0) {
			ifp->if_capenable ^= IFCAP_LRO;
		}
		if ((mask & IFCAP_VLAN_HWTAGGING) != 0) {
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
			reinit = 1;
		}
		if ((mask & IFCAP_VLAN_HWFILTER) != 0) {
			ifp->if_capenable ^= IFCAP_VLAN_HWFILTER;
			reinit = 1;
		}
		if ((mask & IFCAP_VLAN_HWTSO) != 0) {
			ifp->if_capenable ^= IFCAP_VLAN_HWTSO;
			reinit = 1;
		}
		if ((reinit != 0) &&
		    ((ifp->if_drv_flags & IFF_DRV_RUNNING)) != 0)
		{
			al_init(adapter);
		}
		break;
	    }

	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static int
al_is_device_supported(device_t dev)
{
	uint16_t pci_vendor_id = pci_get_vendor(dev);
	uint16_t pci_device_id = pci_get_device(dev);

	return (pci_vendor_id == PCI_VENDOR_ID_ANNAPURNA_LABS &&
	    (pci_device_id == PCI_DEVICE_ID_AL_ETH ||
	    pci_device_id == PCI_DEVICE_ID_AL_ETH_ADVANCED ||
	    pci_device_id == PCI_DEVICE_ID_AL_ETH_NIC ||
	    pci_device_id == PCI_DEVICE_ID_AL_ETH_FPGA_NIC));
}

/* Time in mSec to keep trying to read / write from MDIO in case of error */
#define	MDIO_TIMEOUT_MSEC	100
#define	MDIO_PAUSE_MSEC		10

static int
al_miibus_readreg(device_t dev, int phy, int reg)
{
	struct al_eth_adapter *adapter = device_get_softc(dev);
	uint16_t value = 0;
	int rc;
	int timeout = MDIO_TIMEOUT_MSEC;

	while (timeout > 0) {
		rc = al_eth_mdio_read(&adapter->hal_adapter, adapter->phy_addr,
		    -1, reg, &value);

		if (rc == 0)
			return (value);

		device_printf_dbg(adapter->dev,
		    "mdio read failed. try again in 10 msec\n");

		timeout -= MDIO_PAUSE_MSEC;
		pause("readred pause", MDIO_PAUSE_MSEC);
	}

	if (rc != 0)
		device_printf(adapter->dev, "MDIO read failed on timeout\n");

	return (value);
}

static int
al_miibus_writereg(device_t dev, int phy, int reg, int value)
{
	struct al_eth_adapter *adapter = device_get_softc(dev);
	int rc;
	int timeout = MDIO_TIMEOUT_MSEC;

	while (timeout > 0) {
		rc = al_eth_mdio_write(&adapter->hal_adapter, adapter->phy_addr,
		    -1, reg, value);

		if (rc == 0)
			return (0);

		device_printf(adapter->dev,
		    "mdio write failed. try again in 10 msec\n");

		timeout -= MDIO_PAUSE_MSEC;
		pause("miibus writereg", MDIO_PAUSE_MSEC);
	}

	if (rc != 0)
		device_printf(adapter->dev, "MDIO write failed on timeout\n");

	return (rc);
}

static void
al_miibus_statchg(device_t dev)
{
	struct al_eth_adapter *adapter = device_get_softc(dev);

	device_printf_dbg(adapter->dev,
	    "al_miibus_statchg: state has changed!\n");
	device_printf_dbg(adapter->dev,
	    "al_miibus_statchg: active = 0x%x status = 0x%x\n",
	    adapter->mii->mii_media_active, adapter->mii->mii_media_status);

	if (adapter->up == 0)
		return;

	if ((adapter->mii->mii_media_status & IFM_AVALID) != 0) {
		if (adapter->mii->mii_media_status & IFM_ACTIVE) {
			device_printf(adapter->dev, "link is UP\n");
			adapter->netdev->if_link_state = LINK_STATE_UP;
		} else {
			device_printf(adapter->dev, "link is DOWN\n");
			adapter->netdev->if_link_state = LINK_STATE_DOWN;
		}
	}
}

static void
al_miibus_linkchg(device_t dev)
{
	struct al_eth_adapter *adapter = device_get_softc(dev);
	uint8_t duplex = 0;
	uint8_t speed = 0;

	if (adapter->mii == NULL)
		return;

	if ((adapter->netdev->if_flags & IFF_UP) == 0)
		return;

	/* Ignore link changes when link is not ready */
	if ((adapter->mii->mii_media_status & (IFM_AVALID | IFM_ACTIVE)) !=
	    (IFM_AVALID | IFM_ACTIVE)) {
		return;
	}

	if ((adapter->mii->mii_media_active & IFM_FDX) != 0)
		duplex = 1;

	speed = IFM_SUBTYPE(adapter->mii->mii_media_active);

	if (speed == IFM_10_T) {
		al_eth_mac_link_config(&adapter->hal_adapter, 0, 1,
		    AL_10BASE_T_SPEED, duplex);
		return;
	}

	if (speed == IFM_100_TX) {
		al_eth_mac_link_config(&adapter->hal_adapter, 0, 1,
		    AL_100BASE_TX_SPEED, duplex);
		return;
	}

	if (speed == IFM_1000_T) {
		al_eth_mac_link_config(&adapter->hal_adapter, 0, 1,
		    AL_1000BASE_T_SPEED, duplex);
		return;
	}

	device_printf(adapter->dev, "ERROR: unknown MII media active 0x%08x\n",
	    adapter->mii->mii_media_active);
}
