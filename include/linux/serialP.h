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
	int	count;
	unsigned short	close_delay;
	unsigned short	closing_wait; /* time to wait before closing */
	struct async_icount	icount;	
	struct async_struct *info;
};

struct async_struct {
	unsigned long		port;
	int			xmit_fifo_size;
	struct serial_state	*state;
	struct tty_struct 	*tty;
	int			read_status_mask;
	int			ignore_status_mask;
	int			timeout;
	int			quot;
	int			x_char;	/* xon/xoff character */
	int			close_delay;
	unsigned short		closing_wait;
	int			IER; 	/* Interrupt Enable Register */
	int			MCR; 	/* Modem control register */
	int			line;
	int			blocked_open; /* # of blocked opens */
 	struct circ_buf		xmit;
	wait_queue_head_t	open_wait;
	wait_queue_head_t	close_wait;
	wait_queue_head_t	delta_msr_wait;
	struct async_struct	*next_port; /* For the linked list */
	struct async_struct	*prev_port;
};

#endif /* _LINUX_SERIAL_H */
