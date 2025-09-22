/*	$OpenBSD: reg.c,v 1.3 2016/08/14 23:01:13 guenther Exp $	*/

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
#include <sys/types.h>
#include <stdio.h>

int64_t asm_popc(int64_t);
int64_t c_popc(int64_t);
int test_it(int64_t);
int test_ones(void);
int main(void);

int64_t
asm_popc(int64_t v)
{
	asm("popc %1, %0" : "=r" (v) : "r" (v));
	return (v);
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
test_it(int64_t v)
{
	int64_t tc, ta;

	tc = c_popc(v);
	ta = asm_popc(v);
	if (tc == ta)
		return (0);
	printf("%lld: C(%lld) ASM(%lld)\n", v, tc, ta);
	return (1);
}

int
test_ones(void)
{
	int64_t v;
	int i, r = 0;

	for (i = 0; i < 64; i++) {
		v = 1LL << i;
		if (c_popc(v) != 1) {
			printf("ONES popc(%lld) != 1\n", v);
			r = 1;
		}
		if (test_it(v))
			r = 1;
	}
	return (r);
}

int
main()
{
	int r = 0;

	if (test_ones())
		r = 1;

	return (r);
}
