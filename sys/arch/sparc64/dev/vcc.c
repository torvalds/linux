/*	$OpenBSD: vcc.c,v 1.2 2021/10/24 17:05:04 mpi Exp $	*/
/*
 * Copyright (c) 2009 Mark Kettenis
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
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/autoconf.h>
#include <machine/hypervisor.h>
#include <machine/mdesc.h>

#include <sparc64/dev/cbusvar.h>

#ifdef VCC_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

struct vcc_softc {
	struct device	sc_dv;
	bus_space_tag_t	sc_bustag;
	bus_dma_tag_t	sc_dmatag;
};

int	vcc_match(struct device *, void *, void *);
void	vcc_attach(struct device *, struct device *, void *);

const struct cfattach vcc_ca = {
	sizeof(struct vcc_softc), vcc_match, vcc_attach
};

struct cfdriver vcc_cd = {
	NULL, "vcc", DV_DULL
};

void	vcc_get_channel_endpoint(int, struct cbus_attach_args *);

int
vcc_match(struct device *parent, void *match, void *aux)
{
	struct cbus_attach_args *ca = aux;

	if (strcmp(ca->ca_name, "virtual-console-concentrator") == 0)
		return (1);

	return (0);
}

void
vcc_attach(struct device *parent, struct device *self, void *aux)
{
	struct cbus_attach_args *ca = aux;
	struct cbus_attach_args nca;
	struct md_header *hdr;
	struct md_element *elem;
	const char *name_blk;
	const char *str;
	int idx;
	int arc;

	printf("\n");

	hdr = (struct md_header *)mdesc;
	elem = (struct md_element *)(mdesc + sizeof(struct md_header));
	name_blk = mdesc + sizeof(struct md_header) + hdr->node_blk_sz;

	idx = ca->ca_idx;
	for (; elem[idx].tag != 'E'; idx++) {
		str = name_blk + elem[idx].name_offset;
		if (elem[idx].tag != 'a' || strcmp(str, "fwd") != 0)
			continue;

		arc = elem[idx].d.val;
		str = name_blk + elem[arc].name_offset;
		if (strcmp(str, "virtual-device-port") == 0) {
			str = mdesc_get_prop_str(arc, "vcc-domain-name");
			if (str) {
				bzero(&nca, sizeof(nca));
				nca.ca_name = str;
				nca.ca_node = ca->ca_node;
				nca.ca_bustag = ca->ca_bustag;
				nca.ca_dmatag = ca->ca_dmatag;
				vcc_get_channel_endpoint(arc, &nca);
				config_found(self, &nca, cbus_print);
			}
		}
	}
}

void
vcc_get_channel_endpoint(int idx, struct cbus_attach_args *ca)
{
	struct md_header *hdr;
	struct md_element *elem;
	const char *name_blk;
	const char *str;
	int arc;

	hdr = (struct md_header *)mdesc;
	elem = (struct md_element *)(mdesc + sizeof(struct md_header));
	name_blk = mdesc + sizeof(struct md_header) + hdr->node_blk_sz;

	ca->ca_idx = idx;

	ca->ca_id = -1;
	ca->ca_tx_ino = -1;
	ca->ca_rx_ino = -1;

	for (; elem[idx].tag != 'E'; idx++) {
		str = name_blk + elem[idx].name_offset;
		if (elem[idx].tag != 'a' || strcmp(str, "fwd") != 0)
			continue;

		arc = elem[idx].d.val;
		str = name_blk + elem[arc].name_offset;
		if (strcmp(str, "channel-endpoint") == 0) {
			ca->ca_id = mdesc_get_prop_val(arc, "id");
			ca->ca_tx_ino = mdesc_get_prop_val(arc, "tx-ino");
			ca->ca_rx_ino = mdesc_get_prop_val(arc, "rx-ino");
			return;
		}
	}
}
