/*	$NetBSD: fpsetround.c,v 1.3 2002/01/13 21:45:48 thorpej Exp $	*/

/*
 * Copyright (c) 2016 Justin Hibbits
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Dan Winship.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <machine/spr.h>
#include <ieeefp.h>

#ifndef _SOFT_FLOAT
fp_rnd_t
fpsetround(fp_rnd_t rnd_dir)
{
	uint32_t fpscr;
	fp_rnd_t old;

	__asm__ __volatile("mfspr %0, %1" : "=r"(fpscr) : "K"(SPR_SPEFSCR) );
	old = (fp_rnd_t)(fpscr & 0x3);
	fpscr = (fpscr & 0xfffffffc) | rnd_dir;
	__asm__ __volatile("mtspr %1, %0" :: "r"(fpscr), "K"(SPR_SPEFSCR));
	return (old);
}
#endif
