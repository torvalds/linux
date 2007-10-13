#ifndef _ASM_IA64_IA32_H
#define _ASM_IA64_IA32_H


#include <asm/ptrace.h>
#include <asm/signal.h>

#define IA32_NR_syscalls		285	/* length of syscall table */
#define IA32_PAGE_SHIFT			12	/* 4KB pages */

#ifndef __ASSEMBLY__

# ifdef CONFIG_IA32_SUPPORT

#define IA32_PAGE_OFFSET	0xc0000000

extern void ia32_cpu_init (void);
extern void ia32_mem_init (void);
extern void ia32_gdt_init (void);
extern int ia32_exception (struct pt_regs *regs, unsigned long isr);
extern int ia32_intercept (struct pt_regs *regs, unsigned long isr);
extern int ia32_clone_tls (struct task_struct *child, struct pt_regs *childregs);

# endif /* !CONFIG_IA32_SUPPORT */

/* Declare this unconditionally, so we don't get warnings for unreachable code.  */
extern int ia32_setup_frame1 (int sig, struct k_sigaction *ka, siginfo_t *info,
			      sigset_t *set, struct pt_regs *regs);
#if PAGE_SHIFT > IA32_PAGE_SHIFT
extern int ia32_copy_ia64_partial_page_list(struct task_struct *,
					unsigned long);
extern void ia32_drop_ia64_partial_page_list(struct task_struct *);
#else
# define ia32_copy_ia64_partial_page_list(a1, a2)	0
# define ia32_drop_ia64_partial_page_list(a1)	do { ; } while (0)
#endif

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_IA64_IA32_H */
