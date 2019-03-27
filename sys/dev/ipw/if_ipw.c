/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2006
 *      Damien Bergamini <damien.bergamini@free.fr>. All rights reserved.
 * Copyright (c) 2006 Sam Leffler, Errno Consulting
 * Copyright (c) 2007 Andrew Thompson <thompsa@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*-
 * Intel(R) PRO/Wireless 2100 MiniPCI driver
 * http://www.intel.com/network/connectivity/products/wireless/prowireless_mobile.htm
 */

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/linker.h>
#include <sys/firmware.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>

#include <dev/ipw/if_ipwreg.h>
#include <dev/ipw/if_ipwvar.h>

#define IPW_DEBUG
#ifdef IPW_DEBUG
#define DPRINTF(x)	do { if (ipw_debug > 0) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (ipw_debug >= (n)) printf x; } while (0)
int ipw_debug = 0;
SYSCTL_INT(_debug, OID_AUTO, ipw, CTLFLAG_RW, &ipw_debug, 0, "ipw debug level");
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

MODULE_DEPEND(ipw, pci,  1, 1, 1);
MODULE_DEPEND(ipw, wlan, 1, 1, 1);
MODULE_DEPEND(ipw, firmware, 1, 1, 1);

struct ipw_ident {
	uint16_t	vendor;
	uint16_t	device;
	const char	*name;
};

static const struct ipw_ident ipw_ident_table[] = {
	{ 0x8086, 0x1043, "Intel(R) PRO/Wireless 2100 MiniPCI" },

	{ 0, 0, NULL }
};

static struct ieee80211vap *ipw_vap_create(struct ieee80211com *,
		    const char [IFNAMSIZ], int, enum ieee80211_opmode, int,
		    const uint8_t [IEEE80211_ADDR_LEN],
		    const uint8_t [IEEE80211_ADDR_LEN]);
static void	ipw_vap_delete(struct ieee80211vap *);
static int	ipw_dma_alloc(struct ipw_softc *);
static void	ipw_release(struct ipw_softc *);
static void	ipw_media_status(struct ifnet *, struct ifmediareq *);
static int	ipw_newstate(struct ieee80211vap *, enum ieee80211_state, int);
static uint16_t	ipw_read_prom_word(struct ipw_softc *, uint8_t);
static uint16_t	ipw_read_chanmask(struct ipw_softc *);
static void	ipw_rx_cmd_intr(struct ipw_softc *, struct ipw_soft_buf *);
static void	ipw_rx_newstate_intr(struct ipw_softc *, struct ipw_soft_buf *);
static void	ipw_rx_data_intr(struct ipw_softc *, struct ipw_status *,
		    struct ipw_soft_bd *, struct ipw_soft_buf *);
static void	ipw_rx_intr(struct ipw_softc *);
static void	ipw_release_sbd(struct ipw_softc *, struct ipw_soft_bd *);
static void	ipw_tx_intr(struct ipw_softc *);
static void	ipw_intr(void *);
static void	ipw_dma_map_addr(void *, bus_dma_segment_t *, int, int);
static const char * ipw_cmdname(int);
static int	ipw_cmd(struct ipw_softc *, uint32_t, void *, uint32_t);
static int	ipw_tx_start(struct ipw_softc *, struct mbuf *,
		    struct ieee80211_node *);
static int	ipw_raw_xmit(struct ieee80211_node *, struct mbuf *,
		    const struct ieee80211_bpf_params *);
static int	ipw_transmit(struct ieee80211com *, struct mbuf *);
static void	ipw_start(struct ipw_softc *);
static void	ipw_watchdog(void *);
static void	ipw_parent(struct ieee80211com *);
static void	ipw_stop_master(struct ipw_softc *);
static int	ipw_enable(struct ipw_softc *);
static int	ipw_disable(struct ipw_softc *);
static int	ipw_reset(struct ipw_softc *);
static int	ipw_load_ucode(struct ipw_softc *, const char *, int);
static int	ipw_load_firmware(struct ipw_softc *, const char *, int);
static int	ipw_config(struct ipw_softc *);
static void	ipw_assoc(struct ieee80211com *, struct ieee80211vap *);
static void	ipw_disassoc(struct ieee80211com *, struct ieee80211vap *);
static void	ipw_init_task(void *, int);
static void	ipw_init(void *);
static void	ipw_init_locked(struct ipw_softc *);
static void	ipw_stop(void *);
static void	ipw_stop_locked(struct ipw_softc *);
static int	ipw_sysctl_stats(SYSCTL_HANDLER_ARGS);
static int	ipw_sysctl_radio(SYSCTL_HANDLER_ARGS);
static uint32_t	ipw_read_table1(struct ipw_softc *, uint32_t);
static void	ipw_write_table1(struct ipw_softc *, uint32_t, uint32_t);
#if 0
static int	ipw_read_table2(struct ipw_softc *, uint32_t, void *,
		    uint32_t *);
static void	ipw_read_mem_1(struct ipw_softc *, bus_size_t, uint8_t *,
		    bus_size_t);
#endif
static void	ipw_write_mem_1(struct ipw_softc *, bus_size_t,
		    const uint8_t *, bus_size_t);
static int	ipw_scan(struct ipw_softc *);
static void	ipw_scan_start(struct ieee80211com *);
static void	ipw_scan_end(struct ieee80211com *);
static void	ipw_getradiocaps(struct ieee80211com *, int, int *,
		    struct ieee80211_channel[]);
static void	ipw_set_channel(struct ieee80211com *);
static void	ipw_scan_curchan(struct ieee80211_scan_state *,
		    unsigned long maxdwell);
static void	ipw_scan_mindwell(struct ieee80211_scan_state *);

static int ipw_probe(device_t);
static int ipw_attach(device_t);
static int ipw_detach(device_t);
static int ipw_shutdown(device_t);
static int ipw_suspend(device_t);
static int ipw_resume(device_t);

static device_method_t ipw_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ipw_probe),
	DEVMETHOD(device_attach,	ipw_attach),
	DEVMETHOD(device_detach,	ipw_detach),
	DEVMETHOD(device_shutdown,	ipw_shutdown),
	DEVMETHOD(device_suspend,	ipw_suspend),
	DEVMETHOD(device_resume,	ipw_resume),

	DEVMETHOD_END
};

static driver_t ipw_driver = {
	"ipw",
	ipw_methods,
	sizeof (struct ipw_softc)
};

static devclass_t ipw_devclass;

DRIVER_MODULE(ipw, pci, ipw_driver, ipw_devclass, NULL, NULL);
MODULE_PNP_INFO("U16:vendor;U16:device;D:#", pci, ipw, ipw_ident_table,
    nitems(ipw_ident_table) - 1);

MODULE_VERSION(ipw, 1);

static int
ipw_probe(device_t dev)
{
	const struct ipw_ident *ident;

	for (ident = ipw_ident_table; ident->name != NULL; ident++) {
		if (pci_get_vendor(dev) == ident->vendor &&
		    pci_get_device(dev) == ident->device) {
			device_set_desc(dev, ident->name);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return ENXIO;
}

/* Base Address Register */
static int
ipw_attach(device_t dev)
{
	struct ipw_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t val;
	int error, i;

	sc->sc_dev = dev;

	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF | MTX_RECURSE);
	mbufq_init(&sc->sc_snd, ifqmaxlen);
	TASK_INIT(&sc->sc_init_task, 0, ipw_init_task, sc);
	callout_init_mtx(&sc->sc_wdtimer, &sc->sc_mtx, 0);

	pci_write_config(dev, 0x41, 0, 1);

	/* enable bus-mastering */
	pci_enable_busmaster(dev);

	i = PCIR_BAR(0);
	sc->mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &i, RF_ACTIVE);
	if (sc->mem == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		goto fail;
	}

	sc->sc_st = rman_get_bustag(sc->mem);
	sc->sc_sh = rman_get_bushandle(sc->mem);

	i = 0;
	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &i,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->irq == NULL) {
		device_printf(dev, "could not allocate interrupt resource\n");
		goto fail1;
	}

	if (ipw_reset(sc) != 0) {
		device_printf(dev, "could not reset adapter\n");
		goto fail2;
	}

	if (ipw_dma_alloc(sc) != 0) {
		device_printf(dev, "could not allocate DMA resources\n");
		goto fail2;
	}

	ic->ic_softc = sc;
	ic->ic_name = device_get_nameunit(dev);
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_phytype = IEEE80211_T_DS;

	/* set device capabilities */
	ic->ic_caps =
		  IEEE80211_C_STA		/* station mode supported */
		| IEEE80211_C_IBSS		/* IBSS mode supported */
		| IEEE80211_C_MONITOR		/* monitor mode supported */
		| IEEE80211_C_PMGT		/* power save supported */
		| IEEE80211_C_SHPREAMBLE	/* short preamble supported */
		| IEEE80211_C_WPA		/* 802.11i supported */
		;

	/* read MAC address from EEPROM */
	val = ipw_read_prom_word(sc, IPW_EEPROM_MAC + 0);
	ic->ic_macaddr[0] = val >> 8;
	ic->ic_macaddr[1] = val & 0xff;
	val = ipw_read_prom_word(sc, IPW_EEPROM_MAC + 1);
	ic->ic_macaddr[2] = val >> 8;
	ic->ic_macaddr[3] = val & 0xff;
	val = ipw_read_prom_word(sc, IPW_EEPROM_MAC + 2);
	ic->ic_macaddr[4] = val >> 8;
	ic->ic_macaddr[5] = val & 0xff;

	sc->chanmask = ipw_read_chanmask(sc);
	ipw_getradiocaps(ic, IEEE80211_CHAN_MAX, &ic->ic_nchans,
	    ic->ic_channels);

	/* check support for radio transmitter switch in EEPROM */
	if (!(ipw_read_prom_word(sc, IPW_EEPROM_RADIO) & 8))
		sc->flags |= IPW_FLAG_HAS_RADIO_SWITCH;

	ieee80211_ifattach(ic);
	ic->ic_scan_start = ipw_scan_start;
	ic->ic_scan_end = ipw_scan_end;
	ic->ic_getradiocaps = ipw_getradiocaps;
	ic->ic_set_channel = ipw_set_channel;
	ic->ic_scan_curchan = ipw_scan_curchan;
	ic->ic_scan_mindwell = ipw_scan_mindwell;
	ic->ic_raw_xmit = ipw_raw_xmit;
	ic->ic_vap_create = ipw_vap_create;
	ic->ic_vap_delete = ipw_vap_delete;
	ic->ic_transmit = ipw_transmit;
	ic->ic_parent = ipw_parent;

	ieee80211_radiotap_attach(ic,
	    &sc->sc_txtap.wt_ihdr, sizeof(sc->sc_txtap),
		IPW_TX_RADIOTAP_PRESENT,
	    &sc->sc_rxtap.wr_ihdr, sizeof(sc->sc_rxtap),
		IPW_RX_RADIOTAP_PRESENT);

	/*
	 * Add a few sysctl knobs.
	 */
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO, "radio",
	    CTLTYPE_INT | CTLFLAG_RD, sc, 0, ipw_sysctl_radio, "I",
	    "radio transmitter switch state (0=off, 1=on)");

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO, "stats",
	    CTLTYPE_OPAQUE | CTLFLAG_RD, sc, 0, ipw_sysctl_stats, "S",
	    "statistics");

	/*
	 * Hook our interrupt after all initialization is complete.
	 */
	error = bus_setup_intr(dev, sc->irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, ipw_intr, sc, &sc->sc_ih);
	if (error != 0) {
		device_printf(dev, "could not set up interrupt\n");
		goto fail3;
	}

	if (bootverbose)
		ieee80211_announce(ic);

	return 0;
