#ifndef ASM_X86__I8259_H
#define ASM_X86__I8259_H

#include <linux/delay.h>

extern unsigned int cached_irq_mask;

#define __byte(x, y)		(((unsigned char *)&(y))[x])
#define cached_master_mask	(__byte(0, cached_irq_mask))
#define cached_slave_mask	(__byte(1, cached_irq_mask))

/* i8259A PIC registers */
#define PIC_MASTER_CMD		0x20
#define PIC_MASTER_IMR		0x21
#define PIC_MASTER_ISR		PIC_MASTER_CMD
#define PIC_MASTER_POLL		PIC_MASTER_ISR
#define PIC_MASTER_OCW3		PIC_MASTER_ISR
#define PIC_SLAVE_CMD		0xa0
#define PIC_SLAVE_IMR		0xa1

/* i8259A PIC related value */
#define PIC_CASCADE_IR		2
#define MASTER_ICW4_DEFAULT	0x01
#define SLAVE_ICW4_DEFAULT	0x01
#define PIC_ICW4_AEOI		2

extern spinlock_t i8259A_lock;

extern void init_8259A(int auto_eoi);
extern void enable_8259A_irq(unsigned int irq);
extern void disable_8259A_irq(unsigned int irq);
extern unsigned int startup_8259A_irq(unsigned int irq);

/* the PIC may need a careful delay on some platforms, hence specific calls */
static inline unsigned char inb_pic(unsigned int port)
{
	unsigned char value = inb(port);

	/*
	 * delay for some accesses to PIC on motherboard or in chipset
	 * must be at least one microsecond, so be safe here:
	 */
	udelay(2);

	return value;
}

static inline void outb_pic(unsigned char value, unsigned int port)
{
	outb(value, port);
	/*
	 * delay for some accesses to PIC on motherboard or in chipset
	 * must be at least one microsecond, so be safe here:
	 */
	udelay(2);
}

extern struct irq_chip i8259A_chip;

extern void mask_8259A(void);
extern void unmask_8259A(void);

#endif /* ASM_X86__I8259_H */
