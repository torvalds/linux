/*	$OpenBSD: if_otus.c,v 1.49 2015/11/24 13:33:18 mpi Exp $	*/

/*-
 * Copyright (c) 2009 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2015 Adrian Chadd <adrian@FreeBSD.org>
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
 * Driver for Atheros AR9001U chipset.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/firmware.h>
#include <sys/module.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_ratectl.h>
#ifdef	IEEE80211_SUPPORT_SUPERG
#include <net80211/ieee80211_superg.h>
#endif

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include "usbdevs.h"

#define USB_DEBUG_VAR otus_debug
#include <dev/usb/usb_debug.h>

#include "if_otusreg.h"

static int otus_debug = 0;
static SYSCTL_NODE(_hw_usb, OID_AUTO, otus, CTLFLAG_RW, 0, "USB otus");
SYSCTL_INT(_hw_usb_otus, OID_AUTO, debug, CTLFLAG_RWTUN, &otus_debug, 0,
    "Debug level");
#define	OTUS_DEBUG_XMIT		0x00000001
#define	OTUS_DEBUG_RECV		0x00000002
#define	OTUS_DEBUG_TXDONE	0x00000004
#define	OTUS_DEBUG_RXDONE	0x00000008
#define	OTUS_DEBUG_CMD		0x00000010
#define	OTUS_DEBUG_CMDDONE	0x00000020
#define	OTUS_DEBUG_RESET	0x00000040
#define	OTUS_DEBUG_STATE	0x00000080
#define	OTUS_DEBUG_CMDNOTIFY	0x00000100
#define	OTUS_DEBUG_REGIO	0x00000200
#define	OTUS_DEBUG_IRQ		0x00000400
#define	OTUS_DEBUG_TXCOMP	0x00000800
#define	OTUS_DEBUG_ANY		0xffffffff

#define	OTUS_DPRINTF(sc, dm, ...) \
	do { \
		if ((dm == OTUS_DEBUG_ANY) || (dm & otus_debug)) \
			device_printf(sc->sc_dev, __VA_ARGS__); \
	} while (0)

#define	OTUS_DEV(v, p) { USB_VPI(v, p, 0) }
static const STRUCT_USB_HOST_ID otus_devs[] = {
	OTUS_DEV(USB_VENDOR_ACCTON,		USB_PRODUCT_ACCTON_WN7512),
	OTUS_DEV(USB_VENDOR_ATHEROS2,		USB_PRODUCT_ATHEROS2_3CRUSBN275),
	OTUS_DEV(USB_VENDOR_ATHEROS2,		USB_PRODUCT_ATHEROS2_TG121N),
	OTUS_DEV(USB_VENDOR_ATHEROS2,		USB_PRODUCT_ATHEROS2_AR9170),
	OTUS_DEV(USB_VENDOR_ATHEROS2,		USB_PRODUCT_ATHEROS2_WN612),
	OTUS_DEV(USB_VENDOR_ATHEROS2,		USB_PRODUCT_ATHEROS2_WN821NV2),
	OTUS_DEV(USB_VENDOR_AVM,		USB_PRODUCT_AVM_FRITZWLAN),
	OTUS_DEV(USB_VENDOR_CACE,		USB_PRODUCT_CACE_AIRPCAPNX),
	OTUS_DEV(USB_VENDOR_DLINK2,		USB_PRODUCT_DLINK2_DWA130D1),
	OTUS_DEV(USB_VENDOR_DLINK2,		USB_PRODUCT_DLINK2_DWA160A1),
	OTUS_DEV(USB_VENDOR_DLINK2,		USB_PRODUCT_DLINK2_DWA160A2),
	OTUS_DEV(USB_VENDOR_IODATA,		USB_PRODUCT_IODATA_WNGDNUS2),
	OTUS_DEV(USB_VENDOR_NEC,		USB_PRODUCT_NEC_WL300NUG),
	OTUS_DEV(USB_VENDOR_NETGEAR,		USB_PRODUCT_NETGEAR_WN111V2),
	OTUS_DEV(USB_VENDOR_NETGEAR,		USB_PRODUCT_NETGEAR_WNA1000),
	OTUS_DEV(USB_VENDOR_NETGEAR,		USB_PRODUCT_NETGEAR_WNDA3100),
	OTUS_DEV(USB_VENDOR_PLANEX2,		USB_PRODUCT_PLANEX2_GW_US300),
	OTUS_DEV(USB_VENDOR_WISTRONNEWEB,	USB_PRODUCT_WISTRONNEWEB_O8494),
	OTUS_DEV(USB_VENDOR_WISTRONNEWEB,	USB_PRODUCT_WISTRONNEWEB_WNC0600),
	OTUS_DEV(USB_VENDOR_ZCOM,		USB_PRODUCT_ZCOM_UB81),
	OTUS_DEV(USB_VENDOR_ZCOM,		USB_PRODUCT_ZCOM_UB82),
	OTUS_DEV(USB_VENDOR_ZYDAS,		USB_PRODUCT_ZYDAS_ZD1221),
	OTUS_DEV(USB_VENDOR_ZYXEL,		USB_PRODUCT_ZYXEL_NWD271N),
};

static device_probe_t otus_match;
static device_attach_t otus_attach;
static device_detach_t otus_detach;

static int	otus_attachhook(struct otus_softc *);
void		otus_get_chanlist(struct otus_softc *);
static void	otus_getradiocaps(struct ieee80211com *, int, int *,
		    struct ieee80211_channel[]);
int		otus_load_firmware(struct otus_softc *, const char *,
		    uint32_t);
int		otus_open_pipes(struct otus_softc *);
void		otus_close_pipes(struct otus_softc *);

static int	otus_alloc_tx_cmd_list(struct otus_softc *);
static void	otus_free_tx_cmd_list(struct otus_softc *);

static int	otus_alloc_rx_list(struct otus_softc *);
static void	otus_free_rx_list(struct otus_softc *);
static int	otus_alloc_tx_list(struct otus_softc *);
static void	otus_free_tx_list(struct otus_softc *);
static void	otus_free_list(struct otus_softc *, struct otus_data [], int);
static struct otus_data *_otus_getbuf(struct otus_softc *);
static struct otus_data *otus_getbuf(struct otus_softc *);
static void	otus_freebuf(struct otus_softc *, struct otus_data *);

static struct otus_tx_cmd *_otus_get_txcmd(struct otus_softc *);
static struct otus_tx_cmd *otus_get_txcmd(struct otus_softc *);
static void	otus_free_txcmd(struct otus_softc *, struct otus_tx_cmd *);

void		otus_next_scan(void *, int);
static void	otus_tx_task(void *, int pending);
void		otus_do_async(struct otus_softc *,
		    void (*)(struct otus_softc *, void *), void *, int);
int		otus_newstate(struct ieee80211vap *, enum ieee80211_state,
		    int);
int		otus_cmd(struct otus_softc *, uint8_t, const void *, int,
		    void *, int);
void		otus_write(struct otus_softc *, uint32_t, uint32_t);
int		otus_write_barrier(struct otus_softc *);
static struct	ieee80211_node *otus_node_alloc(struct ieee80211vap *vap,
		    const uint8_t mac[IEEE80211_ADDR_LEN]);
int		otus_media_change(struct ifnet *);
int		otus_read_eeprom(struct otus_softc *);
void		otus_newassoc(struct ieee80211_node *, int);
void		otus_cmd_rxeof(struct otus_softc *, uint8_t *, int);
void		otus_sub_rxeof(struct otus_softc *, uint8_t *, int,
		    struct mbufq *);
static int	otus_tx(struct otus_softc *, struct ieee80211_node *,
		    struct mbuf *, struct otus_data *,
		    const struct ieee80211_bpf_params *);
int		otus_ioctl(struct ifnet *, u_long, caddr_t);
int		otus_set_multi(struct otus_softc *);
static int	otus_updateedca(struct ieee80211com *);
static void	otus_updateedca_locked(struct otus_softc *);
static void	otus_updateslot(struct otus_softc *);
static void	otus_set_operating_mode(struct otus_softc *sc);
static void	otus_set_rx_filter(struct otus_softc *sc);
int		otus_init_mac(struct otus_softc *);
uint32_t	otus_phy_get_def(struct otus_softc *, uint32_t);
int		otus_set_board_values(struct otus_softc *,
		    struct ieee80211_channel *);
int		otus_program_phy(struct otus_softc *,
		    struct ieee80211_channel *);
int		otus_set_rf_bank4(struct otus_softc *,
		    struct ieee80211_channel *);
void		otus_get_delta_slope(uint32_t, uint32_t *, uint32_t *);
static int	otus_set_chan(struct otus_softc *, struct ieee80211_channel *,
		    int);
int		otus_set_key(struct ieee80211com *, struct ieee80211_node *,
		    struct ieee80211_key *);
void		otus_set_key_cb(struct otus_softc *, void *);
void		otus_delete_key(struct ieee80211com *, struct ieee80211_node *,
		    struct ieee80211_key *);
void		otus_delete_key_cb(struct otus_softc *, void *);
void		otus_calibrate_to(void *, int);
int		otus_set_bssid(struct otus_softc *, const uint8_t *);
int		otus_set_macaddr(struct otus_softc *, const uint8_t *);
void		otus_led_newstate_type1(struct otus_softc *);
void		otus_led_newstate_type2(struct otus_softc *);
void		otus_led_newstate_type3(struct otus_softc *);
int		otus_init(struct otus_softc *sc);
void		otus_stop(struct otus_softc *sc);

static device_method_t otus_methods[] = {
	DEVMETHOD(device_probe,		otus_match),
	DEVMETHOD(device_attach,	otus_attach),
	DEVMETHOD(device_detach,	otus_detach),

	DEVMETHOD_END
};

static driver_t otus_driver = {
	.name = "otus",
	.methods = otus_methods,
	.size = sizeof(struct otus_softc)
};

static devclass_t otus_devclass;

DRIVER_MODULE(otus, uhub, otus_driver, otus_devclass, NULL, 0);
MODULE_DEPEND(otus, wlan, 1, 1, 1);
MODULE_DEPEND(otus, usb, 1, 1, 1);
MODULE_DEPEND(otus, firmware, 1, 1, 1);
MODULE_VERSION(otus, 1);

static usb_callback_t	otus_bulk_tx_callback;
static usb_callback_t	otus_bulk_rx_callback;
static usb_callback_t	otus_bulk_irq_callback;
static usb_callback_t	otus_bulk_cmd_callback;

static const struct usb_config otus_config[OTUS_N_XFER] = {
	[OTUS_BULK_TX] = {
	.type = UE_BULK,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_OUT,
	.bufsize = 0x200,
	.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
	.callback = otus_bulk_tx_callback,
	.timeout = 5000,	/* ms */
	},
	[OTUS_BULK_RX] = {
	.type = UE_BULK,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_IN,
	.bufsize = OTUS_RXBUFSZ,
	.flags = { .ext_buffer = 1, .pipe_bof = 1,.short_xfer_ok = 1,},
	.callback = otus_bulk_rx_callback,
	},
	[OTUS_BULK_IRQ] = {
	.type = UE_INTERRUPT,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_IN,
	.bufsize = OTUS_MAX_CTRLSZ,
	.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
	.callback = otus_bulk_irq_callback,
	},
	[OTUS_BULK_CMD] = {
	.type = UE_INTERRUPT,
	.endpoint = UE_ADDR_ANY,
	.direction = UE_DIR_OUT,
	.bufsize = OTUS_MAX_CTRLSZ,
	.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
	.callback = otus_bulk_cmd_callback,
	.timeout = 5000,	/* ms */
	},
};

static int
otus_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->usb_mode != USB_MODE_HOST ||
	    uaa->info.bIfaceIndex != 0 ||
	    uaa->info.bConfigIndex != 0)
	return (ENXIO);

	return (usbd_lookup_id_by_uaa(otus_devs, sizeof(otus_devs), uaa));
}

static int
otus_attach(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);
	struct otus_softc *sc = device_get_softc(self);
	int error;
	uint8_t iface_index;

	device_set_usb_desc(self);
	sc->sc_udev = uaa->device;
	sc->sc_dev = self;

	mtx_init(&sc->sc_mtx, device_get_nameunit(self), MTX_NETWORK_LOCK,
	    MTX_DEF);

	TIMEOUT_TASK_INIT(taskqueue_thread, &sc->scan_to, 0, otus_next_scan, sc);
	TIMEOUT_TASK_INIT(taskqueue_thread, &sc->calib_to, 0, otus_calibrate_to, sc);
	TASK_INIT(&sc->tx_task, 0, otus_tx_task, sc);
	mbufq_init(&sc->sc_snd, ifqmaxlen);

	iface_index = 0;
	error = usbd_transfer_setup(uaa->device, &iface_index, sc->sc_xfer,
	    otus_config, OTUS_N_XFER, sc, &sc->sc_mtx);
	if (error) {
		device_printf(sc->sc_dev,
		    "could not allocate USB transfers, err=%s\n",
		    usbd_errstr(error));
		goto fail_usb;
	}

	if ((error = otus_open_pipes(sc)) != 0) {
		device_printf(sc->sc_dev, "%s: could not open pipes\n",
		    __func__);
		goto fail;
	}

	/* XXX check return status; fail out if appropriate */
	if (otus_attachhook(sc) != 0)
		goto fail;

	return (0);

fail:
	otus_close_pipes(sc);
fail_usb:
	mtx_destroy(&sc->sc_mtx);
	return (ENXIO);
}

static int
otus_detach(device_t self)
{
	struct otus_softc *sc = device_get_softc(self);
	struct ieee80211com *ic = &sc->sc_ic;

	otus_stop(sc);

	usbd_transfer_unsetup(sc->sc_xfer, OTUS_N_XFER);

	taskqueue_drain_timeout(taskqueue_thread, &sc->scan_to);
	taskqueue_drain_timeout(taskqueue_thread, &sc->calib_to);
	taskqueue_drain(taskqueue_thread, &sc->tx_task);

	otus_close_pipes(sc);
#if 0
	/* Wait for all queued asynchronous commands to complete. */
	usb_rem_wait_task(sc->sc_udev, &sc->sc_task);

	usbd_ref_wait(sc->sc_udev);
#endif

	ieee80211_ifdetach(ic);
	mtx_destroy(&sc->sc_mtx);
	return 0;
}

