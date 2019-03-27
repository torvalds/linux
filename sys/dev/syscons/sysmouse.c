/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_evdev.h"
#include "opt_syscons.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/priv.h>
#include <sys/serial.h>
#include <sys/tty.h>
#include <sys/ttydefaults.h>
#include <sys/kernel.h>
#include <sys/cons.h>
#include <sys/consio.h>
#include <sys/mouse.h>

#include <dev/syscons/syscons.h>

#ifdef EVDEV_SUPPORT
#include <dev/evdev/input.h>
#include <dev/evdev/evdev.h>
#endif

#ifndef SC_NO_SYSMOUSE

/* local variables */
static struct tty	*sysmouse_tty;
static int		mouse_level;	/* sysmouse protocol level */
static mousestatus_t	mouse_status;

#ifdef EVDEV_SUPPORT
static struct evdev_dev	*sysmouse_evdev;

static void
smdev_evdev_init(void)
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
smdev_evdev_write(int x, int y, int z, int buttons)
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

static void
smdev_close(struct tty *tp)
{
	mouse_level = 0;
}

static int
smdev_ioctl(struct tty *tp, u_long cmd, caddr_t data, struct thread *td)
{
	mousehw_t *hw;
	mousemode_t *mode;

	switch (cmd) {

	case MOUSE_GETHWINFO:	/* get device information */
		hw = (mousehw_t *)data;
		hw->buttons = 10;		/* XXX unknown */
		hw->iftype = MOUSE_IF_SYSMOUSE;
		hw->type = MOUSE_MOUSE;
		hw->model = MOUSE_MODEL_GENERIC;
		hw->hwid = 0;
		return 0;

	case MOUSE_GETMODE:	/* get protocol/mode */
		mode = (mousemode_t *)data;
		mode->level = mouse_level;
		switch (mode->level) {
		case 0: /* emulate MouseSystems protocol */
			mode->protocol = MOUSE_PROTO_MSC;
			mode->rate = -1;		/* unknown */
			mode->resolution = -1;	/* unknown */
			mode->accelfactor = 0;	/* disabled */
			mode->packetsize = MOUSE_MSC_PACKETSIZE;
			mode->syncmask[0] = MOUSE_MSC_SYNCMASK;
			mode->syncmask[1] = MOUSE_MSC_SYNC;
			break;

		case 1: /* sysmouse protocol */
			mode->protocol = MOUSE_PROTO_SYSMOUSE;
			mode->rate = -1;
			mode->resolution = -1;
			mode->accelfactor = 0;
			mode->packetsize = MOUSE_SYS_PACKETSIZE;
			mode->syncmask[0] = MOUSE_SYS_SYNCMASK;
			mode->syncmask[1] = MOUSE_SYS_SYNC;
			break;
		}
		return 0;

	case MOUSE_SETMODE:	/* set protocol/mode */
		mode = (mousemode_t *)data;
		if (mode->level == -1)
			; 	/* don't change the current setting */
		else if ((mode->level < 0) || (mode->level > 1))
			return EINVAL;
		else
			mouse_level = mode->level;
		return 0;

	case MOUSE_GETLEVEL:	/* get operation level */
		*(int *)data = mouse_level;
		return 0;

	case MOUSE_SETLEVEL:	/* set operation level */
		if ((*(int *)data  < 0) || (*(int *)data > 1))
			return EINVAL;
		mouse_level = *(int *)data;
		return 0;

	case MOUSE_GETSTATUS:	/* get accumulated mouse events */
		*(mousestatus_t *)data = mouse_status;
		mouse_status.flags = 0;
		mouse_status.obutton = mouse_status.button;
		mouse_status.dx = 0;
		mouse_status.dy = 0;
		mouse_status.dz = 0;
		return 0;

	case MOUSE_READSTATE:	/* read status from the device */
	case MOUSE_READDATA:	/* read data from the device */
		return ENODEV;
	}

	return (ENOIOCTL);
}

static int
smdev_param(struct tty *tp, struct termios *t)
{

	/*
	 * Set the output baud rate to zero. The mouse device supports
	 * no output, so we don't want to waste buffers.
	 */
	t->c_ispeed = TTYDEF_SPEED;
	t->c_ospeed = B0;

	return (0);
}

