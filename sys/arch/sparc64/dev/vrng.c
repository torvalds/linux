/*	$OpenBSD: vrng.c,v 1.7 2021/10/24 17:05:04 mpi Exp $	*/
/*
 * Copyright (c) 2008 Mark Kettenis
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
#include <sys/timeout.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/hypervisor.h>
#include <machine/openfirm.h>
#include <machine/sparc64.h>

#define HSVC_GROUP_RNG	0x104

#include <sparc64/dev/vbusvar.h>

struct rng_ctl {
	uint64_t rng_res : 39;
	uint64_t rng_wait_cnt : 16;
	uint64_t rng_bypass : 1;
	uint64_t rng_vcoctl_sel : 2;
	uint64_t rng_anlg_sel : 2;
	uint64_t rng_ctl4 : 1;
	uint64_t rng_ctl3 : 1;
	uint64_t rng_ctl2 : 1;
	uint64_t rng_ctl1 : 1;
};

struct vrng_softc {
	struct device sc_dv;
	struct timeout sc_to;
	int sc_count;
};

int	vrng_match(struct device *, void *, void *);
void	vrng_attach(struct device *, struct device *, void *);

const struct cfattach vrng_ca = {
	sizeof(struct vrng_softc), vrng_match, vrng_attach
};

struct cfdriver vrng_cd = {
	NULL, "vrng", DV_DULL
};

void	vrng_rnd(void *);

int
vrng_match(struct device *parent, void *match, void *aux)
{
	struct vbus_attach_args *va = aux;

	if (strcmp(va->va_name, "random-number-generator") == 0)
		return (1);

	return (0);
}

void
vrng_attach(struct device *parent, struct device *self, void *aux)
{
	struct vrng_softc *sc = (void *)self;
	uint64_t supported_minor;
	struct rng_ctl ctl[4];
	uint64_t delta;
	paddr_t addr;
	int err;

	if (prom_set_sun4v_api_version(HSVC_GROUP_RNG, 1, 0, &supported_minor))
		printf(": unsupported hypervisor\n");

	err = hv_rng_get_diag_control();
	if (err != H_EOK && err != H_ENOACCESS)
		printf(": hv_rng_get_diag_control %d\n", err);

	/*
	 * If we're not the Trusted Domain, the hypervisor call above
	 * will fails with H_ENOACCESS.  In that case we hope that the
	 * RNG has been properly initialized.
	 */
	if (err == H_EOK) {
		bzero(ctl, sizeof(ctl));

		ctl[0].rng_ctl1 = 1;
		ctl[0].rng_vcoctl_sel = 0;
		ctl[0].rng_wait_cnt = 0x3e;

		ctl[1].rng_ctl2 = 1;
		ctl[1].rng_vcoctl_sel = 1;
		ctl[1].rng_wait_cnt = 0x3e;

		ctl[2].rng_ctl3 = 1;
		ctl[2].rng_vcoctl_sel = 2;
		ctl[2].rng_wait_cnt = 0x3e;

		ctl[3].rng_ctl1 = 1;
		ctl[3].rng_ctl2 = 1;
		ctl[3].rng_ctl3 = 1;
		ctl[3].rng_ctl4 = 1;
		ctl[3].rng_wait_cnt = 0x3e;

		if (!pmap_extract(pmap_kernel(), (vaddr_t)&ctl, &addr))
			panic("vrng_attach: pmap_extract failed");

		err = hv_rng_ctl_write(addr, RNG_STATE_CONFIGURED, 0, &delta);
		if (err != H_EOK)
			printf(": hv_rng_ctl_write %d\n", err);
	}

	printf("\n");

	timeout_set(&sc->sc_to, vrng_rnd, sc);
	vrng_rnd(sc);
}

void
vrng_rnd(void *v)
{
	struct vrng_softc *sc = v;
	uint64_t rnd;
	uint64_t delta;
	paddr_t addr;
	int err;

	if (!pmap_extract(pmap_kernel(), (vaddr_t)&rnd, &addr))
		panic("vrng_rnd: pmap_extract failed");
	err = hv_rng_data_read(addr, &delta);
	if (err == H_EOK) {
#if 0
		if ((sc->sc_count++ % 100) == 0)
			printf("vrng: %lx\n", rnd);
#endif
		enqueue_randomness(rnd);
		enqueue_randomness(rnd >> 32);
	}
	if (err != H_EOK && err != H_EWOULDBLOCK)
		printf("vrng_rnd: err = %d\n", err);
	else
		timeout_add(&sc->sc_to, 1);
}		