fail3:
	ipw_release(sc);
fail2:
	bus_release_resource(dev, SYS_RES_IRQ, rman_get_rid(sc->irq), sc->irq);
fail1:
	bus_release_resource(dev, SYS_RES_MEMORY, rman_get_rid(sc->mem),
	    sc->mem);
fail:
	mtx_destroy(&sc->sc_mtx);
	return ENXIO;
}

static int
ipw_detach(device_t dev)
{
	struct ipw_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;

	bus_teardown_intr(dev, sc->irq, sc->sc_ih);

	ieee80211_draintask(ic, &sc->sc_init_task);
	ipw_stop(sc);

	ieee80211_ifdetach(ic);

	callout_drain(&sc->sc_wdtimer);
	mbufq_drain(&sc->sc_snd);

	ipw_release(sc);

	bus_release_resource(dev, SYS_RES_IRQ, rman_get_rid(sc->irq), sc->irq);

	bus_release_resource(dev, SYS_RES_MEMORY, rman_get_rid(sc->mem),
	    sc->mem);

	if (sc->sc_firmware != NULL) {
		firmware_put(sc->sc_firmware, FIRMWARE_UNLOAD);
		sc->sc_firmware = NULL;
	}

	mtx_destroy(&sc->sc_mtx);

	return 0;
}

static struct ieee80211vap *
ipw_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ], int unit,
    enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct ipw_softc *sc = ic->ic_softc;
	struct ipw_vap *ivp;
	struct ieee80211vap *vap;
	const struct firmware *fp;
	const struct ipw_firmware_hdr *hdr;
	const char *imagename;

	if (!TAILQ_EMPTY(&ic->ic_vaps))		/* only one at a time */
		return NULL;

	switch (opmode) {
	case IEEE80211_M_STA:
		imagename = "ipw_bss";
		break;
	case IEEE80211_M_IBSS:
		imagename = "ipw_ibss";
		break;
	case IEEE80211_M_MONITOR:
		imagename = "ipw_monitor";
		break;
	default:
		return NULL;
	}

	/*
	 * Load firmware image using the firmware(9) subsystem.  Doing
	 * this unlocked is ok since we're single-threaded by the
	 * 802.11 layer.
	 */
	if (sc->sc_firmware == NULL ||
	    strcmp(sc->sc_firmware->name, imagename) != 0) {
		if (sc->sc_firmware != NULL)
			firmware_put(sc->sc_firmware, FIRMWARE_UNLOAD);
		sc->sc_firmware = firmware_get(imagename);
	}
	if (sc->sc_firmware == NULL) {
		device_printf(sc->sc_dev,
		    "could not load firmware image '%s'\n", imagename);
		return NULL;
	}
	fp = sc->sc_firmware;
	if (fp->datasize < sizeof *hdr) {
		device_printf(sc->sc_dev,
		    "firmware image too short %zu\n", fp->datasize);
		firmware_put(sc->sc_firmware, FIRMWARE_UNLOAD);
		sc->sc_firmware = NULL;
		return NULL;
	}
	hdr = (const struct ipw_firmware_hdr *)fp->data;
	if (fp->datasize < sizeof *hdr + le32toh(hdr->mainsz) +
	    le32toh(hdr->ucodesz)) {
		device_printf(sc->sc_dev,
		    "firmware image too short %zu\n", fp->datasize);
		firmware_put(sc->sc_firmware, FIRMWARE_UNLOAD);
		sc->sc_firmware = NULL;
		return NULL;
	}

	ivp = malloc(sizeof(struct ipw_vap), M_80211_VAP, M_WAITOK | M_ZERO);
	vap = &ivp->vap;

	ieee80211_vap_setup(ic, vap, name, unit, opmode, flags, bssid);
	/* override with driver methods */
	ivp->newstate = vap->iv_newstate;
	vap->iv_newstate = ipw_newstate;

	/* complete setup */
	ieee80211_vap_attach(vap, ieee80211_media_change, ipw_media_status,
	    mac);
	ic->ic_opmode = opmode;
	return vap;
}

static void
ipw_vap_delete(struct ieee80211vap *vap)
{
	struct ipw_vap *ivp = IPW_VAP(vap);

	ieee80211_vap_detach(vap);
	free(ivp, M_80211_VAP);
}

static int
ipw_dma_alloc(struct ipw_softc *sc)
{
	struct ipw_soft_bd *sbd;
	struct ipw_soft_hdr *shdr;
	struct ipw_soft_buf *sbuf;
	bus_addr_t physaddr;
	int error, i;

	/*
	 * Allocate parent DMA tag for subsequent allocations.
	 */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 1, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    BUS_SPACE_MAXSIZE_32BIT, BUS_SPACE_UNRESTRICTED,
	    BUS_SPACE_MAXSIZE_32BIT, 0, NULL, NULL, &sc->parent_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create parent DMA tag\n");
		goto fail;
	}

	/*
	 * Allocate and map tx ring.
	 */
	error = bus_dma_tag_create(sc->parent_dmat, 4, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, IPW_TBD_SZ, 1, IPW_TBD_SZ, 0, NULL,
	    NULL, &sc->tbd_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create tx ring DMA tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(sc->tbd_dmat, (void **)&sc->tbd_list,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO, &sc->tbd_map);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not allocate tx ring DMA memory\n");
		goto fail;
	}

	error = bus_dmamap_load(sc->tbd_dmat, sc->tbd_map, sc->tbd_list,
	    IPW_TBD_SZ, ipw_dma_map_addr, &sc->tbd_phys, 0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not map tx ring DMA memory\n");
		goto fail;
	}

	/*
	 * Allocate and map rx ring.
	 */
	error = bus_dma_tag_create(sc->parent_dmat, 4, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, IPW_RBD_SZ, 1, IPW_RBD_SZ, 0, NULL,
	    NULL, &sc->rbd_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create rx ring DMA tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(sc->rbd_dmat, (void **)&sc->rbd_list,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO, &sc->rbd_map);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not allocate rx ring DMA memory\n");
		goto fail;
	}

	error = bus_dmamap_load(sc->rbd_dmat, sc->rbd_map, sc->rbd_list,
	    IPW_RBD_SZ, ipw_dma_map_addr, &sc->rbd_phys, 0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not map rx ring DMA memory\n");
		goto fail;
	}

	/*
	 * Allocate and map status ring.
	 */
	error = bus_dma_tag_create(sc->parent_dmat, 4, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, IPW_STATUS_SZ, 1, IPW_STATUS_SZ, 0,
	    NULL, NULL, &sc->status_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not create status ring DMA tag\n");
		goto fail;
	}

	error = bus_dmamem_alloc(sc->status_dmat, (void **)&sc->status_list,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO, &sc->status_map);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not allocate status ring DMA memory\n");
		goto fail;
	}

	error = bus_dmamap_load(sc->status_dmat, sc->status_map,
	    sc->status_list, IPW_STATUS_SZ, ipw_dma_map_addr, &sc->status_phys,
	    0);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not map status ring DMA memory\n");
		goto fail;
	}

	/*
	 * Allocate command DMA map.
	 */
	error = bus_dma_tag_create(sc->parent_dmat, 1, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, sizeof (struct ipw_cmd), 1,
	    sizeof (struct ipw_cmd), 0, NULL, NULL, &sc->cmd_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create command DMA tag\n");
		goto fail;
	}

	error = bus_dmamap_create(sc->cmd_dmat, 0, &sc->cmd_map);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not create command DMA map\n");
		goto fail;
	}

	/*
	 * Allocate headers DMA maps.
	 */
	error = bus_dma_tag_create(sc->parent_dmat, 1, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, sizeof (struct ipw_hdr), 1,
	    sizeof (struct ipw_hdr), 0, NULL, NULL, &sc->hdr_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create header DMA tag\n");
		goto fail;
	}

	SLIST_INIT(&sc->free_shdr);
	for (i = 0; i < IPW_NDATA; i++) {
		shdr = &sc->shdr_list[i];
		error = bus_dmamap_create(sc->hdr_dmat, 0, &shdr->map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not create header DMA map\n");
			goto fail;
		}
		SLIST_INSERT_HEAD(&sc->free_shdr, shdr, next);
	}

	/*
	 * Allocate tx buffers DMA maps.
	 */
	error = bus_dma_tag_create(sc->parent_dmat, 1, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, MCLBYTES, IPW_MAX_NSEG, MCLBYTES, 0,
	    NULL, NULL, &sc->txbuf_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create tx DMA tag\n");
		goto fail;
	}

	SLIST_INIT(&sc->free_sbuf);
	for (i = 0; i < IPW_NDATA; i++) {
		sbuf = &sc->tx_sbuf_list[i];
		error = bus_dmamap_create(sc->txbuf_dmat, 0, &sbuf->map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not create tx DMA map\n");
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
	error = bus_dma_tag_create(sc->parent_dmat, 1, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, MCLBYTES, 1, MCLBYTES, 0, NULL,
	    NULL, &sc->rxbuf_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not create rx DMA tag\n");
		goto fail;
	}

	for (i = 0; i < IPW_NRBD; i++) {
		sbd = &sc->srbd_list[i];
		sbuf = &sc->rx_sbuf_list[i];
		sbd->bd = &sc->rbd_list[i];

		sbuf->m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (sbuf->m == NULL) {
			device_printf(sc->sc_dev,
			    "could not allocate rx mbuf\n");
			error = ENOMEM;
			goto fail;
		}

		error = bus_dmamap_create(sc->rxbuf_dmat, 0, &sbuf->map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not create rx DMA map\n");
			goto fail;
		}

		error = bus_dmamap_load(sc->rxbuf_dmat, sbuf->map,
		    mtod(sbuf->m, void *), MCLBYTES, ipw_dma_map_addr,
		    &physaddr, 0);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not map rx DMA memory\n");
			goto fail;
		}

		sbd->type = IPW_SBD_TYPE_DATA;
		sbd->priv = sbuf;
		sbd->bd->physaddr = htole32(physaddr);
		sbd->bd->len = htole32(MCLBYTES);
	}

	bus_dmamap_sync(sc->rbd_dmat, sc->rbd_map, BUS_DMASYNC_PREWRITE);

	return 0;

fail:	ipw_release(sc);
	return error;
}

