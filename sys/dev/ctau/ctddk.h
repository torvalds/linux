/*-
 * Defines for Cronyx-Tau adapter driver.
 *
 * Copyright (C) 1994-2003 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * Cronyx Id: ctddk.h,v 1.1.2.3 2003/12/11 17:33:44 rik Exp $
 * $FreeBSD$
 */
#define NBRD		3	/* the maximum number of installed boards */
#define NPORT		32	/* the number of i/o ports per board */
#define NCHAN		2	/* the number of channels on the board */
#define NBUF		4	/* the number of buffers per direction */
#define DMABUFSZ	1600	/* buffer size */
#define SCCBUFSZ	50

#ifndef port_t
#   ifdef _M_ALPHA			/* port address on Alpha under */
#      define port_t unsigned long	/* Windows NT is 32 bit long */
#   else
#      define port_t unsigned short	/* all other architectures */
#   endif				/* have 16-bit port addresses */
#endif

/*
 * There are tree models of Tau adapters.
 * Each of two channels of the adapter is assigned a type:
 *
 *		Channel 0	Channel 1
 * ------------------------------------------
 * Tau		T_SERIAL	T_SERIAL
 * Tau/E1	T_E1		T_E1_SERIAL
 * Tau/G703	T_G703		T_G703_SERIAL
 *
 * Each channel could work in one of several modes:
 *
 *		Channel 0	Channel 1
 * ------------------------------------------
 * Tau		M_ASYNC,	M_ASYNC,
 *		M_HDLC		M_HDLC
 * ------------------------------------------
 * Tau/E1	M_E1,		M_E1,
 *		M_E1 & CFG_D,	M_E1 & CFG_D,
 *				M_ASYNC,
 *				M_HDLC
 * ------------------------------------------
 * Tau/G703	M_G703, 	M_G703,
 *				M_ASYNC,
 *				M_HDLC
 * ------------------------------------------
 */
#define B_TAU		0	/* Tau - basic model */
#define B_TAU_E1	1	/* Tau/E1 */
#define B_TAU_G703	2	/* Tau/G.703 */
#define B_TAU_E1C	3	/* Tau/E1 revision C */
#define B_TAU_E1D	4	/* Tau/E1 revision C with phony mode support */
#define B_TAU_G703C	5	/* Tau/G.703 revision C */
#define B_TAU2		6	/* Tau2 - basic model */
#define B_TAU2_E1	7	/* Tau2/E1 */
#define B_TAU2_E1D	8	/* Tau2/E1 with phony mode support */
#define B_TAU2_G703	9	/* Tau2/G.703 */

#define T_SERIAL	1
#define T_E1		2
#define T_G703		4
#define T_E1_SERIAL	(T_E1 | T_SERIAL)
#define T_G703_SERIAL	(T_G703 | T_SERIAL)

#define M_ASYNC 	0	/* asynchronous mode */
#define M_HDLC		1	/* bit-sync mode (HDLC) */
#define M_G703		2
#define M_E1		3

#define CFG_A		0
#define CFG_B		1
#define CFG_C		2
#define CFG_D		3

/* E1/G.703 interfaces - i0, i1
 * Digital interface   - d0
 *
 *
 * Configuration
 * ---------------------------------------------------
 * CFG_A	 | i0<->ct0   i1<->ct1
 * ---------------------------------------------------
 * CFG_B	 | i0<->ct0   d0<->ct1
 *		 |  ^
 *		 |  |
 *		 |  v
 *		 | i1
 * ---------------------------------------------------
 * CFG_C	 | ct0<->i0<->ct1
 *		 |	  ^
 *		 |	  |
 *		 |	  v
 *		 |	 i1
 * ---------------------------------------------------
 * CFG_D	 | i0(e1)<->hdlc<->hdlc<->ct0(e1)
 * ONLY TAU/E1	 | i1(e1)<->hdlc<->hdlc<->ct1(e1)
 *		 |
 */

/*
 * Mode register 0 (MD0) bits.
 */
#define MD0_STOPB_1	0		/* 1 stop bit */
#define MD0_STOPB_15	1		/* 1.5 stop bits */
#define MD0_STOPB_2	2		/* 2 stop bits */

