/*-
 * Copyright (c) 2006,2007
 *	Damien Bergamini <damien.bergamini@free.fr>
 *	Benjamin Close <Benjamin.Close@clearchain.com>
 * Copyright (c) 2015 Andriy Voskoboinyk <avos@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for Intel PRO/Wireless 3945ABG 802.11 network adapters.
 *
 * The 3945ABG network adapter doesn't use traditional hardware as
 * many other adaptors do. Instead at run time the eeprom is set into a known
 * state and told to load boot firmware. The boot firmware loads an init and a
 * main  binary firmware image into SRAM on the card via DMA.
 * Once the firmware is loaded, the driver/hw then
 * communicate by way of circular dma rings via the SRAM to the firmware.
 *
 * There is 6 memory rings. 1 command ring, 1 rx data ring & 4 tx data rings.
 * The 4 tx data rings allow for prioritization QoS.
 *
 * The rx data ring consists of 32 dma buffers. Two registers are used to
 * indicate where in the ring the driver and the firmware are up to. The
 * driver sets the initial read index (reg1) and the initial write index (reg2),
 * the firmware updates the read index (reg1) on rx of a packet and fires an
 * interrupt. The driver then processes the buffers starting at reg1 indicating
 * to the firmware which buffers have been accessed by updating reg2. At the
 * same time allocating new memory for the processed buffer.
 *
 * A similar thing happens with the tx rings. The difference is the firmware
 * stop processing buffers once the queue is full and until confirmation
 * of a successful transmition (tx_done) has occurred.
 *
 * The command ring operates in the same manner as the tx queues.
 *
 * All communication direct to the card (ie eeprom) is classed as Stage1
 * communication
 *
 * All communication via the firmware to the card is classed as State2.
 * The firmware consists of 2 parts. A bootstrap firmware and a runtime
 * firmware. The bootstrap firmware and runtime firmware are loaded
 * from host memory via dma to the card then told to execute. From this point
 * on the majority of communications between the driver and the card goes
 * via the firmware.
 */

#include "opt_wlan.h"
#include "opt_wpi.h"

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

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_ratectl.h>

#include <dev/wpi/if_wpireg.h>
#include <dev/wpi/if_wpivar.h>
#include <dev/wpi/if_wpi_debug.h>

struct wpi_ident {
	uint16_t	vendor;
	uint16_t	device;
	uint16_t	subdevice;
	const char	*name;
};

static const struct wpi_ident wpi_ident_table[] = {
	/* The below entries support ABG regardless of the subid */
	{ 0x8086, 0x4222,    0x0, "Intel(R) PRO/Wireless 3945ABG" },
	{ 0x8086, 0x4227,    0x0, "Intel(R) PRO/Wireless 3945ABG" },
	/* The below entries only support BG */
	{ 0x8086, 0x4222, 0x1005, "Intel(R) PRO/Wireless 3945BG"  },
	{ 0x8086, 0x4222, 0x1034, "Intel(R) PRO/Wireless 3945BG"  },
	{ 0x8086, 0x4227, 0x1014, "Intel(R) PRO/Wireless 3945BG"  },
	{ 0x8086, 0x4222, 0x1044, "Intel(R) PRO/Wireless 3945BG"  },
	{ 0, 0, 0, NULL }
};

static int	wpi_probe(device_t);
static int	wpi_attach(device_t);
static void	wpi_radiotap_attach(struct wpi_softc *);
static void	wpi_sysctlattach(struct wpi_softc *);
static void	wpi_init_beacon(struct wpi_vap *);
static struct ieee80211vap *wpi_vap_create(struct ieee80211com *,
		    const char [IFNAMSIZ], int, enum ieee80211_opmode, int,
		    const uint8_t [IEEE80211_ADDR_LEN],
		    const uint8_t [IEEE80211_ADDR_LEN]);
static void	wpi_vap_delete(struct ieee80211vap *);
static int	wpi_detach(device_t);
static int	wpi_shutdown(device_t);
static int	wpi_suspend(device_t);
static int	wpi_resume(device_t);
static int	wpi_nic_lock(struct wpi_softc *);
static int	wpi_read_prom_data(struct wpi_softc *, uint32_t, void *, int);
static void	wpi_dma_map_addr(void *, bus_dma_segment_t *, int, int);
static int	wpi_dma_contig_alloc(struct wpi_softc *, struct wpi_dma_info *,
		    void **, bus_size_t, bus_size_t);
static void	wpi_dma_contig_free(struct wpi_dma_info *);
static int	wpi_alloc_shared(struct wpi_softc *);
static void	wpi_free_shared(struct wpi_softc *);
static int	wpi_alloc_fwmem(struct wpi_softc *);
static void	wpi_free_fwmem(struct wpi_softc *);
static int	wpi_alloc_rx_ring(struct wpi_softc *);
static void	wpi_update_rx_ring(struct wpi_softc *);
static void	wpi_update_rx_ring_ps(struct wpi_softc *);
static void	wpi_reset_rx_ring(struct wpi_softc *);
static void	wpi_free_rx_ring(struct wpi_softc *);
static int	wpi_alloc_tx_ring(struct wpi_softc *, struct wpi_tx_ring *,
		    uint8_t);
static void	wpi_update_tx_ring(struct wpi_softc *, struct wpi_tx_ring *);
static void	wpi_update_tx_ring_ps(struct wpi_softc *,
		    struct wpi_tx_ring *);
static void	wpi_reset_tx_ring(struct wpi_softc *, struct wpi_tx_ring *);
static void	wpi_free_tx_ring(struct wpi_softc *, struct wpi_tx_ring *);
static int	wpi_read_eeprom(struct wpi_softc *,
		    uint8_t macaddr[IEEE80211_ADDR_LEN]);
static uint32_t	wpi_eeprom_channel_flags(struct wpi_eeprom_chan *);
static void	wpi_read_eeprom_band(struct wpi_softc *, uint8_t, int, int *,
		    struct ieee80211_channel[]);
static int	wpi_read_eeprom_channels(struct wpi_softc *, uint8_t);
static struct wpi_eeprom_chan *wpi_find_eeprom_channel(struct wpi_softc *,
		    struct ieee80211_channel *);
static void	wpi_getradiocaps(struct ieee80211com *, int, int *,
		    struct ieee80211_channel[]);
static int	wpi_setregdomain(struct ieee80211com *,
		    struct ieee80211_regdomain *, int,
		    struct ieee80211_channel[]);
static int	wpi_read_eeprom_group(struct wpi_softc *, uint8_t);
static struct ieee80211_node *wpi_node_alloc(struct ieee80211vap *,
		    const uint8_t mac[IEEE80211_ADDR_LEN]);
static void	wpi_node_free(struct ieee80211_node *);
static void	wpi_ibss_recv_mgmt(struct ieee80211_node *, struct mbuf *, int,
		    const struct ieee80211_rx_stats *,
		    int, int);
static void	wpi_restore_node(void *, struct ieee80211_node *);
static void	wpi_restore_node_table(struct wpi_softc *, struct wpi_vap *);
static int	wpi_newstate(struct ieee80211vap *, enum ieee80211_state, int);
static void	wpi_calib_timeout(void *);
static void	wpi_rx_done(struct wpi_softc *, struct wpi_rx_desc *,
		    struct wpi_rx_data *);
static void	wpi_rx_statistics(struct wpi_softc *, struct wpi_rx_desc *,
		    struct wpi_rx_data *);
static void	wpi_tx_done(struct wpi_softc *, struct wpi_rx_desc *);
static void	wpi_cmd_done(struct wpi_softc *, struct wpi_rx_desc *);
static void	wpi_notif_intr(struct wpi_softc *);
static void	wpi_wakeup_intr(struct wpi_softc *);
#ifdef WPI_DEBUG
static void	wpi_debug_registers(struct wpi_softc *);
#endif
static void	wpi_fatal_intr(struct wpi_softc *);
static void	wpi_intr(void *);
static void	wpi_free_txfrags(struct wpi_softc *, uint16_t);
static int	wpi_cmd2(struct wpi_softc *, struct wpi_buf *);
static int	wpi_tx_data(struct wpi_softc *, struct mbuf *,
		    struct ieee80211_node *);
static int	wpi_tx_data_raw(struct wpi_softc *, struct mbuf *,
		    struct ieee80211_node *,
		    const struct ieee80211_bpf_params *);
static int	wpi_raw_xmit(struct ieee80211_node *, struct mbuf *,
		    const struct ieee80211_bpf_params *);
static int	wpi_transmit(struct ieee80211com *, struct mbuf *);
static void	wpi_watchdog_rfkill(void *);
static void	wpi_scan_timeout(void *);
static void	wpi_tx_timeout(void *);
static void	wpi_parent(struct ieee80211com *);
static int	wpi_cmd(struct wpi_softc *, uint8_t, const void *, uint16_t,
		    int);
static int	wpi_mrr_setup(struct wpi_softc *);
static int	wpi_add_node(struct wpi_softc *, struct ieee80211_node *);
static int	wpi_add_broadcast_node(struct wpi_softc *, int);
static int	wpi_add_ibss_node(struct wpi_softc *, struct ieee80211_node *);
static void	wpi_del_node(struct wpi_softc *, struct ieee80211_node *);
static int	wpi_updateedca(struct ieee80211com *);
static void	wpi_set_promisc(struct wpi_softc *);
static void	wpi_update_promisc(struct ieee80211com *);
static void	wpi_update_mcast(struct ieee80211com *);
static void	wpi_set_led(struct wpi_softc *, uint8_t, uint8_t, uint8_t);
static int	wpi_set_timing(struct wpi_softc *, struct ieee80211_node *);
static void	wpi_power_calibration(struct wpi_softc *);
static int	wpi_set_txpower(struct wpi_softc *, int);
static int	wpi_get_power_index(struct wpi_softc *,
		    struct wpi_power_group *, uint8_t, int, int);
static int	wpi_set_pslevel(struct wpi_softc *, uint8_t, int, int);
static int	wpi_send_btcoex(struct wpi_softc *);
static int	wpi_send_rxon(struct wpi_softc *, int, int);
static int	wpi_config(struct wpi_softc *);
static uint16_t	wpi_get_active_dwell_time(struct wpi_softc *,
		    struct ieee80211_channel *, uint8_t);
static uint16_t	wpi_limit_dwell(struct wpi_softc *, uint16_t);
static uint16_t	wpi_get_passive_dwell_time(struct wpi_softc *,
		    struct ieee80211_channel *);
static uint32_t	wpi_get_scan_pause_time(uint32_t, uint16_t);
static int	wpi_scan(struct wpi_softc *, struct ieee80211_channel *);
static int	wpi_auth(struct wpi_softc *, struct ieee80211vap *);
static int	wpi_config_beacon(struct wpi_vap *);
static int	wpi_setup_beacon(struct wpi_softc *, struct ieee80211_node *);
static void	wpi_update_beacon(struct ieee80211vap *, int);
static void	wpi_newassoc(struct ieee80211_node *, int);
static int	wpi_run(struct wpi_softc *, struct ieee80211vap *);
static int	wpi_load_key(struct ieee80211_node *,
		    const struct ieee80211_key *);
static void	wpi_load_key_cb(void *, struct ieee80211_node *);
static int	wpi_set_global_keys(struct ieee80211_node *);
static int	wpi_del_key(struct ieee80211_node *,
		    const struct ieee80211_key *);
static void	wpi_del_key_cb(void *, struct ieee80211_node *);
static int	wpi_process_key(struct ieee80211vap *,
		    const struct ieee80211_key *, int);
static int	wpi_key_set(struct ieee80211vap *,
		    const struct ieee80211_key *);
static int	wpi_key_delete(struct ieee80211vap *,
		    const struct ieee80211_key *);
static int	wpi_post_alive(struct wpi_softc *);
static int	wpi_load_bootcode(struct wpi_softc *, const uint8_t *,
		    uint32_t);
static int	wpi_load_firmware(struct wpi_softc *);
static int	wpi_read_firmware(struct wpi_softc *);
static void	wpi_unload_firmware(struct wpi_softc *);
static int	wpi_clock_wait(struct wpi_softc *);
static int	wpi_apm_init(struct wpi_softc *);
static void	wpi_apm_stop_master(struct wpi_softc *);
static void	wpi_apm_stop(struct wpi_softc *);
static void	wpi_nic_config(struct wpi_softc *);
static int	wpi_hw_init(struct wpi_softc *);
static void	wpi_hw_stop(struct wpi_softc *);
static void	wpi_radio_on(void *, int);
static void	wpi_radio_off(void *, int);
static int	wpi_init(struct wpi_softc *);
static void	wpi_stop_locked(struct wpi_softc *);
static void	wpi_stop(struct wpi_softc *);
static void	wpi_scan_start(struct ieee80211com *);
static void	wpi_scan_end(struct ieee80211com *);
static void	wpi_set_channel(struct ieee80211com *);
static void	wpi_scan_curchan(struct ieee80211_scan_state *, unsigned long);
static void	wpi_scan_mindwell(struct ieee80211_scan_state *);

static device_method_t wpi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		wpi_probe),
	DEVMETHOD(device_attach,	wpi_attach),
	DEVMETHOD(device_detach,	wpi_detach),
	DEVMETHOD(device_shutdown,	wpi_shutdown),
	DEVMETHOD(device_suspend,	wpi_suspend),
	DEVMETHOD(device_resume,	wpi_resume),

	DEVMETHOD_END
};

static driver_t wpi_driver = {
	"wpi",
	wpi_methods,
	sizeof (struct wpi_softc)
};
static devclass_t wpi_devclass;

DRIVER_MODULE(wpi, pci, wpi_driver, wpi_devclass, NULL, NULL);

MODULE_VERSION(wpi, 1);

MODULE_DEPEND(wpi, pci,  1, 1, 1);
MODULE_DEPEND(wpi, wlan, 1, 1, 1);
MODULE_DEPEND(wpi, firmware, 1, 1, 1);

static int
wpi_probe(device_t dev)
{
	const struct wpi_ident *ident;

	for (ident = wpi_ident_table; ident->name != NULL; ident++) {
		if (pci_get_vendor(dev) == ident->vendor &&
		    pci_get_device(dev) == ident->device) {
			device_set_desc(dev, ident->name);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return ENXIO;
}

static int
wpi_attach(device_t dev)
{
	struct wpi_softc *sc = (struct wpi_softc *)device_get_softc(dev);
	struct ieee80211com *ic;
	uint8_t i;
	int error, rid;
#ifdef WPI_DEBUG
	int supportsa = 1;
	const struct wpi_ident *ident;
#endif

	sc->sc_dev = dev;

#ifdef WPI_DEBUG
	error = resource_int_value(device_get_name(sc->sc_dev),
	    device_get_unit(sc->sc_dev), "debug", &(sc->sc_debug));
	if (error != 0)
		sc->sc_debug = 0;
#else
	sc->sc_debug = 0;
#endif

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	/*
	 * Get the offset of the PCI Express Capability Structure in PCI
	 * Configuration Space.
	 */
	error = pci_find_cap(dev, PCIY_EXPRESS, &sc->sc_cap_off);
	if (error != 0) {
		device_printf(dev, "PCIe capability structure not found!\n");
		return error;
	}

	/*
	 * Some card's only support 802.11b/g not a, check to see if
	 * this is one such card. A 0x0 in the subdevice table indicates
	 * the entire subdevice range is to be ignored.
	 */
#ifdef WPI_DEBUG
	for (ident = wpi_ident_table; ident->name != NULL; ident++) {
		if (ident->subdevice &&
		    pci_get_subdevice(dev) == ident->subdevice) {
		    supportsa = 0;
		    break;
		}
	}
#endif

	/* Clear device-specific "PCI retry timeout" register (41h). */
	pci_write_config(dev, 0x41, 0, 1);

	/* Enable bus-mastering. */
	pci_enable_busmaster(dev);

	rid = PCIR_BAR(0);
	sc->mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem == NULL) {
		device_printf(dev, "can't map mem space\n");
		return ENOMEM;
	}
	sc->sc_st = rman_get_bustag(sc->mem);
	sc->sc_sh = rman_get_bushandle(sc->mem);

	rid = 1;
	if (pci_alloc_msi(dev, &rid) == 0)
		rid = 1;
	else
		rid = 0;
	/* Install interrupt handler. */
	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE |
	    (rid != 0 ? 0 : RF_SHAREABLE));
	if (sc->irq == NULL) {
		device_printf(dev, "can't map interrupt\n");
		error = ENOMEM;
		goto fail;
	}

	WPI_LOCK_INIT(sc);
	WPI_TX_LOCK_INIT(sc);
	WPI_RXON_LOCK_INIT(sc);
	WPI_NT_LOCK_INIT(sc);
	WPI_TXQ_LOCK_INIT(sc);
	WPI_TXQ_STATE_LOCK_INIT(sc);

	/* Allocate DMA memory for firmware transfers. */
	if ((error = wpi_alloc_fwmem(sc)) != 0) {
		device_printf(dev,
		    "could not allocate memory for firmware, error %d\n",
		    error);
		goto fail;
	}

	/* Allocate shared page. */
	if ((error = wpi_alloc_shared(sc)) != 0) {
		device_printf(dev, "could not allocate shared page\n");
		goto fail;
	}

	/* Allocate TX rings - 4 for QoS purposes, 1 for commands. */
	for (i = 0; i < WPI_DRV_NTXQUEUES; i++) {
		if ((error = wpi_alloc_tx_ring(sc, &sc->txq[i], i)) != 0) {
			device_printf(dev,
			    "could not allocate TX ring %d, error %d\n", i,
			    error);
			goto fail;
		}
	}

	/* Allocate RX ring. */
	if ((error = wpi_alloc_rx_ring(sc)) != 0) {
		device_printf(dev, "could not allocate RX ring, error %d\n",
		    error);
		goto fail;
	}

	/* Clear pending interrupts. */
	WPI_WRITE(sc, WPI_INT, 0xffffffff);

	ic = &sc->sc_ic;
	ic->ic_softc = sc;
	ic->ic_name = device_get_nameunit(dev);
	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */

	/* Set device capabilities. */
	ic->ic_caps =
		  IEEE80211_C_STA		/* station mode supported */
		| IEEE80211_C_IBSS		/* IBSS mode supported */
		| IEEE80211_C_HOSTAP		/* Host access point mode */
		| IEEE80211_C_MONITOR		/* monitor mode supported */
		| IEEE80211_C_AHDEMO		/* adhoc demo mode */
		| IEEE80211_C_BGSCAN		/* capable of bg scanning */
		| IEEE80211_C_TXFRAG		/* handle tx frags */
		| IEEE80211_C_TXPMGT		/* tx power management */
		| IEEE80211_C_SHSLOT		/* short slot time supported */
		| IEEE80211_C_WPA		/* 802.11i */
		| IEEE80211_C_SHPREAMBLE	/* short preamble supported */
		| IEEE80211_C_WME		/* 802.11e */
		| IEEE80211_C_PMGT		/* Station-side power mgmt */
		;

	ic->ic_cryptocaps =
		  IEEE80211_CRYPTO_AES_CCM;

	/*
	 * Read in the eeprom and also setup the channels for
	 * net80211. We don't set the rates as net80211 does this for us
	 */
	if ((error = wpi_read_eeprom(sc, ic->ic_macaddr)) != 0) {
		device_printf(dev, "could not read EEPROM, error %d\n",
		    error);
		goto fail;
	}

#ifdef WPI_DEBUG
	if (bootverbose) {
		device_printf(sc->sc_dev, "Regulatory Domain: %.4s\n",
		    sc->domain);
		device_printf(sc->sc_dev, "Hardware Type: %c\n",
		    sc->type > 1 ? 'B': '?');
		device_printf(sc->sc_dev, "Hardware Revision: %c\n",
		    ((sc->rev & 0xf0) == 0xd0) ? 'D': '?');
		device_printf(sc->sc_dev, "SKU %s support 802.11a\n",
		    supportsa ? "does" : "does not");

		/* XXX hw_config uses the PCIDEV for the Hardware rev. Must
		   check what sc->rev really represents - benjsc 20070615 */
	}
#endif

	ieee80211_ifattach(ic);
	ic->ic_vap_create = wpi_vap_create;
	ic->ic_vap_delete = wpi_vap_delete;
	ic->ic_parent = wpi_parent;
	ic->ic_raw_xmit = wpi_raw_xmit;
	ic->ic_transmit = wpi_transmit;
	ic->ic_node_alloc = wpi_node_alloc;
	sc->sc_node_free = ic->ic_node_free;
	ic->ic_node_free = wpi_node_free;
	ic->ic_wme.wme_update = wpi_updateedca;
	ic->ic_update_promisc = wpi_update_promisc;
	ic->ic_update_mcast = wpi_update_mcast;
	ic->ic_newassoc = wpi_newassoc;
	ic->ic_scan_start = wpi_scan_start;
	ic->ic_scan_end = wpi_scan_end;
	ic->ic_set_channel = wpi_set_channel;
	ic->ic_scan_curchan = wpi_scan_curchan;
	ic->ic_scan_mindwell = wpi_scan_mindwell;
	ic->ic_getradiocaps = wpi_getradiocaps;
	ic->ic_setregdomain = wpi_setregdomain;

	sc->sc_update_rx_ring = wpi_update_rx_ring;
	sc->sc_update_tx_ring = wpi_update_tx_ring;

	wpi_radiotap_attach(sc);

	/* Setup Tx status flags (constant). */
	sc->sc_txs.flags = IEEE80211_RATECTL_STATUS_PKTLEN |
	    IEEE80211_RATECTL_STATUS_SHORT_RETRY |
	    IEEE80211_RATECTL_STATUS_LONG_RETRY;

	callout_init_mtx(&sc->calib_to, &sc->rxon_mtx, 0);
	callout_init_mtx(&sc->scan_timeout, &sc->rxon_mtx, 0);
	callout_init_mtx(&sc->tx_timeout, &sc->txq_state_mtx, 0);
	callout_init_mtx(&sc->watchdog_rfkill, &sc->sc_mtx, 0);
	TASK_INIT(&sc->sc_radiooff_task, 0, wpi_radio_off, sc);
	TASK_INIT(&sc->sc_radioon_task, 0, wpi_radio_on, sc);

	wpi_sysctlattach(sc);

	/*
	 * Hook our interrupt after all initialization is complete.
	 */
	error = bus_setup_intr(dev, sc->irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, wpi_intr, sc, &sc->sc_ih);
	if (error != 0) {
		device_printf(dev, "can't establish interrupt, error %d\n",
		    error);
		goto fail;
	}

	if (bootverbose)
		ieee80211_announce(ic);

#ifdef WPI_DEBUG
	if (sc->sc_debug & WPI_DEBUG_HW)
		ieee80211_announce_channels(ic);
#endif

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);
	return 0;

fail:	wpi_detach(dev);
	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END_ERR, __func__);
	return error;
}

/*
 * Attach the interface to 802.11 radiotap.
 */
