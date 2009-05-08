/*
 *  linux/drivers/char/serial_core.h
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef LINUX_SERIAL_CORE_H
#define LINUX_SERIAL_CORE_H

/*
 * The type definitions.  These are from Ted Ts'o's serial.h
 */
#define PORT_UNKNOWN	0
#define PORT_8250	1
#define PORT_16450	2
#define PORT_16550	3
#define PORT_16550A	4
#define PORT_CIRRUS	5
#define PORT_16650	6
#define PORT_16650V2	7
#define PORT_16750	8
#define PORT_STARTECH	9
#define PORT_16C950	10
#define PORT_16654	11
#define PORT_16850	12
#define PORT_RSA	13
#define PORT_NS16550A	14
#define PORT_XSCALE	15
#define PORT_RM9000	16	/* PMC-Sierra RM9xxx internal UART */
#define PORT_OCTEON	17	/* Cavium OCTEON internal UART */
#define PORT_MAX_8250	17	/* max port ID */

/*
 * ARM specific type numbers.  These are not currently guaranteed
 * to be implemented, and will change in the future.  These are
 * separate so any additions to the old serial.c that occur before
 * we are merged can be easily merged here.
 */
#define PORT_PXA	31
#define PORT_AMBA	32
#define PORT_CLPS711X	33
#define PORT_SA1100	34
#define PORT_UART00	35
#define PORT_21285	37

/* Sparc type numbers.  */
#define PORT_SUNZILOG	38
#define PORT_SUNSAB	39

/* DEC */
#define PORT_DZ		46
#define PORT_ZS		47

/* Parisc type numbers. */
#define PORT_MUX	48

/* Atmel AT91 / AT32 SoC */
#define PORT_ATMEL	49

/* Macintosh Zilog type numbers */
#define PORT_MAC_ZILOG	50	/* m68k : not yet implemented */
#define PORT_PMAC_ZILOG	51

/* SH-SCI */
#define PORT_SCI	52
#define PORT_SCIF	53
#define PORT_IRDA	54

/* Samsung S3C2410 SoC and derivatives thereof */
#define PORT_S3C2410    55

/* SGI IP22 aka Indy / Challenge S / Indigo 2 */
#define PORT_IP22ZILOG	56

/* Sharp LH7a40x -- an ARM9 SoC series */
#define PORT_LH7A40X	57

/* PPC CPM type number */
#define PORT_CPM        58

/* MPC52xx type numbers */
#define PORT_MPC52xx	59

/* IBM icom */
#define PORT_ICOM	60

/* Samsung S3C2440 SoC */
#define PORT_S3C2440	61

/* Motorola i.MX SoC */
#define PORT_IMX	62

/* Marvell MPSC */
#define PORT_MPSC	63

/* TXX9 type number */
#define PORT_TXX9	64

/* NEC VR4100 series SIU/DSIU */
#define PORT_VR41XX_SIU		65
#define PORT_VR41XX_DSIU	66

/* Samsung S3C2400 SoC */
#define PORT_S3C2400	67

/* M32R SIO */
#define PORT_M32R_SIO	68

/*Digi jsm */
#define PORT_JSM        69

#define PORT_PNX8XXX	70

/* Hilscher netx */
#define PORT_NETX	71

/* SUN4V Hypervisor Console */
#define PORT_SUNHV	72

#define PORT_S3C2412	73

/* Xilinx uartlite */
#define PORT_UARTLITE	74

/* Blackfin bf5xx */
#define PORT_BFIN	75

/* Micrel KS8695 */
#define PORT_KS8695	76

/* Broadcom SB1250, etc. SOC */
#define PORT_SB1250_DUART	77

/* Freescale ColdFire */
#define PORT_MCF	78

/* Blackfin SPORT */
#define PORT_BFIN_SPORT		79

/* MN10300 on-chip UART numbers */
#define PORT_MN10300		80
#define PORT_MN10300_CTS	81

#define PORT_SC26XX	82

/* SH-SCI */
#define PORT_SCIFA	83

#define PORT_S3C6400	84

/* NWPSERIAL */
#define PORT_NWPSERIAL	85

/* MAX3100 */
#define PORT_MAX3100    86

#ifdef __KERNEL__

#include <linux/compiler.h>
#include <linux/interrupt.h>
#include <linux/circ_buf.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/mutex.h>
#include <linux/sysrq.h>

struct uart_port;
struct uart_info;
struct serial_struct;
struct device;

/*
 * This structure describes all the operations that can be
 * done on the physical hardware.
 */
