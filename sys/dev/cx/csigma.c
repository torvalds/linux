/*-
 * Low-level subroutines for Cronyx-Sigma adapter.
 *
 * Copyright (C) 1994-2000 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * Cronyx Id: csigma.c,v 1.1.2.1 2003/11/12 17:13:41 rik Exp $
 * $FreeBSD$
 */
#include <dev/cx/machdep.h>
#include <dev/cx/cxddk.h>
#include <dev/cx/cxreg.h>
#include <dev/cx/cronyxfw.h>

#define DMA_MASK	0xd4	/* DMA mask register */
#define DMA_MASK_CLEAR	0x04	/* DMA clear mask */
#define DMA_MODE	0xd6	/* DMA mode register */
#define DMA_MODE_MASTER	0xc0	/* DMA master mode */

#define BYTE *(unsigned char*)&

static unsigned char irqmask [] = {
	BCR0_IRQ_DIS,	BCR0_IRQ_DIS,	BCR0_IRQ_DIS,	BCR0_IRQ_3,
	BCR0_IRQ_DIS,	BCR0_IRQ_5,	BCR0_IRQ_DIS,	BCR0_IRQ_7,
	BCR0_IRQ_DIS,	BCR0_IRQ_DIS,	BCR0_IRQ_10,	BCR0_IRQ_11,
	BCR0_IRQ_12,	BCR0_IRQ_DIS,	BCR0_IRQ_DIS,	BCR0_IRQ_15,
};

static unsigned char dmamask [] = {
	BCR0_DMA_DIS,	BCR0_DMA_DIS,	BCR0_DMA_DIS,	BCR0_DMA_DIS,
	BCR0_DMA_DIS,	BCR0_DMA_5,	BCR0_DMA_6,	BCR0_DMA_7,
};

/* standard base port set */
static short porttab [] = {
	0x200, 0x220, 0x240, 0x260, 0x280, 0x2a0, 0x2c0, 0x2e0,
	0x300, 0x320, 0x340, 0x360, 0x380, 0x3a0, 0x3c0, 0x3e0, 0
};

/* valid IRQs and DRQs */
static short irqtab [] = { 3, 5, 7, 10, 11, 12, 15, 0 };
static short dmatab [] = { 5, 6, 7, 0 };

static int valid (short value, short *list)
{
	while (*list)
		if (value == *list++)
			return 1;
	return 0;
}

long cx_rxbaud = 9600;		/* receiver baud rate */
long cx_txbaud = 9600;		/* transmitter baud rate */

int cx_univ_mode = M_HDLC;	/* univ. chan. mode: async or sync */
int cx_sync_mode = M_HDLC;	/* sync. chan. mode: HDLC, Bisync or X.21 */
int cx_iftype = 0;		/* univ. chan. interface: upper/lower */

static int cx_probe_chip (port_t base);
static void cx_setup_chip (cx_chan_t *c);

/*
 * Wait for CCR to clear.
 */
void cx_cmd (port_t base, int cmd)
{
	port_t port = CCR(base);
	int count;

	/* Wait 10 msec for the previous command to complete. */
	for (count=0; inb(port) && count<20000; ++count)
		continue;

	/* Issue the command. */
	outb (port, cmd);

	/* Wait 10 msec for the command to complete. */
	for (count=0; inb(port) && count<20000; ++count)
		continue;
}

/*
 * Reset the chip.
 */
static int cx_reset (port_t port)
{
	int count;

	/* Wait up to 10 msec for revision code to appear after reset. */
	for (count=0; count<20000; ++count)
		if (inb(GFRCR(port)) != 0)
			break;

	cx_cmd (port, CCR_RSTALL);

	/* Firmware revision code should clear immediately. */
	/* Wait up to 10 msec for revision code to appear again. */
	for (count=0; count<20000; ++count)
		if (inb(GFRCR(port)) != 0)
			return (1);

	/* Reset failed. */
	return (0);
}

int cx_download (port_t port, const unsigned char *firmware, long bits,
	const cr_dat_tst_t *tst)
{
	unsigned char cr2, sr;
	long i, n, maxn = (bits + 7) / 8;
	int v, b;

	inb (BDET(port));
	for (i=n=0; n<maxn; ++n) {
		v = ((firmware[n] ^ ' ') << 1) | (firmware[n] >> 7 & 1);
		for (b=0; b<7; b+=2, i+=2) {
			if (i >= bits)
				break;
			cr2 = 0;
			if (v >> b & 1) cr2 |= BCR2_TMS;
			if (v >> b & 2) cr2 |= BCR2_TDI;
			outb (BCR2(port), cr2);
			sr = inb (BSR(port));
			outb (BCR0(port), BCR0800_TCK);
			outb (BCR0(port), 0);
			if (i >= tst->end)
				++tst;
			if (i >= tst->start && (sr & BSR800_LERR))
				return (0);
		}
	}
	return (1);
}

/*
 * Check if the Sigma-XXX board is present at the given base port.
 */
static int cx_probe_chained_board (port_t port, int *c0, int *c1)
{
	int rev, i;

	/* Read and check the board revision code. */
	rev = inb (BSR(port));
	*c0 = *c1 = 0;
	switch (rev & BSR_VAR_MASK) {
	case CRONYX_100:	*c0 = 1;	break;
	case CRONYX_400:	*c1 = 1;	break;
	case CRONYX_500:	*c0 = *c1 = 1;	break;
	case CRONYX_410:	*c0 = 1;	break;
	case CRONYX_810:	*c0 = *c1 = 1;	break;
	case CRONYX_410s:	*c0 = 1;	break;
	case CRONYX_810s:	*c0 = *c1 = 1;	break;
	case CRONYX_440:	*c0 = 1;	break;
	case CRONYX_840:	*c0 = *c1 = 1;	break;
	case CRONYX_401:	*c0 = 1;	break;
	case CRONYX_801:	*c0 = *c1 = 1;	break;
	case CRONYX_401s:	*c0 = 1;	break;
	case CRONYX_801s:	*c0 = *c1 = 1;	break;
	case CRONYX_404:	*c0 = 1;	break;
	case CRONYX_703:	*c0 = *c1 = 1;	break;
	default:		return (0);	/* invalid variant code */
	}

	switch (rev & BSR_OSC_MASK) {
	case BSR_OSC_20:	/* 20 MHz */
	case BSR_OSC_18432:	/* 18.432 MHz */
		break;
	default:
		return (0);	/* oscillator frequency does not match */
	}

	for (i=2; i<0x10; i+=2)
		if ((inb (BSR(port)+i) & BSR_REV_MASK) != (rev & BSR_REV_MASK))
			return (0);	/* status changed? */
	return (1);
}

