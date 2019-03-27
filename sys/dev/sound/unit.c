/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Ariff Abdullah <ariff@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/unit.h>

/*
 * Unit magic allocator for sound driver.
 *
 * 'u' = Unit of attached soundcards
 * 'd' = Device type
 * 'c' = Channel number
 *
 * eg: dsp0.p1  - u=0, d=p, c=1
 *     dsp1.vp0 - u=1, d=vp, c=0
 *     dsp0.10  - u=0, d=clone, c=allocated clone (see further explanation)
 *
 * Maximum unit of soundcards can be tuned through "hw.snd.maxunit", which
 * is between SND_UNIT_UMIN (16) and SND_UNIT_UMAX (2048). By design,
 * maximum allowable allocated channel is 256, with exception for clone
 * devices which doesn't have any notion of channel numbering. The use of
 * channel numbering in a clone device is simply to provide uniqueness among
 * allocated clones. This also means that the maximum allowable clonable
 * device is largely dependant and dynamically tuned depending on
 * hw.snd.maxunit.
 */

/* Default width */
static int snd_u_shift = 9;	/* 0 - 0x1ff :  512 distinct soundcards   */
static int snd_d_shift = 5;	/* 0 - 0x1f  :   32 distinct device types */
static int snd_c_shift = 10;	/* 0 - 0x3ff : 1024 distinct channels
					       (256 limit "by design",
					       except for clone devices)  */

static int snd_unit_initialized = 0;

#ifdef SND_DIAGNOSTIC
#define SND_UNIT_ASSERT()	do {					\
	if (snd_unit_initialized == 0)					\
		panic("%s(): Uninitialized sound unit!", __func__);	\
} while (0)
#else
#define SND_UNIT_ASSERT()	KASSERT(snd_unit_initialized != 0,	\
				("%s(): Uninitialized sound unit!",	\
				__func__))
#endif

#define MKMASK(x)	((1 << snd_##x##_shift) - 1)

int
snd_max_u(void)
{
	SND_UNIT_ASSERT();

	return (MKMASK(u));
}

int
snd_max_d(void)
{
	SND_UNIT_ASSERT();

	return (MKMASK(d));
}

int
snd_max_c(void)
{
	SND_UNIT_ASSERT();

	return (MKMASK(c));
}

int
snd_unit2u(int unit)
{
	SND_UNIT_ASSERT();

	return ((unit >> (snd_c_shift + snd_d_shift)) & MKMASK(u));
}

int
snd_unit2d(int unit)
{
	SND_UNIT_ASSERT();

	return ((unit >> snd_c_shift) & MKMASK(d));
}

int
snd_unit2c(int unit)
{
	SND_UNIT_ASSERT();

	return (unit & MKMASK(c));
}

int
snd_u2unit(int u)
{
	SND_UNIT_ASSERT();

	return ((u & MKMASK(u)) << (snd_c_shift + snd_d_shift));
}

int
snd_d2unit(int d)
{
	SND_UNIT_ASSERT();

	return ((d & MKMASK(d)) << snd_c_shift);
}

int
snd_c2unit(int c)
{
	SND_UNIT_ASSERT();

	return (c & MKMASK(c));
}

int
snd_mkunit(int u, int d, int c)
{
	SND_UNIT_ASSERT();

	return ((c & MKMASK(c)) | ((d & MKMASK(d)) << snd_c_shift) |
	    ((u & MKMASK(u)) << (snd_c_shift + snd_d_shift)));
}

/*
 * This *must* be called first before any of the functions above!!!
 */
void
snd_unit_init(void)
{
	int i;

	if (snd_unit_initialized != 0)
		return;

	snd_unit_initialized = 1;

	if (getenv_int("hw.snd.maxunit", &i) != 0) {
		if (i < SND_UNIT_UMIN)
			i = SND_UNIT_UMIN;
		else if (i > SND_UNIT_UMAX)
			i = SND_UNIT_UMAX;
		else
			i = roundup2(i, 2);

		for (snd_u_shift = 0; (i >> (snd_u_shift + 1)) != 0;
		    snd_u_shift++)
			;

		/*
		 * Make room for channels/clones allocation unit
		 * to fit within 24bit MAXMINOR limit.
		 */
		snd_c_shift = 24 - snd_u_shift - snd_d_shift;
	}

	if (bootverbose != 0)
		printf("%s() u=0x%08x [%d] d=0x%08x [%d] c=0x%08x [%d]\n",
		    __func__, SND_U_MASK, snd_max_u() + 1,
		    SND_D_MASK, snd_max_d() + 1, SND_C_MASK, snd_max_c() + 1);
}
