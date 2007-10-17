/*
 * include/asm-x86_64/processor.h
 *
 * Copyright (C) 1994 Linus Torvalds
 */

#ifndef __ASM_X86_64_PROCESSOR_H
#define __ASM_X86_64_PROCESSOR_H

#include <asm/segment.h>
#include <asm/page.h>
#include <asm/types.h>
#include <asm/sigcontext.h>
#include <asm/cpufeature.h>
#include <linux/threads.h>
#include <asm/msr.h>
#include <asm/current.h>
#include <asm/system.h>
#include <asm/mmsegment.h>
#include <asm/percpu.h>
#include <linux/personality.h>
#include <linux/cpumask.h>
#include <asm/processor-flags.h>

#define TF_MASK		0x00000100
#define IF_MASK		0x00000200
#define IOPL_MASK	0x00003000
#define NT_MASK		0x00004000
#define VM_MASK		0x00020000
#define AC_MASK		0x00040000
#define VIF_MASK	0x00080000	/* virtual interrupt flag */
#define VIP_MASK	0x00100000	/* virtual interrupt pending */
#define ID_MASK		0x00200000

#define desc_empty(desc) \
               (!((desc)->a | (desc)->b))

#define desc_equal(desc1, desc2) \
               (((desc1)->a == (desc2)->a) && ((desc1)->b == (desc2)->b))

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ void *pc; asm volatile("leaq 1f(%%rip),%0\n1:":"=r"(pc)); pc; })

/*
 *  CPU type and hardware bug flags. Kept separately for each CPU.
 */

struct cpuinfo_x86 {
	__u8	x86;		/* CPU family */
	__u8	x86_vendor;	/* CPU vendor */
	__u8	x86_model;
	__u8	x86_mask;
	int	cpuid_level;	/* Maximum supported CPUID level, -1=no CPUID */
	__u32	x86_capability[NCAPINTS];
	char	x86_vendor_id[16];
	char	x86_model_id[64];
	int 	x86_cache_size;  /* in KB */
	int	x86_clflush_size;
	int	x86_cache_alignment;
	int	x86_tlbsize;	/* number of 4K pages in DTLB/ITLB combined(in pages)*/
        __u8    x86_virt_bits, x86_phys_bits;
	__u8	x86_max_cores;	/* cpuid returned max cores value */
        __u32   x86_power; 	
	__u32   extended_cpuid_level;	/* Max extended CPUID function supported */
	unsigned long loops_per_jiffy;
#ifdef CONFIG_SMP
	cpumask_t llc_shared_map;	/* cpus sharing the last level cache */
#endif
	__u8	apicid;
#ifdef CONFIG_SMP
	__u8	booted_cores;	/* number of cores as seen by OS */
	__u8	phys_proc_id;	/* Physical Processor id. */
	__u8	cpu_core_id;	/* Core id. */
#endif
} ____cacheline_aligned;

#define X86_VENDOR_INTEL 0
#define X86_VENDOR_CYRIX 1
#define X86_VENDOR_AMD 2
#define X86_VENDOR_UMC 3
#define X86_VENDOR_NEXGEN 4
#define X86_VENDOR_CENTAUR 5
#define X86_VENDOR_TRANSMETA 7
#define X86_VENDOR_NUM 8
#define X86_VENDOR_UNKNOWN 0xff

#ifdef CONFIG_SMP
extern struct cpuinfo_x86 cpu_data[];
#define current_cpu_data cpu_data[smp_processor_id()]
#else
#define cpu_data (&boot_cpu_data)
#define current_cpu_data boot_cpu_data
#endif

extern char ignore_irq13;

extern void identify_cpu(struct cpuinfo_x86 *);
extern void print_cpu_info(struct cpuinfo_x86 *);
extern void init_scattered_cpuid_features(struct cpuinfo_x86 *c);
extern unsigned int init_intel_cacheinfo(struct cpuinfo_x86 *c);
extern unsigned short num_cache_leaves;

/*
 * Save the cr4 feature set we're using (ie
 * Pentium 4MB enable and PPro Global page
 * enable), so that any CPU's that boot up
 * after us can get the correct flags.
 */
extern unsigned long mmu_cr4_features;

static inline void set_in_cr4 (unsigned long mask)
{
	mmu_cr4_features |= mask;
	__asm__("movq %%cr4,%%rax\n\t"
		"orq %0,%%rax\n\t"
		"movq %%rax,%%cr4\n"
		: : "irg" (mask)
		:"ax");
}

static inline void clear_in_cr4 (unsigned long mask)
{
	mmu_cr4_features &= ~mask;
	__asm__("movq %%cr4,%%rax\n\t"
		"andq %0,%%rax\n\t"
		"movq %%rax,%%cr4\n"
		: : "irg" (~mask)
		:"ax");
}


/*
 * User space process size. 47bits minus one guard page.
 */
