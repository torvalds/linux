/*-
 * cyclades cyclom-y serial driver
 *	Andrew Herbert <andrew@werple.apana.org.au>, 17 August 1993
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993 Andrew Herbert.
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
 * 3. The name Andrew Herbert may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL I BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * TODO:
 * Atomic COR change.
 * Consoles.
 */

/*
 * Temporary compile-time configuration options.
 */
#define	RxFifoThreshold	(CD1400_RX_FIFO_SIZE / 2)
			/* Number of chars in the receiver FIFO before an
			 * an interrupt is generated.  Should depend on
			 * line speed.  Needs to be about 6 on a 486DX33
			 * for 4 active ports at 115200 bps.  Why doesn't
			 * 10 work?
			 */
#define	PollMode	/* Use polling-based irq service routine, not the
			 * hardware svcack lines.  Must be defined for
			 * Cyclom-16Y boards.  Less efficient for Cyclom-8Ys,
			 * and stops 4 * 115200 bps from working.
			 */
#undef	Smarts		/* Enable slightly more CD1400 intelligence.  Mainly
			 * the output CR/LF processing, plus we can avoid a
			 * few checks usually done in ttyinput().
			 *
			 * XXX not fully implemented, and not particularly
			 * worthwhile.
			 */
#undef	CyDebug		/* Include debugging code (not very expensive). */

/* These will go away. */
#undef	SOFT_CTS_OFLOW
#define	SOFT_HOTCHAR

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/serial.h>
#include <sys/syslog.h>
#include <sys/tty.h>

#include <machine/psl.h>

#include <dev/ic/cd1400.h>

#include <dev/cy/cyreg.h>
#include <dev/cy/cyvar.h>

#define	NCY 10			/* KLUDGE */

#define	NPORTS		(NCY * CY_MAX_PORTS)

#define	CY_MAX_PORTS		(CD1400_NO_OF_CHANNELS * CY_MAX_CD1400s)

/* We encode the cyclom unit number (cyu) in spare bits in the IVR's. */
#define	CD1400_xIVR_CHAN_SHIFT	3
#define	CD1400_xIVR_CHAN	0x1F

/*
 * ETC states.  com->etc may also contain a hardware ETC command value,
 * meaning that execution of that command is pending.
 */
#define	ETC_NONE		0	/* we depend on bzero() setting this */
#define	ETC_BREAK_STARTING	1
#define	ETC_BREAK_STARTED	2
#define	ETC_BREAK_ENDING	3
#define	ETC_BREAK_ENDED		4

#define	LOTS_OF_EVENTS	64	/* helps separate urgent events from input */

/*
 * com state bits.
 * (CS_BUSY | CS_TTGO) and (CS_BUSY | CS_TTGO | CS_ODEVREADY) must be higher
 * than the other bits so that they can be tested as a group without masking
 * off the low bits.
 *
 * The following com and tty flags correspond closely:
 *	CS_BUSY		= TS_BUSY (maintained by cystart(), cypoll() and
 *				   comstop())
 *	CS_TTGO		= ~TS_TTSTOP (maintained by cyparam() and cystart())
 *	CS_CTS_OFLOW	= CCTS_OFLOW (maintained by cyparam())
 *	CS_RTS_IFLOW	= CRTS_IFLOW (maintained by cyparam())
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
#define	CSE_ODONE	1	/* output transmitted */

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

#ifdef SMP
#define	COM_LOCK()	mtx_lock_spin(&cy_lock)
#define	COM_UNLOCK()	mtx_unlock_spin(&cy_lock)
#else
#define	COM_LOCK()
#define	COM_UNLOCK()
#endif

/* types.  XXX - should be elsewhere */
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
	u_char	etc;		/* pending Embedded Transmit Command */
	u_char	extra_state;	/* more flag bits, separate for order trick */
	u_char	gfrcr_image;	/* copy of value read from GFRCR */
	u_char	mcr_dtr;	/* MCR bit that is wired to DTR */
	u_char	mcr_image;	/* copy of value written to MCR */
	u_char	mcr_rts;	/* MCR bit that is wired to RTS */
	int	unit;		/* unit	number */

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

	int	cy_align;	/* index for register alignment */
	cy_addr	cy_iobase;	/* base address of this port's cyclom */
	cy_addr	iobase;		/* base address of this port's cd1400 */
	int	mcr_rts_reg;	/* cd1400 reg number of reg holding mcr_rts */

	struct tty	*tp;	/* cross reference */

	u_long	bytes_in;	/* statistics */
	u_long	bytes_out;
	u_int	delta_error_counts[CE_NTYPES];
	u_long	error_counts[CE_NTYPES];

	u_int	recv_exception;	/* exception chars received */
	u_int	mdm;		/* modem signal changes */
#ifdef CyDebug
	u_int	start_count;	/* no. of calls to cystart() */
	u_int	start_real;	/* no. of calls that did something */
#endif
	u_char	car;		/* CD1400 CAR shadow (if first unit in cd) */
	u_char	channel_control;/* CD1400 CCR control command shadow */
	u_char	cor[3];		/* CD1400 COR1-3 shadows */
	u_char	intr_enable;	/* CD1400 SRER shadow */

	/*
	 * Data area for output buffers.  Someday we should build the output
	 * buffer queue without copying data.
	 */
	u_char	obuf1[256];
	u_char	obuf2[256];
};

devclass_t	cy_devclass;
char		cy_driver_name[] = "cy";

static	void	cd1400_channel_cmd(struct com_s *com, int cmd);
static	void	cd1400_channel_cmd_wait(struct com_s *com);
static	void	cd_etc(struct com_s *com, int etc);
static	int	cd_getreg(struct com_s *com, int reg);
static	void	cd_setreg(struct com_s *com, int reg, int val);
static	void	cyinput(struct com_s *com);
static	int	cyparam(struct tty *tp, struct termios *t);
static	void	cypoll(void *arg);
static	void	cysettimeout(void);
static	int	cysetwater(struct com_s *com, speed_t speed);
static	int	cyspeed(speed_t speed, u_long cy_clock, int *prescaler_io);
static	void	cystart(struct tty *tp);
static	void	comstop(struct tty *tp, int rw);
static	timeout_t cywakeup;
static	void	disc_optim(struct tty *tp, struct termios *t,
		    struct com_s *com);

static t_break_t	cybreak;
static t_modem_t	cymodem;
static t_open_t		cyopen;
static t_close_t	cyclose;

#ifdef CyDebug
void	cystatus(int unit);
#endif

static struct	mtx cy_lock;
static int	cy_inited;

/* table and macro for fast conversion from a unit number to its com struct */
static	struct com_s	*p_cy_addr[NPORTS];
#define	cy_addr(unit)	(p_cy_addr[unit])

static	u_int	cy_events;	/* input chars + weighted output completions */
static	void	*cy_fast_ih;
static	void	*cy_slow_ih;
static	int	cy_timeout;
static	int	cy_timeouts_until_log;
static	struct	callout_handle cy_timeout_handle
    = CALLOUT_HANDLE_INITIALIZER(&cy_timeout_handle);

#ifdef CyDebug
static	u_int	cd_inbs;
static	u_int	cy_inbs;
static	u_int	cd_outbs;
static	u_int	cy_outbs;
static	u_int	cy_svrr_probes;
static	u_int	cy_timeouts;
#endif

static	int	cy_chip_offset[] = {
	0x0000, 0x0400, 0x0800, 0x0c00, 0x0200, 0x0600, 0x0a00, 0x0e00,
};
static	int	cy_nr_cd1400s[NCY];
static	int	cy_total_devices;
#undef	RxFifoThreshold
static	int	volatile RxFifoThreshold = (CD1400_RX_FIFO_SIZE / 2);

int
cy_units(cy_addr cy_iobase, int cy_align)
{
	int	cyu;
	u_char	firmware_version;
	int	i;
	cy_addr	iobase;

	for (cyu = 0; cyu < CY_MAX_CD1400s; ++cyu) {
		iobase = cy_iobase + (cy_chip_offset[cyu] << cy_align);

		/* wait for chip to become ready for new command */
		for (i = 0; i < 10; i++) {
			DELAY(50);
			if (!cd_inb(iobase, CD1400_CCR, cy_align))
				break;
		}

		/* clear the GFRCR register */
		cd_outb(iobase, CD1400_GFRCR, cy_align, 0);

		/* issue a reset command */
		cd_outb(iobase, CD1400_CCR, cy_align,
			CD1400_CCR_CMDRESET | CD1400_CCR_FULLRESET);

		/* XXX bogus initialization to avoid a gcc bug/warning. */
		firmware_version = 0;

		/* wait for the CD1400 to initialize itself */
		for (i = 0; i < 200; i++) {
			DELAY(50);

			/* retrieve firmware version */
			firmware_version = cd_inb(iobase, CD1400_GFRCR,
						  cy_align);
			if ((firmware_version & 0xf0) == 0x40)
				break;
		}

		/*
		 * Anything in the 0x40-0x4F range is fine.
		 * If one CD1400 is bad then we don't support higher
		 * numbered good ones on this board.
		 */
		if ((firmware_version & 0xf0) != 0x40)
			break;
	}
	return (cyu);
}

