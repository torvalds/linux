/*-
 * Copyright (c) 2017 Microsoft Corp.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/taskqueue.h>
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/syscallsubr.h>
#include <sys/sysproto.h>
#include <sys/systm.h>
#include <sys/mutex.h>

#include <sys/kbio.h>
#include <dev/kbd/kbdreg.h>

#include <dev/hyperv/include/hyperv.h>
#include <dev/hyperv/utilities/hv_utilreg.h>
#include <dev/hyperv/utilities/vmbus_icreg.h>
#include <dev/hyperv/utilities/vmbus_icvar.h>
#include <dev/hyperv/include/vmbus_xact.h>

#include "dev/hyperv/input/hv_kbdc.h"
#include "vmbus_if.h"

#define HV_KBD_VER_MAJOR	(1)
#define HV_KBD_VER_MINOR	(0)

#define HV_KBD_VER		(HV_KBD_VER_MINOR | (HV_KBD_VER_MAJOR) << 16)

#define HV_KBD_PROTO_ACCEPTED	(1)

#define HV_BUFF_SIZE		(4*PAGE_SIZE)
#define HV_KBD_RINGBUFF_SEND_SZ	(10*PAGE_SIZE)
#define HV_KBD_RINGBUFF_RECV_SZ (10*PAGE_SIZE)

enum hv_kbd_msg_type_t {
	HV_KBD_PROTO_REQUEST        = 1,
	HV_KBD_PROTO_RESPONSE       = 2,
	HV_KBD_PROTO_EVENT          = 3,
	HV_KBD_PROTO_LED_INDICATORS = 4,
};

typedef struct hv_kbd_msg_hdr_t {
	uint32_t type;
} hv_kbd_msg_hdr;

typedef struct hv_kbd_msg_t {
	hv_kbd_msg_hdr hdr;
	char data[];
} hv_kbd_msg;

typedef struct hv_kbd_proto_req_t {
	hv_kbd_msg_hdr	hdr;
	uint32_t	ver;
} hv_kbd_proto_req;

typedef struct hv_kbd_proto_resp_t {
	hv_kbd_msg_hdr  hdr;
	uint32_t	status;
} hv_kbd_proto_resp;

#define HV_KBD_PROTO_REQ_SZ	(sizeof(hv_kbd_proto_req))
#define HV_KBD_PROTO_RESP_SZ	(sizeof(hv_kbd_proto_resp))

/**
 * the struct in win host:
 * typedef struct _HK_MESSAGE_KEYSTROKE
 * {
 *     HK_MESSAGE_HEADER Header;
 *     UINT16 MakeCode;
 *     UINT32 IsUnicode:1;
 *     UINT32 IsBreak:1;
 *     UINT32 IsE0:1;
 *     UINT32 IsE1:1;
 *     UINT32 Reserved:28;
 * } HK_MESSAGE_KEYSTROKE
 */
typedef struct hv_kbd_keystroke_t {
	hv_kbd_msg_hdr  hdr;
	keystroke	ks;
} hv_kbd_keystroke;

static const struct vmbus_ic_desc vmbus_kbd_descs[] = {
	{
		.ic_guid = { .hv_guid = {
		    0x6d, 0xad, 0x12, 0xf9, 0x17, 0x2b, 0xea, 0x48,
		    0xbd, 0x65, 0xf9, 0x27, 0xa6, 0x1c, 0x76,  0x84} },
		.ic_desc = "Hyper-V KBD"
	},
	VMBUS_IC_DESC_END
};

static int hv_kbd_attach(device_t dev);
static int hv_kbd_detach(device_t dev);

/**
 * return 1 if producer is ready
 */
int
hv_kbd_prod_is_ready(hv_kbd_sc *sc)
{
	int ret;
	mtx_lock(&sc->ks_mtx);
	ret = !STAILQ_EMPTY(&sc->ks_queue);
	mtx_unlock(&sc->ks_mtx);
	return (ret);
}

int
hv_kbd_produce_ks(hv_kbd_sc *sc, const keystroke *ks)
{
	int ret = 0;
	keystroke_info *ksi;
	mtx_lock(&sc->ks_mtx);
	if (LIST_EMPTY(&sc->ks_free_list)) {
		DEBUG_HVSC(sc, "NO buffer!\n");
		ret = 1;
	} else {
		ksi = LIST_FIRST(&sc->ks_free_list);
		LIST_REMOVE(ksi, link);
		ksi->ks = *ks;
		STAILQ_INSERT_TAIL(&sc->ks_queue, ksi, slink);
	}
	mtx_unlock(&sc->ks_mtx);
	return (ret);
}

