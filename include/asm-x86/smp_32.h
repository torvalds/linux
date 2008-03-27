#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#ifndef __ASSEMBLY__

extern cpumask_t cpu_callin_map;

extern void (*mtrr_hook)(void);
extern void zap_low_mappings(void);

#endif /* !ASSEMBLY */
#endif
