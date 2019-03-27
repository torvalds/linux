/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
/*
 * USB Mass Storage Class Bulk-Only (BBB) Transport target.
 *
 * http://www.usb.org/developers/docs/devclass_docs/usbmassbulk_10.pdf
 *
 * This code implements the USB Mass Storage frontend driver for the CAM
 * Target Layer (ctl(4)) subsystem.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/refcount.h>
#include <sys/stdint.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include "usbdevs.h"
#include "usb_if.h"

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_da.h>
#include <cam/ctl/ctl_io.h>
#include <cam/ctl/ctl.h>
#include <cam/ctl/ctl_backend.h>
#include <cam/ctl/ctl_error.h>
#include <cam/ctl/ctl_frontend.h>
#include <cam/ctl/ctl_debug.h>
#include <cam/ctl/ctl_ha.h>
#include <cam/ctl/ctl_ioctl.h>
#include <cam/ctl/ctl_private.h>

SYSCTL_NODE(_hw_usb, OID_AUTO, cfumass, CTLFLAG_RW, 0,
    "CAM Target Layer USB Mass Storage Frontend");
static int debug = 1;
SYSCTL_INT(_hw_usb_cfumass, OID_AUTO, debug, CTLFLAG_RWTUN,
    &debug, 1, "Enable debug messages");
static int max_lun = 0;
SYSCTL_INT(_hw_usb_cfumass, OID_AUTO, max_lun, CTLFLAG_RWTUN,
    &max_lun, 1, "Maximum advertised LUN number");
static int ignore_stop = 1;
SYSCTL_INT(_hw_usb_cfumass, OID_AUTO, ignore_stop, CTLFLAG_RWTUN,
    &ignore_stop, 1, "Ignore START STOP UNIT with START and LOEJ bits cleared");

/*
 * The driver uses a single, global CTL port.  It could create its ports
 * in cfumass_attach() instead, but that would make it impossible to specify
 * "port cfumass0" in ctl.conf(5), as the port generally wouldn't exist
 * at the time ctld(8) gets run.
 */
struct ctl_port	cfumass_port;
bool		cfumass_port_online;
volatile u_int	cfumass_refcount;

#ifndef CFUMASS_BULK_SIZE 
#define	CFUMASS_BULK_SIZE	(1U << 17)	/* bytes */
#endif

/*
 * USB transfer definitions.
 */
#define	CFUMASS_T_COMMAND	0
#define	CFUMASS_T_DATA_OUT	1
#define	CFUMASS_T_DATA_IN	2
#define	CFUMASS_T_STATUS	3
#define	CFUMASS_T_MAX		4

/*
 * USB interface specific control requests.
 */
#define	UR_RESET	0xff	/* Bulk-Only Mass Storage Reset */
#define	UR_GET_MAX_LUN	0xfe	/* Get Max LUN */

/*
 * Command Block Wrapper.
 */
struct cfumass_cbw_t {
	uDWord	dCBWSignature;
#define	CBWSIGNATURE		0x43425355 /* "USBC" */
	uDWord	dCBWTag;
	uDWord	dCBWDataTransferLength;
	uByte	bCBWFlags;
#define	CBWFLAGS_OUT		0x00
#define	CBWFLAGS_IN		0x80
	uByte	bCBWLUN;
	uByte	bCDBLength;
#define	CBWCBLENGTH		16
	uByte	CBWCB[CBWCBLENGTH];
} __packed;

#define	CFUMASS_CBW_SIZE	31
CTASSERT(sizeof(struct cfumass_cbw_t) == CFUMASS_CBW_SIZE);

/*
 * Command Status Wrapper.
 */
struct cfumass_csw_t {
	uDWord	dCSWSignature;
#define	CSWSIGNATURE		0x53425355 /* "USBS" */
	uDWord	dCSWTag;
	uDWord	dCSWDataResidue;
	uByte	bCSWStatus;
#define	CSWSTATUS_GOOD		0x0
#define	CSWSTATUS_FAILED	0x1
#define	CSWSTATUS_PHASE		0x2
} __packed;

