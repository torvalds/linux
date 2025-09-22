/*	$OpenBSD: imxgpc.c,v 1.10 2021/10/24 17:52:26 mpi Exp $	*/
/*
 * Copyright (c) 2016 Mark Kettenis
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
#include <sys/malloc.h>

#if defined(__arm64__)
#include <machine/cpufunc.h>
#endif
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_power.h>

#define FSL_SIP_GPC			0xc2000000
#define  FSL_SIP_CONFIG_GPC_PM_DOMAIN		0x03

struct imxgpc_softc {
	struct device	sc_dev;
	struct interrupt_controller sc_ic;

	int		sc_npd;
	struct power_domain_device *sc_pd;
};

int	imxgpc_match(struct device *, void *, void *);
void	imxgpc_attach(struct device *, struct device *, void *);
void	imxgpc_enable(void *, uint32_t *, int);

const struct cfattach imxgpc_ca = {
	sizeof(struct imxgpc_softc), imxgpc_match, imxgpc_attach
};

struct cfdriver imxgpc_cd = {
	NULL, "imxgpc", DV_DULL
};

int
imxgpc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "fsl,imx6q-gpc") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx7d-gpc") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx8mm-gpc") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx8mp-gpc") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx8mq-gpc"));
}

void
imxgpc_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct imxgpc_softc *sc = (struct imxgpc_softc *)self;
	int i, node, list;

	sc->sc_ic.ic_node = faa->fa_node;
	sc->sc_ic.ic_cookie = &sc->sc_ic;
	sc->sc_ic.ic_establish = fdt_intr_parent_establish;
	sc->sc_ic.ic_disestablish = fdt_intr_parent_disestablish;
	sc->sc_ic.ic_barrier = intr_barrier;
	fdt_intr_register(&sc->sc_ic);

	printf("\n");

	if (OF_is_compatible(faa->fa_node, "fsl,imx8mm-gpc") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx8mp-gpc") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx8mq-gpc")) {
		list = OF_child(faa->fa_node);
		if (!list)
			return;
		for (node = OF_child(list); node; node = OF_peer(node))
			sc->sc_npd++;
		if (!sc->sc_npd)
			return;
		sc->sc_pd = mallocarray(sc->sc_npd, sizeof(*sc->sc_pd),
		    M_DEVBUF, M_WAITOK);
		for (node = OF_child(list), i = 0; node;
		    node = OF_peer(node), i++){
			sc->sc_pd[i].pd_node = node;
			sc->sc_pd[i].pd_cookie = &sc->sc_pd[i];
			sc->sc_pd[i].pd_enable = imxgpc_enable;
			power_domain_register(&sc->sc_pd[i]);
		}
	}
}

void
imxgpc_enable(void *cookie, uint32_t *cells, int on)
{
#if defined(__arm64__)
	struct power_domain_device *pd = cookie;
	int domain;

	power_domain_enable(pd->pd_node);

	domain = OF_getpropint(pd->pd_node, "reg", 0);

	/* Set up power domain */
	smc_call(FSL_SIP_GPC, FSL_SIP_CONFIG_GPC_PM_DOMAIN,
	    domain, on);
#endif
}
