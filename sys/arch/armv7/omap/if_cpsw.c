/* $OpenBSD: if_cpsw.c,v 1.53 2023/11/10 15:51:19 bluhm Exp $ */
/*	$NetBSD: if_cpsw.c,v 1.3 2013/04/17 14:36:34 bouyer Exp $	*/

/*
 * Copyright (c) 2013 Jonathan A. Kollasch
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 2012 Damjan Marion <dmarion@Freebsd.org>
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
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/pool.h>
#include <sys/queue.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <dev/mii/miivar.h>

#include <arch/armv7/omap/if_cpswreg.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#include <uvm/uvm_extern.h>

#define CPSW_TXFRAGS	16

#define OMAP2SCM_MAC_ID0_LO	0x630
#define OMAP2SCM_MAC_ID0_HI	0x634

#define CPSW_CPPI_RAM_SIZE (0x2000)
#define CPSW_CPPI_RAM_TXDESCS_SIZE (CPSW_CPPI_RAM_SIZE/2)
#define CPSW_CPPI_RAM_RXDESCS_SIZE \
    (CPSW_CPPI_RAM_SIZE - CPSW_CPPI_RAM_TXDESCS_SIZE)
#define CPSW_CPPI_RAM_TXDESCS_BASE (CPSW_CPPI_RAM_OFFSET + 0x0000)
#define CPSW_CPPI_RAM_RXDESCS_BASE \
    (CPSW_CPPI_RAM_OFFSET + CPSW_CPPI_RAM_TXDESCS_SIZE)

#define CPSW_NTXDESCS (CPSW_CPPI_RAM_TXDESCS_SIZE/sizeof(struct cpsw_cpdma_bd))
#define CPSW_NRXDESCS (CPSW_CPPI_RAM_RXDESCS_SIZE/sizeof(struct cpsw_cpdma_bd))

#define CPSW_PAD_LEN (ETHER_MIN_LEN - ETHER_CRC_LEN)

#define TXDESC_NEXT(x) cpsw_txdesc_adjust((x), 1)
#define TXDESC_PREV(x) cpsw_txdesc_adjust((x), -1)

#define RXDESC_NEXT(x) cpsw_rxdesc_adjust((x), 1)
#define RXDESC_PREV(x) cpsw_rxdesc_adjust((x), -1)

struct cpsw_ring_data {
	bus_dmamap_t		 tx_dm[CPSW_NTXDESCS];
	struct mbuf		*tx_mb[CPSW_NTXDESCS];
	bus_dmamap_t		 rx_dm[CPSW_NRXDESCS];
	struct mbuf		*rx_mb[CPSW_NRXDESCS];
};

struct cpsw_port_config {
	uint8_t			 enaddr[ETHER_ADDR_LEN];
	int			 phy_id;
	int			 rgmii;
	int			 vlan;
};

struct cpsw_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_bst;
	bus_space_handle_t	 sc_bsh;
	bus_dma_tag_t		 sc_bdt;
	bus_space_handle_t	 sc_bsh_txdescs;
	bus_space_handle_t	 sc_bsh_rxdescs;
	bus_addr_t		 sc_txdescs_pa;
	bus_addr_t		 sc_rxdescs_pa;

	struct arpcom		 sc_ac;
	struct mii_data		 sc_mii;

	struct cpsw_ring_data	*sc_rdp;
	volatile u_int		 sc_txnext;
	volatile u_int		 sc_txhead;
	volatile u_int		 sc_rxhead;

	void			*sc_rxthih;
	void			*sc_rxih;
	void			*sc_txih;
	void			*sc_miscih;

	void			*sc_txpad;
	bus_dmamap_t		 sc_txpad_dm;
#define sc_txpad_pa sc_txpad_dm->dm_segs[0].ds_addr

	volatile bool		 sc_txrun;
	volatile bool		 sc_rxrun;
	volatile bool		 sc_txeoq;
	volatile bool		 sc_rxeoq;
	struct timeout		 sc_tick;
	int			 sc_active_port;

	struct cpsw_port_config	 sc_port_config[2];
};

#define DEVNAME(_sc) ((_sc)->sc_dev.dv_xname)

int	cpsw_match(struct device *, void *, void *);
void	cpsw_attach(struct device *, struct device *, void *);

void	cpsw_start(struct ifnet *);
int	cpsw_ioctl(struct ifnet *, u_long, caddr_t);
void	cpsw_watchdog(struct ifnet *);
int	cpsw_init(struct ifnet *);
void	cpsw_stop(struct ifnet *);

int	cpsw_mii_readreg(struct device *, int, int);
void	cpsw_mii_writereg(struct device *, int, int, int);
void	cpsw_mii_statchg(struct device *);

void	cpsw_tick(void *);

int	cpsw_new_rxbuf(struct cpsw_softc * const, const u_int);
int	cpsw_mediachange(struct ifnet *);
void	cpsw_mediastatus(struct ifnet *, struct ifmediareq *);

int	cpsw_rxthintr(void *);
int	cpsw_rxintr(void *);
int	cpsw_txintr(void *);
int	cpsw_miscintr(void *);

void	cpsw_get_port_config(struct cpsw_port_config *, int);

const struct cfattach cpsw_ca = {
	sizeof(struct cpsw_softc),
	cpsw_match,
	cpsw_attach
};

struct cfdriver cpsw_cd = {
	NULL,
	"cpsw",
	DV_IFNET
};

static inline u_int
cpsw_txdesc_adjust(u_int x, int y)
{
	return (((x) + y) & (CPSW_NTXDESCS - 1));
}

static inline u_int
cpsw_rxdesc_adjust(u_int x, int y)
{
	return (((x) + y) & (CPSW_NRXDESCS - 1));
}

static inline void
cpsw_set_txdesc_next(struct cpsw_softc * const sc, const u_int i, uint32_t n)
{
	const bus_size_t o = sizeof(struct cpsw_cpdma_bd) * i + 0;
	bus_space_write_4(sc->sc_bst, sc->sc_bsh_txdescs, o, n);
}

static inline void
cpsw_set_rxdesc_next(struct cpsw_softc * const sc, const u_int i, uint32_t n)
{
	const bus_size_t o = sizeof(struct cpsw_cpdma_bd) * i + 0;
	bus_space_write_4(sc->sc_bst, sc->sc_bsh_rxdescs, o, n);
}

static inline void
cpsw_get_txdesc(struct cpsw_softc * const sc, const u_int i,
    struct cpsw_cpdma_bd * const bdp)
{
	const bus_size_t o = sizeof(struct cpsw_cpdma_bd) * i;
	bus_space_read_region_4(sc->sc_bst, sc->sc_bsh_txdescs, o,
	    (uint32_t *)bdp, 4);
}

static inline void
cpsw_set_txdesc(struct cpsw_softc * const sc, const u_int i,
    struct cpsw_cpdma_bd * const bdp)
{
	const bus_size_t o = sizeof(struct cpsw_cpdma_bd) * i;
	bus_space_write_region_4(sc->sc_bst, sc->sc_bsh_txdescs, o,
	    (uint32_t *)bdp, 4);
}

static inline void
cpsw_get_rxdesc(struct cpsw_softc * const sc, const u_int i,
    struct cpsw_cpdma_bd * const bdp)
{
	const bus_size_t o = sizeof(struct cpsw_cpdma_bd) * i;
	bus_space_read_region_4(sc->sc_bst, sc->sc_bsh_rxdescs, o,
	    (uint32_t *)bdp, 4);
}

static inline void
cpsw_set_rxdesc(struct cpsw_softc * const sc, const u_int i,
    struct cpsw_cpdma_bd * const bdp)
{
	const bus_size_t o = sizeof(struct cpsw_cpdma_bd) * i;
	bus_space_write_region_4(sc->sc_bst, sc->sc_bsh_rxdescs, o,
	    (uint32_t *)bdp, 4);
}

static inline bus_addr_t
cpsw_txdesc_paddr(struct cpsw_softc * const sc, u_int x)
{
	KASSERT(x < CPSW_NTXDESCS);
	return sc->sc_txdescs_pa + sizeof(struct cpsw_cpdma_bd) * x;
}

static inline bus_addr_t
cpsw_rxdesc_paddr(struct cpsw_softc * const sc, u_int x)
{
	KASSERT(x < CPSW_NRXDESCS);
	return sc->sc_rxdescs_pa + sizeof(struct cpsw_cpdma_bd) * x;
}

static void
cpsw_mdio_init(struct cpsw_softc *sc)
{
	uint32_t alive, link;
	u_int tries;

	sc->sc_active_port = 0;

	/* Initialize MDIO - ENABLE, PREAMBLE=0, FAULTENB, CLKDIV=0xFF */
	/* TODO Calculate MDCLK=CLK/(CLKDIV+1) */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, MDIOCONTROL,
	    (1<<30) | (1<<18) | 0xFF);

	for(tries = 0; tries < 1000; tries++) {
		alive = bus_space_read_4(sc->sc_bst, sc->sc_bsh, MDIOALIVE) & 3;
		if (alive)
			break;
		delay(1);
	}

	if (alive == 0) {
		printf("%s: no PHY is alive\n", DEVNAME(sc));
		return;
	}

	link = bus_space_read_4(sc->sc_bst, sc->sc_bsh, MDIOLINK) & 3;

	if (alive == 3) {
		/* both ports are alive, prefer one with link */
		if (link == 2)
			sc->sc_active_port = 1;
	} else if (alive == 2)
		sc->sc_active_port = 1;

	/* Select the port to monitor */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, MDIOUSERPHYSEL0,
	    sc->sc_active_port);
}

