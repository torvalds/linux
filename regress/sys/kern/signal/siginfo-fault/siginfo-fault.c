/*	$OpenBSD: siginfo-fault.c,v 1.2 2021/09/28 08:56:15 kettenis Exp $	*/
/*
 * Copyright (c) 2014 Google Inc.
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

#include <sys/mman.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Some architectures may deliver an imprecise fault address.
 */
#ifdef __sparc64__
#define EXPADDR_MASK	~(3UL)
#else
#define EXPADDR_MASK	~(0UL)
#endif

#define CHECK_EQ(a, b) assert((a) == (b))
#define CHECK_NE(a, b) assert((a) != (b))
#define CHECK_LE(a, b) assert((a) <= (b))
#define FAIL() assert(0)

static jmp_buf env;
static volatile int gotsigno;
static volatile siginfo_t gotsi;

static void
sigsegv(int signo, siginfo_t *si, void *ctx)
{
	gotsigno = signo;
	gotsi = *si;
	siglongjmp(env, 1);
}

static const char *
strsigcode(int signum, int sigcode)
{
	switch (signum) {
	case SIGSEGV:
		switch (sigcode) {
		case SEGV_MAPERR:
			return "address not mapped to object";
		case SEGV_ACCERR:
			return "invalid permissions";
		}
		break;
	case SIGBUS:
		switch (sigcode) {
		case BUS_ADRALN:
			return "invalid address alignment";
		case BUS_ADRERR:
			return "non-existent physical address";
		case BUS_OBJERR:
			return "object specific hardware error";
		}
		break;
	}
	return "unknown";
}

static int
checksig(const char *name, int expsigno, int expcode, volatile char *expaddr)
{
	int fail = 0;
	char str1[NL_TEXTMAX], str2[NL_TEXTMAX];

	expaddr = (char *)((uintptr_t)expaddr & EXPADDR_MASK);

	if (expsigno != gotsigno) {
		strlcpy(str1, strsignal(expsigno), sizeof(str1));
		strlcpy(str2, strsignal(gotsigno), sizeof(str2));
		fprintf(stderr, "%s signo: expect %d (%s), actual %d (%s)\n",
		    name, expsigno, str1, gotsigno, str2);
		++fail;
	}
	if (expsigno != gotsi.si_signo) {
		strlcpy(str1, strsignal(expsigno), sizeof(str1));
		strlcpy(str2, strsignal(gotsi.si_signo), sizeof(str2));
		fprintf(stderr, "%s si_signo: expect %d (%s), actual %d (%s)\n",
		    name, expsigno, str1, gotsi.si_signo, str2);
		++fail;
	}
	if (expcode != gotsi.si_code) {
		fprintf(stderr, "%s si_code: expect %d (%s), actual %d (%s)\n",
		    name, expcode, strsigcode(expsigno, expcode),
		    gotsi.si_code, strsigcode(gotsigno, gotsi.si_code));
		++fail;
	}
	if (expaddr != (char *)((uintptr_t)gotsi.si_addr & EXPADDR_MASK)) {
		fprintf(stderr, "%s si_addr: expect %p, actual %p\n",
		    name, expaddr, gotsi.si_addr);
		++fail;
	}
	return (fail);
}

int
main()
{
	int fail = 0;
	long pagesize = sysconf(_SC_PAGESIZE);
	CHECK_NE(-1, pagesize);

	const struct sigaction sa = {
		.sa_sigaction = sigsegv,
		.sa_flags = SA_SIGINFO,
	};
	CHECK_EQ(0, sigaction(SIGSEGV, &sa, NULL));
	CHECK_EQ(0, sigaction(SIGBUS, &sa, NULL));

	volatile char *p;
	CHECK_NE(MAP_FAILED, (p = mmap(NULL, pagesize, PROT_NONE,
	    MAP_PRIVATE|MAP_ANON, -1, 0)));

	CHECK_EQ(0, mprotect((void *)p, pagesize, PROT_READ));
	if (sigsetjmp(env, 1) == 0) {
		p[0] = 1;
		FAIL();
	}
	fail += checksig("mprotect read", SIGSEGV, SEGV_ACCERR, p);

	CHECK_EQ(0, mprotect((void *)p, pagesize, PROT_NONE));
	if (sigsetjmp(env, 1) == 0) {
		(void)p[1];
		FAIL();
	}
	fail += checksig("mprotect none", SIGSEGV, SEGV_ACCERR, p + 1);

	CHECK_EQ(0, munmap((void *)p, pagesize));
	if (sigsetjmp(env, 1) == 0) {
		(void)p[2];
		FAIL();
	}
	fail += checksig("munmap", SIGSEGV, SEGV_MAPERR, p + 2);

	char filename[] = "/tmp/siginfo-fault.XXXXXXXX";
	int fd;
	CHECK_LE(0, (fd = mkstemp(filename)));
	CHECK_EQ(0, unlink(filename));
	CHECK_EQ(0, ftruncate(fd, 0));  /* just in case */
	CHECK_NE(MAP_FAILED, (p = mmap(NULL, pagesize, PROT_READ|PROT_WRITE,
	    MAP_SHARED, fd, 0)));
	CHECK_EQ(0, close(fd));

	if (sigsetjmp(env, 1) == 0) {
		p[3] = 1;
		FAIL();
	}
	fail += checksig("mmap file", SIGBUS, BUS_OBJERR, p + 3);

	return (fail);
}
