/*	$OpenBSD: wbsd_ebus.c,v 1.2 2021/10/24 17:05:04 mpi Exp $	*/
/*
 * Copyright (c) 2009 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>

#include <dev/ic/w83l518dreg.h>
#include <dev/ic/w83l518dvar.h>
#include <dev/ic/w83l518d_sdmmc.h>

int	wbsd_ebus_match(struct device *, void *, void *);
void	wbsd_ebus_attach(struct device *, struct device *, void *);

const struct cfattach wbsd_ebus_ca = {
	sizeof(struct wb_softc), wbsd_ebus_match, wbsd_ebus_attach
};

struct cfdriver wbsd_cd = {
	NULL, "wbsd", DV_DULL
};

int
wbsd_ebus_match(struct device *parent, void *match, void *aux)
{
	struct ebus_attach_args *ea = aux;

	if (strcmp(ea->ea_name, "TAD,wb-sdcard") == 0)
		return (1);

	return (0);
}

void
wbsd_ebus_attach(struct device *parent, struct device *self, void *aux)
{
	struct wb_softc *sc = (void *)self;
	struct ebus_attach_args *ea = aux;

	if (ebus_bus_map(ea->ea_iotag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]),
	    ea->ea_regs[0].size, 0, 0, &sc->wb_ioh) == 0) {
		sc->wb_iot = ea->ea_iotag;
	} else if (ebus_bus_map(ea->ea_memtag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]),
	    ea->ea_regs[0].size, 0, 0, &sc->wb_ioh) == 0) {
		sc->wb_iot = ea->ea_memtag;
	} else {
		printf(": can't map register space\n");
                return;
	}

	bus_intr_establish(sc->wb_iot, ea->ea_intrs[0], IPL_BIO, 0, wb_intr,
	    sc, self->dv_xname);

	printf("\n");

	sc->wb_type = WB_DEVNO_SD;
	wb_attach(sc);
}
