/*	$OpenBSD: if_urndis.c,v 1.74 2024/05/23 03:21:09 jsg Exp $ */

/*
 * Copyright (c) 2010 Jonathan Armani <armani@openbsd.org>
 * Copyright (c) 2010 Fabien Romano <fabien@openbsd.org>
 * Copyright (c) 2010 Michael Knudsen <mk@openbsd.org>
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

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>

#include <sys/device.h>

#include <machine/bus.h>

#include <net/if.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#include <dev/rndis.h>

#include <dev/usb/if_urndisreg.h>

#ifdef URNDIS_DEBUG
#define DPRINTF(x)      do { printf x; } while (0)
#else
#define DPRINTF(x)
#endif

#define DEVNAME(sc)	((sc)->sc_dev.dv_xname)

int urndis_newbuf(struct urndis_softc *, struct urndis_chain *);

int urndis_ioctl(struct ifnet *, u_long, caddr_t);
#if 0
void urndis_watchdog(struct ifnet *);
#endif

void urndis_start(struct ifnet *);
void urndis_rxeof(struct usbd_xfer *, void *, usbd_status);
void urndis_txeof(struct usbd_xfer *, void *, usbd_status);
int urndis_rx_list_init(struct urndis_softc *);
int urndis_tx_list_init(struct urndis_softc *);

void urndis_init(struct urndis_softc *);
void urndis_stop(struct urndis_softc *);

usbd_status urndis_ctrl_msg(struct urndis_softc *, uint8_t, uint8_t,
    uint16_t, uint16_t, void *, size_t);
usbd_status urndis_ctrl_send(struct urndis_softc *, void *, size_t);
struct rndis_comp_hdr *urndis_ctrl_recv(struct urndis_softc *);

u_int32_t urndis_ctrl_handle(struct urndis_softc *,
    struct rndis_comp_hdr *, void **, size_t *);
u_int32_t urndis_ctrl_handle_init(struct urndis_softc *,
    const struct rndis_comp_hdr *);
u_int32_t urndis_ctrl_handle_query(struct urndis_softc *,
    const struct rndis_comp_hdr *, void **, size_t *);
u_int32_t urndis_ctrl_handle_reset(struct urndis_softc *,
    const struct rndis_comp_hdr *);
u_int32_t urndis_ctrl_handle_status(struct urndis_softc *,
    const struct rndis_comp_hdr *);

u_int32_t urndis_ctrl_init(struct urndis_softc *);
u_int32_t urndis_ctrl_halt(struct urndis_softc *);
u_int32_t urndis_ctrl_query(struct urndis_softc *, u_int32_t, void *, size_t,
    void **, size_t *);
u_int32_t urndis_ctrl_set(struct urndis_softc *, u_int32_t, void *, size_t);
u_int32_t urndis_ctrl_set_param(struct urndis_softc *, const char *, u_int32_t,
    void *, size_t);
#if 0
u_int32_t urndis_ctrl_reset(struct urndis_softc *);
u_int32_t urndis_ctrl_keepalive(struct urndis_softc *);
#endif

int urndis_encap(struct urndis_softc *, struct mbuf *, int);
void urndis_decap(struct urndis_softc *, struct urndis_chain *, u_int32_t);

const struct urndis_class *urndis_lookup(usb_interface_descriptor_t *);

int urndis_match(struct device *, void *, void *);
void urndis_attach(struct device *, struct device *, void *);
int urndis_detach(struct device *, int);

struct cfdriver urndis_cd = {
	NULL, "urndis", DV_IFNET
};

const struct cfattach urndis_ca = {
	sizeof(struct urndis_softc), urndis_match, urndis_attach, urndis_detach
};

const struct urndis_class {
	u_int8_t class;
	u_int8_t subclass;
	u_int8_t protocol;
	const char *typestr;
} urndis_class[] = {
	{ UICLASS_CDC, UISUBCLASS_ABSTRACT_CONTROL_MODEL, 0xff, "Vendor" },
	{ UICLASS_WIRELESS, UISUBCLASS_RF, UIPROTO_RNDIS, "RNDIS" },
	{ UICLASS_MISC, UISUBCLASS_SYNC, UIPROTO_ACTIVESYNC, "Activesync" }
};

usbd_status
urndis_ctrl_msg(struct urndis_softc *sc, uint8_t rt, uint8_t r,
    uint16_t index, uint16_t value, void *buf, size_t buflen)
{
	usb_device_request_t req;

	req.bmRequestType = rt;
	req.bRequest = r;
	USETW(req.wValue, value);
	USETW(req.wIndex, index);
	USETW(req.wLength, buflen);

	return usbd_do_request(sc->sc_udev, &req, buf);
}

usbd_status
urndis_ctrl_send(struct urndis_softc *sc, void *buf, size_t len)
{
	usbd_status err;

	if (usbd_is_dying(sc->sc_udev))
		return(0);

	err = urndis_ctrl_msg(sc, UT_WRITE_CLASS_INTERFACE, UR_GET_STATUS,
	    sc->sc_ifaceno_ctl, 0, buf, len);

	if (err != USBD_NORMAL_COMPLETION)
		printf("%s: %s\n", DEVNAME(sc), usbd_errstr(err));

	return err;
}

struct rndis_comp_hdr *
urndis_ctrl_recv(struct urndis_softc *sc)
{
#define RNDIS_RESPONSE_LEN 0x400
	struct rndis_comp_hdr	*hdr;
	char			*buf;
	usbd_status		 err;

	buf = malloc(RNDIS_RESPONSE_LEN, M_TEMP, M_WAITOK | M_CANFAIL);
	if (buf == NULL) {
		printf("%s: out of memory\n", DEVNAME(sc));
		return NULL;
	}

	err = urndis_ctrl_msg(sc, UT_READ_CLASS_INTERFACE, UR_CLEAR_FEATURE,
	    sc->sc_ifaceno_ctl, 0, buf, RNDIS_RESPONSE_LEN);

	if (err != USBD_NORMAL_COMPLETION && err != USBD_SHORT_XFER) {
		printf("%s: %s\n", DEVNAME(sc), usbd_errstr(err));
		free(buf, M_TEMP, RNDIS_RESPONSE_LEN);
		return NULL;
	}

	hdr = (struct rndis_comp_hdr *)buf;
	DPRINTF(("%s: urndis_ctrl_recv: type 0x%x len %u\n",
	    DEVNAME(sc),
	    letoh32(hdr->rm_type),
	    letoh32(hdr->rm_len)));

	if (letoh32(hdr->rm_len) > RNDIS_RESPONSE_LEN) {
		printf("%s: ctrl message error: wrong size %u > %u\n",
		    DEVNAME(sc),
		    letoh32(hdr->rm_len),
		    RNDIS_RESPONSE_LEN);
		free(buf, M_TEMP, RNDIS_RESPONSE_LEN);
		return NULL;
	}

	return hdr;
}

u_int32_t
urndis_ctrl_handle(struct urndis_softc *sc, struct rndis_comp_hdr *hdr,
    void **buf, size_t *bufsz)
{
	u_int32_t rval;

	DPRINTF(("%s: urndis_ctrl_handle\n", DEVNAME(sc)));

	if (buf && bufsz) {
		*buf = NULL;
		*bufsz = 0;
	}

	switch (letoh32(hdr->rm_type)) {
		case REMOTE_NDIS_INITIALIZE_CMPLT:
			rval = urndis_ctrl_handle_init(sc, hdr);
			break;

		case REMOTE_NDIS_QUERY_CMPLT:
			rval = urndis_ctrl_handle_query(sc, hdr, buf, bufsz);
			break;

		case REMOTE_NDIS_RESET_CMPLT:
			rval = urndis_ctrl_handle_reset(sc, hdr);
			break;

		case REMOTE_NDIS_KEEPALIVE_CMPLT:
		case REMOTE_NDIS_SET_CMPLT:
			rval = letoh32(hdr->rm_status);
			break;

		case REMOTE_NDIS_INDICATE_STATUS_MSG:
			rval = urndis_ctrl_handle_status(sc, hdr);
			break;

		default:
			printf("%s: ctrl message error: unknown event 0x%x\n",
			    DEVNAME(sc), letoh32(hdr->rm_type));
			rval = RNDIS_STATUS_FAILURE;
	}

	free(hdr, M_TEMP, RNDIS_RESPONSE_LEN);

	return rval;
}

u_int32_t
urndis_ctrl_handle_init(struct urndis_softc *sc,
    const struct rndis_comp_hdr *hdr)
{
	const struct rndis_init_comp	*msg;

	msg = (struct rndis_init_comp *) hdr;

	DPRINTF(("%s: urndis_ctrl_handle_init: len %u rid %u status 0x%x "
	    "ver_major %u ver_minor %u devflags 0x%x medium 0x%x pktmaxcnt %u "
	    "pktmaxsz %u align %u aflistoffset %u aflistsz %u\n",
	    DEVNAME(sc),
	    letoh32(msg->rm_len),
	    letoh32(msg->rm_rid),
	    letoh32(msg->rm_status),
	    letoh32(msg->rm_ver_major),
	    letoh32(msg->rm_ver_minor),
	    letoh32(msg->rm_devflags),
	    letoh32(msg->rm_medium),
	    letoh32(msg->rm_pktmaxcnt),
	    letoh32(msg->rm_pktmaxsz),
	    letoh32(msg->rm_align),
	    letoh32(msg->rm_aflistoffset),
	    letoh32(msg->rm_aflistsz)));

	if (letoh32(msg->rm_status) != RNDIS_STATUS_SUCCESS) {
		printf("%s: init failed 0x%x\n",
		    DEVNAME(sc),
		    letoh32(msg->rm_status));

		return letoh32(msg->rm_status);
	}

	if (letoh32(msg->rm_devflags) != RNDIS_DF_CONNECTIONLESS) {
		printf("%s: wrong device type (current type: 0x%x)\n",
		    DEVNAME(sc),
		    letoh32(msg->rm_devflags));

		return RNDIS_STATUS_FAILURE;
	}

	if (letoh32(msg->rm_medium) != RNDIS_MEDIUM_802_3) {
		printf("%s: medium not 802.3 (current medium: 0x%x)\n",
		    DEVNAME(sc), letoh32(msg->rm_medium));

		return RNDIS_STATUS_FAILURE;
	}

	sc->sc_lim_pktsz = letoh32(msg->rm_pktmaxsz);

	return letoh32(msg->rm_status);
}

u_int32_t
urndis_ctrl_handle_query(struct urndis_softc *sc,
    const struct rndis_comp_hdr *hdr, void **buf, size_t *bufsz)
{
	const struct rndis_query_comp	*msg;

	msg = (struct rndis_query_comp *) hdr;

	DPRINTF(("%s: urndis_ctrl_handle_query: len %u rid %u status 0x%x "
	    "buflen %u bufoff %u\n",
	    DEVNAME(sc),
	    letoh32(msg->rm_len),
	    letoh32(msg->rm_rid),
	    letoh32(msg->rm_status),
	    letoh32(msg->rm_infobuflen),
	    letoh32(msg->rm_infobufoffset)));

	if (buf && bufsz) {
		*buf = NULL;
		*bufsz = 0;
	}

	if (letoh32(msg->rm_status) != RNDIS_STATUS_SUCCESS) {
		printf("%s: query failed 0x%x\n",
		    DEVNAME(sc),
		    letoh32(msg->rm_status));

		return letoh32(msg->rm_status);
	}

	if (letoh32(msg->rm_infobuflen) + letoh32(msg->rm_infobufoffset) +
	    RNDIS_HEADER_OFFSET > letoh32(msg->rm_len)) {
		printf("%s: ctrl message error: invalid query info "
		    "len/offset/end_position(%u/%u/%zu) -> "
		    "go out of buffer limit %u\n",
		    DEVNAME(sc),
		    letoh32(msg->rm_infobuflen),
		    letoh32(msg->rm_infobufoffset),
		    letoh32(msg->rm_infobuflen) +
		    letoh32(msg->rm_infobufoffset) + RNDIS_HEADER_OFFSET,
		    letoh32(msg->rm_len));
		return RNDIS_STATUS_FAILURE;
	}

	if (buf && bufsz) {
		*buf = malloc(letoh32(msg->rm_infobuflen),
		    M_TEMP, M_WAITOK | M_CANFAIL);
		if (*buf == NULL) {
			printf("%s: out of memory\n", DEVNAME(sc));
			return RNDIS_STATUS_FAILURE;
		} else {
			char *p;
			*bufsz = letoh32(msg->rm_infobuflen);

			p = (char *)&msg->rm_rid;
			p += letoh32(msg->rm_infobufoffset);
			memcpy(*buf, p, letoh32(msg->rm_infobuflen));
		}
	}

	return letoh32(msg->rm_status);
}

u_int32_t
urndis_ctrl_handle_reset(struct urndis_softc *sc,
    const struct rndis_comp_hdr *hdr)
{
	const struct rndis_reset_comp	*msg;
	u_int32_t			 rval;

	msg = (struct rndis_reset_comp *) hdr;

	rval = letoh32(msg->rm_status);

	DPRINTF(("%s: urndis_ctrl_handle_reset: len %u status 0x%x "
	    "adrreset %u\n",
	    DEVNAME(sc),
	    letoh32(msg->rm_len),
	    rval,
	    letoh32(msg->rm_adrreset)));

	if (rval != RNDIS_STATUS_SUCCESS) {
		printf("%s: reset failed 0x%x\n", DEVNAME(sc), rval);
		return rval;
	}

	if (letoh32(msg->rm_adrreset) != 0) {
		u_int32_t filter;

		filter = htole32(sc->sc_filter);
		rval = urndis_ctrl_set(sc, OID_GEN_CURRENT_PACKET_FILTER,
		    &filter, sizeof(filter));
		if (rval != RNDIS_STATUS_SUCCESS) {
			printf("%s: unable to reset data filters\n",
			    DEVNAME(sc));
			return rval;
		}
	}

	return rval;
}

u_int32_t
urndis_ctrl_handle_status(struct urndis_softc *sc,
    const struct rndis_comp_hdr *hdr)
{
	const struct rndis_status_msg	*msg;
	u_int32_t			 rval;

	msg = (struct rndis_status_msg *)hdr;

	rval = letoh32(msg->rm_status);

	DPRINTF(("%s: urndis_ctrl_handle_status: len %u status 0x%x "
	    "stbuflen %u\n",
	    DEVNAME(sc),
	    letoh32(msg->rm_len),
	    rval,
	    letoh32(msg->rm_stbuflen)));

	switch (rval) {
		case RNDIS_STATUS_MEDIA_CONNECT:
		case RNDIS_STATUS_MEDIA_DISCONNECT:
		case RNDIS_STATUS_OFFLOAD_CURRENT_CONFIG:
			rval = RNDIS_STATUS_SUCCESS;
			break;

		default:
			printf("%s: status 0x%x\n", DEVNAME(sc), rval);
	}

	return rval;
}

u_int32_t
urndis_ctrl_init(struct urndis_softc *sc)
{
	struct rndis_init_req	*msg;
	u_int32_t		 rval;
	struct rndis_comp_hdr	*hdr;

	msg = malloc(sizeof(*msg), M_TEMP, M_WAITOK | M_CANFAIL);
	if (msg == NULL) {
		printf("%s: out of memory\n", DEVNAME(sc));
		return RNDIS_STATUS_FAILURE;
	}

	msg->rm_type = htole32(REMOTE_NDIS_INITIALIZE_MSG);
	msg->rm_len = htole32(sizeof(*msg));
	msg->rm_rid = htole32(0);
	msg->rm_ver_major = htole32(1);
	msg->rm_ver_minor = htole32(1);
	msg->rm_max_xfersz = htole32(RNDIS_BUFSZ);

	DPRINTF(("%s: urndis_ctrl_init send: type %u len %u rid %u ver_major %u "
	    "ver_minor %u max_xfersz %u\n",
	    DEVNAME(sc),
	    letoh32(msg->rm_type),
	    letoh32(msg->rm_len),
	    letoh32(msg->rm_rid),
	    letoh32(msg->rm_ver_major),
	    letoh32(msg->rm_ver_minor),
	    letoh32(msg->rm_max_xfersz)));

	rval = urndis_ctrl_send(sc, msg, sizeof(*msg));
	free(msg, M_TEMP, sizeof *msg);

	if (rval != RNDIS_STATUS_SUCCESS) {
		printf("%s: init failed\n", DEVNAME(sc));
		return rval;
	}

	if ((hdr = urndis_ctrl_recv(sc)) == NULL) {
		printf("%s: unable to get init response\n", DEVNAME(sc));
		return RNDIS_STATUS_FAILURE;
	}
	rval = urndis_ctrl_handle(sc, hdr, NULL, NULL);

	return rval;
}

u_int32_t
urndis_ctrl_halt(struct urndis_softc *sc)
{
	struct rndis_halt_req	*msg;
	u_int32_t		 rval;

	msg = malloc(sizeof(*msg), M_TEMP, M_WAITOK | M_CANFAIL);
	if (msg == NULL) {
		printf("%s: out of memory\n", DEVNAME(sc));
		return RNDIS_STATUS_FAILURE;
	}

	msg->rm_type = htole32(REMOTE_NDIS_HALT_MSG);
	msg->rm_len = htole32(sizeof(*msg));
	msg->rm_rid = 0;

	DPRINTF(("%s: urndis_ctrl_halt send: type %u len %u rid %u\n",
	    DEVNAME(sc),
	    letoh32(msg->rm_type),
	    letoh32(msg->rm_len),
	    letoh32(msg->rm_rid)));

	rval = urndis_ctrl_send(sc, msg, sizeof(*msg));
	free(msg, M_TEMP, sizeof *msg);

	if (rval != RNDIS_STATUS_SUCCESS)
		printf("%s: halt failed\n", DEVNAME(sc));

	return rval;
}

u_int32_t
urndis_ctrl_query(struct urndis_softc *sc, u_int32_t oid,
    void *qbuf, size_t qlen,
    void **rbuf, size_t *rbufsz)
{
	struct rndis_query_req	*msg;
	u_int32_t		 rval;
	struct rndis_comp_hdr	*hdr;

	msg = malloc(sizeof(*msg) + qlen, M_TEMP, M_WAITOK | M_CANFAIL);
	if (msg == NULL) {
		printf("%s: out of memory\n", DEVNAME(sc));
		return RNDIS_STATUS_FAILURE;
	}

	msg->rm_type = htole32(REMOTE_NDIS_QUERY_MSG);
	msg->rm_len = htole32(sizeof(*msg) + qlen);
	msg->rm_rid = 0; /* XXX */
	msg->rm_oid = htole32(oid);
	msg->rm_infobuflen = htole32(qlen);
	if (qlen != 0) {
		msg->rm_infobufoffset = htole32(20);
		memcpy((char*)msg + 20, qbuf, qlen);
	} else
		msg->rm_infobufoffset = 0;
	msg->rm_devicevchdl = 0;

	DPRINTF(("%s: urndis_ctrl_query send: type %u len %u rid %u oid 0x%x "
	    "infobuflen %u infobufoffset %u devicevchdl %u\n",
	    DEVNAME(sc),
	    letoh32(msg->rm_type),
	    letoh32(msg->rm_len),
	    letoh32(msg->rm_rid),
	    letoh32(msg->rm_oid),
	    letoh32(msg->rm_infobuflen),
	    letoh32(msg->rm_infobufoffset),
	    letoh32(msg->rm_devicevchdl)));

	rval = urndis_ctrl_send(sc, msg, sizeof(*msg));
	free(msg, M_TEMP, sizeof *msg + qlen);

	if (rval != RNDIS_STATUS_SUCCESS) {
		printf("%s: query failed\n", DEVNAME(sc));
		return rval;
	}

	if ((hdr = urndis_ctrl_recv(sc)) == NULL) {
		printf("%s: unable to get query response\n", DEVNAME(sc));
		return RNDIS_STATUS_FAILURE;
	}
	rval = urndis_ctrl_handle(sc, hdr, rbuf, rbufsz);

	return rval;
}

