#ifndef __i386_MMU_H
#define __i386_MMU_H

#include <linux/mutex.h>
/*
 * The i386 doesn't have a mmu context, but
 * we put the segment information here.
 *
 * cpu_vm_mask is used to optimize ldt flushing.
 */
typedef struct { 
	int size;
	struct mutex lock;
	void *ldt;
	void *vdso;
} mm_context_t;

#endif
