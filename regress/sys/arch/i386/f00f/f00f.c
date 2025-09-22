/*	$OpenBSD: f00f.c,v 1.2 2018/05/21 23:34:20 bluhm Exp $	*/
/*
 * Copyright (c) 2018 Alexander Bluhm <bluhm@genua.de>
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

/* Test the Pentium f00f bug workaround by executing 0xF0,0x0F,0xC7,0xC8. */

#include <err.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
handler(int sig, siginfo_t *sip, void *ctx)
{
	printf("signo %d, code %d, errno %d\n",
	    sip->si_signo, sip->si_code, sip->si_errno);
	if (sig != SIGILL)
		errx(1, "expected SIGILL: %d", sig);
	printf("addr %p, trapno %d\n", sip->si_addr, sip->si_trapno);
	printf("instruction %.2x %.2x %.2x %.2x\n",
	    ((uint8_t *)sip->si_addr)[0],
	    ((uint8_t *)sip->si_addr)[1],
	    ((uint8_t *)sip->si_addr)[2],
	    ((uint8_t *)sip->si_addr)[3]);
	if (*((uint32_t *)sip->si_addr) != 0xc8c70ff0)
		errx(1, "expected instrcution f0 0f c7 c8");

	exit(0);
}

int
main(int argc, char *argv[])
{
	struct sigaction sa;
	uint64_t mem;
	uint32_t eax, ebx, ecx, edx;

	mem = 0x0123456789abcdefLL;
	eax = 0x11111111;
	ebx = 0x22222222;
	ecx = 0x33333333;
	edx = 0x44444444;

	/* First check that the cmpxchg8b instruction works as expected. */
	printf("mem %.16llx, edx:eax %.8x%8x, ecx:ebx %.8x%.8x\n",
	    mem, edx, eax, ecx, ebx);
	printf("cmpxchg8b mem, mem != edx:eax\n");
	asm volatile (
		"lock	cmpxchg8b %0"
		: "+m" (mem), "+a" (eax), "+b" (ebx), "+c" (ecx), "+d" (edx));
	printf("mem %.16llx, edx:eax %.8x%8x, ecx:ebx %.8x%.8x\n",
	    mem, edx, eax, ecx, ebx);

	if (mem != 0x0123456789abcdefLL)
		errx(1, "expected mem 0x0123456789abcdef");
	if (edx != 0x01234567 || eax != 0x89abcdef)
		errx(1, "expected edx:eax 0x0123456789abcdef");
	if (ecx != 0x33333333 || ebx != 0x22222222)
		errx(1, "expected ecx:ebx 0x3333333322222222");

	printf("cmpxchg8b mem, mem == edx:eax\n");
	asm volatile (
		"lock cmpxchg8b %0"
		: "+m" (mem), "+a" (eax), "+b" (ebx), "+c" (ecx), "+d" (edx));
	printf("mem %.16llx, edx:eax %.8x%8x, ecx:ebx %.8x%.8x\n",
	    mem, edx, eax, ecx, ebx);

	if (mem != 0x3333333322222222LL)
		errx(1, "expected mem 0x3333333322222222");
	if (edx != 0x01234567 || eax != 0x89abcdef)
		errx(1, "expected edx:eax 0x0123456789abcdef");
	if (ecx != 0x33333333 || ebx != 0x22222222)
		errx(1, "expected ecx:ebx 0x3333333322222222");

	/* An illegal instruction must be signalled to user land. */
	memset(&sa, 0 ,sizeof(sa));
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO;
	if (sigaction(SIGILL, &sa, NULL) == -1)
		err(2, "sigaction");

	/* Execute the cmpxchg8b instruction with invalid addressing mode. */
	printf("cmpxchg8b eax\n");
	asm volatile (
		".byte 0xF0,0x0F,0xC7,0xC8"
		: : : "%eax", "%ebx", "%ecx", "%edx");
	printf("mem %.16llx, edx:eax %.8x%8x, ecx:ebx %.8x%.8x\n",
	    mem, edx, eax, ecx, ebx);

	/* Not reached, the processor hangs or the signal handler exits. */
	errx(1, "expected signal");
}
