/*	$OpenBSD: xonly.c,v 1.2 2023/06/26 19:03:03 guenther Exp $	*/

#include <sys/types.h>
#include <sys/mman.h>

#include <dlfcn.h>
#include <err.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define UNREADABLE	0
#define READABLE	1

int main(void);
void sigsegv(int);
void setup_table(void);

void *setup_ldso(void);
void *setup_mmap_xz(void);
void *setup_mmap_x(void);
void *setup_mmap_nrx(void);
void *setup_mmap_nwx(void);
void *setup_mmap_xnwx(void);

char	***_csu_finish(char **_argv, char **_envp, void (*_cleanup)(void));

struct outcome {
	int uu;
	int ku;
};

struct readable {
	const char *name;
	void *(*setup)(void);
	int isfn;
	void *addr;
	int skip;
	struct outcome got;
} readables[] = {
	{ "ld.so",	setup_ldso, 1,		NULL,	0, {} },
	{ "mmap xz",	setup_mmap_xz, 0,	NULL,	0, {} },
	{ "mmap x",	setup_mmap_x, 0,	NULL,	0, {} },
	{ "mmap nrx",	setup_mmap_nrx, 0,	NULL,	0, {} },
	{ "mmap nwx",	setup_mmap_nwx, 0,	NULL,	0, {} },
	{ "mmap xnwx",	setup_mmap_xnwx, 0,	NULL,	0, {} },
	{ "main",	NULL, 1,		&main,	0, {} },
	{ "libc",	NULL, 1,		&_csu_finish, 0, {} },
};

static struct outcome expectations[2][8] = {
#if defined(__aarch64__) || defined(__amd64__)
	[0] = {
		/* ld.so */	{ UNREADABLE,	UNREADABLE },
		/* mmap xz */	{ UNREADABLE,	UNREADABLE },
		/* mmap x */	{ UNREADABLE,	UNREADABLE },
		/* mmap nrx */	{ UNREADABLE,	UNREADABLE },
		/* mmap nwx */	{ UNREADABLE,	UNREADABLE },
		/* mmap xnwx */	{ UNREADABLE,	UNREADABLE },
		/* main */	{ UNREADABLE,	UNREADABLE },
		/* libc */	{ UNREADABLE,	UNREADABLE },
	},
#else
#error "unknown architecture"
#endif
#if defined(__amd64__)
	/* PKU not available. */
	[1] = {
		/* ld.so */	{ READABLE,	UNREADABLE },
		/* mmap xz */	{ UNREADABLE,	UNREADABLE },
		/* mmap x */	{ READABLE,	READABLE },
		/* mmap nrx */	{ READABLE,	READABLE },
		/* mmap nwx */	{ READABLE,	READABLE },
		/* mmap xnwx */	{ READABLE,	READABLE },
		/* main */	{ READABLE,	UNREADABLE },
		/* libc */	{ READABLE,	UNREADABLE },
	},
#endif
};

jmp_buf fail;

void
sigsegv(__unused int signo)
{
	longjmp(fail, 1);
}

void *
setup_mmap_xz(void)
{
	return mmap(NULL, getpagesize(), PROT_EXEC,
	    MAP_ANON | MAP_PRIVATE, -1, 0);
	/* no data written. tests read-fault of an unbacked exec-only page */
}

void *
setup_mmap_x(void)
{
	char *addr;

	addr = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE,
	    MAP_ANON | MAP_PRIVATE, -1, 0);
	explicit_bzero(addr, getpagesize());
	mprotect(addr, getpagesize(), PROT_EXEC);
	return addr;
}

void *
setup_mmap_nrx(void)
{
	char *addr;

	addr = mmap(NULL, getpagesize(), PROT_NONE,
	    MAP_ANON | MAP_PRIVATE, -1, 0);
	mprotect(addr, getpagesize(), PROT_READ | PROT_WRITE);
	explicit_bzero(addr, getpagesize());
	mprotect(addr, getpagesize(), PROT_EXEC);
	return addr;
}

void *
setup_mmap_nwx(void)
{
	char *addr;

	addr = mmap(NULL, getpagesize(), PROT_NONE,
	    MAP_ANON | MAP_PRIVATE, -1, 0);
	mprotect(addr, getpagesize(), PROT_WRITE);
	explicit_bzero(addr, getpagesize());
	mprotect(addr, getpagesize(), PROT_EXEC);
	return addr;
}

void *
setup_mmap_xnwx(void)
{
	char *addr;

	addr = mmap(NULL, getpagesize(), PROT_EXEC,
	    MAP_ANON | MAP_PRIVATE, -1, 0);
	mprotect(addr, getpagesize(), PROT_NONE);
	mprotect(addr, getpagesize(), PROT_WRITE);
	explicit_bzero(addr, getpagesize());
	mprotect(addr, getpagesize(), PROT_EXEC);
	return addr;
}

void *
setup_ldso(void)
{
	void *dlopenp, *handle;

	handle = dlopen("ld.so", RTLD_NOW);
	if (handle == NULL)
		errx(1, "dlopen");
	dlopenp = dlsym(handle, "dlopen");
	return dlopenp;
}

void
setup_table(void)
{
	size_t i;

	for (i = 0; i < sizeof(readables)/sizeof(readables[0]); i++) {
		if (setjmp(fail) == 0) {
			if (readables[i].setup)
				readables[i].addr = readables[i].setup();
		} else {
			readables[i].skip = 1;
		}
#ifdef __hppa__
		/* hppa ptable headers point at the instructions */
		if (readables[i].isfn) {
			readables[i].addr = (void *)*(u_int *)
			    ((u_int)readables[i].addr & ~3);
		}
#endif
	}
}

int
main(void)
{
	size_t i;
	int p[2];
	int error = 0;
	const struct outcome *desires = expectations[0];

#if defined(__amd64__)
	{
		uint32_t ebx, ecx, edx;
		asm("cpuid" : "=b" (ebx), "=c" (ecx), "=d" (edx) : "a" (7), "c" (0));
		if ((ecx & 8) == 0) /* SEFF0ECX_PKU */
			desires = expectations[1];
	}
#endif


	signal(SIGSEGV, sigsegv);
	signal(SIGBUS, sigsegv);

	setup_table();

	for (i = 0; i < sizeof(readables)/sizeof(readables[0]); i++) {
		struct readable *r = &readables[i];
		char c;

		if (r->skip)
			continue;
		pipe(p);
		fcntl(p[0], F_SETFL, O_NONBLOCK);

		if (write(p[1], r->addr, 1) == 1 && read(p[0], &c, 1) == 1)
			r->got.ku = 1;

		if (setjmp(fail) == 0) {
			volatile int x = *(int *)(r->addr);
			(void)x;
			r->got.uu = 1;
		}

		close(p[0]);
		close(p[1]);
	}

	printf("%-16s  %18s  userland     kernel\n", "", "");
	for (i = 0; i < sizeof(readables)/sizeof(readables[0]); i++) {
		struct readable *r = &readables[i];

		if (r->skip) {
			printf("%-16s  %18p  %-12s %-12s\n", r->name, r->addr,
			    "skipped", "skipped");
		} else {
			const struct outcome *want = &desires[i];

			if (r->got.uu != want->uu || r->got.ku != want->ku)
				error++;

			printf("%-16s  %18p  %s%-10s %s%-10s\n",
			    r->name, r->addr,
			    r->got.uu == want->uu ? "P " : "F ",
			    r->got.uu ? "readable" : "unreadable",
			    r->got.ku == want->ku ? "P " : "F ",
			    r->got.ku ? "readable" : "unreadable");
		}
	}
	return error;
}
