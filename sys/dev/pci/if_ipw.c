/*	$OpenBSD: if_ipw.c,v 1.134 2024/04/14 03:26:25 jsg Exp $	*/

/*-
 * Copyright (c) 2004-2008
 *      Damien Bergamini <damien.bergamini@free.fr>. All rights reserved.
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
 * Driver for Intel PRO/Wireless 2100 802.11 network adapters.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/task.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/rwlock.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/endian.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/pci/if_ipwreg.h>
#include <dev/pci/if_ipwvar.h>

int		ipw_match(struct device *, void *, void *);
void		ipw_attach(struct device *, struct device *, void *);
int		ipw_activate(struct device *, int);
void		ipw_wakeup(struct ipw_softc *);
int		ipw_dma_alloc(struct ipw_softc *);
void		ipw_release(struct ipw_softc *);
int		ipw_media_change(struct ifnet *);
void		ipw_media_status(struct ifnet *, struct ifmediareq *);
int		ipw_newstate(struct ieee80211com *, enum ieee80211_state, int);
uint16_t	ipw_read_prom_word(struct ipw_softc *, uint8_t);
void		ipw_command_intr(struct ipw_softc *, struct ipw_soft_buf *);
void		ipw_newstate_intr(struct ipw_softc *, struct ipw_soft_buf *);
void		ipw_data_intr(struct ipw_softc *, struct ipw_status *,
		    struct ipw_soft_bd *, struct ipw_soft_buf *,
		    struct mbuf_list *);
void		ipw_notification_intr(struct ipw_softc *,
		    struct ipw_soft_buf *);
void		ipw_rx_intr(struct ipw_softc *);
void		ipw_release_sbd(struct ipw_softc *, struct ipw_soft_bd *);
void		ipw_tx_intr(struct ipw_softc *);
int		ipw_intr(void *);
int		ipw_cmd(struct ipw_softc *, uint32_t, void *, uint32_t);
int		ipw_send_mgmt(struct ieee80211com *, struct ieee80211_node *,
		    int, int, int);
int		ipw_tx_start(struct ifnet *, struct mbuf *,
		    struct ieee80211_node *);
void		ipw_start(struct ifnet *);
void		ipw_watchdog(struct ifnet *);
int		ipw_ioctl(struct ifnet *, u_long, caddr_t);
uint32_t	ipw_read_table1(struct ipw_softc *, uint32_t);
void		ipw_write_table1(struct ipw_softc *, uint32_t, uint32_t);
int		ipw_read_table2(struct ipw_softc *, uint32_t, void *,
		    uint32_t *);
void		ipw_stop_master(struct ipw_softc *);
int		ipw_reset(struct ipw_softc *);
int		ipw_load_ucode(struct ipw_softc *, u_char *, int);
int		ipw_load_firmware(struct ipw_softc *, u_char *, int);
int		ipw_read_firmware(struct ipw_softc *, struct ipw_firmware *);
void		ipw_scan(void *);
void		ipw_auth_and_assoc(void *);
int		ipw_config(struct ipw_softc *);
int		ipw_init(struct ifnet *);
void		ipw_stop(struct ifnet *, int);
void		ipw_read_mem_1(struct ipw_softc *, bus_size_t, uint8_t *,
		    bus_size_t);
void		ipw_write_mem_1(struct ipw_softc *, bus_size_t, uint8_t *,
		    bus_size_t);

static __inline uint8_t
MEM_READ_1(struct ipw_softc *sc, uint32_t addr)
{
	CSR_WRITE_4(sc, IPW_CSR_INDIRECT_ADDR, addr);
	return CSR_READ_1(sc, IPW_CSR_INDIRECT_DATA);
}

static __inline uint32_t
MEM_READ_4(struct ipw_softc *sc, uint32_t addr)
{
	CSR_WRITE_4(sc, IPW_CSR_INDIRECT_ADDR, addr);
	return CSR_READ_4(sc, IPW_CSR_INDIRECT_DATA);
}

#ifdef IPW_DEBUG
#define DPRINTF(x)	do { if (ipw_debug > 0) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (ipw_debug >= (n)) printf x; } while (0)
int ipw_debug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

const struct cfattach ipw_ca = {
	sizeof (struct ipw_softc), ipw_match, ipw_attach, NULL,
	ipw_activate
};

int
ipw_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR (pa->pa_id) == PCI_VENDOR_INTEL &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_PRO_WL_2100)
		return 1;

	return 0;
}

/* Base Address Register */
#define IPW_PCI_BAR0	0x10

void
ipw_attach(struct device *parent, struct device *self, void *aux)
{
	struct ipw_softc *sc = (struct ipw_softc *)self;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct pci_attach_args *pa = aux;
	const char *intrstr;
	bus_space_tag_t memt;
	bus_space_handle_t memh;
	bus_addr_t base;
	pci_intr_handle_t ih;
	pcireg_t data;
	uint16_t val;
	int error, i;

	sc->sc_pct = pa->pa_pc;
	sc->sc_pcitag = pa->pa_tag,

	/* clear device specific PCI configuration register 0x41 */
	data = pci_conf_read(sc->sc_pct, sc->sc_pcitag, 0x40);
	data &= ~0x0000ff00;
	pci_conf_write(sc->sc_pct, sc->sc_pcitag, 0x40, data);

	/* map the register window */
	error = pci_mapreg_map(pa, IPW_PCI_BAR0, PCI_MAPREG_TYPE_MEM |
	    PCI_MAPREG_MEM_TYPE_32BIT, 0, &memt, &memh, &base, &sc->sc_sz, 0);
	if (error != 0) {
		printf(": can't map mem space\n");
		return;
	}

	sc->sc_st = memt;
	sc->sc_sh = memh;
	sc->sc_dmat = pa->pa_dmat;

	/* disable interrupts */
	CSR_WRITE_4(sc, IPW_CSR_INTR_MASK, 0);

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": can't map interrupt\n");
		return;
	}

	intrstr = pci_intr_string(sc->sc_pct, ih);
	sc->sc_ih = pci_intr_establish(sc->sc_pct, ih, IPL_NET, ipw_intr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s", intrstr);

	rw_init(&sc->sc_rwlock, "ipwlock");
	task_set(&sc->sc_scantask, ipw_scan, sc);
	task_set(&sc->sc_authandassoctask, ipw_auth_and_assoc, sc);

	if (ipw_reset(sc) != 0) {
		printf(": could not reset adapter\n");
		return;
	}

	if (ipw_dma_alloc(sc) != 0) {
		printf(": failed to allocate DMA resources\n");
		return;
	}

	ic->ic_phytype = IEEE80211_T_DS;
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */
	ic->ic_state = IEEE80211_S_INIT;

	/* set device capabilities */
	ic->ic_caps =
#ifndef IEEE80211_STA_ONLY
	    IEEE80211_C_IBSS |		/* IBSS mode supported */
#endif
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
	    IEEE80211_C_TXPMGT |	/* tx power management */
	    IEEE80211_C_SHPREAMBLE |	/* short preamble supported */
	    IEEE80211_C_WEP |		/* s/w WEP */
	    IEEE80211_C_RSN |		/* WPA/RSN */
	    IEEE80211_C_SCANALL;	/* h/w scanning */

	/* read MAC address from EEPROM */
	val = ipw_read_prom_word(sc, IPW_EEPROM_MAC + 0);
	ic->ic_myaddr[0] = val >> 8;
	ic->ic_myaddr[1] = val & 0xff;
	val = ipw_read_prom_word(sc, IPW_EEPROM_MAC + 1);
	ic->ic_myaddr[2] = val >> 8;
	ic->ic_myaddr[3] = val & 0xff;
	val = ipw_read_prom_word(sc, IPW_EEPROM_MAC + 2);
	ic->ic_myaddr[4] = val >> 8;
	ic->ic_myaddr[5] = val & 0xff;

	printf(", address %s\n", ether_sprintf(ic->ic_myaddr));

	/* set supported .11b rates */
	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;

	/* set supported .11b channels (1 through 14) */
	for (i = 1; i <= 14; i++) {
		ic->ic_channels[i].ic_freq =
		    ieee80211_ieee2mhz(i, IEEE80211_CHAN_B);
		ic->ic_channels[i].ic_flags = IEEE80211_CHAN_B;
	}

	/* IBSS channel undefined for now */
	ic->ic_ibss_chan = &ic->ic_channels[0];

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = ipw_ioctl;
	ifp->if_start = ipw_start;
	ifp->if_watchdog = ipw_watchdog;
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp);
	/* override state transition machine */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = ipw_newstate;
	ic->ic_send_mgmt = ipw_send_mgmt;
	ieee80211_media_init(ifp, ipw_media_change, ipw_media_status);

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(IPW_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(IPW_TX_RADIOTAP_PRESENT);
#endif
}

