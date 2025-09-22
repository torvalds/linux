/*	$OpenBSD: if_bnxt.c,v 1.56 2025/09/05 09:58:24 stsp Exp $	*/
/*-
 * Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2016 Broadcom, All Rights Reserved.
 * The term Broadcom refers to Broadcom Limited and/or its subsidiaries
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 2018 Jonathan Matthew <jmatthew@openbsd.org>
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


#include "bpfilter.h"
#include "vlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/stdint.h>
#include <sys/sockio.h>
#include <sys/atomic.h>
#include <sys/intrmap.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_bnxtreg.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/route.h>
#include <net/toeplitz.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>

#define BNXT_HWRM_BAR		0x10
#define BNXT_DOORBELL_BAR	0x18

#define BNXT_MAX_QUEUES		8

#define BNXT_CP_RING_ID_BASE	0
#define BNXT_RX_RING_ID_BASE	(BNXT_MAX_QUEUES + 1)
#define BNXT_AG_RING_ID_BASE	((BNXT_MAX_QUEUES * 2) + 1)
#define BNXT_TX_RING_ID_BASE	((BNXT_MAX_QUEUES * 3) + 1)

#define BNXT_MAX_MTU		9500
#define BNXT_AG_BUFFER_SIZE	8192

#define BNXT_CP_PAGES		4

#define BNXT_MAX_TX_SEGS	31
#define BNXT_TX_SLOTS(bs)	(bs->bs_map->dm_nsegs + 1)

#define BNXT_HWRM_SHORT_REQ_LEN	sizeof(struct hwrm_short_input)

#define BNXT_HWRM_LOCK_INIT(_sc, _name)	\
	mtx_init_flags(&sc->sc_lock, IPL_NET, _name, 0)
#define BNXT_HWRM_LOCK(_sc) 		mtx_enter(&_sc->sc_lock)
#define BNXT_HWRM_UNLOCK(_sc) 		mtx_leave(&_sc->sc_lock)
#define BNXT_HWRM_LOCK_DESTROY(_sc)	/* nothing */
#define BNXT_HWRM_LOCK_ASSERT(_sc)	MUTEX_ASSERT_LOCKED(&_sc->sc_lock)

#define BNXT_FLAG_VF            0x0001
#define BNXT_FLAG_NPAR          0x0002
#define BNXT_FLAG_WOL_CAP       0x0004
#define BNXT_FLAG_SHORT_CMD     0x0008
#define BNXT_FLAG_MSIX          0x0010

/* NVRam stuff has a five minute timeout */
#define BNXT_NVM_TIMEO	(5 * 60 * 1000)

#define NEXT_CP_CONS_V(_ring, _cons, _v_bit)		\
do {	 						\
	if (++(_cons) == (_ring)->ring_size)		\
		((_cons) = 0, (_v_bit) = !_v_bit);	\
} while (0);

struct bnxt_ring {
	uint64_t		paddr;
	uint64_t		doorbell;
	caddr_t			vaddr;
	uint32_t		ring_size;
	uint16_t		id;
	uint16_t		phys_id;
};

struct bnxt_cp_ring {
	struct bnxt_ring	ring;
	void			*irq;
	struct bnxt_softc	*softc;
	uint32_t		cons;
	int			v_bit;
	uint32_t		commit_cons;
	int			commit_v_bit;
	struct ctx_hw_stats	*stats;
	uint32_t		stats_ctx_id;
	struct bnxt_dmamem	*ring_mem;
};

struct bnxt_grp_info {
	uint32_t		grp_id;
	uint16_t		stats_ctx;
	uint16_t		rx_ring_id;
	uint16_t		cp_ring_id;
	uint16_t		ag_ring_id;
};

struct bnxt_vnic_info {
	uint16_t		id;
	uint16_t		def_ring_grp;
	uint16_t		cos_rule;
	uint16_t		lb_rule;
	uint16_t		mru;

	uint32_t		flags;
#define BNXT_VNIC_FLAG_DEFAULT		0x01
#define BNXT_VNIC_FLAG_BD_STALL		0x02
#define BNXT_VNIC_FLAG_VLAN_STRIP	0x04

	uint64_t		filter_id;
	uint32_t		flow_id;

	uint16_t		rss_id;
};

struct bnxt_slot {
	bus_dmamap_t		bs_map;
	struct mbuf		*bs_m;
};

struct bnxt_dmamem {
	bus_dmamap_t		bdm_map;
	bus_dma_segment_t	bdm_seg;
	size_t			bdm_size;
	caddr_t			bdm_kva;
};
#define BNXT_DMA_MAP(_bdm)	((_bdm)->bdm_map)
#define BNXT_DMA_LEN(_bdm)	((_bdm)->bdm_size)
#define BNXT_DMA_DVA(_bdm)	((u_int64_t)(_bdm)->bdm_map->dm_segs[0].ds_addr)
#define BNXT_DMA_KVA(_bdm)	((void *)(_bdm)->bdm_kva)

struct bnxt_rx_queue {
	struct bnxt_softc	*rx_softc;
	struct ifiqueue		*rx_ifiq;
	struct bnxt_dmamem	*rx_ring_mem;	/* rx and ag */
	struct bnxt_ring	rx_ring;
	struct bnxt_ring	rx_ag_ring;
	struct if_rxring	rxr[2];
	struct bnxt_slot	*rx_slots;
	struct bnxt_slot	*rx_ag_slots;
	int			rx_prod;
	int			rx_cons;
	int			rx_ag_prod;
	int			rx_ag_cons;
	struct timeout		rx_refill;
};

struct bnxt_tx_queue {
	struct bnxt_softc	*tx_softc;
	struct ifqueue		*tx_ifq;
	struct bnxt_dmamem	*tx_ring_mem;
	struct bnxt_ring	tx_ring;
	struct bnxt_slot	*tx_slots;
	int			tx_prod;
	int			tx_cons;
	int			tx_ring_prod;
	int			tx_ring_cons;
};

struct bnxt_queue {
	char			q_name[8];
	int			q_index;
	void			*q_ihc;
	struct bnxt_softc	*q_sc;
	struct bnxt_cp_ring	q_cp;
	struct bnxt_rx_queue	q_rx;
	struct bnxt_tx_queue	q_tx;
	struct bnxt_grp_info	q_rg;
};

struct bnxt_softc {
	struct device		sc_dev;
	struct arpcom		sc_ac;
	struct ifmedia		sc_media;

	struct mutex		sc_lock;

	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_tag;
	bus_dma_tag_t		sc_dmat;

	bus_space_tag_t		sc_hwrm_t;
	bus_space_handle_t	sc_hwrm_h;
	bus_size_t		sc_hwrm_s;

	struct bnxt_dmamem	*sc_cmd_resp;
	uint16_t		sc_cmd_seq;
	uint16_t		sc_max_req_len;
	uint32_t		sc_cmd_timeo;
	uint32_t		sc_flags;

	bus_space_tag_t		sc_db_t;
	bus_space_handle_t	sc_db_h;
	bus_size_t		sc_db_s;

	void			*sc_ih;

	int			sc_hwrm_ver;
	int			sc_tx_queue_id;

	struct bnxt_vnic_info	sc_vnic;
	struct bnxt_dmamem	*sc_stats_ctx_mem;
	struct bnxt_dmamem	*sc_rx_cfg;

	struct bnxt_cp_ring	sc_cp_ring;

	int			sc_nqueues;
	struct intrmap		*sc_intrmap;
	struct bnxt_queue	sc_queues[BNXT_MAX_QUEUES];
};
#define DEVNAME(_sc)	((_sc)->sc_dev.dv_xname)

const struct pci_matchid bnxt_devices[] = {
	{ PCI_VENDOR_BROADCOM,	PCI_PRODUCT_BROADCOM_BCM57301 },
	{ PCI_VENDOR_BROADCOM,	PCI_PRODUCT_BROADCOM_BCM57302 },
	{ PCI_VENDOR_BROADCOM,	PCI_PRODUCT_BROADCOM_BCM57304 },
	{ PCI_VENDOR_BROADCOM,	PCI_PRODUCT_BROADCOM_BCM57311 },
	{ PCI_VENDOR_BROADCOM,	PCI_PRODUCT_BROADCOM_BCM57312 },
	{ PCI_VENDOR_BROADCOM,	PCI_PRODUCT_BROADCOM_BCM57314 },
	{ PCI_VENDOR_BROADCOM,	PCI_PRODUCT_BROADCOM_BCM57402 },
	{ PCI_VENDOR_BROADCOM,	PCI_PRODUCT_BROADCOM_BCM57404 },
	{ PCI_VENDOR_BROADCOM,	PCI_PRODUCT_BROADCOM_BCM57406 },
	{ PCI_VENDOR_BROADCOM,	PCI_PRODUCT_BROADCOM_BCM57407 },
	{ PCI_VENDOR_BROADCOM,	PCI_PRODUCT_BROADCOM_BCM57412 },
	{ PCI_VENDOR_BROADCOM,	PCI_PRODUCT_BROADCOM_BCM57414 },
	{ PCI_VENDOR_BROADCOM,	PCI_PRODUCT_BROADCOM_BCM57416 },
	{ PCI_VENDOR_BROADCOM,	PCI_PRODUCT_BROADCOM_BCM57416_SFP },
	{ PCI_VENDOR_BROADCOM,	PCI_PRODUCT_BROADCOM_BCM57417 },
	{ PCI_VENDOR_BROADCOM,	PCI_PRODUCT_BROADCOM_BCM57417_SFP }
};

int		bnxt_match(struct device *, void *, void *);
void		bnxt_attach(struct device *, struct device *, void *);

void		bnxt_up(struct bnxt_softc *);
void		bnxt_down(struct bnxt_softc *);
void		bnxt_iff(struct bnxt_softc *);
int		bnxt_ioctl(struct ifnet *, u_long, caddr_t);
int		bnxt_rxrinfo(struct bnxt_softc *, struct if_rxrinfo *);
void		bnxt_start(struct ifqueue *);
int		bnxt_admin_intr(void *);
int		bnxt_intr(void *);
void		bnxt_watchdog(struct ifnet *);
void		bnxt_media_status(struct ifnet *, struct ifmediareq *);
int		bnxt_media_change(struct ifnet *);
int		bnxt_media_autonegotiate(struct bnxt_softc *);

struct cmpl_base *bnxt_cpr_next_cmpl(struct bnxt_softc *, struct bnxt_cp_ring *);
void		bnxt_cpr_commit(struct bnxt_softc *, struct bnxt_cp_ring *);
void		bnxt_cpr_rollback(struct bnxt_softc *, struct bnxt_cp_ring *);

void		bnxt_mark_cpr_invalid(struct bnxt_cp_ring *);
void		bnxt_write_cp_doorbell(struct bnxt_softc *, struct bnxt_ring *,
		    int);
void		bnxt_write_cp_doorbell_index(struct bnxt_softc *,
		    struct bnxt_ring *, uint32_t, int);
void		bnxt_write_rx_doorbell(struct bnxt_softc *, struct bnxt_ring *,
		    int);
void		bnxt_write_tx_doorbell(struct bnxt_softc *, struct bnxt_ring *,
		    int);

int		bnxt_rx_fill(struct bnxt_queue *);
int		bnxt_rx_fill_ag(struct bnxt_queue *);
u_int		bnxt_rx_fill_slots(struct bnxt_softc *, struct bnxt_ring *, void *,
		    struct bnxt_slot *, uint *, int, uint16_t, u_int);
void		bnxt_refill(void *);
int		bnxt_rx(struct bnxt_softc *, struct bnxt_rx_queue *,
		    struct bnxt_cp_ring *, struct mbuf_list *, struct mbuf_list *,
		    int *, int *, struct cmpl_base *);

void		bnxt_txeof(struct bnxt_softc *, struct bnxt_tx_queue *, int *,
		    struct cmpl_base *);

int		bnxt_set_cp_ring_aggint(struct bnxt_softc *, struct bnxt_cp_ring *);

int		_hwrm_send_message(struct bnxt_softc *, void *, uint32_t);
int		hwrm_send_message(struct bnxt_softc *, void *, uint32_t);
void		bnxt_hwrm_cmd_hdr_init(struct bnxt_softc *, void *, uint16_t);
int 		bnxt_hwrm_err_map(uint16_t err);

/* HWRM Function Prototypes */
int		bnxt_hwrm_ring_alloc(struct bnxt_softc *, uint8_t,
		    struct bnxt_ring *, uint16_t, uint32_t, int);
int		bnxt_hwrm_ring_free(struct bnxt_softc *, uint8_t,
		    struct bnxt_ring *);
int		bnxt_hwrm_ver_get(struct bnxt_softc *);
int		bnxt_hwrm_queue_qportcfg(struct bnxt_softc *);
int		bnxt_hwrm_func_drv_rgtr(struct bnxt_softc *);
int		bnxt_hwrm_func_qcaps(struct bnxt_softc *);
int		bnxt_hwrm_func_qcfg(struct bnxt_softc *);
int		bnxt_hwrm_func_reset(struct bnxt_softc *);
int		bnxt_hwrm_vnic_ctx_alloc(struct bnxt_softc *, uint16_t *);
int		bnxt_hwrm_vnic_ctx_free(struct bnxt_softc *, uint16_t *);
int		bnxt_hwrm_vnic_cfg(struct bnxt_softc *,
		    struct bnxt_vnic_info *);
int		bnxt_hwrm_vnic_cfg_placement(struct bnxt_softc *,
		    struct bnxt_vnic_info *vnic);
int		bnxt_hwrm_stat_ctx_alloc(struct bnxt_softc *,
		    struct bnxt_cp_ring *, uint64_t);
int		bnxt_hwrm_stat_ctx_free(struct bnxt_softc *,
		    struct bnxt_cp_ring *);
int		bnxt_hwrm_ring_grp_alloc(struct bnxt_softc *,
		    struct bnxt_grp_info *);
int		bnxt_hwrm_ring_grp_free(struct bnxt_softc *,
		    struct bnxt_grp_info *);
int		bnxt_hwrm_vnic_alloc(struct bnxt_softc *,
		    struct bnxt_vnic_info *);
int		bnxt_hwrm_vnic_free(struct bnxt_softc *,
		    struct bnxt_vnic_info *);
int		bnxt_hwrm_cfa_l2_set_rx_mask(struct bnxt_softc *,
		    uint32_t, uint32_t, uint64_t, uint32_t);
int		bnxt_hwrm_set_filter(struct bnxt_softc *,
		    struct bnxt_vnic_info *);
int		bnxt_hwrm_free_filter(struct bnxt_softc *,
		    struct bnxt_vnic_info *);
int		bnxt_hwrm_vnic_rss_cfg(struct bnxt_softc *,
		    struct bnxt_vnic_info *, uint32_t, daddr_t, daddr_t);
int		bnxt_cfg_async_cr(struct bnxt_softc *, struct bnxt_cp_ring *);
int		bnxt_hwrm_nvm_get_dev_info(struct bnxt_softc *, uint16_t *,
		    uint16_t *, uint32_t *, uint32_t *, uint32_t *, uint32_t *);
int		bnxt_hwrm_port_phy_qcfg(struct bnxt_softc *,
		    struct ifmediareq *);
int		bnxt_hwrm_func_rgtr_async_events(struct bnxt_softc *);
int		bnxt_get_sffpage(struct bnxt_softc *, struct if_sffpage *);

/* not used yet: */
#if 0
int bnxt_hwrm_func_drv_unrgtr(struct bnxt_softc *softc, bool shutdown);

int bnxt_hwrm_port_qstats(struct bnxt_softc *softc);


int bnxt_hwrm_vnic_tpa_cfg(struct bnxt_softc *softc);
void bnxt_validate_hw_lro_settings(struct bnxt_softc *softc);
int bnxt_hwrm_fw_reset(struct bnxt_softc *softc, uint8_t processor,
    uint8_t *selfreset);
int bnxt_hwrm_fw_qstatus(struct bnxt_softc *softc, uint8_t type,
    uint8_t *selfreset);
int bnxt_hwrm_fw_get_time(struct bnxt_softc *softc, uint16_t *year,
    uint8_t *month, uint8_t *day, uint8_t *hour, uint8_t *minute,
    uint8_t *second, uint16_t *millisecond, uint16_t *zone);
int bnxt_hwrm_fw_set_time(struct bnxt_softc *softc, uint16_t year,
    uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second,
    uint16_t millisecond, uint16_t zone);

#endif


const struct cfattach bnxt_ca = {
	sizeof(struct bnxt_softc), bnxt_match, bnxt_attach
};

struct cfdriver bnxt_cd = {
	NULL, "bnxt", DV_IFNET
};

struct bnxt_dmamem *
bnxt_dmamem_alloc(struct bnxt_softc *sc, size_t size)
{
	struct bnxt_dmamem *m;
	int nsegs;

	m = malloc(sizeof(*m), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (m == NULL)
		return (NULL);

	m->bdm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
	    &m->bdm_map) != 0)
		goto bdmfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0, &m->bdm_seg, 1,
	    &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO | BUS_DMA_64BIT) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &m->bdm_seg, nsegs, size, &m->bdm_kva,
	    BUS_DMA_NOWAIT) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, m->bdm_map, m->bdm_kva, size, NULL,
	    BUS_DMA_NOWAIT) != 0)
		goto unmap;

	return (m);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, m->bdm_kva, m->bdm_size);
