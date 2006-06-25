/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */ 

#ifdef CONFIG_RTE_CB_ME2

#include <asm/rte_me2_cb.h>

#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)

#define irq_cannonicalize(x) (x)
#define BASE_BAUD	250000	/* (16MHz / (16 * 38400)) * 9600 */
#define SERIAL_PORT_DFNS \
   { 0, BASE_BAUD, CB_UART_BASE, IRQ_CB_EXTSIO, STD_COM_FLAGS },

/* Redefine UART register offsets.  */
#undef UART_RX
#undef UART_TX
#undef UART_DLL
#undef UART_TRG
#undef UART_DLM
#undef UART_IER
#undef UART_FCTR
#undef UART_IIR
#undef UART_FCR
#undef UART_EFR
#undef UART_LCR
#undef UART_MCR
#undef UART_LSR
#undef UART_MSR
#undef UART_SCR
#undef UART_EMSR

#define UART_RX		(0 * CB_UART_REG_GAP)
#define UART_TX		(0 * CB_UART_REG_GAP)
#define UART_DLL	(0 * CB_UART_REG_GAP)
#define UART_TRG	(0 * CB_UART_REG_GAP)
#define UART_DLM	(1 * CB_UART_REG_GAP)
#define UART_IER	(1 * CB_UART_REG_GAP)
#define UART_FCTR	(1 * CB_UART_REG_GAP)
#define UART_IIR	(2 * CB_UART_REG_GAP)
#define UART_FCR	(2 * CB_UART_REG_GAP)
#define UART_EFR	(2 * CB_UART_REG_GAP)
#define UART_LCR	(3 * CB_UART_REG_GAP)
#define UART_MCR	(4 * CB_UART_REG_GAP)
#define UART_LSR	(5 * CB_UART_REG_GAP)
#define UART_MSR	(6 * CB_UART_REG_GAP)
#define UART_SCR	(7 * CB_UART_REG_GAP)
#define UART_EMSR	(7 * CB_UART_REG_GAP)

#endif /* CONFIG_RTE_CB_ME2 */