void *
cyattach_common(cy_addr cy_iobase, int cy_align)
{
	int	adapter;
	int	cyu;
	u_char	firmware_version;
	cy_addr	iobase;
	int	ncyu;
	int	unit;
	struct tty *tp;

	while (cy_inited != 2)
		if (atomic_cmpset_int(&cy_inited, 0, 1)) {
			mtx_init(&cy_lock, cy_driver_name, NULL, MTX_SPIN);
			atomic_store_rel_int(&cy_inited, 2);
		}

	adapter = cy_total_devices;
	if ((u_int)adapter >= NCY) {
		printf(
	"cy%d: can't attach adapter: insufficient cy devices configured\n",
		       adapter);
		return (NULL);
	}
	ncyu = cy_units(cy_iobase, cy_align);
	if (ncyu == 0)
		return (NULL);
	cy_nr_cd1400s[adapter] = ncyu;
	cy_total_devices++;

	unit = adapter * CY_MAX_PORTS;
	for (cyu = 0; cyu < ncyu; ++cyu) {
		int	cdu;

		iobase = (cy_addr) (cy_iobase
				    + (cy_chip_offset[cyu] << cy_align));
		firmware_version = cd_inb(iobase, CD1400_GFRCR, cy_align);

		/* Set up a receive timeout period of than 1+ ms. */
		cd_outb(iobase, CD1400_PPR, cy_align,
			howmany(CY_CLOCK(firmware_version)
				/ CD1400_PPR_PRESCALER, 1000));

		for (cdu = 0; cdu < CD1400_NO_OF_CHANNELS; ++cdu, ++unit) {
			struct com_s	*com;
			int		s;

			com = malloc(sizeof *com, M_DEVBUF, M_NOWAIT | M_ZERO);
			if (com == NULL)
				break;
			com->unit = unit;
			com->gfrcr_image = firmware_version;
			if (CY_RTS_DTR_SWAPPED(firmware_version)) {
				com->mcr_dtr = CD1400_MSVR1_RTS;
				com->mcr_rts = CD1400_MSVR2_DTR;
				com->mcr_rts_reg = CD1400_MSVR2;
			} else {
				com->mcr_dtr = CD1400_MSVR2_DTR;
				com->mcr_rts = CD1400_MSVR1_RTS;
				com->mcr_rts_reg = CD1400_MSVR1;
			}
			com->obufs[0].l_head = com->obuf1;
			com->obufs[1].l_head = com->obuf2;

			com->cy_align = cy_align;
			com->cy_iobase = cy_iobase;
			com->iobase = iobase;
			com->car = ~CD1400_CAR_CHAN;

			tp = com->tp = ttyalloc();
			tp->t_open = cyopen;
			tp->t_close = cyclose;
			tp->t_oproc = cystart;
			tp->t_stop = comstop;
			tp->t_param = cyparam;
			tp->t_break = cybreak;
			tp->t_modem = cymodem;
			tp->t_sc = com;

			if (cysetwater(com, tp->t_init_in.c_ispeed) != 0) {
				free(com, M_DEVBUF);
				return (NULL);
			}

			s = spltty();
			cy_addr(unit) = com;
			splx(s);

			if (cy_fast_ih == NULL) {
				swi_add(&tty_intr_event, "cy", cypoll, NULL, SWI_TTY, 0,
					&cy_fast_ih);
				swi_add(&clk_intr_event, "cy", cypoll, NULL, SWI_CLOCK, 0,
					&cy_slow_ih);
			}
			ttycreate(tp, TS_CALLOUT, "c%r%r",
			    adapter, unit % CY_MAX_PORTS);
		}
	}

	/* ensure an edge for the next interrupt */
	cy_outb(cy_iobase, CY_CLEAR_INTR, cy_align, 0);

	return (cy_addr(adapter * CY_MAX_PORTS));
}

static int
cyopen(struct tty *tp, struct cdev *dev)
{
	struct com_s	*com;
	int		s;

	com = tp->t_sc;
	s = spltty();
	/*
	 * We jump to this label after all non-interrupted sleeps to pick
	 * up any changes of the device state.
	 */

	/* Encode per-board unit in LIVR for access in intr routines. */
	cd_setreg(com, CD1400_LIVR,
		  (com->unit & CD1400_xIVR_CHAN) << CD1400_xIVR_CHAN_SHIFT);

	/*
	 * Flush fifos.  This requires a full channel reset which
	 * also disables the transmitter and receiver.  Recover
	 * from this.
	 */
	cd1400_channel_cmd(com,
			   CD1400_CCR_CMDRESET | CD1400_CCR_CHANRESET);
	cd1400_channel_cmd(com, com->channel_control);

	critical_enter();
	COM_LOCK();
	com->prev_modem_status = com->last_modem_status
	    = cd_getreg(com, CD1400_MSVR2);
	cd_setreg(com, CD1400_SRER,
		  com->intr_enable
		  = CD1400_SRER_MDMCH | CD1400_SRER_RXDATA);
	COM_UNLOCK();
	critical_exit();
	cysettimeout();
	return (0);
}


static void
cyclose(struct tty *tp)
{
	cy_addr		iobase;
	struct com_s	*com;
	int		s;
	int		unit;

	com = tp->t_sc;
	unit = com->unit;
	iobase = com->iobase;
	s = spltty();
	/* XXX */
	critical_enter();
	COM_LOCK();
	com->etc = ETC_NONE;
	cd_setreg(com, CD1400_COR2, com->cor[1] &= ~CD1400_COR2_ETC);
	COM_UNLOCK();
	critical_exit();
	cd_etc(com, CD1400_ETC_STOPBREAK);
	cd1400_channel_cmd(com, CD1400_CCR_CMDRESET | CD1400_CCR_FTF);

	{
		critical_enter();
		COM_LOCK();
		cd_setreg(com, CD1400_SRER, com->intr_enable = 0);
		COM_UNLOCK();
		critical_exit();
		tp = com->tp;
		if ((tp->t_cflag & HUPCL)
		    /*
		     * XXX we will miss any carrier drop between here and the
		     * next open.  Perhaps we should watch DCD even when the
		     * port is closed; it is not sufficient to check it at
		     * the next open because it might go up and down while
		     * we're not watching.
		     */
		    || (!tp->t_actout
		       && !(com->prev_modem_status & CD1400_MSVR2_CD)
		       && !(tp->t_init_in.c_cflag & CLOCAL))
		    || !(tp->t_state & TS_ISOPEN)) {
			(void)cymodem(tp, 0, SER_DTR);

			/* Disable receiver (leave transmitter enabled). */
			com->channel_control = CD1400_CCR_CMDCHANCTL
					       | CD1400_CCR_XMTEN
					       | CD1400_CCR_RCVDIS;
			cd1400_channel_cmd(com, com->channel_control);

			ttydtrwaitstart(tp);
		}
	}
	tp->t_actout = FALSE;
	wakeup(&tp->t_actout);
	wakeup(TSA_CARR_ON(tp));	/* restart any wopeners */
	splx(s);
}

/*
 * This function:
 *  a) needs to be called with COM_LOCK() held, and
 *  b) needs to return with COM_LOCK() held.
 */
static void
cyinput(struct com_s *com)
{
	u_char		*buf;
	int		incc;
	u_char		line_status;
	int		recv_data;
	struct tty	*tp;

	buf = com->ibuf;
	tp = com->tp;
	if (!(tp->t_state & TS_ISOPEN)) {
		cy_events -= (com->iptr - com->ibuf);
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
			COM_UNLOCK();
			critical_exit();
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
				cystart(tp);
			}
			critical_enter();
			COM_LOCK();
		} while (buf < com->iptr);
	} else {
		do {
			/*
			 * This may look odd, but it is using save-and-enable
			 * semantics instead of the save-and-disable semantics
			 * that are used everywhere else.
			 */
			COM_UNLOCK();
			critical_exit();
			line_status = buf[com->ierroff];
			recv_data = *buf++;
			if (line_status
			    & (CD1400_RDSR_BREAK | CD1400_RDSR_FE | CD1400_RDSR_OE | CD1400_RDSR_PE)) {
				if (line_status & CD1400_RDSR_BREAK)
					recv_data |= TTY_BI;
				if (line_status & CD1400_RDSR_FE)
					recv_data |= TTY_FE;
				if (line_status & CD1400_RDSR_OE)
					recv_data |= TTY_OE;
				if (line_status & CD1400_RDSR_PE)
					recv_data |= TTY_PE;
			}
			ttyld_rint(tp, recv_data);
			critical_enter();
			COM_LOCK();
		} while (buf < com->iptr);
	}
	cy_events -= (com->iptr - com->ibuf);
	com->iptr = com->ibuf;

	/*
	 * There is now room for another low-level buffer full of input,
	 * so enable RTS if it is now disabled and there is room in the
	 * high-level buffer.
	 */
	if ((com->state & CS_RTS_IFLOW) && !(com->mcr_image & com->mcr_rts) &&
	    !(tp->t_state & TS_TBLOCK))
		cd_setreg(com, com->mcr_rts_reg,
			  com->mcr_image |= com->mcr_rts);
}