/*
 * Check if the Sigma-800 board is present at the given base port.
 * Read board status register 1 and check identification bits
 * which should invert every next read.
 */
static int cx_probe_800_chained_board (port_t port)
{
	unsigned char det, odet;
	int i;

	odet = inb (BDET(port));
	if ((odet & (BDET_IB | BDET_IB_NEG)) != BDET_IB &&
	    (odet & (BDET_IB | BDET_IB_NEG)) != BDET_IB_NEG)
		return (0);
	for (i=0; i<100; ++i) {
		det = inb (BDET(port));
		if (((det ^ odet) & (BDET_IB | BDET_IB_NEG)) !=
		    (BDET_IB | BDET_IB_NEG))
			return (0);
		odet = det;
	}
	/* Reset the controller. */
	outb (BCR0(port), 0);
	outb (BCR1(port), 0);
	outb (BCR2(port), 0);
	return (1);
}

/*
 * Check if the Sigma-2x board is present at the given base port.
 */
static int cx_probe_2x_board (port_t port)
{
	int rev, i;

	/* Read and check the board revision code. */
	rev = inb (BSR(port));
	if ((rev & BSR2X_VAR_MASK) != CRONYX_22 &&
	    (rev & BSR2X_VAR_MASK) != CRONYX_24)
		return (0);		/* invalid variant code */

	for (i=2; i<0x10; i+=2)
		if ((inb (BSR(port)+i) & BSR2X_REV_MASK) !=
		    (rev & BSR2X_REV_MASK))
			return (0);	/* status changed? */
	return (1);
}

/*
 * Check if the Cronyx-Sigma board is present at the given base port.
 */
int cx_probe_board (port_t port, int irq, int dma)
{
	int c0, c1, c2=0, c3=0, result;

	if (! valid (port, porttab))
		return 0;

	if (irq > 0 && ! valid (irq, irqtab))
		return 0;

	if (dma > 0 && ! valid (dma, dmatab))
		return 0;

	if (cx_probe_800_chained_board (port)) {
		/* Sigma-800 detected. */
		if (! (inb (BSR(port)) & BSR_NOCHAIN)) {
			/* chained board attached */
			if (! cx_probe_800_chained_board (port+0x10))
				/* invalid chained board? */
				return (0);
			if (! (inb (BSR(port+0x10)) & BSR_NOCHAIN))
				/* invalid chained board flag? */
				return (0);
		}
		return 1;
	}
	if (cx_probe_chained_board (port, &c0, &c1)) {
		/* Sigma-XXX detected. */
		if (! (inb (BSR(port)) & BSR_NOCHAIN)) {
			/* chained board attached */
			if (! cx_probe_chained_board (port+0x10, &c2, &c3))
				/* invalid chained board? */
				return (0);
			if (! (inb (BSR(port+0x10)) & BSR_NOCHAIN))
				/* invalid chained board flag? */
				return (0);
		}
	} else if (cx_probe_2x_board (port)) {
		c0 = 1;		/* Sigma-2x detected. */
		c1 = 0;
	} else
		return (0);     /* no board detected */

	/* Turn off the reset bit. */
	outb (BCR0(port), BCR0_NORESET);
	if (c2 || c3)
		outb (BCR0(port + 0x10), BCR0_NORESET);

	result = 1;
	if (c0 && ! cx_probe_chip (CS0(port)))
		result = 0;	/* no CD2400 chip here */
	else if (c1 && ! cx_probe_chip (CS1A(port)) &&
	    ! cx_probe_chip (CS1(port)))
		result = 0;	/* no second CD2400 chip */
	else if (c2 && ! cx_probe_chip (CS0(port + 0x10)))
		result = 0;	/* no CD2400 chip on the slave board */
	else if (c3 && ! cx_probe_chip (CS1(port + 0x10)))
		result = 0;	/* no second CD2400 chip on the slave board */

	/* Reset the controller. */
	outb (BCR0(port), 0);
	if (c2 || c3)
		outb (BCR0(port + 0x10), 0);

	/* Yes, we really have valid Sigma board. */
	return (result);
}

/*
 * Check if the CD2400 chip is present at the given base port.
 */
static int cx_probe_chip (port_t base)
{
	int rev, newrev, count;

	/* Wait up to 10 msec for revision code to appear after reset. */
	rev = 0;
	for (count=0; rev==0; ++count) {
		if (count >= 20000)
			return (0); /* reset failed */
		rev = inb (GFRCR(base));
	}

	/* Read and check the global firmware revision code. */
	if (! (rev>=REVCL_MIN && rev<=REVCL_MAX) &&
	    ! (rev>=REVCL31_MIN && rev<=REVCL31_MAX))
		return (0);	/* CD2400/2431 revision does not match */

	/* Reset the chip. */
	if (! cx_reset (base))
		return (0);

	/* Read and check the new global firmware revision code. */
	newrev = inb (GFRCR(base));
	if (newrev != rev)
		return (0);	/* revision changed */

	/* Yes, we really have CD2400/2431 chip here. */
	return (1);
}

/*
 * Check that the irq is functional.
 * irq>0  - activate the interrupt from the adapter (irq=on)
 * irq<0  - deactivate the interrupt (irq=off)
 * irq==0 - free the interrupt line (irq=tri-state)
 * Return the interrupt mask _before_ activating irq.
 */
int cx_probe_irq (cx_board_t *b, int irq)
{
	int mask, rev;
        port_t port;

	rev = inb (BSR(b->port));
        port = ((rev & BSR_VAR_MASK) != CRONYX_400) ? CS0(b->port) : CS1(b->port);

	outb (0x20, 0x0a);
	mask = inb (0x20);
	outb (0xa0, 0x0a);
	mask |= inb (0xa0) << 8;

	if (irq > 0) {
		outb (BCR0(b->port), BCR0_NORESET | irqmask[irq]);
		outb (CAR(port), 0);
		cx_cmd (port, CCR_CLRCH);
		outb (CMR(port), CMR_HDLC);
		outb (TCOR(port), 0);
		outb (TBPR(port), 1);
		cx_cmd (port, CCR_INITCH | CCR_ENTX);
		outb (IER(port), IER_TXMPTY);
	} else if (irq < 0) {
		cx_reset (port);
		if (-irq > 7) {
			outb (0xa0, 0x60 | ((-irq) & 7));
			outb (0x20, 0x62);
		} else
			outb (0x20, 0x60 | (-irq));
	} else
		outb (BCR0(b->port), 0);
	return mask;
}

