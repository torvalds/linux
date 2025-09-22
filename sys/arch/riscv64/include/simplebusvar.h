/*	$OpenBSD: simplebusvar.h,v 1.1 2023/09/22 01:10:44 jsg Exp $	*/

/*
 * Copyright (c) 2016 Patrick Wildt <patrick@blueri.se>
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

struct simplebus_softc {
	struct device		 sc_dev;
	int			 sc_node;
	bus_space_tag_t		 sc_iot;
	bus_dma_tag_t		 sc_dmat;
	int			 sc_acells;
	int			 sc_scells;
	int			 sc_pacells;
	int			 sc_pscells;
	struct bus_space	 sc_bus;
	struct machine_bus_dma_tag sc_dma;
	int			*sc_ranges;
	int			 sc_rangeslen;
	int			*sc_dmaranges;
	int			 sc_dmarangeslen;
	int			 sc_early;
	int			 sc_early_nodes[64];
};

extern void simplebus_attach(struct device *, struct device *, void *);
