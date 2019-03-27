/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Marcel Moolenaar
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/signal.h>

#include <machine/asm.h>
#include <machine/gdb_machdep.h>
#include <machine/pcb.h>
#include <machine/reg.h>

#include <gdb/gdb.h>

void *
gdb_cpu_getreg(int regnum, size_t *regsz)
{
	static uint64_t synth;

	*regsz = gdb_cpu_regsz(regnum);
	switch (regnum) {
		/* 0-7: g0-g7 */
		/* 8-15: o0-o7 */
	case 14:
		synth = kdb_thrctx->pcb_sp - CCFSZ;
		return (&synth);
		/* 16-23: l0-l7 */
		/* 24-31: i0-i7 */
	case 30: return (&kdb_thrctx->pcb_sp);
		/* 32-63: f0-f31 */
		/* 64-79: f32-f62 (16 double FP) */	
	case 80: return (&kdb_thrctx->pcb_pc);
	}
	return (NULL);
}

void
gdb_cpu_setreg(int regnum, void *val)
{
	switch (regnum) {
	}
}
