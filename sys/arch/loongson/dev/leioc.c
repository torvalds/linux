/*	$OpenBSD: leioc.c,v 1.1 2016/11/17 14:41:21 visa Exp $	*/

/*
 * Copyright (c) 2016 Visa Hankala
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

/*
 * Driver for the Loongson 3A low-end IO controller.
 *
 * Each Loongson 3A CPU package has a built-in `low-end' IO controller.
 * The controller provides GPIO, LPCI, PCI, SPI, and UART interfaces.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>

#include <machine/autoconf.h>

#include <loongson/dev/leiocreg.h>
#include <loongson/dev/leiocvar.h>

int	leioc_match(struct device *, void *, void*);
void	leioc_attach(struct device *, struct device *, void *);

const struct cfattach leioc_ca = {
	sizeof(struct device), leioc_match, leioc_attach
};

struct cfdriver leioc_cd = {
	NULL, "leioc", DV_DULL
};

struct mips_bus_space leioc_io_space_tag = {
	.bus_base = PHYS_TO_XKPHYS(0, CCA_NC),
	._space_read_1 = generic_space_read_1,
	._space_write_1 = generic_space_write_1,
	._space_map = generic_space_map,
	._space_unmap = generic_space_unmap
};

int
leioc_match(struct device *parent, void *cfg, void *aux)
{
	struct mainbus_attach_args *maa = aux;

	if (loongson_ver != 0x3a && loongson_ver != 0x3b)
		return 0;

	if (strcmp(maa->maa_name, leioc_cd.cd_name) != 0)
		return 0;

	return 1;
}

void
leioc_attach(struct device *parent, struct device *self, void *aux)
{
	struct leioc_attach_args laa;

	printf("\n");

	laa.laa_name = "com";
	laa.laa_iot = &leioc_io_space_tag;
	laa.laa_base = LEIOC_UART0_BASE;
	config_found(self, &laa, NULL);
	laa.laa_base = LEIOC_UART1_BASE;
	config_found(self, &laa, NULL);
}
