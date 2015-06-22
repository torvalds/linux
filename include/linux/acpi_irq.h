#ifndef _LINUX_ACPI_IRQ_H
#define _LINUX_ACPI_IRQ_H

#include <linux/irq.h>

#ifndef acpi_irq_init
static inline void acpi_irq_init(void) { }
#endif

#endif /* _LINUX_ACPI_IRQ_H */
