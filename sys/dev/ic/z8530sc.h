/*	$OpenBSD: z8530sc.h,v 1.12 2017/04/30 13:04:49 mpi Exp $	*/
/*	$NetBSD: z8530sc.h,v 1.15 2001/05/11 01:40:48 thorpej Exp $	*/

/*
 * Copyright (c) 1994 Gordon W. Ross
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)zsvar.h	8.1 (Berkeley) 6/11/93
 */


/*
 * Function vector - per channel
 */
struct zs_chanstate;
struct zsops {
	void	(*zsop_rxint)(struct zs_chanstate *);
					/* receive char available */
	void	(*zsop_stint)(struct zs_chanstate *, int);
					/* external/status */
	void	(*zsop_txint)(struct zs_chanstate *);
					/* xmit buffer empty */
	void	(*zsop_softint)(struct zs_chanstate *);
					/* process software interrupt */
};

extern struct zsops zsops_null;


/*
 * Software state, per zs channel.
 */
struct zs_chanstate {

	/* Pointers to the device registers. */
	volatile uint8_t *cs_reg_csr; 	/* ctrl, status, and reg. number. */
	volatile uint8_t *cs_reg_data;	/* data or numbered register */

	int	cs_channel;		/* sub-unit number */
	void   *cs_private;		/* sub-driver data pointer */
	struct zsops *cs_ops;

	int	cs_brg_clk;		/* BAUD Rate Generator clock
					 * (usually PCLK / 16) */
	int	cs_defspeed;		/* default baud rate */
	int	cs_defcflag;		/* default cflag */

	/*
	 * We must keep a copy of the write registers as they are
	 * mostly write-only and we sometimes need to set and clear
	 * individual bits (e.g., in WR3).  Not all of these are
	 * needed but 16 bytes is cheap and this makes the addressing
	 * simpler.  Unfortunately, we can only write to some registers
	 * when the chip is not actually transmitting, so whenever
	 * we are expecting a `transmit done' interrupt the preg array
	 * is allowed to `get ahead' of the current values.  In a
	 * few places we must change the current value of a register,
	 * rather than (or in addition to) the pending value; for these
	 * cs_creg[] contains the current value.
	 */
	uint8_t	cs_creg[17];		/* current values */
	uint8_t	cs_preg[17];		/* pending values */
	int 	cs_heldchange;		/* change pending (creg != preg) */

	uint8_t	cs_rr0;			/* last rr0 processed */
	uint8_t	cs_rr0_delta;		/* rr0 changes at status intr. */
	uint8_t	cs_rr0_mask;		/* rr0 bits that stop output */
	uint8_t	cs_rr0_dcd;		/* which bit to read as DCD */
	uint8_t	cs_rr0_cts;		/* which bit to read as CTS */
	uint8_t	cs_rr0_pps;		/* which bit to use for PPS */
	/* the above is set only while CRTSCTS is enabled. */

	uint8_t	cs_wr5_dtr;		/* which bit to write as DTR */
	uint8_t	cs_wr5_rts;		/* which bit to write as RTS */
	/* the above is set only while CRTSCTS is enabled. */

	volatile uint8_t cs_softreq;	/* need soft interrupt call */
	char	cs_cua;  		/* CUA mode flag */

	/*
	 * For strange systems that have oddly wired serial ports, we
	 * provide a pointer to the channel state of the port that has
	 * our status lines on it.
	 */
	struct  zs_chanstate *cs_ctl_chan;

	/* power management hooks */
	int	(*enable)(struct zs_chanstate *);
	void	(*disable)(struct zs_chanstate *);
	int	enabled;

	/* MD code might define a larger variant of this. */
};

struct consdev;
struct zsc_attach_args {
	char *type;		/* type name 'serial', 'keyboard', 'mouse' */
	int channel;		/* two serial channels per zsc */
	int hwflags;		/* see definitions below */
	/* `consdev' is only valid if ZS_HWFLAG_USE_CONSDEV is set */
	struct consdev *consdev;
};
/* In case of split console devices, use these: */
#define ZS_HWFLAG_CONSOLE_INPUT		1
#define ZS_HWFLAG_CONSOLE_OUTPUT	2
#define ZS_HWFLAG_CONSOLE		\
	(ZS_HWFLAG_CONSOLE_INPUT | ZS_HWFLAG_CONSOLE_OUTPUT)
#define ZS_HWFLAG_NO_DCD	4	/* Ignore the DCD bit */
#define ZS_HWFLAG_NO_CTS	8	/* Ignore the CTS bit */
#define ZS_HWFLAG_RAW   	16	/* advise raw mode */
#define ZS_HWFLAG_USE_CONSDEV  	32	/* Use console ops from `consdev' */
#define	ZS_HWFLAG_NORESET	64	/* Don't reset at attach time */

int 	zsc_intr_soft(void *);
int 	zsc_intr_hard(void *);

void	zs_abort(struct zs_chanstate *);
void	zs_break(struct zs_chanstate *, int);
void	zs_iflush(struct zs_chanstate *);
void	zs_loadchannelregs(struct zs_chanstate *);
int 	zs_set_speed(struct zs_chanstate *, int);
int 	zs_set_modes(struct zs_chanstate *, int);

extern int zs_major;
