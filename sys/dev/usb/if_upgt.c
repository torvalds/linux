/*	$OpenBSD: if_upgt.c,v 1.90 2024/05/23 03:21:09 jsg Exp $ */

/*
 * Copyright (c) 2007 Marcus Glocker <mglocker@openbsd.org>
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

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/device.h>
#include <sys/endian.h>

#include <machine/intr.h>

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

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/if_upgtvar.h>

/*
 * Driver for the USB PrismGT devices.
 *
 * For now just USB 2.0 devices with the GW3887 chipset are supported.
 * The driver has been written based on the firmware version 2.13.1.0_LM87.
 *
 * TODO's:
 * - Fix MONITOR mode (MAC filter).
 * - Add HOSTAP mode.
 * - Add IBSS mode.
 * - Support the USB 1.0 devices (NET2280, ISL3880, ISL3886 chipsets).
 *
 * Parts of this driver has been influenced by reading the p54u driver
 * written by Jean-Baptiste Note <jean-baptiste.note@m4x.org> and
 * Sebastien Bourdeauducq <lekernel@prism54.org>.
 */

#ifdef UPGT_DEBUG
int upgt_debug = 2;
#define DPRINTF(l, x...) do { if ((l) <= upgt_debug) printf(x); } while (0)
#else
#define DPRINTF(l, x...)
#endif

/*
 * Prototypes.
 */
int		upgt_match(struct device *, void *, void *);
void		upgt_attach(struct device *, struct device *, void *);
void		upgt_attach_hook(struct device *);
int		upgt_detach(struct device *, int);

int		upgt_device_type(struct upgt_softc *, uint16_t, uint16_t);
int		upgt_device_init(struct upgt_softc *);
int		upgt_mem_init(struct upgt_softc *);
uint32_t	upgt_mem_alloc(struct upgt_softc *);
void		upgt_mem_free(struct upgt_softc *, uint32_t);
int		upgt_fw_alloc(struct upgt_softc *);
void		upgt_fw_free(struct upgt_softc *);
int		upgt_fw_verify(struct upgt_softc *);
int		upgt_fw_load(struct upgt_softc *);
int		upgt_fw_copy(char *, char *, int);
int		upgt_eeprom_read(struct upgt_softc *);
int		upgt_eeprom_parse(struct upgt_softc *);
void		upgt_eeprom_parse_hwrx(struct upgt_softc *, uint8_t *);
void		upgt_eeprom_parse_freq3(struct upgt_softc *, uint8_t *, int);
void		upgt_eeprom_parse_freq4(struct upgt_softc *, uint8_t *, int);
void		upgt_eeprom_parse_freq6(struct upgt_softc *, uint8_t *, int);

int		upgt_ioctl(struct ifnet *, u_long, caddr_t);
int		upgt_init(struct ifnet *);
void		upgt_stop(struct upgt_softc *);
int		upgt_media_change(struct ifnet *);
void		upgt_newassoc(struct ieee80211com *, struct ieee80211_node *,
		    int);
int		upgt_newstate(struct ieee80211com *, enum ieee80211_state, int);
void		upgt_newstate_task(void *);
void		upgt_next_scan(void *);
void		upgt_start(struct ifnet *);
void		upgt_watchdog(struct ifnet *);
void		upgt_tx_task(void *);
void		upgt_tx_done(struct upgt_softc *, uint8_t *);
void		upgt_rx_cb(struct usbd_xfer *, void *, usbd_status);
void		upgt_rx(struct upgt_softc *, uint8_t *, int);
void		upgt_setup_rates(struct upgt_softc *);
uint8_t		upgt_rx_rate(struct upgt_softc *, const int);
int		upgt_set_macfilter(struct upgt_softc *, uint8_t state);
int		upgt_set_channel(struct upgt_softc *, unsigned);
void		upgt_set_led(struct upgt_softc *, int);
void		upgt_set_led_blink(void *);
int		upgt_get_stats(struct upgt_softc *);

int		upgt_alloc_tx(struct upgt_softc *);
int		upgt_alloc_rx(struct upgt_softc *);
int		upgt_alloc_cmd(struct upgt_softc *);
void		upgt_free_tx(struct upgt_softc *);
void		upgt_free_rx(struct upgt_softc *);
void		upgt_free_cmd(struct upgt_softc *);
int		upgt_bulk_xmit(struct upgt_softc *, struct upgt_data *,
		    struct usbd_pipe *, uint32_t *, int);

void		upgt_hexdump(void *, int);
uint32_t	upgt_crc32_le(const void *, size_t);
uint32_t	upgt_chksum_le(const uint32_t *, size_t);

struct cfdriver upgt_cd = {
	NULL, "upgt", DV_IFNET
};

const struct cfattach upgt_ca = {
	sizeof(struct upgt_softc), upgt_match, upgt_attach, upgt_detach
};

static const struct usb_devno upgt_devs_1[] = {
	/* version 1 devices */
	{ USB_VENDOR_ALCATELT,		USB_PRODUCT_ALCATELT_ST120G }
};

static const struct usb_devno upgt_devs_2[] = {
	/* version 2 devices */
	{ USB_VENDOR_ACCTON,		USB_PRODUCT_ACCTON_PRISM_GT },
	{ USB_VENDOR_ALCATELT,		USB_PRODUCT_ALCATELT_ST121G },
	{ USB_VENDOR_BELKIN,		USB_PRODUCT_BELKIN_F5D7050 },
	{ USB_VENDOR_CISCOLINKSYS,	USB_PRODUCT_CISCOLINKSYS_WUSB54AG },
	{ USB_VENDOR_CISCOLINKSYS,	USB_PRODUCT_CISCOLINKSYS_WUSB54GV2 },
	{ USB_VENDOR_CONCEPTRONIC,	USB_PRODUCT_CONCEPTRONIC_PRISM_GT },
	{ USB_VENDOR_DELL,		USB_PRODUCT_DELL_PRISM_GT_1 },
	{ USB_VENDOR_DELL,		USB_PRODUCT_DELL_PRISM_GT_2 },
	{ USB_VENDOR_DLINK,		USB_PRODUCT_DLINK_DWLG122A2 },
	{ USB_VENDOR_FSC,		USB_PRODUCT_FSC_E5400 },
	{ USB_VENDOR_GLOBESPAN,		USB_PRODUCT_GLOBESPAN_PRISM_GT_1 },
	{ USB_VENDOR_GLOBESPAN,		USB_PRODUCT_GLOBESPAN_PRISM_GT_2 },
	{ USB_VENDOR_INTERSIL,		USB_PRODUCT_INTERSIL_PRISM_GT },
	{ USB_VENDOR_PHEENET,		USB_PRODUCT_PHEENET_GWU513 },
	{ USB_VENDOR_PHILIPS,		USB_PRODUCT_PHILIPS_CPWUA054 },
	{ USB_VENDOR_SMC,		USB_PRODUCT_SMC_2862WG },
	{ USB_VENDOR_USR,		USB_PRODUCT_USR_USR5422 },
	{ USB_VENDOR_WISTRONNEWEB,	USB_PRODUCT_WISTRONNEWEB_UR045G },
	{ USB_VENDOR_XYRATEX,		USB_PRODUCT_XYRATEX_PRISM_GT_1 },
	{ USB_VENDOR_XYRATEX,		USB_PRODUCT_XYRATEX_PRISM_GT_2 },
	{ USB_VENDOR_ZCOM,		USB_PRODUCT_ZCOM_MD40900 },
	{ USB_VENDOR_ZCOM,		USB_PRODUCT_ZCOM_XG703A }
};

int
upgt_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != UPGT_CONFIG_NO)
		return (UMATCH_NONE);

	if (usb_lookup(upgt_devs_1, uaa->vendor, uaa->product) != NULL)
		return (UMATCH_VENDOR_PRODUCT);

	if (usb_lookup(upgt_devs_2, uaa->vendor, uaa->product) != NULL)
		return (UMATCH_VENDOR_PRODUCT);

	return (UMATCH_NONE);
}