/**
 * return 0 if successfully get the 1st item of queue without removing it
 */
int
hv_kbd_fetch_top(hv_kbd_sc *sc, keystroke *result)
{
	int ret = 0;
	keystroke_info *ksi = NULL;
	mtx_lock(&sc->ks_mtx);
	if (STAILQ_EMPTY(&sc->ks_queue)) {
		DEBUG_HVSC(sc, "Empty queue!\n");
		ret = 1;
	} else {
		ksi = STAILQ_FIRST(&sc->ks_queue);
		*result = ksi->ks;
	}
	mtx_unlock(&sc->ks_mtx);
	return (ret);
}

/**
 * return 0 if successfully removing the top item
 */
int
hv_kbd_remove_top(hv_kbd_sc *sc)
{
	int ret = 0;
	keystroke_info *ksi = NULL;
	mtx_lock(&sc->ks_mtx);
	if (STAILQ_EMPTY(&sc->ks_queue)) {
		DEBUG_HVSC(sc, "Empty queue!\n");
		ret = 1;
	} else {
		ksi = STAILQ_FIRST(&sc->ks_queue);
		STAILQ_REMOVE_HEAD(&sc->ks_queue, slink);
		LIST_INSERT_HEAD(&sc->ks_free_list, ksi, link);
	}
	mtx_unlock(&sc->ks_mtx);
	return (ret);
}

/**
 * return 0 if successfully modify the 1st item of queue
 */
int
hv_kbd_modify_top(hv_kbd_sc *sc, keystroke *top)
{
	int ret = 0;
	keystroke_info *ksi = NULL;
	mtx_lock(&sc->ks_mtx);
	if (STAILQ_EMPTY(&sc->ks_queue)) {
		DEBUG_HVSC(sc, "Empty queue!\n");
		ret = 1;
	} else {
		ksi = STAILQ_FIRST(&sc->ks_queue);
		ksi->ks = *top;
	}
	mtx_unlock(&sc->ks_mtx);
	return (ret);
}

