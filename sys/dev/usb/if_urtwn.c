/*	$OpenBSD: if_urtwn.c,v 1.114 2025/06/18 13:48:23 jcs Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2016 Nathanial Sloss <nathanialsloss@yahoo.com.au>
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
 * Driver for Realtek RTL8188CE-VAU/RTL8188CUS/RTL8188EU/RTL8188FTV/RTL8188RU/
 * RTL8192CU/RTL8192EU.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/mbuf.h>
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

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usbdevs.h>

#include <dev/ic/r92creg.h>
#include <dev/ic/rtwnvar.h>

/* Maximum number of output pipes is 3. */
#define R92C_MAX_EPOUT	3

#define R92C_HQ_NPAGES		12
#define R92C_LQ_NPAGES		2
#define R92C_NQ_NPAGES		2
#define R92C_TXPKTBUF_COUNT	256
#define R92C_TX_PAGE_COUNT	248
#define R92C_MAX_RX_DMA_SIZE	0x2800

#define R88E_HQ_NPAGES		0
#define R88E_LQ_NPAGES		9
#define R88E_NQ_NPAGES		0
#define R88E_TXPKTBUF_COUNT	177
#define R88E_TX_PAGE_COUNT	168
#define R88E_MAX_RX_DMA_SIZE	0x2400

#define R88F_HQ_NPAGES		12
#define R88F_LQ_NPAGES		2
#define R88F_NQ_NPAGES		2
#define R88F_TXPKTBUF_COUNT	177
#define R88F_TX_PAGE_COUNT	247
#define R88F_MAX_RX_DMA_SIZE	0x3f80

#define R92E_HQ_NPAGES		16
#define R92E_LQ_NPAGES		16
#define R92E_NQ_NPAGES		16
#define R92E_TX_PAGE_COUNT	248
#define R92E_MAX_RX_DMA_SIZE	0x3fc0

#define R92C_TXDESC_SUMSIZE	32
#define R92C_TXDESC_SUMOFFSET	14

/* USB Requests. */
#define R92C_REQ_REGS	0x05

/*
 * Driver definitions.
 */
#define URTWN_RX_LIST_COUNT		1
#define URTWN_TX_LIST_COUNT		8
#define URTWN_HOST_CMD_RING_COUNT	32

#define URTWN_RXBUFSZ	(16 * 1024)
#define URTWN_TXBUFSZ	(sizeof(struct r92e_tx_desc_usb) + IEEE80211_MAX_LEN)

#define URTWN_RIDX_COUNT	28

#define URTWN_TX_TIMEOUT	5000	/* ms */

#define URTWN_LED_LINK	0
#define URTWN_LED_DATA	1

struct urtwn_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	uint8_t		wr_dbm_antsignal;
} __packed;

#define URTWN_RX_RADIOTAP_PRESENT			\
	(1 << IEEE80211_RADIOTAP_FLAGS |		\
	 1 << IEEE80211_RADIOTAP_RATE |			\
	 1 << IEEE80211_RADIOTAP_CHANNEL |		\
	 1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL)

struct urtwn_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define URTWN_TX_RADIOTAP_PRESENT			\
	(1 << IEEE80211_RADIOTAP_FLAGS |		\
	 1 << IEEE80211_RADIOTAP_CHANNEL)

struct urtwn_softc;

struct urtwn_rx_data {
	struct urtwn_softc	*sc;
	struct usbd_xfer	*xfer;
	uint8_t			*buf;
};

struct urtwn_tx_data {
	struct urtwn_softc		*sc;
	struct usbd_pipe		*pipe;
	struct usbd_xfer		*xfer;
	uint8_t				*buf;
	TAILQ_ENTRY(urtwn_tx_data)	next;
};

struct urtwn_host_cmd {
	void	(*cb)(struct urtwn_softc *, void *);
	uint8_t	data[256];
};

struct urtwn_cmd_newstate {
	enum ieee80211_state	state;
	int			arg;
};

struct urtwn_cmd_key {
	struct ieee80211_key	key;
	struct ieee80211_node	*ni;
};

struct urtwn_host_cmd_ring {
	struct urtwn_host_cmd	cmd[URTWN_HOST_CMD_RING_COUNT];
	int			cur;
	int			next;
	int			queued;
};

struct urtwn_softc {
	struct device			sc_dev;
	struct rtwn_softc		sc_sc;

	struct usbd_device		*sc_udev;
	struct usbd_interface		*sc_iface;
	struct usb_task			sc_task;

	struct timeout			scan_to;
	struct timeout			calib_to;

	int				ntx;
	struct usbd_pipe		*rx_pipe;
	struct usbd_pipe		*tx_pipe[R92C_MAX_EPOUT];
	int				ac2idx[EDCA_NUM_AC];

	struct urtwn_host_cmd_ring	cmdq;
	struct urtwn_rx_data		rx_data[URTWN_RX_LIST_COUNT];
	struct urtwn_tx_data		tx_data[URTWN_TX_LIST_COUNT];
	TAILQ_HEAD(, urtwn_tx_data)	tx_free_list;

	struct ieee80211_amrr		amrr;
	struct ieee80211_amrr_node	amn;

#if NBPFILTER > 0
	caddr_t				sc_drvbpf;

	union {
		struct urtwn_rx_radiotap_header th;
		uint8_t	pad[64];
	}				sc_rxtapu;
#define sc_rxtap	sc_rxtapu.th
	int				sc_rxtap_len;

	union {
		struct urtwn_tx_radiotap_header th;
		uint8_t	pad[64];
	}				sc_txtapu;
#define sc_txtap	sc_txtapu.th
	int				sc_txtap_len;
#endif
	int				sc_key_tasks;
};

#ifdef URTWN_DEBUG
#define DPRINTF(x)	do { if (urtwn_debug) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (urtwn_debug >= (n)) printf x; } while (0)
int urtwn_debug = 4;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

/*
 * Various supported device vendors/products.
 */