static int cx_chip_revision (port_t port, int rev)
{
	int count;

	/* Model 400 has no first chip. */
	port = ((rev & BSR_VAR_MASK) != CRONYX_400) ? CS0(port) : CS1(port);

	/* Wait up to 10 msec for revision code to appear after reset. */
	for (count=0; inb(GFRCR(port))==0; ++count)
		if (count >= 20000)
			return (0); /* reset failed */

	return inb (GFRCR (port));
}

/*
 * Probe and initialize the board structure.
 */
void cx_init (cx_board_t *b, int num, port_t port, int irq, int dma)
{
	int gfrcr, rev, chain, mod = 0, rev2 = 0, mod2 = 0;

	rev = inb (BSR(port));
	chain = ! (rev & BSR_NOCHAIN);
	if (cx_probe_800_chained_board (port)) {
		cx_init_800 (b, num, port, irq, dma, chain);
		return;
	}
	if ((rev & BSR2X_VAR_MASK) == CRONYX_22 ||
	    (rev & BSR2X_VAR_MASK) == CRONYX_24) {
		cx_init_2x (b, num, port, irq, dma,
			(rev & BSR2X_VAR_MASK), (rev & BSR2X_OSC_33));
		return;
        }

	outb (BCR0(port), BCR0_NORESET);
	if (chain)
		outb (BCR0(port+0x10), BCR0_NORESET);
	gfrcr = cx_chip_revision (port, rev);
	if (gfrcr >= REVCL31_MIN && gfrcr <= REVCL31_MAX)
		mod = 1;
	if (chain) {
		rev2 = inb (BSR(port+0x10));
		gfrcr = cx_chip_revision (port+0x10, rev2);
		if (gfrcr >= REVCL31_MIN && gfrcr <= REVCL31_MAX)
			mod2 = 1;
		outb (BCR0(port+0x10), 0);
	}
	outb (BCR0(port), 0);

	cx_init_board (b, num, port, irq, dma, chain,
		(rev & BSR_VAR_MASK), (rev & BSR_OSC_MASK), mod,
		(rev2 & BSR_VAR_MASK), (rev2 & BSR_OSC_MASK), mod2);
}

/*
 * Initialize the board structure, given the type of the board.
 */
