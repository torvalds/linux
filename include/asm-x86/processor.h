#ifndef __ASM_X86_PROCESSOR_H
#define __ASM_X86_PROCESSOR_H

#include <asm/processor-flags.h>

/* Forward declaration, a strange C thing */
struct task_struct;
struct mm_struct;

#include <asm/page.h>
#include <asm/percpu.h>
#include <asm/system.h>

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
static inline void *current_text_addr(void)
{
	void *pc;
	asm volatile("mov $1f,%0\n1:":"=r" (pc));
	return pc;
}

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
/* This is the TSS defined by the hardware. */
struct x86_hw_tss {
	unsigned short	back_link, __blh;
	unsigned long	sp0;
	unsigned short	ss0, __ss0h;
	unsigned long	sp1;
	unsigned short	ss1, __ss1h;	/* ss1 caches MSR_IA32_SYSENTER_CS */
	unsigned long	sp2;
	unsigned short	ss2, __ss2h;
	unsigned long	__cr3;
	unsigned long	ip;
	unsigned long	flags;
	unsigned long	ax, cx, dx, bx;
	unsigned long	sp, bp, si, di;
	unsigned short	es, __esh;
	unsigned short	cs, __csh;
	unsigned short	ss, __ssh;
	unsigned short	ds, __dsh;
	unsigned short	fs, __fsh;
	unsigned short	gs, __gsh;
	unsigned short	ldt, __ldth;
	unsigned short	trace, io_bitmap_base;
} __attribute__((packed));
#else
struct x86_hw_tss {
	u32 reserved1;
	u64 sp0;
	u64 sp1;
	u64 sp2;
	u64 reserved2;
	u64 ist[7];
	u32 reserved3;
	u32 reserved4;
	u16 reserved5;
	u16 io_bitmap_base;
} __attribute__((packed)) ____cacheline_aligned;
#endif

/*
 * Size of io_bitmap.
 */
#define IO_BITMAP_BITS  65536
#define IO_BITMAP_BYTES (IO_BITMAP_BITS/8)
#define IO_BITMAP_LONGS (IO_BITMAP_BYTES/sizeof(long))
#define IO_BITMAP_OFFSET offsetof(struct tss_struct, io_bitmap)
#define INVALID_IO_BITMAP_OFFSET 0x8000
#define INVALID_IO_BITMAP_OFFSET_LAZY 0x9000

struct tss_struct {
	struct x86_hw_tss x86_tss;

	/*
	 * The extra 1 is there because the CPU will access an
	 * additional byte beyond the end of the IO permission
	 * bitmap. The extra byte must be all 1 bits, and must
	 * be within the limit.
	 */
	unsigned long	io_bitmap[IO_BITMAP_LONGS + 1];
	/*
	 * Cache the current maximum and the last task that used the bitmap:
	 */
	unsigned long io_bitmap_max;
	struct thread_struct *io_bitmap_owner;
	/*
	 * pads the TSS to be cacheline-aligned (size is 0x100)
	 */
	unsigned long __cacheline_filler[35];
	/*
	 * .. and then another 0x100 bytes for emergency kernel stack
	 */
	unsigned long stack[64];
} __attribute__((packed));

DECLARE_PER_CPU(struct tss_struct, init_tss);

#ifdef CONFIG_X86_32
# include "processor_32.h"
#else
# include "processor_64.h"
#endif

extern void print_cpu_info(struct cpuinfo_x86 *);
extern void init_scattered_cpuid_features(struct cpuinfo_x86 *c);
extern unsigned int init_intel_cacheinfo(struct cpuinfo_x86 *c);
extern unsigned short num_cache_leaves;

struct thread_struct {
/* cached TLS descriptors. */
	struct desc_struct tls_array[GDT_ENTRY_TLS_ENTRIES];
	unsigned long	sp0;
	unsigned long	sp;
#ifdef CONFIG_X86_32
	unsigned long	sysenter_cs;
#else
	unsigned long 	usersp;	/* Copy from PDA */
	unsigned short	es, ds, fsindex, gsindex;
#endif
	unsigned long	ip;
	unsigned long	fs;
	unsigned long	gs;
/* Hardware debugging registers */
	unsigned long	debugreg0;
	unsigned long	debugreg1;
	unsigned long	debugreg2;
	unsigned long	debugreg3;
	unsigned long	debugreg6;
	unsigned long	debugreg7;
/* fault info */
	unsigned long	cr2, trap_no, error_code;
/* floating point info */
	union i387_union	i387 __attribute__((aligned(16)));;
#ifdef CONFIG_X86_32
/* virtual 86 mode info */
	struct vm86_struct __user *vm86_info;
	unsigned long		screen_bitmap;
	unsigned long		v86flags, v86mask, saved_sp0;
	unsigned int		saved_fs, saved_gs;
#endif
/* IO permissions */
	unsigned long	*io_bitmap_ptr;
	unsigned long	iopl;
/* max allowed port in the bitmap, in bytes: */
	unsigned io_bitmap_max;
/* MSR_IA32_DEBUGCTLMSR value to switch in if TIF_DEBUGCTLMSR is set.  */
	unsigned long	debugctlmsr;
/* Debug Store - if not 0 points to a DS Save Area configuration;
 *               goes into MSR_IA32_DS_AREA */
	unsigned long	ds_area_msr;
};

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