int
cpsw_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "ti,cpsw");
}

void
cpsw_attach(struct device *parent, struct device *self, void *aux)
{
	struct cpsw_softc *sc = (struct cpsw_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct arpcom * const ac = &sc->sc_ac;
	struct ifnet * const ifp = &ac->ac_if;
	void *descs;
	u_int32_t idver;
	int error;
	int node;
	u_int i;
	uint32_t memsize;

	if (faa->fa_nreg < 1)
		return;

	/*
	 * fa_reg[0].size is size of CPSW_SS and CPSW_PORT
	 * fa_reg[1].size is size of CPSW_WR
	 * we map a size that is a superset of both
	 */
	memsize = 0x4000;

	pinctrl_byname(faa->fa_node, "default");

	for (node = OF_child(faa->fa_node); node; node = OF_peer(node)) {
		if (OF_is_compatible(node, "ti,davinci_mdio")) {
			clock_enable(node, "fck");
			pinctrl_byname(node, "default");
		}
	}

	timeout_set(&sc->sc_tick, cpsw_tick, sc);

	cpsw_get_port_config(sc->sc_port_config, faa->fa_node);
	memcpy(sc->sc_ac.ac_enaddr, sc->sc_port_config[0].enaddr,
	    ETHER_ADDR_LEN);

	sc->sc_rxthih = arm_intr_establish_fdt_idx(faa->fa_node, 0, IPL_NET,
	    cpsw_rxthintr, sc, DEVNAME(sc));
	sc->sc_rxih = arm_intr_establish_fdt_idx(faa->fa_node, 1, IPL_NET,
	    cpsw_rxintr, sc, DEVNAME(sc));
	sc->sc_txih = arm_intr_establish_fdt_idx(faa->fa_node, 2, IPL_NET,
	    cpsw_txintr, sc, DEVNAME(sc));
	sc->sc_miscih = arm_intr_establish_fdt_idx(faa->fa_node, 3, IPL_NET,
	    cpsw_miscintr, sc, DEVNAME(sc));

	sc->sc_bst = faa->fa_iot;
	sc->sc_bdt = faa->fa_dmat;

	error = bus_space_map(sc->sc_bst, faa->fa_reg[0].addr,
	    memsize, BUS_SPACE_MAP_LINEAR, &sc->sc_bsh);
	if (error) {
		printf("can't map registers: %d\n", error);
		return;
	}

	error = bus_space_subregion(sc->sc_bst, sc->sc_bsh,
	    CPSW_CPPI_RAM_TXDESCS_BASE, CPSW_CPPI_RAM_TXDESCS_SIZE,
	    &sc->sc_bsh_txdescs);
	if (error) {
		printf("can't subregion tx ring SRAM: %d\n", error);
		return;
	}
	descs = bus_space_vaddr(sc->sc_bst, sc->sc_bsh_txdescs);
	pmap_extract(pmap_kernel(), (vaddr_t)descs, &sc->sc_txdescs_pa);

	error = bus_space_subregion(sc->sc_bst, sc->sc_bsh,
	    CPSW_CPPI_RAM_RXDESCS_BASE, CPSW_CPPI_RAM_RXDESCS_SIZE,
	    &sc->sc_bsh_rxdescs);
	if (error) {
		printf("can't subregion rx ring SRAM: %d\n", error);
		return;
	}
	descs = bus_space_vaddr(sc->sc_bst, sc->sc_bsh_rxdescs);
	pmap_extract(pmap_kernel(), (vaddr_t)descs, &sc->sc_rxdescs_pa);

	sc->sc_rdp = malloc(sizeof(*sc->sc_rdp), M_TEMP, M_WAITOK);
	KASSERT(sc->sc_rdp != NULL);

	for (i = 0; i < CPSW_NTXDESCS; i++) {
		if ((error = bus_dmamap_create(sc->sc_bdt, MCLBYTES,
		    CPSW_TXFRAGS, MCLBYTES, 0, 0,
		    &sc->sc_rdp->tx_dm[i])) != 0) {
			printf("unable to create tx DMA map: %d\n", error);
		}
		sc->sc_rdp->tx_mb[i] = NULL;
	}

	for (i = 0; i < CPSW_NRXDESCS; i++) {
		if ((error = bus_dmamap_create(sc->sc_bdt, MCLBYTES, 1,
		    MCLBYTES, 0, 0, &sc->sc_rdp->rx_dm[i])) != 0) {
			printf("unable to create rx DMA map: %d\n", error);
		}
		sc->sc_rdp->rx_mb[i] = NULL;
	}

	sc->sc_txpad = dma_alloc(ETHER_MIN_LEN, PR_WAITOK | PR_ZERO);
	KASSERT(sc->sc_txpad != NULL);
	bus_dmamap_create(sc->sc_bdt, ETHER_MIN_LEN, 1, ETHER_MIN_LEN, 0,
	    BUS_DMA_WAITOK, &sc->sc_txpad_dm);
	bus_dmamap_load(sc->sc_bdt, sc->sc_txpad_dm, sc->sc_txpad,
	    ETHER_MIN_LEN, NULL, BUS_DMA_WAITOK|BUS_DMA_WRITE);
	bus_dmamap_sync(sc->sc_bdt, sc->sc_txpad_dm, 0, ETHER_MIN_LEN,
	    BUS_DMASYNC_PREWRITE);

	idver = bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_SS_IDVER);
	printf(": version %d.%d (%d), address %s\n",
	    CPSW_SS_IDVER_MAJ(idver), CPSW_SS_IDVER_MIN(idver),
	    CPSW_SS_IDVER_RTL(idver), ether_sprintf(ac->ac_enaddr));

	ifp->if_softc = sc;
	ifp->if_capabilities = IFCAP_VLAN_MTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_start = cpsw_start;
	ifp->if_ioctl = cpsw_ioctl;
	ifp->if_watchdog = cpsw_watchdog;
	ifq_init_maxlen(&ifp->if_snd, CPSW_NTXDESCS - 1);
	memcpy(ifp->if_xname, DEVNAME(sc), IFNAMSIZ);

	cpsw_stop(ifp);

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = cpsw_mii_readreg;
	sc->sc_mii.mii_writereg = cpsw_mii_writereg;
	sc->sc_mii.mii_statchg = cpsw_mii_statchg;

	cpsw_mdio_init(sc);

	ifmedia_init(&sc->sc_mii.mii_media, 0, cpsw_mediachange,
	    cpsw_mediastatus);
	mii_attach(self, &sc->sc_mii, 0xffffffff,
	    sc->sc_port_config[0].phy_id, MII_OFFSET_ANY, 0);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		printf("no PHY found!\n");
		ifmedia_add(&sc->sc_mii.mii_media,
		    IFM_ETHER|IFM_MANUAL, 0, NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_MANUAL);
	} else {
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);
	}

	if_attach(ifp);
	ether_ifattach(ifp);

	return;
}