#define URTWN_DEV(v, p, f)					\
        { { USB_VENDOR_##v, USB_PRODUCT_##v##_##p }, (f) | RTWN_CHIP_USB }
#define URTWN_DEV_8192CU(v, p)	URTWN_DEV(v, p, RTWN_CHIP_92C | RTWN_CHIP_88C)
#define URTWN_DEV_8192EU(v, p)	URTWN_DEV(v, p, RTWN_CHIP_92E)
#define URTWN_DEV_8188EU(v, p)	URTWN_DEV(v, p, RTWN_CHIP_88E)
#define URTWN_DEV_8188F(v, p)	URTWN_DEV(v, p, RTWN_CHIP_88F)
static const struct urtwn_type {
	struct usb_devno        dev;
	uint32_t		chip;
} urtwn_devs[] = {
	URTWN_DEV_8192CU(ABOCOM,	RTL8188CU_1),
	URTWN_DEV_8192CU(ABOCOM,	RTL8188CU_2),
	URTWN_DEV_8192CU(ABOCOM,	RTL8192CU),
	URTWN_DEV_8192CU(ASUS,		RTL8192CU),
	URTWN_DEV_8192CU(ASUS,		RTL8192CU_2),
	URTWN_DEV_8192CU(ASUS,		RTL8192CU_3),
	URTWN_DEV_8192CU(AZUREWAVE,	RTL8188CE_1),
	URTWN_DEV_8192CU(AZUREWAVE,	RTL8188CE_2),
	URTWN_DEV_8192CU(AZUREWAVE,	RTL8188CU),
	URTWN_DEV_8192CU(BELKIN,	F7D2102),
	URTWN_DEV_8192CU(BELKIN,	F9L1004V1),
	URTWN_DEV_8192CU(BELKIN,	RTL8188CU),
	URTWN_DEV_8192CU(BELKIN,	RTL8188CUS),
	URTWN_DEV_8192CU(BELKIN,	RTL8192CU),
	URTWN_DEV_8192CU(BELKIN,	RTL8192CU_1),
	URTWN_DEV_8192CU(CHICONY,	RTL8188CUS_1),
	URTWN_DEV_8192CU(CHICONY,	RTL8188CUS_2),
	URTWN_DEV_8192CU(CHICONY,	RTL8188CUS_3),
	URTWN_DEV_8192CU(CHICONY,	RTL8188CUS_4),
	URTWN_DEV_8192CU(CHICONY,	RTL8188CUS_5),
	URTWN_DEV_8192CU(CHICONY,	RTL8188CUS_6),
	URTWN_DEV_8192CU(COMPARE,	RTL8192CU),
	URTWN_DEV_8192CU(COREGA,	RTL8192CU),
	URTWN_DEV_8192CU(DLINK,		DWA131B),
	URTWN_DEV_8192CU(DLINK,		RTL8188CU),
	URTWN_DEV_8192CU(DLINK,		RTL8192CU_1),
	URTWN_DEV_8192CU(DLINK,		RTL8192CU_2),
	URTWN_DEV_8192CU(DLINK,		RTL8192CU_3),
	URTWN_DEV_8192CU(DLINK,		RTL8192CU_4),
	URTWN_DEV_8192CU(EDIMAX,	EW7811UN),
	URTWN_DEV_8192CU(EDIMAX,	RTL8192CU),
	URTWN_DEV_8192CU(FEIXUN,	RTL8188CU),
	URTWN_DEV_8192CU(FEIXUN,	RTL8192CU),
	URTWN_DEV_8192CU(GUILLEMOT,	HWNUP150),
	URTWN_DEV_8192CU(GUILLEMOT,	RTL8192CU),
	URTWN_DEV_8192CU(HAWKING,	RTL8192CU),
	URTWN_DEV_8192CU(HAWKING,	RTL8192CU_2),
	URTWN_DEV_8192CU(HP3,		RTL8188CU),
	URTWN_DEV_8192CU(IODATA,	WNG150UM),
	URTWN_DEV_8192CU(IODATA,	RTL8192CU),
	URTWN_DEV_8192CU(NETGEAR,	N300MA),
	URTWN_DEV_8192CU(NETGEAR,	WNA1000M),
	URTWN_DEV_8192CU(NETGEAR,	WNA1000MV2),
	URTWN_DEV_8192CU(NETGEAR,	RTL8192CU),
	URTWN_DEV_8192CU(NETGEAR4,	RTL8188CU),
	URTWN_DEV_8192CU(NETWEEN,	RTL8192CU),
	URTWN_DEV_8192CU(NOVATECH,	RTL8188CU),
	URTWN_DEV_8192CU(PLANEX2,	RTL8188CU_1),
	URTWN_DEV_8192CU(PLANEX2,	RTL8188CU_2),
	URTWN_DEV_8192CU(PLANEX2,	RTL8188CU_3),
	URTWN_DEV_8192CU(PLANEX2,	RTL8188CU_4),
	URTWN_DEV_8192CU(PLANEX2,	RTL8188CUS),
	URTWN_DEV_8192CU(PLANEX2,	RTL8192CU),
	URTWN_DEV_8192CU(REALTEK,	RTL8188CE_0),
	URTWN_DEV_8192CU(REALTEK,	RTL8188CE_1),
	URTWN_DEV_8192CU(REALTEK,	RTL8188CTV),
	URTWN_DEV_8192CU(REALTEK,	RTL8188CU_0),
	URTWN_DEV_8192CU(REALTEK,	RTL8188CU_1),
	URTWN_DEV_8192CU(REALTEK,	RTL8188CU_2),
	URTWN_DEV_8192CU(REALTEK,	RTL8188CU_3),
	URTWN_DEV_8192CU(REALTEK,	RTL8188CU_4),
	URTWN_DEV_8192CU(REALTEK,	RTL8188CU_5),
	URTWN_DEV_8192CU(REALTEK,	RTL8188CU_COMBO),
	URTWN_DEV_8192CU(REALTEK,	RTL8188CUS),
	URTWN_DEV_8192CU(REALTEK,	RTL8188RU),
	URTWN_DEV_8192CU(REALTEK,	RTL8188RU_2),
	URTWN_DEV_8192CU(REALTEK,	RTL8188RU_3),
	URTWN_DEV_8192CU(REALTEK,	RTL8191CU),
	URTWN_DEV_8192CU(REALTEK,	RTL8192CE),
	URTWN_DEV_8192CU(REALTEK,	RTL8192CE_VAU),
	URTWN_DEV_8192CU(REALTEK,	RTL8192CU),
	URTWN_DEV_8192CU(SITECOMEU,	RTL8188CU),
	URTWN_DEV_8192CU(SITECOMEU,	RTL8188CU_2),
	URTWN_DEV_8192CU(SITECOMEU,	RTL8192CU),
	URTWN_DEV_8192CU(SITECOMEU,	RTL8192CU_2),
	URTWN_DEV_8192CU(SITECOMEU,	WLA2100V2),
	URTWN_DEV_8192CU(TPLINK,	RTL8192CU),
	URTWN_DEV_8192CU(TRENDNET,	RTL8188CU),
	URTWN_DEV_8192CU(TRENDNET,	RTL8192CU),
	URTWN_DEV_8192CU(ZYXEL,		RTL8192CU),
	/* URTWN_RTL8188E */
	URTWN_DEV_8188EU(ABOCOM,	RTL8188EU),
	URTWN_DEV_8188EU(DLINK,		DWA121B1),
	URTWN_DEV_8188EU(DLINK,		DWA123D1),
	URTWN_DEV_8188EU(DLINK,		DWA125D1),
	URTWN_DEV_8188EU(EDIMAX,	EW7811UNV2),
	URTWN_DEV_8188EU(ELECOM,	WDC150SU2M),
	URTWN_DEV_8188EU(MERCUSYS,	MW150USV2),
	URTWN_DEV_8188EU(REALTEK,	RTL8188ETV),
	URTWN_DEV_8188EU(REALTEK,	RTL8188EU),
	URTWN_DEV_8188EU(TPLINK,	RTL8188EUS),
	URTWN_DEV_8188EU(ASUS,  	RTL8188EUS),
	/* URTWN_RTL8188FTV */
	URTWN_DEV_8188F(REALTEK,	RTL8188FTV),

	/* URTWN_RTL8192EU */
	URTWN_DEV_8192EU(DLINK,		DWA131E1),
	URTWN_DEV_8192EU(REALTEK,	RTL8192EU),
	URTWN_DEV_8192EU(REALTEK,	RTL8192EU_2),
	URTWN_DEV_8192EU(TPLINK,	RTL8192EU),
	URTWN_DEV_8192EU(TPLINK,	RTL8192EU_2),
	URTWN_DEV_8192EU(TPLINK,	RTL8192EU_3)
};

#define urtwn_lookup(v, p)	\
	((const struct urtwn_type *)usb_lookup(urtwn_devs, v, p))

int		urtwn_match(struct device *, void *, void *);
void		urtwn_attach(struct device *, struct device *, void *);
int		urtwn_detach(struct device *, int);
int		urtwn_open_pipes(struct urtwn_softc *);
void		urtwn_close_pipes(struct urtwn_softc *);
int		urtwn_alloc_rx_list(struct urtwn_softc *);
void		urtwn_free_rx_list(struct urtwn_softc *);
int		urtwn_alloc_tx_list(struct urtwn_softc *);
void		urtwn_free_tx_list(struct urtwn_softc *);
void		urtwn_task(void *);
void		urtwn_do_async(struct urtwn_softc *,
		    void (*)(struct urtwn_softc *, void *), void *, int);
void		urtwn_wait_async(void *);
int		urtwn_write_region_1(struct urtwn_softc *, uint16_t, uint8_t *,
		    int);
void		urtwn_write_1(void *, uint16_t, uint8_t);
void		urtwn_write_2(void *, uint16_t, uint16_t);
void		urtwn_write_4(void *, uint16_t, uint32_t);
int		urtwn_read_region_1(struct urtwn_softc *, uint16_t, uint8_t *,
		    int);
uint8_t		urtwn_read_1(void *, uint16_t);
uint16_t	urtwn_read_2(void *, uint16_t);
uint32_t	urtwn_read_4(void *, uint16_t);
int		urtwn_llt_write(struct urtwn_softc *, uint32_t, uint32_t);
void		urtwn_calib_to(void *);
void		urtwn_calib_cb(struct urtwn_softc *, void *);
void		urtwn_scan_to(void *);
void		urtwn_next_scan(void *);
void		urtwn_cancel_scan(void *);
int		urtwn_newstate(struct ieee80211com *, enum ieee80211_state,
		    int);
void		urtwn_newstate_cb(struct urtwn_softc *, void *);
void		urtwn_updateslot(struct ieee80211com *);
void		urtwn_updateslot_cb(struct urtwn_softc *, void *);
void		urtwn_updateedca(struct ieee80211com *);
void		urtwn_updateedca_cb(struct urtwn_softc *, void *);
int		urtwn_set_key(struct ieee80211com *, struct ieee80211_node *,
		    struct ieee80211_key *);
void		urtwn_set_key_cb(struct urtwn_softc *, void *);
void		urtwn_delete_key(struct ieee80211com *,
		    struct ieee80211_node *, struct ieee80211_key *);
void		urtwn_delete_key_cb(struct urtwn_softc *, void *);
void		urtwn_rx_frame(struct urtwn_softc *, uint8_t *, int,
		    struct mbuf_list *);
void		urtwn_rxeof(struct usbd_xfer *, void *,
		    usbd_status);
void		urtwn_txeof(struct usbd_xfer *, void *,
		    usbd_status);
int		urtwn_tx(void *, struct mbuf *, struct ieee80211_node *);
int		urtwn_ioctl(struct ifnet *, u_long, caddr_t);
int		urtwn_power_on(void *);
int		urtwn_alloc_buffers(void *);
int		urtwn_r92c_power_on(struct urtwn_softc *);
int		urtwn_r92e_power_on(struct urtwn_softc *);
int		urtwn_r88e_power_on(struct urtwn_softc *);
int		urtwn_r88f_power_on(struct urtwn_softc *);
int		urtwn_llt_init(struct urtwn_softc *, int);
int		urtwn_fw_loadpage(void *, int, uint8_t *, int);
int		urtwn_load_firmware(void *, u_char **, size_t *);
int		urtwn_dma_init(void *);
void		urtwn_aggr_init(void *);
void		urtwn_mac_init(void *);
void		urtwn_bb_init(void *);
void		urtwn_burstlen_init(struct urtwn_softc *);
int		urtwn_init(void *);
void		urtwn_stop(void *);
int		urtwn_is_oactive(void *);
void		urtwn_next_calib(void *);
void		urtwn_cancel_calib(void *);

/* Aliases. */
#define	urtwn_bb_write	urtwn_write_4
#define urtwn_bb_read	urtwn_read_4

struct cfdriver urtwn_cd = {
	NULL, "urtwn", DV_IFNET
};

const struct cfattach urtwn_ca = {
	sizeof(struct urtwn_softc), urtwn_match, urtwn_attach, urtwn_detach
};

int
urtwn_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != 1)
		return (UMATCH_NONE);

	return ((urtwn_lookup(uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT_CONF_IFACE : UMATCH_NONE);
}

void
urtwn_attach(struct device *parent, struct device *self, void *aux)
{
	struct urtwn_softc *sc = (struct urtwn_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct ifnet *ifp;
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;

	sc->sc_udev = uaa->device;
	sc->sc_iface = uaa->iface;

	sc->sc_sc.chip = urtwn_lookup(uaa->vendor, uaa->product)->chip;

	usb_init_task(&sc->sc_task, urtwn_task, sc, USB_TASK_TYPE_GENERIC);
	timeout_set(&sc->scan_to, urtwn_scan_to, sc);
	timeout_set(&sc->calib_to, urtwn_calib_to, sc);
	if (urtwn_open_pipes(sc) != 0)
		return;

	sc->amrr.amrr_min_success_threshold =  1;
	sc->amrr.amrr_max_success_threshold = 10;

	/* Attach the bus-agnostic driver. */
	sc->sc_sc.sc_ops.cookie = sc;
	sc->sc_sc.sc_ops.write_1 = urtwn_write_1;
	sc->sc_sc.sc_ops.write_2 = urtwn_write_2;
	sc->sc_sc.sc_ops.write_4 = urtwn_write_4;
	sc->sc_sc.sc_ops.read_1 = urtwn_read_1;
	sc->sc_sc.sc_ops.read_2 = urtwn_read_2;
	sc->sc_sc.sc_ops.read_4 = urtwn_read_4;
	sc->sc_sc.sc_ops.tx = urtwn_tx;
	sc->sc_sc.sc_ops.power_on = urtwn_power_on;
	sc->sc_sc.sc_ops.dma_init = urtwn_dma_init;
	sc->sc_sc.sc_ops.fw_loadpage = urtwn_fw_loadpage;
	sc->sc_sc.sc_ops.load_firmware = urtwn_load_firmware;
	sc->sc_sc.sc_ops.aggr_init = urtwn_aggr_init;
	sc->sc_sc.sc_ops.mac_init = urtwn_mac_init;
	sc->sc_sc.sc_ops.bb_init = urtwn_bb_init;
	sc->sc_sc.sc_ops.alloc_buffers = urtwn_alloc_buffers;
	sc->sc_sc.sc_ops.init = urtwn_init;
	sc->sc_sc.sc_ops.stop = urtwn_stop;
	sc->sc_sc.sc_ops.is_oactive = urtwn_is_oactive;
	sc->sc_sc.sc_ops.next_calib = urtwn_next_calib;
	sc->sc_sc.sc_ops.cancel_calib = urtwn_cancel_calib;
	sc->sc_sc.sc_ops.next_scan = urtwn_next_scan;
	sc->sc_sc.sc_ops.cancel_scan = urtwn_cancel_scan;
	sc->sc_sc.sc_ops.wait_async = urtwn_wait_async;
	if (rtwn_attach(&sc->sc_dev, &sc->sc_sc) != 0) {
		urtwn_close_pipes(sc);
		return;
	}

	/* ifp is now valid */
	ifp = &sc->sc_sc.sc_ic.ic_if;
	ifp->if_ioctl = urtwn_ioctl;

	ic->ic_updateslot = urtwn_updateslot;
	ic->ic_updateedca = urtwn_updateedca;
	ic->ic_set_key = urtwn_set_key;
	ic->ic_delete_key = urtwn_delete_key;
	/* Override state transition machine. */
	ic->ic_newstate = urtwn_newstate;

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof(struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN);

	sc->sc_rxtap_len = sizeof(sc->sc_rxtapu);
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(URTWN_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof(sc->sc_txtapu);
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(URTWN_TX_RADIOTAP_PRESENT);
#endif
}

int
urtwn_detach(struct device *self, int flags)
{
	struct urtwn_softc *sc = (struct urtwn_softc *)self;
	int s;

	s = splusb();

	if (timeout_initialized(&sc->scan_to))
		timeout_del(&sc->scan_to);
	if (timeout_initialized(&sc->calib_to))
		timeout_del(&sc->calib_to);

	/* Wait for all async commands to complete. */
	usb_rem_wait_task(sc->sc_udev, &sc->sc_task);

	usbd_ref_wait(sc->sc_udev);

	rtwn_detach(&sc->sc_sc, flags);

	/* Abort and close Tx/Rx pipes. */
	urtwn_close_pipes(sc);

	/* Free Tx/Rx buffers. */
	urtwn_free_tx_list(sc);
	urtwn_free_rx_list(sc);
	splx(s);

	return (0);
}

int
urtwn_open_pipes(struct urtwn_softc *sc)
{
	/* Bulk-out endpoints addresses (from highest to lowest prio). */
	uint8_t epaddr[R92C_MAX_EPOUT] = { 0, 0, 0 };
	uint8_t rx_no;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	int i, error, nrx = 0;

	/* Find all bulk endpoints. */
	id = usbd_get_interface_descriptor(sc->sc_iface);
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL || UE_GET_XFERTYPE(ed->bmAttributes) != UE_BULK)
			continue;

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN) {
			rx_no = ed->bEndpointAddress;
			nrx++;
		} else {
			if (sc->ntx < R92C_MAX_EPOUT)
				epaddr[sc->ntx] = ed->bEndpointAddress;
			sc->ntx++;
		}
	}
	if (nrx == 0) {
		printf("%s: %d: invalid number of Rx bulk pipes\n",
		    sc->sc_dev.dv_xname, nrx);
		return (EIO);
	}
	DPRINTF(("found %d bulk-out pipes\n", sc->ntx));
	if (sc->ntx == 0 || sc->ntx > R92C_MAX_EPOUT) {
		printf("%s: %d: invalid number of Tx bulk pipes\n",
		    sc->sc_dev.dv_xname, sc->ntx);
		return (EIO);
	}

	/* Open bulk-in pipe. */
	error = usbd_open_pipe(sc->sc_iface, rx_no, 0, &sc->rx_pipe);
	if (error != 0) {
		printf("%s: could not open Rx bulk pipe\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	/* Open bulk-out pipes (up to 3). */
	for (i = 0; i < sc->ntx; i++) {
		error = usbd_open_pipe(sc->sc_iface, epaddr[i], 0,
		    &sc->tx_pipe[i]);
		if (error != 0) {
			printf("%s: could not open Tx bulk pipe 0x%02x\n",
			    sc->sc_dev.dv_xname, epaddr[i]);
			goto fail;
		}
	}

	/* Map 802.11 access categories to USB pipes. */
	sc->ac2idx[EDCA_AC_BK] =
	sc->ac2idx[EDCA_AC_BE] = (sc->ntx == 3) ? 2 : ((sc->ntx == 2) ? 1 : 0);
	sc->ac2idx[EDCA_AC_VI] = (sc->ntx == 3) ? 1 : 0;
	sc->ac2idx[EDCA_AC_VO] = 0;	/* Always use highest prio. */

	if (error != 0)
 fail:		urtwn_close_pipes(sc);
	return (error);
}

void
urtwn_close_pipes(struct urtwn_softc *sc)
{
	int i;

	/* Close Rx pipe. */
	if (sc->rx_pipe != NULL) {
		usbd_close_pipe(sc->rx_pipe);
		sc->rx_pipe = NULL;
	}
	/* Close Tx pipes. */
	for (i = 0; i < R92C_MAX_EPOUT; i++) {
		if (sc->tx_pipe[i] == NULL)
			continue;
		usbd_close_pipe(sc->tx_pipe[i]);
		sc->tx_pipe[i] = NULL;
	}
}

int
urtwn_alloc_rx_list(struct urtwn_softc *sc)
{
	struct urtwn_rx_data *data;
	int i, error = 0;

	for (i = 0; i < URTWN_RX_LIST_COUNT; i++) {
		data = &sc->rx_data[i];

		data->sc = sc;	/* Backpointer for callbacks. */

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			printf("%s: could not allocate xfer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			break;
		}
		data->buf = usbd_alloc_buffer(data->xfer, URTWN_RXBUFSZ);
		if (data->buf == NULL) {
			printf("%s: could not allocate xfer buffer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			break;
		}
	}
	if (error != 0)
		urtwn_free_rx_list(sc);
	return (error);
}

void
urtwn_free_rx_list(struct urtwn_softc *sc)
{
	int i;

	/* NB: Caller must abort pipe first. */
	for (i = 0; i < URTWN_RX_LIST_COUNT; i++) {
		if (sc->rx_data[i].xfer != NULL)
			usbd_free_xfer(sc->rx_data[i].xfer);
		sc->rx_data[i].xfer = NULL;
	}
}

int
urtwn_alloc_tx_list(struct urtwn_softc *sc)
{
	struct urtwn_tx_data *data;
	int i, error = 0;

	TAILQ_INIT(&sc->tx_free_list);
	for (i = 0; i < URTWN_TX_LIST_COUNT; i++) {
		data = &sc->tx_data[i];

		data->sc = sc;	/* Backpointer for callbacks. */

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			printf("%s: could not allocate xfer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			break;
		}
		data->buf = usbd_alloc_buffer(data->xfer, URTWN_TXBUFSZ);
		if (data->buf == NULL) {
			printf("%s: could not allocate xfer buffer\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			break;
		}
		/* Append this Tx buffer to our free list. */
		TAILQ_INSERT_TAIL(&sc->tx_free_list, data, next);
	}
	if (error != 0)
		urtwn_free_tx_list(sc);
	return (error);
}

void
urtwn_free_tx_list(struct urtwn_softc *sc)
{
	int i;

	/* NB: Caller must abort pipe first. */
	for (i = 0; i < URTWN_TX_LIST_COUNT; i++) {
		if (sc->tx_data[i].xfer != NULL)
			usbd_free_xfer(sc->tx_data[i].xfer);
		sc->tx_data[i].xfer = NULL;
	}
}

void
urtwn_task(void *arg)
{
	struct urtwn_softc *sc = arg;
	struct urtwn_host_cmd_ring *ring = &sc->cmdq;
	struct urtwn_host_cmd *cmd;
	int s;

	/* Process host commands. */
	s = splusb();
	while (ring->next != ring->cur) {
		cmd = &ring->cmd[ring->next];
		splx(s);
		/* Invoke callback. */
		cmd->cb(sc, cmd->data);
		s = splusb();
		ring->queued--;
		ring->next = (ring->next + 1) % URTWN_HOST_CMD_RING_COUNT;
	}
	splx(s);
}

void
urtwn_do_async(struct urtwn_softc *sc,
    void (*cb)(struct urtwn_softc *, void *), void *arg, int len)
{
	struct urtwn_host_cmd_ring *ring = &sc->cmdq;
	struct urtwn_host_cmd *cmd;
	int s;

	s = splusb();
	cmd = &ring->cmd[ring->cur];
	cmd->cb = cb;
	KASSERT(len <= sizeof(cmd->data));
	memcpy(cmd->data, arg, len);
	ring->cur = (ring->cur + 1) % URTWN_HOST_CMD_RING_COUNT;

	/* If there is no pending command already, schedule a task. */
	if (++ring->queued == 1)
		usb_add_task(sc->sc_udev, &sc->sc_task);
	splx(s);
}

void
urtwn_wait_async(void *cookie)
{
	struct urtwn_softc *sc = cookie;
	int s;

	s = splusb();
	/* Wait for all queued asynchronous commands to complete. */
	usb_wait_task(sc->sc_udev, &sc->sc_task);
	splx(s);
}

int
urtwn_write_region_1(struct urtwn_softc *sc, uint16_t addr, uint8_t *buf,
    int len)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = R92C_REQ_REGS;
	USETW(req.wValue, addr);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);
	return (usbd_do_request(sc->sc_udev, &req, buf));
}

void
urtwn_write_1(void *cookie, uint16_t addr, uint8_t val)
{
	struct urtwn_softc *sc = cookie;

	urtwn_write_region_1(sc, addr, &val, 1);
}

void
urtwn_write_2(void *cookie, uint16_t addr, uint16_t val)
{
	struct urtwn_softc *sc = cookie;

	val = htole16(val);
	urtwn_write_region_1(sc, addr, (uint8_t *)&val, 2);
}

void
urtwn_write_4(void *cookie, uint16_t addr, uint32_t val)
{
	struct urtwn_softc *sc = cookie;

	val = htole32(val);
	urtwn_write_region_1(sc, addr, (uint8_t *)&val, 4);
}

int
urtwn_read_region_1(struct urtwn_softc *sc, uint16_t addr, uint8_t *buf,
    int len)
{
	usb_device_request_t req;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = R92C_REQ_REGS;
	USETW(req.wValue, addr);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);
	return (usbd_do_request(sc->sc_udev, &req, buf));
}

uint8_t
urtwn_read_1(void *cookie, uint16_t addr)
{
	struct urtwn_softc *sc = cookie;
	uint8_t val;

	if (urtwn_read_region_1(sc, addr, &val, 1) != 0)
		return (0xff);
	return (val);
}

uint16_t
urtwn_read_2(void *cookie, uint16_t addr)
{
	struct urtwn_softc *sc = cookie;
	uint16_t val;

	if (urtwn_read_region_1(sc, addr, (uint8_t *)&val, 2) != 0)
		return (0xffff);
	return (letoh16(val));
}

uint32_t
urtwn_read_4(void *cookie, uint16_t addr)
{
	struct urtwn_softc *sc = cookie;
	uint32_t val;

	if (urtwn_read_region_1(sc, addr, (uint8_t *)&val, 4) != 0)
		return (0xffffffff);
	return (letoh32(val));
}

int
urtwn_llt_write(struct urtwn_softc *sc, uint32_t addr, uint32_t data)
{
	int ntries;

	urtwn_write_4(sc, R92C_LLT_INIT,
	    SM(R92C_LLT_INIT_OP, R92C_LLT_INIT_OP_WRITE) |
	    SM(R92C_LLT_INIT_ADDR, addr) |
	    SM(R92C_LLT_INIT_DATA, data));
	/* Wait for write operation to complete. */
	for (ntries = 0; ntries < 20; ntries++) {
		if (MS(urtwn_read_4(sc, R92C_LLT_INIT), R92C_LLT_INIT_OP) ==
		    R92C_LLT_INIT_OP_NO_ACTIVE)
			return (0);
		DELAY(5);
	}
	return (ETIMEDOUT);
}

void
urtwn_calib_to(void *arg)
{
	struct urtwn_softc *sc = arg;

	if (usbd_is_dying(sc->sc_udev))
		return;

	usbd_ref_incr(sc->sc_udev);

	/* Do it in a process context. */
	urtwn_do_async(sc, urtwn_calib_cb, NULL, 0);

	usbd_ref_decr(sc->sc_udev);
}

void
urtwn_calib_cb(struct urtwn_softc *sc, void *arg)
{
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;
	int s;

	s = splnet();
	if (ic->ic_opmode == IEEE80211_M_STA) {
		ieee80211_amrr_choose(&sc->amrr, ic->ic_bss, &sc->amn);
	}
	splx(s);

	rtwn_calib(&sc->sc_sc);
}

void
urtwn_next_calib(void *cookie)
{
	struct urtwn_softc *sc = cookie;

	if (!usbd_is_dying(sc->sc_udev))
		timeout_add_sec(&sc->calib_to, 2);
}

void
urtwn_cancel_calib(void *cookie)
{
	struct urtwn_softc *sc = cookie;

	if (timeout_initialized(&sc->calib_to))
		timeout_del(&sc->calib_to);
}

void
urtwn_scan_to(void *arg)
{
	struct urtwn_softc *sc = arg;

	if (usbd_is_dying(sc->sc_udev))
		return;

	usbd_ref_incr(sc->sc_udev);
	rtwn_next_scan(&sc->sc_sc);
	usbd_ref_decr(sc->sc_udev);
}

void
urtwn_next_scan(void *arg)
{
	struct urtwn_softc *sc = arg;

	if (!usbd_is_dying(sc->sc_udev))
		timeout_add_msec(&sc->scan_to, 200);
}

void
urtwn_cancel_scan(void *cookie)
{
	struct urtwn_softc *sc = cookie;

	if (timeout_initialized(&sc->scan_to))
		timeout_del(&sc->scan_to);
}

int
urtwn_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct rtwn_softc *sc_sc = ic->ic_softc;
	struct device *self = sc_sc->sc_pdev;
	struct urtwn_softc *sc = (struct urtwn_softc *)self;
	struct urtwn_cmd_newstate cmd;

	/* Do it in a process context. */
	cmd.state = nstate;
	cmd.arg = arg;
	urtwn_do_async(sc, urtwn_newstate_cb, &cmd, sizeof(cmd));
	return (0);
}

void
urtwn_newstate_cb(struct urtwn_softc *sc, void *arg)
{
	struct urtwn_cmd_newstate *cmd = arg;
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;

	rtwn_newstate(ic, cmd->state, cmd->arg);
}

void
urtwn_updateslot(struct ieee80211com *ic)
{
	struct rtwn_softc *sc_sc = ic->ic_softc;
	struct device *self = sc_sc->sc_pdev;
	struct urtwn_softc *sc = (struct urtwn_softc *)self;

	/* Do it in a process context. */
	urtwn_do_async(sc, urtwn_updateslot_cb, NULL, 0);
}

void
urtwn_updateslot_cb(struct urtwn_softc *sc, void *arg)
{
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;

	rtwn_updateslot(ic);
}

void
urtwn_updateedca(struct ieee80211com *ic)
{
	struct rtwn_softc *sc_sc = ic->ic_softc;
	struct device *self = sc_sc->sc_pdev;
	struct urtwn_softc *sc = (struct urtwn_softc *)self;

	/* Do it in a process context. */
	urtwn_do_async(sc, urtwn_updateedca_cb, NULL, 0);
}

void
urtwn_updateedca_cb(struct urtwn_softc *sc, void *arg)
{
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;

	rtwn_updateedca(ic);
}

int
urtwn_set_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct rtwn_softc *sc_sc = ic->ic_softc;
	struct device *self = sc_sc->sc_pdev;
	struct urtwn_softc *sc = (struct urtwn_softc *)self;
	struct urtwn_cmd_key cmd;

	/* Only handle keys for CCMP */
	if (k->k_cipher != IEEE80211_CIPHER_CCMP)
		return ieee80211_set_key(ic, ni, k);

	/* Defer setting of WEP keys until interface is brought up. */
	if ((ic->ic_if.if_flags & (IFF_UP | IFF_RUNNING)) !=
	    (IFF_UP | IFF_RUNNING))
		return (0);

	/* Do it in a process context. */
	cmd.key = *k;
	cmd.ni = ni;
	urtwn_do_async(sc, urtwn_set_key_cb, &cmd, sizeof(cmd));
	sc->sc_key_tasks++;

	return (EBUSY);
}

