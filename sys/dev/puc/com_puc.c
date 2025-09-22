/*	$OpenBSD: com_puc.c,v 1.28 2023/09/11 08:41:27 mvs Exp $	*/

/*
 * Copyright (c) 1997 - 1999, Jason Downs.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name(s) of the author(s) nor the name OpenBSD
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/syslog.h>
#include <sys/device.h>

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/pci/pucvar.h>

#include "com.h"

#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>
#include <dev/ic/ns16550reg.h>

#define	com_lcr		com_cfcr

int	com_puc_match(struct device *, void *, void *);
void	com_puc_attach(struct device *, struct device *, void *);
int	com_puc_detach(struct device *, int);

const struct cfattach com_puc_ca = {
	sizeof(struct com_softc), com_puc_match,
	com_puc_attach, com_puc_detach, com_activate
};

int
com_puc_match(struct device *parent, void *match, void *aux)
{
	struct puc_attach_args *pa = aux;

	if (PUC_IS_COM(pa->type))
		return(1);

	return(0);
}

void
com_puc_attach(struct device *parent, struct device *self, void *aux)
{
	struct com_softc *sc = (void *)self;
	struct puc_attach_args *pa = aux;
	const char *intrstr;
	int i;

	/* Grab a PCI interrupt. */
	intrstr = pa->intr_string(pa);
	sc->sc_ih = pa->intr_establish(pa, IPL_TTY, comintr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(" %s", intrstr);

	sc->sc_iot = pa->t;
	sc->sc_ioh = pa->h;
	sc->sc_iobase = pa->a;

	sc->sc_frequency = COM_FREQ;

	for (i = 0; i < nitems(puc_port_types); i++)
		if (puc_port_types[i].type == pa->type) {
			sc->sc_frequency = puc_port_types[i].freq;
			break;
		}

	if (pa->type == PUC_PORT_COM_XR17V35X)
		sc->sc_uarttype = COM_UART_XR17V35X;

	com_attach_subr(sc);
}

int
com_puc_detach(struct device *self, int flags)
{
	return com_detach(self, flags);
}