free:
	bus_dmamem_free(sc->sc_dmat, &m->bdm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, m->bdm_map);
bdmfree:
	free(m, M_DEVBUF, sizeof *m);

	return (NULL);
}

void
bnxt_dmamem_free(struct bnxt_softc *sc, struct bnxt_dmamem *m)
{
	bus_dmamap_unload(sc->sc_dmat, m->bdm_map);
	bus_dmamem_unmap(sc->sc_dmat, m->bdm_kva, m->bdm_size);
	bus_dmamem_free(sc->sc_dmat, &m->bdm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, m->bdm_map);
	free(m, M_DEVBUF, sizeof *m);
}

int
bnxt_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, bnxt_devices, nitems(bnxt_devices)));
}

void
bnxt_attach(struct device *parent, struct device *self, void *aux)
{
	struct bnxt_softc *sc = (struct bnxt_softc *)self;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct pci_attach_args *pa = aux;
	struct bnxt_cp_ring *cpr;
	pci_intr_handle_t ih;
	const char *intrstr;
	u_int memtype;
	int i;

	sc->sc_pc = pa->pa_pc;
	sc->sc_tag = pa->pa_tag;
	sc->sc_dmat = pa->pa_dmat;

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, BNXT_HWRM_BAR);
	if (pci_mapreg_map(pa, BNXT_HWRM_BAR, memtype, 0, &sc->sc_hwrm_t,
	    &sc->sc_hwrm_h, NULL, &sc->sc_hwrm_s, 0)) {
		printf(": failed to map hwrm\n");
		return;
	}

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, BNXT_DOORBELL_BAR);
	if (pci_mapreg_map(pa, BNXT_DOORBELL_BAR, memtype, 0, &sc->sc_db_t,
	    &sc->sc_db_h, NULL, &sc->sc_db_s, 0)) {
		printf(": failed to map doorbell\n");
		goto unmap_1;
	}

	BNXT_HWRM_LOCK_INIT(sc, DEVNAME(sc));
	sc->sc_cmd_resp = bnxt_dmamem_alloc(sc, PAGE_SIZE);
	if (sc->sc_cmd_resp == NULL) {
		printf(": failed to allocate command response buffer\n");
		goto unmap_2;
	}

	if (bnxt_hwrm_ver_get(sc) != 0) {
		printf(": failed to query version info\n");
		goto free_resp;
	}

	if (bnxt_hwrm_nvm_get_dev_info(sc, NULL, NULL, NULL, NULL, NULL, NULL)
	    != 0) {
		printf(": failed to get nvram info\n");
		goto free_resp;
	}

	if (bnxt_hwrm_func_drv_rgtr(sc) != 0) {
		printf(": failed to register driver with firmware\n");
		goto free_resp;
	}

	if (bnxt_hwrm_func_rgtr_async_events(sc) != 0) {
		printf(": failed to register async events\n");
		goto free_resp;
	}

	if (bnxt_hwrm_func_qcaps(sc) != 0) {
		printf(": failed to get queue capabilities\n");
		goto free_resp;
	}

	/*
	 * devices advertise msi support, but there's no way to tell a
	 * completion queue to use msi mode, only legacy or msi-x.
	 */
	if (pci_intr_map_msix(pa, 0, &ih) == 0) {
		int nmsix;

		sc->sc_flags |= BNXT_FLAG_MSIX;
		intrstr = pci_intr_string(sc->sc_pc, ih);

		nmsix = pci_intr_msix_count(pa);
		if (nmsix > 1) {
			sc->sc_ih = pci_intr_establish(sc->sc_pc, ih,
			    IPL_NET | IPL_MPSAFE, bnxt_admin_intr, sc, DEVNAME(sc));
			sc->sc_intrmap = intrmap_create(&sc->sc_dev,
			    nmsix - 1, BNXT_MAX_QUEUES, INTRMAP_POWEROF2);
			sc->sc_nqueues = intrmap_count(sc->sc_intrmap);
			KASSERT(sc->sc_nqueues > 0);
			KASSERT(powerof2(sc->sc_nqueues));
		} else {
			sc->sc_ih = pci_intr_establish(sc->sc_pc, ih,
			    IPL_NET | IPL_MPSAFE, bnxt_intr, &sc->sc_queues[0],
			    DEVNAME(sc));
			sc->sc_nqueues = 1;
		}
	} else if (pci_intr_map(pa, &ih) == 0) {
		intrstr = pci_intr_string(sc->sc_pc, ih);
		sc->sc_ih = pci_intr_establish(sc->sc_pc, ih, IPL_NET | IPL_MPSAFE,
		    bnxt_intr, &sc->sc_queues[0], DEVNAME(sc));
		sc->sc_nqueues = 1;
	} else {
		printf(": unable to map interrupt\n");
		goto free_resp;
	}
	if (sc->sc_ih == NULL) {
		printf(": unable to establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto deintr;
	}
	printf("%s, %d queues, address %s\n", intrstr, sc->sc_nqueues,
	    ether_sprintf(sc->sc_ac.ac_enaddr));

	if (bnxt_hwrm_func_qcfg(sc) != 0) {
		printf("%s: failed to query function config\n", DEVNAME(sc));
		goto deintr;
	}

	if (bnxt_hwrm_queue_qportcfg(sc) != 0) {
		printf("%s: failed to query port config\n", DEVNAME(sc));
		goto deintr;
	}

	if (bnxt_hwrm_func_reset(sc) != 0) {
		printf("%s: reset failed\n", DEVNAME(sc));
		goto deintr;
	}

	if (sc->sc_intrmap == NULL)
		cpr = &sc->sc_queues[0].q_cp;
	else
		cpr = &sc->sc_cp_ring;

	cpr->stats_ctx_id = HWRM_NA_SIGNATURE;
	cpr->ring.phys_id = (uint16_t)HWRM_NA_SIGNATURE;
	cpr->softc = sc;
	cpr->ring.id = 0;
	cpr->ring.doorbell = cpr->ring.id * 0x80;
	cpr->ring.ring_size = (PAGE_SIZE * BNXT_CP_PAGES) /
	    sizeof(struct cmpl_base);
	cpr->ring_mem = bnxt_dmamem_alloc(sc, PAGE_SIZE *
	    BNXT_CP_PAGES);
	if (cpr->ring_mem == NULL) {
		printf("%s: failed to allocate completion queue memory\n",
		    DEVNAME(sc));
		goto deintr;
	}
	cpr->ring.vaddr = BNXT_DMA_KVA(cpr->ring_mem);
	cpr->ring.paddr = BNXT_DMA_DVA(cpr->ring_mem);
	cpr->cons = UINT32_MAX;
	cpr->v_bit = 1;
	bnxt_mark_cpr_invalid(cpr);
	if (bnxt_hwrm_ring_alloc(sc, HWRM_RING_ALLOC_INPUT_RING_TYPE_L2_CMPL,
	    &cpr->ring, (uint16_t)HWRM_NA_SIGNATURE,
	    HWRM_NA_SIGNATURE, 1) != 0) {
		printf("%s: failed to allocate completion queue\n",
		    DEVNAME(sc));
		goto free_cp_mem;
	}
	if (bnxt_cfg_async_cr(sc, cpr) != 0) {
		printf("%s: failed to set async completion ring\n",
		    DEVNAME(sc));
		goto free_cp_mem;
	}
	bnxt_write_cp_doorbell(sc, &cpr->ring, 1);

	if (bnxt_set_cp_ring_aggint(sc, cpr) != 0) {
		printf("%s: failed to set interrupt aggregation\n",
		    DEVNAME(sc));
		goto free_cp_mem;
	}

	strlcpy(ifp->if_xname, DEVNAME(sc), IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_MULTICAST | IFF_SIMPLEX;
	ifp->if_xflags = IFXF_MPSAFE;
	ifp->if_ioctl = bnxt_ioctl;
	ifp->if_qstart = bnxt_start;
	ifp->if_watchdog = bnxt_watchdog;
	ifp->if_hardmtu = BNXT_MAX_MTU;
	ifp->if_capabilities = IFCAP_VLAN_MTU | IFCAP_CSUM_IPv4 |
	    IFCAP_CSUM_UDPv4 | IFCAP_CSUM_TCPv4 | IFCAP_CSUM_UDPv6 |
	    IFCAP_CSUM_TCPv6;
	ifp->if_capabilities |= IFCAP_TSOv4 | IFCAP_TSOv6;
#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif
	ifp->if_capabilities |= IFCAP_LRO;	

	ifq_init_maxlen(&ifp->if_snd, 1024);	/* ? */

	ifmedia_init(&sc->sc_media, IFM_IMASK, bnxt_media_change,
	    bnxt_media_status);

	if_attach(ifp);
	ether_ifattach(ifp);

	if_attach_iqueues(ifp, sc->sc_nqueues);
	if_attach_queues(ifp, sc->sc_nqueues);
	for (i = 0; i < sc->sc_nqueues; i++) {
		struct ifiqueue *ifiq = ifp->if_iqs[i];
		struct ifqueue *ifq = ifp->if_ifqs[i];
		struct bnxt_queue *bq = &sc->sc_queues[i];
		struct bnxt_cp_ring *cp = &bq->q_cp;
		struct bnxt_rx_queue *rx = &bq->q_rx;
		struct bnxt_tx_queue *tx = &bq->q_tx;

		bq->q_index = i;
		bq->q_sc = sc;

		rx->rx_softc = sc;
		rx->rx_ifiq = ifiq;
		timeout_set(&rx->rx_refill, bnxt_refill, bq);
		ifiq->ifiq_softc = rx;

		tx->tx_softc = sc;
		tx->tx_ifq = ifq;
		ifq->ifq_softc = tx;

		if (sc->sc_intrmap != NULL) {
			cp->stats_ctx_id = HWRM_NA_SIGNATURE;
			cp->ring.phys_id = (uint16_t)HWRM_NA_SIGNATURE;
			cp->ring.id = i + 1;	/* first cp ring is async only */
			cp->softc = sc;
			cp->ring.doorbell = bq->q_cp.ring.id * 0x80;
			cp->ring.ring_size = (PAGE_SIZE * BNXT_CP_PAGES) /
			    sizeof(struct cmpl_base);
			if (pci_intr_map_msix(pa, i + 1, &ih) != 0) {
				printf("%s: unable to map queue interrupt %d\n",
				    DEVNAME(sc), i);
				goto intrdisestablish;
			}
			snprintf(bq->q_name, sizeof(bq->q_name), "%s:%d",
			    DEVNAME(sc), i);
			bq->q_ihc = pci_intr_establish_cpu(sc->sc_pc, ih,
			    IPL_NET | IPL_MPSAFE, intrmap_cpu(sc->sc_intrmap, i),
			    bnxt_intr, bq, bq->q_name);
			if (bq->q_ihc == NULL) {
				printf("%s: unable to establish interrupt %d\n",
				    DEVNAME(sc), i);
				goto intrdisestablish;
			}
		}
	}

	bnxt_media_autonegotiate(sc);
	bnxt_hwrm_port_phy_qcfg(sc, NULL);
	return;

intrdisestablish:
	for (i = 0; i < sc->sc_nqueues; i++) {
		struct bnxt_queue *bq = &sc->sc_queues[i];
		if (bq->q_ihc == NULL)
			continue;
		pci_intr_disestablish(sc->sc_pc, bq->q_ihc);
		bq->q_ihc = NULL;
	}
free_cp_mem:
	bnxt_dmamem_free(sc, cpr->ring_mem);
deintr:
	if (sc->sc_intrmap != NULL) {
		intrmap_destroy(sc->sc_intrmap);
		sc->sc_intrmap = NULL;
	}
	pci_intr_disestablish(sc->sc_pc, sc->sc_ih);
	sc->sc_ih = NULL;
free_resp:
	bnxt_dmamem_free(sc, sc->sc_cmd_resp);
unmap_2:
	bus_space_unmap(sc->sc_db_t, sc->sc_db_h, sc->sc_db_s);
	sc->sc_db_s = 0;
unmap_1:
	bus_space_unmap(sc->sc_hwrm_t, sc->sc_hwrm_h, sc->sc_hwrm_s);
	sc->sc_hwrm_s = 0;
}

void
bnxt_free_slots(struct bnxt_softc *sc, struct bnxt_slot *slots, int allocated,
    int total)
{
	struct bnxt_slot *bs;

	int i = allocated;
	while (i-- > 0) {
		bs = &slots[i];
		bus_dmamap_destroy(sc->sc_dmat, bs->bs_map);
		if (bs->bs_m != NULL)
			m_freem(bs->bs_m);
	}
	free(slots, M_DEVBUF, total * sizeof(*bs));
}

int
bnxt_set_cp_ring_aggint(struct bnxt_softc *sc, struct bnxt_cp_ring *cpr)
{
	struct hwrm_ring_cmpl_ring_cfg_aggint_params_input aggint;

	/*
	 * set interrupt aggregation parameters for around 10k interrupts
	 * per second.  the timers are in units of 80usec, and the counters
	 * are based on the minimum rx ring size of 32.
	 */
	memset(&aggint, 0, sizeof(aggint));
        bnxt_hwrm_cmd_hdr_init(sc, &aggint,
	    HWRM_RING_CMPL_RING_CFG_AGGINT_PARAMS);
	aggint.ring_id = htole16(cpr->ring.phys_id);
	aggint.num_cmpl_dma_aggr = htole16(32);
	aggint.num_cmpl_dma_aggr_during_int  = aggint.num_cmpl_dma_aggr;
	aggint.cmpl_aggr_dma_tmr = htole16((1000000000 / 20000) / 80);
	aggint.cmpl_aggr_dma_tmr_during_int = aggint.cmpl_aggr_dma_tmr;
	aggint.int_lat_tmr_min = htole16((1000000000 / 20000) / 80);
	aggint.int_lat_tmr_max = htole16((1000000000 / 10000) / 80);
	aggint.num_cmpl_aggr_int = htole16(16);
	return (hwrm_send_message(sc, &aggint, sizeof(aggint)));
}

int
bnxt_queue_up(struct bnxt_softc *sc, struct bnxt_queue *bq)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct bnxt_cp_ring *cp = &bq->q_cp;
	struct bnxt_rx_queue *rx = &bq->q_rx;
	struct bnxt_tx_queue *tx = &bq->q_tx;
	struct bnxt_grp_info *rg = &bq->q_rg;
	struct bnxt_slot *bs;
	int i;

	tx->tx_ring_mem = bnxt_dmamem_alloc(sc, PAGE_SIZE);
	if (tx->tx_ring_mem == NULL) {
		printf("%s: failed to allocate tx ring %d\n", DEVNAME(sc), bq->q_index);
		return ENOMEM;
	}

	rx->rx_ring_mem = bnxt_dmamem_alloc(sc, PAGE_SIZE * 2);
	if (rx->rx_ring_mem == NULL) {
		printf("%s: failed to allocate rx ring %d\n", DEVNAME(sc), bq->q_index);
		goto free_tx;
	}

	/* completion ring is already allocated if we're not using an intrmap */
	if (sc->sc_intrmap != NULL) {
		cp->ring_mem = bnxt_dmamem_alloc(sc, PAGE_SIZE * BNXT_CP_PAGES);
		if (cp->ring_mem == NULL) {
			printf("%s: failed to allocate completion ring %d mem\n",
			    DEVNAME(sc), bq->q_index);
			goto free_rx;
		}
		cp->ring.vaddr = BNXT_DMA_KVA(cp->ring_mem);
		cp->ring.paddr = BNXT_DMA_DVA(cp->ring_mem);
		cp->cons = UINT32_MAX;
		cp->v_bit = 1;
		bnxt_mark_cpr_invalid(cp);

		if (bnxt_hwrm_ring_alloc(sc, HWRM_RING_ALLOC_INPUT_RING_TYPE_L2_CMPL,
		    &cp->ring, (uint16_t)HWRM_NA_SIGNATURE,
		    HWRM_NA_SIGNATURE, 1) != 0) {
			printf("%s: failed to allocate completion queue %d\n",
			    DEVNAME(sc), bq->q_index);
			goto free_rx;
		}

		if (bnxt_set_cp_ring_aggint(sc, cp) != 0) {
			printf("%s: failed to set interrupt %d aggregation\n",
			    DEVNAME(sc), bq->q_index);
			goto free_rx;
		}
		bnxt_write_cp_doorbell(sc, &cp->ring, 1);
	}

	if (bnxt_hwrm_stat_ctx_alloc(sc, &bq->q_cp,
	    BNXT_DMA_DVA(sc->sc_stats_ctx_mem) +
	    (bq->q_index * sizeof(struct ctx_hw_stats))) != 0) {
		printf("%s: failed to set up stats context\n", DEVNAME(sc));
		goto free_rx;
	}

	tx->tx_ring.phys_id = (uint16_t)HWRM_NA_SIGNATURE;
	tx->tx_ring.id = BNXT_TX_RING_ID_BASE + bq->q_index;
	tx->tx_ring.doorbell = tx->tx_ring.id * 0x80;
	tx->tx_ring.ring_size = PAGE_SIZE / sizeof(struct tx_bd_short);
	tx->tx_ring.vaddr = BNXT_DMA_KVA(tx->tx_ring_mem);
	tx->tx_ring.paddr = BNXT_DMA_DVA(tx->tx_ring_mem);
	if (bnxt_hwrm_ring_alloc(sc, HWRM_RING_ALLOC_INPUT_RING_TYPE_TX,
	    &tx->tx_ring, cp->ring.phys_id, HWRM_NA_SIGNATURE, 1) != 0) {
		printf("%s: failed to set up tx ring\n",
		    DEVNAME(sc));
		goto dealloc_stats;
	}
	bnxt_write_tx_doorbell(sc, &tx->tx_ring, 0);

	rx->rx_ring.phys_id = (uint16_t)HWRM_NA_SIGNATURE;
	rx->rx_ring.id = BNXT_RX_RING_ID_BASE + bq->q_index;
	rx->rx_ring.doorbell = rx->rx_ring.id * 0x80;
	rx->rx_ring.ring_size = PAGE_SIZE / sizeof(struct rx_prod_pkt_bd);
	rx->rx_ring.vaddr = BNXT_DMA_KVA(rx->rx_ring_mem);
	rx->rx_ring.paddr = BNXT_DMA_DVA(rx->rx_ring_mem);
	if (bnxt_hwrm_ring_alloc(sc, HWRM_RING_ALLOC_INPUT_RING_TYPE_RX,
	    &rx->rx_ring, cp->ring.phys_id, HWRM_NA_SIGNATURE, 1) != 0) {
		printf("%s: failed to set up rx ring\n",
		    DEVNAME(sc));
		goto dealloc_tx;
	}
	bnxt_write_rx_doorbell(sc, &rx->rx_ring, 0);

	rx->rx_ag_ring.phys_id = (uint16_t)HWRM_NA_SIGNATURE;
	rx->rx_ag_ring.id = BNXT_AG_RING_ID_BASE + bq->q_index;
	rx->rx_ag_ring.doorbell = rx->rx_ag_ring.id * 0x80;
	rx->rx_ag_ring.ring_size = PAGE_SIZE / sizeof(struct rx_prod_pkt_bd);
	rx->rx_ag_ring.vaddr = BNXT_DMA_KVA(rx->rx_ring_mem) + PAGE_SIZE;
	rx->rx_ag_ring.paddr = BNXT_DMA_DVA(rx->rx_ring_mem) + PAGE_SIZE;
	if (bnxt_hwrm_ring_alloc(sc, HWRM_RING_ALLOC_INPUT_RING_TYPE_RX,
	    &rx->rx_ag_ring, cp->ring.phys_id, HWRM_NA_SIGNATURE, 1) != 0) {
		printf("%s: failed to set up rx ag ring\n",
		    DEVNAME(sc));
		goto dealloc_rx;
	}
	bnxt_write_rx_doorbell(sc, &rx->rx_ag_ring, 0);

	rg->grp_id = HWRM_NA_SIGNATURE;
	rg->stats_ctx = cp->stats_ctx_id;
	rg->rx_ring_id = rx->rx_ring.phys_id;
	rg->ag_ring_id = rx->rx_ag_ring.phys_id;
	rg->cp_ring_id = cp->ring.phys_id;
	if (bnxt_hwrm_ring_grp_alloc(sc, rg) != 0) {
		printf("%s: failed to allocate ring group\n",
		    DEVNAME(sc));
		goto dealloc_ag;
	}

	rx->rx_slots = mallocarray(sizeof(*bs), rx->rx_ring.ring_size,
	    M_DEVBUF, M_WAITOK | M_ZERO);
	if (rx->rx_slots == NULL) {
		printf("%s: failed to allocate rx slots\n", DEVNAME(sc));
		goto dealloc_ring_group;
	}

	for (i = 0; i < rx->rx_ring.ring_size; i++) {
		bs = &rx->rx_slots[i];
		if (bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES, 0,
		    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
		    &bs->bs_map) != 0) {
			printf("%s: failed to allocate rx dma maps\n",
			    DEVNAME(sc));
			goto destroy_rx_slots;
		}
	}

	rx->rx_ag_slots = mallocarray(sizeof(*bs), rx->rx_ag_ring.ring_size,
	    M_DEVBUF, M_WAITOK | M_ZERO);
	if (rx->rx_ag_slots == NULL) {
		printf("%s: failed to allocate rx ag slots\n", DEVNAME(sc));
		goto destroy_rx_slots;
	}

	for (i = 0; i < rx->rx_ag_ring.ring_size; i++) {
		bs = &rx->rx_ag_slots[i];
		if (bus_dmamap_create(sc->sc_dmat, BNXT_AG_BUFFER_SIZE, 1,
		    BNXT_AG_BUFFER_SIZE, 0,
		    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
		    &bs->bs_map) != 0) {
			printf("%s: failed to allocate rx ag dma maps\n",
			    DEVNAME(sc));
			goto destroy_rx_ag_slots;
		}
	}

	tx->tx_slots = mallocarray(sizeof(*bs), tx->tx_ring.ring_size,
	    M_DEVBUF, M_WAITOK | M_ZERO);
	if (tx->tx_slots == NULL) {
		printf("%s: failed to allocate tx slots\n", DEVNAME(sc));
		goto destroy_rx_ag_slots;
	}

	for (i = 0; i < tx->tx_ring.ring_size; i++) {
		bs = &tx->tx_slots[i];
		if (bus_dmamap_create(sc->sc_dmat, MAXMCLBYTES, BNXT_MAX_TX_SEGS,
		    BNXT_MAX_MTU, 0,
		    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
		    &bs->bs_map) != 0) {
			printf("%s: failed to allocate tx dma maps\n",
			    DEVNAME(sc));
			goto destroy_tx_slots;
		}
	}

	/*
	 * initially, the rx ring must be filled at least some distance beyond
	 * the current consumer index, as it looks like the firmware assumes the
	 * ring is full on creation, but doesn't prefetch the whole thing.
	 * once the whole ring has been used once, we should be able to back off
	 * to 2 or so slots, but we currently don't have a way of doing that.
	 */
	if_rxr_init(&rx->rxr[0], 32, rx->rx_ring.ring_size - 1);
	if_rxr_init(&rx->rxr[1], 32, rx->rx_ag_ring.ring_size - 1);
	rx->rx_prod = 0;
	rx->rx_cons = 0;
	rx->rx_ag_prod = 0;
	rx->rx_ag_cons = 0;
	bnxt_rx_fill(bq);
	bnxt_rx_fill_ag(bq);

	tx->tx_cons = 0;
	tx->tx_prod = 0;
	tx->tx_ring_cons = 0;
	tx->tx_ring_prod = 0;
	ifq_clr_oactive(ifp->if_ifqs[bq->q_index]);
	ifq_restart(ifp->if_ifqs[bq->q_index]);
	return 0;

