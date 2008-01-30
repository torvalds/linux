/*
 * Copyright (C) 1994 Linus Torvalds
 */

#ifndef __ASM_I386_PROCESSOR_H
#define __ASM_I386_PROCESSOR_H

#include <asm/vm86.h>
#include <asm/math_emu.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/types.h>
#include <asm/sigcontext.h>
#include <asm/cpufeature.h>
#include <asm/msr.h>
#include <asm/system.h>
#include <linux/cache.h>
#include <linux/threads.h>
#include <asm/percpu.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <asm/desc_defs.h>

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ void *pc; __asm__("movl $1f,%0\n1:":"=g" (pc)); pc; })

/*
 *  CPU type and hardware bug flags. Kept separately for each CPU.
 *  Members of this structure are referenced in head.S, so think twice
 *  before touching them. [mj]
 */

struct cpuinfo_x86 {
	__u8	x86;		/* CPU family */
	__u8	x86_vendor;	/* CPU vendor */
	__u8	x86_model;
	__u8	x86_mask;
	char	wp_works_ok;	/* It doesn't on 386's */
	char	hlt_works_ok;	/* Problems on some 486Dx4's and old 386's */
	char	hard_math;
	char	rfu;
       	int	cpuid_level;	/* Maximum supported CPUID level, -1=no CPUID */
	unsigned long	x86_capability[NCAPINTS];
	char	x86_vendor_id[16];
	char	x86_model_id[64];
	int 	x86_cache_size;  /* in KB - valid for CPUS which support this
				    call  */
	int 	x86_cache_alignment;	/* In bytes */
	char	fdiv_bug;
	char	f00f_bug;
	char	coma_bug;
	char	pad0;
	int	x86_power;
	unsigned long loops_per_jiffy;
#ifdef CONFIG_SMP
	cpumask_t llc_shared_map;	/* cpus sharing the last level cache */
#endif
	unsigned char x86_max_cores;	/* cpuid returned max cores value */
	unsigned char apicid;
	unsigned short x86_clflush_size;
#ifdef CONFIG_SMP
	unsigned char booted_cores;	/* number of cores as seen by OS */
	__u8 phys_proc_id; 		/* Physical processor id. */
	__u8 cpu_core_id;  		/* Core id */
	__u8 cpu_index;			/* index into per_cpu list */
#endif
} __attribute__((__aligned__(SMP_CACHE_BYTES)));

#define X86_VENDOR_INTEL 0
#define X86_VENDOR_CYRIX 1
#define X86_VENDOR_AMD 2
#define X86_VENDOR_UMC 3
#define X86_VENDOR_NEXGEN 4
#define X86_VENDOR_CENTAUR 5
#define X86_VENDOR_TRANSMETA 7
#define X86_VENDOR_NSC 8
#define X86_VENDOR_NUM 9
#define X86_VENDOR_UNKNOWN 0xff

/*
 * capabilities of CPUs
 */

extern struct cpuinfo_x86 boot_cpu_data;
extern struct cpuinfo_x86 new_cpu_data;
extern struct tss_struct doublefault_tss;
DECLARE_PER_CPU(struct tss_struct, init_tss);

#ifdef CONFIG_SMP
DECLARE_PER_CPU(struct cpuinfo_x86, cpu_info);
#define cpu_data(cpu)		per_cpu(cpu_info, cpu)
#define current_cpu_data	cpu_data(smp_processor_id())
#else
#define cpu_data(cpu)		boot_cpu_data
#define current_cpu_data	boot_cpu_data
#endif

/*
 * the following now lives in the per cpu area:
 * extern	int cpu_llc_id[NR_CPUS];
 */
DECLARE_PER_CPU(u8, cpu_llc_id);
extern char ignore_fpu_irq;

void __init cpu_detect(struct cpuinfo_x86 *c);

extern void identify_boot_cpu(void);
extern void identify_secondary_cpu(struct cpuinfo_x86 *);

#ifdef CONFIG_X86_HT
extern void detect_ht(struct cpuinfo_x86 *c);
#else
static inline void detect_ht(struct cpuinfo_x86 *c) {}
#endif

/* from system description table in BIOS.  Mostly for MCA use, but
others may find it useful. */
extern unsigned int machine_id;
extern unsigned int machine_submodel_id;
extern unsigned int BIOS_revision;
extern unsigned int mca_pentium_flag;

