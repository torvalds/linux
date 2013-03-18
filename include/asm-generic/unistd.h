#include <uapi/asm-generic/unistd.h>

/*
 * These are required system calls, we should
 * invert the logic eventually and let them
 * be selected by default.
 */
#if __BITS_PER_LONG == 32
#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_LLSEEK
#endif

/*
 * "Conditional" syscalls
 *
 * What we want is __attribute__((weak,alias("sys_ni_syscall"))),
 * but it doesn't work on all toolchains, so we just do it by hand
 */
#ifndef cond_syscall
#ifdef CONFIG_SYMBOL_PREFIX
#define __SYMBOL_PREFIX CONFIG_SYMBOL_PREFIX
#else
#define __SYMBOL_PREFIX
#endif
#define cond_syscall(x) asm(".weak\t" __SYMBOL_PREFIX #x "\n\t" \
			    ".set\t" __SYMBOL_PREFIX #x "," \
			    __SYMBOL_PREFIX "sys_ni_syscall")
#endif