int
cyintr(void *vcom)
{
	struct com_s	*basecom;
	int	baseu;
	int	cy_align;
	cy_addr	cy_iobase;
	int	cyu;
	cy_addr	iobase;
	u_char	status;
	int	unit;

	COM_LOCK();	/* XXX could this be placed down lower in the loop? */

	basecom = (struct com_s *)vcom;
	baseu = basecom->unit;
	cy_align = basecom->cy_align;
	cy_iobase = basecom->cy_iobase;
	unit = baseu / CY_MAX_PORTS;

	/* check each CD1400 in turn */
	for (cyu = 0; cyu < cy_nr_cd1400s[unit]; ++cyu) {
		iobase = (cy_addr) (cy_iobase
				    + (cy_chip_offset[cyu] << cy_align));
		/* poll to see if it has any work */
		status = cd_inb(iobase, CD1400_SVRR, cy_align);
		if (status == 0)
			continue; // XXX - FILTER_STRAY?
#ifdef CyDebug
		++cy_svrr_probes;
#endif
		/* service requests as appropriate, giving priority to RX */
		if (status & CD1400_SVRR_RXRDY) {
			struct com_s	*com;
			u_int		count;
			u_char		*ioptr;
			u_char		line_status;
			u_char		recv_data;
			u_char		serv_type;
#ifdef PollMode
			u_char		save_rir;
#endif

#ifdef PollMode
			save_rir = cd_inb(iobase, CD1400_RIR, cy_align);

			/* enter rx service */
			cd_outb(iobase, CD1400_CAR, cy_align, save_rir);
			cy_addr(baseu + cyu * CD1400_NO_OF_CHANNELS)->car
			= save_rir & CD1400_CAR_CHAN;

			serv_type = cd_inb(iobase, CD1400_RIVR, cy_align);
			com = cy_addr(baseu
				       + ((serv_type >> CD1400_xIVR_CHAN_SHIFT)
					  & CD1400_xIVR_CHAN));
#else
			/* ack receive service */
			serv_type = cy_inb(iobase, CY8_SVCACKR, cy_align);

			com = cy_addr(baseu +
				       + ((serv_type >> CD1400_xIVR_CHAN_SHIFT)
					  & CD1400_xIVR_CHAN));
#endif

		if (serv_type & CD1400_RIVR_EXCEPTION) {
			++com->recv_exception;
			line_status = cd_inb(iobase, CD1400_RDSR, cy_align);
			/* break/unnattached error bits or real input? */
			recv_data = cd_inb(iobase, CD1400_RDSR, cy_align);
#ifndef SOFT_HOTCHAR
			if (line_status & CD1400_RDSR_SPECIAL
			    && com->tp->t_hotchar != 0)
				swi_sched(cy_fast_ih, 0);

#endif
#if 1 /* XXX "intelligent" PFO error handling would break O error handling */
			if (line_status & (CD1400_RDSR_PE|CD1400_RDSR_FE|CD1400_RDSR_BREAK)) {
				/*
				  Don't store PE if IGNPAR and BI if IGNBRK,
				  this hack allows "raw" tty optimization
				  works even if IGN* is set.
				*/
				if (   com->tp == NULL
				    || !(com->tp->t_state & TS_ISOPEN)
				    || ((line_status & (CD1400_RDSR_PE|CD1400_RDSR_FE))
				    &&  (com->tp->t_iflag & IGNPAR))
				    || ((line_status & CD1400_RDSR_BREAK)
				    &&  (com->tp->t_iflag & IGNBRK)))
					goto cont;
				if (   (line_status & (CD1400_RDSR_PE|CD1400_RDSR_FE))
				    && (com->tp->t_state & TS_CAN_BYPASS_L_RINT)
				    && ((line_status & CD1400_RDSR_FE)
				    ||  ((line_status & CD1400_RDSR_PE)
				    &&  (com->tp->t_iflag & INPCK))))
					recv_data = 0;
			}
#endif /* 1 */
			++com->bytes_in;
#ifdef SOFT_HOTCHAR
			if (com->tp->t_hotchar != 0 && recv_data == com->tp->t_hotchar)
				swi_sched(cy_fast_ih, 0);
#endif
			ioptr = com->iptr;
			if (ioptr >= com->ibufend)
				CE_RECORD(com, CE_INTERRUPT_BUF_OVERFLOW);
			else {
				if (com->tp != NULL && com->tp->t_do_timestamp)
					microtime(&com->tp->t_timestamp);
				++cy_events;
				ioptr[0] = recv_data;
				ioptr[com->ierroff] = line_status;
				com->iptr = ++ioptr;
				if (ioptr == com->ihighwater
				    && com->state & CS_RTS_IFLOW)
					cd_outb(iobase, com->mcr_rts_reg,
						cy_align,
						com->mcr_image &=
						~com->mcr_rts);
				if (line_status & CD1400_RDSR_OE)
					CE_RECORD(com, CE_OVERRUN);
			}
			goto cont;
		} else {
			int	ifree;

			count = cd_inb(iobase, CD1400_RDCR, cy_align);
			if (!count)
				goto cont;
			com->bytes_in += count;
			ioptr = com->iptr;
			ifree = com->ibufend - ioptr;
			if (count > ifree) {
				count -= ifree;
				cy_events += ifree;
				if (ifree != 0) {
					if (com->tp != NULL && com->tp->t_do_timestamp)
						microtime(&com->tp->t_timestamp);
					do {
						recv_data = cd_inb(iobase,
								   CD1400_RDSR,
								   cy_align);
#ifdef SOFT_HOTCHAR
						if (com->tp->t_hotchar != 0
						    && recv_data
						       == com->tp->t_hotchar)
							swi_sched(cy_fast_ih,
								  0);
#endif
						ioptr[0] = recv_data;
						ioptr[com->ierroff] = 0;
						++ioptr;
					} while (--ifree != 0);
				}
				com->delta_error_counts
				    [CE_INTERRUPT_BUF_OVERFLOW] += count;
				do {
					recv_data = cd_inb(iobase, CD1400_RDSR,
							   cy_align);
#ifdef SOFT_HOTCHAR
					if (com->tp->t_hotchar != 0
					    && recv_data == com->tp->t_hotchar)
						swi_sched(cy_fast_ih, 0);
#endif
				} while (--count != 0);
			} else {
				if (com->tp != NULL && com->tp->t_do_timestamp)
					microtime(&com->tp->t_timestamp);
				if (ioptr <= com->ihighwater
				    && ioptr + count > com->ihighwater
				    && com->state & CS_RTS_IFLOW)
					cd_outb(iobase, com->mcr_rts_reg,
						cy_align,
						com->mcr_image
						&= ~com->mcr_rts);
				cy_events += count;
				do {
					recv_data = cd_inb(iobase, CD1400_RDSR,
							   cy_align);
#ifdef SOFT_HOTCHAR
					if (com->tp->t_hotchar != 0
					    && recv_data == com->tp->t_hotchar)
						swi_sched(cy_fast_ih, 0);
#endif
					ioptr[0] = recv_data;
					ioptr[com->ierroff] = 0;
					++ioptr;
				} while (--count != 0);
			}
			com->iptr = ioptr;
		}
cont:

			/* terminate service context */
#ifdef PollMode
			cd_outb(iobase, CD1400_RIR, cy_align,
				save_rir
				& ~(CD1400_RIR_RDIREQ | CD1400_RIR_RBUSY));
#else
			cd_outb(iobase, CD1400_EOSRR, cy_align, 0);
#endif
		}
		if (status & CD1400_SVRR_MDMCH) {
			struct com_s	*com;
			u_char	modem_status;
#ifdef PollMode
			u_char	save_mir;
#else
			u_char	vector;
#endif

#ifdef PollMode
			save_mir = cd_inb(iobase, CD1400_MIR, cy_align);

			/* enter modem service */
			cd_outb(iobase, CD1400_CAR, cy_align, save_mir);
			cy_addr(baseu + cyu * CD1400_NO_OF_CHANNELS)->car
			= save_mir & CD1400_CAR_CHAN;

			com = cy_addr(baseu + cyu * CD1400_NO_OF_CHANNELS
				       + (save_mir & CD1400_MIR_CHAN));
#else
			/* ack modem service */
			vector = cy_inb(iobase, CY8_SVCACKM, cy_align);

			com = cy_addr(baseu
				       + ((vector >> CD1400_xIVR_CHAN_SHIFT)
					  & CD1400_xIVR_CHAN));
#endif
			++com->mdm;
			modem_status = cd_inb(iobase, CD1400_MSVR2, cy_align);
		if (modem_status != com->last_modem_status) {
			/*
			 * Schedule high level to handle DCD changes.  Note
			 * that we don't use the delta bits anywhere.  Some
			 * UARTs mess them up, and it's easy to remember the
			 * previous bits and calculate the delta.
			 */
			com->last_modem_status = modem_status;
			if (!(com->state & CS_CHECKMSR)) {
				cy_events += LOTS_OF_EVENTS;
				com->state |= CS_CHECKMSR;
				swi_sched(cy_fast_ih, 0);
			}

#ifdef SOFT_CTS_OFLOW
			/* handle CTS change immediately for crisp flow ctl */
			if (com->state & CS_CTS_OFLOW) {
				if (modem_status & CD1400_MSVR2_CTS) {
					com->state |= CS_ODEVREADY;
					if (com->state >= (CS_BUSY | CS_TTGO
							   | CS_ODEVREADY)
					    && !(com->intr_enable
						 & CD1400_SRER_TXRDY))
						cd_outb(iobase, CD1400_SRER,
							cy_align,
							com->intr_enable
							= com->intr_enable
							  & ~CD1400_SRER_TXMPTY
							  | CD1400_SRER_TXRDY);
				} else {
					com->state &= ~CS_ODEVREADY;
					if (com->intr_enable
					    & CD1400_SRER_TXRDY)
						cd_outb(iobase, CD1400_SRER,
							cy_align,
							com->intr_enable
							= com->intr_enable
							  & ~CD1400_SRER_TXRDY
							  | CD1400_SRER_TXMPTY);
				}
			}
#endif
		}

			/* terminate service context */
#ifdef PollMode
			cd_outb(iobase, CD1400_MIR, cy_align,
				save_mir
				& ~(CD1400_MIR_RDIREQ | CD1400_MIR_RBUSY));
#else
			cd_outb(iobase, CD1400_EOSRR, cy_align, 0);
#endif
		}
		if (status & CD1400_SVRR_TXRDY) {
			struct com_s	*com;
#ifdef PollMode
			u_char	save_tir;
#else
			u_char	vector;
#endif

#ifdef PollMode
			save_tir = cd_inb(iobase, CD1400_TIR, cy_align);

			/* enter tx service */
			cd_outb(iobase, CD1400_CAR, cy_align, save_tir);
			cy_addr(baseu + cyu * CD1400_NO_OF_CHANNELS)->car
			= save_tir & CD1400_CAR_CHAN;

			com = cy_addr(baseu
				       + cyu * CD1400_NO_OF_CHANNELS
				       + (save_tir & CD1400_TIR_CHAN));
#else
			/* ack transmit service */
			vector = cy_inb(iobase, CY8_SVCACKT, cy_align);

			com = cy_addr(baseu
				       + ((vector >> CD1400_xIVR_CHAN_SHIFT)
					  & CD1400_xIVR_CHAN));
#endif

			if (com->etc != ETC_NONE) {
				if (com->intr_enable & CD1400_SRER_TXRDY) {
					/*
					 * Here due to sloppy SRER_TXRDY
					 * enabling.  Ignore.  Come back when
					 * tx is empty.
					 */
					cd_outb(iobase, CD1400_SRER, cy_align,
						com->intr_enable
						= (com->intr_enable
						  & ~CD1400_SRER_TXRDY)
						  | CD1400_SRER_TXMPTY);
					goto terminate_tx_service;
				}
				switch (com->etc) {
				case CD1400_ETC_SENDBREAK:
				case CD1400_ETC_STOPBREAK:
					/*
					 * Start the command.  Come back on
					 * next tx empty interrupt, hopefully
					 * after command has been executed.
					 */
					cd_outb(iobase, CD1400_COR2, cy_align,
						com->cor[1] |= CD1400_COR2_ETC);
					cd_outb(iobase, CD1400_TDR, cy_align,
						CD1400_ETC_CMD);
					cd_outb(iobase, CD1400_TDR, cy_align,
						com->etc);
					if (com->etc == CD1400_ETC_SENDBREAK)
						com->etc = ETC_BREAK_STARTING;
					else
						com->etc = ETC_BREAK_ENDING;
					goto terminate_tx_service;
				case ETC_BREAK_STARTING:
					/*
					 * BREAK is now on.  Continue with
					 * SRER_TXMPTY processing, hopefully
					 * don't come back.
					 */
					com->etc = ETC_BREAK_STARTED;
					break;
				case ETC_BREAK_STARTED:
					/*
					 * Came back due to sloppy SRER_TXMPTY
					 * enabling.  Hope again.
					 */
					break;
				case ETC_BREAK_ENDING:
					/*
					 * BREAK is now off.  Continue with
					 * SRER_TXMPTY processing and don't
					 * come back.  The SWI handler will
					 * restart tx interrupts if necessary.
					 */
					cd_outb(iobase, CD1400_COR2, cy_align,
						com->cor[1]
						&= ~CD1400_COR2_ETC);
					com->etc = ETC_BREAK_ENDED;
					if (!(com->state & CS_ODONE)) {
						cy_events += LOTS_OF_EVENTS;
						com->state |= CS_ODONE;
						swi_sched(cy_fast_ih, 0);
					}
					break;
				case ETC_BREAK_ENDED:
					/*
					 * Shouldn't get here.  Hope again.
					 */
					break;
				}
			}
			if (com->intr_enable & CD1400_SRER_TXMPTY) {
				if (!(com->extra_state & CSE_ODONE)) {
					cy_events += LOTS_OF_EVENTS;
					com->extra_state |= CSE_ODONE;
					swi_sched(cy_fast_ih, 0);
				}
				cd_outb(iobase, CD1400_SRER, cy_align,
					com->intr_enable
					&= ~CD1400_SRER_TXMPTY);
				goto terminate_tx_service;
			}
		if (com->state >= (CS_BUSY | CS_TTGO | CS_ODEVREADY)) {
			u_char	*ioptr;
			u_int	ocount;

			ioptr = com->obufq.l_head;
				ocount = com->obufq.l_tail - ioptr;
				if (ocount > CD1400_TX_FIFO_SIZE)
					ocount = CD1400_TX_FIFO_SIZE;
				com->bytes_out += ocount;
				do
					cd_outb(iobase, CD1400_TDR, cy_align,
						*ioptr++);
				while (--ocount != 0);
			com->obufq.l_head = ioptr;
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
					com->state &= ~CS_BUSY;

					/*
					 * The setting of CSE_ODONE may be
					 * stale here.  We currently only
					 * use it when CS_BUSY is set, and
					 * fixing it when we clear CS_BUSY
					 * is easiest.
					 */
					if (com->extra_state & CSE_ODONE) {
						cy_events -= LOTS_OF_EVENTS;
						com->extra_state &= ~CSE_ODONE;
					}

					cd_outb(iobase, CD1400_SRER, cy_align,
						com->intr_enable
						= (com->intr_enable
						  & ~CD1400_SRER_TXRDY)
						  | CD1400_SRER_TXMPTY);
				}
				if (!(com->state & CS_ODONE)) {
					cy_events += LOTS_OF_EVENTS;
					com->state |= CS_ODONE;

					/* handle at high level ASAP */
					swi_sched(cy_fast_ih, 0);
				}
			}
		}

			/* terminate service context */
terminate_tx_service:
#ifdef PollMode
			cd_outb(iobase, CD1400_TIR, cy_align,
				save_tir
				& ~(CD1400_TIR_RDIREQ | CD1400_TIR_RBUSY));
#else
			cd_outb(iobase, CD1400_EOSRR, cy_align, 0);
#endif
		}
	}

	/* ensure an edge for the next interrupt */
	cy_outb(cy_iobase, CY_CLEAR_INTR, cy_align, 0);

	swi_sched(cy_slow_ih, SWI_DELAY);

	COM_UNLOCK();
	return (FILTER_HANDLED);
}