static void
wpi_radiotap_attach(struct wpi_softc *sc)
{
	struct wpi_rx_radiotap_header *rxtap = &sc->sc_rxtap;
	struct wpi_tx_radiotap_header *txtap = &sc->sc_txtap;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);
	ieee80211_radiotap_attach(&sc->sc_ic,
	    &txtap->wt_ihdr, sizeof(*txtap), WPI_TX_RADIOTAP_PRESENT,
	    &rxtap->wr_ihdr, sizeof(*rxtap), WPI_RX_RADIOTAP_PRESENT);
	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);
}

static void
wpi_sysctlattach(struct wpi_softc *sc)
{
#ifdef WPI_DEBUG
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->sc_dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->sc_dev);

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "debug", CTLFLAG_RW, &sc->sc_debug, sc->sc_debug,
		"control debugging printfs");
#endif
}

static void
wpi_init_beacon(struct wpi_vap *wvp)
{
	struct wpi_buf *bcn = &wvp->wv_bcbuf;
	struct wpi_cmd_beacon *cmd = (struct wpi_cmd_beacon *)&bcn->data;

	cmd->id = WPI_ID_BROADCAST;
	cmd->ofdm_mask = 0xff;
	cmd->cck_mask = 0x0f;
	cmd->lifetime = htole32(WPI_LIFETIME_INFINITE);

	/*
	 * XXX WPI_TX_AUTO_SEQ seems to be ignored - workaround this issue
	 * XXX by using WPI_TX_NEED_ACK instead (with some side effects).
	 */
	cmd->flags = htole32(WPI_TX_NEED_ACK | WPI_TX_INSERT_TSTAMP);

	bcn->code = WPI_CMD_SET_BEACON;
	bcn->ac = WPI_CMD_QUEUE_NUM;
	bcn->size = sizeof(struct wpi_cmd_beacon);
}

static struct ieee80211vap *
wpi_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ], int unit,
    enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct wpi_vap *wvp;
	struct ieee80211vap *vap;

	if (!TAILQ_EMPTY(&ic->ic_vaps))		/* only one at a time */
		return NULL;

	wvp = malloc(sizeof(struct wpi_vap), M_80211_VAP, M_WAITOK | M_ZERO);
	vap = &wvp->wv_vap;
	ieee80211_vap_setup(ic, vap, name, unit, opmode, flags, bssid);

	if (opmode == IEEE80211_M_IBSS || opmode == IEEE80211_M_HOSTAP) {
		WPI_VAP_LOCK_INIT(wvp);
		wpi_init_beacon(wvp);
	}

	/* Override with driver methods. */
	vap->iv_key_set = wpi_key_set;
	vap->iv_key_delete = wpi_key_delete;
	if (opmode == IEEE80211_M_IBSS) {
		wvp->wv_recv_mgmt = vap->iv_recv_mgmt;
		vap->iv_recv_mgmt = wpi_ibss_recv_mgmt;
	}
	wvp->wv_newstate = vap->iv_newstate;
	vap->iv_newstate = wpi_newstate;
	vap->iv_update_beacon = wpi_update_beacon;
	vap->iv_max_aid = WPI_ID_IBSS_MAX - WPI_ID_IBSS_MIN + 1;

	ieee80211_ratectl_init(vap);
	/* Complete setup. */
	ieee80211_vap_attach(vap, ieee80211_media_change,
	    ieee80211_media_status, mac);
	ic->ic_opmode = opmode;
	return vap;
}

static void
wpi_vap_delete(struct ieee80211vap *vap)
{
	struct wpi_vap *wvp = WPI_VAP(vap);
	struct wpi_buf *bcn = &wvp->wv_bcbuf;
	enum ieee80211_opmode opmode = vap->iv_opmode;

	ieee80211_ratectl_deinit(vap);
	ieee80211_vap_detach(vap);

	if (opmode == IEEE80211_M_IBSS || opmode == IEEE80211_M_HOSTAP) {
		if (bcn->m != NULL)
			m_freem(bcn->m);

		WPI_VAP_LOCK_DESTROY(wvp);
	}

	free(wvp, M_80211_VAP);
}

static int
wpi_detach(device_t dev)
{
	struct wpi_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t qid;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	if (ic->ic_vap_create == wpi_vap_create) {
		ieee80211_draintask(ic, &sc->sc_radioon_task);
		ieee80211_draintask(ic, &sc->sc_radiooff_task);

		wpi_stop(sc);

		callout_drain(&sc->watchdog_rfkill);
		callout_drain(&sc->tx_timeout);
		callout_drain(&sc->scan_timeout);
		callout_drain(&sc->calib_to);
		ieee80211_ifdetach(ic);
	}

	/* Uninstall interrupt handler. */
	if (sc->irq != NULL) {
		bus_teardown_intr(dev, sc->irq, sc->sc_ih);
		bus_release_resource(dev, SYS_RES_IRQ, rman_get_rid(sc->irq),
		    sc->irq);
		pci_release_msi(dev);
	}

	if (sc->txq[0].data_dmat) {
		/* Free DMA resources. */
		for (qid = 0; qid < WPI_DRV_NTXQUEUES; qid++)
			wpi_free_tx_ring(sc, &sc->txq[qid]);

		wpi_free_rx_ring(sc);
		wpi_free_shared(sc);
	}

	if (sc->fw_dma.tag)
		wpi_free_fwmem(sc);
		
	if (sc->mem != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->mem), sc->mem);

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);
	WPI_TXQ_STATE_LOCK_DESTROY(sc);
	WPI_TXQ_LOCK_DESTROY(sc);
	WPI_NT_LOCK_DESTROY(sc);
	WPI_RXON_LOCK_DESTROY(sc);
	WPI_TX_LOCK_DESTROY(sc);
	WPI_LOCK_DESTROY(sc);
	return 0;
}

static int
wpi_shutdown(device_t dev)
{
	struct wpi_softc *sc = device_get_softc(dev);

	wpi_stop(sc);
	return 0;
}

static int
wpi_suspend(device_t dev)
{
	struct wpi_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;

	ieee80211_suspend_all(ic);
	return 0;
}

static int
wpi_resume(device_t dev)
{
	struct wpi_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic = &sc->sc_ic;

	/* Clear device-specific "PCI retry timeout" register (41h). */
	pci_write_config(dev, 0x41, 0, 1);

	ieee80211_resume_all(ic);
	return 0;
}

/*
 * Grab exclusive access to NIC memory.
 */
static int
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

	device_printf(sc->sc_dev, "could not lock memory\n");

	return ETIMEDOUT;
}

/*
 * Release lock on NIC memory.
 */
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
    const uint32_t *data, uint32_t count)
{
	for (; count != 0; count--, data++, addr += 4)
		wpi_prph_write(sc, addr, *data);
}

static __inline uint32_t
wpi_mem_read(struct wpi_softc *sc, uint32_t addr)
{
	WPI_WRITE(sc, WPI_MEM_RADDR, addr);
	WPI_BARRIER_READ_WRITE(sc);
	return WPI_READ(sc, WPI_MEM_RDATA);
}

static __inline void
wpi_mem_read_region_4(struct wpi_softc *sc, uint32_t addr, uint32_t *data,
    int count)
{
	for (; count > 0; count--, addr += 4)
		*data++ = wpi_mem_read(sc, addr);
}

static int
wpi_read_prom_data(struct wpi_softc *sc, uint32_t addr, void *data, int count)
{
	uint8_t *out = data;
	uint32_t val;
	int error, ntries;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	if ((error = wpi_nic_lock(sc)) != 0)
		return error;

	for (; count > 0; count -= 2, addr++) {
		WPI_WRITE(sc, WPI_EEPROM, addr << 2);
		for (ntries = 0; ntries < 10; ntries++) {
			val = WPI_READ(sc, WPI_EEPROM);
			if (val & WPI_EEPROM_READ_VALID)
				break;
			DELAY(5);
		}
		if (ntries == 10) {
			device_printf(sc->sc_dev,
			    "timeout reading ROM at 0x%x\n", addr);
			return ETIMEDOUT;
		}
		*out++= val >> 16;
		if (count > 1)
			*out ++= val >> 24;
	}

	wpi_nic_unlock(sc);

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);

	return 0;
}

static void
wpi_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	if (error != 0)
		return;
	KASSERT(nsegs == 1, ("too many DMA segments, %d should be 1", nsegs));
	*(bus_addr_t *)arg = segs[0].ds_addr;
}

/*
 * Allocates a contiguous block of dma memory of the requested size and
 * alignment.
 */
static int
wpi_dma_contig_alloc(struct wpi_softc *sc, struct wpi_dma_info *dma,
    void **kvap, bus_size_t size, bus_size_t alignment)
{
	int error;

	dma->tag = NULL;
	dma->size = size;

	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), alignment,
	    0, BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL, size,
	    1, size, 0, NULL, NULL, &dma->tag);
	if (error != 0)
		goto fail;

	error = bus_dmamem_alloc(dma->tag, (void **)&dma->vaddr,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO | BUS_DMA_COHERENT, &dma->map);
	if (error != 0)
		goto fail;

	error = bus_dmamap_load(dma->tag, dma->map, dma->vaddr, size,
	    wpi_dma_map_addr, &dma->paddr, BUS_DMA_NOWAIT);
	if (error != 0)
		goto fail;

	bus_dmamap_sync(dma->tag, dma->map, BUS_DMASYNC_PREWRITE);

	if (kvap != NULL)
		*kvap = dma->vaddr;

	return 0;

fail:	wpi_dma_contig_free(dma);
	return error;
}

static void
wpi_dma_contig_free(struct wpi_dma_info *dma)
{
	if (dma->vaddr != NULL) {
		bus_dmamap_sync(dma->tag, dma->map,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(dma->tag, dma->map);
		bus_dmamem_free(dma->tag, dma->vaddr, dma->map);
		dma->vaddr = NULL;
	}
	if (dma->tag != NULL) {
		bus_dma_tag_destroy(dma->tag);
		dma->tag = NULL;
	}
}

/*
 * Allocate a shared page between host and NIC.
 */
static int
wpi_alloc_shared(struct wpi_softc *sc)
{
	/* Shared buffer must be aligned on a 4KB boundary. */
	return wpi_dma_contig_alloc(sc, &sc->shared_dma,
	    (void **)&sc->shared, sizeof (struct wpi_shared), 4096);
}

static void
wpi_free_shared(struct wpi_softc *sc)
{
	wpi_dma_contig_free(&sc->shared_dma);
}

/*
 * Allocate DMA-safe memory for firmware transfer.
 */
static int
wpi_alloc_fwmem(struct wpi_softc *sc)
{
	/* Must be aligned on a 16-byte boundary. */
	return wpi_dma_contig_alloc(sc, &sc->fw_dma, NULL,
	    WPI_FW_TEXT_MAXSZ + WPI_FW_DATA_MAXSZ, 16);
}

static void
wpi_free_fwmem(struct wpi_softc *sc)
{
	wpi_dma_contig_free(&sc->fw_dma);
}

static int
wpi_alloc_rx_ring(struct wpi_softc *sc)
{
	struct wpi_rx_ring *ring = &sc->rxq;
	bus_size_t size;
	int i, error;

	ring->cur = 0;
	ring->update = 0;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	/* Allocate RX descriptors (16KB aligned.) */
	size = WPI_RX_RING_COUNT * sizeof (uint32_t);
	error = wpi_dma_contig_alloc(sc, &ring->desc_dma,
	    (void **)&ring->desc, size, WPI_RING_DMA_ALIGN);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not allocate RX ring DMA memory, error %d\n",
		    __func__, error);
		goto fail;
	}

	/* Create RX buffer DMA tag. */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 1, 0, 
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    MJUMPAGESIZE, 1, MJUMPAGESIZE, 0, NULL, NULL, &ring->data_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not create RX buf DMA tag, error %d\n",
		    __func__, error);
		goto fail;
	}

	/*
	 * Allocate and map RX buffers.
	 */
	for (i = 0; i < WPI_RX_RING_COUNT; i++) {
		struct wpi_rx_data *data = &ring->data[i];
		bus_addr_t paddr;

		error = bus_dmamap_create(ring->data_dmat, 0, &data->map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not create RX buf DMA map, error %d\n",
			    __func__, error);
			goto fail;
		}

		data->m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MJUMPAGESIZE);
		if (data->m == NULL) {
			device_printf(sc->sc_dev,
			    "%s: could not allocate RX mbuf\n", __func__);
			error = ENOBUFS;
			goto fail;
		}

		error = bus_dmamap_load(ring->data_dmat, data->map,
		    mtod(data->m, void *), MJUMPAGESIZE, wpi_dma_map_addr,
		    &paddr, BUS_DMA_NOWAIT);
		if (error != 0 && error != EFBIG) {
			device_printf(sc->sc_dev,
			    "%s: can't map mbuf (error %d)\n", __func__,
			    error);
			goto fail;
		}

		/* Set physical address of RX buffer. */
		ring->desc[i] = htole32(paddr);
	}

	bus_dmamap_sync(ring->desc_dma.tag, ring->desc_dma.map,
	    BUS_DMASYNC_PREWRITE);

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);

	return 0;

fail:	wpi_free_rx_ring(sc);

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END_ERR, __func__);

	return error;
}

static void
wpi_update_rx_ring(struct wpi_softc *sc)
{
	WPI_WRITE(sc, WPI_FH_RX_WPTR, sc->rxq.cur & ~7);
}

static void
wpi_update_rx_ring_ps(struct wpi_softc *sc)
{
	struct wpi_rx_ring *ring = &sc->rxq;

	if (ring->update != 0) {
		/* Wait for INT_WAKEUP event. */
		return;
	}

	WPI_TXQ_LOCK(sc);
	WPI_SETBITS(sc, WPI_GP_CNTRL, WPI_GP_CNTRL_MAC_ACCESS_REQ);
	if (WPI_READ(sc, WPI_GP_CNTRL) & WPI_GP_CNTRL_SLEEP) {
		DPRINTF(sc, WPI_DEBUG_PWRSAVE, "%s: wakeup request\n",
		    __func__);
		ring->update = 1;
	} else {
		wpi_update_rx_ring(sc);
		WPI_CLRBITS(sc, WPI_GP_CNTRL, WPI_GP_CNTRL_MAC_ACCESS_REQ);
	}
	WPI_TXQ_UNLOCK(sc);
}

static void
wpi_reset_rx_ring(struct wpi_softc *sc)
{
	struct wpi_rx_ring *ring = &sc->rxq;
	int ntries;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	if (wpi_nic_lock(sc) == 0) {
		WPI_WRITE(sc, WPI_FH_RX_CONFIG, 0);
		for (ntries = 0; ntries < 1000; ntries++) {
			if (WPI_READ(sc, WPI_FH_RX_STATUS) &
			    WPI_FH_RX_STATUS_IDLE)
				break;
			DELAY(10);
		}
		wpi_nic_unlock(sc);
	}

	ring->cur = 0;
	ring->update = 0;
}

static void
wpi_free_rx_ring(struct wpi_softc *sc)
{
	struct wpi_rx_ring *ring = &sc->rxq;
	int i;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	wpi_dma_contig_free(&ring->desc_dma);

	for (i = 0; i < WPI_RX_RING_COUNT; i++) {
		struct wpi_rx_data *data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(ring->data_dmat, data->map,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(ring->data_dmat, data->map);
			m_freem(data->m);
			data->m = NULL;
		}
		if (data->map != NULL)
			bus_dmamap_destroy(ring->data_dmat, data->map);
	}
	if (ring->data_dmat != NULL) {
		bus_dma_tag_destroy(ring->data_dmat);
		ring->data_dmat = NULL;
	}
}

static int
wpi_alloc_tx_ring(struct wpi_softc *sc, struct wpi_tx_ring *ring, uint8_t qid)
{
	bus_addr_t paddr;
	bus_size_t size;
	int i, error;

	ring->qid = qid;
	ring->queued = 0;
	ring->cur = 0;
	ring->pending = 0;
	ring->update = 0;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	/* Allocate TX descriptors (16KB aligned.) */
	size = WPI_TX_RING_COUNT * sizeof (struct wpi_tx_desc);
	error = wpi_dma_contig_alloc(sc, &ring->desc_dma, (void **)&ring->desc,
	    size, WPI_RING_DMA_ALIGN);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not allocate TX ring DMA memory, error %d\n",
		    __func__, error);
		goto fail;
	}

	/* Update shared area with ring physical address. */
	sc->shared->txbase[qid] = htole32(ring->desc_dma.paddr);
	bus_dmamap_sync(sc->shared_dma.tag, sc->shared_dma.map,
	    BUS_DMASYNC_PREWRITE);

	size = WPI_TX_RING_COUNT * sizeof (struct wpi_tx_cmd);
	error = wpi_dma_contig_alloc(sc, &ring->cmd_dma, (void **)&ring->cmd,
	    size, 4);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not allocate TX cmd DMA memory, error %d\n",
		    __func__, error);
		goto fail;
	}

	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 1, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL, MCLBYTES,
	    WPI_MAX_SCATTER - 1, MCLBYTES, 0, NULL, NULL, &ring->data_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not create TX buf DMA tag, error %d\n",
		    __func__, error);
		goto fail;
	}

	paddr = ring->cmd_dma.paddr;
	for (i = 0; i < WPI_TX_RING_COUNT; i++) {
		struct wpi_tx_data *data = &ring->data[i];

		data->cmd_paddr = paddr;
		paddr += sizeof (struct wpi_tx_cmd);

		error = bus_dmamap_create(ring->data_dmat, 0, &data->map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not create TX buf DMA map, error %d\n",
			    __func__, error);
			goto fail;
		}
	}

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);

	return 0;

fail:	wpi_free_tx_ring(sc, ring);
	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END_ERR, __func__);
	return error;
}

static void
wpi_update_tx_ring(struct wpi_softc *sc, struct wpi_tx_ring *ring)
{
	WPI_WRITE(sc, WPI_HBUS_TARG_WRPTR, ring->qid << 8 | ring->cur);
}

static void
wpi_update_tx_ring_ps(struct wpi_softc *sc, struct wpi_tx_ring *ring)
{

	if (ring->update != 0) {
		/* Wait for INT_WAKEUP event. */
		return;
	}

	WPI_SETBITS(sc, WPI_GP_CNTRL, WPI_GP_CNTRL_MAC_ACCESS_REQ);
	if (WPI_READ(sc, WPI_GP_CNTRL) & WPI_GP_CNTRL_SLEEP) {
		DPRINTF(sc, WPI_DEBUG_PWRSAVE, "%s (%d): requesting wakeup\n",
		    __func__, ring->qid);
		ring->update = 1;
	} else {
		wpi_update_tx_ring(sc, ring);
		WPI_CLRBITS(sc, WPI_GP_CNTRL, WPI_GP_CNTRL_MAC_ACCESS_REQ);
	}
}

static void
wpi_reset_tx_ring(struct wpi_softc *sc, struct wpi_tx_ring *ring)
{
	int i;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	for (i = 0; i < WPI_TX_RING_COUNT; i++) {
		struct wpi_tx_data *data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(ring->data_dmat, data->map,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(ring->data_dmat, data->map);
			m_freem(data->m);
			data->m = NULL;
		}
		if (data->ni != NULL) {
			ieee80211_free_node(data->ni);
			data->ni = NULL;
		}
	}
	/* Clear TX descriptors. */
	memset(ring->desc, 0, ring->desc_dma.size);
	bus_dmamap_sync(ring->desc_dma.tag, ring->desc_dma.map,
	    BUS_DMASYNC_PREWRITE);
	ring->queued = 0;
	ring->cur = 0;
	ring->pending = 0;
	ring->update = 0;
}

static void
wpi_free_tx_ring(struct wpi_softc *sc, struct wpi_tx_ring *ring)
{
	int i;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	wpi_dma_contig_free(&ring->desc_dma);
	wpi_dma_contig_free(&ring->cmd_dma);

	for (i = 0; i < WPI_TX_RING_COUNT; i++) {
		struct wpi_tx_data *data = &ring->data[i];

		if (data->m != NULL) {
			bus_dmamap_sync(ring->data_dmat, data->map,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(ring->data_dmat, data->map);
			m_freem(data->m);
		}
		if (data->map != NULL)
			bus_dmamap_destroy(ring->data_dmat, data->map);
	}
	if (ring->data_dmat != NULL) {
		bus_dma_tag_destroy(ring->data_dmat);
		ring->data_dmat = NULL;
	}
}

/*
 * Extract various information from EEPROM.
 */
static int
wpi_read_eeprom(struct wpi_softc *sc, uint8_t macaddr[IEEE80211_ADDR_LEN])
{
#define WPI_CHK(res) do {		\
	if ((error = res) != 0)		\
		goto fail;		\
} while (0)
	uint8_t i;
	int error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	/* Adapter has to be powered on for EEPROM access to work. */
	if ((error = wpi_apm_init(sc)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not power ON adapter, error %d\n", __func__,
		    error);
		return error;
	}

	if ((WPI_READ(sc, WPI_EEPROM_GP) & 0x6) == 0) {
		device_printf(sc->sc_dev, "bad EEPROM signature\n");
		error = EIO;
		goto fail;
	}
	/* Clear HW ownership of EEPROM. */
	WPI_CLRBITS(sc, WPI_EEPROM_GP, WPI_EEPROM_GP_IF_OWNER);

	/* Read the hardware capabilities, revision and SKU type. */
	WPI_CHK(wpi_read_prom_data(sc, WPI_EEPROM_SKU_CAP, &sc->cap,
	    sizeof(sc->cap)));
	WPI_CHK(wpi_read_prom_data(sc, WPI_EEPROM_REVISION, &sc->rev,
	    sizeof(sc->rev)));
	WPI_CHK(wpi_read_prom_data(sc, WPI_EEPROM_TYPE, &sc->type,
	    sizeof(sc->type)));

	sc->rev = le16toh(sc->rev);
	DPRINTF(sc, WPI_DEBUG_EEPROM, "cap=%x rev=%x type=%x\n", sc->cap,
	    sc->rev, sc->type);

	/* Read the regulatory domain (4 ASCII characters.) */
	WPI_CHK(wpi_read_prom_data(sc, WPI_EEPROM_DOMAIN, sc->domain,
	    sizeof(sc->domain)));

	/* Read MAC address. */
	WPI_CHK(wpi_read_prom_data(sc, WPI_EEPROM_MAC, macaddr,
	    IEEE80211_ADDR_LEN));

	/* Read the list of authorized channels. */
	for (i = 0; i < WPI_CHAN_BANDS_COUNT; i++)
		WPI_CHK(wpi_read_eeprom_channels(sc, i));

	/* Read the list of TX power groups. */
	for (i = 0; i < WPI_POWER_GROUPS_COUNT; i++)
		WPI_CHK(wpi_read_eeprom_group(sc, i));

fail:	wpi_apm_stop(sc);	/* Power OFF adapter. */

	DPRINTF(sc, WPI_DEBUG_TRACE, error ? TRACE_STR_END_ERR : TRACE_STR_END,
	    __func__);

	return error;
#undef WPI_CHK
}

/*
 * Translate EEPROM flags to net80211.
 */
