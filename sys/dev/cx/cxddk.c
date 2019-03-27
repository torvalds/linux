/*-
 * Cronyx-Sigma Driver Development Kit.
 *
 * Copyright (C) 1998 Cronyx Engineering.
 * Author: Pavel Novikov, <pavel@inr.net.kiae.su>
 *
 * Copyright (C) 1998-2003 Cronyx Engineering.
 * Author: Roman Kurakin, <rik@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * Cronyx Id: cxddk.c,v 1.1.2.2 2003/11/27 14:24:50 rik Exp $
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/cx/machdep.h>
#include <dev/cx/cxddk.h>
#include <dev/cx/cxreg.h>
#include <dev/cx/cronyxfw.h>
#include <dev/cx/csigmafw.h>

#define BYTE *(unsigned char*)&

/* standard base port set */
static short porttab [] = {
	0x200, 0x220, 0x240, 0x260, 0x280, 0x2a0, 0x2c0, 0x2e0,
	0x300, 0x320, 0x340, 0x360, 0x380, 0x3a0, 0x3c0, 0x3e0, 0
};

/*
 * Compute the optimal size of the receive buffer.
 */
static int cx_compute_buf_len (cx_chan_t *c)
{
	int rbsz;
	if (c->mode == M_ASYNC) {
		rbsz = (c->rxbaud + 800 - 1) / 800 * 2;
		if (rbsz < 4)
			rbsz = 4;
		else if (rbsz  > DMABUFSZ)
			rbsz = DMABUFSZ;
	}
	else
		rbsz = DMABUFSZ;

	return rbsz;
}

/*
 * Auto-detect the installed adapters.
 */
int cx_find (port_t *board_ports)
{
	int i, n;

	for (i=0, n=0; porttab[i] && n<NBRD; i++)
		if (cx_probe_board (porttab[i], -1, -1))
			board_ports[n++] = porttab[i];
	return n;
}

/*
 * Initialize the adapter.
 */
int cx_open_board (cx_board_t *b, int num, port_t port, int irq, int dma)
{
	cx_chan_t *c;

	if (num >= NBRD || ! cx_probe_board (port, irq, dma))
		return 0;

	/* init callback pointers */
	for (c=b->chan; c<b->chan+NCHAN; ++c) {
		c->call_on_tx = 0;
		c->call_on_rx = 0;
		c->call_on_msig = 0;
		c->call_on_err = 0;
	}

	cx_init (b, num, port, irq, dma);

	/* Loading firmware */
	if (! cx_setup_board (b, csigma_fw_data, csigma_fw_len, csigma_fw_tvec))
		return 0;
	return 1;
}

/*
 * Shutdown the adapter.
 */
void cx_close_board (cx_board_t *b)
{
	cx_setup_board (b, 0, 0, 0);

	/* Reset the controller. */
	outb (BCR0(b->port), 0);
	if (b->chan[8].type || b->chan[12].type)
		outb (BCR0(b->port+0x10), 0);
}

/*
 * Start the channel.
 */
void cx_start_chan (cx_chan_t *c, cx_buf_t *cb, unsigned long phys)
{
	int command = 0;
	int mode = 0;
	int ier = 0;
	int rbsz;

	c->overflow = 0;

	/* Setting up buffers */
	if (cb) {
		c->arbuf = cb->rbuffer[0];
		c->brbuf = cb->rbuffer[1];
		c->atbuf = cb->tbuffer[0];
		c->btbuf = cb->tbuffer[1];
		c->arphys = phys + ((char*)c->arbuf - (char*)cb);
		c->brphys = phys + ((char*)c->brbuf - (char*)cb);
		c->atphys = phys + ((char*)c->atbuf - (char*)cb);
		c->btphys = phys + ((char*)c->btbuf - (char*)cb);
	}

	/* Set current channel number */
	outb (CAR(c->port), c->num & 3);

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

	/* rx */
	command |= CCR_ENRX;
	ier |= IER_RXD;
	if (c->board->dma) {
		mode |= CMR_RXDMA;
		if (c->mode == M_ASYNC)
			ier |= IER_RET;
	}

	/* tx */
	command |= CCR_ENTX;
	ier |= (c->mode == M_ASYNC) ? IER_TXD : (IER_TXD | IER_TXMPTY);
	if (c->board->dma)
		mode |= CMR_TXDMA;

	/* Set mode */
	outb (CMR(c->port), mode | (c->mode == M_ASYNC ? CMR_ASYNC : CMR_HDLC));

	/* Clear and initialize channel */
	cx_cmd (c->port, CCR_CLRCH);
	cx_cmd (c->port, CCR_INITCH | command);
	if (c->mode == M_ASYNC)
		cx_cmd (c->port, CCR_ENTX);

	/* Start receiver */
	rbsz = cx_compute_buf_len(c);
	outw (ARBCNT(c->port), rbsz);
	outw (BRBCNT(c->port), rbsz);
	outw (ARBSTS(c->port), BSTS_OWN24);
	outw (BRBSTS(c->port), BSTS_OWN24);

	if (c->mode == M_ASYNC)
		ier |= IER_MDM;

	/* Enable interrupts */
	outb (IER(c->port), ier);

	/* Clear DTR and RTS */
	cx_set_dtr (c, 0);
	cx_set_rts (c, 0);
}

