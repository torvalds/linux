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
 * Definitions for async_struct (and serial_struct) flags field
 *
 * Define ASYNCB_* for convenient use with {test,set,clear}_bit.
 */
#define ASYNCB_HUP_NOTIFY	 0 /* Notify getty on hangups and closes
				    * on the callout port */
#define ASYNCB_FOURPORT		 1 /* Set OU1, OUT2 per AST Fourport settings */
#define ASYNCB_SAK		 2 /* Secure Attention Key (Orange book) */
#define ASYNCB_SPLIT_TERMIOS	 3 /* Separate termios for dialin/callout */
#define ASYNCB_SPD_HI		 4 /* Use 56000 instead of 38400 bps */
#define ASYNCB_SPD_VHI		 5 /* Use 115200 instead of 38400 bps */
#define ASYNCB_SKIP_TEST	 6 /* Skip UART test during autoconfiguration */
#define ASYNCB_AUTO_IRQ		 7 /* Do automatic IRQ during
				    * autoconfiguration */
#define ASYNCB_SESSION_LOCKOUT	 8 /* Lock out cua opens based on session */
#define ASYNCB_PGRP_LOCKOUT	 9 /* Lock out cua opens based on pgrp */
#define ASYNCB_CALLOUT_NOHUP	10 /* Don't do hangups for cua device */
#define ASYNCB_HARDPPS_CD	11 /* Call hardpps when CD goes high  */
#define ASYNCB_SPD_SHI		12 /* Use 230400 instead of 38400 bps */
#define ASYNCB_LOW_LATENCY	13 /* Request low latency behaviour */
#define ASYNCB_BUGGY_UART	14 /* This is a buggy UART, skip some safety
				    * checks.  Note: can be dangerous! */
#define ASYNCB_AUTOPROBE	15 /* Port was autoprobed by PCI or PNP code */
#define ASYNCB_LAST_USER	15

/* Internal flags used only by kernel */
#define ASYNCB_INITIALIZED	31 /* Serial port was initialized */
#define ASYNCB_SUSPENDED	30 /* Serial port is suspended */
#define ASYNCB_NORMAL_ACTIVE	29 /* Normal device is active */
#define ASYNCB_BOOT_AUTOCONF	28 /* Autoconfigure port on bootup */
#define ASYNCB_CLOSING		27 /* Serial port is closing */
#define ASYNCB_CTS_FLOW		26 /* Do CTS flow control */
#define ASYNCB_CHECK_CD		25 /* i.e., CLOCAL */
#define ASYNCB_SHARE_IRQ	24 /* for multifunction cards, no longer used */
#define ASYNCB_CONS_FLOW	23 /* flow control for console  */
#define ASYNCB_BOOT_ONLYMCA	22 /* Probe only if MCA bus */
#define ASYNCB_FIRST_KERNEL	22

#define ASYNC_HUP_NOTIFY	(1U << ASYNCB_HUP_NOTIFY)
#define ASYNC_SUSPENDED		(1U << ASYNCB_SUSPENDED)
#define ASYNC_FOURPORT		(1U << ASYNCB_FOURPORT)
#define ASYNC_SAK		(1U << ASYNCB_SAK)
#define ASYNC_SPLIT_TERMIOS	(1U << ASYNCB_SPLIT_TERMIOS)
#define ASYNC_SPD_HI		(1U << ASYNCB_SPD_HI)
#define ASYNC_SPD_VHI		(1U << ASYNCB_SPD_VHI)
#define ASYNC_SKIP_TEST		(1U << ASYNCB_SKIP_TEST)
#define ASYNC_AUTO_IRQ		(1U << ASYNCB_AUTO_IRQ)
#define ASYNC_SESSION_LOCKOUT	(1U << ASYNCB_SESSION_LOCKOUT)
#define ASYNC_PGRP_LOCKOUT	(1U << ASYNCB_PGRP_LOCKOUT)
#define ASYNC_CALLOUT_NOHUP	(1U << ASYNCB_CALLOUT_NOHUP)
#define ASYNC_HARDPPS_CD	(1U << ASYNCB_HARDPPS_CD)
#define ASYNC_SPD_SHI		(1U << ASYNCB_SPD_SHI)
#define ASYNC_LOW_LATENCY	(1U << ASYNCB_LOW_LATENCY)
#define ASYNC_BUGGY_UART	(1U << ASYNCB_BUGGY_UART)
#define ASYNC_AUTOPROBE		(1U << ASYNCB_AUTOPROBE)

#define ASYNC_FLAGS		((1U << (ASYNCB_LAST_USER + 1)) - 1)
#define ASYNC_USR_MASK		(ASYNC_SPD_MASK|ASYNC_CALLOUT_NOHUP| \
		ASYNC_LOW_LATENCY)
#define ASYNC_SPD_CUST		(ASYNC_SPD_HI|ASYNC_SPD_VHI)
#define ASYNC_SPD_WARP		(ASYNC_SPD_HI|ASYNC_SPD_SHI)
#define ASYNC_SPD_MASK		(ASYNC_SPD_HI|ASYNC_SPD_VHI|ASYNC_SPD_SHI)

#define ASYNC_INITIALIZED	(1U << ASYNCB_INITIALIZED)
#define ASYNC_NORMAL_ACTIVE	(1U << ASYNCB_NORMAL_ACTIVE)
#define ASYNC_BOOT_AUTOCONF	(1U << ASYNCB_BOOT_AUTOCONF)
#define ASYNC_CLOSING		(1U << ASYNCB_CLOSING)
#define ASYNC_CTS_FLOW		(1U << ASYNCB_CTS_FLOW)
#define ASYNC_CHECK_CD		(1U << ASYNCB_CHECK_CD)
#define ASYNC_SHARE_IRQ		(1U << ASYNCB_SHARE_IRQ)
#define ASYNC_CONS_FLOW		(1U << ASYNCB_CONS_FLOW)
#define ASYNC_BOOT_ONLYMCA	(1U << ASYNCB_BOOT_ONLYMCA)
#define ASYNC_INTERNAL_FLAGS	(~((1U << ASYNCB_FIRST_KERNEL) - 1))

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
