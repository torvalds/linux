#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>

#include "utils.h"

extern char __start___ex_table[];
extern char __stop___ex_table[];

#if defined(__powerpc64__)
#define UCONTEXT_NIA(UC)	(UC)->uc_mcontext.gp_regs[PT_NIP]
#elif defined(__powerpc__)
#define UCONTEXT_NIA(UC)	(UC)->uc_mcontext.uc_regs->gregs[PT_NIP]
#else
#error implement UCONTEXT_NIA
#endif

static void segv_handler(int signr, siginfo_t *info, void *ptr)
{
	ucontext_t *uc = (ucontext_t *)ptr;
	unsigned long addr = (unsigned long)info->si_addr;
	unsigned long *ip = &UCONTEXT_NIA(uc);
	unsigned long *ex_p = (unsigned long *)__start___ex_table;

	while (ex_p < (unsigned long *)__stop___ex_table) {
		unsigned long insn, fixup;

		insn = *ex_p++;
		fixup = *ex_p++;

		if (insn == *ip) {
			*ip = fixup;
			return;
		}
	}

	printf("No exception table match for NIA %lx ADDR %lx\n", *ip, addr);
	abort();
}

static void setup_segv_handler(void)
{
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	action.sa_sigaction = segv_handler;
	action.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &action, NULL);
}

unsigned long COPY_LOOP(void *to, const void *from, unsigned long size);
unsigned long test_copy_tofrom_user_reference(void *to, const void *from, unsigned long size);

static int total_passed;
static int total_failed;

static void do_one_test(char *dstp, char *srcp, unsigned long len)
{
	unsigned long got, expected;

	got = COPY_LOOP(dstp, srcp, len);
	expected = test_copy_tofrom_user_reference(dstp, srcp, len);

	if (got != expected) {
		total_failed++;
		printf("FAIL from=%p to=%p len=%ld returned %ld, expected %ld\n",
		       srcp, dstp, len, got, expected);
		//abort();
	} else
		total_passed++;
}

//#define MAX_LEN 512
#define MAX_LEN 16

int test_copy_exception(void)
{
	int page_size;
	static char *p, *q;
	unsigned long src, dst, len;

	page_size = getpagesize();
	p = mmap(NULL, page_size * 2, PROT_READ|PROT_WRITE,
		MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

	if (p == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	memset(p, 0, page_size);

	setup_segv_handler();

	if (mprotect(p + page_size, page_size, PROT_NONE)) {
		perror("mprotect");
		exit(1);
	}

	q = p + page_size - MAX_LEN;

	for (src = 0; src < MAX_LEN; src++) {
		for (dst = 0; dst < MAX_LEN; dst++) {
			for (len = 0; len < MAX_LEN+1; len++) {
				// printf("from=%p to=%p len=%ld\n", q+dst, q+src, len);
				do_one_test(q+dst, q+src, len);
			}
		}
	}

	printf("Totals:\n");
	printf("  Pass: %d\n", total_passed);
	printf("  Fail: %d\n", total_failed);

	return 0;
}

int main(void)
{
	return test_harness(test_copy_exception, str(COPY_LOOP));
}
