/*	$OpenBSD: if_wpi.c,v 1.158 2024/05/24 06:02:57 jsg Exp $	*/

/*-
 * Copyright (c) 2006-2008
 *	Damien Bergamini <damien.bergamini@free.fr>
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
 * Driver for Intel PRO/Wireless 3945ABG 802.11 network adapters.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/rwlock.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/task.h>
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
#include <net80211/ieee80211_amrr.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/pci/if_wpireg.h>
#include <dev/pci/if_wpivar.h>

static const struct pci_matchid wpi_devices[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_PRO_WL_3945ABG_1 },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_PRO_WL_3945ABG_2 }
};

int		wpi_match(struct device *, void *, void *);
void		wpi_attach(struct device *, struct device *, void *);
#if NBPFILTER > 0
void		wpi_radiotap_attach(struct wpi_softc *);
#endif
int		wpi_detach(struct device *, int);
int		wpi_activate(struct device *, int);
void		wpi_wakeup(struct wpi_softc *);
void		wpi_init_task(void *);
int		wpi_nic_lock(struct wpi_softc *);
int		wpi_read_prom_data(struct wpi_softc *, uint32_t, void *, int);
int		wpi_dma_contig_alloc(bus_dma_tag_t, struct wpi_dma_info *,
		    void **, bus_size_t, bus_size_t);
void		wpi_dma_contig_free(struct wpi_dma_info *);
int		wpi_alloc_shared(struct wpi_softc *);
void		wpi_free_shared(struct wpi_softc *);
int		wpi_alloc_fwmem(struct wpi_softc *);
void		wpi_free_fwmem(struct wpi_softc *);
int		wpi_alloc_rx_ring(struct wpi_softc *, struct wpi_rx_ring *);
void		wpi_reset_rx_ring(struct wpi_softc *, struct wpi_rx_ring *);
void		wpi_free_rx_ring(struct wpi_softc *, struct wpi_rx_ring *);
int		wpi_alloc_tx_ring(struct wpi_softc *, struct wpi_tx_ring *,
		    int);
void		wpi_reset_tx_ring(struct wpi_softc *, struct wpi_tx_ring *);
void		wpi_free_tx_ring(struct wpi_softc *, struct wpi_tx_ring *);
int		wpi_read_eeprom(struct wpi_softc *);
void		wpi_read_eeprom_channels(struct wpi_softc *, int);
void		wpi_read_eeprom_group(struct wpi_softc *, int);
struct		ieee80211_node *wpi_node_alloc(struct ieee80211com *);
void		wpi_newassoc(struct ieee80211com *, struct ieee80211_node *,
		    int);
int		wpi_media_change(struct ifnet *);
int		wpi_newstate(struct ieee80211com *, enum ieee80211_state, int);
void		wpi_iter_func(void *, struct ieee80211_node *);
void		wpi_calib_timeout(void *);
int		wpi_ccmp_decap(struct wpi_softc *, struct mbuf *,
		    struct ieee80211_key *);
void		wpi_rx_done(struct wpi_softc *, struct wpi_rx_desc *,
		    struct wpi_rx_data *, struct mbuf_list *);
void		wpi_tx_done(struct wpi_softc *, struct wpi_rx_desc *);
void		wpi_cmd_done(struct wpi_softc *, struct wpi_rx_desc *);
void		wpi_notif_intr(struct wpi_softc *);
void		wpi_fatal_intr(struct wpi_softc *);
int		wpi_intr(void *);
int		wpi_tx(struct wpi_softc *, struct mbuf *,
		    struct ieee80211_node *);
void		wpi_start(struct ifnet *);
void		wpi_watchdog(struct ifnet *);
int		wpi_ioctl(struct ifnet *, u_long, caddr_t);
int		wpi_cmd(struct wpi_softc *, int, const void *, int, int);
int		wpi_mrr_setup(struct wpi_softc *);
void		wpi_updateedca(struct ieee80211com *);
void		wpi_set_led(struct wpi_softc *, uint8_t, uint8_t, uint8_t);
int		wpi_set_timing(struct wpi_softc *, struct ieee80211_node *);
void		wpi_power_calibration(struct wpi_softc *);
int		wpi_set_txpower(struct wpi_softc *, int);
int		wpi_get_power_index(struct wpi_softc *,
		    struct wpi_power_group *, struct ieee80211_channel *, int);
int		wpi_set_pslevel(struct wpi_softc *, int, int, int);
int		wpi_config(struct wpi_softc *);
int		wpi_scan(struct wpi_softc *, uint16_t);
int		wpi_auth(struct wpi_softc *);
int		wpi_run(struct wpi_softc *);
int		wpi_set_key(struct ieee80211com *, struct ieee80211_node *,
		    struct ieee80211_key *);
void		wpi_delete_key(struct ieee80211com *, struct ieee80211_node *,
		    struct ieee80211_key *);
int		wpi_post_alive(struct wpi_softc *);
int		wpi_load_bootcode(struct wpi_softc *, const uint8_t *, int);
int		wpi_load_firmware(struct wpi_softc *);
int		wpi_read_firmware(struct wpi_softc *);
int		wpi_clock_wait(struct wpi_softc *);
int		wpi_apm_init(struct wpi_softc *);
void		wpi_apm_stop_master(struct wpi_softc *);
void		wpi_apm_stop(struct wpi_softc *);
void		wpi_nic_config(struct wpi_softc *);
int		wpi_hw_init(struct wpi_softc *);
void		wpi_hw_stop(struct wpi_softc *);
int		wpi_init(struct ifnet *);
void		wpi_stop(struct ifnet *, int);

#ifdef WPI_DEBUG
#define DPRINTF(x)	do { if (wpi_debug > 0) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (wpi_debug >= (n)) printf x; } while (0)
int wpi_debug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

struct cfdriver wpi_cd = {
	NULL, "wpi", DV_IFNET
};

const struct cfattach wpi_ca = {
	sizeof (struct wpi_softc), wpi_match, wpi_attach, wpi_detach,
	wpi_activate
};

int
wpi_match(struct device *parent, void *match, void *aux)
{
	return pci_matchbyid((struct pci_attach_args *)aux, wpi_devices,
	    nitems(wpi_devices));
}

void
wpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct wpi_softc *sc = (struct wpi_softc *)self;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct pci_attach_args *pa = aux;
	const char *intrstr;
	pci_intr_handle_t ih;
	pcireg_t memtype, reg;
	int i, error;

	sc->sc_pct = pa->pa_pc;
	sc->sc_pcitag = pa->pa_tag;
	sc->sc_dmat = pa->pa_dmat;

	/*
	 * Get the offset of the PCI Express Capability Structure in PCI
	 * Configuration Space (the vendor driver hard-codes it as E0h.)
	 */
	error = pci_get_capability(sc->sc_pct, sc->sc_pcitag,
	    PCI_CAP_PCIEXPRESS, &sc->sc_cap_off, NULL);
	if (error == 0) {
		printf(": PCIe capability structure not found!\n");
		return;
	}

	/* Clear device-specific "PCI retry timeout" register (41h). */
	reg = pci_conf_read(sc->sc_pct, sc->sc_pcitag, 0x40);
	reg &= ~0xff00;
	pci_conf_write(sc->sc_pct, sc->sc_pcitag, 0x40, reg);

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, WPI_PCI_BAR0);
	error = pci_mapreg_map(pa, WPI_PCI_BAR0, memtype, 0, &sc->sc_st,
	    &sc->sc_sh, NULL, &sc->sc_sz, 0);
	if (error != 0) {
		printf(": can't map mem space\n");
		return;
	}

	/* Install interrupt handler. */
	if (pci_intr_map_msi(pa, &ih) != 0 && pci_intr_map(pa, &ih) != 0) {
		printf(": can't map interrupt\n");
		return;
	}
	intrstr = pci_intr_string(sc->sc_pct, ih);
	sc->sc_ih = pci_intr_establish(sc->sc_pct, ih, IPL_NET, wpi_intr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s", intrstr);

	/* Power ON adapter. */
	if ((error = wpi_apm_init(sc)) != 0) {
		printf(": could not power ON adapter\n");
		return;
	}

	/* Read MAC address, channels, etc from EEPROM. */
	if ((error = wpi_read_eeprom(sc)) != 0) {
		printf(": could not read EEPROM\n");
		return;
	}

	/* Allocate DMA memory for firmware transfers. */
	if ((error = wpi_alloc_fwmem(sc)) != 0) {
		printf(": could not allocate memory for firmware\n");
		return;
	}

	/* Allocate shared area. */
	if ((error = wpi_alloc_shared(sc)) != 0) {
		printf(": could not allocate shared area\n");
		goto fail1;
	}

	/* Allocate TX rings. */
	for (i = 0; i < WPI_NTXQUEUES; i++) {
		if ((error = wpi_alloc_tx_ring(sc, &sc->txq[i], i)) != 0) {
			printf(": could not allocate TX ring %d\n", i);
			goto fail2;
		}
	}

	/* Allocate RX ring. */
	if ((error = wpi_alloc_rx_ring(sc, &sc->rxq)) != 0) {
		printf(": could not allocate Rx ring\n");
		goto fail2;
	}

	/* Power OFF adapter. */
	wpi_apm_stop(sc);
	/* Clear pending interrupts. */
	WPI_WRITE(sc, WPI_INT, 0xffffffff);

	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */
	ic->ic_state = IEEE80211_S_INIT;

	/* Set device capabilities. */
	ic->ic_caps =
	    IEEE80211_C_WEP |		/* WEP */
	    IEEE80211_C_RSN |		/* WPA/RSN */
	    IEEE80211_C_SCANALL |	/* device scans all channels at once */
	    IEEE80211_C_SCANALLBAND |	/* driver scans all bands at once */
	    IEEE80211_C_MONITOR |	/* monitor mode supported */
	    IEEE80211_C_SHSLOT |	/* short slot time supported */
	    IEEE80211_C_SHPREAMBLE |	/* short preamble supported */
	    IEEE80211_C_PMGT;		/* power saving supported */

	/* Set supported rates. */
	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;
	if (sc->sc_flags & WPI_FLAG_HAS_5GHZ) {
		ic->ic_sup_rates[IEEE80211_MODE_11A] =
		    ieee80211_std_rateset_11a;
	}

	/* IBSS channel undefined for now. */
	ic->ic_ibss_chan = &ic->ic_channels[0];

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = wpi_ioctl;
	ifp->if_start = wpi_start;
	ifp->if_watchdog = wpi_watchdog;
	memcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp);
	ic->ic_node_alloc = wpi_node_alloc;
	ic->ic_newassoc = wpi_newassoc;
	ic->ic_updateedca = wpi_updateedca;
	ic->ic_set_key = wpi_set_key;
	ic->ic_delete_key = wpi_delete_key;

	/* Override 802.11 state transition machine. */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = wpi_newstate;
	ieee80211_media_init(ifp, wpi_media_change, ieee80211_media_status);

	sc->amrr.amrr_min_success_threshold =  1;
	sc->amrr.amrr_max_success_threshold = 15;

#if NBPFILTER > 0
	wpi_radiotap_attach(sc);
#endif
	timeout_set(&sc->calib_to, wpi_calib_timeout, sc);
	rw_init(&sc->sc_rwlock, "wpilock");
	task_set(&sc->init_task, wpi_init_task, sc);
	return;

	/* Free allocated memory if something failed during attachment. */
fail2:	while (--i >= 0)
		wpi_free_tx_ring(sc, &sc->txq[i]);
	wpi_free_shared(sc);
fail1:	wpi_free_fwmem(sc);
}

#if NBPFILTER > 0
/*
 * Attach the interface to 802.11 radiotap.
 */