int
cpsw_mediachange(struct ifnet *ifp)
{
	struct cpsw_softc *sc = ifp->if_softc;

	if (LIST_FIRST(&sc->sc_mii.mii_phys))
		mii_mediachg(&sc->sc_mii);

	return (0);
}

void
cpsw_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct cpsw_softc *sc = ifp->if_softc;

	if (LIST_FIRST(&sc->sc_mii.mii_phys)) {
		mii_pollstat(&sc->sc_mii);
		ifmr->ifm_active = sc->sc_mii.mii_media_active;
		ifmr->ifm_status = sc->sc_mii.mii_media_status;
	}
}

void
cpsw_start(struct ifnet *ifp)
{
	struct cpsw_softc * const sc = ifp->if_softc;
	struct cpsw_ring_data * const rdp = sc->sc_rdp;
	struct cpsw_cpdma_bd bd;
	struct mbuf *m;
	bus_dmamap_t dm;
	u_int eopi = ~0;
	u_int seg;
	u_int txfree;
	int txstart = -1;
	int error;
	bool pad;
	u_int mlen;

	if (!ISSET(ifp->if_flags, IFF_RUNNING) ||
	    ifq_is_oactive(&ifp->if_snd) ||
	    ifq_empty(&ifp->if_snd))
		return;

	if (sc->sc_txnext >= sc->sc_txhead)
		txfree = CPSW_NTXDESCS - 1 + sc->sc_txhead - sc->sc_txnext;
	else
		txfree = sc->sc_txhead - sc->sc_txnext - 1;

	for (;;) {
		if (txfree <= CPSW_TXFRAGS) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;

		dm = rdp->tx_dm[sc->sc_txnext];
		error = bus_dmamap_load_mbuf(sc->sc_bdt, dm, m, BUS_DMA_NOWAIT);
		switch (error) {
		case 0:
			break;

		case EFBIG: /* mbuf chain is too fragmented */
			if (m_defrag(m, M_DONTWAIT) == 0 &&
			    bus_dmamap_load_mbuf(sc->sc_bdt, dm, m,
			    BUS_DMA_NOWAIT) == 0)
				break;

			/* FALLTHROUGH */
		default:
			m_freem(m);
			ifp->if_oerrors++;
			continue;
		}

		mlen = dm->dm_mapsize;
		pad = mlen < CPSW_PAD_LEN;

		KASSERT(rdp->tx_mb[sc->sc_txnext] == NULL);
		rdp->tx_mb[sc->sc_txnext] = m;

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		bus_dmamap_sync(sc->sc_bdt, dm, 0, dm->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);

		if (txstart == -1)
			txstart = sc->sc_txnext;
		eopi = sc->sc_txnext;
		for (seg = 0; seg < dm->dm_nsegs; seg++) {
			bd.next = cpsw_txdesc_paddr(sc,
			    TXDESC_NEXT(sc->sc_txnext));
			bd.bufptr = dm->dm_segs[seg].ds_addr;
			bd.bufoff = 0;
			bd.buflen = dm->dm_segs[seg].ds_len;
			bd.pktlen = 0;
			bd.flags = 0;

			if (seg == 0) {
				bd.flags = CPDMA_BD_OWNER | CPDMA_BD_SOP;
				bd.pktlen = MAX(mlen, CPSW_PAD_LEN);
			}

			if (seg == dm->dm_nsegs - 1 && !pad)
				bd.flags |= CPDMA_BD_EOP;

			cpsw_set_txdesc(sc, sc->sc_txnext, &bd);
			txfree--;
			eopi = sc->sc_txnext;
			sc->sc_txnext = TXDESC_NEXT(sc->sc_txnext);
		}
		if (pad) {
			bd.next = cpsw_txdesc_paddr(sc,
			    TXDESC_NEXT(sc->sc_txnext));
			bd.bufptr = sc->sc_txpad_pa;
			bd.bufoff = 0;
			bd.buflen = CPSW_PAD_LEN - mlen;
			bd.pktlen = 0;
			bd.flags = CPDMA_BD_EOP;

			cpsw_set_txdesc(sc, sc->sc_txnext, &bd);
			txfree--;
			eopi = sc->sc_txnext;
			sc->sc_txnext = TXDESC_NEXT(sc->sc_txnext);
		}
	}

	if (txstart >= 0) {
		ifp->if_timer = 5;
		/* terminate the new chain */
		KASSERT(eopi == TXDESC_PREV(sc->sc_txnext));
		cpsw_set_txdesc_next(sc, TXDESC_PREV(sc->sc_txnext), 0);
		
		/* link the new chain on */
		cpsw_set_txdesc_next(sc, TXDESC_PREV(txstart),
		    cpsw_txdesc_paddr(sc, txstart));
		if (sc->sc_txeoq) {
			/* kick the dma engine */
			sc->sc_txeoq = false;
			bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_TX_HDP(0),
			    cpsw_txdesc_paddr(sc, txstart));
		}
	}
}

