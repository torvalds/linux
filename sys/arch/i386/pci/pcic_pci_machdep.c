/*	$OpenBSD: pcic_pci_machdep.c,v 1.6 2025/06/13 13:38:41 jsg Exp $ */
/*	$NetBSD: pcic_pci_machdep.c,v 1.1 1998/12/20 17:53:29 nathanw Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
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

#include <dev/pcmcia/pcmciachip.h>
#include <dev/ic/i82365reg.h>
#include <dev/ic/i82365var.h>

#include <dev/isa/isavar.h>
#include <dev/isa/i82365_isavar.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/i82365_pcivar.h>

void *
pcic_pci_machdep_intr_est(pci_chipset_tag_t pc)
{
	return NULL;
}

void *
pcic_pci_machdep_pcic_intr_establish(struct pcic_softc *sc, int (*fct)(void *))
{
	if (isa_intr_alloc(NULL, PCIC_CSC_INTR_IRQ_VALIDMASK & 0xffff,
			   IST_EDGE, &(sc->irq)))
		return (NULL);
	printf("%s: interrupting at irq %d\n", sc->dev.dv_xname, sc->irq);
	return (isa_intr_establish(NULL, sc->irq, IST_EDGE, IPL_TTY,
				   fct, sc, sc->dev.dv_xname));
}
