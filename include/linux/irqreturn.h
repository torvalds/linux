/* irqreturn.h */
#ifndef _LINUX_IRQRETURN_H
#define _LINUX_IRQRETURN_H

/*
 * For 2.4.x compatibility, 2.4.x can use
 *
 *	typedef void irqreturn_t;
 *	#define IRQ_NONE
 *	#define IRQ_HANDLED
 *	#define IRQ_RETVAL(x)
 *
 * To mix old-style and new-style irq handler returns.
 *
 * IRQ_NONE means we didn't handle it.
 * IRQ_HANDLED means that we did have a valid interrupt and handled it.
 * IRQ_RETVAL(x) selects on the two depending on x being non-zero (for handled)
 */
typedef int irqreturn_t;

#define IRQ_NONE	(0)
#define IRQ_HANDLED	(1)
#define IRQ_RETVAL(x)	((x) != 0)

#endif
