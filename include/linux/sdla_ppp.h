/*****************************************************************************
* sdla_ppp.h	Sangoma PPP firmware API definitions.
*
* Author:	Nenad Corbic	<ncorbic@sangoma.com>
*
* Copyright:	(c) 1995-1997 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Feb 24, 2000  Nenad Corbic    v2.1.2
* Jan 06, 1997	Gene Kozin	v2.0
* Apr 11, 1996	Gene Kozin	Initial version.
*****************************************************************************/
#ifndef	_SDLA_PPP_H
#define	_SDLA_PPP_H

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

/* Adapter memory layout and important constants */
#define	PPP508_MB_VECT	0xE000	/* mailbox window vector */
#define	PPP508_MB_OFFS	0		/* mailbox offset */
#define	PPP508_FLG_OFFS	0x1000	/* status flags offset */
#define	PPP508_BUF_OFFS	0x1100	/* buffer info block offset */
#define PPP514_MB_OFFS  0xE000  /* mailbox offset */
#define PPP514_FLG_OFFS 0xF000  /* status flags offset */
#define PPP514_BUF_OFFS 0xF100  /* buffer info block offset */

#define PPP_MAX_DATA	1008	/* command block data buffer length */

/****** Data Structures *****************************************************/

/*----------------------------------------------------------------------------
 * PPP Command Block.
 */
typedef struct ppp_cmd{
	unsigned char  command	PACKED;	/* command code */
	unsigned short length	PACKED;	/* length of data buffer */
	unsigned char  result	PACKED;	/* return code */
	unsigned char  rsrv[11]	PACKED;	/* reserved for future use */
} ppp_cmd_t;

typedef struct cblock{
	unsigned char  opp_flag	PACKED;
	unsigned char  command	PACKED;	/* command code */
	unsigned short length	PACKED;	/* length of data buffer */
	unsigned char  result	PACKED;	/* return code */
	unsigned char  rsrv[11]	PACKED;	/* reserved for future use */
} cblock_t;

typedef struct ppp_udp_pkt{
	ip_pkt_t 	ip_pkt	PACKED;
	udp_pkt_t	udp_pkt	PACKED;
	wp_mgmt_t	wp_mgmt PACKED;
	cblock_t	cblock  PACKED;
	unsigned char   data[MAX_LGTH_UDP_MGNT_PKT] PACKED;
} ppp_udp_pkt_t;	

typedef struct {
	unsigned char	status		PACKED;
	unsigned char	data_avail	PACKED;
	unsigned short	real_length	PACKED;
	unsigned short	time_stamp	PACKED;
	unsigned char	data[1]		PACKED;
} trace_pkt_t;


typedef struct {
	unsigned char 	opp_flag	PACKED;
	unsigned char	trace_type	PACKED;
	unsigned short 	trace_length	PACKED;
	unsigned short 	trace_data_ptr	PACKED;
	unsigned short  trace_time_stamp PACKED;
} trace_element_t;

/* 'command' field defines */
#define PPP_READ_CODE_VERSION	0x10	/* configuration commands */
#define PPP_SET_CONFIG		0x05
#define PPP_READ_CONFIG		0x06
#define	PPP_SET_INTR_FLAGS	0x20
#define	PPP_READ_INTR_FLAGS	0x21
#define	PPP_SET_INBOUND_AUTH	0x30
#define	PPP_SET_OUTBOUND_AUTH	0x31
#define	PPP_GET_CONNECTION_INFO	0x32

#define PPP_COMM_ENABLE		0x03	/* operational commands */
#define PPP_COMM_DISABLE	0x04
#define	PPP_SEND_SIGN_FRAME	0x23
#define	PPP_READ_SIGN_RESPONSE	0x24
#define	PPP_DATALINE_MONITOR	0x33

#define PPP_READ_STATISTICS	0x07	/* statistics commands */
#define PPP_FLUSH_STATISTICS	0x08
#define PPP_READ_ERROR_STATS	0x09
#define PPP_FLUSH_ERROR_STATS	0x0A
#define PPP_READ_PACKET_STATS	0x12
#define PPP_FLUSH_PACKET_STATS	0x13
#define PPP_READ_LCP_STATS	0x14
#define PPP_FLUSH_LCP_STATS	0x15
#define PPP_READ_LPBK_STATS	0x16
#define PPP_FLUSH_LPBK_STATS	0x17
#define PPP_READ_IPCP_STATS	0x18
#define PPP_FLUSH_IPCP_STATS	0x19
#define PPP_READ_IPXCP_STATS	0x1A
#define PPP_FLUSH_IPXCP_STATS	0x1B
#define PPP_READ_PAP_STATS	0x1C
#define PPP_FLUSH_PAP_STATS	0x1D
#define PPP_READ_CHAP_STATS	0x1E
#define PPP_FLUSH_CHAP_STATS	0x1F

