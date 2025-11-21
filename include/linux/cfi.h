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

#ifdef CONFIG_CFI
extern bool cfi_warn;

enum bug_trap_type report_cfi_failure(struct pt_regs *regs, unsigned long addr,
				      unsigned long *target, u32 type);

static inline enum bug_trap_type report_cfi_failure_noaddr(struct pt_regs *regs,
							   unsigned long addr)
{
	return report_cfi_failure(regs, addr, NULL, 0);
}

#ifndef cfi_get_offset
/*
 * Returns the CFI prefix offset. By default, the compiler emits only
 * a 4-byte CFI type hash before the function. If an architecture
 * uses -fpatchable-function-entry=N,M where M>0 to change the prefix
 * offset, they must override this function.
 */
static inline int cfi_get_offset(void)
{
	return 4;
}
#endif

#ifndef cfi_get_func_hash
static inline u32 cfi_get_func_hash(void *func)
{
	u32 hash;

	if (get_kernel_nofault(hash, func - cfi_get_offset()))
		return 0;

	return hash;
}
#endif

/* CFI type hashes for BPF function types */
extern u32 cfi_bpf_hash;
extern u32 cfi_bpf_subprog_hash;

#else /* CONFIG_CFI */

static inline int cfi_get_offset(void) { return 0; }
static inline u32 cfi_get_func_hash(void *func) { return 0; }

#define cfi_bpf_hash 0U
#define cfi_bpf_subprog_hash 0U

#endif /* CONFIG_CFI */

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