destroy_tx_slots:
	bnxt_free_slots(sc, tx->tx_slots, i, tx->tx_ring.ring_size);
	tx->tx_slots = NULL;

	i = rx->rx_ag_ring.ring_size;
destroy_rx_ag_slots:
	bnxt_free_slots(sc, rx->rx_ag_slots, i, rx->rx_ag_ring.ring_size);
	rx->rx_ag_slots = NULL;

	i = rx->rx_ring.ring_size;
destroy_rx_slots:
	bnxt_free_slots(sc, rx->rx_slots, i, rx->rx_ring.ring_size);
	rx->rx_slots = NULL;
dealloc_ring_group:
	bnxt_hwrm_ring_grp_free(sc, &bq->q_rg);
dealloc_ag:
	bnxt_hwrm_ring_free(sc, HWRM_RING_ALLOC_INPUT_RING_TYPE_RX,
	    &rx->rx_ag_ring);
dealloc_tx:
	bnxt_hwrm_ring_free(sc, HWRM_RING_ALLOC_INPUT_RING_TYPE_TX,
	    &tx->tx_ring);
dealloc_rx:
	bnxt_hwrm_ring_free(sc, HWRM_RING_ALLOC_INPUT_RING_TYPE_RX,
	    &rx->rx_ring);
dealloc_stats:
	bnxt_hwrm_stat_ctx_free(sc, cp);
free_rx:
	bnxt_dmamem_free(sc, rx->rx_ring_mem);
	rx->rx_ring_mem = NULL;
free_tx:
	bnxt_dmamem_free(sc, tx->tx_ring_mem);
	tx->tx_ring_mem = NULL;
	return ENOMEM;
}

void
bnxt_queue_down(struct bnxt_softc *sc, struct bnxt_queue *bq)
{
	struct bnxt_cp_ring *cp = &bq->q_cp;
	struct bnxt_rx_queue *rx = &bq->q_rx;
	struct bnxt_tx_queue *tx = &bq->q_tx;

	bnxt_free_slots(sc, tx->tx_slots, tx->tx_ring.ring_size,
	    tx->tx_ring.ring_size);
	tx->tx_slots = NULL;

	bnxt_free_slots(sc, rx->rx_ag_slots, rx->rx_ag_ring.ring_size,
	    rx->rx_ag_ring.ring_size);
	rx->rx_ag_slots = NULL;

	bnxt_free_slots(sc, rx->rx_slots, rx->rx_ring.ring_size,
	    rx->rx_ring.ring_size);
	rx->rx_slots = NULL;

	bnxt_hwrm_ring_grp_free(sc, &bq->q_rg);
	bnxt_hwrm_stat_ctx_free(sc, &bq->q_cp);

	/* may need to wait for 500ms here before we can free the rings */

	bnxt_hwrm_ring_free(sc, HWRM_RING_ALLOC_INPUT_RING_TYPE_TX,
	    &tx->tx_ring);
	bnxt_hwrm_ring_free(sc, HWRM_RING_ALLOC_INPUT_RING_TYPE_RX,
	    &rx->rx_ag_ring);
	bnxt_hwrm_ring_free(sc, HWRM_RING_ALLOC_INPUT_RING_TYPE_RX,
	    &rx->rx_ring);

	/* if no intrmap, leave cp ring in place for async events */
	if (sc->sc_intrmap != NULL) {
		bnxt_hwrm_ring_free(sc, HWRM_RING_ALLOC_INPUT_RING_TYPE_L2_CMPL,
		    &cp->ring);

		bnxt_dmamem_free(sc, cp->ring_mem);
		cp->ring_mem = NULL;
	}

	bnxt_dmamem_free(sc, rx->rx_ring_mem);
	rx->rx_ring_mem = NULL;

	bnxt_dmamem_free(sc, tx->tx_ring_mem);
	tx->tx_ring_mem = NULL;
}

void
bnxt_up(struct bnxt_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int i;

	sc->sc_stats_ctx_mem = bnxt_dmamem_alloc(sc,
	    sizeof(struct ctx_hw_stats) * sc->sc_nqueues);
	if (sc->sc_stats_ctx_mem == NULL) {
		printf("%s: failed to allocate stats contexts\n", DEVNAME(sc));
		return;
	}

	sc->sc_rx_cfg = bnxt_dmamem_alloc(sc, PAGE_SIZE * 2);
	if (sc->sc_rx_cfg == NULL) {
		printf("%s: failed to allocate rx config buffer\n",
		    DEVNAME(sc));
		goto free_stats;
	}

	for (i = 0; i < sc->sc_nqueues; i++) {
		if (bnxt_queue_up(sc, &sc->sc_queues[i]) != 0) {
			goto down_queues;
		}
	}

	sc->sc_vnic.rss_id = (uint16_t)HWRM_NA_SIGNATURE;
	if (bnxt_hwrm_vnic_ctx_alloc(sc, &sc->sc_vnic.rss_id) != 0) {
		printf("%s: failed to allocate vnic rss context\n",
		    DEVNAME(sc));
		goto down_all_queues;
	}

	sc->sc_vnic.id = (uint16_t)HWRM_NA_SIGNATURE;
	sc->sc_vnic.def_ring_grp = sc->sc_queues[0].q_rg.grp_id;
	sc->sc_vnic.mru = BNXT_MAX_MTU;
	sc->sc_vnic.cos_rule = (uint16_t)HWRM_NA_SIGNATURE;
	sc->sc_vnic.lb_rule = (uint16_t)HWRM_NA_SIGNATURE;
	sc->sc_vnic.flags = BNXT_VNIC_FLAG_DEFAULT |
	    BNXT_VNIC_FLAG_VLAN_STRIP;
	if (bnxt_hwrm_vnic_alloc(sc, &sc->sc_vnic) != 0) {
		printf("%s: failed to allocate vnic\n", DEVNAME(sc));
		goto dealloc_vnic_ctx;
	}

	if (bnxt_hwrm_vnic_cfg(sc, &sc->sc_vnic) != 0) {
		printf("%s: failed to configure vnic\n", DEVNAME(sc));
		goto dealloc_vnic;
	}

	if (bnxt_hwrm_vnic_cfg_placement(sc, &sc->sc_vnic) != 0) {
		printf("%s: failed to configure vnic placement mode\n",
		    DEVNAME(sc));
		goto dealloc_vnic;
	}

	sc->sc_vnic.filter_id = -1;
	if (bnxt_hwrm_set_filter(sc, &sc->sc_vnic) != 0) {
		printf("%s: failed to set vnic filter\n", DEVNAME(sc));
		goto dealloc_vnic;
	}

	if (sc->sc_nqueues > 1) {
		uint16_t *rss_table = (BNXT_DMA_KVA(sc->sc_rx_cfg) + PAGE_SIZE);
		uint8_t *hash_key = (uint8_t *)(rss_table + HW_HASH_INDEX_SIZE);

		for (i = 0; i < HW_HASH_INDEX_SIZE; i++) {
			struct bnxt_queue *bq;

			bq = &sc->sc_queues[i % sc->sc_nqueues];
			rss_table[i] = htole16(bq->q_rg.grp_id);
		}
		stoeplitz_to_key(hash_key, HW_HASH_KEY_SIZE);

		if (bnxt_hwrm_vnic_rss_cfg(sc, &sc->sc_vnic,
		    HWRM_VNIC_RSS_CFG_INPUT_HASH_TYPE_IPV4 |
		    HWRM_VNIC_RSS_CFG_INPUT_HASH_TYPE_TCP_IPV4 |
		    HWRM_VNIC_RSS_CFG_INPUT_HASH_TYPE_IPV6 |
		    HWRM_VNIC_RSS_CFG_INPUT_HASH_TYPE_TCP_IPV6,
		    BNXT_DMA_DVA(sc->sc_rx_cfg) + PAGE_SIZE,
		    BNXT_DMA_DVA(sc->sc_rx_cfg) + PAGE_SIZE +
		    (HW_HASH_INDEX_SIZE * sizeof(uint16_t))) != 0) {
			printf("%s: failed to set RSS config\n", DEVNAME(sc));
			goto dealloc_vnic;
		}
	}

	bnxt_iff(sc);
	SET(ifp->if_flags, IFF_RUNNING);

	return;

dealloc_vnic:
	bnxt_hwrm_vnic_free(sc, &sc->sc_vnic);
dealloc_vnic_ctx:
	bnxt_hwrm_vnic_ctx_free(sc, &sc->sc_vnic.rss_id);

down_all_queues:
	i = sc->sc_nqueues;
down_queues:
	while (i-- > 0)
		bnxt_queue_down(sc, &sc->sc_queues[i]);

	bnxt_dmamem_free(sc, sc->sc_rx_cfg);
	sc->sc_rx_cfg = NULL;
free_stats:
	bnxt_dmamem_free(sc, sc->sc_stats_ctx_mem);
	sc->sc_stats_ctx_mem = NULL;
}

void
bnxt_down(struct bnxt_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int i;

	CLR(ifp->if_flags, IFF_RUNNING);

	intr_barrier(sc->sc_ih);

	for (i = 0; i < sc->sc_nqueues; i++) {
		ifq_clr_oactive(ifp->if_ifqs[i]);
		ifq_barrier(ifp->if_ifqs[i]);

		timeout_del_barrier(&sc->sc_queues[i].q_rx.rx_refill);

		if (sc->sc_intrmap != NULL)
			intr_barrier(sc->sc_queues[i].q_ihc);
	}

	bnxt_hwrm_free_filter(sc, &sc->sc_vnic);
	bnxt_hwrm_vnic_free(sc, &sc->sc_vnic);
	bnxt_hwrm_vnic_ctx_free(sc, &sc->sc_vnic.rss_id);

	for (i = 0; i < sc->sc_nqueues; i++)
		bnxt_queue_down(sc, &sc->sc_queues[i]);

	bnxt_dmamem_free(sc, sc->sc_rx_cfg);
	sc->sc_rx_cfg = NULL;

	bnxt_dmamem_free(sc, sc->sc_stats_ctx_mem);
	sc->sc_stats_ctx_mem = NULL;
}

