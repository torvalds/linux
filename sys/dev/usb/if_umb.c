/*	$OpenBSD: if_umb.c,v 1.65 2025/07/10 14:27:43 gerhard Exp $ */

/*
 * Copyright (c) 2016 genua mbH
 * All rights reserved.
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
 * Mobile Broadband Interface Model specification:
 * https://www.usb.org/sites/default/files/MBIM10Errata1_073013.zip
 * Compliance testing guide
 * https://www.usb.org/sites/default/files/MBIM-Compliance-1.0.pdf
 */

#include "bpfilter.h"
#include "kstat.h"

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/syslog.h>
#include <sys/kstat.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_var.h>

#ifdef INET6
#include <netinet6/in6_var.h>
#include <netinet6/in6_ifattach.h>
#include <netinet6/nd6.h>
#endif

#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/usbcdc.h>

#include <dev/usb/mbim.h>
#include <dev/usb/if_umb.h>

#ifdef UMB_DEBUG
#define DPRINTF(x...)							\
		do { if (umb_debug) log(LOG_DEBUG, x); } while (0)

#define DPRINTFN(n, x...)						\
		do { if (umb_debug >= (n)) log(LOG_DEBUG, x); } while (0)

#define DDUMPN(n, b, l)							\
		do {							\
			if (umb_debug >= (n))				\
				umb_dump((b), (l));			\
		} while (0)

int	 umb_debug = 0;
char	*umb_uuid2str(uint8_t [MBIM_UUID_LEN]);
void	 umb_dump(void *, int);

#else
#define DPRINTF(x...)		do { } while (0)
#define DPRINTFN(n, x...)	do { } while (0)
#define DDUMPN(n, b, l)		do { } while (0)
#endif

#define DEVNAM(sc)		(((struct umb_softc *)(sc))->sc_dev.dv_xname)

/*
 * State change timeout
 */
#define UMB_STATE_CHANGE_TIMEOUT	30

/*
 * State change flags
 */
#define UMB_NS_DONT_DROP	0x0001	/* do not drop below current state */
#define UMB_NS_DONT_RAISE	0x0002	/* do not raise below current state */

/*
 * Diagnostic macros
 */
const struct umb_valdescr umb_regstates[] = MBIM_REGSTATE_DESCRIPTIONS;
const struct umb_valdescr umb_dataclasses[] = MBIM_DATACLASS_DESCRIPTIONS;
const struct umb_valdescr umb_simstate[] = MBIM_SIMSTATE_DESCRIPTIONS;
const struct umb_valdescr umb_messages[] = MBIM_MESSAGES_DESCRIPTIONS;
const struct umb_valdescr umb_status[] = MBIM_STATUS_DESCRIPTIONS;
const struct umb_valdescr umb_cids[] = MBIM_CID_DESCRIPTIONS;
const struct umb_valdescr umb_pktstate[] = MBIM_PKTSRV_STATE_DESCRIPTIONS;
const struct umb_valdescr umb_actstate[] = MBIM_ACTIVATION_STATE_DESCRIPTIONS;
const struct umb_valdescr umb_error[] = MBIM_ERROR_DESCRIPTIONS;
const struct umb_valdescr umb_pintype[] = MBIM_PINTYPE_DESCRIPTIONS;
const struct umb_valdescr umb_istate[] = UMB_INTERNAL_STATE_DESCRIPTIONS;

#define umb_regstate(c)		umb_val2descr(umb_regstates, (c))
#define umb_dataclass(c)	umb_val2descr(umb_dataclasses, (c))
#define umb_simstate(s)		umb_val2descr(umb_simstate, (s))
#define umb_request2str(m)	umb_val2descr(umb_messages, (m))
#define umb_status2str(s)	umb_val2descr(umb_status, (s))
#define umb_cid2str(c)		umb_val2descr(umb_cids, (c))
#define umb_packet_state(s)	umb_val2descr(umb_pktstate, (s))
#define umb_activation(s)	umb_val2descr(umb_actstate, (s))
#define umb_error2str(e)	umb_val2descr(umb_error, (e))
#define umb_pin_type(t)		umb_val2descr(umb_pintype, (t))
#define umb_istate(s)		umb_val2descr(umb_istate, (s))

int		 umb_match(struct device *, void *, void *);
void		 umb_attach(struct device *, struct device *, void *);
int		 umb_detach(struct device *, int);
void		 umb_ncm_setup(struct umb_softc *);
void		 umb_ncm_setup_format(struct umb_softc *);
int		 umb_alloc_xfers(struct umb_softc *);
void		 umb_free_xfers(struct umb_softc *);
int		 umb_alloc_bulkpipes(struct umb_softc *);
void		 umb_close_bulkpipes(struct umb_softc *);
int		 umb_ioctl(struct ifnet *, u_long, caddr_t);
int		 umb_output(struct ifnet *, struct mbuf *, struct sockaddr *,
		    struct rtentry *);
void		 umb_start(struct ifnet *);
void		 umb_rtrequest(struct ifnet *, int, struct rtentry *);
void		 umb_watchdog(struct ifnet *);
void		 umb_statechg_timeout(void *);

void		 umb_newstate(struct umb_softc *, enum umb_state, int);
void		 umb_state_task(void *);
void		 umb_up(struct umb_softc *);
void		 umb_down(struct umb_softc *, int);

void		 umb_get_response_task(void *);

void		 umb_decode_response(struct umb_softc *, void *, int);
void		 umb_handle_indicate_status_msg(struct umb_softc *, void *,
		    int);
void		 umb_handle_opendone_msg(struct umb_softc *, void *, int);
void		 umb_handle_closedone_msg(struct umb_softc *, void *, int);
int		 umb_decode_register_state(struct umb_softc *, void *, int);
int		 umb_decode_devices_caps(struct umb_softc *, void *, int);
int		 umb_decode_subscriber_status(struct umb_softc *, void *, int);
int		 umb_decode_radio_state(struct umb_softc *, void *, int);
int		 umb_decode_pin(struct umb_softc *, void *, int);
int		 umb_decode_packet_service(struct umb_softc *, void *, int);
int		 umb_decode_signal_state(struct umb_softc *, void *, int);
int		 umb_decode_connect_info(struct umb_softc *, void *, int);
void		 umb_clear_addr(struct umb_softc *);
int		 umb_add_inet_config(struct umb_softc *, struct in_addr, u_int,
		    struct in_addr);
int		 umb_add_inet6_config(struct umb_softc *, struct in6_addr *,
		    u_int, struct in6_addr *);
void		 umb_send_inet_proposal(struct umb_softc *, int);
int		 umb_decode_ip_configuration(struct umb_softc *, void *, int);
void		 umb_rx(struct umb_softc *);
void		 umb_rxeof(struct usbd_xfer *, void *, usbd_status);
int		 umb_encap(struct umb_softc *, int);
void		 umb_txeof(struct usbd_xfer *, void *, usbd_status);
void		 umb_decap(struct umb_softc *, struct usbd_xfer *);

usbd_status	 umb_send_encap_command(struct umb_softc *, void *, int);
int		 umb_get_encap_response(struct umb_softc *, void *, int *);
void		 umb_ctrl_msg(struct umb_softc *, uint32_t, void *, int);

void		 umb_open(struct umb_softc *);
void		 umb_close(struct umb_softc *);

int		 umb_setpin(struct umb_softc *, int, int, void *, int, void *,
		    int);
void		 umb_setdataclass(struct umb_softc *);
void		 umb_radio(struct umb_softc *, int);
void		 umb_allocate_cid(struct umb_softc *);
void		 umb_send_fcc_auth(struct umb_softc *);
void		 umb_packet_service(struct umb_softc *, int);
void		 umb_connect(struct umb_softc *);
void		 umb_disconnect(struct umb_softc *);
void		 umb_send_connect(struct umb_softc *, int);

void		 umb_qry_ipconfig(struct umb_softc *);
void		 umb_cmd(struct umb_softc *, int, int, void *, int);
void		 umb_cmd1(struct umb_softc *, int, int, void *, int, uint8_t *);
void		 umb_command_done(struct umb_softc *, void *, int);
void		 umb_decode_cid(struct umb_softc *, uint32_t, void *, int);
void		 umb_decode_qmi(struct umb_softc *, uint8_t *, int);

void		 umb_intr(struct usbd_xfer *, void *, usbd_status);

#if NKSTAT > 0
void		 umb_kstat_attach(struct umb_softc *);
void		 umb_kstat_detach(struct umb_softc *);

struct umb_kstat_signal {
	struct kstat_kv		rssi;
	struct kstat_kv		error_rate;
	struct kstat_kv		reports;
};
#endif

int		 umb_xfer_tout = USBD_DEFAULT_TIMEOUT;

uint8_t		 umb_uuid_basic_connect[] = MBIM_UUID_BASIC_CONNECT;
uint8_t		 umb_uuid_context_internet[] = MBIM_UUID_CONTEXT_INTERNET;
uint8_t		 umb_uuid_qmi_mbim[] = MBIM_UUID_QMI_MBIM;
uint32_t	 umb_session_id = 0;

struct cfdriver umb_cd = {
	NULL, "umb", DV_IFNET
};

const struct cfattach umb_ca = {
	sizeof (struct umb_softc),
	umb_match,
	umb_attach,
	umb_detach,
	NULL,
};

int umb_delay = 4000;

struct umb_quirk {
	struct usb_devno	 dev;
	u_int32_t		 umb_flags;
	int			 umb_confno;
	int			 umb_match;
};
const struct umb_quirk umb_quirks[] = {
	{ { USB_VENDOR_DELL, USB_PRODUCT_DELL_DW5821E_1 },
	  0,
	  2,
	  UMATCH_VENDOR_PRODUCT
	},

	{ { USB_VENDOR_DELL, USB_PRODUCT_DELL_DW5821E_2 },
	  0,
	  2,
	  UMATCH_VENDOR_PRODUCT
	},

	{ { USB_VENDOR_HUAWEI, USB_PRODUCT_HUAWEI_ME906S },
	  UMBFLG_NDP_AT_END,
	  3,
	  UMATCH_VENDOR_PRODUCT
	},

	{ { USB_VENDOR_SIERRA, USB_PRODUCT_SIERRA_EM7455 },
	  UMBFLG_FCC_AUTH_REQUIRED,
	  0,
	  0
	},

	{ { USB_VENDOR_SIMCOM, USB_PRODUCT_SIMCOM_SIM7600 },
	  0,
	  1,
	  UMATCH_VENDOR_PRODUCT
	},
};

#define umb_lookup(vid, pid)		\
	((const struct umb_quirk *)usb_lookup(umb_quirks, vid, pid))

uint8_t umb_qmi_alloc_cid[] = {
	0x01,
	0x0f, 0x00,		/* len */
	0x00,			/* QMUX flags */
	0x00,			/* service "ctl" */
	0x00,			/* CID */
	0x00,			/* QMI flags */
	0x01,			/* transaction */
	0x22, 0x00,		/* msg "Allocate CID" */
	0x04, 0x00,		/* TLV len */
	0x01, 0x01, 0x00, 0x02	/* TLV */
};

uint8_t umb_qmi_fcc_auth[] = {
	0x01,
	0x0c, 0x00,		/* len */
	0x00,			/* QMUX flags */
	0x02,			/* service "dms" */
#define UMB_QMI_CID_OFFS	5
	0x00,			/* CID (filled in later) */
	0x00,			/* QMI flags */
	0x01, 0x00,		/* transaction */
	0x5f, 0x55,		/* msg "Send FCC Authentication" */
	0x00, 0x00		/* TLV len */
};

int
umb_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;
	const struct umb_quirk *quirk;
	usb_interface_descriptor_t *id;

	quirk = umb_lookup(uaa->vendor, uaa->product);
	if (quirk != NULL && quirk->umb_match)
		return (quirk->umb_match);
	if (!uaa->iface)
		return UMATCH_NONE;
	if ((id = usbd_get_interface_descriptor(uaa->iface)) == NULL)
		return UMATCH_NONE;

	/*
	 * If this function implements NCM, check if alternate setting
	 * 1 implements MBIM.
	 */
	if (id->bInterfaceClass == UICLASS_CDC &&
	    id->bInterfaceSubClass ==
	    UISUBCLASS_NETWORK_CONTROL_MODEL)
		id = usbd_find_idesc(uaa->device->cdesc, uaa->iface->index, 1);
	if (id == NULL)
		return UMATCH_NONE;

	if (id->bInterfaceClass == UICLASS_CDC &&
	    id->bInterfaceSubClass ==
	    UISUBCLASS_MOBILE_BROADBAND_INTERFACE_MODEL &&
	    id->bInterfaceProtocol == 0)
		return UMATCH_IFACECLASS_IFACESUBCLASS_IFACEPROTO;

	return UMATCH_NONE;
}

