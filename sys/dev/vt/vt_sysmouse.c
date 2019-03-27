/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Copyright (c) 2009 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Ed Schouten under sponsorship from the
 * FreeBSD Foundation.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_evdev.h"

#include <sys/param.h>
#include <sys/condvar.h>
#include <sys/consio.h>
#include <sys/fcntl.h>
#include <sys/filio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/poll.h>
#include <sys/random.h>
#include <sys/selinfo.h>
#include <sys/sigio.h>
#include <sys/signalvar.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <dev/vt/vt.h>

#ifdef EVDEV_SUPPORT
#include <dev/evdev/input.h>
#include <dev/evdev/evdev.h>
#endif

static d_open_t		sysmouse_open;
static d_close_t	sysmouse_close;
static d_read_t		sysmouse_read;
static d_ioctl_t	sysmouse_ioctl;
static d_poll_t		sysmouse_poll;

static struct cdevsw sysmouse_cdevsw = {
	.d_version	= D_VERSION,
	.d_open		= sysmouse_open,
	.d_close	= sysmouse_close,
	.d_read		= sysmouse_read,
	.d_ioctl	= sysmouse_ioctl,
	.d_poll		= sysmouse_poll,
	.d_name		= "sysmouse",
};

static struct mtx	 sysmouse_lock;
static struct cv	 sysmouse_sleep;
static struct selinfo	 sysmouse_bufpoll;

static int		 sysmouse_level;
static mousestatus_t	 sysmouse_status;
static int		 sysmouse_flags;
#define	SM_ASYNC	0x1
static struct sigio	*sysmouse_sigio;

#define	SYSMOUSE_MAXFRAMES	250	/* 2 KB */
static MALLOC_DEFINE(M_SYSMOUSE, "sysmouse", "sysmouse device");
static unsigned char	*sysmouse_buffer;
static unsigned int	 sysmouse_start, sysmouse_length;

#ifdef EVDEV_SUPPORT
static struct evdev_dev	*sysmouse_evdev;

static void
sysmouse_evdev_init(void)
{
	int i;

	sysmouse_evdev = evdev_alloc();
	evdev_set_name(sysmouse_evdev, "System mouse");
	evdev_set_phys(sysmouse_evdev, "sysmouse");
	evdev_set_id(sysmouse_evdev, BUS_VIRTUAL, 0, 0, 0);
	evdev_support_prop(sysmouse_evdev, INPUT_PROP_POINTER);
	evdev_support_event(sysmouse_evdev, EV_SYN);
	evdev_support_event(sysmouse_evdev, EV_REL);
	evdev_support_event(sysmouse_evdev, EV_KEY);
	evdev_support_rel(sysmouse_evdev, REL_X);
	evdev_support_rel(sysmouse_evdev, REL_Y);
	evdev_support_rel(sysmouse_evdev, REL_WHEEL);
	evdev_support_rel(sysmouse_evdev, REL_HWHEEL);
	for (i = 0; i < 8; i++)
		evdev_support_key(sysmouse_evdev, BTN_MOUSE + i);
	if (evdev_register(sysmouse_evdev)) {
		evdev_free(sysmouse_evdev);
		sysmouse_evdev = NULL;
	}
}

