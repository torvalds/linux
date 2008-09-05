#ifndef ASM_X86__PARAVIRT_H
#define ASM_X86__PARAVIRT_H
/* Various instructions on x86 need to be replaced for
 * para-virtualization: those hooks are defined here. */

#ifdef CONFIG_PARAVIRT
#include <asm/page.h>
#include <asm/asm.h>

/* Bitmask of what can be clobbered: usually at least eax. */
#define CLBR_NONE 0
#define CLBR_EAX  (1 << 0)
#define CLBR_ECX  (1 << 1)
#define CLBR_EDX  (1 << 2)

#ifdef CONFIG_X86_64
#define CLBR_RSI  (1 << 3)
#define CLBR_RDI  (1 << 4)
#define CLBR_R8   (1 << 5)
#define CLBR_R9   (1 << 6)
#define CLBR_R10  (1 << 7)
#define CLBR_R11  (1 << 8)
#define CLBR_ANY  ((1 << 9) - 1)
#include <asm/desc_defs.h>
#else
/* CLBR_ANY should match all regs platform has. For i386, that's just it */
#define CLBR_ANY  ((1 << 3) - 1)
#endif /* X86_64 */

#ifndef __ASSEMBLY__
#include <linux/types.h>
#include <linux/cpumask.h>
#include <asm/kmap_types.h>
#include <asm/desc_defs.h>

struct page;
struct thread_struct;
struct desc_ptr;
struct tss_struct;
struct mm_struct;
struct desc_struct;

/* general info */
struct pv_info {
	unsigned int kernel_rpl;
	int shared_kernel_pmd;
	int paravirt_enabled;
	const char *name;
};

struct pv_init_ops {
	/*
	 * Patch may replace one of the defined code sequences with
	 * arbitrary code, subject to the same register constraints.
	 * This generally means the code is not free to clobber any
	 * registers other than EAX.  The patch function should return
	 * the number of bytes of code generated, as we nop pad the
	 * rest in generic code.
	 */
	unsigned (*patch)(u8 type, u16 clobber, void *insnbuf,
			  unsigned long addr, unsigned len);

	/* Basic arch-specific setup */
	void (*arch_setup)(void);
	char *(*memory_setup)(void);
	void (*post_allocator_init)(void);

	/* Print a banner to identify the environment */
	void (*banner)(void);
};


struct pv_lazy_ops {
	/* Set deferred update mode, used for batching operations. */
	void (*enter)(void);
	void (*leave)(void);
};

struct pv_time_ops {
	void (*time_init)(void);

	/* Set and set time of day */
	unsigned long (*get_wallclock)(void);
	int (*set_wallclock)(unsigned long);

	unsigned long long (*sched_clock)(void);
	unsigned long (*get_tsc_khz)(void);
};

struct pv_cpu_ops {
	/* hooks for various privileged instructions */
	unsigned long (*get_debugreg)(int regno);
	void (*set_debugreg)(int regno, unsigned long value);

	void (*clts)(void);

	unsigned long (*read_cr0)(void);
	void (*write_cr0)(unsigned long);

	unsigned long (*read_cr4_safe)(void);
	unsigned long (*read_cr4)(void);
	void (*write_cr4)(unsigned long);

#ifdef CONFIG_X86_64
	unsigned long (*read_cr8)(void);
	void (*write_cr8)(unsigned long);
#endif

	/* Segment descriptor handling */
	void (*load_tr_desc)(void);
	void (*load_gdt)(const struct desc_ptr *);
	void (*load_idt)(const struct desc_ptr *);
	void (*store_gdt)(struct desc_ptr *);
	void (*store_idt)(struct desc_ptr *);
	void (*set_ldt)(const void *desc, unsigned entries);
	unsigned long (*store_tr)(void);
	void (*load_tls)(struct thread_struct *t, unsigned int cpu);
#ifdef CONFIG_X86_64
	void (*load_gs_index)(unsigned int idx);
#endif
	void (*write_ldt_entry)(struct desc_struct *ldt, int entrynum,
				const void *desc);
	void (*write_gdt_entry)(struct desc_struct *,
				int entrynum, const void *desc, int size);
	void (*write_idt_entry)(gate_desc *,
				int entrynum, const gate_desc *gate);
	void (*load_sp0)(struct tss_struct *tss, struct thread_struct *t);

	void (*set_iopl_mask)(unsigned mask);

	void (*wbinvd)(void);
	void (*io_delay)(void);

	/* cpuid emulation, mostly so that caps bits can be disabled */
	void (*cpuid)(unsigned int *eax, unsigned int *ebx,
		      unsigned int *ecx, unsigned int *edx);

	/* MSR, PMC and TSR operations.
	   err = 0/-EFAULT.  wrmsr returns 0/-EFAULT. */
	u64 (*read_msr_amd)(unsigned int msr, int *err);
	u64 (*read_msr)(unsigned int msr, int *err);
	int (*write_msr)(unsigned int msr, unsigned low, unsigned high);

	u64 (*read_tsc)(void);
	u64 (*read_pmc)(int counter);
	unsigned long long (*read_tscp)(unsigned int *aux);

	/*
	 * Atomically enable interrupts and return to userspace.  This
	 * is only ever used to return to 32-bit processes; in a
	 * 64-bit kernel, it's used for 32-on-64 compat processes, but
	 * never native 64-bit processes.  (Jump, not call.)
	 */
	void (*irq_enable_sysexit)(void);

	/*
	 * Switch to usermode gs and return to 64-bit usermode using
	 * sysret.  Only used in 64-bit kernels to return to 64-bit
	 * processes.  Usermode register state, including %rsp, must
	 * already be restored.
	 */
	void (*usergs_sysret64)(void);

	/*
	 * Switch to usermode gs and return to 32-bit usermode using
	 * sysret.  Used to return to 32-on-64 compat processes.
	 * Other usermode register state, including %esp, must already
	 * be restored.
	 */
	void (*usergs_sysret32)(void);

	/* Normal iret.  Jump to this with the standard iret stack
	   frame set up. */
	void (*iret)(void);

	void (*swapgs)(void);

	struct pv_lazy_ops lazy_mode;
};

struct pv_irq_ops {
	void (*init_IRQ)(void);

	/*
	 * Get/set interrupt state.  save_fl and restore_fl are only
	 * expected to use X86_EFLAGS_IF; all other bits
	 * returned from save_fl are undefined, and may be ignored by
	 * restore_fl.
	 */
	unsigned long (*save_fl)(void);
	void (*restore_fl)(unsigned long);
	void (*irq_disable)(void);
	void (*irq_enable)(void);
	void (*safe_halt)(void);
	void (*halt)(void);

#ifdef CONFIG_X86_64
	void (*adjust_exception_frame)(void);
#endif
};

struct pv_apic_ops {
#ifdef CONFIG_X86_LOCAL_APIC
	void (*setup_boot_clock)(void);
	void (*setup_secondary_clock)(void);

	void (*startup_ipi_hook)(int phys_apicid,
				 unsigned long start_eip,
				 unsigned long start_esp);
#endif
};

struct pv_mmu_ops {
	/*
	 * Called before/after init_mm pagetable setup. setup_start
	 * may reset %cr3, and may pre-install parts of the pagetable;
	 * pagetable setup is expected to preserve any existing
	 * mapping.
	 */
	void (*pagetable_setup_start)(pgd_t *pgd_base);
	void (*pagetable_setup_done)(pgd_t *pgd_base);

	unsigned long (*read_cr2)(void);
	void (*write_cr2)(unsigned long);