static void
cybreak(struct tty *tp, int sig)
{
	struct com_s	*com;

	com = tp->t_sc;
	if (sig)
		cd_etc(com, CD1400_ETC_SENDBREAK);
	else
		cd_etc(com, CD1400_ETC_STOPBREAK);
}

static void
cypoll(void *arg)
{
	int		unit;

#ifdef CyDebug
	++cy_timeouts;
#endif
	if (cy_events == 0)
		return;
repeat:
	for (unit = 0; unit < NPORTS; ++unit) {
		struct com_s	*com;
		int		incc;
		struct tty	*tp;

		com = cy_addr(unit);
		if (com == NULL)
			continue;
		tp = com->tp;
		if (tp == NULL) {
			/*
			 * XXX forget any events related to closed devices
			 * (actually never opened devices) so that we don't
			 * loop.
			 */
			critical_enter();
			COM_LOCK();
			incc = com->iptr - com->ibuf;
			com->iptr = com->ibuf;
			if (com->state & CS_CHECKMSR) {
				incc += LOTS_OF_EVENTS;
				com->state &= ~CS_CHECKMSR;
			}
			cy_events -= incc;
			COM_UNLOCK();
			critical_exit();
			if (incc != 0)
				log(LOG_DEBUG,
				    "cy%d: %d events for device with no tp\n",
				    unit, incc);
			continue;
		}
		if (com->iptr != com->ibuf) {
			critical_enter();
			COM_LOCK();
			cyinput(com);
			COM_UNLOCK();
			critical_exit();
		}
		if (com->state & CS_CHECKMSR) {
			u_char	delta_modem_status;

			critical_enter();
			COM_LOCK();
			cyinput(com);
			delta_modem_status = com->last_modem_status
					     ^ com->prev_modem_status;
			com->prev_modem_status = com->last_modem_status;
			cy_events -= LOTS_OF_EVENTS;
			com->state &= ~CS_CHECKMSR;
			COM_UNLOCK();
			critical_exit();
			if (delta_modem_status & CD1400_MSVR2_CD)
				ttyld_modem(tp,
				    com->prev_modem_status & CD1400_MSVR2_CD);
		}
		if (com->extra_state & CSE_ODONE) {
			critical_enter();
			COM_LOCK();
			cy_events -= LOTS_OF_EVENTS;
			com->extra_state &= ~CSE_ODONE;
			COM_UNLOCK();
			critical_exit();
			if (!(com->state & CS_BUSY)) {
				tp->t_state &= ~TS_BUSY;
				ttwwakeup(com->tp);
			}
			if (com->etc != ETC_NONE) {
				if (com->etc == ETC_BREAK_ENDED)
					com->etc = ETC_NONE;
				wakeup(&com->etc);
			}
		}
		if (com->state & CS_ODONE) {
			critical_enter();
			COM_LOCK();
			cy_events -= LOTS_OF_EVENTS;
			com->state &= ~CS_ODONE;
			COM_UNLOCK();
			critical_exit();
			ttyld_start(tp);
		}
		if (cy_events == 0)
			break;
	}
	if (cy_events >= LOTS_OF_EVENTS)
		goto repeat;
}