static void
otus_delay_ms(struct otus_softc *sc, int ms)
{

	DELAY(1000 * ms);
}

static struct ieee80211vap *
otus_vap_create(struct ieee80211com *ic, const char name[IFNAMSIZ], int unit,
    enum ieee80211_opmode opmode, int flags,
    const uint8_t bssid[IEEE80211_ADDR_LEN],
    const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct otus_vap *uvp;
	struct ieee80211vap *vap;

	if (!TAILQ_EMPTY(&ic->ic_vaps))	 /* only one at a time */
		return (NULL);

	uvp =  malloc(sizeof(struct otus_vap), M_80211_VAP, M_WAITOK | M_ZERO);
	vap = &uvp->vap;

	if (ieee80211_vap_setup(ic, vap, name, unit, opmode,
	    flags, bssid) != 0) {
		/* out of memory */
		free(uvp, M_80211_VAP);
		return (NULL);
	}

	/* override state transition machine */
	uvp->newstate = vap->iv_newstate;
	vap->iv_newstate = otus_newstate;

	/* XXX TODO: double-check */
	vap->iv_ampdu_density = IEEE80211_HTCAP_MPDUDENSITY_16;
	vap->iv_ampdu_rxmax = IEEE80211_HTCAP_MAXRXAMPDU_32K;

	ieee80211_ratectl_init(vap);

	/* complete setup */
	ieee80211_vap_attach(vap, ieee80211_media_change,
	    ieee80211_media_status, mac);
	ic->ic_opmode = opmode;

	return (vap);
}

static void
otus_vap_delete(struct ieee80211vap *vap)
{
	struct otus_vap *uvp = OTUS_VAP(vap);

	ieee80211_ratectl_deinit(vap);
	ieee80211_vap_detach(vap);
	free(uvp, M_80211_VAP);
}

static void
otus_parent(struct ieee80211com *ic)
{
	struct otus_softc *sc = ic->ic_softc;
	int startall = 0;

	if (ic->ic_nrunning > 0) {
		if (!sc->sc_running) {
			otus_init(sc);
			startall = 1;
		} else {
			(void) otus_set_multi(sc);
		}
	} else if (sc->sc_running)
		otus_stop(sc);

	if (startall)
		ieee80211_start_all(ic);
}