static struct ttydevsw smdev_ttydevsw = {
	.tsw_flags	= TF_NOPREFIX,
	.tsw_close	= smdev_close,
	.tsw_ioctl	= smdev_ioctl,
	.tsw_param	= smdev_param,
};

static void
sm_attach_mouse(void *unused)
{
	if (!vty_enabled(VTY_SC))
		return;
	sysmouse_tty = tty_alloc(&smdev_ttydevsw, NULL);
	tty_makedev(sysmouse_tty, NULL, "sysmouse");
#ifdef EVDEV_SUPPORT
	smdev_evdev_init();
#endif
}

SYSINIT(sysmouse, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, sm_attach_mouse, NULL);

int
sysmouse_event(mouse_info_t *info)
{
	/* MOUSE_BUTTON?DOWN -> MOUSE_MSC_BUTTON?UP */
	static int butmap[8] = {
	    MOUSE_MSC_BUTTON1UP | MOUSE_MSC_BUTTON2UP | MOUSE_MSC_BUTTON3UP,
	    MOUSE_MSC_BUTTON2UP | MOUSE_MSC_BUTTON3UP,
	    MOUSE_MSC_BUTTON1UP | MOUSE_MSC_BUTTON3UP,
	    MOUSE_MSC_BUTTON3UP,
	    MOUSE_MSC_BUTTON1UP | MOUSE_MSC_BUTTON2UP,
	    MOUSE_MSC_BUTTON2UP,
	    MOUSE_MSC_BUTTON1UP,
	    0,
	};
	u_char buf[8];
	int x, y, z;
	int i, flags = 0;

	tty_lock(sysmouse_tty);

	switch (info->operation) {
	case MOUSE_ACTION:
        	mouse_status.button = info->u.data.buttons;
		/* FALL THROUGH */
	case MOUSE_MOTION_EVENT:
		x = info->u.data.x;
		y = info->u.data.y;
		z = info->u.data.z;
		break;
	case MOUSE_BUTTON_EVENT:
		x = y = z = 0;
		if (info->u.event.value > 0)
			mouse_status.button |= info->u.event.id;
		else
			mouse_status.button &= ~info->u.event.id;
		break;
	default:
		goto done;
	}

	mouse_status.dx += x;
	mouse_status.dy += y;
	mouse_status.dz += z;
	mouse_status.flags |= ((x || y || z) ? MOUSE_POSCHANGED : 0)
			      | (mouse_status.obutton ^ mouse_status.button);
	flags = mouse_status.flags;
	if (flags == 0)
		goto done;

#ifdef EVDEV_SUPPORT
	smdev_evdev_write(x, y, z, mouse_status.button);
#endif

	if (!tty_opened(sysmouse_tty))
		goto done;

	/* the first five bytes are compatible with MouseSystems' */
	buf[0] = MOUSE_MSC_SYNC
		 | butmap[mouse_status.button & MOUSE_STDBUTTONS];
	x = imax(imin(x, 255), -256);
	buf[1] = x >> 1;
	buf[3] = x - buf[1];
	y = -imax(imin(y, 255), -256);
	buf[2] = y >> 1;
	buf[4] = y - buf[2];
	for (i = 0; i < MOUSE_MSC_PACKETSIZE; ++i)
		ttydisc_rint(sysmouse_tty, buf[i], 0);
	if (mouse_level >= 1) {
		/* extended part */
        	z = imax(imin(z, 127), -128);
        	buf[5] = (z >> 1) & 0x7f;
        	buf[6] = (z - (z >> 1)) & 0x7f;
        	/* buttons 4-10 */
        	buf[7] = (~mouse_status.button >> 3) & 0x7f;
        	for (i = MOUSE_MSC_PACKETSIZE; i < MOUSE_SYS_PACKETSIZE; ++i)
			ttydisc_rint(sysmouse_tty, buf[i], 0);
	}
	ttydisc_rint_done(sysmouse_tty);

done:	tty_unlock(sysmouse_tty);
	return (flags);
}

#endif /* !SC_NO_SYSMOUSE */