struct uart_ops {
	unsigned int	(*tx_empty)(struct uart_port *);
	void		(*set_mctrl)(struct uart_port *, unsigned int mctrl);
	unsigned int	(*get_mctrl)(struct uart_port *);
	void		(*stop_tx)(struct uart_port *);
	void		(*start_tx)(struct uart_port *);
	void		(*send_xchar)(struct uart_port *, char ch);
	void		(*stop_rx)(struct uart_port *);
	void		(*enable_ms)(struct uart_port *);
	void		(*break_ctl)(struct uart_port *, int ctl);
	int		(*startup)(struct uart_port *);
	void		(*shutdown)(struct uart_port *);
	void		(*flush_buffer)(struct uart_port *);
	void		(*set_termios)(struct uart_port *, struct ktermios *new,
				       struct ktermios *old);
	void		(*set_ldisc)(struct uart_port *);
	void		(*pm)(struct uart_port *, unsigned int state,
			      unsigned int oldstate);
	int		(*set_wake)(struct uart_port *, unsigned int state);

	/*
	 * Return a string describing the type of the port
	 */
	const char *(*type)(struct uart_port *);

	/*
	 * Release IO and memory resources used by the port.
	 * This includes iounmap if necessary.
	 */
	void		(*release_port)(struct uart_port *);

	/*
	 * Request IO and memory resources used by the port.
	 * This includes iomapping the port if necessary.
	 */
	int		(*request_port)(struct uart_port *);
	void		(*config_port)(struct uart_port *, int);
	int		(*verify_port)(struct uart_port *, struct serial_struct *);
	int		(*ioctl)(struct uart_port *, unsigned int, unsigned long);
#ifdef CONFIG_CONSOLE_POLL
	void	(*poll_put_char)(struct uart_port *, unsigned char);
	int		(*poll_get_char)(struct uart_port *);
#endif
};

#define UART_CONFIG_TYPE	(1 << 0)
#define UART_CONFIG_IRQ		(1 << 1)

struct uart_icount {
	__u32	cts;
	__u32	dsr;
	__u32	rng;
	__u32	dcd;
	__u32	rx;
	__u32	tx;
	__u32	frame;
	__u32	overrun;
	__u32	parity;
	__u32	brk;
	__u32	buf_overrun;
};

typedef unsigned int __bitwise__ upf_t;

struct uart_port {
	spinlock_t		lock;			/* port lock */
	unsigned long		iobase;			/* in/out[bwl] */
	unsigned char __iomem	*membase;		/* read/write[bwl] */
	unsigned int		(*serial_in)(struct uart_port *, int);
	void			(*serial_out)(struct uart_port *, int, int);
	unsigned int		irq;			/* irq number */
	unsigned int		uartclk;		/* base uart clock */
	unsigned int		fifosize;		/* tx fifo size */
	unsigned char		x_char;			/* xon/xoff char */
	unsigned char		regshift;		/* reg offset shift */
	unsigned char		iotype;			/* io access style */
	unsigned char		unused1;

#define UPIO_PORT		(0)
#define UPIO_HUB6		(1)
#define UPIO_MEM		(2)
#define UPIO_MEM32		(3)
#define UPIO_AU			(4)			/* Au1x00 type IO */
#define UPIO_TSI		(5)			/* Tsi108/109 type IO */
#define UPIO_DWAPB		(6)			/* DesignWare APB UART */
#define UPIO_RM9000		(7)			/* RM9000 type IO */

	unsigned int		read_status_mask;	/* driver specific */
	unsigned int		ignore_status_mask;	/* driver specific */
	struct uart_info	*info;			/* pointer to parent info */
	struct uart_icount	icount;			/* statistics */

	struct console		*cons;			/* struct console, if any */
#if defined(CONFIG_SERIAL_CORE_CONSOLE) || defined(SUPPORT_SYSRQ)
	unsigned long		sysrq;			/* sysrq timeout */
#endif

