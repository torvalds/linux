/*-
 * Defines for Cronyx-Sigma adapter driver.
 *
 * Copyright (C) 1994-2001 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
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
 * Cronyx Id: cxddk.h,v 1.1.2.1 2003/11/12 17:13:41 rik Exp $
 * $FreeBSD$
 */

#ifndef port_t
#   ifdef _M_ALPHA                      /* port address on Alpha under */
#      define port_t unsigned long      /* Windows NT is 32 bit long */
#   else
#      define port_t unsigned short     /* all other architectures */
#   endif                               /* have 16-bit port addresses */
#endif

#define NBRD		3		/* the max number of installed boards */
#define NPORT		32		/* the number of i/o ports per board */
#define DMABUFSZ	1600

/*
 * Asynchronous channel mode -------------------------------------------------
 */

/* Parity */
#define	PAR_EVEN	0	/* even parity */
#define	PAR_ODD		1	/* odd parity */

/* Parity mode */
#define	PARM_NOPAR	0	/* no parity */
#define	PARM_FORCE	1	/* force parity (odd = force 1, even = 0) */
#define	PARM_NORMAL	2	/* normal parity */

/* Flow control transparency mode */
#define	FLOWCC_PASS	0	/* pass flow ctl chars as exceptions */
#define FLOWCC_NOTPASS  1       /* don't pass flow ctl chars to the host */

/* Stop bit length */
#define	STOPB_1		2	/* 1 stop bit */
#define	STOPB_15	3	/* 1.5 stop bits */
#define	STOPB_2		4	/* 2 stop bits */

/* Action on break condition */
#define	BRK_INTR	0	/* generate an exception interrupt */
#define	BRK_NULL	1	/* translate to a NULL character */
#define	BRK_RESERVED	2	/* reserved */
#define	BRK_DISCARD	3	/* discard character */

/* Parity/framing error actions */
#define	PERR_INTR	0	/* generate an exception interrupt */
#define	PERR_NULL	1	/* translate to a NULL character */
#define	PERR_IGNORE	2	/* ignore error; char passed as good data */
#define	PERR_DISCARD	3	/* discard error character */
#define	PERR_FFNULL	5	/* translate to FF NULL char */

typedef struct {		/* async channel option register 1 */
	unsigned charlen : 4;	/* character length, 5..8 */
	unsigned ignpar : 1;	/* ignore parity */
	unsigned parmode : 2;	/* parity mode */
	unsigned parity : 1;	/* parity */
} cx_cor1_async_t;

typedef struct {		/* async channel option register 2 */
	unsigned dsrae : 1;	/* DSR automatic enable */
	unsigned ctsae : 1;	/* CTS automatic enable */
	unsigned rtsao : 1;	/* RTS automatic output enable */
	unsigned rlm : 1;	/* remote loopback mode enable */
	unsigned zero : 1;
	unsigned etc : 1;	/* embedded transmitter cmd enable */
	unsigned ixon : 1;	/* in-band XON/XOFF enable */
	unsigned ixany : 1;	/* XON on any character */
} cx_cor2_async_t;

typedef struct {		/* async channel option register 3 */
	unsigned stopb : 3;	/* stop bit length */
	unsigned zero : 1;
	unsigned scde : 1;	/* special char detection enable */
	unsigned flowct : 1;	/* flow control transparency mode */
	unsigned rngde : 1;	/* range detect enable */
	unsigned escde : 1;	/* extended spec. char detect enable */
} cx_cor3_async_t;

typedef struct {		/* async channel option register 6 */
	unsigned parerr : 3;	/* parity/framing error actions */
	unsigned brk : 2;	/* action on break condition */
	unsigned inlcr : 1;	/* translate NL to CR on input */
	unsigned icrnl : 1;	/* translate CR to NL on input */
	unsigned igncr : 1;	/* discard CR on input */
} cx_cor6_async_t;

typedef struct {		/* async channel option register 7 */
	unsigned ocrnl : 1;	/* translate CR to NL on output */
	unsigned onlcr : 1;	/* translate NL to CR on output */
	unsigned zero : 3;
	unsigned fcerr : 1;	/* process flow ctl err chars enable */
	unsigned lnext : 1;	/* LNext option enable */
	unsigned istrip : 1;	/* strip 8-bit on input */
} cx_cor7_async_t;