void
wpi_radiotap_attach(struct wpi_softc *sc)
{
	bpfattach(&sc->sc_drvbpf, &sc->sc_ic.ic_if, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(WPI_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(WPI_TX_RADIOTAP_PRESENT);
}
#endif

int
wpi_detach(struct device *self, int flags)
{
	struct wpi_softc *sc = (struct wpi_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int qid;

	timeout_del(&sc->calib_to);
	task_del(systq, &sc->init_task);

	/* Uninstall interrupt handler. */
	if (sc->sc_ih != NULL)
		pci_intr_disestablish(sc->sc_pct, sc->sc_ih);

	/* Free DMA resources. */
	wpi_free_rx_ring(sc, &sc->rxq);
	for (qid = 0; qid < WPI_NTXQUEUES; qid++)
		wpi_free_tx_ring(sc, &sc->txq[qid]);
	wpi_free_shared(sc);
	wpi_free_fwmem(sc);

	bus_space_unmap(sc->sc_st, sc->sc_sh, sc->sc_sz);

	ieee80211_ifdetach(ifp);
	if_detach(ifp);

	return 0;
}

int
wpi_activate(struct device *self, int act)
{
	struct wpi_softc *sc = (struct wpi_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			wpi_stop(ifp, 0);
		break;
	case DVACT_WAKEUP:
		wpi_wakeup(sc);
		break;
	}

	return 0;
}

void
wpi_wakeup(struct wpi_softc *sc)
{
	pcireg_t reg;

	/* Clear device-specific "PCI retry timeout" register (41h). */
	reg = pci_conf_read(sc->sc_pct, sc->sc_pcitag, 0x40);
	reg &= ~0xff00;
	pci_conf_write(sc->sc_pct, sc->sc_pcitag, 0x40, reg);

	wpi_init_task(sc);
}

void
wpi_init_task(void *arg1)
{
	struct wpi_softc *sc = arg1;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int s;

	rw_enter_write(&sc->sc_rwlock);
	s = splnet();

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == IFF_UP)
		wpi_init(ifp);

	splx(s);
	rw_exit_write(&sc->sc_rwlock);
}

int
wpi_nic_lock(struct wpi_softc *sc)
{
	int ntries;

	/* Request exclusive access to NIC. */
	WPI_SETBITS(sc, WPI_GP_CNTRL, WPI_GP_CNTRL_MAC_ACCESS_REQ);

	/* Spin until we actually get the lock. */
	for (ntries = 0; ntries < 1000; ntries++) {
		if ((WPI_READ(sc, WPI_GP_CNTRL) &
		     (WPI_GP_CNTRL_MAC_ACCESS_ENA | WPI_GP_CNTRL_SLEEP)) ==
		    WPI_GP_CNTRL_MAC_ACCESS_ENA)
			return 0;
		DELAY(10);
	}
	return ETIMEDOUT;
}

static __inline void
wpi_nic_unlock(struct wpi_softc *sc)
{
	WPI_CLRBITS(sc, WPI_GP_CNTRL, WPI_GP_CNTRL_MAC_ACCESS_REQ);
}

static __inline uint32_t
wpi_prph_read(struct wpi_softc *sc, uint32_t addr)
{
	WPI_WRITE(sc, WPI_PRPH_RADDR, WPI_PRPH_DWORD | addr);
	WPI_BARRIER_READ_WRITE(sc);
	return WPI_READ(sc, WPI_PRPH_RDATA);
}

static __inline void
wpi_prph_write(struct wpi_softc *sc, uint32_t addr, uint32_t data)
{
	WPI_WRITE(sc, WPI_PRPH_WADDR, WPI_PRPH_DWORD | addr);
	WPI_BARRIER_WRITE(sc);
	WPI_WRITE(sc, WPI_PRPH_WDATA, data);
}

static __inline void
wpi_prph_setbits(struct wpi_softc *sc, uint32_t addr, uint32_t mask)
{
	wpi_prph_write(sc, addr, wpi_prph_read(sc, addr) | mask);
}

static __inline void
wpi_prph_clrbits(struct wpi_softc *sc, uint32_t addr, uint32_t mask)
{
	wpi_prph_write(sc, addr, wpi_prph_read(sc, addr) & ~mask);
}

static __inline void
wpi_prph_write_region_4(struct wpi_softc *sc, uint32_t addr,
    const uint32_t *data, int count)
{
	for (; count > 0; count--, data++, addr += 4)
		wpi_prph_write(sc, addr, *data);
}

#ifdef WPI_DEBUG

static __inline uint32_t
wpi_mem_read(struct wpi_softc *sc, uint32_t addr)
{
	WPI_WRITE(sc, WPI_MEM_RADDR, addr);
	WPI_BARRIER_READ_WRITE(sc);
	return WPI_READ(sc, WPI_MEM_RDATA);
}

static __inline void  
wpi_mem_write(struct wpi_softc *sc, uint32_t addr, uint32_t data)
{
	WPI_WRITE(sc, WPI_MEM_WADDR, addr);
	WPI_BARRIER_WRITE(sc);
	WPI_WRITE(sc, WPI_MEM_WDATA, data);
}

static __inline void
wpi_mem_read_region_4(struct wpi_softc *sc, uint32_t addr, uint32_t *data,
    int count)
{
	for (; count > 0; count--, addr += 4)
		*data++ = wpi_mem_read(sc, addr);
}

#endif

int
wpi_read_prom_data(struct wpi_softc *sc, uint32_t addr, void *data, int count)
{
	uint8_t *out = data;
	uint32_t val;
	int error, ntries;

	if ((error = wpi_nic_lock(sc)) != 0)
		return error;

	for (; count > 0; count -= 2, addr++) {
		WPI_WRITE(sc, WPI_EEPROM, addr << 2);
		WPI_CLRBITS(sc, WPI_EEPROM, WPI_EEPROM_CMD);

		for (ntries = 0; ntries < 10; ntries++) {
			val = WPI_READ(sc, WPI_EEPROM);
			if (val & WPI_EEPROM_READ_VALID)
				break;
			DELAY(5);
		}
		if (ntries == 10) {
			printf("%s: could not read EEPROM\n",
			    sc->sc_dev.dv_xname);
			return ETIMEDOUT;
		}
		*out++ = val >> 16;
		if (count > 1)
			*out++ = val >> 24;
	}

	wpi_nic_unlock(sc);
	return 0;
}

int
wpi_dma_contig_alloc(bus_dma_tag_t tag, struct wpi_dma_info *dma, void **kvap,
    bus_size_t size, bus_size_t alignment)
{
	int nsegs, error;

	dma->tag = tag;
	dma->size = size;

	error = bus_dmamap_create(tag, size, 1, size, 0, BUS_DMA_NOWAIT,
	    &dma->map);
	if (error != 0)
		goto fail;

	error = bus_dmamem_alloc(tag, size, alignment, 0, &dma->seg, 1, &nsegs,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (error != 0)
		goto fail;

	error = bus_dmamem_map(tag, &dma->seg, 1, size, &dma->vaddr,
	    BUS_DMA_NOWAIT | BUS_DMA_COHERENT);
	if (error != 0)
		goto fail;

	error = bus_dmamap_load_raw(tag, dma->map, &dma->seg, 1, size,
	    BUS_DMA_NOWAIT);
	if (error != 0)
		goto fail;

	bus_dmamap_sync(tag, dma->map, 0, size, BUS_DMASYNC_PREWRITE);

	dma->paddr = dma->map->dm_segs[0].ds_addr;
	if (kvap != NULL)
		*kvap = dma->vaddr;

	return 0;

fail:	wpi_dma_contig_free(dma);
	return error;
}

void
wpi_dma_contig_free(struct wpi_dma_info *dma)
{
	if (dma->map != NULL) {
		if (dma->vaddr != NULL) {
			bus_dmamap_sync(dma->tag, dma->map, 0, dma->size,
			    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(dma->tag, dma->map);
			bus_dmamem_unmap(dma->tag, dma->vaddr, dma->size);
			bus_dmamem_free(dma->tag, &dma->seg, 1);
			dma->vaddr = NULL;
		}
		bus_dmamap_destroy(dma->tag, dma->map);
		dma->map = NULL;
	}
}

int
wpi_alloc_shared(struct wpi_softc *sc)
{
	/* Shared buffer must be aligned on a 4KB boundary. */
	return wpi_dma_contig_alloc(sc->sc_dmat, &sc->shared_dma,
	    (void **)&sc->shared, sizeof (struct wpi_shared), 4096);
}

void
wpi_free_shared(struct wpi_softc *sc)
{
	wpi_dma_contig_free(&sc->shared_dma);
}

int
wpi_alloc_fwmem(struct wpi_softc *sc)
{
	/* Allocate enough contiguous space to store text and data. */
	return wpi_dma_contig_alloc(sc->sc_dmat, &sc->fw_dma, NULL,
	    WPI_FW_TEXT_MAXSZ + WPI_FW_DATA_MAXSZ, 16);
}

void
wpi_free_fwmem(struct wpi_softc *sc)
{
	wpi_dma_contig_free(&sc->fw_dma);
}

int
wpi_alloc_rx_ring(struct wpi_softc *sc, struct wpi_rx_ring *ring)
{
	bus_size_t size;
	int i, error;

	ring->cur = 0;

	/* Allocate RX descriptors (16KB aligned.) */
	size = WPI_RX_RING_COUNT * sizeof (uint32_t);
	error = wpi_dma_contig_alloc(sc->sc_dmat, &ring->desc_dma,
	    (void **)&ring->desc, size, 16 * 1024);
	if (error != 0) {
		printf("%s: could not allocate RX ring DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/*
	 * Allocate and map RX buffers.
	 */
	for (i = 0; i < WPI_RX_RING_COUNT; i++) {
		struct wpi_rx_data *data = &ring->data[i];

		error = bus_dmamap_create(sc->sc_dmat, WPI_RBUF_SIZE, 1,
		    WPI_RBUF_SIZE, 0, BUS_DMA_NOWAIT, &data->map);
		if (error != 0) {
			printf("%s: could not create RX buf DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		data->m = MCLGETL(NULL, M_DONTWAIT, WPI_RBUF_SIZE);
		if (data->m == NULL) {
			printf("%s: could not allocate RX mbuf\n",
			    sc->sc_dev.dv_xname);
			error = ENOBUFS;
			goto fail;
		}

		error = bus_dmamap_load(sc->sc_dmat, data->map,
		    mtod(data->m, void *), WPI_RBUF_SIZE, NULL,
		    BUS_DMA_NOWAIT | BUS_DMA_READ);
		if (error != 0) {
			printf("%s: can't map mbuf (error %d)\n",
			    sc->sc_dev.dv_xname, error);
			goto fail;
		}

		/* Set physical address of RX buffer. */
		ring->desc[i] = htole32(data->map->dm_segs[0].ds_addr);
	}

	bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map, 0, size,
	    BUS_DMASYNC_PREWRITE);

	return 0;

fail:	wpi_free_rx_ring(sc, ring);
	return error;
}

void
wpi_reset_rx_ring(struct wpi_softc *sc, struct wpi_rx_ring *ring)
{
	int ntries;

	if (wpi_nic_lock(sc) == 0) {
		WPI_WRITE(sc, WPI_FH_RX_CONFIG, 0);
		for (ntries = 0; ntries < 100; ntries++) {
			if (WPI_READ(sc, WPI_FH_RX_STATUS) &
			    WPI_FH_RX_STATUS_IDLE)
				break;
			DELAY(10);
		}
		wpi_nic_unlock(sc);
	}
	ring->cur = 0;
}

void
wpi_free_rx_ring(struct wpi_softc *sc, struct wpi_rx_ring *ring)
{
	int i;

	wpi_dma_contig_free(&ring->desc_dma);

	for (i = 0; i < WPI_RX_RING_COUNT; i++) {
		struct wpi_rx_data *data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, data->map, 0,
			    data->map->dm_mapsize, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmat, data->map);
			m_freem(data->m);
		}
		if (data->map != NULL)
			bus_dmamap_destroy(sc->sc_dmat, data->map);
	}
}

int
wpi_alloc_tx_ring(struct wpi_softc *sc, struct wpi_tx_ring *ring, int qid)
{
	bus_addr_t paddr;
	bus_size_t size;
	int i, error;

	ring->qid = qid;
	ring->queued = 0;
	ring->cur = 0;

	/* Allocate TX descriptors (16KB aligned.) */
	size = WPI_TX_RING_COUNT * sizeof (struct wpi_tx_desc);
	error = wpi_dma_contig_alloc(sc->sc_dmat, &ring->desc_dma,
	    (void **)&ring->desc, size, 16 * 1024);
	if (error != 0) {
		printf("%s: could not allocate TX ring DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/* Update shared area with ring physical address. */
	sc->shared->txbase[qid] = htole32(ring->desc_dma.paddr);
	bus_dmamap_sync(sc->sc_dmat, sc->shared_dma.map, 0,
	    sizeof (struct wpi_shared), BUS_DMASYNC_PREWRITE);

	/*
	 * We only use rings 0 through 4 (4 EDCA + cmd) so there is no need
	 * to allocate commands space for other rings.
	 * XXX Do we really need to allocate descriptors for other rings?
	 */
	if (qid > 4)
		return 0;

	size = WPI_TX_RING_COUNT * sizeof (struct wpi_tx_cmd);
	error = wpi_dma_contig_alloc(sc->sc_dmat, &ring->cmd_dma,
	    (void **)&ring->cmd, size, 4);
	if (error != 0) {
		printf("%s: could not allocate TX cmd DMA memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	paddr = ring->cmd_dma.paddr;
	for (i = 0; i < WPI_TX_RING_COUNT; i++) {
		struct wpi_tx_data *data = &ring->data[i];

		data->cmd_paddr = paddr;
		paddr += sizeof (struct wpi_tx_cmd);

		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES,
		    WPI_MAX_SCATTER - 1, MCLBYTES, 0, BUS_DMA_NOWAIT,
		    &data->map);
		if (error != 0) {
			printf("%s: could not create TX buf DMA map\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
	}
	return 0;

fail:	wpi_free_tx_ring(sc, ring);
	return error;
}

void
wpi_reset_tx_ring(struct wpi_softc *sc, struct wpi_tx_ring *ring)
{
	int i;

	for (i = 0; i < WPI_TX_RING_COUNT; i++) {
		struct wpi_tx_data *data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, data->map, 0,
			    data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, data->map);
			m_freem(data->m);
			data->m = NULL;
		}
	}
	/* Clear TX descriptors. */
	memset(ring->desc, 0, ring->desc_dma.size);
	sc->qfullmsk &= ~(1 << ring->qid);
	ring->queued = 0;
	ring->cur = 0;
}

void
wpi_free_tx_ring(struct wpi_softc *sc, struct wpi_tx_ring *ring)
{
	int i;

	wpi_dma_contig_free(&ring->desc_dma);
	wpi_dma_contig_free(&ring->cmd_dma);

	for (i = 0; i < WPI_TX_RING_COUNT; i++) {
		struct wpi_tx_data *data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(sc->sc_dmat, data->map, 0,
			    data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, data->map);
			m_freem(data->m);
		}
		if (data->map != NULL)
			bus_dmamap_destroy(sc->sc_dmat, data->map);
	}
}

int
wpi_read_eeprom(struct wpi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	char domain[4];
	int i;

	if ((WPI_READ(sc, WPI_EEPROM_GP) & 0x6) == 0) {
		printf("%s: bad EEPROM signature\n", sc->sc_dev.dv_xname);
		return EIO;
	}
	/* Clear HW ownership of EEPROM. */
	WPI_CLRBITS(sc, WPI_EEPROM_GP, WPI_EEPROM_GP_IF_OWNER);

	wpi_read_prom_data(sc, WPI_EEPROM_CAPABILITIES, &sc->cap, 1);
	wpi_read_prom_data(sc, WPI_EEPROM_REVISION, &sc->rev, 2);
	wpi_read_prom_data(sc, WPI_EEPROM_TYPE, &sc->type, 1);

	DPRINTF(("cap=%x rev=%x type=%x\n", sc->cap, letoh16(sc->rev),
	    sc->type));

	/* Read and print regulatory domain (4 ASCII characters.) */
	wpi_read_prom_data(sc, WPI_EEPROM_DOMAIN, domain, 4);
	printf(", %.4s", domain);

	/* Read and print MAC address. */
	wpi_read_prom_data(sc, WPI_EEPROM_MAC, ic->ic_myaddr, 6);
	printf(", address %s\n", ether_sprintf(ic->ic_myaddr));

	/* Read the list of authorized channels. */
	for (i = 0; i < WPI_CHAN_BANDS_COUNT; i++)
		wpi_read_eeprom_channels(sc, i);

	/* Read the list of TX power groups. */
	for (i = 0; i < WPI_POWER_GROUPS_COUNT; i++)
		wpi_read_eeprom_group(sc, i);

	return 0;
}

void
wpi_read_eeprom_channels(struct wpi_softc *sc, int n)
{
	struct ieee80211com *ic = &sc->sc_ic;
	const struct wpi_chan_band *band = &wpi_bands[n];
	struct wpi_eeprom_chan channels[WPI_MAX_CHAN_PER_BAND];
	int chan, i;

	wpi_read_prom_data(sc, band->addr, channels,
	    band->nchan * sizeof (struct wpi_eeprom_chan));

	for (i = 0; i < band->nchan; i++) {
		if (!(channels[i].flags & WPI_EEPROM_CHAN_VALID))
			continue;

		chan = band->chan[i];

		if (n == 0) {	/* 2GHz band */
			ic->ic_channels[chan].ic_freq =
			    ieee80211_ieee2mhz(chan, IEEE80211_CHAN_2GHZ);
			ic->ic_channels[chan].ic_flags =
			    IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
			    IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;

		} else {	/* 5GHz band */
			/*
			 * Some adapters support channels 7, 8, 11 and 12
			 * both in the 2GHz and 4.9GHz bands.
			 * Because of limitations in our net80211 layer,
			 * we don't support them in the 4.9GHz band.
			 */
			if (chan <= 14)
				continue;

			ic->ic_channels[chan].ic_freq =
			    ieee80211_ieee2mhz(chan, IEEE80211_CHAN_5GHZ);
			ic->ic_channels[chan].ic_flags = IEEE80211_CHAN_A;
			/* We have at least one valid 5GHz channel. */
			sc->sc_flags |= WPI_FLAG_HAS_5GHZ;
		}

		/* Is active scan allowed on this channel? */
		if (!(channels[i].flags & WPI_EEPROM_CHAN_ACTIVE)) {
			ic->ic_channels[chan].ic_flags |=
			    IEEE80211_CHAN_PASSIVE;
		}

		/* Save maximum allowed TX power for this channel. */
		sc->maxpwr[chan] = channels[i].maxpwr;

		DPRINTF(("adding chan %d flags=0x%x maxpwr=%d\n",
		    chan, channels[i].flags, sc->maxpwr[chan]));
	}
}

void
wpi_read_eeprom_group(struct wpi_softc *sc, int n)
{
	struct wpi_power_group *group = &sc->groups[n];
	struct wpi_eeprom_group rgroup;
	int i;

	wpi_read_prom_data(sc, WPI_EEPROM_POWER_GRP + n * 32, &rgroup,
	    sizeof rgroup);

	/* Save TX power group information. */
	group->chan   = rgroup.chan;
	group->maxpwr = rgroup.maxpwr;
	/* Retrieve temperature at which the samples were taken. */
	group->temp   = (int16_t)letoh16(rgroup.temp);

	DPRINTF(("power group %d: chan=%d maxpwr=%d temp=%d\n", n,
	    group->chan, group->maxpwr, group->temp));

	for (i = 0; i < WPI_SAMPLES_COUNT; i++) {
		group->samples[i].index = rgroup.samples[i].index;
		group->samples[i].power = rgroup.samples[i].power;

		DPRINTF(("\tsample %d: index=%d power=%d\n", i,
		    group->samples[i].index, group->samples[i].power));
	}
}

struct ieee80211_node *
wpi_node_alloc(struct ieee80211com *ic)
{
	return malloc(sizeof (struct wpi_node), M_DEVBUF, M_NOWAIT | M_ZERO);
}

void
wpi_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
	struct wpi_softc *sc = ic->ic_if.if_softc;
	struct wpi_node *wn = (void *)ni;
	uint8_t rate;
	int ridx, i;

	ieee80211_amrr_node_init(&sc->amrr, &wn->amn);
	/* Start at lowest available bit-rate, AMRR will raise. */
	ni->ni_txrate = 0;

	for (i = 0; i < ni->ni_rates.rs_nrates; i++) {
		rate = ni->ni_rates.rs_rates[i] & IEEE80211_RATE_VAL;
		/* Map 802.11 rate to HW rate index. */
		for (ridx = 0; ridx <= WPI_RIDX_MAX; ridx++)
			if (wpi_rates[ridx].rate == rate)
				break;
		wn->ridx[i] = ridx;
	}
}

int
wpi_media_change(struct ifnet *ifp)
{
	struct wpi_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t rate, ridx;
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if (ic->ic_fixed_rate != -1) {
		rate = ic->ic_sup_rates[ic->ic_curmode].
		    rs_rates[ic->ic_fixed_rate] & IEEE80211_RATE_VAL;
		/* Map 802.11 rate to HW rate index. */
		for (ridx = 0; ridx <= WPI_RIDX_MAX; ridx++)
			if (wpi_rates[ridx].rate == rate)
				break;
		sc->fixed_ridx = ridx;
	}

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
	    (IFF_UP | IFF_RUNNING)) {
		wpi_stop(ifp, 0);
		error = wpi_init(ifp);
	}
	return error;
}

int
wpi_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct ifnet *ifp = &ic->ic_if;
	struct wpi_softc *sc = ifp->if_softc;
	int error;

	timeout_del(&sc->calib_to);

	switch (nstate) {
	case IEEE80211_S_SCAN:
		/* Make the link LED blink while we're scanning. */
		wpi_set_led(sc, WPI_LED_LINK, 20, 2);

		if ((error = wpi_scan(sc, IEEE80211_CHAN_2GHZ)) != 0) {
			printf("%s: could not initiate scan\n",
			    sc->sc_dev.dv_xname);
			return error;
		}
		if (ifp->if_flags & IFF_DEBUG)
			printf("%s: %s -> %s\n", ifp->if_xname,
			    ieee80211_state_name[ic->ic_state],
			    ieee80211_state_name[nstate]);
		ieee80211_set_link_state(ic, LINK_STATE_DOWN);
		ieee80211_node_cleanup(ic, ic->ic_bss);
		ic->ic_state = nstate;
		return 0;

	case IEEE80211_S_ASSOC:
		if (ic->ic_state != IEEE80211_S_RUN)
			break;
		/* FALLTHROUGH */
	case IEEE80211_S_AUTH:
		/* Reset state to handle reassociations correctly. */
		sc->rxon.associd = 0;
		sc->rxon.filter &= ~htole32(WPI_FILTER_BSS);

		if ((error = wpi_auth(sc)) != 0) {
			printf("%s: could not move to auth state\n",
			    sc->sc_dev.dv_xname);
			return error;
		}
		break;

	case IEEE80211_S_RUN:
		if ((error = wpi_run(sc)) != 0) {
			printf("%s: could not move to run state\n",
			    sc->sc_dev.dv_xname);
			return error;
		}
		break;

	case IEEE80211_S_INIT:
		break;
	}

	return sc->sc_newstate(ic, nstate, arg);
}

void
wpi_iter_func(void *arg, struct ieee80211_node *ni)
{
	struct wpi_softc *sc = arg;
	struct wpi_node *wn = (struct wpi_node *)ni;

	ieee80211_amrr_choose(&sc->amrr, ni, &wn->amn);
}

void
wpi_calib_timeout(void *arg)
{
	struct wpi_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	int s;

	s = splnet();
	/* Automatic rate control triggered every 500ms. */
	if (ic->ic_fixed_rate == -1) {
		if (ic->ic_opmode == IEEE80211_M_STA)
			wpi_iter_func(sc, ic->ic_bss);
		else
			ieee80211_iterate_nodes(ic, wpi_iter_func, sc);
	}

	/* Force automatic TX power calibration every 60 secs. */
	if (++sc->calib_cnt >= 120) {
		wpi_power_calibration(sc);
		sc->calib_cnt = 0;
	}
	splx(s);

	/* Automatic rate control triggered every 500ms. */
	timeout_add_msec(&sc->calib_to, 500);
}

int
wpi_ccmp_decap(struct wpi_softc *sc, struct mbuf *m, struct ieee80211_key *k)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh;
	uint64_t pn, *prsc;
	uint8_t *ivp;
	uint8_t tid;
	int hdrlen;

	wh = mtod(m, struct ieee80211_frame *);
	hdrlen = ieee80211_get_hdrlen(wh);
	ivp = (uint8_t *)wh + hdrlen;

	/* Check that ExtIV bit is set. */
	if (!(ivp[3] & IEEE80211_WEP_EXTIV)) {
		DPRINTF(("CCMP decap ExtIV not set\n"));
		return 1;
	}
	tid = ieee80211_has_qos(wh) ?
	    ieee80211_get_qos(wh) & IEEE80211_QOS_TID : 0;
	prsc = &k->k_rsc[tid];

	/* Extract the 48-bit PN from the CCMP header. */
	pn = (uint64_t)ivp[0]       |
	     (uint64_t)ivp[1] <<  8 |
	     (uint64_t)ivp[4] << 16 |
	     (uint64_t)ivp[5] << 24 |
	     (uint64_t)ivp[6] << 32 |
	     (uint64_t)ivp[7] << 40;
	if (pn <= *prsc) {
		DPRINTF(("CCMP replayed\n"));
		ic->ic_stats.is_ccmp_replays++;
		return 1;
	}
	/* Last seen packet number is updated in ieee80211_inputm(). */

	/* Strip MIC. IV will be stripped by ieee80211_inputm(). */
	m_adj(m, -IEEE80211_CCMP_MICLEN);
	return 0;
}

void
wpi_rx_done(struct wpi_softc *sc, struct wpi_rx_desc *desc,
    struct wpi_rx_data *data, struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct wpi_rx_ring *ring = &sc->rxq;
	struct wpi_rx_stat *stat;
	struct wpi_rx_head *head;
	struct wpi_rx_tail *tail;
	struct ieee80211_frame *wh;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_node *ni;
	struct mbuf *m, *m1;
	uint32_t flags;
	int error;

	bus_dmamap_sync(sc->sc_dmat, data->map, 0, WPI_RBUF_SIZE,
	    BUS_DMASYNC_POSTREAD);
	stat = (struct wpi_rx_stat *)(desc + 1);

	if (stat->len > WPI_STAT_MAXLEN) {
		printf("%s: invalid RX statistic header\n",
		    sc->sc_dev.dv_xname);
		ifp->if_ierrors++;
		return;
	}
	head = (struct wpi_rx_head *)((caddr_t)(stat + 1) + stat->len);
	tail = (struct wpi_rx_tail *)((caddr_t)(head + 1) + letoh16(head->len));
	flags = letoh32(tail->flags);

	/* Discard frames with a bad FCS early. */
	if ((flags & WPI_RX_NOERROR) != WPI_RX_NOERROR) {
		DPRINTFN(2, ("rx tail flags error %x\n", flags));
		ifp->if_ierrors++;
		return;
	}
	/* Discard frames that are too short. */
	if (letoh16(head->len) < sizeof (*wh)) {
		DPRINTF(("frame too short: %d\n", letoh16(head->len)));
		ic->ic_stats.is_rx_tooshort++;
		ifp->if_ierrors++;
		return;
	}

	m1 = MCLGETL(NULL, M_DONTWAIT, WPI_RBUF_SIZE);
	if (m1 == NULL) {
		ic->ic_stats.is_rx_nombuf++;
		ifp->if_ierrors++;
		return;
	}
	bus_dmamap_unload(sc->sc_dmat, data->map);

	error = bus_dmamap_load(sc->sc_dmat, data->map, mtod(m1, void *),
	    WPI_RBUF_SIZE, NULL, BUS_DMA_NOWAIT | BUS_DMA_READ);
	if (error != 0) {
		m_freem(m1);

		/* Try to reload the old mbuf. */
		error = bus_dmamap_load(sc->sc_dmat, data->map,
		    mtod(data->m, void *), WPI_RBUF_SIZE, NULL,
		    BUS_DMA_NOWAIT | BUS_DMA_READ);
		if (error != 0) {
			panic("%s: could not load old RX mbuf",
			    sc->sc_dev.dv_xname);
		}
		/* Physical address may have changed. */
		ring->desc[ring->cur] = htole32(data->map->dm_segs[0].ds_addr);
		bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map,
		    ring->cur * sizeof (uint32_t), sizeof (uint32_t),
		    BUS_DMASYNC_PREWRITE);
		ifp->if_ierrors++;
		return;
	}

	m = data->m;
	data->m = m1;
	/* Update RX descriptor. */
	ring->desc[ring->cur] = htole32(data->map->dm_segs[0].ds_addr);
	bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map,
	    ring->cur * sizeof (uint32_t), sizeof (uint32_t),
	    BUS_DMASYNC_PREWRITE);

	/* Finalize mbuf. */
	m->m_data = (caddr_t)(head + 1);
	m->m_pkthdr.len = m->m_len = letoh16(head->len);

	/* Grab a reference to the source node. */
	wh = mtod(m, struct ieee80211_frame *);
	ni = ieee80211_find_rxnode(ic, wh);

	memset(&rxi, 0, sizeof(rxi));
	if ((wh->i_fc[1] & IEEE80211_FC1_PROTECTED) &&
	    !IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    (ni->ni_flags & IEEE80211_NODE_RXPROT) &&
	    ni->ni_pairwise_key.k_cipher == IEEE80211_CIPHER_CCMP) {
		if ((flags & WPI_RX_CIPHER_MASK) != WPI_RX_CIPHER_CCMP) {
			ic->ic_stats.is_ccmp_dec_errs++;
			ifp->if_ierrors++;
			m_freem(m);
			ieee80211_release_node(ic, ni);
			return;
		}
		/* Check whether decryption was successful or not. */
		if ((flags & WPI_RX_DECRYPT_MASK) != WPI_RX_DECRYPT_OK) {
			DPRINTF(("CCMP decryption failed 0x%x\n", flags));
			ic->ic_stats.is_ccmp_dec_errs++;
			ifp->if_ierrors++;
			m_freem(m);
			ieee80211_release_node(ic, ni);
			return;
		}
		if (wpi_ccmp_decap(sc, m, &ni->ni_pairwise_key) != 0) {
			ifp->if_ierrors++;
			m_freem(m);
			ieee80211_release_node(ic, ni);
			return;
		}
		rxi.rxi_flags |= IEEE80211_RXI_HWDEC;
	}

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct wpi_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = 0;
		if (letoh16(head->flags) & 0x4)
			tap->wr_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
		tap->wr_chan_freq =
		    htole16(ic->ic_channels[head->chan].ic_freq);
		tap->wr_chan_flags =
		    htole16(ic->ic_channels[head->chan].ic_flags);
		tap->wr_dbm_antsignal = (int8_t)(stat->rssi - WPI_RSSI_OFFSET);
		tap->wr_dbm_antnoise = (int8_t)letoh16(stat->noise);
		tap->wr_tsft = tail->tstamp;
		tap->wr_antenna = (letoh16(head->flags) >> 4) & 0xf;
		switch (head->rate) {
		/* CCK rates. */
		case  10: tap->wr_rate =   2; break;
		case  20: tap->wr_rate =   4; break;
		case  55: tap->wr_rate =  11; break;
		case 110: tap->wr_rate =  22; break;
		/* OFDM rates. */
		case 0xd: tap->wr_rate =  12; break;
		case 0xf: tap->wr_rate =  18; break;
		case 0x5: tap->wr_rate =  24; break;
		case 0x7: tap->wr_rate =  36; break;
		case 0x9: tap->wr_rate =  48; break;
		case 0xb: tap->wr_rate =  72; break;
		case 0x1: tap->wr_rate =  96; break;
		case 0x3: tap->wr_rate = 108; break;
		/* Unknown rate: should not happen. */
		default:  tap->wr_rate =   0;
		}

		bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_rxtap_len,
		    m, BPF_DIRECTION_IN);
	}
