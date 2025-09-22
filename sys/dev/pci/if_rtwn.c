/*	$OpenBSD: if_rtwn.c,v 1.42 2024/05/24 06:02:56 jsg Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2015 Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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
 * PCI front-end for Realtek RTL8188CE/RTL8188EE/RTL8192CE/RTL8723AE driver.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/device.h>
#include <sys/endian.h>

#include <machine/bus.h>
#include <machine/intr.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif
#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_amrr.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ic/r92creg.h>
#include <dev/ic/rtwnvar.h>

/*
 * Driver definitions.
 */

#define R92C_NPQ_NPAGES		0
#define R92C_PUBQ_NPAGES	176
#define R92C_HPQ_NPAGES		41
#define R92C_LPQ_NPAGES		28
#define R92C_TXPKTBUF_COUNT	256
#define R92C_TX_PAGE_COUNT	\
	(R92C_PUBQ_NPAGES + R92C_HPQ_NPAGES + R92C_LPQ_NPAGES)
#define R92C_TX_PAGE_BOUNDARY	(R92C_TX_PAGE_COUNT + 1)
#define R92C_MAX_RX_DMA_SIZE	0x2800

#define R88E_NPQ_NPAGES		0
#define R88E_PUBQ_NPAGES	116
#define R88E_HPQ_NPAGES		41
#define R88E_LPQ_NPAGES		13
#define R88E_TXPKTBUF_COUNT	176
#define R88E_TX_PAGE_COUNT	\
	(R88E_PUBQ_NPAGES + R88E_HPQ_NPAGES + R88E_LPQ_NPAGES)
#define R88E_TX_PAGE_BOUNDARY	(R88E_TX_PAGE_COUNT + 1)
#define R88E_MAX_RX_DMA_SIZE	0x2600

#define R23A_NPQ_NPAGES		0
#define R23A_PUBQ_NPAGES	189
#define R23A_HPQ_NPAGES		28
#define R23A_LPQ_NPAGES		28
#define R23A_TXPKTBUF_COUNT	256
#define R23A_TX_PAGE_COUNT	\
	(R23A_PUBQ_NPAGES + R23A_HPQ_NPAGES + R23A_LPQ_NPAGES)
#define R23A_TX_PAGE_BOUNDARY	(R23A_TX_PAGE_COUNT + 1)
#define R23A_MAX_RX_DMA_SIZE	0x2800

#define RTWN_NTXQUEUES			9
#define RTWN_RX_LIST_COUNT		256
#define RTWN_TX_LIST_COUNT		256

/* TX queue indices. */
#define RTWN_BK_QUEUE			0
#define RTWN_BE_QUEUE			1
#define RTWN_VI_QUEUE			2
#define RTWN_VO_QUEUE			3
#define RTWN_BEACON_QUEUE		4
#define RTWN_TXCMD_QUEUE		5
#define RTWN_MGNT_QUEUE			6
#define RTWN_HIGH_QUEUE			7
#define RTWN_HCCA_QUEUE			8

struct rtwn_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	uint8_t		wr_dbm_antsignal;
} __packed;

#define RTWN_RX_RADIOTAP_PRESENT			\
	(1 << IEEE80211_RADIOTAP_FLAGS |		\
	 1 << IEEE80211_RADIOTAP_RATE |			\
	 1 << IEEE80211_RADIOTAP_CHANNEL |		\
	 1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL)

struct rtwn_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define RTWN_TX_RADIOTAP_PRESENT			\
	(1 << IEEE80211_RADIOTAP_FLAGS |		\
	 1 << IEEE80211_RADIOTAP_CHANNEL)

struct rtwn_rx_data {
	bus_dmamap_t		map;
	struct mbuf		*m;
};

struct rtwn_rx_ring {
	struct r92c_rx_desc_pci	*desc;
	bus_dmamap_t		map;
	bus_dma_segment_t	seg;
	int			nsegs;
	struct rtwn_rx_data	rx_data[RTWN_RX_LIST_COUNT];

};
struct rtwn_tx_data {
	bus_dmamap_t			map;
	struct mbuf			*m;
	struct ieee80211_node		*ni;
};

struct rtwn_tx_ring {
	bus_dmamap_t		map;
	bus_dma_segment_t	seg;
	int			nsegs;
	struct r92c_tx_desc_pci	*desc;
	struct rtwn_tx_data	tx_data[RTWN_TX_LIST_COUNT];
	int			queued;
	int			cur;
};

struct rtwn_pci_softc {
	struct device		sc_dev;
	struct rtwn_softc	sc_sc;

	struct rtwn_rx_ring	rx_ring;
	struct rtwn_tx_ring	tx_ring[RTWN_NTXQUEUES];
	uint32_t		qfullmsk;

	struct timeout		calib_to;
	struct timeout		scan_to;

	/* PCI specific goo. */
	bus_dma_tag_t 		sc_dmat;
	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_tag;
	void			*sc_ih;
	bus_space_tag_t		sc_st;
	bus_space_handle_t	sc_sh;
	bus_size_t		sc_mapsize;
	int			sc_cap_off;

	struct ieee80211_amrr		amrr;
	struct ieee80211_amrr_node	amn;

#if NBPFILTER > 0
	caddr_t				sc_drvbpf;

	union {
		struct rtwn_rx_radiotap_header th;
		uint8_t	pad[64];
	}				sc_rxtapu;
#define sc_rxtap	sc_rxtapu.th
	int				sc_rxtap_len;

	union {
		struct rtwn_tx_radiotap_header th;
		uint8_t	pad[64];
	}				sc_txtapu;
#define sc_txtap	sc_txtapu.th
	int				sc_txtap_len;
#endif
};

#ifdef RTWN_DEBUG
#define DPRINTF(x)	do { if (rtwn_debug) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (rtwn_debug >= (n)) printf x; } while (0)
extern int rtwn_debug;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

/*
 * PCI configuration space registers.
 */
#define	RTWN_PCI_IOBA		0x10	/* i/o mapped base */
#define	RTWN_PCI_MMBA		0x18	/* memory mapped base */

static const struct pci_matchid rtwn_pci_devices[] = {
	{ PCI_VENDOR_REALTEK,	PCI_PRODUCT_REALTEK_RTL8188CE },
	{ PCI_VENDOR_REALTEK,	PCI_PRODUCT_REALTEK_RTL8188EE },
	{ PCI_VENDOR_REALTEK,	PCI_PRODUCT_REALTEK_RTL8192CE },
	{ PCI_VENDOR_REALTEK,	PCI_PRODUCT_REALTEK_RTL8723AE }
};

int		rtwn_pci_match(struct device *, void *, void *);
void		rtwn_pci_attach(struct device *, struct device *, void *);
int		rtwn_pci_detach(struct device *, int);
int		rtwn_pci_activate(struct device *, int);
int		rtwn_alloc_rx_list(struct rtwn_pci_softc *);
void		rtwn_reset_rx_list(struct rtwn_pci_softc *);
void		rtwn_free_rx_list(struct rtwn_pci_softc *);
void		rtwn_setup_rx_desc(struct rtwn_pci_softc *,
		    struct r92c_rx_desc_pci *, bus_addr_t, size_t, int);
int		rtwn_alloc_tx_list(struct rtwn_pci_softc *, int);
void		rtwn_reset_tx_list(struct rtwn_pci_softc *, int);
void		rtwn_free_tx_list(struct rtwn_pci_softc *, int);
void		rtwn_pci_write_1(void *, uint16_t, uint8_t);
void		rtwn_pci_write_2(void *, uint16_t, uint16_t);
void		rtwn_pci_write_4(void *, uint16_t, uint32_t);
uint8_t		rtwn_pci_read_1(void *, uint16_t);
uint16_t	rtwn_pci_read_2(void *, uint16_t);
uint32_t	rtwn_pci_read_4(void *, uint16_t);
void		rtwn_rx_frame(struct rtwn_pci_softc *,
		    struct r92c_rx_desc_pci *, struct rtwn_rx_data *, int,
		    struct mbuf_list *);
int		rtwn_tx(void *, struct mbuf *, struct ieee80211_node *);
void		rtwn_tx_done(struct rtwn_pci_softc *, int);
int		rtwn_alloc_buffers(void *);
int		rtwn_pci_init(void *);
void		rtwn_pci_88e_stop(struct rtwn_pci_softc *);
void		rtwn_pci_stop(void *);
int		rtwn_intr(void *);
int		rtwn_is_oactive(void *);
int		rtwn_92c_power_on(struct rtwn_pci_softc *);
int		rtwn_88e_power_on(struct rtwn_pci_softc *);
int		rtwn_23a_power_on(struct rtwn_pci_softc *);
int		rtwn_power_on(void *);
int		rtwn_llt_write(struct rtwn_pci_softc *, uint32_t, uint32_t);
int		rtwn_llt_init(struct rtwn_pci_softc *, int);
int		rtwn_dma_init(void *);
int		rtwn_fw_loadpage(void *, int, uint8_t *, int);
int		rtwn_pci_load_firmware(void *, u_char **, size_t *);
void		rtwn_mac_init(void *);
void		rtwn_bb_init(void *);
void		rtwn_calib_to(void *);
void		rtwn_next_calib(void *);
void		rtwn_cancel_calib(void *);
void		rtwn_scan_to(void *);
void		rtwn_pci_next_scan(void *);
void		rtwn_cancel_scan(void *);
void		rtwn_wait_async(void *);
void		rtwn_poll_c2h_events(struct rtwn_pci_softc *);
void		rtwn_tx_report(struct rtwn_pci_softc *, uint8_t *, int);

/* Aliases. */
#define	rtwn_bb_write	rtwn_pci_write_4
#define rtwn_bb_read	rtwn_pci_read_4

struct cfdriver rtwn_cd = {
	NULL, "rtwn", DV_IFNET
};

const struct cfattach rtwn_pci_ca = {
	sizeof(struct rtwn_pci_softc),
	rtwn_pci_match,
	rtwn_pci_attach,
	rtwn_pci_detach,
	rtwn_pci_activate
};

int
rtwn_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, rtwn_pci_devices,
	    nitems(rtwn_pci_devices)));
}

