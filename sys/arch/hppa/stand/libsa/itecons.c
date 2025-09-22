/*	$OpenBSD: itecons.c,v 1.12 2014/07/17 12:37:46 miod Exp $	*/

/*
 * Copyright (c) 1998-2004 Michael Shalayeff
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
/*
 * Copyright 1996 1995 by Open Software Foundation, Inc.
 *              All Rights Reserved
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appears in all copies and
 * that both the copyright notice and this permission notice appear in
 * supporting documentation.
 *
 * OSF DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL OSF BE LIABLE FOR ANY SPECIAL, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT,
 * NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include "libsa.h"

#include <sys/param.h>
#include <sys/disklabel.h>
#include <machine/pdc.h>
#include <machine/iomod.h>
#include <dev/cons.h>

#include "dev_hppa.h"

iodcio_t cniodc;	/* console IODC entry point */
iodcio_t kyiodc;	/* keyboard IODC entry point */
pz_device_t *cons_pzdev, *kbd_pzdev;

/*
 * Console.
 */

char cnbuf[IODC_MINIOSIZ] __attribute__ ((aligned (IODC_MINIOSIZ)));
int kycode[IODC_MAXSIZE/sizeof(int)];

int
cnspeed(dev_t dev, int sp)
{
	return CONSPEED;
}

void
ite_probe(cn)
	struct consdev *cn;
{
	cniodc = (iodcio_t)PAGE0->mem_free;
	cons_pzdev = &PAGE0->mem_cons;
	kbd_pzdev = &PAGE0->mem_kbd;

	if ((*pdc)   (PDC_IODC, PDC_IODC_READ, pdcbuf, cons_pzdev->pz_hpa,
		      IODC_INIT, cniodc, IODC_MAXSIZE) < 0 ||
	    (*cniodc)(cons_pzdev->pz_hpa,
		      (cons_pzdev->pz_hpa==PAGE0->mem_boot.pz_hpa)?
			IODC_INIT_DEV: IODC_INIT_ALL, cons_pzdev->pz_spa,
			cons_pzdev->pz_layers, pdcbuf, 0,0,0,0) < 0 ||
	    (*pdc)   (PDC_IODC, PDC_IODC_READ, pdcbuf, cons_pzdev->pz_hpa,
		      IODC_IO, cniodc, IODC_MAXSIZE) < 0) {
		/* morse code with the LED's?!! */
		cons_pzdev->pz_iodc_io = kbd_pzdev->pz_iodc_io = NULL;
	} else {
		cn->cn_pri = CN_MIDPRI;
		cn->cn_dev = makedev(0, 0);
	}
}

void
ite_init(cn)
	struct consdev *cn;
{
	/*
	 * If the keyboard is separate from the console output device,
	 * we load the keyboard code at `kycode'.
	 *
	 * N.B. In this case, since the keyboard code is part of the
	 * boot code, it will be overwritten when we load a kernel.
	 */
	if (cons_pzdev->pz_class != PCL_DUPLEX ||
	    kbd_pzdev->pz_class == PCL_KEYBD) {

		kyiodc = (iodcio_t)kycode;

		if ((*pdc)   (PDC_IODC, PDC_IODC_READ, pdcbuf, kbd_pzdev->pz_hpa,
			      IODC_INIT, kyiodc, IODC_MAXSIZE) < 0 ||
		    (*kyiodc)(kbd_pzdev->pz_hpa,
			      (kbd_pzdev->pz_hpa == PAGE0->mem_boot.pz_hpa ||
			       kbd_pzdev->pz_hpa == cons_pzdev->pz_hpa)?
			      IODC_INIT_DEV: IODC_INIT_ALL, kbd_pzdev->pz_spa,
			      kbd_pzdev->pz_layers, pdcbuf, 0, 0, 0, 0) < 0 ||
		    (*pdc)   (PDC_IODC, PDC_IODC_READ, pdcbuf, kbd_pzdev->pz_hpa,
			      IODC_IO, kyiodc, IODC_MAXSIZE))
			kyiodc = NULL;
	} else {
		kyiodc = cniodc;

		bcopy((char *)&PAGE0->mem_cons, (char *)&PAGE0->mem_kbd,
		      sizeof(struct pz_device));
	}

	cons_pzdev->pz_iodc_io = (u_int)cniodc;
	kbd_pzdev->pz_iodc_io = (u_int)kyiodc;
#ifdef DEBUG
	if (!kyiodc)
		printf("ite_init: no kbd\n");
#endif
}

void
ite_putc(dev, c)
	dev_t dev;
	int c;
{
	if (cniodc == NULL)
		return;

	*cnbuf = c;

	(*cniodc)(cons_pzdev->pz_hpa, IODC_IO_CONSOUT, cons_pzdev->pz_spa,
		  cons_pzdev->pz_layers, pdcbuf, 0, cnbuf, 1, 0);
}

/*
 * since i don't know how to 'just check the char available'
 * i store the key into the stash removing on read op later;
 */
int
ite_getc(dev)
	dev_t dev;
{
	static int stash = 0;
	int err, c, l, i;

	if (kyiodc == NULL)
		return(0x100);

	if (stash) {
		c = stash;
		if (!(dev & 0x80))
			stash = 0;
		return c;
	}

	i = 16;
	do {
		err = (*kyiodc)(kbd_pzdev->pz_hpa, IODC_IO_CONSIN,
				kbd_pzdev->pz_spa, kbd_pzdev->pz_layers,
				pdcbuf, 0, cnbuf, 1, 0);
		l = pdcbuf[0];
		c = cnbuf[0];
#ifdef DEBUG
		if (debug && err < 0)
			printf("KBD input error: %d", err);
#endif

		/* if we are doing ischar() report immediately */
		if (!i-- && (dev & 0x80) && l == 0) {
#ifdef DEBUG
			if (debug > 2)
				printf("ite_getc(0x%x): no char %d(%x)\n",
				       dev, l, c);
#endif
			return (0);
		}
	} while(!l);

#if DEBUG
	if (debug && l > 1)
		printf("KBD input got too much (%d)\n", l);

	if (debug > 3)
		printf("kbd: \'%c\' (0x%x)\n", c, c);
#endif
	if (dev & 0x80)
		stash = c;

	return (c);
}

void
ite_pollc(dev, on)
	dev_t dev;
	int on;
{

}