int
ipw_activate(struct device *self, int act)
{
	struct ipw_softc *sc = (struct ipw_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			ipw_stop(ifp, 0);
		break;
	case DVACT_WAKEUP:
		ipw_wakeup(sc);
		break;
	}

	return 0;
}

void
ipw_wakeup(struct ipw_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	pcireg_t data;
	int s;

	/* clear device specific PCI configuration register 0x41 */
	data = pci_conf_read(sc->sc_pct, sc->sc_pcitag, 0x40);
	data &= ~0x0000ff00;
	pci_conf_write(sc->sc_pct, sc->sc_pcitag, 0x40, data);

	rw_enter_write(&sc->sc_rwlock);
	s = splnet();

	if (ifp->if_flags & IFF_UP)
		ipw_init(ifp);

	splx(s);
	rw_exit_write(&sc->sc_rwlock);
}

int
ipw_dma_alloc(struct ipw_softc *sc)
{
	struct ipw_soft_bd *sbd;
	struct ipw_soft_hdr *shdr;
	struct ipw_soft_buf *sbuf;
	int i, nsegs, error;

	/*
	 * Allocate and map tx ring.
	 */
	error = bus_dmamap_create(sc->sc_dmat, IPW_TBD_SZ, 1, IPW_TBD_SZ, 0,
	    BUS_DMA_NOWAIT, &sc->tbd_map);
	if (error != 0) {
		printf("%s: could not create tx ring DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, IPW_TBD_SZ, PAGE_SIZE, 0,
	    &sc->tbd_seg, 1, &nsegs, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not allocate tx ring DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->tbd_seg, nsegs, IPW_TBD_SZ,
	    (caddr_t *)&sc->tbd_list, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map tx ring DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, sc->tbd_map, sc->tbd_list,
	    IPW_TBD_SZ, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load tx ring DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/*
	 * Allocate and map rx ring.
	 */
	error = bus_dmamap_create(sc->sc_dmat, IPW_RBD_SZ, 1, IPW_RBD_SZ, 0,
	    BUS_DMA_NOWAIT, &sc->rbd_map);
	if (error != 0) {
		printf("%s: could not create rx ring DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, IPW_RBD_SZ, PAGE_SIZE, 0,
	    &sc->rbd_seg, 1, &nsegs, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not allocate rx ring DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->rbd_seg, nsegs, IPW_RBD_SZ,
	    (caddr_t *)&sc->rbd_list, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map rx ring DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, sc->rbd_map, sc->rbd_list,
	    IPW_RBD_SZ, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load tx ring DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/*
	 * Allocate and map status ring.
	 */
	error = bus_dmamap_create(sc->sc_dmat, IPW_STATUS_SZ, 1, IPW_STATUS_SZ,
	    0, BUS_DMA_NOWAIT, &sc->status_map);
	if (error != 0) {
		printf("%s: could not create status ring DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, IPW_STATUS_SZ, PAGE_SIZE, 0,
	    &sc->status_seg, 1, &nsegs, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not allocate status ring DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->status_seg, nsegs,
	    IPW_STATUS_SZ, (caddr_t *)&sc->status_list, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map status ring DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, sc->status_map, sc->status_list,
	    IPW_STATUS_SZ, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load status ring DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/*
	 * Allocate command DMA map.
	 */
	error = bus_dmamap_create(sc->sc_dmat, sizeof (struct ipw_cmd), 1,
	    sizeof (struct ipw_cmd), 0, BUS_DMA_NOWAIT, &sc->cmd_map);
	if (error != 0) {
		printf("%s: could not create command DMA map\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/*
	 * Allocate headers DMA maps.
	 */
	SLIST_INIT(&sc->free_shdr);
	for (i = 0; i < IPW_NDATA; i++) {
		shdr = &sc->shdr_list[i];
		error = bus_dmamap_create(sc->sc_dmat, sizeof (struct ipw_hdr),
		    1, sizeof (struct ipw_hdr), 0, BUS_DMA_NOWAIT, &shdr->map);
		if (error != 0) {
			printf("%s: could not create header DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
		SLIST_INSERT_HEAD(&sc->free_shdr, shdr, next);
	}

	/*
	 * Allocate tx buffers DMA maps.
	 */
	SLIST_INIT(&sc->free_sbuf);
	for (i = 0; i < IPW_NDATA; i++) {
		sbuf = &sc->tx_sbuf_list[i];
		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, IPW_MAX_NSEG,
		    MCLBYTES, 0, BUS_DMA_NOWAIT, &sbuf->map);
		if (error != 0) {
			printf("%s: could not create tx DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
		SLIST_INSERT_HEAD(&sc->free_sbuf, sbuf, next);
	}

	/*
	 * Initialize tx ring.
	 */
	for (i = 0; i < IPW_NTBD; i++) {
		sbd = &sc->stbd_list[i];
		sbd->bd = &sc->tbd_list[i];
		sbd->type = IPW_SBD_TYPE_NOASSOC;
	}

	/*
	 * Pre-allocate rx buffers and DMA maps.
	 */
	for (i = 0; i < IPW_NRBD; i++) {
		sbd = &sc->srbd_list[i];
		sbuf = &sc->rx_sbuf_list[i];
		sbd->bd = &sc->rbd_list[i];

		MGETHDR(sbuf->m, M_DONTWAIT, MT_DATA);
		if (sbuf->m == NULL) {
			printf("%s: could not allocate rx mbuf\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		MCLGET(sbuf->m, M_DONTWAIT);
		if (!(sbuf->m->m_flags & M_EXT)) {
			m_freem(sbuf->m);
			printf("%s: could not allocate rx mbuf cluster\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}

		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES,
		    0, BUS_DMA_NOWAIT, &sbuf->map);
		if (error != 0) {
			printf("%s: could not create rx DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		error = bus_dmamap_load(sc->sc_dmat, sbuf->map,
		    mtod(sbuf->m, void *), MCLBYTES, NULL, BUS_DMA_NOWAIT);
		if (error != 0) {
			printf("%s: can't map rx DMA memory\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		sbd->type = IPW_SBD_TYPE_DATA;
		sbd->priv = sbuf;
		sbd->bd->physaddr = htole32(sbuf->map->dm_segs[0].ds_addr);
		sbd->bd->len = htole32(MCLBYTES);
	}

	bus_dmamap_sync(sc->sc_dmat, sc->rbd_map, 0, IPW_RBD_SZ,
	    BUS_DMASYNC_PREWRITE);

	return 0;

fail:	ipw_release(sc);
	return error;
}

void
ipw_release(struct ipw_softc *sc)
{
	struct ipw_soft_buf *sbuf;
	int i;

	if (sc->tbd_map != NULL) {
		if (sc->tbd_list != NULL) {
			bus_dmamap_unload(sc->sc_dmat, sc->tbd_map);
			bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->tbd_list,
			    IPW_TBD_SZ);
			bus_dmamem_free(sc->sc_dmat, &sc->tbd_seg, 1);
		}
		bus_dmamap_destroy(sc->sc_dmat, sc->tbd_map);
	}

	if (sc->rbd_map != NULL) {
		if (sc->rbd_list != NULL) {
			bus_dmamap_unload(sc->sc_dmat, sc->rbd_map);
			bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->rbd_list,
			    IPW_RBD_SZ);
			bus_dmamem_free(sc->sc_dmat, &sc->rbd_seg, 1);
		}
		bus_dmamap_destroy(sc->sc_dmat, sc->rbd_map);
	}

	if (sc->status_map != NULL) {
		if (sc->status_list != NULL) {
			bus_dmamap_unload(sc->sc_dmat, sc->status_map);
			bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->status_list,
			    IPW_RBD_SZ);
			bus_dmamem_free(sc->sc_dmat, &sc->status_seg, 1);
		}
		bus_dmamap_destroy(sc->sc_dmat, sc->status_map);
	}

	if (sc->cmd_map != NULL)
		bus_dmamap_destroy(sc->sc_dmat, sc->cmd_map);

	for (i = 0; i < IPW_NDATA; i++)
		bus_dmamap_destroy(sc->sc_dmat, sc->shdr_list[i].map);

	for (i = 0; i < IPW_NDATA; i++)
		bus_dmamap_destroy(sc->sc_dmat, sc->tx_sbuf_list[i].map);

	for (i = 0; i < IPW_NRBD; i++) {
		sbuf = &sc->rx_sbuf_list[i];
		if (sbuf->map != NULL) {
			if (sbuf->m != NULL) {
				bus_dmamap_unload(sc->sc_dmat, sbuf->map);
				m_freem(sbuf->m);
			}
			bus_dmamap_destroy(sc->sc_dmat, sbuf->map);
		}
	}

	task_del(systq, &sc->sc_scantask);
	task_del(systq, &sc->sc_authandassoctask);
}

int
ipw_media_change(struct ifnet *ifp)
{
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		ipw_init(ifp);

	return 0;
}

void
ipw_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct ipw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	static const struct {
		uint32_t	val;
		int		rate;
	} rates[] = {
		{ IPW_RATE_DS1,   2 },
		{ IPW_RATE_DS2,   4 },
		{ IPW_RATE_DS5,  11 },
		{ IPW_RATE_DS11, 22 },
	};
	uint32_t val;
	int rate, i;

	imr->ifm_status = IFM_AVALID;
	imr->ifm_active = IFM_IEEE80211;
	if (ic->ic_state == IEEE80211_S_RUN)
		imr->ifm_status |= IFM_ACTIVE;

	/* read current transmission rate from adapter */
	val = ipw_read_table1(sc, IPW_INFO_CURRENT_TX_RATE);
	val &= 0xf;

	/* convert rate to 802.11 rate */
	for (i = 0; i < nitems(rates) && rates[i].val != val; i++)
		;
	rate = (i < nitems(rates)) ? rates[i].rate : 0;

	imr->ifm_active |= IFM_IEEE80211_11B;
	imr->ifm_active |= ieee80211_rate2media(ic, rate, IEEE80211_MODE_11B);
	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_IBSS:
		imr->ifm_active |= IFM_IEEE80211_IBSS;
		break;
#endif
	case IEEE80211_M_MONITOR:
		imr->ifm_active |= IFM_IEEE80211_MONITOR;
		break;
	default:
		/* should not get there */
		break;
	}
}

int
ipw_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct ipw_softc *sc = ic->ic_softc;
	struct ifnet *ifp = &ic->ic_if;

	if (LINK_STATE_IS_UP(ifp->if_link_state))
		ieee80211_set_link_state(ic, LINK_STATE_DOWN);

	switch (nstate) {
	case IEEE80211_S_SCAN:
		task_add(systq, &sc->sc_scantask);
		break;

	case IEEE80211_S_AUTH:
		task_add(systq, &sc->sc_authandassoctask);
		break;

	case IEEE80211_S_RUN:
		if (!(ic->ic_flags & IEEE80211_F_RSNON)) {
			/*
			 * NB: When RSN is enabled, we defer setting
			 * the link up until the port is valid.
			 */
			ieee80211_set_link_state(ic, LINK_STATE_UP);
		}
		break;
	case IEEE80211_S_INIT:
	case IEEE80211_S_ASSOC:
		/* nothing to do */
		break;
	}

	ic->ic_state = nstate;
	return 0;
}

/*
 * Read 16 bits at address 'addr' from the Microwire EEPROM.
 * DON'T PLAY WITH THIS CODE UNLESS YOU KNOW *EXACTLY* WHAT YOU'RE DOING!
 */
uint16_t
ipw_read_prom_word(struct ipw_softc *sc, uint8_t addr)
{
	uint32_t tmp;
	uint16_t val;
	int n;

	/* clock C once before the first command */
	IPW_EEPROM_CTL(sc, 0);
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S);
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S | IPW_EEPROM_C);
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S);

	/* write start bit (1) */
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S | IPW_EEPROM_D);
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S | IPW_EEPROM_D | IPW_EEPROM_C);

	/* write READ opcode (10) */
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S | IPW_EEPROM_D);
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S | IPW_EEPROM_D | IPW_EEPROM_C);
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S);
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S | IPW_EEPROM_C);

	/* write address A7-A0 */
	for (n = 7; n >= 0; n--) {
		IPW_EEPROM_CTL(sc, IPW_EEPROM_S |
		    (((addr >> n) & 1) << IPW_EEPROM_SHIFT_D));
		IPW_EEPROM_CTL(sc, IPW_EEPROM_S |
		    (((addr >> n) & 1) << IPW_EEPROM_SHIFT_D) | IPW_EEPROM_C);
	}

	IPW_EEPROM_CTL(sc, IPW_EEPROM_S);

	/* read data Q15-Q0 */
	val = 0;
	for (n = 15; n >= 0; n--) {
		IPW_EEPROM_CTL(sc, IPW_EEPROM_S | IPW_EEPROM_C);
		IPW_EEPROM_CTL(sc, IPW_EEPROM_S);
		tmp = MEM_READ_4(sc, IPW_MEM_EEPROM_CTL);
		val |= ((tmp & IPW_EEPROM_Q) >> IPW_EEPROM_SHIFT_Q) << n;
	}

	IPW_EEPROM_CTL(sc, 0);

	/* clear Chip Select and clock C */
	IPW_EEPROM_CTL(sc, IPW_EEPROM_S);
	IPW_EEPROM_CTL(sc, 0);
	IPW_EEPROM_CTL(sc, IPW_EEPROM_C);

	return val;
}

void
ipw_command_intr(struct ipw_softc *sc, struct ipw_soft_buf *sbuf)
{
	struct ipw_cmd *cmd;

	bus_dmamap_sync(sc->sc_dmat, sbuf->map, 0, sizeof (struct ipw_cmd),
	    BUS_DMASYNC_POSTREAD);

	cmd = mtod(sbuf->m, struct ipw_cmd *);

	DPRINTFN(2, ("received command ack type=%u,status=%u\n",
	    letoh32(cmd->type), letoh32(cmd->status)));

	wakeup(&sc->cmd);
}

void
ipw_newstate_intr(struct ipw_softc *sc, struct ipw_soft_buf *sbuf)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	uint32_t state;

	bus_dmamap_sync(sc->sc_dmat, sbuf->map, 0, sizeof state,
	    BUS_DMASYNC_POSTREAD);

	state = letoh32(*mtod(sbuf->m, uint32_t *));

	DPRINTFN(2, ("firmware state changed to 0x%x\n", state));

	switch (state) {
	case IPW_STATE_ASSOCIATED:
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
		break;

	case IPW_STATE_SCANNING:
		if (ic->ic_state == IEEE80211_S_RUN)
			ieee80211_begin_scan(ifp);
		break;

	case IPW_STATE_SCAN_COMPLETE:
		if (ic->ic_state == IEEE80211_S_SCAN)
			ieee80211_end_scan(ifp);
		break;

	case IPW_STATE_ASSOCIATION_LOST:
		ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
		break;

	case IPW_STATE_DISABLED:
		wakeup(sc);
		break;

	case IPW_STATE_RADIO_DISABLED:
		ipw_stop(&ic->ic_if, 1);
		break;
	}
}

void
ipw_data_intr(struct ipw_softc *sc, struct ipw_status *status,
    struct ipw_soft_bd *sbd, struct ipw_soft_buf *sbuf, struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct mbuf *mnew, *m;
	struct ieee80211_frame *wh;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_node *ni;
	int error;

	DPRINTFN(5, ("received data frame len=%u,rssi=%u\n",
	    letoh32(status->len), status->rssi));

	/*
	 * Try to allocate a new mbuf for this ring element and load it before
	 * processing the current mbuf.  If the ring element cannot be loaded,
	 * drop the received packet and reuse the old mbuf.  In the unlikely
	 * case that the old mbuf can't be reloaded either, explicitly panic.
	 */
	MGETHDR(mnew, M_DONTWAIT, MT_DATA);
	if (mnew == NULL) {
		ifp->if_ierrors++;
		return;
	}
	MCLGET(mnew, M_DONTWAIT);
	if (!(mnew->m_flags & M_EXT)) {
		m_freem(mnew);
		ifp->if_ierrors++;
		return;
	}

	bus_dmamap_sync(sc->sc_dmat, sbuf->map, 0, letoh32(status->len),
	    BUS_DMASYNC_POSTREAD);
	bus_dmamap_unload(sc->sc_dmat, sbuf->map);

	error = bus_dmamap_load(sc->sc_dmat, sbuf->map, mtod(mnew, void *),
	    MCLBYTES, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		m_freem(mnew);

		/* try to reload the old mbuf */
		error = bus_dmamap_load(sc->sc_dmat, sbuf->map,
		    mtod(sbuf->m, void *), MCLBYTES, NULL, BUS_DMA_NOWAIT);
		if (error != 0) {
			/* very unlikely that it will fail... */
			panic("%s: could not load old rx mbuf",
			    sc->sc_dev.dv_xname);
		}
		sbd->bd->physaddr = htole32(sbuf->map->dm_segs[0].ds_addr);
		ifp->if_ierrors++;
		return;
	}

	m = sbuf->m;
	sbuf->m = mnew;	
	sbd->bd->physaddr = htole32(sbuf->map->dm_segs[0].ds_addr);

	/* finalize mbuf */
	m->m_pkthdr.len = m->m_len = letoh32(status->len);

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct ipw_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = 0;
		tap->wr_antsignal = status->rssi;
		tap->wr_chan_freq = htole16(ic->ic_ibss_chan->ic_freq);
		tap->wr_chan_flags = htole16(ic->ic_ibss_chan->ic_flags);

		bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_rxtap_len,
		    m, BPF_DIRECTION_IN);
	}