static void
sysmouse_evdev_store(int x, int y, int z, int buttons)
{

	if (sysmouse_evdev == NULL || !(evdev_rcpt_mask & EVDEV_RCPT_SYSMOUSE))
		return;

	evdev_push_event(sysmouse_evdev, EV_REL, REL_X, x);
	evdev_push_event(sysmouse_evdev, EV_REL, REL_Y, y);
	switch (evdev_sysmouse_t_axis) {
	case EVDEV_SYSMOUSE_T_AXIS_PSM:
		switch (z) {
		case 1:
		case -1:
			evdev_push_rel(sysmouse_evdev, REL_WHEEL, -z);
			break;
		case 2:
		case -2:
			evdev_push_rel(sysmouse_evdev, REL_HWHEEL, z / 2);
			break;
		}
		break;
	case EVDEV_SYSMOUSE_T_AXIS_UMS:
		if (buttons & (1 << 6))
			evdev_push_rel(sysmouse_evdev, REL_HWHEEL, 1);
		else if (buttons & (1 << 5))
			evdev_push_rel(sysmouse_evdev, REL_HWHEEL, -1);
		buttons &= ~((1 << 5)|(1 << 6));
		/* PASSTHROUGH */
	case EVDEV_SYSMOUSE_T_AXIS_NONE:
	default:
		evdev_push_rel(sysmouse_evdev, REL_WHEEL, -z);
	}
	evdev_push_mouse_btn(sysmouse_evdev, buttons);
	evdev_sync(sysmouse_evdev);
}
#endif

static int
sysmouse_buf_read(struct uio *uio, unsigned int length)
{
	unsigned char buf[MOUSE_SYS_PACKETSIZE];
	int error;

	if (sysmouse_buffer == NULL)
		return (ENXIO);
	else if (sysmouse_length == 0)
		return (EWOULDBLOCK);

	memcpy(buf, sysmouse_buffer +
	    sysmouse_start * MOUSE_SYS_PACKETSIZE, MOUSE_SYS_PACKETSIZE);
	sysmouse_start = (sysmouse_start + 1) % SYSMOUSE_MAXFRAMES;
	sysmouse_length--;

	mtx_unlock(&sysmouse_lock);
	error = uiomove(buf, length, uio);
	mtx_lock(&sysmouse_lock);

	return (error);
}

static void
sysmouse_buf_store(const unsigned char buf[MOUSE_SYS_PACKETSIZE])
{
	unsigned int idx;

	if (sysmouse_buffer == NULL || sysmouse_length == SYSMOUSE_MAXFRAMES)
		return;

	idx = (sysmouse_start + sysmouse_length) % SYSMOUSE_MAXFRAMES;
	memcpy(sysmouse_buffer + idx * MOUSE_SYS_PACKETSIZE, buf,
	    MOUSE_SYS_PACKETSIZE);
	sysmouse_length++;
	cv_broadcast(&sysmouse_sleep);
	selwakeup(&sysmouse_bufpoll);
	if (sysmouse_flags & SM_ASYNC && sysmouse_sigio != NULL)
		pgsigio(&sysmouse_sigio, SIGIO, 0);
}

