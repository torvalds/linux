/*	$OpenBSD: octuctlvar.h,v 1.2 2017/07/25 11:01:28 jmatthew Exp $ */

/*
 * Copyright (c) 2015 Jonathan Matthew  <jmatthew@openbsd.org>
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

#ifndef	_OCTUCTLVAR_H_
#define	_OCTUCTLVAR_H_

#include <machine/bus.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

struct octuctl_attach_args {
	bus_space_tag_t		aa_octuctl_bust;
	bus_space_tag_t  	aa_bust;
	bus_dma_tag_t	 	aa_dmat;
	bus_space_handle_t 	aa_ioh;
	int			aa_node;
	struct fdt_reg		aa_reg;
};

#endif	/* _OCTUCTLVAR_H_ */
