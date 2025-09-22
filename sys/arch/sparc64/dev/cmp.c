/*	$OpenBSD: cmp.c,v 1.3 2021/10/24 17:05:03 mpi Exp $	*/
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
#include <sys/systm.h>

#include <machine/autoconf.h>
#include <machine/openfirm.h>

int	cmp_match(struct device *, void *, void *);
void	cmp_attach(struct device *, struct device *, void *);

const struct cfattach cmp_ca = {
	sizeof(struct device), cmp_match, cmp_attach
};

struct cfdriver cmp_cd = {
	NULL, "cmp", DV_DULL
};

int	cmp_print(void *, const char *);

int
cmp_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, "cmp") == 0)
		return (1);

	return (0);
}

void
cmp_attach(struct device *parent, struct device *self, void *aux)
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
		nma.ma_node = node;
		nma.ma_name = buf;
		config_found(self, &nma, cmp_print);
	}
}

int
cmp_print(void *aux, const char *name)
{
	struct mainbus_attach_args *ma = aux;

	if (name)
		printf("\"%s\" at %s", ma->ma_name, name);
	return (UNCONF);
}