typedef struct {		/* async channel options */
	cx_cor1_async_t cor1;   /* channel option register 1 */
	cx_cor2_async_t cor2;   /* channel option register 2 */
	cx_cor3_async_t cor3;   /* option register 3 */
	cx_cor6_async_t cor6;   /* channel option register 6 */
	cx_cor7_async_t cor7;   /* channel option register 7 */
	unsigned char schr1;	/* special character register 1 (XON) */
	unsigned char schr2;	/* special character register 2 (XOFF) */
	unsigned char schr3;	/* special character register 3 */
	unsigned char schr4;	/* special character register 4 */
	unsigned char scrl;	/* special character range low */
	unsigned char scrh;	/* special character range high */
	unsigned char lnxt;	/* LNext character */
} cx_opt_async_t;

/*
 * HDLC channel mode ---------------------------------------------------------
 */
/* Address field length option */
#define	AFLO_1OCT	0	/* address field is 1 octet in length */
#define	AFLO_2OCT	1	/* address field is 2 octet in length */

/* Clear detect for X.21 data transfer phase */
#define	CLRDET_DISABLE	0	/* clear detect disabled */
#define	CLRDET_ENABLE	1	/* clear detect enabled */

/* Addressing mode */
#define	ADMODE_NOADDR	0	/* no address */
#define	ADMODE_4_1	1	/* 4 * 1 byte */
#define	ADMODE_2_2	2	/* 2 * 2 byte */

/* FCS append */
#define	FCS_NOTPASS	0	/* receive CRC is not passed to the host */
#define	FCS_PASS	1	/* receive CRC is passed to the host */

/* CRC modes */
#define	CRC_INVERT	0	/* CRC is transmitted inverted (CRC V.41) */
#define	CRC_DONT_INVERT	1	/* CRC is not transmitted inverted (CRC-16) */

/* Send sync pattern */
#define	SYNC_00		0	/* send 00h as pad char (NRZI encoding) */
#define	SYNC_AA		1	/* send AAh (Manchester/NRZ encoding) */

/* FCS preset */
#define	FCSP_ONES	0	/* FCS is preset to all ones (CRC V.41) */
#define	FCSP_ZEROS	1	/* FCS is preset to all zeros (CRC-16) */

/* idle mode */
#define	IDLE_FLAG	0	/* idle in flag */
#define	IDLE_MARK	1	/* idle in mark */

/* CRC polynomial select */
#define	POLY_V41	0	/* x^16+x^12+x^5+1 (HDLC, preset to 1) */
#define	POLY_16		1	/* x^16+x^15+x^2+1 (bisync, preset to 0) */

typedef struct {		/* hdlc channel option register 1 */
	unsigned ifflags : 4;	/* number of inter-frame flags sent */
	unsigned admode : 2;	/* addressing mode */
	unsigned clrdet : 1;	/* clear detect for X.21 data transfer phase */
	unsigned aflo : 1;	/* address field length option */
} cx_cor1_hdlc_t;

typedef struct {		/* hdlc channel option register 2 */
	unsigned dsrae : 1;	/* DSR automatic enable */
	unsigned ctsae : 1;	/* CTS automatic enable */
	unsigned rtsao : 1;	/* RTS automatic output enable */
	unsigned zero1 : 1;
	unsigned crcninv : 1;	/* CRC invertion option */
	unsigned zero2 : 1;
	unsigned fcsapd : 1;	/* FCS append */
	unsigned zero3 : 1;
} cx_cor2_hdlc_t;

typedef struct {		/* hdlc channel option register 3 */
	unsigned padcnt : 3;	/* pad character count */
	unsigned idle : 1;	/* idle mode */
	unsigned nofcs : 1;	/* FCS disable */
	unsigned fcspre : 1;	/* FCS preset */
	unsigned syncpat : 1;	/* send sync pattern */
	unsigned sndpad : 1;	/* send pad characters before flag enable */
} cx_cor3_hdlc_t;

typedef struct {		/* hdlc channel options */
	cx_cor1_hdlc_t cor1;    /* hdlc channel option register 1 */
	cx_cor2_hdlc_t cor2;    /* hdlc channel option register 2 */
	cx_cor3_hdlc_t cor3;    /* hdlc channel option register 3 */
	unsigned char rfar1;	/* receive frame address register 1 */
	unsigned char rfar2;	/* receive frame address register 2 */
	unsigned char rfar3;	/* receive frame address register 3 */
	unsigned char rfar4;	/* receive frame address register 4 */
	unsigned char cpsr;	/* CRC polynomial select */
} cx_opt_hdlc_t;