#endif

	/* Send the frame to the 802.11 layer. */
	rxi.rxi_rssi = stat->rssi;
	ieee80211_inputm(ifp, m, ni, &rxi, ml);

	/* Node is no longer needed. */
	ieee80211_release_node(ic, ni);
}

void
wpi_tx_done(struct wpi_softc *sc, struct wpi_rx_desc *desc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct wpi_tx_ring *ring = &sc->txq[desc->qid & 0x3];
	struct wpi_tx_data *data = &ring->data[desc->idx];
	struct wpi_tx_stat *stat = (struct wpi_tx_stat *)(desc + 1);
	struct wpi_node *wn = (struct wpi_node *)data->ni;

	/* Update rate control statistics. */
	wn->amn.amn_txcnt++;
	if (stat->retrycnt > 0)
		wn->amn.amn_retrycnt++;

	if ((letoh32(stat->status) & 0xff) != 1)
		ifp->if_oerrors++;

	/* Unmap and free mbuf. */
	bus_dmamap_sync(sc->sc_dmat, data->map, 0, data->map->dm_mapsize,
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_dmat, data->map);
	m_freem(data->m);
	data->m = NULL;
	ieee80211_release_node(ic, data->ni);
	data->ni = NULL;

	sc->sc_tx_timer = 0;
	if (--ring->queued < WPI_TX_RING_LOMARK) {
		sc->qfullmsk &= ~(1 << ring->qid);
		if (sc->qfullmsk == 0 && ifq_is_oactive(&ifp->if_snd)) {
			ifq_clr_oactive(&ifp->if_snd);
			(*ifp->if_start)(ifp);
		}
	}
}