void cx_init_board (cx_board_t *b, int num, port_t port, int irq, int dma,
	int chain, int rev, int osc, int mod, int rev2, int osc2, int mod2)
{
	cx_chan_t *c;
	char *type;
	int i;

	/* Initialize board structure. */
	b->port = port;
	b->num = num;
	b->irq = irq;
	b->dma = dma;
	b->opt = board_opt_dflt;

	b->type = B_SIGMA_XXX;
	b->if0type = b->if8type = cx_iftype;

	/* Set channels 0 and 8 mode, set DMA and IRQ. */
	b->bcr0 = b->bcr0b = BCR0_NORESET | dmamask[b->dma] | irqmask[b->irq];

	/* Clear DTR[0..3] and DTR[8..12]. */
	b->bcr1 = b->bcr1b = 0;

	/*------------------ Master board -------------------*/

	/* Read and check the board revision code. */
	strcpy (b->name, mod ? "m" : "");
	switch (rev) {
	default:	  type = "";	 break;
	case CRONYX_100:  type = "100";  break;
	case CRONYX_400:  type = "400";  break;
	case CRONYX_500:  type = "500";  break;
	case CRONYX_410:  type = "410";  break;
	case CRONYX_810:  type = "810";  break;
	case CRONYX_410s: type = "410s"; break;
	case CRONYX_810s: type = "810s"; break;
	case CRONYX_440:  type = "440";  break;
	case CRONYX_840:  type = "840";  break;
	case CRONYX_401:  type = "401";  break;
	case CRONYX_801:  type = "801";  break;
	case CRONYX_401s: type = "401s"; break;
	case CRONYX_801s: type = "801s"; break;
	case CRONYX_404:  type = "404";  break;
	case CRONYX_703:  type = "703";  break;
	}
	strcat (b->name, type);

	switch (osc) {
	default:
	case BSR_OSC_20: /* 20 MHz */
		b->chan[0].oscfreq = b->chan[1].oscfreq =
		b->chan[2].oscfreq = b->chan[3].oscfreq =
		b->chan[4].oscfreq = b->chan[5].oscfreq =
		b->chan[6].oscfreq = b->chan[7].oscfreq =
			mod ? 33000000L : 20000000L;
		strcat (b->name, "a");
		break;
	case BSR_OSC_18432: /* 18.432 MHz */
		b->chan[0].oscfreq = b->chan[1].oscfreq =
		b->chan[2].oscfreq = b->chan[3].oscfreq =
		b->chan[4].oscfreq = b->chan[5].oscfreq =
		b->chan[6].oscfreq = b->chan[7].oscfreq =
			mod ? 20000000L : 18432000L;
		strcat (b->name, "b");
		break;
	}

	/*------------------ Slave board -------------------*/

	if (chain) {
		/* Read and check the board revision code. */
		strcat (b->name, mod2 ? "/m" : "/");
		switch (rev2) {
		default:	  type = "";	 break;
		case CRONYX_100:  type = "100";  break;
		case CRONYX_400:  type = "400";  break;
		case CRONYX_500:  type = "500";  break;
		case CRONYX_410:  type = "410";  break;
		case CRONYX_810:  type = "810";  break;
		case CRONYX_410s: type = "410s"; break;
		case CRONYX_810s: type = "810s"; break;
		case CRONYX_440:  type = "440";  break;
		case CRONYX_840:  type = "840";  break;
		case CRONYX_401:  type = "401";  break;
		case CRONYX_801:  type = "801";  break;
		case CRONYX_401s: type = "401s"; break;
		case CRONYX_801s: type = "801s"; break;
		case CRONYX_404:  type = "404";  break;
		case CRONYX_703:  type = "703";  break;
		}
		strcat (b->name, type);

		switch (osc2) {
		default:
		case BSR_OSC_20: /* 20 MHz */
			b->chan[8].oscfreq = b->chan[9].oscfreq =
			b->chan[10].oscfreq = b->chan[11].oscfreq =
			b->chan[12].oscfreq = b->chan[13].oscfreq =
			b->chan[14].oscfreq = b->chan[15].oscfreq =
				mod2 ? 33000000L : 20000000L;
			strcat (b->name, "a");
			break;
		case BSR_OSC_18432: /* 18.432 MHz */
			b->chan[8].oscfreq = b->chan[9].oscfreq =
			b->chan[10].oscfreq = b->chan[11].oscfreq =
			b->chan[12].oscfreq = b->chan[13].oscfreq =
			b->chan[14].oscfreq = b->chan[15].oscfreq =
				mod2 ? 20000000L : 18432000L;
			strcat (b->name, "b");
			break;
		}
	}

	/* Initialize channel structures. */
	for (i=0; i<4; ++i) {
		b->chan[i+0].port  = CS0(port);
		b->chan[i+4].port  = cx_probe_chip (CS1A(port)) ?
			CS1A(port) : CS1(port);
		b->chan[i+8].port  = CS0(port+0x10);
		b->chan[i+12].port = CS1(port+0x10);
	}
	for (c=b->chan; c<b->chan+NCHAN; ++c) {
		c->board = b;
		c->num = c - b->chan;
		c->type = T_NONE;
	}

	/*------------------ Master board -------------------*/

	switch (rev) {
	case CRONYX_400:
		for (i=4; i<8; ++i)
			b->chan[i].type = T_UNIV_RS232;
		break;
	case CRONYX_100:
		b->chan[0].type = T_UNIV_RS232;
		break;
	case CRONYX_500:
		b->chan[0].type = T_UNIV_RS232;
		for (i=4; i<8; ++i)
			b->chan[i].type = T_UNIV_RS232;
		break;
	case CRONYX_410:
		b->chan[0].type = T_UNIV_V35;
		for (i=1; i<4; ++i)
			b->chan[i].type = T_UNIV_RS232;
		break;
	case CRONYX_810:
		b->chan[0].type = T_UNIV_V35;
		for (i=1; i<8; ++i)
			b->chan[i].type = T_UNIV_RS232;
		break;
	case CRONYX_410s:
		b->chan[0].type = T_UNIV_V35;
		for (i=1; i<4; ++i)
			b->chan[i].type = T_SYNC_RS232;
		break;
	case CRONYX_810s:
		b->chan[0].type = T_UNIV_V35;
		for (i=1; i<4; ++i)
			b->chan[i].type = T_SYNC_RS232;
		for (i=4; i<8; ++i)
			b->chan[i].type = T_UNIV_RS232;
		break;
	case CRONYX_440:
		b->chan[0].type = T_UNIV_V35;
		for (i=1; i<4; ++i)
			b->chan[i].type = T_SYNC_V35;
		break;
	case CRONYX_840:
		b->chan[0].type = T_UNIV_V35;
		for (i=1; i<4; ++i)
			b->chan[i].type = T_SYNC_V35;
		for (i=4; i<8; ++i)
			b->chan[i].type = T_UNIV_RS232;
		break;
	case CRONYX_401:
		b->chan[0].type = T_UNIV_RS449;
		for (i=1; i<4; ++i)
			b->chan[i].type = T_UNIV_RS232;
		break;
	case CRONYX_801:
		b->chan[0].type = T_UNIV_RS449;
		for (i=1; i<8; ++i)
			b->chan[i].type = T_UNIV_RS232;
		break;
	case CRONYX_401s:
		b->chan[0].type = T_UNIV_RS449;
		for (i=1; i<4; ++i)
			b->chan[i].type = T_SYNC_RS232;
		break;
	case CRONYX_801s:
		b->chan[0].type = T_UNIV_RS449;
		for (i=1; i<4; ++i)
			b->chan[i].type = T_SYNC_RS232;
		for (i=4; i<8; ++i)
			b->chan[i].type = T_UNIV_RS232;
		break;
	case CRONYX_404:
		b->chan[0].type = T_UNIV_RS449;
		for (i=1; i<4; ++i)
			b->chan[i].type = T_SYNC_RS449;
		break;
	case CRONYX_703:
		b->chan[0].type = T_UNIV_RS449;
		for (i=1; i<3; ++i)
			b->chan[i].type = T_SYNC_RS449;
		for (i=4; i<8; ++i)
			b->chan[i].type = T_UNIV_RS232;
		break;
	}

	/*------------------ Slave board -------------------*/

	if (chain) {
		switch (rev2) {
		case CRONYX_400:
			break;
		case CRONYX_100:
			b->chan[8].type = T_UNIV_RS232;
			break;
		case CRONYX_500:
			b->chan[8].type = T_UNIV_RS232;
			for (i=12; i<16; ++i)
				b->chan[i].type = T_UNIV_RS232;
			break;
		case CRONYX_410:
			b->chan[8].type = T_UNIV_V35;
			for (i=9; i<12; ++i)
				b->chan[i].type = T_UNIV_RS232;
			break;
		case CRONYX_810:
			b->chan[8].type = T_UNIV_V35;
			for (i=9; i<16; ++i)
				b->chan[i].type = T_UNIV_RS232;
			break;
		case CRONYX_410s:
			b->chan[8].type = T_UNIV_V35;
			for (i=9; i<12; ++i)
				b->chan[i].type = T_SYNC_RS232;
			break;
		case CRONYX_810s:
			b->chan[8].type = T_UNIV_V35;
			for (i=9; i<12; ++i)
				b->chan[i].type = T_SYNC_RS232;
			for (i=12; i<16; ++i)
				b->chan[i].type = T_UNIV_RS232;
			break;
		case CRONYX_440:
			b->chan[8].type = T_UNIV_V35;
			for (i=9; i<12; ++i)
				b->chan[i].type = T_SYNC_V35;
			break;
		case CRONYX_840:
			b->chan[8].type = T_UNIV_V35;
			for (i=9; i<12; ++i)
				b->chan[i].type = T_SYNC_V35;
			for (i=12; i<16; ++i)
				b->chan[i].type = T_UNIV_RS232;
			break;
		case CRONYX_401:
			b->chan[8].type = T_UNIV_RS449;
			for (i=9; i<12; ++i)
				b->chan[i].type = T_UNIV_RS232;
			break;
		case CRONYX_801:
			b->chan[8].type = T_UNIV_RS449;
			for (i=9; i<16; ++i)
				b->chan[i].type = T_UNIV_RS232;
			break;
		case CRONYX_401s:
			b->chan[8].type = T_UNIV_RS449;
			for (i=9; i<12; ++i)
				b->chan[i].type = T_UNIV_RS232;
			break;
		case CRONYX_801s:
			b->chan[8].type = T_UNIV_RS449;
			for (i=9; i<12; ++i)
				b->chan[i].type = T_SYNC_RS232;
			for (i=12; i<16; ++i)
				b->chan[i].type = T_UNIV_RS232;
			break;
		case CRONYX_404:
			b->chan[8].type = T_UNIV_RS449;
			for (i=9; i<12; ++i)
				b->chan[i].type = T_SYNC_RS449;
			break;
		case CRONYX_703:
			b->chan[8].type = T_UNIV_RS449;
			for (i=9; i<11; ++i)
				b->chan[i].type = T_SYNC_RS449;
			for (i=12; i<16; ++i)
				b->chan[i].type = T_UNIV_RS232;
			break;
		}
	}

	b->nuniv = b->nsync = b->nasync = 0;
	for (c=b->chan; c<b->chan+NCHAN; ++c)
		switch (c->type) {
		case T_ASYNC:      ++b->nasync; break;
		case T_UNIV:
		case T_UNIV_RS232:
		case T_UNIV_RS449:
		case T_UNIV_V35:   ++b->nuniv;  break;
		case T_SYNC_RS232:
		case T_SYNC_V35:
		case T_SYNC_RS449: ++b->nsync;  break;
		}

	cx_reinit_board (b);
}

