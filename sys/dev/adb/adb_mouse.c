/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2008 Nathan Whitehorn
 * All rights reserved.
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/mouse.h>
#include <sys/poll.h>
#include <sys/condvar.h>
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>

#include <machine/bus.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include "adb.h"

#define CDEV_GET_SOFTC(x) (x)->si_drv1

static int adb_mouse_probe(device_t dev);
static int adb_mouse_attach(device_t dev);
static int adb_mouse_detach(device_t dev);
static void adb_init_trackpad(device_t dev);
static int adb_tapping_sysctl(SYSCTL_HANDLER_ARGS);

static d_open_t  ams_open;
static d_close_t ams_close;
static d_read_t  ams_read;
static d_ioctl_t ams_ioctl;
static d_poll_t  ams_poll;

static u_int adb_mouse_receive_packet(device_t dev, u_char status, 
    u_char command, u_char reg, int len, u_char *data);

struct adb_mouse_softc {
	device_t sc_dev;

	struct mtx sc_mtx;
	struct cv  sc_cv;

	int flags;
#define	AMS_EXTENDED	0x1
#define	AMS_TOUCHPAD	0x2
	uint16_t dpi;

	mousehw_t hw;
	mousemode_t mode;
	u_char id[4];

	int buttons;
	u_int sc_tapping;
	int button_buf;
	int last_buttons;
	int xdelta, ydelta;

	int8_t packet[8];
	size_t packet_read_len;

	struct cdev *cdev;
	struct selinfo rsel;
};

static device_method_t adb_mouse_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,         adb_mouse_probe),
        DEVMETHOD(device_attach,        adb_mouse_attach),
        DEVMETHOD(device_detach,        adb_mouse_detach),
        DEVMETHOD(device_shutdown,      bus_generic_shutdown),
        DEVMETHOD(device_suspend,       bus_generic_suspend),
        DEVMETHOD(device_resume,        bus_generic_resume),

	/* ADB interface */
	DEVMETHOD(adb_receive_packet,	adb_mouse_receive_packet),

	{ 0, 0 }
};

static driver_t adb_mouse_driver = {
	"ams",
	adb_mouse_methods,
	sizeof(struct adb_mouse_softc),
};

static devclass_t adb_mouse_devclass;

DRIVER_MODULE(ams, adb, adb_mouse_driver, adb_mouse_devclass, 0, 0);

static struct cdevsw ams_cdevsw = {
	.d_version = 	D_VERSION,
	.d_flags   = 	0,
	.d_open    =	ams_open,
	.d_close   =	ams_close,
	.d_read    =	ams_read,
	.d_ioctl   =	ams_ioctl,
	.d_poll    =	ams_poll,
	.d_name    =	"ams",
};

static int 
adb_mouse_probe(device_t dev)
{
	uint8_t type;

	type = adb_get_device_type(dev);

	if (type != ADB_DEVICE_MOUSE)
		return (ENXIO);

	device_set_desc(dev,"ADB Mouse");
	return (0);
}
	
static int 
adb_mouse_attach(device_t dev) 
{
	struct adb_mouse_softc *sc;
	char *description = "Unknown Pointing Device";

	size_t r1_len;
	u_char r1[8];

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	mtx_init(&sc->sc_mtx, "ams", NULL, MTX_DEF);
	cv_init(&sc->sc_cv,"ams");

	sc->flags = 0;

	sc->hw.buttons = 2;
	sc->hw.iftype = MOUSE_IF_UNKNOWN;
	sc->hw.type = MOUSE_UNKNOWN;
	sc->hw.model = sc->hw.hwid = 0;

	sc->mode.protocol = MOUSE_PROTO_SYSMOUSE;
	sc->mode.rate = -1;
	sc->mode.resolution = 100;
	sc->mode.accelfactor = 0;
	sc->mode.level = 0;
	sc->mode.packetsize = 5;

	sc->buttons = 0;
	sc->sc_tapping = 0;
	sc->button_buf = 0;
	sc->last_buttons = 0;
	sc->packet_read_len = 0;

	/* Try to switch to extended protocol */
	adb_set_device_handler(dev,4);

	switch(adb_get_device_handler(dev)) {
	case 1:
		sc->mode.resolution = 100;
		break;
	case 2:
		sc->mode.resolution = 200;
		break;
	case 4:
		r1_len = adb_read_register(dev,1,r1);
		if (r1_len < 8)
			break;

		sc->flags |= AMS_EXTENDED;
		memcpy(&sc->hw.hwid,r1,4);
		sc->mode.resolution = (r1[4] << 8) | r1[5];

		switch (r1[6]) {
		case 0:
			sc->hw.type = MOUSE_PAD;
			description = "Tablet";
			break;
		case 1:
			sc->hw.type = MOUSE_MOUSE;
			description = "Mouse";
			break;
		case 2:
			sc->hw.type = MOUSE_TRACKBALL;
			description = "Trackball";
			break;
		case 3:
			sc->flags |= AMS_TOUCHPAD;
			sc->hw.type = MOUSE_PAD;
			adb_init_trackpad(dev);
			description = "Touchpad";
			break;
		}

		sc->hw.buttons = r1[7];

		device_printf(dev,"%d-button %d-dpi %s\n",
		    sc->hw.buttons, sc->mode.resolution,description);

		/*
		 * Check for one of MacAlly's non-compliant 2-button mice.
		 * These claim to speak the extended mouse protocol, but
		 * instead speak the standard protocol and only when their
		 * handler is set to 0x42.
		 */

		if (sc->hw.hwid == 0x4b4f4954) {
			adb_set_device_handler(dev,0x42);

			if (adb_get_device_handler(dev) == 0x42) {
				device_printf(dev, "MacAlly 2-Button Mouse\n");
				sc->flags &= ~AMS_EXTENDED;
			}
		}
			
		break;
	}

	sc->cdev = make_dev(&ams_cdevsw, device_get_unit(dev),
		       UID_ROOT, GID_OPERATOR, 0644, "ams%d", 
		       device_get_unit(dev));
	sc->cdev->si_drv1 = sc;

	adb_set_autopoll(dev,1);

	return (0);
}

