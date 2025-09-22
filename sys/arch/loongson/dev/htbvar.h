/*	$OpenBSD: htbvar.h,v 1.3 2024/05/22 14:22:27 jsg Exp $	*/

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

#ifndef _LOONGSON_DEV_HTBVAR_H_
#define _LOONGSON_DEV_HTBVAR_H_

struct htb_config {
	void		(*hc_attach_hook)(pci_chipset_tag_t);
};

extern struct mips_bus_space htb_pci_mem_space_tag;
extern struct mips_bus_space htb_pci_io_space_tag;

void	 htb_early_setup(void);

#endif /* _LOONGSON_DEV_HTBVAR_H_ */
