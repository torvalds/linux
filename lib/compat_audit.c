// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/types.h>
#include <asm/unistd32.h>

unsigned compat_dir_class[] = {
#include <asm-generic/audit_dir_write.h>
~0U
};

unsigned compat_read_class[] = {
#include <asm-generic/audit_read.h>
~0U
};

unsigned compat_write_class[] = {
#include <asm-generic/audit_write.h>
~0U
};

unsigned compat_chattr_class[] = {
#include <asm-generic/audit_change_attr.h>
~0U
};

unsigned compat_signal_class[] = {
#include <asm-generic/audit_signal.h>
~0U
};

int audit_classify_compat_syscall(int abi, unsigned syscall)
{
	switch (syscall) {
#ifdef __NR_open
	case __NR_open:
		return 2;
#endif
#ifdef __NR_openat
	case __NR_openat:
		return 3;
#endif
#ifdef __NR_socketcall
	case __NR_socketcall:
		return 4;
#endif
	case __NR_execve:
		return 5;
	default:
		return 1;
	}
}
