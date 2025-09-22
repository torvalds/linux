/*	$OpenBSD: qlw_sbus.c,v 1.2 2022/03/13 13:34:54 mpi Exp $	*/
/*
 * Copyright (c) 2014 Mark Kettenis
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
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/autoconf.h>

#include <dev/sbus/sbusvar.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/qlwreg.h>
#include <dev/ic/qlwvar.h>

#ifndef ISP_NOFIRMWARE
#include <dev/microcode/isp/asm_sbus.h>
#endif

int	qlw_sbus_match(struct device *, void *, void *);
void	qlw_sbus_attach(struct device *, struct device *, void *);

const struct cfattach qlw_sbus_ca = {
	sizeof(struct qlw_softc),
	qlw_sbus_match,
	qlw_sbus_attach
};

int
qlw_sbus_match(struct device *parent, void *cf, void *aux)
{
	struct sbus_attach_args *sa = aux;

	if (strcmp("ptisp", sa->sa_name) == 0 ||
	    strcmp("PTI,ptisp", sa->sa_name) == 0 ||
	    strcmp("SUNW,isp", sa->sa_name) == 0 ||
	    strcmp("QLGC,isp", sa->sa_name) == 0)
		return 2;

	return 0;
}

void
qlw_sbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct qlw_softc *sc = (void *)self;
	struct sbus_attach_args *sa = aux;
	u_int32_t sbusburst, burst;
	int freq;

	if (sa->sa_nintr < 1) {
		printf(": no interrupt\n");
		return;
	}

	if (sa->sa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_slot, sa->sa_offset,
	    sa->sa_size, 0, 0, &sc->sc_ioh) != 0) {
		printf(": can't map registers\n");
		return;
	}

	printf(": %s\n", sa->sa_name);

	if (bus_intr_establish(sa->sa_bustag, sa->sa_pri, IPL_BIO, 0,
	    qlw_intr, sc, sc->sc_dev.dv_xname) == NULL) {
		printf("%s: can't establish interrupt\n", DEVNAME(sc));
		goto unmap;
	}

	/*
	 * Get transfer burst size from PROM
	 */
	sbusburst = ((struct sbus_softc *)parent)->sc_burst;
	if (sbusburst == 0)
		sbusburst = SBUS_BURST_32 - 1; /* 1->16 */

	burst = getpropint(sa->sa_node, "burst-sizes", -1);
	if (burst == -1)
		burst = sbusburst;

	/* Clamp at parent's burst sizes */
	burst &= sbusburst;

	if ((burst & SBUS_BURST_32))
		sc->sc_isp_config = QLW_BURST_ENABLE | QLW_SBUS_FIFO_32;
	else if ((burst & SBUS_BURST_16))
		sc->sc_isp_config = QLW_BURST_ENABLE | QLW_SBUS_FIFO_16;
	else if ((burst & SBUS_BURST_8))
		sc->sc_isp_config = QLW_BURST_ENABLE | QLW_SBUS_BURST_8;

	sc->sc_iot = sa->sa_bustag;
	sc->sc_ios = sa->sa_size;
	sc->sc_dmat = sa->sa_dmatag;

	sc->sc_isp_gen = QLW_GEN_ISP1000;
	sc->sc_isp_type = QLW_ISP1000;
	sc->sc_numbusses = 1;

	freq = getpropint(sa->sa_node, "clock-frequency", 40000000);
	sc->sc_clock = (freq + 500000) / 1000000;

#ifndef ISP_NOFIRMWARE
	/*
	 * Some early versions of the PTI cards don't support loading
	 * a new firmware, so only do this in the Sun or QLogic
	 * branded ones.
	 */
	if (strcmp("SUNW,isp", sa->sa_name) == 0 ||
	    strcmp("QLGC,isp", sa->sa_name) == 0)
		sc->sc_firmware = isp_1000_risc_code;
#endif

	sc->sc_initiator[0] = getpropint(sa->sa_node, "scsi-initiator-id", 6);

	sc->sc_host_cmd_ctrl = QLW_HOST_CMD_CTRL_SBUS;
	sc->sc_mbox_base = QLW_MBOX_BASE_SBUS;

	qlw_attach(sc);
	return;

unmap:
	bus_space_unmap(sa->sa_bustag, sc->sc_ioh, sa->sa_size);
}
