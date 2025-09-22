/*	$OpenBSD: efiriscv.c,v 1.1 2024/03/26 22:26:04 kettenis Exp $	*/

/*
 * Copyright (c) 2024 Mark Kettenis <kettenis@openbsd.org>
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

#include <sys/param.h>

#include <efi.h>
#include <efiapi.h>

#include "libsa.h"

#include "efiboot.h"

extern EFI_BOOT_SERVICES	*BS;

/* RISC-V UEFI Boot Protocol */

#define RISCV_EFI_BOOT_PROTOCOL_GUID  \
    { 0xccd15fec, 0x6f73, 0x4eec, { 0x83, 0x95, 0x3e, 0x69, 0xe4, 0xb9, 0x40, 0xbf } }

INTERFACE_DECL(_RISCV_EFI_BOOT_PROTOCOL);

typedef
EFI_STATUS
(EFIAPI *EFI_GET_BOOT_HARTID) (
    IN struct _RISCV_EFI_BOOT_PROTOCOL *This,
    OUT UINTN *BootHartId
    );

typedef struct _RISCV_EFI_BOOT_PROTOCOL {
	UINT64			Revision;
	EFI_GET_BOOT_HARTID	GetBootHartId;
} RISCV_EFI_BOOT_PROTOCOL;

static EFI_GUID			riscv_guid = RISCV_EFI_BOOT_PROTOCOL_GUID;

int32_t
efi_get_boot_hart_id(void)
{
	EFI_STATUS		 status;
	RISCV_EFI_BOOT_PROTOCOL	*riscv = NULL;
	UINTN			 hartid;

	status = BS->LocateProtocol(&riscv_guid, NULL, (void **)&riscv);
	if (riscv == NULL || EFI_ERROR(status))
		return -1;

	status = riscv->GetBootHartId(riscv, &hartid);
	if (status == EFI_SUCCESS)
		return hartid;

	return -1;
}
