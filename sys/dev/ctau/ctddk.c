/*-
 * DDK library for Cronyx-Tau adapters.
 *
 * Copyright (C) 1998-1999 Cronyx Engineering.
 * Author: Alexander Kvitchenko, <aak@cronyx.ru>
 *
 * Copyright (C) 1999-2003 Cronyx Engineering.
 * Author: Roman Kurakin, <rik@cronyx.ru>
 *
 * This source is derived from
 * Diagnose utility for Cronyx-Tau adapter:
 * by Serge Vakulenko, <vak@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * Cronyx Id: ctddk.c,v 1.1.2.3 2003/11/14 16:55:36 rik Exp $
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/cx/machdep.h>
#include <dev/ctau/ctddk.h>
#include <dev/ctau/ctaureg.h>
#include <dev/ctau/hdc64570.h>
#include <dev/ctau/ds2153.h>
#include <dev/ctau/am8530.h>
#include <dev/ctau/lxt318.h>
#include <dev/cx/cronyxfw.h>
#include <dev/ctau/ctaufw.h>
#include <dev/ctau/ctau2fw.h>

#ifndef CT_DDK_NO_G703
#include <dev/ctau/ctaug7fw.h>
#endif

#ifndef CT_DDK_NO_E1
#include <dev/ctau/ctaue1fw.h>
#endif

static void ct_hdlc_interrupt (ct_chan_t *c, int imvr);
static void ct_e1_interrupt (ct_board_t *b);
static void ct_scc_interrupt (ct_board_t *b);
static void ct_e1timer_interrupt (ct_chan_t *c);

static short porttab [] = {	       /* standard base port set */
	0x200, 0x220, 0x240, 0x260, 0x280, 0x2a0, 0x2c0, 0x2e0,
	0x300, 0x320, 0x340, 0x360, 0x380, 0x3a0, 0x3c0, 0x3e0, 0
};

int ct_find (port_t *board_ports)
{
	int i, n;

	for (i=0, n=0; porttab[i] && n<NBRD; i++)
		if (ct_probe_board (porttab[i], -1, -1))
			board_ports[n++] = porttab[i];
	return n;
}

int ct_open_board (ct_board_t *b, int num, port_t port, int irq, int dma)
{
	ct_chan_t *c;
	const unsigned char *fw;
	const cr_dat_tst_t *ft;
	long flen;

	if (num >= NBRD || ! ct_probe_board (port, irq, dma))
		return 0;

	/* init callback pointers */
	for (c=b->chan; c<b->chan+NCHAN; ++c) {
		c->call_on_tx = 0;
		c->call_on_rx = 0;
		c->call_on_msig = 0;
		c->call_on_scc = 0;
		c->call_on_err = 0;
	}

	/* init DDK channel variables */
	for (c=b->chan; c<b->chan+NCHAN; ++c) {
		c->sccrx_empty = c->scctx_empty = 1;
		c->sccrx_b = c->sccrx_e = 0;
		c->scctx_b = c->scctx_e = 0;
		c->e1_first_int = 1;
	}

	/* init board structure */
	ct_init (b, num, port, irq, dma, ctau_fw_data, 
		ctau_fw_len, ctau_fw_tvec, ctau2_fw_data);

	/* determine which firmware should be loaded */
	fw = ctau_fw_data;
	flen = ctau_fw_len;
	ft = ctau_fw_tvec;
	switch (b->type) {
	case B_TAU2:
	case B_TAU2_G703:
	case B_TAU2_E1:
	case B_TAU2_E1D:
		fw = ctau2_fw_data;
		flen = 0;
		ft = NULL;
		break;
#ifndef CT_DDK_NO_G703
	case B_TAU_G703:
		fw = ctaug703_fw_data;
		flen = ctaug703_fw_len;
		ft = ctaug703_fw_tvec;
		break;
#endif
#ifndef CT_DDK_NO_E1
	case B_TAU_E1:
		fw = ctaue1_fw_data;
		flen = ctaue1_fw_len;
		ft = ctaue1_fw_tvec;
		break;
#endif
	}
	/* Load firmware and set up board */
	return ct_setup_board (b, fw, flen, ft);
}

/*
 * must be called on the exit
 */
void ct_close_board (ct_board_t *b)
{
	ct_setup_board (b, 0, 0, 0);

	/* Reset the controller. */
	outb (BCR0(b->port), 0);

	/* Disable DMA channel. */
	ct_disable_dma (b);

	ct_led (b, 0);
}

static void ct_g703_rate (ct_chan_t *c, unsigned long rate)
{
	c->gopt.rate = rate;
	ct_setup_g703 (c->board);
}

/*
 * Set up baud rate.
 */
static void ct_chan_baud (ct_chan_t *c, unsigned long baud)
{
	c->baud = baud;
	if (baud) {
		c->hopt.txs = CLK_INT;
	} else {
		ct_set_dpll (c, 0);
		c->hopt.txs = CLK_LINE;
	}
	ct_update_chan (c);
}

