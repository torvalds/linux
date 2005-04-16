/*************************************************************************
 sdla_chdlc.h	Sangoma Cisco HDLC firmware API definitions

 Author:      	Gideon Hack
		Nenad Corbic <ncorbic@sangoma.com>	

 Copyright:	(c) 1995-2000 Sangoma Technologies Inc.

		This program is free software; you can redistribute it and/or
		modify it under the term of the GNU General Public License
		as published by the Free Software Foundation; either version
		2 of the License, or (at your option) any later version.

===========================================================================
  Oct 04, 1999  Nenad Corbic    Updated API support
  Jun 02, 1999  Gideon Hack     Changes for S514 usage.
  Oct 28, 1998	Jaspreet Singh	Made changes for Dual Port CHDLC.
  Jun 11, 1998	David Fong	Initial version.
===========================================================================

 Organization
	- Compatibility notes
	- Constants defining the shared memory control block (mailbox)
	- Interface commands
	- Return code from interface commands
	- Constants for the commands (structures for casting data)
	- UDP Management constants and structures

*************************************************************************/

#ifndef _SDLA_CHDLC_H
#  define _SDLC_CHDLC_H

/*------------------------------------------------------------------------
   Notes:

	All structres defined in this file are byte-aligned.  

	Compiler	Platform
	------------------------
	GNU C		Linux

------------------------------------------------------------------------*/

#ifndef	PACKED
#define	PACKED __attribute__((packed))
#endif	/* PACKED */


/* ----------------------------------------------------------------------------
 *        Constants defining the shared memory control block (mailbox)
 * --------------------------------------------------------------------------*/

#define PRI_BASE_ADDR_MB_STRUCT 	0xE000 	/* the base address of the mailbox structure on the adapter */
#define SEC_BASE_ADDR_MB_STRUCT 	0xE800 	/* the base address of the mailbox structure on the adapter */
#define SIZEOF_MB_DATA_BFR		2032	/* the size of the actual mailbox data area */
#define NUMBER_MB_RESERVED_BYTES	0x0B	/* the number of reserved bytes in the mailbox header area */


#define MIN_LGTH_CHDLC_DATA_CFG  	300 	/* min length of the CHDLC data field (for configuration purposes) */
#define PRI_MAX_NO_DATA_BYTES_IN_FRAME  15354 /* PRIMARY - max length of the CHDLC data field */

typedef struct {
	unsigned char opp_flag PACKED;			/* the opp flag */
	unsigned char command PACKED;			/* the user command */
	unsigned short buffer_length PACKED;		/* the data length */
  	unsigned char return_code PACKED;		/* the return code */
	unsigned char MB_reserved[NUMBER_MB_RESERVED_BYTES] PACKED;	/* reserved for later */
	unsigned char data[SIZEOF_MB_DATA_BFR] PACKED;	/* the data area */
} CHDLC_MAILBOX_STRUCT;

typedef struct {
        pid_t                   pid_num PACKED;
        CHDLC_MAILBOX_STRUCT     cmdarea PACKED;

} CMDBLOCK_STRUCT;




/* ----------------------------------------------------------------------------
 *                        Interface commands
 * --------------------------------------------------------------------------*/

/* global interface commands */
#define READ_GLOBAL_EXCEPTION_CONDITION	0x01
#define SET_GLOBAL_CONFIGURATION	0x02
#define READ_GLOBAL_CONFIGURATION	0x03
#define READ_GLOBAL_STATISTICS		0x04
#define FLUSH_GLOBAL_STATISTICS		0x05
#define SET_MODEM_STATUS		0x06	/* set status of DTR or RTS */
#define READ_MODEM_STATUS		0x07	/* read status of CTS and DCD */
#define READ_COMMS_ERROR_STATS		0x08	
#define FLUSH_COMMS_ERROR_STATS		0x09
#define SET_TRACE_CONFIGURATION		0x0A	/* set the line trace config */
#define READ_TRACE_CONFIGURATION	0x0B	/* read the line trace config */
#define READ_TRACE_STATISTICS		0x0C	/* read the trace statistics */
#define FLUSH_TRACE_STATISTICS		0x0D	/* flush the trace statistics */
#define FT1_MONITOR_STATUS_CTRL		0x1C	/* set the status of the S508/FT1 monitoring */
#define SET_FT1_CONFIGURATION		0x18	/* set the FT1 configuration */
#define READ_FT1_CONFIGURATION		0x19	/* read the FT1 configuration */
#define TRANSMIT_ASYNC_DATA_TO_FT1	0x1A	/* output asynchronous data to the FT1 */
#define RECEIVE_ASYNC_DATA_FROM_FT1	0x1B	/* receive asynchronous data from the FT1 */
#define FT1_MONITOR_STATUS_CTRL		0x1C	/* set the status of the FT1 monitoring */

#define READ_FT1_OPERATIONAL_STATS	0x1D	/* read the S508/FT1 operational statistics */
#define SET_FT1_MODE			0x1E	/* set the operational mode of the S508/FT1 module */

/* CHDLC-level interface commands */
#define READ_CHDLC_CODE_VERSION		0x20	
#define READ_CHDLC_EXCEPTION_CONDITION	0x21	/* read exception condition from the adapter */
#define SET_CHDLC_CONFIGURATION		0x22
#define READ_CHDLC_CONFIGURATION	0x23
#define ENABLE_CHDLC_COMMUNICATIONS	0x24
#define DISABLE_CHDLC_COMMUNICATIONS	0x25
#define READ_CHDLC_LINK_STATUS		0x26
#define READ_CHDLC_OPERATIONAL_STATS	0x27
#define FLUSH_CHDLC_OPERATIONAL_STATS	0x28
#define SET_CHDLC_INTERRUPT_TRIGGERS	0x30	/* set application interrupt triggers */
#define READ_CHDLC_INTERRUPT_TRIGGERS	0x31	/* read application interrupt trigger configuration */

