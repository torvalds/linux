/*
 * Middle-level code for Cronyx Tau32-PCI adapters.
 *
 * Copyright (C) 2004 Cronyx Engineering
 * Copyright (C) 2004 Roman Kurakin <rik@FreeBSD.org>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 *
 * $Cronyx: ceddk.h,v 1.2.6.1 2005/11/09 13:01:39 rik Exp $
 * $FreeBSD$
 */

#define TAU32_UserContext_Add	void	*sys;
#define TAU32_UserRequest_Add	void	*sys; TAU32_UserRequest *next;

#include <dev/ce/tau32-ddk.h>

#define NCHAN	TAU32_CHANNELS
#ifndef NBRD
#   define NBRD 6
#endif
#if NBRD != 6
#   error "NBRD != 6"
#endif

#define BUFSZ	1664

typedef struct _ce_buf_item_t {
	TAU32_UserRequest req;
	unsigned char buf [BUFSZ+4];
	unsigned long phys;
} ce_buf_item_t;

typedef struct _ce_buf_t {
	ce_buf_item_t tx_item[TAU32_IO_QUEUE];
	ce_buf_item_t rx_item[TAU32_IO_QUEUE];
} ce_buf_t;

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
} ce_gstat_t;

typedef struct _ce_chan_t {
	unsigned char num;
	unsigned char type;
#define T_NONE		0		/* no channel */
#define T_E1		3		/* E1 */
#define T_DATA		6		/* no physical interface */
	
	struct _ce_board_t *board;
	unsigned char dtr;
	unsigned char rts;
	ce_buf_item_t *tx_item;
	ce_buf_item_t *rx_item;
	TAU32_UserRequest *rx_queue;
	TAU32_UserRequest *tx_queue;
	unsigned char debug;
	unsigned char debug_shadow;
	void (*transmit) (struct _ce_chan_t*, void*, int);
	void (*receive) (struct _ce_chan_t*, unsigned char*, int);
	void (*error) (struct _ce_chan_t*, int);
#define CE_FRAME	 1
#define CE_CRC		 2
#define CE_UNDERRUN	 3
#define CE_OVERRUN	 4
#define CE_OVERFLOW	 5
	int tx_pending;
	int rx_pending;
	unsigned long rintr;
	unsigned long tintr;
	ulong64	ibytes;
	ulong64	obytes;
	unsigned long ipkts;
	unsigned long opkts;
	unsigned long underrun;
	unsigned long overrun;
	unsigned long frame;
	unsigned long crc;

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
	ce_gstat_t currnt;		/* current 15-min interval data */
	ce_gstat_t total;		/* total statistics data */
	ce_gstat_t interval [48];	/* 12 hour period data */

	unsigned int acc_status;
	unsigned long config;
	unsigned long baud;
	unsigned long ts;
	unsigned long ts_mask;
	unsigned char dir;
	unsigned char lloop;
	unsigned char rloop;
	unsigned char higain;
	unsigned char phony;
	unsigned char scrambler;
	unsigned char unfram;
	unsigned char monitor;
	unsigned char crc4;
	unsigned char use16;
	unsigned char gsyn;		/* G.703 clock mode */
#define GSYN_INT	0		/* internal transmit clock source */
#define GSYN_RCV	1		/* transmit clock source = receive */
#define GSYN_RCV0	2		/* tclk = rclk from channel 0 */
#define GSYN_RCV1	3		/* ...from channel 1 */
	unsigned long mtu;
	void *sys;
} ce_chan_t;

#define CONFREQSZ	128
typedef struct _ce_conf_req {
	TAU32_UserRequest req[CONFREQSZ+10];
	TAU32_UserRequest *queue;
	int	pending;
} ce_conf_req;

typedef struct _ce_board_t {
	TAU32_UserContext	ddk;
	ce_chan_t		chan[NCHAN];
	int			num;
	int			mux;
#define TAU32_BASE_NAME		"Tau-PCI-32"
#define TAU32_LITE_NAME		"Tau-PCI-32/Lite"
#define TAU32_ADPCM_NAME	"Tau-PCI-32/ADPCM"
#define TAU32_UNKNOWN_NAME	"Unknown Tau-PCI-32"
	char			name [32];
	ce_conf_req		cr;
	TAU32_CrossMatrix	dxc;
	unsigned long		pmask;
	void *sys;
} ce_board_t;

void ce_set_dtr (ce_chan_t *c, int on);
void ce_set_rts (ce_chan_t *c, int on);
int ce_get_cd (ce_chan_t *c);
int ce_get_cts (ce_chan_t *c);
int ce_get_dsr (ce_chan_t *c);

int ce_transmit_space (ce_chan_t *c);
int ce_send_packet (ce_chan_t *c, unsigned char *buf, int len, void *tag);
void ce_start_chan (ce_chan_t *c, int tx, int rx, ce_buf_t *cb, unsigned long phys);
void ce_stop_chan (ce_chan_t *c);
void ce_register_transmit (ce_chan_t *c, void (*func) (ce_chan_t*, void*, int));
void ce_register_receive (ce_chan_t *c, void (*func) (ce_chan_t*,
							unsigned char*, int));
void ce_register_error (ce_chan_t *c, void (*func) (ce_chan_t*, int));

void TAU32_CALLBACK_TYPE
	ce_error_callback(TAU32_UserContext *pContext, int Item,
			  unsigned NotifyBits);
void TAU32_CALLBACK_TYPE
	ce_status_callback(TAU32_UserContext *pContext, int Item,
			  unsigned NotifyBits);

void ce_set_baud (ce_chan_t *c, unsigned long baud);
void ce_set_lloop (ce_chan_t *c, unsigned char on);
void ce_set_rloop (ce_chan_t *c, unsigned char on);
void ce_set_higain (ce_chan_t *c, unsigned char on);
void ce_set_unfram (ce_chan_t *c, unsigned char on);
void ce_set_ts (ce_chan_t *c, unsigned long ts);
void ce_set_phony (ce_chan_t *c, unsigned char on);
void ce_set_scrambler (ce_chan_t *c, unsigned char on);
void ce_set_monitor (ce_chan_t *c, unsigned char on);
void ce_set_use16 (ce_chan_t *c, unsigned char on);
void ce_set_crc4 (ce_chan_t *c, unsigned char on);
void ce_set_gsyn (ce_chan_t *c, int syn);
#define CABLE_TP		11
int ce_get_cable (ce_chan_t *c);
void ce_set_dir (ce_chan_t *c, int dir);
void ce_e1_timer (ce_chan_t *c);
void ce_init_board (ce_board_t *b);
