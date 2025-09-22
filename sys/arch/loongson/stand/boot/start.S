/*	$OpenBSD: start.S,v 1.4 2021/02/17 14:42:20 visa Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <machine/asm.h>
#include <machine/regdef.h>
#include <mips64/mips_cpu.h>

	.set	noreorder
	.set	mips3

	.data
pmon_ra:
	.dword	0
pmon_sp:
	.dword	0

	.text

	.globl	__start
	.ent	__start, 0
__start:
	LA	t0, __start - FRAMESZ(CF_SZ)
	PTR_S	ra, pmon_ra		# save pmon return address
	PTR_S	sp, pmon_sp		# and stack
	move	sp, t0
	jal	pmon_init		# pmon_init(argc, argv, envp, callvec,
	mfc0	a4, COP_0_PRID		#     prid)
	jal	pmon_probe_abi
	nop
	jal	main
	nop
	/* FALLTHROUGH */
	.end	__start

	.globl	_rtt
	.ent	_rtt,0
_rtt:
	PTR_L	ra, pmon_ra
	PTR_L	sp, pmon_sp
	jr	ra
	nop
	.end	_rtt
