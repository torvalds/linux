/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  linux/drivers/char/serial_core.h
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 */
#ifndef LINUX_SERIAL_CORE_H
#define LINUX_SERIAL_CORE_H

#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/circ_buf.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/mutex.h>
#include <linux/sysrq.h>
#include <uapi/linux/serial_core.h>

#ifdef CONFIG_SERIAL_CORE_CONSOLE
#define uart_console(port) \
	((port)->cons && (port)->cons->index == (port)->line)
#else
#define uart_console(port)      ({ (void)port; 0; })
#endif

struct uart_port;
struct serial_struct;
struct serial_port_device;
struct device;
struct gpio_desc;

/**
 * struct uart_ops -- interface between serial_core and the driver
 *
 * This structure describes all the operations that can be done on the
 * physical hardware.
 *
 * @tx_empty: ``unsigned int ()(struct uart_port *port)``
 *
 *	This function tests whether the transmitter fifo and shifter for the
 *	@port is empty. If it is empty, this function should return
 *	%TIOCSER_TEMT, otherwise return 0. If the port does not support this
 *	operation, then it should return %TIOCSER_TEMT.
 *
 *	Locking: none.
 *	Interrupts: caller dependent.
 *	This call must not sleep
 *
 * @set_mctrl: ``void ()(struct uart_port *port, unsigned int mctrl)``
 *
 *	This function sets the modem control lines for @port to the state
 *	described by @mctrl. The relevant bits of @mctrl are:
 *
 *		- %TIOCM_RTS	RTS signal.
 *		- %TIOCM_DTR	DTR signal.
 *		- %TIOCM_OUT1	OUT1 signal.
 *		- %TIOCM_OUT2	OUT2 signal.
 *		- %TIOCM_LOOP	Set the port into loopback mode.
 *
 *	If the appropriate bit is set, the signal should be driven
 *	active.  If the bit is clear, the signal should be driven
 *	inactive.
 *
 *	Locking: @port->lock taken.
 *	Interrupts: locally disabled.
 *	This call must not sleep
 *
 * @get_mctrl: ``unsigned int ()(struct uart_port *port)``
 *
 *	Returns the current state of modem control inputs of @port. The state
 *	of the outputs should not be returned, since the core keeps track of
 *	their state. The state information should include:
 *
 *		- %TIOCM_CAR	state of DCD signal
 *		- %TIOCM_CTS	state of CTS signal
 *		- %TIOCM_DSR	state of DSR signal
 *		- %TIOCM_RI	state of RI signal
 *
 *	The bit is set if the signal is currently driven active.  If
 *	the port does not support CTS, DCD or DSR, the driver should
 *	indicate that the signal is permanently active. If RI is
 *	not available, the signal should not be indicated as active.
 *
 *	Locking: @port->lock taken.
 *	Interrupts: locally disabled.
 *	This call must not sleep
 *
 * @stop_tx: ``void ()(struct uart_port *port)``
 *
 *	Stop transmitting characters. This might be due to the CTS line
 *	becoming inactive or the tty layer indicating we want to stop
 *	transmission due to an %XOFF character.
 *
 *	The driver should stop transmitting characters as soon as possible.
 *
 *	Locking: @port->lock taken.
 *	Interrupts: locally disabled.
 *	This call must not sleep
 *
 * @start_tx: ``void ()(struct uart_port *port)``
 *
 *	Start transmitting characters.
 *
 *	Locking: @port->lock taken.
 *	Interrupts: locally disabled.
 *	This call must not sleep
 *
 * @throttle: ``void ()(struct uart_port *port)``
 *
 *	Notify the serial driver that input buffers for the line discipline are
 *	close to full, and it should somehow signal that no more characters
 *	should be sent to the serial port.
 *	This will be called only if hardware assisted flow control is enabled.
 *
 *	Locking: serialized with @unthrottle() and termios modification by the
 *	tty layer.
 *
 * @unthrottle: ``void ()(struct uart_port *port)``
 *
 *	Notify the serial driver that characters can now be sent to the serial
 *	port without fear of overrunning the input buffers of the line
 *	disciplines.
 *
 *	This will be called only if hardware assisted flow control is enabled.
 *
 *	Locking: serialized with @throttle() and termios modification by the
 *	tty layer.
 *
 * @send_xchar: ``void ()(struct uart_port *port, char ch)``
 *
 *	Transmit a high priority character, even if the port is stopped. This
 *	is used to implement XON/XOFF flow control and tcflow(). If the serial
 *	driver does not implement this function, the tty core will append the
 *	character to the circular buffer and then call start_tx() / stop_tx()
 *	to flush the data out.
 *
 *	Do not transmit if @ch == '\0' (%__DISABLED_CHAR).
 *
 *	Locking: none.
 *	Interrupts: caller dependent.
 *
 * @start_rx: ``void ()(struct uart_port *port)``
 *
 *	Start receiving characters.
 *
 *	Locking: @port->lock taken.
 *	Interrupts: locally disabled.
 *	This call must not sleep
 *
 * @stop_rx: ``void ()(struct uart_port *port)``
 *
 *	Stop receiving characters; the @port is in the process of being closed.
 *
 *	Locking: @port->lock taken.
 *	Interrupts: locally disabled.
 *	This call must not sleep
 *
 * @enable_ms: ``void ()(struct uart_port *port)``
 *
 *	Enable the modem status interrupts.
 *
 *	This method may be called multiple times. Modem status interrupts
 *	should be disabled when the @shutdown() method is called.
 *
 *	Locking: @port->lock taken.
 *	Interrupts: locally disabled.
 *	This call must not sleep
 *
 * @break_ctl: ``void ()(struct uart_port *port, int ctl)``
 *
 *	Control the transmission of a break signal. If @ctl is nonzero, the
 *	break signal should be transmitted. The signal should be terminated
 *	when another call is made with a zero @ctl.
 *
 *	Locking: caller holds tty_port->mutex
 *
 * @startup: ``int ()(struct uart_port *port)``
 *
 *	Grab any interrupt resources and initialise any low level driver state.
 *	Enable the port for reception. It should not activate RTS nor DTR;
 *	this will be done via a separate call to @set_mctrl().
 *
 *	This method will only be called when the port is initially opened.
 *
 *	Locking: port_sem taken.
 *	Interrupts: globally disabled.
 *
 * @shutdown: ``void ()(struct uart_port *port)``
 *
 *	Disable the @port, disable any break condition that may be in effect,
 *	and free any interrupt resources. It should not disable RTS nor DTR;
 *	this will have already been done via a separate call to @set_mctrl().
 *
 *	Drivers must not access @port->state once this call has completed.
 *
 *	This method will only be called when there are no more users of this
 *	@port.
 *
 *	Locking: port_sem taken.
 *	Interrupts: caller dependent.
 *
 * @flush_buffer: ``void ()(struct uart_port *port)``
 *
 *	Flush any write buffers, reset any DMA state and stop any ongoing DMA
 *	transfers.
 *
 *	This will be called whenever the @port->state->xmit circular buffer is
 *	cleared.
 *
 *	Locking: @port->lock taken.
 *	Interrupts: locally disabled.
 *	This call must not sleep
 *
 * @set_termios: ``void ()(struct uart_port *port, struct ktermios *new,
 *			struct ktermios *old)``
 *
 *	Change the @port parameters, including word length, parity, stop bits.
 *	Update @port->read_status_mask and @port->ignore_status_mask to
 *	indicate the types of events we are interested in receiving. Relevant
 *	ktermios::c_cflag bits are:
 *
 *	- %CSIZE - word size
 *	- %CSTOPB - 2 stop bits
 *	- %PARENB - parity enable
 *	- %PARODD - odd parity (when %PARENB is in force)
 *	- %ADDRB - address bit (changed through uart_port::rs485_config()).
 *	- %CREAD - enable reception of characters (if not set, still receive
 *	  characters from the port, but throw them away).
 *	- %CRTSCTS - if set, enable CTS status change reporting.
 *	- %CLOCAL - if not set, enable modem status change reporting.
 *
 *	Relevant ktermios::c_iflag bits are:
 *
 *	- %INPCK - enable frame and parity error events to be passed to the TTY
 *	  layer.
 *	- %BRKINT / %PARMRK - both of these enable break events to be passed to
 *	  the TTY layer.
 *	- %IGNPAR - ignore parity and framing errors.
 *	- %IGNBRK - ignore break errors. If %IGNPAR is also set, ignore overrun
 *	  errors as well.
 *
 *	The interaction of the ktermios::c_iflag bits is as follows (parity
 *	error given as an example):
 *
 *	============ ======= ======= =========================================
 *	Parity error INPCK   IGNPAR
 *	============ ======= ======= =========================================
 *	n/a	     0	     n/a     character received, marked as %TTY_NORMAL
 *	None	     1	     n/a     character received, marked as %TTY_NORMAL
 *	Yes	     1	     0	     character received, marked as %TTY_PARITY
 *	Yes	     1	     1	     character discarded
 *	============ ======= ======= =========================================
 *
 *	Other flags may be used (eg, xon/xoff characters) if your hardware
 *	supports hardware "soft" flow control.
 *
 *	Locking: caller holds tty_port->mutex
 *	Interrupts: caller dependent.
 *	This call must not sleep
 *
 * @set_ldisc: ``void ()(struct uart_port *port, struct ktermios *termios)``
 *
 *	Notifier for discipline change. See
 *	Documentation/driver-api/tty/tty_ldisc.rst.
 *
 *	Locking: caller holds tty_port->mutex
 *
 * @pm: ``void ()(struct uart_port *port, unsigned int state,
 *		 unsigned int oldstate)``
 *
 *	Perform any power management related activities on the specified @port.
 *	@state indicates the new state (defined by enum uart_pm_state),
 *	@oldstate indicates the previous state.
 *
 *	This function should not be used to grab any resources.
 *
 *	This will be called when the @port is initially opened and finally
 *	closed, except when the @port is also the system console. This will
 *	occur even if %CONFIG_PM is not set.
 *
 *	Locking: none.
 *	Interrupts: caller dependent.
 *
 * @type: ``const char *()(struct uart_port *port)``
 *
 *	Return a pointer to a string constant describing the specified @port,
 *	or return %NULL, in which case the string 'unknown' is substituted.
 *
 *	Locking: none.
 *	Interrupts: caller dependent.
 *
 * @release_port: ``void ()(struct uart_port *port)``
 *
 *	Release any memory and IO region resources currently in use by the
 *	@port.
 *
 *	Locking: none.
 *	Interrupts: caller dependent.
 *
 * @request_port: ``int ()(struct uart_port *port)``
 *
 *	Request any memory and IO region resources required by the port. If any
 *	fail, no resources should be registered when this function returns, and
 *	it should return -%EBUSY on failure.
 *
 *	Locking: none.
 *	Interrupts: caller dependent.
 *
 * @config_port: ``void ()(struct uart_port *port, int type)``
 *
 *	Perform any autoconfiguration steps required for the @port. @type
 *	contains a bit mask of the required configuration. %UART_CONFIG_TYPE
 *	indicates that the port requires detection and identification.
 *	@port->type should be set to the type found, or %PORT_UNKNOWN if no
 *	port was detected.
 *
 *	%UART_CONFIG_IRQ indicates autoconfiguration of the interrupt signal,
 *	which should be probed using standard kernel autoprobing techniques.
 *	This is not necessary on platforms where ports have interrupts
 *	internally hard wired (eg, system on a chip implementations).
 *
 *	Locking: none.
 *	Interrupts: caller dependent.
 *
 * @verify_port: ``int ()(struct uart_port *port,
 *			struct serial_struct *serinfo)``
 *
 *	Verify the new serial port information contained within @serinfo is
 *	suitable for this port type.
 *
 *	Locking: none.
 *	Interrupts: caller dependent.
 *
 * @ioctl: ``int ()(struct uart_port *port, unsigned int cmd,
 *		unsigned long arg)``
 *
 *	Perform any port specific IOCTLs. IOCTL commands must be defined using
 *	the standard numbering system found in <asm/ioctl.h>.
 *
 *	Locking: none.
 *	Interrupts: caller dependent.
 *
 * @poll_init: ``int ()(struct uart_port *port)``
 *
 *	Called by kgdb to perform the minimal hardware initialization needed to
 *	support @poll_put_char() and @poll_get_char(). Unlike @startup(), this
 *	should not request interrupts.
 *
 *	Locking: %tty_mutex and tty_port->mutex taken.
 *	Interrupts: n/a.
 *
 * @poll_put_char: ``void ()(struct uart_port *port, unsigned char ch)``
 *
 *	Called by kgdb to write a single character @ch directly to the serial
 *	@port. It can and should block until there is space in the TX FIFO.
 *
 *	Locking: none.
 *	Interrupts: caller dependent.
 *	This call must not sleep
 *
 * @poll_get_char: ``int ()(struct uart_port *port)``
 *
 *	Called by kgdb to read a single character directly from the serial
 *	port. If data is available, it should be returned; otherwise the
 *	function should return %NO_POLL_CHAR immediately.
 *
 *	Locking: none.
 *	Interrupts: caller dependent.
 *	This call must not sleep
 */
