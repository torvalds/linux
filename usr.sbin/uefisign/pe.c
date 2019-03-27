/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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
 */

/*
 * PE format reference:
 * http://www.microsoft.com/whdc/system/platform/firmware/PECOFF.mspx
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "uefisign.h"

#ifndef CTASSERT
#define CTASSERT(x)		_CTASSERT(x, __LINE__)
#define _CTASSERT(x, y)		__CTASSERT(x, y)
#define __CTASSERT(x, y)	typedef char __assert_ ## y [(x) ? 1 : -1]
#endif

#define PE_ALIGMENT_SIZE	8

struct mz_header {
	uint8_t			mz_signature[2];
	uint8_t			mz_dont_care[58];
	uint16_t		mz_lfanew;
} __attribute__((packed));

struct coff_header {
	uint8_t			coff_dont_care[2];
	uint16_t		coff_number_of_sections;
	uint8_t			coff_dont_care_either[16];
} __attribute__((packed));

#define	PE_SIGNATURE		0x00004550

struct pe_header {
	uint32_t		pe_signature;
	struct coff_header	pe_coff;
} __attribute__((packed));

#define	PE_OPTIONAL_MAGIC_32		0x010B
#define	PE_OPTIONAL_MAGIC_32_PLUS	0x020B

#define	PE_OPTIONAL_SUBSYSTEM_EFI_APPLICATION	10
#define	PE_OPTIONAL_SUBSYSTEM_EFI_BOOT		11
#define	PE_OPTIONAL_SUBSYSTEM_EFI_RUNTIME	12

struct pe_optional_header_32 {
	uint16_t		po_magic;
	uint8_t			po_dont_care[58];
	uint32_t		po_size_of_headers;
	uint32_t		po_checksum;
	uint16_t		po_subsystem;
	uint8_t			po_dont_care_either[22];
	uint32_t		po_number_of_rva_and_sizes;
} __attribute__((packed));

CTASSERT(offsetof(struct pe_optional_header_32, po_size_of_headers) == 60);
CTASSERT(offsetof(struct pe_optional_header_32, po_checksum) == 64);
CTASSERT(offsetof(struct pe_optional_header_32, po_subsystem) == 68);
CTASSERT(offsetof(struct pe_optional_header_32, po_number_of_rva_and_sizes) == 92);

struct pe_optional_header_32_plus {
	uint16_t		po_magic;
	uint8_t			po_dont_care[58];
	uint32_t		po_size_of_headers;
	uint32_t		po_checksum;
	uint16_t		po_subsystem;
	uint8_t			po_dont_care_either[38];
	uint32_t		po_number_of_rva_and_sizes;
} __attribute__((packed));

CTASSERT(offsetof(struct pe_optional_header_32_plus, po_size_of_headers) == 60);
CTASSERT(offsetof(struct pe_optional_header_32_plus, po_checksum) == 64);
CTASSERT(offsetof(struct pe_optional_header_32_plus, po_subsystem) == 68);
CTASSERT(offsetof(struct pe_optional_header_32_plus, po_number_of_rva_and_sizes) == 108);

#define	PE_DIRECTORY_ENTRY_CERTIFICATE	4

struct pe_directory_entry {
	uint32_t	pde_rva;
	uint32_t	pde_size;
} __attribute__((packed));

struct pe_section_header {
	uint8_t			psh_dont_care[16];
	uint32_t		psh_size_of_raw_data;
	uint32_t		psh_pointer_to_raw_data;
	uint8_t			psh_dont_care_either[16];
} __attribute__((packed));

CTASSERT(offsetof(struct pe_section_header, psh_size_of_raw_data) == 16);
CTASSERT(offsetof(struct pe_section_header, psh_pointer_to_raw_data) == 20);

#define	PE_CERTIFICATE_REVISION		0x0200
#define	PE_CERTIFICATE_TYPE		0x0002

struct pe_certificate {
	uint32_t	pc_len;
	uint16_t	pc_revision;
	uint16_t	pc_type;
	char		pc_signature[0];
} __attribute__((packed));

void
range_check(const struct executable *x, off_t off, size_t len,
    const char *name)
{

	if (off < 0) {
		errx(1, "%s starts at negative offset %jd",
		    name, (intmax_t)off);
	}
	if (off >= (off_t)x->x_len) {
		errx(1, "%s starts at %jd, past the end of executable at %zd",
		    name, (intmax_t)off, x->x_len);
	}
	if (len >= x->x_len) {
		errx(1, "%s size %zd is larger than the executable size %zd",
		    name, len, x->x_len);
	}
	if (off + len > x->x_len) {
		errx(1, "%s extends to %jd, past the end of executable at %zd",
		    name, (intmax_t)(off + len), x->x_len);
	}
}

size_t
signature_size(const struct executable *x)
{
	const struct pe_directory_entry *pde;

	range_check(x, x->x_certificate_entry_off,
	    x->x_certificate_entry_len, "Certificate Directory");

	pde = (struct pe_directory_entry *)
	    (x->x_buf + x->x_certificate_entry_off);

	if (pde->pde_rva != 0 && pde->pde_size == 0)
		warnx("signature size is 0, but its RVA is %d", pde->pde_rva);
	if (pde->pde_rva == 0 && pde->pde_size != 0)
		warnx("signature RVA is 0, but its size is %d", pde->pde_size);

	return (pde->pde_size);
}

void
show_certificate(const struct executable *x)
{
	struct pe_certificate *pc;
	const struct pe_directory_entry *pde;

	range_check(x, x->x_certificate_entry_off,
	    x->x_certificate_entry_len, "Certificate Directory");

	pde = (struct pe_directory_entry *)
	    (x->x_buf + x->x_certificate_entry_off);

	if (signature_size(x) == 0) {
		printf("file not signed\n");
		return;
	}

#if 0
	printf("certificate chunk at offset %zd, size %zd\n",
	    pde->pde_rva, pde->pde_size);
#endif

	range_check(x, pde->pde_rva, pde->pde_size, "Certificate chunk");

	pc = (struct pe_certificate *)(x->x_buf + pde->pde_rva);
	if (pc->pc_revision != PE_CERTIFICATE_REVISION) {
		errx(1, "wrong certificate chunk revision, is %d, should be %d",
		    pc->pc_revision, PE_CERTIFICATE_REVISION);
	}
	if (pc->pc_type != PE_CERTIFICATE_TYPE) {
		errx(1, "wrong certificate chunk type, is %d, should be %d",
		    pc->pc_type, PE_CERTIFICATE_TYPE);
	}
	printf("to dump PKCS7:\n    "
	    "dd if='%s' bs=1 skip=%zd | openssl pkcs7 -inform DER -print\n",
	    x->x_path, pde->pde_rva + offsetof(struct pe_certificate, pc_signature));
	printf("to dump raw ASN.1:\n    "
	    "openssl asn1parse -i -inform DER -offset %zd -in '%s'\n",
	    pde->pde_rva + offsetof(struct pe_certificate, pc_signature), x->x_path);
}

static void
parse_section_table(struct executable *x, off_t off, int number_of_sections)
{
	const struct pe_section_header *psh;
	int i;

	range_check(x, off, sizeof(*psh) * number_of_sections,
	    "section table");

	if (x->x_headers_len <= off + sizeof(*psh) * number_of_sections)
		errx(1, "section table outside of headers");

	psh = (const struct pe_section_header *)(x->x_buf + off);

	if (number_of_sections >= MAX_SECTIONS) {
		errx(1, "too many sections: got %d, should be %d",
		    number_of_sections, MAX_SECTIONS);
	}
	x->x_nsections = number_of_sections;

	for (i = 0; i < number_of_sections; i++) {
		if (psh->psh_pointer_to_raw_data < x->x_headers_len)
			errx(1, "section points inside the headers");

		range_check(x, psh->psh_pointer_to_raw_data,
		    psh->psh_size_of_raw_data, "section");
#if 0
		printf("section %d: start %d, size %d\n",
		    i, psh->psh_pointer_to_raw_data, psh->psh_size_of_raw_data);
#endif
		x->x_section_off[i] = psh->psh_pointer_to_raw_data;
		x->x_section_len[i] = psh->psh_size_of_raw_data;
		psh++;
	}
}

static void
parse_directory(struct executable *x, off_t off,
    int number_of_rva_and_sizes, int number_of_sections)
{
	//int i;
	const struct pe_directory_entry *pde;

	//printf("Data Directory at offset %zd\n", off);

	if (number_of_rva_and_sizes <= PE_DIRECTORY_ENTRY_CERTIFICATE) {
		errx(1, "wrong NumberOfRvaAndSizes %d; should be at least %d",
		    number_of_rva_and_sizes, PE_DIRECTORY_ENTRY_CERTIFICATE);
	}

	range_check(x, off, sizeof(*pde) * number_of_rva_and_sizes,
	    "PE Data Directory");
	if (x->x_headers_len <= off + sizeof(*pde) * number_of_rva_and_sizes)
		errx(1, "PE Data Directory outside of headers");

	x->x_certificate_entry_off =
	    off + sizeof(*pde) * PE_DIRECTORY_ENTRY_CERTIFICATE;
	x->x_certificate_entry_len = sizeof(*pde);
#if 0
	printf("certificate directory entry at offset %zd, len %zd\n",
	    x->x_certificate_entry_off, x->x_certificate_entry_len);

	pde = (struct pe_directory_entry *)(x->x_buf + off);
	for (i = 0; i < number_of_rva_and_sizes; i++) {
		printf("rva %zd, size %zd\n", pde->pde_rva, pde->pde_size);
		pde++;
	}
#endif

	return (parse_section_table(x,
	    off + sizeof(*pde) * number_of_rva_and_sizes, number_of_sections));
}

/*
 * The PE checksum algorithm is undocumented; this code is mostly based on
 * http://forum.sysinternals.com/optional-header-checksum-calculation_topic24214.html
 *
 * "Sum the entire image file, excluding the CheckSum field in the optional
 * header, as an array of USHORTs, allowing any carry above 16 bits to be added
 * back onto the low 16 bits. Then add the file size to get a 32-bit value."
 *
 * Note that most software does not care about the checksum at all; perhaps
 * we could just set it to 0 instead.
 *
 * XXX: Endianness?
 */
