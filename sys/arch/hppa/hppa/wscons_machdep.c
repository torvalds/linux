/*	$OpenBSD: wscons_machdep.c,v 1.7 2004/10/05 19:27:55 mickey Exp $	*/

/*
 * Copyright (c) 2002-2004 Michael Shalayeff
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
#include <sys/kernel.h>
#include <sys/conf.h>

#include <dev/cons.h>

#include "wsdisplay.h"
#if NWSDISPLAY > 0
#include <dev/wscons/wsdisplayvar.h>
#endif

#include "wskbd.h"
#if NWSKBD > 0
#include <dev/wscons/wskbdvar.h>
#endif

cons_decl(ws);

void
wscnprobe(struct consdev *cp)
{
	/*
	 * Due to various device probe restrictions, the wscons console
	 * can never be enabled early during boot.
	 * It will be enabled as soon as enough wscons components get
	 * attached.
	 * So do nothing there, the switch will occur in
	 * wsdisplay_emul_attach() later.
	 */
}

void
wscninit(struct consdev *cp)
{
}

void
wscnputc(dev_t dev, int i)
{
#if NWSDISPLAY > 0
	wsdisplay_cnputc(dev, i);
#endif
}

int
wscngetc(dev_t dev)
{
#if NWSKBD > 0
	return (wskbd_cngetc(dev));
#else
	return (0);
#endif
}

void
wscnpollc(dev_t dev, int on)
{
#if NWSKBD > 0
	wskbd_cnpollc(dev, on);
#endif
}
