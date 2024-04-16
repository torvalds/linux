// SPDX-License-Identifier: GPL-2.0-only
/*
 * 32-bit syscall ABI conformance test.
 *
 * Copyright (c) 2015 Denys Vlasenko
 */
/*
 * Can be built statically:
 * gcc -Os -Wall -static -m32 test_syscall_vdso.c thunks_32.S
 */
#undef _GNU_SOURCE
#define _GNU_SOURCE 1
#undef __USE_GNU
#define __USE_GNU 1
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <elf.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#if !defined(__i386__)
int main(int argc, char **argv, char **envp)
{
	printf("[SKIP]\tNot a 32-bit x86 userspace\n");
	return 0;
}
#else

long syscall_addr;
long get_syscall(char **envp)
{
	Elf32_auxv_t *auxv;
	while (*envp++ != NULL)
		continue;
	for (auxv = (void *)envp; auxv->a_type != AT_NULL; auxv++)
		if (auxv->a_type == AT_SYSINFO)
			return auxv->a_un.a_val;
	printf("[WARN]\tAT_SYSINFO not supplied\n");
	return 0;
}

asm (
	"	.pushsection .text\n"
	"	.global	int80\n"
	"int80:\n"
	"	int	$0x80\n"
	"	ret\n"
	"	.popsection\n"
);
extern char int80;

struct regs64 {
	uint64_t rax, rbx, rcx, rdx;
	uint64_t rsi, rdi, rbp, rsp;
	uint64_t r8,  r9,  r10, r11;
	uint64_t r12, r13, r14, r15;
};
struct regs64 regs64;
int kernel_is_64bit;

asm (
	"	.pushsection .text\n"
	"	.code64\n"
	"get_regs64:\n"
	"	push	%rax\n"
	"	mov	$regs64, %eax\n"
	"	pop	0*8(%rax)\n"
	"	movq	%rbx, 1*8(%rax)\n"
	"	movq	%rcx, 2*8(%rax)\n"
	"	movq	%rdx, 3*8(%rax)\n"
	"	movq	%rsi, 4*8(%rax)\n"
	"	movq	%rdi, 5*8(%rax)\n"
	"	movq	%rbp, 6*8(%rax)\n"
	"	movq	%rsp, 7*8(%rax)\n"
	"	movq	%r8,  8*8(%rax)\n"
	"	movq	%r9,  9*8(%rax)\n"
	"	movq	%r10, 10*8(%rax)\n"
	"	movq	%r11, 11*8(%rax)\n"
	"	movq	%r12, 12*8(%rax)\n"
	"	movq	%r13, 13*8(%rax)\n"
	"	movq	%r14, 14*8(%rax)\n"
	"	movq	%r15, 15*8(%rax)\n"
	"	ret\n"
	"poison_regs64:\n"
	"	movq	$0x7f7f7f7f, %r8\n"
	"	shl	$32, %r8\n"
	"	orq	$0x7f7f7f7f, %r8\n"
	"	movq	%r8, %r9\n"
	"	incq	%r9\n"
	"	movq	%r9, %r10\n"
	"	incq	%r10\n"
	"	movq	%r10, %r11\n"
	"	incq	%r11\n"
	"	movq	%r11, %r12\n"
	"	incq	%r12\n"
	"	movq	%r12, %r13\n"
	"	incq	%r13\n"
	"	movq	%r13, %r14\n"
	"	incq	%r14\n"
	"	movq	%r14, %r15\n"
	"	incq	%r15\n"
	"	ret\n"
	"	.code32\n"
	"	.popsection\n"
);
extern void get_regs64(void);
extern void poison_regs64(void);
extern unsigned long call64_from_32(void (*function)(void));
void print_regs64(void)
{
	if (!kernel_is_64bit)
		return;
	printf("ax:%016llx bx:%016llx cx:%016llx dx:%016llx\n", regs64.rax,  regs64.rbx,  regs64.rcx,  regs64.rdx);
	printf("si:%016llx di:%016llx bp:%016llx sp:%016llx\n", regs64.rsi,  regs64.rdi,  regs64.rbp,  regs64.rsp);
	printf(" 8:%016llx  9:%016llx 10:%016llx 11:%016llx\n", regs64.r8 ,  regs64.r9 ,  regs64.r10,  regs64.r11);
	printf("12:%016llx 13:%016llx 14:%016llx 15:%016llx\n", regs64.r12,  regs64.r13,  regs64.r14,  regs64.r15);
}