	unsigned long (*read_cr3)(void);
	void (*write_cr3)(unsigned long);

	/*
	 * Hooks for intercepting the creation/use/destruction of an
	 * mm_struct.
	 */
	void (*activate_mm)(struct mm_struct *prev,
			    struct mm_struct *next);
	void (*dup_mmap)(struct mm_struct *oldmm,
			 struct mm_struct *mm);
	void (*exit_mmap)(struct mm_struct *mm);


	/* TLB operations */
	void (*flush_tlb_user)(void);
	void (*flush_tlb_kernel)(void);
	void (*flush_tlb_single)(unsigned long addr);
	void (*flush_tlb_others)(const cpumask_t *cpus, struct mm_struct *mm,
				 unsigned long va);

	/* Hooks for allocating and freeing a pagetable top-level */
	int  (*pgd_alloc)(struct mm_struct *mm);
	void (*pgd_free)(struct mm_struct *mm, pgd_t *pgd);

	/*
	 * Hooks for allocating/releasing pagetable pages when they're
	 * attached to a pagetable
	 */
	void (*alloc_pte)(struct mm_struct *mm, u32 pfn);
	void (*alloc_pmd)(struct mm_struct *mm, u32 pfn);
	void (*alloc_pmd_clone)(u32 pfn, u32 clonepfn, u32 start, u32 count);
	void (*alloc_pud)(struct mm_struct *mm, u32 pfn);
	void (*release_pte)(u32 pfn);
	void (*release_pmd)(u32 pfn);
	void (*release_pud)(u32 pfn);

	/* Pagetable manipulation functions */
	void (*set_pte)(pte_t *ptep, pte_t pteval);
	void (*set_pte_at)(struct mm_struct *mm, unsigned long addr,
			   pte_t *ptep, pte_t pteval);
	void (*set_pmd)(pmd_t *pmdp, pmd_t pmdval);
	void (*pte_update)(struct mm_struct *mm, unsigned long addr,
			   pte_t *ptep);
	void (*pte_update_defer)(struct mm_struct *mm,
				 unsigned long addr, pte_t *ptep);

	pte_t (*ptep_modify_prot_start)(struct mm_struct *mm, unsigned long addr,
					pte_t *ptep);
	void (*ptep_modify_prot_commit)(struct mm_struct *mm, unsigned long addr,
					pte_t *ptep, pte_t pte);

	pteval_t (*pte_val)(pte_t);
	pteval_t (*pte_flags)(pte_t);
	pte_t (*make_pte)(pteval_t pte);

	pgdval_t (*pgd_val)(pgd_t);
	pgd_t (*make_pgd)(pgdval_t pgd);

#if PAGETABLE_LEVELS >= 3
#ifdef CONFIG_X86_PAE
	void (*set_pte_atomic)(pte_t *ptep, pte_t pteval);
	void (*set_pte_present)(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep, pte_t pte);
	void (*pte_clear)(struct mm_struct *mm, unsigned long addr,
			  pte_t *ptep);
	void (*pmd_clear)(pmd_t *pmdp);

#endif	/* CONFIG_X86_PAE */

	void (*set_pud)(pud_t *pudp, pud_t pudval);

	pmdval_t (*pmd_val)(pmd_t);
	pmd_t (*make_pmd)(pmdval_t pmd);

#if PAGETABLE_LEVELS == 4
	pudval_t (*pud_val)(pud_t);
	pud_t (*make_pud)(pudval_t pud);

	void (*set_pgd)(pgd_t *pudp, pgd_t pgdval);
#endif	/* PAGETABLE_LEVELS == 4 */
#endif	/* PAGETABLE_LEVELS >= 3 */

#ifdef CONFIG_HIGHPTE
	void *(*kmap_atomic_pte)(struct page *page, enum km_type type);
#endif

	struct pv_lazy_ops lazy_mode;

	/* dom0 ops */

	/* Sometimes the physical address is a pfn, and sometimes its
	   an mfn.  We can tell which is which from the index. */
	void (*set_fixmap)(unsigned /* enum fixed_addresses */ idx,
			   unsigned long phys, pgprot_t flags);
};

struct raw_spinlock;
struct pv_lock_ops {
	int (*spin_is_locked)(struct raw_spinlock *lock);
	int (*spin_is_contended)(struct raw_spinlock *lock);
	void (*spin_lock)(struct raw_spinlock *lock);
	int (*spin_trylock)(struct raw_spinlock *lock);
	void (*spin_unlock)(struct raw_spinlock *lock);
};

/* This contains all the paravirt structures: we get a convenient
 * number for each function using the offset which we use to indicate
 * what to patch. */
struct paravirt_patch_template {
	struct pv_init_ops pv_init_ops;
	struct pv_time_ops pv_time_ops;
	struct pv_cpu_ops pv_cpu_ops;
	struct pv_irq_ops pv_irq_ops;
	struct pv_apic_ops pv_apic_ops;
	struct pv_mmu_ops pv_mmu_ops;
	struct pv_lock_ops pv_lock_ops;
};

extern struct pv_info pv_info;
extern struct pv_init_ops pv_init_ops;
extern struct pv_time_ops pv_time_ops;
extern struct pv_cpu_ops pv_cpu_ops;
extern struct pv_irq_ops pv_irq_ops;
extern struct pv_apic_ops pv_apic_ops;
extern struct pv_mmu_ops pv_mmu_ops;
extern struct pv_lock_ops pv_lock_ops;

#define PARAVIRT_PATCH(x)					\
	(offsetof(struct paravirt_patch_template, x) / sizeof(void *))

#define paravirt_type(op)				\
	[paravirt_typenum] "i" (PARAVIRT_PATCH(op)),	\
	[paravirt_opptr] "m" (op)
#define paravirt_clobber(clobber)		\
	[paravirt_clobber] "i" (clobber)

/*
 * Generate some code, and mark it as patchable by the
 * apply_paravirt() alternate instruction patcher.
 */
#define _paravirt_alt(insn_string, type, clobber)	\
	"771:\n\t" insn_string "\n" "772:\n"		\
	".pushsection .parainstructions,\"a\"\n"	\
	_ASM_ALIGN "\n"					\
	_ASM_PTR " 771b\n"				\
	"  .byte " type "\n"				\
	"  .byte 772b-771b\n"				\
	"  .short " clobber "\n"			\
	".popsection\n"

/* Generate patchable code, with the default asm parameters. */
#define paravirt_alt(insn_string)					\
	_paravirt_alt(insn_string, "%c[paravirt_typenum]", "%c[paravirt_clobber]")