void ct_set_baud (ct_chan_t *c, unsigned long baud)
{
	unsigned long r;

	if (c->mode == M_E1)
		return;
	if (c->mode == M_G703) {
		if	(baud >= 2048000)  r = 2048;
		else if (baud >= 1024000)  r = 1024;
		else if (baud >= 512000)   r = 512;
		else if (baud >= 256000)   r = 256;
		else if (baud >= 128000)   r = 128;
		else			   r = 64;
		ct_g703_rate (c, r);
	} else
		ct_chan_baud (c, baud);
}

/*
 * Configure Tau/E1 board.
 */
static void ct_e1_config (ct_board_t *b, unsigned char cfg)
{
	if (cfg == b->opt.cfg)
		return;

	if (cfg == CFG_B)
		b->chan[1].mode = M_HDLC;
	else
		b->chan[1].mode = M_E1;

	/* Recovering synchronization */
	if (b->opt.cfg == CFG_B) {
		ct_chan_baud (b->chan+1, 0);
		ct_set_invtxc (b->chan+1, 0);
		ct_set_invrxc (b->chan+1, 0);
		ct_set_nrzi (b->chan+1, 0);
	}
	b->opt.cfg = cfg;
	ct_setup_e1 (b);
}

/*
 * Config Tau/G.703 board
 */
static void ct_g703_config (ct_board_t *b, unsigned char cfg)
{
	if (cfg == b->opt.cfg)
		return;

	if (cfg == CFG_B)
		b->chan[1].mode = M_HDLC;
	else
		b->chan[1].mode = M_G703;

	/* Recovering synchronization */
	if (b->opt.cfg == CFG_B) {
		ct_chan_baud (b->chan+1, 0);
		ct_set_invtxc (b->chan+1, 0);
		ct_set_invrxc (b->chan+1, 0);
		ct_set_nrzi (b->chan+1, 0);
	}
	b->opt.cfg = cfg;
	ct_setup_g703 (b);
}

int ct_set_clk (ct_chan_t *c, int clk)
{
	if (c->num)
		c->board->opt.clk1 = clk;
	else
		c->board->opt.clk0 = clk;
	if (c->mode == M_E1) {
		ct_setup_e1 (c->board);
		return 0;
	} if (c->mode == M_G703) {
		ct_setup_g703 (c->board);
		return 0;
	} else
		return -1;
}

int ct_get_clk (ct_chan_t *c)
{
	return c->num ? c->board->opt.clk1 : c->board->opt.clk0;
}

int ct_set_ts (ct_chan_t *c, unsigned long ts)
{
	if (! (c->mode == M_E1))
		return -1;
	if (c->num)
		c->board->opt.s1 = ts;
	else
		c->board->opt.s0 = ts;
	ct_setup_e1 (c->board);
	return 0;
}

int ct_set_subchan (ct_board_t *b, unsigned long ts)
{
	if (b->chan[0].mode != M_E1)
		return -1;
	b->opt.s2 = ts;
	ct_setup_e1 (b);
	return 0;
}

int ct_set_higain (ct_chan_t *c, int on)
{
	if (! (c->mode == M_E1))
		return -1;
	c->gopt.higain = on ? 1 : 0;
	ct_setup_e1 (c->board);
	return 0;
}

/*
 * Start service channel.
 */
void ct_start_scc (ct_chan_t *c, char *rxbuf, char *txbuf)
{
	c->sccrx = rxbuf;
	c->scctx = txbuf;

	/* Enable interrupts from service channel. */
	if (c->board->type != B_TAU_E1 && c->board->type != B_TAU_E1C &&
	    c->board->type != B_TAU2_E1)
		return;

	cte_out2 (c->board->port, c->num ? AM_IMR : AM_IMR | AM_A,
		 IMR_TX | IMR_RX_ALL);
	cte_out2 (c->board->port, AM_MICR, MICR_MIE);
}

/*
 * Start HDLC channel.
 */
