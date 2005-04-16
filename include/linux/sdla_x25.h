/*****************************************************************************
* sdla_x25.h	Sangoma X.25 firmware API definitions.
*
* Author:	Nenad Corbic	<ncorbic@sangoma.com>
*
* Copyright:	(c) 1995-2000 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
		2 of the License, or (at your option) any later version.
* ============================================================================
* Feb 28, 2000  Nenad Corbic    Updated for socket based x25api
* Dec 13, 1996	Gene Kozin	Initial version
*****************************************************************************/
#ifndef	_SDLA_X25_H
#define	_SDLA_X25_H

/*----------------------------------------------------------------------------
 * Notes:
 * ------
 * 1. All structures defined in this file are byte-alined.  
 *	Compiler	Platform	
 *	--------	--------
 *	GNU C		Linux
 *
 */

#ifndef	PACKED
#	define	PACKED	__attribute__((packed))
#endif	/* PACKED */

/******	CONSTANTS DEFINITIONS ***********************************************/

#define	X25_MAX_CHAN	255	/* max number of open X.25 circuits */
#define	X25_MAX_DATA	1024	/* max length of X.25 data buffer */
/*
 * X.25 shared memory layout.
 */
#define	X25_MBOX_OFFS	0x16B0	/* general mailbox block */
#define	X25_RXMBOX_OFFS	0x1AD0	/* receive mailbox */
#define	X25_STATUS_OFFS	0x1EF0	/* X.25 status structure */
#define X25_MB_VECTOR	0xE000	/* S514 mailbox window vecotr */
#define X25_MISC_HDLC_BITS 0x1F00 /*X.25 miscallaneous HDLC bits */

/* code levels */
#define HDLC_LEVEL 0x01
#define X25_LEVEL  0x02
#define X25_AND_HDLC_LEVEL 0x03
#define DO_HDLC_LEVEL_ERROR_CHECKING 0x04

/****** DATA STRUCTURES *****************************************************/

/*----------------------------------------------------------------------------
 * X.25 Command Block.
 */
typedef struct X25Cmd
{
	unsigned char command	PACKED;	/* command code */
	unsigned short length	PACKED;	/* transfer data length */
	unsigned char result	PACKED;	/* return code */
	unsigned char pf	PACKED;	/* P/F bit */
	unsigned short lcn	PACKED;	/* logical channel */
	unsigned char qdm	PACKED;	/* Q/D/M bits */
	unsigned char cause	PACKED;	/* cause field */
	unsigned char diagn	PACKED;	/* diagnostics */
	unsigned char pktType	PACKED;	/* packet type */
	unsigned char resrv[4]	PACKED;	/* reserved */
} TX25Cmd;

/*
 * Defines for the 'command' field.
 */
/*----- General commands --------------*/
#define X25_SET_GLOBAL_VARS	0x0B   	/* set global variables */ 
#define X25_READ_MODEM_STATUS	0x0C 	/* read modem status */
#define X25_READ_CODE_VERSION	0x15	/* read firmware version number */
#define X25_TRACE_CONFIGURE	0x14	/* configure trace facility */
#define X25_READ_TRACE_DATA	0x16	/* read trace data */
#define	X25_SET_INTERRUPT_MODE	0x17	/* set interrupt generation mode */
#define	X25_READ_INTERRUPT_MODE	0x18	/* read interrupt generation mode */
/*----- HDLC-level commands -----------*/
#define X25_HDLC_LINK_CONFIGURE	0x01	/* configure HDLC link level */   
#define X25_HDLC_LINK_OPEN	0x02	/* open HDLC link */         	
#define X25_HDLC_LINK_CLOSE	0x03	/* close HDLC link */
#define X25_HDLC_LINK_SETUP	0x04	/* set up HDLC link */ 
#define X25_HDLC_LINK_DISC	0x05	/* disconnect DHLC link */
#define X25_HDLC_LINK_STATUS	0x06	/* read DHLC link status */
#define X25_HDLC_READ_STATS	0x07	/* read operational statistics */
#define X25_HDLC_FLUSH_STATS	0x08 	/* flush operational statistics */
#define X25_HDLC_READ_COMM_ERR	0x09 	/* read error statistics */
#define X25_HDLC_FLUSH_COMM_ERR	0x0A	/* flush error statistics */
#define X25_HDLC_FLUSH_BUFFERS	0x0D	/* flush HDLC-level data buffers */
#define X25_HDLC_SPRVS_CNT_STAT 0x0F	/* read surervisory count status */
#define X25_HDLC_SEND_UI_FRAME	0x10	/* send unnumbered information frame */
#define X25_HDLC_WRITE		0x11	/* send HDLC information frame */
#define X25_HDLC_READ		0x21	/* read HDLC information frame */
#define X25_HDLC_READ_CONFIG	0x12	/* read HDLC configuration */
#define X25_HDLC_SET_CONFIG	0x13	/* set HDLC configuration */
#define SET_PROTOCOL_LEVEL	0x1F	/* set protocol level */
/*----- X.25-level commands -----------*/
#define X25_READ		0x22	/* read X.25 packet */
#define X25_WRITE		0x23	/* send X.25 packet */
#define X25_PLACE_CALL		0x30	/* place a call on SVC */
#define X25_ACCEPT_CALL		0x31	/* accept incomming call */
#define X25_CLEAR_CALL		0x32	/* clear call */
#define X25_CLEAR_CONFRM	0x33	/* send clear confirmation packet */
#define X25_RESET		0x34	/* send reset request packet */
#define X25_RESET_CONFRM	0x35	/* send reset confirmation packet */
#define X25_RESTART		0x36	/* send restart request packet */
#define X25_RESTART_CONFRM	0x37	/* send restart confirmation packet */
#define X25_INTERRUPT		0x38	/* send interrupt request packet */
#define X25_INTERRUPT_CONFRM	0x39	/* send interrupt confirmation pkt */
#define X25_REGISTRATION_RQST	0x3A	/* send registration request packet */
#define X25_REGISTRATION_CONFRM	0x3B	/* send registration confirmation */
#define X25_IS_DATA_AVAILABLE	0x40	/* querry receive queue */
#define X25_INCOMMING_CALL_CTL	0x41	/* select incomming call options */
#define X25_CONFIGURE_PVC	0x42	/* configure PVC */
#define X25_GET_ACTIVE_CHANNELS	0x43	/* get a list of active circuits */
#define X25_READ_CHANNEL_CONFIG	0x44	/* read virt. circuit configuration */
#define X25_FLUSH_DATA_BUFFERS	0x45	/* flush X.25-level data buffers */
#define X25_READ_HISTORY_TABLE	0x46	/* read asynchronous event log */
#define X25_HISTORY_TABLE_CTL	0x47	/* control asynchronous event log */
#define	X25_GET_TX_D_BIT_STATUS	0x48	/* is packet with D-bit acknowleged */
#define	X25_READ_STATISTICS	0x49	/* read X.25-level statistics */
#define	X25_FLUSH_STATISTICS	0x4A	/* flush X.25-level statistics */
#define	X25_READ_CONFIGURATION	0x50	/* read HDLC & X.25 configuration */
#define	X25_SET_CONFIGURATION	0x51	/* set HDLC & X.25 configuration */