/* Special UDP drivers management commands */
#define CPIPE_ENABLE_TRACING				0x50
#define CPIPE_DISABLE_TRACING				0x51
#define CPIPE_GET_TRACE_INFO				0x52
#define CPIPE_GET_IBA_DATA				0x53
#define CPIPE_FT1_READ_STATUS				0x54
#define CPIPE_DRIVER_STAT_IFSEND			0x55
#define CPIPE_DRIVER_STAT_INTR				0x56
#define CPIPE_DRIVER_STAT_GEN				0x57
#define CPIPE_FLUSH_DRIVER_STATS			0x58
#define CPIPE_ROUTER_UP_TIME				0x59

/* Driver specific commands for API */
#define	CHDLC_READ_TRACE_DATA		0xE4	/* read trace data */
#define TRACE_ALL                       0x00
#define TRACE_PROT			0x01
#define TRACE_DATA			0x02

#define DISCARD_RX_ERROR_FRAMES	0x0001

/* ----------------------------------------------------------------------------
 *                     Return codes from interface commands
 * --------------------------------------------------------------------------*/

#define COMMAND_OK				0x00

/* return codes from global interface commands */
#define NO_GLOBAL_EXCEP_COND_TO_REPORT		0x01	/* there is no CHDLC exception condition to report */
#define LGTH_GLOBAL_CFG_DATA_INVALID		0x01	/* the length of the passed global configuration data is invalid */
#define LGTH_TRACE_CFG_DATA_INVALID		0x01	/* the length of the passed trace configuration data is invalid */
#define IRQ_TIMEOUT_VALUE_INVALID		0x02	/* an invalid application IRQ timeout value was selected */
#define TRACE_CONFIG_INVALID			0x02	/* the passed line trace configuration is invalid */
#define ADAPTER_OPERATING_FREQ_INVALID		0x03	/* an invalid adapter operating frequency was selected */
#define TRC_DEAC_TMR_INVALID			0x03	/* the trace deactivation timer is invalid */
#define S508_FT1_ADPTR_NOT_PRESENT		0x0C	/* the S508/FT1 adapter is not present */
#define INVALID_FT1_STATUS_SELECTION            0x0D    /* the S508/FT1 status selection is invalid */
#define FT1_OP_STATS_NOT_ENABLED		0x0D    /* the FT1 operational statistics have not been enabled */
#define FT1_OP_STATS_NOT_AVAILABLE		0x0E    /* the FT1 operational statistics are not currently available */
#define S508_FT1_MODE_SELECTION_BUSY		0x0E	/* the S508/FT1 adapter is busy selecting the operational mode */

/* return codes from command READ_GLOBAL_EXCEPTION_CONDITION */
#define EXCEP_MODEM_STATUS_CHANGE		0x10		/* a modem status change occurred */
#define EXCEP_TRC_DISABLED			0x11		/* the trace has been disabled */
#define EXCEP_IRQ_TIMEOUT			0x12		/* IRQ timeout */

/* return codes from CHDLC-level interface commands */
#define NO_CHDLC_EXCEP_COND_TO_REPORT		0x21	/* there is no CHDLC exception condition to report */
#define CHDLC_COMMS_DISABLED			0x21	/* communications are not currently enabled */
#define CHDLC_COMMS_ENABLED			0x21	/* communications are currently enabled */
#define DISABLE_CHDLC_COMMS_BEFORE_CFG		0x21	/* CHDLC communications must be disabled before setting the configuration */
#define ENABLE_CHDLC_COMMS_BEFORE_CONN		0x21	/* communications must be enabled before using the CHDLC_CONNECT conmmand */
#define CHDLC_CFG_BEFORE_COMMS_ENABLED		0x22	/* perform a SET_CHDLC_CONFIGURATION before enabling comms */
#define LGTH_CHDLC_CFG_DATA_INVALID 		0x22	/* the length of the passed CHDLC configuration data is invalid */
#define LGTH_INT_TRIGGERS_DATA_INVALID		0x22	/* the length of the passed interrupt trigger data is invalid */
#define INVALID_IRQ_SELECTED			0x23	/* in invalid IRQ was selected in the SET_CHDLC_INTERRUPT_TRIGGERS */
#define INVALID_CHDLC_CFG_DATA			0x23	/* the passed CHDLC configuration data is invalid */
#define IRQ_TMR_VALUE_INVALID			0x24	/* an invalid application IRQ timer value was selected */
#define LARGER_PERCENT_TX_BFR_REQUIRED		0x24	/* a larger Tx buffer percentage is required */
#define LARGER_PERCENT_RX_BFR_REQUIRED		0x25	/* a larger Rx buffer percentage is required */
#define S514_BOTH_PORTS_SAME_CLK_MODE		0x26	/* S514 - both ports must have same clock mode */
#define INVALID_CMND_HDLC_STREAM_MODE           0x4E    /* the CHDLC interface command is invalid for HDLC streaming mode */
#define INVALID_CHDLC_COMMAND			0x4F	/* the defined CHDLC interface command is invalid */