void
sysmouse_process_event(mouse_info_t *mi)
{
	/* MOUSE_BUTTON?DOWN -> MOUSE_MSC_BUTTON?UP */
	static const int buttonmap[8] = {
	    MOUSE_MSC_BUTTON1UP | MOUSE_MSC_BUTTON2UP | MOUSE_MSC_BUTTON3UP,
	    MOUSE_MSC_BUTTON2UP | MOUSE_MSC_BUTTON3UP,
	    MOUSE_MSC_BUTTON1UP | MOUSE_MSC_BUTTON3UP,
	    MOUSE_MSC_BUTTON3UP,
	    MOUSE_MSC_BUTTON1UP | MOUSE_MSC_BUTTON2UP,
	    MOUSE_MSC_BUTTON2UP,
	    MOUSE_MSC_BUTTON1UP,
	    0,
	};
	unsigned char buf[MOUSE_SYS_PACKETSIZE];
	int x, y, iy, z;

	random_harvest_queue(mi, sizeof *mi, RANDOM_MOUSE);

	mtx_lock(&sysmouse_lock);
	switch (mi->operation) {
	case MOUSE_ACTION:
		sysmouse_status.button = mi->u.data.buttons;
		/* FALLTHROUGH */
	case MOUSE_MOTION_EVENT:
		x = mi->u.data.x;
		y = mi->u.data.y;
		z = mi->u.data.z;
		break;
	case MOUSE_BUTTON_EVENT:
		x = y = z = 0;
		if (mi->u.event.value > 0)
			sysmouse_status.button |= mi->u.event.id;
		else
			sysmouse_status.button &= ~mi->u.event.id;
		break;
	default:
		goto done;
	}

	sysmouse_status.dx += x;
	sysmouse_status.dy += y;
	sysmouse_status.dz += z;
	sysmouse_status.flags |= ((x || y || z) ? MOUSE_POSCHANGED : 0) |
	    (sysmouse_status.obutton ^ sysmouse_status.button);
	if (sysmouse_status.flags == 0)
		goto done;

#ifdef EVDEV_SUPPORT
	sysmouse_evdev_store(x, y, z, sysmouse_status.button);
#endif

	/* The first five bytes are compatible with MouseSystems. */
	buf[0] = MOUSE_MSC_SYNC |
	    buttonmap[sysmouse_status.button & MOUSE_STDBUTTONS];
	x = imax(imin(x, 255), -256);
	buf[1] = x >> 1;
	buf[3] = x - buf[1];
	iy = -imax(imin(y, 255), -256);
	buf[2] = iy >> 1;
	buf[4] = iy - buf[2];
	/* Extended part. */
        z = imax(imin(z, 127), -128);
        buf[5] = (z >> 1) & 0x7f;
        buf[6] = (z - (z >> 1)) & 0x7f;
        /* Buttons 4-10. */
        buf[7] = (~sysmouse_status.button >> 3) & 0x7f;

	sysmouse_buf_store(buf);

#ifndef SC_NO_CUTPASTE
	mtx_unlock(&sysmouse_lock);
	vt_mouse_event(mi->operation, x, y, mi->u.event.id, mi->u.event.value,
	    sysmouse_level);
	return;
#endif

done:	mtx_unlock(&sysmouse_lock);
}

static int
sysmouse_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	void *buf;

	buf = malloc(MOUSE_SYS_PACKETSIZE * SYSMOUSE_MAXFRAMES,
	    M_SYSMOUSE, M_WAITOK);
	mtx_lock(&sysmouse_lock);
	if (sysmouse_buffer == NULL) {
		sysmouse_buffer = buf;
		sysmouse_start = sysmouse_length = 0;
		sysmouse_level = 0;
	} else {
		free(buf, M_SYSMOUSE);
	}
	mtx_unlock(&sysmouse_lock);

	return (0);
}

static int
sysmouse_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{

	mtx_lock(&sysmouse_lock);
	free(sysmouse_buffer, M_SYSMOUSE);
	sysmouse_buffer = NULL;
	sysmouse_level = 0;
	mtx_unlock(&sysmouse_lock);

	return (0);
}

static int
sysmouse_read(struct cdev *dev, struct uio *uio, int ioflag)
{
	unsigned int length;
	ssize_t oresid;
	int error = 0;

	oresid = uio->uio_resid;

	mtx_lock(&sysmouse_lock);
	length = sysmouse_level >= 1 ? MOUSE_SYS_PACKETSIZE :
	    MOUSE_MSC_PACKETSIZE;

	while (uio->uio_resid >= length) {
		error = sysmouse_buf_read(uio, length);
		if (error == 0) {
			/* Process the next frame. */
			continue;
		} else if (error != EWOULDBLOCK) {
			/* Error (e.g. EFAULT). */
			break;
		} else {
			/* Block. */
			if (oresid != uio->uio_resid || ioflag & O_NONBLOCK)
				break;
			error = cv_wait_sig(&sysmouse_sleep, &sysmouse_lock);
			if (error != 0)
				break;
		}
	}
	mtx_unlock(&sysmouse_lock);

	return (error);
}

static int
sysmouse_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag,
    struct thread *td)
{

