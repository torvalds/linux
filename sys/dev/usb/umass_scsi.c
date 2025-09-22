/*	$OpenBSD: umass_scsi.c,v 1.65 2024/05/23 03:21:09 jsg Exp $ */
/*	$NetBSD: umass_scsipi.c,v 1.9 2003/02/16 23:14:08 augustss Exp $	*/
/*
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>

#include <dev/usb/umassvar.h>
#include <dev/usb/umass_scsi.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <machine/bus.h>

struct umass_scsi_softc {
	struct device		*sc_child;
	struct scsi_iopool	sc_iopool;
	int			sc_open;

	struct scsi_sense	sc_sense_cmd;
};


#define UMASS_SCSIID_HOST	0x00
#define UMASS_SCSIID_DEVICE	0x01

int umass_scsi_probe(struct scsi_link *);
void umass_scsi_cmd(struct scsi_xfer *);

const struct scsi_adapter umass_scsi_switch = {
	umass_scsi_cmd, NULL, umass_scsi_probe, NULL, NULL
};

void umass_scsi_cb(struct umass_softc *sc, void *priv, int residue,
		   int status);
void umass_scsi_sense_cb(struct umass_softc *sc, void *priv, int residue,
			 int status);
void *umass_io_get(void *);
void umass_io_put(void *, void *);

int
umass_scsi_attach(struct umass_softc *sc)
{
	struct scsibus_attach_args saa;
	struct umass_scsi_softc *scbus;
	u_int16_t flags = 0;

	scbus = malloc(sizeof(*scbus), M_USBDEV, M_WAITOK | M_ZERO);

	sc->bus = scbus;

	switch (sc->sc_cmd) {
	case UMASS_CPROTO_RBC:
	case UMASS_CPROTO_SCSI:
		DPRINTF(UDMASS_USB, ("%s: umass_attach_bus: SCSI\n"
				     "sc = 0x%p, scbus = 0x%p\n",
				     sc->sc_dev.dv_xname, sc, scbus));
		break;
	case UMASS_CPROTO_UFI:
		flags |= SDEV_UFI | SDEV_ATAPI;
		DPRINTF(UDMASS_USB, ("%s: umass_attach_bus: UFI\n"
				     "sc = 0x%p, scbus = 0x%p\n",
				     sc->sc_dev.dv_xname, sc, scbus));
		break;
	case UMASS_CPROTO_ATAPI:
		flags |= SDEV_ATAPI;
		DPRINTF(UDMASS_USB, ("%s: umass_attach_bus: ATAPI\n"
				     "sc = 0x%p, scbus = 0x%p\n",
				     sc->sc_dev.dv_xname, sc, scbus));
		break;
	default:
		break;
	}

	scsi_iopool_init(&scbus->sc_iopool, scbus, umass_io_get, umass_io_put);

	saa.saa_adapter_buswidth = 2;
	saa.saa_adapter = &umass_scsi_switch;
	saa.saa_adapter_softc = sc;
	saa.saa_adapter_target = UMASS_SCSIID_HOST;
	saa.saa_luns = sc->maxlun + 1;
	saa.saa_openings = 1;
	saa.saa_quirks = sc->sc_busquirks;
	saa.saa_pool = &scbus->sc_iopool;
	saa.saa_flags = SDEV_UMASS | flags;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	sc->sc_refcnt++;
	scbus->sc_child = config_found((struct device *)sc, &saa, scsiprint);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);

	return (0);
}

int
umass_scsi_detach(struct umass_softc *sc, int flags)
{
	struct umass_scsi_softc *scbus = sc->bus;
	int rv = 0;

	if (scbus != NULL) {
		if (scbus->sc_child != NULL)
			rv = config_detach(scbus->sc_child, flags);
		free(scbus, M_USBDEV, sizeof(*scbus));
		sc->bus = NULL;
	}

	return (rv);
}

int
umass_scsi_probe(struct scsi_link *link)
{
	struct umass_softc *sc = link->bus->sb_adapter_softc;
	struct usb_device_info udi;
	size_t len;

	/* dont fake devids when more than one scsi device can attach. */
	if (sc->maxlun > 0)
		return (0);

	usbd_fill_deviceinfo(sc->sc_udev, &udi);

	/*
	 * Create a fake devid using the vendor and product ids and the last
	 * 12 characters of serial number, as recommended by Section 4.1.1 of
	 * the USB Mass Storage Class - Bulk Only Transport spec.
	 */
	len = strlen(udi.udi_serial);
	if (len >= 12) {
		char buf[21];
		snprintf(buf, sizeof(buf), "%04x%04x%s", udi.udi_vendorNo,
		    udi.udi_productNo, udi.udi_serial + len - 12);
		link->id = devid_alloc(DEVID_SERIAL, DEVID_F_PRINT,
		    sizeof(buf) - 1, buf);
	}

	return (0);
}