void ct_start_chan (ct_chan_t *c, ct_buf_t *cb, unsigned long phys)
{
	int i, ier0;
	unsigned long bound;

	if (cb) {
		/* Set up descriptors, align to 64k boundary.
		 * If 64k boundary is inside buffers
		 * buffers will begin on this boundary
		 * (there were allocated additional space for this) */
		c->tdesc = cb->descbuf;
		c->tdphys[0] = phys + ((char*)c->tdesc - (char*)cb);
		bound = ((c->tdphys[0] + 0xffff) & ~(0xffffUL));
		if (bound < c->tdphys[0] + 2*NBUF*sizeof(ct_desc_t)) {
			c->tdesc = (ct_desc_t*) ((char*) c->tdesc +
				(bound - c->tdphys[0]));
			c->tdphys[0] = bound;
		}
		c->rdesc = c->tdesc + NBUF;

		/* Set buffers. */
		for (i=0; i<NBUF; ++i) {
			c->rbuf[i] = cb->rbuffer[i];
			c->tbuf[i] = cb->tbuffer[i];
		}

		/* Set buffer physical addresses */
		for (i=0; i<NBUF; ++i) {
			c->rphys[i] = phys + ((char*)c->rbuf[i] - (char*)cb);
			c->tphys[i] = phys + ((char*)c->tbuf[i] - (char*)cb);
			c->rdphys[i] = phys + ((char*)(c->rdesc+i) - (char*)cb);
			c->tdphys[i] = phys + ((char*)(c->tdesc+i) - (char*)cb);
		}
	}
	/* Set up block chains. */
	/* receive buffers */
	for (i=0; i<NBUF; ++i) {
		B_NEXT (c->rdesc[i]) = c->rdphys[(i+1) % NBUF] & 0xffff;
		B_PTR (c->rdesc[i]) = c->rphys[i];
		B_LEN (c->rdesc[i]) = DMABUFSZ;
		B_STATUS (c->rdesc[i]) = 0;
	}
	/* transmit buffers */
	for (i=0; i<NBUF; ++i) {
		B_NEXT (c->tdesc[i]) = c->tdphys[(i+1) % NBUF] & 0xffff;
		B_PTR (c->tdesc[i]) = c->tphys[i];
		B_LEN (c->tdesc[i]) = DMABUFSZ;
		B_STATUS (c->tdesc[i]) = FST_EOM;
		c->attach[i] = 0;
	}

	if (c->type & T_E1) {
		c->mode = M_E1;
		if (c->num && c->board->opt.cfg == CFG_B)
			c->mode = M_HDLC;
	}
	if (c->type & T_G703) {
		c->mode = M_G703;
		if (c->num && c->board->opt.cfg == CFG_B)
			c->mode = M_HDLC;
	}
	ct_update_chan (c);

	/* enable receiver */
	c->rn = 0;
	ct_start_receiver (c, 1 , c->rphys[0], DMABUFSZ, c->rdphys[0],
		c->rdphys[NBUF-1]);
	outb (c->IE1, inb (c->IE1) | IE1_CDCDE);
	outb (c->IE0, inb (c->IE0) | IE0_RX_INTE);
	ier0 = inb (IER0(c->board->port));
	ier0 |= c->num ? IER0_RX_INTE_1 : IER0_RX_INTE_0;
	outb (IER0(c->board->port), ier0);

	/* Enable transmitter */
	c->tn = 0;
	c->te = 0;
	ct_start_transmitter (c, 1 , c->tphys[0], DMABUFSZ, c->tdphys[0],
		c->tdphys[0]);
	outb (c->TX.DIR, DIR_CHAIN_EOME | DIR_CHAIN_BOFE | DIR_CHAIN_COFE);

	/* Clear DTR and RTS */
	ct_set_dtr (c, 0);
	ct_set_rts (c, 0);
}

/*
 * Turn receiver on/off
 */
void ct_enable_receive (ct_chan_t *c, int on)
{
	unsigned char st3, ier0, ier1;

	st3 = inb (c->ST3);
	/* enable or disable receiver */
	if (on && ! (st3 & ST3_RX_ENABLED)) {
		c->rn = 0;
		ct_start_receiver (c, 1 , c->rphys[0], DMABUFSZ, c->rdphys[0],
			c->rdphys[NBUF-1]);
		/* enable status interrupt */
		outb (c->IE1, inb (c->IE1) | IE1_CDCDE);
		outb (c->IE0, inb (c->IE0) | IE0_RX_INTE);
		ier0 = inb (IER0(c->board->port));
		ier0 |= c->num ? IER0_RX_INTE_1 : IER0_RX_INTE_0;
		outb (IER0(c->board->port), ier0);
		ct_set_rts (c, 1);
	} else if (! on && (st3 & ST3_RX_ENABLED)) {
		ct_set_rts (c, 0);
		outb (c->CMD, CMD_RX_DISABLE);

		ier0 = inb (IER0(c->board->port));
		ier0 &= c->num ? ~(IER0_RX_INTE_1 | IER0_RX_RDYE_1) :
			~(IER0_RX_INTE_0 | IER0_RX_RDYE_0);
		outb (IER0(c->board->port), ier0);

		ier1 = inb (IER1(c->board->port));
		ier1 &= c->num ? ~(IER1_RX_DMERE_1 | IER1_RX_DME_1) :
			~(IER1_RX_DMERE_0 | IER1_RX_DME_0);
		outb (IER1(c->board->port), ier1);
	}

}

/*
 * Turn transmitter on/off
 */
