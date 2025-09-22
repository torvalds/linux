/*	$OpenBSD: z8530sc.c,v 1.8 2017/12/30 20:46:59 guenther Exp $	*/
/*	$NetBSD: z8530sc.c,v 1.30 2009/05/22 03:51:30 mrg Exp $	*/

/*
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
 *	@(#)zs.c	8.1 (Berkeley) 7/19/93
 */

/*
 * Copyright (c) 1994 Gordon W. Ross
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)zs.c	8.1 (Berkeley) 7/19/93
 */

/*
 * Zilog Z8530 Dual UART driver (common part)
 *
 * This file contains the machine-independent parts of the
 * driver common to tty and keyboard/mouse sub-drivers.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/syslog.h>

#include <dev/ic/z8530reg.h>
#include <machine/z8530var.h>

void
zs_break(struct zs_chanstate *cs, int set)
{

	if (set) {
		cs->cs_preg[5] |= ZSWR5_BREAK;
		cs->cs_creg[5] |= ZSWR5_BREAK;
	} else {
		cs->cs_preg[5] &= ~ZSWR5_BREAK;
		cs->cs_creg[5] &= ~ZSWR5_BREAK;
	}
	zs_write_reg(cs, 5, cs->cs_creg[5]);
}


/*
 * drain on-chip fifo
 */
void
zs_iflush(struct zs_chanstate *cs)
{
	uint8_t c, rr0, rr1;
	int i;

	/*
	 * Count how many times we loop. Some systems, such as some
	 * Apple PowerBooks, claim to have SCC's which they really don't.
	 */
	for (i = 0; i < 32; i++) {
		/* Is there input available? */
		rr0 = zs_read_csr(cs);
		if ((rr0 & ZSRR0_RX_READY) == 0)
			break;

		/*
		 * First read the status, because reading the data
		 * destroys the status of this char.
		 */
		rr1 = zs_read_reg(cs, 1);
		c = zs_read_data(cs);

		if (rr1 & (ZSRR1_FE | ZSRR1_DO | ZSRR1_PE)) {
			/* Clear the receive error. */
			zs_write_csr(cs, ZSWR0_RESET_ERRORS);
		}
	}
}


/*
 * Write the given register set to the given zs channel in the proper order.
 * The channel must not be transmitting at the time.  The receiver will
 * be disabled for the time it takes to write all the registers.
 * Call this with interrupts disabled.
 */
void
zs_loadchannelregs(struct zs_chanstate *cs)
{
	uint8_t *reg, v;

	zs_write_csr(cs, ZSM_RESET_ERR); /* XXX: reset error condition */

#if 1
	/*
	 * XXX: Is this really a good idea?
	 * XXX: Should go elsewhere! -gwr
	 */
	zs_iflush(cs);	/* XXX */
#endif

	if (cs->cs_ctl_chan != NULL)
		v = ((cs->cs_ctl_chan->cs_creg[5] & (ZSWR5_RTS | ZSWR5_DTR)) !=
		    (cs->cs_ctl_chan->cs_preg[5] & (ZSWR5_RTS | ZSWR5_DTR)));
	else
		v = 0;

	if (memcmp((void *)cs->cs_preg, (void *)cs->cs_creg, 16) == 0 && !v)
		return;	/* only change if values are different */

	/* Copy "pending" regs to "current" */
	memcpy((void *)cs->cs_creg, (void *)cs->cs_preg, 16);
	reg = cs->cs_creg;	/* current regs */

	/* disable interrupts */
	zs_write_reg(cs, 1, reg[1] & ~ZSWR1_IMASK);

	/* baud clock divisor, stop bits, parity */
	zs_write_reg(cs, 4, reg[4]);

	/* misc. TX/RX control bits */
	zs_write_reg(cs, 10, reg[10]);

	/* char size, enable (RX/TX) */
	zs_write_reg(cs, 3, reg[3] & ~ZSWR3_RX_ENABLE);
	zs_write_reg(cs, 5, reg[5] & ~ZSWR5_TX_ENABLE);

	/* synchronous mode stuff */
	zs_write_reg(cs, 6, reg[6]);
	if (reg[15] & ZSWR15_ENABLE_ENHANCED)
		zs_write_reg(cs, 15, 0);
	zs_write_reg(cs, 7, reg[7]);

#if 0
	/*
	 * Registers 2 and 9 are special because they are
	 * actually common to both channels, but must be
	 * programmed through channel A.  The "zsc" attach
	 * function takes care of setting these registers
	 * and they should not be touched thereafter.
	 */
	/* interrupt vector */
	zs_write_reg(cs, 2, reg[2]);
	/* master interrupt control */
	zs_write_reg(cs, 9, reg[9]);
#endif

	/* Shut down the BRG */
	zs_write_reg(cs, 14, reg[14] & ~ZSWR14_BAUD_ENA);

#ifdef	ZS_MD_SETCLK
	/* Let the MD code setup any external clock. */
	ZS_MD_SETCLK(cs);
#endif	/* ZS_MD_SETCLK */

	/* clock mode control */
	zs_write_reg(cs, 11, reg[11]);

	/* baud rate (lo/hi) */
	zs_write_reg(cs, 12, reg[12]);
	zs_write_reg(cs, 13, reg[13]);

	/* Misc. control bits */
	zs_write_reg(cs, 14, reg[14]);

	/* which lines cause status interrupts */
	zs_write_reg(cs, 15, reg[15]);

	/*
	 * Zilog docs recommend resetting external status twice at this
	 * point. Mainly as the status bits are latched, and the first
	 * interrupt clear might unlatch them to new values, generating
	 * a second interrupt request.
	 */
	zs_write_csr(cs, ZSM_RESET_STINT);
	zs_write_csr(cs, ZSM_RESET_STINT);

	/* char size, enable (RX/TX)*/
	zs_write_reg(cs, 3, reg[3]);
	zs_write_reg(cs, 5, reg[5]);

	/* Write the status bits on the alternate channel also. */
	if (cs->cs_ctl_chan != NULL) {
		v = cs->cs_ctl_chan->cs_preg[5];
		cs->cs_ctl_chan->cs_creg[5] = v;
		zs_write_reg(cs->cs_ctl_chan, 5, v);
	}

	/* Register 7' if applicable */
	if (reg[15] & ZSWR15_ENABLE_ENHANCED)
		zs_write_reg(cs, 7, reg[16]);

	/* interrupt enables: RX, TX, STATUS */
	zs_write_reg(cs, 1, reg[1]);
}