static uint32_t
wpi_eeprom_channel_flags(struct wpi_eeprom_chan *channel)
{
	uint32_t nflags;

	nflags = 0;
	if ((channel->flags & WPI_EEPROM_CHAN_ACTIVE) == 0)
		nflags |= IEEE80211_CHAN_PASSIVE;
	if ((channel->flags & WPI_EEPROM_CHAN_IBSS) == 0)
		nflags |= IEEE80211_CHAN_NOADHOC;
	if (channel->flags & WPI_EEPROM_CHAN_RADAR) {
		nflags |= IEEE80211_CHAN_DFS;
		/* XXX apparently IBSS may still be marked */
		nflags |= IEEE80211_CHAN_NOADHOC;
	}

	/* XXX HOSTAP uses WPI_MODE_IBSS */
	if (nflags & IEEE80211_CHAN_NOADHOC)
		nflags |= IEEE80211_CHAN_NOHOSTAP;

	return nflags;
}

static void
wpi_read_eeprom_band(struct wpi_softc *sc, uint8_t n, int maxchans,
    int *nchans, struct ieee80211_channel chans[])
{
	struct wpi_eeprom_chan *channels = sc->eeprom_channels[n];
	const struct wpi_chan_band *band = &wpi_bands[n];
	uint32_t nflags;
	uint8_t bands[IEEE80211_MODE_BYTES];
	uint8_t chan, i;
	int error;

	memset(bands, 0, sizeof(bands));

	if (n == 0) {
		setbit(bands, IEEE80211_MODE_11B);
		setbit(bands, IEEE80211_MODE_11G);
	} else
		setbit(bands, IEEE80211_MODE_11A);

	for (i = 0; i < band->nchan; i++) {
		if (!(channels[i].flags & WPI_EEPROM_CHAN_VALID)) {
			DPRINTF(sc, WPI_DEBUG_EEPROM,
			    "Channel Not Valid: %d, band %d\n",
			     band->chan[i],n);
			continue;
		}

		chan = band->chan[i];
		nflags = wpi_eeprom_channel_flags(&channels[i]);
		error = ieee80211_add_channel(chans, maxchans, nchans,
		    chan, 0, channels[i].maxpwr, nflags, bands);
		if (error != 0)
			break;

		/* Save maximum allowed TX power for this channel. */
		sc->maxpwr[chan] = channels[i].maxpwr;

		DPRINTF(sc, WPI_DEBUG_EEPROM,
		    "adding chan %d flags=0x%x maxpwr=%d, offset %d\n",
		    chan, channels[i].flags, sc->maxpwr[chan], *nchans);
	}
}

/**
 * Read the eeprom to find out what channels are valid for the given
 * band and update net80211 with what we find.
 */
static int
wpi_read_eeprom_channels(struct wpi_softc *sc, uint8_t n)
{
	struct ieee80211com *ic = &sc->sc_ic;
	const struct wpi_chan_band *band = &wpi_bands[n];
	int error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	error = wpi_read_prom_data(sc, band->addr, &sc->eeprom_channels[n],
	    band->nchan * sizeof (struct wpi_eeprom_chan));
	if (error != 0) {
		DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END_ERR, __func__);
		return error;
	}

	wpi_read_eeprom_band(sc, n, IEEE80211_CHAN_MAX, &ic->ic_nchans,
	    ic->ic_channels);

	ieee80211_sort_channels(ic->ic_channels, ic->ic_nchans);

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);

	return 0;
}

static struct wpi_eeprom_chan *
wpi_find_eeprom_channel(struct wpi_softc *sc, struct ieee80211_channel *c)
{
	int i, j;

	for (j = 0; j < WPI_CHAN_BANDS_COUNT; j++)
		for (i = 0; i < wpi_bands[j].nchan; i++)
			if (wpi_bands[j].chan[i] == c->ic_ieee &&
			    ((j == 0) ^ IEEE80211_IS_CHAN_A(c)) == 1)
				return &sc->eeprom_channels[j][i];

	return NULL;
}

static void
wpi_getradiocaps(struct ieee80211com *ic,
    int maxchans, int *nchans, struct ieee80211_channel chans[])
{
	struct wpi_softc *sc = ic->ic_softc;
	int i;

	/* Parse the list of authorized channels. */
	for (i = 0; i < WPI_CHAN_BANDS_COUNT && *nchans < maxchans; i++)
		wpi_read_eeprom_band(sc, i, maxchans, nchans, chans);
}

/*
 * Enforce flags read from EEPROM.
 */
static int
wpi_setregdomain(struct ieee80211com *ic, struct ieee80211_regdomain *rd,
    int nchan, struct ieee80211_channel chans[])
{
	struct wpi_softc *sc = ic->ic_softc;
	int i;

	for (i = 0; i < nchan; i++) {
		struct ieee80211_channel *c = &chans[i];
		struct wpi_eeprom_chan *channel;

		channel = wpi_find_eeprom_channel(sc, c);
		if (channel == NULL) {
			ic_printf(ic, "%s: invalid channel %u freq %u/0x%x\n",
			    __func__, c->ic_ieee, c->ic_freq, c->ic_flags);
			return EINVAL;
		}
		c->ic_flags |= wpi_eeprom_channel_flags(channel);
	}

	return 0;
}

static int
wpi_read_eeprom_group(struct wpi_softc *sc, uint8_t n)
{
	struct wpi_power_group *group = &sc->groups[n];
	struct wpi_eeprom_group rgroup;
	int i, error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	if ((error = wpi_read_prom_data(sc, WPI_EEPROM_POWER_GRP + n * 32,
	    &rgroup, sizeof rgroup)) != 0) {
		DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END_ERR, __func__);
		return error;
	}

	/* Save TX power group information. */
	group->chan   = rgroup.chan;
	group->maxpwr = rgroup.maxpwr;
	/* Retrieve temperature at which the samples were taken. */
	group->temp   = (int16_t)le16toh(rgroup.temp);

	DPRINTF(sc, WPI_DEBUG_EEPROM,
	    "power group %d: chan=%d maxpwr=%d temp=%d\n", n, group->chan,
	    group->maxpwr, group->temp);

	for (i = 0; i < WPI_SAMPLES_COUNT; i++) {
		group->samples[i].index = rgroup.samples[i].index;
		group->samples[i].power = rgroup.samples[i].power;

		DPRINTF(sc, WPI_DEBUG_EEPROM,
		    "\tsample %d: index=%d power=%d\n", i,
		    group->samples[i].index, group->samples[i].power);
	}

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);

	return 0;
}

static __inline uint8_t
wpi_add_node_entry_adhoc(struct wpi_softc *sc)
{
	uint8_t newid = WPI_ID_IBSS_MIN;

	for (; newid <= WPI_ID_IBSS_MAX; newid++) {
		if ((sc->nodesmsk & (1 << newid)) == 0) {
			sc->nodesmsk |= 1 << newid;
			return newid;
		}
	}

	return WPI_ID_UNDEFINED;
}

static __inline uint8_t
wpi_add_node_entry_sta(struct wpi_softc *sc)
{
	sc->nodesmsk |= 1 << WPI_ID_BSS;

	return WPI_ID_BSS;
}

static __inline int
wpi_check_node_entry(struct wpi_softc *sc, uint8_t id)
{
	if (id == WPI_ID_UNDEFINED)
		return 0;

	return (sc->nodesmsk >> id) & 1;
}

static __inline void
wpi_clear_node_table(struct wpi_softc *sc)
{
	sc->nodesmsk = 0;
}

static __inline void
wpi_del_node_entry(struct wpi_softc *sc, uint8_t id)
{
	sc->nodesmsk &= ~(1 << id);
}

static struct ieee80211_node *
wpi_node_alloc(struct ieee80211vap *vap, const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct wpi_node *wn;

	wn = malloc(sizeof (struct wpi_node), M_80211_NODE,
	    M_NOWAIT | M_ZERO);

	if (wn == NULL)
		return NULL;

	wn->id = WPI_ID_UNDEFINED;

	return &wn->ni;
}

static void
wpi_node_free(struct ieee80211_node *ni)
{
	struct wpi_softc *sc = ni->ni_ic->ic_softc;
	struct wpi_node *wn = WPI_NODE(ni);

	if (wn->id != WPI_ID_UNDEFINED) {
		WPI_NT_LOCK(sc);
		if (wpi_check_node_entry(sc, wn->id)) {
			wpi_del_node_entry(sc, wn->id);
			wpi_del_node(sc, ni);
		}
		WPI_NT_UNLOCK(sc);
	}

	sc->sc_node_free(ni);
}

static __inline int
wpi_check_bss_filter(struct wpi_softc *sc)
{
	return (sc->rxon.filter & htole32(WPI_FILTER_BSS)) != 0;
}

static void
wpi_ibss_recv_mgmt(struct ieee80211_node *ni, struct mbuf *m, int subtype,
    const struct ieee80211_rx_stats *rxs,
    int rssi, int nf)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct wpi_softc *sc = vap->iv_ic->ic_softc;
	struct wpi_vap *wvp = WPI_VAP(vap);
	uint64_t ni_tstamp, rx_tstamp;

	wvp->wv_recv_mgmt(ni, m, subtype, rxs, rssi, nf);

	if (vap->iv_state == IEEE80211_S_RUN &&
	    (subtype == IEEE80211_FC0_SUBTYPE_BEACON ||
	    subtype == IEEE80211_FC0_SUBTYPE_PROBE_RESP)) {
		ni_tstamp = le64toh(ni->ni_tstamp.tsf);
		rx_tstamp = le64toh(sc->rx_tstamp);

		if (ni_tstamp >= rx_tstamp) {
			DPRINTF(sc, WPI_DEBUG_STATE,
			    "ibss merge, tsf %ju tstamp %ju\n",
			    (uintmax_t)rx_tstamp, (uintmax_t)ni_tstamp);
			(void) ieee80211_ibss_merge(ni);
		}
	}
}

static void
wpi_restore_node(void *arg, struct ieee80211_node *ni)
{
	struct wpi_softc *sc = arg;
	struct wpi_node *wn = WPI_NODE(ni);
	int error;

	WPI_NT_LOCK(sc);
	if (wn->id != WPI_ID_UNDEFINED) {
		wn->id = WPI_ID_UNDEFINED;
		if ((error = wpi_add_ibss_node(sc, ni)) != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not add IBSS node, error %d\n",
			    __func__, error);
		}
	}
	WPI_NT_UNLOCK(sc);
}

static void
wpi_restore_node_table(struct wpi_softc *sc, struct wpi_vap *wvp)
{
	struct ieee80211com *ic = &sc->sc_ic;

	/* Set group keys once. */
	WPI_NT_LOCK(sc);
	wvp->wv_gtk = 0;
	WPI_NT_UNLOCK(sc);

	ieee80211_iterate_nodes(&ic->ic_sta, wpi_restore_node, sc);
	ieee80211_crypto_reload_keys(ic);
}

/**
 * Called by net80211 when ever there is a change to 80211 state machine
 */
static int
wpi_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct wpi_vap *wvp = WPI_VAP(vap);
	struct ieee80211com *ic = vap->iv_ic;
	struct wpi_softc *sc = ic->ic_softc;
	int error = 0;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	WPI_TXQ_LOCK(sc);
	if (nstate > IEEE80211_S_INIT && sc->sc_running == 0) {
		DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END_ERR, __func__);
		WPI_TXQ_UNLOCK(sc);

		return ENXIO;
	}
	WPI_TXQ_UNLOCK(sc);

	DPRINTF(sc, WPI_DEBUG_STATE, "%s: %s -> %s\n", __func__,
		ieee80211_state_name[vap->iv_state],
		ieee80211_state_name[nstate]);

	if (vap->iv_state == IEEE80211_S_RUN && nstate < IEEE80211_S_RUN) {
		if ((error = wpi_set_pslevel(sc, 0, 0, 1)) != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not set power saving level\n",
			    __func__);
			return error;
		}

		wpi_set_led(sc, WPI_LED_LINK, 1, 0);
	}

	switch (nstate) {
	case IEEE80211_S_SCAN:
		WPI_RXON_LOCK(sc);
		if (wpi_check_bss_filter(sc) != 0) {
			sc->rxon.filter &= ~htole32(WPI_FILTER_BSS);
			if ((error = wpi_send_rxon(sc, 0, 1)) != 0) {
				device_printf(sc->sc_dev,
				    "%s: could not send RXON\n", __func__);
			}
		}
		WPI_RXON_UNLOCK(sc);
		break;

	case IEEE80211_S_ASSOC:
		if (vap->iv_state != IEEE80211_S_RUN)
			break;
		/* FALLTHROUGH */
	case IEEE80211_S_AUTH:
		/*
		 * NB: do not optimize AUTH -> AUTH state transmission -
		 * this will break powersave with non-QoS AP!
		 */

		/*
		 * The node must be registered in the firmware before auth.
		 * Also the associd must be cleared on RUN -> ASSOC
		 * transitions.
		 */
		if ((error = wpi_auth(sc, vap)) != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not move to AUTH state, error %d\n",
			    __func__, error);
		}
		break;

	case IEEE80211_S_RUN:
		/*
		 * RUN -> RUN transition:
		 * STA mode: Just restart the timers.
		 * IBSS mode: Process IBSS merge.
		 */
		if (vap->iv_state == IEEE80211_S_RUN) {
			if (vap->iv_opmode != IEEE80211_M_IBSS) {
				WPI_RXON_LOCK(sc);
				wpi_calib_timeout(sc);
				WPI_RXON_UNLOCK(sc);
				break;
			} else {
				/*
				 * Drop the BSS_FILTER bit
				 * (there is no another way to change bssid).
				 */
				WPI_RXON_LOCK(sc);
				sc->rxon.filter &= ~htole32(WPI_FILTER_BSS);
				if ((error = wpi_send_rxon(sc, 0, 1)) != 0) {
					device_printf(sc->sc_dev,
					    "%s: could not send RXON\n",
					    __func__);
				}
				WPI_RXON_UNLOCK(sc);

				/* Restore all what was lost. */
				wpi_restore_node_table(sc, wvp);

				/* XXX set conditionally? */
				wpi_updateedca(ic);
			}
		}

		/*
		 * !RUN -> RUN requires setting the association id
		 * which is done with a firmware cmd.  We also defer
		 * starting the timers until that work is done.
		 */
		if ((error = wpi_run(sc, vap)) != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not move to RUN state\n", __func__);
		}
		break;

	default:
		break;
	}
	if (error != 0) {
		DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END_ERR, __func__);
		return error;
	}

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);

	return wvp->wv_newstate(vap, nstate, arg);
}

static void
wpi_calib_timeout(void *arg)
{
	struct wpi_softc *sc = arg;

	if (wpi_check_bss_filter(sc) == 0)
		return;

	wpi_power_calibration(sc);

	callout_reset(&sc->calib_to, 60*hz, wpi_calib_timeout, sc);
}

static __inline uint8_t
rate2plcp(const uint8_t rate)
{
	switch (rate) {
	case 12:	return 0xd;
	case 18:	return 0xf;
	case 24:	return 0x5;
	case 36:	return 0x7;
	case 48:	return 0x9;
	case 72:	return 0xb;
	case 96:	return 0x1;
	case 108:	return 0x3;
	case 2:		return 10;
	case 4:		return 20;
	case 11:	return 55;
	case 22:	return 110;
	default:	return 0;
	}
}

static __inline uint8_t
plcp2rate(const uint8_t plcp)
{
	switch (plcp) {
	case 0xd:	return 12;
	case 0xf:	return 18;
	case 0x5:	return 24;
	case 0x7:	return 36;
	case 0x9:	return 48;
	case 0xb:	return 72;
	case 0x1:	return 96;
	case 0x3:	return 108;
	case 10:	return 2;
	case 20:	return 4;
	case 55:	return 11;
	case 110:	return 22;
	default:	return 0;
	}
}

/* Quickly determine if a given rate is CCK or OFDM. */
#define WPI_RATE_IS_OFDM(rate)	((rate) >= 12 && (rate) != 22)

static void
wpi_rx_done(struct wpi_softc *sc, struct wpi_rx_desc *desc,
    struct wpi_rx_data *data)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct wpi_rx_ring *ring = &sc->rxq;
	struct wpi_rx_stat *stat;
	struct wpi_rx_head *head;
	struct wpi_rx_tail *tail;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	struct mbuf *m, *m1;
	bus_addr_t paddr;
	uint32_t flags;
	uint16_t len;
	int error;

	stat = (struct wpi_rx_stat *)(desc + 1);

	if (__predict_false(stat->len > WPI_STAT_MAXLEN)) {
		device_printf(sc->sc_dev, "invalid RX statistic header\n");
		goto fail1;
	}

	bus_dmamap_sync(ring->data_dmat, data->map, BUS_DMASYNC_POSTREAD);
	head = (struct wpi_rx_head *)((caddr_t)(stat + 1) + stat->len);
	len = le16toh(head->len);
	tail = (struct wpi_rx_tail *)((caddr_t)(head + 1) + len);
	flags = le32toh(tail->flags);

	DPRINTF(sc, WPI_DEBUG_RECV, "%s: idx %d len %d stat len %u rssi %d"
	    " rate %x chan %d tstamp %ju\n", __func__, ring->cur,
	    le32toh(desc->len), len, (int8_t)stat->rssi,
	    head->plcp, head->chan, (uintmax_t)le64toh(tail->tstamp));

	/* Discard frames with a bad FCS early. */
	if ((flags & WPI_RX_NOERROR) != WPI_RX_NOERROR) {
		DPRINTF(sc, WPI_DEBUG_RECV, "%s: RX flags error %x\n",
		    __func__, flags);
		goto fail1;
	}
	/* Discard frames that are too short. */
	if (len < sizeof (struct ieee80211_frame_ack)) {
		DPRINTF(sc, WPI_DEBUG_RECV, "%s: frame too short: %d\n",
		    __func__, len);
		goto fail1;
	}

	m1 = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MJUMPAGESIZE);
	if (__predict_false(m1 == NULL)) {
		DPRINTF(sc, WPI_DEBUG_ANY, "%s: no mbuf to restock ring\n",
		    __func__);
		goto fail1;
	}
	bus_dmamap_unload(ring->data_dmat, data->map);

	error = bus_dmamap_load(ring->data_dmat, data->map, mtod(m1, void *),
	    MJUMPAGESIZE, wpi_dma_map_addr, &paddr, BUS_DMA_NOWAIT);
	if (__predict_false(error != 0 && error != EFBIG)) {
		device_printf(sc->sc_dev,
		    "%s: bus_dmamap_load failed, error %d\n", __func__, error);
		m_freem(m1);

		/* Try to reload the old mbuf. */
		error = bus_dmamap_load(ring->data_dmat, data->map,
		    mtod(data->m, void *), MJUMPAGESIZE, wpi_dma_map_addr,
		    &paddr, BUS_DMA_NOWAIT);
		if (error != 0 && error != EFBIG) {
			panic("%s: could not load old RX mbuf", __func__);
		}
		/* Physical address may have changed. */
		ring->desc[ring->cur] = htole32(paddr);
		bus_dmamap_sync(ring->data_dmat, ring->desc_dma.map,
		    BUS_DMASYNC_PREWRITE);
		goto fail1;
	}

	m = data->m;
	data->m = m1;
	/* Update RX descriptor. */
	ring->desc[ring->cur] = htole32(paddr);
	bus_dmamap_sync(ring->desc_dma.tag, ring->desc_dma.map,
	    BUS_DMASYNC_PREWRITE);

	/* Finalize mbuf. */
	m->m_data = (caddr_t)(head + 1);
	m->m_pkthdr.len = m->m_len = len;

	/* Grab a reference to the source node. */
	wh = mtod(m, struct ieee80211_frame *);

	if ((wh->i_fc[1] & IEEE80211_FC1_PROTECTED) &&
	    (flags & WPI_RX_CIPHER_MASK) == WPI_RX_CIPHER_CCMP) {
		/* Check whether decryption was successful or not. */
		if ((flags & WPI_RX_DECRYPT_MASK) != WPI_RX_DECRYPT_OK) {
			DPRINTF(sc, WPI_DEBUG_RECV,
			    "CCMP decryption failed 0x%x\n", flags);
			goto fail2;
		}
		m->m_flags |= M_WEP;
	}

	if (len >= sizeof(struct ieee80211_frame_min))
		ni = ieee80211_find_rxnode(ic, (struct ieee80211_frame_min *)wh);
	else
		ni = NULL;

	sc->rx_tstamp = tail->tstamp;

	if (ieee80211_radiotap_active(ic)) {
		struct wpi_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = 0;
		if (head->flags & htole16(WPI_STAT_FLAG_SHPREAMBLE))
			tap->wr_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
		tap->wr_dbm_antsignal = (int8_t)(stat->rssi + WPI_RSSI_OFFSET);
		tap->wr_dbm_antnoise = WPI_RSSI_OFFSET;
		tap->wr_tsft = tail->tstamp;
		tap->wr_antenna = (le16toh(head->flags) >> 4) & 0xf;
		tap->wr_rate = plcp2rate(head->plcp);
	}

	WPI_UNLOCK(sc);

	/* Send the frame to the 802.11 layer. */
	if (ni != NULL) {
		(void)ieee80211_input(ni, m, stat->rssi, WPI_RSSI_OFFSET);
		/* Node is no longer needed. */
		ieee80211_free_node(ni);
	} else
		(void)ieee80211_input_all(ic, m, stat->rssi, WPI_RSSI_OFFSET);

	WPI_LOCK(sc);

	return;

fail2:	m_freem(m);

fail1:	counter_u64_add(ic->ic_ierrors, 1);
}

static void
wpi_rx_statistics(struct wpi_softc *sc, struct wpi_rx_desc *desc,
    struct wpi_rx_data *data)
{
	/* Ignore */
}

static void
wpi_tx_done(struct wpi_softc *sc, struct wpi_rx_desc *desc)
{
	struct ieee80211_ratectl_tx_status *txs = &sc->sc_txs;
	struct wpi_tx_ring *ring = &sc->txq[desc->qid & 0x3];
	struct wpi_tx_data *data = &ring->data[desc->idx];
	struct wpi_tx_stat *stat = (struct wpi_tx_stat *)(desc + 1);
	struct mbuf *m;
	struct ieee80211_node *ni;
	uint32_t status = le32toh(stat->status);

	KASSERT(data->ni != NULL, ("no node"));
	KASSERT(data->m != NULL, ("no mbuf"));

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	DPRINTF(sc, WPI_DEBUG_XMIT, "%s: "
	    "qid %d idx %d retries %d btkillcnt %d rate %x duration %d "
	    "status %x\n", __func__, desc->qid, desc->idx, stat->ackfailcnt,
	    stat->btkillcnt, stat->rate, le32toh(stat->duration), status);

	/* Unmap and free mbuf. */
	bus_dmamap_sync(ring->data_dmat, data->map, BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(ring->data_dmat, data->map);
	m = data->m, data->m = NULL;
	ni = data->ni, data->ni = NULL;

	/* Restore frame header. */
	KASSERT(M_LEADINGSPACE(m) >= data->hdrlen, ("no frame header!"));
	M_PREPEND(m, data->hdrlen, M_NOWAIT);
	KASSERT(m != NULL, ("%s: m is NULL\n", __func__));

	/*
	 * Update rate control statistics for the node.
	 */
	txs->pktlen = m->m_pkthdr.len;
	txs->short_retries = stat->rtsfailcnt;
	txs->long_retries = stat->ackfailcnt / WPI_NTRIES_DEFAULT;
	if (!(status & WPI_TX_STATUS_FAIL))
		txs->status = IEEE80211_RATECTL_TX_SUCCESS;
	else {
		switch (status & 0xff) {
		case WPI_TX_STATUS_FAIL_SHORT_LIMIT:
			txs->status = IEEE80211_RATECTL_TX_FAIL_SHORT;
			break;
		case WPI_TX_STATUS_FAIL_LONG_LIMIT:
			txs->status = IEEE80211_RATECTL_TX_FAIL_LONG;
			break;
		case WPI_TX_STATUS_FAIL_LIFE_EXPIRE:
			txs->status = IEEE80211_RATECTL_TX_FAIL_EXPIRED;
			break;
		default:
			txs->status = IEEE80211_RATECTL_TX_FAIL_UNSPECIFIED;
			break;
		}
	}

	ieee80211_ratectl_tx_complete(ni, txs);
	ieee80211_tx_complete(ni, m, (status & WPI_TX_STATUS_FAIL) != 0);

	WPI_TXQ_STATE_LOCK(sc);
	if (--ring->queued > 0)
		callout_reset(&sc->tx_timeout, 5*hz, wpi_tx_timeout, sc);
	else
		callout_stop(&sc->tx_timeout);
	WPI_TXQ_STATE_UNLOCK(sc);

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);
}