static void
otus_drain_mbufq(struct otus_softc *sc)
{
	struct mbuf *m;
	struct ieee80211_node *ni;

	OTUS_LOCK_ASSERT(sc);
	while ((m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		ni = (struct ieee80211_node *) m->m_pkthdr.rcvif;
		m->m_pkthdr.rcvif = NULL;
		ieee80211_free_node(ni);
		m_freem(m);
	}
}

static void
otus_tx_start(struct otus_softc *sc)
{

	taskqueue_enqueue(taskqueue_thread, &sc->tx_task);
}

static int
otus_transmit(struct ieee80211com *ic, struct mbuf *m)
{
	struct otus_softc *sc = ic->ic_softc;
	int error;

	OTUS_LOCK(sc);
	if (! sc->sc_running) {
		OTUS_UNLOCK(sc);
		return (ENXIO);
	}

	/* XXX TODO: handle fragments */
	error = mbufq_enqueue(&sc->sc_snd, m);
	if (error) {
		OTUS_DPRINTF(sc, OTUS_DEBUG_XMIT,
		    "%s: mbufq_enqueue failed: %d\n",
		    __func__,
		    error);
		OTUS_UNLOCK(sc);
		return (error);
	}
	OTUS_UNLOCK(sc);

	/* Kick TX */
	otus_tx_start(sc);

	return (0);
}

static void
_otus_start(struct otus_softc *sc)
{
	struct ieee80211_node *ni;
	struct otus_data *bf;
	struct mbuf *m;

	OTUS_LOCK_ASSERT(sc);

	while ((m = mbufq_dequeue(&sc->sc_snd)) != NULL) {
		bf = otus_getbuf(sc);
		if (bf == NULL) {
			OTUS_DPRINTF(sc, OTUS_DEBUG_XMIT,
			    "%s: failed to get buffer\n", __func__);
			mbufq_prepend(&sc->sc_snd, m);
			break;
		}

		ni = (struct ieee80211_node *)m->m_pkthdr.rcvif;
		m->m_pkthdr.rcvif = NULL;

		if (otus_tx(sc, ni, m, bf, NULL) != 0) {
			OTUS_DPRINTF(sc, OTUS_DEBUG_XMIT,
			    "%s: failed to transmit\n", __func__);
			if_inc_counter(ni->ni_vap->iv_ifp,
			    IFCOUNTER_OERRORS, 1);
			otus_freebuf(sc, bf);
			ieee80211_free_node(ni);
			m_freem(m);
			break;
		}
	}
}

static void
otus_tx_task(void *arg, int pending)
{
	struct otus_softc *sc = arg;

	OTUS_LOCK(sc);
	_otus_start(sc);
	OTUS_UNLOCK(sc);
}

static int
otus_raw_xmit(struct ieee80211_node *ni, struct mbuf *m,
    const struct ieee80211_bpf_params *params)
{
	struct ieee80211com *ic= ni->ni_ic;
	struct otus_softc *sc = ic->ic_softc;
	struct otus_data *bf = NULL;
	int error = 0;

	/* Don't transmit if we're not running */
	OTUS_LOCK(sc);
	if (! sc->sc_running) {
		error = ENETDOWN;
		goto error;
	}

	bf = otus_getbuf(sc);
	if (bf == NULL) {
		error = ENOBUFS;
		goto error;
	}

	if (otus_tx(sc, ni, m, bf, params) != 0) {
		error = EIO;
		goto error;
	}

	OTUS_UNLOCK(sc);
	return (0);
error:
	if (bf)
		otus_freebuf(sc, bf);
	OTUS_UNLOCK(sc);
	m_freem(m);
	return (ENXIO);
}

static void
otus_update_chw(struct ieee80211com *ic)
{

	printf("%s: TODO\n", __func__);
}

static void
otus_set_channel(struct ieee80211com *ic)
{
	struct otus_softc *sc = ic->ic_softc;
	OTUS_DPRINTF(sc, OTUS_DEBUG_RESET, "%s: set channel: %d\n",
	    __func__,
	    ic->ic_curchan->ic_freq);

	OTUS_LOCK(sc);
	(void) otus_set_chan(sc, ic->ic_curchan, 0);
	OTUS_UNLOCK(sc);
}

static int
otus_ampdu_enable(struct ieee80211_node *ni, struct ieee80211_tx_ampdu *tap)
{

	/* For now, no A-MPDU TX support in the driver */
	return (0);
}

static void
otus_scan_start(struct ieee80211com *ic)
{

//	printf("%s: TODO\n", __func__);
}

static void
otus_scan_end(struct ieee80211com *ic)
{

//	printf("%s: TODO\n", __func__);
}

static void
otus_update_mcast(struct ieee80211com *ic)
{
	struct otus_softc *sc = ic->ic_softc;

	(void) otus_set_multi(sc);
}

static int
otus_attachhook(struct otus_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	usb_device_request_t req;
	uint32_t in, out;
	int error;

	/* Not locked */
	error = otus_load_firmware(sc, "otusfw_init", AR_FW_INIT_ADDR);
	if (error != 0) {
		device_printf(sc->sc_dev, "%s: could not load %s firmware\n",
		    __func__, "init");
		return (ENXIO);
	}

	/* XXX not locked? */
	otus_delay_ms(sc, 1000);

	/* Not locked */
	error = otus_load_firmware(sc, "otusfw_main", AR_FW_MAIN_ADDR);
	if (error != 0) {
		device_printf(sc->sc_dev, "%s: could not load %s firmware\n",
		    __func__, "main");
		return (ENXIO);
	}

	OTUS_LOCK(sc);

	/* Tell device that firmware transfer is complete. */
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = AR_FW_DOWNLOAD_COMPLETE;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	if (usbd_do_request_flags(sc->sc_udev, &sc->sc_mtx, &req, NULL,
	    0, NULL, 250) != 0) {
		OTUS_UNLOCK(sc);
		device_printf(sc->sc_dev,
		    "%s: firmware initialization failed\n",
		    __func__);
		return (ENXIO);
	}

	/* Send an ECHO command to check that everything is settled. */
	in = 0xbadc0ffe;
	if (otus_cmd(sc, AR_CMD_ECHO, &in, sizeof in, &out, sizeof(out)) != 0) {
		OTUS_UNLOCK(sc);
		device_printf(sc->sc_dev,
		    "%s: echo command failed\n", __func__);
		return (ENXIO);
	}
	if (in != out) {
		OTUS_UNLOCK(sc);
		device_printf(sc->sc_dev,
		    "%s: echo reply mismatch: 0x%08x!=0x%08x\n",
		    __func__, in, out);
		return (ENXIO);
	}

	/* Read entire EEPROM. */
	if (otus_read_eeprom(sc) != 0) {
		OTUS_UNLOCK(sc);
		device_printf(sc->sc_dev,
		    "%s: could not read EEPROM\n",
		    __func__);
		return (ENXIO);
	}

	OTUS_UNLOCK(sc);

	sc->txmask = sc->eeprom.baseEepHeader.txMask;
	sc->rxmask = sc->eeprom.baseEepHeader.rxMask;
	sc->capflags = sc->eeprom.baseEepHeader.opCapFlags;
	IEEE80211_ADDR_COPY(ic->ic_macaddr, sc->eeprom.baseEepHeader.macAddr);
	sc->sc_led_newstate = otus_led_newstate_type3;	/* XXX */

	device_printf(sc->sc_dev,
	    "MAC/BBP AR9170, RF AR%X, MIMO %dT%dR, address %s\n",
	    (sc->capflags & AR5416_OPFLAGS_11A) ?
		0x9104 : ((sc->txmask == 0x5) ? 0x9102 : 0x9101),
	    (sc->txmask == 0x5) ? 2 : 1, (sc->rxmask == 0x5) ? 2 : 1,
	    ether_sprintf(ic->ic_macaddr));

	ic->ic_softc = sc;
	ic->ic_name = device_get_nameunit(sc->sc_dev);
	ic->ic_phytype = IEEE80211_T_OFDM;	/* not only, but not used */
	ic->ic_opmode = IEEE80211_M_STA;	/* default to BSS mode */

	/* Set device capabilities. */
	ic->ic_caps =
	    IEEE80211_C_STA |		/* station mode */
#if 0
	    IEEE80211_C_BGSCAN |	/* Background scan. */
#endif
	    IEEE80211_C_SHPREAMBLE |	/* Short preamble supported. */
	    IEEE80211_C_WME |		/* WME/QoS */
	    IEEE80211_C_SHSLOT |	/* Short slot time supported. */
	    IEEE80211_C_FF |		/* Atheros fast-frames supported. */
	    IEEE80211_C_MONITOR |
	    IEEE80211_C_WPA;		/* WPA/RSN. */

	/* XXX TODO: 11n */

#if 0
	if (sc->eeprom.baseEepHeader.opCapFlags & AR5416_OPFLAGS_11G) {
		/* Set supported .11b and .11g rates. */
		ic->ic_sup_rates[IEEE80211_MODE_11B] =
		    ieee80211_std_rateset_11b;
		ic->ic_sup_rates[IEEE80211_MODE_11G] =
		    ieee80211_std_rateset_11g;
	}
	if (sc->eeprom.baseEepHeader.opCapFlags & AR5416_OPFLAGS_11A) {
		/* Set supported .11a rates. */
		ic->ic_sup_rates[IEEE80211_MODE_11A] =
		    ieee80211_std_rateset_11a;
	}
#endif

#if 0
	/* Build the list of supported channels. */
	otus_get_chanlist(sc);
#else
	otus_getradiocaps(ic, IEEE80211_CHAN_MAX, &ic->ic_nchans,
	    ic->ic_channels);
#endif

	ieee80211_ifattach(ic);
	ic->ic_raw_xmit = otus_raw_xmit;
	ic->ic_scan_start = otus_scan_start;
	ic->ic_scan_end = otus_scan_end;
	ic->ic_set_channel = otus_set_channel;
	ic->ic_getradiocaps = otus_getradiocaps;
	ic->ic_vap_create = otus_vap_create;
	ic->ic_vap_delete = otus_vap_delete;
	ic->ic_update_mcast = otus_update_mcast;
	ic->ic_update_promisc = otus_update_mcast;
	ic->ic_parent = otus_parent;
	ic->ic_transmit = otus_transmit;
	ic->ic_update_chw = otus_update_chw;
	ic->ic_ampdu_enable = otus_ampdu_enable;
	ic->ic_wme.wme_update = otus_updateedca;
	ic->ic_newassoc = otus_newassoc;
	ic->ic_node_alloc = otus_node_alloc;

#ifdef notyet
	ic->ic_set_key = otus_set_key;
	ic->ic_delete_key = otus_delete_key;
#endif

	ieee80211_radiotap_attach(ic, &sc->sc_txtap.wt_ihdr,
	    sizeof(sc->sc_txtap), OTUS_TX_RADIOTAP_PRESENT,
	    &sc->sc_rxtap.wr_ihdr, sizeof(sc->sc_rxtap),
	    OTUS_RX_RADIOTAP_PRESENT);

	return (0);
}

void
otus_get_chanlist(struct otus_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint16_t domain;
	uint8_t chan;
	int i;

	/* XXX regulatory domain. */
	domain = le16toh(sc->eeprom.baseEepHeader.regDmn[0]);
	OTUS_DPRINTF(sc, OTUS_DEBUG_RESET, "regdomain=0x%04x\n", domain);

	if (sc->eeprom.baseEepHeader.opCapFlags & AR5416_OPFLAGS_11G) {
		for (i = 0; i < 14; i++) {
			chan = ar_chans[i];
			ic->ic_channels[chan].ic_freq =
			    ieee80211_ieee2mhz(chan, IEEE80211_CHAN_2GHZ);
			ic->ic_channels[chan].ic_flags =
			    IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
			    IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;
		}
	}
	if (sc->eeprom.baseEepHeader.opCapFlags & AR5416_OPFLAGS_11A) {
		for (i = 14; i < nitems(ar_chans); i++) {
			chan = ar_chans[i];
			ic->ic_channels[chan].ic_freq =
			    ieee80211_ieee2mhz(chan, IEEE80211_CHAN_5GHZ);
			ic->ic_channels[chan].ic_flags = IEEE80211_CHAN_A;
		}
	}
}

static void
otus_getradiocaps(struct ieee80211com *ic,
    int maxchans, int *nchans, struct ieee80211_channel chans[])
{
	struct otus_softc *sc = ic->ic_softc;
	uint8_t bands[IEEE80211_MODE_BYTES];

	/* Set supported .11b and .11g rates. */
	memset(bands, 0, sizeof(bands));
	if (sc->eeprom.baseEepHeader.opCapFlags & AR5416_OPFLAGS_11G) {
		setbit(bands, IEEE80211_MODE_11B);
		setbit(bands, IEEE80211_MODE_11G);
#if 0
		if (sc->sc_ht)
			setbit(bands, IEEE80211_MODE_11NG);
#endif
		ieee80211_add_channel_list_2ghz(chans, maxchans, nchans,
		    ar_chans, 14, bands, 0);
	}
	if (sc->eeprom.baseEepHeader.opCapFlags & AR5416_OPFLAGS_11A) {
		setbit(bands, IEEE80211_MODE_11A);
		ieee80211_add_channel_list_5ghz(chans, maxchans, nchans,
                    &ar_chans[14], nitems(ar_chans) - 14, bands, 0);
	}
}

int
otus_load_firmware(struct otus_softc *sc, const char *name, uint32_t addr)
{
	usb_device_request_t req;
	char *ptr;
	const struct firmware *fw;
	int mlen, error, size;

	error = 0;

	/* Read firmware image from the filesystem. */
	if ((fw = firmware_get(name)) == NULL) {
		device_printf(sc->sc_dev,
		    "%s: failed loadfirmware of file %s\n", __func__, name);
		return (ENXIO);
	}
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = AR_FW_DOWNLOAD;
	USETW(req.wIndex, 0);

	OTUS_LOCK(sc);

	/* XXX const */
	ptr = __DECONST(char *, fw->data);
	size = fw->datasize;
	addr >>= 8;
	while (size > 0) {
		mlen = MIN(size, 4096);

		USETW(req.wValue, addr);
		USETW(req.wLength, mlen);
		if (usbd_do_request_flags(sc->sc_udev, &sc->sc_mtx,
		    &req, ptr, 0, NULL, 250) != 0) {
			error = EIO;
			break;
		}
		addr += mlen >> 8;
		ptr  += mlen;
		size -= mlen;
	}

	OTUS_UNLOCK(sc);

	firmware_put(fw, FIRMWARE_UNLOAD);
	if (error != 0)
		device_printf(sc->sc_dev,
		    "%s: %s: error=%d\n", __func__, name, error);
	return error;
}

int
otus_open_pipes(struct otus_softc *sc)
{
#if 0
	int isize, error;
	int i;
#endif
	int error;

	OTUS_UNLOCK_ASSERT(sc);

	if ((error = otus_alloc_tx_cmd_list(sc)) != 0) {
		device_printf(sc->sc_dev,
		    "%s: could not allocate command xfer\n",
		    __func__);
		goto fail;
	}

	if ((error = otus_alloc_tx_list(sc)) != 0) {
		device_printf(sc->sc_dev, "%s: could not allocate Tx xfers\n",
		    __func__);
		goto fail;
	}

	if ((error = otus_alloc_rx_list(sc)) != 0) {
		device_printf(sc->sc_dev, "%s: could not allocate Rx xfers\n",
		    __func__);
		goto fail;
	}

	/* Enable RX transfers; needed for initial firmware messages */
	OTUS_LOCK(sc);
	usbd_transfer_start(sc->sc_xfer[OTUS_BULK_RX]);
	usbd_transfer_start(sc->sc_xfer[OTUS_BULK_IRQ]);
	OTUS_UNLOCK(sc);
	return 0;

fail:	otus_close_pipes(sc);
	return error;
}

void
otus_close_pipes(struct otus_softc *sc)
{

	OTUS_LOCK(sc);
	otus_free_tx_cmd_list(sc);
	otus_free_tx_list(sc);
	otus_free_rx_list(sc);
	OTUS_UNLOCK(sc);

	usbd_transfer_unsetup(sc->sc_xfer, OTUS_N_XFER);
}

static void
otus_free_cmd_list(struct otus_softc *sc, struct otus_tx_cmd cmd[], int ndata)
{
	int i;

	/* XXX TODO: someone has to have waken up waiters! */
	for (i = 0; i < ndata; i++) {
		struct otus_tx_cmd *dp = &cmd[i];

		if (dp->buf != NULL) {
			free(dp->buf, M_USBDEV);
			dp->buf = NULL;
		}
	}
}

static int
otus_alloc_cmd_list(struct otus_softc *sc, struct otus_tx_cmd cmd[],
    int ndata, int maxsz)
{
	int i, error;

	for (i = 0; i < ndata; i++) {
		struct otus_tx_cmd *dp = &cmd[i];
		dp->buf = malloc(maxsz, M_USBDEV, M_NOWAIT | M_ZERO);
		dp->odata = NULL;
		if (dp->buf == NULL) {
			device_printf(sc->sc_dev,
			    "could not allocate buffer\n");
			error = ENOMEM;
			goto fail;
		}
	}

	return (0);
fail:
	otus_free_cmd_list(sc, cmd, ndata);
	return (error);
}

static int
otus_alloc_tx_cmd_list(struct otus_softc *sc)
{
	int error, i;

	error = otus_alloc_cmd_list(sc, sc->sc_cmd, OTUS_CMD_LIST_COUNT,
	    OTUS_MAX_TXCMDSZ);
	if (error != 0)
		return (error);

	STAILQ_INIT(&sc->sc_cmd_active);
	STAILQ_INIT(&sc->sc_cmd_inactive);
	STAILQ_INIT(&sc->sc_cmd_pending);
	STAILQ_INIT(&sc->sc_cmd_waiting);

	for (i = 0; i < OTUS_CMD_LIST_COUNT; i++)
		STAILQ_INSERT_HEAD(&sc->sc_cmd_inactive, &sc->sc_cmd[i],
		    next_cmd);

	return (0);
}

static void
otus_free_tx_cmd_list(struct otus_softc *sc)
{

	/*
	 * XXX TODO: something needs to wake up any pending/sleeping
	 * waiters!
	 */
	STAILQ_INIT(&sc->sc_cmd_active);
	STAILQ_INIT(&sc->sc_cmd_inactive);
	STAILQ_INIT(&sc->sc_cmd_pending);
	STAILQ_INIT(&sc->sc_cmd_waiting);

	otus_free_cmd_list(sc, sc->sc_cmd, OTUS_CMD_LIST_COUNT);
}

static int
otus_alloc_list(struct otus_softc *sc, struct otus_data data[],
    int ndata, int maxsz)
{
	int i, error;

	for (i = 0; i < ndata; i++) {
		struct otus_data *dp = &data[i];
		dp->sc = sc;
		dp->m = NULL;
		dp->buf = malloc(maxsz, M_USBDEV, M_NOWAIT | M_ZERO);
		if (dp->buf == NULL) {
			device_printf(sc->sc_dev,
			    "could not allocate buffer\n");
			error = ENOMEM;
			goto fail;
		}
		dp->ni = NULL;
	}

	return (0);
fail:
	otus_free_list(sc, data, ndata);
	return (error);
}

static int
otus_alloc_rx_list(struct otus_softc *sc)
{
	int error, i;

	error = otus_alloc_list(sc, sc->sc_rx, OTUS_RX_LIST_COUNT,
	    OTUS_RXBUFSZ);
	if (error != 0)
		return (error);

	STAILQ_INIT(&sc->sc_rx_active);
	STAILQ_INIT(&sc->sc_rx_inactive);

	for (i = 0; i < OTUS_RX_LIST_COUNT; i++)
		STAILQ_INSERT_HEAD(&sc->sc_rx_inactive, &sc->sc_rx[i], next);

	return (0);
}

static int
otus_alloc_tx_list(struct otus_softc *sc)
{
	int error, i;

	error = otus_alloc_list(sc, sc->sc_tx, OTUS_TX_LIST_COUNT,
	    OTUS_TXBUFSZ);
	if (error != 0)
		return (error);

	STAILQ_INIT(&sc->sc_tx_inactive);

	for (i = 0; i != OTUS_N_XFER; i++) {
		STAILQ_INIT(&sc->sc_tx_active[i]);
		STAILQ_INIT(&sc->sc_tx_pending[i]);
	}

	for (i = 0; i < OTUS_TX_LIST_COUNT; i++) {
		STAILQ_INSERT_HEAD(&sc->sc_tx_inactive, &sc->sc_tx[i], next);
	}

	return (0);
}

static void
otus_free_tx_list(struct otus_softc *sc)
{
	int i;

	/* prevent further allocations from TX list(s) */
	STAILQ_INIT(&sc->sc_tx_inactive);

	for (i = 0; i != OTUS_N_XFER; i++) {
		STAILQ_INIT(&sc->sc_tx_active[i]);
		STAILQ_INIT(&sc->sc_tx_pending[i]);
	}

	otus_free_list(sc, sc->sc_tx, OTUS_TX_LIST_COUNT);
}

static void
otus_free_rx_list(struct otus_softc *sc)
{
	/* prevent further allocations from RX list(s) */
	STAILQ_INIT(&sc->sc_rx_inactive);
	STAILQ_INIT(&sc->sc_rx_active);

	otus_free_list(sc, sc->sc_rx, OTUS_RX_LIST_COUNT);
}

static void
otus_free_list(struct otus_softc *sc, struct otus_data data[], int ndata)
{
	int i;

	for (i = 0; i < ndata; i++) {
		struct otus_data *dp = &data[i];

		if (dp->buf != NULL) {
			free(dp->buf, M_USBDEV);
			dp->buf = NULL;
		}
		if (dp->ni != NULL) {
			ieee80211_free_node(dp->ni);
			dp->ni = NULL;
		}
	}
}

static struct otus_data *
_otus_getbuf(struct otus_softc *sc)
{
	struct otus_data *bf;

	bf = STAILQ_FIRST(&sc->sc_tx_inactive);
	if (bf != NULL)
		STAILQ_REMOVE_HEAD(&sc->sc_tx_inactive, next);
	else
		bf = NULL;
	/* XXX bzero? */
	return (bf);
}

static struct otus_data *
otus_getbuf(struct otus_softc *sc)
{
	struct otus_data *bf;

	OTUS_LOCK_ASSERT(sc);

	bf = _otus_getbuf(sc);
	return (bf);
}

static void
otus_freebuf(struct otus_softc *sc, struct otus_data *bf)
{

	OTUS_LOCK_ASSERT(sc);
	STAILQ_INSERT_TAIL(&sc->sc_tx_inactive, bf, next);
}

static struct otus_tx_cmd *
_otus_get_txcmd(struct otus_softc *sc)
{
	struct otus_tx_cmd *bf;

	bf = STAILQ_FIRST(&sc->sc_cmd_inactive);
	if (bf != NULL)
		STAILQ_REMOVE_HEAD(&sc->sc_cmd_inactive, next_cmd);
	else
		bf = NULL;
	return (bf);
}

static struct otus_tx_cmd *
otus_get_txcmd(struct otus_softc *sc)
{
	struct otus_tx_cmd *bf;

	OTUS_LOCK_ASSERT(sc);

	bf = _otus_get_txcmd(sc);
	if (bf == NULL) {
		device_printf(sc->sc_dev, "%s: no tx cmd buffers\n",
		    __func__);
	}
	return (bf);
}

static void
otus_free_txcmd(struct otus_softc *sc, struct otus_tx_cmd *bf)
{

	OTUS_LOCK_ASSERT(sc);
	STAILQ_INSERT_TAIL(&sc->sc_cmd_inactive, bf, next_cmd);
}

void
otus_next_scan(void *arg, int pending)
{
#if 0
	struct otus_softc *sc = arg;

	if (usbd_is_dying(sc->sc_udev))
		return;

	usbd_ref_incr(sc->sc_udev);

	if (sc->sc_ic.ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(&sc->sc_ic.ic_if);

	usbd_ref_decr(sc->sc_udev);
#endif
}

int
otus_newstate(struct ieee80211vap *vap, enum ieee80211_state nstate, int arg)
{
	struct otus_vap *uvp = OTUS_VAP(vap);
	struct ieee80211com *ic = vap->iv_ic;
	struct otus_softc *sc = ic->ic_softc;
	enum ieee80211_state ostate;

	ostate = vap->iv_state;
	OTUS_DPRINTF(sc, OTUS_DEBUG_STATE, "%s: %s -> %s\n", __func__,
	    ieee80211_state_name[ostate],
	    ieee80211_state_name[nstate]);

	IEEE80211_UNLOCK(ic);

	OTUS_LOCK(sc);

	/* XXX TODO: more fleshing out! */

	switch (nstate) {
	case IEEE80211_S_INIT:
		otus_set_operating_mode(sc);
		otus_set_rx_filter(sc);
		break;
	case IEEE80211_S_RUN:
		if (ic->ic_opmode == IEEE80211_M_STA) {
			otus_updateslot(sc);
			otus_set_operating_mode(sc);
			otus_set_rx_filter(sc);

			/* Start calibration timer. */
			taskqueue_enqueue_timeout(taskqueue_thread,
			    &sc->calib_to, hz);
		}
		break;
	default:
		break;
	}

	/* XXX TODO: calibration? */

	sc->sc_led_newstate(sc);

	OTUS_UNLOCK(sc);
	IEEE80211_LOCK(ic);
	return (uvp->newstate(vap, nstate, arg));
}

int
otus_cmd(struct otus_softc *sc, uint8_t code, const void *idata, int ilen,
    void *odata, int odatalen)
{
	struct otus_tx_cmd *cmd;
	struct ar_cmd_hdr *hdr;
	int xferlen, error;

	OTUS_LOCK_ASSERT(sc);

	/* Always bulk-out a multiple of 4 bytes. */
	xferlen = (sizeof (*hdr) + ilen + 3) & ~3;
	if (xferlen > OTUS_MAX_TXCMDSZ) {
		device_printf(sc->sc_dev, "%s: command (0x%02x) size (%d) > %d\n",
		    __func__,
		    code,
		    xferlen,
		    OTUS_MAX_TXCMDSZ);
		return (EIO);
	}

	cmd = otus_get_txcmd(sc);
	if (cmd == NULL) {
		device_printf(sc->sc_dev, "%s: failed to get buf\n",
		    __func__);
		return (EIO);
	}

	hdr = (struct ar_cmd_hdr *)cmd->buf;
	hdr->code  = code;
	hdr->len   = ilen;
	hdr->token = ++sc->token;	/* Don't care about endianness. */
	cmd->token = hdr->token;
	/* XXX TODO: check max cmd length? */
	memcpy((uint8_t *)&hdr[1], idata, ilen);

	OTUS_DPRINTF(sc, OTUS_DEBUG_CMD,
	    "%s: sending command code=0x%02x len=%d token=%d\n",
	    __func__, code, ilen, hdr->token);

	cmd->odata = odata;
	cmd->odatalen = odatalen;
	cmd->buflen = xferlen;

	/* Queue the command to the endpoint */
	STAILQ_INSERT_TAIL(&sc->sc_cmd_pending, cmd, next_cmd);
	usbd_transfer_start(sc->sc_xfer[OTUS_BULK_CMD]);

	/* Sleep on the command; wait for it to complete */
	error = msleep(cmd, &sc->sc_mtx, PCATCH, "otuscmd", hz);

	/*
	 * At this point we don't own cmd any longer; it'll be
	 * freed by the cmd bulk path or the RX notification
	 * path.  If the data is made available then it'll be copied
	 * to the caller.  All that is left to do is communicate
	 * status back to the caller.
	 */
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "%s: timeout waiting for command 0x%02x reply\n",
		    __func__, code);
	}
	return error;
}

