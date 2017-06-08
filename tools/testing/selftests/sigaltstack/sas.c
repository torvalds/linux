/*
 * Stas Sergeev <stsp@users.sourceforge.net>
 *
 * test sigaltstack(SS_ONSTACK | SS_AUTODISARM)
 * If that succeeds, then swapcontext() can be used inside sighandler safely.
 *
 */

#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <alloca.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#ifndef SS_AUTODISARM
#define SS_AUTODISARM  (1U << 31)
#endif

static void *sstack, *ustack;
static ucontext_t uc, sc;
static const char *msg = "[OK]\tStack preserved";
static const char *msg2 = "[FAIL]\tStack corrupted";
struct stk_data {
	char msg[128];
	int flag;
};

void my_usr1(int sig, siginfo_t *si, void *u)
{
	char *aa;
	int err;
	stack_t stk;
	struct stk_data *p;

	register unsigned long sp asm("sp");

	if (sp < (unsigned long)sstack ||
			sp >= (unsigned long)sstack + SIGSTKSZ) {
		printf("[FAIL]\tSP is not on sigaltstack\n");
		exit(EXIT_FAILURE);
	}
	/* put some data on stack. other sighandler will try to overwrite it */
	aa = alloca(1024);
	assert(aa);
	p = (struct stk_data *)(aa + 512);
	strcpy(p->msg, msg);
	p->flag = 1;
	printf("[RUN]\tsignal USR1\n");
	err = sigaltstack(NULL, &stk);
	if (err) {
		perror("[FAIL]\tsigaltstack()");
		exit(EXIT_FAILURE);
	}
	if (stk.ss_flags != SS_DISABLE)
		printf("[FAIL]\tss_flags=%x, should be SS_DISABLE\n",
				stk.ss_flags);
	else
		printf("[OK]\tsigaltstack is disabled in sighandler\n");
	swapcontext(&sc, &uc);
	printf("%s\n", p->msg);
	if (!p->flag) {
		printf("[RUN]\tAborting\n");
		exit(EXIT_FAILURE);
	}
}

void my_usr2(int sig, siginfo_t *si, void *u)
{
	char *aa;
	struct stk_data *p;

	printf("[RUN]\tsignal USR2\n");
	aa = alloca(1024);
	/* dont run valgrind on this */
	/* try to find the data stored by previous sighandler */
	p = memmem(aa, 1024, msg, strlen(msg));
	if (p) {
		printf("[FAIL]\tsigaltstack re-used\n");
		/* corrupt the data */
		strcpy(p->msg, msg2);
		/* tell other sighandler that his data is corrupted */
		p->flag = 0;
	}
}

static void switch_fn(void)
{
	printf("[RUN]\tswitched to user ctx\n");
	raise(SIGUSR2);
	setcontext(&sc);
}

int main(void)
{
	struct sigaction act;
	stack_t stk;
	int err;

	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_ONSTACK | SA_SIGINFO;
	act.sa_sigaction = my_usr1;
	sigaction(SIGUSR1, &act, NULL);
	act.sa_sigaction = my_usr2;
	sigaction(SIGUSR2, &act, NULL);
	sstack = mmap(NULL, SIGSTKSZ, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
	if (sstack == MAP_FAILED) {
		perror("mmap()");
		return EXIT_FAILURE;
	}

	err = sigaltstack(NULL, &stk);
	if (err) {
		perror("[FAIL]\tsigaltstack()");
		exit(EXIT_FAILURE);
	}
	if (stk.ss_flags == SS_DISABLE) {
		printf("[OK]\tInitial sigaltstack state was SS_DISABLE\n");
	} else {
		printf("[FAIL]\tInitial sigaltstack state was %x; "
		       "should have been SS_DISABLE\n", stk.ss_flags);
		return EXIT_FAILURE;
	}

	stk.ss_sp = sstack;
	stk.ss_size = SIGSTKSZ;
	stk.ss_flags = SS_ONSTACK | SS_AUTODISARM;
	err = sigaltstack(&stk, NULL);
	if (err) {
		if (errno == EINVAL) {
			printf("[NOTE]\tThe running kernel doesn't support SS_AUTODISARM\n");
			/*
			 * If test cases for the !SS_AUTODISARM variant were
			 * added, we could still run them.  We don't have any
			 * test cases like that yet, so just exit and report
			 * success.
			 */
			return 0;
		} else {
			perror("[FAIL]\tsigaltstack(SS_ONSTACK | SS_AUTODISARM)");
			return EXIT_FAILURE;
		}
	}

	ustack = mmap(NULL, SIGSTKSZ, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
	if (ustack == MAP_FAILED) {
		perror("mmap()");
		return EXIT_FAILURE;
	}
	getcontext(&uc);
	uc.uc_link = NULL;
	uc.uc_stack.ss_sp = ustack;
	uc.uc_stack.ss_size = SIGSTKSZ;
	makecontext(&uc, switch_fn, 0);
	raise(SIGUSR1);

	err = sigaltstack(NULL, &stk);
	if (err) {
		perror("[FAIL]\tsigaltstack()");
		exit(EXIT_FAILURE);
	}
	if (stk.ss_flags != SS_AUTODISARM) {
		printf("[FAIL]\tss_flags=%x, should be SS_AUTODISARM\n",
				stk.ss_flags);
		exit(EXIT_FAILURE);
	}
	printf("[OK]\tsigaltstack is still SS_AUTODISARM after signal\n");

	printf("[OK]\tTest passed\n");
	return 0;
}