struct uart_ops {
	unsigned int	(*tx_empty)(struct uart_port *);
	void		(*set_mctrl)(struct uart_port *, unsigned int mctrl);
	unsigned int	(*get_mctrl)(struct uart_port *);
	void		(*stop_tx)(struct uart_port *);
	void		(*start_tx)(struct uart_port *);
	void		(*throttle)(struct uart_port *);
	void		(*unthrottle)(struct uart_port *);
	void		(*send_xchar)(struct uart_port *, char ch);
	void		(*stop_rx)(struct uart_port *);
	void		(*start_rx)(struct uart_port *);
	void		(*enable_ms)(struct uart_port *);
	void		(*break_ctl)(struct uart_port *, int ctl);
	int		(*startup)(struct uart_port *);
	void		(*shutdown)(struct uart_port *);
	void		(*flush_buffer)(struct uart_port *);
	void		(*set_termios)(struct uart_port *, struct ktermios *new,
				       const struct ktermios *old);
	void		(*set_ldisc)(struct uart_port *, struct ktermios *);
	void		(*pm)(struct uart_port *, unsigned int state,
			      unsigned int oldstate);
	const char	*(*type)(struct uart_port *);
	void		(*release_port)(struct uart_port *);
	int		(*request_port)(struct uart_port *);
	void		(*config_port)(struct uart_port *, int);
	int		(*verify_port)(struct uart_port *, struct serial_struct *);
	int		(*ioctl)(struct uart_port *, unsigned int, unsigned long);
#ifdef CONFIG_CONSOLE_POLL
	int		(*poll_init)(struct uart_port *);
	void		(*poll_put_char)(struct uart_port *, unsigned char);
	int		(*poll_get_char)(struct uart_port *);
#endif
};