/*
 * CD2400 channel state structure --------------------------------------------
 */

/* Signal encoding */
#define ENCOD_NRZ        0      /* NRZ mode */
#define ENCOD_NRZI       1      /* NRZI mode */
#define ENCOD_MANCHESTER 2      /* Manchester mode */

/* Clock source */
#define CLK_0           0      /* clock 0 */
#define CLK_1           1      /* clock 1 */
#define CLK_2           2      /* clock 2 */
#define CLK_3           3      /* clock 3 */
#define CLK_4           4      /* clock 4 */
#define CLK_EXT         6      /* external clock */
#define CLK_RCV         7      /* receive clock */

/* Board type */
#define B_SIGMA_XXX     0       /* old Sigmas */
#define B_SIGMA_2X      1       /* Sigma-22 */
#define B_SIGMA_800     2       /* Sigma-800 */

/* Channel type */
#define T_NONE          0       /* no channel */
#define T_ASYNC         1       /* pure asynchronous RS-232 channel */
#define T_SYNC_RS232    2       /* pure synchronous RS-232 channel */
#define T_SYNC_V35      3       /* pure synchronous V.35 channel */
#define T_SYNC_RS449    4       /* pure synchronous RS-449 channel */
#define T_UNIV_RS232    5       /* sync/async RS-232 channel */
#define T_UNIV_RS449    6       /* sync/async RS-232/RS-449 channel */
#define T_UNIV_V35      7       /* sync/async RS-232/V.35 channel */
#define T_UNIV          8       /* sync/async, unknown interface */

#define M_ASYNC         0	/* asynchronous mode */
#define M_HDLC          1	/* bit-sync mode (HDLC) */

typedef struct {		/* channel option register 4 */
	unsigned thr : 4;	/* FIFO threshold */
	unsigned zero : 1;
	unsigned cts_zd : 1;	/* detect 1 to 0 transition on the CTS */
	unsigned cd_zd : 1;	/* detect 1 to 0 transition on the CD */
	unsigned dsr_zd : 1;	/* detect 1 to 0 transition on the DSR */
} cx_cor4_t;

typedef struct {		/* channel option register 5 */
	unsigned rx_thr : 4;	/* receive flow control FIFO threshold */
	unsigned zero : 1;
	unsigned cts_od : 1;	/* detect 0 to 1 transition on the CTS */
	unsigned cd_od : 1;	/* detect 0 to 1 transition on the CD */
	unsigned dsr_od : 1;	/* detect 0 to 1 transition on the DSR */
} cx_cor5_t;

typedef struct {		/* receive clock option register */
	unsigned clk : 3;	/* receive clock source */
	unsigned encod : 2;     /* signal encoding NRZ/NRZI/Manchester */
	unsigned dpll : 1;      /* DPLL enable */
	unsigned zero : 1;
	unsigned tlval : 1;	/* transmit line value */
} cx_rcor_t;

typedef struct {		/* transmit clock option register */
	unsigned zero1 : 1;
	unsigned llm : 1;	/* local loopback mode */
	unsigned zero2 : 1;
	unsigned ext1x : 1;	/* external 1x clock mode */
	unsigned zero3 : 1;
	unsigned clk : 3;	/* transmit clock source */
} cx_tcor_t;

typedef struct {
	cx_cor4_t cor4;         /* channel option register 4 */
	cx_cor5_t cor5;         /* channel option register 5 */
	cx_rcor_t rcor;         /* receive clock option register */
	cx_tcor_t tcor;         /* transmit clock option register */
} cx_chan_opt_t;

typedef enum {                  /* line break mode */
	BRK_IDLE,               /* normal line mode */
	BRK_SEND,               /* start sending break */
	BRK_STOP,               /* stop sending break */
} cx_break_t;

#define BUS_NORMAL	0	/* normal bus timing */
#define BUS_FAST	1	/* fast bus timing (Sigma-22 and -800) */
#define BUS_FAST2	2	/* fast bus timing (Sigma-800) */
#define BUS_FAST3	3	/* fast bus timing (Sigma-800) */

typedef struct {                /* board options */
	unsigned char fast;	/* bus master timing (Sigma-22 and -800) */
} cx_board_opt_t;