void
otus_write(struct otus_softc *sc, uint32_t reg, uint32_t val)
{

	OTUS_LOCK_ASSERT(sc);

	sc->write_buf[sc->write_idx].reg = htole32(reg);
	sc->write_buf[sc->write_idx].val = htole32(val);

	if (++sc->write_idx > (AR_MAX_WRITE_IDX-1))
		(void)otus_write_barrier(sc);
}

int
otus_write_barrier(struct otus_softc *sc)
{
	int error;

	OTUS_LOCK_ASSERT(sc);

	if (sc->write_idx == 0)
		return 0;	/* Nothing to flush. */

	OTUS_DPRINTF(sc, OTUS_DEBUG_REGIO, "%s: called; %d updates\n",
	    __func__,
	    sc->write_idx);

	error = otus_cmd(sc, AR_CMD_WREG, sc->write_buf,
	    sizeof (sc->write_buf[0]) * sc->write_idx, NULL, 0);
	sc->write_idx = 0;
	return error;
}

static struct ieee80211_node *
otus_node_alloc(struct ieee80211vap *vap, const uint8_t mac[IEEE80211_ADDR_LEN])
{

	return malloc(sizeof (struct otus_node), M_80211_NODE,
	    M_NOWAIT | M_ZERO);
}

#if 0
int
otus_media_change(struct ifnet *ifp)
{
	struct otus_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t rate, ridx;
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if (ic->ic_fixed_rate != -1) {
		rate = ic->ic_sup_rates[ic->ic_curmode].
		    rs_rates[ic->ic_fixed_rate] & IEEE80211_RATE_VAL;
		for (ridx = 0; ridx <= OTUS_RIDX_MAX; ridx++)
			if (otus_rates[ridx].rate == rate)
				break;
		sc->fixed_ridx = ridx;
	}

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		error = otus_init(sc);

	return error;
}
#endif

int
otus_read_eeprom(struct otus_softc *sc)
{
	uint32_t regs[8], reg;
	uint8_t *eep;
	int i, j, error;

	OTUS_LOCK_ASSERT(sc);

	/* Read EEPROM by blocks of 32 bytes. */
	eep = (uint8_t *)&sc->eeprom;
	reg = AR_EEPROM_OFFSET;
	for (i = 0; i < sizeof (sc->eeprom) / 32; i++) {
		for (j = 0; j < 8; j++, reg += 4)
			regs[j] = htole32(reg);
		error = otus_cmd(sc, AR_CMD_RREG, regs, sizeof regs, eep, 32);
		if (error != 0)
			break;
		eep += 32;
	}
	return error;
}

void
otus_newassoc(struct ieee80211_node *ni, int isnew)
{
	struct ieee80211com *ic = ni->ni_ic;
	struct otus_softc *sc = ic->ic_softc;
	struct otus_node *on = OTUS_NODE(ni);

	OTUS_DPRINTF(sc, OTUS_DEBUG_STATE, "new assoc isnew=%d addr=%s\n",
	    isnew, ether_sprintf(ni->ni_macaddr));

	on->tx_done = 0;
	on->tx_err = 0;
	on->tx_retries = 0;
}

static void
otus_cmd_handle_response(struct otus_softc *sc, struct ar_cmd_hdr *hdr)
{
	struct otus_tx_cmd *cmd;

	OTUS_LOCK_ASSERT(sc);

	OTUS_DPRINTF(sc, OTUS_DEBUG_CMDDONE,
	    "%s: received reply code=0x%02x len=%d token=%d\n",
	    __func__,
	    hdr->code, hdr->len, hdr->token);

	/*
	 * Walk the list, freeing items that aren't ours,
	 * stopping when we hit our token.
	 */
	while ((cmd = STAILQ_FIRST(&sc->sc_cmd_waiting)) != NULL) {
		STAILQ_REMOVE_HEAD(&sc->sc_cmd_waiting, next_cmd);
		OTUS_DPRINTF(sc, OTUS_DEBUG_CMDDONE,
		    "%s: cmd=%p; hdr.token=%d, cmd.token=%d\n",
		    __func__,
		    cmd,
		    (int) hdr->token,
		    (int) cmd->token);
		if (hdr->token == cmd->token) {
			/* Copy answer into caller's supplied buffer. */
			if (cmd->odata != NULL) {
				if (hdr->len != cmd->odatalen) {
					device_printf(sc->sc_dev,
					    "%s: code 0x%02x, len=%d, olen=%d\n",
					    __func__,
					    (int) hdr->code,
					    (int) hdr->len,
					    (int) cmd->odatalen);
				}
				memcpy(cmd->odata, &hdr[1],
				    MIN(cmd->odatalen, hdr->len));
			}
			wakeup(cmd);
		}

		STAILQ_INSERT_TAIL(&sc->sc_cmd_inactive, cmd, next_cmd);
	}
}

void
otus_cmd_rxeof(struct otus_softc *sc, uint8_t *buf, int len)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ar_cmd_hdr *hdr;

	OTUS_LOCK_ASSERT(sc);

	if (__predict_false(len < sizeof (*hdr))) {
		OTUS_DPRINTF(sc, OTUS_DEBUG_CMDDONE,
		    "cmd too small %d\n", len);
		return;
	}
	hdr = (struct ar_cmd_hdr *)buf;
	if (__predict_false(sizeof (*hdr) + hdr->len > len ||
	    sizeof (*hdr) + hdr->len > 64)) {
		OTUS_DPRINTF(sc, OTUS_DEBUG_CMDDONE,
		    "cmd too large %d\n", hdr->len);
		return;
	}

	OTUS_DPRINTF(sc, OTUS_DEBUG_RXDONE,
	    "%s: code=%.02x\n",
	    __func__,
	    hdr->code);

	/*
	 * This has to reach into the cmd queue "waiting for
	 * an RX response" list, grab the head entry and check
	 * if we need to wake anyone up.
	 */
	if ((hdr->code & 0xc0) != 0xc0) {
		otus_cmd_handle_response(sc, hdr);
		return;
	}

	/* Received unsolicited notification. */
	switch (hdr->code & 0x3f) {
	case AR_EVT_BEACON:
		break;
	case AR_EVT_TX_COMP:
	{
		struct ar_evt_tx_comp *tx = (struct ar_evt_tx_comp *)&hdr[1];
		struct ieee80211_node *ni;

		ni = ieee80211_find_node(&ic->ic_sta, tx->macaddr);
		if (ni == NULL) {
			device_printf(sc->sc_dev,
			    "%s: txcomp on unknown node (%s)\n",
			    __func__,
			    ether_sprintf(tx->macaddr));
			break;
		}

		OTUS_DPRINTF(sc, OTUS_DEBUG_TXCOMP,
		    "tx completed %s status=%d phy=0x%x\n",
		    ether_sprintf(tx->macaddr), le16toh(tx->status),
		    le32toh(tx->phy));

		switch (le16toh(tx->status)) {
		case AR_TX_STATUS_COMP:
#if 0
			ackfailcnt = 0;
			ieee80211_ratectl_tx_complete(ni->ni_vap, ni,
			    IEEE80211_RATECTL_TX_SUCCESS, &ackfailcnt, NULL);
#endif
			/*
			 * We don't get the above; only error notifications.
			 * Sigh.  So, don't worry about this.
			 */
			break;
		case AR_TX_STATUS_RETRY_COMP:
			OTUS_NODE(ni)->tx_retries++;
			break;
		case AR_TX_STATUS_FAILED:
			OTUS_NODE(ni)->tx_err++;
			break;
		}
		ieee80211_free_node(ni);
		break;
	}
	case AR_EVT_TBTT:
		break;
	case AR_EVT_DO_BB_RESET:
		/*
		 * This is "tell driver to reset baseband" from ar9170-fw.
		 *
		 * I'm not sure what we should do here, so I'm going to
		 * fall through; it gets generated when RTSRetryCnt internally
		 * reaches '5' - I guess the firmware authors thought that
		 * meant that the BB may have gone deaf or something.
		 */
	default:
		device_printf(sc->sc_dev,
		    "%s: received notification code=0x%02x len=%d\n",
		    __func__,
		    hdr->code, hdr->len);
	}
}

void
otus_sub_rxeof(struct otus_softc *sc, uint8_t *buf, int len, struct mbufq *rxq)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_rx_stats rxs;
#if 0
	struct ieee80211_node *ni;
#endif
	struct ar_rx_tail *tail;
	struct ieee80211_frame *wh;
	struct mbuf *m;
	uint8_t *plcp;
//	int s;
	int mlen;

	if (__predict_false(len < AR_PLCP_HDR_LEN)) {
		OTUS_DPRINTF(sc, OTUS_DEBUG_RXDONE,
		    "sub-xfer too short %d\n", len);
		return;
	}
	plcp = buf;

	/* All bits in the PLCP header are set to 1 for non-MPDU. */
	if (memcmp(plcp, AR_PLCP_HDR_INTR, AR_PLCP_HDR_LEN) == 0) {
		otus_cmd_rxeof(sc, plcp + AR_PLCP_HDR_LEN,
		    len - AR_PLCP_HDR_LEN);
		return;
	}

	/* Received MPDU. */
	if (__predict_false(len < AR_PLCP_HDR_LEN + sizeof (*tail))) {
		OTUS_DPRINTF(sc, OTUS_DEBUG_RXDONE, "MPDU too short %d\n", len);
		counter_u64_add(ic->ic_ierrors, 1);
		return;
	}
	tail = (struct ar_rx_tail *)(plcp + len - sizeof (*tail));

	/* Discard error frames; don't discard BAD_RA (eg monitor mode); let net80211 do that */
	if (__predict_false((tail->error & ~AR_RX_ERROR_BAD_RA) != 0)) {
		OTUS_DPRINTF(sc, OTUS_DEBUG_RXDONE, "error frame 0x%02x\n", tail->error);
		if (tail->error & AR_RX_ERROR_FCS) {
			OTUS_DPRINTF(sc, OTUS_DEBUG_RXDONE, "bad FCS\n");
		} else if (tail->error & AR_RX_ERROR_MMIC) {
			/* Report Michael MIC failures to net80211. */
#if 0
			ieee80211_notify_michael_failure(ni->ni_vap, wh, keyidx);
#endif
			device_printf(sc->sc_dev, "%s: MIC failure\n", __func__);
		}
		counter_u64_add(ic->ic_ierrors, 1);
		return;
	}
	/* Compute MPDU's length. */
	mlen = len - AR_PLCP_HDR_LEN - sizeof (*tail);
	/* Make sure there's room for an 802.11 header + FCS. */
	if (__predict_false(mlen < IEEE80211_MIN_LEN)) {
		counter_u64_add(ic->ic_ierrors, 1);
		return;
	}
	mlen -= IEEE80211_CRC_LEN;	/* strip 802.11 FCS */

	wh = (struct ieee80211_frame *)(plcp + AR_PLCP_HDR_LEN);

	/*
	 * TODO: I see > 2KiB buffers in this path; is it A-MSDU or something?
	 */
	m = m_get2(mlen, M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL) {
		device_printf(sc->sc_dev, "%s: failed m_get2() (mlen=%d)\n", __func__, mlen);
		counter_u64_add(ic->ic_ierrors, 1);
		return;
	}

	/* Finalize mbuf. */
	memcpy(mtod(m, uint8_t *), wh, mlen);
	m->m_pkthdr.len = m->m_len = mlen;

#if 0
	if (__predict_false(sc->sc_drvbpf != NULL)) {
		struct otus_rx_radiotap_header *tap = &sc->sc_rxtap;
		struct mbuf mb;

		tap->wr_flags = 0;
		tap->wr_antsignal = tail->rssi;
		tap->wr_rate = 2;	/* In case it can't be found below. */
		switch (tail->status & AR_RX_STATUS_MT_MASK) {
		case AR_RX_STATUS_MT_CCK:
			switch (plcp[0]) {
			case  10: tap->wr_rate =   2; break;
			case  20: tap->wr_rate =   4; break;
			case  55: tap->wr_rate =  11; break;
			case 110: tap->wr_rate =  22; break;
			}
			if (tail->status & AR_RX_STATUS_SHPREAMBLE)
				tap->wr_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
			break;
		case AR_RX_STATUS_MT_OFDM:
			switch (plcp[0] & 0xf) {
			case 0xb: tap->wr_rate =  12; break;
			case 0xf: tap->wr_rate =  18; break;
			case 0xa: tap->wr_rate =  24; break;
			case 0xe: tap->wr_rate =  36; break;
			case 0x9: tap->wr_rate =  48; break;
			case 0xd: tap->wr_rate =  72; break;
			case 0x8: tap->wr_rate =  96; break;
			case 0xc: tap->wr_rate = 108; break;
			}
			break;
		}
		mb.m_data = (caddr_t)tap;
		mb.m_next = m;
		mb.m_nextpkt = NULL;
		mb.m_type = 0;
		mb.m_flags = 0;
		bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_IN);
	}
