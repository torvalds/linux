#ifndef _LINUX_BUG_H
#define _LINUX_BUG_H

#include <linux/module.h>
#include <asm/bug.h>

enum bug_trap_type {
	BUG_TRAP_TYPE_NONE = 0,
	BUG_TRAP_TYPE_WARN = 1,
	BUG_TRAP_TYPE_BUG = 2,
};

struct pt_regs;

#ifdef CONFIG_GENERIC_BUG
#include <asm-generic/bug.h>

static inline int is_warning_bug(const struct bug_entry *bug)
{
	return bug->flags & BUGFLAG_WARNING;
}

const struct bug_entry *find_bug(unsigned long bugaddr);

enum bug_trap_type report_bug(unsigned long bug_addr, struct pt_regs *regs);

int  module_bug_finalize(const Elf_Ehdr *, const Elf_Shdr *,
			 struct module *);
void module_bug_cleanup(struct module *);

/* These are defined by the architecture */
int is_valid_bugaddr(unsigned long addr);

#else	/* !CONFIG_GENERIC_BUG */

static inline enum bug_trap_type report_bug(unsigned long bug_addr,
					    struct pt_regs *regs)
{
	return BUG_TRAP_TYPE_BUG;
}
static inline int  module_bug_finalize(const Elf_Ehdr *hdr,
					const Elf_Shdr *sechdrs,
					struct module *mod)
{
	return 0;
}
static inline void module_bug_cleanup(struct module *mod) {}

#endif	/* CONFIG_GENERIC_BUG */
#endif	/* _LINUX_BUG_H */