static void
ipw_release(struct ipw_softc *sc)
{
	struct ipw_soft_buf *sbuf;
	int i;

	if (sc->parent_dmat != NULL) {
		bus_dma_tag_destroy(sc->parent_dmat);
	}

	if (sc->tbd_dmat != NULL) {
		bus_dmamap_unload(sc->tbd_dmat, sc->tbd_map);
		bus_dmamem_free(sc->tbd_dmat, sc->tbd_list, sc->tbd_map);
		bus_dma_tag_destroy(sc->tbd_dmat);
	}

	if (sc->rbd_dmat != NULL) {
		if (sc->rbd_list != NULL) {
			bus_dmamap_unload(sc->rbd_dmat, sc->rbd_map);
			bus_dmamem_free(sc->rbd_dmat, sc->rbd_list,
			    sc->rbd_map);
		}
		bus_dma_tag_destroy(sc->rbd_dmat);
	}

	if (sc->status_dmat != NULL) {
		if (sc->status_list != NULL) {
			bus_dmamap_unload(sc->status_dmat, sc->status_map);
			bus_dmamem_free(sc->status_dmat, sc->status_list,
			    sc->status_map);
		}
		bus_dma_tag_destroy(sc->status_dmat);
	}

	for (i = 0; i < IPW_NTBD; i++)
		ipw_release_sbd(sc, &sc->stbd_list[i]);

	if (sc->cmd_dmat != NULL) {
		bus_dmamap_destroy(sc->cmd_dmat, sc->cmd_map);
		bus_dma_tag_destroy(sc->cmd_dmat);
	}

	if (sc->hdr_dmat != NULL) {
		for (i = 0; i < IPW_NDATA; i++)
			bus_dmamap_destroy(sc->hdr_dmat, sc->shdr_list[i].map);
		bus_dma_tag_destroy(sc->hdr_dmat);
	}

	if (sc->txbuf_dmat != NULL) {
		for (i = 0; i < IPW_NDATA; i++) {
			bus_dmamap_destroy(sc->txbuf_dmat,
			    sc->tx_sbuf_list[i].map);
		}
		bus_dma_tag_destroy(sc->txbuf_dmat);
	}

	if (sc->rxbuf_dmat != NULL) {
		for (i = 0; i < IPW_NRBD; i++) {
			sbuf = &sc->rx_sbuf_list[i];
			if (sbuf->m != NULL) {
				bus_dmamap_sync(sc->rxbuf_dmat, sbuf->map,
				    BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(sc->rxbuf_dmat, sbuf->map);
				m_freem(sbuf->m);
			}
			bus_dmamap_destroy(sc->rxbuf_dmat, sbuf->map);
		}
		bus_dma_tag_destroy(sc->rxbuf_dmat);
	}
}

static int
ipw_shutdown(device_t dev)
{
	struct ipw_softc *sc = device_get_softc(dev);

	ipw_stop(sc);

	return 0;
}

static int
ipw_suspend(device_t dev)
{
	struct ipw_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;

	ieee80211_suspend_all(ic);
	return 0;
}

static int
ipw_resume(device_t dev)
{
	struct ipw_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;

	pci_write_config(dev, 0x41, 0, 1);

	ieee80211_resume_all(ic);
	return 0;
}

static int
ipw_cvtrate(int ipwrate)
{
	switch (ipwrate) {
	case IPW_RATE_DS1:	return 2;
	case IPW_RATE_DS2:	return 4;
	case IPW_RATE_DS5:	return 11;
	case IPW_RATE_DS11:	return 22;
	}
	return 0;
}

/*
 * The firmware automatically adapts the transmit speed. We report its current
 * value here.
 */
static void
ipw_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct ieee80211vap *vap = ifp->if_softc;
	struct ieee80211com *ic = vap->iv_ic;
	struct ipw_softc *sc = ic->ic_softc;

	/* read current transmission rate from adapter */
	vap->iv_bss->ni_txrate = ipw_cvtrate(
	    ipw_read_table1(sc, IPW_INFO_CURRENT_TX_RATE) & 0xf);
	ieee80211_media_status(ifp, imr);
}

static int
ipw_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct ipw_vap *ivp = IPW_VAP(vap);
	struct ieee80211com *ic = vap->iv_ic;
	struct ipw_softc *sc = ic->ic_softc;
	enum ieee80211_state ostate;

	DPRINTF(("%s: %s -> %s flags 0x%x\n", __func__,
		ieee80211_state_name[vap->iv_state],
		ieee80211_state_name[nstate], sc->flags));

	ostate = vap->iv_state;
	IEEE80211_UNLOCK(ic);

	switch (nstate) {
	case IEEE80211_S_RUN:
		if (ic->ic_opmode == IEEE80211_M_IBSS) {
			/*
			 * XXX when joining an ibss network we are called
			 * with a SCAN -> RUN transition on scan complete.
			 * Use that to call ipw_assoc.  On completing the
			 * join we are then called again with an AUTH -> RUN
			 * transition and we want to do nothing.  This is
			 * all totally bogus and needs to be redone.
			 */
			if (ostate == IEEE80211_S_SCAN)
				ipw_assoc(ic, vap);
		}
		break;

	case IEEE80211_S_INIT:
		if (sc->flags & IPW_FLAG_ASSOCIATED)
			ipw_disassoc(ic, vap);
		break;

	case IEEE80211_S_AUTH:
		/*
		 * Move to ASSOC state after the ipw_assoc() call.  Firmware
		 * takes care of authentication, after the call we'll receive
		 * only an assoc response which would otherwise be discared
		 * if we are still in AUTH state.
		 */
		nstate = IEEE80211_S_ASSOC;
		ipw_assoc(ic, vap);
		break;

	case IEEE80211_S_ASSOC:
		/*
		 * If we are not transitioning from AUTH then resend the
		 * association request.
		 */
		if (ostate != IEEE80211_S_AUTH)
			ipw_assoc(ic, vap);
		break;

	default:
		break;
	}
	IEEE80211_LOCK(ic);
	return ivp->newstate(vap, nstate, arg);
}

/*
 * Read 16 bits at address 'addr' from the serial EEPROM.
 */
static uint16_t
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

	return le16toh(val);
}

static uint16_t
ipw_read_chanmask(struct ipw_softc *sc)
{
	uint16_t val;

	/* set supported .11b channels (read from EEPROM) */
	if ((val = ipw_read_prom_word(sc, IPW_EEPROM_CHANNEL_LIST)) == 0)
		val = 0x7ff;	/* default to channels 1-11 */
	val <<= 1;

	return (val);
}

static void
ipw_rx_cmd_intr(struct ipw_softc *sc, struct ipw_soft_buf *sbuf)
{
	struct ipw_cmd *cmd;

	bus_dmamap_sync(sc->rxbuf_dmat, sbuf->map, BUS_DMASYNC_POSTREAD);

	cmd = mtod(sbuf->m, struct ipw_cmd *);

	DPRINTFN(9, ("cmd ack'ed %s(%u, %u, %u, %u, %u)\n",
	    ipw_cmdname(le32toh(cmd->type)), le32toh(cmd->type),
	    le32toh(cmd->subtype), le32toh(cmd->seq), le32toh(cmd->len),
	    le32toh(cmd->status)));

	sc->flags &= ~IPW_FLAG_BUSY;
	wakeup(sc);
}

static void
ipw_rx_newstate_intr(struct ipw_softc *sc, struct ipw_soft_buf *sbuf)
{
#define	IEEESTATE(vap)	ieee80211_state_name[vap->iv_state]
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	uint32_t state;

	bus_dmamap_sync(sc->rxbuf_dmat, sbuf->map, BUS_DMASYNC_POSTREAD);

	state = le32toh(*mtod(sbuf->m, uint32_t *));

	switch (state) {
	case IPW_STATE_ASSOCIATED:
		DPRINTFN(2, ("Association succeeded (%s flags 0x%x)\n",
			IEEESTATE(vap), sc->flags));
		/* XXX suppress state change in case the fw auto-associates */
		if ((sc->flags & IPW_FLAG_ASSOCIATING) == 0) {
			DPRINTF(("Unexpected association (%s, flags 0x%x)\n",
				IEEESTATE(vap), sc->flags));
			break;
		}
		sc->flags &= ~IPW_FLAG_ASSOCIATING;
		sc->flags |= IPW_FLAG_ASSOCIATED;
		break;

	case IPW_STATE_SCANNING:
		DPRINTFN(3, ("Scanning (%s flags 0x%x)\n",
			IEEESTATE(vap), sc->flags));
		/*
		 * NB: Check driver state for association on assoc
		 * loss as the firmware will immediately start to
		 * scan and we would treat it as a beacon miss if
		 * we checked the 802.11 layer state.
		 */
		if (sc->flags & IPW_FLAG_ASSOCIATED) {
			IPW_UNLOCK(sc);
			/* XXX probably need to issue disassoc to fw */
			ieee80211_beacon_miss(ic);
			IPW_LOCK(sc);
		}
		break;

	case IPW_STATE_SCAN_COMPLETE:
		/*
		 * XXX For some reason scan requests generate scan
		 * started + scan done events before any traffic is
		 * received (e.g. probe response frames).  We work
		 * around this by marking the HACK flag and skipping
		 * the first scan complete event.
		*/
		DPRINTFN(3, ("Scan complete (%s flags 0x%x)\n",
			    IEEESTATE(vap), sc->flags));
		if (sc->flags & IPW_FLAG_HACK) {
			sc->flags &= ~IPW_FLAG_HACK;
			break;
		}
		if (sc->flags & IPW_FLAG_SCANNING) {
			IPW_UNLOCK(sc);
			ieee80211_scan_done(vap);
			IPW_LOCK(sc);
			sc->flags &= ~IPW_FLAG_SCANNING;
			sc->sc_scan_timer = 0;
		}
		break;

	case IPW_STATE_ASSOCIATION_LOST:
		DPRINTFN(2, ("Association lost (%s flags 0x%x)\n",
			IEEESTATE(vap), sc->flags));
		sc->flags &= ~(IPW_FLAG_ASSOCIATING | IPW_FLAG_ASSOCIATED);
		if (vap->iv_state == IEEE80211_S_RUN) {
			IPW_UNLOCK(sc);
			ieee80211_new_state(vap, IEEE80211_S_SCAN, -1);
			IPW_LOCK(sc);
		}
		break;

	case IPW_STATE_DISABLED:
		/* XXX? is this right? */
		sc->flags &= ~(IPW_FLAG_HACK | IPW_FLAG_SCANNING |
		    IPW_FLAG_ASSOCIATING | IPW_FLAG_ASSOCIATED);
		DPRINTFN(2, ("Firmware disabled (%s flags 0x%x)\n",
			IEEESTATE(vap), sc->flags));
		break;

	case IPW_STATE_RADIO_DISABLED:
		device_printf(sc->sc_dev, "radio turned off\n");
		ieee80211_notify_radio(ic, 0);
		ipw_stop_locked(sc);
		/* XXX start polling thread to detect radio on */
		break;

	default:
		DPRINTFN(2, ("%s: unhandled state %u %s flags 0x%x\n",
			__func__, state, IEEESTATE(vap), sc->flags));
		break;
	}
#undef IEEESTATE
}

