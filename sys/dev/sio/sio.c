/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)com.c	7.5 (Berkeley) 5/16/91
 *	from: i386/isa sio.c,v 1.234
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_gdb.h"
#include "opt_kdb.h"
#include "opt_sio.h"

/*
 * Serial driver, based on 386BSD-0.1 com driver.
 * Mostly rewritten to use pseudo-DMA.
 * Works for National Semiconductor NS8250-NS16550AF UARTs.
 * COM driver, based on HP dca driver.
 *
 * Changes for PC Card integration:
 *	- Added PC Card driver table and handlers
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/interrupt.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/serial.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/tty.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/timepps.h>
#include <sys/uio.h>
#include <sys/cons.h>

#include <isa/isavar.h>

#include <machine/resource.h>

#include <dev/sio/sioreg.h>
#include <dev/sio/siovar.h>

#ifdef COM_ESP
#include <dev/ic/esp.h>
#endif
#include <dev/ic/ns16550.h>

#define	LOTS_OF_EVENTS	64	/* helps separate urgent events from input */

#ifdef COM_MULTIPORT
/* checks in flags for multiport and which is multiport "master chip"
 * for a given card
 */
#define	COM_ISMULTIPORT(flags)	((flags) & 0x01)
#define	COM_MPMASTER(flags)	(((flags) >> 8) & 0x0ff)
#define	COM_NOTAST4(flags)	((flags) & 0x04)
#else
#define	COM_ISMULTIPORT(flags)	(0)
#endif /* COM_MULTIPORT */

#define	COM_C_IIR_TXRDYBUG	0x80000
#define	COM_CONSOLE(flags)	((flags) & 0x10)
#define	COM_DEBUGGER(flags)	((flags) & 0x80)
#define	COM_FIFOSIZE(flags)	(((flags) & 0xff000000) >> 24)
#define	COM_FORCECONSOLE(flags)	((flags) & 0x20)
#define	COM_IIR_TXRDYBUG(flags)	((flags) & COM_C_IIR_TXRDYBUG)
#define	COM_LLCONSOLE(flags)	((flags) & 0x40)
#define	COM_LOSESOUTINTS(flags)	((flags) & 0x08)
#define	COM_NOFIFO(flags)	((flags) & 0x02)
#define	COM_NOPROBE(flags)	((flags) & 0x40000)
#define	COM_NOSCR(flags)	((flags) & 0x100000)
#define	COM_PPSCTS(flags)	((flags) & 0x10000)
#define	COM_ST16650A(flags)	((flags) & 0x20000)
#define	COM_TI16754(flags)	((flags) & 0x200000)

#define	sio_getreg(com, off) \
	(bus_space_read_1((com)->bst, (com)->bsh, (off)))
#define	sio_setreg(com, off, value) \
	(bus_space_write_1((com)->bst, (com)->bsh, (off), (value)))

/*
 * com state bits.
 * (CS_BUSY | CS_TTGO) and (CS_BUSY | CS_TTGO | CS_ODEVREADY) must be higher
 * than the other bits so that they can be tested as a group without masking
 * off the low bits.
 *
 * The following com and tty flags correspond closely:
 *	CS_BUSY		= TS_BUSY (maintained by comstart(), siopoll() and
 *				   comstop())
 *	CS_TTGO		= ~TS_TTSTOP (maintained by comparam() and comstart())
 *	CS_CTS_OFLOW	= CCTS_OFLOW (maintained by comparam())
 *	CS_RTS_IFLOW	= CRTS_IFLOW (maintained by comparam())
 * TS_FLUSH is not used.
 * XXX I think TIOCSETA doesn't clear TS_TTSTOP when it clears IXON.
 * XXX CS_*FLOW should be CF_*FLOW in com->flags (control flags not state).
 */
#define	CS_BUSY		0x80	/* output in progress */
#define	CS_TTGO		0x40	/* output not stopped by XOFF */
#define	CS_ODEVREADY	0x20	/* external device h/w ready (CTS) */
#define	CS_CHECKMSR	1	/* check of MSR scheduled */
#define	CS_CTS_OFLOW	2	/* use CTS output flow control */
#define	CS_ODONE	4	/* output completed */
#define	CS_RTS_IFLOW	8	/* use RTS input flow control */
#define	CSE_BUSYCHECK	1	/* siobusycheck() scheduled */

static	char const * const	error_desc[] = {
#define	CE_OVERRUN			0
	"silo overflow",
#define	CE_INTERRUPT_BUF_OVERFLOW	1
	"interrupt-level buffer overflow",
#define	CE_TTY_BUF_OVERFLOW		2
	"tty-level buffer overflow",
};

#define	CE_NTYPES			3
#define	CE_RECORD(com, errnum)		(++(com)->delta_error_counts[errnum])

/* types.  XXX - should be elsewhere */
typedef u_int	Port_t;		/* hardware port */
typedef u_char	bool_t;		/* boolean */

/* queue of linear buffers */
struct lbq {
	u_char	*l_head;	/* next char to process */
	u_char	*l_tail;	/* one past the last char to process */
	struct lbq *l_next;	/* next in queue */
	bool_t	l_queued;	/* nonzero if queued */
};

/* com device structure */
struct com_s {
	u_char	state;		/* miscellaneous flag bits */
	u_char	cfcr_image;	/* copy of value written to CFCR */
#ifdef COM_ESP
	bool_t	esp;		/* is this unit a hayes esp board? */
#endif
	u_char	extra_state;	/* more flag bits, separate for order trick */
	u_char	fifo_image;	/* copy of value written to FIFO */
	bool_t	hasfifo;	/* nonzero for 16550 UARTs */
	bool_t	loses_outints;	/* nonzero if device loses output interrupts */
	u_char	mcr_image;	/* copy of value written to MCR */
#ifdef COM_MULTIPORT
	bool_t	multiport;	/* is this unit part of a multiport device? */
#endif /* COM_MULTIPORT */
	bool_t	no_irq;		/* nonzero if irq is not attached */
	bool_t  gone;		/* hardware disappeared */
	bool_t	poll;		/* nonzero if polling is required */
	bool_t	poll_output;	/* nonzero if polling for output is required */
	bool_t	st16650a;	/* nonzero if Startech 16650A compatible */
	int	unit;		/* unit	number */
	u_int	flags;		/* copy of device flags */
	u_int	tx_fifo_size;

	/*
	 * The high level of the driver never reads status registers directly
	 * because there would be too many side effects to handle conveniently.
	 * Instead, it reads copies of the registers stored here by the
	 * interrupt handler.
	 */
	u_char	last_modem_status;	/* last MSR read by intr handler */
	u_char	prev_modem_status;	/* last MSR handled by high level */

	u_char	*ibuf;		/* start of input buffer */
	u_char	*ibufend;	/* end of input buffer */
	u_char	*ibufold;	/* old input buffer, to be freed */
	u_char	*ihighwater;	/* threshold in input buffer */
	u_char	*iptr;		/* next free spot in input buffer */
	int	ibufsize;	/* size of ibuf (not include error bytes) */
	int	ierroff;	/* offset of error bytes in ibuf */

	struct lbq	obufq;	/* head of queue of output buffers */
	struct lbq	obufs[2];	/* output buffers */

	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;

	Port_t	data_port;	/* i/o ports */
#ifdef COM_ESP
	Port_t	esp_port;
#endif
	Port_t	int_ctl_port;
	Port_t	int_id_port;
	Port_t	modem_ctl_port;
	Port_t	line_status_port;
	Port_t	modem_status_port;

	struct tty	*tp;	/* cross reference */

	struct	pps_state pps;
	int	pps_bit;
#ifdef KDB
	int	alt_brk_state;
#endif

	u_long	bytes_in;	/* statistics */
	u_long	bytes_out;
	u_int	delta_error_counts[CE_NTYPES];
	u_long	error_counts[CE_NTYPES];

	u_long	rclk;

	struct resource *irqres;
	struct resource *ioportres;
	int	ioportrid;
	void	*cookie;

	/*
	 * Data area for output buffers.  Someday we should build the output
	 * buffer queue without copying data.
	 */
	u_char	obuf1[256];
	u_char	obuf2[256];
};

#ifdef COM_ESP
static	int	espattach(struct com_s *com, Port_t esp_port);
#endif

static	void	combreak(struct tty *tp, int sig);
static	timeout_t siobusycheck;
static	u_int	siodivisor(u_long rclk, speed_t speed);
static	void	comclose(struct tty *tp);
static	int	comopen(struct tty *tp, struct cdev *dev);
static	void	sioinput(struct com_s *com);
static	void	siointr1(struct com_s *com);
static	int	siointr(void *arg);
static	int	commodem(struct tty *tp, int sigon, int sigoff);
static	int	comparam(struct tty *tp, struct termios *t);
static	void	siopoll(void *);
static	void	siosettimeout(void);
static	int	siosetwater(struct com_s *com, speed_t speed);
static	void	comstart(struct tty *tp);
static	void	comstop(struct tty *tp, int rw);
static	timeout_t comwakeup;

char		sio_driver_name[] = "sio";
static struct	mtx sio_lock;
static int	sio_inited;

/* table and macro for fast conversion from a unit number to its com struct */
devclass_t	sio_devclass;
/*
 * XXX Assmues that devclass_get_device, devclass_get_softc and
 * device_get_softc are fast interrupt safe.  The current implementation
 * of these functions are.
 */
#define	com_addr(unit)	((struct com_s *) \
			 devclass_get_softc(sio_devclass, unit)) /* XXX */

int	comconsole = -1;
static	volatile speed_t	comdefaultrate = CONSPEED;
static	u_long			comdefaultrclk = DEFAULT_RCLK;
SYSCTL_ULONG(_machdep, OID_AUTO, conrclk, CTLFLAG_RW, &comdefaultrclk, 0, "");
static	speed_t			gdbdefaultrate = GDBSPEED;
SYSCTL_UINT(_machdep, OID_AUTO, gdbspeed, CTLFLAG_RW,
	    &gdbdefaultrate, GDBSPEED, "");
static	u_int	com_events;	/* input chars + weighted output completions */
static	Port_t	siocniobase;
static	int	siocnunit = -1;
static	void	*sio_slow_ih;
static	void	*sio_fast_ih;
static	int	sio_timeout;
static	int	sio_timeouts_until_log;
static	struct	callout_handle sio_timeout_handle
    = CALLOUT_HANDLE_INITIALIZER(&sio_timeout_handle);
static	int	sio_numunits;

#ifdef GDB
static	Port_t	siogdbiobase = 0;
#endif

#ifdef COM_ESP
/* XXX configure this properly. */
/* XXX quite broken for new-bus. */
static	Port_t	likely_com_ports[] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8, };
static	Port_t	likely_esp_ports[] = { 0x140, 0x180, 0x280, 0 };
#endif

/*
 * handle sysctl read/write requests for console speed
 * 
 * In addition to setting comdefaultrate for I/O through /dev/console,
 * also set the initial and lock values for the /dev/ttyXX device
 * if there is one associated with the console.  Finally, if the /dev/tty
 * device has already been open, change the speed on the open running port
 * itself.
 */

