/* $OpenBSD: decmonitors.c,v 1.3 2008/06/26 05:42:15 ray Exp $ */

/*-
 * Copyright (c) 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Roland C. Dowdeswell.
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
 */

#include <sys/types.h>
#include <dev/ic/monitors.h>

#define MHz	* 1000000
#define KHz	* 1000

struct monitor decmonitors[] = {
	/* 0x0: 1280 x 1024 @ 72Hz */
	{ 1280,	32,	160,	232,
	  1024,	3,	3,	33,
	  130808 KHz },

	/* 0x1: 1280 x 1024 @ 66Hz */
	{ 1280,	32,	160,	232,
	  1024,	3,	3,	33,
	  119840 KHz },

	/* 0x2: 1280 x 1024 @ 60Hz */
	{ 1280,	44,	184,	200,
	  1024,	3,	3,	26,
	  108180 KHz },

	/* 0x3: 1152 x  900 @ 72Hz */
	{ 1152,	64,	112,	176,
	  900,	6,	10,	44,
	  103994 KHz },

	/* 0x4: 1600 x 1200 @ 65Hz */
	{ 1600,	32,	192,	336,
	  1200,	1,	3,	46,
	  175 MHz },

	/* 0x5: 1024 x  768 @ 70Hz */
	{ 1024,	24,	136,	144,
	  768,	3,	6,	29,
	  75 MHz },

	/* 0x6: 1024 x  768 @ 72Hz */
	{ 1024,	16,	128,	128,
	  768,	1,	6,	22,
	  74 MHz },

	/* 0x7: 1024 x  864 @ 60Hz */
	{ 1024,	12,	128,	116,
	  864,	0,	3,	34,
	  69 MHz },

	/* 0x8: 1024 x  768 @ 60Hz */
	{ 1024,	56,	64,	200,
	  768,	7,	9,	26,
	  65 MHz },

	/* 0x9:  800 x  600 @ 72Hz */
	{ 800,	56,	120,	64,
	  600,	37,	6,	23,
	  50 MHz },

	/* 0xa:  800 x  600 @ 60Hz */
	{ 800,	40,	128,	88,
	  600,	1,	4,	23,
	  40 MHz },

	/* 0xb:  640 x  480 @ 72Hz */
	{ 640,	24,	40,	128,
	  480,	9,	3,	28,
	  31500 KHz },

	/* 0xc:  640 x  480 @ 60Hz */
	{ 640,	16,	96,	48,
	  480,	10,	2,	33,
	  25175 KHz },

	/* 0xd: 1280 x 1024 @ 75Hz */
	{ 1280,	16,	144,	248,
	  1024,	1,	3,	38,
	  135 MHz  },

	/* 0xe: 1280 x 1024 @ 60Hz */
	{ 1280,	19,	163,	234,
	  1024,	6,	7,	44,
	  110 MHz },

	/* 0xf: 1600 x 1200 @ 75Hz */
	/* XXX -- this one's weird.  rcd */
	{ 1600,	32,	192,	336,
	  1200,	1,	3,	46,
	  202500 KHz }
};

#undef MHz
#undef KHz