void
upgt_attach(struct device *parent, struct device *self, void *aux)
{
	struct upgt_softc *sc = (struct upgt_softc *)self;
	struct usb_attach_arg *uaa = aux;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	usbd_status error;
	int i;

	/*
	 * Attach USB device.
	 */
	sc->sc_udev = uaa->device;

	/* check device type */
	if (upgt_device_type(sc, uaa->vendor, uaa->product) != 0)
		return;

	/* get the first interface handle */
	error = usbd_device2interface_handle(sc->sc_udev, UPGT_IFACE_INDEX,
	    &sc->sc_iface);
	if (error != 0) {
		printf("%s: could not get interface handle!\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	/* find endpoints */
	id = usbd_get_interface_descriptor(sc->sc_iface);
	sc->sc_rx_no = sc->sc_tx_no = -1;
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor for iface %d!\n",
			    sc->sc_dev.dv_xname, i);
			return;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			sc->sc_tx_no = ed->bEndpointAddress;
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			sc->sc_rx_no = ed->bEndpointAddress;

		/*
		 * 0x01 TX pipe
		 * 0x81 RX pipe
		 *
		 * Deprecated scheme (not used with fw version >2.5.6.x):
		 * 0x02 TX MGMT pipe
		 * 0x82 TX MGMT pipe
		 */
		if (sc->sc_tx_no != -1 && sc->sc_rx_no != -1)
			break;
	}
	if (sc->sc_rx_no == -1 || sc->sc_tx_no == -1) {
		printf("%s: missing endpoint!\n", sc->sc_dev.dv_xname);
		return;
	}

	/* setup tasks and timeouts */
	usb_init_task(&sc->sc_task_newstate, upgt_newstate_task, sc,
	    USB_TASK_TYPE_GENERIC);
	usb_init_task(&sc->sc_task_tx, upgt_tx_task, sc, USB_TASK_TYPE_GENERIC);
	timeout_set(&sc->scan_to, upgt_next_scan, sc);
	timeout_set(&sc->led_to, upgt_set_led_blink, sc);

	/*
	 * Open TX and RX USB bulk pipes.
	 */
	error = usbd_open_pipe(sc->sc_iface, sc->sc_tx_no, USBD_EXCLUSIVE_USE,
	    &sc->sc_tx_pipeh);
	if (error != 0) {
		printf("%s: could not open TX pipe: %s!\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		goto fail;
	}
	error = usbd_open_pipe(sc->sc_iface, sc->sc_rx_no, USBD_EXCLUSIVE_USE,
	    &sc->sc_rx_pipeh);
	if (error != 0) {
		printf("%s: could not open RX pipe: %s!\n",
		    sc->sc_dev.dv_xname, usbd_errstr(error));
		goto fail;
	}

	/*
	 * Allocate TX, RX, and CMD xfers.
	 */
	if (upgt_alloc_tx(sc) != 0)
		goto fail;
	if (upgt_alloc_rx(sc) != 0)
		goto fail;
	if (upgt_alloc_cmd(sc) != 0)
		goto fail;

	/*
	 * We need the firmware loaded to complete the attach.
	 */
	config_mountroot(self, upgt_attach_hook);

	return;
fail:
	printf("%s: %s failed!\n", sc->sc_dev.dv_xname, __func__);
}

void
upgt_attach_hook(struct device *self)
{
	struct upgt_softc *sc = (struct upgt_softc *)self;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	usbd_status error;
	int i;

	/*
	 * Load firmware file into memory.
	 */
	if (upgt_fw_alloc(sc) != 0)
		goto fail;

	/*
	 * Initialize the device.
	 */
	if (upgt_device_init(sc) != 0)
		goto fail;

	/*
	 * Verify the firmware.
	 */
	if (upgt_fw_verify(sc) != 0)
		goto fail;

	/*
	 * Calculate device memory space.
	 */
	if (sc->sc_memaddr_frame_start == 0 || sc->sc_memaddr_frame_end == 0) {
		printf("%s: could not find memory space addresses on FW!\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	sc->sc_memaddr_frame_end -= UPGT_MEMSIZE_RX + 1;
	sc->sc_memaddr_rx_start = sc->sc_memaddr_frame_end + 1;

	DPRINTF(1, "%s: memory address frame start=0x%08x\n",
	    sc->sc_dev.dv_xname, sc->sc_memaddr_frame_start);
	DPRINTF(1, "%s: memory address frame end=0x%08x\n",
	    sc->sc_dev.dv_xname, sc->sc_memaddr_frame_end);
	DPRINTF(1, "%s: memory address rx start=0x%08x\n",
	    sc->sc_dev.dv_xname, sc->sc_memaddr_rx_start);

	upgt_mem_init(sc);

	/*
	 * Load the firmware.
	 */
	if (upgt_fw_load(sc) != 0)
		goto fail;

	/*
	 * Startup the RX pipe.
	 */
	struct upgt_data *data_rx = &sc->rx_data;

	usbd_setup_xfer(data_rx->xfer, sc->sc_rx_pipeh, data_rx, data_rx->buf,
	    MCLBYTES, USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, upgt_rx_cb);
	error = usbd_transfer(data_rx->xfer);
	if (error != 0 && error != USBD_IN_PROGRESS) {
		printf("%s: could not queue RX transfer!\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}
	usbd_delay_ms(sc->sc_udev, 100);

	/*
	 * Read the whole EEPROM content and parse it.
	 */
	if (upgt_eeprom_read(sc) != 0)
		goto fail;
	if (upgt_eeprom_parse(sc) != 0)
		goto fail;

	/*
	 * Setup the 802.11 device.
	 */
	ic->ic_phytype = IEEE80211_T_OFDM;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_state = IEEE80211_S_INIT;
	ic->ic_caps =
	    IEEE80211_C_MONITOR |
	    IEEE80211_C_SHPREAMBLE |
	    IEEE80211_C_SHSLOT |
	    IEEE80211_C_WEP |
	    IEEE80211_C_RSN;

	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;

	for (i = 1; i <= 14; i++) {
		ic->ic_channels[i].ic_freq =
		    ieee80211_ieee2mhz(i, IEEE80211_CHAN_2GHZ);
		ic->ic_channels[i].ic_flags =
		    IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
		    IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;
	}

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = upgt_ioctl;
	ifp->if_start = upgt_start;
	ifp->if_watchdog = upgt_watchdog;
	memcpy(ifp->if_xname, sc->sc_dev.dv_xname, IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp);
	ic->ic_newassoc = upgt_newassoc;

	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = upgt_newstate;
	ieee80211_media_init(ifp, upgt_media_change, ieee80211_media_status);

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof(struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN);

	sc->sc_rxtap_len = sizeof(sc->sc_rxtapu);
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(UPGT_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof(sc->sc_txtapu);
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(UPGT_TX_RADIOTAP_PRESENT);
#endif

	printf("%s: address %s\n",
	    sc->sc_dev.dv_xname, ether_sprintf(ic->ic_myaddr));

	return;
fail:
	printf("%s: %s failed!\n", sc->sc_dev.dv_xname, __func__);
}

int
upgt_detach(struct device *self, int flags)
{
	struct upgt_softc *sc = (struct upgt_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;
	int s;

	DPRINTF(1, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	s = splusb();

	/* abort and close TX / RX pipes */
	if (sc->sc_tx_pipeh != NULL)
		usbd_close_pipe(sc->sc_tx_pipeh);
	if (sc->sc_rx_pipeh != NULL)
		usbd_close_pipe(sc->sc_rx_pipeh);

	/* remove tasks and timeouts */
	usb_rem_task(sc->sc_udev, &sc->sc_task_newstate);
	usb_rem_task(sc->sc_udev, &sc->sc_task_tx);
	if (timeout_initialized(&sc->scan_to))
		timeout_del(&sc->scan_to);
	if (timeout_initialized(&sc->led_to))
		timeout_del(&sc->led_to);

	/* free xfers */
	upgt_free_tx(sc);
	upgt_free_rx(sc);
	upgt_free_cmd(sc);

	/* free firmware */
	upgt_fw_free(sc);

	if (ifp->if_softc != NULL) {
		/* detach interface */
		ieee80211_ifdetach(ifp);
		if_detach(ifp);
	}

	splx(s);

	return (0);
}

int
upgt_device_type(struct upgt_softc *sc, uint16_t vendor, uint16_t product)
{
	if (usb_lookup(upgt_devs_1, vendor, product) != NULL) {
		sc->sc_device_type = 1;
		/* XXX */
		printf("%s: version 1 devices not supported yet!\n",
		    sc->sc_dev.dv_xname);
		return (1);
	} else {
		sc->sc_device_type = 2;
	}

	return (0);
}

int
upgt_device_init(struct upgt_softc *sc)
{
	struct upgt_data *data_cmd = &sc->cmd_data;
	char init_cmd[] = { 0x7e, 0x7e, 0x7e, 0x7e };
	int len;

	len = sizeof(init_cmd);
	bcopy(init_cmd, data_cmd->buf, len);
	if (upgt_bulk_xmit(sc, data_cmd, sc->sc_tx_pipeh, &len, 0) != 0) {
		printf("%s: could not send device init string!\n",
		    sc->sc_dev.dv_xname);
		return (EIO);
	}
	usbd_delay_ms(sc->sc_udev, 100);

	DPRINTF(1, "%s: device initialized\n", sc->sc_dev.dv_xname);

	return (0);
}

int
upgt_mem_init(struct upgt_softc *sc)
{
	int i;

	for (i = 0; i < UPGT_MEMORY_MAX_PAGES; i++) {
		sc->sc_memory.page[i].used = 0;

		if (i == 0) {
			/*
			 * The first memory page is always reserved for
			 * command data.
			 */
			sc->sc_memory.page[i].addr =
			    sc->sc_memaddr_frame_start + MCLBYTES;
		} else {
			sc->sc_memory.page[i].addr =
			    sc->sc_memory.page[i - 1].addr + MCLBYTES;
		}

		if (sc->sc_memory.page[i].addr + MCLBYTES >=
		    sc->sc_memaddr_frame_end)
			break;

		DPRINTF(2, "%s: memory address page %d=0x%08x\n",
		    sc->sc_dev.dv_xname, i, sc->sc_memory.page[i].addr);
	}

	sc->sc_memory.pages = i;

	DPRINTF(2, "%s: memory pages=%d\n",
	    sc->sc_dev.dv_xname, sc->sc_memory.pages);

	return (0);
}

uint32_t
upgt_mem_alloc(struct upgt_softc *sc)
{
	int i;

	for (i = 0; i < sc->sc_memory.pages; i++) {
		if (sc->sc_memory.page[i].used == 0) {
			sc->sc_memory.page[i].used = 1;
			return (sc->sc_memory.page[i].addr);
		}
	}

	return (0);
}

void
upgt_mem_free(struct upgt_softc *sc, uint32_t addr)
{
	int i;

	for (i = 0; i < sc->sc_memory.pages; i++) {
		if (sc->sc_memory.page[i].addr == addr) {
			sc->sc_memory.page[i].used = 0;
			return;
		}
	}

	printf("%s: could not free memory address 0x%08x!\n",
	    sc->sc_dev.dv_xname, addr);
}


int
upgt_fw_alloc(struct upgt_softc *sc)
{
	const char *name = "upgt-gw3887";
	int error;

	if (sc->sc_fw == NULL) {
		error = loadfirmware(name, &sc->sc_fw, &sc->sc_fw_size);
		if (error != 0) {
			printf("%s: error %d, could not read firmware %s!\n",
			    sc->sc_dev.dv_xname, error, name);
			return (EIO);
		}
	}

	DPRINTF(1, "%s: firmware %s allocated\n", sc->sc_dev.dv_xname, name);

	return (0);
}

void
upgt_fw_free(struct upgt_softc *sc)
{
	if (sc->sc_fw != NULL) {
		free(sc->sc_fw, M_DEVBUF, sc->sc_fw_size);
		sc->sc_fw = NULL;
		DPRINTF(1, "%s: firmware freed\n", sc->sc_dev.dv_xname);
	}
}

int
upgt_fw_verify(struct upgt_softc *sc)
{
	struct upgt_fw_bra_option *bra_option;
	uint32_t bra_option_type, bra_option_len;
	uint32_t *uc;
	int offset, bra_end = 0;

	/*
	 * Seek to beginning of Boot Record Area (BRA).
	 */
	for (offset = 0; offset < sc->sc_fw_size; offset += sizeof(*uc)) {
		uc = (uint32_t *)(sc->sc_fw + offset);
		if (*uc == 0)
			break;
	}
	for (; offset < sc->sc_fw_size; offset += sizeof(*uc)) {
		uc = (uint32_t *)(sc->sc_fw + offset);
		if (*uc != 0)
			break;
	}
	if (offset == sc->sc_fw_size) { 
		printf("%s: firmware Boot Record Area not found!\n",
		    sc->sc_dev.dv_xname);
		return (EIO);
	}
	DPRINTF(1, "%s: firmware Boot Record Area found at offset %d\n",
	    sc->sc_dev.dv_xname, offset);

	/*
	 * Parse Boot Record Area (BRA) options.
	 */
	while (offset < sc->sc_fw_size && bra_end == 0) {
		/* get current BRA option */
		bra_option = (struct upgt_fw_bra_option *)(sc->sc_fw + offset);
		bra_option_type = letoh32(bra_option->type);
		bra_option_len = letoh32(bra_option->len) * sizeof(*uc);

		switch (bra_option_type) {
		case UPGT_BRA_TYPE_FW:
			DPRINTF(1, "%s: UPGT_BRA_TYPE_FW len=%d\n",
			    sc->sc_dev.dv_xname, bra_option_len);

			if (bra_option_len != UPGT_BRA_FWTYPE_SIZE) {
				printf("%s: wrong UPGT_BRA_TYPE_FW len!\n",
				    sc->sc_dev.dv_xname);
				return (EIO);
			}
			if (memcmp(UPGT_BRA_FWTYPE_LM86, bra_option->data,
			    bra_option_len) == 0) {
				sc->sc_fw_type = UPGT_FWTYPE_LM86;
				break;
			}
			if (memcmp(UPGT_BRA_FWTYPE_LM87, bra_option->data,
			    bra_option_len) == 0) {
				sc->sc_fw_type = UPGT_FWTYPE_LM87;
				break;
			}
			if (memcmp(UPGT_BRA_FWTYPE_FMAC, bra_option->data,
			    bra_option_len) == 0) {
				sc->sc_fw_type = UPGT_FWTYPE_FMAC;
				break;
			}
			printf("%s: unsupported firmware type!\n",
			    sc->sc_dev.dv_xname);
			return (EIO);
		case UPGT_BRA_TYPE_VERSION:
			DPRINTF(1, "%s: UPGT_BRA_TYPE_VERSION len=%d\n",
			    sc->sc_dev.dv_xname, bra_option_len);
			break;
		case UPGT_BRA_TYPE_DEPIF:
			DPRINTF(1, "%s: UPGT_BRA_TYPE_DEPIF len=%d\n",
			    sc->sc_dev.dv_xname, bra_option_len);
			break;
		case UPGT_BRA_TYPE_EXPIF:
			DPRINTF(1, "%s: UPGT_BRA_TYPE_EXPIF len=%d\n",
			    sc->sc_dev.dv_xname, bra_option_len);
			break;
		case UPGT_BRA_TYPE_DESCR:
			DPRINTF(1, "%s: UPGT_BRA_TYPE_DESCR len=%d\n",
			    sc->sc_dev.dv_xname, bra_option_len);

			struct upgt_fw_bra_descr *descr =
				(struct upgt_fw_bra_descr *)bra_option->data;

			sc->sc_memaddr_frame_start =
			    letoh32(descr->memaddr_space_start);
			sc->sc_memaddr_frame_end =
			    letoh32(descr->memaddr_space_end);

			DPRINTF(2, "%s: memory address space start=0x%08x\n",
			    sc->sc_dev.dv_xname, sc->sc_memaddr_frame_start);
			DPRINTF(2, "%s: memory address space end=0x%08x\n",
			    sc->sc_dev.dv_xname, sc->sc_memaddr_frame_end);
			break;
		case UPGT_BRA_TYPE_END:
			DPRINTF(1, "%s: UPGT_BRA_TYPE_END len=%d\n",
			    sc->sc_dev.dv_xname, bra_option_len);
			bra_end = 1;
			break;
		default:
			DPRINTF(1, "%s: unknown BRA option len=%d\n",
			    sc->sc_dev.dv_xname, bra_option_len);
			return (EIO);
		}

		/* jump to next BRA option */
		offset += sizeof(struct upgt_fw_bra_option) + bra_option_len;
	}

	DPRINTF(1, "%s: firmware verified\n", sc->sc_dev.dv_xname);

	return (0);
}

int
upgt_fw_load(struct upgt_softc *sc)
{
	struct upgt_data *data_cmd = &sc->cmd_data;
	struct upgt_data *data_rx = &sc->rx_data;
	char start_fwload_cmd[] = { 0x3c, 0x0d };
	int offset, bsize, n, i, len;
	uint32_t crc32;

	/* send firmware start load command */
	len = sizeof(start_fwload_cmd);
	bcopy(start_fwload_cmd, data_cmd->buf, len);
	if (upgt_bulk_xmit(sc, data_cmd, sc->sc_tx_pipeh, &len, 0) != 0) {
		printf("%s: could not send start_firmware_load command!\n",
		    sc->sc_dev.dv_xname);
		return (EIO);
	}

	/* send X2 header */
	len = sizeof(struct upgt_fw_x2_header);
	struct upgt_fw_x2_header *x2 = data_cmd->buf;
	bcopy(UPGT_X2_SIGNATURE, x2->signature, UPGT_X2_SIGNATURE_SIZE);
	x2->startaddr = htole32(UPGT_MEMADDR_FIRMWARE_START);
	x2->len = htole32(sc->sc_fw_size);
	x2->crc = upgt_crc32_le(data_cmd->buf + UPGT_X2_SIGNATURE_SIZE,
	    sizeof(struct upgt_fw_x2_header) - UPGT_X2_SIGNATURE_SIZE -
	    sizeof(uint32_t));
	if (upgt_bulk_xmit(sc, data_cmd, sc->sc_tx_pipeh, &len, 0) != 0) {
		printf("%s: could not send firmware X2 header!\n",
		    sc->sc_dev.dv_xname);
		return (EIO);
	}

	/* download firmware */
	for (offset = 0; offset < sc->sc_fw_size; offset += bsize) {
		if (sc->sc_fw_size - offset > UPGT_FW_BLOCK_SIZE)
			bsize = UPGT_FW_BLOCK_SIZE;
		else
			bsize = sc->sc_fw_size - offset;

		n = upgt_fw_copy(sc->sc_fw + offset, data_cmd->buf, bsize);

		DPRINTF(1, "%s: FW offset=%d, read=%d, sent=%d\n",
		    sc->sc_dev.dv_xname, offset, n, bsize);

		if (upgt_bulk_xmit(sc, data_cmd, sc->sc_tx_pipeh, &bsize, 0)
		    != 0) {
			printf("%s: error while downloading firmware block!\n",
			    sc->sc_dev.dv_xname);
			return (EIO);
		}

		bsize = n;
	}
	DPRINTF(1, "%s: firmware downloaded\n", sc->sc_dev.dv_xname);

	/* load firmware */
	crc32 = upgt_crc32_le(sc->sc_fw, sc->sc_fw_size);
	*((uint32_t *)(data_cmd->buf)    ) = crc32;
	*((uint8_t  *)(data_cmd->buf) + 4) = 'g';
	*((uint8_t  *)(data_cmd->buf) + 5) = '\r';
	len = 6;
	if (upgt_bulk_xmit(sc, data_cmd, sc->sc_tx_pipeh, &len, 0) != 0) {
		printf("%s: could not send load_firmware command!\n",
		    sc->sc_dev.dv_xname);
		return (EIO);
	}

	for (i = 0; i < UPGT_FIRMWARE_TIMEOUT; i++) {
		len = UPGT_FW_BLOCK_SIZE;
		bzero(data_rx->buf, MCLBYTES);
		if (upgt_bulk_xmit(sc, data_rx, sc->sc_rx_pipeh, &len,
		    USBD_SHORT_XFER_OK) != 0) {
			printf("%s: could not read firmware response!\n",
			    sc->sc_dev.dv_xname);
			return (EIO);
		}

		if (memcmp(data_rx->buf, "OK", 2) == 0)
			break;	/* firmware load was successful */
	}
	if (i == UPGT_FIRMWARE_TIMEOUT) {
		printf("%s: firmware load failed!\n", sc->sc_dev.dv_xname);
		return (EIO);
	}
	DPRINTF(1, "%s: firmware loaded\n", sc->sc_dev.dv_xname);

	return (0);
}

/*
 * While copying the version 2 firmware, we need to replace two characters:
 *
 * 0x7e -> 0x7d 0x5e
 * 0x7d -> 0x7d 0x5d
 */
int
upgt_fw_copy(char *src, char *dst, int size)
{
	int i, j;

	for (i = 0, j = 0; i < size && j < size; i++) {
		switch (src[i]) {
		case 0x7e:
			dst[j] = 0x7d;
			j++;
			dst[j] = 0x5e;
			j++;
			break;
		case 0x7d:
			dst[j] = 0x7d;
			j++;
			dst[j] = 0x5d;
			j++;
			break;
		default:
			dst[j] = src[i];
			j++;
			break;
		}
	}

	return (i);
}

int
upgt_eeprom_read(struct upgt_softc *sc)
{
	struct upgt_data *data_cmd = &sc->cmd_data;
	struct upgt_lmac_mem *mem;
	struct upgt_lmac_eeprom	*eeprom;
	int offset, block, len;

	offset = 0;
	block = UPGT_EEPROM_BLOCK_SIZE;
	while (offset < UPGT_EEPROM_SIZE) {
		DPRINTF(1, "%s: request EEPROM block (offset=%d, len=%d)\n",
		    sc->sc_dev.dv_xname, offset, block);

		/*
		 * Transmit the URB containing the CMD data.
		 */
		bzero(data_cmd->buf, MCLBYTES);

		mem = (struct upgt_lmac_mem *)data_cmd->buf;
		mem->addr = htole32(sc->sc_memaddr_frame_start +
		    UPGT_MEMSIZE_FRAME_HEAD);

		eeprom = (struct upgt_lmac_eeprom *)(mem + 1);
		eeprom->header1.flags = 0;
		eeprom->header1.type = UPGT_H1_TYPE_CTRL;
		eeprom->header1.len = htole16((
		    sizeof(struct upgt_lmac_eeprom) -
		    sizeof(struct upgt_lmac_header)) + block);

		eeprom->header2.reqid = htole32(sc->sc_memaddr_frame_start);
		eeprom->header2.type = htole16(UPGT_H2_TYPE_EEPROM);
		eeprom->header2.flags = 0;

		eeprom->offset = htole16(offset);
		eeprom->len = htole16(block);

		len = sizeof(*mem) + sizeof(*eeprom) + block;

		mem->chksum = upgt_chksum_le((uint32_t *)eeprom,
		    len - sizeof(*mem));

		if (upgt_bulk_xmit(sc, data_cmd, sc->sc_tx_pipeh, &len,
		    USBD_FORCE_SHORT_XFER) != 0) {
			printf("%s: could not transmit EEPROM data URB!\n",
			    sc->sc_dev.dv_xname);
			return (EIO);
		}
		if (tsleep_nsec(sc, 0, "eeprom_request",
		    MSEC_TO_NSEC(UPGT_USB_TIMEOUT))) {
			printf("%s: timeout while waiting for EEPROM data!\n",
			    sc->sc_dev.dv_xname);
			return (EIO);
		}

		offset += block;
		if (UPGT_EEPROM_SIZE - offset < block)
			block = UPGT_EEPROM_SIZE - offset;
	}

	return (0);
}

int
upgt_eeprom_parse(struct upgt_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct upgt_eeprom_header *eeprom_header;
	struct upgt_eeprom_option *eeprom_option;
	uint16_t option_len;
	uint16_t option_type;
	uint16_t preamble_len;
	int option_end = 0;

	/* calculate eeprom options start offset */
	eeprom_header = (struct upgt_eeprom_header *)sc->sc_eeprom;
	preamble_len = letoh16(eeprom_header->preamble_len);
	eeprom_option = (struct upgt_eeprom_option *)(sc->sc_eeprom +
	    (sizeof(struct upgt_eeprom_header) + preamble_len));

	while (!option_end) {
		/* the eeprom option length is stored in words */
		option_len =
		    (letoh16(eeprom_option->len) - 1) * sizeof(uint16_t);
		option_type =
		    letoh16(eeprom_option->type);

		switch (option_type) {
		case UPGT_EEPROM_TYPE_NAME:
			DPRINTF(1, "%s: EEPROM name len=%d\n",
			    sc->sc_dev.dv_xname, option_len);
			break;
		case UPGT_EEPROM_TYPE_SERIAL:
			DPRINTF(1, "%s: EEPROM serial len=%d\n",
			    sc->sc_dev.dv_xname, option_len);
			break;
		case UPGT_EEPROM_TYPE_MAC:
			DPRINTF(1, "%s: EEPROM mac len=%d\n",
			    sc->sc_dev.dv_xname, option_len);

			IEEE80211_ADDR_COPY(ic->ic_myaddr, eeprom_option->data);
			break;
		case UPGT_EEPROM_TYPE_HWRX:
			DPRINTF(1, "%s: EEPROM hwrx len=%d\n",
			    sc->sc_dev.dv_xname, option_len);

			upgt_eeprom_parse_hwrx(sc, eeprom_option->data);
			break;
		case UPGT_EEPROM_TYPE_CHIP:
			DPRINTF(1, "%s: EEPROM chip len=%d\n",
			    sc->sc_dev.dv_xname, option_len);
			break;
		case UPGT_EEPROM_TYPE_FREQ3:
			DPRINTF(1, "%s: EEPROM freq3 len=%d\n",
			    sc->sc_dev.dv_xname, option_len);

			upgt_eeprom_parse_freq3(sc, eeprom_option->data,
			    option_len);
			break;
		case UPGT_EEPROM_TYPE_FREQ4:
			DPRINTF(1, "%s: EEPROM freq4 len=%d\n",
			    sc->sc_dev.dv_xname, option_len);

			upgt_eeprom_parse_freq4(sc, eeprom_option->data,
			    option_len);
			break;
		case UPGT_EEPROM_TYPE_FREQ5:
			DPRINTF(1, "%s: EEPROM freq5 len=%d\n",
			    sc->sc_dev.dv_xname, option_len);
			break;
		case UPGT_EEPROM_TYPE_FREQ6:
			DPRINTF(1, "%s: EEPROM freq6 len=%d\n",
			    sc->sc_dev.dv_xname, option_len);

			upgt_eeprom_parse_freq6(sc, eeprom_option->data,
			    option_len);
			break;
		case UPGT_EEPROM_TYPE_END:
			DPRINTF(1, "%s: EEPROM end len=%d\n",
			    sc->sc_dev.dv_xname, option_len);
			option_end = 1;
			break;
		case UPGT_EEPROM_TYPE_OFF:
			DPRINTF(1, "%s: EEPROM off without end option!\n",
			    sc->sc_dev.dv_xname);
			return (EIO);
		default:
			DPRINTF(1, "%s: EEPROM unknown type 0x%04x len=%d\n",
			    sc->sc_dev.dv_xname, option_type, option_len);
			break;
		}

		/* jump to next EEPROM option */
		eeprom_option = (struct upgt_eeprom_option *)
		    (eeprom_option->data + option_len);
	}

	return (0);
}

void
upgt_eeprom_parse_hwrx(struct upgt_softc *sc, uint8_t *data)
{
	struct upgt_eeprom_option_hwrx *option_hwrx;

	option_hwrx = (struct upgt_eeprom_option_hwrx *)data;

	sc->sc_eeprom_hwrx = option_hwrx->rxfilter - UPGT_EEPROM_RX_CONST;

	DPRINTF(2, "%s: hwrx option value=0x%04x\n",
	    sc->sc_dev.dv_xname, sc->sc_eeprom_hwrx);
}

void
upgt_eeprom_parse_freq3(struct upgt_softc *sc, uint8_t *data, int len)
{
	struct upgt_eeprom_freq3_header *freq3_header;
	struct upgt_lmac_freq3 *freq3;
	int i, elements, flags;
	unsigned channel;

	freq3_header = (struct upgt_eeprom_freq3_header *)data;
	freq3 = (struct upgt_lmac_freq3 *)(freq3_header + 1);

	flags = freq3_header->flags;
	elements = freq3_header->elements;

	DPRINTF(2, "%s: flags=0x%02x\n", sc->sc_dev.dv_xname, flags);
	DPRINTF(2, "%s: elements=%d\n", sc->sc_dev.dv_xname, elements);

	for (i = 0; i < elements; i++) {
		channel = ieee80211_mhz2ieee(letoh16(freq3[i].freq), 0);

		sc->sc_eeprom_freq3[channel] = freq3[i];

		DPRINTF(2, "%s: frequency=%d, channel=%d\n",
		    sc->sc_dev.dv_xname,
		    letoh16(sc->sc_eeprom_freq3[channel].freq), channel);
	}
}

void
upgt_eeprom_parse_freq4(struct upgt_softc *sc, uint8_t *data, int len)
{
	struct upgt_eeprom_freq4_header *freq4_header;
	struct upgt_eeprom_freq4_1 *freq4_1;
	struct upgt_eeprom_freq4_2 *freq4_2;
	int i, j, elements, settings, flags;
	unsigned channel;

	freq4_header = (struct upgt_eeprom_freq4_header *)data;
	freq4_1 = (struct upgt_eeprom_freq4_1 *)(freq4_header + 1);

	flags = freq4_header->flags;
	elements = freq4_header->elements;
	settings = freq4_header->settings;

	/* we need this value later */
	sc->sc_eeprom_freq6_settings = freq4_header->settings;

	DPRINTF(2, "%s: flags=0x%02x\n", sc->sc_dev.dv_xname, flags);
	DPRINTF(2, "%s: elements=%d\n", sc->sc_dev.dv_xname, elements);
	DPRINTF(2, "%s: settings=%d\n", sc->sc_dev.dv_xname, settings);

	for (i = 0; i < elements; i++) {
		channel = ieee80211_mhz2ieee(letoh16(freq4_1[i].freq), 0);

		freq4_2 = (struct upgt_eeprom_freq4_2 *)freq4_1[i].data;

		for (j = 0; j < settings; j++) {
			sc->sc_eeprom_freq4[channel][j].cmd = freq4_2[j];
			sc->sc_eeprom_freq4[channel][j].pad = 0;
		}

		DPRINTF(2, "%s: frequency=%d, channel=%d\n",
		    sc->sc_dev.dv_xname,
		    letoh16(freq4_1[i].freq), channel);
	}
}

void
upgt_eeprom_parse_freq6(struct upgt_softc *sc, uint8_t *data, int len)
{
	struct upgt_lmac_freq6 *freq6;
	int i, elements;
	unsigned channel;

	freq6 = (struct upgt_lmac_freq6 *)data;

	elements = len / sizeof(struct upgt_lmac_freq6);

	DPRINTF(2, "%s: elements=%d\n", sc->sc_dev.dv_xname, elements);

	for (i = 0; i < elements; i++) {
		channel = ieee80211_mhz2ieee(letoh16(freq6[i].freq), 0);

		sc->sc_eeprom_freq6[channel] = freq6[i];

		DPRINTF(2, "%s: frequency=%d, channel=%d\n",
		    sc->sc_dev.dv_xname,
		    letoh16(sc->sc_eeprom_freq6[channel].freq), channel);
	}
}

int
upgt_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct upgt_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int s, error = 0;
	uint8_t chan;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_flags & IFF_RUNNING) == 0)
				upgt_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				upgt_stop(sc);
		}
		break;
	case SIOCS80211CHANNEL:
		/* allow fast channel switching in monitor mode */
		error = ieee80211_ioctl(ifp, cmd, data);
		if (error == ENETRESET &&
		    ic->ic_opmode == IEEE80211_M_MONITOR) {
			if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
			    (IFF_UP | IFF_RUNNING)) {
				ic->ic_bss->ni_chan = ic->ic_ibss_chan;
				chan = ieee80211_chan2ieee(ic,
				    ic->ic_bss->ni_chan);
				upgt_set_channel(sc, chan);
			}
			error = 0;
		}
		break;
	default:
		error = ieee80211_ioctl(ifp, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & (IFF_UP | IFF_RUNNING))
			upgt_init(ifp);
		error = 0;
	}

	splx(s);

	return (error);
}

int
upgt_init(struct ifnet *ifp)
{
	struct upgt_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;

	DPRINTF(1, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	IEEE80211_ADDR_COPY(ic->ic_myaddr, LLADDR(ifp->if_sadl));

	/* select default channel */
	ic->ic_bss->ni_chan = ic->ic_ibss_chan;
	sc->sc_cur_chan = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);

	/* setup device rates */
	upgt_setup_rates(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	upgt_set_macfilter(sc, IEEE80211_S_SCAN);

	if (ic->ic_opmode == IEEE80211_M_MONITOR) {
		upgt_set_channel(sc, sc->sc_cur_chan);
		ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
	} else
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);

	return (0);
}

void
upgt_stop(struct upgt_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	DPRINTF(1, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	/* device down */
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	upgt_set_led(sc, UPGT_LED_OFF);

	/* change device back to initial state */
	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
}

int
upgt_media_change(struct ifnet *ifp)
{
	struct upgt_softc *sc = ifp->if_softc;
	int error;

	DPRINTF(1, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	if ((error = ieee80211_media_change(ifp)) != ENETRESET)
		return (error);

	if (ifp->if_flags & (IFF_UP | IFF_RUNNING)) {
		/* give pending USB transfers a chance to finish */
		usbd_delay_ms(sc->sc_udev, 100);
		upgt_init(ifp);
	}

	return (error);
}

void
upgt_newassoc(struct ieee80211com *ic, struct ieee80211_node *ni, int isnew)
{
	ni->ni_txrate = 0;
}

int
upgt_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct upgt_softc *sc = ic->ic_if.if_softc;

	usb_rem_task(sc->sc_udev, &sc->sc_task_newstate);
	timeout_del(&sc->scan_to);

	/* do it in a process context */
	sc->sc_state = nstate;
	sc->sc_arg = arg;
	usb_add_task(sc->sc_udev, &sc->sc_task_newstate);

	return (0);
}

void
upgt_newstate_task(void *arg)
{
	struct upgt_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	unsigned channel;

	switch (sc->sc_state) {
	case IEEE80211_S_INIT:
		DPRINTF(1, "%s: newstate is IEEE80211_S_INIT\n",
		    sc->sc_dev.dv_xname);

		/* do not accept any frames if the device is down */
		upgt_set_macfilter(sc, IEEE80211_S_INIT);
		upgt_set_led(sc, UPGT_LED_OFF);
		break;
	case IEEE80211_S_SCAN:
		DPRINTF(1, "%s: newstate is IEEE80211_S_SCAN\n",
		    sc->sc_dev.dv_xname);

		channel = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
		upgt_set_channel(sc, channel);
		timeout_add_msec(&sc->scan_to, 200);
		break;
	case IEEE80211_S_AUTH:
		DPRINTF(1, "%s: newstate is IEEE80211_S_AUTH\n",
		    sc->sc_dev.dv_xname);

		channel = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
		upgt_set_channel(sc, channel);
		break;
	case IEEE80211_S_ASSOC:
		DPRINTF(1, "%s: newstate is IEEE80211_S_ASSOC\n",
		    sc->sc_dev.dv_xname);
		break;
	case IEEE80211_S_RUN:
		DPRINTF(1, "%s: newstate is IEEE80211_S_RUN\n",
		    sc->sc_dev.dv_xname);

		ni = ic->ic_bss;

		/*
		 * TX rate control is done by the firmware.
		 * Report the maximum rate which is available therefore.
		 */
		ni->ni_txrate = ni->ni_rates.rs_nrates - 1;

		if (ic->ic_opmode != IEEE80211_M_MONITOR)
			upgt_set_macfilter(sc, IEEE80211_S_RUN);
		upgt_set_led(sc, UPGT_LED_ON);
		break;
	}

	sc->sc_newstate(ic, sc->sc_state, sc->sc_arg);
}

void
upgt_next_scan(void *arg)
{
	struct upgt_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;

	DPRINTF(2, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	if (ic->ic_state == IEEE80211_S_SCAN)
		ieee80211_next_scan(ifp);
}

void
upgt_start(struct ifnet *ifp)
{
	struct upgt_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct mbuf *m;
	int i;

	/* don't transmit packets if interface is busy or down */
	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	DPRINTF(2, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	for (i = 0; i < UPGT_TX_COUNT; i++) {
		struct upgt_data *data_tx = &sc->tx_data[i];

		m = mq_dequeue(&ic->ic_mgtq);
		if (m != NULL) {
			/* management frame */
			ni = m->m_pkthdr.ph_cookie;
#if NBPFILTER > 0
			if (ic->ic_rawbpf != NULL)
				bpf_mtap(ic->ic_rawbpf, m, BPF_DIRECTION_OUT);
#endif
			if ((data_tx->addr = upgt_mem_alloc(sc)) == 0) {
				printf("%s: no free prism memory!\n",
				    sc->sc_dev.dv_xname);
				return;
			}
			data_tx->ni = ni;
			data_tx->m = m;
			sc->tx_queued++;
		} else {
			/* data frame */
			if (ic->ic_state != IEEE80211_S_RUN)
				break;

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
			if ((data_tx->addr = upgt_mem_alloc(sc)) == 0) {
				printf("%s: no free prism memory!\n",
				    sc->sc_dev.dv_xname);
				return;
			}
			data_tx->ni = ni;
			data_tx->m = m;
			sc->tx_queued++;
		}
	}

	if (sc->tx_queued > 0) {
		DPRINTF(2, "%s: tx_queued=%d\n",
		    sc->sc_dev.dv_xname, sc->tx_queued);
		/* process the TX queue in process context */
		ifp->if_timer = 5;
		ifq_set_oactive(&ifp->if_snd);
		usb_rem_task(sc->sc_udev, &sc->sc_task_tx);
		usb_add_task(sc->sc_udev, &sc->sc_task_tx);
	}
}

void
upgt_watchdog(struct ifnet *ifp)
{
	struct upgt_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;

	if (ic->ic_state == IEEE80211_S_INIT)
		return;

	printf("%s: watchdog timeout!\n", sc->sc_dev.dv_xname);

	/* TODO: what shall we do on TX timeout? */

	ieee80211_watchdog(ifp);
}

void
upgt_tx_task(void *arg)
{
	struct upgt_softc *sc = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k;
	struct upgt_lmac_mem *mem;
	struct upgt_lmac_tx_desc *txdesc;
	struct mbuf *m;
	uint32_t addr;
	int len, i, s;
	usbd_status error;

	s = splusb();

	upgt_set_led(sc, UPGT_LED_BLINK);

	for (i = 0; i < UPGT_TX_COUNT; i++) {
		struct upgt_data *data_tx = &sc->tx_data[i];

		if (data_tx->m == NULL) {
			DPRINTF(2, "%s: %d: m is NULL\n",
			    sc->sc_dev.dv_xname, i);
			continue;
		}

		m = data_tx->m;
		addr = data_tx->addr + UPGT_MEMSIZE_FRAME_HEAD;

		/*
		 * Software crypto.
		 */
		wh = mtod(m, struct ieee80211_frame *);

		if (wh->i_fc[1] & IEEE80211_FC1_PROTECTED) {
			k = ieee80211_get_txkey(ic, wh, ic->ic_bss);

			if ((m = ieee80211_encrypt(ic, m, k)) == NULL) {
				splx(s);
				return;
			}

			/* in case packet header moved, reset pointer */
			wh = mtod(m, struct ieee80211_frame *);
		}

		/*
		 * Transmit the URB containing the TX data.
		 */
		bzero(data_tx->buf, MCLBYTES);

		mem = (struct upgt_lmac_mem *)data_tx->buf;
		mem->addr = htole32(addr);

		txdesc = (struct upgt_lmac_tx_desc *)(mem + 1);

		/* XXX differ between data and mgmt frames? */
		txdesc->header1.flags = UPGT_H1_FLAGS_TX_DATA;
		txdesc->header1.type = UPGT_H1_TYPE_TX_DATA;
		txdesc->header1.len = htole16(m->m_pkthdr.len);

		txdesc->header2.reqid = htole32(data_tx->addr);
		txdesc->header2.type = htole16(UPGT_H2_TYPE_TX_ACK_YES);
		txdesc->header2.flags = htole16(UPGT_H2_FLAGS_TX_ACK_YES);

		if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) ==
		    IEEE80211_FC0_TYPE_MGT) {
			/* always send mgmt frames at lowest rate (DS1) */
			memset(txdesc->rates, 0x10, sizeof(txdesc->rates));
		} else {
			bcopy(sc->sc_cur_rateset, txdesc->rates,
			    sizeof(txdesc->rates));
		}
		txdesc->type = htole32(UPGT_TX_DESC_TYPE_DATA);
		txdesc->pad3[0] = UPGT_TX_DESC_PAD3_SIZE;

#if NBPFILTER > 0
		if (sc->sc_drvbpf != NULL) {
			struct mbuf mb;
			struct upgt_tx_radiotap_header *tap = &sc->sc_txtap;

			tap->wt_flags = 0;
			tap->wt_rate = 0;	/* TODO: where to get from? */
			tap->wt_chan_freq =
			    htole16(ic->ic_bss->ni_chan->ic_freq);
			tap->wt_chan_flags =
			    htole16(ic->ic_bss->ni_chan->ic_flags);

			mb.m_data = (caddr_t)tap;
			mb.m_len = sc->sc_txtap_len;
			mb.m_next = m;
			mb.m_nextpkt = NULL;
			mb.m_type = 0;
			mb.m_flags = 0;
			bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_OUT);
		}
#endif
		/* copy frame below our TX descriptor header */
		m_copydata(m, 0, m->m_pkthdr.len,
		    data_tx->buf + (sizeof(*mem) + sizeof(*txdesc)));

		/* calculate frame size */
		len = sizeof(*mem) + sizeof(*txdesc) + m->m_pkthdr.len;

		/* we need to align the frame to a 4 byte boundary */
		len = (len + 3) & ~3;

		/* calculate frame checksum */
		mem->chksum = upgt_chksum_le((uint32_t *)txdesc,
		    len - sizeof(*mem));

		/* we do not need the mbuf anymore */
		m_freem(m);
		data_tx->m = NULL;

		DPRINTF(2, "%s: TX start data sending\n", sc->sc_dev.dv_xname);

		usbd_setup_xfer(data_tx->xfer, sc->sc_tx_pipeh, data_tx,
		    data_tx->buf, len, USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
		    UPGT_USB_TIMEOUT, NULL);
		error = usbd_transfer(data_tx->xfer);
		if (error != 0 && error != USBD_IN_PROGRESS) {
			printf("%s: could not transmit TX data URB!\n",
			    sc->sc_dev.dv_xname);
			splx(s);
			return;
		}

		DPRINTF(2, "%s: TX sent (%d bytes)\n",
		    sc->sc_dev.dv_xname, len);
	}

	/*
	 * If we don't regularly read the device statistics, the RX queue
	 * will stall.  It's strange, but it works, so we keep reading
	 * the statistics here.  *shrug*
	 */
	upgt_get_stats(sc);

	splx(s);
}

void
upgt_tx_done(struct upgt_softc *sc, uint8_t *data)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct upgt_lmac_tx_done_desc *desc;
	int i, s;

	s = splnet();

	desc = (struct upgt_lmac_tx_done_desc *)data;

	for (i = 0; i < UPGT_TX_COUNT; i++) {
		struct upgt_data *data_tx = &sc->tx_data[i];

		if (data_tx->addr == letoh32(desc->header2.reqid)) {
			upgt_mem_free(sc, data_tx->addr);
			ieee80211_release_node(ic, data_tx->ni);
			data_tx->ni = NULL;
			data_tx->addr = 0;

			sc->tx_queued--;

			DPRINTF(2, "%s: TX done: ", sc->sc_dev.dv_xname);
			DPRINTF(2, "memaddr=0x%08x, status=0x%04x, rssi=%d, ",
			    letoh32(desc->header2.reqid),
			    letoh16(desc->status),
			    letoh16(desc->rssi));
			DPRINTF(2, "seq=%d\n", letoh16(desc->seq));
			break;
		}
	}

	if (sc->tx_queued == 0) {
		/* TX queued was processed, continue */
		ifp->if_timer = 0;
		ifq_clr_oactive(&ifp->if_snd);
		upgt_start(ifp);
	}

	splx(s);
}

void
upgt_rx_cb(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct upgt_data *data_rx = priv;
	struct upgt_softc *sc = data_rx->sc;
	int len;
	struct upgt_lmac_header *header;
	struct upgt_lmac_eeprom *eeprom;
	uint8_t h1_type;
	uint16_t h2_type;

	DPRINTF(3, "%s: %s\n", sc->sc_dev.dv_xname, __func__);

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_rx_pipeh);
		goto skip;
	}
	usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);

	/*
	 * Check what type of frame came in.
	 */
	header = (struct upgt_lmac_header *)(data_rx->buf + 4);

	h1_type = header->header1.type;
	h2_type = letoh16(header->header2.type);

	if (h1_type == UPGT_H1_TYPE_CTRL &&
	    h2_type == UPGT_H2_TYPE_EEPROM) {
		eeprom = (struct upgt_lmac_eeprom *)(data_rx->buf + 4);
		uint16_t eeprom_offset = letoh16(eeprom->offset);
		uint16_t eeprom_len = letoh16(eeprom->len);

		DPRINTF(2, "%s: received EEPROM block (offset=%d, len=%d)\n",
			sc->sc_dev.dv_xname, eeprom_offset, eeprom_len);

		bcopy(data_rx->buf + sizeof(struct upgt_lmac_eeprom) + 4,
			sc->sc_eeprom + eeprom_offset, eeprom_len);

		/* EEPROM data has arrived in time, wakeup tsleep() */
		wakeup(sc);
	} else
	if (h1_type == UPGT_H1_TYPE_CTRL &&
	    h2_type == UPGT_H2_TYPE_TX_DONE) {
		DPRINTF(2, "%s: received 802.11 TX done\n",
		    sc->sc_dev.dv_xname);

		upgt_tx_done(sc, data_rx->buf + 4);
	} else
	if (h1_type == UPGT_H1_TYPE_RX_DATA ||
	    h1_type == UPGT_H1_TYPE_RX_DATA_MGMT) {
		DPRINTF(3, "%s: received 802.11 RX data\n",
		    sc->sc_dev.dv_xname);

		upgt_rx(sc, data_rx->buf + 4, letoh16(header->header1.len));
	} else
	if (h1_type == UPGT_H1_TYPE_CTRL &&
	    h2_type == UPGT_H2_TYPE_STATS) {
		DPRINTF(2, "%s: received statistic data\n",
		    sc->sc_dev.dv_xname);

		/* TODO: what could we do with the statistic data? */
	} else {
		/* ignore unknown frame types */
		DPRINTF(1, "%s: received unknown frame type 0x%02x\n",
		    sc->sc_dev.dv_xname, header->header1.type);
	}

