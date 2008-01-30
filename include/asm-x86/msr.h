#ifndef __ASM_X86_MSR_H_
#define __ASM_X86_MSR_H_

#include <asm/msr-index.h>

#ifndef __ASSEMBLY__
# include <linux/types.h>
#endif

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <asm/asm.h>
#include <asm/errno.h>

static inline unsigned long long native_read_tscp(unsigned int *aux)
{
	unsigned long low, high;
	asm volatile (".byte 0x0f,0x01,0xf9"
		      : "=a" (low), "=d" (high), "=c" (*aux));
	return low | ((u64)high >> 32);
}

/*
 * i386 calling convention returns 64-bit value in edx:eax, while
 * x86_64 returns at rax. Also, the "A" constraint does not really
 * mean rdx:rax in x86_64, so we need specialized behaviour for each
 * architecture
 */
#ifdef CONFIG_X86_64
#define DECLARE_ARGS(val, low, high)	unsigned low, high
#define EAX_EDX_VAL(val, low, high)	(low | ((u64)(high) << 32))
#define EAX_EDX_ARGS(val, low, high)	"a" (low), "d" (high)
#define EAX_EDX_RET(val, low, high)	"=a" (low), "=d" (high)
#else
#define DECLARE_ARGS(val, low, high)	unsigned long long val
#define EAX_EDX_VAL(val, low, high)	(val)
#define EAX_EDX_ARGS(val, low, high)	"A" (val)
#define EAX_EDX_RET(val, low, high)	"=A" (val)
#endif

static inline unsigned long long native_read_msr(unsigned int msr)
{
	DECLARE_ARGS(val, low, high);

	asm volatile("rdmsr" : EAX_EDX_RET(val, low, high) : "c" (msr));
	return EAX_EDX_VAL(val, low, high);
}

static inline unsigned long long native_read_msr_safe(unsigned int msr,
						      int *err)
{
	DECLARE_ARGS(val, low, high);

	asm volatile("2: rdmsr ; xor %0,%0\n"
		     "1:\n\t"
		     ".section .fixup,\"ax\"\n\t"
		     "3:  mov %3,%0 ; jmp 1b\n\t"
		     ".previous\n\t"
		     ".section __ex_table,\"a\"\n"
		     _ASM_ALIGN "\n\t"
		     _ASM_PTR " 2b,3b\n\t"
		     ".previous"
		     : "=r" (*err), EAX_EDX_RET(val, low, high)
		     : "c" (msr), "i" (-EFAULT));
	return EAX_EDX_VAL(val, low, high);
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

extern unsigned long long native_read_tsc(void);

static __always_inline unsigned long long __native_read_tsc(void)
{
	DECLARE_ARGS(val, low, high);

	rdtsc_barrier();
	asm volatile("rdtsc" : EAX_EDX_RET(val, low, high));
	rdtsc_barrier();

	return EAX_EDX_VAL(val, low, high);
}

static inline unsigned long long native_read_pmc(int counter)
{
	DECLARE_ARGS(val, low, high);

	asm volatile("rdpmc" : EAX_EDX_RET(val, low, high) : "c" (counter));
	return EAX_EDX_VAL(val, low, high);
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

#define wrmsrl(msr, val)						\
	native_write_msr(msr, (u32)((u64)(val)), (u32)((u64)(val) >> 32))

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

#define rdpmc(counter,low,high)					\
	do {							\
		u64 _l = native_read_pmc(counter);		\
		(low)  = (u32)_l;				\
		(high) = (u32)(_l >> 32);			\
	} while(0)

#define rdtscp(low, high, aux)						\
       do {                                                            \
		unsigned long long _val = native_read_tscp(&(aux));     \
		(low) = (u32)_val;                                      \
		(high) = (u32)(_val >> 32);                             \
       } while (0)

#define rdtscpll(val, aux) (val) = native_read_tscp(&(aux))

#endif	/* !CONFIG_PARAVIRT */


#define checking_wrmsrl(msr,val) wrmsr_safe(msr,(u32)(val),(u32)((val)>>32))

#define write_tsc(val1,val2) wrmsr(0x10, val1, val2)

#define write_rdtscp_aux(val) wrmsr(0xc0000103, val, 0)

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
#endif /* __KERNEL__ */


#endif