void
bnxt_iff(struct bnxt_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	char *mc_list;
	uint32_t rx_mask, mc_count;

	rx_mask = HWRM_CFA_L2_SET_RX_MASK_INPUT_MASK_BCAST
	    | HWRM_CFA_L2_SET_RX_MASK_INPUT_MASK_MCAST
	    | HWRM_CFA_L2_SET_RX_MASK_INPUT_MASK_ANYVLAN_NONVLAN;

	mc_list = BNXT_DMA_KVA(sc->sc_rx_cfg);
	mc_count = 0;

	if (ifp->if_flags & IFF_PROMISC) {
		SET(ifp->if_flags, IFF_ALLMULTI);
		rx_mask |= HWRM_CFA_L2_SET_RX_MASK_INPUT_MASK_PROMISCUOUS;
	} else if ((sc->sc_ac.ac_multirangecnt > 0) ||
	    (sc->sc_ac.ac_multicnt > (PAGE_SIZE / ETHER_ADDR_LEN))) {
		SET(ifp->if_flags, IFF_ALLMULTI);
		rx_mask |= HWRM_CFA_L2_SET_RX_MASK_INPUT_MASK_ALL_MCAST;
	} else {
		CLR(ifp->if_flags, IFF_ALLMULTI);
		ETHER_FIRST_MULTI(step, &sc->sc_ac, enm);
		while (enm != NULL) {
			memcpy(mc_list, enm->enm_addrlo, ETHER_ADDR_LEN);
			mc_list += ETHER_ADDR_LEN;
			mc_count++;

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	bnxt_hwrm_cfa_l2_set_rx_mask(sc, sc->sc_vnic.id, rx_mask,
	    BNXT_DMA_DVA(sc->sc_rx_cfg), mc_count);
}

int
bnxt_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct bnxt_softc 	*sc = (struct bnxt_softc *)ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *)data;
	int			s, error = 0;

	s = splnet();
	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */

	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = ENETRESET;
			else
				bnxt_up(sc);
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				bnxt_down(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	case SIOCGIFRXR:
		error = bnxt_rxrinfo(sc, (struct if_rxrinfo *)ifr->ifr_data);
		break;

	case SIOCGIFSFFPAGE:
		error = bnxt_get_sffpage(sc, (struct if_sffpage *)data);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			bnxt_iff(sc);
		error = 0;
	}

	splx(s);

	return (error);
}

int
bnxt_rxrinfo(struct bnxt_softc *sc, struct if_rxrinfo *ifri)
{
	struct if_rxring_info *ifr;
	int i;
	int error;

	ifr = mallocarray(sc->sc_nqueues * 2, sizeof(*ifr), M_TEMP,
	    M_WAITOK | M_ZERO | M_CANFAIL);
	if (ifr == NULL)
		return (ENOMEM);

	for (i = 0; i < sc->sc_nqueues; i++) {
		ifr[(i * 2)].ifr_size = MCLBYTES;
		ifr[(i * 2)].ifr_info = sc->sc_queues[i].q_rx.rxr[0];

		ifr[(i * 2) + 1].ifr_size = BNXT_AG_BUFFER_SIZE;
		ifr[(i * 2) + 1].ifr_info = sc->sc_queues[i].q_rx.rxr[1];
	}

	error = if_rxr_info_ioctl(ifri, sc->sc_nqueues * 2, ifr);
	free(ifr, M_TEMP, sc->sc_nqueues * 2 * sizeof(*ifr));

	return (error);
}

int
bnxt_load_mbuf(struct bnxt_softc *sc, struct bnxt_slot *bs, struct mbuf *m)
{
	switch (bus_dmamap_load_mbuf(sc->sc_dmat, bs->bs_map, m,
	    BUS_DMA_STREAMING | BUS_DMA_NOWAIT)) {
	case 0:
		break;

	case EFBIG:
		if (m_defrag(m, M_DONTWAIT) == 0 &&
		    bus_dmamap_load_mbuf(sc->sc_dmat, bs->bs_map, m,
		    BUS_DMA_STREAMING | BUS_DMA_NOWAIT) == 0)
			break;

	default:
		return (1);
	}

	bs->bs_m = m;
	return (0);
}

void
bnxt_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct tx_bd_short *txring;
	struct tx_bd_long_hi *txhi;
	struct bnxt_tx_queue *tx = ifq->ifq_softc;
	struct bnxt_softc *sc = tx->tx_softc;
	struct bnxt_slot *bs;
	struct ether_extracted ext;
	bus_dmamap_t map;
	struct mbuf *m;
	u_int idx, free, used, laststart;
	uint16_t txflags, lflags;
	int i, slen;

	txring = (struct tx_bd_short *)BNXT_DMA_KVA(tx->tx_ring_mem);

	idx = tx->tx_ring_prod;
	free = tx->tx_ring_cons;
	if (free <= idx)
		free += tx->tx_ring.ring_size;
	free -= idx;

	used = 0;

	for (;;) {
		/* +1 for tx_bd_long_hi, + 1 to leave a slot free */
		if (used + BNXT_MAX_TX_SEGS + 2 > free) {
			ifq_set_oactive(ifq);
			break;
		}

		m = ifq_dequeue(ifq);
		if (m == NULL)
			break;

		bs = &tx->tx_slots[tx->tx_prod];
		if (bnxt_load_mbuf(sc, bs, m) != 0) {
			m_freem(m);
			ifp->if_oerrors++;
			continue;
		}

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
		map = bs->bs_map;
		bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);
		used += BNXT_TX_SLOTS(bs);

		/* first segment */
		laststart = idx;
		txring[idx].len = htole16(map->dm_segs[0].ds_len);
		txring[idx].opaque = tx->tx_prod;
		txring[idx].addr = htole64(map->dm_segs[0].ds_addr);
		if (m->m_pkthdr.csum_flags & M_TCP_TSO)
			slen = m->m_pkthdr.ph_mss;
		else
			slen = map->dm_mapsize;

		if (slen < 512)
			txflags = TX_BD_LONG_FLAGS_LHINT_LT512;
		else if (slen < 1024)
			txflags = TX_BD_LONG_FLAGS_LHINT_LT1K;
		else if (slen < 2048)
			txflags = TX_BD_LONG_FLAGS_LHINT_LT2K;
		else
			txflags = TX_BD_LONG_FLAGS_LHINT_GTE2K;
		txflags |= TX_BD_LONG_TYPE_TX_BD_LONG |
		    TX_BD_LONG_FLAGS_NO_CMPL;
		txflags |= (BNXT_TX_SLOTS(bs) << TX_BD_LONG_FLAGS_BD_CNT_SFT) &
		    TX_BD_LONG_FLAGS_BD_CNT_MASK;
		if (map->dm_nsegs == 1)
			txflags |= TX_BD_SHORT_FLAGS_PACKET_END;
		txring[idx].flags_type = htole16(txflags);

		idx++;
		if (idx == tx->tx_ring.ring_size)
			idx = 0;

		/* long tx descriptor */
		txhi = (struct tx_bd_long_hi *)&txring[idx];
		memset(txhi, 0, sizeof(*txhi));

		lflags = 0;
		if (m->m_pkthdr.csum_flags & M_TCP_TSO) {
			uint16_t hdrsize;
			uint32_t outlen;
			uint32_t paylen;

			ether_extract_headers(m, &ext);
			if (ext.tcp && m->m_pkthdr.ph_mss > 0) {
				lflags |= TX_BD_LONG_LFLAGS_LSO;
				hdrsize = sizeof(*ext.eh);
				if (ext.ip4 || ext.ip6)
					hdrsize += ext.iphlen;
				else
					tcpstat_inc(tcps_outbadtso);

				hdrsize += ext.tcphlen;
				txhi->hdr_size = htole16(hdrsize / 2);

				outlen = m->m_pkthdr.ph_mss;
				txhi->mss = htole32(outlen);

				paylen = m->m_pkthdr.len - hdrsize;
				tcpstat_add(tcps_outpkttso,
				    (paylen + outlen + 1) / outlen);
			} else {
				tcpstat_inc(tcps_outbadtso);
			}
		} else {
			if (m->m_pkthdr.csum_flags & (M_UDP_CSUM_OUT |
			    M_TCP_CSUM_OUT))
				lflags |= TX_BD_LONG_LFLAGS_TCP_UDP_CHKSUM;
			if (m->m_pkthdr.csum_flags & M_IPV4_CSUM_OUT)
				lflags |= TX_BD_LONG_LFLAGS_IP_CHKSUM;
		}
		txhi->lflags = htole16(lflags);

#if NVLAN > 0
		if (m->m_flags & M_VLANTAG) {
			txhi->cfa_meta = htole32(m->m_pkthdr.ether_vtag |
			    TX_BD_LONG_CFA_META_VLAN_TPID_TPID8100 |
			    TX_BD_LONG_CFA_META_KEY_VLAN_TAG);
		}
#endif

		idx++;
		if (idx == tx->tx_ring.ring_size)
			idx = 0;

		/* remaining segments */
		txflags = TX_BD_SHORT_TYPE_TX_BD_SHORT;
		for (i = 1; i < map->dm_nsegs; i++) {
			if (i == map->dm_nsegs - 1)
				txflags |= TX_BD_SHORT_FLAGS_PACKET_END;
			txring[idx].flags_type = htole16(txflags);

			txring[idx].len =
			    htole16(bs->bs_map->dm_segs[i].ds_len);
			txring[idx].opaque = tx->tx_prod;
			txring[idx].addr =
			    htole64(bs->bs_map->dm_segs[i].ds_addr);

			idx++;
			if (idx == tx->tx_ring.ring_size)
				idx = 0;
		}

		if (++tx->tx_prod >= tx->tx_ring.ring_size)
			tx->tx_prod = 0;
	}

	/* unset NO_CMPL on the first bd of the last packet */
	if (used != 0) {
		txring[laststart].flags_type &=
		    ~htole16(TX_BD_SHORT_FLAGS_NO_CMPL);
	}

	bnxt_write_tx_doorbell(sc, &tx->tx_ring, idx);
	tx->tx_ring_prod = idx;
}

void
bnxt_handle_async_event(struct bnxt_softc *sc, struct cmpl_base *cmpl)
{
	struct hwrm_async_event_cmpl *ae = (struct hwrm_async_event_cmpl *)cmpl;
	uint16_t type = le16toh(ae->event_id);

	switch (type) {
	case HWRM_ASYNC_EVENT_CMPL_EVENT_ID_LINK_STATUS_CHANGE:
	case HWRM_ASYNC_EVENT_CMPL_EVENT_ID_LINK_SPEED_CHANGE:
	case HWRM_ASYNC_EVENT_CMPL_EVENT_ID_LINK_SPEED_CFG_CHANGE:
		bnxt_hwrm_port_phy_qcfg(sc, NULL);
		break;

	default:
		printf("%s: unexpected async event %x\n", DEVNAME(sc), type);
		break;
	}
}

struct cmpl_base *
bnxt_cpr_next_cmpl(struct bnxt_softc *sc, struct bnxt_cp_ring *cpr)
{
	struct cmpl_base *cmpl;
	uint32_t cons;
	int v_bit;

	cons = cpr->cons + 1;
	v_bit = cpr->v_bit;
	if (cons == cpr->ring.ring_size) {
		cons = 0;
		v_bit = !v_bit;
	}
	cmpl = &((struct cmpl_base *)cpr->ring.vaddr)[cons];

	if ((!!(cmpl->info3_v & htole32(CMPL_BASE_V))) != (!!v_bit))
		return (NULL);

	cpr->cons = cons;
	cpr->v_bit = v_bit;
	return (cmpl);
}

void
bnxt_cpr_commit(struct bnxt_softc *sc, struct bnxt_cp_ring *cpr)
{
	cpr->commit_cons = cpr->cons;
	cpr->commit_v_bit = cpr->v_bit;
}

void
bnxt_cpr_rollback(struct bnxt_softc *sc, struct bnxt_cp_ring *cpr)
{
	cpr->cons = cpr->commit_cons;
	cpr->v_bit = cpr->commit_v_bit;
}

int
bnxt_admin_intr(void *xsc)
{
	struct bnxt_softc *sc = (struct bnxt_softc *)xsc;
	struct bnxt_cp_ring *cpr = &sc->sc_cp_ring;
	struct cmpl_base *cmpl;
	uint16_t type;

	bnxt_write_cp_doorbell(sc, &cpr->ring, 0);
	cmpl = bnxt_cpr_next_cmpl(sc, cpr);
	while (cmpl != NULL) {
		type = le16toh(cmpl->type) & CMPL_BASE_TYPE_MASK;
		switch (type) {
		case CMPL_BASE_TYPE_HWRM_ASYNC_EVENT:
			bnxt_handle_async_event(sc, cmpl);
			break;
		default:
			printf("%s: unexpected completion type %u\n",
			    DEVNAME(sc), type);
		}

		bnxt_cpr_commit(sc, cpr);
		cmpl = bnxt_cpr_next_cmpl(sc, cpr);
	}

	bnxt_write_cp_doorbell_index(sc, &cpr->ring,
	    (cpr->commit_cons+1) % cpr->ring.ring_size, 1);
	return (1);
}

int
bnxt_intr(void *xq)
{
	struct bnxt_queue *q = (struct bnxt_queue *)xq;
	struct bnxt_softc *sc = q->q_sc;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct bnxt_cp_ring *cpr = &q->q_cp;
	struct bnxt_rx_queue *rx = &q->q_rx;
	struct bnxt_tx_queue *tx = &q->q_tx;
	struct cmpl_base *cmpl;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf_list mltcp = MBUF_LIST_INITIALIZER();
	uint16_t type;
	int rxfree, txfree, agfree, rv, rollback;

	bnxt_write_cp_doorbell(sc, &cpr->ring, 0);
	rxfree = 0;
	txfree = 0;
	agfree = 0;
	rv = -1;
	cmpl = bnxt_cpr_next_cmpl(sc, cpr);
	while (cmpl != NULL) {
		type = le16toh(cmpl->type) & CMPL_BASE_TYPE_MASK;
		rollback = 0;
		switch (type) {
		case CMPL_BASE_TYPE_HWRM_ASYNC_EVENT:
			bnxt_handle_async_event(sc, cmpl);
			break;
		case CMPL_BASE_TYPE_RX_L2:
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				rollback = bnxt_rx(sc, rx, cpr, &ml, &mltcp,
				    &rxfree, &agfree, cmpl);
			break;
		case CMPL_BASE_TYPE_TX_L2:
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				bnxt_txeof(sc, tx, &txfree, cmpl);
			break;
		default:
			printf("%s: unexpected completion type %u\n",
			    DEVNAME(sc), type);
		}

		if (rollback) {
			bnxt_cpr_rollback(sc, cpr);
			break;
		}
		rv = 1;
		bnxt_cpr_commit(sc, cpr);
		cmpl = bnxt_cpr_next_cmpl(sc, cpr);
	}

	/*
	 * comments in bnxtreg.h suggest we should be writing cpr->cons here,
	 * but writing cpr->cons + 1 makes it stop interrupting.
	 */
	bnxt_write_cp_doorbell_index(sc, &cpr->ring,
	    (cpr->commit_cons+1) % cpr->ring.ring_size, 1);

	if (rxfree != 0) {
		int livelocked = 0;

		rx->rx_cons += rxfree;
		if (rx->rx_cons >= rx->rx_ring.ring_size)
			rx->rx_cons -= rx->rx_ring.ring_size;

		rx->rx_ag_cons += agfree;
		if (rx->rx_ag_cons >= rx->rx_ag_ring.ring_size)
			rx->rx_ag_cons -= rx->rx_ag_ring.ring_size;

		if_rxr_put(&rx->rxr[0], rxfree);
		if_rxr_put(&rx->rxr[1], agfree);

		if (ifiq_input(rx->rx_ifiq, &mltcp))
			livelocked = 1;
		if (ifiq_input(rx->rx_ifiq, &ml))
			livelocked = 1;

		if (livelocked) {
			if_rxr_livelocked(&rx->rxr[0]);
			if_rxr_livelocked(&rx->rxr[1]);
		}

		bnxt_rx_fill(q);
		bnxt_rx_fill_ag(q);
		if ((rx->rx_cons == rx->rx_prod) ||
		    (rx->rx_ag_cons == rx->rx_ag_prod))
			timeout_add(&rx->rx_refill, 0);
	}
	if (txfree != 0) {
		if (ifq_is_oactive(tx->tx_ifq))
			ifq_restart(tx->tx_ifq);
	}
	return (rv);
}

void
bnxt_watchdog(struct ifnet *ifp)
{
}

void
bnxt_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct bnxt_softc *sc = (struct bnxt_softc *)ifp->if_softc;
	bnxt_hwrm_port_phy_qcfg(sc, ifmr);
}