skip:	/* setup new transfer */
	usbd_setup_xfer(xfer, sc->sc_rx_pipeh, data_rx, data_rx->buf, MCLBYTES,
	    USBD_SHORT_XFER_OK, USBD_NO_TIMEOUT, upgt_rx_cb);
	(void)usbd_transfer(xfer);
}

void
upgt_rx(struct upgt_softc *sc, uint8_t *data, int pkglen)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct upgt_lmac_rx_desc *rxdesc;
	struct ieee80211_frame *wh;
	struct ieee80211_rxinfo rxi;
	struct ieee80211_node *ni;
	struct mbuf *m;
	int s;

	/* access RX packet descriptor */
	rxdesc = (struct upgt_lmac_rx_desc *)data;

	/* create mbuf which is suitable for strict alignment archs */
	m = m_devget(rxdesc->data, pkglen, ETHER_ALIGN);
	if (m == NULL) {
		DPRINTF(1, "%s: could not create RX mbuf!\n", sc->sc_dev.dv_xname);
		ifp->if_ierrors++;
		return;
	}

	s = splnet();

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct mbuf mb;
		struct upgt_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = IEEE80211_RADIOTAP_F_FCS;
		tap->wr_rate = upgt_rx_rate(sc, rxdesc->rate);
		tap->wr_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wr_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);
		tap->wr_antsignal = rxdesc->rssi;

		mb.m_data = (caddr_t)tap;
		mb.m_len = sc->sc_rxtap_len;
		mb.m_next = m;
		mb.m_nextpkt = NULL;
		mb.m_type = 0;
		mb.m_flags = 0;
		bpf_mtap(sc->sc_drvbpf, &mb, BPF_DIRECTION_IN);
	}
