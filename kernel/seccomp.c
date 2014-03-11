/*
 * linux/kernel/seccomp.c
 *
 * Copyright 2004-2005  Andrea Arcangeli <andrea@cpushare.com>
 *
 * Copyright (C) 2012 Google, Inc.
 * Will Drewry <wad@chromium.org>
 *
 * This defines a simple but solid secure-computing facility.
 *
 * Mode 1 uses a fixed list of allowed system calls.
 * Mode 2 allows user-defined system call filters in the form
 *        of Berkeley Packet Filters/Linux Socket Filters.
 */

#include <linux/atomic.h>
#include <linux/audit.h>
#include <linux/compat.h>
#include <linux/sched.h>
#include <linux/seccomp.h>

/* #define SECCOMP_DEBUG 1 */

#ifdef CONFIG_SECCOMP_FILTER
#include <asm/syscall.h>
#include <linux/filter.h>
#include <linux/ptrace.h>
#include <linux/security.h>
#include <linux/slab.h>
#include <linux/tracehook.h>
#include <linux/uaccess.h>

/**
 * struct seccomp_filter - container for seccomp BPF programs
 *
 * @usage: reference count to manage the object lifetime.
 *         get/put helpers should be used when accessing an instance
 *         outside of a lifetime-guarded section.  In general, this
 *         is only needed for handling filters shared across tasks.
 * @prev: points to a previously installed, or inherited, filter
 * @len: the number of instructions in the program
 * @insns: the BPF program instructions to evaluate
 *
 * seccomp_filter objects are organized in a tree linked via the @prev
 * pointer.  For any task, it appears to be a singly-linked list starting
 * with current->seccomp.filter, the most recently attached or inherited filter.
 * However, multiple filters may share a @prev node, by way of fork(), which
 * results in a unidirectional tree existing in memory.  This is similar to
 * how namespaces work.
 *
 * seccomp_filter objects should never be modified after being attached
 * to a task_struct (other than @usage).
 */
struct seccomp_filter {
	atomic_t usage;
	struct seccomp_filter *prev;
	unsigned short len;  /* Instruction count */
	struct sock_filter insns[];
};

/* Limit any path through the tree to 256KB worth of instructions. */
#define MAX_INSNS_PER_PATH ((1 << 18) / sizeof(struct sock_filter))

/**
 * get_u32 - returns a u32 offset into data
 * @data: a unsigned 64 bit value
 * @index: 0 or 1 to return the first or second 32-bits
 *
 * This inline exists to hide the length of unsigned long.  If a 32-bit
 * unsigned long is passed in, it will be extended and the top 32-bits will be
 * 0. If it is a 64-bit unsigned long, then whatever data is resident will be
 * properly returned.
 *
 * Endianness is explicitly ignored and left for BPF program authors to manage
 * as per the specific architecture.
 */
static inline u32 get_u32(u64 data, int index)
{
	return ((u32 *)&data)[index];
}

/* Helper for bpf_load below. */
#define BPF_DATA(_name) offsetof(struct seccomp_data, _name)
/**
 * bpf_load: checks and returns a pointer to the requested offset
 * @off: offset into struct seccomp_data to load from
 *
 * Returns the requested 32-bits of data.
 * seccomp_check_filter() should assure that @off is 32-bit aligned
 * and not out of bounds.  Failure to do so is a BUG.
 */
u32 seccomp_bpf_load(int off)
{
	struct pt_regs *regs = task_pt_regs(current);
	if (off == BPF_DATA(nr))
		return syscall_get_nr(current, regs);
	if (off == BPF_DATA(arch))
		return syscall_get_arch();
	if (off >= BPF_DATA(args[0]) && off < BPF_DATA(args[6])) {
		unsigned long value;
		int arg = (off - BPF_DATA(args[0])) / sizeof(u64);
		int index = !!(off % sizeof(u64));
		syscall_get_arguments(current, regs, arg, 1, &value);
		return get_u32(value, index);
	}
	if (off == BPF_DATA(instruction_pointer))
		return get_u32(KSTK_EIP(current), 0);
	if (off == BPF_DATA(instruction_pointer) + sizeof(u32))
		return get_u32(KSTK_EIP(current), 1);
	/* seccomp_check_filter should make this impossible. */
	BUG();
}

