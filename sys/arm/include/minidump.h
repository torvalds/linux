/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Peter Wemm
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
 * From: FreeBSD: src/sys/i386/include/minidump.h,v 1.1 2006/04/21 04:28:43
 * $FreeBSD$
 */

#ifndef	_MACHINE_MINIDUMP_H_
#define	_MACHINE_MINIDUMP_H_

#define	MINIDUMP_MAGIC		"minidump FreeBSD/arm"
#define	MINIDUMP_VERSION	1

/*
 * The first page of vmcore is dedicated to the following header.
 * As the rest of the page is zeroed, any header extension can be
 * done without version bumping. It should be taken into account
 * only that new entries will be zero in old vmcores.
 */

struct minidumphdr {
	char magic[24];
	uint32_t version;
	uint32_t msgbufsize;
	uint32_t bitmapsize;
	uint32_t ptesize;
	uint32_t kernbase;
	uint32_t arch;
	uint32_t mmuformat;
};

#define MINIDUMP_MMU_FORMAT_UNKNOWN	0
#define MINIDUMP_MMU_FORMAT_V4		1
#define MINIDUMP_MMU_FORMAT_V6		2
#define MINIDUMP_MMU_FORMAT_V6_LPAE	3

#endif /* _MACHINE_MINIDUMP_H_ */