/*
 * Defines for the 'result' field.
 */
/*----- General results ---------------*/
#define X25RES_OK		0x00
#define X25RES_ERROR		0x01
#define X25RES_LINK_NOT_IN_ABM	0x02	/* link is not in ABM mode */
#define X25RES_LINK_CLOSED	0x03
#define X25RES_INVAL_LENGTH	0x04
#define X25RES_INVAL_CMD	0x05
#define X25RES_UNNUMBERED_FRAME	0x06	/* unnunbered frame received */
#define X25RES_FRM_REJECT_MODE	0x07	/* link is in Frame Reject mode */
#define X25RES_MODEM_FAILURE	0x08	/* DCD and/or CTS dropped */
#define X25RES_N2_RETRY_LIMIT	0x09	/* N2 retry limit has been exceeded */
#define X25RES_INVAL_LCN	0x30	/* invalid logical channel number */
#define X25RES_INVAL_STATE	0x31	/* channel is not in data xfer mode */
#define X25RES_INVAL_DATA_LEN	0x32	/* invalid data length */
#define X25RES_NOT_READY	0x33	/* no data available / buffers full */
#define X25RES_NETWORK_DOWN	0x34
#define X25RES_CHANNEL_IN_USE	0x35	/* there is data queued on this LCN */
#define X25RES_REGST_NOT_SUPPRT	0x36	/* registration not supported */
#define X25RES_INVAL_FORMAT	0x37	/* invalid packet format */
#define X25RES_D_BIT_NOT_SUPPRT	0x38	/* D-bit pragmatics not supported */
#define X25RES_FACIL_NOT_SUPPRT	0x39	/* Call facility not supported */
#define X25RES_INVAL_CALL_ARG	0x3A	/* errorneous call arguments */
#define X25RES_INVAL_CALL_DATA	0x3B	/* errorneous call user data */
#define X25RES_ASYNC_PACKET	0x40	/* asynchronous packet received */
#define X25RES_PROTO_VIOLATION	0x41	/* protocol violation occurred */
#define X25RES_PKT_TIMEOUT	0x42	/* X.25 packet time out */
#define X25RES_PKT_RETRY_LIMIT	0x43	/* X.25 packet retry limit exceeded */
/*----- Command-dependent results -----*/
#define X25RES_LINK_DISC	0x00	/* HDLC_LINK_STATUS */
#define X25RES_LINK_IN_ABM	0x01	/* HDLC_LINK_STATUS */
#define X25RES_NO_DATA		0x01	/* HDLC_READ/READ_TRACE_DATA*/
#define X25RES_TRACE_INACTIVE	0x02	/* READ_TRACE_DATA */
#define X25RES_LINK_IS_OPEN	0x01	/* HDLC_LINK_OPEN */
#define X25RES_LINK_IS_DISC	0x02	/* HDLC_LINK_DISC */
#define X25RES_LINK_IS_CLOSED	0x03	/* HDLC_LINK_CLOSE */
#define X25RES_INVAL_PARAM	0x31	/* INCOMMING_CALL_CTL */
#define X25RES_INVAL_CONFIG	0x35	/* REGISTR_RQST/CONFRM */

