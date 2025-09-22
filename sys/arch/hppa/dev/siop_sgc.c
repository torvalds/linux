/*	$OpenBSD: siop_sgc.c,v 1.3 2024/05/22 14:25:47 jsg Exp $	*/

/*
 * Copyright (c) 2007 Mark Kettenis
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
#include <sys/systm.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/iomod.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/siopreg.h>
#include <dev/ic/siopvar_common.h>
#include <dev/ic/siopvar.h>

#include <hppa/dev/cpudevs.h>

#define IO_II_INTEN		0x20000000
#define IO_II_PACKEN		0x10000000
#define IO_II_PREFETCHEN	0x08000000

int siop_sgc_match(struct device *, void *, void *);
void siop_sgc_attach(struct device *, struct device *, void *);
void siop_sgc_reset(struct siop_common_softc *);

u_int8_t siop_sgc_r1(void *, bus_space_handle_t, bus_size_t);
u_int16_t siop_sgc_r2(void *, bus_space_handle_t, bus_size_t);
void siop_sgc_w1(void *, bus_space_handle_t, bus_size_t, u_int8_t);
void siop_sgc_w2(void *, bus_space_handle_t, bus_size_t, u_int16_t);

struct siop_sgc_softc {
	struct siop_softc sc_siop;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
	struct hppa_bus_space_tag sc_bustag;
};

const struct cfattach siop_gedoens_ca = {
	sizeof(struct siop_sgc_softc), siop_sgc_match, siop_sgc_attach
};

int
siop_sgc_match(struct device *parent, void *match, void *aux)
{
	struct confargs *ca = aux;

	if (ca->ca_type.iodc_type != HPPA_TYPE_ADMA ||
	    ca->ca_type.iodc_sv_model != HPPA_ADMA_FWSCSI)
		return 0;

	return 1;
}

void
siop_sgc_attach(struct device *parent, struct device *self, void *aux)
{
	struct siop_sgc_softc *sc = (struct siop_sgc_softc *)self;
	struct confargs *ca = aux;
	volatile struct iomod *regs;

	sc->sc_iot = ca->ca_iot;
	if (bus_space_map(sc->sc_iot, ca->ca_hpa,
	    IOMOD_HPASIZE, 0, &sc->sc_ioh)) {
		printf(": cannot map io space\n");
		return;
	}

	sc->sc_bustag = *sc->sc_iot;
	sc->sc_bustag.hbt_r1 = siop_sgc_r1;
	sc->sc_bustag.hbt_r2 = siop_sgc_r2;
	sc->sc_bustag.hbt_w1 = siop_sgc_w1;
	sc->sc_bustag.hbt_w2 = siop_sgc_w2;

	sc->sc_siop.sc_c.features = SF_CHIP_PF | SF_CHIP_BE | SF_BUS_WIDE;
	sc->sc_siop.sc_c.maxburst = 4;
	sc->sc_siop.sc_c.maxoff = 8;
	sc->sc_siop.sc_c.clock_div = 3;
	sc->sc_siop.sc_c.clock_period = 250;
	sc->sc_siop.sc_c.ram_size = 0;

	sc->sc_siop.sc_c.sc_reset = siop_sgc_reset;
	sc->sc_siop.sc_c.sc_dmat = ca->ca_dmatag;

	sc->sc_siop.sc_c.sc_rt = &sc->sc_bustag;
	bus_space_subregion(sc->sc_iot, sc->sc_ioh, IOMOD_DEVOFFSET,
	    IOMOD_HPASIZE - IOMOD_DEVOFFSET, &sc->sc_siop.sc_c.sc_rh);

	regs = bus_space_vaddr(sc->sc_iot, sc->sc_ioh);
	regs->io_command = CMD_RESET;
	while ((regs->io_status & IO_ERR_MEM_RY) == 0)
		delay(100);
	regs->io_ii_rw = IO_II_PACKEN | IO_II_PREFETCHEN;

	siop_sgc_reset(&sc->sc_siop.sc_c);

	regs->io_eim = cpu_gethpa(0) | (31 - ca->ca_irq);
	regs->io_ii_rw |= IO_II_INTEN;
	cpu_intr_establish(IPL_BIO, ca->ca_irq, siop_intr, sc,
	    sc->sc_siop.sc_c.sc_dev.dv_xname);

	printf(": NCR53C720 rev %d\n", bus_space_read_1(sc->sc_siop.sc_c.sc_rt,
	    sc->sc_siop.sc_c.sc_rh, SIOP_CTEST3) >> 4);

	siop_attach(&sc->sc_siop);
}

void
siop_sgc_reset(struct siop_common_softc *sc)
{
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_DCNTL, DCNTL_EA);
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_CTEST0, CTEST0_EHP);
	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_CTEST4, CTEST4_MUX);

	bus_space_write_1(sc->sc_rt, sc->sc_rh, SIOP_STIME0,
	    (0xc << STIME0_SEL_SHIFT));
}

u_int8_t
siop_sgc_r1(void *v, bus_space_handle_t h, bus_size_t o)
{
	return *(volatile u_int8_t *)(h + (o ^ 3));
}

u_int16_t
siop_sgc_r2(void *v, bus_space_handle_t h, bus_size_t o)
{
	if (o == SIOP_SIST0) {
		u_int16_t reg;

		reg = siop_sgc_r1(v, h, SIOP_SIST0);
		reg |= siop_sgc_r1(v, h, SIOP_SIST1) << 8;
		return reg;
	}
	return *(volatile u_int16_t *)(h + (o ^ 2));
}

void
siop_sgc_w1(void *v, bus_space_handle_t h, bus_size_t o, u_int8_t vv)
{
	*(volatile u_int8_t *)(h + (o ^ 3)) = vv;
}

void
siop_sgc_w2(void *v, bus_space_handle_t h, bus_size_t o, u_int16_t vv)
{
	*(volatile u_int16_t *)(h + (o ^ 2)) = vv;
}