static int
sysctl_machdep_comdefaultrate(SYSCTL_HANDLER_ARGS)
{
	int error, s;
	speed_t newspeed;
	struct com_s *com;
	struct tty *tp;

	newspeed = comdefaultrate;

	error = sysctl_handle_opaque(oidp, &newspeed, sizeof newspeed, req);
	if (error || !req->newptr)
		return (error);

	comdefaultrate = newspeed;

	if (comconsole < 0)		/* serial console not selected? */
		return (0);

	com = com_addr(comconsole);
	if (com == NULL)
		return (ENXIO);

	tp = com->tp;
	if (tp == NULL)
		return (ENXIO);

	/*
	 * set the initial and lock rates for /dev/ttydXX and /dev/cuaXX
	 * (note, the lock rates really are boolean -- if non-zero, disallow
	 *  speed changes)
	 */
	tp->t_init_in.c_ispeed  = tp->t_init_in.c_ospeed =
	tp->t_lock_in.c_ispeed  = tp->t_lock_in.c_ospeed =
	tp->t_init_out.c_ispeed = tp->t_init_out.c_ospeed =
	tp->t_lock_out.c_ispeed = tp->t_lock_out.c_ospeed = comdefaultrate;

	if (tp->t_state & TS_ISOPEN) {
		tp->t_termios.c_ispeed =
		tp->t_termios.c_ospeed = comdefaultrate;
		s = spltty();
		error = comparam(tp, &tp->t_termios);
		splx(s);
	}
	return error;
}

SYSCTL_PROC(_machdep, OID_AUTO, conspeed, CTLTYPE_INT | CTLFLAG_RWTUN | CTLFLAG_NOFETCH,
	    0, 0, sysctl_machdep_comdefaultrate, "I", "");
TUNABLE_INT("machdep.conspeed", __DEVOLATILE(int *, &comdefaultrate));

#define SET_FLAG(dev, bit) device_set_flags(dev, device_get_flags(dev) | (bit))
#define CLR_FLAG(dev, bit) device_set_flags(dev, device_get_flags(dev) & ~(bit))

/*
 *	Unload the driver and clear the table.
 *	XXX this is mostly wrong.
 *	XXX TODO:
 *	This is usually called when the card is ejected, but
 *	can be caused by a kldunload of a controller driver.
 *	The idea is to reset the driver's view of the device
 *	and ensure that any driver entry points such as
 *	read and write do not hang.
 */
int
siodetach(device_t dev)
{
	struct com_s	*com;

	com = (struct com_s *) device_get_softc(dev);
	if (com == NULL) {
		device_printf(dev, "NULL com in siounload\n");
		return (0);
	}
	com->gone = TRUE;
	if (com->tp)
		ttyfree(com->tp);
	if (com->irqres) {
		bus_teardown_intr(dev, com->irqres, com->cookie);
		bus_release_resource(dev, SYS_RES_IRQ, 0, com->irqres);
	}
	if (com->ioportres)
		bus_release_resource(dev, SYS_RES_IOPORT, com->ioportrid,
				     com->ioportres);
	if (com->ibuf != NULL)
		free(com->ibuf, M_DEVBUF);

	device_set_softc(dev, NULL);
	free(com, M_DEVBUF);
	return (0);
}

int
sioprobe(dev, xrid, rclk, noprobe)
	device_t	dev;
	int		xrid;
	u_long		rclk;
	int		noprobe;
{
#if 0
	static bool_t	already_init;
	device_t	xdev;
#endif
	struct com_s	*com;
	u_int		divisor;
	bool_t		failures[10];
	int		fn;
	device_t	idev;
	Port_t		iobase;
	intrmask_t	irqmap[4];
	intrmask_t	irqs;
	u_char		mcr_image;
	int		result;
	u_long		xirq;
	u_int		flags = device_get_flags(dev);
	int		rid;
	struct resource *port;

	rid = xrid;
	port = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &rid,
					   IO_COMSIZE, RF_ACTIVE);
	if (!port)
		return (ENXIO);

	com = malloc(sizeof(*com), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (com == NULL) {
		bus_release_resource(dev, SYS_RES_IOPORT, rid, port);
		return (ENOMEM);
	}
	device_set_softc(dev, com);
	com->bst = rman_get_bustag(port);
	com->bsh = rman_get_bushandle(port);
	if (rclk == 0)
		rclk = DEFAULT_RCLK;
	com->rclk = rclk;

	while (sio_inited != 2)
		if (atomic_cmpset_int(&sio_inited, 0, 1)) {
			mtx_init(&sio_lock, sio_driver_name, NULL,
			    (comconsole != -1) ?
			    MTX_SPIN | MTX_QUIET : MTX_SPIN);
			atomic_store_rel_int(&sio_inited, 2);
		}

#if 0
	/*
	 * XXX this is broken - when we are first called, there are no
	 * previously configured IO ports.  We could hard code
	 * 0x3f8, 0x2f8, 0x3e8, 0x2e8 etc but that's probably worse.
	 * This code has been doing nothing since the conversion since
	 * "count" is zero the first time around.
	 */
	if (!already_init) {
		/*
		 * Turn off MCR_IENABLE for all likely serial ports.  An unused
		 * port with its MCR_IENABLE gate open will inhibit interrupts
		 * from any used port that shares the interrupt vector.
		 * XXX the gate enable is elsewhere for some multiports.
		 */
		device_t *devs;
		int count, i, xioport;

		devclass_get_devices(sio_devclass, &devs, &count);
		for (i = 0; i < count; i++) {
			xdev = devs[i];
			if (device_is_enabled(xdev) &&
			    bus_get_resource(xdev, SYS_RES_IOPORT, 0, &xioport,
					     NULL) == 0)
				outb(xioport + com_mcr, 0);
		}
		free(devs, M_TEMP);
		already_init = TRUE;
	}
#endif

	if (COM_LLCONSOLE(flags)) {
		printf("sio%d: reserved for low-level i/o\n",
		       device_get_unit(dev));
		bus_release_resource(dev, SYS_RES_IOPORT, rid, port);
		device_set_softc(dev, NULL);
		free(com, M_DEVBUF);
		return (ENXIO);
	}

	/*
	 * If the device is on a multiport card and has an AST/4
	 * compatible interrupt control register, initialize this
	 * register and prepare to leave MCR_IENABLE clear in the mcr.
	 * Otherwise, prepare to set MCR_IENABLE in the mcr.
	 * Point idev to the device struct giving the correct id_irq.
	 * This is the struct for the master device if there is one.
	 */
	idev = dev;
	mcr_image = MCR_IENABLE;
#ifdef COM_MULTIPORT
	if (COM_ISMULTIPORT(flags)) {
		Port_t xiobase;
		u_long io;

		idev = devclass_get_device(sio_devclass, COM_MPMASTER(flags));
		if (idev == NULL) {
			printf("sio%d: master device %d not configured\n",
			       device_get_unit(dev), COM_MPMASTER(flags));
			idev = dev;
		}
		if (!COM_NOTAST4(flags)) {
			if (bus_get_resource(idev, SYS_RES_IOPORT, 0, &io,
					     NULL) == 0) {
				xiobase = io;
				if (bus_get_resource(idev, SYS_RES_IRQ, 0,
				    NULL, NULL) == 0)
					outb(xiobase + com_scr, 0x80);
				else
					outb(xiobase + com_scr, 0);
			}
			mcr_image = 0;
		}
	}
#endif /* COM_MULTIPORT */
	if (bus_get_resource(idev, SYS_RES_IRQ, 0, NULL, NULL) != 0)
		mcr_image = 0;

	bzero(failures, sizeof failures);
	iobase = rman_get_start(port);

	/*
	 * We don't want to get actual interrupts, just masked ones.
	 * Interrupts from this line should already be masked in the ICU,
	 * but mask them in the processor as well in case there are some
	 * (misconfigured) shared interrupts.
	 */
	mtx_lock_spin(&sio_lock);
/* EXTRA DELAY? */

	/*
	 * For the TI16754 chips, set prescaler to 1 (4 is often the
	 * default after-reset value) as otherwise it's impossible to
	 * get highest baudrates.
	 */
	if (COM_TI16754(flags)) {
		u_char cfcr, efr;

		cfcr = sio_getreg(com, com_cfcr);
		sio_setreg(com, com_cfcr, CFCR_EFR_ENABLE);
		efr = sio_getreg(com, com_efr);
		/* Unlock extended features to turn off prescaler. */
		sio_setreg(com, com_efr, efr | EFR_EFE);
		/* Disable EFR. */
		sio_setreg(com, com_cfcr, (cfcr != CFCR_EFR_ENABLE) ? cfcr : 0);
		/* Turn off prescaler. */
		sio_setreg(com, com_mcr,
			   sio_getreg(com, com_mcr) & ~MCR_PRESCALE);
		sio_setreg(com, com_cfcr, CFCR_EFR_ENABLE);
		sio_setreg(com, com_efr, efr);
		sio_setreg(com, com_cfcr, cfcr);
	}

	/*
	 * Initialize the speed and the word size and wait long enough to
	 * drain the maximum of 16 bytes of junk in device output queues.
	 * The speed is undefined after a master reset and must be set
	 * before relying on anything related to output.  There may be
	 * junk after a (very fast) soft reboot and (apparently) after
	 * master reset.
	 * XXX what about the UART bug avoided by waiting in comparam()?
	 * We don't want to wait long enough to drain at 2 bps.
	 */
	if (iobase == siocniobase)
		DELAY((16 + 1) * 1000000 / (comdefaultrate / 10));
	else {
		sio_setreg(com, com_cfcr, CFCR_DLAB | CFCR_8BITS);
		divisor = siodivisor(rclk, SIO_TEST_SPEED);
		sio_setreg(com, com_dlbl, divisor & 0xff);
		sio_setreg(com, com_dlbh, divisor >> 8);
		sio_setreg(com, com_cfcr, CFCR_8BITS);
		DELAY((16 + 1) * 1000000 / (SIO_TEST_SPEED / 10));
	}

	/*
	 * Enable the interrupt gate and disable device interrupts.  This
	 * should leave the device driving the interrupt line low and
	 * guarantee an edge trigger if an interrupt can be generated.
	 */
/* EXTRA DELAY? */
	sio_setreg(com, com_mcr, mcr_image);
	sio_setreg(com, com_ier, 0);
	DELAY(1000);		/* XXX */
	irqmap[0] = isa_irq_pending();

	/*
	 * Attempt to set loopback mode so that we can send a null byte
	 * without annoying any external device.
	 */
/* EXTRA DELAY? */
	sio_setreg(com, com_mcr, mcr_image | MCR_LOOPBACK);

	/*
	 * Attempt to generate an output interrupt.  On 8250's, setting
	 * IER_ETXRDY generates an interrupt independent of the current
	 * setting and independent of whether the THR is empty.  On 16450's,
	 * setting IER_ETXRDY generates an interrupt independent of the
	 * current setting.  On 16550A's, setting IER_ETXRDY only
	 * generates an interrupt when IER_ETXRDY is not already set.
	 */
	sio_setreg(com, com_ier, IER_ETXRDY);

	/*
	 * On some 16x50 incompatibles, setting IER_ETXRDY doesn't generate
	 * an interrupt.  They'd better generate one for actually doing
	 * output.  Loopback may be broken on the same incompatibles but
	 * it's unlikely to do more than allow the null byte out.
	 */
	sio_setreg(com, com_data, 0);
	if (iobase == siocniobase)
		DELAY((1 + 2) * 1000000 / (comdefaultrate / 10));
	else
		DELAY((1 + 2) * 1000000 / (SIO_TEST_SPEED / 10));

	/*
	 * Turn off loopback mode so that the interrupt gate works again
	 * (MCR_IENABLE was hidden).  This should leave the device driving
	 * an interrupt line high.  It doesn't matter if the interrupt
	 * line oscillates while we are not looking at it, since interrupts
	 * are disabled.
	 */
/* EXTRA DELAY? */
	sio_setreg(com, com_mcr, mcr_image);
 
	/*
	 * It seems my Xircom CBEM56G Cardbus modem wants to be reset
	 * to 8 bits *again*, or else probe test 0 will fail.
	 * gwk@sgi.com, 4/19/2001
	 */
	sio_setreg(com, com_cfcr, CFCR_8BITS);

	/*
	 * Some PCMCIA cards (Palido 321s, DC-1S, ...) have the "TXRDY bug",
	 * so we probe for a buggy IIR_TXRDY implementation even in the
	 * noprobe case.  We don't probe for it in the !noprobe case because
	 * noprobe is always set for PCMCIA cards and the problem is not
	 * known to affect any other cards.
	 */
	if (noprobe) {
		/* Read IIR a few times. */
		for (fn = 0; fn < 2; fn ++) {
			DELAY(10000);
			failures[6] = sio_getreg(com, com_iir);
		}

		/* IIR_TXRDY should be clear.  Is it? */
		result = 0;
		if (failures[6] & IIR_TXRDY) {
			/*
			 * No.  We seem to have the bug.  Does our fix for
			 * it work?
			 */
			sio_setreg(com, com_ier, 0);
			if (sio_getreg(com, com_iir) & IIR_NOPEND) {
				/* Yes.  We discovered the TXRDY bug! */
				SET_FLAG(dev, COM_C_IIR_TXRDYBUG);
			} else {
				/* No.  Just fail.  XXX */
				result = ENXIO;
				sio_setreg(com, com_mcr, 0);
			}
		} else {
			/* Yes.  No bug. */
			CLR_FLAG(dev, COM_C_IIR_TXRDYBUG);
		}
		sio_setreg(com, com_ier, 0);
		sio_setreg(com, com_cfcr, CFCR_8BITS);
		mtx_unlock_spin(&sio_lock);
		bus_release_resource(dev, SYS_RES_IOPORT, rid, port);
		if (iobase == siocniobase)
			result = 0;
		/*
		 * XXX: Since we don't return 0, we shouldn't be relying on
		 * the softc that we set to persist to the call to attach
		 * since other probe routines may be called, and the malloc
		 * here causes subr_bus to not allocate anything for the
		 * other probes.  Instead, this softc is preserved and other
		 * probe routines can corrupt it.
		 */
		if (result != 0) {
			device_set_softc(dev, NULL);
			free(com, M_DEVBUF);
		}
		return (result == 0 ? BUS_PROBE_DEFAULT + 1 : result);
	}

	/*
	 * Check that
	 *	o the CFCR, IER and MCR in UART hold the values written to them
	 *	  (the values happen to be all distinct - this is good for
	 *	  avoiding false positive tests from bus echoes).
	 *	o an output interrupt is generated and its vector is correct.
	 *	o the interrupt goes away when the IIR in the UART is read.
	 */
/* EXTRA DELAY? */
	failures[0] = sio_getreg(com, com_cfcr) - CFCR_8BITS;
	failures[1] = sio_getreg(com, com_ier) - IER_ETXRDY;
	failures[2] = sio_getreg(com, com_mcr) - mcr_image;
	DELAY(10000);		/* Some internal modems need this time */
	irqmap[1] = isa_irq_pending();
	failures[4] = (sio_getreg(com, com_iir) & IIR_IMASK) - IIR_TXRDY;
	DELAY(1000);		/* XXX */
	irqmap[2] = isa_irq_pending();
	failures[6] = (sio_getreg(com, com_iir) & IIR_IMASK) - IIR_NOPEND;

	/*
	 * Turn off all device interrupts and check that they go off properly.
	 * Leave MCR_IENABLE alone.  For ports without a master port, it gates
	 * the OUT2 output of the UART to
	 * the ICU input.  Closing the gate would give a floating ICU input
	 * (unless there is another device driving it) and spurious interrupts.
	 * (On the system that this was first tested on, the input floats high
	 * and gives a (masked) interrupt as soon as the gate is closed.)
	 */
	sio_setreg(com, com_ier, 0);
	sio_setreg(com, com_cfcr, CFCR_8BITS);	/* dummy to avoid bus echo */
	failures[7] = sio_getreg(com, com_ier);
	DELAY(1000);		/* XXX */
	irqmap[3] = isa_irq_pending();
	failures[9] = (sio_getreg(com, com_iir) & IIR_IMASK) - IIR_NOPEND;

	mtx_unlock_spin(&sio_lock);

	irqs = irqmap[1] & ~irqmap[0];
	if (bus_get_resource(idev, SYS_RES_IRQ, 0, &xirq, NULL) == 0 &&
	    ((1 << xirq) & irqs) == 0) {
		printf(
		"sio%d: configured irq %ld not in bitmap of probed irqs %#x\n",
		    device_get_unit(dev), xirq, irqs);
		printf(
		"sio%d: port may not be enabled\n",
		    device_get_unit(dev));
	}
	if (bootverbose)
		printf("sio%d: irq maps: %#x %#x %#x %#x\n",
		    device_get_unit(dev),
		    irqmap[0], irqmap[1], irqmap[2], irqmap[3]);

	result = 0;
	for (fn = 0; fn < sizeof failures; ++fn)
		if (failures[fn]) {
			sio_setreg(com, com_mcr, 0);
			result = ENXIO;
			if (bootverbose) {
				printf("sio%d: probe failed test(s):",
				    device_get_unit(dev));
				for (fn = 0; fn < sizeof failures; ++fn)
					if (failures[fn])
						printf(" %d", fn);
				printf("\n");
			}
			break;
		}
	bus_release_resource(dev, SYS_RES_IOPORT, rid, port);
	if (iobase == siocniobase)
		result = 0;
	/*
	 * XXX: Since we don't return 0, we shouldn't be relying on the softc
	 * that we set to persist to the call to attach since other probe
	 * routines may be called, and the malloc here causes subr_bus to not
	 * allocate anything for the other probes.  Instead, this softc is
	 * preserved and other probe routines can corrupt it.
	 */
	if (result != 0) {
		device_set_softc(dev, NULL);
		free(com, M_DEVBUF);
	}
	return (result == 0 ? BUS_PROBE_DEFAULT + 1 : result);
}