void
wpi_cmd_done(struct wpi_softc *sc, struct wpi_rx_desc *desc)
{
	struct wpi_tx_ring *ring = &sc->txq[4];
	struct wpi_tx_data *data;

	if ((desc->qid & 7) != 4)
		return;	/* Not a command ack. */

	data = &ring->data[desc->idx];

	/* If the command was mapped in an mbuf, free it. */
	if (data->m != NULL) {
		bus_dmamap_sync(sc->sc_dmat, data->map, 0,
		    data->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, data->map);
		m_freem(data->m);
		data->m = NULL;
	}
	wakeup(&ring->cmd[desc->idx]);
}

void
wpi_notif_intr(struct wpi_softc *sc)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	uint32_t hw;

	bus_dmamap_sync(sc->sc_dmat, sc->shared_dma.map, 0,
	    sizeof (struct wpi_shared), BUS_DMASYNC_POSTREAD);

	hw = letoh32(sc->shared->next);
	while (sc->rxq.cur != hw) {
		struct wpi_rx_data *data = &sc->rxq.data[sc->rxq.cur];
		struct wpi_rx_desc *desc;

		bus_dmamap_sync(sc->sc_dmat, data->map, 0, sizeof (*desc),
		    BUS_DMASYNC_POSTREAD);
		desc = mtod(data->m, struct wpi_rx_desc *);

		DPRINTFN(4, ("rx notification qid=%x idx=%d flags=%x type=%d "
		    "len=%d\n", desc->qid, desc->idx, desc->flags, desc->type,
		    letoh32(desc->len)));

		if (!(desc->qid & 0x80))	/* Reply to a command. */
			wpi_cmd_done(sc, desc);

		switch (desc->type) {
		case WPI_RX_DONE:
			/* An 802.11 frame has been received. */
			wpi_rx_done(sc, desc, data, &ml);
			break;

		case WPI_TX_DONE:
			/* An 802.11 frame has been transmitted. */
			wpi_tx_done(sc, desc);
			break;

		case WPI_UC_READY:
		{
			struct wpi_ucode_info *uc =
			    (struct wpi_ucode_info *)(desc + 1);

			/* The microcontroller is ready. */
			bus_dmamap_sync(sc->sc_dmat, data->map, sizeof (*desc),
			    sizeof (*uc), BUS_DMASYNC_POSTREAD);
			DPRINTF(("microcode alive notification version %x "
			    "alive %x\n", letoh32(uc->version),
			    letoh32(uc->valid)));

			if (letoh32(uc->valid) != 1) {
				printf("%s: microcontroller initialization "
				    "failed\n", sc->sc_dev.dv_xname);
			}
			if (uc->subtype != WPI_UCODE_INIT) {
				/* Save the address of the error log. */
				sc->errptr = letoh32(uc->errptr);
			}
			break;
		}
		case WPI_STATE_CHANGED:
		{
			uint32_t *status = (uint32_t *)(desc + 1);

			/* Enabled/disabled notification. */
			bus_dmamap_sync(sc->sc_dmat, data->map, sizeof (*desc),
			    sizeof (*status), BUS_DMASYNC_POSTREAD);
			DPRINTF(("state changed to %x\n", letoh32(*status)));

			if (letoh32(*status) & 1) {
				/* The radio button has to be pushed. */
				printf("%s: Radio transmitter is off\n",
				    sc->sc_dev.dv_xname);
				/* Turn the interface down. */
				wpi_stop(ifp, 1);
				return;	/* No further processing. */
			}
			break;
		}
		case WPI_START_SCAN:
		{
			struct wpi_start_scan *scan =
			    (struct wpi_start_scan *)(desc + 1);

			bus_dmamap_sync(sc->sc_dmat, data->map, sizeof (*desc),
			    sizeof (*scan), BUS_DMASYNC_POSTREAD);
			DPRINTFN(2, ("scanning channel %d status %x\n",
			    scan->chan, letoh32(scan->status)));

			/* Fix current channel. */
			ic->ic_bss->ni_chan = &ic->ic_channels[scan->chan];
			break;
		}
		case WPI_STOP_SCAN:
		{
			struct wpi_stop_scan *scan =
			    (struct wpi_stop_scan *)(desc + 1);

			bus_dmamap_sync(sc->sc_dmat, data->map, sizeof (*desc),
			    sizeof (*scan), BUS_DMASYNC_POSTREAD);
			DPRINTF(("scan finished nchan=%d status=%d chan=%d\n",
			    scan->nchan, scan->status, scan->chan));

			if (scan->status == 1 && scan->chan <= 14 &&
			    (sc->sc_flags & WPI_FLAG_HAS_5GHZ)) {
				/*
				 * We just finished scanning 2GHz channels,
				 * start scanning 5GHz ones.
				 */
				if (wpi_scan(sc, IEEE80211_CHAN_5GHZ) == 0)
					break;
			}
			ieee80211_end_scan(ifp);
			break;
		}
		}

		sc->rxq.cur = (sc->rxq.cur + 1) % WPI_RX_RING_COUNT;
	}
	if_input(&ic->ic_if, &ml);

	/* Tell the firmware what we have processed. */
	hw = (hw == 0) ? WPI_RX_RING_COUNT - 1 : hw - 1;
	WPI_WRITE(sc, WPI_FH_RX_WPTR, hw & ~7);
}

#ifdef WPI_DEBUG
/*
 * Dump the error log of the firmware when a firmware panic occurs.  Although
 * we can't debug the firmware because it is neither open source nor free, it
 * can help us to identify certain classes of problems.
 */
void
wpi_fatal_intr(struct wpi_softc *sc)
{
#define N(a)	(sizeof (a) / sizeof ((a)[0]))
	struct wpi_fwdump dump;
	uint32_t i, offset, count;

	/* Check that the error log address is valid. */
	if (sc->errptr < WPI_FW_DATA_BASE ||
	    sc->errptr + sizeof (dump) >
	    WPI_FW_DATA_BASE + WPI_FW_DATA_MAXSZ) {
		printf("%s: bad firmware error log address 0x%08x\n",
		    sc->sc_dev.dv_xname, sc->errptr);
		return;
	}

	if (wpi_nic_lock(sc) != 0) {
		printf("%s: could not read firmware error log\n",
		    sc->sc_dev.dv_xname);
		return;
	}
	/* Read number of entries in the log. */
	count = wpi_mem_read(sc, sc->errptr);
	if (count == 0 || count * sizeof (dump) > WPI_FW_DATA_MAXSZ) {
		printf("%s: invalid count field (count=%u)\n",
		    sc->sc_dev.dv_xname, count);
		wpi_nic_unlock(sc);
		return;
	}
	/* Skip "count" field. */
	offset = sc->errptr + sizeof (uint32_t);
	printf("firmware error log (count=%u):\n", count);
	for (i = 0; i < count; i++) {
		wpi_mem_read_region_4(sc, offset, (uint32_t *)&dump,
		    sizeof (dump) / sizeof (uint32_t));

		printf("  error type = \"%s\" (0x%08X)\n",
		    (dump.desc < N(wpi_fw_errmsg)) ?
			wpi_fw_errmsg[dump.desc] : "UNKNOWN",
		    dump.desc);
		printf("  error data      = 0x%08X\n",
		    dump.data);
		printf("  branch link     = 0x%08X%08X\n",
		    dump.blink[0], dump.blink[1]);
		printf("  interrupt link  = 0x%08X%08X\n",
		    dump.ilink[0], dump.ilink[1]);
		printf("  time            = %u\n", dump.time);

		offset += sizeof (dump);
	}
	wpi_nic_unlock(sc);
	/* Dump driver status (TX and RX rings) while we're here. */
	printf("driver status:\n");
	for (i = 0; i < 6; i++) {
		struct wpi_tx_ring *ring = &sc->txq[i];
		printf("  tx ring %2d: qid=%-2d cur=%-3d queued=%-3d\n",
		    i, ring->qid, ring->cur, ring->queued);
	}
	printf("  rx ring: cur=%d\n", sc->rxq.cur);
	printf("  802.11 state %d\n", sc->sc_ic.ic_state);
#undef N
}
#endif

int
wpi_intr(void *arg)
{
	struct wpi_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	uint32_t r1, r2;

	/* Disable interrupts. */
	WPI_WRITE(sc, WPI_MASK, 0);

	r1 = WPI_READ(sc, WPI_INT);
	r2 = WPI_READ(sc, WPI_FH_INT);

	if (r1 == 0 && r2 == 0) {
		if (ifp->if_flags & IFF_UP)
			WPI_WRITE(sc, WPI_MASK, WPI_INT_MASK);
		return 0;	/* Interrupt not for us. */
	}
	if (r1 == 0xffffffff || (r1 & 0xfffffff0) == 0xa5a5a5a0)
		return 0;	/* Hardware gone! */

	/* Acknowledge interrupts. */
	WPI_WRITE(sc, WPI_INT, r1);
	WPI_WRITE(sc, WPI_FH_INT, r2);

	if (r1 & (WPI_INT_SW_ERR | WPI_INT_HW_ERR)) {
		printf("%s: fatal firmware error\n", sc->sc_dev.dv_xname);
		/* Dump firmware error log and stop. */
#ifdef WPI_DEBUG
		wpi_fatal_intr(sc);
#endif
		wpi_stop(ifp, 1);
		task_add(systq, &sc->init_task);
		return 1;
	}
	if ((r1 & (WPI_INT_FH_RX | WPI_INT_SW_RX)) ||
	    (r2 & WPI_FH_INT_RX))
		wpi_notif_intr(sc);

	if (r1 & WPI_INT_ALIVE)
		wakeup(sc);	/* Firmware is alive. */

	/* Re-enable interrupts. */
	if (ifp->if_flags & IFF_UP)
		WPI_WRITE(sc, WPI_MASK, WPI_INT_MASK);

	return 1;
}

int
wpi_tx(struct wpi_softc *sc, struct mbuf *m, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct wpi_node *wn = (void *)ni;
	struct wpi_tx_ring *ring;
	struct wpi_tx_desc *desc;
	struct wpi_tx_data *data;
	struct wpi_tx_cmd *cmd;
	struct wpi_cmd_data *tx;
	const struct wpi_rate *rinfo;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k = NULL;
	enum ieee80211_edca_ac ac;
	uint32_t flags;
	uint16_t qos;
	u_int hdrlen;
	uint8_t *ivp, tid, ridx, type;
	int i, totlen, hasqos, error;

	wh = mtod(m, struct ieee80211_frame *);
	hdrlen = ieee80211_get_hdrlen(wh);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

	/* Select EDCA Access Category and TX ring for this frame. */
	if ((hasqos = ieee80211_has_qos(wh))) {
		qos = ieee80211_get_qos(wh);
		tid = qos & IEEE80211_QOS_TID;
		ac = ieee80211_up_to_ac(ic, tid);
	} else {
		tid = 0;
		ac = EDCA_AC_BE;
	}

	ring = &sc->txq[ac];
	desc = &ring->desc[ring->cur];
	data = &ring->data[ring->cur];

	/* Choose a TX rate index. */
	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    type != IEEE80211_FC0_TYPE_DATA) {
		ridx = (ic->ic_curmode == IEEE80211_MODE_11A) ?
		    WPI_RIDX_OFDM6 : WPI_RIDX_CCK1;
	} else if (ic->ic_fixed_rate != -1) {
		ridx = sc->fixed_ridx;
	} else
		ridx = wn->ridx[ni->ni_txrate];
	rinfo = &wpi_rates[ridx];

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct wpi_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_chan_freq = htole16(ni->ni_chan->ic_freq);
		tap->wt_chan_flags = htole16(ni->ni_chan->ic_flags);
		tap->wt_rate = rinfo->rate;
		if ((ic->ic_flags & IEEE80211_F_WEPON) &&
		    (wh->i_fc[1] & IEEE80211_FC1_PROTECTED))
			tap->wt_flags |= IEEE80211_RADIOTAP_F_WEP;

		bpf_mtap_hdr(sc->sc_drvbpf, tap, sc->sc_txtap_len,
		    m, BPF_DIRECTION_OUT);
	}
