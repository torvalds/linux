#ifndef __ASM_SH_IRQ_H
#define __ASM_SH_IRQ_H

#include <asm/machvec.h>
#include <asm/ptrace.h>		/* for pt_regs */

/* NR_IRQS is made from three components:
 *   1. ONCHIP_NR_IRQS - number of IRLS + on-chip peripherial modules
 *   2. PINT_NR_IRQS   - number of PINT interrupts
 *   3. OFFCHIP_NR_IRQS - numbe of IRQs from off-chip peripherial modules
 */

/* 1. ONCHIP_NR_IRQS */
#if defined(CONFIG_CPU_SUBTYPE_SH7604)
# define ONCHIP_NR_IRQS 24	// Actually 21
#elif defined(CONFIG_CPU_SUBTYPE_SH7707)
# define ONCHIP_NR_IRQS 64
# define PINT_NR_IRQS   16
#elif defined(CONFIG_CPU_SUBTYPE_SH7708)
# define ONCHIP_NR_IRQS 32
#elif defined(CONFIG_CPU_SUBTYPE_SH7709) || \
      defined(CONFIG_CPU_SUBTYPE_SH7706) || \
      defined(CONFIG_CPU_SUBTYPE_SH7705)
# define ONCHIP_NR_IRQS 64	// Actually 61
# define PINT_NR_IRQS   16
#elif defined(CONFIG_CPU_SUBTYPE_SH7710)
# define ONCHIP_NR_IRQS 104
#elif defined(CONFIG_CPU_SUBTYPE_SH7750)
# define ONCHIP_NR_IRQS 48	// Actually 44
#elif defined(CONFIG_CPU_SUBTYPE_SH7751)
# define ONCHIP_NR_IRQS 72
#elif defined(CONFIG_CPU_SUBTYPE_SH7760)
# define ONCHIP_NR_IRQS 112	/* XXX */
#elif defined(CONFIG_CPU_SUBTYPE_SH4_202)
# define ONCHIP_NR_IRQS 72
#elif defined(CONFIG_CPU_SUBTYPE_ST40STB1)
# define ONCHIP_NR_IRQS 144
#elif defined(CONFIG_CPU_SUBTYPE_SH7300) || \
      defined(CONFIG_CPU_SUBTYPE_SH73180) || \
      defined(CONFIG_CPU_SUBTYPE_SH7343)
# define ONCHIP_NR_IRQS 109
#elif defined(CONFIG_CPU_SUBTYPE_SH7780)
# define ONCHIP_NR_IRQS 111
#elif defined(CONFIG_CPU_SUBTYPE_SH7206)
# define ONCHIP_NR_IRQS 256
#elif defined(CONFIG_CPU_SUBTYPE_SH7619)
# define ONCHIP_NR_IRQS 128
#elif defined(CONFIG_SH_UNKNOWN)	/* Most be last */
# define ONCHIP_NR_IRQS 144
#endif

/* 2. PINT_NR_IRQS */
#ifdef CONFIG_SH_UNKNOWN
# define PINT_NR_IRQS 16
#else
# ifndef PINT_NR_IRQS
#  define PINT_NR_IRQS 0
# endif
#endif

#if PINT_NR_IRQS > 0
# define PINT_IRQ_BASE  ONCHIP_NR_IRQS
#endif

/* 3. OFFCHIP_NR_IRQS */
#if defined(CONFIG_HD64461)
# define OFFCHIP_NR_IRQS 18
#elif defined (CONFIG_SH_BIGSUR) /* must be before CONFIG_HD64465 */
# define OFFCHIP_NR_IRQS 48
#elif defined(CONFIG_HD64465)
# define OFFCHIP_NR_IRQS 16
#elif defined (CONFIG_SH_EC3104)
# define OFFCHIP_NR_IRQS 16
#elif defined (CONFIG_SH_DREAMCAST)
# define OFFCHIP_NR_IRQS 96
#elif defined (CONFIG_SH_TITAN)
# define OFFCHIP_NR_IRQS 4
#elif defined(CONFIG_SH_R7780RP)
# define OFFCHIP_NR_IRQS 16
#elif defined(CONFIG_SH_7343_SOLUTION_ENGINE)
# define OFFCHIP_NR_IRQS 12
#elif defined(CONFIG_SH_UNKNOWN)
# define OFFCHIP_NR_IRQS 16	/* Must also be last */
#else
# define OFFCHIP_NR_IRQS 0
#endif

#if OFFCHIP_NR_IRQS > 0
# define OFFCHIP_IRQ_BASE (ONCHIP_NR_IRQS + PINT_NR_IRQS)
#endif

/* NR_IRQS. 1+2+3 */
#define NR_IRQS (ONCHIP_NR_IRQS + PINT_NR_IRQS + OFFCHIP_NR_IRQS)

/*
 * Simple Mask Register Support
 */
extern void make_maskreg_irq(unsigned int irq);
extern unsigned short *irq_mask_register;

/*
 * PINT IRQs
 */
void init_IRQ_pint(void);

struct ipr_data {
	unsigned int irq;
	unsigned int addr;	/* Address of Interrupt Priority Register */
	int shift;		/* Shifts of the 16-bit data */
	int priority;		/* The priority */
};

/*
 * Function for "on chip support modules".
 */
extern void make_ipr_irq(struct ipr_data *table, unsigned int nr_irqs);
extern void make_imask_irq(unsigned int irq);

struct intc2_data {
	unsigned short irq;
	unsigned char ipr_offset, ipr_shift;
	unsigned char msk_offset, msk_shift;
	unsigned char priority;
};

void make_intc2_irq(struct intc2_data *, unsigned int nr_irqs);
void init_IRQ_intc2(void);

static inline int generic_irq_demux(int irq)
{
	return irq;
}

#define irq_canonicalize(irq)	(irq)
#define irq_demux(irq)		sh_mv.mv_irq_demux(irq)

#ifdef CONFIG_4KSTACKS
extern void irq_ctx_init(int cpu);
extern void irq_ctx_exit(int cpu);
# define __ARCH_HAS_DO_SOFTIRQ
#else
# define irq_ctx_init(cpu) do { } while (0)
# define irq_ctx_exit(cpu) do { } while (0)
#endif

#endif /* __ASM_SH_IRQ_H */
