#ifndef __ASM_PARAVIRT_H
#define __ASM_PARAVIRT_H
/* Various instructions on x86 need to be replaced for
 * para-virtualization: those hooks are defined here. */

#ifdef CONFIG_PARAVIRT
#include <asm/page.h>

/* Bitmask of what can be clobbered: usually at least eax. */
#define CLBR_NONE 0x0
#define CLBR_EAX 0x1
#define CLBR_ECX 0x2
#define CLBR_EDX 0x4
#define CLBR_ANY 0x7

#ifndef __ASSEMBLY__
#include <linux/types.h>

struct thread_struct;
struct Xgt_desc_struct;
struct tss_struct;
struct mm_struct;
struct desc_struct;
struct paravirt_ops
{
	unsigned int kernel_rpl;
	int shared_kernel_pmd;
 	int paravirt_enabled;
	const char *name;

	/*
	 * Patch may replace one of the defined code sequences with arbitrary
	 * code, subject to the same register constraints.  This generally
	 * means the code is not free to clobber any registers other than EAX.
	 * The patch function should return the number of bytes of code
	 * generated, as we nop pad the rest in generic code.
	 */
	unsigned (*patch)(u8 type, u16 clobber, void *firstinsn, unsigned len);

	void (*arch_setup)(void);
	char *(*memory_setup)(void);
	void (*init_IRQ)(void);

	void (*pagetable_setup_start)(pgd_t *pgd_base);
	void (*pagetable_setup_done)(pgd_t *pgd_base);

	void (*banner)(void);

	unsigned long (*get_wallclock)(void);
	int (*set_wallclock)(unsigned long);
	void (*time_init)(void);

	void (*cpuid)(unsigned int *eax, unsigned int *ebx,
		      unsigned int *ecx, unsigned int *edx);

	unsigned long (*get_debugreg)(int regno);
	void (*set_debugreg)(int regno, unsigned long value);

	void (*clts)(void);

	unsigned long (*read_cr0)(void);
	void (*write_cr0)(unsigned long);

	unsigned long (*read_cr2)(void);
	void (*write_cr2)(unsigned long);

	unsigned long (*read_cr3)(void);
	void (*write_cr3)(unsigned long);

	unsigned long (*read_cr4_safe)(void);
	unsigned long (*read_cr4)(void);
	void (*write_cr4)(unsigned long);

	unsigned long (*save_fl)(void);
	void (*restore_fl)(unsigned long);
	void (*irq_disable)(void);
	void (*irq_enable)(void);
	void (*safe_halt)(void);
	void (*halt)(void);
	void (*wbinvd)(void);

	/* err = 0/-EFAULT.  wrmsr returns 0/-EFAULT. */
	u64 (*read_msr)(unsigned int msr, int *err);
	int (*write_msr)(unsigned int msr, u64 val);

	u64 (*read_tsc)(void);
	u64 (*read_pmc)(void);
 	u64 (*get_scheduled_cycles)(void);
	unsigned long (*get_cpu_khz)(void);

	void (*load_tr_desc)(void);
	void (*load_gdt)(const struct Xgt_desc_struct *);
	void (*load_idt)(const struct Xgt_desc_struct *);
	void (*store_gdt)(struct Xgt_desc_struct *);
	void (*store_idt)(struct Xgt_desc_struct *);
	void (*set_ldt)(const void *desc, unsigned entries);
	unsigned long (*store_tr)(void);
	void (*load_tls)(struct thread_struct *t, unsigned int cpu);
	void (*write_ldt_entry)(struct desc_struct *,
				int entrynum, u32 low, u32 high);
	void (*write_gdt_entry)(struct desc_struct *,
				int entrynum, u32 low, u32 high);
	void (*write_idt_entry)(struct desc_struct *,
				int entrynum, u32 low, u32 high);
	void (*load_esp0)(struct tss_struct *tss, struct thread_struct *t);

	void (*set_iopl_mask)(unsigned mask);

	void (*io_delay)(void);

