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

#include <linux/types.h>

#include <linux/tty_flags.h>

#ifdef __KERNEL__
#include <asm/page.h>


/*
 * Counters of the input lines (CTS, DSR, RI, CD) interrupts
 */

struct async_icount {
	__u32	cts, dsr, rng, dcd, tx, rx;
	__u32	frame, parity, overrun, brk;
	__u32	buf_overrun;
};

/*
 * The size of the serial xmit buffer is 1 page, or 4096 bytes
 */
#define SERIAL_XMIT_SIZE PAGE_SIZE

#endif

struct serial_struct {
	int	type;
	int	line;
	unsigned int	port;
	int	irq;
	int	flags;
	int	xmit_fifo_size;
	int	custom_divisor;
	int	baud_base;
	unsigned short	close_delay;
	char	io_type;
	char	reserved_char[1];
	int	hub6;
	unsigned short	closing_wait; /* time to wait before closing */
	unsigned short	closing_wait2; /* no longer used... */
	unsigned char	*iomem_base;
	unsigned short	iomem_reg_shift;
	unsigned int	port_high;
	unsigned long	iomap_base;	/* cookie passed into ioremap */
};

/*
 * For the close wait times, 0 means wait forever for serial port to
 * flush its output.  65535 means don't wait at all.
 */
#define ASYNC_CLOSING_WAIT_INF	0
#define ASYNC_CLOSING_WAIT_NONE	65535

/*
 * These are the supported serial types.
 */
#define PORT_UNKNOWN	0
#define PORT_8250	1
#define PORT_16450	2
#define PORT_16550	3
#define PORT_16550A	4
#define PORT_CIRRUS     5	/* usurped by cyclades.c */
#define PORT_16650	6
#define PORT_16650V2	7
#define PORT_16750	8
#define PORT_STARTECH	9	/* usurped by cyclades.c */
#define PORT_16C950	10	/* Oxford Semiconductor */
#define PORT_16654	11
#define PORT_16850	12
#define PORT_RSA	13	/* RSA-DV II/S card */
#define PORT_MAX	13

#define SERIAL_IO_PORT	0
#define SERIAL_IO_HUB6	1
#define SERIAL_IO_MEM	2

struct serial_uart_config {
	char	*name;
	int	dfl_xmit_fifo_size;
	int	flags;
};

#define UART_CLEAR_FIFO		0x01
#define UART_USE_FIFO		0x02
#define UART_STARTECH		0x04
#define UART_NATSEMI		0x08


/*
 * Multiport serial configuration structure --- external structure
 */
struct serial_multiport_struct {
	int		irq;
	int		port1;
	unsigned char	mask1, match1;
	int		port2;
	unsigned char	mask2, match2;
	int		port3;
	unsigned char	mask3, match3;
	int		port4;
	unsigned char	mask4, match4;
	int		port_monitor;
	int	reserved[32];
};

/*
 * Serial input interrupt line counters -- external structure
 * Four lines can interrupt: CTS, DSR, RI, DCD
 */
struct serial_icounter_struct {
	int cts, dsr, rng, dcd;
	int rx, tx;
	int frame, overrun, parity, brk;
	int buf_overrun;
	int reserved[9];
};

/*
 * Serial interface for controlling RS485 settings on chips with suitable
 * support. Set with TIOCSRS485 and get with TIOCGRS485 if supported by your
 * platform. The set function returns the new state, with any unsupported bits
 * reverted appropriately.
 */

struct serial_rs485 {
	__u32	flags;			/* RS485 feature flags */
#define SER_RS485_ENABLED		(1 << 0)	/* If enabled */
#define SER_RS485_RTS_ON_SEND		(1 << 1)	/* Logical level for
							   RTS pin when
							   sending */
#define SER_RS485_RTS_AFTER_SEND	(1 << 2)	/* Logical level for
							   RTS pin after sent*/
#define SER_RS485_RX_DURING_TX		(1 << 4)
	__u32	delay_rts_before_send;	/* Delay before send (milliseconds) */
	__u32	delay_rts_after_send;	/* Delay after send (milliseconds) */
	__u32	padding[5];		/* Memory is cheap, new structs
					   are a royal PITA .. */
};

#ifdef __KERNEL__
#include <linux/compiler.h>

#endif /* __KERNEL__ */
#endif /* _LINUX_SERIAL_H */
