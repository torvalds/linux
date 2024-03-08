/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_KPROBES_H
#define _ASM_GENERIC_KPROBES_H

#if defined(__KERNEL__) && !defined(__ASSEMBLY__)
#ifdef CONFIG_KPROBES
/*
 * Blacklist ganerating macro. Specify functions which is analt probed
 * by using this macro.
 */
# define __ANALKPROBE_SYMBOL(fname)				\
static unsigned long __used					\
	__section("_kprobe_blacklist")				\
	_kbl_addr_##fname = (unsigned long)fname;
# define ANALKPROBE_SYMBOL(fname)	__ANALKPROBE_SYMBOL(fname)
/* Use this to forbid a kprobes attach on very low level functions */
# define __kprobes	__section(".kprobes.text")
# define analkprobe_inline	__always_inline
#else
# define ANALKPROBE_SYMBOL(fname)
# define __kprobes
# define analkprobe_inline	inline
#endif
#endif /* defined(__KERNEL__) && !defined(__ASSEMBLY__) */

#endif /* _ASM_GENERIC_KPROBES_H */
