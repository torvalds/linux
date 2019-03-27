/* $FreeBSD$ */
/* $NetBSD: citrus_db_file.h,v 1.4 2008/02/10 05:58:22 junyoung Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)2003 Citrus Project,
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

#ifndef _CITRUS_DB_FILE_H_
#define _CITRUS_DB_FILE_H_

/*
 * db format:
 *  +---
 *  | header
 *  |  - magic
 *  |  - num entries
 *  +---
 *  | entry directory
 *  |  +------------
 *  |  | entry0
 *  |  |  - hash value
 *  |  |  - next entry
 *  |  |  - key offset
 *  |  |  - key len
 *  |  |  - data offset
 *  |  |  - data size
 *  |  |---
 *  |  | entry1
 *  |  | ..
 *  |  | entryN
 *  |  +---
 *  +---
 *  | key table
 *  |  - key0
 *  |   ...
 *  |  - keyN
 *  +---
 *  | data table
 *  |  - data0
 *  |   ...
 *  |  - dataN
 *  +---
 */

#define _CITRUS_DB_MAGIC_SIZE	8
#define _CITRUS_DB_HEADER_SIZE	16
struct _citrus_db_header_x {
	char		dhx_magic[_CITRUS_DB_MAGIC_SIZE];
	uint32_t	dhx_num_entries;
	uint32_t	dhx_entry_offset;
} __packed;

struct _citrus_db_entry_x {
	uint32_t	dex_hash_value;
	uint32_t	dex_next_offset;
	uint32_t	dex_key_offset;
	uint32_t	dex_key_size;
	uint32_t	dex_data_offset;
	uint32_t	dex_data_size;
} __packed;
#define _CITRUS_DB_ENTRY_SIZE	24

#endif
