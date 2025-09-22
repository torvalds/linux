/*	$OpenBSD: octeon_iobus.c,v 1.28 2024/05/20 23:17:10 jsg Exp $ */

/*
 * Copyright (c) 2000-2004 Opsycon AB  (www.opsycon.se)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * This is a iobus driver.
 * It handles configuration of all devices on the processor bus except UART.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/atomic.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>

#include <machine/autoconf.h>
#include <machine/fdt.h>
#include <machine/intr.h>
#include <machine/octeonvar.h>
#include <machine/octeonreg.h>
#include <machine/octeon_model.h>

#include <octeon/dev/iobusvar.h>
#include <octeon/dev/octhcireg.h>	/* USBN_BASE */

int	iobusmatch(struct device *, void *, void *);
void	iobusattach(struct device *, struct device *, void *);
int	iobusprint(void *, const char *);
int	iobussubmatch(struct device *, void *, void *);
int	iobussearch(struct device *, void *, void *);

bus_addr_t iobus_pa_to_device(paddr_t);
paddr_t	 iobus_device_to_pa(bus_addr_t);

const struct cfattach iobus_ca = {
	sizeof(struct device), iobusmatch, iobusattach
};

struct cfdriver iobus_cd = {
	NULL, "iobus", DV_DULL
};

bus_space_t iobus_tag = {
	.bus_base = PHYS_TO_XKPHYS(0, CCA_NC),
	.bus_private = NULL,
	._space_read_1 =	generic_space_read_1,
	._space_write_1 =	generic_space_write_1,
	._space_read_2 =	generic_space_read_2,
	._space_write_2 =	generic_space_write_2,
	._space_read_4 =	generic_space_read_4,
	._space_write_4 =	generic_space_write_4,
	._space_read_8 =	generic_space_read_8,
	._space_write_8 =	generic_space_write_8,
	._space_read_raw_2 =	generic_space_read_raw_2,
	._space_write_raw_2 =	generic_space_write_raw_2,
	._space_read_raw_4 =	generic_space_read_raw_4,
	._space_write_raw_4 =	generic_space_write_raw_4,
	._space_read_raw_8 =	generic_space_read_raw_8,
	._space_write_raw_8 =	generic_space_write_raw_8,
	._space_map =		iobus_space_map,
	._space_unmap =		iobus_space_unmap,
	._space_subregion =	generic_space_region,
	._space_vaddr =		generic_space_vaddr
};

struct machine_bus_dma_tag iobus_bus_dma_tag = {
	NULL,			/* _cookie */
	_dmamap_create,
	_dmamap_destroy,
	_dmamap_load,
	_dmamap_load_mbuf,
	_dmamap_load_uio,
	_dmamap_load_raw,
	_dmamap_load_buffer,
	_dmamap_unload,
	_dmamap_sync,
	_dmamem_alloc,
	_dmamem_free,
	_dmamem_map,
	_dmamem_unmap,
	_dmamem_mmap,
	iobus_pa_to_device,
	iobus_device_to_pa,
	0
};

/*
 * List of iobus child devices whose base addresses are too large to be
 * recorded in the kernel configuration file. So look them up from here instead.
 */

static const struct octeon_iobus_addrs iobus_addrs[] = {
	{ "octcf",	OCTEON_CF_BASE  },
	{ "octrng",	OCTEON_RNG_BASE },
	{ "dwctwo",	USBN_BASE       },
	{ "amdcf",	OCTEON_AMDCF_BASE},
};

/* There can only be one. */
int	iobus_found;

/*
 * Match bus only to targets which have this bus.
 */
int
iobusmatch(struct device *parent, void *match, void *aux)
{
	if (iobus_found)
		return (0);

	return (1);
}

int
iobusprint(void *aux, const char *iobus)
{
	struct iobus_attach_args *aa = aux;

	if (iobus != NULL)
		printf("%s at %s", aa->aa_name, iobus);

	if (aa->aa_addr != 0)
		printf(" base 0x%lx", aa->aa_addr);
	if (aa->aa_irq >= 0)
		printf(" irq %d", aa->aa_irq);

	return (UNCONF);
}

