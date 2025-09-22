/*	$OpenBSD: if_mvpp.c,v 1.53 2024/05/13 01:15:50 jsg Exp $	*/
/*
 * Copyright (c) 2008, 2019 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2017, 2020 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
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
 * Copyright (C) 2016 Marvell International Ltd.
 *
 * Marvell BSD License Option
 *
 * If you received this File from Marvell, you may opt to use, redistribute
 * and/or modify this File under the following licensing terms.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *   * Neither the name of Marvell nor the names of its contributors may be
 *     used to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/timeout.h>

#include <uvm/uvm_extern.h>

#include <machine/cpufunc.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/ppp_defs.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>

#include <netinet6/in6_var.h>
#include <netinet/ip6.h>

#include <dev/fdt/if_mvppreg.h>

struct mvpp2_buf {
	bus_dmamap_t		mb_map;
	struct mbuf		*mb_m;
};

#define MVPP2_NTXDESC	512
#define MVPP2_NTXSEGS	16
#define MVPP2_NRXDESC	512

struct mvpp2_bm_pool {
	struct mvpp2_dmamem	*bm_mem;
	struct mvpp2_buf	*rxbuf;
	uint32_t		*freelist;
	int			free_prod;
	int			free_cons;
};

#define MVPP2_BM_SIZE		64
#define MVPP2_BM_POOL_PTR_ALIGN	128
#define MVPP2_BM_POOLS_NUM	8
#define MVPP2_BM_ALIGN		32

struct mvpp2_tx_queue {
	uint8_t			id;
	uint8_t			log_id;
	struct mvpp2_dmamem	*ring;
	struct mvpp2_buf	*buf;
	struct mvpp2_tx_desc	*descs;
	int			prod;
	int			cons;

	uint32_t		done_pkts_coal;
};

struct mvpp2_rx_queue {
	uint8_t			id;
	struct mvpp2_dmamem	*ring;
	struct mvpp2_rx_desc	*descs;
	int			prod;
	struct if_rxring	rxring;
	int			cons;

	uint32_t		pkts_coal;
	uint32_t		time_coal;
};

struct mvpp2_dmamem {
	bus_dmamap_t		mdm_map;
	bus_dma_segment_t	mdm_seg;
	size_t			mdm_size;
	caddr_t			mdm_kva;
};
#define MVPP2_DMA_MAP(_mdm)	((_mdm)->mdm_map)
#define MVPP2_DMA_LEN(_mdm)	((_mdm)->mdm_size)
#define MVPP2_DMA_DVA(_mdm)	((_mdm)->mdm_map->dm_segs[0].ds_addr)
#define MVPP2_DMA_KVA(_mdm)	((void *)(_mdm)->mdm_kva)

struct mvpp2_port;
struct mvpp2_softc {
	struct device		sc_dev;
	int			sc_node;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh_base;
	bus_space_handle_t	sc_ioh_iface;
	paddr_t			sc_ioh_paddr;
	bus_size_t		sc_iosize_base;
	bus_size_t		sc_iosize_iface;
	bus_dma_tag_t		sc_dmat;
	struct regmap		*sc_rm;

	uint32_t		sc_tclk;

	struct mvpp2_bm_pool	*sc_bm_pools;
	int			sc_npools;

	struct mvpp2_prs_shadow	*sc_prs_shadow;
	uint8_t			*sc_prs_double_vlans;

	int			sc_aggr_ntxq;
	struct mvpp2_tx_queue	*sc_aggr_txqs;

	struct mvpp2_port	**sc_ports;
};

struct mvpp2_port {
	struct device		sc_dev;
	struct mvpp2_softc	*sc;
	int			sc_node;
	bus_dma_tag_t		sc_dmat;
	int			sc_id;
	int			sc_gop_id;

	struct arpcom		sc_ac;
#define sc_lladdr	sc_ac.ac_enaddr
	struct mii_data		sc_mii;
#define sc_media	sc_mii.mii_media
	struct mii_bus		*sc_mdio;

	enum {
		PHY_MODE_XAUI,
		PHY_MODE_10GBASER,
		PHY_MODE_2500BASEX,
		PHY_MODE_1000BASEX,
		PHY_MODE_SGMII,
		PHY_MODE_RGMII,
		PHY_MODE_RGMII_ID,
		PHY_MODE_RGMII_RXID,
		PHY_MODE_RGMII_TXID,
	}			sc_phy_mode;
	int			sc_fixed_link;
	int			sc_inband_status;
	int			sc_link;
	int			sc_phyloc;
	int			sc_sfp;

	int			sc_ntxq;
	int			sc_nrxq;

	struct mvpp2_tx_queue	*sc_txqs;
	struct mvpp2_rx_queue	*sc_rxqs;

	struct timeout		sc_tick;

	uint32_t		sc_tx_time_coal;
};

#define MVPP2_MAX_PORTS		4

struct mvpp2_attach_args {
	int			ma_node;
	bus_dma_tag_t		ma_dmat;
};

#define DEVNAME(_s)	((_s)->sc_dev.dv_xname)

static struct rwlock mvpp2_sff_lock = RWLOCK_INITIALIZER("mvpp2sff");

int	mvpp2_match(struct device *, void *, void *);
void	mvpp2_attach(struct device *, struct device *, void *);
void	mvpp2_attach_deferred(struct device *);

const struct cfattach mvppc_ca = {
	sizeof(struct mvpp2_softc), mvpp2_match, mvpp2_attach
};

struct cfdriver mvppc_cd = {
	NULL, "mvppc", DV_DULL
};

int	mvpp2_port_match(struct device *, void *, void *);
void	mvpp2_port_attach(struct device *, struct device *, void *);

const struct cfattach mvpp_ca = {
	sizeof(struct mvpp2_port), mvpp2_port_match, mvpp2_port_attach
};

struct cfdriver mvpp_cd = {
	NULL, "mvpp", DV_IFNET
};

void	mvpp2_port_attach_sfp(struct device *);

uint32_t mvpp2_read(struct mvpp2_softc *, bus_addr_t);
void	mvpp2_write(struct mvpp2_softc *, bus_addr_t, uint32_t);
uint32_t mvpp2_gmac_read(struct mvpp2_port *, bus_addr_t);
void	mvpp2_gmac_write(struct mvpp2_port *, bus_addr_t, uint32_t);
uint32_t mvpp2_xlg_read(struct mvpp2_port *, bus_addr_t);
void	mvpp2_xlg_write(struct mvpp2_port *, bus_addr_t, uint32_t);
uint32_t mvpp2_xpcs_read(struct mvpp2_port *, bus_addr_t);
void	mvpp2_xpcs_write(struct mvpp2_port *, bus_addr_t, uint32_t);
uint32_t mvpp2_mpcs_read(struct mvpp2_port *, bus_addr_t);
void	mvpp2_mpcs_write(struct mvpp2_port *, bus_addr_t, uint32_t);

int	mvpp2_ioctl(struct ifnet *, u_long, caddr_t);
void	mvpp2_start(struct ifnet *);
int	mvpp2_rxrinfo(struct mvpp2_port *, struct if_rxrinfo *);
void	mvpp2_watchdog(struct ifnet *);

int	mvpp2_media_change(struct ifnet *);
void	mvpp2_media_status(struct ifnet *, struct ifmediareq *);

int	mvpp2_mii_readreg(struct device *, int, int);
void	mvpp2_mii_writereg(struct device *, int, int, int);
void	mvpp2_mii_statchg(struct device *);
void	mvpp2_inband_statchg(struct mvpp2_port *);
void	mvpp2_port_change(struct mvpp2_port *);

void	mvpp2_tick(void *);

int	mvpp2_link_intr(void *);
int	mvpp2_intr(void *);
void	mvpp2_tx_proc(struct mvpp2_port *, uint8_t);
void	mvpp2_txq_proc(struct mvpp2_port *, struct mvpp2_tx_queue *);
void	mvpp2_rx_proc(struct mvpp2_port *, uint8_t);
void	mvpp2_rxq_proc(struct mvpp2_port *, struct mvpp2_rx_queue *);
void	mvpp2_rx_refill(struct mvpp2_port *);

void	mvpp2_up(struct mvpp2_port *);
void	mvpp2_down(struct mvpp2_port *);
void	mvpp2_iff(struct mvpp2_port *);

void	mvpp2_aggr_txq_hw_init(struct mvpp2_softc *, struct mvpp2_tx_queue *);
void	mvpp2_txq_hw_init(struct mvpp2_port *, struct mvpp2_tx_queue *);
void	mvpp2_rxq_hw_init(struct mvpp2_port *, struct mvpp2_rx_queue *);
void	mvpp2_txq_hw_deinit(struct mvpp2_port *, struct mvpp2_tx_queue *);
void	mvpp2_rxq_hw_drop(struct mvpp2_port *, struct mvpp2_rx_queue *);
void	mvpp2_rxq_hw_deinit(struct mvpp2_port *, struct mvpp2_rx_queue *);
void	mvpp2_rxq_long_pool_set(struct mvpp2_port *, int, int);
void	mvpp2_rxq_short_pool_set(struct mvpp2_port *, int, int);

void	mvpp2_mac_reset_assert(struct mvpp2_port *);
void	mvpp2_pcs_reset_assert(struct mvpp2_port *);
void	mvpp2_pcs_reset_deassert(struct mvpp2_port *);
void	mvpp2_mac_config(struct mvpp2_port *);
void	mvpp2_xlg_config(struct mvpp2_port *);
void	mvpp2_gmac_config(struct mvpp2_port *);
void	mvpp2_comphy_config(struct mvpp2_port *, int);
void	mvpp2_gop_config(struct mvpp2_port *);
void	mvpp2_gop_intr_mask(struct mvpp2_port *);
void	mvpp2_gop_intr_unmask(struct mvpp2_port *);

struct mvpp2_dmamem *
	mvpp2_dmamem_alloc(struct mvpp2_softc *, bus_size_t, bus_size_t);
void	mvpp2_dmamem_free(struct mvpp2_softc *, struct mvpp2_dmamem *);
struct mbuf *mvpp2_alloc_mbuf(struct mvpp2_softc *, bus_dmamap_t);

void	mvpp2_interrupts_enable(struct mvpp2_port *, int);
void	mvpp2_interrupts_disable(struct mvpp2_port *, int);
int	mvpp2_egress_port(struct mvpp2_port *);
int	mvpp2_txq_phys(int, int);
void	mvpp2_defaults_set(struct mvpp2_port *);
void	mvpp2_ingress_enable(struct mvpp2_port *);
void	mvpp2_ingress_disable(struct mvpp2_port *);
void	mvpp2_egress_enable(struct mvpp2_port *);
void	mvpp2_egress_disable(struct mvpp2_port *);
void	mvpp2_port_enable(struct mvpp2_port *);
void	mvpp2_port_disable(struct mvpp2_port *);
void	mvpp2_rxq_status_update(struct mvpp2_port *, int, int, int);
int	mvpp2_rxq_received(struct mvpp2_port *, int);
void	mvpp2_rxq_offset_set(struct mvpp2_port *, int, int);
void	mvpp2_txp_max_tx_size_set(struct mvpp2_port *);
void	mvpp2_rx_pkts_coal_set(struct mvpp2_port *, struct mvpp2_rx_queue *,
	    uint32_t);
void	mvpp2_tx_pkts_coal_set(struct mvpp2_port *, struct mvpp2_tx_queue *,
	    uint32_t);
void	mvpp2_rx_time_coal_set(struct mvpp2_port *, struct mvpp2_rx_queue *,
	    uint32_t);
void	mvpp2_tx_time_coal_set(struct mvpp2_port *, uint32_t);

void	mvpp2_axi_config(struct mvpp2_softc *);
void	mvpp2_bm_pool_init(struct mvpp2_softc *);
void	mvpp2_rx_fifo_init(struct mvpp2_softc *);
void	mvpp2_tx_fifo_init(struct mvpp2_softc *);
int	mvpp2_prs_default_init(struct mvpp2_softc *);
void	mvpp2_prs_hw_inv(struct mvpp2_softc *, int);
void	mvpp2_prs_hw_port_init(struct mvpp2_softc *, int, int, int, int);
void	mvpp2_prs_def_flow_init(struct mvpp2_softc *);
void	mvpp2_prs_mh_init(struct mvpp2_softc *);
void	mvpp2_prs_mac_init(struct mvpp2_softc *);
void	mvpp2_prs_dsa_init(struct mvpp2_softc *);
int	mvpp2_prs_etype_init(struct mvpp2_softc *);
int	mvpp2_prs_vlan_init(struct mvpp2_softc *);
int	mvpp2_prs_pppoe_init(struct mvpp2_softc *);
int	mvpp2_prs_ip6_init(struct mvpp2_softc *);
int	mvpp2_prs_ip4_init(struct mvpp2_softc *);
void	mvpp2_prs_shadow_ri_set(struct mvpp2_softc *, int,
	    uint32_t, uint32_t);
void	mvpp2_prs_tcam_lu_set(struct mvpp2_prs_entry *, uint32_t);
void	mvpp2_prs_tcam_port_set(struct mvpp2_prs_entry *, uint32_t, int);
void	mvpp2_prs_tcam_port_map_set(struct mvpp2_prs_entry *, uint32_t);
uint32_t mvpp2_prs_tcam_port_map_get(struct mvpp2_prs_entry *);
void	mvpp2_prs_tcam_data_byte_set(struct mvpp2_prs_entry *, uint32_t,
	    uint8_t, uint8_t);
void	mvpp2_prs_tcam_data_byte_get(struct mvpp2_prs_entry *, uint32_t,
	    uint8_t *, uint8_t *);
int	mvpp2_prs_tcam_data_cmp(struct mvpp2_prs_entry *, int, uint16_t);
void	mvpp2_prs_tcam_ai_update(struct mvpp2_prs_entry *, uint32_t, uint32_t);
int	mvpp2_prs_sram_ai_get(struct mvpp2_prs_entry *);
int	mvpp2_prs_tcam_ai_get(struct mvpp2_prs_entry *);
void	mvpp2_prs_tcam_data_word_get(struct mvpp2_prs_entry *, uint32_t,
	    uint32_t *, uint32_t *);
void	mvpp2_prs_match_etype(struct mvpp2_prs_entry *, uint32_t, uint16_t);
int	mvpp2_prs_sram_ri_get(struct mvpp2_prs_entry *);
void	mvpp2_prs_sram_ai_update(struct mvpp2_prs_entry *, uint32_t, uint32_t);
void	mvpp2_prs_sram_ri_update(struct mvpp2_prs_entry *, uint32_t, uint32_t);
void	mvpp2_prs_sram_bits_set(struct mvpp2_prs_entry *, uint32_t, uint32_t);
void	mvpp2_prs_sram_bits_clear(struct mvpp2_prs_entry *, uint32_t, uint32_t);
void	mvpp2_prs_sram_shift_set(struct mvpp2_prs_entry *, int, uint32_t);
void	mvpp2_prs_sram_offset_set(struct mvpp2_prs_entry *, uint32_t, int,
	    uint32_t);
void	mvpp2_prs_sram_next_lu_set(struct mvpp2_prs_entry *, uint32_t);
void	mvpp2_prs_shadow_set(struct mvpp2_softc *, int, uint32_t);
int	mvpp2_prs_hw_write(struct mvpp2_softc *, struct mvpp2_prs_entry *);
int	mvpp2_prs_hw_read(struct mvpp2_softc *, struct mvpp2_prs_entry *, int);
int	mvpp2_prs_flow_find(struct mvpp2_softc *, int);
int	mvpp2_prs_tcam_first_free(struct mvpp2_softc *, uint8_t, uint8_t);
void	mvpp2_prs_mac_drop_all_set(struct mvpp2_softc *, uint32_t, int);
void	mvpp2_prs_mac_promisc_set(struct mvpp2_softc *, uint32_t, int, int);
void	mvpp2_prs_dsa_tag_set(struct mvpp2_softc *, uint32_t, int, int, int);
void	mvpp2_prs_dsa_tag_ethertype_set(struct mvpp2_softc *, uint32_t,
	    int, int, int);
struct mvpp2_prs_entry *mvpp2_prs_vlan_find(struct mvpp2_softc *, uint16_t,
	    int);
int	mvpp2_prs_vlan_add(struct mvpp2_softc *, uint16_t, int, uint32_t);
int	mvpp2_prs_double_vlan_ai_free_get(struct mvpp2_softc *);
struct mvpp2_prs_entry *mvpp2_prs_double_vlan_find(struct mvpp2_softc *,
	    uint16_t, uint16_t);
int	mvpp2_prs_double_vlan_add(struct mvpp2_softc *, uint16_t, uint16_t,
	    uint32_t);
int	mvpp2_prs_ip4_proto(struct mvpp2_softc *, uint16_t, uint32_t, uint32_t);
int	mvpp2_prs_ip4_cast(struct mvpp2_softc *, uint16_t);
int	mvpp2_prs_ip6_proto(struct mvpp2_softc *, uint16_t, uint32_t, uint32_t);
int	mvpp2_prs_ip6_cast(struct mvpp2_softc *, uint16_t);
int	mvpp2_prs_mac_da_range_find(struct mvpp2_softc *, int, const uint8_t *,
	    uint8_t *, int);
int	mvpp2_prs_mac_range_equals(struct mvpp2_prs_entry *, const uint8_t *,
	    uint8_t *);
int	mvpp2_prs_mac_da_accept(struct mvpp2_port *, const uint8_t *, int);
void	mvpp2_prs_mac_del_all(struct mvpp2_port *);
int	mvpp2_prs_tag_mode_set(struct mvpp2_softc *, int, int);
int	mvpp2_prs_def_flow(struct mvpp2_port *);
void	mvpp2_cls_flow_write(struct mvpp2_softc *, struct mvpp2_cls_flow_entry *);
void	mvpp2_cls_lookup_write(struct mvpp2_softc *, struct mvpp2_cls_lookup_entry *);
void	mvpp2_cls_init(struct mvpp2_softc *);
void	mvpp2_cls_port_config(struct mvpp2_port *);
void	mvpp2_cls_oversize_rxq_set(struct mvpp2_port *);

int
mvpp2_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,armada-7k-pp22");
}

void
mvpp2_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvpp2_softc *sc = (void *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 2) {
		printf(": no registers\n");
		return;
	}

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	sc->sc_dmat = faa->fa_dmat;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh_base)) {
		printf(": can't map registers\n");
		return;
	}
	sc->sc_iosize_base = faa->fa_reg[0].size;

	sc->sc_ioh_paddr = bus_space_mmap(sc->sc_iot, faa->fa_reg[0].addr,
	    0, PROT_READ | PROT_WRITE, 0);
	KASSERT(sc->sc_ioh_paddr != -1);
	sc->sc_ioh_paddr &= PMAP_PA_MASK;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
	    faa->fa_reg[1].size, 0, &sc->sc_ioh_iface)) {
		printf(": can't map registers\n");
		bus_space_unmap(sc->sc_iot, sc->sc_ioh_base,
		    sc->sc_iosize_base);
		return;
	}
	sc->sc_iosize_iface = faa->fa_reg[1].size;

	sc->sc_rm = regmap_byphandle(OF_getpropint(faa->fa_node,
	    "marvell,system-controller", 0));

	clock_enable_all(faa->fa_node);
	sc->sc_tclk = clock_get_frequency(faa->fa_node, "pp_clk");

	printf("\n");

	config_defer(self, mvpp2_attach_deferred);
}

void
mvpp2_attach_deferred(struct device *self)
{
	struct mvpp2_softc *sc = (void *)self;
	struct mvpp2_attach_args maa;
	struct mvpp2_tx_queue *txq;
	int i, node;

	mvpp2_axi_config(sc);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh_iface, MVPP22_SMI_MISC_CFG_REG,
	    bus_space_read_4(sc->sc_iot, sc->sc_ioh_iface,
	    MVPP22_SMI_MISC_CFG_REG) & ~MVPP22_SMI_POLLING_EN);

	sc->sc_aggr_ntxq = 1;
	sc->sc_aggr_txqs = mallocarray(sc->sc_aggr_ntxq,
	    sizeof(*sc->sc_aggr_txqs), M_DEVBUF, M_WAITOK | M_ZERO);

	for (i = 0; i < sc->sc_aggr_ntxq; i++) {
		txq = &sc->sc_aggr_txqs[i];
		txq->id = i;
		mvpp2_aggr_txq_hw_init(sc, txq);
	}

	mvpp2_rx_fifo_init(sc);
	mvpp2_tx_fifo_init(sc);

	mvpp2_write(sc, MVPP2_TX_SNOOP_REG, 0x1);

	mvpp2_bm_pool_init(sc);

	sc->sc_prs_shadow = mallocarray(MVPP2_PRS_TCAM_SRAM_SIZE,
	    sizeof(*sc->sc_prs_shadow), M_DEVBUF, M_WAITOK | M_ZERO);

	mvpp2_prs_default_init(sc);
	mvpp2_cls_init(sc);

	memset(&maa, 0, sizeof(maa));
	for (node = OF_child(sc->sc_node); node; node = OF_peer(node)) {
		maa.ma_node = node;
		maa.ma_dmat = sc->sc_dmat;
		config_found(self, &maa, NULL);
	}
}

void
mvpp2_axi_config(struct mvpp2_softc *sc)
{
	uint32_t reg;

	mvpp2_write(sc, MVPP22_BM_ADDR_HIGH_RLS_REG, 0);

	reg = (MVPP22_AXI_CODE_CACHE_WR_CACHE << MVPP22_AXI_ATTR_CACHE_OFFS) |
	    (MVPP22_AXI_CODE_DOMAIN_OUTER_DOM << MVPP22_AXI_ATTR_DOMAIN_OFFS);
	mvpp2_write(sc, MVPP22_AXI_BM_WR_ATTR_REG, reg);
	mvpp2_write(sc, MVPP22_AXI_TXQ_DESCR_WR_ATTR_REG, reg);
	mvpp2_write(sc, MVPP22_AXI_RXQ_DESCR_WR_ATTR_REG, reg);
	mvpp2_write(sc, MVPP22_AXI_RX_DATA_WR_ATTR_REG, reg);

	reg = (MVPP22_AXI_CODE_CACHE_RD_CACHE << MVPP22_AXI_ATTR_CACHE_OFFS) |
	    (MVPP22_AXI_CODE_DOMAIN_OUTER_DOM << MVPP22_AXI_ATTR_DOMAIN_OFFS);
	mvpp2_write(sc, MVPP22_AXI_BM_RD_ATTR_REG, reg);
	mvpp2_write(sc, MVPP22_AXI_AGGRQ_DESCR_RD_ATTR_REG, reg);
	mvpp2_write(sc, MVPP22_AXI_TXQ_DESCR_RD_ATTR_REG, reg);
	mvpp2_write(sc, MVPP22_AXI_TX_DATA_RD_ATTR_REG, reg);

	reg = (MVPP22_AXI_CODE_CACHE_NON_CACHE << MVPP22_AXI_CODE_CACHE_OFFS) |
	    (MVPP22_AXI_CODE_DOMAIN_SYSTEM << MVPP22_AXI_CODE_DOMAIN_OFFS);
	mvpp2_write(sc, MVPP22_AXI_RD_NORMAL_CODE_REG, reg);
	mvpp2_write(sc, MVPP22_AXI_WR_NORMAL_CODE_REG, reg);

	reg = (MVPP22_AXI_CODE_CACHE_RD_CACHE << MVPP22_AXI_CODE_CACHE_OFFS) |
	    (MVPP22_AXI_CODE_DOMAIN_OUTER_DOM << MVPP22_AXI_CODE_DOMAIN_OFFS);
	mvpp2_write(sc, MVPP22_AXI_RD_SNOOP_CODE_REG, reg);

	reg = (MVPP22_AXI_CODE_CACHE_WR_CACHE << MVPP22_AXI_CODE_CACHE_OFFS) |
	    (MVPP22_AXI_CODE_DOMAIN_OUTER_DOM << MVPP22_AXI_CODE_DOMAIN_OFFS);
	mvpp2_write(sc, MVPP22_AXI_WR_SNOOP_CODE_REG, reg);
}

void
mvpp2_bm_pool_init(struct mvpp2_softc *sc)
{
	struct mvpp2_bm_pool *bm;
	struct mvpp2_buf *rxb;
	uint64_t phys, virt;
	int i, j, inuse;

	for (i = 0; i < MVPP2_BM_POOLS_NUM; i++) {
		mvpp2_write(sc, MVPP2_BM_INTR_MASK_REG(i), 0);
		mvpp2_write(sc, MVPP2_BM_INTR_CAUSE_REG(i), 0);
	}

	sc->sc_npools = ncpus;
	sc->sc_npools = min(sc->sc_npools, MVPP2_BM_POOLS_NUM);

	sc->sc_bm_pools = mallocarray(sc->sc_npools, sizeof(*sc->sc_bm_pools),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	for (i = 0; i < sc->sc_npools; i++) {
		bm = &sc->sc_bm_pools[i];
		bm->bm_mem = mvpp2_dmamem_alloc(sc,
		    MVPP2_BM_SIZE * sizeof(uint64_t) * 2,
		    MVPP2_BM_POOL_PTR_ALIGN);
		KASSERT(bm->bm_mem != NULL);
		bus_dmamap_sync(sc->sc_dmat, MVPP2_DMA_MAP(bm->bm_mem), 0,
		    MVPP2_DMA_LEN(bm->bm_mem),
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		mvpp2_write(sc, MVPP2_BM_POOL_CTRL_REG(i),
		    mvpp2_read(sc, MVPP2_BM_POOL_CTRL_REG(i)) |
		    MVPP2_BM_STOP_MASK);

		mvpp2_write(sc, MVPP2_BM_POOL_BASE_REG(i),
		    (uint64_t)MVPP2_DMA_DVA(bm->bm_mem) & 0xffffffff);
		mvpp2_write(sc, MVPP22_BM_POOL_BASE_HIGH_REG,
		    ((uint64_t)MVPP2_DMA_DVA(bm->bm_mem) >> 32)
		    & MVPP22_BM_POOL_BASE_HIGH_MASK);
		mvpp2_write(sc, MVPP2_BM_POOL_SIZE_REG(i),
		    MVPP2_BM_SIZE);

		mvpp2_write(sc, MVPP2_BM_POOL_CTRL_REG(i),
		    mvpp2_read(sc, MVPP2_BM_POOL_CTRL_REG(i)) |
		    MVPP2_BM_START_MASK);

		/*
		 * U-Boot might not have cleaned its pools.  The pool needs
		 * to be empty before we fill it, otherwise our packets are
		 * written to wherever U-Boot allocated memory.  Cleaning it
		 * up ourselves is worrying as well, since the BM's pages are
		 * probably in our own memory.  Best we can do is stop the BM,
		 * set new memory and drain the pool.
		 */
		inuse = mvpp2_read(sc, MVPP2_BM_POOL_PTRS_NUM_REG(i)) &
		    MVPP2_BM_POOL_PTRS_NUM_MASK;
		inuse += mvpp2_read(sc, MVPP2_BM_BPPI_PTRS_NUM_REG(i)) &
		    MVPP2_BM_BPPI_PTRS_NUM_MASK;
		if (inuse)
			inuse++;
		for (j = 0; j < inuse; j++)
			mvpp2_read(sc, MVPP2_BM_PHY_ALLOC_REG(i));

		mvpp2_write(sc, MVPP2_POOL_BUF_SIZE_REG(i),
		    roundup(MCLBYTES, 1 << MVPP2_POOL_BUF_SIZE_OFFSET));

		bm->rxbuf = mallocarray(MVPP2_BM_SIZE, sizeof(struct mvpp2_buf),
		    M_DEVBUF, M_WAITOK);
		bm->freelist = mallocarray(MVPP2_BM_SIZE, sizeof(*bm->freelist),
		    M_DEVBUF, M_WAITOK | M_ZERO);

		for (j = 0; j < MVPP2_BM_SIZE; j++) {
			rxb = &bm->rxbuf[j];
			bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
			    MCLBYTES, 0, BUS_DMA_WAITOK, &rxb->mb_map);
			rxb->mb_m = NULL;
		}

		/* Use pool-id and rxbuf index as cookie. */
		for (j = 0; j < MVPP2_BM_SIZE; j++)
			bm->freelist[j] = (i << 16) | (j << 0);

		for (j = 0; j < MVPP2_BM_SIZE; j++) {
			rxb = &bm->rxbuf[j];
			rxb->mb_m = mvpp2_alloc_mbuf(sc, rxb->mb_map);
			if (rxb->mb_m == NULL)
				break;

			KASSERT(bm->freelist[bm->free_cons] != -1);
			virt = bm->freelist[bm->free_cons];
			bm->freelist[bm->free_cons] = -1;
			bm->free_cons = (bm->free_cons + 1) % MVPP2_BM_SIZE;

			phys = rxb->mb_map->dm_segs[0].ds_addr;
			mvpp2_write(sc, MVPP22_BM_ADDR_HIGH_RLS_REG,
			    (((virt >> 32) & MVPP22_ADDR_HIGH_MASK)
			    << MVPP22_BM_ADDR_HIGH_VIRT_RLS_SHIFT) |
			    ((phys >> 32) & MVPP22_ADDR_HIGH_MASK));
			mvpp2_write(sc, MVPP2_BM_VIRT_RLS_REG,
			    virt & 0xffffffff);
			mvpp2_write(sc, MVPP2_BM_PHY_RLS_REG(i),
			    phys & 0xffffffff);
		}
	}
}

