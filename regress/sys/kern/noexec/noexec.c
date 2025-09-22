/*	$OpenBSD: noexec.c,v 1.23 2022/11/27 15:12:57 anton Exp $	*/

/*
 * Copyright (c) 2002,2003 Michael Shalayeff
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <err.h>
#include <pthread.h>

struct context {
	char **argv;
	int argc;
};

volatile sig_atomic_t fail;
int page_size;
char label[64] = "non-exec ";

#define PAD 64*1024
#define PAGESIZE (1ULL << _MAX_PAGE_SHIFT)
#define	MAXPAGESIZE 16384
#define TESTSZ 256	/* assuming the testfly() will fit */
static u_int64_t mutable[(PAD + TESTSZ + PAD + MAXPAGESIZE) / 8]
	__attribute__((aligned(PAGESIZE)))
	__attribute__((section(".openbsd.mutable")));

void testfly(void);

static void
fdcache(void *p, size_t size)
{
#ifdef __hppa__
	__asm volatile(	/* XXX this hardcodes the TESTSZ */
	    "fdc,m	%1(%0)\n\t"
	    "fdc,m	%1(%0)\n\t"
	    "fdc,m	%1(%0)\n\t"
	    "fdc,m	%1(%0)\n\t"
	    "fdc,m	%1(%0)\n\t"
	    "fdc,m	%1(%0)\n\t"
	    "fdc,m	%1(%0)\n\t"
	    "fdc,m	%1(%0)"
	    : "+r" (p) : "r" (32));
#endif
#ifdef __sparc64__
	char *s = p;
	int i;

	for (i = 0; i < TESTSZ; i += 8)
	  __asm volatile("flush %0" : : "r" (s + i) : "memory");
#endif
}

static void
sigsegv(int sig, siginfo_t *sip, void *scp)
{
	_exit(fail);
}

static int
noexec(void *p, size_t size)
{
	fail = 0;
	printf("%s: execute\n", label);
	fflush(stdout);
	((void (*)(void))p)();

	return (1);
}

static int
noexec_mprotect(void *p, size_t size)
{

	/* here we must fail on segv since we said it gets executable */
	fail = 1;
	if (mprotect(p, size, PROT_READ|PROT_EXEC) < 0)
		err(1, "mprotect 1");
	printf("%s: execute\n", label);
	fflush(stdout);
	((void (*)(void))p)();

	/* here we are successful on segv and fail if it still executes */
	fail = 0;
	if (mprotect(p, size, PROT_READ) < 0)
		err(1, "mprotect 2");
	printf("%s: catch a signal\n", label);
	fflush(stdout);
	((void (*)(void))p)();

	return (1);
}

static void *
getaddr(void *a)
{
	void *ret;

	/*
	 * Compile with -fno-inline to get reasonable result when comparing
	 * local variable address with caller's stack.
	 */
	if ((void *)&ret < a)
		ret = (void *)((u_long)&ret - 4 * page_size);
	else
		ret = (void *)((u_long)&ret + 4 * page_size);

	return (void *)((u_long)ret & ~(page_size - 1));
}

static int
noexec_mmap(void *p, size_t size)
{
	memcpy(p + page_size * 1, p, page_size);
	memcpy(p + page_size * 2, p, page_size);
	fdcache(p + page_size * 1, TESTSZ);
	fdcache(p + page_size * 2, TESTSZ);
	if (mprotect(p, size + 2 * page_size, PROT_READ|PROT_EXEC) != 0)
		err(1, "mprotect");

	/* here we must fail on segv since we said it gets executable */
	fail = 1;

	printf("%s: execute #1\n", label);
	fflush(stdout);
	((void (*)(void))p)();

	/* unmap the first page to see that the higher page is still exec */
	if (munmap(p, page_size) < 0)
		err(1, "munmap");

	p += page_size;
	printf("%s: execute #2\n", label);
	fflush(stdout);
	((void (*)(void))p)();

	/* unmap the last page to see that the lower page is still exec */
	if (munmap(p + page_size, page_size) < 0)
		err(1, "munmap");

	printf("%s: execute #3\n", label);
	fflush(stdout);
	((void (*)(void))p)();

	return (0);
}

static void
usage(void)
{
	extern char *__progname;
	fprintf(stderr, "Usage: %s [-s <size>] -[TDBHS] [-p] [-m]\n",
	    __progname);
	exit(2);
}