/*
 * Defines for the 'qdm_bits' field.
 */
#define X25CMD_Q_BIT_MASK	0x04
#define X25CMD_D_BIT_MASK	0x02
#define X25CMD_M_BIT_MASK	0x01

/*
 * Defines for the 'pkt_type' field.
 */
/*----- Asynchronous events ------*/
#define ASE_CLEAR_RQST		0x02
#define ASE_RESET_RQST		0x04
#define ASE_RESTART_RQST	0x08
#define ASE_INTERRUPT		0x10
#define ASE_DTE_REGISTR_RQST	0x20
#define ASE_CALL_RQST		0x30
#define ASE_CALL_ACCEPTED	0x31
#define ASE_CLEAR_CONFRM	0x32
#define ASE_RESET_CONFRM	0x33
#define ASE_RESTART_CONFRM	0x34
#define ASE_INTERRUPT_CONFRM	0x35
#define ASE_DCE_REGISTR_CONFRM	0x36
#define ASE_DIAGNOSTIC		0x37
#define ASE_CALL_AUTO_CLEAR	0x38
#define AUTO_RESPONSE_FLAG	0x80
/*----- Time-Out events ----------*/
#define TOE_RESTART_RQST	0x03
#define TOE_CALL_RQST		0x05
#define TOE_CLEAR_RQST		0x08
#define TOE_RESET_RQST		0x0A
/*----- Protocol Violation events */
#define PVE_CLEAR_RQST		0x32
#define PVE_RESET_RQST		0x33
#define PVE_RESTART_RQST	0x34
#define PVE_DIAGNOSTIC		0x37

#define INTR_ON_RX_FRAME            0x01
#define INTR_ON_TX_FRAME            0x02
#define INTR_ON_MODEM_STATUS_CHANGE 0x04
#define INTR_ON_COMMAND_COMPLETE    0x08
#define INTR_ON_X25_ASY_TRANSACTION 0x10
#define INTR_ON_TIMER		    0x40
#define DIRECT_RX_INTR_USAGE        0x80

#define NO_INTR_PENDING  	        0x00
#define RX_INTR_PENDING			0x01	
#define TX_INTR_PENDING			0x02
#define MODEM_INTR_PENDING		0x04
#define COMMAND_COMPLETE_INTR_PENDING 	0x08
#define X25_ASY_TRANS_INTR_PENDING	0x10
#define TIMER_INTR_PENDING		0x40

/*----------------------------------------------------------------------------
 * X.25 Mailbox.
 *	This structure is located at offsets X25_MBOX_OFFS and X25_RXMBOX_OFFS
 *	into shared memory window.
 */
typedef struct X25Mbox
{
	unsigned char opflag	PACKED;	/* 00h: execution flag */
	TX25Cmd cmd		PACKED;	/* 01h: command block */
	unsigned char data[1]	PACKED;	/* 10h: data buffer */
} TX25Mbox;

/*----------------------------------------------------------------------------
 * X.25 Time Stamp Structure.
 */
typedef struct X25TimeStamp
{
	unsigned char month	PACKED;
	unsigned char date	PACKED;
	unsigned char sec	PACKED;
	unsigned char min	PACKED;
	unsigned char hour	PACKED;
} TX25TimeStamp;

/*----------------------------------------------------------------------------
 * X.25 Status Block.
 *	This structure is located at offset X25_STATUS_OFF into shared memory
 *	window.
 */
typedef struct X25Status
{
	unsigned short pvc_map	PACKED;	/* 00h: PVC map */
	unsigned short icc_map	PACKED;	/* 02h: Incomming Chan. map */
	unsigned short twc_map	PACKED;	/* 04h: Two-way Cnan. map */
	unsigned short ogc_map	PACKED;	/* 06h: Outgoing Chan. map */
	TX25TimeStamp tstamp	PACKED;	/* 08h: timestamp (BCD) */
	unsigned char iflags	PACKED;	/* 0Dh: interrupt flags */
	unsigned char imask     PACKED; /* 0Eh: interrupt mask  */
	unsigned char resrv	PACKED;	/* 0Eh: */
	unsigned char gflags	PACKED;	/* 10h: misc. HDLC/X25 flags */
	unsigned char cflags[X25_MAX_CHAN] PACKED; /* channel status bytes */
} TX25Status;

/*
 * Bitmasks for the 'iflags' field.
 */
#define X25_RX_INTR	0x01	/* receive interrupt */
#define X25_TX_INTR	0x02	/* transmit interrupt */
#define X25_MODEM_INTR	0x04	/* modem status interrupt (CTS/DCD) */
#define X25_EVENT_INTR	0x10	/* asyncronous event encountered */
#define X25_CMD_INTR	0x08	/* interface command complete */

/*
 * Bitmasks for the 'gflags' field.
 */
