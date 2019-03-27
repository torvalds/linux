/*
 * Copyright (C) 2015 Cavium Inc.
 * All rights reserved.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitset.h>
#include <sys/bitstring.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/pciio.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/cpuset.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_media.h>

#include <machine/bus.h>
#include <machine/_inttypes.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <sys/dnv.h>
#include <sys/nv.h>
#ifdef PCI_IOV
#include <sys/iov_schema.h>
#include <dev/pci/pci_iov.h>
#endif

#include "thunder_bgx.h"
#include "nic_reg.h"
#include "nic.h"
#include "q_struct.h"

#define	VNIC_PF_DEVSTR		"Cavium Thunder NIC Physical Function Driver"

#define	VNIC_PF_REG_RID		PCIR_BAR(PCI_CFG_REG_BAR_NUM)

#define	NIC_SET_VF_LMAC_MAP(bgx, lmac)		((((bgx) & 0xF) << 4) | ((lmac) & 0xF))
#define	NIC_GET_BGX_FROM_VF_LMAC_MAP(map)	(((map) >> 4) & 0xF)
#define	NIC_GET_LMAC_FROM_VF_LMAC_MAP(map)	((map) & 0xF)

/* Structure to be used by the SR-IOV for VF configuration schemas */
struct nicvf_info {
	boolean_t		vf_enabled;
	int			vf_flags;
};

struct nicpf {
	device_t		dev;
	uint8_t			node;
	u_int			flags;
	uint8_t			num_vf_en;      /* No of VF enabled */
	struct nicvf_info	vf_info[MAX_NUM_VFS_SUPPORTED];
	struct resource *	reg_base;       /* Register start address */
	struct pkind_cfg	pkind;
	uint8_t			vf_lmac_map[MAX_LMAC];
	boolean_t		mbx_lock[MAX_NUM_VFS_SUPPORTED];

	struct callout		check_link;
	struct mtx		check_link_mtx;

	uint8_t			link[MAX_LMAC];
	uint8_t			duplex[MAX_LMAC];
	uint32_t		speed[MAX_LMAC];
	uint16_t		cpi_base[MAX_NUM_VFS_SUPPORTED];
	uint16_t		rssi_base[MAX_NUM_VFS_SUPPORTED];
	uint16_t		rss_ind_tbl_size;

	/* MSI-X */
	boolean_t		msix_enabled;
	uint8_t			num_vec;
	struct msix_entry	msix_entries[NIC_PF_MSIX_VECTORS];
	struct resource *	msix_table_res;
};

static int nicpf_probe(device_t);
static int nicpf_attach(device_t);
static int nicpf_detach(device_t);

#ifdef PCI_IOV
static int nicpf_iov_init(device_t, uint16_t, const nvlist_t *);
static void nicpf_iov_uninit(device_t);
static int nicpf_iov_add_vf(device_t, uint16_t, const nvlist_t *);
#endif

static device_method_t nicpf_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		nicpf_probe),
	DEVMETHOD(device_attach,	nicpf_attach),
	DEVMETHOD(device_detach,	nicpf_detach),
	/* PCI SR-IOV interface */
#ifdef PCI_IOV
	DEVMETHOD(pci_iov_init,		nicpf_iov_init),
	DEVMETHOD(pci_iov_uninit,	nicpf_iov_uninit),
	DEVMETHOD(pci_iov_add_vf,	nicpf_iov_add_vf),
#endif
	DEVMETHOD_END,
};

static driver_t vnicpf_driver = {
	"vnicpf",
	nicpf_methods,
	sizeof(struct nicpf),
};

static devclass_t vnicpf_devclass;

DRIVER_MODULE(vnicpf, pci, vnicpf_driver, vnicpf_devclass, 0, 0);
MODULE_VERSION(vnicpf, 1);
MODULE_DEPEND(vnicpf, pci, 1, 1, 1);
MODULE_DEPEND(vnicpf, ether, 1, 1, 1);
MODULE_DEPEND(vnicpf, thunder_bgx, 1, 1, 1);

static int nicpf_alloc_res(struct nicpf *);
static void nicpf_free_res(struct nicpf *);
static void nic_set_lmac_vf_mapping(struct nicpf *);
static void nic_init_hw(struct nicpf *);
static int nic_sriov_init(device_t, struct nicpf *);
static void nic_poll_for_link(void *);
static int nic_register_interrupts(struct nicpf *);
static void nic_unregister_interrupts(struct nicpf *);

/*
 * Device interface
 */