/**
 *	seccomp_check_filter - verify seccomp filter code
 *	@filter: filter to verify
 *	@flen: length of filter
 *
 * Takes a previously checked filter (by sk_chk_filter) and
 * redirects all filter code that loads struct sk_buff data
 * and related data through seccomp_bpf_load.  It also
 * enforces length and alignment checking of those loads.
 *
 * Returns 0 if the rule set is legal or -EINVAL if not.
 */
static int seccomp_check_filter(struct sock_filter *filter, unsigned int flen)
{
	int pc;
	for (pc = 0; pc < flen; pc++) {
		struct sock_filter *ftest = &filter[pc];
		u16 code = ftest->code;
		u32 k = ftest->k;

		switch (code) {
		case BPF_S_LD_W_ABS:
			ftest->code = BPF_S_ANC_SECCOMP_LD_W;
			/* 32-bit aligned and not out of bounds. */
			if (k >= sizeof(struct seccomp_data) || k & 3)
				return -EINVAL;
			continue;
		case BPF_S_LD_W_LEN:
			ftest->code = BPF_S_LD_IMM;
			ftest->k = sizeof(struct seccomp_data);
			continue;
		case BPF_S_LDX_W_LEN:
			ftest->code = BPF_S_LDX_IMM;
			ftest->k = sizeof(struct seccomp_data);
			continue;
		/* Explicitly include allowed calls. */
		case BPF_S_RET_K:
		case BPF_S_RET_A:
		case BPF_S_ALU_ADD_K:
		case BPF_S_ALU_ADD_X:
		case BPF_S_ALU_SUB_K:
		case BPF_S_ALU_SUB_X:
		case BPF_S_ALU_MUL_K:
		case BPF_S_ALU_MUL_X:
		case BPF_S_ALU_DIV_X:
		case BPF_S_ALU_AND_K:
		case BPF_S_ALU_AND_X:
		case BPF_S_ALU_OR_K:
		case BPF_S_ALU_OR_X:
		case BPF_S_ALU_XOR_K:
		case BPF_S_ALU_XOR_X:
		case BPF_S_ALU_LSH_K:
		case BPF_S_ALU_LSH_X:
		case BPF_S_ALU_RSH_K:
		case BPF_S_ALU_RSH_X:
		case BPF_S_ALU_NEG:
		case BPF_S_LD_IMM:
		case BPF_S_LDX_IMM:
		case BPF_S_MISC_TAX:
		case BPF_S_MISC_TXA:
		case BPF_S_ALU_DIV_K:
		case BPF_S_LD_MEM:
		case BPF_S_LDX_MEM:
		case BPF_S_ST:
		case BPF_S_STX:
		case BPF_S_JMP_JA:
		case BPF_S_JMP_JEQ_K:
		case BPF_S_JMP_JEQ_X:
		case BPF_S_JMP_JGE_K:
		case BPF_S_JMP_JGE_X:
		case BPF_S_JMP_JGT_K:
		case BPF_S_JMP_JGT_X:
		case BPF_S_JMP_JSET_K:
		case BPF_S_JMP_JSET_X:
			continue;
		default:
			return -EINVAL;
		}
	}
	return 0;
}

/**
 * seccomp_run_filters - evaluates all seccomp filters against @syscall
 * @syscall: number of the current system call
 *
 * Returns valid seccomp BPF response codes.
 */
static u32 seccomp_run_filters(int syscall)
{
	struct seccomp_filter *f;
	u32 ret = SECCOMP_RET_ALLOW;

	/* Ensure unexpected behavior doesn't result in failing open. */
	if (WARN_ON(current->seccomp.filter == NULL))
		return SECCOMP_RET_KILL;

	/*
	 * All filters in the list are evaluated and the lowest BPF return
	 * value always takes priority (ignoring the DATA).
	 */
	for (f = current->seccomp.filter; f; f = f->prev) {
		u32 cur_ret = sk_run_filter(NULL, f->insns);
		if ((cur_ret & SECCOMP_RET_ACTION) < (ret & SECCOMP_RET_ACTION))
			ret = cur_ret;
	}
	return ret;
}

/**
 * seccomp_attach_filter: Attaches a seccomp filter to current.
 * @fprog: BPF program to install
 *
 * Returns 0 on success or an errno on failure.
 */