uint64_t
bnxt_get_media_type(uint64_t speed, int phy_type)
{
	switch (phy_type) {
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_UNKNOWN:
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_BASECR:
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_25G_BASECR_CA_L:
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_25G_BASECR_CA_S:
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_25G_BASECR_CA_N:
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_100G_BASECR4:
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_40G_BASECR4:
		switch (speed) {
		case IF_Gbps(1):
			return IFM_1000_T;
		case IF_Gbps(10):
			return IFM_10G_SFP_CU;
		case IF_Gbps(25):
			return IFM_25G_CR;
		case IF_Gbps(40):
			return IFM_40G_CR4;
		case IF_Gbps(50):
			return IFM_50G_CR2;
		case IF_Gbps(100):
			return IFM_100G_CR4;
		}
		break;

	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_BASELR:
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_100G_BASELR4:
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_40G_BASELR4:
		switch (speed) {
		case IF_Gbps(1):
			return IFM_1000_LX;
		case IF_Gbps(10):
			return IFM_10G_LR;
		case IF_Gbps(25):
			return IFM_25G_LR;
		case IF_Gbps(40):
			return IFM_40G_LR4;
		case IF_Gbps(100):
			return IFM_100G_LR4;
		}
		break;

	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_BASESR:
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_25G_BASESR:
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_100G_BASESR4:
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_100G_BASESR10:
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_1G_BASESX:
		switch (speed) {
		case IF_Gbps(1):
			return IFM_1000_SX;
		case IF_Gbps(10):
			return IFM_10G_SR;
		case IF_Gbps(25):
			return IFM_25G_SR;
		case IF_Gbps(40):
			return IFM_40G_SR4;
		case IF_Gbps(100):
			return IFM_100G_SR4;
		}
		break;

	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_100G_BASEER4:
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_40G_BASEER4:
		switch (speed) {
		case IF_Gbps(10):
			return IFM_10G_ER;
		case IF_Gbps(25):
			return IFM_25G_ER;
		}
		/* missing IFM_40G_ER4, IFM_100G_ER4 */
		break;

	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_BASEKR4:
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_BASEKR2:
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_BASEKR:
		switch (speed) {
		case IF_Gbps(10):
			return IFM_10G_KR;
		case IF_Gbps(20):
			return IFM_20G_KR2;
		case IF_Gbps(25):
			return IFM_25G_KR;
		case IF_Gbps(40):
			return IFM_40G_KR4;
		case IF_Gbps(50):
			return IFM_50G_KR2;
		case IF_Gbps(100):
			return IFM_100G_KR4;
		}
		break;

	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_BASEKX:
		switch (speed) {
		case IF_Gbps(1):
			return IFM_1000_KX;
		case IF_Mbps(2500):
			return IFM_2500_KX;
		case IF_Gbps(10):
			return IFM_10G_KX4;
		}
		break;

	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_BASET:
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_BASETE:
	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_1G_BASET:
		switch (speed) {
		case IF_Mbps(10):
			return IFM_10_T;
		case IF_Mbps(100):
			return IFM_100_TX;
		case IF_Gbps(1):
			return IFM_1000_T;
		case IF_Mbps(2500):
			return IFM_2500_T;
		case IF_Gbps(10):
			return IFM_10G_T;
		}
		break;

	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_SGMIIEXTPHY:
		switch (speed) {
		case IF_Gbps(1):
			return IFM_1000_SGMII;
		}
		break;

	case HWRM_PORT_PHY_QCFG_OUTPUT_PHY_TYPE_40G_ACTIVE_CABLE:
		switch (speed) {
		case IF_Gbps(10):
			return IFM_10G_AOC;
		case IF_Gbps(25):
			return IFM_25G_AOC;
		case IF_Gbps(40):
			return IFM_40G_AOC;
		case IF_Gbps(100):
			return IFM_100G_AOC;
		}
		break;
	}

	return 0;
}

void
bnxt_add_media_type(struct bnxt_softc *sc, int supported_speeds, uint64_t speed, uint64_t ifmt)
{
	int speed_bit = 0;
	switch (speed) {
	case IF_Gbps(1):
		speed_bit = HWRM_PORT_PHY_QCFG_OUTPUT_SUPPORT_SPEEDS_1GB;
		break;
	case IF_Gbps(2):
		speed_bit = HWRM_PORT_PHY_QCFG_OUTPUT_SUPPORT_SPEEDS_2GB;
		break;
	case IF_Mbps(2500):
		speed_bit = HWRM_PORT_PHY_QCFG_OUTPUT_SUPPORT_SPEEDS_2_5GB;
		break;
	case IF_Gbps(10):
		speed_bit = HWRM_PORT_PHY_QCFG_OUTPUT_SUPPORT_SPEEDS_10GB;
		break;
	case IF_Gbps(20):
		speed_bit = HWRM_PORT_PHY_QCFG_OUTPUT_SUPPORT_SPEEDS_20GB;
		break;
	case IF_Gbps(25):
		speed_bit = HWRM_PORT_PHY_QCFG_OUTPUT_SUPPORT_SPEEDS_25GB;
		break;
	case IF_Gbps(40):
		speed_bit = HWRM_PORT_PHY_QCFG_OUTPUT_SUPPORT_SPEEDS_40GB;
		break;
	case IF_Gbps(50):
		speed_bit = HWRM_PORT_PHY_QCFG_OUTPUT_SUPPORT_SPEEDS_50GB;
		break;
	case IF_Gbps(100):
		speed_bit = HWRM_PORT_PHY_QCFG_OUTPUT_SUPPORT_SPEEDS_100GB;
		break;
	}
	if (supported_speeds & speed_bit)
		ifmedia_add(&sc->sc_media, IFM_ETHER | ifmt, 0, NULL);
}

int
bnxt_hwrm_port_phy_qcfg(struct bnxt_softc *softc, struct ifmediareq *ifmr)
{
	struct ifnet *ifp = &softc->sc_ac.ac_if;
	struct hwrm_port_phy_qcfg_input req = {0};
	struct hwrm_port_phy_qcfg_output *resp =
	    BNXT_DMA_KVA(softc->sc_cmd_resp);
	int link_state = LINK_STATE_DOWN;
	uint64_t speeds[] = {
		IF_Gbps(1), IF_Gbps(2), IF_Mbps(2500), IF_Gbps(10), IF_Gbps(20),
		IF_Gbps(25), IF_Gbps(40), IF_Gbps(50), IF_Gbps(100)
	};
	uint64_t media_type;
	int duplex;
	int rc = 0;
	int i;

	BNXT_HWRM_LOCK(softc);
	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_PORT_PHY_QCFG);

	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc) {
		printf("%s: failed to query port phy config\n", DEVNAME(softc));
		goto exit;
	}

	if (softc->sc_hwrm_ver > 0x10800)
		duplex = resp->duplex_state;
	else
		duplex = resp->duplex_cfg;

	if (resp->link == HWRM_PORT_PHY_QCFG_OUTPUT_LINK_LINK) {
		if (duplex == HWRM_PORT_PHY_QCFG_OUTPUT_DUPLEX_STATE_HALF)
			link_state = LINK_STATE_HALF_DUPLEX;
		else
			link_state = LINK_STATE_FULL_DUPLEX;

		switch (resp->link_speed) {
		case HWRM_PORT_PHY_QCFG_OUTPUT_LINK_SPEED_10MB:
			ifp->if_baudrate = IF_Mbps(10);
			break;
		case HWRM_PORT_PHY_QCFG_OUTPUT_LINK_SPEED_100MB:
			ifp->if_baudrate = IF_Mbps(100);
			break;
		case HWRM_PORT_PHY_QCFG_OUTPUT_LINK_SPEED_1GB:
			ifp->if_baudrate = IF_Gbps(1);
			break;
		case HWRM_PORT_PHY_QCFG_OUTPUT_LINK_SPEED_2GB:
			ifp->if_baudrate = IF_Gbps(2);
			break;
		case HWRM_PORT_PHY_QCFG_OUTPUT_LINK_SPEED_2_5GB:
			ifp->if_baudrate = IF_Mbps(2500);
			break;
		case HWRM_PORT_PHY_QCFG_OUTPUT_LINK_SPEED_10GB:
			ifp->if_baudrate = IF_Gbps(10);
			break;
		case HWRM_PORT_PHY_QCFG_OUTPUT_LINK_SPEED_20GB:
			ifp->if_baudrate = IF_Gbps(20);
			break;
		case HWRM_PORT_PHY_QCFG_OUTPUT_LINK_SPEED_25GB:
			ifp->if_baudrate = IF_Gbps(25);
			break;
		case HWRM_PORT_PHY_QCFG_OUTPUT_LINK_SPEED_40GB:
			ifp->if_baudrate = IF_Gbps(40);
			break;
		case HWRM_PORT_PHY_QCFG_OUTPUT_LINK_SPEED_50GB:
			ifp->if_baudrate = IF_Gbps(50);
			break;
		case HWRM_PORT_PHY_QCFG_OUTPUT_LINK_SPEED_100GB:
			ifp->if_baudrate = IF_Gbps(100);
			break;
		}
	}

	ifmedia_delete_instance(&softc->sc_media, IFM_INST_ANY);
	for (i = 0; i < nitems(speeds); i++) {
		media_type = bnxt_get_media_type(speeds[i], resp->phy_type);
		if (media_type != 0)
			bnxt_add_media_type(softc, resp->support_speeds,
			    speeds[i], media_type);
	}
	ifmedia_add(&softc->sc_media, IFM_ETHER|IFM_AUTO, 0, NULL);
	ifmedia_set(&softc->sc_media, IFM_ETHER|IFM_AUTO);

	if (ifmr != NULL) {
		ifmr->ifm_status = IFM_AVALID;
		if (LINK_STATE_IS_UP(ifp->if_link_state)) {
			ifmr->ifm_status |= IFM_ACTIVE;
			ifmr->ifm_active = IFM_ETHER | IFM_AUTO;
			if (resp->pause & HWRM_PORT_PHY_QCFG_OUTPUT_PAUSE_TX)
				ifmr->ifm_active |= IFM_ETH_TXPAUSE;
			if (resp->pause & HWRM_PORT_PHY_QCFG_OUTPUT_PAUSE_RX)
				ifmr->ifm_active |= IFM_ETH_RXPAUSE;
			if (duplex == HWRM_PORT_PHY_QCFG_OUTPUT_DUPLEX_STATE_HALF)
				ifmr->ifm_active |= IFM_HDX;
			else
				ifmr->ifm_active |= IFM_FDX;

			media_type = bnxt_get_media_type(ifp->if_baudrate, resp->phy_type);
			if (media_type != 0)
				ifmr->ifm_active |= media_type;
		}
	}

exit:
	BNXT_HWRM_UNLOCK(softc);

	if (rc == 0 && (link_state != ifp->if_link_state)) {
		ifp->if_link_state = link_state;
		if_link_state_change(ifp);
	}

	return rc;
}

int
bnxt_media_change(struct ifnet *ifp)
{
	struct bnxt_softc *sc = (struct bnxt_softc *)ifp->if_softc;
	struct hwrm_port_phy_cfg_input req = {0};
	uint64_t link_speed;

	if (IFM_TYPE(sc->sc_media.ifm_media) != IFM_ETHER)
		return EINVAL;

	if (sc->sc_flags & BNXT_FLAG_NPAR)
		return ENODEV;

	bnxt_hwrm_cmd_hdr_init(sc, &req, HWRM_PORT_PHY_CFG);

	switch (IFM_SUBTYPE(sc->sc_media.ifm_media)) {
	case IFM_100G_CR4:
	case IFM_100G_SR4:
	case IFM_100G_KR4:
	case IFM_100G_LR4:
	case IFM_100G_AOC:
		link_speed = HWRM_PORT_PHY_QCFG_OUTPUT_FORCE_LINK_SPEED_100GB;
		break;

	case IFM_50G_CR2:
	case IFM_50G_KR2:
		link_speed = HWRM_PORT_PHY_QCFG_OUTPUT_FORCE_LINK_SPEED_50GB;
		break;

	case IFM_40G_CR4:
	case IFM_40G_SR4:
	case IFM_40G_LR4:
	case IFM_40G_KR4:
	case IFM_40G_AOC:
		link_speed = HWRM_PORT_PHY_QCFG_OUTPUT_FORCE_LINK_SPEED_40GB;
		break;

	case IFM_25G_CR:
	case IFM_25G_KR:
	case IFM_25G_SR:
	case IFM_25G_LR:
	case IFM_25G_ER:
	case IFM_25G_AOC:
		link_speed = HWRM_PORT_PHY_QCFG_OUTPUT_FORCE_LINK_SPEED_25GB;
		break;

	case IFM_10G_LR:
	case IFM_10G_SR:
	case IFM_10G_CX4:
	case IFM_10G_T:
	case IFM_10G_SFP_CU:
	case IFM_10G_LRM:
	case IFM_10G_KX4:
	case IFM_10G_KR:
	case IFM_10G_CR1:
	case IFM_10G_ER:
	case IFM_10G_AOC:
		link_speed = HWRM_PORT_PHY_QCFG_OUTPUT_FORCE_LINK_SPEED_10GB;
		break;

	case IFM_2500_SX:
	case IFM_2500_KX:
	case IFM_2500_T:
		link_speed = HWRM_PORT_PHY_QCFG_OUTPUT_FORCE_LINK_SPEED_2_5GB;
		break;

	case IFM_1000_T:
	case IFM_1000_LX:
	case IFM_1000_SX:
	case IFM_1000_CX:
	case IFM_1000_KX:
		link_speed = HWRM_PORT_PHY_QCFG_OUTPUT_FORCE_LINK_SPEED_1GB;
		break;

	case IFM_100_TX:
		link_speed = HWRM_PORT_PHY_QCFG_OUTPUT_FORCE_LINK_SPEED_100MB;
		break;

	default:
		link_speed = 0;
	}

	req.enables |= htole32(HWRM_PORT_PHY_CFG_INPUT_ENABLES_AUTO_DUPLEX);
	req.auto_duplex = HWRM_PORT_PHY_CFG_INPUT_AUTO_DUPLEX_BOTH;
	if (link_speed == 0) {
		req.auto_mode |=
		    HWRM_PORT_PHY_CFG_INPUT_AUTO_MODE_ALL_SPEEDS;
		req.flags |=
		    htole32(HWRM_PORT_PHY_CFG_INPUT_FLAGS_RESTART_AUTONEG);
		req.enables |=
		    htole32(HWRM_PORT_PHY_CFG_INPUT_ENABLES_AUTO_MODE);
	} else {
		req.force_link_speed = htole16(link_speed);
		req.flags |= htole32(HWRM_PORT_PHY_CFG_INPUT_FLAGS_FORCE);
	}
	req.flags |= htole32(HWRM_PORT_PHY_CFG_INPUT_FLAGS_RESET_PHY);

	return hwrm_send_message(sc, &req, sizeof(req));
}

int
bnxt_media_autonegotiate(struct bnxt_softc *sc)
{
	struct hwrm_port_phy_cfg_input req = {0};

	if (sc->sc_flags & BNXT_FLAG_NPAR)
		return ENODEV;

	bnxt_hwrm_cmd_hdr_init(sc, &req, HWRM_PORT_PHY_CFG);
	req.auto_mode |= HWRM_PORT_PHY_CFG_INPUT_AUTO_MODE_ALL_SPEEDS;
	req.auto_duplex = HWRM_PORT_PHY_CFG_INPUT_AUTO_DUPLEX_BOTH;
	req.enables |= htole32(HWRM_PORT_PHY_CFG_INPUT_ENABLES_AUTO_MODE |
	    HWRM_PORT_PHY_CFG_INPUT_ENABLES_AUTO_DUPLEX);
	req.flags |= htole32(HWRM_PORT_PHY_CFG_INPUT_FLAGS_RESTART_AUTONEG);
	req.flags |= htole32(HWRM_PORT_PHY_CFG_INPUT_FLAGS_RESET_PHY);

	return hwrm_send_message(sc, &req, sizeof(req));
}


void
bnxt_mark_cpr_invalid(struct bnxt_cp_ring *cpr)
{
	struct cmpl_base *cmp = (void *)cpr->ring.vaddr;
	int i;

	for (i = 0; i < cpr->ring.ring_size; i++)
		cmp[i].info3_v = !cpr->v_bit;
}

void
bnxt_write_cp_doorbell(struct bnxt_softc *sc, struct bnxt_ring *ring,
    int enable)
{
	uint32_t val = CMPL_DOORBELL_KEY_CMPL;
	if (enable == 0)
		val |= CMPL_DOORBELL_MASK;

	bus_space_barrier(sc->sc_db_t, sc->sc_db_h, ring->doorbell, 4,
	    BUS_SPACE_BARRIER_WRITE);
	bus_space_barrier(sc->sc_db_t, sc->sc_db_h, 0, sc->sc_db_s,
	    BUS_SPACE_BARRIER_WRITE);
	bus_space_write_4(sc->sc_db_t, sc->sc_db_h, ring->doorbell,
	    htole32(val));
}

void
bnxt_write_cp_doorbell_index(struct bnxt_softc *sc, struct bnxt_ring *ring,
    uint32_t index, int enable)
{
	uint32_t val = CMPL_DOORBELL_KEY_CMPL | CMPL_DOORBELL_IDX_VALID |
	    (index & CMPL_DOORBELL_IDX_MASK);
	if (enable == 0)
		val |= CMPL_DOORBELL_MASK;
	bus_space_barrier(sc->sc_db_t, sc->sc_db_h, ring->doorbell, 4,
	    BUS_SPACE_BARRIER_WRITE);
	bus_space_write_4(sc->sc_db_t, sc->sc_db_h, ring->doorbell,
	    htole32(val));
	bus_space_barrier(sc->sc_db_t, sc->sc_db_h, 0, sc->sc_db_s,
	    BUS_SPACE_BARRIER_WRITE);
}

void
bnxt_write_rx_doorbell(struct bnxt_softc *sc, struct bnxt_ring *ring, int index)
{
	uint32_t val = RX_DOORBELL_KEY_RX | index;
	bus_space_barrier(sc->sc_db_t, sc->sc_db_h, ring->doorbell, 4,
	    BUS_SPACE_BARRIER_WRITE);
	bus_space_write_4(sc->sc_db_t, sc->sc_db_h, ring->doorbell,
	    htole32(val));

	/* second write isn't necessary on all hardware */
	bus_space_barrier(sc->sc_db_t, sc->sc_db_h, ring->doorbell, 4,
	    BUS_SPACE_BARRIER_WRITE);
	bus_space_write_4(sc->sc_db_t, sc->sc_db_h, ring->doorbell,
	    htole32(val));
}