#ifdef COM_ESP
static int
espattach(com, esp_port)
	struct com_s		*com;
	Port_t			esp_port;
{
	u_char	dips;
	u_char	val;

	/*
	 * Check the ESP-specific I/O port to see if we're an ESP
	 * card.  If not, return failure immediately.
	 */
	if ((inb(esp_port) & 0xf3) == 0) {
		printf(" port 0x%x is not an ESP board?\n", esp_port);
		return (0);
	}

	/*
	 * We've got something that claims to be a Hayes ESP card.
	 * Let's hope so.
	 */

	/* Get the dip-switch configuration */
	outb(esp_port + ESP_CMD1, ESP_GETDIPS);
	dips = inb(esp_port + ESP_STATUS1);

	/*
	 * Bits 0,1 of dips say which COM port we are.
	 */
	if (rman_get_start(com->ioportres) == likely_com_ports[dips & 0x03])
		printf(" : ESP");
	else {
		printf(" esp_port has com %d\n", dips & 0x03);
		return (0);
	}

	/*
	 * Check for ESP version 2.0 or later:  bits 4,5,6 = 010.
	 */
	outb(esp_port + ESP_CMD1, ESP_GETTEST);
	val = inb(esp_port + ESP_STATUS1);	/* clear reg 1 */
	val = inb(esp_port + ESP_STATUS2);
	if ((val & 0x70) < 0x20) {
		printf("-old (%o)", val & 0x70);
		return (0);
	}

	/*
	 * Check for ability to emulate 16550:  bit 7 == 1
	 */
	if ((dips & 0x80) == 0) {
		printf(" slave");
		return (0);
	}

	/*
	 * Okay, we seem to be a Hayes ESP card.  Whee.
	 */
	com->esp = TRUE;
	com->esp_port = esp_port;
	return (1);
}
#endif /* COM_ESP */

