#ifndef __ASM_POWERPC_SUSPEND_H
#define __ASM_POWERPC_SUSPEND_H

static inline int arch_prepare_suspend(void) { return 0; }

void save_processor_state(void);
void restore_processor_state(void);

#endif /* __ASM_POWERPC_SUSPEND_H */