#endif

	/* Add RSSI/NF to this mbuf */
	bzero(&rxs, sizeof(rxs));
	rxs.r_flags = IEEE80211_R_NF | IEEE80211_R_RSSI;
	rxs.c_nf = sc->sc_nf[0];	/* XXX chain 0 != combined rssi/nf */
	rxs.c_rssi = tail->rssi;
	/* XXX TODO: add MIMO RSSI/NF as well */
	if (ieee80211_add_rx_params(m, &rxs) == 0) {
		counter_u64_add(ic->ic_ierrors, 1);
		return;
	}

	/* XXX make a method */
	STAILQ_INSERT_TAIL(&rxq->mq_head, m, m_stailqpkt);

#if 0
	OTUS_UNLOCK(sc);
	ni = ieee80211_find_rxnode(ic, wh);
	rxi.rxi_flags = 0;
	rxi.rxi_rssi = tail->rssi;
	rxi.rxi_tstamp = 0;	/* unused */
	ieee80211_input(ifp, m, ni, &rxi);

	/* Node is no longer needed. */
	ieee80211_release_node(ic, ni);
	OTUS_LOCK(sc);
#endif
}

static void
otus_rxeof(struct usb_xfer *xfer, struct otus_data *data, struct mbufq *rxq)
{
	struct otus_softc *sc = usbd_xfer_softc(xfer);
	caddr_t buf = data->buf;
	struct ar_rx_head *head;
	uint16_t hlen;
	int len;

	usbd_xfer_status(xfer, &len, NULL, NULL, NULL);

	while (len >= sizeof (*head)) {
		head = (struct ar_rx_head *)buf;
		if (__predict_false(head->tag != htole16(AR_RX_HEAD_TAG))) {
			OTUS_DPRINTF(sc, OTUS_DEBUG_RXDONE,
			    "tag not valid 0x%x\n", le16toh(head->tag));
			break;
		}
		hlen = le16toh(head->len);
		if (__predict_false(sizeof (*head) + hlen > len)) {
			OTUS_DPRINTF(sc, OTUS_DEBUG_RXDONE,
			    "xfer too short %d/%d\n", len, hlen);
			break;
		}
		/* Process sub-xfer. */
		otus_sub_rxeof(sc, (uint8_t *)&head[1], hlen, rxq);

		/* Next sub-xfer is aligned on a 32-bit boundary. */
		hlen = (sizeof (*head) + hlen + 3) & ~3;
		buf += hlen;
		len -= hlen;
	}
}

static void
otus_bulk_rx_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct otus_softc *sc = usbd_xfer_softc(xfer);
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	struct mbuf *m;
	struct mbufq scrx;
	struct otus_data *data;

	OTUS_LOCK_ASSERT(sc);

	mbufq_init(&scrx, 1024);

#if 0
	device_printf(sc->sc_dev, "%s: called; state=%d; error=%d\n",
	    __func__,
	    USB_GET_STATE(xfer),
	    error);
#endif

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		data = STAILQ_FIRST(&sc->sc_rx_active);
		if (data == NULL)
			goto tr_setup;
		STAILQ_REMOVE_HEAD(&sc->sc_rx_active, next);
		otus_rxeof(xfer, data, &scrx);
		STAILQ_INSERT_TAIL(&sc->sc_rx_inactive, data, next);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		/*
		 * XXX TODO: what if sc_rx isn't empty, but data
		 * is empty?  Then we leak mbufs.
		 */
		data = STAILQ_FIRST(&sc->sc_rx_inactive);
		if (data == NULL) {
			//KASSERT(m == NULL, ("mbuf isn't NULL"));
			return;
		}
		STAILQ_REMOVE_HEAD(&sc->sc_rx_inactive, next);
		STAILQ_INSERT_TAIL(&sc->sc_rx_active, data, next);
		usbd_xfer_set_frame_data(xfer, 0, data->buf,
		    usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		/*
		 * To avoid LOR we should unlock our private mutex here to call
		 * ieee80211_input() because here is at the end of a USB
		 * callback and safe to unlock.
		 */
		OTUS_UNLOCK(sc);
		while ((m = mbufq_dequeue(&scrx)) != NULL) {
			wh = mtod(m, struct ieee80211_frame *);
			ni = ieee80211_find_rxnode(ic,
			    (struct ieee80211_frame_min *)wh);
			if (ni != NULL) {
				if (ni->ni_flags & IEEE80211_NODE_HT)
					m->m_flags |= M_AMPDU;
				(void)ieee80211_input_mimo(ni, m);
				ieee80211_free_node(ni);
			} else
				(void)ieee80211_input_mimo_all(ic, m);
		}
#ifdef	IEEE80211_SUPPORT_SUPERG
		ieee80211_ff_age_all(ic, 100);
#endif
		OTUS_LOCK(sc);
		break;
	default:
		/* needs it to the inactive queue due to a error. */
		data = STAILQ_FIRST(&sc->sc_rx_active);
		if (data != NULL) {
			STAILQ_REMOVE_HEAD(&sc->sc_rx_active, next);
			STAILQ_INSERT_TAIL(&sc->sc_rx_inactive, data, next);
		}
		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			counter_u64_add(ic->ic_ierrors, 1);
			goto tr_setup;
		}
		break;
	}
}

static void
otus_txeof(struct usb_xfer *xfer, struct otus_data *data)
{
	struct otus_softc *sc = usbd_xfer_softc(xfer);

	OTUS_DPRINTF(sc, OTUS_DEBUG_TXDONE,
	    "%s: called; data=%p\n", __func__, data);

	OTUS_LOCK_ASSERT(sc);

	if (sc->sc_tx_n_active == 0) {
		device_printf(sc->sc_dev,
		    "%s: completed but tx_active=0\n",
		    __func__);
	} else {
		sc->sc_tx_n_active--;
	}

	if (data->m) {
		/* XXX status? */
		/* XXX we get TX status via the RX path.. */
		ieee80211_tx_complete(data->ni, data->m, 0);
		data->m = NULL;
		data->ni = NULL;
	}
}

static void
otus_txcmdeof(struct usb_xfer *xfer, struct otus_tx_cmd *cmd)
{
	struct otus_softc *sc = usbd_xfer_softc(xfer);

	OTUS_LOCK_ASSERT(sc);

	OTUS_DPRINTF(sc, OTUS_DEBUG_CMDDONE,
	    "%s: called; data=%p; odata=%p\n",
	    __func__, cmd, cmd->odata);

	/*
	 * Non-response commands still need wakeup so the caller
	 * knows it was submitted and completed OK; response commands should
	 * wait until they're ACKed by the firmware with a response.
	 */
	if (cmd->odata) {
		STAILQ_INSERT_TAIL(&sc->sc_cmd_waiting, cmd, next_cmd);
	} else {
		wakeup(cmd);
		otus_free_txcmd(sc, cmd);
	}
}

static void
otus_bulk_tx_callback(struct usb_xfer *xfer, usb_error_t error)
{
	uint8_t which = OTUS_BULK_TX;
	struct otus_softc *sc = usbd_xfer_softc(xfer);
	struct ieee80211com *ic = &sc->sc_ic;
	struct otus_data *data;

	OTUS_LOCK_ASSERT(sc);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		data = STAILQ_FIRST(&sc->sc_tx_active[which]);
		if (data == NULL)
			goto tr_setup;
		OTUS_DPRINTF(sc, OTUS_DEBUG_TXDONE,
		    "%s: transfer done %p\n", __func__, data);
		STAILQ_REMOVE_HEAD(&sc->sc_tx_active[which], next);
		otus_txeof(xfer, data);
		otus_freebuf(sc, data);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		data = STAILQ_FIRST(&sc->sc_tx_pending[which]);
		if (data == NULL) {
			OTUS_DPRINTF(sc, OTUS_DEBUG_XMIT,
			    "%s: empty pending queue sc %p\n", __func__, sc);
			sc->sc_tx_n_active = 0;
			goto finish;
		}
		STAILQ_REMOVE_HEAD(&sc->sc_tx_pending[which], next);
		STAILQ_INSERT_TAIL(&sc->sc_tx_active[which], data, next);
		usbd_xfer_set_frame_data(xfer, 0, data->buf, data->buflen);
		OTUS_DPRINTF(sc, OTUS_DEBUG_XMIT,
		    "%s: submitting transfer %p\n", __func__, data);
		usbd_transfer_submit(xfer);
		sc->sc_tx_n_active++;
		break;
	default:
		data = STAILQ_FIRST(&sc->sc_tx_active[which]);
		if (data != NULL) {
			STAILQ_REMOVE_HEAD(&sc->sc_tx_active[which], next);
			otus_txeof(xfer, data);
			otus_freebuf(sc, data);
		}
		counter_u64_add(ic->ic_oerrors, 1);

		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}

finish:
#ifdef	IEEE80211_SUPPORT_SUPERG
	/*
	 * If the TX active queue drops below a certain
	 * threshold, ensure we age fast-frames out so they're
	 * transmitted.
	 */
	if (sc->sc_tx_n_active < 2) {
		/* XXX ew - net80211 should defer this for us! */
		OTUS_UNLOCK(sc);
		ieee80211_ff_flush(ic, WME_AC_VO);
		ieee80211_ff_flush(ic, WME_AC_VI);
		ieee80211_ff_flush(ic, WME_AC_BE);
		ieee80211_ff_flush(ic, WME_AC_BK);
		OTUS_LOCK(sc);
	}
#endif
	/* Kick TX */
	otus_tx_start(sc);
}

static void
otus_bulk_cmd_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct otus_softc *sc = usbd_xfer_softc(xfer);
#if 0
	struct ieee80211com *ic = &sc->sc_ic;
#endif
	struct otus_tx_cmd *cmd;

	OTUS_LOCK_ASSERT(sc);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		cmd = STAILQ_FIRST(&sc->sc_cmd_active);
		if (cmd == NULL)
			goto tr_setup;
		OTUS_DPRINTF(sc, OTUS_DEBUG_CMDDONE,
		    "%s: transfer done %p\n", __func__, cmd);
		STAILQ_REMOVE_HEAD(&sc->sc_cmd_active, next_cmd);
		otus_txcmdeof(xfer, cmd);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		cmd = STAILQ_FIRST(&sc->sc_cmd_pending);
		if (cmd == NULL) {
			OTUS_DPRINTF(sc, OTUS_DEBUG_CMD,
			    "%s: empty pending queue sc %p\n", __func__, sc);
			return;
		}
		STAILQ_REMOVE_HEAD(&sc->sc_cmd_pending, next_cmd);
		STAILQ_INSERT_TAIL(&sc->sc_cmd_active, cmd, next_cmd);
		usbd_xfer_set_frame_data(xfer, 0, cmd->buf, cmd->buflen);
		OTUS_DPRINTF(sc, OTUS_DEBUG_CMD,
		    "%s: submitting transfer %p; buf=%p, buflen=%d\n", __func__, cmd, cmd->buf, cmd->buflen);
		usbd_transfer_submit(xfer);
		break;
	default:
		cmd = STAILQ_FIRST(&sc->sc_cmd_active);
		if (cmd != NULL) {
			STAILQ_REMOVE_HEAD(&sc->sc_cmd_active, next_cmd);
			otus_txcmdeof(xfer, cmd);
		}

		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

/*
 * This isn't used by carl9170; it however may be used by the
 * initial bootloader.
 */
static void
otus_bulk_irq_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct otus_softc *sc = usbd_xfer_softc(xfer);
	int actlen;
	int sumlen;

	usbd_xfer_status(xfer, &actlen, &sumlen, NULL, NULL);
	OTUS_DPRINTF(sc, OTUS_DEBUG_IRQ,
	    "%s: called; state=%d\n", __func__, USB_GET_STATE(xfer));

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		/*
		 * Read usb frame data, if any.
		 * "actlen" has the total length for all frames
		 * transferred.
		 */
		OTUS_DPRINTF(sc, OTUS_DEBUG_IRQ,
		    "%s: comp; %d bytes\n",
		    __func__,
		    actlen);
#if 0
		pc = usbd_xfer_get_frame(xfer, 0);
		otus_dump_usb_rx_page(sc, pc, actlen);
#endif
		/* XXX fallthrough */
	case USB_ST_SETUP:
		/*
		 * Setup xfer frame lengths/count and data
		 */
		OTUS_DPRINTF(sc, OTUS_DEBUG_IRQ, "%s: setup\n", __func__);
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default: /* Error */
		/*
		 * Print error message and clear stall
		 * for example.
		 */
		OTUS_DPRINTF(sc, OTUS_DEBUG_IRQ, "%s: ERROR?\n", __func__);
		break;
	}
}

/*
 * Map net80211 rate to hw rate for otus MAC/PHY.
 */
static uint8_t
otus_rate_to_hw_rate(struct otus_softc *sc, uint8_t rate)
{
	int is_2ghz;

	is_2ghz = !! (IEEE80211_IS_CHAN_2GHZ(sc->sc_ic.ic_curchan));

	switch (rate) {
	/* CCK */
	case 2:
		return (0x0);
	case 4:
		return (0x1);
	case 11:
		return (0x2);
	case 22:
		return (0x3);
	/* OFDM */
	case 12:
		return (0xb);
	case 18:
		return (0xf);
	case 24:
		return (0xa);
	case 36:
		return (0xe);
	case 48:
		return (0x9);
	case 72:
		return (0xd);
	case 96:
		return (0x8);
	case 108:
		return (0xc);
	default:
		device_printf(sc->sc_dev, "%s: unknown rate '%d'\n",
		    __func__, (int) rate);
	case 0:
		if (is_2ghz)
			return (0x0);	/* 1MB CCK */
		else
			return (0xb);	/* 6MB OFDM */

	/* XXX TODO: HT */
	}
}

static int
otus_hw_rate_is_ofdm(struct otus_softc *sc, uint8_t hw_rate)
{

	switch (hw_rate) {
	case 0x0:
	case 0x1:
	case 0x2:
	case 0x3:
		return (0);
	default:
		return (1);
	}
}


