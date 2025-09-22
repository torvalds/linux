/*	$OpenBSD: efivar.h,v 1.2 2024/07/10 10:53:55 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
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

#ifndef _MACHINE_EFIVAR_H_
#define _MACHINE_EFIVAR_H_

#include <dev/clock_subr.h>

struct efi_softc {
	struct device	sc_dev;
	struct pmap	*sc_pm;
	EFI_RUNTIME_SERVICES *sc_rs;
	EFI_SYSTEM_RESOURCE_TABLE *sc_esrt;
	u_long		sc_psw;

	struct todr_chip_handle sc_todr;
};


extern EFI_GET_VARIABLE efi_get_variable;
extern EFI_SET_VARIABLE efi_set_variable;
extern EFI_GET_NEXT_VARIABLE_NAME efi_get_next_variable_name;

void	efi_enter(struct efi_softc *);
void	efi_leave(struct efi_softc *);

extern label_t efi_jmpbuf;

#define efi_enter_check(sc) (setjmp(&efi_jmpbuf) ? \
    (efi_leave(sc), EFAULT) : (efi_enter(sc), 0))

#endif /* _MACHINE_EFIVAR_H_ */