static int 
adb_mouse_detach(device_t dev) 
{
	struct adb_mouse_softc *sc;

	adb_set_autopoll(dev,0);

	sc = device_get_softc(dev);
	destroy_dev(sc->cdev);

	mtx_destroy(&sc->sc_mtx);
	cv_destroy(&sc->sc_cv);

	return (0);
}

static void
adb_init_trackpad(device_t dev)
{
	struct adb_mouse_softc *sc;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;

	size_t r1_len;
	u_char r1[8];
	u_char r2[8];

	sc = device_get_softc(dev);

	r1_len = adb_read_register(dev, 1, r1);

	/* An Extended Mouse register1 must return 8 bytes. */
	if (r1_len != 8)
		return;

	if((r1[6] != 0x0d))
	{
		r1[6] = 0x0d;
		
		adb_write_register(dev, 1, 8, r1); 
      
		r1_len = adb_read_register(dev, 1, r1);
      
		if (r1[6] != 0x0d)
		{
			device_printf(dev, "ADB Mouse = 0x%x "
				      "(non-Extended Mode)\n", r1[6]);
			return;
		} else {
			device_printf(dev, "ADB Mouse = 0x%x "
				      "(Extended Mode)\n", r1[6]);
			
			/* Set ADB Extended Features to default values,
			   enabled. */
			r2[0] = 0x19; /* Clicking: 0x19 disabled 0x99 enabled */
			r2[1] = 0x94; /* Dragging: 0x14 disabled 0x94 enabled */
			r2[2] = 0x19;
			r2[3] = 0xff; /* DragLock: 0xff disabled 0xb2 enabled */
			r2[4] = 0xb2;
			r2[5] = 0x8a;
			r2[6] = 0x1b;
		       
			r2[7] = 0x57;  /* 0x57 bits 3:0 for W mode */
			
			adb_write_register(dev, 2, 8, r2);
			
		}
	}

	/*
	 * Set up sysctl
	 */
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "tapping",
			CTLTYPE_INT | CTLFLAG_RW, sc, 0, adb_tapping_sysctl,
			"I", "Tapping the pad causes button events");
	return;
}