static int
nicpf_probe(device_t dev)
{
	uint16_t vendor_id;
	uint16_t device_id;

	vendor_id = pci_get_vendor(dev);
	device_id = pci_get_device(dev);

	if (vendor_id == PCI_VENDOR_ID_CAVIUM &&
	    device_id == PCI_DEVICE_ID_THUNDER_NIC_PF) {
		device_set_desc(dev, VNIC_PF_DEVSTR);
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
nicpf_attach(device_t dev)
{
	struct nicpf *nic;
	int err;

	nic = device_get_softc(dev);
	nic->dev = dev;

	/* Enable bus mastering */
	pci_enable_busmaster(dev);

	/* Allocate PCI resources */
	err = nicpf_alloc_res(nic);
	if (err != 0) {
		device_printf(dev, "Could not allocate PCI resources\n");
		return (err);
	}

	nic->node = nic_get_node_id(nic->reg_base);

	/* Enable Traffic Network Switch (TNS) bypass mode by default */
	nic->flags &= ~NIC_TNS_ENABLED;
	nic_set_lmac_vf_mapping(nic);

	/* Initialize hardware */
	nic_init_hw(nic);

	/* Set RSS TBL size for each VF */
	nic->rss_ind_tbl_size = NIC_MAX_RSS_IDR_TBL_SIZE;

	/* Setup interrupts */
	err = nic_register_interrupts(nic);
	if (err != 0)
		goto err_free_res;

	/* Configure SRIOV */
	err = nic_sriov_init(dev, nic);
	if (err != 0)
		goto err_free_intr;

	if (nic->flags & NIC_TNS_ENABLED)
		return (0);

	mtx_init(&nic->check_link_mtx, "VNIC PF link poll", NULL, MTX_DEF);
	/* Register physical link status poll callout */
	callout_init_mtx(&nic->check_link, &nic->check_link_mtx, 0);
	mtx_lock(&nic->check_link_mtx);
	nic_poll_for_link(nic);
	mtx_unlock(&nic->check_link_mtx);

	return (0);

err_free_intr:
	nic_unregister_interrupts(nic);
err_free_res:
	nicpf_free_res(nic);
	pci_disable_busmaster(dev);

	return (err);
}

static int
nicpf_detach(device_t dev)
{
	struct nicpf *nic;
	int err;

	err = 0;
	nic = device_get_softc(dev);

	callout_drain(&nic->check_link);
	mtx_destroy(&nic->check_link_mtx);

	nic_unregister_interrupts(nic);
	nicpf_free_res(nic);
	pci_disable_busmaster(dev);

#ifdef PCI_IOV
	err = pci_iov_detach(dev);
	if (err != 0)
		device_printf(dev, "SR-IOV in use. Detach first.\n");
#endif
	return (err);
}

/*
 * SR-IOV interface
 */
#ifdef PCI_IOV
static int
nicpf_iov_init(device_t dev, uint16_t num_vfs, const nvlist_t *params)
{
	struct nicpf *nic;

	nic = device_get_softc(dev);

	if (num_vfs == 0)
		return (ENXIO);

	nic->flags |= NIC_SRIOV_ENABLED;

	return (0);
}

static void
nicpf_iov_uninit(device_t dev)
{

	/* ARM64TODO: Implement this function */
}

static int
nicpf_iov_add_vf(device_t dev, uint16_t vfnum, const nvlist_t *params)
{
	const void *mac;
	struct nicpf *nic;
	size_t size;
	int bgx, lmac;

	nic = device_get_softc(dev);

	if ((nic->flags & NIC_SRIOV_ENABLED) == 0)
		return (ENXIO);

	if (vfnum > (nic->num_vf_en - 1))
		return (EINVAL);

	if (nvlist_exists_binary(params, "mac-addr") != 0) {
		mac = nvlist_get_binary(params, "mac-addr", &size);
		bgx = NIC_GET_BGX_FROM_VF_LMAC_MAP(nic->vf_lmac_map[vfnum]);
		lmac = NIC_GET_LMAC_FROM_VF_LMAC_MAP(nic->vf_lmac_map[vfnum]);
		bgx_set_lmac_mac(nic->node, bgx, lmac, mac);
	}

	return (0);
}
#endif

/*
 * Helper routines
 */
static int
nicpf_alloc_res(struct nicpf *nic)
{
	device_t dev;
	int rid;

	dev = nic->dev;

	rid = VNIC_PF_REG_RID;
	nic->reg_base = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (nic->reg_base == NULL) {
		/* For verbose output print some more details */
		if (bootverbose) {
			device_printf(dev,
			    "Could not allocate registers memory\n");
		}
		return (ENXIO);
	}

	return (0);
}

static void
nicpf_free_res(struct nicpf *nic)
{
	device_t dev;

	dev = nic->dev;

	if (nic->reg_base != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(nic->reg_base), nic->reg_base);
	}
}

/* Register read/write APIs */
static __inline void
nic_reg_write(struct nicpf *nic, bus_space_handle_t offset,
    uint64_t val)
{

	bus_write_8(nic->reg_base, offset, val);
}

static __inline uint64_t
nic_reg_read(struct nicpf *nic, uint64_t offset)
{
	uint64_t val;

	val = bus_read_8(nic->reg_base, offset);
	return (val);
}

/* PF -> VF mailbox communication APIs */
static void
nic_enable_mbx_intr(struct nicpf *nic)
{

	/* Enable mailbox interrupt for all 128 VFs */
	nic_reg_write(nic, NIC_PF_MAILBOX_ENA_W1S, ~0UL);
	nic_reg_write(nic, NIC_PF_MAILBOX_ENA_W1S + sizeof(uint64_t), ~0UL);
}

static void
nic_clear_mbx_intr(struct nicpf *nic, int vf, int mbx_reg)
{

	nic_reg_write(nic, NIC_PF_MAILBOX_INT + (mbx_reg << 3), (1UL << vf));
}

static uint64_t
nic_get_mbx_addr(int vf)
{

	return (NIC_PF_VF_0_127_MAILBOX_0_1 + (vf << NIC_VF_NUM_SHIFT));
}

/*
 * Send a mailbox message to VF
 * @vf: vf to which this message to be sent
 * @mbx: Message to be sent
 */
static void
nic_send_msg_to_vf(struct nicpf *nic, int vf, union nic_mbx *mbx)
{
	bus_space_handle_t mbx_addr = nic_get_mbx_addr(vf);
	uint64_t *msg = (uint64_t *)mbx;

	/*
	 * In first revision HW, mbox interrupt is triggerred
	 * when PF writes to MBOX(1), in next revisions when
	 * PF writes to MBOX(0)
	 */
	if (pass1_silicon(nic->dev)) {
		nic_reg_write(nic, mbx_addr + 0, msg[0]);
		nic_reg_write(nic, mbx_addr + 8, msg[1]);
	} else {
		nic_reg_write(nic, mbx_addr + 8, msg[1]);
		nic_reg_write(nic, mbx_addr + 0, msg[0]);
	}
}

/*
 * Responds to VF's READY message with VF's
 * ID, node, MAC address e.t.c
 * @vf: VF which sent READY message
 */
static void
nic_mbx_send_ready(struct nicpf *nic, int vf)
{
	union nic_mbx mbx = {};
	int bgx_idx, lmac;
	const char *mac;

	mbx.nic_cfg.msg = NIC_MBOX_MSG_READY;
	mbx.nic_cfg.vf_id = vf;

	if (nic->flags & NIC_TNS_ENABLED)
		mbx.nic_cfg.tns_mode = NIC_TNS_MODE;
	else
		mbx.nic_cfg.tns_mode = NIC_TNS_BYPASS_MODE;

	if (vf < MAX_LMAC) {
		bgx_idx = NIC_GET_BGX_FROM_VF_LMAC_MAP(nic->vf_lmac_map[vf]);
		lmac = NIC_GET_LMAC_FROM_VF_LMAC_MAP(nic->vf_lmac_map[vf]);

		mac = bgx_get_lmac_mac(nic->node, bgx_idx, lmac);
		if (mac) {
			memcpy((uint8_t *)&mbx.nic_cfg.mac_addr, mac,
			    ETHER_ADDR_LEN);
		}
	}
	mbx.nic_cfg.node_id = nic->node;

	mbx.nic_cfg.loopback_supported = vf < MAX_LMAC;

	nic_send_msg_to_vf(nic, vf, &mbx);
}

/*
 * ACKs VF's mailbox message
 * @vf: VF to which ACK to be sent
 */
static void
nic_mbx_send_ack(struct nicpf *nic, int vf)
{
	union nic_mbx mbx = {};

	mbx.msg.msg = NIC_MBOX_MSG_ACK;
	nic_send_msg_to_vf(nic, vf, &mbx);
}

/*
 * NACKs VF's mailbox message that PF is not able to
 * complete the action
 * @vf: VF to which ACK to be sent
 */
static void
nic_mbx_send_nack(struct nicpf *nic, int vf)
{
	union nic_mbx mbx = {};

	mbx.msg.msg = NIC_MBOX_MSG_NACK;
	nic_send_msg_to_vf(nic, vf, &mbx);
}

/*
 * Flush all in flight receive packets to memory and
 * bring down an active RQ
 */
static int
nic_rcv_queue_sw_sync(struct nicpf *nic)
{
	uint16_t timeout = ~0x00;

	nic_reg_write(nic, NIC_PF_SW_SYNC_RX, 0x01);
	/* Wait till sync cycle is finished */
	while (timeout) {
		if (nic_reg_read(nic, NIC_PF_SW_SYNC_RX_DONE) & 0x1)
			break;
		timeout--;
	}
	nic_reg_write(nic, NIC_PF_SW_SYNC_RX, 0x00);
	if (!timeout) {
		device_printf(nic->dev, "Receive queue software sync failed\n");
		return (ETIMEDOUT);
	}
	return (0);
}

/* Get BGX Rx/Tx stats and respond to VF's request */
static void
nic_get_bgx_stats(struct nicpf *nic, struct bgx_stats_msg *bgx)
{
	int bgx_idx, lmac;
	union nic_mbx mbx = {};

	bgx_idx = NIC_GET_BGX_FROM_VF_LMAC_MAP(nic->vf_lmac_map[bgx->vf_id]);
	lmac = NIC_GET_LMAC_FROM_VF_LMAC_MAP(nic->vf_lmac_map[bgx->vf_id]);

	mbx.bgx_stats.msg = NIC_MBOX_MSG_BGX_STATS;
	mbx.bgx_stats.vf_id = bgx->vf_id;
	mbx.bgx_stats.rx = bgx->rx;
	mbx.bgx_stats.idx = bgx->idx;
	if (bgx->rx != 0) {
		mbx.bgx_stats.stats =
		    bgx_get_rx_stats(nic->node, bgx_idx, lmac, bgx->idx);
	} else {
		mbx.bgx_stats.stats =
		    bgx_get_tx_stats(nic->node, bgx_idx, lmac, bgx->idx);
	}
	nic_send_msg_to_vf(nic, bgx->vf_id, &mbx);
}

/* Update hardware min/max frame size */
static int
nic_update_hw_frs(struct nicpf *nic, int new_frs, int vf)
{

	if ((new_frs > NIC_HW_MAX_FRS) || (new_frs < NIC_HW_MIN_FRS)) {
		device_printf(nic->dev,
		    "Invalid MTU setting from VF%d rejected, "
		    "should be between %d and %d\n",
		    vf, NIC_HW_MIN_FRS, NIC_HW_MAX_FRS);
		return (EINVAL);
	}
	new_frs += ETHER_HDR_LEN;
	if (new_frs <= nic->pkind.maxlen)
		return (0);

	nic->pkind.maxlen = new_frs;
	nic_reg_write(nic, NIC_PF_PKIND_0_15_CFG, *(uint64_t *)&nic->pkind);
	return (0);
}

/* Set minimum transmit packet size */
static void
nic_set_tx_pkt_pad(struct nicpf *nic, int size)
{
	int lmac;
	uint64_t lmac_cfg;

	/* Max value that can be set is 60 */
	if (size > 60)
		size = 60;

	for (lmac = 0; lmac < (MAX_BGX_PER_CN88XX * MAX_LMAC_PER_BGX); lmac++) {
		lmac_cfg = nic_reg_read(nic, NIC_PF_LMAC_0_7_CFG | (lmac << 3));
		lmac_cfg &= ~(0xF << 2);
		lmac_cfg |= ((size / 4) << 2);
		nic_reg_write(nic, NIC_PF_LMAC_0_7_CFG | (lmac << 3), lmac_cfg);
	}
}

/*
 * Function to check number of LMACs present and set VF::LMAC mapping.
 * Mapping will be used while initializing channels.
 */
static void
nic_set_lmac_vf_mapping(struct nicpf *nic)
{
	unsigned bgx_map = bgx_get_map(nic->node);
	int bgx, next_bgx_lmac = 0;
	int lmac, lmac_cnt = 0;
	uint64_t lmac_credit;

	nic->num_vf_en = 0;
	if (nic->flags & NIC_TNS_ENABLED) {
		nic->num_vf_en = DEFAULT_NUM_VF_ENABLED;
		return;
	}

	for (bgx = 0; bgx < NIC_MAX_BGX; bgx++) {
		if ((bgx_map & (1 << bgx)) == 0)
			continue;
		lmac_cnt = bgx_get_lmac_count(nic->node, bgx);
		for (lmac = 0; lmac < lmac_cnt; lmac++)
			nic->vf_lmac_map[next_bgx_lmac++] =
						NIC_SET_VF_LMAC_MAP(bgx, lmac);
		nic->num_vf_en += lmac_cnt;

		/* Program LMAC credits */
		lmac_credit = (1UL << 1); /* channel credit enable */
		lmac_credit |= (0x1ff << 2); /* Max outstanding pkt count */
		/* 48KB BGX Tx buffer size, each unit is of size 16bytes */
		lmac_credit |= (((((48 * 1024) / lmac_cnt) -
		    NIC_HW_MAX_FRS) / 16) << 12);
		lmac = bgx * MAX_LMAC_PER_BGX;
		for (; lmac < lmac_cnt + (bgx * MAX_LMAC_PER_BGX); lmac++) {
			nic_reg_write(nic, NIC_PF_LMAC_0_7_CREDIT + (lmac * 8),
			    lmac_credit);
		}
	}
}

#define TNS_PORT0_BLOCK 6
#define TNS_PORT1_BLOCK 7
#define BGX0_BLOCK 8
#define BGX1_BLOCK 9

static void
nic_init_hw(struct nicpf *nic)
{
	int i;

	/* Enable NIC HW block */
	nic_reg_write(nic, NIC_PF_CFG, 0x3);

	/* Enable backpressure */
	nic_reg_write(nic, NIC_PF_BP_CFG, (1UL << 6) | 0x03);

	if (nic->flags & NIC_TNS_ENABLED) {
		nic_reg_write(nic, NIC_PF_INTF_0_1_SEND_CFG,
		    (NIC_TNS_MODE << 7) | TNS_PORT0_BLOCK);
		nic_reg_write(nic, NIC_PF_INTF_0_1_SEND_CFG | (1 << 8),
		    (NIC_TNS_MODE << 7) | TNS_PORT1_BLOCK);
		nic_reg_write(nic, NIC_PF_INTF_0_1_BP_CFG,
		    (1UL << 63) | TNS_PORT0_BLOCK);
		nic_reg_write(nic, NIC_PF_INTF_0_1_BP_CFG + (1 << 8),
		    (1UL << 63) | TNS_PORT1_BLOCK);

	} else {
		/* Disable TNS mode on both interfaces */
		nic_reg_write(nic, NIC_PF_INTF_0_1_SEND_CFG,
		    (NIC_TNS_BYPASS_MODE << 7) | BGX0_BLOCK);
		nic_reg_write(nic, NIC_PF_INTF_0_1_SEND_CFG | (1 << 8),
		    (NIC_TNS_BYPASS_MODE << 7) | BGX1_BLOCK);
		nic_reg_write(nic, NIC_PF_INTF_0_1_BP_CFG,
		    (1UL << 63) | BGX0_BLOCK);
		nic_reg_write(nic, NIC_PF_INTF_0_1_BP_CFG + (1 << 8),
		    (1UL << 63) | BGX1_BLOCK);
	}

	/* PKIND configuration */
	nic->pkind.minlen = 0;
	nic->pkind.maxlen = NIC_HW_MAX_FRS + ETHER_HDR_LEN;
	nic->pkind.lenerr_en = 1;
	nic->pkind.rx_hdr = 0;
	nic->pkind.hdr_sl = 0;

	for (i = 0; i < NIC_MAX_PKIND; i++) {
		nic_reg_write(nic, NIC_PF_PKIND_0_15_CFG | (i << 3),
		    *(uint64_t *)&nic->pkind);
	}

	nic_set_tx_pkt_pad(nic, NIC_HW_MIN_FRS);

	/* Timer config */
	nic_reg_write(nic, NIC_PF_INTR_TIMER_CFG, NICPF_CLK_PER_INT_TICK);

	/* Enable VLAN ethertype matching and stripping */
	nic_reg_write(nic, NIC_PF_RX_ETYPE_0_7,
	    (2 << 19) | (ETYPE_ALG_VLAN_STRIP << 16) | ETHERTYPE_VLAN);
}

/* Channel parse index configuration */
static void
nic_config_cpi(struct nicpf *nic, struct cpi_cfg_msg *cfg)
{
	uint32_t vnic, bgx, lmac, chan;
	uint32_t padd, cpi_count = 0;
	uint64_t cpi_base, cpi, rssi_base, rssi;
	uint8_t qset, rq_idx = 0;

	vnic = cfg->vf_id;
	bgx = NIC_GET_BGX_FROM_VF_LMAC_MAP(nic->vf_lmac_map[vnic]);
	lmac = NIC_GET_LMAC_FROM_VF_LMAC_MAP(nic->vf_lmac_map[vnic]);

	chan = (lmac * MAX_BGX_CHANS_PER_LMAC) + (bgx * NIC_CHANS_PER_INF);
	cpi_base = (lmac * NIC_MAX_CPI_PER_LMAC) + (bgx * NIC_CPI_PER_BGX);
	rssi_base = (lmac * nic->rss_ind_tbl_size) + (bgx * NIC_RSSI_PER_BGX);

	/* Rx channel configuration */
	nic_reg_write(nic, NIC_PF_CHAN_0_255_RX_BP_CFG | (chan << 3),
	    (1UL << 63) | (vnic << 0));
	nic_reg_write(nic, NIC_PF_CHAN_0_255_RX_CFG | (chan << 3),
	    ((uint64_t)cfg->cpi_alg << 62) | (cpi_base << 48));

	if (cfg->cpi_alg == CPI_ALG_NONE)
		cpi_count = 1;
	else if (cfg->cpi_alg == CPI_ALG_VLAN) /* 3 bits of PCP */
		cpi_count = 8;
	else if (cfg->cpi_alg == CPI_ALG_VLAN16) /* 3 bits PCP + DEI */
		cpi_count = 16;
	else if (cfg->cpi_alg == CPI_ALG_DIFF) /* 6bits DSCP */
		cpi_count = NIC_MAX_CPI_PER_LMAC;

	/* RSS Qset, Qidx mapping */
	qset = cfg->vf_id;
	rssi = rssi_base;
	for (; rssi < (rssi_base + cfg->rq_cnt); rssi++) {
		nic_reg_write(nic, NIC_PF_RSSI_0_4097_RQ | (rssi << 3),
		    (qset << 3) | rq_idx);
		rq_idx++;
	}

	rssi = 0;
	cpi = cpi_base;
	for (; cpi < (cpi_base + cpi_count); cpi++) {
		/* Determine port to channel adder */
		if (cfg->cpi_alg != CPI_ALG_DIFF)
			padd = cpi % cpi_count;
		else
			padd = cpi % 8; /* 3 bits CS out of 6bits DSCP */

		/* Leave RSS_SIZE as '0' to disable RSS */
		if (pass1_silicon(nic->dev)) {
			nic_reg_write(nic, NIC_PF_CPI_0_2047_CFG | (cpi << 3),
			    (vnic << 24) | (padd << 16) | (rssi_base + rssi));
		} else {
			/* Set MPI_ALG to '0' to disable MCAM parsing */
			nic_reg_write(nic, NIC_PF_CPI_0_2047_CFG | (cpi << 3),
			    (padd << 16));
			/* MPI index is same as CPI if MPI_ALG is not enabled */
			nic_reg_write(nic, NIC_PF_MPI_0_2047_CFG | (cpi << 3),
			    (vnic << 24) | (rssi_base + rssi));
		}

		if ((rssi + 1) >= cfg->rq_cnt)
			continue;

		if (cfg->cpi_alg == CPI_ALG_VLAN)
			rssi++;
		else if (cfg->cpi_alg == CPI_ALG_VLAN16)
			rssi = ((cpi - cpi_base) & 0xe) >> 1;
		else if (cfg->cpi_alg == CPI_ALG_DIFF)
			rssi = ((cpi - cpi_base) & 0x38) >> 3;
	}
	nic->cpi_base[cfg->vf_id] = cpi_base;
	nic->rssi_base[cfg->vf_id] = rssi_base;
}

/* Responsds to VF with its RSS indirection table size */
static void
nic_send_rss_size(struct nicpf *nic, int vf)
{
	union nic_mbx mbx = {};

	mbx.rss_size.msg = NIC_MBOX_MSG_RSS_SIZE;
	mbx.rss_size.ind_tbl_size = nic->rss_ind_tbl_size;
	nic_send_msg_to_vf(nic, vf, &mbx);
}

/*
 * Receive side scaling configuration
 * configure:
 * - RSS index
 * - indir table i.e hash::RQ mapping
 * - no of hash bits to consider
 */
static void
nic_config_rss(struct nicpf *nic, struct rss_cfg_msg *cfg)
{
	uint8_t qset, idx;
	uint64_t cpi_cfg, cpi_base, rssi_base, rssi;
	uint64_t idx_addr;

	idx = 0;
	rssi_base = nic->rssi_base[cfg->vf_id] + cfg->tbl_offset;

	rssi = rssi_base;
	qset = cfg->vf_id;

	for (; rssi < (rssi_base + cfg->tbl_len); rssi++) {
		nic_reg_write(nic, NIC_PF_RSSI_0_4097_RQ | (rssi << 3),
		    (qset << 3) | (cfg->ind_tbl[idx] & 0x7));
		idx++;
	}

	cpi_base = nic->cpi_base[cfg->vf_id];
	if (pass1_silicon(nic->dev))
		idx_addr = NIC_PF_CPI_0_2047_CFG;
	else
		idx_addr = NIC_PF_MPI_0_2047_CFG;
	cpi_cfg = nic_reg_read(nic, idx_addr | (cpi_base << 3));
	cpi_cfg &= ~(0xFUL << 20);
	cpi_cfg |= (cfg->hash_bits << 20);
	nic_reg_write(nic, idx_addr | (cpi_base << 3), cpi_cfg);
}

/*
 * 4 level transmit side scheduler configutation
 * for TNS bypass mode
 *
 * Sample configuration for SQ0
 * VNIC0-SQ0 -> TL4(0)   -> TL3[0]   -> TL2[0]  -> TL1[0] -> BGX0
 * VNIC1-SQ0 -> TL4(8)   -> TL3[2]   -> TL2[0]  -> TL1[0] -> BGX0
 * VNIC2-SQ0 -> TL4(16)  -> TL3[4]   -> TL2[1]  -> TL1[0] -> BGX0
 * VNIC3-SQ0 -> TL4(24)  -> TL3[6]   -> TL2[1]  -> TL1[0] -> BGX0
 * VNIC4-SQ0 -> TL4(512) -> TL3[128] -> TL2[32] -> TL1[1] -> BGX1
 * VNIC5-SQ0 -> TL4(520) -> TL3[130] -> TL2[32] -> TL1[1] -> BGX1
 * VNIC6-SQ0 -> TL4(528) -> TL3[132] -> TL2[33] -> TL1[1] -> BGX1
 * VNIC7-SQ0 -> TL4(536) -> TL3[134] -> TL2[33] -> TL1[1] -> BGX1
 */
static void
nic_tx_channel_cfg(struct nicpf *nic, uint8_t vnic, struct sq_cfg_msg *sq)
{
	uint32_t bgx, lmac, chan;
	uint32_t tl2, tl3, tl4;
	uint32_t rr_quantum;
	uint8_t sq_idx = sq->sq_num;
	uint8_t pqs_vnic;

	pqs_vnic = vnic;

	bgx = NIC_GET_BGX_FROM_VF_LMAC_MAP(nic->vf_lmac_map[pqs_vnic]);
	lmac = NIC_GET_LMAC_FROM_VF_LMAC_MAP(nic->vf_lmac_map[pqs_vnic]);

	/* 24 bytes for FCS, IPG and preamble */
	rr_quantum = ((NIC_HW_MAX_FRS + 24) / 4);

	tl4 = (lmac * NIC_TL4_PER_LMAC) + (bgx * NIC_TL4_PER_BGX);
	tl4 += sq_idx;

	tl3 = tl4 / (NIC_MAX_TL4 / NIC_MAX_TL3);
	nic_reg_write(nic, NIC_PF_QSET_0_127_SQ_0_7_CFG2 |
	    ((uint64_t)vnic << NIC_QS_ID_SHIFT) |
	    ((uint32_t)sq_idx << NIC_Q_NUM_SHIFT), tl4);
	nic_reg_write(nic, NIC_PF_TL4_0_1023_CFG | (tl4 << 3),
	    ((uint64_t)vnic << 27) | ((uint32_t)sq_idx << 24) | rr_quantum);

	nic_reg_write(nic, NIC_PF_TL3_0_255_CFG | (tl3 << 3), rr_quantum);
	chan = (lmac * MAX_BGX_CHANS_PER_LMAC) + (bgx * NIC_CHANS_PER_INF);
	nic_reg_write(nic, NIC_PF_TL3_0_255_CHAN | (tl3 << 3), chan);
	/* Enable backpressure on the channel */
	nic_reg_write(nic, NIC_PF_CHAN_0_255_TX_CFG | (chan << 3), 1);

	tl2 = tl3 >> 2;
	nic_reg_write(nic, NIC_PF_TL3A_0_63_CFG | (tl2 << 3), tl2);
	nic_reg_write(nic, NIC_PF_TL2_0_63_CFG | (tl2 << 3), rr_quantum);
	/* No priorities as of now */
	nic_reg_write(nic, NIC_PF_TL2_0_63_PRI | (tl2 << 3), 0x00);
}

static int
nic_config_loopback(struct nicpf *nic, struct set_loopback *lbk)
{
	int bgx_idx, lmac_idx;

	if (lbk->vf_id > MAX_LMAC)
		return (ENXIO);

	bgx_idx = NIC_GET_BGX_FROM_VF_LMAC_MAP(nic->vf_lmac_map[lbk->vf_id]);
	lmac_idx = NIC_GET_LMAC_FROM_VF_LMAC_MAP(nic->vf_lmac_map[lbk->vf_id]);

	bgx_lmac_internal_loopback(nic->node, bgx_idx, lmac_idx, lbk->enable);

	return (0);
}

/* Interrupt handler to handle mailbox messages from VFs */
static void
nic_handle_mbx_intr(struct nicpf *nic, int vf)
{
	union nic_mbx mbx = {};
	uint64_t *mbx_data;
	uint64_t mbx_addr;
	uint64_t reg_addr;
	uint64_t cfg;
	int bgx, lmac;
	int i;
	int ret = 0;

	nic->mbx_lock[vf] = TRUE;

	mbx_addr = nic_get_mbx_addr(vf);
	mbx_data = (uint64_t *)&mbx;

	for (i = 0; i < NIC_PF_VF_MAILBOX_SIZE; i++) {
		*mbx_data = nic_reg_read(nic, mbx_addr);
		mbx_data++;
		mbx_addr += sizeof(uint64_t);
	}

	switch (mbx.msg.msg) {
	case NIC_MBOX_MSG_READY:
		nic_mbx_send_ready(nic, vf);
		if (vf < MAX_LMAC) {
			nic->link[vf] = 0;
			nic->duplex[vf] = 0;
			nic->speed[vf] = 0;
		}
		ret = 1;
		break;
	case NIC_MBOX_MSG_QS_CFG:
		reg_addr = NIC_PF_QSET_0_127_CFG |
		    (mbx.qs.num << NIC_QS_ID_SHIFT);
		cfg = mbx.qs.cfg;
		nic_reg_write(nic, reg_addr, cfg);
		break;
	case NIC_MBOX_MSG_RQ_CFG:
		reg_addr = NIC_PF_QSET_0_127_RQ_0_7_CFG |
		    (mbx.rq.qs_num << NIC_QS_ID_SHIFT) |
		    (mbx.rq.rq_num << NIC_Q_NUM_SHIFT);
		nic_reg_write(nic, reg_addr, mbx.rq.cfg);
		break;
	case NIC_MBOX_MSG_RQ_BP_CFG:
		reg_addr = NIC_PF_QSET_0_127_RQ_0_7_BP_CFG |
		    (mbx.rq.qs_num << NIC_QS_ID_SHIFT) |
		    (mbx.rq.rq_num << NIC_Q_NUM_SHIFT);
		nic_reg_write(nic, reg_addr, mbx.rq.cfg);
		break;
	case NIC_MBOX_MSG_RQ_SW_SYNC:
		ret = nic_rcv_queue_sw_sync(nic);
		break;
	case NIC_MBOX_MSG_RQ_DROP_CFG:
		reg_addr = NIC_PF_QSET_0_127_RQ_0_7_DROP_CFG |
		    (mbx.rq.qs_num << NIC_QS_ID_SHIFT) |
		    (mbx.rq.rq_num << NIC_Q_NUM_SHIFT);
		nic_reg_write(nic, reg_addr, mbx.rq.cfg);
		break;
	case NIC_MBOX_MSG_SQ_CFG:
		reg_addr = NIC_PF_QSET_0_127_SQ_0_7_CFG |
		    (mbx.sq.qs_num << NIC_QS_ID_SHIFT) |
		    (mbx.sq.sq_num << NIC_Q_NUM_SHIFT);
		nic_reg_write(nic, reg_addr, mbx.sq.cfg);
		nic_tx_channel_cfg(nic, mbx.qs.num, &mbx.sq);
		break;
	case NIC_MBOX_MSG_SET_MAC:
		lmac = mbx.mac.vf_id;
		bgx = NIC_GET_BGX_FROM_VF_LMAC_MAP(nic->vf_lmac_map[lmac]);
		lmac = NIC_GET_LMAC_FROM_VF_LMAC_MAP(nic->vf_lmac_map[lmac]);
		bgx_set_lmac_mac(nic->node, bgx, lmac, mbx.mac.mac_addr);
		break;
	case NIC_MBOX_MSG_SET_MAX_FRS:
		ret = nic_update_hw_frs(nic, mbx.frs.max_frs, mbx.frs.vf_id);
		break;
	case NIC_MBOX_MSG_CPI_CFG:
		nic_config_cpi(nic, &mbx.cpi_cfg);
		break;
	case NIC_MBOX_MSG_RSS_SIZE:
		nic_send_rss_size(nic, vf);
		goto unlock;
	case NIC_MBOX_MSG_RSS_CFG:
	case NIC_MBOX_MSG_RSS_CFG_CONT: /* fall through */
		nic_config_rss(nic, &mbx.rss_cfg);
		break;
	case NIC_MBOX_MSG_CFG_DONE:
		/* Last message of VF config msg sequence */
		nic->vf_info[vf].vf_enabled = TRUE;
		goto unlock;
	case NIC_MBOX_MSG_SHUTDOWN:
		/* First msg in VF teardown sequence */
		nic->vf_info[vf].vf_enabled = FALSE;
		break;
	case NIC_MBOX_MSG_BGX_STATS:
		nic_get_bgx_stats(nic, &mbx.bgx_stats);
		goto unlock;
	case NIC_MBOX_MSG_LOOPBACK:
		ret = nic_config_loopback(nic, &mbx.lbk);
		break;
	default:
		device_printf(nic->dev,
		    "Invalid msg from VF%d, msg 0x%x\n", vf, mbx.msg.msg);
		break;
	}

	if (ret == 0)
		nic_mbx_send_ack(nic, vf);
	else if (mbx.msg.msg != NIC_MBOX_MSG_READY)
		nic_mbx_send_nack(nic, vf);
unlock:
	nic->mbx_lock[vf] = FALSE;
}

static void
nic_mbx_intr_handler(struct nicpf *nic, int mbx)
{
	uint64_t intr;
	uint8_t  vf, vf_per_mbx_reg = 64;

	intr = nic_reg_read(nic, NIC_PF_MAILBOX_INT + (mbx << 3));
	for (vf = 0; vf < vf_per_mbx_reg; vf++) {
		if (intr & (1UL << vf)) {
			nic_handle_mbx_intr(nic, vf + (mbx * vf_per_mbx_reg));
			nic_clear_mbx_intr(nic, vf, mbx);
		}
	}
}

static void
nic_mbx0_intr_handler (void *arg)
{
	struct nicpf *nic = (struct nicpf *)arg;

	nic_mbx_intr_handler(nic, 0);
}

static void
nic_mbx1_intr_handler (void *arg)
{
	struct nicpf *nic = (struct nicpf *)arg;

	nic_mbx_intr_handler(nic, 1);
}

static int
nic_enable_msix(struct nicpf *nic)
{
	struct pci_devinfo *dinfo;
	int rid, count;
	int ret;

	dinfo = device_get_ivars(nic->dev);
	rid = dinfo->cfg.msix.msix_table_bar;
	nic->msix_table_res =
	    bus_alloc_resource_any(nic->dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (nic->msix_table_res == NULL) {
		device_printf(nic->dev,
		    "Could not allocate memory for MSI-X table\n");
		return (ENXIO);
	}

	count = nic->num_vec = NIC_PF_MSIX_VECTORS;

	ret = pci_alloc_msix(nic->dev, &count);
	if ((ret != 0) || (count != nic->num_vec)) {
		device_printf(nic->dev,
		    "Request for #%d msix vectors failed, error: %d\n",
		    nic->num_vec, ret);
		return (ret);
	}

	nic->msix_enabled = 1;
	return (0);
}

static void
nic_disable_msix(struct nicpf *nic)
{
	if (nic->msix_enabled) {
		pci_release_msi(nic->dev);
		nic->msix_enabled = 0;
		nic->num_vec = 0;
	}

	bus_release_resource(nic->dev, SYS_RES_MEMORY,
	    rman_get_rid(nic->msix_table_res), nic->msix_table_res);
}

static void
nic_free_all_interrupts(struct nicpf *nic)
{
	int irq;

	for (irq = 0; irq < nic->num_vec; irq++) {
		if (nic->msix_entries[irq].irq_res == NULL)
			continue;
		if (nic->msix_entries[irq].handle != NULL) {
			bus_teardown_intr(nic->dev,
			    nic->msix_entries[irq].irq_res,
			    nic->msix_entries[irq].handle);
		}

		bus_release_resource(nic->dev, SYS_RES_IRQ, irq + 1,
		    nic->msix_entries[irq].irq_res);
	}
}

static int
nic_register_interrupts(struct nicpf *nic)
{
	int irq, rid;
	int ret;

	/* Enable MSI-X */
	ret = nic_enable_msix(nic);
	if (ret != 0)
		return (ret);

	/* Register mailbox interrupt handlers */
	irq = NIC_PF_INTR_ID_MBOX0;
	rid = irq + 1;
	nic->msix_entries[irq].irq_res = bus_alloc_resource_any(nic->dev,
	    SYS_RES_IRQ, &rid, (RF_SHAREABLE | RF_ACTIVE));
	if (nic->msix_entries[irq].irq_res == NULL) {
		ret = ENXIO;
		goto fail;
	}
	ret = bus_setup_intr(nic->dev, nic->msix_entries[irq].irq_res,
	    (INTR_MPSAFE | INTR_TYPE_MISC), NULL, nic_mbx0_intr_handler, nic,
	    &nic->msix_entries[irq].handle);
	if (ret != 0)
		goto fail;

	irq = NIC_PF_INTR_ID_MBOX1;
	rid = irq + 1;
	nic->msix_entries[irq].irq_res = bus_alloc_resource_any(nic->dev,
	    SYS_RES_IRQ, &rid, (RF_SHAREABLE | RF_ACTIVE));
	if (nic->msix_entries[irq].irq_res == NULL) {
		ret = ENXIO;
		goto fail;
	}
	ret = bus_setup_intr(nic->dev, nic->msix_entries[irq].irq_res,
	    (INTR_MPSAFE | INTR_TYPE_MISC), NULL, nic_mbx1_intr_handler, nic,
	    &nic->msix_entries[irq].handle);
	if (ret != 0)
		goto fail;

	/* Enable mailbox interrupt */
	nic_enable_mbx_intr(nic);
	return (0);

fail:
	nic_free_all_interrupts(nic);
	return (ret);
}

static void
nic_unregister_interrupts(struct nicpf *nic)
{

	nic_free_all_interrupts(nic);
	nic_disable_msix(nic);
}

static int nic_sriov_init(device_t dev, struct nicpf *nic)
{
#ifdef PCI_IOV
	nvlist_t *pf_schema, *vf_schema;
	int iov_pos;
	int err;
	uint16_t total_vf_cnt;

	err = pci_find_extcap(dev, PCIZ_SRIOV, &iov_pos);
	if (err != 0) {
		device_printf(dev,
		    "SR-IOV capability is not found in PCIe config space\n");
		return (err);
	}
	/* Fix-up the number of enabled VFs */
	total_vf_cnt = pci_read_config(dev, iov_pos + PCIR_SRIOV_TOTAL_VFS, 2);
	if (total_vf_cnt == 0)
		return (ENXIO);

	/* Attach SR-IOV */
	pf_schema = pci_iov_schema_alloc_node();
	vf_schema = pci_iov_schema_alloc_node();
	pci_iov_schema_add_unicast_mac(vf_schema, "mac-addr", 0, NULL);
	/*
	 * All VFs can change their MACs.
	 * This flag will be ignored but we set it just for the record.
	 */
	pci_iov_schema_add_bool(vf_schema, "allow-set-mac",
	    IOV_SCHEMA_HASDEFAULT, TRUE);

	err = pci_iov_attach(dev, pf_schema, vf_schema);
	if (err != 0) {
		device_printf(dev,
		    "Failed to initialize SR-IOV (error=%d)\n",
		    err);
		return (err);
	}
#endif
	return (0);
}

/*
 * Poll for BGX LMAC link status and update corresponding VF
 * if there is a change, valid only if internal L2 switch
 * is not present otherwise VF link is always treated as up
 */
static void
nic_poll_for_link(void *arg)
{
	union nic_mbx mbx = {};
	struct nicpf *nic;
	struct bgx_link_status link;
	uint8_t vf, bgx, lmac;

	nic = (struct nicpf *)arg;

	mbx.link_status.msg = NIC_MBOX_MSG_BGX_LINK_CHANGE;

	for (vf = 0; vf < nic->num_vf_en; vf++) {
		/* Poll only if VF is UP */
		if (!nic->vf_info[vf].vf_enabled)
			continue;

		/* Get BGX, LMAC indices for the VF */
		bgx = NIC_GET_BGX_FROM_VF_LMAC_MAP(nic->vf_lmac_map[vf]);
		lmac = NIC_GET_LMAC_FROM_VF_LMAC_MAP(nic->vf_lmac_map[vf]);
		/* Get interface link status */
		bgx_get_lmac_link_state(nic->node, bgx, lmac, &link);

		/* Inform VF only if link status changed */
		if (nic->link[vf] == link.link_up)
			continue;

		if (!nic->mbx_lock[vf]) {
			nic->link[vf] = link.link_up;
			nic->duplex[vf] = link.duplex;
			nic->speed[vf] = link.speed;

			/* Send a mbox message to VF with current link status */
			mbx.link_status.link_up = link.link_up;
			mbx.link_status.duplex = link.duplex;
			mbx.link_status.speed = link.speed;
			nic_send_msg_to_vf(nic, vf, &mbx);
		}
	}
	callout_reset(&nic->check_link, hz * 2, nic_poll_for_link, nic);
}
