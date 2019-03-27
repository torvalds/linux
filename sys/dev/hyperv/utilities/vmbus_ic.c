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
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <dev/hyperv/include/hyperv.h>
#include <dev/hyperv/include/vmbus.h>
#include <dev/hyperv/utilities/vmbus_icreg.h>
#include <dev/hyperv/utilities/vmbus_icvar.h>

#include "vmbus_if.h"

#define VMBUS_IC_BRSIZE		(4 * PAGE_SIZE)

#define VMBUS_IC_VERCNT		2
#define VMBUS_IC_NEGOSZ		\
	__offsetof(struct vmbus_icmsg_negotiate, ic_ver[VMBUS_IC_VERCNT])
CTASSERT(VMBUS_IC_NEGOSZ < VMBUS_IC_BRSIZE);

static int	vmbus_ic_fwver_sysctl(SYSCTL_HANDLER_ARGS);
static int	vmbus_ic_msgver_sysctl(SYSCTL_HANDLER_ARGS);

int
vmbus_ic_negomsg(struct vmbus_ic_softc *sc, void *data, int *dlen0,
    uint32_t fw_ver, uint32_t msg_ver)
{
	struct vmbus_icmsg_negotiate *nego;
	int i, cnt, dlen = *dlen0, error;
	uint32_t sel_fw_ver, sel_msg_ver;
	bool has_fw_ver, has_msg_ver;

	/*
	 * Preliminary message verification.
	 */
	if (dlen < sizeof(*nego)) {
		device_printf(sc->ic_dev, "truncated ic negotiate, len %d\n",
		    dlen);
		return (EINVAL);
	}
	nego = data;

	if (nego->ic_fwver_cnt == 0) {
		device_printf(sc->ic_dev, "ic negotiate does not contain "
		    "framework version %u\n", nego->ic_fwver_cnt);
		return (EINVAL);
	}
	if (nego->ic_msgver_cnt == 0) {
		device_printf(sc->ic_dev, "ic negotiate does not contain "
		    "message version %u\n", nego->ic_msgver_cnt);
		return (EINVAL);
	}

	cnt = nego->ic_fwver_cnt + nego->ic_msgver_cnt;
	if (dlen < __offsetof(struct vmbus_icmsg_negotiate, ic_ver[cnt])) {
		device_printf(sc->ic_dev, "ic negotiate does not contain "
		    "versions %d\n", dlen);
		return (EINVAL);
	}

	error = EOPNOTSUPP;

	/*
	 * Find the best match framework version.
	 */
	has_fw_ver = false;
	for (i = 0; i < nego->ic_fwver_cnt; ++i) {
		if (VMBUS_ICVER_LE(nego->ic_ver[i], fw_ver)) {
			if (!has_fw_ver) {
				sel_fw_ver = nego->ic_ver[i];
				has_fw_ver = true;
			} else if (VMBUS_ICVER_GT(nego->ic_ver[i],
			    sel_fw_ver)) {
				sel_fw_ver = nego->ic_ver[i];
			}
		}
	}
	if (!has_fw_ver) {
		device_printf(sc->ic_dev, "failed to select framework "
		    "version\n");
		goto done;
	}

	/*
	 * Fine the best match message version.
	 */
	has_msg_ver = false;
	for (i = nego->ic_fwver_cnt;
	    i < nego->ic_fwver_cnt + nego->ic_msgver_cnt; ++i) {
		if (VMBUS_ICVER_LE(nego->ic_ver[i], msg_ver)) {
			if (!has_msg_ver) {
				sel_msg_ver = nego->ic_ver[i];
				has_msg_ver = true;
			} else if (VMBUS_ICVER_GT(nego->ic_ver[i],
			    sel_msg_ver)) {
				sel_msg_ver = nego->ic_ver[i];
			}
		}
	}
	if (!has_msg_ver) {
		device_printf(sc->ic_dev, "failed to select message "
		    "version\n");
		goto done;
	}

	error = 0;
done:
	if (bootverbose || !has_fw_ver || !has_msg_ver) {
		if (has_fw_ver) {
			device_printf(sc->ic_dev, "sel framework version: "
			    "%u.%u\n",
			    VMBUS_ICVER_MAJOR(sel_fw_ver),
			    VMBUS_ICVER_MINOR(sel_fw_ver));
		}
		for (i = 0; i < nego->ic_fwver_cnt; i++) {
			device_printf(sc->ic_dev, "supp framework version: "
			    "%u.%u\n",
			    VMBUS_ICVER_MAJOR(nego->ic_ver[i]),
			    VMBUS_ICVER_MINOR(nego->ic_ver[i]));
		}

		if (has_msg_ver) {
			device_printf(sc->ic_dev, "sel message version: "
			    "%u.%u\n",
			    VMBUS_ICVER_MAJOR(sel_msg_ver),
			    VMBUS_ICVER_MINOR(sel_msg_ver));
		}
		for (i = nego->ic_fwver_cnt;
		    i < nego->ic_fwver_cnt + nego->ic_msgver_cnt; i++) {
			device_printf(sc->ic_dev, "supp message version: "
			    "%u.%u\n",
			    VMBUS_ICVER_MAJOR(nego->ic_ver[i]),
			    VMBUS_ICVER_MINOR(nego->ic_ver[i]));
		}
	}
	if (error)
		return (error);

	/* Record the selected versions. */
	sc->ic_fwver = sel_fw_ver;
	sc->ic_msgver = sel_msg_ver;

	/* One framework version. */
	nego->ic_fwver_cnt = 1;
	nego->ic_ver[0] = sel_fw_ver;

	/* One message version. */
	nego->ic_msgver_cnt = 1;
	nego->ic_ver[1] = sel_msg_ver;

	/* Update data size. */
	nego->ic_hdr.ic_dsize = VMBUS_IC_NEGOSZ -
	    sizeof(struct vmbus_icmsg_hdr);

	/* Update total size, if necessary. */
	if (dlen < VMBUS_IC_NEGOSZ)
		*dlen0 = VMBUS_IC_NEGOSZ;

	return (0);
}