/*
 * Process a "command done" firmware notification.  This is where we wakeup
 * processes waiting for a synchronous command completion.
 */
static void
wpi_cmd_done(struct wpi_softc *sc, struct wpi_rx_desc *desc)
{
	struct wpi_tx_ring *ring = &sc->txq[WPI_CMD_QUEUE_NUM];
	struct wpi_tx_data *data;
	struct wpi_tx_cmd *cmd;

	DPRINTF(sc, WPI_DEBUG_CMD, "cmd notification qid %x idx %d flags %x "
				   "type %s len %d\n", desc->qid, desc->idx,
				   desc->flags, wpi_cmd_str(desc->type),
				   le32toh(desc->len));

	if ((desc->qid & WPI_RX_DESC_QID_MSK) != WPI_CMD_QUEUE_NUM)
		return;	/* Not a command ack. */

	KASSERT(ring->queued == 0, ("ring->queued must be 0"));

	data = &ring->data[desc->idx];
	cmd = &ring->cmd[desc->idx];

	/* If the command was mapped in an mbuf, free it. */
	if (data->m != NULL) {
		bus_dmamap_sync(ring->data_dmat, data->map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(ring->data_dmat, data->map);
		m_freem(data->m);
		data->m = NULL;
	}

	wakeup(cmd);

	if (desc->type == WPI_CMD_SET_POWER_MODE) {
		struct wpi_pmgt_cmd *pcmd = (struct wpi_pmgt_cmd *)cmd->data;

		bus_dmamap_sync(ring->data_dmat, ring->cmd_dma.map,
		    BUS_DMASYNC_POSTREAD);

		WPI_TXQ_LOCK(sc);
		if (le16toh(pcmd->flags) & WPI_PS_ALLOW_SLEEP) {
			sc->sc_update_rx_ring = wpi_update_rx_ring_ps;
			sc->sc_update_tx_ring = wpi_update_tx_ring_ps;
		} else {
			sc->sc_update_rx_ring = wpi_update_rx_ring;
			sc->sc_update_tx_ring = wpi_update_tx_ring;
		}
		WPI_TXQ_UNLOCK(sc);
	}
}

static void
wpi_notif_intr(struct wpi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	uint32_t hw;

	bus_dmamap_sync(sc->shared_dma.tag, sc->shared_dma.map,
	    BUS_DMASYNC_POSTREAD);

	hw = le32toh(sc->shared->next) & 0xfff;
	hw = (hw == 0) ? WPI_RX_RING_COUNT - 1 : hw - 1;

	while (sc->rxq.cur != hw) {
		sc->rxq.cur = (sc->rxq.cur + 1) % WPI_RX_RING_COUNT;

		struct wpi_rx_data *data = &sc->rxq.data[sc->rxq.cur];
		struct wpi_rx_desc *desc;

		bus_dmamap_sync(sc->rxq.data_dmat, data->map,
		    BUS_DMASYNC_POSTREAD);
		desc = mtod(data->m, struct wpi_rx_desc *);

		DPRINTF(sc, WPI_DEBUG_NOTIFY,
		    "%s: cur=%d; qid %x idx %d flags %x type %d(%s) len %d\n",
		    __func__, sc->rxq.cur, desc->qid, desc->idx, desc->flags,
		    desc->type, wpi_cmd_str(desc->type), le32toh(desc->len));

		if (!(desc->qid & WPI_UNSOLICITED_RX_NOTIF)) {
			/* Reply to a command. */
			wpi_cmd_done(sc, desc);
		}

		switch (desc->type) {
		case WPI_RX_DONE:
			/* An 802.11 frame has been received. */
			wpi_rx_done(sc, desc, data);

			if (__predict_false(sc->sc_running == 0)) {
				/* wpi_stop() was called. */
				return;
			}

			break;

		case WPI_TX_DONE:
			/* An 802.11 frame has been transmitted. */
			wpi_tx_done(sc, desc);
			break;

		case WPI_RX_STATISTICS:
		case WPI_BEACON_STATISTICS:
			wpi_rx_statistics(sc, desc, data);
			break;

		case WPI_BEACON_MISSED:
		{
			struct wpi_beacon_missed *miss =
			    (struct wpi_beacon_missed *)(desc + 1);
			uint32_t expected, misses, received, threshold;

			bus_dmamap_sync(sc->rxq.data_dmat, data->map,
			    BUS_DMASYNC_POSTREAD);

			misses = le32toh(miss->consecutive);
			expected = le32toh(miss->expected);
			received = le32toh(miss->received);
			threshold = MAX(2, vap->iv_bmissthreshold);

			DPRINTF(sc, WPI_DEBUG_BMISS,
			    "%s: beacons missed %u(%u) (received %u/%u)\n",
			    __func__, misses, le32toh(miss->total), received,
			    expected);

			if (misses >= threshold ||
			    (received == 0 && expected >= threshold)) {
				WPI_RXON_LOCK(sc);
				if (callout_pending(&sc->scan_timeout)) {
					wpi_cmd(sc, WPI_CMD_SCAN_ABORT, NULL,
					    0, 1);
				}
				WPI_RXON_UNLOCK(sc);
				if (vap->iv_state == IEEE80211_S_RUN &&
				    (ic->ic_flags & IEEE80211_F_SCAN) == 0)
					ieee80211_beacon_miss(ic);
			}

			break;
		}
#ifdef WPI_DEBUG
		case WPI_BEACON_SENT:
		{
			struct wpi_tx_stat *stat =
			    (struct wpi_tx_stat *)(desc + 1);
			uint64_t *tsf = (uint64_t *)(stat + 1);
			uint32_t *mode = (uint32_t *)(tsf + 1);

			bus_dmamap_sync(sc->rxq.data_dmat, data->map,
			    BUS_DMASYNC_POSTREAD);

			DPRINTF(sc, WPI_DEBUG_BEACON,
			    "beacon sent: rts %u, ack %u, btkill %u, rate %u, "
			    "duration %u, status %x, tsf %ju, mode %x\n",
			    stat->rtsfailcnt, stat->ackfailcnt,
			    stat->btkillcnt, stat->rate, le32toh(stat->duration),
			    le32toh(stat->status), le64toh(*tsf),
			    le32toh(*mode));

			break;
		}
#endif
		case WPI_UC_READY:
		{
			struct wpi_ucode_info *uc =
			    (struct wpi_ucode_info *)(desc + 1);

			/* The microcontroller is ready. */
			bus_dmamap_sync(sc->rxq.data_dmat, data->map,
			    BUS_DMASYNC_POSTREAD);
			DPRINTF(sc, WPI_DEBUG_RESET,
			    "microcode alive notification version=%d.%d "
			    "subtype=%x alive=%x\n", uc->major, uc->minor,
			    uc->subtype, le32toh(uc->valid));

			if (le32toh(uc->valid) != 1) {
				device_printf(sc->sc_dev,
				    "microcontroller initialization failed\n");
				wpi_stop_locked(sc);
				return;
			}
			/* Save the address of the error log in SRAM. */
			sc->errptr = le32toh(uc->errptr);
			break;
		}
		case WPI_STATE_CHANGED:
		{
			bus_dmamap_sync(sc->rxq.data_dmat, data->map,
			    BUS_DMASYNC_POSTREAD);

			uint32_t *status = (uint32_t *)(desc + 1);

			DPRINTF(sc, WPI_DEBUG_STATE, "state changed to %x\n",
			    le32toh(*status));

			if (le32toh(*status) & 1) {
				WPI_NT_LOCK(sc);
				wpi_clear_node_table(sc);
				WPI_NT_UNLOCK(sc);
				ieee80211_runtask(ic,
				    &sc->sc_radiooff_task);
				return;
			}
			break;
		}
#ifdef WPI_DEBUG
		case WPI_START_SCAN:
		{
			bus_dmamap_sync(sc->rxq.data_dmat, data->map,
			    BUS_DMASYNC_POSTREAD);

			struct wpi_start_scan *scan =
			    (struct wpi_start_scan *)(desc + 1);
			DPRINTF(sc, WPI_DEBUG_SCAN,
			    "%s: scanning channel %d status %x\n",
			    __func__, scan->chan, le32toh(scan->status));

			break;
		}
#endif
		case WPI_STOP_SCAN:
		{
			bus_dmamap_sync(sc->rxq.data_dmat, data->map,
			    BUS_DMASYNC_POSTREAD);

			struct wpi_stop_scan *scan =
			    (struct wpi_stop_scan *)(desc + 1);

			DPRINTF(sc, WPI_DEBUG_SCAN,
			    "scan finished nchan=%d status=%d chan=%d\n",
			    scan->nchan, scan->status, scan->chan);

			WPI_RXON_LOCK(sc);
			callout_stop(&sc->scan_timeout);
			WPI_RXON_UNLOCK(sc);
			if (scan->status == WPI_SCAN_ABORTED)
				ieee80211_cancel_scan(vap);
			else
				ieee80211_scan_next(vap);
			break;
		}
		}

		if (sc->rxq.cur % 8 == 0) {
			/* Tell the firmware what we have processed. */
			sc->sc_update_rx_ring(sc);
		}
	}
}

/*
 * Process an INT_WAKEUP interrupt raised when the microcontroller wakes up
 * from power-down sleep mode.
 */
static void
wpi_wakeup_intr(struct wpi_softc *sc)
{
	int qid;

	DPRINTF(sc, WPI_DEBUG_PWRSAVE,
	    "%s: ucode wakeup from power-down sleep\n", __func__);

	/* Wakeup RX and TX rings. */
	if (sc->rxq.update) {
		sc->rxq.update = 0;
		wpi_update_rx_ring(sc);
	}
	WPI_TXQ_LOCK(sc);
	for (qid = 0; qid < WPI_DRV_NTXQUEUES; qid++) {
		struct wpi_tx_ring *ring = &sc->txq[qid];

		if (ring->update) {
			ring->update = 0;
			wpi_update_tx_ring(sc, ring);
		}
	}
	WPI_CLRBITS(sc, WPI_GP_CNTRL, WPI_GP_CNTRL_MAC_ACCESS_REQ);
	WPI_TXQ_UNLOCK(sc);
}

/*
 * This function prints firmware registers
 */
#ifdef WPI_DEBUG
static void
wpi_debug_registers(struct wpi_softc *sc)
{
	size_t i;
	static const uint32_t csr_tbl[] = {
		WPI_HW_IF_CONFIG,
		WPI_INT,
		WPI_INT_MASK,
		WPI_FH_INT,
		WPI_GPIO_IN,
		WPI_RESET,
		WPI_GP_CNTRL,
		WPI_EEPROM,
		WPI_EEPROM_GP,
		WPI_GIO,
		WPI_UCODE_GP1,
		WPI_UCODE_GP2,
		WPI_GIO_CHICKEN,
		WPI_ANA_PLL,
		WPI_DBG_HPET_MEM,
	};
	static const uint32_t prph_tbl[] = {
		WPI_APMG_CLK_CTRL,
		WPI_APMG_PS,
		WPI_APMG_PCI_STT,
		WPI_APMG_RFKILL,
	};

	DPRINTF(sc, WPI_DEBUG_REGISTER,"%s","\n");

	for (i = 0; i < nitems(csr_tbl); i++) {
		DPRINTF(sc, WPI_DEBUG_REGISTER, "  %-18s: 0x%08x ",
		    wpi_get_csr_string(csr_tbl[i]), WPI_READ(sc, csr_tbl[i]));

		if ((i + 1) % 2 == 0)
			DPRINTF(sc, WPI_DEBUG_REGISTER, "\n");
	}
	DPRINTF(sc, WPI_DEBUG_REGISTER, "\n\n");

	if (wpi_nic_lock(sc) == 0) {
		for (i = 0; i < nitems(prph_tbl); i++) {
			DPRINTF(sc, WPI_DEBUG_REGISTER, "  %-18s: 0x%08x ",
			    wpi_get_prph_string(prph_tbl[i]),
			    wpi_prph_read(sc, prph_tbl[i]));

			if ((i + 1) % 2 == 0)
				DPRINTF(sc, WPI_DEBUG_REGISTER, "\n");
		}
		DPRINTF(sc, WPI_DEBUG_REGISTER, "\n");
		wpi_nic_unlock(sc);
	} else {
		DPRINTF(sc, WPI_DEBUG_REGISTER,
		    "Cannot access internal registers.\n");
	}
}
#endif

/*
 * Dump the error log of the firmware when a firmware panic occurs.  Although
 * we can't debug the firmware because it is neither open source nor free, it
 * can help us to identify certain classes of problems.
 */
static void
wpi_fatal_intr(struct wpi_softc *sc)
{
	struct wpi_fw_dump dump;
	uint32_t i, offset, count;

	/* Check that the error log address is valid. */
	if (sc->errptr < WPI_FW_DATA_BASE ||
	    sc->errptr + sizeof (dump) >
	    WPI_FW_DATA_BASE + WPI_FW_DATA_MAXSZ) {
		printf("%s: bad firmware error log address 0x%08x\n", __func__,
		    sc->errptr);
		return;
	}
	if (wpi_nic_lock(sc) != 0) {
		printf("%s: could not read firmware error log\n", __func__);
		return;
	}
	/* Read number of entries in the log. */
	count = wpi_mem_read(sc, sc->errptr);
	if (count == 0 || count * sizeof (dump) > WPI_FW_DATA_MAXSZ) {
		printf("%s: invalid count field (count = %u)\n", __func__,
		    count);
		wpi_nic_unlock(sc);
		return;
	}
	/* Skip "count" field. */
	offset = sc->errptr + sizeof (uint32_t);
	printf("firmware error log (count = %u):\n", count);
	for (i = 0; i < count; i++) {
		wpi_mem_read_region_4(sc, offset, (uint32_t *)&dump,
		    sizeof (dump) / sizeof (uint32_t));

		printf("  error type = \"%s\" (0x%08X)\n",
		    (dump.desc < nitems(wpi_fw_errmsg)) ?
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
	WPI_TXQ_LOCK(sc);
	for (i = 0; i < WPI_DRV_NTXQUEUES; i++) {
		struct wpi_tx_ring *ring = &sc->txq[i];
		printf("  tx ring %2d: qid=%-2d cur=%-3d queued=%-3d\n",
		    i, ring->qid, ring->cur, ring->queued);
	}
	WPI_TXQ_UNLOCK(sc);
	printf("  rx ring: cur=%d\n", sc->rxq.cur);
}

static void
wpi_intr(void *arg)
{
	struct wpi_softc *sc = arg;
	uint32_t r1, r2;

	WPI_LOCK(sc);

	/* Disable interrupts. */
	WPI_WRITE(sc, WPI_INT_MASK, 0);

	r1 = WPI_READ(sc, WPI_INT);

	if (__predict_false(r1 == 0xffffffff ||
			   (r1 & 0xfffffff0) == 0xa5a5a5a0))
		goto end;	/* Hardware gone! */

	r2 = WPI_READ(sc, WPI_FH_INT);

	DPRINTF(sc, WPI_DEBUG_INTR, "%s: reg1=0x%08x reg2=0x%08x\n", __func__,
	    r1, r2);

	if (r1 == 0 && r2 == 0)
		goto done;	/* Interrupt not for us. */

	/* Acknowledge interrupts. */
	WPI_WRITE(sc, WPI_INT, r1);
	WPI_WRITE(sc, WPI_FH_INT, r2);

	if (__predict_false(r1 & (WPI_INT_SW_ERR | WPI_INT_HW_ERR))) {
		struct ieee80211com *ic = &sc->sc_ic;

		device_printf(sc->sc_dev, "fatal firmware error\n");
#ifdef WPI_DEBUG
		wpi_debug_registers(sc);
#endif
		wpi_fatal_intr(sc);
		DPRINTF(sc, WPI_DEBUG_HW,
		    "(%s)\n", (r1 & WPI_INT_SW_ERR) ? "(Software Error)" :
		    "(Hardware Error)");
		ieee80211_restart_all(ic);
		goto end;
	}

	if ((r1 & (WPI_INT_FH_RX | WPI_INT_SW_RX)) ||
	    (r2 & WPI_FH_INT_RX))
		wpi_notif_intr(sc);

	if (r1 & WPI_INT_ALIVE)
		wakeup(sc);	/* Firmware is alive. */

	if (r1 & WPI_INT_WAKEUP)
		wpi_wakeup_intr(sc);

done:
	/* Re-enable interrupts. */
	if (__predict_true(sc->sc_running))
		WPI_WRITE(sc, WPI_INT_MASK, WPI_INT_MASK_DEF);

end:	WPI_UNLOCK(sc);
}

static void
wpi_free_txfrags(struct wpi_softc *sc, uint16_t ac)
{
	struct wpi_tx_ring *ring;
	struct wpi_tx_data *data;
	uint8_t cur;

	WPI_TXQ_LOCK(sc);
	ring = &sc->txq[ac];

	while (ring->pending != 0) {
		ring->pending--;
		cur = (ring->cur + ring->pending) % WPI_TX_RING_COUNT;
		data = &ring->data[cur];

		bus_dmamap_sync(ring->data_dmat, data->map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(ring->data_dmat, data->map);
		m_freem(data->m);
		data->m = NULL;

		ieee80211_node_decref(data->ni);
		data->ni = NULL;
	}

	WPI_TXQ_UNLOCK(sc);
}

static int
wpi_cmd2(struct wpi_softc *sc, struct wpi_buf *buf)
{
	struct ieee80211_frame *wh;
	struct wpi_tx_cmd *cmd;
	struct wpi_tx_data *data;
	struct wpi_tx_desc *desc;
	struct wpi_tx_ring *ring;
	struct mbuf *m1;
	bus_dma_segment_t *seg, segs[WPI_MAX_SCATTER];
	uint8_t cur, pad;
	uint16_t hdrlen;
	int error, i, nsegs, totlen, frag;

	WPI_TXQ_LOCK(sc);

	KASSERT(buf->size <= sizeof(buf->data), ("buffer overflow"));

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	if (__predict_false(sc->sc_running == 0)) {
		/* wpi_stop() was called */
		error = ENETDOWN;
		goto end;
	}

	wh = mtod(buf->m, struct ieee80211_frame *);
	hdrlen = ieee80211_anyhdrsize(wh);
	totlen = buf->m->m_pkthdr.len;
	frag = ((buf->m->m_flags & (M_FRAG | M_LASTFRAG)) == M_FRAG);

	if (__predict_false(totlen < sizeof(struct ieee80211_frame_min))) {
		error = EINVAL;
		goto end;
	}

	if (hdrlen & 3) {
		/* First segment length must be a multiple of 4. */
		pad = 4 - (hdrlen & 3);
	} else
		pad = 0;

	ring = &sc->txq[buf->ac];
	cur = (ring->cur + ring->pending) % WPI_TX_RING_COUNT;
	desc = &ring->desc[cur];
	data = &ring->data[cur];

	/* Prepare TX firmware command. */
	cmd = &ring->cmd[cur];
	cmd->code = buf->code;
	cmd->flags = 0;
	cmd->qid = ring->qid;
	cmd->idx = cur;

	memcpy(cmd->data, buf->data, buf->size);

	/* Save and trim IEEE802.11 header. */
	memcpy((uint8_t *)(cmd->data + buf->size), wh, hdrlen);
	m_adj(buf->m, hdrlen);

	error = bus_dmamap_load_mbuf_sg(ring->data_dmat, data->map, buf->m,
	    segs, &nsegs, BUS_DMA_NOWAIT);
	if (error != 0 && error != EFBIG) {
		device_printf(sc->sc_dev,
		    "%s: can't map mbuf (error %d)\n", __func__, error);
		goto end;
	}
	if (error != 0) {
		/* Too many DMA segments, linearize mbuf. */
		m1 = m_collapse(buf->m, M_NOWAIT, WPI_MAX_SCATTER - 1);
		if (m1 == NULL) {
			device_printf(sc->sc_dev,
			    "%s: could not defrag mbuf\n", __func__);
			error = ENOBUFS;
			goto end;
		}
		buf->m = m1;

		error = bus_dmamap_load_mbuf_sg(ring->data_dmat, data->map,
		    buf->m, segs, &nsegs, BUS_DMA_NOWAIT);
		if (__predict_false(error != 0)) {
			/* XXX fix this (applicable to the iwn(4) too) */
			/*
			 * NB: Do not return error;
			 * original mbuf does not exist anymore.
			 */
			device_printf(sc->sc_dev,
			    "%s: can't map mbuf (error %d)\n", __func__,
			    error);
			if (ring->qid < WPI_CMD_QUEUE_NUM) {
				if_inc_counter(buf->ni->ni_vap->iv_ifp,
				    IFCOUNTER_OERRORS, 1);
				if (!frag)
					ieee80211_free_node(buf->ni);
			}
			m_freem(buf->m);
			error = 0;
			goto end;
		}
	}

	KASSERT(nsegs < WPI_MAX_SCATTER,
	    ("too many DMA segments, nsegs (%d) should be less than %d",
	     nsegs, WPI_MAX_SCATTER));

	data->m = buf->m;
	data->ni = buf->ni;
	data->hdrlen = hdrlen;

	DPRINTF(sc, WPI_DEBUG_XMIT, "%s: qid %d idx %d len %d nsegs %d\n",
	    __func__, ring->qid, cur, totlen, nsegs);

	/* Fill TX descriptor. */
	desc->nsegs = WPI_PAD32(totlen + pad) << 4 | (1 + nsegs);
	/* First DMA segment is used by the TX command. */
	desc->segs[0].addr = htole32(data->cmd_paddr);
	desc->segs[0].len  = htole32(4 + buf->size + hdrlen + pad);
	/* Other DMA segments are for data payload. */
	seg = &segs[0];
	for (i = 1; i <= nsegs; i++) {
		desc->segs[i].addr = htole32(seg->ds_addr);
		desc->segs[i].len  = htole32(seg->ds_len);
		seg++;
	}

	bus_dmamap_sync(ring->data_dmat, data->map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(ring->data_dmat, ring->cmd_dma.map,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(ring->desc_dma.tag, ring->desc_dma.map,
	    BUS_DMASYNC_PREWRITE);

	ring->pending += 1;

	if (!frag) {
		if (ring->qid < WPI_CMD_QUEUE_NUM) {
			WPI_TXQ_STATE_LOCK(sc);
			ring->queued += ring->pending;
			callout_reset(&sc->tx_timeout, 5*hz, wpi_tx_timeout,
			    sc);
			WPI_TXQ_STATE_UNLOCK(sc);
		}

		/* Kick TX ring. */
		ring->cur = (ring->cur + ring->pending) % WPI_TX_RING_COUNT;
		ring->pending = 0;
		sc->sc_update_tx_ring(sc, ring);
	} else
		ieee80211_node_incref(data->ni);

end:	DPRINTF(sc, WPI_DEBUG_TRACE, error ? TRACE_STR_END_ERR : TRACE_STR_END,
	    __func__);

	WPI_TXQ_UNLOCK(sc);

	return (error);
}

/*
 * Construct the data packet for a transmit buffer.
 */
static int
wpi_tx_data(struct wpi_softc *sc, struct mbuf *m, struct ieee80211_node *ni)
{
	const struct ieee80211_txparam *tp = ni->ni_txparms;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct wpi_node *wn = WPI_NODE(ni);
	struct ieee80211_frame *wh;
	struct ieee80211_key *k = NULL;
	struct wpi_buf tx_data;
	struct wpi_cmd_data *tx = (struct wpi_cmd_data *)&tx_data.data;
	uint32_t flags;
	uint16_t ac, qos;
	uint8_t tid, type, rate;
	int swcrypt, ismcast, totlen;

	wh = mtod(m, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	ismcast = IEEE80211_IS_MULTICAST(wh->i_addr1);
	swcrypt = 1;

	/* Select EDCA Access Category and TX ring for this frame. */
	if (IEEE80211_QOS_HAS_SEQ(wh)) {
		qos = ((const struct ieee80211_qosframe *)wh)->i_qos[0];
		tid = qos & IEEE80211_QOS_TID;
	} else {
		qos = 0;
		tid = 0;
	}
	ac = M_WME_GETAC(m);

	/* Choose a TX rate index. */
	if (type == IEEE80211_FC0_TYPE_MGT ||
	    type == IEEE80211_FC0_TYPE_CTL ||
	    (m->m_flags & M_EAPOL) != 0)
		rate = tp->mgmtrate;
	else if (ismcast)
		rate = tp->mcastrate;
	else if (tp->ucastrate != IEEE80211_FIXED_RATE_NONE)
		rate = tp->ucastrate;
	else {
		/* XXX pass pktlen */
		(void) ieee80211_ratectl_rate(ni, NULL, 0);
		rate = ni->ni_txrate;
	}

	/* Encrypt the frame if need be. */
	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		/* Retrieve key for TX. */
		k = ieee80211_crypto_encap(ni, m);
		if (k == NULL)
			return (ENOBUFS);

		swcrypt = k->wk_flags & IEEE80211_KEY_SWCRYPT;

		/* 802.11 header may have moved. */
		wh = mtod(m, struct ieee80211_frame *);
	}
	totlen = m->m_pkthdr.len;

	if (ieee80211_radiotap_active_vap(vap)) {
		struct wpi_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;
		if (k != NULL)
			tap->wt_flags |= IEEE80211_RADIOTAP_F_WEP;
		if (wh->i_fc[1] & IEEE80211_FC1_MORE_FRAG)
			tap->wt_flags |= IEEE80211_RADIOTAP_F_FRAG;

		ieee80211_radiotap_tx(vap, m);
	}

	flags = 0;
	if (!ismcast) {
		/* Unicast frame, check if an ACK is expected. */
		if (!qos || (qos & IEEE80211_QOS_ACKPOLICY) !=
		    IEEE80211_QOS_ACKPOLICY_NOACK)
			flags |= WPI_TX_NEED_ACK;
	}

	if (!IEEE80211_QOS_HAS_SEQ(wh))
		flags |= WPI_TX_AUTO_SEQ;
	if (wh->i_fc[1] & IEEE80211_FC1_MORE_FRAG)
		flags |= WPI_TX_MORE_FRAG;

	/* Check if frame must be protected using RTS/CTS or CTS-to-self. */
	if (!ismcast) {
		/* NB: Group frames are sent using CCK in 802.11b/g. */
		if (totlen + IEEE80211_CRC_LEN > vap->iv_rtsthreshold) {
			flags |= WPI_TX_NEED_RTS;
		} else if ((ic->ic_flags & IEEE80211_F_USEPROT) &&
		    WPI_RATE_IS_OFDM(rate)) {
			if (ic->ic_protmode == IEEE80211_PROT_CTSONLY)
				flags |= WPI_TX_NEED_CTS;
			else if (ic->ic_protmode == IEEE80211_PROT_RTSCTS)
				flags |= WPI_TX_NEED_RTS;
		}

		if (flags & (WPI_TX_NEED_RTS | WPI_TX_NEED_CTS))
			flags |= WPI_TX_FULL_TXOP;
	}

	memset(tx, 0, sizeof (struct wpi_cmd_data));
	if (type == IEEE80211_FC0_TYPE_MGT) {
		uint8_t subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

		/* Tell HW to set timestamp in probe responses. */
		if (subtype == IEEE80211_FC0_SUBTYPE_PROBE_RESP)
			flags |= WPI_TX_INSERT_TSTAMP;
		if (subtype == IEEE80211_FC0_SUBTYPE_ASSOC_REQ ||
		    subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ)
			tx->timeout = htole16(3);
		else
			tx->timeout = htole16(2);
	}

	if (ismcast || type != IEEE80211_FC0_TYPE_DATA)
		tx->id = WPI_ID_BROADCAST;
	else {
		if (wn->id == WPI_ID_UNDEFINED) {
			device_printf(sc->sc_dev,
			    "%s: undefined node id\n", __func__);
			return (EINVAL);
		}

		tx->id = wn->id;
	}

	if (!swcrypt) {
		switch (k->wk_cipher->ic_cipher) {
		case IEEE80211_CIPHER_AES_CCM:
			tx->security = WPI_CIPHER_CCMP;
			break;

		default:
			break;
		}

		memcpy(tx->key, k->wk_key, k->wk_keylen);
	}

	if (wh->i_fc[1] & IEEE80211_FC1_MORE_FRAG) {
		struct mbuf *next = m->m_nextpkt;

		tx->lnext = htole16(next->m_pkthdr.len);
		tx->fnext = htole32(tx->security |
				    (flags & WPI_TX_NEED_ACK) |
				    WPI_NEXT_STA_ID(tx->id));
	}

	tx->len = htole16(totlen);
	tx->flags = htole32(flags);
	tx->plcp = rate2plcp(rate);
	tx->tid = tid;
	tx->lifetime = htole32(WPI_LIFETIME_INFINITE);
	tx->ofdm_mask = 0xff;
	tx->cck_mask = 0x0f;
	tx->rts_ntries = 7;
	tx->data_ntries = tp->maxretry;

	tx_data.ni = ni;
	tx_data.m = m;
	tx_data.size = sizeof(struct wpi_cmd_data);
	tx_data.code = WPI_CMD_TX_DATA;
	tx_data.ac = ac;

	return wpi_cmd2(sc, &tx_data);
}

static int
wpi_tx_data_raw(struct wpi_softc *sc, struct mbuf *m,
    struct ieee80211_node *ni, const struct ieee80211_bpf_params *params)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_key *k = NULL;
	struct ieee80211_frame *wh;
	struct wpi_buf tx_data;
	struct wpi_cmd_data *tx = (struct wpi_cmd_data *)&tx_data.data;
	uint32_t flags;
	uint8_t ac, type, rate;
	int swcrypt, totlen;

	wh = mtod(m, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	swcrypt = 1;

	ac = params->ibp_pri & 3;

	/* Choose a TX rate index. */
	rate = params->ibp_rate0;

	flags = 0;
	if (!IEEE80211_QOS_HAS_SEQ(wh))
		flags |= WPI_TX_AUTO_SEQ;
	if ((params->ibp_flags & IEEE80211_BPF_NOACK) == 0)
		flags |= WPI_TX_NEED_ACK;
	if (params->ibp_flags & IEEE80211_BPF_RTS)
		flags |= WPI_TX_NEED_RTS;
	if (params->ibp_flags & IEEE80211_BPF_CTS)
		flags |= WPI_TX_NEED_CTS;
	if (flags & (WPI_TX_NEED_RTS | WPI_TX_NEED_CTS))
		flags |= WPI_TX_FULL_TXOP;

	/* Encrypt the frame if need be. */
	if (params->ibp_flags & IEEE80211_BPF_CRYPTO) {
		/* Retrieve key for TX. */
		k = ieee80211_crypto_encap(ni, m);
		if (k == NULL)
			return (ENOBUFS);

		swcrypt = k->wk_flags & IEEE80211_KEY_SWCRYPT;

		/* 802.11 header may have moved. */
		wh = mtod(m, struct ieee80211_frame *);
	}
	totlen = m->m_pkthdr.len;

	if (ieee80211_radiotap_active_vap(vap)) {
		struct wpi_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;
		if (params->ibp_flags & IEEE80211_BPF_CRYPTO)
			tap->wt_flags |= IEEE80211_RADIOTAP_F_WEP;

		ieee80211_radiotap_tx(vap, m);
	}

	memset(tx, 0, sizeof (struct wpi_cmd_data));
	if (type == IEEE80211_FC0_TYPE_MGT) {
		uint8_t subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

		/* Tell HW to set timestamp in probe responses. */
		if (subtype == IEEE80211_FC0_SUBTYPE_PROBE_RESP)
			flags |= WPI_TX_INSERT_TSTAMP;
		if (subtype == IEEE80211_FC0_SUBTYPE_ASSOC_REQ ||
		    subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ)
			tx->timeout = htole16(3);
		else
			tx->timeout = htole16(2);
	}

	if (!swcrypt) {
		switch (k->wk_cipher->ic_cipher) {
		case IEEE80211_CIPHER_AES_CCM:
			tx->security = WPI_CIPHER_CCMP;
			break;

		default:
			break;
		}

		memcpy(tx->key, k->wk_key, k->wk_keylen);
	}

	tx->len = htole16(totlen);
	tx->flags = htole32(flags);
	tx->plcp = rate2plcp(rate);
	tx->id = WPI_ID_BROADCAST;
	tx->lifetime = htole32(WPI_LIFETIME_INFINITE);
	tx->rts_ntries = params->ibp_try1;
	tx->data_ntries = params->ibp_try0;

	tx_data.ni = ni;
	tx_data.m = m;
	tx_data.size = sizeof(struct wpi_cmd_data);
	tx_data.code = WPI_CMD_TX_DATA;
	tx_data.ac = ac;

	return wpi_cmd2(sc, &tx_data);
}

static __inline int
wpi_tx_ring_free_space(struct wpi_softc *sc, uint16_t ac)
{
	struct wpi_tx_ring *ring = &sc->txq[ac];
	int retval;

	WPI_TXQ_STATE_LOCK(sc);
	retval = WPI_TX_RING_HIMARK - ring->queued;
	WPI_TXQ_STATE_UNLOCK(sc);

	return retval;
}

static int
wpi_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
    const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct wpi_softc *sc = ic->ic_softc;
	uint16_t ac;
	int error = 0;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	ac = M_WME_GETAC(m);

	WPI_TX_LOCK(sc);

	/* NB: no fragments here */
	if (sc->sc_running == 0 || wpi_tx_ring_free_space(sc, ac) < 1) {
		error = sc->sc_running ? ENOBUFS : ENETDOWN;
		goto unlock;
	}

	if (params == NULL) {
		/*
		 * Legacy path; interpret frame contents to decide
		 * precisely how to send the frame.
		 */
		error = wpi_tx_data(sc, m, ni);
	} else {
		/*
		 * Caller supplied explicit parameters to use in
		 * sending the frame.
		 */
		error = wpi_tx_data_raw(sc, m, ni, params);
	}

unlock:	WPI_TX_UNLOCK(sc);

	if (error != 0) {
		m_freem(m);
		DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END_ERR, __func__);

		return error;
	}

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);

	return 0;
}

static int
wpi_transmit(struct ieee80211com *ic, struct mbuf *m)
{
	struct wpi_softc *sc = ic->ic_softc;
	struct ieee80211_node *ni;
	struct mbuf *mnext;
	uint16_t ac;
	int error, nmbufs;

	WPI_TX_LOCK(sc);
	DPRINTF(sc, WPI_DEBUG_XMIT, "%s: called\n", __func__);

	/* Check if interface is up & running. */
	if (__predict_false(sc->sc_running == 0)) {
		error = ENXIO;
		goto unlock;
	}

	nmbufs = 1;
	for (mnext = m->m_nextpkt; mnext != NULL; mnext = mnext->m_nextpkt)
		nmbufs++;

	/* Check for available space. */
	ac = M_WME_GETAC(m);
	if (wpi_tx_ring_free_space(sc, ac) < nmbufs) {
		error = ENOBUFS;
		goto unlock;
	}

	error = 0;
	ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
	do {
		mnext = m->m_nextpkt;
		if (wpi_tx_data(sc, m, ni) != 0) {
			if_inc_counter(ni->ni_vap->iv_ifp, IFCOUNTER_OERRORS,
			    nmbufs);
			wpi_free_txfrags(sc, ac);
			ieee80211_free_mbuf(m);
			ieee80211_free_node(ni);
			break;
		}
	} while((m = mnext) != NULL);

	DPRINTF(sc, WPI_DEBUG_XMIT, "%s: done\n", __func__);

unlock:	WPI_TX_UNLOCK(sc);

	return (error);
}

static void
wpi_watchdog_rfkill(void *arg)
{
	struct wpi_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;

	DPRINTF(sc, WPI_DEBUG_WATCHDOG, "RFkill Watchdog: tick\n");

	/* No need to lock firmware memory. */
	if ((wpi_prph_read(sc, WPI_APMG_RFKILL) & 0x1) == 0) {
		/* Radio kill switch is still off. */
		callout_reset(&sc->watchdog_rfkill, hz, wpi_watchdog_rfkill,
		    sc);
	} else
		ieee80211_runtask(ic, &sc->sc_radioon_task);
}

static void
wpi_scan_timeout(void *arg)
{
	struct wpi_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;

	ic_printf(ic, "scan timeout\n");
	ieee80211_restart_all(ic);
}

static void
wpi_tx_timeout(void *arg)
{
	struct wpi_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;

	ic_printf(ic, "device timeout\n");
	ieee80211_restart_all(ic);
}

static void
wpi_parent(struct ieee80211com *ic)
{
	struct wpi_softc *sc = ic->ic_softc;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);

	if (ic->ic_nrunning > 0) {
		if (wpi_init(sc) == 0) {
			ieee80211_notify_radio(ic, 1);
			ieee80211_start_all(ic);
		} else {
			ieee80211_notify_radio(ic, 0);
			ieee80211_stop(vap);
		}
	} else {
		ieee80211_notify_radio(ic, 0);
		wpi_stop(sc);
	}
}

/*
 * Send a command to the firmware.
 */
static int
wpi_cmd(struct wpi_softc *sc, uint8_t code, const void *buf, uint16_t size,
    int async)
{
	struct wpi_tx_ring *ring = &sc->txq[WPI_CMD_QUEUE_NUM];
	struct wpi_tx_desc *desc;
	struct wpi_tx_data *data;
	struct wpi_tx_cmd *cmd;
	struct mbuf *m;
	bus_addr_t paddr;
	uint16_t totlen;
	int error;

	WPI_TXQ_LOCK(sc);

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	if (__predict_false(sc->sc_running == 0)) {
		/* wpi_stop() was called */
		if (code == WPI_CMD_SCAN)
			error = ENETDOWN;
		else
			error = 0;

		goto fail;
	}

	if (async == 0)
		WPI_LOCK_ASSERT(sc);

	DPRINTF(sc, WPI_DEBUG_CMD, "%s: cmd %s size %u async %d\n",
	    __func__, wpi_cmd_str(code), size, async);

	desc = &ring->desc[ring->cur];
	data = &ring->data[ring->cur];
	totlen = 4 + size;

	if (size > sizeof cmd->data) {
		/* Command is too large to fit in a descriptor. */
		if (totlen > MCLBYTES) {
			error = EINVAL;
			goto fail;
		}
		m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MJUMPAGESIZE);
		if (m == NULL) {
			error = ENOMEM;
			goto fail;
		}
		cmd = mtod(m, struct wpi_tx_cmd *);
		error = bus_dmamap_load(ring->data_dmat, data->map, cmd,
		    totlen, wpi_dma_map_addr, &paddr, BUS_DMA_NOWAIT);
		if (error != 0) {
			m_freem(m);
			goto fail;
		}
		data->m = m;
	} else {
		cmd = &ring->cmd[ring->cur];
		paddr = data->cmd_paddr;
	}

	cmd->code = code;
	cmd->flags = 0;
	cmd->qid = ring->qid;
	cmd->idx = ring->cur;
	memcpy(cmd->data, buf, size);

	desc->nsegs = 1 + (WPI_PAD32(size) << 4);
	desc->segs[0].addr = htole32(paddr);
	desc->segs[0].len  = htole32(totlen);

	if (size > sizeof cmd->data) {
		bus_dmamap_sync(ring->data_dmat, data->map,
		    BUS_DMASYNC_PREWRITE);
	} else {
		bus_dmamap_sync(ring->data_dmat, ring->cmd_dma.map,
		    BUS_DMASYNC_PREWRITE);
	}
	bus_dmamap_sync(ring->desc_dma.tag, ring->desc_dma.map,
	    BUS_DMASYNC_PREWRITE);

	/* Kick command ring. */
	ring->cur = (ring->cur + 1) % WPI_TX_RING_COUNT;
	sc->sc_update_tx_ring(sc, ring);

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);

	WPI_TXQ_UNLOCK(sc);

	return async ? 0 : mtx_sleep(cmd, &sc->sc_mtx, PCATCH, "wpicmd", hz);

fail:	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END_ERR, __func__);

	WPI_TXQ_UNLOCK(sc);

	return error;
}

