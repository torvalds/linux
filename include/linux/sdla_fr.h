/*****************************************************************************
* sdla_fr.h	Sangoma frame relay firmware API definitions.
*
* Author:       Gideon Hack  	
*		Nenad Corbic <ncorbic@sangoma.com> 	
*
* Copyright:	(c) 1995-2000 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Oct 04, 1999  Gideon Hack     Updated API structures
* Jun 02, 1999  Gideon Hack 	Modifications for S514 support
* Oct 12, 1997	Jaspreet Singh	Added FR_READ_DLCI_IB_MAPPING
* Jul 21, 1997 	Jaspreet Singh	Changed FRRES_TOO_LONG and FRRES_TOO_MANY to 
*				0x05 and 0x06 respectively.
* Dec 23, 1996	Gene Kozin	v2.0
* Apr 29, 1996	Gene Kozin	v1.0 (merged version S502 & S508 definitions).
* Sep 26, 1995	Gene Kozin	Initial version.
*****************************************************************************/
#ifndef	_SDLA_FR_H
#define	_SDLA_FR_H

/*----------------------------------------------------------------------------
 * Notes:
 * ------
 * 1. All structures defined in this file are byte-alined.  
 *
 *	Compiler	Platform
 *	--------	--------
 *	GNU C		Linux
 */

#ifndef	PACKED
#    define	PACKED	__attribute__((packed))
#endif	/* PACKED */

/* Adapter memory layout */
#define	FR_MB_VECTOR	0xE000	/* mailbox window vector */
#define	FR502_RX_VECTOR	0xA000	/* S502 direct receive window vector */
#define	FR502_MBOX_OFFS	0xF60	/* S502 mailbox offset */
#define	FR508_MBOX_OFFS	0	/* S508 mailbox offset */
#define	FR502_FLAG_OFFS	0x1FF0	/* S502 status flags offset */
#define	FR508_FLAG_OFFS	0x1000	/* S508 status flags offset */
#define	FR502_RXMB_OFFS	0x900	/* S502 direct receive mailbox offset */
#define	FR508_TXBC_OFFS	0x1100	/* S508 Tx buffer info offset */
#define	FR508_RXBC_OFFS	0x1120	/* S508 Rx buffer info offset */

/* Important constants */
#define FR502_MAX_DATA	4096	/* maximum data buffer length */
#define FR508_MAX_DATA	4080	/* maximum data buffer length */
#define MIN_LGTH_FR_DATA_CFG         300     /* min Information frame length
(for configuration purposes) */
#define FR_MAX_NO_DATA_BYTES_IN_FRAME  15354 	/* max Information frame length */
 
#define HIGHEST_VALID_DLCI	991

/****** Data Structures *****************************************************/

/*----------------------------------------------------------------------------
 * Frame relay command block.
 */
typedef struct fr_cmd
{
	unsigned char  command	PACKED;	/* command code */
	unsigned short length	PACKED;	/* length of data buffer */
	unsigned char  result	PACKED;	/* return code */
	unsigned short dlci	PACKED;	/* DLCI number */
	unsigned char  attr	PACKED;	/* FECN, BECN, DE and C/R bits */
	unsigned short rxlost1	PACKED;	/* frames discarded at int. level */
	unsigned long  rxlost2	PACKED;	/* frames discarded at app. level */
	unsigned char  rsrv[2]	PACKED;	/* reserved for future use */
} fr_cmd_t;

