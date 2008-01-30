#ifndef __ASM_X86_PROCESSOR_H
#define __ASM_X86_PROCESSOR_H

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


#ifdef CONFIG_X86_32
# include "processor_32.h"
#else
# include "processor_64.h"
#endif

#ifndef CONFIG_PARAVIRT
#define __cpuid native_cpuid
#endif

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