void
urtwn_set_key_cb(struct urtwn_softc *sc, void *arg)
{
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;
	struct urtwn_cmd_key *cmd = arg;

	sc->sc_key_tasks--;

	if (rtwn_set_key(ic, cmd->ni, &cmd->key) == 0) {
		if (sc->sc_key_tasks == 0) {
			DPRINTF(("marking port %s valid\n",
			    ether_sprintf(cmd->ni->ni_macaddr)));
			cmd->ni->ni_port_valid = 1;
			ieee80211_set_link_state(ic, LINK_STATE_UP);
		}
	} else {
		IEEE80211_SEND_MGMT(ic, cmd->ni, IEEE80211_FC0_SUBTYPE_DEAUTH,
		    IEEE80211_REASON_AUTH_LEAVE);
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
	}
}

void
urtwn_delete_key(struct ieee80211com *ic, struct ieee80211_node *ni,
    struct ieee80211_key *k)
{
	struct rtwn_softc *sc_sc = ic->ic_softc;
	struct device *self = sc_sc->sc_pdev;
	struct urtwn_softc *sc = (struct urtwn_softc *)self;
	struct urtwn_cmd_key cmd;

	/* Only handle keys for CCMP */
	if (k->k_cipher != IEEE80211_CIPHER_CCMP) {
		ieee80211_delete_key(ic, ni, k);
		return;
	}

