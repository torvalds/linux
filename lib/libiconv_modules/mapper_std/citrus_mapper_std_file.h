/* $FreeBSD$ */
/*	$NetBSD: citrus_mapper_std_file.h,v 1.3 2006/09/09 14:35:17 tnozaki Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)2003, 2006 Citrus Project,
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
 */

#ifndef _CITRUS_MAPPER_STD_FILE_H_
#define _CITRUS_MAPPER_STD_FILE_H_

#define _CITRUS_MAPPER_STD_MAGIC		"MAPPER\0\0"

#define _CITRUS_MAPPER_STD_SYM_TYPE		"type"
#define _CITRUS_MAPPER_STD_SYM_INFO		"info"
#define _CITRUS_MAPPER_STD_SYM_TABLE		"table"

#define _CITRUS_MAPPER_STD_TYPE_ROWCOL		"rowcol"
struct _citrus_mapper_std_rowcol_info_x {
	uint32_t		rcx_src_rowcol_bits;
	uint32_t		rcx_dst_invalid;
#define _CITRUS_MAPPER_STD_ROWCOL_MAX			4
	struct {
		uint32_t		begin;
		uint32_t		end;
	} __packed		rcx_src_rowcol[_CITRUS_MAPPER_STD_ROWCOL_MAX];
	uint32_t		rcx_dst_unit_bits;
	uint32_t		rcx_src_rowcol_len;
} __packed;
#define _CITRUS_MAPPER_STD_ROWCOL_INFO_SIZE		48

/* old file layout */
struct _citrus_mapper_std_rowcol_info_compat_x {
	uint32_t		rcx_src_col_bits;
	uint32_t		rcx_dst_invalid;
	uint32_t		rcx_src_row_begin;
	uint32_t		rcx_src_row_end;
	uint32_t		rcx_src_col_begin;
	uint32_t		rcx_src_col_end;
	uint32_t		rcx_dst_unit_bits;
	uint32_t		rcx_pad;
} __packed;
#define _CITRUS_MAPPER_STD_ROWCOL_INFO_COMPAT_SIZE	32

/* rowcol oob extension info */
#define _CITRUS_MAPPER_STD_SYM_ROWCOL_EXT_ILSEQ		"rowcol_ext_ilseq"
struct _citrus_mapper_std_rowcol_ext_ilseq_info_x {
#define _CITRUS_MAPPER_STD_OOB_NONIDENTICAL	0
#define _CITRUS_MAPPER_STD_OOB_ILSEQ		1
	uint32_t		eix_oob_mode;
	uint32_t		eix_dst_ilseq;
} __packed;
#define _CITRUS_MAPPER_STD_ROWCOL_EXT_ILSEQ_SIZE	8

#endif