/*
 * User space process size: 3GB (default).
 */
#define TASK_SIZE	(PAGE_OFFSET)


/*
 * Size of io_bitmap.
 */
#define IO_BITMAP_BITS  65536
#define IO_BITMAP_BYTES (IO_BITMAP_BITS/8)
#define IO_BITMAP_LONGS (IO_BITMAP_BYTES/sizeof(long))
#define IO_BITMAP_OFFSET offsetof(struct tss_struct,io_bitmap)
#define INVALID_IO_BITMAP_OFFSET 0x8000
#define INVALID_IO_BITMAP_OFFSET_LAZY 0x9000

struct i387_fsave_struct {
	long	cwd;
	long	swd;
	long	twd;
	long	fip;
	long	fcs;
	long	foo;
	long	fos;
	long	st_space[20];	/* 8*10 bytes for each FP-reg = 80 bytes */
	long	status;		/* software status information */
};

struct i387_fxsave_struct {
	unsigned short	cwd;
	unsigned short	swd;
	unsigned short	twd;
	unsigned short	fop;
	long	fip;
	long	fcs;
	long	foo;
	long	fos;
	long	mxcsr;
	long	mxcsr_mask;
	long	st_space[32];	/* 8*16 bytes for each FP-reg = 128 bytes */
	long	xmm_space[32];	/* 8*16 bytes for each XMM-reg = 128 bytes */
	long	padding[56];
} __attribute__ ((aligned (16)));

struct i387_soft_struct {
	long	cwd;
	long	swd;
	long	twd;
	long	fip;
	long	fcs;
	long	foo;
	long	fos;
	long	st_space[20];	/* 8*10 bytes for each FP-reg = 80 bytes */
	unsigned char	ftop, changed, lookahead, no_update, rm, alimit;
	struct info	*info;
	unsigned long	entry_eip;
};

union i387_union {
	struct i387_fsave_struct	fsave;
	struct i387_fxsave_struct	fxsave;
	struct i387_soft_struct soft;
};

typedef struct {
	unsigned long seg;
} mm_segment_t;

struct thread_struct;

/* This is the TSS defined by the hardware. */
struct i386_hw_tss {
	unsigned short	back_link,__blh;
	unsigned long	sp0;
	unsigned short	ss0,__ss0h;
	unsigned long	sp1;
	unsigned short	ss1,__ss1h;	/* ss1 is used to cache MSR_IA32_SYSENTER_CS */
	unsigned long	sp2;
	unsigned short	ss2,__ss2h;
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

struct tss_struct {
	struct i386_hw_tss x86_tss;

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

#define ARCH_MIN_TASKALIGN	16

struct thread_struct {
/* cached TLS descriptors. */
	struct desc_struct tls_array[GDT_ENTRY_TLS_ENTRIES];
	unsigned long	sp0;
	unsigned long	sysenter_cs;
	unsigned long	ip;
	unsigned long	sp;
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
	union i387_union	i387;
/* virtual 86 mode info */
	struct vm86_struct __user * vm86_info;
	unsigned long		screen_bitmap;
	unsigned long		v86flags, v86mask, saved_sp0;
	unsigned int		saved_fs, saved_gs;
/* IO permissions */
	unsigned long	*io_bitmap_ptr;
 	unsigned long	iopl;
/* max allowed port in the bitmap, in bytes: */
	unsigned long	io_bitmap_max;
/* MSR_IA32_DEBUGCTLMSR value to switch in if TIF_DEBUGCTLMSR is set.  */
	unsigned long	debugctlmsr;
/* Debug Store - if not 0 points to a DS Save Area configuration;
 *               goes into MSR_IA32_DS_AREA */
	unsigned long	ds_area_msr;
};

#define INIT_THREAD  {							\
	.sp0 = sizeof(init_stack) + (long)&init_stack,			\
	.vm86_info = NULL,						\
	.sysenter_cs = __KERNEL_CS,					\
	.io_bitmap_ptr = NULL,						\
	.fs = __KERNEL_PERCPU,						\
}

/*
 * Note that the .io_bitmap member must be extra-big. This is because
 * the CPU will access an additional byte beyond the end of the IO
 * permission bitmap. The extra byte must be all 1 bits, and must
 * be within the limit.
 */
#define INIT_TSS  {							\
	.x86_tss = {							\
		.sp0		= sizeof(init_stack) + (long)&init_stack, \
		.ss0		= __KERNEL_DS,				\
		.ss1		= __KERNEL_CS,				\
		.io_bitmap_base	= INVALID_IO_BITMAP_OFFSET,		\
	 },								\
	.io_bitmap	= { [ 0 ... IO_BITMAP_LONGS] = ~0 },		\
}

