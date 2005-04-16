#ifndef __UM_ARCHPARAM_PPC_H
#define __UM_ARCHPARAM_PPC_H

/********* Bits for asm-um/elf.h ************/

#define ELF_PLATFORM (0)

#define ELF_ET_DYN_BASE (0x08000000)

/* the following stolen from asm-ppc/elf.h */
#define ELF_NGREG	48	/* includes nip, msr, lr, etc. */
#define ELF_NFPREG	33	/* includes fpscr */
/* General registers */
typedef unsigned long elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

/* Floating point registers */
typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

#define ELF_DATA        ELFDATA2MSB
#define ELF_ARCH	EM_PPC

/********* Bits for asm-um/hw_irq.h **********/

struct hw_interrupt_type;

/********* Bits for asm-um/hardirq.h **********/

#define irq_enter(cpu, irq) hardirq_enter(cpu)
#define irq_exit(cpu, irq) hardirq_exit(cpu)

/********* Bits for asm-um/string.h **********/

#define __HAVE_ARCH_STRRCHR

#endif