/* 'result' field defines */
#define PPPRES_OK		0x00	/* command executed successfully */
#define	PPPRES_INVALID_STATE	0x09	/* invalid command in this context */

/*----------------------------------------------------------------------------
 * PPP Mailbox.
 *	This structure is located at offset PPP???_MB_OFFS into PPP???_MB_VECT
 */
typedef struct ppp_mbox
{
	unsigned char flag	PACKED;	/* 00h: command execution flag */
	ppp_cmd_t     cmd	PACKED; /* 01h: command block */
	unsigned char data[1]	PACKED;	/* 10h: variable length data buffer */
} ppp_mbox_t;

/*----------------------------------------------------------------------------
 * PPP Status Flags.
 *	This structure is located at offset PPP???_FLG_OFFS into
 *	PPP???_MB_VECT.
 */
typedef struct	ppp_flags
{
	unsigned char iflag		PACKED;	/* 00: interrupt flag */
	unsigned char imask		PACKED;	/* 01: interrupt mask */
	unsigned char resrv		PACKED;
	unsigned char mstatus		PACKED;	/* 03: modem status */
	unsigned char lcp_state		PACKED; /* 04: LCP state */
	unsigned char ppp_phase		PACKED;	/* 05: PPP phase */
	unsigned char ip_state		PACKED; /* 06: IPCP state */
	unsigned char ipx_state		PACKED; /* 07: IPXCP state */
	unsigned char pap_state		PACKED; /* 08: PAP state */
	unsigned char chap_state	PACKED; /* 09: CHAP state */
	unsigned short disc_cause	PACKED;	/* 0A: disconnection cause */
} ppp_flags_t;

/* 'iflag' defines */
#define	PPP_INTR_RXRDY		0x01	/* Rx ready */
#define	PPP_INTR_TXRDY		0x02	/* Tx ready */
#define	PPP_INTR_MODEM		0x04	/* modem status change (DCD, CTS) */
#define	PPP_INTR_CMD		0x08	/* interface command completed */
#define	PPP_INTR_DISC		0x10	/* data link disconnected */
#define	PPP_INTR_OPEN		0x20	/* data link open */
#define	PPP_INTR_DROP_DTR	0x40	/* DTR drop timeout expired */
#define PPP_INTR_TIMER          0x80    /* timer interrupt */


/* 'mstatus' defines */
#define	PPP_MDM_DCD		0x08	/* mdm_status: DCD */
#define	PPP_MDM_CTS		0x20	/* mdm_status: CTS */

/* 'disc_cause' defines */
#define PPP_LOCAL_TERMINATION   0x0001	/* Local Request by PPP termination phase */
#define PPP_DCD_CTS_DROP        0x0002  /* DCD and/or CTS dropped. Link down */
#define PPP_REMOTE_TERMINATION	0x0800	/* Remote Request by PPP termination phase */

/* 'misc_config_bits' defines */
#define DONT_RE_TX_ABORTED_I_FRAMES 	0x01
#define TX_FRM_BYTE_COUNT_STATS         0x02
#define RX_FRM_BYTE_COUNT_STATS         0x04
#define TIME_STAMP_IN_RX_FRAMES         0x08
#define NON_STD_ADPTR_FREQ              0x10
#define INTERFACE_LEVEL_RS232           0x20
#define AUTO_LINK_RECOVERY              0x100
#define DONT_TERMINATE_LNK_MAX_CONFIG   0x200                    

/* 'authentication options' defines */
#define NO_AUTHENTICATION	0x00
#define INBOUND_AUTH		0x80
#define PAP_AUTH		0x01
#define CHAP_AUTH		0x02		

/* 'ip options' defines */
#define L_AND_R_IP_NO_ASSIG	0x00
#define L_IP_LOCAL_ASSIG    	0x01
#define L_IP_REMOTE_ASSIG   	0x02
#define R_IP_LOCAL_ASSIG        0x04
#define R_IP_REMOTE_ASSIG       0x08
#define ENABLE_IP		0x80