/*
 * Set driver state for current channel.
 */
static void
ipw_setcurchan(struct ipw_softc *sc, struct ieee80211_channel *chan)
{
	struct ieee80211com *ic = &sc->sc_ic;

	ic->ic_curchan = chan;
	ieee80211_radiotap_chan_change(ic);
}

/*
 * XXX: Hack to set the current channel to the value advertised in beacons or
 * probe responses. Only used during AP detection.
 */
static void
ipw_fix_channel(struct ipw_softc *sc, struct mbuf *m)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *c;
	struct ieee80211_frame *wh;
	uint8_t subtype;
	uint8_t *frm, *efrm;

	wh = mtod(m, struct ieee80211_frame *);

	if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_MGT)
		return;

	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

	if (subtype != IEEE80211_FC0_SUBTYPE_BEACON &&
	    subtype != IEEE80211_FC0_SUBTYPE_PROBE_RESP)
		return;

	/* XXX use ieee80211_parse_beacon */
	frm = (uint8_t *)(wh + 1);
	efrm = mtod(m, uint8_t *) + m->m_len;

	frm += 12;	/* skip tstamp, bintval and capinfo fields */
	while (frm < efrm) {
		if (*frm == IEEE80211_ELEMID_DSPARMS)
#if IEEE80211_CHAN_MAX < 255
		if (frm[2] <= IEEE80211_CHAN_MAX)
#endif
		{
			DPRINTF(("Fixing channel to %d\n", frm[2]));
			c = ieee80211_find_channel(ic,
				ieee80211_ieee2mhz(frm[2], 0),
				IEEE80211_CHAN_B);
			if (c == NULL)
				c = &ic->ic_channels[0];
			ipw_setcurchan(sc, c);
		}

		frm += frm[1] + 2;
	}
}

static void
ipw_rx_data_intr(struct ipw_softc *sc, struct ipw_status *status,
    struct ipw_soft_bd *sbd, struct ipw_soft_buf *sbuf)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct mbuf *mnew, *m;
	struct ieee80211_node *ni;
	bus_addr_t physaddr;
	int error;
	int8_t rssi, nf;

	DPRINTFN(5, ("received frame len=%u, rssi=%u\n", le32toh(status->len),
	    status->rssi));

	if (le32toh(status->len) < sizeof (struct ieee80211_frame_min) ||
	    le32toh(status->len) > MCLBYTES)
		return;

	/*
	 * Try to allocate a new mbuf for this ring element and load it before
	 * processing the current mbuf. If the ring element cannot be loaded,
	 * drop the received packet and reuse the old mbuf. In the unlikely
	 * case that the old mbuf can't be reloaded either, explicitly panic.
	 */
	mnew = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (mnew == NULL) {
		counter_u64_add(ic->ic_ierrors, 1);
		return;
	}

	bus_dmamap_sync(sc->rxbuf_dmat, sbuf->map, BUS_DMASYNC_POSTREAD);
	bus_dmamap_unload(sc->rxbuf_dmat, sbuf->map);

	error = bus_dmamap_load(sc->rxbuf_dmat, sbuf->map, mtod(mnew, void *),
	    MCLBYTES, ipw_dma_map_addr, &physaddr, 0);
	if (error != 0) {
		m_freem(mnew);

		/* try to reload the old mbuf */
		error = bus_dmamap_load(sc->rxbuf_dmat, sbuf->map,
		    mtod(sbuf->m, void *), MCLBYTES, ipw_dma_map_addr,
		    &physaddr, 0);
		if (error != 0) {
			/* very unlikely that it will fail... */
			panic("%s: could not load old rx mbuf",
			    device_get_name(sc->sc_dev));
		}
		counter_u64_add(ic->ic_ierrors, 1);
		return;
	}

	/*
	 * New mbuf successfully loaded, update Rx ring and continue
	 * processing.
	 */
	m = sbuf->m;
	sbuf->m = mnew;
	sbd->bd->physaddr = htole32(physaddr);
	m->m_pkthdr.len = m->m_len = le32toh(status->len);

	rssi = status->rssi + IPW_RSSI_TO_DBM;
	nf = -95;
	if (ieee80211_radiotap_active(ic)) {
		struct ipw_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = 0;
		tap->wr_antsignal = rssi;
		tap->wr_antnoise = nf;
	}

	if (sc->flags & IPW_FLAG_SCANNING)
		ipw_fix_channel(sc, m);

	IPW_UNLOCK(sc);
	ni = ieee80211_find_rxnode(ic, mtod(m, struct ieee80211_frame_min *));
	if (ni != NULL) {
		(void) ieee80211_input(ni, m, rssi - nf, nf);
		ieee80211_free_node(ni);
	} else
		(void) ieee80211_input_all(ic, m, rssi - nf, nf);
	IPW_LOCK(sc);

	bus_dmamap_sync(sc->rbd_dmat, sc->rbd_map, BUS_DMASYNC_PREWRITE);
}

static void
ipw_rx_intr(struct ipw_softc *sc)
{
	struct ipw_status *status;
	struct ipw_soft_bd *sbd;
	struct ipw_soft_buf *sbuf;
	uint32_t r, i;

	if (!(sc->flags & IPW_FLAG_FW_INITED))
		return;

	r = CSR_READ_4(sc, IPW_CSR_RX_READ);

	bus_dmamap_sync(sc->status_dmat, sc->status_map, BUS_DMASYNC_POSTREAD);

	for (i = (sc->rxcur + 1) % IPW_NRBD; i != r; i = (i + 1) % IPW_NRBD) {
		status = &sc->status_list[i];
		sbd = &sc->srbd_list[i];
		sbuf = sbd->priv;

		switch (le16toh(status->code) & 0xf) {
		case IPW_STATUS_CODE_COMMAND:
			ipw_rx_cmd_intr(sc, sbuf);
			break;

		case IPW_STATUS_CODE_NEWSTATE:
			ipw_rx_newstate_intr(sc, sbuf);
			break;

		case IPW_STATUS_CODE_DATA_802_3:
		case IPW_STATUS_CODE_DATA_802_11:
			ipw_rx_data_intr(sc, status, sbd, sbuf);
			break;

		case IPW_STATUS_CODE_NOTIFICATION:
			DPRINTFN(2, ("notification status, len %u flags 0x%x\n",
			    le32toh(status->len), status->flags));
			/* XXX maybe drive state machine AUTH->ASSOC? */
			break;

		default:
			device_printf(sc->sc_dev, "unexpected status code %u\n",
			    le16toh(status->code));
		}

		/* firmware was killed, stop processing received frames */
		if (!(sc->flags & IPW_FLAG_FW_INITED))
			return;

		sbd->bd->flags = 0;
	}

	bus_dmamap_sync(sc->rbd_dmat, sc->rbd_map, BUS_DMASYNC_PREWRITE);

	/* kick the firmware */
	sc->rxcur = (r == 0) ? IPW_NRBD - 1 : r - 1;
	CSR_WRITE_4(sc, IPW_CSR_RX_WRITE, sc->rxcur);
}

static void
ipw_release_sbd(struct ipw_softc *sc, struct ipw_soft_bd *sbd)
{
	struct ipw_soft_hdr *shdr;
	struct ipw_soft_buf *sbuf;

	switch (sbd->type) {
	case IPW_SBD_TYPE_COMMAND:
		bus_dmamap_sync(sc->cmd_dmat, sc->cmd_map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->cmd_dmat, sc->cmd_map);
		break;

	case IPW_SBD_TYPE_HEADER:
		shdr = sbd->priv;
		bus_dmamap_sync(sc->hdr_dmat, shdr->map, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->hdr_dmat, shdr->map);
		SLIST_INSERT_HEAD(&sc->free_shdr, shdr, next);
		break;

	case IPW_SBD_TYPE_DATA:
		sbuf = sbd->priv;
		bus_dmamap_sync(sc->txbuf_dmat, sbuf->map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->txbuf_dmat, sbuf->map);
		SLIST_INSERT_HEAD(&sc->free_sbuf, sbuf, next);

		ieee80211_tx_complete(sbuf->ni, sbuf->m, 0/*XXX*/);

		sc->sc_tx_timer = 0;
		break;
	}

	sbd->type = IPW_SBD_TYPE_NOASSOC;
}

static void
ipw_tx_intr(struct ipw_softc *sc)
{
	struct ipw_soft_bd *sbd;
	uint32_t r, i;

	if (!(sc->flags & IPW_FLAG_FW_INITED))
		return;

	r = CSR_READ_4(sc, IPW_CSR_TX_READ);

	for (i = (sc->txold + 1) % IPW_NTBD; i != r; i = (i + 1) % IPW_NTBD) {
		sbd = &sc->stbd_list[i];
		ipw_release_sbd(sc, sbd);
		sc->txfree++;
	}

	/* remember what the firmware has processed */
	sc->txold = (r == 0) ? IPW_NTBD - 1 : r - 1;

	ipw_start(sc);
}

static void
ipw_fatal_error_intr(struct ipw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);

	device_printf(sc->sc_dev, "firmware error\n");
	if (vap != NULL) {
		IPW_UNLOCK(sc);
		ieee80211_cancel_scan(vap);
		IPW_LOCK(sc);
	}
	ieee80211_runtask(ic, &sc->sc_init_task);
}

static void
ipw_intr(void *arg)
{
	struct ipw_softc *sc = arg;
	uint32_t r;

	IPW_LOCK(sc);

	r = CSR_READ_4(sc, IPW_CSR_INTR);
	if (r == 0 || r == 0xffffffff)
		goto done;

	/* disable interrupts */
	CSR_WRITE_4(sc, IPW_CSR_INTR_MASK, 0);

	/* acknowledge all interrupts */
	CSR_WRITE_4(sc, IPW_CSR_INTR, r);

	if (r & (IPW_INTR_FATAL_ERROR | IPW_INTR_PARITY_ERROR)) {
		ipw_fatal_error_intr(sc);
		goto done;
	}

	if (r & IPW_INTR_FW_INIT_DONE)
		wakeup(sc);

	if (r & IPW_INTR_RX_TRANSFER)
		ipw_rx_intr(sc);

	if (r & IPW_INTR_TX_TRANSFER)
		ipw_tx_intr(sc);

	/* re-enable interrupts */
	CSR_WRITE_4(sc, IPW_CSR_INTR_MASK, IPW_INTR_MASK);
done:
	IPW_UNLOCK(sc);
}

