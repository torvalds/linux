/*	$OpenBSD: rpirtc.c,v 1.1 2025/08/24 10:51:42 kettenis Exp $	*/
/*
 * Copyright (c) 2025 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/fdt.h>

#include <dev/clock_subr.h>

#include <dev/ofw/openfirm.h>

#include <dev/ic/bcm2835_mbox.h>
#include <dev/ic/bcm2835_vcprop.h>

struct rpirtc_softc {
	struct device		sc_dev;
	struct todr_chip_handle sc_todr;
};

int	rpirtc_match(struct device *, void *, void *);
void	rpirtc_attach(struct device *, struct device *, void *);

const struct cfattach rpirtc_ca = {
	sizeof (struct rpirtc_softc), rpirtc_match, rpirtc_attach
};

struct cfdriver rpirtc_cd = {
	NULL, "rpirtc", DV_DULL
};

int	rpirtc_gettime(struct todr_chip_handle *, struct timeval *);
int	rpirtc_settime(struct todr_chip_handle *, struct timeval *);

int
rpirtc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "raspberrypi,rpi-rtc");
}

void
rpirtc_attach(struct device *parent, struct device *self, void *aux)
{
	struct rpirtc_softc *sc = (struct rpirtc_softc *)self;

	sc->sc_todr.cookie = sc;
	sc->sc_todr.todr_gettime = rpirtc_gettime;
	sc->sc_todr.todr_settime = rpirtc_settime;
	sc->sc_todr.todr_quality = 0;
	todr_attach(&sc->sc_todr);

	printf("\n");
}

int
rpirtc_gettime(struct todr_chip_handle *ch, struct timeval *tv) {
	struct request {
		struct vcprop_buffer_hdr vb_hdr;
		struct vcprop_tag_rtc vbt_rtc;
		struct vcprop_tag end;
	} __packed;

	uint32_t result;
	struct request req = {
		.vb_hdr = {
			.vpb_len = sizeof(req),
			.vpb_rcode = VCPROP_PROCESS_REQUEST,
		},
		.vbt_rtc = {
			.tag = {
				.vpt_tag = VCPROPTAG_GET_RTC_REG,
				.vpt_len = VCPROPTAG_LEN(req.vbt_rtc),
				.vpt_rcode = VCPROPTAG_REQUEST,
			},
			.id = VCPROP_RTC_TIME,
		},
		.end = {
			.vpt_tag = VCPROPTAG_NULL,
		}
	};

	bcmmbox_post(BCMMBOX_CHANARM2VC, &req, sizeof(req), &result);

	if (vcprop_tag_success_p(&req.vbt_rtc.tag)) {
		tv->tv_sec = req.vbt_rtc.data;
		tv->tv_usec = 0;
		return 0;
	}

	printf("%s: vcprop result %x:%x\n", __func__, req.vb_hdr.vpb_rcode,
	    req.vbt_rtc.tag.vpt_rcode);
	return EIO;
}

int
rpirtc_settime(struct todr_chip_handle *ch, struct timeval *tv) {
	struct request {
		struct vcprop_buffer_hdr vb_hdr;
		struct vcprop_tag_rtc vbt_rtc;
		struct vcprop_tag end;
	} __attribute((aligned(16), packed));

	uint32_t result;
	struct request req = {
		.vb_hdr = {
			.vpb_len = sizeof(req),
			.vpb_rcode = VCPROP_PROCESS_REQUEST,
		},
		.vbt_rtc = {
			.tag = {
				.vpt_tag = VCPROPTAG_SET_RTC_REG,
				.vpt_len = VCPROPTAG_LEN(req.vbt_rtc),
				.vpt_rcode = VCPROPTAG_REQUEST,
			},
			.id = VCPROP_RTC_TIME,
			.data = tv->tv_sec,
		},
		.end = {
			.vpt_tag = VCPROPTAG_NULL,
		}
	};

	bcmmbox_post(BCMMBOX_CHANARM2VC, &req, sizeof(req), &result);

	if (vcprop_tag_success_p(&req.vbt_rtc.tag))
		return 0;

        printf("%s: vcprop result %x:%x\n", __func__, req.vb_hdr.vpb_rcode,
	    req.vbt_rtc.tag.vpt_rcode);
	return EIO;
}
