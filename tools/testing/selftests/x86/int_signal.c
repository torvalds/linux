// SPDX-License-Identifier: GPL-2.0-only
/* Check the signal context for INT instructions with IDT and FRED entry. */
#define _GNU_SOURCE

#include <cpuid.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ucontext.h>

#include "helpers.h"

#ifdef __x86_64__
#define REG_IP REG_RIP
#define USER_IP rip
#define STACK_PTR "%rsp"
#else
#define REG_IP REG_EIP
#define USER_IP eip
#define STACK_PTR "%esp"
#endif

/*
 * Each instruction has normal and single-step entry points. Resume at the
 * NOP after handling its signal, then expect a trace trap after that NOP
 * when TF is set. Explicit labels avoid assuming the kernel's saved IP.
 */
#define PROBE(name, insn) \
	extern void name(void); \
	extern void name##_tf(void); \
	extern const char name##_end[], name##_step[]; \
	asm(".pushsection .text\n" \
	    ".globl " #name "_tf\n" \
	    ".type " #name "_tf, @function\n" \
	    #name "_tf:\n" \
	    "pushf\n" \
	    "orl $0x100, (" STACK_PTR ")\n" \
	    "popf\n" \
	    ".globl " #name "\n" \
	    ".type " #name ", @function\n" \
	    #name ":\n" insn "\n" \
	    ".globl " #name "_end\n" \
	    #name "_end:\nnop\n" \
	    ".globl " #name "_step\n" \
	    #name "_step:\nret\n" \
	    ".size " #name ", .-" #name "\n" \
	    ".size " #name "_tf, .-" #name "_tf\n" \
	    ".popsection\n")

PROBE(int1, ".byte 0xcd, 0x01");
PROBE(int29, ".byte 0xcd, 0x29");
PROBE(int2c, ".byte 0xcd, 0x2c");
PROBE(int2d, ".byte 0xcd, 0x2d");
PROBE(prefixed_int2d, ".byte 0x66, 0xcd, 0x2d");
PROBE(long_int2d, ".fill 13, 1, 0x2e\n.byte 0xcd, 0x2d");
PROBE(int81, ".byte 0xcd, 0x81");
PROBE(intff, ".byte 0xcd, 0xff");
PROBE(short_int3, ".byte 0xcc");
PROBE(long_int3, ".byte 0xcd, 0x03");
PROBE(int4, ".byte 0xcd, 0x04");
PROBE(ud2, ".byte 0x0f, 0x0b");
PROBE(hlt, ".byte 0xf4");

struct test {
	const char *name;
	void (*run)(void);
	void (*run_tf)(void);
	const char *end, *step;
	int signo, trap, error, ip_offset, flags, code;
};

#define TEST(name, sig, trap, error, offset, flags, code) \
	{ #name, name, name##_tf, name##_end, name##_step, \
	  sig, trap, error, offset, flags, code }

#define GP(name, error) \
	TEST(name, SIGSEGV, 13, error, 0, X86_EFLAGS_RF, SI_KERNEL)

static const struct test tests[] = {
	GP(int1, 0x00a),
	GP(int29, 0x14a),
	GP(int2c, 0x162),
	GP(int2d, 0x16a),
	GP(prefixed_int2d, 0x16a),
	GP(long_int2d, 0x16a),
	GP(int81, 0x40a),
	GP(intff, 0x7fa),
	GP(hlt, 0),
	TEST(short_int3, SIGTRAP, 3, 0, 1, 0, SI_KERNEL),
	TEST(long_int3, SIGTRAP, 3, 0, 2, 0, SI_KERNEL),
	TEST(int4, SIGSEGV, 4, 0, 2, 0, SI_KERNEL),
	TEST(ud2, SIGILL, 6, 0, 0, X86_EFLAGS_RF, ILL_ILLOPN),
};

static const struct test *active;
static volatile sig_atomic_t seen, signo, trap, error, ip_offset, flags;
static volatile sig_atomic_t code, addr_ok, single_step, stepped, step_ok;

