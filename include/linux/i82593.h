/*
 * Definitions for Intel 82593 CSMA/CD Core LAN Controller
 * The definitions are taken from the 1992 users manual with Intel
 * order number 297125-001.
 *
 * /usr/src/pc/RCS/i82593.h,v 1.1 1996/07/17 15:23:12 root Exp
 *
 * Copyright 1994, Anders Klemets <klemets@it.kth.se>
 *
 * HISTORY
 * i82593.h,v
 * Revision 1.4  2005/11/4  09:15:00  baroniunas
 * Modified copyright with permission of author as follows:
 *
 *   "If I82539.H is the only file with my copyright statement
 *    that is included in the Source Forge project, then you have
 *    my approval to change the copyright statement to be a GPL
 *    license, in the way you proposed on October 10."
 *
 * Revision 1.1  1996/07/17 15:23:12  root
 * Initial revision
 *
 * Revision 1.3  1995/04/05  15:13:58  adj
 * Initial alpha release
 *
 * Revision 1.2  1994/06/16  23:57:31  klemets
 * Mirrored all the fields in the configuration block.
 *
 * Revision 1.1  1994/06/02  20:25:34  klemets
 * Initial revision
 *
 *
 */
#ifndef	_I82593_H
#define	_I82593_H

/* Intel 82593 CSMA/CD Core LAN Controller */

/* Port 0 Command Register definitions */

/* Execution operations */
#define OP0_NOP			0	/* CHNL = 0 */
#define OP0_SWIT_TO_PORT_1	0	/* CHNL = 1 */
#define OP0_IA_SETUP		1
#define OP0_CONFIGURE		2
#define OP0_MC_SETUP		3
#define OP0_TRANSMIT		4
#define OP0_TDR			5
#define OP0_DUMP		6
#define OP0_DIAGNOSE		7
#define OP0_TRANSMIT_NO_CRC	9
#define OP0_RETRANSMIT		12
#define OP0_ABORT		13
/* Reception operations */
#define OP0_RCV_ENABLE		8
#define OP0_RCV_DISABLE		10
#define OP0_STOP_RCV		11
/* Status pointer control operations */
#define OP0_FIX_PTR		15	/* CHNL = 1 */
#define OP0_RLS_PTR		15	/* CHNL = 0 */
#define OP0_RESET		14

#define CR0_CHNL		(1 << 4)	/* 0=Channel 0, 1=Channel 1 */
#define CR0_STATUS_0		0x00
#define CR0_STATUS_1		0x20
#define CR0_STATUS_2		0x40
#define CR0_STATUS_3		0x60
#define CR0_INT_ACK		(1 << 7)	/* 0=No ack, 1=acknowledge */

/* Port 0 Status Register definitions */

#define SR0_NO_RESULT		0		/* dummy */
#define SR0_EVENT_MASK		0x0f
#define SR0_IA_SETUP_DONE	1
#define SR0_CONFIGURE_DONE	2
#define SR0_MC_SETUP_DONE	3
#define SR0_TRANSMIT_DONE	4
#define SR0_TDR_DONE		5
#define SR0_DUMP_DONE		6
#define SR0_DIAGNOSE_PASSED	7
#define SR0_TRANSMIT_NO_CRC_DONE 9
#define SR0_RETRANSMIT_DONE	12
#define SR0_EXECUTION_ABORTED	13
#define SR0_END_OF_FRAME	8
#define SR0_RECEPTION_ABORTED	10
#define SR0_DIAGNOSE_FAILED	15
#define SR0_STOP_REG_HIT	11

#define SR0_CHNL		(1 << 4)
#define SR0_EXECUTION		(1 << 5)
#define SR0_RECEPTION		(1 << 6)
#define SR0_INTERRUPT		(1 << 7)
#define SR0_BOTH_RX_TX		(SR0_EXECUTION | SR0_RECEPTION)

#define SR3_EXEC_STATE_MASK	0x03
#define SR3_EXEC_IDLE		0
#define SR3_TX_ABORT_IN_PROGRESS 1
#define SR3_EXEC_ACTIVE		2
#define SR3_ABORT_IN_PROGRESS	3
#define SR3_EXEC_CHNL		(1 << 2)
#define SR3_STP_ON_NO_RSRC	(1 << 3)
#define SR3_RCVING_NO_RSRC	(1 << 4)
#define SR3_RCV_STATE_MASK	0x60
#define SR3_RCV_IDLE		0x00
#define SR3_RCV_READY		0x20
#define SR3_RCV_ACTIVE		0x40
#define SR3_RCV_STOP_IN_PROG	0x60
#define SR3_RCV_CHNL		(1 << 7)