/* return codes from command READ_CHDLC_EXCEPTION_CONDITION */
#define EXCEP_LINK_ACTIVE			0x30	/* the CHDLC link has become active */
#define EXCEP_LINK_INACTIVE_MODEM		0x31	/* the CHDLC link has become inactive (modem status) */
#define EXCEP_LINK_INACTIVE_KPALV		0x32	/* the CHDLC link has become inactive (keepalive status) */
#define EXCEP_IP_ADDRESS_DISCOVERED		0x33	/* the IP address has been discovered */
#define EXCEP_LOOPBACK_CONDITION		0x34	/* a loopback condition has occurred */


/* return code from command CHDLC_SEND_WAIT and CHDLC_SEND_NO_WAIT */
#define LINK_DISCONNECTED			0x21
#define NO_TX_BFRS_AVAIL			0x24


/* ----------------------------------------------------------------------------
 * Constants for the SET_GLOBAL_CONFIGURATION/READ_GLOBAL_CONFIGURATION commands
 * --------------------------------------------------------------------------*/

/* the global configuration structure */
typedef struct {
	unsigned short adapter_config_options PACKED;	/* adapter config options */
	unsigned short app_IRQ_timeout PACKED;		/* application IRQ timeout */
	unsigned long adapter_operating_frequency PACKED;	/* adapter operating frequency */
} GLOBAL_CONFIGURATION_STRUCT;

/* settings for the 'app_IRQ_timeout' */
#define MAX_APP_IRQ_TIMEOUT_VALUE	5000	/* the maximum permitted IRQ timeout */



/* ----------------------------------------------------------------------------
 *             Constants for the READ_GLOBAL_STATISTICS command
 * --------------------------------------------------------------------------*/

/* the global statistics structure */
typedef struct {
	unsigned short app_IRQ_timeout_count PACKED;
} GLOBAL_STATS_STRUCT;



/* ----------------------------------------------------------------------------
 *             Constants for the READ_COMMS_ERROR_STATS command
 * --------------------------------------------------------------------------*/

/* the communications error statistics structure */
typedef struct {
	unsigned short Rx_overrun_err_count PACKED;
	unsigned short CRC_err_count PACKED;	/* receiver CRC error count */
	unsigned short Rx_abort_count PACKED; 	/* abort frames recvd count */
	unsigned short Rx_dis_pri_bfrs_full_count PACKED;/* receiver disabled */
	unsigned short comms_err_stat_reserved_1 PACKED;/* reserved for later */
	unsigned short sec_Tx_abort_msd_Tx_int_count PACKED; /* secondary - abort frames transmitted count (missed Tx interrupt) */
	unsigned short missed_Tx_und_int_count PACKED;	/* missed tx underrun interrupt count */
        unsigned short sec_Tx_abort_count PACKED;   /*secondary-abort frames tx count */
	unsigned short DCD_state_change_count PACKED; /* DCD state change */
	unsigned short CTS_state_change_count PACKED; /* CTS state change */
} COMMS_ERROR_STATS_STRUCT;



/* ----------------------------------------------------------------------------
 *                  Constants used for line tracing
 * --------------------------------------------------------------------------*/

/* the trace configuration structure (SET_TRACE_CONFIGURATION/READ_TRACE_CONFIGURATION commands) */
typedef struct {
	unsigned char trace_config PACKED;		/* trace configuration */
	unsigned short trace_deactivation_timer PACKED;	/* trace deactivation timer */
	unsigned long ptr_trace_stat_el_cfg_struct PACKED;	/* a pointer to the line trace element configuration structure */
} LINE_TRACE_CONFIG_STRUCT;

/* 'trace_config' bit settings */
#define TRACE_INACTIVE		0x00	/* trace is inactive */
#define TRACE_ACTIVE		0x01	/* trace is active */
#define TRACE_DELAY_MODE	0x04	/* operate the trace in delay mode */
#define TRACE_DATA_FRAMES	0x08	/* trace Data frames */
#define TRACE_SLARP_FRAMES	0x10	/* trace SLARP frames */
#define TRACE_CDP_FRAMES	0x20	/* trace CDP frames */

/* the line trace status element configuration structure */
typedef struct {
	unsigned short number_trace_status_elements PACKED;	/* number of line trace elements */
	unsigned long base_addr_trace_status_elements PACKED;	/* base address of the trace element list */
	unsigned long next_trace_element_to_use PACKED;	/* pointer to the next trace element to be used */
	unsigned long base_addr_trace_buffer PACKED;		/* base address of the trace data buffer */
	unsigned long end_addr_trace_buffer PACKED;		/* end address of the trace data buffer */
} TRACE_STATUS_EL_CFG_STRUCT;

/* the line trace status element structure */
typedef struct {
	unsigned char opp_flag PACKED;			/* opp flag */
	unsigned short trace_length PACKED;		/* trace length */
	unsigned char trace_type PACKED;		/* trace type */
	unsigned short trace_time_stamp PACKED;	/* time stamp */
	unsigned short trace_reserved_1 PACKED;	/* reserved for later use */
	unsigned long trace_reserved_2 PACKED;		/* reserved for later use */
	unsigned long ptr_data_bfr PACKED;		/* ptr to the trace data buffer */
} TRACE_STATUS_ELEMENT_STRUCT;

/* "trace_type" bit settings */
#define TRACE_INCOMING 			0x00
#define TRACE_OUTGOINGING 		0x01
#define TRACE_INCOMING_ABORTED 		0x10
#define TRACE_INCOMING_CRC_ERROR 	0x20
#define TRACE_INCOMING_OVERRUN_ERROR 	0x40



/* the line trace statistics structure */
typedef struct {
	unsigned long frames_traced_count PACKED;	/* number of frames traced */
	unsigned long trc_frms_not_recorded_count PACKED;	/* number of trace frames discarded */
} LINE_TRACE_STATS_STRUCT;