void
bnxt_write_tx_doorbell(struct bnxt_softc *sc, struct bnxt_ring *ring, int index)
{
	uint32_t val = TX_DOORBELL_KEY_TX | index;
	bus_space_barrier(sc->sc_db_t, sc->sc_db_h, ring->doorbell, 4,
	    BUS_SPACE_BARRIER_WRITE);
	bus_space_write_4(sc->sc_db_t, sc->sc_db_h, ring->doorbell,
	    htole32(val));

	/* second write isn't necessary on all hardware */
	bus_space_barrier(sc->sc_db_t, sc->sc_db_h, ring->doorbell, 4,
	    BUS_SPACE_BARRIER_WRITE);
	bus_space_write_4(sc->sc_db_t, sc->sc_db_h, ring->doorbell,
	    htole32(val));
}

u_int
bnxt_rx_fill_slots(struct bnxt_softc *sc, struct bnxt_ring *ring, void *ring_mem,
    struct bnxt_slot *slots, uint *prod, int bufsize, uint16_t bdtype,
    u_int nslots)
{
	struct rx_prod_pkt_bd *rxring;
	struct bnxt_slot *bs;
	struct mbuf *m;
	uint p, fills;

	rxring = (struct rx_prod_pkt_bd *)ring_mem;
	p = *prod;
	for (fills = 0; fills < nslots; fills++) {
		bs = &slots[p];
		m = MCLGETL(NULL, M_DONTWAIT, bufsize);
		if (m == NULL)
			break;

		m->m_len = m->m_pkthdr.len = bufsize;
		if (bus_dmamap_load_mbuf(sc->sc_dmat, bs->bs_map, m,
		    BUS_DMA_NOWAIT) != 0) {
			m_freem(m);
			break;
		}
		bs->bs_m = m;

		rxring[p].flags_type = htole16(bdtype);
		rxring[p].len = htole16(bufsize);
		rxring[p].opaque = p;
		rxring[p].addr = htole64(bs->bs_map->dm_segs[0].ds_addr);

		if (++p >= ring->ring_size)
			p = 0;
	}

	if (fills != 0)
		bnxt_write_rx_doorbell(sc, ring, p);
	*prod = p;

	return (nslots - fills);
}

int
bnxt_rx_fill(struct bnxt_queue *q)
{
	struct bnxt_rx_queue *rx = &q->q_rx;
	struct bnxt_softc *sc = q->q_sc;
	u_int slots;
	int rv = 0;

	slots = if_rxr_get(&rx->rxr[0], rx->rx_ring.ring_size);
	if (slots > 0) {
		slots = bnxt_rx_fill_slots(sc, &rx->rx_ring,
		    BNXT_DMA_KVA(rx->rx_ring_mem), rx->rx_slots,
		    &rx->rx_prod, MCLBYTES,
		    RX_PROD_PKT_BD_TYPE_RX_PROD_PKT, slots);
		if_rxr_put(&rx->rxr[0], slots);
	} else
		rv = 1;

	return (rv);
}

int
bnxt_rx_fill_ag(struct bnxt_queue *q)
{
	struct bnxt_rx_queue *rx = &q->q_rx;
	struct bnxt_softc *sc = q->q_sc;
	u_int slots;
	int rv = 0;

	slots = if_rxr_get(&rx->rxr[1],  rx->rx_ag_ring.ring_size);
	if (slots > 0) {
		slots = bnxt_rx_fill_slots(sc, &rx->rx_ag_ring,
		    BNXT_DMA_KVA(rx->rx_ring_mem) + PAGE_SIZE,
		    rx->rx_ag_slots, &rx->rx_ag_prod,
		    BNXT_AG_BUFFER_SIZE,
		    RX_PROD_AGG_BD_TYPE_RX_PROD_AGG, slots);
		if_rxr_put(&rx->rxr[1], slots);
	} else
		rv = 1;

	return (rv);
}

void
bnxt_refill(void *xq)
{
	struct bnxt_queue *q = xq;
	struct bnxt_rx_queue *rx = &q->q_rx;

	if (rx->rx_cons == rx->rx_prod)
		bnxt_rx_fill(q);

	if (rx->rx_ag_cons == rx->rx_ag_prod)
		bnxt_rx_fill_ag(q);

	if ((rx->rx_cons == rx->rx_prod) ||
	    (rx->rx_ag_cons == rx->rx_ag_prod))
		timeout_add(&rx->rx_refill, 1);
}

int
bnxt_rx(struct bnxt_softc *sc, struct bnxt_rx_queue *rx,
    struct bnxt_cp_ring *cpr, struct mbuf_list *ml, struct mbuf_list *mltcp,
    int *slots, int *agslots, struct cmpl_base *cmpl)
{
	struct mbuf *m, *am;
	struct bnxt_slot *bs;
	struct rx_pkt_cmpl *rxlo = (struct rx_pkt_cmpl *)cmpl;
	struct rx_pkt_cmpl_hi *rxhi;
	struct rx_abuf_cmpl *ag;
	uint32_t flags;
	uint16_t errors;

	/* second part of the rx completion */
	rxhi = (struct rx_pkt_cmpl_hi *)bnxt_cpr_next_cmpl(sc, cpr);
	if (rxhi == NULL) {
		return (1);
	}

	/* packets over 2k in size use an aggregation buffer completion too */
	ag = NULL;
	if ((rxlo->agg_bufs_v1 >> RX_PKT_CMPL_AGG_BUFS_SFT) != 0) {
		ag = (struct rx_abuf_cmpl *)bnxt_cpr_next_cmpl(sc, cpr);
		if (ag == NULL) {
			return (1);
		}
	}

	bs = &rx->rx_slots[rxlo->opaque];
	bus_dmamap_sync(sc->sc_dmat, bs->bs_map, 0, bs->bs_map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD);
	bus_dmamap_unload(sc->sc_dmat, bs->bs_map);

	m = bs->bs_m;
	bs->bs_m = NULL;
	m->m_pkthdr.len = m->m_len = letoh16(rxlo->len);
	(*slots)++;

	/* checksum flags */
	flags = lemtoh32(&rxhi->flags2);
	errors = lemtoh16(&rxhi->errors_v2);
	if ((flags & RX_PKT_CMPL_FLAGS2_IP_CS_CALC) != 0 &&
	    (errors & RX_PKT_CMPL_ERRORS_IP_CS_ERROR) == 0)
		m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;

	if ((flags & RX_PKT_CMPL_FLAGS2_L4_CS_CALC) != 0 &&
	    (errors & RX_PKT_CMPL_ERRORS_L4_CS_ERROR) == 0)
		m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_OK |
		    M_UDP_CSUM_IN_OK;

#if NVLAN > 0
	if ((flags & RX_PKT_CMPL_FLAGS2_META_FORMAT_MASK) ==
	    RX_PKT_CMPL_FLAGS2_META_FORMAT_VLAN) {
		m->m_pkthdr.ether_vtag = lemtoh16(&rxhi->metadata);
		m->m_flags |= M_VLANTAG;
	}
#endif

	if (lemtoh16(&rxlo->flags_type) & RX_PKT_CMPL_FLAGS_RSS_VALID) {
		m->m_pkthdr.ph_flowid = lemtoh32(&rxlo->rss_hash);
		m->m_pkthdr.csum_flags |= M_FLOWID;
	}

	if (ag != NULL) {
		bs = &rx->rx_ag_slots[ag->opaque];
		bus_dmamap_sync(sc->sc_dmat, bs->bs_map, 0,
		    bs->bs_map->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, bs->bs_map);

		am = bs->bs_m;
		bs->bs_m = NULL;
		am->m_len = letoh16(ag->len);
		m->m_next = am;
		m->m_pkthdr.len += am->m_len;
		(*agslots)++;
	}

#ifndef SMALL_KERNEL
	if (ISSET(sc->sc_ac.ac_if.if_xflags, IFXF_LRO) &&
	    ((lemtoh16(&rxlo->flags_type) & RX_PKT_CMPL_FLAGS_ITYPE_TCP) ==
	    RX_PKT_CMPL_FLAGS_ITYPE_TCP))
		tcp_softlro_glue(mltcp, m, &sc->sc_ac.ac_if);
	else
#endif
		ml_enqueue(ml, m);

	return (0);
}

void
bnxt_txeof(struct bnxt_softc *sc, struct bnxt_tx_queue *tx, int *txfree,
    struct cmpl_base *cmpl)
{
	struct tx_cmpl *txcmpl = (struct tx_cmpl *)cmpl;
	struct bnxt_slot *bs;
	bus_dmamap_t map;
	u_int idx, segs, last;

	idx = tx->tx_ring_cons;
	last = tx->tx_cons;
	do {
		bs = &tx->tx_slots[tx->tx_cons];
		map = bs->bs_map;

		segs = BNXT_TX_SLOTS(bs);
		bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, map);
		m_freem(bs->bs_m);
		bs->bs_m = NULL;

		idx += segs;
		(*txfree) += segs;
		if (idx >= tx->tx_ring.ring_size)
			idx -= tx->tx_ring.ring_size;

		last = tx->tx_cons;
		if (++tx->tx_cons >= tx->tx_ring.ring_size)
			tx->tx_cons = 0;

	} while (last != txcmpl->opaque);
	tx->tx_ring_cons = idx;
}

/* bnxt_hwrm.c */

int
bnxt_hwrm_err_map(uint16_t err)
{
	int rc;

	switch (err) {
	case HWRM_ERR_CODE_SUCCESS:
		return 0;
	case HWRM_ERR_CODE_INVALID_PARAMS:
	case HWRM_ERR_CODE_INVALID_FLAGS:
	case HWRM_ERR_CODE_INVALID_ENABLES:
		return EINVAL;
	case HWRM_ERR_CODE_RESOURCE_ACCESS_DENIED:
		return EACCES;
	case HWRM_ERR_CODE_RESOURCE_ALLOC_ERROR:
		return ENOMEM;
	case HWRM_ERR_CODE_CMD_NOT_SUPPORTED:
		return ENOSYS;
	case HWRM_ERR_CODE_FAIL:
		return EIO;
	case HWRM_ERR_CODE_HWRM_ERROR:
	case HWRM_ERR_CODE_UNKNOWN_ERR:
	default:
		return EIO;
	}

	return rc;
}

void
bnxt_hwrm_cmd_hdr_init(struct bnxt_softc *softc, void *request,
    uint16_t req_type)
{
	struct input *req = request;

	req->req_type = htole16(req_type);
	req->cmpl_ring = 0xffff;
	req->target_id = 0xffff;
	req->resp_addr = htole64(BNXT_DMA_DVA(softc->sc_cmd_resp));
}

int
_hwrm_send_message(struct bnxt_softc *softc, void *msg, uint32_t msg_len)
{
	struct input *req = msg;
	struct hwrm_err_output *resp = BNXT_DMA_KVA(softc->sc_cmd_resp);
	uint32_t *data = msg;
	int i;
	uint8_t *valid;
	uint16_t err;
	uint16_t max_req_len = HWRM_MAX_REQ_LEN;
	struct hwrm_short_input short_input = {0};

	/* TODO: DMASYNC in here. */
	req->seq_id = htole16(softc->sc_cmd_seq++);
	memset(resp, 0, PAGE_SIZE);

	if (softc->sc_flags & BNXT_FLAG_SHORT_CMD) {
		void *short_cmd_req = BNXT_DMA_KVA(softc->sc_cmd_resp);

		memcpy(short_cmd_req, req, msg_len);
		memset((uint8_t *) short_cmd_req + msg_len, 0,
		    softc->sc_max_req_len - msg_len);

		short_input.req_type = req->req_type;
		short_input.signature =
		    htole16(HWRM_SHORT_INPUT_SIGNATURE_SHORT_CMD);
		short_input.size = htole16(msg_len);
		short_input.req_addr =
		    htole64(BNXT_DMA_DVA(softc->sc_cmd_resp));

		data = (uint32_t *)&short_input;
		msg_len = sizeof(short_input);

		/* Sync memory write before updating doorbell */
		membar_sync();

		max_req_len = BNXT_HWRM_SHORT_REQ_LEN;
	}

	/* Write request msg to hwrm channel */
	for (i = 0; i < msg_len; i += 4) {
		bus_space_write_4(softc->sc_hwrm_t,
				  softc->sc_hwrm_h,
				  i, *data);
		data++;
	}

	/* Clear to the end of the request buffer */
	for (i = msg_len; i < max_req_len; i += 4)
		bus_space_write_4(softc->sc_hwrm_t, softc->sc_hwrm_h,
		    i, 0);

	/* Ring channel doorbell */
	bus_space_write_4(softc->sc_hwrm_t, softc->sc_hwrm_h, 0x100,
	    htole32(1));

	/* Check if response len is updated */
	for (i = 0; i < softc->sc_cmd_timeo; i++) {
		if (resp->resp_len && resp->resp_len <= 4096)
			break;
		DELAY(1000);
	}
	if (i >= softc->sc_cmd_timeo) {
		printf("%s: timeout sending %s: (timeout: %u) seq: %d\n",
		    DEVNAME(softc), GET_HWRM_REQ_TYPE(req->req_type),
		    softc->sc_cmd_timeo,
		    le16toh(req->seq_id));
		return ETIMEDOUT;
	}
	/* Last byte of resp contains the valid key */
	valid = (uint8_t *)resp + resp->resp_len - 1;
	for (i = 0; i < softc->sc_cmd_timeo; i++) {
		if (*valid == HWRM_RESP_VALID_KEY)
			break;
		DELAY(1000);
	}
	if (i >= softc->sc_cmd_timeo) {
		printf("%s: timeout sending %s: "
		    "(timeout: %u) msg {0x%x 0x%x} len:%d v: %d\n",
		    DEVNAME(softc), GET_HWRM_REQ_TYPE(req->req_type),
		    softc->sc_cmd_timeo, le16toh(req->req_type),
		    le16toh(req->seq_id), msg_len,
		    *valid);
		return ETIMEDOUT;
	}

	err = le16toh(resp->error_code);
	if (err) {
		/* HWRM_ERR_CODE_FAIL is a "normal" error, don't log */
		if (err != HWRM_ERR_CODE_FAIL) {
			printf("%s: %s command returned %s error.\n",
			    DEVNAME(softc),
			    GET_HWRM_REQ_TYPE(req->req_type),
			    GET_HWRM_ERROR_CODE(err));
		}
		return bnxt_hwrm_err_map(err);
	}

	return 0;
}


int
hwrm_send_message(struct bnxt_softc *softc, void *msg, uint32_t msg_len)
{
	int rc;

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, msg, msg_len);
	BNXT_HWRM_UNLOCK(softc);
	return rc;
}


int
bnxt_hwrm_queue_qportcfg(struct bnxt_softc *softc)
{
	struct hwrm_queue_qportcfg_input req = {0};
	struct hwrm_queue_qportcfg_output *resp =
	    BNXT_DMA_KVA(softc->sc_cmd_resp);
	int	rc = 0;

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_QUEUE_QPORTCFG);

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc)
		goto qportcfg_exit;

	if (!resp->max_configurable_queues) {
		rc = -EINVAL;
		goto qportcfg_exit;
	}

	softc->sc_tx_queue_id = resp->queue_id0;

qportcfg_exit:
	BNXT_HWRM_UNLOCK(softc);
	return rc;
}

int
bnxt_hwrm_ver_get(struct bnxt_softc *softc)
{
	struct hwrm_ver_get_input	req = {0};
	struct hwrm_ver_get_output	*resp =
	    BNXT_DMA_KVA(softc->sc_cmd_resp);
	int				rc;
#if 0
	const char nastr[] = "<not installed>";
	const char naver[] = "<N/A>";
#endif
	uint32_t dev_caps_cfg;

	softc->sc_max_req_len = HWRM_MAX_REQ_LEN;
	softc->sc_cmd_timeo = 1000;
	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_VER_GET);

	req.hwrm_intf_maj = HWRM_VERSION_MAJOR;
	req.hwrm_intf_min = HWRM_VERSION_MINOR;
	req.hwrm_intf_upd = HWRM_VERSION_UPDATE;

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc)
		goto fail;

	printf(": fw ver %d.%d.%d, ", resp->hwrm_fw_maj, resp->hwrm_fw_min,
	    resp->hwrm_fw_bld);

	softc->sc_hwrm_ver = (resp->hwrm_intf_maj << 16) |
	    (resp->hwrm_intf_min << 8) | resp->hwrm_intf_upd;