#define start_thread(regs, new_eip, new_esp) do {		\
	__asm__("movl %0,%%gs": :"r" (0));			\
	regs->fs = 0;						\
	set_fs(USER_DS);					\
	regs->ds = __USER_DS;					\
	regs->es = __USER_DS;					\
	regs->ss = __USER_DS;					\
	regs->cs = __USER_CS;					\
	regs->ip = new_eip;					\
	regs->sp = new_esp;					\
} while (0)


extern unsigned long thread_saved_pc(struct task_struct *tsk);

#define THREAD_SIZE_LONGS      (THREAD_SIZE/sizeof(unsigned long))
#define KSTK_TOP(info)                                                 \
({                                                                     \
       unsigned long *__ptr = (unsigned long *)(info);                 \
       (unsigned long)(&__ptr[THREAD_SIZE_LONGS]);                     \
})

/*
 * The below -8 is to reserve 8 bytes on top of the ring0 stack.
 * This is necessary to guarantee that the entire "struct pt_regs"
 * is accessable even if the CPU haven't stored the SS/ESP registers
 * on the stack (interrupt gate does not save these registers
 * when switching to the same priv ring).
 * Therefore beware: accessing the ss/esp fields of the
 * "struct pt_regs" is possible, but they may contain the
 * completely wrong values.
 */
#define task_pt_regs(task)                                             \
({                                                                     \
       struct pt_regs *__regs__;                                       \
       __regs__ = (struct pt_regs *)(KSTK_TOP(task_stack_page(task))-8); \
       __regs__ - 1;                                                   \
})

#define KSTK_ESP(task) (task_pt_regs(task)->sp)

static inline void native_load_sp0(struct tss_struct *tss, struct thread_struct *thread)
{
	tss->x86_tss.sp0 = thread->sp0;
	/* This can only happen when SEP is enabled, no need to test "SEP"arately */
	if (unlikely(tss->x86_tss.ss1 != thread->sysenter_cs)) {
		tss->x86_tss.ss1 = thread->sysenter_cs;
		wrmsr(MSR_IA32_SYSENTER_CS, thread->sysenter_cs, 0);
	}
}

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else

static inline void load_sp0(struct tss_struct *tss, struct thread_struct *thread)
{
	native_load_sp0(tss, thread);
}
#endif /* CONFIG_PARAVIRT */

/* generic versions from gas */
#define GENERIC_NOP1	".byte 0x90\n"
#define GENERIC_NOP2    	".byte 0x89,0xf6\n"
#define GENERIC_NOP3        ".byte 0x8d,0x76,0x00\n"
#define GENERIC_NOP4        ".byte 0x8d,0x74,0x26,0x00\n"
#define GENERIC_NOP5        GENERIC_NOP1 GENERIC_NOP4
#define GENERIC_NOP6	".byte 0x8d,0xb6,0x00,0x00,0x00,0x00\n"
#define GENERIC_NOP7	".byte 0x8d,0xb4,0x26,0x00,0x00,0x00,0x00\n"
#define GENERIC_NOP8	GENERIC_NOP1 GENERIC_NOP7

/* Opteron nops */
#define K8_NOP1 GENERIC_NOP1
#define K8_NOP2	".byte 0x66,0x90\n" 
#define K8_NOP3	".byte 0x66,0x66,0x90\n" 
#define K8_NOP4	".byte 0x66,0x66,0x66,0x90\n" 
#define K8_NOP5	K8_NOP3 K8_NOP2 
#define K8_NOP6	K8_NOP3 K8_NOP3
#define K8_NOP7	K8_NOP4 K8_NOP3
#define K8_NOP8	K8_NOP4 K8_NOP4