static uint32_t
compute_checksum(const struct executable *x)
{
	uint32_t cksum = 0;
	uint16_t tmp;
	int i;

	range_check(x, x->x_checksum_off, x->x_checksum_len, "PE checksum");

	assert(x->x_checksum_off % 2 == 0);

	for (i = 0; i + sizeof(tmp) < x->x_len; i += 2) {
		/*
		 * Don't checksum the checksum.  The +2 is because the checksum
		 * is 4 bytes, and here we're iterating over 2 byte chunks.
		 */
		if (i == x->x_checksum_off || i == x->x_checksum_off + 2) {
			tmp = 0;
		} else {
			assert(i + sizeof(tmp) <= x->x_len);
			memcpy(&tmp, x->x_buf + i, sizeof(tmp));
		}

		cksum += tmp;
		cksum += cksum >> 16;
		cksum &= 0xffff;
	}

	cksum += cksum >> 16;
	cksum &= 0xffff;

	cksum += x->x_len;

	return (cksum);
}

static void
parse_optional_32_plus(struct executable *x, off_t off,
    int number_of_sections)
{
#if 0
	uint32_t computed_checksum;
#endif
	const struct pe_optional_header_32_plus	*po;

	range_check(x, off, sizeof(*po), "PE Optional Header");

	po = (struct pe_optional_header_32_plus *)(x->x_buf + off);
	switch (po->po_subsystem) {
	case PE_OPTIONAL_SUBSYSTEM_EFI_APPLICATION:
	case PE_OPTIONAL_SUBSYSTEM_EFI_BOOT:
	case PE_OPTIONAL_SUBSYSTEM_EFI_RUNTIME:
		break;
	default:
		errx(1, "wrong PE Optional Header subsystem 0x%x",
		    po->po_subsystem);
	}

#if 0
	printf("subsystem %d, checksum 0x%x, %d data directories\n",
	    po->po_subsystem, po->po_checksum, po->po_number_of_rva_and_sizes);
#endif

	x->x_checksum_off = off +
	    offsetof(struct pe_optional_header_32_plus, po_checksum);
	x->x_checksum_len = sizeof(po->po_checksum);
#if 0
	printf("checksum 0x%x at offset %zd, len %zd\n",
	    po->po_checksum, x->x_checksum_off, x->x_checksum_len);

	computed_checksum = compute_checksum(x);
	if (computed_checksum != po->po_checksum) {
		warnx("invalid PE+ checksum; is 0x%x, should be 0x%x",
		    po->po_checksum, computed_checksum);
	}
#endif

	if (x->x_len < x->x_headers_len)
		errx(1, "invalid SizeOfHeaders %d", po->po_size_of_headers);
	x->x_headers_len = po->po_size_of_headers;
	//printf("Size of Headers: %d\n", po->po_size_of_headers);

	return (parse_directory(x, off + sizeof(*po),
	    po->po_number_of_rva_and_sizes, number_of_sections));
}

