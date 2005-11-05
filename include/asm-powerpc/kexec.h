#ifndef _ASM_POWERPC_KEXEC_H
#define _ASM_POWERPC_KEXEC_H

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

#ifndef __ASSEMBLY__

#define MAX_NOTE_BYTES 1024
typedef u32 note_buf_t[MAX_NOTE_BYTES / sizeof(u32)];

extern note_buf_t crash_notes[];

#ifdef __powerpc64__
extern void kexec_smp_wait(void);	/* get and clear naca physid, wait for
					  master to copy new code to 0 */
#else
struct kimage;
extern void machine_kexec_simple(struct kimage *image);
#endif

#endif /* ! __ASSEMBLY__ */
#endif /* _ASM_POWERPC_KEXEC_H */