#define	CFUMASS_CSW_SIZE	13
CTASSERT(sizeof(struct cfumass_csw_t) == CFUMASS_CSW_SIZE);

struct cfumass_softc {
	device_t		sc_dev;
	struct usb_device	*sc_udev;
	struct usb_xfer		*sc_xfer[CFUMASS_T_MAX];

	struct cfumass_cbw_t *sc_cbw;
	struct cfumass_csw_t *sc_csw;

	struct mtx	sc_mtx;
	int		sc_online;
	int		sc_ctl_initid;

	/*
	 * This is used to communicate between CTL callbacks
	 * and USB callbacks; basically, it holds the state
	 * for the current command ("the" command, since there
	 * is no queueing in USB Mass Storage).
	 */
	bool		sc_current_stalled;

	/*
	 * The following are set upon receiving a SCSI command.
	 */
	int		sc_current_tag;
	int		sc_current_transfer_length;
	int		sc_current_flags;

	/*
	 * The following are set in ctl_datamove().
	 */
	int		sc_current_residue;
	union ctl_io	*sc_ctl_io;

	/*
	 * The following is set in cfumass_done().
	 */
	int		sc_current_status;

	/*
	 * Number of requests queued to CTL.
	 */
	volatile u_int	sc_queued;
};

/*
 * USB interface.
 */
static device_probe_t		cfumass_probe;
static device_attach_t		cfumass_attach;
static device_detach_t		cfumass_detach;
static device_suspend_t		cfumass_suspend;
static device_resume_t		cfumass_resume;
static usb_handle_request_t	cfumass_handle_request;

static usb_callback_t		cfumass_t_command_callback;
static usb_callback_t		cfumass_t_data_callback;
static usb_callback_t		cfumass_t_status_callback;

static device_method_t cfumass_methods[] = {

	/* USB interface. */
	DEVMETHOD(usb_handle_request, cfumass_handle_request),

	/* Device interface. */
	DEVMETHOD(device_probe, cfumass_probe),
	DEVMETHOD(device_attach, cfumass_attach),
	DEVMETHOD(device_detach, cfumass_detach),
	DEVMETHOD(device_suspend, cfumass_suspend),
	DEVMETHOD(device_resume, cfumass_resume),

	DEVMETHOD_END
};

static driver_t cfumass_driver = {
	.name = "cfumass",
	.methods = cfumass_methods,
	.size = sizeof(struct cfumass_softc),
};

static devclass_t cfumass_devclass;

DRIVER_MODULE(cfumass, uhub, cfumass_driver, cfumass_devclass, NULL, 0);
MODULE_VERSION(cfumass, 0);
MODULE_DEPEND(cfumass, usb, 1, 1, 1);
MODULE_DEPEND(cfumass, usb_template, 1, 1, 1);

static struct usb_config cfumass_config[CFUMASS_T_MAX] = {

	[CFUMASS_T_COMMAND] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = sizeof(struct cfumass_cbw_t),
		.callback = &cfumass_t_command_callback,
		.usb_mode = USB_MODE_DEVICE,
	},

	[CFUMASS_T_DATA_OUT] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = CFUMASS_BULK_SIZE,
		.flags = {.proxy_buffer = 1, .short_xfer_ok = 1,
		    .ext_buffer = 1},
		.callback = &cfumass_t_data_callback,
		.usb_mode = USB_MODE_DEVICE,
	},

	[CFUMASS_T_DATA_IN] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = CFUMASS_BULK_SIZE,
		.flags = {.proxy_buffer = 1, .short_xfer_ok = 1,
		    .ext_buffer = 1},
		.callback = &cfumass_t_data_callback,
		.usb_mode = USB_MODE_DEVICE,
	},

	[CFUMASS_T_STATUS] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = sizeof(struct cfumass_csw_t),
		.flags = {.short_xfer_ok = 1},
		.callback = &cfumass_t_status_callback,
		.usb_mode = USB_MODE_DEVICE,
	},
};

/*
 * CTL frontend interface.
 */