	switch (cmd) {
	case FIOASYNC:
		mtx_lock(&sysmouse_lock);
		if (*(int *)data)
			sysmouse_flags |= SM_ASYNC;
		else
			sysmouse_flags &= ~SM_ASYNC;
		mtx_unlock(&sysmouse_lock);
		return (0);
	case FIONBIO:
		return (0);
	case FIOGETOWN:
		*(int *)data = fgetown(&sysmouse_sigio);
		return (0);
	case FIOSETOWN:
		return (fsetown(*(int *)data, &sysmouse_sigio));
	case MOUSE_GETHWINFO: {
		mousehw_t *hw = (mousehw_t *)data;

		hw->buttons = 10;
		hw->iftype = MOUSE_IF_SYSMOUSE;
		hw->type = MOUSE_MOUSE;
		hw->model = MOUSE_MODEL_GENERIC;
		hw->hwid = 0;

		return (0);
	}
	case MOUSE_GETLEVEL:
		*(int *)data = sysmouse_level;
		return (0);
	case MOUSE_GETMODE: {
		mousemode_t *mode = (mousemode_t *)data;

		mode->rate = -1;
		mode->resolution = -1;
		mode->accelfactor = 0;
		mode->level = sysmouse_level;

		switch (mode->level) {
		case 0:
			mode->protocol = MOUSE_PROTO_MSC;
			mode->packetsize = MOUSE_MSC_PACKETSIZE;
			mode->syncmask[0] = MOUSE_MSC_SYNCMASK;
			mode->syncmask[1] = MOUSE_MSC_SYNC;
			break;
		case 1:
			mode->protocol = MOUSE_PROTO_SYSMOUSE;
			mode->packetsize = MOUSE_SYS_PACKETSIZE;
			mode->syncmask[0] = MOUSE_SYS_SYNCMASK;
			mode->syncmask[1] = MOUSE_SYS_SYNC;
			break;
		}

		return (0);
	}
	case MOUSE_GETSTATUS:
		mtx_lock(&sysmouse_lock);
		*(mousestatus_t *)data = sysmouse_status;

		sysmouse_status.flags = 0;
		sysmouse_status.obutton = sysmouse_status.button;
		sysmouse_status.dx = 0;
		sysmouse_status.dy = 0;
		sysmouse_status.dz = 0;
		mtx_unlock(&sysmouse_lock);

		return (0);
	case MOUSE_SETLEVEL: {
		int level;

		level = *(int *)data;
		if (level != 0 && level != 1)
			return (EINVAL);

		sysmouse_level = level;
		return (0);
	}
	case MOUSE_SETMODE: {
		mousemode_t *mode = (mousemode_t *)data;

		switch (mode->level) {
		case -1:
			/* Do nothing. */
			break;
		case 0:
		case 1:
			sysmouse_level = mode->level;
			break;
		default:
			return (EINVAL);
		}

		return (0);
	}
	case MOUSE_MOUSECHAR:
		return (0);
	default:
#ifdef VT_SYSMOUSE_DEBUG
		printf("sysmouse: unknown ioctl: %c:%lx\n",
		    (char)IOCGROUP(cmd), IOCBASECMD(cmd));
#endif
		return (ENOIOCTL);
	}
}

static int
sysmouse_poll(struct cdev *dev, int events, struct thread *td)
{
	int revents = 0;

	mtx_lock(&sysmouse_lock);
	if (events & (POLLIN|POLLRDNORM)) {
		if (sysmouse_length > 0)
			revents = events & (POLLIN|POLLRDNORM);
		else
			selrecord(td, &sysmouse_bufpoll);
	}
	mtx_unlock(&sysmouse_lock);

	return (revents);
}

static void
sysmouse_drvinit(void *unused)
{

	if (!vty_enabled(VTY_VT))
		return;
	mtx_init(&sysmouse_lock, "sysmouse", NULL, MTX_DEF);
	cv_init(&sysmouse_sleep, "sysmrd");
	make_dev(&sysmouse_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600,
	    "sysmouse");
#ifdef EVDEV_SUPPORT
	sysmouse_evdev_init();
#endif
}

SYSINIT(sysmouse, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, sysmouse_drvinit, NULL);