	void (*activate_mm)(struct mm_struct *prev,
			    struct mm_struct *next);
	void (*dup_mmap)(struct mm_struct *oldmm,
			 struct mm_struct *mm);
	void (*exit_mmap)(struct mm_struct *mm);

#ifdef CONFIG_X86_LOCAL_APIC
	void (*apic_write)(unsigned long reg, unsigned long v);
	void (*apic_write_atomic)(unsigned long reg, unsigned long v);
	unsigned long (*apic_read)(unsigned long reg);
	void (*setup_boot_clock)(void);
	void (*setup_secondary_clock)(void);
#endif

	void (*flush_tlb_user)(void);
	void (*flush_tlb_kernel)(void);
	void (*flush_tlb_single)(u32 addr);

	void (*map_pt_hook)(int type, pte_t *va, u32 pfn);

	void (*alloc_pt)(u32 pfn);
	void (*alloc_pd)(u32 pfn);
	void (*alloc_pd_clone)(u32 pfn, u32 clonepfn, u32 start, u32 count);
	void (*release_pt)(u32 pfn);
	void (*release_pd)(u32 pfn);

	void (*set_pte)(pte_t *ptep, pte_t pteval);
	void (*set_pte_at)(struct mm_struct *mm, unsigned long addr, pte_t *ptep, pte_t pteval);
	void (*set_pmd)(pmd_t *pmdp, pmd_t pmdval);
	void (*pte_update)(struct mm_struct *mm, unsigned long addr, pte_t *ptep);
	void (*pte_update_defer)(struct mm_struct *mm, unsigned long addr, pte_t *ptep);

 	pte_t (*ptep_get_and_clear)(pte_t *ptep);

#ifdef CONFIG_X86_PAE
	void (*set_pte_atomic)(pte_t *ptep, pte_t pteval);
 	void (*set_pte_present)(struct mm_struct *mm, unsigned long addr, pte_t *ptep, pte_t pte);
	void (*set_pud)(pud_t *pudp, pud_t pudval);
 	void (*pte_clear)(struct mm_struct *mm, unsigned long addr, pte_t *ptep);
	void (*pmd_clear)(pmd_t *pmdp);

	unsigned long long (*pte_val)(pte_t);
	unsigned long long (*pmd_val)(pmd_t);
	unsigned long long (*pgd_val)(pgd_t);

	pte_t (*make_pte)(unsigned long long pte);
	pmd_t (*make_pmd)(unsigned long long pmd);
	pgd_t (*make_pgd)(unsigned long long pgd);
#else
	unsigned long (*pte_val)(pte_t);
	unsigned long (*pgd_val)(pgd_t);

	pte_t (*make_pte)(unsigned long pte);
	pgd_t (*make_pgd)(unsigned long pgd);
#endif

	void (*set_lazy_mode)(int mode);

	/* These two are jmp to, not actually called. */
	void (*irq_enable_sysexit)(void);
	void (*iret)(void);

	void (*startup_ipi_hook)(int phys_apicid, unsigned long start_eip, unsigned long start_esp);
};