/* ----------------------------------------------------------------------------
 *               Constants for the FT1_MONITOR_STATUS_CTRL command
 * --------------------------------------------------------------------------*/

#define DISABLE_FT1_STATUS_STATISTICS	0x00    /* disable the FT1 status and statistics monitoring */
#define ENABLE_READ_FT1_STATUS		0x01    /* read the FT1 operational status */
#define ENABLE_READ_FT1_OP_STATS	0x02    /* read the FT1 operational statistics */
#define FLUSH_FT1_OP_STATS		0x04 	/* flush the FT1 operational statistics */




/* ----------------------------------------------------------------------------
 *               Constants for the SET_CHDLC_CONFIGURATION command
 * --------------------------------------------------------------------------*/

/* the CHDLC configuration structure */
typedef struct {
	unsigned long baud_rate PACKED;		/* the baud rate */	
	unsigned short line_config_options PACKED;	/* line configuration options */
	unsigned short modem_config_options PACKED;	/* modem configration options */
	unsigned short modem_status_timer PACKED;	/* timer for monitoring modem status changes */
	unsigned short CHDLC_API_options PACKED;	/* CHDLC API options */
	unsigned short CHDLC_protocol_options PACKED;	/* CHDLC protocol options */
	unsigned short percent_data_buffer_for_Tx PACKED;	/* percentage data buffering used for Tx */
	unsigned short CHDLC_statistics_options PACKED;	/* CHDLC operational statistics options */
	unsigned short max_CHDLC_data_field_length PACKED;	/* the maximum length of the CHDLC Data field */
	unsigned short transmit_keepalive_timer PACKED;		/* the transmit keepalive timer */
	unsigned short receive_keepalive_timer PACKED;		/* the receive keepalive timer */
	unsigned short keepalive_error_tolerance PACKED;	/* the receive keepalive error tolerance */
	unsigned short SLARP_request_timer PACKED;		/* the SLARP request timer */
	unsigned long IP_address PACKED;			/* the IP address */
	unsigned long IP_netmask PACKED;			/* the IP netmask */
	unsigned long ptr_shared_mem_info_struct PACKED;	/* a pointer to the shared memory area information structure */
	unsigned long ptr_CHDLC_Tx_stat_el_cfg_struct PACKED;	/* a pointer to the transmit status element configuration structure */
	unsigned long ptr_CHDLC_Rx_stat_el_cfg_struct PACKED;	/* a pointer to the receive status element configuration structure */
} CHDLC_CONFIGURATION_STRUCT;

/* settings for the 'line_config_options' */
#define INTERFACE_LEVEL_V35					0x0000 /* V.35 interface level */
#define INTERFACE_LEVEL_RS232					0x0001 /* RS-232 interface level */

/* settings for the 'modem_config_options' */

#define DONT_RAISE_DTR_RTS_ON_EN_COMMS		0x0001
/* don't automatically raise DTR and RTS when performing an
   ENABLE_CHDLC_COMMUNICATIONS command */

#define DONT_REPORT_CHG_IN_MODEM_STAT 		0x0002
/* don't report changes in modem status to the application */


/* bit settings for the 'CHDLC_protocol_options' byte */

#define IGNORE_DCD_FOR_LINK_STAT		0x0001
/* ignore DCD in determining the CHDLC link status */

#define IGNORE_CTS_FOR_LINK_STAT		0x0002
/* ignore CTS in determining the CHDLC link status */

#define IGNORE_KPALV_FOR_LINK_STAT		0x0004
/* ignore keepalive frames in determining the CHDLC link status */ 

#define SINGLE_TX_BUFFER			0x4000 
/* configure a single transmit buffer */

#define HDLC_STREAMING_MODE			0x8000

/*   settings for the 'CHDLC_statistics_options' */

#define CHDLC_TX_DATA_BYTE_COUNT_STAT		0x0001
/* record the number of Data bytes transmitted */

#define CHDLC_RX_DATA_BYTE_COUNT_STAT		0x0002
/* record the number of Data bytes received */

#define CHDLC_TX_THROUGHPUT_STAT		0x0004
/* compute the Data frame transmit throughput */

#define CHDLC_RX_THROUGHPUT_STAT		0x0008
/* compute the Data frame receive throughput */


/* permitted minimum and maximum values for setting the CHDLC configuration */
#define PRI_MAX_BAUD_RATE_S508	2666666 /* PRIMARY   - maximum baud rate (S508) */
#define SEC_MAX_BAUD_RATE_S508	258064 	/* SECONDARY - maximum baud rate (S508) */
#define PRI_MAX_BAUD_RATE_S514  2750000 /* PRIMARY   - maximum baud rate (S508) */
#define SEC_MAX_BAUD_RATE_S514  515625  /* SECONDARY - maximum baud rate (S508) */
 
#define MIN_MODEM_TIMER	0			/* minimum modem status timer */
#define MAX_MODEM_TIMER	5000			/* maximum modem status timer */

#define SEC_MAX_NO_DATA_BYTES_IN_FRAME  2048 /* SECONDARY - max length of the CHDLC data field */

#define MIN_Tx_KPALV_TIMER	0	  /* minimum transmit keepalive timer */
#define MAX_Tx_KPALV_TIMER	60000	  /* maximum transmit keepalive timer */
#define DEFAULT_Tx_KPALV_TIMER	10000	  /* default transmit keepalive timer */

#define MIN_Rx_KPALV_TIMER	10	  /* minimum receive keepalive timer */
#define MAX_Rx_KPALV_TIMER	60000	  /* maximum receive keepalive timer */
#define DEFAULT_Rx_KPALV_TIMER	10000	  /* default receive keepalive timer */

