/*	$OpenBSD: voyagervar.h,v 1.2 2010/02/26 14:53:11 miod Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
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

struct voyager_attach_args {
	const char			*vaa_name;

	struct pci_attach_args		*vaa_pa;
	bus_space_tag_t			 vaa_fbt;
	bus_space_handle_t		 vaa_fbh;
	bus_space_tag_t			 vaa_mmiot;
	bus_space_handle_t		 vaa_mmioh;
};

void	*voyager_intr_establish(void *, int, int, int (*)(void *), void *,
	    const char *);
const char *
	 voyager_intr_string(void *);