#endif

	totlen = m->m_pkthdr.len;

	/* Encrypt the frame if need be. */
	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		/* Retrieve key for TX. */
		k = ieee80211_get_txkey(ic, wh, ni);
		if (k->k_cipher != IEEE80211_CIPHER_CCMP) {
			/* Do software encryption. */
			if ((m = ieee80211_encrypt(ic, m, k)) == NULL)
				return ENOBUFS;
			/* 802.11 header may have moved. */
			wh = mtod(m, struct ieee80211_frame *);
			totlen = m->m_pkthdr.len;

		} else	/* HW appends CCMP MIC. */
			totlen += IEEE80211_CCMP_HDRLEN;
	}

	/* Prepare TX firmware command. */
	cmd = &ring->cmd[ring->cur];
	cmd->code = WPI_CMD_TX_DATA;
	cmd->flags = 0;
	cmd->qid = ring->qid;
	cmd->idx = ring->cur;

	tx = (struct wpi_cmd_data *)cmd->data;
	/* NB: No need to clear tx, all fields are reinitialized here. */

	flags = 0;
	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		/* Unicast frame, check if an ACK is expected. */
		if (!hasqos || (qos & IEEE80211_QOS_ACK_POLICY_MASK) !=
		    IEEE80211_QOS_ACK_POLICY_NOACK)
			flags |= WPI_TX_NEED_ACK;
	}

	/* Check if frame must be protected using RTS/CTS or CTS-to-self. */
	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		/* NB: Group frames are sent using CCK in 802.11b/g. */
		if (totlen + IEEE80211_CRC_LEN > ic->ic_rtsthreshold) {
			flags |= WPI_TX_NEED_RTS | WPI_TX_FULL_TXOP;
		} else if ((ic->ic_flags & IEEE80211_F_USEPROT) &&
		    ridx <= WPI_RIDX_OFDM54) {
			if (ic->ic_protmode == IEEE80211_PROT_CTSONLY)
				flags |= WPI_TX_NEED_CTS | WPI_TX_FULL_TXOP;
			else if (ic->ic_protmode == IEEE80211_PROT_RTSCTS)
				flags |= WPI_TX_NEED_RTS | WPI_TX_FULL_TXOP;
		}
	}

	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    type != IEEE80211_FC0_TYPE_DATA)
		tx->id = WPI_ID_BROADCAST;
	else
		tx->id = wn->id;

	if (type == IEEE80211_FC0_TYPE_MGT) {
		uint8_t subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

#ifndef IEEE80211_STA_ONLY
		/* Tell HW to set timestamp in probe responses. */
		if (subtype == IEEE80211_FC0_SUBTYPE_PROBE_RESP)
			flags |= WPI_TX_INSERT_TSTAMP;
#endif
		if (subtype == IEEE80211_FC0_SUBTYPE_ASSOC_REQ ||
		    subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ)
			tx->timeout = htole16(3);
		else
			tx->timeout = htole16(2);
	} else
		tx->timeout = htole16(0);

	tx->len = htole16(totlen);
	tx->tid = tid;
	tx->rts_ntries = 7;
	tx->data_ntries = 15;
	tx->ofdm_mask = 0xff;
	tx->cck_mask = 0x0f;
	tx->lifetime = htole32(WPI_LIFETIME_INFINITE);
	tx->plcp = rinfo->plcp;

	/* Copy 802.11 header in TX command. */
	memcpy((uint8_t *)(tx + 1), wh, hdrlen);

	if (k != NULL && k->k_cipher == IEEE80211_CIPHER_CCMP) {
		/* Trim 802.11 header and prepend CCMP IV. */
		m_adj(m, hdrlen - IEEE80211_CCMP_HDRLEN);
		ivp = mtod(m, uint8_t *);
		k->k_tsc++;
		ivp[0] = k->k_tsc;
		ivp[1] = k->k_tsc >> 8;
		ivp[2] = 0;
		ivp[3] = k->k_id << 6 | IEEE80211_WEP_EXTIV;
		ivp[4] = k->k_tsc >> 16;
		ivp[5] = k->k_tsc >> 24;
		ivp[6] = k->k_tsc >> 32;
		ivp[7] = k->k_tsc >> 40;

		tx->security = WPI_CIPHER_CCMP;
		memcpy(tx->key, k->k_key, k->k_len);
	} else {
		/* Trim 802.11 header. */
		m_adj(m, hdrlen);
		tx->security = 0;
	}
	tx->flags = htole32(flags);

	error = bus_dmamap_load_mbuf(sc->sc_dmat, data->map, m,
	    BUS_DMA_NOWAIT | BUS_DMA_WRITE);
	if (error != 0 && error != EFBIG) {
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

	data->m = m;
	data->ni = ni;

	DPRINTFN(4, ("sending data: qid=%d idx=%d len=%d nsegs=%d\n",
	    ring->qid, ring->cur, m->m_pkthdr.len, data->map->dm_nsegs));

	/* Fill TX descriptor. */
	desc->flags = htole32(WPI_PAD32(m->m_pkthdr.len) << 28 |
	    (1 + data->map->dm_nsegs) << 24);
	/* First DMA segment is used by the TX command. */
	desc->segs[0].addr = htole32(ring->cmd_dma.paddr +
	    ring->cur * sizeof (struct wpi_tx_cmd));
	desc->segs[0].len  = htole32(4 + sizeof (struct wpi_cmd_data) +
	    ((hdrlen + 3) & ~3));
	/* Other DMA segments are for data payload. */
	for (i = 1; i <= data->map->dm_nsegs; i++) {
		desc->segs[i].addr =
		    htole32(data->map->dm_segs[i - 1].ds_addr);
		desc->segs[i].len  =
		    htole32(data->map->dm_segs[i - 1].ds_len);
	}

	bus_dmamap_sync(sc->sc_dmat, data->map, 0, data->map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, ring->cmd_dma.map,
	    (caddr_t)cmd - ring->cmd_dma.vaddr, sizeof (*cmd),
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map,
	    (caddr_t)desc - ring->desc_dma.vaddr, sizeof (*desc),
	    BUS_DMASYNC_PREWRITE);

	/* Kick TX ring. */
	ring->cur = (ring->cur + 1) % WPI_TX_RING_COUNT;
	WPI_WRITE(sc, WPI_HBUS_TARG_WRPTR, ring->qid << 8 | ring->cur);

	/* Mark TX ring as full if we reach a certain threshold. */
	if (++ring->queued > WPI_TX_RING_HIMARK)
		sc->qfullmsk |= 1 << ring->qid;

	return 0;
}

void
wpi_start(struct ifnet *ifp)
{
	struct wpi_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct mbuf *m;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		if (sc->qfullmsk != 0) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}
		/* Send pending management frames first. */
		m = mq_dequeue(&ic->ic_mgtq);
		if (m != NULL) {
			ni = m->m_pkthdr.ph_cookie;
			goto sendit;
		}
		if (ic->ic_state != IEEE80211_S_RUN)
			break;

		/* Encapsulate and send data frames. */
		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;
#if NBPFILTER > 0
		if (ifp->if_bpf != NULL)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
		if ((m = ieee80211_encap(ifp, m, &ni)) == NULL)
			continue;
sendit:
#if NBPFILTER > 0
		if (ic->ic_rawbpf != NULL)
			bpf_mtap(ic->ic_rawbpf, m, BPF_DIRECTION_OUT);
#endif
		if (wpi_tx(sc, m, ni) != 0) {
			ieee80211_release_node(ic, ni);
			ifp->if_oerrors++;
			continue;
		}

		sc->sc_tx_timer = 5;
		ifp->if_timer = 1;
	}
}

void
wpi_watchdog(struct ifnet *ifp)
{
	struct wpi_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			printf("%s: device timeout\n", sc->sc_dev.dv_xname);
			wpi_stop(ifp, 1);
			ifp->if_oerrors++;
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}

int
wpi_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct wpi_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
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
				error = wpi_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				wpi_stop(ifp, 1);
		}
		break;

	case SIOCS80211POWER:
		error = ieee80211_ioctl(ifp, cmd, data);
		if (error != ENETRESET)
			break;
		if (ic->ic_state == IEEE80211_S_RUN) {
			if (ic->ic_flags & IEEE80211_F_PMGTON)
				error = wpi_set_pslevel(sc, 0, 3, 0);
			else	/* back to CAM */
				error = wpi_set_pslevel(sc, 0, 0, 0);
		} else {
			/* Defer until transition to IEEE80211_S_RUN. */
			error = 0;
		}
		break;

	default:
		error = ieee80211_ioctl(ifp, cmd, data);
	}

	if (error == ENETRESET) {
		error = 0;
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING)) {
			wpi_stop(ifp, 0);
			error = wpi_init(ifp);
		}
	}

	splx(s);
	rw_exit_write(&sc->sc_rwlock);
	return error;
}

/*
 * Send a command to the firmware.
 */
int
wpi_cmd(struct wpi_softc *sc, int code, const void *buf, int size, int async)
{
	struct wpi_tx_ring *ring = &sc->txq[4];
	struct wpi_tx_desc *desc;
	struct wpi_tx_data *data;
	struct wpi_tx_cmd *cmd;
	struct mbuf *m;
	bus_addr_t paddr;
	int totlen, error;

	desc = &ring->desc[ring->cur];
	data = &ring->data[ring->cur];
	totlen = 4 + size;

	if (size > sizeof cmd->data) {
		/* Command is too large to fit in a descriptor. */
		if (totlen > MCLBYTES)
			return EINVAL;
		MGETHDR(m, M_DONTWAIT, MT_DATA);
		if (m == NULL)
			return ENOMEM;
		if (totlen > MHLEN) {
			MCLGET(m, M_DONTWAIT);
			if (!(m->m_flags & M_EXT)) {
				m_freem(m);
				return ENOMEM;
			}
		}
		cmd = mtod(m, struct wpi_tx_cmd *);
		error = bus_dmamap_load(sc->sc_dmat, data->map, cmd, totlen,
		    NULL, BUS_DMA_NOWAIT | BUS_DMA_WRITE);
		if (error != 0) {
			m_freem(m);
			return error;
		}
		data->m = m;
		paddr = data->map->dm_segs[0].ds_addr;
	} else {
		cmd = &ring->cmd[ring->cur];
		paddr = data->cmd_paddr;
	}

	cmd->code = code;
	cmd->flags = 0;
	cmd->qid = ring->qid;
	cmd->idx = ring->cur;
	memcpy(cmd->data, buf, size);

	desc->flags = htole32(WPI_PAD32(size) << 28 | 1 << 24);
	desc->segs[0].addr = htole32(paddr);
	desc->segs[0].len  = htole32(totlen);

	if (size > sizeof cmd->data) {
		bus_dmamap_sync(sc->sc_dmat, data->map, 0, totlen,
		    BUS_DMASYNC_PREWRITE);
	} else {
		bus_dmamap_sync(sc->sc_dmat, ring->cmd_dma.map,
		    (caddr_t)cmd - ring->cmd_dma.vaddr, totlen,
		    BUS_DMASYNC_PREWRITE);
	}
	bus_dmamap_sync(sc->sc_dmat, ring->desc_dma.map,
	    (caddr_t)desc - ring->desc_dma.vaddr, sizeof (*desc),
	    BUS_DMASYNC_PREWRITE);

	/* Kick command ring. */
	ring->cur = (ring->cur + 1) % WPI_TX_RING_COUNT;
	WPI_WRITE(sc, WPI_HBUS_TARG_WRPTR, ring->qid << 8 | ring->cur);

	return async ? 0 : tsleep_nsec(cmd, PCATCH, "wpicmd", SEC_TO_NSEC(1));
}

/*
 * Configure HW multi-rate retries.
 */
int
wpi_mrr_setup(struct wpi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct wpi_mrr_setup mrr;
	int i, error;

	/* CCK rates (not used with 802.11a). */
	for (i = WPI_RIDX_CCK1; i <= WPI_RIDX_CCK11; i++) {
		mrr.rates[i].flags = 0;
		mrr.rates[i].plcp = wpi_rates[i].plcp;
		/* Fallback to the immediate lower CCK rate (if any.) */
		mrr.rates[i].next =
		    (i == WPI_RIDX_CCK1) ? WPI_RIDX_CCK1 : i - 1;
		/* Try one time at this rate before falling back to "next". */
		mrr.rates[i].ntries = 1;
	}
	/* OFDM rates (not used with 802.11b). */
	for (i = WPI_RIDX_OFDM6; i <= WPI_RIDX_OFDM54; i++) {
		mrr.rates[i].flags = 0;
		mrr.rates[i].plcp = wpi_rates[i].plcp;
		/* Fallback to the immediate lower rate (if any.) */
		/* We allow fallback from OFDM/6 to CCK/2 in 11b/g mode. */
		mrr.rates[i].next = (i == WPI_RIDX_OFDM6) ?
		    ((ic->ic_curmode == IEEE80211_MODE_11A) ?
			WPI_RIDX_OFDM6 : WPI_RIDX_CCK2) :
		    i - 1;
		/* Try one time at this rate before falling back to "next". */
		mrr.rates[i].ntries = 1;
	}
	/* Setup MRR for control frames. */
	mrr.which = htole32(WPI_MRR_CTL);
	error = wpi_cmd(sc, WPI_CMD_MRR_SETUP, &mrr, sizeof mrr, 0);
	if (error != 0) {
		printf("%s: could not setup MRR for control frames\n",
		    sc->sc_dev.dv_xname);
		return error;
	}
	/* Setup MRR for data frames. */
	mrr.which = htole32(WPI_MRR_DATA);
	error = wpi_cmd(sc, WPI_CMD_MRR_SETUP, &mrr, sizeof mrr, 0);
	if (error != 0) {
		printf("%s: could not setup MRR for data frames\n",
		    sc->sc_dev.dv_xname);
		return error;
	}
	return 0;
}

void
wpi_updateedca(struct ieee80211com *ic)
{
#define WPI_EXP2(x)	((1 << (x)) - 1)	/* CWmin = 2^ECWmin - 1 */
	struct wpi_softc *sc = ic->ic_softc;
	struct wpi_edca_params cmd;
	int aci;

	memset(&cmd, 0, sizeof cmd);
	cmd.flags = htole32(WPI_EDCA_UPDATE);
	for (aci = 0; aci < EDCA_NUM_AC; aci++) {
		const struct ieee80211_edca_ac_params *ac =
		    &ic->ic_edca_ac[aci];
		cmd.ac[aci].aifsn = ac->ac_aifsn;
		cmd.ac[aci].cwmin = htole16(WPI_EXP2(ac->ac_ecwmin));
		cmd.ac[aci].cwmax = htole16(WPI_EXP2(ac->ac_ecwmax));
		cmd.ac[aci].txoplimit =
		    htole16(IEEE80211_TXOP_TO_US(ac->ac_txoplimit));
	}
	(void)wpi_cmd(sc, WPI_CMD_EDCA_PARAMS, &cmd, sizeof cmd, 1);
#undef WPI_EXP2
}

void
wpi_set_led(struct wpi_softc *sc, uint8_t which, uint8_t off, uint8_t on)
{
	struct wpi_cmd_led led;

	led.which = which;
	led.unit = htole32(100000);	/* on/off in unit of 100ms */
	led.off = off;
	led.on = on;
	(void)wpi_cmd(sc, WPI_CMD_SET_LED, &led, sizeof led, 1);
}

int
wpi_set_timing(struct wpi_softc *sc, struct ieee80211_node *ni)
{
	struct wpi_cmd_timing cmd;
	uint64_t val, mod;

	memset(&cmd, 0, sizeof cmd);
	memcpy(&cmd.tstamp, ni->ni_tstamp, sizeof (uint64_t));
	cmd.bintval = htole16(ni->ni_intval);
	cmd.lintval = htole16(10);

	/* Compute remaining time until next beacon. */
	val = (uint64_t)ni->ni_intval * 1024;	/* msecs -> usecs */
	mod = letoh64(cmd.tstamp) % val;
	cmd.binitval = htole32((uint32_t)(val - mod));

	DPRINTF(("timing bintval=%u, tstamp=%llu, init=%u\n",
	    ni->ni_intval, letoh64(cmd.tstamp), (uint32_t)(val - mod)));

	return wpi_cmd(sc, WPI_CMD_TIMING, &cmd, sizeof cmd, 1);
}

/*
 * This function is called periodically (every minute) to adjust TX power
 * based on temperature variation.
 */
void
wpi_power_calibration(struct wpi_softc *sc)
{
	int temp;

	temp = (int)WPI_READ(sc, WPI_UCODE_GP2);
	/* Sanity-check temperature. */
	if (temp < -260 || temp > 25) {
		/* This can't be correct, ignore. */
		DPRINTF(("out-of-range temperature reported: %d\n", temp));
		return;
	}
	DPRINTF(("temperature %d->%d\n", sc->temp, temp));
	/* Adjust TX power if need be (delta > 6). */
	if (abs(temp - sc->temp) > 6) {
		/* Record temperature of last calibration. */
		sc->temp = temp;
		(void)wpi_set_txpower(sc, 1);
	}
}

