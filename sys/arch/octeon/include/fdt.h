/*	$OpenBSD: fdt.h,v 1.2 2018/08/06 10:52:30 patrick Exp $	*/

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

#ifndef __OCTEON_FDT_H__
#define __OCTEON_FDT_H__

#include <machine/bus.h>

struct fdt_attach_args {
	const char		*fa_name;
	int			 fa_node;
	bus_space_tag_t		 fa_iot;
	bus_dma_tag_t		 fa_dmat;
	struct fdt_reg		*fa_reg;
	int			 fa_nreg;
	uint32_t		*fa_intr;
	int			 fa_nintr;
	int			 fa_acells;
	int			 fa_scells;
};

#define fdt_intr_establish octeon_intr_establish_fdt
#define fdt_intr_disestablish octeon_intr_disestablish_fdt

#endif /* __OCTEON_FDT_H__ */