/* 'command' field defines */
#define	FR_WRITE		0x01
#define	FR_READ			0x02
#define	FR_ISSUE_IS_FRAME	0x03
#define FR_SET_CONFIG		0x10
#define FR_READ_CONFIG		0x11
#define FR_COMM_DISABLE		0x12
#define FR_COMM_ENABLE		0x13
#define FR_READ_STATUS		0x14
#define FR_READ_STATISTICS	0x15
#define FR_FLUSH_STATISTICS	0x16
#define	FR_LIST_ACTIVE_DLCI	0x17
#define FR_FLUSH_DATA_BUFFERS	0x18
#define FR_READ_ADD_DLC_STATS	0x19
#define	FR_ADD_DLCI		0x20
#define	FR_DELETE_DLCI		0x21
#define	FR_ACTIVATE_DLCI	0x22
#define	FR_DEACTIVATE_DLCI	0x22
#define FR_READ_MODEM_STATUS	0x30
#define FR_SET_MODEM_STATUS	0x31
#define FR_READ_ERROR_STATS	0x32
#define FR_FLUSH_ERROR_STATS	0x33
#define FR_READ_DLCI_IB_MAPPING 0x34
#define FR_READ_CODE_VERSION	0x40
#define	FR_SET_INTR_MODE	0x50
#define	FR_READ_INTR_MODE	0x51
#define FR_SET_TRACE_CONFIG	0x60
#define FR_FT1_STATUS_CTRL 	0x80
#define FR_SET_FT1_MODE		0x81

/* Special UDP drivers management commands */
#define FPIPE_ENABLE_TRACING          	0x41
#define FPIPE_DISABLE_TRACING		0x42
#define FPIPE_GET_TRACE_INFO            0x43
#define FPIPE_FT1_READ_STATUS           0x44
#define FPIPE_DRIVER_STAT_IFSEND        0x45
#define FPIPE_DRIVER_STAT_INTR          0x46
#define FPIPE_DRIVER_STAT_GEN           0x47
#define FPIPE_FLUSH_DRIVER_STATS        0x48
#define FPIPE_ROUTER_UP_TIME            0x49

/* 'result' field defines */
#define FRRES_OK		0x00	/* command executed successfully */
#define	FRRES_DISABLED		0x01	/* communications not enabled */
#define	FRRES_INOPERATIVE	0x02	/* channel inoperative */
#define	FRRES_DLCI_INACTIVE	0x03	/* DLCI is inactive */
#define	FRRES_DLCI_INVALID	0x04	/* DLCI is not configured */
#define	FRRES_TOO_LONG		0x05
#define	FRRES_TOO_MANY		0x06
#define	FRRES_CIR_OVERFLOW	0x07	/* Tx throughput has exceeded CIR */
#define	FRRES_BUFFER_OVERFLOW	0x08
#define	FRRES_MODEM_FAILURE	0x10	/* DCD and/or CTS dropped */
#define	FRRES_CHANNEL_DOWN	0x11	/* channel became inoperative */
#define	FRRES_CHANNEL_UP	0x12	/* channel became operative */
#define	FRRES_DLCI_CHANGE	0x13	/* DLCI status (or number) changed */
#define	FRRES_DLCI_MISMATCH	0x14
#define	FRRES_INVALID_CMD	0x1F	/* invalid command */

/* 'attr' field defines */
#define	FRATTR_

/*----------------------------------------------------------------------------
 * Frame relay mailbox.
 *	This structure is located at offset FR50?_MBOX_OFFS into FR_MB_VECTOR.
 *	For S502 it is also located at offset FR502_RXMB_OFFS into
 *	FR502_RX_VECTOR.
 */
typedef struct fr_mbox
{
	unsigned char opflag	PACKED;	/* 00h: execution flag */
	fr_cmd_t cmd		PACKED;	/* 01h: command block */
	unsigned char data[1]	PACKED;	/* 10h: variable length data buffer */
} fr_mbox_t;

/*----------------------------------------------------------------------------
 * S502 frame relay status flags.
 *	This structure is located at offset FR502_FLAG_OFFS into FR_MB_VECTOR.
 */
typedef struct	fr502_flags
{	
	unsigned char rsrv1[1]	PACKED;	/* 00h: */
	unsigned char tx_ready	PACKED;	/* 01h: Tx buffer available */
	unsigned char rx_ready	PACKED;	/* 02h: Rx frame available */
	unsigned char event	PACKED;	/* 03h: asynchronous event */
	unsigned char mstatus	PACKED;	/* 04h: modem status */
	unsigned char rsrv2[8]	PACKED;	/* 05h: */
	unsigned char iflag	PACKED;	/* 0Dh: interrupt flag */
	unsigned char imask	PACKED;	/* 0Eh: interrupt mask */
} fr502_flags_t;