#endif
	/* trim FCS */
	m_adj(m, -IEEE80211_CRC_LEN);

	wh = mtod(m, struct ieee80211_frame *);
	ni = ieee80211_find_rxnode(ic, wh);

	/* push the frame up to the 802.11 stack */
	memset(&rxi, 0, sizeof(rxi));
	rxi.rxi_flags = 0;
	rxi.rxi_rssi = rxdesc->rssi;
	ieee80211_input(ifp, m, ni, &rxi);

	/* node is no longer needed */
	ieee80211_release_node(ic, ni);

	splx(s);

	DPRINTF(3, "%s: RX done\n", sc->sc_dev.dv_xname);
}

void
upgt_setup_rates(struct upgt_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;

	/*
	 * 0x01 = OFMD6   0x10 = DS1
	 * 0x04 = OFDM9   0x11 = DS2
	 * 0x06 = OFDM12  0x12 = DS5
	 * 0x07 = OFDM18  0x13 = DS11
	 * 0x08 = OFDM24
	 * 0x09 = OFDM36
	 * 0x0a = OFDM48
	 * 0x0b = OFDM54
	 */
	const uint8_t rateset_auto_11b[] =
	    { 0x13, 0x13, 0x12, 0x11, 0x11, 0x10, 0x10, 0x10 };
	const uint8_t rateset_auto_11g[] =
	    { 0x0b, 0x0a, 0x09, 0x08, 0x07, 0x06, 0x04, 0x01 };
	const uint8_t rateset_fix_11bg[] =
	    { 0x10, 0x11, 0x12, 0x13, 0x01, 0x04, 0x06, 0x07,
	      0x08, 0x09, 0x0a, 0x0b };

	if (ic->ic_fixed_rate == -1) {
		/*
		 * Automatic rate control is done by the device.
		 * We just pass the rateset from which the device
		 * will pickup a rate.
		 */
		if (ic->ic_curmode == IEEE80211_MODE_11B)
			bcopy(rateset_auto_11b, sc->sc_cur_rateset,
			    sizeof(sc->sc_cur_rateset));
		if (ic->ic_curmode == IEEE80211_MODE_11G ||
		    ic->ic_curmode == IEEE80211_MODE_AUTO)
			bcopy(rateset_auto_11g, sc->sc_cur_rateset,
			    sizeof(sc->sc_cur_rateset));
	} else {
		/* set a fixed rate */
		memset(sc->sc_cur_rateset, rateset_fix_11bg[ic->ic_fixed_rate],
		    sizeof(sc->sc_cur_rateset));
	}
}