static void
parse_optional_32(struct executable *x, off_t off, int number_of_sections)
{
#if 0
	uint32_t computed_checksum;
#endif
	const struct pe_optional_header_32 *po;

	range_check(x, off, sizeof(*po), "PE Optional Header");

	po = (struct pe_optional_header_32 *)(x->x_buf + off);
	switch (po->po_subsystem) {
	case PE_OPTIONAL_SUBSYSTEM_EFI_APPLICATION:
	case PE_OPTIONAL_SUBSYSTEM_EFI_BOOT:
	case PE_OPTIONAL_SUBSYSTEM_EFI_RUNTIME:
		break;
	default:
		errx(1, "wrong PE Optional Header subsystem 0x%x",
		    po->po_subsystem);
	}

#if 0
	printf("subsystem %d, checksum 0x%x, %d data directories\n",
	    po->po_subsystem, po->po_checksum, po->po_number_of_rva_and_sizes);
#endif

	x->x_checksum_off = off +
	    offsetof(struct pe_optional_header_32, po_checksum);
	x->x_checksum_len = sizeof(po->po_checksum);
#if 0
	printf("checksum at offset %zd, len %zd\n",
	    x->x_checksum_off, x->x_checksum_len);

	computed_checksum = compute_checksum(x);
	if (computed_checksum != po->po_checksum) {
		warnx("invalid PE checksum; is 0x%x, should be 0x%x",
		    po->po_checksum, computed_checksum);
	}
#endif

	if (x->x_len < x->x_headers_len)
		errx(1, "invalid SizeOfHeaders %d", po->po_size_of_headers);
	x->x_headers_len = po->po_size_of_headers;
	//printf("Size of Headers: %d\n", po->po_size_of_headers);

	return (parse_directory(x, off + sizeof(*po),
	    po->po_number_of_rva_and_sizes, number_of_sections));
}