#define MD0_MODE_ASYNC	 0		/* asynchronous mode */
#define MD0_MODE_EXTSYNC 3		/* external byte-sync mode */
#define MD0_MODE_HDLC	 4		/* HDLC mode */

typedef struct {
	unsigned stopb : 2;		/* stop bit length */
	unsigned : 2;
	unsigned cts_rts_dcd : 1;	/* auto-enable CTS/DCD/RTS */
	unsigned mode : 3;		/* protocol mode */
} ct_md0_async_t;

typedef struct {
	unsigned crcpre : 1;		/* CRC preset 1s / 0s */
	unsigned ccitt : 1;		/* CRC-CCITT / CRC-16 */
	unsigned crc : 1;		/* CRC enable */
	unsigned : 1;
	unsigned cts_dcd : 1;		/* auto-enable CTS/DCD */
	unsigned mode : 3;		/* protocol mode */
} ct_md0_hdlc_t;

/*
 * Mode register 1 (MD1) bits.
 */
#define MD1_PAR_NO	0	/* no parity */
#define MD1_PAR_CMD	1	/* parity bit appended by command */
#define MD1_PAR_EVEN	2	/* even parity */
#define MD1_PAR_ODD	3	/* odd parity */

#define MD1_CLEN_8	0	/* 8 bits/character */
#define MD1_CLEN_7	1	/* 7 bits/character */
#define MD1_CLEN_6	2	/* 6 bits/character */
#define MD1_CLEN_5	3	/* 5 bits/character */

#define MD1_CLK_1	0	/* 1/1 clock rate */
#define MD1_CLK_16	1	/* 1/16 clock rate */
#define MD1_CLK_32	2	/* 1/32 clock rate */
#define MD1_CLK_64	3	/* 1/64 clock rate */

#define MD1_ADDR_NOCHK	0	/* do not check address field */
#define MD1_ADDR_SNGLE1 1	/* single address 1 */
#define MD1_ADDR_SNGLE2 2	/* single address 2 */
#define MD1_ADDR_DUAL	3	/* dual address */

typedef struct {
	unsigned parmode : 2;	/* parity mode */
	unsigned rxclen : 2;	/* receive character length */
	unsigned txclen : 2;	/* transmit character length */
	unsigned clk : 2;	/* clock rate */
} ct_md1_async_t;

typedef struct {
	unsigned : 6;
	unsigned addr : 2;	/* address field check */
} ct_md1_hdlc_t;

/*
 * Mode register 2 (MD2) bits.
 */
#define MD2_FDX 	0	/* full duplex communication */
#define MD2_RLOOP	1	/* remote loopback (auto echo) */
#define MD2_LLOOP	3	/* local+remote loopback */

#define MD2_DPLL_CLK_8	0	/* x8 ADPLL clock rate */
#define MD2_DPLL_CLK_16 1	/* x16 ADPLL clock rate */
#define MD2_DPLL_CLK_32 2	/* x32 ADPLL clock rate */

#define MD2_ENCOD_NRZ	     0	/* NRZ encoding */
#define MD2_ENCOD_NRZI	     1	/* NRZI encoding */
#define MD2_ENCOD_MANCHESTER 4	/* Manchester encoding */
#define MD2_ENCOD_FM0	     5	/* FM0 encoding */
#define MD2_ENCOD_FM1	     6	/* FM1 encoding */

typedef struct {
	unsigned loop : 2;	/* loopback mode */
	unsigned : 1;
	unsigned dpll_clk : 2;	/* ADPLL clock rate */
	unsigned encod : 3;	/* signal encoding NRZ/NRZI/etc. */
} ct_md2_t;

/*
 * DMA priority control register (PCR) values.
 */
#define PCR_PRIO_0_1	0	/* priority c0r > c0t > c1r > c1t */
#define PCR_PRIO_1_0	1	/* priority c1r > c1t > c0r > c0t */
#define PCR_PRIO_RX_TX	2	/* priority c0r > c1r > c0t > c1t */
#define PCR_PRIO_TX_RX	3	/* priority c0t > c1t > c0r > c1r */
#define PCR_PRIO_ROTATE 4	/* rotation priority -c0r-c0t-c1r-c1t- */

