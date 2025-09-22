/*	$OpenBSD: bcm2835_clock.c,v 1.4 2025/08/24 10:50:43 kettenis Exp $	*/

/*
 * Copyright (c) 2020 Tobias Heider <tobhe@openbsd.org>
 * Copyright (c) 2019 Neil Ashford <ashfordneil0@gmail.com>
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

/*-
 * Copyright (c) 2017 Jared D. McNeill <jmcneill@invisible.ca>
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <machine/fdt.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/openfirm.h>

#include <dev/ic/bcm2835_mbox.h>
#include <dev/ic/bcm2835_vcprop.h>

enum {
	BCMCLOCK_CLOCK_TIMER = 17,
	BCMCLOCK_CLOCK_UART = 19,
	BCMCLOCK_CLOCK_VPU = 20,
	BCMCLOCK_CLOCK_V3D = 21,
	BCMCLOCK_CLOCK_ISP = 22,
	BCMCLOCK_CLOCK_H264 = 23,
	BCMCLOCK_CLOCK_VEC = 24,
	BCMCLOCK_CLOCK_HSM = 25,
	BCMCLOCK_CLOCK_SDRAM = 26,
	BCMCLOCK_CLOCK_TSENS = 27,
	BCMCLOCK_CLOCK_EMMC = 28,
	BCMCLOCK_CLOCK_PERIIMAGE = 29,
	BCMCLOCK_CLOCK_PWM = 30,
	BCMCLOCK_CLOCK_PCM = 31,
	BCMCLOCK_NCLOCK
};


struct bcmclock_softc {
	struct device		sc_dev;
	struct clock_device	sc_cd;

};

int bcmclock_match(struct device *, void *, void *);
void bcmclock_attach(struct device *, struct device *, void *);

const struct cfattach bcmclock_ca = {
	sizeof(struct bcmclock_softc),
	bcmclock_match,
	bcmclock_attach,
};

uint32_t bcmclock_get_frequency(void *, uint32_t *);

struct cfdriver bcmclock_cd = { NULL, "bcmclock", DV_DULL };

int
bcmclock_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "brcm,bcm2711-cprman") ||
	    OF_is_compatible(faa->fa_node, "brcm,bcm2835-cprman"));
}

void
bcmclock_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmclock_softc *sc = (struct bcmclock_softc *)self;
	struct fdt_attach_args *faa = aux;

	printf("\n");

	sc->sc_cd.cd_node = faa->fa_node;
	sc->sc_cd.cd_cookie = sc;
	sc->sc_cd.cd_get_frequency = bcmclock_get_frequency;

	clock_register(&sc->sc_cd);
}

uint32_t
bcmclock_get_frequency(void *cookie, uint32_t *cells)
{
	struct request {
		struct vcprop_buffer_hdr vb_hdr;
		struct vcprop_tag_clockrate vbt_clkrate;
		struct vcprop_tag end;
	} __packed;

	uint32_t result;
	struct request req = {
		.vb_hdr = {
			.vpb_len = sizeof(req),
			.vpb_rcode = VCPROP_PROCESS_REQUEST,
		},
		.vbt_clkrate = {
			.tag = {
				.vpt_tag = VCPROPTAG_GET_CLOCKRATE,
				.vpt_len = VCPROPTAG_LEN(req.vbt_clkrate),
				.vpt_rcode = VCPROPTAG_REQUEST
			},
		},
		.end = {
			.vpt_tag = VCPROPTAG_NULL
		}
	};

	switch (cells[0]) {
	case BCMCLOCK_CLOCK_TIMER:
		break;
	case BCMCLOCK_CLOCK_UART:
		req.vbt_clkrate.id = VCPROP_CLK_UART;
		break;
	case BCMCLOCK_CLOCK_VPU:
		req.vbt_clkrate.id = VCPROP_CLK_CORE;
		break;
	case BCMCLOCK_CLOCK_V3D:
		req.vbt_clkrate.id = VCPROP_CLK_V3D;
		break;
	case BCMCLOCK_CLOCK_ISP:
		req.vbt_clkrate.id = VCPROP_CLK_ISP;
		break;
	case BCMCLOCK_CLOCK_H264:
		req.vbt_clkrate.id = VCPROP_CLK_H264;
		break;
	case BCMCLOCK_CLOCK_VEC:
		break;
	case BCMCLOCK_CLOCK_HSM:
		break;
	case BCMCLOCK_CLOCK_SDRAM:
		req.vbt_clkrate.id = VCPROP_CLK_SDRAM;
		break;
	case BCMCLOCK_CLOCK_TSENS:
		break;
	case BCMCLOCK_CLOCK_EMMC:
		req.vbt_clkrate.id = VCPROP_CLK_EMMC;
		break;
	case BCMCLOCK_CLOCK_PERIIMAGE:
		break;
	case BCMCLOCK_CLOCK_PWM:
		req.vbt_clkrate.id = VCPROP_CLK_PWM;
		break;
	case BCMCLOCK_CLOCK_PCM:
		break;
	}

	if (req.vbt_clkrate.id == 0) {
		printf("bcmclock[unknown]: request to unknown clock type %d\n",
		       cells[0]);
		return 0;
	}

	bcmmbox_post(BCMMBOX_CHANARM2VC, &req, sizeof(req), &result);

	if (vcprop_tag_success_p(&req.vbt_clkrate.tag))
		return req.vbt_clkrate.rate;

	printf("%s: vcprop result %x:%x\n", __func__, req.vb_hdr.vpb_rcode,
	       req.vbt_clkrate.tag.vpt_rcode);

	return 0;
}