int
cpsw_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct cpsw_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s = splnet();
	int error = 0;

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				cpsw_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				cpsw_stop(ifp);
		}
		break;
	case SIOCSIFMEDIA:
		ifr->ifr_media &= ~IFM_ETH_FMASK;
		/* FALLTHROUGH */
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;
	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
		break;
	}
	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			cpsw_init(ifp);
		error = 0;
	}

	splx(s);

	return error;
}

void
cpsw_watchdog(struct ifnet *ifp)
{
	printf("%s: device timeout\n", ifp->if_xname);

	ifp->if_oerrors++;
	cpsw_init(ifp);
	cpsw_start(ifp);
}

static int
cpsw_mii_wait(struct cpsw_softc * const sc, int reg)
{
	u_int tries;

	for(tries = 0; tries < 1000; tries++) {
		if ((bus_space_read_4(sc->sc_bst, sc->sc_bsh, reg) & (1U << 31)) == 0)
			return 0;
		delay(1);
	}
	return ETIMEDOUT;
}

int
cpsw_mii_readreg(struct device *dev, int phy, int reg)
{
	struct cpsw_softc * const sc = (struct cpsw_softc *)dev;
	uint32_t v;

	if (cpsw_mii_wait(sc, MDIOUSERACCESS0) != 0)
		return 0;

	bus_space_write_4(sc->sc_bst, sc->sc_bsh, MDIOUSERACCESS0, (1U << 31) |
	    ((reg & 0x1F) << 21) | ((phy & 0x1F) << 16));

	if (cpsw_mii_wait(sc, MDIOUSERACCESS0) != 0)
		return 0;

	v = bus_space_read_4(sc->sc_bst, sc->sc_bsh, MDIOUSERACCESS0);
	if (v & (1 << 29))
		return v & 0xffff;
	else
		return 0;
}