void
umb_attach(struct device *parent, struct device *self, void *aux)
{
	struct umb_softc *sc = (struct umb_softc *)self;
	struct usb_attach_arg *uaa = aux;
	const struct umb_quirk *quirk;
	usbd_status status;
	struct usbd_desc_iter iter;
	const usb_descriptor_t *desc;
	int	 v;
	struct usb_cdc_union_descriptor *ud;
	struct mbim_descriptor *md;
	int	 i;
	int	 ctrl_ep;
	usb_interface_descriptor_t *id;
	usb_config_descriptor_t	*cd;
	usb_endpoint_descriptor_t *ed;
	usb_interface_assoc_descriptor_t *ad;
	int	 current_ifaceno = -1;
	int	 data_ifaceno = -1;
	int	 altnum;
	int	 s;
	struct ifnet *ifp;
	uint32_t maxpktlen;

	sc->sc_udev = uaa->device;
	sc->sc_ctrl_ifaceno = uaa->ifaceno;
	ml_init(&sc->sc_tx_ml);

	quirk = umb_lookup(uaa->vendor, uaa->product);
	if (quirk != NULL && quirk->umb_flags) {
		DPRINTF("%s: setting flags 0x%x from quirk\n", DEVNAM(sc),
                    quirk->umb_flags);
		sc->sc_flags |= quirk->umb_flags;
	}

	/*
	 * Normally, MBIM devices are detected by their interface class and
	 * subclass. But for some models that have multiple configurations, it
	 * is better to match by vendor and product id so that we can select
	 * the desired configuration ourselves, e.g. to override a class-based
	 * match to another driver.
	 */
	if (uaa->configno < 0) {
		if (quirk == NULL) {
			printf("%s: unknown configuration for vid/pid match\n",
			    DEVNAM(sc));
			goto fail;
		}
		uaa->configno = quirk->umb_confno;
		DPRINTF("%s: switching to config #%d\n", DEVNAM(sc),
		    uaa->configno);
		status = usbd_set_config_no(sc->sc_udev, uaa->configno, 1);
		if (status) {
			printf("%s: failed to switch to config #%d: %s\n",
			    DEVNAM(sc), uaa->configno, usbd_errstr(status));
			goto fail;
		}
		usbd_delay_ms(sc->sc_udev, 200);

		/*
		 * Need to do some manual setup that usbd_probe_and_attach()
		 * would do for us otherwise.
		 */
		uaa->nifaces = uaa->device->cdesc->bNumInterfaces;
		for (i = 0; i < uaa->nifaces; i++) {
			if (usbd_iface_claimed(sc->sc_udev, i))
				continue;
			id = usbd_get_interface_descriptor(&uaa->device->ifaces[i]);
			if (id != NULL && id->bInterfaceClass == UICLASS_CDC &&
			    id->bInterfaceSubClass ==
			    UISUBCLASS_MOBILE_BROADBAND_INTERFACE_MODEL) {
				uaa->iface = &uaa->device->ifaces[i];
				uaa->ifaceno = uaa->iface->idesc->bInterfaceNumber;
				sc->sc_ctrl_ifaceno = uaa->ifaceno;
				break;
			}
		}
	}

	/*
	 * Some MBIM hardware does not provide the mandatory CDC Union
	 * Descriptor, so we also look at matching Interface
	 * Association Descriptors to find out the MBIM Data Interface
	 * number.
	 */
	sc->sc_ver_maj = sc->sc_ver_min = -1;
	sc->sc_maxpktlen = MBIM_MAXSEGSZ_MINVAL;
	usbd_desc_iter_init(sc->sc_udev, &iter);
	while ((desc = usbd_desc_iter_next(&iter))) {
		if (desc->bDescriptorType == UDESC_IFACE_ASSOC) {
			ad = (usb_interface_assoc_descriptor_t *)desc;
			if (ad->bFirstInterface == uaa->ifaceno &&
			    ad->bInterfaceCount > 1)
				data_ifaceno = uaa->ifaceno + 1;
			continue;
		}
		if (desc->bDescriptorType == UDESC_INTERFACE) {
			id = (usb_interface_descriptor_t *)desc;
			current_ifaceno = id->bInterfaceNumber;
			continue;
		}
		if (current_ifaceno != uaa->ifaceno)
			continue;
		if (desc->bDescriptorType != UDESC_CS_INTERFACE)
			continue;
		switch (desc->bDescriptorSubtype) {
		case UDESCSUB_CDC_UNION:
			ud = (struct usb_cdc_union_descriptor *)desc;
			data_ifaceno = ud->bSlaveInterface[0];
			break;
		case UDESCSUB_MBIM:
			md = (struct mbim_descriptor *)desc;
			v = UGETW(md->bcdMBIMVersion);
			sc->sc_ver_maj = MBIM_VER_MAJOR(v);
			sc->sc_ver_min = MBIM_VER_MINOR(v);
			sc->sc_ctrl_len = UGETW(md->wMaxControlMessage);
			/* Never trust a USB device! Could try to exploit us */
			if (sc->sc_ctrl_len < MBIM_CTRLMSG_MINLEN ||
			    sc->sc_ctrl_len > MBIM_CTRLMSG_MAXLEN) {
				DPRINTF("%s: control message len %d out of "
				    "bounds [%d .. %d]\n", DEVNAM(sc),
				    sc->sc_ctrl_len, MBIM_CTRLMSG_MINLEN,
				    MBIM_CTRLMSG_MAXLEN);
				/* cont. anyway */
			}
			maxpktlen = UGETW(md->wMaxSegmentSize);
			if (maxpktlen > 0) {
				sc->sc_maxpktlen = maxpktlen;
				DPRINTFN(2, "%s: ctrl_len=%d, maxpktlen=%d, "
				    "cap=0x%x\n", DEVNAM(sc), sc->sc_ctrl_len,
				    sc->sc_maxpktlen,
				    md->bmNetworkCapabilities);
			} else {
				DPRINTFN(2, "%s: max segment size %d out of "
				    "range\n", DEVNAM(sc), maxpktlen);
			}
			break;
		default:
			break;
		}
	}
	if (sc->sc_ver_maj < 0) {
		printf("%s: missing MBIM descriptor\n", DEVNAM(sc));
		goto fail;
	}
	if (sc->sc_flags & UMBFLG_FCC_AUTH_REQUIRED)
		sc->sc_cid = -1;

	for (i = 0; i < uaa->nifaces; i++) {
		if (usbd_iface_claimed(sc->sc_udev, i))
			continue;
		id = usbd_get_interface_descriptor(&sc->sc_udev->ifaces[i]);
		if (id != NULL && id->bInterfaceNumber == data_ifaceno) {
			sc->sc_data_iface = &sc->sc_udev->ifaces[i];
			usbd_claim_iface(sc->sc_udev, i);
		}
	}
	if (sc->sc_data_iface == NULL) {
		printf("%s: no data interface found\n", DEVNAM(sc));
		goto fail;
	}

	/*
	 * If this is a combined NCM/MBIM function, switch to
	 * alternate setting one to enable MBIM.
	 */
	id = usbd_get_interface_descriptor(uaa->iface);
	if (id->bInterfaceClass == UICLASS_CDC &&
	    id->bInterfaceSubClass ==
	    UISUBCLASS_NETWORK_CONTROL_MODEL)
		usbd_set_interface(uaa->iface, 1);

	id = usbd_get_interface_descriptor(uaa->iface);
	ctrl_ep = -1;
	for (i = 0; i < id->bNumEndpoints && ctrl_ep == -1; i++) {
		ed = usbd_interface2endpoint_descriptor(uaa->iface, i);
		if (ed == NULL)
			break;
		if (UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT &&
		    UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN)
			ctrl_ep = ed->bEndpointAddress;
	}
	if (ctrl_ep == -1) {
		printf("%s: missing interrupt endpoint\n", DEVNAM(sc));
		goto fail;
	}

	/*
	 * For the MBIM Data Interface, select the appropriate
	 * alternate setting by looking for a matching descriptor that
	 * has two endpoints.
	 */
	cd = usbd_get_config_descriptor(sc->sc_udev);
	altnum = usbd_get_no_alts(cd, data_ifaceno);
	for (i = 0; i < altnum; i++) {
		id = usbd_find_idesc(cd, sc->sc_data_iface->index, i);
		if (id == NULL)
			continue;
		if (id->bInterfaceClass == UICLASS_CDC_DATA &&
		    id->bInterfaceSubClass == UISUBCLASS_DATA &&
		    id->bInterfaceProtocol == UIPROTO_DATA_MBIM &&
		    id->bNumEndpoints == 2)
			break;
	}
	if (i == altnum || id == NULL) {
		printf("%s: missing alt setting for interface #%d\n",
		    DEVNAM(sc), data_ifaceno);
		goto fail;
	}
	status = usbd_set_interface(sc->sc_data_iface, i);
	if (status) {
		printf("%s: select alt setting %d for interface #%d "
		    "failed: %s\n", DEVNAM(sc), i, data_ifaceno,
		    usbd_errstr(status));
		goto fail;
	}

	id = usbd_get_interface_descriptor(sc->sc_data_iface);
	sc->sc_rx_ep = sc->sc_tx_ep = -1;
	for (i = 0; i < id->bNumEndpoints; i++) {
		if ((ed = usbd_interface2endpoint_descriptor(sc->sc_data_iface,
		    i)) == NULL)
			break;
		if (UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK &&
		    UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN)
			sc->sc_rx_ep = ed->bEndpointAddress;
		else if (UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK &&
		    UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT)
			sc->sc_tx_ep = ed->bEndpointAddress;
	}
	if (sc->sc_rx_ep == -1 || sc->sc_tx_ep == -1) {
		printf("%s: missing bulk endpoints\n", DEVNAM(sc));
		goto fail;
	}

	DPRINTFN(2, "%s: ctrl-ifno#%d: ep-ctrl=%d, data-ifno#%d: ep-rx=%d, "
	    "ep-tx=%d\n", DEVNAM(sc), sc->sc_ctrl_ifaceno,
	    UE_GET_ADDR(ctrl_ep), data_ifaceno,
	    UE_GET_ADDR(sc->sc_rx_ep), UE_GET_ADDR(sc->sc_tx_ep));

	usb_init_task(&sc->sc_umb_task, umb_state_task, sc,
	    USB_TASK_TYPE_GENERIC);
	usb_init_task(&sc->sc_get_response_task, umb_get_response_task, sc,
	    USB_TASK_TYPE_GENERIC);
	timeout_set(&sc->sc_statechg_timer, umb_statechg_timeout, sc);

	if (usbd_open_pipe_intr(uaa->iface, ctrl_ep, USBD_SHORT_XFER_OK,
	    &sc->sc_ctrl_pipe, sc, &sc->sc_intr_msg, sizeof (sc->sc_intr_msg),
	    umb_intr, USBD_DEFAULT_INTERVAL)) {
		printf("%s: failed to open control pipe\n", DEVNAM(sc));
		goto fail;
	}
	sc->sc_resp_buf = malloc(sc->sc_ctrl_len, M_USBDEV, M_NOWAIT);
	if (sc->sc_resp_buf == NULL) {
		printf("%s: allocation of resp buffer failed\n", DEVNAM(sc));
		goto fail;
	}
	sc->sc_ctrl_msg = malloc(sc->sc_ctrl_len, M_USBDEV, M_NOWAIT);
	if (sc->sc_ctrl_msg == NULL) {
		printf("%s: allocation of ctrl msg buffer failed\n",
		    DEVNAM(sc));
		goto fail;
	}

	sc->sc_info.regstate = MBIM_REGSTATE_UNKNOWN;
	sc->sc_info.pin_attempts_left = UMB_VALUE_UNKNOWN;
	sc->sc_info.rssi = UMB_VALUE_UNKNOWN;
	sc->sc_info.ber = UMB_VALUE_UNKNOWN;

	/* Default to 16 bit NTB format. */
	sc->sc_ncm_format = NCM_FORMAT_NTB16;
	umb_ncm_setup(sc);
	umb_ncm_setup_format(sc);
	if (sc->sc_ncm_supported_formats == 0)
		goto fail;
	DPRINTFN(2, "%s: rx/tx size %d/%d\n", DEVNAM(sc),
	    sc->sc_rx_bufsz, sc->sc_tx_bufsz);

	s = splnet();
	ifp = GET_IFP(sc);
	ifp->if_flags = IFF_SIMPLEX | IFF_MULTICAST | IFF_POINTOPOINT;
	ifp->if_ioctl = umb_ioctl;
	ifp->if_start = umb_start;
	ifp->if_rtrequest = umb_rtrequest;

	ifp->if_watchdog = umb_watchdog;
	strlcpy(ifp->if_xname, DEVNAM(sc), IFNAMSIZ);
	ifp->if_link_state = LINK_STATE_DOWN;

	ifp->if_type = IFT_MBIM;
	ifp->if_priority = IF_WWAN_DEFAULT_PRIORITY;
	ifp->if_addrlen = 0;
	ifp->if_hdrlen = sizeof (struct ncm_header16) +
	    sizeof (struct ncm_pointer16);
	ifp->if_mtu = 1500;		/* use a common default */
	ifp->if_hardmtu = sc->sc_maxpktlen;
	ifp->if_bpf_mtap = p2p_bpf_mtap;
	ifp->if_input = p2p_input;
	ifp->if_output = umb_output;
	if_attach(ifp);
	if_alloc_sadl(ifp);
	ifp->if_softc = sc;
#if NBPFILTER > 0
	bpfattach(&ifp->if_bpf, ifp, DLT_LOOP, sizeof(uint32_t));
#endif

#if NKSTAT > 0
	umb_kstat_attach(sc);
#endif

	/*
	 * Open the device now so that we are able to query device information.
	 * XXX maybe close when done?
	 */
	umb_open(sc);
	splx(s);

	DPRINTF("%s: vers %d.%d\n", DEVNAM(sc), sc->sc_ver_maj, sc->sc_ver_min);
	return;

fail:
	usbd_deactivate(sc->sc_udev);
	return;
}

int
umb_detach(struct device *self, int flags)
{
	struct umb_softc *sc = (struct umb_softc *)self;
	struct ifnet *ifp = GET_IFP(sc);
	int	 s;

	s = splnet();
	if (ifp->if_flags & IFF_RUNNING)
		umb_down(sc, 1);
	umb_close(sc);

#if NKSTAT > 0
	umb_kstat_detach(sc);
#endif

	usb_rem_wait_task(sc->sc_udev, &sc->sc_get_response_task);
	if (timeout_initialized(&sc->sc_statechg_timer))
		timeout_del(&sc->sc_statechg_timer);
	sc->sc_nresp = 0;
	usb_rem_wait_task(sc->sc_udev, &sc->sc_umb_task);
	if (sc->sc_ctrl_pipe) {
		usbd_close_pipe(sc->sc_ctrl_pipe);
		sc->sc_ctrl_pipe = NULL;
	}
	if (sc->sc_ctrl_msg) {
		free(sc->sc_ctrl_msg, M_USBDEV, sc->sc_ctrl_len);
		sc->sc_ctrl_msg = NULL;
	}
	if (sc->sc_resp_buf) {
		free(sc->sc_resp_buf, M_USBDEV, sc->sc_ctrl_len);
		sc->sc_resp_buf = NULL;
	}
	if (ifp->if_softc != NULL) {
		if_detach(ifp);
	}

	splx(s);
	return 0;
}

void
umb_ncm_setup(struct umb_softc *sc)
{
	usb_device_request_t req;
	struct ncm_ntb_parameters np;

	/* Query NTB transfer sizes */
	req.bmRequestType = UT_READ_CLASS_INTERFACE;
	req.bRequest = NCM_GET_NTB_PARAMETERS;
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->sc_ctrl_ifaceno);
	USETW(req.wLength, sizeof (np));
	if (usbd_do_request(sc->sc_udev, &req, &np) == USBD_NORMAL_COMPLETION &&
	    UGETW(np.wLength) == sizeof (np)) {
		sc->sc_rx_bufsz = MIN(UGETDW(np.dwNtbInMaxSize), UINT16_MAX);
		sc->sc_tx_bufsz = MIN(UGETDW(np.dwNtbOutMaxSize), UINT16_MAX);
		sc->sc_maxdgram = UGETW(np.wNtbOutMaxDatagrams);
		sc->sc_align = UGETW(np.wNdpOutAlignment);
		sc->sc_ndp_div = UGETW(np.wNdpOutDivisor);
		sc->sc_ndp_remainder = UGETW(np.wNdpOutPayloadRemainder);
		/* Validate values */
		if (!powerof2(sc->sc_align) || sc->sc_align == 0 ||
		    sc->sc_align >= sc->sc_tx_bufsz)
			sc->sc_align = sizeof (uint32_t);
		if (!powerof2(sc->sc_ndp_div) || sc->sc_ndp_div == 0 ||
		    sc->sc_ndp_div >= sc->sc_tx_bufsz)
			sc->sc_ndp_div = sizeof (uint32_t);
		if (sc->sc_ndp_remainder >= sc->sc_ndp_div)
			sc->sc_ndp_remainder = 0;
		DPRINTF("%s: NCM align=%d div=%d rem=%d\n", DEVNAM(sc),
		    sc->sc_align, sc->sc_ndp_div, sc->sc_ndp_remainder);
		sc->sc_ncm_supported_formats = UGETW(np.bmNtbFormatsSupported);
	} else {
		sc->sc_rx_bufsz = sc->sc_tx_bufsz = 8 * 1024;
		sc->sc_maxdgram = 0;
		sc->sc_align = sc->sc_ndp_div = sizeof (uint32_t);
		sc->sc_ndp_remainder = 0;
		DPRINTF("%s: align=default div=default rem=default\n",
		    DEVNAM(sc));
		sc->sc_ncm_supported_formats = NCM_FORMAT_NTB16_MASK;
	}
}

