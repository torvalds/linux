/*	$OpenBSD: if_oce.c,v 1.109 2024/05/24 06:02:56 jsg Exp $	*/

/*
 * Copyright (c) 2012 Mike Belopuhov
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

/*-
 * Copyright (C) 2012 Emulex
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Emulex Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
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
 *
 * Contact Information:
 * freebsd-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#include "bpfilter.h"
#include "vlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/timeout.h>
#include <sys/pool.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_ocereg.h>

#ifndef TRUE
#define TRUE			1
#endif
#ifndef FALSE
#define FALSE			0
#endif

#define OCE_MBX_TIMEOUT		5

#define OCE_MAX_PAYLOAD		65536

#define OCE_TX_RING_SIZE	512
#define OCE_RX_RING_SIZE	1024

/* This should be powers of 2. Like 2,4,8 & 16 */
#define OCE_MAX_RSS		4 /* TODO: 8 */
#define OCE_MAX_RQ		OCE_MAX_RSS + 1 /* one default queue */
#define OCE_MAX_WQ		8

#define OCE_MAX_EQ		32
#define OCE_MAX_CQ		OCE_MAX_RQ + OCE_MAX_WQ + 1 /* one MCC queue */
#define OCE_MAX_CQ_EQ		8 /* Max CQ that can attached to an EQ */

#define OCE_DEFAULT_EQD		80

#define OCE_MIN_MTU		256
#define OCE_MAX_MTU		9000

#define OCE_MAX_RQ_COMPL	64
#define OCE_MAX_RQ_POSTS	255
#define OCE_RX_BUF_SIZE		2048

#define OCE_MAX_TX_ELEMENTS	29
#define OCE_MAX_TX_DESC		1024
#define OCE_MAX_TX_SIZE		65535

#define OCE_MEM_KVA(_m)		((void *)((_m)->vaddr))
#define OCE_MEM_DVA(_m)		((_m)->paddr)

#define OCE_WQ_FOREACH(sc, wq, i) 	\
	for (i = 0, wq = sc->sc_wq[0]; i < sc->sc_nwq; i++, wq = sc->sc_wq[i])
#define OCE_RQ_FOREACH(sc, rq, i) 	\
	for (i = 0, rq = sc->sc_rq[0]; i < sc->sc_nrq; i++, rq = sc->sc_rq[i])
#define OCE_EQ_FOREACH(sc, eq, i) 	\
	for (i = 0, eq = sc->sc_eq[0]; i < sc->sc_neq; i++, eq = sc->sc_eq[i])
#define OCE_CQ_FOREACH(sc, cq, i) 	\
	for (i = 0, cq = sc->sc_cq[0]; i < sc->sc_ncq; i++, cq = sc->sc_cq[i])
#define OCE_RING_FOREACH(_r, _v, _c)	\
	for ((_v) = oce_ring_first(_r); _c; (_v) = oce_ring_next(_r))

static inline int
ilog2(unsigned int v)
{
	int r = 0;

	while (v >>= 1)
		r++;
	return (r);
}

struct oce_pkt {
	struct mbuf *		mbuf;
	bus_dmamap_t		map;
	int			nsegs;
	SIMPLEQ_ENTRY(oce_pkt)	entry;
};
SIMPLEQ_HEAD(oce_pkt_list, oce_pkt);

struct oce_dma_mem {
	bus_dma_tag_t		tag;
	bus_dmamap_t		map;
	bus_dma_segment_t	segs;
	int			nsegs;
	bus_size_t		size;
	caddr_t			vaddr;
	bus_addr_t		paddr;
};

struct oce_ring {
	int			index;
	int			nitems;
	int			nused;
	int			isize;
	struct oce_dma_mem	dma;
};

struct oce_softc;

enum cq_len {
	CQ_LEN_256  = 256,
	CQ_LEN_512  = 512,
	CQ_LEN_1024 = 1024
};

enum eq_len {
	EQ_LEN_256  = 256,
	EQ_LEN_512  = 512,
	EQ_LEN_1024 = 1024,
	EQ_LEN_2048 = 2048,
	EQ_LEN_4096 = 4096
};

enum eqe_size {
	EQE_SIZE_4  = 4,
	EQE_SIZE_16 = 16
};

enum qtype {
	QTYPE_EQ,
	QTYPE_MQ,
	QTYPE_WQ,
	QTYPE_RQ,
	QTYPE_CQ,
	QTYPE_RSS
};

struct oce_eq {
	struct oce_softc *	sc;
	struct oce_ring *	ring;
	enum qtype		type;
	int			id;

	struct oce_cq *		cq[OCE_MAX_CQ_EQ];
	int			cq_valid;

	int			nitems;
	int			isize;
	int			delay;
};

struct oce_cq {
	struct oce_softc *	sc;
	struct oce_ring *	ring;
	enum qtype		type;
	int			id;

	struct oce_eq *		eq;

	void			(*cq_intr)(void *);
	void *			cb_arg;

	int			nitems;
	int			nodelay;
	int			eventable;
	int			ncoalesce;
};

struct oce_mq {
	struct oce_softc *	sc;
	struct oce_ring *	ring;
	enum qtype		type;
	int			id;

	struct oce_cq *		cq;

	int			nitems;
};

struct oce_wq {
	struct oce_softc *	sc;
	struct oce_ring *	ring;
	enum qtype		type;
	int			id;

	struct oce_cq *		cq;

	struct oce_pkt_list	pkt_list;
	struct oce_pkt_list	pkt_free;

	int			nitems;
};

struct oce_rq {
	struct oce_softc *	sc;
	struct oce_ring *	ring;
	enum qtype		type;
	int			id;

	struct oce_cq *		cq;

	struct if_rxring	rxring;
	struct oce_pkt_list	pkt_list;
	struct oce_pkt_list	pkt_free;

	uint32_t		rss_cpuid;

#ifdef OCE_LRO
	struct lro_ctrl		lro;
	int			lro_pkts_queued;
#endif

	int			nitems;
	int			fragsize;
	int			mtu;
	int			rss;
};

struct oce_softc {
	struct device		sc_dev;

	uint			sc_flags;
#define  OCE_F_BE2		 0x00000001
#define  OCE_F_BE3		 0x00000002
#define  OCE_F_XE201		 0x00000008
#define  OCE_F_BE3_NATIVE	 0x00000100
#define  OCE_F_RESET_RQD	 0x00001000
#define  OCE_F_MBOX_ENDIAN_RQD	 0x00002000

	bus_dma_tag_t		sc_dmat;

	bus_space_tag_t		sc_cfg_iot;
	bus_space_handle_t	sc_cfg_ioh;
	bus_size_t		sc_cfg_size;

	bus_space_tag_t		sc_csr_iot;
	bus_space_handle_t	sc_csr_ioh;
	bus_size_t		sc_csr_size;

	bus_space_tag_t		sc_db_iot;
	bus_space_handle_t	sc_db_ioh;
	bus_size_t		sc_db_size;

	void *			sc_ih;

	struct arpcom		sc_ac;
	struct ifmedia		sc_media;
	ushort			sc_link_up;
	ushort			sc_link_speed;
	uint64_t		sc_fc;

	struct oce_dma_mem	sc_mbx;
	struct oce_dma_mem	sc_pld;

	uint			sc_port;
	uint			sc_fmode;

	struct oce_wq *		sc_wq[OCE_MAX_WQ];	/* TX work queues */
	struct oce_rq *		sc_rq[OCE_MAX_RQ];	/* RX work queues */
	struct oce_cq *		sc_cq[OCE_MAX_CQ];	/* Completion queues */
	struct oce_eq *		sc_eq[OCE_MAX_EQ];	/* Event queues */
	struct oce_mq *		sc_mq;			/* Mailbox queue */

	ushort			sc_neq;
	ushort			sc_ncq;
	ushort			sc_nrq;
	ushort			sc_nwq;
	ushort			sc_nintr;

	ushort			sc_tx_ring_size;
	ushort			sc_rx_ring_size;
	ushort			sc_rss_enable;

	uint32_t		sc_if_id;	/* interface ID */
	uint32_t		sc_pmac_id;	/* PMAC id */
	char			sc_macaddr[ETHER_ADDR_LEN];

	uint32_t		sc_pvid;

	uint64_t		sc_rx_errors;
	uint64_t		sc_tx_errors;

	struct timeout		sc_tick;
	struct timeout		sc_rxrefill;

	void *			sc_statcmd;
};

#define IS_BE(sc)		ISSET((sc)->sc_flags, OCE_F_BE2 | OCE_F_BE3)
#define IS_XE201(sc)		ISSET((sc)->sc_flags, OCE_F_XE201)

#define ADDR_HI(x)		((uint32_t)((uint64_t)(x) >> 32))
#define ADDR_LO(x)		((uint32_t)((uint64_t)(x) & 0xffffffff))

#define IF_LRO_ENABLED(ifp)	ISSET((ifp)->if_capabilities, IFCAP_LRO)

int 	oce_match(struct device *, void *, void *);
void	oce_attach(struct device *, struct device *, void *);
int 	oce_pci_alloc(struct oce_softc *, struct pci_attach_args *);
void	oce_attachhook(struct device *);
void	oce_attach_ifp(struct oce_softc *);
int 	oce_ioctl(struct ifnet *, u_long, caddr_t);
int	oce_rxrinfo(struct oce_softc *, struct if_rxrinfo *);
void	oce_iff(struct oce_softc *);
void	oce_link_status(struct oce_softc *);
void	oce_media_status(struct ifnet *, struct ifmediareq *);
int 	oce_media_change(struct ifnet *);
void	oce_tick(void *);
void	oce_init(void *);
void	oce_stop(struct oce_softc *);
void	oce_watchdog(struct ifnet *);
void	oce_start(struct ifnet *);
int	oce_encap(struct oce_softc *, struct mbuf **, int wqidx);
#ifdef OCE_TSO
struct mbuf *
	oce_tso(struct oce_softc *, struct mbuf **);
#endif
int 	oce_intr(void *);
void	oce_intr_wq(void *);
void	oce_txeof(struct oce_wq *);
void	oce_intr_rq(void *);
void	oce_rxeof(struct oce_rq *, struct oce_nic_rx_cqe *);
void	oce_rxeoc(struct oce_rq *, struct oce_nic_rx_cqe *);
int 	oce_vtp_valid(struct oce_softc *, struct oce_nic_rx_cqe *);
int 	oce_port_valid(struct oce_softc *, struct oce_nic_rx_cqe *);
#ifdef OCE_LRO
void	oce_flush_lro(struct oce_rq *);
int 	oce_init_lro(struct oce_softc *);
void	oce_free_lro(struct oce_softc *);
#endif
int	oce_get_buf(struct oce_rq *);
int	oce_alloc_rx_bufs(struct oce_rq *);
void	oce_refill_rx(void *);
void	oce_free_posted_rxbuf(struct oce_rq *);
void	oce_intr_mq(void *);
void	oce_link_event(struct oce_softc *,
	    struct oce_async_cqe_link_state *);

int 	oce_init_queues(struct oce_softc *);
void	oce_release_queues(struct oce_softc *);
struct oce_wq *oce_create_wq(struct oce_softc *, struct oce_eq *);
void	oce_drain_wq(struct oce_wq *);
void	oce_destroy_wq(struct oce_wq *);
struct oce_rq *
	oce_create_rq(struct oce_softc *, struct oce_eq *, int rss);
void	oce_drain_rq(struct oce_rq *);
void	oce_destroy_rq(struct oce_rq *);
struct oce_eq *
	oce_create_eq(struct oce_softc *);
static inline void
	oce_arm_eq(struct oce_eq *, int neqe, int rearm, int clearint);
void	oce_drain_eq(struct oce_eq *);
void	oce_destroy_eq(struct oce_eq *);
struct oce_mq *
	oce_create_mq(struct oce_softc *, struct oce_eq *);
void	oce_drain_mq(struct oce_mq *);
void	oce_destroy_mq(struct oce_mq *);
struct oce_cq *
	oce_create_cq(struct oce_softc *, struct oce_eq *, int nitems,
	    int isize, int eventable, int nodelay, int ncoalesce);
static inline void
	oce_arm_cq(struct oce_cq *, int ncqe, int rearm);
void	oce_destroy_cq(struct oce_cq *);

int	oce_dma_alloc(struct oce_softc *, bus_size_t, struct oce_dma_mem *);
void	oce_dma_free(struct oce_softc *, struct oce_dma_mem *);
#define	oce_dma_sync(d, f) \
	    bus_dmamap_sync((d)->tag, (d)->map, 0, (d)->map->dm_mapsize, f)

struct oce_ring *
	oce_create_ring(struct oce_softc *, int nitems, int isize, int maxseg);
void	oce_destroy_ring(struct oce_softc *, struct oce_ring *);
int	oce_load_ring(struct oce_softc *, struct oce_ring *,
	    struct oce_pa *, int max_segs);
static inline void *
	oce_ring_get(struct oce_ring *);
static inline void *
	oce_ring_first(struct oce_ring *);
static inline void *
	oce_ring_next(struct oce_ring *);
struct oce_pkt *
	oce_pkt_alloc(struct oce_softc *, size_t size, int nsegs,
	    int maxsegsz);
void	oce_pkt_free(struct oce_softc *, struct oce_pkt *);
static inline struct oce_pkt *
	oce_pkt_get(struct oce_pkt_list *);
static inline void
	oce_pkt_put(struct oce_pkt_list *, struct oce_pkt *);

int	oce_init_fw(struct oce_softc *);
int	oce_mbox_init(struct oce_softc *);
int	oce_mbox_dispatch(struct oce_softc *);
int	oce_cmd(struct oce_softc *, int subsys, int opcode, int version,
	    void *payload, int length);
void	oce_first_mcc(struct oce_softc *);

int	oce_get_fw_config(struct oce_softc *);
int	oce_check_native_mode(struct oce_softc *);
int	oce_create_iface(struct oce_softc *, uint8_t *macaddr);
int	oce_config_vlan(struct oce_softc *, struct normal_vlan *vtags,
	    int nvtags, int untagged, int promisc);
int	oce_set_flow_control(struct oce_softc *, uint64_t);
int	oce_config_rss(struct oce_softc *, int enable);
int	oce_update_mcast(struct oce_softc *, uint8_t multi[][ETHER_ADDR_LEN],
	    int naddr);
int	oce_set_promisc(struct oce_softc *, int enable);
int	oce_get_link_status(struct oce_softc *);