void
cpsw_mii_writereg(struct device *dev, int phy, int reg, int val)
{
	struct cpsw_softc * const sc = (struct cpsw_softc *)dev;
	uint32_t v;

	KASSERT((val & 0xffff0000UL) == 0);

	if (cpsw_mii_wait(sc, MDIOUSERACCESS0) != 0)
		goto out;

	bus_space_write_4(sc->sc_bst, sc->sc_bsh, MDIOUSERACCESS0, (1U << 31) | (1 << 30) |
	    ((reg & 0x1F) << 21) | ((phy & 0x1F) << 16) | val);

	if (cpsw_mii_wait(sc, MDIOUSERACCESS0) != 0)
		goto out;

	v = bus_space_read_4(sc->sc_bst, sc->sc_bsh, MDIOUSERACCESS0);
	if ((v & (1 << 29)) == 0)
out:
		printf("%s error\n", __func__);

}

void
cpsw_mii_statchg(struct device *self)
{
	return;
}

int
cpsw_new_rxbuf(struct cpsw_softc * const sc, const u_int i)
{
	struct cpsw_ring_data * const rdp = sc->sc_rdp;
	const u_int h = RXDESC_PREV(i);
	struct cpsw_cpdma_bd bd;
	struct mbuf *m;
	int error = ENOBUFS;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL) {
		goto reuse;
	}

	MCLGET(m, M_DONTWAIT);
	if ((m->m_flags & M_EXT) == 0) {
		m_freem(m);
		goto reuse;
	}

	/* We have a new buffer, prepare it for the ring. */

	if (rdp->rx_mb[i] != NULL)
		bus_dmamap_unload(sc->sc_bdt, rdp->rx_dm[i]);

	m->m_len = m->m_pkthdr.len = MCLBYTES;

	rdp->rx_mb[i] = m;

	error = bus_dmamap_load_mbuf(sc->sc_bdt, rdp->rx_dm[i], rdp->rx_mb[i],
	    BUS_DMA_READ|BUS_DMA_NOWAIT);
	if (error) {
		printf("can't load rx DMA map %d: %d\n", i, error);
	}

	bus_dmamap_sync(sc->sc_bdt, rdp->rx_dm[i],
	    0, rdp->rx_dm[i]->dm_mapsize, BUS_DMASYNC_PREREAD);

	error = 0;

reuse:
	/* (re-)setup the descriptor */
	bd.next = 0;
	bd.bufptr = rdp->rx_dm[i]->dm_segs[0].ds_addr;
	bd.bufoff = 0;
	bd.buflen = MIN(0x7ff, rdp->rx_dm[i]->dm_segs[0].ds_len);
	bd.pktlen = 0;
	bd.flags = CPDMA_BD_OWNER;

	cpsw_set_rxdesc(sc, i, &bd);
	/* and link onto ring */
	cpsw_set_rxdesc_next(sc, h, cpsw_rxdesc_paddr(sc, i));

	return error;
}

