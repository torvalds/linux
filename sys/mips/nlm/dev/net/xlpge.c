/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/limits.h>
#include <sys/bus.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#define __RMAN_RESOURCE_VISIBLE
#include <sys/rman.h>
#include <sys/taskqueue.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/bpf.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <dev/pci/pcivar.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/uma.h>

#include <machine/reg.h>
#include <machine/cpu.h>
#include <machine/mips_opcode.h>
#include <machine/asm.h>
#include <machine/cpuregs.h>

#include <machine/intr_machdep.h>
#include <machine/clock.h>	/* for DELAY */
#include <machine/bus.h>
#include <machine/resource.h>
#include <mips/nlm/hal/haldefs.h>
#include <mips/nlm/hal/iomap.h>
#include <mips/nlm/hal/mips-extns.h>
#include <mips/nlm/hal/cop2.h>
#include <mips/nlm/hal/fmn.h>
#include <mips/nlm/hal/sys.h>
#include <mips/nlm/hal/nae.h>
#include <mips/nlm/hal/mdio.h>
#include <mips/nlm/hal/sgmii.h>
#include <mips/nlm/hal/xaui.h>
#include <mips/nlm/hal/poe.h>
#include <ucore_app_bin.h>
#include <mips/nlm/hal/ucore_loader.h>
#include <mips/nlm/xlp.h>
#include <mips/nlm/board.h>
#include <mips/nlm/msgring.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include "miidevs.h"
#include <dev/mii/brgphyreg.h>
#include "miibus_if.h"
#include <sys/sysctl.h>

#include <mips/nlm/dev/net/xlpge.h>

/*#define XLP_DRIVER_LOOPBACK*/

static struct nae_port_config nae_port_config[64];

int poe_cl_tbl[MAX_POE_CLASSES] = {
	0x0, 0x249249,
	0x492492, 0x6db6db,
	0x924924, 0xb6db6d,
	0xdb6db6, 0xffffff
};

/* #define DUMP_PACKET */

static uint64_t
nlm_paddr_ld(uint64_t paddr)
{
	uint64_t xkaddr = 0x9800000000000000 | paddr;

	return (nlm_load_dword_daddr(xkaddr));
}

struct nlm_xlp_portdata ifp_ports[64];
static uma_zone_t nl_tx_desc_zone;

/* This implementation will register the following tree of device
 * registration:
 *                      pcibus
 *                       |
 *                      xlpnae (1 instance - virtual entity)
 *                       |
 *                     xlpge
 *      (18 sgmii / 4 xaui / 2 interlaken instances)
 *                       |
 *                    miibus
 */

static int nlm_xlpnae_probe(device_t);
static int nlm_xlpnae_attach(device_t);
static int nlm_xlpnae_detach(device_t);
static int nlm_xlpnae_suspend(device_t);
static int nlm_xlpnae_resume(device_t);
static int nlm_xlpnae_shutdown(device_t);

static device_method_t nlm_xlpnae_methods[] = {
	/* Methods from the device interface */
	DEVMETHOD(device_probe,		nlm_xlpnae_probe),
	DEVMETHOD(device_attach,	nlm_xlpnae_attach),
	DEVMETHOD(device_detach,	nlm_xlpnae_detach),
	DEVMETHOD(device_suspend,	nlm_xlpnae_suspend),
	DEVMETHOD(device_resume,	nlm_xlpnae_resume),
	DEVMETHOD(device_shutdown,	nlm_xlpnae_shutdown),

	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	DEVMETHOD_END
};

static driver_t nlm_xlpnae_driver = {
	"xlpnae",
	nlm_xlpnae_methods,
	sizeof(struct nlm_xlpnae_softc)
};

static devclass_t nlm_xlpnae_devclass;

static int nlm_xlpge_probe(device_t);
static int nlm_xlpge_attach(device_t);
static int nlm_xlpge_detach(device_t);
static int nlm_xlpge_suspend(device_t);
static int nlm_xlpge_resume(device_t);
static int nlm_xlpge_shutdown(device_t);

/* mii override functions */
static int nlm_xlpge_mii_read(device_t, int, int);
static int nlm_xlpge_mii_write(device_t, int, int, int);
static void nlm_xlpge_mii_statchg(device_t);

static device_method_t nlm_xlpge_methods[] = {
	/* Methods from the device interface */
	DEVMETHOD(device_probe,		nlm_xlpge_probe),
	DEVMETHOD(device_attach,	nlm_xlpge_attach),
	DEVMETHOD(device_detach,	nlm_xlpge_detach),
	DEVMETHOD(device_suspend,	nlm_xlpge_suspend),
	DEVMETHOD(device_resume,	nlm_xlpge_resume),
	DEVMETHOD(device_shutdown,	nlm_xlpge_shutdown),

	/* Methods from the nexus bus needed for explicitly
	 * probing children when driver is loaded as a kernel module
	 */
	DEVMETHOD(miibus_readreg,	nlm_xlpge_mii_read),
	DEVMETHOD(miibus_writereg,	nlm_xlpge_mii_write),
	DEVMETHOD(miibus_statchg,	nlm_xlpge_mii_statchg),

	/* Terminate method list */
	DEVMETHOD_END
};

static driver_t nlm_xlpge_driver = {
	"xlpge",
	nlm_xlpge_methods,
	sizeof(struct nlm_xlpge_softc)
};

static devclass_t nlm_xlpge_devclass;

DRIVER_MODULE(xlpnae, pci, nlm_xlpnae_driver, nlm_xlpnae_devclass, 0, 0);
DRIVER_MODULE(xlpge, xlpnae, nlm_xlpge_driver, nlm_xlpge_devclass, 0, 0);
DRIVER_MODULE(miibus, xlpge, miibus_driver, miibus_devclass, 0, 0);

MODULE_DEPEND(pci, xlpnae, 1, 1, 1);
MODULE_DEPEND(xlpnae, xlpge, 1, 1, 1);
MODULE_DEPEND(xlpge, ether, 1, 1, 1);
MODULE_DEPEND(xlpge, miibus, 1, 1, 1);

#define SGMII_RCV_CONTEXT_WIDTH 8

/* prototypes */
static void nlm_xlpge_msgring_handler(int vc, int size,
    int code, int srcid, struct nlm_fmn_msg *msg, void *data);
static void nlm_xlpge_submit_rx_free_desc(struct nlm_xlpge_softc *sc, int num);
static void nlm_xlpge_init(void *addr);
static void nlm_xlpge_port_disable(struct nlm_xlpge_softc *sc);
static void nlm_xlpge_port_enable(struct nlm_xlpge_softc *sc);

/* globals */
int dbg_on = 1;
int cntx2port[524];

static __inline void
atomic_incr_long(unsigned long *addr)
{
	atomic_add_long(addr, 1);
}

/*
 * xlpnae driver implementation
 */
static int
nlm_xlpnae_probe(device_t dev)
{
	if (pci_get_vendor(dev) != PCI_VENDOR_NETLOGIC ||
	    pci_get_device(dev) != PCI_DEVICE_ID_NLM_NAE)
		return (ENXIO);

	return (BUS_PROBE_DEFAULT);
}

