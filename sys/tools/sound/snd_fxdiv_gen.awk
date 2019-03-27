#!/usr/bin/awk -f
#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2008-2009 Ariff Abdullah <ariff@FreeBSD.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

function floor(x, r)
{
	r = int(x);
	if (r > x)
		r--;
	return (r + 0);
}

function shl(x, y)
{
	while (y > 0) {
		x *= 2;
		y--;
	}
	return (x);
}

function shr(x, y)
{
	while (y > 0 && x != 0) {
		x = floor(x / 2);
		y--;
	}
	return (x);
}

function calcdiv(r, x, y, z)
{
	y = floor(FXONE / x);
	z = FXSHIFT;

	while (shr((y * x), z) < 1)
		y++;

	while ((y % 2) == 0 && z > 0) {
		y = floor(y / 2);
		z--;
	}

	r["mul"] = y;
	r["shift"] = z;
}

BEGIN {
	FXSHIFT = 16;
	FXONE   = shl(1, FXSHIFT);

	SND_CHN_MAX = 127;

	PCM_8_BPS  = 1;
	PCM_16_BPS = 2;
	PCM_24_BPS = 3;
	PCM_32_BPS = 4;

	SND_MAX_ALIGN = SND_CHN_MAX * PCM_32_BPS;

	for (i = 1; i <= SND_CHN_MAX; i++) {
		aligns[PCM_8_BPS * i]  = 1;
		aligns[PCM_16_BPS * i] = 1;
		aligns[PCM_24_BPS * i] = 1;
		aligns[PCM_32_BPS * i] = 1;
	}

	printf("#ifndef _SND_FXDIV_GEN_H_\n");
	printf("#define _SND_FXDIV_GEN_H_\n\n");

	printf("/*\n");
	printf(" * Generated using snd_fxdiv_gen.awk, heaven, wind and awesome.\n");
	printf(" *\n");
	printf(" * DO NOT EDIT!\n");
	printf(" */\n\n");
	printf("#ifdef SND_USE_FXDIV\n\n");

	printf("/*\n");
	printf(" * Fast unsigned 32bit integer division and rounding, accurate for\n");
	printf(" * x = 1 - %d. This table should be enough to handle possible\n", FXONE);
	printf(" * division for 1 - 508 (more can be generated though..).\n");
	printf(" *\n");
	printf(" * 508 = SND_CHN_MAX * PCM_32_BPS, which is why....\n");
	printf(" */\n\n");

	printf("extern const uint32_t snd_fxdiv_table[%d][2];\n\n", SND_MAX_ALIGN + 1);

	printf("#ifdef SND_DECLARE_FXDIV\n");
	printf("const uint32_t snd_fxdiv_table[%d][2] = {\n", SND_MAX_ALIGN + 1);

	for (i = 1; i <= SND_MAX_ALIGN; i++) {
		if (aligns[i] != 1)
			continue;
		calcdiv(r, i);
		printf("\t[0x%02x] = { 0x%04x, 0x%02x },",		\
		    i, r["mul"], r["shift"]);
		printf("\t/* x / %-2d = (x * %-5d) >> %-2d */\n",	\
		    i, r["mul"], r["shift"]);
	}

	printf("};\n#endif\n\n");

	printf("#define SND_FXDIV_MAX\t\t0x%08x\n", FXONE);
	printf("#define SND_FXDIV(x, y)\t\t(((uint32_t)(x) *\t\t\t\\\n");
	printf("\t\t\t\t    snd_fxdiv_table[y][0]) >>\t\t\\\n");
	printf("\t\t\t\t    snd_fxdiv_table[y][1])\n");
	printf("#define SND_FXROUND(x, y)\t(SND_FXDIV(x, y) * (y))\n");
	printf("#define SND_FXMOD(x, y)\t\t((x) - SND_FXROUND(x, y))\n\n");

	printf("#else\t/* !SND_USE_FXDIV */\n\n");

	printf("#define SND_FXDIV_MAX\t\t0x%08x\n", 131072);
	printf("#define SND_FXDIV(x, y)\t\t((x) / (y))\n");
	printf("#define SND_FXROUND(x, y)\t((x) - ((x) %% (y)))\n");
	printf("#define SND_FXMOD(x, y)\t\t((x) %% (y))\n\n");

	printf("#endif\t/* SND_USE_FXDIV */\n\n");

	printf("#endif\t/* !_SND_FXDIV_GEN_H_ */\n");
}