/*
 * Turn the receiver on/off.
 */
void cx_enable_receive (cx_chan_t *c, int on)
{
	unsigned char ier;

	if (cx_receive_enabled(c) && ! on) {
		outb (CAR(c->port), c->num & 3);
		if (c->mode == M_ASYNC) {
			ier = inb (IER(c->port));
			outb (IER(c->port), ier & ~ (IER_RXD | IER_RET));
		}
		cx_cmd (c->port, CCR_DISRX);
	} else if (! cx_receive_enabled(c) && on) {
		outb (CAR(c->port), c->num & 3);
		ier = inb (IER(c->port));
		if (c->mode == M_ASYNC)
			outb (IER(c->port), ier | (IER_RXD | IER_RET));
		else
			outb (IER(c->port), ier | IER_RXD);
 		cx_cmd (c->port, CCR_ENRX);
	}
}

/*
 * Turn the transmitter on/off.
 */
void cx_enable_transmit (cx_chan_t *c, int on)
{
	if (cx_transmit_enabled(c) && ! on) {
		outb (CAR(c->port), c->num & 3);
		if (c->mode != M_ASYNC)
			outb (STCR(c->port), STC_ABORTTX | STC_SNDSPC);
		cx_cmd (c->port, CCR_DISTX);
	} else if (! cx_transmit_enabled(c) && on) {
		outb (CAR(c->port), c->num & 3);
		cx_cmd (c->port, CCR_ENTX);
	}
}

/*
 * Get channel status.
 */
int cx_receive_enabled (cx_chan_t *c)
{
	outb (CAR(c->port), c->num & 3);
	return (inb (CSR(c->port)) & CSRA_RXEN) != 0;
}

int cx_transmit_enabled (cx_chan_t *c)
{
	outb (CAR(c->port), c->num & 3);
	return (inb (CSR(c->port)) & CSRA_TXEN) != 0;
}

unsigned long cx_get_baud (cx_chan_t *c)
{
	return (c->opt.tcor.clk == CLK_EXT) ? 0 : c->txbaud;
}

int cx_get_loop (cx_chan_t *c)
{
	return c->opt.tcor.llm ? 1 : 0;
}

int cx_get_nrzi (cx_chan_t *c)
{
	return c->opt.rcor.encod == ENCOD_NRZI;
}

int cx_get_dpll (cx_chan_t *c)
{
	return c->opt.rcor.dpll ? 1 : 0;
}