uint8_t
upgt_rx_rate(struct upgt_softc *sc, const int rate)
{
	struct ieee80211com *ic = &sc->sc_ic;

	if (ic->ic_curmode == IEEE80211_MODE_11B) {
		if (rate < 0 || rate > 3)
			/* invalid rate */
			return (0);

		switch (rate) {
		case 0:
			return (2);
		case 1:
			return (4);
		case 2:
			return (11);
		case 3:
			return (22);
		default:
			return (0);
		}
	}

	if (ic->ic_curmode == IEEE80211_MODE_11G) {
		if (rate < 0 || rate > 11)
			/* invalid rate */
			return (0);

		switch (rate) {
		case 0:
			return (2);
		case 1:
			return (4);
		case 2:
			return (11);
		case 3:
			return (22);
		case 4:
			return (12);
		case 5:
			return (18);
		case 6:
			return (24);
		case 7:
			return (36);
		case 8:
			return (48);
		case 9:
			return (72);
		case 10:
			return (96);
		case 11:
			return (108);
		default:
			return (0);
		}
	}

	return (0);
}

int
upgt_set_macfilter(struct upgt_softc *sc, uint8_t state)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	struct upgt_data *data_cmd = &sc->cmd_data;
	struct upgt_lmac_mem *mem;
	struct upgt_lmac_filter *filter;
	int len;
	uint8_t broadcast[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	/*
	 * Transmit the URB containing the CMD data.
	 */
	bzero(data_cmd->buf, MCLBYTES);

	mem = (struct upgt_lmac_mem *)data_cmd->buf;
	mem->addr = htole32(sc->sc_memaddr_frame_start +
	    UPGT_MEMSIZE_FRAME_HEAD);

	filter = (struct upgt_lmac_filter *)(mem + 1);

	filter->header1.flags = UPGT_H1_FLAGS_TX_NO_CALLBACK;
	filter->header1.type = UPGT_H1_TYPE_CTRL;
	filter->header1.len = htole16(
	    sizeof(struct upgt_lmac_filter) -
	    sizeof(struct upgt_lmac_header));

	filter->header2.reqid = htole32(sc->sc_memaddr_frame_start);
	filter->header2.type = htole16(UPGT_H2_TYPE_MACFILTER);
	filter->header2.flags = 0;

	switch (state) {
	case IEEE80211_S_INIT:
		DPRINTF(1, "%s: set MAC filter to INIT\n",
		    sc->sc_dev.dv_xname);

		filter->type = htole16(UPGT_FILTER_TYPE_RESET);
		break;
	case IEEE80211_S_SCAN:
		DPRINTF(1, "%s: set MAC filter to SCAN (bssid %s)\n",
		    sc->sc_dev.dv_xname, ether_sprintf(broadcast));

		filter->type = htole16(UPGT_FILTER_TYPE_NONE);
		IEEE80211_ADDR_COPY(filter->dst, ic->ic_myaddr);
		IEEE80211_ADDR_COPY(filter->src, broadcast);
		filter->unknown1 = htole16(UPGT_FILTER_UNKNOWN1);
		filter->rxaddr = htole32(sc->sc_memaddr_rx_start);
		filter->unknown2 = htole16(UPGT_FILTER_UNKNOWN2);
		filter->rxhw = htole32(sc->sc_eeprom_hwrx);
		filter->unknown3 = htole16(UPGT_FILTER_UNKNOWN3);
		break;
	case IEEE80211_S_RUN:
		DPRINTF(1, "%s: set MAC filter to RUN (bssid %s)\n",
		    sc->sc_dev.dv_xname, ether_sprintf(ni->ni_bssid));

		filter->type = htole16(UPGT_FILTER_TYPE_STA);
		IEEE80211_ADDR_COPY(filter->dst, ic->ic_myaddr);
		IEEE80211_ADDR_COPY(filter->src, ni->ni_bssid);
		filter->unknown1 = htole16(UPGT_FILTER_UNKNOWN1);
		filter->rxaddr = htole32(sc->sc_memaddr_rx_start);
		filter->unknown2 = htole16(UPGT_FILTER_UNKNOWN2);
		filter->rxhw = htole32(sc->sc_eeprom_hwrx);
		filter->unknown3 = htole16(UPGT_FILTER_UNKNOWN3);
		break;
	default:
		printf("%s: MAC filter does not know that state!\n",
		    sc->sc_dev.dv_xname);
		break;
	}

	len = sizeof(*mem) + sizeof(*filter);

	mem->chksum = upgt_chksum_le((uint32_t *)filter,
	    len - sizeof(*mem));

	if (upgt_bulk_xmit(sc, data_cmd, sc->sc_tx_pipeh, &len, 0) != 0) {
		printf("%s: could not transmit macfilter CMD data URB!\n",
		    sc->sc_dev.dv_xname);
		return (EIO);
	}

	return (0);
}