u_int32_t
urndis_ctrl_set(struct urndis_softc *sc, u_int32_t oid, void *buf, size_t len)
{
	struct rndis_set_req	*msg;
	u_int32_t		 rval;
	struct rndis_comp_hdr	*hdr;

	msg = malloc(sizeof(*msg) + len, M_TEMP, M_WAITOK | M_CANFAIL);
	if (msg == NULL) {
		printf("%s: out of memory\n", DEVNAME(sc));
		return RNDIS_STATUS_FAILURE;
	}

	msg->rm_type = htole32(REMOTE_NDIS_SET_MSG);
	msg->rm_len = htole32(sizeof(*msg) + len);
	msg->rm_rid = 0; /* XXX */
	msg->rm_oid = htole32(oid);
	msg->rm_infobuflen = htole32(len);
	if (len != 0) {
		msg->rm_infobufoffset = htole32(20);
		memcpy((char*)msg + 28, buf, len);
	} else
		msg->rm_infobufoffset = 0;
	msg->rm_devicevchdl = 0;

	DPRINTF(("%s: urndis_ctrl_set send: type %u len %u rid %u oid 0x%x "
	    "infobuflen %u infobufoffset %u devicevchdl %u\n",
	    DEVNAME(sc),
	    letoh32(msg->rm_type),
	    letoh32(msg->rm_len),
	    letoh32(msg->rm_rid),
	    letoh32(msg->rm_oid),
	    letoh32(msg->rm_infobuflen),
	    letoh32(msg->rm_infobufoffset),
	    letoh32(msg->rm_devicevchdl)));

	rval = urndis_ctrl_send(sc, msg, sizeof(*msg) + len);
	free(msg, M_TEMP, sizeof *msg + len);

	if (rval != RNDIS_STATUS_SUCCESS) {
		printf("%s: set failed\n", DEVNAME(sc));
		return rval;
	}

	if ((hdr = urndis_ctrl_recv(sc)) == NULL) {
		printf("%s: unable to get set response\n", DEVNAME(sc));
		return RNDIS_STATUS_FAILURE;
	}
	rval = urndis_ctrl_handle(sc, hdr, NULL, NULL);
	if (rval != RNDIS_STATUS_SUCCESS)
		printf("%s: set failed 0x%x\n", DEVNAME(sc), rval);

	return rval;
}

