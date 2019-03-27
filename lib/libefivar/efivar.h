/*-
 * Copyright (c) 2016 Netflix, Inc.
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
 *
 * $FreeBSD$
 */

#ifndef	_EFIVAR_H_
#define	_EFIVAR_H_

#include <uuid.h>
#include <sys/efi.h>
#include <sys/endian.h>
#include <stdint.h>

/* Shoud these be elsewhere ? */
#define	EFI_VARIABLE_NON_VOLATILE		0x00000001
#define	EFI_VARIABLE_BOOTSERVICE_ACCESS		0x00000002
#define	EFI_VARIABLE_RUNTIME_ACCESS		0x00000004
#define	EFI_VARIABLE_HARDWARE_ERROR_RECORD	0x00000008
#define	EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS	0x00000010
#define	EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS \
						0x00000020
#define	EFI_VARIABLE_APPEND_WRITE		0x00000040
#if 0 /* todo */
#define	EFI_VARIABLE_HAS_AUTH_HEADER
#define EFI_VARIABLE_HAS_SIGNATURE
#endif


#ifndef _EFIVAR_EFI_GUID_T_DEF
#define _EFIVAR_EFI_GUID_T_DEF
typedef uuid_t efi_guid_t;
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
#define	EFI_GUID(a, b, c, d, e0, e1, e2, e3, e4, e5)			\
	((efi_guid_t) {(a), (b), (c), (d) >> 8, (d) & 0xff,		\
	{ (e0), (e1), (e2), (e3), (e4), (e5) }})
#else
#define	EFI_GUID(a, b, c, d, e0, e1, e2, e3, e4, e5)			\
	((efi_guid_t) {(a), (b), (c), (d) & 0xff, (d) >> 8,		\
	{ (e0), (e1), (e2), (e3), (e4), (e5) }})
#endif

#define EFI_GLOBAL_GUID EFI_GUID(0x8be4df61, 0x93ca, 0x11d2, 0xaa0d, \
    0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c)

int efi_append_variable(efi_guid_t guid, const char *name,
    uint8_t *data, size_t data_size, uint32_t attributes);
int efi_del_variable(efi_guid_t guid, const char *name);
int efi_get_variable(efi_guid_t guid, const char *name,
    uint8_t **data, size_t *data_size, uint32_t *attributes);
int efi_get_variable_attributes(efi_guid_t guid, const char *name,
    uint32_t *attributes);
int efi_get_variable_size(efi_guid_t guid, const char *name, size_t *size);
int efi_get_next_variable_name(efi_guid_t **guid, char **name);
int efi_guid_cmp(const efi_guid_t *guid1, const efi_guid_t *guid2);
int efi_guid_is_zero(const efi_guid_t *guid1);
int efi_guid_to_name(efi_guid_t *guid, char **name);
int efi_guid_to_symbol(efi_guid_t *guid, char **symbol);
int efi_guid_to_str(const efi_guid_t *guid, char **sp);
int efi_name_to_guid(const char *name, efi_guid_t *guid);
int efi_set_variable(efi_guid_t guid, const char *name,
    uint8_t *data, size_t data_size, uint32_t attributes);
int efi_str_to_guid(const char *s, efi_guid_t *guid);
int efi_variables_supported(void);

/* FreeBSD extensions */
struct uuid_table
{
	const char *uuid_str;
	const char *name;
	efi_guid_t guid;
};

int efi_known_guid(struct uuid_table **);

extern const efi_guid_t efi_guid_empty;

#endif /* _EFIVAR_H_ */