typedef struct {
	unsigned prio : 3;	/* priority of channels */
	unsigned noshare : 1;	/* 1 - chan holds the bus until end of data */
				/* 0 - all channels share the bus hold */
	unsigned release : 1;	/* 1 - release the bus between transfers */
				/* 0 - hold the bus until all transfers done */
} ct_pcr_t;

typedef struct {		/* hdlc channel options */
	ct_md0_hdlc_t md0;	/* mode register 0 */
	ct_md1_hdlc_t md1;	/* mode register 1 */
	unsigned char ctl;	/* control register */
	unsigned char sa0;	/* sync/address register 0 */
	unsigned char sa1;	/* sync/address register 1 */
	unsigned char rxs;	/* receive clock source */
	unsigned char txs;	/* transmit clock source */
} ct_opt_hdlc_t;

typedef struct {
	ct_md2_t md2;		/* mode register 2 */
	unsigned char dma_rrc;	/* DMA mode receive FIFO ready level */
	unsigned char dma_trc0; /* DMA mode transmit FIFO empty mark */
	unsigned char dma_trc1; /* DMA mode transmit FIFO full mark */
	unsigned char pio_rrc;	/* port i/o mode receive FIFO ready level */
	unsigned char pio_trc0; /* port i/o transmit FIFO empty mark */
	unsigned char pio_trc1; /* port i/o transmit FIFO full mark */
} ct_chan_opt_t;

/*
 * Option CLK is valid for both E1 and G.703 models.
 * Options RATE, PCE, TEST are for G.703 only.
 */
#define GCLK_INT	0	/* internal transmit clock source */
#define GCLK_RCV	1	/* transmit clock source = receive */
#define GCLK_RCLKO	2	/* tclk = receive clock of another channel */

#define GTEST_DIS	0	/* test disabled, normal operation */
#define GTEST_0 	1	/* test "all zeros" */
#define GTEST_1 	2	/* test "all ones" */
#define GTEST_01	3	/* test "0/1" */

typedef struct {		/* E1/G.703 channel options */
	unsigned char hdb3;	/* encoding HDB3/AMI */
	unsigned char pce;	/* precoder enable */
	unsigned char test;	/* test mode 0/1/01/disable */
	unsigned char crc4;	/* E1 CRC4 enable */
	unsigned char cas;	/* E1 signalling mode CAS/CCS */
	unsigned char higain;	/* E1 high gain amplifier (30 dB) */
	unsigned char phony;	/* E1 phony mode */
	unsigned char pce2;	/* old PCM2 precoder compatibility */
	unsigned long rate;	/* data rate 2048/1024/512/256/128/64 kbit/s */
	unsigned short level;	/* G.703 input signal level, -cB */
} ct_opt_g703_t;

typedef struct {
	unsigned char bcr2;	/* board control register 2 */
	ct_pcr_t pcr;		/* DMA priority control register */
	unsigned char clk0;	/* E1/G.703 chan 0 txclk src int/rcv/rclki */
	unsigned char clk1;	/* E1/G.703 chan 1 txclk src int/rcv/rclki */
	unsigned char cfg;	/* E1 configuration II/HI/K */
	unsigned long s0;	/* E1 channel 0 timeslot mask */
	unsigned long s1;	/* E1 channel 1 timeslot mask */
	unsigned long s2;	/* E1 subchannel pass-through timeslot mask */
} ct_board_opt_t;

/*
 * Board control register 2 bits.
 */
#define BCR2_INVTXC0	0x10	/* channel 0 invert transmit clock */
#define BCR2_INVTXC1	0x20	/* channel 1 invert transmit clock */
#define BCR2_INVRXC0	0x40	/* channel 0 invert receive clock */
#define BCR2_INVRXC1	0x80	/* channel 1 invert receive clock */

#define BCR2_BUS_UNLIM	0x01	/* unlimited DMA master burst length */
#define BCR2_BUS_RFST	0x02	/* fast read cycle bus timing */
#define BCR2_BUS_WFST	0x04	/* fast write cycle bus timing */

/*
 * Receive/transmit clock source register (RXS/TXS) bits - from hdc64570.h.
 */
#define CLK_MASK	  0x70	/* RXC/TXC clock input mask */
#define CLK_LINE	  0x00	/* RXC/TXC line input */
#define CLK_INT 	  0x40	/* internal baud rate generator */