#define NO_POLL_CHAR		0x00ff0000
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

typedef u64 __bitwise upf_t;
typedef unsigned int __bitwise upstat_t;

struct uart_port {
	spinlock_t		lock;			/* port lock */
	unsigned long		iobase;			/* in/out[bwl] */
	unsigned char __iomem	*membase;		/* read/write[bwl] */
	unsigned int		(*serial_in)(struct uart_port *, int);
	void			(*serial_out)(struct uart_port *, int, int);
	void			(*set_termios)(struct uart_port *,
				               struct ktermios *new,
				               const struct ktermios *old);
	void			(*set_ldisc)(struct uart_port *,
					     struct ktermios *);
	unsigned int		(*get_mctrl)(struct uart_port *);
	void			(*set_mctrl)(struct uart_port *, unsigned int);
	unsigned int		(*get_divisor)(struct uart_port *,
					       unsigned int baud,
					       unsigned int *frac);
	void			(*set_divisor)(struct uart_port *,
					       unsigned int baud,
					       unsigned int quot,
					       unsigned int quot_frac);
	int			(*startup)(struct uart_port *port);
	void			(*shutdown)(struct uart_port *port);
	void			(*throttle)(struct uart_port *port);
	void			(*unthrottle)(struct uart_port *port);
	int			(*handle_irq)(struct uart_port *);
	void			(*pm)(struct uart_port *, unsigned int state,
				      unsigned int old);
	void			(*handle_break)(struct uart_port *);
	int			(*rs485_config)(struct uart_port *,
						struct ktermios *termios,
						struct serial_rs485 *rs485);
	int			(*iso7816_config)(struct uart_port *,
						  struct serial_iso7816 *iso7816);
	unsigned int		ctrl_id;		/* optional serial core controller id */
	unsigned int		port_id;		/* optional serial core port id */
	unsigned int		irq;			/* irq number */
	unsigned long		irqflags;		/* irq flags  */
	unsigned int		uartclk;		/* base uart clock */
	unsigned int		fifosize;		/* tx fifo size */
	unsigned char		x_char;			/* xon/xoff char */
	unsigned char		regshift;		/* reg offset shift */
	unsigned char		iotype;			/* io access style */
	unsigned char		quirks;			/* internal quirks */

#define UPIO_PORT		(SERIAL_IO_PORT)	/* 8b I/O port access */
#define UPIO_HUB6		(SERIAL_IO_HUB6)	/* Hub6 ISA card */
#define UPIO_MEM		(SERIAL_IO_MEM)		/* driver-specific */
#define UPIO_MEM32		(SERIAL_IO_MEM32)	/* 32b little endian */
#define UPIO_AU			(SERIAL_IO_AU)		/* Au1x00 and RT288x type IO */
#define UPIO_TSI		(SERIAL_IO_TSI)		/* Tsi108/109 type IO */
#define UPIO_MEM32BE		(SERIAL_IO_MEM32BE)	/* 32b big endian */
#define UPIO_MEM16		(SERIAL_IO_MEM16)	/* 16b little endian */

