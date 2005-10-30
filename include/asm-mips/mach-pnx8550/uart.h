#ifndef __IP3106_UART_H
#define __IP3106_UART_H

#include <int.h>

/* early macros for kgdb use. fixme: clean this up */

#define UART_BASE		0xbbe4a000	/* PNX8550 */

#define PNX8550_UART_PORT0	(UART_BASE)
#define PNX8550_UART_PORT1	(UART_BASE + 0x1000)

#define PNX8550_UART_INT(x)		(PNX8550_INT_GIC_MIN+19+x)
#define IRQ_TO_UART(x)			(x-PNX8550_INT_GIC_MIN-19)

#endif