void	oce_macaddr_set(struct oce_softc *);
int	oce_macaddr_get(struct oce_softc *, uint8_t *macaddr);
int	oce_macaddr_add(struct oce_softc *, uint8_t *macaddr, uint32_t *pmac);
int	oce_macaddr_del(struct oce_softc *, uint32_t pmac);

int	oce_new_rq(struct oce_softc *, struct oce_rq *);
int	oce_new_wq(struct oce_softc *, struct oce_wq *);
int	oce_new_mq(struct oce_softc *, struct oce_mq *);
int	oce_new_eq(struct oce_softc *, struct oce_eq *);
int	oce_new_cq(struct oce_softc *, struct oce_cq *);

int	oce_init_stats(struct oce_softc *);
int	oce_update_stats(struct oce_softc *);
int	oce_stats_be2(struct oce_softc *, uint64_t *, uint64_t *);
int	oce_stats_be3(struct oce_softc *, uint64_t *, uint64_t *);
int	oce_stats_xe(struct oce_softc *, uint64_t *, uint64_t *);

struct pool *oce_pkt_pool;

struct cfdriver oce_cd = {
	NULL, "oce", DV_IFNET
};

const struct cfattach oce_ca = {
	sizeof(struct oce_softc), oce_match, oce_attach, NULL, NULL
};

const struct pci_matchid oce_devices[] = {
	{ PCI_VENDOR_SERVERENGINES, PCI_PRODUCT_SERVERENGINES_BE2 },
	{ PCI_VENDOR_SERVERENGINES, PCI_PRODUCT_SERVERENGINES_BE3 },
	{ PCI_VENDOR_SERVERENGINES, PCI_PRODUCT_SERVERENGINES_OCBE2 },
	{ PCI_VENDOR_SERVERENGINES, PCI_PRODUCT_SERVERENGINES_OCBE3 },
	{ PCI_VENDOR_EMULEX, PCI_PRODUCT_EMULEX_XE201 },
};

int
oce_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, oce_devices, nitems(oce_devices)));
}

void
oce_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	struct oce_softc *sc = (struct oce_softc *)self;
	const char *intrstr = NULL;
	pci_intr_handle_t ih;

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_SERVERENGINES_BE2:
	case PCI_PRODUCT_SERVERENGINES_OCBE2:
		SET(sc->sc_flags, OCE_F_BE2);
		break;
	case PCI_PRODUCT_SERVERENGINES_BE3:
	case PCI_PRODUCT_SERVERENGINES_OCBE3:
		SET(sc->sc_flags, OCE_F_BE3);
		break;
	case PCI_PRODUCT_EMULEX_XE201:
		SET(sc->sc_flags, OCE_F_XE201);
		break;
	}

	sc->sc_dmat = pa->pa_dmat;
	if (oce_pci_alloc(sc, pa))
		return;

	sc->sc_tx_ring_size = OCE_TX_RING_SIZE;
	sc->sc_rx_ring_size = OCE_RX_RING_SIZE;

	/* create the bootstrap mailbox */
	if (oce_dma_alloc(sc, sizeof(struct oce_bmbx), &sc->sc_mbx)) {
		printf(": failed to allocate mailbox memory\n");
		return;
	}
	if (oce_dma_alloc(sc, OCE_MAX_PAYLOAD, &sc->sc_pld)) {
		printf(": failed to allocate payload memory\n");
		goto fail_1;
	}

	if (oce_init_fw(sc))
		goto fail_2;

	if (oce_mbox_init(sc)) {
		printf(": failed to initialize mailbox\n");
		goto fail_2;
	}

	if (oce_get_fw_config(sc)) {
		printf(": failed to get firmware configuration\n");
		goto fail_2;
	}

	if (ISSET(sc->sc_flags, OCE_F_BE3)) {
		if (oce_check_native_mode(sc))
			goto fail_2;
	}

	if (oce_macaddr_get(sc, sc->sc_macaddr)) {
		printf(": failed to fetch MAC address\n");
		goto fail_2;
	}
	memcpy(sc->sc_ac.ac_enaddr, sc->sc_macaddr, ETHER_ADDR_LEN);

	if (oce_pkt_pool == NULL) {
		oce_pkt_pool = malloc(sizeof(struct pool), M_DEVBUF, M_NOWAIT);
		if (oce_pkt_pool == NULL) {
			printf(": unable to allocate descriptor pool\n");
			goto fail_2;
		}
		pool_init(oce_pkt_pool, sizeof(struct oce_pkt), 0, IPL_NET,
		    0, "ocepkts", NULL);
	}

	/* We allocate a single interrupt resource */
	sc->sc_nintr = 1;
	if (pci_intr_map_msi(pa, &ih) != 0 &&
	    pci_intr_map(pa, &ih) != 0) {
		printf(": couldn't map interrupt\n");
		goto fail_2;
	}

	intrstr = pci_intr_string(pa->pa_pc, ih);
	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_NET, oce_intr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt\n");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail_2;
	}
	printf(": %s", intrstr);

	if (oce_init_stats(sc))
		goto fail_3;

	if (oce_init_queues(sc))
		goto fail_3;

	oce_attach_ifp(sc);

#ifdef OCE_LRO
	if (oce_init_lro(sc))
		goto fail_4;
#endif

	timeout_set(&sc->sc_tick, oce_tick, sc);
	timeout_set(&sc->sc_rxrefill, oce_refill_rx, sc);

	config_mountroot(self, oce_attachhook);

	printf(", address %s\n", ether_sprintf(sc->sc_ac.ac_enaddr));

	return;

#ifdef OCE_LRO
fail_4:
	oce_free_lro(sc);
	ether_ifdetach(&sc->sc_ac.ac_if);
	if_detach(&sc->sc_ac.ac_if);
	oce_release_queues(sc);
#endif
fail_3:
	pci_intr_disestablish(pa->pa_pc, sc->sc_ih);
fail_2:
	oce_dma_free(sc, &sc->sc_pld);
fail_1:
	oce_dma_free(sc, &sc->sc_mbx);
}

int
oce_pci_alloc(struct oce_softc *sc, struct pci_attach_args *pa)
{
	pcireg_t memtype, reg;

	/* setup the device config region */
	if (ISSET(sc->sc_flags, OCE_F_BE2))
		reg = OCE_BAR_CFG_BE2;
	else
		reg = OCE_BAR_CFG;

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, reg);
	if (pci_mapreg_map(pa, reg, memtype, 0, &sc->sc_cfg_iot,
	    &sc->sc_cfg_ioh, NULL, &sc->sc_cfg_size,
	    IS_BE(sc) ? 0 : 32768)) {
		printf(": can't find cfg mem space\n");
		return (ENXIO);
	}

	/*
	 * Read the SLI_INTF register and determine whether we
	 * can use this port and its features
	 */
	reg = pci_conf_read(pa->pa_pc, pa->pa_tag, OCE_INTF_REG_OFFSET);
	if (OCE_SLI_SIGNATURE(reg) != OCE_INTF_VALID_SIG) {
		printf(": invalid signature\n");
		goto fail_1;
	}
	if (OCE_SLI_REVISION(reg) != OCE_INTF_SLI_REV4) {
		printf(": unsupported SLI revision\n");
		goto fail_1;
	}
	if (OCE_SLI_IFTYPE(reg) == OCE_INTF_IF_TYPE_1)
		SET(sc->sc_flags, OCE_F_MBOX_ENDIAN_RQD);
	if (OCE_SLI_HINT1(reg) == OCE_INTF_FUNC_RESET_REQD)
		SET(sc->sc_flags, OCE_F_RESET_RQD);

	/* Lancer has one BAR (CFG) but BE3 has three (CFG, CSR, DB) */
	if (IS_BE(sc)) {
		/* set up CSR region */
		reg = OCE_BAR_CSR;
		memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, reg);
		if (pci_mapreg_map(pa, reg, memtype, 0, &sc->sc_csr_iot,
		    &sc->sc_csr_ioh, NULL, &sc->sc_csr_size, 0)) {
			printf(": can't find csr mem space\n");
			goto fail_1;
		}

		/* set up DB doorbell region */
		reg = OCE_BAR_DB;
		memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, reg);
		if (pci_mapreg_map(pa, reg, memtype, 0, &sc->sc_db_iot,
		    &sc->sc_db_ioh, NULL, &sc->sc_db_size, 0)) {
			printf(": can't find csr mem space\n");
			goto fail_2;
		}
	} else {
		sc->sc_csr_iot = sc->sc_db_iot = sc->sc_cfg_iot;
		sc->sc_csr_ioh = sc->sc_db_ioh = sc->sc_cfg_ioh;
	}

	return (0);

fail_2:
	bus_space_unmap(sc->sc_csr_iot, sc->sc_csr_ioh, sc->sc_csr_size);
fail_1:
	bus_space_unmap(sc->sc_cfg_iot, sc->sc_cfg_ioh, sc->sc_cfg_size);
	return (ENXIO);
}

static inline uint32_t
oce_read_cfg(struct oce_softc *sc, bus_size_t off)
{
	bus_space_barrier(sc->sc_cfg_iot, sc->sc_cfg_ioh, off, 4,
	    BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(sc->sc_cfg_iot, sc->sc_cfg_ioh, off));
}

static inline uint32_t
oce_read_csr(struct oce_softc *sc, bus_size_t off)
{
	bus_space_barrier(sc->sc_csr_iot, sc->sc_csr_ioh, off, 4,
	    BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(sc->sc_csr_iot, sc->sc_csr_ioh, off));
}

static inline uint32_t
oce_read_db(struct oce_softc *sc, bus_size_t off)
{
	bus_space_barrier(sc->sc_db_iot, sc->sc_db_ioh, off, 4,
	    BUS_SPACE_BARRIER_READ);
	return (bus_space_read_4(sc->sc_db_iot, sc->sc_db_ioh, off));
}

static inline void
oce_write_cfg(struct oce_softc *sc, bus_size_t off, uint32_t val)
{
	bus_space_write_4(sc->sc_cfg_iot, sc->sc_cfg_ioh, off, val);
	bus_space_barrier(sc->sc_cfg_iot, sc->sc_cfg_ioh, off, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

static inline void
oce_write_csr(struct oce_softc *sc, bus_size_t off, uint32_t val)
{
	bus_space_write_4(sc->sc_csr_iot, sc->sc_csr_ioh, off, val);
	bus_space_barrier(sc->sc_csr_iot, sc->sc_csr_ioh, off, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

static inline void
oce_write_db(struct oce_softc *sc, bus_size_t off, uint32_t val)
{
	bus_space_write_4(sc->sc_db_iot, sc->sc_db_ioh, off, val);
	bus_space_barrier(sc->sc_db_iot, sc->sc_db_ioh, off, 4,
	    BUS_SPACE_BARRIER_WRITE);
}

static inline void
oce_intr_enable(struct oce_softc *sc)
{
	uint32_t reg;

	reg = oce_read_cfg(sc, PCI_INTR_CTRL);
	oce_write_cfg(sc, PCI_INTR_CTRL, reg | HOSTINTR_MASK);
}

static inline void
oce_intr_disable(struct oce_softc *sc)
{
	uint32_t reg;

	reg = oce_read_cfg(sc, PCI_INTR_CTRL);
	oce_write_cfg(sc, PCI_INTR_CTRL, reg & ~HOSTINTR_MASK);
}

void
oce_attachhook(struct device *self)
{
	struct oce_softc *sc = (struct oce_softc *)self;

	oce_get_link_status(sc);

	oce_arm_cq(sc->sc_mq->cq, 0, TRUE);

	/*
	 * We need to get MCC async events. So enable intrs and arm
	 * first EQ, Other EQs will be armed after interface is UP
	 */
	oce_intr_enable(sc);
	oce_arm_eq(sc->sc_eq[0], 0, TRUE, FALSE);

	/*
	 * Send first mcc cmd and after that we get gracious
	 * MCC notifications from FW
	 */
	oce_first_mcc(sc);
}

void
oce_attach_ifp(struct oce_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	ifmedia_init(&sc->sc_media, IFM_IMASK, oce_media_change,
	    oce_media_status);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);

	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = oce_ioctl;
	ifp->if_start = oce_start;
	ifp->if_watchdog = oce_watchdog;
	ifp->if_hardmtu = OCE_MAX_MTU;
	ifp->if_softc = sc;
	ifq_init_maxlen(&ifp->if_snd, sc->sc_tx_ring_size - 1);

	ifp->if_capabilities = IFCAP_VLAN_MTU | IFCAP_CSUM_IPv4 |
	    IFCAP_CSUM_TCPv4 | IFCAP_CSUM_UDPv4;

#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif

#ifdef OCE_TSO
	ifp->if_capabilities |= IFCAP_TSO;
	ifp->if_capabilities |= IFCAP_VLAN_HWTSO;
#endif
#ifdef OCE_LRO
	ifp->if_capabilities |= IFCAP_LRO;
#endif

	if_attach(ifp);
	ether_ifattach(ifp);
}

int
oce_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct oce_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			oce_init(sc);
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				oce_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				oce_stop(sc);
		}
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, command);
		break;
	case SIOCGIFRXR:
		error = oce_rxrinfo(sc, (struct if_rxrinfo *)ifr->ifr_data);
		break;
	default:
		error = ether_ioctl(ifp, &sc->sc_ac, command, data);
		break;
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			oce_iff(sc);
		error = 0;
	}

	splx(s);

	return (error);
}

int
oce_rxrinfo(struct oce_softc *sc, struct if_rxrinfo *ifri)
{
	struct if_rxring_info *ifr, ifr1;
	struct oce_rq *rq;
	int error, i;
	u_int n = 0;

	if (sc->sc_nrq > 1) {
		ifr = mallocarray(sc->sc_nrq, sizeof(*ifr), M_DEVBUF,
		    M_WAITOK | M_ZERO);
	} else
		ifr = &ifr1;

	OCE_RQ_FOREACH(sc, rq, i) {
		ifr[n].ifr_size = MCLBYTES;
		snprintf(ifr[n].ifr_name, sizeof(ifr[n].ifr_name), "/%d", i);
		ifr[n].ifr_info = rq->rxring;
		n++;
	}

	error = if_rxr_info_ioctl(ifri, sc->sc_nrq, ifr);

	if (sc->sc_nrq > 1)
		free(ifr, M_DEVBUF, sc->sc_nrq * sizeof(*ifr));
	return (error);
}