	if (!(ic->ic_if.if_flags & IFF_RUNNING) ||
	    ic->ic_state != IEEE80211_S_RUN)
		return;	/* Nothing to do. */

	/* Do it in a process context. */
	cmd.key = *k;
	cmd.ni = ni;
	urtwn_do_async(sc, urtwn_delete_key_cb, &cmd, sizeof(cmd));
}

void
urtwn_delete_key_cb(struct urtwn_softc *sc, void *arg)
{
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;
	struct urtwn_cmd_key *cmd = arg;

	rtwn_delete_key(ic, cmd->ni, &cmd->key);
}

int
urtwn_ccmp_decap(struct urtwn_softc *sc, struct mbuf *m,
    struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;
	struct ieee80211_key *k;
	struct ieee80211_frame *wh;
	uint64_t pn, *prsc;
	uint8_t *ivp;
	uint8_t tid;
	int hdrlen, hasqos;

	k = ieee80211_get_rxkey(ic, m, ni);
	if (k == NULL)
		return 1;

	wh = mtod(m, struct ieee80211_frame *);
	hdrlen = ieee80211_get_hdrlen(wh);
	ivp = (uint8_t *)wh + hdrlen;

	/* Check that ExtIV bit is set. */
	if (!(ivp[3] & IEEE80211_WEP_EXTIV))
		return 1;

	hasqos = ieee80211_has_qos(wh);
	tid = hasqos ? ieee80211_get_qos(wh) & IEEE80211_QOS_TID : 0;
	prsc = &k->k_rsc[tid];

	/* Extract the 48-bit PN from the CCMP header. */
	pn = (uint64_t)ivp[0]       |
	     (uint64_t)ivp[1] <<  8 |
	     (uint64_t)ivp[4] << 16 |
	     (uint64_t)ivp[5] << 24 |
	     (uint64_t)ivp[6] << 32 |
	     (uint64_t)ivp[7] << 40;
	if (pn <= *prsc) {
		ic->ic_stats.is_ccmp_replays++;
		return 1;
	}
	/* Last seen packet number is updated in ieee80211_inputm(). */

	/* Strip MIC. IV will be stripped by ieee80211_inputm(). */
	m_adj(m, -IEEE80211_CCMP_MICLEN);
	return 0;
}

void
urtwn_rx_frame(struct urtwn_softc *sc, uint8_t *buf, int pktlen,
    struct mbuf_list *ml)
{
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	struct r92c_rx_desc_usb *rxd;
	uint32_t rxdw0, rxdw3;
	struct mbuf *m;
	uint8_t rate;
	int8_t rssi = 0;
	int s, infosz;

	rxd = (struct r92c_rx_desc_usb *)buf;
	rxdw0 = letoh32(rxd->rxdw0);
	rxdw3 = letoh32(rxd->rxdw3);

	if (__predict_false(rxdw0 & (R92C_RXDW0_CRCERR | R92C_RXDW0_ICVERR))) {
		/*
		 * This should not happen since we setup our Rx filter
		 * to not receive these frames.
		 */
		ifp->if_ierrors++;
		return;
	}
	if (__predict_false(pktlen < sizeof(*wh) || pktlen > MCLBYTES)) {
		ifp->if_ierrors++;
		return;
	}

	rate = (sc->sc_sc.chip & (RTWN_CHIP_88F | RTWN_CHIP_92E)) ?
	    MS(rxdw3, R92E_RXDW3_RATE) : MS(rxdw3, R92C_RXDW3_RATE);
	infosz = MS(rxdw0, R92C_RXDW0_INFOSZ) * 8;

	/* Get RSSI from PHY status descriptor if present. */
	if (infosz != 0 && (rxdw0 & R92C_RXDW0_PHYST)) {
		rssi = rtwn_get_rssi(&sc->sc_sc, rate, &rxd[1]);
		/* Update our average RSSI. */
		rtwn_update_avgrssi(&sc->sc_sc, rate, rssi);
	}

	DPRINTFN(5, ("Rx frame len=%d rate=%d infosz=%d rssi=%d\n",
	    pktlen, rate, infosz, rssi));

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (__predict_false(m == NULL)) {
		ifp->if_ierrors++;
		return;
	}
	if (pktlen > MHLEN) {
		MCLGET(m, M_DONTWAIT);
		if (__predict_false(!(m->m_flags & M_EXT))) {
			ifp->if_ierrors++;
			m_freem(m);
			return;
		}
	}
	/* Finalize mbuf. */
	wh = (struct ieee80211_frame *)((uint8_t *)&rxd[1] + infosz);
	memcpy(mtod(m, uint8_t *), wh, pktlen);
	m->m_pkthdr.len = m->m_len = pktlen;