/* 'ipx options' defines */
#define ROUTING_PROT_DEFAULT    0x20
#define ENABLE_IPX		0x80
#define DISABLE_IPX		0x00

/*----------------------------------------------------------------------------
 * PPP Buffer Info.
 *	This structure is located at offset PPP508_BUF_OFFS into
 *	PPP508_MB_VECT.
 */
typedef struct	ppp508_buf_info
{
	unsigned short txb_num	PACKED;	/* 00: number of transmit buffers */
	unsigned long  txb_ptr	PACKED;	/* 02: pointer to the buffer ctl. */
	unsigned long  txb_nxt  PACKED;
	unsigned char  rsrv1[22] PACKED;
	unsigned short rxb_num	PACKED;	/* 20: number of receive buffers */
	unsigned long  rxb_ptr	PACKED;	/* 22: pointer to the buffer ctl. */
	unsigned long  rxb1_ptr	PACKED;	/* 26: pointer to the first buf.ctl. */
	unsigned long  rxb_base	PACKED;	/* 2A: pointer to the buffer base */
	unsigned char  rsrv2[2]	PACKED;
	unsigned long  rxb_end	PACKED;	/* 30: pointer to the buffer end */
} ppp508_buf_info_t;

/*----------------------------------------------------------------------------
 * Transmit/Receive Buffer Control Block.
 */
typedef struct	ppp_buf_ctl
{
	unsigned char  flag		PACKED;	/* 00: 'buffer ready' flag */
	unsigned short length		PACKED;	/* 01: length of data */
	unsigned char  reserved1[1]	PACKED;	/* 03: */
	unsigned char  proto		PACKED;	/* 04: protocol */
	unsigned short timestamp	PACKED;	/* 05: time stamp (Rx only) */
	unsigned char  reserved2[5]	PACKED;	/* 07: */
	union
	{
		unsigned short o_p[2];	/* 1C: buffer offset & page (S502) */
		unsigned long  ptr;	/* 1C: buffer pointer (S508) */
	} buf				PACKED;
} ppp_buf_ctl_t;

/*----------------------------------------------------------------------------
 * S508 Adapter Configuration Block (passed to the PPP_SET_CONFIG command).
 */
typedef struct	ppp508_conf
{
	unsigned long  line_speed	PACKED;	/* 00: baud rate, bps */
	unsigned short txbuf_percent	PACKED;	/* 04: % of Tx buffer */
	unsigned short conf_flags	PACKED;	/* 06: configuration bits */
	unsigned short mtu_local	PACKED;	/* 08: local MTU */
	unsigned short mtu_remote	PACKED;	/* 0A: remote MTU */
	unsigned short restart_tmr	PACKED;	/* 0C: restart timer */
	unsigned short auth_rsrt_tmr	PACKED;	/* 0E: authentication timer */
	unsigned short auth_wait_tmr	PACKED;	/* 10: authentication timer */
	unsigned short mdm_fail_tmr	PACKED;	/* 12: modem failure timer */
	unsigned short dtr_drop_tmr	PACKED;	/* 14: DTR drop timer */
	unsigned short connect_tmout	PACKED;	/* 16: connection timeout */
	unsigned short conf_retry	PACKED;	/* 18: max. retry */
	unsigned short term_retry	PACKED;	/* 1A: max. retry */
	unsigned short fail_retry	PACKED;	/* 1C: max. retry */
	unsigned short auth_retry	PACKED;	/* 1E: max. retry */
	unsigned char  auth_options	PACKED;	/* 20: authentication opt. */
	unsigned char  ip_options	PACKED;	/* 21: IP options */
	unsigned long  ip_local		PACKED;	/* 22: local IP address */
	unsigned long  ip_remote	PACKED;	/* 26: remote IP address */
	unsigned char  ipx_options	PACKED;	/* 2A: IPX options */
	unsigned char  ipx_netno[4]	PACKED;	/* 2B: IPX net number */
	unsigned char  ipx_local[6]	PACKED;	/* 2F: local IPX node number*/
	unsigned char  ipx_remote[6]	PACKED;	/* 35: remote IPX node num.*/
	unsigned char  ipx_router[48]	PACKED;	/* 3B: IPX router name*/
	unsigned long  alt_cpu_clock	PACKED;	/* 6B:  */
} ppp508_conf_t;