/*
 * Configure HW multi-rate retries.
 */
static int
wpi_mrr_setup(struct wpi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct wpi_mrr_setup mrr;
	uint8_t i;
	int error;

	/* CCK rates (not used with 802.11a). */
	for (i = WPI_RIDX_CCK1; i <= WPI_RIDX_CCK11; i++) {
		mrr.rates[i].flags = 0;
		mrr.rates[i].plcp = wpi_ridx_to_plcp[i];
		/* Fallback to the immediate lower CCK rate (if any.) */
		mrr.rates[i].next =
		    (i == WPI_RIDX_CCK1) ? WPI_RIDX_CCK1 : i - 1;
		/* Try twice at this rate before falling back to "next". */
		mrr.rates[i].ntries = WPI_NTRIES_DEFAULT;
	}
	/* OFDM rates (not used with 802.11b). */
	for (i = WPI_RIDX_OFDM6; i <= WPI_RIDX_OFDM54; i++) {
		mrr.rates[i].flags = 0;
		mrr.rates[i].plcp = wpi_ridx_to_plcp[i];
		/* Fallback to the immediate lower rate (if any.) */
		/* We allow fallback from OFDM/6 to CCK/2 in 11b/g mode. */
		mrr.rates[i].next = (i == WPI_RIDX_OFDM6) ?
		    ((ic->ic_curmode == IEEE80211_MODE_11A) ?
			WPI_RIDX_OFDM6 : WPI_RIDX_CCK2) :
		    i - 1;
		/* Try twice at this rate before falling back to "next". */
		mrr.rates[i].ntries = WPI_NTRIES_DEFAULT;
	}
	/* Setup MRR for control frames. */
	mrr.which = htole32(WPI_MRR_CTL);
	error = wpi_cmd(sc, WPI_CMD_MRR_SETUP, &mrr, sizeof mrr, 0);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not setup MRR for control frames\n");
		return error;
	}
	/* Setup MRR for data frames. */
	mrr.which = htole32(WPI_MRR_DATA);
	error = wpi_cmd(sc, WPI_CMD_MRR_SETUP, &mrr, sizeof mrr, 0);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "could not setup MRR for data frames\n");
		return error;
	}
	return 0;
}

static int
wpi_add_node(struct wpi_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct wpi_vap *wvp = WPI_VAP(ni->ni_vap);
	struct wpi_node *wn = WPI_NODE(ni);
	struct wpi_node_info node;
	int error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	if (wn->id == WPI_ID_UNDEFINED)
		return EINVAL;

	memset(&node, 0, sizeof node);
	IEEE80211_ADDR_COPY(node.macaddr, ni->ni_macaddr);
	node.id = wn->id;
	node.plcp = (ic->ic_curmode == IEEE80211_MODE_11A) ?
	    wpi_ridx_to_plcp[WPI_RIDX_OFDM6] : wpi_ridx_to_plcp[WPI_RIDX_CCK1];
	node.action = htole32(WPI_ACTION_SET_RATE);
	node.antenna = WPI_ANTENNA_BOTH;

	DPRINTF(sc, WPI_DEBUG_NODE, "%s: adding node %d (%s)\n", __func__,
	    wn->id, ether_sprintf(ni->ni_macaddr));

	error = wpi_cmd(sc, WPI_CMD_ADD_NODE, &node, sizeof node, 1);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: wpi_cmd() call failed with error code %d\n", __func__,
		    error);
		return error;
	}

	if (wvp->wv_gtk != 0) {
		error = wpi_set_global_keys(ni);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: error while setting global keys\n", __func__);
			return ENXIO;
		}
	}

	return 0;
}

/*
 * Broadcast node is used to send group-addressed and management frames.
 */
static int
wpi_add_broadcast_node(struct wpi_softc *sc, int async)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct wpi_node_info node;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	memset(&node, 0, sizeof node);
	IEEE80211_ADDR_COPY(node.macaddr, ieee80211broadcastaddr);
	node.id = WPI_ID_BROADCAST;
	node.plcp = (ic->ic_curmode == IEEE80211_MODE_11A) ?
	    wpi_ridx_to_plcp[WPI_RIDX_OFDM6] : wpi_ridx_to_plcp[WPI_RIDX_CCK1];
	node.action = htole32(WPI_ACTION_SET_RATE);
	node.antenna = WPI_ANTENNA_BOTH;

	DPRINTF(sc, WPI_DEBUG_NODE, "%s: adding broadcast node\n", __func__);

	return wpi_cmd(sc, WPI_CMD_ADD_NODE, &node, sizeof node, async);
}

static int
wpi_add_sta_node(struct wpi_softc *sc, struct ieee80211_node *ni)
{
	struct wpi_node *wn = WPI_NODE(ni);
	int error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	wn->id = wpi_add_node_entry_sta(sc);

	if ((error = wpi_add_node(sc, ni)) != 0) {
		wpi_del_node_entry(sc, wn->id);
		wn->id = WPI_ID_UNDEFINED;
		return error;
	}

	return 0;
}

