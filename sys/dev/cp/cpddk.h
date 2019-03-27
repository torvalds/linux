/*-
 * Cronyx Tau-PCI DDK definitions.
 *
 * Copyright (C) 1999-2003 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * Copyright (C) 2000-2004 Cronyx Engineering.
 * Author: Roman Kurakin, <rik@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 *
 * $Cronyx: cpddk.h,v 1.8.4.20 2004/12/06 16:21:06 rik Exp $
 * $FreeBSD$
 */
#define NBRD		6	/* the maximum number of installed boards */
#define NCHAN		4	/* the number of channels on the board */
#define NRBUF		64	/* the number of receive buffers per channel,
				   min 2 */
#define NTBUF		4	/* the number of transmit buffers per channel */
#define BUFSZ		1664	/* i/o buffer size (26*64, min 1601) */
#define QSZ		128	/* intr queue size (multiple of 32, min 32) */

#ifndef CPDDK_COBF_SAFE
#pragma pack(4)

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
} cp_gstat_t;

typedef struct {			/* cross-connector parameters */
	unsigned char ts [32];		/* timeslot number */
	unsigned char link [32];	/* E1 link number */
} cp_dxc_t;

typedef struct {
	unsigned long len;		/* data buffer length, fe, hold, hi */
#define DESC_FE		0x80000000
#define DESC_HOLD	0x40000000
#define DESC_HI		0x20000000
#define DESC_LEN(v)	((v) >> 16 & 0x1fff)

	unsigned long next;		/* next descriptor pointer */
	unsigned long data;		/* pointer to data buffer */
	unsigned long status;		/* complete, receive abort, fe, len */
#define DESC_RA		0x00000200
#define DESC_C		0x40000000

	unsigned long fe;		/* pointer to frame end descriptor */
} cp_desc_t;

typedef struct {
	cp_desc_t tdesc [NTBUF];	/* transmit buffer descriptors */
	cp_desc_t rdesc [NRBUF];	/* receive buffer descriptors */
	unsigned char tbuffer [NTBUF] [BUFSZ];	/* transmit buffers */
	unsigned char rbuffer [NRBUF] [BUFSZ];	/* receive buffers  */
} cp_buf_t;

typedef struct {
	unsigned long iqrx [NCHAN] [QSZ];	/* rx intr queue */
	unsigned long iqtx [NCHAN] [QSZ];	/* tx intr queue */
	unsigned long iqlx [QSZ];		/* LBI intr queue */
} cp_qbuf_t;