/* Mark a paravirt probe function. */
#define paravirt_probe(fn)						\
 static asmlinkage void (*__paravirtprobe_##fn)(void) __attribute_used__ \
		__attribute__((__section__(".paravirtprobe"))) = fn

extern struct paravirt_ops paravirt_ops;

#define PARAVIRT_PATCH(x)					\
	(offsetof(struct paravirt_ops, x) / sizeof(void *))

#define paravirt_type(type)					\
	[paravirt_typenum] "i" (PARAVIRT_PATCH(type))
#define paravirt_clobber(clobber)		\
	[paravirt_clobber] "i" (clobber)

#define PARAVIRT_CALL	"call *paravirt_ops+%c[paravirt_typenum]*4;"

#define _paravirt_alt(insn_string, type, clobber)	\
	"771:\n\t" insn_string "\n" "772:\n"		\
	".pushsection .parainstructions,\"a\"\n"	\
	"  .long 771b\n"				\
	"  .byte " type "\n"				\
	"  .byte 772b-771b\n"				\
	"  .short " clobber "\n"			\
	".popsection\n"

#define paravirt_alt(insn_string)				\
	_paravirt_alt(insn_string, "%c[paravirt_typenum]", "%c[paravirt_clobber]")

#define paravirt_enabled() (paravirt_ops.paravirt_enabled)

static inline void load_esp0(struct tss_struct *tss,
			     struct thread_struct *thread)
{
	paravirt_ops.load_esp0(tss, thread);
}

#define ARCH_SETUP			paravirt_ops.arch_setup();
static inline unsigned long get_wallclock(void)
{
	return paravirt_ops.get_wallclock();
}

static inline int set_wallclock(unsigned long nowtime)
{
	return paravirt_ops.set_wallclock(nowtime);
}

static inline void (*choose_time_init(void))(void)
{
	return paravirt_ops.time_init;
}

/* The paravirtualized CPUID instruction. */
static inline void __cpuid(unsigned int *eax, unsigned int *ebx,
			   unsigned int *ecx, unsigned int *edx)
{
	paravirt_ops.cpuid(eax, ebx, ecx, edx);
}

/*
 * These special macros can be used to get or set a debugging register
 */
#define get_debugreg(var, reg) var = paravirt_ops.get_debugreg(reg)
#define set_debugreg(val, reg) paravirt_ops.set_debugreg(reg, val)

#define clts() paravirt_ops.clts()

#define read_cr0() paravirt_ops.read_cr0()
#define write_cr0(x) paravirt_ops.write_cr0(x)

#define read_cr2() paravirt_ops.read_cr2()
#define write_cr2(x) paravirt_ops.write_cr2(x)

#define read_cr3() paravirt_ops.read_cr3()
#define write_cr3(x) paravirt_ops.write_cr3(x)

#define read_cr4() paravirt_ops.read_cr4()
#define read_cr4_safe(x) paravirt_ops.read_cr4_safe()
#define write_cr4(x) paravirt_ops.write_cr4(x)

#define raw_ptep_get_and_clear(xp)	(paravirt_ops.ptep_get_and_clear(xp))

static inline void raw_safe_halt(void)
{
	paravirt_ops.safe_halt();
}

static inline void halt(void)
{
	paravirt_ops.safe_halt();
}
#define wbinvd() paravirt_ops.wbinvd()

#define get_kernel_rpl()  (paravirt_ops.kernel_rpl)

/* These should all do BUG_ON(_err), but our headers are too tangled. */
#define rdmsr(msr,val1,val2) do {				\
	int _err;						\
	u64 _l = paravirt_ops.read_msr(msr,&_err);		\
	val1 = (u32)_l;						\
	val2 = _l >> 32;					\
} while(0)

#define wrmsr(msr,val1,val2) do {				\
	u64 _l = ((u64)(val2) << 32) | (val1);			\
	paravirt_ops.write_msr((msr), _l);			\
} while(0)

#define rdmsrl(msr,val) do {					\
	int _err;						\
	val = paravirt_ops.read_msr((msr),&_err);		\
} while(0)

#define wrmsrl(msr,val) (paravirt_ops.write_msr((msr),(val)))
#define wrmsr_safe(msr,a,b) ({					\
	u64 _l = ((u64)(b) << 32) | (a);			\
	paravirt_ops.write_msr((msr),_l);			\
})

/* rdmsr with exception handling */
#define rdmsr_safe(msr,a,b) ({					\
	int _err;						\
	u64 _l = paravirt_ops.read_msr(msr,&_err);		\
	(*a) = (u32)_l;						\
	(*b) = _l >> 32;					\
	_err; })

#define rdtsc(low,high) do {					\
	u64 _l = paravirt_ops.read_tsc();			\
	low = (u32)_l;						\
	high = _l >> 32;					\
} while(0)

#define rdtscl(low) do {					\
	u64 _l = paravirt_ops.read_tsc();			\
	low = (int)_l;						\
} while(0)

#define rdtscll(val) (val = paravirt_ops.read_tsc())

#define get_scheduled_cycles(val) (val = paravirt_ops.get_scheduled_cycles())
#define calculate_cpu_khz() (paravirt_ops.get_cpu_khz())

#define write_tsc(val1,val2) wrmsr(0x10, val1, val2)

#define rdpmc(counter,low,high) do {				\
	u64 _l = paravirt_ops.read_pmc();			\
	low = (u32)_l;						\
	high = _l >> 32;					\
} while(0)