void cx_set_baud (cx_chan_t *c, unsigned long bps)
{
	int clock, period;

	c->txbaud = c->rxbaud = bps;

	/* Set current channel number */
	outb (CAR(c->port), c->num & 3);
	if (bps) {
		if (c->mode == M_ASYNC || c->opt.rcor.dpll || c->opt.tcor.llm) {
			/* Receive baud - internal */
			cx_clock (c->oscfreq, c->rxbaud, &clock, &period);
			c->opt.rcor.clk = clock;
			outb (RCOR(c->port), BYTE c->opt.rcor);
			outb (RBPR(c->port), period);
		} else {
			/* Receive baud - external */
			c->opt.rcor.clk = CLK_EXT;
			outb (RCOR(c->port), BYTE c->opt.rcor);
			outb (RBPR(c->port), 1);
		}

		/* Transmit baud - internal */
		cx_clock (c->oscfreq, c->txbaud, &clock, &period);
		c->opt.tcor.clk = clock;
		c->opt.tcor.ext1x = 0;
		outb (TBPR(c->port), period);
	} else if (c->mode != M_ASYNC) {
		/* External clock - disable local loopback and DPLL */
		c->opt.tcor.llm = 0;
		c->opt.rcor.dpll = 0;

		/* Transmit baud - external */
		c->opt.tcor.ext1x = 1;
		c->opt.tcor.clk = CLK_EXT;
		outb (TBPR(c->port), 1);

		/* Receive baud - external */
		c->opt.rcor.clk = CLK_EXT;
		outb (RCOR(c->port), BYTE c->opt.rcor);
		outb (RBPR(c->port), 1);
	}
	if (c->opt.tcor.llm)
		outb (COR2(c->port), (BYTE c->hopt.cor2) & ~3);
	else
		outb (COR2(c->port), BYTE c->hopt.cor2);
	outb (TCOR(c->port), BYTE c->opt.tcor);
}

void cx_set_loop (cx_chan_t *c, int on)
{
	if (! c->txbaud)
		return;

	c->opt.tcor.llm = on ? 1 : 0;
	cx_set_baud (c, c->txbaud);
}

void cx_set_dpll (cx_chan_t *c, int on)
{
	if (! c->txbaud)
		return;

	c->opt.rcor.dpll = on ? 1 : 0;
	cx_set_baud (c, c->txbaud);
}

void cx_set_nrzi (cx_chan_t *c, int nrzi)
{
	c->opt.rcor.encod = (nrzi ? ENCOD_NRZI : ENCOD_NRZ);
	outb (CAR(c->port), c->num & 3);
	outb (RCOR(c->port), BYTE c->opt.rcor);
}

static int cx_send (cx_chan_t *c, char *data, int len,
	void *attachment)
{
	unsigned char *buf;
	port_t cnt_port, sts_port;
	void **attp;

	/* Set the current channel number. */
	outb (CAR(c->port), c->num & 3);

	/* Determine the buffer order. */
	if (inb (DMABSTS(c->port)) & DMABSTS_NTBUF) {
		if (inb (BTBSTS(c->port)) & BSTS_OWN24) {
			buf	 = c->atbuf;
			cnt_port = ATBCNT(c->port);
			sts_port = ATBSTS(c->port);
			attp	 = &c->attach[0];
		} else {
			buf	 = c->btbuf;
			cnt_port = BTBCNT(c->port);
			sts_port = BTBSTS(c->port);
			attp	 = &c->attach[1];
		}
	} else {
		if (inb (ATBSTS(c->port)) & BSTS_OWN24) {
			buf	 = c->btbuf;
			cnt_port = BTBCNT(c->port);
			sts_port = BTBSTS(c->port);
			attp	 = &c->attach[1];
		} else {
			buf	 = c->atbuf;
			cnt_port = ATBCNT(c->port);
			sts_port = ATBSTS(c->port);
			attp	 = &c->attach[0];
		}
	}
	/* Is it busy? */
	if (inb (sts_port) & BSTS_OWN24)
		return -1;

	memcpy (buf, data, len);
	*attp = attachment;

	/* Start transmitter. */
	outw (cnt_port, len);
	outb (sts_port, BSTS_EOFR | BSTS_INTR | BSTS_OWN24);

	/* Enable TXMPTY interrupt,
	 * to catch the case when the second buffer is empty. */
	if (c->mode != M_ASYNC) {
		if ((inb(ATBSTS(c->port)) & BSTS_OWN24) &&
		    (inb(BTBSTS(c->port)) & BSTS_OWN24)) {
			outb (IER(c->port), IER_RXD | IER_TXD | IER_TXMPTY);
		} else
			outb (IER(c->port), IER_RXD | IER_TXD);
	}
	return 0;
}

/*
 * Number of free buffs
 */
int cx_buf_free (cx_chan_t *c)
{
	return ! (inb (ATBSTS(c->port)) & BSTS_OWN24) +
		! (inb (BTBSTS(c->port)) & BSTS_OWN24);
}