void ct_enable_transmit (ct_chan_t *c, int on)
{
	unsigned char st3, ier0, ier1;

	st3 = inb (c->ST3);
	/* enable or disable receiver */
	if (on && ! (st3 & ST3_TX_ENABLED)) {
		c->tn = 0;
		c->te = 0;
		ct_start_transmitter (c, 1 , c->tphys[0], DMABUFSZ,
			c->tdphys[0], c->tdphys[0]);
		outb (c->TX.DIR,
			DIR_CHAIN_EOME | DIR_CHAIN_BOFE | DIR_CHAIN_COFE);
	} else if (! on && (st3 & ST3_TX_ENABLED)) {
		outb (c->CMD, CMD_TX_DISABLE);

		ier0 = inb (IER0(c->board->port));
		ier0 &= c->num ? ~(IER0_TX_INTE_1 | IER0_TX_RDYE_1) :
			~(IER0_TX_INTE_0 | IER0_TX_RDYE_0);
		outb (IER0(c->board->port), ier0);

		ier1 = inb (IER1(c->board->port));
		ier1 &= c->num ? ~(IER1_TX_DMERE_1 | IER1_TX_DME_1) :
			~(IER1_TX_DMERE_0 | IER1_TX_DME_0);
		outb (IER1(c->board->port), ier1);
	}

}

int ct_set_config (ct_board_t *b, int cfg)
{
	if (b->opt.cfg == cfg)
		return 0;
	switch (b->type) {
	case B_TAU_G703:
	case B_TAU_G703C:
	case B_TAU2_G703:
		if (cfg == CFG_C)
			return -1;
		ct_g703_config (b, cfg);
		return 0;
	case B_TAU_E1:
	case B_TAU_E1C:
	case B_TAU_E1D:
	case B_TAU2_E1:
	case B_TAU2_E1D:
		ct_e1_config (b, cfg);
		return 0;
	default:
		return cfg == CFG_A ? 0 : -1;
	}
}

int ct_get_dpll (ct_chan_t *c)
{
	return (c->hopt.rxs == CLK_RXS_DPLL_INT);
}

void ct_set_dpll (ct_chan_t *c, int on)
{
	if (on && ct_get_baud (c))
		c->hopt.rxs = CLK_RXS_DPLL_INT;
	else
		c->hopt.rxs = CLK_LINE;
	ct_update_chan (c);
}

int ct_get_nrzi (ct_chan_t *c)
{
	return (c->opt.md2.encod == MD2_ENCOD_NRZI);
}

/*
 * Change line encoding to NRZI, default is NRZ
 */
void ct_set_nrzi (ct_chan_t *c, int on)
{
	c->opt.md2.encod = on ? MD2_ENCOD_NRZI : MD2_ENCOD_NRZ;
	outb (c->MD2, *(unsigned char*)&c->opt.md2);
}

/*
 * Transmit clock inversion
 */
void ct_set_invtxc (ct_chan_t *c, int on)
{
	if (on) c->board->bcr2 |=  (c->num ? BCR2_INVTXC1 : BCR2_INVTXC0);
	else	c->board->bcr2 &= ~(c->num ? BCR2_INVTXC1 : BCR2_INVTXC0);
	outb (BCR2(c->board->port), c->board->bcr2);
}

int ct_get_invtxc (ct_chan_t *c)
{
	return (c->board->bcr2 & (c->num ? BCR2_INVTXC1 : BCR2_INVTXC0)) != 0;
}

/*
 * Receive clock inversion
 */
void ct_set_invrxc (ct_chan_t *c, int on)
{
	if (on) c->board->bcr2 |=  (c->num ? BCR2_INVRXC1 : BCR2_INVRXC0);
	else	c->board->bcr2 &= ~(c->num ? BCR2_INVRXC1 : BCR2_INVRXC0);
	outb (BCR2(c->board->port), c->board->bcr2);
}

int ct_get_invrxc (ct_chan_t *c)
{
	return (c->board->bcr2 & (c->num ? BCR2_INVRXC1 : BCR2_INVRXC0)) != 0;
}

/*
 * Main interrupt handler
 */
void ct_int_handler (ct_board_t *b)
{
	unsigned char bsr0, imvr;
	ct_chan_t *c;

	while ((bsr0 = inb (BSR0(b->port))) & BSR0_INTR) {
		if (bsr0 & BSR0_RDYERR) {
			outb (BCR1(b->port), b->bcr1);
		} else if (bsr0 & BSR0_GINT) {
			if (b->type == B_TAU_E1 || b->type == B_TAU_E1C ||
			    b->type == B_TAU_E1D || b->type == B_TAU2_E1 ||
			    b->type == B_TAU2_E1D)
				ct_e1_interrupt (b);
		} else if (bsr0 & BSR0_HDINT) {
			/* Read the interrupt modified vector register. */
			imvr = inb (IACK(b->port));
			c = b->chan + (imvr & IMVR_CHAN1 ? 1 : 0);
			ct_hdlc_interrupt (c, imvr);
		}
	}
}

static void ct_e1_interrupt (ct_board_t *b)
{
	unsigned char sr;

	sr = inb (E1SR(b->port));

	if (sr & E1SR_SCC_IRQ) ct_scc_interrupt (b);
	if (sr & E1SR_E0_IRQ1) ct_e1timer_interrupt (b->chan + 0);
	if (sr & E1SR_E1_IRQ1) ct_e1timer_interrupt (b->chan + 1);
}