/* K7 nops */
/* uses eax dependencies (arbitary choice) */
#define K7_NOP1  GENERIC_NOP1
#define K7_NOP2	".byte 0x8b,0xc0\n" 
#define K7_NOP3	".byte 0x8d,0x04,0x20\n"
#define K7_NOP4	".byte 0x8d,0x44,0x20,0x00\n"
#define K7_NOP5	K7_NOP4 ASM_NOP1
#define K7_NOP6	".byte 0x8d,0x80,0,0,0,0\n"
#define K7_NOP7        ".byte 0x8D,0x04,0x05,0,0,0,0\n"
#define K7_NOP8        K7_NOP7 ASM_NOP1

/* P6 nops */
/* uses eax dependencies (Intel-recommended choice) */
#define P6_NOP1	GENERIC_NOP1
#define P6_NOP2	".byte 0x66,0x90\n"
#define P6_NOP3	".byte 0x0f,0x1f,0x00\n"
#define P6_NOP4	".byte 0x0f,0x1f,0x40,0\n"
#define P6_NOP5	".byte 0x0f,0x1f,0x44,0x00,0\n"
#define P6_NOP6	".byte 0x66,0x0f,0x1f,0x44,0x00,0\n"
#define P6_NOP7	".byte 0x0f,0x1f,0x80,0,0,0,0\n"
#define P6_NOP8	".byte 0x0f,0x1f,0x84,0x00,0,0,0,0\n"

#ifdef CONFIG_MK8
#define ASM_NOP1 K8_NOP1
#define ASM_NOP2 K8_NOP2
#define ASM_NOP3 K8_NOP3
#define ASM_NOP4 K8_NOP4
#define ASM_NOP5 K8_NOP5
#define ASM_NOP6 K8_NOP6
#define ASM_NOP7 K8_NOP7
#define ASM_NOP8 K8_NOP8
#elif defined(CONFIG_MK7)
#define ASM_NOP1 K7_NOP1
#define ASM_NOP2 K7_NOP2
#define ASM_NOP3 K7_NOP3
#define ASM_NOP4 K7_NOP4
#define ASM_NOP5 K7_NOP5
#define ASM_NOP6 K7_NOP6
#define ASM_NOP7 K7_NOP7
#define ASM_NOP8 K7_NOP8
#elif defined(CONFIG_M686) || defined(CONFIG_MPENTIUMII) || \
      defined(CONFIG_MPENTIUMIII) || defined(CONFIG_MPENTIUMM) || \
      defined(CONFIG_MCORE2) || defined(CONFIG_PENTIUM4)
#define ASM_NOP1 P6_NOP1
#define ASM_NOP2 P6_NOP2
#define ASM_NOP3 P6_NOP3
#define ASM_NOP4 P6_NOP4
#define ASM_NOP5 P6_NOP5
#define ASM_NOP6 P6_NOP6
#define ASM_NOP7 P6_NOP7
#define ASM_NOP8 P6_NOP8
#else
#define ASM_NOP1 GENERIC_NOP1
#define ASM_NOP2 GENERIC_NOP2
#define ASM_NOP3 GENERIC_NOP3
#define ASM_NOP4 GENERIC_NOP4
#define ASM_NOP5 GENERIC_NOP5
#define ASM_NOP6 GENERIC_NOP6
#define ASM_NOP7 GENERIC_NOP7
#define ASM_NOP8 GENERIC_NOP8
#endif

#define ASM_NOP_MAX 8

/* Prefetch instructions for Pentium III and AMD Athlon */
/* It's not worth to care about 3dnow! prefetches for the K6
   because they are microcoded there and very slow.
   However we don't do prefetches for pre XP Athlons currently
   That should be fixed. */
static inline void prefetch(const void *x)
{
	alternative_input(ASM_NOP4,
			  "prefetchnta (%1)",
			  X86_FEATURE_XMM,
			  "r" (x));
}

#define ARCH_HAS_PREFETCH

/* 3dnow! prefetch to get an exclusive cache line. Useful for 
   spinlocks to avoid one state transition in the cache coherency protocol. */
static inline void prefetchw(const void *x)
{
	alternative_input(ASM_NOP4,
			  "prefetchw (%1)",
			  X86_FEATURE_3DNOW,
			  "r" (x));
}

extern void enable_sep_cpu(void);
extern int sysenter_setup(void);

/* Defined in head.S */
extern struct desc_ptr early_gdt_descr;

extern void cpu_set_gdt(int);
extern void switch_to_new_gdt(void);
extern void cpu_init(void);
extern void init_gdt(int cpu);

#endif /* __ASM_I386_PROCESSOR_H */