#define MIN_KPALV_ERR_TOL	1	  /* min kpalv error tolerance count */
#define MAX_KPALV_ERR_TOL	20	  /* max kpalv error tolerance count */
#define DEFAULT_KPALV_ERR_TOL	3	  /* default value */

#define MIN_SLARP_REQ_TIMER	0	  /* min transmit SLARP Request timer */
#define MAX_SLARP_REQ_TIMER	60000	  /* max transmit SLARP Request timer */
#define DEFAULT_SLARP_REQ_TIMER	0	  /* default value -- no SLARP */



/* ----------------------------------------------------------------------------
 *             Constants for the READ_CHDLC_LINK_STATUS command
 * --------------------------------------------------------------------------*/

/* the CHDLC status structure */
typedef struct {
	unsigned char CHDLC_link_status PACKED;	/* CHDLC link status */
	unsigned char no_Data_frms_for_app PACKED;	/* number of Data frames available for the application */
	unsigned char receiver_status PACKED;	/* enabled/disabled */
	unsigned char SLARP_state PACKED;	/* internal SLARP state */
} CHDLC_LINK_STATUS_STRUCT;

/* settings for the 'CHDLC_link_status' variable */
#define CHDLC_LINK_INACTIVE		0x00	/* the CHDLC link is inactive */
#define CHDLC_LINK_ACTIVE		0x01	/* the CHDLC link is active */



/* ----------------------------------------------------------------------------
 *           Constants for the READ_CHDLC_OPERATIONAL_STATS command
 * --------------------------------------------------------------------------*/

/* the CHDLC operational statistics structure */
typedef struct {

	/* Data frame transmission statistics */
	unsigned long Data_frames_Tx_count PACKED;	/* # of frames transmitted */
	unsigned long Data_bytes_Tx_count PACKED; 	/* # of bytes transmitted */
	unsigned long Data_Tx_throughput PACKED;	/* transmit throughput */
	unsigned long no_ms_for_Data_Tx_thruput_comp PACKED;	/* millisecond time used for the Tx throughput computation */
	unsigned long Tx_Data_discard_lgth_err_count PACKED;	/* number of Data frames discarded (length error) */
	unsigned long reserved_Data_frm_Tx_stat1 PACKED;	/* reserved for later */
	unsigned long reserved_Data_frm_Tx_stat2 PACKED;	/* reserved for later */
	unsigned long reserved_Data_frm_Tx_stat3 PACKED;	/* reserved for later */

	/* Data frame reception statistics */
	unsigned long Data_frames_Rx_count PACKED;	/* number of frames received */
	unsigned long Data_bytes_Rx_count PACKED;	/* number of bytes received */
	unsigned long Data_Rx_throughput PACKED;	/* receive throughput */
	unsigned long no_ms_for_Data_Rx_thruput_comp PACKED;	/* millisecond time used for the Rx throughput computation */
	unsigned long Rx_Data_discard_short_count PACKED;	/* received Data frames discarded (too short) */
	unsigned long Rx_Data_discard_long_count PACKED;	/* received Data frames discarded (too long) */
	unsigned long Rx_Data_discard_inactive_count PACKED;	/* received Data frames discarded (link inactive) */
	unsigned long reserved_Data_frm_Rx_stat1 PACKED;	/* reserved for later */

	/* SLARP frame transmission/reception statistics */
	unsigned long CHDLC_SLARP_REQ_Tx_count PACKED;		/* number of SLARP Request frames transmitted */
	unsigned long CHDLC_SLARP_REQ_Rx_count PACKED;		/* number of SLARP Request frames received */
	unsigned long CHDLC_SLARP_REPLY_Tx_count PACKED;	/* number of SLARP Reply frames transmitted */
	unsigned long CHDLC_SLARP_REPLY_Rx_count PACKED;	/* number of SLARP Reply frames received */
	unsigned long CHDLC_SLARP_KPALV_Tx_count PACKED;	/* number of SLARP keepalive frames transmitted */
	unsigned long CHDLC_SLARP_KPALV_Rx_count PACKED;	/* number of SLARP keepalive frames received */
	unsigned long reserved_SLARP_stat1 PACKED;		/* reserved for later */
	unsigned long reserved_SLARP_stat2 PACKED;		/* reserved for later */

	/* CDP frame transmission/reception statistics */
	unsigned long CHDLC_CDP_Tx_count PACKED;		/* number of CDP frames transmitted */
	unsigned long CHDLC_CDP_Rx_count PACKED;		/* number of CDP frames received */
	unsigned long reserved_CDP_stat1 PACKED;		/* reserved for later */
	unsigned long reserved_CDP_stat2 PACKED;		/* reserved for later */
	unsigned long reserved_CDP_stat3 PACKED;		/* reserved for later */
	unsigned long reserved_CDP_stat4 PACKED;		/* reserved for later */
	unsigned long reserved_CDP_stat5 PACKED;		/* reserved for later */
	unsigned long reserved_CDP_stat6 PACKED;		/* reserved for later */

	/* Incoming frames with a format error statistics */
	unsigned short Rx_frm_incomp_CHDLC_hdr_count PACKED;	/* frames received of with incomplete Cisco HDLC header */
	unsigned short Rx_frms_too_long_count PACKED;		/* frames received of excessive length count */
	unsigned short Rx_invalid_CHDLC_addr_count PACKED;	/* frames received with an invalid CHDLC address count */
	unsigned short Rx_invalid_CHDLC_ctrl_count PACKED;	/* frames received with an invalid CHDLC control field count */
	unsigned short Rx_invalid_CHDLC_type_count PACKED;	/* frames received of an invalid CHDLC frame type count */
	unsigned short Rx_SLARP_invalid_code_count PACKED;	/* SLARP frame received with an invalid packet code */
	unsigned short Rx_SLARP_Reply_bad_IP_addr PACKED;	/* SLARP Reply received - bad IP address */
	unsigned short Rx_SLARP_Reply_bad_netmask PACKED;	/* SLARP Reply received - bad netmask */
	unsigned long reserved_frm_format_err1 PACKED;		/* reserved for later */
	unsigned long reserved_frm_format_err2 PACKED;		/* reserved for later */
	unsigned long reserved_frm_format_err3 PACKED;		/* reserved for later */
	unsigned long reserved_frm_format_err4 PACKED;		/* reserved for later */

	/* CHDLC timeout/retry statistics */
	unsigned short SLARP_Rx_keepalive_TO_count PACKED;	/* timeout count for incoming SLARP frames */
	unsigned short SLARP_Request_TO_count PACKED;		/* timeout count for SLARP Request frames */
	unsigned long To_retry_reserved_stat1 PACKED;		/* reserved for later */
	unsigned long To_retry_reserved_stat2 PACKED;		/* reserved for later */
	unsigned long To_retry_reserved_stat3 PACKED;		/* reserved for later */

	/* CHDLC link active/inactive and loopback statistics */
	unsigned short link_active_count PACKED;		/* number of times that the link went active */
	unsigned short link_inactive_modem_count PACKED;	/* number of times that the link went inactive (modem failure) */
	unsigned short link_inactive_keepalive_count PACKED;	/* number of times that the link went inactive (keepalive failure) */
	unsigned short link_looped_count PACKED;		/* link looped count */
	unsigned long link_status_reserved_stat1 PACKED;	/* reserved for later use */
	unsigned long link_status_reserved_stat2 PACKED;	/* reserved for later use */

	/* miscellaneous statistics */
	unsigned long reserved_misc_stat1 PACKED;		/* reserved for later */
	unsigned long reserved_misc_stat2 PACKED;		/* reserved for later */
	unsigned long reserved_misc_stat3 PACKED;		/* reserved for later */
	unsigned long reserved_misc_stat4 PACKED;		/* reserved for later */

} CHDLC_OPERATIONAL_STATS_STRUCT;