void
oce_iff(struct oce_softc *sc)
{
	uint8_t multi[OCE_MAX_MC_FILTER_SIZE][ETHER_ADDR_LEN];
	struct arpcom *ac = &sc->sc_ac;
	struct ifnet *ifp = &ac->ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	int naddr = 0, promisc = 0;

	ifp->if_flags &= ~IFF_ALLMULTI;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0 ||
	    ac->ac_multicnt >= OCE_MAX_MC_FILTER_SIZE) {
		ifp->if_flags |= IFF_ALLMULTI;
		promisc = 1;
	} else {
		ETHER_FIRST_MULTI(step, &sc->sc_ac, enm);
		while (enm != NULL) {
			memcpy(multi[naddr++], enm->enm_addrlo, ETHER_ADDR_LEN);
			ETHER_NEXT_MULTI(step, enm);
		}
		oce_update_mcast(sc, multi, naddr);
	}

	oce_set_promisc(sc, promisc);
}

void
oce_link_status(struct oce_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int link_state = LINK_STATE_DOWN;

	ifp->if_baudrate = 0;
	if (sc->sc_link_up) {
		link_state = LINK_STATE_FULL_DUPLEX;

		switch (sc->sc_link_speed) {
		case 1:
			ifp->if_baudrate = IF_Mbps(10);
			break;
		case 2:
			ifp->if_baudrate = IF_Mbps(100);
			break;
		case 3:
			ifp->if_baudrate = IF_Gbps(1);
			break;
		case 4:
			ifp->if_baudrate = IF_Gbps(10);
			break;
		}
	}
	if (ifp->if_link_state != link_state) {
		ifp->if_link_state = link_state;
		if_link_state_change(ifp);
	}
}

void
oce_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct oce_softc *sc = ifp->if_softc;

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	if (oce_get_link_status(sc) == 0)
		oce_link_status(sc);

	if (!sc->sc_link_up) {
		ifmr->ifm_active |= IFM_NONE;
		return;
	}

	ifmr->ifm_status |= IFM_ACTIVE;

	switch (sc->sc_link_speed) {
	case 1: /* 10 Mbps */
		ifmr->ifm_active |= IFM_10_T | IFM_FDX;
		break;
	case 2: /* 100 Mbps */
		ifmr->ifm_active |= IFM_100_TX | IFM_FDX;
		break;
	case 3: /* 1 Gbps */
		ifmr->ifm_active |= IFM_1000_T | IFM_FDX;
		break;
	case 4: /* 10 Gbps */
		ifmr->ifm_active |= IFM_10G_SR | IFM_FDX;
		break;
	}

	if (sc->sc_fc & IFM_ETH_RXPAUSE)
		ifmr->ifm_active |= IFM_FLOW | IFM_ETH_RXPAUSE;
	if (sc->sc_fc & IFM_ETH_TXPAUSE)
		ifmr->ifm_active |= IFM_FLOW | IFM_ETH_TXPAUSE;
}

int
oce_media_change(struct ifnet *ifp)
{
	return (0);
}

void
oce_tick(void *arg)
{
	struct oce_softc *sc = arg;
	int s;

	s = splnet();

	if (oce_update_stats(sc) == 0)
		timeout_add_sec(&sc->sc_tick, 1);

	splx(s);
}

void
oce_init(void *arg)
{
	struct oce_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct oce_eq *eq;
	struct oce_rq *rq;
	struct oce_wq *wq;
	int i;

	oce_stop(sc);

	DELAY(10);

	oce_macaddr_set(sc);

	oce_iff(sc);

	/* Enable VLAN promiscuous mode */
	if (oce_config_vlan(sc, NULL, 0, 1, 1))
		goto error;

	if (oce_set_flow_control(sc, IFM_ETH_RXPAUSE | IFM_ETH_TXPAUSE))
		goto error;

	OCE_RQ_FOREACH(sc, rq, i) {
		rq->mtu = ifp->if_hardmtu + ETHER_HDR_LEN + ETHER_CRC_LEN +
		    ETHER_VLAN_ENCAP_LEN;
		if (oce_new_rq(sc, rq)) {
			printf("%s: failed to create rq\n",
			    sc->sc_dev.dv_xname);
			goto error;
		}
		rq->ring->index	 = 0;

		/* oce splits jumbos into 2k chunks... */
		if_rxr_init(&rq->rxring, 8, rq->nitems);

		if (!oce_alloc_rx_bufs(rq)) {
			printf("%s: failed to allocate rx buffers\n",
			    sc->sc_dev.dv_xname);
			goto error;
		}
	}

#ifdef OCE_RSS
	/* RSS config */
	if (sc->sc_rss_enable) {
		if (oce_config_rss(sc, (uint8_t)sc->sc_if_id, 1)) {
			printf("%s: failed to configure RSS\n",
			    sc->sc_dev.dv_xname);
			goto error;
		}
	}
#endif

	OCE_RQ_FOREACH(sc, rq, i)
		oce_arm_cq(rq->cq, 0, TRUE);

	OCE_WQ_FOREACH(sc, wq, i)
		oce_arm_cq(wq->cq, 0, TRUE);

	oce_arm_cq(sc->sc_mq->cq, 0, TRUE);

	OCE_EQ_FOREACH(sc, eq, i)
		oce_arm_eq(eq, 0, TRUE, FALSE);

	if (oce_get_link_status(sc) == 0)
		oce_link_status(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	timeout_add_sec(&sc->sc_tick, 1);

	oce_intr_enable(sc);

	return;
error:
	oce_stop(sc);
}

void
oce_stop(struct oce_softc *sc)
{
	struct mbx_delete_nic_rq cmd;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct oce_rq *rq;
	struct oce_wq *wq;
	struct oce_eq *eq;
	int i;

	timeout_del(&sc->sc_tick);
	timeout_del(&sc->sc_rxrefill);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/* Stop intrs and finish any bottom halves pending */
	oce_intr_disable(sc);

	/* Invalidate any pending cq and eq entries */
	OCE_EQ_FOREACH(sc, eq, i)
		oce_drain_eq(eq);
	OCE_RQ_FOREACH(sc, rq, i) {
		/* destroy the work queue in the firmware */
		memset(&cmd, 0, sizeof(cmd));
		cmd.params.req.rq_id = htole16(rq->id);
		oce_cmd(sc, SUBSYS_NIC, OPCODE_NIC_DELETE_RQ,
		    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
		DELAY(1000);
		oce_drain_rq(rq);
		oce_free_posted_rxbuf(rq);
	}
	OCE_WQ_FOREACH(sc, wq, i)
		oce_drain_wq(wq);
}

void
oce_watchdog(struct ifnet *ifp)
{
	printf("%s: watchdog timeout -- resetting\n", ifp->if_xname);

	oce_init(ifp->if_softc);

	ifp->if_oerrors++;
}

void
oce_start(struct ifnet *ifp)
{
	struct oce_softc *sc = ifp->if_softc;
	struct mbuf *m;
	int pkts = 0;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;

		if (oce_encap(sc, &m, 0)) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap_ether(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
		pkts++;
	}

	/* Set a timeout in case the chip goes out to lunch */
	if (pkts)
		ifp->if_timer = 5;
}

int
oce_encap(struct oce_softc *sc, struct mbuf **mpp, int wqidx)
{
	struct mbuf *m = *mpp;
	struct oce_wq *wq = sc->sc_wq[wqidx];
	struct oce_pkt *pkt = NULL;
	struct oce_nic_hdr_wqe *nhe;
	struct oce_nic_frag_wqe *nfe;
	int i, nwqe, err;

#ifdef OCE_TSO
	if (m->m_pkthdr.csum_flags & CSUM_TSO) {
		/* consolidate packet buffers for TSO/LSO segment offload */
		m = oce_tso(sc, mpp);
		if (m == NULL)
			goto error;
	}
#endif

	if ((pkt = oce_pkt_get(&wq->pkt_free)) == NULL)
		goto error;

	err = bus_dmamap_load_mbuf(sc->sc_dmat, pkt->map, m, BUS_DMA_NOWAIT);
	if (err == EFBIG) {
		if (m_defrag(m, M_DONTWAIT) ||
		    bus_dmamap_load_mbuf(sc->sc_dmat, pkt->map, m,
			BUS_DMA_NOWAIT))
			goto error;
		*mpp = m;
	} else if (err != 0)
		goto error;

	pkt->nsegs = pkt->map->dm_nsegs;

	nwqe = pkt->nsegs + 1;
	if (IS_BE(sc)) {
		/* BE2 and BE3 require even number of WQEs */
		if (nwqe & 1)
			nwqe++;
	}

	/* Fail if there's not enough free WQEs */
	if (nwqe >= wq->ring->nitems - wq->ring->nused) {
		bus_dmamap_unload(sc->sc_dmat, pkt->map);
		goto error;
	}

	bus_dmamap_sync(sc->sc_dmat, pkt->map, 0, pkt->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	pkt->mbuf = m;

	/* TX work queue entry for the header */
	nhe = oce_ring_get(wq->ring);
	memset(nhe, 0, sizeof(*nhe));

	nhe->u0.s.complete = 1;
	nhe->u0.s.event = 1;
	nhe->u0.s.crc = 1;
	nhe->u0.s.forward = 0;
	nhe->u0.s.ipcs = (m->m_pkthdr.csum_flags & M_IPV4_CSUM_OUT) ? 1 : 0;
	nhe->u0.s.udpcs = (m->m_pkthdr.csum_flags & M_UDP_CSUM_OUT) ? 1 : 0;
	nhe->u0.s.tcpcs = (m->m_pkthdr.csum_flags & M_TCP_CSUM_OUT) ? 1 : 0;
	nhe->u0.s.num_wqe = nwqe;
	nhe->u0.s.total_length = m->m_pkthdr.len;

#if NVLAN > 0
	if (m->m_flags & M_VLANTAG) {
		nhe->u0.s.vlan = 1; /* Vlan present */
		nhe->u0.s.vlan_tag = m->m_pkthdr.ether_vtag;
	}
#endif

#ifdef OCE_TSO
	if (m->m_pkthdr.csum_flags & CSUM_TSO) {
		if (m->m_pkthdr.tso_segsz) {
			nhe->u0.s.lso = 1;
			nhe->u0.s.lso_mss  = m->m_pkthdr.tso_segsz;
		}
		if (!IS_BE(sc))
			nhe->u0.s.ipcs = 1;
	}
#endif

	oce_dma_sync(&wq->ring->dma, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);

	wq->ring->nused++;

	/* TX work queue entries for data chunks */
	for (i = 0; i < pkt->nsegs; i++) {
		nfe = oce_ring_get(wq->ring);
		memset(nfe, 0, sizeof(*nfe));
		nfe->u0.s.frag_pa_hi = ADDR_HI(pkt->map->dm_segs[i].ds_addr);
		nfe->u0.s.frag_pa_lo = ADDR_LO(pkt->map->dm_segs[i].ds_addr);
		nfe->u0.s.frag_len = pkt->map->dm_segs[i].ds_len;
		wq->ring->nused++;
	}
	if (nwqe > (pkt->nsegs + 1)) {
		nfe = oce_ring_get(wq->ring);
		memset(nfe, 0, sizeof(*nfe));
		wq->ring->nused++;
		pkt->nsegs++;
	}

	oce_pkt_put(&wq->pkt_list, pkt);

	oce_dma_sync(&wq->ring->dma, BUS_DMASYNC_POSTREAD |
	    BUS_DMASYNC_POSTWRITE);

	oce_write_db(sc, PD_TXULP_DB, wq->id | (nwqe << 16));

	return (0);

error:
	if (pkt)
		oce_pkt_put(&wq->pkt_free, pkt);
	m_freem(*mpp);
	*mpp = NULL;
	return (1);
}

#ifdef OCE_TSO
struct mbuf *
oce_tso(struct oce_softc *sc, struct mbuf **mpp)
{
	struct mbuf *m;
	struct ip *ip;
#ifdef INET6
	struct ip6_hdr *ip6;
#endif
	struct ether_vlan_header *eh;
	struct tcphdr *th;
	uint16_t etype;
	int total_len = 0, ehdrlen = 0;

	m = *mpp;

	if (M_WRITABLE(m) == 0) {
		m = m_dup(*mpp, M_DONTWAIT);
		if (!m)
			return (NULL);
		m_freem(*mpp);
		*mpp = m;
	}

	eh = mtod(m, struct ether_vlan_header *);
	if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
		etype = ntohs(eh->evl_proto);
		ehdrlen = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
	} else {
		etype = ntohs(eh->evl_encap_proto);
		ehdrlen = ETHER_HDR_LEN;
	}

	switch (etype) {
	case ETHERTYPE_IP:
		ip = (struct ip *)(m->m_data + ehdrlen);
		if (ip->ip_p != IPPROTO_TCP)
			return (NULL);
		th = (struct tcphdr *)((caddr_t)ip + (ip->ip_hl << 2));

		total_len = ehdrlen + (ip->ip_hl << 2) + (th->th_off << 2);
		break;
#ifdef INET6
	case ETHERTYPE_IPV6:
		ip6 = (struct ip6_hdr *)(m->m_data + ehdrlen);
		if (ip6->ip6_nxt != IPPROTO_TCP)
			return NULL;
		th = (struct tcphdr *)((caddr_t)ip6 + sizeof(struct ip6_hdr));

		total_len = ehdrlen + sizeof(struct ip6_hdr) +
		    (th->th_off << 2);
		break;
#endif
	default:
		return (NULL);
	}

	m = m_pullup(m, total_len);
	if (!m)
		return (NULL);
	*mpp = m;
	return (m);

}
#endif /* OCE_TSO */

int
oce_intr(void *arg)
{
	struct oce_softc *sc = arg;
	struct oce_eq *eq = sc->sc_eq[0];
	struct oce_eqe *eqe;
	struct oce_cq *cq = NULL;
	int i, neqe = 0;

	oce_dma_sync(&eq->ring->dma, BUS_DMASYNC_POSTREAD);

	OCE_RING_FOREACH(eq->ring, eqe, eqe->evnt != 0) {
		eqe->evnt = 0;
		neqe++;
	}

	/* Spurious? */
	if (!neqe) {
		oce_arm_eq(eq, 0, TRUE, FALSE);
		return (0);
	}

	oce_dma_sync(&eq->ring->dma, BUS_DMASYNC_PREWRITE);

 	/* Clear EQ entries, but dont arm */
	oce_arm_eq(eq, neqe, FALSE, TRUE);

	/* Process TX, RX and MCC completion queues */
	for (i = 0; i < eq->cq_valid; i++) {
		cq = eq->cq[i];
		(*cq->cq_intr)(cq->cb_arg);
		oce_arm_cq(cq, 0, TRUE);
	}

	oce_arm_eq(eq, 0, TRUE, FALSE);
	return (1);
}

/* Handle the Completion Queue for transmit */
void
oce_intr_wq(void *arg)
{
	struct oce_wq *wq = (struct oce_wq *)arg;
	struct oce_cq *cq = wq->cq;
	struct oce_nic_tx_cqe *cqe;
	struct oce_softc *sc = wq->sc;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int ncqe = 0;

	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_POSTREAD);
	OCE_RING_FOREACH(cq->ring, cqe, WQ_CQE_VALID(cqe)) {
		oce_txeof(wq);
		WQ_CQE_INVALIDATE(cqe);
		ncqe++;
	}
	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_PREWRITE);

	if (ifq_is_oactive(&ifp->if_snd)) {
		if (wq->ring->nused < (wq->ring->nitems / 2)) {
			ifq_clr_oactive(&ifp->if_snd);
			oce_start(ifp);
		}
	}
	if (wq->ring->nused == 0)
		ifp->if_timer = 0;

	if (ncqe)
		oce_arm_cq(cq, ncqe, FALSE);
}