/*
 * Initialize the Sigma-800 board structure.
 */
void cx_init_800 (cx_board_t *b, int num, port_t port, int irq, int dma,
	int chain)
{
	cx_chan_t *c;
	int i;

	/* Initialize board structure. */
	b->port = port;
	b->num = num;
	b->irq = irq;
	b->dma = dma;
	b->opt = board_opt_dflt;
	b->type = B_SIGMA_800;

	/* Set channels 0 and 8 mode, set DMA and IRQ. */
	b->bcr0 = b->bcr0b = dmamask[b->dma] | irqmask[b->irq];

	/* Clear DTR[0..7] and DTR[8..15]. */
	b->bcr1 = b->bcr1b = 0;

	strcpy (b->name, "800");
	if (chain)
		strcat (b->name, "/800");

	/* Initialize channel structures. */
	for (i=0; i<4; ++i) {
		b->chan[i+0].port  = CS0(port);
		b->chan[i+4].port  = cx_probe_chip (CS1A(port)) ?
			CS1A(port) : CS1(port);
		b->chan[i+8].port  = CS0(port+0x10);
		b->chan[i+12].port = CS1(port+0x10);
	}
	for (c=b->chan; c<b->chan+NCHAN; ++c) {
		c->board = b;
		c->num = c - b->chan;
		c->oscfreq = 33000000L;
		c->type = (c->num < 8 || chain) ? T_UNIV_RS232 : T_NONE;
	}

	b->nuniv = b->nsync = b->nasync = 0;
	for (c=b->chan; c<b->chan+NCHAN; ++c)
		switch (c->type) {
		case T_ASYNC:      ++b->nasync; break;
		case T_UNIV:
		case T_UNIV_RS232:
		case T_UNIV_RS449:
		case T_UNIV_V35:   ++b->nuniv;  break;
		case T_SYNC_RS232:
		case T_SYNC_V35:
		case T_SYNC_RS449: ++b->nsync;  break;
		}

	cx_reinit_board (b);
}

/*
 * Initialize the Sigma-2x board structure.
 */
void cx_init_2x (cx_board_t *b, int num, port_t port, int irq, int dma,
	int rev, int osc)
{
	cx_chan_t *c;
	int i;

	/* Initialize board structure. */
	b->port = port;
	b->num = num;
	b->irq = irq;
	b->dma = dma;
	b->opt = board_opt_dflt;

	b->type = B_SIGMA_2X;

	/* Set channels 0 and 8 mode, set DMA and IRQ. */
	b->bcr0 = BCR0_NORESET | dmamask[b->dma] | irqmask[b->irq];
	if (b->type == B_SIGMA_2X && b->opt.fast)
		b->bcr0 |= BCR02X_FAST;

	/* Clear DTR[0..3] and DTR[8..12]. */
	b->bcr1 = 0;

	/* Initialize channel structures. */
	for (i=0; i<4; ++i) {
		b->chan[i+0].port  = CS0(port);
		b->chan[i+4].port  = CS1(port);
		b->chan[i+8].port  = CS0(port+0x10);
		b->chan[i+12].port = CS1(port+0x10);
	}
	for (c=b->chan; c<b->chan+NCHAN; ++c) {
		c->board = b;
		c->num = c - b->chan;
		c->type = T_NONE;
		c->oscfreq = (osc & BSR2X_OSC_33) ? 33000000L : 20000000L;
	}

	/* Check the board revision code. */
	strcpy (b->name, "22");
	b->chan[0].type = T_UNIV;
	b->chan[1].type = T_UNIV;
	b->nsync = b->nasync = 0;
	b->nuniv = 2;
	if (rev == CRONYX_24) {
		strcpy (b->name, "24");
		b->chan[2].type = T_UNIV;
		b->chan[3].type = T_UNIV;
		b->nuniv += 2;
	}
	strcat (b->name, (osc & BSR2X_OSC_33) ? "c" : "a");
	cx_reinit_board (b);
}

/*
 * Reinitialize all channels, using new options and baud rate.
 */
void cx_reinit_board (cx_board_t *b)
{
	cx_chan_t *c;

	b->opt = board_opt_dflt;
	if (b->type == B_SIGMA_2X) {
		b->bcr0 &= ~BCR02X_FAST;
		if (b->opt.fast)
			b->bcr0 |= BCR02X_FAST;
	} else
		b->if0type = b->if8type = cx_iftype;
	for (c=b->chan; c<b->chan+NCHAN; ++c) {
		switch (c->type) {
		default:
		case T_NONE:
			continue;
		case T_UNIV:
		case T_UNIV_RS232:
		case T_UNIV_RS449:
		case T_UNIV_V35:
			c->mode = (cx_univ_mode == M_ASYNC) ?
				M_ASYNC : cx_sync_mode;
			break;
		case T_SYNC_RS232:
		case T_SYNC_V35:
		case T_SYNC_RS449:
			c->mode = cx_sync_mode;
			break;
		case T_ASYNC:
			c->mode = M_ASYNC;
			break;
		}
		c->rxbaud = cx_rxbaud;
		c->txbaud = cx_txbaud;
		c->opt = chan_opt_dflt;
		c->aopt = opt_async_dflt;
		c->hopt = opt_hdlc_dflt;
	}
}