/*----------------------------------------------------------------------------
 * S508 frame relay status flags.
 *	This structure is located at offset FR508_FLAG_OFFS into FR_MB_VECTOR.
 */
typedef struct	fr508_flags
{
	unsigned char rsrv1[3]	PACKED;	/* 00h: reserved */
	unsigned char event	PACKED;	/* 03h: asynchronous event */
	unsigned char mstatus	PACKED;	/* 04h: modem status */
	unsigned char rsrv2[11]	PACKED;	/* 05h: reserved */
	unsigned char iflag	PACKED;	/* 10h: interrupt flag */
	unsigned char imask	PACKED;	/* 11h: interrupt mask */
	unsigned long tse_offs	PACKED;	/* 12h: Tx status element */
	unsigned short dlci	PACKED; /* 16h: DLCI NUMBER */
} fr508_flags_t;

/* 'event' field defines */
#define	FR_EVENT_STATUS		0x01	/* channel status change */
#define	FR_EVENT_DLC_STATUS	0x02	/* DLC status change */
#define	FR_EVENT_BAD_DLCI	0x04	/* FSR included wrong DLCI */
#define	FR_EVENT_LINK_DOWN	0x40	/* DCD or CTS low */

/* 'mstatus' field defines */
#define	FR_MDM_DCD		0x08	/* mdm_status: DCD */
#define	FR_MDM_CTS		0x20	/* mdm_status: CTS */

/* 'iflag' & 'imask' fields defines */
#define	FR_INTR_RXRDY		0x01	/* Rx ready */
#define	FR_INTR_TXRDY		0x02	/* Tx ready */
#define	FR_INTR_MODEM		0x04	/* modem status change (DCD, CTS) */
#define	FR_INTR_READY		0x08	/* interface command completed */
#define	FR_INTR_DLC		0x10	/* DLC status change */
#define	FR_INTR_TIMER		0x20	/* millisecond timer */
#define FR_INTR_TX_MULT_DLCIs	0x80	/* Tx interrupt on multiple DLCIs */


/*----------------------------------------------------------------------------
 * Receive Buffer Configuration Info. S508 only!
 *	This structure is located at offset FR508_RXBC_OFFS into FR_MB_VECTOR.
 */
typedef struct	fr_buf_info
{
	unsigned short rse_num	PACKED;	/* 00h: number of status elements */
	unsigned long rse_base	PACKED;	/* 02h: receive status array base */
	unsigned long rse_next	PACKED;	/* 06h: next status element */
	unsigned long buf_base	PACKED;	/* 0Ah: rotational buffer base */
	unsigned short reserved	PACKED;	/* 0Eh:  */
	unsigned long buf_top	PACKED;	/* 10h: rotational buffer top */
} fr_buf_info_t;

/*----------------------------------------------------------------------------
 * Buffer Status Element. S508 only!
 *	Array of structures of this type is located at offset defined by the
 *	'rse_base' field of the frBufInfo_t structure into absolute adapter
 *	memory address space.
 */
typedef struct	fr_rx_buf_ctl
{
	unsigned char flag	PACKED;	/* 00h: ready flag */
	unsigned short length	PACKED;	/* 01h: frame length */
	unsigned short dlci	PACKED;	/* 03h: DLCI */
	unsigned char attr	PACKED;	/* 05h: FECN/BECN/DE/CR */
	unsigned short tmstamp	PACKED;	/* 06h: time stamp */
	unsigned short rsrv[2]	PACKED; /* 08h:  */
	unsigned long offset	PACKED;	/* 0Ch: buffer absolute address */
} fr_rx_buf_ctl_t;

