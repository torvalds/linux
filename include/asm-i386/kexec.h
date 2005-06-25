#ifndef _I386_KEXEC_H
#define _I386_KEXEC_H

#include <asm/fixmap.h>

/*
 * KEXEC_SOURCE_MEMORY_LIMIT maximum page get_free_page can return.
 * I.e. Maximum page that is mapped directly into kernel memory,
 * and kmap is not required.
 *
 * Someone correct me if FIXADDR_START - PAGEOFFSET is not the correct
 * calculation for the amount of memory directly mappable into the
 * kernel memory space.
 */

/* Maximum physical address we can use pages from */
#define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)
/* Maximum address we can reach in physical address mode */
#define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)
/* Maximum address we can use for the control code buffer */
#define KEXEC_CONTROL_MEMORY_LIMIT TASK_SIZE

#define KEXEC_CONTROL_CODE_SIZE	4096

/* The native architecture */
#define KEXEC_ARCH KEXEC_ARCH_386

#define MAX_NOTE_BYTES 1024
typedef u32 note_buf_t[MAX_NOTE_BYTES/4];

extern note_buf_t crash_notes[];

#endif /* _I386_KEXEC_H */