	/* quirks must be updated while holding port mutex */
#define UPQ_NO_TXEN_TEST	BIT(0)

	unsigned int		read_status_mask;	/* driver specific */
	unsigned int		ignore_status_mask;	/* driver specific */
	struct uart_state	*state;			/* pointer to parent state */
	struct uart_icount	icount;			/* statistics */

	struct console		*cons;			/* struct console, if any */
	/* flags must be updated while holding port mutex */
	upf_t			flags;

	/*
	 * These flags must be equivalent to the flags defined in
	 * include/uapi/linux/tty_flags.h which are the userspace definitions
	 * assigned from the serial_struct flags in uart_set_info()
	 * [for bit definitions in the UPF_CHANGE_MASK]
	 *
	 * Bits [0..ASYNCB_LAST_USER] are userspace defined/visible/changeable
	 * The remaining bits are serial-core specific and not modifiable by
	 * userspace.
	 */
#define UPF_FOURPORT		((__force upf_t) ASYNC_FOURPORT       /* 1  */ )
#define UPF_SAK			((__force upf_t) ASYNC_SAK            /* 2  */ )
#define UPF_SPD_HI		((__force upf_t) ASYNC_SPD_HI         /* 4  */ )
#define UPF_SPD_VHI		((__force upf_t) ASYNC_SPD_VHI        /* 5  */ )
#define UPF_SPD_CUST		((__force upf_t) ASYNC_SPD_CUST   /* 0x0030 */ )
#define UPF_SPD_WARP		((__force upf_t) ASYNC_SPD_WARP   /* 0x1010 */ )
#define UPF_SPD_MASK		((__force upf_t) ASYNC_SPD_MASK   /* 0x1030 */ )
#define UPF_SKIP_TEST		((__force upf_t) ASYNC_SKIP_TEST      /* 6  */ )
#define UPF_AUTO_IRQ		((__force upf_t) ASYNC_AUTO_IRQ       /* 7  */ )
#define UPF_HARDPPS_CD		((__force upf_t) ASYNC_HARDPPS_CD     /* 11 */ )
#define UPF_SPD_SHI		((__force upf_t) ASYNC_SPD_SHI        /* 12 */ )
#define UPF_LOW_LATENCY		((__force upf_t) ASYNC_LOW_LATENCY    /* 13 */ )
#define UPF_BUGGY_UART		((__force upf_t) ASYNC_BUGGY_UART     /* 14 */ )
#define UPF_MAGIC_MULTIPLIER	((__force upf_t) ASYNC_MAGIC_MULTIPLIER /* 16 */ )

#define UPF_NO_THRE_TEST	((__force upf_t) BIT_ULL(19))
/* Port has hardware-assisted h/w flow control */
#define UPF_AUTO_CTS		((__force upf_t) BIT_ULL(20))
#define UPF_AUTO_RTS		((__force upf_t) BIT_ULL(21))
#define UPF_HARD_FLOW		((__force upf_t) (UPF_AUTO_CTS | UPF_AUTO_RTS))
/* Port has hardware-assisted s/w flow control */
#define UPF_SOFT_FLOW		((__force upf_t) BIT_ULL(22))
#define UPF_CONS_FLOW		((__force upf_t) BIT_ULL(23))
#define UPF_SHARE_IRQ		((__force upf_t) BIT_ULL(24))
#define UPF_EXAR_EFR		((__force upf_t) BIT_ULL(25))
#define UPF_BUG_THRE		((__force upf_t) BIT_ULL(26))
/* The exact UART type is known and should not be probed.  */
#define UPF_FIXED_TYPE		((__force upf_t) BIT_ULL(27))
#define UPF_BOOT_AUTOCONF	((__force upf_t) BIT_ULL(28))
#define UPF_FIXED_PORT		((__force upf_t) BIT_ULL(29))
#define UPF_DEAD		((__force upf_t) BIT_ULL(30))
#define UPF_IOREMAP		((__force upf_t) BIT_ULL(31))
#define UPF_FULL_PROBE		((__force upf_t) BIT_ULL(32))

#define __UPF_CHANGE_MASK	0x17fff
#define UPF_CHANGE_MASK		((__force upf_t) __UPF_CHANGE_MASK)
#define UPF_USR_MASK		((__force upf_t) (UPF_SPD_MASK|UPF_LOW_LATENCY))

#if __UPF_CHANGE_MASK > ASYNC_FLAGS
#error Change mask not equivalent to userspace-visible bit defines
#endif

