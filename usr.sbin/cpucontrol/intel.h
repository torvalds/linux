/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006, 2008 Stanislav Sedov <stas@FreeBSD.org>.
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
 *
 * $FreeBSD$
 */

#ifndef INTEL_H
#define	INTEL_H

/*
 * Prototypes.
 */
ucode_probe_t	intel_probe;
ucode_update_t	intel_update;

typedef struct intel_fw_header {
	uint32_t	header_version;		/* Version of the header. */
	int32_t		revision;		/* Unique version number. */
	uint32_t	date;			/* Date of creation in BCD. */
	uint32_t	cpu_signature;		/* Extended family, extended
						   model, type, family, model
						   and stepping. */
	uint32_t	checksum;		/* Sum of all DWORDS should
						   be 0. */
	uint32_t	loader_revision;	/* Version of the loader
						   required to load update. */
	uint32_t	cpu_flags;		/* Platform IDs encoded in
						   the lower 8 bits. */
	uint32_t	data_size;
	uint32_t	total_size;
	uint8_t		reserved[12];
} intel_fw_header_t;

typedef struct intel_cpu_signature {
	uint32_t	cpu_signature;
	uint32_t	cpu_flags;
	uint32_t	checksum;
} intel_cpu_signature_t;

typedef struct intel_ext_header {
	uint32_t	sig_count;
	uint32_t	checksum;
	uint8_t		reserved[12];
} intel_ext_header_t;

#define	INTEL_HEADER_VERSION	0x00000001
#define	INTEL_LOADER_REVISION	0x00000001

#endif /* !INTEL_H */