#define X25_HDLC_ABM	0x01	/* HDLC is in ABM mode */
#define X25_RX_READY	0x02	/* X.25 data available */
#define X25_TRACE_READY	0x08	/* trace data available */
#define X25_EVENT_IND	0x20	/* asynchronous event indicator */
#define X25_TX_READY	0x40	/* space is available in Tx buf.*/

/*
 * Bitmasks for the 'cflags' field.
 */
#define X25_XFER_MODE	0x80	/* channel is in data transfer mode */
#define X25_TXWIN_OPEN	0x40	/* transmit window open */
#define X25_RXBUF_MASK	0x3F	/* number of data buffers available */

/*****************************************************************************
 * Following definitions structurize contents of the TX25Mbox.data field for
 * different X.25 interface commands.
 ****************************************************************************/

/* ---------------------------------------------------------------------------
 * X25_SET_GLOBAL_VARS Command.
 */
typedef struct X25GlobalVars
{
	unsigned char resrv	PACKED;	/* 00h: reserved */
	unsigned char dtrCtl	PACKED;	/* 01h: DTR control code */
	unsigned char resErr	PACKED;	/* 01h: '1' - reset modem error */
} TX25GlobalVars;

/*
 * Defines for the 'dtrCtl' field.
 */
#define X25_RAISE_DTR	0x01
#define X25_DROP_DTR	0x02

/* ---------------------------------------------------------------------------
 * X25_READ_MODEM_STATUS Command.
 */
typedef struct X25ModemStatus
{
	unsigned char	status	PACKED;		/* 00h: modem status */
} TX25ModemStatus;

/*
 * Defines for the 'status' field.
 */
#define X25_CTS_MASK	0x20
#define X25_DCD_MASK	0x08

/* ---------------------------------------------------------------------------
 * X25_HDLC_LINK_STATUS Command.
 */
typedef struct X25LinkStatus
{
	unsigned char txQueued	PACKED;	/* 00h: queued Tx I-frames*/
	unsigned char rxQueued	PACKED;	/* 01h: queued Rx I-frames*/
	unsigned char station	PACKED;	/* 02h: DTE/DCE config. */
	unsigned char reserved	PACKED;	/* 03h: reserved */
	unsigned char sfTally	PACKED;	/* 04h: supervisory frame tally */
} TX25LinkStatus;

/*
 * Defines for the 'station' field.
 */
#define	X25_STATION_DTE	0x01	/* station configured as DTE */
#define X25_STATION_DCE	0x02	/* station configured as DCE */

/* ---------------------------------------------------------------------------
 * X25_HDLC_READ_STATS Command.
 */
typedef struct HdlcStats
{						/*	a number of ... */
	unsigned short rxIFrames	PACKED;	/* 00h: ready Rx I-frames */
	unsigned short rxNoseq		PACKED;	/* 02h: frms out-of-sequence */
	unsigned short rxNodata		PACKED;	/* 04h: I-frms without data */
	unsigned short rxDiscarded	PACKED;	/* 06h: discarded frames */
	unsigned short rxTooLong	PACKED;	/* 08h: frames too long */
	unsigned short rxBadAddr	PACKED;	/* 0Ah: frms with inval.addr*/
	unsigned short txAcked		PACKED;	/* 0Ch: acknowledged I-frms */
	unsigned short txRetransm	PACKED;	/* 0Eh: re-transmit. I-frms */
	unsigned short t1Timeout	PACKED;	/* 10h: T1 timeouts */
	unsigned short rxSABM		PACKED;	/* 12h: received SABM frames */
	unsigned short rxDISC		PACKED;	/* 14h: received DISC frames */
	unsigned short rxDM		PACKED;	/* 16h: received DM frames */
	unsigned short rxFRMR		PACKED;	/* 18h: FRMR frames received */
	unsigned short txSABM		PACKED;	/* 1Ah: transm. SABM frames*/
	unsigned short txDISC		PACKED;	/* 1Ch: transm. DISC frames*/
	unsigned short txDM		PACKED;	/* 1Eh: transm. DM frames */
	unsigned short txFRMR		PACKED;	/* 20h: transm. FRMR frames*/
} THdlcStats;

/* ---------------------------------------------------------------------------
 * X25_HDLC_READ_COMM_ERR Command.
 */
typedef struct HdlcCommErr
{						/*	a number of ... */
	unsigned char rxOverrun		PACKED;	/* 00h: Rx overrun errors */
	unsigned char rxBadCrc		PACKED;	/* 01h: Rx CRC errors */
	unsigned char rxAborted		PACKED;	/* 02h: Rx aborted frames */
	unsigned char rxDropped		PACKED;	/* 03h: frames lost */
	unsigned char txAborted		PACKED;	/* 04h: Tx aborted frames */
	unsigned char txUnderrun	PACKED;	/* 05h: Tx underrun errors */
	unsigned char txMissIntr	PACKED;	/* 06h: missed underrun ints */
	unsigned char reserved		PACKED;	/* 07h: reserved */
	unsigned char droppedDCD	PACKED;	/* 08h: times DCD dropped */
	unsigned char droppedCTS	PACKED;	/* 09h: times CTS dropped */
} THdlcCommErr;