static int	cfumass_init(void);
static int	cfumass_shutdown(void);
static void	cfumass_online(void *arg);
static void	cfumass_offline(void *arg);
static void	cfumass_datamove(union ctl_io *io);
static void	cfumass_done(union ctl_io *io);

static struct ctl_frontend cfumass_frontend = {
	.name = "umass",
	.init = cfumass_init,
	.shutdown = cfumass_shutdown,
};
CTL_FRONTEND_DECLARE(ctlcfumass, cfumass_frontend);

#define	CFUMASS_DEBUG(S, X, ...)					\
	do {								\
		if (debug > 1) {					\
			device_printf(S->sc_dev, "%s: " X "\n",		\
			    __func__, ## __VA_ARGS__);			\
		}							\
	} while (0)

#define	CFUMASS_WARN(S, X, ...)						\
	do {								\
		if (debug > 0) {					\
			device_printf(S->sc_dev, "WARNING: %s: " X "\n",\
			    __func__, ## __VA_ARGS__);			\
		}							\
	} while (0)

#define CFUMASS_LOCK(X)		mtx_lock(&X->sc_mtx)
#define CFUMASS_UNLOCK(X)	mtx_unlock(&X->sc_mtx)

static void	cfumass_transfer_start(struct cfumass_softc *sc,
		    uint8_t xfer_index);
static void	cfumass_terminate(struct cfumass_softc *sc);

static int
cfumass_probe(device_t dev)
{
	struct usb_attach_arg *uaa;
	struct usb_interface_descriptor *id;

	uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_DEVICE)
		return (ENXIO);

	/*
	 * Check for a compliant device.
	 */
	id = usbd_get_interface_descriptor(uaa->iface);
	if ((id == NULL) ||
	    (id->bInterfaceClass != UICLASS_MASS) ||
	    (id->bInterfaceSubClass != UISUBCLASS_SCSI) ||
	    (id->bInterfaceProtocol != UIPROTO_MASS_BBB)) {
		return (ENXIO);
	}

	return (BUS_PROBE_GENERIC);
}

static int
cfumass_attach(device_t dev)
{
	struct cfumass_softc *sc;
	struct usb_attach_arg *uaa;
	int error;

	sc = device_get_softc(dev);
	uaa = device_get_ivars(dev);

	sc->sc_dev = dev;
	sc->sc_udev = uaa->device;

	CFUMASS_DEBUG(sc, "go");

	usbd_set_power_mode(uaa->device, USB_POWER_MODE_SAVE);
	device_set_usb_desc(dev);

	mtx_init(&sc->sc_mtx, "cfumass", NULL, MTX_DEF);
	refcount_acquire(&cfumass_refcount);

	error = usbd_transfer_setup(uaa->device,
	    &uaa->info.bIfaceIndex, sc->sc_xfer, cfumass_config,
	    CFUMASS_T_MAX, sc, &sc->sc_mtx);
	if (error != 0) {
		CFUMASS_WARN(sc, "usbd_transfer_setup() failed: %s",
		    usbd_errstr(error));
		refcount_release(&cfumass_refcount);
		return (ENXIO);
	}

	sc->sc_cbw =
	    usbd_xfer_get_frame_buffer(sc->sc_xfer[CFUMASS_T_COMMAND], 0);
	sc->sc_csw =
	    usbd_xfer_get_frame_buffer(sc->sc_xfer[CFUMASS_T_STATUS], 0);

	sc->sc_ctl_initid = ctl_add_initiator(&cfumass_port, -1, 0, NULL);
	if (sc->sc_ctl_initid < 0) {
		CFUMASS_WARN(sc, "ctl_add_initiator() failed with error %d",
		    sc->sc_ctl_initid);
		usbd_transfer_unsetup(sc->sc_xfer, CFUMASS_T_MAX);
		refcount_release(&cfumass_refcount);
		return (ENXIO);
	}

	refcount_init(&sc->sc_queued, 0);

	CFUMASS_LOCK(sc);
	cfumass_transfer_start(sc, CFUMASS_T_COMMAND);
	CFUMASS_UNLOCK(sc);

	return (0);
}

static int
cfumass_detach(device_t dev)
{
	struct cfumass_softc *sc;
	int error;

	sc = device_get_softc(dev);

	CFUMASS_DEBUG(sc, "go");

	CFUMASS_LOCK(sc);
	cfumass_terminate(sc);
	CFUMASS_UNLOCK(sc);
	usbd_transfer_unsetup(sc->sc_xfer, CFUMASS_T_MAX);

	if (sc->sc_ctl_initid != -1) {
		error = ctl_remove_initiator(&cfumass_port, sc->sc_ctl_initid);
		if (error != 0) {
			CFUMASS_WARN(sc, "ctl_remove_initiator() failed "
			    "with error %d", error);
		}
		sc->sc_ctl_initid = -1;
	}

	mtx_destroy(&sc->sc_mtx);
	refcount_release(&cfumass_refcount);

	return (0);
}

static int
cfumass_suspend(device_t dev)
{
	struct cfumass_softc *sc;

	sc = device_get_softc(dev);
	CFUMASS_DEBUG(sc, "go");

	return (0);
}

static int
cfumass_resume(device_t dev)
{
	struct cfumass_softc *sc;

	sc = device_get_softc(dev);
	CFUMASS_DEBUG(sc, "go");

	return (0);
}

static void
cfumass_transfer_start(struct cfumass_softc *sc, uint8_t xfer_index)
{

	usbd_transfer_start(sc->sc_xfer[xfer_index]);
}

static void
cfumass_transfer_stop_and_drain(struct cfumass_softc *sc, uint8_t xfer_index)
{

	usbd_transfer_stop(sc->sc_xfer[xfer_index]);
	CFUMASS_UNLOCK(sc);
	usbd_transfer_drain(sc->sc_xfer[xfer_index]);
	CFUMASS_LOCK(sc);
}

static void
cfumass_terminate(struct cfumass_softc *sc)
{
	int last;

	for (;;) {
		cfumass_transfer_stop_and_drain(sc, CFUMASS_T_COMMAND);
		cfumass_transfer_stop_and_drain(sc, CFUMASS_T_DATA_IN);
		cfumass_transfer_stop_and_drain(sc, CFUMASS_T_DATA_OUT);

		if (sc->sc_ctl_io != NULL) {
			CFUMASS_DEBUG(sc, "terminating CTL transfer");
			ctl_set_data_phase_error(&sc->sc_ctl_io->scsiio);
			sc->sc_ctl_io->scsiio.be_move_done(sc->sc_ctl_io);
			sc->sc_ctl_io = NULL;
		}

		cfumass_transfer_stop_and_drain(sc, CFUMASS_T_STATUS);

		refcount_acquire(&sc->sc_queued);
		last = refcount_release(&sc->sc_queued);
		if (last != 0)
			break;

		CFUMASS_DEBUG(sc, "%d CTL tasks pending", sc->sc_queued);
		msleep(__DEVOLATILE(void *, &sc->sc_queued), &sc->sc_mtx,
		    0, "cfumass_reset", hz / 100);
	}
}

static int
cfumass_handle_request(device_t dev,
    const void *preq, void **pptr, uint16_t *plen,
    uint16_t offset, uint8_t *pstate)
{
	static uint8_t max_lun_tmp;
	struct cfumass_softc *sc;
	const struct usb_device_request *req;
	uint8_t is_complete;

	sc = device_get_softc(dev);
	req = preq;
	is_complete = *pstate;

	CFUMASS_DEBUG(sc, "go");

	if (is_complete)
		return (ENXIO);

	if ((req->bmRequestType == UT_WRITE_CLASS_INTERFACE) &&
	    (req->bRequest == UR_RESET)) {
		CFUMASS_WARN(sc, "received Bulk-Only Mass Storage Reset");
		*plen = 0;

		CFUMASS_LOCK(sc);
		cfumass_terminate(sc);
		cfumass_transfer_start(sc, CFUMASS_T_COMMAND);
		CFUMASS_UNLOCK(sc);

		CFUMASS_DEBUG(sc, "Bulk-Only Mass Storage Reset done");
		return (0);
	}

	if ((req->bmRequestType == UT_READ_CLASS_INTERFACE) &&
	    (req->bRequest == UR_GET_MAX_LUN)) {
		CFUMASS_DEBUG(sc, "received Get Max LUN");
		if (offset == 0) {
			*plen = 1;
			/*
			 * The protocol doesn't support LUN numbers higher
			 * than 15.  Also, some initiators (namely Windows XP
			 * SP3 Version 2002) can't properly query the number
			 * of LUNs, resulting in inaccessible "fake" ones - thus
			 * the default limit of one LUN.
			 */
			if (max_lun < 0 || max_lun > 15) {
				CFUMASS_WARN(sc,
				    "invalid hw.usb.cfumass.max_lun, must be "
				    "between 0 and 15; defaulting to 0");
				max_lun_tmp = 0;
			} else {
				max_lun_tmp = max_lun;
			}
			*pptr = &max_lun_tmp;
		} else {
			*plen = 0;
		}
		return (0);
	}

	return (ENXIO);
}

static int
cfumass_quirk(struct cfumass_softc *sc, unsigned char *cdb, int cdb_len)
{
	struct scsi_start_stop_unit *sssu;

	switch (cdb[0]) {
	case START_STOP_UNIT:
		/*
		 * Some initiators - eg OSX, Darwin Kernel Version 15.6.0,
		 * root:xnu-3248.60.11~2/RELEASE_X86_64 - attempt to stop
		 * the unit on eject, but fail to start it when it's plugged
		 * back.  Just ignore the command.
		 */

		if (cdb_len < sizeof(*sssu)) {
			CFUMASS_DEBUG(sc, "received START STOP UNIT with "
			    "bCDBLength %d, should be %zd",
			    cdb_len, sizeof(*sssu));
			break;
		}

		sssu = (struct scsi_start_stop_unit *)cdb;
		if ((sssu->how & SSS_PC_MASK) != 0)
			break;

		if ((sssu->how & SSS_START) != 0)
			break;

		if ((sssu->how & SSS_LOEJ) != 0)
			break;
		
		if (ignore_stop == 0) {
			break;
		} else if (ignore_stop == 1) {
			CFUMASS_WARN(sc, "ignoring START STOP UNIT request");
		} else {
			CFUMASS_DEBUG(sc, "ignoring START STOP UNIT request");
		}

		sc->sc_current_status = 0;
		cfumass_transfer_start(sc, CFUMASS_T_STATUS);

		return (1);
	default:
		break;
	}

	return (0);
}

static void
cfumass_t_command_callback(struct usb_xfer *xfer, usb_error_t usb_error)
{
	struct cfumass_softc *sc;
	uint32_t signature;
	union ctl_io *io;
	int error = 0;

	sc = usbd_xfer_softc(xfer);

	KASSERT(sc->sc_ctl_io == NULL,
	    ("sc_ctl_io is %p, should be NULL", sc->sc_ctl_io));

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		CFUMASS_DEBUG(sc, "USB_ST_TRANSFERRED");

		signature = UGETDW(sc->sc_cbw->dCBWSignature);
		if (signature != CBWSIGNATURE) {
			CFUMASS_WARN(sc, "wrong dCBWSignature 0x%08x, "
			    "should be 0x%08x", signature, CBWSIGNATURE);
			break;
		}

		if (sc->sc_cbw->bCDBLength <= 0 ||
		    sc->sc_cbw->bCDBLength > sizeof(sc->sc_cbw->CBWCB)) {
			CFUMASS_WARN(sc, "invalid bCDBLength %d, should be <= %zd",
			    sc->sc_cbw->bCDBLength, sizeof(sc->sc_cbw->CBWCB));
			break;
		}

		sc->sc_current_stalled = false;
		sc->sc_current_status = 0;
		sc->sc_current_tag = UGETDW(sc->sc_cbw->dCBWTag);
		sc->sc_current_transfer_length =
		    UGETDW(sc->sc_cbw->dCBWDataTransferLength);
		sc->sc_current_flags = sc->sc_cbw->bCBWFlags;

		/*
		 * Make sure to report proper residue if the datamove wasn't
		 * required, or wasn't called due to SCSI error.
		 */
		sc->sc_current_residue = sc->sc_current_transfer_length;

		if (cfumass_quirk(sc,
		    sc->sc_cbw->CBWCB, sc->sc_cbw->bCDBLength) != 0)
			break;

		if (!cfumass_port_online) {
			CFUMASS_DEBUG(sc, "cfumass port is offline; stalling");
			usbd_xfer_set_stall(xfer);
			break;
		}

		/*
		 * Those CTL functions cannot be called with mutex held.
		 */
		CFUMASS_UNLOCK(sc);
		io = ctl_alloc_io(cfumass_port.ctl_pool_ref);
		ctl_zero_io(io);
		io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr = sc;
		io->io_hdr.io_type = CTL_IO_SCSI;
		io->io_hdr.nexus.initid = sc->sc_ctl_initid;
		io->io_hdr.nexus.targ_port = cfumass_port.targ_port;
		io->io_hdr.nexus.targ_lun = ctl_decode_lun(sc->sc_cbw->bCBWLUN);
		io->scsiio.tag_num = UGETDW(sc->sc_cbw->dCBWTag);
		io->scsiio.tag_type = CTL_TAG_UNTAGGED;
		io->scsiio.cdb_len = sc->sc_cbw->bCDBLength;
		memcpy(io->scsiio.cdb, sc->sc_cbw->CBWCB, sc->sc_cbw->bCDBLength);
		refcount_acquire(&sc->sc_queued);
		error = ctl_queue(io);
		if (error != CTL_RETVAL_COMPLETE) {
			CFUMASS_WARN(sc,
			    "ctl_queue() failed; error %d; stalling", error);
			ctl_free_io(io);
			refcount_release(&sc->sc_queued);
			CFUMASS_LOCK(sc);
			usbd_xfer_set_stall(xfer);
			break;
		}

		CFUMASS_LOCK(sc);
		break;

	case USB_ST_SETUP:
tr_setup:
		CFUMASS_DEBUG(sc, "USB_ST_SETUP");

		usbd_xfer_set_frame_len(xfer, 0, sizeof(*sc->sc_cbw));
		usbd_transfer_submit(xfer);
		break;

	default:
		if (usb_error == USB_ERR_CANCELLED) {
			CFUMASS_DEBUG(sc, "USB_ERR_CANCELLED");
			break;
		}

		CFUMASS_DEBUG(sc, "USB_ST_ERROR: %s", usbd_errstr(usb_error));

		goto tr_setup;
	}
}