void
mvpp2_rx_fifo_init(struct mvpp2_softc *sc)
{
	int i;

	mvpp2_write(sc, MVPP2_RX_DATA_FIFO_SIZE_REG(0),
	    MVPP2_RX_FIFO_PORT_DATA_SIZE_32KB);
	mvpp2_write(sc, MVPP2_RX_ATTR_FIFO_SIZE_REG(0),
	    MVPP2_RX_FIFO_PORT_ATTR_SIZE_32KB);

	mvpp2_write(sc, MVPP2_RX_DATA_FIFO_SIZE_REG(1),
	    MVPP2_RX_FIFO_PORT_DATA_SIZE_8KB);
	mvpp2_write(sc, MVPP2_RX_ATTR_FIFO_SIZE_REG(1),
	    MVPP2_RX_FIFO_PORT_ATTR_SIZE_8KB);

	for (i = 2; i < MVPP2_MAX_PORTS; i++) {
		mvpp2_write(sc, MVPP2_RX_DATA_FIFO_SIZE_REG(i),
		    MVPP2_RX_FIFO_PORT_DATA_SIZE_4KB);
		mvpp2_write(sc, MVPP2_RX_ATTR_FIFO_SIZE_REG(i),
		    MVPP2_RX_FIFO_PORT_ATTR_SIZE_4KB);
	}

	mvpp2_write(sc, MVPP2_RX_MIN_PKT_SIZE_REG, MVPP2_RX_FIFO_PORT_MIN_PKT);
	mvpp2_write(sc, MVPP2_RX_FIFO_INIT_REG, 0x1);
}

void
mvpp2_tx_fifo_init(struct mvpp2_softc *sc)
{
	int i;

	mvpp2_write(sc, MVPP22_TX_FIFO_SIZE_REG(0),
	    MVPP22_TX_FIFO_DATA_SIZE_10KB);
	mvpp2_write(sc, MVPP22_TX_FIFO_THRESH_REG(0),
	    MVPP2_TX_FIFO_THRESHOLD_10KB);

	for (i = 1; i < MVPP2_MAX_PORTS; i++) {
		mvpp2_write(sc, MVPP22_TX_FIFO_SIZE_REG(i),
		    MVPP22_TX_FIFO_DATA_SIZE_3KB);
		mvpp2_write(sc, MVPP22_TX_FIFO_THRESH_REG(i),
		    MVPP2_TX_FIFO_THRESHOLD_3KB);
	}
}

int
mvpp2_prs_default_init(struct mvpp2_softc *sc)
{
	int i, j, ret;

	mvpp2_write(sc, MVPP2_PRS_TCAM_CTRL_REG, MVPP2_PRS_TCAM_EN_MASK);

	for (i = 0; i < MVPP2_PRS_TCAM_SRAM_SIZE; i++) {
		mvpp2_write(sc, MVPP2_PRS_TCAM_IDX_REG, i);
		for (j = 0; j < MVPP2_PRS_TCAM_WORDS; j++)
			mvpp2_write(sc, MVPP2_PRS_TCAM_DATA_REG(j), 0);

		mvpp2_write(sc, MVPP2_PRS_SRAM_IDX_REG, i);
		for (j = 0; j < MVPP2_PRS_SRAM_WORDS; j++)
			mvpp2_write(sc, MVPP2_PRS_SRAM_DATA_REG(j), 0);
	}

	for (i = 0; i < MVPP2_PRS_TCAM_SRAM_SIZE; i++)
		mvpp2_prs_hw_inv(sc, i);

	for (i = 0; i < MVPP2_MAX_PORTS; i++)
		mvpp2_prs_hw_port_init(sc, i, MVPP2_PRS_LU_MH,
		    MVPP2_PRS_PORT_LU_MAX, 0);

	mvpp2_prs_def_flow_init(sc);
	mvpp2_prs_mh_init(sc);
	mvpp2_prs_mac_init(sc);
	mvpp2_prs_dsa_init(sc);
	ret = mvpp2_prs_etype_init(sc);
	if (ret)
		return ret;
	ret = mvpp2_prs_vlan_init(sc);
	if (ret)
		return ret;
	ret = mvpp2_prs_pppoe_init(sc);
	if (ret)
		return ret;
	ret = mvpp2_prs_ip6_init(sc);
	if (ret)
		return ret;
	ret = mvpp2_prs_ip4_init(sc);
	if (ret)
		return ret;

	return 0;
}

void
mvpp2_prs_hw_inv(struct mvpp2_softc *sc, int index)
{
	mvpp2_write(sc, MVPP2_PRS_TCAM_IDX_REG, index);
	mvpp2_write(sc, MVPP2_PRS_TCAM_DATA_REG(MVPP2_PRS_TCAM_INV_WORD),
	    MVPP2_PRS_TCAM_INV_MASK);
}

void
mvpp2_prs_hw_port_init(struct mvpp2_softc *sc, int port,
    int lu_first, int lu_max, int offset)
{
	uint32_t reg;

	reg = mvpp2_read(sc, MVPP2_PRS_INIT_LOOKUP_REG);
	reg &= ~MVPP2_PRS_PORT_LU_MASK(port);
	reg |=  MVPP2_PRS_PORT_LU_VAL(port, lu_first);
	mvpp2_write(sc, MVPP2_PRS_INIT_LOOKUP_REG, reg);

	reg = mvpp2_read(sc, MVPP2_PRS_MAX_LOOP_REG(port));
	reg &= ~MVPP2_PRS_MAX_LOOP_MASK(port);
	reg |= MVPP2_PRS_MAX_LOOP_VAL(port, lu_max);
	mvpp2_write(sc, MVPP2_PRS_MAX_LOOP_REG(port), reg);

	reg = mvpp2_read(sc, MVPP2_PRS_INIT_OFFS_REG(port));
	reg &= ~MVPP2_PRS_INIT_OFF_MASK(port);
	reg |= MVPP2_PRS_INIT_OFF_VAL(port, offset);
	mvpp2_write(sc, MVPP2_PRS_INIT_OFFS_REG(port), reg);
}

void
mvpp2_prs_def_flow_init(struct mvpp2_softc *sc)
{
	struct mvpp2_prs_entry pe;
	int i;

	for (i = 0; i < MVPP2_MAX_PORTS; i++) {
		memset(&pe, 0, sizeof(pe));
		mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_FLOWS);
		pe.index = MVPP2_PE_FIRST_DEFAULT_FLOW - i;
		mvpp2_prs_tcam_port_map_set(&pe, 0);
		mvpp2_prs_sram_ai_update(&pe, i, MVPP2_PRS_FLOW_ID_MASK);
		mvpp2_prs_sram_bits_set(&pe, MVPP2_PRS_SRAM_LU_DONE_BIT, 1);
		mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_FLOWS);
		mvpp2_prs_hw_write(sc, &pe);
	}
}

void
mvpp2_prs_mh_init(struct mvpp2_softc *sc)
{
	struct mvpp2_prs_entry pe;

	memset(&pe, 0, sizeof(pe));
	pe.index = MVPP2_PE_MH_DEFAULT;
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_MH);
	mvpp2_prs_sram_shift_set(&pe, MVPP2_MH_SIZE,
	    MVPP2_PRS_SRAM_OP_SEL_SHIFT_ADD);
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_MAC);
	mvpp2_prs_tcam_port_map_set(&pe, MVPP2_PRS_PORT_MASK);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_MH);
	mvpp2_prs_hw_write(sc, &pe);
}

void
mvpp2_prs_mac_init(struct mvpp2_softc *sc)
{
	struct mvpp2_prs_entry pe;

	memset(&pe, 0, sizeof(pe));
	pe.index = MVPP2_PE_MAC_NON_PROMISCUOUS;
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_MAC);
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_DROP_MASK,
	    MVPP2_PRS_RI_DROP_MASK);
	mvpp2_prs_sram_bits_set(&pe, MVPP2_PRS_SRAM_LU_GEN_BIT, 1);
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_FLOWS);
	mvpp2_prs_tcam_port_map_set(&pe, MVPP2_PRS_PORT_MASK);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_MAC);
	mvpp2_prs_hw_write(sc, &pe);
	mvpp2_prs_mac_drop_all_set(sc, 0, 0);
	mvpp2_prs_mac_promisc_set(sc, 0, MVPP2_PRS_L2_UNI_CAST, 0);
	mvpp2_prs_mac_promisc_set(sc, 0, MVPP2_PRS_L2_MULTI_CAST, 0);
}

void
mvpp2_prs_dsa_init(struct mvpp2_softc *sc)
{
	struct mvpp2_prs_entry pe;

	mvpp2_prs_dsa_tag_set(sc, 0, 0, MVPP2_PRS_UNTAGGED, MVPP2_PRS_EDSA);
	mvpp2_prs_dsa_tag_set(sc, 0, 0, MVPP2_PRS_TAGGED, MVPP2_PRS_EDSA);
	mvpp2_prs_dsa_tag_set(sc, 0, 0, MVPP2_PRS_UNTAGGED, MVPP2_PRS_DSA);
	mvpp2_prs_dsa_tag_set(sc, 0, 0, MVPP2_PRS_TAGGED, MVPP2_PRS_DSA);
	mvpp2_prs_dsa_tag_ethertype_set(sc, 0, 0, MVPP2_PRS_UNTAGGED, MVPP2_PRS_EDSA);
	mvpp2_prs_dsa_tag_ethertype_set(sc, 0, 0, MVPP2_PRS_TAGGED, MVPP2_PRS_EDSA);
	mvpp2_prs_dsa_tag_ethertype_set(sc, 0, 1, MVPP2_PRS_UNTAGGED, MVPP2_PRS_DSA);
	mvpp2_prs_dsa_tag_ethertype_set(sc, 0, 1, MVPP2_PRS_TAGGED, MVPP2_PRS_DSA);
	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_DSA);
	pe.index = MVPP2_PE_DSA_DEFAULT;
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_VLAN);
	mvpp2_prs_sram_shift_set(&pe, 0, MVPP2_PRS_SRAM_OP_SEL_SHIFT_ADD);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_MAC);
	mvpp2_prs_sram_ai_update(&pe, 0, MVPP2_PRS_SRAM_AI_MASK);
	mvpp2_prs_tcam_port_map_set(&pe, MVPP2_PRS_PORT_MASK);
	mvpp2_prs_hw_write(sc, &pe);
}

int
mvpp2_prs_etype_init(struct mvpp2_softc *sc)
{
	struct mvpp2_prs_entry pe;
	int tid;

	/* Ethertype: PPPoE */
	tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_FIRST_FREE_TID,
	    MVPP2_PE_LAST_FREE_TID);
	if (tid < 0)
		return tid;
	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_L2);
	pe.index = tid;
	mvpp2_prs_match_etype(&pe, 0, ETHERTYPE_PPPOE);
	mvpp2_prs_sram_shift_set(&pe, MVPP2_PPPOE_HDR_SIZE,
	    MVPP2_PRS_SRAM_OP_SEL_SHIFT_ADD);
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_PPPOE);
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_PPPOE_MASK,
	    MVPP2_PRS_RI_PPPOE_MASK);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_L2);
	sc->sc_prs_shadow[pe.index].udf = MVPP2_PRS_UDF_L2_DEF;
	sc->sc_prs_shadow[pe.index].finish = 0;
	mvpp2_prs_shadow_ri_set(sc, pe.index, MVPP2_PRS_RI_PPPOE_MASK,
	    MVPP2_PRS_RI_PPPOE_MASK);
	mvpp2_prs_hw_write(sc, &pe);

	/* Ethertype: ARP */
	tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_FIRST_FREE_TID,
	    MVPP2_PE_LAST_FREE_TID);
	if (tid < 0)
		return tid;
	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_L2);
	pe.index = tid;
	mvpp2_prs_match_etype(&pe, 0, ETHERTYPE_ARP);
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_FLOWS);
	mvpp2_prs_sram_bits_set(&pe, MVPP2_PRS_SRAM_LU_GEN_BIT, 1);
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_L3_ARP,
	    MVPP2_PRS_RI_L3_PROTO_MASK);
	mvpp2_prs_sram_offset_set(&pe, MVPP2_PRS_SRAM_UDF_TYPE_L3,
	    MVPP2_ETH_TYPE_LEN, MVPP2_PRS_SRAM_OP_SEL_UDF_ADD);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_L2);
	sc->sc_prs_shadow[pe.index].udf = MVPP2_PRS_UDF_L2_DEF;
	sc->sc_prs_shadow[pe.index].finish = 1;
	mvpp2_prs_shadow_ri_set(sc, pe.index, MVPP2_PRS_RI_L3_ARP,
	    MVPP2_PRS_RI_L3_PROTO_MASK);
	mvpp2_prs_hw_write(sc, &pe);

	/* Ethertype: LBTD */
	tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_FIRST_FREE_TID,
	    MVPP2_PE_LAST_FREE_TID);
	if (tid < 0)
		return tid;
	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_L2);
	pe.index = tid;
	mvpp2_prs_match_etype(&pe, 0, MVPP2_IP_LBDT_TYPE);
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_FLOWS);
	mvpp2_prs_sram_bits_set(&pe, MVPP2_PRS_SRAM_LU_GEN_BIT, 1);
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_CPU_CODE_RX_SPEC |
	    MVPP2_PRS_RI_UDF3_RX_SPECIAL, MVPP2_PRS_RI_CPU_CODE_MASK |
	    MVPP2_PRS_RI_UDF3_MASK);
	mvpp2_prs_sram_offset_set(&pe, MVPP2_PRS_SRAM_UDF_TYPE_L3,
	    MVPP2_ETH_TYPE_LEN, MVPP2_PRS_SRAM_OP_SEL_UDF_ADD);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_L2);
	sc->sc_prs_shadow[pe.index].udf = MVPP2_PRS_UDF_L2_DEF;
	sc->sc_prs_shadow[pe.index].finish = 1;
	mvpp2_prs_shadow_ri_set(sc, pe.index, MVPP2_PRS_RI_CPU_CODE_RX_SPEC |
	    MVPP2_PRS_RI_UDF3_RX_SPECIAL, MVPP2_PRS_RI_CPU_CODE_MASK |
	    MVPP2_PRS_RI_UDF3_MASK);
	mvpp2_prs_hw_write(sc, &pe);

	/* Ethertype: IPv4 without options */
	tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_FIRST_FREE_TID,
	    MVPP2_PE_LAST_FREE_TID);
	if (tid < 0)
		return tid;
	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_L2);
	pe.index = tid;
	mvpp2_prs_match_etype(&pe, 0, ETHERTYPE_IP);
	mvpp2_prs_tcam_data_byte_set(&pe, MVPP2_ETH_TYPE_LEN,
	    MVPP2_PRS_IPV4_HEAD | MVPP2_PRS_IPV4_IHL,
	    MVPP2_PRS_IPV4_HEAD_MASK | MVPP2_PRS_IPV4_IHL_MASK);
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_IP4);
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_L3_IP4,
	    MVPP2_PRS_RI_L3_PROTO_MASK);
	mvpp2_prs_sram_shift_set(&pe, MVPP2_ETH_TYPE_LEN + 4,
	    MVPP2_PRS_SRAM_OP_SEL_SHIFT_ADD);
	mvpp2_prs_sram_offset_set(&pe, MVPP2_PRS_SRAM_UDF_TYPE_L3,
	    MVPP2_ETH_TYPE_LEN, MVPP2_PRS_SRAM_OP_SEL_UDF_ADD);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_L2);
	sc->sc_prs_shadow[pe.index].udf = MVPP2_PRS_UDF_L2_DEF;
	sc->sc_prs_shadow[pe.index].finish = 0;
	mvpp2_prs_shadow_ri_set(sc, pe.index, MVPP2_PRS_RI_L3_IP4,
	    MVPP2_PRS_RI_L3_PROTO_MASK);
	mvpp2_prs_hw_write(sc, &pe);

	/* Ethertype: IPv4 with options */
	tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_FIRST_FREE_TID,
	    MVPP2_PE_LAST_FREE_TID);
	if (tid < 0)
		return tid;
	pe.index = tid;

	pe.tcam.byte[MVPP2_PRS_TCAM_DATA_BYTE(MVPP2_ETH_TYPE_LEN)] = 0x0;
	pe.tcam.byte[MVPP2_PRS_TCAM_DATA_BYTE_EN(MVPP2_ETH_TYPE_LEN)] = 0x0;
	mvpp2_prs_tcam_data_byte_set(&pe, MVPP2_ETH_TYPE_LEN,
	    MVPP2_PRS_IPV4_HEAD, MVPP2_PRS_IPV4_HEAD_MASK);
	pe.sram.word[MVPP2_PRS_SRAM_RI_WORD] = 0x0;
	pe.sram.word[MVPP2_PRS_SRAM_RI_CTRL_WORD] = 0x0;
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_L3_IP4_OPT,
	    MVPP2_PRS_RI_L3_PROTO_MASK);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_L2);
	sc->sc_prs_shadow[pe.index].udf = MVPP2_PRS_UDF_L2_DEF;
	sc->sc_prs_shadow[pe.index].finish = 0;
	mvpp2_prs_shadow_ri_set(sc, pe.index, MVPP2_PRS_RI_L3_IP4_OPT,
	    MVPP2_PRS_RI_L3_PROTO_MASK);
	mvpp2_prs_hw_write(sc, &pe);

	/* Ethertype: IPv6 without options */
	tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_FIRST_FREE_TID,
	    MVPP2_PE_LAST_FREE_TID);
	if (tid < 0)
		return tid;
	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_L2);
	pe.index = tid;
	mvpp2_prs_match_etype(&pe, 0, ETHERTYPE_IPV6);
	mvpp2_prs_sram_shift_set(&pe, MVPP2_ETH_TYPE_LEN + 8 +
	    MVPP2_MAX_L3_ADDR_SIZE, MVPP2_PRS_SRAM_OP_SEL_SHIFT_ADD);
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_IP6);
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_L3_IP6,
	    MVPP2_PRS_RI_L3_PROTO_MASK);
	mvpp2_prs_sram_offset_set(&pe, MVPP2_PRS_SRAM_UDF_TYPE_L3,
	    MVPP2_ETH_TYPE_LEN, MVPP2_PRS_SRAM_OP_SEL_UDF_ADD);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_L2);
	sc->sc_prs_shadow[pe.index].udf = MVPP2_PRS_UDF_L2_DEF;
	sc->sc_prs_shadow[pe.index].finish = 0;
	mvpp2_prs_shadow_ri_set(sc, pe.index, MVPP2_PRS_RI_L3_IP6,
	    MVPP2_PRS_RI_L3_PROTO_MASK);
	mvpp2_prs_hw_write(sc, &pe);

	/* Default entry for MVPP2_PRS_LU_L2 - Unknown ethtype */
	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_L2);
	pe.index = MVPP2_PE_ETH_TYPE_UN;
	mvpp2_prs_tcam_port_map_set(&pe, MVPP2_PRS_PORT_MASK);
	mvpp2_prs_sram_bits_set(&pe, MVPP2_PRS_SRAM_LU_GEN_BIT, 1);
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_FLOWS);
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_L3_UN,
	    MVPP2_PRS_RI_L3_PROTO_MASK);
	mvpp2_prs_sram_offset_set(&pe, MVPP2_PRS_SRAM_UDF_TYPE_L3,
	    MVPP2_ETH_TYPE_LEN, MVPP2_PRS_SRAM_OP_SEL_UDF_ADD);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_L2);
	sc->sc_prs_shadow[pe.index].udf = MVPP2_PRS_UDF_L2_DEF;
	sc->sc_prs_shadow[pe.index].finish = 1;
	mvpp2_prs_shadow_ri_set(sc, pe.index, MVPP2_PRS_RI_L3_UN,
	    MVPP2_PRS_RI_L3_PROTO_MASK);
	mvpp2_prs_hw_write(sc, &pe);

	return 0;
}

int
mvpp2_prs_vlan_init(struct mvpp2_softc *sc)
{
	struct mvpp2_prs_entry pe;
	int ret;

	sc->sc_prs_double_vlans = mallocarray(MVPP2_PRS_DBL_VLANS_MAX,
	    sizeof(*sc->sc_prs_double_vlans), M_DEVBUF, M_WAITOK | M_ZERO);

	ret = mvpp2_prs_double_vlan_add(sc, ETHERTYPE_VLAN, ETHERTYPE_QINQ,
	    MVPP2_PRS_PORT_MASK);
	if (ret)
		return ret;
	ret = mvpp2_prs_double_vlan_add(sc, ETHERTYPE_VLAN, ETHERTYPE_VLAN,
	    MVPP2_PRS_PORT_MASK);
	if (ret)
		return ret;
	ret = mvpp2_prs_vlan_add(sc, ETHERTYPE_QINQ, MVPP2_PRS_SINGLE_VLAN_AI,
	    MVPP2_PRS_PORT_MASK);
	if (ret)
		return ret;
	ret = mvpp2_prs_vlan_add(sc, ETHERTYPE_VLAN, MVPP2_PRS_SINGLE_VLAN_AI,
	    MVPP2_PRS_PORT_MASK);
	if (ret)
		return ret;

	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_VLAN);
	pe.index = MVPP2_PE_VLAN_DBL;
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_L2);
	mvpp2_prs_sram_ai_update(&pe, 0, MVPP2_PRS_SRAM_AI_MASK);
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_VLAN_DOUBLE,
	    MVPP2_PRS_RI_VLAN_MASK);
	mvpp2_prs_tcam_ai_update(&pe, MVPP2_PRS_DBL_VLAN_AI_BIT,
	    MVPP2_PRS_DBL_VLAN_AI_BIT);
	mvpp2_prs_tcam_port_map_set(&pe, MVPP2_PRS_PORT_MASK);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_VLAN);
	mvpp2_prs_hw_write(sc, &pe);

	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_VLAN);
	pe.index = MVPP2_PE_VLAN_NONE;
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_L2);
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_VLAN_NONE,
	    MVPP2_PRS_RI_VLAN_MASK);
	mvpp2_prs_tcam_port_map_set(&pe, MVPP2_PRS_PORT_MASK);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_VLAN);
	mvpp2_prs_hw_write(sc, &pe);

	return 0;
}