int
sioattach(dev, xrid, rclk)
	device_t	dev;
	int		xrid;
	u_long		rclk;
{
	struct com_s	*com;
#ifdef COM_ESP
	Port_t		*espp;
#endif
	Port_t		iobase;
	int		unit;
	u_int		flags;
	int		rid;
	struct resource *port;
	int		ret;
	int		error;
	struct tty	*tp;

	rid = xrid;
	port = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &rid,
					   IO_COMSIZE, RF_ACTIVE);
	if (!port)
		return (ENXIO);

	iobase = rman_get_start(port);
	unit = device_get_unit(dev);
	com = device_get_softc(dev);
	flags = device_get_flags(dev);

	if (unit >= sio_numunits)
		sio_numunits = unit + 1;
	/*
	 * sioprobe() has initialized the device registers as follows:
	 *	o cfcr = CFCR_8BITS.
	 *	  It is most important that CFCR_DLAB is off, so that the
	 *	  data port is not hidden when we enable interrupts.
	 *	o ier = 0.
	 *	  Interrupts are only enabled when the line is open.
	 *	o mcr = MCR_IENABLE, or 0 if the port has AST/4 compatible
	 *	  interrupt control register or the config specifies no irq.
	 *	  Keeping MCR_DTR and MCR_RTS off might stop the external
	 *	  device from sending before we are ready.
	 */
	bzero(com, sizeof *com);
	com->unit = unit;
	com->ioportres = port;
	com->ioportrid = rid;
	com->bst = rman_get_bustag(port);
	com->bsh = rman_get_bushandle(port);
	com->cfcr_image = CFCR_8BITS;
	com->loses_outints = COM_LOSESOUTINTS(flags) != 0;
	com->no_irq = bus_get_resource(dev, SYS_RES_IRQ, 0, NULL, NULL) != 0;
	com->tx_fifo_size = 1;
	com->obufs[0].l_head = com->obuf1;
	com->obufs[1].l_head = com->obuf2;

	com->data_port = iobase + com_data;
	com->int_ctl_port = iobase + com_ier;
	com->int_id_port = iobase + com_iir;
	com->modem_ctl_port = iobase + com_mcr;
	com->mcr_image = inb(com->modem_ctl_port);
	com->line_status_port = iobase + com_lsr;
	com->modem_status_port = iobase + com_msr;

	tp = com->tp = ttyalloc();
	tp->t_oproc = comstart;
	tp->t_param = comparam;
	tp->t_stop = comstop;
	tp->t_modem = commodem;
	tp->t_break = combreak;
	tp->t_close = comclose;
	tp->t_open = comopen;
	tp->t_sc = com;

	if (rclk == 0)
		rclk = DEFAULT_RCLK;
	com->rclk = rclk;

	if (unit == comconsole)
		ttyconsolemode(tp, comdefaultrate);
	error = siosetwater(com, tp->t_init_in.c_ispeed);
	mtx_unlock_spin(&sio_lock);
	if (error) {
		/*
		 * Leave i/o resources allocated if this is a `cn'-level
		 * console, so that other devices can't snarf them.
		 */
		if (iobase != siocniobase)
			bus_release_resource(dev, SYS_RES_IOPORT, rid, port);
		return (ENOMEM);
	}

	/* attempt to determine UART type */
	printf("sio%d: type", unit);

	if (!COM_ISMULTIPORT(flags) &&
	    !COM_IIR_TXRDYBUG(flags) && !COM_NOSCR(flags)) {
		u_char	scr;
		u_char	scr1;
		u_char	scr2;

		scr = sio_getreg(com, com_scr);
		sio_setreg(com, com_scr, 0xa5);
		scr1 = sio_getreg(com, com_scr);
		sio_setreg(com, com_scr, 0x5a);
		scr2 = sio_getreg(com, com_scr);
		sio_setreg(com, com_scr, scr);
		if (scr1 != 0xa5 || scr2 != 0x5a) {
			printf(" 8250 or not responding");
			goto determined_type;
		}
	}
	sio_setreg(com, com_fifo, FIFO_ENABLE | FIFO_RX_HIGH);
	DELAY(100);
	switch (inb(com->int_id_port) & IIR_FIFO_MASK) {
	case FIFO_RX_LOW:
		printf(" 16450");
		break;
	case FIFO_RX_MEDL:
		printf(" 16450?");
		break;
	case FIFO_RX_MEDH:
		printf(" 16550?");
		break;
	case FIFO_RX_HIGH:
		if (COM_NOFIFO(flags)) {
			printf(" 16550A fifo disabled");
			break;
		}
		com->hasfifo = TRUE;
		if (COM_ST16650A(flags)) {
			printf(" ST16650A");
			com->st16650a = TRUE;
			com->tx_fifo_size = 32;
			break;
		}
		if (COM_TI16754(flags)) {
			printf(" TI16754");
			com->tx_fifo_size = 64;
			break;
		}
		printf(" 16550A");
#ifdef COM_ESP
		for (espp = likely_esp_ports; *espp != 0; espp++)
			if (espattach(com, *espp)) {
				com->tx_fifo_size = 1024;
				break;
			}
		if (com->esp)
			break;
#endif
		com->tx_fifo_size = COM_FIFOSIZE(flags);
		if (com->tx_fifo_size == 0)
			com->tx_fifo_size = 16;
		else
			printf(" lookalike with %u bytes FIFO",
			       com->tx_fifo_size);
		break;
	}
#ifdef COM_ESP
	if (com->esp) {
		/*
		 * Set 16550 compatibility mode.
		 * We don't use the ESP_MODE_SCALE bit to increase the
		 * fifo trigger levels because we can't handle large
		 * bursts of input.
		 * XXX flow control should be set in comparam(), not here.
		 */
		outb(com->esp_port + ESP_CMD1, ESP_SETMODE);
		outb(com->esp_port + ESP_CMD2, ESP_MODE_RTS | ESP_MODE_FIFO);

		/* Set RTS/CTS flow control. */
		outb(com->esp_port + ESP_CMD1, ESP_SETFLOWTYPE);
		outb(com->esp_port + ESP_CMD2, ESP_FLOW_RTS);
		outb(com->esp_port + ESP_CMD2, ESP_FLOW_CTS);

		/* Set flow-control levels. */
		outb(com->esp_port + ESP_CMD1, ESP_SETRXFLOW);
		outb(com->esp_port + ESP_CMD2, HIBYTE(768));
		outb(com->esp_port + ESP_CMD2, LOBYTE(768));
		outb(com->esp_port + ESP_CMD2, HIBYTE(512));
		outb(com->esp_port + ESP_CMD2, LOBYTE(512));
	}
#endif /* COM_ESP */
	sio_setreg(com, com_fifo, 0);
determined_type: ;

#ifdef COM_MULTIPORT
	if (COM_ISMULTIPORT(flags)) {
		device_t masterdev;

		com->multiport = TRUE;
		printf(" (multiport");
		if (unit == COM_MPMASTER(flags))
			printf(" master");
		printf(")");
		masterdev = devclass_get_device(sio_devclass,
		    COM_MPMASTER(flags));
		com->no_irq = (masterdev == NULL || bus_get_resource(masterdev,
		    SYS_RES_IRQ, 0, NULL, NULL) != 0);
	 }
#endif /* COM_MULTIPORT */
	if (unit == comconsole)
		printf(", console");
	if (COM_IIR_TXRDYBUG(flags))
		printf(" with a buggy IIR_TXRDY implementation");
	printf("\n");

	if (sio_fast_ih == NULL) {
		swi_add(&tty_intr_event, "sio", siopoll, NULL, SWI_TTY, 0,
		    &sio_fast_ih);
		swi_add(&clk_intr_event, "sio", siopoll, NULL, SWI_CLOCK, 0,
		    &sio_slow_ih);
	}

	com->flags = flags;
	com->pps.ppscap = PPS_CAPTUREASSERT | PPS_CAPTURECLEAR;
	tp->t_pps = &com->pps;

	if (COM_PPSCTS(flags))
		com->pps_bit = MSR_CTS;
	else
		com->pps_bit = MSR_DCD;
	pps_init(&com->pps);

	rid = 0;
	com->irqres = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
	if (com->irqres) {
		ret = bus_setup_intr(dev, com->irqres,
				     INTR_TYPE_TTY,
				     siointr, NULL, com, 
				     &com->cookie);
		if (ret) {
			ret = bus_setup_intr(dev,
					     com->irqres, INTR_TYPE_TTY,
					     NULL, (driver_intr_t *)siointr, com, &com->cookie);
			if (ret == 0)
				device_printf(dev, "unable to activate interrupt in fast mode - using normal mode\n");
		}
		if (ret)
			device_printf(dev, "could not activate interrupt\n");
#if defined(KDB)
		/*
		 * Enable interrupts for early break-to-debugger support
		 * on the console.
		 */
		if (ret == 0 && unit == comconsole)
			outb(siocniobase + com_ier, IER_ERXRDY | IER_ERLS |
			    IER_EMSC);
#endif
	}

	/* We're ready, open the doors... */
	ttycreate(tp, TS_CALLOUT, "d%r", unit);

	return (0);
}

static int
comopen(struct tty *tp, struct cdev *dev)
{
	struct com_s	*com;
	int i;

	com = tp->t_sc;
	com->poll = com->no_irq;
	com->poll_output = com->loses_outints;
	if (com->hasfifo) {
		/*
		 * (Re)enable and drain fifos.
		 *
		 * Certain SMC chips cause problems if the fifos
		 * are enabled while input is ready.  Turn off the
		 * fifo if necessary to clear the input.  We test
		 * the input ready bit after enabling the fifos
		 * since we've already enabled them in comparam()
		 * and to handle races between enabling and fresh
		 * input.
		 */
		for (i = 0; i < 500; i++) {
			sio_setreg(com, com_fifo,
				   FIFO_RCV_RST | FIFO_XMT_RST
				   | com->fifo_image);
			/*
			 * XXX the delays are for superstitious
			 * historical reasons.  It must be less than
			 * the character time at the maximum
			 * supported speed (87 usec at 115200 bps
			 * 8N1).  Otherwise we might loop endlessly
			 * if data is streaming in.  We used to use
			 * delays of 100.  That usually worked
			 * because DELAY(100) used to usually delay
			 * for about 85 usec instead of 100.
			 */
			DELAY(50);
			if (!(inb(com->line_status_port) & LSR_RXRDY))
				break;
			sio_setreg(com, com_fifo, 0);
			DELAY(50);
			(void) inb(com->data_port);
		}
		if (i == 500)
			return (EIO);
	}

	mtx_lock_spin(&sio_lock);
	(void) inb(com->line_status_port);
	(void) inb(com->data_port);
	com->prev_modem_status = com->last_modem_status
	    = inb(com->modem_status_port);
	outb(com->int_ctl_port,
	     IER_ERXRDY | IER_ERLS | IER_EMSC
	     | (COM_IIR_TXRDYBUG(com->flags) ? 0 : IER_ETXRDY));
	mtx_unlock_spin(&sio_lock);
	siosettimeout();
	/* XXX: should be generic ? */
	if (com->prev_modem_status & MSR_DCD || ISCALLOUT(dev))
		ttyld_modem(tp, 1);
	return (0);
}

static void
comclose(tp)
	struct tty	*tp;
{
	int		s;
	struct com_s	*com;

	s = spltty();
	com = tp->t_sc;
	com->poll = FALSE;
	com->poll_output = FALSE;
	sio_setreg(com, com_cfcr, com->cfcr_image &= ~CFCR_SBREAK);

#if defined(KDB)
	/*
	 * Leave interrupts enabled and don't clear DTR if this is the
	 * console. This allows us to detect break-to-debugger events
	 * while the console device is closed.
	 */
	if (com->unit != comconsole)
#endif
	{
		sio_setreg(com, com_ier, 0);
		if (tp->t_cflag & HUPCL
		    /*
		     * XXX we will miss any carrier drop between here and the
		     * next open.  Perhaps we should watch DCD even when the
		     * port is closed; it is not sufficient to check it at
		     * the next open because it might go up and down while
		     * we're not watching.
		     */
		    || (!tp->t_actout
		        && !(com->prev_modem_status & MSR_DCD)
		        && !(tp->t_init_in.c_cflag & CLOCAL))
		    || !(tp->t_state & TS_ISOPEN)) {
			(void)commodem(tp, 0, SER_DTR);
			ttydtrwaitstart(tp);
		}
	}
	if (com->hasfifo) {
		/*
		 * Disable fifos so that they are off after controlled
		 * reboots.  Some BIOSes fail to detect 16550s when the
		 * fifos are enabled.
		 */
		sio_setreg(com, com_fifo, 0);
	}
	tp->t_actout = FALSE;
	wakeup(&tp->t_actout);
	wakeup(TSA_CARR_ON(tp));	/* restart any wopeners */
	siosettimeout();
	splx(s);
}

