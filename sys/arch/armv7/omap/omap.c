/* $OpenBSD: omap.c,v 1.24 2021/10/24 17:52:27 mpi Exp $ */
/*
 * Copyright (c) 2005,2008 Dale Rahn <drahn@openbsd.org>
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

#include <machine/bus.h>

#include <arm/mainbus/mainbus.h>
#include <armv7/armv7/armv7var.h>

#include <dev/ofw/fdt.h>

int	omap_match(struct device *, void *, void *);
void	omap3_init(void);
void	omap4_init(void);
void	am335x_init(void);

const struct cfattach omap_ca = {
	sizeof(struct armv7_softc), omap_match, armv7_attach
};

struct cfdriver omap_cd = {
	NULL, "omap", DV_DULL
};

struct board_dev omap3_dev[] = {
	{ "prcm",	0 },
	{ "gptimer",	0 },
	{ "gptimer",	1 },
	{ NULL,		0 }
};

struct board_dev am33xx_dev[] = {
	{ "prcm",	0 },
	{ "dmtimer",	0 },
	{ "dmtimer",	1 },
	{ NULL,		0 }
};

struct board_dev omap4_dev[] = {
	{ "omapid",	0 },
	{ "prcm",	0 },
	{ NULL,		0 }
};

struct omap_soc {
	char			*compatible;
	struct board_dev	*devs;
	void			(*init)(void);
};

struct omap_soc omap_socs[] = {
	{
		"ti,omap3",
		omap3_dev,
		omap3_init,
	},
	{
		"ti,am33xx",
		am33xx_dev,
		am335x_init,
	},
	{
		"ti,omap4",
		omap4_dev,
		omap4_init,
	},
	{ NULL, NULL, NULL },
};

struct board_dev *
omap_board_devs(void)
{
	void *node;
	int i;

	node = fdt_find_node("/");
	if (node == NULL)
		return NULL;

	for (i = 0; omap_socs[i].compatible != NULL; i++) {
		if (fdt_is_compatible(node, omap_socs[i].compatible))
			return omap_socs[i].devs;
	}
	return NULL;
}

void
omap_board_init(void)
{
	void *node;
	int i;

	node = fdt_find_node("/");
	if (node == NULL)
		return;

	for (i = 0; omap_socs[i].compatible != NULL; i++) {
		if (fdt_is_compatible(node, omap_socs[i].compatible)) {
			omap_socs[i].init();
			break;
		}
	}
}

int
omap_match(struct device *parent, void *cfdata, void *aux)
{
	union mainbus_attach_args *ma = (union mainbus_attach_args *)aux;
	struct cfdata *cf = (struct cfdata *)cfdata;

	if (ma->ma_name == NULL)
		return (0);

	if (strcmp(cf->cf_driver->cd_name, ma->ma_name) != 0)
		return (0);

	return (omap_board_devs() != NULL);
}