int
mvpp2_prs_pppoe_init(struct mvpp2_softc *sc)
{
	struct mvpp2_prs_entry pe;
	int tid;

	tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_FIRST_FREE_TID,
	    MVPP2_PE_LAST_FREE_TID);
	if (tid < 0)
		return tid;

	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_PPPOE);
	pe.index = tid;
	mvpp2_prs_match_etype(&pe, 0, PPP_IP);
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_IP4);
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_L3_IP4_OPT,
	    MVPP2_PRS_RI_L3_PROTO_MASK);
	mvpp2_prs_sram_shift_set(&pe, MVPP2_ETH_TYPE_LEN + 4,
	    MVPP2_PRS_SRAM_OP_SEL_SHIFT_ADD);
	mvpp2_prs_sram_offset_set(&pe, MVPP2_PRS_SRAM_UDF_TYPE_L3,
	    MVPP2_ETH_TYPE_LEN, MVPP2_PRS_SRAM_OP_SEL_UDF_ADD);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_PPPOE);
	mvpp2_prs_hw_write(sc, &pe);

	tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_FIRST_FREE_TID,
	    MVPP2_PE_LAST_FREE_TID);
	if (tid < 0)
		return tid;

	pe.index = tid;
	mvpp2_prs_tcam_data_byte_set(&pe, MVPP2_ETH_TYPE_LEN,
	    MVPP2_PRS_IPV4_HEAD | MVPP2_PRS_IPV4_IHL,
	    MVPP2_PRS_IPV4_HEAD_MASK | MVPP2_PRS_IPV4_IHL_MASK);
	pe.sram.word[MVPP2_PRS_SRAM_RI_WORD] = 0x0;
	pe.sram.word[MVPP2_PRS_SRAM_RI_CTRL_WORD] = 0x0;
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_L3_IP4, MVPP2_PRS_RI_L3_PROTO_MASK);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_PPPOE);
	mvpp2_prs_hw_write(sc, &pe);

	tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_FIRST_FREE_TID,
	    MVPP2_PE_LAST_FREE_TID);
	if (tid < 0)
		return tid;

	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_PPPOE);
	pe.index = tid;
	mvpp2_prs_match_etype(&pe, 0, PPP_IPV6);
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_IP6);
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_L3_IP6,
	    MVPP2_PRS_RI_L3_PROTO_MASK);
	mvpp2_prs_sram_shift_set(&pe, MVPP2_ETH_TYPE_LEN + 4,
	    MVPP2_PRS_SRAM_OP_SEL_SHIFT_ADD);
	mvpp2_prs_sram_offset_set(&pe, MVPP2_PRS_SRAM_UDF_TYPE_L3,
	    MVPP2_ETH_TYPE_LEN, MVPP2_PRS_SRAM_OP_SEL_UDF_ADD);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_PPPOE);
	mvpp2_prs_hw_write(sc, &pe);

	tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_FIRST_FREE_TID,
	    MVPP2_PE_LAST_FREE_TID);
	if (tid < 0)
		return tid;

	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_PPPOE);
	pe.index = tid;
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_L3_UN,
	    MVPP2_PRS_RI_L3_PROTO_MASK);
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_FLOWS);
	mvpp2_prs_sram_bits_set(&pe, MVPP2_PRS_SRAM_LU_GEN_BIT, 1);
	mvpp2_prs_sram_offset_set(&pe, MVPP2_PRS_SRAM_UDF_TYPE_L3,
	    MVPP2_ETH_TYPE_LEN, MVPP2_PRS_SRAM_OP_SEL_UDF_ADD);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_PPPOE);
	mvpp2_prs_hw_write(sc, &pe);

	return 0;
}

int
mvpp2_prs_ip6_init(struct mvpp2_softc *sc)
{
	struct mvpp2_prs_entry pe;
	int tid, ret;

	ret = mvpp2_prs_ip6_proto(sc, IPPROTO_TCP, MVPP2_PRS_RI_L4_TCP,
	    MVPP2_PRS_RI_L4_PROTO_MASK);
	if (ret)
		return ret;
	ret = mvpp2_prs_ip6_proto(sc, IPPROTO_UDP, MVPP2_PRS_RI_L4_UDP,
	    MVPP2_PRS_RI_L4_PROTO_MASK);
	if (ret)
		return ret;
	ret = mvpp2_prs_ip6_proto(sc, IPPROTO_ICMPV6,
	    MVPP2_PRS_RI_CPU_CODE_RX_SPEC | MVPP2_PRS_RI_UDF3_RX_SPECIAL,
	    MVPP2_PRS_RI_CPU_CODE_MASK | MVPP2_PRS_RI_UDF3_MASK);
	if (ret)
		return ret;
	ret = mvpp2_prs_ip6_proto(sc, IPPROTO_IPIP, MVPP2_PRS_RI_UDF7_IP6_LITE,
	    MVPP2_PRS_RI_UDF7_MASK);
	if (ret)
		return ret;
	ret = mvpp2_prs_ip6_cast(sc, MVPP2_PRS_L3_MULTI_CAST);
	if (ret)
		return ret;

	tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_FIRST_FREE_TID,
	    MVPP2_PE_LAST_FREE_TID);
	if (tid < 0)
		return tid;

	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_IP6);
	pe.index = tid;
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_FLOWS);
	mvpp2_prs_sram_bits_set(&pe, MVPP2_PRS_SRAM_LU_GEN_BIT, 1);
	mvpp2_prs_sram_ri_update(&pe,
	    MVPP2_PRS_RI_L3_UN | MVPP2_PRS_RI_DROP_MASK,
	    MVPP2_PRS_RI_L3_PROTO_MASK | MVPP2_PRS_RI_DROP_MASK);
	mvpp2_prs_tcam_data_byte_set(&pe, 1, 0x00, MVPP2_PRS_IPV6_HOP_MASK);
	mvpp2_prs_tcam_ai_update(&pe, MVPP2_PRS_IPV6_NO_EXT_AI_BIT,
	    MVPP2_PRS_IPV6_NO_EXT_AI_BIT);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_IP4);
	mvpp2_prs_hw_write(sc, &pe);

	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_IP6);
	pe.index = MVPP2_PE_IP6_PROTO_UN;
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_FLOWS);
	mvpp2_prs_sram_bits_set(&pe, MVPP2_PRS_SRAM_LU_GEN_BIT, 1);
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_L4_OTHER,
	    MVPP2_PRS_RI_L4_PROTO_MASK);
	mvpp2_prs_sram_offset_set(&pe, MVPP2_PRS_SRAM_UDF_TYPE_L4,
	    sizeof(struct ip6_hdr) - 6, MVPP2_PRS_SRAM_OP_SEL_UDF_ADD);
	mvpp2_prs_tcam_ai_update(&pe, MVPP2_PRS_IPV6_NO_EXT_AI_BIT,
	    MVPP2_PRS_IPV6_NO_EXT_AI_BIT);
	mvpp2_prs_tcam_port_map_set(&pe, MVPP2_PRS_PORT_MASK);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_IP4);
	mvpp2_prs_hw_write(sc, &pe);

	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_IP6);
	pe.index = MVPP2_PE_IP6_EXT_PROTO_UN;
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_FLOWS);
	mvpp2_prs_sram_bits_set(&pe, MVPP2_PRS_SRAM_LU_GEN_BIT, 1);
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_L4_OTHER,
	    MVPP2_PRS_RI_L4_PROTO_MASK);
	mvpp2_prs_tcam_ai_update(&pe, MVPP2_PRS_IPV6_EXT_AI_BIT,
	    MVPP2_PRS_IPV6_EXT_AI_BIT);
	mvpp2_prs_tcam_port_map_set(&pe, MVPP2_PRS_PORT_MASK);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_IP4);
	mvpp2_prs_hw_write(sc, &pe);

	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_IP6);
	pe.index = MVPP2_PE_IP6_ADDR_UN;
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_IP6);
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_L3_UCAST,
	    MVPP2_PRS_RI_L3_ADDR_MASK);
	mvpp2_prs_sram_ai_update(&pe, MVPP2_PRS_IPV6_NO_EXT_AI_BIT,
	    MVPP2_PRS_IPV6_NO_EXT_AI_BIT);
	mvpp2_prs_sram_shift_set(&pe, -18, MVPP2_PRS_SRAM_OP_SEL_SHIFT_ADD);
	mvpp2_prs_tcam_ai_update(&pe, 0, MVPP2_PRS_IPV6_NO_EXT_AI_BIT);
	mvpp2_prs_tcam_port_map_set(&pe, MVPP2_PRS_PORT_MASK);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_IP6);
	mvpp2_prs_hw_write(sc, &pe);

	return 0;
}

int
mvpp2_prs_ip4_init(struct mvpp2_softc *sc)
{
	struct mvpp2_prs_entry pe;
	int ret;

	ret = mvpp2_prs_ip4_proto(sc, IPPROTO_TCP, MVPP2_PRS_RI_L4_TCP,
	    MVPP2_PRS_RI_L4_PROTO_MASK);
	if (ret)
		return ret;
	ret = mvpp2_prs_ip4_proto(sc, IPPROTO_UDP, MVPP2_PRS_RI_L4_UDP,
	    MVPP2_PRS_RI_L4_PROTO_MASK);
	if (ret)
		return ret;
	ret = mvpp2_prs_ip4_proto(sc, IPPROTO_IGMP,
	    MVPP2_PRS_RI_CPU_CODE_RX_SPEC | MVPP2_PRS_RI_UDF3_RX_SPECIAL,
	    MVPP2_PRS_RI_CPU_CODE_MASK | MVPP2_PRS_RI_UDF3_MASK);
	if (ret)
		return ret;
	ret = mvpp2_prs_ip4_cast(sc, MVPP2_PRS_L3_BROAD_CAST);
	if (ret)
		return ret;
	ret = mvpp2_prs_ip4_cast(sc, MVPP2_PRS_L3_MULTI_CAST);
	if (ret)
		return ret;

	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_IP4);
	pe.index = MVPP2_PE_IP4_PROTO_UN;
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_IP4);
	mvpp2_prs_sram_shift_set(&pe, 12, MVPP2_PRS_SRAM_OP_SEL_SHIFT_ADD);
	mvpp2_prs_sram_offset_set(&pe, MVPP2_PRS_SRAM_UDF_TYPE_L4,
	    sizeof(struct ip) - 4, MVPP2_PRS_SRAM_OP_SEL_UDF_ADD);
	mvpp2_prs_sram_ai_update(&pe, MVPP2_PRS_IPV4_DIP_AI_BIT,
	    MVPP2_PRS_IPV4_DIP_AI_BIT);
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_L4_OTHER,
	    MVPP2_PRS_RI_L4_PROTO_MASK);
	mvpp2_prs_tcam_ai_update(&pe, 0, MVPP2_PRS_IPV4_DIP_AI_BIT);
	mvpp2_prs_tcam_port_map_set(&pe, MVPP2_PRS_PORT_MASK);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_IP4);
	mvpp2_prs_hw_write(sc, &pe);

	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_IP4);
	pe.index = MVPP2_PE_IP4_ADDR_UN;
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_FLOWS);
	mvpp2_prs_sram_bits_set(&pe, MVPP2_PRS_SRAM_LU_GEN_BIT, 1);
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_L3_UCAST,
	    MVPP2_PRS_RI_L3_ADDR_MASK);
	mvpp2_prs_tcam_ai_update(&pe, MVPP2_PRS_IPV4_DIP_AI_BIT,
	    MVPP2_PRS_IPV4_DIP_AI_BIT);
	mvpp2_prs_tcam_port_map_set(&pe, MVPP2_PRS_PORT_MASK);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_IP4);
	mvpp2_prs_hw_write(sc, &pe);

	return 0;
}

int
mvpp2_port_match(struct device *parent, void *cfdata, void *aux)
{
	struct mvpp2_attach_args *maa = aux;
	char buf[32];

	if (OF_getprop(maa->ma_node, "status", buf, sizeof(buf)) > 0 &&
	    strcmp(buf, "disabled") == 0)
		return 0;

	return 1;
}

void
mvpp2_port_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvpp2_port *sc = (void *)self;
	struct mvpp2_attach_args *maa = aux;
	struct mvpp2_tx_queue *txq;
	struct mvpp2_rx_queue *rxq;
	struct ifnet *ifp;
	uint32_t phy, reg;
	int i, idx, len, node;
	int mii_flags = 0;
	char *phy_mode;
	char *managed;

	sc->sc = (void *)parent;
	sc->sc_node = maa->ma_node;
	sc->sc_dmat = maa->ma_dmat;

	sc->sc_id = OF_getpropint(sc->sc_node, "port-id", 0);
	sc->sc_gop_id = OF_getpropint(sc->sc_node, "gop-port-id", 0);
	sc->sc_sfp = OF_getpropint(sc->sc_node, "sfp", 0);

	len = OF_getproplen(sc->sc_node, "phy-mode");
	if (len <= 0) {
		printf("%s: cannot extract phy-mode\n", self->dv_xname);
		return;
	}

	phy_mode = malloc(len, M_TEMP, M_WAITOK);
	OF_getprop(sc->sc_node, "phy-mode", phy_mode, len);
	if (!strncmp(phy_mode, "10gbase-r", strlen("10gbase-r")))
		sc->sc_phy_mode = PHY_MODE_10GBASER;
	else if (!strncmp(phy_mode, "10gbase-kr", strlen("10gbase-kr")))
		sc->sc_phy_mode = PHY_MODE_10GBASER;
	else if (!strncmp(phy_mode, "2500base-x", strlen("2500base-x")))
		sc->sc_phy_mode = PHY_MODE_2500BASEX;
	else if (!strncmp(phy_mode, "1000base-x", strlen("1000base-x")))
		sc->sc_phy_mode = PHY_MODE_1000BASEX;
	else if (!strncmp(phy_mode, "sgmii", strlen("sgmii")))
		sc->sc_phy_mode = PHY_MODE_SGMII;
	else if (!strncmp(phy_mode, "rgmii-rxid", strlen("rgmii-rxid")))
		sc->sc_phy_mode = PHY_MODE_RGMII_RXID;
	else if (!strncmp(phy_mode, "rgmii-txid", strlen("rgmii-txid")))
		sc->sc_phy_mode = PHY_MODE_RGMII_TXID;
	else if (!strncmp(phy_mode, "rgmii-id", strlen("rgmii-id")))
		sc->sc_phy_mode = PHY_MODE_RGMII_ID;
	else if (!strncmp(phy_mode, "rgmii", strlen("rgmii")))
		sc->sc_phy_mode = PHY_MODE_RGMII;
	else {
		printf("%s: cannot use phy-mode %s\n", self->dv_xname,
		    phy_mode);
		return;
	}
	free(phy_mode, M_TEMP, len);

	/* Lookup PHY. */
	phy = OF_getpropint(sc->sc_node, "phy", 0);
	if (phy) {
		node = OF_getnodebyphandle(phy);
		if (!node) {
			printf(": no phy\n");
			return;
		}
		sc->sc_mdio = mii_byphandle(phy);
		sc->sc_phyloc = OF_getpropint(node, "reg", MII_PHY_ANY);
		sc->sc_sfp = OF_getpropint(node, "sfp", sc->sc_sfp);
		sc->sc_mii.mii_node = node;
	}

	if (sc->sc_sfp)
		config_mountroot(self, mvpp2_port_attach_sfp);

	if ((len = OF_getproplen(sc->sc_node, "managed")) >= 0) {
		managed = malloc(len, M_TEMP, M_WAITOK);
		OF_getprop(sc->sc_node, "managed", managed, len);
		if (!strncmp(managed, "in-band-status",
		    strlen("in-band-status")))
			sc->sc_inband_status = 1;
		free(managed, M_TEMP, len);
	}

	if (OF_getprop(sc->sc_node, "local-mac-address",
	    &sc->sc_lladdr, ETHER_ADDR_LEN) != ETHER_ADDR_LEN)
		memset(sc->sc_lladdr, 0xff, sizeof(sc->sc_lladdr));
	printf(": address %s\n", ether_sprintf(sc->sc_lladdr));

	sc->sc_ntxq = sc->sc_nrxq = 1;
	sc->sc_txqs = mallocarray(sc->sc_ntxq, sizeof(*sc->sc_txqs),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	sc->sc_rxqs = mallocarray(sc->sc_nrxq, sizeof(*sc->sc_rxqs),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	for (i = 0; i < sc->sc_ntxq; i++) {
		txq = &sc->sc_txqs[i];
		txq->id = mvpp2_txq_phys(sc->sc_id, i);
		txq->log_id = i;
		txq->done_pkts_coal = MVPP2_TXDONE_COAL_PKTS_THRESH;
	}

	sc->sc_tx_time_coal = MVPP2_TXDONE_COAL_USEC;

	for (i = 0; i < sc->sc_nrxq; i++) {
		rxq = &sc->sc_rxqs[i];
		rxq->id = sc->sc_id * 32 + i;
		rxq->pkts_coal = MVPP2_RX_COAL_PKTS;
		rxq->time_coal = MVPP2_RX_COAL_USEC;
	}

	mvpp2_egress_disable(sc);
	mvpp2_port_disable(sc);

	mvpp2_write(sc->sc, MVPP2_ISR_RXQ_GROUP_INDEX_REG,
	    sc->sc_id << MVPP2_ISR_RXQ_GROUP_INDEX_GROUP_SHIFT |
	    0 /* queue vector id */);
	mvpp2_write(sc->sc, MVPP2_ISR_RXQ_SUB_GROUP_CONFIG_REG,
	    sc->sc_nrxq << MVPP2_ISR_RXQ_SUB_GROUP_CONFIG_SIZE_SHIFT |
	    0 /* first rxq */);

	mvpp2_ingress_disable(sc);
	mvpp2_defaults_set(sc);

	mvpp2_cls_oversize_rxq_set(sc);
	mvpp2_cls_port_config(sc);

	/*
	 * We have one pool per core, so all RX queues on a specific
	 * core share that pool.  Also long and short uses the same
	 * pool.
	 */
	for (i = 0; i < sc->sc_nrxq; i++) {
		mvpp2_rxq_long_pool_set(sc, i, i);
		mvpp2_rxq_short_pool_set(sc, i, i);
	}

	mvpp2_mac_reset_assert(sc);
	mvpp2_pcs_reset_assert(sc);

	timeout_set(&sc->sc_tick, mvpp2_tick, sc);

	ifp = &sc->sc_ac.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = mvpp2_ioctl;
	ifp->if_start = mvpp2_start;
	ifp->if_watchdog = mvpp2_watchdog;
	ifq_init_maxlen(&ifp->if_snd, MVPP2_NTXDESC - 1);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = mvpp2_mii_readreg;
	sc->sc_mii.mii_writereg = mvpp2_mii_writereg;
	sc->sc_mii.mii_statchg = mvpp2_mii_statchg;

	ifmedia_init(&sc->sc_media, 0, mvpp2_media_change, mvpp2_media_status);

	if (sc->sc_mdio) {
		switch (sc->sc_phy_mode) {
		case PHY_MODE_1000BASEX:
			mii_flags |= MIIF_IS_1000X;
			break;
		case PHY_MODE_SGMII:
			mii_flags |= MIIF_SGMII;
			break;
		case PHY_MODE_RGMII_ID:
			mii_flags |= MIIF_RXID | MIIF_TXID;
			break;
		case PHY_MODE_RGMII_RXID:
			mii_flags |= MIIF_RXID;
			break;
		case PHY_MODE_RGMII_TXID:
			mii_flags |= MIIF_TXID;
			break;
		default:
			break;
		}
		mii_attach(self, &sc->sc_mii, 0xffffffff, sc->sc_phyloc,
		    (sc->sc_phyloc == MII_PHY_ANY) ? 0 : MII_OFFSET_ANY,
		    mii_flags);
		if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
			printf("%s: no PHY found!\n", self->dv_xname);
			ifmedia_add(&sc->sc_mii.mii_media,
			    IFM_ETHER|IFM_MANUAL, 0, NULL);
			ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_MANUAL);
		} else
			ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);
	} else {
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO, 0, NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);

		if (sc->sc_inband_status) {
			switch (sc->sc_phy_mode) {
			case PHY_MODE_1000BASEX:
				sc->sc_mii.mii_media_active =
				    IFM_ETHER|IFM_1000_KX|IFM_FDX;
				break;
			case PHY_MODE_2500BASEX:
				sc->sc_mii.mii_media_active =
				    IFM_ETHER|IFM_2500_KX|IFM_FDX;
				break;
			case PHY_MODE_10GBASER:
				sc->sc_mii.mii_media_active =
				    IFM_ETHER|IFM_10G_KR|IFM_FDX;
				break;
			default:
				break;
			}
			mvpp2_inband_statchg(sc);
		} else {
			sc->sc_mii.mii_media_status = IFM_AVALID|IFM_ACTIVE;
			sc->sc_mii.mii_media_active = IFM_ETHER|IFM_1000_T|IFM_FDX;
			mvpp2_mii_statchg(self);
		}

		ifp->if_baudrate = ifmedia_baudrate(sc->sc_mii.mii_media_active);
		ifp->if_link_state = LINK_STATE_FULL_DUPLEX;
	}

	if_attach(ifp);
	ether_ifattach(ifp);

	if (sc->sc_phy_mode == PHY_MODE_2500BASEX ||
	    sc->sc_phy_mode == PHY_MODE_1000BASEX ||
	    sc->sc_phy_mode == PHY_MODE_SGMII ||
	    sc->sc_phy_mode == PHY_MODE_RGMII ||
	    sc->sc_phy_mode == PHY_MODE_RGMII_ID ||
	    sc->sc_phy_mode == PHY_MODE_RGMII_RXID ||
	    sc->sc_phy_mode == PHY_MODE_RGMII_TXID) {
		reg = mvpp2_gmac_read(sc, MVPP2_GMAC_INT_MASK_REG);
		reg |= MVPP2_GMAC_INT_CAUSE_LINK_CHANGE;
		mvpp2_gmac_write(sc, MVPP2_GMAC_INT_MASK_REG, reg);
	}

	if (sc->sc_gop_id == 0) {
		reg = mvpp2_xlg_read(sc, MV_XLG_INTERRUPT_MASK_REG);
		reg |= MV_XLG_INTERRUPT_LINK_CHANGE;
		mvpp2_xlg_write(sc, MV_XLG_INTERRUPT_MASK_REG, reg);
	}

	mvpp2_gop_intr_unmask(sc);

	idx = OF_getindex(sc->sc_node, "link", "interrupt-names");
	if (idx >= 0)
		fdt_intr_establish_idx(sc->sc_node, idx, IPL_NET,
		    mvpp2_link_intr, sc, sc->sc_dev.dv_xname);
	idx = OF_getindex(sc->sc_node, "hif0", "interrupt-names");
	if (idx < 0)
		idx = OF_getindex(sc->sc_node, "tx-cpu0", "interrupt-names");
	if (idx >= 0)
		fdt_intr_establish_idx(sc->sc_node, idx, IPL_NET,
		    mvpp2_intr, sc, sc->sc_dev.dv_xname);
}

void
mvpp2_port_attach_sfp(struct device *self)
{
	struct mvpp2_port *sc = (struct mvpp2_port *)self;
	uint32_t reg;

	rw_enter(&mvpp2_sff_lock, RW_WRITE);
	sfp_disable(sc->sc_sfp);
	sfp_add_media(sc->sc_sfp, &sc->sc_mii);
	rw_exit(&mvpp2_sff_lock);

	switch (IFM_SUBTYPE(sc->sc_mii.mii_media_active)) {
	case IFM_10G_SR:
	case IFM_10G_LR:
	case IFM_10G_LRM:
	case IFM_10G_ER:
	case IFM_10G_SFP_CU:
		sc->sc_phy_mode = PHY_MODE_10GBASER;
		sc->sc_mii.mii_media_status = IFM_AVALID;
		sc->sc_inband_status = 1;
		break;
	case IFM_2500_SX:
		sc->sc_phy_mode = PHY_MODE_2500BASEX;
		sc->sc_mii.mii_media_status = IFM_AVALID;
		sc->sc_inband_status = 1;
		break;
	case IFM_1000_CX:
	case IFM_1000_LX:
	case IFM_1000_SX:
	case IFM_1000_T:
		sc->sc_phy_mode = PHY_MODE_1000BASEX;
		sc->sc_mii.mii_media_status = IFM_AVALID;
		sc->sc_inband_status = 1;
		break;
	}

	if (sc->sc_inband_status) {
		reg = mvpp2_gmac_read(sc, MVPP2_GMAC_INT_MASK_REG);
		reg |= MVPP2_GMAC_INT_CAUSE_LINK_CHANGE;
		mvpp2_gmac_write(sc, MVPP2_GMAC_INT_MASK_REG, reg);
	}
}

uint32_t
mvpp2_read(struct mvpp2_softc *sc, bus_addr_t addr)
{
	return bus_space_read_4(sc->sc_iot, sc->sc_ioh_base, addr);
}

void
mvpp2_write(struct mvpp2_softc *sc, bus_addr_t addr, uint32_t data)
{
	bus_space_write_4(sc->sc_iot, sc->sc_ioh_base, addr, data);
}

uint32_t
mvpp2_gmac_read(struct mvpp2_port *sc, bus_addr_t addr)
{
	return bus_space_read_4(sc->sc->sc_iot, sc->sc->sc_ioh_iface,
	    MVPP22_GMAC_OFFSET + sc->sc_gop_id * MVPP22_GMAC_REG_SIZE + addr);
}

void
mvpp2_gmac_write(struct mvpp2_port *sc, bus_addr_t addr, uint32_t data)
{
	bus_space_write_4(sc->sc->sc_iot, sc->sc->sc_ioh_iface,
	    MVPP22_GMAC_OFFSET + sc->sc_gop_id * MVPP22_GMAC_REG_SIZE + addr,
	    data);
}

uint32_t
mvpp2_xlg_read(struct mvpp2_port *sc, bus_addr_t addr)
{
	return bus_space_read_4(sc->sc->sc_iot, sc->sc->sc_ioh_iface,
	    MVPP22_XLG_OFFSET + sc->sc_gop_id * MVPP22_XLG_REG_SIZE + addr);
}

void
mvpp2_xlg_write(struct mvpp2_port *sc, bus_addr_t addr, uint32_t data)
{
	bus_space_write_4(sc->sc->sc_iot, sc->sc->sc_ioh_iface,
	    MVPP22_XLG_OFFSET + sc->sc_gop_id * MVPP22_XLG_REG_SIZE + addr,
	    data);
}

uint32_t
mvpp2_mpcs_read(struct mvpp2_port *sc, bus_addr_t addr)
{
	return bus_space_read_4(sc->sc->sc_iot, sc->sc->sc_ioh_iface,
	    MVPP22_MPCS_OFFSET + sc->sc_gop_id * MVPP22_MPCS_REG_SIZE + addr);
}