#define load_TR_desc() (paravirt_ops.load_tr_desc())
#define load_gdt(dtr) (paravirt_ops.load_gdt(dtr))
#define load_idt(dtr) (paravirt_ops.load_idt(dtr))
#define set_ldt(addr, entries) (paravirt_ops.set_ldt((addr), (entries)))
#define store_gdt(dtr) (paravirt_ops.store_gdt(dtr))
#define store_idt(dtr) (paravirt_ops.store_idt(dtr))
#define store_tr(tr) ((tr) = paravirt_ops.store_tr())
#define load_TLS(t,cpu) (paravirt_ops.load_tls((t),(cpu)))
#define write_ldt_entry(dt, entry, low, high)				\
	(paravirt_ops.write_ldt_entry((dt), (entry), (low), (high)))
#define write_gdt_entry(dt, entry, low, high)				\
	(paravirt_ops.write_gdt_entry((dt), (entry), (low), (high)))
#define write_idt_entry(dt, entry, low, high)				\
	(paravirt_ops.write_idt_entry((dt), (entry), (low), (high)))
#define set_iopl_mask(mask) (paravirt_ops.set_iopl_mask(mask))

#define __pte(x)	paravirt_ops.make_pte(x)
#define __pgd(x)	paravirt_ops.make_pgd(x)

#define pte_val(x)	paravirt_ops.pte_val(x)
#define pgd_val(x)	paravirt_ops.pgd_val(x)

#ifdef CONFIG_X86_PAE
#define __pmd(x)	paravirt_ops.make_pmd(x)
#define pmd_val(x)	paravirt_ops.pmd_val(x)
#endif

/* The paravirtualized I/O functions */
static inline void slow_down_io(void) {
	paravirt_ops.io_delay();
#ifdef REALLY_SLOW_IO
	paravirt_ops.io_delay();
	paravirt_ops.io_delay();
	paravirt_ops.io_delay();
#endif
}

#ifdef CONFIG_X86_LOCAL_APIC
/*
 * Basic functions accessing APICs.
 */
static inline void apic_write(unsigned long reg, unsigned long v)
{
	paravirt_ops.apic_write(reg,v);
}

static inline void apic_write_atomic(unsigned long reg, unsigned long v)
{
	paravirt_ops.apic_write_atomic(reg,v);
}

static inline unsigned long apic_read(unsigned long reg)
{
	return paravirt_ops.apic_read(reg);
}

static inline void setup_boot_clock(void)
{
	paravirt_ops.setup_boot_clock();
}

static inline void setup_secondary_clock(void)
{
	paravirt_ops.setup_secondary_clock();
}
#endif

static inline void paravirt_pagetable_setup_start(pgd_t *base)
{
	if (paravirt_ops.pagetable_setup_start)
		(*paravirt_ops.pagetable_setup_start)(base);
}

static inline void paravirt_pagetable_setup_done(pgd_t *base)
{
	if (paravirt_ops.pagetable_setup_done)
		(*paravirt_ops.pagetable_setup_done)(base);
}

#ifdef CONFIG_SMP
static inline void startup_ipi_hook(int phys_apicid, unsigned long start_eip,
				    unsigned long start_esp)
{
	return paravirt_ops.startup_ipi_hook(phys_apicid, start_eip, start_esp);
}
#endif

static inline void paravirt_activate_mm(struct mm_struct *prev,
					struct mm_struct *next)
{
	paravirt_ops.activate_mm(prev, next);
}

static inline void arch_dup_mmap(struct mm_struct *oldmm,
				 struct mm_struct *mm)
{
	paravirt_ops.dup_mmap(oldmm, mm);
}

static inline void arch_exit_mmap(struct mm_struct *mm)
{
	paravirt_ops.exit_mmap(mm);
}

#define __flush_tlb() paravirt_ops.flush_tlb_user()
#define __flush_tlb_global() paravirt_ops.flush_tlb_kernel()
#define __flush_tlb_single(addr) paravirt_ops.flush_tlb_single(addr)

#define paravirt_map_pt_hook(type, va, pfn) paravirt_ops.map_pt_hook(type, va, pfn)

