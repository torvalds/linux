/*	$OpenBSD: efiboot.h,v 1.6 2022/04/06 21:27:03 kettenis Exp $	*/

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

void	*efi_acpi(void);
void	efi_cleanup(void);
void	efi_diskprobe(void);
void	efi_pxeprobe(void);
void	*efi_makebootargs(char *, int);
void	efi_cons_probe(struct consdev *);
void	efi_cons_init(struct consdev *);
int	efi_cons_getc(dev_t);
void	efi_cons_putc(dev_t, int);
void	efi_com_probe(struct consdev *);
void	efi_com_init(struct consdev *);
int	efi_com_getc(dev_t);
void	efi_com_putc(dev_t, int);
void	efi_fb_probe(struct consdev *);
void	efi_fb_init(struct consdev *);
int	efi_fb_getc(dev_t);
void	efi_fb_putc(dev_t, int);