static void
cfumass_t_data_callback(struct usb_xfer *xfer, usb_error_t usb_error)
{
	struct cfumass_softc *sc = usbd_xfer_softc(xfer);
	union ctl_io *io = sc->sc_ctl_io;
	uint32_t max_bulk;
	struct ctl_sg_entry sg_entry, *sglist;
	int actlen, sumlen, sg_count;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		CFUMASS_DEBUG(sc, "USB_ST_TRANSFERRED");

		usbd_xfer_status(xfer, &actlen, &sumlen, NULL, NULL);
		sc->sc_current_residue -= actlen;
		io->scsiio.ext_data_filled += actlen;
		io->scsiio.kern_data_resid -= actlen;
		if (actlen < sumlen ||
		    sc->sc_current_residue == 0 ||
		    io->scsiio.kern_data_resid == 0) {
			sc->sc_ctl_io = NULL;
			io->scsiio.be_move_done(io);
			break;
		}
		/* FALLTHROUGH */

	case USB_ST_SETUP:
tr_setup:
		CFUMASS_DEBUG(sc, "USB_ST_SETUP");

		if (io->scsiio.kern_sg_entries > 0) {
			sglist = (struct ctl_sg_entry *)io->scsiio.kern_data_ptr;
			sg_count = io->scsiio.kern_sg_entries;
		} else {
			sglist = &sg_entry;
			sglist->addr = io->scsiio.kern_data_ptr;
			sglist->len = io->scsiio.kern_data_len;
			sg_count = 1;
		}

		sumlen = io->scsiio.ext_data_filled -
		    io->scsiio.kern_rel_offset;
		while (sumlen >= sglist->len && sg_count > 0) {
			sumlen -= sglist->len;
			sglist++;
			sg_count--;
		}
		KASSERT(sg_count > 0, ("Run out of S/G list entries"));

		max_bulk = usbd_xfer_max_len(xfer);
		actlen = min(sglist->len - sumlen, max_bulk);
		actlen = min(actlen, sc->sc_current_transfer_length -
		    io->scsiio.ext_data_filled);
		CFUMASS_DEBUG(sc, "requested %d, done %d, max_bulk %d, "
		    "segment %zd => transfer %d",
		    sc->sc_current_transfer_length, io->scsiio.ext_data_filled,
		    max_bulk, sglist->len - sumlen, actlen);

		usbd_xfer_set_frame_data(xfer, 0,
		    (uint8_t *)sglist->addr + sumlen, actlen);
		usbd_transfer_submit(xfer);
		break;

	default:
		if (usb_error == USB_ERR_CANCELLED) {
			CFUMASS_DEBUG(sc, "USB_ERR_CANCELLED");
			break;
		}
		CFUMASS_DEBUG(sc, "USB_ST_ERROR: %s", usbd_errstr(usb_error));
		goto tr_setup;
	}
}