/*
 * Set up the board.
 */
int cx_setup_board (cx_board_t *b, const unsigned char *firmware,
	long bits, const cr_dat_tst_t *tst)
{
	int i;
#ifndef NDIS_MINIPORT_DRIVER
	/* Disable DMA channel. */
	outb (DMA_MASK, (b->dma & 3) | DMA_MASK_CLEAR);
#endif
	/* Reset the controller. */
	outb (BCR0(b->port), 0);
	if (b->chan[8].type || b->chan[12].type)
		outb (BCR0(b->port+0x10), 0);

	/* Load the firmware. */
	if (b->type == B_SIGMA_800) {
		/* Reset the controllers. */
		outb (BCR2(b->port), BCR2_TMS);
		if (b->chan[8].type || b->chan[12].type)
			outb (BCR2(b->port+0x10), BCR2_TMS);
		outb (BCR2(b->port), 0);
		if (b->chan[8].type || b->chan[12].type)
			outb (BCR2(b->port+0x10), 0);

		if (firmware &&
		    (! cx_download (b->port, firmware, bits, tst) ||
		    ((b->chan[8].type || b->chan[12].type) &&
		    ! cx_download (b->port+0x10, firmware, bits, tst))))
			return (0);
	}

	/*
	 * Set channels 0 and 8 to RS232 async. mode.
	 * Enable DMA and IRQ.
	 */
	outb (BCR0(b->port), b->bcr0);
	if (b->chan[8].type || b->chan[12].type)
		outb (BCR0(b->port+0x10), b->bcr0b);

	/* Clear DTR[0..3] and DTR[8..12]. */
	outw (BCR1(b->port), b->bcr1);
	if (b->chan[8].type || b->chan[12].type)
		outw (BCR1(b->port+0x10), b->bcr1b);

	if (b->type == B_SIGMA_800)
		outb (BCR2(b->port), b->opt.fast &
			(BCR2_BUS0 | BCR2_BUS1));

	/* Initialize all controllers. */
	for (i=0; i<NCHAN; i+=4)
		if (b->chan[i].type != T_NONE)
			cx_setup_chip (b->chan + i);
#ifndef NDIS_MINIPORT_DRIVER
	/* Set up DMA channel to master mode. */
	outb (DMA_MODE, (b->dma & 3) | DMA_MODE_MASTER);

	/* Enable DMA channel. */
	outb (DMA_MASK, b->dma & 3);
#endif
	/* Initialize all channels. */
	for (i=0; i<NCHAN; ++i)
		if (b->chan[i].type != T_NONE)
			cx_setup_chan (b->chan + i);
	return (1);
}

/*
 * Initialize the board.
 */
static void cx_setup_chip (cx_chan_t *c)
{
	/* Reset the chip. */
	cx_reset (c->port);

	/*
	 * Set all interrupt level registers to the same value.
	 * This enables the internal CD2400 priority scheme.
	 */
	outb (RPILR(c->port), BRD_INTR_LEVEL);
	outb (TPILR(c->port), BRD_INTR_LEVEL);
	outb (MPILR(c->port), BRD_INTR_LEVEL);

	/* Set bus error count to zero. */
	outb (BERCNT(c->port), 0);

	/* Set 16-bit DMA mode. */
	outb (DMR(c->port), 0);

	/* Set timer period register to 1 msec (approximately). */
	outb (TPR(c->port), 10);
}

/*
 * Initialize the CD2400 channel.
 */
void cx_update_chan (cx_chan_t *c)
{
	int clock, period;

	if (c->board->type == B_SIGMA_XXX)
		switch (c->num) {
		case 0:
			c->board->bcr0 &= ~BCR0_UMASK;
			if (c->mode != M_ASYNC)
				c->board->bcr0 |= BCR0_UM_SYNC;
			if (c->board->if0type &&
			    (c->type==T_UNIV_RS449 || c->type==T_UNIV_V35))
				c->board->bcr0 |= BCR0_UI_RS449;
			outb (BCR0(c->board->port), c->board->bcr0);
			break;
		case 8:
			c->board->bcr0b &= ~BCR0_UMASK;
			if (c->mode != M_ASYNC)
				c->board->bcr0b |= BCR0_UM_SYNC;
			if (c->board->if8type &&
			    (c->type==T_UNIV_RS449 || c->type==T_UNIV_V35))
				c->board->bcr0b |= BCR0_UI_RS449;
			outb (BCR0(c->board->port+0x10), c->board->bcr0b);
			break;
		}

	/* set current channel number */
	outb (CAR(c->port), c->num & 3);

	switch (c->mode) {	/* initialize the channel mode */
	case M_ASYNC:
		/* set receiver timeout register */
		outw (RTPR(c->port), 10);          /* 10 msec, see TPR */
		c->opt.rcor.encod = ENCOD_NRZ;

		outb (CMR(c->port), CMR_RXDMA | CMR_TXDMA | CMR_ASYNC);
		outb (COR1(c->port), BYTE c->aopt.cor1);
		outb (COR2(c->port), BYTE c->aopt.cor2);
		outb (COR3(c->port), BYTE c->aopt.cor3);
		outb (COR6(c->port), BYTE c->aopt.cor6);
		outb (COR7(c->port), BYTE c->aopt.cor7);
		outb (SCHR1(c->port), c->aopt.schr1);
		outb (SCHR2(c->port), c->aopt.schr2);
		outb (SCHR3(c->port), c->aopt.schr3);
		outb (SCHR4(c->port), c->aopt.schr4);
		outb (SCRL(c->port), c->aopt.scrl);
		outb (SCRH(c->port), c->aopt.scrh);
		outb (LNXT(c->port), c->aopt.lnxt);
		break;
	case M_HDLC:
		outb (CMR(c->port), CMR_RXDMA | CMR_TXDMA | CMR_HDLC);
		outb (COR1(c->port), BYTE c->hopt.cor1);
		outb (COR2(c->port), BYTE c->hopt.cor2);
		outb (COR3(c->port), BYTE c->hopt.cor3);
		outb (RFAR1(c->port), c->hopt.rfar1);
		outb (RFAR2(c->port), c->hopt.rfar2);
		outb (RFAR3(c->port), c->hopt.rfar3);
		outb (RFAR4(c->port), c->hopt.rfar4);
		outb (CPSR(c->port), c->hopt.cpsr);
		break;
	}

	/* set mode-independent options */
	outb (COR4(c->port), BYTE c->opt.cor4);
	outb (COR5(c->port), BYTE c->opt.cor5);

	/* set up receiver clock values */
	if (c->mode == M_ASYNC || c->opt.rcor.dpll || c->opt.tcor.llm) {
		cx_clock (c->oscfreq, c->rxbaud, &clock, &period);
		c->opt.rcor.clk = clock;
	} else {
		c->opt.rcor.clk = CLK_EXT;
		period = 1;
	}
	outb (RCOR(c->port), BYTE c->opt.rcor);
	outb (RBPR(c->port), period);

	/* set up transmitter clock values */
	if (c->mode == M_ASYNC || !c->opt.tcor.ext1x) {
		unsigned ext1x = c->opt.tcor.ext1x;
		c->opt.tcor.ext1x = 0;
		cx_clock (c->oscfreq, c->txbaud, &clock, &period);
		c->opt.tcor.clk = clock;
		c->opt.tcor.ext1x = ext1x;
	} else {
		c->opt.tcor.clk = CLK_EXT;
		period = 1;
	}
	outb (TCOR(c->port), BYTE c->opt.tcor);
	outb (TBPR(c->port), period);
}

