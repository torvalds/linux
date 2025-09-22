/*	$OpenBSD: bootarg.h,v 1.17 2020/05/25 15:49:42 deraadt Exp $	*/

/*
 * Copyright (c) 1996-1999 Michael Shalayeff
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
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#define	BOOTARG_APIVER	(BAPIV_VECTOR|BAPIV_ENV|BAPIV_BMEMMAP)
#define	BAPIV_ANCIENT	0x00000000	/* MD old i386 bootblocks */
#define	BAPIV_VARS	0x00000001	/* MD structure w/ add info passed */
#define	BAPIV_VECTOR	0x00000002	/* MI vector of MD structures passed */
#define	BAPIV_ENV	0x00000004	/* MI environment vars vector */
#define	BAPIV_BMEMMAP	0x00000008	/* MI memory map passed is in bytes */

typedef struct _boot_args {
	int ba_type;
	size_t ba_size;
	struct _boot_args *ba_next;
	int ba_arg[1];
} bootarg_t;

#define	BOOTARG_ENV	0x1000
#define	BOOTARG_END	-1

#if defined(_KERNEL) || defined(_STANDALONE)
extern void *bootargv;
extern int bootargc;
extern bootarg_t *bootargp;
#endif

int	loadrandom(char *name, char *buf, size_t buflen);
int	mdrandom(char *buf, size_t buflen);
int	fwrandom(char *buf, size_t buflen);

#ifdef _STANDALONE
void addbootarg(int, size_t, void *);
void makebootargs(caddr_t, size_t *);
void makebootargs32(caddr_t, size_t *);
#endif /* _STANDALONE */