static int
wpi_add_ibss_node(struct wpi_softc *sc, struct ieee80211_node *ni)
{
	struct wpi_node *wn = WPI_NODE(ni);
	int error;

	KASSERT(wn->id == WPI_ID_UNDEFINED,
	    ("the node %d was added before", wn->id));

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	if ((wn->id = wpi_add_node_entry_adhoc(sc)) == WPI_ID_UNDEFINED) {
		device_printf(sc->sc_dev, "%s: h/w table is full\n", __func__);
		return ENOMEM;
	}

	if ((error = wpi_add_node(sc, ni)) != 0) {
		wpi_del_node_entry(sc, wn->id);
		wn->id = WPI_ID_UNDEFINED;
		return error;
	}

	return 0;
}

static void
wpi_del_node(struct wpi_softc *sc, struct ieee80211_node *ni)
{
	struct wpi_node *wn = WPI_NODE(ni);
	struct wpi_cmd_del_node node;
	int error;

	KASSERT(wn->id != WPI_ID_UNDEFINED, ("undefined node id passed"));

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	memset(&node, 0, sizeof node);
	IEEE80211_ADDR_COPY(node.macaddr, ni->ni_macaddr);
	node.count = 1;

	DPRINTF(sc, WPI_DEBUG_NODE, "%s: deleting node %d (%s)\n", __func__,
	    wn->id, ether_sprintf(ni->ni_macaddr));

	error = wpi_cmd(sc, WPI_CMD_DEL_NODE, &node, sizeof node, 1);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not delete node %u, error %d\n", __func__,
		    wn->id, error);
	}
}

static int
wpi_updateedca(struct ieee80211com *ic)
{
#define WPI_EXP2(x)	((1 << (x)) - 1)	/* CWmin = 2^ECWmin - 1 */
	struct wpi_softc *sc = ic->ic_softc;
	struct chanAccParams chp;
	struct wpi_edca_params cmd;
	int aci, error;

	ieee80211_wme_ic_getparams(ic, &chp);

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	memset(&cmd, 0, sizeof cmd);
	cmd.flags = htole32(WPI_EDCA_UPDATE);
	for (aci = 0; aci < WME_NUM_AC; aci++) {
		const struct wmeParams *ac = &chp.cap_wmeParams[aci];
		cmd.ac[aci].aifsn = ac->wmep_aifsn;
		cmd.ac[aci].cwmin = htole16(WPI_EXP2(ac->wmep_logcwmin));
		cmd.ac[aci].cwmax = htole16(WPI_EXP2(ac->wmep_logcwmax));
		cmd.ac[aci].txoplimit = 
		    htole16(IEEE80211_TXOP_TO_US(ac->wmep_txopLimit));

		DPRINTF(sc, WPI_DEBUG_EDCA,
		    "setting WME for queue %d aifsn=%d cwmin=%d cwmax=%d "
		    "txoplimit=%d\n", aci, cmd.ac[aci].aifsn,
		    cmd.ac[aci].cwmin, cmd.ac[aci].cwmax,
		    cmd.ac[aci].txoplimit);
	}
	error = wpi_cmd(sc, WPI_CMD_EDCA_PARAMS, &cmd, sizeof cmd, 1);

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);

	return error;
#undef WPI_EXP2
}

static void
wpi_set_promisc(struct wpi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	uint32_t promisc_filter;

	promisc_filter = WPI_FILTER_CTL;
	if (vap != NULL && vap->iv_opmode != IEEE80211_M_HOSTAP)
		promisc_filter |= WPI_FILTER_PROMISC;

	if (ic->ic_promisc > 0)
		sc->rxon.filter |= htole32(promisc_filter);
	else
		sc->rxon.filter &= ~htole32(promisc_filter);
}

static void
wpi_update_promisc(struct ieee80211com *ic)
{
	struct wpi_softc *sc = ic->ic_softc;

	WPI_LOCK(sc);
	if (sc->sc_running == 0) {
		WPI_UNLOCK(sc);
		return;
	}
	WPI_UNLOCK(sc);

	WPI_RXON_LOCK(sc);
	wpi_set_promisc(sc);

	if (wpi_send_rxon(sc, 1, 1) != 0) {
		device_printf(sc->sc_dev, "%s: could not send RXON\n",
		    __func__);
	}
	WPI_RXON_UNLOCK(sc);
}

static void
wpi_update_mcast(struct ieee80211com *ic)
{
	/* Ignore */
}

static void
wpi_set_led(struct wpi_softc *sc, uint8_t which, uint8_t off, uint8_t on)
{
	struct wpi_cmd_led led;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	led.which = which;
	led.unit = htole32(100000);	/* on/off in unit of 100ms */
	led.off = off;
	led.on = on;
	(void)wpi_cmd(sc, WPI_CMD_SET_LED, &led, sizeof led, 1);
}

static int
wpi_set_timing(struct wpi_softc *sc, struct ieee80211_node *ni)
{
	struct wpi_cmd_timing cmd;
	uint64_t val, mod;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	memset(&cmd, 0, sizeof cmd);
	memcpy(&cmd.tstamp, ni->ni_tstamp.data, sizeof (uint64_t));
	cmd.bintval = htole16(ni->ni_intval);
	cmd.lintval = htole16(10);

	/* Compute remaining time until next beacon. */
	val = (uint64_t)ni->ni_intval * IEEE80211_DUR_TU;
	mod = le64toh(cmd.tstamp) % val;
	cmd.binitval = htole32((uint32_t)(val - mod));

	DPRINTF(sc, WPI_DEBUG_RESET, "timing bintval=%u tstamp=%ju, init=%u\n",
	    ni->ni_intval, le64toh(cmd.tstamp), (uint32_t)(val - mod));

	return wpi_cmd(sc, WPI_CMD_TIMING, &cmd, sizeof cmd, 1);
}

/*
 * This function is called periodically (every 60 seconds) to adjust output
 * power to temperature changes.
 */
static void
wpi_power_calibration(struct wpi_softc *sc)
{
	int temp;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	/* Update sensor data. */
	temp = (int)WPI_READ(sc, WPI_UCODE_GP2);
	DPRINTF(sc, WPI_DEBUG_TEMP, "Temp in calibration is: %d\n", temp);

	/* Sanity-check read value. */
	if (temp < -260 || temp > 25) {
		/* This can't be correct, ignore. */
		DPRINTF(sc, WPI_DEBUG_TEMP,
		    "out-of-range temperature reported: %d\n", temp);
		return;
	}

	DPRINTF(sc, WPI_DEBUG_TEMP, "temperature %d->%d\n", sc->temp, temp);

	/* Adjust Tx power if need be. */
	if (abs(temp - sc->temp) <= 6)
		return;

	sc->temp = temp;

	if (wpi_set_txpower(sc, 1) != 0) {
		/* just warn, too bad for the automatic calibration... */
		device_printf(sc->sc_dev,"could not adjust Tx power\n");
	}
}

/*
 * Set TX power for current channel.
 */
static int
wpi_set_txpower(struct wpi_softc *sc, int async)
{
	struct wpi_power_group *group;
	struct wpi_cmd_txpower cmd;
	uint8_t chan;
	int idx, is_chan_5ghz, i;

	/* Retrieve current channel from last RXON. */
	chan = sc->rxon.chan;
	is_chan_5ghz = (sc->rxon.flags & htole32(WPI_RXON_24GHZ)) == 0;

	/* Find the TX power group to which this channel belongs. */
	if (is_chan_5ghz) {
		for (group = &sc->groups[1]; group < &sc->groups[4]; group++)
			if (chan <= group->chan)
				break;
	} else
		group = &sc->groups[0];

	memset(&cmd, 0, sizeof cmd);
	cmd.band = is_chan_5ghz ? WPI_BAND_5GHZ : WPI_BAND_2GHZ;
	cmd.chan = htole16(chan);

	/* Set TX power for all OFDM and CCK rates. */
	for (i = 0; i <= WPI_RIDX_MAX ; i++) {
		/* Retrieve TX power for this channel/rate. */
		idx = wpi_get_power_index(sc, group, chan, is_chan_5ghz, i);

		cmd.rates[i].plcp = wpi_ridx_to_plcp[i];

		if (is_chan_5ghz) {
			cmd.rates[i].rf_gain = wpi_rf_gain_5ghz[idx];
			cmd.rates[i].dsp_gain = wpi_dsp_gain_5ghz[idx];
		} else {
			cmd.rates[i].rf_gain = wpi_rf_gain_2ghz[idx];
			cmd.rates[i].dsp_gain = wpi_dsp_gain_2ghz[idx];
		}
		DPRINTF(sc, WPI_DEBUG_TEMP,
		    "chan %d/ridx %d: power index %d\n", chan, i, idx);
	}

	return wpi_cmd(sc, WPI_CMD_TXPOWER, &cmd, sizeof cmd, async);
}

/*
 * Determine Tx power index for a given channel/rate combination.
 * This takes into account the regulatory information from EEPROM and the
 * current temperature.
 */
static int
wpi_get_power_index(struct wpi_softc *sc, struct wpi_power_group *group,
    uint8_t chan, int is_chan_5ghz, int ridx)
{
/* Fixed-point arithmetic division using a n-bit fractional part. */
#define fdivround(a, b, n)	\
	((((1 << n) * (a)) / (b) + (1 << n) / 2) / (1 << n))

/* Linear interpolation. */
#define interpolate(x, x1, y1, x2, y2, n)	\
	((y1) + fdivround(((x) - (x1)) * ((y2) - (y1)), (x2) - (x1), n))

	struct wpi_power_sample *sample;
	int pwr, idx;

	/* Default TX power is group maximum TX power minus 3dB. */
	pwr = group->maxpwr / 2;

	/* Decrease TX power for highest OFDM rates to reduce distortion. */
	switch (ridx) {
	case WPI_RIDX_OFDM36:
		pwr -= is_chan_5ghz ?  5 : 0;
		break;
	case WPI_RIDX_OFDM48:
		pwr -= is_chan_5ghz ? 10 : 7;
		break;
	case WPI_RIDX_OFDM54:
		pwr -= is_chan_5ghz ? 12 : 9;
		break;
	}

	/* Never exceed the channel maximum allowed TX power. */
	pwr = min(pwr, sc->maxpwr[chan]);

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
		return 0;
	if (idx > WPI_MAX_PWR_INDEX)
		return WPI_MAX_PWR_INDEX;
	return idx;

#undef interpolate
#undef fdivround
}

/*
 * Set STA mode power saving level (between 0 and 5).
 * Level 0 is CAM (Continuously Aware Mode), 5 is for maximum power saving.
 */
static int
wpi_set_pslevel(struct wpi_softc *sc, uint8_t dtim, int level, int async)
{
	struct wpi_pmgt_cmd cmd;
	const struct wpi_pmgt *pmgt;
	uint32_t max, reg;
	uint8_t skip_dtim;
	int i;

	DPRINTF(sc, WPI_DEBUG_PWRSAVE,
	    "%s: dtim=%d, level=%d, async=%d\n",
	    __func__, dtim, level, async);

	/* Select which PS parameters to use. */
	if (dtim <= 10)
		pmgt = &wpi_pmgt[0][level];
	else
		pmgt = &wpi_pmgt[1][level];

	memset(&cmd, 0, sizeof cmd);
	if (level != 0)	/* not CAM */
		cmd.flags |= htole16(WPI_PS_ALLOW_SLEEP);
	/* Retrieve PCIe Active State Power Management (ASPM). */
	reg = pci_read_config(sc->sc_dev, sc->sc_cap_off + PCIER_LINK_CTL, 1);
	if (!(reg & PCIEM_LINK_CTL_ASPMC_L0S))	/* L0s Entry disabled. */
		cmd.flags |= htole16(WPI_PS_PCI_PMGT);

	cmd.rxtimeout = htole32(pmgt->rxtimeout * IEEE80211_DUR_TU);
	cmd.txtimeout = htole32(pmgt->txtimeout * IEEE80211_DUR_TU);

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
			max = rounddown(max, dtim);
	} else
		max = dtim;

	for (i = 0; i < 5; i++)
		cmd.intval[i] = htole32(MIN(max, pmgt->intval[i]));

	return wpi_cmd(sc, WPI_CMD_SET_POWER_MODE, &cmd, sizeof cmd, async);
}

static int
wpi_send_btcoex(struct wpi_softc *sc)
{
	struct wpi_bluetooth cmd;

	memset(&cmd, 0, sizeof cmd);
	cmd.flags = WPI_BT_COEX_MODE_4WIRE;
	cmd.lead_time = WPI_BT_LEAD_TIME_DEF;
	cmd.max_kill = WPI_BT_MAX_KILL_DEF;
	DPRINTF(sc, WPI_DEBUG_RESET, "%s: configuring bluetooth coexistence\n",
	    __func__);
	return wpi_cmd(sc, WPI_CMD_BT_COEX, &cmd, sizeof(cmd), 0);
}

static int
wpi_send_rxon(struct wpi_softc *sc, int assoc, int async)
{
	int error;

	if (async)
		WPI_RXON_LOCK_ASSERT(sc);

	if (assoc && wpi_check_bss_filter(sc) != 0) {
		struct wpi_assoc rxon_assoc;

		rxon_assoc.flags = sc->rxon.flags;
		rxon_assoc.filter = sc->rxon.filter;
		rxon_assoc.ofdm_mask = sc->rxon.ofdm_mask;
		rxon_assoc.cck_mask = sc->rxon.cck_mask;
		rxon_assoc.reserved = 0;

		error = wpi_cmd(sc, WPI_CMD_RXON_ASSOC, &rxon_assoc,
		    sizeof (struct wpi_assoc), async);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "RXON_ASSOC command failed, error %d\n", error);
			return error;
		}
	} else {
		if (async) {
			WPI_NT_LOCK(sc);
			error = wpi_cmd(sc, WPI_CMD_RXON, &sc->rxon,
			    sizeof (struct wpi_rxon), async);
			if (error == 0)
				wpi_clear_node_table(sc);
			WPI_NT_UNLOCK(sc);
		} else {
			error = wpi_cmd(sc, WPI_CMD_RXON, &sc->rxon,
			    sizeof (struct wpi_rxon), async);
			if (error == 0)
				wpi_clear_node_table(sc);
		}

		if (error != 0) {
			device_printf(sc->sc_dev,
			    "RXON command failed, error %d\n", error);
			return error;
		}

		/* Add broadcast node. */
		error = wpi_add_broadcast_node(sc, async);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "could not add broadcast node, error %d\n", error);
			return error;
		}
	}

	/* Configuration has changed, set Tx power accordingly. */
	if ((error = wpi_set_txpower(sc, async)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not set TX power, error %d\n", __func__, error);
		return error;
	}

	return 0;
}

/**
 * Configure the card to listen to a particular channel, this transisions the
 * card in to being able to receive frames from remote devices.
 */
static int
wpi_config(struct wpi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	struct ieee80211_channel *c = ic->ic_curchan;
	int error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	/* Set power saving level to CAM during initialization. */
	if ((error = wpi_set_pslevel(sc, 0, 0, 0)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not set power saving level\n", __func__);
		return error;
	}

	/* Configure bluetooth coexistence. */
	if ((error = wpi_send_btcoex(sc)) != 0) {
		device_printf(sc->sc_dev,
		    "could not configure bluetooth coexistence\n");
		return error;
	}

	/* Configure adapter. */
	memset(&sc->rxon, 0, sizeof (struct wpi_rxon));
	IEEE80211_ADDR_COPY(sc->rxon.myaddr, vap->iv_myaddr);

	/* Set default channel. */
	sc->rxon.chan = ieee80211_chan2ieee(ic, c);
	sc->rxon.flags = htole32(WPI_RXON_TSF | WPI_RXON_CTS_TO_SELF);
	if (IEEE80211_IS_CHAN_2GHZ(c))
		sc->rxon.flags |= htole32(WPI_RXON_AUTO | WPI_RXON_24GHZ);

	sc->rxon.filter = WPI_FILTER_MULTICAST;
	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		sc->rxon.mode = WPI_MODE_STA;
		break;
	case IEEE80211_M_IBSS:
		sc->rxon.mode = WPI_MODE_IBSS;
		sc->rxon.filter |= WPI_FILTER_BEACON;
		break;
	case IEEE80211_M_HOSTAP:
		/* XXX workaround for beaconing */
		sc->rxon.mode = WPI_MODE_IBSS;
		sc->rxon.filter |= WPI_FILTER_ASSOC | WPI_FILTER_PROMISC;
		break;
	case IEEE80211_M_AHDEMO:
		sc->rxon.mode = WPI_MODE_HOSTAP;
		break;
	case IEEE80211_M_MONITOR:
		sc->rxon.mode = WPI_MODE_MONITOR;
		break;
	default:
		device_printf(sc->sc_dev, "unknown opmode %d\n",
		    ic->ic_opmode);
		return EINVAL;
	}
	sc->rxon.filter = htole32(sc->rxon.filter);
	wpi_set_promisc(sc);
	sc->rxon.cck_mask  = 0x0f;	/* not yet negotiated */
	sc->rxon.ofdm_mask = 0xff;	/* not yet negotiated */

	if ((error = wpi_send_rxon(sc, 0, 0)) != 0) {
		device_printf(sc->sc_dev, "%s: could not send RXON\n",
		    __func__);
		return error;
	}

	/* Setup rate scalling. */
	if ((error = wpi_mrr_setup(sc)) != 0) {
		device_printf(sc->sc_dev, "could not setup MRR, error %d\n",
		    error);
		return error;
	}

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);

	return 0;
}

static uint16_t
wpi_get_active_dwell_time(struct wpi_softc *sc,
    struct ieee80211_channel *c, uint8_t n_probes)
{
	/* No channel? Default to 2GHz settings. */
	if (c == NULL || IEEE80211_IS_CHAN_2GHZ(c)) {
		return (WPI_ACTIVE_DWELL_TIME_2GHZ +
		WPI_ACTIVE_DWELL_FACTOR_2GHZ * (n_probes + 1));
	}

	/* 5GHz dwell time. */
	return (WPI_ACTIVE_DWELL_TIME_5GHZ +
	    WPI_ACTIVE_DWELL_FACTOR_5GHZ * (n_probes + 1));
}

/*
 * Limit the total dwell time.
 *
 * Returns the dwell time in milliseconds.
 */
static uint16_t
wpi_limit_dwell(struct wpi_softc *sc, uint16_t dwell_time)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	uint16_t bintval = 0;

	/* bintval is in TU (1.024mS) */
	if (vap != NULL)
		bintval = vap->iv_bss->ni_intval;

	/*
	 * If it's non-zero, we should calculate the minimum of
	 * it and the DWELL_BASE.
	 *
	 * XXX Yes, the math should take into account that bintval
	 * is 1.024mS, not 1mS..
	 */
	if (bintval > 0) {
		DPRINTF(sc, WPI_DEBUG_SCAN, "%s: bintval=%d\n", __func__,
		    bintval);
		return (MIN(dwell_time, bintval - WPI_CHANNEL_TUNE_TIME * 2));
	}

	/* No association context? Default. */
	return dwell_time;
}

static uint16_t
wpi_get_passive_dwell_time(struct wpi_softc *sc, struct ieee80211_channel *c)
{
	uint16_t passive;

	if (c == NULL || IEEE80211_IS_CHAN_2GHZ(c))
		passive = WPI_PASSIVE_DWELL_BASE + WPI_PASSIVE_DWELL_TIME_2GHZ;
	else
		passive = WPI_PASSIVE_DWELL_BASE + WPI_PASSIVE_DWELL_TIME_5GHZ;

	/* Clamp to the beacon interval if we're associated. */
	return (wpi_limit_dwell(sc, passive));
}

static uint32_t
wpi_get_scan_pause_time(uint32_t time, uint16_t bintval)
{
	uint32_t mod = (time % bintval) * IEEE80211_DUR_TU;
	uint32_t nbeacons = time / bintval;

	if (mod > WPI_PAUSE_MAX_TIME)
		mod = WPI_PAUSE_MAX_TIME;

	return WPI_PAUSE_SCAN(nbeacons, mod);
}

/*
 * Send a scan request to the firmware.
 */