int
upgt_set_channel(struct upgt_softc *sc, unsigned channel)
{
	struct upgt_data *data_cmd = &sc->cmd_data;
	struct upgt_lmac_mem *mem;
	struct upgt_lmac_channel *chan;
	int len;

	DPRINTF(1, "%s: %s: %d\n", sc->sc_dev.dv_xname, __func__, channel);

	/*
	 * Transmit the URB containing the CMD data.
	 */
	bzero(data_cmd->buf, MCLBYTES);

	mem = (struct upgt_lmac_mem *)data_cmd->buf;
	mem->addr = htole32(sc->sc_memaddr_frame_start +
	    UPGT_MEMSIZE_FRAME_HEAD);

	chan = (struct upgt_lmac_channel *)(mem + 1);

	chan->header1.flags = UPGT_H1_FLAGS_TX_NO_CALLBACK;
	chan->header1.type = UPGT_H1_TYPE_CTRL;
	chan->header1.len = htole16(
	    sizeof(struct upgt_lmac_channel) -
	    sizeof(struct upgt_lmac_header));

	chan->header2.reqid = htole32(sc->sc_memaddr_frame_start);
	chan->header2.type = htole16(UPGT_H2_TYPE_CHANNEL);
	chan->header2.flags = 0;

	chan->unknown1 = htole16(UPGT_CHANNEL_UNKNOWN1);
	chan->unknown2 = htole16(UPGT_CHANNEL_UNKNOWN2);
	chan->freq6 = sc->sc_eeprom_freq6[channel];
	chan->settings = sc->sc_eeprom_freq6_settings;
	chan->unknown3 = UPGT_CHANNEL_UNKNOWN3;

	bcopy(&sc->sc_eeprom_freq3[channel].data, chan->freq3_1,
	    sizeof(chan->freq3_1));

	bcopy(&sc->sc_eeprom_freq4[channel], chan->freq4,
	    sizeof(sc->sc_eeprom_freq4[channel]));

	bcopy(&sc->sc_eeprom_freq3[channel].data, chan->freq3_2,
	    sizeof(chan->freq3_2));

	len = sizeof(*mem) + sizeof(*chan);

	mem->chksum = upgt_chksum_le((uint32_t *)chan,
	    len - sizeof(*mem));

	if (upgt_bulk_xmit(sc, data_cmd, sc->sc_tx_pipeh, &len, 0) != 0) {
		printf("%s: could not transmit channel CMD data URB!\n",
		    sc->sc_dev.dv_xname);
		return (EIO);
	}

	return (0);
}