	/*
	 * Must hold termios_rwsem, port mutex and port lock to change;
	 * can hold any one lock to read.
	 */
	upstat_t		status;

#define UPSTAT_CTS_ENABLE	((__force upstat_t) (1 << 0))
#define UPSTAT_DCD_ENABLE	((__force upstat_t) (1 << 1))
#define UPSTAT_AUTORTS		((__force upstat_t) (1 << 2))
#define UPSTAT_AUTOCTS		((__force upstat_t) (1 << 3))
#define UPSTAT_AUTOXOFF		((__force upstat_t) (1 << 4))
#define UPSTAT_SYNC_FIFO	((__force upstat_t) (1 << 5))

	bool			hw_stopped;		/* sw-assisted CTS flow state */
	unsigned int		mctrl;			/* current modem ctrl settings */
	unsigned int		frame_time;		/* frame timing in ns */
	unsigned int		type;			/* port type */
	const struct uart_ops	*ops;
	unsigned int		custom_divisor;
	unsigned int		line;			/* port index */
	unsigned int		minor;
	resource_size_t		mapbase;		/* for ioremap */
	resource_size_t		mapsize;
	struct device		*dev;			/* serial port physical parent device */
	struct serial_port_device *port_dev;		/* serial core port device */

	unsigned long		sysrq;			/* sysrq timeout */
	u8			sysrq_ch;		/* char for sysrq */
	unsigned char		has_sysrq;
	unsigned char		sysrq_seq;		/* index in sysrq_toggle_seq */

	unsigned char		hub6;			/* this should be in the 8250 driver */
	unsigned char		suspended;
	unsigned char		console_reinit;
	const char		*name;			/* port name */
	struct attribute_group	*attr_group;		/* port specific attributes */
	const struct attribute_group **tty_groups;	/* all attributes (serial core use only) */
	struct serial_rs485     rs485;
	struct serial_rs485	rs485_supported;	/* Supported mask for serial_rs485 */
	struct gpio_desc	*rs485_term_gpio;	/* enable RS485 bus termination */
	struct gpio_desc	*rs485_rx_during_tx_gpio; /* Output GPIO that sets the state of RS485 RX during TX */
	struct serial_iso7816   iso7816;
	void			*private_data;		/* generic platform data pointer */
};

/**
 * uart_port_lock - Lock the UART port
 * @up:		Pointer to UART port structure
 */
static inline void uart_port_lock(struct uart_port *up)
{
	spin_lock(&up->lock);
}

/**
 * uart_port_lock_irq - Lock the UART port and disable interrupts
 * @up:		Pointer to UART port structure
 */
static inline void uart_port_lock_irq(struct uart_port *up)
{
	spin_lock_irq(&up->lock);
}

/**
 * uart_port_lock_irqsave - Lock the UART port, save and disable interrupts
 * @up:		Pointer to UART port structure
 * @flags:	Pointer to interrupt flags storage
 */
static inline void uart_port_lock_irqsave(struct uart_port *up, unsigned long *flags)
{
	spin_lock_irqsave(&up->lock, *flags);
}

/**
 * uart_port_trylock - Try to lock the UART port
 * @up:		Pointer to UART port structure
 *
 * Returns: True if lock was acquired, false otherwise
 */
static inline bool uart_port_trylock(struct uart_port *up)
{
	return spin_trylock(&up->lock);
}

