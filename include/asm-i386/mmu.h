#ifndef __i386_MMU_H
#define __i386_MMU_H

#include <asm/semaphore.h>
/*
 * The i386 doesn't have a mmu context, but
 * we put the segment information here.
 *
 * cpu_vm_mask is used to optimize ldt flushing.
 */
typedef struct { 
	int size;
	struct semaphore sem;
	void *ldt;
} mm_context_t;

#endif