	s = splnet();
#if NBPFILTER > 0
	if (__predict_false(sc->sc_drvbpf != NULL)) {
		struct urtwn_rx_radiotap_header *tap = &sc->sc_rxtap;
		struct mbuf mb;

		tap->wr_flags = 0;
		/* Map HW rate index to 802.11 rate. */
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
			if (rate <= 3)
				tap->wr_flags |= IEEE80211_RADIOTAP_F_SHORTPRE;
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

	/* Handle hardware decryption. */
	if (((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_CTL)
	    && (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) &&
	    (ni->ni_flags & IEEE80211_NODE_RXPROT) &&
	    ((!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    ni->ni_pairwise_key.k_cipher == IEEE80211_CIPHER_CCMP) ||
	    (IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    ni->ni_rsngroupcipher == IEEE80211_CIPHER_CCMP))) {
		if (urtwn_ccmp_decap(sc, m, ni) != 0) {
			ifp->if_ierrors++;
			m_freem(m);
			ieee80211_release_node(ic, ni);
			splx(s);
			return;
		}
		rxi.rxi_flags |= IEEE80211_RXI_HWDEC;
	}

	ieee80211_inputm(ifp, m, ni, &rxi, ml);
	/* Node is no longer needed. */
	ieee80211_release_node(ic, ni);
	splx(s);
}

void
urtwn_rxeof(struct usbd_xfer *xfer, void *priv,
    usbd_status status)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct urtwn_rx_data *data = priv;
	struct urtwn_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;
	struct r92c_rx_desc_usb *rxd;
	uint32_t rxdw0;
	uint8_t *buf;
	int len, totlen, pktlen, infosz, npkts, error, align;

	if (__predict_false(status != USBD_NORMAL_COMPLETION)) {
		DPRINTF(("RX status=%d\n", status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->rx_pipe);
		if (status != USBD_CANCELLED)
			goto resubmit;
		return;
	}
	usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);

	if (__predict_false(len < sizeof(*rxd))) {
		DPRINTF(("xfer too short %d\n", len));
		goto resubmit;
	}
	buf = data->buf;

	/* Get the number of encapsulated frames. */
	rxd = (struct r92c_rx_desc_usb *)buf;
	npkts = MS(letoh32(rxd->rxdw2), R92C_RXDW2_PKTCNT);
	DPRINTFN(4, ("Rx %d frames in one chunk\n", npkts));

	if (sc->sc_sc.chip & RTWN_CHIP_88E) {
		int ntries, type;
		struct r88e_tx_rpt_ccx *rxstat;

		type = MS(letoh32(rxd->rxdw3), R88E_RXDW3_RPT);

		if (type == R88E_RXDW3_RPT_TX1) {
			buf += sizeof(struct r92c_rx_desc_usb);
			rxstat = (struct r88e_tx_rpt_ccx *)buf;
			ntries = MS(letoh32(rxstat->rptb2),
			    R88E_RPTB2_RETRY_CNT);

			if (rxstat->rptb1 & R88E_RPTB1_PKT_OK)
				sc->amn.amn_txcnt++;
			if (ntries > 0)
				sc->amn.amn_retrycnt++;

			goto resubmit;
		}
	} else if (sc->sc_sc.chip & (RTWN_CHIP_88F | RTWN_CHIP_92E)) {
		int type;
		struct r92e_c2h_tx_rpt *txrpt;

		if (letoh32(rxd->rxdw2) & R92E_RXDW2_RPT_C2H) {
			if (len < sizeof(struct r92c_rx_desc_usb) + 2)
				goto resubmit;

			type = buf[sizeof(struct r92c_rx_desc_usb)];
			switch (type) {
			case R92C_C2HEVT_TX_REPORT:
				buf += sizeof(struct r92c_rx_desc_usb) + 2;
				txrpt = (struct r92e_c2h_tx_rpt *)buf;
				if (MS(txrpt->rptb2, R92E_RPTB2_RETRY_CNT) > 0)
					sc->amn.amn_retrycnt++;
				if ((txrpt->rptb0 & (R92E_RPTB0_RETRY_OVER |
				    R92E_RPTB0_LIFE_EXPIRE)) == 0)
					sc->amn.amn_txcnt++;
				break;
			default:
				break;
			}
			goto resubmit;
		}
	}

	align = ((sc->sc_sc.chip & (RTWN_CHIP_88F | RTWN_CHIP_92E)) ? 7 : 127);

	/* Process all of them. */
	while (npkts-- > 0) {
		if (__predict_false(len < sizeof(*rxd)))
			break;
		rxd = (struct r92c_rx_desc_usb *)buf;
		rxdw0 = letoh32(rxd->rxdw0);

		pktlen = MS(rxdw0, R92C_RXDW0_PKTLEN);
		if (__predict_false(pktlen == 0))
			break;

		infosz = MS(rxdw0, R92C_RXDW0_INFOSZ) * 8;

		/* Make sure everything fits in xfer. */
		totlen = sizeof(*rxd) + infosz + pktlen;
		if (__predict_false(totlen > len))
			break;

		/* Process 802.11 frame. */
		urtwn_rx_frame(sc, buf, pktlen, &ml);

		/* Handle chunk alignment. */
		totlen = (totlen + align) & ~align;
		buf += totlen;
		len -= totlen;
	}
	if_input(&ic->ic_if, &ml);

 resubmit:
	/* Setup a new transfer. */
	usbd_setup_xfer(xfer, sc->rx_pipe, data, data->buf, URTWN_RXBUFSZ,
	    USBD_SHORT_XFER_OK | USBD_NO_COPY, USBD_NO_TIMEOUT, urtwn_rxeof);
	error = usbd_transfer(data->xfer);
	if (error != 0 && error != USBD_IN_PROGRESS)
		DPRINTF(("could not set up new transfer: %d\n", error));
}

void
urtwn_txeof(struct usbd_xfer *xfer, void *priv,
    usbd_status status)
{
	struct urtwn_tx_data *data = priv;
	struct urtwn_softc *sc = data->sc;
	struct ifnet *ifp = &sc->sc_sc.sc_ic.ic_if;
	int s;

	s = splnet();
	/* Put this Tx buffer back to our free list. */
	TAILQ_INSERT_TAIL(&sc->tx_free_list, data, next);

	if (__predict_false(status != USBD_NORMAL_COMPLETION)) {
		DPRINTF(("TX status=%d\n", status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(data->pipe);
		ifp->if_oerrors++;
		splx(s);
		return;
	}
	sc->sc_sc.sc_tx_timer = 0;

	/* We just released a Tx buffer, notify Tx. */
	if (ifq_is_oactive(&ifp->if_snd)) {
		ifq_clr_oactive(&ifp->if_snd);
		rtwn_start(ifp);
	}
	splx(s);
}

void
urtwn_tx_fill_desc(struct urtwn_softc *sc, uint8_t **txdp, struct mbuf *m,
    struct ieee80211_frame *wh, struct ieee80211_key *k,
    struct ieee80211_node *ni)
{
	struct r92c_tx_desc_usb *txd;
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;
	uint8_t raid, type, rtsrate;
	uint32_t pktlen;

	txd = (struct r92c_tx_desc_usb *)*txdp;
	(*txdp) += sizeof(*txd);
	memset(txd, 0, sizeof(*txd));

	pktlen = m->m_pkthdr.len;
	if (k != NULL && k->k_cipher == IEEE80211_CIPHER_CCMP) {
		txd->txdw1 |= htole32(SM(R92C_TXDW1_CIPHER,
		    R92C_TXDW1_CIPHER_AES));
		pktlen += IEEE80211_CCMP_HDRLEN;
	}

	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

	txd->txdw0 |= htole32(
	    SM(R92C_TXDW0_PKTLEN, pktlen) |
	    SM(R92C_TXDW0_OFFSET, sizeof(*txd)) |
	    R92C_TXDW0_OWN | R92C_TXDW0_FSG | R92C_TXDW0_LSG);
	if (IEEE80211_IS_MULTICAST(wh->i_addr1))
		txd->txdw0 |= htole32(R92C_TXDW0_BMCAST);

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
			/* Request TX status report for AMRR */
			txd->txdw2 |= htole32(R92C_TXDW2_CCX_RPT);
		} else {
			txd->txdw1 |= htole32(
			    SM(R92C_TXDW1_MACID, R92C_MACID_BSS) |
			    SM(R92C_TXDW1_QSEL, R92C_TXDW1_QSEL_BE) |
			    SM(R92C_TXDW1_RAID, raid) | R92C_TXDW1_AGGBK);
		}

		if (pktlen + IEEE80211_CRC_LEN > ic->ic_rtsthreshold) {
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
		txd->txdw5 |= htole32(0x0001ff00);

		if (ic->ic_curmode == IEEE80211_MODE_11B)
			rtsrate = 0; /* CCK1 */
		else
			rtsrate = 8; /* OFDM24 */

		if (sc->sc_sc.chip & RTWN_CHIP_88E) {
			/* Use AMRR */
			txd->txdw4 |= htole32(R92C_TXDW4_DRVRATE);
			txd->txdw4 |= htole32(SM(R92C_TXDW4_RTSRATE, rtsrate));
			txd->txdw5 |= htole32(SM(R92C_TXDW5_DATARATE,
			    ni->ni_txrate));
		} else {
			/* Send data at OFDM54. */
			txd->txdw4 |= htole32(SM(R92C_TXDW4_RTSRATE, rtsrate));
			txd->txdw5 |= htole32(SM(R92C_TXDW5_DATARATE, 11));
		}
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
	txd->txdseq |= (*(uint16_t *)wh->i_seq) >> IEEE80211_SEQ_SEQ_SHIFT;

	if (!ieee80211_has_qos(wh)) {
		/* Use HW sequence numbering for non-QoS frames. */
		txd->txdw4  |= htole32(R92C_TXDW4_HWSEQ);
		txd->txdseq |= htole16(R92C_TXDW3_HWSEQEN);
	} else
		txd->txdw4 |= htole32(R92C_TXDW4_QOS);
}

void
urtwn_tx_fill_desc_gen2(struct urtwn_softc *sc, uint8_t **txdp, struct mbuf *m,
    struct ieee80211_frame *wh, struct ieee80211_key *k,
    struct ieee80211_node *ni)
{
	struct r92e_tx_desc_usb *txd;
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;
	uint8_t raid, type;
	uint32_t pktlen;

	txd = (struct r92e_tx_desc_usb *)*txdp;
	(*txdp) += sizeof(*txd);
	memset(txd, 0, sizeof(*txd));

	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

	pktlen = m->m_pkthdr.len;
	if (k != NULL && k->k_cipher == IEEE80211_CIPHER_CCMP) {
		txd->txdw1 |= htole32(SM(R92C_TXDW1_CIPHER,
		    R92C_TXDW1_CIPHER_AES));
		pktlen += IEEE80211_CCMP_HDRLEN;
	}

	txd->txdw0 |= htole32(
	    SM(R92C_TXDW0_PKTLEN, pktlen) |
	    SM(R92C_TXDW0_OFFSET, sizeof(*txd)) |
	    R92C_TXDW0_OWN | R92C_TXDW0_FSG | R92C_TXDW0_LSG);
	if (IEEE80211_IS_MULTICAST(wh->i_addr1))
		txd->txdw0 |= htole32(R92C_TXDW0_BMCAST);

	if (!IEEE80211_IS_MULTICAST(wh->i_addr1) &&
	    type == IEEE80211_FC0_TYPE_DATA) {
		if (ic->ic_curmode == IEEE80211_MODE_11B ||
		    (sc->sc_sc.sc_flags & RTWN_FLAG_FORCE_RAID_11B))
			raid = R92E_RAID_11B;
		else
			raid = R92E_RAID_11BG;
		txd->txdw1 |= htole32(
		    SM(R92E_TXDW1_MACID, R92C_MACID_BSS) |
		    SM(R92C_TXDW1_QSEL, R92C_TXDW1_QSEL_BE) |
		    SM(R92C_TXDW1_RAID, raid));
		/* Request TX status report for AMRR */
		txd->txdw2 |= htole32(R92C_TXDW2_CCX_RPT | R88E_TXDW2_AGGBK);

		if (pktlen + IEEE80211_CRC_LEN > ic->ic_rtsthreshold) {
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
		txd->txdw5 |= htole32(0x0001ff00);

		/* Use AMRR */
		txd->txdw3 |= htole32(R92E_TXDW3_DRVRATE);
		txd->txdw4 |= htole32(SM(R92E_TXDW4_RTSRATE, 8));
		txd->txdw4 |= htole32(SM(R92E_TXDW4_DATARATE, ni->ni_txrate));
	} else {
		txd->txdw1 |= htole32(
		    SM(R92E_TXDW1_MACID, 0) |
		    SM(R92C_TXDW1_QSEL, R92C_TXDW1_QSEL_MGNT) |
		    SM(R92C_TXDW1_RAID, R92E_RAID_11B));

		/* Force CCK1. */
		txd->txdw3 |= htole32(R92E_TXDW3_DRVRATE);
		txd->txdw4 |= htole32(SM(R92E_TXDW4_DATARATE, 0));
	}
	txd->txdw4 |= htole32(SM(R92E_TXDW4_DATARATEFB, 0x1f));

	txd->txdseq2 |= htole16(SM(R92E_TXDSEQ2_HWSEQ, *(uint16_t *)wh->i_seq));

	if (!ieee80211_has_qos(wh)) {
		/* Use HW sequence numbering for non-QoS frames. */
		txd->txdw7 |= htole16(R92C_TXDW3_HWSEQEN);
	}
}

int
urtwn_tx(void *cookie, struct mbuf *m, struct ieee80211_node *ni)
{
	struct urtwn_softc *sc = cookie;
	struct ieee80211com *ic = &sc->sc_sc.sc_ic;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k = NULL;
	struct urtwn_tx_data *data;
	struct usbd_pipe *pipe;
	uint16_t qos, sum;
	uint8_t tid, qid;
	int i, xferlen, error, headerlen;
	uint8_t *txdp;

	wh = mtod(m, struct ieee80211_frame *);

	if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
		k = ieee80211_get_txkey(ic, wh, ni);
		if (k->k_cipher != IEEE80211_CIPHER_CCMP) {
			if ((m = ieee80211_encrypt(ic, m, k)) == NULL)
				return (ENOBUFS);
			wh = mtod(m, struct ieee80211_frame *);
		}
	}

	if (ieee80211_has_qos(wh)) {
		qos = ieee80211_get_qos(wh);
		tid = qos & IEEE80211_QOS_TID;
		qid = ieee80211_up_to_ac(ic, tid);
	} else if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK)
	    != IEEE80211_FC0_TYPE_DATA) {
		/* Use AC VO for management frames. */
		qid = EDCA_AC_VO;
	} else
		qid = EDCA_AC_BE;

	/* Get the USB pipe to use for this AC. */
	pipe = sc->tx_pipe[sc->ac2idx[qid]];

	/* Grab a Tx buffer from our free list. */
	data = TAILQ_FIRST(&sc->tx_free_list);
	TAILQ_REMOVE(&sc->tx_free_list, data, next);

	/* Fill Tx descriptor. */
	txdp = data->buf;
	if (sc->sc_sc.chip & (RTWN_CHIP_88F | RTWN_CHIP_92E))
		urtwn_tx_fill_desc_gen2(sc, &txdp, m, wh, k, ni);
	else
		urtwn_tx_fill_desc(sc, &txdp, m, wh, k, ni);

	/* Compute Tx descriptor checksum. */
	sum = 0;
	for (i = 0; i < R92C_TXDESC_SUMSIZE / 2; i++)
		sum ^= ((uint16_t *)data->buf)[i];
	((uint16_t *)data->buf)[R92C_TXDESC_SUMOFFSET] = sum;

#if NBPFILTER > 0
	if (__predict_false(sc->sc_drvbpf != NULL)) {
		struct urtwn_tx_radiotap_header *tap = &sc->sc_txtap;
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

	if (k != NULL && k->k_cipher == IEEE80211_CIPHER_CCMP) {
		xferlen = (txdp - data->buf) + m->m_pkthdr.len +
		    IEEE80211_CCMP_HDRLEN;
		headerlen = ieee80211_get_hdrlen(wh);

		m_copydata(m, 0, headerlen, txdp);
		txdp += headerlen;

		k->k_tsc++;
		txdp[0] = k->k_tsc;
		txdp[1] = k->k_tsc >> 8;
		txdp[2] = 0;
		txdp[3] = k->k_id | IEEE80211_WEP_EXTIV;
		txdp[4] = k->k_tsc >> 16;
		txdp[5] = k->k_tsc >> 24;
		txdp[6] = k->k_tsc >> 32;
		txdp[7] = k->k_tsc >> 40;
		txdp += IEEE80211_CCMP_HDRLEN;

		m_copydata(m, headerlen, m->m_pkthdr.len - headerlen, txdp);
		m_freem(m);
	} else {
		xferlen = (txdp - data->buf) + m->m_pkthdr.len;
		m_copydata(m, 0, m->m_pkthdr.len, txdp);
		m_freem(m);
	}

	data->pipe = pipe;
	usbd_setup_xfer(data->xfer, pipe, data, data->buf, xferlen,
	    USBD_FORCE_SHORT_XFER | USBD_NO_COPY, URTWN_TX_TIMEOUT,
	    urtwn_txeof);
	error = usbd_transfer(data->xfer);
	if (__predict_false(error != USBD_IN_PROGRESS && error != 0)) {
		/* Put this Tx buffer back to our free list. */
		TAILQ_INSERT_TAIL(&sc->tx_free_list, data, next);
		return (error);
	}
	ieee80211_release_node(ic, ni);
	return (0);
}

int
urtwn_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct rtwn_softc *sc_sc = ifp->if_softc;
	struct device *self = sc_sc->sc_pdev;
	struct urtwn_softc *sc = (struct urtwn_softc *)self;
	int error;

	if (usbd_is_dying(sc->sc_udev))
		return ENXIO;

	usbd_ref_incr(sc->sc_udev);
	error = rtwn_ioctl(ifp, cmd, data);
	usbd_ref_decr(sc->sc_udev);

	return (error);
}

int
urtwn_r92c_power_on(struct urtwn_softc *sc)
{
	uint32_t reg;
	int ntries;

	/* Wait for autoload done bit. */
	for (ntries = 0; ntries < 1000; ntries++) {
		if (urtwn_read_1(sc, R92C_APS_FSMCO) & R92C_APS_FSMCO_PFM_ALDN)
			break;
		DELAY(5);
	}
	if (ntries == 1000) {
		printf("%s: timeout waiting for chip autoload\n",
		    sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}

	/* Unlock ISO/CLK/Power control register. */
	urtwn_write_1(sc, R92C_RSV_CTRL, 0);
	/* Move SPS into PWM mode. */
	urtwn_write_1(sc, R92C_SPS0_CTRL, 0x2b);
	DELAY(100);

	reg = urtwn_read_1(sc, R92C_LDOV12D_CTRL);
	if (!(reg & R92C_LDOV12D_CTRL_LDV12_EN)) {
		urtwn_write_1(sc, R92C_LDOV12D_CTRL,
		    reg | R92C_LDOV12D_CTRL_LDV12_EN);
		DELAY(100);
		urtwn_write_1(sc, R92C_SYS_ISO_CTRL,
		    urtwn_read_1(sc, R92C_SYS_ISO_CTRL) &
		    ~R92C_SYS_ISO_CTRL_MD2PP);
	}

	/* Auto enable WLAN. */
	urtwn_write_2(sc, R92C_APS_FSMCO,
	    urtwn_read_2(sc, R92C_APS_FSMCO) | R92C_APS_FSMCO_APFM_ONMAC);
	for (ntries = 0; ntries < 1000; ntries++) {
		if (!(urtwn_read_2(sc, R92C_APS_FSMCO) &
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
	urtwn_write_2(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_AFSM_HSUS |
	    R92C_APS_FSMCO_PDN_EN |
	    R92C_APS_FSMCO_PFM_ALDN);
	/* Release RF digital isolation. */
	urtwn_write_2(sc, R92C_SYS_ISO_CTRL,
	    urtwn_read_2(sc, R92C_SYS_ISO_CTRL) & ~R92C_SYS_ISO_CTRL_DIOR);

	/* Enable MAC DMA/WMAC/SCHEDULE/SEC blocks. */
	reg = urtwn_read_2(sc, R92C_CR);
	reg |= R92C_CR_HCI_TXDMA_EN | R92C_CR_HCI_RXDMA_EN |
	    R92C_CR_TXDMA_EN | R92C_CR_RXDMA_EN | R92C_CR_PROTOCOL_EN |
	    R92C_CR_SCHEDULE_EN | R92C_CR_MACTXEN | R92C_CR_MACRXEN |
	    R92C_CR_ENSEC;
	urtwn_write_2(sc, R92C_CR, reg);

	urtwn_write_1(sc, 0xfe10, 0x19);
	return (0);
}

int
urtwn_r92e_power_on(struct urtwn_softc *sc)
{
	uint32_t reg;
	int ntries;

	if (urtwn_read_4(sc, R92C_SYS_CFG) & R92E_SYS_CFG_SPSLDO_SEL) {
		/* LDO. */
		urtwn_write_1(sc, R92E_LDO_SWR_CTRL, 0xc3);
	} else {
		reg = urtwn_read_4(sc, R92C_SYS_SWR_CTRL2);
		reg &= 0xff0fffff;
		reg |= 0x00500000;
		urtwn_write_4(sc, R92C_SYS_SWR_CTRL2, reg);
		urtwn_write_1(sc, R92E_LDO_SWR_CTRL, 0x83);
	}

	/* 40MHz crystal source */
	urtwn_write_1(sc, R92C_AFE_PLL_CTRL,
	    urtwn_read_1(sc, R92C_AFE_PLL_CTRL) & 0xfb);
	urtwn_write_4(sc, R92C_AFE_XTAL_CTRL_EXT,
	    urtwn_read_4(sc, R92C_AFE_XTAL_CTRL_EXT) & 0xfffffc7f);

	urtwn_write_1(sc, R92C_AFE_PLL_CTRL,
	    urtwn_read_1(sc, R92C_AFE_PLL_CTRL) & 0xbf);
	urtwn_write_4(sc, R92C_AFE_XTAL_CTRL_EXT,
	    urtwn_read_4(sc, R92C_AFE_XTAL_CTRL_EXT) & 0xffdfffff);

	/* Disable HWPDN. */
	urtwn_write_2(sc, R92C_APS_FSMCO,
	    urtwn_read_2(sc, R92C_APS_FSMCO) & ~R92C_APS_FSMCO_APDM_HPDN);
	for (ntries = 0; ntries < 5000; ntries++) {
		if (urtwn_read_4(sc, R92C_APS_FSMCO) & R92C_APS_FSMCO_SUS_HOST)
			break;
		DELAY(10);
	}
	if (ntries == 5000) {
		printf("%s: timeout waiting for chip power up\n",
		    sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}

	/* Disable WL suspend. */
	urtwn_write_2(sc, R92C_APS_FSMCO,
	    urtwn_read_2(sc, R92C_APS_FSMCO) &
	    ~(R92C_APS_FSMCO_AFSM_HSUS | R92C_APS_FSMCO_AFSM_PCIE));

	/* Auto enable WLAN. */
	urtwn_write_4(sc, R92C_APS_FSMCO,
	    urtwn_read_4(sc, R92C_APS_FSMCO) | R92C_APS_FSMCO_RDY_MACON);
	urtwn_write_2(sc, R92C_APS_FSMCO,
	    urtwn_read_2(sc, R92C_APS_FSMCO) | R92C_APS_FSMCO_APFM_ONMAC);
	for (ntries = 0; ntries < 5000; ntries++) {
		if (!(urtwn_read_2(sc, R92C_APS_FSMCO) &
		    R92C_APS_FSMCO_APFM_ONMAC))
			break;
		DELAY(10);
	}
	if (ntries == 5000) {
		printf("%s: timeout waiting for MAC auto ON\n",
		    sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}

	/* Enable MAC DMA/WMAC/SCHEDULE/SEC blocks. */
	urtwn_write_2(sc, R92C_CR, 0);
	reg = urtwn_read_2(sc, R92C_CR);
	reg |= R92C_CR_HCI_TXDMA_EN | R92C_CR_HCI_RXDMA_EN |
	    R92C_CR_TXDMA_EN | R92C_CR_RXDMA_EN | R92C_CR_PROTOCOL_EN |
	    R92C_CR_SCHEDULE_EN | R92C_CR_ENSEC | R92C_CR_CALTMR_EN;
	urtwn_write_2(sc, R92C_CR, reg);
	return (0);
}

int
urtwn_r88e_power_on(struct urtwn_softc *sc)
{
	uint32_t reg;
	int ntries;

	/* Wait for power ready bit. */
	for (ntries = 0; ntries < 5000; ntries++) {
		if (urtwn_read_4(sc, R92C_APS_FSMCO) & R92C_APS_FSMCO_SUS_HOST)
			break;
		DELAY(10);
	}
	if (ntries == 5000) {
		printf("%s: timeout waiting for chip power up\n",
		    sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}

	/* Reset BB. */
	urtwn_write_1(sc, R92C_SYS_FUNC_EN,
	    urtwn_read_1(sc, R92C_SYS_FUNC_EN) & ~(R92C_SYS_FUNC_EN_BBRSTB |
	    R92C_SYS_FUNC_EN_BB_GLB_RST));

	urtwn_write_1(sc, R92C_AFE_XTAL_CTRL + 2,
	    urtwn_read_1(sc, R92C_AFE_XTAL_CTRL + 2) | 0x80);

	/* Disable HWPDN. */
	urtwn_write_2(sc, R92C_APS_FSMCO,
	    urtwn_read_2(sc, R92C_APS_FSMCO) & ~R92C_APS_FSMCO_APDM_HPDN);
	/* Disable WL suspend. */
	urtwn_write_2(sc, R92C_APS_FSMCO,
	    urtwn_read_2(sc, R92C_APS_FSMCO) &
	    ~(R92C_APS_FSMCO_AFSM_HSUS | R92C_APS_FSMCO_AFSM_PCIE));

	/* Auto enable WLAN. */
	urtwn_write_2(sc, R92C_APS_FSMCO,
	    urtwn_read_2(sc, R92C_APS_FSMCO) | R92C_APS_FSMCO_APFM_ONMAC);
	for (ntries = 0; ntries < 5000; ntries++) {
		if (!(urtwn_read_2(sc, R92C_APS_FSMCO) &
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
	urtwn_write_1(sc, R92C_LPLDO_CTRL,
	    urtwn_read_1(sc, R92C_LPLDO_CTRL) & ~0x10);

	/* Enable MAC DMA/WMAC/SCHEDULE/SEC blocks. */
	urtwn_write_2(sc, R92C_CR, 0);
	reg = urtwn_read_2(sc, R92C_CR);
	reg |= R92C_CR_HCI_TXDMA_EN | R92C_CR_HCI_RXDMA_EN |
	    R92C_CR_TXDMA_EN | R92C_CR_RXDMA_EN | R92C_CR_PROTOCOL_EN |
	    R92C_CR_SCHEDULE_EN | R92C_CR_ENSEC | R92C_CR_CALTMR_EN;
	urtwn_write_2(sc, R92C_CR, reg);
	return (0);
}

int
urtwn_r88f_power_on(struct urtwn_softc *sc)
{
	uint32_t reg;
	int ntries;

	/* Enable WL suspend. */
	urtwn_write_2(sc, R92C_APS_FSMCO,
	    urtwn_read_2(sc, R92C_APS_FSMCO) &
	    ~(R92C_APS_FSMCO_AFSM_HSUS | R92C_APS_FSMCO_AFSM_PCIE));
	/* Turn off USB APHY LDO under suspend mode. */
	urtwn_write_1(sc, 0xc4, urtwn_read_1(sc, 0xc4) & ~0x10);

	/* Disable SW LPS. */
	urtwn_write_2(sc, R92C_APS_FSMCO,
	    urtwn_read_2(sc, R92C_APS_FSMCO) & ~R92C_APS_FSMCO_APFM_RSM);
	/* Wait for power ready bit. */
	for (ntries = 0; ntries < 5000; ntries++) {
		if (urtwn_read_4(sc, R92C_APS_FSMCO) & R92C_APS_FSMCO_SUS_HOST)
			break;
		DELAY(10);
	}
	if (ntries == 5000) {
		printf("%s: timeout waiting for chip power up\n",
		    sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}
	/* Disable HWPDN. */
	urtwn_write_2(sc, R92C_APS_FSMCO,
	    urtwn_read_2(sc, R92C_APS_FSMCO) & ~R92C_APS_FSMCO_APDM_HPDN);
	/* Disable WL suspend. */
	urtwn_write_2(sc, R92C_APS_FSMCO,
	    urtwn_read_2(sc, R92C_APS_FSMCO) & ~R92C_APS_FSMCO_AFSM_HSUS);
	/* Auto enable WLAN. */
	urtwn_write_2(sc, R92C_APS_FSMCO,
	    urtwn_read_2(sc, R92C_APS_FSMCO) | R92C_APS_FSMCO_APFM_ONMAC);
	for (ntries = 0; ntries < 5000; ntries++) {
		if (!(urtwn_read_2(sc, R92C_APS_FSMCO) &
		    R92C_APS_FSMCO_APFM_ONMAC))
			break;
		DELAY(10);
	}
	if (ntries == 5000) {
		printf("%s: timeout waiting for MAC auto ON\n",
		    sc->sc_dev.dv_xname);
		return (ETIMEDOUT);
	}
	/* Reduce RF noise. */
	urtwn_write_1(sc, R92C_AFE_LDO_CTRL, 0x35);

	/* Enable MAC DMA/WMAC/SCHEDULE/SEC blocks. */
	urtwn_write_2(sc, R92C_CR, 0);
	reg = urtwn_read_2(sc, R92C_CR);
	reg |= R92C_CR_HCI_TXDMA_EN | R92C_CR_HCI_RXDMA_EN |
	    R92C_CR_TXDMA_EN | R92C_CR_RXDMA_EN | R92C_CR_PROTOCOL_EN |
	    R92C_CR_SCHEDULE_EN | R92C_CR_ENSEC | R92C_CR_CALTMR_EN;
	urtwn_write_2(sc, R92C_CR, reg);
	return (0);
}

int
urtwn_llt_init(struct urtwn_softc *sc, int page_count)
{
	int i, error, pktbuf_count;

	pktbuf_count = (sc->sc_sc.chip & RTWN_CHIP_88E) ?
	    R88E_TXPKTBUF_COUNT : R92C_TXPKTBUF_COUNT;

	/* Reserve pages [0; page_count]. */
	for (i = 0; i < page_count; i++) {
		if ((error = urtwn_llt_write(sc, i, i + 1)) != 0)
			return (error);
	}
	/* NB: 0xff indicates end-of-list. */
	if ((error = urtwn_llt_write(sc, i, 0xff)) != 0)
		return (error);
	/*
	 * Use pages [page_count + 1; pktbuf_count - 1]
	 * as ring buffer.
	 */
	for (++i; i < pktbuf_count - 1; i++) {
		if ((error = urtwn_llt_write(sc, i, i + 1)) != 0)
			return (error);
	}
	/* Make the last page point to the beginning of the ring buffer. */
	error = urtwn_llt_write(sc, i, page_count + 1);
	return (error);
}

int
urtwn_auto_llt_init(struct urtwn_softc *sc)
{
	int ntries;

	urtwn_write_4(sc, R92E_AUTO_LLT,
	    urtwn_read_4(sc, R92E_AUTO_LLT) | R92E_AUTO_LLT_EN);
	for (ntries = 0; ntries < 1000; ntries++) {
		if (!(urtwn_read_4(sc, R92E_AUTO_LLT) & R92E_AUTO_LLT_EN))
			return (0);
		DELAY(2);
	}

	return (ETIMEDOUT);
}

int
urtwn_fw_loadpage(void *cookie, int page, uint8_t *buf, int len)
{
	struct urtwn_softc *sc = cookie;
	uint32_t reg;
	int maxblksz, off, mlen, error = 0;

	reg = urtwn_read_4(sc, R92C_MCUFWDL);
	reg = RW(reg, R92C_MCUFWDL_PAGE, page);
	urtwn_write_4(sc, R92C_MCUFWDL, reg);

	maxblksz = (sc->sc_sc.chip & RTWN_CHIP_92E) ? 254 : 196;

	off = R92C_FW_START_ADDR;
	while (len > 0) {
		if (len > maxblksz)
			mlen = maxblksz;
		else if (len > 4)
			mlen = 4;
		else
			mlen = 1;
		error = urtwn_write_region_1(sc, off, buf, mlen);
		if (error != 0)
			break;
		off += mlen;
		buf += mlen;
		len -= mlen;
	}
	return (error);
}

int
urtwn_load_firmware(void *cookie, u_char **fw, size_t *len)
{
	struct urtwn_softc *sc = cookie;
	const char *name;
	int error;

	if (sc->sc_sc.chip & RTWN_CHIP_92E)
		name = "urtwn-rtl8192eu";
	else if (sc->sc_sc.chip & RTWN_CHIP_88E)
		name = "urtwn-rtl8188eu";
	else if (sc->sc_sc.chip & RTWN_CHIP_88F)
		name = "urtwn-rtl8188ftv";
	else if ((sc->sc_sc.chip & (RTWN_CHIP_UMC_A_CUT | RTWN_CHIP_92C)) ==
		    RTWN_CHIP_UMC_A_CUT)
		name = "urtwn-rtl8192cU";
	else
		name = "urtwn-rtl8192cT";

	error = loadfirmware(name, fw, len);
	if (error)
		printf("%s: could not read firmware %s (error %d)\n",
		    sc->sc_dev.dv_xname, name, error);
	return (error);
}

int
urtwn_dma_init(void *cookie)
{
	struct urtwn_softc *sc = cookie;
	uint32_t reg;
	uint16_t dmasize;
	int hqpages, lqpages, nqpages, pagecnt, boundary;
	int error, hashq, haslq, hasnq;

	/* Default initialization of chipset values. */
	if (sc->sc_sc.chip & RTWN_CHIP_88E) {
		hqpages = R88E_HQ_NPAGES;
		lqpages = R88E_LQ_NPAGES;
		nqpages = R88E_NQ_NPAGES;
		pagecnt = R88E_TX_PAGE_COUNT;
		dmasize = R88E_MAX_RX_DMA_SIZE;
	} else if (sc->sc_sc.chip & RTWN_CHIP_88F) {
		hqpages = R88F_HQ_NPAGES;
		lqpages = R88F_LQ_NPAGES;
		nqpages = R88F_NQ_NPAGES;
		pagecnt = R88F_TX_PAGE_COUNT;
		dmasize = R88F_MAX_RX_DMA_SIZE;
	} else if (sc->sc_sc.chip & RTWN_CHIP_92E) {
		hqpages = R92E_HQ_NPAGES;
		lqpages = R92E_LQ_NPAGES;
		nqpages = R92E_NQ_NPAGES;
		pagecnt = R92E_TX_PAGE_COUNT;
		dmasize = R92E_MAX_RX_DMA_SIZE;
	} else {
		hqpages = R92C_HQ_NPAGES;
		lqpages = R92C_LQ_NPAGES;
		nqpages = R92C_NQ_NPAGES;
		pagecnt = R92C_TX_PAGE_COUNT;
		dmasize = R92C_MAX_RX_DMA_SIZE;
	}
	boundary = pagecnt + 1;

	/* Initialize LLT table. */
	if (sc->sc_sc.chip & (RTWN_CHIP_88F | RTWN_CHIP_92E))
		error = urtwn_auto_llt_init(sc);
	else
		error = urtwn_llt_init(sc, pagecnt);
	if (error != 0)
		return (error);

	/* Get Tx queues to USB endpoints mapping. */
	hashq = hasnq = haslq = 0;
	switch (sc->ntx) {
	case 3:
		haslq = 1;
		pagecnt -= lqpages;
		/* FALLTHROUGH */
	case 2:
		hasnq = 1;
		pagecnt -= nqpages;
		/* FALLTHROUGH */
	case 1:
		hashq = 1;
		pagecnt -= hqpages;
		break;
	}

	/* Set number of pages for normal priority queue. */
	urtwn_write_1(sc, R92C_RQPN_NPQ, hasnq ? nqpages : 0);
	urtwn_write_4(sc, R92C_RQPN,
	    /* Set number of pages for public queue. */
	    SM(R92C_RQPN_PUBQ, pagecnt) |
	    /* Set number of pages for high priority queue. */
	    SM(R92C_RQPN_HPQ, hashq ? hqpages : 0) |
	    /* Set number of pages for low priority queue. */
	    SM(R92C_RQPN_LPQ, haslq ? lqpages : 0) |
	    /* Load values. */
	    R92C_RQPN_LD);

	urtwn_write_1(sc, R92C_TXPKTBUF_BCNQ_BDNY, boundary);
	urtwn_write_1(sc, R92C_TXPKTBUF_MGQ_BDNY, boundary);
	urtwn_write_1(sc, R92C_TXPKTBUF_WMAC_LBK_BF_HD, boundary);
	urtwn_write_1(sc, R92C_TRXFF_BNDY, boundary);
	urtwn_write_1(sc, R92C_TDECTRL + 1, boundary);

	/* Set queue to USB pipe mapping. */
	reg = urtwn_read_2(sc, R92C_TRXDMA_CTRL);
	reg &= ~R92C_TRXDMA_CTRL_QMAP_M;
	if (haslq)
		reg |= R92C_TRXDMA_CTRL_QMAP_3EP;
	else if (hashq) {
		if (!hasnq)
			reg |= R92C_TRXDMA_CTRL_QMAP_HQ;
		else
			reg |= R92C_TRXDMA_CTRL_QMAP_HQ_NQ;
	}
	urtwn_write_2(sc, R92C_TRXDMA_CTRL, reg);

	/* Set Tx/Rx transfer page boundary. */
	urtwn_write_2(sc, R92C_TRXFF_BNDY + 2, dmasize - 1);

	if (!(sc->sc_sc.chip & RTWN_CHIP_92E)) {
		/* Set Tx/Rx transfer page size. */
		if (sc->sc_sc.chip & RTWN_CHIP_88F) {
			urtwn_write_1(sc, R92C_PBP,
			    SM(R92C_PBP_PSRX, R92C_PBP_256) |
			    SM(R92C_PBP_PSTX, R92C_PBP_256));
		} else {
			urtwn_write_1(sc, R92C_PBP,
			    SM(R92C_PBP_PSRX, R92C_PBP_128) |
			    SM(R92C_PBP_PSTX, R92C_PBP_128));
		}
	}
	return (error);
}

void
urtwn_aggr_init(void *cookie)
{
	struct urtwn_softc *sc = cookie;
	uint32_t reg = 0;
	int dmasize, dmatiming, ndesc;

	/* Set burst packet length. */
	if (sc->sc_sc.chip & (RTWN_CHIP_88F | RTWN_CHIP_92E))
		urtwn_burstlen_init(sc);

	if (sc->sc_sc.chip & RTWN_CHIP_88F) {
		dmasize = 5;
		dmatiming = 32;
		ndesc = 6;
	} else if (sc->sc_sc.chip & RTWN_CHIP_92E) {
		dmasize = 6;
		dmatiming = 32;
		ndesc = 3;
	} else {
		dmasize = 48;
		dmatiming = 4;
		ndesc = (sc->sc_sc.chip & RTWN_CHIP_88E) ? 1 : 6;
	}

	/* Tx aggregation setting. */
	reg = urtwn_read_4(sc, R92C_TDECTRL);
	reg = RW(reg, R92C_TDECTRL_BLK_DESC_NUM, ndesc);
	urtwn_write_4(sc, R92C_TDECTRL, reg);
	if (sc->sc_sc.chip & (RTWN_CHIP_88F | RTWN_CHIP_92E))
		urtwn_write_1(sc, R92E_DWBCN1_CTRL, ndesc << 1);

	/* Rx aggregation setting. */
	urtwn_write_1(sc, R92C_TRXDMA_CTRL,
	    urtwn_read_1(sc, R92C_TRXDMA_CTRL) | R92C_TRXDMA_CTRL_RXDMA_AGG_EN);

	urtwn_write_1(sc, R92C_RXDMA_AGG_PG_TH, dmasize);
	if (sc->sc_sc.chip & (RTWN_CHIP_92C | RTWN_CHIP_88C))
		urtwn_write_1(sc, R92C_USB_DMA_AGG_TO, dmatiming);
	else
		urtwn_write_1(sc, R92C_RXDMA_AGG_PG_TH + 1, dmatiming);

	if (sc->sc_sc.chip & RTWN_CHIP_88F) {
		urtwn_write_1(sc, R92E_RXDMA_PRO,
		    urtwn_read_1(sc, R92E_RXDMA_PRO) | R92E_RXDMA_PRO_DMA_MODE);
	}

	/* Drop incorrect bulk out. */
	urtwn_write_4(sc, R92C_TXDMA_OFFSET_CHK,
	    urtwn_read_4(sc, R92C_TXDMA_OFFSET_CHK) |
	    R92C_TXDMA_OFFSET_CHK_DROP_DATA_EN);
}

void
urtwn_mac_init(void *cookie)
{
	struct urtwn_softc *sc = cookie;
	int i;

	/* Write MAC initialization values. */
	if (sc->sc_sc.chip & RTWN_CHIP_88E) {
		for (i = 0; i < nitems(rtl8188eu_mac); i++) {
			urtwn_write_1(sc, rtl8188eu_mac[i].reg,
			    rtl8188eu_mac[i].val);
		}
		urtwn_write_1(sc, R92C_MAX_AGGR_NUM, 0x07);
	} else if (sc->sc_sc.chip & RTWN_CHIP_88F) {
		for (i = 0; i < nitems(rtl8188ftv_mac); i++) {
			urtwn_write_1(sc, rtl8188ftv_mac[i].reg,
			    rtl8188ftv_mac[i].val);
		}
	} else if (sc->sc_sc.chip & RTWN_CHIP_92E) {
		for (i = 0; i < nitems(rtl8192eu_mac); i++) {
			urtwn_write_1(sc, rtl8192eu_mac[i].reg,
			    rtl8192eu_mac[i].val);
		}
	} else {
		for (i = 0; i < nitems(rtl8192cu_mac); i++)
			urtwn_write_1(sc, rtl8192cu_mac[i].reg,
			    rtl8192cu_mac[i].val);
	}
}

void
urtwn_bb_init(void *cookie)
{
	struct urtwn_softc *sc = cookie;
	const struct r92c_bb_prog *prog;
	uint32_t reg;
	uint8_t xtal;
	int i;

	/* Enable BB and RF. */
	urtwn_write_2(sc, R92C_SYS_FUNC_EN,
	    urtwn_read_2(sc, R92C_SYS_FUNC_EN) |
	    R92C_SYS_FUNC_EN_BBRSTB | R92C_SYS_FUNC_EN_BB_GLB_RST |
	    R92C_SYS_FUNC_EN_DIO_RF);

	if (!(sc->sc_sc.chip & (RTWN_CHIP_88E | RTWN_CHIP_88F | RTWN_CHIP_92E)))
		urtwn_write_2(sc, R92C_AFE_PLL_CTRL, 0xdb83);

	urtwn_write_1(sc, R92C_RF_CTRL,
	    R92C_RF_CTRL_EN | R92C_RF_CTRL_RSTB | R92C_RF_CTRL_SDMRSTB);
	urtwn_write_1(sc, R92C_SYS_FUNC_EN,
	    R92C_SYS_FUNC_EN_USBA | R92C_SYS_FUNC_EN_USBD |
	    R92C_SYS_FUNC_EN_BB_GLB_RST | R92C_SYS_FUNC_EN_BBRSTB);

	if (!(sc->sc_sc.chip &
	    (RTWN_CHIP_88E | RTWN_CHIP_88F | RTWN_CHIP_92E))) {
		urtwn_write_1(sc, R92C_LDOHCI12_CTRL, 0x0f);
		urtwn_write_1(sc, 0x15, 0xe9);
		urtwn_write_1(sc, R92C_AFE_XTAL_CTRL + 1, 0x80);
	}

	/* Select BB programming based on board type. */
	if (sc->sc_sc.chip & RTWN_CHIP_88E)
		prog = &rtl8188eu_bb_prog;
	else if (sc->sc_sc.chip & RTWN_CHIP_88F)
		prog = &rtl8188ftv_bb_prog;
	else if (sc->sc_sc.chip & RTWN_CHIP_92E)
		prog = &rtl8192eu_bb_prog;
	else if (!(sc->sc_sc.chip & RTWN_CHIP_92C)) {
		if (sc->sc_sc.board_type == R92C_BOARD_TYPE_MINICARD)
			prog = &rtl8188ce_bb_prog;
		else if (sc->sc_sc.board_type == R92C_BOARD_TYPE_HIGHPA)
			prog = &rtl8188ru_bb_prog;
		else
			prog = &rtl8188cu_bb_prog;
	} else {
		if (sc->sc_sc.board_type == R92C_BOARD_TYPE_MINICARD)
			prog = &rtl8192ce_bb_prog;
		else
			prog = &rtl8192cu_bb_prog;
	}
	/* Write BB initialization values. */
	for (i = 0; i < prog->count; i++) {
		urtwn_bb_write(sc, prog->regs[i], prog->vals[i]);
		DELAY(1);
	}

	if (sc->sc_sc.chip & RTWN_CHIP_92C_1T2R) {
		/* 8192C 1T only configuration. */
		reg = urtwn_bb_read(sc, R92C_FPGA0_TXINFO);
		reg = (reg & ~0x00000003) | 0x2;
		urtwn_bb_write(sc, R92C_FPGA0_TXINFO, reg);

		reg = urtwn_bb_read(sc, R92C_FPGA1_TXINFO);
		reg = (reg & ~0x00300033) | 0x00200022;
		urtwn_bb_write(sc, R92C_FPGA1_TXINFO, reg);

		reg = urtwn_bb_read(sc, R92C_CCK0_AFESETTING);
		reg = (reg & ~0xff000000) | 0x45 << 24;
		urtwn_bb_write(sc, R92C_CCK0_AFESETTING, reg);

		reg = urtwn_bb_read(sc, R92C_OFDM0_TRXPATHENA);
		reg = (reg & ~0x000000ff) | 0x23;
		urtwn_bb_write(sc, R92C_OFDM0_TRXPATHENA, reg);

		reg = urtwn_bb_read(sc, R92C_OFDM0_AGCPARAM1);
		reg = (reg & ~0x00000030) | 1 << 4;
		urtwn_bb_write(sc, R92C_OFDM0_AGCPARAM1, reg);

		reg = urtwn_bb_read(sc, 0xe74);
		reg = (reg & ~0x0c000000) | 2 << 26;
		urtwn_bb_write(sc, 0xe74, reg);
		reg = urtwn_bb_read(sc, 0xe78);
		reg = (reg & ~0x0c000000) | 2 << 26;
		urtwn_bb_write(sc, 0xe78, reg);
		reg = urtwn_bb_read(sc, 0xe7c);
		reg = (reg & ~0x0c000000) | 2 << 26;
		urtwn_bb_write(sc, 0xe7c, reg);
		reg = urtwn_bb_read(sc, 0xe80);
		reg = (reg & ~0x0c000000) | 2 << 26;
		urtwn_bb_write(sc, 0xe80, reg);
		reg = urtwn_bb_read(sc, 0xe88);
		reg = (reg & ~0x0c000000) | 2 << 26;
		urtwn_bb_write(sc, 0xe88, reg);
	}

	/* Write AGC values. */
	for (i = 0; i < prog->agccount; i++) {
		urtwn_bb_write(sc, R92C_OFDM0_AGCRSSITABLE,
		    prog->agcvals[i]);
		DELAY(1);
	}

	if (sc->sc_sc.chip & (RTWN_CHIP_88E | RTWN_CHIP_88F)) {
		urtwn_bb_write(sc, R92C_OFDM0_AGCCORE1(0), 0x69553422);
		DELAY(1);
		urtwn_bb_write(sc, R92C_OFDM0_AGCCORE1(0), 0x69553420);
		DELAY(1);
	} else if (sc->sc_sc.chip & RTWN_CHIP_92E) {
		urtwn_bb_write(sc, R92C_OFDM0_AGCCORE1(0), 0x00040022);
		DELAY(1);
		urtwn_bb_write(sc, R92C_OFDM0_AGCCORE1(0), 0x00040020);
		DELAY(1);
	}

	if (sc->sc_sc.chip & (RTWN_CHIP_88E | RTWN_CHIP_88F)) {
		xtal = sc->sc_sc.crystal_cap & 0x3f;
		reg = urtwn_bb_read(sc, R92C_AFE_XTAL_CTRL);
		urtwn_bb_write(sc, R92C_AFE_XTAL_CTRL,
		    RW(reg, R92C_AFE_XTAL_CTRL_ADDR, xtal | xtal << 6));
	} else if (sc->sc_sc.chip & RTWN_CHIP_92E) {
		xtal = sc->sc_sc.crystal_cap & 0x3f;
		reg = urtwn_bb_read(sc, R92C_AFE_CTRL3);
		urtwn_bb_write(sc, R92C_AFE_CTRL3,
		    RW(reg, R92C_AFE_CTRL3_ADDR, xtal | xtal << 6));
		urtwn_write_4(sc, R92C_AFE_XTAL_CTRL, 0x000f81fb);
	}

	if (urtwn_bb_read(sc, R92C_HSSI_PARAM2(0)) & R92C_HSSI_PARAM2_CCK_HIPWR)
		sc->sc_sc.sc_flags |= RTWN_FLAG_CCK_HIPWR;
}

void
urtwn_burstlen_init(struct urtwn_softc *sc)
{
	uint8_t reg;

	reg = urtwn_read_1(sc, R92E_RXDMA_PRO);
	reg &= ~0x30;
	switch (sc->sc_udev->speed) {
	case USB_SPEED_HIGH:
		urtwn_write_1(sc, R92E_RXDMA_PRO, reg | 0x1e);
		break;
	default:
		urtwn_write_1(sc, R92E_RXDMA_PRO, reg | 0x2e);
		break;
	}

	if (sc->sc_sc.chip & RTWN_CHIP_88F) {
		/* Setup AMPDU aggregation. */
		urtwn_write_1(sc, R88F_HT_SINGLE_AMPDU,
		    urtwn_read_1(sc, R88F_HT_SINGLE_AMPDU) |
		    R88F_HT_SINGLE_AMPDU_EN);
		urtwn_write_2(sc, R92C_MAX_AGGR_NUM, 0x0c14);
		urtwn_write_1(sc, R88F_AMPDU_MAX_TIME, 0x70);
		urtwn_write_4(sc, R92C_AGGLEN_LMT, 0xffffffff);

		/* For VHT packet length 11K */
		urtwn_write_1(sc, R88F_RX_PKT_LIMIT, 0x18);

		urtwn_write_1(sc, R92C_PIFS, 0);
		urtwn_write_1(sc, R92C_FWHW_TXQ_CTRL, 0x80);
		urtwn_write_4(sc, R92C_FAST_EDCA_CTRL, 0x03086666);
		urtwn_write_1(sc, R92C_USTIME_TSF, 0x28);
		urtwn_write_1(sc, R88F_USTIME_EDCA, 0x28);

		/* To prevent bus resetting the mac. */
		urtwn_write_1(sc, R92C_RSV_CTRL,
		    urtwn_read_1(sc, R92C_RSV_CTRL) |
		    R92C_RSV_CTRL_R_DIS_PRST_0 | R92C_RSV_CTRL_R_DIS_PRST_1);
	}
}

int
urtwn_power_on(void *cookie)
{
	struct urtwn_softc *sc = cookie;

	if (sc->sc_sc.chip & RTWN_CHIP_88E)
		return (urtwn_r88e_power_on(sc));
	else if (sc->sc_sc.chip & RTWN_CHIP_88F)
		return (urtwn_r88f_power_on(sc));
	else if (sc->sc_sc.chip & RTWN_CHIP_92E)
		return (urtwn_r92e_power_on(sc));

	return (urtwn_r92c_power_on(sc));
}

int
urtwn_alloc_buffers(void *cookie)
{
	struct urtwn_softc *sc = cookie;
	int error;

	/* Init host async commands ring. */
	sc->cmdq.cur = sc->cmdq.next = sc->cmdq.queued = 0;

	/* Allocate Tx/Rx buffers. */
	error = urtwn_alloc_rx_list(sc);
	if (error != 0) {
		printf("%s: could not allocate Rx buffers\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}
	error = urtwn_alloc_tx_list(sc);
	if (error != 0) {
		printf("%s: could not allocate Tx buffers\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	return (0);
}

int
urtwn_init(void *cookie)
{
	struct urtwn_softc *sc = cookie;
	int i, error;

	/* Reset USB mode switch setting. */
	if (sc->sc_sc.chip & RTWN_CHIP_92E)
		urtwn_write_1(sc, R92C_ACLK_MON, 0);

	/* Queue Rx xfers. */
	for (i = 0; i < URTWN_RX_LIST_COUNT; i++) {
		struct urtwn_rx_data *data = &sc->rx_data[i];

		usbd_setup_xfer(data->xfer, sc->rx_pipe, data, data->buf,
		    URTWN_RXBUFSZ, USBD_SHORT_XFER_OK | USBD_NO_COPY,
		    USBD_NO_TIMEOUT, urtwn_rxeof);
		error = usbd_transfer(data->xfer);
		if (error != 0 && error != USBD_IN_PROGRESS)
			return (error);
	}

	ieee80211_amrr_node_init(&sc->amrr, &sc->amn);

	/*
	 * Enable TX reports for AMRR.
	 * In order to get reports we need to explicitly reset the register.
	 */
	if (sc->sc_sc.chip & RTWN_CHIP_88E)
		urtwn_write_1(sc, R88E_TX_RPT_CTRL, (urtwn_read_1(sc,
		    R88E_TX_RPT_CTRL) & ~0) | R88E_TX_RPT_CTRL_EN);

	return (0);
}

void
urtwn_stop(void *cookie)
{
	struct urtwn_softc *sc = cookie;
	int i;

	/* Abort Tx. */
	for (i = 0; i < R92C_MAX_EPOUT; i++) {
		if (sc->tx_pipe[i] != NULL)
			usbd_abort_pipe(sc->tx_pipe[i]);
	}
	/* Stop Rx pipe. */
	usbd_abort_pipe(sc->rx_pipe);
	/* Free Tx/Rx buffers. */
	urtwn_free_tx_list(sc);
	urtwn_free_rx_list(sc);
}

int
urtwn_is_oactive(void *cookie)
{
	struct urtwn_softc *sc = cookie;

	return (TAILQ_EMPTY(&sc->tx_free_list));
}