static void
cfumass_t_status_callback(struct usb_xfer *xfer, usb_error_t usb_error)
{
	struct cfumass_softc *sc;

	sc = usbd_xfer_softc(xfer);

	KASSERT(sc->sc_ctl_io == NULL,
	    ("sc_ctl_io is %p, should be NULL", sc->sc_ctl_io));

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		CFUMASS_DEBUG(sc, "USB_ST_TRANSFERRED");

		cfumass_transfer_start(sc, CFUMASS_T_COMMAND);
		break;

	case USB_ST_SETUP:
tr_setup:
		CFUMASS_DEBUG(sc, "USB_ST_SETUP");

		if (sc->sc_current_residue > 0 && !sc->sc_current_stalled) {
			CFUMASS_DEBUG(sc, "non-zero residue, stalling");
			usbd_xfer_set_stall(xfer);
			sc->sc_current_stalled = true;
		}

		USETDW(sc->sc_csw->dCSWSignature, CSWSIGNATURE);
		USETDW(sc->sc_csw->dCSWTag, sc->sc_current_tag);
		USETDW(sc->sc_csw->dCSWDataResidue, sc->sc_current_residue);
		sc->sc_csw->bCSWStatus = sc->sc_current_status;

		usbd_xfer_set_frame_len(xfer, 0, sizeof(*sc->sc_csw));
		usbd_transfer_submit(xfer);
		break;

	default:
		if (usb_error == USB_ERR_CANCELLED) {
			CFUMASS_DEBUG(sc, "USB_ERR_CANCELLED");
			break;
		}

		CFUMASS_DEBUG(sc, "USB_ST_ERROR: %s",
		    usbd_errstr(usb_error));

		goto tr_setup;
	}
}