int
iobussubmatch(struct device *parent, void *vcf, void *args)
{
	struct cfdata *cf = vcf;
	struct iobus_attach_args *aa = args;

	if (strcmp(cf->cf_driver->cd_name, aa->aa_name) != 0)
		return 0;

	if (cf->cf_loc[0] != -1 && cf->cf_loc[0] != (int)aa->aa_addr)
		return 0;

	return (*cf->cf_attach->ca_match)(parent, cf, aa);
}

void
iobusattach(struct device *parent, struct device *self, void *aux)
{
	struct iobus_attach_args aa;
	struct fdt_attach_args fa;
	struct octeon_config oc;
	struct device *sc = self;
	int soc;

	iobus_found = 1;

	printf("\n");

	/* XXX */
	oc.mc_iobus_bust = &iobus_tag;
	oc.mc_iobus_dmat = &iobus_bus_dma_tag;
	void	cn30xxfpa_bootstrap(struct octeon_config *);
	cn30xxfpa_bootstrap(&oc);
	void	cn30xxpow_bootstrap(struct octeon_config *);
	cn30xxpow_bootstrap(&oc);

	/*
	 * Attach all subdevices as described in the config file.
	 */

	if ((soc = OF_finddevice("/soc")) != -1) {
		memset(&fa, 0, sizeof(fa));
		fa.fa_name = "";
		fa.fa_node = soc;
		fa.fa_iot = &iobus_tag;
		fa.fa_dmat = &iobus_bus_dma_tag;
		config_found(self, &fa, NULL);
	}

	config_search(iobussearch, self, sc);

	if (octeon_ver == OCTEON_2 || octeon_ver == OCTEON_3) {
		memset(&aa, 0, sizeof(aa));
		aa.aa_name = "octpcie";
		aa.aa_bust = &iobus_tag;
		aa.aa_dmat = &iobus_bus_dma_tag;
		aa.aa_irq = -1;
		config_found_sm(self, &aa, iobusprint, iobussubmatch);
	}
}

int
iobussearch(struct device *parent, void *v, void *aux)
{
	struct iobus_attach_args aa;
	struct cfdata *cf = v;
	int i;

	aa.aa_name = cf->cf_driver->cd_name;
	aa.aa_bust = &iobus_tag;
	aa.aa_dmat = &iobus_bus_dma_tag;
	aa.aa_addr = cf->cf_loc[0];
	aa.aa_irq  = cf->cf_loc[1];
	aa.aa_unitno = cf->cf_unit;

	/* No address specified, try to look it up. */
	if (aa.aa_addr == -1) {
		for (i = 0; i < nitems(iobus_addrs); i++) {
			if (strcmp(iobus_addrs[i].name, cf->cf_driver->cd_name) == 0)
				aa.aa_addr = iobus_addrs[i].address;
		}
		if (aa.aa_addr == -1)
			return 0;
	}

	if (cf->cf_attach->ca_match(parent, cf, &aa) == 0)
		return 0;

	config_attach(parent, cf, &aa, iobusprint);
	return 1;
}

int
iobus_space_map(bus_space_tag_t t, bus_addr_t offs, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	if (ISSET(flags, BUS_SPACE_MAP_KSEG0)) {
		*bshp = PHYS_TO_CKSEG0(offs);
		return 0;
	}
	if (ISSET(flags, BUS_SPACE_MAP_CACHEABLE))
		offs +=
		    PHYS_TO_XKPHYS(0, CCA_CACHED) - PHYS_TO_XKPHYS(0, CCA_NC);
	*bshp = t->bus_base + offs;
	return 0;
}

void
iobus_space_unmap(bus_space_tag_t t, bus_space_handle_t bsh, bus_size_t size)
{
}

/*
 * Iobus bus_dma helpers.
 */

bus_addr_t
iobus_pa_to_device(paddr_t pa)
{
	return (bus_addr_t)pa;
}

paddr_t
iobus_device_to_pa(bus_addr_t addr)
{
	return (paddr_t)addr;
}