/* ---------------------------------------------------------------------------
 * X25_SET_CONFIGURATION & X25_READ_CONFIGURATION Commands.
 */
typedef struct X25Config
{
unsigned char baudRate		PACKED;	/* 00h:  */
	unsigned char t1		PACKED;	/* 01h:  */
	unsigned char t2		PACKED;	/* 02h:  */
	unsigned char n2		PACKED;	/* 03h:  */
	unsigned short hdlcMTU		PACKED;	/* 04h:  */
	unsigned char hdlcWindow	PACKED;	/* 06h:  */
	unsigned char t4		PACKED;	/* 07h:  */
	unsigned char autoModem		PACKED;	/* 08h:  */
	unsigned char autoHdlc		PACKED;	/* 09h:  */
	unsigned char hdlcOptions	PACKED;	/* 0Ah:  */
	unsigned char station		PACKED;	/* 0Bh:  */
	unsigned char pktWindow		PACKED;	/* 0Ch:  */
	unsigned short defPktSize	PACKED;	/* 0Dh:  */
	unsigned short pktMTU		PACKED;	/* 0Fh:  */
	unsigned short loPVC		PACKED;	/* 11h:  */
	unsigned short hiPVC		PACKED;	/* 13h:  */
	unsigned short loIncommingSVC	PACKED;	/* 15h:  */
	unsigned short hiIncommingSVC	PACKED;	/* 17h:  */
	unsigned short loTwoWaySVC	PACKED;	/* 19h:  */
	unsigned short hiTwoWaySVC	PACKED;	/* 1Bh:  */
	unsigned short loOutgoingSVC	PACKED;	/* 1Dh:  */
	unsigned short hiOutgoingSVC	PACKED;	/* 1Fh:  */
	unsigned short options		PACKED;	/* 21h:  */
	unsigned char responseOpt	PACKED;	/* 23h:  */
	unsigned short facil1		PACKED;	/* 24h:  */
	unsigned short facil2		PACKED;	/* 26h:  */
	unsigned short ccittFacil	PACKED;	/* 28h:  */
	unsigned short otherFacil	PACKED;	/* 2Ah:  */
	unsigned short ccittCompat	PACKED;	/* 2Ch:  */
	unsigned char t10t20		PACKED;	/* 2Eh:  */
	unsigned char t11t21		PACKED;	/* 2Fh:  */
	unsigned char t12t22		PACKED;	/* 30h:  */
	unsigned char t13t23		PACKED;	/* 31h:  */
	unsigned char t16t26		PACKED;	/* 32H:  */
	unsigned char t28		PACKED;	/* 33h:  */
	unsigned char r10r20		PACKED;	/* 34h:  */
	unsigned char r12r22		PACKED;	/* 35h:  */
	unsigned char r13r23		PACKED;	/* 36h:  */
} TX25Config;

/* ---------------------------------------------------------------------------
 * X25_READ_CHANNEL_CONFIG Command.
 */
typedef struct X25ChanAlloc			/*----- Channel allocation -*/
{
	unsigned short loPVC		PACKED;	/* 00h: lowest PVC number */
	unsigned short hiPVC		PACKED;	/* 02h: highest PVC number */
	unsigned short loIncommingSVC	PACKED;	/* 04h: lowest incoming SVC */
	unsigned short hiIncommingSVC	PACKED;	/* 06h: highest incoming SVC */
	unsigned short loTwoWaySVC	PACKED;	/* 08h: lowest two-way SVC */
	unsigned short hiTwoWaySVC	PACKED;	/* 0Ah: highest two-way SVC */
	unsigned short loOutgoingSVC	PACKED;	/* 0Ch: lowest outgoing SVC */
	unsigned short hiOutgoingSVC	PACKED;	/* 0Eh: highest outgoing SVC */
} TX25ChanAlloc;

typedef struct X25ChanCfg		/*------ Channel configuration -----*/
{
	unsigned char type	PACKED;	/* 00h: channel type */
	unsigned char txConf	PACKED;	/* 01h: Tx packet and window sizes */
	unsigned char rxConf	PACKED;	/* 01h: Rx packet and window sizes */
} TX25ChanCfg;

/*
 * Defines for the 'type' field.
 */
#define	X25_PVC  	0x01	/* PVC */
#define	X25_SVC_IN	0x03	/* Incoming SVC */
#define	X25_SVC_TWOWAY	0x07	/* Two-way SVC */
#define	X25_SVC_OUT	0x0B	/* Outgoing SVC */

/*----------------------------------------------------------------------------
 * X25_READ_STATISTICS Command.
 */