static int
wpi_scan(struct wpi_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_scan_state *ss = ic->ic_scan;
	struct ieee80211vap *vap = ss->ss_vap;
	struct wpi_scan_hdr *hdr;
	struct wpi_cmd_data *tx;
	struct wpi_scan_essid *essids;
	struct wpi_scan_chan *chan;
	struct ieee80211_frame *wh;
	struct ieee80211_rateset *rs;
	uint16_t bintval, buflen, dwell_active, dwell_passive;
	uint8_t *buf, *frm, i, nssid;
	int bgscan, error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	/*
	 * We are absolutely not allowed to send a scan command when another
	 * scan command is pending.
	 */
	if (callout_pending(&sc->scan_timeout)) {
		device_printf(sc->sc_dev, "%s: called whilst scanning!\n",
		    __func__);
		error = EAGAIN;
		goto fail;
	}

	bgscan = wpi_check_bss_filter(sc);
	bintval = vap->iv_bss->ni_intval;
	if (bgscan != 0 &&
	    bintval < WPI_QUIET_TIME_DEFAULT + WPI_CHANNEL_TUNE_TIME * 2) {
		error = EOPNOTSUPP;
		goto fail;
	}

	buf = malloc(WPI_SCAN_MAXSZ, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (buf == NULL) {
		device_printf(sc->sc_dev,
		    "%s: could not allocate buffer for scan command\n",
		    __func__);
		error = ENOMEM;
		goto fail;
	}
	hdr = (struct wpi_scan_hdr *)buf;

	/*
	 * Move to the next channel if no packets are received within 10 msecs
	 * after sending the probe request.
	 */
	hdr->quiet_time = htole16(WPI_QUIET_TIME_DEFAULT);
	hdr->quiet_threshold = htole16(1);

	if (bgscan != 0) {
		/*
		 * Max needs to be greater than active and passive and quiet!
		 * It's also in microseconds!
		 */
		hdr->max_svc = htole32(250 * IEEE80211_DUR_TU);
		hdr->pause_svc = htole32(wpi_get_scan_pause_time(100,
		    bintval));
	}

	hdr->filter = htole32(WPI_FILTER_MULTICAST | WPI_FILTER_BEACON);

	tx = (struct wpi_cmd_data *)(hdr + 1);
	tx->flags = htole32(WPI_TX_AUTO_SEQ);
	tx->id = WPI_ID_BROADCAST;
	tx->lifetime = htole32(WPI_LIFETIME_INFINITE);

	if (IEEE80211_IS_CHAN_5GHZ(c)) {
		/* Send probe requests at 6Mbps. */
		tx->plcp = wpi_ridx_to_plcp[WPI_RIDX_OFDM6];
		rs = &ic->ic_sup_rates[IEEE80211_MODE_11A];
	} else {
		hdr->flags = htole32(WPI_RXON_24GHZ | WPI_RXON_AUTO);
		/* Send probe requests at 1Mbps. */
		tx->plcp = wpi_ridx_to_plcp[WPI_RIDX_CCK1];
		rs = &ic->ic_sup_rates[IEEE80211_MODE_11G];
	}

	essids = (struct wpi_scan_essid *)(tx + 1);
	nssid = MIN(ss->ss_nssid, WPI_SCAN_MAX_ESSIDS);
	for (i = 0; i < nssid; i++) {
		essids[i].id = IEEE80211_ELEMID_SSID;
		essids[i].len = MIN(ss->ss_ssid[i].len, IEEE80211_NWID_LEN);
		memcpy(essids[i].data, ss->ss_ssid[i].ssid, essids[i].len);
#ifdef WPI_DEBUG
		if (sc->sc_debug & WPI_DEBUG_SCAN) {
			printf("Scanning Essid: ");
			ieee80211_print_essid(essids[i].data, essids[i].len);
			printf("\n");
		}
#endif
	}

	/*
	 * Build a probe request frame.  Most of the following code is a
	 * copy & paste of what is done in net80211.
	 */
	wh = (struct ieee80211_frame *)(essids + WPI_SCAN_MAX_ESSIDS);
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT |
		IEEE80211_FC0_SUBTYPE_PROBE_REQ;
	wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	IEEE80211_ADDR_COPY(wh->i_addr1, ieee80211broadcastaddr);
	IEEE80211_ADDR_COPY(wh->i_addr2, vap->iv_myaddr);
	IEEE80211_ADDR_COPY(wh->i_addr3, ieee80211broadcastaddr);

	frm = (uint8_t *)(wh + 1);
	frm = ieee80211_add_ssid(frm, NULL, 0);
	frm = ieee80211_add_rates(frm, rs);
	if (rs->rs_nrates > IEEE80211_RATE_SIZE)
		frm = ieee80211_add_xrates(frm, rs);

	/* Set length of probe request. */
	tx->len = htole16(frm - (uint8_t *)wh);

	/*
	 * Construct information about the channel that we
	 * want to scan. The firmware expects this to be directly
	 * after the scan probe request
	 */
	chan = (struct wpi_scan_chan *)frm;
	chan->chan = ieee80211_chan2ieee(ic, c);
	chan->flags = 0;
	if (nssid) {
		hdr->crc_threshold = WPI_SCAN_CRC_TH_DEFAULT;
		chan->flags |= WPI_CHAN_NPBREQS(nssid);
	} else
		hdr->crc_threshold = WPI_SCAN_CRC_TH_NEVER;

	if (!IEEE80211_IS_CHAN_PASSIVE(c))
		chan->flags |= WPI_CHAN_ACTIVE;

	/*
	 * Calculate the active/passive dwell times.
	 */
	dwell_active = wpi_get_active_dwell_time(sc, c, nssid);
	dwell_passive = wpi_get_passive_dwell_time(sc, c);

	/* Make sure they're valid. */
	if (dwell_active > dwell_passive)
		dwell_active = dwell_passive;

	chan->active = htole16(dwell_active);
	chan->passive = htole16(dwell_passive);

	chan->dsp_gain = 0x6e;  /* Default level */

	if (IEEE80211_IS_CHAN_5GHZ(c))
		chan->rf_gain = 0x3b;
	else
		chan->rf_gain = 0x28;

	DPRINTF(sc, WPI_DEBUG_SCAN, "Scanning %u Passive: %d\n",
	    chan->chan, IEEE80211_IS_CHAN_PASSIVE(c));

	hdr->nchan++;

	if (hdr->nchan == 1 && sc->rxon.chan == chan->chan) {
		/* XXX Force probe request transmission. */
		memcpy(chan + 1, chan, sizeof (struct wpi_scan_chan));

		chan++;

		/* Reduce unnecessary delay. */
		chan->flags = 0;
		chan->passive = chan->active = hdr->quiet_time;

		hdr->nchan++;
	}

	chan++;

	buflen = (uint8_t *)chan - buf;
	hdr->len = htole16(buflen);

	DPRINTF(sc, WPI_DEBUG_CMD, "sending scan command nchan=%d\n",
	    hdr->nchan);
	error = wpi_cmd(sc, WPI_CMD_SCAN, buf, buflen, 1);
	free(buf, M_DEVBUF);

	if (error != 0)
		goto fail;

	callout_reset(&sc->scan_timeout, 5*hz, wpi_scan_timeout, sc);

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);

	return 0;

fail:	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END_ERR, __func__);

	return error;
}

static int
wpi_auth(struct wpi_softc *sc, struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_node *ni = vap->iv_bss;
	struct ieee80211_channel *c = ni->ni_chan;
	int error;

	WPI_RXON_LOCK(sc);

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	/* Update adapter configuration. */
	sc->rxon.associd = 0;
	sc->rxon.filter &= ~htole32(WPI_FILTER_BSS);
	IEEE80211_ADDR_COPY(sc->rxon.bssid, ni->ni_bssid);
	sc->rxon.chan = ieee80211_chan2ieee(ic, c);
	sc->rxon.flags = htole32(WPI_RXON_TSF | WPI_RXON_CTS_TO_SELF);
	if (IEEE80211_IS_CHAN_2GHZ(c))
		sc->rxon.flags |= htole32(WPI_RXON_AUTO | WPI_RXON_24GHZ);
	if (ic->ic_flags & IEEE80211_F_SHSLOT)
		sc->rxon.flags |= htole32(WPI_RXON_SHSLOT);
	if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
		sc->rxon.flags |= htole32(WPI_RXON_SHPREAMBLE);
	if (IEEE80211_IS_CHAN_A(c)) {
		sc->rxon.cck_mask  = 0;
		sc->rxon.ofdm_mask = 0x15;
	} else if (IEEE80211_IS_CHAN_B(c)) {
		sc->rxon.cck_mask  = 0x03;
		sc->rxon.ofdm_mask = 0;
	} else {
		/* Assume 802.11b/g. */
		sc->rxon.cck_mask  = 0x0f;
		sc->rxon.ofdm_mask = 0x15;
	}

	DPRINTF(sc, WPI_DEBUG_STATE, "rxon chan %d flags %x cck %x ofdm %x\n",
	    sc->rxon.chan, sc->rxon.flags, sc->rxon.cck_mask,
	    sc->rxon.ofdm_mask);

	if ((error = wpi_send_rxon(sc, 0, 1)) != 0) {
		device_printf(sc->sc_dev, "%s: could not send RXON\n",
		    __func__);
	}

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);

	WPI_RXON_UNLOCK(sc);

	return error;
}

static int
wpi_config_beacon(struct wpi_vap *wvp)
{
	struct ieee80211vap *vap = &wvp->wv_vap;
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_beacon_offsets *bo = &vap->iv_bcn_off;
	struct wpi_buf *bcn = &wvp->wv_bcbuf;
	struct wpi_softc *sc = ic->ic_softc;
	struct wpi_cmd_beacon *cmd = (struct wpi_cmd_beacon *)&bcn->data;
	struct ieee80211_tim_ie *tie;
	struct mbuf *m;
	uint8_t *ptr;
	int error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	WPI_VAP_LOCK_ASSERT(wvp);

	cmd->len = htole16(bcn->m->m_pkthdr.len);
	cmd->plcp = (ic->ic_curmode == IEEE80211_MODE_11A) ?
	    wpi_ridx_to_plcp[WPI_RIDX_OFDM6] : wpi_ridx_to_plcp[WPI_RIDX_CCK1];

	/* XXX seems to be unused */
	if (*(bo->bo_tim) == IEEE80211_ELEMID_TIM) {
		tie = (struct ieee80211_tim_ie *) bo->bo_tim;
		ptr = mtod(bcn->m, uint8_t *);

		cmd->tim = htole16(bo->bo_tim - ptr);
		cmd->timsz = tie->tim_len;
	}

	/* Necessary for recursion in ieee80211_beacon_update(). */
	m = bcn->m;
	bcn->m = m_dup(m, M_NOWAIT);
	if (bcn->m == NULL) {
		device_printf(sc->sc_dev,
		    "%s: could not copy beacon frame\n", __func__);
		error = ENOMEM;
		goto end;
	}

	if ((error = wpi_cmd2(sc, bcn)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not update beacon frame, error %d", __func__,
		    error);
		m_freem(bcn->m);
	}

	/* Restore mbuf. */
end:	bcn->m = m;

	return error;
}

static int
wpi_setup_beacon(struct wpi_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct wpi_vap *wvp = WPI_VAP(vap);
	struct wpi_buf *bcn = &wvp->wv_bcbuf;
	struct mbuf *m;
	int error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	if (ni->ni_chan == IEEE80211_CHAN_ANYC)
		return EINVAL;

	m = ieee80211_beacon_alloc(ni);
	if (m == NULL) {
		device_printf(sc->sc_dev,
		    "%s: could not allocate beacon frame\n", __func__);
		return ENOMEM;
	}

	WPI_VAP_LOCK(wvp);
	if (bcn->m != NULL)
		m_freem(bcn->m);

	bcn->m = m;

	error = wpi_config_beacon(wvp);
	WPI_VAP_UNLOCK(wvp);

	return error;
}

static void
wpi_update_beacon(struct ieee80211vap *vap, int item)
{
	struct wpi_softc *sc = vap->iv_ic->ic_softc;
	struct wpi_vap *wvp = WPI_VAP(vap);
	struct wpi_buf *bcn = &wvp->wv_bcbuf;
	struct ieee80211_beacon_offsets *bo = &vap->iv_bcn_off;
	struct ieee80211_node *ni = vap->iv_bss;
	int mcast = 0;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	WPI_VAP_LOCK(wvp);
	if (bcn->m == NULL) {
		bcn->m = ieee80211_beacon_alloc(ni);
		if (bcn->m == NULL) {
			device_printf(sc->sc_dev,
			    "%s: could not allocate beacon frame\n", __func__);

			DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END_ERR,
			    __func__);

			WPI_VAP_UNLOCK(wvp);
			return;
		}
	}
	WPI_VAP_UNLOCK(wvp);

	if (item == IEEE80211_BEACON_TIM)
		mcast = 1;	/* TODO */

	setbit(bo->bo_flags, item);
	ieee80211_beacon_update(ni, bcn->m, mcast);

	WPI_VAP_LOCK(wvp);
	wpi_config_beacon(wvp);
	WPI_VAP_UNLOCK(wvp);

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);
}

static void
wpi_newassoc(struct ieee80211_node *ni, int isnew)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct wpi_softc *sc = ni->ni_ic->ic_softc;
	struct wpi_node *wn = WPI_NODE(ni);
	int error;

	WPI_NT_LOCK(sc);

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	if (vap->iv_opmode != IEEE80211_M_STA && wn->id == WPI_ID_UNDEFINED) {
		if ((error = wpi_add_ibss_node(sc, ni)) != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not add IBSS node, error %d\n",
			    __func__, error);
		}
	}
	WPI_NT_UNLOCK(sc);
}

static int
wpi_run(struct wpi_softc *sc, struct ieee80211vap *vap)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct ieee80211_node *ni = vap->iv_bss;
	struct ieee80211_channel *c = ni->ni_chan;
	int error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	if (vap->iv_opmode == IEEE80211_M_MONITOR) {
		/* Link LED blinks while monitoring. */
		wpi_set_led(sc, WPI_LED_LINK, 5, 5);
		return 0;
	}

	/* XXX kernel panic workaround */
	if (c == IEEE80211_CHAN_ANYC) {
		device_printf(sc->sc_dev, "%s: incomplete configuration\n",
		    __func__);
		return EINVAL;
	}

	if ((error = wpi_set_timing(sc, ni)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not set timing, error %d\n", __func__, error);
		return error;
	}

	/* Update adapter configuration. */
	WPI_RXON_LOCK(sc);
	IEEE80211_ADDR_COPY(sc->rxon.bssid, ni->ni_bssid);
	sc->rxon.associd = htole16(IEEE80211_NODE_AID(ni));
	sc->rxon.chan = ieee80211_chan2ieee(ic, c);
	sc->rxon.flags = htole32(WPI_RXON_TSF | WPI_RXON_CTS_TO_SELF);
	if (IEEE80211_IS_CHAN_2GHZ(c))
		sc->rxon.flags |= htole32(WPI_RXON_AUTO | WPI_RXON_24GHZ);
	if (ic->ic_flags & IEEE80211_F_SHSLOT)
		sc->rxon.flags |= htole32(WPI_RXON_SHSLOT);
	if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
		sc->rxon.flags |= htole32(WPI_RXON_SHPREAMBLE);
	if (IEEE80211_IS_CHAN_A(c)) {
		sc->rxon.cck_mask  = 0;
		sc->rxon.ofdm_mask = 0x15;
	} else if (IEEE80211_IS_CHAN_B(c)) {
		sc->rxon.cck_mask  = 0x03;
		sc->rxon.ofdm_mask = 0;
	} else {
		/* Assume 802.11b/g. */
		sc->rxon.cck_mask  = 0x0f;
		sc->rxon.ofdm_mask = 0x15;
	}
	sc->rxon.filter |= htole32(WPI_FILTER_BSS);

	DPRINTF(sc, WPI_DEBUG_STATE, "rxon chan %d flags %x\n",
	    sc->rxon.chan, sc->rxon.flags);

	if ((error = wpi_send_rxon(sc, 0, 1)) != 0) {
		device_printf(sc->sc_dev, "%s: could not send RXON\n",
		    __func__);
		return error;
	}

	/* Start periodic calibration timer. */
	callout_reset(&sc->calib_to, 60*hz, wpi_calib_timeout, sc);

	WPI_RXON_UNLOCK(sc);

	if (vap->iv_opmode == IEEE80211_M_IBSS ||
	    vap->iv_opmode == IEEE80211_M_HOSTAP) {
		if ((error = wpi_setup_beacon(sc, ni)) != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not setup beacon, error %d\n", __func__,
			    error);
			return error;
		}
	}

	if (vap->iv_opmode == IEEE80211_M_STA) {
		/* Add BSS node. */
		WPI_NT_LOCK(sc);
		error = wpi_add_sta_node(sc, ni);
		WPI_NT_UNLOCK(sc);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not add BSS node, error %d\n", __func__,
			    error);
			return error;
		}
	}

	/* Link LED always on while associated. */
	wpi_set_led(sc, WPI_LED_LINK, 0, 1);

	/* Enable power-saving mode if requested by user. */
	if ((vap->iv_flags & IEEE80211_F_PMGTON) &&
	    vap->iv_opmode != IEEE80211_M_IBSS)
		(void)wpi_set_pslevel(sc, 0, 3, 1);

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);

	return 0;
}

static int
wpi_load_key(struct ieee80211_node *ni, const struct ieee80211_key *k)
{
	const struct ieee80211_cipher *cip = k->wk_cipher;
	struct ieee80211vap *vap = ni->ni_vap;
	struct wpi_softc *sc = ni->ni_ic->ic_softc;
	struct wpi_node *wn = WPI_NODE(ni);
	struct wpi_node_info node;
	uint16_t kflags;
	int error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	if (wpi_check_node_entry(sc, wn->id) == 0) {
		device_printf(sc->sc_dev, "%s: node does not exist\n",
		    __func__);
		return 0;
	}

	switch (cip->ic_cipher) {
	case IEEE80211_CIPHER_AES_CCM:
		kflags = WPI_KFLAG_CCMP;
		break;

	default:
		device_printf(sc->sc_dev, "%s: unknown cipher %d\n", __func__,
		    cip->ic_cipher);
		return 0;
	}

	kflags |= WPI_KFLAG_KID(k->wk_keyix);
	if (k->wk_flags & IEEE80211_KEY_GROUP)
		kflags |= WPI_KFLAG_MULTICAST;

	memset(&node, 0, sizeof node);
	node.id = wn->id;
	node.control = WPI_NODE_UPDATE;
	node.flags = WPI_FLAG_KEY_SET;
	node.kflags = htole16(kflags);
	memcpy(node.key, k->wk_key, k->wk_keylen);
again:
	DPRINTF(sc, WPI_DEBUG_KEY,
	    "%s: setting %s key id %d for node %d (%s)\n", __func__,
	    (kflags & WPI_KFLAG_MULTICAST) ? "group" : "ucast", k->wk_keyix,
	    node.id, ether_sprintf(ni->ni_macaddr));

	error = wpi_cmd(sc, WPI_CMD_ADD_NODE, &node, sizeof node, 1);
	if (error != 0) {
		device_printf(sc->sc_dev, "can't update node info, error %d\n",
		    error);
		return !error;
	}

	if (!(kflags & WPI_KFLAG_MULTICAST) && &vap->iv_nw_keys[0] <= k &&
	    k < &vap->iv_nw_keys[IEEE80211_WEP_NKID]) {
		kflags |= WPI_KFLAG_MULTICAST;
		node.kflags = htole16(kflags);

		goto again;
	}

	return 1;
}

static void
wpi_load_key_cb(void *arg, struct ieee80211_node *ni)
{
	const struct ieee80211_key *k = arg;
	struct ieee80211vap *vap = ni->ni_vap;
	struct wpi_softc *sc = ni->ni_ic->ic_softc;
	struct wpi_node *wn = WPI_NODE(ni);
	int error;

	if (vap->iv_bss == ni && wn->id == WPI_ID_UNDEFINED)
		return;

	WPI_NT_LOCK(sc);
	error = wpi_load_key(ni, k);
	WPI_NT_UNLOCK(sc);

	if (error == 0) {
		device_printf(sc->sc_dev, "%s: error while setting key\n",
		    __func__);
	}
}

static int
wpi_set_global_keys(struct ieee80211_node *ni)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_key *wk = &vap->iv_nw_keys[0];
	int error = 1;

	for (; wk < &vap->iv_nw_keys[IEEE80211_WEP_NKID] && error; wk++)
		if (wk->wk_keyix != IEEE80211_KEYIX_NONE)
			error = wpi_load_key(ni, wk);

	return !error;
}

static int
wpi_del_key(struct ieee80211_node *ni, const struct ieee80211_key *k)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct wpi_softc *sc = ni->ni_ic->ic_softc;
	struct wpi_node *wn = WPI_NODE(ni);
	struct wpi_node_info node;
	uint16_t kflags;
	int error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	if (wpi_check_node_entry(sc, wn->id) == 0) {
		DPRINTF(sc, WPI_DEBUG_KEY, "%s: node was removed\n", __func__);
		return 1;	/* Nothing to do. */
	}

	kflags = WPI_KFLAG_KID(k->wk_keyix);
	if (k->wk_flags & IEEE80211_KEY_GROUP)
		kflags |= WPI_KFLAG_MULTICAST;

	memset(&node, 0, sizeof node);
	node.id = wn->id;
	node.control = WPI_NODE_UPDATE;
	node.flags = WPI_FLAG_KEY_SET;
	node.kflags = htole16(kflags);
again:
	DPRINTF(sc, WPI_DEBUG_KEY, "%s: deleting %s key %d for node %d (%s)\n",
	    __func__, (kflags & WPI_KFLAG_MULTICAST) ? "group" : "ucast",
	    k->wk_keyix, node.id, ether_sprintf(ni->ni_macaddr));

	error = wpi_cmd(sc, WPI_CMD_ADD_NODE, &node, sizeof node, 1);
	if (error != 0) {
		device_printf(sc->sc_dev, "can't update node info, error %d\n",
		    error);
		return !error;
	}

	if (!(kflags & WPI_KFLAG_MULTICAST) && &vap->iv_nw_keys[0] <= k &&
	    k < &vap->iv_nw_keys[IEEE80211_WEP_NKID]) {
		kflags |= WPI_KFLAG_MULTICAST;
		node.kflags = htole16(kflags);

		goto again;
	}

	return 1;
}

static void
wpi_del_key_cb(void *arg, struct ieee80211_node *ni)
{
	const struct ieee80211_key *k = arg;
	struct ieee80211vap *vap = ni->ni_vap;
	struct wpi_softc *sc = ni->ni_ic->ic_softc;
	struct wpi_node *wn = WPI_NODE(ni);
	int error;

	if (vap->iv_bss == ni && wn->id == WPI_ID_UNDEFINED)
		return;

	WPI_NT_LOCK(sc);
	error = wpi_del_key(ni, k);
	WPI_NT_UNLOCK(sc);

	if (error == 0) {
		device_printf(sc->sc_dev, "%s: error while deleting key\n",
		    __func__);
	}
}

static int
wpi_process_key(struct ieee80211vap *vap, const struct ieee80211_key *k,
    int set)
{
	struct ieee80211com *ic = vap->iv_ic;
	struct wpi_softc *sc = ic->ic_softc;
	struct wpi_vap *wvp = WPI_VAP(vap);
	struct ieee80211_node *ni;
	int error, ni_ref = 0;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	if (k->wk_flags & IEEE80211_KEY_SWCRYPT) {
		/* Not for us. */
		return 1;
	}

	if (!(k->wk_flags & IEEE80211_KEY_RECV)) {
		/* XMIT keys are handled in wpi_tx_data(). */
		return 1;
	}

	/* Handle group keys. */
	if (&vap->iv_nw_keys[0] <= k &&
	    k < &vap->iv_nw_keys[IEEE80211_WEP_NKID]) {
		WPI_NT_LOCK(sc);
		if (set)
			wvp->wv_gtk |= WPI_VAP_KEY(k->wk_keyix);
		else
			wvp->wv_gtk &= ~WPI_VAP_KEY(k->wk_keyix);
		WPI_NT_UNLOCK(sc);

		if (vap->iv_state == IEEE80211_S_RUN) {
			ieee80211_iterate_nodes(&ic->ic_sta,
			    set ? wpi_load_key_cb : wpi_del_key_cb,
			    __DECONST(void *, k));
		}

		return 1;
	}

	switch (vap->iv_opmode) {
	case IEEE80211_M_STA:
		ni = vap->iv_bss;
		break;

	case IEEE80211_M_IBSS:
	case IEEE80211_M_AHDEMO:
	case IEEE80211_M_HOSTAP:
		ni = ieee80211_find_vap_node(&ic->ic_sta, vap, k->wk_macaddr);
		if (ni == NULL)
			return 0;	/* should not happen */

		ni_ref = 1;
		break;

	default:
		device_printf(sc->sc_dev, "%s: unknown opmode %d\n", __func__,
		    vap->iv_opmode);
		return 0;
	}

	WPI_NT_LOCK(sc);
	if (set)
		error = wpi_load_key(ni, k);
	else
		error = wpi_del_key(ni, k);
	WPI_NT_UNLOCK(sc);

	if (ni_ref)
		ieee80211_node_decref(ni);

	return error;
}

static int
wpi_key_set(struct ieee80211vap *vap, const struct ieee80211_key *k)
{
	return wpi_process_key(vap, k, 1);
}

static int
wpi_key_delete(struct ieee80211vap *vap, const struct ieee80211_key *k)
{
	return wpi_process_key(vap, k, 0);
}

/*
 * This function is called after the runtime firmware notifies us of its
 * readiness (called in a process context).
 */
