/*-
 * Copyright (C) 2018 Justin Hibbits
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/types.h>
#include <sys/bus.h>
#include <sys/kthread.h>
#include <sys/proc.h>
#include <sys/selinfo.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <sys/ipmi.h>
#include <dev/ipmi/ipmivars.h>

#include <powerpc/powernv/opal.h>

struct opal_ipmi_softc {
	struct ipmi_softc ipmi;
	uint64_t sc_interface;
	struct opal_ipmi_msg *sc_msg; /* Protected by IPMI lock */
};

static MALLOC_DEFINE(M_IPMI, "ipmi", "OPAL IPMI");

static int
opal_ipmi_polled_request(struct opal_ipmi_softc *sc, struct ipmi_request *req,
    int timo)
{
	uint64_t msg_len;
	int err;

	/* Construct and send the message. */
	sc->sc_msg->version = OPAL_IPMI_MSG_FORMAT_VERSION_1;
	sc->sc_msg->netfn = req->ir_addr;
	sc->sc_msg->cmd = req->ir_command;

	if (req->ir_requestlen > IPMI_MAX_RX) {
		err = ENOMEM;
		goto out;
	}
	memcpy(sc->sc_msg->data, req->ir_request, req->ir_requestlen);

	msg_len = sizeof(*sc->sc_msg) + req->ir_requestlen;
	err = opal_call(OPAL_IPMI_SEND, sc->sc_interface, vtophys(sc->sc_msg),
	    msg_len);
	switch (err) {
	case OPAL_SUCCESS:
		break;
	case OPAL_PARAMETER:
		err = EINVAL;
		goto out;
	case OPAL_HARDWARE:
		err = EIO;
		goto out;
	case OPAL_UNSUPPORTED:
		err = EINVAL;
		goto out;
	case OPAL_RESOURCE:
		err = ENOMEM;
		goto out;
	}

	timo *= 10; /* Timeout is in milliseconds, we delay in 100us */
	do {
		msg_len = sizeof(struct opal_ipmi_msg) + IPMI_MAX_RX;
		err = opal_call(OPAL_IPMI_RECV, sc->sc_interface,
		    vtophys(sc->sc_msg), vtophys(&msg_len));
		if (err != OPAL_EMPTY)
			break;
		DELAY(100);
	} while (err == OPAL_EMPTY && timo-- != 0);

	switch (err) {
	case OPAL_SUCCESS:
		/* Subtract one extra for the completion code. */
		req->ir_replylen = msg_len - sizeof(struct opal_ipmi_msg) - 1;
		req->ir_replylen = min(req->ir_replylen, req->ir_replybuflen);
		memcpy(req->ir_reply, &sc->sc_msg->data[1], req->ir_replylen);
		req->ir_compcode = sc->sc_msg->data[0];
		break;
	case OPAL_RESOURCE:
		err = ENOMEM;
		break;
	case OPAL_EMPTY:
		err = EAGAIN;
		break;
	default:
		err = EIO;
		break;
	}

out:

	return (err);
}

static int
opal_ipmi_probe(device_t dev)
{
	if (!ofw_bus_is_compatible(dev, "ibm,opal-ipmi"))
		return (ENXIO);

	device_set_desc(dev, "OPAL IPMI System Interface");

	return (BUS_PROBE_DEFAULT);
}

static void
opal_ipmi_loop(void *arg)
{
	struct opal_ipmi_softc *sc = arg;
	struct ipmi_request *req;
	int i, ok;

	IPMI_LOCK(&sc->ipmi);
	while ((req = ipmi_dequeue_request(&sc->ipmi)) != NULL) {
		IPMI_UNLOCK(&sc->ipmi);
		ok = 0;
		for (i = 0; i < 3 && !ok; i++) {
			IPMI_IO_LOCK(&sc->ipmi);
			ok = opal_ipmi_polled_request(sc, req, MAX_TIMEOUT);
			IPMI_IO_UNLOCK(&sc->ipmi);
		}
		if (ok)
			req->ir_error = 0;
		else
			req->ir_error = EIO;
		IPMI_LOCK(&sc->ipmi);
		ipmi_complete_request(&sc->ipmi, req);
	}
	IPMI_UNLOCK(&sc->ipmi);
	kproc_exit(0);
}

static int
opal_ipmi_startup(struct ipmi_softc *sc)
{

	return (kproc_create(opal_ipmi_loop, sc, &sc->ipmi_kthread, 0, 0,
	    "%s: opal", device_get_nameunit(sc->ipmi_dev)));
}

static int
opal_ipmi_driver_request(struct ipmi_softc *isc, struct ipmi_request *req,
    int timo)
{
	struct opal_ipmi_softc *sc = (struct opal_ipmi_softc *)isc;
	int i, err;

	for (i = 0; i < 3; i++) {
		IPMI_LOCK(&sc->ipmi);
		err = opal_ipmi_polled_request(sc, req, timo);
		IPMI_UNLOCK(&sc->ipmi);
		if (err == 0)
			break;
	}

	req->ir_error = err;

	return (err);
}

static int
opal_ipmi_attach(device_t dev)
{
	struct opal_ipmi_softc *sc;

	sc = device_get_softc(dev);

	if (OF_getencprop(ofw_bus_get_node(dev), "ibm,ipmi-interface-id",
	    (pcell_t*)&sc->sc_interface, sizeof(sc->sc_interface)) < 0) {
		device_printf(dev, "Missing interface id\n");
		return (ENXIO);
	}
	sc->ipmi.ipmi_startup = opal_ipmi_startup;
	sc->ipmi.ipmi_driver_request = opal_ipmi_driver_request;
	sc->ipmi.ipmi_enqueue_request = ipmi_polled_enqueue_request;
	sc->ipmi.ipmi_driver_requests_polled = 1;
	sc->ipmi.ipmi_dev = dev;

	sc->sc_msg = malloc(sizeof(struct opal_ipmi_msg) + IPMI_MAX_RX, M_IPMI,
	    M_WAITOK | M_ZERO);

	return (ipmi_attach(dev));
}

static int
opal_ipmi_detach(device_t dev)
{
	return (EBUSY);
}

static device_method_t	opal_ipmi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		opal_ipmi_probe),
	DEVMETHOD(device_attach,	opal_ipmi_attach),
	DEVMETHOD(device_detach,	opal_ipmi_detach),
	DEVMETHOD_END
};

static driver_t opal_ipmi_driver = {
	"ipmi",
	opal_ipmi_methods,
	sizeof(struct opal_ipmi_softc)
};

DRIVER_MODULE(opal_ipmi, opal, opal_ipmi_driver, ipmi_devclass, NULL, NULL);