static void
nlm_xlpnae_print_frin_desc_carving(struct nlm_xlpnae_softc *sc)
{
	int intf;
	uint32_t value;
	int start, size;

	/* XXXJC: use max_ports instead of 20 ? */
	for (intf = 0; intf < 20; intf++) {
		nlm_write_nae_reg(sc->base, NAE_FREE_IN_FIFO_CFG,
		    (0x80000000 | intf));
		value = nlm_read_nae_reg(sc->base, NAE_FREE_IN_FIFO_CFG);
		size = 2 * ((value >> 20) & 0x3ff);
		start = 2 * ((value >> 8) & 0x1ff);
	}
}

static void
nlm_config_egress(struct nlm_xlpnae_softc *sc, int nblock,
    int context_base, int hwport, int max_channels)
{
	int offset, num_channels;
	uint32_t data;

	num_channels = sc->portcfg[hwport].num_channels;

	data = (2048 << 12) | (hwport << 4) | 1;
	nlm_write_nae_reg(sc->base, NAE_TX_IF_BURSTMAX_CMD, data);

	data = ((context_base + num_channels - 1) << 22) |
	    (context_base << 12) | (hwport << 4) | 1;
	nlm_write_nae_reg(sc->base, NAE_TX_DDR_ACTVLIST_CMD, data);

	config_egress_fifo_carvings(sc->base, hwport,
	    context_base, num_channels, max_channels, sc->portcfg);
	config_egress_fifo_credits(sc->base, hwport,
	    context_base, num_channels, max_channels, sc->portcfg);

	data = nlm_read_nae_reg(sc->base, NAE_DMA_TX_CREDIT_TH);
	data |= (1 << 25) | (1 << 24);
	nlm_write_nae_reg(sc->base, NAE_DMA_TX_CREDIT_TH, data);

	for (offset = 0; offset < num_channels; offset++) {
		nlm_write_nae_reg(sc->base, NAE_TX_SCHED_MAP_CMD1,
		    NAE_DRR_QUANTA);
		data = (hwport << 15) | ((context_base + offset) << 5);
		if (sc->cmplx_type[nblock] == ILC)
			data |= (offset << 20);
		nlm_write_nae_reg(sc->base, NAE_TX_SCHED_MAP_CMD0, data | 1);
		nlm_write_nae_reg(sc->base, NAE_TX_SCHED_MAP_CMD0, data);
	}
}

static int
xlpnae_get_maxchannels(struct nlm_xlpnae_softc *sc)
{
	int maxchans = 0;
	int i;

	for (i = 0; i < sc->max_ports; i++) {
		if (sc->portcfg[i].type == UNKNOWN)
			continue;
		maxchans += sc->portcfg[i].num_channels;
	}

	return (maxchans);
}

static void
nlm_setup_interface(struct nlm_xlpnae_softc *sc, int nblock,
    int port, uint32_t cur_flow_base, uint32_t flow_mask,
    int max_channels, int context)
{
	uint64_t nae_base = sc->base;
	int mtu = 1536;			/* XXXJC: don't hard code */
	uint32_t ucore_mask;

	if (sc->cmplx_type[nblock] == XAUIC)
		nlm_config_xaui(nae_base, nblock, mtu,
		    mtu, sc->portcfg[port].vlan_pri_en);
	nlm_config_freein_fifo_uniq_cfg(nae_base,
	    port, sc->portcfg[port].free_desc_sizes);
	nlm_config_ucore_iface_mask_cfg(nae_base,
	    port, sc->portcfg[port].ucore_mask);

	nlm_program_flow_cfg(nae_base, port, cur_flow_base, flow_mask);

	if (sc->cmplx_type[nblock] == SGMIIC)
		nlm_configure_sgmii_interface(nae_base, nblock, port, mtu, 0);

	nlm_config_egress(sc, nblock, context, port, max_channels);

	nlm_nae_init_netior(nae_base, sc->nblocks);
	nlm_nae_open_if(nae_base, nblock, sc->cmplx_type[nblock], port,
	    sc->portcfg[port].free_desc_sizes);

	/*  XXXJC: check mask calculation */
	ucore_mask = (1 << sc->nucores) - 1;
	nlm_nae_init_ucore(nae_base, port, ucore_mask);
}

static void
nlm_setup_interfaces(struct nlm_xlpnae_softc *sc)
{
	uint64_t nae_base;
	uint32_t cur_slot, cur_slot_base;
	uint32_t cur_flow_base, port, flow_mask;
	int max_channels;
	int i, context;

	cur_slot = 0;
	cur_slot_base = 0;
	cur_flow_base = 0;
	nae_base = sc->base;
	flow_mask = nlm_get_flow_mask(sc->total_num_ports);
	/* calculate max_channels */
	max_channels = xlpnae_get_maxchannels(sc);

	port = 0;
	context = 0;
	for (i = 0; i < sc->max_ports; i++) {
		if (sc->portcfg[i].type == UNKNOWN)
			continue;
		nlm_setup_interface(sc, sc->portcfg[i].block, i, cur_flow_base,
		    flow_mask, max_channels, context);
		cur_flow_base += sc->per_port_num_flows;
		context += sc->portcfg[i].num_channels;
	}
}

static void
nlm_xlpnae_init(int node, struct nlm_xlpnae_softc *sc)
{
	uint64_t nae_base;
	uint32_t ucoremask = 0;
	uint32_t val;
	int i;

	nae_base = sc->base;

	nlm_nae_flush_free_fifo(nae_base, sc->nblocks);
	nlm_deflate_frin_fifo_carving(nae_base, sc->max_ports);
	nlm_reset_nae(node);

	for (i = 0; i < sc->nucores; i++)	/* XXXJC: code repeated below */
		ucoremask |= (0x1 << i);
	printf("Loading 0x%x ucores with microcode\n", ucoremask);
	nlm_ucore_load_all(nae_base, ucoremask, 1);

	val = nlm_set_device_frequency(node, DFS_DEVICE_NAE, sc->freq);
	printf("Setup NAE frequency to %dMHz\n", val);

	nlm_mdio_reset_all(nae_base);

	printf("Initialze SGMII PCS for blocks 0x%x\n", sc->sgmiimask);
	nlm_sgmii_pcs_init(nae_base, sc->sgmiimask);

	printf("Initialze XAUI PCS for blocks 0x%x\n", sc->xauimask);
	nlm_xaui_pcs_init(nae_base, sc->xauimask);

	/* clear NETIOR soft reset */
	nlm_write_nae_reg(nae_base, NAE_LANE_CFG_SOFTRESET, 0x0);

	/* Disable RX enable bit in RX_CONFIG */
	val = nlm_read_nae_reg(nae_base, NAE_RX_CONFIG);
	val &= 0xfffffffe;
	nlm_write_nae_reg(nae_base, NAE_RX_CONFIG, val);

	if (nlm_is_xlp8xx_ax() == 0) {
		val = nlm_read_nae_reg(nae_base, NAE_TX_CONFIG);
		val &= ~(1 << 3);
		nlm_write_nae_reg(nae_base, NAE_TX_CONFIG, val);
	}

	nlm_setup_poe_class_config(nae_base, MAX_POE_CLASSES,
	    sc->ncontexts, poe_cl_tbl);

	nlm_setup_vfbid_mapping(nae_base);

	nlm_setup_flow_crc_poly(nae_base, sc->flow_crc_poly);

	nlm_setup_rx_cal_cfg(nae_base, sc->max_ports, sc->portcfg);
	/* note: xlp8xx Ax does not have Tx Calendering */
	if (!nlm_is_xlp8xx_ax())
		nlm_setup_tx_cal_cfg(nae_base, sc->max_ports, sc->portcfg);

	nlm_setup_interfaces(sc);
	nlm_config_poe(sc->poe_base, sc->poedv_base);

	if (sc->hw_parser_en)
		nlm_enable_hardware_parser(nae_base);

	if (sc->prepad_en)
		nlm_prepad_enable(nae_base, sc->prepad_size);

	if (sc->ieee_1588_en)
		nlm_setup_1588_timer(sc->base, sc->portcfg);
}

