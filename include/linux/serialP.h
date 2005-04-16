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

#include <linux/version.h>
#include <linux/config.h>
#include <linux/termios.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/circ_buf.h>
#include <linux/wait.h>

struct serial_state {
	int	magic;
	int	baud_base;
	unsigned long	port;
	int	irq;
	int	flags;
	int	hub6;
	int	type;
	int	line;
	int	revision;	/* Chip revision (950) */
	int	xmit_fifo_size;
	int	custom_divisor;
	int	count;
	u8	*iomem_base;
	u16	iomem_reg_shift;
	unsigned short	close_delay;
	unsigned short	closing_wait; /* time to wait before closing */
	struct async_icount	icount;	
	int	io_type;
	struct async_struct *info;
	struct pci_dev	*dev;
};

struct async_struct {
	int			magic;
	unsigned long		port;
	int			hub6;
	int			flags;
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
	unsigned short		closing_wait2; /* obsolete */
	int			IER; 	/* Interrupt Enable Register */
	int			MCR; 	/* Modem control register */
	int			LCR; 	/* Line control register */
	int			ACR;	 /* 16950 Additional Control Reg. */
	unsigned long		event;
	unsigned long		last_active;
	int			line;
	int			blocked_open; /* # of blocked opens */
 	struct circ_buf		xmit;
 	spinlock_t		xmit_lock;
	u8			*iomem_base;
	u16			iomem_reg_shift;
	int			io_type;
	struct work_struct			work;
	struct tasklet_struct	tlet;
#ifdef DECLARE_WAITQUEUE
	wait_queue_head_t	open_wait;
	wait_queue_head_t	close_wait;
	wait_queue_head_t	delta_msr_wait;
#else	
	struct wait_queue	*open_wait;
	struct wait_queue	*close_wait;
	struct wait_queue	*delta_msr_wait;
#endif	
	struct async_struct	*next_port; /* For the linked list */
	struct async_struct	*prev_port;
};

#define CONFIGURED_SERIAL_PORT(info) ((info)->port || ((info)->iomem_base))

#define SERIAL_MAGIC 0x5301
#define SSTATE_MAGIC 0x5302

/*
 * Events are used to schedule things to happen at timer-interrupt
 * time, instead of at rs interrupt time.
 */
#define RS_EVENT_WRITE_WAKEUP	0

/*
 * Multiport serial configuration structure --- internal structure
 */
struct rs_multiport_struct {
	int		port1;
	unsigned char	mask1, match1;
	int		port2;
	unsigned char	mask2, match2;
	int		port3;
	unsigned char	mask3, match3;
	int		port4;
	unsigned char	mask4, match4;
	int		port_monitor;
};

#if defined(__alpha__) && !defined(CONFIG_PCI)
/*
 * Digital did something really horribly wrong with the OUT1 and OUT2
 * lines on at least some ALPHA's.  The failure mode is that if either
 * is cleared, the machine locks up with endless interrupts.
 *
 * This is still used by arch/mips/au1000/common/serial.c for some weird
 * reason (mips != alpha!)
 */
#define ALPHA_KLUDGE_MCR  (UART_MCR_OUT2 | UART_MCR_OUT1)
#elif defined(CONFIG_SBC8560)
/*
 * WindRiver did something similarly broken on their SBC8560 board. The
 * UART tristates its IRQ output while OUT2 is clear, but they pulled
 * the interrupt line _up_ instead of down, so if we register the IRQ
 * while the UART is in that state, we die in an IRQ storm. */
#define ALPHA_KLUDGE_MCR (UART_MCR_OUT2)
#else
#define ALPHA_KLUDGE_MCR 0
#endif

/*
 * Definitions for PCI support.
 */
#define SPCI_FL_BASE_MASK	0x0007
#define SPCI_FL_BASE0	0x0000
#define SPCI_FL_BASE1	0x0001
#define SPCI_FL_BASE2	0x0002
#define SPCI_FL_BASE3	0x0003
#define SPCI_FL_BASE4	0x0004
#define SPCI_FL_GET_BASE(x)	(x & SPCI_FL_BASE_MASK)

#define SPCI_FL_IRQ_MASK       (0x0007 << 4)
#define SPCI_FL_IRQBASE0       (0x0000 << 4)
#define SPCI_FL_IRQBASE1       (0x0001 << 4)
#define SPCI_FL_IRQBASE2       (0x0002 << 4)
#define SPCI_FL_IRQBASE3       (0x0003 << 4)
#define SPCI_FL_IRQBASE4       (0x0004 << 4)
#define SPCI_FL_GET_IRQBASE(x)        ((x & SPCI_FL_IRQ_MASK) >> 4)

/* Use successive BARs (PCI base address registers), 
   else use offset into some specified BAR */
#define SPCI_FL_BASE_TABLE	0x0100

/* Use successive entries in the irq resource table */
#define SPCI_FL_IRQ_TABLE	0x0200

/* Use the irq resource table instead of dev->irq */
#define SPCI_FL_IRQRESOURCE	0x0400

/* Use the Base address register size to cap number of ports */
#define SPCI_FL_REGION_SZ_CAP	0x0800

/* Do not use irq sharing for this device */
#define SPCI_FL_NO_SHIRQ	0x1000

/* This is a PNP device */
#define SPCI_FL_ISPNP		0x2000

#define SPCI_FL_PNPDEFAULT	(SPCI_FL_IRQRESOURCE|SPCI_FL_ISPNP)

#endif /* _LINUX_SERIAL_H */
