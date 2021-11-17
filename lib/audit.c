// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/types.h>
#include <linux/audit.h>
#include <asm/unistd.h>

static unsigned dir_class[] = {
#include <asm-generic/audit_dir_write.h>
~0U
};

static unsigned read_class[] = {
#include <asm-generic/audit_read.h>
~0U
};

static unsigned write_class[] = {
#include <asm-generic/audit_write.h>
~0U
};

static unsigned chattr_class[] = {
#include <asm-generic/audit_change_attr.h>
~0U
};

static unsigned signal_class[] = {
#include <asm-generic/audit_signal.h>
~0U
};

int audit_classify_arch(int arch)
{
	if (audit_is_compat(arch))
		return 1;
	else
		return 0;
}

int audit_classify_syscall(int abi, unsigned syscall)
{
	if (audit_is_compat(abi))
		return audit_classify_compat_syscall(abi, syscall);

	switch(syscall) {
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
#ifdef __NR_execveat
	case __NR_execveat:
#endif
	case __NR_execve:
		return 5;
	default:
		return 0;
	}
}

static int __init audit_classes_init(void)
{
#ifdef CONFIG_AUDIT_COMPAT_GENERIC
	audit_register_class(AUDIT_CLASS_WRITE_32, compat_write_class);
	audit_register_class(AUDIT_CLASS_READ_32, compat_read_class);
	audit_register_class(AUDIT_CLASS_DIR_WRITE_32, compat_dir_class);
	audit_register_class(AUDIT_CLASS_CHATTR_32, compat_chattr_class);
	audit_register_class(AUDIT_CLASS_SIGNAL_32, compat_signal_class);
#endif
	audit_register_class(AUDIT_CLASS_WRITE, write_class);
	audit_register_class(AUDIT_CLASS_READ, read_class);
	audit_register_class(AUDIT_CLASS_DIR_WRITE, dir_class);
	audit_register_class(AUDIT_CLASS_CHATTR, chattr_class);
	audit_register_class(AUDIT_CLASS_SIGNAL, signal_class);
	return 0;
}

__initcall(audit_classes_init);