void
oce_txeof(struct oce_wq *wq)
{
	struct oce_softc *sc = wq->sc;
	struct oce_pkt *pkt;
	struct mbuf *m;

	if ((pkt = oce_pkt_get(&wq->pkt_list)) == NULL) {
		printf("%s: missing descriptor in txeof\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	wq->ring->nused -= pkt->nsegs + 1;
	bus_dmamap_sync(sc->sc_dmat, pkt->map, 0, pkt->map->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_dmat, pkt->map);

	m = pkt->mbuf;
	m_freem(m);
	pkt->mbuf = NULL;
	oce_pkt_put(&wq->pkt_free, pkt);
}

/* Handle the Completion Queue for receive */
void
oce_intr_rq(void *arg)
{
	struct oce_rq *rq = (struct oce_rq *)arg;
	struct oce_cq *cq = rq->cq;
	struct oce_softc *sc = rq->sc;
	struct oce_nic_rx_cqe *cqe;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int maxrx, ncqe = 0;

	maxrx = IS_XE201(sc) ? 8 : OCE_MAX_RQ_COMPL;

	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_POSTREAD);

	OCE_RING_FOREACH(cq->ring, cqe, RQ_CQE_VALID(cqe) && ncqe <= maxrx) {
		if (cqe->u0.s.error == 0) {
			if (cqe->u0.s.pkt_size == 0)
				/* partial DMA workaround for Lancer */
				oce_rxeoc(rq, cqe);
			else
				oce_rxeof(rq, cqe);
		} else {
			ifp->if_ierrors++;
			if (IS_XE201(sc))
				/* Lancer A0 no buffer workaround */
				oce_rxeoc(rq, cqe);
			else
				/* Post L3/L4 errors to stack.*/
				oce_rxeof(rq, cqe);
		}
#ifdef OCE_LRO
		if (IF_LRO_ENABLED(ifp) && rq->lro_pkts_queued >= 16)
			oce_flush_lro(rq);
#endif
		RQ_CQE_INVALIDATE(cqe);
		ncqe++;
	}

	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_PREWRITE);

#ifdef OCE_LRO
	if (IF_LRO_ENABLED(ifp))
		oce_flush_lro(rq);
#endif

	if (ncqe) {
		oce_arm_cq(cq, ncqe, FALSE);
		if (!oce_alloc_rx_bufs(rq))
			timeout_add(&sc->sc_rxrefill, 1);
	}
}

void
oce_rxeof(struct oce_rq *rq, struct oce_nic_rx_cqe *cqe)
{
	struct oce_softc *sc = rq->sc;
	struct oce_pkt *pkt = NULL;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m = NULL, *tail = NULL;
	int i, len, frag_len;
	uint16_t vtag;

	len = cqe->u0.s.pkt_size;

	 /* Get vlan_tag value */
	if (IS_BE(sc))
		vtag = ntohs(cqe->u0.s.vlan_tag);
	else
		vtag = cqe->u0.s.vlan_tag;

	for (i = 0; i < cqe->u0.s.num_fragments; i++) {
		if ((pkt = oce_pkt_get(&rq->pkt_list)) == NULL) {
			printf("%s: missing descriptor in rxeof\n",
			    sc->sc_dev.dv_xname);
			goto exit;
		}

		bus_dmamap_sync(sc->sc_dmat, pkt->map, 0, pkt->map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, pkt->map);
		if_rxr_put(&rq->rxring, 1);

		frag_len = (len > rq->fragsize) ? rq->fragsize : len;
		pkt->mbuf->m_len = frag_len;

		if (tail != NULL) {
			/* additional fragments */
			pkt->mbuf->m_flags &= ~M_PKTHDR;
			tail->m_next = pkt->mbuf;
			tail = pkt->mbuf;
		} else {
			/* first fragment, fill out most of the header */
			pkt->mbuf->m_pkthdr.len = len;
			pkt->mbuf->m_pkthdr.csum_flags = 0;
			if (cqe->u0.s.ip_cksum_pass) {
				if (!cqe->u0.s.ip_ver) { /* IPV4 */
					pkt->mbuf->m_pkthdr.csum_flags =
					    M_IPV4_CSUM_IN_OK;
				}
			}
			if (cqe->u0.s.l4_cksum_pass) {
				pkt->mbuf->m_pkthdr.csum_flags |=
				    M_TCP_CSUM_IN_OK | M_UDP_CSUM_IN_OK;
			}
			m = tail = pkt->mbuf;
		}
		pkt->mbuf = NULL;
		oce_pkt_put(&rq->pkt_free, pkt);
		len -= frag_len;
	}

	if (m) {
		if (!oce_port_valid(sc, cqe)) {
			 m_freem(m);
			 goto exit;
		}

#if NVLAN > 0
		/* This determines if vlan tag is valid */
		if (oce_vtp_valid(sc, cqe)) {
			if (sc->sc_fmode & FNM_FLEX10_MODE) {
				/* FLEX10. If QnQ is not set, neglect VLAN */
				if (cqe->u0.s.qnq) {
					m->m_pkthdr.ether_vtag = vtag;
					m->m_flags |= M_VLANTAG;
				}
			} else if (sc->sc_pvid != (vtag & VLAN_VID_MASK))  {
				/*
				 * In UMC mode generally pvid will be striped.
				 * But in some cases we have seen it comes
				 * with pvid. So if pvid == vlan, neglect vlan.
				 */
				m->m_pkthdr.ether_vtag = vtag;
				m->m_flags |= M_VLANTAG;
			}
		}
#endif

#ifdef OCE_LRO
		/* Try to queue to LRO */
		if (IF_LRO_ENABLED(ifp) && !(m->m_flags & M_VLANTAG) &&
		    cqe->u0.s.ip_cksum_pass && cqe->u0.s.l4_cksum_pass &&
		    !cqe->u0.s.ip_ver && rq->lro.lro_cnt != 0) {

			if (tcp_lro_rx(&rq->lro, m, 0) == 0) {
				rq->lro_pkts_queued ++;
				goto exit;
			}
			/* If LRO posting fails then try to post to STACK */
		}
#endif

		ml_enqueue(&ml, m);
	}
exit:
	if (ifiq_input(&ifp->if_rcv, &ml))
		if_rxr_livelocked(&rq->rxring);
}

void
oce_rxeoc(struct oce_rq *rq, struct oce_nic_rx_cqe *cqe)
{
	struct oce_softc *sc = rq->sc;
	struct oce_pkt *pkt;
	int i, num_frags = cqe->u0.s.num_fragments;

	if (IS_XE201(sc) && cqe->u0.s.error) {
		/*
		 * Lancer A0 workaround:
		 * num_frags will be 1 more than actual in case of error
		 */
		if (num_frags)
			num_frags--;
	}
	for (i = 0; i < num_frags; i++) {
		if ((pkt = oce_pkt_get(&rq->pkt_list)) == NULL) {
			printf("%s: missing descriptor in rxeoc\n",
			    sc->sc_dev.dv_xname);
			return;
		}
		bus_dmamap_sync(sc->sc_dmat, pkt->map, 0, pkt->map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, pkt->map);
		if_rxr_put(&rq->rxring, 1);
		m_freem(pkt->mbuf);
		oce_pkt_put(&rq->pkt_free, pkt);
	}
}

int
oce_vtp_valid(struct oce_softc *sc, struct oce_nic_rx_cqe *cqe)
{
	struct oce_nic_rx_cqe_v1 *cqe_v1;

	if (IS_BE(sc) && ISSET(sc->sc_flags, OCE_F_BE3_NATIVE)) {
		cqe_v1 = (struct oce_nic_rx_cqe_v1 *)cqe;
		return (cqe_v1->u0.s.vlan_tag_present);
	}
	return (cqe->u0.s.vlan_tag_present);
}

int
oce_port_valid(struct oce_softc *sc, struct oce_nic_rx_cqe *cqe)
{
	struct oce_nic_rx_cqe_v1 *cqe_v1;

	if (IS_BE(sc) && ISSET(sc->sc_flags, OCE_F_BE3_NATIVE)) {
		cqe_v1 = (struct oce_nic_rx_cqe_v1 *)cqe;
		if (sc->sc_port != cqe_v1->u0.s.port)
			return (0);
	}
	return (1);
}

#ifdef OCE_LRO
void
oce_flush_lro(struct oce_rq *rq)
{
	struct oce_softc *sc = rq->sc;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct lro_ctrl	*lro = &rq->lro;
	struct lro_entry *queued;

	if (!IF_LRO_ENABLED(ifp))
		return;

	while ((queued = SLIST_FIRST(&lro->lro_active)) != NULL) {
		SLIST_REMOVE_HEAD(&lro->lro_active, next);
		tcp_lro_flush(lro, queued);
	}
	rq->lro_pkts_queued = 0;
}

int
oce_init_lro(struct oce_softc *sc)
{
	struct lro_ctrl *lro = NULL;
	int i = 0, rc = 0;

	for (i = 0; i < sc->sc_nrq; i++) {
		lro = &sc->sc_rq[i]->lro;
		rc = tcp_lro_init(lro);
		if (rc != 0) {
			printf("%s: LRO init failed\n",
			    sc->sc_dev.dv_xname);
			return rc;
		}
		lro->ifp = &sc->sc_ac.ac_if;
	}

	return (rc);
}

void
oce_free_lro(struct oce_softc *sc)
{
	struct lro_ctrl *lro = NULL;
	int i = 0;

	for (i = 0; i < sc->sc_nrq; i++) {
		lro = &sc->sc_rq[i]->lro;
		if (lro)
			tcp_lro_free(lro);
	}
}
#endif /* OCE_LRO */

int
oce_get_buf(struct oce_rq *rq)
{
	struct oce_softc *sc = rq->sc;
	struct oce_pkt *pkt;
	struct oce_nic_rqe *rqe;

	if ((pkt = oce_pkt_get(&rq->pkt_free)) == NULL)
		return (0);

	pkt->mbuf = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
	if (pkt->mbuf == NULL) {
		oce_pkt_put(&rq->pkt_free, pkt);
		return (0);
	}

	pkt->mbuf->m_len = pkt->mbuf->m_pkthdr.len = MCLBYTES;
#ifdef __STRICT_ALIGNMENT
	m_adj(pkt->mbuf, ETHER_ALIGN);
#endif

	if (bus_dmamap_load_mbuf(sc->sc_dmat, pkt->map, pkt->mbuf,
	    BUS_DMA_NOWAIT)) {
		m_freem(pkt->mbuf);
		pkt->mbuf = NULL;
		oce_pkt_put(&rq->pkt_free, pkt);
		return (0);
	}

	bus_dmamap_sync(sc->sc_dmat, pkt->map, 0, pkt->map->dm_mapsize,
	    BUS_DMASYNC_PREREAD);

	oce_dma_sync(&rq->ring->dma, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);

	rqe = oce_ring_get(rq->ring);
	rqe->u0.s.frag_pa_hi = ADDR_HI(pkt->map->dm_segs[0].ds_addr);
	rqe->u0.s.frag_pa_lo = ADDR_LO(pkt->map->dm_segs[0].ds_addr);

	oce_dma_sync(&rq->ring->dma, BUS_DMASYNC_POSTREAD |
	    BUS_DMASYNC_POSTWRITE);

	oce_pkt_put(&rq->pkt_list, pkt);

	return (1);
}

int
oce_alloc_rx_bufs(struct oce_rq *rq)
{
	struct oce_softc *sc = rq->sc;
	int i, nbufs = 0;
	u_int slots;

	for (slots = if_rxr_get(&rq->rxring, rq->nitems); slots > 0; slots--) {
		if (oce_get_buf(rq) == 0)
			break;

		nbufs++;
	}
	if_rxr_put(&rq->rxring, slots);

	if (!nbufs)
		return (0);
	for (i = nbufs / OCE_MAX_RQ_POSTS; i > 0; i--) {
		oce_write_db(sc, PD_RXULP_DB, rq->id |
		    (OCE_MAX_RQ_POSTS << 24));
		nbufs -= OCE_MAX_RQ_POSTS;
	}
	if (nbufs > 0)
		oce_write_db(sc, PD_RXULP_DB, rq->id | (nbufs << 24));
	return (1);
}

void
oce_refill_rx(void *arg)
{
	struct oce_softc *sc = arg;
	struct oce_rq *rq;
	int i, s;

	s = splnet();
	OCE_RQ_FOREACH(sc, rq, i) {
		if (!oce_alloc_rx_bufs(rq))
			timeout_add(&sc->sc_rxrefill, 5);
	}
	splx(s);
}