#if 0
	snprintf(softc->ver_info->hwrm_if_ver, BNXT_VERSTR_SIZE, "%d.%d.%d",
	    resp->hwrm_intf_maj, resp->hwrm_intf_min, resp->hwrm_intf_upd);
	softc->ver_info->hwrm_if_major = resp->hwrm_intf_maj;
	softc->ver_info->hwrm_if_minor = resp->hwrm_intf_min;
	softc->ver_info->hwrm_if_update = resp->hwrm_intf_upd;
	snprintf(softc->ver_info->hwrm_fw_ver, BNXT_VERSTR_SIZE, "%d.%d.%d",
	    resp->hwrm_fw_maj, resp->hwrm_fw_min, resp->hwrm_fw_bld);
	strlcpy(softc->ver_info->driver_hwrm_if_ver, HWRM_VERSION_STR,
	    BNXT_VERSTR_SIZE);
	strlcpy(softc->ver_info->hwrm_fw_name, resp->hwrm_fw_name,
	    BNXT_NAME_SIZE);

	if (resp->mgmt_fw_maj == 0 && resp->mgmt_fw_min == 0 &&
	    resp->mgmt_fw_bld == 0) {
		strlcpy(softc->ver_info->mgmt_fw_ver, naver, BNXT_VERSTR_SIZE);
		strlcpy(softc->ver_info->mgmt_fw_name, nastr, BNXT_NAME_SIZE);
	}
	else {
		snprintf(softc->ver_info->mgmt_fw_ver, BNXT_VERSTR_SIZE,
		    "%d.%d.%d", resp->mgmt_fw_maj, resp->mgmt_fw_min,
		    resp->mgmt_fw_bld);
		strlcpy(softc->ver_info->mgmt_fw_name, resp->mgmt_fw_name,
		    BNXT_NAME_SIZE);
	}
	if (resp->netctrl_fw_maj == 0 && resp->netctrl_fw_min == 0 &&
	    resp->netctrl_fw_bld == 0) {
		strlcpy(softc->ver_info->netctrl_fw_ver, naver,
		    BNXT_VERSTR_SIZE);
		strlcpy(softc->ver_info->netctrl_fw_name, nastr,
		    BNXT_NAME_SIZE);
	}
	else {
		snprintf(softc->ver_info->netctrl_fw_ver, BNXT_VERSTR_SIZE,
		    "%d.%d.%d", resp->netctrl_fw_maj, resp->netctrl_fw_min,
		    resp->netctrl_fw_bld);
		strlcpy(softc->ver_info->netctrl_fw_name, resp->netctrl_fw_name,
		    BNXT_NAME_SIZE);
	}
	if (resp->roce_fw_maj == 0 && resp->roce_fw_min == 0 &&
	    resp->roce_fw_bld == 0) {
		strlcpy(softc->ver_info->roce_fw_ver, naver, BNXT_VERSTR_SIZE);
		strlcpy(softc->ver_info->roce_fw_name, nastr, BNXT_NAME_SIZE);
	}
	else {
		snprintf(softc->ver_info->roce_fw_ver, BNXT_VERSTR_SIZE,
		    "%d.%d.%d", resp->roce_fw_maj, resp->roce_fw_min,
		    resp->roce_fw_bld);
		strlcpy(softc->ver_info->roce_fw_name, resp->roce_fw_name,
		    BNXT_NAME_SIZE);
	}
	softc->ver_info->chip_num = le16toh(resp->chip_num);
	softc->ver_info->chip_rev = resp->chip_rev;
	softc->ver_info->chip_metal = resp->chip_metal;
	softc->ver_info->chip_bond_id = resp->chip_bond_id;
	softc->ver_info->chip_type = resp->chip_platform_type;
#endif

	if (resp->max_req_win_len)
		softc->sc_max_req_len = le16toh(resp->max_req_win_len);
	if (resp->def_req_timeout)
		softc->sc_cmd_timeo = le16toh(resp->def_req_timeout);

	dev_caps_cfg = le32toh(resp->dev_caps_cfg);
	if ((dev_caps_cfg & HWRM_VER_GET_OUTPUT_DEV_CAPS_CFG_SHORT_CMD_SUPPORTED) &&
	    (dev_caps_cfg & HWRM_VER_GET_OUTPUT_DEV_CAPS_CFG_SHORT_CMD_REQUIRED))
		softc->sc_flags |= BNXT_FLAG_SHORT_CMD;

fail:
	BNXT_HWRM_UNLOCK(softc);
	return rc;
}


int
bnxt_hwrm_func_drv_rgtr(struct bnxt_softc *softc)
{
	struct hwrm_func_drv_rgtr_input req = {0};

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_FUNC_DRV_RGTR);

	req.enables = htole32(HWRM_FUNC_DRV_RGTR_INPUT_ENABLES_VER |
	    HWRM_FUNC_DRV_RGTR_INPUT_ENABLES_OS_TYPE);
	req.os_type = htole16(HWRM_FUNC_DRV_RGTR_INPUT_OS_TYPE_FREEBSD);

	req.ver_maj = 6;
	req.ver_min = 4;
	req.ver_upd = 0;

	return hwrm_send_message(softc, &req, sizeof(req));
}

#if 0

int
bnxt_hwrm_func_drv_unrgtr(struct bnxt_softc *softc, bool shutdown)
{
	struct hwrm_func_drv_unrgtr_input req = {0};

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_FUNC_DRV_UNRGTR);
	if (shutdown == true)
		req.flags |=
		    HWRM_FUNC_DRV_UNRGTR_INPUT_FLAGS_PREPARE_FOR_SHUTDOWN;
	return hwrm_send_message(softc, &req, sizeof(req));
}

#endif

int
bnxt_hwrm_func_qcaps(struct bnxt_softc *softc)
{
	int rc = 0;
	struct hwrm_func_qcaps_input req = {0};
	struct hwrm_func_qcaps_output *resp =
	    BNXT_DMA_KVA(softc->sc_cmd_resp);
	/* struct bnxt_func_info *func = &softc->func; */

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_FUNC_QCAPS);
	req.fid = htole16(0xffff);

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc)
		goto fail;

	if (resp->flags &
	    htole32(HWRM_FUNC_QCAPS_OUTPUT_FLAGS_WOL_MAGICPKT_SUPPORTED))
		softc->sc_flags |= BNXT_FLAG_WOL_CAP;

	memcpy(softc->sc_ac.ac_enaddr, resp->mac_address, 6);
	/*
	func->fw_fid = le16toh(resp->fid);
	memcpy(func->mac_addr, resp->mac_address, ETHER_ADDR_LEN);
	func->max_rsscos_ctxs = le16toh(resp->max_rsscos_ctx);
	func->max_cp_rings = le16toh(resp->max_cmpl_rings);
	func->max_tx_rings = le16toh(resp->max_tx_rings);
	func->max_rx_rings = le16toh(resp->max_rx_rings);
	func->max_hw_ring_grps = le32toh(resp->max_hw_ring_grps);
	if (!func->max_hw_ring_grps)
		func->max_hw_ring_grps = func->max_tx_rings;
	func->max_l2_ctxs = le16toh(resp->max_l2_ctxs);
	func->max_vnics = le16toh(resp->max_vnics);
	func->max_stat_ctxs = le16toh(resp->max_stat_ctx);
	if (BNXT_PF(softc)) {
		struct bnxt_pf_info *pf = &softc->pf;

		pf->port_id = le16toh(resp->port_id);
		pf->first_vf_id = le16toh(resp->first_vf_id);
		pf->max_vfs = le16toh(resp->max_vfs);
		pf->max_encap_records = le32toh(resp->max_encap_records);
		pf->max_decap_records = le32toh(resp->max_decap_records);
		pf->max_tx_em_flows = le32toh(resp->max_tx_em_flows);
		pf->max_tx_wm_flows = le32toh(resp->max_tx_wm_flows);
		pf->max_rx_em_flows = le32toh(resp->max_rx_em_flows);
		pf->max_rx_wm_flows = le32toh(resp->max_rx_wm_flows);
	}
	if (!_is_valid_ether_addr(func->mac_addr)) {
		device_printf(softc->dev, "Invalid ethernet address, generating random locally administered address\n");
		get_random_ether_addr(func->mac_addr);
	}
	*/

fail:
	BNXT_HWRM_UNLOCK(softc);
	return rc;
}


int 
bnxt_hwrm_func_qcfg(struct bnxt_softc *softc)
{
        struct hwrm_func_qcfg_input req = {0};
        /* struct hwrm_func_qcfg_output *resp =
	    BNXT_DMA_KVA(softc->sc_cmd_resp);
	struct bnxt_func_qcfg *fn_qcfg = &softc->fn_qcfg; */
        int rc;

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_FUNC_QCFG);
        req.fid = htole16(0xffff);
	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
        if (rc)
		goto fail;

	/*
	fn_qcfg->alloc_completion_rings = le16toh(resp->alloc_cmpl_rings);
	fn_qcfg->alloc_tx_rings = le16toh(resp->alloc_tx_rings);
	fn_qcfg->alloc_rx_rings = le16toh(resp->alloc_rx_rings);
	fn_qcfg->alloc_vnics = le16toh(resp->alloc_vnics);
	*/
fail:
	BNXT_HWRM_UNLOCK(softc);
        return rc;
}


int
bnxt_hwrm_func_reset(struct bnxt_softc *softc)
{
	struct hwrm_func_reset_input req = {0};

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_FUNC_RESET);
	req.enables = 0;

	return hwrm_send_message(softc, &req, sizeof(req));
}

int
bnxt_hwrm_vnic_cfg_placement(struct bnxt_softc *softc,
    struct bnxt_vnic_info *vnic)
{
	struct hwrm_vnic_plcmodes_cfg_input req = {0};

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_VNIC_PLCMODES_CFG);

	req.flags = htole32(
	    HWRM_VNIC_PLCMODES_CFG_INPUT_FLAGS_JUMBO_PLACEMENT);
	req.enables = htole32(
	    HWRM_VNIC_PLCMODES_CFG_INPUT_ENABLES_JUMBO_THRESH_VALID);
	req.vnic_id = htole16(vnic->id);
	req.jumbo_thresh = htole16(MCLBYTES);

	return hwrm_send_message(softc, &req, sizeof(req));
}

int
bnxt_hwrm_vnic_cfg(struct bnxt_softc *softc, struct bnxt_vnic_info *vnic)
{
	struct hwrm_vnic_cfg_input req = {0};

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_VNIC_CFG);

	if (vnic->flags & BNXT_VNIC_FLAG_DEFAULT)
		req.flags |= htole32(HWRM_VNIC_CFG_INPUT_FLAGS_DEFAULT);
	if (vnic->flags & BNXT_VNIC_FLAG_BD_STALL)
		req.flags |= htole32(HWRM_VNIC_CFG_INPUT_FLAGS_BD_STALL_MODE);
	if (vnic->flags & BNXT_VNIC_FLAG_VLAN_STRIP)
		req.flags |= htole32(HWRM_VNIC_CFG_INPUT_FLAGS_VLAN_STRIP_MODE);
	req.enables = htole32(HWRM_VNIC_CFG_INPUT_ENABLES_DFLT_RING_GRP |
	    HWRM_VNIC_CFG_INPUT_ENABLES_RSS_RULE |
	    HWRM_VNIC_CFG_INPUT_ENABLES_MRU);
	req.vnic_id = htole16(vnic->id);
	req.dflt_ring_grp = htole16(vnic->def_ring_grp);
	req.rss_rule = htole16(vnic->rss_id);
	req.cos_rule = htole16(vnic->cos_rule);
	req.lb_rule = htole16(vnic->lb_rule);
	req.mru = htole16(vnic->mru);

	return hwrm_send_message(softc, &req, sizeof(req));
}

int
bnxt_hwrm_vnic_alloc(struct bnxt_softc *softc, struct bnxt_vnic_info *vnic)
{
	struct hwrm_vnic_alloc_input req = {0};
	struct hwrm_vnic_alloc_output *resp =
	    BNXT_DMA_KVA(softc->sc_cmd_resp);
	int rc;

	if (vnic->id != (uint16_t)HWRM_NA_SIGNATURE) {
		printf("%s: attempt to re-allocate vnic %04x\n",
		    DEVNAME(softc), vnic->id);
		return EINVAL;
	}

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_VNIC_ALLOC);

	if (vnic->flags & BNXT_VNIC_FLAG_DEFAULT)
		req.flags = htole32(HWRM_VNIC_ALLOC_INPUT_FLAGS_DEFAULT);

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc)
		goto fail;

	vnic->id = le32toh(resp->vnic_id);

fail:
	BNXT_HWRM_UNLOCK(softc);
	return rc;
}

int
bnxt_hwrm_vnic_free(struct bnxt_softc *softc, struct bnxt_vnic_info *vnic)
{
	struct hwrm_vnic_free_input req = {0};
	int rc;

	if (vnic->id == (uint16_t)HWRM_NA_SIGNATURE) {
		printf("%s: attempt to deallocate vnic %04x\n",
		    DEVNAME(softc), vnic->id);
		return (EINVAL);
	}

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_VNIC_FREE);
	req.vnic_id = htole16(vnic->id);

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc == 0)
		vnic->id = (uint16_t)HWRM_NA_SIGNATURE;
	BNXT_HWRM_UNLOCK(softc);

	return (rc);
}

int
bnxt_hwrm_vnic_ctx_alloc(struct bnxt_softc *softc, uint16_t *ctx_id)
{
	struct hwrm_vnic_rss_cos_lb_ctx_alloc_input req = {0};
	struct hwrm_vnic_rss_cos_lb_ctx_alloc_output *resp =
	    BNXT_DMA_KVA(softc->sc_cmd_resp);
	int rc;

	if (*ctx_id != (uint16_t)HWRM_NA_SIGNATURE) {
		printf("%s: attempt to re-allocate vnic ctx %04x\n",
		    DEVNAME(softc), *ctx_id);
		return EINVAL;
	}

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_VNIC_RSS_COS_LB_CTX_ALLOC);

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc)
		goto fail;

	*ctx_id = letoh16(resp->rss_cos_lb_ctx_id);

fail:
	BNXT_HWRM_UNLOCK(softc);
	return (rc);
}

int
bnxt_hwrm_vnic_ctx_free(struct bnxt_softc *softc, uint16_t *ctx_id)
{
	struct hwrm_vnic_rss_cos_lb_ctx_free_input req = {0};
	int rc;

	if (*ctx_id == (uint16_t)HWRM_NA_SIGNATURE) {
		printf("%s: attempt to deallocate vnic ctx %04x\n",
		    DEVNAME(softc), *ctx_id);
		return (EINVAL);
	}

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_VNIC_RSS_COS_LB_CTX_FREE);
	req.rss_cos_lb_ctx_id = htole32(*ctx_id);

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc == 0)
		*ctx_id = (uint16_t)HWRM_NA_SIGNATURE;
	BNXT_HWRM_UNLOCK(softc);
	return (rc);
}

int
bnxt_hwrm_ring_grp_alloc(struct bnxt_softc *softc, struct bnxt_grp_info *grp)
{
	struct hwrm_ring_grp_alloc_input req = {0};
	struct hwrm_ring_grp_alloc_output *resp;
	int rc = 0;

	if (grp->grp_id != HWRM_NA_SIGNATURE) {
		printf("%s: attempt to re-allocate ring group %04x\n",
		    DEVNAME(softc), grp->grp_id);
		return EINVAL;
	}

	resp = BNXT_DMA_KVA(softc->sc_cmd_resp);
	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_RING_GRP_ALLOC);
	req.cr = htole16(grp->cp_ring_id);
	req.rr = htole16(grp->rx_ring_id);
	req.ar = htole16(grp->ag_ring_id);
	req.sc = htole16(grp->stats_ctx);

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc)
		goto fail;

	grp->grp_id = letoh32(resp->ring_group_id);

fail:
	BNXT_HWRM_UNLOCK(softc);
	return rc;
}

int
bnxt_hwrm_ring_grp_free(struct bnxt_softc *softc, struct bnxt_grp_info *grp)
{
	struct hwrm_ring_grp_free_input req = {0};
	int rc = 0;

	if (grp->grp_id == HWRM_NA_SIGNATURE) {
		printf("%s: attempt to free ring group %04x\n",
		    DEVNAME(softc), grp->grp_id);
		return EINVAL;
	}

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_RING_GRP_FREE);
	req.ring_group_id = htole32(grp->grp_id);

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc == 0)
		grp->grp_id = HWRM_NA_SIGNATURE;

	BNXT_HWRM_UNLOCK(softc);
	return (rc);
}

/*
 * Ring allocation message to the firmware
 */
int
bnxt_hwrm_ring_alloc(struct bnxt_softc *softc, uint8_t type,
    struct bnxt_ring *ring, uint16_t cmpl_ring_id, uint32_t stat_ctx_id,
    int irq)
{
	struct hwrm_ring_alloc_input req = {0};
	struct hwrm_ring_alloc_output *resp;
	int rc;

	if (ring->phys_id != (uint16_t)HWRM_NA_SIGNATURE) {
		printf("%s: attempt to re-allocate ring %04x\n",
		    DEVNAME(softc), ring->phys_id);
		return EINVAL;
	}

	resp = BNXT_DMA_KVA(softc->sc_cmd_resp);
	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_RING_ALLOC);
	req.enables = htole32(0);
	req.fbo = htole32(0);

	if (stat_ctx_id != HWRM_NA_SIGNATURE) {
		req.enables |= htole32(
		    HWRM_RING_ALLOC_INPUT_ENABLES_STAT_CTX_ID_VALID);
		req.stat_ctx_id = htole32(stat_ctx_id);
	}
	req.ring_type = type;
	req.page_tbl_addr = htole64(ring->paddr);
	req.length = htole32(ring->ring_size);
	req.logical_id = htole16(ring->id);
	req.cmpl_ring_id = htole16(cmpl_ring_id);
	req.queue_id = htole16(softc->sc_tx_queue_id);
	req.int_mode = (softc->sc_flags & BNXT_FLAG_MSIX) ?
	    HWRM_RING_ALLOC_INPUT_INT_MODE_MSIX :
	    HWRM_RING_ALLOC_INPUT_INT_MODE_LEGACY;
	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc)
		goto fail;

	ring->phys_id = le16toh(resp->ring_id);

