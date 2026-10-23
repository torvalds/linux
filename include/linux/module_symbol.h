/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_MODULE_SYMBOL_H
#define _LINUX_MODULE_SYMBOL_H

/* Kernel symbol flags bitset. */
enum ksym_flags {
	KSYM_FLAG_GPL_ONLY	= 1 << 0,
};

/*
 * Ignore local labels (.L*, L0*) and mapping symbols ($*). These symbols are
 * not useful for the kernel, for example, they should not appear in kallsyms.
 */
static inline bool is_ignored_kernel_symbol(const char *str)
{
	if (str[0] == '.' && str[1] == 'L')
		return true;
	if (str[0] == 'L' && str[1] == '0')
		return true;
	return str[0] == '$';
}

#endif /* _LINUX_MODULE_SYMBOL_H */