void
mvpp2_mpcs_write(struct mvpp2_port *sc, bus_addr_t addr, uint32_t data)
{
	bus_space_write_4(sc->sc->sc_iot, sc->sc->sc_ioh_iface,
	    MVPP22_MPCS_OFFSET + sc->sc_gop_id * MVPP22_MPCS_REG_SIZE + addr,
	    data);
}

uint32_t
mvpp2_xpcs_read(struct mvpp2_port *sc, bus_addr_t addr)
{
	return bus_space_read_4(sc->sc->sc_iot, sc->sc->sc_ioh_iface,
	    MVPP22_XPCS_OFFSET + sc->sc_gop_id * MVPP22_XPCS_REG_SIZE + addr);
}

void
mvpp2_xpcs_write(struct mvpp2_port *sc, bus_addr_t addr, uint32_t data)
{
	bus_space_write_4(sc->sc->sc_iot, sc->sc->sc_ioh_iface,
	    MVPP22_XPCS_OFFSET + sc->sc_gop_id * MVPP22_XPCS_REG_SIZE + addr,
	    data);
}

static inline int
mvpp2_load_mbuf(bus_dma_tag_t dmat, bus_dmamap_t map, struct mbuf *m)
{
	int error;

	error = bus_dmamap_load_mbuf(dmat, map, m, BUS_DMA_NOWAIT);
	if (error != EFBIG)
		return (error);

	error = m_defrag(m, M_DONTWAIT);
	if (error != 0)
		return (error);

	return bus_dmamap_load_mbuf(dmat, map, m, BUS_DMA_NOWAIT);
}

void
mvpp2_start(struct ifnet *ifp)
{
	struct mvpp2_port *sc = ifp->if_softc;
	struct mvpp2_tx_queue *txq = &sc->sc->sc_aggr_txqs[0];
	struct mvpp2_tx_desc *txd;
	struct mbuf *m;
	bus_dmamap_t map;
	uint32_t command;
	int i, current, first, last;
	int free, prod, used;

	if (!(ifp->if_flags & IFF_RUNNING))
		return;
	if (ifq_is_oactive(&ifp->if_snd))
		return;
	if (ifq_empty(&ifp->if_snd))
		return;
	if (!sc->sc_link)
		return;

	used = 0;
	prod = txq->prod;
	free = txq->cons;
	if (free <= prod)
		free += MVPP2_AGGR_TXQ_SIZE;
	free -= prod;

	for (;;) {
		if (free <= MVPP2_NTXSEGS) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;

		first = last = current = prod;
		map = txq->buf[current].mb_map;

		if (mvpp2_load_mbuf(sc->sc_dmat, map, m) != 0) {
			ifp->if_oerrors++;
			m_freem(m);
			continue;
		}

		bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);

		command = MVPP2_TXD_L4_CSUM_NOT |
		    MVPP2_TXD_IP_CSUM_DISABLE;
		for (i = 0; i < map->dm_nsegs; i++) {
			txd = &txq->descs[current];
			memset(txd, 0, sizeof(*txd));
			txd->buf_phys_addr_hw_cmd2 =
			    map->dm_segs[i].ds_addr & ~0x1f;
			txd->packet_offset =
			    map->dm_segs[i].ds_addr & 0x1f;
			txd->data_size = map->dm_segs[i].ds_len;
			txd->phys_txq = sc->sc_txqs[0].id;
			txd->command = command |
			    MVPP2_TXD_PADDING_DISABLE;
			if (i == 0)
				txd->command |= MVPP2_TXD_F_DESC;
			if (i == (map->dm_nsegs - 1))
				txd->command |= MVPP2_TXD_L_DESC;

			bus_dmamap_sync(sc->sc_dmat, MVPP2_DMA_MAP(txq->ring),
			    current * sizeof(*txd), sizeof(*txd),
			    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

			last = current;
			current = (current + 1) % MVPP2_AGGR_TXQ_SIZE;
			KASSERT(current != txq->cons);
		}

		KASSERT(txq->buf[last].mb_m == NULL);
		txq->buf[first].mb_map = txq->buf[last].mb_map;
		txq->buf[last].mb_map = map;
		txq->buf[last].mb_m = m;

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		free -= map->dm_nsegs;
		used += map->dm_nsegs;
		prod = current;
	}

	if (used)
		mvpp2_write(sc->sc, MVPP2_AGGR_TXQ_UPDATE_REG, used);

	if (txq->prod != prod)
		txq->prod = prod;
}

int
mvpp2_ioctl(struct ifnet *ifp, u_long cmd, caddr_t addr)
{
	struct mvpp2_port *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)addr;
	int error = 0, s;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				mvpp2_up(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				mvpp2_down(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	case SIOCGIFRXR:
		error = mvpp2_rxrinfo(sc, (struct if_rxrinfo *)ifr->ifr_data);
		break;

	case SIOCGIFSFFPAGE:
		error = rw_enter(&mvpp2_sff_lock, RW_WRITE|RW_INTR);
		if (error != 0)
			break;

		error = sfp_get_sffpage(sc->sc_sfp, (struct if_sffpage *)addr);
		rw_exit(&mvpp2_sff_lock);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, addr);
		break;
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			mvpp2_iff(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

int
mvpp2_rxrinfo(struct mvpp2_port *sc, struct if_rxrinfo *ifri)
{
	struct mvpp2_rx_queue *rxq;
	struct if_rxring_info *ifrs, *ifr;
	unsigned int i;
	int error;

	ifrs = mallocarray(sc->sc_nrxq, sizeof(*ifrs), M_TEMP,
	    M_WAITOK|M_ZERO|M_CANFAIL);
	if (ifrs == NULL)
		return (ENOMEM);

	for (i = 0; i < sc->sc_nrxq; i++) {
		rxq = &sc->sc_rxqs[i];
		ifr = &ifrs[i];

		snprintf(ifr->ifr_name, sizeof(ifr->ifr_name), "%u", i);
		ifr->ifr_size = MCLBYTES;
		ifr->ifr_info = rxq->rxring;
	}

	error = if_rxr_info_ioctl(ifri, i, ifrs);
	free(ifrs, M_TEMP, i * sizeof(*ifrs));

	return (error);
}

void
mvpp2_watchdog(struct ifnet *ifp)
{
	printf("%s\n", __func__);
}

int
mvpp2_media_change(struct ifnet *ifp)
{
	struct mvpp2_port *sc = ifp->if_softc;

	if (LIST_FIRST(&sc->sc_mii.mii_phys))
		mii_mediachg(&sc->sc_mii);

	return (0);
}

void
mvpp2_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct mvpp2_port *sc = ifp->if_softc;

	if (LIST_FIRST(&sc->sc_mii.mii_phys))
		mii_pollstat(&sc->sc_mii);

	ifmr->ifm_active = sc->sc_mii.mii_media_active;
	ifmr->ifm_status = sc->sc_mii.mii_media_status;
}

int
mvpp2_mii_readreg(struct device *self, int phy, int reg)
{
	struct mvpp2_port *sc = (void *)self;
	return sc->sc_mdio->md_readreg(sc->sc_mdio->md_cookie, phy, reg);
}

void
mvpp2_mii_writereg(struct device *self, int phy, int reg, int val)
{
	struct mvpp2_port *sc = (void *)self;
	return sc->sc_mdio->md_writereg(sc->sc_mdio->md_cookie, phy, reg, val);
}

void
mvpp2_mii_statchg(struct device *self)
{
	struct mvpp2_port *sc = (void *)self;
	mvpp2_port_change(sc);
}

void
mvpp2_inband_statchg(struct mvpp2_port *sc)
{
	uint64_t subtype = IFM_SUBTYPE(sc->sc_mii.mii_media_active);
	uint32_t reg;

	sc->sc_mii.mii_media_status = IFM_AVALID;
	sc->sc_mii.mii_media_active = IFM_ETHER;

	if (sc->sc_gop_id == 0 && (sc->sc_phy_mode == PHY_MODE_10GBASER ||
	    sc->sc_phy_mode == PHY_MODE_XAUI)) {
		reg = mvpp2_xlg_read(sc, MV_XLG_MAC_PORT_STATUS_REG);
		if (reg & MV_XLG_MAC_PORT_STATUS_LINKSTATUS)
			sc->sc_mii.mii_media_status |= IFM_ACTIVE;
		sc->sc_mii.mii_media_active |= IFM_FDX;
		sc->sc_mii.mii_media_active |= subtype;
	} else {
		reg = mvpp2_gmac_read(sc, MVPP2_PORT_STATUS0_REG);
		if (reg & MVPP2_PORT_STATUS0_LINKUP)
			sc->sc_mii.mii_media_status |= IFM_ACTIVE;
		if (reg & MVPP2_PORT_STATUS0_FULLDX)
			sc->sc_mii.mii_media_active |= IFM_FDX;
		if (sc->sc_phy_mode == PHY_MODE_2500BASEX)
			sc->sc_mii.mii_media_active |= subtype;
		else if (sc->sc_phy_mode == PHY_MODE_1000BASEX)
			sc->sc_mii.mii_media_active |= subtype;
		else if (reg & MVPP2_PORT_STATUS0_GMIISPEED)
			sc->sc_mii.mii_media_active |= IFM_1000_T;
		else if (reg & MVPP2_PORT_STATUS0_MIISPEED)
			sc->sc_mii.mii_media_active |= IFM_100_TX;
		else
			sc->sc_mii.mii_media_active |= IFM_10_T;
	}

	mvpp2_port_change(sc);
}

void
mvpp2_port_change(struct mvpp2_port *sc)
{
	uint32_t reg;

	sc->sc_link = !!(sc->sc_mii.mii_media_status & IFM_ACTIVE);

	if (sc->sc_inband_status)
		return;

	if (sc->sc_link) {
		if (sc->sc_phy_mode == PHY_MODE_10GBASER ||
		    sc->sc_phy_mode == PHY_MODE_XAUI) {
			reg = mvpp2_xlg_read(sc, MV_XLG_PORT_MAC_CTRL0_REG);
			reg &= ~MV_XLG_MAC_CTRL0_FORCELINKDOWN;
			reg |= MV_XLG_MAC_CTRL0_FORCELINKPASS;
			mvpp2_xlg_write(sc, MV_XLG_PORT_MAC_CTRL0_REG, reg);
		} else {
			reg = mvpp2_gmac_read(sc, MVPP2_GMAC_AUTONEG_CONFIG);
			reg &= ~MVPP2_GMAC_FORCE_LINK_DOWN;
			reg |= MVPP2_GMAC_FORCE_LINK_PASS;
			reg &= ~MVPP2_GMAC_CONFIG_MII_SPEED;
			reg &= ~MVPP2_GMAC_CONFIG_GMII_SPEED;
			reg &= ~MVPP2_GMAC_CONFIG_FULL_DUPLEX;
			if (IFM_SUBTYPE(sc->sc_mii.mii_media_active) == IFM_2500_KX ||
			    IFM_SUBTYPE(sc->sc_mii.mii_media_active) == IFM_2500_SX ||
			    IFM_SUBTYPE(sc->sc_mii.mii_media_active) == IFM_1000_CX ||
			    IFM_SUBTYPE(sc->sc_mii.mii_media_active) == IFM_1000_LX ||
			    IFM_SUBTYPE(sc->sc_mii.mii_media_active) == IFM_1000_KX ||
			    IFM_SUBTYPE(sc->sc_mii.mii_media_active) == IFM_1000_SX ||
			    IFM_SUBTYPE(sc->sc_mii.mii_media_active) == IFM_1000_T)
				reg |= MVPP2_GMAC_CONFIG_GMII_SPEED;
			if (IFM_SUBTYPE(sc->sc_mii.mii_media_active) == IFM_100_TX)
				reg |= MVPP2_GMAC_CONFIG_MII_SPEED;
			if ((sc->sc_mii.mii_media_active & IFM_GMASK) == IFM_FDX)
				reg |= MVPP2_GMAC_CONFIG_FULL_DUPLEX;
			mvpp2_gmac_write(sc, MVPP2_GMAC_AUTONEG_CONFIG, reg);
		}
	} else {
		if (sc->sc_phy_mode == PHY_MODE_10GBASER ||
		    sc->sc_phy_mode == PHY_MODE_XAUI) {
			reg = mvpp2_xlg_read(sc, MV_XLG_PORT_MAC_CTRL0_REG);
			reg &= ~MV_XLG_MAC_CTRL0_FORCELINKPASS;
			reg |= MV_XLG_MAC_CTRL0_FORCELINKDOWN;
			mvpp2_xlg_write(sc, MV_XLG_PORT_MAC_CTRL0_REG, reg);
		} else {
			reg = mvpp2_gmac_read(sc, MVPP2_GMAC_AUTONEG_CONFIG);
			reg &= ~MVPP2_GMAC_FORCE_LINK_PASS;
			reg |= MVPP2_GMAC_FORCE_LINK_DOWN;
			mvpp2_gmac_write(sc, MVPP2_GMAC_AUTONEG_CONFIG, reg);
		}
	}
}

void
mvpp2_tick(void *arg)
{
	struct mvpp2_port *sc = arg;
	int s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	splx(s);

	timeout_add_sec(&sc->sc_tick, 1);
}

int
mvpp2_link_intr(void *arg)
{
	struct mvpp2_port *sc = arg;
	uint32_t reg;
	int event = 0;

	if (sc->sc_gop_id == 0 && (sc->sc_phy_mode == PHY_MODE_10GBASER ||
	    sc->sc_phy_mode == PHY_MODE_XAUI)) {
		reg = mvpp2_xlg_read(sc, MV_XLG_INTERRUPT_CAUSE_REG);
		if (reg & MV_XLG_INTERRUPT_LINK_CHANGE)
			event = 1;
	} else if (sc->sc_phy_mode == PHY_MODE_2500BASEX ||
	    sc->sc_phy_mode == PHY_MODE_1000BASEX ||
	    sc->sc_phy_mode == PHY_MODE_SGMII ||
	    sc->sc_phy_mode == PHY_MODE_RGMII ||
	    sc->sc_phy_mode == PHY_MODE_RGMII_ID ||
	    sc->sc_phy_mode == PHY_MODE_RGMII_RXID ||
	    sc->sc_phy_mode == PHY_MODE_RGMII_TXID) {
		reg = mvpp2_gmac_read(sc, MVPP2_GMAC_INT_CAUSE_REG);
		if (reg & MVPP2_GMAC_INT_CAUSE_LINK_CHANGE)
			event = 1;
	}

	if (event && sc->sc_inband_status)
		mvpp2_inband_statchg(sc);

	return (1);
}

int
mvpp2_intr(void *arg)
{
	struct mvpp2_port *sc = arg;
	uint32_t reg;

	reg = mvpp2_read(sc->sc, MVPP2_ISR_RX_TX_CAUSE_REG(sc->sc_id));
	if (reg & MVPP2_CAUSE_MISC_SUM_MASK) {
		mvpp2_write(sc->sc, MVPP2_ISR_MISC_CAUSE_REG, 0);
		mvpp2_write(sc->sc, MVPP2_ISR_RX_TX_CAUSE_REG(sc->sc_id),
		    reg & ~MVPP2_CAUSE_MISC_SUM_MASK);
	}
	if (reg & MVPP2_CAUSE_TXQ_OCCUP_DESC_ALL_MASK)
		mvpp2_tx_proc(sc,
		    (reg & MVPP2_CAUSE_TXQ_OCCUP_DESC_ALL_MASK) >>
		    MVPP2_CAUSE_TXQ_OCCUP_DESC_ALL_OFFSET);

	if (reg & MVPP2_CAUSE_RXQ_OCCUP_DESC_ALL_MASK)
		mvpp2_rx_proc(sc,
		    reg & MVPP2_CAUSE_RXQ_OCCUP_DESC_ALL_MASK);

	return (1);
}

void
mvpp2_tx_proc(struct mvpp2_port *sc, uint8_t queues)
{
	struct mvpp2_tx_queue *txq;
	int i;

	for (i = 0; i < sc->sc_ntxq; i++) {
		txq = &sc->sc_txqs[i];
		if ((queues & (1 << i)) == 0)
			continue;
		mvpp2_txq_proc(sc, txq);
	}
}

void
mvpp2_txq_proc(struct mvpp2_port *sc, struct mvpp2_tx_queue *txq)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct mvpp2_tx_queue *aggr_txq = &sc->sc->sc_aggr_txqs[0];
	struct mvpp2_buf *txb;
	int i, idx, nsent;

	/* XXX: this is a percpu register! */
	nsent = (mvpp2_read(sc->sc, MVPP2_TXQ_SENT_REG(txq->id)) &
	    MVPP2_TRANSMITTED_COUNT_MASK) >>
	    MVPP2_TRANSMITTED_COUNT_OFFSET;

	for (i = 0; i < nsent; i++) {
		idx = aggr_txq->cons;
		KASSERT(idx < MVPP2_AGGR_TXQ_SIZE);

		txb = &aggr_txq->buf[idx];
		if (txb->mb_m) {
			bus_dmamap_sync(sc->sc_dmat, txb->mb_map, 0,
			    txb->mb_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, txb->mb_map);

			m_freem(txb->mb_m);
			txb->mb_m = NULL;
		}

		aggr_txq->cons = (aggr_txq->cons + 1) % MVPP2_AGGR_TXQ_SIZE;
	}

	if (ifq_is_oactive(&ifp->if_snd))
		ifq_restart(&ifp->if_snd);
}

void
mvpp2_rx_proc(struct mvpp2_port *sc, uint8_t queues)
{
	struct mvpp2_rx_queue *rxq;
	int i;

	for (i = 0; i < sc->sc_nrxq; i++) {
		rxq = &sc->sc_rxqs[i];
		if ((queues & (1 << i)) == 0)
			continue;
		mvpp2_rxq_proc(sc, rxq);
	}

	mvpp2_rx_refill(sc);
}