/* Handle the Completion Queue for the Mailbox/Async notifications */
void
oce_intr_mq(void *arg)
{
	struct oce_mq *mq = (struct oce_mq *)arg;
	struct oce_softc *sc = mq->sc;
	struct oce_cq *cq = mq->cq;
	struct oce_mq_cqe *cqe;
	struct oce_async_cqe_link_state *acqe;
	struct oce_async_event_grp5_pvid_state *gcqe;
	int evtype, optype, ncqe = 0;

	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_POSTREAD);

	OCE_RING_FOREACH(cq->ring, cqe, MQ_CQE_VALID(cqe)) {
		if (cqe->u0.s.async_event) {
			evtype = cqe->u0.s.event_type;
			optype = cqe->u0.s.async_type;
			if (evtype  == ASYNC_EVENT_CODE_LINK_STATE) {
				/* Link status evt */
				acqe = (struct oce_async_cqe_link_state *)cqe;
				oce_link_event(sc, acqe);
			} else if ((evtype == ASYNC_EVENT_GRP5) &&
				   (optype == ASYNC_EVENT_PVID_STATE)) {
				/* GRP5 PVID */
				gcqe =
				(struct oce_async_event_grp5_pvid_state *)cqe;
				if (gcqe->enabled)
					sc->sc_pvid =
					    gcqe->tag & VLAN_VID_MASK;
				else
					sc->sc_pvid = 0;
			}
		}
		MQ_CQE_INVALIDATE(cqe);
		ncqe++;
	}

	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_PREWRITE);

	if (ncqe)
		oce_arm_cq(cq, ncqe, FALSE);
}

void
oce_link_event(struct oce_softc *sc, struct oce_async_cqe_link_state *acqe)
{
	/* Update Link status */
	sc->sc_link_up = ((acqe->u0.s.link_status & ~ASYNC_EVENT_LOGICAL) ==
	    ASYNC_EVENT_LINK_UP);
	/* Update speed */
	sc->sc_link_speed = acqe->u0.s.speed;
	oce_link_status(sc);
}

int
oce_init_queues(struct oce_softc *sc)
{
	struct oce_wq *wq;
	struct oce_rq *rq;
	int i;

	sc->sc_nrq = 1;
	sc->sc_nwq = 1;

	/* Create network interface on card */
	if (oce_create_iface(sc, sc->sc_macaddr))
		goto error;

	/* create all of the event queues */
	for (i = 0; i < sc->sc_nintr; i++) {
		sc->sc_eq[i] = oce_create_eq(sc);
		if (!sc->sc_eq[i])
			goto error;
	}

	/* alloc tx queues */
	OCE_WQ_FOREACH(sc, wq, i) {
		sc->sc_wq[i] = oce_create_wq(sc, sc->sc_eq[i]);
		if (!sc->sc_wq[i])
			goto error;
	}

	/* alloc rx queues */
	OCE_RQ_FOREACH(sc, rq, i) {
		sc->sc_rq[i] = oce_create_rq(sc, sc->sc_eq[i > 0 ? i - 1 : 0],
		    i > 0 ? sc->sc_rss_enable : 0);
		if (!sc->sc_rq[i])
			goto error;
	}

	/* alloc mailbox queue */
	sc->sc_mq = oce_create_mq(sc, sc->sc_eq[0]);
	if (!sc->sc_mq)
		goto error;

	return (0);
error:
	oce_release_queues(sc);
	return (1);
}

void
oce_release_queues(struct oce_softc *sc)
{
	struct oce_wq *wq;
	struct oce_rq *rq;
	struct oce_eq *eq;
	int i;

	OCE_RQ_FOREACH(sc, rq, i) {
		if (rq)
			oce_destroy_rq(sc->sc_rq[i]);
	}

	OCE_WQ_FOREACH(sc, wq, i) {
		if (wq)
			oce_destroy_wq(sc->sc_wq[i]);
	}

	if (sc->sc_mq)
		oce_destroy_mq(sc->sc_mq);

	OCE_EQ_FOREACH(sc, eq, i) {
		if (eq)
			oce_destroy_eq(sc->sc_eq[i]);
	}
}

/**
 * @brief 		Function to create a WQ for NIC Tx
 * @param sc 		software handle to the device
 * @returns		the pointer to the WQ created or NULL on failure
 */
struct oce_wq *
oce_create_wq(struct oce_softc *sc, struct oce_eq *eq)
{
	struct oce_wq *wq;
	struct oce_cq *cq;
	struct oce_pkt *pkt;
	int i;

	if (sc->sc_tx_ring_size < 256 || sc->sc_tx_ring_size > 2048)
		return (NULL);

	wq = malloc(sizeof(struct oce_wq), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!wq)
		return (NULL);

	wq->ring = oce_create_ring(sc, sc->sc_tx_ring_size, NIC_WQE_SIZE, 8);
	if (!wq->ring) {
		free(wq, M_DEVBUF, 0);
		return (NULL);
	}

	cq = oce_create_cq(sc, eq, CQ_LEN_512, sizeof(struct oce_nic_tx_cqe),
	    1, 0, 3);
	if (!cq) {
		oce_destroy_ring(sc, wq->ring);
		free(wq, M_DEVBUF, 0);
		return (NULL);
	}

	wq->id = -1;
	wq->sc = sc;

	wq->cq = cq;
	wq->nitems = sc->sc_tx_ring_size;

	SIMPLEQ_INIT(&wq->pkt_free);
	SIMPLEQ_INIT(&wq->pkt_list);

	for (i = 0; i < sc->sc_tx_ring_size / 2; i++) {
		pkt = oce_pkt_alloc(sc, OCE_MAX_TX_SIZE, OCE_MAX_TX_ELEMENTS,
		    PAGE_SIZE);
		if (pkt == NULL) {
			oce_destroy_wq(wq);
			return (NULL);
		}
		oce_pkt_put(&wq->pkt_free, pkt);
	}

	if (oce_new_wq(sc, wq)) {
		oce_destroy_wq(wq);
		return (NULL);
	}

	eq->cq[eq->cq_valid] = cq;
	eq->cq_valid++;
	cq->cb_arg = wq;
	cq->cq_intr = oce_intr_wq;

	return (wq);
}

void
oce_drain_wq(struct oce_wq *wq)
{
	struct oce_cq *cq = wq->cq;
	struct oce_nic_tx_cqe *cqe;
	int ncqe = 0;

	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_POSTREAD);
	OCE_RING_FOREACH(cq->ring, cqe, WQ_CQE_VALID(cqe)) {
		WQ_CQE_INVALIDATE(cqe);
		ncqe++;
	}
	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_PREWRITE);
	oce_arm_cq(cq, ncqe, FALSE);
}

void
oce_destroy_wq(struct oce_wq *wq)
{
	struct mbx_delete_nic_wq cmd;
	struct oce_softc *sc = wq->sc;
	struct oce_pkt *pkt;

	if (wq->id >= 0) {
		memset(&cmd, 0, sizeof(cmd));
		cmd.params.req.wq_id = htole16(wq->id);
		oce_cmd(sc, SUBSYS_NIC, OPCODE_NIC_DELETE_WQ, OCE_MBX_VER_V0,
		    &cmd, sizeof(cmd));
	}
	if (wq->cq != NULL)
		oce_destroy_cq(wq->cq);
	if (wq->ring != NULL)
		oce_destroy_ring(sc, wq->ring);
	while ((pkt = oce_pkt_get(&wq->pkt_free)) != NULL)
		oce_pkt_free(sc, pkt);
	free(wq, M_DEVBUF, 0);
}

/**
 * @brief 		function to allocate receive queue resources
 * @param sc		software handle to the device
 * @param eq		pointer to associated event queue
 * @param rss		is-rss-queue flag
 * @returns		the pointer to the RQ created or NULL on failure
 */
struct oce_rq *
oce_create_rq(struct oce_softc *sc, struct oce_eq *eq, int rss)
{
	struct oce_rq *rq;
	struct oce_cq *cq;
	struct oce_pkt *pkt;
	int i;

	/* Hardware doesn't support any other value */
	if (sc->sc_rx_ring_size != 1024)
		return (NULL);

	rq = malloc(sizeof(struct oce_rq), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!rq)
		return (NULL);

	rq->ring = oce_create_ring(sc, sc->sc_rx_ring_size,
	    sizeof(struct oce_nic_rqe), 2);
	if (!rq->ring) {
		free(rq, M_DEVBUF, 0);
		return (NULL);
	}

	cq = oce_create_cq(sc, eq, CQ_LEN_1024, sizeof(struct oce_nic_rx_cqe),
	    1, 0, 3);
	if (!cq) {
		oce_destroy_ring(sc, rq->ring);
		free(rq, M_DEVBUF, 0);
		return (NULL);
	}

	rq->id = -1;
	rq->sc = sc;

	rq->nitems = sc->sc_rx_ring_size;
	rq->fragsize = OCE_RX_BUF_SIZE;
	rq->rss = rss;

	SIMPLEQ_INIT(&rq->pkt_free);
	SIMPLEQ_INIT(&rq->pkt_list);

	for (i = 0; i < sc->sc_rx_ring_size; i++) {
		pkt = oce_pkt_alloc(sc, OCE_RX_BUF_SIZE, 1, OCE_RX_BUF_SIZE);
		if (pkt == NULL) {
			oce_destroy_rq(rq);
			return (NULL);
		}
		oce_pkt_put(&rq->pkt_free, pkt);
	}

	rq->cq = cq;
	eq->cq[eq->cq_valid] = cq;
	eq->cq_valid++;
	cq->cb_arg = rq;
	cq->cq_intr = oce_intr_rq;

	/* RX queue is created in oce_init */

	return (rq);
}

void
oce_drain_rq(struct oce_rq *rq)
{
	struct oce_nic_rx_cqe *cqe;
	struct oce_cq *cq = rq->cq;
	int ncqe = 0;

	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_POSTREAD);
	OCE_RING_FOREACH(cq->ring, cqe, RQ_CQE_VALID(cqe)) {
		RQ_CQE_INVALIDATE(cqe);
		ncqe++;
	}
	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_PREWRITE);
	oce_arm_cq(cq, ncqe, FALSE);
}

void
oce_destroy_rq(struct oce_rq *rq)
{
	struct mbx_delete_nic_rq cmd;
	struct oce_softc *sc = rq->sc;
	struct oce_pkt *pkt;

	if (rq->id >= 0) {
		memset(&cmd, 0, sizeof(cmd));
		cmd.params.req.rq_id = htole16(rq->id);
		oce_cmd(sc, SUBSYS_NIC, OPCODE_NIC_DELETE_RQ, OCE_MBX_VER_V0,
		    &cmd, sizeof(cmd));
	}
	if (rq->cq != NULL)
		oce_destroy_cq(rq->cq);
	if (rq->ring != NULL)
		oce_destroy_ring(sc, rq->ring);
	while ((pkt = oce_pkt_get(&rq->pkt_free)) != NULL)
		oce_pkt_free(sc, pkt);
	free(rq, M_DEVBUF, 0);
}

struct oce_eq *
oce_create_eq(struct oce_softc *sc)
{
	struct oce_eq *eq;

	/* allocate an eq */
	eq = malloc(sizeof(struct oce_eq), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (eq == NULL)
		return (NULL);

	eq->ring = oce_create_ring(sc, EQ_LEN_1024, EQE_SIZE_4, 8);
	if (!eq->ring) {
		free(eq, M_DEVBUF, 0);
		return (NULL);
	}

	eq->id = -1;
	eq->sc = sc;
	eq->nitems = EQ_LEN_1024;	/* length of event queue */
	eq->isize = EQE_SIZE_4; 	/* size of a queue item */
	eq->delay = OCE_DEFAULT_EQD;	/* event queue delay */

	if (oce_new_eq(sc, eq)) {
		oce_destroy_ring(sc, eq->ring);
		free(eq, M_DEVBUF, 0);
		return (NULL);
	}

	return (eq);
}

/**
 * @brief		Function to arm an EQ so that it can generate events
 * @param eq		pointer to event queue structure
 * @param neqe		number of EQEs to arm
 * @param rearm		rearm bit enable/disable
 * @param clearint	bit to clear the interrupt condition because of which
 *			EQEs are generated
 */
static inline void
oce_arm_eq(struct oce_eq *eq, int neqe, int rearm, int clearint)
{
	oce_write_db(eq->sc, PD_EQ_DB, eq->id | PD_EQ_DB_EVENT |
	    (clearint << 9) | (neqe << 16) | (rearm << 29));
}

void
oce_drain_eq(struct oce_eq *eq)
{
	struct oce_eqe *eqe;
	int neqe = 0;

	oce_dma_sync(&eq->ring->dma, BUS_DMASYNC_POSTREAD);
	OCE_RING_FOREACH(eq->ring, eqe, eqe->evnt != 0) {
		eqe->evnt = 0;
		neqe++;
	}
	oce_dma_sync(&eq->ring->dma, BUS_DMASYNC_PREWRITE);
	oce_arm_eq(eq, neqe, FALSE, TRUE);
}

void
oce_destroy_eq(struct oce_eq *eq)
{
	struct mbx_destroy_common_eq cmd;
	struct oce_softc *sc = eq->sc;

	if (eq->id >= 0) {
		memset(&cmd, 0, sizeof(cmd));
		cmd.params.req.id = htole16(eq->id);
		oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_DESTROY_EQ,
		    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	}
	if (eq->ring != NULL)
		oce_destroy_ring(sc, eq->ring);
	free(eq, M_DEVBUF, 0);
}

struct oce_mq *
oce_create_mq(struct oce_softc *sc, struct oce_eq *eq)
{
	struct oce_mq *mq = NULL;
	struct oce_cq *cq;

	/* allocate the mq */
	mq = malloc(sizeof(struct oce_mq), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!mq)
		return (NULL);

	mq->ring = oce_create_ring(sc, 128, sizeof(struct oce_mbx), 8);
	if (!mq->ring) {
		free(mq, M_DEVBUF, 0);
		return (NULL);
	}

	cq = oce_create_cq(sc, eq, CQ_LEN_256, sizeof(struct oce_mq_cqe),
	    1, 0, 0);
	if (!cq) {
		oce_destroy_ring(sc, mq->ring);
		free(mq, M_DEVBUF, 0);
		return (NULL);
	}

	mq->id = -1;
	mq->sc = sc;
	mq->cq = cq;

	mq->nitems = 128;

	if (oce_new_mq(sc, mq)) {
		oce_destroy_cq(mq->cq);
		oce_destroy_ring(sc, mq->ring);
		free(mq, M_DEVBUF, 0);
		return (NULL);
	}

	eq->cq[eq->cq_valid] = cq;
	eq->cq_valid++;
	mq->cq->eq = eq;
	mq->cq->cb_arg = mq;
	mq->cq->cq_intr = oce_intr_mq;

	return (mq);
}

void
oce_drain_mq(struct oce_mq *mq)
{
	struct oce_cq *cq = mq->cq;
	struct oce_mq_cqe *cqe;
	int ncqe = 0;

	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_POSTREAD);
	OCE_RING_FOREACH(cq->ring, cqe, MQ_CQE_VALID(cqe)) {
		MQ_CQE_INVALIDATE(cqe);
		ncqe++;
	}
	oce_dma_sync(&cq->ring->dma, BUS_DMASYNC_PREWRITE);
	oce_arm_cq(cq, ncqe, FALSE);
}