/*
 * Set IOPL bits in EFLAGS from given mask
 */
static inline void native_set_iopl_mask(unsigned mask)
{
#ifdef CONFIG_X86_32
	unsigned int reg;
	__asm__ __volatile__ ("pushfl;"
			      "popl %0;"
			      "andl %1, %0;"
			      "orl %2, %0;"
			      "pushl %0;"
			      "popfl"
				: "=&r" (reg)
				: "i" (~X86_EFLAGS_IOPL), "r" (mask));
#endif
}

static inline void native_load_sp0(struct tss_struct *tss,
				   struct thread_struct *thread)
{
	tss->x86_tss.sp0 = thread->sp0;
#ifdef CONFIG_X86_32
	/* Only happens when SEP is enabled, no need to test "SEP"arately */
	if (unlikely(tss->x86_tss.ss1 != thread->sysenter_cs)) {
		tss->x86_tss.ss1 = thread->sysenter_cs;
		wrmsr(MSR_IA32_SYSENTER_CS, thread->sysenter_cs, 0);
	}
#endif
}

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#define __cpuid native_cpuid
#define paravirt_enabled() 0

/*
 * These special macros can be used to get or set a debugging register
 */
#define get_debugreg(var, register)				\
	(var) = native_get_debugreg(register)
#define set_debugreg(value, register)				\
	native_set_debugreg(register, value)

static inline void load_sp0(struct tss_struct *tss,
			    struct thread_struct *thread)
{
	native_load_sp0(tss, thread);
}

#define set_iopl_mask native_set_iopl_mask
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

struct microcode_header {
	unsigned int hdrver;
	unsigned int rev;
	unsigned int date;
	unsigned int sig;
	unsigned int cksum;
	unsigned int ldrver;
	unsigned int pf;
	unsigned int datasize;
	unsigned int totalsize;
	unsigned int reserved[3];
};

struct microcode {
	struct microcode_header hdr;
	unsigned int bits[0];
};

typedef struct microcode microcode_t;
typedef struct microcode_header microcode_header_t;

/* microcode format is extended from prescott processors */
struct extended_signature {
	unsigned int sig;
	unsigned int pf;
	unsigned int cksum;
};

struct extended_sigtable {
	unsigned int count;
	unsigned int cksum;
	unsigned int reserved[3];
	struct extended_signature sigs[0];
};

/*
 * create a kernel thread without removing it from tasklists
 */
extern int kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/* Prepare to copy thread state - unlazy all lazy status */
extern void prepare_to_copy(struct task_struct *tsk);

unsigned long get_wchan(struct task_struct *p);

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

/* REP NOP (PAUSE) is a good thing to insert into busy-wait loops. */
static inline void rep_nop(void)
{
	__asm__ __volatile__("rep;nop": : :"memory");
}

/* Stop speculative execution */
static inline void sync_core(void)
{
	int tmp;
	asm volatile("cpuid" : "=a" (tmp) : "0" (1)
					  : "ebx", "ecx", "edx", "memory");
}

#define cpu_relax()   rep_nop()

static inline void __monitor(const void *eax, unsigned long ecx,
		unsigned long edx)
{
	/* "monitor %eax,%ecx,%edx;" */
	asm volatile(
		".byte 0x0f,0x01,0xc8;"
		: :"a" (eax), "c" (ecx), "d"(edx));
}

static inline void __mwait(unsigned long eax, unsigned long ecx)
{
	/* "mwait %eax,%ecx;" */
	asm volatile(
		".byte 0x0f,0x01,0xc9;"
		: :"a" (eax), "c" (ecx));
}

static inline void __sti_mwait(unsigned long eax, unsigned long ecx)
{
	/* "mwait %eax,%ecx;" */
	asm volatile(
		"sti; .byte 0x0f,0x01,0xc9;"
		: :"a" (eax), "c" (ecx));
}

extern void mwait_idle_with_hints(unsigned long eax, unsigned long ecx);

extern int force_mwait;

extern void select_idle_routine(const struct cpuinfo_x86 *c);

extern unsigned long boot_option_idle_override;

/* Boot loader type from the setup header */
extern int bootloader_type;
#define cache_line_size() (boot_cpu_data.x86_cache_alignment)

#define HAVE_ARCH_PICK_MMAP_LAYOUT 1
#define ARCH_HAS_PREFETCHW
#define ARCH_HAS_SPINLOCK_PREFETCH

#define spin_lock_prefetch(x)	prefetchw(x)
/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE	(PAGE_ALIGN(TASK_SIZE / 3))

#define KSTK_EIP(task) (task_pt_regs(task)->ip)

#endif