static void
siobusycheck(chan)
	void	*chan;
{
	struct com_s	*com;
	int		s;

	com = (struct com_s *)chan;

	/*
	 * Clear TS_BUSY if low-level output is complete.
	 * spl locking is sufficient because siointr1() does not set CS_BUSY.
	 * If siointr1() clears CS_BUSY after we look at it, then we'll get
	 * called again.  Reading the line status port outside of siointr1()
	 * is safe because CS_BUSY is clear so there are no output interrupts
	 * to lose.
	 */
	s = spltty();
	if (com->state & CS_BUSY)
		com->extra_state &= ~CSE_BUSYCHECK;	/* False alarm. */
	else if ((inb(com->line_status_port) & (LSR_TSRE | LSR_TXRDY))
	    == (LSR_TSRE | LSR_TXRDY)) {
		com->tp->t_state &= ~TS_BUSY;
		ttwwakeup(com->tp);
		com->extra_state &= ~CSE_BUSYCHECK;
	} else
		timeout(siobusycheck, com, hz / 100);
	splx(s);
}

static u_int
siodivisor(rclk, speed)
	u_long	rclk;
	speed_t	speed;
{
	long	actual_speed;
	u_int	divisor;
	int	error;

	if (speed == 0)
		return (0);
#if UINT_MAX > (ULONG_MAX - 1) / 8
	if (speed > (ULONG_MAX - 1) / 8)
		return (0);
#endif
	divisor = (rclk / (8UL * speed) + 1) / 2;
	if (divisor == 0 || divisor >= 65536)
		return (0);
	actual_speed = rclk / (16UL * divisor);

	/* 10 times error in percent: */
	error = ((actual_speed - (long)speed) * 2000 / (long)speed + 1) / 2;

	/* 3.0% maximum error tolerance: */
	if (error < -30 || error > 30)
		return (0);

	return (divisor);
}

/*
 * Call this function with the sio_lock mutex held.  It will return with the
 * lock still held.
 */
static void
sioinput(com)
	struct com_s	*com;
{
	u_char		*buf;
	int		incc;
	u_char		line_status;
	int		recv_data;
	struct tty	*tp;

	buf = com->ibuf;
	tp = com->tp;
	if (!(tp->t_state & TS_ISOPEN) || !(tp->t_cflag & CREAD)) {
		com_events -= (com->iptr - com->ibuf);
		com->iptr = com->ibuf;
		return;
	}
	if (tp->t_state & TS_CAN_BYPASS_L_RINT) {
		/*
		 * Avoid the grotesquely inefficient lineswitch routine
		 * (ttyinput) in "raw" mode.  It usually takes about 450
		 * instructions (that's without canonical processing or echo!).
		 * slinput is reasonably fast (usually 40 instructions plus
		 * call overhead).
		 */
		do {
			/*
			 * This may look odd, but it is using save-and-enable
			 * semantics instead of the save-and-disable semantics
			 * that are used everywhere else.
			 */
			mtx_unlock_spin(&sio_lock);
			incc = com->iptr - buf;
			if (tp->t_rawq.c_cc + incc > tp->t_ihiwat
			    && (com->state & CS_RTS_IFLOW
				|| tp->t_iflag & IXOFF)
			    && !(tp->t_state & TS_TBLOCK))
				ttyblock(tp);
			com->delta_error_counts[CE_TTY_BUF_OVERFLOW]
				+= b_to_q((char *)buf, incc, &tp->t_rawq);
			buf += incc;
			tk_nin += incc;
			tk_rawcc += incc;
			tp->t_rawcc += incc;
			ttwakeup(tp);
			if (tp->t_state & TS_TTSTOP
			    && (tp->t_iflag & IXANY
				|| tp->t_cc[VSTART] == tp->t_cc[VSTOP])) {
				tp->t_state &= ~TS_TTSTOP;
				tp->t_lflag &= ~FLUSHO;
				comstart(tp);
			}
			mtx_lock_spin(&sio_lock);
		} while (buf < com->iptr);
	} else {
		do {
			/*
			 * This may look odd, but it is using save-and-enable
			 * semantics instead of the save-and-disable semantics
			 * that are used everywhere else.
			 */
			mtx_unlock_spin(&sio_lock);
			line_status = buf[com->ierroff];
			recv_data = *buf++;
			if (line_status
			    & (LSR_BI | LSR_FE | LSR_OE | LSR_PE)) {
				if (line_status & LSR_BI)
					recv_data |= TTY_BI;
				if (line_status & LSR_FE)
					recv_data |= TTY_FE;
				if (line_status & LSR_OE)
					recv_data |= TTY_OE;
				if (line_status & LSR_PE)
					recv_data |= TTY_PE;
			}
			ttyld_rint(tp, recv_data);
			mtx_lock_spin(&sio_lock);
		} while (buf < com->iptr);
	}
	com_events -= (com->iptr - com->ibuf);
	com->iptr = com->ibuf;

	/*
	 * There is now room for another low-level buffer full of input,
	 * so enable RTS if it is now disabled and there is room in the
	 * high-level buffer.
	 */
	if ((com->state & CS_RTS_IFLOW) && !(com->mcr_image & MCR_RTS) &&
	    !(tp->t_state & TS_TBLOCK))
		outb(com->modem_ctl_port, com->mcr_image |= MCR_RTS);
}

static int
siointr(arg)
	void		*arg;
{
	struct com_s	*com;

#ifndef COM_MULTIPORT
	com = (struct com_s *)arg;

	mtx_lock_spin(&sio_lock);
	siointr1(com);
	mtx_unlock_spin(&sio_lock);
#else /* COM_MULTIPORT */
	bool_t		possibly_more_intrs;
	int		unit;

	/*
	 * Loop until there is no activity on any port.  This is necessary
	 * to get an interrupt edge more than to avoid another interrupt.
	 * If the IRQ signal is just an OR of the IRQ signals from several
	 * devices, then the edge from one may be lost because another is
	 * on.
	 */
	mtx_lock_spin(&sio_lock);
	do {
		possibly_more_intrs = FALSE;
		for (unit = 0; unit < sio_numunits; ++unit) {
			com = com_addr(unit);
			/*
			 * XXX COM_LOCK();
			 * would it work here, or be counter-productive?
			 */
			if (com != NULL 
			    && !com->gone
			    && (inb(com->int_id_port) & IIR_IMASK)
			       != IIR_NOPEND) {
				siointr1(com);
				possibly_more_intrs = TRUE;
			}
			/* XXX COM_UNLOCK(); */
		}
	} while (possibly_more_intrs);
	mtx_unlock_spin(&sio_lock);
#endif /* COM_MULTIPORT */
	return(FILTER_HANDLED);
}

static struct timespec siots[8];
static int siotso;
static int volatile siotsunit = -1;

static int
sysctl_siots(SYSCTL_HANDLER_ARGS)
{
	char buf[128];
	long long delta;
	size_t len;
	int error, i, tso;

	for (i = 1, tso = siotso; i < tso; i++) {
		delta = (long long)(siots[i].tv_sec - siots[i - 1].tv_sec) *
		    1000000000 +
		    (siots[i].tv_nsec - siots[i - 1].tv_nsec);
		len = sprintf(buf, "%lld\n", delta);
		if (delta >= 110000)
			len += sprintf(buf + len - 1, ": *** %ld.%09ld\n",
			    (long)siots[i].tv_sec, siots[i].tv_nsec) - 1;
		if (i == tso - 1)
			buf[len - 1] = '\0';
		error = SYSCTL_OUT(req, buf, len);
		if (error != 0)
			return (error);
	}
	return (0);
}

SYSCTL_PROC(_machdep, OID_AUTO, siots, CTLTYPE_STRING | CTLFLAG_RD,
    0, 0, sysctl_siots, "A", "sio timestamps");

static void
siointr1(com)
	struct com_s	*com;
{
	u_char	int_ctl;
	u_char	int_ctl_new;
	u_char	line_status;
	u_char	modem_status;
	u_char	*ioptr;
	u_char	recv_data;

#ifdef KDB
again:
#endif

	if (COM_IIR_TXRDYBUG(com->flags)) {
		int_ctl = inb(com->int_ctl_port);
		int_ctl_new = int_ctl;
	} else {
		int_ctl = 0;
		int_ctl_new = 0;
	}

	while (!com->gone) {
		if (com->pps.ppsparam.mode & PPS_CAPTUREBOTH) {
			modem_status = inb(com->modem_status_port);
		        if ((modem_status ^ com->last_modem_status) &
			    com->pps_bit) {
				pps_capture(&com->pps);
				pps_event(&com->pps,
				    (modem_status & com->pps_bit) ? 
				    PPS_CAPTUREASSERT : PPS_CAPTURECLEAR);
			}
		}
		line_status = inb(com->line_status_port);

		/* input event? (check first to help avoid overruns) */
		while (line_status & LSR_RCV_MASK) {
			/* break/unnattached error bits or real input? */
			if (!(line_status & LSR_RXRDY))
				recv_data = 0;
			else
				recv_data = inb(com->data_port);
#ifdef KDB
			if (com->unit == comconsole &&
			    kdb_alt_break(recv_data, &com->alt_brk_state) != 0)
				goto again;
#endif /* KDB */
			if (line_status & (LSR_BI | LSR_FE | LSR_PE)) {
				/*
				 * Don't store BI if IGNBRK or FE/PE if IGNPAR.
				 * Otherwise, push the work to a higher level
				 * (to handle PARMRK) if we're bypassing.
				 * Otherwise, convert BI/FE and PE+INPCK to 0.
				 *
				 * This makes bypassing work right in the
				 * usual "raw" case (IGNBRK set, and IGNPAR
				 * and INPCK clear).
				 *
				 * Note: BI together with FE/PE means just BI.
				 */
				if (line_status & LSR_BI) {
#if defined(KDB)
					if (com->unit == comconsole) {
						kdb_break();
						goto cont;
					}
#endif
					if (com->tp == NULL
					    || com->tp->t_iflag & IGNBRK)
						goto cont;
				} else {
					if (com->tp == NULL
					    || com->tp->t_iflag & IGNPAR)
						goto cont;
				}
				if (com->tp->t_state & TS_CAN_BYPASS_L_RINT
				    && (line_status & (LSR_BI | LSR_FE)
					|| com->tp->t_iflag & INPCK))
					recv_data = 0;
			}
			++com->bytes_in;
			if (com->tp != NULL &&
			    com->tp->t_hotchar != 0 && recv_data == com->tp->t_hotchar)
				swi_sched(sio_fast_ih, 0);
			ioptr = com->iptr;
			if (ioptr >= com->ibufend)
				CE_RECORD(com, CE_INTERRUPT_BUF_OVERFLOW);
			else {
				if (com->tp != NULL && com->tp->t_do_timestamp)
					microtime(&com->tp->t_timestamp);
				++com_events;
				swi_sched(sio_slow_ih, SWI_DELAY);
#if 0 /* for testing input latency vs efficiency */
if (com->iptr - com->ibuf == 8)
	swi_sched(sio_fast_ih, 0);
#endif
				ioptr[0] = recv_data;
				ioptr[com->ierroff] = line_status;
				com->iptr = ++ioptr;
				if (ioptr == com->ihighwater
				    && com->state & CS_RTS_IFLOW)
					outb(com->modem_ctl_port,
					     com->mcr_image &= ~MCR_RTS);
				if (line_status & LSR_OE)
					CE_RECORD(com, CE_OVERRUN);
			}
cont:
			if (line_status & LSR_TXRDY
			    && com->state >= (CS_BUSY | CS_TTGO | CS_ODEVREADY))
				goto txrdy;

			/*
			 * "& 0x7F" is to avoid the gcc-1.40 generating a slow
			 * jump from the top of the loop to here
			 */
			line_status = inb(com->line_status_port) & 0x7F;
		}

		/* modem status change? (always check before doing output) */
		modem_status = inb(com->modem_status_port);
		if (modem_status != com->last_modem_status) {
			/*
			 * Schedule high level to handle DCD changes.  Note
			 * that we don't use the delta bits anywhere.  Some
			 * UARTs mess them up, and it's easy to remember the
			 * previous bits and calculate the delta.
			 */
			com->last_modem_status = modem_status;
			if (!(com->state & CS_CHECKMSR)) {
				com_events += LOTS_OF_EVENTS;
				com->state |= CS_CHECKMSR;
				swi_sched(sio_fast_ih, 0);
			}

			/* handle CTS change immediately for crisp flow ctl */
			if (com->state & CS_CTS_OFLOW) {
				if (modem_status & MSR_CTS)
					com->state |= CS_ODEVREADY;
				else
					com->state &= ~CS_ODEVREADY;
			}
		}

txrdy:
		/* output queued and everything ready? */
		if (line_status & LSR_TXRDY
		    && com->state >= (CS_BUSY | CS_TTGO | CS_ODEVREADY)) {
			ioptr = com->obufq.l_head;
			if (com->tx_fifo_size > 1 && com->unit != siotsunit) {
				u_int	ocount;

				ocount = com->obufq.l_tail - ioptr;
				if (ocount > com->tx_fifo_size)
					ocount = com->tx_fifo_size;
				com->bytes_out += ocount;
				do
					outb(com->data_port, *ioptr++);
				while (--ocount != 0);
			} else {
				outb(com->data_port, *ioptr++);
				++com->bytes_out;
				if (com->unit == siotsunit
				    && siotso < nitems(siots))
					nanouptime(&siots[siotso++]);
			}
			com->obufq.l_head = ioptr;
			if (COM_IIR_TXRDYBUG(com->flags))
				int_ctl_new = int_ctl | IER_ETXRDY;
			if (ioptr >= com->obufq.l_tail) {
				struct lbq	*qp;

				qp = com->obufq.l_next;
				qp->l_queued = FALSE;
				qp = qp->l_next;
				if (qp != NULL) {
					com->obufq.l_head = qp->l_head;
					com->obufq.l_tail = qp->l_tail;
					com->obufq.l_next = qp;
				} else {
					/* output just completed */
					if (COM_IIR_TXRDYBUG(com->flags))
						int_ctl_new = int_ctl
							      & ~IER_ETXRDY;
					com->state &= ~CS_BUSY;
				}
				if (!(com->state & CS_ODONE)) {
					com_events += LOTS_OF_EVENTS;
					com->state |= CS_ODONE;
					/* handle at high level ASAP */
					swi_sched(sio_fast_ih, 0);
				}
			}
			if (COM_IIR_TXRDYBUG(com->flags)
			    && int_ctl != int_ctl_new)
				outb(com->int_ctl_port, int_ctl_new);
		}

		/* finished? */
#ifndef COM_MULTIPORT
		if ((inb(com->int_id_port) & IIR_IMASK) == IIR_NOPEND)
#endif /* COM_MULTIPORT */
			return;
	}
}