void
mvpp2_rxq_proc(struct mvpp2_port *sc, struct mvpp2_rx_queue *rxq)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mvpp2_rx_desc *rxd;
	struct mvpp2_bm_pool *bm;
	struct mvpp2_buf *rxb;
	struct mbuf *m;
	uint64_t virt;
	uint32_t i, nrecv, pool;

	nrecv = mvpp2_rxq_received(sc, rxq->id);
	if (!nrecv)
		return;

	pool = curcpu()->ci_cpuid;
	KASSERT(pool < sc->sc->sc_npools);
	bm = &sc->sc->sc_bm_pools[pool];

	bus_dmamap_sync(sc->sc_dmat, MVPP2_DMA_MAP(rxq->ring), 0,
	    MVPP2_DMA_LEN(rxq->ring),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (i = 0; i < nrecv; i++) {
		rxd = &rxq->descs[rxq->cons];
		virt = rxd->buf_cookie_bm_qset_cls_info;
		KASSERT(((virt >> 16) & 0xffff) == pool);
		KASSERT((virt & 0xffff) < MVPP2_BM_SIZE);
		rxb = &bm->rxbuf[virt & 0xffff];
		KASSERT(rxb->mb_m != NULL);

		bus_dmamap_sync(sc->sc_dmat, rxb->mb_map, 0,
		    rxd->data_size, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, rxb->mb_map);

		m = rxb->mb_m;
		rxb->mb_m = NULL;

		m->m_pkthdr.len = m->m_len = rxd->data_size;
		m_adj(m, MVPP2_MH_SIZE);
		ml_enqueue(&ml, m);

		KASSERT(bm->freelist[bm->free_prod] == -1);
		bm->freelist[bm->free_prod] = virt & 0xffffffff;
		bm->free_prod = (bm->free_prod + 1) % MVPP2_BM_SIZE;

		rxq->cons = (rxq->cons + 1) % MVPP2_NRXDESC;
	}

	bus_dmamap_sync(sc->sc_dmat, MVPP2_DMA_MAP(rxq->ring), 0,
	    MVPP2_DMA_LEN(rxq->ring),
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	mvpp2_rxq_status_update(sc, rxq->id, nrecv, nrecv);

	if_input(ifp, &ml);
}

/*
 * We have a pool per core, and since we should not assume that
 * RX buffers are always used in order, keep a list of rxbuf[]
 * indices that should be filled with an mbuf, if possible.
 */
void
mvpp2_rx_refill(struct mvpp2_port *sc)
{
	struct mvpp2_bm_pool *bm;
	struct mvpp2_buf *rxb;
	uint64_t phys, virt;
	int pool;

	pool = curcpu()->ci_cpuid;
	KASSERT(pool < sc->sc->sc_npools);
	bm = &sc->sc->sc_bm_pools[pool];

	while (bm->freelist[bm->free_cons] != -1) {
		virt = bm->freelist[bm->free_cons];
		KASSERT(((virt >> 16) & 0xffff) == pool);
		KASSERT((virt & 0xffff) < MVPP2_BM_SIZE);
		rxb = &bm->rxbuf[virt & 0xffff];
		KASSERT(rxb->mb_m == NULL);

		rxb->mb_m = mvpp2_alloc_mbuf(sc->sc, rxb->mb_map);
		if (rxb->mb_m == NULL)
			break;

		bm->freelist[bm->free_cons] = -1;
		bm->free_cons = (bm->free_cons + 1) % MVPP2_BM_SIZE;

		phys = rxb->mb_map->dm_segs[0].ds_addr;
		mvpp2_write(sc->sc, MVPP22_BM_ADDR_HIGH_RLS_REG,
		    (((virt >> 32) & MVPP22_ADDR_HIGH_MASK)
		    << MVPP22_BM_ADDR_HIGH_VIRT_RLS_SHIFT) |
		    ((phys >> 32) & MVPP22_ADDR_HIGH_MASK));
		mvpp2_write(sc->sc, MVPP2_BM_VIRT_RLS_REG,
		    virt & 0xffffffff);
		mvpp2_write(sc->sc, MVPP2_BM_PHY_RLS_REG(pool),
		    phys & 0xffffffff);
	}
}

void
mvpp2_up(struct mvpp2_port *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int i;

	if (sc->sc_sfp) {
		rw_enter(&mvpp2_sff_lock, RW_WRITE);
		sfp_enable(sc->sc_sfp);
		rw_exit(&mvpp2_sff_lock);
	}

	mvpp2_prs_mac_da_accept(sc, etherbroadcastaddr, 1);
	mvpp2_prs_mac_da_accept(sc, sc->sc_lladdr, 1);
	mvpp2_prs_tag_mode_set(sc->sc, sc->sc_id, MVPP2_TAG_TYPE_MH);
	mvpp2_prs_def_flow(sc);

	for (i = 0; i < sc->sc_ntxq; i++)
		mvpp2_txq_hw_init(sc, &sc->sc_txqs[i]);

	mvpp2_tx_time_coal_set(sc, sc->sc_tx_time_coal);

	for (i = 0; i < sc->sc_nrxq; i++)
		mvpp2_rxq_hw_init(sc, &sc->sc_rxqs[i]);

	/* FIXME: rx buffer fill */

	/* Configure media. */
	if (LIST_FIRST(&sc->sc_mii.mii_phys))
		mii_mediachg(&sc->sc_mii);

	/* Program promiscuous mode and multicast filters. */
	mvpp2_iff(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	mvpp2_txp_max_tx_size_set(sc);

	/* XXX: single vector */
	mvpp2_write(sc->sc, MVPP2_ISR_RX_TX_MASK_REG(sc->sc_id),
	    MVPP2_CAUSE_RXQ_OCCUP_DESC_ALL_MASK |
	    MVPP2_CAUSE_TXQ_OCCUP_DESC_ALL_MASK |
	    MVPP2_CAUSE_MISC_SUM_MASK);
	mvpp2_interrupts_enable(sc, (1 << 0));

	mvpp2_mac_config(sc);
	mvpp2_egress_enable(sc);
	mvpp2_ingress_enable(sc);

	timeout_add_sec(&sc->sc_tick, 1);
}

void
mvpp2_aggr_txq_hw_init(struct mvpp2_softc *sc, struct mvpp2_tx_queue *txq)
{
	struct mvpp2_buf *txb;
	int i;

	txq->ring = mvpp2_dmamem_alloc(sc,
	    MVPP2_AGGR_TXQ_SIZE * sizeof(struct mvpp2_tx_desc), 32);
	KASSERT(txq->ring != NULL);
	txq->descs = MVPP2_DMA_KVA(txq->ring);

	txq->buf = mallocarray(MVPP2_AGGR_TXQ_SIZE, sizeof(struct mvpp2_buf),
	    M_DEVBUF, M_WAITOK);

	for (i = 0; i < MVPP2_AGGR_TXQ_SIZE; i++) {
		txb = &txq->buf[i];
		bus_dmamap_create(sc->sc_dmat, MCLBYTES, MVPP2_NTXSEGS,
		    MCLBYTES, 0, BUS_DMA_WAITOK, &txb->mb_map);
		txb->mb_m = NULL;
	}

	bus_dmamap_sync(sc->sc_dmat, MVPP2_DMA_MAP(txq->ring), 0,
	    MVPP2_DMA_LEN(txq->ring), BUS_DMASYNC_PREWRITE);

	txq->prod = mvpp2_read(sc, MVPP2_AGGR_TXQ_INDEX_REG(txq->id));
	mvpp2_write(sc, MVPP2_AGGR_TXQ_DESC_ADDR_REG(txq->id),
	    MVPP2_DMA_DVA(txq->ring) >> MVPP22_DESC_ADDR_OFFS);
	mvpp2_write(sc, MVPP2_AGGR_TXQ_DESC_SIZE_REG(txq->id),
	    MVPP2_AGGR_TXQ_SIZE);
}

void
mvpp2_txq_hw_init(struct mvpp2_port *sc, struct mvpp2_tx_queue *txq)
{
	struct mvpp2_buf *txb;
	int desc, desc_per_txq;
	uint32_t reg;
	int i;

	txq->prod = txq->cons = 0;
//	txq->last_desc = txq->size - 1;

	txq->ring = mvpp2_dmamem_alloc(sc->sc,
	    MVPP2_NTXDESC * sizeof(struct mvpp2_tx_desc), 32);
	KASSERT(txq->ring != NULL);
	txq->descs = MVPP2_DMA_KVA(txq->ring);

	txq->buf = mallocarray(MVPP2_NTXDESC, sizeof(struct mvpp2_buf),
	    M_DEVBUF, M_WAITOK);

	for (i = 0; i < MVPP2_NTXDESC; i++) {
		txb = &txq->buf[i];
		bus_dmamap_create(sc->sc_dmat, MCLBYTES, MVPP2_NTXSEGS,
		    MCLBYTES, 0, BUS_DMA_WAITOK, &txb->mb_map);
		txb->mb_m = NULL;
	}

	bus_dmamap_sync(sc->sc_dmat, MVPP2_DMA_MAP(txq->ring), 0,
	    MVPP2_DMA_LEN(txq->ring), BUS_DMASYNC_PREWRITE);

	mvpp2_write(sc->sc, MVPP2_TXQ_NUM_REG, txq->id);
	mvpp2_write(sc->sc, MVPP2_TXQ_DESC_ADDR_REG,
	    MVPP2_DMA_DVA(txq->ring));
	mvpp2_write(sc->sc, MVPP2_TXQ_DESC_SIZE_REG,
	    MVPP2_NTXDESC & MVPP2_TXQ_DESC_SIZE_MASK);
	mvpp2_write(sc->sc, MVPP2_TXQ_INDEX_REG, 0);
	mvpp2_write(sc->sc, MVPP2_TXQ_RSVD_CLR_REG,
	    txq->id << MVPP2_TXQ_RSVD_CLR_OFFSET);
	reg = mvpp2_read(sc->sc, MVPP2_TXQ_PENDING_REG);
	reg &= ~MVPP2_TXQ_PENDING_MASK;
	mvpp2_write(sc->sc, MVPP2_TXQ_PENDING_REG, reg);

	desc_per_txq = 16;
	desc = (sc->sc_id * MVPP2_MAX_TXQ * desc_per_txq) +
	    (txq->log_id * desc_per_txq);

	mvpp2_write(sc->sc, MVPP2_TXQ_PREF_BUF_REG,
	    MVPP2_PREF_BUF_PTR(desc) | MVPP2_PREF_BUF_SIZE_16 |
	    MVPP2_PREF_BUF_THRESH(desc_per_txq / 2));

	/* WRR / EJP configuration - indirect access */
	mvpp2_write(sc->sc, MVPP2_TXP_SCHED_PORT_INDEX_REG,
	    mvpp2_egress_port(sc));

	reg = mvpp2_read(sc->sc, MVPP2_TXQ_SCHED_REFILL_REG(txq->log_id));
	reg &= ~MVPP2_TXQ_REFILL_PERIOD_ALL_MASK;
	reg |= MVPP2_TXQ_REFILL_PERIOD_MASK(1);
	reg |= MVPP2_TXQ_REFILL_TOKENS_ALL_MASK;
	mvpp2_write(sc->sc, MVPP2_TXQ_SCHED_REFILL_REG(txq->log_id), reg);

	mvpp2_write(sc->sc, MVPP2_TXQ_SCHED_TOKEN_SIZE_REG(txq->log_id),
	    MVPP2_TXQ_TOKEN_SIZE_MAX);

	mvpp2_tx_pkts_coal_set(sc, txq, txq->done_pkts_coal);

	mvpp2_read(sc->sc, MVPP2_TXQ_SENT_REG(txq->id));
}

void
mvpp2_rxq_hw_init(struct mvpp2_port *sc, struct mvpp2_rx_queue *rxq)
{
	rxq->prod = rxq->cons = 0;

	rxq->ring = mvpp2_dmamem_alloc(sc->sc,
	    MVPP2_NRXDESC * sizeof(struct mvpp2_rx_desc), 32);
	KASSERT(rxq->ring != NULL);
	rxq->descs = MVPP2_DMA_KVA(rxq->ring);

	bus_dmamap_sync(sc->sc_dmat, MVPP2_DMA_MAP(rxq->ring),
	    0, MVPP2_DMA_LEN(rxq->ring),
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	mvpp2_write(sc->sc, MVPP2_RXQ_STATUS_REG(rxq->id), 0);
	mvpp2_write(sc->sc, MVPP2_RXQ_NUM_REG, rxq->id);
	mvpp2_write(sc->sc, MVPP2_RXQ_DESC_ADDR_REG,
	    MVPP2_DMA_DVA(rxq->ring) >> MVPP22_DESC_ADDR_OFFS);
	mvpp2_write(sc->sc, MVPP2_RXQ_DESC_SIZE_REG, MVPP2_NRXDESC);
	mvpp2_write(sc->sc, MVPP2_RXQ_INDEX_REG, 0);
	mvpp2_rxq_offset_set(sc, rxq->id, 0);
	mvpp2_rx_pkts_coal_set(sc, rxq, rxq->pkts_coal);
	mvpp2_rx_time_coal_set(sc, rxq, rxq->time_coal);
	mvpp2_rxq_status_update(sc, rxq->id, 0, MVPP2_NRXDESC);
}

void
mvpp2_mac_reset_assert(struct mvpp2_port *sc)
{
	mvpp2_gmac_write(sc, MVPP2_PORT_CTRL2_REG,
	    mvpp2_gmac_read(sc, MVPP2_PORT_CTRL2_REG) |
	    MVPP2_PORT_CTRL2_PORTMACRESET);
	if (sc->sc_gop_id == 0)
		mvpp2_xlg_write(sc, MV_XLG_PORT_MAC_CTRL0_REG,
		    mvpp2_xlg_read(sc, MV_XLG_PORT_MAC_CTRL0_REG) &
		    ~MV_XLG_MAC_CTRL0_MACRESETN);
}

void
mvpp2_pcs_reset_assert(struct mvpp2_port *sc)
{
	uint32_t reg;

	if (sc->sc_gop_id != 0)
		return;

	reg = mvpp2_mpcs_read(sc, MVPP22_MPCS_CLOCK_RESET);
	reg |= MVPP22_MPCS_CLK_DIV_PHASE_SET;
	reg &= ~MVPP22_MPCS_TX_SD_CLK_RESET;
	reg &= ~MVPP22_MPCS_RX_SD_CLK_RESET;
	reg &= ~MVPP22_MPCS_MAC_CLK_RESET;
	mvpp2_mpcs_write(sc, MVPP22_MPCS_CLOCK_RESET, reg);
	reg = mvpp2_xpcs_read(sc, MVPP22_XPCS_GLOBAL_CFG_0_REG);
	reg &= ~MVPP22_XPCS_PCSRESET;
	mvpp2_xpcs_write(sc, MVPP22_XPCS_GLOBAL_CFG_0_REG, reg);
}

void
mvpp2_pcs_reset_deassert(struct mvpp2_port *sc)
{
	uint32_t reg;

	if (sc->sc_gop_id != 0)
		return;

	if (sc->sc_phy_mode == PHY_MODE_10GBASER) {
		reg = mvpp2_mpcs_read(sc, MVPP22_MPCS_CLOCK_RESET);
		reg &= ~MVPP22_MPCS_CLK_DIV_PHASE_SET;
		reg |= MVPP22_MPCS_TX_SD_CLK_RESET;
		reg |= MVPP22_MPCS_RX_SD_CLK_RESET;
		reg |= MVPP22_MPCS_MAC_CLK_RESET;
		mvpp2_mpcs_write(sc, MVPP22_MPCS_CLOCK_RESET, reg);
	} else if (sc->sc_phy_mode == PHY_MODE_XAUI) {
		reg = mvpp2_xpcs_read(sc, MVPP22_XPCS_GLOBAL_CFG_0_REG);
		reg |= MVPP22_XPCS_PCSRESET;
		mvpp2_xpcs_write(sc, MVPP22_XPCS_GLOBAL_CFG_0_REG, reg);
	}
}

void
mvpp2_mac_config(struct mvpp2_port *sc)
{
	uint32_t reg;

	reg = mvpp2_gmac_read(sc, MVPP2_GMAC_AUTONEG_CONFIG);
	reg &= ~MVPP2_GMAC_FORCE_LINK_PASS;
	reg |= MVPP2_GMAC_FORCE_LINK_DOWN;
	mvpp2_gmac_write(sc, MVPP2_GMAC_AUTONEG_CONFIG, reg);
	if (sc->sc_gop_id == 0) {
		reg = mvpp2_xlg_read(sc, MV_XLG_PORT_MAC_CTRL0_REG);
		reg &= ~MV_XLG_MAC_CTRL0_FORCELINKPASS;
		reg |= MV_XLG_MAC_CTRL0_FORCELINKDOWN;
		mvpp2_xlg_write(sc, MV_XLG_PORT_MAC_CTRL0_REG, reg);
	}

	mvpp2_port_disable(sc);

	mvpp2_mac_reset_assert(sc);
	mvpp2_pcs_reset_assert(sc);

	mvpp2_gop_intr_mask(sc);
	mvpp2_comphy_config(sc, 0);

	if (sc->sc_gop_id == 0 && (sc->sc_phy_mode == PHY_MODE_10GBASER ||
	    sc->sc_phy_mode == PHY_MODE_XAUI))
		mvpp2_xlg_config(sc);
	else
		mvpp2_gmac_config(sc);

	mvpp2_comphy_config(sc, 1);
	mvpp2_gop_config(sc);

	mvpp2_pcs_reset_deassert(sc);

	if (sc->sc_gop_id == 0) {
		reg = mvpp2_xlg_read(sc, MV_XLG_PORT_MAC_CTRL3_REG);
		reg &= ~MV_XLG_MAC_CTRL3_MACMODESELECT_MASK;
		if (sc->sc_phy_mode == PHY_MODE_10GBASER ||
		    sc->sc_phy_mode == PHY_MODE_XAUI)
			reg |= MV_XLG_MAC_CTRL3_MACMODESELECT_10G;
		else
			reg |= MV_XLG_MAC_CTRL3_MACMODESELECT_GMAC;
		mvpp2_xlg_write(sc, MV_XLG_PORT_MAC_CTRL3_REG, reg);
	}

	if (sc->sc_gop_id == 0 && (sc->sc_phy_mode == PHY_MODE_10GBASER ||
	    sc->sc_phy_mode == PHY_MODE_XAUI)) {
		reg = mvpp2_xlg_read(sc, MV_XLG_PORT_MAC_CTRL1_REG);
		reg &= ~MV_XLG_MAC_CTRL1_FRAMESIZELIMIT_MASK;
		reg |= ((MCLBYTES - MVPP2_MH_SIZE) / 2) <<
		    MV_XLG_MAC_CTRL1_FRAMESIZELIMIT_OFFS;
		mvpp2_xlg_write(sc, MV_XLG_PORT_MAC_CTRL1_REG, reg);
	} else {
		reg = mvpp2_gmac_read(sc, MVPP2_GMAC_CTRL_0_REG);
		reg &= ~MVPP2_GMAC_MAX_RX_SIZE_MASK;
		reg |= ((MCLBYTES - MVPP2_MH_SIZE) / 2) <<
		    MVPP2_GMAC_MAX_RX_SIZE_OFFS;
		mvpp2_gmac_write(sc, MVPP2_GMAC_CTRL_0_REG, reg);
	}

	mvpp2_gop_intr_unmask(sc);

	if (!(sc->sc_phy_mode == PHY_MODE_10GBASER ||
	    sc->sc_phy_mode == PHY_MODE_XAUI)) {
		mvpp2_gmac_write(sc, MVPP2_PORT_CTRL2_REG,
		    mvpp2_gmac_read(sc, MVPP2_PORT_CTRL2_REG) &
		    ~MVPP2_PORT_CTRL2_PORTMACRESET);
		while (mvpp2_gmac_read(sc, MVPP2_PORT_CTRL2_REG) &
		    MVPP2_PORT_CTRL2_PORTMACRESET)
			;
	}

	mvpp2_port_enable(sc);

	if (sc->sc_inband_status) {
		reg = mvpp2_gmac_read(sc, MVPP2_GMAC_AUTONEG_CONFIG);
		reg &= ~MVPP2_GMAC_FORCE_LINK_PASS;
		reg &= ~MVPP2_GMAC_FORCE_LINK_DOWN;
		mvpp2_gmac_write(sc, MVPP2_GMAC_AUTONEG_CONFIG, reg);
		if (sc->sc_gop_id == 0) {
			reg = mvpp2_xlg_read(sc, MV_XLG_PORT_MAC_CTRL0_REG);
			reg &= ~MV_XLG_MAC_CTRL0_FORCELINKPASS;
			reg &= ~MV_XLG_MAC_CTRL0_FORCELINKDOWN;
			mvpp2_xlg_write(sc, MV_XLG_PORT_MAC_CTRL0_REG, reg);
		}
	} else
		mvpp2_port_change(sc);
}

void
mvpp2_xlg_config(struct mvpp2_port *sc)
{
	uint32_t ctl0, ctl4;

	ctl0 = mvpp2_xlg_read(sc, MV_XLG_PORT_MAC_CTRL0_REG);
	ctl4 = mvpp2_xlg_read(sc, MV_XLG_PORT_MAC_CTRL4_REG);

	ctl0 |= MV_XLG_MAC_CTRL0_MACRESETN;
	ctl4 &= ~MV_XLG_MAC_CTRL4_EN_IDLE_CHECK_FOR_LINK;
	ctl4 |= MV_XLG_MAC_CTRL4_FORWARD_PFC_EN;
	ctl4 |= MV_XLG_MAC_CTRL4_FORWARD_802_3X_FC_EN;

	mvpp2_xlg_write(sc, MV_XLG_PORT_MAC_CTRL0_REG, ctl0);
	mvpp2_xlg_write(sc, MV_XLG_PORT_MAC_CTRL4_REG, ctl4);

	/* Port reset */
	while ((mvpp2_xlg_read(sc, MV_XLG_PORT_MAC_CTRL0_REG) &
	    MV_XLG_MAC_CTRL0_MACRESETN) == 0)
		;
}

void
mvpp2_gmac_config(struct mvpp2_port *sc)
{
	uint32_t ctl0, ctl2, ctl4, panc;

	/* Setup phy. */
	ctl0 = mvpp2_gmac_read(sc, MVPP2_PORT_CTRL0_REG);
	ctl2 = mvpp2_gmac_read(sc, MVPP2_PORT_CTRL2_REG);
	ctl4 = mvpp2_gmac_read(sc, MVPP2_PORT_CTRL4_REG);
	panc = mvpp2_gmac_read(sc, MVPP2_GMAC_AUTONEG_CONFIG);

	ctl0 &= ~MVPP2_GMAC_PORT_TYPE_MASK;
	ctl2 &= ~(MVPP2_GMAC_PORT_RESET_MASK | MVPP2_GMAC_PCS_ENABLE_MASK |
	    MVPP2_GMAC_INBAND_AN_MASK);
	panc &= ~(MVPP2_GMAC_AN_DUPLEX_EN | MVPP2_GMAC_FLOW_CTRL_AUTONEG |
	    MVPP2_GMAC_FC_ADV_ASM_EN | MVPP2_GMAC_FC_ADV_EN |
	    MVPP2_GMAC_AN_SPEED_EN | MVPP2_GMAC_IN_BAND_AUTONEG_BYPASS |
	    MVPP2_GMAC_IN_BAND_AUTONEG);

	switch (sc->sc_phy_mode) {
	case PHY_MODE_XAUI:
	case PHY_MODE_10GBASER:
		break;
	case PHY_MODE_2500BASEX:
	case PHY_MODE_1000BASEX:
		ctl2 |= MVPP2_GMAC_PCS_ENABLE_MASK;
		ctl4 &= ~MVPP2_PORT_CTRL4_EXT_PIN_GMII_SEL;
		ctl4 |= MVPP2_PORT_CTRL4_SYNC_BYPASS;
		ctl4 |= MVPP2_PORT_CTRL4_DP_CLK_SEL;
		ctl4 |= MVPP2_PORT_CTRL4_QSGMII_BYPASS_ACTIVE;
		break;
	case PHY_MODE_SGMII:
		ctl2 |= MVPP2_GMAC_PCS_ENABLE_MASK;
		ctl2 |= MVPP2_GMAC_INBAND_AN_MASK;
		ctl4 &= ~MVPP2_PORT_CTRL4_EXT_PIN_GMII_SEL;
		ctl4 |= MVPP2_PORT_CTRL4_SYNC_BYPASS;
		ctl4 |= MVPP2_PORT_CTRL4_DP_CLK_SEL;
		ctl4 |= MVPP2_PORT_CTRL4_QSGMII_BYPASS_ACTIVE;
		break;
	case PHY_MODE_RGMII:
	case PHY_MODE_RGMII_ID:
	case PHY_MODE_RGMII_RXID:
	case PHY_MODE_RGMII_TXID:
		ctl4 &= ~MVPP2_PORT_CTRL4_DP_CLK_SEL;
		ctl4 |= MVPP2_PORT_CTRL4_EXT_PIN_GMII_SEL;
		ctl4 |= MVPP2_PORT_CTRL4_SYNC_BYPASS;
		ctl4 |= MVPP2_PORT_CTRL4_QSGMII_BYPASS_ACTIVE;
		break;
	}

	/* Use Auto-Negotiation for Inband Status only */
	if (sc->sc_inband_status) {
		panc &= ~MVPP2_GMAC_CONFIG_MII_SPEED;
		panc &= ~MVPP2_GMAC_CONFIG_GMII_SPEED;
		panc &= ~MVPP2_GMAC_CONFIG_FULL_DUPLEX;
		panc |= MVPP2_GMAC_IN_BAND_AUTONEG;
		/* TODO: read mode from SFP */
		if (sc->sc_phy_mode == PHY_MODE_SGMII) {
			/* SGMII */
			panc |= MVPP2_GMAC_AN_SPEED_EN;
			panc |= MVPP2_GMAC_AN_DUPLEX_EN;
		} else {
			/* 802.3z */
			ctl0 |= MVPP2_GMAC_PORT_TYPE_MASK;
			panc |= MVPP2_GMAC_CONFIG_GMII_SPEED;
			panc |= MVPP2_GMAC_CONFIG_FULL_DUPLEX;
		}
	}

	mvpp2_gmac_write(sc, MVPP2_PORT_CTRL0_REG, ctl0);
	mvpp2_gmac_write(sc, MVPP2_PORT_CTRL2_REG, ctl2);
	mvpp2_gmac_write(sc, MVPP2_PORT_CTRL4_REG, ctl4);
	mvpp2_gmac_write(sc, MVPP2_GMAC_AUTONEG_CONFIG, panc);
}

#define COMPHY_BASE		0x120000
#define COMPHY_SIP_POWER_ON	0x82000001
#define COMPHY_SIP_POWER_OFF	0x82000002
#define COMPHY_SPEED(x)		((x) << 2)
#define  COMPHY_SPEED_1_25G		0 /* SGMII 1G */
#define  COMPHY_SPEED_2_5G		1
#define  COMPHY_SPEED_3_125G		2 /* SGMII 2.5G */
#define  COMPHY_SPEED_5G		3
#define  COMPHY_SPEED_5_15625G		4 /* XFI 5G */
#define  COMPHY_SPEED_6G		5
#define  COMPHY_SPEED_10_3125G		6 /* XFI 10G */
#define COMPHY_UNIT(x)		((x) << 8)
#define COMPHY_MODE(x)		((x) << 12)
#define  COMPHY_MODE_SATA		1
#define  COMPHY_MODE_SGMII		2 /* SGMII 1G */
#define  COMPHY_MODE_HS_SGMII		3 /* SGMII 2.5G */
#define  COMPHY_MODE_USB3H		4
#define  COMPHY_MODE_USB3D		5
#define  COMPHY_MODE_PCIE		6
#define  COMPHY_MODE_RXAUI		7
#define  COMPHY_MODE_XFI		8
#define  COMPHY_MODE_SFI		9
#define  COMPHY_MODE_USB3		10
#define  COMPHY_MODE_AP			11

void
mvpp2_comphy_config(struct mvpp2_port *sc, int on)
{
	int node, phys[2], lane, unit;
	uint32_t mode;

	if (OF_getpropintarray(sc->sc_node, "phys", phys, sizeof(phys)) !=
	    sizeof(phys))
		return;
	node = OF_getnodebyphandle(phys[0]);
	if (!node)
		return;

	lane = OF_getpropint(node, "reg", 0);
	unit = phys[1];

	switch (sc->sc_phy_mode) {
	case PHY_MODE_XAUI:
		mode = COMPHY_MODE(COMPHY_MODE_RXAUI) |
		    COMPHY_UNIT(unit);
		break;
	case PHY_MODE_10GBASER:
		mode = COMPHY_MODE(COMPHY_MODE_XFI) |
		    COMPHY_SPEED(COMPHY_SPEED_10_3125G) |
		    COMPHY_UNIT(unit);
		break;
	case PHY_MODE_2500BASEX:
		mode = COMPHY_MODE(COMPHY_MODE_HS_SGMII) |
		    COMPHY_SPEED(COMPHY_SPEED_3_125G) |
		    COMPHY_UNIT(unit);
		break;
	case PHY_MODE_1000BASEX:
	case PHY_MODE_SGMII:
		mode = COMPHY_MODE(COMPHY_MODE_SGMII) |
		    COMPHY_SPEED(COMPHY_SPEED_1_25G) |
		    COMPHY_UNIT(unit);
		break;
	default:
		return;
	}

	if (on)
		smc_call(COMPHY_SIP_POWER_ON, sc->sc->sc_ioh_paddr + COMPHY_BASE,
		    lane, mode);
	else
		smc_call(COMPHY_SIP_POWER_OFF, sc->sc->sc_ioh_paddr + COMPHY_BASE,
		    lane, 0);
}

void
mvpp2_gop_config(struct mvpp2_port *sc)
{
	uint32_t reg;

	if (sc->sc->sc_rm == NULL)
		return;

	if (sc->sc_phy_mode == PHY_MODE_RGMII ||
	    sc->sc_phy_mode == PHY_MODE_RGMII_ID ||
	    sc->sc_phy_mode == PHY_MODE_RGMII_RXID ||
	    sc->sc_phy_mode == PHY_MODE_RGMII_TXID) {
		if (sc->sc_gop_id == 0)
			return;
		reg = regmap_read_4(sc->sc->sc_rm, GENCONF_PORT_CTRL0);
		reg |= GENCONF_PORT_CTRL0_BUS_WIDTH_SELECT;
		regmap_write_4(sc->sc->sc_rm, GENCONF_PORT_CTRL0, reg);
		reg = regmap_read_4(sc->sc->sc_rm, GENCONF_CTRL0);
		if (sc->sc_gop_id == 2)
			reg |= GENCONF_CTRL0_PORT0_RGMII |
			    GENCONF_CTRL0_PORT1_RGMII;
		else if (sc->sc_gop_id == 3)
			reg |= GENCONF_CTRL0_PORT1_RGMII_MII;
		regmap_write_4(sc->sc->sc_rm, GENCONF_CTRL0, reg);
	} else if (sc->sc_phy_mode == PHY_MODE_2500BASEX ||
	    sc->sc_phy_mode == PHY_MODE_1000BASEX ||
	    sc->sc_phy_mode == PHY_MODE_SGMII) {
		reg = regmap_read_4(sc->sc->sc_rm, GENCONF_PORT_CTRL0);
		reg |= GENCONF_PORT_CTRL0_BUS_WIDTH_SELECT |
		    GENCONF_PORT_CTRL0_RX_DATA_SAMPLE;
		regmap_write_4(sc->sc->sc_rm, GENCONF_PORT_CTRL0, reg);
		if (sc->sc_gop_id > 1) {
			reg = regmap_read_4(sc->sc->sc_rm, GENCONF_CTRL0);
			if (sc->sc_gop_id == 2)
				reg &= ~GENCONF_CTRL0_PORT0_RGMII;
			else if (sc->sc_gop_id == 3)
				reg &= ~GENCONF_CTRL0_PORT1_RGMII_MII;
			regmap_write_4(sc->sc->sc_rm, GENCONF_CTRL0, reg);
		}
	} else if (sc->sc_phy_mode == PHY_MODE_10GBASER) {
		if (sc->sc_gop_id != 0)
			return;
		reg = mvpp2_xpcs_read(sc, MVPP22_XPCS_GLOBAL_CFG_0_REG);
		reg &= ~MVPP22_XPCS_PCSMODE_MASK;
		reg &= ~MVPP22_XPCS_LANEACTIVE_MASK;
		reg |= 2 << MVPP22_XPCS_LANEACTIVE_OFFS;
		mvpp2_xpcs_write(sc, MVPP22_XPCS_GLOBAL_CFG_0_REG, reg);
		reg = mvpp2_mpcs_read(sc, MVPP22_MPCS40G_COMMON_CONTROL);
		reg &= ~MVPP22_MPCS_FORWARD_ERROR_CORRECTION_MASK;
		mvpp2_mpcs_write(sc, MVPP22_MPCS40G_COMMON_CONTROL, reg);
		reg = mvpp2_mpcs_read(sc, MVPP22_MPCS_CLOCK_RESET);
		reg &= ~MVPP22_MPCS_CLK_DIVISION_RATIO_MASK;
		reg |= MVPP22_MPCS_CLK_DIVISION_RATIO_DEFAULT;
		mvpp2_mpcs_write(sc, MVPP22_MPCS_CLOCK_RESET, reg);
	} else
		return;

	reg = regmap_read_4(sc->sc->sc_rm, GENCONF_PORT_CTRL1);
	reg |= GENCONF_PORT_CTRL1_RESET(sc->sc_gop_id) |
	    GENCONF_PORT_CTRL1_EN(sc->sc_gop_id);
	regmap_write_4(sc->sc->sc_rm, GENCONF_PORT_CTRL1, reg);

	reg = regmap_read_4(sc->sc->sc_rm, GENCONF_PORT_CTRL0);
	reg |= GENCONF_PORT_CTRL0_CLK_DIV_PHASE_CLR;
	regmap_write_4(sc->sc->sc_rm, GENCONF_PORT_CTRL0, reg);

	reg = regmap_read_4(sc->sc->sc_rm, GENCONF_SOFT_RESET1);
	reg |= GENCONF_SOFT_RESET1_GOP;
	regmap_write_4(sc->sc->sc_rm, GENCONF_SOFT_RESET1, reg);
}

void
mvpp2_gop_intr_mask(struct mvpp2_port *sc)
{
	uint32_t reg;

	if (sc->sc_gop_id == 0) {
		reg = mvpp2_xlg_read(sc, MV_XLG_EXTERNAL_INTERRUPT_MASK_REG);
		reg &= ~MV_XLG_EXTERNAL_INTERRUPT_LINK_CHANGE_XLG;
		reg &= ~MV_XLG_EXTERNAL_INTERRUPT_LINK_CHANGE_GIG;
		mvpp2_xlg_write(sc, MV_XLG_EXTERNAL_INTERRUPT_MASK_REG, reg);
	}

	reg = mvpp2_gmac_read(sc, MVPP2_GMAC_INT_SUM_MASK_REG);
	reg &= ~MVPP2_GMAC_INT_SUM_CAUSE_LINK_CHANGE;
	mvpp2_gmac_write(sc, MVPP2_GMAC_INT_SUM_MASK_REG, reg);
}

void
mvpp2_gop_intr_unmask(struct mvpp2_port *sc)
{
	uint32_t reg;

	reg = mvpp2_gmac_read(sc, MVPP2_GMAC_INT_SUM_MASK_REG);
	reg |= MVPP2_GMAC_INT_SUM_CAUSE_LINK_CHANGE;
	mvpp2_gmac_write(sc, MVPP2_GMAC_INT_SUM_MASK_REG, reg);

	if (sc->sc_gop_id == 0) {
		reg = mvpp2_xlg_read(sc, MV_XLG_EXTERNAL_INTERRUPT_MASK_REG);
		if (sc->sc_phy_mode == PHY_MODE_10GBASER ||
		    sc->sc_phy_mode == PHY_MODE_XAUI)
			reg |= MV_XLG_EXTERNAL_INTERRUPT_LINK_CHANGE_XLG;
		else
			reg |= MV_XLG_EXTERNAL_INTERRUPT_LINK_CHANGE_GIG;
		mvpp2_xlg_write(sc, MV_XLG_EXTERNAL_INTERRUPT_MASK_REG, reg);
	}
}

void
mvpp2_down(struct mvpp2_port *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint32_t reg;
	int i;

	timeout_del(&sc->sc_tick);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	mvpp2_egress_disable(sc);
	mvpp2_ingress_disable(sc);

	mvpp2_mac_reset_assert(sc);
	mvpp2_pcs_reset_assert(sc);

	/* XXX: single vector */
	mvpp2_interrupts_disable(sc, (1 << 0));
	mvpp2_write(sc->sc, MVPP2_ISR_RX_TX_MASK_REG(sc->sc_id), 0);

	reg = mvpp2_read(sc->sc, MVPP2_TX_PORT_FLUSH_REG);
	reg |= MVPP2_TX_PORT_FLUSH_MASK(sc->sc_id);
	mvpp2_write(sc->sc, MVPP2_TX_PORT_FLUSH_REG, reg);

	for (i = 0; i < sc->sc_ntxq; i++)
		mvpp2_txq_hw_deinit(sc, &sc->sc_txqs[i]);

	reg &= ~MVPP2_TX_PORT_FLUSH_MASK(sc->sc_id);
	mvpp2_write(sc->sc, MVPP2_TX_PORT_FLUSH_REG, reg);

	for (i = 0; i < sc->sc_nrxq; i++)
		mvpp2_rxq_hw_deinit(sc, &sc->sc_rxqs[i]);

	if (sc->sc_sfp) {
		rw_enter(&mvpp2_sff_lock, RW_WRITE);
		sfp_disable(sc->sc_sfp);
		rw_exit(&mvpp2_sff_lock);
	}
}

void
mvpp2_txq_hw_deinit(struct mvpp2_port *sc, struct mvpp2_tx_queue *txq)
{
	struct mvpp2_buf *txb;
	int i, pending;
	uint32_t reg;

	mvpp2_write(sc->sc, MVPP2_TXQ_NUM_REG, txq->id);
	reg = mvpp2_read(sc->sc, MVPP2_TXQ_PREF_BUF_REG);
	reg |= MVPP2_TXQ_DRAIN_EN_MASK;
	mvpp2_write(sc->sc, MVPP2_TXQ_PREF_BUF_REG, reg);

	/*
	 * the queue has been stopped so wait for all packets
	 * to be transmitted.
	 */
	i = 0;
	do {
		if (i >= MVPP2_TX_PENDING_TIMEOUT_MSEC) {
			printf("%s: port %d: cleaning queue %d timed out\n",
			    sc->sc_dev.dv_xname, sc->sc_id, txq->log_id);
			break;
		}
		delay(1000);
		i++;

		pending = mvpp2_read(sc->sc, MVPP2_TXQ_PENDING_REG) &
		    MVPP2_TXQ_PENDING_MASK;
	} while (pending);

	reg &= ~MVPP2_TXQ_DRAIN_EN_MASK;
	mvpp2_write(sc->sc, MVPP2_TXQ_PREF_BUF_REG, reg);

	mvpp2_write(sc->sc, MVPP2_TXQ_SCHED_TOKEN_CNTR_REG(txq->log_id), 0);
	mvpp2_write(sc->sc, MVPP2_TXQ_NUM_REG, txq->id);
	mvpp2_write(sc->sc, MVPP2_TXQ_DESC_ADDR_REG, 0);
	mvpp2_write(sc->sc, MVPP2_TXQ_DESC_SIZE_REG, 0);
	mvpp2_read(sc->sc, MVPP2_TXQ_SENT_REG(txq->id));

	for (i = 0; i < MVPP2_NTXDESC; i++) {
		txb = &txq->buf[i];
		if (txb->mb_m) {
			bus_dmamap_sync(sc->sc_dmat, txb->mb_map, 0,
			    txb->mb_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, txb->mb_map);
			m_freem(txb->mb_m);
		}
		bus_dmamap_destroy(sc->sc_dmat, txb->mb_map);
	}

	mvpp2_dmamem_free(sc->sc, txq->ring);
	free(txq->buf, M_DEVBUF, sizeof(struct mvpp2_buf) *
	    MVPP2_NTXDESC);
}

void
mvpp2_rxq_hw_drop(struct mvpp2_port *sc, struct mvpp2_rx_queue *rxq)
{
	struct mvpp2_rx_desc *rxd;
	struct mvpp2_bm_pool *bm;
	uint64_t phys, virt;
	uint32_t i, nrecv, pool;
	struct mvpp2_buf *rxb;

	nrecv = mvpp2_rxq_received(sc, rxq->id);
	if (!nrecv)
		return;

	bus_dmamap_sync(sc->sc_dmat, MVPP2_DMA_MAP(rxq->ring), 0,
	    MVPP2_DMA_LEN(rxq->ring),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (i = 0; i < nrecv; i++) {
		rxd = &rxq->descs[rxq->cons];
		virt = rxd->buf_cookie_bm_qset_cls_info;
		pool = (virt >> 16) & 0xffff;
		KASSERT(pool < sc->sc->sc_npools);
		bm = &sc->sc->sc_bm_pools[pool];
		KASSERT((virt & 0xffff) < MVPP2_BM_SIZE);
		rxb = &bm->rxbuf[virt & 0xffff];
		KASSERT(rxb->mb_m != NULL);
		virt &= 0xffffffff;
		phys = rxb->mb_map->dm_segs[0].ds_addr;
		mvpp2_write(sc->sc, MVPP22_BM_ADDR_HIGH_RLS_REG,
		    (((virt >> 32) & MVPP22_ADDR_HIGH_MASK)
		    << MVPP22_BM_ADDR_HIGH_VIRT_RLS_SHIFT) |
		    ((phys >> 32) & MVPP22_ADDR_HIGH_MASK));
		mvpp2_write(sc->sc, MVPP2_BM_VIRT_RLS_REG,
		    virt & 0xffffffff);
		mvpp2_write(sc->sc, MVPP2_BM_PHY_RLS_REG(pool),
		    phys & 0xffffffff);
		rxq->cons = (rxq->cons + 1) % MVPP2_NRXDESC;
	}

	bus_dmamap_sync(sc->sc_dmat, MVPP2_DMA_MAP(rxq->ring), 0,
	    MVPP2_DMA_LEN(rxq->ring),
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	mvpp2_rxq_status_update(sc, rxq->id, nrecv, nrecv);
}

void
mvpp2_rxq_hw_deinit(struct mvpp2_port *sc, struct mvpp2_rx_queue *rxq)
{
	mvpp2_rxq_hw_drop(sc, rxq);

	mvpp2_write(sc->sc, MVPP2_RXQ_STATUS_REG(rxq->id), 0);
	mvpp2_write(sc->sc, MVPP2_RXQ_NUM_REG, rxq->id);
	mvpp2_write(sc->sc, MVPP2_RXQ_DESC_ADDR_REG, 0);
	mvpp2_write(sc->sc, MVPP2_RXQ_DESC_SIZE_REG, 0);

	mvpp2_dmamem_free(sc->sc, rxq->ring);
}

void
mvpp2_rxq_long_pool_set(struct mvpp2_port *port, int lrxq, int pool)
{
	uint32_t val;
	int prxq;

	/* get queue physical ID */
	prxq = port->sc_rxqs[lrxq].id;

	val = mvpp2_read(port->sc, MVPP2_RXQ_CONFIG_REG(prxq));
	val &= ~MVPP2_RXQ_POOL_LONG_MASK;
	val |= ((pool << MVPP2_RXQ_POOL_LONG_OFFS) & MVPP2_RXQ_POOL_LONG_MASK);

	mvpp2_write(port->sc, MVPP2_RXQ_CONFIG_REG(prxq), val);
}

void
mvpp2_rxq_short_pool_set(struct mvpp2_port *port, int lrxq, int pool)
{
	uint32_t val;
	int prxq;

	/* get queue physical ID */
	prxq = port->sc_rxqs[lrxq].id;

	val = mvpp2_read(port->sc, MVPP2_RXQ_CONFIG_REG(prxq));
	val &= ~MVPP2_RXQ_POOL_SHORT_MASK;
	val |= ((pool << MVPP2_RXQ_POOL_SHORT_OFFS) & MVPP2_RXQ_POOL_SHORT_MASK);

	mvpp2_write(port->sc, MVPP2_RXQ_CONFIG_REG(prxq), val);
}

void
mvpp2_iff(struct mvpp2_port *sc)
{
	struct arpcom *ac = &sc->sc_ac;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;

	ifp->if_flags &= ~IFF_ALLMULTI;

	/* Removes all but broadcast and (new) lladdr */
	mvpp2_prs_mac_del_all(sc);

	if (ifp->if_flags & IFF_PROMISC) {
		mvpp2_prs_mac_promisc_set(sc->sc, sc->sc_id,
		    MVPP2_PRS_L2_UNI_CAST, 1);
		mvpp2_prs_mac_promisc_set(sc->sc, sc->sc_id,
		    MVPP2_PRS_L2_MULTI_CAST, 1);
		return;
	}

	mvpp2_prs_mac_promisc_set(sc->sc, sc->sc_id,
	    MVPP2_PRS_L2_UNI_CAST, 0);
	mvpp2_prs_mac_promisc_set(sc->sc, sc->sc_id,
	    MVPP2_PRS_L2_MULTI_CAST, 0);

	if (ac->ac_multirangecnt > 0 ||
	    ac->ac_multicnt > MVPP2_PRS_MAC_MC_FILT_MAX) {
		ifp->if_flags |= IFF_ALLMULTI;
		mvpp2_prs_mac_promisc_set(sc->sc, sc->sc_id,
		    MVPP2_PRS_L2_MULTI_CAST, 1);
	} else {
		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			mvpp2_prs_mac_da_accept(sc, enm->enm_addrlo, 1);
			ETHER_NEXT_MULTI(step, enm);
		}
	}
}

struct mvpp2_dmamem *
mvpp2_dmamem_alloc(struct mvpp2_softc *sc, bus_size_t size, bus_size_t align)
{
	struct mvpp2_dmamem *mdm;
	int nsegs;

	mdm = malloc(sizeof(*mdm), M_DEVBUF, M_WAITOK | M_ZERO);
	mdm->mdm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, &mdm->mdm_map) != 0)
		goto mdmfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, align, 0, &mdm->mdm_seg, 1,
	    &nsegs, BUS_DMA_WAITOK) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &mdm->mdm_seg, nsegs, size,
	    &mdm->mdm_kva, BUS_DMA_WAITOK | BUS_DMA_COHERENT) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, mdm->mdm_map, mdm->mdm_kva, size,
	    NULL, BUS_DMA_WAITOK) != 0)
		goto unmap;

	bzero(mdm->mdm_kva, size);

	return (mdm);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, mdm->mdm_kva, size);