static void
nlm_xlpnae_update_pde(void *dummy __unused)
{
	struct nlm_xlpnae_softc *sc;
	uint32_t dv[NUM_WORDS_PER_DV];
	device_t dev;
	int vec;

	dev = devclass_get_device(devclass_find("xlpnae"), 0);
	sc = device_get_softc(dev);

	nlm_write_poe_reg(sc->poe_base, POE_DISTR_EN, 0);
	for (vec = 0; vec < NUM_DIST_VEC; vec++) {
		if (nlm_get_poe_distvec(vec, dv) != 0)
			continue;

		nlm_write_poe_distvec(sc->poedv_base, vec, dv);
	}
	nlm_write_poe_reg(sc->poe_base, POE_DISTR_EN, 1);
}

SYSINIT(nlm_xlpnae_update_pde, SI_SUB_SMP, SI_ORDER_ANY,
    nlm_xlpnae_update_pde, NULL);

/* configuration common for sgmii, xaui, ilaken goes here */
static void
nlm_setup_portcfg(struct nlm_xlpnae_softc *sc, struct xlp_nae_ivars *naep,
    int block, int port)
{
	int i;
	uint32_t ucore_mask = 0;
	struct xlp_block_ivars *bp;
	struct xlp_port_ivars *p;

	bp = &(naep->block_ivars[block]);
	p  = &(bp->port_ivars[port & 0x3]);

	sc->portcfg[port].node = p->node;
	sc->portcfg[port].block = p->block;
	sc->portcfg[port].port = p->port;
	sc->portcfg[port].type = p->type;
	sc->portcfg[port].mdio_bus = p->mdio_bus;
	sc->portcfg[port].phy_addr = p->phy_addr;
	sc->portcfg[port].loopback_mode = p->loopback_mode;
	sc->portcfg[port].num_channels = p->num_channels;
	if (p->free_desc_sizes != MCLBYTES) {
		printf("[%d, %d] Error: free_desc_sizes %d != %d\n",
		    block, port, p->free_desc_sizes, MCLBYTES);
		return;
	}
	sc->portcfg[port].free_desc_sizes = p->free_desc_sizes;
	for (i = 0; i < sc->nucores; i++)	/* XXXJC: configure this */
		ucore_mask |= (0x1 << i);
	sc->portcfg[port].ucore_mask = ucore_mask;
	sc->portcfg[port].vlan_pri_en = p->vlan_pri_en;
	sc->portcfg[port].num_free_descs = p->num_free_descs;
	sc->portcfg[port].iface_fifo_size = p->iface_fifo_size;
	sc->portcfg[port].rxbuf_size = p->rxbuf_size;
	sc->portcfg[port].rx_slots_reqd = p->rx_slots_reqd;
	sc->portcfg[port].tx_slots_reqd = p->tx_slots_reqd;
	sc->portcfg[port].pseq_fifo_size = p->pseq_fifo_size;

	sc->portcfg[port].stg2_fifo_size = p->stg2_fifo_size;
	sc->portcfg[port].eh_fifo_size = p->eh_fifo_size;
	sc->portcfg[port].frout_fifo_size = p->frout_fifo_size;
	sc->portcfg[port].ms_fifo_size = p->ms_fifo_size;
	sc->portcfg[port].pkt_fifo_size = p->pkt_fifo_size;
	sc->portcfg[port].pktlen_fifo_size = p->pktlen_fifo_size;
	sc->portcfg[port].max_stg2_offset = p->max_stg2_offset;
	sc->portcfg[port].max_eh_offset = p->max_eh_offset;
	sc->portcfg[port].max_frout_offset = p->max_frout_offset;
	sc->portcfg[port].max_ms_offset = p->max_ms_offset;
	sc->portcfg[port].max_pmem_offset = p->max_pmem_offset;
	sc->portcfg[port].stg1_2_credit = p->stg1_2_credit;
	sc->portcfg[port].stg2_eh_credit = p->stg2_eh_credit;
	sc->portcfg[port].stg2_frout_credit = p->stg2_frout_credit;
	sc->portcfg[port].stg2_ms_credit = p->stg2_ms_credit;
	sc->portcfg[port].ieee1588_inc_intg = p->ieee1588_inc_intg;
	sc->portcfg[port].ieee1588_inc_den = p->ieee1588_inc_den;
	sc->portcfg[port].ieee1588_inc_num = p->ieee1588_inc_num;
	sc->portcfg[port].ieee1588_userval = p->ieee1588_userval;
	sc->portcfg[port].ieee1588_ptpoff = p->ieee1588_ptpoff;
	sc->portcfg[port].ieee1588_tmr1 = p->ieee1588_tmr1;
	sc->portcfg[port].ieee1588_tmr2 = p->ieee1588_tmr2;
	sc->portcfg[port].ieee1588_tmr3 = p->ieee1588_tmr3;

	sc->total_free_desc += sc->portcfg[port].free_desc_sizes;
	sc->total_num_ports++;
}