u_int32_t
urndis_ctrl_set_param(struct urndis_softc *sc,
    const char *name,
    u_int32_t type,
    void *buf,
    size_t len)
{
	struct rndis_set_parameter	*param;
	u_int32_t			 rval;
	size_t				 namelen, tlen;

	if (name)
		namelen = strlen(name);
	else
		namelen = 0;
	tlen = sizeof(*param) + len + namelen;
	param = malloc(tlen, M_TEMP, M_WAITOK | M_CANFAIL);
	if (param == NULL) {
		printf("%s: out of memory\n", DEVNAME(sc));
		return RNDIS_STATUS_FAILURE;
	}

	param->rm_namelen = htole32(namelen);
	param->rm_valuelen = htole32(len);
	param->rm_type = htole32(type);
	if (namelen != 0) {
		param->rm_nameoffset = htole32(20);
		memcpy(param + 20, name, namelen);
	} else
		param->rm_nameoffset = 0;
	if (len != 0) {
		param->rm_valueoffset = htole32(20 + namelen);
		memcpy(param + 20 + namelen, buf, len);
	} else
		param->rm_valueoffset = 0;

	DPRINTF(("%s: urndis_ctrl_set_param send: nameoffset %u namelen %u "
	    "type 0x%x valueoffset %u valuelen %u\n",
	    DEVNAME(sc),
	    letoh32(param->rm_nameoffset),
	    letoh32(param->rm_namelen),
	    letoh32(param->rm_type),
	    letoh32(param->rm_valueoffset),
	    letoh32(param->rm_valuelen)));

	rval = urndis_ctrl_set(sc, OID_GEN_RNDIS_CONFIG_PARAMETER, param, tlen);
	free(param, M_TEMP, tlen);
	if (rval != RNDIS_STATUS_SUCCESS)
		printf("%s: set param failed 0x%x\n", DEVNAME(sc), rval);

	return rval;
}