/*----------------------------------------------------------------------------
 * S508 Adapter Read Connection Information Block 
 *    Returned by the PPP_GET_CONNECTION_INFO command
 */
typedef struct	ppp508_connect_info
{
	unsigned short 	mru		PACKED;	/* 00-01 Remote Max Rec' Unit */
	unsigned char  	ip_options 	PACKED; /* 02: Negotiated ip options  */
	unsigned long  	ip_local	PACKED;	/* 03-06: local IP address    */
	unsigned long  	ip_remote	PACKED;	/* 07-0A: remote IP address   */
	unsigned char	ipx_options	PACKED; /* 0B: Negotiated ipx options */
	unsigned char  	ipx_netno[4]	PACKED;	/* 0C-0F: IPX net number      */
	unsigned char  	ipx_local[6]	PACKED;	/* 10-1F: local IPX node #    */
	unsigned char  	ipx_remote[6]	PACKED;	/* 16-1B: remote IPX node #   */
	unsigned char  	ipx_router[48]	PACKED;	/* 1C-4B: IPX router name     */
	unsigned char	auth_status	PACKED; /* 4C: Authentication Status  */
	unsigned char 	inbd_auth_peerID[1] PACKED; /* 4D: variable length inbound authenticated peer ID */
} ppp508_connect_info_t;

/* 'line_speed' field */
#define	PPP_BITRATE_1200	0x01
#define	PPP_BITRATE_2400	0x02
#define	PPP_BITRATE_4800	0x03
#define	PPP_BITRATE_9600	0x04
#define	PPP_BITRATE_19200	0x05
#define	PPP_BITRATE_38400	0x06
#define	PPP_BITRATE_45000	0x07
#define	PPP_BITRATE_56000	0x08
#define	PPP_BITRATE_64000	0x09
#define	PPP_BITRATE_74000	0x0A
#define	PPP_BITRATE_112000	0x0B
#define	PPP_BITRATE_128000	0x0C
#define	PPP_BITRATE_156000	0x0D

/* Defines for the 'conf_flags' field */
#define	PPP_IGNORE_TX_ABORT	0x01	/* don't re-transmit aborted frames */
#define	PPP_ENABLE_TX_STATS	0x02	/* enable Tx statistics */
#define	PPP_ENABLE_RX_STATS	0x04	/* enable Rx statistics */
#define	PPP_ENABLE_TIMESTAMP	0x08	/* enable timestamp */

/* 'ip_options' defines */
#define	PPP_LOCAL_IP_LOCAL	0x01
#define	PPP_LOCAL_IP_REMOTE	0x02
#define	PPP_REMOTE_IP_LOCAL	0x04
#define	PPP_REMOTE_IP_REMOTE	0x08

/* 'ipx_options' defines */
#define	PPP_REMOTE_IPX_NETNO	0x01
#define	PPP_REMOTE_IPX_LOCAL	0x02
#define	PPP_REMOTE_IPX_REMOTE	0x04
#define	PPP_IPX_ROUTE_RIP_SAP	0x08
#define	PPP_IPX_ROUTE_NLSP	0x10
#define	PPP_IPX_ROUTE_DEFAULT	0x20
#define	PPP_IPX_CONF_COMPLETE	0x40
#define	PPP_IPX_ENABLE		0x80

/*----------------------------------------------------------------------------
 * S508 Adapter Configuration Block (returned by the PPP_READ_CONFIG command).
 */
typedef struct	ppp508_get_conf
{
	unsigned long  bps	PACKED;	/* 00: baud rate, bps */
	ppp508_conf_t  conf	PACKED;	/* 04: requested config. */
	unsigned short txb_num	PACKED;	/* 6F: number of Tx buffers */
	unsigned short rxb_num	PACKED;	/* 71: number of Rx buffers */
} ppp508_get_conf_t;

/*----------------------------------------------------------------------------
 * S508 Operational Statistics (returned by the PPP_READ_STATISTIC command).
 */
typedef struct ppp508_stats
{
	unsigned short reserved1	PACKED;	/* 00: */
	unsigned short rx_bad_len	PACKED;	/* 02: */
	unsigned short reserved2	PACKED;	/* 04: */
	unsigned long  tx_frames	PACKED;	/* 06: */
	unsigned long  tx_bytes	PACKED;	/* 0A: */
	unsigned long  rx_frames	PACKED;	/* 0E: */
	unsigned long  rx_bytes	PACKED;	/* 12: */
} ppp508_stats_t;