void
umb_ncm_setup_format(struct umb_softc *sc)
{
	usb_device_request_t req;
	uWord wFmt;
	uint16_t fmt;

	assertwaitok();
	if (sc->sc_ncm_supported_formats == 0)
		goto fail;

	/* NCM_GET_NTB_FORMAT is not allowed for 16-bit only devices. */
	if (sc->sc_ncm_supported_formats == NCM_FORMAT_NTB16_MASK) {
		DPRINTF("%s: Only NTB16 format supported.\n", DEVNAM(sc));
		sc->sc_ncm_format = NCM_FORMAT_NTB16;
		return;
	}

	/* Query NTB FORMAT (16 vs. 32 bit) */
	req.bmRequestType = UT_READ_CLASS_INTERFACE;
	req.bRequest = NCM_GET_NTB_FORMAT;
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->sc_ctrl_ifaceno);
	USETW(req.wLength, sizeof (wFmt));
	if (usbd_do_request(sc->sc_udev, &req, wFmt) != USBD_NORMAL_COMPLETION)
		goto fail;
	fmt = UGETW(wFmt);
	if ((sc->sc_ncm_supported_formats & (1UL << fmt)) == 0)
		goto fail;
	if (fmt != NCM_FORMAT_NTB16 && fmt != NCM_FORMAT_NTB32)
		goto fail;
	sc->sc_ncm_format = fmt;

	DPRINTF("%s: Using NCM format %d, supported=0x%x\n",
	    DEVNAM(sc), sc->sc_ncm_format, sc->sc_ncm_supported_formats);
	return;

fail:
	DPRINTF("%s: Cannot setup NCM format\n", DEVNAM(sc));
	sc->sc_ncm_supported_formats = 0;
}

int
umb_alloc_xfers(struct umb_softc *sc)
{
	if (!sc->sc_rx_xfer) {
		if ((sc->sc_rx_xfer = usbd_alloc_xfer(sc->sc_udev)) != NULL)
			sc->sc_rx_buf = usbd_alloc_buffer(sc->sc_rx_xfer,
			    sc->sc_rx_bufsz);
	}
	if (!sc->sc_tx_xfer) {
		if ((sc->sc_tx_xfer = usbd_alloc_xfer(sc->sc_udev)) != NULL)
			sc->sc_tx_buf = usbd_alloc_buffer(sc->sc_tx_xfer,
			    sc->sc_tx_bufsz);
	}
	return (sc->sc_rx_buf && sc->sc_tx_buf) ? 1 : 0;
}

void
umb_free_xfers(struct umb_softc *sc)
{
	if (sc->sc_rx_xfer) {
		/* implicit usbd_free_buffer() */
		usbd_free_xfer(sc->sc_rx_xfer);
		sc->sc_rx_xfer = NULL;
		sc->sc_rx_buf = NULL;
	}
	if (sc->sc_tx_xfer) {
		usbd_free_xfer(sc->sc_tx_xfer);
		sc->sc_tx_xfer = NULL;
		sc->sc_tx_buf = NULL;
	}
	ml_purge(&sc->sc_tx_ml);
}

int
umb_alloc_bulkpipes(struct umb_softc *sc)
{
	struct ifnet *ifp = GET_IFP(sc);

	if (!(ifp->if_flags & IFF_RUNNING)) {
		if (usbd_open_pipe(sc->sc_data_iface, sc->sc_rx_ep,
		    USBD_EXCLUSIVE_USE, &sc->sc_rx_pipe))
			return 0;
		if (usbd_open_pipe(sc->sc_data_iface, sc->sc_tx_ep,
		    USBD_EXCLUSIVE_USE, &sc->sc_tx_pipe))
			return 0;

		ifp->if_flags |= IFF_RUNNING;
		ifq_clr_oactive(&ifp->if_snd);
		umb_rx(sc);
	}
	return 1;
}

void
umb_close_bulkpipes(struct umb_softc *sc)
{
	struct ifnet *ifp = GET_IFP(sc);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;
	if (sc->sc_rx_pipe) {
		usbd_close_pipe(sc->sc_rx_pipe);
		sc->sc_rx_pipe = NULL;
	}
	if (sc->sc_tx_pipe) {
		usbd_close_pipe(sc->sc_tx_pipe);
		sc->sc_tx_pipe = NULL;
	}
}

int
umb_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct proc *p = curproc;
	struct umb_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int	 s, error = 0;
	struct umb_parameter mp;

	if (usbd_is_dying(sc->sc_udev))
		return ENXIO;

	s = splnet();
	switch (cmd) {
	case SIOCSIFFLAGS:
		usb_add_task(sc->sc_udev, &sc->sc_umb_task);
		break;
	case SIOCGUMBINFO:
		error = copyout(&sc->sc_info, ifr->ifr_data,
		    sizeof (sc->sc_info));
		break;
	case SIOCSUMBPARAM:
		if ((error = suser(p)) != 0)
			break;
		if ((error = copyin(ifr->ifr_data, &mp, sizeof (mp))) != 0)
			break;

		if ((error = umb_setpin(sc, mp.op, mp.is_puk, mp.pin, mp.pinlen,
		    mp.newpin, mp.newpinlen)) != 0)
			break;

		if (mp.apnlen < 0 || mp.apnlen > sizeof (sc->sc_info.apn)) {
			error = EINVAL;
			break;
		}
		sc->sc_roaming = mp.roaming ? 1 : 0;
		memset(sc->sc_info.apn, 0, sizeof (sc->sc_info.apn));
		memcpy(sc->sc_info.apn, mp.apn, mp.apnlen);
		sc->sc_info.apnlen = mp.apnlen;
		sc->sc_info.preferredclasses = mp.preferredclasses;
		umb_setdataclass(sc);
		break;
	case SIOCGUMBPARAM:
		memset(&mp, 0, sizeof (mp));
		memcpy(mp.apn, sc->sc_info.apn, sc->sc_info.apnlen);
		mp.apnlen = sc->sc_info.apnlen;
		mp.roaming = sc->sc_roaming;
		mp.preferredclasses = sc->sc_info.preferredclasses;
		error = copyout(&mp, ifr->ifr_data, sizeof (mp));
		break;
	case SIOCSIFMTU:
		/* Does this include the NCM headers and tail? */
		if (ifr->ifr_mtu > ifp->if_hardmtu) {
			error = EINVAL;
			break;
		}
		ifp->if_mtu = ifr->ifr_mtu;
		break;
	case SIOCSIFADDR:
	case SIOCAIFADDR:
	case SIOCSIFDSTADDR:
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;
	default:
		error = ENOTTY;
		break;
	}
	splx(s);
	return error;
}

int
umb_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct rtentry *rtp)
{
	if ((ifp->if_flags & (IFF_UP|IFF_RUNNING)) != (IFF_UP|IFF_RUNNING)) {
		m_freem(m);
		return ENETDOWN;
	}
	m->m_pkthdr.ph_family = dst->sa_family;
	return if_enqueue(ifp, m);
}

static inline int
umb_align(size_t bufsz, int offs, int alignment, int remainder)
{
	size_t	 m = alignment - 1;
	int	 align;

	align = (((size_t)offs + m) & ~m) - alignment + remainder;
	if (align < offs)
		align += alignment;
	if (align > bufsz)
		align = bufsz;
	return align - offs;
}

static inline int
umb_padding(void *buf, size_t bufsz, int offs, int alignment, int remainder)
{
	int	 nb;

	nb = umb_align(bufsz, offs, alignment, remainder);
	if (nb > 0)
		memset(buf + offs, 0, nb);
	return nb;
}

void
umb_start(struct ifnet *ifp)
{
	struct umb_softc *sc = ifp->if_softc;
	struct mbuf *m = NULL;
	int	 ndgram = 0;
	int	 offs, len, mlen;
	int	 maxoverhead;

	if (usbd_is_dying(sc->sc_udev) ||
	    !(ifp->if_flags & IFF_RUNNING) ||
	    ifq_is_oactive(&ifp->if_snd))
		return;

	KASSERT(ml_empty(&sc->sc_tx_ml));

	switch (sc->sc_ncm_format) {
	case NCM_FORMAT_NTB16:
		offs = sizeof (struct ncm_header16);
		offs += umb_align(sc->sc_tx_bufsz, offs, sc->sc_align, 0);
		offs += sizeof (struct ncm_pointer16);
		maxoverhead = sizeof (struct ncm_pointer16_dgram);
		break;
	case NCM_FORMAT_NTB32:
		offs = sizeof (struct ncm_header32);
		offs += umb_align(sc->sc_tx_bufsz, offs, sc->sc_align, 0);
		offs += sizeof (struct ncm_pointer32);
		maxoverhead = sizeof (struct ncm_pointer32_dgram);
		break;
	}

	/*
	 * Overhead for per packet alignment plus packet pointer. Note
	 * that 'struct ncm_pointer{16,32}' already includes space for
	 * the terminating zero pointer.
	 */
	maxoverhead += sc->sc_ndp_div - 1;

	len = 0;
	while (1) {
		m = ifq_deq_begin(&ifp->if_snd);
		if (m == NULL)
			break;

		/*
		 * Check if mbuf plus required NCM pointer still fits into
		 * xfer buffers. Assume maximal padding.
		 */
		mlen = maxoverhead +  m->m_pkthdr.len;
		if ((sc->sc_maxdgram != 0 && ndgram >= sc->sc_maxdgram) ||
		    (offs + len + mlen > sc->sc_tx_bufsz)) {
			ifq_deq_rollback(&ifp->if_snd, m);
			break;
		}
		ifq_deq_commit(&ifp->if_snd, m);

		ndgram++;
		len += mlen;
		ml_enqueue(&sc->sc_tx_ml, m);

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap_af(ifp->if_bpf, m->m_pkthdr.ph_family, m,
			    BPF_DIRECTION_OUT);
#endif
	}
	if (ml_empty(&sc->sc_tx_ml))
		return;
	if (umb_encap(sc, ndgram)) {
		ifq_set_oactive(&ifp->if_snd);
		ifp->if_timer = (2 * umb_xfer_tout) / 1000;
	}
}

void
umb_rtrequest(struct ifnet *ifp, int req, struct rtentry *rt)
{
	struct umb_softc *sc = ifp->if_softc;

	if (req == RTM_PROPOSAL) {
		KERNEL_LOCK();
		umb_send_inet_proposal(sc, AF_INET);
#ifdef INET6
		umb_send_inet_proposal(sc, AF_INET6);
#endif
		KERNEL_UNLOCK();
		return;
	}

	p2p_rtrequest(ifp, req, rt);
}


void
umb_watchdog(struct ifnet *ifp)
{
	struct umb_softc *sc = ifp->if_softc;

	if (usbd_is_dying(sc->sc_udev))
		return;

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", DEVNAM(sc));
	usbd_abort_pipe(sc->sc_tx_pipe);
	return;
}

void
umb_statechg_timeout(void *arg)
{
	struct umb_softc *sc = arg;
	struct ifnet *ifp = GET_IFP(sc);

	if (sc->sc_info.regstate != MBIM_REGSTATE_ROAMING || sc->sc_roaming)
		if (ifp->if_flags & IFF_DEBUG)
			log(LOG_DEBUG, "%s: state change timeout\n",
			    DEVNAM(sc));
	usb_add_task(sc->sc_udev, &sc->sc_umb_task);
}

void
umb_newstate(struct umb_softc *sc, enum umb_state newstate, int flags)
{
	struct ifnet *ifp = GET_IFP(sc);

	if (newstate == sc->sc_state)
		return;
	if (((flags & UMB_NS_DONT_DROP) && newstate < sc->sc_state) ||
	    ((flags & UMB_NS_DONT_RAISE) && newstate > sc->sc_state))
		return;
	if (ifp->if_flags & IFF_DEBUG)
		log(LOG_DEBUG, "%s: state going %s from '%s' to '%s'\n",
		    DEVNAM(sc), newstate > sc->sc_state ? "up" : "down",
		    umb_istate(sc->sc_state), umb_istate(newstate));
	sc->sc_state = newstate;
	usb_add_task(sc->sc_udev, &sc->sc_umb_task);
}

void
umb_state_task(void *arg)
{
	struct umb_softc *sc = arg;
	struct ifnet *ifp = GET_IFP(sc);
	int	 s;
	int	 state;

	if (sc->sc_info.regstate == MBIM_REGSTATE_ROAMING && !sc->sc_roaming) {
		/*
		 * Query the registration state until we're with the home
		 * network again.
		 */
		umb_cmd(sc, MBIM_CID_REGISTER_STATE, MBIM_CMDOP_QRY, NULL, 0);
		return;
	}

	s = splnet();
	if (ifp->if_flags & IFF_UP)
		umb_up(sc);
	else
		umb_down(sc, 0);

	state = sc->sc_state == UMB_S_UP ? LINK_STATE_UP : LINK_STATE_DOWN;
	if (ifp->if_link_state != state) {
		if (ifp->if_flags & IFF_DEBUG)
			log(LOG_DEBUG, "%s: link state changed from %s to %s\n",
			    DEVNAM(sc),
			    LINK_STATE_IS_UP(ifp->if_link_state)
			    ? "up" : "down",
			    LINK_STATE_IS_UP(state) ? "up" : "down");
		ifp->if_link_state = state;
		if_link_state_change(ifp);
	}
	splx(s);
}

void
umb_up(struct umb_softc *sc)
{
	splassert(IPL_NET);

	switch (sc->sc_state) {
	case UMB_S_DOWN:
		DPRINTF("%s: init: opening ...\n", DEVNAM(sc));
		umb_open(sc);
		break;
	case UMB_S_OPEN:
		if (sc->sc_flags & UMBFLG_FCC_AUTH_REQUIRED) {
			if (sc->sc_cid == -1) {
				DPRINTF("%s: init: allocating CID ...\n",
				    DEVNAM(sc));
				umb_allocate_cid(sc);
				break;
			} else
				umb_newstate(sc, UMB_S_CID, UMB_NS_DONT_DROP);
		} else {
			DPRINTF("%s: init: turning radio on ...\n", DEVNAM(sc));
			umb_radio(sc, 1);
			break;
		}
		/*FALLTHROUGH*/
	case UMB_S_CID:
		DPRINTF("%s: init: sending FCC auth ...\n", DEVNAM(sc));
		umb_send_fcc_auth(sc);
		break;
	case UMB_S_RADIO:
		DPRINTF("%s: init: checking SIM state ...\n", DEVNAM(sc));
		umb_cmd(sc, MBIM_CID_SUBSCRIBER_READY_STATUS, MBIM_CMDOP_QRY,
		    NULL, 0);
		break;
	case UMB_S_SIMREADY:
		DPRINTF("%s: init: attaching ...\n", DEVNAM(sc));
		umb_packet_service(sc, 1);
		break;
	case UMB_S_ATTACHED:
		sc->sc_tx_seq = 0;
		if (!umb_alloc_xfers(sc)) {
			umb_free_xfers(sc);
			printf("%s: allocation of xfers failed\n", DEVNAM(sc));
			break;
		}
		DPRINTF("%s: init: connecting ...\n", DEVNAM(sc));
		umb_connect(sc);
		break;
	case UMB_S_CONNECTED:
		DPRINTF("%s: init: getting IP config ...\n", DEVNAM(sc));
		umb_qry_ipconfig(sc);
		break;
	case UMB_S_UP:
		DPRINTF("%s: init: reached state UP\n", DEVNAM(sc));
		if (!umb_alloc_bulkpipes(sc)) {
			printf("%s: opening bulk pipes failed\n", DEVNAM(sc));
			umb_down(sc, 1);
		}
		break;
	}
	if (sc->sc_state < UMB_S_UP)
		timeout_add_sec(&sc->sc_statechg_timer,
		    UMB_STATE_CHANGE_TIMEOUT);
	else
		timeout_del(&sc->sc_statechg_timer);
	return;
}