/* ----------------------------------------------------------------------------
 *                 Constants for using application interrupts
 * --------------------------------------------------------------------------*/

/* the structure used for the SET_CHDLC_INTERRUPT_TRIGGERS/READ_CHDLC_INTERRUPT_TRIGGERS command */
typedef struct {
	unsigned char CHDLC_interrupt_triggers PACKED;	/* CHDLC interrupt trigger configuration */
	unsigned char IRQ PACKED;			/* IRQ to be used */
	unsigned short interrupt_timer PACKED;		/* interrupt timer */
	unsigned short misc_interrupt_bits PACKED;	/* miscellaneous bits */
} CHDLC_INT_TRIGGERS_STRUCT;

/* 'CHDLC_interrupt_triggers' bit settings */
#define APP_INT_ON_RX_FRAME		0x01	/* interrupt on Data frame reception */
#define APP_INT_ON_TX_FRAME		0x02	/* interrupt when an Data frame may be transmitted */
#define APP_INT_ON_COMMAND_COMPLETE	0x04	/* interrupt when an interface command is complete */
#define APP_INT_ON_TIMER		0x08	/* interrupt on a defined millisecond timeout */
#define APP_INT_ON_GLOBAL_EXCEP_COND 	0x10	/* interrupt on a global exception condition */
#define APP_INT_ON_CHDLC_EXCEP_COND	0x20	/* interrupt on an CHDLC exception condition */
#define APP_INT_ON_TRACE_DATA_AVAIL	0x80	/* interrupt when trace data is available */

/* interrupt types indicated at 'interrupt_type' byte of the INTERRUPT_INFORMATION_STRUCT */
#define NO_APP_INTS_PEND		0x00	/* no interrups are pending */
#define RX_APP_INT_PEND			0x01	/* a receive interrupt is pending */
#define TX_APP_INT_PEND			0x02	/* a transmit interrupt is pending */
#define COMMAND_COMPLETE_APP_INT_PEND	0x04	/* a 'command complete' interrupt is pending */
#define TIMER_APP_INT_PEND		0x08	/* a timer interrupt is pending */
#define GLOBAL_EXCEP_COND_APP_INT_PEND 	0x10	/* a global exception condition interrupt is pending */
#define CHDLC_EXCEP_COND_APP_INT_PEND 	0x20	/* an CHDLC exception condition interrupt is pending */
#define TRACE_DATA_AVAIL_APP_INT_PEND	0x80	/* a trace data available interrupt is pending */


/* modem status changes */
#define DCD_HIGH			0x08
#define CTS_HIGH			0x20


/* ----------------------------------------------------------------------------
 *                   Constants for Data frame transmission
 * --------------------------------------------------------------------------*/

/* the Data frame transmit status element configuration structure */
typedef struct {
	unsigned short number_Tx_status_elements PACKED;	/* number of transmit status elements */
	unsigned long base_addr_Tx_status_elements PACKED;	/* base address of the transmit element list */
	unsigned long next_Tx_status_element_to_use PACKED;	/* pointer to the next transmit element to be used */
} CHDLC_TX_STATUS_EL_CFG_STRUCT;