/*----------------------------------------------------------------------------
 * Adapter Error Statistics (returned by the PPP_READ_ERROR_STATS command).
 */
typedef struct	ppp_err_stats
{
	unsigned char	 rx_overrun	PACKED;	/* 00: Rx overrun errors */
	unsigned char	 rx_bad_crc	PACKED;	/* 01: Rx CRC errors */
	unsigned char	 rx_abort	PACKED;	/* 02: Rx aborted frames */
	unsigned char	 rx_lost	PACKED;	/* 03: Rx frames lost */
	unsigned char	 tx_abort	PACKED;	/* 04: Tx aborted frames */
	unsigned char	 tx_underrun	PACKED;	/* 05: Tx underrun errors */
	unsigned char	 tx_missed_intr	PACKED;	/* 06: Tx underruns missed */
	unsigned char	 reserved	PACKED;	/* 07: Tx underruns missed */
	unsigned char	 dcd_trans	PACKED;	/* 08: DCD transitions */
	unsigned char	 cts_trans	PACKED;	/* 09: CTS transitions */
} ppp_err_stats_t;

/*----------------------------------------------------------------------------
 * Packet Statistics (returned by the PPP_READ_PACKET_STATS command).
 */
typedef struct	ppp_pkt_stats
{
	unsigned short rx_bad_header	PACKED;	/* 00: */
	unsigned short rx_prot_unknwn	PACKED;	/* 02: */
	unsigned short rx_too_large	PACKED;	/* 04: */
	unsigned short rx_lcp		PACKED;	/* 06: */
	unsigned short tx_lcp		PACKED;	/* 08: */
	unsigned short rx_ipcp		PACKED;	/* 0A: */
	unsigned short tx_ipcp		PACKED;	/* 0C: */
	unsigned short rx_ipxcp		PACKED;	/* 0E: */
	unsigned short tx_ipxcp		PACKED;	/* 10: */
	unsigned short rx_pap		PACKED;	/* 12: */
	unsigned short tx_pap		PACKED;	/* 14: */
	unsigned short rx_chap		PACKED;	/* 16: */
	unsigned short tx_chap		PACKED;	/* 18: */
	unsigned short rx_lqr		PACKED;	/* 1A: */
	unsigned short tx_lqr		PACKED;	/* 1C: */
	unsigned short rx_ip		PACKED;	/* 1E: */
	unsigned short tx_ip		PACKED;	/* 20: */
	unsigned short rx_ipx		PACKED;	/* 22: */
	unsigned short tx_ipx		PACKED;	/* 24: */
} ppp_pkt_stats_t;

/*----------------------------------------------------------------------------
 * LCP Statistics (returned by the PPP_READ_LCP_STATS command).
 */
typedef struct	ppp_lcp_stats
{
	unsigned short rx_unknown	PACKED;	/* 00: unknown LCP type */
	unsigned short rx_conf_rqst	PACKED;	/* 02: Configure-Request */
	unsigned short rx_conf_ack	PACKED;	/* 04: Configure-Ack */
	unsigned short rx_conf_nak	PACKED;	/* 06: Configure-Nak */
	unsigned short rx_conf_rej	PACKED;	/* 08: Configure-Reject */
	unsigned short rx_term_rqst	PACKED;	/* 0A: Terminate-Request */
	unsigned short rx_term_ack	PACKED;	/* 0C: Terminate-Ack */
	unsigned short rx_code_rej	PACKED;	/* 0E: Code-Reject */
	unsigned short rx_proto_rej	PACKED;	/* 10: Protocol-Reject */
	unsigned short rx_echo_rqst	PACKED;	/* 12: Echo-Request */
	unsigned short rx_echo_reply	PACKED;	/* 14: Echo-Reply */
	unsigned short rx_disc_rqst	PACKED;	/* 16: Discard-Request */
	unsigned short tx_conf_rqst	PACKED;	/* 18: Configure-Request */
	unsigned short tx_conf_ack	PACKED;	/* 1A: Configure-Ack */
	unsigned short tx_conf_nak	PACKED;	/* 1C: Configure-Nak */
	unsigned short tx_conf_rej	PACKED;	/* 1E: Configure-Reject */
	unsigned short tx_term_rqst	PACKED;	/* 20: Terminate-Request */
	unsigned short tx_term_ack	PACKED;	/* 22: Terminate-Ack */
	unsigned short tx_code_rej	PACKED;	/* 24: Code-Reject */
	unsigned short tx_proto_rej	PACKED;	/* 26: Protocol-Reject */
	unsigned short tx_echo_rqst	PACKED;	/* 28: Echo-Request */
	unsigned short tx_echo_reply	PACKED;	/* 2A: Echo-Reply */
	unsigned short tx_disc_rqst	PACKED;	/* 2E: Discard-Request */
	unsigned short rx_too_large	PACKED;	/* 30: packets too large */
	unsigned short rx_ack_inval	PACKED;	/* 32: invalid Conf-Ack */
	unsigned short rx_rej_inval	PACKED;	/* 34: invalid Conf-Reject */
	unsigned short rx_rej_badid	PACKED;	/* 36: Conf-Reject w/bad ID */
} ppp_lcp_stats_t;

