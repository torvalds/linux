#ifndef X86_64_MSR_H
#define X86_64_MSR_H 1

#include <asm/msr-index.h>

#ifndef __ASSEMBLY__
#include <linux/errno.h>
/*
 * Access to machine-specific registers (available on 586 and better only)
 * Note: the rd* operations modify the parameters directly (without using
 * pointer indirection), this allows gcc to optimize better
 */

#define rdmsr(msr,val1,val2) \
       __asm__ __volatile__("rdmsr" \
			    : "=a" (val1), "=d" (val2) \
			    : "c" (msr))


#define rdmsrl(msr,val) do { unsigned long a__,b__; \
       __asm__ __volatile__("rdmsr" \
			    : "=a" (a__), "=d" (b__) \
			    : "c" (msr)); \
       val = a__ | (b__<<32); \
} while(0)

#define wrmsr(msr,val1,val2) \
     __asm__ __volatile__("wrmsr" \
			  : /* no outputs */ \
			  : "c" (msr), "a" (val1), "d" (val2))

#define wrmsrl(msr,val) wrmsr(msr,(__u32)((__u64)(val)),((__u64)(val))>>32) 

/* wrmsr with exception handling */
#define wrmsr_safe(msr,a,b) ({ int ret__;			\
	asm volatile("2: wrmsr ; xorl %0,%0\n"			\
		     "1:\n\t"					\
		     ".section .fixup,\"ax\"\n\t"		\
		     "3:  movl %4,%0 ; jmp 1b\n\t"		\
		     ".previous\n\t"				\
 		     ".section __ex_table,\"a\"\n"		\
		     "   .align 8\n\t"				\
		     "   .quad 	2b,3b\n\t"			\
		     ".previous"				\
		     : "=a" (ret__)				\
		     : "c" (msr), "0" (a), "d" (b), "i" (-EFAULT)); \
	ret__; })

#define checking_wrmsrl(msr,val) wrmsr_safe(msr,(u32)(val),(u32)((val)>>32))

#define rdmsr_safe(msr,a,b) \
	({ int ret__;						\
	  asm volatile ("1:       rdmsr\n"			\
                      "2:\n"					\
                      ".section .fixup,\"ax\"\n"		\
                      "3:       movl %4,%0\n"			\
                      " jmp 2b\n"				\
                      ".previous\n"				\
                      ".section __ex_table,\"a\"\n"		\
                      " .align 8\n"				\
                      " .quad 1b,3b\n"				\
                      ".previous":"=&bDS" (ret__), "=a"(*(a)), "=d"(*(b))\
                      :"c"(msr), "i"(-EIO), "0"(0));		\
	  ret__; })		

#define rdtsc(low,high) \
     __asm__ __volatile__("rdtsc" : "=a" (low), "=d" (high))

#define rdtscl(low) \
     __asm__ __volatile__ ("rdtsc" : "=a" (low) : : "edx")

#define rdtscp(low,high,aux) \
     asm volatile (".byte 0x0f,0x01,0xf9" : "=a" (low), "=d" (high), "=c" (aux))

#define rdtscll(val) do { \
     unsigned int __a,__d; \
     asm volatile("rdtsc" : "=a" (__a), "=d" (__d)); \
     (val) = ((unsigned long)__a) | (((unsigned long)__d)<<32); \
} while(0)

#define rdtscpll(val, aux) do { \
     unsigned long __a, __d; \
     asm volatile (".byte 0x0f,0x01,0xf9" : "=a" (__a), "=d" (__d), "=c" (aux)); \
     (val) = (__d << 32) | __a; \
} while (0)

#define write_tsc(val1,val2) wrmsr(0x10, val1, val2)

#define write_rdtscp_aux(val) wrmsr(0xc0000103, val, 0)

#define rdpmc(counter,low,high) \
     __asm__ __volatile__("rdpmc" \
			  : "=a" (low), "=d" (high) \
			  : "c" (counter))

static inline void cpuid(int op, unsigned int *eax, unsigned int *ebx,
			 unsigned int *ecx, unsigned int *edx)
{
	__asm__("cpuid"
		: "=a" (*eax),
		  "=b" (*ebx),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "0" (op));
}

/* Some CPUID calls want 'count' to be placed in ecx */
static inline void cpuid_count(int op, int count, int *eax, int *ebx, int *ecx,
	       	int *edx)
{
	__asm__("cpuid"
		: "=a" (*eax),
		  "=b" (*ebx),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "0" (op), "c" (count));
}

/*
 * CPUID functions returning a single datum
 */
static inline unsigned int cpuid_eax(unsigned int op)
{
	unsigned int eax;

	__asm__("cpuid"
		: "=a" (eax)
		: "0" (op)
		: "bx", "cx", "dx");
	return eax;
}
static inline unsigned int cpuid_ebx(unsigned int op)
{
	unsigned int eax, ebx;

	__asm__("cpuid"
		: "=a" (eax), "=b" (ebx)
		: "0" (op)
		: "cx", "dx" );
	return ebx;
}
static inline unsigned int cpuid_ecx(unsigned int op)
{
	unsigned int eax, ecx;

	__asm__("cpuid"
		: "=a" (eax), "=c" (ecx)
		: "0" (op)
		: "bx", "dx" );
	return ecx;
}
static inline unsigned int cpuid_edx(unsigned int op)
{
	unsigned int eax, edx;

	__asm__("cpuid"
		: "=a" (eax), "=d" (edx)
		: "0" (op)
		: "bx", "cx");
	return edx;
}

#ifdef CONFIG_SMP
void rdmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h);
void wrmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h);
int rdmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h);
int wrmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h);
#else  /*  CONFIG_SMP  */
static inline void rdmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h)
{
	rdmsr(msr_no, *l, *h);
}
static inline void wrmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h)
{
	wrmsr(msr_no, l, h);
}
static inline int rdmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h)
{
	return rdmsr_safe(msr_no, l, h);
}
static inline int wrmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h)
{
	return wrmsr_safe(msr_no, l, h);
}
#endif  /* CONFIG_SMP */
#endif /* __ASSEMBLY__ */
#endif	/* X86_64_MSR_H */
