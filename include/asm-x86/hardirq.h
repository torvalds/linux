#ifdef CONFIG_X86_32
# include "hardirq_32.h"
#else
# include "hardirq_64.h"
#endif

extern u64 arch_irq_stat_cpu(unsigned int cpu);
#define arch_irq_stat_cpu	arch_irq_stat_cpu

extern u64 arch_irq_stat(void);
#define arch_irq_stat		arch_irq_stat
