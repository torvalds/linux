#ifndef __ASM_PARAVIRT_H
#define __ASM_PARAVIRT_H
/* Various instructions on x86 need to be replaced for
 * para-virtualization: those hooks are defined here. */
#include <linux/linkage.h>
#include <linux/stringify.h>

#ifdef CONFIG_PARAVIRT
/* These are the most performance critical ops, so we want to be able to patch
 * callers */
#define PARAVIRT_IRQ_DISABLE 0
#define PARAVIRT_IRQ_ENABLE 1
#define PARAVIRT_RESTORE_FLAGS 2
#define PARAVIRT_SAVE_FLAGS 3
#define PARAVIRT_SAVE_FLAGS_IRQ_DISABLE 4
#define PARAVIRT_INTERRUPT_RETURN 5
#define PARAVIRT_STI_SYSEXIT 6

/* Bitmask of what can be clobbered: usually at least eax. */
#define CLBR_NONE 0x0
#define CLBR_EAX 0x1
#define CLBR_ECX 0x2
#define CLBR_EDX 0x4
#define CLBR_ANY 0x7

#ifndef __ASSEMBLY__
struct thread_struct;
struct Xgt_desc_struct;
struct tss_struct;
struct paravirt_ops
{
	unsigned int kernel_rpl;
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

	void (*banner)(void);

	unsigned long (*get_wallclock)(void);
	int (*set_wallclock)(unsigned long);
	void (*time_init)(void);

	/* All the function pointers here are declared as "fastcall"
	   so that we get a specific register-based calling
	   convention.  This makes it easier to implement inline
	   assembler replacements. */

	void (fastcall *cpuid)(unsigned int *eax, unsigned int *ebx,
		      unsigned int *ecx, unsigned int *edx);

	unsigned long (fastcall *get_debugreg)(int regno);
	void (fastcall *set_debugreg)(int regno, unsigned long value);

	void (fastcall *clts)(void);

	unsigned long (fastcall *read_cr0)(void);
	void (fastcall *write_cr0)(unsigned long);

	unsigned long (fastcall *read_cr2)(void);
	void (fastcall *write_cr2)(unsigned long);

	unsigned long (fastcall *read_cr3)(void);
	void (fastcall *write_cr3)(unsigned long);

	unsigned long (fastcall *read_cr4_safe)(void);
	unsigned long (fastcall *read_cr4)(void);
	void (fastcall *write_cr4)(unsigned long);

	unsigned long (fastcall *save_fl)(void);
	void (fastcall *restore_fl)(unsigned long);
	void (fastcall *irq_disable)(void);
	void (fastcall *irq_enable)(void);
	void (fastcall *safe_halt)(void);
	void (fastcall *halt)(void);
	void (fastcall *wbinvd)(void);

	/* err = 0/-EFAULT.  wrmsr returns 0/-EFAULT. */
	u64 (fastcall *read_msr)(unsigned int msr, int *err);
	int (fastcall *write_msr)(unsigned int msr, u64 val);

	u64 (fastcall *read_tsc)(void);
	u64 (fastcall *read_pmc)(void);

	void (fastcall *load_tr_desc)(void);
	void (fastcall *load_gdt)(const struct Xgt_desc_struct *);
	void (fastcall *load_idt)(const struct Xgt_desc_struct *);
	void (fastcall *store_gdt)(struct Xgt_desc_struct *);
	void (fastcall *store_idt)(struct Xgt_desc_struct *);
	void (fastcall *set_ldt)(const void *desc, unsigned entries);
	unsigned long (fastcall *store_tr)(void);
	void (fastcall *load_tls)(struct thread_struct *t, unsigned int cpu);
	void (fastcall *write_ldt_entry)(void *dt, int entrynum,
					 u32 low, u32 high);
	void (fastcall *write_gdt_entry)(void *dt, int entrynum,
					 u32 low, u32 high);
	void (fastcall *write_idt_entry)(void *dt, int entrynum,
					 u32 low, u32 high);
	void (fastcall *load_esp0)(struct tss_struct *tss,
				   struct thread_struct *thread);

	void (fastcall *set_iopl_mask)(unsigned mask);

	void (fastcall *io_delay)(void);
	void (*const_udelay)(unsigned long loops);

#ifdef CONFIG_X86_LOCAL_APIC
	void (fastcall *apic_write)(unsigned long reg, unsigned long v);
	void (fastcall *apic_write_atomic)(unsigned long reg, unsigned long v);
	unsigned long (fastcall *apic_read)(unsigned long reg);
#endif

	/* These two are jmp to, not actually called. */
	void (fastcall *irq_enable_sysexit)(void);
	void (fastcall *iret)(void);
};