void
umb_down(struct umb_softc *sc, int force)
{
	splassert(IPL_NET);

	umb_close_bulkpipes(sc);
	if (sc->sc_state < UMB_S_CONNECTED)
		umb_free_xfers(sc);

	switch (sc->sc_state) {
	case UMB_S_UP:
		umb_clear_addr(sc);
		/*FALLTHROUGH*/
	case UMB_S_CONNECTED:
		DPRINTF("%s: stop: disconnecting ...\n", DEVNAM(sc));
		umb_disconnect(sc);
		if (!force)
			break;
		/*FALLTHROUGH*/
	case UMB_S_ATTACHED:
		DPRINTF("%s: stop: detaching ...\n", DEVNAM(sc));
		umb_packet_service(sc, 0);
		if (!force)
			break;
		/*FALLTHROUGH*/
	case UMB_S_SIMREADY:
	case UMB_S_RADIO:
		DPRINTF("%s: stop: turning radio off ...\n", DEVNAM(sc));
		umb_radio(sc, 0);
		if (!force)
			break;
		/*FALLTHROUGH*/
	case UMB_S_CID:
	case UMB_S_OPEN:
	case UMB_S_DOWN:
		/* Do not close the device */
		DPRINTF("%s: stop: reached state DOWN\n", DEVNAM(sc));
		break;
	}
	if (force)
		sc->sc_state = UMB_S_OPEN;

	if (sc->sc_state > UMB_S_OPEN)
		timeout_add_sec(&sc->sc_statechg_timer,
		    UMB_STATE_CHANGE_TIMEOUT);
	else
		timeout_del(&sc->sc_statechg_timer);
}

void
umb_get_response_task(void *arg)
{
	struct umb_softc *sc = arg;
	int	 len;
	int	 s;

	/*
	 * Function is required to send on RESPONSE_AVAILABLE notification for
	 * each encapsulated response that is to be processed by the host.
	 * But of course, we can receive multiple notifications before the
	 * response task is run.
	 */
	s = splusb();
	while (sc->sc_nresp > 0) {
		--sc->sc_nresp;
		len = sc->sc_ctrl_len;
		if (umb_get_encap_response(sc, sc->sc_resp_buf, &len))
			umb_decode_response(sc, sc->sc_resp_buf, len);
	}
	splx(s);
}

void
umb_decode_response(struct umb_softc *sc, void *response, int len)
{
	struct mbim_msghdr *hdr = response;
	struct mbim_fragmented_msg_hdr *fraghdr;
	uint32_t type;
	uint32_t tid;

	DPRINTFN(3, "%s: got response: len %d\n", DEVNAM(sc), len);
	DDUMPN(4, response, len);

	if (len < sizeof (*hdr) || letoh32(hdr->len) != len) {
		/*
		 * We should probably cancel a transaction, but since the
		 * message is too short, we cannot decode the transaction
		 * id (tid) and hence don't know, whom to cancel. Must wait
		 * for the timeout.
		 */
		DPRINTF("%s: received short response (len %d)\n",
		    DEVNAM(sc), len);
		return;
	}

	/*
	 * XXX FIXME: if message is fragmented, store it until last frag
	 *	is received and then re-assemble all fragments.
	 */
	type = letoh32(hdr->type);
	tid = letoh32(hdr->tid);
	switch (type) {
	case MBIM_INDICATE_STATUS_MSG:
	case MBIM_COMMAND_DONE:
		fraghdr = response;
		if (letoh32(fraghdr->frag.nfrag) != 1) {
			DPRINTF("%s: discarding fragmented messages\n",
			    DEVNAM(sc));
			return;
		}
		break;
	default:
		break;
	}

	DPRINTF("%s: <- rcv %s (tid %u)\n", DEVNAM(sc), umb_request2str(type),
	    tid);
	switch (type) {
	case MBIM_FUNCTION_ERROR_MSG:
	case MBIM_HOST_ERROR_MSG:
	{
		struct mbim_f2h_hosterr *e;
		int	 err;

		if (len >= sizeof (*e)) {
			e = response;
			err = letoh32(e->err);

			DPRINTF("%s: %s message, error %s (tid %u)\n",
			    DEVNAM(sc), umb_request2str(type),
			    umb_error2str(err), tid);
			if (err == MBIM_ERROR_NOT_OPENED)
				umb_newstate(sc, UMB_S_DOWN, 0);
		}
		break;
	}
	case MBIM_INDICATE_STATUS_MSG:
		umb_handle_indicate_status_msg(sc, response, len);
		break;
	case MBIM_OPEN_DONE:
		umb_handle_opendone_msg(sc, response, len);
		break;
	case MBIM_CLOSE_DONE:
		umb_handle_closedone_msg(sc, response, len);
		break;
	case MBIM_COMMAND_DONE:
		umb_command_done(sc, response, len);
		break;
	default:
		DPRINTF("%s: discard message %s\n", DEVNAM(sc),
		    umb_request2str(type));
		break;
	}
}

void
umb_handle_indicate_status_msg(struct umb_softc *sc, void *data, int len)
{
	struct mbim_f2h_indicate_status *m = data;
	uint32_t infolen;
	uint32_t cid;

	if (len < sizeof (*m)) {
		DPRINTF("%s: discard short %s message\n", DEVNAM(sc),
		    umb_request2str(letoh32(m->hdr.type)));
		return;
	}
	if (memcmp(m->devid, umb_uuid_basic_connect, sizeof (m->devid))) {
		DPRINTF("%s: discard %s message for other UUID '%s'\n",
		    DEVNAM(sc), umb_request2str(letoh32(m->hdr.type)),
		    umb_uuid2str(m->devid));
		return;
	}
	infolen = letoh32(m->infolen);
	if (len < sizeof (*m) + infolen) {
		DPRINTF("%s: discard truncated %s message (want %d, got %d)\n",
		    DEVNAM(sc), umb_request2str(letoh32(m->hdr.type)),
		    (int)sizeof (*m) + infolen, len);
		return;
	}

	cid = letoh32(m->cid);
	DPRINTF("%s: indicate %s status\n", DEVNAM(sc), umb_cid2str(cid));
	umb_decode_cid(sc, cid, m->info, infolen);
}

void
umb_handle_opendone_msg(struct umb_softc *sc, void *data, int len)
{
	struct mbim_f2h_openclosedone *resp = data;
	struct ifnet *ifp = GET_IFP(sc);
	uint32_t status;

	status = letoh32(resp->status);
	if (status == MBIM_STATUS_SUCCESS) {
		if (sc->sc_maxsessions == 0) {
			umb_cmd(sc, MBIM_CID_DEVICE_CAPS, MBIM_CMDOP_QRY, NULL,
			    0);
			umb_cmd(sc, MBIM_CID_PIN, MBIM_CMDOP_QRY, NULL, 0);
			umb_cmd(sc, MBIM_CID_REGISTER_STATE, MBIM_CMDOP_QRY,
			    NULL, 0);
		}
		umb_newstate(sc, UMB_S_OPEN, UMB_NS_DONT_DROP);
	} else if (ifp->if_flags & IFF_DEBUG)
		log(LOG_ERR, "%s: open error: %s\n", DEVNAM(sc),
		    umb_status2str(status));
	return;
}

void
umb_handle_closedone_msg(struct umb_softc *sc, void *data, int len)
{
	struct mbim_f2h_openclosedone *resp = data;
	uint32_t status;

	status = letoh32(resp->status);
	if (status == MBIM_STATUS_SUCCESS)
		umb_newstate(sc, UMB_S_DOWN, 0);
	else
		DPRINTF("%s: close error: %s\n", DEVNAM(sc),
		    umb_status2str(status));
	return;
}

static inline void
umb_getinfobuf(void *in, int inlen, uint32_t offs, uint32_t sz,
    void *out, size_t outlen)
{
	offs = letoh32(offs);
	sz = letoh32(sz);
	memset(out, 0, outlen);
	if ((uint64_t)inlen >= (uint64_t)offs + (uint64_t)sz)
		memcpy(out, in + offs, MIN(sz, outlen));
}

static inline int
umb_addstr(void *buf, size_t bufsz, int *offs, void *str, int slen,
    uint32_t *offsmember, uint32_t *sizemember)
{
	if (*offs + slen > bufsz)
		return 0;

	*sizemember = htole32((uint32_t)slen);
	if (slen && str) {
		*offsmember = htole32((uint32_t)*offs);
		memcpy(buf + *offs, str, slen);
		*offs += slen;
		*offs += umb_padding(buf, bufsz, *offs, sizeof (uint32_t), 0);
	} else
		*offsmember = htole32(0);
	return 1;
}

int
umb_decode_register_state(struct umb_softc *sc, void *data, int len)
{
	struct mbim_cid_registration_state_info *rs = data;
	struct ifnet *ifp = GET_IFP(sc);

	if (len < sizeof (*rs))
		return 0;
	sc->sc_info.nwerror = letoh32(rs->nwerror);
	sc->sc_info.regstate = letoh32(rs->regstate);
	sc->sc_info.regmode = letoh32(rs->regmode);
	sc->sc_info.cellclass = letoh32(rs->curcellclass);

	umb_getinfobuf(data, len, rs->provname_offs, rs->provname_size,
	    sc->sc_info.provider, sizeof (sc->sc_info.provider));
	umb_getinfobuf(data, len, rs->provid_offs, rs->provid_size,
	    sc->sc_info.providerid, sizeof (sc->sc_info.providerid));
	umb_getinfobuf(data, len, rs->roamingtxt_offs, rs->roamingtxt_size,
	    sc->sc_info.roamingtxt, sizeof (sc->sc_info.roamingtxt));

	DPRINTFN(2, "%s: %s, availclass 0x%x, class 0x%x, regmode %d\n",
	    DEVNAM(sc), umb_regstate(sc->sc_info.regstate),
	    letoh32(rs->availclasses), sc->sc_info.cellclass,
	    sc->sc_info.regmode);

	if (sc->sc_info.regstate == MBIM_REGSTATE_ROAMING &&
	    !sc->sc_roaming &&
	    sc->sc_info.activation == MBIM_ACTIVATION_STATE_ACTIVATED) {
		if (ifp->if_flags & IFF_DEBUG)
			log(LOG_INFO,
			    "%s: disconnecting from roaming network\n",
			    DEVNAM(sc));
		umb_disconnect(sc);
	}
	return 1;
}

int
umb_decode_devices_caps(struct umb_softc *sc, void *data, int len)
{
	struct mbim_cid_device_caps *dc = data;

	if (len < sizeof (*dc))
		return 0;
	sc->sc_maxsessions = letoh32(dc->max_sessions);
	sc->sc_info.supportedclasses = letoh32(dc->dataclass);
	umb_getinfobuf(data, len, dc->devid_offs, dc->devid_size,
	    sc->sc_info.devid, sizeof (sc->sc_info.devid));
	umb_getinfobuf(data, len, dc->fwinfo_offs, dc->fwinfo_size,
	    sc->sc_info.fwinfo, sizeof (sc->sc_info.fwinfo));
	umb_getinfobuf(data, len, dc->hwinfo_offs, dc->hwinfo_size,
	    sc->sc_info.hwinfo, sizeof (sc->sc_info.hwinfo));
	DPRINTFN(2, "%s: max sessions %d, supported classes 0x%x\n",
	    DEVNAM(sc), sc->sc_maxsessions, sc->sc_info.supportedclasses);
	return 1;
}

int
umb_decode_subscriber_status(struct umb_softc *sc, void *data, int len)
{
	struct mbim_cid_subscriber_ready_info *si = data;
	struct ifnet *ifp = GET_IFP(sc);
	int	npn;

	if (len < sizeof (*si))
		return 0;
	sc->sc_info.sim_state = letoh32(si->ready);

	umb_getinfobuf(data, len, si->sid_offs, si->sid_size,
	    sc->sc_info.sid, sizeof (sc->sc_info.sid));
	umb_getinfobuf(data, len, si->icc_offs, si->icc_size,
	    sc->sc_info.iccid, sizeof (sc->sc_info.iccid));

	npn = letoh32(si->no_pn);
	if (npn > 0)
		umb_getinfobuf(data, len, si->pn[0].offs, si->pn[0].size,
		    sc->sc_info.pn, sizeof (sc->sc_info.pn));
	else
		memset(sc->sc_info.pn, 0, sizeof (sc->sc_info.pn));

	if (sc->sc_info.sim_state == MBIM_SIMSTATE_LOCKED)
		sc->sc_info.pin_state = UMB_PUK_REQUIRED;
	if (ifp->if_flags & IFF_DEBUG)
		log(LOG_INFO, "%s: SIM %s\n", DEVNAM(sc),
		    umb_simstate(sc->sc_info.sim_state));
	if (sc->sc_info.sim_state == MBIM_SIMSTATE_INITIALIZED)
		umb_newstate(sc, UMB_S_SIMREADY, UMB_NS_DONT_DROP);
	return 1;
}

int
umb_decode_radio_state(struct umb_softc *sc, void *data, int len)
{
	struct mbim_cid_radio_state_info *rs = data;
	struct ifnet *ifp = GET_IFP(sc);

	if (len < sizeof (*rs))
		return 0;

	sc->sc_info.hw_radio_on =
	    (letoh32(rs->hw_state) == MBIM_RADIO_STATE_ON) ? 1 : 0;
	sc->sc_info.sw_radio_on =
	    (letoh32(rs->sw_state) == MBIM_RADIO_STATE_ON) ? 1 : 0;
	if (!sc->sc_info.hw_radio_on) {
		printf("%s: radio is disabled by hardware switch\n",
		    DEVNAM(sc));
		/*
		 * XXX do we need a time to poll the state of the rfkill switch
		 *	or will the device send an unsolicited notification
		 *	in case the state changes?
		 */
		umb_newstate(sc, UMB_S_OPEN, 0);
	} else if (!sc->sc_info.sw_radio_on) {
		if (ifp->if_flags & IFF_DEBUG)
			log(LOG_INFO, "%s: radio is off\n", DEVNAM(sc));
		umb_newstate(sc, UMB_S_OPEN, 0);
	} else
		umb_newstate(sc, UMB_S_RADIO, UMB_NS_DONT_DROP);
	return 1;
}

int
umb_decode_pin(struct umb_softc *sc, void *data, int len)
{
	struct mbim_cid_pin_info *pi = data;
	struct ifnet *ifp = GET_IFP(sc);
	uint32_t	attempts_left;

	if (len < sizeof (*pi))
		return 0;

	attempts_left = letoh32(pi->remaining_attempts);
	if (attempts_left != 0xffffffff)
		sc->sc_info.pin_attempts_left = attempts_left;

	switch (letoh32(pi->state)) {
	case MBIM_PIN_STATE_UNLOCKED:
		sc->sc_info.pin_state = UMB_PIN_UNLOCKED;
		break;
	case MBIM_PIN_STATE_LOCKED:
		switch (letoh32(pi->type)) {
		case MBIM_PIN_TYPE_PIN1:
			sc->sc_info.pin_state = UMB_PIN_REQUIRED;
			break;
		case MBIM_PIN_TYPE_PUK1:
			sc->sc_info.pin_state = UMB_PUK_REQUIRED;
			break;
		case MBIM_PIN_TYPE_PIN2:
		case MBIM_PIN_TYPE_PUK2:
			/* Assume that PIN1 was accepted */
			sc->sc_info.pin_state = UMB_PIN_UNLOCKED;
			break;
		}
		break;
	}
	if (ifp->if_flags & IFF_DEBUG)
		log(LOG_INFO, "%s: %s state %s (%d attempts left)\n",
		    DEVNAM(sc), umb_pin_type(letoh32(pi->type)),
		    (letoh32(pi->state) == MBIM_PIN_STATE_UNLOCKED) ?
			"unlocked" : "locked",
		    letoh32(pi->remaining_attempts));

	/*
	 * In case the PIN was set after IFF_UP, retrigger the state machine
	 */
	usb_add_task(sc->sc_udev, &sc->sc_umb_task);
	return 1;
}

