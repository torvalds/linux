/* Public domain. */

#ifndef _ASM_CPUFEATURE_H
#define _ASM_CPUFEATURE_H

#if defined(__amd64__) || defined(__i386__)

#include <sys/param.h>

#define X86_FEATURE_CLFLUSH	1
#define X86_FEATURE_XMM4_1	2
#define X86_FEATURE_PAT		3
#define X86_FEATURE_HYPERVISOR	4

static inline bool
static_cpu_has(uint16_t f)
{
	switch (f) {
	case X86_FEATURE_XMM4_1:
		return (cpu_ecxfeature & CPUIDECX_SSE41) != 0;
#ifdef __amd64__
	case X86_FEATURE_CLFLUSH:
	case X86_FEATURE_PAT:
		return true;
#else
	case X86_FEATURE_CLFLUSH:
		return curcpu()->ci_cflushsz != 0;
	case X86_FEATURE_PAT:
		return (curcpu()->ci_feature_flags & CPUID_PAT) != 0;
#endif
	case X86_FEATURE_HYPERVISOR:
		return (cpu_ecxfeature & CPUIDECX_HV) != 0;
	default:
		return false;
	}
}

static inline bool
pat_enabled(void)
{
	return static_cpu_has(X86_FEATURE_PAT);
}

#define boot_cpu_has(x) static_cpu_has(x)

static inline void
clflushopt(volatile void *addr)
{
	if (curcpu()->ci_feature_sefflags_ebx & SEFF0EBX_CLFLUSHOPT)
		__asm volatile("clflushopt %0" : "+m" (*(volatile char *)addr));
	else
		__asm volatile("clflush %0" : "+m" (*(volatile char *)addr));
}

#endif

#endif