static int
nlm_xlpnae_attach(device_t dev)
{
	struct xlp_nae_ivars	*nae_ivars;
	struct nlm_xlpnae_softc *sc;
	device_t tmpd;
	uint32_t dv[NUM_WORDS_PER_DV];
	int port, i, j, nchan, nblock, node, qstart, qnum;
	int offset, context, txq_base, rxvcbase;
	uint64_t poe_pcibase, nae_pcibase;

	node = pci_get_slot(dev) / 8;
	nae_ivars = &xlp_board_info.nodes[node].nae_ivars;

	sc = device_get_softc(dev);
	sc->xlpnae_dev = dev;
	sc->node = nae_ivars->node;
	sc->base = nlm_get_nae_regbase(sc->node);
	sc->poe_base = nlm_get_poe_regbase(sc->node);
	sc->poedv_base = nlm_get_poedv_regbase(sc->node);
	sc->portcfg = nae_port_config;
	sc->blockmask = nae_ivars->blockmask;
	sc->ilmask = nae_ivars->ilmask;
	sc->xauimask = nae_ivars->xauimask;
	sc->sgmiimask = nae_ivars->sgmiimask;
	sc->nblocks = nae_ivars->nblocks;
	sc->freq = nae_ivars->freq;

	/* flow table generation is done by CRC16 polynomial */
	sc->flow_crc_poly = nae_ivars->flow_crc_poly;

	sc->hw_parser_en = nae_ivars->hw_parser_en;
	sc->prepad_en = nae_ivars->prepad_en;
	sc->prepad_size = nae_ivars->prepad_size;
	sc->ieee_1588_en = nae_ivars->ieee_1588_en;

	nae_pcibase = nlm_get_nae_pcibase(sc->node);
	sc->ncontexts = nlm_read_reg(nae_pcibase, XLP_PCI_DEVINFO_REG5);
	sc->nucores = nlm_num_uengines(nae_pcibase);

	for (nblock = 0; nblock < sc->nblocks; nblock++) {
		sc->cmplx_type[nblock] = nae_ivars->block_ivars[nblock].type;
		sc->portmask[nblock] = nae_ivars->block_ivars[nblock].portmask;
	}

	for (i = 0; i < sc->ncontexts; i++)
		cntx2port[i] = 18;	/* 18 is an invalid port */

	if (sc->nblocks == 5)
		sc->max_ports = 18;	/* 8xx has a block 4 with 2 ports */
	else
		sc->max_ports = sc->nblocks * PORTS_PER_CMPLX;

	for (i = 0; i < sc->max_ports; i++)
		sc->portcfg[i].type = UNKNOWN; /* Port Not Present */
	/*
	 * Now setup all internal fifo carvings based on
	 * total number of ports in the system
	 */
	sc->total_free_desc = 0;
	sc->total_num_ports = 0;
	port = 0;
	context = 0;
	txq_base = nlm_qidstart(nae_pcibase);
	rxvcbase = txq_base + sc->ncontexts;
	for (i = 0; i < sc->nblocks; i++) {
		uint32_t portmask;

		if ((nae_ivars->blockmask & (1 << i)) == 0) {
			port += 4;
			continue;
		}
		portmask = nae_ivars->block_ivars[i].portmask;
		for (j = 0; j < PORTS_PER_CMPLX; j++, port++) {
			if ((portmask & (1 << j)) == 0)
				continue;
			nlm_setup_portcfg(sc, nae_ivars, i, port);
			nchan = sc->portcfg[port].num_channels;
			for (offset = 0; offset < nchan; offset++)
				cntx2port[context + offset] = port;
			sc->portcfg[port].txq = txq_base + context;
			sc->portcfg[port].rxfreeq = rxvcbase + port;
			context += nchan;
		}
	}

	poe_pcibase = nlm_get_poe_pcibase(sc->node);
	sc->per_port_num_flows =
	    nlm_poe_max_flows(poe_pcibase) / sc->total_num_ports;

	/* zone for P2P descriptors */
	nl_tx_desc_zone = uma_zcreate("NL Tx Desc",
	    sizeof(struct xlpge_tx_desc), NULL, NULL, NULL, NULL,
	    NAE_CACHELINE_SIZE, 0);

	/* NAE FMN messages have CMS src station id's in the
	 * range of qstart to qnum.
	 */
	qstart = nlm_qidstart(nae_pcibase);
	qnum = nlm_qnum(nae_pcibase);
	if (register_msgring_handler(qstart, qstart + qnum - 1,
	    nlm_xlpge_msgring_handler, sc)) {
		panic("Couldn't register NAE msgring handler\n");
	}

	/* POE FMN messages have CMS src station id's in the
	 * range of qstart to qnum.
	 */
	qstart = nlm_qidstart(poe_pcibase);
	qnum = nlm_qnum(poe_pcibase);
	if (register_msgring_handler(qstart, qstart + qnum - 1,
	    nlm_xlpge_msgring_handler, sc)) {
		panic("Couldn't register POE msgring handler\n");
	}

	nlm_xlpnae_init(node, sc);

	for (i = 0; i < sc->max_ports; i++) {
		char desc[32];
		int block, port;

		if (sc->portcfg[i].type == UNKNOWN)
			continue;
		block = sc->portcfg[i].block;
		port = sc->portcfg[i].port;
		tmpd = device_add_child(dev, "xlpge", i);
		device_set_ivars(tmpd,
		    &(nae_ivars->block_ivars[block].port_ivars[port]));
		sprintf(desc, "XLP NAE Port %d,%d", block, port);
		device_set_desc_copy(tmpd, desc);
	}
	nlm_setup_iface_fifo_cfg(sc->base, sc->max_ports, sc->portcfg);
	nlm_setup_rx_base_config(sc->base, sc->max_ports, sc->portcfg);
	nlm_setup_rx_buf_config(sc->base, sc->max_ports, sc->portcfg);
	nlm_setup_freein_fifo_cfg(sc->base, sc->portcfg);
	nlm_program_nae_parser_seq_fifo(sc->base, sc->max_ports, sc->portcfg);

	nlm_xlpnae_print_frin_desc_carving(sc);
	bus_generic_probe(dev);
	bus_generic_attach(dev);

	/*
	 * Enable only boot cpu at this point, full distribution comes
	 * only after SMP is started
	 */
	nlm_write_poe_reg(sc->poe_base, POE_DISTR_EN, 0);
	nlm_calc_poe_distvec(0x1, 0, 0, 0, 0x1 << XLPGE_RX_VC, dv);
	nlm_write_poe_distvec(sc->poedv_base, 0, dv);
	nlm_write_poe_reg(sc->poe_base, POE_DISTR_EN, 1);

	return (0);
}

static int
nlm_xlpnae_detach(device_t dev)
{
	/*  TODO - free zone here */
	return (0);
}

static int
nlm_xlpnae_suspend(device_t dev)
{
	return (0);
}

static int
nlm_xlpnae_resume(device_t dev)
{
	return (0);
}

static int
nlm_xlpnae_shutdown(device_t dev)
{
	return (0);
}

/*
 * xlpge driver implementation
 */

static void
nlm_xlpge_mac_set_rx_mode(struct nlm_xlpge_softc *sc)
{
	if (sc->if_flags & IFF_PROMISC) {
		if (sc->type == SGMIIC)
			nlm_nae_setup_rx_mode_sgmii(sc->base_addr,
			    sc->block, sc->port, sc->type, 1 /* broadcast */,
			    1/* multicast */, 0 /* pause */, 1 /* promisc */);
		else
			nlm_nae_setup_rx_mode_xaui(sc->base_addr,
			    sc->block, sc->port, sc->type, 1 /* broadcast */,
			    1/* multicast */, 0 /* pause */, 1 /* promisc */);
	} else {
		if (sc->type == SGMIIC)
			nlm_nae_setup_rx_mode_sgmii(sc->base_addr,
			    sc->block, sc->port, sc->type, 1 /* broadcast */,
			    1/* multicast */, 0 /* pause */, 0 /* promisc */);
		else
			nlm_nae_setup_rx_mode_xaui(sc->base_addr,
			    sc->block, sc->port, sc->type, 1 /* broadcast */,
			    1/* multicast */, 0 /* pause */, 0 /* promisc */);
	}
}