static int
cyparam(struct tty *tp, struct termios *t)
{
	int		bits;
	int		cflag;
	struct com_s	*com;
	u_char		cor_change;
	u_long		cy_clock;
	int		idivisor;
	int		iflag;
	int		iprescaler;
	int		itimeout;
	int		odivisor;
	int		oprescaler;
	u_char		opt;
	int		s;

	com = tp->t_sc;

	/* check requested parameters */
	cy_clock = CY_CLOCK(com->gfrcr_image);
	idivisor = cyspeed(t->c_ispeed, cy_clock, &iprescaler);
	if (idivisor <= 0)
		return (EINVAL);
	odivisor = cyspeed(t->c_ospeed != 0 ? t->c_ospeed : tp->t_ospeed,
			    cy_clock, &oprescaler);
	if (odivisor <= 0)
		return (EINVAL);

	/* parameters are OK, convert them to the com struct and the device */
	s = spltty();
	if (t->c_ospeed == 0)
		(void)cymodem(tp, 0, SER_DTR);
	else
		(void)cymodem(tp, SER_DTR, 0);

	(void) cysetwater(com, t->c_ispeed);

	/* XXX we don't actually change the speed atomically. */

	cd_setreg(com, CD1400_RBPR, idivisor);
	cd_setreg(com, CD1400_RCOR, iprescaler);
	cd_setreg(com, CD1400_TBPR, odivisor);
	cd_setreg(com, CD1400_TCOR, oprescaler);

	/*
	 * channel control
	 *	receiver enable
	 *	transmitter enable (always set)
	 */
	cflag = t->c_cflag;
	opt = CD1400_CCR_CMDCHANCTL | CD1400_CCR_XMTEN
	      | (cflag & CREAD ? CD1400_CCR_RCVEN : CD1400_CCR_RCVDIS);
	if (opt != com->channel_control) {
		com->channel_control = opt;
		cd1400_channel_cmd(com, opt);
	}

#ifdef Smarts
	/* set special chars */
	/* XXX if one is _POSIX_VDISABLE, can't use some others */
	if (t->c_cc[VSTOP] != _POSIX_VDISABLE)
		cd_setreg(com, CD1400_SCHR1, t->c_cc[VSTOP]);
	if (t->c_cc[VSTART] != _POSIX_VDISABLE)
		cd_setreg(com, CD1400_SCHR2, t->c_cc[VSTART]);
	if (t->c_cc[VINTR] != _POSIX_VDISABLE)
		cd_setreg(com, CD1400_SCHR3, t->c_cc[VINTR]);
	if (t->c_cc[VSUSP] != _POSIX_VDISABLE)
		cd_setreg(com, CD1400_SCHR4, t->c_cc[VSUSP]);
#endif

	/*
	 * set channel option register 1 -
	 *	parity mode
	 *	stop bits
	 *	char length
	 */
	opt = 0;
	/* parity */
	if (cflag & PARENB) {
		if (cflag & PARODD)
			opt |= CD1400_COR1_PARODD;
		opt |= CD1400_COR1_PARNORMAL;
	}
	iflag = t->c_iflag;
	if (!(iflag & INPCK))
		opt |= CD1400_COR1_NOINPCK;
	bits = 1 + 1;
	/* stop bits */
	if (cflag & CSTOPB) {
		++bits;
		opt |= CD1400_COR1_STOP2;
	}
	/* char length */
	switch (cflag & CSIZE) {
	case CS5:
		bits += 5;
		opt |= CD1400_COR1_CS5;
		break;
	case CS6:
		bits += 6;
		opt |= CD1400_COR1_CS6;
		break;
	case CS7:
		bits += 7;
		opt |= CD1400_COR1_CS7;
		break;
	default:
		bits += 8;
		opt |= CD1400_COR1_CS8;
		break;
	}
	cor_change = 0;
	if (opt != com->cor[0]) {
		cor_change |= CD1400_CCR_COR1;
		cd_setreg(com, CD1400_COR1, com->cor[0] = opt);
	}

	/*
	 * Set receive time-out period, normally to max(one char time, 5 ms).
	 */
	itimeout = howmany(1000 * bits, t->c_ispeed);
#ifdef SOFT_HOTCHAR
#define	MIN_RTP		1
#else
#define	MIN_RTP		5
#endif
	if (itimeout < MIN_RTP)
		itimeout = MIN_RTP;
	if (!(t->c_lflag & ICANON) && t->c_cc[VMIN] != 0 && t->c_cc[VTIME] != 0
	    && t->c_cc[VTIME] * 10 > itimeout)
		itimeout = t->c_cc[VTIME] * 10;
	if (itimeout > 255)
		itimeout = 255;
	cd_setreg(com, CD1400_RTPR, itimeout);

	/*
	 * set channel option register 2 -
	 *	flow control
	 */
	opt = 0;
#ifdef Smarts
	if (iflag & IXANY)
		opt |= CD1400_COR2_IXANY;
	if (iflag & IXOFF)
		opt |= CD1400_COR2_IXOFF;
#endif
#ifndef SOFT_CTS_OFLOW
	if (cflag & CCTS_OFLOW)
		opt |= CD1400_COR2_CCTS_OFLOW;
#endif
	critical_enter();
	COM_LOCK();
	if (opt != com->cor[1]) {
		cor_change |= CD1400_CCR_COR2;
		cd_setreg(com, CD1400_COR2, com->cor[1] = opt);
	}
	COM_UNLOCK();
	critical_exit();

	/*
	 * set channel option register 3 -
	 *	receiver FIFO interrupt threshold
	 *	flow control
	 */
	opt = RxFifoThreshold;
#ifdef Smarts
	if (t->c_lflag & ICANON)
		opt |= CD1400_COR3_SCD34;	/* detect INTR & SUSP chars */
	if (iflag & IXOFF)
		/* detect and transparently handle START and STOP chars */
		opt |= CD1400_COR3_FCT | CD1400_COR3_SCD12;
#endif
	if (opt != com->cor[2]) {
		cor_change |= CD1400_CCR_COR3;
		cd_setreg(com, CD1400_COR3, com->cor[2] = opt);
	}

	/* notify the CD1400 if COR1-3 have changed */
	if (cor_change)
		cd1400_channel_cmd(com, CD1400_CCR_CMDCORCHG | cor_change);

	/*
	 * set channel option register 4 -
	 *	CR/NL processing
	 *	break processing
	 *	received exception processing
	 */
	opt = 0;
	if (iflag & IGNCR)
		opt |= CD1400_COR4_IGNCR;
#ifdef Smarts
	/*
	 * we need a new ttyinput() for this, as we don't want to
	 * have ICRNL && INLCR being done in both layers, or to have
	 * synchronisation problems
	 */
	if (iflag & ICRNL)
		opt |= CD1400_COR4_ICRNL;
	if (iflag & INLCR)
		opt |= CD1400_COR4_INLCR;
#endif
	if (iflag & IGNBRK)
		opt |= CD1400_COR4_IGNBRK | CD1400_COR4_NOBRKINT;
	/*
	 * The `-ignbrk -brkint parmrk' case is not handled by the hardware,
	 * so only tell the hardware about -brkint if -parmrk.
	 */
	if (!(iflag & (BRKINT | PARMRK)))
		opt |= CD1400_COR4_NOBRKINT;
#if 0
	/* XXX using this "intelligence" breaks reporting of overruns. */
	if (iflag & IGNPAR)
		opt |= CD1400_COR4_PFO_DISCARD;
	else {
		if (iflag & PARMRK)
			opt |= CD1400_COR4_PFO_ESC;
		else
			opt |= CD1400_COR4_PFO_NUL;
	}
#else
	opt |= CD1400_COR4_PFO_EXCEPTION;
#endif
	cd_setreg(com, CD1400_COR4, opt);

	/*
	 * set channel option register 5 -
	 */
	opt = 0;
	if (iflag & ISTRIP)
		opt |= CD1400_COR5_ISTRIP;
	if (t->c_iflag & IEXTEN)
		/* enable LNEXT (e.g. ctrl-v quoting) handling */
		opt |= CD1400_COR5_LNEXT;
#ifdef Smarts
	if (t->c_oflag & ONLCR)
		opt |= CD1400_COR5_ONLCR;
	if (t->c_oflag & OCRNL)
		opt |= CD1400_COR5_OCRNL;
#endif
	cd_setreg(com, CD1400_COR5, opt);

	/*
	 * We always generate modem status change interrupts for CD changes.
	 * Among other things, this is necessary to track TS_CARR_ON for
	 * pstat to print even when the driver doesn't care.  CD changes
	 * should be rare so interrupts for them are not worth extra code to
	 * avoid.  We avoid interrupts for other modem status changes (except
	 * for CTS changes when SOFT_CTS_OFLOW is configured) since this is
	 * simplest and best.
	 */

	/*
	 * set modem change option register 1
	 *	generate modem interrupts on which 1 -> 0 input transitions
	 *	also controls auto-DTR output flow-control, which we don't use
	 */
	opt = CD1400_MCOR1_CDzd;
#ifdef SOFT_CTS_OFLOW
	if (cflag & CCTS_OFLOW)
		opt |= CD1400_MCOR1_CTSzd;
#endif
	cd_setreg(com, CD1400_MCOR1, opt);

	/*
	 * set modem change option register 2
	 *	generate modem interrupts on specific 0 -> 1 input transitions
	 */
	opt = CD1400_MCOR2_CDod;
#ifdef SOFT_CTS_OFLOW
	if (cflag & CCTS_OFLOW)
		opt |= CD1400_MCOR2_CTSod;
#endif
	cd_setreg(com, CD1400_MCOR2, opt);

	/*
	 * XXX should have done this long ago, but there is too much state
	 * to change all atomically.
	 */
	critical_enter();
	COM_LOCK();

	com->state &= ~CS_TTGO;
	if (!(tp->t_state & TS_TTSTOP))
		com->state |= CS_TTGO;
	if (cflag & CRTS_IFLOW) {
		com->state |= CS_RTS_IFLOW;
		/*
		 * If CS_RTS_IFLOW just changed from off to on, the change
		 * needs to be propagated to CD1400_MSVR1_RTS.  This isn't urgent,
		 * so do it later by calling cystart() instead of repeating
		 * a lot of code from cystart() here.
		 */
	} else if (com->state & CS_RTS_IFLOW) {
		com->state &= ~CS_RTS_IFLOW;
		/*
		 * CS_RTS_IFLOW just changed from on to off.  Force CD1400_MSVR1_RTS
		 * on here, since cystart() won't do it later.
		 */
		cd_setreg(com, com->mcr_rts_reg,
			  com->mcr_image |= com->mcr_rts);
	}

	/*
	 * Set up state to handle output flow control.
	 * XXX - worth handling MDMBUF (DCD) flow control at the lowest level?
	 * Now has 10+ msec latency, while CTS flow has 50- usec latency.
	 */
	com->state |= CS_ODEVREADY;
#ifdef SOFT_CTS_OFLOW
	com->state &= ~CS_CTS_OFLOW;
	if (cflag & CCTS_OFLOW) {
		com->state |= CS_CTS_OFLOW;
		if (!(com->last_modem_status & CD1400_MSVR2_CTS))
			com->state &= ~CS_ODEVREADY;
	}
#endif
	/* XXX shouldn't call functions while intrs are disabled. */
	disc_optim(tp, t, com);
#if 0
	/*
	 * Recover from fiddling with CS_TTGO.  We used to call cyintr1()
	 * unconditionally, but that defeated the careful discarding of
	 * stale input in cyopen().
	 */
	if (com->state >= (CS_BUSY | CS_TTGO))
		cyintr1(com);
#endif
	if (com->state >= (CS_BUSY | CS_TTGO | CS_ODEVREADY)) {
		if (!(com->intr_enable & CD1400_SRER_TXRDY))
			cd_setreg(com, CD1400_SRER,
				  com->intr_enable
				  = (com->intr_enable & ~CD1400_SRER_TXMPTY)
				    | CD1400_SRER_TXRDY);
	} else {
		if (com->intr_enable & CD1400_SRER_TXRDY)
			cd_setreg(com, CD1400_SRER,
				  com->intr_enable
				  = (com->intr_enable & ~CD1400_SRER_TXRDY)
				    | CD1400_SRER_TXMPTY);
	}

	COM_UNLOCK();
	critical_exit();
	splx(s);
	cystart(tp);
	if (com->ibufold != NULL) {
		free(com->ibufold, M_DEVBUF);
		com->ibufold = NULL;
	}
	return (0);
}