typedef struct _cp_chan_t {
	unsigned char *regs;	/* base addr of channel registers */
	volatile unsigned long *RXBAR, *TXBAR, *CFG;
	volatile unsigned long *BRDA, *FRDA, *LRDA, *BTDA, *FTDA, *LTDA;
	unsigned char CCR, CSR, GMD, GLS, E1CS, E1CR, E1EPS;

	unsigned char num;		/* channel number, 0..1 */
	unsigned char type;		/* channel type */
#define T_NONE		0		/* no channel */
#define T_SERIAL	1		/* V.35/RS */
#define T_G703		2		/* G.703 */
#define T_E1		3		/* E1 */
#define T_E3		4		/* E3 */
#define T_HSSI		5		/* HSSI */
#define T_DATA		6		/* no physical interface */
#define	T_T3		7		/* T3 */
#define T_STS1		8		/* STS1 */

	struct _cp_board_t *board;	/* board pointer */

	unsigned char dtr;		/* DTR signal value */
	unsigned char rts;		/* RTS signal value */
	unsigned long baud;		/* data rate, bps */
	unsigned char dpll;		/* dpll mode */
	unsigned char nrzi;		/* nrzi mode */
	unsigned char invtxc;		/* invert tx clock */
	unsigned char invrxc;		/* invert rx clock */
	unsigned char lloop;		/* local loopback mode */
	unsigned char rloop;		/* remote loopback mode */
	unsigned char gsyn;		/* G.703 clock mode */
#define GSYN_INT	0		/* internal transmit clock source */
#define GSYN_RCV	1		/* transmit clock source = receive */
#define GSYN_RCV0	2		/* tclk = rclk from channel 0 */
#define GSYN_RCV1	3		/* ...from channel 1 */
#define GSYN_RCV2	4		/* ...from channel 2 */
#define GSYN_RCV3	5		/* ...from channel 3 */

	unsigned char scrambler;	/* G.703 scrambler enable */

	unsigned long ts;		/* E1 timeslot mask */
	unsigned char higain;		/* E1 high gain mode */
	unsigned char use16;		/* E1 use ts 16 */
	unsigned char crc4;		/* E1 enable CRC4 */
	unsigned char phony;		/* E1 phony mode */
	unsigned char unfram;		/* E1 unframed mode */
	unsigned char monitor;		/* E1 monitoring mode */
	unsigned char dir;		/* E1 direction mode */
	cp_dxc_t dxc;			/* E1 cross-connect params */

	unsigned char ais;		/* E3 AIS */
	unsigned char losais;		/* E3 AIS on LOS*/
	unsigned char ber;		/* E3 BER */
	unsigned char cablen;		/* E3 cable length */
	unsigned char e3cr1;		/* e3cr1 clone */

	unsigned char scc_ien;		/* SCC Interrupts enabled */
	unsigned char ds_ien;		/* DS Interrupts enabled */

	unsigned long imr;
	unsigned char ccr;		/* CCR image */
	unsigned long ccr0;		/* CCR0 clone */
	unsigned long ccr1;		/* CCR1 clone */
	unsigned long ccr2;		/* CCR2 clone */
	unsigned char gmd;		/* G.703 MDi register image */
	unsigned char e1cr;		/* E1 CR register image */
	unsigned char ds21x54;		/* new tranceiver flag */

	unsigned long rintr;		/* receive interrupts */
	unsigned long tintr;		/* transmit interrupts */
	ulong64 ibytes;			/* input bytes */
	ulong64 obytes;			/* output bytes */
	unsigned long ipkts;		/* input packets */
	unsigned long opkts;		/* output packets */
	unsigned long underrun;		/* output underrun errors */
	unsigned long overrun;		/* input overrun errors */
	unsigned long frame;		/* input frame errors */
	unsigned long crc;		/* input crc errors */

	unsigned short status;		/* E1/G.703 line status bit mask */
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

	unsigned long totsec;		/* total seconds elapsed */
	unsigned long cursec;		/* current seconds elapsed */
	unsigned long degsec;		/* degraded seconds */
	unsigned long degerr;		/* errors during degraded seconds */
	cp_gstat_t currnt;		/* current 15-min interval data */
	cp_gstat_t total;		/* total statistics data */
	cp_gstat_t interval [48];	/* 12 hour period data */
	unsigned long e3status;		/* E3 status */
#define E3STS_LOS	0x00000002	/* Lost of synchronization */
#define E3STS_TXE	0x00000004	/* Transmit error */
#define E3STS_AIS	0x00000008	/* Transmit error */
	unsigned long e3csec_5;		/* 1/5 of second counter */
	unsigned long e3tsec;		/* total seconds coounter */
	unsigned long e3ccv;		/* E3 current 15-min cv errors */
	unsigned long e3tcv;		/* E3 total cv errors */
	unsigned long e3icv[48];	/* E3 12 hour period cv errors */

	unsigned long *iqrx;		/* rx intr queue */
	unsigned long *iqtx;		/* tx intr queue */
	int irn, itn;

	unsigned char *tbuf [NTBUF];	/* transmit buffers */
	cp_desc_t *tdesc;		/* transmit buffer descriptors */
	unsigned long tphys [NTBUF];	/* transmit buffer phys address */
	unsigned long tdphys [NTBUF];	/* transmit descr phys addresses */
	int tn;				/* first active transmit buffer */
	int te;				/* first empty transmit buffer */

	unsigned char *rbuf [NRBUF];	/* receive buffers */
	cp_desc_t *rdesc;		/* receive buffer descriptors */
	unsigned long rphys [NRBUF];	/* receive buffer phys address */
	unsigned long rdphys [NRBUF];	/* receive descr phys addresses */
	int rn;				/* first active receive buffer */

	void *tag [NTBUF];		/* system dependent data per buffer */
	void *sys;			/* system dependent data per channel */
	unsigned char debug;		/* debug level, 0..2 */
	unsigned char debug_shadow;	/* debug shadow */

	void (*transmit) (struct _cp_chan_t *c, void *tag, int len);
	void (*receive) (struct _cp_chan_t *c, unsigned char *data, int len);
	void (*error) (struct _cp_chan_t *c, int reason);
#define CP_FRAME	 1
#define CP_CRC		 2
#define CP_UNDERRUN	 3
#define CP_OVERRUN	 4
#define CP_OVERFLOW	 5
} cp_chan_t;

