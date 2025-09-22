/* Public domain. */

#ifndef _LINUX_IRQRETURN_H
#define _LINUX_IRQRETURN_H

typedef int irqreturn_t;
enum irqreturn {
	IRQ_NONE = 0,
	IRQ_HANDLED = 1
};

#endif