/* software interrupt handler for SWI_TTY */
static void
siopoll(void *dummy)
{
	int		unit;

	if (com_events == 0)
		return;
repeat:
	for (unit = 0; unit < sio_numunits; ++unit) {
		struct com_s	*com;
		int		incc;
		struct tty	*tp;

		com = com_addr(unit);
		if (com == NULL)
			continue;
		tp = com->tp;
		if (tp == NULL || com->gone) {
			/*
			 * Discard any events related to never-opened or
			 * going-away devices.
			 */
			mtx_lock_spin(&sio_lock);
			incc = com->iptr - com->ibuf;
			com->iptr = com->ibuf;
			if (com->state & CS_CHECKMSR) {
				incc += LOTS_OF_EVENTS;
				com->state &= ~CS_CHECKMSR;
			}
			com_events -= incc;
			mtx_unlock_spin(&sio_lock);
			continue;
		}
		if (com->iptr != com->ibuf) {
			mtx_lock_spin(&sio_lock);
			sioinput(com);
			mtx_unlock_spin(&sio_lock);
		}
		if (com->state & CS_CHECKMSR) {
			u_char	delta_modem_status;

			mtx_lock_spin(&sio_lock);
			delta_modem_status = com->last_modem_status
					     ^ com->prev_modem_status;
			com->prev_modem_status = com->last_modem_status;
			com_events -= LOTS_OF_EVENTS;
			com->state &= ~CS_CHECKMSR;
			mtx_unlock_spin(&sio_lock);
			if (delta_modem_status & MSR_DCD)
				ttyld_modem(tp,
				    com->prev_modem_status & MSR_DCD);
		}
		if (com->state & CS_ODONE) {
			mtx_lock_spin(&sio_lock);
			com_events -= LOTS_OF_EVENTS;
			com->state &= ~CS_ODONE;
			mtx_unlock_spin(&sio_lock);
			if (!(com->state & CS_BUSY)
			    && !(com->extra_state & CSE_BUSYCHECK)) {
				timeout(siobusycheck, com, hz / 100);
				com->extra_state |= CSE_BUSYCHECK;
			}
			ttyld_start(tp);
		}
		if (com_events == 0)
			break;
	}
	if (com_events >= LOTS_OF_EVENTS)
		goto repeat;
}

static void
combreak(tp, sig)
	struct tty 	*tp;
	int		sig;
{
	struct com_s	*com;

	com = tp->t_sc;

	if (sig)
		sio_setreg(com, com_cfcr, com->cfcr_image |= CFCR_SBREAK);
	else
		sio_setreg(com, com_cfcr, com->cfcr_image &= ~CFCR_SBREAK);
}

static int
comparam(tp, t)
	struct tty	*tp;
	struct termios	*t;
{
	u_int		cfcr;
	int		cflag;
	struct com_s	*com;
	u_int		divisor;
	u_char		dlbh;
	u_char		dlbl;
	u_char		efr_flowbits;
	int		s;

	com = tp->t_sc;
	if (com == NULL)
		return (ENODEV);

	/* check requested parameters */
	if (t->c_ispeed != (t->c_ospeed != 0 ? t->c_ospeed : tp->t_ospeed))
		return (EINVAL);
	divisor = siodivisor(com->rclk, t->c_ispeed);
	if (divisor == 0)
		return (EINVAL);

	/* parameters are OK, convert them to the com struct and the device */
	s = spltty();
	if (t->c_ospeed == 0)
		(void)commodem(tp, 0, SER_DTR);	/* hang up line */
	else
		(void)commodem(tp, SER_DTR, 0);
	cflag = t->c_cflag;
	switch (cflag & CSIZE) {
	case CS5:
		cfcr = CFCR_5BITS;
		break;
	case CS6:
		cfcr = CFCR_6BITS;
		break;
	case CS7:
		cfcr = CFCR_7BITS;
		break;
	default:
		cfcr = CFCR_8BITS;
		break;
	}
	if (cflag & PARENB) {
		cfcr |= CFCR_PENAB;
		if (!(cflag & PARODD))
			cfcr |= CFCR_PEVEN;
	}
	if (cflag & CSTOPB)
		cfcr |= CFCR_STOPB;

	if (com->hasfifo) {
		/*
		 * Use a fifo trigger level low enough so that the input
		 * latency from the fifo is less than about 16 msec and
		 * the total latency is less than about 30 msec.  These
		 * latencies are reasonable for humans.  Serial comms
		 * protocols shouldn't expect anything better since modem
		 * latencies are larger.
		 *
		 * The fifo trigger level cannot be set at RX_HIGH for high
		 * speed connections without further work on reducing 
		 * interrupt disablement times in other parts of the system,
		 * without producing silo overflow errors.
		 */
		com->fifo_image = com->unit == siotsunit ? 0
				  : t->c_ispeed <= 4800
				  ? FIFO_ENABLE : FIFO_ENABLE | FIFO_RX_MEDH;
#ifdef COM_ESP
		/*
		 * The Hayes ESP card needs the fifo DMA mode bit set
		 * in compatibility mode.  If not, it will interrupt
		 * for each character received.
		 */
		if (com->esp)
			com->fifo_image |= FIFO_DMA_MODE;
#endif
		sio_setreg(com, com_fifo, com->fifo_image);
	}

	/*
	 * This returns with interrupts disabled so that we can complete
	 * the speed change atomically.  Keeping interrupts disabled is
	 * especially important while com_data is hidden.
	 */
	(void) siosetwater(com, t->c_ispeed);

	sio_setreg(com, com_cfcr, cfcr | CFCR_DLAB);
	/*
	 * Only set the divisor registers if they would change, since on
	 * some 16550 incompatibles (UMC8669F), setting them while input
	 * is arriving loses sync until data stops arriving.
	 */
	dlbl = divisor & 0xFF;
	if (sio_getreg(com, com_dlbl) != dlbl)
		sio_setreg(com, com_dlbl, dlbl);
	dlbh = divisor >> 8;
	if (sio_getreg(com, com_dlbh) != dlbh)
		sio_setreg(com, com_dlbh, dlbh);

	efr_flowbits = 0;

	if (cflag & CRTS_IFLOW) {
		com->state |= CS_RTS_IFLOW;
		efr_flowbits |= EFR_AUTORTS;
		/*
		 * If CS_RTS_IFLOW just changed from off to on, the change
		 * needs to be propagated to MCR_RTS.  This isn't urgent,
		 * so do it later by calling comstart() instead of repeating
		 * a lot of code from comstart() here.
		 */
	} else if (com->state & CS_RTS_IFLOW) {
		com->state &= ~CS_RTS_IFLOW;
		/*
		 * CS_RTS_IFLOW just changed from on to off.  Force MCR_RTS
		 * on here, since comstart() won't do it later.
		 */
		outb(com->modem_ctl_port, com->mcr_image |= MCR_RTS);
	}

	/*
	 * Set up state to handle output flow control.
	 * XXX - worth handling MDMBUF (DCD) flow control at the lowest level?
	 * Now has 10+ msec latency, while CTS flow has 50- usec latency.
	 */
	com->state |= CS_ODEVREADY;
	com->state &= ~CS_CTS_OFLOW;
	if (cflag & CCTS_OFLOW) {
		com->state |= CS_CTS_OFLOW;
		efr_flowbits |= EFR_AUTOCTS;
		if (!(com->last_modem_status & MSR_CTS))
			com->state &= ~CS_ODEVREADY;
	}

	if (com->st16650a) {
		sio_setreg(com, com_lcr, LCR_EFR_ENABLE);
		sio_setreg(com, com_efr,
			   (sio_getreg(com, com_efr)
			    & ~(EFR_AUTOCTS | EFR_AUTORTS)) | efr_flowbits);
	}
	sio_setreg(com, com_cfcr, com->cfcr_image = cfcr);