int check_regs64(void)
{
	int err = 0;
	int num = 8;
	uint64_t *r64 = &regs64.r8;
	uint64_t expected = 0x7f7f7f7f7f7f7f7fULL;

	if (!kernel_is_64bit)
		return 0;

	do {
		if (*r64 == expected++)
			continue; /* register did not change */
		if (syscall_addr != (long)&int80) {
			/*
			 * Non-INT80 syscall entrypoints are allowed to clobber R8+ regs:
			 * either clear them to 0, or for R11, load EFLAGS.
			 */
			if (*r64 == 0)
				continue;
			if (num == 11) {
				printf("[NOTE]\tR11 has changed:%016llx - assuming clobbered by SYSRET insn\n", *r64);
				continue;
			}
		} else {
			/*
			 * INT80 syscall entrypoint can be used by
			 * 64-bit programs too, unlike SYSCALL/SYSENTER.
			 * Therefore it must preserve R12+
			 * (they are callee-saved registers in 64-bit C ABI).
			 *
			 * Starting in Linux 4.17 (and any kernel that
			 * backports the change), R8..11 are preserved.
			 * Historically (and probably unintentionally), they
			 * were clobbered or zeroed.
			 */
		}
		printf("[FAIL]\tR%d has changed:%016llx\n", num, *r64);
		err++;
	} while (r64++, ++num < 16);

	if (!err)
		printf("[OK]\tR8..R15 did not leak kernel data\n");
	return err;
}

int nfds;
fd_set rfds;
fd_set wfds;
fd_set efds;
struct timespec timeout;
sigset_t sigmask;
struct {
	sigset_t *sp;
	int sz;
} sigmask_desc;

void prep_args()
{
	nfds = 42;
	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	FD_ZERO(&efds);
	FD_SET(0, &rfds);
	FD_SET(1, &wfds);
	FD_SET(2, &efds);
	timeout.tv_sec = 0;
	timeout.tv_nsec = 123;
	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGINT);
	sigaddset(&sigmask, SIGUSR2);
	sigaddset(&sigmask, SIGRTMAX);
	sigmask_desc.sp = &sigmask;
	sigmask_desc.sz = 8; /* bytes */
}

static void print_flags(const char *name, unsigned long r)
{
	static const char *bitarray[] = {
	"\n" ,"c\n" ,/* Carry Flag */
	"0 " ,"1 "  ,/* Bit 1 - always on */
	""   ,"p "  ,/* Parity Flag */
	"0 " ,"3? " ,
	""   ,"a "  ,/* Auxiliary carry Flag */
	"0 " ,"5? " ,
	""   ,"z "  ,/* Zero Flag */
	""   ,"s "  ,/* Sign Flag */
	""   ,"t "  ,/* Trap Flag */
	""   ,"i "  ,/* Interrupt Flag */
	""   ,"d "  ,/* Direction Flag */
	""   ,"o "  ,/* Overflow Flag */
	"0 " ,"1 "  ,/* I/O Privilege Level (2 bits) */
	"0"  ,"1"   ,/* I/O Privilege Level (2 bits) */
	""   ,"n "  ,/* Nested Task */
	"0 " ,"15? ",
	""   ,"r "  ,/* Resume Flag */
	""   ,"v "  ,/* Virtual Mode */
	""   ,"ac " ,/* Alignment Check/Access Control */
	""   ,"vif ",/* Virtual Interrupt Flag */
	""   ,"vip ",/* Virtual Interrupt Pending */
	""   ,"id " ,/* CPUID detection */
	NULL
	};
	const char **bitstr;
	int bit;

	printf("%s=%016lx ", name, r);
	bitstr = bitarray + 42;
	bit = 21;
	if ((r >> 22) != 0)
		printf("(extra bits are set) ");
	do {
		if (bitstr[(r >> bit) & 1][0])
			fputs(bitstr[(r >> bit) & 1], stdout);
		bitstr -= 2;
		bit--;
	} while (bit >= 0);
}

