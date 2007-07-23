#ifndef _ASM_IRQ_VECTORS_LIMITS_H
#define _ASM_IRQ_VECTORS_LIMITS_H

#if defined(CONFIG_X86_IO_APIC) || defined(CONFIG_PARAVIRT)
#define NR_IRQS 224
# if (224 >= 32 * NR_CPUS)
# define NR_IRQ_VECTORS NR_IRQS
# else
# define NR_IRQ_VECTORS (32 * NR_CPUS)
# endif
#else
#define NR_IRQS 16
#define NR_IRQ_VECTORS NR_IRQS
#endif

#endif /* _ASM_IRQ_VECTORS_LIMITS_H */
