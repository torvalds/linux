#ifndef __x86_64_MMU_H
#define __x86_64_MMU_H

#include <linux/spinlock.h>
#include <linux/mutex.h>

/*
 * The x86_64 doesn't have a mmu context, but
 * we put the segment information here.
 *
 * cpu_vm_mask is used to optimize ldt flushing.
 */
typedef struct { 
	void *ldt;
	rwlock_t ldtlock; 
	int size;
	struct mutex lock;
	void *vdso;
} mm_context_t;

#endif