#endif

	wh = mtod(m, struct ieee80211_frame *);
	ni = ieee80211_find_rxnode(ic, wh);

	/* send the frame to the upper layer */
	memset(&rxi, 0, sizeof(rxi));
	rxi.rxi_rssi = status->rssi;
	ieee80211_inputm(ifp, m, ni, &rxi, ml);

	ieee80211_release_node(ic, ni);
}

void
ipw_notification_intr(struct ipw_softc *sc, struct ipw_soft_buf *sbuf)
{
	DPRINTFN(2, ("received notification\n"));
}

void
ipw_rx_intr(struct ipw_softc *sc)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct ipw_status *status;
	struct ipw_soft_bd *sbd;
	struct ipw_soft_buf *sbuf;
	uint32_t r, i;

	r = CSR_READ_4(sc, IPW_CSR_RX_READ_INDEX);

	for (i = (sc->rxcur + 1) % IPW_NRBD; i != r; i = (i + 1) % IPW_NRBD) {

		bus_dmamap_sync(sc->sc_dmat, sc->rbd_map,
		    i * sizeof (struct ipw_bd), sizeof (struct ipw_bd),
		    BUS_DMASYNC_POSTREAD);

		bus_dmamap_sync(sc->sc_dmat, sc->status_map,
		    i * sizeof (struct ipw_status), sizeof (struct ipw_status),
		    BUS_DMASYNC_POSTREAD);

		status = &sc->status_list[i];
		sbd = &sc->srbd_list[i];
		sbuf = sbd->priv;

		switch (letoh16(status->code) & 0xf) {
		case IPW_STATUS_CODE_COMMAND:
			ipw_command_intr(sc, sbuf);
			break;

		case IPW_STATUS_CODE_NEWSTATE:
			ipw_newstate_intr(sc, sbuf);
			break;

		case IPW_STATUS_CODE_DATA_802_3:
		case IPW_STATUS_CODE_DATA_802_11:
			ipw_data_intr(sc, status, sbd, sbuf, &ml);
			break;

		case IPW_STATUS_CODE_NOTIFICATION:
			ipw_notification_intr(sc, sbuf);
			break;

		default:
			printf("%s: unknown status code %u\n",
			    sc->sc_dev.dv_xname, letoh16(status->code));
		}
		sbd->bd->flags = 0;

		bus_dmamap_sync(sc->sc_dmat, sc->rbd_map,
		    i * sizeof (struct ipw_bd), sizeof (struct ipw_bd),
		    BUS_DMASYNC_PREWRITE);
	}
	if_input(&sc->sc_ic.ic_if, &ml);

	/* tell the firmware what we have processed */
	sc->rxcur = (r == 0) ? IPW_NRBD - 1 : r - 1;
	CSR_WRITE_4(sc, IPW_CSR_RX_WRITE_INDEX, sc->rxcur);
}