#define NCHIP    4		/* the number of controllers per board */
#define NCHAN    16		/* the number of channels on the board */

typedef struct {
	unsigned char tbuffer [2] [DMABUFSZ];
	unsigned char rbuffer [2] [DMABUFSZ];
} cx_buf_t;

typedef struct _cx_chan_t {
	struct _cx_board_t *board;      /* board pointer */
	unsigned char type;             /* channel type */
	unsigned char num;              /* channel number, 0..15 */
	port_t port;                    /* base port address */
	unsigned long oscfreq;		/* oscillator frequency in Hz */
	unsigned long rxbaud;		/* receiver speed */
	unsigned long txbaud;		/* transmitter speed */
	unsigned char mode;             /* channel mode */
	cx_chan_opt_t opt;              /* common channel options */
	cx_opt_async_t aopt;            /* async mode options */
	cx_opt_hdlc_t hopt;             /* hdlc mode options */
	unsigned char *arbuf;           /* receiver A dma buffer */
	unsigned char *brbuf;           /* receiver B dma buffer */
	unsigned char *atbuf;           /* transmitter A dma buffer */
	unsigned char *btbuf;           /* transmitter B dma buffer */
	unsigned long arphys;           /* receiver A phys address */
	unsigned long brphys;           /* receiver B phys address */
	unsigned long atphys;           /* transmitter A phys address */
	unsigned long btphys;           /* transmitter B phys address */
	unsigned char dtr;              /* DTR signal value */
	unsigned char rts;              /* RTS signal value */

	unsigned long rintr;            /* receive interrupts */
	unsigned long tintr;            /* transmit interrupts */
	unsigned long mintr;            /* modem interrupts */
	unsigned long ibytes;           /* input bytes */
	unsigned long ipkts;            /* input packets */
	unsigned long ierrs;            /* input errors */
	unsigned long obytes;           /* output bytes */
	unsigned long opkts;            /* output packets */
	unsigned long oerrs;            /* output errors */

	void *sys;
	int debug;
	int debug_shadow;
	void *attach [2];
	char *received_data;
	int received_len;
	int overflow;

	void (*call_on_rx) (struct _cx_chan_t*, char*, int);
	void (*call_on_tx) (struct _cx_chan_t*, void*, int);
	void (*call_on_msig) (struct _cx_chan_t*);
	void (*call_on_err) (struct _cx_chan_t*, int);

} cx_chan_t;

typedef struct _cx_board_t {
	unsigned char type;             /* board type */
	unsigned char num;		/* board number, 0..2 */
	port_t port;                    /* base board port, 0..3f0 */
	unsigned char irq;              /* irq {3 5 7 10 11 12 15} */
	unsigned char dma;              /* DMA request {5 6 7} */
	char name[16];                  /* board version name */
	unsigned char nuniv;            /* number of universal channels */
	unsigned char nsync;            /* number of sync. channels */
	unsigned char nasync;           /* number of async. channels */
	unsigned char if0type;          /* chan0 interface RS-232/RS-449/V.35 */
	unsigned char if8type;          /* chan8 interface RS-232/RS-449/V.35 */
	unsigned short bcr0;            /* BCR0 image */
	unsigned short bcr0b;           /* BCR0b image */
	unsigned short bcr1;            /* BCR1 image */
	unsigned short bcr1b;           /* BCR1b image */
	cx_board_opt_t opt;             /* board options */
	cx_chan_t chan[NCHAN];          /* channel structures */
	void *sys;
} cx_board_t;

extern long cx_rxbaud, cx_txbaud;
extern int cx_univ_mode, cx_sync_mode, cx_iftype;

extern cx_chan_opt_t chan_opt_dflt;     /* default mode-independent options */
extern cx_opt_async_t opt_async_dflt;   /* default async options */
extern cx_opt_hdlc_t opt_hdlc_dflt;     /* default hdlc options */
extern cx_board_opt_t board_opt_dflt;   /* default board options */

struct _cr_dat_tst;
int cx_probe_board (port_t port, int irq, int dma);
void cx_init (cx_board_t *b, int num, port_t port, int irq, int dma);
void cx_init_board (cx_board_t *b, int num, port_t port, int irq, int dma,
	int chain, int rev, int osc, int mod, int rev2, int osc2, int mod2);
