/*	$OpenBSD: wscons_machdep.c,v 1.14 2017/10/14 04:44:43 jsg Exp $ */

/*
 * Copyright (c) 2001 Aaron Campbell
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
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>

#include <machine/bus.h>

#include <dev/cons.h>

#include "vga.h"
#include "pcdisplay.h"
#if (NVGA > 0) || (NPCDISPLAY > 0)
#include <dev/ic/mc6845reg.h>
#include <dev/ic/pcdisplayvar.h>
#if (NVGA > 0)
#include <dev/ic/vgareg.h>
#include <dev/ic/vgavar.h>
#endif
#if (NPCDISPLAY > 0)
#include <dev/isa/pcdisplayvar.h>
#endif
#endif

#include "wsdisplay.h"
#if NWSDISPLAY > 0
#include <dev/wscons/wsdisplayvar.h>
#endif

#include "pckbc.h"
#if (NPCKBC > 0)
#include <dev/isa/isareg.h>
#include <dev/ic/i8042reg.h>
#include <dev/ic/pckbcvar.h>
#endif
#include "pckbd.h"
#include "ukbd.h"
#if (NUKBD > 0)
#include <dev/usb/ukbdvar.h>
#endif
#include "wskbd.h"
#if NWSKBD > 0
#include <dev/wscons/wskbdvar.h>
#endif
#include "efifb.h"
#if NEFIFB > 0
#include <machine/efifbvar.h>
#endif

int	wscn_video_init(void);
void	wscn_input_init(int);

cons_decl(ws);

void
wscnprobe(struct consdev *cp)
{
	int maj;

	/* locate the major number */
	for (maj = 0; maj < nchrdev; maj++) {
		if (cdevsw[maj].d_open == wsdisplayopen)
			break;
	}

	if (maj == nchrdev) {
		/* we are not in cdevsw[], give up */
		panic("wsdisplay is not in cdevsw[]");
	}

	cp->cn_dev = makedev(maj, 0);
	cp->cn_pri = CN_MIDPRI;
}

void
wscninit(struct consdev *cp)
{
	static int initted = 0;

	if (initted)
		return;

	if (wscn_video_init() == 0) {
		initted = 1;
		wscn_input_init(0);
	}
}

void
wscnputc(dev_t dev, int i)
{
	wsdisplay_cnputc(dev, i);
}

int
wscngetc(dev_t dev)
{
	return (wskbd_cngetc(dev));
}

void
wscnpollc(dev_t dev, int on)
{
	wskbd_cnpollc(dev, on);
}

/*
 * Configure the display part of the console.
 */
int
wscn_video_init(void)
{
#if (NEFIFB > 0)
	if (efifb_cnattach() == 0)
		return (0);
#endif
#if (NVGA > 0)
	if (vga_cnattach(X86_BUS_SPACE_IO, X86_BUS_SPACE_MEM, -1, 1) == 0)
		return (0);
#endif
#if (NEFIFB > 0)
	if (efifb_cb_cnattach() == 0)
		return (0);
#endif
#if (NPCDISPLAY > 0)
	if (pcdisplay_cnattach(X86_BUS_SPACE_IO, X86_BUS_SPACE_MEM) == 0)
		return (0);
#endif
	return (-1);
}

/*
 * Configure the keyboard part of the console.
 * This is tricky, because of the games USB controllers play.
 *
 * On a truly legacy-free design, no PS/2 keyboard controller will be
 * found, so we'll settle for the first USB keyboard as the console
 * input device.
 *
 * Otherwise, the PS/2 controller will claim console, even if no PS/2
 * keyboard is plugged into it.  This is intentional, so that a PS/2
 * keyboard can be plugged late (even though this is theoretically not
 * allowed, most PS/2 controllers survive this).
 *
 * However, if there isn't any PS/2 keyboard connector, but an USB
 * controller in Legacy mode, the kernel will detect a PS/2 keyboard
 * connected (while there really isn't any), until the USB controller
 * driver attaches. At that point the ghost of the legacy keyboard
 * flees away.
 *
 * The pckbc(4) driver will, however, detect that the keyboard is gone
 * missing, and will invoke this function again, allowing a new console
 * input device choice.
 */

void
wscn_input_init(int pass)
{
	if (pass != 0) {
#if NWSKBD > 0
		wskbd_cndetach();
#endif
	}

#if (NPCKBC > 0)
	if (pass == 0 &&
	    pckbc_cnattach(X86_BUS_SPACE_IO, IO_KBD, KBCMDP, 0) == 0)
			return;
#endif
#if (NUKBD > 0)
	if (ukbd_cnattach() == 0)
		return;
#endif
}