static void
ipw_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	if (error != 0)
		return;

	KASSERT(nseg == 1, ("too many DMA segments, %d should be 1", nseg));

	*(bus_addr_t *)arg = segs[0].ds_addr;
}

static const char *
ipw_cmdname(int cmd)
{
	static const struct {
		int	cmd;
		const char *name;
	} cmds[] = {
		{ IPW_CMD_ADD_MULTICAST,	"ADD_MULTICAST" },
		{ IPW_CMD_BROADCAST_SCAN,	"BROADCAST_SCAN" },
		{ IPW_CMD_DISABLE,		"DISABLE" },
		{ IPW_CMD_DISABLE_PHY,		"DISABLE_PHY" },
		{ IPW_CMD_ENABLE,		"ENABLE" },
		{ IPW_CMD_PREPARE_POWER_DOWN,	"PREPARE_POWER_DOWN" },
		{ IPW_CMD_SET_BASIC_TX_RATES,	"SET_BASIC_TX_RATES" },
		{ IPW_CMD_SET_BEACON_INTERVAL,	"SET_BEACON_INTERVAL" },
		{ IPW_CMD_SET_CHANNEL,		"SET_CHANNEL" },
		{ IPW_CMD_SET_CONFIGURATION,	"SET_CONFIGURATION" },
		{ IPW_CMD_SET_DESIRED_BSSID,	"SET_DESIRED_BSSID" },
		{ IPW_CMD_SET_ESSID,		"SET_ESSID" },
		{ IPW_CMD_SET_FRAG_THRESHOLD,	"SET_FRAG_THRESHOLD" },
		{ IPW_CMD_SET_MAC_ADDRESS,	"SET_MAC_ADDRESS" },
		{ IPW_CMD_SET_MANDATORY_BSSID,	"SET_MANDATORY_BSSID" },
		{ IPW_CMD_SET_MODE,		"SET_MODE" },
		{ IPW_CMD_SET_MSDU_TX_RATES,	"SET_MSDU_TX_RATES" },
		{ IPW_CMD_SET_POWER_MODE,	"SET_POWER_MODE" },
		{ IPW_CMD_SET_RTS_THRESHOLD,	"SET_RTS_THRESHOLD" },
		{ IPW_CMD_SET_SCAN_OPTIONS,	"SET_SCAN_OPTIONS" },
		{ IPW_CMD_SET_SECURITY_INFO,	"SET_SECURITY_INFO" },
		{ IPW_CMD_SET_TX_POWER_INDEX,	"SET_TX_POWER_INDEX" },
		{ IPW_CMD_SET_TX_RATES,		"SET_TX_RATES" },
		{ IPW_CMD_SET_WEP_FLAGS,	"SET_WEP_FLAGS" },
		{ IPW_CMD_SET_WEP_KEY,		"SET_WEP_KEY" },
		{ IPW_CMD_SET_WEP_KEY_INDEX,	"SET_WEP_KEY_INDEX" },
		{ IPW_CMD_SET_WPA_IE,		"SET_WPA_IE" },

	};
	static char buf[12];
	int i;

	for (i = 0; i < nitems(cmds); i++)
		if (cmds[i].cmd == cmd)
			return cmds[i].name;
	snprintf(buf, sizeof(buf), "%u", cmd);
	return buf;
}

/*
 * Send a command to the firmware and wait for the acknowledgement.
 */
static int
ipw_cmd(struct ipw_softc *sc, uint32_t type, void *data, uint32_t len)
{
	struct ipw_soft_bd *sbd;
	bus_addr_t physaddr;
	int error;

	IPW_LOCK_ASSERT(sc);

	if (sc->flags & IPW_FLAG_BUSY) {
		device_printf(sc->sc_dev, "%s: %s not sent, busy\n",
			__func__, ipw_cmdname(type));
		return EAGAIN;
	}
	sc->flags |= IPW_FLAG_BUSY;

	sbd = &sc->stbd_list[sc->txcur];

	error = bus_dmamap_load(sc->cmd_dmat, sc->cmd_map, &sc->cmd,
	    sizeof (struct ipw_cmd), ipw_dma_map_addr, &physaddr, 0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not map command DMA memory\n");
		sc->flags &= ~IPW_FLAG_BUSY;
		return error;
	}

	sc->cmd.type = htole32(type);
	sc->cmd.subtype = 0;
	sc->cmd.len = htole32(len);
	sc->cmd.seq = 0;
	memcpy(sc->cmd.data, data, len);

	sbd->type = IPW_SBD_TYPE_COMMAND;
	sbd->bd->physaddr = htole32(physaddr);
	sbd->bd->len = htole32(sizeof (struct ipw_cmd));
	sbd->bd->nfrag = 1;
	sbd->bd->flags = IPW_BD_FLAG_TX_FRAME_COMMAND |
	    IPW_BD_FLAG_TX_LAST_FRAGMENT;

	bus_dmamap_sync(sc->cmd_dmat, sc->cmd_map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->tbd_dmat, sc->tbd_map, BUS_DMASYNC_PREWRITE);

#ifdef IPW_DEBUG
	if (ipw_debug >= 4) {
		printf("sending %s(%u, %u, %u, %u)", ipw_cmdname(type), type,
		    0, 0, len);
		/* Print the data buffer in the higher debug level */
		if (ipw_debug >= 9 && len > 0) {
			printf(" data: 0x");
			for (int i = 1; i <= len; i++)
				printf("%1D", (u_char *)data + len - i, "");
		}
		printf("\n");
	}
#endif

	/* kick firmware */
	sc->txfree--;
	sc->txcur = (sc->txcur + 1) % IPW_NTBD;
	CSR_WRITE_4(sc, IPW_CSR_TX_WRITE, sc->txcur);

	/* wait at most one second for command to complete */
	error = msleep(sc, &sc->sc_mtx, 0, "ipwcmd", hz);
	if (error != 0) {
		device_printf(sc->sc_dev, "%s: %s failed, timeout (error %u)\n",
		    __func__, ipw_cmdname(type), error);
		sc->flags &= ~IPW_FLAG_BUSY;
		return (error);
	}
	return (0);
}

static int
ipw_tx_start(struct ipw_softc *sc, struct mbuf *m0, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_frame *wh;
	struct ipw_soft_bd *sbd;
	struct ipw_soft_hdr *shdr;
	struct ipw_soft_buf *sbuf;
	struct ieee80211_key *k;
	struct mbuf *mnew;
	bus_dma_segment_t segs[IPW_MAX_NSEG];
	bus_addr_t physaddr;
	int nsegs, error, i;

	wh = mtod(m0, struct ieee80211_frame *);

	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_crypto_encap(ni, m0);
		if (k == NULL) {
			m_freem(m0);
			return ENOBUFS;
		}
		/* packet header may have moved, reset our local pointer */
		wh = mtod(m0, struct ieee80211_frame *);
	}

	if (ieee80211_radiotap_active_vap(vap)) {
		struct ipw_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;

		ieee80211_radiotap_tx(vap, m0);
	}

	shdr = SLIST_FIRST(&sc->free_shdr);
	sbuf = SLIST_FIRST(&sc->free_sbuf);
	KASSERT(shdr != NULL && sbuf != NULL, ("empty sw hdr/buf pool"));

	shdr->hdr.type = htole32(IPW_HDR_TYPE_SEND);
	shdr->hdr.subtype = 0;
	shdr->hdr.encrypted = (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) ? 1 : 0;
	shdr->hdr.encrypt = 0;
	shdr->hdr.keyidx = 0;
	shdr->hdr.keysz = 0;
	shdr->hdr.fragmentsz = 0;
	IEEE80211_ADDR_COPY(shdr->hdr.src_addr, wh->i_addr2);
	if (ic->ic_opmode == IEEE80211_M_STA)
		IEEE80211_ADDR_COPY(shdr->hdr.dst_addr, wh->i_addr3);
	else
		IEEE80211_ADDR_COPY(shdr->hdr.dst_addr, wh->i_addr1);

	/* trim IEEE802.11 header */
	m_adj(m0, sizeof (struct ieee80211_frame));

	error = bus_dmamap_load_mbuf_sg(sc->txbuf_dmat, sbuf->map, m0, segs,
	    &nsegs, 0);
	if (error != 0 && error != EFBIG) {
		device_printf(sc->sc_dev, "could not map mbuf (error %d)\n",
		    error);
		m_freem(m0);
		return error;
	}
	if (error != 0) {
		mnew = m_defrag(m0, M_NOWAIT);
		if (mnew == NULL) {
			device_printf(sc->sc_dev,
			    "could not defragment mbuf\n");
			m_freem(m0);
			return ENOBUFS;
		}
		m0 = mnew;

		error = bus_dmamap_load_mbuf_sg(sc->txbuf_dmat, sbuf->map, m0,
		    segs, &nsegs, 0);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not map mbuf (error %d)\n", error);
			m_freem(m0);
			return error;
		}
	}

	error = bus_dmamap_load(sc->hdr_dmat, shdr->map, &shdr->hdr,
	    sizeof (struct ipw_hdr), ipw_dma_map_addr, &physaddr, 0);
	if (error != 0) {
		device_printf(sc->sc_dev, "could not map header DMA memory\n");
		bus_dmamap_unload(sc->txbuf_dmat, sbuf->map);
		m_freem(m0);
		return error;
	}

	SLIST_REMOVE_HEAD(&sc->free_sbuf, next);
	SLIST_REMOVE_HEAD(&sc->free_shdr, next);

	sbd = &sc->stbd_list[sc->txcur];
	sbd->type = IPW_SBD_TYPE_HEADER;
	sbd->priv = shdr;
	sbd->bd->physaddr = htole32(physaddr);
	sbd->bd->len = htole32(sizeof (struct ipw_hdr));
	sbd->bd->nfrag = 1 + nsegs;
	sbd->bd->flags = IPW_BD_FLAG_TX_FRAME_802_3 |
	    IPW_BD_FLAG_TX_NOT_LAST_FRAGMENT;

	DPRINTFN(5, ("sending tx hdr (%u, %u, %u, %u, %6D, %6D)\n",
	    shdr->hdr.type, shdr->hdr.subtype, shdr->hdr.encrypted,
	    shdr->hdr.encrypt, shdr->hdr.src_addr, ":", shdr->hdr.dst_addr,
	    ":"));

	sc->txfree--;
	sc->txcur = (sc->txcur + 1) % IPW_NTBD;

	sbuf->m = m0;
	sbuf->ni = ni;

	for (i = 0; i < nsegs; i++) {
		sbd = &sc->stbd_list[sc->txcur];

		sbd->bd->physaddr = htole32(segs[i].ds_addr);
		sbd->bd->len = htole32(segs[i].ds_len);
		sbd->bd->nfrag = 0;
		sbd->bd->flags = IPW_BD_FLAG_TX_FRAME_802_3;
		if (i == nsegs - 1) {
			sbd->type = IPW_SBD_TYPE_DATA;
			sbd->priv = sbuf;
			sbd->bd->flags |= IPW_BD_FLAG_TX_LAST_FRAGMENT;
		} else {
			sbd->type = IPW_SBD_TYPE_NOASSOC;
			sbd->bd->flags |= IPW_BD_FLAG_TX_NOT_LAST_FRAGMENT;
		}

		DPRINTFN(5, ("sending fragment (%d)\n", i));

		sc->txfree--;
		sc->txcur = (sc->txcur + 1) % IPW_NTBD;
	}

	bus_dmamap_sync(sc->hdr_dmat, shdr->map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->txbuf_dmat, sbuf->map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->tbd_dmat, sc->tbd_map, BUS_DMASYNC_PREWRITE);

	/* kick firmware */
	CSR_WRITE_4(sc, IPW_CSR_TX_WRITE, sc->txcur);

	return 0;
}