static void ct_scc_interrupt (ct_board_t *b)
{
	unsigned char rsr;
	unsigned char ivr, a = AM_A;		/* assume channel A */
	ct_chan_t *c = b->chan;

	ivr = cte_in2 (b->port, AM_IVR);
	if (! (ivr & IVR_A))
		++c, a = 0;			/* really channel B */

	switch (ivr & IVR_REASON) {
	case IVR_TXRDY: 			/* transmitter empty */
		c->scctx_b = (c->scctx_b + 1) % SCCBUFSZ;
		if (c->scctx_b == c->scctx_e) {
			c->scctx_empty = 1;
			cte_out2c (c, AM_CR | CR_RST_TXINT);
		} else
			cte_out2d (c, c->scctx[c->scctx_b]);
		break;

	case IVR_RXERR: 		/* receive error */
	case IVR_RX:			/* receive character available */
		rsr = cte_in2 (b->port, a|AM_RSR);

		if (rsr & RSR_RXOVRN) { 	/* rx overrun */
			if (c->call_on_err)
				c->call_on_err (c, CT_SCC_OVERRUN);
		} else if (rsr & RSR_FRME) {	/* frame error */
			if (c->call_on_err)
				c->call_on_err (c, CT_SCC_FRAME);
		} else {
			c->sccrx[c->sccrx_e] = cte_in2d (c);
			c->sccrx_e = (c->sccrx_e + 1) % SCCBUFSZ;
			c->sccrx_empty &= 0;
			if (c->call_on_scc)
				c->call_on_scc (c);
			if (c->sccrx_e == c->sccrx_b && ! c->sccrx_empty)
				if (c->call_on_err)
					c->call_on_err (c, CT_SCC_OVERFLOW);
		}
		if (rsr)
			cte_out2c (c, CR_RST_ERROR);
		break;

	case IVR_STATUS:		/* external status interrupt */
		/* Unexpected SCC status interrupt. */
		cte_out2c (c, CR_RST_EXTINT);
		break;
	}
}

/*
 * G.703 mode channel: process 1-second timer interrupts.
 * Read error and request registers, and fill the status field.
 */
void ct_g703_timer (ct_chan_t *c)
{
	int bpv, cd, tsterr, tstreq;

	/* Count seconds.
	 * During the first second after the channel startup
	 * the status registers are not stable yet,
	 * we will so skip the first second. */
	++c->cursec;
	if (c->mode != M_G703)
		return;
	if (c->totsec + c->cursec <= 1)
		return;
	c->status = 0;

	cd = ct_get_cd (c);

	bpv = inb (GERR (c->board->port)) & (c->num ? GERR_BPV1 : GERR_BPV0);
	outb (GERR (c->board->port), bpv);

	tsterr = inb (GERR (c->board->port)) & (c->num ? GERR_ERR1 : GERR_ERR0);
	outb (GERR (c->board->port), tsterr);

	tstreq = inb (GLDR (c->board->port)) &
		(c->num ? GLDR_LREQ1 : GLDR_LREQ0);
	outb (GLDR (c->board->port), tstreq);

	/* Compute the SNMP-compatible channel status. */
	if (bpv)
		++c->currnt.bpv;	  /* bipolar violation */
	if (! cd)
		c->status |= ESTS_LOS;	  /* loss of signal */
	if (tsterr)
		c->status |= ESTS_TSTERR; /* test error */
	if (tstreq)
		c->status |= ESTS_TSTREQ; /* test code detected */

	if (! c->status)
		c->status = ESTS_NOALARM;

	/* Unavaiable second -- loss of carrier, or receiving test code. */
	if ((! cd) || tstreq)
		/* Unavailable second -- no other counters. */
		++c->currnt.uas;
	else {
		/* Line errored second -- any BPV. */
		if (bpv)
			++c->currnt.les;

		/* Collect data for computing
		 * degraded minutes. */
		++c->degsec;
		if (cd && bpv)
			++c->degerr;
	}

	/* Degraded minutes -- having more than 50% error intervals. */
	if (c->cursec / 60 == 0) {
		if (c->degerr*2 > c->degsec)
			++c->currnt.dm;
		c->degsec = 0;
		c->degerr = 0;
	}

	/* Rotate statistics every 15 minutes. */
	if (c->cursec > 15*60) {
		int i;

		for (i=47; i>0; --i)
			c->interval[i] = c->interval[i-1];
		c->interval[0] = c->currnt;

		/* Accumulate total statistics. */
		c->total.bpv   += c->currnt.bpv;
		c->total.fse   += c->currnt.fse;
		c->total.crce  += c->currnt.crce;
		c->total.rcrce += c->currnt.rcrce;
		c->total.uas   += c->currnt.uas;
		c->total.les   += c->currnt.les;
		c->total.es    += c->currnt.es;
		c->total.bes   += c->currnt.bes;
		c->total.ses   += c->currnt.ses;
		c->total.oofs  += c->currnt.oofs;
		c->total.css   += c->currnt.css;
		c->total.dm    += c->currnt.dm;
		memset (&c->currnt, 0, sizeof (c->currnt));

		c->totsec += c->cursec;
		c->cursec = 0;
	}
}

