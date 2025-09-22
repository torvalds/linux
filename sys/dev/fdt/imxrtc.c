/*	$OpenBSD: imxrtc.c,v 1.3 2022/10/17 19:09:46 kettenis Exp $	*/
/*
 * Copyright (c) 2018 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>
#include <dev/ofw/ofw_misc.h>

#include <dev/clock_subr.h>

/* Registers. */
#define LPCR			0x38
#define  LPCR_SRTC_ENV		(1 << 0)
#define LPSR			0x4c
#define LPSRTCMR		0x50
#define LPSRTCLR		0x54

#define HREAD4(sc, reg)							\
	(regmap_read_4((sc)->sc_rm, (reg)))
#define HWRITE4(sc, reg, val)						\
	regmap_write_4((sc)->sc_rm, (reg), (val))

struct imxrtc_softc {
	struct device		sc_dev;
	struct regmap		*sc_rm;

	struct todr_chip_handle sc_todr;
};

int imxrtc_match(struct device *, void *, void *);
void imxrtc_attach(struct device *, struct device *, void *);

const struct cfattach	imxrtc_ca = {
	sizeof (struct imxrtc_softc), imxrtc_match, imxrtc_attach
};

struct cfdriver imxrtc_cd = {
	NULL, "imxrtc", DV_DULL
};

int	imxrtc_gettime(struct todr_chip_handle *, struct timeval *);
int	imxrtc_settime(struct todr_chip_handle *, struct timeval *);

int
imxrtc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "fsl,sec-v4.0-mon-rtc-lp");
}

void
imxrtc_attach(struct device *parent, struct device *self, void *aux)
{
	struct imxrtc_softc *sc = (struct imxrtc_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t regmap;

	regmap = OF_getpropint(faa->fa_node, "regmap", 0);
	sc->sc_rm = regmap_byphandle(regmap);
	if (sc->sc_rm == NULL) {
		printf(": no registers\n");
		return;
	}

	printf("\n");

	sc->sc_todr.cookie = sc;
	sc->sc_todr.todr_gettime = imxrtc_gettime;
	sc->sc_todr.todr_settime = imxrtc_settime;
	sc->sc_todr.todr_quality = 0;
	todr_attach(&sc->sc_todr);
}

int
imxrtc_gettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct imxrtc_softc *sc = handle->cookie;
	uint64_t mr, lr, srtc, srtc2;
	uint32_t cr;
	int retries;
	int s;

	cr = HREAD4(sc, LPCR);
	if ((cr & LPCR_SRTC_ENV) == 0)
		return EINVAL;

	/*
	 * Read counters until we read back the same values twice.
	 * This shouldn't take more than two attempts; throw in an
	 * extra round just in case.
	 */
	s = splhigh();
	mr = HREAD4(sc, LPSRTCMR);
	lr = HREAD4(sc, LPSRTCLR);
	srtc = (mr << 32) | lr;
	for (retries = 3; retries > 0; retries--) {
		mr = HREAD4(sc, LPSRTCMR);
		lr = HREAD4(sc, LPSRTCLR);
		srtc2 = (mr << 32) | lr;
		if (srtc == srtc2)
			break;
		srtc = srtc2;
	}
	splx(s);
	if (retries == 0)
		return EIO;

	tv->tv_sec = srtc / 32768;
	tv->tv_usec = ((srtc % 32768) * 1000000U) / 32768U;
	return 0;
}

int
imxrtc_settime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct imxrtc_softc *sc = handle->cookie;
	uint64_t srtc;
	uint32_t cr;
	int timeout;

	/* Disable RTC. */
	cr = HREAD4(sc, LPCR);
	cr &= ~LPCR_SRTC_ENV;
	HWRITE4(sc, LPCR, cr);
	for (timeout = 1000000; timeout > 0; timeout--) {
		if ((HREAD4(sc, LPCR) & LPCR_SRTC_ENV) == 0)
			break;
	}

	srtc = tv->tv_sec * 32768 + (tv->tv_usec * 32768U / 1000000U);
	HWRITE4(sc, LPSRTCMR, srtc >> 32);
	HWRITE4(sc, LPSRTCLR, srtc & 0xffffffff);

	/* Enable RTC. */
	cr |= LPCR_SRTC_ENV;
	HWRITE4(sc, LPCR, cr);
	for (timeout = 1000000; timeout > 0; timeout--) {
		if (HREAD4(sc, LPCR) & LPCR_SRTC_ENV)
			break;
	}

	return 0;
}