void
ipw_release_sbd(struct ipw_softc *sc, struct ipw_soft_bd *sbd)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ipw_soft_hdr *shdr;
	struct ipw_soft_buf *sbuf;

	switch (sbd->type) {
	case IPW_SBD_TYPE_COMMAND:
		bus_dmamap_unload(sc->sc_dmat, sc->cmd_map);
		break;

	case IPW_SBD_TYPE_HEADER:
		shdr = sbd->priv;
		bus_dmamap_unload(sc->sc_dmat, shdr->map);
		SLIST_INSERT_HEAD(&sc->free_shdr, shdr, next);
		break;

	case IPW_SBD_TYPE_DATA:
		sbuf = sbd->priv;
		bus_dmamap_unload(sc->sc_dmat, sbuf->map);
		SLIST_INSERT_HEAD(&sc->free_sbuf, sbuf, next);

		m_freem(sbuf->m);

		if (sbuf->ni != NULL)
			ieee80211_release_node(ic, sbuf->ni);

		/* kill watchdog timer */
		sc->sc_tx_timer = 0;
		break;
	}
	sbd->type = IPW_SBD_TYPE_NOASSOC;
}

void
ipw_tx_intr(struct ipw_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct ipw_soft_bd *sbd;
	uint32_t r, i;

	r = CSR_READ_4(sc, IPW_CSR_TX_READ_INDEX);

	for (i = (sc->txold + 1) % IPW_NTBD; i != r; i = (i + 1) % IPW_NTBD) {
		sbd = &sc->stbd_list[i];

		ipw_release_sbd(sc, sbd);
		sc->txfree++;
	}

	/* remember what the firmware has processed */
	sc->txold = (r == 0) ? IPW_NTBD - 1 : r - 1;

	/* call start() since some buffer descriptors have been released */
	ifq_clr_oactive(&ifp->if_snd);
	(*ifp->if_start)(ifp);
}

int
ipw_intr(void *arg)
{
	struct ipw_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t r;

	if ((r = CSR_READ_4(sc, IPW_CSR_INTR)) == 0 || r == 0xffffffff)
		return 0;

	/* disable interrupts */
	CSR_WRITE_4(sc, IPW_CSR_INTR_MASK, 0);

	if (r & (IPW_INTR_FATAL_ERROR | IPW_INTR_PARITY_ERROR)) {
		printf("%s: fatal firmware error\n", sc->sc_dev.dv_xname);
		ipw_stop(ifp, 1);
		return 1;
	}

	if (r & IPW_INTR_FW_INIT_DONE)
		wakeup(sc);

	if (r & IPW_INTR_RX_TRANSFER)
		ipw_rx_intr(sc);

	if (r & IPW_INTR_TX_TRANSFER)
		ipw_tx_intr(sc);

	/* acknowledge interrupts */
	CSR_WRITE_4(sc, IPW_CSR_INTR, r);

	/* re-enable interrupts */
	CSR_WRITE_4(sc, IPW_CSR_INTR_MASK, IPW_INTR_MASK);

	return 1;
}