#if 0
/* XXX : adrreset, get it from response */
u_int32_t
urndis_ctrl_reset(struct urndis_softc *sc)
{
	struct rndis_reset_req		*reset;
	u_int32_t			 rval;
	struct rndis_comp_hdr		*hdr;

	reset = malloc(sizeof(*reset), M_TEMP, M_WAITOK | M_CANFAIL);
	if (reset == NULL) {
		printf("%s: out of memory\n", DEVNAME(sc));
		return RNDIS_STATUS_FAILURE;
	}

	reset->rm_type = htole32(REMOTE_NDIS_RESET_MSG);
	reset->rm_len = htole32(sizeof(*reset));
	reset->rm_rid = 0; /* XXX rm_rid == reserved ... remove ? */

	DPRINTF(("%s: urndis_ctrl_reset send: type %u len %u rid %u\n",
	    DEVNAME(sc),
	    letoh32(reset->rm_type),
	    letoh32(reset->rm_len),
	    letoh32(reset->rm_rid)));

	rval = urndis_ctrl_send(sc, reset, sizeof(*reset));
	free(reset, M_TEMP, sizeof *reset);

	if (rval != RNDIS_STATUS_SUCCESS) {
		printf("%s: reset failed\n", DEVNAME(sc));
		return rval;
	}

	if ((hdr = urndis_ctrl_recv(sc)) == NULL) {
		printf("%s: unable to get reset response\n", DEVNAME(sc));
		return RNDIS_STATUS_FAILURE;
	}
	rval = urndis_ctrl_handle(sc, hdr, NULL, NULL);

	return rval;
}