static void
otus_tx_update_ratectl(struct otus_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211_ratectl_tx_stats *txs = &sc->sc_txs;
	struct otus_node *on = OTUS_NODE(ni);

	txs->flags = IEEE80211_RATECTL_TX_STATS_NODE |
		     IEEE80211_RATECTL_TX_STATS_RETRIES;
	txs->ni = ni;
	txs->nframes = on->tx_done;
	txs->nsuccess = on->tx_done - on->tx_err;
	txs->nretries = on->tx_retries;

	ieee80211_ratectl_tx_update(ni->ni_vap, txs);
	on->tx_done = on->tx_err = on->tx_retries = 0;
}

/*
 * XXX TODO: support tx bpf parameters for configuration!
 *
 * Relevant pieces:
 *
 * ac = params->ibp_pri & 3;
 * rate = params->ibp_rate0;
 * params->ibp_flags & IEEE80211_BPF_NOACK
 * params->ibp_flags & IEEE80211_BPF_RTS
 * params->ibp_flags & IEEE80211_BPF_CTS
 * tx->rts_ntries = params->ibp_try1;
 * tx->data_ntries = params->ibp_try0;
 */
static int
otus_tx(struct otus_softc *sc, struct ieee80211_node *ni, struct mbuf *m,
    struct otus_data *data, const struct ieee80211_bpf_params *params)
{
	const struct ieee80211_txparam *tp = ni->ni_txparms;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	struct ar_tx_head *head;
	uint32_t phyctl;
	uint16_t macctl, qos;
	uint8_t qid, rate;
	int hasqos, xferlen, type, ismcast;

	wh = mtod(m, struct ieee80211_frame *);
	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_crypto_encap(ni, m);
		if (k == NULL) {
			device_printf(sc->sc_dev,
			    "%s: m=%p: ieee80211_crypto_encap returns NULL\n",
			    __func__,
			    m);
			return (ENOBUFS);
		}
		wh = mtod(m, struct ieee80211_frame *);
	}

	/* Calculate transfer length; ensure data buffer is large enough */
	xferlen = sizeof (*head) + m->m_pkthdr.len;
	if (xferlen > OTUS_TXBUFSZ) {
		device_printf(sc->sc_dev,
		    "%s: 802.11 TX frame is %d bytes, max %d bytes\n",
		    __func__,
		    xferlen,
		    OTUS_TXBUFSZ);
		return (ENOBUFS);
	}

	hasqos = !! IEEE80211_QOS_HAS_SEQ(wh);

	if (hasqos) {
		uint8_t tid;
		qos = ((const struct ieee80211_qosframe *)wh)->i_qos[0];
		tid = qos & IEEE80211_QOS_TID;
		qid = TID_TO_WME_AC(tid);
	} else {
		qos = 0;
		qid = WME_AC_BE;
	}

	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;
	ismcast = IEEE80211_IS_MULTICAST(wh->i_addr1);

	/* Pickup a rate index. */
	if (params != NULL)
		rate = otus_rate_to_hw_rate(sc, params->ibp_rate0);
	else if (!!(m->m_flags & M_EAPOL) || type != IEEE80211_FC0_TYPE_DATA)
		rate = otus_rate_to_hw_rate(sc, tp->mgmtrate);
	else if (ismcast)
		rate = otus_rate_to_hw_rate(sc, tp->mcastrate);
	else if (tp->ucastrate != IEEE80211_FIXED_RATE_NONE)
		rate = otus_rate_to_hw_rate(sc, tp->ucastrate);
	else {
		(void) ieee80211_ratectl_rate(ni, NULL, 0);
		rate = otus_rate_to_hw_rate(sc, ni->ni_txrate);
	}

	phyctl = 0;
	macctl = AR_TX_MAC_BACKOFF | AR_TX_MAC_HW_DUR | AR_TX_MAC_QID(qid);

	/*
	 * XXX TODO: params for NOACK, ACK, RTS, CTS, etc
	 */
	if (ismcast ||
	    (hasqos && ((qos & IEEE80211_QOS_ACKPOLICY) ==
	     IEEE80211_QOS_ACKPOLICY_NOACK)))
		macctl |= AR_TX_MAC_NOACK;

	if (!ismcast) {
		if (m->m_pkthdr.len + IEEE80211_CRC_LEN >= vap->iv_rtsthreshold)
			macctl |= AR_TX_MAC_RTS;
		else if (ic->ic_flags & IEEE80211_F_USEPROT) {
			if (ic->ic_protmode == IEEE80211_PROT_CTSONLY)
				macctl |= AR_TX_MAC_CTS;
			else if (ic->ic_protmode == IEEE80211_PROT_RTSCTS)
				macctl |= AR_TX_MAC_RTS;
		}
	}

	phyctl |= AR_TX_PHY_MCS(rate);
	if (otus_hw_rate_is_ofdm(sc, rate)) {
		phyctl |= AR_TX_PHY_MT_OFDM;
		/* Always use all tx antennas for now, just to be safe */
		phyctl |= AR_TX_PHY_ANTMSK(sc->txmask);
	} else {	/* CCK */
		phyctl |= AR_TX_PHY_MT_CCK;
		phyctl |= AR_TX_PHY_ANTMSK(sc->txmask);
	}

	/* Update net80211 with the current counters */
	otus_tx_update_ratectl(sc, ni);

	/* Update rate control stats for frames that are ACK'ed. */
	if (!(macctl & AR_TX_MAC_NOACK))
		OTUS_NODE(ni)->tx_done++;


	/* Fill Tx descriptor. */
	head = (struct ar_tx_head *)data->buf;
	head->len = htole16(m->m_pkthdr.len + IEEE80211_CRC_LEN);
	head->macctl = htole16(macctl);
	head->phyctl = htole32(phyctl);

	m_copydata(m, 0, m->m_pkthdr.len, (caddr_t)&head[1]);

	data->buflen = xferlen;
	data->ni = ni;
	data->m = m;

	OTUS_DPRINTF(sc, OTUS_DEBUG_XMIT,
	    "%s: tx: m=%p; data=%p; len=%d mac=0x%04x phy=0x%08x rate=0x%02x, ni_txrate=%d\n",
	    __func__, m, data, le16toh(head->len), macctl, phyctl,
	    (int) rate, (int) ni->ni_txrate);

	/* Submit transfer */
	STAILQ_INSERT_TAIL(&sc->sc_tx_pending[OTUS_BULK_TX], data, next);
	usbd_transfer_start(sc->sc_xfer[OTUS_BULK_TX]);

	return 0;
}

int
otus_set_multi(struct otus_softc *sc)
{
	uint32_t lo, hi;
	struct ieee80211com *ic = &sc->sc_ic;
	int r;

	if (ic->ic_allmulti > 0 || ic->ic_promisc > 0 ||
	    ic->ic_opmode == IEEE80211_M_MONITOR) {
		lo = 0xffffffff;
		hi = 0xffffffff;
	} else {
		struct ieee80211vap *vap;
		struct ifnet *ifp;
		struct ifmultiaddr *ifma;

		lo = hi = 0;
		TAILQ_FOREACH(vap, &ic->ic_vaps, iv_next) {
			ifp = vap->iv_ifp;
			if_maddr_rlock(ifp);
			CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
				caddr_t dl;
				uint32_t val;

				dl = LLADDR((struct sockaddr_dl *) ifma->ifma_addr);
				val = le32dec(dl + 4);
				/* Get address byte 5 */
				val = val & 0x0000ff00;
				val = val >> 8;

				/* As per below, shift it >> 2 to get only 6 bits */
				val = val >> 2;
				if (val < 32)
					lo |= 1 << val;
				else
					hi |= 1 << (val - 32);
			}
			if_maddr_runlock(ifp);
		}
	}
#if 0
	/* XXX openbsd code */
	while (enm != NULL) {
		bit = enm->enm_addrlo[5] >> 2;
		if (bit < 32)
			lo |= 1 << bit;
		else
			hi |= 1 << (bit - 32);
		ETHER_NEXT_MULTI(step, enm);
	}
#endif

	hi |= 1U << 31;	/* Make sure the broadcast bit is set. */

	OTUS_LOCK(sc);
	otus_write(sc, AR_MAC_REG_GROUP_HASH_TBL_L, lo);
	otus_write(sc, AR_MAC_REG_GROUP_HASH_TBL_H, hi);
	r = otus_write_barrier(sc);
	/* XXX operating mode? filter? */
	OTUS_UNLOCK(sc);
	return (r);
}

static int
otus_updateedca(struct ieee80211com *ic)
{
	struct otus_softc *sc = ic->ic_softc;

	OTUS_LOCK(sc);
	/*
	 * XXX TODO: take temporary copy of EDCA information
	 * when scheduling this so we have a more time-correct view
	 * of things.
	 * XXX TODO: this can be done on the net80211 level
	 */
	otus_updateedca_locked(sc);
	OTUS_UNLOCK(sc);
	return (0);
}

static void
otus_updateedca_locked(struct otus_softc *sc)
{
#define EXP2(val)	((1 << (val)) - 1)
#define AIFS(val)	((val) * 9 + 10)
	struct chanAccParams chp;
	struct ieee80211com *ic = &sc->sc_ic;
	const struct wmeParams *edca;

	ieee80211_wme_ic_getparams(ic, &chp);

	OTUS_LOCK_ASSERT(sc);

	edca = chp.cap_wmeParams;

	/* Set CWmin/CWmax values. */
	otus_write(sc, AR_MAC_REG_AC0_CW,
	    EXP2(edca[WME_AC_BE].wmep_logcwmax) << 16 |
	    EXP2(edca[WME_AC_BE].wmep_logcwmin));
	otus_write(sc, AR_MAC_REG_AC1_CW,
	    EXP2(edca[WME_AC_BK].wmep_logcwmax) << 16 |
	    EXP2(edca[WME_AC_BK].wmep_logcwmin));
	otus_write(sc, AR_MAC_REG_AC2_CW,
	    EXP2(edca[WME_AC_VI].wmep_logcwmax) << 16 |
	    EXP2(edca[WME_AC_VI].wmep_logcwmin));
	otus_write(sc, AR_MAC_REG_AC3_CW,
	    EXP2(edca[WME_AC_VO].wmep_logcwmax) << 16 |
	    EXP2(edca[WME_AC_VO].wmep_logcwmin));
	otus_write(sc, AR_MAC_REG_AC4_CW,		/* Special TXQ. */
	    EXP2(edca[WME_AC_VO].wmep_logcwmax) << 16 |
	    EXP2(edca[WME_AC_VO].wmep_logcwmin));

	/* Set AIFSN values. */
	otus_write(sc, AR_MAC_REG_AC1_AC0_AIFS,
	    AIFS(edca[WME_AC_VI].wmep_aifsn) << 24 |
	    AIFS(edca[WME_AC_BK].wmep_aifsn) << 12 |
	    AIFS(edca[WME_AC_BE].wmep_aifsn));
	otus_write(sc, AR_MAC_REG_AC3_AC2_AIFS,
	    AIFS(edca[WME_AC_VO].wmep_aifsn) << 16 |	/* Special TXQ. */
	    AIFS(edca[WME_AC_VO].wmep_aifsn) <<  4 |
	    AIFS(edca[WME_AC_VI].wmep_aifsn) >>  8);

	/* Set TXOP limit. */
	otus_write(sc, AR_MAC_REG_AC1_AC0_TXOP,
	    edca[WME_AC_BK].wmep_txopLimit << 16 |
	    edca[WME_AC_BE].wmep_txopLimit);
	otus_write(sc, AR_MAC_REG_AC3_AC2_TXOP,
	    edca[WME_AC_VO].wmep_txopLimit << 16 |
	    edca[WME_AC_VI].wmep_txopLimit);

	/* XXX ACK policy? */

	(void)otus_write_barrier(sc);

#undef AIFS
#undef EXP2
}

static void
otus_updateslot(struct otus_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t slottime;

	OTUS_LOCK_ASSERT(sc);

	slottime = IEEE80211_GET_SLOTTIME(ic);
	otus_write(sc, AR_MAC_REG_SLOT_TIME, slottime << 10);
	(void)otus_write_barrier(sc);
}

int
otus_init_mac(struct otus_softc *sc)
{
	int error;

	OTUS_LOCK_ASSERT(sc);

	otus_write(sc, AR_MAC_REG_ACK_EXTENSION, 0x40);
	otus_write(sc, AR_MAC_REG_RETRY_MAX, 0);
	otus_write(sc, AR_MAC_REG_RX_THRESHOLD, 0xc1f80);
	otus_write(sc, AR_MAC_REG_RX_PE_DELAY, 0x70);
	otus_write(sc, AR_MAC_REG_EIFS_AND_SIFS, 0xa144000);
	otus_write(sc, AR_MAC_REG_SLOT_TIME, 9 << 10);
	otus_write(sc, AR_MAC_REG_TID_CFACK_CFEND_RATE, 0x19000000);
	/* NAV protects ACK only (in TXOP). */
	otus_write(sc, AR_MAC_REG_TXOP_DURATION, 0x201);
	/* Set beacon Tx power to 0x7. */
	otus_write(sc, AR_MAC_REG_BCN_HT1, 0x8000170);
	otus_write(sc, AR_MAC_REG_BACKOFF_PROTECT, 0x105);
	otus_write(sc, AR_MAC_REG_AMPDU_FACTOR, 0x10000a);

	otus_set_rx_filter(sc);

	otus_write(sc, AR_MAC_REG_BASIC_RATE, 0x150f);
	otus_write(sc, AR_MAC_REG_MANDATORY_RATE, 0x150f);
	otus_write(sc, AR_MAC_REG_RTS_CTS_RATE, 0x10b01bb);
	otus_write(sc, AR_MAC_REG_ACK_TPC, 0x4003c1e);

	/* Enable LED0 and LED1. */
	otus_write(sc, AR_GPIO_REG_PORT_TYPE, 0x3);
	otus_write(sc, AR_GPIO_REG_PORT_DATA, 0x3);
	/* Switch MAC to OTUS interface. */
	otus_write(sc, 0x1c3600, 0x3);
	otus_write(sc, AR_MAC_REG_AMPDU_RX_THRESH, 0xffff);
	otus_write(sc, AR_MAC_REG_MISC_680, 0xf00008);
	/* Disable Rx timeout (workaround). */
	otus_write(sc, AR_MAC_REG_RX_TIMEOUT, 0);

	/* Set USB Rx stream mode maximum frame number to 2. */
	otus_write(sc, 0x1e1110, 0x4);
	/* Set USB Rx stream mode timeout to 10us. */
	otus_write(sc, 0x1e1114, 0x80);

	/* Set clock frequency to 88/80MHz. */
	otus_write(sc, AR_PWR_REG_CLOCK_SEL, 0x73);
	/* Set WLAN DMA interrupt mode: generate intr per packet. */
	otus_write(sc, AR_MAC_REG_TXRX_MPI, 0x110011);
	otus_write(sc, AR_MAC_REG_FCS_SELECT, 0x4);
	otus_write(sc, AR_MAC_REG_TXOP_NOT_ENOUGH_INDICATION, 0x141e0f48);

	/* Disable HW decryption for now. */
	otus_write(sc, AR_MAC_REG_ENCRYPTION, 0x78);

	if ((error = otus_write_barrier(sc)) != 0)
		return error;

	/* Set default EDCA parameters. */
	otus_updateedca_locked(sc);

	return 0;
}