#define paravirt_alloc_pt(pfn) paravirt_ops.alloc_pt(pfn)
#define paravirt_release_pt(pfn) paravirt_ops.release_pt(pfn)

#define paravirt_alloc_pd(pfn) paravirt_ops.alloc_pd(pfn)
#define paravirt_alloc_pd_clone(pfn, clonepfn, start, count) \
	paravirt_ops.alloc_pd_clone(pfn, clonepfn, start, count)
#define paravirt_release_pd(pfn) paravirt_ops.release_pd(pfn)

static inline void set_pte(pte_t *ptep, pte_t pteval)
{
	paravirt_ops.set_pte(ptep, pteval);
}

static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pteval)
{
	paravirt_ops.set_pte_at(mm, addr, ptep, pteval);
}

static inline void set_pmd(pmd_t *pmdp, pmd_t pmdval)
{
	paravirt_ops.set_pmd(pmdp, pmdval);
}

static inline void pte_update(struct mm_struct *mm, u32 addr, pte_t *ptep)
{
	paravirt_ops.pte_update(mm, addr, ptep);
}

static inline void pte_update_defer(struct mm_struct *mm, u32 addr, pte_t *ptep)
{
	paravirt_ops.pte_update_defer(mm, addr, ptep);
}

#ifdef CONFIG_X86_PAE
static inline void set_pte_atomic(pte_t *ptep, pte_t pteval)
{
	paravirt_ops.set_pte_atomic(ptep, pteval);
}

static inline void set_pte_present(struct mm_struct *mm, unsigned long addr, pte_t *ptep, pte_t pte)
{
	paravirt_ops.set_pte_present(mm, addr, ptep, pte);
}

static inline void set_pud(pud_t *pudp, pud_t pudval)
{
	paravirt_ops.set_pud(pudp, pudval);
}

static inline void pte_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	paravirt_ops.pte_clear(mm, addr, ptep);
}

static inline void pmd_clear(pmd_t *pmdp)
{
	paravirt_ops.pmd_clear(pmdp);
}
#endif

/* Lazy mode for batching updates / context switch */
#define PARAVIRT_LAZY_NONE 0
#define PARAVIRT_LAZY_MMU  1
#define PARAVIRT_LAZY_CPU  2
#define PARAVIRT_LAZY_FLUSH 3

#define  __HAVE_ARCH_ENTER_LAZY_CPU_MODE
#define arch_enter_lazy_cpu_mode() paravirt_ops.set_lazy_mode(PARAVIRT_LAZY_CPU)
#define arch_leave_lazy_cpu_mode() paravirt_ops.set_lazy_mode(PARAVIRT_LAZY_NONE)
#define arch_flush_lazy_cpu_mode() paravirt_ops.set_lazy_mode(PARAVIRT_LAZY_FLUSH)

#define  __HAVE_ARCH_ENTER_LAZY_MMU_MODE
#define arch_enter_lazy_mmu_mode() paravirt_ops.set_lazy_mode(PARAVIRT_LAZY_MMU)
#define arch_leave_lazy_mmu_mode() paravirt_ops.set_lazy_mode(PARAVIRT_LAZY_NONE)
#define arch_flush_lazy_mmu_mode() paravirt_ops.set_lazy_mode(PARAVIRT_LAZY_FLUSH)

void _paravirt_nop(void);
#define paravirt_nop	((void *)_paravirt_nop)

/* These all sit in the .parainstructions section to tell us what to patch. */
struct paravirt_patch_site {
	u8 *instr; 		/* original instructions */
	u8 instrtype;		/* type of this instruction */
	u8 len;			/* length of original instruction */
	u16 clobbers;		/* what registers you may clobber */
};

extern struct paravirt_patch_site __parainstructions[],
	__parainstructions_end[];

static inline unsigned long __raw_local_save_flags(void)
{
	unsigned long f;

	asm volatile(paravirt_alt("pushl %%ecx; pushl %%edx;"
				  PARAVIRT_CALL
				  "popl %%edx; popl %%ecx")
		     : "=a"(f)
		     : paravirt_type(save_fl),
		       paravirt_clobber(CLBR_NONE)
		     : "memory", "cc");
	return f;
}