u_int32_t
urndis_ctrl_keepalive(struct urndis_softc *sc)
{
	struct rndis_keepalive_req	*keep;
	u_int32_t			 rval;
	struct rndis_comp_hdr		*hdr;

	keep = malloc(sizeof(*keep), M_TEMP, M_WAITOK | M_CANFAIL);
	if (keep == NULL) {
		printf("%s: out of memory\n", DEVNAME(sc));
		return RNDIS_STATUS_FAILURE;
	}

	keep->rm_type = htole32(REMOTE_NDIS_KEEPALIVE_MSG);
	keep->rm_len = htole32(sizeof(*keep));
	keep->rm_rid = 0; /* XXX rm_rid == reserved ... remove ? */

	DPRINTF(("%s: urndis_ctrl_keepalive: type %u len %u rid %u\n",
	    DEVNAME(sc),
	    letoh32(keep->rm_type),
	    letoh32(keep->rm_len),
	    letoh32(keep->rm_rid)));

	rval = urndis_ctrl_send(sc, keep, sizeof(*keep));
	free(keep, M_TEMP, sizeof *keep);

	if (rval != RNDIS_STATUS_SUCCESS) {
		printf("%s: keepalive failed\n", DEVNAME(sc));
		return rval;
	}

	if ((hdr = urndis_ctrl_recv(sc)) == NULL) {
		printf("%s: unable to get keepalive response\n", DEVNAME(sc));
		return RNDIS_STATUS_FAILURE;
	}
	rval = urndis_ctrl_handle(sc, hdr, NULL, NULL);
	if (rval != RNDIS_STATUS_SUCCESS) {
		printf("%s: keepalive failed 0x%x\n", DEVNAME(sc), rval);
		urndis_ctrl_reset(sc);
	}

	return rval;
}
#endif

int
urndis_encap(struct urndis_softc *sc, struct mbuf *m, int idx)
{
	struct urndis_chain		*c;
	usbd_status			 err;
	struct rndis_packet_msg		*msg;

	c = &sc->sc_data.sc_tx_chain[idx];

	msg = (struct rndis_packet_msg *)c->sc_buf;

	memset(msg, 0, sizeof(*msg));
	msg->rm_type = htole32(REMOTE_NDIS_PACKET_MSG);
	msg->rm_len = htole32(sizeof(*msg) + m->m_pkthdr.len);

	msg->rm_dataoffset = htole32(RNDIS_DATA_OFFSET);
	msg->rm_datalen = htole32(m->m_pkthdr.len);

	m_copydata(m, 0, m->m_pkthdr.len,
	    ((char*)msg + RNDIS_DATA_OFFSET + RNDIS_HEADER_OFFSET));

	DPRINTF(("%s: urndis_encap type 0x%x len %u data(off %u len %u)\n",
	    DEVNAME(sc),
	    letoh32(msg->rm_type),
	    letoh32(msg->rm_len),
	    letoh32(msg->rm_dataoffset),
	    letoh32(msg->rm_datalen)));

	c->sc_mbuf = m;

	usbd_setup_xfer(c->sc_xfer, sc->sc_bulkout_pipe, c, c->sc_buf,
	    letoh32(msg->rm_len), USBD_FORCE_SHORT_XFER | USBD_NO_COPY, 10000,
	    urndis_txeof);

	/* Transmit */
	err = usbd_transfer(c->sc_xfer);
	if (err != USBD_IN_PROGRESS) {
		c->sc_mbuf = NULL;
		urndis_stop(sc);
		return(EIO);
	}

	sc->sc_data.sc_tx_cnt++;

	return(0);
}