void
upgt_set_led(struct upgt_softc *sc, int action)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct upgt_data *data_cmd = &sc->cmd_data;
	struct upgt_lmac_mem *mem;
	struct upgt_lmac_led *led;
	int len;

	/*
	 * Transmit the URB containing the CMD data.
	 */
	bzero(data_cmd->buf, MCLBYTES);

	mem = (struct upgt_lmac_mem *)data_cmd->buf;
	mem->addr = htole32(sc->sc_memaddr_frame_start +
	    UPGT_MEMSIZE_FRAME_HEAD);

	led = (struct upgt_lmac_led *)(mem + 1);

	led->header1.flags = UPGT_H1_FLAGS_TX_NO_CALLBACK;
	led->header1.type = UPGT_H1_TYPE_CTRL;
	led->header1.len = htole16(
	    sizeof(struct upgt_lmac_led) -
	    sizeof(struct upgt_lmac_header));

	led->header2.reqid = htole32(sc->sc_memaddr_frame_start);
	led->header2.type = htole16(UPGT_H2_TYPE_LED);
	led->header2.flags = 0;

	switch (action) {
	case UPGT_LED_OFF:
		led->mode = htole16(UPGT_LED_MODE_SET);
		led->action_fix = 0;
		led->action_tmp = htole16(UPGT_LED_ACTION_OFF);
		led->action_tmp_dur = 0;
		break;
	case UPGT_LED_ON:
		led->mode = htole16(UPGT_LED_MODE_SET);
		led->action_fix = 0;
		led->action_tmp = htole16(UPGT_LED_ACTION_ON);
		led->action_tmp_dur = 0;
		break;
	case UPGT_LED_BLINK:
		if (ic->ic_state != IEEE80211_S_RUN)
			return;
		if (sc->sc_led_blink)
			/* previous blink was not finished */
			return;
		led->mode = htole16(UPGT_LED_MODE_SET);
		led->action_fix = htole16(UPGT_LED_ACTION_OFF);
		led->action_tmp = htole16(UPGT_LED_ACTION_ON);
		led->action_tmp_dur = htole16(UPGT_LED_ACTION_TMP_DUR);
		/* lock blink */
		sc->sc_led_blink = 1;
		timeout_add_msec(&sc->led_to, UPGT_LED_ACTION_TMP_DUR);
		break;
	default:
		return;
	}

	len = sizeof(*mem) + sizeof(*led);

	mem->chksum = upgt_chksum_le((uint32_t *)led,
	    len - sizeof(*mem));

	if (upgt_bulk_xmit(sc, data_cmd, sc->sc_tx_pipeh, &len, 0) != 0) {
		printf("%s: could not transmit led CMD URB!\n",
		    sc->sc_dev.dv_xname);
	}
}

