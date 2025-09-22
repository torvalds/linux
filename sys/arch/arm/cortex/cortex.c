/*	$OpenBSD: cortex.c,v 1.9 2025/08/11 07:18:40 miod Exp $	*/
/* $NetBSD: mainbus.c,v 1.3 2001/06/13 17:52:43 nathanw Exp $ */

/*
 * Copyright (c) 1994,1995 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Brini.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * mainbus.c
 *
 * mainbus configuration
 *
 * Created      : 15/12/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#define _ARM32_BUS_DMA_PRIVATE
#include <machine/bus.h>
#include <arm/cpufunc.h>
#include <arm/armv7/armv7var.h>
#include <arm/cortex/cortex.h>
#include <arm/mainbus/mainbus.h>

struct arm32_bus_dma_tag cortex_bus_dma_tag = {
	NULL,
	_bus_dmamap_create,
	_bus_dmamap_destroy,
	_bus_dmamap_load,
	_bus_dmamap_load_mbuf,
	_bus_dmamap_load_uio,
	_bus_dmamap_load_raw,
	_bus_dmamap_load_buffer,
	_bus_dmamap_unload,
	_bus_dmamap_sync,
	_bus_dmamem_alloc,
	_bus_dmamem_alloc_range,
	_bus_dmamem_free,
	_bus_dmamem_map,
	_bus_dmamem_unmap,
	_bus_dmamem_mmap,
};

/* Prototypes for functions provided */

int  cortexmatch(struct device *, void *, void *);
void cortexattach(struct device *, struct device *, void *);
int  cortexprint(void *aux, const char *cortex);
int cortexsearch(struct device *,  void *, void *);

/* attach and device structures for the device */

const struct cfattach cortex_ca = {
	sizeof(struct device), cortexmatch, cortexattach
};

struct cfdriver cortex_cd = {
	NULL, "cortex", DV_DULL
};

/*
 * int cortexmatch(struct device *parent, struct cfdata *cf, void *aux)
 */

int
cortexmatch(struct device *parent, void *cfdata, void *aux)
{
	union mainbus_attach_args *ma = aux;
	struct cfdata *cf = (struct cfdata *)cfdata;
	int cputype = cpufunc_id();

	if (strcmp(cf->cf_driver->cd_name, ma->ma_name) != 0)
		return (0);

	if ((cputype & CPU_ID_CORTEX_A7_MASK) == CPU_ID_CORTEX_A7 ||
	    (cputype & CPU_ID_CORTEX_A9_MASK) == CPU_ID_CORTEX_A9 ||
	    (cputype & CPU_ID_CORTEX_A15_MASK) == CPU_ID_CORTEX_A15 ||
	    (cputype & CPU_ID_CORTEX_A17_MASK) == CPU_ID_CORTEX_A17) {
		if (armv7_periphbase())
			return (1);
	}

	return (0);
}

/*
 * void cortexattach(struct device *parent, struct device *self, void *aux)
 *
 * probe and attach all children
 */

void
cortexattach(struct device *parent, struct device *self, void *aux)
{
	printf("\n");

	config_search(cortexsearch, self, aux);
}

int
cortexsearch(struct device *parent, void *vcf, void *aux)
{
	struct cortex_attach_args ca;
	struct cfdata *cf = vcf;

	ca.ca_name = cf->cf_driver->cd_name;
	ca.ca_iot = &armv7_bs_tag;
	ca.ca_dmat = &cortex_bus_dma_tag;
	ca.ca_periphbase = armv7_periphbase();

	/* allow for devices to be disabled in UKC */
	if ((*cf->cf_attach->ca_match)(parent, cf, &ca) == 0)
		return 0;

	config_attach(parent, cf, &ca, cortexprint);
	return 1;
}

/*
 * int cortexprint(void *aux, const char *cortex)
 *
 * print routine used during config of children
 */

int
cortexprint(void *aux, const char *cortex)
{
	struct cortex_attach_args *ca = aux;

	if (cortex != NULL)
		printf("%s at %s", ca->ca_name, cortex);

	return (UNCONF);
}