/**
 * uart_port_trylock_irqsave - Try to lock the UART port, save and disable interrupts
 * @up:		Pointer to UART port structure
 * @flags:	Pointer to interrupt flags storage
 *
 * Returns: True if lock was acquired, false otherwise
 */
static inline bool uart_port_trylock_irqsave(struct uart_port *up, unsigned long *flags)
{
	return spin_trylock_irqsave(&up->lock, *flags);
}

/**
 * uart_port_unlock - Unlock the UART port
 * @up:		Pointer to UART port structure
 */
static inline void uart_port_unlock(struct uart_port *up)
{
	spin_unlock(&up->lock);
}

/**
 * uart_port_unlock_irq - Unlock the UART port and re-enable interrupts
 * @up:		Pointer to UART port structure
 */
static inline void uart_port_unlock_irq(struct uart_port *up)
{
	spin_unlock_irq(&up->lock);
}

/**
 * uart_port_unlock_irqrestore - Unlock the UART port, restore interrupts
 * @up:		Pointer to UART port structure
 * @flags:	The saved interrupt flags for restore
 */
static inline void uart_port_unlock_irqrestore(struct uart_port *up, unsigned long flags)
{
	spin_unlock_irqrestore(&up->lock, flags);
}

static inline int serial_port_in(struct uart_port *up, int offset)
{
	return up->serial_in(up, offset);
}

static inline void serial_port_out(struct uart_port *up, int offset, int value)
{
	up->serial_out(up, offset, value);
}

/**
 * enum uart_pm_state - power states for UARTs
 * @UART_PM_STATE_ON: UART is powered, up and operational
 * @UART_PM_STATE_OFF: UART is powered off
 * @UART_PM_STATE_UNDEFINED: sentinel
 */
enum uart_pm_state {
	UART_PM_STATE_ON = 0,
	UART_PM_STATE_OFF = 3, /* number taken from ACPI */
	UART_PM_STATE_UNDEFINED,
};

/*
 * This is the state information which is persistent across opens.
 */
struct uart_state {
	struct tty_port		port;

	enum uart_pm_state	pm_state;
	struct circ_buf		xmit;

	atomic_t		refcount;
	wait_queue_head_t	remove_wait;
	struct uart_port	*uart_port;
};

#define UART_XMIT_SIZE	PAGE_SIZE


/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS		256

/**
 * uart_xmit_advance - Advance xmit buffer and account Tx'ed chars
 * @up: uart_port structure describing the port
 * @chars: number of characters sent
 *
 * This function advances the tail of circular xmit buffer by the number of
 * @chars transmitted and handles accounting of transmitted bytes (into
 * @up's icount.tx).
 */
static inline void uart_xmit_advance(struct uart_port *up, unsigned int chars)
{
	struct circ_buf *xmit = &up->state->xmit;

	xmit->tail = (xmit->tail + chars) & (UART_XMIT_SIZE - 1);
	up->icount.tx += chars;
}

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

/**
 * enum UART_TX_FLAGS -- flags for uart_port_tx_flags()
 *
 * @UART_TX_NOSTOP: don't call port->ops->stop_tx() on empty buffer
 */
enum UART_TX_FLAGS {
	UART_TX_NOSTOP = BIT(0),
};

#define __uart_port_tx(uport, ch, flags, tx_ready, put_char, tx_done,	      \
		       for_test, for_post)				      \
({									      \
	struct uart_port *__port = (uport);				      \
	struct circ_buf *xmit = &__port->state->xmit;			      \
	unsigned int pending;						      \
									      \
	for (; (for_test) && (tx_ready); (for_post), __port->icount.tx++) {   \
		if (__port->x_char) {					      \
			(ch) = __port->x_char;				      \
			(put_char);					      \
			__port->x_char = 0;				      \
			continue;					      \
		}							      \
									      \
		if (uart_circ_empty(xmit) || uart_tx_stopped(__port))	      \
			break;						      \
									      \
		(ch) = xmit->buf[xmit->tail];				      \
		(put_char);						      \
		xmit->tail = (xmit->tail + 1) % UART_XMIT_SIZE;		      \
	}								      \
									      \
	(tx_done);							      \
									      \
	pending = uart_circ_chars_pending(xmit);			      \
	if (pending < WAKEUP_CHARS) {					      \
		uart_write_wakeup(__port);				      \
									      \
		if (!((flags) & UART_TX_NOSTOP) && pending == 0)	      \
			__port->ops->stop_tx(__port);			      \
	}								      \
									      \
	pending;							      \
})

/**
 * uart_port_tx_limited -- transmit helper for uart_port with count limiting
 * @port: uart port
 * @ch: variable to store a character to be written to the HW
 * @count: a limit of characters to send
 * @tx_ready: can HW accept more data function
 * @put_char: function to write a character
 * @tx_done: function to call after the loop is done
 *
 * This helper transmits characters from the xmit buffer to the hardware using
 * @put_char(). It does so until @count characters are sent and while @tx_ready
 * evaluates to true.
 *
 * Returns: the number of characters in the xmit buffer when done.
 *
 * The expression in macro parameters shall be designed as follows:
 *  * **tx_ready:** should evaluate to true if the HW can accept more data to
 *    be sent. This parameter can be %true, which means the HW is always ready.
 *  * **put_char:** shall write @ch to the device of @port.
 *  * **tx_done:** when the write loop is done, this can perform arbitrary
 *    action before potential invocation of ops->stop_tx() happens. If the
 *    driver does not need to do anything, use e.g. ({}).
 *
 * For all of them, @port->lock is held, interrupts are locally disabled and
 * the expressions must not sleep.
 */