void
umass_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *sc_link = xs->sc_link;
	struct umass_softc *sc = sc_link->bus->sb_adapter_softc;
	struct scsi_generic *cmd;
	int cmdlen, dir;

#ifdef UMASS_DEBUG
	microtime(&sc->tv);
#endif

	DPRINTF(UDMASS_CMD, ("%s: umass_scsi_cmd: at %lld.%06ld: %d:%d "
		"xs=%p cmd=0x%02x datalen=%d (quirks=0x%x, poll=%d)\n",
		sc->sc_dev.dv_xname, (long long)sc->tv.tv_sec, sc->tv.tv_usec,
		sc_link->target, sc_link->lun, xs, xs->cmd.opcode,
		xs->datalen, sc_link->quirks, xs->flags & SCSI_POLL));

	if (usbd_is_dying(sc->sc_udev)) {
		xs->error = XS_DRIVER_STUFFUP;
		goto done;
	}

#if defined(UMASS_DEBUG)
	if (sc_link->target != UMASS_SCSIID_DEVICE) {
		DPRINTF(UDMASS_SCSI, ("%s: wrong SCSI ID %d\n",
			sc->sc_dev.dv_xname, sc_link->target));
		xs->error = XS_DRIVER_STUFFUP;
		goto done;
	}
#endif

	cmd = &xs->cmd;
	cmdlen = xs->cmdlen;

	dir = DIR_NONE;
	if (xs->datalen) {
		switch (xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) {
		case SCSI_DATA_IN:
			dir = DIR_IN;
			break;
		case SCSI_DATA_OUT:
			dir = DIR_OUT;
			break;
		}
	}

	if (xs->datalen > UMASS_MAX_TRANSFER_SIZE) {
		printf("umass_cmd: large datalen, %d\n", xs->datalen);
		xs->error = XS_DRIVER_STUFFUP;
		goto done;
	}

	if (xs->flags & SCSI_POLL) {
		DPRINTF(UDMASS_SCSI, ("umass_scsi_cmd: sync dir=%d\n", dir));
		usbd_set_polling(sc->sc_udev, 1);
		sc->sc_xfer_flags = USBD_SYNCHRONOUS;
		sc->polled_xfer_status = USBD_INVAL;
		sc->sc_methods->wire_xfer(sc, sc_link->lun, cmd, cmdlen,
					  xs->data, xs->datalen, dir,
					  xs->timeout, umass_scsi_cb, xs);
		sc->sc_xfer_flags = 0;
		DPRINTF(UDMASS_SCSI, ("umass_scsi_cmd: done err=%d\n",
				      sc->polled_xfer_status));
		usbd_set_polling(sc->sc_udev, 0);
		/* scsi_done() has already been called. */
		return;
	} else {
		DPRINTF(UDMASS_SCSI,
			("umass_scsi_cmd: async dir=%d, cmdlen=%d"
			 " datalen=%d\n",
			 dir, cmdlen, xs->datalen));
		sc->sc_methods->wire_xfer(sc, sc_link->lun, cmd, cmdlen,
					  xs->data, xs->datalen, dir,
					  xs->timeout, umass_scsi_cb, xs);
		/* scsi_done() has already been called. */
		return;
	}

	/* Return if command finishes early. */
 done:
	scsi_done(xs);
}

