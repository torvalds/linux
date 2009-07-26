/*
 *
 * Author	Karsten Keil <kkeil@novell.com>
 *
 *   Basic declarations for the mISDN HW channels
 *
 * Copyright 2008  by Karsten Keil <kkeil@novell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef MISDNHW_H
#define MISDNHW_H
#include <linux/mISDNif.h>
#include <linux/timer.h>

/*
 * HW DEBUG 0xHHHHGGGG
 * H - hardware driver specific bits
 * G - for all drivers
 */

#define DEBUG_HW		0x00000001
#define DEBUG_HW_OPEN		0x00000002
#define DEBUG_HW_DCHANNEL	0x00000100
#define DEBUG_HW_DFIFO		0x00000200
#define DEBUG_HW_BCHANNEL	0x00001000
#define DEBUG_HW_BFIFO		0x00002000

#define MAX_DFRAME_LEN_L1	300
#define MAX_MON_FRAME		32
#define MAX_LOG_SPACE		2048
#define MISDN_COPY_SIZE		32

/* channel->Flags bit field */
#define FLG_TX_BUSY		0	/* tx_buf in use */
#define FLG_TX_NEXT		1	/* next_skb in use */
#define FLG_L1_BUSY		2	/* L1 is permanent busy */
#define FLG_L2_ACTIVATED	3	/* activated from L2 */
#define FLG_OPEN		5	/* channel is in use */
#define FLG_ACTIVE		6	/* channel is activated */
#define FLG_BUSY_TIMER		7
/* channel type */
#define FLG_DCHANNEL		8	/* channel is D-channel */
#define FLG_BCHANNEL		9	/* channel is B-channel */
#define FLG_ECHANNEL		10	/* channel is E-channel */
#define FLG_TRANSPARENT		12	/* channel use transparent data */
#define FLG_HDLC		13	/* channel use hdlc data */
#define FLG_L2DATA		14	/* channel use L2 DATA primitivs */
#define FLG_ORIGIN		15	/* channel is on origin site */
/* channel specific stuff */
#define FLG_FILLEMPTY		16	/* fill fifo on first frame (empty) */
/* arcofi specific */
#define FLG_ARCOFI_TIMER	17
#define FLG_ARCOFI_ERROR	18
/* isar specific */
#define FLG_INITIALIZED		17
#define FLG_DLEETX		18
#define FLG_LASTDLE		19
#define FLG_FIRST		20
#define FLG_LASTDATA		21
#define FLG_NMD_DATA		22
#define FLG_FTI_RUN		23
#define FLG_LL_OK		24
#define FLG_LL_CONN		25
#define FLG_DTMFSEND		26

/* workq events */
#define FLG_RECVQUEUE		30
#define	FLG_PHCHANGE		31

#define schedule_event(s, ev)	do { \
					test_and_set_bit(ev, &((s)->Flags)); \
					schedule_work(&((s)->workq)); \
				} while (0)

struct dchannel {
	struct mISDNdevice	dev;
	u_long			Flags;
	struct work_struct	workq;
	void			(*phfunc) (struct dchannel *);
	u_int			state;
	void			*l1;
	void			*hw;
	int			slot;	/* multiport card channel slot */
	struct timer_list	timer;
	/* receive data */
	struct sk_buff		*rx_skb;
	int			maxlen;
	/* send data */
	struct sk_buff_head	squeue;
	struct sk_buff_head	rqueue;
	struct sk_buff		*tx_skb;
	int			tx_idx;
	int			debug;
	/* statistics */
	int			err_crc;
	int			err_tx;
	int			err_rx;
};

typedef int	(dchannel_l1callback)(struct dchannel *, u_int);
extern int	create_l1(struct dchannel *, dchannel_l1callback *);

/* private L1 commands */
#define INFO0		0x8002
#define INFO1		0x8102
#define INFO2		0x8202
#define INFO3_P8	0x8302
#define INFO3_P10	0x8402
#define INFO4_P8	0x8502
#define INFO4_P10	0x8602
#define LOSTFRAMING	0x8702
#define ANYSIGNAL	0x8802
#define HW_POWERDOWN	0x8902
#define HW_RESET_REQ	0x8a02
#define HW_POWERUP_REQ	0x8b02
#define HW_DEACT_REQ	0x8c02
#define HW_ACTIVATE_REQ	0x8e02
#define HW_D_NOBLOCKED  0x8f02
#define HW_RESET_IND	0x9002
#define HW_POWERUP_IND	0x9102
#define HW_DEACT_IND	0x9202
#define HW_ACTIVATE_IND	0x9302
#define HW_DEACT_CNF	0x9402
#define HW_TESTLOOP	0x9502
#define HW_TESTRX_RAW	0x9602
#define HW_TESTRX_HDLC	0x9702
#define HW_TESTRX_OFF	0x9802

struct layer1;
extern int	l1_event(struct layer1 *, u_int);


struct bchannel {
	struct mISDNchannel	ch;
	int			nr;
	u_long			Flags;
	struct work_struct	workq;
	u_int			state;
	void			*hw;
	int			slot;	/* multiport card channel slot */
	struct timer_list	timer;
	/* receive data */
	struct sk_buff		*rx_skb;
	int			maxlen;
	/* send data */
	struct sk_buff		*next_skb;
	struct sk_buff		*tx_skb;
	struct sk_buff_head	rqueue;
	int			rcount;
	int			tx_idx;
	int			debug;
	/* statistics */
	int			err_crc;
	int			err_tx;
	int			err_rx;
};

extern int	mISDN_initdchannel(struct dchannel *, int, void *);
extern int	mISDN_initbchannel(struct bchannel *, int);
extern int	mISDN_freedchannel(struct dchannel *);
extern void	mISDN_clear_bchannel(struct bchannel *);
extern int	mISDN_freebchannel(struct bchannel *);
extern void	queue_ch_frame(struct mISDNchannel *, u_int,
			int, struct sk_buff *);
extern int	dchannel_senddata(struct dchannel *, struct sk_buff *);
extern int	bchannel_senddata(struct bchannel *, struct sk_buff *);
extern void	recv_Dchannel(struct dchannel *);
extern void	recv_Echannel(struct dchannel *, struct dchannel *);
extern void	recv_Bchannel(struct bchannel *, unsigned int id);
extern void	recv_Dchannel_skb(struct dchannel *, struct sk_buff *);
extern void	recv_Bchannel_skb(struct bchannel *, struct sk_buff *);
extern void	confirm_Bsend(struct bchannel *bch);
extern int	get_next_bframe(struct bchannel *);
extern int	get_next_dframe(struct dchannel *);

#endif