/* Simple instruction patching code. */
#define DEF_NATIVE(ops, name, code) 					\
	extern const char start_##ops##_##name[], end_##ops##_##name[];	\
	asm("start_" #ops "_" #name ": " code "; end_" #ops "_" #name ":")

unsigned paravirt_patch_nop(void);
unsigned paravirt_patch_ignore(unsigned len);
unsigned paravirt_patch_call(void *insnbuf,
			     const void *target, u16 tgt_clobbers,
			     unsigned long addr, u16 site_clobbers,
			     unsigned len);
unsigned paravirt_patch_jmp(void *insnbuf, const void *target,
			    unsigned long addr, unsigned len);
unsigned paravirt_patch_default(u8 type, u16 clobbers, void *insnbuf,
				unsigned long addr, unsigned len);

unsigned paravirt_patch_insns(void *insnbuf, unsigned len,
			      const char *start, const char *end);

unsigned native_patch(u8 type, u16 clobbers, void *ibuf,
		      unsigned long addr, unsigned len);

int paravirt_disable_iospace(void);

/*
 * This generates an indirect call based on the operation type number.
 * The type number, computed in PARAVIRT_PATCH, is derived from the
 * offset into the paravirt_patch_template structure, and can therefore be
 * freely converted back into a structure offset.
 */
#define PARAVIRT_CALL	"call *%[paravirt_opptr];"

/*
 * These macros are intended to wrap calls through one of the paravirt
 * ops structs, so that they can be later identified and patched at
 * runtime.
 *
 * Normally, a call to a pv_op function is a simple indirect call:
 * (pv_op_struct.operations)(args...).
 *
 * Unfortunately, this is a relatively slow operation for modern CPUs,
 * because it cannot necessarily determine what the destination
 * address is.  In this case, the address is a runtime constant, so at
 * the very least we can patch the call to e a simple direct call, or
 * ideally, patch an inline implementation into the callsite.  (Direct
 * calls are essentially free, because the call and return addresses
 * are completely predictable.)
 *
 * For i386, these macros rely on the standard gcc "regparm(3)" calling
 * convention, in which the first three arguments are placed in %eax,
 * %edx, %ecx (in that order), and the remaining arguments are placed
 * on the stack.  All caller-save registers (eax,edx,ecx) are expected
 * to be modified (either clobbered or used for return values).
 * X86_64, on the other hand, already specifies a register-based calling
 * conventions, returning at %rax, with parameteres going on %rdi, %rsi,
 * %rdx, and %rcx. Note that for this reason, x86_64 does not need any
 * special handling for dealing with 4 arguments, unlike i386.
 * However, x86_64 also have to clobber all caller saved registers, which
 * unfortunately, are quite a bit (r8 - r11)
 *
 * The call instruction itself is marked by placing its start address
 * and size into the .parainstructions section, so that
 * apply_paravirt() in arch/i386/kernel/alternative.c can do the
 * appropriate patching under the control of the backend pv_init_ops
 * implementation.
 *
 * Unfortunately there's no way to get gcc to generate the args setup
 * for the call, and then allow the call itself to be generated by an
 * inline asm.  Because of this, we must do the complete arg setup and
 * return value handling from within these macros.  This is fairly
 * cumbersome.
 *
 * There are 5 sets of PVOP_* macros for dealing with 0-4 arguments.
 * It could be extended to more arguments, but there would be little
 * to be gained from that.  For each number of arguments, there are
 * the two VCALL and CALL variants for void and non-void functions.
 *
 * When there is a return value, the invoker of the macro must specify
 * the return type.  The macro then uses sizeof() on that type to
 * determine whether its a 32 or 64 bit value, and places the return
 * in the right register(s) (just %eax for 32-bit, and %edx:%eax for
 * 64-bit). For x86_64 machines, it just returns at %rax regardless of
 * the return value size.
 *
 * 64-bit arguments are passed as a pair of adjacent 32-bit arguments
 * i386 also passes 64-bit arguments as a pair of adjacent 32-bit arguments
 * in low,high order
 *
 * Small structures are passed and returned in registers.  The macro
 * calling convention can't directly deal with this, so the wrapper
 * functions must do this.
 *
 * These PVOP_* macros are only defined within this header.  This
 * means that all uses must be wrapped in inline functions.  This also
 * makes sure the incoming and outgoing types are always correct.
 */
#ifdef CONFIG_X86_32
#define PVOP_VCALL_ARGS			unsigned long __eax, __edx, __ecx
#define PVOP_CALL_ARGS			PVOP_VCALL_ARGS
#define PVOP_VCALL_CLOBBERS		"=a" (__eax), "=d" (__edx),	\
					"=c" (__ecx)
#define PVOP_CALL_CLOBBERS		PVOP_VCALL_CLOBBERS
#define EXTRA_CLOBBERS
#define VEXTRA_CLOBBERS
#else
#define PVOP_VCALL_ARGS		unsigned long __edi, __esi, __edx, __ecx
#define PVOP_CALL_ARGS		PVOP_VCALL_ARGS, __eax
#define PVOP_VCALL_CLOBBERS	"=D" (__edi),				\
				"=S" (__esi), "=d" (__edx),		\
				"=c" (__ecx)

#define PVOP_CALL_CLOBBERS	PVOP_VCALL_CLOBBERS, "=a" (__eax)

#define EXTRA_CLOBBERS	 , "r8", "r9", "r10", "r11"
#define VEXTRA_CLOBBERS	 , "rax", "r8", "r9", "r10", "r11"
#endif

#ifdef CONFIG_PARAVIRT_DEBUG
#define PVOP_TEST_NULL(op)	BUG_ON(op == NULL)
#else
#define PVOP_TEST_NULL(op)	((void)op)
#endif

#define __PVOP_CALL(rettype, op, pre, post, ...)			\
	({								\
		rettype __ret;						\
		PVOP_CALL_ARGS;					\
		PVOP_TEST_NULL(op);					\
		/* This is 32-bit specific, but is okay in 64-bit */	\
		/* since this condition will never hold */		\
		if (sizeof(rettype) > sizeof(unsigned long)) {		\
			asm volatile(pre				\
				     paravirt_alt(PARAVIRT_CALL)	\
				     post				\
				     : PVOP_CALL_CLOBBERS		\
				     : paravirt_type(op),		\
				       paravirt_clobber(CLBR_ANY),	\
				       ##__VA_ARGS__			\
				     : "memory", "cc" EXTRA_CLOBBERS);	\
			__ret = (rettype)((((u64)__edx) << 32) | __eax); \
		} else {						\
			asm volatile(pre				\
				     paravirt_alt(PARAVIRT_CALL)	\
				     post				\
				     : PVOP_CALL_CLOBBERS		\
				     : paravirt_type(op),		\
				       paravirt_clobber(CLBR_ANY),	\
				       ##__VA_ARGS__			\
				     : "memory", "cc" EXTRA_CLOBBERS);	\
			__ret = (rettype)__eax;				\
		}							\
		__ret;							\
	})
#define __PVOP_VCALL(op, pre, post, ...)				\
	({								\
		PVOP_VCALL_ARGS;					\
		PVOP_TEST_NULL(op);					\
		asm volatile(pre					\
			     paravirt_alt(PARAVIRT_CALL)		\
			     post					\
			     : PVOP_VCALL_CLOBBERS			\
			     : paravirt_type(op),			\
			       paravirt_clobber(CLBR_ANY),		\
			       ##__VA_ARGS__				\
			     : "memory", "cc" VEXTRA_CLOBBERS);		\
	})

#define PVOP_CALL0(rettype, op)						\
	__PVOP_CALL(rettype, op, "", "")
#define PVOP_VCALL0(op)							\
	__PVOP_VCALL(op, "", "")

#define PVOP_CALL1(rettype, op, arg1)					\
	__PVOP_CALL(rettype, op, "", "", "0" ((unsigned long)(arg1)))
#define PVOP_VCALL1(op, arg1)						\
	__PVOP_VCALL(op, "", "", "0" ((unsigned long)(arg1)))

#define PVOP_CALL2(rettype, op, arg1, arg2)				\
	__PVOP_CALL(rettype, op, "", "", "0" ((unsigned long)(arg1)), 	\
	"1" ((unsigned long)(arg2)))
#define PVOP_VCALL2(op, arg1, arg2)					\
	__PVOP_VCALL(op, "", "", "0" ((unsigned long)(arg1)), 		\
	"1" ((unsigned long)(arg2)))

#define PVOP_CALL3(rettype, op, arg1, arg2, arg3)			\
	__PVOP_CALL(rettype, op, "", "", "0" ((unsigned long)(arg1)),	\
	"1"((unsigned long)(arg2)), "2"((unsigned long)(arg3)))
#define PVOP_VCALL3(op, arg1, arg2, arg3)				\
	__PVOP_VCALL(op, "", "", "0" ((unsigned long)(arg1)),		\
	"1"((unsigned long)(arg2)), "2"((unsigned long)(arg3)))

/* This is the only difference in x86_64. We can make it much simpler */
#ifdef CONFIG_X86_32
#define PVOP_CALL4(rettype, op, arg1, arg2, arg3, arg4)			\
	__PVOP_CALL(rettype, op,					\
		    "push %[_arg4];", "lea 4(%%esp),%%esp;",		\
		    "0" ((u32)(arg1)), "1" ((u32)(arg2)),		\
		    "2" ((u32)(arg3)), [_arg4] "mr" ((u32)(arg4)))
#define PVOP_VCALL4(op, arg1, arg2, arg3, arg4)				\
	__PVOP_VCALL(op,						\
		    "push %[_arg4];", "lea 4(%%esp),%%esp;",		\
		    "0" ((u32)(arg1)), "1" ((u32)(arg2)),		\
		    "2" ((u32)(arg3)), [_arg4] "mr" ((u32)(arg4)))
#else
#define PVOP_CALL4(rettype, op, arg1, arg2, arg3, arg4)			\
	__PVOP_CALL(rettype, op, "", "", "0" ((unsigned long)(arg1)),	\
	"1"((unsigned long)(arg2)), "2"((unsigned long)(arg3)),		\
	"3"((unsigned long)(arg4)))
#define PVOP_VCALL4(op, arg1, arg2, arg3, arg4)				\
	__PVOP_VCALL(op, "", "", "0" ((unsigned long)(arg1)),		\
	"1"((unsigned long)(arg2)), "2"((unsigned long)(arg3)),		\
	"3"((unsigned long)(arg4)))
#endif

static inline int paravirt_enabled(void)
{
	return pv_info.paravirt_enabled;
}

static inline void load_sp0(struct tss_struct *tss,
			     struct thread_struct *thread)
{
	PVOP_VCALL2(pv_cpu_ops.load_sp0, tss, thread);
}

#define ARCH_SETUP			pv_init_ops.arch_setup();
static inline unsigned long get_wallclock(void)
{
	return PVOP_CALL0(unsigned long, pv_time_ops.get_wallclock);
}

static inline int set_wallclock(unsigned long nowtime)
{
	return PVOP_CALL1(int, pv_time_ops.set_wallclock, nowtime);
}

static inline void (*choose_time_init(void))(void)
{
	return pv_time_ops.time_init;
}

/* The paravirtualized CPUID instruction. */
static inline void __cpuid(unsigned int *eax, unsigned int *ebx,
			   unsigned int *ecx, unsigned int *edx)
{
	PVOP_VCALL4(pv_cpu_ops.cpuid, eax, ebx, ecx, edx);
}

/*
 * These special macros can be used to get or set a debugging register
 */
static inline unsigned long paravirt_get_debugreg(int reg)
{
	return PVOP_CALL1(unsigned long, pv_cpu_ops.get_debugreg, reg);
}
#define get_debugreg(var, reg) var = paravirt_get_debugreg(reg)
static inline void set_debugreg(unsigned long val, int reg)
{
	PVOP_VCALL2(pv_cpu_ops.set_debugreg, reg, val);
}

static inline void clts(void)
{
	PVOP_VCALL0(pv_cpu_ops.clts);
}

static inline unsigned long read_cr0(void)
{
	return PVOP_CALL0(unsigned long, pv_cpu_ops.read_cr0);
}

static inline void write_cr0(unsigned long x)
{
	PVOP_VCALL1(pv_cpu_ops.write_cr0, x);
}

static inline unsigned long read_cr2(void)
{
	return PVOP_CALL0(unsigned long, pv_mmu_ops.read_cr2);
}

static inline void write_cr2(unsigned long x)
{
	PVOP_VCALL1(pv_mmu_ops.write_cr2, x);
}

static inline unsigned long read_cr3(void)
{
	return PVOP_CALL0(unsigned long, pv_mmu_ops.read_cr3);
}

static inline void write_cr3(unsigned long x)
{
	PVOP_VCALL1(pv_mmu_ops.write_cr3, x);
}

static inline unsigned long read_cr4(void)
{
	return PVOP_CALL0(unsigned long, pv_cpu_ops.read_cr4);
}
static inline unsigned long read_cr4_safe(void)
{
	return PVOP_CALL0(unsigned long, pv_cpu_ops.read_cr4_safe);
}

static inline void write_cr4(unsigned long x)
{
	PVOP_VCALL1(pv_cpu_ops.write_cr4, x);
}

#ifdef CONFIG_X86_64
static inline unsigned long read_cr8(void)
{
	return PVOP_CALL0(unsigned long, pv_cpu_ops.read_cr8);
}

static inline void write_cr8(unsigned long x)
{
	PVOP_VCALL1(pv_cpu_ops.write_cr8, x);
}
#endif

static inline void raw_safe_halt(void)
{
	PVOP_VCALL0(pv_irq_ops.safe_halt);
}

static inline void halt(void)
{
	PVOP_VCALL0(pv_irq_ops.safe_halt);
}

static inline void wbinvd(void)
{
	PVOP_VCALL0(pv_cpu_ops.wbinvd);
}

#define get_kernel_rpl()  (pv_info.kernel_rpl)

static inline u64 paravirt_read_msr(unsigned msr, int *err)
{
	return PVOP_CALL2(u64, pv_cpu_ops.read_msr, msr, err);
}
static inline u64 paravirt_read_msr_amd(unsigned msr, int *err)
{
	return PVOP_CALL2(u64, pv_cpu_ops.read_msr_amd, msr, err);
}
static inline int paravirt_write_msr(unsigned msr, unsigned low, unsigned high)
{
	return PVOP_CALL3(int, pv_cpu_ops.write_msr, msr, low, high);
}

/* These should all do BUG_ON(_err), but our headers are too tangled. */
#define rdmsr(msr, val1, val2)			\
do {						\
	int _err;				\
	u64 _l = paravirt_read_msr(msr, &_err);	\
	val1 = (u32)_l;				\
	val2 = _l >> 32;			\
} while (0)

#define wrmsr(msr, val1, val2)			\
do {						\
	paravirt_write_msr(msr, val1, val2);	\
} while (0)

#define rdmsrl(msr, val)			\
do {						\
	int _err;				\
	val = paravirt_read_msr(msr, &_err);	\
} while (0)

#define wrmsrl(msr, val)	wrmsr(msr, (u32)((u64)(val)), ((u64)(val))>>32)
#define wrmsr_safe(msr, a, b)	paravirt_write_msr(msr, a, b)

/* rdmsr with exception handling */
#define rdmsr_safe(msr, a, b)			\
({						\
	int _err;				\
	u64 _l = paravirt_read_msr(msr, &_err);	\
	(*a) = (u32)_l;				\
	(*b) = _l >> 32;			\
	_err;					\
})

static inline int rdmsrl_safe(unsigned msr, unsigned long long *p)
{
	int err;

	*p = paravirt_read_msr(msr, &err);
	return err;
}
static inline int rdmsrl_amd_safe(unsigned msr, unsigned long long *p)
{
	int err;

	*p = paravirt_read_msr_amd(msr, &err);
	return err;
}

static inline u64 paravirt_read_tsc(void)
{
	return PVOP_CALL0(u64, pv_cpu_ops.read_tsc);
}

#define rdtscl(low)				\
do {						\
	u64 _l = paravirt_read_tsc();		\
	low = (int)_l;				\
} while (0)

#define rdtscll(val) (val = paravirt_read_tsc())

static inline unsigned long long paravirt_sched_clock(void)
{
	return PVOP_CALL0(unsigned long long, pv_time_ops.sched_clock);
}
#define calibrate_tsc() (pv_time_ops.get_tsc_khz())

static inline unsigned long long paravirt_read_pmc(int counter)
{
	return PVOP_CALL1(u64, pv_cpu_ops.read_pmc, counter);
}

#define rdpmc(counter, low, high)		\
do {						\
	u64 _l = paravirt_read_pmc(counter);	\
	low = (u32)_l;				\
	high = _l >> 32;			\
} while (0)

static inline unsigned long long paravirt_rdtscp(unsigned int *aux)
{
	return PVOP_CALL1(u64, pv_cpu_ops.read_tscp, aux);
}

#define rdtscp(low, high, aux)				\
do {							\
	int __aux;					\
	unsigned long __val = paravirt_rdtscp(&__aux);	\
	(low) = (u32)__val;				\
	(high) = (u32)(__val >> 32);			\
	(aux) = __aux;					\
} while (0)

#define rdtscpll(val, aux)				\
do {							\
	unsigned long __aux; 				\
	val = paravirt_rdtscp(&__aux);			\
	(aux) = __aux;					\
} while (0)

static inline void load_TR_desc(void)
{
	PVOP_VCALL0(pv_cpu_ops.load_tr_desc);
}
static inline void load_gdt(const struct desc_ptr *dtr)
{
	PVOP_VCALL1(pv_cpu_ops.load_gdt, dtr);
}
static inline void load_idt(const struct desc_ptr *dtr)
{
	PVOP_VCALL1(pv_cpu_ops.load_idt, dtr);
}
static inline void set_ldt(const void *addr, unsigned entries)
{
	PVOP_VCALL2(pv_cpu_ops.set_ldt, addr, entries);
}
static inline void store_gdt(struct desc_ptr *dtr)
{
	PVOP_VCALL1(pv_cpu_ops.store_gdt, dtr);
}
static inline void store_idt(struct desc_ptr *dtr)
{
	PVOP_VCALL1(pv_cpu_ops.store_idt, dtr);
}
static inline unsigned long paravirt_store_tr(void)
{
	return PVOP_CALL0(unsigned long, pv_cpu_ops.store_tr);
}
#define store_tr(tr)	((tr) = paravirt_store_tr())
static inline void load_TLS(struct thread_struct *t, unsigned cpu)
{
	PVOP_VCALL2(pv_cpu_ops.load_tls, t, cpu);
}

#ifdef CONFIG_X86_64
static inline void load_gs_index(unsigned int gs)
{
	PVOP_VCALL1(pv_cpu_ops.load_gs_index, gs);
}
#endif

static inline void write_ldt_entry(struct desc_struct *dt, int entry,
				   const void *desc)
{
	PVOP_VCALL3(pv_cpu_ops.write_ldt_entry, dt, entry, desc);
}

static inline void write_gdt_entry(struct desc_struct *dt, int entry,
				   void *desc, int type)
{
	PVOP_VCALL4(pv_cpu_ops.write_gdt_entry, dt, entry, desc, type);
}

static inline void write_idt_entry(gate_desc *dt, int entry, const gate_desc *g)
{
	PVOP_VCALL3(pv_cpu_ops.write_idt_entry, dt, entry, g);
}
static inline void set_iopl_mask(unsigned mask)
{
	PVOP_VCALL1(pv_cpu_ops.set_iopl_mask, mask);
}

/* The paravirtualized I/O functions */
static inline void slow_down_io(void)
{
	pv_cpu_ops.io_delay();
#ifdef REALLY_SLOW_IO
	pv_cpu_ops.io_delay();
	pv_cpu_ops.io_delay();
	pv_cpu_ops.io_delay();
#endif
}

#ifdef CONFIG_X86_LOCAL_APIC
static inline void setup_boot_clock(void)
{
	PVOP_VCALL0(pv_apic_ops.setup_boot_clock);
}

static inline void setup_secondary_clock(void)
{
	PVOP_VCALL0(pv_apic_ops.setup_secondary_clock);
}
#endif

static inline void paravirt_post_allocator_init(void)
{
	if (pv_init_ops.post_allocator_init)
		(*pv_init_ops.post_allocator_init)();
}

static inline void paravirt_pagetable_setup_start(pgd_t *base)
{
	(*pv_mmu_ops.pagetable_setup_start)(base);
}

static inline void paravirt_pagetable_setup_done(pgd_t *base)
{
	(*pv_mmu_ops.pagetable_setup_done)(base);
}

#ifdef CONFIG_SMP
static inline void startup_ipi_hook(int phys_apicid, unsigned long start_eip,
				    unsigned long start_esp)
{
	PVOP_VCALL3(pv_apic_ops.startup_ipi_hook,
		    phys_apicid, start_eip, start_esp);
}
#endif

static inline void paravirt_activate_mm(struct mm_struct *prev,
					struct mm_struct *next)
{
	PVOP_VCALL2(pv_mmu_ops.activate_mm, prev, next);
}

static inline void arch_dup_mmap(struct mm_struct *oldmm,
				 struct mm_struct *mm)
{
	PVOP_VCALL2(pv_mmu_ops.dup_mmap, oldmm, mm);
}

static inline void arch_exit_mmap(struct mm_struct *mm)
{
	PVOP_VCALL1(pv_mmu_ops.exit_mmap, mm);
}

static inline void __flush_tlb(void)
{
	PVOP_VCALL0(pv_mmu_ops.flush_tlb_user);
}
static inline void __flush_tlb_global(void)
{
	PVOP_VCALL0(pv_mmu_ops.flush_tlb_kernel);
}
static inline void __flush_tlb_single(unsigned long addr)
{
	PVOP_VCALL1(pv_mmu_ops.flush_tlb_single, addr);
}

static inline void flush_tlb_others(cpumask_t cpumask, struct mm_struct *mm,
				    unsigned long va)
{
	PVOP_VCALL3(pv_mmu_ops.flush_tlb_others, &cpumask, mm, va);
}

static inline int paravirt_pgd_alloc(struct mm_struct *mm)
{
	return PVOP_CALL1(int, pv_mmu_ops.pgd_alloc, mm);
}

static inline void paravirt_pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	PVOP_VCALL2(pv_mmu_ops.pgd_free, mm, pgd);
}

