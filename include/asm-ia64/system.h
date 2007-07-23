#ifndef _ASM_IA64_SYSTEM_H
#define _ASM_IA64_SYSTEM_H

/*
 * System defines. Note that this is included both from .c and .S
 * files, so it does only defines, not any C code.  This is based
 * on information published in the Processor Abstraction Layer
 * and the System Abstraction Layer manual.
 *
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Asit Mallick <asit.k.mallick@intel.com>
 * Copyright (C) 1999 Don Dugger <don.dugger@intel.com>
 */

#include <asm/kregs.h>
#include <asm/page.h>
#include <asm/pal.h>
#include <asm/percpu.h>

#define GATE_ADDR		RGN_BASE(RGN_GATE)

/*
 * 0xa000000000000000+2*PERCPU_PAGE_SIZE
 * - 0xa000000000000000+3*PERCPU_PAGE_SIZE remain unmapped (guard page)
 */
#define KERNEL_START		 (GATE_ADDR+__IA64_UL_CONST(0x100000000))
#define PERCPU_ADDR		(-PERCPU_PAGE_SIZE)

#ifndef __ASSEMBLY__

#include <linux/kernel.h>
#include <linux/types.h>

struct pci_vector_struct {
	__u16 segment;	/* PCI Segment number */
	__u16 bus;	/* PCI Bus number */
	__u32 pci_id;	/* ACPI split 16 bits device, 16 bits function (see section 6.1.1) */
	__u8 pin;	/* PCI PIN (0 = A, 1 = B, 2 = C, 3 = D) */
	__u32 irq;	/* IRQ assigned */
};

extern struct ia64_boot_param {
	__u64 command_line;		/* physical address of command line arguments */
	__u64 efi_systab;		/* physical address of EFI system table */
	__u64 efi_memmap;		/* physical address of EFI memory map */
	__u64 efi_memmap_size;		/* size of EFI memory map */
	__u64 efi_memdesc_size;		/* size of an EFI memory map descriptor */
	__u32 efi_memdesc_version;	/* memory descriptor version */
	struct {
		__u16 num_cols;	/* number of columns on console output device */
		__u16 num_rows;	/* number of rows on console output device */
		__u16 orig_x;	/* cursor's x position */
		__u16 orig_y;	/* cursor's y position */
	} console_info;
	__u64 fpswa;		/* physical address of the fpswa interface */
	__u64 initrd_start;
	__u64 initrd_size;
} *ia64_boot_param;

/*
 * Macros to force memory ordering.  In these descriptions, "previous"
 * and "subsequent" refer to program order; "visible" means that all
 * architecturally visible effects of a memory access have occurred
 * (at a minimum, this means the memory has been read or written).
 *
 *   wmb():	Guarantees that all preceding stores to memory-
 *		like regions are visible before any subsequent
 *		stores and that all following stores will be
 *		visible only after all previous stores.
 *   rmb():	Like wmb(), but for reads.
 *   mb():	wmb()/rmb() combo, i.e., all previous memory
 *		accesses are visible before all subsequent
 *		accesses and vice versa.  This is also known as
 *		a "fence."
 *
 * Note: "mb()" and its variants cannot be used as a fence to order
 * accesses to memory mapped I/O registers.  For that, mf.a needs to
 * be used.  However, we don't want to always use mf.a because (a)
 * it's (presumably) much slower than mf and (b) mf.a is supported for
 * sequential memory pages only.
 */
#define mb()	ia64_mf()
#define rmb()	mb()
#define wmb()	mb()
#define read_barrier_depends()	do { } while(0)

#ifdef CONFIG_SMP
# define smp_mb()	mb()
# define smp_rmb()	rmb()
# define smp_wmb()	wmb()
# define smp_read_barrier_depends()	read_barrier_depends()
#else
# define smp_mb()	barrier()
# define smp_rmb()	barrier()
# define smp_wmb()	barrier()
# define smp_read_barrier_depends()	do { } while(0)
#endif

/*
 * XXX check on this ---I suspect what Linus really wants here is
 * acquire vs release semantics but we can't discuss this stuff with
 * Linus just yet.  Grrr...
 */
#define set_mb(var, value)	do { (var) = (value); mb(); } while (0)

#define safe_halt()         ia64_pal_halt_light()    /* PAL_HALT_LIGHT */

/*
 * The group barrier in front of the rsm & ssm are necessary to ensure
 * that none of the previous instructions in the same group are
 * affected by the rsm/ssm.
 */
/* For spinlocks etc */

/*
 * - clearing psr.i is implicitly serialized (visible by next insn)
 * - setting psr.i requires data serialization
 * - we need a stop-bit before reading PSR because we sometimes
 *   write a floating-point register right before reading the PSR
 *   and that writes to PSR.mfl
 */
#define __local_irq_save(x)			\
do {						\
	ia64_stop();				\
	(x) = ia64_getreg(_IA64_REG_PSR);	\
	ia64_stop();				\
	ia64_rsm(IA64_PSR_I);			\
} while (0)

#define __local_irq_disable()			\
do {						\
	ia64_stop();				\
	ia64_rsm(IA64_PSR_I);			\
} while (0)

#define __local_irq_restore(x)	ia64_intrin_local_irq_restore((x) & IA64_PSR_I)

