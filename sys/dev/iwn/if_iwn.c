/*-
 * Copyright (c) 2007-2009 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2008 Benjamin Close <benjsc@FreeBSD.org>
 * Copyright (c) 2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2011 Intel Corporation
 * Copyright (c) 2013 Cedric GROSS <c.gross@kreiz-it.fr>
 * Copyright (c) 2013 Adrian Chadd <adrian@FreeBSD.org>
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
 * Driver for Intel WiFi Link 4965 and 1000/5000/6000 Series 802.11 network
 * adapters.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"
#include "opt_iwn.h"

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/rman.h>
#include <sys/endian.h>
#include <sys/firmware.h>
#include <sys/limits.h>
#include <sys/module.h>
#include <sys/priv.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/clock.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_ratectl.h>

#include <dev/iwn/if_iwnreg.h>
#include <dev/iwn/if_iwnvar.h>
#include <dev/iwn/if_iwn_devid.h>
#include <dev/iwn/if_iwn_chip_cfg.h>
#include <dev/iwn/if_iwn_debug.h>
#include <dev/iwn/if_iwn_ioctl.h>

struct iwn_ident {
	uint16_t	vendor;
	uint16_t	device;
	const char	*name;
};

static const struct iwn_ident iwn_ident_table[] = {
	{ 0x8086, IWN_DID_6x05_1, "Intel Centrino Advanced-N 6205"		},
	{ 0x8086, IWN_DID_1000_1, "Intel Centrino Wireless-N 1000"		},
	{ 0x8086, IWN_DID_1000_2, "Intel Centrino Wireless-N 1000"		},
	{ 0x8086, IWN_DID_6x05_2, "Intel Centrino Advanced-N 6205"		},
	{ 0x8086, IWN_DID_6050_1, "Intel Centrino Advanced-N + WiMAX 6250"	},
	{ 0x8086, IWN_DID_6050_2, "Intel Centrino Advanced-N + WiMAX 6250"	},
	{ 0x8086, IWN_DID_x030_1, "Intel Centrino Wireless-N 1030"		},
	{ 0x8086, IWN_DID_x030_2, "Intel Centrino Wireless-N 1030"		},
	{ 0x8086, IWN_DID_x030_3, "Intel Centrino Advanced-N 6230"		},
	{ 0x8086, IWN_DID_x030_4, "Intel Centrino Advanced-N 6230"		},
	{ 0x8086, IWN_DID_6150_1, "Intel Centrino Wireless-N + WiMAX 6150"	},
	{ 0x8086, IWN_DID_6150_2, "Intel Centrino Wireless-N + WiMAX 6150"	},
	{ 0x8086, IWN_DID_2x00_1, "Intel(R) Centrino(R) Wireless-N 2200 BGN"	},
	{ 0x8086, IWN_DID_2x00_2, "Intel(R) Centrino(R) Wireless-N 2200 BGN"	},
	/* XXX 2200D is IWN_SDID_2x00_4; there's no way to express this here! */
	{ 0x8086, IWN_DID_2x30_1, "Intel Centrino Wireless-N 2230"		},
	{ 0x8086, IWN_DID_2x30_2, "Intel Centrino Wireless-N 2230"		},
	{ 0x8086, IWN_DID_130_1, "Intel Centrino Wireless-N 130"		},
	{ 0x8086, IWN_DID_130_2, "Intel Centrino Wireless-N 130"		},
	{ 0x8086, IWN_DID_100_1, "Intel Centrino Wireless-N 100"		},
	{ 0x8086, IWN_DID_100_2, "Intel Centrino Wireless-N 100"		},
	{ 0x8086, IWN_DID_105_1, "Intel Centrino Wireless-N 105"		},
	{ 0x8086, IWN_DID_105_2, "Intel Centrino Wireless-N 105"		},
	{ 0x8086, IWN_DID_135_1, "Intel Centrino Wireless-N 135"		},
	{ 0x8086, IWN_DID_135_2, "Intel Centrino Wireless-N 135"		},
	{ 0x8086, IWN_DID_4965_1, "Intel Wireless WiFi Link 4965"		},
	{ 0x8086, IWN_DID_6x00_1, "Intel Centrino Ultimate-N 6300"		},
	{ 0x8086, IWN_DID_6x00_2, "Intel Centrino Advanced-N 6200"		},
	{ 0x8086, IWN_DID_4965_2, "Intel Wireless WiFi Link 4965"		},
	{ 0x8086, IWN_DID_4965_3, "Intel Wireless WiFi Link 4965"		},
	{ 0x8086, IWN_DID_5x00_1, "Intel WiFi Link 5100"			},
	{ 0x8086, IWN_DID_4965_4, "Intel Wireless WiFi Link 4965"		},
	{ 0x8086, IWN_DID_5x00_3, "Intel Ultimate N WiFi Link 5300"		},
	{ 0x8086, IWN_DID_5x00_4, "Intel Ultimate N WiFi Link 5300"		},
	{ 0x8086, IWN_DID_5x00_2, "Intel WiFi Link 5100"			},
	{ 0x8086, IWN_DID_6x00_3, "Intel Centrino Ultimate-N 6300"		},
	{ 0x8086, IWN_DID_6x00_4, "Intel Centrino Advanced-N 6200"		},
	{ 0x8086, IWN_DID_5x50_1, "Intel WiMAX/WiFi Link 5350"			},
	{ 0x8086, IWN_DID_5x50_2, "Intel WiMAX/WiFi Link 5350"			},
	{ 0x8086, IWN_DID_5x50_3, "Intel WiMAX/WiFi Link 5150"			},
	{ 0x8086, IWN_DID_5x50_4, "Intel WiMAX/WiFi Link 5150"			},
	{ 0x8086, IWN_DID_6035_1, "Intel Centrino Advanced 6235"		},
	{ 0x8086, IWN_DID_6035_2, "Intel Centrino Advanced 6235"		},
	{ 0, 0, NULL }
};

static int	iwn_probe(device_t);
static int	iwn_attach(device_t);
static void	iwn4965_attach(struct iwn_softc *, uint16_t);
static void	iwn5000_attach(struct iwn_softc *, uint16_t);
static int	iwn_config_specific(struct iwn_softc *, uint16_t);
static void	iwn_radiotap_attach(struct iwn_softc *);
static void	iwn_sysctlattach(struct iwn_softc *);
static struct ieee80211vap *iwn_vap_create(struct ieee80211com *,
		    const char [IFNAMSIZ], int, enum ieee80211_opmode, int,
		    const uint8_t [IEEE80211_ADDR_LEN],
		    const uint8_t [IEEE80211_ADDR_LEN]);
static void	iwn_vap_delete(struct ieee80211vap *);
static int	iwn_detach(device_t);
static int	iwn_shutdown(device_t);
static int	iwn_suspend(device_t);
static int	iwn_resume(device_t);
static int	iwn_nic_lock(struct iwn_softc *);
static int	iwn_eeprom_lock(struct iwn_softc *);
static int	iwn_init_otprom(struct iwn_softc *);
static int	iwn_read_prom_data(struct iwn_softc *, uint32_t, void *, int);
static void	iwn_dma_map_addr(void *, bus_dma_segment_t *, int, int);
static int	iwn_dma_contig_alloc(struct iwn_softc *, struct iwn_dma_info *,
		    void **, bus_size_t, bus_size_t);
static void	iwn_dma_contig_free(struct iwn_dma_info *);
static int	iwn_alloc_sched(struct iwn_softc *);
static void	iwn_free_sched(struct iwn_softc *);
static int	iwn_alloc_kw(struct iwn_softc *);
static void	iwn_free_kw(struct iwn_softc *);
static int	iwn_alloc_ict(struct iwn_softc *);
static void	iwn_free_ict(struct iwn_softc *);
static int	iwn_alloc_fwmem(struct iwn_softc *);
static void	iwn_free_fwmem(struct iwn_softc *);
static int	iwn_alloc_rx_ring(struct iwn_softc *, struct iwn_rx_ring *);
static void	iwn_reset_rx_ring(struct iwn_softc *, struct iwn_rx_ring *);
static void	iwn_free_rx_ring(struct iwn_softc *, struct iwn_rx_ring *);
static int	iwn_alloc_tx_ring(struct iwn_softc *, struct iwn_tx_ring *,
		    int);
static void	iwn_reset_tx_ring(struct iwn_softc *, struct iwn_tx_ring *);
static void	iwn_free_tx_ring(struct iwn_softc *, struct iwn_tx_ring *);
static void	iwn_check_tx_ring(struct iwn_softc *, int);
static void	iwn5000_ict_reset(struct iwn_softc *);
static int	iwn_read_eeprom(struct iwn_softc *,
		    uint8_t macaddr[IEEE80211_ADDR_LEN]);
static void	iwn4965_read_eeprom(struct iwn_softc *);
#ifdef	IWN_DEBUG
static void	iwn4965_print_power_group(struct iwn_softc *, int);
#endif
static void	iwn5000_read_eeprom(struct iwn_softc *);
static uint32_t	iwn_eeprom_channel_flags(struct iwn_eeprom_chan *);
static void	iwn_read_eeprom_band(struct iwn_softc *, int, int, int *,
		    struct ieee80211_channel[]);
static void	iwn_read_eeprom_ht40(struct iwn_softc *, int, int, int *,
		    struct ieee80211_channel[]);
static void	iwn_read_eeprom_channels(struct iwn_softc *, int, uint32_t);
static struct iwn_eeprom_chan *iwn_find_eeprom_channel(struct iwn_softc *,
		    struct ieee80211_channel *);
static void	iwn_getradiocaps(struct ieee80211com *, int, int *,
		    struct ieee80211_channel[]);
static int	iwn_setregdomain(struct ieee80211com *,
		    struct ieee80211_regdomain *, int,
		    struct ieee80211_channel[]);
static void	iwn_read_eeprom_enhinfo(struct iwn_softc *);
static struct ieee80211_node *iwn_node_alloc(struct ieee80211vap *,
		    const uint8_t mac[IEEE80211_ADDR_LEN]);
static void	iwn_newassoc(struct ieee80211_node *, int);
static int	iwn_media_change(struct ifnet *);
static int	iwn_newstate(struct ieee80211vap *, enum ieee80211_state, int);
static void	iwn_calib_timeout(void *);
static void	iwn_rx_phy(struct iwn_softc *, struct iwn_rx_desc *);
static void	iwn_rx_done(struct iwn_softc *, struct iwn_rx_desc *,
		    struct iwn_rx_data *);
static void	iwn_agg_tx_complete(struct iwn_softc *, struct iwn_tx_ring *,
		    int, int, int);
static void	iwn_rx_compressed_ba(struct iwn_softc *, struct iwn_rx_desc *);
static void	iwn5000_rx_calib_results(struct iwn_softc *,
		    struct iwn_rx_desc *);
static void	iwn_rx_statistics(struct iwn_softc *, struct iwn_rx_desc *);
static void	iwn4965_tx_done(struct iwn_softc *, struct iwn_rx_desc *,
		    struct iwn_rx_data *);
static void	iwn5000_tx_done(struct iwn_softc *, struct iwn_rx_desc *,
		    struct iwn_rx_data *);
static void	iwn_adj_ampdu_ptr(struct iwn_softc *, struct iwn_tx_ring *);
static void	iwn_tx_done(struct iwn_softc *, struct iwn_rx_desc *, int, int,
		    uint8_t);
static int	iwn_ampdu_check_bitmap(uint64_t, int, int);
static int	iwn_ampdu_index_check(struct iwn_softc *, struct iwn_tx_ring *,
		    uint64_t, int, int);
static void	iwn_ampdu_tx_done(struct iwn_softc *, int, int, int, void *);
static void	iwn_cmd_done(struct iwn_softc *, struct iwn_rx_desc *);
static void	iwn_notif_intr(struct iwn_softc *);
static void	iwn_wakeup_intr(struct iwn_softc *);
static void	iwn_rftoggle_task(void *, int);
static void	iwn_fatal_intr(struct iwn_softc *);
static void	iwn_intr(void *);
static void	iwn4965_update_sched(struct iwn_softc *, int, int, uint8_t,
		    uint16_t);
static void	iwn5000_update_sched(struct iwn_softc *, int, int, uint8_t,
		    uint16_t);
#ifdef notyet
static void	iwn5000_reset_sched(struct iwn_softc *, int, int);
#endif
static int	iwn_tx_data(struct iwn_softc *, struct mbuf *,
		    struct ieee80211_node *);
static int	iwn_tx_data_raw(struct iwn_softc *, struct mbuf *,
		    struct ieee80211_node *,
		    const struct ieee80211_bpf_params *params);
static int	iwn_tx_cmd(struct iwn_softc *, struct mbuf *,
		    struct ieee80211_node *, struct iwn_tx_ring *);
static void	iwn_xmit_task(void *arg0, int pending);
static int	iwn_raw_xmit(struct ieee80211_node *, struct mbuf *,
		    const struct ieee80211_bpf_params *);
static int	iwn_transmit(struct ieee80211com *, struct mbuf *);
static void	iwn_scan_timeout(void *);
static void	iwn_watchdog(void *);
static int	iwn_ioctl(struct ieee80211com *, u_long , void *);
static void	iwn_parent(struct ieee80211com *);
static int	iwn_cmd(struct iwn_softc *, int, const void *, int, int);
static int	iwn4965_add_node(struct iwn_softc *, struct iwn_node_info *,
		    int);
static int	iwn5000_add_node(struct iwn_softc *, struct iwn_node_info *,
		    int);
static int	iwn_set_link_quality(struct iwn_softc *,
		    struct ieee80211_node *);
static int	iwn_add_broadcast_node(struct iwn_softc *, int);
static int	iwn_updateedca(struct ieee80211com *);
static void	iwn_set_promisc(struct iwn_softc *);
static void	iwn_update_promisc(struct ieee80211com *);
static void	iwn_update_mcast(struct ieee80211com *);
static void	iwn_set_led(struct iwn_softc *, uint8_t, uint8_t, uint8_t);
static int	iwn_set_critical_temp(struct iwn_softc *);
static int	iwn_set_timing(struct iwn_softc *, struct ieee80211_node *);
static void	iwn4965_power_calibration(struct iwn_softc *, int);
static int	iwn4965_set_txpower(struct iwn_softc *, int);
static int	iwn5000_set_txpower(struct iwn_softc *, int);
static int	iwn4965_get_rssi(struct iwn_softc *, struct iwn_rx_stat *);
static int	iwn5000_get_rssi(struct iwn_softc *, struct iwn_rx_stat *);
static int	iwn_get_noise(const struct iwn_rx_general_stats *);
static int	iwn4965_get_temperature(struct iwn_softc *);
static int	iwn5000_get_temperature(struct iwn_softc *);
static int	iwn_init_sensitivity(struct iwn_softc *);
static void	iwn_collect_noise(struct iwn_softc *,
		    const struct iwn_rx_general_stats *);
static int	iwn4965_init_gains(struct iwn_softc *);
static int	iwn5000_init_gains(struct iwn_softc *);
static int	iwn4965_set_gains(struct iwn_softc *);
static int	iwn5000_set_gains(struct iwn_softc *);
static void	iwn_tune_sensitivity(struct iwn_softc *,
		    const struct iwn_rx_stats *);
static void	iwn_save_stats_counters(struct iwn_softc *,
		    const struct iwn_stats *);
static int	iwn_send_sensitivity(struct iwn_softc *);
static void	iwn_check_rx_recovery(struct iwn_softc *, struct iwn_stats *);
static int	iwn_set_pslevel(struct iwn_softc *, int, int, int);
static int	iwn_send_btcoex(struct iwn_softc *);
static int	iwn_send_advanced_btcoex(struct iwn_softc *);
static int	iwn5000_runtime_calib(struct iwn_softc *);
static int	iwn_check_bss_filter(struct iwn_softc *);
static int	iwn4965_rxon_assoc(struct iwn_softc *, int);
static int	iwn5000_rxon_assoc(struct iwn_softc *, int);
static int	iwn_send_rxon(struct iwn_softc *, int, int);
static int	iwn_config(struct iwn_softc *);
static int	iwn_scan(struct iwn_softc *, struct ieee80211vap *,
		    struct ieee80211_scan_state *, struct ieee80211_channel *);
static int	iwn_auth(struct iwn_softc *, struct ieee80211vap *vap);
static int	iwn_run(struct iwn_softc *, struct ieee80211vap *vap);
static int	iwn_ampdu_rx_start(struct ieee80211_node *,
		    struct ieee80211_rx_ampdu *, int, int, int);
static void	iwn_ampdu_rx_stop(struct ieee80211_node *,
		    struct ieee80211_rx_ampdu *);
static int	iwn_addba_request(struct ieee80211_node *,
		    struct ieee80211_tx_ampdu *, int, int, int);
static int	iwn_addba_response(struct ieee80211_node *,
		    struct ieee80211_tx_ampdu *, int, int, int);
static int	iwn_ampdu_tx_start(struct ieee80211com *,
		    struct ieee80211_node *, uint8_t);
static void	iwn_ampdu_tx_stop(struct ieee80211_node *,
		    struct ieee80211_tx_ampdu *);
static void	iwn4965_ampdu_tx_start(struct iwn_softc *,
		    struct ieee80211_node *, int, uint8_t, uint16_t);
static void	iwn4965_ampdu_tx_stop(struct iwn_softc *, int,
		    uint8_t, uint16_t);
static void	iwn5000_ampdu_tx_start(struct iwn_softc *,
		    struct ieee80211_node *, int, uint8_t, uint16_t);
static void	iwn5000_ampdu_tx_stop(struct iwn_softc *, int,
		    uint8_t, uint16_t);
static int	iwn5000_query_calibration(struct iwn_softc *);
static int	iwn5000_send_calibration(struct iwn_softc *);
static int	iwn5000_send_wimax_coex(struct iwn_softc *);
static int	iwn5000_crystal_calib(struct iwn_softc *);
static int	iwn5000_temp_offset_calib(struct iwn_softc *);
static int	iwn5000_temp_offset_calibv2(struct iwn_softc *);
static int	iwn4965_post_alive(struct iwn_softc *);
static int	iwn5000_post_alive(struct iwn_softc *);
static int	iwn4965_load_bootcode(struct iwn_softc *, const uint8_t *,
		    int);
static int	iwn4965_load_firmware(struct iwn_softc *);
static int	iwn5000_load_firmware_section(struct iwn_softc *, uint32_t,
		    const uint8_t *, int);
static int	iwn5000_load_firmware(struct iwn_softc *);
static int	iwn_read_firmware_leg(struct iwn_softc *,
		    struct iwn_fw_info *);
static int	iwn_read_firmware_tlv(struct iwn_softc *,
		    struct iwn_fw_info *, uint16_t);
static int	iwn_read_firmware(struct iwn_softc *);
static void	iwn_unload_firmware(struct iwn_softc *);
static int	iwn_clock_wait(struct iwn_softc *);
static int	iwn_apm_init(struct iwn_softc *);
static void	iwn_apm_stop_master(struct iwn_softc *);
static void	iwn_apm_stop(struct iwn_softc *);
static int	iwn4965_nic_config(struct iwn_softc *);
static int	iwn5000_nic_config(struct iwn_softc *);
static int	iwn_hw_prepare(struct iwn_softc *);
static int	iwn_hw_init(struct iwn_softc *);
static void	iwn_hw_stop(struct iwn_softc *);
static void	iwn_panicked(void *, int);
static int	iwn_init_locked(struct iwn_softc *);
static int	iwn_init(struct iwn_softc *);
static void	iwn_stop_locked(struct iwn_softc *);
static void	iwn_stop(struct iwn_softc *);
static void	iwn_scan_start(struct ieee80211com *);
static void	iwn_scan_end(struct ieee80211com *);
static void	iwn_set_channel(struct ieee80211com *);
static void	iwn_scan_curchan(struct ieee80211_scan_state *, unsigned long);
static void	iwn_scan_mindwell(struct ieee80211_scan_state *);
#ifdef	IWN_DEBUG
static char	*iwn_get_csr_string(int);
static void	iwn_debug_register(struct iwn_softc *);
#endif

static device_method_t iwn_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		iwn_probe),
	DEVMETHOD(device_attach,	iwn_attach),
	DEVMETHOD(device_detach,	iwn_detach),
	DEVMETHOD(device_shutdown,	iwn_shutdown),
	DEVMETHOD(device_suspend,	iwn_suspend),
	DEVMETHOD(device_resume,	iwn_resume),

	DEVMETHOD_END
};

static driver_t iwn_driver = {
	"iwn",
	iwn_methods,
	sizeof(struct iwn_softc)
};
static devclass_t iwn_devclass;

DRIVER_MODULE(iwn, pci, iwn_driver, iwn_devclass, NULL, NULL);
MODULE_PNP_INFO("U16:vendor;U16:device;D:#", pci, iwn, iwn_ident_table,
    nitems(iwn_ident_table) - 1);
MODULE_VERSION(iwn, 1);

MODULE_DEPEND(iwn, firmware, 1, 1, 1);
MODULE_DEPEND(iwn, pci, 1, 1, 1);
MODULE_DEPEND(iwn, wlan, 1, 1, 1);

static d_ioctl_t iwn_cdev_ioctl;
static d_open_t iwn_cdev_open;
static d_close_t iwn_cdev_close;

static struct cdevsw iwn_cdevsw = {
	.d_version = D_VERSION,
	.d_flags = 0,
	.d_open = iwn_cdev_open,
	.d_close = iwn_cdev_close,
	.d_ioctl = iwn_cdev_ioctl,
	.d_name = "iwn",
};

static int
iwn_probe(device_t dev)
{
	const struct iwn_ident *ident;

	for (ident = iwn_ident_table; ident->name != NULL; ident++) {
		if (pci_get_vendor(dev) == ident->vendor &&
		    pci_get_device(dev) == ident->device) {
			device_set_desc(dev, ident->name);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return ENXIO;
}

static int
iwn_is_3stream_device(struct iwn_softc *sc)
{
	/* XXX for now only 5300, until the 5350 can be tested */
	if (sc->hw_type == IWN_HW_REV_TYPE_5300)
		return (1);
	return (0);
}

static int
iwn_attach(device_t dev)
{
	struct iwn_softc *sc = device_get_softc(dev);
	struct ieee80211com *ic;
	int i, error, rid;

	sc->sc_dev = dev;

#ifdef	IWN_DEBUG
	error = resource_int_value(device_get_name(sc->sc_dev),
	    device_get_unit(sc->sc_dev), "debug", &(sc->sc_debug));
	if (error != 0)
		sc->sc_debug = 0;
#else
	sc->sc_debug = 0;
#endif

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: begin\n",__func__);

	/*
	 * Get the offset of the PCI Express Capability Structure in PCI
	 * Configuration Space.
	 */
	error = pci_find_cap(dev, PCIY_EXPRESS, &sc->sc_cap_off);
	if (error != 0) {
		device_printf(dev, "PCIe capability structure not found!\n");
		return error;
	}

	/* Clear device-specific "PCI retry timeout" register (41h). */
	pci_write_config(dev, 0x41, 0, 1);

	/* Enable bus-mastering. */
	pci_enable_busmaster(dev);

	rid = PCIR_BAR(0);
	sc->mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem == NULL) {
		device_printf(dev, "can't map mem space\n");
		error = ENOMEM;
		return error;
	}
	sc->sc_st = rman_get_bustag(sc->mem);
	sc->sc_sh = rman_get_bushandle(sc->mem);

	i = 1;
	rid = 0;
	if (pci_alloc_msi(dev, &i) == 0)
		rid = 1;
	/* Install interrupt handler. */
	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE |
	    (rid != 0 ? 0 : RF_SHAREABLE));
	if (sc->irq == NULL) {
		device_printf(dev, "can't map interrupt\n");
		error = ENOMEM;
		goto fail;
	}

	IWN_LOCK_INIT(sc);

	/* Read hardware revision and attach. */
	sc->hw_type = (IWN_READ(sc, IWN_HW_REV) >> IWN_HW_REV_TYPE_SHIFT)
	    & IWN_HW_REV_TYPE_MASK;
	sc->subdevice_id = pci_get_subdevice(dev);

	/*
	 * 4965 versus 5000 and later have different methods.
	 * Let's set those up first.
	 */
	if (sc->hw_type == IWN_HW_REV_TYPE_4965)
		iwn4965_attach(sc, pci_get_device(dev));
	else
		iwn5000_attach(sc, pci_get_device(dev));

	/*
	 * Next, let's setup the various parameters of each NIC.
	 */
	error = iwn_config_specific(sc, pci_get_device(dev));
	if (error != 0) {
		device_printf(dev, "could not attach device, error %d\n",
		    error);
		goto fail;
	}

	if ((error = iwn_hw_prepare(sc)) != 0) {
		device_printf(dev, "hardware not ready, error %d\n", error);
		goto fail;
	}

	/* Allocate DMA memory for firmware transfers. */
	if ((error = iwn_alloc_fwmem(sc)) != 0) {
		device_printf(dev,
		    "could not allocate memory for firmware, error %d\n",
		    error);
		goto fail;
	}

	/* Allocate "Keep Warm" page. */
	if ((error = iwn_alloc_kw(sc)) != 0) {
		device_printf(dev,
		    "could not allocate keep warm page, error %d\n", error);
		goto fail;
	}

	/* Allocate ICT table for 5000 Series. */
	if (sc->hw_type != IWN_HW_REV_TYPE_4965 &&
	    (error = iwn_alloc_ict(sc)) != 0) {
		device_printf(dev, "could not allocate ICT table, error %d\n",
		    error);
		goto fail;
	}

	/* Allocate TX scheduler "rings". */
	if ((error = iwn_alloc_sched(sc)) != 0) {
		device_printf(dev,
		    "could not allocate TX scheduler rings, error %d\n", error);
		goto fail;
	}

	/* Allocate TX rings (16 on 4965AGN, 20 on >=5000). */
	for (i = 0; i < sc->ntxqs; i++) {
		if ((error = iwn_alloc_tx_ring(sc, &sc->txq[i], i)) != 0) {
			device_printf(dev,
			    "could not allocate TX ring %d, error %d\n", i,
			    error);
			goto fail;
		}
	}

	/* Allocate RX ring. */
	if ((error = iwn_alloc_rx_ring(sc, &sc->rxq)) != 0) {
		device_printf(dev, "could not allocate RX ring, error %d\n",
		    error);
		goto fail;
	}

	/* Clear pending interrupts. */
	IWN_WRITE(sc, IWN_INT, 0xffffffff);

	ic = &sc->sc_ic;
	ic->ic_softc = sc;
	ic->ic_name = device_get_nameunit(dev);
	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */

	/* Set device capabilities. */
	ic->ic_caps =
		  IEEE80211_C_STA		/* station mode supported */
		| IEEE80211_C_MONITOR		/* monitor mode supported */
#if 0
		| IEEE80211_C_BGSCAN		/* background scanning */
#endif
		| IEEE80211_C_TXPMGT		/* tx power management */
		| IEEE80211_C_SHSLOT		/* short slot time supported */
		| IEEE80211_C_WPA
		| IEEE80211_C_SHPREAMBLE	/* short preamble supported */
#if 0
		| IEEE80211_C_IBSS		/* ibss/adhoc mode */
#endif
		| IEEE80211_C_WME		/* WME */
		| IEEE80211_C_PMGT		/* Station-side power mgmt */
		;

	/* Read MAC address, channels, etc from EEPROM. */
	if ((error = iwn_read_eeprom(sc, ic->ic_macaddr)) != 0) {
		device_printf(dev, "could not read EEPROM, error %d\n",
		    error);
		goto fail;
	}

	/* Count the number of available chains. */
	sc->ntxchains =
	    ((sc->txchainmask >> 2) & 1) +
	    ((sc->txchainmask >> 1) & 1) +
	    ((sc->txchainmask >> 0) & 1);
	sc->nrxchains =
	    ((sc->rxchainmask >> 2) & 1) +
	    ((sc->rxchainmask >> 1) & 1) +
	    ((sc->rxchainmask >> 0) & 1);
	if (bootverbose) {
		device_printf(dev, "MIMO %dT%dR, %.4s, address %6D\n",
		    sc->ntxchains, sc->nrxchains, sc->eeprom_domain,
		    ic->ic_macaddr, ":");
	}

	if (sc->sc_flags & IWN_FLAG_HAS_11N) {
		ic->ic_rxstream = sc->nrxchains;
		ic->ic_txstream = sc->ntxchains;

		/*
		 * Some of the 3 antenna devices (ie, the 4965) only supports
		 * 2x2 operation.  So correct the number of streams if
		 * it's not a 3-stream device.
		 */
		if (! iwn_is_3stream_device(sc)) {
			if (ic->ic_rxstream > 2)
				ic->ic_rxstream = 2;
			if (ic->ic_txstream > 2)
				ic->ic_txstream = 2;
		}

		ic->ic_htcaps =
			  IEEE80211_HTCAP_SMPS_OFF	/* SMPS mode disabled */
			| IEEE80211_HTCAP_SHORTGI20	/* short GI in 20MHz */
			| IEEE80211_HTCAP_CHWIDTH40	/* 40MHz channel width*/
			| IEEE80211_HTCAP_SHORTGI40	/* short GI in 40MHz */
#ifdef notyet
			| IEEE80211_HTCAP_GREENFIELD
#if IWN_RBUF_SIZE == 8192
			| IEEE80211_HTCAP_MAXAMSDU_7935	/* max A-MSDU length */
#else
			| IEEE80211_HTCAP_MAXAMSDU_3839	/* max A-MSDU length */
#endif
#endif
			/* s/w capabilities */
			| IEEE80211_HTC_HT		/* HT operation */
			| IEEE80211_HTC_AMPDU		/* tx A-MPDU */
#ifdef notyet
			| IEEE80211_HTC_AMSDU		/* tx A-MSDU */
#endif
			;
	}

	ieee80211_ifattach(ic);
	ic->ic_vap_create = iwn_vap_create;
	ic->ic_ioctl = iwn_ioctl;
	ic->ic_parent = iwn_parent;
	ic->ic_vap_delete = iwn_vap_delete;
	ic->ic_transmit = iwn_transmit;
	ic->ic_raw_xmit = iwn_raw_xmit;
	ic->ic_node_alloc = iwn_node_alloc;
	sc->sc_ampdu_rx_start = ic->ic_ampdu_rx_start;
	ic->ic_ampdu_rx_start = iwn_ampdu_rx_start;
	sc->sc_ampdu_rx_stop = ic->ic_ampdu_rx_stop;
	ic->ic_ampdu_rx_stop = iwn_ampdu_rx_stop;
	sc->sc_addba_request = ic->ic_addba_request;
	ic->ic_addba_request = iwn_addba_request;
	sc->sc_addba_response = ic->ic_addba_response;
	ic->ic_addba_response = iwn_addba_response;
	sc->sc_addba_stop = ic->ic_addba_stop;
	ic->ic_addba_stop = iwn_ampdu_tx_stop;
	ic->ic_newassoc = iwn_newassoc;
	ic->ic_wme.wme_update = iwn_updateedca;
	ic->ic_update_promisc = iwn_update_promisc;
	ic->ic_update_mcast = iwn_update_mcast;
	ic->ic_scan_start = iwn_scan_start;
	ic->ic_scan_end = iwn_scan_end;
	ic->ic_set_channel = iwn_set_channel;
	ic->ic_scan_curchan = iwn_scan_curchan;
	ic->ic_scan_mindwell = iwn_scan_mindwell;
	ic->ic_getradiocaps = iwn_getradiocaps;
	ic->ic_setregdomain = iwn_setregdomain;

	iwn_radiotap_attach(sc);

	callout_init_mtx(&sc->calib_to, &sc->sc_mtx, 0);
	callout_init_mtx(&sc->scan_timeout, &sc->sc_mtx, 0);
	callout_init_mtx(&sc->watchdog_to, &sc->sc_mtx, 0);
	TASK_INIT(&sc->sc_rftoggle_task, 0, iwn_rftoggle_task, sc);
	TASK_INIT(&sc->sc_panic_task, 0, iwn_panicked, sc);
	TASK_INIT(&sc->sc_xmit_task, 0, iwn_xmit_task, sc);

	mbufq_init(&sc->sc_xmit_queue, 1024);

	sc->sc_tq = taskqueue_create("iwn_taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->sc_tq);
	error = taskqueue_start_threads(&sc->sc_tq, 1, 0, "iwn_taskq");
	if (error != 0) {
		device_printf(dev, "can't start threads, error %d\n", error);
		goto fail;
	}

	iwn_sysctlattach(sc);

	/*
	 * Hook our interrupt after all initialization is complete.
	 */
	error = bus_setup_intr(dev, sc->irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, iwn_intr, sc, &sc->sc_ih);
	if (error != 0) {
		device_printf(dev, "can't establish interrupt, error %d\n",
		    error);
		goto fail;
	}

#if 0
	device_printf(sc->sc_dev, "%s: rx_stats=%d, rx_stats_bt=%d\n",
	    __func__,
	    sizeof(struct iwn_stats),
	    sizeof(struct iwn_stats_bt));
#endif

	if (bootverbose)
		ieee80211_announce(ic);
	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

	/* Add debug ioctl right at the end */
	sc->sc_cdev = make_dev(&iwn_cdevsw, device_get_unit(dev),
	    UID_ROOT, GID_WHEEL, 0600, "%s", device_get_nameunit(dev));
	if (sc->sc_cdev == NULL) {
		device_printf(dev, "failed to create debug character device\n");
	} else {
		sc->sc_cdev->si_drv1 = sc;
	}
	return 0;
fail:
	iwn_detach(dev);
	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end in error\n",__func__);
	return error;
}

/*
 * Define specific configuration based on device id and subdevice id
 * pid : PCI device id
 */
static int
iwn_config_specific(struct iwn_softc *sc, uint16_t pid)
{

	switch (pid) {
/* 4965 series */
	case IWN_DID_4965_1:
	case IWN_DID_4965_2:
	case IWN_DID_4965_3:
	case IWN_DID_4965_4:
		sc->base_params = &iwn4965_base_params;
		sc->limits = &iwn4965_sensitivity_limits;
		sc->fwname = "iwn4965fw";
		/* Override chains masks, ROM is known to be broken. */
		sc->txchainmask = IWN_ANT_AB;
		sc->rxchainmask = IWN_ANT_ABC;
		/* Enable normal btcoex */
		sc->sc_flags |= IWN_FLAG_BTCOEX;
		break;
/* 1000 Series */
	case IWN_DID_1000_1:
	case IWN_DID_1000_2:
		switch(sc->subdevice_id) {
			case	IWN_SDID_1000_1:
			case	IWN_SDID_1000_2:
			case	IWN_SDID_1000_3:
			case	IWN_SDID_1000_4:
			case	IWN_SDID_1000_5:
			case	IWN_SDID_1000_6:
			case	IWN_SDID_1000_7:
			case	IWN_SDID_1000_8:
			case	IWN_SDID_1000_9:
			case	IWN_SDID_1000_10:
			case	IWN_SDID_1000_11:
			case	IWN_SDID_1000_12:
				sc->limits = &iwn1000_sensitivity_limits;
				sc->base_params = &iwn1000_base_params;
				sc->fwname = "iwn1000fw";
				break;
			default:
				device_printf(sc->sc_dev, "adapter type id : 0x%04x sub id :"
				    "0x%04x rev %d not supported (subdevice)\n", pid,
				    sc->subdevice_id,sc->hw_type);
				return ENOTSUP;
		}
		break;
/* 6x00 Series */
	case IWN_DID_6x00_2:
	case IWN_DID_6x00_4:
	case IWN_DID_6x00_1:
	case IWN_DID_6x00_3:
		sc->fwname = "iwn6000fw";
		sc->limits = &iwn6000_sensitivity_limits;
		switch(sc->subdevice_id) {
			case IWN_SDID_6x00_1:
			case IWN_SDID_6x00_2:
			case IWN_SDID_6x00_8:
				//iwl6000_3agn_cfg
				sc->base_params = &iwn_6000_base_params;
				break;
			case IWN_SDID_6x00_3:
			case IWN_SDID_6x00_6:
			case IWN_SDID_6x00_9:
				////iwl6000i_2agn
			case IWN_SDID_6x00_4:
			case IWN_SDID_6x00_7:
			case IWN_SDID_6x00_10:
				//iwl6000i_2abg_cfg
			case IWN_SDID_6x00_5:
				//iwl6000i_2bg_cfg
				sc->base_params = &iwn_6000i_base_params;
				sc->sc_flags |= IWN_FLAG_INTERNAL_PA;
				sc->txchainmask = IWN_ANT_BC;
				sc->rxchainmask = IWN_ANT_BC;
				break;
			default:
				device_printf(sc->sc_dev, "adapter type id : 0x%04x sub id :"
				    "0x%04x rev %d not supported (subdevice)\n", pid,
				    sc->subdevice_id,sc->hw_type);
				return ENOTSUP;
		}
		break;
/* 6x05 Series */
	case IWN_DID_6x05_1:
	case IWN_DID_6x05_2:
		switch(sc->subdevice_id) {
			case IWN_SDID_6x05_1:
			case IWN_SDID_6x05_4:
			case IWN_SDID_6x05_6:
				//iwl6005_2agn_cfg
			case IWN_SDID_6x05_2:
			case IWN_SDID_6x05_5:
			case IWN_SDID_6x05_7:
				//iwl6005_2abg_cfg
			case IWN_SDID_6x05_3:
				//iwl6005_2bg_cfg
			case IWN_SDID_6x05_8:
			case IWN_SDID_6x05_9:
				//iwl6005_2agn_sff_cfg
			case IWN_SDID_6x05_10:
				//iwl6005_2agn_d_cfg
			case IWN_SDID_6x05_11:
				//iwl6005_2agn_mow1_cfg
			case IWN_SDID_6x05_12:
				//iwl6005_2agn_mow2_cfg
				sc->fwname = "iwn6000g2afw";
				sc->limits = &iwn6000_sensitivity_limits;
				sc->base_params = &iwn_6000g2_base_params;
				break;
			default:
				device_printf(sc->sc_dev, "adapter type id : 0x%04x sub id :"
				    "0x%04x rev %d not supported (subdevice)\n", pid,
				    sc->subdevice_id,sc->hw_type);
				return ENOTSUP;
		}
		break;
/* 6x35 Series */
	case IWN_DID_6035_1:
	case IWN_DID_6035_2:
		switch(sc->subdevice_id) {
			case IWN_SDID_6035_1:
			case IWN_SDID_6035_2:
			case IWN_SDID_6035_3:
			case IWN_SDID_6035_4:
			case IWN_SDID_6035_5:
				sc->fwname = "iwn6000g2bfw";
				sc->limits = &iwn6235_sensitivity_limits;
				sc->base_params = &iwn_6235_base_params;
				break;
			default:
				device_printf(sc->sc_dev, "adapter type id : 0x%04x sub id :"
				    "0x%04x rev %d not supported (subdevice)\n", pid,
				    sc->subdevice_id,sc->hw_type);
				return ENOTSUP;
		}
		break;
/* 6x50 WiFi/WiMax Series */
	case IWN_DID_6050_1:
	case IWN_DID_6050_2:
		switch(sc->subdevice_id) {
			case IWN_SDID_6050_1:
			case IWN_SDID_6050_3:
			case IWN_SDID_6050_5:
				//iwl6050_2agn_cfg
			case IWN_SDID_6050_2:
			case IWN_SDID_6050_4:
			case IWN_SDID_6050_6:
				//iwl6050_2abg_cfg
				sc->fwname = "iwn6050fw";
				sc->txchainmask = IWN_ANT_AB;
				sc->rxchainmask = IWN_ANT_AB;
				sc->limits = &iwn6000_sensitivity_limits;
				sc->base_params = &iwn_6050_base_params;
				break;
			default:
				device_printf(sc->sc_dev, "adapter type id : 0x%04x sub id :"
				    "0x%04x rev %d not supported (subdevice)\n", pid,
				    sc->subdevice_id,sc->hw_type);
				return ENOTSUP;
		}
		break;
/* 6150 WiFi/WiMax Series */
	case IWN_DID_6150_1:
	case IWN_DID_6150_2:
		switch(sc->subdevice_id) {
			case IWN_SDID_6150_1:
			case IWN_SDID_6150_3:
			case IWN_SDID_6150_5:
				// iwl6150_bgn_cfg
			case IWN_SDID_6150_2:
			case IWN_SDID_6150_4:
			case IWN_SDID_6150_6:
				//iwl6150_bg_cfg
				sc->fwname = "iwn6050fw";
				sc->limits = &iwn6000_sensitivity_limits;
				sc->base_params = &iwn_6150_base_params;
				break;
			default:
				device_printf(sc->sc_dev, "adapter type id : 0x%04x sub id :"
				    "0x%04x rev %d not supported (subdevice)\n", pid,
				    sc->subdevice_id,sc->hw_type);
				return ENOTSUP;
		}
		break;
/* 6030 Series and 1030 Series */
	case IWN_DID_x030_1:
	case IWN_DID_x030_2:
	case IWN_DID_x030_3:
	case IWN_DID_x030_4:
		switch(sc->subdevice_id) {
			case IWN_SDID_x030_1:
			case IWN_SDID_x030_3:
			case IWN_SDID_x030_5:
			// iwl1030_bgn_cfg
			case IWN_SDID_x030_2:
			case IWN_SDID_x030_4:
			case IWN_SDID_x030_6:
			//iwl1030_bg_cfg
			case IWN_SDID_x030_7:
			case IWN_SDID_x030_10:
			case IWN_SDID_x030_14:
			//iwl6030_2agn_cfg
			case IWN_SDID_x030_8:
			case IWN_SDID_x030_11:
			case IWN_SDID_x030_15:
			// iwl6030_2bgn_cfg
			case IWN_SDID_x030_9:
			case IWN_SDID_x030_12:
			case IWN_SDID_x030_16:
			// iwl6030_2abg_cfg
			case IWN_SDID_x030_13:
			//iwl6030_2bg_cfg
				sc->fwname = "iwn6000g2bfw";
				sc->limits = &iwn6000_sensitivity_limits;
				sc->base_params = &iwn_6000g2b_base_params;
				break;
			default:
				device_printf(sc->sc_dev, "adapter type id : 0x%04x sub id :"
				    "0x%04x rev %d not supported (subdevice)\n", pid,
				    sc->subdevice_id,sc->hw_type);
				return ENOTSUP;
		}
		break;
/* 130 Series WiFi */
/* XXX: This series will need adjustment for rate.
 * see rx_with_siso_diversity in linux kernel
 */
	case IWN_DID_130_1:
	case IWN_DID_130_2:
		switch(sc->subdevice_id) {
			case IWN_SDID_130_1:
			case IWN_SDID_130_3:
			case IWN_SDID_130_5:
			//iwl130_bgn_cfg
			case IWN_SDID_130_2:
			case IWN_SDID_130_4:
			case IWN_SDID_130_6:
			//iwl130_bg_cfg
				sc->fwname = "iwn6000g2bfw";
				sc->limits = &iwn6000_sensitivity_limits;
				sc->base_params = &iwn_6000g2b_base_params;
				break;
			default:
				device_printf(sc->sc_dev, "adapter type id : 0x%04x sub id :"
				    "0x%04x rev %d not supported (subdevice)\n", pid,
				    sc->subdevice_id,sc->hw_type);
				return ENOTSUP;
		}
		break;
/* 100 Series WiFi */
	case IWN_DID_100_1:
	case IWN_DID_100_2:
		switch(sc->subdevice_id) {
			case IWN_SDID_100_1:
			case IWN_SDID_100_2:
			case IWN_SDID_100_3:
			case IWN_SDID_100_4:
			case IWN_SDID_100_5:
			case IWN_SDID_100_6:
				sc->limits = &iwn1000_sensitivity_limits;
				sc->base_params = &iwn1000_base_params;
				sc->fwname = "iwn100fw";
				break;
			default:
				device_printf(sc->sc_dev, "adapter type id : 0x%04x sub id :"
				    "0x%04x rev %d not supported (subdevice)\n", pid,
				    sc->subdevice_id,sc->hw_type);
				return ENOTSUP;
		}
		break;

/* 105 Series */
/* XXX: This series will need adjustment for rate.
 * see rx_with_siso_diversity in linux kernel
 */
	case IWN_DID_105_1:
	case IWN_DID_105_2:
		switch(sc->subdevice_id) {
			case IWN_SDID_105_1:
			case IWN_SDID_105_2:
			case IWN_SDID_105_3:
			//iwl105_bgn_cfg
			case IWN_SDID_105_4:
			//iwl105_bgn_d_cfg
				sc->limits = &iwn2030_sensitivity_limits;
				sc->base_params = &iwn2000_base_params;
				sc->fwname = "iwn105fw";
				break;
			default:
				device_printf(sc->sc_dev, "adapter type id : 0x%04x sub id :"
				    "0x%04x rev %d not supported (subdevice)\n", pid,
				    sc->subdevice_id,sc->hw_type);
				return ENOTSUP;
		}
		break;

/* 135 Series */
/* XXX: This series will need adjustment for rate.
 * see rx_with_siso_diversity in linux kernel
 */
	case IWN_DID_135_1:
	case IWN_DID_135_2:
		switch(sc->subdevice_id) {
			case IWN_SDID_135_1:
			case IWN_SDID_135_2:
			case IWN_SDID_135_3:
				sc->limits = &iwn2030_sensitivity_limits;
				sc->base_params = &iwn2030_base_params;
				sc->fwname = "iwn135fw";
				break;
			default:
				device_printf(sc->sc_dev, "adapter type id : 0x%04x sub id :"
				    "0x%04x rev %d not supported (subdevice)\n", pid,
				    sc->subdevice_id,sc->hw_type);
				return ENOTSUP;
		}
		break;

/* 2x00 Series */
	case IWN_DID_2x00_1:
	case IWN_DID_2x00_2:
		switch(sc->subdevice_id) {
			case IWN_SDID_2x00_1:
			case IWN_SDID_2x00_2:
			case IWN_SDID_2x00_3:
			//iwl2000_2bgn_cfg
			case IWN_SDID_2x00_4:
			//iwl2000_2bgn_d_cfg
				sc->limits = &iwn2030_sensitivity_limits;
				sc->base_params = &iwn2000_base_params;
				sc->fwname = "iwn2000fw";
				break;
			default:
				device_printf(sc->sc_dev, "adapter type id : 0x%04x sub id :"
				    "0x%04x rev %d not supported (subdevice) \n",
				    pid, sc->subdevice_id, sc->hw_type);
				return ENOTSUP;
		}
		break;
/* 2x30 Series */
	case IWN_DID_2x30_1:
	case IWN_DID_2x30_2:
		switch(sc->subdevice_id) {
			case IWN_SDID_2x30_1:
			case IWN_SDID_2x30_3:
			case IWN_SDID_2x30_5:
			//iwl100_bgn_cfg
			case IWN_SDID_2x30_2:
			case IWN_SDID_2x30_4:
			case IWN_SDID_2x30_6:
			//iwl100_bg_cfg
				sc->limits = &iwn2030_sensitivity_limits;
				sc->base_params = &iwn2030_base_params;
				sc->fwname = "iwn2030fw";
				break;
			default:
				device_printf(sc->sc_dev, "adapter type id : 0x%04x sub id :"
				    "0x%04x rev %d not supported (subdevice)\n", pid,
				    sc->subdevice_id,sc->hw_type);
				return ENOTSUP;
		}
		break;
/* 5x00 Series */
	case IWN_DID_5x00_1:
	case IWN_DID_5x00_2:
	case IWN_DID_5x00_3:
	case IWN_DID_5x00_4:
		sc->limits = &iwn5000_sensitivity_limits;
		sc->base_params = &iwn5000_base_params;
		sc->fwname = "iwn5000fw";
		switch(sc->subdevice_id) {
			case IWN_SDID_5x00_1:
			case IWN_SDID_5x00_2:
			case IWN_SDID_5x00_3:
			case IWN_SDID_5x00_4:
			case IWN_SDID_5x00_9:
			case IWN_SDID_5x00_10:
			case IWN_SDID_5x00_11:
			case IWN_SDID_5x00_12:
			case IWN_SDID_5x00_17:
			case IWN_SDID_5x00_18:
			case IWN_SDID_5x00_19:
			case IWN_SDID_5x00_20:
			//iwl5100_agn_cfg
				sc->txchainmask = IWN_ANT_B;
				sc->rxchainmask = IWN_ANT_AB;
				break;
			case IWN_SDID_5x00_5:
			case IWN_SDID_5x00_6:
			case IWN_SDID_5x00_13:
			case IWN_SDID_5x00_14:
			case IWN_SDID_5x00_21:
			case IWN_SDID_5x00_22:
			//iwl5100_bgn_cfg
				sc->txchainmask = IWN_ANT_B;
				sc->rxchainmask = IWN_ANT_AB;
				break;
			case IWN_SDID_5x00_7:
			case IWN_SDID_5x00_8:
			case IWN_SDID_5x00_15:
			case IWN_SDID_5x00_16:
			case IWN_SDID_5x00_23:
			case IWN_SDID_5x00_24:
			//iwl5100_abg_cfg
				sc->txchainmask = IWN_ANT_B;
				sc->rxchainmask = IWN_ANT_AB;
				break;
			case IWN_SDID_5x00_25:
			case IWN_SDID_5x00_26:
			case IWN_SDID_5x00_27:
			case IWN_SDID_5x00_28:
			case IWN_SDID_5x00_29:
			case IWN_SDID_5x00_30:
			case IWN_SDID_5x00_31:
			case IWN_SDID_5x00_32:
			case IWN_SDID_5x00_33:
			case IWN_SDID_5x00_34:
			case IWN_SDID_5x00_35:
			case IWN_SDID_5x00_36:
			//iwl5300_agn_cfg
				sc->txchainmask = IWN_ANT_ABC;
				sc->rxchainmask = IWN_ANT_ABC;
				break;
			default:
				device_printf(sc->sc_dev, "adapter type id : 0x%04x sub id :"
				    "0x%04x rev %d not supported (subdevice)\n", pid,
				    sc->subdevice_id,sc->hw_type);
				return ENOTSUP;
		}
		break;
/* 5x50 Series */
	case IWN_DID_5x50_1:
	case IWN_DID_5x50_2:
	case IWN_DID_5x50_3:
	case IWN_DID_5x50_4:
		sc->limits = &iwn5000_sensitivity_limits;
		sc->base_params = &iwn5000_base_params;
		sc->fwname = "iwn5000fw";
		switch(sc->subdevice_id) {
			case IWN_SDID_5x50_1:
			case IWN_SDID_5x50_2:
			case IWN_SDID_5x50_3:
			//iwl5350_agn_cfg
				sc->limits = &iwn5000_sensitivity_limits;
				sc->base_params = &iwn5000_base_params;
				sc->fwname = "iwn5000fw";
				break;
			case IWN_SDID_5x50_4:
			case IWN_SDID_5x50_5:
			case IWN_SDID_5x50_8:
			case IWN_SDID_5x50_9:
			case IWN_SDID_5x50_10:
			case IWN_SDID_5x50_11:
			//iwl5150_agn_cfg
			case IWN_SDID_5x50_6:
			case IWN_SDID_5x50_7:
			case IWN_SDID_5x50_12:
			case IWN_SDID_5x50_13:
			//iwl5150_abg_cfg
				sc->limits = &iwn5000_sensitivity_limits;
				sc->fwname = "iwn5150fw";
				sc->base_params = &iwn_5x50_base_params;
				break;
			default:
				device_printf(sc->sc_dev, "adapter type id : 0x%04x sub id :"
				    "0x%04x rev %d not supported (subdevice)\n", pid,
				    sc->subdevice_id,sc->hw_type);
				return ENOTSUP;
		}
		break;
	default:
		device_printf(sc->sc_dev, "adapter type id : 0x%04x sub id : 0x%04x"
		    "rev 0x%08x not supported (device)\n", pid, sc->subdevice_id,
		     sc->hw_type);
		return ENOTSUP;
	}
	return 0;
}

static void
iwn4965_attach(struct iwn_softc *sc, uint16_t pid)
{
	struct iwn_ops *ops = &sc->ops;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	ops->load_firmware = iwn4965_load_firmware;
	ops->read_eeprom = iwn4965_read_eeprom;
	ops->post_alive = iwn4965_post_alive;
	ops->nic_config = iwn4965_nic_config;
	ops->update_sched = iwn4965_update_sched;
	ops->get_temperature = iwn4965_get_temperature;
	ops->get_rssi = iwn4965_get_rssi;
	ops->set_txpower = iwn4965_set_txpower;
	ops->init_gains = iwn4965_init_gains;
	ops->set_gains = iwn4965_set_gains;
	ops->rxon_assoc = iwn4965_rxon_assoc;
	ops->add_node = iwn4965_add_node;
	ops->tx_done = iwn4965_tx_done;
	ops->ampdu_tx_start = iwn4965_ampdu_tx_start;
	ops->ampdu_tx_stop = iwn4965_ampdu_tx_stop;
	sc->ntxqs = IWN4965_NTXQUEUES;
	sc->firstaggqueue = IWN4965_FIRSTAGGQUEUE;
	sc->ndmachnls = IWN4965_NDMACHNLS;
	sc->broadcast_id = IWN4965_ID_BROADCAST;
	sc->rxonsz = IWN4965_RXONSZ;
	sc->schedsz = IWN4965_SCHEDSZ;
	sc->fw_text_maxsz = IWN4965_FW_TEXT_MAXSZ;
	sc->fw_data_maxsz = IWN4965_FW_DATA_MAXSZ;
	sc->fwsz = IWN4965_FWSZ;
	sc->sched_txfact_addr = IWN4965_SCHED_TXFACT;
	sc->limits = &iwn4965_sensitivity_limits;
	sc->fwname = "iwn4965fw";
	/* Override chains masks, ROM is known to be broken. */
	sc->txchainmask = IWN_ANT_AB;
	sc->rxchainmask = IWN_ANT_ABC;
	/* Enable normal btcoex */
	sc->sc_flags |= IWN_FLAG_BTCOEX;

	DPRINTF(sc, IWN_DEBUG_TRACE, "%s: end\n",__func__);
}

static void
iwn5000_attach(struct iwn_softc *sc, uint16_t pid)
{
	struct iwn_ops *ops = &sc->ops;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	ops->load_firmware = iwn5000_load_firmware;
	ops->read_eeprom = iwn5000_read_eeprom;
	ops->post_alive = iwn5000_post_alive;
	ops->nic_config = iwn5000_nic_config;
	ops->update_sched = iwn5000_update_sched;
	ops->get_temperature = iwn5000_get_temperature;
	ops->get_rssi = iwn5000_get_rssi;
	ops->set_txpower = iwn5000_set_txpower;
	ops->init_gains = iwn5000_init_gains;
	ops->set_gains = iwn5000_set_gains;
	ops->rxon_assoc = iwn5000_rxon_assoc;
	ops->add_node = iwn5000_add_node;
	ops->tx_done = iwn5000_tx_done;
	ops->ampdu_tx_start = iwn5000_ampdu_tx_start;
	ops->ampdu_tx_stop = iwn5000_ampdu_tx_stop;
	sc->ntxqs = IWN5000_NTXQUEUES;
	sc->firstaggqueue = IWN5000_FIRSTAGGQUEUE;
	sc->ndmachnls = IWN5000_NDMACHNLS;
	sc->broadcast_id = IWN5000_ID_BROADCAST;
	sc->rxonsz = IWN5000_RXONSZ;
	sc->schedsz = IWN5000_SCHEDSZ;
	sc->fw_text_maxsz = IWN5000_FW_TEXT_MAXSZ;
	sc->fw_data_maxsz = IWN5000_FW_DATA_MAXSZ;
	sc->fwsz = IWN5000_FWSZ;
	sc->sched_txfact_addr = IWN5000_SCHED_TXFACT;
	sc->reset_noise_gain = IWN5000_PHY_CALIB_RESET_NOISE_GAIN;
	sc->noise_gain = IWN5000_PHY_CALIB_NOISE_GAIN;

	DPRINTF(sc, IWN_DEBUG_TRACE, "%s: end\n",__func__);
}

/*
 * Attach the interface to 802.11 radiotap.
 */
static void
iwn_radiotap_attach(struct iwn_softc *sc)
{

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);
	ieee80211_radiotap_attach(&sc->sc_ic,
	    &sc->sc_txtap.wt_ihdr, sizeof(sc->sc_txtap),
		IWN_TX_RADIOTAP_PRESENT,
	    &sc->sc_rxtap.wr_ihdr, sizeof(sc->sc_rxtap),
		IWN_RX_RADIOTAP_PRESENT);
	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s end\n", __func__);
}

static void
iwn_sysctlattach(struct iwn_softc *sc)
{
#ifdef	IWN_DEBUG
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->sc_dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->sc_dev);

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "debug", CTLFLAG_RW, &sc->sc_debug, sc->sc_debug,
		"control debugging printfs");
#endif
}

static struct ieee80211vap *
iwn_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ], int unit,
    enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct iwn_softc *sc = ic->ic_softc;
	struct iwn_vap *ivp;
	struct ieee80211vap *vap;

	if (!TAILQ_EMPTY(&ic->ic_vaps))		/* only one at a time */
		return NULL;

	ivp = malloc(sizeof(struct iwn_vap), M_80211_VAP, M_WAITOK | M_ZERO);
	vap = &ivp->iv_vap;
	ieee80211_vap_setup(ic, vap, name, unit, opmode, flags, bssid);
	ivp->ctx = IWN_RXON_BSS_CTX;
	vap->iv_bmissthreshold = 10;		/* override default */
	/* Override with driver methods. */
	ivp->iv_newstate = vap->iv_newstate;
	vap->iv_newstate = iwn_newstate;
	sc->ivap[IWN_RXON_BSS_CTX] = vap;

	ieee80211_ratectl_init(vap);
	/* Complete setup. */
	ieee80211_vap_attach(vap, iwn_media_change, ieee80211_media_status,
	    mac);
	ic->ic_opmode = opmode;
	return vap;
}

static void
iwn_vap_delete(struct ieee80211vap *vap)
{
	struct iwn_vap *ivp = IWN_VAP(vap);

	ieee80211_ratectl_deinit(vap);
	ieee80211_vap_detach(vap);
	free(ivp, M_80211_VAP);
}

static void
iwn_xmit_queue_drain(struct iwn_softc *sc)
{
	struct mbuf *m;
	struct ieee80211_node *ni;

	IWN_LOCK_ASSERT(sc);
	while ((m = mbufq_dequeue(&sc->sc_xmit_queue)) != NULL) {
		ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
		ieee80211_free_node(ni);
		m_freem(m);
	}
}

static int
iwn_xmit_queue_enqueue(struct iwn_softc *sc, struct mbuf *m)
{

	IWN_LOCK_ASSERT(sc);
	return (mbufq_enqueue(&sc->sc_xmit_queue, m));
}

static int
iwn_detach(device_t dev)
{
	struct iwn_softc *sc = device_get_softc(dev);
	int qid;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	if (sc->sc_ic.ic_softc != NULL) {
		/* Free the mbuf queue and node references */
		IWN_LOCK(sc);
		iwn_xmit_queue_drain(sc);
		IWN_UNLOCK(sc);

		iwn_stop(sc);

		taskqueue_drain_all(sc->sc_tq);
		taskqueue_free(sc->sc_tq);

		callout_drain(&sc->watchdog_to);
		callout_drain(&sc->scan_timeout);
		callout_drain(&sc->calib_to);
		ieee80211_ifdetach(&sc->sc_ic);
	}

	/* Uninstall interrupt handler. */
	if (sc->irq != NULL) {
		bus_teardown_intr(dev, sc->irq, sc->sc_ih);
		bus_release_resource(dev, SYS_RES_IRQ, rman_get_rid(sc->irq),
		    sc->irq);
		pci_release_msi(dev);
	}

	/* Free DMA resources. */
	iwn_free_rx_ring(sc, &sc->rxq);
	for (qid = 0; qid < sc->ntxqs; qid++)
		iwn_free_tx_ring(sc, &sc->txq[qid]);
	iwn_free_sched(sc);
	iwn_free_kw(sc);
	if (sc->ict != NULL)
		iwn_free_ict(sc);
	iwn_free_fwmem(sc);

	if (sc->mem != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->mem), sc->mem);

	if (sc->sc_cdev) {
		destroy_dev(sc->sc_cdev);
		sc->sc_cdev = NULL;
	}

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n", __func__);
	IWN_LOCK_DESTROY(sc);
	return 0;
}

static int
iwn_shutdown(device_t dev)
{
	struct iwn_softc *sc = device_get_softc(dev);

	iwn_stop(sc);
	return 0;
}

static int
iwn_suspend(device_t dev)
{
	struct iwn_softc *sc = device_get_softc(dev);

	ieee80211_suspend_all(&sc->sc_ic);
	return 0;
}

static int
iwn_resume(device_t dev)
{
	struct iwn_softc *sc = device_get_softc(dev);

	/* Clear device-specific "PCI retry timeout" register (41h). */
	pci_write_config(dev, 0x41, 0, 1);

	ieee80211_resume_all(&sc->sc_ic);
	return 0;
}

static int
iwn_nic_lock(struct iwn_softc *sc)
{
	int ntries;

	/* Request exclusive access to NIC. */
	IWN_SETBITS(sc, IWN_GP_CNTRL, IWN_GP_CNTRL_MAC_ACCESS_REQ);

	/* Spin until we actually get the lock. */
	for (ntries = 0; ntries < 1000; ntries++) {
		if ((IWN_READ(sc, IWN_GP_CNTRL) &
		     (IWN_GP_CNTRL_MAC_ACCESS_ENA | IWN_GP_CNTRL_SLEEP)) ==
		    IWN_GP_CNTRL_MAC_ACCESS_ENA)
			return 0;
		DELAY(10);
	}
	return ETIMEDOUT;
}

static __inline void
iwn_nic_unlock(struct iwn_softc *sc)
{
	IWN_CLRBITS(sc, IWN_GP_CNTRL, IWN_GP_CNTRL_MAC_ACCESS_REQ);
}

static __inline uint32_t
iwn_prph_read(struct iwn_softc *sc, uint32_t addr)
{
	IWN_WRITE(sc, IWN_PRPH_RADDR, IWN_PRPH_DWORD | addr);
	IWN_BARRIER_READ_WRITE(sc);
	return IWN_READ(sc, IWN_PRPH_RDATA);
}

static __inline void
iwn_prph_write(struct iwn_softc *sc, uint32_t addr, uint32_t data)
{
	IWN_WRITE(sc, IWN_PRPH_WADDR, IWN_PRPH_DWORD | addr);
	IWN_BARRIER_WRITE(sc);
	IWN_WRITE(sc, IWN_PRPH_WDATA, data);
}

static __inline void
iwn_prph_setbits(struct iwn_softc *sc, uint32_t addr, uint32_t mask)
{
	iwn_prph_write(sc, addr, iwn_prph_read(sc, addr) | mask);
}

static __inline void
iwn_prph_clrbits(struct iwn_softc *sc, uint32_t addr, uint32_t mask)
{
	iwn_prph_write(sc, addr, iwn_prph_read(sc, addr) & ~mask);
}

static __inline void
iwn_prph_write_region_4(struct iwn_softc *sc, uint32_t addr,
    const uint32_t *data, int count)
{
	for (; count > 0; count--, data++, addr += 4)
		iwn_prph_write(sc, addr, *data);
}

static __inline uint32_t
iwn_mem_read(struct iwn_softc *sc, uint32_t addr)
{
	IWN_WRITE(sc, IWN_MEM_RADDR, addr);
	IWN_BARRIER_READ_WRITE(sc);
	return IWN_READ(sc, IWN_MEM_RDATA);
}

static __inline void
iwn_mem_write(struct iwn_softc *sc, uint32_t addr, uint32_t data)
{
	IWN_WRITE(sc, IWN_MEM_WADDR, addr);
	IWN_BARRIER_WRITE(sc);
	IWN_WRITE(sc, IWN_MEM_WDATA, data);
}

static __inline void
iwn_mem_write_2(struct iwn_softc *sc, uint32_t addr, uint16_t data)
{
	uint32_t tmp;

	tmp = iwn_mem_read(sc, addr & ~3);
	if (addr & 3)
		tmp = (tmp & 0x0000ffff) | data << 16;
	else
		tmp = (tmp & 0xffff0000) | data;
	iwn_mem_write(sc, addr & ~3, tmp);
}

static __inline void
iwn_mem_read_region_4(struct iwn_softc *sc, uint32_t addr, uint32_t *data,
    int count)
{
	for (; count > 0; count--, addr += 4)
		*data++ = iwn_mem_read(sc, addr);
}

static __inline void
iwn_mem_set_region_4(struct iwn_softc *sc, uint32_t addr, uint32_t val,
    int count)
{
	for (; count > 0; count--, addr += 4)
		iwn_mem_write(sc, addr, val);
}

static int
iwn_eeprom_lock(struct iwn_softc *sc)
{
	int i, ntries;

	for (i = 0; i < 100; i++) {
		/* Request exclusive access to EEPROM. */
		IWN_SETBITS(sc, IWN_HW_IF_CONFIG,
		    IWN_HW_IF_CONFIG_EEPROM_LOCKED);

		/* Spin until we actually get the lock. */
		for (ntries = 0; ntries < 100; ntries++) {
			if (IWN_READ(sc, IWN_HW_IF_CONFIG) &
			    IWN_HW_IF_CONFIG_EEPROM_LOCKED)
				return 0;
			DELAY(10);
		}
	}
	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s end timeout\n", __func__);
	return ETIMEDOUT;
}

static __inline void
iwn_eeprom_unlock(struct iwn_softc *sc)
{
	IWN_CLRBITS(sc, IWN_HW_IF_CONFIG, IWN_HW_IF_CONFIG_EEPROM_LOCKED);
}

/*
 * Initialize access by host to One Time Programmable ROM.
 * NB: This kind of ROM can be found on 1000 or 6000 Series only.
 */
static int
iwn_init_otprom(struct iwn_softc *sc)
{
	uint16_t prev, base, next;
	int count, error;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	/* Wait for clock stabilization before accessing prph. */
	if ((error = iwn_clock_wait(sc)) != 0)
		return error;

	if ((error = iwn_nic_lock(sc)) != 0)
		return error;
	iwn_prph_setbits(sc, IWN_APMG_PS, IWN_APMG_PS_RESET_REQ);
	DELAY(5);
	iwn_prph_clrbits(sc, IWN_APMG_PS, IWN_APMG_PS_RESET_REQ);
	iwn_nic_unlock(sc);

	/* Set auto clock gate disable bit for HW with OTP shadow RAM. */
	if (sc->base_params->shadow_ram_support) {
		IWN_SETBITS(sc, IWN_DBG_LINK_PWR_MGMT,
		    IWN_RESET_LINK_PWR_MGMT_DIS);
	}
	IWN_CLRBITS(sc, IWN_EEPROM_GP, IWN_EEPROM_GP_IF_OWNER);
	/* Clear ECC status. */
	IWN_SETBITS(sc, IWN_OTP_GP,
	    IWN_OTP_GP_ECC_CORR_STTS | IWN_OTP_GP_ECC_UNCORR_STTS);

	/*
	 * Find the block before last block (contains the EEPROM image)
	 * for HW without OTP shadow RAM.
	 */
	if (! sc->base_params->shadow_ram_support) {
		/* Switch to absolute addressing mode. */
		IWN_CLRBITS(sc, IWN_OTP_GP, IWN_OTP_GP_RELATIVE_ACCESS);
		base = prev = 0;
		for (count = 0; count < sc->base_params->max_ll_items;
		    count++) {
			error = iwn_read_prom_data(sc, base, &next, 2);
			if (error != 0)
				return error;
			if (next == 0)	/* End of linked-list. */
				break;
			prev = base;
			base = le16toh(next);
		}
		if (count == 0 || count == sc->base_params->max_ll_items)
			return EIO;
		/* Skip "next" word. */
		sc->prom_base = prev + 1;
	}

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s end\n", __func__);

	return 0;
}

static int
iwn_read_prom_data(struct iwn_softc *sc, uint32_t addr, void *data, int count)
{
	uint8_t *out = data;
	uint32_t val, tmp;
	int ntries;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	addr += sc->prom_base;
	for (; count > 0; count -= 2, addr++) {
		IWN_WRITE(sc, IWN_EEPROM, addr << 2);
		for (ntries = 0; ntries < 10; ntries++) {
			val = IWN_READ(sc, IWN_EEPROM);
			if (val & IWN_EEPROM_READ_VALID)
				break;
			DELAY(5);
		}
		if (ntries == 10) {
			device_printf(sc->sc_dev,
			    "timeout reading ROM at 0x%x\n", addr);
			return ETIMEDOUT;
		}
		if (sc->sc_flags & IWN_FLAG_HAS_OTPROM) {
			/* OTPROM, check for ECC errors. */
			tmp = IWN_READ(sc, IWN_OTP_GP);
			if (tmp & IWN_OTP_GP_ECC_UNCORR_STTS) {
				device_printf(sc->sc_dev,
				    "OTPROM ECC error at 0x%x\n", addr);
				return EIO;
			}
			if (tmp & IWN_OTP_GP_ECC_CORR_STTS) {
				/* Correctable ECC error, clear bit. */
				IWN_SETBITS(sc, IWN_OTP_GP,
				    IWN_OTP_GP_ECC_CORR_STTS);
			}
		}
		*out++ = val >> 16;
		if (count > 1)
			*out++ = val >> 24;
	}

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s end\n", __func__);

	return 0;
}

static void
iwn_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	if (error != 0)
		return;
	KASSERT(nsegs == 1, ("too many DMA segments, %d should be 1", nsegs));
	*(bus_addr_t *)arg = segs[0].ds_addr;
}

static int
iwn_dma_contig_alloc(struct iwn_softc *sc, struct iwn_dma_info *dma,
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
	    iwn_dma_map_addr, &dma->paddr, BUS_DMA_NOWAIT);
	if (error != 0)
		goto fail;

	bus_dmamap_sync(dma->tag, dma->map, BUS_DMASYNC_PREWRITE);

	if (kvap != NULL)
		*kvap = dma->vaddr;

	return 0;

fail:	iwn_dma_contig_free(dma);
	return error;
}

static void
iwn_dma_contig_free(struct iwn_dma_info *dma)
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

static int
iwn_alloc_sched(struct iwn_softc *sc)
{
	/* TX scheduler rings must be aligned on a 1KB boundary. */
	return iwn_dma_contig_alloc(sc, &sc->sched_dma, (void **)&sc->sched,
	    sc->schedsz, 1024);
}

static void
iwn_free_sched(struct iwn_softc *sc)
{
	iwn_dma_contig_free(&sc->sched_dma);
}

static int
iwn_alloc_kw(struct iwn_softc *sc)
{
	/* "Keep Warm" page must be aligned on a 4KB boundary. */
	return iwn_dma_contig_alloc(sc, &sc->kw_dma, NULL, 4096, 4096);
}

static void
iwn_free_kw(struct iwn_softc *sc)
{
	iwn_dma_contig_free(&sc->kw_dma);
}

static int
iwn_alloc_ict(struct iwn_softc *sc)
{
	/* ICT table must be aligned on a 4KB boundary. */
	return iwn_dma_contig_alloc(sc, &sc->ict_dma, (void **)&sc->ict,
	    IWN_ICT_SIZE, 4096);
}

static void
iwn_free_ict(struct iwn_softc *sc)
{
	iwn_dma_contig_free(&sc->ict_dma);
}

static int
iwn_alloc_fwmem(struct iwn_softc *sc)
{
	/* Must be aligned on a 16-byte boundary. */
	return iwn_dma_contig_alloc(sc, &sc->fw_dma, NULL, sc->fwsz, 16);
}

static void
iwn_free_fwmem(struct iwn_softc *sc)
{
	iwn_dma_contig_free(&sc->fw_dma);
}

static int
iwn_alloc_rx_ring(struct iwn_softc *sc, struct iwn_rx_ring *ring)
{
	bus_size_t size;
	int i, error;

	ring->cur = 0;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	/* Allocate RX descriptors (256-byte aligned). */
	size = IWN_RX_RING_COUNT * sizeof (uint32_t);
	error = iwn_dma_contig_alloc(sc, &ring->desc_dma, (void **)&ring->desc,
	    size, 256);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not allocate RX ring DMA memory, error %d\n",
		    __func__, error);
		goto fail;
	}

	/* Allocate RX status area (16-byte aligned). */
	error = iwn_dma_contig_alloc(sc, &ring->stat_dma, (void **)&ring->stat,
	    sizeof (struct iwn_rx_status), 16);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not allocate RX status DMA memory, error %d\n",
		    __func__, error);
		goto fail;
	}

	/* Create RX buffer DMA tag. */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 1, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    IWN_RBUF_SIZE, 1, IWN_RBUF_SIZE, 0, NULL, NULL, &ring->data_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not create RX buf DMA tag, error %d\n",
		    __func__, error);
		goto fail;
	}

	/*
	 * Allocate and map RX buffers.
	 */
	for (i = 0; i < IWN_RX_RING_COUNT; i++) {
		struct iwn_rx_data *data = &ring->data[i];
		bus_addr_t paddr;

		error = bus_dmamap_create(ring->data_dmat, 0, &data->map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not create RX buf DMA map, error %d\n",
			    __func__, error);
			goto fail;
		}

		data->m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR,
		    IWN_RBUF_SIZE);
		if (data->m == NULL) {
			device_printf(sc->sc_dev,
			    "%s: could not allocate RX mbuf\n", __func__);
			error = ENOBUFS;
			goto fail;
		}

		error = bus_dmamap_load(ring->data_dmat, data->map,
		    mtod(data->m, void *), IWN_RBUF_SIZE, iwn_dma_map_addr,
		    &paddr, BUS_DMA_NOWAIT);
		if (error != 0 && error != EFBIG) {
			device_printf(sc->sc_dev,
			    "%s: can't map mbuf, error %d\n", __func__,
			    error);
			goto fail;
		}

		bus_dmamap_sync(ring->data_dmat, data->map,
		    BUS_DMASYNC_PREREAD);

		/* Set physical address of RX buffer (256-byte aligned). */
		ring->desc[i] = htole32(paddr >> 8);
	}

	bus_dmamap_sync(ring->desc_dma.tag, ring->desc_dma.map,
	    BUS_DMASYNC_PREWRITE);

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

	return 0;

fail:	iwn_free_rx_ring(sc, ring);

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end in error\n",__func__);

	return error;
}

static void
iwn_reset_rx_ring(struct iwn_softc *sc, struct iwn_rx_ring *ring)
{
	int ntries;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	if (iwn_nic_lock(sc) == 0) {
		IWN_WRITE(sc, IWN_FH_RX_CONFIG, 0);
		for (ntries = 0; ntries < 1000; ntries++) {
			if (IWN_READ(sc, IWN_FH_RX_STATUS) &
			    IWN_FH_RX_STATUS_IDLE)
				break;
			DELAY(10);
		}
		iwn_nic_unlock(sc);
	}
	ring->cur = 0;
	sc->last_rx_valid = 0;
}

static void
iwn_free_rx_ring(struct iwn_softc *sc, struct iwn_rx_ring *ring)
{
	int i;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s \n", __func__);

	iwn_dma_contig_free(&ring->desc_dma);
	iwn_dma_contig_free(&ring->stat_dma);

	for (i = 0; i < IWN_RX_RING_COUNT; i++) {
		struct iwn_rx_data *data = &ring->data[i];

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
iwn_alloc_tx_ring(struct iwn_softc *sc, struct iwn_tx_ring *ring, int qid)
{
	bus_addr_t paddr;
	bus_size_t size;
	int i, error;

	ring->qid = qid;
	ring->queued = 0;
	ring->cur = 0;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	/* Allocate TX descriptors (256-byte aligned). */
	size = IWN_TX_RING_COUNT * sizeof (struct iwn_tx_desc);
	error = iwn_dma_contig_alloc(sc, &ring->desc_dma, (void **)&ring->desc,
	    size, 256);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not allocate TX ring DMA memory, error %d\n",
		    __func__, error);
		goto fail;
	}

	size = IWN_TX_RING_COUNT * sizeof (struct iwn_tx_cmd);
	error = iwn_dma_contig_alloc(sc, &ring->cmd_dma, (void **)&ring->cmd,
	    size, 4);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not allocate TX cmd DMA memory, error %d\n",
		    __func__, error);
		goto fail;
	}

	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 1, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL, MCLBYTES,
	    IWN_MAX_SCATTER - 1, MCLBYTES, 0, NULL, NULL, &ring->data_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not create TX buf DMA tag, error %d\n",
		    __func__, error);
		goto fail;
	}

	paddr = ring->cmd_dma.paddr;
	for (i = 0; i < IWN_TX_RING_COUNT; i++) {
		struct iwn_tx_data *data = &ring->data[i];

		data->cmd_paddr = paddr;
		data->scratch_paddr = paddr + 12;
		paddr += sizeof (struct iwn_tx_cmd);

		error = bus_dmamap_create(ring->data_dmat, 0, &data->map);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not create TX buf DMA map, error %d\n",
			    __func__, error);
			goto fail;
		}
	}

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s end\n", __func__);

	return 0;

fail:	iwn_free_tx_ring(sc, ring);
	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s end in error\n", __func__);
	return error;
}

static void
iwn_reset_tx_ring(struct iwn_softc *sc, struct iwn_tx_ring *ring)
{
	int i;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->doing %s \n", __func__);

	for (i = 0; i < IWN_TX_RING_COUNT; i++) {
		struct iwn_tx_data *data = &ring->data[i];

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
		data->remapped = 0;
		data->long_retries = 0;
	}
	/* Clear TX descriptors. */
	memset(ring->desc, 0, ring->desc_dma.size);
	bus_dmamap_sync(ring->desc_dma.tag, ring->desc_dma.map,
	    BUS_DMASYNC_PREWRITE);
	sc->qfullmsk &= ~(1 << ring->qid);
	ring->queued = 0;
	ring->cur = 0;
}

static void
iwn_free_tx_ring(struct iwn_softc *sc, struct iwn_tx_ring *ring)
{
	int i;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s \n", __func__);

	iwn_dma_contig_free(&ring->desc_dma);
	iwn_dma_contig_free(&ring->cmd_dma);

	for (i = 0; i < IWN_TX_RING_COUNT; i++) {
		struct iwn_tx_data *data = &ring->data[i];

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

static void
iwn_check_tx_ring(struct iwn_softc *sc, int qid)
{
	struct iwn_tx_ring *ring = &sc->txq[qid];

	KASSERT(ring->queued >= 0, ("%s: ring->queued (%d) for queue %d < 0!",
	    __func__, ring->queued, qid));

	if (qid >= sc->firstaggqueue) {
		struct iwn_ops *ops = &sc->ops;
		struct ieee80211_tx_ampdu *tap = sc->qid2tap[qid];

		if (ring->queued == 0 && !IEEE80211_AMPDU_RUNNING(tap)) {
			uint16_t ssn = tap->txa_start & 0xfff;
			uint8_t tid = tap->txa_tid;
			int *res = tap->txa_private;

			iwn_nic_lock(sc);
			ops->ampdu_tx_stop(sc, qid, tid, ssn);
			iwn_nic_unlock(sc);

			sc->qid2tap[qid] = NULL;
			free(res, M_DEVBUF);
		}
	}

	if (ring->queued < IWN_TX_RING_LOMARK) {
		sc->qfullmsk &= ~(1 << qid);

		if (ring->queued == 0)
			sc->sc_tx_timer = 0;
		else
			sc->sc_tx_timer = 5;
	}
}

static void
iwn5000_ict_reset(struct iwn_softc *sc)
{
	/* Disable interrupts. */
	IWN_WRITE(sc, IWN_INT_MASK, 0);

	/* Reset ICT table. */
	memset(sc->ict, 0, IWN_ICT_SIZE);
	sc->ict_cur = 0;

	bus_dmamap_sync(sc->ict_dma.tag, sc->ict_dma.map,
	    BUS_DMASYNC_PREWRITE);

	/* Set physical address of ICT table (4KB aligned). */
	DPRINTF(sc, IWN_DEBUG_RESET, "%s: enabling ICT\n", __func__);
	IWN_WRITE(sc, IWN_DRAM_INT_TBL, IWN_DRAM_INT_TBL_ENABLE |
	    IWN_DRAM_INT_TBL_WRAP_CHECK | sc->ict_dma.paddr >> 12);

	/* Enable periodic RX interrupt. */
	sc->int_mask |= IWN_INT_RX_PERIODIC;
	/* Switch to ICT interrupt mode in driver. */
	sc->sc_flags |= IWN_FLAG_USE_ICT;

	/* Re-enable interrupts. */
	IWN_WRITE(sc, IWN_INT, 0xffffffff);
	IWN_WRITE(sc, IWN_INT_MASK, sc->int_mask);
}

static int
iwn_read_eeprom(struct iwn_softc *sc, uint8_t macaddr[IEEE80211_ADDR_LEN])
{
	struct iwn_ops *ops = &sc->ops;
	uint16_t val;
	int error;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	/* Check whether adapter has an EEPROM or an OTPROM. */
	if (sc->hw_type >= IWN_HW_REV_TYPE_1000 &&
	    (IWN_READ(sc, IWN_OTP_GP) & IWN_OTP_GP_DEV_SEL_OTP))
		sc->sc_flags |= IWN_FLAG_HAS_OTPROM;
	DPRINTF(sc, IWN_DEBUG_RESET, "%s found\n",
	    (sc->sc_flags & IWN_FLAG_HAS_OTPROM) ? "OTPROM" : "EEPROM");

	/* Adapter has to be powered on for EEPROM access to work. */
	if ((error = iwn_apm_init(sc)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not power ON adapter, error %d\n", __func__,
		    error);
		return error;
	}

	if ((IWN_READ(sc, IWN_EEPROM_GP) & 0x7) == 0) {
		device_printf(sc->sc_dev, "%s: bad ROM signature\n", __func__);
		return EIO;
	}
	if ((error = iwn_eeprom_lock(sc)) != 0) {
		device_printf(sc->sc_dev, "%s: could not lock ROM, error %d\n",
		    __func__, error);
		return error;
	}
	if (sc->sc_flags & IWN_FLAG_HAS_OTPROM) {
		if ((error = iwn_init_otprom(sc)) != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not initialize OTPROM, error %d\n",
			    __func__, error);
			return error;
		}
	}

	iwn_read_prom_data(sc, IWN_EEPROM_SKU_CAP, &val, 2);
	DPRINTF(sc, IWN_DEBUG_RESET, "SKU capabilities=0x%04x\n", le16toh(val));
	/* Check if HT support is bonded out. */
	if (val & htole16(IWN_EEPROM_SKU_CAP_11N))
		sc->sc_flags |= IWN_FLAG_HAS_11N;

	iwn_read_prom_data(sc, IWN_EEPROM_RFCFG, &val, 2);
	sc->rfcfg = le16toh(val);
	DPRINTF(sc, IWN_DEBUG_RESET, "radio config=0x%04x\n", sc->rfcfg);
	/* Read Tx/Rx chains from ROM unless it's known to be broken. */
	if (sc->txchainmask == 0)
		sc->txchainmask = IWN_RFCFG_TXANTMSK(sc->rfcfg);
	if (sc->rxchainmask == 0)
		sc->rxchainmask = IWN_RFCFG_RXANTMSK(sc->rfcfg);

	/* Read MAC address. */
	iwn_read_prom_data(sc, IWN_EEPROM_MAC, macaddr, 6);

	/* Read adapter-specific information from EEPROM. */
	ops->read_eeprom(sc);

	iwn_apm_stop(sc);	/* Power OFF adapter. */

	iwn_eeprom_unlock(sc);

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s end\n", __func__);

	return 0;
}

static void
iwn4965_read_eeprom(struct iwn_softc *sc)
{
	uint32_t addr;
	uint16_t val;
	int i;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	/* Read regulatory domain (4 ASCII characters). */
	iwn_read_prom_data(sc, IWN4965_EEPROM_DOMAIN, sc->eeprom_domain, 4);

	/* Read the list of authorized channels (20MHz & 40MHz). */
	for (i = 0; i < IWN_NBANDS - 1; i++) {
		addr = iwn4965_regulatory_bands[i];
		iwn_read_eeprom_channels(sc, i, addr);
	}

	/* Read maximum allowed TX power for 2GHz and 5GHz bands. */
	iwn_read_prom_data(sc, IWN4965_EEPROM_MAXPOW, &val, 2);
	sc->maxpwr2GHz = val & 0xff;
	sc->maxpwr5GHz = val >> 8;
	/* Check that EEPROM values are within valid range. */
	if (sc->maxpwr5GHz < 20 || sc->maxpwr5GHz > 50)
		sc->maxpwr5GHz = 38;
	if (sc->maxpwr2GHz < 20 || sc->maxpwr2GHz > 50)
		sc->maxpwr2GHz = 38;
	DPRINTF(sc, IWN_DEBUG_RESET, "maxpwr 2GHz=%d 5GHz=%d\n",
	    sc->maxpwr2GHz, sc->maxpwr5GHz);

	/* Read samples for each TX power group. */
	iwn_read_prom_data(sc, IWN4965_EEPROM_BANDS, sc->bands,
	    sizeof sc->bands);

	/* Read voltage at which samples were taken. */
	iwn_read_prom_data(sc, IWN4965_EEPROM_VOLTAGE, &val, 2);
	sc->eeprom_voltage = (int16_t)le16toh(val);
	DPRINTF(sc, IWN_DEBUG_RESET, "voltage=%d (in 0.3V)\n",
	    sc->eeprom_voltage);

#ifdef IWN_DEBUG
	/* Print samples. */
	if (sc->sc_debug & IWN_DEBUG_ANY) {
		for (i = 0; i < IWN_NBANDS - 1; i++)
			iwn4965_print_power_group(sc, i);
	}
#endif

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s end\n", __func__);
}

#ifdef IWN_DEBUG
static void
iwn4965_print_power_group(struct iwn_softc *sc, int i)
{
	struct iwn4965_eeprom_band *band = &sc->bands[i];
	struct iwn4965_eeprom_chan_samples *chans = band->chans;
	int j, c;

	printf("===band %d===\n", i);
	printf("chan lo=%d, chan hi=%d\n", band->lo, band->hi);
	printf("chan1 num=%d\n", chans[0].num);
	for (c = 0; c < 2; c++) {
		for (j = 0; j < IWN_NSAMPLES; j++) {
			printf("chain %d, sample %d: temp=%d gain=%d "
			    "power=%d pa_det=%d\n", c, j,
			    chans[0].samples[c][j].temp,
			    chans[0].samples[c][j].gain,
			    chans[0].samples[c][j].power,
			    chans[0].samples[c][j].pa_det);
		}
	}
	printf("chan2 num=%d\n", chans[1].num);
	for (c = 0; c < 2; c++) {
		for (j = 0; j < IWN_NSAMPLES; j++) {
			printf("chain %d, sample %d: temp=%d gain=%d "
			    "power=%d pa_det=%d\n", c, j,
			    chans[1].samples[c][j].temp,
			    chans[1].samples[c][j].gain,
			    chans[1].samples[c][j].power,
			    chans[1].samples[c][j].pa_det);
		}
	}
}
#endif

static void
iwn5000_read_eeprom(struct iwn_softc *sc)
{
	struct iwn5000_eeprom_calib_hdr hdr;
	int32_t volt;
	uint32_t base, addr;
	uint16_t val;
	int i;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	/* Read regulatory domain (4 ASCII characters). */
	iwn_read_prom_data(sc, IWN5000_EEPROM_REG, &val, 2);
	base = le16toh(val);
	iwn_read_prom_data(sc, base + IWN5000_EEPROM_DOMAIN,
	    sc->eeprom_domain, 4);

	/* Read the list of authorized channels (20MHz & 40MHz). */
	for (i = 0; i < IWN_NBANDS - 1; i++) {
		addr =  base + sc->base_params->regulatory_bands[i];
		iwn_read_eeprom_channels(sc, i, addr);
	}

	/* Read enhanced TX power information for 6000 Series. */
	if (sc->base_params->enhanced_TX_power)
		iwn_read_eeprom_enhinfo(sc);

	iwn_read_prom_data(sc, IWN5000_EEPROM_CAL, &val, 2);
	base = le16toh(val);
	iwn_read_prom_data(sc, base, &hdr, sizeof hdr);
	DPRINTF(sc, IWN_DEBUG_CALIBRATE,
	    "%s: calib version=%u pa type=%u voltage=%u\n", __func__,
	    hdr.version, hdr.pa_type, le16toh(hdr.volt));
	sc->calib_ver = hdr.version;

	if (sc->base_params->calib_need & IWN_FLG_NEED_PHY_CALIB_TEMP_OFFSETv2) {
		sc->eeprom_voltage = le16toh(hdr.volt);
		iwn_read_prom_data(sc, base + IWN5000_EEPROM_TEMP, &val, 2);
		sc->eeprom_temp_high=le16toh(val);
		iwn_read_prom_data(sc, base + IWN5000_EEPROM_VOLT, &val, 2);
		sc->eeprom_temp = le16toh(val);
	}

	if (sc->hw_type == IWN_HW_REV_TYPE_5150) {
		/* Compute temperature offset. */
		iwn_read_prom_data(sc, base + IWN5000_EEPROM_TEMP, &val, 2);
		sc->eeprom_temp = le16toh(val);
		iwn_read_prom_data(sc, base + IWN5000_EEPROM_VOLT, &val, 2);
		volt = le16toh(val);
		sc->temp_off = sc->eeprom_temp - (volt / -5);
		DPRINTF(sc, IWN_DEBUG_CALIBRATE, "temp=%d volt=%d offset=%dK\n",
		    sc->eeprom_temp, volt, sc->temp_off);
	} else {
		/* Read crystal calibration. */
		iwn_read_prom_data(sc, base + IWN5000_EEPROM_CRYSTAL,
		    &sc->eeprom_crystal, sizeof (uint32_t));
		DPRINTF(sc, IWN_DEBUG_CALIBRATE, "crystal calibration 0x%08x\n",
		    le32toh(sc->eeprom_crystal));
	}

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s end\n", __func__);

}

/*
 * Translate EEPROM flags to net80211.
 */
static uint32_t
iwn_eeprom_channel_flags(struct iwn_eeprom_chan *channel)
{
	uint32_t nflags;

	nflags = 0;
	if ((channel->flags & IWN_EEPROM_CHAN_ACTIVE) == 0)
		nflags |= IEEE80211_CHAN_PASSIVE;
	if ((channel->flags & IWN_EEPROM_CHAN_IBSS) == 0)
		nflags |= IEEE80211_CHAN_NOADHOC;
	if (channel->flags & IWN_EEPROM_CHAN_RADAR) {
		nflags |= IEEE80211_CHAN_DFS;
		/* XXX apparently IBSS may still be marked */
		nflags |= IEEE80211_CHAN_NOADHOC;
	}

	return nflags;
}

static void
iwn_read_eeprom_band(struct iwn_softc *sc, int n, int maxchans, int *nchans,
    struct ieee80211_channel chans[])
{
	struct iwn_eeprom_chan *channels = sc->eeprom_channels[n];
	const struct iwn_chan_band *band = &iwn_bands[n];
	uint8_t bands[IEEE80211_MODE_BYTES];
	uint8_t chan;
	int i, error, nflags;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	memset(bands, 0, sizeof(bands));
	if (n == 0) {
		setbit(bands, IEEE80211_MODE_11B);
		setbit(bands, IEEE80211_MODE_11G);
		if (sc->sc_flags & IWN_FLAG_HAS_11N)
			setbit(bands, IEEE80211_MODE_11NG);
	} else {
		setbit(bands, IEEE80211_MODE_11A);
		if (sc->sc_flags & IWN_FLAG_HAS_11N)
			setbit(bands, IEEE80211_MODE_11NA);
	}

	for (i = 0; i < band->nchan; i++) {
		if (!(channels[i].flags & IWN_EEPROM_CHAN_VALID)) {
			DPRINTF(sc, IWN_DEBUG_RESET,
			    "skip chan %d flags 0x%x maxpwr %d\n",
			    band->chan[i], channels[i].flags,
			    channels[i].maxpwr);
			continue;
		}

		chan = band->chan[i];
		nflags = iwn_eeprom_channel_flags(&channels[i]);
		error = ieee80211_add_channel(chans, maxchans, nchans,
		    chan, 0, channels[i].maxpwr, nflags, bands);
		if (error != 0)
			break;

		/* Save maximum allowed TX power for this channel. */
		/* XXX wrong */
		sc->maxpwr[chan] = channels[i].maxpwr;

		DPRINTF(sc, IWN_DEBUG_RESET,
		    "add chan %d flags 0x%x maxpwr %d\n", chan,
		    channels[i].flags, channels[i].maxpwr);
	}

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s end\n", __func__);

}

static void
iwn_read_eeprom_ht40(struct iwn_softc *sc, int n, int maxchans, int *nchans,
    struct ieee80211_channel chans[])
{
	struct iwn_eeprom_chan *channels = sc->eeprom_channels[n];
	const struct iwn_chan_band *band = &iwn_bands[n];
	uint8_t chan;
	int i, error, nflags;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s start\n", __func__);

	if (!(sc->sc_flags & IWN_FLAG_HAS_11N)) {
		DPRINTF(sc, IWN_DEBUG_TRACE, "->%s end no 11n\n", __func__);
		return;
	}

	for (i = 0; i < band->nchan; i++) {
		if (!(channels[i].flags & IWN_EEPROM_CHAN_VALID)) {
			DPRINTF(sc, IWN_DEBUG_RESET,
			    "skip chan %d flags 0x%x maxpwr %d\n",
			    band->chan[i], channels[i].flags,
			    channels[i].maxpwr);
			continue;
		}

		chan = band->chan[i];
		nflags = iwn_eeprom_channel_flags(&channels[i]);
		nflags |= (n == 5 ? IEEE80211_CHAN_G : IEEE80211_CHAN_A);
		error = ieee80211_add_channel_ht40(chans, maxchans, nchans,
		    chan, channels[i].maxpwr, nflags);
		switch (error) {
		case EINVAL:
			device_printf(sc->sc_dev,
			    "%s: no entry for channel %d\n", __func__, chan);
			continue;
		case ENOENT:
			DPRINTF(sc, IWN_DEBUG_RESET,
			    "%s: skip chan %d, extension channel not found\n",
			    __func__, chan);
			continue;
		case ENOBUFS:
			device_printf(sc->sc_dev,
			    "%s: channel table is full!\n", __func__);
			break;
		case 0:
			DPRINTF(sc, IWN_DEBUG_RESET,
			    "add ht40 chan %d flags 0x%x maxpwr %d\n",
			    chan, channels[i].flags, channels[i].maxpwr);
			/* FALLTHROUGH */
		default:
			break;
		}
	}

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s end\n", __func__);

}

static void
iwn_read_eeprom_channels(struct iwn_softc *sc, int n, uint32_t addr)
{
	struct ieee80211com *ic = &sc->sc_ic;

	iwn_read_prom_data(sc, addr, &sc->eeprom_channels[n],
	    iwn_bands[n].nchan * sizeof (struct iwn_eeprom_chan));

	if (n < 5) {
		iwn_read_eeprom_band(sc, n, IEEE80211_CHAN_MAX, &ic->ic_nchans,
		    ic->ic_channels);
	} else {
		iwn_read_eeprom_ht40(sc, n, IEEE80211_CHAN_MAX, &ic->ic_nchans,
		    ic->ic_channels);
	}
	ieee80211_sort_channels(ic->ic_channels, ic->ic_nchans);
}

static struct iwn_eeprom_chan *
iwn_find_eeprom_channel(struct iwn_softc *sc, struct ieee80211_channel *c)
{
	int band, chan, i, j;

	if (IEEE80211_IS_CHAN_HT40(c)) {
		band = IEEE80211_IS_CHAN_5GHZ(c) ? 6 : 5;
		if (IEEE80211_IS_CHAN_HT40D(c))
			chan = c->ic_extieee;
		else
			chan = c->ic_ieee;
		for (i = 0; i < iwn_bands[band].nchan; i++) {
			if (iwn_bands[band].chan[i] == chan)
				return &sc->eeprom_channels[band][i];
		}
	} else {
		for (j = 0; j < 5; j++) {
			for (i = 0; i < iwn_bands[j].nchan; i++) {
				if (iwn_bands[j].chan[i] == c->ic_ieee &&
				    ((j == 0) ^ IEEE80211_IS_CHAN_A(c)) == 1)
					return &sc->eeprom_channels[j][i];
			}
		}
	}
	return NULL;
}

static void
iwn_getradiocaps(struct ieee80211com *ic,
    int maxchans, int *nchans, struct ieee80211_channel chans[])
{
	struct iwn_softc *sc = ic->ic_softc;
	int i;

	/* Parse the list of authorized channels. */
	for (i = 0; i < 5 && *nchans < maxchans; i++)
		iwn_read_eeprom_band(sc, i, maxchans, nchans, chans);
	for (i = 5; i < IWN_NBANDS - 1 && *nchans < maxchans; i++)
		iwn_read_eeprom_ht40(sc, i, maxchans, nchans, chans);
}

/*
 * Enforce flags read from EEPROM.
 */
static int
iwn_setregdomain(struct ieee80211com *ic, struct ieee80211_regdomain *rd,
    int nchan, struct ieee80211_channel chans[])
{
	struct iwn_softc *sc = ic->ic_softc;
	int i;

	for (i = 0; i < nchan; i++) {
		struct ieee80211_channel *c = &chans[i];
		struct iwn_eeprom_chan *channel;

		channel = iwn_find_eeprom_channel(sc, c);
		if (channel == NULL) {
			ic_printf(ic, "%s: invalid channel %u freq %u/0x%x\n",
			    __func__, c->ic_ieee, c->ic_freq, c->ic_flags);
			return EINVAL;
		}
		c->ic_flags |= iwn_eeprom_channel_flags(channel);
	}

	return 0;
}

static void
iwn_read_eeprom_enhinfo(struct iwn_softc *sc)
{
	struct iwn_eeprom_enhinfo enhinfo[35];
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *c;
	uint16_t val, base;
	int8_t maxpwr;
	uint8_t flags;
	int i, j;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	iwn_read_prom_data(sc, IWN5000_EEPROM_REG, &val, 2);
	base = le16toh(val);
	iwn_read_prom_data(sc, base + IWN6000_EEPROM_ENHINFO,
	    enhinfo, sizeof enhinfo);

	for (i = 0; i < nitems(enhinfo); i++) {
		flags = enhinfo[i].flags;
		if (!(flags & IWN_ENHINFO_VALID))
			continue;	/* Skip invalid entries. */

		maxpwr = 0;
		if (sc->txchainmask & IWN_ANT_A)
			maxpwr = MAX(maxpwr, enhinfo[i].chain[0]);
		if (sc->txchainmask & IWN_ANT_B)
			maxpwr = MAX(maxpwr, enhinfo[i].chain[1]);
		if (sc->txchainmask & IWN_ANT_C)
			maxpwr = MAX(maxpwr, enhinfo[i].chain[2]);
		if (sc->ntxchains == 2)
			maxpwr = MAX(maxpwr, enhinfo[i].mimo2);
		else if (sc->ntxchains == 3)
			maxpwr = MAX(maxpwr, enhinfo[i].mimo3);

		for (j = 0; j < ic->ic_nchans; j++) {
			c = &ic->ic_channels[j];
			if ((flags & IWN_ENHINFO_5GHZ)) {
				if (!IEEE80211_IS_CHAN_A(c))
					continue;
			} else if ((flags & IWN_ENHINFO_OFDM)) {
				if (!IEEE80211_IS_CHAN_G(c))
					continue;
			} else if (!IEEE80211_IS_CHAN_B(c))
				continue;
			if ((flags & IWN_ENHINFO_HT40)) {
				if (!IEEE80211_IS_CHAN_HT40(c))
					continue;
			} else {
				if (IEEE80211_IS_CHAN_HT40(c))
					continue;
			}
			if (enhinfo[i].chan != 0 &&
			    enhinfo[i].chan != c->ic_ieee)
				continue;

			DPRINTF(sc, IWN_DEBUG_RESET,
			    "channel %d(%x), maxpwr %d\n", c->ic_ieee,
			    c->ic_flags, maxpwr / 2);
			c->ic_maxregpower = maxpwr / 2;
			c->ic_maxpower = maxpwr;
		}
	}

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s end\n", __func__);

}

static struct ieee80211_node *
iwn_node_alloc(struct ieee80211vap *vap, const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct iwn_node *wn;

	wn = malloc(sizeof (struct iwn_node), M_80211_NODE, M_NOWAIT | M_ZERO);
	if (wn == NULL)
		return (NULL);

	wn->id = IWN_ID_UNDEFINED;

	return (&wn->ni);
}

static __inline int
rate2plcp(int rate)
{
	switch (rate & 0xff) {
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
	}
	return 0;
}

static __inline uint8_t
plcp2rate(const uint8_t rate_plcp)
{
	switch (rate_plcp) {
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

static int
iwn_get_1stream_tx_antmask(struct iwn_softc *sc)
{

	return IWN_LSB(sc->txchainmask);
}

static int
iwn_get_2stream_tx_antmask(struct iwn_softc *sc)
{
	int tx;

	/*
	 * The '2 stream' setup is a bit .. odd.
	 *
	 * For NICs that support only 1 antenna, default to IWN_ANT_AB or
	 * the firmware panics (eg Intel 5100.)
	 *
	 * For NICs that support two antennas, we use ANT_AB.
	 *
	 * For NICs that support three antennas, we use the two that
	 * wasn't the default one.
	 *
	 * XXX TODO: if bluetooth (full concurrent) is enabled, restrict
	 * this to only one antenna.
	 */

	/* Default - transmit on the other antennas */
	tx = (sc->txchainmask & ~IWN_LSB(sc->txchainmask));

	/* Now, if it's zero, set it to IWN_ANT_AB, so to not panic firmware */
	if (tx == 0)
		tx = IWN_ANT_AB;

	/*
	 * If the NIC is a two-stream TX NIC, configure the TX mask to
	 * the default chainmask
	 */
	else if (sc->ntxchains == 2)
		tx = sc->txchainmask;

	return (tx);
}



/*
 * Calculate the required PLCP value from the given rate,
 * to the given node.
 *
 * This will take the node configuration (eg 11n, rate table
 * setup, etc) into consideration.
 */
static uint32_t
iwn_rate_to_plcp(struct iwn_softc *sc, struct ieee80211_node *ni,
    uint8_t rate)
{
	struct ieee80211com *ic = ni->ni_ic;
	uint32_t plcp = 0;
	int ridx;

	/*
	 * If it's an MCS rate, let's set the plcp correctly
	 * and set the relevant flags based on the node config.
	 */
	if (rate & IEEE80211_RATE_MCS) {
		/*
		 * Set the initial PLCP value to be between 0->31 for
		 * MCS 0 -> MCS 31, then set the "I'm an MCS rate!"
		 * flag.
		 */
		plcp = IEEE80211_RV(rate) | IWN_RFLAG_MCS;

		/*
		 * XXX the following should only occur if both
		 * the local configuration _and_ the remote node
		 * advertise these capabilities.  Thus this code
		 * may need fixing!
		 */

		/*
		 * Set the channel width and guard interval.
		 */
		if (IEEE80211_IS_CHAN_HT40(ni->ni_chan)) {
			plcp |= IWN_RFLAG_HT40;
			if (ni->ni_htcap & IEEE80211_HTCAP_SHORTGI40)
				plcp |= IWN_RFLAG_SGI;
		} else if (ni->ni_htcap & IEEE80211_HTCAP_SHORTGI20) {
			plcp |= IWN_RFLAG_SGI;
		}

		/*
		 * Ensure the selected rate matches the link quality
		 * table entries being used.
		 */
		if (rate > 0x8f)
			plcp |= IWN_RFLAG_ANT(sc->txchainmask);
		else if (rate > 0x87)
			plcp |= IWN_RFLAG_ANT(iwn_get_2stream_tx_antmask(sc));
		else
			plcp |= IWN_RFLAG_ANT(iwn_get_1stream_tx_antmask(sc));
	} else {
		/*
		 * Set the initial PLCP - fine for both
		 * OFDM and CCK rates.
		 */
		plcp = rate2plcp(rate);

		/* Set CCK flag if it's CCK */

		/* XXX It would be nice to have a method
		 * to map the ridx -> phy table entry
		 * so we could just query that, rather than
		 * this hack to check against IWN_RIDX_OFDM6.
		 */
		ridx = ieee80211_legacy_rate_lookup(ic->ic_rt,
		    rate & IEEE80211_RATE_VAL);
		if (ridx < IWN_RIDX_OFDM6 &&
		    IEEE80211_IS_CHAN_2GHZ(ni->ni_chan))
			plcp |= IWN_RFLAG_CCK;

		/* Set antenna configuration */
		/* XXX TODO: is this the right antenna to use for legacy? */
		plcp |= IWN_RFLAG_ANT(iwn_get_1stream_tx_antmask(sc));
	}

	DPRINTF(sc, IWN_DEBUG_TXRATE, "%s: rate=0x%02x, plcp=0x%08x\n",
	    __func__,
	    rate,
	    plcp);

	return (htole32(plcp));
}

static void
iwn_newassoc(struct ieee80211_node *ni, int isnew)
{
	/* Doesn't do anything at the moment */
}

static int
iwn_media_change(struct ifnet *ifp)
{
	int error;

	error = ieee80211_media_change(ifp);
	/* NB: only the fixed rate can change and that doesn't need a reset */
	return (error == ENETRESET ? 0 : error);
}

static int
iwn_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct iwn_vap *ivp = IWN_VAP(vap);
	struct ieee80211com *ic = vap->iv_ic;
	struct iwn_softc *sc = ic->ic_softc;
	int error = 0;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	DPRINTF(sc, IWN_DEBUG_STATE, "%s: %s -> %s\n", __func__,
	    ieee80211_state_name[vap->iv_state], ieee80211_state_name[nstate]);

	IEEE80211_UNLOCK(ic);
	IWN_LOCK(sc);
	callout_stop(&sc->calib_to);

	sc->rxon = &sc->rx_on[IWN_RXON_BSS_CTX];

	switch (nstate) {
	case IEEE80211_S_ASSOC:
		if (vap->iv_state != IEEE80211_S_RUN)
			break;
		/* FALLTHROUGH */
	case IEEE80211_S_AUTH:
		if (vap->iv_state == IEEE80211_S_AUTH)
			break;

		/*
		 * !AUTH -> AUTH transition requires state reset to handle
		 * reassociations correctly.
		 */
		sc->rxon->associd = 0;
		sc->rxon->filter &= ~htole32(IWN_FILTER_BSS);
		sc->calib.state = IWN_CALIB_STATE_INIT;

		/* Wait until we hear a beacon before we transmit */
		if (IEEE80211_IS_CHAN_PASSIVE(ic->ic_curchan))
			sc->sc_beacon_wait = 1;

		if ((error = iwn_auth(sc, vap)) != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not move to auth state\n", __func__);
		}
		break;

	case IEEE80211_S_RUN:
		/*
		 * RUN -> RUN transition; Just restart the timers.
		 */
		if (vap->iv_state == IEEE80211_S_RUN) {
			sc->calib_cnt = 0;
			break;
		}

		/* Wait until we hear a beacon before we transmit */
		if (IEEE80211_IS_CHAN_PASSIVE(ic->ic_curchan))
			sc->sc_beacon_wait = 1;

		/*
		 * !RUN -> RUN requires setting the association id
		 * which is done with a firmware cmd.  We also defer
		 * starting the timers until that work is done.
		 */
		if ((error = iwn_run(sc, vap)) != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not move to run state\n", __func__);
		}
		break;

	case IEEE80211_S_INIT:
		sc->calib.state = IWN_CALIB_STATE_INIT;
		/*
		 * Purge the xmit queue so we don't have old frames
		 * during a new association attempt.
		 */
		sc->sc_beacon_wait = 0;
		iwn_xmit_queue_drain(sc);
		break;

	default:
		break;
	}
	IWN_UNLOCK(sc);
	IEEE80211_LOCK(ic);
	if (error != 0){
		DPRINTF(sc, IWN_DEBUG_TRACE, "->%s end in error\n", __func__);
		return error;
	}

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

	return ivp->iv_newstate(vap, nstate, arg);
}

static void
iwn_calib_timeout(void *arg)
{
	struct iwn_softc *sc = arg;

	IWN_LOCK_ASSERT(sc);

	/* Force automatic TX power calibration every 60 secs. */
	if (++sc->calib_cnt >= 120) {
		uint32_t flags = 0;

		DPRINTF(sc, IWN_DEBUG_CALIBRATE, "%s\n",
		    "sending request for statistics");
		(void)iwn_cmd(sc, IWN_CMD_GET_STATISTICS, &flags,
		    sizeof flags, 1);
		sc->calib_cnt = 0;
	}
	callout_reset(&sc->calib_to, msecs_to_ticks(500), iwn_calib_timeout,
	    sc);
}

/*
 * Process an RX_PHY firmware notification.  This is usually immediately
 * followed by an MPDU_RX_DONE notification.
 */
static void
iwn_rx_phy(struct iwn_softc *sc, struct iwn_rx_desc *desc)
{
	struct iwn_rx_stat *stat = (struct iwn_rx_stat *)(desc + 1);

	DPRINTF(sc, IWN_DEBUG_CALIBRATE, "%s: received PHY stats\n", __func__);

	/* Save RX statistics, they will be used on MPDU_RX_DONE. */
	memcpy(&sc->last_rx_stat, stat, sizeof (*stat));
	sc->last_rx_valid = 1;
}

/*
 * Process an RX_DONE (4965AGN only) or MPDU_RX_DONE firmware notification.
 * Each MPDU_RX_DONE notification must be preceded by an RX_PHY one.
 */
static void
iwn_rx_done(struct iwn_softc *sc, struct iwn_rx_desc *desc,
    struct iwn_rx_data *data)
{
	struct iwn_ops *ops = &sc->ops;
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwn_rx_ring *ring = &sc->rxq;
	struct ieee80211_frame_min *wh;
	struct ieee80211_node *ni;
	struct mbuf *m, *m1;
	struct iwn_rx_stat *stat;
	caddr_t head;
	bus_addr_t paddr;
	uint32_t flags;
	int error, len, rssi, nf;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	if (desc->type == IWN_MPDU_RX_DONE) {
		/* Check for prior RX_PHY notification. */
		if (!sc->last_rx_valid) {
			DPRINTF(sc, IWN_DEBUG_ANY,
			    "%s: missing RX_PHY\n", __func__);
			return;
		}
		stat = &sc->last_rx_stat;
	} else
		stat = (struct iwn_rx_stat *)(desc + 1);

	if (stat->cfg_phy_len > IWN_STAT_MAXLEN) {
		device_printf(sc->sc_dev,
		    "%s: invalid RX statistic header, len %d\n", __func__,
		    stat->cfg_phy_len);
		return;
	}
	if (desc->type == IWN_MPDU_RX_DONE) {
		struct iwn_rx_mpdu *mpdu = (struct iwn_rx_mpdu *)(desc + 1);
		head = (caddr_t)(mpdu + 1);
		len = le16toh(mpdu->len);
	} else {
		head = (caddr_t)(stat + 1) + stat->cfg_phy_len;
		len = le16toh(stat->len);
	}

	flags = le32toh(*(uint32_t *)(head + len));

	/* Discard frames with a bad FCS early. */
	if ((flags & IWN_RX_NOERROR) != IWN_RX_NOERROR) {
		DPRINTF(sc, IWN_DEBUG_RECV, "%s: RX flags error %x\n",
		    __func__, flags);
		counter_u64_add(ic->ic_ierrors, 1);
		return;
	}
	/* Discard frames that are too short. */
	if (len < sizeof (struct ieee80211_frame_ack)) {
		DPRINTF(sc, IWN_DEBUG_RECV, "%s: frame too short: %d\n",
		    __func__, len);
		counter_u64_add(ic->ic_ierrors, 1);
		return;
	}

	m1 = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, IWN_RBUF_SIZE);
	if (m1 == NULL) {
		DPRINTF(sc, IWN_DEBUG_ANY, "%s: no mbuf to restock ring\n",
		    __func__);
		counter_u64_add(ic->ic_ierrors, 1);
		return;
	}
	bus_dmamap_unload(ring->data_dmat, data->map);

	error = bus_dmamap_load(ring->data_dmat, data->map, mtod(m1, void *),
	    IWN_RBUF_SIZE, iwn_dma_map_addr, &paddr, BUS_DMA_NOWAIT);
	if (error != 0 && error != EFBIG) {
		device_printf(sc->sc_dev,
		    "%s: bus_dmamap_load failed, error %d\n", __func__, error);
		m_freem(m1);

		/* Try to reload the old mbuf. */
		error = bus_dmamap_load(ring->data_dmat, data->map,
		    mtod(data->m, void *), IWN_RBUF_SIZE, iwn_dma_map_addr,
		    &paddr, BUS_DMA_NOWAIT);
		if (error != 0 && error != EFBIG) {
			panic("%s: could not load old RX mbuf", __func__);
		}
		bus_dmamap_sync(ring->data_dmat, data->map,
		    BUS_DMASYNC_PREREAD);
		/* Physical address may have changed. */
		ring->desc[ring->cur] = htole32(paddr >> 8);
		bus_dmamap_sync(ring->desc_dma.tag, ring->desc_dma.map,
		    BUS_DMASYNC_PREWRITE);
		counter_u64_add(ic->ic_ierrors, 1);
		return;
	}

	bus_dmamap_sync(ring->data_dmat, data->map,
	    BUS_DMASYNC_PREREAD);

	m = data->m;
	data->m = m1;
	/* Update RX descriptor. */
	ring->desc[ring->cur] = htole32(paddr >> 8);
	bus_dmamap_sync(ring->desc_dma.tag, ring->desc_dma.map,
	    BUS_DMASYNC_PREWRITE);

	/* Finalize mbuf. */
	m->m_data = head;
	m->m_pkthdr.len = m->m_len = len;

	/* Grab a reference to the source node. */
	wh = mtod(m, struct ieee80211_frame_min *);
	if (len >= sizeof(struct ieee80211_frame_min))
		ni = ieee80211_find_rxnode(ic, wh);
	else
		ni = NULL;
	nf = (ni != NULL && ni->ni_vap->iv_state == IEEE80211_S_RUN &&
	    (ic->ic_flags & IEEE80211_F_SCAN) == 0) ? sc->noise : -95;

	rssi = ops->get_rssi(sc, stat);

	if (ieee80211_radiotap_active(ic)) {
		struct iwn_rx_radiotap_header *tap = &sc->sc_rxtap;
		uint32_t rate = le32toh(stat->rate);

		tap->wr_flags = 0;
		if (stat->flags & htole16(IWN_STAT_FLAG_SHPREAMBLE))
			tap->wr_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
		tap->wr_dbm_antsignal = (int8_t)rssi;
		tap->wr_dbm_antnoise = (int8_t)nf;
		tap->wr_tsft = stat->tstamp;
		if (rate & IWN_RFLAG_MCS) {
			tap->wr_rate = rate & IWN_RFLAG_RATE_MCS;
			tap->wr_rate |= IEEE80211_RATE_MCS;
		} else
			tap->wr_rate = plcp2rate(rate & IWN_RFLAG_RATE);
	}

	/*
	 * If it's a beacon and we're waiting, then do the
	 * wakeup.  This should unblock raw_xmit/start.
	 */
	if (sc->sc_beacon_wait) {
		uint8_t type, subtype;
		/* NB: Re-assign wh */
		wh = mtod(m, struct ieee80211_frame_min *);
		type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
		subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
		/*
		 * This assumes at this point we've received our own
		 * beacon.
		 */
		DPRINTF(sc, IWN_DEBUG_TRACE,
		    "%s: beacon_wait, type=%d, subtype=%d\n",
		    __func__, type, subtype);
		if (type == IEEE80211_FC0_TYPE_MGT &&
		    subtype == IEEE80211_FC0_SUBTYPE_BEACON) {
			DPRINTF(sc, IWN_DEBUG_TRACE | IWN_DEBUG_XMIT,
			    "%s: waking things up\n", __func__);
			/* queue taskqueue to transmit! */
			taskqueue_enqueue(sc->sc_tq, &sc->sc_xmit_task);
		}
	}

	IWN_UNLOCK(sc);

	/* Send the frame to the 802.11 layer. */
	if (ni != NULL) {
		if (ni->ni_flags & IEEE80211_NODE_HT)
			m->m_flags |= M_AMPDU;
		(void)ieee80211_input(ni, m, rssi - nf, nf);
		/* Node is no longer needed. */
		ieee80211_free_node(ni);
	} else
		(void)ieee80211_input_all(ic, m, rssi - nf, nf);

	IWN_LOCK(sc);

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

}

static void
iwn_agg_tx_complete(struct iwn_softc *sc, struct iwn_tx_ring *ring, int tid,
    int idx, int success)
{
	struct ieee80211_ratectl_tx_status *txs = &sc->sc_txs;
	struct iwn_tx_data *data = &ring->data[idx];
	struct iwn_node *wn;
	struct mbuf *m;
	struct ieee80211_node *ni;

	KASSERT(data->ni != NULL, ("idx %d: no node", idx));
	KASSERT(data->m != NULL, ("idx %d: no mbuf", idx));

	/* Unmap and free mbuf. */
	bus_dmamap_sync(ring->data_dmat, data->map,
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(ring->data_dmat, data->map);
	m = data->m, data->m = NULL;
	ni = data->ni, data->ni = NULL;
	wn = (void *)ni;

#if 0
	/* XXX causes significant performance degradation. */
	txs->flags = IEEE80211_RATECTL_STATUS_SHORT_RETRY |
		     IEEE80211_RATECTL_STATUS_LONG_RETRY;
	txs->long_retries = data->long_retries - 1;
#else
	txs->flags = IEEE80211_RATECTL_STATUS_SHORT_RETRY;
#endif
	txs->short_retries = wn->agg[tid].short_retries;
	if (success)
		txs->status = IEEE80211_RATECTL_TX_SUCCESS;
	else
		txs->status = IEEE80211_RATECTL_TX_FAIL_UNSPECIFIED;

	wn->agg[tid].short_retries = 0;
	data->long_retries = 0;

	DPRINTF(sc, IWN_DEBUG_AMPDU, "%s: freeing m %p ni %p idx %d qid %d\n",
	    __func__, m, ni, idx, ring->qid);
	ieee80211_ratectl_tx_complete(ni, txs);
	ieee80211_tx_complete(ni, m, !success);
}

/* Process an incoming Compressed BlockAck. */
static void
iwn_rx_compressed_ba(struct iwn_softc *sc, struct iwn_rx_desc *desc)
{
	struct iwn_tx_ring *ring;
	struct iwn_tx_data *data;
	struct iwn_node *wn;
	struct iwn_compressed_ba *ba = (struct iwn_compressed_ba *)(desc + 1);
	struct ieee80211_tx_ampdu *tap;
	uint64_t bitmap;
	uint8_t tid;
	int i, qid, shift;
	int tx_ok = 0;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	qid = le16toh(ba->qid);
	tap = sc->qid2tap[qid];
	ring = &sc->txq[qid];
	tid = tap->txa_tid;
	wn = (void *)tap->txa_ni;

	DPRINTF(sc, IWN_DEBUG_AMPDU, "%s: qid %d tid %d seq %04X ssn %04X\n"
	    "bitmap: ba %016jX wn %016jX, start %d\n",
	    __func__, qid, tid, le16toh(ba->seq), le16toh(ba->ssn),
	    (uintmax_t)le64toh(ba->bitmap), (uintmax_t)wn->agg[tid].bitmap,
	    wn->agg[tid].startidx);

	if (wn->agg[tid].bitmap == 0)
		return;

	shift = wn->agg[tid].startidx - ((le16toh(ba->seq) >> 4) & 0xff);
	if (shift <= -64)
		shift += 0x100;

	/*
	 * Walk the bitmap and calculate how many successful attempts
	 * are made.
	 *
	 * Yes, the rate control code doesn't know these are A-MPDU
	 * subframes; due to that long_retries stats are not used here.
	 */
	bitmap = le64toh(ba->bitmap);
	if (shift >= 0)
		bitmap >>= shift;
	else
		bitmap <<= -shift;
	bitmap &= wn->agg[tid].bitmap;
	wn->agg[tid].bitmap = 0;

	for (i = wn->agg[tid].startidx;
	     bitmap;
	     bitmap >>= 1, i = (i + 1) % IWN_TX_RING_COUNT) {
		if ((bitmap & 1) == 0)
			continue;

		data = &ring->data[i];
		if (__predict_false(data->m == NULL)) {
			/*
			 * There is no frame; skip this entry.
			 *
			 * NB: it is "ok" to have both
			 * 'tx done' + 'compressed BA' replies for frame
			 * with STATE_SCD_QUERY status.
			 */
			DPRINTF(sc, IWN_DEBUG_AMPDU,
			    "%s: ring %d: no entry %d\n", __func__, qid, i);
			continue;
		}

		tx_ok++;
		iwn_agg_tx_complete(sc, ring, tid, i, 1);
	}

	ring->queued -= tx_ok;
	iwn_check_tx_ring(sc, qid);

	DPRINTF(sc, IWN_DEBUG_TRACE | IWN_DEBUG_AMPDU,
	    "->%s: end; %d ok\n",__func__, tx_ok);
}

/*
 * Process a CALIBRATION_RESULT notification sent by the initialization
 * firmware on response to a CMD_CALIB_CONFIG command (5000 only).
 */
static void
iwn5000_rx_calib_results(struct iwn_softc *sc, struct iwn_rx_desc *desc)
{
	struct iwn_phy_calib *calib = (struct iwn_phy_calib *)(desc + 1);
	int len, idx = -1;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	/* Runtime firmware should not send such a notification. */
	if (sc->sc_flags & IWN_FLAG_CALIB_DONE){
		DPRINTF(sc, IWN_DEBUG_TRACE,
		    "->%s received after calib done\n", __func__);
		return;
	}
	len = (le32toh(desc->len) & 0x3fff) - 4;

	switch (calib->code) {
	case IWN5000_PHY_CALIB_DC:
		if (sc->base_params->calib_need & IWN_FLG_NEED_PHY_CALIB_DC)
			idx = 0;
		break;
	case IWN5000_PHY_CALIB_LO:
		if (sc->base_params->calib_need & IWN_FLG_NEED_PHY_CALIB_LO)
			idx = 1;
		break;
	case IWN5000_PHY_CALIB_TX_IQ:
		if (sc->base_params->calib_need & IWN_FLG_NEED_PHY_CALIB_TX_IQ)
			idx = 2;
		break;
	case IWN5000_PHY_CALIB_TX_IQ_PERIODIC:
		if (sc->base_params->calib_need & IWN_FLG_NEED_PHY_CALIB_TX_IQ_PERIODIC)
			idx = 3;
		break;
	case IWN5000_PHY_CALIB_BASE_BAND:
		if (sc->base_params->calib_need & IWN_FLG_NEED_PHY_CALIB_BASE_BAND)
			idx = 4;
		break;
	}
	if (idx == -1)	/* Ignore other results. */
		return;

	/* Save calibration result. */
	if (sc->calibcmd[idx].buf != NULL)
		free(sc->calibcmd[idx].buf, M_DEVBUF);
	sc->calibcmd[idx].buf = malloc(len, M_DEVBUF, M_NOWAIT);
	if (sc->calibcmd[idx].buf == NULL) {
		DPRINTF(sc, IWN_DEBUG_CALIBRATE,
		    "not enough memory for calibration result %d\n",
		    calib->code);
		return;
	}
	DPRINTF(sc, IWN_DEBUG_CALIBRATE,
	    "saving calibration result idx=%d, code=%d len=%d\n", idx, calib->code, len);
	sc->calibcmd[idx].len = len;
	memcpy(sc->calibcmd[idx].buf, calib, len);
}

static void
iwn_stats_update(struct iwn_softc *sc, struct iwn_calib_state *calib,
    struct iwn_stats *stats, int len)
{
	struct iwn_stats_bt *stats_bt;
	struct iwn_stats *lstats;

	/*
	 * First - check whether the length is the bluetooth or normal.
	 *
	 * If it's normal - just copy it and bump out.
	 * Otherwise we have to convert things.
	 */

	if (len == sizeof(struct iwn_stats) + 4) {
		memcpy(&sc->last_stat, stats, sizeof(struct iwn_stats));
		sc->last_stat_valid = 1;
		return;
	}

	/*
	 * If it's not the bluetooth size - log, then just copy.
	 */
	if (len != sizeof(struct iwn_stats_bt) + 4) {
		DPRINTF(sc, IWN_DEBUG_STATS,
		    "%s: size of rx statistics (%d) not an expected size!\n",
		    __func__,
		    len);
		memcpy(&sc->last_stat, stats, sizeof(struct iwn_stats));
		sc->last_stat_valid = 1;
		return;
	}

	/*
	 * Ok. Time to copy.
	 */
	stats_bt = (struct iwn_stats_bt *) stats;
	lstats = &sc->last_stat;

	/* flags */
	lstats->flags = stats_bt->flags;
	/* rx_bt */
	memcpy(&lstats->rx.ofdm, &stats_bt->rx_bt.ofdm,
	    sizeof(struct iwn_rx_phy_stats));
	memcpy(&lstats->rx.cck, &stats_bt->rx_bt.cck,
	    sizeof(struct iwn_rx_phy_stats));
	memcpy(&lstats->rx.general, &stats_bt->rx_bt.general_bt.common,
	    sizeof(struct iwn_rx_general_stats));
	memcpy(&lstats->rx.ht, &stats_bt->rx_bt.ht,
	    sizeof(struct iwn_rx_ht_phy_stats));
	/* tx */
	memcpy(&lstats->tx, &stats_bt->tx,
	    sizeof(struct iwn_tx_stats));
	/* general */
	memcpy(&lstats->general, &stats_bt->general,
	    sizeof(struct iwn_general_stats));

	/* XXX TODO: Squirrel away the extra bluetooth stats somewhere */
	sc->last_stat_valid = 1;
}

/*
 * Process an RX_STATISTICS or BEACON_STATISTICS firmware notification.
 * The latter is sent by the firmware after each received beacon.
 */
static void
iwn_rx_statistics(struct iwn_softc *sc, struct iwn_rx_desc *desc)
{
	struct iwn_ops *ops = &sc->ops;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	struct iwn_calib_state *calib = &sc->calib;
	struct iwn_stats *stats = (struct iwn_stats *)(desc + 1);
	struct iwn_stats *lstats;
	int temp;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	/* Ignore statistics received during a scan. */
	if (vap->iv_state != IEEE80211_S_RUN ||
	    (ic->ic_flags & IEEE80211_F_SCAN)){
		DPRINTF(sc, IWN_DEBUG_TRACE, "->%s received during calib\n",
	    __func__);
		return;
	}

	DPRINTF(sc, IWN_DEBUG_CALIBRATE | IWN_DEBUG_STATS,
	    "%s: received statistics, cmd %d, len %d\n",
	    __func__, desc->type, le16toh(desc->len));
	sc->calib_cnt = 0;	/* Reset TX power calibration timeout. */

	/*
	 * Collect/track general statistics for reporting.
	 *
	 * This takes care of ensuring that the bluetooth sized message
	 * will be correctly converted to the legacy sized message.
	 */
	iwn_stats_update(sc, calib, stats, le16toh(desc->len));

	/*
	 * And now, let's take a reference of it to use!
	 */
	lstats = &sc->last_stat;

	/* Test if temperature has changed. */
	if (lstats->general.temp != sc->rawtemp) {
		/* Convert "raw" temperature to degC. */
		sc->rawtemp = stats->general.temp;
		temp = ops->get_temperature(sc);
		DPRINTF(sc, IWN_DEBUG_CALIBRATE, "%s: temperature %d\n",
		    __func__, temp);

		/* Update TX power if need be (4965AGN only). */
		if (sc->hw_type == IWN_HW_REV_TYPE_4965)
			iwn4965_power_calibration(sc, temp);
	}

	if (desc->type != IWN_BEACON_STATISTICS)
		return;	/* Reply to a statistics request. */

	sc->noise = iwn_get_noise(&lstats->rx.general);
	DPRINTF(sc, IWN_DEBUG_CALIBRATE, "%s: noise %d\n", __func__, sc->noise);

	/* Test that RSSI and noise are present in stats report. */
	if (le32toh(lstats->rx.general.flags) != 1) {
		DPRINTF(sc, IWN_DEBUG_ANY, "%s\n",
		    "received statistics without RSSI");
		return;
	}

	if (calib->state == IWN_CALIB_STATE_ASSOC)
		iwn_collect_noise(sc, &lstats->rx.general);
	else if (calib->state == IWN_CALIB_STATE_RUN) {
		iwn_tune_sensitivity(sc, &lstats->rx);
		/*
		 * XXX TODO: Only run the RX recovery if we're associated!
		 */
		iwn_check_rx_recovery(sc, lstats);
		iwn_save_stats_counters(sc, lstats);
	}

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);
}

/*
 * Save the relevant statistic counters for the next calibration
 * pass.
 */
static void
iwn_save_stats_counters(struct iwn_softc *sc, const struct iwn_stats *rs)
{
	struct iwn_calib_state *calib = &sc->calib;

	/* Save counters values for next call. */
	calib->bad_plcp_cck = le32toh(rs->rx.cck.bad_plcp);
	calib->fa_cck = le32toh(rs->rx.cck.fa);
	calib->bad_plcp_ht = le32toh(rs->rx.ht.bad_plcp);
	calib->bad_plcp_ofdm = le32toh(rs->rx.ofdm.bad_plcp);
	calib->fa_ofdm = le32toh(rs->rx.ofdm.fa);

	/* Last time we received these tick values */
	sc->last_calib_ticks = ticks;
}

/*
 * Process a TX_DONE firmware notification.  Unfortunately, the 4965AGN
 * and 5000 adapters have different incompatible TX status formats.
 */
static void
iwn4965_tx_done(struct iwn_softc *sc, struct iwn_rx_desc *desc,
    struct iwn_rx_data *data)
{
	struct iwn4965_tx_stat *stat = (struct iwn4965_tx_stat *)(desc + 1);
	int qid = desc->qid & IWN_RX_DESC_QID_MSK;

	DPRINTF(sc, IWN_DEBUG_XMIT, "%s: "
	    "qid %d idx %d RTS retries %d ACK retries %d nkill %d rate %x duration %d status %x\n",
	    __func__, desc->qid, desc->idx,
	    stat->rtsfailcnt,
	    stat->ackfailcnt,
	    stat->btkillcnt,
	    stat->rate, le16toh(stat->duration),
	    le32toh(stat->status));

	if (qid >= sc->firstaggqueue && stat->nframes != 1) {
		iwn_ampdu_tx_done(sc, qid, stat->nframes, stat->rtsfailcnt,
		    &stat->status);
	} else {
		iwn_tx_done(sc, desc, stat->rtsfailcnt, stat->ackfailcnt,
		    le32toh(stat->status) & 0xff);
	}
}

static void
iwn5000_tx_done(struct iwn_softc *sc, struct iwn_rx_desc *desc,
    struct iwn_rx_data *data)
{
	struct iwn5000_tx_stat *stat = (struct iwn5000_tx_stat *)(desc + 1);
	int qid = desc->qid & IWN_RX_DESC_QID_MSK;

	DPRINTF(sc, IWN_DEBUG_XMIT, "%s: "
	    "qid %d idx %d RTS retries %d ACK retries %d nkill %d rate %x duration %d status %x\n",
	    __func__, desc->qid, desc->idx,
	    stat->rtsfailcnt,
	    stat->ackfailcnt,
	    stat->btkillcnt,
	    stat->rate, le16toh(stat->duration),
	    le32toh(stat->status));

#ifdef notyet
	/* Reset TX scheduler slot. */
	iwn5000_reset_sched(sc, qid, desc->idx);
#endif

	if (qid >= sc->firstaggqueue && stat->nframes != 1) {
		iwn_ampdu_tx_done(sc, qid, stat->nframes, stat->rtsfailcnt,
		    &stat->status);
	} else {
		iwn_tx_done(sc, desc, stat->rtsfailcnt, stat->ackfailcnt,
		    le16toh(stat->status) & 0xff);
	}
}

static void
iwn_adj_ampdu_ptr(struct iwn_softc *sc, struct iwn_tx_ring *ring)
{
	int i;

	for (i = ring->read; i != ring->cur; i = (i + 1) % IWN_TX_RING_COUNT) {
		struct iwn_tx_data *data = &ring->data[i];

		if (data->m != NULL)
			break;

		data->remapped = 0;
	}

	ring->read = i;
}

/*
 * Adapter-independent backend for TX_DONE firmware notifications.
 */
static void
iwn_tx_done(struct iwn_softc *sc, struct iwn_rx_desc *desc, int rtsfailcnt,
    int ackfailcnt, uint8_t status)
{
	struct ieee80211_ratectl_tx_status *txs = &sc->sc_txs;
	struct iwn_tx_ring *ring = &sc->txq[desc->qid & IWN_RX_DESC_QID_MSK];
	struct iwn_tx_data *data = &ring->data[desc->idx];
	struct mbuf *m;
	struct ieee80211_node *ni;

	if (__predict_false(data->m == NULL &&
	    ring->qid >= sc->firstaggqueue)) {
		/*
		 * There is no frame; skip this entry.
		 */
		DPRINTF(sc, IWN_DEBUG_AMPDU, "%s: ring %d: no entry %d\n",
		    __func__, ring->qid, desc->idx);
		return;
	}

	KASSERT(data->ni != NULL, ("no node"));
	KASSERT(data->m != NULL, ("no mbuf"));

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	/* Unmap and free mbuf. */
	bus_dmamap_sync(ring->data_dmat, data->map, BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(ring->data_dmat, data->map);
	m = data->m, data->m = NULL;
	ni = data->ni, data->ni = NULL;

	data->long_retries = 0;

	if (ring->qid >= sc->firstaggqueue)
		iwn_adj_ampdu_ptr(sc, ring);

	/*
	 * XXX f/w may hang (device timeout) when desc->idx - ring->read == 64
	 * (aggregation queues only).
	 */

	ring->queued--;
	iwn_check_tx_ring(sc, ring->qid);

	/*
	 * Update rate control statistics for the node.
	 */
	txs->flags = IEEE80211_RATECTL_STATUS_SHORT_RETRY |
		     IEEE80211_RATECTL_STATUS_LONG_RETRY;
	txs->short_retries = rtsfailcnt;
	txs->long_retries = ackfailcnt;
	if (!(status & IWN_TX_FAIL))
		txs->status = IEEE80211_RATECTL_TX_SUCCESS;
	else {
		switch (status) {
		case IWN_TX_FAIL_SHORT_LIMIT:
			txs->status = IEEE80211_RATECTL_TX_FAIL_SHORT;
			break;
		case IWN_TX_FAIL_LONG_LIMIT:
			txs->status = IEEE80211_RATECTL_TX_FAIL_LONG;
			break;
		case IWN_TX_STATUS_FAIL_LIFE_EXPIRE:
			txs->status = IEEE80211_RATECTL_TX_FAIL_EXPIRED;
			break;
		default:
			txs->status = IEEE80211_RATECTL_TX_FAIL_UNSPECIFIED;
			break;
		}
	}
	ieee80211_ratectl_tx_complete(ni, txs);

	/*
	 * Channels marked for "radar" require traffic to be received
	 * to unlock before we can transmit.  Until traffic is seen
	 * any attempt to transmit is returned immediately with status
	 * set to IWN_TX_FAIL_TX_LOCKED.  Unfortunately this can easily
	 * happen on first authenticate after scanning.  To workaround
	 * this we ignore a failure of this sort in AUTH state so the
	 * 802.11 layer will fall back to using a timeout to wait for
	 * the AUTH reply.  This allows the firmware time to see
	 * traffic so a subsequent retry of AUTH succeeds.  It's
	 * unclear why the firmware does not maintain state for
	 * channels recently visited as this would allow immediate
	 * use of the channel after a scan (where we see traffic).
	 */
	if (status == IWN_TX_FAIL_TX_LOCKED &&
	    ni->ni_vap->iv_state == IEEE80211_S_AUTH)
		ieee80211_tx_complete(ni, m, 0);
	else
		ieee80211_tx_complete(ni, m,
		    (status & IWN_TX_FAIL) != 0);

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);
}

/*
 * Process a "command done" firmware notification.  This is where we wakeup
 * processes waiting for a synchronous command completion.
 */
static void
iwn_cmd_done(struct iwn_softc *sc, struct iwn_rx_desc *desc)
{
	struct iwn_tx_ring *ring;
	struct iwn_tx_data *data;
	int cmd_queue_num;

	if (sc->sc_flags & IWN_FLAG_PAN_SUPPORT)
		cmd_queue_num = IWN_PAN_CMD_QUEUE;
	else
		cmd_queue_num = IWN_CMD_QUEUE_NUM;

	if ((desc->qid & IWN_RX_DESC_QID_MSK) != cmd_queue_num)
		return;	/* Not a command ack. */

	ring = &sc->txq[cmd_queue_num];
	data = &ring->data[desc->idx];

	/* If the command was mapped in an mbuf, free it. */
	if (data->m != NULL) {
		bus_dmamap_sync(ring->data_dmat, data->map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(ring->data_dmat, data->map);
		m_freem(data->m);
		data->m = NULL;
	}
	wakeup(&ring->desc[desc->idx]);
}

static int
iwn_ampdu_check_bitmap(uint64_t bitmap, int start, int idx)
{
	int bit, shift;

	bit = idx - start;
	shift = 0;
	if (bit >= 64) {
		shift = 0x100 - bit;
		bit = 0;
	} else if (bit <= -64)
		bit = 0x100 + bit;
	else if (bit < 0) {
		shift = -bit;
		bit = 0;
	}

	if (bit - shift >= 64)
		return (0);

	return ((bitmap & (1ULL << (bit - shift))) != 0);
}

/*
 * Firmware bug workaround: in case if 'retries' counter
 * overflows 'seqno' field will be incremented:
 *    status|sequence|status|sequence|status|sequence
 *     0000    0A48    0001    0A49    0000    0A6A
 *     1000    0A48    1000    0A49    1000    0A6A
 *     2000    0A48    2000    0A49    2000    0A6A
 * ...
 *     E000    0A48    E000    0A49    E000    0A6A
 *     F000    0A48    F000    0A49    F000    0A6A
 *     0000    0A49    0000    0A49    0000    0A6B
 *     1000    0A49    1000    0A49    1000    0A6B
 * ...
 *     D000    0A49    D000    0A49    D000    0A6B
 *     E000    0A49    E001    0A49    E000    0A6B
 *     F000    0A49    F001    0A49    F000    0A6B
 *     0000    0A4A    0000    0A4B    0000    0A6A
 *     1000    0A4A    1000    0A4B    1000    0A6A
 * ...
 *
 * Odd 'seqno' numbers are incremened by 2 every 2 overflows.
 * For even 'seqno' % 4 != 0 overflow is cyclic (0 -> +1 -> 0).
 * Not checked with nretries >= 64.
 *
 */
static int
iwn_ampdu_index_check(struct iwn_softc *sc, struct iwn_tx_ring *ring,
    uint64_t bitmap, int start, int idx)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwn_tx_data *data;
	int diff, min_retries, max_retries, new_idx, loop_end;

	new_idx = idx - IWN_LONG_RETRY_LIMIT_LOG;
	if (new_idx < 0)
		new_idx += IWN_TX_RING_COUNT;

	/*
	 * Corner case: check if retry count is not too big;
	 * reset device otherwise.
	 */
	if (!iwn_ampdu_check_bitmap(bitmap, start, new_idx)) {
		data = &ring->data[new_idx];
		if (data->long_retries > IWN_LONG_RETRY_LIMIT) {
			device_printf(sc->sc_dev,
			    "%s: retry count (%d) for idx %d/%d overflow, "
			    "resetting...\n", __func__, data->long_retries,
			    ring->qid, new_idx);
			ieee80211_restart_all(ic);
			return (-1);
		}
	}

	/* Correct index if needed. */
	loop_end = idx;
	do {
		data = &ring->data[new_idx];
		diff = idx - new_idx;
		if (diff < 0)
			diff += IWN_TX_RING_COUNT;

		min_retries = IWN_LONG_RETRY_FW_OVERFLOW * diff;
		if ((new_idx % 2) == 0)
			max_retries = IWN_LONG_RETRY_FW_OVERFLOW * (diff + 1);
		else
			max_retries = IWN_LONG_RETRY_FW_OVERFLOW * (diff + 2);

		if (!iwn_ampdu_check_bitmap(bitmap, start, new_idx) &&
		    ((data->long_retries >= min_retries &&
		      data->long_retries < max_retries) ||
		     (diff == 1 &&
		      (new_idx & 0x03) == 0x02 &&
		      data->long_retries >= IWN_LONG_RETRY_FW_OVERFLOW))) {
			DPRINTF(sc, IWN_DEBUG_AMPDU,
			    "%s: correcting index %d -> %d in queue %d"
			    " (retries %d)\n", __func__, idx, new_idx,
			    ring->qid, data->long_retries);
			return (new_idx);
		}

		new_idx = (new_idx + 1) % IWN_TX_RING_COUNT;
	} while (new_idx != loop_end);

	return (idx);
}

static void
iwn_ampdu_tx_done(struct iwn_softc *sc, int qid, int nframes, int rtsfailcnt,
    void *stat)
{
	struct iwn_tx_ring *ring = &sc->txq[qid];
	struct ieee80211_tx_ampdu *tap = sc->qid2tap[qid];
	struct iwn_node *wn = (void *)tap->txa_ni;
	struct iwn_tx_data *data;
	uint64_t bitmap = 0;
	uint16_t *aggstatus = stat;
	uint8_t tid = tap->txa_tid;
	int bit, i, idx, shift, start, tx_err;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	start = le16toh(*(aggstatus + nframes * 2)) & 0xff;

	for (i = 0; i < nframes; i++) {
		uint16_t status = le16toh(aggstatus[i * 2]);

		if (status & IWN_AGG_TX_STATE_IGNORE_MASK)
			continue;

		idx = le16toh(aggstatus[i * 2 + 1]) & 0xff;
		data = &ring->data[idx];
		if (data->remapped) {
			idx = iwn_ampdu_index_check(sc, ring, bitmap, start, idx);
			if (idx == -1) {
				/* skip error (device will be restarted anyway). */
				continue;
			}

			/* Index may have changed. */
			data = &ring->data[idx];
		}

		/*
		 * XXX Sometimes (rarely) some frames are excluded from events.
		 * XXX Due to that long_retries counter may be wrong.
		 */
		data->long_retries &= ~0x0f;
		data->long_retries += IWN_AGG_TX_TRY_COUNT(status) + 1;

		if (data->long_retries >= IWN_LONG_RETRY_FW_OVERFLOW) {
			int diff, wrong_idx;

			diff = data->long_retries / IWN_LONG_RETRY_FW_OVERFLOW;
			wrong_idx = (idx + diff) % IWN_TX_RING_COUNT;

			/*
			 * Mark the entry so the above code will check it
			 * next time.
			 */
			ring->data[wrong_idx].remapped = 1;
		}

		if (status & IWN_AGG_TX_STATE_UNDERRUN_MSK) {
			/*
			 * NB: count retries but postpone - it was not
			 * transmitted.
			 */
			continue;
		}

		bit = idx - start;
		shift = 0;
		if (bit >= 64) {
			shift = 0x100 - bit;
			bit = 0;
		} else if (bit <= -64)
			bit = 0x100 + bit;
		else if (bit < 0) {
			shift = -bit;
			bit = 0;
		}
		bitmap = bitmap << shift;
		bitmap |= 1ULL << bit;
	}
	wn->agg[tid].startidx = start;
	wn->agg[tid].bitmap = bitmap;
	wn->agg[tid].short_retries = rtsfailcnt;

	DPRINTF(sc, IWN_DEBUG_AMPDU, "%s: nframes %d start %d bitmap %016jX\n",
	    __func__, nframes, start, (uintmax_t)bitmap);

	i = ring->read;

	for (tx_err = 0;
	     i != wn->agg[tid].startidx;
	     i = (i + 1) % IWN_TX_RING_COUNT) {
		data = &ring->data[i];
		data->remapped = 0;
		if (data->m == NULL)
			continue;

		tx_err++;
		iwn_agg_tx_complete(sc, ring, tid, i, 0);
	}

	ring->read = wn->agg[tid].startidx;
	ring->queued -= tx_err;

	iwn_check_tx_ring(sc, qid);

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);
}

/*
 * Process an INT_FH_RX or INT_SW_RX interrupt.
 */
static void
iwn_notif_intr(struct iwn_softc *sc)
{
	struct iwn_ops *ops = &sc->ops;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	uint16_t hw;
	int is_stopped;

	bus_dmamap_sync(sc->rxq.stat_dma.tag, sc->rxq.stat_dma.map,
	    BUS_DMASYNC_POSTREAD);

	hw = le16toh(sc->rxq.stat->closed_count) & 0xfff;
	while (sc->rxq.cur != hw) {
		struct iwn_rx_data *data = &sc->rxq.data[sc->rxq.cur];
		struct iwn_rx_desc *desc;

		bus_dmamap_sync(sc->rxq.data_dmat, data->map,
		    BUS_DMASYNC_POSTREAD);
		desc = mtod(data->m, struct iwn_rx_desc *);

		DPRINTF(sc, IWN_DEBUG_RECV,
		    "%s: cur=%d; qid %x idx %d flags %x type %d(%s) len %d\n",
		    __func__, sc->rxq.cur, desc->qid & IWN_RX_DESC_QID_MSK,
		    desc->idx, desc->flags, desc->type,
		    iwn_intr_str(desc->type), le16toh(desc->len));

		if (!(desc->qid & IWN_UNSOLICITED_RX_NOTIF))	/* Reply to a command. */
			iwn_cmd_done(sc, desc);

		switch (desc->type) {
		case IWN_RX_PHY:
			iwn_rx_phy(sc, desc);
			break;

		case IWN_RX_DONE:		/* 4965AGN only. */
		case IWN_MPDU_RX_DONE:
			/* An 802.11 frame has been received. */
			iwn_rx_done(sc, desc, data);

			is_stopped = (sc->sc_flags & IWN_FLAG_RUNNING) == 0;
			if (__predict_false(is_stopped))
				return;

			break;

		case IWN_RX_COMPRESSED_BA:
			/* A Compressed BlockAck has been received. */
			iwn_rx_compressed_ba(sc, desc);
			break;

		case IWN_TX_DONE:
			/* An 802.11 frame has been transmitted. */
			ops->tx_done(sc, desc, data);
			break;

		case IWN_RX_STATISTICS:
		case IWN_BEACON_STATISTICS:
			iwn_rx_statistics(sc, desc);
			break;

		case IWN_BEACON_MISSED:
		{
			struct iwn_beacon_missed *miss =
			    (struct iwn_beacon_missed *)(desc + 1);
			int misses;

			misses = le32toh(miss->consecutive);

			DPRINTF(sc, IWN_DEBUG_STATE,
			    "%s: beacons missed %d/%d\n", __func__,
			    misses, le32toh(miss->total));
			/*
			 * If more than 5 consecutive beacons are missed,
			 * reinitialize the sensitivity state machine.
			 */
			if (vap->iv_state == IEEE80211_S_RUN &&
			    (ic->ic_flags & IEEE80211_F_SCAN) == 0) {
				if (misses > 5)
					(void)iwn_init_sensitivity(sc);
				if (misses >= vap->iv_bmissthreshold) {
					IWN_UNLOCK(sc);
					ieee80211_beacon_miss(ic);
					IWN_LOCK(sc);

					is_stopped = (sc->sc_flags &
					    IWN_FLAG_RUNNING) == 0;
					if (__predict_false(is_stopped))
						return;
				}
			}
			break;
		}
		case IWN_UC_READY:
		{
			struct iwn_ucode_info *uc =
			    (struct iwn_ucode_info *)(desc + 1);

			/* The microcontroller is ready. */
			DPRINTF(sc, IWN_DEBUG_RESET,
			    "microcode alive notification version=%d.%d "
			    "subtype=%x alive=%x\n", uc->major, uc->minor,
			    uc->subtype, le32toh(uc->valid));

			if (le32toh(uc->valid) != 1) {
				device_printf(sc->sc_dev,
				    "microcontroller initialization failed");
				break;
			}
			if (uc->subtype == IWN_UCODE_INIT) {
				/* Save microcontroller report. */
				memcpy(&sc->ucode_info, uc, sizeof (*uc));
			}
			/* Save the address of the error log in SRAM. */
			sc->errptr = le32toh(uc->errptr);
			break;
		}
#ifdef IWN_DEBUG
		case IWN_STATE_CHANGED:
		{
			/*
			 * State change allows hardware switch change to be
			 * noted. However, we handle this in iwn_intr as we
			 * get both the enable/disble intr.
			 */
			uint32_t *status = (uint32_t *)(desc + 1);
			DPRINTF(sc, IWN_DEBUG_INTR | IWN_DEBUG_STATE,
			    "state changed to %x\n",
			    le32toh(*status));
			break;
		}
		case IWN_START_SCAN:
		{
			struct iwn_start_scan *scan =
			    (struct iwn_start_scan *)(desc + 1);
			DPRINTF(sc, IWN_DEBUG_ANY,
			    "%s: scanning channel %d status %x\n",
			    __func__, scan->chan, le32toh(scan->status));
			break;
		}
#endif
		case IWN_STOP_SCAN:
		{
#ifdef	IWN_DEBUG
			struct iwn_stop_scan *scan =
			    (struct iwn_stop_scan *)(desc + 1);
			DPRINTF(sc, IWN_DEBUG_STATE | IWN_DEBUG_SCAN,
			    "scan finished nchan=%d status=%d chan=%d\n",
			    scan->nchan, scan->status, scan->chan);
#endif
			sc->sc_is_scanning = 0;
			callout_stop(&sc->scan_timeout);
			IWN_UNLOCK(sc);
			ieee80211_scan_next(vap);
			IWN_LOCK(sc);

			is_stopped = (sc->sc_flags & IWN_FLAG_RUNNING) == 0;
			if (__predict_false(is_stopped))  
				return;

			break;
		}
		case IWN5000_CALIBRATION_RESULT:
			iwn5000_rx_calib_results(sc, desc);
			break;

		case IWN5000_CALIBRATION_DONE:
			sc->sc_flags |= IWN_FLAG_CALIB_DONE;
			wakeup(sc);
			break;
		}

		sc->rxq.cur = (sc->rxq.cur + 1) % IWN_RX_RING_COUNT;
	}

	/* Tell the firmware what we have processed. */
	hw = (hw == 0) ? IWN_RX_RING_COUNT - 1 : hw - 1;
	IWN_WRITE(sc, IWN_FH_RX_WPTR, hw & ~7);
}

/*
 * Process an INT_WAKEUP interrupt raised when the microcontroller wakes up
 * from power-down sleep mode.
 */
static void
iwn_wakeup_intr(struct iwn_softc *sc)
{
	int qid;

	DPRINTF(sc, IWN_DEBUG_RESET, "%s: ucode wakeup from power-down sleep\n",
	    __func__);

	/* Wakeup RX and TX rings. */
	IWN_WRITE(sc, IWN_FH_RX_WPTR, sc->rxq.cur & ~7);
	for (qid = 0; qid < sc->ntxqs; qid++) {
		struct iwn_tx_ring *ring = &sc->txq[qid];
		IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | ring->cur);
	}
}

static void
iwn_rftoggle_task(void *arg, int npending)
{
	struct iwn_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t tmp;

	IWN_LOCK(sc);
	tmp = IWN_READ(sc, IWN_GP_CNTRL);
	IWN_UNLOCK(sc);

	device_printf(sc->sc_dev, "RF switch: radio %s\n",
	    (tmp & IWN_GP_CNTRL_RFKILL) ? "enabled" : "disabled");
	if (!(tmp & IWN_GP_CNTRL_RFKILL)) {
		ieee80211_suspend_all(ic);

		/* Enable interrupts to get RF toggle notification. */
		IWN_LOCK(sc);
		IWN_WRITE(sc, IWN_INT, 0xffffffff);
		IWN_WRITE(sc, IWN_INT_MASK, sc->int_mask);
		IWN_UNLOCK(sc);
	} else
		ieee80211_resume_all(ic);
}

/*
 * Dump the error log of the firmware when a firmware panic occurs.  Although
 * we can't debug the firmware because it is neither open source nor free, it
 * can help us to identify certain classes of problems.
 */
static void
iwn_fatal_intr(struct iwn_softc *sc)
{
	struct iwn_fw_dump dump;
	int i;

	IWN_LOCK_ASSERT(sc);

	/* Force a complete recalibration on next init. */
	sc->sc_flags &= ~IWN_FLAG_CALIB_DONE;

	/* Check that the error log address is valid. */
	if (sc->errptr < IWN_FW_DATA_BASE ||
	    sc->errptr + sizeof (dump) >
	    IWN_FW_DATA_BASE + sc->fw_data_maxsz) {
		printf("%s: bad firmware error log address 0x%08x\n", __func__,
		    sc->errptr);
		return;
	}
	if (iwn_nic_lock(sc) != 0) {
		printf("%s: could not read firmware error log\n", __func__);
		return;
	}
	/* Read firmware error log from SRAM. */
	iwn_mem_read_region_4(sc, sc->errptr, (uint32_t *)&dump,
	    sizeof (dump) / sizeof (uint32_t));
	iwn_nic_unlock(sc);

	if (dump.valid == 0) {
		printf("%s: firmware error log is empty\n", __func__);
		return;
	}
	printf("firmware error log:\n");
	printf("  error type      = \"%s\" (0x%08X)\n",
	    (dump.id < nitems(iwn_fw_errmsg)) ?
		iwn_fw_errmsg[dump.id] : "UNKNOWN",
	    dump.id);
	printf("  program counter = 0x%08X\n", dump.pc);
	printf("  source line     = 0x%08X\n", dump.src_line);
	printf("  error data      = 0x%08X%08X\n",
	    dump.error_data[0], dump.error_data[1]);
	printf("  branch link     = 0x%08X%08X\n",
	    dump.branch_link[0], dump.branch_link[1]);
	printf("  interrupt link  = 0x%08X%08X\n",
	    dump.interrupt_link[0], dump.interrupt_link[1]);
	printf("  time            = %u\n", dump.time[0]);

	/* Dump driver status (TX and RX rings) while we're here. */
	printf("driver status:\n");
	for (i = 0; i < sc->ntxqs; i++) {
		struct iwn_tx_ring *ring = &sc->txq[i];
		printf("  tx ring %2d: qid=%-2d cur=%-3d queued=%-3d\n",
		    i, ring->qid, ring->cur, ring->queued);
	}
	printf("  rx ring: cur=%d\n", sc->rxq.cur);
}

static void
iwn_intr(void *arg)
{
	struct iwn_softc *sc = arg;
	uint32_t r1, r2, tmp;

	IWN_LOCK(sc);

	/* Disable interrupts. */
	IWN_WRITE(sc, IWN_INT_MASK, 0);

	/* Read interrupts from ICT (fast) or from registers (slow). */
	if (sc->sc_flags & IWN_FLAG_USE_ICT) {
		bus_dmamap_sync(sc->ict_dma.tag, sc->ict_dma.map,
		    BUS_DMASYNC_POSTREAD);
		tmp = 0;
		while (sc->ict[sc->ict_cur] != 0) {
			tmp |= sc->ict[sc->ict_cur];
			sc->ict[sc->ict_cur] = 0;	/* Acknowledge. */
			sc->ict_cur = (sc->ict_cur + 1) % IWN_ICT_COUNT;
		}
		tmp = le32toh(tmp);
		if (tmp == 0xffffffff)	/* Shouldn't happen. */
			tmp = 0;
		else if (tmp & 0xc0000)	/* Workaround a HW bug. */
			tmp |= 0x8000;
		r1 = (tmp & 0xff00) << 16 | (tmp & 0xff);
		r2 = 0;	/* Unused. */
	} else {
		r1 = IWN_READ(sc, IWN_INT);
		if (r1 == 0xffffffff || (r1 & 0xfffffff0) == 0xa5a5a5a0) {
			IWN_UNLOCK(sc);
			return;	/* Hardware gone! */
		}
		r2 = IWN_READ(sc, IWN_FH_INT);
	}

	DPRINTF(sc, IWN_DEBUG_INTR, "interrupt reg1=0x%08x reg2=0x%08x\n"
    , r1, r2);

	if (r1 == 0 && r2 == 0)
		goto done;	/* Interrupt not for us. */

	/* Acknowledge interrupts. */
	IWN_WRITE(sc, IWN_INT, r1);
	if (!(sc->sc_flags & IWN_FLAG_USE_ICT))
		IWN_WRITE(sc, IWN_FH_INT, r2);

	if (r1 & IWN_INT_RF_TOGGLED) {
		taskqueue_enqueue(sc->sc_tq, &sc->sc_rftoggle_task);
		goto done;
	}
	if (r1 & IWN_INT_CT_REACHED) {
		device_printf(sc->sc_dev, "%s: critical temperature reached!\n",
		    __func__);
	}
	if (r1 & (IWN_INT_SW_ERR | IWN_INT_HW_ERR)) {
		device_printf(sc->sc_dev, "%s: fatal firmware error\n",
		    __func__);
#ifdef	IWN_DEBUG
		iwn_debug_register(sc);
#endif
		/* Dump firmware error log and stop. */
		iwn_fatal_intr(sc);

		taskqueue_enqueue(sc->sc_tq, &sc->sc_panic_task);
		goto done;
	}
	if ((r1 & (IWN_INT_FH_RX | IWN_INT_SW_RX | IWN_INT_RX_PERIODIC)) ||
	    (r2 & IWN_FH_INT_RX)) {
		if (sc->sc_flags & IWN_FLAG_USE_ICT) {
			if (r1 & (IWN_INT_FH_RX | IWN_INT_SW_RX))
				IWN_WRITE(sc, IWN_FH_INT, IWN_FH_INT_RX);
			IWN_WRITE_1(sc, IWN_INT_PERIODIC,
			    IWN_INT_PERIODIC_DIS);
			iwn_notif_intr(sc);
			if (r1 & (IWN_INT_FH_RX | IWN_INT_SW_RX)) {
				IWN_WRITE_1(sc, IWN_INT_PERIODIC,
				    IWN_INT_PERIODIC_ENA);
			}
		} else
			iwn_notif_intr(sc);
	}

	if ((r1 & IWN_INT_FH_TX) || (r2 & IWN_FH_INT_TX)) {
		if (sc->sc_flags & IWN_FLAG_USE_ICT)
			IWN_WRITE(sc, IWN_FH_INT, IWN_FH_INT_TX);
		wakeup(sc);	/* FH DMA transfer completed. */
	}

	if (r1 & IWN_INT_ALIVE)
		wakeup(sc);	/* Firmware is alive. */

	if (r1 & IWN_INT_WAKEUP)
		iwn_wakeup_intr(sc);

done:
	/* Re-enable interrupts. */
	if (sc->sc_flags & IWN_FLAG_RUNNING)
		IWN_WRITE(sc, IWN_INT_MASK, sc->int_mask);

	IWN_UNLOCK(sc);
}

/*
 * Update TX scheduler ring when transmitting an 802.11 frame (4965AGN and
 * 5000 adapters use a slightly different format).
 */
static void
iwn4965_update_sched(struct iwn_softc *sc, int qid, int idx, uint8_t id,
    uint16_t len)
{
	uint16_t *w = &sc->sched[qid * IWN4965_SCHED_COUNT + idx];

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	*w = htole16(len + 8);
	bus_dmamap_sync(sc->sched_dma.tag, sc->sched_dma.map,
	    BUS_DMASYNC_PREWRITE);
	if (idx < IWN_SCHED_WINSZ) {
		*(w + IWN_TX_RING_COUNT) = *w;
		bus_dmamap_sync(sc->sched_dma.tag, sc->sched_dma.map,
		    BUS_DMASYNC_PREWRITE);
	}
}

static void
iwn5000_update_sched(struct iwn_softc *sc, int qid, int idx, uint8_t id,
    uint16_t len)
{
	uint16_t *w = &sc->sched[qid * IWN5000_SCHED_COUNT + idx];

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	*w = htole16(id << 12 | (len + 8));
	bus_dmamap_sync(sc->sched_dma.tag, sc->sched_dma.map,
	    BUS_DMASYNC_PREWRITE);
	if (idx < IWN_SCHED_WINSZ) {
		*(w + IWN_TX_RING_COUNT) = *w;
		bus_dmamap_sync(sc->sched_dma.tag, sc->sched_dma.map,
		    BUS_DMASYNC_PREWRITE);
	}
}

#ifdef notyet
static void
iwn5000_reset_sched(struct iwn_softc *sc, int qid, int idx)
{
	uint16_t *w = &sc->sched[qid * IWN5000_SCHED_COUNT + idx];

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	*w = (*w & htole16(0xf000)) | htole16(1);
	bus_dmamap_sync(sc->sched_dma.tag, sc->sched_dma.map,
	    BUS_DMASYNC_PREWRITE);
	if (idx < IWN_SCHED_WINSZ) {
		*(w + IWN_TX_RING_COUNT) = *w;
		bus_dmamap_sync(sc->sched_dma.tag, sc->sched_dma.map,
		    BUS_DMASYNC_PREWRITE);
	}
}
#endif

/*
 * Check whether OFDM 11g protection will be enabled for the given rate.
 *
 * The original driver code only enabled protection for OFDM rates.
 * It didn't check to see whether it was operating in 11a or 11bg mode.
 */
static int
iwn_check_rate_needs_protection(struct iwn_softc *sc,
    struct ieee80211vap *vap, uint8_t rate)
{
	struct ieee80211com *ic = vap->iv_ic;

	/*
	 * Not in 2GHz mode? Then there's no need to enable OFDM
	 * 11bg protection.
	 */
	if (! IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan)) {
		return (0);
	}

	/*
	 * 11bg protection not enabled? Then don't use it.
	 */
	if ((ic->ic_flags & IEEE80211_F_USEPROT) == 0)
		return (0);

	/*
	 * If it's an 11n rate - no protection.
	 * We'll do it via a specific 11n check.
	 */
	if (rate & IEEE80211_RATE_MCS) {
		return (0);
	}

	/*
	 * Do a rate table lookup.  If the PHY is CCK,
	 * don't do protection.
	 */
	if (ieee80211_rate2phytype(ic->ic_rt, rate) == IEEE80211_T_CCK)
		return (0);

	/*
	 * Yup, enable protection.
	 */
	return (1);
}

/*
 * return a value between 0 and IWN_MAX_TX_RETRIES-1 as an index into
 * the link quality table that reflects this particular entry.
 */
static int
iwn_tx_rate_to_linkq_offset(struct iwn_softc *sc, struct ieee80211_node *ni,
    uint8_t rate)
{
	struct ieee80211_rateset *rs;
	int is_11n;
	int nr;
	int i;
	uint8_t cmp_rate;

	/*
	 * Figure out if we're using 11n or not here.
	 */
	if (IEEE80211_IS_CHAN_HT(ni->ni_chan) && ni->ni_htrates.rs_nrates > 0)
		is_11n = 1;
	else
		is_11n = 0;

	/*
	 * Use the correct rate table.
	 */
	if (is_11n) {
		rs = (struct ieee80211_rateset *) &ni->ni_htrates;
		nr = ni->ni_htrates.rs_nrates;
	} else {
		rs = &ni->ni_rates;
		nr = rs->rs_nrates;
	}

	/*
	 * Find the relevant link quality entry in the table.
	 */
	for (i = 0; i < nr && i < IWN_MAX_TX_RETRIES - 1 ; i++) {
		/*
		 * The link quality table index starts at 0 == highest
		 * rate, so we walk the rate table backwards.
		 */
		cmp_rate = rs->rs_rates[(nr - 1) - i];
		if (rate & IEEE80211_RATE_MCS)
			cmp_rate |= IEEE80211_RATE_MCS;

#if 0
		DPRINTF(sc, IWN_DEBUG_XMIT, "%s: idx %d: nr=%d, rate=0x%02x, rateentry=0x%02x\n",
		    __func__,
		    i,
		    nr,
		    rate,
		    cmp_rate);
#endif

		if (cmp_rate == rate)
			return (i);
	}

	/* Failed? Start at the end */
	return (IWN_MAX_TX_RETRIES - 1);
}

static int
iwn_tx_data(struct iwn_softc *sc, struct mbuf *m, struct ieee80211_node *ni)
{
	const struct ieee80211_txparam *tp = ni->ni_txparms;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211com *ic = ni->ni_ic;
	struct iwn_node *wn = (void *)ni;
	struct iwn_tx_ring *ring;
	struct iwn_tx_cmd *cmd;
	struct iwn_cmd_data *tx;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k = NULL;
	uint32_t flags;
	uint16_t qos;
	uint8_t tid, type;
	int ac, totlen, rate;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	IWN_LOCK_ASSERT(sc);

	wh = mtod(m, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

	/* Select EDCA Access Category and TX ring for this frame. */
	if (IEEE80211_QOS_HAS_SEQ(wh)) {
		qos = ((const struct ieee80211_qosframe *)wh)->i_qos[0];
		tid = qos & IEEE80211_QOS_TID;
	} else {
		qos = 0;
		tid = 0;
	}

	/* Choose a TX rate index. */
	if (type == IEEE80211_FC0_TYPE_MGT ||
	    type == IEEE80211_FC0_TYPE_CTL ||
	    (m->m_flags & M_EAPOL) != 0)
		rate = tp->mgmtrate;
	else if (IEEE80211_IS_MULTICAST(wh->i_addr1))
		rate = tp->mcastrate;
	else if (tp->ucastrate != IEEE80211_FIXED_RATE_NONE)
		rate = tp->ucastrate;
	else {
		/* XXX pass pktlen */
		(void) ieee80211_ratectl_rate(ni, NULL, 0);
		rate = ni->ni_txrate;
	}

	/*
	 * XXX TODO: Group addressed frames aren't aggregated and must
	 * go to the normal non-aggregation queue, and have a NONQOS TID
	 * assigned from net80211.
	 */

	ac = M_WME_GETAC(m);
	if (m->m_flags & M_AMPDU_MPDU) {
		struct ieee80211_tx_ampdu *tap = &ni->ni_tx_ampdu[ac];

		if (!IEEE80211_AMPDU_RUNNING(tap))
			return (EINVAL);

		ac = *(int *)tap->txa_private;
	}

	/* Encrypt the frame if need be. */
	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		/* Retrieve key for TX. */
		k = ieee80211_crypto_encap(ni, m);
		if (k == NULL) {
			return ENOBUFS;
		}
		/* 802.11 header may have moved. */
		wh = mtod(m, struct ieee80211_frame *);
	}
	totlen = m->m_pkthdr.len;

	if (ieee80211_radiotap_active_vap(vap)) {
		struct iwn_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;
		if (k != NULL)
			tap->wt_flags |= IEEE80211_RADIOTAP_F_WEP;

		ieee80211_radiotap_tx(vap, m);
	}

	flags = 0;
	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		/* Unicast frame, check if an ACK is expected. */
		if (!qos || (qos & IEEE80211_QOS_ACKPOLICY) !=
		    IEEE80211_QOS_ACKPOLICY_NOACK)
			flags |= IWN_TX_NEED_ACK;
	}
	if ((wh->i_fc[0] &
	    (IEEE80211_FC0_TYPE_MASK | IEEE80211_FC0_SUBTYPE_MASK)) ==
	    (IEEE80211_FC0_TYPE_CTL | IEEE80211_FC0_SUBTYPE_BAR))
		flags |= IWN_TX_IMM_BA;		/* Cannot happen yet. */

	if (wh->i_fc[1] & IEEE80211_FC1_MORE_FRAG)
		flags |= IWN_TX_MORE_FRAG;	/* Cannot happen yet. */

	/* Check if frame must be protected using RTS/CTS or CTS-to-self. */
	if (!IEEE80211_IS_MULTICAST(wh->i_addr1)) {
		/* NB: Group frames are sent using CCK in 802.11b/g. */
		if (totlen + IEEE80211_CRC_LEN > vap->iv_rtsthreshold) {
			flags |= IWN_TX_NEED_RTS;
		} else if (iwn_check_rate_needs_protection(sc, vap, rate)) {
			if (ic->ic_protmode == IEEE80211_PROT_CTSONLY)
				flags |= IWN_TX_NEED_CTS;
			else if (ic->ic_protmode == IEEE80211_PROT_RTSCTS)
				flags |= IWN_TX_NEED_RTS;
		} else if ((rate & IEEE80211_RATE_MCS) &&
			(ic->ic_htprotmode == IEEE80211_PROT_RTSCTS)) {
			flags |= IWN_TX_NEED_RTS;
		}

		/* XXX HT protection? */

		if (flags & (IWN_TX_NEED_RTS | IWN_TX_NEED_CTS)) {
			if (sc->hw_type != IWN_HW_REV_TYPE_4965) {
				/* 5000 autoselects RTS/CTS or CTS-to-self. */
				flags &= ~(IWN_TX_NEED_RTS | IWN_TX_NEED_CTS);
				flags |= IWN_TX_NEED_PROTECTION;
			} else
				flags |= IWN_TX_FULL_TXOP;
		}
	}

	ring = &sc->txq[ac];
	if (m->m_flags & M_AMPDU_MPDU) {
		uint16_t seqno = ni->ni_txseqs[tid];

		if (ring->queued > IWN_TX_RING_COUNT / 2 &&
		    (ring->cur + 1) % IWN_TX_RING_COUNT == ring->read) {
			DPRINTF(sc, IWN_DEBUG_AMPDU, "%s: no more space "
			    "(queued %d) left in %d queue!\n",
			    __func__, ring->queued, ac);
			return (ENOBUFS);
		}

		/*
		 * Queue this frame to the hardware ring that we've
		 * negotiated AMPDU TX on.
		 *
		 * Note that the sequence number must match the TX slot
		 * being used!
		 */
		if ((seqno % 256) != ring->cur) {
			device_printf(sc->sc_dev,
			    "%s: m=%p: seqno (%d) (%d) != ring index (%d) !\n",
			    __func__,
			    m,
			    seqno,
			    seqno % 256,
			    ring->cur);

			/* XXX until D9195 will not be committed */
			ni->ni_txseqs[tid] &= ~0xff;
			ni->ni_txseqs[tid] += ring->cur;
			seqno = ni->ni_txseqs[tid];
		}

		*(uint16_t *)wh->i_seq =
		    htole16(seqno << IEEE80211_SEQ_SEQ_SHIFT);
		ni->ni_txseqs[tid]++;
	}

	/* Prepare TX firmware command. */
	cmd = &ring->cmd[ring->cur];
	tx = (struct iwn_cmd_data *)cmd->data;

	/* NB: No need to clear tx, all fields are reinitialized here. */
	tx->scratch = 0;	/* clear "scratch" area */

	if (IEEE80211_IS_MULTICAST(wh->i_addr1) ||
	    type != IEEE80211_FC0_TYPE_DATA)
		tx->id = sc->broadcast_id;
	else
		tx->id = wn->id;

	if (type == IEEE80211_FC0_TYPE_MGT) {
		uint8_t subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

		/* Tell HW to set timestamp in probe responses. */
		if (subtype == IEEE80211_FC0_SUBTYPE_PROBE_RESP)
			flags |= IWN_TX_INSERT_TSTAMP;
		if (subtype == IEEE80211_FC0_SUBTYPE_ASSOC_REQ ||
		    subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ)
			tx->timeout = htole16(3);
		else
			tx->timeout = htole16(2);
	} else
		tx->timeout = htole16(0);

	if (tx->id == sc->broadcast_id) {
		/* Group or management frame. */
		tx->linkq = 0;
	} else {
		tx->linkq = iwn_tx_rate_to_linkq_offset(sc, ni, rate);
		flags |= IWN_TX_LINKQ;	/* enable MRR */
	}

	tx->tid = tid;
	tx->rts_ntries = 60;
	tx->data_ntries = 15;
	tx->lifetime = htole32(IWN_LIFETIME_INFINITE);
	tx->rate = iwn_rate_to_plcp(sc, ni, rate);
	tx->security = 0;
	tx->flags = htole32(flags);

	return (iwn_tx_cmd(sc, m, ni, ring));
}

static int
iwn_tx_data_raw(struct iwn_softc *sc, struct mbuf *m,
    struct ieee80211_node *ni, const struct ieee80211_bpf_params *params)
{
	struct ieee80211vap *vap = ni->ni_vap;
	struct iwn_tx_cmd *cmd;
	struct iwn_cmd_data *tx;
	struct ieee80211_frame *wh;
	struct iwn_tx_ring *ring;
	uint32_t flags;
	int ac, rate;
	uint8_t type;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	IWN_LOCK_ASSERT(sc);

	wh = mtod(m, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

	ac = params->ibp_pri & 3;

	/* Choose a TX rate. */
	rate = params->ibp_rate0;

	flags = 0;
	if ((params->ibp_flags & IEEE80211_BPF_NOACK) == 0)
		flags |= IWN_TX_NEED_ACK;
	if (params->ibp_flags & IEEE80211_BPF_RTS) {
		if (sc->hw_type != IWN_HW_REV_TYPE_4965) {
			/* 5000 autoselects RTS/CTS or CTS-to-self. */
			flags &= ~IWN_TX_NEED_RTS;
			flags |= IWN_TX_NEED_PROTECTION;
		} else
			flags |= IWN_TX_NEED_RTS | IWN_TX_FULL_TXOP;
	}
	if (params->ibp_flags & IEEE80211_BPF_CTS) {
		if (sc->hw_type != IWN_HW_REV_TYPE_4965) {
			/* 5000 autoselects RTS/CTS or CTS-to-self. */
			flags &= ~IWN_TX_NEED_CTS;
			flags |= IWN_TX_NEED_PROTECTION;
		} else
			flags |= IWN_TX_NEED_CTS | IWN_TX_FULL_TXOP;
	}

	if (ieee80211_radiotap_active_vap(vap)) {
		struct iwn_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_rate = rate;

		ieee80211_radiotap_tx(vap, m);
	}

	ring = &sc->txq[ac];
	cmd = &ring->cmd[ring->cur];

	tx = (struct iwn_cmd_data *)cmd->data;
	/* NB: No need to clear tx, all fields are reinitialized here. */
	tx->scratch = 0;	/* clear "scratch" area */

	if (type == IEEE80211_FC0_TYPE_MGT) {
		uint8_t subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

		/* Tell HW to set timestamp in probe responses. */
		if (subtype == IEEE80211_FC0_SUBTYPE_PROBE_RESP)
			flags |= IWN_TX_INSERT_TSTAMP;

		if (subtype == IEEE80211_FC0_SUBTYPE_ASSOC_REQ ||
		    subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ)
			tx->timeout = htole16(3);
		else
			tx->timeout = htole16(2);
	} else
		tx->timeout = htole16(0);

	tx->tid = 0;
	tx->id = sc->broadcast_id;
	tx->rts_ntries = params->ibp_try1;
	tx->data_ntries = params->ibp_try0;
	tx->lifetime = htole32(IWN_LIFETIME_INFINITE);
	tx->rate = iwn_rate_to_plcp(sc, ni, rate);
	tx->security = 0;
	tx->flags = htole32(flags);

	/* Group or management frame. */
	tx->linkq = 0;

	return (iwn_tx_cmd(sc, m, ni, ring));
}

static int
iwn_tx_cmd(struct iwn_softc *sc, struct mbuf *m, struct ieee80211_node *ni,
    struct iwn_tx_ring *ring)
{
	struct iwn_ops *ops = &sc->ops;
	struct iwn_tx_cmd *cmd;
	struct iwn_cmd_data *tx;
	struct ieee80211_frame *wh;
	struct iwn_tx_desc *desc;
	struct iwn_tx_data *data;
	bus_dma_segment_t *seg, segs[IWN_MAX_SCATTER];
	struct mbuf *m1;
	u_int hdrlen;
	int totlen, error, pad, nsegs = 0, i;

	wh = mtod(m, struct ieee80211_frame *);
	hdrlen = ieee80211_anyhdrsize(wh);
	totlen = m->m_pkthdr.len;

	desc = &ring->desc[ring->cur];
	data = &ring->data[ring->cur];

	if (__predict_false(data->m != NULL || data->ni != NULL)) {
		device_printf(sc->sc_dev, "%s: ni (%p) or m (%p) for idx %d "
		    "in queue %d is not NULL!\n", __func__, data->ni, data->m,
		    ring->cur, ring->qid);
		return EIO;
	}

	/* Prepare TX firmware command. */
	cmd = &ring->cmd[ring->cur];
	cmd->code = IWN_CMD_TX_DATA;
	cmd->flags = 0;
	cmd->qid = ring->qid;
	cmd->idx = ring->cur;

	tx = (struct iwn_cmd_data *)cmd->data;
	tx->len = htole16(totlen);

	/* Set physical address of "scratch area". */
	tx->loaddr = htole32(IWN_LOADDR(data->scratch_paddr));
	tx->hiaddr = IWN_HIADDR(data->scratch_paddr);
	if (hdrlen & 3) {
		/* First segment length must be a multiple of 4. */
		tx->flags |= htole32(IWN_TX_NEED_PADDING);
		pad = 4 - (hdrlen & 3);
	} else
		pad = 0;

	/* Copy 802.11 header in TX command. */
	memcpy((uint8_t *)(tx + 1), wh, hdrlen);

	/* Trim 802.11 header. */
	m_adj(m, hdrlen);

	error = bus_dmamap_load_mbuf_sg(ring->data_dmat, data->map, m, segs,
	    &nsegs, BUS_DMA_NOWAIT);
	if (error != 0) {
		if (error != EFBIG) {
			device_printf(sc->sc_dev,
			    "%s: can't map mbuf (error %d)\n", __func__, error);
			return error;
		}
		/* Too many DMA segments, linearize mbuf. */
		m1 = m_collapse(m, M_NOWAIT, IWN_MAX_SCATTER - 1);
		if (m1 == NULL) {
			device_printf(sc->sc_dev,
			    "%s: could not defrag mbuf\n", __func__);
			return ENOBUFS;
		}
		m = m1;

		error = bus_dmamap_load_mbuf_sg(ring->data_dmat, data->map, m,
		    segs, &nsegs, BUS_DMA_NOWAIT);
		if (error != 0) {
			/* XXX fix this */
			/*
			 * NB: Do not return error;
			 * original mbuf does not exist anymore.
			 */
			device_printf(sc->sc_dev,
			    "%s: can't map mbuf (error %d)\n",
			    __func__, error);
			if_inc_counter(ni->ni_vap->iv_ifp,
			    IFCOUNTER_OERRORS, 1);
			ieee80211_free_node(ni);
			m_freem(m);
			return 0;
		}
	}

	data->m = m;
	data->ni = ni;

	DPRINTF(sc, IWN_DEBUG_XMIT, "%s: qid %d idx %d len %d nsegs %d "
	    "plcp %d\n",
	    __func__, ring->qid, ring->cur, totlen, nsegs, tx->rate);

	/* Fill TX descriptor. */
	desc->nsegs = 1;
	if (m->m_len != 0)
		desc->nsegs += nsegs;
	/* First DMA segment is used by the TX command. */
	desc->segs[0].addr = htole32(IWN_LOADDR(data->cmd_paddr));
	desc->segs[0].len  = htole16(IWN_HIADDR(data->cmd_paddr) |
	    (4 + sizeof (*tx) + hdrlen + pad) << 4);
	/* Other DMA segments are for data payload. */
	seg = &segs[0];
	for (i = 1; i <= nsegs; i++) {
		desc->segs[i].addr = htole32(IWN_LOADDR(seg->ds_addr));
		desc->segs[i].len  = htole16(IWN_HIADDR(seg->ds_addr) |
		    seg->ds_len << 4);
		seg++;
	}

	bus_dmamap_sync(ring->data_dmat, data->map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(ring->cmd_dma.tag, ring->cmd_dma.map,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(ring->desc_dma.tag, ring->desc_dma.map,
	    BUS_DMASYNC_PREWRITE);

	/* Update TX scheduler. */
	if (ring->qid >= sc->firstaggqueue)
		ops->update_sched(sc, ring->qid, ring->cur, tx->id, totlen);

	/* Kick TX ring. */
	ring->cur = (ring->cur + 1) % IWN_TX_RING_COUNT;
	IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, ring->qid << 8 | ring->cur);

	/* Mark TX ring as full if we reach a certain threshold. */
	if (++ring->queued > IWN_TX_RING_HIMARK)
		sc->qfullmsk |= 1 << ring->qid;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

	return 0;
}

static void
iwn_xmit_task(void *arg0, int pending)
{
	struct iwn_softc *sc = arg0;
	struct ieee80211_node *ni;
	struct mbuf *m;
	int error;
	struct ieee80211_bpf_params p;
	int have_p;

	DPRINTF(sc, IWN_DEBUG_XMIT, "%s: called\n", __func__);

	IWN_LOCK(sc);
	/*
	 * Dequeue frames, attempt to transmit,
	 * then disable beaconwait when we're done.
	 */
	while ((m = mbufq_dequeue(&sc->sc_xmit_queue)) != NULL) {
		have_p = 0;
		ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;

		/* Get xmit params if appropriate */
		if (ieee80211_get_xmit_params(m, &p) == 0)
			have_p = 1;

		DPRINTF(sc, IWN_DEBUG_XMIT, "%s: m=%p, have_p=%d\n",
		    __func__, m, have_p);

		/* If we have xmit params, use them */
		if (have_p)
			error = iwn_tx_data_raw(sc, m, ni, &p);
		else
			error = iwn_tx_data(sc, m, ni);

		if (error != 0) {
			if_inc_counter(ni->ni_vap->iv_ifp,
			    IFCOUNTER_OERRORS, 1);
			ieee80211_free_node(ni);
			m_freem(m);
		}
	}

	sc->sc_beacon_wait = 0;
	IWN_UNLOCK(sc);
}

/*
 * raw frame xmit - free node/reference if failed.
 */
static int
iwn_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
    const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct iwn_softc *sc = ic->ic_softc;
	int error = 0;

	DPRINTF(sc, IWN_DEBUG_XMIT | IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	IWN_LOCK(sc);
	if ((sc->sc_flags & IWN_FLAG_RUNNING) == 0) {
		m_freem(m);
		IWN_UNLOCK(sc);
		return (ENETDOWN);
	}

	/* queue frame if we have to */
	if (sc->sc_beacon_wait) {
		if (iwn_xmit_queue_enqueue(sc, m) != 0) {
			m_freem(m);
			IWN_UNLOCK(sc);
			return (ENOBUFS);
		}
		/* Queued, so just return OK */
		IWN_UNLOCK(sc);
		return (0);
	}

	if (params == NULL) {
		/*
		 * Legacy path; interpret frame contents to decide
		 * precisely how to send the frame.
		 */
		error = iwn_tx_data(sc, m, ni);
	} else {
		/*
		 * Caller supplied explicit parameters to use in
		 * sending the frame.
		 */
		error = iwn_tx_data_raw(sc, m, ni, params);
	}
	if (error == 0)
		sc->sc_tx_timer = 5;
	else
		m_freem(m);

	IWN_UNLOCK(sc);

	DPRINTF(sc, IWN_DEBUG_TRACE | IWN_DEBUG_XMIT, "->%s: end\n",__func__);

	return (error);
}

/*
 * transmit - don't free mbuf if failed; don't free node ref if failed.
 */
static int
iwn_transmit(struct ieee80211com *ic, struct mbuf *m)
{
	struct iwn_softc *sc = ic->ic_softc;
	struct ieee80211_node *ni;
	int error;

	ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;

	IWN_LOCK(sc);
	if ((sc->sc_flags & IWN_FLAG_RUNNING) == 0 || sc->sc_beacon_wait) {
		IWN_UNLOCK(sc);
		return (ENXIO);
	}

	if (sc->qfullmsk) {
		IWN_UNLOCK(sc);
		return (ENOBUFS);
	}

	error = iwn_tx_data(sc, m, ni);
	if (!error)
		sc->sc_tx_timer = 5;
	IWN_UNLOCK(sc);
	return (error);
}

static void
iwn_scan_timeout(void *arg)
{
	struct iwn_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;

	ic_printf(ic, "scan timeout\n");
	ieee80211_restart_all(ic);
}

static void
iwn_watchdog(void *arg)
{
	struct iwn_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;

	IWN_LOCK_ASSERT(sc);

	KASSERT(sc->sc_flags & IWN_FLAG_RUNNING, ("not running"));

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			ic_printf(ic, "device timeout\n");
			ieee80211_restart_all(ic);
			return;
		}
	}
	callout_reset(&sc->watchdog_to, hz, iwn_watchdog, sc);
}

static int
iwn_cdev_open(struct cdev *dev, int flags, int type, struct thread *td)
{

	return (0);
}

static int
iwn_cdev_close(struct cdev *dev, int flags, int type, struct thread *td)
{

	return (0);
}

static int
iwn_cdev_ioctl(struct cdev *dev, unsigned long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	int rc;
	struct iwn_softc *sc = dev->si_drv1;
	struct iwn_ioctl_data *d;

	rc = priv_check(td, PRIV_DRIVER);
	if (rc != 0)
		return (0);

	switch (cmd) {
	case SIOCGIWNSTATS:
		d = (struct iwn_ioctl_data *) data;
		IWN_LOCK(sc);
		/* XXX validate permissions/memory/etc? */
		rc = copyout(&sc->last_stat, d->dst_addr, sizeof(struct iwn_stats));
		IWN_UNLOCK(sc);
		break;
	case SIOCZIWNSTATS:
		IWN_LOCK(sc);
		memset(&sc->last_stat, 0, sizeof(struct iwn_stats));
		IWN_UNLOCK(sc);
		break;
	default:
		rc = EINVAL;
		break;
	}
	return (rc);
}

static int
iwn_ioctl(struct ieee80211com *ic, u_long cmd, void *data)
{

	return (ENOTTY);
}

static void
iwn_parent(struct ieee80211com *ic)
{
	struct iwn_softc *sc = ic->ic_softc;
	struct ieee80211vap *vap;
	int error;

	if (ic->ic_nrunning > 0) {
		error = iwn_init(sc);

		switch (error) {
		case 0:
			ieee80211_start_all(ic);
			break;
		case 1:
			/* radio is disabled via RFkill switch */
			taskqueue_enqueue(sc->sc_tq, &sc->sc_rftoggle_task);
			break;
		default:
			vap = TAILQ_FIRST(&ic->ic_vaps);
			if (vap != NULL)
				ieee80211_stop(vap);
			break;
		}
	} else
		iwn_stop(sc);
}

/*
 * Send a command to the firmware.
 */
static int
iwn_cmd(struct iwn_softc *sc, int code, const void *buf, int size, int async)
{
	struct iwn_tx_ring *ring;
	struct iwn_tx_desc *desc;
	struct iwn_tx_data *data;
	struct iwn_tx_cmd *cmd;
	struct mbuf *m;
	bus_addr_t paddr;
	int totlen, error;
	int cmd_queue_num;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	if (async == 0)
		IWN_LOCK_ASSERT(sc);

	if (sc->sc_flags & IWN_FLAG_PAN_SUPPORT)
		cmd_queue_num = IWN_PAN_CMD_QUEUE;
	else
		cmd_queue_num = IWN_CMD_QUEUE_NUM;

	ring = &sc->txq[cmd_queue_num];
	desc = &ring->desc[ring->cur];
	data = &ring->data[ring->cur];
	totlen = 4 + size;

	if (size > sizeof cmd->data) {
		/* Command is too large to fit in a descriptor. */
		if (totlen > MCLBYTES)
			return EINVAL;
		m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MJUMPAGESIZE);
		if (m == NULL)
			return ENOMEM;
		cmd = mtod(m, struct iwn_tx_cmd *);
		error = bus_dmamap_load(ring->data_dmat, data->map, cmd,
		    totlen, iwn_dma_map_addr, &paddr, BUS_DMA_NOWAIT);
		if (error != 0) {
			m_freem(m);
			return error;
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

	desc->nsegs = 1;
	desc->segs[0].addr = htole32(IWN_LOADDR(paddr));
	desc->segs[0].len  = htole16(IWN_HIADDR(paddr) | totlen << 4);

	DPRINTF(sc, IWN_DEBUG_CMD, "%s: %s (0x%x) flags %d qid %d idx %d\n",
	    __func__, iwn_intr_str(cmd->code), cmd->code,
	    cmd->flags, cmd->qid, cmd->idx);

	if (size > sizeof cmd->data) {
		bus_dmamap_sync(ring->data_dmat, data->map,
		    BUS_DMASYNC_PREWRITE);
	} else {
		bus_dmamap_sync(ring->cmd_dma.tag, ring->cmd_dma.map,
		    BUS_DMASYNC_PREWRITE);
	}
	bus_dmamap_sync(ring->desc_dma.tag, ring->desc_dma.map,
	    BUS_DMASYNC_PREWRITE);

	/* Kick command ring. */
	ring->cur = (ring->cur + 1) % IWN_TX_RING_COUNT;
	IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, ring->qid << 8 | ring->cur);

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

	return async ? 0 : msleep(desc, &sc->sc_mtx, PCATCH, "iwncmd", hz);
}

static int
iwn4965_add_node(struct iwn_softc *sc, struct iwn_node_info *node, int async)
{
	struct iwn4965_node_info hnode;
	caddr_t src, dst;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	/*
	 * We use the node structure for 5000 Series internally (it is
	 * a superset of the one for 4965AGN). We thus copy the common
	 * fields before sending the command.
	 */
	src = (caddr_t)node;
	dst = (caddr_t)&hnode;
	memcpy(dst, src, 48);
	/* Skip TSC, RX MIC and TX MIC fields from ``src''. */
	memcpy(dst + 48, src + 72, 20);
	return iwn_cmd(sc, IWN_CMD_ADD_NODE, &hnode, sizeof hnode, async);
}

static int
iwn5000_add_node(struct iwn_softc *sc, struct iwn_node_info *node, int async)
{

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	/* Direct mapping. */
	return iwn_cmd(sc, IWN_CMD_ADD_NODE, node, sizeof (*node), async);
}

static int
iwn_set_link_quality(struct iwn_softc *sc, struct ieee80211_node *ni)
{
	struct iwn_node *wn = (void *)ni;
	struct ieee80211_rateset *rs;
	struct iwn_cmd_link_quality linkq;
	int i, rate, txrate;
	int is_11n;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	memset(&linkq, 0, sizeof linkq);
	linkq.id = wn->id;
	linkq.antmsk_1stream = iwn_get_1stream_tx_antmask(sc);
	linkq.antmsk_2stream = iwn_get_2stream_tx_antmask(sc);

	linkq.ampdu_max = 32;		/* XXX negotiated? */
	linkq.ampdu_threshold = 3;
	linkq.ampdu_limit = htole16(4000);	/* 4ms */

	DPRINTF(sc, IWN_DEBUG_XMIT,
	    "%s: 1stream antenna=0x%02x, 2stream antenna=0x%02x, ntxstreams=%d\n",
	    __func__,
	    linkq.antmsk_1stream,
	    linkq.antmsk_2stream,
	    sc->ntxchains);

	/*
	 * Are we using 11n rates? Ensure the channel is
	 * 11n _and_ we have some 11n rates, or don't
	 * try.
	 */
	if (IEEE80211_IS_CHAN_HT(ni->ni_chan) && ni->ni_htrates.rs_nrates > 0) {
		rs = (struct ieee80211_rateset *) &ni->ni_htrates;
		is_11n = 1;
	} else {
		rs = &ni->ni_rates;
		is_11n = 0;
	}

	/* Start at highest available bit-rate. */
	/*
	 * XXX this is all very dirty!
	 */
	if (is_11n)
		txrate = ni->ni_htrates.rs_nrates - 1;
	else
		txrate = rs->rs_nrates - 1;
	for (i = 0; i < IWN_MAX_TX_RETRIES; i++) {
		uint32_t plcp;

		/*
		 * XXX TODO: ensure the last two slots are the two lowest
		 * rate entries, just for now.
		 */
		if (i == 14 || i == 15)
			txrate = 0;

		if (is_11n)
			rate = IEEE80211_RATE_MCS | rs->rs_rates[txrate];
		else
			rate = IEEE80211_RV(rs->rs_rates[txrate]);

		/* Do rate -> PLCP config mapping */
		plcp = iwn_rate_to_plcp(sc, ni, rate);
		linkq.retry[i] = plcp;
		DPRINTF(sc, IWN_DEBUG_XMIT,
		    "%s: i=%d, txrate=%d, rate=0x%02x, plcp=0x%08x\n",
		    __func__,
		    i,
		    txrate,
		    rate,
		    le32toh(plcp));

		/*
		 * The mimo field is an index into the table which
		 * indicates the first index where it and subsequent entries
		 * will not be using MIMO.
		 *
		 * Since we're filling linkq from 0..15 and we're filling
		 * from the highest MCS rates to the lowest rates, if we
		 * _are_ doing a dual-stream rate, set mimo to idx+1 (ie,
		 * the next entry.)  That way if the next entry is a non-MIMO
		 * entry, we're already pointing at it.
		 */
		if ((le32toh(plcp) & IWN_RFLAG_MCS) &&
		    IEEE80211_RV(le32toh(plcp)) > 7)
			linkq.mimo = i + 1;

		/* Next retry at immediate lower bit-rate. */
		if (txrate > 0)
			txrate--;
	}
	/*
	 * If we reached the end of the list and indeed we hit
	 * all MIMO rates (eg 5300 doing MCS23-15) then yes,
	 * set mimo to 15.  Setting it to 16 panics the firmware.
	 */
	if (linkq.mimo > 15)
		linkq.mimo = 15;

	DPRINTF(sc, IWN_DEBUG_XMIT, "%s: mimo = %d\n", __func__, linkq.mimo);

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

	return iwn_cmd(sc, IWN_CMD_LINK_QUALITY, &linkq, sizeof linkq, 1);
}

/*
 * Broadcast node is used to send group-addressed and management frames.
 */
static int
iwn_add_broadcast_node(struct iwn_softc *sc, int async)
{
	struct iwn_ops *ops = &sc->ops;
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwn_node_info node;
	struct iwn_cmd_link_quality linkq;
	uint8_t txant;
	int i, error;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	sc->rxon = &sc->rx_on[IWN_RXON_BSS_CTX];

	memset(&node, 0, sizeof node);
	IEEE80211_ADDR_COPY(node.macaddr, ieee80211broadcastaddr);
	node.id = sc->broadcast_id;
	DPRINTF(sc, IWN_DEBUG_RESET, "%s: adding broadcast node\n", __func__);
	if ((error = ops->add_node(sc, &node, async)) != 0)
		return error;

	/* Use the first valid TX antenna. */
	txant = IWN_LSB(sc->txchainmask);

	memset(&linkq, 0, sizeof linkq);
	linkq.id = sc->broadcast_id;
	linkq.antmsk_1stream = iwn_get_1stream_tx_antmask(sc);
	linkq.antmsk_2stream = iwn_get_2stream_tx_antmask(sc);
	linkq.ampdu_max = 64;
	linkq.ampdu_threshold = 3;
	linkq.ampdu_limit = htole16(4000);	/* 4ms */

	/* Use lowest mandatory bit-rate. */
	/* XXX rate table lookup? */
	if (IEEE80211_IS_CHAN_5GHZ(ic->ic_curchan))
		linkq.retry[0] = htole32(0xd);
	else
		linkq.retry[0] = htole32(10 | IWN_RFLAG_CCK);
	linkq.retry[0] |= htole32(IWN_RFLAG_ANT(txant));
	/* Use same bit-rate for all TX retries. */
	for (i = 1; i < IWN_MAX_TX_RETRIES; i++) {
		linkq.retry[i] = linkq.retry[0];
	}

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

	return iwn_cmd(sc, IWN_CMD_LINK_QUALITY, &linkq, sizeof linkq, async);
}

static int
iwn_updateedca(struct ieee80211com *ic)
{
#define IWN_EXP2(x)	((1 << (x)) - 1)	/* CWmin = 2^ECWmin - 1 */
	struct iwn_softc *sc = ic->ic_softc;
	struct iwn_edca_params cmd;
	struct chanAccParams chp;
	int aci;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	ieee80211_wme_ic_getparams(ic, &chp);

	memset(&cmd, 0, sizeof cmd);
	cmd.flags = htole32(IWN_EDCA_UPDATE);

	IEEE80211_LOCK(ic);
	for (aci = 0; aci < WME_NUM_AC; aci++) {
		const struct wmeParams *ac = &chp.cap_wmeParams[aci];
		cmd.ac[aci].aifsn = ac->wmep_aifsn;
		cmd.ac[aci].cwmin = htole16(IWN_EXP2(ac->wmep_logcwmin));
		cmd.ac[aci].cwmax = htole16(IWN_EXP2(ac->wmep_logcwmax));
		cmd.ac[aci].txoplimit =
		    htole16(IEEE80211_TXOP_TO_US(ac->wmep_txopLimit));
	}
	IEEE80211_UNLOCK(ic);

	IWN_LOCK(sc);
	(void)iwn_cmd(sc, IWN_CMD_EDCA_PARAMS, &cmd, sizeof cmd, 1);
	IWN_UNLOCK(sc);

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

	return 0;
#undef IWN_EXP2
}

static void
iwn_set_promisc(struct iwn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t promisc_filter;

	promisc_filter = IWN_FILTER_CTL | IWN_FILTER_PROMISC;
	if (ic->ic_promisc > 0 || ic->ic_opmode == IEEE80211_M_MONITOR)
		sc->rxon->filter |= htole32(promisc_filter);
	else
		sc->rxon->filter &= ~htole32(promisc_filter);
}

static void
iwn_update_promisc(struct ieee80211com *ic)
{
	struct iwn_softc *sc = ic->ic_softc;
	int error;

	if (ic->ic_opmode == IEEE80211_M_MONITOR)
		return;		/* nothing to do */

	IWN_LOCK(sc);
	if (!(sc->sc_flags & IWN_FLAG_RUNNING)) {
		IWN_UNLOCK(sc);
		return;
	}

	iwn_set_promisc(sc);
	if ((error = iwn_send_rxon(sc, 1, 1)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not send RXON, error %d\n",
		    __func__, error);
	}
	IWN_UNLOCK(sc);
}

static void
iwn_update_mcast(struct ieee80211com *ic)
{
	/* Ignore */
}

static void
iwn_set_led(struct iwn_softc *sc, uint8_t which, uint8_t off, uint8_t on)
{
	struct iwn_cmd_led led;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

#if 0
	/* XXX don't set LEDs during scan? */
	if (sc->sc_is_scanning)
		return;
#endif

	/* Clear microcode LED ownership. */
	IWN_CLRBITS(sc, IWN_LED, IWN_LED_BSM_CTRL);

	led.which = which;
	led.unit = htole32(10000);	/* on/off in unit of 100ms */
	led.off = off;
	led.on = on;
	(void)iwn_cmd(sc, IWN_CMD_SET_LED, &led, sizeof led, 1);
}

/*
 * Set the critical temperature at which the firmware will stop the radio
 * and notify us.
 */
static int
iwn_set_critical_temp(struct iwn_softc *sc)
{
	struct iwn_critical_temp crit;
	int32_t temp;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	IWN_WRITE(sc, IWN_UCODE_GP1_CLR, IWN_UCODE_GP1_CTEMP_STOP_RF);

	if (sc->hw_type == IWN_HW_REV_TYPE_5150)
		temp = (IWN_CTOK(110) - sc->temp_off) * -5;
	else if (sc->hw_type == IWN_HW_REV_TYPE_4965)
		temp = IWN_CTOK(110);
	else
		temp = 110;
	memset(&crit, 0, sizeof crit);
	crit.tempR = htole32(temp);
	DPRINTF(sc, IWN_DEBUG_RESET, "setting critical temp to %d\n", temp);
	return iwn_cmd(sc, IWN_CMD_SET_CRITICAL_TEMP, &crit, sizeof crit, 0);
}

static int
iwn_set_timing(struct iwn_softc *sc, struct ieee80211_node *ni)
{
	struct iwn_cmd_timing cmd;
	uint64_t val, mod;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	memset(&cmd, 0, sizeof cmd);
	memcpy(&cmd.tstamp, ni->ni_tstamp.data, sizeof (uint64_t));
	cmd.bintval = htole16(ni->ni_intval);
	cmd.lintval = htole16(10);

	/* Compute remaining time until next beacon. */
	val = (uint64_t)ni->ni_intval * IEEE80211_DUR_TU;
	mod = le64toh(cmd.tstamp) % val;
	cmd.binitval = htole32((uint32_t)(val - mod));

	DPRINTF(sc, IWN_DEBUG_RESET, "timing bintval=%u tstamp=%ju, init=%u\n",
	    ni->ni_intval, le64toh(cmd.tstamp), (uint32_t)(val - mod));

	return iwn_cmd(sc, IWN_CMD_TIMING, &cmd, sizeof cmd, 1);
}

static void
iwn4965_power_calibration(struct iwn_softc *sc, int temp)
{

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	/* Adjust TX power if need be (delta >= 3 degC). */
	DPRINTF(sc, IWN_DEBUG_CALIBRATE, "%s: temperature %d->%d\n",
	    __func__, sc->temp, temp);
	if (abs(temp - sc->temp) >= 3) {
		/* Record temperature of last calibration. */
		sc->temp = temp;
		(void)iwn4965_set_txpower(sc, 1);
	}
}

/*
 * Set TX power for current channel (each rate has its own power settings).
 * This function takes into account the regulatory information from EEPROM,
 * the current temperature and the current voltage.
 */
static int
iwn4965_set_txpower(struct iwn_softc *sc, int async)
{
/* Fixed-point arithmetic division using a n-bit fractional part. */
#define fdivround(a, b, n)	\
	((((1 << n) * (a)) / (b) + (1 << n) / 2) / (1 << n))
/* Linear interpolation. */
#define interpolate(x, x1, y1, x2, y2, n)	\
	((y1) + fdivround(((int)(x) - (x1)) * ((y2) - (y1)), (x2) - (x1), n))

	static const int tdiv[IWN_NATTEN_GROUPS] = { 9, 8, 8, 8, 6 };
	struct iwn_ucode_info *uc = &sc->ucode_info;
	struct iwn4965_cmd_txpower cmd;
	struct iwn4965_eeprom_chan_samples *chans;
	const uint8_t *rf_gain, *dsp_gain;
	int32_t vdiff, tdiff;
	int i, is_chan_5ghz, c, grp, maxpwr;
	uint8_t chan;

	sc->rxon = &sc->rx_on[IWN_RXON_BSS_CTX];
	/* Retrieve current channel from last RXON. */
	chan = sc->rxon->chan;
	is_chan_5ghz = (sc->rxon->flags & htole32(IWN_RXON_24GHZ)) == 0;
	DPRINTF(sc, IWN_DEBUG_RESET, "setting TX power for channel %d\n",
	    chan);

	memset(&cmd, 0, sizeof cmd);
	cmd.band = is_chan_5ghz ? 0 : 1;
	cmd.chan = chan;

	if (is_chan_5ghz) {
		maxpwr   = sc->maxpwr5GHz;
		rf_gain  = iwn4965_rf_gain_5ghz;
		dsp_gain = iwn4965_dsp_gain_5ghz;
	} else {
		maxpwr   = sc->maxpwr2GHz;
		rf_gain  = iwn4965_rf_gain_2ghz;
		dsp_gain = iwn4965_dsp_gain_2ghz;
	}

	/* Compute voltage compensation. */
	vdiff = ((int32_t)le32toh(uc->volt) - sc->eeprom_voltage) / 7;
	if (vdiff > 0)
		vdiff *= 2;
	if (abs(vdiff) > 2)
		vdiff = 0;
	DPRINTF(sc, IWN_DEBUG_CALIBRATE | IWN_DEBUG_TXPOW,
	    "%s: voltage compensation=%d (UCODE=%d, EEPROM=%d)\n",
	    __func__, vdiff, le32toh(uc->volt), sc->eeprom_voltage);

	/* Get channel attenuation group. */
	if (chan <= 20)		/* 1-20 */
		grp = 4;
	else if (chan <= 43)	/* 34-43 */
		grp = 0;
	else if (chan <= 70)	/* 44-70 */
		grp = 1;
	else if (chan <= 124)	/* 71-124 */
		grp = 2;
	else			/* 125-200 */
		grp = 3;
	DPRINTF(sc, IWN_DEBUG_CALIBRATE | IWN_DEBUG_TXPOW,
	    "%s: chan %d, attenuation group=%d\n", __func__, chan, grp);

	/* Get channel sub-band. */
	for (i = 0; i < IWN_NBANDS; i++)
		if (sc->bands[i].lo != 0 &&
		    sc->bands[i].lo <= chan && chan <= sc->bands[i].hi)
			break;
	if (i == IWN_NBANDS)	/* Can't happen in real-life. */
		return EINVAL;
	chans = sc->bands[i].chans;
	DPRINTF(sc, IWN_DEBUG_CALIBRATE | IWN_DEBUG_TXPOW,
	    "%s: chan %d sub-band=%d\n", __func__, chan, i);

	for (c = 0; c < 2; c++) {
		uint8_t power, gain, temp;
		int maxchpwr, pwr, ridx, idx;

		power = interpolate(chan,
		    chans[0].num, chans[0].samples[c][1].power,
		    chans[1].num, chans[1].samples[c][1].power, 1);
		gain  = interpolate(chan,
		    chans[0].num, chans[0].samples[c][1].gain,
		    chans[1].num, chans[1].samples[c][1].gain, 1);
		temp  = interpolate(chan,
		    chans[0].num, chans[0].samples[c][1].temp,
		    chans[1].num, chans[1].samples[c][1].temp, 1);
		DPRINTF(sc, IWN_DEBUG_CALIBRATE | IWN_DEBUG_TXPOW,
		    "%s: Tx chain %d: power=%d gain=%d temp=%d\n",
		    __func__, c, power, gain, temp);

		/* Compute temperature compensation. */
		tdiff = ((sc->temp - temp) * 2) / tdiv[grp];
		DPRINTF(sc, IWN_DEBUG_CALIBRATE | IWN_DEBUG_TXPOW,
		    "%s: temperature compensation=%d (current=%d, EEPROM=%d)\n",
		    __func__, tdiff, sc->temp, temp);

		for (ridx = 0; ridx <= IWN_RIDX_MAX; ridx++) {
			/* Convert dBm to half-dBm. */
			maxchpwr = sc->maxpwr[chan] * 2;
			if ((ridx / 8) & 1)
				maxchpwr -= 6;	/* MIMO 2T: -3dB */

			pwr = maxpwr;

			/* Adjust TX power based on rate. */
			if ((ridx % 8) == 5)
				pwr -= 15;	/* OFDM48: -7.5dB */
			else if ((ridx % 8) == 6)
				pwr -= 17;	/* OFDM54: -8.5dB */
			else if ((ridx % 8) == 7)
				pwr -= 20;	/* OFDM60: -10dB */
			else
				pwr -= 10;	/* Others: -5dB */

			/* Do not exceed channel max TX power. */
			if (pwr > maxchpwr)
				pwr = maxchpwr;

			idx = gain - (pwr - power) - tdiff - vdiff;
			if ((ridx / 8) & 1)	/* MIMO */
				idx += (int32_t)le32toh(uc->atten[grp][c]);

			if (cmd.band == 0)
				idx += 9;	/* 5GHz */
			if (ridx == IWN_RIDX_MAX)
				idx += 5;	/* CCK */

			/* Make sure idx stays in a valid range. */
			if (idx < 0)
				idx = 0;
			else if (idx > IWN4965_MAX_PWR_INDEX)
				idx = IWN4965_MAX_PWR_INDEX;

			DPRINTF(sc, IWN_DEBUG_CALIBRATE | IWN_DEBUG_TXPOW,
			    "%s: Tx chain %d, rate idx %d: power=%d\n",
			    __func__, c, ridx, idx);
			cmd.power[ridx].rf_gain[c] = rf_gain[idx];
			cmd.power[ridx].dsp_gain[c] = dsp_gain[idx];
		}
	}

	DPRINTF(sc, IWN_DEBUG_CALIBRATE | IWN_DEBUG_TXPOW,
	    "%s: set tx power for chan %d\n", __func__, chan);
	return iwn_cmd(sc, IWN_CMD_TXPOWER, &cmd, sizeof cmd, async);

#undef interpolate
#undef fdivround
}

static int
iwn5000_set_txpower(struct iwn_softc *sc, int async)
{
	struct iwn5000_cmd_txpower cmd;
	int cmdid;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	/*
	 * TX power calibration is handled automatically by the firmware
	 * for 5000 Series.
	 */
	memset(&cmd, 0, sizeof cmd);
	cmd.global_limit = 2 * IWN5000_TXPOWER_MAX_DBM;	/* 16 dBm */
	cmd.flags = IWN5000_TXPOWER_NO_CLOSED;
	cmd.srv_limit = IWN5000_TXPOWER_AUTO;
	DPRINTF(sc, IWN_DEBUG_CALIBRATE | IWN_DEBUG_XMIT,
	    "%s: setting TX power; rev=%d\n",
	    __func__,
	    IWN_UCODE_API(sc->ucode_rev));
	if (IWN_UCODE_API(sc->ucode_rev) == 1)
		cmdid = IWN_CMD_TXPOWER_DBM_V1;
	else
		cmdid = IWN_CMD_TXPOWER_DBM;
	return iwn_cmd(sc, cmdid, &cmd, sizeof cmd, async);
}

/*
 * Retrieve the maximum RSSI (in dBm) among receivers.
 */
static int
iwn4965_get_rssi(struct iwn_softc *sc, struct iwn_rx_stat *stat)
{
	struct iwn4965_rx_phystat *phy = (void *)stat->phybuf;
	uint8_t mask, agc;
	int rssi;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	mask = (le16toh(phy->antenna) >> 4) & IWN_ANT_ABC;
	agc  = (le16toh(phy->agc) >> 7) & 0x7f;

	rssi = 0;
	if (mask & IWN_ANT_A)
		rssi = MAX(rssi, phy->rssi[0]);
	if (mask & IWN_ANT_B)
		rssi = MAX(rssi, phy->rssi[2]);
	if (mask & IWN_ANT_C)
		rssi = MAX(rssi, phy->rssi[4]);

	DPRINTF(sc, IWN_DEBUG_RECV,
	    "%s: agc %d mask 0x%x rssi %d %d %d result %d\n", __func__, agc,
	    mask, phy->rssi[0], phy->rssi[2], phy->rssi[4],
	    rssi - agc - IWN_RSSI_TO_DBM);
	return rssi - agc - IWN_RSSI_TO_DBM;
}

static int
iwn5000_get_rssi(struct iwn_softc *sc, struct iwn_rx_stat *stat)
{
	struct iwn5000_rx_phystat *phy = (void *)stat->phybuf;
	uint8_t agc;
	int rssi;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	agc = (le32toh(phy->agc) >> 9) & 0x7f;

	rssi = MAX(le16toh(phy->rssi[0]) & 0xff,
		   le16toh(phy->rssi[1]) & 0xff);
	rssi = MAX(le16toh(phy->rssi[2]) & 0xff, rssi);

	DPRINTF(sc, IWN_DEBUG_RECV,
	    "%s: agc %d rssi %d %d %d result %d\n", __func__, agc,
	    phy->rssi[0], phy->rssi[1], phy->rssi[2],
	    rssi - agc - IWN_RSSI_TO_DBM);
	return rssi - agc - IWN_RSSI_TO_DBM;
}

/*
 * Retrieve the average noise (in dBm) among receivers.
 */
static int
iwn_get_noise(const struct iwn_rx_general_stats *stats)
{
	int i, total, nbant, noise;

	total = nbant = 0;
	for (i = 0; i < 3; i++) {
		if ((noise = le32toh(stats->noise[i]) & 0xff) == 0)
			continue;
		total += noise;
		nbant++;
	}
	/* There should be at least one antenna but check anyway. */
	return (nbant == 0) ? -127 : (total / nbant) - 107;
}

/*
 * Compute temperature (in degC) from last received statistics.
 */
static int
iwn4965_get_temperature(struct iwn_softc *sc)
{
	struct iwn_ucode_info *uc = &sc->ucode_info;
	int32_t r1, r2, r3, r4, temp;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	r1 = le32toh(uc->temp[0].chan20MHz);
	r2 = le32toh(uc->temp[1].chan20MHz);
	r3 = le32toh(uc->temp[2].chan20MHz);
	r4 = le32toh(sc->rawtemp);

	if (r1 == r3)	/* Prevents division by 0 (should not happen). */
		return 0;

	/* Sign-extend 23-bit R4 value to 32-bit. */
	r4 = ((r4 & 0xffffff) ^ 0x800000) - 0x800000;
	/* Compute temperature in Kelvin. */
	temp = (259 * (r4 - r2)) / (r3 - r1);
	temp = (temp * 97) / 100 + 8;

	DPRINTF(sc, IWN_DEBUG_ANY, "temperature %dK/%dC\n", temp,
	    IWN_KTOC(temp));
	return IWN_KTOC(temp);
}

static int
iwn5000_get_temperature(struct iwn_softc *sc)
{
	int32_t temp;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	/*
	 * Temperature is not used by the driver for 5000 Series because
	 * TX power calibration is handled by firmware.
	 */
	temp = le32toh(sc->rawtemp);
	if (sc->hw_type == IWN_HW_REV_TYPE_5150) {
		temp = (temp / -5) + sc->temp_off;
		temp = IWN_KTOC(temp);
	}
	return temp;
}

/*
 * Initialize sensitivity calibration state machine.
 */
static int
iwn_init_sensitivity(struct iwn_softc *sc)
{
	struct iwn_ops *ops = &sc->ops;
	struct iwn_calib_state *calib = &sc->calib;
	uint32_t flags;
	int error;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	/* Reset calibration state machine. */
	memset(calib, 0, sizeof (*calib));
	calib->state = IWN_CALIB_STATE_INIT;
	calib->cck_state = IWN_CCK_STATE_HIFA;
	/* Set initial correlation values. */
	calib->ofdm_x1     = sc->limits->min_ofdm_x1;
	calib->ofdm_mrc_x1 = sc->limits->min_ofdm_mrc_x1;
	calib->ofdm_x4     = sc->limits->min_ofdm_x4;
	calib->ofdm_mrc_x4 = sc->limits->min_ofdm_mrc_x4;
	calib->cck_x4      = 125;
	calib->cck_mrc_x4  = sc->limits->min_cck_mrc_x4;
	calib->energy_cck  = sc->limits->energy_cck;

	/* Write initial sensitivity. */
	if ((error = iwn_send_sensitivity(sc)) != 0)
		return error;

	/* Write initial gains. */
	if ((error = ops->init_gains(sc)) != 0)
		return error;

	/* Request statistics at each beacon interval. */
	flags = 0;
	DPRINTF(sc, IWN_DEBUG_CALIBRATE, "%s: sending request for statistics\n",
	    __func__);
	return iwn_cmd(sc, IWN_CMD_GET_STATISTICS, &flags, sizeof flags, 1);
}

/*
 * Collect noise and RSSI statistics for the first 20 beacons received
 * after association and use them to determine connected antennas and
 * to set differential gains.
 */
static void
iwn_collect_noise(struct iwn_softc *sc,
    const struct iwn_rx_general_stats *stats)
{
	struct iwn_ops *ops = &sc->ops;
	struct iwn_calib_state *calib = &sc->calib;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t val;
	int i;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	/* Accumulate RSSI and noise for all 3 antennas. */
	for (i = 0; i < 3; i++) {
		calib->rssi[i] += le32toh(stats->rssi[i]) & 0xff;
		calib->noise[i] += le32toh(stats->noise[i]) & 0xff;
	}
	/* NB: We update differential gains only once after 20 beacons. */
	if (++calib->nbeacons < 20)
		return;

	/* Determine highest average RSSI. */
	val = MAX(calib->rssi[0], calib->rssi[1]);
	val = MAX(calib->rssi[2], val);

	/* Determine which antennas are connected. */
	sc->chainmask = sc->rxchainmask;
	for (i = 0; i < 3; i++)
		if (val - calib->rssi[i] > 15 * 20)
			sc->chainmask &= ~(1 << i);
	DPRINTF(sc, IWN_DEBUG_CALIBRATE | IWN_DEBUG_XMIT,
	    "%s: RX chains mask: theoretical=0x%x, actual=0x%x\n",
	    __func__, sc->rxchainmask, sc->chainmask);

	/* If none of the TX antennas are connected, keep at least one. */
	if ((sc->chainmask & sc->txchainmask) == 0)
		sc->chainmask |= IWN_LSB(sc->txchainmask);

	(void)ops->set_gains(sc);
	calib->state = IWN_CALIB_STATE_RUN;

#ifdef notyet
	/* XXX Disable RX chains with no antennas connected. */
	sc->rxon->rxchain = htole16(IWN_RXCHAIN_SEL(sc->chainmask));
	if (sc->sc_is_scanning)
		device_printf(sc->sc_dev,
		    "%s: is_scanning set, before RXON\n",
		    __func__);
	(void)iwn_cmd(sc, IWN_CMD_RXON, sc->rxon, sc->rxonsz, 1);
#endif

	/* Enable power-saving mode if requested by user. */
	if (ic->ic_flags & IEEE80211_F_PMGTON)
		(void)iwn_set_pslevel(sc, 0, 3, 1);

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

}

static int
iwn4965_init_gains(struct iwn_softc *sc)
{
	struct iwn_phy_calib_gain cmd;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	memset(&cmd, 0, sizeof cmd);
	cmd.code = IWN4965_PHY_CALIB_DIFF_GAIN;
	/* Differential gains initially set to 0 for all 3 antennas. */
	DPRINTF(sc, IWN_DEBUG_CALIBRATE,
	    "%s: setting initial differential gains\n", __func__);
	return iwn_cmd(sc, IWN_CMD_PHY_CALIB, &cmd, sizeof cmd, 1);
}

static int
iwn5000_init_gains(struct iwn_softc *sc)
{
	struct iwn_phy_calib cmd;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	memset(&cmd, 0, sizeof cmd);
	cmd.code = sc->reset_noise_gain;
	cmd.ngroups = 1;
	cmd.isvalid = 1;
	DPRINTF(sc, IWN_DEBUG_CALIBRATE,
	    "%s: setting initial differential gains\n", __func__);
	return iwn_cmd(sc, IWN_CMD_PHY_CALIB, &cmd, sizeof cmd, 1);
}

static int
iwn4965_set_gains(struct iwn_softc *sc)
{
	struct iwn_calib_state *calib = &sc->calib;
	struct iwn_phy_calib_gain cmd;
	int i, delta, noise;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	/* Get minimal noise among connected antennas. */
	noise = INT_MAX;	/* NB: There's at least one antenna. */
	for (i = 0; i < 3; i++)
		if (sc->chainmask & (1 << i))
			noise = MIN(calib->noise[i], noise);

	memset(&cmd, 0, sizeof cmd);
	cmd.code = IWN4965_PHY_CALIB_DIFF_GAIN;
	/* Set differential gains for connected antennas. */
	for (i = 0; i < 3; i++) {
		if (sc->chainmask & (1 << i)) {
			/* Compute attenuation (in unit of 1.5dB). */
			delta = (noise - (int32_t)calib->noise[i]) / 30;
			/* NB: delta <= 0 */
			/* Limit to [-4.5dB,0]. */
			cmd.gain[i] = MIN(abs(delta), 3);
			if (delta < 0)
				cmd.gain[i] |= 1 << 2;	/* sign bit */
		}
	}
	DPRINTF(sc, IWN_DEBUG_CALIBRATE,
	    "setting differential gains Ant A/B/C: %x/%x/%x (%x)\n",
	    cmd.gain[0], cmd.gain[1], cmd.gain[2], sc->chainmask);
	return iwn_cmd(sc, IWN_CMD_PHY_CALIB, &cmd, sizeof cmd, 1);
}

static int
iwn5000_set_gains(struct iwn_softc *sc)
{
	struct iwn_calib_state *calib = &sc->calib;
	struct iwn_phy_calib_gain cmd;
	int i, ant, div, delta;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	/* We collected 20 beacons and !=6050 need a 1.5 factor. */
	div = (sc->hw_type == IWN_HW_REV_TYPE_6050) ? 20 : 30;

	memset(&cmd, 0, sizeof cmd);
	cmd.code = sc->noise_gain;
	cmd.ngroups = 1;
	cmd.isvalid = 1;
	/* Get first available RX antenna as referential. */
	ant = IWN_LSB(sc->rxchainmask);
	/* Set differential gains for other antennas. */
	for (i = ant + 1; i < 3; i++) {
		if (sc->chainmask & (1 << i)) {
			/* The delta is relative to antenna "ant". */
			delta = ((int32_t)calib->noise[ant] -
			    (int32_t)calib->noise[i]) / div;
			/* Limit to [-4.5dB,+4.5dB]. */
			cmd.gain[i - 1] = MIN(abs(delta), 3);
			if (delta < 0)
				cmd.gain[i - 1] |= 1 << 2;	/* sign bit */
		}
	}
	DPRINTF(sc, IWN_DEBUG_CALIBRATE | IWN_DEBUG_XMIT,
	    "setting differential gains Ant B/C: %x/%x (%x)\n",
	    cmd.gain[0], cmd.gain[1], sc->chainmask);
	return iwn_cmd(sc, IWN_CMD_PHY_CALIB, &cmd, sizeof cmd, 1);
}

/*
 * Tune RF RX sensitivity based on the number of false alarms detected
 * during the last beacon period.
 */
static void
iwn_tune_sensitivity(struct iwn_softc *sc, const struct iwn_rx_stats *stats)
{
#define inc(val, inc, max)			\
	if ((val) < (max)) {			\
		if ((val) < (max) - (inc))	\
			(val) += (inc);		\
		else				\
			(val) = (max);		\
		needs_update = 1;		\
	}
#define dec(val, dec, min)			\
	if ((val) > (min)) {			\
		if ((val) > (min) + (dec))	\
			(val) -= (dec);		\
		else				\
			(val) = (min);		\
		needs_update = 1;		\
	}

	const struct iwn_sensitivity_limits *limits = sc->limits;
	struct iwn_calib_state *calib = &sc->calib;
	uint32_t val, rxena, fa;
	uint32_t energy[3], energy_min;
	uint8_t noise[3], noise_ref;
	int i, needs_update = 0;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	/* Check that we've been enabled long enough. */
	if ((rxena = le32toh(stats->general.load)) == 0){
		DPRINTF(sc, IWN_DEBUG_TRACE, "->%s end not so long\n", __func__);
		return;
	}

	/* Compute number of false alarms since last call for OFDM. */
	fa  = le32toh(stats->ofdm.bad_plcp) - calib->bad_plcp_ofdm;
	fa += le32toh(stats->ofdm.fa) - calib->fa_ofdm;
	fa *= 200 * IEEE80211_DUR_TU;	/* 200TU */

	if (fa > 50 * rxena) {
		/* High false alarm count, decrease sensitivity. */
		DPRINTF(sc, IWN_DEBUG_CALIBRATE,
		    "%s: OFDM high false alarm count: %u\n", __func__, fa);
		inc(calib->ofdm_x1,     1, limits->max_ofdm_x1);
		inc(calib->ofdm_mrc_x1, 1, limits->max_ofdm_mrc_x1);
		inc(calib->ofdm_x4,     1, limits->max_ofdm_x4);
		inc(calib->ofdm_mrc_x4, 1, limits->max_ofdm_mrc_x4);

	} else if (fa < 5 * rxena) {
		/* Low false alarm count, increase sensitivity. */
		DPRINTF(sc, IWN_DEBUG_CALIBRATE,
		    "%s: OFDM low false alarm count: %u\n", __func__, fa);
		dec(calib->ofdm_x1,     1, limits->min_ofdm_x1);
		dec(calib->ofdm_mrc_x1, 1, limits->min_ofdm_mrc_x1);
		dec(calib->ofdm_x4,     1, limits->min_ofdm_x4);
		dec(calib->ofdm_mrc_x4, 1, limits->min_ofdm_mrc_x4);
	}

	/* Compute maximum noise among 3 receivers. */
	for (i = 0; i < 3; i++)
		noise[i] = (le32toh(stats->general.noise[i]) >> 8) & 0xff;
	val = MAX(noise[0], noise[1]);
	val = MAX(noise[2], val);
	/* Insert it into our samples table. */
	calib->noise_samples[calib->cur_noise_sample] = val;
	calib->cur_noise_sample = (calib->cur_noise_sample + 1) % 20;

	/* Compute maximum noise among last 20 samples. */
	noise_ref = calib->noise_samples[0];
	for (i = 1; i < 20; i++)
		noise_ref = MAX(noise_ref, calib->noise_samples[i]);

	/* Compute maximum energy among 3 receivers. */
	for (i = 0; i < 3; i++)
		energy[i] = le32toh(stats->general.energy[i]);
	val = MIN(energy[0], energy[1]);
	val = MIN(energy[2], val);
	/* Insert it into our samples table. */
	calib->energy_samples[calib->cur_energy_sample] = val;
	calib->cur_energy_sample = (calib->cur_energy_sample + 1) % 10;

	/* Compute minimum energy among last 10 samples. */
	energy_min = calib->energy_samples[0];
	for (i = 1; i < 10; i++)
		energy_min = MAX(energy_min, calib->energy_samples[i]);
	energy_min += 6;

	/* Compute number of false alarms since last call for CCK. */
	fa  = le32toh(stats->cck.bad_plcp) - calib->bad_plcp_cck;
	fa += le32toh(stats->cck.fa) - calib->fa_cck;
	fa *= 200 * IEEE80211_DUR_TU;	/* 200TU */

	if (fa > 50 * rxena) {
		/* High false alarm count, decrease sensitivity. */
		DPRINTF(sc, IWN_DEBUG_CALIBRATE,
		    "%s: CCK high false alarm count: %u\n", __func__, fa);
		calib->cck_state = IWN_CCK_STATE_HIFA;
		calib->low_fa = 0;

		if (calib->cck_x4 > 160) {
			calib->noise_ref = noise_ref;
			if (calib->energy_cck > 2)
				dec(calib->energy_cck, 2, energy_min);
		}
		if (calib->cck_x4 < 160) {
			calib->cck_x4 = 161;
			needs_update = 1;
		} else
			inc(calib->cck_x4, 3, limits->max_cck_x4);

		inc(calib->cck_mrc_x4, 3, limits->max_cck_mrc_x4);

	} else if (fa < 5 * rxena) {
		/* Low false alarm count, increase sensitivity. */
		DPRINTF(sc, IWN_DEBUG_CALIBRATE,
		    "%s: CCK low false alarm count: %u\n", __func__, fa);
		calib->cck_state = IWN_CCK_STATE_LOFA;
		calib->low_fa++;

		if (calib->cck_state != IWN_CCK_STATE_INIT &&
		    (((int32_t)calib->noise_ref - (int32_t)noise_ref) > 2 ||
		     calib->low_fa > 100)) {
			inc(calib->energy_cck, 2, limits->min_energy_cck);
			dec(calib->cck_x4,     3, limits->min_cck_x4);
			dec(calib->cck_mrc_x4, 3, limits->min_cck_mrc_x4);
		}
	} else {
		/* Not worth to increase or decrease sensitivity. */
		DPRINTF(sc, IWN_DEBUG_CALIBRATE,
		    "%s: CCK normal false alarm count: %u\n", __func__, fa);
		calib->low_fa = 0;
		calib->noise_ref = noise_ref;

		if (calib->cck_state == IWN_CCK_STATE_HIFA) {
			/* Previous interval had many false alarms. */
			dec(calib->energy_cck, 8, energy_min);
		}
		calib->cck_state = IWN_CCK_STATE_INIT;
	}

	if (needs_update)
		(void)iwn_send_sensitivity(sc);

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

#undef dec
#undef inc
}

static int
iwn_send_sensitivity(struct iwn_softc *sc)
{
	struct iwn_calib_state *calib = &sc->calib;
	struct iwn_enhanced_sensitivity_cmd cmd;
	int len;

	memset(&cmd, 0, sizeof cmd);
	len = sizeof (struct iwn_sensitivity_cmd);
	cmd.which = IWN_SENSITIVITY_WORKTBL;
	/* OFDM modulation. */
	cmd.corr_ofdm_x1       = htole16(calib->ofdm_x1);
	cmd.corr_ofdm_mrc_x1   = htole16(calib->ofdm_mrc_x1);
	cmd.corr_ofdm_x4       = htole16(calib->ofdm_x4);
	cmd.corr_ofdm_mrc_x4   = htole16(calib->ofdm_mrc_x4);
	cmd.energy_ofdm        = htole16(sc->limits->energy_ofdm);
	cmd.energy_ofdm_th     = htole16(62);
	/* CCK modulation. */
	cmd.corr_cck_x4        = htole16(calib->cck_x4);
	cmd.corr_cck_mrc_x4    = htole16(calib->cck_mrc_x4);
	cmd.energy_cck         = htole16(calib->energy_cck);
	/* Barker modulation: use default values. */
	cmd.corr_barker        = htole16(190);
	cmd.corr_barker_mrc    = htole16(sc->limits->barker_mrc);

	DPRINTF(sc, IWN_DEBUG_CALIBRATE,
	    "%s: set sensitivity %d/%d/%d/%d/%d/%d/%d\n", __func__,
	    calib->ofdm_x1, calib->ofdm_mrc_x1, calib->ofdm_x4,
	    calib->ofdm_mrc_x4, calib->cck_x4,
	    calib->cck_mrc_x4, calib->energy_cck);

	if (!(sc->sc_flags & IWN_FLAG_ENH_SENS))
		goto send;
	/* Enhanced sensitivity settings. */
	len = sizeof (struct iwn_enhanced_sensitivity_cmd);
	cmd.ofdm_det_slope_mrc = htole16(668);
	cmd.ofdm_det_icept_mrc = htole16(4);
	cmd.ofdm_det_slope     = htole16(486);
	cmd.ofdm_det_icept     = htole16(37);
	cmd.cck_det_slope_mrc  = htole16(853);
	cmd.cck_det_icept_mrc  = htole16(4);
	cmd.cck_det_slope      = htole16(476);
	cmd.cck_det_icept      = htole16(99);
send:
	return iwn_cmd(sc, IWN_CMD_SET_SENSITIVITY, &cmd, len, 1);
}

/*
 * Look at the increase of PLCP errors over time; if it exceeds
 * a programmed threshold then trigger an RF retune.
 */
static void
iwn_check_rx_recovery(struct iwn_softc *sc, struct iwn_stats *rs)
{
	int32_t delta_ofdm, delta_ht, delta_cck;
	struct iwn_calib_state *calib = &sc->calib;
	int delta_ticks, cur_ticks;
	int delta_msec;
	int thresh;

	/*
	 * Calculate the difference between the current and
	 * previous statistics.
	 */
	delta_cck = le32toh(rs->rx.cck.bad_plcp) - calib->bad_plcp_cck;
	delta_ofdm = le32toh(rs->rx.ofdm.bad_plcp) - calib->bad_plcp_ofdm;
	delta_ht = le32toh(rs->rx.ht.bad_plcp) - calib->bad_plcp_ht;

	/*
	 * Calculate the delta in time between successive statistics
	 * messages.  Yes, it can roll over; so we make sure that
	 * this doesn't happen.
	 *
	 * XXX go figure out what to do about rollover
	 * XXX go figure out what to do if ticks rolls over to -ve instead!
	 * XXX go stab signed integer overflow undefined-ness in the face.
	 */
	cur_ticks = ticks;
	delta_ticks = cur_ticks - sc->last_calib_ticks;

	/*
	 * If any are negative, then the firmware likely reset; so just
	 * bail.  We'll pick this up next time.
	 */
	if (delta_cck < 0 || delta_ofdm < 0 || delta_ht < 0 || delta_ticks < 0)
		return;

	/*
	 * delta_ticks is in ticks; we need to convert it up to milliseconds
	 * so we can do some useful math with it.
	 */
	delta_msec = ticks_to_msecs(delta_ticks);

	/*
	 * Calculate what our threshold is given the current delta_msec.
	 */
	thresh = sc->base_params->plcp_err_threshold * delta_msec;

	DPRINTF(sc, IWN_DEBUG_STATE,
	    "%s: time delta: %d; cck=%d, ofdm=%d, ht=%d, total=%d, thresh=%d\n",
	    __func__,
	    delta_msec,
	    delta_cck,
	    delta_ofdm,
	    delta_ht,
	    (delta_msec + delta_cck + delta_ofdm + delta_ht),
	    thresh);

	/*
	 * If we need a retune, then schedule a single channel scan
	 * to a channel that isn't the currently active one!
	 *
	 * The math from linux iwlwifi:
	 *
	 * if ((delta * 100 / msecs) > threshold)
	 */
	if (thresh > 0 && (delta_cck + delta_ofdm + delta_ht) * 100 > thresh) {
		DPRINTF(sc, IWN_DEBUG_ANY,
		    "%s: PLCP error threshold raw (%d) comparison (%d) "
		    "over limit (%d); retune!\n",
		    __func__,
		    (delta_cck + delta_ofdm + delta_ht),
		    (delta_cck + delta_ofdm + delta_ht) * 100,
		    thresh);
	}
}

/*
 * Set STA mode power saving level (between 0 and 5).
 * Level 0 is CAM (Continuously Aware Mode), 5 is for maximum power saving.
 */
static int
iwn_set_pslevel(struct iwn_softc *sc, int dtim, int level, int async)
{
	struct iwn_pmgt_cmd cmd;
	const struct iwn_pmgt *pmgt;
	uint32_t max, skip_dtim;
	uint32_t reg;
	int i;

	DPRINTF(sc, IWN_DEBUG_PWRSAVE,
	    "%s: dtim=%d, level=%d, async=%d\n",
	    __func__,
	    dtim,
	    level,
	    async);

	/* Select which PS parameters to use. */
	if (dtim <= 2)
		pmgt = &iwn_pmgt[0][level];
	else if (dtim <= 10)
		pmgt = &iwn_pmgt[1][level];
	else
		pmgt = &iwn_pmgt[2][level];

	memset(&cmd, 0, sizeof cmd);
	if (level != 0)	/* not CAM */
		cmd.flags |= htole16(IWN_PS_ALLOW_SLEEP);
	if (level == 5)
		cmd.flags |= htole16(IWN_PS_FAST_PD);
	/* Retrieve PCIe Active State Power Management (ASPM). */
	reg = pci_read_config(sc->sc_dev, sc->sc_cap_off + PCIER_LINK_CTL, 4);
	if (!(reg & PCIEM_LINK_CTL_ASPMC_L0S))	/* L0s Entry disabled. */
		cmd.flags |= htole16(IWN_PS_PCI_PMGT);
	cmd.rxtimeout = htole32(pmgt->rxtimeout * 1024);
	cmd.txtimeout = htole32(pmgt->txtimeout * 1024);

	if (dtim == 0) {
		dtim = 1;
		skip_dtim = 0;
	} else
		skip_dtim = pmgt->skip_dtim;
	if (skip_dtim != 0) {
		cmd.flags |= htole16(IWN_PS_SLEEP_OVER_DTIM);
		max = pmgt->intval[4];
		if (max == (uint32_t)-1)
			max = dtim * (skip_dtim + 1);
		else if (max > dtim)
			max = rounddown(max, dtim);
	} else
		max = dtim;
	for (i = 0; i < 5; i++)
		cmd.intval[i] = htole32(MIN(max, pmgt->intval[i]));

	DPRINTF(sc, IWN_DEBUG_RESET, "setting power saving level to %d\n",
	    level);
	return iwn_cmd(sc, IWN_CMD_SET_POWER_MODE, &cmd, sizeof cmd, async);
}

static int
iwn_send_btcoex(struct iwn_softc *sc)
{
	struct iwn_bluetooth cmd;

	memset(&cmd, 0, sizeof cmd);
	cmd.flags = IWN_BT_COEX_CHAN_ANN | IWN_BT_COEX_BT_PRIO;
	cmd.lead_time = IWN_BT_LEAD_TIME_DEF;
	cmd.max_kill = IWN_BT_MAX_KILL_DEF;
	DPRINTF(sc, IWN_DEBUG_RESET, "%s: configuring bluetooth coexistence\n",
	    __func__);
	return iwn_cmd(sc, IWN_CMD_BT_COEX, &cmd, sizeof(cmd), 0);
}

static int
iwn_send_advanced_btcoex(struct iwn_softc *sc)
{
	static const uint32_t btcoex_3wire[12] = {
		0xaaaaaaaa, 0xaaaaaaaa, 0xaeaaaaaa, 0xaaaaaaaa,
		0xcc00ff28, 0x0000aaaa, 0xcc00aaaa, 0x0000aaaa,
		0xc0004000, 0x00004000, 0xf0005000, 0xf0005000,
	};
	struct iwn6000_btcoex_config btconfig;
	struct iwn2000_btcoex_config btconfig2k;
	struct iwn_btcoex_priotable btprio;
	struct iwn_btcoex_prot btprot;
	int error, i;
	uint8_t flags;

	memset(&btconfig, 0, sizeof btconfig);
	memset(&btconfig2k, 0, sizeof btconfig2k);

	flags = IWN_BT_FLAG_COEX6000_MODE_3W <<
	    IWN_BT_FLAG_COEX6000_MODE_SHIFT; // Done as is in linux kernel 3.2

	if (sc->base_params->bt_sco_disable)
		flags &= ~IWN_BT_FLAG_SYNC_2_BT_DISABLE;
	else
		flags |= IWN_BT_FLAG_SYNC_2_BT_DISABLE;

	flags |= IWN_BT_FLAG_COEX6000_CHAN_INHIBITION;

	/* Default flags result is 145 as old value */

	/*
	 * Flags value has to be review. Values must change if we
	 * which to disable it
	 */
	if (sc->base_params->bt_session_2) {
		btconfig2k.flags = flags;
		btconfig2k.max_kill = 5;
		btconfig2k.bt3_t7_timer = 1;
		btconfig2k.kill_ack = htole32(0xffff0000);
		btconfig2k.kill_cts = htole32(0xffff0000);
		btconfig2k.sample_time = 2;
		btconfig2k.bt3_t2_timer = 0xc;

		for (i = 0; i < 12; i++)
			btconfig2k.lookup_table[i] = htole32(btcoex_3wire[i]);
		btconfig2k.valid = htole16(0xff);
		btconfig2k.prio_boost = htole32(0xf0);
		DPRINTF(sc, IWN_DEBUG_RESET,
		    "%s: configuring advanced bluetooth coexistence"
		    " session 2, flags : 0x%x\n",
		    __func__,
		    flags);
		error = iwn_cmd(sc, IWN_CMD_BT_COEX, &btconfig2k,
		    sizeof(btconfig2k), 1);
	} else {
		btconfig.flags = flags;
		btconfig.max_kill = 5;
		btconfig.bt3_t7_timer = 1;
		btconfig.kill_ack = htole32(0xffff0000);
		btconfig.kill_cts = htole32(0xffff0000);
		btconfig.sample_time = 2;
		btconfig.bt3_t2_timer = 0xc;

		for (i = 0; i < 12; i++)
			btconfig.lookup_table[i] = htole32(btcoex_3wire[i]);
		btconfig.valid = htole16(0xff);
		btconfig.prio_boost = 0xf0;
		DPRINTF(sc, IWN_DEBUG_RESET,
		    "%s: configuring advanced bluetooth coexistence,"
		    " flags : 0x%x\n",
		    __func__,
		    flags);
		error = iwn_cmd(sc, IWN_CMD_BT_COEX, &btconfig,
		    sizeof(btconfig), 1);
	}

	if (error != 0)
		return error;

	memset(&btprio, 0, sizeof btprio);
	btprio.calib_init1 = 0x6;
	btprio.calib_init2 = 0x7;
	btprio.calib_periodic_low1 = 0x2;
	btprio.calib_periodic_low2 = 0x3;
	btprio.calib_periodic_high1 = 0x4;
	btprio.calib_periodic_high2 = 0x5;
	btprio.dtim = 0x6;
	btprio.scan52 = 0x8;
	btprio.scan24 = 0xa;
	error = iwn_cmd(sc, IWN_CMD_BT_COEX_PRIOTABLE, &btprio, sizeof(btprio),
	    1);
	if (error != 0)
		return error;

	/* Force BT state machine change. */
	memset(&btprot, 0, sizeof btprot);
	btprot.open = 1;
	btprot.type = 1;
	error = iwn_cmd(sc, IWN_CMD_BT_COEX_PROT, &btprot, sizeof(btprot), 1);
	if (error != 0)
		return error;
	btprot.open = 0;
	return iwn_cmd(sc, IWN_CMD_BT_COEX_PROT, &btprot, sizeof(btprot), 1);
}

static int
iwn5000_runtime_calib(struct iwn_softc *sc)
{
	struct iwn5000_calib_config cmd;

	memset(&cmd, 0, sizeof cmd);
	cmd.ucode.once.enable = 0xffffffff;
	cmd.ucode.once.start = IWN5000_CALIB_DC;
	DPRINTF(sc, IWN_DEBUG_CALIBRATE,
	    "%s: configuring runtime calibration\n", __func__);
	return iwn_cmd(sc, IWN5000_CMD_CALIB_CONFIG, &cmd, sizeof(cmd), 0);
}

static uint32_t
iwn_get_rxon_ht_flags(struct iwn_softc *sc, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t htflags = 0;

	if (! IEEE80211_IS_CHAN_HT(c))
		return (0);

	htflags |= IWN_RXON_HT_PROTMODE(ic->ic_curhtprotmode);

	if (IEEE80211_IS_CHAN_HT40(c)) {
		switch (ic->ic_curhtprotmode) {
		case IEEE80211_HTINFO_OPMODE_HT20PR:
			htflags |= IWN_RXON_HT_MODEPURE40;
			break;
		default:
			htflags |= IWN_RXON_HT_MODEMIXED;
			break;
		}
	}
	if (IEEE80211_IS_CHAN_HT40D(c))
		htflags |= IWN_RXON_HT_HT40MINUS;

	return (htflags);
}

static int
iwn_check_bss_filter(struct iwn_softc *sc)
{
	return ((sc->rxon->filter & htole32(IWN_FILTER_BSS)) != 0);
}

static int
iwn4965_rxon_assoc(struct iwn_softc *sc, int async)
{
	struct iwn4965_rxon_assoc cmd;
	struct iwn_rxon *rxon = sc->rxon;

	cmd.flags = rxon->flags;
	cmd.filter = rxon->filter;
	cmd.ofdm_mask = rxon->ofdm_mask;
	cmd.cck_mask = rxon->cck_mask;
	cmd.ht_single_mask = rxon->ht_single_mask;
	cmd.ht_dual_mask = rxon->ht_dual_mask;
	cmd.rxchain = rxon->rxchain;
	cmd.reserved = 0;

	return (iwn_cmd(sc, IWN_CMD_RXON_ASSOC, &cmd, sizeof(cmd), async));
}

static int
iwn5000_rxon_assoc(struct iwn_softc *sc, int async)
{
	struct iwn5000_rxon_assoc cmd;
	struct iwn_rxon *rxon = sc->rxon;

	cmd.flags = rxon->flags;
	cmd.filter = rxon->filter;
	cmd.ofdm_mask = rxon->ofdm_mask;
	cmd.cck_mask = rxon->cck_mask;
	cmd.reserved1 = 0;
	cmd.ht_single_mask = rxon->ht_single_mask;
	cmd.ht_dual_mask = rxon->ht_dual_mask;
	cmd.ht_triple_mask = rxon->ht_triple_mask;
	cmd.reserved2 = 0;
	cmd.rxchain = rxon->rxchain;
	cmd.acquisition = rxon->acquisition;
	cmd.reserved3 = 0;

	return (iwn_cmd(sc, IWN_CMD_RXON_ASSOC, &cmd, sizeof(cmd), async));
}

static int
iwn_send_rxon(struct iwn_softc *sc, int assoc, int async)
{
	struct iwn_ops *ops = &sc->ops;
	int error;

	IWN_LOCK_ASSERT(sc);

	if (assoc && iwn_check_bss_filter(sc) != 0) {
		error = ops->rxon_assoc(sc, async);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: RXON_ASSOC command failed, error %d\n",
			    __func__, error);
			return (error);
		}
	} else {
		if (sc->sc_is_scanning)
			device_printf(sc->sc_dev,
			    "%s: is_scanning set, before RXON\n",
			    __func__);

		error = iwn_cmd(sc, IWN_CMD_RXON, sc->rxon, sc->rxonsz, async);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: RXON command failed, error %d\n",
			    __func__, error);
			return (error);
		}

		/*
		 * Reconfiguring RXON clears the firmware nodes table so
		 * we must add the broadcast node again.
		 */
		if (iwn_check_bss_filter(sc) == 0 &&
		    (error = iwn_add_broadcast_node(sc, async)) != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not add broadcast node, error %d\n",
			    __func__, error);
			return (error);
		}
	}

	/* Configuration has changed, set TX power accordingly. */
	if ((error = ops->set_txpower(sc, async)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not set TX power, error %d\n",
		    __func__, error);
		return (error);
	}

	return (0);
}

static int
iwn_config(struct iwn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
	const uint8_t *macaddr;
	uint32_t txmask;
	uint16_t rxchain;
	int error;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	if ((sc->base_params->calib_need & IWN_FLG_NEED_PHY_CALIB_TEMP_OFFSET)
	    && (sc->base_params->calib_need & IWN_FLG_NEED_PHY_CALIB_TEMP_OFFSETv2)) {
		device_printf(sc->sc_dev,"%s: temp_offset and temp_offsetv2 are"
		    " exclusive each together. Review NIC config file. Conf"
		    " :  0x%08x Flags :  0x%08x  \n", __func__,
		    sc->base_params->calib_need,
		    (IWN_FLG_NEED_PHY_CALIB_TEMP_OFFSET |
		    IWN_FLG_NEED_PHY_CALIB_TEMP_OFFSETv2));
		return (EINVAL);
	}

	/* Compute temperature calib if needed. Will be send by send calib */
	if (sc->base_params->calib_need & IWN_FLG_NEED_PHY_CALIB_TEMP_OFFSET) {
		error = iwn5000_temp_offset_calib(sc);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not set temperature offset\n", __func__);
			return (error);
		}
	} else if (sc->base_params->calib_need & IWN_FLG_NEED_PHY_CALIB_TEMP_OFFSETv2) {
		error = iwn5000_temp_offset_calibv2(sc);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not compute temperature offset v2\n",
			    __func__);
			return (error);
		}
	}

	if (sc->hw_type == IWN_HW_REV_TYPE_6050) {
		/* Configure runtime DC calibration. */
		error = iwn5000_runtime_calib(sc);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not configure runtime calibration\n",
			    __func__);
			return error;
		}
	}

	/* Configure valid TX chains for >=5000 Series. */
	if (sc->hw_type != IWN_HW_REV_TYPE_4965 &&
	    IWN_UCODE_API(sc->ucode_rev) > 1) {
		txmask = htole32(sc->txchainmask);
		DPRINTF(sc, IWN_DEBUG_RESET | IWN_DEBUG_XMIT,
		    "%s: configuring valid TX chains 0x%x\n", __func__, txmask);
		error = iwn_cmd(sc, IWN5000_CMD_TX_ANT_CONFIG, &txmask,
		    sizeof txmask, 0);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not configure valid TX chains, "
			    "error %d\n", __func__, error);
			return error;
		}
	}

	/* Configure bluetooth coexistence. */
	error = 0;

	/* Configure bluetooth coexistence if needed. */
	if (sc->base_params->bt_mode == IWN_BT_ADVANCED)
		error = iwn_send_advanced_btcoex(sc);
	if (sc->base_params->bt_mode == IWN_BT_SIMPLE)
		error = iwn_send_btcoex(sc);

	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not configure bluetooth coexistence, error %d\n",
		    __func__, error);
		return error;
	}

	/* Set mode, channel, RX filter and enable RX. */
	sc->rxon = &sc->rx_on[IWN_RXON_BSS_CTX];
	memset(sc->rxon, 0, sizeof (struct iwn_rxon));
	macaddr = vap ? vap->iv_myaddr : ic->ic_macaddr;
	IEEE80211_ADDR_COPY(sc->rxon->myaddr, macaddr);
	IEEE80211_ADDR_COPY(sc->rxon->wlap, macaddr);
	sc->rxon->chan = ieee80211_chan2ieee(ic, ic->ic_curchan);
	sc->rxon->flags = htole32(IWN_RXON_TSF | IWN_RXON_CTS_TO_SELF);
	if (IEEE80211_IS_CHAN_2GHZ(ic->ic_curchan))
		sc->rxon->flags |= htole32(IWN_RXON_AUTO | IWN_RXON_24GHZ);

	sc->rxon->filter = htole32(IWN_FILTER_MULTICAST);
	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		sc->rxon->mode = IWN_MODE_STA;
		break;
	case IEEE80211_M_MONITOR:
		sc->rxon->mode = IWN_MODE_MONITOR;
		break;
	default:
		/* Should not get there. */
		break;
	}
	iwn_set_promisc(sc);
	sc->rxon->cck_mask  = 0x0f;	/* not yet negotiated */
	sc->rxon->ofdm_mask = 0xff;	/* not yet negotiated */
	sc->rxon->ht_single_mask = 0xff;
	sc->rxon->ht_dual_mask = 0xff;
	sc->rxon->ht_triple_mask = 0xff;
	/*
	 * In active association mode, ensure that
	 * all the receive chains are enabled.
	 *
	 * Since we're not yet doing SMPS, don't allow the
	 * number of idle RX chains to be less than the active
	 * number.
	 */
	rxchain =
	    IWN_RXCHAIN_VALID(sc->rxchainmask) |
	    IWN_RXCHAIN_MIMO_COUNT(sc->nrxchains) |
	    IWN_RXCHAIN_IDLE_COUNT(sc->nrxchains);
	sc->rxon->rxchain = htole16(rxchain);
	DPRINTF(sc, IWN_DEBUG_RESET | IWN_DEBUG_XMIT,
	    "%s: rxchainmask=0x%x, nrxchains=%d\n",
	    __func__,
	    sc->rxchainmask,
	    sc->nrxchains);

	sc->rxon->flags |= htole32(iwn_get_rxon_ht_flags(sc, ic->ic_curchan));

	DPRINTF(sc, IWN_DEBUG_RESET,
	    "%s: setting configuration; flags=0x%08x\n",
	    __func__, le32toh(sc->rxon->flags));
	if ((error = iwn_send_rxon(sc, 0, 0)) != 0) {
		device_printf(sc->sc_dev, "%s: could not send RXON\n",
		    __func__);
		return error;
	}

	if ((error = iwn_set_critical_temp(sc)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not set critical temperature\n", __func__);
		return error;
	}

	/* Set power saving level to CAM during initialization. */
	if ((error = iwn_set_pslevel(sc, 0, 0, 0)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not set power saving level\n", __func__);
		return error;
	}

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

	return 0;
}

static uint16_t
iwn_get_active_dwell_time(struct iwn_softc *sc,
    struct ieee80211_channel *c, uint8_t n_probes)
{
	/* No channel? Default to 2GHz settings */
	if (c == NULL || IEEE80211_IS_CHAN_2GHZ(c)) {
		return (IWN_ACTIVE_DWELL_TIME_2GHZ +
		IWN_ACTIVE_DWELL_FACTOR_2GHZ * (n_probes + 1));
	}

	/* 5GHz dwell time */
	return (IWN_ACTIVE_DWELL_TIME_5GHZ +
	    IWN_ACTIVE_DWELL_FACTOR_5GHZ * (n_probes + 1));
}

/*
 * Limit the total dwell time to 85% of the beacon interval.
 *
 * Returns the dwell time in milliseconds.
 */
static uint16_t
iwn_limit_dwell(struct iwn_softc *sc, uint16_t dwell_time)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = NULL;
	int bintval = 0;

	/* bintval is in TU (1.024mS) */
	if (! TAILQ_EMPTY(&ic->ic_vaps)) {
		vap = TAILQ_FIRST(&ic->ic_vaps);
		bintval = vap->iv_bss->ni_intval;
	}

	/*
	 * If it's non-zero, we should calculate the minimum of
	 * it and the DWELL_BASE.
	 *
	 * XXX Yes, the math should take into account that bintval
	 * is 1.024mS, not 1mS..
	 */
	if (bintval > 0) {
		DPRINTF(sc, IWN_DEBUG_SCAN,
		    "%s: bintval=%d\n",
		    __func__,
		    bintval);
		return (MIN(IWN_PASSIVE_DWELL_BASE, ((bintval * 85) / 100)));
	}

	/* No association context? Default */
	return (IWN_PASSIVE_DWELL_BASE);
}

static uint16_t
iwn_get_passive_dwell_time(struct iwn_softc *sc, struct ieee80211_channel *c)
{
	uint16_t passive;

	if (c == NULL || IEEE80211_IS_CHAN_2GHZ(c)) {
		passive = IWN_PASSIVE_DWELL_BASE + IWN_PASSIVE_DWELL_TIME_2GHZ;
	} else {
		passive = IWN_PASSIVE_DWELL_BASE + IWN_PASSIVE_DWELL_TIME_5GHZ;
	}

	/* Clamp to the beacon interval if we're associated */
	return (iwn_limit_dwell(sc, passive));
}

static int
iwn_scan(struct iwn_softc *sc, struct ieee80211vap *vap,
    struct ieee80211_scan_state *ss, struct ieee80211_channel *c)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = vap->iv_bss;
	struct iwn_scan_hdr *hdr;
	struct iwn_cmd_data *tx;
	struct iwn_scan_essid *essid;
	struct iwn_scan_chan *chan;
	struct ieee80211_frame *wh;
	struct ieee80211_rateset *rs;
	uint8_t *buf, *frm;
	uint16_t rxchain;
	uint8_t txant;
	int buflen, error;
	int is_active;
	uint16_t dwell_active, dwell_passive;
	uint32_t extra, scan_service_time;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	/*
	 * We are absolutely not allowed to send a scan command when another
	 * scan command is pending.
	 */
	if (sc->sc_is_scanning) {
		device_printf(sc->sc_dev, "%s: called whilst scanning!\n",
		    __func__);
		return (EAGAIN);
	}

	/* Assign the scan channel */
	c = ic->ic_curchan;

	sc->rxon = &sc->rx_on[IWN_RXON_BSS_CTX];
	buf = malloc(IWN_SCAN_MAXSZ, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (buf == NULL) {
		device_printf(sc->sc_dev,
		    "%s: could not allocate buffer for scan command\n",
		    __func__);
		return ENOMEM;
	}
	hdr = (struct iwn_scan_hdr *)buf;
	/*
	 * Move to the next channel if no frames are received within 10ms
	 * after sending the probe request.
	 */
	hdr->quiet_time = htole16(10);		/* timeout in milliseconds */
	hdr->quiet_threshold = htole16(1);	/* min # of packets */
	/*
	 * Max needs to be greater than active and passive and quiet!
	 * It's also in microseconds!
	 */
	hdr->max_svc = htole32(250 * 1024);

	/*
	 * Reset scan: interval=100
	 * Normal scan: interval=becaon interval
	 * suspend_time: 100 (TU)
	 *
	 */
	extra = (100 /* suspend_time */ / 100 /* beacon interval */) << 22;
	//scan_service_time = extra | ((100 /* susp */ % 100 /* int */) * 1024);
	scan_service_time = (4 << 22) | (100 * 1024);	/* Hardcode for now! */
	hdr->pause_svc = htole32(scan_service_time);

	/* Select antennas for scanning. */
	rxchain =
	    IWN_RXCHAIN_VALID(sc->rxchainmask) |
	    IWN_RXCHAIN_FORCE_MIMO_SEL(sc->rxchainmask) |
	    IWN_RXCHAIN_DRIVER_FORCE;
	if (IEEE80211_IS_CHAN_A(c) &&
	    sc->hw_type == IWN_HW_REV_TYPE_4965) {
		/* Ant A must be avoided in 5GHz because of an HW bug. */
		rxchain |= IWN_RXCHAIN_FORCE_SEL(IWN_ANT_B);
	} else	/* Use all available RX antennas. */
		rxchain |= IWN_RXCHAIN_FORCE_SEL(sc->rxchainmask);
	hdr->rxchain = htole16(rxchain);
	hdr->filter = htole32(IWN_FILTER_MULTICAST | IWN_FILTER_BEACON);

	tx = (struct iwn_cmd_data *)(hdr + 1);
	tx->flags = htole32(IWN_TX_AUTO_SEQ);
	tx->id = sc->broadcast_id;
	tx->lifetime = htole32(IWN_LIFETIME_INFINITE);

	if (IEEE80211_IS_CHAN_5GHZ(c)) {
		/* Send probe requests at 6Mbps. */
		tx->rate = htole32(0xd);
		rs = &ic->ic_sup_rates[IEEE80211_MODE_11A];
	} else {
		hdr->flags = htole32(IWN_RXON_24GHZ | IWN_RXON_AUTO);
		if (sc->hw_type == IWN_HW_REV_TYPE_4965 &&
		    sc->rxon->associd && sc->rxon->chan > 14)
			tx->rate = htole32(0xd);
		else {
			/* Send probe requests at 1Mbps. */
			tx->rate = htole32(10 | IWN_RFLAG_CCK);
		}
		rs = &ic->ic_sup_rates[IEEE80211_MODE_11G];
	}
	/* Use the first valid TX antenna. */
	txant = IWN_LSB(sc->txchainmask);
	tx->rate |= htole32(IWN_RFLAG_ANT(txant));

	/*
	 * Only do active scanning if we're announcing a probe request
	 * for a given SSID (or more, if we ever add it to the driver.)
	 */
	is_active = 0;

	/*
	 * If we're scanning for a specific SSID, add it to the command.
	 *
	 * XXX maybe look at adding support for scanning multiple SSIDs?
	 */
	essid = (struct iwn_scan_essid *)(tx + 1);
	if (ss != NULL) {
		if (ss->ss_ssid[0].len != 0) {
			essid[0].id = IEEE80211_ELEMID_SSID;
			essid[0].len = ss->ss_ssid[0].len;
			memcpy(essid[0].data, ss->ss_ssid[0].ssid, ss->ss_ssid[0].len);
		}

		DPRINTF(sc, IWN_DEBUG_SCAN, "%s: ssid_len=%d, ssid=%*s\n",
		    __func__,
		    ss->ss_ssid[0].len,
		    ss->ss_ssid[0].len,
		    ss->ss_ssid[0].ssid);

		if (ss->ss_nssid > 0)
			is_active = 1;
	}

	/*
	 * Build a probe request frame.  Most of the following code is a
	 * copy & paste of what is done in net80211.
	 */
	wh = (struct ieee80211_frame *)(essid + 20);
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT |
	    IEEE80211_FC0_SUBTYPE_PROBE_REQ;
	wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	IEEE80211_ADDR_COPY(wh->i_addr1, vap->iv_ifp->if_broadcastaddr);
	IEEE80211_ADDR_COPY(wh->i_addr2, IF_LLADDR(vap->iv_ifp));
	IEEE80211_ADDR_COPY(wh->i_addr3, vap->iv_ifp->if_broadcastaddr);
	*(uint16_t *)&wh->i_dur[0] = 0;	/* filled by HW */
	*(uint16_t *)&wh->i_seq[0] = 0;	/* filled by HW */

	frm = (uint8_t *)(wh + 1);
	frm = ieee80211_add_ssid(frm, NULL, 0);
	frm = ieee80211_add_rates(frm, rs);
	if (rs->rs_nrates > IEEE80211_RATE_SIZE)
		frm = ieee80211_add_xrates(frm, rs);
	if (ic->ic_htcaps & IEEE80211_HTC_HT)
		frm = ieee80211_add_htcap(frm, ni);

	/* Set length of probe request. */
	tx->len = htole16(frm - (uint8_t *)wh);

	/*
	 * If active scanning is requested but a certain channel is
	 * marked passive, we can do active scanning if we detect
	 * transmissions.
	 *
	 * There is an issue with some firmware versions that triggers
	 * a sysassert on a "good CRC threshold" of zero (== disabled),
	 * on a radar channel even though this means that we should NOT
	 * send probes.
	 *
	 * The "good CRC threshold" is the number of frames that we
	 * need to receive during our dwell time on a channel before
	 * sending out probes -- setting this to a huge value will
	 * mean we never reach it, but at the same time work around
	 * the aforementioned issue. Thus use IWL_GOOD_CRC_TH_NEVER
	 * here instead of IWL_GOOD_CRC_TH_DISABLED.
	 *
	 * This was fixed in later versions along with some other
	 * scan changes, and the threshold behaves as a flag in those
	 * versions.
	 */

	/*
	 * If we're doing active scanning, set the crc_threshold
	 * to a suitable value.  This is different to active veruss
	 * passive scanning depending upon the channel flags; the
	 * firmware will obey that particular check for us.
	 */
	if (sc->tlv_feature_flags & IWN_UCODE_TLV_FLAGS_NEWSCAN)
		hdr->crc_threshold = is_active ?
		    IWN_GOOD_CRC_TH_DEFAULT : IWN_GOOD_CRC_TH_DISABLED;
	else
		hdr->crc_threshold = is_active ?
		    IWN_GOOD_CRC_TH_DEFAULT : IWN_GOOD_CRC_TH_NEVER;

	chan = (struct iwn_scan_chan *)frm;
	chan->chan = htole16(ieee80211_chan2ieee(ic, c));
	chan->flags = 0;
	if (ss->ss_nssid > 0)
		chan->flags |= htole32(IWN_CHAN_NPBREQS(1));
	chan->dsp_gain = 0x6e;

	/*
	 * Set the passive/active flag depending upon the channel mode.
	 * XXX TODO: take the is_active flag into account as well?
	 */
	if (c->ic_flags & IEEE80211_CHAN_PASSIVE)
		chan->flags |= htole32(IWN_CHAN_PASSIVE);
	else
		chan->flags |= htole32(IWN_CHAN_ACTIVE);

	/*
	 * Calculate the active/passive dwell times.
	 */

	dwell_active = iwn_get_active_dwell_time(sc, c, ss->ss_nssid);
	dwell_passive = iwn_get_passive_dwell_time(sc, c);

	/* Make sure they're valid */
	if (dwell_passive <= dwell_active)
		dwell_passive = dwell_active + 1;

	chan->active = htole16(dwell_active);
	chan->passive = htole16(dwell_passive);

	if (IEEE80211_IS_CHAN_5GHZ(c))
		chan->rf_gain = 0x3b;
	else
		chan->rf_gain = 0x28;

	DPRINTF(sc, IWN_DEBUG_STATE,
	    "%s: chan %u flags 0x%x rf_gain 0x%x "
	    "dsp_gain 0x%x active %d passive %d scan_svc_time %d crc 0x%x "
	    "isactive=%d numssid=%d\n", __func__,
	    chan->chan, chan->flags, chan->rf_gain, chan->dsp_gain,
	    dwell_active, dwell_passive, scan_service_time,
	    hdr->crc_threshold, is_active, ss->ss_nssid);

	hdr->nchan++;
	chan++;
	buflen = (uint8_t *)chan - buf;
	hdr->len = htole16(buflen);

	if (sc->sc_is_scanning) {
		device_printf(sc->sc_dev,
		    "%s: called with is_scanning set!\n",
		    __func__);
	}
	sc->sc_is_scanning = 1;

	DPRINTF(sc, IWN_DEBUG_STATE, "sending scan command nchan=%d\n",
	    hdr->nchan);
	error = iwn_cmd(sc, IWN_CMD_SCAN, buf, buflen, 1);
	free(buf, M_DEVBUF);
	if (error == 0)
		callout_reset(&sc->scan_timeout, 5*hz, iwn_scan_timeout, sc);

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

	return error;
}

static int
iwn_auth(struct iwn_softc *sc, struct ieee80211vap *vap)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = vap->iv_bss;
	int error;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	sc->rxon = &sc->rx_on[IWN_RXON_BSS_CTX];
	/* Update adapter configuration. */
	IEEE80211_ADDR_COPY(sc->rxon->bssid, ni->ni_bssid);
	sc->rxon->chan = ieee80211_chan2ieee(ic, ni->ni_chan);
	sc->rxon->flags = htole32(IWN_RXON_TSF | IWN_RXON_CTS_TO_SELF);
	if (IEEE80211_IS_CHAN_2GHZ(ni->ni_chan))
		sc->rxon->flags |= htole32(IWN_RXON_AUTO | IWN_RXON_24GHZ);
	if (ic->ic_flags & IEEE80211_F_SHSLOT)
		sc->rxon->flags |= htole32(IWN_RXON_SHSLOT);
	if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
		sc->rxon->flags |= htole32(IWN_RXON_SHPREAMBLE);
	if (IEEE80211_IS_CHAN_A(ni->ni_chan)) {
		sc->rxon->cck_mask  = 0;
		sc->rxon->ofdm_mask = 0x15;
	} else if (IEEE80211_IS_CHAN_B(ni->ni_chan)) {
		sc->rxon->cck_mask  = 0x03;
		sc->rxon->ofdm_mask = 0;
	} else {
		/* Assume 802.11b/g. */
		sc->rxon->cck_mask  = 0x03;
		sc->rxon->ofdm_mask = 0x15;
	}

	/* try HT */
	sc->rxon->flags |= htole32(iwn_get_rxon_ht_flags(sc, ic->ic_curchan));

	DPRINTF(sc, IWN_DEBUG_STATE, "rxon chan %d flags %x cck %x ofdm %x\n",
	    sc->rxon->chan, sc->rxon->flags, sc->rxon->cck_mask,
	    sc->rxon->ofdm_mask);

	if ((error = iwn_send_rxon(sc, 0, 1)) != 0) {
		device_printf(sc->sc_dev, "%s: could not send RXON\n",
		    __func__);
		return (error);
	}

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

	return (0);
}

static int
iwn_run(struct iwn_softc *sc, struct ieee80211vap *vap)
{
	struct iwn_ops *ops = &sc->ops;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = vap->iv_bss;
	struct iwn_node_info node;
	int error;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	sc->rxon = &sc->rx_on[IWN_RXON_BSS_CTX];
	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		/* Link LED blinks while monitoring. */
		iwn_set_led(sc, IWN_LED_LINK, 5, 5);
		return 0;
	}
	if ((error = iwn_set_timing(sc, ni)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not set timing, error %d\n", __func__, error);
		return error;
	}

	/* Update adapter configuration. */
	IEEE80211_ADDR_COPY(sc->rxon->bssid, ni->ni_bssid);
	sc->rxon->associd = htole16(IEEE80211_AID(ni->ni_associd));
	sc->rxon->chan = ieee80211_chan2ieee(ic, ni->ni_chan);
	sc->rxon->flags = htole32(IWN_RXON_TSF | IWN_RXON_CTS_TO_SELF);
	if (IEEE80211_IS_CHAN_2GHZ(ni->ni_chan))
		sc->rxon->flags |= htole32(IWN_RXON_AUTO | IWN_RXON_24GHZ);
	if (ic->ic_flags & IEEE80211_F_SHSLOT)
		sc->rxon->flags |= htole32(IWN_RXON_SHSLOT);
	if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
		sc->rxon->flags |= htole32(IWN_RXON_SHPREAMBLE);
	if (IEEE80211_IS_CHAN_A(ni->ni_chan)) {
		sc->rxon->cck_mask  = 0;
		sc->rxon->ofdm_mask = 0x15;
	} else if (IEEE80211_IS_CHAN_B(ni->ni_chan)) {
		sc->rxon->cck_mask  = 0x03;
		sc->rxon->ofdm_mask = 0;
	} else {
		/* Assume 802.11b/g. */
		sc->rxon->cck_mask  = 0x0f;
		sc->rxon->ofdm_mask = 0x15;
	}
	/* try HT */
	sc->rxon->flags |= htole32(iwn_get_rxon_ht_flags(sc, ni->ni_chan));
	sc->rxon->filter |= htole32(IWN_FILTER_BSS);
	DPRINTF(sc, IWN_DEBUG_STATE, "rxon chan %d flags %x, curhtprotmode=%d\n",
	    sc->rxon->chan, le32toh(sc->rxon->flags), ic->ic_curhtprotmode);

	if ((error = iwn_send_rxon(sc, 0, 1)) != 0) {
		device_printf(sc->sc_dev, "%s: could not send RXON\n",
		    __func__);
		return error;
	}

	/* Fake a join to initialize the TX rate. */
	((struct iwn_node *)ni)->id = IWN_ID_BSS;
	iwn_newassoc(ni, 1);

	/* Add BSS node. */
	memset(&node, 0, sizeof node);
	IEEE80211_ADDR_COPY(node.macaddr, ni->ni_macaddr);
	node.id = IWN_ID_BSS;
	if (IEEE80211_IS_CHAN_HT(ni->ni_chan)) {
		switch (ni->ni_htcap & IEEE80211_HTCAP_SMPS) {
		case IEEE80211_HTCAP_SMPS_ENA:
			node.htflags |= htole32(IWN_SMPS_MIMO_DIS);
			break;
		case IEEE80211_HTCAP_SMPS_DYNAMIC:
			node.htflags |= htole32(IWN_SMPS_MIMO_PROT);
			break;
		}
		node.htflags |= htole32(IWN_AMDPU_SIZE_FACTOR(3) |
		    IWN_AMDPU_DENSITY(5));	/* 4us */
		if (IEEE80211_IS_CHAN_HT40(ni->ni_chan))
			node.htflags |= htole32(IWN_NODE_HT40);
	}
	DPRINTF(sc, IWN_DEBUG_STATE, "%s: adding BSS node\n", __func__);
	error = ops->add_node(sc, &node, 1);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not add BSS node, error %d\n", __func__, error);
		return error;
	}
	DPRINTF(sc, IWN_DEBUG_STATE, "%s: setting link quality for node %d\n",
	    __func__, node.id);
	if ((error = iwn_set_link_quality(sc, ni)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not setup link quality for node %d, error %d\n",
		    __func__, node.id, error);
		return error;
	}

	if ((error = iwn_init_sensitivity(sc)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not set sensitivity, error %d\n", __func__,
		    error);
		return error;
	}
	/* Start periodic calibration timer. */
	sc->calib.state = IWN_CALIB_STATE_ASSOC;
	sc->calib_cnt = 0;
	callout_reset(&sc->calib_to, msecs_to_ticks(500), iwn_calib_timeout,
	    sc);

	/* Link LED always on while associated. */
	iwn_set_led(sc, IWN_LED_LINK, 0, 1);

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

	return 0;
}

/*
 * This function is called by upper layer when an ADDBA request is received
 * from another STA and before the ADDBA response is sent.
 */
static int
iwn_ampdu_rx_start(struct ieee80211_node *ni, struct ieee80211_rx_ampdu *rap,
    int baparamset, int batimeout, int baseqctl)
{
#define MS(_v, _f)	(((_v) & _f) >> _f##_S)
	struct iwn_softc *sc = ni->ni_ic->ic_softc;
	struct iwn_ops *ops = &sc->ops;
	struct iwn_node *wn = (void *)ni;
	struct iwn_node_info node;
	uint16_t ssn;
	uint8_t tid;
	int error;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	tid = MS(le16toh(baparamset), IEEE80211_BAPS_TID);
	ssn = MS(le16toh(baseqctl), IEEE80211_BASEQ_START);

	if (wn->id == IWN_ID_UNDEFINED)
		return (ENOENT);

	memset(&node, 0, sizeof node);
	node.id = wn->id;
	node.control = IWN_NODE_UPDATE;
	node.flags = IWN_FLAG_SET_ADDBA;
	node.addba_tid = tid;
	node.addba_ssn = htole16(ssn);
	DPRINTF(sc, IWN_DEBUG_RECV, "ADDBA RA=%d TID=%d SSN=%d\n",
	    wn->id, tid, ssn);
	error = ops->add_node(sc, &node, 1);
	if (error != 0)
		return error;
	return sc->sc_ampdu_rx_start(ni, rap, baparamset, batimeout, baseqctl);
#undef MS
}

/*
 * This function is called by upper layer on teardown of an HT-immediate
 * Block Ack agreement (eg. uppon receipt of a DELBA frame).
 */
static void
iwn_ampdu_rx_stop(struct ieee80211_node *ni, struct ieee80211_rx_ampdu *rap)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct iwn_softc *sc = ic->ic_softc;
	struct iwn_ops *ops = &sc->ops;
	struct iwn_node *wn = (void *)ni;
	struct iwn_node_info node;
	uint8_t tid;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	if (wn->id == IWN_ID_UNDEFINED)
		goto end;

	/* XXX: tid as an argument */
	for (tid = 0; tid < WME_NUM_TID; tid++) {
		if (&ni->ni_rx_ampdu[tid] == rap)
			break;
	}

	memset(&node, 0, sizeof node);
	node.id = wn->id;
	node.control = IWN_NODE_UPDATE;
	node.flags = IWN_FLAG_SET_DELBA;
	node.delba_tid = tid;
	DPRINTF(sc, IWN_DEBUG_RECV, "DELBA RA=%d TID=%d\n", wn->id, tid);
	(void)ops->add_node(sc, &node, 1);
end:
	sc->sc_ampdu_rx_stop(ni, rap);
}

static int
iwn_addba_request(struct ieee80211_node *ni, struct ieee80211_tx_ampdu *tap,
    int dialogtoken, int baparamset, int batimeout)
{
	struct iwn_softc *sc = ni->ni_ic->ic_softc;
	int qid;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	for (qid = sc->firstaggqueue; qid < sc->ntxqs; qid++) {
		if (sc->qid2tap[qid] == NULL)
			break;
	}
	if (qid == sc->ntxqs) {
		DPRINTF(sc, IWN_DEBUG_XMIT, "%s: not free aggregation queue\n",
		    __func__);
		return 0;
	}
	tap->txa_private = malloc(sizeof(int), M_DEVBUF, M_NOWAIT);
	if (tap->txa_private == NULL) {
		device_printf(sc->sc_dev,
		    "%s: failed to alloc TX aggregation structure\n", __func__);
		return 0;
	}
	sc->qid2tap[qid] = tap;
	*(int *)tap->txa_private = qid;
	return sc->sc_addba_request(ni, tap, dialogtoken, baparamset,
	    batimeout);
}

static int
iwn_addba_response(struct ieee80211_node *ni, struct ieee80211_tx_ampdu *tap,
    int code, int baparamset, int batimeout)
{
	struct iwn_softc *sc = ni->ni_ic->ic_softc;
	int qid = *(int *)tap->txa_private;
	uint8_t tid = tap->txa_tid;
	int ret;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	if (code == IEEE80211_STATUS_SUCCESS) {
		ni->ni_txseqs[tid] = tap->txa_start & 0xfff;
		ret = iwn_ampdu_tx_start(ni->ni_ic, ni, tid);
		if (ret != 1)
			return ret;
	} else {
		sc->qid2tap[qid] = NULL;
		free(tap->txa_private, M_DEVBUF);
		tap->txa_private = NULL;
	}
	return sc->sc_addba_response(ni, tap, code, baparamset, batimeout);
}

/*
 * This function is called by upper layer when an ADDBA response is received
 * from another STA.
 */
static int
iwn_ampdu_tx_start(struct ieee80211com *ic, struct ieee80211_node *ni,
    uint8_t tid)
{
	struct ieee80211_tx_ampdu *tap = &ni->ni_tx_ampdu[tid];
	struct iwn_softc *sc = ni->ni_ic->ic_softc;
	struct iwn_ops *ops = &sc->ops;
	struct iwn_node *wn = (void *)ni;
	struct iwn_node_info node;
	int error, qid;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	if (wn->id == IWN_ID_UNDEFINED)
		return (0);

	/* Enable TX for the specified RA/TID. */
	wn->disable_tid &= ~(1 << tid);
	memset(&node, 0, sizeof node);
	node.id = wn->id;
	node.control = IWN_NODE_UPDATE;
	node.flags = IWN_FLAG_SET_DISABLE_TID;
	node.disable_tid = htole16(wn->disable_tid);
	error = ops->add_node(sc, &node, 1);
	if (error != 0)
		return 0;

	if ((error = iwn_nic_lock(sc)) != 0)
		return 0;
	qid = *(int *)tap->txa_private;
	DPRINTF(sc, IWN_DEBUG_XMIT, "%s: ra=%d tid=%d ssn=%d qid=%d\n",
	    __func__, wn->id, tid, tap->txa_start, qid);
	ops->ampdu_tx_start(sc, ni, qid, tid, tap->txa_start & 0xfff);
	iwn_nic_unlock(sc);

	iwn_set_link_quality(sc, ni);
	return 1;
}

static void
iwn_ampdu_tx_stop(struct ieee80211_node *ni, struct ieee80211_tx_ampdu *tap)
{
	struct iwn_softc *sc = ni->ni_ic->ic_softc;
	struct iwn_ops *ops = &sc->ops;
	uint8_t tid = tap->txa_tid;
	int qid;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	sc->sc_addba_stop(ni, tap);

	if (tap->txa_private == NULL)
		return;

	qid = *(int *)tap->txa_private;
	if (sc->txq[qid].queued != 0)
		return;
	if (iwn_nic_lock(sc) != 0)
		return;
	ops->ampdu_tx_stop(sc, qid, tid, tap->txa_start & 0xfff);
	iwn_nic_unlock(sc);
	sc->qid2tap[qid] = NULL;
	free(tap->txa_private, M_DEVBUF);
	tap->txa_private = NULL;
}

static void
iwn4965_ampdu_tx_start(struct iwn_softc *sc, struct ieee80211_node *ni,
    int qid, uint8_t tid, uint16_t ssn)
{
	struct iwn_node *wn = (void *)ni;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	/* Stop TX scheduler while we're changing its configuration. */
	iwn_prph_write(sc, IWN4965_SCHED_QUEUE_STATUS(qid),
	    IWN4965_TXQ_STATUS_CHGACT);

	/* Assign RA/TID translation to the queue. */
	iwn_mem_write_2(sc, sc->sched_base + IWN4965_SCHED_TRANS_TBL(qid),
	    wn->id << 4 | tid);

	/* Enable chain-building mode for the queue. */
	iwn_prph_setbits(sc, IWN4965_SCHED_QCHAIN_SEL, 1 << qid);

	/* Set starting sequence number from the ADDBA request. */
	sc->txq[qid].cur = sc->txq[qid].read = (ssn & 0xff);
	IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | (ssn & 0xff));
	iwn_prph_write(sc, IWN4965_SCHED_QUEUE_RDPTR(qid), ssn);

	/* Set scheduler window size. */
	iwn_mem_write(sc, sc->sched_base + IWN4965_SCHED_QUEUE_OFFSET(qid),
	    IWN_SCHED_WINSZ);
	/* Set scheduler frame limit. */
	iwn_mem_write(sc, sc->sched_base + IWN4965_SCHED_QUEUE_OFFSET(qid) + 4,
	    IWN_SCHED_LIMIT << 16);

	/* Enable interrupts for the queue. */
	iwn_prph_setbits(sc, IWN4965_SCHED_INTR_MASK, 1 << qid);

	/* Mark the queue as active. */
	iwn_prph_write(sc, IWN4965_SCHED_QUEUE_STATUS(qid),
	    IWN4965_TXQ_STATUS_ACTIVE | IWN4965_TXQ_STATUS_AGGR_ENA |
	    iwn_tid2fifo[tid] << 1);
}

static void
iwn4965_ampdu_tx_stop(struct iwn_softc *sc, int qid, uint8_t tid, uint16_t ssn)
{
	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	/* Stop TX scheduler while we're changing its configuration. */
	iwn_prph_write(sc, IWN4965_SCHED_QUEUE_STATUS(qid),
	    IWN4965_TXQ_STATUS_CHGACT);

	/* Set starting sequence number from the ADDBA request. */
	IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | (ssn & 0xff));
	iwn_prph_write(sc, IWN4965_SCHED_QUEUE_RDPTR(qid), ssn);

	/* Disable interrupts for the queue. */
	iwn_prph_clrbits(sc, IWN4965_SCHED_INTR_MASK, 1 << qid);

	/* Mark the queue as inactive. */
	iwn_prph_write(sc, IWN4965_SCHED_QUEUE_STATUS(qid),
	    IWN4965_TXQ_STATUS_INACTIVE | iwn_tid2fifo[tid] << 1);
}

static void
iwn5000_ampdu_tx_start(struct iwn_softc *sc, struct ieee80211_node *ni,
    int qid, uint8_t tid, uint16_t ssn)
{
	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	struct iwn_node *wn = (void *)ni;

	/* Stop TX scheduler while we're changing its configuration. */
	iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
	    IWN5000_TXQ_STATUS_CHGACT);

	/* Assign RA/TID translation to the queue. */
	iwn_mem_write_2(sc, sc->sched_base + IWN5000_SCHED_TRANS_TBL(qid),
	    wn->id << 4 | tid);

	/* Enable chain-building mode for the queue. */
	iwn_prph_setbits(sc, IWN5000_SCHED_QCHAIN_SEL, 1 << qid);

	/* Enable aggregation for the queue. */
	iwn_prph_setbits(sc, IWN5000_SCHED_AGGR_SEL, 1 << qid);

	/* Set starting sequence number from the ADDBA request. */
	sc->txq[qid].cur = sc->txq[qid].read = (ssn & 0xff);
	IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | (ssn & 0xff));
	iwn_prph_write(sc, IWN5000_SCHED_QUEUE_RDPTR(qid), ssn);

	/* Set scheduler window size and frame limit. */
	iwn_mem_write(sc, sc->sched_base + IWN5000_SCHED_QUEUE_OFFSET(qid) + 4,
	    IWN_SCHED_LIMIT << 16 | IWN_SCHED_WINSZ);

	/* Enable interrupts for the queue. */
	iwn_prph_setbits(sc, IWN5000_SCHED_INTR_MASK, 1 << qid);

	/* Mark the queue as active. */
	iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
	    IWN5000_TXQ_STATUS_ACTIVE | iwn_tid2fifo[tid]);
}

static void
iwn5000_ampdu_tx_stop(struct iwn_softc *sc, int qid, uint8_t tid, uint16_t ssn)
{
	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	/* Stop TX scheduler while we're changing its configuration. */
	iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
	    IWN5000_TXQ_STATUS_CHGACT);

	/* Disable aggregation for the queue. */
	iwn_prph_clrbits(sc, IWN5000_SCHED_AGGR_SEL, 1 << qid);

	/* Set starting sequence number from the ADDBA request. */
	IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | (ssn & 0xff));
	iwn_prph_write(sc, IWN5000_SCHED_QUEUE_RDPTR(qid), ssn);

	/* Disable interrupts for the queue. */
	iwn_prph_clrbits(sc, IWN5000_SCHED_INTR_MASK, 1 << qid);

	/* Mark the queue as inactive. */
	iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
	    IWN5000_TXQ_STATUS_INACTIVE | iwn_tid2fifo[tid]);
}

/*
 * Query calibration tables from the initialization firmware.  We do this
 * only once at first boot.  Called from a process context.
 */
static int
iwn5000_query_calibration(struct iwn_softc *sc)
{
	struct iwn5000_calib_config cmd;
	int error;

	memset(&cmd, 0, sizeof cmd);
	cmd.ucode.once.enable = htole32(0xffffffff);
	cmd.ucode.once.start  = htole32(0xffffffff);
	cmd.ucode.once.send   = htole32(0xffffffff);
	cmd.ucode.flags       = htole32(0xffffffff);
	DPRINTF(sc, IWN_DEBUG_CALIBRATE, "%s: sending calibration query\n",
	    __func__);
	error = iwn_cmd(sc, IWN5000_CMD_CALIB_CONFIG, &cmd, sizeof cmd, 0);
	if (error != 0)
		return error;

	/* Wait at most two seconds for calibration to complete. */
	if (!(sc->sc_flags & IWN_FLAG_CALIB_DONE))
		error = msleep(sc, &sc->sc_mtx, PCATCH, "iwncal", 2 * hz);
	return error;
}

/*
 * Send calibration results to the runtime firmware.  These results were
 * obtained on first boot from the initialization firmware.
 */
static int
iwn5000_send_calibration(struct iwn_softc *sc)
{
	int idx, error;

	for (idx = 0; idx < IWN5000_PHY_CALIB_MAX_RESULT; idx++) {
		if (!(sc->base_params->calib_need & (1<<idx))) {
			DPRINTF(sc, IWN_DEBUG_CALIBRATE,
			    "No need of calib %d\n",
			    idx);
			continue; /* no need for this calib */
		}
		if (sc->calibcmd[idx].buf == NULL) {
			DPRINTF(sc, IWN_DEBUG_CALIBRATE,
			    "Need calib idx : %d but no available data\n",
			    idx);
			continue;
		}

		DPRINTF(sc, IWN_DEBUG_CALIBRATE,
		    "send calibration result idx=%d len=%d\n", idx,
		    sc->calibcmd[idx].len);
		error = iwn_cmd(sc, IWN_CMD_PHY_CALIB, sc->calibcmd[idx].buf,
		    sc->calibcmd[idx].len, 0);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not send calibration result, error %d\n",
			    __func__, error);
			return error;
		}
	}
	return 0;
}

static int
iwn5000_send_wimax_coex(struct iwn_softc *sc)
{
	struct iwn5000_wimax_coex wimax;

#if 0
	if (sc->hw_type == IWN_HW_REV_TYPE_6050) {
		/* Enable WiMAX coexistence for combo adapters. */
		wimax.flags =
		    IWN_WIMAX_COEX_ASSOC_WA_UNMASK |
		    IWN_WIMAX_COEX_UNASSOC_WA_UNMASK |
		    IWN_WIMAX_COEX_STA_TABLE_VALID |
		    IWN_WIMAX_COEX_ENABLE;
		memcpy(wimax.events, iwn6050_wimax_events,
		    sizeof iwn6050_wimax_events);
	} else
#endif
	{
		/* Disable WiMAX coexistence. */
		wimax.flags = 0;
		memset(wimax.events, 0, sizeof wimax.events);
	}
	DPRINTF(sc, IWN_DEBUG_RESET, "%s: Configuring WiMAX coexistence\n",
	    __func__);
	return iwn_cmd(sc, IWN5000_CMD_WIMAX_COEX, &wimax, sizeof wimax, 0);
}

static int
iwn5000_crystal_calib(struct iwn_softc *sc)
{
	struct iwn5000_phy_calib_crystal cmd;

	memset(&cmd, 0, sizeof cmd);
	cmd.code = IWN5000_PHY_CALIB_CRYSTAL;
	cmd.ngroups = 1;
	cmd.isvalid = 1;
	cmd.cap_pin[0] = le32toh(sc->eeprom_crystal) & 0xff;
	cmd.cap_pin[1] = (le32toh(sc->eeprom_crystal) >> 16) & 0xff;
	DPRINTF(sc, IWN_DEBUG_CALIBRATE, "sending crystal calibration %d, %d\n",
	    cmd.cap_pin[0], cmd.cap_pin[1]);
	return iwn_cmd(sc, IWN_CMD_PHY_CALIB, &cmd, sizeof cmd, 0);
}

static int
iwn5000_temp_offset_calib(struct iwn_softc *sc)
{
	struct iwn5000_phy_calib_temp_offset cmd;

	memset(&cmd, 0, sizeof cmd);
	cmd.code = IWN5000_PHY_CALIB_TEMP_OFFSET;
	cmd.ngroups = 1;
	cmd.isvalid = 1;
	if (sc->eeprom_temp != 0)
		cmd.offset = htole16(sc->eeprom_temp);
	else
		cmd.offset = htole16(IWN_DEFAULT_TEMP_OFFSET);
	DPRINTF(sc, IWN_DEBUG_CALIBRATE, "setting radio sensor offset to %d\n",
	    le16toh(cmd.offset));
	return iwn_cmd(sc, IWN_CMD_PHY_CALIB, &cmd, sizeof cmd, 0);
}

static int
iwn5000_temp_offset_calibv2(struct iwn_softc *sc)
{
	struct iwn5000_phy_calib_temp_offsetv2 cmd;

	memset(&cmd, 0, sizeof cmd);
	cmd.code = IWN5000_PHY_CALIB_TEMP_OFFSET;
	cmd.ngroups = 1;
	cmd.isvalid = 1;
	if (sc->eeprom_temp != 0) {
		cmd.offset_low = htole16(sc->eeprom_temp);
		cmd.offset_high = htole16(sc->eeprom_temp_high);
	} else {
		cmd.offset_low = htole16(IWN_DEFAULT_TEMP_OFFSET);
		cmd.offset_high = htole16(IWN_DEFAULT_TEMP_OFFSET);
	}
	cmd.burnt_voltage_ref = htole16(sc->eeprom_voltage);

	DPRINTF(sc, IWN_DEBUG_CALIBRATE,
	    "setting radio sensor low offset to %d, high offset to %d, voltage to %d\n",
	    le16toh(cmd.offset_low),
	    le16toh(cmd.offset_high),
	    le16toh(cmd.burnt_voltage_ref));

	return iwn_cmd(sc, IWN_CMD_PHY_CALIB, &cmd, sizeof cmd, 0);
}

/*
 * This function is called after the runtime firmware notifies us of its
 * readiness (called in a process context).
 */
static int
iwn4965_post_alive(struct iwn_softc *sc)
{
	int error, qid;

	if ((error = iwn_nic_lock(sc)) != 0)
		return error;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	/* Clear TX scheduler state in SRAM. */
	sc->sched_base = iwn_prph_read(sc, IWN_SCHED_SRAM_ADDR);
	iwn_mem_set_region_4(sc, sc->sched_base + IWN4965_SCHED_CTX_OFF, 0,
	    IWN4965_SCHED_CTX_LEN / sizeof (uint32_t));

	/* Set physical address of TX scheduler rings (1KB aligned). */
	iwn_prph_write(sc, IWN4965_SCHED_DRAM_ADDR, sc->sched_dma.paddr >> 10);

	IWN_SETBITS(sc, IWN_FH_TX_CHICKEN, IWN_FH_TX_CHICKEN_SCHED_RETRY);

	/* Disable chain mode for all our 16 queues. */
	iwn_prph_write(sc, IWN4965_SCHED_QCHAIN_SEL, 0);

	for (qid = 0; qid < IWN4965_NTXQUEUES; qid++) {
		iwn_prph_write(sc, IWN4965_SCHED_QUEUE_RDPTR(qid), 0);
		IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | 0);

		/* Set scheduler window size. */
		iwn_mem_write(sc, sc->sched_base +
		    IWN4965_SCHED_QUEUE_OFFSET(qid), IWN_SCHED_WINSZ);
		/* Set scheduler frame limit. */
		iwn_mem_write(sc, sc->sched_base +
		    IWN4965_SCHED_QUEUE_OFFSET(qid) + 4,
		    IWN_SCHED_LIMIT << 16);
	}

	/* Enable interrupts for all our 16 queues. */
	iwn_prph_write(sc, IWN4965_SCHED_INTR_MASK, 0xffff);
	/* Identify TX FIFO rings (0-7). */
	iwn_prph_write(sc, IWN4965_SCHED_TXFACT, 0xff);

	/* Mark TX rings (4 EDCA + cmd + 2 HCCA) as active. */
	for (qid = 0; qid < 7; qid++) {
		static uint8_t qid2fifo[] = { 3, 2, 1, 0, 4, 5, 6 };
		iwn_prph_write(sc, IWN4965_SCHED_QUEUE_STATUS(qid),
		    IWN4965_TXQ_STATUS_ACTIVE | qid2fifo[qid] << 1);
	}
	iwn_nic_unlock(sc);
	return 0;
}

/*
 * This function is called after the initialization or runtime firmware
 * notifies us of its readiness (called in a process context).
 */
static int
iwn5000_post_alive(struct iwn_softc *sc)
{
	int error, qid;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	/* Switch to using ICT interrupt mode. */
	iwn5000_ict_reset(sc);

	if ((error = iwn_nic_lock(sc)) != 0){
		DPRINTF(sc, IWN_DEBUG_TRACE, "->%s end in error\n", __func__);
		return error;
	}

	/* Clear TX scheduler state in SRAM. */
	sc->sched_base = iwn_prph_read(sc, IWN_SCHED_SRAM_ADDR);
	iwn_mem_set_region_4(sc, sc->sched_base + IWN5000_SCHED_CTX_OFF, 0,
	    IWN5000_SCHED_CTX_LEN / sizeof (uint32_t));

	/* Set physical address of TX scheduler rings (1KB aligned). */
	iwn_prph_write(sc, IWN5000_SCHED_DRAM_ADDR, sc->sched_dma.paddr >> 10);

	IWN_SETBITS(sc, IWN_FH_TX_CHICKEN, IWN_FH_TX_CHICKEN_SCHED_RETRY);

	/* Enable chain mode for all queues, except command queue. */
	if (sc->sc_flags & IWN_FLAG_PAN_SUPPORT)
		iwn_prph_write(sc, IWN5000_SCHED_QCHAIN_SEL, 0xfffdf);
	else
		iwn_prph_write(sc, IWN5000_SCHED_QCHAIN_SEL, 0xfffef);
	iwn_prph_write(sc, IWN5000_SCHED_AGGR_SEL, 0);

	for (qid = 0; qid < IWN5000_NTXQUEUES; qid++) {
		iwn_prph_write(sc, IWN5000_SCHED_QUEUE_RDPTR(qid), 0);
		IWN_WRITE(sc, IWN_HBUS_TARG_WRPTR, qid << 8 | 0);

		iwn_mem_write(sc, sc->sched_base +
		    IWN5000_SCHED_QUEUE_OFFSET(qid), 0);
		/* Set scheduler window size and frame limit. */
		iwn_mem_write(sc, sc->sched_base +
		    IWN5000_SCHED_QUEUE_OFFSET(qid) + 4,
		    IWN_SCHED_LIMIT << 16 | IWN_SCHED_WINSZ);
	}

	/* Enable interrupts for all our 20 queues. */
	iwn_prph_write(sc, IWN5000_SCHED_INTR_MASK, 0xfffff);
	/* Identify TX FIFO rings (0-7). */
	iwn_prph_write(sc, IWN5000_SCHED_TXFACT, 0xff);

	/* Mark TX rings (4 EDCA + cmd + 2 HCCA) as active. */
	if (sc->sc_flags & IWN_FLAG_PAN_SUPPORT) {
		/* Mark TX rings as active. */
		for (qid = 0; qid < 11; qid++) {
			static uint8_t qid2fifo[] = { 3, 2, 1, 0, 0, 4, 2, 5, 4, 7, 5 };
			iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
			    IWN5000_TXQ_STATUS_ACTIVE | qid2fifo[qid]);
		}
	} else {
		/* Mark TX rings (4 EDCA + cmd + 2 HCCA) as active. */
		for (qid = 0; qid < 7; qid++) {
			static uint8_t qid2fifo[] = { 3, 2, 1, 0, 7, 5, 6 };
			iwn_prph_write(sc, IWN5000_SCHED_QUEUE_STATUS(qid),
			    IWN5000_TXQ_STATUS_ACTIVE | qid2fifo[qid]);
		}
	}
	iwn_nic_unlock(sc);

	/* Configure WiMAX coexistence for combo adapters. */
	error = iwn5000_send_wimax_coex(sc);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not configure WiMAX coexistence, error %d\n",
		    __func__, error);
		return error;
	}
	if (sc->hw_type != IWN_HW_REV_TYPE_5150) {
		/* Perform crystal calibration. */
		error = iwn5000_crystal_calib(sc);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "%s: crystal calibration failed, error %d\n",
			    __func__, error);
			return error;
		}
	}
	if (!(sc->sc_flags & IWN_FLAG_CALIB_DONE)) {
		/* Query calibration from the initialization firmware. */
		if ((error = iwn5000_query_calibration(sc)) != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not query calibration, error %d\n",
			    __func__, error);
			return error;
		}
		/*
		 * We have the calibration results now, reboot with the
		 * runtime firmware (call ourselves recursively!)
		 */
		iwn_hw_stop(sc);
		error = iwn_hw_init(sc);
	} else {
		/* Send calibration results to runtime firmware. */
		error = iwn5000_send_calibration(sc);
	}

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

	return error;
}

/*
 * The firmware boot code is small and is intended to be copied directly into
 * the NIC internal memory (no DMA transfer).
 */
static int
iwn4965_load_bootcode(struct iwn_softc *sc, const uint8_t *ucode, int size)
{
	int error, ntries;

	size /= sizeof (uint32_t);

	if ((error = iwn_nic_lock(sc)) != 0)
		return error;

	/* Copy microcode image into NIC memory. */
	iwn_prph_write_region_4(sc, IWN_BSM_SRAM_BASE,
	    (const uint32_t *)ucode, size);

	iwn_prph_write(sc, IWN_BSM_WR_MEM_SRC, 0);
	iwn_prph_write(sc, IWN_BSM_WR_MEM_DST, IWN_FW_TEXT_BASE);
	iwn_prph_write(sc, IWN_BSM_WR_DWCOUNT, size);

	/* Start boot load now. */
	iwn_prph_write(sc, IWN_BSM_WR_CTRL, IWN_BSM_WR_CTRL_START);

	/* Wait for transfer to complete. */
	for (ntries = 0; ntries < 1000; ntries++) {
		if (!(iwn_prph_read(sc, IWN_BSM_WR_CTRL) &
		    IWN_BSM_WR_CTRL_START))
			break;
		DELAY(10);
	}
	if (ntries == 1000) {
		device_printf(sc->sc_dev, "%s: could not load boot firmware\n",
		    __func__);
		iwn_nic_unlock(sc);
		return ETIMEDOUT;
	}

	/* Enable boot after power up. */
	iwn_prph_write(sc, IWN_BSM_WR_CTRL, IWN_BSM_WR_CTRL_START_EN);

	iwn_nic_unlock(sc);
	return 0;
}

static int
iwn4965_load_firmware(struct iwn_softc *sc)
{
	struct iwn_fw_info *fw = &sc->fw;
	struct iwn_dma_info *dma = &sc->fw_dma;
	int error;

	/* Copy initialization sections into pre-allocated DMA-safe memory. */
	memcpy(dma->vaddr, fw->init.data, fw->init.datasz);
	bus_dmamap_sync(dma->tag, dma->map, BUS_DMASYNC_PREWRITE);
	memcpy(dma->vaddr + IWN4965_FW_DATA_MAXSZ,
	    fw->init.text, fw->init.textsz);
	bus_dmamap_sync(dma->tag, dma->map, BUS_DMASYNC_PREWRITE);

	/* Tell adapter where to find initialization sections. */
	if ((error = iwn_nic_lock(sc)) != 0)
		return error;
	iwn_prph_write(sc, IWN_BSM_DRAM_DATA_ADDR, dma->paddr >> 4);
	iwn_prph_write(sc, IWN_BSM_DRAM_DATA_SIZE, fw->init.datasz);
	iwn_prph_write(sc, IWN_BSM_DRAM_TEXT_ADDR,
	    (dma->paddr + IWN4965_FW_DATA_MAXSZ) >> 4);
	iwn_prph_write(sc, IWN_BSM_DRAM_TEXT_SIZE, fw->init.textsz);
	iwn_nic_unlock(sc);

	/* Load firmware boot code. */
	error = iwn4965_load_bootcode(sc, fw->boot.text, fw->boot.textsz);
	if (error != 0) {
		device_printf(sc->sc_dev, "%s: could not load boot firmware\n",
		    __func__);
		return error;
	}
	/* Now press "execute". */
	IWN_WRITE(sc, IWN_RESET, 0);

	/* Wait at most one second for first alive notification. */
	if ((error = msleep(sc, &sc->sc_mtx, PCATCH, "iwninit", hz)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: timeout waiting for adapter to initialize, error %d\n",
		    __func__, error);
		return error;
	}

	/* Retrieve current temperature for initial TX power calibration. */
	sc->rawtemp = sc->ucode_info.temp[3].chan20MHz;
	sc->temp = iwn4965_get_temperature(sc);

	/* Copy runtime sections into pre-allocated DMA-safe memory. */
	memcpy(dma->vaddr, fw->main.data, fw->main.datasz);
	bus_dmamap_sync(dma->tag, dma->map, BUS_DMASYNC_PREWRITE);
	memcpy(dma->vaddr + IWN4965_FW_DATA_MAXSZ,
	    fw->main.text, fw->main.textsz);
	bus_dmamap_sync(dma->tag, dma->map, BUS_DMASYNC_PREWRITE);

	/* Tell adapter where to find runtime sections. */
	if ((error = iwn_nic_lock(sc)) != 0)
		return error;
	iwn_prph_write(sc, IWN_BSM_DRAM_DATA_ADDR, dma->paddr >> 4);
	iwn_prph_write(sc, IWN_BSM_DRAM_DATA_SIZE, fw->main.datasz);
	iwn_prph_write(sc, IWN_BSM_DRAM_TEXT_ADDR,
	    (dma->paddr + IWN4965_FW_DATA_MAXSZ) >> 4);
	iwn_prph_write(sc, IWN_BSM_DRAM_TEXT_SIZE,
	    IWN_FW_UPDATED | fw->main.textsz);
	iwn_nic_unlock(sc);

	return 0;
}

static int
iwn5000_load_firmware_section(struct iwn_softc *sc, uint32_t dst,
    const uint8_t *section, int size)
{
	struct iwn_dma_info *dma = &sc->fw_dma;
	int error;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	/* Copy firmware section into pre-allocated DMA-safe memory. */
	memcpy(dma->vaddr, section, size);
	bus_dmamap_sync(dma->tag, dma->map, BUS_DMASYNC_PREWRITE);

	if ((error = iwn_nic_lock(sc)) != 0)
		return error;

	IWN_WRITE(sc, IWN_FH_TX_CONFIG(IWN_SRVC_DMACHNL),
	    IWN_FH_TX_CONFIG_DMA_PAUSE);

	IWN_WRITE(sc, IWN_FH_SRAM_ADDR(IWN_SRVC_DMACHNL), dst);
	IWN_WRITE(sc, IWN_FH_TFBD_CTRL0(IWN_SRVC_DMACHNL),
	    IWN_LOADDR(dma->paddr));
	IWN_WRITE(sc, IWN_FH_TFBD_CTRL1(IWN_SRVC_DMACHNL),
	    IWN_HIADDR(dma->paddr) << 28 | size);
	IWN_WRITE(sc, IWN_FH_TXBUF_STATUS(IWN_SRVC_DMACHNL),
	    IWN_FH_TXBUF_STATUS_TBNUM(1) |
	    IWN_FH_TXBUF_STATUS_TBIDX(1) |
	    IWN_FH_TXBUF_STATUS_TFBD_VALID);

	/* Kick Flow Handler to start DMA transfer. */
	IWN_WRITE(sc, IWN_FH_TX_CONFIG(IWN_SRVC_DMACHNL),
	    IWN_FH_TX_CONFIG_DMA_ENA | IWN_FH_TX_CONFIG_CIRQ_HOST_ENDTFD);

	iwn_nic_unlock(sc);

	/* Wait at most five seconds for FH DMA transfer to complete. */
	return msleep(sc, &sc->sc_mtx, PCATCH, "iwninit", 5 * hz);
}

static int
iwn5000_load_firmware(struct iwn_softc *sc)
{
	struct iwn_fw_part *fw;
	int error;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	/* Load the initialization firmware on first boot only. */
	fw = (sc->sc_flags & IWN_FLAG_CALIB_DONE) ?
	    &sc->fw.main : &sc->fw.init;

	error = iwn5000_load_firmware_section(sc, IWN_FW_TEXT_BASE,
	    fw->text, fw->textsz);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not load firmware %s section, error %d\n",
		    __func__, ".text", error);
		return error;
	}
	error = iwn5000_load_firmware_section(sc, IWN_FW_DATA_BASE,
	    fw->data, fw->datasz);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not load firmware %s section, error %d\n",
		    __func__, ".data", error);
		return error;
	}

	/* Now press "execute". */
	IWN_WRITE(sc, IWN_RESET, 0);
	return 0;
}

/*
 * Extract text and data sections from a legacy firmware image.
 */
static int
iwn_read_firmware_leg(struct iwn_softc *sc, struct iwn_fw_info *fw)
{
	const uint32_t *ptr;
	size_t hdrlen = 24;
	uint32_t rev;

	ptr = (const uint32_t *)fw->data;
	rev = le32toh(*ptr++);

	sc->ucode_rev = rev;

	/* Check firmware API version. */
	if (IWN_FW_API(rev) <= 1) {
		device_printf(sc->sc_dev,
		    "%s: bad firmware, need API version >=2\n", __func__);
		return EINVAL;
	}
	if (IWN_FW_API(rev) >= 3) {
		/* Skip build number (version 2 header). */
		hdrlen += 4;
		ptr++;
	}
	if (fw->size < hdrlen) {
		device_printf(sc->sc_dev, "%s: firmware too short: %zu bytes\n",
		    __func__, fw->size);
		return EINVAL;
	}
	fw->main.textsz = le32toh(*ptr++);
	fw->main.datasz = le32toh(*ptr++);
	fw->init.textsz = le32toh(*ptr++);
	fw->init.datasz = le32toh(*ptr++);
	fw->boot.textsz = le32toh(*ptr++);

	/* Check that all firmware sections fit. */
	if (fw->size < hdrlen + fw->main.textsz + fw->main.datasz +
	    fw->init.textsz + fw->init.datasz + fw->boot.textsz) {
		device_printf(sc->sc_dev, "%s: firmware too short: %zu bytes\n",
		    __func__, fw->size);
		return EINVAL;
	}

	/* Get pointers to firmware sections. */
	fw->main.text = (const uint8_t *)ptr;
	fw->main.data = fw->main.text + fw->main.textsz;
	fw->init.text = fw->main.data + fw->main.datasz;
	fw->init.data = fw->init.text + fw->init.textsz;
	fw->boot.text = fw->init.data + fw->init.datasz;
	return 0;
}

/*
 * Extract text and data sections from a TLV firmware image.
 */
static int
iwn_read_firmware_tlv(struct iwn_softc *sc, struct iwn_fw_info *fw,
    uint16_t alt)
{
	const struct iwn_fw_tlv_hdr *hdr;
	const struct iwn_fw_tlv *tlv;
	const uint8_t *ptr, *end;
	uint64_t altmask;
	uint32_t len, tmp;

	if (fw->size < sizeof (*hdr)) {
		device_printf(sc->sc_dev, "%s: firmware too short: %zu bytes\n",
		    __func__, fw->size);
		return EINVAL;
	}
	hdr = (const struct iwn_fw_tlv_hdr *)fw->data;
	if (hdr->signature != htole32(IWN_FW_SIGNATURE)) {
		device_printf(sc->sc_dev, "%s: bad firmware signature 0x%08x\n",
		    __func__, le32toh(hdr->signature));
		return EINVAL;
	}
	DPRINTF(sc, IWN_DEBUG_RESET, "FW: \"%.64s\", build 0x%x\n", hdr->descr,
	    le32toh(hdr->build));
	sc->ucode_rev = le32toh(hdr->rev);

	/*
	 * Select the closest supported alternative that is less than
	 * or equal to the specified one.
	 */
	altmask = le64toh(hdr->altmask);
	while (alt > 0 && !(altmask & (1ULL << alt)))
		alt--;	/* Downgrade. */
	DPRINTF(sc, IWN_DEBUG_RESET, "using alternative %d\n", alt);

	ptr = (const uint8_t *)(hdr + 1);
	end = (const uint8_t *)(fw->data + fw->size);

	/* Parse type-length-value fields. */
	while (ptr + sizeof (*tlv) <= end) {
		tlv = (const struct iwn_fw_tlv *)ptr;
		len = le32toh(tlv->len);

		ptr += sizeof (*tlv);
		if (ptr + len > end) {
			device_printf(sc->sc_dev,
			    "%s: firmware too short: %zu bytes\n", __func__,
			    fw->size);
			return EINVAL;
		}
		/* Skip other alternatives. */
		if (tlv->alt != 0 && tlv->alt != htole16(alt))
			goto next;

		switch (le16toh(tlv->type)) {
		case IWN_FW_TLV_MAIN_TEXT:
			fw->main.text = ptr;
			fw->main.textsz = len;
			break;
		case IWN_FW_TLV_MAIN_DATA:
			fw->main.data = ptr;
			fw->main.datasz = len;
			break;
		case IWN_FW_TLV_INIT_TEXT:
			fw->init.text = ptr;
			fw->init.textsz = len;
			break;
		case IWN_FW_TLV_INIT_DATA:
			fw->init.data = ptr;
			fw->init.datasz = len;
			break;
		case IWN_FW_TLV_BOOT_TEXT:
			fw->boot.text = ptr;
			fw->boot.textsz = len;
			break;
		case IWN_FW_TLV_ENH_SENS:
			if (!len)
				sc->sc_flags |= IWN_FLAG_ENH_SENS;
			break;
		case IWN_FW_TLV_PHY_CALIB:
			tmp = le32toh(*ptr);
			if (tmp < 253) {
				sc->reset_noise_gain = tmp;
				sc->noise_gain = tmp + 1;
			}
			break;
		case IWN_FW_TLV_PAN:
			sc->sc_flags |= IWN_FLAG_PAN_SUPPORT;
			DPRINTF(sc, IWN_DEBUG_RESET,
			    "PAN Support found: %d\n", 1);
			break;
		case IWN_FW_TLV_FLAGS:
			if (len < sizeof(uint32_t))
				break;
			if (len % sizeof(uint32_t))
				break;
			sc->tlv_feature_flags = le32toh(*ptr);
			DPRINTF(sc, IWN_DEBUG_RESET,
			    "%s: feature: 0x%08x\n",
			    __func__,
			    sc->tlv_feature_flags);
			break;
		case IWN_FW_TLV_PBREQ_MAXLEN:
		case IWN_FW_TLV_RUNT_EVTLOG_PTR:
		case IWN_FW_TLV_RUNT_EVTLOG_SIZE:
		case IWN_FW_TLV_RUNT_ERRLOG_PTR:
		case IWN_FW_TLV_INIT_EVTLOG_PTR:
		case IWN_FW_TLV_INIT_EVTLOG_SIZE:
		case IWN_FW_TLV_INIT_ERRLOG_PTR:
		case IWN_FW_TLV_WOWLAN_INST:
		case IWN_FW_TLV_WOWLAN_DATA:
			DPRINTF(sc, IWN_DEBUG_RESET,
			    "TLV type %d recognized but not handled\n",
			    le16toh(tlv->type));
			break;
		default:
			DPRINTF(sc, IWN_DEBUG_RESET,
			    "TLV type %d not handled\n", le16toh(tlv->type));
			break;
		}
 next:		/* TLV fields are 32-bit aligned. */
		ptr += (len + 3) & ~3;
	}
	return 0;
}

static int
iwn_read_firmware(struct iwn_softc *sc)
{
	struct iwn_fw_info *fw = &sc->fw;
	int error;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	IWN_UNLOCK(sc);

	memset(fw, 0, sizeof (*fw));

	/* Read firmware image from filesystem. */
	sc->fw_fp = firmware_get(sc->fwname);
	if (sc->fw_fp == NULL) {
		device_printf(sc->sc_dev, "%s: could not read firmware %s\n",
		    __func__, sc->fwname);
		IWN_LOCK(sc);
		return EINVAL;
	}
	IWN_LOCK(sc);

	fw->size = sc->fw_fp->datasize;
	fw->data = (const uint8_t *)sc->fw_fp->data;
	if (fw->size < sizeof (uint32_t)) {
		device_printf(sc->sc_dev, "%s: firmware too short: %zu bytes\n",
		    __func__, fw->size);
		error = EINVAL;
		goto fail;
	}

	/* Retrieve text and data sections. */
	if (*(const uint32_t *)fw->data != 0)	/* Legacy image. */
		error = iwn_read_firmware_leg(sc, fw);
	else
		error = iwn_read_firmware_tlv(sc, fw, 1);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not read firmware sections, error %d\n",
		    __func__, error);
		goto fail;
	}

	device_printf(sc->sc_dev, "%s: ucode rev=0x%08x\n", __func__, sc->ucode_rev);

	/* Make sure text and data sections fit in hardware memory. */
	if (fw->main.textsz > sc->fw_text_maxsz ||
	    fw->main.datasz > sc->fw_data_maxsz ||
	    fw->init.textsz > sc->fw_text_maxsz ||
	    fw->init.datasz > sc->fw_data_maxsz ||
	    fw->boot.textsz > IWN_FW_BOOT_TEXT_MAXSZ ||
	    (fw->boot.textsz & 3) != 0) {
		device_printf(sc->sc_dev, "%s: firmware sections too large\n",
		    __func__);
		error = EINVAL;
		goto fail;
	}

	/* We can proceed with loading the firmware. */
	return 0;

fail:	iwn_unload_firmware(sc);
	return error;
}

static void
iwn_unload_firmware(struct iwn_softc *sc)
{
	firmware_put(sc->fw_fp, FIRMWARE_UNLOAD);
	sc->fw_fp = NULL;
}

static int
iwn_clock_wait(struct iwn_softc *sc)
{
	int ntries;

	/* Set "initialization complete" bit. */
	IWN_SETBITS(sc, IWN_GP_CNTRL, IWN_GP_CNTRL_INIT_DONE);

	/* Wait for clock stabilization. */
	for (ntries = 0; ntries < 2500; ntries++) {
		if (IWN_READ(sc, IWN_GP_CNTRL) & IWN_GP_CNTRL_MAC_CLOCK_READY)
			return 0;
		DELAY(10);
	}
	device_printf(sc->sc_dev,
	    "%s: timeout waiting for clock stabilization\n", __func__);
	return ETIMEDOUT;
}

static int
iwn_apm_init(struct iwn_softc *sc)
{
	uint32_t reg;
	int error;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	/* Disable L0s exit timer (NMI bug workaround). */
	IWN_SETBITS(sc, IWN_GIO_CHICKEN, IWN_GIO_CHICKEN_DIS_L0S_TIMER);
	/* Don't wait for ICH L0s (ICH bug workaround). */
	IWN_SETBITS(sc, IWN_GIO_CHICKEN, IWN_GIO_CHICKEN_L1A_NO_L0S_RX);

	/* Set FH wait threshold to max (HW bug under stress workaround). */
	IWN_SETBITS(sc, IWN_DBG_HPET_MEM, 0xffff0000);

	/* Enable HAP INTA to move adapter from L1a to L0s. */
	IWN_SETBITS(sc, IWN_HW_IF_CONFIG, IWN_HW_IF_CONFIG_HAP_WAKE_L1A);

	/* Retrieve PCIe Active State Power Management (ASPM). */
	reg = pci_read_config(sc->sc_dev, sc->sc_cap_off + PCIER_LINK_CTL, 4);
	/* Workaround for HW instability in PCIe L0->L0s->L1 transition. */
	if (reg & PCIEM_LINK_CTL_ASPMC_L1)	/* L1 Entry enabled. */
		IWN_SETBITS(sc, IWN_GIO, IWN_GIO_L0S_ENA);
	else
		IWN_CLRBITS(sc, IWN_GIO, IWN_GIO_L0S_ENA);

	if (sc->base_params->pll_cfg_val)
		IWN_SETBITS(sc, IWN_ANA_PLL, sc->base_params->pll_cfg_val);

	/* Wait for clock stabilization before accessing prph. */
	if ((error = iwn_clock_wait(sc)) != 0)
		return error;

	if ((error = iwn_nic_lock(sc)) != 0)
		return error;
	if (sc->hw_type == IWN_HW_REV_TYPE_4965) {
		/* Enable DMA and BSM (Bootstrap State Machine). */
		iwn_prph_write(sc, IWN_APMG_CLK_EN,
		    IWN_APMG_CLK_CTRL_DMA_CLK_RQT |
		    IWN_APMG_CLK_CTRL_BSM_CLK_RQT);
	} else {
		/* Enable DMA. */
		iwn_prph_write(sc, IWN_APMG_CLK_EN,
		    IWN_APMG_CLK_CTRL_DMA_CLK_RQT);
	}
	DELAY(20);
	/* Disable L1-Active. */
	iwn_prph_setbits(sc, IWN_APMG_PCI_STT, IWN_APMG_PCI_STT_L1A_DIS);
	iwn_nic_unlock(sc);

	return 0;
}

static void
iwn_apm_stop_master(struct iwn_softc *sc)
{
	int ntries;

	/* Stop busmaster DMA activity. */
	IWN_SETBITS(sc, IWN_RESET, IWN_RESET_STOP_MASTER);
	for (ntries = 0; ntries < 100; ntries++) {
		if (IWN_READ(sc, IWN_RESET) & IWN_RESET_MASTER_DISABLED)
			return;
		DELAY(10);
	}
	device_printf(sc->sc_dev, "%s: timeout waiting for master\n", __func__);
}

static void
iwn_apm_stop(struct iwn_softc *sc)
{
	iwn_apm_stop_master(sc);

	/* Reset the entire device. */
	IWN_SETBITS(sc, IWN_RESET, IWN_RESET_SW);
	DELAY(10);
	/* Clear "initialization complete" bit. */
	IWN_CLRBITS(sc, IWN_GP_CNTRL, IWN_GP_CNTRL_INIT_DONE);
}

static int
iwn4965_nic_config(struct iwn_softc *sc)
{
	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	if (IWN_RFCFG_TYPE(sc->rfcfg) == 1) {
		/*
		 * I don't believe this to be correct but this is what the
		 * vendor driver is doing. Probably the bits should not be
		 * shifted in IWN_RFCFG_*.
		 */
		IWN_SETBITS(sc, IWN_HW_IF_CONFIG,
		    IWN_RFCFG_TYPE(sc->rfcfg) |
		    IWN_RFCFG_STEP(sc->rfcfg) |
		    IWN_RFCFG_DASH(sc->rfcfg));
	}
	IWN_SETBITS(sc, IWN_HW_IF_CONFIG,
	    IWN_HW_IF_CONFIG_RADIO_SI | IWN_HW_IF_CONFIG_MAC_SI);
	return 0;
}

static int
iwn5000_nic_config(struct iwn_softc *sc)
{
	uint32_t tmp;
	int error;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	if (IWN_RFCFG_TYPE(sc->rfcfg) < 3) {
		IWN_SETBITS(sc, IWN_HW_IF_CONFIG,
		    IWN_RFCFG_TYPE(sc->rfcfg) |
		    IWN_RFCFG_STEP(sc->rfcfg) |
		    IWN_RFCFG_DASH(sc->rfcfg));
	}
	IWN_SETBITS(sc, IWN_HW_IF_CONFIG,
	    IWN_HW_IF_CONFIG_RADIO_SI | IWN_HW_IF_CONFIG_MAC_SI);

	if ((error = iwn_nic_lock(sc)) != 0)
		return error;
	iwn_prph_setbits(sc, IWN_APMG_PS, IWN_APMG_PS_EARLY_PWROFF_DIS);

	if (sc->hw_type == IWN_HW_REV_TYPE_1000) {
		/*
		 * Select first Switching Voltage Regulator (1.32V) to
		 * solve a stability issue related to noisy DC2DC line
		 * in the silicon of 1000 Series.
		 */
		tmp = iwn_prph_read(sc, IWN_APMG_DIGITAL_SVR);
		tmp &= ~IWN_APMG_DIGITAL_SVR_VOLTAGE_MASK;
		tmp |= IWN_APMG_DIGITAL_SVR_VOLTAGE_1_32;
		iwn_prph_write(sc, IWN_APMG_DIGITAL_SVR, tmp);
	}
	iwn_nic_unlock(sc);

	if (sc->sc_flags & IWN_FLAG_INTERNAL_PA) {
		/* Use internal power amplifier only. */
		IWN_WRITE(sc, IWN_GP_DRIVER, IWN_GP_DRIVER_RADIO_2X2_IPA);
	}
	if (sc->base_params->additional_nic_config && sc->calib_ver >= 6) {
		/* Indicate that ROM calibration version is >=6. */
		IWN_SETBITS(sc, IWN_GP_DRIVER, IWN_GP_DRIVER_CALIB_VER6);
	}
	if (sc->base_params->additional_gp_drv_bit)
		IWN_SETBITS(sc, IWN_GP_DRIVER,
		    sc->base_params->additional_gp_drv_bit);
	return 0;
}

/*
 * Take NIC ownership over Intel Active Management Technology (AMT).
 */
static int
iwn_hw_prepare(struct iwn_softc *sc)
{
	int ntries;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	/* Check if hardware is ready. */
	IWN_SETBITS(sc, IWN_HW_IF_CONFIG, IWN_HW_IF_CONFIG_NIC_READY);
	for (ntries = 0; ntries < 5; ntries++) {
		if (IWN_READ(sc, IWN_HW_IF_CONFIG) &
		    IWN_HW_IF_CONFIG_NIC_READY)
			return 0;
		DELAY(10);
	}

	/* Hardware not ready, force into ready state. */
	IWN_SETBITS(sc, IWN_HW_IF_CONFIG, IWN_HW_IF_CONFIG_PREPARE);
	for (ntries = 0; ntries < 15000; ntries++) {
		if (!(IWN_READ(sc, IWN_HW_IF_CONFIG) &
		    IWN_HW_IF_CONFIG_PREPARE_DONE))
			break;
		DELAY(10);
	}
	if (ntries == 15000)
		return ETIMEDOUT;

	/* Hardware should be ready now. */
	IWN_SETBITS(sc, IWN_HW_IF_CONFIG, IWN_HW_IF_CONFIG_NIC_READY);
	for (ntries = 0; ntries < 5; ntries++) {
		if (IWN_READ(sc, IWN_HW_IF_CONFIG) &
		    IWN_HW_IF_CONFIG_NIC_READY)
			return 0;
		DELAY(10);
	}
	return ETIMEDOUT;
}

static int
iwn_hw_init(struct iwn_softc *sc)
{
	struct iwn_ops *ops = &sc->ops;
	int error, chnl, qid;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	/* Clear pending interrupts. */
	IWN_WRITE(sc, IWN_INT, 0xffffffff);

	if ((error = iwn_apm_init(sc)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not power ON adapter, error %d\n", __func__,
		    error);
		return error;
	}

	/* Select VMAIN power source. */
	if ((error = iwn_nic_lock(sc)) != 0)
		return error;
	iwn_prph_clrbits(sc, IWN_APMG_PS, IWN_APMG_PS_PWR_SRC_MASK);
	iwn_nic_unlock(sc);

	/* Perform adapter-specific initialization. */
	if ((error = ops->nic_config(sc)) != 0)
		return error;

	/* Initialize RX ring. */
	if ((error = iwn_nic_lock(sc)) != 0)
		return error;
	IWN_WRITE(sc, IWN_FH_RX_CONFIG, 0);
	IWN_WRITE(sc, IWN_FH_RX_WPTR, 0);
	/* Set physical address of RX ring (256-byte aligned). */
	IWN_WRITE(sc, IWN_FH_RX_BASE, sc->rxq.desc_dma.paddr >> 8);
	/* Set physical address of RX status (16-byte aligned). */
	IWN_WRITE(sc, IWN_FH_STATUS_WPTR, sc->rxq.stat_dma.paddr >> 4);
	/* Enable RX. */
	IWN_WRITE(sc, IWN_FH_RX_CONFIG,
	    IWN_FH_RX_CONFIG_ENA           |
	    IWN_FH_RX_CONFIG_IGN_RXF_EMPTY |	/* HW bug workaround */
	    IWN_FH_RX_CONFIG_IRQ_DST_HOST  |
	    IWN_FH_RX_CONFIG_SINGLE_FRAME  |
	    IWN_FH_RX_CONFIG_RB_TIMEOUT(0) |
	    IWN_FH_RX_CONFIG_NRBD(IWN_RX_RING_COUNT_LOG));
	iwn_nic_unlock(sc);
	IWN_WRITE(sc, IWN_FH_RX_WPTR, (IWN_RX_RING_COUNT - 1) & ~7);

	if ((error = iwn_nic_lock(sc)) != 0)
		return error;

	/* Initialize TX scheduler. */
	iwn_prph_write(sc, sc->sched_txfact_addr, 0);

	/* Set physical address of "keep warm" page (16-byte aligned). */
	IWN_WRITE(sc, IWN_FH_KW_ADDR, sc->kw_dma.paddr >> 4);

	/* Initialize TX rings. */
	for (qid = 0; qid < sc->ntxqs; qid++) {
		struct iwn_tx_ring *txq = &sc->txq[qid];

		/* Set physical address of TX ring (256-byte aligned). */
		IWN_WRITE(sc, IWN_FH_CBBC_QUEUE(qid),
		    txq->desc_dma.paddr >> 8);
	}
	iwn_nic_unlock(sc);

	/* Enable DMA channels. */
	for (chnl = 0; chnl < sc->ndmachnls; chnl++) {
		IWN_WRITE(sc, IWN_FH_TX_CONFIG(chnl),
		    IWN_FH_TX_CONFIG_DMA_ENA |
		    IWN_FH_TX_CONFIG_DMA_CREDIT_ENA);
	}

	/* Clear "radio off" and "commands blocked" bits. */
	IWN_WRITE(sc, IWN_UCODE_GP1_CLR, IWN_UCODE_GP1_RFKILL);
	IWN_WRITE(sc, IWN_UCODE_GP1_CLR, IWN_UCODE_GP1_CMD_BLOCKED);

	/* Clear pending interrupts. */
	IWN_WRITE(sc, IWN_INT, 0xffffffff);
	/* Enable interrupt coalescing. */
	IWN_WRITE(sc, IWN_INT_COALESCING, 512 / 8);
	/* Enable interrupts. */
	IWN_WRITE(sc, IWN_INT_MASK, sc->int_mask);

	/* _Really_ make sure "radio off" bit is cleared! */
	IWN_WRITE(sc, IWN_UCODE_GP1_CLR, IWN_UCODE_GP1_RFKILL);
	IWN_WRITE(sc, IWN_UCODE_GP1_CLR, IWN_UCODE_GP1_RFKILL);

	/* Enable shadow registers. */
	if (sc->base_params->shadow_reg_enable)
		IWN_SETBITS(sc, IWN_SHADOW_REG_CTRL, 0x800fffff);

	if ((error = ops->load_firmware(sc)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not load firmware, error %d\n", __func__,
		    error);
		return error;
	}
	/* Wait at most one second for firmware alive notification. */
	if ((error = msleep(sc, &sc->sc_mtx, PCATCH, "iwninit", hz)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: timeout waiting for adapter to initialize, error %d\n",
		    __func__, error);
		return error;
	}
	/* Do post-firmware initialization. */

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

	return ops->post_alive(sc);
}

static void
iwn_hw_stop(struct iwn_softc *sc)
{
	int chnl, qid, ntries;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	IWN_WRITE(sc, IWN_RESET, IWN_RESET_NEVO);

	/* Disable interrupts. */
	IWN_WRITE(sc, IWN_INT_MASK, 0);
	IWN_WRITE(sc, IWN_INT, 0xffffffff);
	IWN_WRITE(sc, IWN_FH_INT, 0xffffffff);
	sc->sc_flags &= ~IWN_FLAG_USE_ICT;

	/* Make sure we no longer hold the NIC lock. */
	iwn_nic_unlock(sc);

	/* Stop TX scheduler. */
	iwn_prph_write(sc, sc->sched_txfact_addr, 0);

	/* Stop all DMA channels. */
	if (iwn_nic_lock(sc) == 0) {
		for (chnl = 0; chnl < sc->ndmachnls; chnl++) {
			IWN_WRITE(sc, IWN_FH_TX_CONFIG(chnl), 0);
			for (ntries = 0; ntries < 200; ntries++) {
				if (IWN_READ(sc, IWN_FH_TX_STATUS) &
				    IWN_FH_TX_STATUS_IDLE(chnl))
					break;
				DELAY(10);
			}
		}
		iwn_nic_unlock(sc);
	}

	/* Stop RX ring. */
	iwn_reset_rx_ring(sc, &sc->rxq);

	/* Reset all TX rings. */
	for (qid = 0; qid < sc->ntxqs; qid++)
		iwn_reset_tx_ring(sc, &sc->txq[qid]);

	if (iwn_nic_lock(sc) == 0) {
		iwn_prph_write(sc, IWN_APMG_CLK_DIS,
		    IWN_APMG_CLK_CTRL_DMA_CLK_RQT);
		iwn_nic_unlock(sc);
	}
	DELAY(5);
	/* Power OFF adapter. */
	iwn_apm_stop(sc);
}

static void
iwn_panicked(void *arg0, int pending)
{
	struct iwn_softc *sc = arg0;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);
#if 0
	int error;
#endif

	if (vap == NULL) {
		printf("%s: null vap\n", __func__);
		return;
	}

	device_printf(sc->sc_dev, "%s: controller panicked, iv_state = %d; "
	    "restarting\n", __func__, vap->iv_state);

	/*
	 * This is not enough work. We need to also reinitialise
	 * the correct transmit state for aggregation enabled queues,
	 * which has a very specific requirement of
	 * ring index = 802.11 seqno % 256.  If we don't do this (which
	 * we definitely don't!) then the firmware will just panic again.
	 */
#if 1
	ieee80211_restart_all(ic);
#else
	IWN_LOCK(sc);

	iwn_stop_locked(sc);
	if ((error = iwn_init_locked(sc)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not init hardware\n", __func__);
		goto unlock;
	}
	if (vap->iv_state >= IEEE80211_S_AUTH &&
	    (error = iwn_auth(sc, vap)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not move to auth state\n", __func__);
	}
	if (vap->iv_state >= IEEE80211_S_RUN &&
	    (error = iwn_run(sc, vap)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not move to run state\n", __func__);
	}

unlock:
	IWN_UNLOCK(sc);
#endif
}

static int
iwn_init_locked(struct iwn_softc *sc)
{
	int error;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s begin\n", __func__);

	IWN_LOCK_ASSERT(sc);

	if (sc->sc_flags & IWN_FLAG_RUNNING)
		goto end;

	sc->sc_flags |= IWN_FLAG_RUNNING;

	if ((error = iwn_hw_prepare(sc)) != 0) {
		device_printf(sc->sc_dev, "%s: hardware not ready, error %d\n",
		    __func__, error);
		goto fail;
	}

	/* Initialize interrupt mask to default value. */
	sc->int_mask = IWN_INT_MASK_DEF;
	sc->sc_flags &= ~IWN_FLAG_USE_ICT;

	/* Check that the radio is not disabled by hardware switch. */
	if (!(IWN_READ(sc, IWN_GP_CNTRL) & IWN_GP_CNTRL_RFKILL)) {
		iwn_stop_locked(sc);
		DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

		return (1);
	}

	/* Read firmware images from the filesystem. */
	if ((error = iwn_read_firmware(sc)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not read firmware, error %d\n", __func__,
		    error);
		goto fail;
	}

	/* Initialize hardware and upload firmware. */
	error = iwn_hw_init(sc);
	iwn_unload_firmware(sc);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not initialize hardware, error %d\n", __func__,
		    error);
		goto fail;
	}

	/* Configure adapter now that it is ready. */
	if ((error = iwn_config(sc)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not configure device, error %d\n", __func__,
		    error);
		goto fail;
	}

	callout_reset(&sc->watchdog_to, hz, iwn_watchdog, sc);

end:
	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end\n",__func__);

	return (0);

fail:
	iwn_stop_locked(sc);

	DPRINTF(sc, IWN_DEBUG_TRACE, "->%s: end in error\n",__func__);

	return (-1);
}

static int
iwn_init(struct iwn_softc *sc)
{
	int error;

	IWN_LOCK(sc);
	error = iwn_init_locked(sc);
	IWN_UNLOCK(sc);

	return (error);
}

static void
iwn_stop_locked(struct iwn_softc *sc)
{

	IWN_LOCK_ASSERT(sc);

	if (!(sc->sc_flags & IWN_FLAG_RUNNING))
		return;

	sc->sc_is_scanning = 0;
	sc->sc_tx_timer = 0;
	callout_stop(&sc->watchdog_to);
	callout_stop(&sc->scan_timeout);
	callout_stop(&sc->calib_to);
	sc->sc_flags &= ~IWN_FLAG_RUNNING;

	/* Power OFF hardware. */
	iwn_hw_stop(sc);
}

static void
iwn_stop(struct iwn_softc *sc)
{
	IWN_LOCK(sc);
	iwn_stop_locked(sc);
	IWN_UNLOCK(sc);
}

/*
 * Callback from net80211 to start a scan.
 */
static void
iwn_scan_start(struct ieee80211com *ic)
{
	struct iwn_softc *sc = ic->ic_softc;

	IWN_LOCK(sc);
	/* make the link LED blink while we're scanning */
	iwn_set_led(sc, IWN_LED_LINK, 20, 2);
	IWN_UNLOCK(sc);
}

/*
 * Callback from net80211 to terminate a scan.
 */
static void
iwn_scan_end(struct ieee80211com *ic)
{
	struct iwn_softc *sc = ic->ic_softc;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);

	IWN_LOCK(sc);
	if (vap->iv_state == IEEE80211_S_RUN) {
		/* Set link LED to ON status if we are associated */
		iwn_set_led(sc, IWN_LED_LINK, 0, 1);
	}
	IWN_UNLOCK(sc);
}

/*
 * Callback from net80211 to force a channel change.
 */
static void
iwn_set_channel(struct ieee80211com *ic)
{
	struct iwn_softc *sc = ic->ic_softc;
	int error;

	DPRINTF(sc, IWN_DEBUG_TRACE, "->Doing %s\n", __func__);

	IWN_LOCK(sc);
	/*
	 * Only need to set the channel in Monitor mode. AP scanning and auth
	 * are already taken care of by their respective firmware commands.
	 */
	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		error = iwn_config(sc);
		if (error != 0)
		device_printf(sc->sc_dev,
		    "%s: error %d settting channel\n", __func__, error);
	}
	IWN_UNLOCK(sc);
}

/*
 * Callback from net80211 to start scanning of the current channel.
 */
static void
iwn_scan_curchan(struct ieee80211_scan_state *ss, unsigned long maxdwell)
{
	struct ieee80211vap *vap = ss->ss_vap;
	struct ieee80211com *ic = vap->iv_ic;
	struct iwn_softc *sc = ic->ic_softc;
	int error;

	IWN_LOCK(sc);
	error = iwn_scan(sc, vap, ss, ic->ic_curchan);
	IWN_UNLOCK(sc);
	if (error != 0)
		ieee80211_cancel_scan(vap);
}

/*
 * Callback from net80211 to handle the minimum dwell time being met.
 * The intent is to terminate the scan but we just let the firmware
 * notify us when it's finished as we have no safe way to abort it.
 */
static void
iwn_scan_mindwell(struct ieee80211_scan_state *ss)
{
	/* NB: don't try to abort scan; wait for firmware to finish */
}
#ifdef	IWN_DEBUG
#define	IWN_DESC(x) case x:	return #x

/*
 * Translate CSR code to string
 */
static char *iwn_get_csr_string(int csr)
{
	switch (csr) {
		IWN_DESC(IWN_HW_IF_CONFIG);
		IWN_DESC(IWN_INT_COALESCING);
		IWN_DESC(IWN_INT);
		IWN_DESC(IWN_INT_MASK);
		IWN_DESC(IWN_FH_INT);
		IWN_DESC(IWN_GPIO_IN);
		IWN_DESC(IWN_RESET);
		IWN_DESC(IWN_GP_CNTRL);
		IWN_DESC(IWN_HW_REV);
		IWN_DESC(IWN_EEPROM);
		IWN_DESC(IWN_EEPROM_GP);
		IWN_DESC(IWN_OTP_GP);
		IWN_DESC(IWN_GIO);
		IWN_DESC(IWN_GP_UCODE);
		IWN_DESC(IWN_GP_DRIVER);
		IWN_DESC(IWN_UCODE_GP1);
		IWN_DESC(IWN_UCODE_GP2);
		IWN_DESC(IWN_LED);
		IWN_DESC(IWN_DRAM_INT_TBL);
		IWN_DESC(IWN_GIO_CHICKEN);
		IWN_DESC(IWN_ANA_PLL);
		IWN_DESC(IWN_HW_REV_WA);
		IWN_DESC(IWN_DBG_HPET_MEM);
	default:
		return "UNKNOWN CSR";
	}
}

/*
 * This function print firmware register
 */
static void
iwn_debug_register(struct iwn_softc *sc)
{
	int i;
	static const uint32_t csr_tbl[] = {
		IWN_HW_IF_CONFIG,
		IWN_INT_COALESCING,
		IWN_INT,
		IWN_INT_MASK,
		IWN_FH_INT,
		IWN_GPIO_IN,
		IWN_RESET,
		IWN_GP_CNTRL,
		IWN_HW_REV,
		IWN_EEPROM,
		IWN_EEPROM_GP,
		IWN_OTP_GP,
		IWN_GIO,
		IWN_GP_UCODE,
		IWN_GP_DRIVER,
		IWN_UCODE_GP1,
		IWN_UCODE_GP2,
		IWN_LED,
		IWN_DRAM_INT_TBL,
		IWN_GIO_CHICKEN,
		IWN_ANA_PLL,
		IWN_HW_REV_WA,
		IWN_DBG_HPET_MEM,
	};
	DPRINTF(sc, IWN_DEBUG_REGISTER,
	    "CSR values: (2nd byte of IWN_INT_COALESCING is IWN_INT_PERIODIC)%s",
	    "\n");
	for (i = 0; i <  nitems(csr_tbl); i++){
		DPRINTF(sc, IWN_DEBUG_REGISTER,"  %10s: 0x%08x ",
			iwn_get_csr_string(csr_tbl[i]), IWN_READ(sc, csr_tbl[i]));
		if ((i+1) % 3 == 0)
			DPRINTF(sc, IWN_DEBUG_REGISTER,"%s","\n");
	}
	DPRINTF(sc, IWN_DEBUG_REGISTER,"%s","\n");
}
#endif


