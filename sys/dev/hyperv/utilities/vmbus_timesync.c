/*-
 * Copyright (c) 2014,2016-2017 Microsoft Corp.
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
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <dev/hyperv/include/hyperv.h>
#include <dev/hyperv/include/vmbus.h>
#include <dev/hyperv/utilities/vmbus_icreg.h>
#include <dev/hyperv/utilities/vmbus_icvar.h>

#define VMBUS_TIMESYNC_FWVER_MAJOR	3
#define VMBUS_TIMESYNC_FWVER		\
	VMBUS_IC_VERSION(VMBUS_TIMESYNC_FWVER_MAJOR, 0)

#define VMBUS_TIMESYNC_MSGVER_MAJOR	4
#define VMBUS_TIMESYNC_MSGVER		\
	VMBUS_IC_VERSION(VMBUS_TIMESYNC_MSGVER_MAJOR, 0)

#define VMBUS_TIMESYNC_MSGVER4(sc)	\
	VMBUS_ICVER_LE(VMBUS_IC_VERSION(4, 0), (sc)->ic_msgver)

#define VMBUS_TIMESYNC_DORTT(sc)	\
	(VMBUS_TIMESYNC_MSGVER4((sc)) && hyperv_tc64 != NULL)

static int			vmbus_timesync_probe(device_t);
static int			vmbus_timesync_attach(device_t);

static const struct vmbus_ic_desc vmbus_timesync_descs[] = {
	{
		.ic_guid = { .hv_guid = {
		    0x30, 0xe6, 0x27, 0x95, 0xae, 0xd0, 0x7b, 0x49,
		    0xad, 0xce, 0xe8, 0x0a, 0xb0, 0x17, 0x5c, 0xaf } },
		.ic_desc = "Hyper-V Timesync"
	},
	VMBUS_IC_DESC_END
};

static device_method_t vmbus_timesync_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		vmbus_timesync_probe),
	DEVMETHOD(device_attach,	vmbus_timesync_attach),
	DEVMETHOD(device_detach,	vmbus_ic_detach),
	DEVMETHOD_END
};

static driver_t vmbus_timesync_driver = {
	"hvtimesync",
	vmbus_timesync_methods,
	sizeof(struct vmbus_ic_softc)
};

static devclass_t vmbus_timesync_devclass;

DRIVER_MODULE(hv_timesync, vmbus, vmbus_timesync_driver,
    vmbus_timesync_devclass, NULL, NULL);
MODULE_VERSION(hv_timesync, 1);
MODULE_DEPEND(hv_timesync, vmbus, 1, 1, 1);

SYSCTL_NODE(_hw, OID_AUTO, hvtimesync, CTLFLAG_RW | CTLFLAG_MPSAFE, NULL,
    "Hyper-V timesync interface");

static int vmbus_ts_ignore_sync = 0;
SYSCTL_INT(_hw_hvtimesync, OID_AUTO, ignore_sync, CTLFLAG_RWTUN,
    &vmbus_ts_ignore_sync, 0, "Ignore the sync request.");

/*
 * Trigger sample sync when drift exceeds threshold (ms).
 * Ignore the sample request when set to 0.
 */
static int vmbus_ts_sample_thresh = 100;
SYSCTL_INT(_hw_hvtimesync, OID_AUTO, sample_thresh, CTLFLAG_RWTUN,
    &vmbus_ts_sample_thresh, 0,
    "Threshold that makes sample request trigger the sync (unit: ms).");

static int vmbus_ts_sample_verbose = 0;
SYSCTL_INT(_hw_hvtimesync, OID_AUTO, sample_verbose, CTLFLAG_RWTUN,
    &vmbus_ts_sample_verbose, 0, "Increase sample request verbosity.");