#define CLK_RXS_LINE_NS   0x20	/* RXC line with noise suppression */
#define CLK_RXS_DPLL_INT  0x60	/* ADPLL based on internal BRG */
#define CLK_RXS_DPLL_LINE 0x70	/* ADPLL based on RXC line */

#define CLK_TXS_RECV	  0x60	/* receive clock */

/*
 * Control register (CTL) bits - from hdc64570.h.
 */
#define CTL_RTS_INV	0x01	/* RTS control bit (inverted) */
#define CTL_SYNCLD	0x04	/* load SYN characters */
#define CTL_BRK 	0x08	/* async: send break */
#define CTL_IDLE_MARK	0	/* HDLC: when idle, transmit mark */
#define CTL_IDLE_PTRN	0x10	/* HDLC: when idle, transmit an idle pattern */
#define CTL_UDRN_ABORT	0	/* HDLC: on underrun - abort */
#define CTL_UDRN_FCS	0x20	/* HDLC: on underrun - send FCS/flag */

typedef struct {
	unsigned long bpv;		/* bipolar violations */
	unsigned long fse;		/* frame sync errors */
	unsigned long crce;		/* CRC errors */
	unsigned long rcrce;		/* remote CRC errors (E-bit) */
	unsigned long uas;		/* unavailable seconds */
	unsigned long les;		/* line errored seconds */
	unsigned long es;		/* errored seconds */
	unsigned long bes;		/* bursty errored seconds */
	unsigned long ses;		/* severely errored seconds */
	unsigned long oofs;		/* out of frame seconds */
	unsigned long css;		/* controlled slip seconds */
	unsigned long dm;		/* degraded minutes */
} ct_gstat_t;

#define ESTS_NOALARM	0x0001		/* no alarm present */
#define ESTS_FARLOF	0x0002		/* receiving far loss of framing */
#define ESTS_AIS	0x0008		/* receiving all ones */
#define ESTS_LOF	0x0020		/* loss of framing */
#define ESTS_LOS	0x0040		/* loss of signal */
#define ESTS_AIS16	0x0100		/* receiving all ones in timeslot 16 */
#define ESTS_FARLOMF	0x0200		/* receiving alarm in timeslot 16 */
#define ESTS_LOMF	0x0400		/* loss of multiframe sync */
#define ESTS_TSTREQ	0x0800		/* test code detected */
#define ESTS_TSTERR	0x1000		/* test error */

typedef struct {
	unsigned char data[10];
} ct_desc_t;

typedef struct {
	unsigned char tbuffer [NBUF] [DMABUFSZ]; /* transmit buffers */
	unsigned char rbuffer [NBUF] [DMABUFSZ]; /* receive buffers  */
	ct_desc_t descbuf [4*NBUF];	 /* descriptors */
					 /* double size for alignment */
} ct_buf_t;

#define B_NEXT(b)   (*(unsigned short*)(b).data)     /* next descriptor ptr */
#define B_PTR(b)    (*(unsigned long*) ((b).data+2)) /* ptr to data buffer */
#define B_LEN(b)    (*(unsigned short*)((b).data+6)) /* data buffer length */
#define B_STATUS(b) (*(unsigned short*)((b).data+8)) /* buf status, see FST */

typedef struct {
	port_t DAR, DARB, SAR, SARB, CDA, EDA, BFL, BCR, DSR,
	       DMR, FCT, DIR, DCR, TCNT, TCONR, TCSR, TEPR;
} ct_dmareg_t;

#ifdef NDIS_MINIPORT_DRIVER
typedef struct _ct_queue_t {		/* packet queue */
	PNDIS_WAN_PACKET	head;	/* first packet in queue */
	PNDIS_WAN_PACKET	tail;	/* last packet in queue */
} ct_queue_t;
#endif