int
cpsw_init(struct ifnet *ifp)
{
	struct cpsw_softc * const sc = ifp->if_softc;
	struct arpcom *ac = &sc->sc_ac;
	struct mii_data * const mii = &sc->sc_mii;
	int i;

	cpsw_stop(ifp);

	sc->sc_txnext = 0;
	sc->sc_txhead = 0;

	/* Reset wrapper */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_WR_SOFT_RESET, 1);
	while(bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_WR_SOFT_RESET) & 1);

	/* Reset SS */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_SS_SOFT_RESET, 1);
	while(bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_SS_SOFT_RESET) & 1);

	/* Clear table (30) and enable ALE(31) and set passthrough (4) */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_ALE_CONTROL, (3 << 30) | 0x10);

	/* Reset and init Sliver port 1 and 2 */
	for (i = 0; i < 2; i++) {
		/* Reset */
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_SL_SOFT_RESET(i), 1);
		while(bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_SL_SOFT_RESET(i)) & 1);
		/* Set Slave Mapping */
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_SL_RX_PRI_MAP(i), 0x76543210);
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_PORT_P_TX_PRI_MAP(i+1), 0x33221100);
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_SL_RX_MAXLEN(i), 0x5f2);
		/* Set MAC Address */
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_PORT_P_SA_HI(i+1),
		    ac->ac_enaddr[0] | (ac->ac_enaddr[1] << 8) |
		    (ac->ac_enaddr[2] << 16) | (ac->ac_enaddr[3] << 24));
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_PORT_P_SA_LO(i+1),
		    ac->ac_enaddr[4] | (ac->ac_enaddr[5] << 8));

		/* Set MACCONTROL for ports 0,1: FULLDUPLEX(0), GMII_EN(5),
		   IFCTL_A(15), IFCTL_B(16) FIXME */
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_SL_MACCONTROL(i),
		    1 | (1<<5) | (1<<15) | (1<<16));

		/* Set ALE port to forwarding(3) on the active port */
		if (i == sc->sc_active_port)
			bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_ALE_PORTCTL(i+1), 3);
	}

	/* Set Host Port Mapping */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_PORT_P0_CPDMA_TX_PRI_MAP, 0x76543210);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_PORT_P0_CPDMA_RX_CH_MAP, 0);

	/* Set ALE port to forwarding(3) */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_ALE_PORTCTL(0), 3);

	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_SS_PTYPE, 0);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_SS_STAT_PORT_EN, 7);

	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_SOFT_RESET, 1);
	while(bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_SOFT_RESET) & 1);

	for (i = 0; i < 8; i++) {
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_TX_HDP(i), 0);
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_RX_HDP(i), 0);
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_TX_CP(i), 0);
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_RX_CP(i), 0);
	}

	bus_space_set_region_4(sc->sc_bst, sc->sc_bsh_txdescs, 0, 0,
	    CPSW_CPPI_RAM_TXDESCS_SIZE/4);

	sc->sc_txhead = 0;
	sc->sc_txnext = 0;

	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_RX_FREEBUFFER(0), 0);

	bus_space_set_region_4(sc->sc_bst, sc->sc_bsh_rxdescs, 0, 0,
	    CPSW_CPPI_RAM_RXDESCS_SIZE/4);

	/* Initialize RX Buffer Descriptors */
	cpsw_set_rxdesc_next(sc, RXDESC_PREV(0), 0);
	for (i = 0; i < CPSW_NRXDESCS; i++) {
		cpsw_new_rxbuf(sc, i);
	}
	sc->sc_rxhead = 0;

	/* align layer 3 header to 32-bit */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_RX_BUFFER_OFFSET, ETHER_ALIGN);

	/* Clear all interrupt Masks */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_RX_INTMASK_CLEAR, 0xFFFFFFFF);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_TX_INTMASK_CLEAR, 0xFFFFFFFF);

	/* Enable TX & RX DMA */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_TX_CONTROL, 1);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_RX_CONTROL, 1);

	/* Enable interrupt pacing for C0 RX/TX (IMAX set to max intr/ms allowed) */
#define CPSW_VBUSP_CLK_MHZ	2400	/* hardcoded for BBB */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_WR_C_RX_IMAX(0), 2);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_WR_C_TX_IMAX(0), 2);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_WR_INT_CONTROL, 3 << 16 | CPSW_VBUSP_CLK_MHZ/4);

	/* Enable TX and RX interrupt receive for core 0 */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_WR_C_TX_EN(0), 1);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_WR_C_RX_EN(0), 1);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_WR_C_MISC_EN(0), 0x1F);

	/* Enable host Error Interrupt */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_DMA_INTMASK_SET, 2);

	/* Enable interrupts for TX and RX Channel 0 */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_TX_INTMASK_SET, 1);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_RX_INTMASK_SET, 1);

	/* Ack stalled irqs */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_CPDMA_EOI_VECTOR, CPSW_INTROFF_RXTH);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_CPDMA_EOI_VECTOR, CPSW_INTROFF_RX);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_CPDMA_EOI_VECTOR, CPSW_INTROFF_TX);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_CPDMA_EOI_VECTOR, CPSW_INTROFF_MISC);

	cpsw_mdio_init(sc);

	mii_mediachg(mii);

	/* Write channel 0 RX HDP */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_RX_HDP(0), cpsw_rxdesc_paddr(sc, 0));
	sc->sc_rxrun = true;
	sc->sc_rxeoq = false;

	sc->sc_txrun = true;
	sc->sc_txeoq = true;

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	timeout_add_sec(&sc->sc_tick, 1);

	return 0;
}