/* the Data frame transmit status element structure */
typedef struct {
	unsigned char opp_flag PACKED;		/* opp flag */
	unsigned short frame_length PACKED;	/* length of the frame to be transmitted */
	unsigned char reserved_1 PACKED;	/* reserved for internal use */
	unsigned long reserved_2 PACKED;	/* reserved for internal use */
	unsigned long reserved_3 PACKED;	/* reserved for internal use */
	unsigned long ptr_data_bfr PACKED;	/* pointer to the data area */
} CHDLC_DATA_TX_STATUS_EL_STRUCT;



/* ----------------------------------------------------------------------------
 *                   Constants for Data frame reception
 * --------------------------------------------------------------------------*/

/* the Data frame receive status element configuration structure */
typedef struct {
	unsigned short number_Rx_status_elements PACKED;	/* number of receive status elements */
	unsigned long base_addr_Rx_status_elements PACKED;	/* base address of the receive element list */
	unsigned long next_Rx_status_element_to_use PACKED;	/* pointer to the next receive element to be used */
	unsigned long base_addr_Rx_buffer PACKED;		/* base address of the receive data buffer */
	unsigned long end_addr_Rx_buffer PACKED;		/* end address of the receive data buffer */
} CHDLC_RX_STATUS_EL_CFG_STRUCT;

/* the Data frame receive status element structure */
typedef struct {
	unsigned char opp_flag PACKED;		/* opp flag */
	unsigned short frame_length PACKED;   /* length of the received frame */
        unsigned char error_flag PACKED; /* frame errors (HDLC_STREAMING_MODE)*/
        unsigned short time_stamp PACKED; /* receive time stamp (HDLC_STREAMING_MODE) */
        unsigned long reserved_1 PACKED; 	/* reserved for internal use */
        unsigned short reserved_2 PACKED; 	/* reserved for internal use */
        unsigned long ptr_data_bfr PACKED;	/* pointer to the data area */
} CHDLC_DATA_RX_STATUS_EL_STRUCT;



/* ----------------------------------------------------------------------------
 *         Constants defining the shared memory information area
 * --------------------------------------------------------------------------*/

/* the global information structure */
typedef struct {
 	unsigned char global_status PACKED;		/* global status */
 	unsigned char modem_status PACKED;		/* current modem status */
 	unsigned char global_excep_conditions PACKED;	/* global exception conditions */
	unsigned char glob_info_reserved[5] PACKED;	/* reserved */
	unsigned char codename[4] PACKED;		/* Firmware name */
	unsigned char codeversion[4] PACKED;		/* Firmware version */
} GLOBAL_INFORMATION_STRUCT;

/* the CHDLC information structure */
typedef struct {
	unsigned char CHDLC_status PACKED;		/* CHDLC status */
 	unsigned char CHDLC_excep_conditions PACKED;	/* CHDLC exception conditions */
	unsigned char CHDLC_info_reserved[14] PACKED;	/* reserved */
} CHDLC_INFORMATION_STRUCT;

/* the interrupt information structure */
typedef struct {
 	unsigned char interrupt_type PACKED;		/* type of interrupt triggered */
 	unsigned char interrupt_permission PACKED;	/* interrupt permission mask */
	unsigned char int_info_reserved[14] PACKED;	/* reserved */
} INTERRUPT_INFORMATION_STRUCT;

/* the S508/FT1 information structure */
typedef struct {
 	unsigned char parallel_port_A_input PACKED;	/* input - parallel port A */
 	unsigned char parallel_port_B_input PACKED;	/* input - parallel port B */
	unsigned char FT1_info_reserved[14] PACKED;	/* reserved */
} FT1_INFORMATION_STRUCT;

/* the shared memory area information structure */
typedef struct {
	GLOBAL_INFORMATION_STRUCT global_info_struct PACKED;		/* the global information structure */
	CHDLC_INFORMATION_STRUCT CHDLC_info_struct PACKED;		/* the CHDLC information structure */
	INTERRUPT_INFORMATION_STRUCT interrupt_info_struct PACKED;	/* the interrupt information structure */
	FT1_INFORMATION_STRUCT FT1_info_struct PACKED;			/* the S508/FT1 information structure */
} SHARED_MEMORY_INFO_STRUCT;

/* ----------------------------------------------------------------------------
 *        UDP Management constants and structures 
 * --------------------------------------------------------------------------*/

/* The embedded control block for UDP mgmt 
   This is essentially a mailbox structure, without the large data field */

typedef struct {
        unsigned char  opp_flag PACKED;                  /* the opp flag */
        unsigned char  command PACKED;                   /* the user command */
        unsigned short buffer_length PACKED;             /* the data length */
        unsigned char  return_code PACKED;               /* the return code */
	unsigned char  MB_reserved[NUMBER_MB_RESERVED_BYTES] PACKED;	/* reserved for later */
} cblock_t;


/* UDP management packet layout (data area of ip packet) */
/*
typedef struct {
	unsigned char		signature[8]	PACKED;
	unsigned char		request_reply	PACKED;
	unsigned char		id		PACKED;
	unsigned char		reserved[6]	PACKED;
	cblock_t		cblock		PACKED;
	unsigned char		num_frames	PACKED;
	unsigned char		ismoredata	PACKED;
	unsigned char 		data[SIZEOF_MB_DATA_BFR] 	PACKED;
} udp_management_packet_t;

*/

typedef struct {
	unsigned char		num_frames	PACKED;
	unsigned char		ismoredata	PACKED;
} trace_info_t;

typedef struct {
	ip_pkt_t 		ip_pkt		PACKED;
	udp_pkt_t		udp_pkt		PACKED;
	wp_mgmt_t		wp_mgmt		PACKED;
	cblock_t                cblock          PACKED;
	trace_info_t       	trace_info      PACKED;
	unsigned char           data[SIZEOF_MB_DATA_BFR]      PACKED;
} chdlc_udp_pkt_t;