typedef struct  fr_tx_buf_ctl
{
        unsigned char flag      PACKED; /* 00h: ready flag */
	unsigned short rsrv0[2]	PACKED;	/* 01h: */
        unsigned short length   PACKED; /* 05h: frame length */
        unsigned short dlci     PACKED; /* 07h: DLCI */
        unsigned char attr      PACKED; /* 09h: FECN/BECN/DE/CR */
        unsigned short rsrv1 	PACKED; /* 0Ah:  */
        unsigned long offset    PACKED; /* 0Ch: buffer absolute address */
} fr_tx_buf_ctl_t;

/*----------------------------------------------------------------------------
 * Global Configuration Block. Passed to FR_SET_CONFIG command when dlci == 0.
 */
typedef struct	fr_conf
{
	unsigned short station	PACKED;	/* 00h: CPE/Node */
	unsigned short options	PACKED;	/* 02h: configuration options */
	unsigned short kbps	PACKED;	/* 04h: baud rate in kbps */
	unsigned short port	PACKED;	/* 06h: RS-232/V.35 */
	unsigned short mtu	PACKED;	/* 08h: max. transmit length */
	unsigned short t391	PACKED;	/* 0Ah:  */
	unsigned short t392	PACKED;	/* 0Ch:  */
	unsigned short n391	PACKED;	/* 0Eh:  */
	unsigned short n392	PACKED;	/* 10h:  */
	unsigned short n393	PACKED;	/* 12h:  */
	unsigned short cir_fwd	PACKED;	/* 14h:  */
	unsigned short bc_fwd	PACKED;	/* 16h:  */
	unsigned short be_fwd	PACKED;	/* 18h:  */
	unsigned short cir_bwd	PACKED;	/* 1Ah:  */
	unsigned short bc_bwd	PACKED;	/* 1Ch:  */
	unsigned short be_bwd	PACKED;	/* 1Eh:  */
	unsigned short dlci[0]	PACKED;	/* 20h:  */
} fr_conf_t;

/* 'station_type' defines */
#define	FRCFG_STATION_CPE	0
#define	FRCFG_STATION_NODE	1

/* 'conf_flags' defines */
#define	FRCFG_IGNORE_TX_CIR	0x0001
#define	FRCFG_IGNORE_RX_CIR	0x0002
#define	FRCFG_DONT_RETRANSMIT	0x0004
#define	FRCFG_IGNORE_CBS	0x0008
#define	FRCFG_THROUGHPUT	0x0010	/* enable throughput calculation */
#define	FRCFG_DIRECT_RX		0x0080	/* enable direct receive buffer */
#define	FRCFG_AUTO_CONFIG	0x8000	/* enable  auto DLCI configuration */

/* 'baud_rate' defines */
#define	FRCFG_BAUD_1200		12
#define	FRCFG_BAUD_2400		24
#define	FRCFG_BAUD_4800		48
#define	FRCFG_BAUD_9600		96
#define	FRCFG_BAUD_19200	19
#define	FRCFG_BAUD_38400	38
#define	FRCFG_BAUD_56000	56
#define	FRCFG_BAUD_64000	64
#define	FRCFG_BAUD_128000	128

/* 'port_mode' defines */
#define	FRCFG_MODE_EXT_CLK	0x0000
#define	FRCFG_MODE_INT_CLK	0x0001
#define	FRCFG_MODE_V35		0x0000	/* S508 only */
#define	FRCFG_MODE_RS232	0x0002	/* S508 only */

/* defines for line tracing */

/* the line trace status element presented by the frame relay code */
typedef struct {
        unsigned char flag      PACKED; /* ready flag */
        unsigned short length   PACKED; /* trace length */
        unsigned char rsrv0[2]  PACKED; /* reserved */
        unsigned char attr      PACKED; /* trace attributes */
        unsigned short tmstamp  PACKED; /* time stamp */
        unsigned char rsrv1[4]  PACKED; /* reserved */
        unsigned long offset    PACKED; /* buffer absolute address */
} fr_trc_el_t;