void
urndis_decap(struct urndis_softc *sc, struct urndis_chain *c, u_int32_t len)
{
	struct mbuf		*m;
	struct mbuf_list	 ml = MBUF_LIST_INITIALIZER();
	struct rndis_packet_msg	*msg;
	struct ifnet		*ifp;
	int			 s;
	int			 offset;

	ifp = GET_IFP(sc);
	offset = 0;

	while (len > 1) {
		msg = (struct rndis_packet_msg *)((char*)c->sc_buf + offset);
		m = c->sc_mbuf;

		DPRINTF(("%s: urndis_decap buffer size left %u\n", DEVNAME(sc),
		    len));

		if (len < sizeof(*msg)) {
			printf("%s: urndis_decap invalid buffer len %u < "
			    "minimum header %zu\n",
			    DEVNAME(sc),
			    len,
			    sizeof(*msg));
			break;
		}

		DPRINTF(("%s: urndis_decap len %u data(off:%u len:%u) "
		    "oobdata(off:%u len:%u nb:%u) perpacket(off:%u len:%u)\n",
		    DEVNAME(sc),
		    letoh32(msg->rm_len),
		    letoh32(msg->rm_dataoffset),
		    letoh32(msg->rm_datalen),
		    letoh32(msg->rm_oobdataoffset),
		    letoh32(msg->rm_oobdatalen),
		    letoh32(msg->rm_oobdataelements),
		    letoh32(msg->rm_pktinfooffset),
		    letoh32(msg->rm_pktinfooffset)));

		if (letoh32(msg->rm_type) != REMOTE_NDIS_PACKET_MSG) {
			printf("%s: urndis_decap invalid type 0x%x != 0x%x\n",
			    DEVNAME(sc),
			    letoh32(msg->rm_type),
			    REMOTE_NDIS_PACKET_MSG);
			break;
		}
		if (letoh32(msg->rm_len) < sizeof(*msg)) {
			printf("%s: urndis_decap invalid msg len %u < %zu\n",
			    DEVNAME(sc),
			    letoh32(msg->rm_len),
			    sizeof(*msg));
			break;
		}
		if (letoh32(msg->rm_len) > len) {
			printf("%s: urndis_decap invalid msg len %u > buffer "
			    "len %u\n",
			    DEVNAME(sc),
			    letoh32(msg->rm_len),
			    len);
			break;
		}

		if (letoh32(msg->rm_dataoffset) +
		    letoh32(msg->rm_datalen) + RNDIS_HEADER_OFFSET
			> letoh32(msg->rm_len)) {
			printf("%s: urndis_decap invalid data "
			    "len/offset/end_position(%u/%u/%zu) -> "
			    "go out of receive buffer limit %u\n",
			    DEVNAME(sc),
			    letoh32(msg->rm_datalen),
			    letoh32(msg->rm_dataoffset),
			    letoh32(msg->rm_dataoffset) +
			    letoh32(msg->rm_datalen) + RNDIS_HEADER_OFFSET,
			    letoh32(msg->rm_len));
			break;
		}

		if (letoh32(msg->rm_datalen) < sizeof(struct ether_header)) {
			ifp->if_ierrors++;
			DPRINTF(("%s: urndis_decap invalid ethernet size "
			    "%u < %zu\n",
			    DEVNAME(sc),
			    letoh32(msg->rm_datalen),
			    sizeof(struct ether_header)));
			break;
		}

		memcpy(mtod(m, char*),
		    ((char*)&msg->rm_dataoffset + letoh32(msg->rm_dataoffset)),
		    letoh32(msg->rm_datalen));
		m->m_pkthdr.len = m->m_len = letoh32(msg->rm_datalen);

		if (urndis_newbuf(sc, c) == ENOBUFS) {
			ifp->if_ierrors++;
		} else {
			ml_enqueue(&ml, m);
		}

		offset += letoh32(msg->rm_len);
		len -= letoh32(msg->rm_len);
	}
	if (ml_empty(&ml))
		return;

	s = splnet();
	if_input(ifp, &ml);
	splx(s);
}

int
urndis_newbuf(struct urndis_softc *sc, struct urndis_chain *c)
{
	struct mbuf *m_new = NULL;

	MGETHDR(m_new, M_DONTWAIT, MT_DATA);
	if (m_new == NULL) {
		printf("%s: no memory for rx list -- packet dropped!\n",
		    DEVNAME(sc));
		return (ENOBUFS);
	}
	MCLGET(m_new, M_DONTWAIT);
	if (!(m_new->m_flags & M_EXT)) {
		printf("%s: no memory for rx list -- packet dropped!\n",
		    DEVNAME(sc));
		m_freem(m_new);
		return (ENOBUFS);
	}
	m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;

	m_adj(m_new, ETHER_ALIGN);
	c->sc_mbuf = m_new;
	return (0);
}

int
urndis_rx_list_init(struct urndis_softc *sc)
{
	struct urndis_cdata	*cd;
	struct urndis_chain	*c;
	int			 i;

	cd = &sc->sc_data;
	for (i = 0; i < RNDIS_RX_LIST_CNT; i++) {
		c = &cd->sc_rx_chain[i];
		c->sc_softc = sc;
		c->sc_idx = i;

		if (urndis_newbuf(sc, c) == ENOBUFS)
			return (ENOBUFS);

		if (c->sc_xfer == NULL) {
			c->sc_xfer = usbd_alloc_xfer(sc->sc_udev);
			if (c->sc_xfer == NULL)
				return (ENOBUFS);
			c->sc_buf = usbd_alloc_buffer(c->sc_xfer,
			    RNDIS_BUFSZ);
			if (c->sc_buf == NULL)
				return (ENOBUFS);
		}
	}

	return (0);
}

int
urndis_tx_list_init(struct urndis_softc *sc)
{
	struct urndis_cdata	*cd;
	struct urndis_chain	*c;
	int			 i;

	cd = &sc->sc_data;
	for (i = 0; i < RNDIS_TX_LIST_CNT; i++) {
		c = &cd->sc_tx_chain[i];
		c->sc_softc = sc;
		c->sc_idx = i;
		c->sc_mbuf = NULL;
		if (c->sc_xfer == NULL) {
			c->sc_xfer = usbd_alloc_xfer(sc->sc_udev);
			if (c->sc_xfer == NULL)
				return (ENOBUFS);
			c->sc_buf = usbd_alloc_buffer(c->sc_xfer,
			    RNDIS_BUFSZ);
			if (c->sc_buf == NULL)
				return (ENOBUFS);
		}
	}
	return (0);
}

int
urndis_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct urndis_softc	*sc = ifp->if_softc;
	int			 s, error = 0;

	if (usbd_is_dying(sc->sc_udev))
		return ENXIO;

	s = splnet();

	switch(command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			urndis_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				urndis_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				urndis_stop(sc);
		}
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, command, data);
		break;
	}

	if (error == ENETRESET)
		error = 0;

	splx(s);
	return (error);
}

#if 0
void
urndis_watchdog(struct ifnet *ifp)
{
	struct urndis_softc *sc;

	sc = ifp->if_softc;

	if (usbd_is_dying(sc->sc_udev))
		return;

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", DEVNAME(sc));

	urndis_ctrl_keepalive(sc);
}
#endif

void
urndis_init(struct urndis_softc *sc)
{
	struct ifnet		*ifp = GET_IFP(sc);
	int			 i, s;
	usbd_status		 err;

	if (urndis_ctrl_init(sc) != RNDIS_STATUS_SUCCESS)
		return;

	s = splnet();

	if (urndis_tx_list_init(sc) == ENOBUFS) {
		printf("%s: tx list init failed\n",
		    DEVNAME(sc));
		splx(s);
		return;
	}

	if (urndis_rx_list_init(sc) == ENOBUFS) {
		printf("%s: rx list init failed\n",
		    DEVNAME(sc));
		splx(s);
		return;
	}

	err = usbd_open_pipe(sc->sc_iface_data, sc->sc_bulkin_no,
	    USBD_EXCLUSIVE_USE, &sc->sc_bulkin_pipe);
	if (err) {
		printf("%s: open rx pipe failed: %s\n", DEVNAME(sc),
		    usbd_errstr(err));
		splx(s);
		return;
	}

	err = usbd_open_pipe(sc->sc_iface_data, sc->sc_bulkout_no,
	    USBD_EXCLUSIVE_USE, &sc->sc_bulkout_pipe);
	if (err) {
		printf("%s: open tx pipe failed: %s\n", DEVNAME(sc),
		    usbd_errstr(err));
		splx(s);
		return;
	}

	for (i = 0; i < RNDIS_RX_LIST_CNT; i++) {
		struct urndis_chain *c;

		c = &sc->sc_data.sc_rx_chain[i];
		usbd_setup_xfer(c->sc_xfer, sc->sc_bulkin_pipe, c,
		    c->sc_buf, RNDIS_BUFSZ,
		    USBD_SHORT_XFER_OK | USBD_NO_COPY,
		    USBD_NO_TIMEOUT, urndis_rxeof);
		usbd_transfer(c->sc_xfer);
	}

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);
}