static int
nlm_xlpge_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct mii_data		*mii;
	struct nlm_xlpge_softc	*sc;
	struct ifreq		*ifr;
	int			error;

	sc = ifp->if_softc;
	error = 0;
	ifr = (struct ifreq *)data;

	switch (command) {
	case SIOCSIFFLAGS:
		XLPGE_LOCK(sc);
		sc->if_flags = ifp->if_flags;
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
				nlm_xlpge_init(sc);
			else
				nlm_xlpge_port_enable(sc);
			nlm_xlpge_mac_set_rx_mode(sc);
			sc->link = NLM_LINK_UP;
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				nlm_xlpge_port_disable(sc);
			sc->link = NLM_LINK_DOWN;
		}
		XLPGE_UNLOCK(sc);
		error = 0;
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		if (sc->mii_bus != NULL) {
			mii = device_get_softc(sc->mii_bus);
			error = ifmedia_ioctl(ifp, ifr, &mii->mii_media,
			    command);
		}
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static int
xlpge_tx(struct ifnet *ifp, struct mbuf *mbuf_chain)
{
	struct nlm_fmn_msg	msg;
	struct xlpge_tx_desc	*p2p;
	struct nlm_xlpge_softc	*sc;
	struct mbuf	*m;
	vm_paddr_t      paddr;
	int		fbid, dst, pos, err;
	int		ret = 0, tx_msgstatus, retries;

	err = 0;
	if (mbuf_chain == NULL)
		return (0);

	sc = ifp->if_softc;
	p2p = NULL;
	if (!(ifp->if_drv_flags & IFF_DRV_RUNNING) ||
	    ifp->if_drv_flags & IFF_DRV_OACTIVE) {
		err = ENXIO;
		goto fail;
	}

	/* free a few in coming messages on the fb vc */
	xlp_handle_msg_vc(1 << XLPGE_FB_VC, 2);

	/* vfb id table is setup to map cpu to vc 3 of the cpu */
	fbid = nlm_cpuid();
	dst = sc->txq;

	pos = 0;
	p2p = uma_zalloc(nl_tx_desc_zone, M_NOWAIT);
	if (p2p == NULL) {
		printf("alloc fail\n");
		err = ENOBUFS;
		goto fail;
	}

	for (m = mbuf_chain; m != NULL; m = m->m_next) {
		vm_offset_t buf = (vm_offset_t) m->m_data;
		int	len = m->m_len;
		int	frag_sz;
		uint64_t desc;

		/*printf("m_data = %p len %d\n", m->m_data, len); */
		while (len) {
			if (pos == XLP_NTXFRAGS - 3) {
				device_printf(sc->xlpge_dev,
				    "packet defrag %d\n",
				    m_length(mbuf_chain, NULL));
				err = ENOBUFS; /* TODO fix error */
				goto fail;
			}
			paddr = vtophys(buf);
			frag_sz = PAGE_SIZE - (buf & PAGE_MASK);
			if (len < frag_sz)
				frag_sz = len;
			desc = nae_tx_desc(P2D_NEOP, 0, 127,
			    frag_sz, paddr);
			p2p->frag[pos] = htobe64(desc);
			pos++;
			len -= frag_sz;
			buf += frag_sz;
		}
	}

	KASSERT(pos != 0, ("Zero-length mbuf chain?\n"));

	/* Make the last one P2D EOP */
	p2p->frag[pos-1] |= htobe64((uint64_t)P2D_EOP << 62);

	/* stash useful pointers in the desc */
	p2p->frag[XLP_NTXFRAGS-3] = 0xf00bad;
	p2p->frag[XLP_NTXFRAGS-2] = (uintptr_t)p2p;
	p2p->frag[XLP_NTXFRAGS-1] = (uintptr_t)mbuf_chain;

	paddr = vtophys(p2p);
	msg.msg[0] = nae_tx_desc(P2P, 0, fbid, pos, paddr);

	for (retries = 16;  retries > 0; retries--) {
		ret = nlm_fmn_msgsend(dst, 1, FMN_SWCODE_NAE, &msg);
		if (ret == 0)
			return (0);
	}

fail:
	if (ret != 0) {
		tx_msgstatus = nlm_read_c2_txmsgstatus();
		if ((tx_msgstatus >> 24) & 0x1)
			device_printf(sc->xlpge_dev, "Transmit queue full - ");
		if ((tx_msgstatus >> 3) & 0x1)
			device_printf(sc->xlpge_dev, "ECC error - ");
		if ((tx_msgstatus >> 2) & 0x1)
			device_printf(sc->xlpge_dev, "Pending Sync - ");
		if ((tx_msgstatus >> 1) & 0x1)
			device_printf(sc->xlpge_dev,
			    "Insufficient input queue credits - ");
		if (tx_msgstatus & 0x1)
			device_printf(sc->xlpge_dev,
			    "Insufficient output queue credits - ");
	}
	device_printf(sc->xlpge_dev, "Send failed! err = %d\n", err);
	if (p2p)
		uma_zfree(nl_tx_desc_zone, p2p);
	m_freem(mbuf_chain);
	if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
	return (err);
}


static int
nlm_xlpge_gmac_config_speed(struct nlm_xlpge_softc *sc)
{
	struct mii_data *mii;

	if (sc->type == XAUIC || sc->type == ILC)
		return (0);

	if (sc->mii_bus) {
		mii = device_get_softc(sc->mii_bus);
		mii_pollstat(mii);
	}

	return (0);
}

static void
nlm_xlpge_port_disable(struct nlm_xlpge_softc *sc)
{
	struct ifnet   *ifp;

	ifp = sc->xlpge_if;
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;

	callout_stop(&sc->xlpge_callout);
	nlm_mac_disable(sc->base_addr, sc->block, sc->type, sc->port);
}

static void
nlm_mii_pollstat(void *arg)
{
	struct nlm_xlpge_softc *sc = (struct nlm_xlpge_softc *)arg;
	struct mii_data *mii = NULL;

	if (sc->mii_bus) {
		mii = device_get_softc(sc->mii_bus);

		KASSERT(mii != NULL, ("mii ptr is NULL"));

		mii_pollstat(mii);

		callout_reset(&sc->xlpge_callout, hz,
		    nlm_mii_pollstat, sc);
	}
}

static void
nlm_xlpge_port_enable(struct nlm_xlpge_softc *sc)
{
	if ((sc->type != SGMIIC) && (sc->type != XAUIC))
		return;
	nlm_mac_enable(sc->base_addr, sc->block, sc->type, sc->port);
	nlm_mii_pollstat((void *)sc);
}

static void
nlm_xlpge_init(void *addr)
{
	struct nlm_xlpge_softc *sc;
	struct ifnet   *ifp;
	struct mii_data *mii = NULL;

	sc = (struct nlm_xlpge_softc *)addr;
	ifp = sc->xlpge_if;

	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		return;

	if (sc->mii_bus) {
		mii = device_get_softc(sc->mii_bus);
		mii_mediachg(mii);
	}

	nlm_xlpge_gmac_config_speed(sc);
	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	nlm_xlpge_port_enable(sc);

	/* start the callout */
	callout_reset(&sc->xlpge_callout, hz, nlm_mii_pollstat, sc);
}

