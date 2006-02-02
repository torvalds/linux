#ifndef _ASM_POWERPC_KEXEC_H
#define _ASM_POWERPC_KEXEC_H
#ifdef __KERNEL__

/*
 * Maximum page that is mapped directly into kernel memory.
 * XXX: Since we copy virt we can use any page we allocate
 */
#define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)

/*
 * Maximum address we can reach in physical address mode.
 * XXX: I want to allow initrd in highmem. Otherwise set to rmo on LPAR.
 */
#define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)

/* Maximum address we can use for the control code buffer */
#ifdef __powerpc64__
#define KEXEC_CONTROL_MEMORY_LIMIT (-1UL)
#else
/* TASK_SIZE, probably left over from use_mm ?? */
#define KEXEC_CONTROL_MEMORY_LIMIT TASK_SIZE
#endif

#define KEXEC_CONTROL_CODE_SIZE 4096

/* The native architecture */
#ifdef __powerpc64__
#define KEXEC_ARCH KEXEC_ARCH_PPC64
#else
#define KEXEC_ARCH KEXEC_ARCH_PPC
#endif

#ifdef CONFIG_KEXEC

#ifndef __ASSEMBLY__
#ifdef __powerpc64__
/*
 * This function is responsible for capturing register states if coming
 * via panic or invoking dump using sysrq-trigger.
 */
static inline void crash_setup_regs(struct pt_regs *newregs,
					struct pt_regs *oldregs)
{
	if (oldregs)
		memcpy(newregs, oldregs, sizeof(*newregs));
	else {
		/* FIXME Merge this with xmon_save_regs ?? */
		unsigned long tmp1, tmp2;
		__asm__ __volatile__ (
			"std    0,0(%2)\n"
			"std    1,8(%2)\n"
			"std    2,16(%2)\n"
			"std    3,24(%2)\n"
			"std    4,32(%2)\n"
			"std    5,40(%2)\n"
			"std    6,48(%2)\n"
			"std    7,56(%2)\n"
			"std    8,64(%2)\n"
			"std    9,72(%2)\n"
			"std    10,80(%2)\n"
			"std    11,88(%2)\n"
			"std    12,96(%2)\n"
			"std    13,104(%2)\n"
			"std    14,112(%2)\n"
			"std    15,120(%2)\n"
			"std    16,128(%2)\n"
			"std    17,136(%2)\n"
			"std    18,144(%2)\n"
			"std    19,152(%2)\n"
			"std    20,160(%2)\n"
			"std    21,168(%2)\n"
			"std    22,176(%2)\n"
			"std    23,184(%2)\n"
			"std    24,192(%2)\n"
			"std    25,200(%2)\n"
			"std    26,208(%2)\n"
			"std    27,216(%2)\n"
			"std    28,224(%2)\n"
			"std    29,232(%2)\n"
			"std    30,240(%2)\n"
			"std    31,248(%2)\n"
			"mfmsr  %0\n"
			"std    %0, 264(%2)\n"
			"mfctr  %0\n"
			"std    %0, 280(%2)\n"
			"mflr   %0\n"
			"std    %0, 288(%2)\n"
			"bl     1f\n"
		"1:     mflr   %1\n"
			"std    %1, 256(%2)\n"
			"mtlr   %0\n"
			"mfxer  %0\n"
			"std    %0, 296(%2)\n"
			: "=&r" (tmp1), "=&r" (tmp2)
			: "b" (newregs));
	}
}
#else
/*
 * Provide a dummy definition to avoid build failures. Will remain
 * empty till crash dump support is enabled.
 */
static inline void crash_setup_regs(struct pt_regs *newregs,
					struct pt_regs *oldregs) { }
#endif /* !__powerpc64 __ */

#define MAX_NOTE_BYTES 1024

#ifdef __powerpc64__
extern void kexec_smp_wait(void);	/* get and clear naca physid, wait for
					  master to copy new code to 0 */
extern void __init kexec_setup(void);
extern int crashing_cpu;
extern void crash_send_ipi(void (*crash_ipi_callback)(struct pt_regs *));
#endif /* __powerpc64 __ */

struct kimage;
struct pt_regs;
extern void default_machine_kexec(struct kimage *image);
extern int default_machine_kexec_prepare(struct kimage *image);
extern void default_machine_crash_shutdown(struct pt_regs *regs);

extern void machine_kexec_simple(struct kimage *image);

#endif /* ! __ASSEMBLY__ */
#endif /* CONFIG_KEXEC */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_KEXEC_H */