/*
 * Send the data packet.
 */
int cx_send_packet (cx_chan_t *c, char *data, int len, void *attachment)
{
	if (len >= DMABUFSZ)
		return -2;
	if (c->mode == M_ASYNC) {
		static char buf [DMABUFSZ];
		char *p, *t = buf;

		/* Async -- double all nulls. */
		for (p=data; p < data+len && t < buf+DMABUFSZ-1; ++p)
			if ((*t++ = *p) == 0)
				*t++ = 0;
		return cx_send (c, buf, t-buf, attachment);
	}
	return cx_send (c, data, len, attachment);
}

static int cx_receive_interrupt (cx_chan_t *c)
{
	unsigned short risr;
	int len = 0, rbsz;

	++c->rintr;
	risr = inw (RISR(c->port));

	/* Compute optimal receiver buffer length */
	rbsz = cx_compute_buf_len(c);
	if (c->mode == M_ASYNC && (risr & RISA_TIMEOUT)) {
		unsigned long rcbadr = (unsigned short) inw (RCBADRL(c->port)) |
			(long) inw (RCBADRU(c->port)) << 16;
		unsigned char *buf = NULL;
		port_t cnt_port = 0, sts_port = 0;

		if (rcbadr >= c->brphys && rcbadr < c->brphys+DMABUFSZ) {
			buf = c->brbuf;
			len = rcbadr - c->brphys;
			cnt_port = BRBCNT(c->port);
			sts_port = BRBSTS(c->port);
		} else if (rcbadr >= c->arphys && rcbadr < c->arphys+DMABUFSZ) {
			buf = c->arbuf;
			len = rcbadr - c->arphys;
			cnt_port = ARBCNT(c->port);
			sts_port = ARBSTS(c->port);
		}

		if (len) {
			c->ibytes += len;
			c->received_data = buf;
			c->received_len = len;

			/* Restart receiver. */
			outw (cnt_port, rbsz);
			outb (sts_port, BSTS_OWN24);
		}
		return (REOI_TERMBUFF);
	}

	/* Receive errors. */
	if (risr & RIS_OVERRUN) {
		++c->ierrs;
		if (c->call_on_err)
			c->call_on_err (c, CX_OVERRUN);
	} else if (c->mode != M_ASYNC && (risr & RISH_CRCERR)) {
		++c->ierrs;
		if (c->call_on_err)
			c->call_on_err (c, CX_CRC);
	} else if (c->mode != M_ASYNC && (risr & (RISH_RXABORT | RISH_RESIND))) {
		++c->ierrs;
		if (c->call_on_err)
			c->call_on_err (c, CX_FRAME);
	} else if (c->mode == M_ASYNC && (risr & RISA_PARERR)) {
		++c->ierrs;
		if (c->call_on_err)
			c->call_on_err (c, CX_CRC);
	} else if (c->mode == M_ASYNC && (risr & RISA_FRERR)) {
		++c->ierrs;
		if (c->call_on_err)
			c->call_on_err (c, CX_FRAME);
	} else if (c->mode == M_ASYNC && (risr & RISA_BREAK)) {
		if (c->call_on_err)
			c->call_on_err (c, CX_BREAK);
	} else if (! (risr & RIS_EOBUF)) {
		++c->ierrs;
	} else {
		/* Handle received data. */
		len = (risr & RIS_BB) ? inw(BRBCNT(c->port)) : inw(ARBCNT(c->port));

		if (len > DMABUFSZ) {
			/* Fatal error: actual DMA transfer size
			 * exceeds our buffer size.  It could be caused
			 * by incorrectly programmed DMA register or
			 * hardware fault.  Possibly, should panic here. */
			len = DMABUFSZ;
		} else if (c->mode != M_ASYNC && ! (risr & RIS_EOFR)) {
			/* The received frame does not fit in the DMA buffer.
			 * It could be caused by serial lie noise,
			 * or if the peer has too big MTU. */
			if (! c->overflow) {
				if (c->call_on_err)
					c->call_on_err (c, CX_OVERFLOW);
				c->overflow = 1;
				++c->ierrs;
			}
		} else if (! c->overflow) {
			if (risr & RIS_BB) {
				c->received_data = c->brbuf;
				c->received_len = len;
			} else {
				c->received_data = c->arbuf;
				c->received_len = len;
			}
			if (c->mode != M_ASYNC)
				++c->ipkts;
			c->ibytes += len;
		} else
			c->overflow = 0;
	}

	/* Restart receiver. */
	if (! (inb (ARBSTS(c->port)) & BSTS_OWN24)) {
		outw (ARBCNT(c->port), rbsz);
		outb (ARBSTS(c->port), BSTS_OWN24);
	}
	if (! (inb (BRBSTS(c->port)) & BSTS_OWN24)) {
		outw (BRBCNT(c->port), rbsz);
		outb (BRBSTS(c->port), BSTS_OWN24);
	}

	/* Discard exception characters. */
	if ((risr & RISA_SCMASK) && c->aopt.cor2.ixon)
		return (REOI_DISCEXC);
	else
		return (0);
}