/*
 * Read the MAC address from FDT or board eeprom.
 */
static void
xlpge_read_mac_addr(struct nlm_xlpge_softc *sc)
{

	xlpge_get_macaddr(sc->dev_addr);
	/* last octet is port specific */
	sc->dev_addr[5] += (sc->block * 4) + sc->port;

	if (sc->type == SGMIIC)
		nlm_nae_setup_mac_addr_sgmii(sc->base_addr, sc->block,
		    sc->port, sc->type, sc->dev_addr);
	else if (sc->type == XAUIC)
		nlm_nae_setup_mac_addr_xaui(sc->base_addr, sc->block,
		    sc->port, sc->type, sc->dev_addr);
}


static int
xlpge_mediachange(struct ifnet *ifp)
{
	return (0);
}

static void
xlpge_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct nlm_xlpge_softc *sc;
	struct mii_data *md;

	md = NULL;
	sc = ifp->if_softc;

	if (sc->mii_bus)
		md = device_get_softc(sc->mii_bus);

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (sc->link == NLM_LINK_DOWN)
		return;

	if (md != NULL)
		ifmr->ifm_active = md->mii_media.ifm_cur->ifm_media;
	ifmr->ifm_status |= IFM_ACTIVE;
}

static int
nlm_xlpge_ifinit(struct nlm_xlpge_softc *sc)
{
	struct ifnet *ifp;
	device_t dev;
	int port = sc->block * 4 + sc->port;

	dev = sc->xlpge_dev;
	ifp = sc->xlpge_if = if_alloc(IFT_ETHER);
	/*(sc->network_sc)->ifp_ports[port].xlpge_if = ifp;*/
	ifp_ports[port].xlpge_if = ifp;

	if (ifp == NULL) {
		device_printf(dev, "cannot if_alloc()\n");
		return (ENOSPC);
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	sc->if_flags = ifp->if_flags;
	/*ifp->if_capabilities = IFCAP_TXCSUM | IFCAP_VLAN_HWTAGGING;*/
	ifp->if_capabilities = 0;
	ifp->if_capenable = ifp->if_capabilities;
	ifp->if_ioctl = nlm_xlpge_ioctl;
	ifp->if_init  = nlm_xlpge_init ;
	ifp->if_hwassist = 0;
	ifp->if_snd.ifq_drv_maxlen = NLM_XLPGE_TXQ_SIZE; /* TODO: make this a sysint */
	IFQ_SET_MAXLEN(&ifp->if_snd, ifp->if_snd.ifq_drv_maxlen);
	IFQ_SET_READY(&ifp->if_snd);

	ifmedia_init(&sc->xlpge_mii.mii_media, 0, xlpge_mediachange,
	    xlpge_mediastatus);
	ifmedia_add(&sc->xlpge_mii.mii_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->xlpge_mii.mii_media, IFM_ETHER | IFM_AUTO);
	sc->xlpge_mii.mii_media.ifm_media =
	    sc->xlpge_mii.mii_media.ifm_cur->ifm_media;
	xlpge_read_mac_addr(sc);

	ether_ifattach(ifp, sc->dev_addr);

	/* override if_transmit : per ifnet(9), do it after if_attach */
	ifp->if_transmit = xlpge_tx;

	return (0);
}

static int
nlm_xlpge_probe(device_t dev)
{
	return (BUS_PROBE_DEFAULT);
}

static void *
get_buf(void)
{
	struct mbuf     *m_new;
	uint64_t        *md;
#ifdef INVARIANTS
	vm_paddr_t      temp1, temp2;
#endif

	if ((m_new = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR)) == NULL)
		return (NULL);
	m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
	KASSERT(((uintptr_t)m_new->m_data & (NAE_CACHELINE_SIZE - 1)) == 0,
	    ("m_new->m_data is not cacheline aligned"));
	md = (uint64_t *)m_new->m_data;
	md[0] = (intptr_t)m_new;        /* Back Ptr */
	md[1] = 0xf00bad;
	m_adj(m_new, NAE_CACHELINE_SIZE);

#ifdef INVARIANTS
	temp1 = vtophys((vm_offset_t) m_new->m_data);
	temp2 = vtophys((vm_offset_t) m_new->m_data + 1536);
	KASSERT((temp1 + 1536) == temp2,
	    ("Alloced buffer is not contiguous"));
#endif
	return ((void *)m_new->m_data);
}

static void
nlm_xlpge_mii_init(device_t dev, struct nlm_xlpge_softc *sc)
{
	int error;

	error = mii_attach(dev, &sc->mii_bus, sc->xlpge_if,
			xlpge_mediachange, xlpge_mediastatus,
			BMSR_DEFCAPMASK, sc->phy_addr, MII_OFFSET_ANY, 0);

	if (error) {
		device_printf(dev, "attaching PHYs failed\n");
		sc->mii_bus = NULL;
	}

	if (sc->mii_bus != NULL) {
		/* enable MDIO interrupts in the PHY */
		/* XXXJC: TODO */
	}
}

static int
xlpge_stats_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct nlm_xlpge_softc *sc;
	uint32_t val;
	int reg, field;

	sc = arg1;
	field = arg2;
	reg = SGMII_STATS_MLR(sc->block, sc->port) + field;
	val = nlm_read_nae_reg(sc->base_addr, reg);
	return (sysctl_handle_int(oidp, &val, 0, req));
}