void
rtwn_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct rtwn_pci_softc *sc = (struct rtwn_pci_softc*)self;
	struct pci_attach_args *pa = aux;
	struct ifnet *ifp;
	int i, error;
	pcireg_t memtype;
	pci_intr_handle_t ih;
	const char *intrstr;

	sc->sc_dmat = pa->pa_dmat;
	sc->sc_pc = pa->pa_pc;
	sc->sc_tag = pa->pa_tag;

	timeout_set(&sc->calib_to, rtwn_calib_to, sc);
	timeout_set(&sc->scan_to, rtwn_scan_to, sc);

	pci_set_powerstate(pa->pa_pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	/* Map control/status registers. */
	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, RTWN_PCI_MMBA);
	error = pci_mapreg_map(pa, RTWN_PCI_MMBA, memtype, 0, &sc->sc_st,
	    &sc->sc_sh, NULL, &sc->sc_mapsize, 0);
	if (error != 0) {
		printf(": can't map mem space\n");
		return;
	}

	if (pci_intr_map_msi(pa, &ih) && pci_intr_map(pa, &ih)) {
		printf(": can't map interrupt\n");
		return;
	}
	intrstr = pci_intr_string(sc->sc_pc, ih);
	sc->sc_ih = pci_intr_establish(sc->sc_pc, ih, IPL_NET,
	    rtwn_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s\n", intrstr);

	/* Disable PCIe Active State Power Management (ASPM). */
	if (pci_get_capability(sc->sc_pc, sc->sc_tag, PCI_CAP_PCIEXPRESS,
	    &sc->sc_cap_off, NULL)) {
		uint32_t lcsr = pci_conf_read(sc->sc_pc, sc->sc_tag,
		    sc->sc_cap_off + PCI_PCIE_LCSR);
		lcsr &= ~(PCI_PCIE_LCSR_ASPM_L0S | PCI_PCIE_LCSR_ASPM_L1);
		pci_conf_write(sc->sc_pc, sc->sc_tag,
		    sc->sc_cap_off + PCI_PCIE_LCSR, lcsr);
	}

	/* Allocate Tx/Rx buffers. */
	error = rtwn_alloc_rx_list(sc);
	if (error != 0) {
		printf("%s: could not allocate Rx buffers\n",
		    sc->sc_dev.dv_xname);
		return;
	}
	for (i = 0; i < RTWN_NTXQUEUES; i++) {
		error = rtwn_alloc_tx_list(sc, i);
		if (error != 0) {
			printf("%s: could not allocate Tx buffers\n",
			    sc->sc_dev.dv_xname);
			rtwn_free_rx_list(sc);
			return;
		}
	}

	sc->amrr.amrr_min_success_threshold = 1;
	sc->amrr.amrr_max_success_threshold = 15;

	/* Attach the bus-agnostic driver. */
	sc->sc_sc.sc_ops.cookie = sc;
	sc->sc_sc.sc_ops.write_1 = rtwn_pci_write_1;
	sc->sc_sc.sc_ops.write_2 = rtwn_pci_write_2;
	sc->sc_sc.sc_ops.write_4 = rtwn_pci_write_4;
	sc->sc_sc.sc_ops.read_1 = rtwn_pci_read_1;
	sc->sc_sc.sc_ops.read_2 = rtwn_pci_read_2;
	sc->sc_sc.sc_ops.read_4 = rtwn_pci_read_4;
	sc->sc_sc.sc_ops.tx = rtwn_tx;
	sc->sc_sc.sc_ops.power_on = rtwn_power_on;
	sc->sc_sc.sc_ops.dma_init = rtwn_dma_init;
	sc->sc_sc.sc_ops.load_firmware = rtwn_pci_load_firmware;
	sc->sc_sc.sc_ops.fw_loadpage = rtwn_fw_loadpage;
	sc->sc_sc.sc_ops.mac_init = rtwn_mac_init;
	sc->sc_sc.sc_ops.bb_init = rtwn_bb_init;
	sc->sc_sc.sc_ops.alloc_buffers = rtwn_alloc_buffers;
	sc->sc_sc.sc_ops.init = rtwn_pci_init;
	sc->sc_sc.sc_ops.stop = rtwn_pci_stop;
	sc->sc_sc.sc_ops.is_oactive = rtwn_is_oactive;
	sc->sc_sc.sc_ops.next_calib = rtwn_next_calib;
	sc->sc_sc.sc_ops.cancel_calib = rtwn_cancel_calib;
	sc->sc_sc.sc_ops.next_scan = rtwn_pci_next_scan;
	sc->sc_sc.sc_ops.cancel_scan = rtwn_cancel_scan;
	sc->sc_sc.sc_ops.wait_async = rtwn_wait_async;

	sc->sc_sc.chip = RTWN_CHIP_PCI;
	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_REALTEK_RTL8188CE:
	case PCI_PRODUCT_REALTEK_RTL8192CE:
		sc->sc_sc.chip |= RTWN_CHIP_88C | RTWN_CHIP_92C;
		break;
	case PCI_PRODUCT_REALTEK_RTL8188EE:
		sc->sc_sc.chip |= RTWN_CHIP_88E;
		break;
	case PCI_PRODUCT_REALTEK_RTL8723AE:
		sc->sc_sc.chip |= RTWN_CHIP_23A;
		break;
	}

	error = rtwn_attach(&sc->sc_dev, &sc->sc_sc);
	if (error != 0) {
		rtwn_free_rx_list(sc);
		for (i = 0; i < RTWN_NTXQUEUES; i++)
			rtwn_free_tx_list(sc, i);
		return;
	}

	/* ifp is now valid */
	ifp = &sc->sc_sc.sc_ic.ic_if;
#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof(struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN);

	sc->sc_rxtap_len = sizeof(sc->sc_rxtapu);
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(RTWN_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof(sc->sc_txtapu);
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(RTWN_TX_RADIOTAP_PRESENT);
#endif
}

int
rtwn_pci_detach(struct device *self, int flags)
{
	struct rtwn_pci_softc *sc = (struct rtwn_pci_softc *)self;
	int s, i;

	s = splnet();

	if (timeout_initialized(&sc->calib_to))
		timeout_del(&sc->calib_to);
	if (timeout_initialized(&sc->scan_to))
		timeout_del(&sc->scan_to);

	rtwn_detach(&sc->sc_sc, flags);

	/* Free Tx/Rx buffers. */
	for (i = 0; i < RTWN_NTXQUEUES; i++)
		rtwn_free_tx_list(sc, i);
	rtwn_free_rx_list(sc);
	splx(s);

	return (0);
}

int
rtwn_pci_activate(struct device *self, int act)
{
	struct rtwn_pci_softc *sc = (struct rtwn_pci_softc *)self;

	return rtwn_activate(&sc->sc_sc, act);
}

void
rtwn_setup_rx_desc(struct rtwn_pci_softc *sc, struct r92c_rx_desc_pci *desc,
    bus_addr_t addr, size_t len, int idx)
{
	memset(desc, 0, sizeof(*desc));
	desc->rxdw0 = htole32(SM(R92C_RXDW0_PKTLEN, len) |
		((idx == RTWN_RX_LIST_COUNT - 1) ? R92C_RXDW0_EOR : 0));
	desc->rxbufaddr = htole32(addr);
	bus_space_barrier(sc->sc_st, sc->sc_sh, 0, sc->sc_mapsize,
	    BUS_SPACE_BARRIER_WRITE);
	desc->rxdw0 |= htole32(R92C_RXDW0_OWN);
}