static inline void paravirt_alloc_pte(struct mm_struct *mm, unsigned pfn)
{
	PVOP_VCALL2(pv_mmu_ops.alloc_pte, mm, pfn);
}
static inline void paravirt_release_pte(unsigned pfn)
{
	PVOP_VCALL1(pv_mmu_ops.release_pte, pfn);
}

static inline void paravirt_alloc_pmd(struct mm_struct *mm, unsigned pfn)
{
	PVOP_VCALL2(pv_mmu_ops.alloc_pmd, mm, pfn);
}

static inline void paravirt_alloc_pmd_clone(unsigned pfn, unsigned clonepfn,
					    unsigned start, unsigned count)
{
	PVOP_VCALL4(pv_mmu_ops.alloc_pmd_clone, pfn, clonepfn, start, count);
}
static inline void paravirt_release_pmd(unsigned pfn)
{
	PVOP_VCALL1(pv_mmu_ops.release_pmd, pfn);
}

static inline void paravirt_alloc_pud(struct mm_struct *mm, unsigned pfn)
{
	PVOP_VCALL2(pv_mmu_ops.alloc_pud, mm, pfn);
}
static inline void paravirt_release_pud(unsigned pfn)
{
	PVOP_VCALL1(pv_mmu_ops.release_pud, pfn);
}