	upf_t			flags;

#define UPF_FOURPORT		((__force upf_t) (1 << 1))
#define UPF_SAK			((__force upf_t) (1 << 2))
#define UPF_SPD_MASK		((__force upf_t) (0x1030))
#define UPF_SPD_HI		((__force upf_t) (0x0010))
#define UPF_SPD_VHI		((__force upf_t) (0x0020))
#define UPF_SPD_CUST		((__force upf_t) (0x0030))
#define UPF_SPD_SHI		((__force upf_t) (0x1000))
#define UPF_SPD_WARP		((__force upf_t) (0x1010))
#define UPF_SKIP_TEST		((__force upf_t) (1 << 6))
#define UPF_AUTO_IRQ		((__force upf_t) (1 << 7))
#define UPF_HARDPPS_CD		((__force upf_t) (1 << 11))
#define UPF_LOW_LATENCY		((__force upf_t) (1 << 13))
#define UPF_BUGGY_UART		((__force upf_t) (1 << 14))
#define UPF_NO_TXEN_TEST	((__force upf_t) (1 << 15))
#define UPF_MAGIC_MULTIPLIER	((__force upf_t) (1 << 16))
#define UPF_CONS_FLOW		((__force upf_t) (1 << 23))
#define UPF_SHARE_IRQ		((__force upf_t) (1 << 24))
/* The exact UART type is known and should not be probed.  */
#define UPF_FIXED_TYPE		((__force upf_t) (1 << 27))
#define UPF_BOOT_AUTOCONF	((__force upf_t) (1 << 28))
#define UPF_FIXED_PORT		((__force upf_t) (1 << 29))
#define UPF_DEAD		((__force upf_t) (1 << 30))
#define UPF_IOREMAP		((__force upf_t) (1 << 31))

#define UPF_CHANGE_MASK		((__force upf_t) (0x17fff))
#define UPF_USR_MASK		((__force upf_t) (UPF_SPD_MASK|UPF_LOW_LATENCY))

	unsigned int		mctrl;			/* current modem ctrl settings */
	unsigned int		timeout;		/* character-based timeout */
	unsigned int		type;			/* port type */
	const struct uart_ops	*ops;
	unsigned int		custom_divisor;
	unsigned int		line;			/* port index */
	resource_size_t		mapbase;		/* for ioremap */
	struct device		*dev;			/* parent device */
	unsigned char		hub6;			/* this should be in the 8250 driver */
	unsigned char		suspended;
	unsigned char		unused[2];
	void			*private_data;		/* generic platform data pointer */
};

/*
 * This is the state information which is only valid when the port
 * is open; it may be cleared the core driver once the device has
 * been closed.  Either the low level driver or the core can modify
 * stuff here.
 */
typedef unsigned int __bitwise__ uif_t;

struct uart_info {
	struct tty_port		port;
	struct circ_buf		xmit;
	uif_t			flags;

/*
 * Definitions for info->flags.  These are _private_ to serial_core, and
 * are specific to this structure.  They may be queried by low level drivers.
 *
 * FIXME: use the ASY_ definitions
 */
#define UIF_CHECK_CD		((__force uif_t) (1 << 25))
#define UIF_CTS_FLOW		((__force uif_t) (1 << 26))
#define UIF_NORMAL_ACTIVE	((__force uif_t) (1 << 29))
#define UIF_INITIALIZED		((__force uif_t) (1 << 31))
#define UIF_SUSPENDED		((__force uif_t) (1 << 30))

	struct tasklet_struct	tlet;
	wait_queue_head_t	delta_msr_wait;
};

/*
 * This is the state information which is persistent across opens.
 * The low level driver must not to touch any elements contained
 * within.
 */
struct uart_state {
	unsigned int		close_delay;		/* msec */
	unsigned int		closing_wait;		/* msec */

#define USF_CLOSING_WAIT_INF	(0)
#define USF_CLOSING_WAIT_NONE	(~0U)

	int			count;
	int			pm_state;
	struct uart_info	info;
	struct uart_port	*port;

	struct mutex		mutex;
};

#define UART_XMIT_SIZE	PAGE_SIZE


/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS		256

struct module;
struct tty_driver;

struct uart_driver {
	struct module		*owner;
	const char		*driver_name;
	const char		*dev_name;
	int			 major;
	int			 minor;
	int			 nr;
	struct console		*cons;

	/*
	 * these are private; the low level driver should not
	 * touch these; they should be initialised to NULL
	 */
	struct uart_state	*state;
	struct tty_driver	*tty_driver;
};

void uart_write_wakeup(struct uart_port *port);

/*
 * Baud rate helpers.
 */
void uart_update_timeout(struct uart_port *port, unsigned int cflag,
			 unsigned int baud);
unsigned int uart_get_baud_rate(struct uart_port *port, struct ktermios *termios,
				struct ktermios *old, unsigned int min,
				unsigned int max);
unsigned int uart_get_divisor(struct uart_port *port, unsigned int baud);

/*
 * Console helpers.
 */
struct uart_port *uart_get_console(struct uart_port *ports, int nr,
				   struct console *c);
void uart_parse_options(char *options, int *baud, int *parity, int *bits,
			int *flow);
int uart_set_options(struct uart_port *port, struct console *co, int baud,
		     int parity, int bits, int flow);
struct tty_driver *uart_console_device(struct console *co, int *index);
void uart_console_write(struct uart_port *port, const char *s,
			unsigned int count,
			void (*putchar)(struct uart_port *, int));

/*
 * Port/driver registration/removal
 */