void cx_init_2x (cx_board_t *b, int num, port_t port, int irq, int dma,
	int rev, int osc);
void cx_init_800 (cx_board_t *b, int num, port_t port, int irq, int dma,
	int chain);
int cx_download (port_t port, const unsigned char *firmware, long bits,
	const struct _cr_dat_tst *tst);
int cx_setup_board (cx_board_t *b, const unsigned char *firmware,
	long bits, const struct _cr_dat_tst *tst);
void cx_setup_chan (cx_chan_t *c);
void cx_update_chan (cx_chan_t *c);
void cx_set_dtr (cx_chan_t *c, int on);
void cx_set_rts (cx_chan_t *c, int on);
void cx_led (cx_board_t *b, int on);
void cx_cmd (port_t base, int cmd);
void cx_disable_dma (cx_board_t *b);
void cx_reinit_board (cx_board_t *b);
int cx_get_dsr (cx_chan_t *c);
int cx_get_cts (cx_chan_t *c);
int cx_get_cd (cx_chan_t *c);
void cx_clock (long hz, long ba, int *clk, int *div);

/* DDK errors */
#define CX_FRAME	 1
#define CX_CRC		 2
#define CX_OVERRUN	 3
#define CX_OVERFLOW	 4
#define CX_UNDERRUN	 5
#define CX_BREAK	 6

/* clock sources */
#define CX_CLK_INT	 0
#define CX_CLK_EXT	 6
#define CX_CLK_RCV	 7
#define CX_CLK_DPLL	 8
#define CX_CLK_DPLL_EXT	 14

/* functions dealing with interrupt vector in DOS */
#if defined (MSDOS) || defined (__MSDOS__)
int ddk_int_alloc (int irq, void (*func)(), void *arg);
int ddk_int_restore (int irq);
#endif

int cx_probe_irq (cx_board_t *b, int irq);
void cx_int_handler (cx_board_t *b);

int cx_find (port_t *board_ports);
int cx_open_board (cx_board_t *b, int num, port_t port, int irq, int dma);
void cx_close_board (cx_board_t *b);

void cx_start_chan (cx_chan_t *c, cx_buf_t *cb, unsigned long phys);

/*
 Set port type for old models of Sigma
 */
void cx_set_port (cx_chan_t *c, int iftype);

/*
 Get port type for old models of Sigma
 -1 Fixed port type or auto detect
  0 RS232
  1 V35
  2 RS449
 */
int cx_get_port (cx_chan_t *c);

void cx_enable_receive (cx_chan_t *c, int on);
void cx_enable_transmit (cx_chan_t *c, int on);
int cx_receive_enabled (cx_chan_t *c);
int cx_transmit_enabled (cx_chan_t *c);

void cx_set_baud (cx_chan_t *, unsigned long baud);
int  cx_set_mode (cx_chan_t *c, int mode);
void cx_set_loop (cx_chan_t *c, int on);
void cx_set_nrzi (cx_chan_t *c, int nrzi);
void cx_set_dpll (cx_chan_t *c, int on);

unsigned long cx_get_baud (cx_chan_t *c);
int cx_get_loop (cx_chan_t *c);
int cx_get_nrzi (cx_chan_t *c);
int cx_get_dpll (cx_chan_t *c);

int cx_send_packet (cx_chan_t *c, char *data, int len, void *attachment);
int cx_buf_free (cx_chan_t *c);

void cx_register_transmit (cx_chan_t *c,
	void (*func) (cx_chan_t *c, void *attachment, int len));
void cx_register_receive (cx_chan_t *c,
	void (*func) (cx_chan_t *c, char *data, int len));
void cx_register_modem (cx_chan_t *c, void (*func) (cx_chan_t *c));
void cx_register_error (cx_chan_t *c, void (*func) (cx_chan_t *c, int data));
void	cx_intr_off (cx_board_t *b);
void	cx_intr_on (cx_board_t *b);
int	cx_checkintr (cx_board_t *b);

/* Async functions */
void cx_transmitter_ctl (cx_chan_t *c, int start);
void cx_flush_transmit (cx_chan_t *c);
void cx_xflow_ctl (cx_chan_t *c, int on);
void cx_send_break (cx_chan_t *c, int msec);
void cx_set_async_param (cx_chan_t *c, int baud, int bits, int parity,
	int stop2, int ignpar, int rtscts,
	int ixon, int ixany, int symstart, int symstop);