typedef struct {
        unsigned char status    	PACKED; /* status flag */
	unsigned char data_passed	PACKED;	/* 0 if no data passed, 1 if */
						/* data passed */
        unsigned short length   	PACKED; /* frame length */
        unsigned short tmstamp  	PACKED; /* time stamp */
} fpipemon_trc_hdr_t;

typedef struct {
	fpipemon_trc_hdr_t fpipemon_trc_hdr			PACKED;
        unsigned char data[FR_MAX_NO_DATA_BYTES_IN_FRAME]	PACKED;
} fpipemon_trc_t;

/* bit settings for the 'status' byte  - note that bits 1, 2 and 3 are used */
/* for returning the number of frames being passed to fpipemon */
#define TRC_OUTGOING_FRM	0x01
#define TRC_ABORT_ERROR         0x10
#define TRC_CRC_ERROR           0x20
#define TRC_OVERRUN_ERROR       0x40
#define MORE_TRC_DATA		0x80

#define MAX_FRMS_TRACED		0x07

#define NO_TRC_ELEMENTS_OFF		0x9000
#define BASE_TRC_ELEMENTS_OFF		0x9002
#define TRC_ACTIVE			0x01
#define FLUSH_TRC_BUFFERS 		0x02
#define FLUSH_TRC_STATISTICS		0x04
#define TRC_SIGNALLING_FRMS		0x10
#define TRC_INFO_FRMS			0x20
#define ACTIVATE_TRC	(TRC_ACTIVE | TRC_SIGNALLING_FRMS | TRC_INFO_FRMS)
#define RESET_TRC	(FLUSH_TRC_BUFFERS | FLUSH_TRC_STATISTICS)

/*----------------------------------------------------------------------------
 * Channel configuration.
 *	This structure is passed to the FR_SET_CONFIG command when dlci != 0.
 */
typedef struct	fr_dlc_conf
{
	unsigned short conf_flags	PACKED;	/* 00h: configuration bits */
	unsigned short cir_fwd		PACKED;	/* 02h:  */
	unsigned short bc_fwd		PACKED;	/* 04h:  */
	unsigned short be_fwd		PACKED;	/* 06h:  */
	unsigned short cir_bwd		PACKED;	/* 08h:  */
	unsigned short bc_bwd		PACKED;	/* 0Ah:  */
	unsigned short be_bwd		PACKED;	/* 0Ch:  */
} fr_dlc_conf_t;

/*----------------------------------------------------------------------------
 * S502 interrupt mode control block.
 *	This structure is passed to the FR_SET_INTR_FLAGS and returned by the
 *	FR_READ_INTR_FLAGS commands.
 */
typedef struct fr502_intr_ctl
{
	unsigned char mode	PACKED;	/* 00h: interrupt enable flags */
	unsigned short tx_len	PACKED;	/* 01h: required Tx buffer size */
} fr502_intr_ctl_t;

/*----------------------------------------------------------------------------
 * S508 interrupt mode control block.
 *	This structure is passed to the FR_SET_INTR_FLAGS and returned by the
 *	FR_READ_INTR_FLAGS commands.
 */
typedef struct fr508_intr_ctl
{
	unsigned char mode	PACKED;	/* 00h: interrupt enable flags */
	unsigned short tx_len	PACKED;	/* 01h: required Tx buffer size */
	unsigned char irq	PACKED;	/* 03h: IRQ level to activate */
	unsigned char flags	PACKED;	/* 04h: ?? */
	unsigned short timeout	PACKED;	/* 05h: ms, for timer interrupt */
} fr508_intr_ctl_t;

/*----------------------------------------------------------------------------
 * Channel status.
 *	This structure is returned by the FR_READ_STATUS command.
 */
typedef struct	fr_dlc_Status
{
	unsigned char status		PACKED;	/* 00h: link/DLCI status */
	struct
	{
		unsigned short dlci	PACKED;	/* 01h: DLCI number */
		unsigned char status	PACKED;	/* 03h: DLCI status */
	} circuit[1]			PACKED;
} fr_dlc_status_t;