typedef struct _cp_board_t {
	unsigned char *base;		/* base address of adapter registers */
	unsigned char num;		/* board number, 0..5 */
	unsigned char type;		/* board type Tau/TauE1/TauG703 */
#define B_TAUPCI	1		/* 2 channels V.35/RS */
#define B_TAUPCI_E3	2		/* 1 channel E3 */
#define B_TAUPCI_HSSI	3		/* 1 channel HSSI */
#define B_TAUPCI_G703	4		/* 2 channels G703 */
#define B_TAUPCI_E1	5		/* 2 channels E1 */
#define B_TAUPCI4	6		/* 4 channels V.35/RS */
#define B_TAUPCI4_G703	7		/* 2 channels G.703 + 2 channels V.35/RS */
#define B_TAUPCI4_4G703 8		/* 4 channels G.703 */
#define B_TAUPCI_2E1	9		/* 2 channels E1, 4 data ports */
#define B_TAUPCI4_E1	10		/* 2 channels E1 + 2 channels V.35/RS */
#define B_TAUPCI4_4E1	11		/* 4 channels E1 */
#define B_TAUPCI_L	12		/* 1 channel V.35/RS */

	unsigned long osc;		/* oscillator frequency */
	char name[16];			/* board version name */
	cp_chan_t chan[NCHAN];		/* channel structures */
	unsigned char mux;		/* E1 mux mode */
	unsigned char dxc_cas;		/* CAS cross-connection */
	unsigned char bcr;		/* BCR image */
	unsigned char e1cfg;		/* E1 CFG register image */
	unsigned char gpidle;		/* idle bits of gp port */
	unsigned char E1DATA;
	unsigned long intr;		/* interrupt counter */
	unsigned long *iqlx;		/* LBI intr queue */
	int iln;
	unsigned char fw_type;		/* firmware type */
#define FW_TAUPCI_NONE	0
#define FW_TAUPCI_E3_B	1
#define FW_TAUPCI_2E1_B	2
#define FW_TAUPCI_2E1_A	3
#define FW_TAUPCI_4E1_B	6
#define FW_TAUPCI_4E1_A	7
	unsigned char *firmware[8];	/* external firmware */
	void *sys;
} cp_board_t;

#pragma pack()

/* PCI device identifiers. */
extern unsigned short cp_vendor_id;
extern unsigned short cp_device_id;

/* Initialization. */
unsigned short cp_init (cp_board_t *b, int num, unsigned char *base);
void cp_reset (cp_board_t *b, cp_qbuf_t *buf, unsigned long phys);
void cp_hard_reset (cp_board_t *b);
unsigned long cp_regio (cp_chan_t *c, int op, int reg, unsigned long val);
#define REGIO_INB		0
#define REGIO_IN		1
#define REGIO_INS		2
#define REGIO_INX		3
#define REGIO_INB_OUTB		4
#define REGIO_OUTB		5
#define REGIO_OUTX		6
#define REGIO_R_W		7
#define REGIO_OUT_IN		8
#define REGIO_OUTB_INB		9

