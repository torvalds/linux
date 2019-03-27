/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Michael Smith
 * Copyright (c) 1998 Jonathan Lemon
 * All rights reserved.
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

#ifndef _MACHINE_PC_BIOS_H_
#define _MACHINE_PC_BIOS_H_

/*
 * Int 15:E820 'SMAP' structure
 */
#define SMAP_SIG	0x534D4150			/* 'SMAP' */

#define	SMAP_TYPE_MEMORY	1
#define	SMAP_TYPE_RESERVED	2
#define	SMAP_TYPE_ACPI_RECLAIM	3
#define	SMAP_TYPE_ACPI_NVS	4
#define	SMAP_TYPE_ACPI_ERROR	5
#define	SMAP_TYPE_DISABLED	6
#define	SMAP_TYPE_PMEM		7
#define	SMAP_TYPE_PRAM		12

#define	SMAP_XATTR_ENABLED	0x00000001
#define	SMAP_XATTR_NON_VOLATILE	0x00000002
#define	SMAP_XATTR_MASK		(SMAP_XATTR_ENABLED | SMAP_XATTR_NON_VOLATILE)

struct bios_smap {
    u_int64_t	base;
    u_int64_t	length;
    u_int32_t	type;
} __packed;

/* Structure extended to include extended attribute field in ACPI 3.0. */
struct bios_smap_xattr {
    u_int64_t	base;
    u_int64_t	length;
    u_int32_t	type;
    u_int32_t	xattr;
} __packed;
	
/*
 * System Management BIOS
 */
#define	SMBIOS_START	0xf0000
#define	SMBIOS_STEP	0x10
#define	SMBIOS_OFF	0
#define	SMBIOS_LEN	4
#define	SMBIOS_SIG	"_SM_"

struct smbios_eps {
	uint8_t		anchor_string[4];		/* '_SM_' */
	uint8_t		checksum;
	uint8_t		length;
	uint8_t		major_version;
	uint8_t		minor_version;
	uint16_t	maximum_structure_size;
	uint8_t		entry_point_revision;
	uint8_t		formatted_area[5];
	uint8_t		intermediate_anchor_string[5];	/* '_DMI_' */
	uint8_t		intermediate_checksum;
	uint16_t	structure_table_length;
	uint32_t	structure_table_address;
	uint16_t	number_structures;
	uint8_t		BCD_revision;
};

struct smbios_structure_header {
	uint8_t		type;
	uint8_t		length;
	uint16_t	handle;
};

#ifdef _KERNEL
#define BIOS_PADDRTOVADDR(x)	((x) + KERNBASE)
#define BIOS_VADDRTOPADDR(x)	((x) - KERNBASE)

struct bios_oem_signature {
	char * anchor;		/* search anchor string in BIOS memory */
	size_t offset;		/* offset from anchor (may be negative) */
	size_t totlen;		/* total length of BIOS string to copy */
} __packed;

struct bios_oem_range {
	u_int from;		/* shouldn't be below 0xe0000 */
	u_int to;		/* shouldn't be above 0xfffff */
} __packed;

struct bios_oem {
	struct bios_oem_range range;
	struct bios_oem_signature signature[];
} __packed;

int	bios_oem_strings(struct bios_oem *oem, u_char *buffer, size_t maxlen);
uint32_t bios_sigsearch(uint32_t start, u_char *sig, int siglen, int paralen,
	    int sigofs);
void bios_add_smap_entries(struct bios_smap *smapbase, u_int32_t smapsize,
	    vm_paddr_t *physmap, int *physmap_idx);
#endif

#endif /* _MACHINE_PC_BIOS_H_ */
