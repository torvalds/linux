/* ASB2305-specific 8250 serial ports
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_UNIT_SERIAL_H
#define _ASM_UNIT_SERIAL_H

#include <asm/cpu/cpu-regs.h>
#include <asm/proc/irq.h>
#include <linux/serial_reg.h>

#define SERIAL_PORT0_BASE_ADDRESS	0xA6FB0000
#define ASB2305_DEBUG_MCR	__SYSREG(0xA6FB0000 + UART_MCR * 2, u8)

#define SERIAL_IRQ	XIRQ0	/* Dual serial (PC16552)	(Hi) */

/*
 * dispose of the /dev/ttyS0 serial port
 */
#ifndef CONFIG_GDBSTUB_ON_TTYSx

#define SERIAL_PORT_DFNS						\
	{								\
	.baud_base		= BASE_BAUD,				\
	.irq			= SERIAL_IRQ,				\
	.flags			= STD_COM_FLAGS,			\
	.iomem_base		= (u8 *) SERIAL_PORT0_BASE_ADDRESS,	\
	.iomem_reg_shift	= 2,					\
	.io_type		= SERIAL_IO_MEM,			\
	},

#ifndef __ASSEMBLY__

static inline void __debug_to_serial(const char *p, int n)
{
}

#endif /* !__ASSEMBLY__ */

#else /* CONFIG_GDBSTUB_ON_TTYSx */

#define SERIAL_PORT_DFNS /* stolen by gdb-stub */

#if defined(CONFIG_GDBSTUB_ON_TTYS0)
#define GDBPORT_SERIAL_RX	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_RX  * 4, u8)
#define GDBPORT_SERIAL_TX	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_TX  * 4, u8)
#define GDBPORT_SERIAL_DLL	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_DLL * 4, u8)
#define GDBPORT_SERIAL_DLM	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_DLM * 4, u8)
#define GDBPORT_SERIAL_IER	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_IER * 4, u8)
#define GDBPORT_SERIAL_IIR	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_IIR * 4, u8)
#define GDBPORT_SERIAL_FCR	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_FCR * 4, u8)
#define GDBPORT_SERIAL_LCR	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_LCR * 4, u8)
#define GDBPORT_SERIAL_MCR	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_MCR * 4, u8)
#define GDBPORT_SERIAL_LSR	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_LSR * 4, u8)
#define GDBPORT_SERIAL_MSR	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_MSR * 4, u8)
#define GDBPORT_SERIAL_SCR	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_SCR * 4, u8)
#define GDBPORT_SERIAL_IRQ	SERIAL_IRQ

#elif defined(CONFIG_GDBSTUB_ON_TTYS1)
#error The ASB2305 doesnt have a /dev/ttyS1
#endif

#ifndef __ASSEMBLY__

#define TTYS0_TX	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_TX  * 4, u8)
#define TTYS0_MCR	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_MCR * 4, u8)
#define TTYS0_LSR	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_LSR * 4, u8)
#define TTYS0_MSR	__SYSREG(SERIAL_PORT0_BASE_ADDRESS + UART_MSR * 4, u8)

#define LSR_WAIT_FOR(STATE)				\
do {							\
	while (!(TTYS0_LSR & UART_LSR_##STATE)) {}	\
} while (0)
#define FLOWCTL_WAIT_FOR(LINE)				\
do {							\
	while (!(TTYS0_MSR & UART_MSR_##LINE)) {}	\
} while (0)
#define FLOWCTL_CLEAR(LINE)			\
do {						\
	TTYS0_MCR &= ~UART_MCR_##LINE;		\
} while (0)
#define FLOWCTL_SET(LINE)			\
do {						\
	TTYS0_MCR |= UART_MCR_##LINE;		\
} while (0)
#define FLOWCTL_QUERY(LINE)	({ TTYS0_MSR & UART_MSR_##LINE; })

static inline void __debug_to_serial(const char *p, int n)
{
	char ch;

	FLOWCTL_SET(DTR);

	for (; n > 0; n--) {
		LSR_WAIT_FOR(THRE);
		FLOWCTL_WAIT_FOR(CTS);

		ch = *p++;
		if (ch == 0x0a) {
			TTYS0_TX = 0x0d;
			LSR_WAIT_FOR(THRE);
			FLOWCTL_WAIT_FOR(CTS);
		}
		TTYS0_TX = ch;
	}

	FLOWCTL_CLEAR(DTR);
}

#endif /* !__ASSEMBLY__ */

#endif /* CONFIG_GDBSTUB_ON_TTYSx */

#endif /* _ASM_UNIT_SERIAL_H */