static void
nlm_xlpge_setup_stats_sysctl(device_t dev, struct nlm_xlpge_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *child;
	struct sysctl_oid *tree;

	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	child = SYSCTL_CHILDREN(tree);

#define XLPGE_STAT(name, offset, desc) \
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, name,	\
	    CTLTYPE_UINT | CTLFLAG_RD, sc, offset,	\
	    xlpge_stats_sysctl, "IU", desc)

	XLPGE_STAT("tr127", nlm_sgmii_stats_tr127, "TxRx 64 - 127 Bytes");
	XLPGE_STAT("tr255", nlm_sgmii_stats_tr255, "TxRx 128 - 255 Bytes");
	XLPGE_STAT("tr511", nlm_sgmii_stats_tr511, "TxRx 256 - 511 Bytes");
	XLPGE_STAT("tr1k",  nlm_sgmii_stats_tr1k,  "TxRx 512 - 1023 Bytes");
	XLPGE_STAT("trmax", nlm_sgmii_stats_trmax, "TxRx 1024 - 1518 Bytes");
	XLPGE_STAT("trmgv", nlm_sgmii_stats_trmgv, "TxRx 1519 - 1522 Bytes");

	XLPGE_STAT("rbyt", nlm_sgmii_stats_rbyt, "Rx Bytes");
	XLPGE_STAT("rpkt", nlm_sgmii_stats_rpkt, "Rx Packets");
	XLPGE_STAT("rfcs", nlm_sgmii_stats_rfcs, "Rx FCS Error");
	XLPGE_STAT("rmca", nlm_sgmii_stats_rmca, "Rx Multicast Packets");
	XLPGE_STAT("rbca", nlm_sgmii_stats_rbca, "Rx Broadcast Packets");
	XLPGE_STAT("rxcf", nlm_sgmii_stats_rxcf, "Rx Control Frames");
	XLPGE_STAT("rxpf", nlm_sgmii_stats_rxpf, "Rx Pause Frames");
	XLPGE_STAT("rxuo", nlm_sgmii_stats_rxuo, "Rx Unknown Opcode");
	XLPGE_STAT("raln", nlm_sgmii_stats_raln, "Rx Alignment Errors");
	XLPGE_STAT("rflr", nlm_sgmii_stats_rflr, "Rx Framelength Errors");
	XLPGE_STAT("rcde", nlm_sgmii_stats_rcde, "Rx Code Errors");
	XLPGE_STAT("rcse", nlm_sgmii_stats_rcse, "Rx Carrier Sense Errors");
	XLPGE_STAT("rund", nlm_sgmii_stats_rund, "Rx Undersize Packet Errors");
	XLPGE_STAT("rovr", nlm_sgmii_stats_rovr, "Rx Oversize Packet Errors");
	XLPGE_STAT("rfrg", nlm_sgmii_stats_rfrg, "Rx Fragments");
	XLPGE_STAT("rjbr", nlm_sgmii_stats_rjbr, "Rx Jabber");

	XLPGE_STAT("tbyt", nlm_sgmii_stats_tbyt, "Tx Bytes");
	XLPGE_STAT("tpkt", nlm_sgmii_stats_tpkt, "Tx Packets");
	XLPGE_STAT("tmca", nlm_sgmii_stats_tmca, "Tx Multicast Packets");
	XLPGE_STAT("tbca", nlm_sgmii_stats_tbca, "Tx Broadcast Packets");
	XLPGE_STAT("txpf", nlm_sgmii_stats_txpf, "Tx Pause Frame");
	XLPGE_STAT("tdfr", nlm_sgmii_stats_tdfr, "Tx Deferral Packets");
	XLPGE_STAT("tedf", nlm_sgmii_stats_tedf, "Tx Excessive Deferral Pkts");
	XLPGE_STAT("tscl", nlm_sgmii_stats_tscl, "Tx Single Collisions");
	XLPGE_STAT("tmcl", nlm_sgmii_stats_tmcl, "Tx Multiple Collisions");
	XLPGE_STAT("tlcl", nlm_sgmii_stats_tlcl, "Tx Late Collision Pkts");
	XLPGE_STAT("txcl", nlm_sgmii_stats_txcl, "Tx Excessive Collisions");
	XLPGE_STAT("tncl", nlm_sgmii_stats_tncl, "Tx Total Collisions");
	XLPGE_STAT("tjbr", nlm_sgmii_stats_tjbr, "Tx Jabber Frames");
	XLPGE_STAT("tfcs", nlm_sgmii_stats_tfcs, "Tx FCS Errors");
	XLPGE_STAT("txcf", nlm_sgmii_stats_txcf, "Tx Control Frames");
	XLPGE_STAT("tovr", nlm_sgmii_stats_tovr, "Tx Oversize Frames");
	XLPGE_STAT("tund", nlm_sgmii_stats_tund, "Tx Undersize Frames");
	XLPGE_STAT("tfrg", nlm_sgmii_stats_tfrg, "Tx Fragments");
#undef XLPGE_STAT
}

static int
nlm_xlpge_attach(device_t dev)
{
	struct xlp_port_ivars *pv;
	struct nlm_xlpge_softc *sc;
	int port;

	pv = device_get_ivars(dev);
	sc = device_get_softc(dev);
	sc->xlpge_dev = dev;
	sc->mii_bus = NULL;
	sc->block = pv->block;
	sc->node = pv->node;
	sc->port = pv->port;
	sc->type = pv->type;
	sc->xlpge_if = NULL;
	sc->phy_addr = pv->phy_addr;
	sc->mdio_bus = pv->mdio_bus;
	sc->portcfg = nae_port_config;
	sc->hw_parser_en = pv->hw_parser_en;

	/* default settings */
	sc->speed = NLM_SGMII_SPEED_10;
	sc->duplexity = NLM_SGMII_DUPLEX_FULL;
	sc->link = NLM_LINK_DOWN;
	sc->flowctrl = NLM_FLOWCTRL_DISABLED;

	sc->network_sc = device_get_softc(device_get_parent(dev));
	sc->base_addr = sc->network_sc->base;
	sc->prepad_en = sc->network_sc->prepad_en;
	sc->prepad_size = sc->network_sc->prepad_size;

	callout_init(&sc->xlpge_callout, 1);

	XLPGE_LOCK_INIT(sc, device_get_nameunit(dev));

	port = (sc->block*4)+sc->port;
	sc->nfree_desc = nae_port_config[port].num_free_descs;
	sc->txq = nae_port_config[port].txq;
	sc->rxfreeq = nae_port_config[port].rxfreeq;

	nlm_xlpge_submit_rx_free_desc(sc, sc->nfree_desc);
	if (sc->hw_parser_en)
		nlm_enable_hardware_parser_per_port(sc->base_addr,
		    sc->block, sc->port);

	nlm_xlpge_ifinit(sc);
	ifp_ports[port].xlpge_sc = sc;
	nlm_xlpge_mii_init(dev, sc);

	nlm_xlpge_setup_stats_sysctl(dev, sc);

	return (0);
}

static int
nlm_xlpge_detach(device_t dev)
{
	return (0);
}

static int
nlm_xlpge_suspend(device_t dev)
{
	return (0);
}

static int
nlm_xlpge_resume(device_t dev)
{
	return (0);
}

static int
nlm_xlpge_shutdown(device_t dev)
{
	return (0);
}

/*
 * miibus function with custom implementation
 */
static int
nlm_xlpge_mii_read(device_t dev, int phyaddr, int regidx)
{
	struct nlm_xlpge_softc *sc;
	int val;

	sc = device_get_softc(dev);
	if (sc->type == SGMIIC)
		val = nlm_gmac_mdio_read(sc->base_addr, sc->mdio_bus,
		    BLOCK_7, LANE_CFG, phyaddr, regidx);
	else
		val = 0xffff;

	return (val);
}

static int
nlm_xlpge_mii_write(device_t dev, int phyaddr, int regidx, int val)
{
	struct nlm_xlpge_softc *sc;

	sc = device_get_softc(dev);
	if (sc->type == SGMIIC)
		nlm_gmac_mdio_write(sc->base_addr, sc->mdio_bus, BLOCK_7,
		    LANE_CFG, phyaddr, regidx, val);

	return (0);
}

