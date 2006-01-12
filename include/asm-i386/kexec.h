#ifndef _I386_KEXEC_H
#define _I386_KEXEC_H

#include <asm/fixmap.h>
#include <asm/ptrace.h>
#include <asm/string.h>

/*
 * KEXEC_SOURCE_MEMORY_LIMIT maximum page get_free_page can return.
 * I.e. Maximum page that is mapped directly into kernel memory,
 * and kmap is not required.
 *
 * Someone correct me if FIXADDR_START - PAGEOFFSET is not the correct
 * calculation for the amount of memory directly mappable into the
 * kernel memory space.
 */

/* Maximum physical address we can use pages from */
#define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)
/* Maximum address we can reach in physical address mode */
#define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)
/* Maximum address we can use for the control code buffer */
#define KEXEC_CONTROL_MEMORY_LIMIT TASK_SIZE

#define KEXEC_CONTROL_CODE_SIZE	4096

/* The native architecture */
#define KEXEC_ARCH KEXEC_ARCH_386

#define MAX_NOTE_BYTES 1024

/* CPU does not save ss and esp on stack if execution is already
 * running in kernel mode at the time of NMI occurrence. This code
 * fixes it.
 */
static inline void crash_fixup_ss_esp(struct pt_regs *newregs,
					struct pt_regs *oldregs)
{
	memcpy(newregs, oldregs, sizeof(*newregs));
	newregs->esp = (unsigned long)&(oldregs->esp);
	__asm__ __volatile__(
			"xorl %%eax, %%eax\n\t"
			"movw %%ss, %%ax\n\t"
			:"=a"(newregs->xss));
}

/*
 * This function is responsible for capturing register states if coming
 * via panic otherwise just fix up the ss and esp if coming via kernel
 * mode exception.
 */
static inline void crash_setup_regs(struct pt_regs *newregs,
                                       struct pt_regs *oldregs)
{
       if (oldregs)
               crash_fixup_ss_esp(newregs, oldregs);
       else {
               __asm__ __volatile__("movl %%ebx,%0" : "=m"(newregs->ebx));
               __asm__ __volatile__("movl %%ecx,%0" : "=m"(newregs->ecx));
               __asm__ __volatile__("movl %%edx,%0" : "=m"(newregs->edx));
               __asm__ __volatile__("movl %%esi,%0" : "=m"(newregs->esi));
               __asm__ __volatile__("movl %%edi,%0" : "=m"(newregs->edi));
               __asm__ __volatile__("movl %%ebp,%0" : "=m"(newregs->ebp));
               __asm__ __volatile__("movl %%eax,%0" : "=m"(newregs->eax));
               __asm__ __volatile__("movl %%esp,%0" : "=m"(newregs->esp));
               __asm__ __volatile__("movw %%ss, %%ax;" :"=a"(newregs->xss));
               __asm__ __volatile__("movw %%cs, %%ax;" :"=a"(newregs->xcs));
               __asm__ __volatile__("movw %%ds, %%ax;" :"=a"(newregs->xds));
               __asm__ __volatile__("movw %%es, %%ax;" :"=a"(newregs->xes));
               __asm__ __volatile__("pushfl; popl %0" :"=m"(newregs->eflags));

               newregs->eip = (unsigned long)current_text_addr();
       }
}

#endif /* _I386_KEXEC_H */