static void ct_e1timer_interrupt (ct_chan_t *c)
{
	unsigned short port;
	unsigned char sr1, sr2, ssr;
	unsigned long bpv, fas, crc4, ebit, pcv, oof;

	port = c->num ? E1CS1(c->board->port) : E1CS0(c->board->port);

	sr2 = cte_ins (port, DS_SR2, 0xff);
	/* is it timer interrupt ? */
	if (! (sr2 & SR2_SEC))
		return;

	/* first interrupts should be ignored */
	if (c->e1_first_int > 0) {
		c->e1_first_int --;
		return;
	}

	++c->cursec;
	c->status = 0;

	/* Compute the SNMP-compatible channel status. */
	sr1 = cte_ins (port, DS_SR1, 0xff);
	ssr = cte_in (port, DS_SSR);
	oof = 0;

	if (sr1 & (SR1_RCL | SR1_RLOS))
		c->status |= ESTS_LOS;		/* loss of signal */
	if (sr1 & SR1_RUA1)
		c->status |= ESTS_AIS;		/* receiving all ones */
	if (c->gopt.cas && (sr1 & SR1_RSA1))
		c->status |= ESTS_AIS16;	/* signaling all ones */
	if (c->gopt.cas && (sr1 & SR1_RDMA))
		c->status |= ESTS_FARLOMF;	/* alarm in timeslot 16 */
	if (sr1 & SR1_RRA)
		c->status |= ESTS_FARLOF;	/* far loss of framing */

	/* Controlled slip second -- any slip event. */
	if (sr1 & SR1_RSLIP) {
		++c->currnt.css;
	}

	if (ssr & SSR_SYNC) {
		c->status |= ESTS_LOF;		/* loss of framing */
		++oof;				/* out of framing */
	}
	if ((c->gopt.cas && (ssr & SSR_SYNC_CAS)) ||
	    (c->gopt.crc4 && (ssr & SSR_SYNC_CRC4))) {
		c->status |= ESTS_LOMF; 	/* loss of multiframing */
		++oof;				/* out of framing */
	}

	if (! c->status)
		c->status = ESTS_NOALARM;

	/* Get error counters. */
	bpv = VCR (cte_in (port, DS_VCR1), cte_in (port, DS_VCR2));
	fas = FASCR (cte_in (port, DS_FASCR1), cte_in (port, DS_FASCR2));
	crc4 = CRCCR (cte_in (port, DS_CRCCR1), cte_in (port, DS_CRCCR2));
	ebit = EBCR (cte_in (port, DS_EBCR1), cte_in (port, DS_EBCR2));

	c->currnt.bpv += bpv;
	c->currnt.fse += fas;
	if (c->gopt.crc4) {
		c->currnt.crce += crc4;
		c->currnt.rcrce += ebit;
	}

	/* Path code violation is frame sync error if CRC4 disabled,
	 * or CRC error if CRC4 enabled. */
	pcv = fas;
	if (c->gopt.crc4)
		pcv += crc4;

	/* Unavaiable second -- receiving all ones, or
	 * loss of carrier, or loss of signal. */
	if (sr1 & (SR1_RUA1 | SR1_RCL | SR1_RLOS))
		/* Unavailable second -- no other counters. */
		++c->currnt.uas;
	else {
		/* Line errored second -- any BPV. */
		if (bpv)
			++c->currnt.les;

		/* Errored second -- any PCV, or out of frame sync,
		 * or any slip events. */
		if (pcv || oof || (sr1 & SR1_RSLIP))
			++c->currnt.es;

		/* Severely errored framing second -- out of frame sync. */
		if (oof)
			++c->currnt.oofs;

		/* Severely errored seconds --
		 * 832 or more PCVs, or 2048 or more BPVs. */
		if (bpv >= 2048 || pcv >= 832)
			++c->currnt.ses;
		else {
			/* Bursty errored seconds --
			 * no SES and more than 1 PCV. */
			if (pcv > 1)
				++c->currnt.bes;

			/* Collect data for computing
			 * degraded minutes. */
			++c->degsec;
			c->degerr += bpv + pcv;
		}
	}

	/* Degraded minutes -- having error rate more than 10e-6,
	 * not counting unavailable and severely errored seconds. */
	if (c->cursec / 60 == 0) {
		if (c->degerr > c->degsec * 2048 / 1000)
			++c->currnt.dm;
		c->degsec = 0;
		c->degerr = 0;
	}

	/* Rotate statistics every 15 minutes. */
	if (c->cursec > 15*60) {
		int i;

		for (i=47; i>0; --i)
			c->interval[i] = c->interval[i-1];
		c->interval[0] = c->currnt;

		/* Accumulate total statistics. */
		c->total.bpv   += c->currnt.bpv;
		c->total.fse   += c->currnt.fse;
		c->total.crce  += c->currnt.crce;
		c->total.rcrce += c->currnt.rcrce;
		c->total.uas   += c->currnt.uas;
		c->total.les   += c->currnt.les;
		c->total.es    += c->currnt.es;
		c->total.bes   += c->currnt.bes;
		c->total.ses   += c->currnt.ses;
		c->total.oofs  += c->currnt.oofs;
		c->total.css   += c->currnt.css;
		c->total.dm    += c->currnt.dm;
		for (i=0; i<sizeof (c->currnt); ++i)
			*(((char *)(&c->currnt))+i)=0;

		c->totsec += c->cursec;
		c->cursec = 0;
	}
}

