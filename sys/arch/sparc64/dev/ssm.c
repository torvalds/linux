/*	$OpenBSD: ssm.c,v 1.3 2021/10/24 17:05:04 mpi Exp $	*/
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

#include <machine/autoconf.h>
#include <machine/openfirm.h>

int	ssm_match(struct device *, void *, void *);
void	ssm_attach(struct device *, struct device *, void *);

const struct cfattach ssm_ca = {
	sizeof(struct device), ssm_match, ssm_attach
};

struct cfdriver ssm_cd = {
	NULL, "ssm", DV_DULL
};

int	ssm_print(void *, const char *);

int
ssm_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, "ssm") == 0)
		return (1);

	return (0);
}

void
ssm_attach(struct device *parent, struct device *self, void *aux)
{
	struct mainbus_attach_args *ma = aux;
	struct mainbus_attach_args nma;
	char buf[32];
	int node;

	printf("\n");

	for (node = OF_child(ma->ma_node); node; node = OF_peer(node)) {
		if (!checkstatus(node))
			continue;

		OF_getprop(node, "name", buf, sizeof(buf));
		if (strcmp(buf, "cpu") == 0)
			OF_getprop(node, "compatible", buf, sizeof(buf));

		bzero(&nma, sizeof(nma));
		nma.ma_bustag = ma->ma_bustag;
		nma.ma_dmatag = ma->ma_dmatag;
		nma.ma_node = node;
		nma.ma_name = buf;
		nma.ma_upaid = getpropint(node, "portid", -1);
		getprop(node, "reg", sizeof(*nma.ma_reg),
		    &nma.ma_nreg, (void **)&nma.ma_reg);
		config_found(self, &nma, ssm_print);
		free(nma.ma_reg, M_DEVBUF, 0);
	}
}

int
ssm_print(void *aux, const char *name)
{
	struct mainbus_attach_args *ma = aux;

	if (name)
		printf("\"%s\" at %s", ma->ma_name, name);
	return (UNCONF);
}