/*
 * Return default value for PHY register based on current operating mode.
 */
uint32_t
otus_phy_get_def(struct otus_softc *sc, uint32_t reg)
{
	int i;

	for (i = 0; i < nitems(ar5416_phy_regs); i++)
		if (AR_PHY(ar5416_phy_regs[i]) == reg)
			return sc->phy_vals[i];
	return 0;	/* Register not found. */
}

/*
 * Update PHY's programming based on vendor-specific data stored in EEPROM.
 * This is for FEM-type devices only.
 */
int
otus_set_board_values(struct otus_softc *sc, struct ieee80211_channel *c)
{
	const struct ModalEepHeader *eep;
	uint32_t tmp, offset;

	if (IEEE80211_IS_CHAN_5GHZ(c))
		eep = &sc->eeprom.modalHeader[0];
	else
		eep = &sc->eeprom.modalHeader[1];

	/* Offset of chain 2. */
	offset = 2 * 0x1000;

	tmp = le32toh(eep->antCtrlCommon);
	otus_write(sc, AR_PHY_SWITCH_COM, tmp);

	tmp = le32toh(eep->antCtrlChain[0]);
	otus_write(sc, AR_PHY_SWITCH_CHAIN_0, tmp);

	tmp = le32toh(eep->antCtrlChain[1]);
	otus_write(sc, AR_PHY_SWITCH_CHAIN_0 + offset, tmp);

	if (1 /* sc->sc_sco == AR_SCO_SCN */) {
		tmp = otus_phy_get_def(sc, AR_PHY_SETTLING);
		tmp &= ~(0x7f << 7);
		tmp |= (eep->switchSettling & 0x7f) << 7;
		otus_write(sc, AR_PHY_SETTLING, tmp);
	}

	tmp = otus_phy_get_def(sc, AR_PHY_DESIRED_SZ);
	tmp &= ~0xffff;
	tmp |= eep->pgaDesiredSize << 8 | eep->adcDesiredSize;
	otus_write(sc, AR_PHY_DESIRED_SZ, tmp);

	tmp = eep->txEndToXpaOff << 24 | eep->txEndToXpaOff << 16 |
	      eep->txFrameToXpaOn << 8 | eep->txFrameToXpaOn;
	otus_write(sc, AR_PHY_RF_CTL4, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_RF_CTL3);
	tmp &= ~(0xff << 16);
	tmp |= eep->txEndToRxOn << 16;
	otus_write(sc, AR_PHY_RF_CTL3, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_CCA);
	tmp &= ~(0x7f << 12);
	tmp |= (eep->thresh62 & 0x7f) << 12;
	otus_write(sc, AR_PHY_CCA, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_RXGAIN);
	tmp &= ~(0x3f << 12);
	tmp |= (eep->txRxAttenCh[0] & 0x3f) << 12;
	otus_write(sc, AR_PHY_RXGAIN, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_RXGAIN + offset);
	tmp &= ~(0x3f << 12);
	tmp |= (eep->txRxAttenCh[1] & 0x3f) << 12;
	otus_write(sc, AR_PHY_RXGAIN + offset, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_GAIN_2GHZ);
	tmp &= ~(0x3f << 18);
	tmp |= (eep->rxTxMarginCh[0] & 0x3f) << 18;
	if (IEEE80211_IS_CHAN_5GHZ(c)) {
		tmp &= ~(0xf << 10);
		tmp |= (eep->bswMargin[0] & 0xf) << 10;
	}
	otus_write(sc, AR_PHY_GAIN_2GHZ, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_GAIN_2GHZ + offset);
	tmp &= ~(0x3f << 18);
	tmp |= (eep->rxTxMarginCh[1] & 0x3f) << 18;
	otus_write(sc, AR_PHY_GAIN_2GHZ + offset, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_TIMING_CTRL4);
	tmp &= ~(0x3f << 5 | 0x1f);
	tmp |= (eep->iqCalICh[0] & 0x3f) << 5 | (eep->iqCalQCh[0] & 0x1f);
	otus_write(sc, AR_PHY_TIMING_CTRL4, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_TIMING_CTRL4 + offset);
	tmp &= ~(0x3f << 5 | 0x1f);
	tmp |= (eep->iqCalICh[1] & 0x3f) << 5 | (eep->iqCalQCh[1] & 0x1f);
	otus_write(sc, AR_PHY_TIMING_CTRL4 + offset, tmp);

	tmp = otus_phy_get_def(sc, AR_PHY_TPCRG1);
	tmp &= ~(0xf << 16);
	tmp |= (eep->xpd & 0xf) << 16;
	otus_write(sc, AR_PHY_TPCRG1, tmp);

	return otus_write_barrier(sc);
}

int
otus_program_phy(struct otus_softc *sc, struct ieee80211_channel *c)
{
	const uint32_t *vals;
	int error, i;

	/* Select PHY programming based on band and bandwidth. */
	if (IEEE80211_IS_CHAN_2GHZ(c))
		vals = ar5416_phy_vals_2ghz_20mhz;
	else
		vals = ar5416_phy_vals_5ghz_20mhz;
	for (i = 0; i < nitems(ar5416_phy_regs); i++)
		otus_write(sc, AR_PHY(ar5416_phy_regs[i]), vals[i]);
	sc->phy_vals = vals;

	if (sc->eeprom.baseEepHeader.deviceType == 0x80)	/* FEM */
		if ((error = otus_set_board_values(sc, c)) != 0)
			return error;

	/* Initial Tx power settings. */
	otus_write(sc, AR_PHY_POWER_TX_RATE_MAX, 0x7f);
	otus_write(sc, AR_PHY_POWER_TX_RATE1, 0x3f3f3f3f);
	otus_write(sc, AR_PHY_POWER_TX_RATE2, 0x3f3f3f3f);
	otus_write(sc, AR_PHY_POWER_TX_RATE3, 0x3f3f3f3f);
	otus_write(sc, AR_PHY_POWER_TX_RATE4, 0x3f3f3f3f);
	otus_write(sc, AR_PHY_POWER_TX_RATE5, 0x3f3f3f3f);
	otus_write(sc, AR_PHY_POWER_TX_RATE6, 0x3f3f3f3f);
	otus_write(sc, AR_PHY_POWER_TX_RATE7, 0x3f3f3f3f);
	otus_write(sc, AR_PHY_POWER_TX_RATE8, 0x3f3f3f3f);
	otus_write(sc, AR_PHY_POWER_TX_RATE9, 0x3f3f3f3f);

	if (IEEE80211_IS_CHAN_2GHZ(c))
		otus_write(sc, AR_PWR_REG_PLL_ADDAC, 0x5163);
	else
		otus_write(sc, AR_PWR_REG_PLL_ADDAC, 0x5143);

	return otus_write_barrier(sc);
}

static __inline uint8_t
otus_reverse_bits(uint8_t v)
{
	v = ((v >> 1) & 0x55) | ((v & 0x55) << 1);
	v = ((v >> 2) & 0x33) | ((v & 0x33) << 2);
	v = ((v >> 4) & 0x0f) | ((v & 0x0f) << 4);
	return v;
}

int
otus_set_rf_bank4(struct otus_softc *sc, struct ieee80211_channel *c)
{
	uint8_t chansel, d0, d1;
	uint16_t data;
	int error;

	OTUS_LOCK_ASSERT(sc);

	d0 = 0;
	if (IEEE80211_IS_CHAN_5GHZ(c)) {
		chansel = (c->ic_freq - 4800) / 5;
		if (chansel & 1)
			d0 |= AR_BANK4_AMODE_REFSEL(2);
		else
			d0 |= AR_BANK4_AMODE_REFSEL(1);
	} else {
		d0 |= AR_BANK4_AMODE_REFSEL(2);
		if (c->ic_freq == 2484) {	/* CH 14 */
			d0 |= AR_BANK4_BMODE_LF_SYNTH_FREQ;
			chansel = 10 + (c->ic_freq - 2274) / 5;
		} else
			chansel = 16 + (c->ic_freq - 2272) / 5;
		chansel <<= 2;
	}
	d0 |= AR_BANK4_ADDR(1) | AR_BANK4_CHUP;
	d1 = otus_reverse_bits(chansel);

	/* Write bits 0-4 of d0 and d1. */
	data = (d1 & 0x1f) << 5 | (d0 & 0x1f);
	otus_write(sc, AR_PHY(44), data);
	/* Write bits 5-7 of d0 and d1. */
	data = (d1 >> 5) << 5 | (d0 >> 5);
	otus_write(sc, AR_PHY(58), data);

	if ((error = otus_write_barrier(sc)) == 0)
		otus_delay_ms(sc, 10);
	return error;
}

void
otus_get_delta_slope(uint32_t coeff, uint32_t *exponent, uint32_t *mantissa)
{
#define COEFF_SCALE_SHIFT	24
	uint32_t exp, man;

	/* exponent = 14 - floor(log2(coeff)) */
	for (exp = 31; exp > 0; exp--)
		if (coeff & (1 << exp))
			break;
	KASSERT(exp != 0, ("exp"));
	exp = 14 - (exp - COEFF_SCALE_SHIFT);

	/* mantissa = floor(coeff * 2^exponent + 0.5) */
	man = coeff + (1 << (COEFF_SCALE_SHIFT - exp - 1));

	*mantissa = man >> (COEFF_SCALE_SHIFT - exp);
	*exponent = exp - 16;
#undef COEFF_SCALE_SHIFT
}

static int
otus_set_chan(struct otus_softc *sc, struct ieee80211_channel *c, int assoc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ar_cmd_frequency cmd;
	struct ar_rsp_frequency rsp;
	const uint32_t *vals;
	uint32_t coeff, exp, man, tmp;
	uint8_t code;
	int error, chan, i;

	error = 0;
	chan = ieee80211_chan2ieee(ic, c);

	OTUS_DPRINTF(sc, OTUS_DEBUG_RESET,
	    "setting channel %d (%dMHz)\n", chan, c->ic_freq);

	tmp = IEEE80211_IS_CHAN_2GHZ(c) ? 0x105 : 0x104;
	otus_write(sc, AR_MAC_REG_DYNAMIC_SIFS_ACK, tmp);
	if ((error = otus_write_barrier(sc)) != 0)
		goto finish;

	/* Disable BB Heavy Clip. */
	otus_write(sc, AR_PHY_HEAVY_CLIP_ENABLE, 0x200);
	if ((error = otus_write_barrier(sc)) != 0)
		goto finish;

	/* XXX Is that FREQ_START ? */
	error = otus_cmd(sc, AR_CMD_FREQ_STRAT, NULL, 0, NULL, 0);
	if (error != 0)
		goto finish;

	/* Reprogram PHY and RF on channel band or bandwidth changes. */
	if (sc->bb_reset || c->ic_flags != sc->sc_curchan->ic_flags) {
		OTUS_DPRINTF(sc, OTUS_DEBUG_RESET, "band switch\n");

		/* Cold/Warm reset BB/ADDA. */
		otus_write(sc, AR_PWR_REG_RESET, sc->bb_reset ? 0x800 : 0x400);
		if ((error = otus_write_barrier(sc)) != 0)
			goto finish;
		otus_write(sc, AR_PWR_REG_RESET, 0);
		if ((error = otus_write_barrier(sc)) != 0)
			goto finish;
		sc->bb_reset = 0;

		if ((error = otus_program_phy(sc, c)) != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not program PHY\n",
			    __func__);
			goto finish;
		}

		/* Select RF programming based on band. */
		if (IEEE80211_IS_CHAN_5GHZ(c))
			vals = ar5416_banks_vals_5ghz;
		else
			vals = ar5416_banks_vals_2ghz;
		for (i = 0; i < nitems(ar5416_banks_regs); i++)
			otus_write(sc, AR_PHY(ar5416_banks_regs[i]), vals[i]);
		if ((error = otus_write_barrier(sc)) != 0) {
			device_printf(sc->sc_dev,
			    "%s: could not program RF\n",
			    __func__);
			goto finish;
		}
		code = AR_CMD_RF_INIT;
	} else {
		code = AR_CMD_FREQUENCY;
	}

	if ((error = otus_set_rf_bank4(sc, c)) != 0)
		goto finish;

	tmp = (sc->txmask == 0x5) ? 0x340 : 0x240;
	otus_write(sc, AR_PHY_TURBO, tmp);
	if ((error = otus_write_barrier(sc)) != 0)
		goto finish;

	/* Send firmware command to set channel. */
	cmd.freq = htole32((uint32_t)c->ic_freq * 1000);
	cmd.dynht2040 = htole32(0);
	cmd.htena = htole32(1);
	/* Set Delta Slope (exponent and mantissa). */
	coeff = (100 << 24) / c->ic_freq;
	otus_get_delta_slope(coeff, &exp, &man);
	cmd.dsc_exp = htole32(exp);
	cmd.dsc_man = htole32(man);
	OTUS_DPRINTF(sc, OTUS_DEBUG_RESET,
	    "ds coeff=%u exp=%u man=%u\n", coeff, exp, man);
	/* For Short GI, coeff is 9/10 that of normal coeff. */
	coeff = (9 * coeff) / 10;
	otus_get_delta_slope(coeff, &exp, &man);
	cmd.dsc_shgi_exp = htole32(exp);
	cmd.dsc_shgi_man = htole32(man);
	OTUS_DPRINTF(sc, OTUS_DEBUG_RESET,
	    "ds shgi coeff=%u exp=%u man=%u\n", coeff, exp, man);
	/* Set wait time for AGC and noise calibration (100 or 200ms). */
	cmd.check_loop_count = assoc ? htole32(2000) : htole32(1000);
	OTUS_DPRINTF(sc, OTUS_DEBUG_RESET,
	    "%s\n", (code == AR_CMD_RF_INIT) ? "RF_INIT" : "FREQUENCY");
	error = otus_cmd(sc, code, &cmd, sizeof cmd, &rsp, sizeof(rsp));
	if (error != 0)
		goto finish;
	if ((rsp.status & htole32(AR_CAL_ERR_AGC | AR_CAL_ERR_NF_VAL)) != 0) {
		OTUS_DPRINTF(sc, OTUS_DEBUG_RESET,
		    "status=0x%x\n", le32toh(rsp.status));
		/* Force cold reset on next channel. */
		sc->bb_reset = 1;
	}