void
cpsw_stop(struct ifnet *ifp)
{
	struct cpsw_softc * const sc = ifp->if_softc;
	struct cpsw_ring_data * const rdp = sc->sc_rdp;
	u_int i;

#if 0
	/* XXX find where disable comes from */
	printf("%s: ifp %p disable %d\n", __func__, ifp, disable);
#endif
	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	timeout_del(&sc->sc_tick);

	mii_down(&sc->sc_mii);

	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_TX_INTMASK_CLEAR, 1);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_RX_INTMASK_CLEAR, 1);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_WR_C_TX_EN(0), 0x0);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_WR_C_RX_EN(0), 0x0);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_WR_C_MISC_EN(0), 0x1F);

	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_TX_TEARDOWN, 0);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_RX_TEARDOWN, 0);
	i = 0;
	while ((sc->sc_txrun || sc->sc_rxrun) && i < 10000) {
		delay(10);
		if ((sc->sc_txrun == true) && cpsw_txintr(sc) == 0)
			sc->sc_txrun = false;
		if ((sc->sc_rxrun == true) && cpsw_rxintr(sc) == 0)
			sc->sc_rxrun = false;
		i++;
	}
	/* printf("%s toredown complete in %u\n", __func__, i); */

	/* Reset wrapper */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_WR_SOFT_RESET, 1);
	while(bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_WR_SOFT_RESET) & 1);

	/* Reset SS */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_SS_SOFT_RESET, 1);
	while(bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_SS_SOFT_RESET) & 1);

	for (i = 0; i < 2; i++) {
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_SL_SOFT_RESET(i), 1);
		while(bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_SL_SOFT_RESET(i)) & 1);
	}

	/* Reset CPDMA */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_SOFT_RESET, 1);
	while(bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_SOFT_RESET) & 1);

	/* Release any queued transmit buffers. */
	for (i = 0; i < CPSW_NTXDESCS; i++) {
		bus_dmamap_unload(sc->sc_bdt, rdp->tx_dm[i]);
		m_freem(rdp->tx_mb[i]);
		rdp->tx_mb[i] = NULL;
	}

	ifp->if_flags &= ~IFF_RUNNING;
	ifp->if_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);

	/* XXX Not sure what this is doing calling disable here
	    where is disable set?
	*/
#if 0
	if (!disable)
		return;
#endif

	for (i = 0; i < CPSW_NRXDESCS; i++) {
		bus_dmamap_unload(sc->sc_bdt, rdp->rx_dm[i]);
		m_freem(rdp->rx_mb[i]);
		rdp->rx_mb[i] = NULL;
	}
}

int
cpsw_rxthintr(void *arg)
{
	struct cpsw_softc * const sc = arg;

	/* this won't deassert the interrupt though */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_CPDMA_EOI_VECTOR, CPSW_INTROFF_RXTH);

	return 1;
}

int
cpsw_rxintr(void *arg)
{
	struct cpsw_softc * const sc = arg;
	struct ifnet * const ifp = &sc->sc_ac.ac_if;
	struct cpsw_ring_data * const rdp = sc->sc_rdp;
	struct cpsw_cpdma_bd bd;
	bus_dmamap_t dm;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	u_int i;
	u_int len, off;

	sc->sc_rxeoq = false;
	
	for (;;) {
		KASSERT(sc->sc_rxhead < CPSW_NRXDESCS);

		i = sc->sc_rxhead;
		dm = rdp->rx_dm[i];
		m = rdp->rx_mb[i];

		KASSERT(dm != NULL);
		KASSERT(m != NULL);

		cpsw_get_rxdesc(sc, i, &bd);

		if (bd.flags & CPDMA_BD_OWNER)
			break;

		if (bd.flags & CPDMA_BD_TDOWNCMPLT) {
			sc->sc_rxrun = false;
			goto done;
		}

		bus_dmamap_sync(sc->sc_bdt, dm, 0, dm->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);

		if (cpsw_new_rxbuf(sc, i) != 0) {
			/* drop current packet, reuse buffer for new */
			ifp->if_ierrors++;
			goto next;
		}

		if ((bd.flags & (CPDMA_BD_SOP|CPDMA_BD_EOP)) !=
		    (CPDMA_BD_SOP|CPDMA_BD_EOP)) {
			if (bd.flags & CPDMA_BD_SOP) {
				printf("cpsw: rx packet too large\n");
				ifp->if_ierrors++;
			}
			m_freem(m);
			goto next;
		}

		off = bd.bufoff;
		len = bd.pktlen;

		if (bd.flags & CPDMA_BD_PASSCRC)
			len -= ETHER_CRC_LEN;

		m->m_pkthdr.len = m->m_len = len;
		m->m_data += off;

		ml_enqueue(&ml, m);

next:
		sc->sc_rxhead = RXDESC_NEXT(sc->sc_rxhead);
		if (bd.flags & CPDMA_BD_EOQ) {
			sc->sc_rxeoq = true;
			sc->sc_rxrun = false;
		}
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_RX_CP(0),
		    cpsw_rxdesc_paddr(sc, i));
	}

	if (sc->sc_rxeoq) {
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_RX_HDP(0),
				  cpsw_rxdesc_paddr(sc, sc->sc_rxhead));
		sc->sc_rxrun = true;
		sc->sc_rxeoq = false;
	}

	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_CPDMA_EOI_VECTOR,
	    CPSW_INTROFF_RX);

done:
	if_input(ifp, &ml);

	return 1;
}

void
cpsw_tick(void *arg)
{
	struct cpsw_softc *sc = arg;
	int s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	splx(s);

	timeout_add_sec(&sc->sc_tick, 1);
}

