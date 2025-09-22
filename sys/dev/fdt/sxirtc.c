/*	$OpenBSD: sxirtc.c,v 1.9 2024/01/27 11:22:16 kettenis Exp $	*/
/*
 * Copyright (c) 2008 Mark Kettenis
 * Copyright (c) 2013 Artturi Alm
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

#include <dev/clock_subr.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/fdt/sunxireg.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>
#include <dev/ofw/ofw_clock.h>

#define SXIRTC_LOSC_CTRL		0x00
#define  SXIRTC_LOSC_CTRL_KEY_FIELD	0x16aa0000
#define  SXIRTC_LOSC_CTRL_SEL_EXT32K	0x00000001
#define SXIRTC_YYMMDD_A10		0x04
#define SXIRTC_HHMMSS_A10		0x08
#define SXIRTC_YYMMDD_A31		0x10
#define SXIRTC_HHMMSS_A31		0x14
#define SXIRTC_LOSC_OUT_GATING		0x60

#define LEAPYEAR(y)        \
    (((y) % 4 == 0 &&    \
    (y) % 100 != 0) ||    \
    (y) % 400 == 0) 

struct sxirtc_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct clock_device	sc_cd;

	bus_size_t		sc_yymmdd;
	bus_size_t		sc_hhmmss;
	uint32_t		base_year;
	uint32_t		year_mask;
	uint32_t		leap_shift;
	int			linear_day;
};

int	sxirtc_match(struct device *, void *, void *);
void	sxirtc_attach(struct device *, struct device *, void *);

const struct cfattach sxirtc_ca = {
	sizeof(struct sxirtc_softc), sxirtc_match, sxirtc_attach
};

struct cfdriver sxirtc_cd = {
	NULL, "sxirtc", DV_DULL
};

uint32_t sxirtc_get_frequency(void *, uint32_t *);
void	sxirtc_enable(void *, uint32_t *, int);
int	sxirtc_gettime(todr_chip_handle_t, struct timeval *);
int	sxirtc_settime(todr_chip_handle_t, struct timeval *);

int
sxirtc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "allwinner,sun4i-a10-rtc") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun7i-a20-rtc") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun6i-a31-rtc") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun8i-h3-rtc") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun50i-h5-rtc") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun50i-h616-rtc") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun50i-r329-rtc"));
}

void
sxirtc_attach(struct device *parent, struct device *self, void *aux)
{
	struct sxirtc_softc *sc = (struct sxirtc_softc *)self;
	struct fdt_attach_args *faa = aux;
	todr_chip_handle_t handle;

	if (faa->fa_nreg < 1)
		return;

	handle = malloc(sizeof(struct todr_chip_handle), M_DEVBUF, M_NOWAIT);
	if (handle == NULL)
		panic("sxirtc_attach: couldn't allocate todr_handle");

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("sxirtc_attach: bus_space_map failed!");

	if (OF_is_compatible(faa->fa_node, "allwinner,sun6i-a31-rtc") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun8i-h3-rtc") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun50i-h5-rtc") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun50i-h616-rtc") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun50i-r329-rtc")) {
		sc->sc_yymmdd = SXIRTC_YYMMDD_A31;
		sc->sc_hhmmss = SXIRTC_HHMMSS_A31;
	} else {
		sc->sc_yymmdd = SXIRTC_YYMMDD_A10;
		sc->sc_hhmmss = SXIRTC_HHMMSS_A10;
	}

	if (OF_is_compatible(faa->fa_node, "allwinner,sun7i-a20-rtc")) {
		sc->base_year = 1970;
		sc->year_mask = 0xff;
		sc->leap_shift = 24;
	} else {
		sc->base_year = 2010;
		sc->year_mask = 0x3f;
		sc->leap_shift = 22;
	}

	/*
	 * Newer SoCs store the number of days since a fixed epoch
	 * instead of YYMMDD.  Take this to be the number of days
	 * since the Unix epoch since that is what Linux does.
	 */
	if (OF_is_compatible(faa->fa_node, "allwinner,sun50i-h616-rtc") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun50i-r329-rtc")) {
		sc->base_year = 1970;
		sc->linear_day = 1;
	}

	if (OF_is_compatible(faa->fa_node, "allwinner,sun8i-h3-rtc") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun50i-h5-rtc")) {
		/* Switch to external oscillator. */
		SXIWRITE4(sc, SXIRTC_LOSC_CTRL,
		    SXIRTC_LOSC_CTRL_KEY_FIELD | SXIRTC_LOSC_CTRL_SEL_EXT32K);

		sc->sc_cd.cd_node = faa->fa_node;
		sc->sc_cd.cd_cookie = sc;
		sc->sc_cd.cd_get_frequency = sxirtc_get_frequency;
		sc->sc_cd.cd_enable = sxirtc_enable;
		clock_register(&sc->sc_cd);
	}

	handle->cookie = self;
	handle->todr_gettime = sxirtc_gettime;
	handle->todr_settime = sxirtc_settime;
	handle->bus_cookie = NULL;
	handle->todr_setwen = NULL;
	handle->todr_quality = 0;
	todr_attach(handle);

	printf("\n");
}