/*
 * Initialize the CD2400 channel.
 */
void cx_setup_chan (cx_chan_t *c)
{
	/* set current channel number */
	outb (CAR(c->port), c->num & 3);

	/* reset the channel */
	cx_cmd (c->port, CCR_CLRCH);

	/* set LIVR to contain the board and channel numbers */
	outb (LIVR(c->port), c->board->num << 6 | c->num << 2);

	/* clear DTR, RTS, set TXCout/DTR pin */
	outb (MSVR_RTS(c->port), 0);
	outb (MSVR_DTR(c->port), c->mode==M_ASYNC ? 0 : MSV_TXCOUT);

	/* set receiver A buffer physical address */
	outw (ARBADRU(c->port), (unsigned short) (c->arphys>>16));
	outw (ARBADRL(c->port), (unsigned short) c->arphys);

	/* set receiver B buffer physical address */
	outw (BRBADRU(c->port), (unsigned short) (c->brphys>>16));
	outw (BRBADRL(c->port), (unsigned short) c->brphys);

	/* set transmitter A buffer physical address */
	outw (ATBADRU(c->port), (unsigned short) (c->atphys>>16));
	outw (ATBADRL(c->port), (unsigned short) c->atphys);

	/* set transmitter B buffer physical address */
	outw (BTBADRU(c->port), (unsigned short) (c->btphys>>16));
	outw (BTBADRL(c->port), (unsigned short) c->btphys);

	c->dtr = 0;
	c->rts = 0;

	cx_update_chan (c);
}

/*
 * Control DTR signal for the channel.
 * Turn it on/off.
 */
void cx_set_dtr (cx_chan_t *c, int on)
{
	cx_board_t *b = c->board;

	c->dtr = on ? 1 : 0;

	if (b->type == B_SIGMA_2X) {
		if (on) b->bcr1 |= BCR1_DTR(c->num);
		else    b->bcr1 &= ~BCR1_DTR(c->num);
		outw (BCR1(b->port), b->bcr1);
		return;
	}
	if (b->type == B_SIGMA_800) {
		if (c->num >= 8) {
			if (on) b->bcr1b |= BCR1800_DTR(c->num);
			else    b->bcr1b &= ~BCR1800_DTR(c->num);
			outb (BCR1(b->port+0x10), b->bcr1b);
		} else {
			if (on) b->bcr1 |= BCR1800_DTR(c->num);
			else    b->bcr1 &= ~BCR1800_DTR(c->num);
			outb (BCR1(b->port), b->bcr1);
		}
		return;
	}
	if (c->mode == M_ASYNC) {
		outb (CAR(c->port), c->num & 3);
		outb (MSVR_DTR(c->port), on ? MSV_DTR : 0);
		return;
	}

	switch (c->num) {
	default:
		/* Channels 4..7 and 12..15 in synchronous mode
		 * have no DTR signal. */
		break;

	case 1: case 2:  case 3:
		if (c->type == T_UNIV_RS232)
			break;
	case 0:
		if (on) b->bcr1 |= BCR1_DTR(c->num);
		else    b->bcr1 &= ~BCR1_DTR(c->num);
		outw (BCR1(b->port), b->bcr1);
		break;

	case 9: case 10: case 11:
		if (c->type == T_UNIV_RS232)
			break;
	case 8:
		if (on) b->bcr1b |= BCR1_DTR(c->num & 3);
		else    b->bcr1b &= ~BCR1_DTR(c->num & 3);
		outw (BCR1(b->port+0x10), b->bcr1b);
		break;
	}
}

/*
 * Control RTS signal for the channel.
 * Turn it on/off.
 */
void cx_set_rts (cx_chan_t *c, int on)
{
	c->rts = on ? 1 : 0;
	outb (CAR(c->port), c->num & 3);
	outb (MSVR_RTS(c->port), on ? MSV_RTS : 0);
}

/*
 * Get the state of DSR signal of the channel.
 */
int cx_get_dsr (cx_chan_t *c)
{
	unsigned char sigval;

	if (c->board->type == B_SIGMA_2X ||
	    c->board->type == B_SIGMA_800 ||
	    c->mode == M_ASYNC) {
		outb (CAR(c->port), c->num & 3);
		return (inb (MSVR(c->port)) & MSV_DSR ? 1 : 0);
	}

	/*
	 * Channels 4..7 and 12..15 don't have DSR signal available.
	 */
	switch (c->num) {
	default:
		return (1);

	case 1: case 2:  case 3:
		if (c->type == T_UNIV_RS232)
			return (1);
	case 0:
		sigval = inw (BSR(c->board->port)) >> 8;
		break;

	case 9: case 10: case 11:
		if (c->type == T_UNIV_RS232)
			return (1);
	case 8:
		sigval = inw (BSR(c->board->port+0x10)) >> 8;
		break;
	}
	return (~sigval >> (c->num & 3) & 1);
}