static int
ipw_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
	const struct ieee80211_bpf_params *params)
{
	/* no support; just discard */
	m_freem(m);
	ieee80211_free_node(ni);
	return 0;
}

static int
ipw_transmit(struct ieee80211com *ic, struct mbuf *m)
{
	struct ipw_softc *sc = ic->ic_softc;
	int error;

	IPW_LOCK(sc);
	if ((sc->flags & IPW_FLAG_RUNNING) == 0) {
		IPW_UNLOCK(sc);
		return (ENXIO);
	}
	error = mbufq_enqueue(&sc->sc_snd, m);
	if (error) {
		IPW_UNLOCK(sc);
		return (error);
	}
	ipw_start(sc);
	IPW_UNLOCK(sc);
	return (0);
}

static void
ipw_start(struct ipw_softc *sc)
{
	struct ieee80211_node *ni;
	struct mbuf *m;

	IPW_LOCK_ASSERT(sc);

	while (sc->txfree >= 1 + IPW_MAX_NSEG &&
	    (m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		ni = (struct ieee80211_node *) m->m_pkthdr.rcvif;
		if (ipw_tx_start(sc, m, ni) != 0) {
			if_inc_counter(ni->ni_vap->iv_ifp,
			    IFCOUNTER_OERRORS, 1);
			ieee80211_free_node(ni);
			break;
		}
		/* start watchdog timer */
		sc->sc_tx_timer = 5;
	}
}

static void
ipw_watchdog(void *arg)
{
	struct ipw_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;

	IPW_LOCK_ASSERT(sc);

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			device_printf(sc->sc_dev, "device timeout\n");
			counter_u64_add(ic->ic_oerrors, 1);
			taskqueue_enqueue(taskqueue_swi, &sc->sc_init_task);
		}
	}
	if (sc->sc_scan_timer > 0) {
		if (--sc->sc_scan_timer == 0) {
			DPRINTFN(3, ("Scan timeout\n"));
			/* End the scan */
			if (sc->flags & IPW_FLAG_SCANNING) {
				IPW_UNLOCK(sc);
				ieee80211_scan_done(TAILQ_FIRST(&ic->ic_vaps));
				IPW_LOCK(sc);
				sc->flags &= ~IPW_FLAG_SCANNING;
			}
		}
	}
	if (sc->flags & IPW_FLAG_RUNNING)
		callout_reset(&sc->sc_wdtimer, hz, ipw_watchdog, sc);
}

static void
ipw_parent(struct ieee80211com *ic)
{
	struct ipw_softc *sc = ic->ic_softc;
	int startall = 0;

	IPW_LOCK(sc);
	if (ic->ic_nrunning > 0) {
		if (!(sc->flags & IPW_FLAG_RUNNING)) {
			ipw_init_locked(sc);
			startall = 1;
		}
	} else if (sc->flags & IPW_FLAG_RUNNING)
		ipw_stop_locked(sc);
	IPW_UNLOCK(sc);
	if (startall)
		ieee80211_start_all(ic);
}

static void
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
		device_printf(sc->sc_dev, "timeout waiting for master\n");

	tmp = CSR_READ_4(sc, IPW_CSR_RST);
	CSR_WRITE_4(sc, IPW_CSR_RST, tmp | IPW_RST_PRINCETON_RESET);

	/* Clear all flags except the following */
	sc->flags &= IPW_FLAG_HAS_RADIO_SWITCH;
}

static int
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

	tmp =  CSR_READ_4(sc, IPW_CSR_RST);
	CSR_WRITE_4(sc, IPW_CSR_RST, tmp | IPW_RST_SW_RESET);

	DELAY(10);

	tmp = CSR_READ_4(sc, IPW_CSR_CTL);
	CSR_WRITE_4(sc, IPW_CSR_CTL, tmp | IPW_CTL_INIT);

	return 0;
}

static int
ipw_waitfordisable(struct ipw_softc *sc, int waitfor)
{
	int ms = hz < 1000 ? 1 : hz/10;
	int i, error;

	for (i = 0; i < 100; i++) {
		if (ipw_read_table1(sc, IPW_INFO_CARD_DISABLED) == waitfor)
			return 0;
		error = msleep(sc, &sc->sc_mtx, PCATCH, __func__, ms);
		if (error == 0 || error != EWOULDBLOCK)
			return 0;
	}
	DPRINTF(("%s: timeout waiting for %s\n",
		__func__, waitfor ? "disable" : "enable"));
	return ETIMEDOUT;
}

static int
ipw_enable(struct ipw_softc *sc)
{
	int error;

	if ((sc->flags & IPW_FLAG_ENABLED) == 0) {
		DPRINTF(("Enable adapter\n"));
		error = ipw_cmd(sc, IPW_CMD_ENABLE, NULL, 0);
		if (error != 0)
			return error;
		error = ipw_waitfordisable(sc, 0);
		if (error != 0)
			return error;
		sc->flags |= IPW_FLAG_ENABLED;
	}
	return 0;
}

static int
ipw_disable(struct ipw_softc *sc)
{
	int error;

	if (sc->flags & IPW_FLAG_ENABLED) {
		DPRINTF(("Disable adapter\n"));
		error = ipw_cmd(sc, IPW_CMD_DISABLE, NULL, 0);
		if (error != 0)
			return error;
		error = ipw_waitfordisable(sc, 1);
		if (error != 0)
			return error;
		sc->flags &= ~IPW_FLAG_ENABLED;
	}
	return 0;
}

/*
 * Upload the microcode to the device.
 */
static int
ipw_load_ucode(struct ipw_softc *sc, const char *uc, int size)
{
	int ntries;

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

	for (ntries = 0; ntries < 10; ntries++) {
		if (MEM_READ_1(sc, 0x210000) & 1)
			break;
		DELAY(10);
	}
	if (ntries == 10) {
		device_printf(sc->sc_dev,
		    "timeout waiting for ucode to initialize\n");
		return EIO;
	}

	MEM_WRITE_4(sc, 0x3000e0, 0);

	return 0;
}

/* set of macros to handle unaligned little endian data in firmware image */
#define GETLE32(p) ((p)[0] | (p)[1] << 8 | (p)[2] << 16 | (p)[3] << 24)
#define GETLE16(p) ((p)[0] | (p)[1] << 8)
static int
ipw_load_firmware(struct ipw_softc *sc, const char *fw, int size)
{
	const uint8_t *p, *end;
	uint32_t tmp, dst;
	uint16_t len;
	int error;

	p = fw;
	end = fw + size;
	while (p < end) {
		dst = GETLE32(p); p += 4;
		len = GETLE16(p); p += 2;

		ipw_write_mem_1(sc, dst, p, len);
		p += len;
	}

	CSR_WRITE_4(sc, IPW_CSR_IO, IPW_IO_GPIO1_ENABLE | IPW_IO_GPIO3_MASK |
	    IPW_IO_LED_OFF);

	/* enable interrupts */
	CSR_WRITE_4(sc, IPW_CSR_INTR_MASK, IPW_INTR_MASK);

	/* kick the firmware */
	CSR_WRITE_4(sc, IPW_CSR_RST, 0);

	tmp = CSR_READ_4(sc, IPW_CSR_CTL);
	CSR_WRITE_4(sc, IPW_CSR_CTL, tmp | IPW_CTL_ALLOW_STANDBY);

	/* wait at most one second for firmware initialization to complete */
	if ((error = msleep(sc, &sc->sc_mtx, 0, "ipwinit", hz)) != 0) {
		device_printf(sc->sc_dev, "timeout waiting for firmware "
		    "initialization to complete\n");
		return error;
	}

	tmp = CSR_READ_4(sc, IPW_CSR_IO);
	CSR_WRITE_4(sc, IPW_CSR_IO, tmp | IPW_IO_GPIO1_MASK |
	    IPW_IO_GPIO3_MASK);

	return 0;
}

static int
ipw_setwepkeys(struct ipw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	struct ipw_wep_key wepkey;
	struct ieee80211_key *wk;
	int error, i;

	for (i = 0; i < IEEE80211_WEP_NKID; i++) {
		wk = &vap->iv_nw_keys[i];

		if (wk->wk_cipher == NULL ||
		    wk->wk_cipher->ic_cipher != IEEE80211_CIPHER_WEP)
			continue;

		wepkey.idx = i;
		wepkey.len = wk->wk_keylen;
		memset(wepkey.key, 0, sizeof wepkey.key);
		memcpy(wepkey.key, wk->wk_key, wk->wk_keylen);
		DPRINTF(("Setting wep key index %u len %u\n", wepkey.idx,
		    wepkey.len));
		error = ipw_cmd(sc, IPW_CMD_SET_WEP_KEY, &wepkey,
		    sizeof wepkey);
		if (error != 0)
			return error;
	}
	return 0;
}

static int
ipw_setwpaie(struct ipw_softc *sc, const void *ie, int ielen)
{
	struct ipw_wpa_ie wpaie;

	memset(&wpaie, 0, sizeof(wpaie));
	wpaie.len = htole32(ielen);
	/* XXX verify length */
	memcpy(&wpaie.ie, ie, ielen);
	DPRINTF(("Setting WPA IE\n"));
	return ipw_cmd(sc, IPW_CMD_SET_WPA_IE, &wpaie, sizeof(wpaie));
}