int
ipw_cmd(struct ipw_softc *sc, uint32_t type, void *data, uint32_t len)
{
	struct ipw_soft_bd *sbd;
	int s, error;

	s = splnet();

	sc->cmd.type = htole32(type);
	sc->cmd.subtype = htole32(0);
	sc->cmd.len = htole32(len);
	sc->cmd.seq = htole32(0);
	if (data != NULL)
		bcopy(data, sc->cmd.data, len);

	error = bus_dmamap_load(sc->sc_dmat, sc->cmd_map, &sc->cmd,
	    sizeof (struct ipw_cmd), NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map command DMA memory\n",
		    sc->sc_dev.dv_xname);
		splx(s);
		return error;
	}

	sbd = &sc->stbd_list[sc->txcur];
	sbd->type = IPW_SBD_TYPE_COMMAND;
	sbd->bd->physaddr = htole32(sc->cmd_map->dm_segs[0].ds_addr);
	sbd->bd->len = htole32(sizeof (struct ipw_cmd));
	sbd->bd->nfrag = 1;
	sbd->bd->flags = IPW_BD_FLAG_TX_FRAME_COMMAND |
	    IPW_BD_FLAG_TX_LAST_FRAGMENT;

	bus_dmamap_sync(sc->sc_dmat, sc->cmd_map, 0, sizeof (struct ipw_cmd),
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, sc->tbd_map,
	    sc->txcur * sizeof (struct ipw_bd), sizeof (struct ipw_bd),
	    BUS_DMASYNC_PREWRITE);

	sc->txcur = (sc->txcur + 1) % IPW_NTBD;
	sc->txfree--;
	CSR_WRITE_4(sc, IPW_CSR_TX_WRITE_INDEX, sc->txcur);

	DPRINTFN(2, ("sending command type=%u,len=%u\n", type, len));

	/* wait at most one second for command to complete */
	error = tsleep_nsec(&sc->cmd, 0, "ipwcmd", SEC_TO_NSEC(1));
	splx(s);

	return error;
}

int
ipw_send_mgmt(struct ieee80211com *ic, struct ieee80211_node *ni, int type,
    int arg1, int arg2)
{
	return EOPNOTSUPP;
}

int
ipw_tx_start(struct ifnet *ifp, struct mbuf *m, struct ieee80211_node *ni)
{
	struct ipw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	struct ipw_soft_bd *sbd;
	struct ipw_soft_hdr *shdr;
	struct ipw_soft_buf *sbuf;
	int error, i;

	wh = mtod(m, struct ieee80211_frame *);

	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_get_txkey(ic, wh, ni);

		if ((m = ieee80211_encrypt(ic, m, k)) == NULL)
			return ENOBUFS;

		/* packet header may have moved, reset our local pointer */
		wh = mtod(m, struct ieee80211_frame *);
	}

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct ipw_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_chan_freq = htole16(ic->ic_ibss_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_ibss_chan->ic_flags);

		bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_txtap_len,
		    m, BPF_DIRECTION_OUT);
	}
#endif

	shdr = SLIST_FIRST(&sc->free_shdr);
	sbuf = SLIST_FIRST(&sc->free_sbuf);

	shdr->hdr.type = htole32(IPW_HDR_TYPE_SEND);
	shdr->hdr.subtype = htole32(0);
	shdr->hdr.encrypted = (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) ? 1 : 0;
	shdr->hdr.encrypt = 0;
	shdr->hdr.keyidx = 0;
	shdr->hdr.keysz = 0;
	shdr->hdr.fragmentsz = htole16(0);
	IEEE80211_ADDR_COPY(shdr->hdr.src_addr, wh->i_addr2);
	if (ic->ic_opmode == IEEE80211_M_STA)
		IEEE80211_ADDR_COPY(shdr->hdr.dst_addr, wh->i_addr3);
	else
		IEEE80211_ADDR_COPY(shdr->hdr.dst_addr, wh->i_addr1);

	/* trim IEEE802.11 header */
	m_adj(m, sizeof (struct ieee80211_frame));

	error = bus_dmamap_load_mbuf(sc->sc_dmat, sbuf->map, m, BUS_DMA_NOWAIT);
	if (error != 0 && error != EFBIG) {
		printf("%s: can't map mbuf (error %d)\n",
		    sc->sc_dev.dv_xname, error);
		m_freem(m);
		return error;
	}
	if (error != 0) {
		/* too many fragments, linearize */
		if (m_defrag(m, M_DONTWAIT)) {
			m_freem(m);
			return ENOBUFS;
		}
		error = bus_dmamap_load_mbuf(sc->sc_dmat, sbuf->map, m,
		    BUS_DMA_NOWAIT);
		if (error != 0) {
			printf("%s: can't map mbuf (error %d)\n",
			    sc->sc_dev.dv_xname, error);
			m_freem(m);
			return error;
		}
	}

	error = bus_dmamap_load(sc->sc_dmat, shdr->map, &shdr->hdr,
	    sizeof (struct ipw_hdr), NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: can't map header DMA memory (error %d)\n",
		    sc->sc_dev.dv_xname, error);
		bus_dmamap_unload(sc->sc_dmat, sbuf->map);
		m_freem(m);
		return error;
	}

	SLIST_REMOVE_HEAD(&sc->free_sbuf, next);
	SLIST_REMOVE_HEAD(&sc->free_shdr, next);

	sbd = &sc->stbd_list[sc->txcur];
	sbd->type = IPW_SBD_TYPE_HEADER;
	sbd->priv = shdr;
	sbd->bd->physaddr = htole32(shdr->map->dm_segs[0].ds_addr);
	sbd->bd->len = htole32(sizeof (struct ipw_hdr));
	sbd->bd->nfrag = 1 + sbuf->map->dm_nsegs;
	sbd->bd->flags = IPW_BD_FLAG_TX_FRAME_802_3 |
	    IPW_BD_FLAG_TX_NOT_LAST_FRAGMENT;

	bus_dmamap_sync(sc->sc_dmat, sc->tbd_map,
	    sc->txcur * sizeof (struct ipw_bd),
	    sizeof (struct ipw_bd), BUS_DMASYNC_PREWRITE);

	sc->txcur = (sc->txcur + 1) % IPW_NTBD;
	sc->txfree--;

	sbuf->m = m;
	sbuf->ni = ni;

	for (i = 0; i < sbuf->map->dm_nsegs; i++) {
		sbd = &sc->stbd_list[sc->txcur];
		sbd->bd->physaddr = htole32(sbuf->map->dm_segs[i].ds_addr);
		sbd->bd->len = htole32(sbuf->map->dm_segs[i].ds_len);
		sbd->bd->nfrag = 0;	/* used only in first bd */
		sbd->bd->flags = IPW_BD_FLAG_TX_FRAME_802_3;
		if (i == sbuf->map->dm_nsegs - 1) {
			sbd->type = IPW_SBD_TYPE_DATA;
			sbd->priv = sbuf;
			sbd->bd->flags |= IPW_BD_FLAG_TX_LAST_FRAGMENT;
		} else {
			sbd->type = IPW_SBD_TYPE_NOASSOC;
			sbd->bd->flags |= IPW_BD_FLAG_TX_NOT_LAST_FRAGMENT;
		}

		bus_dmamap_sync(sc->sc_dmat, sc->tbd_map,
		    sc->txcur * sizeof (struct ipw_bd),
		    sizeof (struct ipw_bd), BUS_DMASYNC_PREWRITE);

		sc->txcur = (sc->txcur + 1) % IPW_NTBD;
		sc->txfree--;
	}

	bus_dmamap_sync(sc->sc_dmat, sbuf->map, 0, sbuf->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, shdr->map, 0, sizeof (struct ipw_hdr),
	    BUS_DMASYNC_PREWRITE);

	/* inform firmware about this new packet */
	CSR_WRITE_4(sc, IPW_CSR_TX_WRITE_INDEX, sc->txcur);

	return 0;
}

void
ipw_start(struct ifnet *ifp)
{
	struct ipw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct mbuf *m;

	if (ic->ic_state != IEEE80211_S_RUN)
		return;

	for (;;) {
		if (sc->txfree < 1 + IPW_MAX_NSEG) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;

#if NBPFILTER > 0
		if (ifp->if_bpf != NULL)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		m = ieee80211_encap(ifp, m, &ni);
		if (m == NULL)
			continue;
#if NBPFILTER > 0
		if (ic->ic_rawbpf != NULL)
			bpf_mtap(ic->ic_rawbpf, m, BPF_DIRECTION_OUT);
#endif
		if (ipw_tx_start(ifp, m, ni) != 0) {
			if (ni != NULL)
				ieee80211_release_node(ic, ni);
			ifp->if_oerrors++;
			break;
		}

		/* start watchdog timer */
		sc->sc_tx_timer = 5;
		ifp->if_timer = 1;
	}
}