/* Port 1 Command Register definitions */

#define OP1_NOP			0
#define OP1_SWIT_TO_PORT_0	1
#define OP1_INT_DISABLE		2
#define OP1_INT_ENABLE		3
#define OP1_SET_TS		5
#define OP1_RST_TS		7
#define OP1_POWER_DOWN		8
#define OP1_RESET_RING_MNGMT	11
#define OP1_RESET		14
#define OP1_SEL_RST		15

#define CR1_STATUS_4		0x00
#define CR1_STATUS_5		0x20
#define CR1_STATUS_6		0x40
#define CR1_STOP_REG_UPDATE	(1 << 7)

/* Receive frame status bits */

#define	RX_RCLD			(1 << 0)
#define RX_IA_MATCH		(1 << 1)
#define	RX_NO_AD_MATCH		(1 << 2)
#define RX_NO_SFD		(1 << 3)
#define RX_SRT_FRM		(1 << 7)
#define RX_OVRRUN		(1 << 8)
#define RX_ALG_ERR		(1 << 10)
#define RX_CRC_ERR		(1 << 11)
#define RX_LEN_ERR		(1 << 12)
#define RX_RCV_OK		(1 << 13)
#define RX_TYP_LEN		(1 << 15)

/* Transmit status bits */

#define TX_NCOL_MASK		0x0f
#define TX_FRTL			(1 << 4)
#define TX_MAX_COL		(1 << 5)
#define TX_HRT_BEAT		(1 << 6)
#define TX_DEFER		(1 << 7)
#define TX_UND_RUN		(1 << 8)
#define TX_LOST_CTS		(1 << 9)
#define TX_LOST_CRS		(1 << 10)
#define TX_LTCOL		(1 << 11)
#define TX_OK			(1 << 13)
#define TX_COLL			(1 << 15)

struct i82593_conf_block {
  u_char fifo_limit : 4,
  	 forgnesi   : 1,
  	 fifo_32    : 1,
  	 d6mod      : 1,
  	 throttle_enb : 1;
  u_char throttle   : 6,
	 cntrxint   : 1,
	 contin	    : 1;
  u_char addr_len   : 3,
  	 acloc 	    : 1,
 	 preamb_len : 2,
  	 loopback   : 2;
  u_char lin_prio   : 3,
	 tbofstop   : 1,
	 exp_prio   : 3,
	 bof_met    : 1;
  u_char	    : 4,
	 ifrm_spc   : 4;
  u_char	    : 5,
	 slottim_low : 3;
  u_char slottim_hi : 3,
		    : 1,
	 max_retr   : 4;
  u_char prmisc     : 1,
	 bc_dis     : 1,
  		    : 1,
	 crs_1	    : 1,
	 nocrc_ins  : 1,
	 crc_1632   : 1,
  	 	    : 1,
  	 crs_cdt    : 1;
  u_char cs_filter  : 3,
	 crs_src    : 1,
	 cd_filter  : 3,
		    : 1;
  u_char	    : 2,
  	 min_fr_len : 6;
  u_char lng_typ    : 1,
	 lng_fld    : 1,
	 rxcrc_xf   : 1,
	 artx	    : 1,
	 sarec	    : 1,
	 tx_jabber  : 1,	/* why is this called max_len in the manual? */
	 hash_1	    : 1,
  	 lbpkpol    : 1;
  u_char	    : 6,
  	 fdx	    : 1,
  	  	    : 1;
  u_char dummy_6    : 6,	/* supposed to be ones */
  	 mult_ia    : 1,
  	 dis_bof    : 1;
  u_char dummy_1    : 1,	/* supposed to be one */
	 tx_ifs_retrig : 2,
	 mc_all     : 1,
	 rcv_mon    : 2,
	 frag_acpt  : 1,
  	 tstrttrs   : 1;
  u_char fretx	    : 1,
	 runt_eop   : 1,
	 hw_sw_pin  : 1,
	 big_endn   : 1,
	 syncrqs    : 1,
	 sttlen     : 1,
	 tx_eop     : 1,
  	 rx_eop	    : 1;
  u_char rbuf_size  : 5,
	 rcvstop    : 1,
  	 	    : 2;
};

#define I82593_MAX_MULTICAST_ADDRESSES	128	/* Hardware hashed filter */

#endif /* _I82593_H */