static int
ipw_setbssid(struct ipw_softc *sc, uint8_t *bssid)
{
	static const uint8_t zerobssid[IEEE80211_ADDR_LEN];

	if (bssid == NULL || bcmp(bssid, zerobssid, IEEE80211_ADDR_LEN) == 0) {
		DPRINTF(("Setting mandatory BSSID to null\n"));
		return ipw_cmd(sc, IPW_CMD_SET_MANDATORY_BSSID, NULL, 0);
	} else {
		DPRINTF(("Setting mandatory BSSID to %6D\n", bssid, ":"));
		return ipw_cmd(sc, IPW_CMD_SET_MANDATORY_BSSID,
			bssid, IEEE80211_ADDR_LEN);
	}
}

static int
ipw_setssid(struct ipw_softc *sc, void *ssid, size_t ssidlen)
{
	if (ssidlen == 0) {
		/*
		 * A bug in the firmware breaks the ``don't associate''
		 * bit in the scan options command.  To compensate for
		 * this install a bogus ssid when no ssid is specified
		 * so the firmware won't try to associate.
		 */
		DPRINTF(("Setting bogus ESSID to WAR firmware bug\n"));
		return ipw_cmd(sc, IPW_CMD_SET_ESSID,
			"\x18\x19\x20\x21\x22\x23\x24\x25\x26\x27"
			"\x28\x29\x2a\x2b\x2c\x2d\x2e\x2f\x30\x31"
			"\x32\x33\x34\x35\x36\x37\x38\x39\x3a\x3b"
			"\x3c\x3d", IEEE80211_NWID_LEN);
	} else {
#ifdef IPW_DEBUG
		if (ipw_debug > 0) {
			printf("Setting ESSID to ");
			ieee80211_print_essid(ssid, ssidlen);
			printf("\n");
		}
#endif
		return ipw_cmd(sc, IPW_CMD_SET_ESSID, ssid, ssidlen);
	}
}

static int
ipw_setscanopts(struct ipw_softc *sc, uint32_t chanmask, uint32_t flags)
{
	struct ipw_scan_options opts;

	DPRINTF(("Scan options: mask 0x%x flags 0x%x\n", chanmask, flags));
	opts.channels = htole32(chanmask);
	opts.flags = htole32(flags);
	return ipw_cmd(sc, IPW_CMD_SET_SCAN_OPTIONS, &opts, sizeof(opts));
}

static int
ipw_scan(struct ipw_softc *sc)
{
	uint32_t params;
	int error;

	DPRINTF(("%s: flags 0x%x\n", __func__, sc->flags));

	if (sc->flags & IPW_FLAG_SCANNING)
		return (EBUSY);
	sc->flags |= IPW_FLAG_SCANNING | IPW_FLAG_HACK;

	/* NB: IPW_SCAN_DO_NOT_ASSOCIATE does not work (we set it anyway) */
	error = ipw_setscanopts(sc, 0x3fff, IPW_SCAN_DO_NOT_ASSOCIATE);
	if (error != 0)
		goto done;

	/*
	 * Setup null/bogus ssid so firmware doesn't use any previous
	 * ssid to try and associate.  This is because the ``don't
	 * associate'' option bit is broken (sigh).
	 */
	error = ipw_setssid(sc, NULL, 0);
	if (error != 0)
		goto done;

	/*
	 * NB: the adapter may be disabled on association lost;
	 *     if so just re-enable it to kick off scanning.
	 */
	DPRINTF(("Starting scan\n"));
	sc->sc_scan_timer = 3;
	if (sc->flags & IPW_FLAG_ENABLED) {
		params = 0;				/* XXX? */
		error = ipw_cmd(sc, IPW_CMD_BROADCAST_SCAN,
				&params, sizeof(params));
	} else
		error = ipw_enable(sc);
done:
	if (error != 0) {
		DPRINTF(("Scan failed\n"));
		sc->flags &= ~(IPW_FLAG_SCANNING | IPW_FLAG_HACK);
	}
	return (error);
}