static int
cysetwater(struct com_s *com, speed_t speed)
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
		return (0);
	}

	/*
	 * Allocate input buffer.  The extra factor of 2 in the size is
	 * to allow for an error byte for each input byte.
	 */
	ibuf = malloc(2 * ibufsize, M_DEVBUF, M_NOWAIT);
	if (ibuf == NULL) {
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
	critical_enter();
	COM_LOCK();
	if (com->iptr != com->ibuf)
		cyinput(com);

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

	COM_UNLOCK();
	critical_exit();
	return (0);
}

static void
cystart(struct tty *tp)
{
	struct com_s	*com;
	int		s;
#ifdef CyDebug
	bool_t		started;
#endif

	com = tp->t_sc;
	s = spltty();

#ifdef CyDebug
	++com->start_count;
	started = FALSE;
#endif

	critical_enter();
	COM_LOCK();
	if (tp->t_state & TS_TTSTOP) {
		com->state &= ~CS_TTGO;
		if (com->intr_enable & CD1400_SRER_TXRDY)
			cd_setreg(com, CD1400_SRER,
				  com->intr_enable
				  = (com->intr_enable & ~CD1400_SRER_TXRDY)
				    | CD1400_SRER_TXMPTY);
	} else {
		com->state |= CS_TTGO;
		if (com->state >= (CS_BUSY | CS_TTGO | CS_ODEVREADY)
		    && !(com->intr_enable & CD1400_SRER_TXRDY))
			cd_setreg(com, CD1400_SRER,
				  com->intr_enable
				  = (com->intr_enable & ~CD1400_SRER_TXMPTY)
				    | CD1400_SRER_TXRDY);
	}
	if (tp->t_state & TS_TBLOCK) {
		if (com->mcr_image & com->mcr_rts && com->state & CS_RTS_IFLOW)
#if 0
			outb(com->modem_ctl_port, com->mcr_image &= ~CD1400_MSVR1_RTS);
#else
			cd_setreg(com, com->mcr_rts_reg,
				  com->mcr_image &= ~com->mcr_rts);
#endif
	} else {
		if (!(com->mcr_image & com->mcr_rts)
		    && com->iptr < com->ihighwater
		    && com->state & CS_RTS_IFLOW)
#if 0
			outb(com->modem_ctl_port, com->mcr_image |= CD1400_MSVR1_RTS);
#else
			cd_setreg(com, com->mcr_rts_reg,
				  com->mcr_image |= com->mcr_rts);
#endif
	}
	COM_UNLOCK();
	critical_exit();
	if (tp->t_state & (TS_TIMEOUT | TS_TTSTOP)) {
		ttwwakeup(tp);
		splx(s);
		return;
	}
	if (tp->t_outq.c_cc != 0) {
		struct lbq	*qp;
		struct lbq	*next;

		if (!com->obufs[0].l_queued) {
#ifdef CyDebug
			started = TRUE;
#endif
			com->obufs[0].l_tail
			    = com->obuf1 + q_to_b(&tp->t_outq, com->obuf1,
						  sizeof com->obuf1);
			com->obufs[0].l_next = NULL;
			com->obufs[0].l_queued = TRUE;
			critical_enter();
			COM_LOCK();
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
				if (com->state >= (CS_BUSY | CS_TTGO
						   | CS_ODEVREADY))
					cd_setreg(com, CD1400_SRER,
						  com->intr_enable
						  = (com->intr_enable
						    & ~CD1400_SRER_TXMPTY)
						    | CD1400_SRER_TXRDY);
			}
			COM_UNLOCK();
			critical_exit();
		}
		if (tp->t_outq.c_cc != 0 && !com->obufs[1].l_queued) {
#ifdef CyDebug
			started = TRUE;
#endif
			com->obufs[1].l_tail
			    = com->obuf2 + q_to_b(&tp->t_outq, com->obuf2,
						  sizeof com->obuf2);
			com->obufs[1].l_next = NULL;
			com->obufs[1].l_queued = TRUE;
			critical_enter();
			COM_LOCK();
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
				if (com->state >= (CS_BUSY | CS_TTGO
						   | CS_ODEVREADY))
					cd_setreg(com, CD1400_SRER,
						  com->intr_enable
						  = (com->intr_enable
						    & ~CD1400_SRER_TXMPTY)
						    | CD1400_SRER_TXRDY);
			}
			COM_UNLOCK();
			critical_exit();
		}
		tp->t_state |= TS_BUSY;
	}