static inline void raw_local_irq_restore(unsigned long f)
{
	asm volatile(paravirt_alt("pushl %%ecx; pushl %%edx;"
				  PARAVIRT_CALL
				  "popl %%edx; popl %%ecx")
		     : "=a"(f)
		     : "0"(f),
		       paravirt_type(restore_fl),
		       paravirt_clobber(CLBR_EAX)
		     : "memory", "cc");
}

static inline void raw_local_irq_disable(void)
{
	asm volatile(paravirt_alt("pushl %%ecx; pushl %%edx;"
				  PARAVIRT_CALL
				  "popl %%edx; popl %%ecx")
		     :
		     : paravirt_type(irq_disable),
		       paravirt_clobber(CLBR_EAX)
		     : "memory", "eax", "cc");
}

static inline void raw_local_irq_enable(void)
{
	asm volatile(paravirt_alt("pushl %%ecx; pushl %%edx;"
				  PARAVIRT_CALL
				  "popl %%edx; popl %%ecx")
		     :
		     : paravirt_type(irq_enable),
		       paravirt_clobber(CLBR_EAX)
		     : "memory", "eax", "cc");
}

static inline unsigned long __raw_local_irq_save(void)
{
	unsigned long f;

	f = __raw_local_save_flags();
	raw_local_irq_disable();
	return f;
}

#define CLI_STRING							\
	_paravirt_alt("pushl %%ecx; pushl %%edx;"			\
		      "call *paravirt_ops+%c[paravirt_cli_type]*4;"	\
		      "popl %%edx; popl %%ecx",				\
		      "%c[paravirt_cli_type]", "%c[paravirt_clobber]")

#define STI_STRING							\
	_paravirt_alt("pushl %%ecx; pushl %%edx;"			\
		      "call *paravirt_ops+%c[paravirt_sti_type]*4;"	\
		      "popl %%edx; popl %%ecx",				\
		      "%c[paravirt_sti_type]", "%c[paravirt_clobber]")

#define CLI_STI_CLOBBERS , "%eax"
#define CLI_STI_INPUT_ARGS						\
	,								\
	[paravirt_cli_type] "i" (PARAVIRT_PATCH(irq_disable)),		\
	[paravirt_sti_type] "i" (PARAVIRT_PATCH(irq_enable)),		\
	paravirt_clobber(CLBR_EAX)

#undef PARAVIRT_CALL

#else  /* __ASSEMBLY__ */

#define PARA_PATCH(off)	((off) / 4)

#define PARA_SITE(ptype, clobbers, ops)		\
771:;						\
	ops;					\
772:;						\
	.pushsection .parainstructions,"a";	\
	 .long 771b;				\
	 .byte ptype;				\
	 .byte 772b-771b;			\
	 .short clobbers;			\
	.popsection

#define INTERRUPT_RETURN					\
	PARA_SITE(PARA_PATCH(PARAVIRT_iret), CLBR_ANY,		\
		  jmp *%cs:paravirt_ops+PARAVIRT_iret)

#define DISABLE_INTERRUPTS(clobbers)					\
	PARA_SITE(PARA_PATCH(PARAVIRT_irq_disable), clobbers,		\
		  pushl %ecx; pushl %edx;				\
		  call *%cs:paravirt_ops+PARAVIRT_irq_disable;		\
		  popl %edx; popl %ecx)					\

#define ENABLE_INTERRUPTS(clobbers)					\
	PARA_SITE(PARA_PATCH(PARAVIRT_irq_enable), clobbers,		\
		  pushl %ecx; pushl %edx;				\
		  call *%cs:paravirt_ops+PARAVIRT_irq_enable;		\
		  popl %edx; popl %ecx)

#define ENABLE_INTERRUPTS_SYSEXIT					\
	PARA_SITE(PARA_PATCH(PARAVIRT_irq_enable_sysexit), CLBR_ANY,	\
		  jmp *%cs:paravirt_ops+PARAVIRT_irq_enable_sysexit)

#define GET_CR0_INTO_EAX			\
	call *paravirt_ops+PARAVIRT_read_cr0

#endif /* __ASSEMBLY__ */
#endif /* CONFIG_PARAVIRT */
#endif	/* __ASM_PARAVIRT_H */
