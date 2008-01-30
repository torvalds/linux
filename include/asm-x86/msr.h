#ifndef __ASM_X86_MSR_H_
#define __ASM_X86_MSR_H_

#include <asm/msr-index.h>

#ifndef __ASSEMBLY__
# include <linux/types.h>
#endif

#ifdef __KERNEL__
#ifndef __ASSEMBLY__
static inline unsigned long long native_read_tscp(int *aux)
{
	unsigned long low, high;
	asm volatile (".byte 0x0f,0x01,0xf9"
		      : "=a" (low), "=d" (high), "=c" (*aux));
	return low | ((u64)high >> 32);
}

#define rdtscp(low, high, aux)						\
       do {                                                            \
		unsigned long long _val = native_read_tscp(&(aux));     \
		(low) = (u32)_val;                                      \
		(high) = (u32)(_val >> 32);                             \
       } while (0)

#define rdtscpll(val, aux) (val) = native_read_tscp(&(aux))
#endif
#endif

#ifdef __i386__

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <asm/asm.h>
#include <asm/errno.h>

static inline unsigned long long native_read_msr(unsigned int msr)
{
	unsigned long long val;

	asm volatile("rdmsr" : "=A" (val) : "c" (msr));
	return val;
}

static inline unsigned long long native_read_msr_safe(unsigned int msr,
						      int *err)
{
	unsigned long long val;

	asm volatile("2: rdmsr ; xor %0,%0\n"
		     "1:\n\t"
		     ".section .fixup,\"ax\"\n\t"
		     "3:  mov %3,%0 ; jmp 1b\n\t"
		     ".previous\n\t"
		     ".section __ex_table,\"a\"\n"
		     _ASM_ALIGN "\n\t"
		     _ASM_PTR " 2b,3b\n\t"
		     ".previous"
		     : "=r" (*err), "=A" (val)
		     : "c" (msr), "i" (-EFAULT));

	return val;
}

static inline void native_write_msr(unsigned int msr,
				    unsigned low, unsigned high)
{
	asm volatile("wrmsr" : : "c" (msr), "a"(low), "d" (high));
}

static inline int native_write_msr_safe(unsigned int msr,
					unsigned low, unsigned high)
{
	int err;
	asm volatile("2: wrmsr ; xor %0,%0\n"
		     "1:\n\t"
		     ".section .fixup,\"ax\"\n\t"
		     "3:  mov %4,%0 ; jmp 1b\n\t"
		     ".previous\n\t"
		     ".section __ex_table,\"a\"\n"
		     _ASM_ALIGN "\n\t"
		     _ASM_PTR " 2b,3b\n\t"
		     ".previous"
		     : "=a" (err)
		     : "c" (msr), "0" (low), "d" (high),
		       "i" (-EFAULT));
	return err;
}

static inline unsigned long long native_read_tsc(void)
{
	unsigned long long val;
	asm volatile("rdtsc" : "=A" (val));
	return val;
}

static inline unsigned long long native_read_pmc(int counter)
{
	unsigned long long val;
	asm volatile("rdpmc" : "=A" (val) : "c" (counter));
	return val;
}

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#include <linux/errno.h>
/*
 * Access to machine-specific registers (available on 586 and better only)
 * Note: the rd* operations modify the parameters directly (without using
 * pointer indirection), this allows gcc to optimize better
 */

#define rdmsr(msr,val1,val2)						\
	do {								\
		u64 __val = native_read_msr(msr);			\
		(val1) = (u32)__val;					\
		(val2) = (u32)(__val >> 32);				\
	} while(0)

static inline void wrmsr(unsigned msr, unsigned low, unsigned high)
{
	native_write_msr(msr, low, high);
}

#define rdmsrl(msr,val)							\
	((val) = native_read_msr(msr))

#define wrmsrl(msr, val) native_write_msr(msr, (u32)val, (u32)(val >> 32))

/* wrmsr with exception handling */
static inline int wrmsr_safe(unsigned msr, unsigned low, unsigned high)
{
	return native_write_msr_safe(msr, low, high);
}

/* rdmsr with exception handling */
#define rdmsr_safe(msr,p1,p2)						\
	({								\
		int __err;						\
		u64 __val = native_read_msr_safe(msr, &__err);		\
		(*p1) = (u32)__val;					\
		(*p2) = (u32)(__val >> 32);				\
		__err;							\
	})

#define rdtscl(low)						\
	((low) = (u32)native_read_tsc())

#define rdtscll(val)						\
	((val) = native_read_tsc())

#define write_tsc(val1,val2) wrmsr(0x10, val1, val2)

#define rdpmc(counter,low,high)					\
	do {							\
		u64 _l = native_read_pmc(counter);		\
		(low)  = (u32)_l;				\
		(high) = (u32)(_l >> 32);			\
	} while(0)
#endif	/* !CONFIG_PARAVIRT */

#endif  /* ! __ASSEMBLY__ */
#endif  /* __KERNEL__ */

#else   /* __i386__ */

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

#define rdtsc(low,high) \
     __asm__ __volatile__("rdtsc" : "=a" (low), "=d" (high))

#define rdtscl(low) \
     __asm__ __volatile__ ("rdtsc" : "=a" (low) : : "edx")


#define rdtscll(val) do { \
     unsigned int __a,__d; \
     __asm__ __volatile__("rdtsc" : "=a" (__a), "=d" (__d)); \
     (val) = ((unsigned long)__a) | (((unsigned long)__d)<<32); \
} while(0)

#define write_tsc(val1,val2) wrmsr(0x10, val1, val2)

#define write_rdtscp_aux(val) wrmsr(0xc0000103, val, 0)

#define rdpmc(counter,low,high) \
     __asm__ __volatile__("rdpmc" \
			  : "=a" (low), "=d" (high) \
			  : "c" (counter))


#ifdef __KERNEL__

/* wrmsr with exception handling */
#define wrmsr_safe(msr,a,b) ({ int ret__;			\
	asm volatile("2: wrmsr ; xorl %0,%0\n"			\
		     "1:\n\t"					\
		     ".section .fixup,\"ax\"\n\t"		\
		     "3:  movl %4,%0 ; jmp 1b\n\t"		\
		     ".previous\n\t"				\
		     ".section __ex_table,\"a\"\n"		\
		     "   .align 8\n\t"				\
		     "   .quad	2b,3b\n\t"			\
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
			".previous":"=&bDS" (ret__), "=a"(*(a)), "=d"(*(b)) \
			:"c"(msr), "i"(-EIO), "0"(0));			\
	  ret__; })

#endif  /* __ASSEMBLY__ */

#endif  /* !__i386__ */

#ifndef __ASSEMBLY__

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
#endif  /* __KERNEL__ */
#endif /* __ASSEMBLY__ */

#endif