static void handler(int sig, siginfo_t *info, void *context)
{
	ucontext_t *uc = context;
	uintptr_t ip = uc->uc_mcontext.gregs[REG_IP];
	uintptr_t start = (uintptr_t)active->run;
	uintptr_t end = (uintptr_t)active->end;

	if (seen && single_step && sig == SIGTRAP) {
		if (stepped++) {
			ksft_print_msg("%s: second trace trap at %#lx\n",
				       active->name, (unsigned long)ip);
			_exit(KSFT_FAIL);
		}
		step_ok = ip == (uintptr_t)active->step &&
			uc->uc_mcontext.gregs[REG_TRAPNO] == 1 &&
			info->si_code == TRAP_TRACE;
		uc->uc_mcontext.gregs[REG_EFL] &= ~X86_EFLAGS_TF;
		return;
	}

	if (seen || ip < start || ip > end) {
		ksft_print_msg("%s: unexpected signal %d at %#lx\n",
			       active->name, sig, (unsigned long)ip);
		_exit(KSFT_FAIL);
	}

	signo = sig;
	trap = uc->uc_mcontext.gregs[REG_TRAPNO];
	error = uc->uc_mcontext.gregs[REG_ERR];
	ip_offset = ip - start;
	flags = uc->uc_mcontext.gregs[REG_EFL] & (X86_EFLAGS_RF | X86_EFLAGS_TF);
	code = info->si_code;
	/* force_sig() reports no address, force_sig_fault() reports the IP. */
	addr_ok = info->si_addr == (code == SI_KERNEL ? NULL : (void *)ip);
	seen = 1;
	uc->uc_mcontext.gregs[REG_IP] = end;
}

static void wait_for_child(pid_t child, int *status)
{
	pid_t ret;

	do {
		ret = waitpid(child, status, 0);
	} while (ret < 0 && errno == EINTR);
	if (ret != child)
		ksft_exit_fail_perror("waitpid");
}

/* Resume the tracee and check where the next stop lands. */
static bool resume_to(pid_t child, int *status, int request, int sig,
		      const void *ip, const char *what)
{
	struct user_regs_struct regs;

	if (ptrace(request, child, 0, 0))
		return false;
	wait_for_child(child, status);
	if (!WIFSTOPPED(*status)) {
		ksft_print_msg("%s: tracee did not stop\n", what);
		return false;
	}
	if (WSTOPSIG(*status) != sig) {
		ksft_print_msg("%s: stopped with signal %d, expected %d\n",
			       what, WSTOPSIG(*status), sig);
		return false;
	}
	if (ptrace(PTRACE_GETREGS, child, 0, &regs))
		return false;
	if ((unsigned long)regs.USER_IP != (unsigned long)ip) {
		ksft_print_msg("%s: stopped at %#lx, expected %#lx\n", what,
			       (unsigned long)regs.USER_IP, (unsigned long)ip);
		return false;
	}
	return true;
}

static bool set_ip(pid_t child, const void *ip, bool tf)
{
	struct user_regs_struct regs;

	if (ptrace(PTRACE_GETREGS, child, 0, &regs))
		return false;
	regs.USER_IP = (unsigned long)ip;
	if (tf)
		regs.eflags |= X86_EFLAGS_TF;
	return !ptrace(PTRACE_SETREGS, child, 0, &regs);
}

/*
 * Exercise the tracer paths that resume through the fault frame rather than
 * sigreturn. A stale FRED software event flag on that frame traps before the
 * NOP executes instead of after it.
 */