int
cpsw_txintr(void *arg)
{
	struct cpsw_softc * const sc = arg;
	struct ifnet * const ifp = &sc->sc_ac.ac_if;
	struct cpsw_ring_data * const rdp = sc->sc_rdp;
	struct cpsw_cpdma_bd bd;
	bool handled = false;
	uint32_t tx0_cp;
	u_int cpi;

	KASSERT(sc->sc_txrun);

	tx0_cp = bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_TX_CP(0));

	if (tx0_cp == 0xfffffffc) {
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_TX_CP(0), 0xfffffffc);
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_TX_HDP(0), 0);
		sc->sc_txrun = false;
		return 0;
	}

	for (;;) {
		tx0_cp = bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_TX_CP(0));
		cpi = (tx0_cp - sc->sc_txdescs_pa) /
		    sizeof(struct cpsw_cpdma_bd);
		KASSERT(sc->sc_txhead < CPSW_NTXDESCS);

		cpsw_get_txdesc(sc, sc->sc_txhead, &bd);

		if (bd.buflen == 0) {
			/* db_enter(); */
		}

		if ((bd.flags & CPDMA_BD_SOP) == 0)
			goto next;

		if (bd.flags & CPDMA_BD_OWNER) {
			printf("pwned %x %x %x\n", cpi, sc->sc_txhead,
			    sc->sc_txnext);
			break;
		}

		if (bd.flags & CPDMA_BD_TDOWNCMPLT) {
			sc->sc_txrun = false;
			return 1;
		}

		bus_dmamap_sync(sc->sc_bdt, rdp->tx_dm[sc->sc_txhead],
		    0, rdp->tx_dm[sc->sc_txhead]->dm_mapsize,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_bdt, rdp->tx_dm[sc->sc_txhead]);

		m_freem(rdp->tx_mb[sc->sc_txhead]);
		rdp->tx_mb[sc->sc_txhead] = NULL;

		handled = true;

		ifq_clr_oactive(&ifp->if_snd);

next:
		if ((bd.flags & (CPDMA_BD_EOP|CPDMA_BD_EOQ)) ==
		    (CPDMA_BD_EOP|CPDMA_BD_EOQ))
			sc->sc_txeoq = true;

		if (sc->sc_txhead == cpi) {
			bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_TX_CP(0),
			    cpsw_txdesc_paddr(sc, cpi));
			sc->sc_txhead = TXDESC_NEXT(sc->sc_txhead);
			break;
		}
		sc->sc_txhead = TXDESC_NEXT(sc->sc_txhead);
		if (sc->sc_txeoq == true)
			break;
	}

	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_CPDMA_EOI_VECTOR, CPSW_INTROFF_TX);

	if ((sc->sc_txnext != sc->sc_txhead) && sc->sc_txeoq) {
		if (bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_TX_HDP(0)) == 0) {
			sc->sc_txeoq = false;
			bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_TX_HDP(0),
			    cpsw_txdesc_paddr(sc, sc->sc_txhead));
		}
	}

	if (handled && sc->sc_txnext == sc->sc_txhead)
		ifp->if_timer = 0;

	if (handled)
		cpsw_start(ifp);

	return handled;
}

int
cpsw_miscintr(void *arg)
{
	struct cpsw_softc * const sc = arg;
	uint32_t miscstat;
	uint32_t dmastat;
	uint32_t stat;

	miscstat = bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_WR_C_MISC_STAT(0));
	printf("%s %x FIRE\n", __func__, miscstat);

	if (miscstat & CPSW_MISC_HOST_PEND) {
		/* Host Error */
		dmastat = bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_DMA_INTSTAT_MASKED);
		printf("CPSW_CPDMA_DMA_INTSTAT_MASKED %x\n", dmastat);

		printf("rxhead %02x\n", sc->sc_rxhead);

		stat = bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_DMASTATUS);
		printf("CPSW_CPDMA_DMASTATUS %x\n", stat);
		stat = bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_TX_HDP(0));
		printf("CPSW_CPDMA_TX0_HDP %x\n", stat);
		stat = bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_TX_CP(0));
		printf("CPSW_CPDMA_TX0_CP %x\n", stat);
		stat = bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_RX_HDP(0));
		printf("CPSW_CPDMA_RX0_HDP %x\n", stat);
		stat = bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_RX_CP(0));
		printf("CPSW_CPDMA_RX0_CP %x\n", stat);

		/* db_enter(); */

		bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_DMA_INTMASK_CLEAR, dmastat);
		dmastat = bus_space_read_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_DMA_INTSTAT_MASKED);
		printf("CPSW_CPDMA_DMA_INTSTAT_MASKED %x\n", dmastat);
	}

	bus_space_write_4(sc->sc_bst, sc->sc_bsh, CPSW_CPDMA_CPDMA_EOI_VECTOR, CPSW_INTROFF_MISC);

	return 1;
}

void
cpsw_get_port_config(struct cpsw_port_config *conf, int pnode)
{
	char mode[32];
	uint32_t phy_id[2];
	int node, phy_handle, phy_node;
	int port = 0;

	for (node = OF_child(pnode); node; node = OF_peer(node)) {
		if (OF_getprop(node, "local-mac-address", conf[port].enaddr,
		    sizeof(conf[port].enaddr)) != sizeof(conf[port].enaddr))
			continue;

		conf[port].vlan = OF_getpropint(node, "dual_emac_res_vlan", 0);

		if (OF_getpropintarray(node, "phy_id", phy_id,
		    sizeof(phy_id)) == sizeof(phy_id))
			conf[port].phy_id = phy_id[1];
		else if ((phy_handle =
		    OF_getpropint(node, "phy-handle", 0)) != 0) {
			phy_node = OF_getnodebyphandle(phy_handle);
			if (phy_node)
				conf[port].phy_id = OF_getpropint(phy_node,
				    "reg", MII_PHY_ANY);
		}

		if (OF_getprop(node, "phy-mode", mode, sizeof(mode)) > 0 &&
		    !strcmp(mode, "rgmii"))
			conf[port].rgmii = 1;
		else
			conf[port].rgmii = 0;

		if (port == 0)
			port = 1;
	}
}
