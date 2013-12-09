#ifndef _LINUX_ELFCORE_H
#define _LINUX_ELFCORE_H

#include <linux/user.h>
#include <linux/bug.h>
#include <asm/elf.h>
#include <uapi/linux/elfcore.h>

struct coredump_params;

static inline void elf_core_copy_regs(elf_gregset_t *elfregs, struct pt_regs *regs)
{
#ifdef ELF_CORE_COPY_REGS
	ELF_CORE_COPY_REGS((*elfregs), regs)
#else
	BUG_ON(sizeof(*elfregs) != sizeof(*regs));
	*(struct pt_regs *)elfregs = *regs;
#endif
}

static inline void elf_core_copy_kernel_regs(elf_gregset_t *elfregs, struct pt_regs *regs)
{
#ifdef ELF_CORE_COPY_KERNEL_REGS
	ELF_CORE_COPY_KERNEL_REGS((*elfregs), regs);
#else
	elf_core_copy_regs(elfregs, regs);
#endif
}

static inline int elf_core_copy_task_regs(struct task_struct *t, elf_gregset_t* elfregs)
{
#if defined (ELF_CORE_COPY_TASK_REGS)
	return ELF_CORE_COPY_TASK_REGS(t, elfregs);
#elif defined (task_pt_regs)
	elf_core_copy_regs(elfregs, task_pt_regs(t));
#endif
	return 0;
}

extern int dump_fpu (struct pt_regs *, elf_fpregset_t *);

static inline int elf_core_copy_task_fpregs(struct task_struct *t, struct pt_regs *regs, elf_fpregset_t *fpu)
{
#ifdef ELF_CORE_COPY_FPREGS
	return ELF_CORE_COPY_FPREGS(t, fpu);
#else
	return dump_fpu(regs, fpu);
#endif
}

#ifdef ELF_CORE_COPY_XFPREGS
static inline int elf_core_copy_task_xfpregs(struct task_struct *t, elf_fpxregset_t *xfpu)
{
	return ELF_CORE_COPY_XFPREGS(t, xfpu);
}
#endif

/*
 * These functions parameterize elf_core_dump in fs/binfmt_elf.c to write out
 * extra segments containing the gate DSO contents.  Dumping its
 * contents makes post-mortem fully interpretable later without matching up
 * the same kernel and hardware config to see what PC values meant.
 * Dumping its extra ELF program headers includes all the other information
 * a debugger needs to easily find how the gate DSO was being used.
 */
extern Elf_Half elf_core_extra_phdrs(void);
extern int
elf_core_write_extra_phdrs(struct coredump_params *cprm, loff_t offset);
extern int
elf_core_write_extra_data(struct coredump_params *cprm);
extern size_t elf_core_extra_data_size(void);

#endif /* _LINUX_ELFCORE_H */