void
oce_destroy_mq(struct oce_mq *mq)
{
	struct mbx_destroy_common_mq cmd;
	struct oce_softc *sc = mq->sc;

	if (mq->id >= 0) {
		memset(&cmd, 0, sizeof(cmd));
		cmd.params.req.id = htole16(mq->id);
		oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_DESTROY_MQ,
		    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	}
	if (mq->ring != NULL)
		oce_destroy_ring(sc, mq->ring);
	if (mq->cq != NULL)
		oce_destroy_cq(mq->cq);
	free(mq, M_DEVBUF, 0);
}

/**
 * @brief		Function to create a completion queue
 * @param sc		software handle to the device
 * @param eq		optional eq to be associated with to the cq
 * @param nitems	length of completion queue
 * @param isize		size of completion queue items
 * @param eventable	event table
 * @param nodelay	no delay flag
 * @param ncoalesce	no coalescence flag
 * @returns 		pointer to the cq created, NULL on failure
 */
struct oce_cq *
oce_create_cq(struct oce_softc *sc, struct oce_eq *eq, int nitems, int isize,
    int eventable, int nodelay, int ncoalesce)
{
	struct oce_cq *cq = NULL;

	cq = malloc(sizeof(struct oce_cq), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!cq)
		return (NULL);

	cq->ring = oce_create_ring(sc, nitems, isize, 4);
	if (!cq->ring) {
		free(cq, M_DEVBUF, 0);
		return (NULL);
	}

	cq->sc = sc;
	cq->eq = eq;
	cq->nitems = nitems;
	cq->nodelay = nodelay;
	cq->ncoalesce = ncoalesce;
	cq->eventable = eventable;

	if (oce_new_cq(sc, cq)) {
		oce_destroy_ring(sc, cq->ring);
		free(cq, M_DEVBUF, 0);
		return (NULL);
	}

	sc->sc_cq[sc->sc_ncq++] = cq;

	return (cq);
}

void
oce_destroy_cq(struct oce_cq *cq)
{
	struct mbx_destroy_common_cq cmd;
	struct oce_softc *sc = cq->sc;

	if (cq->id >= 0) {
		memset(&cmd, 0, sizeof(cmd));
		cmd.params.req.id = htole16(cq->id);
		oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_DESTROY_CQ,
		    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	}
	if (cq->ring != NULL)
		oce_destroy_ring(sc, cq->ring);
	free(cq, M_DEVBUF, 0);
}

/**
 * @brief		Function to arm a CQ with CQEs
 * @param cq		pointer to the completion queue structure
 * @param ncqe		number of CQEs to arm
 * @param rearm		rearm bit enable/disable
 */
static inline void
oce_arm_cq(struct oce_cq *cq, int ncqe, int rearm)
{
	oce_write_db(cq->sc, PD_CQ_DB, cq->id | (ncqe << 16) | (rearm << 29));
}

void
oce_free_posted_rxbuf(struct oce_rq *rq)
{
	struct oce_softc *sc = rq->sc;
	struct oce_pkt *pkt;

	while ((pkt = oce_pkt_get(&rq->pkt_list)) != NULL) {
		bus_dmamap_sync(sc->sc_dmat, pkt->map, 0, pkt->map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, pkt->map);
		if (pkt->mbuf != NULL) {
			m_freem(pkt->mbuf);
			pkt->mbuf = NULL;
		}
		oce_pkt_put(&rq->pkt_free, pkt);
		if_rxr_put(&rq->rxring, 1);
	}
}