void
umass_scsi_cb(struct umass_softc *sc, void *priv, int residue, int status)
{
	struct umass_scsi_softc *scbus = sc->bus;
	struct scsi_xfer *xs = priv;
	struct scsi_link *link = xs->sc_link;
	int cmdlen;
#ifdef UMASS_DEBUG
	struct timeval tv;
	u_int delta;
	microtime(&tv);
	delta = (tv.tv_sec - sc->tv.tv_sec) * 1000000 +
		tv.tv_usec - sc->tv.tv_usec;
#endif

	DPRINTF(UDMASS_CMD,
		("umass_scsi_cb: at %lld.%06ld, delta=%u: xs=%p residue=%d"
		 " status=%d\n", (long long)tv.tv_sec, tv.tv_usec, delta, xs, residue,
		 status));

	xs->resid = residue;

	switch (status) {
	case STATUS_CMD_OK:
		xs->error = XS_NOERROR;
		break;

	case STATUS_CMD_UNKNOWN:
		DPRINTF(UDMASS_CMD, ("umass_scsi_cb: status cmd unknown\n"));
		/* we can't issue REQUEST SENSE */
		if (xs->sc_link->quirks & ADEV_NOSENSE) {
			/*
			 * If no residue and no other USB error,
			 * command succeeded.
			 */
			if (residue == 0) {
				xs->error = XS_NOERROR;
				break;
			}

			/*
			 * Some devices return a short INQUIRY
			 * response, omitting response data from the
			 * "vendor specific data" on...
			 */
			if (xs->cmd.opcode == INQUIRY &&
			    residue < xs->datalen) {
				xs->error = XS_NOERROR;
				break;
			}

			xs->error = XS_DRIVER_STUFFUP;
			break;
		}
		/* FALLTHROUGH */
	case STATUS_CMD_FAILED:
		DPRINTF(UDMASS_CMD, ("umass_scsi_cb: status cmd failed for "
		    "scsi op 0x%02x\n", xs->cmd.opcode));
		/* fetch sense data */
		sc->sc_sense = 1;
		memset(&scbus->sc_sense_cmd, 0, sizeof(scbus->sc_sense_cmd));
		scbus->sc_sense_cmd.opcode = REQUEST_SENSE;
		scbus->sc_sense_cmd.byte2 = link->lun << SCSI_CMD_LUN_SHIFT;
		scbus->sc_sense_cmd.length = sizeof(xs->sense);

		cmdlen = sizeof(scbus->sc_sense_cmd);
		if (xs->flags & SCSI_POLL) {
			usbd_set_polling(sc->sc_udev, 1);
			sc->sc_xfer_flags = USBD_SYNCHRONOUS;
			sc->polled_xfer_status = USBD_INVAL;
		}
		/* scsi_done() has already been called. */
		sc->sc_methods->wire_xfer(sc, link->lun,
					  &scbus->sc_sense_cmd, cmdlen,
					  &xs->sense, sizeof(xs->sense),
					  DIR_IN, xs->timeout,
					  umass_scsi_sense_cb, xs);
		if (xs->flags & SCSI_POLL) {
			sc->sc_xfer_flags = 0;
			usbd_set_polling(sc->sc_udev, 0);
		}
		return;

	case STATUS_WIRE_FAILED:
		xs->error = XS_RESET;
		break;

	default:
		panic("%s: Unknown status %d in umass_scsi_cb",
		      sc->sc_dev.dv_xname, status);
	}

	DPRINTF(UDMASS_CMD,("umass_scsi_cb: at %lld.%06ld: return error=%d, "
			    "status=0x%x resid=%zu\n",
			    (long long)tv.tv_sec, tv.tv_usec,
			    xs->error, xs->status, xs->resid));

	if ((xs->flags & SCSI_POLL) && (xs->error == XS_NOERROR)) {
		switch (sc->polled_xfer_status) {
		case USBD_NORMAL_COMPLETION:
			xs->error = XS_NOERROR;
			break;
		case USBD_TIMEOUT:
			xs->error = XS_TIMEOUT;
			break;
		default:
			xs->error = XS_DRIVER_STUFFUP;
			break;
		}
	}

	scsi_done(xs);
}

/*
 * Finalise a completed autosense operation
 */
void
umass_scsi_sense_cb(struct umass_softc *sc, void *priv, int residue,
		    int status)
{
	struct scsi_xfer *xs = priv;

	DPRINTF(UDMASS_CMD,("umass_scsi_sense_cb: xs=%p residue=%d "
		"status=%d\n", xs, residue, status));

	sc->sc_sense = 0;
	switch (status) {
	case STATUS_CMD_OK:
	case STATUS_CMD_UNKNOWN:
		/* getting sense data succeeded */
		if (residue == 0 || residue == 14)/* XXX */
			xs->error = XS_SENSE;
		else
			xs->error = XS_SHORTSENSE;
		break;
	default:
		DPRINTF(UDMASS_SCSI, ("%s: Autosense failed, status %d\n",
			sc->sc_dev.dv_xname, status));
		xs->error = XS_DRIVER_STUFFUP;
		break;
	}

	DPRINTF(UDMASS_CMD,("umass_scsi_sense_cb: return xs->error=%d, "
		"xs->flags=0x%x xs->resid=%zu\n", xs->error, xs->status,
		xs->resid));

	if ((xs->flags & SCSI_POLL) && (xs->error == XS_NOERROR)) {
		switch (sc->polled_xfer_status) {
		case USBD_NORMAL_COMPLETION:
			xs->error = XS_NOERROR;
			break;
		case USBD_TIMEOUT:
			xs->error = XS_TIMEOUT;
			break;
		default:
			xs->error = XS_DRIVER_STUFFUP;
			break;
		}
	}

	scsi_done(xs);
}

void *
umass_io_get(void *cookie)
{
	struct umass_scsi_softc *scbus = cookie;
	void *io = NULL;
	int s;

	s = splusb();
	if (!scbus->sc_open) {
		scbus->sc_open = 1;
		io = scbus; /* just has to be non-NULL */
	}
	splx(s);

	return (io);
}

void
umass_io_put(void *cookie, void *io)
{
	struct umass_scsi_softc *scbus = cookie;
	int s;

	s = splusb();
	scbus->sc_open = 0;
	splx(s);
}