int
rtwn_alloc_rx_list(struct rtwn_pci_softc *sc)
{
	struct rtwn_rx_ring *rx_ring = &sc->rx_ring;
	struct rtwn_rx_data *rx_data;
	size_t size;
	int i, error = 0;

	/* Allocate Rx descriptors. */
	size = sizeof(struct r92c_rx_desc_pci) * RTWN_RX_LIST_COUNT;
	error = bus_dmamap_create(sc->sc_dmat, size, 1, size, 0, BUS_DMA_NOWAIT,
		&rx_ring->map);
	if (error != 0) {
		printf("%s: could not create rx desc DMA map\n",
		    sc->sc_dev.dv_xname);
		rx_ring->map = NULL;
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, size, 0, 0, &rx_ring->seg, 1,
	    &rx_ring->nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		printf("%s: could not allocate rx desc\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &rx_ring->seg, rx_ring->nsegs,
	    size, (caddr_t *)&rx_ring->desc, 
	    BUS_DMA_NOWAIT | BUS_DMA_COHERENT);
	if (error != 0) {
		bus_dmamem_free(sc->sc_dmat, &rx_ring->seg, rx_ring->nsegs);
		rx_ring->desc = NULL;
		printf("%s: could not map rx desc\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load_raw(sc->sc_dmat, rx_ring->map, &rx_ring->seg,
	    1, size, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load rx desc\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	bus_dmamap_sync(sc->sc_dmat, rx_ring->map, 0, size,
	    BUS_DMASYNC_PREWRITE);

	/* Allocate Rx buffers. */
	for (i = 0; i < RTWN_RX_LIST_COUNT; i++) {
		rx_data = &rx_ring->rx_data[i];

		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES,
		    0, BUS_DMA_NOWAIT, &rx_data->map);
		if (error != 0) {
			printf("%s: could not create rx buf DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		rx_data->m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
		if (rx_data->m == NULL) {
			printf("%s: could not allocate rx mbuf\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}

		error = bus_dmamap_load(sc->sc_dmat, rx_data->map,
		    mtod(rx_data->m, void *), MCLBYTES, NULL,
		    BUS_DMA_NOWAIT | BUS_DMA_READ);
		if (error != 0) {
			printf("%s: could not load rx buf DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		rtwn_setup_rx_desc(sc, &rx_ring->desc[i],
		    rx_data->map->dm_segs[0].ds_addr, MCLBYTES, i);
	}
fail:	if (error != 0)
		rtwn_free_rx_list(sc);
	return (error);
}

void
rtwn_reset_rx_list(struct rtwn_pci_softc *sc)
{
	struct rtwn_rx_ring *rx_ring = &sc->rx_ring;
	struct rtwn_rx_data *rx_data;
	int i;

	for (i = 0; i < RTWN_RX_LIST_COUNT; i++) {
		rx_data = &rx_ring->rx_data[i];
		rtwn_setup_rx_desc(sc, &rx_ring->desc[i],
		    rx_data->map->dm_segs[0].ds_addr, MCLBYTES, i);
	}
}

void
rtwn_free_rx_list(struct rtwn_pci_softc *sc)
{
	struct rtwn_rx_ring *rx_ring = &sc->rx_ring;
	struct rtwn_rx_data *rx_data;
	int i, s;

	s = splnet();

	if (rx_ring->map) {
		if (rx_ring->desc) {
			bus_dmamap_unload(sc->sc_dmat, rx_ring->map);
			bus_dmamem_unmap(sc->sc_dmat, (caddr_t)rx_ring->desc,
			    sizeof (struct r92c_rx_desc_pci) *
			    RTWN_RX_LIST_COUNT);
			bus_dmamem_free(sc->sc_dmat, &rx_ring->seg,
			    rx_ring->nsegs);
			rx_ring->desc = NULL;
		}
		bus_dmamap_destroy(sc->sc_dmat, rx_ring->map);
		rx_ring->map = NULL;
	}

	for (i = 0; i < RTWN_RX_LIST_COUNT; i++) {
		rx_data = &rx_ring->rx_data[i];

		if (rx_data->m != NULL) {
			bus_dmamap_unload(sc->sc_dmat, rx_data->map);
			m_freem(rx_data->m);
			rx_data->m = NULL;
		}
		bus_dmamap_destroy(sc->sc_dmat, rx_data->map);
		rx_data->map = NULL;
	}

	splx(s);
}

int
rtwn_alloc_tx_list(struct rtwn_pci_softc *sc, int qid)
{
	struct rtwn_tx_ring *tx_ring = &sc->tx_ring[qid];
	struct rtwn_tx_data *tx_data;
	int i = 0, error = 0;

	error = bus_dmamap_create(sc->sc_dmat,
	    sizeof (struct r92c_tx_desc_pci) * RTWN_TX_LIST_COUNT, 1,
	    sizeof (struct r92c_tx_desc_pci) * RTWN_TX_LIST_COUNT, 0,
	    BUS_DMA_NOWAIT, &tx_ring->map);
	if (error != 0) {
		printf("%s: could not create tx ring DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat,
	    sizeof (struct r92c_tx_desc_pci) * RTWN_TX_LIST_COUNT, PAGE_SIZE, 0,
	    &tx_ring->seg, 1, &tx_ring->nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0) {
		printf("%s: could not allocate tx ring DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &tx_ring->seg, tx_ring->nsegs,
	    sizeof (struct r92c_tx_desc_pci) * RTWN_TX_LIST_COUNT,
	    (caddr_t *)&tx_ring->desc, BUS_DMA_NOWAIT);
	if (error != 0) {
		bus_dmamem_free(sc->sc_dmat, &tx_ring->seg, tx_ring->nsegs);
		printf("%s: can't map tx ring DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, tx_ring->map, tx_ring->desc,
	    sizeof (struct r92c_tx_desc_pci) * RTWN_TX_LIST_COUNT, NULL,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load tx ring DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	for (i = 0; i < RTWN_TX_LIST_COUNT; i++) {
		struct r92c_tx_desc_pci *desc = &tx_ring->desc[i];

		/* setup tx desc */
		desc->nextdescaddr = htole32(tx_ring->map->dm_segs[0].ds_addr
		  + sizeof(struct r92c_tx_desc_pci)
		  * ((i + 1) % RTWN_TX_LIST_COUNT));

		tx_data = &tx_ring->tx_data[i];
		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES,
		    0, BUS_DMA_NOWAIT, &tx_data->map);
		if (error != 0) {
			printf("%s: could not create tx buf DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
		tx_data->m = NULL;
		tx_data->ni = NULL;
	}
fail:
	if (error != 0)
		rtwn_free_tx_list(sc, qid);
	return (error);
}

void
rtwn_reset_tx_list(struct rtwn_pci_softc *sc, int qid)
{
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;
	struct rtwn_tx_ring *tx_ring = &sc->tx_ring[qid];
	int i;

	for (i = 0; i < RTWN_TX_LIST_COUNT; i++) {
		struct r92c_tx_desc_pci *desc = &tx_ring->desc[i];
		struct rtwn_tx_data *tx_data = &tx_ring->tx_data[i];

		memset(desc, 0, sizeof(*desc) -
		    (sizeof(desc->reserved) + sizeof(desc->nextdescaddr64) +
		    sizeof(desc->nextdescaddr)));

		if (tx_data->m != NULL) {
			bus_dmamap_unload(sc->sc_dmat, tx_data->map);
			m_freem(tx_data->m);
			tx_data->m = NULL;
			ieee80211_release_node(ic, tx_data->ni);
			tx_data->ni = NULL;
		}
	}

	bus_dmamap_sync(sc->sc_dmat, tx_ring->map, 0, MCLBYTES,
	    BUS_DMASYNC_POSTWRITE);

	sc->qfullmsk &= ~(1 << qid);
	tx_ring->queued = 0;
	tx_ring->cur = 0;
}

void
rtwn_free_tx_list(struct rtwn_pci_softc *sc, int qid)
{
	struct rtwn_tx_ring *tx_ring = &sc->tx_ring[qid];
	struct rtwn_tx_data *tx_data;
	int i;

	if (tx_ring->map != NULL) {
		if (tx_ring->desc != NULL) {
			bus_dmamap_unload(sc->sc_dmat, tx_ring->map);
			bus_dmamem_unmap(sc->sc_dmat, (caddr_t)tx_ring->desc,
			    sizeof (struct r92c_tx_desc_pci) *
			    RTWN_TX_LIST_COUNT);
			bus_dmamem_free(sc->sc_dmat, &tx_ring->seg, tx_ring->nsegs);
		}
		bus_dmamap_destroy(sc->sc_dmat, tx_ring->map);
	}

	for (i = 0; i < RTWN_TX_LIST_COUNT; i++) {
		tx_data = &tx_ring->tx_data[i];

		if (tx_data->m != NULL) {
			bus_dmamap_unload(sc->sc_dmat, tx_data->map);
			m_freem(tx_data->m);
			tx_data->m = NULL;
		}
		bus_dmamap_destroy(sc->sc_dmat, tx_data->map);
	}

	sc->qfullmsk &= ~(1 << qid);
	tx_ring->queued = 0;
	tx_ring->cur = 0;
}

void
rtwn_pci_write_1(void *cookie, uint16_t addr, uint8_t val)
{
	struct rtwn_pci_softc *sc = cookie;

	bus_space_write_1(sc->sc_st, sc->sc_sh, addr, val);
}

void
rtwn_pci_write_2(void *cookie, uint16_t addr, uint16_t val)
{
	struct rtwn_pci_softc *sc = cookie;

	val = htole16(val);
	bus_space_write_2(sc->sc_st, sc->sc_sh, addr, val);
}

void
rtwn_pci_write_4(void *cookie, uint16_t addr, uint32_t val)
{
	struct rtwn_pci_softc *sc = cookie;

	val = htole32(val);
	bus_space_write_4(sc->sc_st, sc->sc_sh, addr, val);
}

uint8_t
rtwn_pci_read_1(void *cookie, uint16_t addr)
{
	struct rtwn_pci_softc *sc = cookie;

	return bus_space_read_1(sc->sc_st, sc->sc_sh, addr);
}

uint16_t
rtwn_pci_read_2(void *cookie, uint16_t addr)
{
	struct rtwn_pci_softc *sc = cookie;
	uint16_t val;

	val = bus_space_read_2(sc->sc_st, sc->sc_sh, addr);
	return le16toh(val);
}

uint32_t
rtwn_pci_read_4(void *cookie, uint16_t addr)
{
	struct rtwn_pci_softc *sc = cookie;
	uint32_t val;

	val = bus_space_read_4(sc->sc_st, sc->sc_sh, addr);
	return le32toh(val);
}

void
rtwn_rx_frame(struct rtwn_pci_softc *sc, struct r92c_rx_desc_pci *rx_desc,
    struct rtwn_rx_data *rx_data, int desc_idx, struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	struct r92c_rx_phystat *phy = NULL;
	uint32_t rxdw0, rxdw3;
	struct mbuf *m, *m1;
	uint8_t rate;
	int8_t rssi = 0;
	int infosz, pktlen, shift, error;

	rxdw0 = letoh32(rx_desc->rxdw0);
	rxdw3 = letoh32(rx_desc->rxdw3);

	if (sc->sc_sc.chip & RTWN_CHIP_88E) {
		int ntries, type;
		struct r88e_tx_rpt_ccx *rxstat;

		type = MS(rxdw3, R88E_RXDW3_RPT);
		if (type == R88E_RXDW3_RPT_TX1) {
			uint32_t rptb1, rptb2;

			rxstat = mtod(rx_data->m, struct r88e_tx_rpt_ccx *);
			rptb1 = letoh32(rxstat->rptb1);
			rptb2 = letoh32(rxstat->rptb2);
			ntries = MS(rptb2, R88E_RPTB2_RETRY_CNT);
			if (rptb1 & R88E_RPTB1_PKT_OK)
				sc->amn.amn_txcnt++;
			if (ntries > 0)
				sc->amn.amn_retrycnt++;

			rtwn_setup_rx_desc(sc, rx_desc,
			    rx_data->map->dm_segs[0].ds_addr, MCLBYTES,
			    desc_idx);
			return;
		}
	}

	if (__predict_false(rxdw0 & (R92C_RXDW0_CRCERR | R92C_RXDW0_ICVERR))) {
		/*
		 * This should not happen since we setup our Rx filter
		 * to not receive these frames.
		 */
		ifp->if_ierrors++;
		return;
	}

	pktlen = MS(rxdw0, R92C_RXDW0_PKTLEN);
	if (__predict_false(pktlen < sizeof(*wh) || pktlen > MCLBYTES)) {
		ifp->if_ierrors++;
		return;
	}

	rate = MS(rxdw3, R92C_RXDW3_RATE);
	infosz = MS(rxdw0, R92C_RXDW0_INFOSZ) * 8;
	if (infosz > sizeof(struct r92c_rx_phystat))
		infosz = sizeof(struct r92c_rx_phystat);
	shift = MS(rxdw0, R92C_RXDW0_SHIFT);

	/* Get RSSI from PHY status descriptor if present. */
	if (infosz != 0 && (rxdw0 & R92C_RXDW0_PHYST)) {
		phy = mtod(rx_data->m, struct r92c_rx_phystat *);
		rssi = rtwn_get_rssi(&sc->sc_sc, rate, phy);
		/* Update our average RSSI. */
		rtwn_update_avgrssi(&sc->sc_sc, rate, rssi);
	}

	DPRINTFN(5, ("Rx frame len=%d rate=%d infosz=%d shift=%d rssi=%d\n",
	    pktlen, rate, infosz, shift, rssi));

	m1 = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
	if (m1 == NULL) {
		ifp->if_ierrors++;
		return;
	}
	bus_dmamap_unload(sc->sc_dmat, rx_data->map);
	error = bus_dmamap_load(sc->sc_dmat, rx_data->map,
	    mtod(m1, void *), MCLBYTES, NULL,
	    BUS_DMA_NOWAIT | BUS_DMA_READ);
	if (error != 0) {
		m_freem(m1);

		if (bus_dmamap_load_mbuf(sc->sc_dmat, rx_data->map,
		    rx_data->m, BUS_DMA_NOWAIT))
			panic("%s: could not load old RX mbuf",
			    sc->sc_dev.dv_xname);

		/* Physical address may have changed. */
		rtwn_setup_rx_desc(sc, rx_desc,
		    rx_data->map->dm_segs[0].ds_addr, MCLBYTES, desc_idx);

		ifp->if_ierrors++;
		return;
	}

	/* Finalize mbuf. */
	m = rx_data->m;
	rx_data->m = m1;
	m->m_pkthdr.len = m->m_len = pktlen + infosz + shift;

	/* Update RX descriptor. */
	rtwn_setup_rx_desc(sc, rx_desc, rx_data->map->dm_segs[0].ds_addr,
	    MCLBYTES, desc_idx);

	/* Get ieee80211 frame header. */
	if (rxdw0 & R92C_RXDW0_PHYST)
		m_adj(m, infosz + shift);
	else
		m_adj(m, shift);
	wh = mtod(m, struct ieee80211_frame *);

#if NBPFILTER > 0
	if (__predict_false(sc->sc_drvbpf != NULL)) {
		struct rtwn_rx_radiotap_header *tap = &sc->sc_rxtap;
		struct mbuf mb;

		tap->wr_flags = 0;
		/* Map HW rate index to 802.11 rate. */
		tap->wr_flags = 2;
		if (!(rxdw3 & R92C_RXDW3_HT)) {
			switch (rate) {
			/* CCK. */
			case  0: tap->wr_rate =   2; break;
			case  1: tap->wr_rate =   4; break;
			case  2: tap->wr_rate =  11; break;
			case  3: tap->wr_rate =  22; break;
			/* OFDM. */
			case  4: tap->wr_rate =  12; break;
			case  5: tap->wr_rate =  18; break;
			case  6: tap->wr_rate =  24; break;
			case  7: tap->wr_rate =  36; break;
			case  8: tap->wr_rate =  48; break;
			case  9: tap->wr_rate =  72; break;
			case 10: tap->wr_rate =  96; break;
			case 11: tap->wr_rate = 108; break;
			}
		} else if (rate >= 12) {	/* MCS0~15. */
			/* Bit 7 set means HT MCS instead of rate. */
			tap->wr_rate = 0x80 | (rate - 12);
		}
		tap->wr_dbm_antsignal = rssi;
		tap->wr_chan_freq = htole16(ic->ic_ibss_chan->ic_freq);
		tap->wr_chan_flags = htole16(ic->ic_ibss_chan->ic_flags);

		mb.m_data = (caddr_t)tap;
		mb.m_len = sc->sc_rxtap_len;
		mb.m_next = m;
		mb.m_nextpkt = NULL;
		mb.m_type = 0;
		mb.m_flags = 0;
		bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_IN);
	}
#endif

	ni = ieee80211_find_rxnode(ic, wh);
	memset(&rxi, 0, sizeof(rxi));
	rxi.rxi_rssi = rssi;
	ieee80211_inputm(ifp, m, ni, &rxi, ml);
	/* Node is no longer needed. */
	ieee80211_release_node(ic, ni);
}

int
rtwn_tx(void *cookie, struct mbuf *m, struct ieee80211_node *ni)
{
	struct rtwn_pci_softc *sc = cookie;
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k = NULL;
	struct rtwn_tx_ring *tx_ring;
	struct rtwn_tx_data *data;
	struct r92c_tx_desc_pci *txd;
	uint16_t qos;
	uint8_t raid, type, tid, qid;
	int hasqos, error;

	wh = mtod(m, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_get_txkey(ic, wh, ni);
		if ((m = ieee80211_encrypt(ic, m, k)) == NULL)
			return (ENOBUFS);
		wh = mtod(m, struct ieee80211_frame *);
	}

	if ((hasqos = ieee80211_has_qos(wh))) {
		qos = ieee80211_get_qos(wh);
		tid = qos & IEEE80211_QOS_TID;
		qid = ieee80211_up_to_ac(ic, tid);
	} else if (type != IEEE80211_FC0_TYPE_DATA) {
		qid = RTWN_VO_QUEUE;
	} else
		qid = RTWN_BE_QUEUE;

	/* Grab a Tx buffer from the ring. */
	tx_ring = &sc->tx_ring[qid];
	data = &tx_ring->tx_data[tx_ring->cur];
	if (data->m != NULL) {
		m_freem(m);
		return (ENOBUFS);
	}

	/* Fill Tx descriptor. */
	txd = &tx_ring->desc[tx_ring->cur];
	if (htole32(txd->txdw0) & R92C_TXDW0_OWN) {
		m_freem(m);
		return (ENOBUFS);
	}
	txd->txdw0 = htole32(
	    SM(R92C_TXDW0_PKTLEN, m->m_pkthdr.len) |
	    SM(R92C_TXDW0_OFFSET, sizeof(*txd)) |
	    R92C_TXDW0_FSG | R92C_TXDW0_LSG);
	if (IEEE80211_IS_MULTICAST(wh->i_addr1))
		txd->txdw0 |= htole32(R92C_TXDW0_BMCAST);

	txd->txdw1 = 0;
#ifdef notyet
	if (k != NULL) {
		switch (k->k_cipher) {
		case IEEE80211_CIPHER_WEP40:
		case IEEE80211_CIPHER_WEP104:
		case IEEE80211_CIPHER_TKIP:
			cipher = R92C_TXDW1_CIPHER_RC4;
			break;
		case IEEE80211_CIPHER_CCMP:
			cipher = R92C_TXDW1_CIPHER_AES;
			break;
		default:
			cipher = R92C_TXDW1_CIPHER_NONE;
		}
		txd->txdw1 |= htole32(SM(R92C_TXDW1_CIPHER, cipher));
	}
#endif
	txd->txdw4 = 0;
	txd->txdw5 = 0;
	if (!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    type == IEEE80211_FC0_TYPE_DATA) {
		if (ic->ic_curmode == IEEE80211_MODE_11B ||
		    (sc->sc_sc.sc_flags & RTWN_FLAG_FORCE_RAID_11B))
			raid = R92C_RAID_11B;
		else
			raid = R92C_RAID_11BG;

		if (sc->sc_sc.chip & RTWN_CHIP_88E) {
			txd->txdw1 |= htole32(
			    SM(R88E_TXDW1_MACID, R92C_MACID_BSS) |
			    SM(R92C_TXDW1_QSEL, R92C_TXDW1_QSEL_BE) |
			    SM(R92C_TXDW1_RAID, raid));
			txd->txdw2 |= htole32(R88E_TXDW2_AGGBK);
		} else {
			txd->txdw1 |= htole32(
			    SM(R92C_TXDW1_MACID, R92C_MACID_BSS) |
			    SM(R92C_TXDW1_QSEL, R92C_TXDW1_QSEL_BE) |
			    SM(R92C_TXDW1_RAID, raid) |
			    R92C_TXDW1_AGGBK);
		}

		/* Request TX status report for AMRR. */
		txd->txdw2 |= htole32(R92C_TXDW2_CCX_RPT);

		if (m->m_pkthdr.len + IEEE80211_CRC_LEN > ic->ic_rtsthreshold) {
			txd->txdw4 |= htole32(R92C_TXDW4_RTSEN |
			    R92C_TXDW4_HWRTSEN);
		} else if (ic->ic_flags & IEEE80211_F_USEPROT) {
			if (ic->ic_protmode == IEEE80211_PROT_CTSONLY) {
				txd->txdw4 |= htole32(R92C_TXDW4_CTS2SELF |
				    R92C_TXDW4_HWRTSEN);
			} else if (ic->ic_protmode == IEEE80211_PROT_RTSCTS) {
				txd->txdw4 |= htole32(R92C_TXDW4_RTSEN |
				    R92C_TXDW4_HWRTSEN);
			}
		}

		if (ic->ic_curmode == IEEE80211_MODE_11B)
			txd->txdw4 |= htole32(SM(R92C_TXDW4_RTSRATE, 0));
		else
			txd->txdw4 |= htole32(SM(R92C_TXDW4_RTSRATE, 8));
		txd->txdw5 |= htole32(SM(R92C_TXDW5_RTSRATE_FBLIMIT, 0xf));

		/* Use AMMR rate for data. */
		txd->txdw4 |= htole32(R92C_TXDW4_DRVRATE);
		if (ic->ic_fixed_rate != -1)
			txd->txdw5 |= htole32(SM(R92C_TXDW5_DATARATE,
			    ic->ic_fixed_rate));
		else
			txd->txdw5 |= htole32(SM(R92C_TXDW5_DATARATE,
			    ni->ni_txrate));
		txd->txdw5 |= htole32(SM(R92C_TXDW5_DATARATE_FBLIMIT, 0x1f));
	} else {
		txd->txdw1 |= htole32(
		    SM(R92C_TXDW1_MACID, 0) |
		    SM(R92C_TXDW1_QSEL, R92C_TXDW1_QSEL_MGNT) |
		    SM(R92C_TXDW1_RAID, R92C_RAID_11B));

		/* Force CCK1. */
		txd->txdw4 |= htole32(R92C_TXDW4_DRVRATE);
		txd->txdw5 |= htole32(SM(R92C_TXDW5_DATARATE, 0));
	}
	/* Set sequence number (already little endian). */
	txd->txdseq = (*(uint16_t *)wh->i_seq) >> IEEE80211_SEQ_SEQ_SHIFT;
	if (sc->sc_sc.chip & RTWN_CHIP_23A)
		txd->txdseq |= htole16(R23A_TXDW3_TXRPTEN);

	if (!hasqos) {
		/* Use HW sequence numbering for non-QoS frames. */
		if (!(sc->sc_sc.chip & RTWN_CHIP_23A))
			txd->txdw4 |= htole32(R92C_TXDW4_HWSEQ);
		txd->txdseq |= htole16(R92C_TXDW3_HWSEQEN);
	} else
		txd->txdw4 |= htole32(R92C_TXDW4_QOS);

	error = bus_dmamap_load_mbuf(sc->sc_dmat, data->map, m,
	    BUS_DMA_NOWAIT | BUS_DMA_WRITE);
	if (error && error != EFBIG) {
		printf("%s: can't map mbuf (error %d)\n",
		    sc->sc_dev.dv_xname, error);
		m_freem(m);
		return error;
	}
	if (error != 0) {
		/* Too many DMA segments, linearize mbuf. */
		if (m_defrag(m, M_DONTWAIT)) {
			m_freem(m);
			return ENOBUFS;
		}

		error = bus_dmamap_load_mbuf(sc->sc_dmat, data->map, m,
		    BUS_DMA_NOWAIT | BUS_DMA_WRITE);
		if (error != 0) {
			printf("%s: can't map mbuf (error %d)\n",
			    sc->sc_dev.dv_xname, error);
			m_freem(m);
			return error;
		}
	}

	txd->txbufaddr = htole32(data->map->dm_segs[0].ds_addr);
	txd->txbufsize = htole16(m->m_pkthdr.len);
	bus_space_barrier(sc->sc_st, sc->sc_sh, 0, sc->sc_mapsize,
	    BUS_SPACE_BARRIER_WRITE);
	txd->txdw0 |= htole32(R92C_TXDW0_OWN);

	bus_dmamap_sync(sc->sc_dmat, tx_ring->map, 0, MCLBYTES,
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(sc->sc_dmat, data->map, 0, MCLBYTES,
	    BUS_DMASYNC_POSTWRITE);

	data->m = m;
	data->ni = ni;

#if NBPFILTER > 0
	if (__predict_false(sc->sc_drvbpf != NULL)) {
		struct rtwn_tx_radiotap_header *tap = &sc->sc_txtap;
		struct mbuf mb;

		tap->wt_flags = 0;
		tap->wt_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);

		mb.m_data = (caddr_t)tap;
		mb.m_len = sc->sc_txtap_len;
		mb.m_next = m;
		mb.m_nextpkt = NULL;
		mb.m_type = 0;
		mb.m_flags = 0;
		bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_OUT);
	}
#endif

	tx_ring->cur = (tx_ring->cur + 1) % RTWN_TX_LIST_COUNT;
	tx_ring->queued++;

	if (tx_ring->queued >= (RTWN_TX_LIST_COUNT - 1))
		sc->qfullmsk |= (1 << qid);

	/* Kick TX. */
	rtwn_pci_write_2(sc, R92C_PCIE_CTRL_REG, (1 << qid));

	return (0);
}

void
rtwn_tx_done(struct rtwn_pci_softc *sc, int qid)
{
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct rtwn_tx_ring *tx_ring = &sc->tx_ring[qid];
	struct rtwn_tx_data *tx_data;
	struct r92c_tx_desc_pci *tx_desc;
	int i;

	bus_dmamap_sync(sc->sc_dmat, tx_ring->map, 0, MCLBYTES,
	    BUS_DMASYNC_POSTREAD);

	for (i = 0; i < RTWN_TX_LIST_COUNT; i++) {
		tx_data = &tx_ring->tx_data[i];
		if (tx_data->m == NULL)
			continue;

		tx_desc = &tx_ring->desc[i];
		if (letoh32(tx_desc->txdw0) & R92C_TXDW0_OWN)
			continue;

		bus_dmamap_unload(sc->sc_dmat, tx_data->map);
		m_freem(tx_data->m);
		tx_data->m = NULL;
		ieee80211_release_node(ic, tx_data->ni);
		tx_data->ni = NULL;

		sc->sc_sc.sc_tx_timer = 0;
		tx_ring->queued--;

		if (!(sc->sc_sc.chip & RTWN_CHIP_23A))
			rtwn_poll_c2h_events(sc);
	}

	if (tx_ring->queued < (RTWN_TX_LIST_COUNT - 1))
		sc->qfullmsk &= ~(1 << qid);
	
	if (sc->qfullmsk == 0) {
		ifq_clr_oactive(&ifp->if_snd);
		(*ifp->if_start)(ifp);
	}
}

int
rtwn_alloc_buffers(void *cookie)
{
	/* Tx/Rx buffers were already allocated in rtwn_pci_attach() */
	return (0);
}

int
rtwn_pci_init(void *cookie)
{
	struct rtwn_pci_softc *sc = cookie;
	ieee80211_amrr_node_init(&sc->amrr, &sc->amn);

	/* Enable TX reports for AMRR */
	if (sc->sc_sc.chip & RTWN_CHIP_88E) {
		rtwn_pci_write_1(sc, R88E_TX_RPT_CTRL,
		    (rtwn_pci_read_1(sc, R88E_TX_RPT_CTRL) & ~0) |
		    R88E_TX_RPT_CTRL_EN);
		rtwn_pci_write_1(sc, R88E_TX_RPT_CTRL + 1, 0x02);

		rtwn_pci_write_2(sc, R88E_TX_RPT_TIME, 0xcdf0);
	}

	return (0);
}

void
rtwn_pci_92c_stop(struct rtwn_pci_softc *sc)
{
	uint16_t reg;

	/* Disable interrupts. */
	rtwn_pci_write_4(sc, R92C_HIMR, 0x00000000);

	/* Stop hardware. */
	rtwn_pci_write_1(sc, R92C_TXPAUSE, R92C_TXPAUSE_ALL);
	rtwn_pci_write_1(sc, R92C_RF_CTRL, 0x00);
	reg = rtwn_pci_read_1(sc, R92C_SYS_FUNC_EN);
	reg |= R92C_SYS_FUNC_EN_BB_GLB_RST;
	rtwn_pci_write_1(sc, R92C_SYS_FUNC_EN, reg);
	reg &= ~R92C_SYS_FUNC_EN_BB_GLB_RST;
	rtwn_pci_write_1(sc, R92C_SYS_FUNC_EN, reg);
	reg = rtwn_pci_read_2(sc, R92C_CR);
	reg &= ~(R92C_CR_HCI_TXDMA_EN | R92C_CR_HCI_RXDMA_EN |
	    R92C_CR_TXDMA_EN | R92C_CR_RXDMA_EN | R92C_CR_PROTOCOL_EN |
	    R92C_CR_SCHEDULE_EN | R92C_CR_MACTXEN | R92C_CR_MACRXEN |
	    R92C_CR_ENSEC);
	rtwn_pci_write_2(sc, R92C_CR, reg);
	if (rtwn_pci_read_1(sc, R92C_MCUFWDL) & R92C_MCUFWDL_RAM_DL_SEL)
		rtwn_fw_reset(&sc->sc_sc);
	/* TODO: linux does additional btcoex stuff here */
	rtwn_pci_write_2(sc, R92C_AFE_PLL_CTRL, 0x80); /* linux magic number */
	rtwn_pci_write_1(sc, R92C_SPS0_CTRL, 0x23); /* ditto */
	rtwn_pci_write_1(sc, R92C_AFE_XTAL_CTRL, 0x0e); /* differs in btcoex */
	rtwn_pci_write_1(sc, R92C_RSV_CTRL, R92C_RSV_CTRL_WLOCK_00 |
	    R92C_RSV_CTRL_WLOCK_04 | R92C_RSV_CTRL_WLOCK_08);
	rtwn_pci_write_1(sc, R92C_APS_FSMCO, R92C_APS_FSMCO_PDN_EN);
}

void
rtwn_pci_88e_stop(struct rtwn_pci_softc *sc)
{
	int i;
	uint16_t reg;

	/* Disable interrupts. */
	rtwn_pci_write_4(sc, R88E_HIMR, 0x00000000);

	/* Stop hardware. */
	rtwn_pci_write_1(sc, R88E_TX_RPT_CTRL,
	    rtwn_pci_read_1(sc, R88E_TX_RPT_CTRL) &
	    ~(R88E_TX_RPT_CTRL_EN));

	for (i = 0; i < 100; i++) {
		if (rtwn_pci_read_1(sc, R88E_RXDMA_CTRL) & 0x02)
			break;
		DELAY(10);
	}
	if (i == 100)
		DPRINTF(("rxdma ctrl didn't go off, %x\n", rtwn_pci_read_1(sc, R88E_RXDMA_CTRL)));

	rtwn_pci_write_1(sc, R92C_PCIE_CTRL_REG + 1, 0xff);

	rtwn_pci_write_1(sc, R92C_TXPAUSE, R92C_TXPAUSE_ALL);

	/* ensure transmission has stopped */
	for (i = 0; i < 100; i++) {
		if (rtwn_pci_read_4(sc, 0x5f8) == 0)
			break;
		DELAY(10);
	}
	if (i == 100)
		DPRINTF(("tx didn't stop\n"));

	rtwn_pci_write_1(sc, R92C_SYS_FUNC_EN,
	    rtwn_pci_read_1(sc, R92C_SYS_FUNC_EN) &
	    ~(R92C_SYS_FUNC_EN_BBRSTB));
	DELAY(1);
	reg = rtwn_pci_read_2(sc, R92C_CR);
	reg &= ~(R92C_CR_HCI_TXDMA_EN | R92C_CR_HCI_RXDMA_EN |
	    R92C_CR_TXDMA_EN | R92C_CR_RXDMA_EN | R92C_CR_PROTOCOL_EN |
	    R92C_CR_SCHEDULE_EN | R92C_CR_MACTXEN | R92C_CR_MACRXEN |
	    R92C_CR_ENSEC);
	rtwn_pci_write_2(sc, R92C_CR, reg);
	rtwn_pci_write_1(sc, R92C_DUAL_TSF_RST,
	    rtwn_pci_read_1(sc, R92C_DUAL_TSF_RST) | 0x20);

	rtwn_pci_write_1(sc, R92C_RF_CTRL, 0x00);
	if (rtwn_pci_read_1(sc, R92C_MCUFWDL) & R92C_MCUFWDL_RAM_DL_SEL)
		rtwn_fw_reset(&sc->sc_sc);

	rtwn_pci_write_1(sc, R92C_SYS_FUNC_EN + 1,
	    rtwn_pci_read_1(sc, R92C_SYS_FUNC_EN + 1) & ~0x02);
	rtwn_pci_write_1(sc, R92C_MCUFWDL, 0);

	rtwn_pci_write_1(sc, R88E_32K_CTRL,
	    rtwn_pci_read_1(sc, R88E_32K_CTRL) & ~(0x01));

	/* transition to cardemu state */
	rtwn_pci_write_1(sc, R92C_RF_CTRL, 0);
	rtwn_pci_write_1(sc, R92C_LPLDO_CTRL,
	    rtwn_pci_read_1(sc, R92C_LPLDO_CTRL) | 0x10);
	rtwn_pci_write_2(sc, R92C_APS_FSMCO,
	    rtwn_pci_read_2(sc, R92C_APS_FSMCO) | R92C_APS_FSMCO_APFM_OFF);
	for (i = 0; i < 100; i++) {
		if ((rtwn_pci_read_2(sc, R92C_APS_FSMCO) &
		    R92C_APS_FSMCO_APFM_OFF) == 0)
			break;
		DELAY(10);
	}
	if (i == 100)
		DPRINTF(("apfm off didn't go off\n"));

	/* transition to card disabled state */
	rtwn_pci_write_1(sc, R92C_AFE_XTAL_CTRL + 2,
	    rtwn_pci_read_1(sc, R92C_AFE_XTAL_CTRL + 2) | 0x80);

	rtwn_pci_write_1(sc, R92C_RSV_CTRL + 1,
	    rtwn_pci_read_1(sc, R92C_RSV_CTRL + 1) & ~R92C_RSV_CTRL_WLOCK_08);
	rtwn_pci_write_1(sc, R92C_RSV_CTRL + 1,
	    rtwn_pci_read_1(sc, R92C_RSV_CTRL + 1) | R92C_RSV_CTRL_WLOCK_08);

	rtwn_pci_write_1(sc, R92C_RSV_CTRL, R92C_RSV_CTRL_WLOCK_00 |
	    R92C_RSV_CTRL_WLOCK_04 | R92C_RSV_CTRL_WLOCK_08);
}

void
rtwn_pci_23a_stop(struct rtwn_pci_softc *sc)
{
	int i;

	/* Disable interrupts. */
	rtwn_pci_write_4(sc, R92C_HIMR, 0x00000000);

	rtwn_pci_write_1(sc, R92C_PCIE_CTRL_REG + 1, 0xff);
	rtwn_pci_write_1(sc, R92C_TXPAUSE, R92C_TXPAUSE_ALL);

	/* ensure transmission has stopped */
	for (i = 0; i < 100; i++) {
		if (rtwn_pci_read_4(sc, 0x5f8) == 0)
			break;
		DELAY(10);
	}
	if (i == 100)
		DPRINTF(("tx didn't stop\n"));

	rtwn_pci_write_1(sc, R92C_SYS_FUNC_EN,
	    rtwn_pci_read_1(sc, R92C_SYS_FUNC_EN) &
	    ~(R92C_SYS_FUNC_EN_BBRSTB));
	DELAY(1);
	rtwn_pci_write_1(sc, R92C_SYS_FUNC_EN,
	    rtwn_pci_read_1(sc, R92C_SYS_FUNC_EN) &
	    ~(R92C_SYS_FUNC_EN_BB_GLB_RST));

	rtwn_pci_write_2(sc, R92C_CR,
	    rtwn_pci_read_2(sc, R92C_CR) &
	    ~(R92C_CR_MACTXEN | R92C_CR_MACRXEN | R92C_CR_ENSWBCN));

	rtwn_pci_write_1(sc, R92C_DUAL_TSF_RST,
	    rtwn_pci_read_1(sc, R92C_DUAL_TSF_RST) | 0x20);

	/* Turn off RF */
	rtwn_pci_write_1(sc, R92C_RF_CTRL, 0x00);
	if (rtwn_pci_read_1(sc, R92C_MCUFWDL) & R92C_MCUFWDL_RAM_DL_SEL)
		rtwn_fw_reset(&sc->sc_sc);

	rtwn_pci_write_1(sc, R92C_SYS_FUNC_EN + 1,
	    rtwn_pci_read_1(sc, R92C_SYS_FUNC_EN + 1) & ~R92C_SYS_FUNC_EN_DIOE);
	rtwn_pci_write_1(sc, R92C_MCUFWDL, 0);

	rtwn_pci_write_1(sc, R92C_RF_CTRL, 0x00);
	rtwn_pci_write_1(sc, R92C_LEDCFG2, rtwn_pci_read_1(sc, R92C_LEDCFG2) & ~(0x80));
	rtwn_pci_write_2(sc, R92C_APS_FSMCO, rtwn_pci_read_2(sc, R92C_APS_FSMCO) |
	    R92C_APS_FSMCO_APFM_OFF);
	rtwn_pci_write_2(sc, R92C_APS_FSMCO, rtwn_pci_read_2(sc, R92C_APS_FSMCO) &
	    ~(R92C_APS_FSMCO_APFM_OFF));

	rtwn_pci_write_4(sc, R92C_APS_FSMCO,
	    rtwn_pci_read_4(sc, R92C_APS_FSMCO) & ~R92C_APS_FSMCO_RDY_MACON);
	rtwn_pci_write_4(sc, R92C_APS_FSMCO,
	    rtwn_pci_read_4(sc, R92C_APS_FSMCO) | R92C_APS_FSMCO_APDM_HPDN);

	rtwn_pci_write_1(sc, R92C_RSV_CTRL + 1,
	    rtwn_pci_read_1(sc, R92C_RSV_CTRL + 1) & ~R92C_RSV_CTRL_WLOCK_08);
	rtwn_pci_write_1(sc, R92C_RSV_CTRL + 1,
	    rtwn_pci_read_1(sc, R92C_RSV_CTRL + 1) | R92C_RSV_CTRL_WLOCK_08);

	rtwn_pci_write_1(sc, R92C_RSV_CTRL, R92C_RSV_CTRL_WLOCK_00 |
	    R92C_RSV_CTRL_WLOCK_04 | R92C_RSV_CTRL_WLOCK_08);
}

void
rtwn_pci_stop(void *cookie)
{
	struct rtwn_pci_softc *sc = cookie;
	int i, s;

	s = splnet();

	if (sc->sc_sc.chip & RTWN_CHIP_88E) {
		rtwn_pci_88e_stop(sc);
	} else if (sc->sc_sc.chip & RTWN_CHIP_23A) {
		rtwn_pci_23a_stop(sc);
	} else {
		rtwn_pci_92c_stop(sc);
	}

	for (i = 0; i < RTWN_NTXQUEUES; i++)
		rtwn_reset_tx_list(sc, i);
	rtwn_reset_rx_list(sc);

	splx(s);
}

int
rtwn_88e_intr(struct rtwn_pci_softc *sc)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	u_int32_t status, estatus;
	int i;

	status = rtwn_pci_read_4(sc, R88E_HISR);
	if (status == 0 || status == 0xffffffff)
		return (0);

	estatus = rtwn_pci_read_4(sc, R88E_HISRE);

	status &= RTWN_88E_INT_ENABLE;
	estatus &= R88E_HIMRE_RXFOVW;

	rtwn_pci_write_4(sc, R88E_HIMR, 0);
	rtwn_pci_write_4(sc, R88E_HIMRE, 0);
	rtwn_pci_write_4(sc, R88E_HISR, status);
	rtwn_pci_write_4(sc, R88E_HISRE, estatus);

	if (status & R88E_HIMR_HIGHDOK)
		rtwn_tx_done(sc, RTWN_HIGH_QUEUE);
	if (status & R88E_HIMR_MGNTDOK)
		rtwn_tx_done(sc, RTWN_MGNT_QUEUE);
	if (status & R88E_HIMR_BKDOK)
		rtwn_tx_done(sc, RTWN_BK_QUEUE);
	if (status & R88E_HIMR_BEDOK)
		rtwn_tx_done(sc, RTWN_BE_QUEUE);
	if (status & R88E_HIMR_VIDOK)
		rtwn_tx_done(sc, RTWN_VI_QUEUE);
	if (status & R88E_HIMR_VODOK)
		rtwn_tx_done(sc, RTWN_VO_QUEUE);
	if ((status & (R88E_HIMR_ROK | R88E_HIMR_RDU)) ||
	    (estatus & R88E_HIMRE_RXFOVW)) {
		struct ieee80211com *ic = &sc->sc_sc.sc_ic;

		bus_dmamap_sync(sc->sc_dmat, sc->rx_ring.map, 0,
		    sizeof(struct r92c_rx_desc_pci) * RTWN_RX_LIST_COUNT,
		    BUS_DMASYNC_POSTREAD);

		for (i = 0; i < RTWN_RX_LIST_COUNT; i++) {
			struct r92c_rx_desc_pci *rx_desc = &sc->rx_ring.desc[i];
			struct rtwn_rx_data *rx_data = &sc->rx_ring.rx_data[i];

			if (letoh32(rx_desc->rxdw0) & R92C_RXDW0_OWN)
				continue;

			rtwn_rx_frame(sc, rx_desc, rx_data, i, &ml);
		}
		if_input(&ic->ic_if, &ml);
	}

	if (status & R88E_HIMR_HSISR_IND_ON_INT) {
		rtwn_pci_write_1(sc, R92C_HSISR,
		    rtwn_pci_read_1(sc, R92C_HSISR) |
		    R88E_HSIMR_PDN_INT_EN | R88E_HSIMR_RON_INT_EN);
	}

	/* Enable interrupts. */
	rtwn_pci_write_4(sc, R88E_HIMR, RTWN_88E_INT_ENABLE);
	rtwn_pci_write_4(sc, R88E_HIMRE, R88E_HIMRE_RXFOVW);

	return (1);
}

int
rtwn_intr(void *xsc)
{
	struct rtwn_pci_softc *sc = xsc;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	u_int32_t status;
	int i;

	if (sc->sc_sc.chip & RTWN_CHIP_88E)
		return (rtwn_88e_intr(sc));

	status = rtwn_pci_read_4(sc, R92C_HISR);
	if (status == 0 || status == 0xffffffff)
		return (0);

	/* Disable interrupts. */
	rtwn_pci_write_4(sc, R92C_HIMR, 0x00000000);

	/* Ack interrupts. */
	rtwn_pci_write_4(sc, R92C_HISR, status);

	/* Vendor driver treats RX errors like ROK... */
	if (status & (R92C_IMR_ROK | R92C_IMR_RXFOVW | R92C_IMR_RDU)) {
		struct ieee80211com *ic = &sc->sc_sc.sc_ic;

		bus_dmamap_sync(sc->sc_dmat, sc->rx_ring.map, 0,
		    sizeof(struct r92c_rx_desc_pci) * RTWN_RX_LIST_COUNT,
		    BUS_DMASYNC_POSTREAD);

		for (i = 0; i < RTWN_RX_LIST_COUNT; i++) {
			struct r92c_rx_desc_pci *rx_desc = &sc->rx_ring.desc[i];
			struct rtwn_rx_data *rx_data = &sc->rx_ring.rx_data[i];

			if (letoh32(rx_desc->rxdw0) & R92C_RXDW0_OWN)
				continue;

			rtwn_rx_frame(sc, rx_desc, rx_data, i, &ml);
		}
		if_input(&ic->ic_if, &ml);
	}

	if (status & R92C_IMR_BDOK)
		rtwn_tx_done(sc, RTWN_BEACON_QUEUE);
	if (status & R92C_IMR_HIGHDOK)
		rtwn_tx_done(sc, RTWN_HIGH_QUEUE);
	if (status & R92C_IMR_MGNTDOK)
		rtwn_tx_done(sc, RTWN_MGNT_QUEUE);
	if (status & R92C_IMR_BKDOK)
		rtwn_tx_done(sc, RTWN_BK_QUEUE);
	if (status & R92C_IMR_BEDOK)
		rtwn_tx_done(sc, RTWN_BE_QUEUE);
	if (status & R92C_IMR_VIDOK)
		rtwn_tx_done(sc, RTWN_VI_QUEUE);
	if (status & R92C_IMR_VODOK)
		rtwn_tx_done(sc, RTWN_VO_QUEUE);

	if (sc->sc_sc.chip & RTWN_CHIP_23A) {
		if (status & R92C_IMR_ATIMEND)
			rtwn_poll_c2h_events(sc);
	}

	/* Enable interrupts. */
	rtwn_pci_write_4(sc, R92C_HIMR, RTWN_92C_INT_ENABLE);

	return (1);
}

int
rtwn_is_oactive(void *cookie)
{
	struct rtwn_pci_softc *sc = cookie;
	
	return (sc->qfullmsk != 0);
}

int
rtwn_llt_write(struct rtwn_pci_softc *sc, uint32_t addr, uint32_t data)
{
	int ntries;

	rtwn_pci_write_4(sc, R92C_LLT_INIT,
	    SM(R92C_LLT_INIT_OP, R92C_LLT_INIT_OP_WRITE) |
	    SM(R92C_LLT_INIT_ADDR, addr) |
	    SM(R92C_LLT_INIT_DATA, data));
	/* Wait for write operation to complete. */
	for (ntries = 0; ntries < 20; ntries++) {
		if (MS(rtwn_pci_read_4(sc, R92C_LLT_INIT), R92C_LLT_INIT_OP) ==
		    R92C_LLT_INIT_OP_NO_ACTIVE)
			return (0);
		DELAY(5);
	}
	return (ETIMEDOUT);
}

int
rtwn_llt_init(struct rtwn_pci_softc *sc, int page_count)
{
	int i, error, pktbuf_count;

	if (sc->sc_sc.chip & RTWN_CHIP_88E)
		pktbuf_count = R88E_TXPKTBUF_COUNT;
	else if (sc->sc_sc.chip & RTWN_CHIP_23A)
		pktbuf_count = R23A_TXPKTBUF_COUNT;
	else
		pktbuf_count = R92C_TXPKTBUF_COUNT;

	/* Reserve pages [0; page_count]. */
	for (i = 0; i < page_count; i++) {
		if ((error = rtwn_llt_write(sc, i, i + 1)) != 0)
			return (error);
	}
	/* NB: 0xff indicates end-of-list. */
	if ((error = rtwn_llt_write(sc, i, 0xff)) != 0)
		return (error);
	/*
	 * Use pages [page_count + 1; pktbuf_count - 1]
	 * as ring buffer.
	 */
	for (++i; i < pktbuf_count - 1; i++) {
		if ((error = rtwn_llt_write(sc, i, i + 1)) != 0)
			return (error);
	}
	/* Make the last page point to the beginning of the ring buffer. */
	error = rtwn_llt_write(sc, i, pktbuf_count + 1);
	return (error);
}

int
rtwn_92c_power_on(struct rtwn_pci_softc *sc)
{
	uint32_t reg;
	int ntries;

	/* Wait for autoload done bit. */
	for (ntries = 0; ntries < 1000; ntries++) {
		if (rtwn_pci_read_1(sc, R92C_APS_FSMCO) &
		    R92C_APS_FSMCO_PFM_ALDN)
			break;
		DELAY(5);
	}
	if (ntries == 1000) {
		printf("%s: timeout waiting for chip autoload\n",
		    sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}

	/* Unlock ISO/CLK/Power control register. */
	rtwn_pci_write_1(sc, R92C_RSV_CTRL, 0);

	/* TODO: check if we need this for 8188CE */
	if (sc->sc_sc.board_type != R92C_BOARD_TYPE_DONGLE) {
		/* bt coex */
		reg = rtwn_pci_read_4(sc, R92C_APS_FSMCO);
		reg |= (R92C_APS_FSMCO_SOP_ABG |
			R92C_APS_FSMCO_SOP_AMB |
			R92C_APS_FSMCO_XOP_BTCK);
		rtwn_pci_write_4(sc, R92C_APS_FSMCO, reg);
	}

	/* Move SPS into PWM mode. */
	rtwn_pci_write_1(sc, R92C_SPS0_CTRL, 0x2b);
	DELAY(100);

	/* Set low byte to 0x0f, leave others unchanged. */
	rtwn_pci_write_4(sc, R92C_AFE_XTAL_CTRL,
	    (rtwn_pci_read_4(sc, R92C_AFE_XTAL_CTRL) & 0xffffff00) | 0x0f);

	/* TODO: check if we need this for 8188CE */
	if (sc->sc_sc.board_type != R92C_BOARD_TYPE_DONGLE) {
		/* bt coex */
		reg = rtwn_pci_read_4(sc, R92C_AFE_XTAL_CTRL);
		reg &= (~0x00024800); /* XXX magic from linux */
		rtwn_pci_write_4(sc, R92C_AFE_XTAL_CTRL, reg);
	}

	rtwn_pci_write_2(sc, R92C_SYS_ISO_CTRL,
	  (rtwn_pci_read_2(sc, R92C_SYS_ISO_CTRL) & 0xff) |
	  R92C_SYS_ISO_CTRL_PWC_EV12V | R92C_SYS_ISO_CTRL_DIOR);
	DELAY(200);

	/* TODO: linux does additional btcoex stuff here */

	/* Auto enable WLAN. */
	rtwn_pci_write_2(sc, R92C_APS_FSMCO,
	    rtwn_pci_read_2(sc, R92C_APS_FSMCO) | R92C_APS_FSMCO_APFM_ONMAC);
	for (ntries = 0; ntries < 1000; ntries++) {
		if (!(rtwn_pci_read_2(sc, R92C_APS_FSMCO) &
		    R92C_APS_FSMCO_APFM_ONMAC))
			break;
		DELAY(5);
	}
	if (ntries == 1000) {
		printf("%s: timeout waiting for MAC auto ON\n",
		    sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}

	/* Enable radio, GPIO and LED functions. */
	rtwn_pci_write_2(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_AFSM_PCIE |
	    R92C_APS_FSMCO_PDN_EN |
	    R92C_APS_FSMCO_PFM_ALDN);
	/* Release RF digital isolation. */
	rtwn_pci_write_2(sc, R92C_SYS_ISO_CTRL,
	    rtwn_pci_read_2(sc, R92C_SYS_ISO_CTRL) & ~R92C_SYS_ISO_CTRL_DIOR);

	if (sc->sc_sc.chip & RTWN_CHIP_92C)
		rtwn_pci_write_1(sc, R92C_PCIE_CTRL_REG + 3, 0x77);
	else
		rtwn_pci_write_1(sc, R92C_PCIE_CTRL_REG + 3, 0x22);

	rtwn_pci_write_4(sc, R92C_INT_MIG, 0);

	if (sc->sc_sc.board_type != R92C_BOARD_TYPE_DONGLE) {
		/* bt coex */
		reg = rtwn_pci_read_4(sc, R92C_AFE_XTAL_CTRL + 2);
		reg &= 0xfd; /* XXX magic from linux */
		rtwn_pci_write_4(sc, R92C_AFE_XTAL_CTRL + 2, reg);
	}

	rtwn_pci_write_1(sc, R92C_GPIO_MUXCFG,
	    rtwn_pci_read_1(sc, R92C_GPIO_MUXCFG) & ~R92C_GPIO_MUXCFG_RFKILL);

	reg = rtwn_pci_read_1(sc, R92C_GPIO_IO_SEL);
	if (!(reg & R92C_GPIO_IO_SEL_RFKILL)) {
		printf("%s: radio is disabled by hardware switch\n",
		    sc->sc_dev.dv_xname);
		return (EPERM);	/* :-) */
	}

	/* Initialize MAC. */
	rtwn_pci_write_1(sc, R92C_APSD_CTRL,
	    rtwn_pci_read_1(sc, R92C_APSD_CTRL) & ~R92C_APSD_CTRL_OFF);
	for (ntries = 0; ntries < 200; ntries++) {
		if (!(rtwn_pci_read_1(sc, R92C_APSD_CTRL) &
		    R92C_APSD_CTRL_OFF_STATUS))
			break;
		DELAY(500);
	}
	if (ntries == 200) {
		printf("%s: timeout waiting for MAC initialization\n",
		    sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}

	/* Enable MAC DMA/WMAC/SCHEDULE/SEC blocks. */
	reg = rtwn_pci_read_2(sc, R92C_CR);
	reg |= R92C_CR_HCI_TXDMA_EN | R92C_CR_HCI_RXDMA_EN |
	    R92C_CR_TXDMA_EN | R92C_CR_RXDMA_EN | R92C_CR_PROTOCOL_EN |
	    R92C_CR_SCHEDULE_EN | R92C_CR_MACTXEN | R92C_CR_MACRXEN |
	    R92C_CR_ENSEC;
	rtwn_pci_write_2(sc, R92C_CR, reg);

	rtwn_pci_write_1(sc, 0xfe10, 0x19);

	return (0);
}

int
rtwn_88e_power_on(struct rtwn_pci_softc *sc)
{
	uint32_t reg;
	int ntries;

	/* Disable XTAL output for power saving. */
	rtwn_pci_write_1(sc, R88E_XCK_OUT_CTRL,
	    rtwn_pci_read_1(sc, R88E_XCK_OUT_CTRL) & ~R88E_XCK_OUT_CTRL_EN);

	rtwn_pci_write_2(sc, R92C_APS_FSMCO,
	    rtwn_pci_read_2(sc, R92C_APS_FSMCO) & (~R92C_APS_FSMCO_APDM_HPDN));
	rtwn_pci_write_1(sc, R92C_RSV_CTRL, 0);

	/* Wait for power ready bit. */
	for (ntries = 0; ntries < 5000; ntries++) {
		if (rtwn_pci_read_4(sc, R92C_APS_FSMCO) & R92C_APS_FSMCO_SUS_HOST)
			break;
		DELAY(10);
	}
	if (ntries == 5000) {
		printf("%s: timeout waiting for chip power up\n",
		    sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}

	/* Reset BB. */
	rtwn_pci_write_1(sc, R92C_SYS_FUNC_EN,
	    rtwn_pci_read_1(sc, R92C_SYS_FUNC_EN) & ~(R92C_SYS_FUNC_EN_BBRSTB |
	    R92C_SYS_FUNC_EN_BB_GLB_RST));

	rtwn_pci_write_1(sc, R92C_AFE_XTAL_CTRL + 2,
	    rtwn_pci_read_1(sc, R92C_AFE_XTAL_CTRL + 2) | 0x80);

	/* Disable HWPDN. */
	rtwn_pci_write_2(sc, R92C_APS_FSMCO,
	    rtwn_pci_read_2(sc, R92C_APS_FSMCO) & ~R92C_APS_FSMCO_APDM_HPDN);
	/* Disable WL suspend. */
	rtwn_pci_write_2(sc, R92C_APS_FSMCO,
	    rtwn_pci_read_2(sc, R92C_APS_FSMCO) &
	    ~(R92C_APS_FSMCO_AFSM_HSUS | R92C_APS_FSMCO_AFSM_PCIE));

	/* Auto enable WLAN. */
	rtwn_pci_write_2(sc, R92C_APS_FSMCO,
	    rtwn_pci_read_2(sc, R92C_APS_FSMCO) | R92C_APS_FSMCO_APFM_ONMAC);
	for (ntries = 0; ntries < 5000; ntries++) {
		if (!(rtwn_pci_read_2(sc, R92C_APS_FSMCO) &
		    R92C_APS_FSMCO_APFM_ONMAC))
			break;
		DELAY(10);
	}
	if (ntries == 5000) {
		printf("%s: timeout waiting for MAC auto ON\n",
		    sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}

	/* Enable LDO normal mode. */
	rtwn_pci_write_1(sc, R92C_LPLDO_CTRL,
	    rtwn_pci_read_1(sc, R92C_LPLDO_CTRL) & ~0x10);

	rtwn_pci_write_1(sc, R92C_APS_FSMCO,
	    rtwn_pci_read_1(sc, R92C_APS_FSMCO) | R92C_APS_FSMCO_PDN_EN);
	rtwn_pci_write_1(sc, R92C_PCIE_CTRL_REG + 2,
	    rtwn_pci_read_1(sc, R92C_PCIE_CTRL_REG + 2) | 0x04);

	rtwn_pci_write_1(sc, R92C_AFE_XTAL_CTRL_EXT + 1,
	    rtwn_pci_read_1(sc, R92C_AFE_XTAL_CTRL_EXT + 1) | 0x02);

	rtwn_pci_write_1(sc, R92C_SYS_CLKR,
	    rtwn_pci_read_1(sc, R92C_SYS_CLKR) | 0x08);

	rtwn_pci_write_2(sc, R92C_GPIO_MUXCFG,
	    rtwn_pci_read_2(sc, R92C_GPIO_MUXCFG) & ~R92C_GPIO_MUXCFG_ENSIC);

	/* Enable MAC DMA/WMAC/SCHEDULE/SEC blocks. */
	rtwn_pci_write_2(sc, R92C_CR, 0);
	reg = rtwn_pci_read_2(sc, R92C_CR);
	reg |= R92C_CR_HCI_TXDMA_EN | R92C_CR_HCI_RXDMA_EN |
	    R92C_CR_TXDMA_EN | R92C_CR_RXDMA_EN | R92C_CR_PROTOCOL_EN |
	    R92C_CR_SCHEDULE_EN | R92C_CR_MACTXEN | R92C_CR_MACRXEN |
	    R92C_CR_ENSEC | R92C_CR_CALTMR_EN;
	rtwn_pci_write_2(sc, R92C_CR, reg);

	rtwn_pci_write_1(sc, R92C_MSR, 0);
	return (0);
}

int
rtwn_23a_power_on(struct rtwn_pci_softc *sc)
{
	uint32_t reg;
	int ntries;

	rtwn_pci_write_1(sc, R92C_RSV_CTRL, 0x00);

	rtwn_pci_write_2(sc, R92C_APS_FSMCO,
	    rtwn_pci_read_2(sc, R92C_APS_FSMCO) &
	    ~(R92C_APS_FSMCO_AFSM_HSUS | R92C_APS_FSMCO_AFSM_PCIE));
	rtwn_pci_write_1(sc, R92C_PCIE_CTRL_REG + 1, 0x00);
	rtwn_pci_write_2(sc, R92C_APS_FSMCO,
	    rtwn_pci_read_2(sc, R92C_APS_FSMCO) & ~R92C_APS_FSMCO_APFM_RSM);

	/* Wait for power ready bit. */
	for (ntries = 0; ntries < 5000; ntries++) {
		if (rtwn_pci_read_4(sc, R92C_APS_FSMCO) & R92C_APS_FSMCO_SUS_HOST)
			break;
		DELAY(10);
	}
	if (ntries == 5000) {
		printf("%s: timeout waiting for chip power up\n",
		    sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}

	/* Release WLON reset */
	rtwn_pci_write_4(sc, R92C_APS_FSMCO, rtwn_pci_read_4(sc, R92C_APS_FSMCO) |
	    R92C_APS_FSMCO_RDY_MACON);
	/* Disable HWPDN. */
	rtwn_pci_write_2(sc, R92C_APS_FSMCO,
	    rtwn_pci_read_2(sc, R92C_APS_FSMCO) & ~R92C_APS_FSMCO_APDM_HPDN);
	/* Disable WL suspend. */
	rtwn_pci_write_2(sc, R92C_APS_FSMCO,
	    rtwn_pci_read_2(sc, R92C_APS_FSMCO) &
	    ~(R92C_APS_FSMCO_AFSM_HSUS | R92C_APS_FSMCO_AFSM_PCIE));

	/* Auto enable WLAN. */
	rtwn_pci_write_2(sc, R92C_APS_FSMCO,
	    rtwn_pci_read_2(sc, R92C_APS_FSMCO) | R92C_APS_FSMCO_APFM_ONMAC);
	for (ntries = 0; ntries < 5000; ntries++) {
		if (!(rtwn_pci_read_2(sc, R92C_APS_FSMCO) &
		    R92C_APS_FSMCO_APFM_ONMAC))
			break;
		DELAY(10);
	}
	if (ntries == 5000) {
		printf("%s: timeout waiting for MAC auto ON (%x)\n",
		    sc->sc_dev.dv_xname, rtwn_pci_read_2(sc, R92C_APS_FSMCO));
		return (ETIMEDOUT);
	}

	rtwn_pci_write_1(sc, R92C_PCIE_CTRL_REG + 2,
	    rtwn_pci_read_1(sc, R92C_PCIE_CTRL_REG + 2) | 0x04);

	/* emac time out */
	rtwn_pci_write_1(sc, 0x369, rtwn_pci_read_1(sc, 0x369) | 0x80);

	for (ntries = 0; ntries < 100; ntries++) {
		rtwn_pci_write_2(sc, R92C_MDIO + 4, 0x5e);
		DELAY(100);
		rtwn_pci_write_2(sc, R92C_MDIO + 2, 0xc280);
		rtwn_pci_write_2(sc, R92C_MDIO, 0xc290);
		rtwn_pci_write_2(sc, R92C_MDIO + 4, 0x3e);
		DELAY(100);
		rtwn_pci_write_2(sc, R92C_MDIO + 4, 0x5e);
		DELAY(100);
		if (rtwn_pci_read_2(sc, R92C_MDIO + 2) == 0xc290)
			break;
	}
	if (ntries == 100) {
		printf("%s: timeout configuring ePHY\n", sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}

	/* Enable MAC DMA/WMAC/SCHEDULE/SEC blocks. */
	rtwn_pci_write_2(sc, R92C_CR, 0);
	reg = rtwn_pci_read_2(sc, R92C_CR);
	reg |= R92C_CR_HCI_TXDMA_EN | R92C_CR_HCI_RXDMA_EN |
	    R92C_CR_TXDMA_EN | R92C_CR_RXDMA_EN | R92C_CR_PROTOCOL_EN |
	    R92C_CR_SCHEDULE_EN | R92C_CR_MACTXEN | R92C_CR_MACRXEN |
	    R92C_CR_ENSEC | R92C_CR_CALTMR_EN;
	rtwn_pci_write_2(sc, R92C_CR, reg);

	return (0);
}

int
rtwn_power_on(void *cookie)
{
	struct rtwn_pci_softc *sc = cookie;

	if (sc->sc_sc.chip & RTWN_CHIP_88E)
		return (rtwn_88e_power_on(sc));
	else if (sc->sc_sc.chip & RTWN_CHIP_23A)
		return (rtwn_23a_power_on(sc));
	else
		return (rtwn_92c_power_on(sc));
}

int
rtwn_dma_init(void *cookie)
{
	struct rtwn_pci_softc *sc = cookie;
	uint32_t reg;
	uint16_t dmasize;
	int hqpages, lqpages, nqpages, pagecnt, boundary, trxdma, tcr;
	int error;

	if (sc->sc_sc.chip & RTWN_CHIP_88E) {
		nqpages = R88E_NPQ_NPAGES;
		hqpages = R88E_HPQ_NPAGES;
		lqpages = R88E_LPQ_NPAGES;
		pagecnt = R88E_TX_PAGE_COUNT;
		boundary = R88E_TX_PAGE_BOUNDARY;
		dmasize = R88E_MAX_RX_DMA_SIZE;
		tcr = R92C_TCR_CFENDFORM | R92C_TCR_ERRSTEN3;
		trxdma = 0xe771;
	} else if (sc->sc_sc.chip & RTWN_CHIP_23A) {
		nqpages = R23A_NPQ_NPAGES;
		hqpages = R23A_HPQ_NPAGES;
		lqpages = R23A_LPQ_NPAGES;
		pagecnt = R23A_TX_PAGE_COUNT;
		boundary = R23A_TX_PAGE_BOUNDARY;
		dmasize = R23A_MAX_RX_DMA_SIZE;
		tcr = R92C_TCR_CFENDFORM | R92C_TCR_ERRSTEN0 |
		    R92C_TCR_ERRSTEN1;
		trxdma = 0xf771;
	} else {
		nqpages = R92C_NPQ_NPAGES;
		hqpages = R92C_HPQ_NPAGES;
		lqpages = R92C_LPQ_NPAGES;
		pagecnt = R92C_TX_PAGE_COUNT;
		boundary = R92C_TX_PAGE_BOUNDARY;
		dmasize = R92C_MAX_RX_DMA_SIZE;
		tcr = R92C_TCR_CFENDFORM | R92C_TCR_ERRSTEN0 |
		    R92C_TCR_ERRSTEN1;
		trxdma = 0xf771;
	}

	/* Initialize LLT table. */
	error = rtwn_llt_init(sc, pagecnt);
	if (error != 0)
		return error;

	/* Set number of pages for normal priority queue. */
	rtwn_pci_write_2(sc, R92C_RQPN_NPQ, nqpages);
	rtwn_pci_write_4(sc, R92C_RQPN,
	    /* Set number of pages for public queue. */
	    SM(R92C_RQPN_PUBQ, pagecnt) |
	    /* Set number of pages for high priority queue. */
	    SM(R92C_RQPN_HPQ, hqpages) |
	    /* Set number of pages for low priority queue. */
	    SM(R92C_RQPN_LPQ, lqpages) |
	    /* Load values. */
	    R92C_RQPN_LD);

	rtwn_pci_write_1(sc, R92C_TXPKTBUF_BCNQ_BDNY, boundary);
	rtwn_pci_write_1(sc, R92C_TXPKTBUF_MGQ_BDNY, boundary);
	rtwn_pci_write_1(sc, R92C_TXPKTBUF_WMAC_LBK_BF_HD,
	    boundary);
	rtwn_pci_write_1(sc, R92C_TRXFF_BNDY, boundary);
	rtwn_pci_write_1(sc, R92C_TDECTRL + 1, boundary);

	reg = rtwn_pci_read_2(sc, R92C_TRXDMA_CTRL);
	reg &= ~R92C_TRXDMA_CTRL_QMAP_M;
	reg |= trxdma;
	rtwn_pci_write_2(sc, R92C_TRXDMA_CTRL, reg);

	rtwn_pci_write_4(sc, R92C_TCR, tcr);

	/* Configure Tx DMA. */
	rtwn_pci_write_4(sc, R92C_BKQ_DESA,
		sc->tx_ring[RTWN_BK_QUEUE].map->dm_segs[0].ds_addr);
	rtwn_pci_write_4(sc, R92C_BEQ_DESA,
		sc->tx_ring[RTWN_BE_QUEUE].map->dm_segs[0].ds_addr);
	rtwn_pci_write_4(sc, R92C_VIQ_DESA,
		sc->tx_ring[RTWN_VI_QUEUE].map->dm_segs[0].ds_addr);
	rtwn_pci_write_4(sc, R92C_VOQ_DESA,
		sc->tx_ring[RTWN_VO_QUEUE].map->dm_segs[0].ds_addr);
	rtwn_pci_write_4(sc, R92C_BCNQ_DESA,
		sc->tx_ring[RTWN_BEACON_QUEUE].map->dm_segs[0].ds_addr);
	rtwn_pci_write_4(sc, R92C_MGQ_DESA,
		sc->tx_ring[RTWN_MGNT_QUEUE].map->dm_segs[0].ds_addr);
	rtwn_pci_write_4(sc, R92C_HQ_DESA,
		sc->tx_ring[RTWN_HIGH_QUEUE].map->dm_segs[0].ds_addr);

	/* Configure Rx DMA. */
	rtwn_pci_write_4(sc, R92C_RX_DESA, sc->rx_ring.map->dm_segs[0].ds_addr);
	rtwn_pci_write_1(sc, R92C_PCIE_CTRL_REG+1, 0);

	/* Set Tx/Rx transfer page boundary. */
	rtwn_pci_write_2(sc, R92C_TRXFF_BNDY + 2, dmasize - 1);

	/* Set Tx/Rx transfer page size. */
	rtwn_pci_write_1(sc, R92C_PBP,
	    SM(R92C_PBP_PSRX, R92C_PBP_128) |
	    SM(R92C_PBP_PSTX, R92C_PBP_128));

	return (0);
}

int
rtwn_fw_loadpage(void *cookie, int page, uint8_t *buf, int len)
{
	struct rtwn_pci_softc *sc = cookie;
	uint32_t reg;
	int off, mlen, error = 0, i;

	reg = rtwn_pci_read_4(sc, R92C_MCUFWDL);
	reg = RW(reg, R92C_MCUFWDL_PAGE, page);
	rtwn_pci_write_4(sc, R92C_MCUFWDL, reg);

	DELAY(5);

	off = R92C_FW_START_ADDR;
	while (len > 0) {
		if (len > 196)
			mlen = 196;
		else if (len > 4)
			mlen = 4;
		else
			mlen = 1;
		for (i = 0; i < mlen; i++)
			rtwn_pci_write_1(sc, off++, buf[i]);
		buf += mlen;
		len -= mlen;
	}

	return (error);
}

int
rtwn_pci_load_firmware(void *cookie, u_char **fw, size_t *len)
{
	struct rtwn_pci_softc *sc = cookie;
	const char *name;
	int error;

	if (sc->sc_sc.chip & RTWN_CHIP_88E)
		name = "rtwn-rtl8188e";
	else if (sc->sc_sc.chip & RTWN_CHIP_23A) {
		if (sc->sc_sc.chip & RTWN_CHIP_UMC_A_CUT)
			name = "rtwn-rtl8723";
		else
			name = "rtwn-rtl8723_B";
	} else if ((sc->sc_sc.chip & (RTWN_CHIP_UMC_A_CUT | RTWN_CHIP_92C)) ==
	    RTWN_CHIP_UMC_A_CUT)
		name = "rtwn-rtl8192cU";
	else
		name = "rtwn-rtl8192cU_B";

	error = loadfirmware(name, fw, len);
	if (error)
		printf("%s: could not read firmware %s (error %d)\n",
		    sc->sc_dev.dv_xname, name, error);
	return (error);
}

void
rtwn_mac_init(void *cookie)
{
	struct rtwn_pci_softc *sc = cookie;
	int i;

	/* Write MAC initialization values. */
	if (sc->sc_sc.chip & RTWN_CHIP_88E) {
		for (i = 0; i < nitems(rtl8188eu_mac); i++) {
			if (rtl8188eu_mac[i].reg == R92C_GPIO_MUXCFG)
				continue;
			rtwn_pci_write_1(sc, rtl8188eu_mac[i].reg,
			    rtl8188eu_mac[i].val);
		}
		rtwn_pci_write_1(sc, R92C_MAX_AGGR_NUM, 0x07);
	} else if (sc->sc_sc.chip & RTWN_CHIP_23A) {
		for (i = 0; i < nitems(rtl8192cu_mac); i++) {
			rtwn_pci_write_1(sc, rtl8192cu_mac[i].reg,
			    rtl8192cu_mac[i].val);
		}
		rtwn_pci_write_1(sc, R92C_MAX_AGGR_NUM, 0x0a);
	} else {
		for (i = 0; i < nitems(rtl8192ce_mac); i++)
			rtwn_pci_write_1(sc, rtl8192ce_mac[i].reg,
			    rtl8192ce_mac[i].val);
	}
}

void
rtwn_bb_init(void *cookie)
{
	struct rtwn_pci_softc *sc = cookie;
	const struct r92c_bb_prog *prog;
	uint32_t reg;
	int i;

	/* Enable BB and RF. */
	rtwn_pci_write_2(sc, R92C_SYS_FUNC_EN,
	    rtwn_pci_read_2(sc, R92C_SYS_FUNC_EN) |
	    R92C_SYS_FUNC_EN_BBRSTB | R92C_SYS_FUNC_EN_BB_GLB_RST |
	    R92C_SYS_FUNC_EN_DIO_RF);

	if (!(sc->sc_sc.chip & RTWN_CHIP_88E))
		rtwn_pci_write_2(sc, R92C_AFE_PLL_CTRL, 0xdb83);

	rtwn_pci_write_1(sc, R92C_RF_CTRL,
	    R92C_RF_CTRL_EN | R92C_RF_CTRL_RSTB | R92C_RF_CTRL_SDMRSTB);

	rtwn_pci_write_1(sc, R92C_SYS_FUNC_EN,
	    R92C_SYS_FUNC_EN_DIO_PCIE | R92C_SYS_FUNC_EN_PCIEA |
	    R92C_SYS_FUNC_EN_PPLL | R92C_SYS_FUNC_EN_BB_GLB_RST |
	    R92C_SYS_FUNC_EN_BBRSTB);

	if (!(sc->sc_sc.chip & RTWN_CHIP_88E)) {
		rtwn_pci_write_1(sc, R92C_AFE_XTAL_CTRL + 1, 0x80);
	}

	rtwn_pci_write_4(sc, R92C_LEDCFG0,
	    rtwn_pci_read_4(sc, R92C_LEDCFG0) | 0x00800000);

	/* Select BB programming. */
	if (sc->sc_sc.chip & RTWN_CHIP_88E)
		prog = &rtl8188eu_bb_prog;
	else if (sc->sc_sc.chip & RTWN_CHIP_23A)
		prog = &rtl8723a_bb_prog;
	else if (!(sc->sc_sc.chip & RTWN_CHIP_92C))
		prog = &rtl8192ce_bb_prog_1t;
	else
		prog = &rtl8192ce_bb_prog_2t;

	/* Write BB initialization values. */
	for (i = 0; i < prog->count; i++) {
		rtwn_bb_write(sc, prog->regs[i], prog->vals[i]);
		DELAY(1);
	}

	if (sc->sc_sc.chip & RTWN_CHIP_92C_1T2R) {
		/* 8192C 1T only configuration. */
		reg = rtwn_bb_read(sc, R92C_FPGA0_TXINFO);
		reg = (reg & ~0x00000003) | 0x2;
		rtwn_bb_write(sc, R92C_FPGA0_TXINFO, reg);

		reg = rtwn_bb_read(sc, R92C_FPGA1_TXINFO);
		reg = (reg & ~0x00300033) | 0x00200022;
		rtwn_bb_write(sc, R92C_FPGA1_TXINFO, reg);

		reg = rtwn_bb_read(sc, R92C_CCK0_AFESETTING);
		reg = (reg & ~0xff000000) | 0x45 << 24;
		rtwn_bb_write(sc, R92C_CCK0_AFESETTING, reg);

		reg = rtwn_bb_read(sc, R92C_OFDM0_TRXPATHENA);
		reg = (reg & ~0x000000ff) | 0x23;
		rtwn_bb_write(sc, R92C_OFDM0_TRXPATHENA, reg);

		reg = rtwn_bb_read(sc, R92C_OFDM0_AGCPARAM1);
		reg = (reg & ~0x00000030) | 1 << 4;
		rtwn_bb_write(sc, R92C_OFDM0_AGCPARAM1, reg);

		reg = rtwn_bb_read(sc, 0xe74);
		reg = (reg & ~0x0c000000) | 2 << 26;
		rtwn_bb_write(sc, 0xe74, reg);
		reg = rtwn_bb_read(sc, 0xe78);
		reg = (reg & ~0x0c000000) | 2 << 26;
		rtwn_bb_write(sc, 0xe78, reg);
		reg = rtwn_bb_read(sc, 0xe7c);
		reg = (reg & ~0x0c000000) | 2 << 26;
		rtwn_bb_write(sc, 0xe7c, reg);
		reg = rtwn_bb_read(sc, 0xe80);
		reg = (reg & ~0x0c000000) | 2 << 26;
		rtwn_bb_write(sc, 0xe80, reg);
		reg = rtwn_bb_read(sc, 0xe88);
		reg = (reg & ~0x0c000000) | 2 << 26;
		rtwn_bb_write(sc, 0xe88, reg);
	}

	/* Write AGC values. */
	for (i = 0; i < prog->agccount; i++) {
		rtwn_bb_write(sc, R92C_OFDM0_AGCRSSITABLE,
		    prog->agcvals[i]);
		DELAY(1);
	}

	if (rtwn_bb_read(sc, R92C_HSSI_PARAM2(0)) & R92C_HSSI_PARAM2_CCK_HIPWR)
		sc->sc_sc.sc_flags |= RTWN_FLAG_CCK_HIPWR;
}

void
rtwn_calib_to(void *arg)
{
	struct rtwn_pci_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;
	int s;

	s = splnet();
	ieee80211_amrr_choose(&sc->amrr, ic->ic_bss, &sc->amn);
	splx(s);

	rtwn_calib(&sc->sc_sc);
}

void
rtwn_next_calib(void *cookie)
{
	struct rtwn_pci_softc *sc = cookie;

	timeout_add_sec(&sc->calib_to, 2);
}

void
rtwn_cancel_calib(void *cookie)
{
	struct rtwn_pci_softc *sc = cookie;

	if (timeout_initialized(&sc->calib_to))
		timeout_del(&sc->calib_to);
}

void
rtwn_scan_to(void *arg)
{
	struct rtwn_pci_softc *sc = arg;

	rtwn_next_scan(&sc->sc_sc);
}

void
rtwn_pci_next_scan(void *cookie)
{
	struct rtwn_pci_softc *sc = cookie;

	timeout_add_msec(&sc->scan_to, 200);
}

void
rtwn_cancel_scan(void *cookie)
{
	struct rtwn_pci_softc *sc = cookie;

	if (timeout_initialized(&sc->scan_to))
		timeout_del(&sc->scan_to);
}

void
rtwn_wait_async(void *cookie)
{
	/* nothing to do */
}

void
rtwn_tx_report(struct rtwn_pci_softc *sc, uint8_t *buf, int len)
{
	struct r92c_c2h_tx_rpt *rpt = (struct r92c_c2h_tx_rpt *)buf;
	int packets, tries, tx_ok, drop, expire, over;

	if (len != sizeof(*rpt))
		return;

	if (sc->sc_sc.chip & RTWN_CHIP_23A) {
		struct r88e_tx_rpt_ccx *rxstat = (struct r88e_tx_rpt_ccx *)buf;

		/*
		 * we seem to get some garbage reports, so check macid makes
		 * sense.
		 */
		if (MS(rxstat->rptb1, R88E_RPTB1_MACID) != R92C_MACID_BSS) {
			return;
		}

		packets = 1;
		tx_ok = (rxstat->rptb1 & R88E_RPTB1_PKT_OK) ? 1 : 0;
		tries = MS(rxstat->rptb2, R88E_RPTB2_RETRY_CNT);
		expire = (rxstat->rptb2 & R88E_RPTB2_LIFE_EXPIRE);
		over = (rxstat->rptb2 & R88E_RPTB2_RETRY_OVER);
		drop = 0;
	} else {
		packets = MS(rpt->rptb6, R92C_RPTB6_RPT_PKT_NUM);
		tries = MS(rpt->rptb0, R92C_RPTB0_RETRY_CNT);
		tx_ok = (rpt->rptb7 & R92C_RPTB7_PKT_OK);
		drop = (rpt->rptb6 & R92C_RPTB6_PKT_DROP);
		expire = (rpt->rptb6 & R92C_RPTB6_LIFE_EXPIRE);
		over = (rpt->rptb6 & R92C_RPTB6_RETRY_OVER);
	}

	if (packets > 0) {
		sc->amn.amn_txcnt += packets;
		if (!tx_ok || tries > 1 || drop || expire || over)
			sc->amn.amn_retrycnt++;
	}
}

void
rtwn_poll_c2h_events(struct rtwn_pci_softc *sc)
{
	const uint16_t off = R92C_C2HEVT_MSG + sizeof(struct r92c_c2h_evt);
	uint8_t buf[R92C_C2H_MSG_MAX_LEN];
	uint8_t id, len, status;
	int i;

	/* Read current status. */
	status = rtwn_pci_read_1(sc, R92C_C2HEVT_CLEAR);
	if (status == R92C_C2HEVT_HOST_CLOSE)
		return;	/* nothing to do */

	if (status == R92C_C2HEVT_FW_CLOSE) {
		len = rtwn_pci_read_1(sc, R92C_C2HEVT_MSG);
		id = MS(len, R92C_C2H_EVTB0_ID);
		len = MS(len, R92C_C2H_EVTB0_LEN);

		if (id == R92C_C2HEVT_TX_REPORT && len <= sizeof(buf)) {
			memset(buf, 0, sizeof(buf));
			for (i = 0; i < len; i++)
				buf[i] = rtwn_pci_read_1(sc, off + i);
			rtwn_tx_report(sc, buf, len);
		} else
			DPRINTF(("unhandled C2H event %d (%d bytes)\n",
			    id, len));
	}

	/* Prepare for next event. */
	rtwn_pci_write_1(sc, R92C_C2HEVT_CLEAR, R92C_C2HEVT_HOST_CLOSE);
}