static void
cfumass_online(void *arg __unused)
{

	cfumass_port_online = true;
}

static void
cfumass_offline(void *arg __unused)
{

	cfumass_port_online = false;
}

static void
cfumass_datamove(union ctl_io *io)
{
	struct cfumass_softc *sc;

	sc = io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr;

	CFUMASS_DEBUG(sc, "go");

	CFUMASS_LOCK(sc);

	KASSERT(sc->sc_ctl_io == NULL,
	    ("sc_ctl_io is %p, should be NULL", sc->sc_ctl_io));
	sc->sc_ctl_io = io;

	if ((io->io_hdr.flags & CTL_FLAG_DATA_MASK) == CTL_FLAG_DATA_IN) {
		/*
		 * Verify that CTL wants us to send the data in the direction
		 * expected by the initiator.
		 */
		if (sc->sc_current_flags != CBWFLAGS_IN) {
			CFUMASS_WARN(sc, "wrong bCBWFlags 0x%x, should be 0x%x",
			    sc->sc_current_flags, CBWFLAGS_IN);
			goto fail;
		}

		cfumass_transfer_start(sc, CFUMASS_T_DATA_IN);
	} else {
		if (sc->sc_current_flags != CBWFLAGS_OUT) {
			CFUMASS_WARN(sc, "wrong bCBWFlags 0x%x, should be 0x%x",
			    sc->sc_current_flags, CBWFLAGS_OUT);
			goto fail;
		}

		cfumass_transfer_start(sc, CFUMASS_T_DATA_OUT);
	}

	CFUMASS_UNLOCK(sc);
	return;

fail:
	ctl_set_data_phase_error(&io->scsiio);
	io->scsiio.be_move_done(io);
	sc->sc_ctl_io = NULL;
}