#define TASK_SIZE64	(0x800000000000UL - 4096)

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define IA32_PAGE_OFFSET ((current->personality & ADDR_LIMIT_3GB) ? 0xc0000000 : 0xFFFFe000)

#define TASK_SIZE 		(test_thread_flag(TIF_IA32) ? IA32_PAGE_OFFSET : TASK_SIZE64)
#define TASK_SIZE_OF(child) 	((test_tsk_thread_flag(child, TIF_IA32)) ? IA32_PAGE_OFFSET : TASK_SIZE64)

#define TASK_UNMAPPED_BASE	PAGE_ALIGN(TASK_SIZE/3)

/*
 * Size of io_bitmap.
 */
#define IO_BITMAP_BITS  65536
#define IO_BITMAP_BYTES (IO_BITMAP_BITS/8)
#define IO_BITMAP_LONGS (IO_BITMAP_BYTES/sizeof(long))
#define IO_BITMAP_OFFSET offsetof(struct tss_struct,io_bitmap)
#define INVALID_IO_BITMAP_OFFSET 0x8000

struct i387_fxsave_struct {
	u16	cwd;
	u16	swd;
	u16	twd;
	u16	fop;
	u64	rip;
	u64	rdp; 
	u32	mxcsr;
	u32	mxcsr_mask;
	u32	st_space[32];	/* 8*16 bytes for each FP-reg = 128 bytes */
	u32	xmm_space[64];	/* 16*16 bytes for each XMM-reg = 256 bytes */
	u32	padding[24];
} __attribute__ ((aligned (16)));

union i387_union {
	struct i387_fxsave_struct	fxsave;
};

struct tss_struct {
	u32 reserved1;
	u64 rsp0;	
	u64 rsp1;
	u64 rsp2;
	u64 reserved2;
	u64 ist[7];
	u32 reserved3;
	u32 reserved4;
	u16 reserved5;
	u16 io_bitmap_base;
	/*
	 * The extra 1 is there because the CPU will access an
	 * additional byte beyond the end of the IO permission
	 * bitmap. The extra byte must be all 1 bits, and must
	 * be within the limit. Thus we have:
	 *
	 * 128 bytes, the bitmap itself, for ports 0..0x3ff
	 * 8 bytes, for an extra "long" of ~0UL
	 */
	unsigned long io_bitmap[IO_BITMAP_LONGS + 1];
} __attribute__((packed)) ____cacheline_aligned;


extern struct cpuinfo_x86 boot_cpu_data;
DECLARE_PER_CPU(struct tss_struct,init_tss);
/* Save the original ist values for checking stack pointers during debugging */
struct orig_ist {
	unsigned long ist[7];
};
DECLARE_PER_CPU(struct orig_ist, orig_ist);

#ifdef CONFIG_X86_VSMP
#define ARCH_MIN_TASKALIGN	(1 << INTERNODE_CACHE_SHIFT)
#define ARCH_MIN_MMSTRUCT_ALIGN	(1 << INTERNODE_CACHE_SHIFT)
#else
#define ARCH_MIN_TASKALIGN	16
#define ARCH_MIN_MMSTRUCT_ALIGN	0
#endif

struct thread_struct {
	unsigned long	rsp0;
	unsigned long	rsp;
	unsigned long 	userrsp;	/* Copy from PDA */ 
	unsigned long	fs;
	unsigned long	gs;
	unsigned short	es, ds, fsindex, gsindex;	
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
	union i387_union	i387  __attribute__((aligned(16)));
/* IO permissions. the bitmap could be moved into the GDT, that would make
   switch faster for a limited number of ioperm using tasks. -AK */
	int		ioperm;
	unsigned long	*io_bitmap_ptr;
	unsigned io_bitmap_max;
/* cached TLS descriptors. */
	u64 tls_array[GDT_ENTRY_TLS_ENTRIES];
} __attribute__((aligned(16)));

#define INIT_THREAD  { \
	.rsp0 = (unsigned long)&init_stack + sizeof(init_stack) \
}

#define INIT_TSS  { \
	.rsp0 = (unsigned long)&init_stack + sizeof(init_stack) \
}

#define INIT_MMAP \
{ &init_mm, 0, 0, NULL, PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC, 1, NULL, NULL }

#define start_thread(regs,new_rip,new_rsp) do { \
	asm volatile("movl %0,%%fs; movl %0,%%es; movl %0,%%ds": :"r" (0));	 \
	load_gs_index(0);							\
	(regs)->rip = (new_rip);						 \
	(regs)->rsp = (new_rsp);						 \
	write_pda(oldrsp, (new_rsp));						 \
	(regs)->cs = __USER_CS;							 \
	(regs)->ss = __USER_DS;							 \
	(regs)->eflags = 0x200;							 \
	set_fs(USER_DS);							 \
} while(0) 

