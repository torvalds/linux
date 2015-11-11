/*
 *  linux/include/linux/serial_8250.h
 *
 *  Copyright (C) 2004 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef _LINUX_SERIAL_8250_H
#define _LINUX_SERIAL_8250_H

#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/platform_device.h>

/*
 * This is the platform device platform_data structure
 */
struct plat_serial8250_port {
	unsigned long	iobase;		/* io base address */
	void __iomem	*membase;	/* ioremap cookie or NULL */
	resource_size_t	mapbase;	/* resource base */
	unsigned int	irq;		/* interrupt number */
	unsigned long	irqflags;	/* request_irq flags */
	unsigned int	uartclk;	/* UART clock rate */
	void            *private_data;
	unsigned char	regshift;	/* register shift */
	unsigned char	iotype;		/* UPIO_* */
	unsigned char	hub6;
	upf_t		flags;		/* UPF_* flags */
	unsigned int	type;		/* If UPF_FIXED_TYPE */
	unsigned int	(*serial_in)(struct uart_port *, int);
	void		(*serial_out)(struct uart_port *, int, int);
	void		(*set_termios)(struct uart_port *,
			               struct ktermios *new,
			               struct ktermios *old);
	int		(*handle_irq)(struct uart_port *);
	void		(*pm)(struct uart_port *, unsigned int state,
			      unsigned old);
	void		(*handle_break)(struct uart_port *);
};

/*
 * Allocate 8250 platform device IDs.  Nothing is implied by
 * the numbering here, except for the legacy entry being -1.
 */
enum {
	PLAT8250_DEV_LEGACY = -1,
	PLAT8250_DEV_PLATFORM,
	PLAT8250_DEV_PLATFORM1,
	PLAT8250_DEV_PLATFORM2,
	PLAT8250_DEV_FOURPORT,
	PLAT8250_DEV_ACCENT,
	PLAT8250_DEV_BOCA,
	PLAT8250_DEV_EXAR_ST16C554,
	PLAT8250_DEV_HUB6,
	PLAT8250_DEV_AU1X00,
	PLAT8250_DEV_SM501,
};

struct uart_8250_dma;
struct uart_8250_port;

/**
 * 8250 core driver operations
 *
 * @setup_irq()		Setup irq handling. The universal 8250 driver links this
 *			port to the irq chain. Other drivers may @request_irq().
 * @release_irq()	Undo irq handling. The universal 8250 driver unlinks
 *			the port from the irq chain.
 */
struct uart_8250_ops {
	int		(*setup_irq)(struct uart_8250_port *);
	void		(*release_irq)(struct uart_8250_port *);
};

/*
 * This should be used by drivers which want to register
 * their own 8250 ports without registering their own
 * platform device.  Using these will make your driver
 * dependent on the 8250 driver.
 */

struct uart_8250_port {
	struct uart_port	port;
	struct timer_list	timer;		/* "no irq" timer */
	struct list_head	list;		/* ports on this IRQ */
	unsigned short		capabilities;	/* port capabilities */
	unsigned short		bugs;		/* port bugs */
	bool			fifo_bug;	/* min RX trigger if enabled */
	unsigned int		tx_loadsz;	/* transmit fifo load size */
	unsigned char		acr;
	unsigned char		fcr;
	unsigned char		ier;
	unsigned char		lcr;
	unsigned char		mcr;
	unsigned char		mcr_mask;	/* mask of user bits */
	unsigned char		mcr_force;	/* mask of forced bits */
	unsigned char		cur_iotype;	/* Running I/O type */
	unsigned int		rpm_tx_active;
	unsigned char		canary;		/* non-zero during system sleep
						 *   if no_console_suspend
						 */
	unsigned char		probe;
#define UART_PROBE_RSA	(1 << 0)

	/*
	 * Some bits in registers are cleared on a read, so they must
	 * be saved whenever the register is read but the bits will not
	 * be immediately processed.
	 */
#define LSR_SAVE_FLAGS UART_LSR_BRK_ERROR_BITS
	unsigned char		lsr_saved_flags;
#define MSR_SAVE_FLAGS UART_MSR_ANY_DELTA
	unsigned char		msr_saved_flags;

	struct uart_8250_dma	*dma;
	const struct uart_8250_ops *ops;

	/* 8250 specific callbacks */
	int			(*dl_read)(struct uart_8250_port *);
	void			(*dl_write)(struct uart_8250_port *, int);
};

static inline struct uart_8250_port *up_to_u8250p(struct uart_port *up)
{
	return container_of(up, struct uart_8250_port, port);
}

int serial8250_register_8250_port(struct uart_8250_port *);
void serial8250_unregister_port(int line);
void serial8250_suspend_port(int line);
void serial8250_resume_port(int line);

extern int early_serial_setup(struct uart_port *port);

extern int early_serial8250_setup(struct earlycon_device *device,
					 const char *options);
extern void serial8250_do_set_termios(struct uart_port *port,
		struct ktermios *termios, struct ktermios *old);
extern int serial8250_do_startup(struct uart_port *port);
extern void serial8250_do_shutdown(struct uart_port *port);
extern void serial8250_do_pm(struct uart_port *port, unsigned int state,
			     unsigned int oldstate);
extern void serial8250_do_set_mctrl(struct uart_port *port, unsigned int mctrl);
extern int fsl8250_handle_irq(struct uart_port *port);
int serial8250_handle_irq(struct uart_port *port, unsigned int iir);
unsigned char serial8250_rx_chars(struct uart_8250_port *up, unsigned char lsr);
void serial8250_tx_chars(struct uart_8250_port *up);
unsigned int serial8250_modem_status(struct uart_8250_port *up);
void serial8250_init_port(struct uart_8250_port *up);
void serial8250_set_defaults(struct uart_8250_port *up);
void serial8250_console_write(struct uart_8250_port *up, const char *s,
			      unsigned int count);
int serial8250_console_setup(struct uart_port *port, char *options, bool probe);

extern void serial8250_set_isa_configurator(void (*v)
					(int port, struct uart_port *up,
						unsigned short *capabilities));

#endif