/* 'status' defines */
#define	FR_LINK_INOPER	0x00		/* for global status (DLCI == 0) */
#define	FR_LINK_OPER	0x01
#define	FR_DLCI_DELETED	0x01		/* for circuit status (DLCI != 0) */
#define	FR_DLCI_ACTIVE	0x02
#define	FR_DLCI_WAITING	0x04
#define	FR_DLCI_NEW	0x08
#define	FR_DLCI_REPORT	0x40

/*----------------------------------------------------------------------------
 * Global Statistics Block.
 *	This structure is returned by the FR_READ_STATISTICS command when
 *	dcli == 0.
 */
typedef struct	fr_link_stat
{
	unsigned short rx_too_long	PACKED;	/* 00h:  */
	unsigned short rx_dropped	PACKED;	/* 02h:  */
	unsigned short rx_dropped2	PACKED;	/* 04h:  */
	unsigned short rx_bad_dlci	PACKED;	/* 06h:  */
	unsigned short rx_bad_format	PACKED;	/* 08h:  */
	unsigned short retransmitted	PACKED;	/* 0Ah:  */
	unsigned short cpe_tx_FSE	PACKED;	/* 0Ch:  */
	unsigned short cpe_tx_LIV	PACKED;	/* 0Eh:  */
	unsigned short cpe_rx_FSR	PACKED;	/* 10h:  */
	unsigned short cpe_rx_LIV	PACKED;	/* 12h:  */
	unsigned short node_rx_FSE	PACKED;	/* 14h:  */
	unsigned short node_rx_LIV	PACKED;	/* 16h:  */
	unsigned short node_tx_FSR	PACKED;	/* 18h:  */
	unsigned short node_tx_LIV	PACKED;	/* 1Ah:  */
	unsigned short rx_ISF_err	PACKED;	/* 1Ch:  */
	unsigned short rx_unsolicited	PACKED;	/* 1Eh:  */
	unsigned short rx_SSN_err	PACKED;	/* 20h:  */
	unsigned short rx_RSN_err	PACKED;	/* 22h:  */
	unsigned short T391_timeouts	PACKED;	/* 24h:  */
	unsigned short T392_timeouts	PACKED;	/* 26h:  */
	unsigned short N392_reached	PACKED;	/* 28h:  */
	unsigned short cpe_SSN_RSN	PACKED;	/* 2Ah:  */
	unsigned short current_SSN	PACKED;	/* 2Ch:  */
	unsigned short current_RSN	PACKED;	/* 2Eh:  */
	unsigned short curreny_T391	PACKED;	/* 30h:  */
	unsigned short current_T392	PACKED;	/* 32h:  */
	unsigned short current_N392	PACKED;	/* 34h:  */
	unsigned short current_N393	PACKED;	/* 36h:  */
} fr_link_stat_t;

/*----------------------------------------------------------------------------
 * DLCI statistics.
 *	This structure is returned by the FR_READ_STATISTICS command when
 *	dlci != 0.
 */
typedef struct	fr_dlci_stat
{
	unsigned long tx_frames		PACKED;	/* 00h:  */
	unsigned long tx_bytes		PACKED;	/* 04h:  */
	unsigned long rx_frames		PACKED;	/* 08h:  */
	unsigned long rx_bytes		PACKED;	/* 0Ch:  */
	unsigned long rx_dropped	PACKED;	/* 10h:  */
	unsigned long rx_inactive	PACKED;	/* 14h:  */
	unsigned long rx_exceed_CIR	PACKED;	/* 18h:  */
	unsigned long rx_DE_set		PACKED;	/* 1Ch:  */
	unsigned long tx_throughput	PACKED;	/* 20h:  */
	unsigned long tx_calc_timer	PACKED;	/* 24h:  */
	unsigned long rx_throughput	PACKED;	/* 28h:  */
	unsigned long rx_calc_timer	PACKED;	/* 2Ch:  */
} fr_dlci_stat_t;

