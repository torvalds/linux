/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Robert Nordier
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _BTX_H_
#define _BTX_H_

#include <sys/types.h>

#define BTX_PGSIZE	0x1000		/* Page size */
#define BTX_PGBASE	0x5000		/* Start of page tables */
#define BTX_MAXCWR	0x3bc		/* Max. btx_pgctl adjustment */

/*
 * BTX image header.
 */
struct btx_hdr {
    uint8_t	btx_machid;		/* Machine ID */
    uint8_t	btx_hdrsz;		/* Header size */
    uint8_t	btx_magic[3];		/* Magic */
    uint8_t	btx_majver;		/* Major version */
    uint8_t	btx_minver;		/* Minor version */
    uint8_t	btx_flags;		/* Flags */
    uint16_t	btx_pgctl;		/* Paging control */
    uint16_t	btx_textsz;		/* Text size */
    uint32_t	btx_entry;		/* Client entry address */
};

/* btx_machid */
#define BTX_I386	0xeb		/* Intel i386 or compatible */

/* btx_magic */
#define BTX_MAG0	'B'
#define BTX_MAG1	'T'
#define BTX_MAG2	'X'

/* btx_flags */
#define BTX_MAPONE	0x80		/* Start mapping at page 1 */

#define BTX_MAPPED(btx) (((btx).btx_pgctl | (BTX_PGSIZE / 4 - 1)) + 1)
#define BTX_ORIGIN(btx) (BTX_PGBASE + BTX_MAPPED(btx) * 4)
#define BTX_ENTRY(btx)	(BTX_ORIGIN(btx) + 2 + (btx).btx_hdrsz)

#endif /* !_BTX_H_ */