typedef struct _ct_chan_t {
	port_t MD0, MD1, MD2, CTL, RXS, TXS, TMC, CMD, ST0,
	       ST1, ST2, ST3, FST, IE0, IE1, IE2, FIE, SA0,
	       SA1, IDL, TRB, RRC, TRC0, TRC1, CST;
	ct_dmareg_t RX; 	/* RX DMA/timer registers */
	ct_dmareg_t TX; 	/* TX DMA/timer registers */

	unsigned char num;		/* channel number, 0..1 */
	struct _ct_board_t *board;	/* board pointer */
	unsigned long baud;		/* data rate */
	unsigned char type;		/* channel type */
	unsigned char mode;		/* channel mode */
	ct_chan_opt_t opt;		/* common channel options */
	ct_opt_hdlc_t hopt;		/* hdlc mode options */
	ct_opt_g703_t gopt;		/* E1/G.703 options */
	unsigned char dtr;		/* DTR signal value */
	unsigned char rts;		/* RTS signal value */
	unsigned char lx;		/* LXT input bit settings */

	unsigned char *tbuf [NBUF];	/* transmit buffer */
	ct_desc_t *tdesc;		/* transmit buffer descriptors */
	unsigned long tphys [NBUF];	/* transmit buffer phys address */
	unsigned long tdphys [NBUF];	/* transmit descr phys addresses */
	int tn; 			/* first active transmit buffer */
	int te; 			/* first active transmit buffer */

	unsigned char *rbuf [NBUF];	/* receive buffers */
	ct_desc_t *rdesc;		/* receive buffer descriptors */
	unsigned long rphys [NBUF];	/* receive buffer phys address */
	unsigned long rdphys [NBUF];	/* receive descr phys addresses */
	int rn; 			/* first active receive buffer */

	unsigned long rintr;		/* receive interrupts */
	unsigned long tintr;		/* transmit interrupts */
	unsigned long mintr;		/* modem interrupts */
	unsigned long ibytes;		/* input bytes */
	unsigned long ipkts;		/* input packets */
	unsigned long ierrs;		/* input errors */
	unsigned long obytes;		/* output bytes */
	unsigned long opkts;		/* output packets */
	unsigned long oerrs;		/* output errors */

	unsigned short status;		/* line status bit mask */
	unsigned long totsec;		/* total seconds elapsed */
	unsigned long cursec;		/* total seconds elapsed */
	unsigned long degsec;		/* degraded seconds */
	unsigned long degerr;		/* errors during degraded seconds */
	ct_gstat_t currnt;		/* current 15-min interval data */
	ct_gstat_t total;		/* total statistics data */
	ct_gstat_t interval [48];	/* 12 hour period data */

	void *attach [NBUF];		/* system dependent data per buffer */
	void *sys;			/* system dependent data per channel */
	int debug;
	int debug_shadow;

	int e1_first_int;
	unsigned char *sccrx, *scctx;  /* pointers to SCC rx and tx buffers */
	int sccrx_empty, scctx_empty;  /* flags : set when buffer is empty  */
	int sccrx_b, scctx_b;	       /* first byte in queue	   */
	int sccrx_e, scctx_e;	       /* first free byte in queue */

	/* pointers to callback functions */
	void (*call_on_tx) (struct _ct_chan_t*, void*, int);
	void (*call_on_rx) (struct _ct_chan_t*, char*, int);
	void (*call_on_msig) (struct _ct_chan_t*);
	void (*call_on_scc) (struct _ct_chan_t*);
	void (*call_on_err) (struct _ct_chan_t*, int);

#ifdef NDIS_MINIPORT_DRIVER		/* NDIS 3 - WinNT/Win95 */
	HTAPI_LINE		htline; /* TAPI line descriptor */
	HTAPI_CALL		htcall; /* TAPI call descriptor */
	NDIS_HANDLE		connect; /* WAN connection context */
	ct_queue_t		sendq;	/* packets to transmit queue */
	ct_queue_t		busyq;	/* transmit busy queue */
	UINT			state;	/* line state mask */
	int			timo;	/* state timeout counter */
#endif
} ct_chan_t;