int
umb_decode_packet_service(struct umb_softc *sc, void *data, int len)
{
	struct mbim_cid_packet_service_info *psi = data;
	int	 state, highestclass;
	uint64_t up_speed, down_speed;
	struct ifnet *ifp = GET_IFP(sc);

	if (len < sizeof (*psi))
		return 0;

	sc->sc_info.nwerror = letoh32(psi->nwerror);
	state = letoh32(psi->state);
	highestclass = letoh32(psi->highest_dataclass);
	up_speed = letoh64(psi->uplink_speed);
	down_speed = letoh64(psi->downlink_speed);
	if (sc->sc_info.packetstate  != state ||
	    sc->sc_info.uplink_speed != up_speed ||
	    sc->sc_info.downlink_speed != down_speed) {
		if (ifp->if_flags & IFF_DEBUG) {
			log(LOG_INFO, "%s: packet service ", DEVNAM(sc));
			if (sc->sc_info.packetstate  != state)
				addlog("changed from %s to ",
				    umb_packet_state(sc->sc_info.packetstate));
			addlog("%s, class %s, speed: %llu up / %llu down\n",
			    umb_packet_state(state), 
			    umb_dataclass(highestclass), up_speed, down_speed);
		}
	}
	sc->sc_info.packetstate = state;
	sc->sc_info.highestclass = highestclass;
	sc->sc_info.uplink_speed = up_speed;
	sc->sc_info.downlink_speed = down_speed;

	if (sc->sc_info.regmode == MBIM_REGMODE_AUTOMATIC) {
		/*
		 * For devices using automatic registration mode, just proceed,
		 * once registration has completed.
		 */
		if (ifp->if_flags & IFF_UP) {
			switch (sc->sc_info.regstate) {
			case MBIM_REGSTATE_HOME:
			case MBIM_REGSTATE_ROAMING:
			case MBIM_REGSTATE_PARTNER:
				umb_newstate(sc, UMB_S_ATTACHED,
				    UMB_NS_DONT_DROP);
				break;
			default:
				break;
			}
		} else
			umb_newstate(sc, UMB_S_SIMREADY, UMB_NS_DONT_RAISE);
	} else switch (sc->sc_info.packetstate) {
	case MBIM_PKTSERVICE_STATE_ATTACHED:
		umb_newstate(sc, UMB_S_ATTACHED, UMB_NS_DONT_DROP);
		break;
	case MBIM_PKTSERVICE_STATE_DETACHED:
		umb_newstate(sc, UMB_S_SIMREADY, UMB_NS_DONT_RAISE);
		break;
	}
	return 1;
}

int
umb_decode_signal_state(struct umb_softc *sc, void *data, int len)
{
	struct mbim_cid_signal_state *ss = data;
	struct ifnet *ifp = GET_IFP(sc);
	int	 rssi;
#if NKSTAT > 0
	struct kstat *ks;
#endif

	if (len < sizeof (*ss))
		return 0;

	if (letoh32(ss->rssi) == 99)
		rssi = UMB_VALUE_UNKNOWN;
	else {
		rssi = -113 + 2 * letoh32(ss->rssi);
		if ((ifp->if_flags & IFF_DEBUG) && sc->sc_info.rssi != rssi &&
		    sc->sc_state >= UMB_S_CONNECTED)
			log(LOG_INFO, "%s: rssi %d dBm\n", DEVNAM(sc), rssi);
	}
	sc->sc_info.rssi = rssi;
	sc->sc_info.ber = letoh32(ss->err_rate);
	if (sc->sc_info.ber == 99)
		sc->sc_info.ber = UMB_VALUE_UNKNOWN;

#if NKSTAT > 0
	ks = sc->sc_kstat_signal;
	if (ks != NULL) {
		struct umb_kstat_signal *uks = ks->ks_data;

		rw_enter_write(&sc->sc_kstat_lock);
		kstat_kv_u64(&uks->reports)++;

		if (sc->sc_info.rssi == UMB_VALUE_UNKNOWN)
			uks->rssi.kv_type = KSTAT_KV_T_NULL;
		else {
			uks->rssi.kv_type = KSTAT_KV_T_INT32;
			kstat_kv_s32(&uks->rssi) = sc->sc_info.rssi;
		}
	
		if (sc->sc_info.ber == UMB_VALUE_UNKNOWN)
			uks->error_rate.kv_type = KSTAT_KV_T_NULL;
		else {
			uks->error_rate.kv_type = KSTAT_KV_T_INT32;
			kstat_kv_s32(&uks->error_rate) = sc->sc_info.ber;
		}

		ks->ks_interval.tv_sec = letoh32(ss->ss_intvl);
		getnanouptime(&ks->ks_updated);
		rw_exit_write(&sc->sc_kstat_lock);
	}
#endif

	return 1;
}

int
umb_decode_connect_info(struct umb_softc *sc, void *data, int len)
{
	struct mbim_cid_connect_info *ci = data;
	struct ifnet *ifp = GET_IFP(sc);
	int	 act;

	if (len < sizeof (*ci))
		return 0;

	if (letoh32(ci->sessionid) != umb_session_id) {
		DPRINTF("%s: discard connection info for session %u\n",
		    DEVNAM(sc), letoh32(ci->sessionid));
		return 1;
	}
	if (memcmp(ci->context, umb_uuid_context_internet,
	    sizeof (ci->context))) {
		DPRINTF("%s: discard connection info for other context\n",
		    DEVNAM(sc));
		return 1;
	}
	act = letoh32(ci->activation);
	if (sc->sc_info.activation != act) {
		if (ifp->if_flags & IFF_DEBUG)
			log(LOG_INFO, "%s: connection %s\n", DEVNAM(sc),
			    umb_activation(act));

		sc->sc_info.activation = act;
		sc->sc_info.nwerror = letoh32(ci->nwerror);

		if (sc->sc_info.activation == MBIM_ACTIVATION_STATE_ACTIVATED)
			umb_newstate(sc, UMB_S_CONNECTED, UMB_NS_DONT_DROP);
		else if (sc->sc_info.activation ==
		    MBIM_ACTIVATION_STATE_DEACTIVATED)
			umb_newstate(sc, UMB_S_ATTACHED, 0);
		/* else: other states are purely transitional */
	}
	return 1;
}

void
umb_clear_addr(struct umb_softc *sc)
{
	struct ifnet *ifp = GET_IFP(sc);

	memset(sc->sc_info.ipv4dns, 0, sizeof (sc->sc_info.ipv4dns));
	memset(sc->sc_info.ipv6dns, 0, sizeof (sc->sc_info.ipv6dns));
	umb_send_inet_proposal(sc, AF_INET);
#ifdef INET6
	umb_send_inet_proposal(sc, AF_INET6);
#endif
	NET_LOCK();
	in_ifdetach(ifp);
#ifdef INET6
	in6_ifdetach(ifp);
#endif
	NET_UNLOCK();
}

int
umb_add_inet_config(struct umb_softc *sc, struct in_addr ip, u_int prefixlen,
    struct in_addr gw)
{
	struct ifnet *ifp = GET_IFP(sc);
	struct in_aliasreq ifra;
	struct sockaddr_in *sin, default_sin;
	struct rt_addrinfo info;
	struct rtentry *rt;
	int	 rv;

	memset(&ifra, 0, sizeof (ifra));
	rv = in_ioctl(SIOCDIFADDR, (caddr_t)&ifra, ifp, 1);
	if (rv != 0 && rv != EADDRNOTAVAIL) {
		printf("%s: unable to delete IPv4 address, error %d\n",
		    DEVNAM(ifp->if_softc), rv);
		return rv;
	}

	memset(&ifra, 0, sizeof (ifra));
	sin = &ifra.ifra_addr;
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof (*sin);
	sin->sin_addr = ip;

	sin = &ifra.ifra_dstaddr;
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof (*sin);
	sin->sin_addr = gw;

	sin = &ifra.ifra_mask;
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof (*sin);
	in_len2mask(&sin->sin_addr,
	    MIN(prefixlen, sizeof (struct in_addr) * 8));

	rv = in_ioctl(SIOCAIFADDR, (caddr_t)&ifra, ifp, 1);
	if (rv != 0) {
		printf("%s: unable to set IPv4 address, error %d\n",
		    DEVNAM(ifp->if_softc), rv);
		return rv;
	}

	memset(&default_sin, 0, sizeof(default_sin));
	default_sin.sin_family = AF_INET;
	default_sin.sin_len = sizeof (default_sin);

	memset(&info, 0, sizeof(info));
	NET_LOCK();
	info.rti_flags = RTF_GATEWAY /* maybe | RTF_STATIC */;
	info.rti_ifa = ifa_ifwithaddr(sintosa(&ifra.ifra_addr),
	    ifp->if_rdomain);
	info.rti_info[RTAX_DST] = sintosa(&default_sin);
	info.rti_info[RTAX_NETMASK] = sintosa(&default_sin);
	info.rti_info[RTAX_GATEWAY] = sintosa(&ifra.ifra_dstaddr);

	rv = rtrequest(RTM_ADD, &info, 0, &rt, ifp->if_rdomain);
	if (rv) {
		printf("%s: unable to set IPv4 default route, "
		    "error %d\n", DEVNAM(ifp->if_softc), rv);
		rtm_miss(RTM_MISS, &info, 0, RTP_NONE, 0, rv,
		    ifp->if_rdomain);
	} else {
		/* Inform listeners of the new route */
		rtm_send(rt, RTM_ADD, rv, ifp->if_rdomain);
		rtfree(rt);
	}
	NET_UNLOCK();

	if (ifp->if_flags & IFF_DEBUG) {
		char str[3][INET_ADDRSTRLEN];
		log(LOG_INFO, "%s: IPv4 addr %s, mask %s, gateway %s\n",
		    DEVNAM(ifp->if_softc),
		    sockaddr_ntop(sintosa(&ifra.ifra_addr), str[0],
		    sizeof(str[0])),
		    sockaddr_ntop(sintosa(&ifra.ifra_mask), str[1],
		    sizeof(str[1])),
		    sockaddr_ntop(sintosa(&ifra.ifra_dstaddr), str[2],
		    sizeof(str[2])));
	}
	return 0;
}

#ifdef INET6
int
umb_add_inet6_config(struct umb_softc *sc, struct in6_addr *ip, u_int prefixlen,
    struct in6_addr *gw)
{
	struct ifnet *ifp = GET_IFP(sc);
	struct in6_aliasreq ifra;
	struct sockaddr_in6 *sin6, default_sin6;
	struct rt_addrinfo info;
	struct rtentry *rt;
	int	 rv;

	memset(&ifra, 0, sizeof (ifra));
	sin6 = &ifra.ifra_addr;
	sin6->sin6_family = AF_INET6;
	sin6->sin6_len = sizeof (*sin6);
	memcpy(&sin6->sin6_addr, ip, sizeof (sin6->sin6_addr));

	sin6 = &ifra.ifra_dstaddr;
	sin6->sin6_family = AF_INET6;
	sin6->sin6_len = sizeof (*sin6);
	memcpy(&sin6->sin6_addr, gw, sizeof (sin6->sin6_addr));

	/* XXX: in6_update_ifa() accepts only 128 bits for P2P interfaces. */
	prefixlen = 128;

	sin6 = &ifra.ifra_prefixmask;
	sin6->sin6_family = AF_INET6;
	sin6->sin6_len = sizeof (*sin6);
	in6_prefixlen2mask(&sin6->sin6_addr, prefixlen);

	ifra.ifra_lifetime.ia6t_vltime = ND6_INFINITE_LIFETIME;
	ifra.ifra_lifetime.ia6t_pltime = ND6_INFINITE_LIFETIME;

	rv = in6_ioctl(SIOCAIFADDR_IN6, (caddr_t)&ifra, ifp, 1);
	if (rv != 0) {
		printf("%s: unable to set IPv6 address, error %d\n",
		    DEVNAM(ifp->if_softc), rv);
		return rv;
	}

	memset(&default_sin6, 0, sizeof(default_sin6));
	default_sin6.sin6_family = AF_INET6;
	default_sin6.sin6_len = sizeof (default_sin6);

	memset(&info, 0, sizeof(info));
	NET_LOCK();
	info.rti_flags = RTF_GATEWAY /* maybe | RTF_STATIC */;
	info.rti_ifa = ifa_ifwithaddr(sin6tosa(&ifra.ifra_addr),
	    ifp->if_rdomain);
	info.rti_info[RTAX_DST] = sin6tosa(&default_sin6);
	info.rti_info[RTAX_NETMASK] = sin6tosa(&default_sin6);
	info.rti_info[RTAX_GATEWAY] = sin6tosa(&ifra.ifra_dstaddr);

	rv = rtrequest(RTM_ADD, &info, 0, &rt, ifp->if_rdomain);
	if (rv) {
		printf("%s: unable to set IPv6 default route, "
		    "error %d\n", DEVNAM(ifp->if_softc), rv);
		rtm_miss(RTM_MISS, &info, 0, RTP_NONE, 0, rv,
		    ifp->if_rdomain);
	} else {
		/* Inform listeners of the new route */
		rtm_send(rt, RTM_ADD, rv, ifp->if_rdomain);
		rtfree(rt);
	}
	NET_UNLOCK();

	if (ifp->if_flags & IFF_DEBUG) {
		char str[3][INET6_ADDRSTRLEN];
		log(LOG_INFO, "%s: IPv6 addr %s, mask %s, gateway %s\n",
		    DEVNAM(ifp->if_softc),
		    sockaddr_ntop(sin6tosa(&ifra.ifra_addr), str[0],
		    sizeof(str[0])),
		    sockaddr_ntop(sin6tosa(&ifra.ifra_prefixmask), str[1],
		    sizeof(str[1])),
		    sockaddr_ntop(sin6tosa(&ifra.ifra_dstaddr), str[2],
		    sizeof(str[2])));
	}
	return 0;
}
#endif

void
umb_send_inet_proposal(struct umb_softc *sc, int af)
{
	struct ifnet *ifp = GET_IFP(sc);
	struct sockaddr_rtdns rtdns;
	struct rt_addrinfo info;
	int i, flag = 0;
	size_t sz = 0;

	memset(&rtdns, 0, sizeof(rtdns));
	memset(&info, 0, sizeof(info));

	for (i = 0; i < UMB_MAX_DNSSRV; i++) {
		if (af == AF_INET) {
			sz = sizeof (sc->sc_info.ipv4dns[i]);
			if (sc->sc_info.ipv4dns[i].s_addr == INADDR_ANY)
				break;
			memcpy(rtdns.sr_dns + i * sz, &sc->sc_info.ipv4dns[i],
			    sz);
			flag = RTF_UP;
#ifdef INET6
		} else if (af == AF_INET6) {
			sz = sizeof (sc->sc_info.ipv6dns[i]);
			if (IN6_ARE_ADDR_EQUAL(&sc->sc_info.ipv6dns[i],
			    &in6addr_any))
				break;
			memcpy(rtdns.sr_dns + i * sz, &sc->sc_info.ipv6dns[i],
			    sz);
			flag = RTF_UP;
#endif
		}
	}
	rtdns.sr_family = af;
	rtdns.sr_len = 2 + i * sz;
	info.rti_info[RTAX_DNS] = srtdnstosa(&rtdns);

	rtm_proposal(ifp, &info, flag, RTP_PROPOSAL_UMB);
}