/*
 * ZS hardware interrupt.  Scan all ZS channels.  NB: we know here that
 * channels are kept in (A,B) pairs.
 *
 * Do just a little, then get out; set a software interrupt if more
 * work is needed.
 *
 * We deliberately ignore the vectoring Zilog gives us, and match up
 * only the number of `reset interrupt under service' operations, not
 * the order.
 */
int
zsc_intr_hard(void *arg)
{
	struct zsc_softc *zsc = arg;
	struct zs_chanstate *cs0, *cs1;
	int handled;
	uint8_t rr3;

	handled = 0;

	/* First look at channel A. */
	cs0 = zsc->zsc_cs[0];
	cs1 = zsc->zsc_cs[1];

	/*
	 * We have to clear interrupt first to avoid a race condition,
	 * but it will be done in each MD handler.
	 */
	for (;;) {
		/* Note: only channel A has an RR3 */
		rr3 = zs_read_reg(cs0, 3);

		if ((rr3 & (ZSRR3_IP_A_RX | ZSRR3_IP_A_TX | ZSRR3_IP_A_STAT |
		    ZSRR3_IP_B_RX | ZSRR3_IP_B_TX | ZSRR3_IP_B_STAT)) == 0) {
			break;
		}
		handled = 1;

		/* First look at channel A. */
		if (rr3 & (ZSRR3_IP_A_RX | ZSRR3_IP_A_TX | ZSRR3_IP_A_STAT))
			zs_write_csr(cs0, ZSWR0_CLR_INTR);

		if (rr3 & ZSRR3_IP_A_RX)
			(*cs0->cs_ops->zsop_rxint)(cs0);
		if (rr3 & ZSRR3_IP_A_STAT)
			(*cs0->cs_ops->zsop_stint)(cs0, 0);
		if (rr3 & ZSRR3_IP_A_TX)
			(*cs0->cs_ops->zsop_txint)(cs0);

		/* Now look at channel B. */
		if (rr3 & (ZSRR3_IP_B_RX | ZSRR3_IP_B_TX | ZSRR3_IP_B_STAT))
			zs_write_csr(cs1, ZSWR0_CLR_INTR);

		if (rr3 & ZSRR3_IP_B_RX)
			(*cs1->cs_ops->zsop_rxint)(cs1);
		if (rr3 & ZSRR3_IP_B_STAT)
			(*cs1->cs_ops->zsop_stint)(cs1, 0);
		if (rr3 & ZSRR3_IP_B_TX)
			(*cs1->cs_ops->zsop_txint)(cs1);
	}

	/* Note: caller will check cs_x->cs_softreq and DTRT. */
	return handled;
}


/*
 * ZS software interrupt.  Scan all channels for deferred interrupts.
 */
int
zsc_intr_soft(void *arg)
{
	struct zsc_softc *zsc = arg;
	struct zs_chanstate *cs;
	int rval, chan;

	rval = 0;
	for (chan = 0; chan < 2; chan++) {
		cs = zsc->zsc_cs[chan];

		/*
		 * The softint flag can be safely cleared once
		 * we have decided to call the softint routine.
		 * (No need to do splzs() first.)
		 */
		if (cs->cs_softreq) {
			cs->cs_softreq = 0;
			(*cs->cs_ops->zsop_softint)(cs);
			rval++;
		}
	}
	return (rval);
}

/*
 * Provide a null zs "ops" vector.
 */

static void zsnull_rxint  (struct zs_chanstate *);
static void zsnull_stint  (struct zs_chanstate *, int);
static void zsnull_txint  (struct zs_chanstate *);
static void zsnull_softint(struct zs_chanstate *);

static void
zsnull_rxint(struct zs_chanstate *cs)
{

	/* Ask for softint() call. */
	cs->cs_softreq = 1;
}

static void
zsnull_stint(struct zs_chanstate *cs, int force)
{

	/* Ask for softint() call. */
	cs->cs_softreq = 1;
}

static void
zsnull_txint(struct zs_chanstate *cs)
{

	/* Ask for softint() call. */
	cs->cs_softreq = 1;
}

static void
zsnull_softint(struct zs_chanstate *cs)
{

	zs_write_reg(cs,  1, 0);
	zs_write_reg(cs, 15, 0);
}

struct zsops zsops_null = {
	zsnull_rxint,	/* receive char available */
	zsnull_stint,	/* external/status */
	zsnull_txint,	/* xmit buffer empty */
	zsnull_softint,	/* process software interrupt */
};