static void ct_hdlc_interrupt (ct_chan_t *c, int imvr)
{
	int i, dsr, st1, st2, cda;

	switch (imvr & IMVR_VECT_MASK) {
	case IMVR_RX_DMOK:		/* receive DMA normal end */
		dsr = inb (c->RX.DSR);
		cda = inw (c->RX.CDA);
		for (i=0; i<NBUF; ++i)
			if (cda == (unsigned short) c->rdphys[i])
				break;
		if (i >= NBUF)
			i = c->rn; /* cannot happen */
		while (c->rn != i) {
			int cst = B_STATUS (c->rdesc[c->rn]);
			if (cst == FST_EOM) {
				/* process data */
				if (c->call_on_rx)
					 c->call_on_rx (c, c->rbuf[c->rn],
						B_LEN(c->rdesc[c->rn]));
				++c->ipkts;
				c->ibytes += B_LEN(c->rdesc[c->rn]);
			} else if (cst & ST2_OVRN) {
				/* Receive overrun error */
				if (c->call_on_err)
					c->call_on_err (c, CT_OVERRUN);
				++c->ierrs;
			} else if (cst & (ST2_HDLC_RBIT |
				ST2_HDLC_ABT | ST2_HDLC_SHRT)) {
				/* Receive frame error */
				if (c->call_on_err)
					c->call_on_err (c, CT_FRAME);
				++c->ierrs;
			} else if ((cst & ST2_HDLC_EOM)
				&& (cst & ST2_HDLC_CRCE)) {
				/* Receive CRC error */
				if (c->call_on_err)
					c->call_on_err (c, CT_CRC);
				++c->ierrs;
			} else if (! (cst & ST2_HDLC_EOM)) {
				/* Frame dose not fit in the buffer.*/
				if (c->call_on_err)
					c->call_on_err (c, CT_OVERFLOW);
				++c->ierrs;
			}

			B_NEXT (c->rdesc[c->rn]) =
				c->rdphys[(c->rn+1) % NBUF] & 0xffff;
			B_PTR (c->rdesc[c->rn]) = c->rphys[c->rn];
			B_LEN (c->rdesc[c->rn]) = DMABUFSZ;
			B_STATUS (c->rdesc[c->rn]) = 0;
			c->rn = (c->rn + 1) % NBUF;
		}
		outw (c->RX.EDA, (unsigned short) c->rdphys[(i+NBUF-1)%NBUF]);
		/* Clear DMA interrupt. */
		if (inb (c->RX.DSR) & DSR_DMA_ENABLE) {
			outb (c->RX.DSR, dsr);
		} else {
			outb (c->RX.DSR, (dsr & 0xfc) | DSR_DMA_ENABLE);
		}
		++c->rintr;
		break;

	case IMVR_RX_INT:		/* receive status */
		st1 = inb (c->ST1);
		st2 = inb (c->ST2);
		if (st1 & ST1_CDCD){
			if (c->call_on_msig)
				c->call_on_msig (c);
			++c->mintr;
		}
		/* Clear interrupt. */
		outb (c->ST1, st1);
		outb (c->ST2, st2);
		++c->rintr;
		break;

	case IMVR_RX_DMERR:		/* receive DMA error */
		dsr = inb (c->RX.DSR);
		if (dsr & (DSR_CHAIN_BOF | DSR_CHAIN_COF)) {
			if (c->call_on_err)
				c->call_on_err (c, CT_OVERFLOW);
			++c->ierrs;
			for (i=0; i<NBUF; ++i) {
				B_LEN (c->rdesc[i]) = DMABUFSZ;
				B_STATUS (c->rdesc[i]) = 0;
			}
			ct_start_receiver (c, 1, c->rphys[0], DMABUFSZ,
				c->rdphys[0], c->rdphys[NBUF-1]);
			c->rn = 0;
		}
		/* Clear DMA interrupt. */
		outb (c->RX.DSR, dsr);
		++c->rintr;
		break;

	case IMVR_TX_DMOK:		/* transmit DMA normal end */
	case IMVR_TX_DMERR:		/* transmit DMA error	   */
		dsr = inb (c->TX.DSR);
		cda = inw (c->TX.CDA);

		for (i=0; i<NBUF && cda != (unsigned short)c->tdphys[i]; ++i)
			continue;
		if (i >= NBUF)
			i = 1; /* cannot happen */
		if (dsr & DSR_CHAIN_COF) {
			if (c->call_on_err)
				c->call_on_err (c, CT_UNDERRUN);
			++c->oerrs;
		}
		while (c->tn != i) {
			if (c->call_on_tx)
				c->call_on_tx (c, c->attach[c->tn],
					B_LEN(c->tdesc[c->tn]));
			++c->opkts;
			c->obytes += B_LEN(c->tdesc[c->tn]);

			c->tn = (c->tn + 1) % NBUF;
			/* Clear DMA interrupt. */
			outb (c->TX.DSR, DSR_CHAIN_EOM | DSR_DMA_CONTINUE);
		}
		outb (c->TX.DSR, dsr & ~DSR_CHAIN_EOM);
		++c->tintr;
		break;

	case IMVR_TX_INT:		/* transmit error, HDLC only */
		st1 = inb (c->ST1);
		if (st1 & ST1_HDLC_UDRN) {
			if (c->call_on_err)
				c->call_on_err (c, CT_UNDERRUN);
			++c->oerrs;
		}
		outb (c->ST1, st1);
		++c->tintr;
		break;

	default:
		/* Unknown interrupt - cannot happen. */
		break;
	}
}