int
umb_decode_ip_configuration(struct umb_softc *sc, void *data, int len)
{
	struct mbim_cid_ip_configuration_info *ic = data;
	struct ifnet *ifp = GET_IFP(sc);
	int	 s;
	uint32_t avail_v4;
	uint32_t val;
	int	 n, i;
	int	 off;
	struct mbim_cid_ipv4_element ipv4elem;
	struct in_addr addr, gw;
	int	 state = -1;
	int	 rv;
	int	 hasmtu = 0;
#ifdef INET6
	uint32_t avail_v6;
	struct mbim_cid_ipv6_element ipv6elem;
	struct in6_addr addr6, gw6;
#endif

	if (len < sizeof (*ic))
		return 0;
	if (letoh32(ic->sessionid) != umb_session_id) {
		DPRINTF("%s: ignore IP configuration for session id %d\n",
		    DEVNAM(sc), letoh32(ic->sessionid));
		return 0;
	}
	s = splnet();

	memset(sc->sc_info.ipv4dns, 0, sizeof (sc->sc_info.ipv4dns));
	memset(sc->sc_info.ipv6dns, 0, sizeof (sc->sc_info.ipv6dns));

	/*
	 * IPv4 configuration
	 */
	avail_v4 = letoh32(ic->ipv4_available);
	if ((avail_v4 & (MBIM_IPCONF_HAS_ADDRINFO | MBIM_IPCONF_HAS_GWINFO)) ==
	    (MBIM_IPCONF_HAS_ADDRINFO | MBIM_IPCONF_HAS_GWINFO)) {
		n = letoh32(ic->ipv4_naddr);
		off = letoh32(ic->ipv4_addroffs);

		if (n == 0 || off + sizeof (ipv4elem) > len)
			goto tryv6;
		if (n != 1 && ifp->if_flags & IFF_DEBUG)
			log(LOG_INFO, "%s: more than one IPv4 addr: %d\n",
			    DEVNAM(ifp->if_softc), n);

		/* Only pick the first one */
		memcpy(&ipv4elem, data + off, sizeof (ipv4elem));
		ipv4elem.prefixlen = letoh32(ipv4elem.prefixlen);
		addr.s_addr = ipv4elem.addr;

		off = letoh32(ic->ipv4_gwoffs);
		if (off + sizeof (gw) > len)
			goto done;
		memcpy(&gw, data + off, sizeof(gw));

		rv = umb_add_inet_config(sc, addr, ipv4elem.prefixlen, gw);
		if (rv == 0) 
			state = UMB_S_UP;

	}

	memset(sc->sc_info.ipv4dns, 0, sizeof (sc->sc_info.ipv4dns));
	if (avail_v4 & MBIM_IPCONF_HAS_DNSINFO) {
		n = letoh32(ic->ipv4_ndnssrv);
		off = letoh32(ic->ipv4_dnssrvoffs);
		i = 0;
		while (n-- > 0) {
			if (off + sizeof (addr) > len)
				break;
			memcpy(&addr, data + off, sizeof(addr));
			if (i < UMB_MAX_DNSSRV)
				sc->sc_info.ipv4dns[i++] = addr;
			off += sizeof(addr);
			if (ifp->if_flags & IFF_DEBUG) {
				char str[INET_ADDRSTRLEN];
				log(LOG_INFO, "%s: IPv4 nameserver %s\n",
				    DEVNAM(ifp->if_softc), inet_ntop(AF_INET,
				    &addr, str, sizeof(str)));
			}
		}
		umb_send_inet_proposal(sc, AF_INET);
	}
	if ((avail_v4 & MBIM_IPCONF_HAS_MTUINFO)) {
		val = letoh32(ic->ipv4_mtu);
		if (ifp->if_hardmtu != val && val <= sc->sc_maxpktlen) {
			hasmtu = 1;
			ifp->if_hardmtu = val;
			if (ifp->if_mtu > val)
				ifp->if_mtu = val;
		}
	}

tryv6:;
#ifdef INET6
	/*
	 * IPv6 configuration
	 */
	avail_v6 = letoh32(ic->ipv6_available);
	if (avail_v6 == 0) {
		if (ifp->if_flags & IFF_DEBUG)
			log(LOG_INFO, "%s: ISP or WWAN module offers no IPv6 "
			    "support\n", DEVNAM(ifp->if_softc));
		goto done;
	}

	if ((avail_v6 & (MBIM_IPCONF_HAS_ADDRINFO | MBIM_IPCONF_HAS_GWINFO)) ==
	    (MBIM_IPCONF_HAS_ADDRINFO | MBIM_IPCONF_HAS_GWINFO)) {
		n = letoh32(ic->ipv6_naddr);
		off = letoh32(ic->ipv6_addroffs);

		if (n == 0 || off + sizeof (ipv6elem) > len)
			goto done;
		if (n != 1 && ifp->if_flags & IFF_DEBUG)
			log(LOG_INFO, "%s: more than one IPv6 addr: %d\n",
			    DEVNAM(ifp->if_softc), n);

		/* Only pick the first one */
		memcpy(&ipv6elem, data + off, sizeof (ipv6elem));
		memcpy(&addr6, ipv6elem.addr, sizeof (addr6));

		off = letoh32(ic->ipv6_gwoffs);
		if (off + sizeof (gw6) > len)
			goto done;
		memcpy(&gw6, data + off, sizeof (gw6));

		rv = umb_add_inet6_config(sc, &addr6, ipv6elem.prefixlen, &gw6);
		if (rv == 0)
			state = UMB_S_UP;
	}

	if (avail_v6 & MBIM_IPCONF_HAS_DNSINFO) {
		n = letoh32(ic->ipv6_ndnssrv);
		off = letoh32(ic->ipv6_dnssrvoffs);
		i = 0;
		while (n-- > 0) {
			if (off + sizeof (addr6) > len)
				break;
			memcpy(&addr6, data + off, sizeof(addr6));
			if (i < UMB_MAX_DNSSRV)
				sc->sc_info.ipv6dns[i++] = addr6;
			off += sizeof(addr6);
			if (ifp->if_flags & IFF_DEBUG) {
				char str[INET6_ADDRSTRLEN];
				log(LOG_INFO, "%s: IPv6 nameserver %s\n",
				    DEVNAM(ifp->if_softc), inet_ntop(AF_INET6,
				    &addr6, str, sizeof(str)));
			}
		}
		umb_send_inet_proposal(sc, AF_INET6);
	}

	if ((avail_v6 & MBIM_IPCONF_HAS_MTUINFO)) {
		val = letoh32(ic->ipv6_mtu);
		if (ifp->if_hardmtu != val && val <= sc->sc_maxpktlen) {
			hasmtu = 1;
			ifp->if_hardmtu = val;
			if (ifp->if_mtu > val)
				ifp->if_mtu = val;
		}
	}
#endif

done:
	if (hasmtu && (ifp->if_flags & IFF_DEBUG))
		log(LOG_INFO, "%s: MTU %d\n", DEVNAM(sc), ifp->if_hardmtu);

	if (state != -1)
		umb_newstate(sc, state, 0);

	splx(s);
	return 1;
}

void
umb_rx(struct umb_softc *sc)
{
	usbd_setup_xfer(sc->sc_rx_xfer, sc->sc_rx_pipe, sc, sc->sc_rx_buf,
	    sc->sc_rx_bufsz, USBD_SHORT_XFER_OK | USBD_NO_COPY,
	    USBD_NO_TIMEOUT, umb_rxeof);
	usbd_transfer(sc->sc_rx_xfer);
}

void
umb_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct umb_softc *sc = priv;
	struct ifnet *ifp = GET_IFP(sc);

	if (usbd_is_dying(sc->sc_udev) || !(ifp->if_flags & IFF_RUNNING))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		DPRINTF("%s: rx error: %s\n", DEVNAM(sc), usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_rx_pipe);
		if (++sc->sc_rx_nerr > 100) {
			log(LOG_ERR, "%s: too many rx errors, disabling\n",
			    DEVNAM(sc));
			usbd_deactivate(sc->sc_udev);
		}
	} else {
		sc->sc_rx_nerr = 0;
		umb_decap(sc, xfer);
	}

	umb_rx(sc);
	return;
}

int
umb_encap(struct umb_softc *sc, int ndgram)
{
	struct ncm_header16 *hdr16 = NULL;
	struct ncm_header32 *hdr32 = NULL;
	struct ncm_pointer16 *ptr16 = NULL;
	struct ncm_pointer32 *ptr32 = NULL;
	struct ncm_pointer16_dgram *dgram16 = NULL;
	struct ncm_pointer32_dgram *dgram32 = NULL;
	int	 offs = 0, plen = 0;
	int	 dgoffs = 0, poffs;
	struct mbuf *m;
	usbd_status  err;

	/* All size constraints have been validated by the caller! */

	/* NCM Header */
	switch (sc->sc_ncm_format) {
	case NCM_FORMAT_NTB16:
		hdr16 = sc->sc_tx_buf;
		USETDW(hdr16->dwSignature, NCM_HDR16_SIG);
		USETW(hdr16->wHeaderLength, sizeof (*hdr16));
		USETW(hdr16->wSequence, sc->sc_tx_seq);
		USETW(hdr16->wBlockLength, 0);
		offs = sizeof (*hdr16);
		break;
	case NCM_FORMAT_NTB32:
		hdr32 = sc->sc_tx_buf;
		USETDW(hdr32->dwSignature, NCM_HDR32_SIG);
		USETW(hdr32->wHeaderLength, sizeof (*hdr32));
		USETW(hdr32->wSequence, sc->sc_tx_seq);
		USETDW(hdr32->dwBlockLength, 0);
		offs = sizeof (*hdr32);
		break;
	}
	offs += umb_padding(sc->sc_tx_buf, sc->sc_tx_bufsz, offs,
	    sc->sc_align, 0);

	if (sc->sc_flags & UMBFLG_NDP_AT_END) {
		dgoffs = offs;

		/*
		 * Calculate space needed for datagrams.
		 *
		 * XXX cannot use ml_len(&sc->sc_tx_ml), since it ignores
		 *	the padding requirements.
		 */
		poffs = dgoffs;
		MBUF_LIST_FOREACH(&sc->sc_tx_ml, m) {
			poffs += umb_padding(sc->sc_tx_buf, sc->sc_tx_bufsz,
			    poffs, sc->sc_ndp_div, sc->sc_ndp_remainder);
			poffs += m->m_pkthdr.len;
		}
		poffs += umb_padding(sc->sc_tx_buf, sc->sc_tx_bufsz,
		    poffs, sc->sc_ndp_div, sc->sc_ndp_remainder);
	} else
		poffs = offs;

	/* NCM Pointer */
	switch (sc->sc_ncm_format) {
	case NCM_FORMAT_NTB16:
		USETW(hdr16->wNdpIndex, poffs);
		ptr16 = (struct ncm_pointer16 *)(sc->sc_tx_buf + poffs);
		plen = sizeof(*ptr16) + ndgram * sizeof(*dgram16);
		USETDW(ptr16->dwSignature, MBIM_NCM_NTH16_SIG(umb_session_id));
		USETW(ptr16->wLength, plen);
		USETW(ptr16->wNextNdpIndex, 0);
		dgram16 = ptr16->dgram;
		break;
	case NCM_FORMAT_NTB32:
		USETDW(hdr32->dwNdpIndex, poffs);
		ptr32 = (struct ncm_pointer32 *)(sc->sc_tx_buf + poffs);
		plen = sizeof(*ptr32) + ndgram * sizeof(*dgram32);
		USETDW(ptr32->dwSignature, MBIM_NCM_NTH32_SIG(umb_session_id));
		USETW(ptr32->wLength, plen);
		USETW(ptr32->wReserved6, 0);
		USETDW(ptr32->dwNextNdpIndex, 0);
		USETDW(ptr32->dwReserved12, 0);
		dgram32 = ptr32->dgram;
		break;
	}

	if (!(sc->sc_flags & UMBFLG_NDP_AT_END))
		dgoffs = offs + plen;

	/* Encap mbufs to NCM dgrams */
	sc->sc_tx_seq++;
	while ((m = ml_dequeue(&sc->sc_tx_ml)) != NULL) {
		dgoffs += umb_padding(sc->sc_tx_buf, sc->sc_tx_bufsz, dgoffs,
		    sc->sc_ndp_div, sc->sc_ndp_remainder);
		switch (sc->sc_ncm_format) {
		case NCM_FORMAT_NTB16:
			USETW(dgram16->wDatagramIndex, dgoffs);
			USETW(dgram16->wDatagramLen, m->m_pkthdr.len);
			dgram16++;
			break;
		case NCM_FORMAT_NTB32:
			USETDW(dgram32->dwDatagramIndex, dgoffs);
			USETDW(dgram32->dwDatagramLen, m->m_pkthdr.len);
			dgram32++;
			break;
		}
		m_copydata(m, 0, m->m_pkthdr.len, sc->sc_tx_buf + dgoffs);
		dgoffs += m->m_pkthdr.len;
		m_freem(m);
	}

	if (sc->sc_flags & UMBFLG_NDP_AT_END)
		offs = poffs + plen;
	else
		offs = dgoffs;

	/* Terminating pointer and datagram size */
	switch (sc->sc_ncm_format) {
	case NCM_FORMAT_NTB16:
		USETW(dgram16->wDatagramIndex, 0);
		USETW(dgram16->wDatagramLen, 0);
		USETW(hdr16->wBlockLength, offs);
		KASSERT(dgram16 - ptr16->dgram == ndgram);
		break;
	case NCM_FORMAT_NTB32:
		USETDW(dgram32->dwDatagramIndex, 0);
		USETDW(dgram32->dwDatagramLen, 0);
		USETDW(hdr32->dwBlockLength, offs);
		KASSERT(dgram32 - ptr32->dgram == ndgram);
		break;
	}

	DPRINTFN(3, "%s: encap %d bytes\n", DEVNAM(sc), offs);
	DDUMPN(5, sc->sc_tx_buf, offs);
	KASSERT(offs <= sc->sc_tx_bufsz);

	usbd_setup_xfer(sc->sc_tx_xfer, sc->sc_tx_pipe, sc, sc->sc_tx_buf, offs,
	    USBD_FORCE_SHORT_XFER | USBD_NO_COPY, umb_xfer_tout, umb_txeof);
	err = usbd_transfer(sc->sc_tx_xfer);
	if (err != USBD_IN_PROGRESS) {
		DPRINTF("%s: start tx error: %s\n", DEVNAM(sc),
		    usbd_errstr(err));
		ml_purge(&sc->sc_tx_ml);
		return 0;
	}
	return 1;
}

void
umb_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct umb_softc *sc = priv;
	struct ifnet *ifp = GET_IFP(sc);
	int	 s;

	s = splnet();
	ml_purge(&sc->sc_tx_ml);
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status != USBD_NOT_STARTED && status != USBD_CANCELLED) {
			ifp->if_oerrors++;
			DPRINTF("%s: tx error: %s\n", DEVNAM(sc),
			    usbd_errstr(status));
			if (status == USBD_STALLED)
				usbd_clear_endpoint_stall_async(sc->sc_tx_pipe);
		}
	}
	if (ifq_empty(&ifp->if_snd) == 0)
		umb_start(ifp);

	splx(s);
}