static void
vmbus_timesync(struct vmbus_ic_softc *sc, uint64_t hvtime, uint64_t sent_tc,
    uint8_t tsflags)
{
	struct timespec vm_ts;
	uint64_t hv_ns, vm_ns, rtt = 0;

	if (VMBUS_TIMESYNC_DORTT(sc))
		rtt = hyperv_tc64() - sent_tc;

	hv_ns = (hvtime - VMBUS_ICMSG_TS_BASE + rtt) * HYPERV_TIMER_NS_FACTOR;
	nanotime(&vm_ts);
	vm_ns = (vm_ts.tv_sec * NANOSEC) + vm_ts.tv_nsec;

	if ((tsflags & VMBUS_ICMSG_TS_FLAG_SYNC) && !vmbus_ts_ignore_sync) {
		struct timespec hv_ts;

		if (bootverbose) {
			device_printf(sc->ic_dev, "apply sync request, "
			    "hv: %ju, vm: %ju\n",
			    (uintmax_t)hv_ns, (uintmax_t)vm_ns);
		}
		hv_ts.tv_sec = hv_ns / NANOSEC;
		hv_ts.tv_nsec = hv_ns % NANOSEC;
		kern_clock_settime(curthread, CLOCK_REALTIME, &hv_ts);
		/* Done! */
		return;
	}

	if ((tsflags & VMBUS_ICMSG_TS_FLAG_SAMPLE) &&
	    vmbus_ts_sample_thresh >= 0) {
		int64_t diff;

		if (vmbus_ts_sample_verbose) {
			device_printf(sc->ic_dev, "sample request, "
			    "hv: %ju, vm: %ju\n",
			    (uintmax_t)hv_ns, (uintmax_t)vm_ns);
		}

		if (hv_ns > vm_ns)
			diff = hv_ns - vm_ns;
		else
			diff = vm_ns - hv_ns;
		/* nanosec -> millisec */
		diff /= 1000000;

		if (diff > vmbus_ts_sample_thresh) {
			struct timespec hv_ts;

			if (bootverbose) {
				device_printf(sc->ic_dev,
				    "apply sample request, hv: %ju, vm: %ju\n",
				    (uintmax_t)hv_ns, (uintmax_t)vm_ns);
			}
			hv_ts.tv_sec = hv_ns / NANOSEC;
			hv_ts.tv_nsec = hv_ns % NANOSEC;
			kern_clock_settime(curthread, CLOCK_REALTIME, &hv_ts);
		}
		/* Done */
		return;
	}
}

static void
vmbus_timesync_cb(struct vmbus_channel *chan, void *xsc)
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
		    VMBUS_TIMESYNC_FWVER, VMBUS_TIMESYNC_MSGVER);
		if (error)
			return;
		if (VMBUS_TIMESYNC_DORTT(sc))
			device_printf(sc->ic_dev, "RTT\n");
		break;

	case VMBUS_ICMSG_TYPE_TIMESYNC:
		if (VMBUS_TIMESYNC_MSGVER4(sc)) {
			const struct vmbus_icmsg_timesync4 *msg4;

			if (dlen < sizeof(*msg4)) {
				device_printf(sc->ic_dev, "invalid timesync4 "
				    "len %d\n", dlen);
				return;
			}
			msg4 = data;
			vmbus_timesync(sc, msg4->ic_hvtime, msg4->ic_sent_tc,
			    msg4->ic_tsflags);
		} else {
			const struct vmbus_icmsg_timesync *msg;

			if (dlen < sizeof(*msg)) {
				device_printf(sc->ic_dev, "invalid timesync "
				    "len %d\n", dlen);
				return;
			}
			msg = data;
			vmbus_timesync(sc, msg->ic_hvtime, 0, msg->ic_tsflags);
		}
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
vmbus_timesync_probe(device_t dev)
{

	return (vmbus_ic_probe(dev, vmbus_timesync_descs));
}

static int
vmbus_timesync_attach(device_t dev)
{

	return (vmbus_ic_attach(dev, vmbus_timesync_cb));
}