int ct_receive_enabled (ct_chan_t *c)
{
	int st3;

	st3 = inb (c->ST3);
	return (st3 & ST3_RX_ENABLED) ? 1 : 0;
}

int ct_transmit_enabled (ct_chan_t *c)
{
	int st3;

	st3 = inb (c->ST3);
	return (st3 & ST3_TX_ENABLED) ? 1 : 0;
}

int ct_buf_free (ct_chan_t *c)
{
	return (NBUF + c->tn - c->te - 1) % NBUF;
}

int ct_send_packet (ct_chan_t *c, unsigned char *data, int len,
	void *attachment)
{
	int dsr, ne;

	if (len > DMABUFSZ)
		return -2;

	/* Is it really free? */
	ne = (c->te+1) % NBUF;
	if (ne == c->tn)
		return -1;

	/* Set up the tx descriptor. */
	B_LEN (c->tdesc[c->te]) = len;
	B_STATUS (c->tdesc[c->te]) = FST_EOM;
	c->attach[c->te] = attachment;
	if (c->tbuf[c->te] != data)
		memcpy (c->tbuf[c->te], data, len);

	/* Start the transmitter. */
	c->te = ne;
	outw (c->TX.EDA, (unsigned short) c->tdphys[ne]);
	dsr = inb (c->TX.DSR);
	if (! (dsr & DSR_DMA_ENABLE))
		outb (c->TX.DSR, DSR_DMA_ENABLE);
	return 0;
}

int scc_write (ct_chan_t *c, unsigned char *d, int len)
{
	int i, free;

	/* determining free place in buffer */
	if (c->scctx_empty)
		free = SCCBUFSZ;
	else
		free = (SCCBUFSZ + c->scctx_b - c->scctx_e) % SCCBUFSZ;

	if (len > free)
		return -1;

	for (i=0; i<len; i++){
		c->scctx[c->scctx_e] = d[i];
		c->scctx_e = (c->scctx_e+1) % SCCBUFSZ;
	}
	if (c->scctx_empty && len) {
		cte_out2d (c, c->scctx[c->scctx_b]);
		c->scctx_empty = 0;
	}
	return 0;
}

int scc_read (ct_chan_t *c, unsigned char *d, int len)
{
	int i, bytes;

	if (c->sccrx_empty)
		bytes = 0;
	else
		bytes = (SCCBUFSZ + c->sccrx_e - 1 - c->sccrx_b) %
				SCCBUFSZ + 1;
	if (len > bytes)
		return -1;

	for (i=0; i<len; i++){
		d[i] = c->sccrx[c->sccrx_b];
		c->sccrx_b = (c->sccrx_b+1) % SCCBUFSZ;
	}
	if (c->sccrx_b==c->sccrx_e)
		c->sccrx_empty = 1;
	return 0;
}

int sccrx_check (ct_chan_t *c)
{
	int bytes;

	if (c->sccrx_empty)
		bytes = 0;
	else
		bytes = (SCCBUFSZ + c->sccrx_e - 1 - c->sccrx_b) %
				SCCBUFSZ + 1;
	return bytes;
}

int scc_read_byte (ct_chan_t *c)
{
	unsigned char a;

	if (scc_read (c, &a, 1) < 0)
		return -1;
	return a;
}

int scc_write_byte (ct_chan_t *c, unsigned char b)
{
	if (scc_write (c, &b, 1) < 0)
		return -1;
	return b;
}

/*
 * Register event processing functions
 */
void ct_register_transmit (ct_chan_t *c, void (*func) (ct_chan_t*, void*, int))
{
	c->call_on_tx = func;
}

void ct_register_receive (ct_chan_t *c, void (*func) (ct_chan_t*, char*, int))
{
	c->call_on_rx = func;
}

void ct_register_error (ct_chan_t *c, void (*func) (ct_chan_t*, int))
{
	c->call_on_err = func;
}

void ct_register_scc (ct_chan_t *c, void (*func) (ct_chan_t*))
{
	c->call_on_scc = func;
}

void ct_register_modem (ct_chan_t *c, void (*func) (ct_chan_t*))
{
	c->call_on_msig = func;
}
