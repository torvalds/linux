/*	$OpenBSD: efifbvar.h,v 1.10 2019/05/04 11:34:47 kettenis Exp $	*/

/*
 * Copyright (c) 2015 YASUOKA Masahiko <yasuoka@yasuoka.net>
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

#ifndef _MACHINE_EFIFB_H_
#define _MACHINE_EFIFB_H_

struct efifb_attach_args {
	const char		*eaa_name;
};

struct pci_attach_args;

int efifb_cnattach(void);
void efifb_cnremap(void);
int efifb_is_console(struct pci_attach_args *);
int efifb_is_primary(struct pci_attach_args *);
void efifb_detach(void);
void efifb_reattach(void);

int efifb_cb_found(void);
int efifb_cb_cnattach(void);

psize_t efifb_stolen(void);

#endif /* _MACHINE_EFIFB_H_ */
