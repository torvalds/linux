#ifndef _X86_64_KEXEC_H
#define _X86_64_KEXEC_H

#define PA_CONTROL_PAGE  0
#define VA_CONTROL_PAGE  1
#define PA_PGD           2
#define VA_PGD           3
#define PA_PUD_0         4
#define VA_PUD_0         5
#define PA_PMD_0         6
#define VA_PMD_0         7
#define PA_PTE_0         8
#define VA_PTE_0         9
#define PA_PUD_1         10
#define VA_PUD_1         11
#define PA_PMD_1         12
#define VA_PMD_1         13
#define PA_PTE_1         14
#define VA_PTE_1         15
#define PA_TABLE_PAGE    16
#define PAGES_NR         17

#ifndef __ASSEMBLY__

#include <linux/string.h>

#include <asm/page.h>
#include <asm/ptrace.h>

/*
 * KEXEC_SOURCE_MEMORY_LIMIT maximum page get_free_page can return.
 * I.e. Maximum page that is mapped directly into kernel memory,
 * and kmap is not required.
 *
 * So far x86_64 is limited to 40 physical address bits.
 */

/* Maximum physical address we can use pages from */
#define KEXEC_SOURCE_MEMORY_LIMIT      (0xFFFFFFFFFFUL)
/* Maximum address we can reach in physical address mode */
#define KEXEC_DESTINATION_MEMORY_LIMIT (0xFFFFFFFFFFUL)
/* Maximum address we can use for the control pages */
#define KEXEC_CONTROL_MEMORY_LIMIT     (0xFFFFFFFFFFUL)

/* Allocate one page for the pdp and the second for the code */
#define KEXEC_CONTROL_CODE_SIZE  (4096UL + 4096UL)

/* The native architecture */
#define KEXEC_ARCH KEXEC_ARCH_X86_64

/*
 * Saving the registers of the cpu on which panic occured in
 * crash_kexec to save a valid sp. The registers of other cpus
 * will be saved in machine_crash_shutdown while shooting down them.
 */

static inline void crash_setup_regs(struct pt_regs *newregs,
						struct pt_regs *oldregs)
{
	if (oldregs)
		memcpy(newregs, oldregs, sizeof(*newregs));
	else {
		__asm__ __volatile__("movq %%rbx,%0" : "=m"(newregs->rbx));
		__asm__ __volatile__("movq %%rcx,%0" : "=m"(newregs->rcx));
		__asm__ __volatile__("movq %%rdx,%0" : "=m"(newregs->rdx));
		__asm__ __volatile__("movq %%rsi,%0" : "=m"(newregs->rsi));
		__asm__ __volatile__("movq %%rdi,%0" : "=m"(newregs->rdi));
		__asm__ __volatile__("movq %%rbp,%0" : "=m"(newregs->rbp));
		__asm__ __volatile__("movq %%rax,%0" : "=m"(newregs->rax));
		__asm__ __volatile__("movq %%rsp,%0" : "=m"(newregs->rsp));
		__asm__ __volatile__("movq %%r8,%0" : "=m"(newregs->r8));
		__asm__ __volatile__("movq %%r9,%0" : "=m"(newregs->r9));
		__asm__ __volatile__("movq %%r10,%0" : "=m"(newregs->r10));
		__asm__ __volatile__("movq %%r11,%0" : "=m"(newregs->r11));
		__asm__ __volatile__("movq %%r12,%0" : "=m"(newregs->r12));
		__asm__ __volatile__("movq %%r13,%0" : "=m"(newregs->r13));
		__asm__ __volatile__("movq %%r14,%0" : "=m"(newregs->r14));
		__asm__ __volatile__("movq %%r15,%0" : "=m"(newregs->r15));
		__asm__ __volatile__("movl %%ss, %%eax;" :"=a"(newregs->ss));
		__asm__ __volatile__("movl %%cs, %%eax;" :"=a"(newregs->cs));
		__asm__ __volatile__("pushfq; popq %0" :"=m"(newregs->eflags));

		newregs->rip = (unsigned long)current_text_addr();
	}
}

NORET_TYPE void
relocate_kernel(unsigned long indirection_page,
		unsigned long page_list,
		unsigned long start_address) ATTRIB_NORET;

#endif /* __ASSEMBLY__ */

#endif /* _X86_64_KEXEC_H */