#ifdef CONFIG_HIGHPTE
static inline void *kmap_atomic_pte(struct page *page, enum km_type type)
{
	unsigned long ret;
	ret = PVOP_CALL2(unsigned long, pv_mmu_ops.kmap_atomic_pte, page, type);
	return (void *)ret;
}
#endif

static inline void pte_update(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep)
{
	PVOP_VCALL3(pv_mmu_ops.pte_update, mm, addr, ptep);
}

static inline void pte_update_defer(struct mm_struct *mm, unsigned long addr,
				    pte_t *ptep)
{
	PVOP_VCALL3(pv_mmu_ops.pte_update_defer, mm, addr, ptep);
}

static inline pte_t __pte(pteval_t val)
{
	pteval_t ret;

	if (sizeof(pteval_t) > sizeof(long))
		ret = PVOP_CALL2(pteval_t,
				 pv_mmu_ops.make_pte,
				 val, (u64)val >> 32);
	else
		ret = PVOP_CALL1(pteval_t,
				 pv_mmu_ops.make_pte,
				 val);

	return (pte_t) { .pte = ret };
}

static inline pteval_t pte_val(pte_t pte)
{
	pteval_t ret;

	if (sizeof(pteval_t) > sizeof(long))
		ret = PVOP_CALL2(pteval_t, pv_mmu_ops.pte_val,
				 pte.pte, (u64)pte.pte >> 32);
	else
		ret = PVOP_CALL1(pteval_t, pv_mmu_ops.pte_val,
				 pte.pte);

	return ret;
}