/*----------------------------------------------------------------------------
 * Communications error statistics.
 *	This structure is returned by the FR_READ_ERROR_STATS command.
 */
typedef struct	fr_comm_stat
{
	unsigned char rx_overruns	PACKED;	/* 00h:  */
	unsigned char rx_bad_crc	PACKED;	/* 01h:  */
	unsigned char rx_aborts		PACKED;	/* 02h:  */
	unsigned char rx_too_long	PACKED;	/* 03h:  */
	unsigned char tx_aborts		PACKED;	/* 04h:  */
	unsigned char tx_underruns	PACKED;	/* 05h:  */
	unsigned char tx_missed_undr	PACKED;	/* 06h:  */
	unsigned char dcd_dropped	PACKED;	/* 07h:  */
	unsigned char cts_dropped	PACKED;	/* 08h:  */
} fr_comm_stat_t;

/*----------------------------------------------------------------------------
 * Defines for the FR_ISSUE_IS_FRAME command.
 */
#define	FR_ISF_LVE	2		/* issue Link Verification Enquiry */
#define	FR_ISF_FSE	3		/* issue Full Status Enquiry */

/*----------------------------------------------------------------------------
 * Frame Relay ARP Header -- Used for Dynamic route creation with InvARP 
 */

typedef struct arphdr_fr
	{
	unsigned short ar_hrd PACKED;		/* format of hardware addr */
	unsigned short ar_pro PACKED;		/* format of protocol addr */
	unsigned char  ar_hln PACKED;		/* length of hardware addr */	
	unsigned char  ar_pln PACKED;		/* length of protocol addr */
	unsigned short ar_op  PACKED;		/* ARP opcode		   */
	unsigned short ar_sha PACKED;		/* Sender DLCI addr 2 bytes */
	unsigned long  ar_sip PACKED;		/* Sender IP   addr 4 bytes */
	unsigned short ar_tha PACKED;		/* Target DLCI addr 2 bytes */
	unsigned long  ar_tip PACKED;		/* Target IP   addr 4 bytes */
	} arphdr_fr_t;

/*----------------------------------------------------------------------------
 * Frame Relay RFC 1490 SNAP Header -- Used to check for ARP packets
 */
typedef struct arphdr_1490
	{
	unsigned char control PACKED;		/* UI, etc...  */
	unsigned char pad     PACKED;		/* Pad */
	unsigned char NLPID   PACKED;		/* SNAP */
	unsigned char OUI[3]  PACKED;		/* Ethertype, etc... */
	unsigned short PID    PACKED;		/* ARP, IP, etc... */
	}  arphdr_1490_t;

/* UDP/IP packet (for UDP management) layout */

/* The embedded control block for UDP mgmt
   This is essentially a mailbox structure, without the large data field */

typedef struct {
        unsigned char  opp_flag PACKED; /* the opp flag */
        unsigned char  command  PACKED; /* command code */
        unsigned short length   PACKED; /* length of data buffer */
        unsigned char  result   PACKED; /* return code */
        unsigned short dlci     PACKED; /* DLCI number */
        unsigned char  attr     PACKED; /* FECN, BECN, DE and C/R bits */
        unsigned short rxlost1  PACKED; /* frames discarded at int. level */
        unsigned long  rxlost2  PACKED; /* frames discarded at app. level */
        unsigned char  rsrv[2]  PACKED; /* reserved for future use */
} cblock_t;


/* UDP management packet layout (data area of ip packet) */

typedef struct {
        unsigned char   control                 PACKED;
        unsigned char   NLPID                   PACKED;
} fr_encap_hdr_t;

typedef struct {
//	fr_encap_hdr_t 		fr_encap_hdr	PACKED;
	ip_pkt_t 		ip_pkt		PACKED;
	udp_pkt_t		udp_pkt		PACKED;
	wp_mgmt_t 		wp_mgmt       	PACKED;
        cblock_t                cblock          PACKED;
        unsigned char           data[4080]      PACKED;
} fr_udp_pkt_t;