int
oce_dma_alloc(struct oce_softc *sc, bus_size_t size, struct oce_dma_mem *dma)
{
	int rc;

	memset(dma, 0, sizeof(struct oce_dma_mem));

	dma->tag = sc->sc_dmat;
	rc = bus_dmamap_create(dma->tag, size, 1, size, 0, BUS_DMA_NOWAIT,
	    &dma->map);
	if (rc != 0) {
		printf("%s: failed to allocate DMA handle",
		    sc->sc_dev.dv_xname);
		goto fail_0;
	}

	rc = bus_dmamem_alloc(dma->tag, size, PAGE_SIZE, 0, &dma->segs, 1,
	    &dma->nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (rc != 0) {
		printf("%s: failed to allocate DMA memory",
		    sc->sc_dev.dv_xname);
		goto fail_1;
	}

	rc = bus_dmamem_map(dma->tag, &dma->segs, dma->nsegs, size,
	    &dma->vaddr, BUS_DMA_NOWAIT);
	if (rc != 0) {
		printf("%s: failed to map DMA memory", sc->sc_dev.dv_xname);
		goto fail_2;
	}

	rc = bus_dmamap_load(dma->tag, dma->map, dma->vaddr, size, NULL,
	    BUS_DMA_NOWAIT);
	if (rc != 0) {
		printf("%s: failed to load DMA memory", sc->sc_dev.dv_xname);
		goto fail_3;
	}

	bus_dmamap_sync(dma->tag, dma->map, 0, dma->map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	dma->paddr = dma->map->dm_segs[0].ds_addr;
	dma->size = size;

	return (0);

fail_3:
	bus_dmamem_unmap(dma->tag, dma->vaddr, size);
fail_2:
	bus_dmamem_free(dma->tag, &dma->segs, dma->nsegs);
fail_1:
	bus_dmamap_destroy(dma->tag, dma->map);
fail_0:
	return (rc);
}

void
oce_dma_free(struct oce_softc *sc, struct oce_dma_mem *dma)
{
	if (dma->tag == NULL)
		return;

	if (dma->map != NULL) {
		oce_dma_sync(dma, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(dma->tag, dma->map);

		if (dma->vaddr != 0) {
			bus_dmamem_free(dma->tag, &dma->segs, dma->nsegs);
			dma->vaddr = 0;
		}

		bus_dmamap_destroy(dma->tag, dma->map);
		dma->map = NULL;
		dma->tag = NULL;
	}
}

struct oce_ring *
oce_create_ring(struct oce_softc *sc, int nitems, int isize, int maxsegs)
{
	struct oce_dma_mem *dma;
	struct oce_ring *ring;
	bus_size_t size = nitems * isize;
	int rc;

	if (size > maxsegs * PAGE_SIZE)
		return (NULL);

	ring = malloc(sizeof(struct oce_ring), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ring == NULL)
		return (NULL);

	ring->isize = isize;
	ring->nitems = nitems;

	dma = &ring->dma;
	dma->tag = sc->sc_dmat;
	rc = bus_dmamap_create(dma->tag, size, maxsegs, PAGE_SIZE, 0,
	    BUS_DMA_NOWAIT, &dma->map);
	if (rc != 0) {
		printf("%s: failed to allocate DMA handle",
		    sc->sc_dev.dv_xname);
		goto fail_0;
	}

	rc = bus_dmamem_alloc(dma->tag, size, 0, 0, &dma->segs, maxsegs,
	    &dma->nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (rc != 0) {
		printf("%s: failed to allocate DMA memory",
		    sc->sc_dev.dv_xname);
		goto fail_1;
	}

	rc = bus_dmamem_map(dma->tag, &dma->segs, dma->nsegs, size,
	    &dma->vaddr, BUS_DMA_NOWAIT);
	if (rc != 0) {
		printf("%s: failed to map DMA memory", sc->sc_dev.dv_xname);
		goto fail_2;
	}

	bus_dmamap_sync(dma->tag, dma->map, 0, dma->map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	dma->paddr = 0;
	dma->size = size;

	return (ring);

fail_2:
	bus_dmamem_free(dma->tag, &dma->segs, dma->nsegs);
fail_1:
	bus_dmamap_destroy(dma->tag, dma->map);
fail_0:
	free(ring, M_DEVBUF, 0);
	return (NULL);
}

void
oce_destroy_ring(struct oce_softc *sc, struct oce_ring *ring)
{
	oce_dma_free(sc, &ring->dma);
	free(ring, M_DEVBUF, 0);
}

int
oce_load_ring(struct oce_softc *sc, struct oce_ring *ring,
    struct oce_pa *pa, int maxsegs)
{
	struct oce_dma_mem *dma = &ring->dma;
	int i;

	if (bus_dmamap_load(dma->tag, dma->map, dma->vaddr,
	    ring->isize * ring->nitems, NULL, BUS_DMA_NOWAIT)) {
		printf("%s: failed to load a ring map\n", sc->sc_dev.dv_xname);
		return (0);
	}

	if (dma->map->dm_nsegs > maxsegs) {
		printf("%s: too many segments\n", sc->sc_dev.dv_xname);
		return (0);
	}

	bus_dmamap_sync(dma->tag, dma->map, 0, dma->map->dm_mapsize,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	for (i = 0; i < dma->map->dm_nsegs; i++)
		pa[i].addr = dma->map->dm_segs[i].ds_addr;

	return (dma->map->dm_nsegs);
}

static inline void *
oce_ring_get(struct oce_ring *ring)
{
	int index = ring->index;

	if (++ring->index == ring->nitems)
		ring->index = 0;
	return ((void *)(ring->dma.vaddr + index * ring->isize));
}

static inline void *
oce_ring_first(struct oce_ring *ring)
{
	return ((void *)(ring->dma.vaddr + ring->index * ring->isize));
}

static inline void *
oce_ring_next(struct oce_ring *ring)
{
	if (++ring->index == ring->nitems)
		ring->index = 0;
	return ((void *)(ring->dma.vaddr + ring->index * ring->isize));
}

struct oce_pkt *
oce_pkt_alloc(struct oce_softc *sc, size_t size, int nsegs, int maxsegsz)
{
	struct oce_pkt *pkt;

	if ((pkt = pool_get(oce_pkt_pool, PR_NOWAIT | PR_ZERO)) == NULL)
		return (NULL);

	if (bus_dmamap_create(sc->sc_dmat, size, nsegs, maxsegsz, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &pkt->map)) {
		pool_put(oce_pkt_pool, pkt);
		return (NULL);
	}

	return (pkt);
}

void
oce_pkt_free(struct oce_softc *sc, struct oce_pkt *pkt)
{
	if (pkt->map) {
		bus_dmamap_unload(sc->sc_dmat, pkt->map);
		bus_dmamap_destroy(sc->sc_dmat, pkt->map);
	}
	pool_put(oce_pkt_pool, pkt);
}

static inline struct oce_pkt *
oce_pkt_get(struct oce_pkt_list *lst)
{
	struct oce_pkt *pkt;

	pkt = SIMPLEQ_FIRST(lst);
	if (pkt == NULL)
		return (NULL);

	SIMPLEQ_REMOVE_HEAD(lst, entry);

	return (pkt);
}

static inline void
oce_pkt_put(struct oce_pkt_list *lst, struct oce_pkt *pkt)
{
	SIMPLEQ_INSERT_TAIL(lst, pkt, entry);
}

/**
 * @brief Wait for FW to become ready and reset it
 * @param sc		software handle to the device
 */
int
oce_init_fw(struct oce_softc *sc)
{
	struct ioctl_common_function_reset cmd;
	uint32_t reg;
	int err = 0, tmo = 60000;

	/* read semaphore CSR */
	reg = oce_read_csr(sc, MPU_EP_SEMAPHORE(sc));

	/* if host is ready then wait for fw ready else send POST */
	if ((reg & MPU_EP_SEM_STAGE_MASK) <= POST_STAGE_AWAITING_HOST_RDY) {
		reg = (reg & ~MPU_EP_SEM_STAGE_MASK) | POST_STAGE_CHIP_RESET;
		oce_write_csr(sc, MPU_EP_SEMAPHORE(sc), reg);
	}

	/* wait for FW to become ready */
	for (;;) {
		if (--tmo == 0)
			break;

		DELAY(1000);

		reg = oce_read_csr(sc, MPU_EP_SEMAPHORE(sc));
		if (reg & MPU_EP_SEM_ERROR) {
			printf(": POST failed: %#x\n", reg);
			return (ENXIO);
		}
		if ((reg & MPU_EP_SEM_STAGE_MASK) == POST_STAGE_ARMFW_READY) {
			/* reset FW */
			if (ISSET(sc->sc_flags, OCE_F_RESET_RQD)) {
				memset(&cmd, 0, sizeof(cmd));
				err = oce_cmd(sc, SUBSYS_COMMON,
				    OPCODE_COMMON_FUNCTION_RESET,
				    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
			}
			return (err);
		}
	}

	printf(": POST timed out: %#x\n", reg);

	return (ENXIO);
}

static inline int
oce_mbox_wait(struct oce_softc *sc)
{
	int i;

	for (i = 0; i < 20000; i++) {
		if (oce_read_db(sc, PD_MPU_MBOX_DB) & PD_MPU_MBOX_DB_READY)
			return (0);
		DELAY(100);
	}
	return (ETIMEDOUT);
}

/**
 * @brief Mailbox dispatch
 * @param sc		software handle to the device
 */
int
oce_mbox_dispatch(struct oce_softc *sc)
{
	uint32_t pa, reg;
	int err;

	pa = (uint32_t)((uint64_t)OCE_MEM_DVA(&sc->sc_mbx) >> 34);
	reg = PD_MPU_MBOX_DB_HI | (pa << PD_MPU_MBOX_DB_ADDR_SHIFT);

	if ((err = oce_mbox_wait(sc)) != 0)
		goto out;

	oce_write_db(sc, PD_MPU_MBOX_DB, reg);

	pa = (uint32_t)((uint64_t)OCE_MEM_DVA(&sc->sc_mbx) >> 4) & 0x3fffffff;
	reg = pa << PD_MPU_MBOX_DB_ADDR_SHIFT;

	if ((err = oce_mbox_wait(sc)) != 0)
		goto out;

	oce_write_db(sc, PD_MPU_MBOX_DB, reg);

	oce_dma_sync(&sc->sc_mbx, BUS_DMASYNC_POSTWRITE);

	if ((err = oce_mbox_wait(sc)) != 0)
		goto out;

out:
	oce_dma_sync(&sc->sc_mbx, BUS_DMASYNC_PREREAD);
	return (err);
}

/**
 * @brief Function to initialize the hw with host endian information
 * @param sc		software handle to the device
 * @returns		0 on success, ETIMEDOUT on failure
 */
int
oce_mbox_init(struct oce_softc *sc)
{
	struct oce_bmbx *bmbx = OCE_MEM_KVA(&sc->sc_mbx);
	uint8_t *ptr = (uint8_t *)&bmbx->mbx;

	if (!ISSET(sc->sc_flags, OCE_F_MBOX_ENDIAN_RQD))
		return (0);

	/* Endian Signature */
	*ptr++ = 0xff;
	*ptr++ = 0x12;
	*ptr++ = 0x34;
	*ptr++ = 0xff;
	*ptr++ = 0xff;
	*ptr++ = 0x56;
	*ptr++ = 0x78;
	*ptr = 0xff;

	return (oce_mbox_dispatch(sc));
}

int
oce_cmd(struct oce_softc *sc, int subsys, int opcode, int version,
    void *payload, int length)
{
	struct oce_bmbx *bmbx = OCE_MEM_KVA(&sc->sc_mbx);
	struct oce_mbx *mbx = &bmbx->mbx;
	struct mbx_hdr *hdr;
	caddr_t epayload = NULL;
	int err;

	if (length > OCE_MBX_PAYLOAD)
		epayload = OCE_MEM_KVA(&sc->sc_pld);
	if (length > OCE_MAX_PAYLOAD)
		return (EINVAL);

	oce_dma_sync(&sc->sc_mbx, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	memset(mbx, 0, sizeof(struct oce_mbx));

	mbx->payload_length = length;

	if (epayload) {
		mbx->flags = OCE_MBX_F_SGE;
		oce_dma_sync(&sc->sc_pld, BUS_DMASYNC_PREREAD);
		memcpy(epayload, payload, length);
		mbx->pld.sgl[0].addr = OCE_MEM_DVA(&sc->sc_pld);
		mbx->pld.sgl[0].length = length;
		hdr = (struct mbx_hdr *)epayload;
	} else {
		mbx->flags = OCE_MBX_F_EMBED;
		memcpy(mbx->pld.data, payload, length);
		hdr = (struct mbx_hdr *)&mbx->pld.data;
	}

	hdr->subsys = subsys;
	hdr->opcode = opcode;
	hdr->version = version;
	hdr->length = length - sizeof(*hdr);
	if (opcode == OPCODE_COMMON_FUNCTION_RESET)
		hdr->timeout = 2 * OCE_MBX_TIMEOUT;
	else
		hdr->timeout = OCE_MBX_TIMEOUT;

	if (epayload)
		oce_dma_sync(&sc->sc_pld, BUS_DMASYNC_PREWRITE);

	err = oce_mbox_dispatch(sc);
	if (err == 0) {
		if (epayload) {
			oce_dma_sync(&sc->sc_pld, BUS_DMASYNC_POSTWRITE);
			memcpy(payload, epayload, length);
		} else
			memcpy(payload, &mbx->pld.data, length);
	} else
		printf("%s: mailbox timeout, subsys %d op %d ver %d "
		    "%spayload length %d\n", sc->sc_dev.dv_xname, subsys,
		    opcode, version, epayload ? "ext " : "",
		    length);
	return (err);
}

/**
 * @brief	Firmware will send gracious notifications during
 *		attach only after sending first mcc command. We
 *		use MCC queue only for getting async and mailbox
 *		for sending cmds. So to get gracious notifications
 *		at least send one dummy command on mcc.
 */
void
oce_first_mcc(struct oce_softc *sc)
{
	struct oce_mbx *mbx;
	struct oce_mq *mq = sc->sc_mq;
	struct mbx_hdr *hdr;
	struct mbx_get_common_fw_version *cmd;

	mbx = oce_ring_get(mq->ring);
	memset(mbx, 0, sizeof(struct oce_mbx));

	cmd = (struct mbx_get_common_fw_version *)&mbx->pld.data;

	hdr = &cmd->hdr;
	hdr->subsys = SUBSYS_COMMON;
	hdr->opcode = OPCODE_COMMON_GET_FW_VERSION;
	hdr->version = OCE_MBX_VER_V0;
	hdr->timeout = OCE_MBX_TIMEOUT;
	hdr->length = sizeof(*cmd) - sizeof(*hdr);

	mbx->flags = OCE_MBX_F_EMBED;
	mbx->payload_length = sizeof(*cmd);
	oce_dma_sync(&mq->ring->dma, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);
	oce_write_db(sc, PD_MQ_DB, mq->id | (1 << 16));
}

int
oce_get_fw_config(struct oce_softc *sc)
{
	struct mbx_common_query_fw_config cmd;
	int err;

	memset(&cmd, 0, sizeof(cmd));

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_QUERY_FIRMWARE_CONFIG,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err)
		return (err);

	sc->sc_port = cmd.params.rsp.port_id;
	sc->sc_fmode = cmd.params.rsp.function_mode;

	return (0);
}

int
oce_check_native_mode(struct oce_softc *sc)
{
	struct mbx_common_set_function_cap cmd;
	int err;

	memset(&cmd, 0, sizeof(cmd));

	cmd.params.req.valid_capability_flags = CAP_SW_TIMESTAMPS |
	    CAP_BE3_NATIVE_ERX_API;
	cmd.params.req.capability_flags = CAP_BE3_NATIVE_ERX_API;

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_SET_FUNCTIONAL_CAPS,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err)
		return (err);

	if (cmd.params.rsp.capability_flags & CAP_BE3_NATIVE_ERX_API)
		SET(sc->sc_flags, OCE_F_BE3_NATIVE);

	return (0);
}

/**
 * @brief Function for creating a network interface.
 * @param sc		software handle to the device
 * @returns		0 on success, error otherwise
 */
int
oce_create_iface(struct oce_softc *sc, uint8_t *macaddr)
{
	struct mbx_create_common_iface cmd;
	uint32_t caps, caps_en;
	int err = 0;

	/* interface capabilities to give device when creating interface */
	caps = MBX_RX_IFACE_BROADCAST | MBX_RX_IFACE_UNTAGGED |
	    MBX_RX_IFACE_PROMISC | MBX_RX_IFACE_MCAST_PROMISC |
	    MBX_RX_IFACE_RSS;

	/* capabilities to enable by default (others set dynamically) */
	caps_en = MBX_RX_IFACE_BROADCAST | MBX_RX_IFACE_UNTAGGED;

	if (!IS_XE201(sc)) {
		/* LANCER A0 workaround */
		caps |= MBX_RX_IFACE_PASS_L3L4_ERR;
		caps_en |= MBX_RX_IFACE_PASS_L3L4_ERR;
	}

	/* enable capabilities controlled via driver startup parameters */
	if (sc->sc_rss_enable)
		caps_en |= MBX_RX_IFACE_RSS;

	memset(&cmd, 0, sizeof(cmd));

	cmd.params.req.version = 0;
	cmd.params.req.cap_flags = htole32(caps);
	cmd.params.req.enable_flags = htole32(caps_en);
	if (macaddr != NULL) {
		memcpy(&cmd.params.req.mac_addr[0], macaddr, ETHER_ADDR_LEN);
		cmd.params.req.mac_invalid = 0;
	} else
		cmd.params.req.mac_invalid = 1;

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_CREATE_IFACE,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err)
		return (err);

	sc->sc_if_id = letoh32(cmd.params.rsp.if_id);

	if (macaddr != NULL)
		sc->sc_pmac_id = letoh32(cmd.params.rsp.pmac_id);

	return (0);
}

/**
 * @brief Function to send the mbx command to configure vlan
 * @param sc 		software handle to the device
 * @param vtags		array of vlan tags
 * @param nvtags	number of elements in array
 * @param untagged	boolean TRUE/FLASE
 * @param promisc	flag to enable/disable VLAN promiscuous mode
 * @returns		0 on success, EIO on failure
 */
int
oce_config_vlan(struct oce_softc *sc, struct normal_vlan *vtags, int nvtags,
    int untagged, int promisc)
{
	struct mbx_common_config_vlan cmd;

	memset(&cmd, 0, sizeof(cmd));

	cmd.params.req.if_id = sc->sc_if_id;
	cmd.params.req.promisc = promisc;
	cmd.params.req.untagged = untagged;
	cmd.params.req.num_vlans = nvtags;

	if (!promisc)
		memcpy(cmd.params.req.tags.normal_vlans, vtags,
			nvtags * sizeof(struct normal_vlan));

	return (oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_CONFIG_IFACE_VLAN,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd)));
}

/**
 * @brief Function to set flow control capability in the hardware
 * @param sc 		software handle to the device
 * @param flags		flow control flags to set
 * @returns		0 on success, EIO on failure
 */
int
oce_set_flow_control(struct oce_softc *sc, uint64_t flags)
{
	struct mbx_common_get_set_flow_control cmd;
	int err;

	memset(&cmd, 0, sizeof(cmd));

	cmd.rx_flow_control = flags & IFM_ETH_RXPAUSE ? 1 : 0;
	cmd.tx_flow_control = flags & IFM_ETH_TXPAUSE ? 1 : 0;

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_SET_FLOW_CONTROL,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err)
		return (err);

	memset(&cmd, 0, sizeof(cmd));

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_GET_FLOW_CONTROL,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err)
		return (err);

	sc->sc_fc  = cmd.rx_flow_control ? IFM_ETH_RXPAUSE : 0;
	sc->sc_fc |= cmd.tx_flow_control ? IFM_ETH_TXPAUSE : 0;

	return (0);
}

#ifdef OCE_RSS
/**
 * @brief Function to set flow control capability in the hardware
 * @param sc 		software handle to the device
 * @param enable	0=disable, OCE_RSS_xxx flags otherwise
 * @returns		0 on success, EIO on failure
 */
int
oce_config_rss(struct oce_softc *sc, int enable)
{
	struct mbx_config_nic_rss cmd;
	uint8_t *tbl = &cmd.params.req.cputable;
	int i, j;

	memset(&cmd, 0, sizeof(cmd));

	if (enable)
		cmd.params.req.enable_rss = RSS_ENABLE_IPV4 | RSS_ENABLE_IPV6 |
		    RSS_ENABLE_TCP_IPV4 | RSS_ENABLE_TCP_IPV6;
	cmd.params.req.flush = OCE_FLUSH;
	cmd.params.req.if_id = htole32(sc->sc_if_id);

	arc4random_buf(cmd.params.req.hash, sizeof(cmd.params.req.hash));

	/*
	 * Initialize the RSS CPU indirection table.
	 *
	 * The table is used to choose the queue to place incoming packets.
	 * Incoming packets are hashed.  The lowest bits in the hash result
	 * are used as the index into the CPU indirection table.
	 * Each entry in the table contains the RSS CPU-ID returned by the NIC
	 * create.  Based on the CPU ID, the receive completion is routed to
	 * the corresponding RSS CQs.  (Non-RSS packets are always completed
	 * on the default (0) CQ).
	 */
	for (i = 0, j = 0; j < sc->sc_nrq; j++) {
		if (sc->sc_rq[j]->cfg.is_rss_queue)
			tbl[i++] = sc->sc_rq[j]->rss_cpuid;
	}
	if (i > 0)
		cmd->params.req.cpu_tbl_sz_log2 = htole16(ilog2(i));
	else
		return (ENXIO);

	return (oce_cmd(sc, SUBSYS_NIC, OPCODE_NIC_CONFIG_RSS, OCE_MBX_VER_V0,
	    &cmd, sizeof(cmd)));
}
#endif	/* OCE_RSS */

/**
 * @brief Function for hardware update multicast filter
 * @param sc		software handle to the device
 * @param multi		table of multicast addresses
 * @param naddr		number of multicast addresses in the table
 */
int
oce_update_mcast(struct oce_softc *sc,
    uint8_t multi[][ETHER_ADDR_LEN], int naddr)
{
	struct mbx_set_common_iface_multicast cmd;

	memset(&cmd, 0, sizeof(cmd));

	memcpy(&cmd.params.req.mac[0], &multi[0], naddr * ETHER_ADDR_LEN);
	cmd.params.req.num_mac = htole16(naddr);
	cmd.params.req.if_id = sc->sc_if_id;

	return (oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_SET_IFACE_MULTICAST,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd)));
}

/**
 * @brief RXF function to enable/disable device promiscuous mode
 * @param sc		software handle to the device
 * @param enable	enable/disable flag
 * @returns		0 on success, EIO on failure
 * @note
 *	The OPCODE_NIC_CONFIG_PROMISCUOUS command deprecated for Lancer.
 *	This function uses the COMMON_SET_IFACE_RX_FILTER command instead.
 */
int
oce_set_promisc(struct oce_softc *sc, int enable)
{
	struct mbx_set_common_iface_rx_filter cmd;
	struct iface_rx_filter_ctx *req;

	memset(&cmd, 0, sizeof(cmd));

	req = &cmd.params.req;
	req->if_id = sc->sc_if_id;

	if (enable)
		req->iface_flags = req->iface_flags_mask =
		    MBX_RX_IFACE_PROMISC | MBX_RX_IFACE_VLAN_PROMISC;

	return (oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_SET_IFACE_RX_FILTER,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd)));
}

/**
 * @brief Function to query the link status from the hardware
 * @param sc 		software handle to the device
 * @param[out] link	pointer to the structure returning link attributes
 * @returns		0 on success, EIO on failure
 */
int
oce_get_link_status(struct oce_softc *sc)
{
	struct mbx_query_common_link_config cmd;
	int err;

	memset(&cmd, 0, sizeof(cmd));

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_QUERY_LINK_CONFIG,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err)
		return (err);

	sc->sc_link_up = (letoh32(cmd.params.rsp.logical_link_status) ==
	    NTWK_LOGICAL_LINK_UP);

	if (cmd.params.rsp.mac_speed < 5)
		sc->sc_link_speed = cmd.params.rsp.mac_speed;
	else
		sc->sc_link_speed = 0;

	return (0);
}

void
oce_macaddr_set(struct oce_softc *sc)
{
	uint32_t old_pmac_id = sc->sc_pmac_id;
	int status = 0;

	if (!memcmp(sc->sc_macaddr, sc->sc_ac.ac_enaddr, ETHER_ADDR_LEN))
		return;

	status = oce_macaddr_add(sc, sc->sc_ac.ac_enaddr, &sc->sc_pmac_id);
	if (!status)
		status = oce_macaddr_del(sc, old_pmac_id);
	else
		printf("%s: failed to set MAC address\n", sc->sc_dev.dv_xname);
}