static int
wpi_post_alive(struct wpi_softc *sc)
{
	int ntries, error;

	/* Check (again) that the radio is not disabled. */
	if ((error = wpi_nic_lock(sc)) != 0)
		return error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	/* NB: Runtime firmware must be up and running. */
	if (!(wpi_prph_read(sc, WPI_APMG_RFKILL) & 1)) {
		device_printf(sc->sc_dev,
		    "RF switch: radio disabled (%s)\n", __func__);
		wpi_nic_unlock(sc);
		return EPERM;   /* :-) */
	}
	wpi_nic_unlock(sc);

	/* Wait for thermal sensor to calibrate. */
	for (ntries = 0; ntries < 1000; ntries++) {
		if ((sc->temp = (int)WPI_READ(sc, WPI_UCODE_GP2)) != 0)
			break;
		DELAY(10);
	}

	if (ntries == 1000) {
		device_printf(sc->sc_dev,
		    "timeout waiting for thermal sensor calibration\n");
		return ETIMEDOUT;
	}

	DPRINTF(sc, WPI_DEBUG_TEMP, "temperature %d\n", sc->temp);
	return 0;
}

/*
 * The firmware boot code is small and is intended to be copied directly into
 * the NIC internal memory (no DMA transfer).
 */
static int
wpi_load_bootcode(struct wpi_softc *sc, const uint8_t *ucode, uint32_t size)
{
	int error, ntries;

	DPRINTF(sc, WPI_DEBUG_HW, "Loading microcode size 0x%x\n", size);

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
		uint32_t status = WPI_READ(sc, WPI_FH_TX_STATUS);
		DPRINTF(sc, WPI_DEBUG_HW,
		    "firmware status=0x%x, val=0x%x, result=0x%x\n", status,
		    WPI_FH_TX_STATUS_IDLE(6),
		    status & WPI_FH_TX_STATUS_IDLE(6));
		if (status & WPI_FH_TX_STATUS_IDLE(6)) {
			DPRINTF(sc, WPI_DEBUG_HW,
			    "Status Match! - ntries = %d\n", ntries);
			break;
		}
		DELAY(10);
	}
	if (ntries == 1000) {
		device_printf(sc->sc_dev, "%s: could not load boot firmware\n",
		    __func__);
		wpi_nic_unlock(sc);
		return ETIMEDOUT;
	}

	/* Enable boot after power up. */
	wpi_prph_write(sc, WPI_BSM_WR_CTRL, WPI_BSM_WR_CTRL_START_EN);

	wpi_nic_unlock(sc);
	return 0;
}

static int
wpi_load_firmware(struct wpi_softc *sc)
{
	struct wpi_fw_info *fw = &sc->fw;
	struct wpi_dma_info *dma = &sc->fw_dma;
	int error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	/* Copy initialization sections into pre-allocated DMA-safe memory. */
	memcpy(dma->vaddr, fw->init.data, fw->init.datasz);
	bus_dmamap_sync(dma->tag, dma->map, BUS_DMASYNC_PREWRITE);
	memcpy(dma->vaddr + WPI_FW_DATA_MAXSZ, fw->init.text, fw->init.textsz);
	bus_dmamap_sync(dma->tag, dma->map, BUS_DMASYNC_PREWRITE);

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
		device_printf(sc->sc_dev, "%s: could not load boot firmware\n",
		    __func__);
		return error;
	}

	/* Now press "execute". */
	WPI_WRITE(sc, WPI_RESET, 0);

	/* Wait at most one second for first alive notification. */
	if ((error = mtx_sleep(sc, &sc->sc_mtx, PCATCH, "wpiinit", hz)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: timeout waiting for adapter to initialize, error %d\n",
		    __func__, error);
		return error;
	}

	/* Copy runtime sections into pre-allocated DMA-safe memory. */
	memcpy(dma->vaddr, fw->main.data, fw->main.datasz);
	bus_dmamap_sync(dma->tag, dma->map, BUS_DMASYNC_PREWRITE);
	memcpy(dma->vaddr + WPI_FW_DATA_MAXSZ, fw->main.text, fw->main.textsz);
	bus_dmamap_sync(dma->tag, dma->map, BUS_DMASYNC_PREWRITE);

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

static int
wpi_read_firmware(struct wpi_softc *sc)
{
	const struct firmware *fp;
	struct wpi_fw_info *fw = &sc->fw;
	const struct wpi_firmware_hdr *hdr;
	int error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	DPRINTF(sc, WPI_DEBUG_FIRMWARE,
	    "Attempting Loading Firmware from %s module\n", WPI_FW_NAME);

	WPI_UNLOCK(sc);
	fp = firmware_get(WPI_FW_NAME);
	WPI_LOCK(sc);

	if (fp == NULL) {
		device_printf(sc->sc_dev,
		    "could not load firmware image '%s'\n", WPI_FW_NAME);
		return EINVAL;
	}

	sc->fw_fp = fp;

	if (fp->datasize < sizeof (struct wpi_firmware_hdr)) {
		device_printf(sc->sc_dev,
		    "firmware file too short: %zu bytes\n", fp->datasize);
		error = EINVAL;
		goto fail;
	}

	fw->size = fp->datasize;
	fw->data = (const uint8_t *)fp->data;

	/* Extract firmware header information. */
	hdr = (const struct wpi_firmware_hdr *)fw->data;

	/*     |  RUNTIME FIRMWARE   |    INIT FIRMWARE    | BOOT FW  |
	   |HDR|<--TEXT-->|<--DATA-->|<--TEXT-->|<--DATA-->|<--TEXT-->| */

	fw->main.textsz = le32toh(hdr->rtextsz);
	fw->main.datasz = le32toh(hdr->rdatasz);
	fw->init.textsz = le32toh(hdr->itextsz);
	fw->init.datasz = le32toh(hdr->idatasz);
	fw->boot.textsz = le32toh(hdr->btextsz);
	fw->boot.datasz = 0;

	/* Sanity-check firmware header. */
	if (fw->main.textsz > WPI_FW_TEXT_MAXSZ ||
	    fw->main.datasz > WPI_FW_DATA_MAXSZ ||
	    fw->init.textsz > WPI_FW_TEXT_MAXSZ ||
	    fw->init.datasz > WPI_FW_DATA_MAXSZ ||
	    fw->boot.textsz > WPI_FW_BOOT_TEXT_MAXSZ ||
	    (fw->boot.textsz & 3) != 0) {
		device_printf(sc->sc_dev, "invalid firmware header\n");
		error = EINVAL;
		goto fail;
	}

	/* Check that all firmware sections fit. */
	if (fw->size < sizeof (*hdr) + fw->main.textsz + fw->main.datasz +
	    fw->init.textsz + fw->init.datasz + fw->boot.textsz) {
		device_printf(sc->sc_dev,
		    "firmware file too short: %zu bytes\n", fw->size);
		error = EINVAL;
		goto fail;
	}

	/* Get pointers to firmware sections. */
	fw->main.text = (const uint8_t *)(hdr + 1);
	fw->main.data = fw->main.text + fw->main.textsz;
	fw->init.text = fw->main.data + fw->main.datasz;
	fw->init.data = fw->init.text + fw->init.textsz;
	fw->boot.text = fw->init.data + fw->init.datasz;

	DPRINTF(sc, WPI_DEBUG_FIRMWARE,
	    "Firmware Version: Major %d, Minor %d, Driver %d, \n"
	    "runtime (text: %u, data: %u) init (text: %u, data %u) "
	    "boot (text %u)\n", hdr->major, hdr->minor, le32toh(hdr->driver),
	    fw->main.textsz, fw->main.datasz,
	    fw->init.textsz, fw->init.datasz, fw->boot.textsz);

	DPRINTF(sc, WPI_DEBUG_FIRMWARE, "fw->main.text %p\n", fw->main.text);
	DPRINTF(sc, WPI_DEBUG_FIRMWARE, "fw->main.data %p\n", fw->main.data);
	DPRINTF(sc, WPI_DEBUG_FIRMWARE, "fw->init.text %p\n", fw->init.text);
	DPRINTF(sc, WPI_DEBUG_FIRMWARE, "fw->init.data %p\n", fw->init.data);
	DPRINTF(sc, WPI_DEBUG_FIRMWARE, "fw->boot.text %p\n", fw->boot.text);

	return 0;

fail:	wpi_unload_firmware(sc);
	return error;
}

/**
 * Free the referenced firmware image
 */
static void
wpi_unload_firmware(struct wpi_softc *sc)
{
	if (sc->fw_fp != NULL) {
		firmware_put(sc->fw_fp, FIRMWARE_UNLOAD);
		sc->fw_fp = NULL;
	}
}

static int
wpi_clock_wait(struct wpi_softc *sc)
{
	int ntries;

	/* Set "initialization complete" bit. */
	WPI_SETBITS(sc, WPI_GP_CNTRL, WPI_GP_CNTRL_INIT_DONE);

	/* Wait for clock stabilization. */
	for (ntries = 0; ntries < 2500; ntries++) {
		if (WPI_READ(sc, WPI_GP_CNTRL) & WPI_GP_CNTRL_MAC_CLOCK_READY)
			return 0;
		DELAY(100);
	}
	device_printf(sc->sc_dev,
	    "%s: timeout waiting for clock stabilization\n", __func__);

	return ETIMEDOUT;
}

static int
wpi_apm_init(struct wpi_softc *sc)
{
	uint32_t reg;
	int error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	/* Disable L0s exit timer (NMI bug workaround). */
	WPI_SETBITS(sc, WPI_GIO_CHICKEN, WPI_GIO_CHICKEN_DIS_L0S_TIMER);
	/* Don't wait for ICH L0s (ICH bug workaround). */
	WPI_SETBITS(sc, WPI_GIO_CHICKEN, WPI_GIO_CHICKEN_L1A_NO_L0S_RX);

	/* Set FH wait threshold to max (HW bug under stress workaround). */
	WPI_SETBITS(sc, WPI_DBG_HPET_MEM, 0xffff0000);

	/* Retrieve PCIe Active State Power Management (ASPM). */
	reg = pci_read_config(sc->sc_dev, sc->sc_cap_off + PCIER_LINK_CTL, 1);
	/* Workaround for HW instability in PCIe L0->L0s->L1 transition. */
	if (reg & PCIEM_LINK_CTL_ASPMC_L1)	/* L1 Entry enabled. */
		WPI_SETBITS(sc, WPI_GIO, WPI_GIO_L0S_ENA);
	else
		WPI_CLRBITS(sc, WPI_GIO, WPI_GIO_L0S_ENA);

	WPI_SETBITS(sc, WPI_ANA_PLL, WPI_ANA_PLL_INIT);

	/* Wait for clock stabilization before accessing prph. */
	if ((error = wpi_clock_wait(sc)) != 0)
		return error;

	if ((error = wpi_nic_lock(sc)) != 0)
		return error;
	/* Cleanup. */
	wpi_prph_write(sc, WPI_APMG_CLK_DIS, 0x00000400);
	wpi_prph_clrbits(sc, WPI_APMG_PS, 0x00000200);

	/* Enable DMA and BSM (Bootstrap State Machine). */
	wpi_prph_write(sc, WPI_APMG_CLK_EN,
	    WPI_APMG_CLK_CTRL_DMA_CLK_RQT | WPI_APMG_CLK_CTRL_BSM_CLK_RQT);
	DELAY(20);
	/* Disable L1-Active. */
	wpi_prph_setbits(sc, WPI_APMG_PCI_STT, WPI_APMG_PCI_STT_L1A_DIS);
	wpi_nic_unlock(sc);

	return 0;
}

static void
wpi_apm_stop_master(struct wpi_softc *sc)
{
	int ntries;

	/* Stop busmaster DMA activity. */
	WPI_SETBITS(sc, WPI_RESET, WPI_RESET_STOP_MASTER);

	if ((WPI_READ(sc, WPI_GP_CNTRL) & WPI_GP_CNTRL_PS_MASK) ==
	    WPI_GP_CNTRL_MAC_PS)
		return; /* Already asleep. */

	for (ntries = 0; ntries < 100; ntries++) {
		if (WPI_READ(sc, WPI_RESET) & WPI_RESET_MASTER_DISABLED)
			return;
		DELAY(10);
	}
	device_printf(sc->sc_dev, "%s: timeout waiting for master\n",
	    __func__);
}

static void
wpi_apm_stop(struct wpi_softc *sc)
{
	wpi_apm_stop_master(sc);

	/* Reset the entire device. */
	WPI_SETBITS(sc, WPI_RESET, WPI_RESET_SW);
	DELAY(10);
	/* Clear "initialization complete" bit. */
	WPI_CLRBITS(sc, WPI_GP_CNTRL, WPI_GP_CNTRL_INIT_DONE);
}

static void
wpi_nic_config(struct wpi_softc *sc)
{
	uint32_t rev;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	/* voodoo from the Linux "driver".. */
	rev = pci_read_config(sc->sc_dev, PCIR_REVID, 1);
	if ((rev & 0xc0) == 0x40)
		WPI_SETBITS(sc, WPI_HW_IF_CONFIG, WPI_HW_IF_CONFIG_ALM_MB);
	else if (!(rev & 0x80))
		WPI_SETBITS(sc, WPI_HW_IF_CONFIG, WPI_HW_IF_CONFIG_ALM_MM);

	if (sc->cap == 0x80)
		WPI_SETBITS(sc, WPI_HW_IF_CONFIG, WPI_HW_IF_CONFIG_SKU_MRC);

	if ((sc->rev & 0xf0) == 0xd0)
		WPI_SETBITS(sc, WPI_HW_IF_CONFIG, WPI_HW_IF_CONFIG_REV_D);
	else
		WPI_CLRBITS(sc, WPI_HW_IF_CONFIG, WPI_HW_IF_CONFIG_REV_D);

	if (sc->type > 1)
		WPI_SETBITS(sc, WPI_HW_IF_CONFIG, WPI_HW_IF_CONFIG_TYPE_B);
}

static int
wpi_hw_init(struct wpi_softc *sc)
{
	uint8_t chnl;
	int ntries, error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	/* Clear pending interrupts. */
	WPI_WRITE(sc, WPI_INT, 0xffffffff);

	if ((error = wpi_apm_init(sc)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not power ON adapter, error %d\n", __func__,
		    error);
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
		device_printf(sc->sc_dev, "timeout selecting power source\n");
		return ETIMEDOUT;
	}

	/* Perform adapter initialization. */
	wpi_nic_config(sc);

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
	    WPI_FH_RX_CONFIG_IRQ_TIMEOUT(1));
	(void)WPI_READ(sc, WPI_FH_RSSR_TBL);	/* barrier */
	wpi_nic_unlock(sc);
	WPI_WRITE(sc, WPI_FH_RX_WPTR, (WPI_RX_RING_COUNT - 1) & ~7);

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
	WPI_WRITE(sc, WPI_INT_MASK, WPI_INT_MASK_DEF);

	/* _Really_ make sure "radio off" bit is cleared! */
	WPI_WRITE(sc, WPI_UCODE_GP1_CLR, WPI_UCODE_GP1_RFKILL);
	WPI_WRITE(sc, WPI_UCODE_GP1_CLR, WPI_UCODE_GP1_RFKILL);

	if ((error = wpi_load_firmware(sc)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not load firmware, error %d\n", __func__,
		    error);
		return error;
	}
	/* Wait at most one second for firmware alive notification. */
	if ((error = mtx_sleep(sc, &sc->sc_mtx, PCATCH, "wpiinit", hz)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: timeout waiting for adapter to initialize, error %d\n",
		    __func__, error);
		return error;
	}

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);

	/* Do post-firmware initialization. */
	return wpi_post_alive(sc);
}

static void
wpi_hw_stop(struct wpi_softc *sc)
{
	uint8_t chnl, qid;
	int ntries;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	if (WPI_READ(sc, WPI_UCODE_GP1) & WPI_UCODE_GP1_MAC_SLEEP)
		wpi_nic_lock(sc);

	WPI_WRITE(sc, WPI_RESET, WPI_RESET_NEVO);

	/* Disable interrupts. */
	WPI_WRITE(sc, WPI_INT_MASK, 0);
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
			for (ntries = 0; ntries < 200; ntries++) {
				if (WPI_READ(sc, WPI_FH_TX_STATUS) &
				    WPI_FH_TX_STATUS_IDLE(chnl))
					break;
				DELAY(10);
			}
		}
		wpi_nic_unlock(sc);
	}

	/* Stop RX ring. */
	wpi_reset_rx_ring(sc);

	/* Reset all TX rings. */
	for (qid = 0; qid < WPI_DRV_NTXQUEUES; qid++)
		wpi_reset_tx_ring(sc, &sc->txq[qid]);

	if (wpi_nic_lock(sc) == 0) {
		wpi_prph_write(sc, WPI_APMG_CLK_DIS,
		    WPI_APMG_CLK_CTRL_DMA_CLK_RQT);
		wpi_nic_unlock(sc);
	}
	DELAY(5);
	/* Power OFF adapter. */
	wpi_apm_stop(sc);
}

static void
wpi_radio_on(void *arg0, int pending)
{
	struct wpi_softc *sc = arg0;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);

	device_printf(sc->sc_dev, "RF switch: radio enabled\n");

	WPI_LOCK(sc);
	callout_stop(&sc->watchdog_rfkill);
	WPI_UNLOCK(sc);

	if (vap != NULL)
		ieee80211_init(vap);
}

static void
wpi_radio_off(void *arg0, int pending)
{
	struct wpi_softc *sc = arg0;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);

	device_printf(sc->sc_dev, "RF switch: radio disabled\n");

	ieee80211_notify_radio(ic, 0);
	wpi_stop(sc);
	if (vap != NULL)
		ieee80211_stop(vap);

	WPI_LOCK(sc);
	callout_reset(&sc->watchdog_rfkill, hz, wpi_watchdog_rfkill, sc);
	WPI_UNLOCK(sc);
}

static int
wpi_init(struct wpi_softc *sc)
{
	int error = 0;

	WPI_LOCK(sc);

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_BEGIN, __func__);

	if (sc->sc_running != 0)
		goto end;

	/* Check that the radio is not disabled by hardware switch. */
	if (!(WPI_READ(sc, WPI_GP_CNTRL) & WPI_GP_CNTRL_RFKILL)) {
		device_printf(sc->sc_dev,
		    "RF switch: radio disabled (%s)\n", __func__);
		callout_reset(&sc->watchdog_rfkill, hz, wpi_watchdog_rfkill,
		    sc);
		error = EINPROGRESS;
		goto end;
	}

	/* Read firmware images from the filesystem. */
	if ((error = wpi_read_firmware(sc)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not read firmware, error %d\n", __func__,
		    error);
		goto end;
	}

	sc->sc_running = 1;

	/* Initialize hardware and upload firmware. */
	error = wpi_hw_init(sc);
	wpi_unload_firmware(sc);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not initialize hardware, error %d\n", __func__,
		    error);
		goto fail;
	}

	/* Configure adapter now that it is ready. */
	if ((error = wpi_config(sc)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not configure device, error %d\n", __func__,
		    error);
		goto fail;
	}

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END, __func__);

	WPI_UNLOCK(sc);

	return 0;

fail:	wpi_stop_locked(sc);

end:	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_END_ERR, __func__);
	WPI_UNLOCK(sc);

	return error;
}

static void
wpi_stop_locked(struct wpi_softc *sc)
{

	WPI_LOCK_ASSERT(sc);

	if (sc->sc_running == 0)
		return;

	WPI_TX_LOCK(sc);
	WPI_TXQ_LOCK(sc);
	sc->sc_running = 0;
	WPI_TXQ_UNLOCK(sc);
	WPI_TX_UNLOCK(sc);

	WPI_TXQ_STATE_LOCK(sc);
	callout_stop(&sc->tx_timeout);
	WPI_TXQ_STATE_UNLOCK(sc);

	WPI_RXON_LOCK(sc);
	callout_stop(&sc->scan_timeout);
	callout_stop(&sc->calib_to);
	WPI_RXON_UNLOCK(sc);

	/* Power OFF hardware. */
	wpi_hw_stop(sc);
}

static void
wpi_stop(struct wpi_softc *sc)
{
	WPI_LOCK(sc);
	wpi_stop_locked(sc);
	WPI_UNLOCK(sc);
}

/*
 * Callback from net80211 to start a scan.
 */
static void
wpi_scan_start(struct ieee80211com *ic)
{
	struct wpi_softc *sc = ic->ic_softc;

	wpi_set_led(sc, WPI_LED_LINK, 20, 2);
}

/*
 * Callback from net80211 to terminate a scan.
 */
static void
wpi_scan_end(struct ieee80211com *ic)
{
	struct wpi_softc *sc = ic->ic_softc;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);

	if (vap->iv_state == IEEE80211_S_RUN)
		wpi_set_led(sc, WPI_LED_LINK, 0, 1);
}

/**
 * Called by the net80211 framework to indicate to the driver
 * that the channel should be changed
 */
static void
wpi_set_channel(struct ieee80211com *ic)
{
	const struct ieee80211_channel *c = ic->ic_curchan;
	struct wpi_softc *sc = ic->ic_softc;
	int error;

	DPRINTF(sc, WPI_DEBUG_TRACE, TRACE_STR_DOING, __func__);

	WPI_LOCK(sc);
	sc->sc_rxtap.wr_chan_freq = htole16(c->ic_freq);
	sc->sc_rxtap.wr_chan_flags = htole16(c->ic_flags);
	WPI_UNLOCK(sc);
	WPI_TX_LOCK(sc);
	sc->sc_txtap.wt_chan_freq = htole16(c->ic_freq);
	sc->sc_txtap.wt_chan_flags = htole16(c->ic_flags);
	WPI_TX_UNLOCK(sc);

	/*
	 * Only need to set the channel in Monitor mode. AP scanning and auth
	 * are already taken care of by their respective firmware commands.
	 */
	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		WPI_RXON_LOCK(sc);
		sc->rxon.chan = ieee80211_chan2ieee(ic, c);
		if (IEEE80211_IS_CHAN_2GHZ(c)) {
			sc->rxon.flags |= htole32(WPI_RXON_AUTO |
			    WPI_RXON_24GHZ);
		} else {
			sc->rxon.flags &= ~htole32(WPI_RXON_AUTO |
			    WPI_RXON_24GHZ);
		}
		if ((error = wpi_send_rxon(sc, 0, 1)) != 0)
			device_printf(sc->sc_dev,
			    "%s: error %d setting channel\n", __func__,
			    error);
		WPI_RXON_UNLOCK(sc);
	}
}

/**
 * Called by net80211 to indicate that we need to scan the current
 * channel. The channel is previously be set via the wpi_set_channel
 * callback.
 */
static void
wpi_scan_curchan(struct ieee80211_scan_state *ss, unsigned long maxdwell)
{
	struct ieee80211vap *vap = ss->ss_vap;
	struct ieee80211com *ic = vap->iv_ic;
	struct wpi_softc *sc = ic->ic_softc;
	int error;

	WPI_RXON_LOCK(sc);
	error = wpi_scan(sc, ic->ic_curchan);
	WPI_RXON_UNLOCK(sc);
	if (error != 0)
		ieee80211_cancel_scan(vap);
}

/**
 * Called by the net80211 framework to indicate
 * the minimum dwell time has been met, terminate the scan.
 * We don't actually terminate the scan as the firmware will notify
 * us when it's finished and we have no way to interrupt it.
 */
static void
wpi_scan_mindwell(struct ieee80211_scan_state *ss)
{
	/* NB: don't try to abort scan; wait for firmware to finish */
}
