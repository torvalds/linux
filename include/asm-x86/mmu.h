#ifndef ASM_X86__MMU_H
#define ASM_X86__MMU_H

#include <linux/spinlock.h>
#include <linux/mutex.h>

/*
 * The x86 doesn't have a mmu context, but
 * we put the segment information here.
 *
 * cpu_vm_mask is used to optimize ldt flushing.
 */
typedef struct {
	void *ldt;
#ifdef CONFIG_X86_64
	rwlock_t ldtlock;
#endif
	int size;
	struct mutex lock;
	void *vdso;
} mm_context_t;

#ifdef CONFIG_SMP
void leave_mm(int cpu);
#else
static inline void leave_mm(int cpu)
{
}
#endif

#endif /* ASM_X86__MMU_H */