#define uart_port_tx_limited(port, ch, count, tx_ready, put_char, tx_done) ({ \
	unsigned int __count = (count);					      \
	__uart_port_tx(port, ch, 0, tx_ready, put_char, tx_done, __count,     \
			__count--);					      \
})

/**
 * uart_port_tx -- transmit helper for uart_port
 * @port: uart port
 * @ch: variable to store a character to be written to the HW
 * @tx_ready: can HW accept more data function
 * @put_char: function to write a character
 *
 * See uart_port_tx_limited() for more details.
 */
#define uart_port_tx(port, ch, tx_ready, put_char)			\
	__uart_port_tx(port, ch, 0, tx_ready, put_char, ({}), true, ({}))


/**
 * uart_port_tx_flags -- transmit helper for uart_port with flags
 * @port: uart port
 * @ch: variable to store a character to be written to the HW
 * @flags: %UART_TX_NOSTOP or similar
 * @tx_ready: can HW accept more data function
 * @put_char: function to write a character
 *
 * See uart_port_tx_limited() for more details.
 */
#define uart_port_tx_flags(port, ch, flags, tx_ready, put_char)		\
	__uart_port_tx(port, ch, flags, tx_ready, put_char, ({}), true, ({}))
/*
 * Baud rate helpers.
 */
void uart_update_timeout(struct uart_port *port, unsigned int cflag,
			 unsigned int baud);
unsigned int uart_get_baud_rate(struct uart_port *port, struct ktermios *termios,
				const struct ktermios *old, unsigned int min,
				unsigned int max);
unsigned int uart_get_divisor(struct uart_port *port, unsigned int baud);

/*
 * Calculates FIFO drain time.
 */
static inline unsigned long uart_fifo_timeout(struct uart_port *port)
{
	u64 fifo_timeout = (u64)READ_ONCE(port->frame_time) * port->fifosize;

	/* Add .02 seconds of slop */
	fifo_timeout += 20 * NSEC_PER_MSEC;

	return max(nsecs_to_jiffies(fifo_timeout), 1UL);
}

/* Base timer interval for polling */
static inline int uart_poll_timeout(struct uart_port *port)
{
	int timeout = uart_fifo_timeout(port);

	return timeout > 6 ? (timeout / 2 - 2) : 1;
}

/*
 * Console helpers.
 */
struct earlycon_device {
	struct console *con;
	struct uart_port port;
	char options[32];		/* e.g., 115200n8 */
	unsigned int baud;
};

struct earlycon_id {
	char	name[15];
	char	name_term;	/* In case compiler didn't '\0' term name */
	char	compatible[128];
	int	(*setup)(struct earlycon_device *, const char *options);
};

extern const struct earlycon_id __earlycon_table[];
extern const struct earlycon_id __earlycon_table_end[];

#if defined(CONFIG_SERIAL_EARLYCON) && !defined(MODULE)
#define EARLYCON_USED_OR_UNUSED	__used
#else
#define EARLYCON_USED_OR_UNUSED	__maybe_unused
#endif

#define OF_EARLYCON_DECLARE(_name, compat, fn)				\
	static const struct earlycon_id __UNIQUE_ID(__earlycon_##_name) \
		EARLYCON_USED_OR_UNUSED  __section("__earlycon_table")  \
		__aligned(__alignof__(struct earlycon_id))		\
		= { .name = __stringify(_name),				\
		    .compatible = compat,				\
		    .setup = fn }

#define EARLYCON_DECLARE(_name, fn)	OF_EARLYCON_DECLARE(_name, "", fn)

int of_setup_earlycon(const struct earlycon_id *match, unsigned long node,
		      const char *options);

#ifdef CONFIG_SERIAL_EARLYCON
extern bool earlycon_acpi_spcr_enable __initdata;
int setup_earlycon(char *buf);
#else
static const bool earlycon_acpi_spcr_enable EARLYCON_USED_OR_UNUSED;
static inline int setup_earlycon(char *buf) { return 0; }
#endif

/* Variant of uart_console_registered() when the console_list_lock is held. */
static inline bool uart_console_registered_locked(struct uart_port *port)
{
	return uart_console(port) && console_is_registered_locked(port->cons);
}

static inline bool uart_console_registered(struct uart_port *port)
{
	return uart_console(port) && console_is_registered(port->cons);
}

struct uart_port *uart_get_console(struct uart_port *ports, int nr,
				   struct console *c);
int uart_parse_earlycon(char *p, unsigned char *iotype, resource_size_t *addr,
			char **options);
void uart_parse_options(const char *options, int *baud, int *parity, int *bits,
			int *flow);
int uart_set_options(struct uart_port *port, struct console *co, int baud,
		     int parity, int bits, int flow);
struct tty_driver *uart_console_device(struct console *co, int *index);
void uart_console_write(struct uart_port *port, const char *s,
			unsigned int count,
			void (*putchar)(struct uart_port *, unsigned char));

/*
 * Port/driver registration/removal
 */
int uart_register_driver(struct uart_driver *uart);
void uart_unregister_driver(struct uart_driver *uart);
int uart_add_one_port(struct uart_driver *reg, struct uart_port *port);
void uart_remove_one_port(struct uart_driver *reg, struct uart_port *port);
bool uart_match_port(const struct uart_port *port1,
		const struct uart_port *port2);

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
	struct tty_struct *tty = port->state->port.tty;
	if ((tty && tty->flow.stopped) || port->hw_stopped)
		return 1;
	return 0;
}