typedef struct X25Stats
{						/* number of packets Tx/Rx'ed */
	unsigned short txRestartRqst	PACKED;	/* 00h: Restart Request */
	unsigned short rxRestartRqst	PACKED;	/* 02h: Restart Request */
	unsigned short txRestartConf	PACKED;	/* 04h: Restart Confirmation */
	unsigned short rxRestartConf	PACKED;	/* 06h: Restart Confirmation */
	unsigned short txResetRqst	PACKED;	/* 08h: Reset Request */
	unsigned short rxResetRqst	PACKED;	/* 0Ah: Reset Request */
	unsigned short txResetConf	PACKED;	/* 0Ch: Reset Confirmation */
	unsigned short rxResetConf	PACKED;	/* 0Eh: Reset Confirmation */
	unsigned short txCallRequest	PACKED;	/* 10h: Call Request */
	unsigned short rxCallRequest	PACKED;	/* 12h: Call Request */
	unsigned short txCallAccept	PACKED;	/* 14h: Call Accept */
	unsigned short rxCallAccept	PACKED;	/* 16h: Call Accept */
	unsigned short txClearRqst	PACKED;	/* 18h: Clear Request */
	unsigned short rxClearRqst	PACKED;	/* 1Ah: Clear Request */
	unsigned short txClearConf	PACKED;	/* 1Ch: Clear Confirmation */
	unsigned short rxClearConf	PACKED;	/* 1Eh: Clear Confirmation */
	unsigned short txDiagnostic	PACKED;	/* 20h: Diagnostic */
	unsigned short rxDiagnostic	PACKED;	/* 22h: Diagnostic */
	unsigned short txRegRqst	PACKED;	/* 24h: Registration Request */
	unsigned short rxRegRqst	PACKED;	/* 26h: Registration Request */
	unsigned short txRegConf	PACKED;	/* 28h: Registration Confirm.*/
	unsigned short rxRegConf	PACKED;	/* 2Ah: Registration Confirm.*/
	unsigned short txInterrupt	PACKED;	/* 2Ch: Interrupt */
	unsigned short rxInterrupt	PACKED;	/* 2Eh: Interrupt */
	unsigned short txIntrConf	PACKED;	/* 30h: Interrupt Confirm. */
	unsigned short rxIntrConf	PACKED;	/* 32h: Interrupt Confirm. */
	unsigned short txData		PACKED;	/* 34h: Data */
	unsigned short rxData		PACKED;	/* 36h: Data */
	unsigned short txRR		PACKED;	/* 38h: RR */
	unsigned short rxRR		PACKED;	/* 3Ah: RR */
	unsigned short txRNR		PACKED;	/* 3Ch: RNR */
	unsigned short rxRNR		PACKED;	/* 3Eh: RNR */
} TX25Stats;

/*----------------------------------------------------------------------------
 * X25_READ_HISTORY_TABLE Command.
 */
typedef struct X25EventLog
{
	unsigned char	type	PACKED;	/* 00h: transaction type */
	unsigned short	lcn	PACKED;	/* 01h: logical channel num */
	unsigned char	packet	PACKED;	/* 03h: async packet type */
	unsigned char	cause	PACKED;	/* 04h: X.25 cause field */
	unsigned char	diag	PACKED;	/* 05h: X.25 diag field */
	TX25TimeStamp	ts	PACKED;	/* 06h: time stamp */
} TX25EventLog;

/*
 * Defines for the 'type' field.
 */
#define X25LOG_INCOMMING	0x00
#define X25LOG_APPLICATION 	0x01
#define X25LOG_AUTOMATIC	0x02
#define X25LOG_ERROR		0x04
#define X25LOG_TIMEOUT		0x08
#define X25LOG_RECOVERY		0x10

/*
 * Defines for the 'packet' field.
 */
#define X25LOG_CALL_RQST	0x0B
#define X25LOG_CALL_ACCEPTED	0x0F
#define X25LOG_CLEAR_RQST	0x13
#define X25LOG_CLEAR_CONFRM	0x17
#define X25LOG_RESET_RQST	0x1B
#define X25LOG_RESET_CONFRM	0x1F
#define X25LOG_RESTART_RQST	0xFB
#define X25LOG_RESTART_COMFRM	0xFF
#define X25LOG_DIAGNOSTIC	0xF1
#define X25LOG_DTE_REG_RQST	0xF3
#define X25LOG_DTE_REG_COMFRM	0xF7

/* ---------------------------------------------------------------------------
 * X25_TRACE_CONFIGURE Command.
 */
typedef struct X25TraceCfg
{
	unsigned char flags	PACKED;	/* 00h: trace configuration flags */
	unsigned char timeout	PACKED;	/* 01h: timeout for trace delay mode*/
} TX25TraceCfg;

/*
 * Defines for the 'flags' field.
 */
#define X25_TRC_ENABLE		0x01	/* bit0: '1' - trace enabled */
#define X25_TRC_TIMESTAMP	0x02	/* bit1: '1' - time stamping enabled*/
#define X25_TRC_DELAY		0x04	/* bit2: '1' - trace delay enabled */
#define X25_TRC_DATA		0x08	/* bit3: '1' - trace data packets */
#define X25_TRC_SUPERVISORY	0x10    /* bit4: '1' - trace suprvisory pkts*/
#define X25_TRC_ASYNCHRONOUS	0x20	/* bit5: '1' - trace asynch. packets*/
#define X25_TRC_HDLC		0x40	/* bit6: '1' - trace all packets */
#define X25_TRC_READ		0x80	/* bit7: '1' - get current config. */