void
upgt_set_led_blink(void *arg)
{
	struct upgt_softc *sc = arg;

	/* blink finished, we are ready for a next one */
	sc->sc_led_blink = 0;
	timeout_del(&sc->led_to);
}

int
upgt_get_stats(struct upgt_softc *sc)
{
	struct upgt_data *data_cmd = &sc->cmd_data;
	struct upgt_lmac_mem *mem;
	struct upgt_lmac_stats *stats;
	int len;

	/*
	 * Transmit the URB containing the CMD data.
	 */
	bzero(data_cmd->buf, MCLBYTES);

	mem = (struct upgt_lmac_mem *)data_cmd->buf;
	mem->addr = htole32(sc->sc_memaddr_frame_start +
	    UPGT_MEMSIZE_FRAME_HEAD);

	stats = (struct upgt_lmac_stats *)(mem + 1);

	stats->header1.flags = 0;
	stats->header1.type = UPGT_H1_TYPE_CTRL;
	stats->header1.len = htole16(
	    sizeof(struct upgt_lmac_stats) -
	    sizeof(struct upgt_lmac_header));

	stats->header2.reqid = htole32(sc->sc_memaddr_frame_start);
	stats->header2.type = htole16(UPGT_H2_TYPE_STATS);
	stats->header2.flags = 0;

	len = sizeof(*mem) + sizeof(*stats);

	mem->chksum = upgt_chksum_le((uint32_t *)stats,
	    len - sizeof(*mem));

	if (upgt_bulk_xmit(sc, data_cmd, sc->sc_tx_pipeh, &len, 0) != 0) {
		printf("%s: could not transmit statistics CMD data URB!\n",
		    sc->sc_dev.dv_xname);
		return (EIO);
	}

	return (0);

}

int
upgt_alloc_tx(struct upgt_softc *sc)
{
	int i;

	sc->tx_queued = 0;

	for (i = 0; i < UPGT_TX_COUNT; i++) {
		struct upgt_data *data_tx = &sc->tx_data[i];

		data_tx->sc = sc;

		data_tx->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data_tx->xfer == NULL) {
			printf("%s: could not allocate TX xfer!\n",
			    sc->sc_dev.dv_xname);
			return (ENOMEM);
		}

		data_tx->buf = usbd_alloc_buffer(data_tx->xfer, MCLBYTES);
		if (data_tx->buf == NULL) {
			printf("%s: could not allocate TX buffer!\n",
			    sc->sc_dev.dv_xname);
			return (ENOMEM);
		}

		bzero(data_tx->buf, MCLBYTES);
	}

	return (0);
}

int
upgt_alloc_rx(struct upgt_softc *sc)
{
	struct upgt_data *data_rx = &sc->rx_data;

	data_rx->sc = sc;

	data_rx->xfer = usbd_alloc_xfer(sc->sc_udev);
	if (data_rx->xfer == NULL) {
		printf("%s: could not allocate RX xfer!\n",
		    sc->sc_dev.dv_xname);
		return (ENOMEM);
	}

	data_rx->buf = usbd_alloc_buffer(data_rx->xfer, MCLBYTES);
	if (data_rx->buf == NULL) {
		printf("%s: could not allocate RX buffer!\n",
		    sc->sc_dev.dv_xname);
		return (ENOMEM);
	}

	bzero(data_rx->buf, MCLBYTES);

	return (0);
}

int
upgt_alloc_cmd(struct upgt_softc *sc)
{
	struct upgt_data *data_cmd = &sc->cmd_data;

	data_cmd->sc = sc;

	data_cmd->xfer = usbd_alloc_xfer(sc->sc_udev);
	if (data_cmd->xfer == NULL) {
		printf("%s: could not allocate RX xfer!\n",
		    sc->sc_dev.dv_xname);
		return (ENOMEM);
	}

	data_cmd->buf = usbd_alloc_buffer(data_cmd->xfer, MCLBYTES);
	if (data_cmd->buf == NULL) {
		printf("%s: could not allocate RX buffer!\n",
		    sc->sc_dev.dv_xname);
		return (ENOMEM);
	}

	bzero(data_cmd->buf, MCLBYTES);

	return (0);
}

void
upgt_free_tx(struct upgt_softc *sc)
{
	int i;

	for (i = 0; i < UPGT_TX_COUNT; i++) {
		struct upgt_data *data_tx = &sc->tx_data[i];

		if (data_tx->xfer != NULL) {
			usbd_free_xfer(data_tx->xfer);
			data_tx->xfer = NULL;
		}

		data_tx->ni = NULL;
	}
}

void
upgt_free_rx(struct upgt_softc *sc)
{
	struct upgt_data *data_rx = &sc->rx_data;

	if (data_rx->xfer != NULL) {
		usbd_free_xfer(data_rx->xfer);
		data_rx->xfer = NULL;
	}

	data_rx->ni = NULL;
}

void
upgt_free_cmd(struct upgt_softc *sc)
{
	struct upgt_data *data_cmd = &sc->cmd_data;

	if (data_cmd->xfer != NULL) {
		usbd_free_xfer(data_cmd->xfer);
		data_cmd->xfer = NULL;
	}
}

int
upgt_bulk_xmit(struct upgt_softc *sc, struct upgt_data *data,
    struct usbd_pipe *pipeh, uint32_t *size, int flags)
{
        usbd_status status;

	usbd_setup_xfer(data->xfer, pipeh, 0, data->buf, *size,
	    USBD_NO_COPY | USBD_SYNCHRONOUS | flags, UPGT_USB_TIMEOUT, NULL);
	status = usbd_transfer(data->xfer);
	if (status != USBD_NORMAL_COMPLETION) {
		printf("%s: %s: error %s!\n",
		    sc->sc_dev.dv_xname, __func__, usbd_errstr(status));
		return (EIO);
	}

	return (0);
}

void
upgt_hexdump(void *buf, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (i % 16 == 0)
			printf("%s%5i:", i ? "\n" : "", i);
		if (i % 4 == 0)
			printf(" ");
		printf("%02x", (int)*((u_char *)buf + i));
	}
	printf("\n");
}

uint32_t
upgt_crc32_le(const void *buf, size_t size)
{
	uint32_t crc;

	crc = ether_crc32_le(buf, size);

	/* apply final XOR value as common for CRC-32 */
	crc = htole32(crc ^ 0xffffffffU);

	return (crc);
}

/*
 * The firmware awaits a checksum for each frame we send to it.
 * The algorithm used therefor is uncommon but somehow similar to CRC32.
 */
uint32_t
upgt_chksum_le(const uint32_t *buf, size_t size)
{
	int i;
	uint32_t crc = 0;

	for (i = 0; i < size; i += sizeof(uint32_t)) {
		crc = htole32(crc ^ *buf++);
		crc = htole32((crc >> 5) ^ (crc << 3));
	}

	return (crc);
}