fail:
	BNXT_HWRM_UNLOCK(softc);
	return rc;
}

int
bnxt_hwrm_ring_free(struct bnxt_softc *softc, uint8_t type, struct bnxt_ring *ring)
{
	struct hwrm_ring_free_input req = {0};
	int rc;

	if (ring->phys_id == (uint16_t)HWRM_NA_SIGNATURE) {
		printf("%s: attempt to deallocate ring %04x\n",
		    DEVNAME(softc), ring->phys_id);
		return (EINVAL);
	}

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_RING_FREE);
	req.ring_type = type;
	req.ring_id = htole16(ring->phys_id);
	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc)
		goto fail;

	ring->phys_id = (uint16_t)HWRM_NA_SIGNATURE;
fail:
	BNXT_HWRM_UNLOCK(softc);
	return (rc);
}


int
bnxt_hwrm_stat_ctx_alloc(struct bnxt_softc *softc, struct bnxt_cp_ring *cpr,
    uint64_t paddr)
{
	struct hwrm_stat_ctx_alloc_input req = {0};
	struct hwrm_stat_ctx_alloc_output *resp;
	int rc = 0;

	if (cpr->stats_ctx_id != HWRM_NA_SIGNATURE) {
		printf("%s: attempt to re-allocate stats ctx %08x\n",
		    DEVNAME(softc), cpr->stats_ctx_id);
		return EINVAL;
	}

	resp = BNXT_DMA_KVA(softc->sc_cmd_resp);
	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_STAT_CTX_ALLOC);

	req.update_period_ms = htole32(1000);
	req.stats_dma_addr = htole64(paddr);

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc)
		goto fail;

	cpr->stats_ctx_id = le32toh(resp->stat_ctx_id);

fail:
	BNXT_HWRM_UNLOCK(softc);

	return rc;
}

int
bnxt_hwrm_stat_ctx_free(struct bnxt_softc *softc, struct bnxt_cp_ring *cpr)
{
	struct hwrm_stat_ctx_free_input req = {0};
	int rc = 0;

	if (cpr->stats_ctx_id == HWRM_NA_SIGNATURE) {
		printf("%s: attempt to free stats ctx %08x\n",
		    DEVNAME(softc), cpr->stats_ctx_id);
		return EINVAL;
	}

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_STAT_CTX_FREE);
	req.stat_ctx_id = htole32(cpr->stats_ctx_id);

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	BNXT_HWRM_UNLOCK(softc);

	if (rc == 0)
		cpr->stats_ctx_id = HWRM_NA_SIGNATURE;

	return (rc);
}

#if 0

int
bnxt_hwrm_port_qstats(struct bnxt_softc *softc)
{
	struct hwrm_port_qstats_input req = {0};
	int rc = 0;

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_PORT_QSTATS);

	req.port_id = htole16(softc->pf.port_id);
	req.rx_stat_host_addr = htole64(softc->hw_rx_port_stats.idi_paddr);
	req.tx_stat_host_addr = htole64(softc->hw_tx_port_stats.idi_paddr);

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	BNXT_HWRM_UNLOCK(softc);

	return rc;
}

#endif

int
bnxt_hwrm_cfa_l2_set_rx_mask(struct bnxt_softc *softc,
    uint32_t vnic_id, uint32_t rx_mask, uint64_t mc_addr, uint32_t mc_count)
{
	struct hwrm_cfa_l2_set_rx_mask_input req = {0};

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_CFA_L2_SET_RX_MASK);

	req.vnic_id = htole32(vnic_id);
	req.mask = htole32(rx_mask);
	req.mc_tbl_addr = htole64(mc_addr);
	req.num_mc_entries = htole32(mc_count);
	return hwrm_send_message(softc, &req, sizeof(req));
}

int
bnxt_hwrm_set_filter(struct bnxt_softc *softc, struct bnxt_vnic_info *vnic)
{
	struct hwrm_cfa_l2_filter_alloc_input	req = {0};
	struct hwrm_cfa_l2_filter_alloc_output	*resp;
	uint32_t enables = 0;
	int rc = 0;

	if (vnic->filter_id != -1) {
		printf("%s: attempt to re-allocate l2 ctx filter\n",
		    DEVNAME(softc));
		return EINVAL;
	}

	resp = BNXT_DMA_KVA(softc->sc_cmd_resp);
	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_CFA_L2_FILTER_ALLOC);

	req.flags = htole32(HWRM_CFA_L2_FILTER_ALLOC_INPUT_FLAGS_PATH_RX
	    | HWRM_CFA_L2_FILTER_ALLOC_INPUT_FLAGS_OUTERMOST);
	enables = HWRM_CFA_L2_FILTER_ALLOC_INPUT_ENABLES_L2_ADDR
	    | HWRM_CFA_L2_FILTER_ALLOC_INPUT_ENABLES_L2_ADDR_MASK
	    | HWRM_CFA_L2_FILTER_ALLOC_INPUT_ENABLES_DST_ID;
	req.enables = htole32(enables);
	req.dst_id = htole16(vnic->id);
	memcpy(req.l2_addr, softc->sc_ac.ac_enaddr, ETHER_ADDR_LEN);
	memset(&req.l2_addr_mask, 0xff, sizeof(req.l2_addr_mask));

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc)
		goto fail;

	vnic->filter_id = le64toh(resp->l2_filter_id);
	vnic->flow_id = le64toh(resp->flow_id);

fail:
	BNXT_HWRM_UNLOCK(softc);
	return (rc);
}

int
bnxt_hwrm_free_filter(struct bnxt_softc *softc, struct bnxt_vnic_info *vnic)
{
	struct hwrm_cfa_l2_filter_free_input req = {0};
	int rc = 0;

	if (vnic->filter_id == -1) {
		printf("%s: attempt to deallocate filter %llx\n",
		     DEVNAME(softc), vnic->filter_id);
		return (EINVAL);
	}

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_CFA_L2_FILTER_FREE);
	req.l2_filter_id = htole64(vnic->filter_id);

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc == 0)
		vnic->filter_id = -1;
	BNXT_HWRM_UNLOCK(softc);

	return (rc);
}


int
bnxt_hwrm_vnic_rss_cfg(struct bnxt_softc *softc, struct bnxt_vnic_info *vnic,
    uint32_t hash_type, daddr_t rss_table, daddr_t rss_key)
{
	struct hwrm_vnic_rss_cfg_input	req = {0};

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_VNIC_RSS_CFG);

	req.hash_type = htole32(hash_type);
	req.ring_grp_tbl_addr = htole64(rss_table);
	req.hash_key_tbl_addr = htole64(rss_key);
	req.rss_ctx_idx = htole16(vnic->rss_id);

	return hwrm_send_message(softc, &req, sizeof(req));
}

int
bnxt_cfg_async_cr(struct bnxt_softc *softc, struct bnxt_cp_ring *cpr)
{
	int rc = 0;
	
	if (1 /* BNXT_PF(softc) */) {
		struct hwrm_func_cfg_input req = {0};

		bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_FUNC_CFG);

		req.fid = htole16(0xffff);
		req.enables = htole32(HWRM_FUNC_CFG_INPUT_ENABLES_ASYNC_EVENT_CR);
		req.async_event_cr = htole16(cpr->ring.phys_id);

		rc = hwrm_send_message(softc, &req, sizeof(req));
	} else {
		struct hwrm_func_vf_cfg_input req = {0};

		bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_FUNC_VF_CFG);

		req.enables = htole32(HWRM_FUNC_VF_CFG_INPUT_ENABLES_ASYNC_EVENT_CR);
		req.async_event_cr = htole16(cpr->ring.phys_id);

		rc = hwrm_send_message(softc, &req, sizeof(req));
	}
	return rc;
}

#if 0

void
bnxt_validate_hw_lro_settings(struct bnxt_softc *softc)
{
	softc->hw_lro.enable = min(softc->hw_lro.enable, 1);

        softc->hw_lro.is_mode_gro = min(softc->hw_lro.is_mode_gro, 1);

	softc->hw_lro.max_agg_segs = min(softc->hw_lro.max_agg_segs,
		HWRM_VNIC_TPA_CFG_INPUT_MAX_AGG_SEGS_MAX);

	softc->hw_lro.max_aggs = min(softc->hw_lro.max_aggs,
		HWRM_VNIC_TPA_CFG_INPUT_MAX_AGGS_MAX);

	softc->hw_lro.min_agg_len = min(softc->hw_lro.min_agg_len, BNXT_MAX_MTU);
}

int
bnxt_hwrm_vnic_tpa_cfg(struct bnxt_softc *softc)
{
	struct hwrm_vnic_tpa_cfg_input req = {0};
	uint32_t flags;

	if (softc->vnic_info.id == (uint16_t) HWRM_NA_SIGNATURE) {
		return 0;
	}

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_VNIC_TPA_CFG);

	if (softc->hw_lro.enable) {
		flags = HWRM_VNIC_TPA_CFG_INPUT_FLAGS_TPA |
			HWRM_VNIC_TPA_CFG_INPUT_FLAGS_ENCAP_TPA |
			HWRM_VNIC_TPA_CFG_INPUT_FLAGS_AGG_WITH_ECN |
			HWRM_VNIC_TPA_CFG_INPUT_FLAGS_AGG_WITH_SAME_GRE_SEQ;
		
        	if (softc->hw_lro.is_mode_gro)
			flags |= HWRM_VNIC_TPA_CFG_INPUT_FLAGS_GRO;
		else
			flags |= HWRM_VNIC_TPA_CFG_INPUT_FLAGS_RSC_WND_UPDATE;
			
		req.flags = htole32(flags);

		req.enables = htole32(HWRM_VNIC_TPA_CFG_INPUT_ENABLES_MAX_AGG_SEGS |
				HWRM_VNIC_TPA_CFG_INPUT_ENABLES_MAX_AGGS |
				HWRM_VNIC_TPA_CFG_INPUT_ENABLES_MIN_AGG_LEN);

		req.max_agg_segs = htole16(softc->hw_lro.max_agg_segs);
		req.max_aggs = htole16(softc->hw_lro.max_aggs);
		req.min_agg_len = htole32(softc->hw_lro.min_agg_len);
	}

	req.vnic_id = htole16(softc->vnic_info.id);

	return hwrm_send_message(softc, &req, sizeof(req));
}


int
bnxt_hwrm_fw_reset(struct bnxt_softc *softc, uint8_t processor,
    uint8_t *selfreset)
{
	struct hwrm_fw_reset_input req = {0};
	struct hwrm_fw_reset_output *resp =
	    (void *)softc->hwrm_cmd_resp.idi_vaddr;
	int rc;

	MPASS(selfreset);

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_FW_RESET);
	req.embedded_proc_type = processor;
	req.selfrst_status = *selfreset;

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc)
		goto exit;
	*selfreset = resp->selfrst_status;

exit:
	BNXT_HWRM_UNLOCK(softc);
	return rc;
}

int
bnxt_hwrm_fw_qstatus(struct bnxt_softc *softc, uint8_t type, uint8_t *selfreset)
{
	struct hwrm_fw_qstatus_input req = {0};
	struct hwrm_fw_qstatus_output *resp =
	    (void *)softc->hwrm_cmd_resp.idi_vaddr;
	int rc;

	MPASS(selfreset);

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_FW_QSTATUS);
	req.embedded_proc_type = type;

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc)
		goto exit;
	*selfreset = resp->selfrst_status;

exit:
	BNXT_HWRM_UNLOCK(softc);
	return rc;
}

#endif

int
bnxt_hwrm_nvm_get_dev_info(struct bnxt_softc *softc, uint16_t *mfg_id,
    uint16_t *device_id, uint32_t *sector_size, uint32_t *nvram_size,
    uint32_t *reserved_size, uint32_t *available_size)
{
	struct hwrm_nvm_get_dev_info_input req = {0};
	struct hwrm_nvm_get_dev_info_output *resp =
	    BNXT_DMA_KVA(softc->sc_cmd_resp);
	int rc;
	uint32_t old_timeo;

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_NVM_GET_DEV_INFO);

	BNXT_HWRM_LOCK(softc);
	old_timeo = softc->sc_cmd_timeo;
	softc->sc_cmd_timeo = BNXT_NVM_TIMEO;
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	softc->sc_cmd_timeo = old_timeo;
	if (rc)
		goto exit;

	if (mfg_id)
		*mfg_id = le16toh(resp->manufacturer_id);
	if (device_id)
		*device_id = le16toh(resp->device_id);
	if (sector_size)
		*sector_size = le32toh(resp->sector_size);
	if (nvram_size)
		*nvram_size = le32toh(resp->nvram_size);
	if (reserved_size)
		*reserved_size = le32toh(resp->reserved_size);
	if (available_size)
		*available_size = le32toh(resp->available_size);

exit:
	BNXT_HWRM_UNLOCK(softc);
	return rc;
}

#if 0

int
bnxt_hwrm_fw_get_time(struct bnxt_softc *softc, uint16_t *year, uint8_t *month,
    uint8_t *day, uint8_t *hour, uint8_t *minute, uint8_t *second,
    uint16_t *millisecond, uint16_t *zone)
{
	struct hwrm_fw_get_time_input req = {0};
	struct hwrm_fw_get_time_output *resp =
	    (void *)softc->hwrm_cmd_resp.idi_vaddr;
	int rc;

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_FW_GET_TIME);

	BNXT_HWRM_LOCK(softc);
	rc = _hwrm_send_message(softc, &req, sizeof(req));
	if (rc)
		goto exit;

	if (year)
		*year = le16toh(resp->year);
	if (month)
		*month = resp->month;
	if (day)
		*day = resp->day;
	if (hour)
		*hour = resp->hour;
	if (minute)
		*minute = resp->minute;
	if (second)
		*second = resp->second;
	if (millisecond)
		*millisecond = le16toh(resp->millisecond);
	if (zone)
		*zone = le16toh(resp->zone);

exit:
	BNXT_HWRM_UNLOCK(softc);
	return rc;
}

int
bnxt_hwrm_fw_set_time(struct bnxt_softc *softc, uint16_t year, uint8_t month,
    uint8_t day, uint8_t hour, uint8_t minute, uint8_t second,
    uint16_t millisecond, uint16_t zone)
{
	struct hwrm_fw_set_time_input req = {0};

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_FW_SET_TIME);

	req.year = htole16(year);
	req.month = month;
	req.day = day;
	req.hour = hour;
	req.minute = minute;
	req.second = second;
	req.millisecond = htole16(millisecond);
	req.zone = htole16(zone);
	return hwrm_send_message(softc, &req, sizeof(req));
}

#endif

void
_bnxt_hwrm_set_async_event_bit(struct hwrm_func_drv_rgtr_input *req, int bit)
{
	req->async_event_fwd[bit/32] |= (1 << (bit % 32));
}

int
bnxt_hwrm_func_rgtr_async_events(struct bnxt_softc *softc)
{
	struct hwrm_func_drv_rgtr_input req = {0};
	int events[] = {
		HWRM_ASYNC_EVENT_CMPL_EVENT_ID_LINK_STATUS_CHANGE,
		HWRM_ASYNC_EVENT_CMPL_EVENT_ID_PF_DRVR_UNLOAD,
		HWRM_ASYNC_EVENT_CMPL_EVENT_ID_PORT_CONN_NOT_ALLOWED,
		HWRM_ASYNC_EVENT_CMPL_EVENT_ID_VF_CFG_CHANGE,
		HWRM_ASYNC_EVENT_CMPL_EVENT_ID_LINK_SPEED_CFG_CHANGE
	};
	int i;

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_FUNC_DRV_RGTR);

	req.enables =
		htole32(HWRM_FUNC_DRV_RGTR_INPUT_ENABLES_ASYNC_EVENT_FWD);

	for (i = 0; i < nitems(events); i++)
		_bnxt_hwrm_set_async_event_bit(&req, events[i]);

	return hwrm_send_message(softc, &req, sizeof(req));
}

int
bnxt_get_sffpage(struct bnxt_softc *softc, struct if_sffpage *sff)
{
	struct hwrm_port_phy_i2c_read_input req;
	struct hwrm_port_phy_i2c_read_output *out;
	int offset;

	bnxt_hwrm_cmd_hdr_init(softc, &req, HWRM_PORT_PHY_I2C_READ);
	req.i2c_slave_addr = sff->sff_addr;
	req.page_number = htole16(sff->sff_page);

	for (offset = 0; offset < 256; offset += sizeof(out->data)) {
		req.page_offset = htole16(offset);
		req.data_length = sizeof(out->data);
		req.enables = htole32(HWRM_PORT_PHY_I2C_READ_REQ_ENABLES_PAGE_OFFSET);
		
		if (hwrm_send_message(softc, &req, sizeof(req))) {
			printf("%s: failed to read i2c data\n", DEVNAME(softc));
			return 1;
		}

		out = (struct hwrm_port_phy_i2c_read_output *)
		    BNXT_DMA_KVA(softc->sc_cmd_resp);
		memcpy(sff->sff_data + offset, out->data, sizeof(out->data));
	}

	return 0;
}