free:
	bus_dmamem_free(sc->sc_dmat, &mdm->mdm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, mdm->mdm_map);
mdmfree:
	free(mdm, M_DEVBUF, 0);

	return (NULL);
}

void
mvpp2_dmamem_free(struct mvpp2_softc *sc, struct mvpp2_dmamem *mdm)
{
	bus_dmamem_unmap(sc->sc_dmat, mdm->mdm_kva, mdm->mdm_size);
	bus_dmamem_free(sc->sc_dmat, &mdm->mdm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, mdm->mdm_map);
	free(mdm, M_DEVBUF, 0);
}

struct mbuf *
mvpp2_alloc_mbuf(struct mvpp2_softc *sc, bus_dmamap_t map)
{
	struct mbuf *m = NULL;

	m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
	if (!m)
		return (NULL);
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	if (bus_dmamap_load_mbuf(sc->sc_dmat, map, m, BUS_DMA_NOWAIT) != 0) {
		printf("%s: could not load mbuf DMA map", DEVNAME(sc));
		m_freem(m);
		return (NULL);
	}

	bus_dmamap_sync(sc->sc_dmat, map, 0,
	    m->m_pkthdr.len, BUS_DMASYNC_PREREAD);

	return (m);
}

void
mvpp2_interrupts_enable(struct mvpp2_port *port, int cpu_mask)
{
	mvpp2_write(port->sc, MVPP2_ISR_ENABLE_REG(port->sc_id),
	    MVPP2_ISR_ENABLE_INTERRUPT(cpu_mask));
}

void
mvpp2_interrupts_disable(struct mvpp2_port *port, int cpu_mask)
{
	mvpp2_write(port->sc, MVPP2_ISR_ENABLE_REG(port->sc_id),
	    MVPP2_ISR_DISABLE_INTERRUPT(cpu_mask));
}

int
mvpp2_egress_port(struct mvpp2_port *port)
{
	return MVPP2_MAX_TCONT + port->sc_id;
}

int
mvpp2_txq_phys(int port, int txq)
{
	return (MVPP2_MAX_TCONT + port) * MVPP2_MAX_TXQ + txq;
}

void
mvpp2_defaults_set(struct mvpp2_port *port)
{
	int val, queue;

	mvpp2_write(port->sc, MVPP2_TXP_SCHED_PORT_INDEX_REG,
	    mvpp2_egress_port(port));
	mvpp2_write(port->sc, MVPP2_TXP_SCHED_CMD_1_REG, 0);

	for (queue = 0; queue < MVPP2_MAX_TXQ; queue++)
		mvpp2_write(port->sc, MVPP2_TXQ_SCHED_TOKEN_CNTR_REG(queue), 0);

	mvpp2_write(port->sc, MVPP2_TXP_SCHED_PERIOD_REG, port->sc->sc_tclk /
	    (1000 * 1000));
	val = mvpp2_read(port->sc, MVPP2_TXP_SCHED_REFILL_REG);
	val &= ~MVPP2_TXP_REFILL_PERIOD_ALL_MASK;
	val |= MVPP2_TXP_REFILL_PERIOD_MASK(1);
	val |= MVPP2_TXP_REFILL_TOKENS_ALL_MASK;
	mvpp2_write(port->sc, MVPP2_TXP_SCHED_REFILL_REG, val);
	val = MVPP2_TXP_TOKEN_SIZE_MAX;
	mvpp2_write(port->sc, MVPP2_TXP_SCHED_TOKEN_SIZE_REG, val);

	/* set maximum_low_latency_packet_size value to 256 */
	mvpp2_write(port->sc, MVPP2_RX_CTRL_REG(port->sc_id),
	    MVPP2_RX_USE_PSEUDO_FOR_CSUM_MASK |
	    MVPP2_RX_LOW_LATENCY_PKT_SIZE(256));

	/* mask all interrupts to all present cpus */
	mvpp2_interrupts_disable(port, (0xf << 0));
}

void
mvpp2_ingress_enable(struct mvpp2_port *port)
{
	uint32_t val;
	int lrxq, queue;

	for (lrxq = 0; lrxq < port->sc_nrxq; lrxq++) {
		queue = port->sc_rxqs[lrxq].id;
		val = mvpp2_read(port->sc, MVPP2_RXQ_CONFIG_REG(queue));
		val &= ~MVPP2_RXQ_DISABLE_MASK;
		mvpp2_write(port->sc, MVPP2_RXQ_CONFIG_REG(queue), val);
	}
}

void
mvpp2_ingress_disable(struct mvpp2_port *port)
{
	uint32_t val;
	int lrxq, queue;

	for (lrxq = 0; lrxq < port->sc_nrxq; lrxq++) {
		queue = port->sc_rxqs[lrxq].id;
		val = mvpp2_read(port->sc, MVPP2_RXQ_CONFIG_REG(queue));
		val |= MVPP2_RXQ_DISABLE_MASK;
		mvpp2_write(port->sc, MVPP2_RXQ_CONFIG_REG(queue), val);
	}
}

void
mvpp2_egress_enable(struct mvpp2_port *port)
{
	struct mvpp2_tx_queue *txq;
	uint32_t qmap;
	int queue;

	qmap = 0;
	for (queue = 0; queue < port->sc_ntxq; queue++) {
		txq = &port->sc_txqs[queue];

		if (txq->descs != NULL) {
			qmap |= (1 << queue);
		}
	}

	mvpp2_write(port->sc, MVPP2_TXP_SCHED_PORT_INDEX_REG,
	    mvpp2_egress_port(port));
	mvpp2_write(port->sc, MVPP2_TXP_SCHED_Q_CMD_REG, qmap);
}

void
mvpp2_egress_disable(struct mvpp2_port *port)
{
	uint32_t reg_data;
	int i;

	mvpp2_write(port->sc, MVPP2_TXP_SCHED_PORT_INDEX_REG,
	    mvpp2_egress_port(port));
	reg_data = (mvpp2_read(port->sc, MVPP2_TXP_SCHED_Q_CMD_REG)) &
	    MVPP2_TXP_SCHED_ENQ_MASK;
	if (reg_data)
		mvpp2_write(port->sc, MVPP2_TXP_SCHED_Q_CMD_REG,
		    reg_data << MVPP2_TXP_SCHED_DISQ_OFFSET);

	i = 0;
	do {
		if (i >= MVPP2_TX_DISABLE_TIMEOUT_MSEC) {
			printf("%s: tx stop timed out, status=0x%08x\n",
			    port->sc_dev.dv_xname, reg_data);
			break;
		}
		delay(1000);
		i++;
		reg_data = mvpp2_read(port->sc, MVPP2_TXP_SCHED_Q_CMD_REG);
	} while (reg_data & MVPP2_TXP_SCHED_ENQ_MASK);
}

void
mvpp2_port_enable(struct mvpp2_port *port)
{
	uint32_t val;

	if (port->sc_gop_id == 0 && (port->sc_phy_mode == PHY_MODE_10GBASER ||
	    port->sc_phy_mode == PHY_MODE_XAUI)) {
		val = mvpp2_xlg_read(port, MV_XLG_PORT_MAC_CTRL0_REG);
		val |= MV_XLG_MAC_CTRL0_PORTEN;
		val &= ~MV_XLG_MAC_CTRL0_MIBCNTDIS;
		mvpp2_xlg_write(port, MV_XLG_PORT_MAC_CTRL0_REG, val);
	} else {
		val = mvpp2_gmac_read(port, MVPP2_GMAC_CTRL_0_REG);
		val |= MVPP2_GMAC_PORT_EN_MASK;
		val |= MVPP2_GMAC_MIB_CNTR_EN_MASK;
		mvpp2_gmac_write(port, MVPP2_GMAC_CTRL_0_REG, val);
	}
}

void
mvpp2_port_disable(struct mvpp2_port *port)
{
	uint32_t val;

	if (port->sc_gop_id == 0 && (port->sc_phy_mode == PHY_MODE_10GBASER ||
	    port->sc_phy_mode == PHY_MODE_XAUI)) {
		val = mvpp2_xlg_read(port, MV_XLG_PORT_MAC_CTRL0_REG);
		val &= ~MV_XLG_MAC_CTRL0_PORTEN;
		mvpp2_xlg_write(port, MV_XLG_PORT_MAC_CTRL0_REG, val);
	}

	val = mvpp2_gmac_read(port, MVPP2_GMAC_CTRL_0_REG);
	val &= ~MVPP2_GMAC_PORT_EN_MASK;
	mvpp2_gmac_write(port, MVPP2_GMAC_CTRL_0_REG, val);
}

int
mvpp2_rxq_received(struct mvpp2_port *port, int rxq_id)
{
	uint32_t val = mvpp2_read(port->sc, MVPP2_RXQ_STATUS_REG(rxq_id));

	return val & MVPP2_RXQ_OCCUPIED_MASK;
}

void
mvpp2_rxq_status_update(struct mvpp2_port *port, int rxq_id,
    int used_count, int free_count)
{
	uint32_t val = used_count | (free_count << MVPP2_RXQ_NUM_NEW_OFFSET);
	mvpp2_write(port->sc, MVPP2_RXQ_STATUS_UPDATE_REG(rxq_id), val);
}

void
mvpp2_rxq_offset_set(struct mvpp2_port *port, int prxq, int offset)
{
	uint32_t val;

	offset = offset >> 5;
	val = mvpp2_read(port->sc, MVPP2_RXQ_CONFIG_REG(prxq));
	val &= ~MVPP2_RXQ_PACKET_OFFSET_MASK;
	val |= ((offset << MVPP2_RXQ_PACKET_OFFSET_OFFS) &
	    MVPP2_RXQ_PACKET_OFFSET_MASK);
	mvpp2_write(port->sc, MVPP2_RXQ_CONFIG_REG(prxq), val);
}

void
mvpp2_txp_max_tx_size_set(struct mvpp2_port *port)
{
	uint32_t val, size, mtu;
	int txq;

	mtu = MCLBYTES * 8;
	if (mtu > MVPP2_TXP_MTU_MAX)
		mtu = MVPP2_TXP_MTU_MAX;

	/* WA for wrong token bucket update: set MTU value = 3*real MTU value */
	mtu = 3 * mtu;

	/* indirect access to reg_valisters */
	mvpp2_write(port->sc, MVPP2_TXP_SCHED_PORT_INDEX_REG,
	    mvpp2_egress_port(port));

	/* set MTU */
	val = mvpp2_read(port->sc, MVPP2_TXP_SCHED_MTU_REG);
	val &= ~MVPP2_TXP_MTU_MAX;
	val |= mtu;
	mvpp2_write(port->sc, MVPP2_TXP_SCHED_MTU_REG, val);

	/* TXP token size and all TXqs token size must be larger that MTU */
	val = mvpp2_read(port->sc, MVPP2_TXP_SCHED_TOKEN_SIZE_REG);
	size = val & MVPP2_TXP_TOKEN_SIZE_MAX;
	if (size < mtu) {
		size = mtu;
		val &= ~MVPP2_TXP_TOKEN_SIZE_MAX;
		val |= size;
		mvpp2_write(port->sc, MVPP2_TXP_SCHED_TOKEN_SIZE_REG, val);
	}

	for (txq = 0; txq < port->sc_ntxq; txq++) {
		val = mvpp2_read(port->sc, MVPP2_TXQ_SCHED_TOKEN_SIZE_REG(txq));
		size = val & MVPP2_TXQ_TOKEN_SIZE_MAX;

		if (size < mtu) {
			size = mtu;
			val &= ~MVPP2_TXQ_TOKEN_SIZE_MAX;
			val |= size;
			mvpp2_write(port->sc, MVPP2_TXQ_SCHED_TOKEN_SIZE_REG(txq), val);
		}
	}
}

void
mvpp2_rx_pkts_coal_set(struct mvpp2_port *port, struct mvpp2_rx_queue *rxq,
    uint32_t pkts)
{
	rxq->pkts_coal =
	    pkts <= MVPP2_OCCUPIED_THRESH_MASK ?
	    pkts : MVPP2_OCCUPIED_THRESH_MASK;

	mvpp2_write(port->sc, MVPP2_RXQ_NUM_REG, rxq->id);
	mvpp2_write(port->sc, MVPP2_RXQ_THRESH_REG, rxq->pkts_coal);

}

void
mvpp2_tx_pkts_coal_set(struct mvpp2_port *port, struct mvpp2_tx_queue *txq,
    uint32_t pkts)
{
	txq->done_pkts_coal =
	    pkts <= MVPP2_TRANSMITTED_THRESH_MASK ?
	    pkts : MVPP2_TRANSMITTED_THRESH_MASK;

	mvpp2_write(port->sc, MVPP2_TXQ_NUM_REG, txq->id);
	mvpp2_write(port->sc, MVPP2_TXQ_THRESH_REG,
	    txq->done_pkts_coal << MVPP2_TRANSMITTED_THRESH_OFFSET);
}

void
mvpp2_rx_time_coal_set(struct mvpp2_port *port, struct mvpp2_rx_queue *rxq,
    uint32_t usec)
{
	uint32_t val;

	val = (port->sc->sc_tclk / (1000 * 1000)) * usec;
	mvpp2_write(port->sc, MVPP2_ISR_RX_THRESHOLD_REG(rxq->id), val);

	rxq->time_coal = usec;
}

void
mvpp2_tx_time_coal_set(struct mvpp2_port *port, uint32_t usec)
{
	uint32_t val;

	val = (port->sc->sc_tclk / (1000 * 1000)) * usec;
	mvpp2_write(port->sc, MVPP2_ISR_TX_THRESHOLD_REG(port->sc_id), val);

	port->sc_tx_time_coal = usec;
}

void
mvpp2_prs_shadow_ri_set(struct mvpp2_softc *sc, int index,
    uint32_t ri, uint32_t ri_mask)
{
	sc->sc_prs_shadow[index].ri_mask = ri_mask;
	sc->sc_prs_shadow[index].ri = ri;
}