static inline pteval_t pte_flags(pte_t pte)
{
	pteval_t ret;

	if (sizeof(pteval_t) > sizeof(long))
		ret = PVOP_CALL2(pteval_t, pv_mmu_ops.pte_flags,
				 pte.pte, (u64)pte.pte >> 32);
	else
		ret = PVOP_CALL1(pteval_t, pv_mmu_ops.pte_flags,
				 pte.pte);

#ifdef CONFIG_PARAVIRT_DEBUG
	BUG_ON(ret & PTE_PFN_MASK);
#endif
	return ret;
}

static inline pgd_t __pgd(pgdval_t val)
{
	pgdval_t ret;

	if (sizeof(pgdval_t) > sizeof(long))
		ret = PVOP_CALL2(pgdval_t, pv_mmu_ops.make_pgd,
				 val, (u64)val >> 32);
	else
		ret = PVOP_CALL1(pgdval_t, pv_mmu_ops.make_pgd,
				 val);

	return (pgd_t) { ret };
}

static inline pgdval_t pgd_val(pgd_t pgd)
{
	pgdval_t ret;

	if (sizeof(pgdval_t) > sizeof(long))
		ret =  PVOP_CALL2(pgdval_t, pv_mmu_ops.pgd_val,
				  pgd.pgd, (u64)pgd.pgd >> 32);
	else
		ret =  PVOP_CALL1(pgdval_t, pv_mmu_ops.pgd_val,
				  pgd.pgd);

	return ret;
}

#define  __HAVE_ARCH_PTEP_MODIFY_PROT_TRANSACTION
static inline pte_t ptep_modify_prot_start(struct mm_struct *mm, unsigned long addr,
					   pte_t *ptep)
{
	pteval_t ret;

	ret = PVOP_CALL3(pteval_t, pv_mmu_ops.ptep_modify_prot_start,
			 mm, addr, ptep);

	return (pte_t) { .pte = ret };
}

static inline void ptep_modify_prot_commit(struct mm_struct *mm, unsigned long addr,
					   pte_t *ptep, pte_t pte)
{
	if (sizeof(pteval_t) > sizeof(long))
		/* 5 arg words */
		pv_mmu_ops.ptep_modify_prot_commit(mm, addr, ptep, pte);
	else
		PVOP_VCALL4(pv_mmu_ops.ptep_modify_prot_commit,
			    mm, addr, ptep, pte.pte);
}

static inline void set_pte(pte_t *ptep, pte_t pte)
{
	if (sizeof(pteval_t) > sizeof(long))
		PVOP_VCALL3(pv_mmu_ops.set_pte, ptep,
			    pte.pte, (u64)pte.pte >> 32);
	else
		PVOP_VCALL2(pv_mmu_ops.set_pte, ptep,
			    pte.pte);
}

static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pte)
{
	if (sizeof(pteval_t) > sizeof(long))
		/* 5 arg words */
		pv_mmu_ops.set_pte_at(mm, addr, ptep, pte);
	else
		PVOP_VCALL4(pv_mmu_ops.set_pte_at, mm, addr, ptep, pte.pte);
}

static inline void set_pmd(pmd_t *pmdp, pmd_t pmd)
{
	pmdval_t val = native_pmd_val(pmd);

	if (sizeof(pmdval_t) > sizeof(long))
		PVOP_VCALL3(pv_mmu_ops.set_pmd, pmdp, val, (u64)val >> 32);
	else
		PVOP_VCALL2(pv_mmu_ops.set_pmd, pmdp, val);
}

#if PAGETABLE_LEVELS >= 3
static inline pmd_t __pmd(pmdval_t val)
{
	pmdval_t ret;

	if (sizeof(pmdval_t) > sizeof(long))
		ret = PVOP_CALL2(pmdval_t, pv_mmu_ops.make_pmd,
				 val, (u64)val >> 32);
	else
		ret = PVOP_CALL1(pmdval_t, pv_mmu_ops.make_pmd,
				 val);

	return (pmd_t) { ret };
}

static inline pmdval_t pmd_val(pmd_t pmd)
{
	pmdval_t ret;

	if (sizeof(pmdval_t) > sizeof(long))
		ret =  PVOP_CALL2(pmdval_t, pv_mmu_ops.pmd_val,
				  pmd.pmd, (u64)pmd.pmd >> 32);
	else
		ret =  PVOP_CALL1(pmdval_t, pv_mmu_ops.pmd_val,
				  pmd.pmd);

	return ret;
}

static inline void set_pud(pud_t *pudp, pud_t pud)
{
	pudval_t val = native_pud_val(pud);

	if (sizeof(pudval_t) > sizeof(long))
		PVOP_VCALL3(pv_mmu_ops.set_pud, pudp,
			    val, (u64)val >> 32);
	else
		PVOP_VCALL2(pv_mmu_ops.set_pud, pudp,
			    val);
}
#if PAGETABLE_LEVELS == 4
static inline pud_t __pud(pudval_t val)
{
	pudval_t ret;

	if (sizeof(pudval_t) > sizeof(long))
		ret = PVOP_CALL2(pudval_t, pv_mmu_ops.make_pud,
				 val, (u64)val >> 32);
	else
		ret = PVOP_CALL1(pudval_t, pv_mmu_ops.make_pud,
				 val);

	return (pud_t) { ret };
}

static inline pudval_t pud_val(pud_t pud)
{
	pudval_t ret;

	if (sizeof(pudval_t) > sizeof(long))
		ret =  PVOP_CALL2(pudval_t, pv_mmu_ops.pud_val,
				  pud.pud, (u64)pud.pud >> 32);
	else
		ret =  PVOP_CALL1(pudval_t, pv_mmu_ops.pud_val,
				  pud.pud);

	return ret;
}

