/*	$OpenBSD: bootconfig.h,v 1.2 2019/05/06 03:34:43 mlarkin Exp $	*/
/*	$NetBSD: bootconfig.h,v 1.2 2001/06/21 22:08:28 chris Exp $	*/

/*
 * Copyright (c) 1994 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Mark Brinicombe
 *	for the NetBSD Project.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * boot configuration structures
 *
 * Created      : 12/09/94
 *
 * Based on kate/boot/bootconfig.h
 */

#if defined(_KERNEL) || defined(_STANDALONE)

typedef struct _PhysMem {
	u_int address;
	u_int pages;
} PhysMem;

#define	DRAM_BLOCKS	2
#define	MAX_BOOT_STRING 255

typedef struct _BootConfig {
	PhysMem dram[DRAM_BLOCKS];
	u_int dramblocks;
	char bootstring[MAX_BOOT_STRING];
} BootConfig;

extern BootConfig bootconfig;

#endif	/* _KERNEL || _STANDALONE */
#if defined(_KERNEL)
extern char *boot_args;
extern char *boot_file;
#endif	/* _KERNEL */

/* End of bootconfig.h */