typedef struct _ct_board_t {
	port_t port;			/* base board port, 200..3e0 */
	unsigned short num;		/* board number, 0..2 */
	unsigned char irq;		/* intterupt request {3 5 7 10 11 12 15} */
	unsigned char dma;		/* DMA request {5 6 7} */
	unsigned long osc;		/* oscillator frequency: 10MHz or 8.192 */
	unsigned char type;		/* board type Tau/TauE1/TauG703 */
	char name[16];			/* board version name */
	unsigned char bcr0;		/* BCR0 image */
	unsigned char bcr1;		/* BCR1 image */
	unsigned char bcr2;		/* BCR2 image */
	unsigned char gmd0;		/* G.703 MD0 register image */
	unsigned char gmd1;		/* G.703 MD1 register image */
	unsigned char gmd2;		/* G.703 MD2 register image */
	unsigned char e1cfg;		/* E1 CFG register image */
	unsigned char e1syn;		/* E1 SYN register image */
	ct_board_opt_t opt;		/* board options */
	ct_chan_t chan[NCHAN];		/* channel structures */
#ifdef NDIS_MINIPORT_DRIVER		/* NDIS 3 - WinNT/Win95 */
	PVOID			ioaddr; /* mapped i/o port address */
	NDIS_HANDLE		mh;	/* miniport adapter handler */
	NDIS_MINIPORT_INTERRUPT irqh;	/* interrupt handler */
	NDIS_HANDLE		dmah;	/* dma channel handler */
	ULONG			bufsz;	/* size of shared memory buffer */
	PVOID			buf;	/* shared memory for adapter */
	NDIS_PHYSICAL_ADDRESS	bphys;	/* shared memory phys address */
	NDIS_SPIN_LOCK		lock;	/* lock descriptor */
	ULONG			debug;	/* debug flags */
	ULONG			idbase; /* TAPI device identifier base number */
	ULONG			anum;	/* adapter number, from inf setup script */
	NDIS_MINIPORT_TIMER	timer;	/* periodic timer structure */
#endif
} ct_board_t;

extern long ct_baud;
extern unsigned char ct_chan_mode;

extern ct_board_opt_t ct_board_opt_dflt; /* default board options */
extern ct_chan_opt_t ct_chan_opt_dflt;	 /* default channel options */
extern ct_opt_hdlc_t ct_opt_hdlc_dflt;	 /* default hdlc mode options */
extern ct_opt_g703_t ct_opt_g703_dflt;	 /* default E1/G.703 options */

struct _cr_dat_tst;
int ct_probe_board (port_t port, int irq, int dma);
void ct_init (ct_board_t *b, int num, port_t port, int irq, int dma,
	const unsigned char *firmware, long bits,
	const struct _cr_dat_tst *tst, const unsigned char *firmware2);
void ct_init_board (ct_board_t *b, int num, port_t port, int irq, int dma,
	int type, long osc);
int ct_download (port_t port, const unsigned char *firmware, long bits,
	const struct _cr_dat_tst *tst);
int ct_download2 (port_t port, const unsigned char *firmware);
int ct_setup_board (ct_board_t *b, const unsigned char *firmware,
	long bits, const struct _cr_dat_tst *tst);
void ct_setup_e1 (ct_board_t *b);
void ct_setup_g703 (ct_board_t *b);
void ct_setup_chan (ct_chan_t *c);
void ct_update_chan (ct_chan_t *c);
void ct_start_receiver (ct_chan_t *c, int dma, unsigned long buf1,
	unsigned len, unsigned long buf, unsigned long lim);
void ct_start_transmitter (ct_chan_t *c, int dma, unsigned long buf1,
	unsigned len, unsigned long buf, unsigned long lim);
void ct_set_dtr (ct_chan_t *c, int on);
void ct_set_rts (ct_chan_t *c, int on);
void ct_set_brk (ct_chan_t *c, int on);
void ct_led (ct_board_t *b, int on);
void ct_cmd (port_t base, int cmd);
void ct_disable_dma (ct_board_t *b);
void ct_reinit_board (ct_board_t *b);
void ct_reinit_chan (ct_chan_t *c);
int ct_get_dsr (ct_chan_t *c);
int ct_get_cd (ct_chan_t *c);
int ct_get_cts (ct_chan_t *c);
int ct_get_lq (ct_chan_t *c);
void ct_compute_clock (long hz, long baud, int *txbr, int *tmc);
unsigned char cte_in (port_t base, unsigned char reg);
void cte_out (port_t base, unsigned char reg, unsigned char val);
unsigned char cte_ins (port_t base, unsigned char reg,
	unsigned char mask);