static u_int 
adb_mouse_receive_packet(device_t dev, u_char status, u_char command, 
    u_char reg, int len, u_char *data) 
{
	struct adb_mouse_softc *sc;
	int i = 0;
	int xdelta, ydelta;
	int buttons, tmp_buttons;

	sc = device_get_softc(dev);

	if (command != ADB_COMMAND_TALK || reg != 0 || len < 2)
		return (0);

	ydelta = data[0] & 0x7f;
	xdelta = data[1] & 0x7f;

	buttons = 0;
	buttons |= !(data[0] & 0x80);
	buttons |= !(data[1] & 0x80) << 1;

	if (sc->flags & AMS_EXTENDED) {
		for (i = 2; i < len && i < 5; i++) {
			xdelta |= (data[i] & 0x07) << (3*i + 1);
			ydelta |= (data[i] & 0x70) << (3*i - 3);

			buttons |= !(data[i] & 0x08) << (2*i - 2);
			buttons |= !(data[i] & 0x80) << (2*i - 1);
		}
	} else {
		len = 2; /* Ignore extra data */
	}

	/* Do sign extension as necessary */
	if (xdelta & (0x40 << 3*(len-2)))
		xdelta |= 0xffffffc0 << 3*(len - 2);
	if (ydelta & (0x40 << 3*(len-2)))
		ydelta |= 0xffffffc0 << 3*(len - 2);

	if ((sc->flags & AMS_TOUCHPAD) && (sc->sc_tapping == 1)) {
		tmp_buttons = buttons;
		if (buttons == 0x12) {
			/* Map a double tap on button 3.
			   Keep the button state for the next sequence.
			   A double tap sequence is followed by a single tap
			   sequence.
			*/
			tmp_buttons = 0x3;
			sc->button_buf = tmp_buttons;
		} else if (buttons == 0x2) {
			/* Map a single tap on button 2. But only if it is
			   not a successor from a double tap.
			*/
			if (sc->button_buf != 0x3) 
				tmp_buttons = 0x2;
			else
				tmp_buttons = 0;

			sc->button_buf = 0;
		}
		buttons = tmp_buttons;
	}

	/*
	 * Some mice report high-numbered buttons on the wrong button number,
	 * so set the highest-numbered real button as pressed if there are
	 * mysterious high-numbered ones set.
	 *
	 * Don't do this for touchpads, because touchpads also trigger
	 * high button events when they are touched.
	 */

	if (rounddown2(buttons, 1 << sc->hw.buttons)
	    && !(sc->flags & AMS_TOUCHPAD)) {
		buttons |= 1 << (sc->hw.buttons - 1);
	}
	buttons &= (1 << sc->hw.buttons) - 1;

	mtx_lock(&sc->sc_mtx);

	/* Add in our new deltas, and take into account
	   Apple's opposite meaning for Y axis motion */

	sc->xdelta += xdelta;
	sc->ydelta -= ydelta;

	sc->buttons = buttons;

	mtx_unlock(&sc->sc_mtx);

	cv_broadcast(&sc->sc_cv);
	selwakeuppri(&sc->rsel, PZERO);

	return (0);
}

static int
ams_open(struct cdev *dev, int flag, int fmt, struct thread *p)
{
	struct adb_mouse_softc *sc;

	sc = CDEV_GET_SOFTC(dev);
	if (sc == NULL)
		return (ENXIO);

	mtx_lock(&sc->sc_mtx);
	sc->packet_read_len = 0;
	sc->xdelta = 0;
	sc->ydelta = 0;
	sc->buttons = 0;
	mtx_unlock(&sc->sc_mtx);

	return (0);
}

static int
ams_close(struct cdev *dev, int flag, int fmt, struct thread *p)
{
	struct adb_mouse_softc *sc;

	sc = CDEV_GET_SOFTC(dev);

	cv_broadcast(&sc->sc_cv);
	selwakeuppri(&sc->rsel, PZERO);
	return (0);
}

static int
ams_poll(struct cdev *dev, int events, struct thread *p)
{
	struct adb_mouse_softc *sc;

	sc = CDEV_GET_SOFTC(dev);
	if (sc == NULL)
		return (EIO);

	if (events & (POLLIN | POLLRDNORM)) {
		mtx_lock(&sc->sc_mtx);
		
		if (sc->xdelta == 0 && sc->ydelta == 0 && 
		   sc->buttons == sc->last_buttons &&
		   sc->packet_read_len == 0) {
			selrecord(p, &sc->rsel);
			events = 0;
		} else {
			events &= (POLLIN | POLLRDNORM);
		}

		mtx_unlock(&sc->sc_mtx);
	}

	return events;
}