#ifdef CyDebug
	if (started)
		++com->start_real;
#endif
#if 0
	critical_enter();
	COM_LOCK();
	if (com->state >= (CS_BUSY | CS_TTGO))
		cyintr1(com);	/* fake interrupt to start output */
	COM_UNLOCK();
	critical_exit();
#endif
	ttwwakeup(tp);
	splx(s);
}

static void
comstop(struct tty *tp, int rw)
{
	struct com_s	*com;
	bool_t		wakeup_etc;

	com = tp->t_sc;
	wakeup_etc = FALSE;
	critical_enter();
	COM_LOCK();
	if (rw & FWRITE) {
		com->obufs[0].l_queued = FALSE;
		com->obufs[1].l_queued = FALSE;
		if (com->extra_state & CSE_ODONE) {
			cy_events -= LOTS_OF_EVENTS;
			com->extra_state &= ~CSE_ODONE;
			if (com->etc != ETC_NONE) {
				if (com->etc == ETC_BREAK_ENDED)
					com->etc = ETC_NONE;
				wakeup_etc = TRUE;
			}
		}
		com->tp->t_state &= ~TS_BUSY;
		if (com->state & CS_ODONE)
			cy_events -= LOTS_OF_EVENTS;
		com->state &= ~(CS_ODONE | CS_BUSY);
	}
	if (rw & FREAD) {
		/* XXX no way to reset only input fifo. */
		cy_events -= (com->iptr - com->ibuf);
		com->iptr = com->ibuf;
	}
	COM_UNLOCK();
	critical_exit();
	if (wakeup_etc)
		wakeup(&com->etc);
	if (rw & FWRITE && com->etc == ETC_NONE)
		cd1400_channel_cmd(com, CD1400_CCR_CMDRESET | CD1400_CCR_FTF);
	cystart(tp);
}

static int
cymodem(struct tty *tp, int sigon, int sigoff)
{
	struct com_s	*com;
	int	mcr;
	int	msr;

	com = tp->t_sc;
	if (sigon == 0 && sigoff == 0) {
		sigon = 0;
		mcr = com->mcr_image;
		if (mcr & com->mcr_dtr)
			sigon |= SER_DTR;
		if (mcr & com->mcr_rts)
			/* XXX wired on for Cyclom-8Ys */
			sigon |= SER_RTS;

		/*
		 * We must read the modem status from the hardware because
		 * we don't generate modem status change interrupts for all
		 * changes, so com->prev_modem_status is not guaranteed to
		 * be up to date.  This is safe, unlike for sio, because
		 * reading the status register doesn't clear pending modem
		 * status change interrupts.
		 */
		msr = cd_getreg(com, CD1400_MSVR2);

		if (msr & CD1400_MSVR2_CTS)
			sigon |= SER_CTS;
		if (msr & CD1400_MSVR2_CD)
			sigon |= SER_DCD;
		if (msr & CD1400_MSVR2_DSR)
			sigon |= SER_DSR;
		if (msr & CD1400_MSVR2_RI)
			/* XXX not connected except for Cyclom-16Y? */
			sigon |= SER_RI;
		return (sigon);
	}
	mcr = com->mcr_image;
	if (sigon & SER_DTR)
		mcr |= com->mcr_dtr;
	if (sigoff & SER_DTR)
		mcr &= ~com->mcr_dtr;
	if (sigon & SER_RTS)
		mcr |= com->mcr_rts;
	if (sigoff & SER_RTS)
		mcr &= ~com->mcr_rts;
	critical_enter();
	COM_LOCK();
	com->mcr_image = mcr;
	cd_setreg(com, CD1400_MSVR1, mcr);
	cd_setreg(com, CD1400_MSVR2, mcr);
	COM_UNLOCK();
	critical_exit();
	return (0);
}

static void
cysettimeout()
{
	struct com_s	*com;
	bool_t		someopen;
	int		unit;

	/*
	 * Set our timeout period to 1 second if no polled devices are open.
	 * Otherwise set it to max(1/200, 1/hz).
	 * Enable timeouts iff some device is open.
	 */
	untimeout(cywakeup, (void *)NULL, cy_timeout_handle);
	cy_timeout = hz;
	someopen = FALSE;
	for (unit = 0; unit < NPORTS; ++unit) {
		com = cy_addr(unit);
		if (com != NULL && com->tp != NULL
		    && com->tp->t_state & TS_ISOPEN) {
			someopen = TRUE;
		}
	}
	if (someopen) {
		cy_timeouts_until_log = hz / cy_timeout;
		cy_timeout_handle = timeout(cywakeup, (void *)NULL,
					     cy_timeout);
	} else {
		/* Flush error messages, if any. */
		cy_timeouts_until_log = 1;
		cywakeup((void *)NULL);
		untimeout(cywakeup, (void *)NULL, cy_timeout_handle);
	}
}

static void
cywakeup(void *chan)
{
	struct com_s	*com;
	int		unit;

	cy_timeout_handle = timeout(cywakeup, (void *)NULL, cy_timeout);

	/*
	 * Check for and log errors, but not too often.
	 */
	if (--cy_timeouts_until_log > 0)
		return;
	cy_timeouts_until_log = hz / cy_timeout;
	for (unit = 0; unit < NPORTS; ++unit) {
		int	errnum;

		com = cy_addr(unit);
		if (com == NULL)
			continue;
		for (errnum = 0; errnum < CE_NTYPES; ++errnum) {
			u_int	delta;
			u_long	total;

			critical_enter();
			COM_LOCK();
			delta = com->delta_error_counts[errnum];
			com->delta_error_counts[errnum] = 0;
			COM_UNLOCK();
			critical_exit();
			if (delta == 0)
				continue;
			total = com->error_counts[errnum] += delta;
			log(LOG_ERR, "cy%d: %u more %s%s (total %lu)\n",
			    unit, delta, error_desc[errnum],
			    delta == 1 ? "" : "s", total);
		}
	}
}

static void
disc_optim(struct tty *tp, struct termios *t, struct com_s *com)
{
#ifndef SOFT_HOTCHAR
	u_char	opt;
#endif

	ttyldoptim(tp);
#ifndef SOFT_HOTCHAR
	opt = com->cor[2] & ~CD1400_COR3_SCD34;
	if (com->tp->t_hotchar != 0) {
		cd_setreg(com, CD1400_SCHR3, com->tp->t_hotchar);
		cd_setreg(com, CD1400_SCHR4, com->tp->t_hotchar);
		opt |= CD1400_COR3_SCD34;
	}
	if (opt != com->cor[2]) {
		cd_setreg(com, CD1400_COR3, com->cor[2] = opt);
		cd1400_channel_cmd(com, CD1400_CCR_CMDCORCHG | CD1400_CCR_COR3);
	}
#endif
}