static inline bool uart_cts_enabled(struct uart_port *uport)
{
	return !!(uport->status & UPSTAT_CTS_ENABLE);
}

static inline bool uart_softcts_mode(struct uart_port *uport)
{
	upstat_t mask = UPSTAT_CTS_ENABLE | UPSTAT_AUTOCTS;

	return ((uport->status & mask) == UPSTAT_CTS_ENABLE);
}

/*
 * The following are helper functions for the low level drivers.
 */

void uart_handle_dcd_change(struct uart_port *uport, bool active);
void uart_handle_cts_change(struct uart_port *uport, bool active);

void uart_insert_char(struct uart_port *port, unsigned int status,
		      unsigned int overrun, u8 ch, u8 flag);

void uart_xchar_out(struct uart_port *uport, int offset);

#ifdef CONFIG_MAGIC_SYSRQ_SERIAL
#define SYSRQ_TIMEOUT	(HZ * 5)

bool uart_try_toggle_sysrq(struct uart_port *port, u8 ch);

static inline int uart_handle_sysrq_char(struct uart_port *port, u8 ch)
{
	if (!port->sysrq)
		return 0;

	if (ch && time_before(jiffies, port->sysrq)) {
		if (sysrq_mask()) {
			handle_sysrq(ch);
			port->sysrq = 0;
			return 1;
		}
		if (uart_try_toggle_sysrq(port, ch))
			return 1;
	}
	port->sysrq = 0;

	return 0;
}

static inline int uart_prepare_sysrq_char(struct uart_port *port, u8 ch)
{
	if (!port->sysrq)
		return 0;

	if (ch && time_before(jiffies, port->sysrq)) {
		if (sysrq_mask()) {
			port->sysrq_ch = ch;
			port->sysrq = 0;
			return 1;
		}
		if (uart_try_toggle_sysrq(port, ch))
			return 1;
	}
	port->sysrq = 0;

	return 0;
}

static inline void uart_unlock_and_check_sysrq(struct uart_port *port)
{
	u8 sysrq_ch;

	if (!port->has_sysrq) {
		spin_unlock(&port->lock);
		return;
	}

	sysrq_ch = port->sysrq_ch;
	port->sysrq_ch = 0;

	spin_unlock(&port->lock);

	if (sysrq_ch)
		handle_sysrq(sysrq_ch);
}

static inline void uart_unlock_and_check_sysrq_irqrestore(struct uart_port *port,
		unsigned long flags)
{
	u8 sysrq_ch;

	if (!port->has_sysrq) {
		spin_unlock_irqrestore(&port->lock, flags);
		return;
	}

	sysrq_ch = port->sysrq_ch;
	port->sysrq_ch = 0;

	spin_unlock_irqrestore(&port->lock, flags);

	if (sysrq_ch)
		handle_sysrq(sysrq_ch);
}
#else	/* CONFIG_MAGIC_SYSRQ_SERIAL */
static inline int uart_handle_sysrq_char(struct uart_port *port, u8 ch)
{
	return 0;
}
static inline int uart_prepare_sysrq_char(struct uart_port *port, u8 ch)
{
	return 0;
}
static inline void uart_unlock_and_check_sysrq(struct uart_port *port)
{
	spin_unlock(&port->lock);
}
static inline void uart_unlock_and_check_sysrq_irqrestore(struct uart_port *port,
		unsigned long flags)
{
	spin_unlock_irqrestore(&port->lock, flags);
}
#endif	/* CONFIG_MAGIC_SYSRQ_SERIAL */

/*
 * We do the SysRQ and SAK checking like this...
 */
static inline int uart_handle_break(struct uart_port *port)
{
	struct uart_state *state = port->state;

	if (port->handle_break)
		port->handle_break(port);

#ifdef CONFIG_MAGIC_SYSRQ_SERIAL
	if (port->has_sysrq && uart_console(port)) {
		if (!port->sysrq) {
			port->sysrq = jiffies + SYSRQ_TIMEOUT;
			return 1;
		}
		port->sysrq = 0;
	}
#endif
	if (port->flags & UPF_SAK)
		do_SAK(state->port.tty);
	return 0;
}

/*
 *	UART_ENABLE_MS - determine if port should enable modem status irqs
 */
#define UART_ENABLE_MS(port,cflag)	((port)->flags & UPF_HARDPPS_CD || \
					 (cflag) & CRTSCTS || \
					 !((cflag) & CLOCAL))

int uart_get_rs485_mode(struct uart_port *port);
#endif /* LINUX_SERIAL_CORE_H */