static void
cfumass_done(union ctl_io *io)
{
	struct cfumass_softc *sc;

	sc = io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr;

	CFUMASS_DEBUG(sc, "go");

	KASSERT(((io->io_hdr.status & CTL_STATUS_MASK) != CTL_STATUS_NONE),
	    ("invalid CTL status %#x", io->io_hdr.status));
	KASSERT(sc->sc_ctl_io == NULL,
	    ("sc_ctl_io is %p, should be NULL", sc->sc_ctl_io));

	if (io->io_hdr.io_type == CTL_IO_TASK &&
	    io->taskio.task_action == CTL_TASK_I_T_NEXUS_RESET) {
		/*
		 * Implicit task termination has just completed; nothing to do.
		 */
		ctl_free_io(io);
		return;
	}

	/*
	 * Do not return status for aborted commands.
	 * There are exceptions, but none supported by CTL yet.
	 */
	if (((io->io_hdr.flags & CTL_FLAG_ABORT) &&
	     (io->io_hdr.flags & CTL_FLAG_ABORT_STATUS) == 0) ||
	    (io->io_hdr.flags & CTL_FLAG_STATUS_SENT)) {
		ctl_free_io(io);
		return;
	}

	if ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SUCCESS)
		sc->sc_current_status = 0;
	else
		sc->sc_current_status = 1;

	/* XXX: How should we report BUSY, RESERVATION CONFLICT, etc? */
	if ((io->io_hdr.status & CTL_STATUS_MASK) == CTL_SCSI_ERROR &&
	    io->scsiio.scsi_status == SCSI_STATUS_CHECK_COND)
		ctl_queue_sense(io);
	else
		ctl_free_io(io);

	CFUMASS_LOCK(sc);
	cfumass_transfer_start(sc, CFUMASS_T_STATUS);
	CFUMASS_UNLOCK(sc);

	refcount_release(&sc->sc_queued);
}