int uart_register_driver(struct uart_driver *uart);
void uart_unregister_driver(struct uart_driver *uart);
int uart_add_one_port(struct uart_driver *reg, struct uart_port *port);
int uart_remove_one_port(struct uart_driver *reg, struct uart_port *port);
int uart_match_port(struct uart_port *port1, struct uart_port *port2);

/*
 * Power Management
 */
int uart_suspend_port(struct uart_driver *reg, struct uart_port *port);
int uart_resume_port(struct uart_driver *reg, struct uart_port *port);

#define uart_circ_empty(circ)		((circ)->head == (circ)->tail)
#define uart_circ_clear(circ)		((circ)->head = (circ)->tail = 0)

#define uart_circ_chars_pending(circ)	\
	(CIRC_CNT((circ)->head, (circ)->tail, UART_XMIT_SIZE))

#define uart_circ_chars_free(circ)	\
	(CIRC_SPACE((circ)->head, (circ)->tail, UART_XMIT_SIZE))

static inline int uart_tx_stopped(struct uart_port *port)
{
	struct tty_struct *tty = port->info->port.tty;
	if(tty->stopped || tty->hw_stopped)
		return 1;
	return 0;
}

/*
 * The following are helper functions for the low level drivers.
 */
static inline int
uart_handle_sysrq_char(struct uart_port *port, unsigned int ch)
{
#ifdef SUPPORT_SYSRQ
	if (port->sysrq) {
		if (ch && time_before(jiffies, port->sysrq)) {
			handle_sysrq(ch, port->info->port.tty);
			port->sysrq = 0;
			return 1;
		}
		port->sysrq = 0;
	}
#endif
	return 0;
}
#ifndef SUPPORT_SYSRQ
#define uart_handle_sysrq_char(port,ch) uart_handle_sysrq_char(port, 0)
#endif

/*
 * We do the SysRQ and SAK checking like this...
 */
static inline int uart_handle_break(struct uart_port *port)
{
	struct uart_info *info = port->info;
#ifdef SUPPORT_SYSRQ
	if (port->cons && port->cons->index == port->line) {
		if (!port->sysrq) {
			port->sysrq = jiffies + HZ*5;
			return 1;
		}
		port->sysrq = 0;
	}
#endif
	if (port->flags & UPF_SAK)
		do_SAK(info->port.tty);
	return 0;
}

/**
 *	uart_handle_dcd_change - handle a change of carrier detect state
 *	@port: uart_port structure for the open port
 *	@status: new carrier detect status, nonzero if active
 */
static inline void
uart_handle_dcd_change(struct uart_port *port, unsigned int status)
{
	struct uart_info *info = port->info;

	port->icount.dcd++;

#ifdef CONFIG_HARD_PPS
	if ((port->flags & UPF_HARDPPS_CD) && status)
		hardpps();
#endif

	if (info->flags & UIF_CHECK_CD) {
		if (status)
			wake_up_interruptible(&info->port.open_wait);
		else if (info->port.tty)
			tty_hangup(info->port.tty);
	}
}

/**
 *	uart_handle_cts_change - handle a change of clear-to-send state
 *	@port: uart_port structure for the open port
 *	@status: new clear to send status, nonzero if active
 */
static inline void
uart_handle_cts_change(struct uart_port *port, unsigned int status)
{
	struct uart_info *info = port->info;
	struct tty_struct *tty = info->port.tty;

	port->icount.cts++;

	if (info->flags & UIF_CTS_FLOW) {
		if (tty->hw_stopped) {
			if (status) {
				tty->hw_stopped = 0;
				port->ops->start_tx(port);
				uart_write_wakeup(port);
			}
		} else {
			if (!status) {
				tty->hw_stopped = 1;
				port->ops->stop_tx(port);
			}
		}
	}
}

#include <linux/tty_flip.h>

static inline void
uart_insert_char(struct uart_port *port, unsigned int status,
		 unsigned int overrun, unsigned int ch, unsigned int flag)
{
	struct tty_struct *tty = port->info->port.tty;

	if ((status & port->ignore_status_mask & ~overrun) == 0)
		tty_insert_flip_char(tty, ch, flag);

	/*
	 * Overrun is special.  Since it's reported immediately,
	 * it doesn't affect the current character.
	 */
	if (status & ~port->ignore_status_mask & overrun)
		tty_insert_flip_char(tty, 0, TTY_OVERRUN);
}

/*
 *	UART_ENABLE_MS - determine if port should enable modem status irqs
 */
#define UART_ENABLE_MS(port,cflag)	((port)->flags & UPF_HARDPPS_CD || \
					 (cflag) & CRTSCTS || \
					 !((cflag) & CLOCAL))

#endif

#endif /* LINUX_SERIAL_CORE_H */