int
vmbus_ic_probe(device_t dev, const struct vmbus_ic_desc descs[])
{
	device_t bus = device_get_parent(dev);
	const struct vmbus_ic_desc *d;

	if (resource_disabled(device_get_name(dev), 0))
		return (ENXIO);

	for (d = descs; d->ic_desc != NULL; ++d) {
		if (VMBUS_PROBE_GUID(bus, dev, &d->ic_guid) == 0) {
			device_set_desc(dev, d->ic_desc);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

int
vmbus_ic_attach(device_t dev, vmbus_chan_callback_t cb)
{
	struct vmbus_ic_softc *sc = device_get_softc(dev);
	struct vmbus_channel *chan = vmbus_get_channel(dev);
	struct sysctl_oid_list *child;
	struct sysctl_ctx_list *ctx;
	int error;

	sc->ic_dev = dev;
	sc->ic_buflen = VMBUS_IC_BRSIZE;
	sc->ic_buf = malloc(VMBUS_IC_BRSIZE, M_DEVBUF, M_WAITOK | M_ZERO);

	/*
	 * These services are not performance critical and do not need
	 * batched reading. Furthermore, some services such as KVP can
	 * only handle one message from the host at a time.
	 * Turn off batched reading for all util drivers before we open the
	 * channel.
	 */
	vmbus_chan_set_readbatch(chan, false);

	error = vmbus_chan_open(chan, VMBUS_IC_BRSIZE, VMBUS_IC_BRSIZE, NULL, 0,
	    cb, sc);
	if (error) {
		free(sc->ic_buf, M_DEVBUF);
		return (error);
	}

	ctx = device_get_sysctl_ctx(dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(dev));
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "fw_version",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    vmbus_ic_fwver_sysctl, "A", "framework version");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "msg_version",
	    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    vmbus_ic_msgver_sysctl, "A", "message version");

	return (0);
}

static int
vmbus_ic_fwver_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct vmbus_ic_softc *sc = arg1;
	char verstr[16];

	snprintf(verstr, sizeof(verstr), "%u.%u",
	    VMBUS_ICVER_MAJOR(sc->ic_fwver), VMBUS_ICVER_MINOR(sc->ic_fwver));
	return sysctl_handle_string(oidp, verstr, sizeof(verstr), req);
}

static int
vmbus_ic_msgver_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct vmbus_ic_softc *sc = arg1;
	char verstr[16];

	snprintf(verstr, sizeof(verstr), "%u.%u",
	    VMBUS_ICVER_MAJOR(sc->ic_msgver), VMBUS_ICVER_MINOR(sc->ic_msgver));
	return sysctl_handle_string(oidp, verstr, sizeof(verstr), req);
}

int
vmbus_ic_detach(device_t dev)
{
	struct vmbus_ic_softc *sc = device_get_softc(dev);

	vmbus_chan_close(vmbus_get_channel(dev));
	free(sc->ic_buf, M_DEVBUF);

	return (0);
}

int
vmbus_ic_sendresp(struct vmbus_ic_softc *sc, struct vmbus_channel *chan,
    void *data, int dlen, uint64_t xactid)
{
	struct vmbus_icmsg_hdr *hdr;
	int error;

	KASSERT(dlen >= sizeof(*hdr), ("invalid data length %d", dlen));
	hdr = data;

	hdr->ic_flags = VMBUS_ICMSG_FLAG_XACT | VMBUS_ICMSG_FLAG_RESP;
	error = vmbus_chan_send(chan, VMBUS_CHANPKT_TYPE_INBAND, 0,
	    data, dlen, xactid);
	if (error)
		device_printf(sc->ic_dev, "resp send failed: %d\n", error);
	return (error);
}
