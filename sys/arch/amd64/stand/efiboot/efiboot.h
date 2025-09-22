/*	$OpenBSD: efiboot.h,v 1.7 2025/08/27 09:08:12 jmatthew Exp $	*/

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

void	 efi_cleanup(void);
void	 efi_cons_probe(struct consdev *);
void	 efi_memprobe(void);
void	 efi_hardprobe(void);
void	 efi_diskprobe(void);
void	 efi_pxeprobe(void);
void	 efi_cons_init(struct consdev *);
int	 efi_cons_getc(dev_t);
void	 efi_cons_putc(dev_t, int);
int	 efi_cons_getshifts(dev_t dev);
void	 efi_com_probe(struct consdev *);
void	 efi_com_init(struct consdev *);
int	 efi_com_getc(dev_t);
void	 efi_com_putc(dev_t, int);
int	 Xvideo_efi(void);
int	 Xgop_efi(void);
int	 Xexit_efi(void);
void	 efi_makebootargs(void);
void	 efi_setconsdev(void);

int	 Xpoweroff_efi(void);
#ifdef IDLE_POWEROFF
int	 Xidle_efi(void);
#endif
int	 Xfwsetup_efi(void);

extern void (*run_i386)(u_long, u_long, int, int, int, int, int, int, int, int)
    __attribute__ ((noreturn));