/* valid ip_protocol for UDP management */
#define UDPMGMT_UDP_PROTOCOL 0x11

#define UDPMGMT_FPIPE_SIGNATURE         "FPIPE8ND"
#define UDPMGMT_DRVRSTATS_SIGNATURE     "DRVSTATS"

/* values for request/reply byte */
#define UDPMGMT_REQUEST	0x01
#define UDPMGMT_REPLY	0x02
#define UDP_OFFSET	12

typedef struct {
        unsigned long if_send_entry;
        unsigned long if_send_skb_null;
        unsigned long if_send_broadcast;
        unsigned long if_send_multicast;
        unsigned long if_send_critical_ISR;
        unsigned long if_send_critical_non_ISR;
        unsigned long if_send_busy;
        unsigned long if_send_busy_timeout;
	unsigned long if_send_DRVSTATS_request;
        unsigned long if_send_FPIPE_request;
        unsigned long if_send_wan_disconnected;
        unsigned long if_send_dlci_disconnected;
        unsigned long if_send_no_bfrs;
        unsigned long if_send_adptr_bfrs_full;
        unsigned long if_send_bfrs_passed_to_adptr;
	unsigned long if_send_consec_send_fail;
} drvstats_if_send_t; 

typedef struct {
        unsigned long rx_intr_no_socket;
        unsigned long rx_intr_dev_not_started;
        unsigned long rx_intr_DRVSTATS_request;
        unsigned long rx_intr_FPIPE_request;
        unsigned long rx_intr_bfr_not_passed_to_stack;
        unsigned long rx_intr_bfr_passed_to_stack;
 } drvstats_rx_intr_t;

typedef struct {
        unsigned long UDP_FPIPE_mgmt_kmalloc_err;
        unsigned long UDP_FPIPE_mgmt_direction_err;
        unsigned long UDP_FPIPE_mgmt_adptr_type_err;
        unsigned long UDP_FPIPE_mgmt_adptr_cmnd_OK;
        unsigned long UDP_FPIPE_mgmt_adptr_cmnd_timeout;
        unsigned long UDP_FPIPE_mgmt_adptr_send_passed;
        unsigned long UDP_FPIPE_mgmt_adptr_send_failed;
        unsigned long UDP_FPIPE_mgmt_not_passed_to_stack;
        unsigned long UDP_FPIPE_mgmt_passed_to_stack;
        unsigned long UDP_FPIPE_mgmt_no_socket;
        unsigned long UDP_DRVSTATS_mgmt_kmalloc_err;
        unsigned long UDP_DRVSTATS_mgmt_adptr_cmnd_OK;
        unsigned long UDP_DRVSTATS_mgmt_adptr_cmnd_timeout;
        unsigned long UDP_DRVSTATS_mgmt_adptr_send_passed;
        unsigned long UDP_DRVSTATS_mgmt_adptr_send_failed;
        unsigned long UDP_DRVSTATS_mgmt_not_passed_to_stack;
        unsigned long UDP_DRVSTATS_mgmt_passed_to_stack;
        unsigned long UDP_DRVSTATS_mgmt_no_socket;
} drvstats_gen_t;

typedef struct {
        unsigned char   attr      	PACKED;
        unsigned short  time_stamp      PACKED;
        unsigned char   reserved[13]    PACKED;
} api_rx_hdr_t;

typedef struct {
        api_rx_hdr_t    api_rx_hdr      PACKED;
        void *          data            PACKED;
} api_rx_element_t;

typedef struct {
        unsigned char   attr            PACKED;
        unsigned char   reserved[15]    PACKED;
} api_tx_hdr_t;

typedef struct {
        api_tx_hdr_t    api_tx_hdr      PACKED;
        void *          data            PACKED;
} api_tx_element_t;

#ifdef		_MSC_
#  pragma	pack()
#endif
#endif	/* _SDLA_FR_H */

