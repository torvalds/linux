/*
 * include/linux/serial.h
 *
 * Copyright (C) 1992 by Theodore Ts'o.
 * 
 * Redistribution of this file is permitted under the terms of the GNU 
 * Public License (GPL)
 */
#ifndef _LINUX_SERIAL_H
#define _LINUX_SERIAL_H

#include <uapi/linux/serial.h>
#include <uapi/linux/serial_reg.h>

#define UART_IER_ALL_INTR	(UART_IER_MSI | \
				 UART_IER_RLSI | \
				 UART_IER_THRI | \
				 UART_IER_RDI)

/* Helper for dealing with UART_LCR_WLEN* defines */
#define UART_LCR_WLEN(x)	((x) - 5)

/* FIFO and shifting register empty */
#define UART_LSR_BOTH_EMPTY	(UART_LSR_TEMT | UART_LSR_THRE)

static inline bool uart_lsr_tx_empty(u16 lsr)
{
	return (lsr & UART_LSR_BOTH_EMPTY) == UART_LSR_BOTH_EMPTY;
}

#define UART_MSR_STATUS_BITS	(UART_MSR_DCD | \
				 UART_MSR_RI | \
				 UART_MSR_DSR | \
				 UART_MSR_CTS)

/*
 * Counters of the input lines (CTS, DSR, RI, CD) interrupts
 */

struct async_icount {
	__u32	cts, dsr, rng, dcd, tx, rx;
	__u32	frame, parity, overrun, brk;
	__u32	buf_overrun;
};

#include <linux/compiler.h>

#endif /* _LINUX_SERIAL_H */