static inline void set_pgd(pgd_t *pgdp, pgd_t pgd)
{
	pgdval_t val = native_pgd_val(pgd);

	if (sizeof(pgdval_t) > sizeof(long))
		PVOP_VCALL3(pv_mmu_ops.set_pgd, pgdp,
			    val, (u64)val >> 32);
	else
		PVOP_VCALL2(pv_mmu_ops.set_pgd, pgdp,
			    val);
}

static inline void pgd_clear(pgd_t *pgdp)
{
	set_pgd(pgdp, __pgd(0));
}

static inline void pud_clear(pud_t *pudp)
{
	set_pud(pudp, __pud(0));
}

#endif	/* PAGETABLE_LEVELS == 4 */

#endif	/* PAGETABLE_LEVELS >= 3 */

#ifdef CONFIG_X86_PAE
/* Special-case pte-setting operations for PAE, which can't update a
   64-bit pte atomically */
static inline void set_pte_atomic(pte_t *ptep, pte_t pte)
{
	PVOP_VCALL3(pv_mmu_ops.set_pte_atomic, ptep,
		    pte.pte, pte.pte >> 32);
}

static inline void set_pte_present(struct mm_struct *mm, unsigned long addr,
				   pte_t *ptep, pte_t pte)
{
	/* 5 arg words */
	pv_mmu_ops.set_pte_present(mm, addr, ptep, pte);
}

static inline void pte_clear(struct mm_struct *mm, unsigned long addr,
			     pte_t *ptep)
{
	PVOP_VCALL3(pv_mmu_ops.pte_clear, mm, addr, ptep);
}

static inline void pmd_clear(pmd_t *pmdp)
{
	PVOP_VCALL1(pv_mmu_ops.pmd_clear, pmdp);
}
#else  /* !CONFIG_X86_PAE */
static inline void set_pte_atomic(pte_t *ptep, pte_t pte)
{
	set_pte(ptep, pte);
}

static inline void set_pte_present(struct mm_struct *mm, unsigned long addr,
				   pte_t *ptep, pte_t pte)
{
	set_pte(ptep, pte);
}

static inline void pte_clear(struct mm_struct *mm, unsigned long addr,
			     pte_t *ptep)
{
	set_pte_at(mm, addr, ptep, __pte(0));
}

static inline void pmd_clear(pmd_t *pmdp)
{
	set_pmd(pmdp, __pmd(0));
}
#endif	/* CONFIG_X86_PAE */

/* Lazy mode for batching updates / context switch */
enum paravirt_lazy_mode {
	PARAVIRT_LAZY_NONE,
	PARAVIRT_LAZY_MMU,
	PARAVIRT_LAZY_CPU,
};

enum paravirt_lazy_mode paravirt_get_lazy_mode(void);
void paravirt_enter_lazy_cpu(void);
void paravirt_leave_lazy_cpu(void);
void paravirt_enter_lazy_mmu(void);
void paravirt_leave_lazy_mmu(void);
void paravirt_leave_lazy(enum paravirt_lazy_mode mode);

#define  __HAVE_ARCH_ENTER_LAZY_CPU_MODE
static inline void arch_enter_lazy_cpu_mode(void)
{
	PVOP_VCALL0(pv_cpu_ops.lazy_mode.enter);
}

static inline void arch_leave_lazy_cpu_mode(void)
{
	PVOP_VCALL0(pv_cpu_ops.lazy_mode.leave);
}

static inline void arch_flush_lazy_cpu_mode(void)
{
	if (unlikely(paravirt_get_lazy_mode() == PARAVIRT_LAZY_CPU)) {
		arch_leave_lazy_cpu_mode();
		arch_enter_lazy_cpu_mode();
	}
}


#define  __HAVE_ARCH_ENTER_LAZY_MMU_MODE
static inline void arch_enter_lazy_mmu_mode(void)
{
	PVOP_VCALL0(pv_mmu_ops.lazy_mode.enter);
}

static inline void arch_leave_lazy_mmu_mode(void)
{
	PVOP_VCALL0(pv_mmu_ops.lazy_mode.leave);
}

static inline void arch_flush_lazy_mmu_mode(void)
{
	if (unlikely(paravirt_get_lazy_mode() == PARAVIRT_LAZY_MMU)) {
		arch_leave_lazy_mmu_mode();
		arch_enter_lazy_mmu_mode();
	}
}

static inline void __set_fixmap(unsigned /* enum fixed_addresses */ idx,
				unsigned long phys, pgprot_t flags)
{
	pv_mmu_ops.set_fixmap(idx, phys, flags);
}

void _paravirt_nop(void);
#define paravirt_nop	((void *)_paravirt_nop)

void paravirt_use_bytelocks(void);

#ifdef CONFIG_SMP

static inline int __raw_spin_is_locked(struct raw_spinlock *lock)
{
	return PVOP_CALL1(int, pv_lock_ops.spin_is_locked, lock);
}

static inline int __raw_spin_is_contended(struct raw_spinlock *lock)
{
	return PVOP_CALL1(int, pv_lock_ops.spin_is_contended, lock);
}

static __always_inline void __raw_spin_lock(struct raw_spinlock *lock)
{
	PVOP_VCALL1(pv_lock_ops.spin_lock, lock);
}

static __always_inline int __raw_spin_trylock(struct raw_spinlock *lock)
{
	return PVOP_CALL1(int, pv_lock_ops.spin_trylock, lock);
}

static __always_inline void __raw_spin_unlock(struct raw_spinlock *lock)
{
	PVOP_VCALL1(pv_lock_ops.spin_unlock, lock);
}

#endif

/* These all sit in the .parainstructions section to tell us what to patch. */
struct paravirt_patch_site {
	u8 *instr; 		/* original instructions */
	u8 instrtype;		/* type of this instruction */
	u8 len;			/* length of original instruction */
	u16 clobbers;		/* what registers you may clobber */
};

extern struct paravirt_patch_site __parainstructions[],
	__parainstructions_end[];

#ifdef CONFIG_X86_32
#define PV_SAVE_REGS "pushl %%ecx; pushl %%edx;"
#define PV_RESTORE_REGS "popl %%edx; popl %%ecx"
#define PV_FLAGS_ARG "0"
#define PV_EXTRA_CLOBBERS
#define PV_VEXTRA_CLOBBERS
#else
/* We save some registers, but all of them, that's too much. We clobber all
 * caller saved registers but the argument parameter */
#define PV_SAVE_REGS "pushq %%rdi;"
#define PV_RESTORE_REGS "popq %%rdi;"
#define PV_EXTRA_CLOBBERS EXTRA_CLOBBERS, "rcx" , "rdx", "rsi"
#define PV_VEXTRA_CLOBBERS EXTRA_CLOBBERS, "rdi", "rcx" , "rdx", "rsi"
#define PV_FLAGS_ARG "D"
#endif

static inline unsigned long __raw_local_save_flags(void)
{
	unsigned long f;

	asm volatile(paravirt_alt(PV_SAVE_REGS
				  PARAVIRT_CALL
				  PV_RESTORE_REGS)
		     : "=a"(f)
		     : paravirt_type(pv_irq_ops.save_fl),
		       paravirt_clobber(CLBR_EAX)
		     : "memory", "cc" PV_VEXTRA_CLOBBERS);
	return f;
}

