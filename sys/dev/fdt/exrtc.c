/*	$OpenBSD: exrtc.c,v 1.5 2022/10/17 19:09:46 kettenis Exp $	*/
/*
 * Copyright (c) 2017 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/time.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#include <dev/clock_subr.h>

#define RTCCTRL		0x40
#define RTCCTRL_RTCEN	(1 << 0)

#define RTCSEC		0x70
#define RTCMIN		0x74
#define RTCHOUR		0x78
#define RTCDAY		0x7c
#define RTCMON		0x84
#define RTCYEAR		0x88

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct exrtc_softc {
	struct device	sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	struct todr_chip_handle sc_todr;
};

int	exrtc_match(struct device *, void *, void *);
void	exrtc_attach(struct device *, struct device *, void *);

const struct cfattach exrtc_ca = {
	sizeof(struct exrtc_softc), exrtc_match, exrtc_attach
};

struct cfdriver exrtc_cd = {
	NULL, "exrtc", DV_DULL
};

int	exrtc_gettime(todr_chip_handle_t, struct timeval *);
int	exrtc_settime(todr_chip_handle_t, struct timeval *);

int
exrtc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "samsung,s3c6410-rtc");
}

void
exrtc_attach(struct device *parent, struct device *self, void *aux)
{
	struct exrtc_softc *sc = (struct exrtc_softc *)self;
	struct fdt_attach_args *faa = aux;

	sc->sc_iot = faa->fa_iot;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	printf("\n");

	sc->sc_todr.cookie = sc;
	sc->sc_todr.todr_gettime = exrtc_gettime;
	sc->sc_todr.todr_settime = exrtc_settime;
	sc->sc_todr.todr_quality = 0;
	todr_attach(&sc->sc_todr);
}

int
exrtc_gettime(todr_chip_handle_t handle, struct timeval *tv)
{
	struct exrtc_softc *sc = handle->cookie;
	struct clock_ymdhms dt;
	int retried = 0;

retry:
	dt.dt_sec = FROMBCD(HREAD4(sc, RTCSEC));
	dt.dt_min = FROMBCD(HREAD4(sc, RTCMIN));
	dt.dt_hour = FROMBCD(HREAD4(sc, RTCHOUR));
	dt.dt_day = FROMBCD(HREAD4(sc, RTCDAY));
	dt.dt_mon = FROMBCD(HREAD4(sc, RTCMON));
	dt.dt_year = FROMBCD(HREAD4(sc, RTCYEAR)) + 1900;

	/* If the second counter rolled over, retry. */
	if (dt.dt_sec > FROMBCD(HREAD4(sc, RTCSEC)) && !retried) {
		retried = 1;
		goto retry;
	}

	if (dt.dt_sec > 59 || dt.dt_min > 59 || dt.dt_hour > 23 ||
	    dt.dt_day > 31 || dt.dt_day == 0 ||
	    dt.dt_mon > 12 || dt.dt_mon == 0 ||
	    dt.dt_year < POSIX_BASE_YEAR)
		return 1;

	tv->tv_sec = clock_ymdhms_to_secs(&dt);
	tv->tv_usec = 0;
	return 0;
}

int
exrtc_settime(todr_chip_handle_t handle, struct timeval *tv)
{
	struct exrtc_softc *sc = handle->cookie;
	struct clock_ymdhms dt;
	uint32_t val;

	clock_secs_to_ymdhms(tv->tv_sec, &dt);

	HWRITE4(sc, RTCSEC, TOBCD(dt.dt_sec));
	HWRITE4(sc, RTCMIN, TOBCD(dt.dt_min));
	HWRITE4(sc, RTCHOUR, TOBCD(dt.dt_hour));
	HWRITE4(sc, RTCDAY, TOBCD(dt.dt_day));
	HWRITE4(sc, RTCMON, TOBCD(dt.dt_mon));
	HWRITE4(sc, RTCYEAR, TOBCD(dt.dt_year - 1900));

	val = HREAD4(sc, RTCCTRL);
	HWRITE4(sc, RTCCTRL, val | RTCCTRL_RTCEN);

	return 0;
}