/* ---------------------------------------------------------------------------
 * X25_READ_TRACE_DATA Command.
 */
typedef struct X25Trace			/*----- Trace data structure -------*/
{
	unsigned short length	PACKED;	/* 00h: trace data length */
	unsigned char type	PACKED;	/* 02h: trace type */
	unsigned char lost_cnt	PACKED;	/* 03h: N of traces lost */
	TX25TimeStamp tstamp	PACKED;	/* 04h: mon/date/sec/min/hour */
	unsigned short millisec	PACKED;	/* 09h: ms time stamp */
	unsigned char data[0]	PACKED;	/* 0Bh: traced frame */
} TX25Trace;

/*
 * Defines for the 'type' field.
 */
#define X25_TRC_TYPE_MASK	0x0F	/* bits 0..3: trace type */
#define X25_TRC_TYPE_RX_FRAME	0x00	/* received frame trace */
#define X25_TRC_TYPE_TX_FRAME	0x01	/* transmitted frame */
#define X25_TRC_TYPE_ERR_FRAME	0x02	/* error frame */

#define X25_TRC_ERROR_MASK	0xF0	/* bits 4..7: error code */
#define X25_TRCERR_RX_ABORT	0x10	/* receive abort error */
#define X25_TRCERR_RX_BADCRC	0x20	/* receive CRC error */
#define X25_TRCERR_RX_OVERRUN	0x30	/* receiver overrun error */
#define X25_TRCERR_RX_TOO_LONG	0x40	/* excessive frame length error */
#define X25_TRCERR_TX_ABORT	0x70	/* aborted frame transmittion error */
#define X25_TRCERR_TX_UNDERRUN	0x80	/* transmit underrun error */

/*****************************************************************************
 * Following definitions describe HDLC frame and X.25 packet formats.
 ****************************************************************************/

typedef struct HDLCFrame		/*----- DHLC Frame Format ----------*/
{
	unsigned char addr	PACKED;	/* address field */
	unsigned char cntl	PACKED;	/* control field */
	unsigned char data[0]	PACKED;
} THDLCFrame;

typedef struct X25Pkt			/*----- X.25 Paket Format ----------*/
{
	unsigned char lcn_hi	PACKED;	/* 4 MSB of Logical Channel Number */
	unsigned char lcn_lo	PACKED;	/* 8 LSB of Logical Channel Number */
	unsigned char type	PACKED;
	unsigned char data[0]	PACKED;
} TX25Pkt;

/*
 * Defines for the 'lcn_hi' field.
 */
#define	X25_Q_BIT_MASK		0x80	/* Data Qualifier Bit mask */
#define	X25_D_BIT_MASK		0x40	/* Delivery Confirmation Bit mask */
#define	X25_M_BITS_MASK		0x30	/* Modulo Bits mask */
#define	X25_LCN_MSB_MASK	0x0F	/* LCN most significant bits mask */

/*
 * Defines for the 'type' field.
 */
#define	X25PKT_DATA		0x01	/* Data packet mask */
#define	X25PKT_SUPERVISORY	0x02	/* Supervisory packet mask */
#define	X25PKT_CALL_RQST	0x0B	/* Call Request/Incoming */
#define	X25PKT_CALL_ACCEPTED	0x0F	/* Call Accepted/Connected */
#define	X25PKT_CLEAR_RQST	0x13	/* Clear Request/Indication */
#define	X25PKT_CLEAR_CONFRM	0x17	/* Clear Confirmation */
#define	X25PKT_RESET_RQST	0x1B	/* Reset Request/Indication */
#define	X25PKT_RESET_CONFRM	0x1F	/* Reset Confirmation */
#define	X25PKT_RESTART_RQST	0xFB	/* Restart Request/Indication */
#define	X25PKT_RESTART_CONFRM	0xFF	/* Restart Confirmation */
#define	X25PKT_INTERRUPT	0x23	/* Interrupt */
#define	X25PKT_INTERRUPT_CONFRM	0x27	/* Interrupt Confirmation */
#define	X25PKT_DIAGNOSTIC	0xF1	/* Diagnostic */
#define	X25PKT_REGISTR_RQST	0xF3	/* Registration Request */
#define	X25PKT_REGISTR_CONFRM	0xF7	/* Registration Confirmation */
#define	X25PKT_RR_MASKED	0x01	/* Receive Ready packet after masking */
#define	X25PKT_RNR_MASKED	0x05	/* Receive Not Ready after masking  */


typedef struct {
	TX25Cmd cmd		PACKED;
	char data[X25_MAX_DATA]	PACKED;
} mbox_cmd_t;


typedef struct {
	unsigned char  qdm	PACKED;	/* Q/D/M bits */
	unsigned char  cause	PACKED;	/* cause field */
	unsigned char  diagn	PACKED;	/* diagnostics */
	unsigned char  pktType  PACKED;
	unsigned short length   PACKED;
	unsigned char  result	PACKED;
	unsigned short lcn	PACKED;
	char reserved[7]	PACKED;
}x25api_hdr_t;