	/* XXX shouldn't call functions while intrs are disabled. */
	ttyldoptim(tp);

	mtx_unlock_spin(&sio_lock);
	splx(s);
	comstart(tp);
	if (com->ibufold != NULL) {
		free(com->ibufold, M_DEVBUF);
		com->ibufold = NULL;
	}
	return (0);
}

/*
 * This function must be called with the sio_lock mutex released and will
 * return with it obtained.
 */
static int
siosetwater(com, speed)
	struct com_s	*com;
	speed_t		speed;
{
	int		cp4ticks;
	u_char		*ibuf;
	int		ibufsize;
	struct tty	*tp;

	/*
	 * Make the buffer size large enough to handle a softtty interrupt
	 * latency of about 2 ticks without loss of throughput or data
	 * (about 3 ticks if input flow control is not used or not honoured,
	 * but a bit less for CS5-CS7 modes).
	 */
	cp4ticks = speed / 10 / hz * 4;
	for (ibufsize = 128; ibufsize < cp4ticks;)
		ibufsize <<= 1;
	if (ibufsize == com->ibufsize) {
		mtx_lock_spin(&sio_lock);
		return (0);
	}

	/*
	 * Allocate input buffer.  The extra factor of 2 in the size is
	 * to allow for an error byte for each input byte.
	 */
	ibuf = malloc(2 * ibufsize, M_DEVBUF, M_NOWAIT);
	if (ibuf == NULL) {
		mtx_lock_spin(&sio_lock);
		return (ENOMEM);
	}

	/* Initialize non-critical variables. */
	com->ibufold = com->ibuf;
	com->ibufsize = ibufsize;
	tp = com->tp;
	if (tp != NULL) {
		tp->t_ififosize = 2 * ibufsize;
		tp->t_ispeedwat = (speed_t)-1;
		tp->t_ospeedwat = (speed_t)-1;
	}

	/*
	 * Read current input buffer, if any.  Continue with interrupts
	 * disabled.
	 */
	mtx_lock_spin(&sio_lock);
	if (com->iptr != com->ibuf)
		sioinput(com);

	/*-
	 * Initialize critical variables, including input buffer watermarks.
	 * The external device is asked to stop sending when the buffer
	 * exactly reaches high water, or when the high level requests it.
	 * The high level is notified immediately (rather than at a later
	 * clock tick) when this watermark is reached.
	 * The buffer size is chosen so the watermark should almost never
	 * be reached.
	 * The low watermark is invisibly 0 since the buffer is always
	 * emptied all at once.
	 */
	com->iptr = com->ibuf = ibuf;
	com->ibufend = ibuf + ibufsize;
	com->ierroff = ibufsize;
	com->ihighwater = ibuf + 3 * ibufsize / 4;
	return (0);
}

static void
comstart(tp)
	struct tty	*tp;
{
	struct com_s	*com;
	int		s;

	com = tp->t_sc;
	if (com == NULL)
		return;
	s = spltty();
	mtx_lock_spin(&sio_lock);
	if (tp->t_state & TS_TTSTOP)
		com->state &= ~CS_TTGO;
	else
		com->state |= CS_TTGO;
	if (tp->t_state & TS_TBLOCK) {
		if (com->mcr_image & MCR_RTS && com->state & CS_RTS_IFLOW)
			outb(com->modem_ctl_port, com->mcr_image &= ~MCR_RTS);
	} else {
		if (!(com->mcr_image & MCR_RTS) && com->iptr < com->ihighwater
		    && com->state & CS_RTS_IFLOW)
			outb(com->modem_ctl_port, com->mcr_image |= MCR_RTS);
	}
	mtx_unlock_spin(&sio_lock);
	if (tp->t_state & (TS_TIMEOUT | TS_TTSTOP)) {
		ttwwakeup(tp);
		splx(s);
		return;
	}
	if (tp->t_outq.c_cc != 0) {
		struct lbq	*qp;
		struct lbq	*next;

		if (!com->obufs[0].l_queued) {
			com->obufs[0].l_tail
			    = com->obuf1 + q_to_b(&tp->t_outq, com->obuf1,
						  sizeof com->obuf1);
			com->obufs[0].l_next = NULL;
			com->obufs[0].l_queued = TRUE;
			mtx_lock_spin(&sio_lock);
			if (com->state & CS_BUSY) {
				qp = com->obufq.l_next;
				while ((next = qp->l_next) != NULL)
					qp = next;
				qp->l_next = &com->obufs[0];
			} else {
				com->obufq.l_head = com->obufs[0].l_head;
				com->obufq.l_tail = com->obufs[0].l_tail;
				com->obufq.l_next = &com->obufs[0];
				com->state |= CS_BUSY;
			}
			mtx_unlock_spin(&sio_lock);
		}
		if (tp->t_outq.c_cc != 0 && !com->obufs[1].l_queued) {
			com->obufs[1].l_tail
			    = com->obuf2 + q_to_b(&tp->t_outq, com->obuf2,
						  sizeof com->obuf2);
			com->obufs[1].l_next = NULL;
			com->obufs[1].l_queued = TRUE;
			mtx_lock_spin(&sio_lock);
			if (com->state & CS_BUSY) {
				qp = com->obufq.l_next;
				while ((next = qp->l_next) != NULL)
					qp = next;
				qp->l_next = &com->obufs[1];
			} else {
				com->obufq.l_head = com->obufs[1].l_head;
				com->obufq.l_tail = com->obufs[1].l_tail;
				com->obufq.l_next = &com->obufs[1];
				com->state |= CS_BUSY;
			}
			mtx_unlock_spin(&sio_lock);
		}
		tp->t_state |= TS_BUSY;
	}
	mtx_lock_spin(&sio_lock);
	if (com->state >= (CS_BUSY | CS_TTGO))
		siointr1(com);	/* fake interrupt to start output */
	mtx_unlock_spin(&sio_lock);
	ttwwakeup(tp);
	splx(s);
}

static void
comstop(tp, rw)
	struct tty	*tp;
	int		rw;
{
	struct com_s	*com;

	com = tp->t_sc;
	if (com == NULL || com->gone)
		return;
	mtx_lock_spin(&sio_lock);
	if (rw & FWRITE) {
		if (com->hasfifo)
#ifdef COM_ESP
		    /* XXX avoid h/w bug. */
		    if (!com->esp)
#endif
			sio_setreg(com, com_fifo,
				   FIFO_XMT_RST | com->fifo_image);
		com->obufs[0].l_queued = FALSE;
		com->obufs[1].l_queued = FALSE;
		if (com->state & CS_ODONE)
			com_events -= LOTS_OF_EVENTS;
		com->state &= ~(CS_ODONE | CS_BUSY);
		com->tp->t_state &= ~TS_BUSY;
	}
	if (rw & FREAD) {
		if (com->hasfifo)
#ifdef COM_ESP
		    /* XXX avoid h/w bug. */
		    if (!com->esp)
#endif
			sio_setreg(com, com_fifo,
				   FIFO_RCV_RST | com->fifo_image);
		com_events -= (com->iptr - com->ibuf);
		com->iptr = com->ibuf;
	}
	mtx_unlock_spin(&sio_lock);
	comstart(tp);
}

static int
commodem(struct tty *tp, int sigon, int sigoff)
{
	struct com_s	*com;
	int	bitand, bitor, msr;

	com = tp->t_sc;
	if (com->gone)
		return(0);
	if (sigon != 0 || sigoff != 0) {
		bitand = bitor = 0;
		if (sigoff & SER_DTR)
			bitand |= MCR_DTR;
		if (sigoff & SER_RTS)
			bitand |= MCR_RTS;
		if (sigon & SER_DTR)
			bitor |= MCR_DTR;
		if (sigon & SER_RTS)
			bitor |= MCR_RTS;
		bitand = ~bitand;
		mtx_lock_spin(&sio_lock);
		com->mcr_image &= bitand;
		com->mcr_image |= bitor;
		outb(com->modem_ctl_port, com->mcr_image);
		mtx_unlock_spin(&sio_lock);
		return (0);
	} else {
		bitor = 0;
		if (com->mcr_image & MCR_DTR)
			bitor |= SER_DTR;
		if (com->mcr_image & MCR_RTS)
			bitor |= SER_RTS;
		msr = com->prev_modem_status;
		if (msr & MSR_CTS)
			bitor |= SER_CTS;
		if (msr & MSR_DCD)
			bitor |= SER_DCD;
		if (msr & MSR_DSR)
			bitor |= SER_DSR;
		if (msr & MSR_DSR)
			bitor |= SER_DSR;
		if (msr & (MSR_RI | MSR_TERI))
			bitor |= SER_RI;
		return (bitor);
	}
}

static void
siosettimeout()
{
	struct com_s	*com;
	bool_t		someopen;
	int		unit;

	/*
	 * Set our timeout period to 1 second if no polled devices are open.
	 * Otherwise set it to max(1/200, 1/hz).
	 * Enable timeouts iff some device is open.
	 */
	untimeout(comwakeup, (void *)NULL, sio_timeout_handle);
	sio_timeout = hz;
	someopen = FALSE;
	for (unit = 0; unit < sio_numunits; ++unit) {
		com = com_addr(unit);
		if (com != NULL && com->tp != NULL
		    && com->tp->t_state & TS_ISOPEN && !com->gone) {
			someopen = TRUE;
			if (com->poll || com->poll_output) {
				sio_timeout = hz > 200 ? hz / 200 : 1;
				break;
			}
		}
	}
	if (someopen) {
		sio_timeouts_until_log = hz / sio_timeout;
		sio_timeout_handle = timeout(comwakeup, (void *)NULL,
					     sio_timeout);
	} else {
		/* Flush error messages, if any. */
		sio_timeouts_until_log = 1;
		comwakeup((void *)NULL);
		untimeout(comwakeup, (void *)NULL, sio_timeout_handle);
	}
}

static void
comwakeup(chan)
	void	*chan;
{
	struct com_s	*com;
	int		unit;

	sio_timeout_handle = timeout(comwakeup, (void *)NULL, sio_timeout);

	/*
	 * Recover from lost output interrupts.
	 * Poll any lines that don't use interrupts.
	 */
	for (unit = 0; unit < sio_numunits; ++unit) {
		com = com_addr(unit);
		if (com != NULL && !com->gone
		    && (com->state >= (CS_BUSY | CS_TTGO) || com->poll)) {
			mtx_lock_spin(&sio_lock);
			siointr1(com);
			mtx_unlock_spin(&sio_lock);
		}
	}

	/*
	 * Check for and log errors, but not too often.
	 */
	if (--sio_timeouts_until_log > 0)
		return;
	sio_timeouts_until_log = hz / sio_timeout;
	for (unit = 0; unit < sio_numunits; ++unit) {
		int	errnum;

		com = com_addr(unit);
		if (com == NULL)
			continue;
		if (com->gone)
			continue;
		for (errnum = 0; errnum < CE_NTYPES; ++errnum) {
			u_int	delta;
			u_long	total;

			mtx_lock_spin(&sio_lock);
			delta = com->delta_error_counts[errnum];
			com->delta_error_counts[errnum] = 0;
			mtx_unlock_spin(&sio_lock);
			if (delta == 0)
				continue;
			total = com->error_counts[errnum] += delta;
			log(LOG_ERR, "sio%d: %u more %s%s (total %lu)\n",
			    unit, delta, error_desc[errnum],
			    delta == 1 ? "" : "s", total);
		}
	}
}