uint32_t
sxirtc_get_frequency(void *cookie, uint32_t *cells)
{
	struct sxirtc_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case 0:			/* osc32k */
	case 1:			/* osc32k-out */
		return clock_get_frequency_idx(sc->sc_cd.cd_node, 0);
	case 2:			/* iosc */
		return 16000000;
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

void
sxirtc_enable(void *cookie, uint32_t *cells, int on)
{
	struct sxirtc_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case 0:			/* osc32k */
		break;
	case 1:			/* osc32k-out */
		if (on)
			SXISET4(sc, SXIRTC_LOSC_OUT_GATING, 1);
		else
			SXICLR4(sc, SXIRTC_LOSC_OUT_GATING, 1);
		break;
	case 2:			/* iosc */
		break;
	default:
		printf("%s: 0x%08x\n", __func__, idx);
		break;
	}
}

int
sxirtc_gettime(todr_chip_handle_t handle, struct timeval *tv)
{
	struct sxirtc_softc *sc = (struct sxirtc_softc *)handle->cookie;
	struct clock_ymdhms dt;
	uint32_t reg;

	reg = SXIREAD4(sc, sc->sc_yymmdd);
	if (sc->linear_day) {
		clock_secs_to_ymdhms(reg * SECDAY, &dt);
	} else {
		dt.dt_day = reg & 0x1f;
		dt.dt_mon = reg >> 8 & 0x0f;
		dt.dt_year = (reg >> 16 & sc->year_mask) + sc->base_year;
	}

	reg = SXIREAD4(sc, sc->sc_hhmmss);
	dt.dt_sec = reg & 0x3f;
	dt.dt_min = reg >> 8 & 0x3f;
	dt.dt_hour = reg >> 16 & 0x1f;
	dt.dt_wday = reg >> 29 & 0x07;

	if (dt.dt_sec > 59 || dt.dt_min > 59 ||
	    dt.dt_hour > 23 || dt.dt_wday > 6 ||
	    dt.dt_day > 31 || dt.dt_day == 0 ||
	    dt.dt_mon > 12 || dt.dt_mon == 0)
		return 1;

	/*
	 * Reject the first year that can be represented by the clock.
	 * This avoids reporting a bogus time if the RTC isn't battery
	 * powered.
	 */
	if (dt.dt_year == sc->base_year)
		return 1;

	tv->tv_sec = clock_ymdhms_to_secs(&dt);
	tv->tv_usec = 0;
	return 0;
}

int
sxirtc_settime(todr_chip_handle_t handle, struct timeval *tv)
{
	struct sxirtc_softc *sc = (struct sxirtc_softc *)handle->cookie;
	struct clock_ymdhms dt;

	clock_secs_to_ymdhms(tv->tv_sec, &dt);

	if (dt.dt_sec > 59 || dt.dt_min > 59 ||
	    dt.dt_hour > 23 || dt.dt_wday > 6 ||
	    dt.dt_day > 31 || dt.dt_day == 0 ||
	    dt.dt_mon > 12 || dt.dt_mon == 0)
		return 1;

	SXICMS4(sc, sc->sc_hhmmss, 0xe0000000 | 0x1f0000 | 0x3f00 | 0x3f,
	    dt.dt_sec | (dt.dt_min << 8) | (dt.dt_hour << 16) |
	    (dt.dt_wday << 29));

	if (sc->linear_day) {
		SXICMS4(sc, sc->sc_yymmdd, 0xffff, tv->tv_sec / SECDAY);
	} else {
		SXICMS4(sc, sc->sc_yymmdd, 0x00400000 | (sc->year_mask << 16) |
		    0x0f00 | 0x1f, dt.dt_day | (dt.dt_mon << 8) |
		    ((dt.dt_year - sc->base_year) << 16) |
		    (LEAPYEAR(dt.dt_year) << sc->leap_shift));
	}

	return 0;
}
