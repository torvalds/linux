/*	$OpenBSD: octpip.c,v 1.3 2020/09/08 13:54:48 visa Exp $	*/

/*
 * Copyright (c) 2019 Visa Hankala
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>

#include <machine/fdt.h>

#include <octeon/dev/iobusvar.h>
#include <octeon/dev/cn30xxgmxreg.h>

struct octpip_softc {
	struct device		 sc_dev;
};

int	 octpip_match(struct device *, void *, void *);
void	 octpip_attach(struct device *, struct device *, void *);
int	 octpip_print(void *, const char *);

const struct cfattach octpip_ca = {
	sizeof(struct octpip_softc), octpip_match, octpip_attach
};

struct cfdriver octpip_cd = {
	NULL, "octpip", DV_DULL
};

int
octpip_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "cavium,octeon-3860-pip");
}

void
octpip_attach(struct device *parent, struct device *self, void *aux)
{
	struct iobus_attach_args iaa;
	struct fdt_attach_args *faa = aux;
	paddr_t addr;
	uint32_t ifindex;
	int node;

	printf("\n");

	for (node = OF_child(faa->fa_node); node != 0; node = OF_peer(node)) {
		if (!OF_is_compatible(node, "cavium,octeon-3860-pip-interface"))
			continue;

		ifindex = OF_getpropint(node, "reg", (uint32_t)-1);
		if (ifindex < 2)
			addr = GMX0_BASE_PORT0 + GMX_BLOCK_SIZE * ifindex;
		else if (ifindex == 4)
			addr = AGL_BASE;
		else
			continue;

		memset(&iaa, 0, sizeof(iaa));
		iaa.aa_name = "octgmx";
		iaa.aa_bust = faa->fa_iot;
		iaa.aa_dmat = faa->fa_dmat;
		iaa.aa_addr = addr;
		iaa.aa_irq = -1;
		iaa.aa_unitno = ifindex;
		config_found(self, &iaa, octpip_print);
	}
}

int
octpip_print(void *aux, const char *parentname)
{
	struct iobus_attach_args *iaa = aux;

	if (parentname != NULL)
		printf("%s at %s", iaa->aa_name, parentname);
	printf(" interface %d", iaa->aa_unitno);

	return UNCONF;
}