/* Callback registration. */
void cp_register_transmit (cp_chan_t *c, void (*func) (cp_chan_t*, void*, int));
void cp_register_receive (cp_chan_t *c, void (*func) (cp_chan_t*,
							unsigned char*, int));
void cp_register_error (cp_chan_t *c, void (*func) (cp_chan_t*, int));

/* Data transmittion. */
void cp_start_chan (cp_chan_t *c, int tx, int rx, cp_buf_t *cb, unsigned long phys);
void cp_stop_chan (cp_chan_t *c);
void cp_start_e1 (cp_chan_t *c);
void cp_stop_e1 (cp_chan_t *c);
int cp_transmit_space (cp_chan_t *c);
int cp_send_packet (cp_chan_t *c, unsigned char *data, int len, void *tag);

/* Interrupt control. */
int cp_interrupt (cp_board_t *b);
int cp_interrupt_poll (cp_board_t *b, int ack);
void cp_handle_interrupt (cp_board_t *b);
void cp_enable_interrupt (cp_board_t *b, int on);

/* G.703 timer. */
void cp_g703_timer (cp_chan_t *c);

/* E1 timer. */
void cp_e1_timer (cp_chan_t *c);

/* E3 timer. */
void cp_e3_timer (cp_chan_t *c);

/* LED control. */
void cp_led (cp_board_t *b, int on);

/* Modem signals. */
void cp_set_dtr (cp_chan_t *c, int on);
void cp_set_rts (cp_chan_t *c, int on);
int cp_get_dsr (cp_chan_t *c);
int cp_get_cd (cp_chan_t *c);
int cp_get_cts (cp_chan_t *c);
int cp_get_txcerr (cp_chan_t *c);
int cp_get_rxcerr (cp_chan_t *c);

/* HDLC parameters. */
void cp_set_baud (cp_chan_t *c, int baud);
void cp_set_dpll (cp_chan_t *c, int on);
void cp_set_nrzi (cp_chan_t *c, int on);
void cp_set_invtxc (cp_chan_t *c, int on);
void cp_set_invrxc (cp_chan_t *c, int on);
void cp_set_lloop (cp_chan_t *c, int on);

/* Channel status, cable type. */
int cp_get_rloop (cp_chan_t *c);
int cp_get_lq (cp_chan_t *c);
int cp_get_cable (cp_chan_t *c);
#define CABLE_RS232		0
#define CABLE_V35		1
#define CABLE_RS530		2
#define CABLE_X21		3
#define CABLE_RS485		4
#define CABLE_NOT_ATTACHED	9
#define CABLE_COAX		10
#define CABLE_TP		11

/* E1/G.703 parameters. */
void cp_set_gsyn (cp_chan_t *c, int syn);
void cp_set_ts (cp_chan_t *c, unsigned long ts);
void cp_set_dir (cp_chan_t *c, int dir);
void cp_set_mux (cp_board_t *b, int on);
void cp_dxc_cas_enable (cp_board_t *b, int on);
void cp_set_dxc (cp_chan_t *c, cp_dxc_t *param);
void cp_set_higain (cp_chan_t *c, int on);
void cp_set_use16 (cp_chan_t *c, int on);
void cp_set_crc4 (cp_chan_t *c, int on);
void cp_set_phony (cp_chan_t *c, int on);
void cp_set_unfram (cp_chan_t *c, int on);
void cp_set_scrambler (cp_chan_t *c, int on);
void cp_set_monitor (cp_chan_t *c, int on);

/* E3 parameters. */
void cp_set_rloop (cp_chan_t *c, int on);
void cp_set_ber (cp_chan_t *c, int on);
void cp_set_cablen (cp_chan_t *c, int on);
void cp_set_losais (cp_chan_t *c, int on);

#endif /* CPDDK_COBF_SAFE */