static void
parse_optional(struct executable *x, off_t off, int number_of_sections)
{
	const struct pe_optional_header_32 *po;

	//printf("Optional header offset %zd\n", off);

	range_check(x, off, sizeof(*po), "PE Optional Header");

	po = (struct pe_optional_header_32 *)(x->x_buf + off);

	switch (po->po_magic) {
	case PE_OPTIONAL_MAGIC_32:
		return (parse_optional_32(x, off, number_of_sections));
	case PE_OPTIONAL_MAGIC_32_PLUS:
		return (parse_optional_32_plus(x, off, number_of_sections));
	default:
		errx(1, "wrong PE Optional Header magic 0x%x", po->po_magic);
	}
}

static void
parse_pe(struct executable *x, off_t off)
{
	const struct pe_header *pe;

	//printf("PE offset %zd, PE size %zd\n", off, sizeof(*pe));

	range_check(x, off, sizeof(*pe), "PE header");

	pe = (struct pe_header *)(x->x_buf + off);
	if (pe->pe_signature != PE_SIGNATURE)
		errx(1, "wrong PE signature 0x%x", pe->pe_signature);

	//printf("Number of sections: %d\n", pe->pe_coff.coff_number_of_sections);

	parse_optional(x, off + sizeof(*pe),
	    pe->pe_coff.coff_number_of_sections);
}

void
parse(struct executable *x)
{
	const struct mz_header *mz;

	range_check(x, 0, sizeof(*mz), "MZ header");

	mz = (struct mz_header *)x->x_buf;
	if (mz->mz_signature[0] != 'M' || mz->mz_signature[1] != 'Z')
		errx(1, "MZ header not found");

	return (parse_pe(x, mz->mz_lfanew));
}

static off_t
append(struct executable *x, void *ptr, size_t len, size_t aligment)
{
	off_t off;

	off = x->x_len;
	x->x_buf = realloc(x->x_buf, x->x_len + len + aligment);
	if (x->x_buf == NULL)
		err(1, "realloc");
	memcpy(x->x_buf + x->x_len, ptr, len);
	memset(x->x_buf + x->x_len + len, 0, aligment);
	x->x_len += len + aligment;

	return (off);
}

void
update(struct executable *x)
{
	uint32_t checksum;
	struct pe_certificate *pc;
	struct pe_directory_entry pde;
	size_t pc_len;
	size_t pc_aligment;
	off_t pc_off;

	pc_len = sizeof(*pc) + x->x_signature_len;
	pc = calloc(1, pc_len);
	if (pc == NULL)
		err(1, "calloc");

	if (pc_len % PE_ALIGMENT_SIZE > 0)
		pc_aligment = PE_ALIGMENT_SIZE - (pc_len % PE_ALIGMENT_SIZE);
	else
		pc_aligment = 0;

#if 0
	/*
	 * Note that pc_len is the length of pc_certificate,
	 * not the whole structure.
	 *
	 * XXX: That's what the spec says - but it breaks at least
	 *      sbverify and "pesign -S", so the spec is probably wrong.
	 */
	pc->pc_len = x->x_signature_len;
#else
	pc->pc_len = pc_len;
#endif
	pc->pc_revision = PE_CERTIFICATE_REVISION;
	pc->pc_type = PE_CERTIFICATE_TYPE;
	memcpy(&pc->pc_signature, x->x_signature, x->x_signature_len);

	pc_off = append(x, pc, pc_len, pc_aligment);
#if 0
	printf("added signature chunk at offset %zd, len %zd\n",
	    pc_off, pc_len);
#endif

	free(pc);

	pde.pde_rva = pc_off;
	pde.pde_size = pc_len + pc_aligment;
	memcpy(x->x_buf + x->x_certificate_entry_off, &pde, sizeof(pde));

	checksum = compute_checksum(x);
	assert(sizeof(checksum) == x->x_checksum_len);
	memcpy(x->x_buf + x->x_checksum_off, &checksum, sizeof(checksum));
#if 0
	printf("new checksum 0x%x\n", checksum);
#endif
}