int run_syscall(void)
{
	long flags, bad_arg;

	prep_args();

	if (kernel_is_64bit)
		call64_from_32(poison_regs64);
	/*print_regs64();*/

	asm("\n"
	/* Try 6-arg syscall: pselect. It should return quickly */
	"	push	%%ebp\n"
	"	mov	$308, %%eax\n"     /* PSELECT */
	"	mov	nfds, %%ebx\n"     /* ebx  arg1 */
	"	mov	$rfds, %%ecx\n"    /* ecx  arg2 */
	"	mov	$wfds, %%edx\n"    /* edx  arg3 */
	"	mov	$efds, %%esi\n"    /* esi  arg4 */
	"	mov	$timeout, %%edi\n" /* edi  arg5 */
	"	mov	$sigmask_desc, %%ebp\n" /* %ebp arg6 */
	"	push	$0x200ed7\n"      /* set almost all flags */
	"	popf\n"		/* except TF, IOPL, NT, RF, VM, AC, VIF, VIP */
	"	call	*syscall_addr\n"
	/* Check that registers are not clobbered */
	"	pushf\n"
	"	pop	%%eax\n"
	"	cld\n"
	"	cmp	nfds, %%ebx\n"     /* ebx  arg1 */
	"	mov	$1, %%ebx\n"
	"	jne	1f\n"
	"	cmp	$rfds, %%ecx\n"    /* ecx  arg2 */
	"	mov	$2, %%ebx\n"
	"	jne	1f\n"
	"	cmp	$wfds, %%edx\n"    /* edx  arg3 */
	"	mov	$3, %%ebx\n"
	"	jne	1f\n"
	"	cmp	$efds, %%esi\n"    /* esi  arg4 */
	"	mov	$4, %%ebx\n"
	"	jne	1f\n"
	"	cmp	$timeout, %%edi\n" /* edi  arg5 */
	"	mov	$5, %%ebx\n"
	"	jne	1f\n"
	"	cmpl	$sigmask_desc, %%ebp\n" /* %ebp arg6 */
	"	mov	$6, %%ebx\n"
	"	jne	1f\n"
	"	mov	$0, %%ebx\n"
	"1:\n"
	"	pop	%%ebp\n"
	: "=a" (flags), "=b" (bad_arg)
	:
	: "cx", "dx", "si", "di"
	);

	if (kernel_is_64bit) {
		memset(&regs64, 0x77, sizeof(regs64));
		call64_from_32(get_regs64);
		/*print_regs64();*/
	}

	/*
	 * On paravirt kernels, flags are not preserved across syscalls.
	 * Thus, we do not consider it a bug if some are changed.
	 * We just show ones which do.
	 */
	if ((0x200ed7 ^ flags) != 0) {
		print_flags("[WARN]\tFlags before", 0x200ed7);
		print_flags("[WARN]\tFlags  after", flags);
		print_flags("[WARN]\tFlags change", (0x200ed7 ^ flags));
	}

	if (bad_arg) {
		printf("[FAIL]\targ#%ld clobbered\n", bad_arg);
		return 1;
	}
	printf("[OK]\tArguments are preserved across syscall\n");

	return check_regs64();
}

int run_syscall_twice()
{
	int exitcode = 0;
	long sv;

	if (syscall_addr) {
		printf("[RUN]\tExecuting 6-argument 32-bit syscall via VDSO\n");
		exitcode = run_syscall();
	}
	sv = syscall_addr;
	syscall_addr = (long)&int80;
	printf("[RUN]\tExecuting 6-argument 32-bit syscall via INT 80\n");
	exitcode += run_syscall();
	syscall_addr = sv;
	return exitcode;
}

void ptrace_me()
{
	pid_t pid;

	fflush(NULL);
	pid = fork();
	if (pid < 0)
		exit(1);
	if (pid == 0) {
		/* child */
		if (ptrace(PTRACE_TRACEME, 0L, 0L, 0L) != 0)
			exit(0);
		raise(SIGSTOP);
		return;
	}
	/* parent */
	printf("[RUN]\tRunning tests under ptrace\n");
	while (1) {
		int status;
		pid = waitpid(-1, &status, __WALL);
		if (WIFEXITED(status))
			exit(WEXITSTATUS(status));
		if (WIFSIGNALED(status))
			exit(WTERMSIG(status));
		if (pid <= 0 || !WIFSTOPPED(status)) /* paranoia */
			exit(255);
		/*
		 * Note: we do not inject sig = WSTOPSIG(status).
		 * We probably should, but careful: do not inject SIGTRAP
		 * generated by syscall entry/exit stops.
		 * That kills the child.
		 */
		ptrace(PTRACE_SYSCALL, pid, 0L, 0L /*sig*/);
	}
}

int main(int argc, char **argv, char **envp)
{
	int exitcode = 0;
	int cs;

	asm("\n"
	"	movl	%%cs, %%eax\n"
	: "=a" (cs)
	);
	kernel_is_64bit = (cs == 0x23);
	if (!kernel_is_64bit)
		printf("[NOTE]\tNot a 64-bit kernel, won't test R8..R15 leaks\n");

	/* This only works for non-static builds:
	 * syscall_addr = dlsym(dlopen("linux-gate.so.1", RTLD_NOW), "__kernel_vsyscall");
	 */
	syscall_addr = get_syscall(envp);

	exitcode += run_syscall_twice();
	ptrace_me();
	exitcode += run_syscall_twice();

	return exitcode;
}
#endif
