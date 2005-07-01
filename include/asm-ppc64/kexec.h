#ifndef _PPC64_KEXEC_H
#define _PPC64_KEXEC_H

/*
 * KEXEC_SOURCE_MEMORY_LIMIT maximum page get_free_page can return.
 * I.e. Maximum page that is mapped directly into kernel memory,
 * and kmap is not required.
 */

/* Maximum physical address we can use pages from */
/* XXX: since we copy virt we can use any page we allocate */
#define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)

/* Maximum address we can reach in physical address mode */
/* XXX: I want to allow initrd in highmem.  otherwise set to rmo on lpar */
#define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)

/* Maximum address we can use for the control code buffer */
/* XXX: unused today, ppc32 uses TASK_SIZE, probably left over from use_mm  */
#define KEXEC_CONTROL_MEMORY_LIMIT (-1UL)

/* XXX: today we don't use this at all, althogh we have a static stack */
#define KEXEC_CONTROL_CODE_SIZE 4096

/* The native architecture */
#define KEXEC_ARCH KEXEC_ARCH_PPC64

#define MAX_NOTE_BYTES 1024

#ifndef __ASSEMBLY__

typedef u32 note_buf_t[MAX_NOTE_BYTES/4];

extern note_buf_t crash_notes[];

extern void kexec_smp_wait(void);	/* get and clear naca physid, wait for
					  master to copy new code to 0 */

#endif /* __ASSEMBLY__ */
#endif /* _PPC_KEXEC_H */