void
urndis_stop(struct urndis_softc *sc)
{
	usbd_status	 err;
	struct ifnet	*ifp;
	int		 i;

	ifp = GET_IFP(sc);
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (sc->sc_bulkin_pipe != NULL) {
		err = usbd_close_pipe(sc->sc_bulkin_pipe);
		if (err)
			printf("%s: close rx pipe failed: %s\n",
			    DEVNAME(sc), usbd_errstr(err));
		sc->sc_bulkin_pipe = NULL;
	}

	if (sc->sc_bulkout_pipe != NULL) {
		err = usbd_close_pipe(sc->sc_bulkout_pipe);
		if (err)
			printf("%s: close tx pipe failed: %s\n",
			    DEVNAME(sc), usbd_errstr(err));
		sc->sc_bulkout_pipe = NULL;
	}

	for (i = 0; i < RNDIS_RX_LIST_CNT; i++) {
		if (sc->sc_data.sc_rx_chain[i].sc_mbuf != NULL) {
			m_freem(sc->sc_data.sc_rx_chain[i].sc_mbuf);
			sc->sc_data.sc_rx_chain[i].sc_mbuf = NULL;
		}
		if (sc->sc_data.sc_rx_chain[i].sc_xfer != NULL) {
			usbd_free_xfer(sc->sc_data.sc_rx_chain[i].sc_xfer);
			sc->sc_data.sc_rx_chain[i].sc_xfer = NULL;
		}
	}

	for (i = 0; i < RNDIS_TX_LIST_CNT; i++) {
		if (sc->sc_data.sc_tx_chain[i].sc_mbuf != NULL) {
			m_freem(sc->sc_data.sc_tx_chain[i].sc_mbuf);
			sc->sc_data.sc_tx_chain[i].sc_mbuf = NULL;
		}
		if (sc->sc_data.sc_tx_chain[i].sc_xfer != NULL) {
			usbd_free_xfer(sc->sc_data.sc_tx_chain[i].sc_xfer);
			sc->sc_data.sc_tx_chain[i].sc_xfer = NULL;
		}
	}
}

void
urndis_start(struct ifnet *ifp)
{
	struct urndis_softc	*sc;
	struct mbuf		*m_head = NULL;

	sc = ifp->if_softc;

	if (usbd_is_dying(sc->sc_udev) || ifq_is_oactive(&ifp->if_snd))
		return;

	m_head = ifq_dequeue(&ifp->if_snd);
	if (m_head == NULL)
		return;

	if (urndis_encap(sc, m_head, 0)) {
		m_freem(m_head);
		ifq_set_oactive(&ifp->if_snd);
		return;
	}

	/*
	 * If there's a BPF listener, bounce a copy of this frame
	 * to him.
	 */
#if NBPFILTER > 0
	if (ifp->if_bpf)
		bpf_mtap(ifp->if_bpf, m_head, BPF_DIRECTION_OUT);
#endif

	ifq_set_oactive(&ifp->if_snd);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;

	return;
}

void
urndis_rxeof(struct usbd_xfer *xfer,
    void *priv,
    usbd_status status)
{
	struct urndis_chain	*c;
	struct urndis_softc	*sc;
	struct ifnet		*ifp;
	u_int32_t		 total_len;

	c = priv;
	sc = c->sc_softc;
	ifp = GET_IFP(sc);
	total_len = 0;

	if (usbd_is_dying(sc->sc_udev) || !(ifp->if_flags & IFF_RUNNING))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if (usbd_ratecheck(&sc->sc_rx_notice)) {
			DPRINTF(("%s: usb errors on rx: %s\n",
			    DEVNAME(sc), usbd_errstr(status)));
		}
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_bulkin_pipe);

		ifp->if_ierrors++;
		goto done;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);
	urndis_decap(sc, c, total_len);

done:
	/* Setup new transfer. */
	usbd_setup_xfer(c->sc_xfer, sc->sc_bulkin_pipe, c, c->sc_buf,
	    RNDIS_BUFSZ, USBD_SHORT_XFER_OK | USBD_NO_COPY, USBD_NO_TIMEOUT,
	    urndis_rxeof);
	usbd_transfer(c->sc_xfer);
}

void
urndis_txeof(struct usbd_xfer *xfer,
    void *priv,
    usbd_status status)
{
	struct urndis_chain	*c;
	struct urndis_softc	*sc;
	struct ifnet		*ifp;
	usbd_status		 err;
	int			 s;

	c = priv;
	sc = c->sc_softc;
	ifp = GET_IFP(sc);

	DPRINTF(("%s: urndis_txeof\n", DEVNAME(sc)));

	if (usbd_is_dying(sc->sc_udev))
		return;

	s = splnet();

	ifp->if_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			splx(s);
			return;
		}
		ifp->if_oerrors++;
		DPRINTF(("%s: usb error on tx: %s\n", DEVNAME(sc),
		    usbd_errstr(status)));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->sc_bulkout_pipe);
		splx(s);
		return;
	}

	usbd_get_xfer_status(c->sc_xfer, NULL, NULL, NULL, &err);

	if (c->sc_mbuf != NULL) {
		m_freem(c->sc_mbuf);
		c->sc_mbuf = NULL;
	}

	if (err)
		ifp->if_oerrors++;

	if (ifq_empty(&ifp->if_snd) == 0)
		urndis_start(ifp);

	splx(s);
}