static void
nlm_xlpge_mii_statchg(device_t dev)
{
	struct nlm_xlpge_softc *sc;
	struct mii_data *mii;
	char *speed, *duplexity;

	sc = device_get_softc(dev);
	if (sc->mii_bus == NULL)
		return;

	mii = device_get_softc(sc->mii_bus);
	if (mii->mii_media_status & IFM_ACTIVE) {
		if (IFM_SUBTYPE(mii->mii_media_active) ==  IFM_10_T) {
			sc->speed = NLM_SGMII_SPEED_10;
			speed =  "10Mbps";
		} else if (IFM_SUBTYPE(mii->mii_media_active) == IFM_100_TX) {
			sc->speed = NLM_SGMII_SPEED_100;
			speed = "100Mbps";
		} else { /* default to 1G */
			sc->speed = NLM_SGMII_SPEED_1000;
			speed =  "1Gbps";
		}

		if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX) {
			sc->duplexity = NLM_SGMII_DUPLEX_FULL;
			duplexity =  "full";
		} else {
			sc->duplexity = NLM_SGMII_DUPLEX_HALF;
			duplexity = "half";
		}

		printf("Port [%d, %d] setup with speed=%s duplex=%s\n",
		    sc->block, sc->port, speed, duplexity);

		nlm_nae_setup_mac(sc->base_addr, sc->block, sc->port, 0, 1, 1,
		    sc->speed, sc->duplexity);
	}
}

/*
 * xlpge support function implementations
 */
static void
nlm_xlpge_release_mbuf(uint64_t paddr)
{
	uint64_t	mag, desc, mbuf;

	paddr += (XLP_NTXFRAGS - 3) * sizeof(uint64_t);
	mag = nlm_paddr_ld(paddr);
	desc = nlm_paddr_ld(paddr + sizeof(uint64_t));
	mbuf = nlm_paddr_ld(paddr + 2 * sizeof(uint64_t));

	if (mag != 0xf00bad) {
		/* somebody else packet Error - FIXME in intialization */
		printf("cpu %d: ERR Tx packet paddr %jx, mag %jx, desc %jx mbuf %jx\n",
		    nlm_cpuid(), (uintmax_t)paddr, (uintmax_t)mag,
		    (intmax_t)desc, (uintmax_t)mbuf);
		return;
	}
	m_freem((struct mbuf *)(uintptr_t)mbuf);
	uma_zfree(nl_tx_desc_zone, (void *)(uintptr_t)desc);
}

static void
nlm_xlpge_rx(struct nlm_xlpge_softc *sc, int port, vm_paddr_t paddr, int len)
{
	struct ifnet	*ifp;
	struct mbuf	*m;
	vm_offset_t	temp;
	unsigned long	mag;
	int		prepad_size;

	ifp = sc->xlpge_if;
	temp = nlm_paddr_ld(paddr - NAE_CACHELINE_SIZE);
	mag = nlm_paddr_ld(paddr - NAE_CACHELINE_SIZE + sizeof(uint64_t));

	m = (struct mbuf *)(intptr_t)temp;
	if (mag != 0xf00bad) {
		/* somebody else packet Error - FIXME in intialization */
		printf("cpu %d: ERR Rx packet paddr %jx, temp %p, mag %lx\n",
		    nlm_cpuid(), (uintmax_t)paddr, (void *)temp, mag);
		return;
	}

	m->m_pkthdr.rcvif = ifp;

#ifdef DUMP_PACKET
	{
		int     i = 0, j = 64;
		unsigned char *buf = (char *)m->m_data;
		printf("(cpu_%d: nlge_rx, !RX_COPY) Rx Packet: length=%d\n",
				nlm_cpuid(), len);
		if (len < j)
			j = len;
		if (sc->prepad_en)
			j += ((sc->prepad_size + 1) * 16);
		for (i = 0; i < j; i++) {
			if (i && (i % 16) == 0)
				printf("\n");
			printf("%02x ", buf[i]);
		}
		printf("\n");
	}
#endif

	if (sc->prepad_en) {
		prepad_size = ((sc->prepad_size + 1) * 16);
		m->m_data += prepad_size;
		m->m_pkthdr.len = m->m_len = (len - prepad_size);
	} else
		m->m_pkthdr.len = m->m_len = len;

	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
#ifdef XLP_DRIVER_LOOPBACK
	if (port == 16 || port == 17)
		(*ifp->if_input)(ifp, m);
	else
		xlpge_tx(ifp, m);
#else
	(*ifp->if_input)(ifp, m);
#endif
}

void
nlm_xlpge_submit_rx_free_desc(struct nlm_xlpge_softc *sc, int num)
{
	int i, size, ret, n;
	struct nlm_fmn_msg msg;
	void *ptr;

	for(i = 0; i < num; i++) {
		memset(&msg, 0, sizeof(msg));
		ptr = get_buf();
		if (!ptr) {
			device_printf(sc->xlpge_dev, "Cannot allocate mbuf\n");
			break;
		}

		msg.msg[0] = vtophys(ptr);
		if (msg.msg[0] == 0) {
			printf("Bad ptr for %p\n", ptr);
			break;
		}
		size = 1;

		n = 0;
		while (1) {
			/* on success returns 1, else 0 */
			ret = nlm_fmn_msgsend(sc->rxfreeq, size, 0, &msg);
			if (ret == 0)
				break;
			if (n++ > 10000) {
				printf("Too many credit fails for send free desc\n");
				break;
			}
		}
	}
}

void
nlm_xlpge_msgring_handler(int vc, int size, int code, int src_id,
    struct nlm_fmn_msg *msg, void *data)
{
	uint64_t phys_addr;
	struct nlm_xlpnae_softc *sc;
	struct nlm_xlpge_softc *xlpge_sc;
	struct ifnet *ifp;
	uint32_t context;
	uint32_t port = 0;
	uint32_t length;

	sc = (struct nlm_xlpnae_softc *)data;
	KASSERT(sc != NULL, ("Null sc in msgring handler"));

	if (size == 1) { /* process transmit complete */
		phys_addr = msg->msg[0] & 0xffffffffffULL;

		/* context is SGMII_RCV_CONTEXT_NUM + three bit vlan type
		 * or vlan priority
		 */
		context = (msg->msg[0] >> 40) & 0x3fff;
		port = cntx2port[context];

		if (port >= XLP_MAX_PORTS) {
			printf("%s:%d Bad port %d (context=%d)\n",
				__func__, __LINE__, port, context);
			return;
		}
		ifp = ifp_ports[port].xlpge_if;
		xlpge_sc = ifp_ports[port].xlpge_sc;

		nlm_xlpge_release_mbuf(phys_addr);

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

	} else if (size > 1) { /* Recieve packet */
		phys_addr = msg->msg[1] & 0xffffffffc0ULL;
		length = (msg->msg[1] >> 40) & 0x3fff;
		length -= MAC_CRC_LEN;

		/* context is SGMII_RCV_CONTEXT_NUM + three bit vlan type
		 * or vlan priority
		 */
		context = (msg->msg[1] >> 54) & 0x3ff;
		port = cntx2port[context];

		if (port >= XLP_MAX_PORTS) {
			printf("%s:%d Bad port %d (context=%d)\n",
				__func__, __LINE__, port, context);
			return;
		}

		ifp = ifp_ports[port].xlpge_if;
		xlpge_sc = ifp_ports[port].xlpge_sc;

		nlm_xlpge_rx(xlpge_sc, port, phys_addr, length);
		/* return back a free descriptor to NA */
		nlm_xlpge_submit_rx_free_desc(xlpge_sc, 1);
	}
}