void
ipw_watchdog(struct ifnet *ifp)
{
	struct ipw_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			printf("%s: device timeout\n", sc->sc_dev.dv_xname);
			ipw_stop(ifp, 1);
			ifp->if_oerrors++;
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}

int
ipw_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ipw_softc *sc = ifp->if_softc;
	int s, error = 0;

	error = rw_enter(&sc->sc_rwlock, RW_WRITE | RW_INTR);
	if (error)
		return error;
	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (!(ifp->if_flags & IFF_RUNNING))
				ipw_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				ipw_stop(ifp, 1);
		}
		break;

	case SIOCG80211TXPOWER:
		/*
		 * If the hardware radio transmitter switch is off, report a
		 * tx power of IEEE80211_TXPOWER_MIN to indicate that radio
		 * transmitter is killed.
		 */
		((struct ieee80211_txpower *)data)->i_val =
		    (CSR_READ_4(sc, IPW_CSR_IO) & IPW_IO_RADIO_DISABLED) ?
		    IEEE80211_TXPOWER_MIN : sc->sc_ic.ic_txpower;
		break;

	default:
		error = ieee80211_ioctl(ifp, cmd, data);
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			ipw_init(ifp);
		error = 0;
	}

	splx(s);
	rw_exit_write(&sc->sc_rwlock);
	return error;
}

uint32_t
ipw_read_table1(struct ipw_softc *sc, uint32_t off)
{
	return MEM_READ_4(sc, MEM_READ_4(sc, sc->table1_base + off));
}

void
ipw_write_table1(struct ipw_softc *sc, uint32_t off, uint32_t info)
{
	MEM_WRITE_4(sc, MEM_READ_4(sc, sc->table1_base + off), info);
}

int
ipw_read_table2(struct ipw_softc *sc, uint32_t off, void *buf, uint32_t *len)
{
	uint32_t addr, info;
	uint16_t count, size;
	uint32_t total;

	/* addr[4] + count[2] + size[2] */
	addr = MEM_READ_4(sc, sc->table2_base + off);
	info = MEM_READ_4(sc, sc->table2_base + off + 4);

	count = info >> 16;
	size  = info & 0xffff;
	total = count * size;

	if (total > *len) {
		*len = total;
		return EINVAL;
	}
	*len = total;
	ipw_read_mem_1(sc, addr, buf, total);

	return 0;
}

void
ipw_stop_master(struct ipw_softc *sc)
{
	uint32_t tmp;
	int ntries;

	/* disable interrupts */
	CSR_WRITE_4(sc, IPW_CSR_INTR_MASK, 0);

	CSR_WRITE_4(sc, IPW_CSR_RST, IPW_RST_STOP_MASTER);
	for (ntries = 0; ntries < 50; ntries++) {
		if (CSR_READ_4(sc, IPW_CSR_RST) & IPW_RST_MASTER_DISABLED)
			break;
		DELAY(10);
	}
	if (ntries == 50)
		printf("%s: timeout waiting for master\n",
		    sc->sc_dev.dv_xname);

	tmp = CSR_READ_4(sc, IPW_CSR_RST);
	CSR_WRITE_4(sc, IPW_CSR_RST, tmp | IPW_RST_PRINCETON_RESET);
}

int
ipw_reset(struct ipw_softc *sc)
{
	uint32_t tmp;
	int ntries;

	ipw_stop_master(sc);

	/* move adapter to D0 state */
	tmp = CSR_READ_4(sc, IPW_CSR_CTL);
	CSR_WRITE_4(sc, IPW_CSR_CTL, tmp | IPW_CTL_INIT);

	/* wait for clock stabilization */
	for (ntries = 0; ntries < 1000; ntries++) {
		if (CSR_READ_4(sc, IPW_CSR_CTL) & IPW_CTL_CLOCK_READY)
			break;
		DELAY(200);
	}
	if (ntries == 1000)
		return EIO;

	tmp = CSR_READ_4(sc, IPW_CSR_RST);
	CSR_WRITE_4(sc, IPW_CSR_RST, tmp | IPW_RST_SW_RESET);

	DELAY(10);

	tmp = CSR_READ_4(sc, IPW_CSR_CTL);
	CSR_WRITE_4(sc, IPW_CSR_CTL, tmp | IPW_CTL_INIT);

	return 0;
}

int
ipw_load_ucode(struct ipw_softc *sc, u_char *uc, int size)
{
	int ntries;

	/* voodoo from the Intel Linux driver */
	MEM_WRITE_4(sc, 0x3000e0, 0x80000000);
	CSR_WRITE_4(sc, IPW_CSR_RST, 0);

	MEM_WRITE_2(sc, 0x220000, 0x0703);
	MEM_WRITE_2(sc, 0x220000, 0x0707);

	MEM_WRITE_1(sc, 0x210014, 0x72);
	MEM_WRITE_1(sc, 0x210014, 0x72);

	MEM_WRITE_1(sc, 0x210000, 0x40);
	MEM_WRITE_1(sc, 0x210000, 0x00);
	MEM_WRITE_1(sc, 0x210000, 0x40);

	MEM_WRITE_MULTI_1(sc, 0x210010, uc, size);

	MEM_WRITE_1(sc, 0x210000, 0x00);
	MEM_WRITE_1(sc, 0x210000, 0x00);
	MEM_WRITE_1(sc, 0x210000, 0x80);

	MEM_WRITE_2(sc, 0x220000, 0x0703);
	MEM_WRITE_2(sc, 0x220000, 0x0707);

	MEM_WRITE_1(sc, 0x210014, 0x72);
	MEM_WRITE_1(sc, 0x210014, 0x72);

	MEM_WRITE_1(sc, 0x210000, 0x00);
	MEM_WRITE_1(sc, 0x210000, 0x80);

	for (ntries = 0; ntries < 100; ntries++) {
		if (MEM_READ_1(sc, 0x210000) & 1)
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for ucode to initialize\n",
		    sc->sc_dev.dv_xname);
		return EIO;
	}

	MEM_WRITE_4(sc, 0x3000e0, 0);

	return 0;
}

/* set of macros to handle unaligned little endian data in firmware image */
#define GETLE32(p) ((p)[0] | (p)[1] << 8 | (p)[2] << 16 | (p)[3] << 24)
#define GETLE16(p) ((p)[0] | (p)[1] << 8)
int
ipw_load_firmware(struct ipw_softc *sc, u_char *fw, int size)
{
	u_char *p, *end;
	uint32_t tmp, dst;
	uint16_t len;
	int error;

	p = fw;
	end = fw + size;
	while (p < end) {
		if (p + 6 > end)
			return EINVAL;

		dst = GETLE32(p); p += 4;
		len = GETLE16(p); p += 2;

		if (p + len > end)
			return EINVAL;

		ipw_write_mem_1(sc, dst, p, len);
		p += len;
	}

	CSR_WRITE_4(sc, IPW_CSR_IO, IPW_IO_GPIO1_ENABLE | IPW_IO_GPIO3_MASK |
	    IPW_IO_LED_OFF);

	/* allow interrupts so we know when the firmware is inited */
	CSR_WRITE_4(sc, IPW_CSR_INTR_MASK, IPW_INTR_MASK);

	/* tell the adapter to initialize the firmware */
	CSR_WRITE_4(sc, IPW_CSR_RST, 0);
	tmp = CSR_READ_4(sc, IPW_CSR_CTL);
	CSR_WRITE_4(sc, IPW_CSR_CTL, tmp | IPW_CTL_ALLOW_STANDBY);

	/* wait at most one second for firmware initialization to complete */
	if ((error = tsleep_nsec(sc, 0, "ipwinit", SEC_TO_NSEC(1))) != 0) {
		printf("%s: timeout waiting for firmware initialization to "
		    "complete\n", sc->sc_dev.dv_xname);
		return error;
	}

	tmp = CSR_READ_4(sc, IPW_CSR_IO);
	CSR_WRITE_4(sc, IPW_CSR_IO, tmp | IPW_IO_GPIO1_MASK |
	    IPW_IO_GPIO3_MASK);

	return 0;
}

