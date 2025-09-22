/*	$OpenBSD: cbusvar.h,v 1.5 2015/01/25 21:42:13 kettenis Exp $	*/
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

#ifndef _SPARC64_DEV_CBUSVAR_H_
#define _SPARC64_DEV_CBUSVAR_H_

struct cbus_attach_args {
	const char	*ca_name;
	int		ca_node;
	int		ca_idx;

	bus_space_tag_t	ca_bustag;
	bus_dma_tag_t	ca_dmatag;

	u_int32_t	*ca_reg;
	int		ca_nreg;

	u_int64_t	ca_id;
	u_int64_t	ca_tx_ino;
	u_int64_t	ca_rx_ino;
};

int	cbus_print(void *, const char *);
int	cbus_intr_setstate(bus_space_tag_t, uint64_t, uint64_t);
int	cbus_intr_setenabled(bus_space_tag_t, uint64_t, uint64_t);

#endif