int
cfumass_init(void)
{
	int error;

	cfumass_port.frontend = &cfumass_frontend;
	cfumass_port.port_type = CTL_PORT_UMASS;
	cfumass_port.num_requested_ctl_io = 1;
	cfumass_port.port_name = "cfumass";
	cfumass_port.physical_port = 0;
	cfumass_port.virtual_port = 0;
	cfumass_port.port_online = cfumass_online;
	cfumass_port.port_offline = cfumass_offline;
	cfumass_port.onoff_arg = NULL;
	cfumass_port.fe_datamove = cfumass_datamove;
	cfumass_port.fe_done = cfumass_done;
	cfumass_port.targ_port = -1;

	error = ctl_port_register(&cfumass_port);
	if (error != 0) {
		printf("%s: ctl_port_register() failed "
		    "with error %d", __func__, error);
	}

	cfumass_port_online = true;
	refcount_init(&cfumass_refcount, 0);

	return (error);
}

int
cfumass_shutdown(void)
{
	int error;

	if (cfumass_refcount > 0) {
		if (debug > 1) {
			printf("%s: still have %u attachments; "
			    "returning EBUSY\n", __func__, cfumass_refcount);
		}
		return (EBUSY);
	}

	error = ctl_port_deregister(&cfumass_port);
	if (error != 0) {
		printf("%s: ctl_port_deregister() failed "
		    "with error %d\n", __func__, error);
	}

	return (error);
}
