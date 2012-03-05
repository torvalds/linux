/*
 * Private header file for the (dumb) serial driver
 *
 * Copyright (C) 1997 by Theodore Ts'o.
 * 
 * Redistribution of this file is permitted under the terms of the GNU 
 * Public License (GPL)
 */

#ifndef _LINUX_SERIALP_H
#define _LINUX_SERIALP_H

/*
 * This is our internal structure for each serial port's state.
 * 
 * Many fields are paralleled by the structure used by the serial_struct
 * structure.
 *
 * For definitions of the flags field, see tty.h
 */

#include <linux/termios.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/circ_buf.h>
#include <linux/tty.h>
#include <linux/wait.h>

struct serial_state {
	int	baud_base;
	unsigned long	port;
	int	irq;
	int	flags;
	int	type;
	int	line;
	int	xmit_fifo_size;
	int	custom_divisor;
	struct async_icount	icount;	
	struct tty_port tport;

	/* amiserial */
	int			read_status_mask;
	int			ignore_status_mask;
	int			timeout;
	int			quot;
	int			IER; 	/* Interrupt Enable Register */
	int			MCR; 	/* Modem control register */
	/* simserial */
	int			x_char;	/* xon/xoff character */
 	struct circ_buf		xmit;
	/* /simserial */
	/* /amiserial */
};

#endif /* _LINUX_SERIAL_H */