/*
 * Get the state of CARRIER signal of the channel.
 */
int cx_get_cd (cx_chan_t *c)
{
	unsigned char sigval;

	if (c->board->type == B_SIGMA_2X ||
	    c->board->type == B_SIGMA_800 ||
	    c->mode == M_ASYNC) {
		outb (CAR(c->port), c->num & 3);
		return (inb (MSVR(c->port)) & MSV_CD ? 1 : 0);
	}

	/*
	 * Channels 4..7 and 12..15 don't have CD signal available.
	 */
	switch (c->num) {
	default:
		return (1);

	case 1: case 2:  case 3:
		if (c->type == T_UNIV_RS232)
			return (1);
	case 0:
		sigval = inw (BSR(c->board->port)) >> 8;
		break;

	case 9: case 10: case 11:
		if (c->type == T_UNIV_RS232)
			return (1);
	case 8:
		sigval = inw (BSR(c->board->port+0x10)) >> 8;
		break;
	}
	return (~sigval >> 4 >> (c->num & 3) & 1);
}

/*
 * Get the state of CTS signal of the channel.
 */
int cx_get_cts (cx_chan_t *c)
{
	outb (CAR(c->port), c->num & 3);
	return (inb (MSVR(c->port)) & MSV_CTS ? 1 : 0);
}

/*
 * Compute CD2400 clock values.
 */
void cx_clock (long hz, long ba, int *clk, int *div)
{
	static short clocktab[] = { 8, 32, 128, 512, 2048, 0 };

	for (*clk=0; clocktab[*clk]; ++*clk) {
		long c = ba * clocktab[*clk];
		if (hz <= c*256) {
			*div = (2 * hz + c) / (2 * c) - 1;
			return;
		}
	}
	/* Incorrect baud rate.  Return some meaningful values. */
	*clk = 0;
	*div = 255;
}

/*
 * Turn LED on/off.
 */
void cx_led (cx_board_t *b, int on)
{
	switch (b->type) {
	case B_SIGMA_2X:
		if (on) b->bcr0 |= BCR02X_LED;
		else    b->bcr0 &= ~BCR02X_LED;
		outb (BCR0(b->port), b->bcr0);
		break;
	}
}

void cx_disable_dma (cx_board_t *b)
{
#ifndef NDIS_MINIPORT_DRIVER
	/* Disable DMA channel. */
	outb (DMA_MASK, (b->dma & 3) | DMA_MASK_CLEAR);
#endif
}

cx_board_opt_t board_opt_dflt = { /* board options */
	BUS_NORMAL,		/* normal bus master timing */
};

cx_chan_opt_t chan_opt_dflt = { /* mode-independent options */
	{			/* cor4 */
		7,		/* FIFO threshold, odd is better */
		0,
		0,              /* don't detect 1 to 0 on CTS */
		1,		/* detect 1 to 0 on CD */
		0,              /* detect 1 to 0 on DSR */
	},
	{			/* cor5 */
		0,		/* receive flow control FIFO threshold */
		0,
		0,              /* don't detect 0 to 1 on CTS */
		1,		/* detect 0 to 1 on CD */
		0,              /* detect 0 to 1 on DSR */
	},
	{			/* rcor */
		0,		/* dummy clock source */
		ENCOD_NRZ,      /* NRZ mode */
		0,              /* disable DPLL */
		0,
		0,		/* transmit line value */
	},
	{			/* tcor */
		0,
		0,		/* local loopback mode */
		0,
		1,		/* external 1x clock mode */
		0,
		0,		/* dummy transmit clock source */
	},
};

cx_opt_async_t opt_async_dflt = { /* default async options */
	{			/* cor1 */
		8-1,		/* 8-bit char length */
		0,		/* don't ignore parity */
		PARM_NOPAR,	/* no parity */
		PAR_EVEN,	/* even parity */
	},
	{			/* cor2 */
		0,              /* disable automatic DSR */
		1,              /* enable automatic CTS */
		0,              /* disable automatic RTS */
		0,		/* no remote loopback */
		0,
		0,              /* disable embedded cmds */
		0,		/* disable XON/XOFF */
		0,		/* disable XANY */
	},
	{			/* cor3 */
		STOPB_1,	/* 1 stop bit */
		0,
		0,		/* disable special char detection */
		FLOWCC_PASS,	/* pass flow ctl chars to the host */
		0,		/* range detect disable */
		0,		/* disable extended spec. char detect */
	},
	{			/* cor6 */
		PERR_INTR,	/* generate exception on parity errors */
		BRK_INTR,	/* generate exception on break condition */
		0,		/* don't translate NL to CR on input */
		0,		/* don't translate CR to NL on input */
		0,		/* don't discard CR on input */
	},
	{			/* cor7 */
		0,		/* don't translate CR to NL on output */
		0,		/* don't translate NL to CR on output */
		0,
		0,		/* don't process flow ctl err chars */
		0,		/* disable LNext option */
		0,		/* don't strip 8 bit on input */
	},
	0, 0, 0, 0, 0, 0, 0,	/* clear schr1-4, scrl, scrh, lnxt */
};

cx_opt_hdlc_t opt_hdlc_dflt = { /* default hdlc options */
	{			/* cor1 */
		2,              /* 2 inter-frame flags */
		0,		/* no-address mode */
		CLRDET_DISABLE,	/* disable clear detect */
		AFLO_1OCT,	/* 1-byte address field length */
	},
	{			/* cor2 */
		0,		/* disable automatic DSR */
		0,              /* disable automatic CTS */
		0,              /* disable automatic RTS */
		0,
		CRC_INVERT,	/* use CRC V.41 */
		0,
		FCS_NOTPASS,	/* don't pass received CRC to the host */
		0,
	},
	{			/* cor3 */
		0,              /* 0 pad characters sent */
		IDLE_FLAG,	/* idle in flag */
		0,		/* enable FCS */
		FCSP_ONES,	/* FCS preset to all ones (V.41) */
		SYNC_AA,	/* use AAh as sync char */
		0,              /* disable pad characters */
	},
	0, 0, 0, 0,		/* clear rfar1-4 */
	POLY_V41,		/* use V.41 CRC polynomial */
};