const struct urndis_class *
urndis_lookup(usb_interface_descriptor_t *id)
{
	const struct urndis_class	*uc;
	int				 i;

	uc = urndis_class;
	for (i = 0; i < nitems(urndis_class); i++, uc++) {
		if (uc->class == id->bInterfaceClass &&
		    uc->subclass == id->bInterfaceSubClass &&
		    uc->protocol == id->bInterfaceProtocol)
			return (uc);
	}
	return (NULL);
}

int
urndis_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg		*uaa = aux;
	usb_interface_descriptor_t	*id;

	/* Advertises both RNDIS and CDC Ethernet, but RNDIS doesn't work. */
	if (uaa->vendor == USB_VENDOR_FUJITSUCOMP &&
	    uaa->product == USB_PRODUCT_FUJITSUCOMP_VIRTETH)
		return (UMATCH_NONE);

	if (!uaa->iface)
		return (UMATCH_NONE);

	id = usbd_get_interface_descriptor(uaa->iface);
	if (id == NULL)
		return (UMATCH_NONE);

	return (urndis_lookup(id) ?
	    UMATCH_IFACECLASS_IFACESUBCLASS_IFACEPROTO : UMATCH_NONE);
}

void
urndis_attach(struct device *parent, struct device *self, void *aux)
{
	const struct urndis_class	*uc;
	struct urndis_softc		*sc;
	struct usb_attach_arg		*uaa;
	struct ifnet			*ifp;
	usb_interface_descriptor_t	*id;
	usb_endpoint_descriptor_t	*ed;
	usb_config_descriptor_t		*cd;
	int				 i, j, altcnt;
	int				 s;
	u_char				 eaddr[ETHER_ADDR_LEN];
	void				*buf;
	size_t				 bufsz;
	u_int32_t			 filter;

	sc = (void *)self;
	uaa = aux;

	sc->sc_attached = 0;
	sc->sc_udev = uaa->device;
	id = usbd_get_interface_descriptor(uaa->iface);
	sc->sc_ifaceno_ctl = id->bInterfaceNumber;

	for (i = 0; i < uaa->nifaces; i++) {
		if (usbd_iface_claimed(sc->sc_udev, i))
			continue;

		if (uaa->ifaces[i] != uaa->iface) {
			sc->sc_iface_data = uaa->ifaces[i];
			usbd_claim_iface(sc->sc_udev, i);
			break;
		}
	}

	if (sc->sc_iface_data == NULL) {
		printf("%s: no data interface\n", DEVNAME(sc));
		return;
	}

	uc = urndis_lookup(id);
	printf("%s: using %s", DEVNAME(sc), uc->typestr);

	id = usbd_get_interface_descriptor(sc->sc_iface_data);
	cd = usbd_get_config_descriptor(sc->sc_udev);
	altcnt = usbd_get_no_alts(cd, id->bInterfaceNumber);

	for (j = 0; j < altcnt; j++) {
		if (usbd_set_interface(sc->sc_iface_data, j)) {
			printf(": interface alternate setting %u failed\n", j);
			return;
		}
		/* Find endpoints. */
		id = usbd_get_interface_descriptor(sc->sc_iface_data);
		sc->sc_bulkin_no = sc->sc_bulkout_no = -1;
		for (i = 0; i < id->bNumEndpoints; i++) {
			ed = usbd_interface2endpoint_descriptor(
			    sc->sc_iface_data, i);
			if (!ed) {
				printf(": no descriptor for bulk endpoint "
				    "%u\n", i);
				return;
			}
			if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
			    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
				sc->sc_bulkin_no = ed->bEndpointAddress;
			}
			else if (
			    UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
				sc->sc_bulkout_no = ed->bEndpointAddress;
			}
		}

		if (sc->sc_bulkin_no != -1 && sc->sc_bulkout_no != -1) {
			DPRINTF(("%s: in=0x%x, out=0x%x\n",
			    DEVNAME(sc),
			    sc->sc_bulkin_no,
			    sc->sc_bulkout_no));
			goto found;
		}
	}

	if (sc->sc_bulkin_no == -1)
		printf(": could not find data bulk in\n");
	if (sc->sc_bulkout_no == -1 )
		printf(": could not find data bulk out\n");
	return;

	found:

	ifp = GET_IFP(sc);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_start = urndis_start;
	ifp->if_ioctl = urndis_ioctl;
#if 0
	ifp->if_watchdog = urndis_watchdog;
#endif

	strlcpy(ifp->if_xname, DEVNAME(sc), IFNAMSIZ);

	s = splnet();

	if (urndis_ctrl_query(sc, OID_802_3_PERMANENT_ADDRESS, NULL, 0,
	    &buf, &bufsz) != RNDIS_STATUS_SUCCESS) {
		printf(": unable to get hardware address\n");
		splx(s);
		return;
	}

	if (bufsz == ETHER_ADDR_LEN) {
		memcpy(eaddr, buf, ETHER_ADDR_LEN);
		printf(", address %s\n", ether_sprintf(eaddr));
		free(buf, M_TEMP, bufsz);
	} else {
		printf(", invalid address\n");
		free(buf, M_TEMP, bufsz);
		splx(s);
		return;
	}

	/* Initialize packet filter */
	sc->sc_filter = NDIS_PACKET_TYPE_BROADCAST;
	sc->sc_filter |= NDIS_PACKET_TYPE_ALL_MULTICAST;
	filter = htole32(sc->sc_filter);
	if (urndis_ctrl_set(sc, OID_GEN_CURRENT_PACKET_FILTER, &filter,
	    sizeof(filter)) != RNDIS_STATUS_SUCCESS) {
		printf("%s: unable to set data filters\n", DEVNAME(sc));
		splx(s);
		return;
	}

	bcopy(eaddr, (char *)&sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN);

	if_attach(ifp);
	ether_ifattach(ifp);
	sc->sc_attached = 1;

	splx(s);
}

int
urndis_detach(struct device *self, int flags)
{
	struct urndis_softc	*sc;
	struct ifnet		*ifp;
	int			 s;

	sc = (void*)self;

	DPRINTF(("urndis_detach: %s flags %u\n", DEVNAME(sc),
	    flags));

	if (!sc->sc_attached)
		return 0;

	s = splusb();

	ifp = GET_IFP(sc);

	if (ifp->if_softc != NULL) {
		ether_ifdetach(ifp);
		if_detach(ifp);
	}

	urndis_stop(sc);
	sc->sc_attached = 0;

	splx(s);

	return 0;
}