static long seccomp_attach_filter(struct sock_fprog *fprog)
{
	struct seccomp_filter *filter;
	unsigned long fp_size = fprog->len * sizeof(struct sock_filter);
	unsigned long total_insns = fprog->len;
	long ret;

	if (fprog->len == 0 || fprog->len > BPF_MAXINSNS)
		return -EINVAL;

	for (filter = current->seccomp.filter; filter; filter = filter->prev)
		total_insns += filter->len + 4;  /* include a 4 instr penalty */
	if (total_insns > MAX_INSNS_PER_PATH)
		return -ENOMEM;

	/*
	 * Installing a seccomp filter requires that the task have
	 * CAP_SYS_ADMIN in its namespace or be running with no_new_privs.
	 * This avoids scenarios where unprivileged tasks can affect the
	 * behavior of privileged children.
	 */
	if (!current->no_new_privs &&
	    security_capable_noaudit(current_cred(), current_user_ns(),
				     CAP_SYS_ADMIN) != 0)
		return -EACCES;

	/* Allocate a new seccomp_filter */
	filter = kzalloc(sizeof(struct seccomp_filter) + fp_size,
			 GFP_KERNEL|__GFP_NOWARN);
	if (!filter)
		return -ENOMEM;
	atomic_set(&filter->usage, 1);
	filter->len = fprog->len;

	/* Copy the instructions from fprog. */
	ret = -EFAULT;
	if (copy_from_user(filter->insns, fprog->filter, fp_size))
		goto fail;

	/* Check and rewrite the fprog via the skb checker */
	ret = sk_chk_filter(filter->insns, filter->len);
	if (ret)
		goto fail;

	/* Check and rewrite the fprog for seccomp use */
	ret = seccomp_check_filter(filter->insns, filter->len);
	if (ret)
		goto fail;

	/*
	 * If there is an existing filter, make it the prev and don't drop its
	 * task reference.
	 */
	filter->prev = current->seccomp.filter;
	current->seccomp.filter = filter;
	return 0;
fail:
	kfree(filter);
	return ret;
}

/**
 * seccomp_attach_user_filter - attaches a user-supplied sock_fprog
 * @user_filter: pointer to the user data containing a sock_fprog.
 *
 * Returns 0 on success and non-zero otherwise.
 */
long seccomp_attach_user_filter(char __user *user_filter)
{
	struct sock_fprog fprog;
	long ret = -EFAULT;

#ifdef CONFIG_COMPAT
	if (is_compat_task()) {
		struct compat_sock_fprog fprog32;
		if (copy_from_user(&fprog32, user_filter, sizeof(fprog32)))
			goto out;
		fprog.len = fprog32.len;
		fprog.filter = compat_ptr(fprog32.filter);
	} else /* falls through to the if below. */
#endif
	if (copy_from_user(&fprog, user_filter, sizeof(fprog)))
		goto out;
	ret = seccomp_attach_filter(&fprog);
out:
	return ret;
}

/* get_seccomp_filter - increments the reference count of the filter on @tsk */
void get_seccomp_filter(struct task_struct *tsk)
{
	struct seccomp_filter *orig = tsk->seccomp.filter;
	if (!orig)
		return;
	/* Reference count is bounded by the number of total processes. */
	atomic_inc(&orig->usage);
}

/* put_seccomp_filter - decrements the ref count of tsk->seccomp.filter */
void put_seccomp_filter(struct task_struct *tsk)
{
	struct seccomp_filter *orig = tsk->seccomp.filter;
	/* Clean up single-reference branches iteratively. */
	while (orig && atomic_dec_and_test(&orig->usage)) {
		struct seccomp_filter *freeme = orig;
		orig = orig->prev;
		kfree(freeme);
	}
}

/**
 * seccomp_send_sigsys - signals the task to allow in-process syscall emulation
 * @syscall: syscall number to send to userland
 * @reason: filter-supplied reason code to send to userland (via si_errno)
 *
 * Forces a SIGSYS with a code of SYS_SECCOMP and related sigsys info.
 */
static void seccomp_send_sigsys(int syscall, int reason)
{
	struct siginfo info;
	memset(&info, 0, sizeof(info));
	info.si_signo = SIGSYS;
	info.si_code = SYS_SECCOMP;
	info.si_call_addr = (void __user *)KSTK_EIP(current);
	info.si_errno = reason;
	info.si_arch = syscall_get_arch();
	info.si_syscall = syscall;
	force_sig_info(SIGSYS, &info, current);
}
#endif	/* CONFIG_SECCOMP_FILTER */

/*
 * Secure computing mode 1 allows only read/write/exit/sigreturn.
 * To be fully secure this must be combined with rlimit
 * to limit the stack allocations too.
 */
static int mode1_syscalls[] = {
	__NR_seccomp_read, __NR_seccomp_write, __NR_seccomp_exit, __NR_seccomp_sigreturn,
	0, /* null terminated */
};

