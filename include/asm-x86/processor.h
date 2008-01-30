#ifndef __ASM_X86_PROCESSOR_H
#define __ASM_X86_PROCESSOR_H

#include <asm/processor-flags.h>

#include <asm/page.h>
#include <asm/system.h>

static inline void native_cpuid(unsigned int *eax, unsigned int *ebx,
					 unsigned int *ecx, unsigned int *edx)
{
	/* ecx is often an input as well as an output. */
	__asm__("cpuid"
		: "=a" (*eax),
		  "=b" (*ebx),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "0" (*eax), "2" (*ecx));
}

static inline void load_cr3(pgd_t *pgdir)
{
	write_cr3(__pa(pgdir));
}

#ifdef CONFIG_X86_32
# include "processor_32.h"
#else
# include "processor_64.h"
#endif

static inline unsigned long native_get_debugreg(int regno)
{
	unsigned long val = 0; 	/* Damn you, gcc! */

	switch (regno) {
	case 0:
		asm("mov %%db0, %0" :"=r" (val)); break;
	case 1:
		asm("mov %%db1, %0" :"=r" (val)); break;
	case 2:
		asm("mov %%db2, %0" :"=r" (val)); break;
	case 3:
		asm("mov %%db3, %0" :"=r" (val)); break;
	case 6:
		asm("mov %%db6, %0" :"=r" (val)); break;
	case 7:
		asm("mov %%db7, %0" :"=r" (val)); break;
	default:
		BUG();
	}
	return val;
}

static inline void native_set_debugreg(int regno, unsigned long value)
{
	switch (regno) {
	case 0:
		asm("mov %0,%%db0"	: /* no output */ :"r" (value));
		break;
	case 1:
		asm("mov %0,%%db1"	: /* no output */ :"r" (value));
		break;
	case 2:
		asm("mov %0,%%db2"	: /* no output */ :"r" (value));
		break;
	case 3:
		asm("mov %0,%%db3"	: /* no output */ :"r" (value));
		break;
	case 6:
		asm("mov %0,%%db6"	: /* no output */ :"r" (value));
		break;
	case 7:
		asm("mov %0,%%db7"	: /* no output */ :"r" (value));
		break;
	default:
		BUG();
	}
}


#ifndef CONFIG_PARAVIRT
#define __cpuid native_cpuid
#define paravirt_enabled() 0

/*
 * These special macros can be used to get or set a debugging register
 */
#define get_debugreg(var, register)				\
	(var) = native_get_debugreg(register)
#define set_debugreg(value, register)				\
	native_set_debugreg(register, value)

#endif /* CONFIG_PARAVIRT */

/*
 * Save the cr4 feature set we're using (ie
 * Pentium 4MB enable and PPro Global page
 * enable), so that any CPU's that boot up
 * after us can get the correct flags.
 */
extern unsigned long mmu_cr4_features;

static inline void set_in_cr4(unsigned long mask)
{
	unsigned cr4;
	mmu_cr4_features |= mask;
	cr4 = read_cr4();
	cr4 |= mask;
	write_cr4(cr4);
}

static inline void clear_in_cr4(unsigned long mask)
{
	unsigned cr4;
	mmu_cr4_features &= ~mask;
	cr4 = read_cr4();
	cr4 &= ~mask;
	write_cr4(cr4);
}



/*
 * Generic CPUID function
 * clear %ecx since some cpus (Cyrix MII) do not set or clear %ecx
 * resulting in stale register contents being returned.
 */
static inline void cpuid(unsigned int op,
			 unsigned int *eax, unsigned int *ebx,
			 unsigned int *ecx, unsigned int *edx)
{
	*eax = op;
	*ecx = 0;
	__cpuid(eax, ebx, ecx, edx);
}

/* Some CPUID calls want 'count' to be placed in ecx */
static inline void cpuid_count(unsigned int op, int count,
			       unsigned int *eax, unsigned int *ebx,
			       unsigned int *ecx, unsigned int *edx)
{
	*eax = op;
	*ecx = count;
	__cpuid(eax, ebx, ecx, edx);
}

/*
 * CPUID functions returning a single datum
 */
static inline unsigned int cpuid_eax(unsigned int op)
{
	unsigned int eax, ebx, ecx, edx;

	cpuid(op, &eax, &ebx, &ecx, &edx);
	return eax;
}
static inline unsigned int cpuid_ebx(unsigned int op)
{
	unsigned int eax, ebx, ecx, edx;

	cpuid(op, &eax, &ebx, &ecx, &edx);
	return ebx;
}
static inline unsigned int cpuid_ecx(unsigned int op)
{
	unsigned int eax, ebx, ecx, edx;

	cpuid(op, &eax, &ebx, &ecx, &edx);
	return ecx;
}
static inline unsigned int cpuid_edx(unsigned int op)
{
	unsigned int eax, ebx, ecx, edx;

	cpuid(op, &eax, &ebx, &ecx, &edx);
	return edx;
}

#endif
