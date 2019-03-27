/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996 Bruce D. Evans.
 * Copyright (c) 2002 by Thomas Moestl <tmm@FreeBSD.org>.
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
 *	from: src/sys/i386/isa/prof_machdep.c,v 1.16 2000/07/04 11:25:19
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef GUPROF

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/gmon.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <machine/profile.h>

int	cputime_bias;

/*
 * Return the time elapsed since the last call.  The units are machine-
 * dependent.
 * XXX: this is not SMP-safe.  It should use per-CPU variables; %tick can be
 * used though.
 */
int
cputime(void)
{
	u_long count;
	static u_long prev_count;
	int delta;

	count = rd(tick);
	delta = (int)(count - prev_count);
	prev_count = count;
	return (delta);
}

/*
 * The start and stop routines need not be here since we turn off profiling
 * before calling them.  They are here for convenience.
 */
void
startguprof(struct gmonparam *gp)
{

	gp->profrate = tick_freq;
	cputime_bias = 0;
	cputime();
}

void
stopguprof(struct gmonparam *gp)
{

	/* Nothing to do. */
}

#endif /* GUPROF */
