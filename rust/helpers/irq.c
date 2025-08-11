// SPDX-License-Identifier: GPL-2.0

#include <linux/interrupt.h>

int rust_helper_request_irq(unsigned int irq, irq_handler_t handler,
			    unsigned long flags, const char *name, void *dev)
{
	return request_irq(irq, handler, flags, name, dev);
}
