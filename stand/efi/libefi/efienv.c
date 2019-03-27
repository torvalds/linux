/*-
 * Copyright (c) 2018 Netflix, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include <efi.h>
#include <efichar.h>
#include <efilib.h>

static EFI_GUID FreeBSDBootVarGUID = FREEBSD_BOOT_VAR_GUID;
static EFI_GUID GlobalBootVarGUID = EFI_GLOBAL_VARIABLE;

EFI_STATUS
efi_getenv(EFI_GUID *g, const char *v, void *data, size_t *len)
{
	size_t ul;
	CHAR16 *uv;
	UINT32 attr;
	UINTN dl;
	EFI_STATUS rv;

	uv = NULL;
	if (utf8_to_ucs2(v, &uv, &ul) != 0)
		return (EFI_OUT_OF_RESOURCES);
	dl = *len;
	rv = RS->GetVariable(uv, g, &attr, &dl, data);
	if (rv == EFI_SUCCESS || rv == EFI_BUFFER_TOO_SMALL)
		*len = dl;
	free(uv);
	return (rv);
}

EFI_STATUS
efi_global_getenv(const char *v, void *data, size_t *len)
{

	return (efi_getenv(&GlobalBootVarGUID, v, data, len));
}

EFI_STATUS
efi_freebsd_getenv(const char *v, void *data, size_t *len)
{

	return (efi_getenv(&FreeBSDBootVarGUID, v, data, len));
}

EFI_STATUS
efi_setenv_freebsd_wcs(const char *varname, CHAR16 *valstr)
{
	CHAR16 *var = NULL;
	size_t len;
	EFI_STATUS rv;

	if (utf8_to_ucs2(varname, &var, &len) != 0)
		return (EFI_OUT_OF_RESOURCES);
	rv = RS->SetVariable(var, &FreeBSDBootVarGUID,
	    EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
	    (ucs2len(valstr) + 1) * sizeof(efi_char), valstr);
	free(var);
	return (rv);
}