/*
 * Set TX power for current channel (each rate has its own power settings).
 */
int
wpi_set_txpower(struct wpi_softc *sc, int async)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *ch;
	struct wpi_power_group *group;
	struct wpi_cmd_txpower cmd;
	u_int chan;
	int idx, i;

	/* Retrieve current channel from last RXON. */
	chan = sc->rxon.chan;
	DPRINTF(("setting TX power for channel %d\n", chan));
	ch = &ic->ic_channels[chan];

	/* Find the TX power group to which this channel belongs. */
	if (IEEE80211_IS_CHAN_5GHZ(ch)) {
		for (group = &sc->groups[1]; group < &sc->groups[4]; group++)
			if (chan <= group->chan)
				break;
	} else
		group = &sc->groups[0];

	memset(&cmd, 0, sizeof cmd);
	cmd.band = IEEE80211_IS_CHAN_5GHZ(ch) ? 0 : 1;
	cmd.chan = htole16(chan);

	/* Set TX power for all OFDM and CCK rates. */
	for (i = 0; i <= WPI_RIDX_MAX ; i++) {
		/* Retrieve TX power for this channel/rate. */
		idx = wpi_get_power_index(sc, group, ch, i);

		cmd.rates[i].plcp = wpi_rates[i].plcp;

		if (IEEE80211_IS_CHAN_5GHZ(ch)) {
			cmd.rates[i].rf_gain = wpi_rf_gain_5ghz[idx];
			cmd.rates[i].dsp_gain = wpi_dsp_gain_5ghz[idx];
		} else {
			cmd.rates[i].rf_gain = wpi_rf_gain_2ghz[idx];
			cmd.rates[i].dsp_gain = wpi_dsp_gain_2ghz[idx];
		}
		DPRINTF(("chan %d/rate %d: power index %d\n", chan,
		    wpi_rates[i].rate, idx));
	}
	return wpi_cmd(sc, WPI_CMD_TXPOWER, &cmd, sizeof cmd, async);
}

/*
 * Determine TX power index for a given channel/rate combination.
 * This takes into account the regulatory information from EEPROM and the
 * current temperature.
 */
int
wpi_get_power_index(struct wpi_softc *sc, struct wpi_power_group *group,
    struct ieee80211_channel *c, int ridx)
{
/* Fixed-point arithmetic division using a n-bit fractional part. */
#define fdivround(a, b, n)	\
	((((1 << n) * (a)) / (b) + (1 << n) / 2) / (1 << n))

/* Linear interpolation. */
#define interpolate(x, x1, y1, x2, y2, n)	\
	((y1) + fdivround(((x) - (x1)) * ((y2) - (y1)), (x2) - (x1), n))

	struct ieee80211com *ic = &sc->sc_ic;
	struct wpi_power_sample *sample;
	int pwr, idx;
	u_int chan;

	/* Get channel number. */
	chan = ieee80211_chan2ieee(ic, c);

	/* Default TX power is group maximum TX power minus 3dB. */
	pwr = group->maxpwr / 2;

	/* Decrease TX power for highest OFDM rates to reduce distortion. */
	switch (ridx) {
	case WPI_RIDX_OFDM36:
		pwr -= IEEE80211_IS_CHAN_2GHZ(c) ? 0 :  5;
		break;
	case WPI_RIDX_OFDM48:
		pwr -= IEEE80211_IS_CHAN_2GHZ(c) ? 7 : 10;
		break;
	case WPI_RIDX_OFDM54:
		pwr -= IEEE80211_IS_CHAN_2GHZ(c) ? 9 : 12;
		break;
	}

	/* Never exceed the channel maximum allowed TX power. */
	pwr = MIN(pwr, sc->maxpwr[chan]);

	/* Retrieve TX power index into gain tables from samples. */
	for (sample = group->samples; sample < &group->samples[3]; sample++)
		if (pwr > sample[1].power)
			break;
	/* Fixed-point linear interpolation using a 19-bit fractional part. */
	idx = interpolate(pwr, sample[0].power, sample[0].index,
	    sample[1].power, sample[1].index, 19);

	/*-
	 * Adjust power index based on current temperature:
	 * - if cooler than factory-calibrated: decrease output power
	 * - if warmer than factory-calibrated: increase output power
	 */
	idx -= (sc->temp - group->temp) * 11 / 100;

	/* Decrease TX power for CCK rates (-5dB). */
	if (ridx >= WPI_RIDX_CCK1)
		idx += 10;

	/* Make sure idx stays in a valid range. */
	if (idx < 0)
		idx = 0;
	else if (idx > WPI_MAX_PWR_INDEX)
		idx = WPI_MAX_PWR_INDEX;
	return idx;

#undef interpolate
#undef fdivround
}

/*
 * Set STA mode power saving level (between 0 and 5).
 * Level 0 is CAM (Continuously Aware Mode), 5 is for maximum power saving.
 */
int
wpi_set_pslevel(struct wpi_softc *sc, int dtim, int level, int async)
{
	struct wpi_pmgt_cmd cmd;
	const struct wpi_pmgt *pmgt;
	uint32_t max, skip_dtim;
	pcireg_t reg;
	int i;

	/* Select which PS parameters to use. */
	if (dtim <= 10)
		pmgt = &wpi_pmgt[0][level];
	else
		pmgt = &wpi_pmgt[1][level];

	memset(&cmd, 0, sizeof cmd);
	if (level != 0)	/* not CAM */
		cmd.flags |= htole16(WPI_PS_ALLOW_SLEEP);
	/* Retrieve PCIe Active State Power Management (ASPM). */
	reg = pci_conf_read(sc->sc_pct, sc->sc_pcitag,
	    sc->sc_cap_off + PCI_PCIE_LCSR);
	if (!(reg & PCI_PCIE_LCSR_ASPM_L0S))	/* L0s Entry disabled. */
		cmd.flags |= htole16(WPI_PS_PCI_PMGT);
	cmd.rxtimeout = htole32(pmgt->rxtimeout * 1024);
	cmd.txtimeout = htole32(pmgt->txtimeout * 1024);

	if (dtim == 0) {
		dtim = 1;
		skip_dtim = 0;
	} else
		skip_dtim = pmgt->skip_dtim;
	if (skip_dtim != 0) {
		cmd.flags |= htole16(WPI_PS_SLEEP_OVER_DTIM);
		max = pmgt->intval[4];
		if (max == (uint32_t)-1)
			max = dtim * (skip_dtim + 1);
		else if (max > dtim)
			max = (max / dtim) * dtim;
	} else
		max = dtim;
	for (i = 0; i < 5; i++)
		cmd.intval[i] = htole32(MIN(max, pmgt->intval[i]));

	DPRINTF(("setting power saving level to %d\n", level));
	return wpi_cmd(sc, WPI_CMD_SET_POWER_MODE, &cmd, sizeof cmd, async);
}

int
wpi_config(struct wpi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct wpi_bluetooth bluetooth;
	struct wpi_node_info node;
	int error;

	/* Set power saving level to CAM during initialization. */
	if ((error = wpi_set_pslevel(sc, 0, 0, 0)) != 0) {
		printf("%s: could not set power saving level\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	/* Configure bluetooth coexistence. */
	memset(&bluetooth, 0, sizeof bluetooth);
	bluetooth.flags = WPI_BT_COEX_MODE_4WIRE;
	bluetooth.lead_time = WPI_BT_LEAD_TIME_DEF;
	bluetooth.max_kill = WPI_BT_MAX_KILL_DEF;
	error = wpi_cmd(sc, WPI_CMD_BT_COEX, &bluetooth, sizeof bluetooth, 0);
	if (error != 0) {
		printf("%s: could not configure bluetooth coexistence\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	/* Configure adapter. */
	memset(&sc->rxon, 0, sizeof (struct wpi_rxon));
	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));
	IEEE80211_ADDR_COPY(sc->rxon.myaddr, ic->ic_myaddr);
	/* Set default channel. */
	sc->rxon.chan = ieee80211_chan2ieee(ic, ic->ic_ibss_chan);
	sc->rxon.flags = htole32(WPI_RXON_TSF);
	if (IEEE80211_IS_CHAN_2GHZ(ic->ic_ibss_chan))
		sc->rxon.flags |= htole32(WPI_RXON_AUTO | WPI_RXON_24GHZ);
	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		sc->rxon.mode = WPI_MODE_STA;
		sc->rxon.filter = htole32(WPI_FILTER_MULTICAST);
		break;
	case IEEE80211_M_MONITOR:
		sc->rxon.mode = WPI_MODE_MONITOR;
		sc->rxon.filter = htole32(WPI_FILTER_MULTICAST |
		    WPI_FILTER_CTL | WPI_FILTER_PROMISC);
		break;
	default:
		/* Should not get there. */
		break;
	}
	sc->rxon.cck_mask  = 0x0f;	/* not yet negotiated */
	sc->rxon.ofdm_mask = 0xff;	/* not yet negotiated */
	DPRINTF(("setting configuration\n"));
	error = wpi_cmd(sc, WPI_CMD_RXON, &sc->rxon, sizeof (struct wpi_rxon),
	    0);
	if (error != 0) {
		printf("%s: RXON command failed\n", sc->sc_dev.dv_xname);
		return error;
	}

	/* Configuration has changed, set TX power accordingly. */
	if ((error = wpi_set_txpower(sc, 0)) != 0) {
		printf("%s: could not set TX power\n", sc->sc_dev.dv_xname);
		return error;
	}

	/* Add broadcast node. */
	memset(&node, 0, sizeof node);
	IEEE80211_ADDR_COPY(node.macaddr, etherbroadcastaddr);
	node.id = WPI_ID_BROADCAST;
	node.plcp = wpi_rates[WPI_RIDX_CCK1].plcp;
	node.action = htole32(WPI_ACTION_SET_RATE);
	node.antenna = WPI_ANTENNA_BOTH;
	error = wpi_cmd(sc, WPI_CMD_ADD_NODE, &node, sizeof node, 0);
	if (error != 0) {
		printf("%s: could not add broadcast node\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	if ((error = wpi_mrr_setup(sc)) != 0) {
		printf("%s: could not setup MRR\n", sc->sc_dev.dv_xname);
		return error;
	}
	return 0;
}

int
wpi_scan(struct wpi_softc *sc, uint16_t flags)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct wpi_scan_hdr *hdr;
	struct wpi_cmd_data *tx;
	struct wpi_scan_essid *essid;
	struct wpi_scan_chan *chan;
	struct ieee80211_frame *wh;
	struct ieee80211_rateset *rs;
	struct ieee80211_channel *c;
	uint8_t *buf, *frm;
	int buflen, error;

	buf = malloc(WPI_SCAN_MAXSZ, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (buf == NULL) {
		printf("%s: could not allocate buffer for scan command\n",
		    sc->sc_dev.dv_xname);
		return ENOMEM;
	}
	hdr = (struct wpi_scan_hdr *)buf;
	/*
	 * Move to the next channel if no frames are received within 10ms
	 * after sending the probe request.
	 */
	hdr->quiet_time = htole16(10);		/* timeout in milliseconds */
	hdr->quiet_threshold = htole16(1);	/* min # of packets */

	tx = (struct wpi_cmd_data *)(hdr + 1);
	tx->flags = htole32(WPI_TX_AUTO_SEQ);
	tx->id = WPI_ID_BROADCAST;
	tx->lifetime = htole32(WPI_LIFETIME_INFINITE);

	if (flags & IEEE80211_CHAN_5GHZ) {
		hdr->crc_threshold = htole16(1);
		/* Send probe requests at 6Mbps. */
		tx->plcp = wpi_rates[WPI_RIDX_OFDM6].plcp;
		rs = &ic->ic_sup_rates[IEEE80211_MODE_11A];
	} else {
		hdr->flags = htole32(WPI_RXON_24GHZ | WPI_RXON_AUTO);
		/* Send probe requests at 1Mbps. */
		tx->plcp = wpi_rates[WPI_RIDX_CCK1].plcp;
		rs = &ic->ic_sup_rates[IEEE80211_MODE_11G];
	}

	essid = (struct wpi_scan_essid *)(tx + 1);
	if (ic->ic_des_esslen != 0) {
		essid[0].id  = IEEE80211_ELEMID_SSID;
		essid[0].len = ic->ic_des_esslen;
		memcpy(essid[0].data, ic->ic_des_essid, ic->ic_des_esslen);
	}
	/*
	 * Build a probe request frame.  Most of the following code is a
	 * copy & paste of what is done in net80211.
	 */
	wh = (struct ieee80211_frame *)(essid + 4);
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT |
	    IEEE80211_FC0_SUBTYPE_PROBE_REQ;
	wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	IEEE80211_ADDR_COPY(wh->i_addr1, etherbroadcastaddr);
	IEEE80211_ADDR_COPY(wh->i_addr2, ic->ic_myaddr);
	IEEE80211_ADDR_COPY(wh->i_addr3, etherbroadcastaddr);
	*(uint16_t *)&wh->i_dur[0] = 0;	/* filled by HW */
	*(uint16_t *)&wh->i_seq[0] = 0;	/* filled by HW */

	frm = (uint8_t *)(wh + 1);
	frm = ieee80211_add_ssid(frm, NULL, 0);
	frm = ieee80211_add_rates(frm, rs);
	if (rs->rs_nrates > IEEE80211_RATE_SIZE)
		frm = ieee80211_add_xrates(frm, rs);

	/* Set length of probe request. */
	tx->len = htole16(frm - (uint8_t *)wh);

	chan = (struct wpi_scan_chan *)frm;
	for (c  = &ic->ic_channels[1];
	     c <= &ic->ic_channels[IEEE80211_CHAN_MAX]; c++) {
		if ((c->ic_flags & flags) != flags)
			continue;

		chan->chan = ieee80211_chan2ieee(ic, c);
		DPRINTFN(2, ("adding channel %d\n", chan->chan));
		chan->flags = 0;
		if (!(c->ic_flags & IEEE80211_CHAN_PASSIVE))
			chan->flags |= WPI_CHAN_ACTIVE;
		if (ic->ic_des_esslen != 0)
			chan->flags |= WPI_CHAN_NPBREQS(1);
		chan->dsp_gain = 0x6e;
		if (IEEE80211_IS_CHAN_5GHZ(c)) {
			chan->rf_gain = 0x3b;
			chan->active  = htole16(24);
			chan->passive = htole16(110);
		} else {
			chan->rf_gain = 0x28;
			chan->active  = htole16(36);
			chan->passive = htole16(120);
		}
		hdr->nchan++;
		chan++;
	}

	buflen = (uint8_t *)chan - buf;
	hdr->len = htole16(buflen);

	DPRINTF(("sending scan command nchan=%d\n", hdr->nchan));
	error = wpi_cmd(sc, WPI_CMD_SCAN, buf, buflen, 1);
	free(buf, M_DEVBUF, WPI_SCAN_MAXSZ);
	return error;
}

int
wpi_auth(struct wpi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	struct wpi_node_info node;
	int error;

	/* Update adapter configuration. */
	IEEE80211_ADDR_COPY(sc->rxon.bssid, ni->ni_bssid);
	sc->rxon.chan = ieee80211_chan2ieee(ic, ni->ni_chan);
	sc->rxon.flags = htole32(WPI_RXON_TSF);
	if (IEEE80211_IS_CHAN_2GHZ(ni->ni_chan))
		sc->rxon.flags |= htole32(WPI_RXON_AUTO | WPI_RXON_24GHZ);
	if (ic->ic_flags & IEEE80211_F_SHSLOT)
		sc->rxon.flags |= htole32(WPI_RXON_SHSLOT);
	if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
		sc->rxon.flags |= htole32(WPI_RXON_SHPREAMBLE);
	switch (ic->ic_curmode) {
	case IEEE80211_MODE_11A:
		sc->rxon.cck_mask  = 0;
		sc->rxon.ofdm_mask = 0x15;
		break;
	case IEEE80211_MODE_11B:
		sc->rxon.cck_mask  = 0x03;
		sc->rxon.ofdm_mask = 0;
		break;
	default:	/* Assume 802.11b/g. */
		sc->rxon.cck_mask  = 0x0f;
		sc->rxon.ofdm_mask = 0x15;
	}
	DPRINTF(("rxon chan %d flags %x cck %x ofdm %x\n", sc->rxon.chan,
	    sc->rxon.flags, sc->rxon.cck_mask, sc->rxon.ofdm_mask));
	error = wpi_cmd(sc, WPI_CMD_RXON, &sc->rxon, sizeof (struct wpi_rxon),
	    1);
	if (error != 0) {
		printf("%s: RXON command failed\n", sc->sc_dev.dv_xname);
		return error;
	}

	/* Configuration has changed, set TX power accordingly. */
	if ((error = wpi_set_txpower(sc, 1)) != 0) {
		printf("%s: could not set TX power\n", sc->sc_dev.dv_xname);
		return error;
	}
	/*
	 * Reconfiguring RXON clears the firmware nodes table so we must
	 * add the broadcast node again.
	 */
	memset(&node, 0, sizeof node);
	IEEE80211_ADDR_COPY(node.macaddr, etherbroadcastaddr);
	node.id = WPI_ID_BROADCAST;
	node.plcp = (ic->ic_curmode == IEEE80211_MODE_11A) ?
	    wpi_rates[WPI_RIDX_OFDM6].plcp : wpi_rates[WPI_RIDX_CCK1].plcp;
	node.action = htole32(WPI_ACTION_SET_RATE);
	node.antenna = WPI_ANTENNA_BOTH;
	error = wpi_cmd(sc, WPI_CMD_ADD_NODE, &node, sizeof node, 1);
	if (error != 0) {
		printf("%s: could not add broadcast node\n",
		    sc->sc_dev.dv_xname);
		return error;
	}
	return 0;
}

int
wpi_run(struct wpi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	struct wpi_node_info node;
	int error;

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		/* Link LED blinks while monitoring. */
		wpi_set_led(sc, WPI_LED_LINK, 5, 5);
		return 0;
	}
	if ((error = wpi_set_timing(sc, ni)) != 0) {
		printf("%s: could not set timing\n", sc->sc_dev.dv_xname);
		return error;
	}

	/* Update adapter configuration. */
	sc->rxon.associd = htole16(IEEE80211_AID(ni->ni_associd));
	/* Short preamble and slot time are negotiated when associating. */
	sc->rxon.flags &= ~htole32(WPI_RXON_SHPREAMBLE | WPI_RXON_SHSLOT);
	if (ic->ic_flags & IEEE80211_F_SHSLOT)
		sc->rxon.flags |= htole32(WPI_RXON_SHSLOT);
	if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
		sc->rxon.flags |= htole32(WPI_RXON_SHPREAMBLE);
	sc->rxon.filter |= htole32(WPI_FILTER_BSS);
	DPRINTF(("rxon chan %d flags %x\n", sc->rxon.chan, sc->rxon.flags));
	error = wpi_cmd(sc, WPI_CMD_RXON, &sc->rxon, sizeof (struct wpi_rxon),
	    1);
	if (error != 0) {
		printf("%s: RXON command failed\n", sc->sc_dev.dv_xname);
		return error;
	}

	/* Configuration has changed, set TX power accordingly. */
	if ((error = wpi_set_txpower(sc, 1)) != 0) {
		printf("%s: could not set TX power\n", sc->sc_dev.dv_xname);
		return error;
	}

	/* Fake a join to init the TX rate. */
	((struct wpi_node *)ni)->id = WPI_ID_BSS;
	wpi_newassoc(ic, ni, 1);

	/* Add BSS node. */
	memset(&node, 0, sizeof node);
	IEEE80211_ADDR_COPY(node.macaddr, ni->ni_bssid);
	node.id = WPI_ID_BSS;
	node.plcp = (ic->ic_curmode == IEEE80211_MODE_11A) ?
	    wpi_rates[WPI_RIDX_OFDM6].plcp : wpi_rates[WPI_RIDX_CCK1].plcp;
	node.action = htole32(WPI_ACTION_SET_RATE);
	node.antenna = WPI_ANTENNA_BOTH;
	DPRINTF(("adding BSS node\n"));
	error = wpi_cmd(sc, WPI_CMD_ADD_NODE, &node, sizeof node, 1);
	if (error != 0) {
		printf("%s: could not add BSS node\n", sc->sc_dev.dv_xname);
		return error;
	}

	/* Start periodic calibration timer. */
	sc->calib_cnt = 0;
	timeout_add_msec(&sc->calib_to, 500);

	/* Link LED always on while associated. */
	wpi_set_led(sc, WPI_LED_LINK, 0, 1);

	/* Enable power-saving mode if requested by user. */
	if (sc->sc_ic.ic_flags & IEEE80211_F_PMGTON)
		(void)wpi_set_pslevel(sc, 0, 3, 1);

	return 0;
}

/*
 * We support CCMP hardware encryption/decryption of unicast frames only.
 * HW support for TKIP really sucks.  We should let TKIP die anyway.
 */
int
wpi_set_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct wpi_softc *sc = ic->ic_softc;
	struct wpi_node *wn = (void *)ni;
	struct wpi_node_info node;
	uint16_t kflags;

	if ((k->k_flags & IEEE80211_KEY_GROUP) ||
	    k->k_cipher != IEEE80211_CIPHER_CCMP)
		return ieee80211_set_key(ic, ni, k);

	kflags = WPI_KFLAG_CCMP | WPI_KFLAG_KID(k->k_id);
	memset(&node, 0, sizeof node);
	node.id = wn->id;
	node.control = WPI_NODE_UPDATE;
	node.flags = WPI_FLAG_SET_KEY;
	node.kflags = htole16(kflags);
	memcpy(node.key, k->k_key, k->k_len);
	DPRINTF(("set key id=%d for node %d\n", k->k_id, node.id));
	return wpi_cmd(sc, WPI_CMD_ADD_NODE, &node, sizeof node, 1);
}

