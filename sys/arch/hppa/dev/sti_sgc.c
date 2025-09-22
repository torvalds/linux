/*	$OpenBSD: sti_sgc.c,v 1.41 2022/03/13 08:04:38 mpi Exp $	*/

/*
 * Copyright (c) 2000-2003 Michael Shalayeff
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
 * These cards has to be known to work so far:
 *	- HPA1991AGrayscale rev 0.02	(705/35) (byte-wide)
 *	- HPA1991AC19       rev 0.02	(715/33) (byte-wide)
 *	- HPA208LC1280      rev 8.04	(712/80) just works
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/iomod.h>
#include <machine/autoconf.h>

#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wsconsio.h>

#include <dev/ic/stireg.h>
#include <dev/ic/stivar.h>

#include <hppa/dev/cpudevs.h>

#define	STI_ROMSIZE	(sizeof(struct sti_dd) * 4)
#define	STI_ID_FDDI	0x280b31af	/* Medusa FDDI ROM id */

/* gecko optional graphics (these share the onboard's prom) */
const char sti_sgc_opt[] = { 0x17, 0x20, 0x30, 0x40, 0x70, 0xc0, 0xd0 };

extern struct cfdriver sti_cd;

int	sti_sgc_probe(struct device *, void *, void *);
void	sti_sgc_attach(struct device *, struct device *, void *);
paddr_t	sti_sgc_getrom(int, struct confargs *);

const struct cfattach sti_gedoens_ca = {
	sizeof(struct sti_softc), sti_sgc_probe, sti_sgc_attach
};

/*
 * Locate STI ROM.
 * On some machines it may not be part of the HPA space.
 */
paddr_t
sti_sgc_getrom(int unit, struct confargs *ca)
{
	paddr_t rom = PAGE0->pd_resv2[1];
	int i;

	if (unit) {
		i = -1;
		if (ca->ca_type.iodc_sv_model == HPPA_FIO_GSGC)
			for (i = sizeof(sti_sgc_opt); i-- &&
			    sti_sgc_opt[i] != ca->ca_type.iodc_revision; )
				;
		if (i < 0)
			rom = 0;
	}

	if (rom < HPPA_IOBEGIN) {
		if (ca->ca_naddrs > 0)
			rom = ca->ca_addrs[0].addr;
		else
			rom = ca->ca_hpa;
	}

	return (rom);
}

int
sti_sgc_probe(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;
	struct confargs *ca = aux;
	bus_space_handle_t romh;
	paddr_t rom;
	u_int32_t id;
	u_char devtype;
	int rv = 0, romunmapped = 0;

	/* due to the graphic nature of this program do probe only one */
	if (cf->cf_unit > sti_cd.cd_ndevs)
		return (0);

	if (ca->ca_type.iodc_type != HPPA_TYPE_FIO)
		return (0);

	/* these need further checking for the graphics id */
	if (ca->ca_type.iodc_sv_model != HPPA_FIO_GSGC &&
	    ca->ca_type.iodc_sv_model != HPPA_FIO_SGC)
		return 0;

	rom = sti_sgc_getrom(cf->cf_unit, ca);
#ifdef STIDEBUG
	printf ("sti: hpa=%lx, rom=%lx\n", ca->ca_hpa, rom);
#endif

	/* if it does not map, probably part of the lasi space */
	if ((rv = bus_space_map(ca->ca_iot, rom, STI_ROMSIZE, 0, &romh))) {
#ifdef STIDEBUG
		printf ("sti: cannot map rom space (%d)\n", rv);
#endif
		if ((rom & HPPA_IOBEGIN) == HPPA_IOBEGIN) {
			romh = rom;
			romunmapped++;
		} else {
			/* in this case nobody has no freaking idea */
			return 0;
		}
	}

	devtype = bus_space_read_1(ca->ca_iot, romh, 3);

#ifdef STIDEBUG
	printf("sti: devtype=%d\n", devtype);
#endif
	rv = 1;
	switch (devtype) {
	case STI_DEVTYPE4:
		id = bus_space_read_4(ca->ca_iot, romh, 0x8);
		break;
	case STI_DEVTYPE1:
		id = (bus_space_read_1(ca->ca_iot, romh, 0x10 +  3) << 24) |
		     (bus_space_read_1(ca->ca_iot, romh, 0x10 +  7) << 16) |
		     (bus_space_read_1(ca->ca_iot, romh, 0x10 + 11) <<  8) |
		     (bus_space_read_1(ca->ca_iot, romh, 0x10 + 15));
		break;
	default:
#ifdef STIDEBUG
		printf("sti: unknown type (%x)\n", devtype);
#endif
		rv = 0;
	}

	if (rv &&
	    ca->ca_type.iodc_sv_model == HPPA_FIO_SGC && id == STI_ID_FDDI) {
#ifdef STIDEBUG
		printf("sti: not a graphics device\n");
#endif
		rv = 0;
	}

	if (ca->ca_naddrs >= sizeof(ca->ca_addrs)/sizeof(ca->ca_addrs[0])) {
		printf("sti: address list overflow\n");
		return (0);
	}

	ca->ca_addrs[ca->ca_naddrs].addr = rom;
	ca->ca_addrs[ca->ca_naddrs].size = sti_rom_size(ca->ca_iot, romh);
	ca->ca_naddrs++;

	if (!romunmapped)
		bus_space_unmap(ca->ca_iot, romh, STI_ROMSIZE);
	return (rv);
}

void
sti_sgc_attach(struct device *parent, struct device *self, void *aux)
{
	struct sti_softc *sc = (void *)self;
	struct confargs *ca = aux;
	bus_space_handle_t romh;
	paddr_t rom;
	u_int32_t romlen;
	int rv;
	int i;

	/* we stashed rom addr/len into the last slot during probe */
	rom = ca->ca_addrs[ca->ca_naddrs - 1].addr;
	romlen = ca->ca_addrs[ca->ca_naddrs - 1].size;
	if ((rv = bus_space_map(ca->ca_iot, rom, romlen, 0, &romh))) {
		if ((rom & HPPA_IOBEGIN) == HPPA_IOBEGIN)
			romh = rom;
		else {
			printf (": cannot map rom space (%d)\n", rv);
			return;
		}
	}

	sc->bases[0] = romh;
	for (i = 1; i < STI_REGION_MAX; i++)
		sc->bases[i] = ca->ca_hpa;

#ifdef	HP7300LC_CPU
	/* PCXL2: enable accel i/o for this space */
	if (cpu_type == hpcxl2)
		eaio_l2(0x8 >> (((ca->ca_hpa >> 25) & 3) - 2));
#endif

	if (ca->ca_hpa == (hppa_hpa_t)PAGE0->mem_cons.pz_hpa)
		sc->sc_flags |= STI_CONSOLE;
	if (sti_attach_common(sc, ca->ca_iot, ca->ca_iot, romh,
	    STI_CODEBASE_PA) == 0)
		startuphook_establish(sti_end_attach, sc);
}
