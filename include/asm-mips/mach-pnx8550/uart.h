#ifndef __IP3106_UART_H
#define __IP3106_UART_H

#include <int.h>

/* early macros for kgdb use. fixme: clean this up */

#define UART_BASE		0xbbe4a000	/* PNX8550 */

#define PNX8550_UART_PORT0	(UART_BASE)
#define PNX8550_UART_PORT1	(UART_BASE + 0x1000)

#define PNX8550_UART_INT(x)		(PNX8550_INT_GIC_MIN+19+x)
#define IRQ_TO_UART(x)			(x-PNX8550_INT_GIC_MIN-19)

/* early macros needed for prom/kgdb */

#define ip3106_lcr(base,port)    *(volatile u32 *)(base+(port*0x1000) + 0x000)
#define ip3106_mcr(base, port)   *(volatile u32 *)(base+(port*0x1000) + 0x004)
#define ip3106_baud(base, port)  *(volatile u32 *)(base+(port*0x1000) + 0x008)
#define ip3106_cfg(base, port)   *(volatile u32 *)(base+(port*0x1000) + 0x00C)
#define ip3106_fifo(base, port)	 *(volatile u32 *)(base+(port*0x1000) + 0x028)
#define ip3106_istat(base, port) *(volatile u32 *)(base+(port*0x1000) + 0xFE0)
#define ip3106_ien(base, port)   *(volatile u32 *)(base+(port*0x1000) + 0xFE4)
#define ip3106_iclr(base, port)  *(volatile u32 *)(base+(port*0x1000) + 0xFE8)
#define ip3106_iset(base, port)  *(volatile u32 *)(base+(port*0x1000) + 0xFEC)
#define ip3106_pd(base, port)    *(volatile u32 *)(base+(port*0x1000) + 0xFF4)
#define ip3106_mid(base, port)   *(volatile u32 *)(base+(port*0x1000) + 0xFFC)

#endif
