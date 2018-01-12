/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_ERROR_INJECTION_H
#define _ASM_GENERIC_ERROR_INJECTION_H

#if defined(__KERNEL__) && !defined(__ASSEMBLY__)
#ifdef CONFIG_FUNCTION_ERROR_INJECTION
/*
 * Whitelist ganerating macro. Specify functions which can be
 * error-injectable using this macro.
 */
#define ALLOW_ERROR_INJECTION(fname)					\
static unsigned long __used						\
	__attribute__((__section__("_error_injection_whitelist")))	\
	_eil_addr_##fname = (unsigned long)fname;
#else
#define ALLOW_ERROR_INJECTION(fname)
#endif
#endif

#endif /* _ASM_GENERIC_ERROR_INJECTION_H */