int
oce_macaddr_get(struct oce_softc *sc, uint8_t *macaddr)
{
	struct mbx_query_common_iface_mac cmd;
	int err;

	memset(&cmd, 0, sizeof(cmd));

	cmd.params.req.type = MAC_ADDRESS_TYPE_NETWORK;
	cmd.params.req.permanent = 1;

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_QUERY_IFACE_MAC,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err == 0)
		memcpy(macaddr, &cmd.params.rsp.mac.mac_addr[0],
		    ETHER_ADDR_LEN);
	return (err);
}

int
oce_macaddr_add(struct oce_softc *sc, uint8_t *enaddr, uint32_t *pmac)
{
	struct mbx_add_common_iface_mac cmd;
	int err;

	memset(&cmd, 0, sizeof(cmd));

	cmd.params.req.if_id = htole16(sc->sc_if_id);
	memcpy(cmd.params.req.mac_address, enaddr, ETHER_ADDR_LEN);

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_ADD_IFACE_MAC,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err == 0)
		*pmac = letoh32(cmd.params.rsp.pmac_id);
	return (err);
}

int
oce_macaddr_del(struct oce_softc *sc, uint32_t pmac)
{
	struct mbx_del_common_iface_mac cmd;

	memset(&cmd, 0, sizeof(cmd));

	cmd.params.req.if_id = htole16(sc->sc_if_id);
	cmd.params.req.pmac_id = htole32(pmac);

	return (oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_DEL_IFACE_MAC,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd)));
}

int
oce_new_rq(struct oce_softc *sc, struct oce_rq *rq)
{
	struct mbx_create_nic_rq cmd;
	int err, npages;

	memset(&cmd, 0, sizeof(cmd));

	npages = oce_load_ring(sc, rq->ring, &cmd.params.req.pages[0],
	    nitems(cmd.params.req.pages));
	if (!npages) {
		printf("%s: failed to load the rq ring\n", __func__);
		return (1);
	}

	if (IS_XE201(sc)) {
		cmd.params.req.frag_size = rq->fragsize / 2048;
		cmd.params.req.page_size = 1;
	} else
		cmd.params.req.frag_size = ilog2(rq->fragsize);
	cmd.params.req.num_pages = npages;
	cmd.params.req.cq_id = rq->cq->id;
	cmd.params.req.if_id = htole32(sc->sc_if_id);
	cmd.params.req.max_frame_size = htole16(rq->mtu);
	cmd.params.req.is_rss_queue = htole32(rq->rss);

	err = oce_cmd(sc, SUBSYS_NIC, OPCODE_NIC_CREATE_RQ,
	    IS_XE201(sc) ? OCE_MBX_VER_V1 : OCE_MBX_VER_V0, &cmd,
	    sizeof(cmd));
	if (err)
		return (err);

	rq->id = letoh16(cmd.params.rsp.rq_id);
	rq->rss_cpuid = cmd.params.rsp.rss_cpuid;

	return (0);
}

int
oce_new_wq(struct oce_softc *sc, struct oce_wq *wq)
{
	struct mbx_create_nic_wq cmd;
	int err, npages;

	memset(&cmd, 0, sizeof(cmd));

	npages = oce_load_ring(sc, wq->ring, &cmd.params.req.pages[0],
	    nitems(cmd.params.req.pages));
	if (!npages) {
		printf("%s: failed to load the wq ring\n", __func__);
		return (1);
	}

	if (IS_XE201(sc))
		cmd.params.req.if_id = sc->sc_if_id;
	cmd.params.req.nic_wq_type = NIC_WQ_TYPE_STANDARD;
	cmd.params.req.num_pages = npages;
	cmd.params.req.wq_size = ilog2(wq->nitems) + 1;
	cmd.params.req.cq_id = htole16(wq->cq->id);
	cmd.params.req.ulp_num = 1;

	err = oce_cmd(sc, SUBSYS_NIC, OPCODE_NIC_CREATE_WQ,
	    IS_XE201(sc) ? OCE_MBX_VER_V1 : OCE_MBX_VER_V0, &cmd,
	    sizeof(cmd));
	if (err)
		return (err);

	wq->id = letoh16(cmd.params.rsp.wq_id);

	return (0);
}

int
oce_new_mq(struct oce_softc *sc, struct oce_mq *mq)
{
	struct mbx_create_common_mq_ex cmd;
	union oce_mq_ext_ctx *ctx;
	int err, npages;

	memset(&cmd, 0, sizeof(cmd));

	npages = oce_load_ring(sc, mq->ring, &cmd.params.req.pages[0],
	    nitems(cmd.params.req.pages));
	if (!npages) {
		printf("%s: failed to load the mq ring\n", __func__);
		return (-1);
	}

	ctx = &cmd.params.req.context;
	ctx->v0.num_pages = npages;
	ctx->v0.cq_id = mq->cq->id;
	ctx->v0.ring_size = ilog2(mq->nitems) + 1;
	ctx->v0.valid = 1;
	/* Subscribe to Link State and Group 5 Events(bits 1 and 5 set) */
	ctx->v0.async_evt_bitmap = 0xffffffff;

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_CREATE_MQ_EXT,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err)
		return (err);

	mq->id = letoh16(cmd.params.rsp.mq_id);

	return (0);
}

int
oce_new_eq(struct oce_softc *sc, struct oce_eq *eq)
{
	struct mbx_create_common_eq cmd;
	int err, npages;

	memset(&cmd, 0, sizeof(cmd));

	npages = oce_load_ring(sc, eq->ring, &cmd.params.req.pages[0],
	    nitems(cmd.params.req.pages));
	if (!npages) {
		printf("%s: failed to load the eq ring\n", __func__);
		return (-1);
	}

	cmd.params.req.ctx.num_pages = htole16(npages);
	cmd.params.req.ctx.valid = 1;
	cmd.params.req.ctx.size = (eq->isize == 4) ? 0 : 1;
	cmd.params.req.ctx.count = ilog2(eq->nitems / 256);
	cmd.params.req.ctx.armed = 0;
	cmd.params.req.ctx.delay_mult = htole32(eq->delay);

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_CREATE_EQ,
	    OCE_MBX_VER_V0, &cmd, sizeof(cmd));
	if (err)
		return (err);

	eq->id = letoh16(cmd.params.rsp.eq_id);

	return (0);
}

int
oce_new_cq(struct oce_softc *sc, struct oce_cq *cq)
{
	struct mbx_create_common_cq cmd;
	union oce_cq_ctx *ctx;
	int err, npages;

	memset(&cmd, 0, sizeof(cmd));

	npages = oce_load_ring(sc, cq->ring, &cmd.params.req.pages[0],
	    nitems(cmd.params.req.pages));
	if (!npages) {
		printf("%s: failed to load the cq ring\n", __func__);
		return (-1);
	}

	ctx = &cmd.params.req.cq_ctx;

	if (IS_XE201(sc)) {
		ctx->v2.num_pages = htole16(npages);
		ctx->v2.page_size = 1; /* for 4K */
		ctx->v2.eventable = cq->eventable;
		ctx->v2.valid = 1;
		ctx->v2.count = ilog2(cq->nitems / 256);
		ctx->v2.nodelay = cq->nodelay;
		ctx->v2.coalesce_wm = cq->ncoalesce;
		ctx->v2.armed = 0;
		ctx->v2.eq_id = cq->eq->id;
		if (ctx->v2.count == 3) {
			if (cq->nitems > (4*1024)-1)
				ctx->v2.cqe_count = (4*1024)-1;
			else
				ctx->v2.cqe_count = cq->nitems;
		}
	} else {
		ctx->v0.num_pages = htole16(npages);
		ctx->v0.eventable = cq->eventable;
		ctx->v0.valid = 1;
		ctx->v0.count = ilog2(cq->nitems / 256);
		ctx->v0.nodelay = cq->nodelay;
		ctx->v0.coalesce_wm = cq->ncoalesce;
		ctx->v0.armed = 0;
		ctx->v0.eq_id = cq->eq->id;
	}

	err = oce_cmd(sc, SUBSYS_COMMON, OPCODE_COMMON_CREATE_CQ,
	    IS_XE201(sc) ? OCE_MBX_VER_V2 : OCE_MBX_VER_V0, &cmd,
	    sizeof(cmd));
	if (err)
		return (err);

	cq->id = letoh16(cmd.params.rsp.cq_id);

	return (0);
}

int
oce_init_stats(struct oce_softc *sc)
{
	union cmd {
		struct mbx_get_nic_stats_v0	_be2;
		struct mbx_get_nic_stats	_be3;
		struct mbx_get_pport_stats	_xe201;
	};

	sc->sc_statcmd = malloc(sizeof(union cmd), M_DEVBUF, M_ZERO | M_NOWAIT);
	if (sc->sc_statcmd == NULL) {
		printf("%s: failed to allocate statistics command block\n",
		    sc->sc_dev.dv_xname);
		return (-1);
	}
	return (0);
}

int
oce_update_stats(struct oce_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint64_t rxe, txe;
	int err;

	if (ISSET(sc->sc_flags, OCE_F_BE2))
		err = oce_stats_be2(sc, &rxe, &txe);
	else if (ISSET(sc->sc_flags, OCE_F_BE3))
		err = oce_stats_be3(sc, &rxe, &txe);
	else
		err = oce_stats_xe(sc, &rxe, &txe);
	if (err)
		return (err);

	ifp->if_ierrors += (rxe > sc->sc_rx_errors) ?
	    rxe - sc->sc_rx_errors : sc->sc_rx_errors - rxe;
	sc->sc_rx_errors = rxe;
	ifp->if_oerrors += (txe > sc->sc_tx_errors) ?
	    txe - sc->sc_tx_errors : sc->sc_tx_errors - txe;
	sc->sc_tx_errors = txe;

	return (0);
}

int
oce_stats_be2(struct oce_softc *sc, uint64_t *rxe, uint64_t *txe)
{
	struct mbx_get_nic_stats_v0 *cmd = sc->sc_statcmd;
	struct oce_pmem_stats *ms;
	struct oce_rxf_stats_v0 *rs;
	struct oce_port_rxf_stats_v0 *ps;
	int err;

	memset(cmd, 0, sizeof(*cmd));

	err = oce_cmd(sc, SUBSYS_NIC, OPCODE_NIC_GET_STATS, OCE_MBX_VER_V0,
	    cmd, sizeof(*cmd));
	if (err)
		return (err);

	ms = &cmd->params.rsp.stats.pmem;
	rs = &cmd->params.rsp.stats.rxf;
	ps = &rs->port[sc->sc_port];

	*rxe = ps->rx_crc_errors + ps->rx_in_range_errors +
	    ps->rx_frame_too_long + ps->rx_dropped_runt +
	    ps->rx_ip_checksum_errs + ps->rx_tcp_checksum_errs +
	    ps->rx_udp_checksum_errs + ps->rxpp_fifo_overflow_drop +
	    ps->rx_dropped_tcp_length + ps->rx_dropped_too_small +
	    ps->rx_dropped_too_short + ps->rx_out_range_errors +
	    ps->rx_dropped_header_too_small + ps->rx_input_fifo_overflow_drop +
	    ps->rx_alignment_symbol_errors;
	if (sc->sc_if_id)
		*rxe += rs->port1_jabber_events;
	else
		*rxe += rs->port0_jabber_events;
	*rxe += ms->eth_red_drops;

	*txe = 0; /* hardware doesn't provide any extra tx error statistics */

	return (0);
}

int
oce_stats_be3(struct oce_softc *sc, uint64_t *rxe, uint64_t *txe)
{
	struct mbx_get_nic_stats *cmd = sc->sc_statcmd;
	struct oce_pmem_stats *ms;
	struct oce_rxf_stats_v1 *rs;
	struct oce_port_rxf_stats_v1 *ps;
	int err;

	memset(cmd, 0, sizeof(*cmd));

	err = oce_cmd(sc, SUBSYS_NIC, OPCODE_NIC_GET_STATS, OCE_MBX_VER_V1,
	    cmd, sizeof(*cmd));
	if (err)
		return (err);

	ms = &cmd->params.rsp.stats.pmem;
	rs = &cmd->params.rsp.stats.rxf;
	ps = &rs->port[sc->sc_port];

	*rxe = ps->rx_crc_errors + ps->rx_in_range_errors +
	    ps->rx_frame_too_long + ps->rx_dropped_runt +
	    ps->rx_ip_checksum_errs + ps->rx_tcp_checksum_errs +
	    ps->rx_udp_checksum_errs + ps->rxpp_fifo_overflow_drop +
	    ps->rx_dropped_tcp_length + ps->rx_dropped_too_small +
	    ps->rx_dropped_too_short + ps->rx_out_range_errors +
	    ps->rx_dropped_header_too_small + ps->rx_input_fifo_overflow_drop +
	    ps->rx_alignment_symbol_errors + ps->jabber_events;
	*rxe += ms->eth_red_drops;

	*txe = 0; /* hardware doesn't provide any extra tx error statistics */

	return (0);
}

int
oce_stats_xe(struct oce_softc *sc, uint64_t *rxe, uint64_t *txe)
{
	struct mbx_get_pport_stats *cmd = sc->sc_statcmd;
	struct oce_pport_stats *pps;
	int err;

	memset(cmd, 0, sizeof(*cmd));

	cmd->params.req.reset_stats = 0;
	cmd->params.req.port_number = sc->sc_if_id;

	err = oce_cmd(sc, SUBSYS_NIC, OPCODE_NIC_GET_PPORT_STATS,
	    OCE_MBX_VER_V0, cmd, sizeof(*cmd));
	if (err)
		return (err);

	pps = &cmd->params.rsp.pps;

	*rxe = pps->rx_discards + pps->rx_errors + pps->rx_crc_errors +
	    pps->rx_alignment_errors + pps->rx_symbol_errors +
	    pps->rx_frames_too_long + pps->rx_internal_mac_errors +
	    pps->rx_undersize_pkts + pps->rx_oversize_pkts + pps->rx_jabbers +
	    pps->rx_control_frames_unknown_opcode + pps->rx_in_range_errors +
	    pps->rx_out_of_range_errors + pps->rx_ip_checksum_errors +
	    pps->rx_tcp_checksum_errors + pps->rx_udp_checksum_errors +
	    pps->rx_fifo_overflow + pps->rx_input_fifo_overflow +
	    pps->rx_drops_too_many_frags + pps->rx_drops_mtu;

	*txe = pps->tx_discards + pps->tx_errors + pps->tx_internal_mac_errors;

	return (0);
}