typedef struct {
	x25api_hdr_t hdr	PACKED;
	char data[X25_MAX_DATA]	PACKED;
}x25api_t;


/* 
 * XPIPEMON Definitions
 */

/* valid ip_protocol for UDP management */
#define UDPMGMT_UDP_PROTOCOL 0x11
#define UDPMGMT_XPIPE_SIGNATURE         "XLINK8ND"
#define UDPMGMT_DRVRSTATS_SIGNATURE     "DRVSTATS"

/* values for request/reply byte */
#define UDPMGMT_REQUEST	0x01
#define UDPMGMT_REPLY	0x02
#define UDP_OFFSET	12


typedef struct {
	unsigned char opp_flag  PACKED; /* the opp flag */
	unsigned char command	PACKED;	/* command code */
	unsigned short length	PACKED;	/* transfer data length */
	unsigned char result	PACKED;	/* return code */
	unsigned char pf	PACKED;	/* P/F bit */
	unsigned short lcn	PACKED;	/* logical channel */
	unsigned char qdm	PACKED;	/* Q/D/M bits */
	unsigned char cause	PACKED;	/* cause field */
	unsigned char diagn	PACKED;	/* diagnostics */
	unsigned char pktType	PACKED;	/* packet type */
	unsigned char resrv[4]	PACKED;	/* reserved */
} cblock_t;

typedef struct {
	ip_pkt_t 		ip_pkt		PACKED;
	udp_pkt_t		udp_pkt		PACKED;
	wp_mgmt_t 		wp_mgmt       	PACKED;
        cblock_t                cblock          PACKED;
        unsigned char           data[4080]      PACKED;
} x25_udp_pkt_t;


typedef struct read_hdlc_stat {
	unsigned short inf_frames_rx_ok PACKED;
        unsigned short inf_frames_rx_out_of_seq PACKED;
	unsigned short inf_frames_rx_no_data PACKED;
	unsigned short inf_frames_rx_dropped PACKED;
	unsigned short inf_frames_rx_data_too_long PACKED;
	unsigned short inf_frames_rx_invalid_addr PACKED;
	unsigned short inf_frames_tx_ok PACKED;
        unsigned short inf_frames_tx_retransmit PACKED;
       	unsigned short T1_timeouts PACKED;
	unsigned short SABM_frames_rx PACKED;
	unsigned short DISC_frames_rx PACKED;
	unsigned short DM_frames_rx PACKED;
	unsigned short FRMR_frames_rx PACKED;
	unsigned short SABM_frames_tx PACKED;
	unsigned short DISC_frames_tx PACKED;
	unsigned short DM_frames_tx PACKED;
	unsigned short FRMR_frames_tx PACKED;
} read_hdlc_stat_t;

typedef struct read_comms_err_stats{
	unsigned char overrun_err_rx PACKED;
	unsigned char CRC_err PACKED;
	unsigned char abort_frames_rx PACKED;
	unsigned char frames_dropped_buf_full PACKED;
	unsigned char abort_frames_tx PACKED;
	unsigned char transmit_underruns PACKED;
	unsigned char missed_tx_underruns_intr PACKED;
	unsigned char reserved PACKED;
	unsigned char DCD_drop PACKED;
	unsigned char CTS_drop PACKED;
} read_comms_err_stats_t;

typedef struct trace_data {
	unsigned short length PACKED;
	unsigned char  type PACKED;
	unsigned char  trace_dropped PACKED;
	unsigned char  reserved[5] PACKED;
	unsigned short timestamp PACKED;
        unsigned char  data PACKED;
} trace_data_t;

enum {UDP_XPIPE_TYPE};

#define XPIPE_ENABLE_TRACING                    0x14
#define XPIPE_DISABLE_TRACING                   0x14
#define XPIPE_GET_TRACE_INFO                    0x16
#define XPIPE_FT1_READ_STATUS                   0x74
#define XPIPE_DRIVER_STAT_IFSEND                0x75
#define XPIPE_DRIVER_STAT_INTR                  0x76
#define XPIPE_DRIVER_STAT_GEN                   0x77
#define XPIPE_FLUSH_DRIVER_STATS                0x78
#define XPIPE_ROUTER_UP_TIME                    0x79        
#define XPIPE_SET_FT1_MODE			0x81
#define XPIPE_FT1_STATUS_CTRL			0x80


/* error messages */
#define NO_BUFFS_OR_CLOSED_WIN  0x33
#define DATA_LENGTH_TOO_BIG     0x32
#define NO_DATA_AVAILABLE       0x33
#define Z80_TIMEOUT_ERROR       0x0a   
#define	NO_BUFFS		0x08


/* Trace options */
#define TRACE_DEFAULT		0x03
#define TRACE_SUPERVISOR_FRMS	0x10
#define TRACE_ASYNC_FRMS	0x20
#define TRACE_ALL_HDLC_FRMS	0x40
#define TRACE_DATA_FRMS		0x08


#endif	/* _SDLA_X25_H */
