#ifndef _LINUX_IRQRETURN_H
#define _LINUX_IRQRETURN_H

/**
 * enum irqreturn
 * @IRQ_NONE		interrupt was not from this device
 * @IRQ_HANDLED		interrupt was handled by this device
 */
enum irqreturn {
	IRQ_NONE,
	IRQ_HANDLED,
};

typedef enum irqreturn irqreturn_t;
#define IRQ_RETVAL(x)	((x) != IRQ_NONE)

#endif