static void test_ptrace(void)
{
	bool into = false, step = false, cont = false;
	pid_t child;
	int status;

	child = fork();
	if (child < 0)
		ksft_exit_fail_perror("fork");
	if (!child) {
		if (ptrace(PTRACE_TRACEME, 0, 0, 0))
			_exit(KSFT_FAIL);
		/* Start from a breakpoint frame, not the syscall frame of raise(). */
		asm volatile("int3");
		_exit(KSFT_FAIL);
	}

	wait_for_child(child, &status);
	if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGTRAP)
		goto out;
	if (ptrace(PTRACE_SETOPTIONS, child, 0, PTRACE_O_EXITKILL))
		goto out;

	/* Single-step into the INT. The fault must report the INT's address. */
	if (!set_ip(child, int2d, false))
		goto out;
	into = resume_to(child, &status, PTRACE_SINGLESTEP, SIGSEGV, int2d,
			 "single-step into INT");
	if (!into)
		goto out;

	/* Suppress SIGSEGV and single-step the NOP. */
	if (!set_ip(child, int2d_end, false))
		goto out;
	step = resume_to(child, &status, PTRACE_SINGLESTEP, SIGTRAP, int2d_step,
			 "single-step after INT");
	if (!step)
		goto out;

	/* Fault again, then suppress SIGSEGV and continue with TF set. */
	if (!set_ip(child, int2d, false))
		goto out;
	if (!resume_to(child, &status, PTRACE_CONT, SIGSEGV, int2d,
		       "continue to INT"))
		goto out;
	if (!set_ip(child, int2d_end, true))
		goto out;
	cont = resume_to(child, &status, PTRACE_CONT, SIGTRAP, int2d_step,
			 "continue with TF after INT");
out:
	if (WIFSTOPPED(status)) {
		kill(child, SIGKILL);
		wait_for_child(child, &status);
	}
	ksft_test_result(into, "ptrace single-step into INT faults at the INT\n");
	ksft_test_result(step, "ptrace single-step after suppressing SIGSEGV\n");
	ksft_test_result(cont, "ptrace continue with TF after suppressing SIGSEGV\n");
}

static bool cpu_has_fred(void)
{
	unsigned int eax, ebx, ecx, edx;

	if (__get_cpuid_max(0, NULL) < 7)
		return false;
	__cpuid_count(7, 1, eax, ebx, ecx, edx);
	return eax & (1 << 17);
}

int main(void)
{
	unsigned int i, tf;
	int expected_flags, ok;

	ksft_print_header();
	ksft_set_plan(2 * ARRAY_SIZE(tests) + 3);
	ksft_print_msg("CPU %s FRED\n", cpu_has_fred() ? "supports" : "lacks");
	sethandler(SIGSEGV, handler, 0);
	sethandler(SIGTRAP, handler, 0);
	sethandler(SIGILL, handler, 0);

	for (tf = 0; tf < 2; tf++) {
		for (i = 0; i < ARRAY_SIZE(tests); i++) {
			active = &tests[i];
			single_step = tf;
			seen = signo = trap = error = ip_offset = flags = 0;
			code = addr_ok = stepped = step_ok = 0;
			expected_flags = active->flags | (tf ? X86_EFLAGS_TF : 0);
			if (tf)
				active->run_tf();
			else
				active->run();

			ok = seen && signo == active->signo && trap == active->trap &&
				error == active->error && ip_offset == active->ip_offset &&
				flags == expected_flags && code == active->code && addr_ok &&
				(!tf || (stepped && step_ok));
			ksft_test_result(ok, "%s%s\n", active->name, tf ? " with TF" : "");
			if (!ok) {
				ksft_print_msg("got signal=%d trap=%d error=%#x ip=%d\n",
					       signo, trap, error, ip_offset);
				ksft_print_msg("got flags=%#x code=%d addr_ok=%d step_ok=%d\n",
					       flags, code, addr_ok, step_ok);
				ksft_print_msg("expected signal=%d trap=%d error=%#x ip=%d\n",
					       active->signo, active->trap, active->error,
					       active->ip_offset);
				ksft_print_msg("expected flags=%#x code=%d\n",
					       expected_flags, active->code);
			}
		}
	}
	test_ptrace();
	ksft_finished();
}