/* Mark a paravirt probe function. */
#define paravirt_probe(fn)						\
 static asmlinkage void (*__paravirtprobe_##fn)(void) __attribute_used__ \
		__attribute__((__section__(".paravirtprobe"))) = fn

extern struct paravirt_ops paravirt_ops;

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

static inline void do_time_init(void)
{
	return paravirt_ops.time_init();
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
#endif


/* These all sit in the .parainstructions section to tell us what to patch. */
struct paravirt_patch {
	u8 *instr; 		/* original instructions */
	u8 instrtype;		/* type of this instruction */
	u8 len;			/* length of original instruction */
	u16 clobbers;		/* what registers you may clobber */
};

#define paravirt_alt(insn_string, typenum, clobber)	\
	"771:\n\t" insn_string "\n" "772:\n"		\
	".pushsection .parainstructions,\"a\"\n"	\
	"  .long 771b\n"				\
	"  .byte " __stringify(typenum) "\n"		\
	"  .byte 772b-771b\n"				\
	"  .short " __stringify(clobber) "\n"		\
	".popsection"

static inline unsigned long __raw_local_save_flags(void)
{
	unsigned long f;

	__asm__ __volatile__(paravirt_alt( "pushl %%ecx; pushl %%edx;"
					   "call *%1;"
					   "popl %%edx; popl %%ecx",
					  PARAVIRT_SAVE_FLAGS, CLBR_NONE)
			     : "=a"(f): "m"(paravirt_ops.save_fl)
			     : "memory", "cc");
	return f;
}

static inline void raw_local_irq_restore(unsigned long f)
{
	__asm__ __volatile__(paravirt_alt( "pushl %%ecx; pushl %%edx;"
					   "call *%1;"
					   "popl %%edx; popl %%ecx",
					  PARAVIRT_RESTORE_FLAGS, CLBR_EAX)
			     : "=a"(f) : "m" (paravirt_ops.restore_fl), "0"(f)
			     : "memory", "cc");
}

static inline void raw_local_irq_disable(void)
{
	__asm__ __volatile__(paravirt_alt( "pushl %%ecx; pushl %%edx;"
					   "call *%0;"
					   "popl %%edx; popl %%ecx",
					  PARAVIRT_IRQ_DISABLE, CLBR_EAX)
			     : : "m" (paravirt_ops.irq_disable)
			     : "memory", "eax", "cc");
}

static inline void raw_local_irq_enable(void)
{
	__asm__ __volatile__(paravirt_alt( "pushl %%ecx; pushl %%edx;"
					   "call *%0;"
					   "popl %%edx; popl %%ecx",
					  PARAVIRT_IRQ_ENABLE, CLBR_EAX)
			     : : "m" (paravirt_ops.irq_enable)
			     : "memory", "eax", "cc");
}

static inline unsigned long __raw_local_irq_save(void)
{
	unsigned long f;

	__asm__ __volatile__(paravirt_alt( "pushl %%ecx; pushl %%edx;"
					   "call *%1; pushl %%eax;"
					   "call *%2; popl %%eax;"
					   "popl %%edx; popl %%ecx",
					  PARAVIRT_SAVE_FLAGS_IRQ_DISABLE,
					  CLBR_NONE)
			     : "=a"(f)
			     : "m" (paravirt_ops.save_fl),
			       "m" (paravirt_ops.irq_disable)
			     : "memory", "cc");
	return f;
}

#define CLI_STRING paravirt_alt("pushl %%ecx; pushl %%edx;"		\
		     "call *paravirt_ops+%c[irq_disable];"		\
		     "popl %%edx; popl %%ecx",				\
		     PARAVIRT_IRQ_DISABLE, CLBR_EAX)

#define STI_STRING paravirt_alt("pushl %%ecx; pushl %%edx;"		\
		     "call *paravirt_ops+%c[irq_enable];"		\
		     "popl %%edx; popl %%ecx",				\
		     PARAVIRT_IRQ_ENABLE, CLBR_EAX)
#define CLI_STI_CLOBBERS , "%eax"
#define CLI_STI_INPUT_ARGS \
	,								\
	[irq_disable] "i" (offsetof(struct paravirt_ops, irq_disable)),	\
	[irq_enable] "i" (offsetof(struct paravirt_ops, irq_enable))

#else  /* __ASSEMBLY__ */

#define PARA_PATCH(ptype, clobbers, ops)	\
771:;						\
	ops;					\
772:;						\
	.pushsection .parainstructions,"a";	\
	 .long 771b;				\
	 .byte ptype;				\
	 .byte 772b-771b;			\
	 .short clobbers;			\
	.popsection

#define INTERRUPT_RETURN				\
	PARA_PATCH(PARAVIRT_INTERRUPT_RETURN, CLBR_ANY,	\
	jmp *%cs:paravirt_ops+PARAVIRT_iret)

#define DISABLE_INTERRUPTS(clobbers)			\
	PARA_PATCH(PARAVIRT_IRQ_DISABLE, clobbers,	\
	pushl %ecx; pushl %edx;				\
	call *paravirt_ops+PARAVIRT_irq_disable;	\
	popl %edx; popl %ecx)				\

#define ENABLE_INTERRUPTS(clobbers)			\
	PARA_PATCH(PARAVIRT_IRQ_ENABLE, clobbers,	\
	pushl %ecx; pushl %edx;				\
	call *%cs:paravirt_ops+PARAVIRT_irq_enable;	\
	popl %edx; popl %ecx)

#define ENABLE_INTERRUPTS_SYSEXIT			\
	PARA_PATCH(PARAVIRT_STI_SYSEXIT, CLBR_ANY,	\
	jmp *%cs:paravirt_ops+PARAVIRT_irq_enable_sysexit)

#define GET_CR0_INTO_EAX			\
	call *paravirt_ops+PARAVIRT_read_cr0

#endif /* __ASSEMBLY__ */
#endif /* CONFIG_PARAVIRT */
#endif	/* __ASM_PARAVIRT_H */