#define get_debugreg(var, register)				\
		__asm__("movq %%db" #register ", %0"		\
			:"=r" (var))
#define set_debugreg(value, register)			\
		__asm__("movq %0,%%db" #register		\
			: /* no output */			\
			:"r" (value))

struct task_struct;
struct mm_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/* Prepare to copy thread state - unlazy all lazy status */
extern void prepare_to_copy(struct task_struct *tsk);

/*
 * create a kernel thread without removing it from tasklists
 */
extern long kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

/*
 * Return saved PC of a blocked thread.
 * What is this good for? it will be always the scheduler or ret_from_fork.
 */
#define thread_saved_pc(t) (*(unsigned long *)((t)->thread.rsp - 8))

extern unsigned long get_wchan(struct task_struct *p);
#define task_pt_regs(tsk) ((struct pt_regs *)(tsk)->thread.rsp0 - 1)
#define KSTK_EIP(tsk) (task_pt_regs(tsk)->rip)
#define KSTK_ESP(tsk) -1 /* sorry. doesn't work for syscall. */


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


#if defined(CONFIG_MPSC) || defined(CONFIG_MCORE2)
#define ASM_NOP1 P6_NOP1
#define ASM_NOP2 P6_NOP2
#define ASM_NOP3 P6_NOP3
#define ASM_NOP4 P6_NOP4
#define ASM_NOP5 P6_NOP5
#define ASM_NOP6 P6_NOP6
#define ASM_NOP7 P6_NOP7
#define ASM_NOP8 P6_NOP8
#else
#define ASM_NOP1 K8_NOP1
#define ASM_NOP2 K8_NOP2
#define ASM_NOP3 K8_NOP3
#define ASM_NOP4 K8_NOP4
#define ASM_NOP5 K8_NOP5
#define ASM_NOP6 K8_NOP6
#define ASM_NOP7 K8_NOP7
#define ASM_NOP8 K8_NOP8
#endif

/* Opteron nops */
#define K8_NOP1 ".byte 0x90\n"
#define K8_NOP2	".byte 0x66,0x90\n" 
#define K8_NOP3	".byte 0x66,0x66,0x90\n" 
#define K8_NOP4	".byte 0x66,0x66,0x66,0x90\n" 
#define K8_NOP5	K8_NOP3 K8_NOP2 
#define K8_NOP6	K8_NOP3 K8_NOP3
#define K8_NOP7	K8_NOP4 K8_NOP3
#define K8_NOP8	K8_NOP4 K8_NOP4

/* P6 nops */
/* uses eax dependencies (Intel-recommended choice) */
#define P6_NOP1	".byte 0x90\n"
#define P6_NOP2	".byte 0x66,0x90\n"
#define P6_NOP3	".byte 0x0f,0x1f,0x00\n"
#define P6_NOP4	".byte 0x0f,0x1f,0x40,0\n"
#define P6_NOP5	".byte 0x0f,0x1f,0x44,0x00,0\n"
#define P6_NOP6	".byte 0x66,0x0f,0x1f,0x44,0x00,0\n"
#define P6_NOP7	".byte 0x0f,0x1f,0x80,0,0,0,0\n"
#define P6_NOP8	".byte 0x0f,0x1f,0x84,0x00,0,0,0,0\n"

#define ASM_NOP_MAX 8

/* REP NOP (PAUSE) is a good thing to insert into busy-wait loops. */
static inline void rep_nop(void)
{
	__asm__ __volatile__("rep;nop": : :"memory");
}

/* Stop speculative execution */
static inline void sync_core(void)
{ 
	int tmp;
	asm volatile("cpuid" : "=a" (tmp) : "0" (1) : "ebx","ecx","edx","memory");
} 

#define ARCH_HAS_PREFETCH
static inline void prefetch(void *x) 
{ 
	asm volatile("prefetcht0 (%0)" :: "r" (x));
} 

#define ARCH_HAS_PREFETCHW 1
static inline void prefetchw(void *x) 
{ 
	alternative_input("prefetcht0 (%1)",
			  "prefetchw (%1)",
			  X86_FEATURE_3DNOW,
			  "r" (x));
} 

#define ARCH_HAS_SPINLOCK_PREFETCH 1

#define spin_lock_prefetch(x)  prefetchw(x)

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

#define stack_current() \
({								\
	struct thread_info *ti;					\
	asm("andq %%rsp,%0; ":"=r" (ti) : "0" (CURRENT_MASK));	\
	ti->task;					\
})

#define cache_line_size() (boot_cpu_data.x86_cache_alignment)

extern unsigned long boot_option_idle_override;
/* Boot loader type from the setup header */
extern int bootloader_type;

#define HAVE_ARCH_PICK_MMAP_LAYOUT 1

#endif /* __ASM_X86_64_PROCESSOR_H */
