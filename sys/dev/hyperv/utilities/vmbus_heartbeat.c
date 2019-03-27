/*-
 * Copyright (c) 2014,2016 Microsoft Corp.
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>

#include <dev/hyperv/include/hyperv.h>
#include <dev/hyperv/include/vmbus.h>
#include <dev/hyperv/utilities/vmbus_icreg.h>
#include <dev/hyperv/utilities/vmbus_icvar.h>

#define VMBUS_HEARTBEAT_FWVER_MAJOR	3
#define VMBUS_HEARTBEAT_FWVER		\
	VMBUS_IC_VERSION(VMBUS_HEARTBEAT_FWVER_MAJOR, 0)

#define VMBUS_HEARTBEAT_MSGVER_MAJOR	3
#define VMBUS_HEARTBEAT_MSGVER		\
	VMBUS_IC_VERSION(VMBUS_HEARTBEAT_MSGVER_MAJOR, 0)

static int			vmbus_heartbeat_probe(device_t);
static int			vmbus_heartbeat_attach(device_t);

static const struct vmbus_ic_desc vmbus_heartbeat_descs[] = {
	{
		.ic_guid = { .hv_guid = {
		    0x39, 0x4f, 0x16, 0x57, 0x15, 0x91, 0x78, 0x4e,
		    0xab, 0x55, 0x38, 0x2f, 0x3b, 0xd5, 0x42, 0x2d} },
		.ic_desc = "Hyper-V Heartbeat"
	},
	VMBUS_IC_DESC_END
};

static device_method_t vmbus_heartbeat_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		vmbus_heartbeat_probe),
	DEVMETHOD(device_attach,	vmbus_heartbeat_attach),
	DEVMETHOD(device_detach,	vmbus_ic_detach),
	DEVMETHOD_END
};

static driver_t vmbus_heartbeat_driver = {
	"hvheartbeat",
	vmbus_heartbeat_methods,
	sizeof(struct vmbus_ic_softc)
};

static devclass_t vmbus_heartbeat_devclass;

DRIVER_MODULE(hv_heartbeat, vmbus, vmbus_heartbeat_driver,
    vmbus_heartbeat_devclass, NULL, NULL);
MODULE_VERSION(hv_heartbeat, 1);
MODULE_DEPEND(hv_heartbeat, vmbus, 1, 1, 1);

static void
vmbus_heartbeat_cb(struct vmbus_channel *chan, void *xsc)
{
	struct vmbus_ic_softc *sc = xsc;
	struct vmbus_icmsg_hdr *hdr;
	int dlen, error;
	uint64_t xactid;
	void *data;

	/*
	 * Receive request.
	 */
	data = sc->ic_buf;
	dlen = sc->ic_buflen;
	error = vmbus_chan_recv(chan, data, &dlen, &xactid);
	KASSERT(error != ENOBUFS, ("icbuf is not large enough"));
	if (error)
		return;

	if (dlen < sizeof(*hdr)) {
		device_printf(sc->ic_dev, "invalid data len %d\n", dlen);
		return;
	}
	hdr = data;

	/*
	 * Update request, which will be echoed back as response.
	 */
	switch (hdr->ic_type) {
	case VMBUS_ICMSG_TYPE_NEGOTIATE:
		error = vmbus_ic_negomsg(sc, data, &dlen,
		    VMBUS_HEARTBEAT_FWVER, VMBUS_HEARTBEAT_MSGVER);
		if (error)
			return;
		break;

	case VMBUS_ICMSG_TYPE_HEARTBEAT:
		/* Only ic_seq is a must */
		if (dlen < VMBUS_ICMSG_HEARTBEAT_SIZE_MIN) {
			device_printf(sc->ic_dev, "invalid heartbeat len %d\n",
			    dlen);
			return;
		}
		((struct vmbus_icmsg_heartbeat *)data)->ic_seq++;
		break;

	default:
		device_printf(sc->ic_dev, "got 0x%08x icmsg\n", hdr->ic_type);
		break;
	}

	/*
	 * Send response by echoing the request back.
	 */
	vmbus_ic_sendresp(sc, chan, data, dlen, xactid);
}

static int
vmbus_heartbeat_probe(device_t dev)
{

	return (vmbus_ic_probe(dev, vmbus_heartbeat_descs));
}

static int
vmbus_heartbeat_attach(device_t dev)
{

	return (vmbus_ic_attach(dev, vmbus_heartbeat_cb));
}
