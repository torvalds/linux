/*	$OpenBSD: joy_isa.c,v 1.8 2022/02/21 10:24:28 mpi Exp $	*/
/*	$NetBSD: joy.c,v 1.3 1996/05/05 19:46:15 christos Exp $	*/

/*-
 * Copyright (c) 1995 Jean-Marc Zucconi
 * All rights reserved.
 *
 * Ported to NetBSD by Matthieu Herrb <matthieu@laas.fr>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/errno.h>

#include <machine/cpu.h>
#include <machine/pio.h>
#include <machine/cpufunc.h>
#include <machine/joystick.h>
#include <machine/conf.h>

#include <dev/isa/isavar.h>
#include <dev/isa/isareg.h>
#include <dev/ic/i8253reg.h>
#include <i386/isa/joyreg.h>

int		joy_isa_probe(struct device *, void *, void *);
void		joy_isa_attach(struct device *, struct device *, void *);

const struct cfattach joy_isa_ca = {
	sizeof(struct joy_softc), joy_isa_probe, joy_isa_attach
};

int
joy_isa_probe(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
#ifdef WANT_JOYSTICK_CONNECTED
	int iobase = ia->ia_iobase;

	outb(iobase, 0xff);
	DELAY(10000);		/* 10 ms delay */
	return (inb(iobase) & 0x0f) != 0x0f;
#else
	ia->ia_iosize = JOY_NPORTS;
	ia->ia_msize = 0;
	return 1;
#endif
}

void
joy_isa_attach(struct device *parent, struct device *self, void *aux)
{
	struct joy_softc *sc = (void *) self;
	struct isa_attach_args *ia = aux;
	int iobase = ia->ia_iobase;

	sc->port = iobase;
	sc->timeout[0] = sc->timeout[1] = 0;
	outb(iobase, 0xff);
	DELAY(10000);		/* 10 ms delay */
#if 0
	printf(": joystick%sconnected\n",
	    (inb(iobase) & 0x0f) == 0x0f ? " not " : " ");
#else
	printf("\n");
#endif
}
