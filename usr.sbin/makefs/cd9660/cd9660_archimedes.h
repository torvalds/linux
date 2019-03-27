/* $NetBSD: cd9660_archimedes.h,v 1.1 2009/01/10 22:06:29 bjh21 Exp $ */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1998, 2009 Ben Harris
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
/*
 * cd9660_archimedes.c - support for RISC OS "ARCHIMEDES" extension
 *
 * $FreeBSD$
 */

struct ISO_ARCHIMEDES {
	char		magic[10];	/* "ARCHIMEDES" */
	unsigned char	loadaddr[4];	/* Load address, little-endian */
	unsigned char	execaddr[4];	/* Exec address, little-endian */
	unsigned char	ro_attr;	/* RISC OS attributes */
#define RO_ACCESS_UR	0x01 /* Owner read */
#define RO_ACCESS_UW	0x02 /* Owner write */
#define RO_ACCESS_L	0x04 /* Locked */
#define RO_ACCESS_OR	0x10 /* Public read */
#define RO_ACCESS_OW	0x20 /* Public write */
	unsigned char	cdfs_attr;	/* Extra attributes for CDFS */
#define CDFS_PLING	0x01	/* Filename begins with '!' */
	char		reserved[12];
};

extern void archimedes_convert_tree(cd9660node *);
