#ifndef _X86_64_KEXEC_H
#define _X86_64_KEXEC_H

#include <asm/page.h>
#include <asm/proto.h>

/*
 * KEXEC_SOURCE_MEMORY_LIMIT maximum page get_free_page can return.
 * I.e. Maximum page that is mapped directly into kernel memory,
 * and kmap is not required.
 *
 * So far x86_64 is limited to 40 physical address bits.
 */

/* Maximum physical address we can use pages from */
#define KEXEC_SOURCE_MEMORY_LIMIT      (0xFFFFFFFFFFUL)
/* Maximum address we can reach in physical address mode */
#define KEXEC_DESTINATION_MEMORY_LIMIT (0xFFFFFFFFFFUL)
/* Maximum address we can use for the control pages */
#define KEXEC_CONTROL_MEMORY_LIMIT     (0xFFFFFFFFFFUL)

/* Allocate one page for the pdp and the second for the code */
#define KEXEC_CONTROL_CODE_SIZE  (4096UL + 4096UL)

/* The native architecture */
#define KEXEC_ARCH KEXEC_ARCH_X86_64

#define MAX_NOTE_BYTES 1024
typedef u32 note_buf_t[MAX_NOTE_BYTES/4];

extern note_buf_t crash_notes[];

#endif /* _X86_64_KEXEC_H */