static inline void raw_local_irq_restore(unsigned long f)
{
	asm volatile(paravirt_alt(PV_SAVE_REGS
				  PARAVIRT_CALL
				  PV_RESTORE_REGS)
		     : "=a"(f)
		     : PV_FLAGS_ARG(f),
		       paravirt_type(pv_irq_ops.restore_fl),
		       paravirt_clobber(CLBR_EAX)
		     : "memory", "cc" PV_EXTRA_CLOBBERS);
}

static inline void raw_local_irq_disable(void)
{
	asm volatile(paravirt_alt(PV_SAVE_REGS
				  PARAVIRT_CALL
				  PV_RESTORE_REGS)
		     :
		     : paravirt_type(pv_irq_ops.irq_disable),
		       paravirt_clobber(CLBR_EAX)
		     : "memory", "eax", "cc" PV_EXTRA_CLOBBERS);
}

static inline void raw_local_irq_enable(void)
{
	asm volatile(paravirt_alt(PV_SAVE_REGS
				  PARAVIRT_CALL
				  PV_RESTORE_REGS)
		     :
		     : paravirt_type(pv_irq_ops.irq_enable),
		       paravirt_clobber(CLBR_EAX)
		     : "memory", "eax", "cc" PV_EXTRA_CLOBBERS);
}

static inline unsigned long __raw_local_irq_save(void)
{
	unsigned long f;

	f = __raw_local_save_flags();
	raw_local_irq_disable();
	return f;
}


/* Make sure as little as possible of this mess escapes. */
#undef PARAVIRT_CALL
#undef __PVOP_CALL
#undef __PVOP_VCALL
#undef PVOP_VCALL0
#undef PVOP_CALL0
#undef PVOP_VCALL1
#undef PVOP_CALL1
#undef PVOP_VCALL2
#undef PVOP_CALL2
#undef PVOP_VCALL3
#undef PVOP_CALL3
#undef PVOP_VCALL4
#undef PVOP_CALL4

#else  /* __ASSEMBLY__ */

#define _PVSITE(ptype, clobbers, ops, word, algn)	\
771:;						\
	ops;					\
772:;						\
	.pushsection .parainstructions,"a";	\
	 .align	algn;				\
	 word 771b;				\
	 .byte ptype;				\
	 .byte 772b-771b;			\
	 .short clobbers;			\
	.popsection


#ifdef CONFIG_X86_64
#define PV_SAVE_REGS				\
	push %rax;				\
	push %rcx;				\
	push %rdx;				\
	push %rsi;				\
	push %rdi;				\
	push %r8;				\
	push %r9;				\
	push %r10;				\
	push %r11
#define PV_RESTORE_REGS				\
	pop %r11;				\
	pop %r10;				\
	pop %r9;				\
	pop %r8;				\
	pop %rdi;				\
	pop %rsi;				\
	pop %rdx;				\
	pop %rcx;				\
	pop %rax
#define PARA_PATCH(struct, off)        ((PARAVIRT_PATCH_##struct + (off)) / 8)
#define PARA_SITE(ptype, clobbers, ops) _PVSITE(ptype, clobbers, ops, .quad, 8)
#define PARA_INDIRECT(addr)	*addr(%rip)
#else
#define PV_SAVE_REGS   pushl %eax; pushl %edi; pushl %ecx; pushl %edx
#define PV_RESTORE_REGS popl %edx; popl %ecx; popl %edi; popl %eax
#define PARA_PATCH(struct, off)        ((PARAVIRT_PATCH_##struct + (off)) / 4)
#define PARA_SITE(ptype, clobbers, ops) _PVSITE(ptype, clobbers, ops, .long, 4)
#define PARA_INDIRECT(addr)	*%cs:addr
#endif

#define INTERRUPT_RETURN						\
	PARA_SITE(PARA_PATCH(pv_cpu_ops, PV_CPU_iret), CLBR_NONE,	\
		  jmp PARA_INDIRECT(pv_cpu_ops+PV_CPU_iret))

#define DISABLE_INTERRUPTS(clobbers)					\
	PARA_SITE(PARA_PATCH(pv_irq_ops, PV_IRQ_irq_disable), clobbers, \
		  PV_SAVE_REGS;						\
		  call PARA_INDIRECT(pv_irq_ops+PV_IRQ_irq_disable);	\
		  PV_RESTORE_REGS;)			\

#define ENABLE_INTERRUPTS(clobbers)					\
	PARA_SITE(PARA_PATCH(pv_irq_ops, PV_IRQ_irq_enable), clobbers,	\
		  PV_SAVE_REGS;						\
		  call PARA_INDIRECT(pv_irq_ops+PV_IRQ_irq_enable);	\
		  PV_RESTORE_REGS;)

#define USERGS_SYSRET32							\
	PARA_SITE(PARA_PATCH(pv_cpu_ops, PV_CPU_usergs_sysret32),	\
		  CLBR_NONE,						\
		  jmp PARA_INDIRECT(pv_cpu_ops+PV_CPU_usergs_sysret32))

#ifdef CONFIG_X86_32
#define GET_CR0_INTO_EAX				\
	push %ecx; push %edx;				\
	call PARA_INDIRECT(pv_cpu_ops+PV_CPU_read_cr0);	\
	pop %edx; pop %ecx

#define ENABLE_INTERRUPTS_SYSEXIT					\
	PARA_SITE(PARA_PATCH(pv_cpu_ops, PV_CPU_irq_enable_sysexit),	\
		  CLBR_NONE,						\
		  jmp PARA_INDIRECT(pv_cpu_ops+PV_CPU_irq_enable_sysexit))


#else	/* !CONFIG_X86_32 */

/*
 * If swapgs is used while the userspace stack is still current,
 * there's no way to call a pvop.  The PV replacement *must* be
 * inlined, or the swapgs instruction must be trapped and emulated.
 */
#define SWAPGS_UNSAFE_STACK						\
	PARA_SITE(PARA_PATCH(pv_cpu_ops, PV_CPU_swapgs), CLBR_NONE,	\
		  swapgs)

#define SWAPGS								\
	PARA_SITE(PARA_PATCH(pv_cpu_ops, PV_CPU_swapgs), CLBR_NONE,	\
		  PV_SAVE_REGS;						\
		  call PARA_INDIRECT(pv_cpu_ops+PV_CPU_swapgs);		\
		  PV_RESTORE_REGS					\
		 )

#define GET_CR2_INTO_RCX				\
	call PARA_INDIRECT(pv_mmu_ops+PV_MMU_read_cr2);	\
	movq %rax, %rcx;				\
	xorq %rax, %rax;

#define PARAVIRT_ADJUST_EXCEPTION_FRAME					\
	PARA_SITE(PARA_PATCH(pv_irq_ops, PV_IRQ_adjust_exception_frame), \
		  CLBR_NONE,						\
		  call PARA_INDIRECT(pv_irq_ops+PV_IRQ_adjust_exception_frame))

#define USERGS_SYSRET64							\
	PARA_SITE(PARA_PATCH(pv_cpu_ops, PV_CPU_usergs_sysret64),	\
		  CLBR_NONE,						\
		  jmp PARA_INDIRECT(pv_cpu_ops+PV_CPU_usergs_sysret64))

#define ENABLE_INTERRUPTS_SYSEXIT32					\
	PARA_SITE(PARA_PATCH(pv_cpu_ops, PV_CPU_irq_enable_sysexit),	\
		  CLBR_NONE,						\
		  jmp PARA_INDIRECT(pv_cpu_ops+PV_CPU_irq_enable_sysexit))
#endif	/* CONFIG_X86_32 */

#endif /* __ASSEMBLY__ */
#endif /* CONFIG_PARAVIRT */
#endif /* ASM_X86__PARAVIRT_H */