void
umb_decap(struct umb_softc *sc, struct usbd_xfer *xfer)
{
	struct ifnet *ifp = GET_IFP(sc);
	int	 s;
	void	*buf;
	uint32_t len;
	char	*dp;
	struct ncm_header16 *hdr16;
	struct ncm_header32 *hdr32;
	struct ncm_pointer16 *ptr16;
	struct ncm_pointer16_dgram *dgram16;
	struct ncm_pointer32_dgram *dgram32;
	uint32_t hsig, psig;
	uint32_t ptrlen, dgentryoff;
	uint64_t blen, ptroff, doff, dlen;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;

	usbd_get_xfer_status(xfer, NULL, &buf, &len, NULL);
	DPRINTFN(4, "%s: recv %d bytes\n", DEVNAM(sc), len);
	DDUMPN(5, buf, len);
	s = splnet();
	if (len < sizeof (*hdr16))
		goto toosmall;

	hdr16 = (struct ncm_header16 *)buf;
	hsig = UGETDW(hdr16->dwSignature);

	switch (hsig) {
	case NCM_HDR16_SIG:
		blen = UGETW(hdr16->wBlockLength);
		ptroff = UGETW(hdr16->wNdpIndex);
		if (UGETW(hdr16->wHeaderLength) != sizeof (*hdr16)) {
			DPRINTF("%s: bad header len %d for NTH16 (exp %zu)\n",
			    DEVNAM(sc), UGETW(hdr16->wHeaderLength),
			    sizeof (*hdr16));
			goto fail;
		}
		break;
	case NCM_HDR32_SIG:
		if (len < sizeof (*hdr32))
			goto toosmall;
		hdr32 = (struct ncm_header32 *)hdr16;
		blen = UGETDW(hdr32->dwBlockLength);
		ptroff = UGETDW(hdr32->dwNdpIndex);
		if (UGETW(hdr32->wHeaderLength) != sizeof (*hdr32)) {
			DPRINTF("%s: bad header len %d for NTH32 (exp %zu)\n",
			    DEVNAM(sc), UGETW(hdr32->wHeaderLength),
			    sizeof (*hdr32));
			goto fail;
		}
		break;
	default:
		DPRINTF("%s: unsupported NCM header signature (0x%08x)\n",
		    DEVNAM(sc), hsig);
		goto fail;
	}
	if (blen != 0 && len < blen) {
		DPRINTF("%s: bad NTB len (%llu) for %d bytes of data\n",
		    DEVNAM(sc), blen, len);
		goto fail;
	}

	if (len < ptroff)
		goto toosmall;
	ptr16 = (struct ncm_pointer16 *)(buf + ptroff);
	psig = UGETDW(ptr16->dwSignature);
	ptrlen = UGETW(ptr16->wLength);
	if ((uint64_t)len < (uint64_t)ptrlen + ptroff)
		goto toosmall;
	if (!MBIM_NCM_NTH16_ISISG(psig) && !MBIM_NCM_NTH32_ISISG(psig)) {
		DPRINTF("%s: unsupported NCM pointer signature (0x%08x)\n",
		    DEVNAM(sc), psig);
		goto fail;
	}

	switch (hsig) {
	case NCM_HDR16_SIG:
		dgentryoff = offsetof(struct ncm_pointer16, dgram);
		break;
	case NCM_HDR32_SIG:
		dgentryoff = offsetof(struct ncm_pointer32, dgram);
		break;
	default:
		goto fail;
	}

	while (dgentryoff < ptrlen) {
		switch (hsig) {
		case NCM_HDR16_SIG:
			if (ptroff + dgentryoff < sizeof (*dgram16))
				goto done;
			dgram16 = (struct ncm_pointer16_dgram *)
			    (buf + ptroff + dgentryoff);
			dgentryoff += sizeof (*dgram16);
			dlen = UGETW(dgram16->wDatagramLen);
			doff = UGETW(dgram16->wDatagramIndex);
			break;
		case NCM_HDR32_SIG:
			if (ptroff + dgentryoff < sizeof (*dgram32))
				goto done;
			dgram32 = (struct ncm_pointer32_dgram *)
			    (buf + ptroff + dgentryoff);
			dgentryoff += sizeof (*dgram32);
			dlen = UGETDW(dgram32->dwDatagramLen);
			doff = UGETDW(dgram32->dwDatagramIndex);
			break;
		default:
			ifp->if_ierrors++;
			goto done;
		}

		/* Terminating zero entry */
		if (dlen == 0 || doff == 0)
			break;
		if ((uint64_t)len < dlen + doff) {
			/* Skip giant datagram but continue processing */
			DPRINTF("%s: datagram too large (%llu @ off %llu)\n",
			    DEVNAM(sc), dlen, doff);
			continue;
		}

		dp = buf + doff;
		DPRINTFN(3, "%s: decap %llu bytes\n", DEVNAM(sc), dlen);
		m = m_devget(dp, (int)dlen, sizeof(uint32_t));
		if (m == NULL) {
			ifp->if_iqdrops++;
			continue;
		}
		switch (*dp & 0xf0) {
		case 4 << 4:
			m->m_pkthdr.ph_family = AF_INET;
			break;
		case 6 << 4:
			m->m_pkthdr.ph_family = AF_INET6;
			break;
		}
		ml_enqueue(&ml, m);
	}
done:
	if_input(ifp, &ml);
	splx(s);
	return;
toosmall:
	DPRINTF("%s: packet too small (%d)\n", DEVNAM(sc), len);
fail:
	ifp->if_ierrors++;
	splx(s);
}

usbd_status
umb_send_encap_command(struct umb_softc *sc, void *data, int len)
{
	struct usbd_xfer *xfer;
	usb_device_request_t req;
	char *buf;

	if (len > sc->sc_ctrl_len)
		return USBD_INVAL;

	if ((xfer = usbd_alloc_xfer(sc->sc_udev)) == NULL)
		return USBD_NOMEM;
	if ((buf = usbd_alloc_buffer(xfer, len)) == NULL) {
		usbd_free_xfer(xfer);
		return USBD_NOMEM;
	}
	memcpy(buf, data, len);

	/* XXX FIXME: if (total len > sc->sc_ctrl_len) => must fragment */
	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SEND_ENCAPSULATED_COMMAND;
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->sc_ctrl_ifaceno);
	USETW(req.wLength, len);
	DELAY(umb_delay);
	return usbd_request_async(xfer, &req, NULL, NULL);
}

int
umb_get_encap_response(struct umb_softc *sc, void *buf, int *len)
{
	usb_device_request_t req;
	usbd_status err;

	req.bmRequestType = UT_READ_CLASS_INTERFACE;
	req.bRequest = UCDC_GET_ENCAPSULATED_RESPONSE;
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->sc_ctrl_ifaceno);
	USETW(req.wLength, *len);
	/* XXX FIXME: re-assemble fragments */

	DELAY(umb_delay);
	err = usbd_do_request_flags(sc->sc_udev, &req, buf, USBD_SHORT_XFER_OK,
	    len, umb_xfer_tout);
	if (err == USBD_NORMAL_COMPLETION)
		return 1;
	DPRINTF("%s: ctrl recv: %s\n", DEVNAM(sc), usbd_errstr(err));
	return 0;
}

void
umb_ctrl_msg(struct umb_softc *sc, uint32_t req, void *data, int len)
{
	struct ifnet *ifp = GET_IFP(sc);
	uint32_t tid;
	struct mbim_msghdr *hdr = data;
	usbd_status err;
	int	 s;

	assertwaitok();
	if (usbd_is_dying(sc->sc_udev))
		return;
	if (len < sizeof (*hdr))
		return;
	tid = ++sc->sc_tid;

	hdr->type = htole32(req);
	hdr->len = htole32(len);
	hdr->tid = htole32(tid);

#ifdef UMB_DEBUG
	if (umb_debug) {
		const char *op, *str;
		if (req == MBIM_COMMAND_MSG) {
			struct mbim_h2f_cmd *c = data;
			if (letoh32(c->op) == MBIM_CMDOP_SET)
				op = "set";
			else
				op = "qry";
			str = umb_cid2str(letoh32(c->cid));
		} else {
			op = "snd";
			str = umb_request2str(req);
		}
		DPRINTF("%s: -> %s %s (tid %u)\n", DEVNAM(sc), op, str, tid);
	}
#endif
	s = splusb();
	err = umb_send_encap_command(sc, data, len);
	splx(s);
	if (err != USBD_NORMAL_COMPLETION) {
		if (ifp->if_flags & IFF_DEBUG)
			log(LOG_ERR, "%s: send %s msg (tid %u) failed: %s\n",
			    DEVNAM(sc), umb_request2str(req), tid,
			    usbd_errstr(err));

		/* will affect other transactions, too */
		usbd_abort_pipe(sc->sc_udev->default_pipe);
	} else {
		DPRINTFN(2, "%s: sent %s (tid %u)\n", DEVNAM(sc),
		    umb_request2str(req), tid);
		DDUMPN(3, data, len);
	}
	return;
}

void
umb_open(struct umb_softc *sc)
{
	struct mbim_h2f_openmsg msg;

	memset(&msg, 0, sizeof (msg));
	msg.maxlen = htole32(sc->sc_ctrl_len);
	umb_ctrl_msg(sc, MBIM_OPEN_MSG, &msg, sizeof (msg));
	return;
}

void
umb_close(struct umb_softc *sc)
{
	struct mbim_h2f_closemsg msg;

	memset(&msg, 0, sizeof (msg));
	umb_ctrl_msg(sc, MBIM_CLOSE_MSG, &msg, sizeof (msg));
}

int
umb_setpin(struct umb_softc *sc, int op, int is_puk, void *pin, int pinlen,
    void *newpin, int newpinlen)
{
	struct mbim_cid_pin cp;
	int	 off;

	if (pinlen == 0)
		return 0;
	if (pinlen < 0 || pinlen > MBIM_PIN_MAXLEN ||
	    newpinlen < 0 || newpinlen > MBIM_PIN_MAXLEN ||
	    op < 0 || op > MBIM_PIN_OP_CHANGE ||
	    (is_puk && op != MBIM_PIN_OP_ENTER))
		return EINVAL;

	memset(&cp, 0, sizeof (cp));
	cp.type = htole32(is_puk ? MBIM_PIN_TYPE_PUK1 : MBIM_PIN_TYPE_PIN1);

	off = offsetof(struct mbim_cid_pin, data);
	if (!umb_addstr(&cp, sizeof (cp), &off, pin, pinlen,
	    &cp.pin_offs, &cp.pin_size))
		return EINVAL;

	cp.op  = htole32(op);
	if (newpinlen) {
		if (!umb_addstr(&cp, sizeof (cp), &off, newpin, newpinlen,
		    &cp.newpin_offs, &cp.newpin_size))
			return EINVAL;
	} else {
		if ((op == MBIM_PIN_OP_CHANGE) || is_puk)
			return EINVAL;
		if (!umb_addstr(&cp, sizeof (cp), &off, NULL, 0,
		    &cp.newpin_offs, &cp.newpin_size))
			return EINVAL;
	}
	umb_cmd(sc, MBIM_CID_PIN, MBIM_CMDOP_SET, &cp, off);
	return 0;
}

void
umb_setdataclass(struct umb_softc *sc)
{
	struct mbim_cid_registration_state rs;
	uint32_t	 classes;

	if (sc->sc_info.supportedclasses == MBIM_DATACLASS_NONE)
		return;

	memset(&rs, 0, sizeof (rs));
	rs.regaction = htole32(MBIM_REGACTION_AUTOMATIC);
	classes = sc->sc_info.supportedclasses;
	if (sc->sc_info.preferredclasses != MBIM_DATACLASS_NONE)
		classes &= sc->sc_info.preferredclasses;
	rs.data_class = htole32(classes);
	umb_cmd(sc, MBIM_CID_REGISTER_STATE, MBIM_CMDOP_SET, &rs, sizeof (rs));
}

void
umb_radio(struct umb_softc *sc, int on)
{
	struct mbim_cid_radio_state s;

	DPRINTF("%s: set radio %s\n", DEVNAM(sc), on ? "on" : "off");
	memset(&s, 0, sizeof (s));
	s.state = htole32(on ? MBIM_RADIO_STATE_ON : MBIM_RADIO_STATE_OFF);
	umb_cmd(sc, MBIM_CID_RADIO_STATE, MBIM_CMDOP_SET, &s, sizeof (s));
}

void
umb_allocate_cid(struct umb_softc *sc)
{
	umb_cmd1(sc, MBIM_CID_DEVICE_CAPS, MBIM_CMDOP_SET,
	    umb_qmi_alloc_cid, sizeof (umb_qmi_alloc_cid), umb_uuid_qmi_mbim);
}

void
umb_send_fcc_auth(struct umb_softc *sc)
{
	uint8_t	 fccauth[sizeof (umb_qmi_fcc_auth)];

	if (sc->sc_cid == -1) {
		DPRINTF("%s: missing CID, cannot send FCC auth\n", DEVNAM(sc));
		umb_allocate_cid(sc);
		return;
	}
	memcpy(fccauth, umb_qmi_fcc_auth, sizeof (fccauth));
	fccauth[UMB_QMI_CID_OFFS] = sc->sc_cid;
	umb_cmd1(sc, MBIM_CID_DEVICE_CAPS, MBIM_CMDOP_SET,
	    fccauth, sizeof (fccauth), umb_uuid_qmi_mbim);
}

void
umb_packet_service(struct umb_softc *sc, int attach)
{
	struct mbim_cid_packet_service	s;

	DPRINTF("%s: %s packet service\n", DEVNAM(sc),
	    attach ? "attach" : "detach");
	memset(&s, 0, sizeof (s));
	s.action = htole32(attach ?
	    MBIM_PKTSERVICE_ACTION_ATTACH : MBIM_PKTSERVICE_ACTION_DETACH);
	umb_cmd(sc, MBIM_CID_PACKET_SERVICE, MBIM_CMDOP_SET, &s, sizeof (s));
}

void
umb_connect(struct umb_softc *sc)
{
	struct ifnet *ifp = GET_IFP(sc);

	if (sc->sc_info.regstate == MBIM_REGSTATE_ROAMING && !sc->sc_roaming) {
		log(LOG_INFO, "%s: connection disabled in roaming network\n",
		    DEVNAM(sc));
		return;
	}
	if (ifp->if_flags & IFF_DEBUG)
		log(LOG_DEBUG, "%s: connecting ...\n", DEVNAM(sc));
	umb_send_connect(sc, MBIM_CONNECT_ACTIVATE);
}

void
umb_disconnect(struct umb_softc *sc)
{
	struct ifnet *ifp = GET_IFP(sc);

	if (ifp->if_flags & IFF_DEBUG)
		log(LOG_DEBUG, "%s: disconnecting ...\n", DEVNAM(sc));
	umb_send_connect(sc, MBIM_CONNECT_DEACTIVATE);
}

void
umb_send_connect(struct umb_softc *sc, int command)
{
	struct mbim_cid_connect *c;
	int	 off;

	/* Too large or the stack */
	c = malloc(sizeof (*c), M_USBDEV, M_WAIT|M_ZERO);
	c->sessionid = htole32(umb_session_id);
	c->command = htole32(command);
	off = offsetof(struct mbim_cid_connect, data);
	if (!umb_addstr(c, sizeof (*c), &off, sc->sc_info.apn,
	    sc->sc_info.apnlen, &c->access_offs, &c->access_size))
		goto done;
	/* XXX FIXME: support user name and passphrase */
	c->user_offs = htole32(0);
	c->user_size = htole32(0);
	c->passwd_offs = htole32(0);
	c->passwd_size = htole32(0);
	c->authprot = htole32(MBIM_AUTHPROT_NONE);
	c->compression = htole32(MBIM_COMPRESSION_NONE);
	c->iptype = htole32(MBIM_CONTEXT_IPTYPE_IPV4);
#ifdef INET6
	/* XXX FIXME: support IPv6-only mode, too */
	if ((sc->sc_flags & UMBFLG_NO_INET6) == 0 &&
	    in6ifa_ifpforlinklocal(GET_IFP(sc), 0) != NULL)
		c->iptype = htole32(MBIM_CONTEXT_IPTYPE_IPV4V6);
#endif
	memcpy(c->context, umb_uuid_context_internet, sizeof (c->context));
	umb_cmd(sc, MBIM_CID_CONNECT, MBIM_CMDOP_SET, c, off);
done:
	free(c, M_USBDEV, sizeof (*c));
	return;
}

