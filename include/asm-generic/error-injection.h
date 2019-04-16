/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_ERROR_INJECTION_H
#define _ASM_GENERIC_ERROR_INJECTION_H

#if defined(__KERNEL__) && !defined(__ASSEMBLY__)
enum {
	EI_ETYPE_NONE,		/* Dummy value for undefined case */
	EI_ETYPE_NULL,		/* Return NULL if failure */
	EI_ETYPE_ERRNO,		/* Return -ERRNO if failure */
	EI_ETYPE_ERRNO_NULL,	/* Return -ERRNO or NULL if failure */
	EI_ETYPE_TRUE,		/* Return true if failure */
};

struct error_injection_entry {
	unsigned long	addr;
	int		etype;
};

#ifdef CONFIG_FUNCTION_ERROR_INJECTION
/*
 * Whitelist ganerating macro. Specify functions which can be
 * error-injectable using this macro.
 */
#define ALLOW_ERROR_INJECTION(fname, _etype)				\
static struct error_injection_entry __used				\
	__attribute__((__section__("_error_injection_whitelist")))	\
	_eil_addr_##fname = {						\
		.addr = (unsigned long)fname,				\
		.etype = EI_ETYPE_##_etype,				\
	};
#else
#define ALLOW_ERROR_INJECTION(fname, _etype)
#endif
#endif

#endif /* _ASM_GENERIC_ERROR_INJECTION_H */
