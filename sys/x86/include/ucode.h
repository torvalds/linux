/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 The FreeBSD Foundation
 *
 * This software was developed by Mark Johnston under sponsorship from
 * the FreeBSD Foundation.
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

#ifndef _MACHINE_UCODE_H_
#define	_MACHINE_UCODE_H_

struct ucode_intel_header {
	uint32_t	header_version;
	int32_t		update_revision;
	uint32_t	dat;
	uint32_t	processor_signature;
	uint32_t	checksum;
	uint32_t	loader_revision;
	uint32_t	processor_flags;
#define	UCODE_INTEL_DEFAULT_DATA_SIZE		2000
	uint32_t	data_size;
	uint32_t	total_size;
	uint32_t	reserved[3];
};

struct ucode_intel_extsig_table {
	uint32_t	signature_count;
	uint32_t	signature_table_checksum;
	uint32_t	reserved[3];
	struct ucode_intel_extsig {
		uint32_t	processor_signature;
		uint32_t	processor_flags;
		uint32_t	checksum;
	} entries[0];
};

int	ucode_intel_load(void *data, bool unsafe,
	    uint64_t *nrevp, uint64_t *orevp);
size_t	ucode_load_bsp(uintptr_t free);
void	ucode_load_ap(int cpu);
void	ucode_reload(void);
void *	ucode_update(void *data);

#endif /* _MACHINE_UCODE_H_ */
