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

#define HAVE_ARCH_COPY_OLDMEM_PAGE

#ifndef __ASSEMBLY__

#ifdef CONFIG_KEXEC

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

#endif /* !CONFIG_KEXEC */

#endif /* ! __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_KEXEC_H */