static int
hv_kbd_probe(device_t dev)
{
	device_t bus = device_get_parent(dev);
	const struct vmbus_ic_desc *d;

	if (resource_disabled(device_get_name(dev), 0))
		return (ENXIO);

	for (d = vmbus_kbd_descs; d->ic_desc != NULL; ++d) {
		if (VMBUS_PROBE_GUID(bus, dev, &d->ic_guid) == 0) {
			device_set_desc(dev, d->ic_desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static void
hv_kbd_on_response(hv_kbd_sc *sc, struct vmbus_chanpkt_hdr *pkt)
{
	struct vmbus_xact_ctx *xact = sc->hs_xact_ctx;
	if (xact != NULL) {
		DEBUG_HVSC(sc, "hvkbd is ready\n");
		vmbus_xact_ctx_wakeup(xact, VMBUS_CHANPKT_CONST_DATA(pkt),
		    VMBUS_CHANPKT_DATALEN(pkt));
	}
}

static void
hv_kbd_on_received(hv_kbd_sc *sc, struct vmbus_chanpkt_hdr *pkt)
{

	const hv_kbd_msg *msg = VMBUS_CHANPKT_CONST_DATA(pkt);
	const hv_kbd_proto_resp *resp =
	    VMBUS_CHANPKT_CONST_DATA(pkt);
	const hv_kbd_keystroke *keystroke =
	    VMBUS_CHANPKT_CONST_DATA(pkt);
	uint32_t msg_len = VMBUS_CHANPKT_DATALEN(pkt);
	enum hv_kbd_msg_type_t msg_type;
	uint32_t info;
	uint16_t scan_code;

	if (msg_len <= sizeof(hv_kbd_msg)) {
		device_printf(sc->dev, "Illegal packet\n");
		return;
	}
	msg_type = msg->hdr.type;
	switch (msg_type) {
		case HV_KBD_PROTO_RESPONSE:
			hv_kbd_on_response(sc, pkt);
			DEBUG_HVSC(sc, "keyboard resp: 0x%x\n",
			    resp->status);
			break;
		case HV_KBD_PROTO_EVENT:
			info = keystroke->ks.info;
			scan_code = keystroke->ks.makecode;
			DEBUG_HVSC(sc, "keystroke info: 0x%x, scan: 0x%x\n",
			    info, scan_code);
			hv_kbd_produce_ks(sc, &keystroke->ks);
			hv_kbd_intr(sc);
		default:
			break;
	}
}

void 
hv_kbd_read_channel(struct vmbus_channel *channel, void *context)
{
	uint8_t *buf;
	uint32_t buflen = 0;
	int ret = 0;

	hv_kbd_sc *sc = (hv_kbd_sc*)context;
	buf = sc->buf;
	buflen = sc->buflen;
	for (;;) {
		struct vmbus_chanpkt_hdr *pkt = (struct vmbus_chanpkt_hdr *)buf;
		uint32_t rxed = buflen;

		ret = vmbus_chan_recv_pkt(channel, pkt, &rxed);
		if (__predict_false(ret == ENOBUFS)) {
			buflen = sc->buflen * 2;
			while (buflen < rxed)
				buflen *= 2;
			buf = malloc(buflen, M_DEVBUF, M_WAITOK | M_ZERO);
			device_printf(sc->dev, "expand recvbuf %d -> %d\n",
			    sc->buflen, buflen);
			free(sc->buf, M_DEVBUF);
			sc->buf = buf;
			sc->buflen = buflen;
			continue;
		} else if (__predict_false(ret == EAGAIN)) {
			/* No more channel packets; done! */
			break;
		}
		KASSERT(!ret, ("vmbus_chan_recv_pkt failed: %d", ret));

		DEBUG_HVSC(sc, "event: 0x%x\n", pkt->cph_type);
		switch (pkt->cph_type) {
		case VMBUS_CHANPKT_TYPE_COMP:
		case VMBUS_CHANPKT_TYPE_RXBUF:
			device_printf(sc->dev, "unhandled event: %d\n",
			    pkt->cph_type);
			break;
		case VMBUS_CHANPKT_TYPE_INBAND:
			hv_kbd_on_received(sc, pkt);
			break;
		default:
			device_printf(sc->dev, "unknown event: %d\n",
			    pkt->cph_type);
			break;
		}
	}
}

static int
hv_kbd_connect_vsp(hv_kbd_sc *sc)
{
	int ret;
	size_t resplen;
	struct vmbus_xact *xact;
	hv_kbd_proto_req *req;
	const hv_kbd_proto_resp *resp;

	xact = vmbus_xact_get(sc->hs_xact_ctx, sizeof(*req));
	if (xact == NULL) {
		device_printf(sc->dev, "no xact for kbd init");
		return (ENODEV);
	}
	req = vmbus_xact_req_data(xact);
	req->hdr.type = HV_KBD_PROTO_REQUEST;
	req->ver = HV_KBD_VER;

	vmbus_xact_activate(xact);
	ret = vmbus_chan_send(sc->hs_chan,
		VMBUS_CHANPKT_TYPE_INBAND,
		VMBUS_CHANPKT_FLAG_RC,
		req, sizeof(hv_kbd_proto_req),
		(uint64_t)(uintptr_t)xact);
	if (ret) {
		device_printf(sc->dev, "fail to send\n");
		vmbus_xact_deactivate(xact);
		return (ret);
	}
	resp = vmbus_chan_xact_wait(sc->hs_chan, xact, &resplen, true);
	if (resplen < HV_KBD_PROTO_RESP_SZ) {
		device_printf(sc->dev, "hv_kbd init communicate failed\n");
		ret = ENODEV;
		goto clean;
	}

	if (!(resp->status & HV_KBD_PROTO_ACCEPTED)) {
		device_printf(sc->dev, "hv_kbd protocol request failed\n");
		ret = ENODEV;
	}
clean:
	vmbus_xact_put(xact);
	DEBUG_HVSC(sc, "finish connect vsp\n");
	return (ret);
}

static int
hv_kbd_attach1(device_t dev, vmbus_chan_callback_t cb)
{
	int ret;
	hv_kbd_sc *sc;

        sc = device_get_softc(dev);
	sc->buflen = HV_BUFF_SIZE;
	sc->buf = malloc(sc->buflen, M_DEVBUF, M_WAITOK | M_ZERO);
	vmbus_chan_set_readbatch(sc->hs_chan, false);
	ret = vmbus_chan_open(
		sc->hs_chan,
		HV_KBD_RINGBUFF_SEND_SZ,
		HV_KBD_RINGBUFF_RECV_SZ,
		NULL, 0,
		cb,
		sc);
	if (ret != 0) {
		free(sc->buf, M_DEVBUF);
	}
	return (ret);
}

static int
hv_kbd_detach1(device_t dev)
{
	hv_kbd_sc *sc = device_get_softc(dev);
	vmbus_chan_close(vmbus_get_channel(dev));
	free(sc->buf, M_DEVBUF);
	return (0);
}

static void
hv_kbd_init(hv_kbd_sc *sc)
{
	const int max_list = 16;
	int i;
	keystroke_info *ksi;

	mtx_init(&sc->ks_mtx, "hv_kbdc mutex", NULL, MTX_DEF);
	LIST_INIT(&sc->ks_free_list);
	STAILQ_INIT(&sc->ks_queue);
	for (i = 0; i < max_list; i++) {
		ksi = malloc(sizeof(keystroke_info),
		    M_DEVBUF, M_WAITOK|M_ZERO);
		LIST_INSERT_HEAD(&sc->ks_free_list, ksi, link);
	}
}

static void
hv_kbd_fini(hv_kbd_sc *sc)
{
	keystroke_info *ksi;
	while (!LIST_EMPTY(&sc->ks_free_list)) {
		ksi = LIST_FIRST(&sc->ks_free_list);
		LIST_REMOVE(ksi, link);
		free(ksi, M_DEVBUF);
	}
	while (!STAILQ_EMPTY(&sc->ks_queue)) {
		ksi = STAILQ_FIRST(&sc->ks_queue);
		STAILQ_REMOVE_HEAD(&sc->ks_queue, slink);
		free(ksi, M_DEVBUF);
	}
	mtx_destroy(&sc->ks_mtx);
}

static void
hv_kbd_sysctl(device_t dev)
{
	struct sysctl_oid_list *child;
	struct sysctl_ctx_list *ctx;
	hv_kbd_sc *sc;

	sc = device_get_softc(dev);
	ctx = device_get_sysctl_ctx(dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(dev));
	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "debug", CTLFLAG_RW,
	    &sc->debug, 0, "debug hyperv keyboard");
}

static int
hv_kbd_attach(device_t dev)
{
	int error = 0;
	hv_kbd_sc *sc;

	sc = device_get_softc(dev);
	sc->hs_chan = vmbus_get_channel(dev);
	sc->dev = dev;
	hv_kbd_init(sc);
	sc->hs_xact_ctx = vmbus_xact_ctx_create(bus_get_dma_tag(dev),
	    HV_KBD_PROTO_REQ_SZ, HV_KBD_PROTO_RESP_SZ, 0);
	if (sc->hs_xact_ctx == NULL) {
		error = ENOMEM;
		goto failed;
	}

	error = hv_kbd_attach1(dev, hv_kbd_read_channel);
	if (error)
		goto failed;
	error = hv_kbd_connect_vsp(sc);
	if (error)
		goto failed;

	error = hv_kbd_drv_attach(dev);
	if (error)
		goto failed;
	hv_kbd_sysctl(dev);
	return (0);
failed:
	hv_kbd_detach(dev);
	return (error);
}

static int
hv_kbd_detach(device_t dev)
{
	int ret;
	hv_kbd_sc *sc = device_get_softc(dev);
	hv_kbd_fini(sc);
	if (sc->hs_xact_ctx != NULL)
		vmbus_xact_ctx_destroy(sc->hs_xact_ctx);
	ret = hv_kbd_detach1(dev);
	if (!ret)
		device_printf(dev, "Fail to detach\n");
	return hv_kbd_drv_detach(dev);
}

static device_method_t kbd_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, hv_kbd_probe),
	DEVMETHOD(device_attach, hv_kbd_attach),
	DEVMETHOD(device_detach, hv_kbd_detach),
	{ 0, 0 }
};

static driver_t kbd_driver = {HVKBD_DRIVER_NAME , kbd_methods, sizeof(hv_kbd_sc)};

static devclass_t kbd_devclass;

DRIVER_MODULE(hv_kbd, vmbus, kbd_driver, kbd_devclass, hvkbd_driver_load, NULL);
MODULE_VERSION(hv_kbd, 1);
MODULE_DEPEND(hv_kbd, vmbus, 1, 1, 1);