void
mvpp2_prs_tcam_lu_set(struct mvpp2_prs_entry *pe, uint32_t lu)
{
	int enable_off = MVPP2_PRS_TCAM_EN_OFFS(MVPP2_PRS_TCAM_LU_BYTE);

	pe->tcam.byte[MVPP2_PRS_TCAM_LU_BYTE] = lu;
	pe->tcam.byte[enable_off] = MVPP2_PRS_LU_MASK;
}

void
mvpp2_prs_tcam_port_set(struct mvpp2_prs_entry *pe, uint32_t port, int add)
{
	int enable_off = MVPP2_PRS_TCAM_EN_OFFS(MVPP2_PRS_TCAM_PORT_BYTE);

	if (add)
		pe->tcam.byte[enable_off] &= ~(1 << port);
	else
		pe->tcam.byte[enable_off] |= (1 << port);
}

void
mvpp2_prs_tcam_port_map_set(struct mvpp2_prs_entry *pe, uint32_t port_mask)
{
	int enable_off = MVPP2_PRS_TCAM_EN_OFFS(MVPP2_PRS_TCAM_PORT_BYTE);
	uint8_t mask = MVPP2_PRS_PORT_MASK;

	pe->tcam.byte[MVPP2_PRS_TCAM_PORT_BYTE] = 0;
	pe->tcam.byte[enable_off] &= ~mask;
	pe->tcam.byte[enable_off] |= ~port_mask & MVPP2_PRS_PORT_MASK;
}

uint32_t
mvpp2_prs_tcam_port_map_get(struct mvpp2_prs_entry *pe)
{
	int enable_off = MVPP2_PRS_TCAM_EN_OFFS(MVPP2_PRS_TCAM_PORT_BYTE);

	return ~(pe->tcam.byte[enable_off]) & MVPP2_PRS_PORT_MASK;
}

void
mvpp2_prs_tcam_data_byte_set(struct mvpp2_prs_entry *pe, uint32_t offs,
    uint8_t byte, uint8_t enable)
{
	pe->tcam.byte[MVPP2_PRS_TCAM_DATA_BYTE(offs)] = byte;
	pe->tcam.byte[MVPP2_PRS_TCAM_DATA_BYTE_EN(offs)] = enable;
}

void
mvpp2_prs_tcam_data_byte_get(struct mvpp2_prs_entry *pe, uint32_t offs,
    uint8_t *byte, uint8_t *enable)
{
	*byte = pe->tcam.byte[MVPP2_PRS_TCAM_DATA_BYTE(offs)];
	*enable = pe->tcam.byte[MVPP2_PRS_TCAM_DATA_BYTE_EN(offs)];
}

int
mvpp2_prs_tcam_data_cmp(struct mvpp2_prs_entry *pe, int offset, uint16_t data)
{
	int byte_offset = MVPP2_PRS_TCAM_DATA_BYTE(offset);
	uint16_t tcam_data;

	tcam_data = (pe->tcam.byte[byte_offset + 1] << 8) |
	    pe->tcam.byte[byte_offset];
	return tcam_data == data;
}

void
mvpp2_prs_tcam_ai_update(struct mvpp2_prs_entry *pe, uint32_t bits, uint32_t enable)
{
	int i, ai_idx = MVPP2_PRS_TCAM_AI_BYTE;

	for (i = 0; i < MVPP2_PRS_AI_BITS; i++) {
		if (!(enable & BIT(i)))
			continue;

		if (bits & BIT(i))
			pe->tcam.byte[ai_idx] |= BIT(i);
		else
			pe->tcam.byte[ai_idx] &= ~BIT(i);
	}

	pe->tcam.byte[MVPP2_PRS_TCAM_EN_OFFS(ai_idx)] |= enable;
}

int
mvpp2_prs_tcam_ai_get(struct mvpp2_prs_entry *pe)
{
	return pe->tcam.byte[MVPP2_PRS_TCAM_AI_BYTE];
}

void
mvpp2_prs_tcam_data_word_get(struct mvpp2_prs_entry *pe, uint32_t data_offset,
    uint32_t *word, uint32_t *enable)
{
	int index, position;
	uint8_t byte, mask;

	for (index = 0; index < 4; index++) {
		position = (data_offset * sizeof(int)) + index;
		mvpp2_prs_tcam_data_byte_get(pe, position, &byte, &mask);
		((uint8_t *)word)[index] = byte;
		((uint8_t *)enable)[index] = mask;
	}
}

void
mvpp2_prs_match_etype(struct mvpp2_prs_entry *pe, uint32_t offs,
    uint16_t ether_type)
{
	mvpp2_prs_tcam_data_byte_set(pe, offs + 0, ether_type >> 8, 0xff);
	mvpp2_prs_tcam_data_byte_set(pe, offs + 1, ether_type & 0xff, 0xff);
}

void
mvpp2_prs_sram_bits_set(struct mvpp2_prs_entry *pe, uint32_t bit, uint32_t val)
{
	pe->sram.byte[bit / 8] |= (val << (bit % 8));
}

void
mvpp2_prs_sram_bits_clear(struct mvpp2_prs_entry *pe, uint32_t bit, uint32_t val)
{
	pe->sram.byte[bit / 8] &= ~(val << (bit % 8));
}

void
mvpp2_prs_sram_ri_update(struct mvpp2_prs_entry *pe, uint32_t bits, uint32_t mask)
{
	int i;

	for (i = 0; i < MVPP2_PRS_SRAM_RI_CTRL_BITS; i++) {
		if (!(mask & BIT(i)))
			continue;

		if (bits & BIT(i))
			mvpp2_prs_sram_bits_set(pe,
			    MVPP2_PRS_SRAM_RI_OFFS + i, 1);
		else
			mvpp2_prs_sram_bits_clear(pe,
			    MVPP2_PRS_SRAM_RI_OFFS + i, 1);

		mvpp2_prs_sram_bits_set(pe, MVPP2_PRS_SRAM_RI_CTRL_OFFS + i, 1);
	}
}

int
mvpp2_prs_sram_ri_get(struct mvpp2_prs_entry *pe)
{
	return pe->sram.word[MVPP2_PRS_SRAM_RI_WORD];
}

void
mvpp2_prs_sram_ai_update(struct mvpp2_prs_entry *pe, uint32_t bits, uint32_t mask)
{
	int i;

	for (i = 0; i < MVPP2_PRS_SRAM_AI_CTRL_BITS; i++) {
		if (!(mask & BIT(i)))
			continue;

		if (bits & BIT(i))
			mvpp2_prs_sram_bits_set(pe,
			    MVPP2_PRS_SRAM_AI_OFFS + i, 1);
		else
			mvpp2_prs_sram_bits_clear(pe,
			    MVPP2_PRS_SRAM_AI_OFFS + i, 1);

		mvpp2_prs_sram_bits_set(pe, MVPP2_PRS_SRAM_AI_CTRL_OFFS + i, 1);
	}
}

int
mvpp2_prs_sram_ai_get(struct mvpp2_prs_entry *pe)
{
	uint8_t bits;
	int ai_off = MVPP2_BIT_TO_BYTE(MVPP2_PRS_SRAM_AI_OFFS);
	int ai_en_off = ai_off + 1;
	int ai_shift = MVPP2_PRS_SRAM_AI_OFFS % 8;

	bits = (pe->sram.byte[ai_off] >> ai_shift) |
	    (pe->sram.byte[ai_en_off] << (8 - ai_shift));

	return bits;
}

void
mvpp2_prs_sram_shift_set(struct mvpp2_prs_entry *pe, int shift, uint32_t op)
{
	if (shift < 0) {
		mvpp2_prs_sram_bits_set(pe, MVPP2_PRS_SRAM_SHIFT_SIGN_BIT, 1);
		shift = -shift;
	} else {
		mvpp2_prs_sram_bits_clear(pe, MVPP2_PRS_SRAM_SHIFT_SIGN_BIT, 1);
	}

	pe->sram.byte[MVPP2_BIT_TO_BYTE(MVPP2_PRS_SRAM_SHIFT_OFFS)] |=
	    shift & MVPP2_PRS_SRAM_SHIFT_MASK;
	mvpp2_prs_sram_bits_clear(pe, MVPP2_PRS_SRAM_OP_SEL_SHIFT_OFFS,
	    MVPP2_PRS_SRAM_OP_SEL_SHIFT_MASK);
	mvpp2_prs_sram_bits_set(pe, MVPP2_PRS_SRAM_OP_SEL_SHIFT_OFFS, op);
	mvpp2_prs_sram_bits_clear(pe, MVPP2_PRS_SRAM_OP_SEL_BASE_OFFS, 1);
}

void
mvpp2_prs_sram_offset_set(struct mvpp2_prs_entry *pe, uint32_t type, int offset,
    uint32_t op)
{
	uint8_t udf_byte, udf_byte_offset;
	uint8_t op_sel_udf_byte, op_sel_udf_byte_offset;

	udf_byte = MVPP2_BIT_TO_BYTE(MVPP2_PRS_SRAM_UDF_OFFS +
	    MVPP2_PRS_SRAM_UDF_BITS);
	udf_byte_offset = (8 - (MVPP2_PRS_SRAM_UDF_OFFS % 8));
	op_sel_udf_byte = MVPP2_BIT_TO_BYTE(MVPP2_PRS_SRAM_OP_SEL_UDF_OFFS +
	    MVPP2_PRS_SRAM_OP_SEL_UDF_BITS);
	op_sel_udf_byte_offset = (8 - (MVPP2_PRS_SRAM_OP_SEL_UDF_OFFS % 8));

	if (offset < 0) {
		mvpp2_prs_sram_bits_set(pe, MVPP2_PRS_SRAM_UDF_SIGN_BIT, 1);
		offset = -offset;
	} else {
		mvpp2_prs_sram_bits_clear(pe, MVPP2_PRS_SRAM_UDF_SIGN_BIT, 1);
	}

	mvpp2_prs_sram_bits_clear(pe, MVPP2_PRS_SRAM_UDF_OFFS,
	    MVPP2_PRS_SRAM_UDF_MASK);
	mvpp2_prs_sram_bits_set(pe, MVPP2_PRS_SRAM_UDF_OFFS, offset);
	pe->sram.byte[udf_byte] &= ~(MVPP2_PRS_SRAM_UDF_MASK >> udf_byte_offset);
	pe->sram.byte[udf_byte] |= (offset >> udf_byte_offset);
	mvpp2_prs_sram_bits_clear(pe, MVPP2_PRS_SRAM_UDF_TYPE_OFFS,
	    MVPP2_PRS_SRAM_UDF_TYPE_MASK);
	mvpp2_prs_sram_bits_set(pe, MVPP2_PRS_SRAM_UDF_TYPE_OFFS, type);
	mvpp2_prs_sram_bits_clear(pe, MVPP2_PRS_SRAM_OP_SEL_UDF_OFFS,
	    MVPP2_PRS_SRAM_OP_SEL_UDF_MASK);
	mvpp2_prs_sram_bits_set(pe, MVPP2_PRS_SRAM_OP_SEL_UDF_OFFS, op);
	pe->sram.byte[op_sel_udf_byte] &= ~(MVPP2_PRS_SRAM_OP_SEL_UDF_MASK >>
	    op_sel_udf_byte_offset);
	pe->sram.byte[op_sel_udf_byte] |= (op >> op_sel_udf_byte_offset);
	mvpp2_prs_sram_bits_clear(pe, MVPP2_PRS_SRAM_OP_SEL_BASE_OFFS, 1);
}

void
mvpp2_prs_sram_next_lu_set(struct mvpp2_prs_entry *pe, uint32_t lu)
{
	int sram_next_off = MVPP2_PRS_SRAM_NEXT_LU_OFFS;

	mvpp2_prs_sram_bits_clear(pe, sram_next_off, MVPP2_PRS_SRAM_NEXT_LU_MASK);
	mvpp2_prs_sram_bits_set(pe, sram_next_off, lu);
}

void
mvpp2_prs_shadow_set(struct mvpp2_softc *sc, int index, uint32_t lu)
{
	sc->sc_prs_shadow[index].valid = 1;
	sc->sc_prs_shadow[index].lu = lu;
}

int
mvpp2_prs_hw_write(struct mvpp2_softc *sc, struct mvpp2_prs_entry *pe)
{
	int i;

	if (pe->index > MVPP2_PRS_TCAM_SRAM_SIZE - 1)
		return EINVAL;

	pe->tcam.word[MVPP2_PRS_TCAM_INV_WORD] &= ~MVPP2_PRS_TCAM_INV_MASK;
	mvpp2_write(sc, MVPP2_PRS_TCAM_IDX_REG, pe->index);
	for (i = 0; i < MVPP2_PRS_TCAM_WORDS; i++)
		mvpp2_write(sc, MVPP2_PRS_TCAM_DATA_REG(i), pe->tcam.word[i]);
	mvpp2_write(sc, MVPP2_PRS_SRAM_IDX_REG, pe->index);
	for (i = 0; i < MVPP2_PRS_SRAM_WORDS; i++)
		mvpp2_write(sc, MVPP2_PRS_SRAM_DATA_REG(i), pe->sram.word[i]);

	return 0;
}

int
mvpp2_prs_hw_read(struct mvpp2_softc *sc, struct mvpp2_prs_entry *pe, int tid)
{
	int i;

	if (tid > MVPP2_PRS_TCAM_SRAM_SIZE - 1)
		return EINVAL;

	memset(pe, 0, sizeof(*pe));
	pe->index = tid;

	mvpp2_write(sc, MVPP2_PRS_TCAM_IDX_REG, pe->index);
	pe->tcam.word[MVPP2_PRS_TCAM_INV_WORD] =
	    mvpp2_read(sc, MVPP2_PRS_TCAM_DATA_REG(MVPP2_PRS_TCAM_INV_WORD));
	if (pe->tcam.word[MVPP2_PRS_TCAM_INV_WORD] & MVPP2_PRS_TCAM_INV_MASK)
		return EINVAL;
	for (i = 0; i < MVPP2_PRS_TCAM_WORDS; i++)
		pe->tcam.word[i] =
		    mvpp2_read(sc, MVPP2_PRS_TCAM_DATA_REG(i));

	mvpp2_write(sc, MVPP2_PRS_SRAM_IDX_REG, pe->index);
	for (i = 0; i < MVPP2_PRS_SRAM_WORDS; i++)
		pe->sram.word[i] =
		    mvpp2_read(sc, MVPP2_PRS_SRAM_DATA_REG(i));

	return 0;
}

int
mvpp2_prs_flow_find(struct mvpp2_softc *sc, int flow)
{
	struct mvpp2_prs_entry pe;
	uint8_t bits;
	int tid;

	for (tid = MVPP2_PRS_TCAM_SRAM_SIZE - 1; tid >= 0; tid--) {
		if (!sc->sc_prs_shadow[tid].valid ||
		    sc->sc_prs_shadow[tid].lu != MVPP2_PRS_LU_FLOWS)
			continue;

		mvpp2_prs_hw_read(sc, &pe, tid);
		bits = mvpp2_prs_sram_ai_get(&pe);

		if ((bits & MVPP2_PRS_FLOW_ID_MASK) == flow)
			return tid;
	}

	return -1;
}

int
mvpp2_prs_tcam_first_free(struct mvpp2_softc *sc, uint8_t start, uint8_t end)
{
	uint8_t tmp;
	int tid;

	if (start > end) {
		tmp = end;
		end = start;
		start = tmp;
	}

	for (tid = start; tid <= end; tid++) {
		if (!sc->sc_prs_shadow[tid].valid)
			return tid;
	}

	return -1;
}

void
mvpp2_prs_mac_drop_all_set(struct mvpp2_softc *sc, uint32_t port, int add)
{
	struct mvpp2_prs_entry pe;

	if (sc->sc_prs_shadow[MVPP2_PE_DROP_ALL].valid) {
		mvpp2_prs_hw_read(sc, &pe, MVPP2_PE_DROP_ALL);
	} else {
		memset(&pe, 0, sizeof(pe));
		mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_MAC);
		pe.index = MVPP2_PE_DROP_ALL;
		mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_DROP_MASK,
		    MVPP2_PRS_RI_DROP_MASK);
		mvpp2_prs_sram_bits_set(&pe, MVPP2_PRS_SRAM_LU_GEN_BIT, 1);
		mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_FLOWS);
		mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_MAC);
		mvpp2_prs_tcam_port_map_set(&pe, 0);
	}

	mvpp2_prs_tcam_port_set(&pe, port, add);
	mvpp2_prs_hw_write(sc, &pe);
}

void
mvpp2_prs_mac_promisc_set(struct mvpp2_softc *sc, uint32_t port, int l2_cast,
    int add)
{
	struct mvpp2_prs_entry pe;
	uint8_t cast_match;
	uint32_t ri;
	int tid;

	if (l2_cast == MVPP2_PRS_L2_UNI_CAST) {
		cast_match = MVPP2_PRS_UCAST_VAL;
		tid = MVPP2_PE_MAC_UC_PROMISCUOUS;
		ri = MVPP2_PRS_RI_L2_UCAST;
	} else {
		cast_match = MVPP2_PRS_MCAST_VAL;
		tid = MVPP2_PE_MAC_MC_PROMISCUOUS;
		ri = MVPP2_PRS_RI_L2_MCAST;
	}

	if (sc->sc_prs_shadow[tid].valid) {
		mvpp2_prs_hw_read(sc, &pe, tid);
	} else {
		memset(&pe, 0, sizeof(pe));
		mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_MAC);
		pe.index = tid;
		mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_DSA);
		mvpp2_prs_sram_ri_update(&pe, ri, MVPP2_PRS_RI_L2_CAST_MASK);
		mvpp2_prs_tcam_data_byte_set(&pe, 0, cast_match,
		    MVPP2_PRS_CAST_MASK);
		mvpp2_prs_sram_shift_set(&pe, 2 * ETHER_ADDR_LEN,
		    MVPP2_PRS_SRAM_OP_SEL_SHIFT_ADD);
		mvpp2_prs_tcam_port_map_set(&pe, 0);
		mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_MAC);
	}

	mvpp2_prs_tcam_port_set(&pe, port, add);
	mvpp2_prs_hw_write(sc, &pe);
}

void
mvpp2_prs_dsa_tag_set(struct mvpp2_softc *sc, uint32_t port, int add,
    int tagged, int extend)
{
	struct mvpp2_prs_entry pe;
	int32_t tid, shift;

	if (extend) {
		tid = tagged ? MVPP2_PE_EDSA_TAGGED : MVPP2_PE_EDSA_UNTAGGED;
		shift = 8;
	} else {
		tid = tagged ? MVPP2_PE_DSA_TAGGED : MVPP2_PE_DSA_UNTAGGED;
		shift = 4;
	}

	if (sc->sc_prs_shadow[tid].valid) {
		mvpp2_prs_hw_read(sc, &pe, tid);
	} else {
		memset(&pe, 0, sizeof(pe));
		mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_DSA);
		pe.index = tid;
		mvpp2_prs_sram_shift_set(&pe, shift,
		    MVPP2_PRS_SRAM_OP_SEL_SHIFT_ADD);
		mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_DSA);
		if (tagged) {
			mvpp2_prs_tcam_data_byte_set(&pe, 0,
			    MVPP2_PRS_TCAM_DSA_TAGGED_BIT,
			    MVPP2_PRS_TCAM_DSA_TAGGED_BIT);
			mvpp2_prs_sram_ai_update(&pe, 0, MVPP2_PRS_SRAM_AI_MASK);
			mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_VLAN);
		} else {
			mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_VLAN_NONE,
			    MVPP2_PRS_RI_VLAN_MASK);
			mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_L2);
		}
		mvpp2_prs_tcam_port_map_set(&pe, 0);
	}

	mvpp2_prs_tcam_port_set(&pe, port, add);
	mvpp2_prs_hw_write(sc, &pe);
}

void
mvpp2_prs_dsa_tag_ethertype_set(struct mvpp2_softc *sc, uint32_t port,
    int add, int tagged, int extend)
{
	struct mvpp2_prs_entry pe;
	int32_t tid, shift, port_mask;

	if (extend) {
		tid = tagged ? MVPP2_PE_EDSA_TAGGED : MVPP2_PE_EDSA_UNTAGGED;
		port_mask = 0;
		shift = 8;
	} else {
		tid = tagged ? MVPP2_PE_DSA_TAGGED : MVPP2_PE_DSA_UNTAGGED;
		port_mask = MVPP2_PRS_PORT_MASK;
		shift = 4;
	}

	if (sc->sc_prs_shadow[tid].valid) {
		mvpp2_prs_hw_read(sc, &pe, tid);
	} else {
		memset(&pe, 0, sizeof(pe));
		mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_DSA);
		pe.index = tid;
		mvpp2_prs_match_etype(&pe, 0, 0xdada);
		mvpp2_prs_match_etype(&pe, 2, 0);
		mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_DSA_MASK,
		    MVPP2_PRS_RI_DSA_MASK);
		mvpp2_prs_sram_shift_set(&pe, 2 * ETHER_ADDR_LEN + shift,
		    MVPP2_PRS_SRAM_OP_SEL_SHIFT_ADD);
		mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_DSA);
		if (tagged) {
			mvpp2_prs_tcam_data_byte_set(&pe,
			    MVPP2_ETH_TYPE_LEN + 2 + 3,
			    MVPP2_PRS_TCAM_DSA_TAGGED_BIT,
			    MVPP2_PRS_TCAM_DSA_TAGGED_BIT);
			mvpp2_prs_sram_ai_update(&pe, 0, MVPP2_PRS_SRAM_AI_MASK);
			mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_VLAN);
		} else {
			mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_VLAN_NONE,
			    MVPP2_PRS_RI_VLAN_MASK);
			mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_L2);
		}
		mvpp2_prs_tcam_port_map_set(&pe, port_mask);
	}

	mvpp2_prs_tcam_port_set(&pe, port, add);
	mvpp2_prs_hw_write(sc, &pe);
}

struct mvpp2_prs_entry *
mvpp2_prs_vlan_find(struct mvpp2_softc *sc, uint16_t tpid, int ai)
{
	struct mvpp2_prs_entry *pe;
	uint32_t ri_bits, ai_bits;
	int match, tid;

	pe = malloc(sizeof(*pe), M_TEMP, M_NOWAIT);
	if (pe == NULL)
		return NULL;

	mvpp2_prs_tcam_lu_set(pe, MVPP2_PRS_LU_VLAN);

	for (tid = MVPP2_PE_FIRST_FREE_TID; tid <= MVPP2_PE_LAST_FREE_TID; tid++) {
		if (!sc->sc_prs_shadow[tid].valid ||
		    sc->sc_prs_shadow[tid].lu != MVPP2_PRS_LU_VLAN)
			continue;
		mvpp2_prs_hw_read(sc, pe, tid);
		match = mvpp2_prs_tcam_data_cmp(pe, 0, swap16(tpid));
		if (!match)
			continue;
		ri_bits = mvpp2_prs_sram_ri_get(pe);
		ri_bits &= MVPP2_PRS_RI_VLAN_MASK;
		ai_bits = mvpp2_prs_tcam_ai_get(pe);
		ai_bits &= ~MVPP2_PRS_DBL_VLAN_AI_BIT;
		if (ai != ai_bits)
			continue;
		if (ri_bits == MVPP2_PRS_RI_VLAN_SINGLE ||
		    ri_bits == MVPP2_PRS_RI_VLAN_TRIPLE)
			return pe;
	}

	free(pe, M_TEMP, sizeof(*pe));
	return NULL;
}

int
mvpp2_prs_vlan_add(struct mvpp2_softc *sc, uint16_t tpid, int ai, uint32_t port_map)
{
	struct mvpp2_prs_entry *pe;
	uint32_t ri_bits;
	int tid_aux, tid;
	int ret = 0;

	pe = mvpp2_prs_vlan_find(sc, tpid, ai);
	if (pe == NULL) {
		tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_LAST_FREE_TID,
		    MVPP2_PE_FIRST_FREE_TID);
		if (tid < 0)
			return tid;

		pe = malloc(sizeof(*pe), M_TEMP, M_NOWAIT);
		if (pe == NULL)
			return ENOMEM;

		/* get last double vlan tid */
		for (tid_aux = MVPP2_PE_LAST_FREE_TID;
		    tid_aux >= MVPP2_PE_FIRST_FREE_TID; tid_aux--) {
			if (!sc->sc_prs_shadow[tid_aux].valid ||
			    sc->sc_prs_shadow[tid_aux].lu != MVPP2_PRS_LU_VLAN)
				continue;
			mvpp2_prs_hw_read(sc, pe, tid_aux);
			ri_bits = mvpp2_prs_sram_ri_get(pe);
			if ((ri_bits & MVPP2_PRS_RI_VLAN_MASK) ==
			    MVPP2_PRS_RI_VLAN_DOUBLE)
				break;
		}

		if (tid <= tid_aux) {
			ret = EINVAL;
			goto error;
		}

		memset(pe, 0, sizeof(*pe));
		mvpp2_prs_tcam_lu_set(pe, MVPP2_PRS_LU_VLAN);
		pe->index = tid;
		mvpp2_prs_match_etype(pe, 0, tpid);
		mvpp2_prs_sram_next_lu_set(pe, MVPP2_PRS_LU_L2);
		mvpp2_prs_sram_shift_set(pe, MVPP2_VLAN_TAG_LEN,
				   MVPP2_PRS_SRAM_OP_SEL_SHIFT_ADD);
		mvpp2_prs_sram_ai_update(pe, 0, MVPP2_PRS_SRAM_AI_MASK);
		if (ai == MVPP2_PRS_SINGLE_VLAN_AI) {
			mvpp2_prs_sram_ri_update(pe, MVPP2_PRS_RI_VLAN_SINGLE,
			    MVPP2_PRS_RI_VLAN_MASK);
		} else {
			ai |= MVPP2_PRS_DBL_VLAN_AI_BIT;
			mvpp2_prs_sram_ri_update(pe, MVPP2_PRS_RI_VLAN_TRIPLE,
			    MVPP2_PRS_RI_VLAN_MASK);
		}
		mvpp2_prs_tcam_ai_update(pe, ai, MVPP2_PRS_SRAM_AI_MASK);
		mvpp2_prs_shadow_set(sc, pe->index, MVPP2_PRS_LU_VLAN);
	}

	mvpp2_prs_tcam_port_map_set(pe, port_map);
	mvpp2_prs_hw_write(sc, pe);

