/*	$OpenBSD: simm13.c,v 1.6 2014/04/18 14:38:21 guenther Exp $	*/

/*
 * Copyright (c) 2003 Jason L. Wright (jason@thought.net)
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * check that "popc immediate, %rd" gets the correct value.  It is
 * emulated on most SPARC v9 implementations.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <machine/instr.h>
#include <err.h>
#include <stdio.h>

#define	SIGN_EXT13(v)	(((int64_t)(v) << 51) >> 51)

void gen_simm(u_int32_t *, int);
int64_t a_popc_imm(void *, int);
int64_t c_popc(int64_t);
int main(void);

void
gen_simm(u_int32_t *p, int imm)
{
	/*
	 * generate the following asm, and flush the pipeline
	 *	retl
	 *	 popc imm, %o0
	 */
	p[0] = I_JMPLri(I_G0, I_O7, 8);
	__asm volatile("iflush %0+0" : : "r" (p + 0));
	p[1] = _I_OP3_R_RI(I_O0, IOP3_POPC, I_G0, imm);
	__asm volatile("iflush %0+0" : : "r" (p + 1));
	__asm volatile("nop;nop;nop;nop;nop");
}

int64_t
a_popc_imm(void *v, int i)
{
	int64_t (*func)(void) = v, r;

	if (mprotect(v, 2 * sizeof(union instr), PROT_READ|PROT_WRITE) == -1)
		err(1, "mprotect");
	gen_simm(v, i);

	if (mprotect(v, 2 * sizeof(union instr), PROT_READ|PROT_EXEC) == -1)
		err(1, "mprotect");
	r = (*func)();

	if (mprotect(v, 2 * sizeof(union instr), PROT_NONE) == -1)
		err(1, "mprotect");

	return (r);
}

int64_t
c_popc(int64_t v)
{
	int64_t bit, r;

	for (bit = 1, r = 0; bit; bit <<= 1)
		if (v & bit)
			r++;
	return (r);
}

int
main()
{
	void *v;
	int i, a, c;
	int r = 0;

	v = mmap(NULL, 2 * sizeof(union instr), PROT_NONE, MAP_ANON, -1, 0);
	if (v == MAP_FAILED)
		err(1, "mmap");

	for (i = -4096; i <= 4095; i++) {
		a = a_popc_imm(v, i);
		c = c_popc(SIGN_EXT13(i));
		if (c != a) {
			printf("BAD: %d: asm %d, c %d\n", i, a, c);
			r = 1;
		}
	}

	return (r);
}