static void cx_transmit_interrupt (cx_chan_t *c)
{
	unsigned char tisr;
	int len = 0;

	++c->tintr;
	tisr = inb (TISR(c->port));
	if (tisr & TIS_UNDERRUN) {	/* Transmit underrun error */
		if (c->call_on_err)
			c->call_on_err (c, CX_UNDERRUN);
		++c->oerrs;
	} else if (tisr & (TIS_EOBUF | TIS_TXEMPTY | TIS_TXDATA)) {
		/* Call processing function */
		if (tisr & TIS_BB) {
			len = inw(BTBCNT(c->port));
			if (c->call_on_tx)
				c->call_on_tx (c, c->attach[1], len);
		} else {
			len = inw(ATBCNT(c->port));
			if (c->call_on_tx)
				c->call_on_tx (c, c->attach[0], len);
		}
		if (c->mode != M_ASYNC && len != 0)
			++c->opkts;
		c->obytes += len;
	}

	/* Enable TXMPTY interrupt,
	 * to catch the case when the second buffer is empty. */
	if (c->mode != M_ASYNC) {
		if ((inb (ATBSTS(c->port)) & BSTS_OWN24) &&
		   (inb (BTBSTS(c->port)) & BSTS_OWN24)) {
			outb (IER(c->port), IER_RXD | IER_TXD | IER_TXMPTY);
		} else
			outb (IER(c->port), IER_RXD | IER_TXD);
	}
}

void cx_int_handler (cx_board_t *b)
{
	unsigned char livr;
	cx_chan_t *c;

	while (! (inw (BSR(b->port)) & BSR_NOINTR)) {
		/* Enter the interrupt context, using IACK bus cycle.
		   Read the local interrupt vector register. */
		livr = inb (IACK(b->port, BRD_INTR_LEVEL));
		c = b->chan + (livr>>2 & 0xf);
		if (c->type == T_NONE)
			continue;
		switch (livr & 3) {
		case LIV_MODEM: 		/* modem interrupt */
			++c->mintr;
			if (c->call_on_msig)
				c->call_on_msig (c);
			outb (MEOIR(c->port), 0);
			break;
		case LIV_EXCEP: 		/* receive exception */
		case LIV_RXDATA:		/* receive interrupt */
			outb (REOIR(c->port), cx_receive_interrupt (c));
			if (c->call_on_rx && c->received_data) {
				c->call_on_rx (c, c->received_data,
					c->received_len);
				c->received_data = 0;
			}
			break;
		case LIV_TXDATA:		/* transmit interrupt */
			cx_transmit_interrupt (c);
			outb (TEOIR(c->port), 0);
			break;
		}
	}
}

/*
 * Register event processing functions
 */
void cx_register_transmit (cx_chan_t *c,
	void (*func) (cx_chan_t *c, void *attachment, int len))
{
	c->call_on_tx = func;
}

void cx_register_receive (cx_chan_t *c,
	void (*func) (cx_chan_t *c, char *data, int len))
{
	c->call_on_rx = func;
}

void cx_register_modem (cx_chan_t *c, void (*func) (cx_chan_t *c))
{
	c->call_on_msig = func;
}

void cx_register_error (cx_chan_t *c, void (*func) (cx_chan_t *c, int data))
{
	c->call_on_err = func;
}