/*----------------------------------------------------------------------------
 * Loopback Error Statistics (returned by the PPP_READ_LPBK_STATS command).
 */
typedef struct	ppp_lpbk_stats
{
	unsigned short conf_magic	PACKED;	/* 00:  */
	unsigned short loc_echo_rqst	PACKED;	/* 02:  */
	unsigned short rem_echo_rqst	PACKED;	/* 04:  */
	unsigned short loc_echo_reply	PACKED;	/* 06:  */
	unsigned short rem_echo_reply	PACKED;	/* 08:  */
	unsigned short loc_disc_rqst	PACKED;	/* 0A:  */
	unsigned short rem_disc_rqst	PACKED;	/* 0C:  */
	unsigned short echo_tx_collsn	PACKED;	/* 0E:  */
	unsigned short echo_rx_collsn	PACKED;	/* 10:  */
} ppp_lpbk_stats_t;

/*----------------------------------------------------------------------------
 * Protocol Statistics (returned by the PPP_READ_IPCP_STATS and
 * PPP_READ_IPXCP_STATS commands).
 */
typedef struct	ppp_prot_stats
{
	unsigned short rx_unknown	PACKED;	/* 00: unknown type */
	unsigned short rx_conf_rqst	PACKED;	/* 02: Configure-Request */
	unsigned short rx_conf_ack	PACKED;	/* 04: Configure-Ack */
	unsigned short rx_conf_nak	PACKED;	/* 06: Configure-Nak */
	unsigned short rx_conf_rej	PACKED;	/* 08: Configure-Reject */
	unsigned short rx_term_rqst	PACKED;	/* 0A: Terminate-Request */
	unsigned short rx_term_ack	PACKED;	/* 0C: Terminate-Ack */
	unsigned short rx_code_rej	PACKED;	/* 0E: Code-Reject */
	unsigned short reserved		PACKED;	/* 10: */
	unsigned short tx_conf_rqst	PACKED;	/* 12: Configure-Request */
	unsigned short tx_conf_ack	PACKED;	/* 14: Configure-Ack */
	unsigned short tx_conf_nak	PACKED;	/* 16: Configure-Nak */
	unsigned short tx_conf_rej	PACKED;	/* 18: Configure-Reject */
	unsigned short tx_term_rqst	PACKED;	/* 1A: Terminate-Request */
	unsigned short tx_term_ack	PACKED;	/* 1C: Terminate-Ack */
	unsigned short tx_code_rej	PACKED;	/* 1E: Code-Reject */
	unsigned short rx_too_large	PACKED;	/* 20: packets too large */
	unsigned short rx_ack_inval	PACKED;	/* 22: invalid Conf-Ack */
	unsigned short rx_rej_inval	PACKED;	/* 24: invalid Conf-Reject */
	unsigned short rx_rej_badid	PACKED;	/* 26: Conf-Reject w/bad ID */
} ppp_prot_stats_t;

/*----------------------------------------------------------------------------
 * PAP Statistics (returned by the PPP_READ_PAP_STATS command).
 */
typedef struct	ppp_pap_stats
{
	unsigned short rx_unknown	PACKED;	/* 00: unknown type */
	unsigned short rx_auth_rqst	PACKED;	/* 02: Authenticate-Request */
	unsigned short rx_auth_ack	PACKED;	/* 04: Authenticate-Ack */
	unsigned short rx_auth_nak	PACKED;	/* 06: Authenticate-Nak */
	unsigned short reserved		PACKED;	/* 08: */
	unsigned short tx_auth_rqst	PACKED;	/* 0A: Authenticate-Request */
	unsigned short tx_auth_ack	PACKED;	/* 0C: Authenticate-Ack */
	unsigned short tx_auth_nak	PACKED;	/* 0E: Authenticate-Nak */
	unsigned short rx_too_large	PACKED;	/* 10: packets too large */
	unsigned short rx_bad_peerid	PACKED;	/* 12: invalid peer ID */
	unsigned short rx_bad_passwd	PACKED;	/* 14: invalid password */
} ppp_pap_stats_t;