void
wpi_delete_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct wpi_softc *sc = ic->ic_softc;
	struct wpi_node *wn = (void *)ni;
	struct wpi_node_info node;

	if ((k->k_flags & IEEE80211_KEY_GROUP) ||
	    k->k_cipher != IEEE80211_CIPHER_CCMP) {
		/* See comment about other ciphers above. */
		ieee80211_delete_key(ic, ni, k);
		return;
	}
	if (ic->ic_state != IEEE80211_S_RUN)
		return;	/* Nothing to do. */
	memset(&node, 0, sizeof node);
	node.id = wn->id;
	node.control = WPI_NODE_UPDATE;
	node.flags = WPI_FLAG_SET_KEY;
	node.kflags = 0;
	DPRINTF(("delete keys for node %d\n", node.id));
	(void)wpi_cmd(sc, WPI_CMD_ADD_NODE, &node, sizeof node, 1);
}

int
wpi_post_alive(struct wpi_softc *sc)
{
	int ntries, error;

	/* Check (again) that the radio is not disabled. */
	if ((error = wpi_nic_lock(sc)) != 0)
		return error;
	/* NB: Runtime firmware must be up and running. */
	if (!(wpi_prph_read(sc, WPI_APMG_RFKILL) & 1)) {
		printf("%s: radio is disabled by hardware switch\n",
		    sc->sc_dev.dv_xname);
		wpi_nic_unlock(sc);
		return EPERM;	/* :-) */
	}
	wpi_nic_unlock(sc);

	/* Wait for thermal sensor to calibrate. */
	for (ntries = 0; ntries < 1000; ntries++) {
		if ((sc->temp = (int)WPI_READ(sc, WPI_UCODE_GP2)) != 0)
			break;
		DELAY(10);
	}
	if (ntries == 1000) {
		printf("%s: timeout waiting for thermal sensor calibration\n",
		    sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}
	DPRINTF(("temperature %d\n", sc->temp));
	return 0;
}

/*
 * The firmware boot code is small and is intended to be copied directly into
 * the NIC internal memory (no DMA transfer.)
 */
int
wpi_load_bootcode(struct wpi_softc *sc, const uint8_t *ucode, int size)
{
	int error, ntries;

	size /= sizeof (uint32_t);

	if ((error = wpi_nic_lock(sc)) != 0)
		return error;

	/* Copy microcode image into NIC memory. */
	wpi_prph_write_region_4(sc, WPI_BSM_SRAM_BASE,
	    (const uint32_t *)ucode, size);

	wpi_prph_write(sc, WPI_BSM_WR_MEM_SRC, 0);
	wpi_prph_write(sc, WPI_BSM_WR_MEM_DST, WPI_FW_TEXT_BASE);
	wpi_prph_write(sc, WPI_BSM_WR_DWCOUNT, size);

	/* Start boot load now. */
	wpi_prph_write(sc, WPI_BSM_WR_CTRL, WPI_BSM_WR_CTRL_START);

	/* Wait for transfer to complete. */
	for (ntries = 0; ntries < 1000; ntries++) {
		if (!(wpi_prph_read(sc, WPI_BSM_WR_CTRL) &
		    WPI_BSM_WR_CTRL_START))
			break;
		DELAY(10);
	}
	if (ntries == 1000) {
		printf("%s: could not load boot firmware\n",
		    sc->sc_dev.dv_xname);
		wpi_nic_unlock(sc);
		return ETIMEDOUT;
	}

	/* Enable boot after power up. */
	wpi_prph_write(sc, WPI_BSM_WR_CTRL, WPI_BSM_WR_CTRL_START_EN);

	wpi_nic_unlock(sc);
	return 0;
}

int
wpi_load_firmware(struct wpi_softc *sc)
{
	struct wpi_fw_info *fw = &sc->fw;
	struct wpi_dma_info *dma = &sc->fw_dma;
	int error;

	/* Copy initialization sections into pre-allocated DMA-safe memory. */
	memcpy(dma->vaddr, fw->init.data, fw->init.datasz);
	bus_dmamap_sync(sc->sc_dmat, dma->map, 0, fw->init.datasz,
	    BUS_DMASYNC_PREWRITE);
	memcpy(dma->vaddr + WPI_FW_DATA_MAXSZ,
	    fw->init.text, fw->init.textsz);
	bus_dmamap_sync(sc->sc_dmat, dma->map, WPI_FW_DATA_MAXSZ,
	    fw->init.textsz, BUS_DMASYNC_PREWRITE);

	/* Tell adapter where to find initialization sections. */
	if ((error = wpi_nic_lock(sc)) != 0)
		return error;
	wpi_prph_write(sc, WPI_BSM_DRAM_DATA_ADDR, dma->paddr);
	wpi_prph_write(sc, WPI_BSM_DRAM_DATA_SIZE, fw->init.datasz);
	wpi_prph_write(sc, WPI_BSM_DRAM_TEXT_ADDR,
	    dma->paddr + WPI_FW_DATA_MAXSZ);
	wpi_prph_write(sc, WPI_BSM_DRAM_TEXT_SIZE, fw->init.textsz);
	wpi_nic_unlock(sc);

	/* Load firmware boot code. */
	error = wpi_load_bootcode(sc, fw->boot.text, fw->boot.textsz);
	if (error != 0) {
		printf("%s: could not load boot firmware\n",
		    sc->sc_dev.dv_xname);
		return error;
	}
	/* Now press "execute". */
	WPI_WRITE(sc, WPI_RESET, 0);

	/* Wait at most one second for first alive notification. */
	if ((error = tsleep_nsec(sc, PCATCH, "wpiinit", SEC_TO_NSEC(1))) != 0) {
		printf("%s: timeout waiting for adapter to initialize\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	/* Copy runtime sections into pre-allocated DMA-safe memory. */
	memcpy(dma->vaddr, fw->main.data, fw->main.datasz);
	bus_dmamap_sync(sc->sc_dmat, dma->map, 0, fw->main.datasz,
	    BUS_DMASYNC_PREWRITE);
	memcpy(dma->vaddr + WPI_FW_DATA_MAXSZ,
	    fw->main.text, fw->main.textsz);
	bus_dmamap_sync(sc->sc_dmat, dma->map, WPI_FW_DATA_MAXSZ,
	    fw->main.textsz, BUS_DMASYNC_PREWRITE);

	/* Tell adapter where to find runtime sections. */
	if ((error = wpi_nic_lock(sc)) != 0)
		return error;
	wpi_prph_write(sc, WPI_BSM_DRAM_DATA_ADDR, dma->paddr);
	wpi_prph_write(sc, WPI_BSM_DRAM_DATA_SIZE, fw->main.datasz);
	wpi_prph_write(sc, WPI_BSM_DRAM_TEXT_ADDR,
	    dma->paddr + WPI_FW_DATA_MAXSZ);
	wpi_prph_write(sc, WPI_BSM_DRAM_TEXT_SIZE,
	    WPI_FW_UPDATED | fw->main.textsz);
	wpi_nic_unlock(sc);

	return 0;
}

int
wpi_read_firmware(struct wpi_softc *sc)
{
	struct wpi_fw_info *fw = &sc->fw;
	const struct wpi_firmware_hdr *hdr;
	int error;

	/* Read firmware image from filesystem. */
	if ((error = loadfirmware("wpi-3945abg", &fw->data, &fw->datalen)) != 0) {
		printf("%s: error, %d, could not read firmware %s\n",
		    sc->sc_dev.dv_xname, error, "wpi-3945abg");
		return error;
	}
	if (fw->datalen < sizeof (*hdr)) {
		printf("%s: truncated firmware header: %zu bytes\n",
		    sc->sc_dev.dv_xname, fw->datalen);
		free(fw->data, M_DEVBUF, fw->datalen);
		return EINVAL;
	}
	/* Extract firmware header information. */
	hdr = (struct wpi_firmware_hdr *)fw->data;
	fw->main.textsz = letoh32(hdr->main_textsz);
	fw->main.datasz = letoh32(hdr->main_datasz);
	fw->init.textsz = letoh32(hdr->init_textsz);
	fw->init.datasz = letoh32(hdr->init_datasz);
	fw->boot.textsz = letoh32(hdr->boot_textsz);
	fw->boot.datasz = 0;

	/* Sanity-check firmware header. */
	if (fw->main.textsz > WPI_FW_TEXT_MAXSZ ||
	    fw->main.datasz > WPI_FW_DATA_MAXSZ ||
	    fw->init.textsz > WPI_FW_TEXT_MAXSZ ||
	    fw->init.datasz > WPI_FW_DATA_MAXSZ ||
	    fw->boot.textsz > WPI_FW_BOOT_TEXT_MAXSZ ||
	    (fw->boot.textsz & 3) != 0) {
		printf("%s: invalid firmware header\n", sc->sc_dev.dv_xname);
		free(fw->data, M_DEVBUF, fw->datalen);
		return EINVAL;
	}

	/* Check that all firmware sections fit. */
	if (fw->datalen < sizeof (*hdr) + fw->main.textsz + fw->main.datasz +
	    fw->init.textsz + fw->init.datasz + fw->boot.textsz) {
		printf("%s: firmware file too short: %zu bytes\n",
		    sc->sc_dev.dv_xname, fw->datalen);
		free(fw->data, M_DEVBUF, fw->datalen);
		return EINVAL;
	}

	/* Get pointers to firmware sections. */
	fw->main.text = (const uint8_t *)(hdr + 1);
	fw->main.data = fw->main.text + fw->main.textsz;
	fw->init.text = fw->main.data + fw->main.datasz;
	fw->init.data = fw->init.text + fw->init.textsz;
	fw->boot.text = fw->init.data + fw->init.datasz;

	return 0;
}

int
wpi_clock_wait(struct wpi_softc *sc)
{
	int ntries;

	/* Set "initialization complete" bit. */
	WPI_SETBITS(sc, WPI_GP_CNTRL, WPI_GP_CNTRL_INIT_DONE);

	/* Wait for clock stabilization. */
	for (ntries = 0; ntries < 25000; ntries++) {
		if (WPI_READ(sc, WPI_GP_CNTRL) & WPI_GP_CNTRL_MAC_CLOCK_READY)
			return 0;
		DELAY(100);
	}
	printf("%s: timeout waiting for clock stabilization\n",
	    sc->sc_dev.dv_xname);
	return ETIMEDOUT;
}

int
wpi_apm_init(struct wpi_softc *sc)
{
	int error;

	WPI_SETBITS(sc, WPI_ANA_PLL, WPI_ANA_PLL_INIT);
	/* Disable L0s. */
	WPI_SETBITS(sc, WPI_GIO_CHICKEN, WPI_GIO_CHICKEN_L1A_NO_L0S_RX);

	if ((error = wpi_clock_wait(sc)) != 0)
		return error;

	if ((error = wpi_nic_lock(sc)) != 0)
		return error;
	/* Enable DMA. */
	wpi_prph_write(sc, WPI_APMG_CLK_ENA,
	    WPI_APMG_CLK_DMA_CLK_RQT | WPI_APMG_CLK_BSM_CLK_RQT);
	DELAY(20);
	/* Disable L1. */
	wpi_prph_setbits(sc, WPI_APMG_PCI_STT, WPI_APMG_PCI_STT_L1A_DIS);
	wpi_nic_unlock(sc);

	return 0;
}

void
wpi_apm_stop_master(struct wpi_softc *sc)
{
	int ntries;

	WPI_SETBITS(sc, WPI_RESET, WPI_RESET_STOP_MASTER);

	if ((WPI_READ(sc, WPI_GP_CNTRL) & WPI_GP_CNTRL_PS_MASK) ==
	    WPI_GP_CNTRL_MAC_PS)
		return;	/* Already asleep. */

	for (ntries = 0; ntries < 100; ntries++) {
		if (WPI_READ(sc, WPI_RESET) & WPI_RESET_MASTER_DISABLED)
			return;
		DELAY(10);
	}
	printf("%s: timeout waiting for master\n", sc->sc_dev.dv_xname);
}

void
wpi_apm_stop(struct wpi_softc *sc)
{
	wpi_apm_stop_master(sc);
	WPI_SETBITS(sc, WPI_RESET, WPI_RESET_SW);
}

void
wpi_nic_config(struct wpi_softc *sc)
{
	pcireg_t reg;
	uint8_t rev;

	/* Voodoo from the reference driver. */
	reg = pci_conf_read(sc->sc_pct, sc->sc_pcitag, PCI_CLASS_REG);
	rev = PCI_REVISION(reg);
	if ((rev & 0xc0) == 0x40)
		WPI_SETBITS(sc, WPI_HW_IF_CONFIG, WPI_HW_IF_CONFIG_ALM_MB);
	else if (!(rev & 0x80))
		WPI_SETBITS(sc, WPI_HW_IF_CONFIG, WPI_HW_IF_CONFIG_ALM_MM);

	if (sc->cap == 0x80)
		WPI_SETBITS(sc, WPI_HW_IF_CONFIG, WPI_HW_IF_CONFIG_SKU_MRC);

	if ((letoh16(sc->rev) & 0xf0) == 0xd0)
		WPI_SETBITS(sc, WPI_HW_IF_CONFIG, WPI_HW_IF_CONFIG_REV_D);
	else
		WPI_CLRBITS(sc, WPI_HW_IF_CONFIG, WPI_HW_IF_CONFIG_REV_D);

	if (sc->type > 1)
		WPI_SETBITS(sc, WPI_HW_IF_CONFIG, WPI_HW_IF_CONFIG_TYPE_B);
}

int
wpi_hw_init(struct wpi_softc *sc)
{
	int chnl, ntries, error;

	/* Clear pending interrupts. */
	WPI_WRITE(sc, WPI_INT, 0xffffffff);

	if ((error = wpi_apm_init(sc)) != 0) {
		printf("%s: could not power ON adapter\n",
		    sc->sc_dev.dv_xname);
		return error;
	}

	/* Select VMAIN power source. */
	if ((error = wpi_nic_lock(sc)) != 0)
		return error;
	wpi_prph_clrbits(sc, WPI_APMG_PS, WPI_APMG_PS_PWR_SRC_MASK);
	wpi_nic_unlock(sc);
	/* Spin until VMAIN gets selected. */
	for (ntries = 0; ntries < 5000; ntries++) {
		if (WPI_READ(sc, WPI_GPIO_IN) & WPI_GPIO_IN_VMAIN)
			break;
		DELAY(10);
	}
	if (ntries == 5000) {
		printf("%s: timeout selecting power source\n",
		    sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}

	/* Perform adapter initialization. */
	(void)wpi_nic_config(sc);

	/* Initialize RX ring. */
	if ((error = wpi_nic_lock(sc)) != 0)
		return error;
	/* Set physical address of RX ring. */
	WPI_WRITE(sc, WPI_FH_RX_BASE, sc->rxq.desc_dma.paddr);
	/* Set physical address of RX read pointer. */
	WPI_WRITE(sc, WPI_FH_RX_RPTR_ADDR, sc->shared_dma.paddr +
	    offsetof(struct wpi_shared, next));
	WPI_WRITE(sc, WPI_FH_RX_WPTR, 0);
	/* Enable RX. */
	WPI_WRITE(sc, WPI_FH_RX_CONFIG,
	    WPI_FH_RX_CONFIG_DMA_ENA |
	    WPI_FH_RX_CONFIG_RDRBD_ENA |
	    WPI_FH_RX_CONFIG_WRSTATUS_ENA |
	    WPI_FH_RX_CONFIG_MAXFRAG |
	    WPI_FH_RX_CONFIG_NRBD(WPI_RX_RING_COUNT_LOG) |
	    WPI_FH_RX_CONFIG_IRQ_DST_HOST |
	    WPI_FH_RX_CONFIG_IRQ_RBTH(1));
	(void)WPI_READ(sc, WPI_FH_RSSR_TBL);	/* barrier */
	WPI_WRITE(sc, WPI_FH_RX_WPTR, (WPI_RX_RING_COUNT - 1) & ~7);
	wpi_nic_unlock(sc);

	/* Initialize TX rings. */
	if ((error = wpi_nic_lock(sc)) != 0)
		return error;
	wpi_prph_write(sc, WPI_ALM_SCHED_MODE, 2);	/* bypass mode */
	wpi_prph_write(sc, WPI_ALM_SCHED_ARASTAT, 1);	/* enable RA0 */
	/* Enable all 6 TX rings. */
	wpi_prph_write(sc, WPI_ALM_SCHED_TXFACT, 0x3f);
	wpi_prph_write(sc, WPI_ALM_SCHED_SBYPASS_MODE1, 0x10000);
	wpi_prph_write(sc, WPI_ALM_SCHED_SBYPASS_MODE2, 0x30002);
	wpi_prph_write(sc, WPI_ALM_SCHED_TXF4MF, 4);
	wpi_prph_write(sc, WPI_ALM_SCHED_TXF5MF, 5);
	/* Set physical address of TX rings. */
	WPI_WRITE(sc, WPI_FH_TX_BASE, sc->shared_dma.paddr);
	WPI_WRITE(sc, WPI_FH_MSG_CONFIG, 0xffff05a5);

	/* Enable all DMA channels. */
	for (chnl = 0; chnl < WPI_NDMACHNLS; chnl++) {
		WPI_WRITE(sc, WPI_FH_CBBC_CTRL(chnl), 0);
		WPI_WRITE(sc, WPI_FH_CBBC_BASE(chnl), 0);
		WPI_WRITE(sc, WPI_FH_TX_CONFIG(chnl), 0x80200008);
	}
	wpi_nic_unlock(sc);
	(void)WPI_READ(sc, WPI_FH_TX_BASE);	/* barrier */

	/* Clear "radio off" and "commands blocked" bits. */
	WPI_WRITE(sc, WPI_UCODE_GP1_CLR, WPI_UCODE_GP1_RFKILL);
	WPI_WRITE(sc, WPI_UCODE_GP1_CLR, WPI_UCODE_GP1_CMD_BLOCKED);

	/* Clear pending interrupts. */
	WPI_WRITE(sc, WPI_INT, 0xffffffff);
	/* Enable interrupts. */
	WPI_WRITE(sc, WPI_MASK, WPI_INT_MASK);

	/* _Really_ make sure "radio off" bit is cleared! */
	WPI_WRITE(sc, WPI_UCODE_GP1_CLR, WPI_UCODE_GP1_RFKILL);
	WPI_WRITE(sc, WPI_UCODE_GP1_CLR, WPI_UCODE_GP1_RFKILL);

	if ((error = wpi_load_firmware(sc)) != 0) {
		printf("%s: could not load firmware\n", sc->sc_dev.dv_xname);
		return error;
	}
	/* Wait at most one second for firmware alive notification. */
	if ((error = tsleep_nsec(sc, PCATCH, "wpiinit", SEC_TO_NSEC(1))) != 0) {
		printf("%s: timeout waiting for adapter to initialize\n",
		    sc->sc_dev.dv_xname);
		return error;
	}
	/* Do post-firmware initialization. */
	return wpi_post_alive(sc);
}

void
wpi_hw_stop(struct wpi_softc *sc)
{
	int chnl, qid, ntries;
	uint32_t tmp;

	WPI_WRITE(sc, WPI_RESET, WPI_RESET_NEVO);

	/* Disable interrupts. */
	WPI_WRITE(sc, WPI_MASK, 0);
	WPI_WRITE(sc, WPI_INT, 0xffffffff);
	WPI_WRITE(sc, WPI_FH_INT, 0xffffffff);

	/* Make sure we no longer hold the NIC lock. */
	wpi_nic_unlock(sc);

	if (wpi_nic_lock(sc) == 0) {
		/* Stop TX scheduler. */
		wpi_prph_write(sc, WPI_ALM_SCHED_MODE, 0);
		wpi_prph_write(sc, WPI_ALM_SCHED_TXFACT, 0);

		/* Stop all DMA channels. */
		for (chnl = 0; chnl < WPI_NDMACHNLS; chnl++) {
			WPI_WRITE(sc, WPI_FH_TX_CONFIG(chnl), 0);
			for (ntries = 0; ntries < 100; ntries++) {
				tmp = WPI_READ(sc, WPI_FH_TX_STATUS);
				if ((tmp & WPI_FH_TX_STATUS_IDLE(chnl)) ==
				    WPI_FH_TX_STATUS_IDLE(chnl))
					break;
				DELAY(10);
			}
		}
		wpi_nic_unlock(sc);
	}

	/* Stop RX ring. */
	wpi_reset_rx_ring(sc, &sc->rxq);

	/* Reset all TX rings. */
	for (qid = 0; qid < WPI_NTXQUEUES; qid++)
		wpi_reset_tx_ring(sc, &sc->txq[qid]);

	if (wpi_nic_lock(sc) == 0) {
		wpi_prph_write(sc, WPI_APMG_CLK_DIS, WPI_APMG_CLK_DMA_CLK_RQT);
		wpi_nic_unlock(sc);
	}
	DELAY(5);
	/* Power OFF adapter. */
	wpi_apm_stop(sc);
}

int
wpi_init(struct ifnet *ifp)
{
	struct wpi_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int error;

#ifdef notyet
	/* Check that the radio is not disabled by hardware switch. */
	if (!(WPI_READ(sc, WPI_GP_CNTRL) & WPI_GP_CNTRL_RFKILL)) {
		printf("%s: radio is disabled by hardware switch\n",
		    sc->sc_dev.dv_xname);
		error = EPERM;	/* :-) */
		goto fail;
	}
#endif
	/* Read firmware images from the filesystem. */
	if ((error = wpi_read_firmware(sc)) != 0) {
		printf("%s: could not read firmware\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	/* Initialize hardware and upload firmware. */
	error = wpi_hw_init(sc);
	free(sc->fw.data, M_DEVBUF, sc->fw.datalen);
	if (error != 0) {
		printf("%s: could not initialize hardware\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/* Configure adapter now that it is ready. */
	if ((error = wpi_config(sc)) != 0) {
		printf("%s: could not configure device\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_flags |= IFF_RUNNING;

	if (ic->ic_opmode != IEEE80211_M_MONITOR)
		ieee80211_begin_scan(ifp);
	else
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);

	return 0;

fail:	wpi_stop(ifp, 1);
	return error;
}

void
wpi_stop(struct ifnet *ifp, int disable)
{
	struct wpi_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;

	ifp->if_timer = sc->sc_tx_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);

	/* Power OFF hardware. */
	wpi_hw_stop(sc);
}