/*
 * Async protocol functions.
 */

/*
 * Enable/disable transmitter.
 */
void cx_transmitter_ctl (cx_chan_t *c,int start)
{
	outb (CAR(c->port), c->num & 3);
	cx_cmd (c->port, start ? CCR_ENTX : CCR_DISTX);
}

/*
 * Discard all data queued in transmitter.
 */
void cx_flush_transmit (cx_chan_t *c)
{
	outb (CAR(c->port), c->num & 3);
	cx_cmd (c->port, CCR_CLRTX);
}

/*
 * Send the XON/XOFF flow control symbol.
 */
void cx_xflow_ctl (cx_chan_t *c, int on)
{
	outb (CAR(c->port), c->num & 3);
	outb (STCR(c->port), STC_SNDSPC | (on ? STC_SSPC_1 : STC_SSPC_2));
}

/*
 * Send the break signal for a given number of milliseconds.
 */
void cx_send_break (cx_chan_t *c, int msec)
{
	static unsigned char buf [128];
	unsigned char *p;

	p = buf;
	*p++ = 0;		/* extended transmit command */
	*p++ = 0x81;		/* send break */

	if (msec > 10000)	/* max 10 seconds */
		msec = 10000;
	if (msec < 10)		/* min 10 msec */
		msec = 10;
	while (msec > 0) {
		int ms = 250;	/* 250 msec */
		if (ms > msec)
			ms = msec;
		msec -= ms;
		*p++ = 0;	/* extended transmit command */
		*p++ = 0x82;	/* insert delay */
		*p++ = ms;
	}
	*p++ = 0;		/* extended transmit command */
	*p++ = 0x83;		/* stop break */

	cx_send (c, buf, p-buf, 0);
}

/*
 * Set async parameters.
 */
void cx_set_async_param (cx_chan_t *c, int baud, int bits, int parity,
	int stop2, int ignpar, int rtscts,
	int ixon, int ixany, int symstart, int symstop)
{
	int clock, period;
	cx_cor1_async_t cor1;

	/* Set character length and parity mode. */
	BYTE cor1 = 0;
	cor1.charlen = bits - 1;
	cor1.parmode = parity ? PARM_NORMAL : PARM_NOPAR;
	cor1.parity = parity==1 ? PAR_ODD : PAR_EVEN;
	cor1.ignpar = ignpar ? 1 : 0;

	/* Enable/disable hardware CTS. */
	c->aopt.cor2.ctsae = rtscts ? 1 : 0;

	/* Enable extended transmit command mode.
	 * Unfortunately, there is no other method for sending break. */
	c->aopt.cor2.etc = 1;

	/* Enable/disable hardware XON/XOFF. */
	c->aopt.cor2.ixon = ixon ? 1 : 0;
	c->aopt.cor2.ixany = ixany ? 1 : 0;

	/* Set the number of stop bits. */
	if (stop2)
		c->aopt.cor3.stopb = STOPB_2;
	else
		c->aopt.cor3.stopb = STOPB_1;

	/* Disable/enable passing XON/XOFF chars to the host. */
	c->aopt.cor3.scde = ixon ? 1 : 0;
	c->aopt.cor3.flowct = ixon ? FLOWCC_NOTPASS : FLOWCC_PASS;

	c->aopt.schr1 = symstart;	/* XON */
	c->aopt.schr2 = symstop;	/* XOFF */

	/* Set current channel number. */
	outb (CAR(c->port), c->num & 3);

	/* Set up clock values. */
	if (baud) {
		c->rxbaud = c->txbaud = baud;

		/* Receiver. */
		cx_clock (c->oscfreq, c->rxbaud, &clock, &period);
		c->opt.rcor.clk = clock;
		outb (RCOR(c->port), BYTE c->opt.rcor);
		outb (RBPR(c->port), period);

		/* Transmitter. */
		cx_clock (c->oscfreq, c->txbaud, &clock, &period);
		c->opt.tcor.clk = clock;
		c->opt.tcor.ext1x = 0;
		outb (TCOR(c->port), BYTE c->opt.tcor);
		outb (TBPR(c->port), period);
	}
	outb (COR2(c->port), BYTE c->aopt.cor2);
	outb (COR3(c->port), BYTE c->aopt.cor3);
	outb (SCHR1(c->port), c->aopt.schr1);
	outb (SCHR2(c->port), c->aopt.schr2);

	if (BYTE c->aopt.cor1 != BYTE cor1) {
		BYTE c->aopt.cor1 = BYTE cor1;
		outb (COR1(c->port), BYTE c->aopt.cor1);
		/* Any change to COR1 require reinitialization. */
		/* Unfortunately, it may cause transmitter glitches... */
		cx_cmd (c->port, CCR_INITCH);
	}
}