static void *
worker(void *arg)
{
	struct context *ctx = arg;
	u_int64_t stack[TESTSZ/8];	/* assuming the testfly() will fit */
	struct sigaction sa;
	int (*func)(void *, size_t);
	size_t size;
	char *ep;
	void *p, *ptr;
	int pflags, ch;

	if ((page_size = sysconf(_SC_PAGESIZE)) < 0)
		err(1, "sysconf");

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	p = NULL;
	pflags = MAP_PRIVATE|MAP_ANON|MAP_FIXED;
	func = &noexec;
	size = TESTSZ;
	while ((ch = getopt(ctx->argc, ctx->argv, "TDBMHSmps:")) != -1) {
		if (p == NULL) {
			switch (ch) {
			case 'T': {
				u_int64_t *text;
				size_t textsiz = TESTSZ;

				text = mmap(NULL, textsiz,
				    PROT_READ | PROT_WRITE,
				    MAP_PRIVATE | MAP_ANON, -1, 0);
				if (text == MAP_FAILED)
					err(1, "mmap");
				memcpy(text, &testfly, textsiz);
				if (mprotect(text, textsiz,
				    PROT_READ | PROT_EXEC) == -1)
					err(1, "mprotect");
				p = text;
				pflags &=~ MAP_FIXED;
				(void) strlcat(label, "text", sizeof(label));
				continue;
			}

			case 'D': {
				u_int64_t *data;
				size_t datasiz = (PAD + TESTSZ + PAD +
				    MAXPAGESIZE) / 8;

				data = mmap(NULL, datasiz,
				    PROT_READ | PROT_WRITE,
				    MAP_PRIVATE | MAP_ANON, -1, 0);
				if (data == MAP_FAILED)
					err(1, "mmap");
				p = &data[(PAD + page_size) / 8];
				p = (void *)((long)p & ~(page_size - 1));
				(void) strlcat(label, "data", sizeof(label));
				continue;
			}

			case 'B': {
				u_int64_t *bss;
				size_t bsssiz = (PAD + TESTSZ + PAD +
				    MAXPAGESIZE) / 8;

				bss = mmap(NULL, bsssiz,
				    PROT_READ | PROT_WRITE,
				    MAP_PRIVATE | MAP_ANON, -1, 0);
				if (bss == MAP_FAILED)
					err(1, "mmap");
				p = &bss[(PAD + page_size) / 8];
				p = (void *)((long)p & ~(page_size - 1));
				(void) strlcat(label, "bss", sizeof(label));
				continue;
			}

			case 'M':
				p = &mutable[(PAD + page_size) / 8];
				p = (void *)((long)p & ~(page_size - 1));
				(void)strlcat(label, "mutable", sizeof(label));
				continue;
			case 'H':
				p = malloc(size + 2 * page_size);
				if (p == NULL)
					err(2, "malloc");
				p += page_size;
				p = (void *)((long)p & ~(page_size - 1));
				(void) strlcat(label, "heap", sizeof(label));
				continue;
			case 'S':
				p = getaddr(&stack);
				pflags |= MAP_STACK;
				(void) strlcat(label, "stack", sizeof(label));
				continue;
			case 's':	/* only valid for heap and size */
				size = strtoul(optarg, &ep, 0);
				if (size > ULONG_MAX)
					errno = ERANGE;
				if (errno)
					err(1, "invalid size: %s", optarg);
				if (*ep)
					errx(1, "invalid size: %s", optarg);
				continue;
			}
		}
		switch (ch) {
		case 'm':
			if (p) {
				(void) strlcat(label, "-mmap", sizeof(label));
			} else {
				pflags = MAP_ANON;
				func = &noexec_mmap;
				(void) strlcat(label, "mmap", sizeof(label));
			}
			ptr = mmap(p, size + 2 * page_size,
			    PROT_READ|PROT_WRITE, pflags, -1, 0LL);
			if (ptr == MAP_FAILED) {
				err(1, "mmap: addr %p, len %zu, prot %d, "
				    "flags %d, fd %d, offset %lld",
				    p, size + 2 * page_size,
				    PROT_READ|PROT_WRITE, pflags, -1, 0LL);
			}
			p = ptr;
			break;
		case 'p':
			func = &noexec_mprotect;
			(void) strlcat(label, "-mprotect", sizeof(label));
			break;
		default:
			usage();
		}
	}
	ctx->argc -= optind;
	ctx->argv += optind;

	if (ctx->argc > 0)
		usage();

	if (p == NULL)
		exit(2);

	sa.sa_sigaction = &sigsegv;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGSEGV, &sa, NULL);

	if (p != &testfly) {
		memcpy(p, &testfly, TESTSZ);
		fdcache(p, size);
	}

	exit((*func)(p, size));
	/* NOTREACHED */
	return NULL;
}

int
main(int argc, char *argv[])
{
	struct context ctx = {.argc = argc, .argv = argv};
	pthread_t th;
	int error;

	if ((error = pthread_create(&th, NULL, worker, (void *)&ctx)))
		errc(1, error, "pthread_create");
	if ((error = pthread_join(th, NULL)))
		errc(1, error, "pthread_join");
	return 0;
}
