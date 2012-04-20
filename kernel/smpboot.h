#ifndef SMPBOOT_H
#define SMPBOOT_H

struct task_struct;

int smpboot_prepare(unsigned int cpu);

#ifdef CONFIG_GENERIC_SMP_IDLE_THREAD
struct task_struct *idle_thread_get(unsigned int cpu);
void idle_thread_set_boot_cpu(void);
#else
static inline struct task_struct *idle_thread_get(unsigned int cpu) { return NULL; }
static inline void idle_thread_set_boot_cpu(void) { }
#endif

#endif
