/*	$OpenBSD: wbsio.c,v 1.12 2022/04/06 18:59:29 naddy Exp $	*/
/*
 * Copyright (c) 2008 Mark Kettenis <kettenis@openbsd.org>
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

/*
 * Winbond LPC Super I/O driver.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <machine/bus.h>

#include <dev/isa/isavar.h>
#include <dev/isa/wbsioreg.h>

#ifdef WBSIO_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

struct wbsio_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

int	wbsio_probe(struct device *, void *, void *);
void	wbsio_attach(struct device *, struct device *, void *);
int	wbsio_print(void *, const char *);

const struct cfattach wbsio_ca = {
	sizeof(struct wbsio_softc),
	wbsio_probe,
	wbsio_attach
};

struct cfdriver wbsio_cd = {
	NULL, "wbsio", DV_DULL
};

static __inline void
wbsio_conf_enable(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	bus_space_write_1(iot, ioh, WBSIO_INDEX, WBSIO_CONF_EN_MAGIC);
	bus_space_write_1(iot, ioh, WBSIO_INDEX, WBSIO_CONF_EN_MAGIC);
}

static __inline void
wbsio_conf_disable(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	bus_space_write_1(iot, ioh, WBSIO_INDEX, WBSIO_CONF_DS_MAGIC);
}

static __inline u_int8_t
wbsio_conf_read(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t index)
{
	bus_space_write_1(iot, ioh, WBSIO_INDEX, index);
	return (bus_space_read_1(iot, ioh, WBSIO_DATA));
}

static __inline void
wbsio_conf_write(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t index,
    u_int8_t data)
{
	bus_space_write_1(iot, ioh, WBSIO_INDEX, index);
	bus_space_write_1(iot, ioh, WBSIO_DATA, data);
}

int
wbsio_probe(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	u_int8_t reg;

	/* Match by device ID */
	iot = ia->ia_iot;
	if (bus_space_map(iot, ia->ipa_io[0].base, WBSIO_IOSIZE, 0, &ioh))
		return (0);
	wbsio_conf_enable(iot, ioh);
	reg = wbsio_conf_read(iot, ioh, WBSIO_ID);
	DPRINTF(("wbsio_probe: id 0x%02x\n", reg));
	wbsio_conf_disable(iot, ioh);
	bus_space_unmap(iot, ioh, WBSIO_IOSIZE);
	switch (reg) {
	case WBSIO_ID_W83627HF:
	case WBSIO_ID_W83627THF:
	case WBSIO_ID_W83627EHF:
	case WBSIO_ID_W83627DHG:
	case WBSIO_ID_W83627DHGP:
	case WBSIO_ID_W83627UHG:
	case WBSIO_ID_W83637HF:
	case WBSIO_ID_W83697HF:
	case WBSIO_ID_NCT6775F:
	case WBSIO_ID_NCT6776F:
	case WBSIO_ID_NCT5104D:
	case WBSIO_ID_NCT6779D:
	case WBSIO_ID_NCT6791D:
	case WBSIO_ID_NCT6792D:
		ia->ipa_nio = 1;
		ia->ipa_io[0].length = WBSIO_IOSIZE;
		ia->ipa_nmem = 0;
		ia->ipa_nirq = 0;
		ia->ipa_ndrq = 0;
		return (1);
	}

	return (0);
}

void
wbsio_attach(struct device *parent, struct device *self, void *aux)
{
	struct wbsio_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;
	struct isa_attach_args nia;
	u_int8_t devid, reg, reg0, reg1;
	u_int16_t iobase;

	/* Map ISA I/O space */
	sc->sc_iot = ia->ia_iot;
	if (bus_space_map(sc->sc_iot, ia->ipa_io[0].base,
	    WBSIO_IOSIZE, 0, &sc->sc_ioh)) {
		printf(": can't map i/o space\n");
		return;
	}

	/* Enter configuration mode */
	wbsio_conf_enable(sc->sc_iot, sc->sc_ioh);

	/* Read device ID */
	devid = wbsio_conf_read(sc->sc_iot, sc->sc_ioh, WBSIO_ID);
	switch (devid) {
	case WBSIO_ID_W83627HF:
		printf(": W83627HF");
		break;
	case WBSIO_ID_W83627THF:
		printf(": W83627THF");
		break;
	case WBSIO_ID_W83627EHF:
		printf(": W83627EHF");
		break;
	case WBSIO_ID_W83627DHG:
		printf(": W83627DHG");
		break;
	case WBSIO_ID_W83627DHGP:
		printf(": W83627DHG-P");
		break;
	case WBSIO_ID_W83627UHG:
		printf(": W83627UHG");
		break;
	case WBSIO_ID_W83637HF:
		printf(": W83637HF");
		break;
	case WBSIO_ID_W83697HF:
		printf(": W83697HF");
		break;
	case WBSIO_ID_NCT6775F:
		printf(": NCT6775F");
		break;
	case WBSIO_ID_NCT6776F:
		printf(": NCT6776F");
		break;
	case WBSIO_ID_NCT6779D:
		printf(": NCT6779D");
		break;
	case WBSIO_ID_NCT6791D:
		printf(": NCT6791D");
		break;
	case WBSIO_ID_NCT6792D:
		printf(": NCT6792D");
		break;
	case WBSIO_ID_NCT6793D:
		printf(": NCT6793D");
		break;
	case WBSIO_ID_NCT6795D:
		printf(": NCT6795D");
		break;
	case WBSIO_ID_NCT5104D:
		printf(": NCT5104D");
		break;
	}

	/* Read device revision */
	reg = wbsio_conf_read(sc->sc_iot, sc->sc_ioh, WBSIO_REV);
	printf(" rev 0x%02x", reg);

	/* Select HM logical device */
	wbsio_conf_write(sc->sc_iot, sc->sc_ioh, WBSIO_LDN, WBSIO_LDN_HM);

	/*
	 * The address should be 8-byte aligned, but it seems some
	 * BIOSes ignore this.  They get away with it, because
	 * Apparently the hardware simply ignores the lower three
	 * bits.  We do the same here.
	 */
	reg0 = wbsio_conf_read(sc->sc_iot, sc->sc_ioh, WBSIO_HM_ADDR_LSB);
	reg1 = wbsio_conf_read(sc->sc_iot, sc->sc_ioh, WBSIO_HM_ADDR_MSB);
	iobase = (reg1 << 8) | (reg0 & ~0x7);

	printf("\n");

	/* Escape from configuration mode */
	wbsio_conf_disable(sc->sc_iot, sc->sc_ioh);

	if (iobase == 0)
		return;

	nia = *ia;
	nia.ia_iobase = iobase;
	nia.ia_aux = (void *)(u_long)devid; /* pass devid down to wb_match() */

	config_found(self, &nia, wbsio_print);
}

int
wbsio_print(void *aux, const char *pnp)
{
	struct isa_attach_args *ia = aux;

	if (pnp)
		printf("%s", pnp);
	if (ia->ia_iosize)
		printf(" port 0x%x", ia->ia_iobase);
	if (ia->ia_iosize > 1)
		printf("/%d", ia->ia_iosize);
	return (UNCONF);
}