/*
 * Set mode: M_ASYNC or M_HDLC.
 * Both receiver and transmitter are disabled.
 */
int cx_set_mode (cx_chan_t *c, int mode)
{
	if (mode == M_HDLC) {
		if (c->type == T_ASYNC)
			return -1;

		if (c->mode == M_HDLC)
			return 0;

		c->mode = M_HDLC;
	} else if (mode == M_ASYNC) {
		if (c->type == T_SYNC_RS232 ||
		    c->type == T_SYNC_V35   ||
		    c->type == T_SYNC_RS449)
			return -1;

		if (c->mode == M_ASYNC)
			return 0;

		c->mode = M_ASYNC;
		c->opt.tcor.ext1x = 0;
		c->opt.tcor.llm = 0;
		c->opt.rcor.dpll = 0;
		c->opt.rcor.encod = ENCOD_NRZ;
		if (! c->txbaud || ! c->rxbaud)
			c->txbaud = c->rxbaud = 9600;
	} else
		return -1;

	cx_setup_chan (c);
	cx_start_chan (c, 0, 0);
	cx_enable_receive (c, 0);
	cx_enable_transmit (c, 0);
	return 0;
}

/*
 * Set port type for old models of Sigma
 */
void cx_set_port (cx_chan_t *c, int iftype)
{
	if (c->board->type == B_SIGMA_XXX) {
		switch (c->num) {
		case 0:
			if ((c->board->if0type != 0) == (iftype != 0))
				return;
			c->board->if0type = iftype;
			c->board->bcr0 &= ~BCR0_UMASK;
			if (c->board->if0type &&
			    (c->type==T_UNIV_RS449 || c->type==T_UNIV_V35))
				c->board->bcr0 |= BCR0_UI_RS449;
			outb (BCR0(c->board->port), c->board->bcr0);
			break;
		case 8:
			if ((c->board->if8type != 0) == (iftype != 0))
				return;
			c->board->if8type = iftype;
			c->board->bcr0b &= ~BCR0_UMASK;
			if (c->board->if8type &&
			    (c->type==T_UNIV_RS449 || c->type==T_UNIV_V35))
				c->board->bcr0b |= BCR0_UI_RS449;
			outb (BCR0(c->board->port+0x10), c->board->bcr0b);
			break;
		}
	}
}

/*
 * Get port type for old models of Sigma
 * -1 Fixed port type or auto detect
 *  0 RS232
 *  1 V35
 *  2 RS449
 */
int cx_get_port (cx_chan_t *c)
{
	int iftype;

	if (c->board->type == B_SIGMA_XXX) {
		switch (c->num) {
		case 0:
			iftype = c->board->if0type; break;
		case 8:
			iftype = c->board->if8type; break;
		default:
			return -1;
		}

		if (iftype)
			switch (c->type) {
			case T_UNIV_V35:   return 1;
			case T_UNIV_RS449: return 2;
			default:	   return -1;
			}
		else
			return 0;
	} else
		return -1;
}

void cx_intr_off (cx_board_t *b)
{
	outb (BCR0(b->port), b->bcr0 & ~BCR0_IRQ_MASK);
	if (b->chan[8].port || b->chan[12].port)
		outb (BCR0(b->port+0x10), b->bcr0b & ~BCR0_IRQ_MASK);
}

void cx_intr_on (cx_board_t *b)
{
	outb (BCR0(b->port), b->bcr0);
	if (b->chan[8].port || b->chan[12].port)
		outb (BCR0(b->port+0x10), b->bcr0b);
}

int cx_checkintr (cx_board_t *b)
{
	return (!(inw (BSR(b->port)) & BSR_NOINTR));
}