static int
ams_read(struct cdev *dev, struct uio *uio, int flag)
{
	struct adb_mouse_softc *sc;
	size_t len;
	int8_t outpacket[8];
	int error;

	sc = CDEV_GET_SOFTC(dev);
	if (sc == NULL)
		return (EIO);

	if (uio->uio_resid <= 0)
		return (0);

	mtx_lock(&sc->sc_mtx);

	if (!sc->packet_read_len) {
		if (sc->xdelta == 0 && sc->ydelta == 0 && 
		   sc->buttons == sc->last_buttons) {

			if (flag & O_NONBLOCK) {
				mtx_unlock(&sc->sc_mtx);
				return EWOULDBLOCK;
			}

	
			/* Otherwise, block on new data */
			error = cv_wait_sig(&sc->sc_cv, &sc->sc_mtx);
			if (error) {
				mtx_unlock(&sc->sc_mtx);
				return (error);
			}
		}

		sc->packet[0] = 1U << 7;
		sc->packet[0] |= (!(sc->buttons & 1)) << 2;
		sc->packet[0] |= (!(sc->buttons & 4)) << 1;
		sc->packet[0] |= (!(sc->buttons & 2));

		if (sc->xdelta > 127) {
			sc->packet[1] = 127;
			sc->packet[3] = sc->xdelta - 127;
		} else if (sc->xdelta < -127) {
			sc->packet[1] = -127;
			sc->packet[3] = sc->xdelta + 127;
		} else {
			sc->packet[1] = sc->xdelta;
			sc->packet[3] = 0;
		}

		if (sc->ydelta > 127) {
			sc->packet[2] = 127;
			sc->packet[4] = sc->ydelta - 127;
		} else if (sc->ydelta < -127) {
			sc->packet[2] = -127;
			sc->packet[4] = sc->ydelta + 127;
		} else {
			sc->packet[2] = sc->ydelta;
			sc->packet[4] = 0;
		}

		/* No Z movement */
		sc->packet[5] = 0;
		sc->packet[6] = 0; 

		sc->packet[7] = ~((uint8_t)(sc->buttons >> 3)) & 0x7f;


		sc->last_buttons = sc->buttons;
		sc->xdelta = 0;
		sc->ydelta = 0;

		sc->packet_read_len = sc->mode.packetsize;
	}

	len = (sc->packet_read_len > uio->uio_resid) ? 
		uio->uio_resid : sc->packet_read_len;

	memcpy(outpacket,sc->packet + 
		(sc->mode.packetsize - sc->packet_read_len),len);
	sc->packet_read_len -= len;

	mtx_unlock(&sc->sc_mtx);

	error = uiomove(outpacket,len,uio);

	return (error);
}


static int
ams_ioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flag, 
    struct thread *p)
{
	struct adb_mouse_softc *sc;
	mousemode_t mode;

	sc = CDEV_GET_SOFTC(dev);
	if (sc == NULL)
		return (EIO);

	switch (cmd) {
	case MOUSE_GETHWINFO:
		*(mousehw_t *)addr = sc->hw;
		break;
	case MOUSE_GETMODE:
		*(mousemode_t *)addr = sc->mode;
		break;
	case MOUSE_SETMODE:
		mode = *(mousemode_t *)addr;
		addr = (caddr_t)&mode.level;

		/* Fallthrough */

	case MOUSE_SETLEVEL:
		if (*(int *)addr == -1)
			break;
		else if (*(int *)addr == 1) {
			sc->mode.level = 1;
			sc->mode.packetsize = 8;
			break;
		} else if (*(int *)addr == 0) {
			sc->mode.level = 0;
			sc->mode.packetsize = 5;
			break;
		}
	
		return EINVAL;
	case MOUSE_GETLEVEL:
		*(int *)addr = sc->mode.level;
		break;
	
	case MOUSE_GETSTATUS: {
		mousestatus_t *status = (mousestatus_t *) addr;

		mtx_lock(&sc->sc_mtx);

		status->button = sc->buttons;
		status->obutton = sc->last_buttons;
		
		status->flags = status->button ^ status->obutton;

		if (sc->xdelta != 0 || sc->ydelta)
			status->flags |= MOUSE_POSCHANGED;
		if (status->button != status->obutton)
			status->flags |= MOUSE_BUTTONSCHANGED;

		status->dx = sc->xdelta;
		status->dy = sc->ydelta;
		status->dz = 0;

		sc->xdelta = 0;
		sc->ydelta = 0;
		sc->last_buttons = sc->buttons;

		mtx_unlock(&sc->sc_mtx);

		break; }
	default:
		return ENOTTY;
	}

	return (0);
}

static int
adb_tapping_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct adb_mouse_softc *sc = arg1;
	device_t dev;
        int error;
	u_char r2[8];
	u_int tapping;

	dev = sc->sc_dev;
	tapping = sc->sc_tapping;

	error = sysctl_handle_int(oidp, &tapping, 0, req);

	if (error || !req->newptr)
		return (error);

	if (tapping == 1) {
		adb_read_register(dev, 2, r2);
		r2[0] = 0x99; /* enable tapping. */
		adb_write_register(dev, 2, 8, r2);
                sc->sc_tapping = 1;
	} else if (tapping == 0) {
		adb_read_register(dev, 2, r2);
		r2[0] = 0x19; /* disable tapping. */
		adb_write_register(dev, 2, 8, r2);
		sc->sc_tapping = 0;
	}
        else
		return (EINVAL);

	return (0);
}