/*
 * Following are all routines needed for SIO to act as console
 */
struct siocnstate {
	u_char	dlbl;
	u_char	dlbh;
	u_char	ier;
	u_char	cfcr;
	u_char	mcr;
};

/*
 * This is a function in order to not replicate "ttyd%d" more
 * places than absolutely necessary.
 */
static void
siocnset(struct consdev *cd, int unit)
{

	cd->cn_unit = unit;
	sprintf(cd->cn_name, "ttyd%d", unit);
}

static speed_t siocngetspeed(Port_t, u_long rclk);
static void siocnclose(struct siocnstate *sp, Port_t iobase);
static void siocnopen(struct siocnstate *sp, Port_t iobase, int speed);
static void siocntxwait(Port_t iobase);

static cn_probe_t sio_cnprobe;
static cn_init_t sio_cninit;
static cn_term_t sio_cnterm;
static cn_getc_t sio_cngetc;
static cn_putc_t sio_cnputc;
static cn_grab_t sio_cngrab;
static cn_ungrab_t sio_cnungrab;

CONSOLE_DRIVER(sio);

static void
siocntxwait(iobase)
	Port_t	iobase;
{
	int	timo;

	/*
	 * Wait for any pending transmission to finish.  Required to avoid
	 * the UART lockup bug when the speed is changed, and for normal
	 * transmits.
	 */
	timo = 100000;
	while ((inb(iobase + com_lsr) & (LSR_TSRE | LSR_TXRDY))
	       != (LSR_TSRE | LSR_TXRDY) && --timo != 0)
		;
}

/*
 * Read the serial port specified and try to figure out what speed
 * it's currently running at.  We're assuming the serial port has
 * been initialized and is basically idle.  This routine is only intended
 * to be run at system startup.
 *
 * If the value read from the serial port doesn't make sense, return 0.
 */

static speed_t
siocngetspeed(iobase, rclk)
	Port_t	iobase;
	u_long	rclk;
{
	u_int	divisor;
	u_char	dlbh;
	u_char	dlbl;
	u_char  cfcr;

	cfcr = inb(iobase + com_cfcr);
	outb(iobase + com_cfcr, CFCR_DLAB | cfcr);

	dlbl = inb(iobase + com_dlbl);
	dlbh = inb(iobase + com_dlbh);

	outb(iobase + com_cfcr, cfcr);

	divisor = dlbh << 8 | dlbl;

	/* XXX there should be more sanity checking. */
	if (divisor == 0)
		return (CONSPEED);
	return (rclk / (16UL * divisor));
}

static void
siocnopen(sp, iobase, speed)
	struct siocnstate	*sp;
	Port_t			iobase;
	int			speed;
{
	u_int	divisor;
	u_char	dlbh;
	u_char	dlbl;

	/*
	 * Save all the device control registers except the fifo register
	 * and set our default ones (cs8 -parenb speed=comdefaultrate).
	 * We can't save the fifo register since it is read-only.
	 */
	sp->ier = inb(iobase + com_ier);
	outb(iobase + com_ier, 0);	/* spltty() doesn't stop siointr() */
	siocntxwait(iobase);
	sp->cfcr = inb(iobase + com_cfcr);
	outb(iobase + com_cfcr, CFCR_DLAB | CFCR_8BITS);
	sp->dlbl = inb(iobase + com_dlbl);
	sp->dlbh = inb(iobase + com_dlbh);
	/*
	 * Only set the divisor registers if they would change, since on
	 * some 16550 incompatibles (Startech), setting them clears the
	 * data input register.  This also reduces the effects of the
	 * UMC8669F bug.
	 */
	divisor = siodivisor(comdefaultrclk, speed);
	dlbl = divisor & 0xFF;
	if (sp->dlbl != dlbl)
		outb(iobase + com_dlbl, dlbl);
	dlbh = divisor >> 8;
	if (sp->dlbh != dlbh)
		outb(iobase + com_dlbh, dlbh);
	outb(iobase + com_cfcr, CFCR_8BITS);
	sp->mcr = inb(iobase + com_mcr);
	/*
	 * We don't want interrupts, but must be careful not to "disable"
	 * them by clearing the MCR_IENABLE bit, since that might cause
	 * an interrupt by floating the IRQ line.
	 */
	outb(iobase + com_mcr, (sp->mcr & MCR_IENABLE) | MCR_DTR | MCR_RTS);
}

static void
siocnclose(sp, iobase)
	struct siocnstate	*sp;
	Port_t			iobase;
{
	/*
	 * Restore the device control registers.
	 */
	siocntxwait(iobase);
	outb(iobase + com_cfcr, CFCR_DLAB | CFCR_8BITS);
	if (sp->dlbl != inb(iobase + com_dlbl))
		outb(iobase + com_dlbl, sp->dlbl);
	if (sp->dlbh != inb(iobase + com_dlbh))
		outb(iobase + com_dlbh, sp->dlbh);
	outb(iobase + com_cfcr, sp->cfcr);
	/*
	 * XXX damp oscillations of MCR_DTR and MCR_RTS by not restoring them.
	 */
	outb(iobase + com_mcr, sp->mcr | MCR_DTR | MCR_RTS);
	outb(iobase + com_ier, sp->ier);
}

static void
sio_cnprobe(cp)
	struct consdev	*cp;
{
	speed_t			boot_speed;
	u_char			cfcr;
	u_int			divisor;
	int			s, unit;
	struct siocnstate	sp;

	/*
	 * Find our first enabled console, if any.  If it is a high-level
	 * console device, then initialize it and return successfully.
	 * If it is a low-level console device, then initialize it and
	 * return unsuccessfully.  It must be initialized in both cases
	 * for early use by console drivers and debuggers.  Initializing
	 * the hardware is not necessary in all cases, since the i/o
	 * routines initialize it on the fly, but it is necessary if
	 * input might arrive while the hardware is switched back to an
	 * uninitialized state.  We can't handle multiple console devices
	 * yet because our low-level routines don't take a device arg.
	 * We trust the user to set the console flags properly so that we
	 * don't need to probe.
	 */
	cp->cn_pri = CN_DEAD;

	for (unit = 0; unit < 16; unit++) { /* XXX need to know how many */
		int flags;

		if (resource_disabled("sio", unit))
			continue;
		if (resource_int_value("sio", unit, "flags", &flags))
			continue;
		if (COM_CONSOLE(flags) || COM_DEBUGGER(flags)) {
			int port;
			Port_t iobase;

			if (resource_int_value("sio", unit, "port", &port))
				continue;
			iobase = port;
			s = spltty();
			if ((boothowto & RB_SERIAL) && COM_CONSOLE(flags)) {
				boot_speed =
				    siocngetspeed(iobase, comdefaultrclk);
				if (boot_speed)
					comdefaultrate = boot_speed;
			}

			/*
			 * Initialize the divisor latch.  We can't rely on
			 * siocnopen() to do this the first time, since it 
			 * avoids writing to the latch if the latch appears
			 * to have the correct value.  Also, if we didn't
			 * just read the speed from the hardware, then we
			 * need to set the speed in hardware so that
			 * switching it later is null.
			 */
			cfcr = inb(iobase + com_cfcr);
			outb(iobase + com_cfcr, CFCR_DLAB | cfcr);
			divisor = siodivisor(comdefaultrclk, comdefaultrate);
			outb(iobase + com_dlbl, divisor & 0xff);
			outb(iobase + com_dlbh, divisor >> 8);
			outb(iobase + com_cfcr, cfcr);

			siocnopen(&sp, iobase, comdefaultrate);

			splx(s);
			if (COM_CONSOLE(flags) && !COM_LLCONSOLE(flags)) {
				siocnset(cp, unit);
				cp->cn_pri = COM_FORCECONSOLE(flags)
					     || boothowto & RB_SERIAL
					     ? CN_REMOTE : CN_NORMAL;
				siocniobase = iobase;
				siocnunit = unit;
			}
#ifdef GDB
			if (COM_DEBUGGER(flags))
				siogdbiobase = iobase;
#endif
		}
	}
}

static void
sio_cninit(cp)
	struct consdev	*cp;
{
	comconsole = cp->cn_unit;
}

static void
sio_cnterm(cp)
	struct consdev	*cp;
{
	comconsole = -1;
}

static void
sio_cngrab(struct consdev *cp)
{
}

static void
sio_cnungrab(struct consdev *cp)
{
}

static int
sio_cngetc(struct consdev *cd)
{
	int	c;
	Port_t	iobase;
	int	s;
	struct siocnstate	sp;
	speed_t	speed;

	if (cd != NULL && cd->cn_unit == siocnunit) {
		iobase = siocniobase;
		speed = comdefaultrate;
	} else {
#ifdef GDB
		iobase = siogdbiobase;
		speed = gdbdefaultrate;
#else
		return (-1);
#endif
	}
	s = spltty();
	siocnopen(&sp, iobase, speed);
	if (inb(iobase + com_lsr) & LSR_RXRDY)
		c = inb(iobase + com_data);
	else
		c = -1;
	siocnclose(&sp, iobase);
	splx(s);
	return (c);
}

static void
sio_cnputc(struct consdev *cd, int c)
{
	int	need_unlock;
	int	s;
	struct siocnstate	sp;
	Port_t	iobase;
	speed_t	speed;

	if (cd != NULL && cd->cn_unit == siocnunit) {
		iobase = siocniobase;
		speed = comdefaultrate;
	} else {
#ifdef GDB
		iobase = siogdbiobase;
		speed = gdbdefaultrate;
#else
		return;
#endif
	}
	s = spltty();
	need_unlock = 0;
	if (!kdb_active && sio_inited == 2 && !mtx_owned(&sio_lock)) {
		mtx_lock_spin(&sio_lock);
		need_unlock = 1;
	}
	siocnopen(&sp, iobase, speed);
	siocntxwait(iobase);
	outb(iobase + com_data, c);
	siocnclose(&sp, iobase);
	if (need_unlock)
		mtx_unlock_spin(&sio_lock);
	splx(s);
}

/*
 * Remote gdb(1) support.
 */

#if defined(GDB)

#include <gdb/gdb.h>

static gdb_probe_f siogdbprobe;
static gdb_init_f siogdbinit;
static gdb_term_f siogdbterm;
static gdb_getc_f siogdbgetc;
static gdb_putc_f siogdbputc;

GDB_DBGPORT(sio, siogdbprobe, siogdbinit, siogdbterm, siogdbgetc, siogdbputc);

static int
siogdbprobe(void)
{
	return ((siogdbiobase != 0) ? 0 : -1);
}

static void
siogdbinit(void)
{
}

static void
siogdbterm(void)
{
}

static void
siogdbputc(int c)
{
	sio_cnputc(NULL, c);
}

static int
siogdbgetc(void)
{
	return (sio_cngetc(NULL));
}

#endif