#ifdef Smarts
/* standard line discipline input routine */
int
cyinput(int c, struct tty *tp)
{
	/* XXX duplicate ttyinput(), but without the IXOFF/IXON/ISTRIP/IPARMRK
	 * bits, as they are done by the CD1400.  Hardly worth the effort,
	 * given that high-throughput session are raw anyhow.
	 */
}
#endif /* Smarts */

static int
cyspeed(speed_t speed, u_long cy_clock, int *prescaler_io)
{
	int	actual;
	int	error;
	int	divider;
	int	prescaler;
	int	prescaler_unit;

	if (speed == 0)
		return (0);
	if (speed < 0 || speed > 150000)
		return (-1);

	/* determine which prescaler to use */
	for (prescaler_unit = 4, prescaler = 2048; prescaler_unit;
		prescaler_unit--, prescaler >>= 2) {
		if (cy_clock / prescaler / speed > 63)
			break;
	}

	divider = (cy_clock / prescaler * 2 / speed + 1) / 2; /* round off */
	if (divider > 255)
		divider = 255;
	actual = cy_clock/prescaler/divider;

	/* 10 times error in percent: */
	error = ((actual - (long)speed) * 2000 / (long)speed + 1) / 2;

	/* 3.0% max error tolerance */
	if (error < -30 || error > 30)
		return (-1);

	*prescaler_io = prescaler_unit;
	return (divider);
}

static void
cd1400_channel_cmd(struct com_s *com, int cmd)
{
	cd1400_channel_cmd_wait(com);
	cd_setreg(com, CD1400_CCR, cmd);
	cd1400_channel_cmd_wait(com);
}

static void
cd1400_channel_cmd_wait(struct com_s *com)
{
	struct timeval	start;
	struct timeval	tv;
	long		usec;

	if (cd_getreg(com, CD1400_CCR) == 0)
		return;
	microtime(&start);
	for (;;) {
		if (cd_getreg(com, CD1400_CCR) == 0)
			return;
		microtime(&tv);
		usec = 1000000 * (tv.tv_sec - start.tv_sec) +
		    tv.tv_usec - start.tv_usec;
		if (usec >= 5000) {
			log(LOG_ERR,
			    "cy%d: channel command timeout (%ld usec)\n",
			    com->unit, usec);
			return;
		}
	}
}

static void
cd_etc(struct com_s *com, int etc)
{

	/*
	 * We can't change the hardware's ETC state while there are any
	 * characters in the tx fifo, since those characters would be
	 * interpreted as commands!  Unputting characters from the fifo
	 * is difficult, so we wait up to 12 character times for the fifo
	 * to drain.  The command will be delayed for up to 2 character
	 * times for the tx to become empty.  Unputting characters from
	 * the tx holding and shift registers is impossible, so we wait
	 * for the tx to become empty so that the command is sure to be
	 * executed soon after we issue it.
	 */
	critical_enter();
	COM_LOCK();
	if (com->etc == etc)
		goto wait;
	if ((etc == CD1400_ETC_SENDBREAK
	    && (com->etc == ETC_BREAK_STARTING
		|| com->etc == ETC_BREAK_STARTED))
	    || (etc == CD1400_ETC_STOPBREAK
	       && (com->etc == ETC_BREAK_ENDING || com->etc == ETC_BREAK_ENDED
		   || com->etc == ETC_NONE))) {
		COM_UNLOCK();
		critical_exit();
		return;
	}
	com->etc = etc;
	cd_setreg(com, CD1400_SRER,
		  com->intr_enable
		  = (com->intr_enable & ~CD1400_SRER_TXRDY) | CD1400_SRER_TXMPTY);
wait:
	COM_UNLOCK();
	critical_exit();
	while (com->etc == etc
	       && tsleep(&com->etc, TTIPRI | PCATCH, "cyetc", 0) == 0)
		continue;
}

static int
cd_getreg(struct com_s *com, int reg)
{
	struct com_s	*basecom;
	u_char	car;
	int	cy_align;
	cy_addr	iobase;
#ifdef SMP
	int	need_unlock;
#endif
	int	val;

	basecom = cy_addr(com->unit & ~(CD1400_NO_OF_CHANNELS - 1));
	car = com->unit & CD1400_CAR_CHAN;
	cy_align = com->cy_align;
	iobase = com->iobase;
	critical_enter();
#ifdef SMP
	need_unlock = 0;
	if (!mtx_owned(&cy_lock)) {
		COM_LOCK();
		need_unlock = 1;
	}
#endif
	if (basecom->car != car)
		cd_outb(iobase, CD1400_CAR, cy_align, basecom->car = car);
	val = cd_inb(iobase, reg, cy_align);
#ifdef SMP
	if (need_unlock)
		COM_UNLOCK();
#endif
	critical_exit();
	return (val);
}

static void
cd_setreg(struct com_s *com, int reg, int val)
{
	struct com_s	*basecom;
	u_char	car;
	int	cy_align;
	cy_addr	iobase;
#ifdef SMP
	int	need_unlock;
#endif

	basecom = cy_addr(com->unit & ~(CD1400_NO_OF_CHANNELS - 1));
	car = com->unit & CD1400_CAR_CHAN;
	cy_align = com->cy_align;
	iobase = com->iobase;
	critical_enter();
#ifdef SMP
	need_unlock = 0;
	if (!mtx_owned(&cy_lock)) {
		COM_LOCK();
		need_unlock = 1;
	}
#endif
	if (basecom->car != car)
		cd_outb(iobase, CD1400_CAR, cy_align, basecom->car = car);
	cd_outb(iobase, reg, cy_align, val);
#ifdef SMP
	if (need_unlock)
		COM_UNLOCK();
#endif
	critical_exit();
}

#ifdef CyDebug
/* useful in ddb */
void
cystatus(int unit)
{
	struct com_s	*com;
	cy_addr		iobase;
	u_int		ocount;
	struct tty	*tp;

	com = cy_addr(unit);
	printf("info for channel %d\n", unit);
	printf("------------------\n");
	printf("total cyclom service probes:\t%d\n", cy_svrr_probes);
	printf("calls to upper layer:\t\t%d\n", cy_timeouts);
	if (com == NULL)
		return;
	iobase = com->iobase;
	printf("\n");
	printf("cd1400 base address:\\tt%p\n", iobase);
	printf("saved channel_control:\t\t0x%02x\n", com->channel_control);
	printf("saved cor1-3:\t\t\t0x%02x 0x%02x 0x%02x\n",
	       com->cor[0], com->cor[1], com->cor[2]);
	printf("service request enable reg:\t0x%02x (0x%02x cached)\n",
	       cd_getreg(com, CD1400_SRER), com->intr_enable);
	printf("service request register:\t0x%02x\n",
	       cd_inb(iobase, CD1400_SVRR, com->cy_align));
	printf("modem status:\t\t\t0x%02x (0x%02x cached)\n",
	       cd_getreg(com, CD1400_MSVR2), com->prev_modem_status);
	printf("rx/tx/mdm interrupt registers:\t0x%02x 0x%02x 0x%02x\n",
	       cd_inb(iobase, CD1400_RIR, com->cy_align),
	       cd_inb(iobase, CD1400_TIR, com->cy_align),
	       cd_inb(iobase, CD1400_MIR, com->cy_align));
	printf("\n");
	printf("com state:\t\t\t0x%02x\n", com->state);
	printf("calls to cystart():\t\t%d (%d useful)\n",
	       com->start_count, com->start_real);
	printf("rx buffer chars free:\t\t%d\n", com->iptr - com->ibuf);
	ocount = 0;
	if (com->obufs[0].l_queued)
		ocount += com->obufs[0].l_tail - com->obufs[0].l_head;
	if (com->obufs[1].l_queued)
		ocount += com->obufs[1].l_tail - com->obufs[1].l_head;
	printf("tx buffer chars:\t\t%u\n", ocount);
	printf("received chars:\t\t\t%d\n", com->bytes_in);
	printf("received exceptions:\t\t%d\n", com->recv_exception);
	printf("modem signal deltas:\t\t%d\n", com->mdm);
	printf("transmitted chars:\t\t%d\n", com->bytes_out);
	printf("\n");
	tp = com->tp;
	if (tp != NULL) {
		printf("tty state:\t\t\t0x%08x\n", tp->t_state);
		printf(
		"upper layer queue lengths:\t%d raw, %d canon, %d output\n",
		       tp->t_rawq.c_cc, tp->t_canq.c_cc, tp->t_outq.c_cc);
	} else
		printf("tty state:\t\t\tclosed\n");
}
#endif /* CyDebug */
