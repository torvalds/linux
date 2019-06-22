#ifndef _LINUX_CFI_H
#define _LINUX_CFI_H

#include <linux/stringify.h>

#ifdef CONFIG_CFI_CLANG
#ifdef CONFIG_MODULES

typedef void (*cfi_check_fn)(uint64_t, void *, void *);

/* Compiler-generated function in each module, and the kernel */
#define CFI_CHECK_FN		__cfi_check
#define CFI_CHECK_FN_NAME	__stringify(CFI_CHECK_FN)

extern void CFI_CHECK_FN(uint64_t, void *, void *);

#ifdef CONFIG_CFI_CLANG_SHADOW
extern void cfi_module_add(struct module *mod, unsigned long min_addr,
	unsigned long max_addr);

extern void cfi_module_remove(struct module *mod, unsigned long min_addr,
	unsigned long max_addr);
#else
static inline void cfi_module_add(struct module *mod, unsigned long min_addr,
	unsigned long max_addr)
{
}

static inline void cfi_module_remove(struct module *mod, unsigned long min_addr,
	unsigned long max_addr)
{
}
#endif /* CONFIG_CFI_CLANG_SHADOW */

#endif /* CONFIG_MODULES */
#endif /* CONFIG_CFI_CLANG */

#endif /* _LINUX_CFI_H */