int
ipw_read_firmware(struct ipw_softc *sc, struct ipw_firmware *fw)
{
	const struct ipw_firmware_hdr *hdr;
	const char *name;
	int error;

	switch (sc->sc_ic.ic_opmode) {
	case IEEE80211_M_STA:
		name = "ipw-bss";
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_IBSS:
		name = "ipw-ibss";
		break;
#endif
	case IEEE80211_M_MONITOR:
		name = "ipw-monitor";
		break;
	default:
		/* should not get there */
		return ENODEV;
	}
	if ((error = loadfirmware(name, &fw->data, &fw->size)) != 0)
		return error;

	if (fw->size < sizeof (*hdr)) {
		error = EINVAL;
		goto fail;
	}
	hdr = (const struct ipw_firmware_hdr *)fw->data;
	fw->main_size  = letoh32(hdr->main_size);
	fw->ucode_size = letoh32(hdr->ucode_size);

	if (fw->size < sizeof (*hdr) + fw->main_size + fw->ucode_size) {
		error = EINVAL;
		goto fail;
	}
	fw->main  = fw->data + sizeof (*hdr);
	fw->ucode = fw->main + fw->main_size;

	return 0;

fail:	free(fw->data, M_DEVBUF, fw->size);
	return error;
}

void
ipw_scan(void *arg1)
{
	struct ipw_softc *sc = arg1;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	struct ipw_scan_options scan;
	uint8_t ssid[IEEE80211_NWID_LEN];
	int error;

	/*
	 * Firmware has a bug and does not honour the ``do not associate
	 * after scan'' bit in the scan command.  To prevent the firmware
	 * from associating after the scan, we set the ESSID to something
	 * unlikely to be used by a real AP.
	 * XXX would setting the desired BSSID to a multicast address work?
	 */
	memset(ssid, '\r', sizeof ssid);
	error = ipw_cmd(sc, IPW_CMD_SET_ESSID, ssid, sizeof ssid);
	if (error != 0)
		goto fail;

	/* no mandatory BSSID */
	DPRINTF(("Setting mandatory BSSID to null\n"));
	error = ipw_cmd(sc, IPW_CMD_SET_MANDATORY_BSSID, NULL, 0);
	if (error != 0)
		goto fail;

	scan.flags = htole32(IPW_SCAN_DO_NOT_ASSOCIATE | IPW_SCAN_MIXED_CELL);
	scan.channels = htole32(0x3fff);	/* scan channels 1-14 */
	DPRINTF(("Setting scan options to 0x%x\n", letoh32(scan.flags)));
	error = ipw_cmd(sc, IPW_CMD_SET_SCAN_OPTIONS, &scan, sizeof scan);
	if (error != 0)
		goto fail;

	/* start scanning */
	DPRINTF(("Enabling adapter\n"));
	error = ipw_cmd(sc, IPW_CMD_ENABLE, NULL, 0);
	if (error != 0)
		goto fail;

	return;
fail:
	printf("%s: scan request failed (error=%d)\n", sc->sc_dev.dv_xname,
	    error);
	ieee80211_end_scan(ifp);
}