void
umb_qry_ipconfig(struct umb_softc *sc)
{
	struct mbim_cid_ip_configuration_info ipc;

	memset(&ipc, 0, sizeof (ipc));
	ipc.sessionid = htole32(umb_session_id);
	umb_cmd(sc, MBIM_CID_IP_CONFIGURATION, MBIM_CMDOP_QRY,
	    &ipc, sizeof (ipc));
}

void
umb_cmd(struct umb_softc *sc, int cid, int op, void *data, int len)
{
	umb_cmd1(sc, cid, op, data, len, umb_uuid_basic_connect);
}

void
umb_cmd1(struct umb_softc *sc, int cid, int op, void *data, int len,
    uint8_t *uuid)
{
	struct mbim_h2f_cmd *cmd;
	int	totlen;

	/* XXX FIXME support sending fragments */
	if (sizeof (*cmd) + len > sc->sc_ctrl_len) {
		DPRINTF("%s: set %s msg too long: cannot send\n",
		    DEVNAM(sc), umb_cid2str(cid));
		return;
	}
	cmd = sc->sc_ctrl_msg;
	memset(cmd, 0, sizeof (*cmd));
	cmd->frag.nfrag = htole32(1);
	memcpy(cmd->devid, uuid, sizeof (cmd->devid));
	cmd->cid = htole32(cid);
	cmd->op = htole32(op);
	cmd->infolen = htole32(len);
	totlen = sizeof (*cmd);
	if (len > 0) {
		memcpy(cmd + 1, data, len);
		totlen += len;
	}
	umb_ctrl_msg(sc, MBIM_COMMAND_MSG, cmd, totlen);
}

void
umb_command_done(struct umb_softc *sc, void *data, int len)
{
	struct mbim_f2h_cmddone *cmd = data;
	struct ifnet *ifp = GET_IFP(sc);
	uint32_t status;
	uint32_t cid;
	uint32_t infolen;
	int	 qmimsg = 0;

	if (len < sizeof (*cmd)) {
		DPRINTF("%s: discard short %s message\n", DEVNAM(sc),
		    umb_request2str(letoh32(cmd->hdr.type)));
		return;
	}
	cid = letoh32(cmd->cid);
	if (memcmp(cmd->devid, umb_uuid_basic_connect, sizeof (cmd->devid))) {
		if (memcmp(cmd->devid, umb_uuid_qmi_mbim,
		    sizeof (cmd->devid))) {
			DPRINTF("%s: discard %s message for other UUID '%s'\n",
			    DEVNAM(sc), umb_request2str(letoh32(cmd->hdr.type)),
			    umb_uuid2str(cmd->devid));
			return;
		} else
			qmimsg = 1;
	}

	status = letoh32(cmd->status);
	switch (status) {
	case MBIM_STATUS_SUCCESS:
		break;
#ifdef INET6
	case MBIM_STATUS_NO_DEVICE_SUPPORT:
		if ((cid == MBIM_CID_CONNECT) &&
		    (sc->sc_flags & UMBFLG_NO_INET6) == 0) {
			sc->sc_flags |= UMBFLG_NO_INET6;
			if (ifp->if_flags & IFF_DEBUG)
				log(LOG_ERR,
				    "%s: device does not support IPv6\n",
				    DEVNAM(sc));
		}
		/* Re-trigger the connect, this time IPv4 only */
		usb_add_task(sc->sc_udev, &sc->sc_umb_task);
		return;
#endif
	case MBIM_STATUS_NOT_INITIALIZED:
		if (ifp->if_flags & IFF_DEBUG)
			log(LOG_ERR, "%s: SIM not initialized (PIN missing)\n",
			    DEVNAM(sc));
		return;
	case MBIM_STATUS_PIN_REQUIRED:
		sc->sc_info.pin_state = UMB_PIN_REQUIRED;
		/*FALLTHROUGH*/
	default:
		if (ifp->if_flags & IFF_DEBUG)
			log(LOG_ERR, "%s: set/qry %s failed: %s\n", DEVNAM(sc),
			    umb_cid2str(cid), umb_status2str(status));
		return;
	}

	infolen = letoh32(cmd->infolen);
	if (len < sizeof (*cmd) + infolen) {
		DPRINTF("%s: discard truncated %s message (want %d, got %d)\n",
		    DEVNAM(sc), umb_cid2str(cid),
		    (int)sizeof (*cmd) + infolen, len);
		return;
	}
	if (qmimsg) {
		if (sc->sc_flags & UMBFLG_FCC_AUTH_REQUIRED)
			umb_decode_qmi(sc, cmd->info, infolen);
	} else {
		DPRINTFN(2, "%s: set/qry %s done\n", DEVNAM(sc),
		    umb_cid2str(cid));
		umb_decode_cid(sc, cid, cmd->info, infolen);
	}
}

void
umb_decode_cid(struct umb_softc *sc, uint32_t cid, void *data, int len)
{
	int	 ok = 1;

	switch (cid) {
	case MBIM_CID_DEVICE_CAPS:
		ok = umb_decode_devices_caps(sc, data, len);
		break;
	case MBIM_CID_SUBSCRIBER_READY_STATUS:
		ok = umb_decode_subscriber_status(sc, data, len);
		break;
	case MBIM_CID_RADIO_STATE:
		ok = umb_decode_radio_state(sc, data, len);
		break;
	case MBIM_CID_PIN:
		ok = umb_decode_pin(sc, data, len);
		break;
	case MBIM_CID_REGISTER_STATE:
		ok = umb_decode_register_state(sc, data, len);
		break;
	case MBIM_CID_PACKET_SERVICE:
		ok = umb_decode_packet_service(sc, data, len);
		break;
	case MBIM_CID_SIGNAL_STATE:
		ok = umb_decode_signal_state(sc, data, len);
		break;
	case MBIM_CID_CONNECT:
		ok = umb_decode_connect_info(sc, data, len);
		break;
	case MBIM_CID_IP_CONFIGURATION:
		ok = umb_decode_ip_configuration(sc, data, len);
		break;
	default:
		/*
		 * Note: the above list is incomplete and only contains
		 *	mandatory CIDs from the BASIC_CONNECT set.
		 *	So alternate values are not unusual.
		 */
		DPRINTFN(4, "%s: ignore %s\n", DEVNAM(sc), umb_cid2str(cid));
		break;
	}
	if (!ok)
		DPRINTF("%s: discard %s with bad info length %d\n",
		    DEVNAM(sc), umb_cid2str(cid), len);
	return;
}

void
umb_decode_qmi(struct umb_softc *sc, uint8_t *data, int len)
{
	uint8_t	srv;
	uint16_t msg, tlvlen;
	uint32_t val;

#define UMB_QMI_QMUXLEN		6
	if (len < UMB_QMI_QMUXLEN)
		goto tooshort;

	srv = data[4];
	data += UMB_QMI_QMUXLEN;
	len -= UMB_QMI_QMUXLEN;

#define UMB_GET16(p)	((uint16_t)*p | (uint16_t)*(p + 1) << 8)
#define UMB_GET32(p)	((uint32_t)*p | (uint32_t)*(p + 1) << 8 | \
			    (uint32_t)*(p + 2) << 16 |(uint32_t)*(p + 3) << 24)
	switch (srv) {
	case 0:	/* ctl */
#define UMB_QMI_CTLLEN		6
		if (len < UMB_QMI_CTLLEN)
			goto tooshort;
		msg = UMB_GET16(&data[2]);
		tlvlen = UMB_GET16(&data[4]);
		data += UMB_QMI_CTLLEN;
		len -= UMB_QMI_CTLLEN;
		break;
	case 2:	/* dms  */
#define UMB_QMI_DMSLEN		7
		if (len < UMB_QMI_DMSLEN)
			goto tooshort;
		msg = UMB_GET16(&data[3]);
		tlvlen = UMB_GET16(&data[5]);
		data += UMB_QMI_DMSLEN;
		len -= UMB_QMI_DMSLEN;
		break;
	default:
		DPRINTF("%s: discard QMI message for unknown service type %d\n",
		    DEVNAM(sc), srv);
		return;
	}

	if (len < tlvlen)
		goto tooshort;

#define UMB_QMI_TLVLEN		3
	while (len > 0) {
		if (len < UMB_QMI_TLVLEN)
			goto tooshort;
		tlvlen = UMB_GET16(&data[1]);
		if (len < UMB_QMI_TLVLEN + tlvlen)
			goto tooshort;
		switch (data[0]) {
		case 1:	/* allocation info */
			if (msg == 0x0022) {	/* Allocate CID */
				if (tlvlen != 2 || data[3] != 2) /* dms */
					break;
				sc->sc_cid = data[4];
				DPRINTF("%s: QMI CID %d allocated\n",
				    DEVNAM(sc), sc->sc_cid);
				umb_newstate(sc, UMB_S_CID, UMB_NS_DONT_DROP);
			}
			break;
		case 2:	/* response */
			if (tlvlen != sizeof (val))
				break;
			val = UMB_GET32(&data[3]);
			switch (msg) {
			case 0x0022:	/* Allocate CID */
				if (val != 0) {
					log(LOG_ERR, "%s: allocation of QMI CID"
					    " failed, error 0x%x\n", DEVNAM(sc),
					    val);
					/* XXX how to proceed? */
					return;
				}
				break;
			case 0x555f:	/* Send FCC Authentication */
				if (val == 0)
					DPRINTF("%s: send FCC "
					    "Authentication succeeded\n",
					    DEVNAM(sc));
				else if (val == 0x001a0001)
					DPRINTF("%s: FCC Authentication "
					    "not required\n", DEVNAM(sc));
				else
					log(LOG_INFO, "%s: send FCC "
					    "Authentication failed, "
					    "error 0x%x\n", DEVNAM(sc), val);

				/* FCC Auth is needed only once after power-on*/
				sc->sc_flags &= ~UMBFLG_FCC_AUTH_REQUIRED;

				/* Try to proceed anyway */
				DPRINTF("%s: init: turning radio on ...\n",
				    DEVNAM(sc));
				umb_radio(sc, 1);
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
		data += UMB_QMI_TLVLEN + tlvlen;
		len -= UMB_QMI_TLVLEN + tlvlen;
	}
	return;

tooshort:
	DPRINTF("%s: discard short QMI message\n", DEVNAM(sc));
	return;
}

void
umb_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct umb_softc *sc = priv;
	struct ifnet *ifp = GET_IFP(sc);
	int	 total_len;

	if (status != USBD_NORMAL_COMPLETION) {
		DPRINTF("%s: notification error: %s\n", DEVNAM(sc),
		    usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_ctrl_pipe);
		return;
	}
	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);
	if (total_len < UCDC_NOTIFICATION_LENGTH) {
		DPRINTF("%s: short notification (%d<%d)\n", DEVNAM(sc),
		    total_len, UCDC_NOTIFICATION_LENGTH);
		return;
	}
	if (sc->sc_intr_msg.bmRequestType != UCDC_NOTIFICATION) {
		DPRINTF("%s: unexpected notification (type=0x%02x)\n",
		    DEVNAM(sc), sc->sc_intr_msg.bmRequestType);
		return;
	}

	switch (sc->sc_intr_msg.bNotification) {
	case UCDC_N_NETWORK_CONNECTION:
		if (ifp->if_flags & IFF_DEBUG)
			log(LOG_DEBUG, "%s: network %sconnected\n", DEVNAM(sc),
			    UGETW(sc->sc_intr_msg.wValue) ? "" : "dis");
		break;
	case UCDC_N_RESPONSE_AVAILABLE:
		DPRINTFN(2, "%s: umb_intr: response available\n", DEVNAM(sc));
		++sc->sc_nresp;
		usb_add_task(sc->sc_udev, &sc->sc_get_response_task);
		break;
	case UCDC_N_CONNECTION_SPEED_CHANGE:
		DPRINTFN(2, "%s: umb_intr: connection speed changed\n",
		    DEVNAM(sc));
		break;
	default:
		DPRINTF("%s: unexpected notification (0x%02x)\n",
		    DEVNAM(sc), sc->sc_intr_msg.bNotification);
		break;
	}
}

/*
 * Diagnostic routines
 */
#ifdef UMB_DEBUG
char *
umb_uuid2str(uint8_t uuid[MBIM_UUID_LEN])
{
	static char uuidstr[2 * MBIM_UUID_LEN + 5];

#define UUID_BFMT	"%02X"
#define UUID_SEP	"-"
	snprintf(uuidstr, sizeof (uuidstr),
	    UUID_BFMT UUID_BFMT UUID_BFMT UUID_BFMT UUID_SEP
	    UUID_BFMT UUID_BFMT UUID_SEP
	    UUID_BFMT UUID_BFMT UUID_SEP
	    UUID_BFMT UUID_BFMT UUID_SEP
	    UUID_BFMT UUID_BFMT UUID_BFMT UUID_BFMT UUID_BFMT UUID_BFMT,
	    uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5],
	    uuid[6], uuid[7], uuid[8], uuid[9], uuid[10], uuid[11],
	    uuid[12], uuid[13], uuid[14], uuid[15]);
	return uuidstr;
}

void
umb_dump(void *buf, int len)
{
	int	 i = 0;
	uint8_t	*c = buf;

	if (len == 0)
		return;
	while (i < len) {
		if ((i % 16) == 0) {
			if (i > 0)
				addlog("\n");
			log(LOG_DEBUG, "%4d:  ", i);
		}
		addlog(" %02x", *c);
		c++;
		i++;
	}
	addlog("\n");
}
#endif /* UMB_DEBUG */

#if NKSTAT > 0

void
umb_kstat_attach(struct umb_softc *sc)
{
	struct kstat *ks;
	struct umb_kstat_signal *uks;

	rw_init(&sc->sc_kstat_lock, "umbkstat");

	ks = kstat_create(DEVNAM(sc), 0, "mbim-signal", 0, KSTAT_T_KV, 0);
	if (ks == NULL)
		return;

	uks = malloc(sizeof(*uks), M_DEVBUF, M_WAITOK|M_ZERO);
	kstat_kv_init(&uks->rssi, "rssi", KSTAT_KV_T_NULL);
	kstat_kv_init(&uks->error_rate, "error rate", KSTAT_KV_T_NULL);
	kstat_kv_init(&uks->reports, "reports", KSTAT_KV_T_COUNTER64);

	kstat_set_rlock(ks, &sc->sc_kstat_lock);
	ks->ks_data = uks;
	ks->ks_datalen = sizeof(*uks);
	ks->ks_read = kstat_read_nop;

	ks->ks_softc = sc;
	sc->sc_kstat_signal = ks;
	kstat_install(ks);
}

void
umb_kstat_detach(struct umb_softc *sc)
{
	struct kstat *ks = sc->sc_kstat_signal;
	struct umb_kstat_signal *uks;

	if (ks == NULL)
		return;

	kstat_remove(ks);
	sc->sc_kstat_signal = NULL;

	uks = ks->ks_data;
	free(uks, M_DEVBUF, sizeof(*uks));

	kstat_destroy(ks);
}
#endif /* NKSTAT > 0 */