#ifdef CONFIG_IA64_DEBUG_IRQ

  extern unsigned long last_cli_ip;

# define __save_ip()		last_cli_ip = ia64_getreg(_IA64_REG_IP)

# define local_irq_save(x)					\
do {								\
	unsigned long psr;					\
								\
	__local_irq_save(psr);					\
	if (psr & IA64_PSR_I)					\
		__save_ip();					\
	(x) = psr;						\
} while (0)

# define local_irq_disable()	do { unsigned long x; local_irq_save(x); } while (0)

# define local_irq_restore(x)					\
do {								\
	unsigned long old_psr, psr = (x);			\
								\
	local_save_flags(old_psr);				\
	__local_irq_restore(psr);				\
	if ((old_psr & IA64_PSR_I) && !(psr & IA64_PSR_I))	\
		__save_ip();					\
} while (0)

#else /* !CONFIG_IA64_DEBUG_IRQ */
# define local_irq_save(x)	__local_irq_save(x)
# define local_irq_disable()	__local_irq_disable()
# define local_irq_restore(x)	__local_irq_restore(x)
#endif /* !CONFIG_IA64_DEBUG_IRQ */

#define local_irq_enable()	({ ia64_stop(); ia64_ssm(IA64_PSR_I); ia64_srlz_d(); })
#define local_save_flags(flags)	({ ia64_stop(); (flags) = ia64_getreg(_IA64_REG_PSR); })

#define irqs_disabled()				\
({						\
	unsigned long __ia64_id_flags;		\
	local_save_flags(__ia64_id_flags);	\
	(__ia64_id_flags & IA64_PSR_I) == 0;	\
})

#ifdef __KERNEL__

#ifdef CONFIG_IA32_SUPPORT
# define IS_IA32_PROCESS(regs)	(ia64_psr(regs)->is != 0)
#else
# define IS_IA32_PROCESS(regs)		0
struct task_struct;
static inline void ia32_save_state(struct task_struct *t __attribute__((unused))){}
static inline void ia32_load_state(struct task_struct *t __attribute__((unused))){}
#endif

/*
 * Context switch from one thread to another.  If the two threads have
 * different address spaces, schedule() has already taken care of
 * switching to the new address space by calling switch_mm().
 *
 * Disabling access to the fph partition and the debug-register
 * context switch MUST be done before calling ia64_switch_to() since a
 * newly created thread returns directly to
 * ia64_ret_from_syscall_clear_r8.
 */
extern struct task_struct *ia64_switch_to (void *next_task);

struct task_struct;

extern void ia64_save_extra (struct task_struct *task);
extern void ia64_load_extra (struct task_struct *task);

#ifdef CONFIG_PERFMON
  DECLARE_PER_CPU(unsigned long, pfm_syst_info);
# define PERFMON_IS_SYSWIDE() (__get_cpu_var(pfm_syst_info) & 0x1)
#else
# define PERFMON_IS_SYSWIDE() (0)
#endif

#define IA64_HAS_EXTRA_STATE(t)							\
	((t)->thread.flags & (IA64_THREAD_DBG_VALID|IA64_THREAD_PM_VALID)	\
	 || IS_IA32_PROCESS(task_pt_regs(t)) || PERFMON_IS_SYSWIDE())

#define __switch_to(prev,next,last) do {							 \
	if (IA64_HAS_EXTRA_STATE(prev))								 \
		ia64_save_extra(prev);								 \
	if (IA64_HAS_EXTRA_STATE(next))								 \
		ia64_load_extra(next);								 \
	ia64_psr(task_pt_regs(next))->dfh = !ia64_is_local_fpu_owner(next);			 \
	(last) = ia64_switch_to((next));							 \
} while (0)

#ifdef CONFIG_SMP
/*
 * In the SMP case, we save the fph state when context-switching away from a thread that
 * modified fph.  This way, when the thread gets scheduled on another CPU, the CPU can
 * pick up the state from task->thread.fph, avoiding the complication of having to fetch
 * the latest fph state from another CPU.  In other words: eager save, lazy restore.
 */
# define switch_to(prev,next,last) do {						\
	if (ia64_psr(task_pt_regs(prev))->mfh && ia64_is_local_fpu_owner(prev)) {				\
		ia64_psr(task_pt_regs(prev))->mfh = 0;			\
		(prev)->thread.flags |= IA64_THREAD_FPH_VALID;			\
		__ia64_save_fpu((prev)->thread.fph);				\
	}									\
	__switch_to(prev, next, last);						\
	/* "next" in old context is "current" in new context */			\
	if (unlikely((current->thread.flags & IA64_THREAD_MIGRATION) &&	       \
		     (task_cpu(current) !=				       \
		      		      task_thread_info(current)->last_cpu))) { \
		platform_migrate(current);				       \
		task_thread_info(current)->last_cpu = task_cpu(current);       \
	}								       \
} while (0)
#else
# define switch_to(prev,next,last)	__switch_to(prev, next, last)
#endif

#define __ARCH_WANT_UNLOCKED_CTXSW
#define ARCH_HAS_PREFETCH_SWITCH_STACK
#define ia64_platform_is(x) (strcmp(x, platform_name) == 0)

void cpu_idle_wait(void);

#define arch_align_stack(x) (x)

void default_idle(void);

#endif /* __KERNEL__ */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_IA64_SYSTEM_H */