/*----------------------------------------------------------------------------
 * CHAP Statistics (returned by the PPP_READ_CHAP_STATS command).
 */
typedef struct	ppp_chap_stats
{
	unsigned short rx_unknown	PACKED;	/* 00: unknown type */
	unsigned short rx_challenge	PACKED;	/* 02: Authenticate-Request */
	unsigned short rx_response	PACKED;	/* 04: Authenticate-Ack */
	unsigned short rx_success	PACKED;	/* 06: Authenticate-Nak */
	unsigned short rx_failure	PACKED;	/* 08: Authenticate-Nak */
	unsigned short reserved		PACKED;	/* 0A: */
	unsigned short tx_challenge	PACKED;	/* 0C: Authenticate-Request */
	unsigned short tx_response	PACKED;	/* 0E: Authenticate-Ack */
	unsigned short tx_success	PACKED;	/* 10: Authenticate-Nak */
	unsigned short tx_failure	PACKED;	/* 12: Authenticate-Nak */
	unsigned short rx_too_large	PACKED;	/* 14: packets too large */
	unsigned short rx_bad_peerid	PACKED;	/* 16: invalid peer ID */
	unsigned short rx_bad_passwd	PACKED;	/* 18: invalid password */
	unsigned short rx_bad_md5	PACKED;	/* 1A: invalid MD5 format */
	unsigned short rx_bad_resp	PACKED;	/* 1C: invalid response */
} ppp_chap_stats_t;

/*----------------------------------------------------------------------------
 * Connection Information (returned by the PPP_GET_CONNECTION_INFO command).
 */
typedef struct	ppp_conn_info
{
	unsigned short remote_mru	PACKED;	/* 00:  */
	unsigned char  ip_options	PACKED;	/* 02:  */
	unsigned char  ip_local[4]	PACKED;	/* 03:  */
	unsigned char  ip_remote[4]	PACKED;	/* 07:  */
	unsigned char  ipx_options	PACKED;	/* 0B:  */
	unsigned char  ipx_network[4]	PACKED;	/* 0C:  */
	unsigned char  ipx_local[6]	PACKED;	/* 10:  */
	unsigned char  ipx_remote[6]	PACKED;	/* 16:  */
	unsigned char  ipx_router[48]	PACKED;	/* 1C:  */
	unsigned char  auth_status	PACKED;	/* 4C:  */
	unsigned char  peer_id[0]	PACKED;	/* 4D:  */
} ppp_conn_info_t;

/* Data structure for SET_TRIGGER_INTR command
 */

typedef struct ppp_intr_info{
	unsigned char  i_enable		PACKED; /* 0 Interrupt enable bits */
	unsigned char  irq              PACKED; /* 1 Irq number */
	unsigned short timer_len        PACKED; /* 2 Timer delay */
} ppp_intr_info_t;


#define FT1_MONITOR_STATUS_CTRL                         0x80
#define SET_FT1_MODE                                    0x81



/* Special UDP drivers management commands */
#define PPIPE_ENABLE_TRACING                            0x20
#define PPIPE_DISABLE_TRACING                           0x21
#define PPIPE_GET_TRACE_INFO                            0x22
#define PPIPE_GET_IBA_DATA                              0x23
#define PPIPE_KILL_BOARD     				0x24
#define PPIPE_FT1_READ_STATUS                           0x25
#define PPIPE_DRIVER_STAT_IFSEND                        0x26
#define PPIPE_DRIVER_STAT_INTR                          0x27
#define PPIPE_DRIVER_STAT_GEN                           0x28
#define PPIPE_FLUSH_DRIVER_STATS                        0x29
#define PPIPE_ROUTER_UP_TIME                            0x30

#define DISABLE_TRACING 				0x00
#define TRACE_SIGNALLING_FRAMES				0x01
#define TRACE_DATA_FRAMES				0x02



#ifdef		_MSC_
#  pragma	pack()
#endif
#endif	/* _SDLA_PPP_H */
