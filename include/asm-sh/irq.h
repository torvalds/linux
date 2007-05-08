#ifndef __ASM_SH_IRQ_H
#define __ASM_SH_IRQ_H

#include <asm/machvec.h>

/*
 * A sane default based on a reasonable vector table size, platforms are
 * advised to cap this at the hard limit that they're interested in
 * through the machvec.
 */
#define NR_IRQS 256

/*
 * Convert back and forth between INTEVT and IRQ values.
 */
#ifdef CONFIG_CPU_HAS_INTEVT
#define evt2irq(evt)		(((evt) >> 5) - 16)
#define irq2evt(irq)		(((irq) + 16) << 5)
#else
#define evt2irq(evt)		(evt)
#define irq2evt(irq)		(irq)
#endif

/*
 * Simple Mask Register Support
 */
extern void make_maskreg_irq(unsigned int irq);
extern unsigned short *irq_mask_register;

/*
 * PINT IRQs
 */
void init_IRQ_pint(void);

/*
 * The shift value is now the number of bits to shift, not the number of
 * bits/4. This is to make it easier to read the value directly from the
 * datasheets. The IPR address, addr, will be set from ipr_idx via the
 * map_ipridx_to_addr function.
 */
struct ipr_data {
	unsigned int irq;
	int ipr_idx;		/* Index for the IPR registered */
	int shift;		/* Number of bits to shift the data */
	int priority;		/* The priority */
	unsigned int addr;	/* Address of Interrupt Priority Register */
};

/*
 * Given an IPR IDX, map the value to an IPR register address.
 */
unsigned int map_ipridx_to_addr(int idx);

/*
 * Enable individual interrupt mode for external IPR IRQs.
 */
void ipr_irq_enable_irlm(void);

/*
 * Function for "on chip support modules".
 */
void make_ipr_irq(struct ipr_data *table, unsigned int nr_irqs);
void make_imask_irq(unsigned int irq);
void init_IRQ_ipr(void);

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
