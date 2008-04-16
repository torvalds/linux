#ifndef _ASM_X86_THREAD_INFO_H
#ifdef CONFIG_X86_32
# include "thread_info_32.h"
#else
# include "thread_info_64.h"
#endif

#ifndef __ASSEMBLY__
extern void arch_task_cache_init(void);
extern void free_thread_info(struct thread_info *ti);
extern int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src);
#define arch_task_cache_init arch_task_cache_init
#endif
#endif /* _ASM_X86_THREAD_INFO_H */
