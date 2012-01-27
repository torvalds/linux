/*
 * linux/kernel/seccomp.c
 *
 * Copyright 2004-2005  Andrea Arcangeli <andrea@cpushare.com>
 *
 * This defines a simple but solid secure-computing mode.
 */

#include <linux/audit.h>
#include <linux/seccomp.h>
#include <linux/sched.h>
#include <linux/compat.h>

/* #define SECCOMP_DEBUG 1 */
#define NR_SECCOMP_MODES 1

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

void __secure_computing(int this_syscall)
{
	int mode = current->seccomp.mode;
	int * syscall;

	switch (mode) {
	case 1:
		syscall = mode1_syscalls;
#ifdef CONFIG_COMPAT
		if (is_compat_task())
			syscall = mode1_syscalls_32;
#endif
		do {
			if (*syscall == this_syscall)
				return;
		} while (*++syscall);
		break;
	default:
		BUG();
	}

#ifdef SECCOMP_DEBUG
	dump_stack();
#endif
	audit_seccomp(this_syscall);
	do_exit(SIGKILL);
}

long prctl_get_seccomp(void)
{
	return current->seccomp.mode;
}

long prctl_set_seccomp(unsigned long seccomp_mode)
{
	long ret;

	/* can set it only once to be even more secure */
	ret = -EPERM;
	if (unlikely(current->seccomp.mode))
		goto out;

	ret = -EINVAL;
	if (seccomp_mode && seccomp_mode <= NR_SECCOMP_MODES) {
		current->seccomp.mode = seccomp_mode;
		set_thread_flag(TIF_SECCOMP);
#ifdef TIF_NOTSC
		disable_TSC();
#endif
		ret = 0;
	}

 out:
	return ret;
}
