/*	$OpenBSD: efirng.c,v 1.2 2021/06/25 17:49:49 krw Exp $	*/

/*
 * Copyright (c) 2018 Mark Kettenis <kettenis@openbsd.org>
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

extern EFI_BOOT_SERVICES	*BS;

/* Random Number Generator Protocol */

#define EFI_RNG_PROTOCOL_GUID \
    { 0x3152bca5, 0xeade, 0x433d, {0x86, 0x2e, 0xc0, 0x1c, 0xdc, 0x29, 0x1f, 0x44} }

INTERFACE_DECL(_EFI_RNG_PROTOCOL);

typedef EFI_GUID EFI_RNG_ALGORITHM;

typedef
EFI_STATUS
(EFIAPI *EFI_RNG_GET_INFO) (
    IN struct _EFI_RNG_PROTOCOL		*This,
    IN  OUT UINTN			*RNGAlgorithmListSize,
    OUT EFI_RNG_ALGORITHM		*RNGAlgorithmList
    );

typedef
EFI_STATUS
(EFIAPI *EFI_RNG_GET_RNG) (
    IN struct _EFI_RNG_PROTOCOL		*This,
    IN EFI_RNG_ALGORITHM		*RNGAlgorithm, OPTIONAL
    IN UINTN				RNGValueLength,
    OUT UINT8				*RNGValue
    );

typedef struct _EFI_RNG_PROTOCOL {
	EFI_RNG_GET_INFO	GetInfo;
	EFI_RNG_GET_RNG		GetRNG;
} EFI_RNG_PROTOCOL;

static EFI_GUID			rng_guid = EFI_RNG_PROTOCOL_GUID;

int
fwrandom(char *buf, size_t buflen)
{
	EFI_STATUS		 status;
	EFI_RNG_PROTOCOL	*rng = NULL;
	UINT8			*random;
	size_t			 i;
	int			 ret = 0;

	status = BS->LocateProtocol(&rng_guid, NULL, (void **)&rng);
	if (rng == NULL || EFI_ERROR(status))
		return -1;

	random = alloc(buflen);

	status = rng->GetRNG(rng, NULL, buflen, random);
	if (EFI_ERROR(status)) {
		printf("RNG GetRNG() failed (%d)\n", status);
		ret = -1;
		goto out;
	}

	for (i = 0; i < buflen; i++)
		buf[i] ^= random[i];

out:
	free(random, buflen);
	return ret;
}
