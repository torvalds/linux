#ifndef _ASM_GENERIC_KPROBES_H
#define _ASM_GENERIC_KPROBES_H

#if defined(__KERNEL__) && !defined(__ASSEMBLY__)
#ifdef CONFIG_KPROBES
/*
 * Blacklist ganerating macro. Specify functions which is not probed
 * by using this macro.
 */
# define __NOKPROBE_SYMBOL(fname)				\
static unsigned long __used					\
	__attribute__((__section__("_kprobe_blacklist")))	\
	_kbl_addr_##fname = (unsigned long)fname;
# define NOKPROBE_SYMBOL(fname)	__NOKPROBE_SYMBOL(fname)
/* Use this to forbid a kprobes attach on very low level functions */
# define __kprobes	__attribute__((__section__(".kprobes.text")))
# define nokprobe_inline	__always_inline
#else
# define NOKPROBE_SYMBOL(fname)
# define __kprobes
# define nokprobe_inline	inline
#endif
#endif /* defined(__KERNEL__) && !defined(__ASSEMBLY__) */

#endif /* _ASM_GENERIC_KPROBES_H */