typedef struct ft1_exec_cmd{
	unsigned char  command PACKED;                   /* the user command */
        unsigned short buffer_length PACKED;             /* the data length */
        unsigned char  return_code PACKED;               /* the return code */
	unsigned char  MB_reserved[NUMBER_MB_RESERVED_BYTES] PACKED;
} ft1_exec_cmd_t;

typedef struct {
	unsigned char  opp_flag 			PACKED;
	ft1_exec_cmd_t cmd				PACKED;
	unsigned char  data[SIZEOF_MB_DATA_BFR]      	PACKED;
} ft1_exec_t;

#define UDPMGMT_SIGNATURE	"CTPIPEAB"


/* UDP/IP packet (for UDP management) layout */
/*
typedef struct {
	unsigned char	reserved[2]	PACKED;
	unsigned short	ip_length	PACKED;
	unsigned char	reserved2[4]	PACKED;
	unsigned char	ip_ttl		PACKED;
	unsigned char	ip_protocol	PACKED;
	unsigned short	ip_checksum	PACKED;
	unsigned long	ip_src_address	PACKED;
	unsigned long	ip_dst_address	PACKED;
	unsigned short	udp_src_port	PACKED;
	unsigned short	udp_dst_port	PACKED;
	unsigned short	udp_length	PACKED;
	unsigned short	udp_checksum	PACKED;
	udp_management_packet_t um_packet PACKED;
} ip_packet_t;
*/

/* valid ip_protocol for UDP management */
#define UDPMGMT_UDP_PROTOCOL 0x11


typedef struct {
	unsigned char	status		PACKED;
	unsigned char	data_avail	PACKED;
	unsigned short	real_length	PACKED;
	unsigned short	time_stamp	PACKED;
	unsigned char	data[1]		PACKED;
} trace_pkt_t;

typedef struct {
	unsigned char	error_flag	PACKED;
	unsigned short	time_stamp	PACKED;
	unsigned char	reserved[13]	PACKED;
} api_rx_hdr_t;

typedef struct {
        api_rx_hdr_t	api_rx_hdr      PACKED;
        void *   	data    	PACKED;
} api_rx_element_t;

typedef struct {
	unsigned char 	attr		PACKED;
	unsigned char  	reserved[15]	PACKED;
} api_tx_hdr_t;

typedef struct {
	api_tx_hdr_t 	api_tx_hdr	PACKED;
	void *		data		PACKED;
} api_tx_element_t;

/* ----------------------------------------------------------------------------
 *   Constants for the SET_FT1_CONFIGURATION/READ_FT1_CONFIGURATION command
 * --------------------------------------------------------------------------*/

/* the FT1 configuration structure */
typedef struct {
	unsigned short framing_mode;
	unsigned short encoding_mode;
	unsigned short line_build_out;
	unsigned short channel_base;
	unsigned short baud_rate_kbps;					/* the baud rate (in kbps) */	
	unsigned short clock_mode;
} ft1_config_t;

/* settings for the 'framing_mode' */
#define ESF_FRAMING 	0x00	/* ESF framing */
#define D4_FRAMING  	0x01	/* D4 framing */

/* settings for the 'encoding_mode' */
#define B8ZS_ENCODING 	0x00	/* B8ZS encoding */
#define AMI_ENCODING	0x01	/* AMI encoding */

/* settings for the 'line_build_out' */
#define LN_BLD_CSU_0dB_DSX1_0_to_133	0x00	/* set build out to CSU (0db) or DSX-1 (0-133ft) */
#define LN_BLD_DSX1_133_to_266		0x01	/* set build out DSX-1 (133-266ft) */
#define LN_BLD_DSX1_266_to_399		0x02	/* set build out DSX-1 (266-399ft) */
#define LN_BLD_DSX1_399_to_533		0x03	/* set build out DSX-1 (399-533ft) */
#define LN_BLD_DSX1_533_to_655		0x04	/* set build out DSX-1 (533-655ft) */
#define LN_BLD_CSU_NEG_7dB		0x05	/* set build out to CSU (-7.5db) */
#define LN_BLD_CSU_NEG_15dB		0x06	/* set build out to CSU (-15db) */
#define LN_BLD_CSU_NEG_22dB		0x07	/* set build out to CSU (-22.5db) */

/* settings for the 'channel_base' */
#define MIN_CHANNEL_BASE_VALUE		1		/* the minimum permitted channel base value */
#define MAX_CHANNEL_BASE_VALUE		24		/* the maximum permitted channel base value */

/* settings for the 'baud_rate_kbps' */
#define MIN_BAUD_RATE_KBPS		0		/* the minimum permitted baud rate (kbps) */
#define MAX_BAUD_RATE_KBPS 		1536	/* the maximum permitted baud rate (kbps) */
#define BAUD_RATE_FT1_AUTO_CONFIG	0xFFFF /* the baud rate used to trigger an automatic FT1 configuration */

/* settings for the 'clock_mode' */
#define CLOCK_MODE_NORMAL		0x00	/* clock mode set to normal (slave) */
#define CLOCK_MODE_MASTER		0x01	/* clock mode set to master */


#define BAUD_RATE_FT1_AUTO_CONFIG   	0xFFFF
#define AUTO_FT1_CONFIG_NOT_COMPLETE	0x08
#define AUTO_FT1_CFG_FAIL_OP_MODE	0x0C
#define AUTO_FT1_CFG_FAIL_INVALID_LINE 	0x0D

 
#ifdef		_MSC_
#  pragma	pack()
#endif
#endif	/* _SDLA_CHDLC_H */
