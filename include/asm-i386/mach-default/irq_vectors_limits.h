#ifndef _ASM_IRQ_VECTORS_LIMITS_H
#define _ASM_IRQ_VECTORS_LIMITS_H

#ifdef CONFIG_PCI_MSI
#define NR_IRQS FIRST_SYSTEM_VECTOR
#define NR_IRQ_VECTORS NR_IRQS
#else
#ifdef CONFIG_X86_IO_APIC
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
#endif

#endif /* _ASM_IRQ_VECTORS_LIMITS_H */
