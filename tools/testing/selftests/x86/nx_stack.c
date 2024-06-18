/*
 * Copyright (c) 2023 Alexey Dobriyan <adobriyan@gmail.com>
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
/*
 * Test that userspace stack is NX. Requires linking with -Wl,-z,noexecstack
 * because I don't want to bother with PT_GNU_STACK detection.
 *
 * Fill the stack with INT3's and then try to execute some of them:
 * SIGSEGV -- good, SIGTRAP -- bad.
 *
 * Regular stack is completely overwritten before testing.
 * Test doesn't exit SIGSEGV handler after first fault at INT3.
 */
#undef _GNU_SOURCE
#define _GNU_SOURCE
#undef NDEBUG
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#define PAGE_SIZE 4096

/*
 * This is memset(rsp, 0xcc, -1); but down.
 * It will SIGSEGV when bottom of the stack is reached.
 * Byte-size access is important! (see rdi tweak in the signal handler).
 */
void make_stack1(void);
asm(
".pushsection .text\n"
".globl make_stack1\n"
".align 16\n"
"make_stack1:\n"
	"mov $0xcc, %al\n"
#if defined __amd64__
	"mov %rsp, %rdi\n"
	"mov $-1, %rcx\n"
#elif defined __i386__
	"mov %esp, %edi\n"
	"mov $-1, %ecx\n"
#else
#error
#endif
	"std\n"
	"rep stosb\n"
	/* unreachable */
	"hlt\n"
".type make_stack1,@function\n"
".size make_stack1,.-make_stack1\n"
".popsection\n"
);

/*
 * memset(p, 0xcc, -1);
 * It will SIGSEGV when top of the stack is reached.
 */
void make_stack2(uint64_t p);
asm(
".pushsection .text\n"
".globl make_stack2\n"
".align 16\n"
"make_stack2:\n"
	"mov $0xcc, %al\n"
#if defined __amd64__
	"mov $-1, %rcx\n"
#elif defined __i386__
	"mov $-1, %ecx\n"
#else
#error
#endif
	"cld\n"
	"rep stosb\n"
	/* unreachable */
	"hlt\n"
".type make_stack2,@function\n"
".size make_stack2,.-make_stack2\n"
".popsection\n"
);

static volatile int test_state = 0;
static volatile unsigned long stack_min_addr;

#if defined __amd64__
#define RDI	REG_RDI
#define RIP	REG_RIP
#define RIP_STRING "rip"
#elif defined __i386__
#define RDI	REG_EDI
#define RIP	REG_EIP
#define RIP_STRING "eip"
#else
#error
#endif

static void sigsegv(int _, siginfo_t *__, void *uc_)
{
	/*
	 * Some Linux versions didn't clear DF before entering signal
	 * handler. make_stack1() doesn't have a chance to clear DF
	 * either so we clear it by hand here.
	 */
	asm volatile ("cld" ::: "memory");

	ucontext_t *uc = uc_;

	if (test_state == 0) {
		/* Stack is faulted and cleared from RSP to the lowest address. */
		stack_min_addr = ++uc->uc_mcontext.gregs[RDI];
		if (1) {
			printf("stack min %lx\n", stack_min_addr);
		}
		uc->uc_mcontext.gregs[RIP] = (uintptr_t)&make_stack2;
		test_state = 1;
	} else if (test_state == 1) {
		/* Stack has been cleared from top to bottom. */
		unsigned long stack_max_addr = uc->uc_mcontext.gregs[RDI];
		if (1) {
			printf("stack max %lx\n", stack_max_addr);
		}
		/* Start faulting pages on stack and see what happens. */
		uc->uc_mcontext.gregs[RIP] = stack_max_addr - PAGE_SIZE;
		test_state = 2;
	} else if (test_state == 2) {
		/* Stack page is NX -- good, test next page. */
		uc->uc_mcontext.gregs[RIP] -= PAGE_SIZE;
		if (uc->uc_mcontext.gregs[RIP] == stack_min_addr) {
			/* One more SIGSEGV and test ends. */
			test_state = 3;
		}
	} else {
		printf("PASS\tAll stack pages are NX\n");
		_exit(EXIT_SUCCESS);
	}
}

static void sigtrap(int _, siginfo_t *__, void *uc_)
{
	const ucontext_t *uc = uc_;
	unsigned long rip = uc->uc_mcontext.gregs[RIP];
	printf("FAIL\texecutable page on the stack: " RIP_STRING " %lx\n", rip);
	_exit(EXIT_FAILURE);
}

int main(void)
{
	{
		struct sigaction act = {};
		sigemptyset(&act.sa_mask);
		act.sa_flags = SA_SIGINFO;
		act.sa_sigaction = &sigsegv;
		int rv = sigaction(SIGSEGV, &act, NULL);
		assert(rv == 0);
	}
	{
		struct sigaction act = {};
		sigemptyset(&act.sa_mask);
		act.sa_flags = SA_SIGINFO;
		act.sa_sigaction = &sigtrap;
		int rv = sigaction(SIGTRAP, &act, NULL);
		assert(rv == 0);
	}
	{
		struct rlimit rlim;
		int rv = getrlimit(RLIMIT_STACK, &rlim);
		assert(rv == 0);
		/* Cap stack at time-honored 8 MiB value. */
		rlim.rlim_max = rlim.rlim_cur;
		if (rlim.rlim_max > 8 * 1024 * 1024) {
			rlim.rlim_max = 8 * 1024 * 1024;
		}
		rv = setrlimit(RLIMIT_STACK, &rlim);
		assert(rv == 0);
	}
	{
		/*
		 * We don't know now much stack SIGSEGV handler uses.
		 * Bump this by 1 page every time someone complains,
		 * or rewrite it in assembly.
		 */
		const size_t len = SIGSTKSZ;
		void *p = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
		assert(p != MAP_FAILED);
		stack_t ss = {};
		ss.ss_sp = p;
		ss.ss_size = len;
		int rv = sigaltstack(&ss, NULL);
		assert(rv == 0);
	}
	make_stack1();
	/*
	 * Unreachable, but if _this_ INT3 is ever reached, it's a bug somewhere.
	 * Fold it into main SIGTRAP pathway.
	 */
	__builtin_trap();
}