static int
ipw_setchannel(struct ipw_softc *sc, struct ieee80211_channel *chan)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t data;
	int error;

	data = htole32(ieee80211_chan2ieee(ic, chan));
	DPRINTF(("Setting channel to %u\n", le32toh(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_CHANNEL, &data, sizeof data);
	if (error == 0)
		ipw_setcurchan(sc, chan);
	return error;
}

static void
ipw_assoc(struct ieee80211com *ic, struct ieee80211vap *vap)
{
	struct ipw_softc *sc = ic->ic_softc;
	struct ieee80211_node *ni = vap->iv_bss;
	struct ipw_security security;
	uint32_t data;
	int error;

	IPW_LOCK(sc);
	error = ipw_disable(sc);
	if (error != 0)
		goto done;

	memset(&security, 0, sizeof security);
	security.authmode = (ni->ni_authmode == IEEE80211_AUTH_SHARED) ?
	    IPW_AUTH_SHARED : IPW_AUTH_OPEN;
	security.ciphers = htole32(IPW_CIPHER_NONE);
	DPRINTF(("Setting authmode to %u\n", security.authmode));
	error = ipw_cmd(sc, IPW_CMD_SET_SECURITY_INFO, &security,
	    sizeof security);
	if (error != 0)
		goto done;

	data = htole32(vap->iv_rtsthreshold);
	DPRINTF(("Setting RTS threshold to %u\n", le32toh(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_RTS_THRESHOLD, &data, sizeof data);
	if (error != 0)
		goto done;

	data = htole32(vap->iv_fragthreshold);
	DPRINTF(("Setting frag threshold to %u\n", le32toh(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_FRAG_THRESHOLD, &data, sizeof data);
	if (error != 0)
		goto done;

	if (vap->iv_flags & IEEE80211_F_PRIVACY) {
		error = ipw_setwepkeys(sc);
		if (error != 0)
			goto done;

		if (vap->iv_def_txkey != IEEE80211_KEYIX_NONE) {
			data = htole32(vap->iv_def_txkey);
			DPRINTF(("Setting wep tx key index to %u\n",
				le32toh(data)));
			error = ipw_cmd(sc, IPW_CMD_SET_WEP_KEY_INDEX, &data,
			    sizeof data);
			if (error != 0)
				goto done;
		}
	}

	data = htole32((vap->iv_flags & IEEE80211_F_PRIVACY) ? IPW_WEPON : 0);
	DPRINTF(("Setting wep flags to 0x%x\n", le32toh(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_WEP_FLAGS, &data, sizeof data);
	if (error != 0)
		goto done;

	error = ipw_setssid(sc, ni->ni_essid, ni->ni_esslen);
	if (error != 0)
		goto done;

	error = ipw_setbssid(sc, ni->ni_bssid);
	if (error != 0)
		goto done;

	if (vap->iv_appie_wpa != NULL) {
		struct ieee80211_appie *ie = vap->iv_appie_wpa;
		error = ipw_setwpaie(sc, ie->ie_data, ie->ie_len);
		if (error != 0)
			goto done;
	}
	if (ic->ic_opmode == IEEE80211_M_IBSS) {
		error = ipw_setchannel(sc, ni->ni_chan);
		if (error != 0)
			goto done;
	}

	/* lock scan to ap's channel and enable associate */
	error = ipw_setscanopts(sc,
	    1<<(ieee80211_chan2ieee(ic, ni->ni_chan)-1), 0);
	if (error != 0)
		goto done;

	error = ipw_enable(sc);		/* finally, enable adapter */
	if (error == 0)
		sc->flags |= IPW_FLAG_ASSOCIATING;
done:
	IPW_UNLOCK(sc);
}

static void
ipw_disassoc(struct ieee80211com *ic, struct ieee80211vap *vap)
{
	struct ieee80211_node *ni = vap->iv_bss;
	struct ipw_softc *sc = ic->ic_softc;

	IPW_LOCK(sc);
	DPRINTF(("Disassociate from %6D\n", ni->ni_bssid, ":"));
	/*
	 * NB: don't try to do this if ipw_stop_master has
	 *     shutdown the firmware and disabled interrupts.
	 */
	if (sc->flags & IPW_FLAG_FW_INITED) {
		sc->flags &= ~IPW_FLAG_ASSOCIATED;
		/*
		 * NB: firmware currently ignores bssid parameter, but
		 *     supply it in case this changes (follow linux driver).
		 */
		(void) ipw_cmd(sc, IPW_CMD_DISASSOCIATE,
			ni->ni_bssid, IEEE80211_ADDR_LEN);
	}
	IPW_UNLOCK(sc);
}

/*
 * Handler for sc_init_task.  This is a simple wrapper around ipw_init().
 * It is called on firmware panics or on watchdog timeouts.
 */
static void
ipw_init_task(void *context, int pending)
{
	ipw_init(context);
}

static void
ipw_init(void *priv)
{
	struct ipw_softc *sc = priv;
	struct ieee80211com *ic = &sc->sc_ic;

	IPW_LOCK(sc);
	ipw_init_locked(sc);
	IPW_UNLOCK(sc);

	if (sc->flags & IPW_FLAG_RUNNING)
		ieee80211_start_all(ic);		/* start all vap's */
}

static void
ipw_init_locked(struct ipw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	const struct firmware *fp;
	const struct ipw_firmware_hdr *hdr;
	const char *fw;

	IPW_LOCK_ASSERT(sc);

	DPRINTF(("%s: state %s flags 0x%x\n", __func__,
		ieee80211_state_name[vap->iv_state], sc->flags));

	/*
	 * Avoid re-entrant calls.  We need to release the mutex in ipw_init()
	 * when loading the firmware and we don't want to be called during this
	 * operation.
	 */
	if (sc->flags & IPW_FLAG_INIT_LOCKED)
		return;
	sc->flags |= IPW_FLAG_INIT_LOCKED;

	ipw_stop_locked(sc);

	if (ipw_reset(sc) != 0) {
		device_printf(sc->sc_dev, "could not reset adapter\n");
		goto fail;
	}

	if (sc->sc_firmware == NULL) {
		device_printf(sc->sc_dev, "no firmware\n");
		goto fail;
	}
	/* NB: consistency already checked on load */
	fp = sc->sc_firmware;
	hdr = (const struct ipw_firmware_hdr *)fp->data;

	DPRINTF(("Loading firmware image '%s'\n", fp->name));
	fw = (const char *)fp->data + sizeof *hdr + le32toh(hdr->mainsz);
	if (ipw_load_ucode(sc, fw, le32toh(hdr->ucodesz)) != 0) {
		device_printf(sc->sc_dev, "could not load microcode\n");
		goto fail;
	}

	ipw_stop_master(sc);

	/*
	 * Setup tx, rx and status rings.
	 */
	sc->txold = IPW_NTBD - 1;
	sc->txcur = 0;
	sc->txfree = IPW_NTBD - 2;
	sc->rxcur = IPW_NRBD - 1;

	CSR_WRITE_4(sc, IPW_CSR_TX_BASE,  sc->tbd_phys);
	CSR_WRITE_4(sc, IPW_CSR_TX_SIZE,  IPW_NTBD);
	CSR_WRITE_4(sc, IPW_CSR_TX_READ,  0);
	CSR_WRITE_4(sc, IPW_CSR_TX_WRITE, sc->txcur);

	CSR_WRITE_4(sc, IPW_CSR_RX_BASE,  sc->rbd_phys);
	CSR_WRITE_4(sc, IPW_CSR_RX_SIZE,  IPW_NRBD);
	CSR_WRITE_4(sc, IPW_CSR_RX_READ,  0);
	CSR_WRITE_4(sc, IPW_CSR_RX_WRITE, sc->rxcur);

	CSR_WRITE_4(sc, IPW_CSR_STATUS_BASE, sc->status_phys);

	fw = (const char *)fp->data + sizeof *hdr;
	if (ipw_load_firmware(sc, fw, le32toh(hdr->mainsz)) != 0) {
		device_printf(sc->sc_dev, "could not load firmware\n");
		goto fail;
	}

	sc->flags |= IPW_FLAG_FW_INITED;

	/* retrieve information tables base addresses */
	sc->table1_base = CSR_READ_4(sc, IPW_CSR_TABLE1_BASE);
	sc->table2_base = CSR_READ_4(sc, IPW_CSR_TABLE2_BASE);

	ipw_write_table1(sc, IPW_INFO_LOCK, 0);

	if (ipw_config(sc) != 0) {
		device_printf(sc->sc_dev, "device configuration failed\n");
		goto fail;
	}

	callout_reset(&sc->sc_wdtimer, hz, ipw_watchdog, sc);
	sc->flags |= IPW_FLAG_RUNNING;
	sc->flags &= ~IPW_FLAG_INIT_LOCKED;
	return;

fail:
	ipw_stop_locked(sc);
	sc->flags &= ~IPW_FLAG_INIT_LOCKED;
}

static int
ipw_config(struct ipw_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ipw_configuration config;
	uint32_t data;
	int error;

	error = ipw_disable(sc);
	if (error != 0)
		return error;

	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
	case IEEE80211_M_HOSTAP:
	case IEEE80211_M_WDS:		/* XXX */
		data = htole32(IPW_MODE_BSS);
		break;
	case IEEE80211_M_IBSS:
	case IEEE80211_M_AHDEMO:
		data = htole32(IPW_MODE_IBSS);
		break;
	case IEEE80211_M_MONITOR:
		data = htole32(IPW_MODE_MONITOR);
		break;
	default:
		device_printf(sc->sc_dev, "unknown opmode %d\n", ic->ic_opmode);
		return EINVAL;
	}
	DPRINTF(("Setting mode to %u\n", le32toh(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_MODE, &data, sizeof data);
	if (error != 0)
		return error;

	if (ic->ic_opmode == IEEE80211_M_IBSS ||
	    ic->ic_opmode == IEEE80211_M_MONITOR) {
		error = ipw_setchannel(sc, ic->ic_curchan);
		if (error != 0)
			return error;
	}

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		return ipw_enable(sc);

	config.flags = htole32(IPW_CFG_BSS_MASK | IPW_CFG_IBSS_MASK |
	    IPW_CFG_PREAMBLE_AUTO | IPW_CFG_802_1x_ENABLE);
	if (ic->ic_opmode == IEEE80211_M_IBSS)
		config.flags |= htole32(IPW_CFG_IBSS_AUTO_START);
	if (ic->ic_promisc > 0)
		config.flags |= htole32(IPW_CFG_PROMISCUOUS);
	config.bss_chan = htole32(0x3fff); /* channels 1-14 */
	config.ibss_chan = htole32(0x7ff); /* channels 1-11 */
	DPRINTF(("Setting configuration to 0x%x\n", le32toh(config.flags)));
	error = ipw_cmd(sc, IPW_CMD_SET_CONFIGURATION, &config, sizeof config);
	if (error != 0)
		return error;

	data = htole32(0xf); /* 1, 2, 5.5, 11 */
	DPRINTF(("Setting basic tx rates to 0x%x\n", le32toh(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_BASIC_TX_RATES, &data, sizeof data);
	if (error != 0)
		return error;

	/* Use the same rate set */
	DPRINTF(("Setting msdu tx rates to 0x%x\n", le32toh(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_MSDU_TX_RATES, &data, sizeof data);
	if (error != 0)
		return error;

	/* Use the same rate set */
	DPRINTF(("Setting tx rates to 0x%x\n", le32toh(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_TX_RATES, &data, sizeof data);
	if (error != 0)
		return error;

	data = htole32(IPW_POWER_MODE_CAM);
	DPRINTF(("Setting power mode to %u\n", le32toh(data)));
	error = ipw_cmd(sc, IPW_CMD_SET_POWER_MODE, &data, sizeof data);
	if (error != 0)
		return error;

	if (ic->ic_opmode == IEEE80211_M_IBSS) {
		data = htole32(32); /* default value */
		DPRINTF(("Setting tx power index to %u\n", le32toh(data)));
		error = ipw_cmd(sc, IPW_CMD_SET_TX_POWER_INDEX, &data,
		    sizeof data);
		if (error != 0)
			return error;
	}

	return 0;
}

static void
ipw_stop(void *priv)
{
	struct ipw_softc *sc = priv;

	IPW_LOCK(sc);
	ipw_stop_locked(sc);
	IPW_UNLOCK(sc);
}

static void
ipw_stop_locked(struct ipw_softc *sc)
{
	int i;

	IPW_LOCK_ASSERT(sc);

	callout_stop(&sc->sc_wdtimer);
	ipw_stop_master(sc);

	CSR_WRITE_4(sc, IPW_CSR_RST, IPW_RST_SW_RESET);

	/*
	 * Release tx buffers.
	 */
	for (i = 0; i < IPW_NTBD; i++)
		ipw_release_sbd(sc, &sc->stbd_list[i]);

	sc->sc_tx_timer = 0;
	sc->flags &= ~IPW_FLAG_RUNNING;
}

static int
ipw_sysctl_stats(SYSCTL_HANDLER_ARGS)
{
	struct ipw_softc *sc = arg1;
	uint32_t i, size, buf[256];

	memset(buf, 0, sizeof buf);

	if (!(sc->flags & IPW_FLAG_FW_INITED))
		return SYSCTL_OUT(req, buf, sizeof buf);

	CSR_WRITE_4(sc, IPW_CSR_AUTOINC_ADDR, sc->table1_base);

	size = min(CSR_READ_4(sc, IPW_CSR_AUTOINC_DATA), 256);
	for (i = 1; i < size; i++)
		buf[i] = MEM_READ_4(sc, CSR_READ_4(sc, IPW_CSR_AUTOINC_DATA));

	return SYSCTL_OUT(req, buf, size);
}

static int
ipw_sysctl_radio(SYSCTL_HANDLER_ARGS)
{
	struct ipw_softc *sc = arg1;
	int val;

	val = !((sc->flags & IPW_FLAG_HAS_RADIO_SWITCH) &&
	        (CSR_READ_4(sc, IPW_CSR_IO) & IPW_IO_RADIO_DISABLED));

	return SYSCTL_OUT(req, &val, sizeof val);
}

static uint32_t
ipw_read_table1(struct ipw_softc *sc, uint32_t off)
{
	return MEM_READ_4(sc, MEM_READ_4(sc, sc->table1_base + off));
}

static void
ipw_write_table1(struct ipw_softc *sc, uint32_t off, uint32_t info)
{
	MEM_WRITE_4(sc, MEM_READ_4(sc, sc->table1_base + off), info);
}

#if 0
static int
ipw_read_table2(struct ipw_softc *sc, uint32_t off, void *buf, uint32_t *len)
{
	uint32_t addr, info;
	uint16_t count, size;
	uint32_t total;

	/* addr[4] + count[2] + size[2] */
	addr = MEM_READ_4(sc, sc->table2_base + off);
	info = MEM_READ_4(sc, sc->table2_base + off + 4);

	count = info >> 16;
	size = info & 0xffff;
	total = count * size;

	if (total > *len) {
		*len = total;
		return EINVAL;
	}

	*len = total;
	ipw_read_mem_1(sc, addr, buf, total);

	return 0;
}

static void
ipw_read_mem_1(struct ipw_softc *sc, bus_size_t offset, uint8_t *datap,
    bus_size_t count)
{
	for (; count > 0; offset++, datap++, count--) {
		CSR_WRITE_4(sc, IPW_CSR_INDIRECT_ADDR, offset & ~3);
		*datap = CSR_READ_1(sc, IPW_CSR_INDIRECT_DATA + (offset & 3));
	}
}
#endif

static void
ipw_write_mem_1(struct ipw_softc *sc, bus_size_t offset, const uint8_t *datap,
    bus_size_t count)
{
	for (; count > 0; offset++, datap++, count--) {
		CSR_WRITE_4(sc, IPW_CSR_INDIRECT_ADDR, offset & ~3);
		CSR_WRITE_1(sc, IPW_CSR_INDIRECT_DATA + (offset & 3), *datap);
	}
}

static void
ipw_scan_start(struct ieee80211com *ic)
{
	struct ipw_softc *sc = ic->ic_softc;

	IPW_LOCK(sc);
	ipw_scan(sc);
	IPW_UNLOCK(sc);
}

static void
ipw_getradiocaps(struct ieee80211com *ic,
    int maxchans, int *nchans, struct ieee80211_channel chans[])
{
	struct ipw_softc *sc = ic->ic_softc;
	uint8_t bands[IEEE80211_MODE_BYTES];
	int i;

	memset(bands, 0, sizeof(bands));
	setbit(bands, IEEE80211_MODE_11B);

	for (i = 1; i < 16; i++) {
		if (sc->chanmask & (1 << i)) {
			ieee80211_add_channel(chans, maxchans, nchans,
			    i, 0, 0, 0, bands);
		}
	}

}

static void
ipw_set_channel(struct ieee80211com *ic)
{
	struct ipw_softc *sc = ic->ic_softc;

	IPW_LOCK(sc);
	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		ipw_disable(sc);
		ipw_setchannel(sc, ic->ic_curchan);
		ipw_enable(sc);
	}
	IPW_UNLOCK(sc);
}

static void
ipw_scan_curchan(struct ieee80211_scan_state *ss, unsigned long maxdwell)
{
	/* NB: all channels are scanned at once */
}

static void
ipw_scan_mindwell(struct ieee80211_scan_state *ss)
{
	/* NB: don't try to abort scan; wait for firmware to finish */
}

static void
ipw_scan_end(struct ieee80211com *ic)
{
	struct ipw_softc *sc = ic->ic_softc;

	IPW_LOCK(sc);
	sc->flags &= ~IPW_FLAG_SCANNING;
	IPW_UNLOCK(sc);
}
