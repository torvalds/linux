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

/* Helper for dealing with UART_LCR_WLEN* defines */
#define UART_LCR_WLEN(x)	((x) - 5)

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