error:
	free(pe, M_TEMP, sizeof(*pe));
	return ret;
}

int
mvpp2_prs_double_vlan_ai_free_get(struct mvpp2_softc *sc)
{
	int i;

	for (i = 1; i < MVPP2_PRS_DBL_VLANS_MAX; i++)
		if (!sc->sc_prs_double_vlans[i])
			return i;

	return -1;
}

struct mvpp2_prs_entry *
mvpp2_prs_double_vlan_find(struct mvpp2_softc *sc, uint16_t tpid1, uint16_t tpid2)
{
	struct mvpp2_prs_entry *pe;
	uint32_t ri_mask;
	int match, tid;

	pe = malloc(sizeof(*pe), M_TEMP, M_NOWAIT);
	if (pe == NULL)
		return NULL;

	mvpp2_prs_tcam_lu_set(pe, MVPP2_PRS_LU_VLAN);

	for (tid = MVPP2_PE_FIRST_FREE_TID; tid <= MVPP2_PE_LAST_FREE_TID; tid++) {
		if (!sc->sc_prs_shadow[tid].valid ||
		    sc->sc_prs_shadow[tid].lu != MVPP2_PRS_LU_VLAN)
			continue;

		mvpp2_prs_hw_read(sc, pe, tid);
		match = mvpp2_prs_tcam_data_cmp(pe, 0, swap16(tpid1)) &&
		    mvpp2_prs_tcam_data_cmp(pe, 4, swap16(tpid2));
		if (!match)
			continue;
		ri_mask = mvpp2_prs_sram_ri_get(pe) & MVPP2_PRS_RI_VLAN_MASK;
		if (ri_mask == MVPP2_PRS_RI_VLAN_DOUBLE)
			return pe;
	}

	free(pe, M_TEMP, sizeof(*pe));
	return NULL;
}

int
mvpp2_prs_double_vlan_add(struct mvpp2_softc *sc, uint16_t tpid1, uint16_t tpid2,
    uint32_t port_map)
{
	struct mvpp2_prs_entry *pe;
	int tid_aux, tid, ai, ret = 0;
	uint32_t ri_bits;

	pe = mvpp2_prs_double_vlan_find(sc, tpid1, tpid2);
	if (pe == NULL) {
		tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_FIRST_FREE_TID,
		    MVPP2_PE_LAST_FREE_TID);
		if (tid < 0)
			return tid;

		pe = malloc(sizeof(*pe), M_TEMP, M_NOWAIT);
		if (pe == NULL)
			return ENOMEM;

		ai = mvpp2_prs_double_vlan_ai_free_get(sc);
		if (ai < 0) {
			ret = ai;
			goto error;
		}

		for (tid_aux = MVPP2_PE_FIRST_FREE_TID;
		    tid_aux <= MVPP2_PE_LAST_FREE_TID; tid_aux++) {
			if (!sc->sc_prs_shadow[tid_aux].valid ||
			    sc->sc_prs_shadow[tid_aux].lu != MVPP2_PRS_LU_VLAN)
				continue;
			mvpp2_prs_hw_read(sc, pe, tid_aux);
			ri_bits = mvpp2_prs_sram_ri_get(pe);
			ri_bits &= MVPP2_PRS_RI_VLAN_MASK;
			if (ri_bits == MVPP2_PRS_RI_VLAN_SINGLE ||
			    ri_bits == MVPP2_PRS_RI_VLAN_TRIPLE)
				break;
		}

		if (tid >= tid_aux) {
			ret = ERANGE;
			goto error;
		}

		memset(pe, 0, sizeof(*pe));
		mvpp2_prs_tcam_lu_set(pe, MVPP2_PRS_LU_VLAN);
		pe->index = tid;
		sc->sc_prs_double_vlans[ai] = 1;
		mvpp2_prs_match_etype(pe, 0, tpid1);
		mvpp2_prs_match_etype(pe, 4, tpid2);
		mvpp2_prs_sram_next_lu_set(pe, MVPP2_PRS_LU_VLAN);
		mvpp2_prs_sram_shift_set(pe, 2 * MVPP2_VLAN_TAG_LEN,
		    MVPP2_PRS_SRAM_OP_SEL_SHIFT_ADD);
		mvpp2_prs_sram_ri_update(pe, MVPP2_PRS_RI_VLAN_DOUBLE,
		    MVPP2_PRS_RI_VLAN_MASK);
		mvpp2_prs_sram_ai_update(pe, ai | MVPP2_PRS_DBL_VLAN_AI_BIT,
		    MVPP2_PRS_SRAM_AI_MASK);
		mvpp2_prs_shadow_set(sc, pe->index, MVPP2_PRS_LU_VLAN);
	}

	mvpp2_prs_tcam_port_map_set(pe, port_map);
	mvpp2_prs_hw_write(sc, pe);

error:
	free(pe, M_TEMP, sizeof(*pe));
	return ret;
}

int
mvpp2_prs_ip4_proto(struct mvpp2_softc *sc, uint16_t proto, uint32_t ri,
    uint32_t ri_mask)
{
	struct mvpp2_prs_entry pe;
	int tid;

	if ((proto != IPPROTO_TCP) && (proto != IPPROTO_UDP) &&
	    (proto != IPPROTO_IGMP))
		return EINVAL;

	tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_FIRST_FREE_TID,
	    MVPP2_PE_LAST_FREE_TID);
	if (tid < 0)
		return tid;

	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_IP4);
	pe.index = tid;
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_IP4);
	mvpp2_prs_sram_shift_set(&pe, 12, MVPP2_PRS_SRAM_OP_SEL_SHIFT_ADD);
	mvpp2_prs_sram_offset_set(&pe, MVPP2_PRS_SRAM_UDF_TYPE_L4,
	    sizeof(struct ip) - 4, MVPP2_PRS_SRAM_OP_SEL_UDF_ADD);
	mvpp2_prs_sram_ai_update(&pe, MVPP2_PRS_IPV4_DIP_AI_BIT,
	    MVPP2_PRS_IPV4_DIP_AI_BIT);
	mvpp2_prs_sram_ri_update(&pe, ri, ri_mask | MVPP2_PRS_RI_IP_FRAG_MASK);
	mvpp2_prs_tcam_data_byte_set(&pe, 2, 0x00, MVPP2_PRS_TCAM_PROTO_MASK_L);
	mvpp2_prs_tcam_data_byte_set(&pe, 3, 0x00, MVPP2_PRS_TCAM_PROTO_MASK);
	mvpp2_prs_tcam_data_byte_set(&pe, 5, proto, MVPP2_PRS_TCAM_PROTO_MASK);
	mvpp2_prs_tcam_ai_update(&pe, 0, MVPP2_PRS_IPV4_DIP_AI_BIT);
	mvpp2_prs_tcam_port_map_set(&pe, MVPP2_PRS_PORT_MASK);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_IP4);
	mvpp2_prs_hw_write(sc, &pe);

	tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_FIRST_FREE_TID,
	    MVPP2_PE_LAST_FREE_TID);
	if (tid < 0)
		return tid;

	pe.index = tid;
	pe.sram.word[MVPP2_PRS_SRAM_RI_WORD] = 0x0;
	pe.sram.word[MVPP2_PRS_SRAM_RI_CTRL_WORD] = 0x0;
	mvpp2_prs_sram_ri_update(&pe, ri, ri_mask);
	mvpp2_prs_sram_ri_update(&pe, ri | MVPP2_PRS_RI_IP_FRAG_MASK,
	    ri_mask | MVPP2_PRS_RI_IP_FRAG_MASK);
	mvpp2_prs_tcam_data_byte_set(&pe, 2, 0x00, 0x0);
	mvpp2_prs_tcam_data_byte_set(&pe, 3, 0x00, 0x0);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_IP4);
	mvpp2_prs_hw_write(sc, &pe);

	return 0;
}

int
mvpp2_prs_ip4_cast(struct mvpp2_softc *sc, uint16_t l3_cast)
{
	struct mvpp2_prs_entry pe;
	int mask, tid;

	tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_FIRST_FREE_TID,
	    MVPP2_PE_LAST_FREE_TID);
	if (tid < 0)
		return tid;

	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_IP4);
	pe.index = tid;

	switch (l3_cast) {
	case MVPP2_PRS_L3_MULTI_CAST:
		mvpp2_prs_tcam_data_byte_set(&pe, 0, MVPP2_PRS_IPV4_MC,
		    MVPP2_PRS_IPV4_MC_MASK);
		mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_L3_MCAST,
		    MVPP2_PRS_RI_L3_ADDR_MASK);
		break;
	case  MVPP2_PRS_L3_BROAD_CAST:
		mask = MVPP2_PRS_IPV4_BC_MASK;
		mvpp2_prs_tcam_data_byte_set(&pe, 0, mask, mask);
		mvpp2_prs_tcam_data_byte_set(&pe, 1, mask, mask);
		mvpp2_prs_tcam_data_byte_set(&pe, 2, mask, mask);
		mvpp2_prs_tcam_data_byte_set(&pe, 3, mask, mask);
		mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_L3_BCAST,
		    MVPP2_PRS_RI_L3_ADDR_MASK);
		break;
	default:
		return EINVAL;
	}

	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_FLOWS);
	mvpp2_prs_sram_bits_set(&pe, MVPP2_PRS_SRAM_LU_GEN_BIT, 1);
	mvpp2_prs_tcam_ai_update(&pe, MVPP2_PRS_IPV4_DIP_AI_BIT,
	    MVPP2_PRS_IPV4_DIP_AI_BIT);
	mvpp2_prs_tcam_port_map_set(&pe, MVPP2_PRS_PORT_MASK);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_IP4);
	mvpp2_prs_hw_write(sc, &pe);

	return 0;
}

int
mvpp2_prs_ip6_proto(struct mvpp2_softc *sc, uint16_t proto, uint32_t ri,
    uint32_t ri_mask)
{
	struct mvpp2_prs_entry pe;
	int tid;

	if ((proto != IPPROTO_TCP) && (proto != IPPROTO_UDP) &&
	    (proto != IPPROTO_ICMPV6) && (proto != IPPROTO_IPIP))
		return EINVAL;

	tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_FIRST_FREE_TID,
	    MVPP2_PE_LAST_FREE_TID);
	if (tid < 0)
		return tid;

	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_IP6);
	pe.index = tid;
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_FLOWS);
	mvpp2_prs_sram_bits_set(&pe, MVPP2_PRS_SRAM_LU_GEN_BIT, 1);
	mvpp2_prs_sram_ri_update(&pe, ri, ri_mask);
	mvpp2_prs_sram_offset_set(&pe, MVPP2_PRS_SRAM_UDF_TYPE_L4,
	    sizeof(struct ip6_hdr) - 6, MVPP2_PRS_SRAM_OP_SEL_UDF_ADD);
	mvpp2_prs_tcam_data_byte_set(&pe, 0, proto, MVPP2_PRS_TCAM_PROTO_MASK);
	mvpp2_prs_tcam_ai_update(&pe, MVPP2_PRS_IPV6_NO_EXT_AI_BIT,
	    MVPP2_PRS_IPV6_NO_EXT_AI_BIT);
	mvpp2_prs_tcam_port_map_set(&pe, MVPP2_PRS_PORT_MASK);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_IP6);
	mvpp2_prs_hw_write(sc, &pe);

	return 0;
}

int
mvpp2_prs_ip6_cast(struct mvpp2_softc *sc, uint16_t l3_cast)
{
	struct mvpp2_prs_entry pe;
	int tid;

	if (l3_cast != MVPP2_PRS_L3_MULTI_CAST)
		return EINVAL;

	tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_FIRST_FREE_TID,
	    MVPP2_PE_LAST_FREE_TID);
	if (tid < 0)
		return tid;

	memset(&pe, 0, sizeof(pe));
	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_IP6);
	pe.index = tid;
	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_IP6);
	mvpp2_prs_sram_ri_update(&pe, MVPP2_PRS_RI_L3_MCAST,
	    MVPP2_PRS_RI_L3_ADDR_MASK);
	mvpp2_prs_sram_ai_update(&pe, MVPP2_PRS_IPV6_NO_EXT_AI_BIT,
	    MVPP2_PRS_IPV6_NO_EXT_AI_BIT);
	mvpp2_prs_sram_shift_set(&pe, -18, MVPP2_PRS_SRAM_OP_SEL_SHIFT_ADD);
	mvpp2_prs_tcam_data_byte_set(&pe, 0, MVPP2_PRS_IPV6_MC,
	    MVPP2_PRS_IPV6_MC_MASK);
	mvpp2_prs_tcam_ai_update(&pe, 0, MVPP2_PRS_IPV6_NO_EXT_AI_BIT);
	mvpp2_prs_tcam_port_map_set(&pe, MVPP2_PRS_PORT_MASK);
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_IP6);
	mvpp2_prs_hw_write(sc, &pe);

	return 0;
}

int
mvpp2_prs_mac_range_equals(struct mvpp2_prs_entry *pe, const uint8_t *da,
    uint8_t *mask)
{
	uint8_t tcam_byte, tcam_mask;
	int index;

	for (index = 0; index < ETHER_ADDR_LEN; index++) {
		mvpp2_prs_tcam_data_byte_get(pe, index, &tcam_byte,
		    &tcam_mask);
		if (tcam_mask != mask[index])
			return 0;
		if ((tcam_mask & tcam_byte) != (da[index] & mask[index]))
			return 0;
	}

	return 1;
}

int
mvpp2_prs_mac_da_range_find(struct mvpp2_softc *sc, int pmap, const uint8_t *da,
    uint8_t *mask, int udf_type)
{
	struct mvpp2_prs_entry pe;
	int tid;

	for (tid = MVPP2_PE_MAC_RANGE_START; tid <= MVPP2_PE_MAC_RANGE_END;
	    tid++) {
		uint32_t entry_pmap;

		if (!sc->sc_prs_shadow[tid].valid ||
		    (sc->sc_prs_shadow[tid].lu != MVPP2_PRS_LU_MAC) ||
		    (sc->sc_prs_shadow[tid].udf != udf_type))
			continue;

		mvpp2_prs_hw_read(sc, &pe, tid);
		entry_pmap = mvpp2_prs_tcam_port_map_get(&pe);
		if (mvpp2_prs_mac_range_equals(&pe, da, mask) &&
		    entry_pmap == pmap)
			return tid;
	}

	return -1;
}

int
mvpp2_prs_mac_da_accept(struct mvpp2_port *port, const uint8_t *da, int add)
{
	struct mvpp2_softc *sc = port->sc;
	struct mvpp2_prs_entry pe;
	uint32_t pmap, len, ri;
	uint8_t mask[ETHER_ADDR_LEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	int tid;

	memset(&pe, 0, sizeof(pe));

	tid = mvpp2_prs_mac_da_range_find(sc, BIT(port->sc_id), da, mask,
	    MVPP2_PRS_UDF_MAC_DEF);
	if (tid < 0) {
		if (!add)
			return 0;

		tid = mvpp2_prs_tcam_first_free(sc, MVPP2_PE_MAC_RANGE_START,
		    MVPP2_PE_MAC_RANGE_END);
		if (tid < 0)
			return tid;

		pe.index = tid;
		mvpp2_prs_tcam_port_map_set(&pe, 0);
	} else {
		mvpp2_prs_hw_read(sc, &pe, tid);
	}

	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_MAC);

	mvpp2_prs_tcam_port_set(&pe, port->sc_id, add);

	/* invalidate the entry if no ports are left enabled */
	pmap = mvpp2_prs_tcam_port_map_get(&pe);
	if (pmap == 0) {
		if (add)
			return -1;
		mvpp2_prs_hw_inv(sc, pe.index);
		sc->sc_prs_shadow[pe.index].valid = 0;
		return 0;
	}

	mvpp2_prs_sram_next_lu_set(&pe, MVPP2_PRS_LU_DSA);

	len = ETHER_ADDR_LEN;
	while (len--)
		mvpp2_prs_tcam_data_byte_set(&pe, len, da[len], 0xff);

	if (ETHER_IS_BROADCAST(da))
		ri = MVPP2_PRS_RI_L2_BCAST;
	else if (ETHER_IS_MULTICAST(da))
		ri = MVPP2_PRS_RI_L2_MCAST;
	else
		ri = MVPP2_PRS_RI_L2_UCAST | MVPP2_PRS_RI_MAC_ME_MASK;

	mvpp2_prs_sram_ri_update(&pe, ri, MVPP2_PRS_RI_L2_CAST_MASK |
	    MVPP2_PRS_RI_MAC_ME_MASK);
	mvpp2_prs_shadow_ri_set(sc, pe.index, ri, MVPP2_PRS_RI_L2_CAST_MASK |
	    MVPP2_PRS_RI_MAC_ME_MASK);
	mvpp2_prs_sram_shift_set(&pe, 2 * ETHER_ADDR_LEN,
	    MVPP2_PRS_SRAM_OP_SEL_SHIFT_ADD);
	sc->sc_prs_shadow[pe.index].udf = MVPP2_PRS_UDF_MAC_DEF;
	mvpp2_prs_shadow_set(sc, pe.index, MVPP2_PRS_LU_MAC);
	mvpp2_prs_hw_write(sc, &pe);

	return 0;
}

void
mvpp2_prs_mac_del_all(struct mvpp2_port *port)
{
	struct mvpp2_softc *sc = port->sc;
	struct mvpp2_prs_entry pe;
	uint32_t pmap;
	int index, tid;

	for (tid = MVPP2_PE_MAC_RANGE_START; tid <= MVPP2_PE_MAC_RANGE_END;
	    tid++) {
		uint8_t da[ETHER_ADDR_LEN], da_mask[ETHER_ADDR_LEN];

		if (!sc->sc_prs_shadow[tid].valid ||
		    (sc->sc_prs_shadow[tid].lu != MVPP2_PRS_LU_MAC) ||
		    (sc->sc_prs_shadow[tid].udf != MVPP2_PRS_UDF_MAC_DEF))
			continue;

		mvpp2_prs_hw_read(sc, &pe, tid);
		pmap = mvpp2_prs_tcam_port_map_get(&pe);

		if (!(pmap & (1 << port->sc_id)))
			continue;

		for (index = 0; index < ETHER_ADDR_LEN; index++)
			mvpp2_prs_tcam_data_byte_get(&pe, index, &da[index],
			    &da_mask[index]);

		if (ETHER_IS_BROADCAST(da) || ETHER_IS_EQ(da, port->sc_lladdr))
			continue;

		mvpp2_prs_mac_da_accept(port, da, 0);
	}
}

int
mvpp2_prs_tag_mode_set(struct mvpp2_softc *sc, int port_id, int type)
{
	switch (type) {
	case MVPP2_TAG_TYPE_EDSA:
		mvpp2_prs_dsa_tag_set(sc, port_id, 1, MVPP2_PRS_TAGGED,
		    MVPP2_PRS_EDSA);
		mvpp2_prs_dsa_tag_set(sc, port_id, 1, MVPP2_PRS_UNTAGGED,
		    MVPP2_PRS_EDSA);
		mvpp2_prs_dsa_tag_set(sc, port_id, 0, MVPP2_PRS_TAGGED,
		    MVPP2_PRS_DSA);
		mvpp2_prs_dsa_tag_set(sc, port_id, 0, MVPP2_PRS_UNTAGGED,
		    MVPP2_PRS_DSA);
		break;
	case MVPP2_TAG_TYPE_DSA:
		mvpp2_prs_dsa_tag_set(sc, port_id, 1, MVPP2_PRS_TAGGED,
		    MVPP2_PRS_DSA);
		mvpp2_prs_dsa_tag_set(sc, port_id, 1, MVPP2_PRS_UNTAGGED,
		    MVPP2_PRS_DSA);
		mvpp2_prs_dsa_tag_set(sc, port_id, 0, MVPP2_PRS_TAGGED,
		    MVPP2_PRS_EDSA);
		mvpp2_prs_dsa_tag_set(sc, port_id, 0, MVPP2_PRS_UNTAGGED,
		    MVPP2_PRS_EDSA);
		break;
	case MVPP2_TAG_TYPE_MH:
	case MVPP2_TAG_TYPE_NONE:
		mvpp2_prs_dsa_tag_set(sc, port_id, 0, MVPP2_PRS_TAGGED,
		    MVPP2_PRS_DSA);
		mvpp2_prs_dsa_tag_set(sc, port_id, 0, MVPP2_PRS_UNTAGGED,
		    MVPP2_PRS_DSA);
		mvpp2_prs_dsa_tag_set(sc, port_id, 0, MVPP2_PRS_TAGGED,
		    MVPP2_PRS_EDSA);
		mvpp2_prs_dsa_tag_set(sc, port_id, 0, MVPP2_PRS_UNTAGGED,
		    MVPP2_PRS_EDSA);
		break;
	default:
		if ((type < 0) || (type > MVPP2_TAG_TYPE_EDSA))
			return EINVAL;
		break;
	}

	return 0;
}

int
mvpp2_prs_def_flow(struct mvpp2_port *port)
{
	struct mvpp2_prs_entry pe;
	int tid;

	memset(&pe, 0, sizeof(pe));

	tid = mvpp2_prs_flow_find(port->sc, port->sc_id);
	if (tid < 0) {
		tid = mvpp2_prs_tcam_first_free(port->sc,
		    MVPP2_PE_LAST_FREE_TID, MVPP2_PE_FIRST_FREE_TID);
		if (tid < 0)
			return tid;

		pe.index = tid;
		mvpp2_prs_sram_ai_update(&pe, port->sc_id,
		    MVPP2_PRS_FLOW_ID_MASK);
		mvpp2_prs_sram_bits_set(&pe, MVPP2_PRS_SRAM_LU_DONE_BIT, 1);
		mvpp2_prs_shadow_set(port->sc, pe.index, MVPP2_PRS_LU_FLOWS);
	} else {
		mvpp2_prs_hw_read(port->sc, &pe, tid);
	}

	mvpp2_prs_tcam_lu_set(&pe, MVPP2_PRS_LU_FLOWS);
	mvpp2_prs_tcam_port_map_set(&pe, (1 << port->sc_id));
	mvpp2_prs_hw_write(port->sc, &pe);
	return 0;
}

void
mvpp2_cls_flow_write(struct mvpp2_softc *sc, struct mvpp2_cls_flow_entry *fe)
{
	mvpp2_write(sc, MVPP2_CLS_FLOW_INDEX_REG, fe->index);
	mvpp2_write(sc, MVPP2_CLS_FLOW_TBL0_REG, fe->data[0]);
	mvpp2_write(sc, MVPP2_CLS_FLOW_TBL1_REG, fe->data[1]);
	mvpp2_write(sc, MVPP2_CLS_FLOW_TBL2_REG, fe->data[2]);
}

void
mvpp2_cls_lookup_write(struct mvpp2_softc *sc, struct mvpp2_cls_lookup_entry *le)
{
	uint32_t val;

	val = (le->way << MVPP2_CLS_LKP_INDEX_WAY_OFFS) | le->lkpid;
	mvpp2_write(sc, MVPP2_CLS_LKP_INDEX_REG, val);
	mvpp2_write(sc, MVPP2_CLS_LKP_TBL_REG, le->data);
}

void
mvpp2_cls_init(struct mvpp2_softc *sc)
{
	struct mvpp2_cls_lookup_entry le;
	struct mvpp2_cls_flow_entry fe;
	int index;

	mvpp2_write(sc, MVPP2_CLS_MODE_REG, MVPP2_CLS_MODE_ACTIVE_MASK);
	memset(&fe.data, 0, sizeof(fe.data));
	for (index = 0; index < MVPP2_CLS_FLOWS_TBL_SIZE; index++) {
		fe.index = index;
		mvpp2_cls_flow_write(sc, &fe);
	}
	le.data = 0;
	for (index = 0; index < MVPP2_CLS_LKP_TBL_SIZE; index++) {
		le.lkpid = index;
		le.way = 0;
		mvpp2_cls_lookup_write(sc, &le);
		le.way = 1;
		mvpp2_cls_lookup_write(sc, &le);
	}
}

void
mvpp2_cls_port_config(struct mvpp2_port *port)
{
	struct mvpp2_cls_lookup_entry le;
	uint32_t val;

	/* set way for the port */
	val = mvpp2_read(port->sc, MVPP2_CLS_PORT_WAY_REG);
	val &= ~MVPP2_CLS_PORT_WAY_MASK(port->sc_id);
	mvpp2_write(port->sc, MVPP2_CLS_PORT_WAY_REG, val);

	/*
	 * pick the entry to be accessed in lookup ID decoding table
	 * according to the way and lkpid.
	 */
	le.lkpid = port->sc_id;
	le.way = 0;
	le.data = 0;

	/* set initial CPU queue for receiving packets */
	le.data &= ~MVPP2_CLS_LKP_TBL_RXQ_MASK;
	le.data |= (port->sc_id * 32);

	/* disable classification engines */
	le.data &= ~MVPP2_CLS_LKP_TBL_LOOKUP_EN_MASK;

	/* update lookup ID table entry */
	mvpp2_cls_lookup_write(port->sc, &le);
}

void
mvpp2_cls_oversize_rxq_set(struct mvpp2_port *port)
{
	uint32_t val;

	mvpp2_write(port->sc, MVPP2_CLS_OVERSIZE_RXQ_LOW_REG(port->sc_id),
	    (port->sc_id * 32) & MVPP2_CLS_OVERSIZE_RXQ_LOW_MASK);
	mvpp2_write(port->sc, MVPP2_CLS_SWFWD_P2HQ_REG(port->sc_id),
	    (port->sc_id * 32) >> MVPP2_CLS_OVERSIZE_RXQ_LOW_BITS);
	val = mvpp2_read(port->sc, MVPP2_CLS_SWFWD_PCTRL_REG);
	val &= ~MVPP2_CLS_SWFWD_PCTRL_MASK(port->sc_id);
	mvpp2_write(port->sc, MVPP2_CLS_SWFWD_PCTRL_REG, val);
}
