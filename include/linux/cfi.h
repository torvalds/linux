/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Clang Control Flow Integrity (CFI) support.
 *
 * Copyright (C) 2022 Google LLC
 */
#ifndef _LINUX_CFI_H
#define _LINUX_CFI_H

#include <linux/bug.h>
#include <linux/module.h>
#include <asm/cfi.h>

extern bool cfi_warn;

#ifndef cfi_get_offset
static inline int cfi_get_offset(void)
{
	return 0;
}
#endif

#ifdef CONFIG_CFI_CLANG
enum bug_trap_type report_cfi_failure(struct pt_regs *regs, unsigned long addr,
				      unsigned long *target, u32 type);

static inline enum bug_trap_type report_cfi_failure_noaddr(struct pt_regs *regs,
							   unsigned long addr)
{
	return report_cfi_failure(regs, addr, NULL, 0);
}
#endif /* CONFIG_CFI_CLANG */

#ifdef CONFIG_ARCH_USES_CFI_TRAPS
bool is_cfi_trap(unsigned long addr);
#else
static inline bool is_cfi_trap(unsigned long addr) { return false; }
#endif

#ifdef CONFIG_MODULES
#ifdef CONFIG_ARCH_USES_CFI_TRAPS
void module_cfi_finalize(const Elf_Ehdr *hdr, const Elf_Shdr *sechdrs,
			 struct module *mod);
#else
static inline void module_cfi_finalize(const Elf_Ehdr *hdr,
				       const Elf_Shdr *sechdrs,
				       struct module *mod) {}
#endif /* CONFIG_ARCH_USES_CFI_TRAPS */
#endif /* CONFIG_MODULES */

#ifndef CFI_NOSEAL
#define CFI_NOSEAL(x)
#endif

#endif /* _LINUX_CFI_H */