void
ipw_auth_and_assoc(void *arg1)
{
	struct ipw_softc *sc = arg1;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	struct ipw_scan_options scan;
	struct ipw_security security;
	struct ipw_assoc_req assoc;
	uint32_t data;
	uint8_t chan;
	int s, error;

	DPRINTF(("Disabling adapter\n"));
	error = ipw_cmd(sc, IPW_CMD_DISABLE, NULL, 0);
	if (error != 0)
		goto fail;
#if 1
	/* wait at most one second for card to be disabled */
	s = splnet();
	error = tsleep_nsec(sc, 0, "ipwdis", SEC_TO_NSEC(1));
	splx(s);
	if (error != 0) {
		printf("%s: timeout waiting for disabled state\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
#else
	/* Intel's Linux driver polls for the DISABLED state instead.. */
	for (ntries = 0; ntries < 1000; ntries++) {
		if (ipw_read_table1(sc, IPW_INFO_CARD_DISABLED) == 1)
			break;
		DELAY(10);
	}
	if (ntries == 1000) {
		printf("%s: timeout waiting for disabled state\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
#endif

	bzero(&security, sizeof security);
	security.authmode = IPW_AUTH_OPEN;
	security.ciphers = htole32(IPW_CIPHER_NONE);
	DPRINTF(("Setting authmode to %u\n", security.authmode));
	error = ipw_cmd(sc, IPW_CMD_SET_SECURITY_INFORMATION, &security,
	    sizeof security);
	if (error != 0)
		goto fail;

#ifdef IPW_DEBUG
	if (ipw_debug > 0) {
		printf("Setting ESSID to ");
		ieee80211_print_essid(ni->ni_essid, ni->ni_esslen);
		printf("\n");
	}
#endif
	error = ipw_cmd(sc, IPW_CMD_SET_ESSID, ni->ni_essid, ni->ni_esslen);
	if (error != 0)
		goto fail;

	DPRINTF(("Setting BSSID to %s\n", ether_sprintf(ni->ni_bssid)));
	error = ipw_cmd(sc, IPW_CMD_SET_MANDATORY_BSSID, ni->ni_bssid,
	    IEEE80211_ADDR_LEN);
	if (error != 0)
		goto fail;

	data = htole32((ic->ic_flags & (IEEE80211_F_WEPON |
	    IEEE80211_F_RSNON)) ? IPW_PRIVACYON : 0);
	DPRINTF(("Setting privacy flags to 0x%x\n", letoh32(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_PRIVACY_FLAGS, &data, sizeof data);
	if (error != 0)
		goto fail;

	/* let firmware set the capinfo, lintval, and bssid fixed fields */
	bzero(&assoc, sizeof assoc);
	if (ic->ic_flags & IEEE80211_F_RSNON) {
		uint8_t *frm = assoc.optie;

		/* tell firmware to add a WPA or RSN IE in (Re)Assoc req */
		if (ni->ni_rsnprotos & IEEE80211_PROTO_RSN)
			frm = ieee80211_add_rsn(frm, ic, ni);
		else if (ni->ni_rsnprotos & IEEE80211_PROTO_WPA)
			frm = ieee80211_add_wpa(frm, ic, ni);
		assoc.optie_len = htole32(frm - assoc.optie);
	}
	DPRINTF(("Preparing association request (optional IE length=%d)\n",
	    letoh32(assoc.optie_len)));
	error = ipw_cmd(sc, IPW_CMD_SET_ASSOC_REQ, &assoc, sizeof assoc);
	if (error != 0)
		goto fail;

	scan.flags = htole32(IPW_SCAN_MIXED_CELL);
	chan = ieee80211_chan2ieee(ic, ni->ni_chan);
	scan.channels = htole32(1 << (chan - 1));
	DPRINTF(("Setting scan options to 0x%x\n", letoh32(scan.flags)));
	error = ipw_cmd(sc, IPW_CMD_SET_SCAN_OPTIONS, &scan, sizeof scan);
	if (error != 0)
		goto fail;

	/* trigger scan+association */
	DPRINTF(("Enabling adapter\n"));
	error = ipw_cmd(sc, IPW_CMD_ENABLE, NULL, 0);
	if (error != 0)
		goto fail;

	/*
	 * net80211 won't see the AP's auth response. Move to ASSOC state
	 * in order to make net80211 accept the AP's assoc response.
	 */
	ic->ic_newstate(ic, IEEE80211_S_ASSOC, -1);

	return;
fail:
	printf("%s: association failed (error=%d)\n", sc->sc_dev.dv_xname,
	    error);
	ieee80211_begin_scan(&ic->ic_if);
}

int
ipw_config(struct ipw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ipw_configuration config;
	uint32_t data;
	int error;

	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		data = htole32(IPW_MODE_BSS);
		break;
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_IBSS:
		data = htole32(IPW_MODE_IBSS);
		break;
#endif
	case IEEE80211_M_MONITOR:
		data = htole32(IPW_MODE_MONITOR);
		break;
	default:
		/* should not get there */
		return ENODEV;
	}
	DPRINTF(("Setting mode to %u\n", letoh32(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_MODE, &data, sizeof data);
	if (error != 0)
		return error;

	if (
#ifndef IEEE80211_STA_ONLY
	    ic->ic_opmode == IEEE80211_M_IBSS ||
#endif
	    ic->ic_opmode == IEEE80211_M_MONITOR) {
		data = htole32(ieee80211_chan2ieee(ic, ic->ic_ibss_chan));
		DPRINTF(("Setting channel to %u\n", letoh32(data)));
		error = ipw_cmd(sc, IPW_CMD_SET_CHANNEL, &data, sizeof data);
		if (error != 0)
			return error;
	}

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		DPRINTF(("Enabling adapter\n"));
		return ipw_cmd(sc, IPW_CMD_ENABLE, NULL, 0);
	}

	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
	DPRINTF(("Setting MAC address to %s\n", ether_sprintf(ic->ic_myaddr)));
	error = ipw_cmd(sc, IPW_CMD_SET_MAC_ADDRESS, ic->ic_myaddr,
	    IEEE80211_ADDR_LEN);
	if (error != 0)
		return error;

	config.flags = htole32(IPW_CFG_BSS_MASK | IPW_CFG_IBSS_MASK |
	    IPW_CFG_PREAMBLE_AUTO | IPW_CFG_802_1X_ENABLE);
#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_IBSS)
		config.flags |= htole32(IPW_CFG_IBSS_AUTO_START);
#endif
	if (ifp->if_flags & IFF_PROMISC)
		config.flags |= htole32(IPW_CFG_PROMISCUOUS);
	config.bss_chan = htole32(0x3fff);	/* channels 1-14 */
	config.ibss_chan = htole32(0x7ff);	/* channels 1-11 */
	DPRINTF(("Setting configuration 0x%x\n", config.flags));
	error = ipw_cmd(sc, IPW_CMD_SET_CONFIGURATION, &config, sizeof config);
	if (error != 0)
		return error;

	data = htole32(ic->ic_rtsthreshold);
	DPRINTF(("Setting RTS threshold to %u\n", letoh32(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_RTS_THRESHOLD, &data, sizeof data);
	if (error != 0)
		return error;

	data = htole32(ic->ic_fragthreshold);
	DPRINTF(("Setting frag threshold to %u\n", letoh32(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_FRAG_THRESHOLD, &data, sizeof data);
	if (error != 0)
		return error;

	data = htole32(0x3);	/* 1, 2 */
	DPRINTF(("Setting basic tx rates to 0x%x\n", letoh32(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_BASIC_TX_RATES, &data, sizeof data);
	if (error != 0)
		return error;

	data = htole32(0xf);	/* 1, 2, 5.5, 11 */
	DPRINTF(("Setting tx rates to 0x%x\n", letoh32(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_TX_RATES, &data, sizeof data);
	if (error != 0)
		return error;

	data = htole32(0xf);	/* 1, 2, 5.5, 11 */
	DPRINTF(("Setting MSDU tx rates to 0x%x\n", letoh32(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_MSDU_TX_RATES, &data, sizeof data);
	if (error != 0)
		return error;

	data = htole32(IPW_POWER_MODE_CAM);
	DPRINTF(("Setting power mode to %u\n", letoh32(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_POWER_MODE, &data, sizeof data);
	if (error != 0)
		return error;

#ifndef IEEE80211_STA_ONLY
	if (ic->ic_opmode == IEEE80211_M_IBSS) {
		data = htole32(32);	/* default value */
		DPRINTF(("Setting tx power index to %u\n", letoh32(data)));
		error = ipw_cmd(sc, IPW_CMD_SET_TX_POWER_INDEX, &data,
		    sizeof data);
		if (error != 0)
			return error;

		data = htole32(ic->ic_lintval);
		DPRINTF(("Setting beacon interval to %u\n", letoh32(data)));
		error = ipw_cmd(sc, IPW_CMD_SET_BEACON_INTERVAL, &data,
		    sizeof data);
		if (error != 0)
			return error;
	}
#endif
	return 0;
}

int
ipw_init(struct ifnet *ifp)
{
	struct ipw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ipw_firmware fw;
	int error;

	ipw_stop(ifp, 0);

	if ((error = ipw_reset(sc)) != 0) {
		printf("%s: could not reset adapter\n", sc->sc_dev.dv_xname);
		goto fail1;
	}

	if ((error = ipw_read_firmware(sc, &fw)) != 0) {
		printf("%s: error %d, could not read firmware\n",
		    sc->sc_dev.dv_xname, error);
		goto fail1;
	}
	if ((error = ipw_load_ucode(sc, fw.ucode, fw.ucode_size)) != 0) {
		printf("%s: could not load microcode\n", sc->sc_dev.dv_xname);
		goto fail2;
	}

	ipw_stop_master(sc);

	/*
	 * Setup tx, rx and status rings.
	 */
	CSR_WRITE_4(sc, IPW_CSR_TX_BD_BASE, sc->tbd_map->dm_segs[0].ds_addr);
	CSR_WRITE_4(sc, IPW_CSR_TX_BD_SIZE, IPW_NTBD);
	CSR_WRITE_4(sc, IPW_CSR_TX_READ_INDEX, 0);
	CSR_WRITE_4(sc, IPW_CSR_TX_WRITE_INDEX, 0);
	sc->txold = IPW_NTBD - 1;	/* latest bd index ack by firmware */
	sc->txcur = 0; /* bd index to write to */
	sc->txfree = IPW_NTBD - 2;

	CSR_WRITE_4(sc, IPW_CSR_RX_BD_BASE, sc->rbd_map->dm_segs[0].ds_addr);
	CSR_WRITE_4(sc, IPW_CSR_RX_BD_SIZE, IPW_NRBD);
	CSR_WRITE_4(sc, IPW_CSR_RX_READ_INDEX, 0);
	CSR_WRITE_4(sc, IPW_CSR_RX_WRITE_INDEX, IPW_NRBD - 1);
	sc->rxcur = IPW_NRBD - 1;	/* latest bd index I've read */

	CSR_WRITE_4(sc, IPW_CSR_RX_STATUS_BASE,
	    sc->status_map->dm_segs[0].ds_addr);

	if ((error = ipw_load_firmware(sc, fw.main, fw.main_size)) != 0) {
		printf("%s: could not load firmware\n", sc->sc_dev.dv_xname);
		goto fail2;
	}
	free(fw.data, M_DEVBUF, fw.size);
	fw.data = NULL;

	/* retrieve information tables base addresses */
	sc->table1_base = CSR_READ_4(sc, IPW_CSR_TABLE1_BASE);
	sc->table2_base = CSR_READ_4(sc, IPW_CSR_TABLE2_BASE);

	ipw_write_table1(sc, IPW_INFO_LOCK, 0);

	if ((error = ipw_config(sc)) != 0) {
		printf("%s: device configuration failed\n",
		    sc->sc_dev.dv_xname);
		goto fail1;
	}

	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_flags |= IFF_RUNNING;

	if (ic->ic_opmode != IEEE80211_M_MONITOR)
		ieee80211_begin_scan(ifp);
	else
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);

	return 0;

fail2:	free(fw.data, M_DEVBUF, fw.size);
	fw.data = NULL;
fail1:	ipw_stop(ifp, 0);
	return error;
}

void
ipw_stop(struct ifnet *ifp, int disable)
{
	struct ipw_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int i;

	ipw_stop_master(sc);
	CSR_WRITE_4(sc, IPW_CSR_RST, IPW_RST_SW_RESET);

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/*
	 * Release tx buffers.
	 */
	for (i = 0; i < IPW_NTBD; i++)
		ipw_release_sbd(sc, &sc->stbd_list[i]);

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
}

void
ipw_read_mem_1(struct ipw_softc *sc, bus_size_t offset, uint8_t *datap,
    bus_size_t count)
{
	for (; count > 0; offset++, datap++, count--) {
		CSR_WRITE_4(sc, IPW_CSR_INDIRECT_ADDR, offset & ~3);
		*datap = CSR_READ_1(sc, IPW_CSR_INDIRECT_DATA + (offset & 3));
	}
}

void
ipw_write_mem_1(struct ipw_softc *sc, bus_size_t offset, uint8_t *datap,
    bus_size_t count)
{
	for (; count > 0; offset++, datap++, count--) {
		CSR_WRITE_4(sc, IPW_CSR_INDIRECT_ADDR, offset & ~3);
		CSR_WRITE_1(sc, IPW_CSR_INDIRECT_DATA + (offset & 3), *datap);
	}
}

struct cfdriver ipw_cd = {
	NULL, "ipw", DV_IFNET
};
