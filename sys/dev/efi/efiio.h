/*	$OpenBSD: efiio.h,v 1.2 2024/10/22 21:50:02 jsg Exp $	*/
/*-
 * Copyright (c) 2016 Netflix, Inc.
 * Copyright (c) 2022 3mdeb <contact@3mdeb.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_EFI_EFIIO_H_
#define _DEV_EFI_EFIIO_H_

#include <sys/types.h>
#include <sys/ioccom.h>
#include <sys/uuid.h>

typedef uint16_t efi_char;

#define EFI_TABLE_ESRT					\
	{0xb122a263,0x3661,0x4f68,0x99,0x29,{0x78,0xf8,0xb0,0xd6,0x21,0x80}}

struct efi_esrt_table {
	uint32_t	fw_resource_count;
	uint32_t	fw_resource_count_max;
	uint64_t	fw_resource_version;
#define ESRT_FIRMWARE_RESOURCE_VERSION 1
	uint8_t		entries[];
};

struct efi_esrt_entry_v1 {
	struct uuid	fw_class;
	uint32_t 	fw_type;
	uint32_t	fw_version;
	uint32_t	lowest_supported_fw_version;
	uint32_t	capsule_flags;
	uint32_t	last_attempt_version;
	uint32_t	last_attempt_status;
};

struct efi_get_table_ioc {
	void *buf;		/* Pointer to userspace buffer */
	struct uuid uuid;	/* UUID to look up */
	size_t table_len;	/* Table size */
	size_t buf_len;		/* Size of the buffer */
};

struct efi_var_ioc {
	uint16_t *name;		/* User pointer to name, in UCS2 chars */
	size_t namesize;	/* Number of *bytes* in the name including
				   terminator */
	struct uuid vendor;	/* Vendor's UUID for variable */
	uint32_t attrib;	/* Attributes */
	void *data;		/* User pointer to value */
	size_t datasize;	/* Number of *bytes* in the value */
};

#define EFIIOC_GET_TABLE	_IOWR('E',  1, struct efi_get_table_ioc)
#define EFIIOC_VAR_GET		_IOWR('E',  2, struct efi_var_ioc)
#define EFIIOC_VAR_NEXT		_IOWR('E',  3, struct efi_var_ioc)
#define EFIIOC_VAR_SET		_IOWR('E',  4, struct efi_var_ioc)

#endif /* _DEV_EFI_EFIIO_H_ */