#ifdef USB_DEBUG
	if (otus_debug & OTUS_DEBUG_RESET) {
		device_printf(sc->sc_dev, "calibration status=0x%x\n",
		    le32toh(rsp.status));
		for (i = 0; i < 2; i++) {	/* 2 Rx chains */
			/* Sign-extend 9-bit NF values. */
			device_printf(sc->sc_dev,
			    "noisefloor chain %d=%d\n", i,
			    (((int32_t)le32toh(rsp.nf[i])) << 4) >> 23);
			device_printf(sc->sc_dev,
			    "noisefloor ext chain %d=%d\n", i,
			    ((int32_t)le32toh(rsp.nf_ext[i])) >> 23);
		}
	}
#endif
	for (i = 0; i < OTUS_NUM_CHAINS; i++) {
		sc->sc_nf[i] = ((((int32_t)le32toh(rsp.nf[i])) << 4) >> 23);
	}
	sc->sc_curchan = c;
finish:
	return (error);
}

#ifdef notyet
int
otus_set_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct otus_softc *sc = ic->ic_softc;
	struct otus_cmd_key cmd;

	/* Defer setting of WEP keys until interface is brought up. */
	if ((ic->ic_if.if_flags & (IFF_UP | IFF_RUNNING)) !=
	    (IFF_UP | IFF_RUNNING))
		return 0;

	/* Do it in a process context. */
	cmd.key = *k;
	cmd.associd = (ni != NULL) ? ni->ni_associd : 0;
	otus_do_async(sc, otus_set_key_cb, &cmd, sizeof cmd);
	return 0;
}

void
otus_set_key_cb(struct otus_softc *sc, void *arg)
{
	struct otus_cmd_key *cmd = arg;
	struct ieee80211_key *k = &cmd->key;
	struct ar_cmd_ekey key;
	uint16_t cipher;
	int error;

	memset(&key, 0, sizeof key);
	if (k->k_flags & IEEE80211_KEY_GROUP) {
		key.uid = htole16(k->k_id);
		IEEE80211_ADDR_COPY(key.macaddr, sc->sc_ic.ic_myaddr);
		key.macaddr[0] |= 0x80;
	} else {
		key.uid = htole16(OTUS_UID(cmd->associd));
		IEEE80211_ADDR_COPY(key.macaddr, ni->ni_macaddr);
	}
	key.kix = htole16(0);
	/* Map net80211 cipher to hardware. */
	switch (k->k_cipher) {
	case IEEE80211_CIPHER_WEP40:
		cipher = AR_CIPHER_WEP64;
		break;
	case IEEE80211_CIPHER_WEP104:
		cipher = AR_CIPHER_WEP128;
		break;
	case IEEE80211_CIPHER_TKIP:
		cipher = AR_CIPHER_TKIP;
		break;
	case IEEE80211_CIPHER_CCMP:
		cipher = AR_CIPHER_AES;
		break;
	default:
		return;
	}
	key.cipher = htole16(cipher);
	memcpy(key.key, k->k_key, MIN(k->k_len, 16));
	error = otus_cmd(sc, AR_CMD_EKEY, &key, sizeof key, NULL, 0);
	if (error != 0 || k->k_cipher != IEEE80211_CIPHER_TKIP)
		return;

	/* TKIP: set Tx/Rx MIC Key. */
	key.kix = htole16(1);
	memcpy(key.key, k->k_key + 16, 16);
	(void)otus_cmd(sc, AR_CMD_EKEY, &key, sizeof key, NULL, 0);
}

void
otus_delete_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct otus_softc *sc = ic->ic_softc;
	struct otus_cmd_key cmd;

	if (!(ic->ic_if.if_flags & IFF_RUNNING) ||
	    ic->ic_state != IEEE80211_S_RUN)
		return;	/* Nothing to do. */

	/* Do it in a process context. */
	cmd.key = *k;
	cmd.associd = (ni != NULL) ? ni->ni_associd : 0;
	otus_do_async(sc, otus_delete_key_cb, &cmd, sizeof cmd);
}

void
otus_delete_key_cb(struct otus_softc *sc, void *arg)
{
	struct otus_cmd_key *cmd = arg;
	struct ieee80211_key *k = &cmd->key;
	uint32_t uid;

	if (k->k_flags & IEEE80211_KEY_GROUP)
		uid = htole32(k->k_id);
	else
		uid = htole32(OTUS_UID(cmd->associd));
	(void)otus_cmd(sc, AR_CMD_DKEY, &uid, sizeof uid, NULL, 0);
}
#endif

/*
 * XXX TODO: check if we have to be doing any calibration in the host
 * or whether it's purely a firmware thing.
 */
void
otus_calibrate_to(void *arg, int pending)
{
#if 0
	struct otus_softc *sc = arg;

	device_printf(sc->sc_dev, "%s: called\n", __func__);
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	int s;

	if (usbd_is_dying(sc->sc_udev))
		return;

	usbd_ref_incr(sc->sc_udev);

	s = splnet();
	ni = ic->ic_bss;
	ieee80211_amrr_choose(&sc->amrr, ni, &((struct otus_node *)ni)->amn);
	splx(s);

	if (!usbd_is_dying(sc->sc_udev))
		timeout_add_sec(&sc->calib_to, 1);

	usbd_ref_decr(sc->sc_udev);
#endif
}

int
otus_set_bssid(struct otus_softc *sc, const uint8_t *bssid)
{

	OTUS_LOCK_ASSERT(sc);

	otus_write(sc, AR_MAC_REG_BSSID_L,
	    bssid[0] | bssid[1] << 8 | bssid[2] << 16 | bssid[3] << 24);
	otus_write(sc, AR_MAC_REG_BSSID_H,
	    bssid[4] | bssid[5] << 8);
	return otus_write_barrier(sc);
}

int
otus_set_macaddr(struct otus_softc *sc, const uint8_t *addr)
{
	OTUS_LOCK_ASSERT(sc);

	otus_write(sc, AR_MAC_REG_MAC_ADDR_L,
	    addr[0] | addr[1] << 8 | addr[2] << 16 | addr[3] << 24);
	otus_write(sc, AR_MAC_REG_MAC_ADDR_H,
	    addr[4] | addr[5] << 8);
	return otus_write_barrier(sc);
}

/* Default single-LED. */
void
otus_led_newstate_type1(struct otus_softc *sc)
{
	/* TBD */
	device_printf(sc->sc_dev, "%s: TODO\n", __func__);
}

/* NETGEAR, dual-LED. */
void
otus_led_newstate_type2(struct otus_softc *sc)
{
	/* TBD */
	device_printf(sc->sc_dev, "%s: TODO\n", __func__);
}

/* NETGEAR, single-LED/3 colors (blue, red, purple.) */
void
otus_led_newstate_type3(struct otus_softc *sc)
{
#if 0
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap = TAILQ_FIRST(&ic->ic_vaps);

	uint32_t state = sc->led_state;

	OTUS_LOCK_ASSERT(sc);

	if (!vap) {
		state = 0;	/* led off */
	} else if (vap->iv_state == IEEE80211_S_INIT) {
		state = 0;	/* LED off. */
	} else if (vap->iv_state == IEEE80211_S_RUN) {
		/* Associated, LED always on. */
		if (IEEE80211_IS_CHAN_2GHZ(sc->sc_curchan))
			state = AR_LED0_ON;	/* 2GHz=>Red. */
		else
			state = AR_LED1_ON;	/* 5GHz=>Blue. */
	} else {
		/* Scanning, blink LED. */
		state ^= AR_LED0_ON | AR_LED1_ON;
		if (IEEE80211_IS_CHAN_2GHZ(sc->sc_curchan))
			state &= ~AR_LED1_ON;
		else
			state &= ~AR_LED0_ON;
	}
	if (state != sc->led_state) {
		otus_write(sc, AR_GPIO_REG_PORT_DATA, state);
		if (otus_write_barrier(sc) == 0)
			sc->led_state = state;
	}
#endif
}

static uint8_t zero_macaddr[IEEE80211_ADDR_LEN] = { 0,0,0,0,0,0 };

/*
 * Set up operating mode, MAC/BSS address and RX filter.
 */
static void
otus_set_operating_mode(struct otus_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211vap *vap;
	uint32_t cam_mode = AR_MAC_CAM_DEFAULTS;
	uint32_t rx_ctrl = AR_MAC_RX_CTRL_DEAGG | AR_MAC_RX_CTRL_SHORT_FILTER;
	uint32_t sniffer = AR_MAC_SNIFFER_DEFAULTS;
	uint32_t enc_mode = 0x78; /* XXX */
	const uint8_t *macaddr;
	uint8_t bssid[IEEE80211_ADDR_LEN];
	struct ieee80211_node *ni;

	OTUS_LOCK_ASSERT(sc);

	/*
	 * If we're in sniffer mode or we don't have a MAC
	 * address assigned, ensure it gets reset to all-zero.
	 */
	IEEE80211_ADDR_COPY(bssid, zero_macaddr);
	vap = TAILQ_FIRST(&ic->ic_vaps);
	macaddr = ic->ic_macaddr;

	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		if (vap) {
			ni = ieee80211_ref_node(vap->iv_bss);
			IEEE80211_ADDR_COPY(bssid, ni->ni_bssid);
			ieee80211_free_node(ni);
		}
		cam_mode |= AR_MAC_CAM_STA;
		rx_ctrl |= AR_MAC_RX_CTRL_PASS_TO_HOST;
		break;
	case IEEE80211_M_MONITOR:
		/*
		 * Note: monitor mode ends up causing the MAC to
		 * generate ACK frames for everything it sees.
		 * So don't do that; instead just put it in STA mode
		 * and disable RX filters.
		 */
	default:
		cam_mode |= AR_MAC_CAM_STA;
		rx_ctrl |= AR_MAC_RX_CTRL_PASS_TO_HOST;
		break;
	}

	/*
	 * TODO: if/when we do hardware encryption, ensure it's
	 * disabled if the NIC is in monitor mode.
	 */
	otus_write(sc, AR_MAC_REG_SNIFFER, sniffer);
	otus_write(sc, AR_MAC_REG_CAM_MODE, cam_mode);
	otus_write(sc, AR_MAC_REG_ENCRYPTION, enc_mode);
	otus_write(sc, AR_MAC_REG_RX_CONTROL, rx_ctrl);
	otus_set_macaddr(sc, macaddr);
	otus_set_bssid(sc, bssid);
	/* XXX barrier? */
}

static void
otus_set_rx_filter(struct otus_softc *sc)
{
//	struct ieee80211com *ic = &sc->sc_ic;

	OTUS_LOCK_ASSERT(sc);

#if 0
	if (ic->ic_allmulti > 0 || ic->ic_promisc > 0 ||
	    ic->ic_opmode == IEEE80211_M_MONITOR) {
		otus_write(sc, AR_MAC_REG_FRAMETYPE_FILTER, 0xff00ffff);
	} else {
#endif
		/* Filter any control frames, BAR is bit 24. */
		otus_write(sc, AR_MAC_REG_FRAMETYPE_FILTER, 0x0500ffff);
#if 0
	}
#endif
}

int
otus_init(struct otus_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int error;

	OTUS_UNLOCK_ASSERT(sc);

	OTUS_LOCK(sc);

	/* Drain any pending TX frames */
	otus_drain_mbufq(sc);

	/* Init MAC */
	if ((error = otus_init_mac(sc)) != 0) {
		OTUS_UNLOCK(sc);
		device_printf(sc->sc_dev,
		    "%s: could not initialize MAC\n", __func__);
		return error;
	}

	otus_set_operating_mode(sc);
	otus_set_rx_filter(sc);
	(void) otus_set_operating_mode(sc);

	sc->bb_reset = 1;	/* Force cold reset. */

	if ((error = otus_set_chan(sc, ic->ic_curchan, 0)) != 0) {
		OTUS_UNLOCK(sc);
		device_printf(sc->sc_dev,
		    "%s: could not set channel\n", __func__);
		return error;
	}

	/* Start Rx. */
	otus_write(sc, AR_MAC_REG_DMA_TRIGGER, 0x100);
	(void)otus_write_barrier(sc);

	sc->sc_running = 1;

	OTUS_UNLOCK(sc);
	return 0;
}

void
otus_stop(struct otus_softc *sc)
{
#if 0
	int s;
#endif

	OTUS_UNLOCK_ASSERT(sc);

	OTUS_LOCK(sc);
	sc->sc_running = 0;
	sc->sc_tx_timer = 0;
	OTUS_UNLOCK(sc);

	taskqueue_drain_timeout(taskqueue_thread, &sc->scan_to);
	taskqueue_drain_timeout(taskqueue_thread, &sc->calib_to);
	taskqueue_drain(taskqueue_thread, &sc->tx_task);

	OTUS_LOCK(sc);
	sc->sc_running = 0;
	/* Stop Rx. */
	otus_write(sc, AR_MAC_REG_DMA_TRIGGER, 0);
	(void)otus_write_barrier(sc);

	/* Drain any pending TX frames */
	otus_drain_mbufq(sc);

	OTUS_UNLOCK(sc);
}