unsigned char cte_in2 (port_t base, unsigned char reg);
void cte_out2 (port_t base, unsigned char reg, unsigned char val);
void ctg_outx (ct_chan_t *c, unsigned char reg, unsigned char val);
unsigned char ctg_inx (ct_chan_t *c, unsigned char reg);
unsigned char cte_in2d (ct_chan_t *c);
void cte_out2d (ct_chan_t *c, unsigned char val);
void cte_out2c (ct_chan_t *c, unsigned char val);

/* functions dealing with interrupt vector in DOS */
#if defined (MSDOS) || defined (__MSDOS__)
int ddk_int_alloc (int irq, void (*func)(), void *arg);
int ddk_int_restore (int irq);
#endif

int ct_probe_irq (ct_board_t *b, int irq);
void ct_int_handler (ct_board_t *b);
void ct_g703_timer (ct_chan_t *c);

/* DDK errors */
#define CT_FRAME		1
#define CT_CRC			2
#define CT_OVERRUN		3
#define CT_OVERFLOW		4
#define CT_UNDERRUN		5
#define CT_SCC_OVERRUN		6
#define CT_SCC_FRAME		7
#define CT_SCC_OVERFLOW 	8

int ct_open_board (ct_board_t *b, int num, port_t port, int irq, int dma);
void ct_close_board (ct_board_t *b);
int ct_find (port_t *board_ports);

int ct_set_config (ct_board_t *b, int cfg);
int ct_set_clk (ct_chan_t *c, int clk);
int ct_set_ts (ct_chan_t *c, unsigned long ts);
int ct_set_subchan (ct_board_t *b, unsigned long ts);
int ct_set_higain (ct_chan_t *c, int on);
void ct_set_phony (ct_chan_t *c, int on);

#define ct_get_config(b)    ((b)->opt.cfg)
#define ct_get_subchan(b)   ((b)->opt.s2)
#define ct_get_higain(c)    ((c)->gopt.higain)
#define ct_get_phony(c)     ((c)->gopt.phony)
int ct_get_clk (ct_chan_t *c);
unsigned long ct_get_ts (ct_chan_t *c);

void ct_start_chan (ct_chan_t *c, ct_buf_t *cb, unsigned long phys);
void ct_enable_receive (ct_chan_t *c, int on);
void ct_enable_transmit (ct_chan_t *c, int on);
int ct_receive_enabled (ct_chan_t *c);
int ct_transmit_enabled (ct_chan_t *c);

void ct_set_baud (ct_chan_t *c, unsigned long baud);
unsigned long ct_get_baud (ct_chan_t *c);

void ct_set_dpll (ct_chan_t *c, int on);
int ct_get_dpll (ct_chan_t *c);

void ct_set_nrzi (ct_chan_t *c, int on);
int ct_get_nrzi (ct_chan_t *c);

void ct_set_loop (ct_chan_t *c, int on);
int ct_get_loop (ct_chan_t *c);

void ct_set_invtxc (ct_chan_t *c, int on);
int ct_get_invtxc (ct_chan_t *c);
void ct_set_invrxc (ct_chan_t *c, int on);
int ct_get_invrxc (ct_chan_t *c);

int ct_buf_free (ct_chan_t *c);
int ct_send_packet (ct_chan_t *c, unsigned char *data, int len,
	void *attachment);

void ct_start_scc (ct_chan_t *c, char *rxbuf, char * txbuf);
int sccrx_check (ct_chan_t *c);
int scc_read (ct_chan_t *c, unsigned char *d, int len);
int scc_write (ct_chan_t *c, unsigned char *d, int len);
int scc_read_byte (ct_chan_t *c);
int scc_write_byte (ct_chan_t *c, unsigned char b);

void ct_register_transmit (ct_chan_t *c,
	void (*func) (ct_chan_t*, void *attachment, int len));
void ct_register_receive (ct_chan_t *c,
	void (*func) (ct_chan_t*, char *data, int len));
void ct_register_error (ct_chan_t *c,
	void (*func) (ct_chan_t *c, int data));
void ct_register_modem (ct_chan_t *c, void (*func) (ct_chan_t *c));
void ct_register_scc (ct_chan_t *c, void (*func) (ct_chan_t *c));