#ifdef CONFIG_COMPAT
static int mode1_syscalls_32[] = {
	__NR_seccomp_read_32, __NR_seccomp_write_32, __NR_seccomp_exit_32, __NR_seccomp_sigreturn_32,
	0, /* null terminated */
};
#endif

int __secure_computing(int this_syscall)
{
	int mode = current->seccomp.mode;
	int exit_sig = 0;
	int *syscall;
	u32 ret;

	switch (mode) {
	case SECCOMP_MODE_STRICT:
		syscall = mode1_syscalls;
#ifdef CONFIG_COMPAT
		if (is_compat_task())
			syscall = mode1_syscalls_32;
#endif
		do {
			if (*syscall == this_syscall)
				return 0;
		} while (*++syscall);
		exit_sig = SIGKILL;
		ret = SECCOMP_RET_KILL;
		break;
#ifdef CONFIG_SECCOMP_FILTER
	case SECCOMP_MODE_FILTER: {
		int data;
		struct pt_regs *regs = task_pt_regs(current);
		ret = seccomp_run_filters(this_syscall);
		data = ret & SECCOMP_RET_DATA;
		ret &= SECCOMP_RET_ACTION;
		switch (ret) {
		case SECCOMP_RET_ERRNO:
			/* Set the low-order 16-bits as a errno. */
			syscall_set_return_value(current, regs,
						 -data, 0);
			goto skip;
		case SECCOMP_RET_TRAP:
			/* Show the handler the original registers. */
			syscall_rollback(current, regs);
			/* Let the filter pass back 16 bits of data. */
			seccomp_send_sigsys(this_syscall, data);
			goto skip;
		case SECCOMP_RET_TRACE:
			/* Skip these calls if there is no tracer. */
			if (!ptrace_event_enabled(current, PTRACE_EVENT_SECCOMP)) {
				syscall_set_return_value(current, regs,
							 -ENOSYS, 0);
				goto skip;
			}
			/* Allow the BPF to provide the event message */
			ptrace_event(PTRACE_EVENT_SECCOMP, data);
			/*
			 * The delivery of a fatal signal during event
			 * notification may silently skip tracer notification.
			 * Terminating the task now avoids executing a system
			 * call that may not be intended.
			 */
			if (fatal_signal_pending(current))
				break;
			if (syscall_get_nr(current, regs) < 0)
				goto skip;  /* Explicit request to skip. */

			return 0;
		case SECCOMP_RET_ALLOW:
			return 0;
		case SECCOMP_RET_KILL:
		default:
			break;
		}
		exit_sig = SIGSYS;
		break;
	}
#endif
	default:
		BUG();
	}

#ifdef SECCOMP_DEBUG
	dump_stack();
#endif
	audit_seccomp(this_syscall, exit_sig, ret);
	do_exit(exit_sig);
#ifdef CONFIG_SECCOMP_FILTER
skip:
	audit_seccomp(this_syscall, exit_sig, ret);
#endif
	return -1;
}

long prctl_get_seccomp(void)
{
	return current->seccomp.mode;
}

/**
 * prctl_set_seccomp: configures current->seccomp.mode
 * @seccomp_mode: requested mode to use
 * @filter: optional struct sock_fprog for use with SECCOMP_MODE_FILTER
 *
 * This function may be called repeatedly with a @seccomp_mode of
 * SECCOMP_MODE_FILTER to install additional filters.  Every filter
 * successfully installed will be evaluated (in reverse order) for each system
 * call the task makes.
 *
 * Once current->seccomp.mode is non-zero, it may not be changed.
 *
 * Returns 0 on success or -EINVAL on failure.
 */
long prctl_set_seccomp(unsigned long seccomp_mode, char __user *filter)
{
	long ret = -EINVAL;

	if (current->seccomp.mode &&
	    current->seccomp.mode != seccomp_mode)
		goto out;

	switch (seccomp_mode) {
	case SECCOMP_MODE_STRICT:
		ret = 0;
#ifdef TIF_NOTSC
		disable_TSC();
#endif
		break;
#ifdef CONFIG_SECCOMP_FILTER
	case SECCOMP_MODE_FILTER:
		ret = seccomp_attach_user_filter(filter);
		if (ret)
			goto out;
		break;
#endif
	default:
		goto out;
	}

	current->seccomp.mode = seccomp_mode;
	set_thread_flag(TIF_SECCOMP);
out:
	return ret;
}
