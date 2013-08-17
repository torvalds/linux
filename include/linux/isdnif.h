/* $Id: isdnif.h,v 1.43.2.2 2004/01/12 23:08:35 keil Exp $
 *
 * Linux ISDN subsystem
 * Definition of the interface between the subsystem and its low-level drivers.
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1995,96    Thinking Objects Software GmbH Wuerzburg
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef __ISDNIF_H__
#define __ISDNIF_H__


/*
 * Values for general protocol-selection
 */
#define ISDN_PTYPE_UNKNOWN   0   /* Protocol undefined   */
#define ISDN_PTYPE_1TR6      1   /* german 1TR6-protocol */
#define ISDN_PTYPE_EURO      2   /* EDSS1-protocol       */
#define ISDN_PTYPE_LEASED    3   /* for leased lines     */
#define ISDN_PTYPE_NI1       4   /* US NI-1 protocol     */
#define ISDN_PTYPE_MAX       7   /* Max. 8 Protocols     */

/*
 * Values for Layer-2-protocol-selection
 */
#define ISDN_PROTO_L2_X75I   0   /* X75/LAPB with I-Frames            */
#define ISDN_PROTO_L2_X75UI  1   /* X75/LAPB with UI-Frames           */
#define ISDN_PROTO_L2_X75BUI 2   /* X75/LAPB with UI-Frames           */
#define ISDN_PROTO_L2_HDLC   3   /* HDLC                              */
#define ISDN_PROTO_L2_TRANS  4   /* Transparent (Voice)               */
#define ISDN_PROTO_L2_X25DTE 5   /* X25/LAPB DTE mode                 */
#define ISDN_PROTO_L2_X25DCE 6   /* X25/LAPB DCE mode                 */
#define ISDN_PROTO_L2_V11096 7   /* V.110 bitrate adaption 9600 Baud  */
#define ISDN_PROTO_L2_V11019 8   /* V.110 bitrate adaption 19200 Baud */
#define ISDN_PROTO_L2_V11038 9   /* V.110 bitrate adaption 38400 Baud */
#define ISDN_PROTO_L2_MODEM  10  /* Analog Modem on Board */
#define ISDN_PROTO_L2_FAX    11  /* Fax Group 2/3         */
#define ISDN_PROTO_L2_HDLC_56K 12   /* HDLC 56k                          */
#define ISDN_PROTO_L2_MAX    15  /* Max. 16 Protocols                 */

/*
 * Values for Layer-3-protocol-selection
 */
#define ISDN_PROTO_L3_TRANS	0	/* Transparent */
#define ISDN_PROTO_L3_TRANSDSP	1	/* Transparent with DSP */
#define ISDN_PROTO_L3_FCLASS2	2	/* Fax Group 2/3 CLASS 2 */
#define ISDN_PROTO_L3_FCLASS1	3	/* Fax Group 2/3 CLASS 1 */
#define ISDN_PROTO_L3_MAX	7	/* Max. 8 Protocols */

#ifdef __KERNEL__

#include <linux/skbuff.h>

/***************************************************************************/
/* Extensions made by Werner Cornelius (werner@ikt.de)                     */
/*                                                                         */ 
/* The proceed command holds a incoming call in a state to leave processes */
/* enough time to check whether ist should be accepted.                    */
/* The PROT_IO Command extends the interface to make protocol dependent    */
/* features available (call diversion, call waiting...).                   */
/*                                                                         */ 
/* The PROT_IO Command is executed with the desired driver id and the arg  */
/* parameter coded as follows:                                             */
/* The lower 8 bits of arg contain the desired protocol from ISDN_PTYPE    */
/* definitions. The upper 24 bits represent the protocol specific cmd/stat.*/
/* Any additional data is protocol and command specific.                   */
/* This mechanism also applies to the statcallb callback STAT_PROT.        */    
/*                                                                         */
/* This suggested extension permits an easy expansion of protocol specific */
/* handling. Extensions may be added at any time without changing the HL   */
/* driver code and not getting conflicts without certifications.           */
/* The well known CAPI 2.0 interface handles such extensions in a similar  */
/* way. Perhaps a protocol specific module may be added and separately     */
/* loaded and linked to the basic isdn module for handling.                */                    
/***************************************************************************/

/*****************/
/* DSS1 commands */ 
/*****************/
#define DSS1_CMD_INVOKE       ((0x00 << 8) | ISDN_PTYPE_EURO)   /* invoke a supplementary service */
#define DSS1_CMD_INVOKE_ABORT ((0x01 << 8) | ISDN_PTYPE_EURO)   /* abort a invoke cmd */

/*******************************/
/* DSS1 Status callback values */
/*******************************/
#define DSS1_STAT_INVOKE_RES  ((0x80 << 8) | ISDN_PTYPE_EURO)   /* Result for invocation */
#define DSS1_STAT_INVOKE_ERR  ((0x81 << 8) | ISDN_PTYPE_EURO)   /* Error Return for invocation */
#define DSS1_STAT_INVOKE_BRD  ((0x82 << 8) | ISDN_PTYPE_EURO)   /* Deliver invoke broadcast info */


/*********************************************************************/
/* structures for DSS1 commands and callback                         */
/*                                                                   */
/* An action is invoked by sending a DSS1_CMD_INVOKE. The ll_id, proc*/
/* timeout, datalen and data fields must be set before calling.      */
/*                                                                   */
/* The return value is a positive hl_id value also delivered in the  */
/* hl_id field. A value of zero signals no more left hl_id capacitys.*/
/* A negative return value signals errors in LL. So if the return    */
/* value is <= 0 no action in LL will be taken -> request ignored    */
/*                                                                   */
/* The timeout field must be filled with a positive value specifying */
/* the amount of time the INVOKED process waits for a reaction from  */
/* the network.                                                      */
/* If a response (either error or result) is received during this    */
/* intervall, a reporting callback is initiated and the process will */
/* be deleted, the hl identifier will be freed.                      */
/* If no response is received during the specified intervall, a error*/
/* callback is initiated with timeout set to -1 and a datalen set    */
/* to 0.                                                             */
/* If timeout is set to a value <= 0 during INVOCATION the process is*/
/* immediately deleted after sending the data. No callback occurs !  */
/*                                                                   */
/* A currently waiting process may be aborted with INVOKE_ABORT. No  */
/* callback will occur when a process has been aborted.              */
/*                                                                   */
/* Broadcast invoke frames from the network are reported via the     */
/* STAT_INVOKE_BRD callback. The ll_id is set to 0, the other fields */
/* are supplied by the network and not by the HL.                    */   
/*********************************************************************/

/*****************/
/* NI1 commands */ 
/*****************/
#define NI1_CMD_INVOKE       ((0x00 << 8) | ISDN_PTYPE_NI1)   /* invoke a supplementary service */
#define NI1_CMD_INVOKE_ABORT ((0x01 << 8) | ISDN_PTYPE_NI1)   /* abort a invoke cmd */

/*******************************/
/* NI1 Status callback values */
/*******************************/
#define NI1_STAT_INVOKE_RES  ((0x80 << 8) | ISDN_PTYPE_NI1)   /* Result for invocation */
#define NI1_STAT_INVOKE_ERR  ((0x81 << 8) | ISDN_PTYPE_NI1)   /* Error Return for invocation */
#define NI1_STAT_INVOKE_BRD  ((0x82 << 8) | ISDN_PTYPE_NI1)   /* Deliver invoke broadcast info */

typedef struct
  { ulong ll_id; /* ID supplied by LL when executing    */
		 /* a command and returned by HL for    */
                 /* INVOKE_RES and INVOKE_ERR           */
    int hl_id;   /* ID supplied by HL when called       */
                 /* for executing a cmd and delivered   */
                 /* for results and errors              */
                 /* must be supplied by LL when aborting*/  
    int proc;    /* invoke procedure used by CMD_INVOKE */
                 /* returned by callback and broadcast  */ 
    int timeout; /* timeout for INVOKE CMD in ms        */
                 /* -1  in stat callback when timed out */
                 /* error value when error callback     */
    int datalen; /* length of cmd or stat data          */
    u_char *data;/* pointer to data delivered or send   */
  } isdn_cmd_stat;

/*
 * Commands from linklevel to lowlevel
 *
 */
#define ISDN_CMD_IOCTL    0       /* Perform ioctl                         */
#define ISDN_CMD_DIAL     1       /* Dial out                              */
#define ISDN_CMD_ACCEPTD  2       /* Accept an incoming call on D-Chan.    */
#define ISDN_CMD_ACCEPTB  3       /* Request B-Channel connect.            */
#define ISDN_CMD_HANGUP   4       /* Hangup                                */
#define ISDN_CMD_CLREAZ   5       /* Clear EAZ(s) of channel               */
#define ISDN_CMD_SETEAZ   6       /* Set EAZ(s) of channel                 */
#define ISDN_CMD_GETEAZ   7       /* Get EAZ(s) of channel                 */
#define ISDN_CMD_SETSIL   8       /* Set Service-Indicator-List of channel */
#define ISDN_CMD_GETSIL   9       /* Get Service-Indicator-List of channel */
#define ISDN_CMD_SETL2   10       /* Set B-Chan. Layer2-Parameter          */
#define ISDN_CMD_GETL2   11       /* Get B-Chan. Layer2-Parameter          */
#define ISDN_CMD_SETL3   12       /* Set B-Chan. Layer3-Parameter          */
#define ISDN_CMD_GETL3   13       /* Get B-Chan. Layer3-Parameter          */
// #define ISDN_CMD_LOCK    14       /* Signal usage by upper levels          */
// #define ISDN_CMD_UNLOCK  15       /* Release usage-lock                    */
#define ISDN_CMD_SUSPEND 16       /* Suspend connection                    */
#define ISDN_CMD_RESUME  17       /* Resume connection                     */
#define ISDN_CMD_PROCEED 18       /* Proceed with call establishment       */
#define ISDN_CMD_ALERT   19       /* Alert after Proceeding                */
#define ISDN_CMD_REDIR   20       /* Redir a incoming call                 */
#define ISDN_CMD_PROT_IO 21       /* Protocol specific commands            */
#define CAPI_PUT_MESSAGE 22       /* CAPI message send down or up          */
#define ISDN_CMD_FAXCMD  23       /* FAX commands to HL-driver             */
#define ISDN_CMD_AUDIO   24       /* DSP, DTMF, ... settings               */

/*
 * Status-Values delivered from lowlevel to linklevel via
 * statcallb().
 *
 */
#define ISDN_STAT_STAVAIL 256    /* Raw status-data available             */
#define ISDN_STAT_ICALL   257    /* Incoming call detected                */
#define ISDN_STAT_RUN     258    /* Signal protocol-code is running       */
#define ISDN_STAT_STOP    259    /* Signal halt of protocol-code          */
#define ISDN_STAT_DCONN   260    /* Signal D-Channel connect              */
#define ISDN_STAT_BCONN   261    /* Signal B-Channel connect              */
#define ISDN_STAT_DHUP    262    /* Signal D-Channel disconnect           */
#define ISDN_STAT_BHUP    263    /* Signal B-Channel disconnect           */
#define ISDN_STAT_CINF    264    /* Charge-Info                           */
#define ISDN_STAT_LOAD    265    /* Signal new lowlevel-driver is loaded  */
#define ISDN_STAT_UNLOAD  266    /* Signal unload of lowlevel-driver      */
#define ISDN_STAT_BSENT   267    /* Signal packet sent                    */
#define ISDN_STAT_NODCH   268    /* Signal no D-Channel                   */
#define ISDN_STAT_ADDCH   269    /* Add more Channels                     */
#define ISDN_STAT_CAUSE   270    /* Cause-Message                         */
#define ISDN_STAT_ICALLW  271    /* Incoming call without B-chan waiting  */
#define ISDN_STAT_REDIR   272    /* Redir result                          */
#define ISDN_STAT_PROT    273    /* protocol IO specific callback         */
#define ISDN_STAT_DISPLAY 274    /* deliver a received display message    */
#define ISDN_STAT_L1ERR   275    /* Signal Layer-1 Error                  */
#define ISDN_STAT_FAXIND  276    /* FAX indications from HL-driver        */
#define ISDN_STAT_AUDIO   277    /* DTMF, DSP indications                 */
#define ISDN_STAT_DISCH   278    /* Disable/Enable channel usage          */

/*
 * Audio commands
 */
#define ISDN_AUDIO_SETDD	0	/* Set DTMF detection           */
#define ISDN_AUDIO_DTMF		1	/* Rx/Tx DTMF                   */

/*
 * Values for errcode field
 */
#define ISDN_STAT_L1ERR_SEND 1
#define ISDN_STAT_L1ERR_RECV 2

/*
 * Values for feature-field of interface-struct.
 */
/* Layer 2 */
#define ISDN_FEATURE_L2_X75I    (0x0001 << ISDN_PROTO_L2_X75I)
#define ISDN_FEATURE_L2_X75UI   (0x0001 << ISDN_PROTO_L2_X75UI)
#define ISDN_FEATURE_L2_X75BUI  (0x0001 << ISDN_PROTO_L2_X75BUI)
#define ISDN_FEATURE_L2_HDLC    (0x0001 << ISDN_PROTO_L2_HDLC)
#define ISDN_FEATURE_L2_TRANS   (0x0001 << ISDN_PROTO_L2_TRANS)
#define ISDN_FEATURE_L2_X25DTE  (0x0001 << ISDN_PROTO_L2_X25DTE)
#define ISDN_FEATURE_L2_X25DCE  (0x0001 << ISDN_PROTO_L2_X25DCE)
#define ISDN_FEATURE_L2_V11096  (0x0001 << ISDN_PROTO_L2_V11096)
#define ISDN_FEATURE_L2_V11019  (0x0001 << ISDN_PROTO_L2_V11019)
#define ISDN_FEATURE_L2_V11038  (0x0001 << ISDN_PROTO_L2_V11038)
#define ISDN_FEATURE_L2_MODEM   (0x0001 << ISDN_PROTO_L2_MODEM)
#define ISDN_FEATURE_L2_FAX	(0x0001 << ISDN_PROTO_L2_FAX)
#define ISDN_FEATURE_L2_HDLC_56K (0x0001 << ISDN_PROTO_L2_HDLC_56K)

#define ISDN_FEATURE_L2_MASK    (0x0FFFF) /* Max. 16 protocols */
#define ISDN_FEATURE_L2_SHIFT   (0)

/* Layer 3 */
#define ISDN_FEATURE_L3_TRANS   (0x10000 << ISDN_PROTO_L3_TRANS)
#define ISDN_FEATURE_L3_TRANSDSP (0x10000 << ISDN_PROTO_L3_TRANSDSP)
#define ISDN_FEATURE_L3_FCLASS2	(0x10000 << ISDN_PROTO_L3_FCLASS2)
#define ISDN_FEATURE_L3_FCLASS1	(0x10000 << ISDN_PROTO_L3_FCLASS1)

#define ISDN_FEATURE_L3_MASK    (0x0FF0000) /* Max. 8 Protocols */
#define ISDN_FEATURE_L3_SHIFT   (16)

/* Signaling */
#define ISDN_FEATURE_P_UNKNOWN  (0x1000000 << ISDN_PTYPE_UNKNOWN)
#define ISDN_FEATURE_P_1TR6     (0x1000000 << ISDN_PTYPE_1TR6)
#define ISDN_FEATURE_P_EURO     (0x1000000 << ISDN_PTYPE_EURO)
#define ISDN_FEATURE_P_NI1      (0x1000000 << ISDN_PTYPE_NI1)

#define ISDN_FEATURE_P_MASK     (0x0FF000000) /* Max. 8 Protocols */
#define ISDN_FEATURE_P_SHIFT    (24)

typedef struct setup_parm {
    unsigned char phone[32];	/* Remote Phone-Number */
    unsigned char eazmsn[32];	/* Local EAZ or MSN    */
    unsigned char si1;      /* Service Indicator 1 */
    unsigned char si2;      /* Service Indicator 2 */
    unsigned char plan;     /* Numbering plan      */
    unsigned char screen;   /* Screening info      */
} setup_parm;


#ifdef CONFIG_ISDN_TTY_FAX
/* T.30 Fax G3 */

#define FAXIDLEN 21

typedef struct T30_s {
	/* session parameters */
	__u8 resolution;
	__u8 rate;
	__u8 width;
	__u8 length;
	__u8 compression;
	__u8 ecm;
	__u8 binary;
	__u8 scantime;
	__u8 id[FAXIDLEN];
	/* additional parameters */
	__u8 phase;
	__u8 direction;
	__u8 code;
	__u8 badlin;
	__u8 badmul;
	__u8 bor;
	__u8 fet;
	__u8 pollid[FAXIDLEN];
	__u8 cq;
	__u8 cr;
	__u8 ctcrty;
	__u8 minsp;
	__u8 phcto;
	__u8 rel;
	__u8 nbc;
	/* remote station parameters */
	__u8 r_resolution;
	__u8 r_rate;
	__u8 r_width;
	__u8 r_length;
	__u8 r_compression;
	__u8 r_ecm;
	__u8 r_binary;
	__u8 r_scantime;
	__u8 r_id[FAXIDLEN];
	__u8 r_code;
} __packed T30_s;

#define ISDN_TTY_FAX_CONN_IN	0
#define ISDN_TTY_FAX_CONN_OUT	1

#define ISDN_TTY_FAX_FCON	0
#define ISDN_TTY_FAX_DIS 	1
#define ISDN_TTY_FAX_FTT 	2
#define ISDN_TTY_FAX_MCF 	3
#define ISDN_TTY_FAX_DCS 	4
#define ISDN_TTY_FAX_TRAIN_OK	5
#define ISDN_TTY_FAX_EOP 	6
#define ISDN_TTY_FAX_EOM 	7
#define ISDN_TTY_FAX_MPS 	8
#define ISDN_TTY_FAX_DTC 	9
#define ISDN_TTY_FAX_RID 	10
#define ISDN_TTY_FAX_HNG 	11
#define ISDN_TTY_FAX_DT  	12
#define ISDN_TTY_FAX_FCON_I	13
#define ISDN_TTY_FAX_DR  	14
#define ISDN_TTY_FAX_ET  	15
#define ISDN_TTY_FAX_CFR 	16
#define ISDN_TTY_FAX_PTS 	17
#define ISDN_TTY_FAX_SENT	18

#define ISDN_FAX_PHASE_IDLE	0
#define ISDN_FAX_PHASE_A	1
#define ISDN_FAX_PHASE_B   	2
#define ISDN_FAX_PHASE_C   	3
#define ISDN_FAX_PHASE_D   	4
#define ISDN_FAX_PHASE_E   	5

#endif /* TTY_FAX */

#define ISDN_FAX_CLASS1_FAE	0
#define ISDN_FAX_CLASS1_FTS	1
#define ISDN_FAX_CLASS1_FRS	2
#define ISDN_FAX_CLASS1_FTM	3
#define ISDN_FAX_CLASS1_FRM	4
#define ISDN_FAX_CLASS1_FTH	5
#define ISDN_FAX_CLASS1_FRH	6
#define ISDN_FAX_CLASS1_CTRL	7

#define ISDN_FAX_CLASS1_OK	0
#define ISDN_FAX_CLASS1_CONNECT	1
#define ISDN_FAX_CLASS1_NOCARR	2
#define ISDN_FAX_CLASS1_ERROR	3
#define ISDN_FAX_CLASS1_FCERROR	4
#define ISDN_FAX_CLASS1_QUERY	5

typedef struct {
	__u8	cmd;
	__u8	subcmd;
	__u8	para[50];
} aux_s;

#define AT_COMMAND	0
#define AT_EQ_VALUE	1
#define AT_QUERY	2
#define AT_EQ_QUERY	3

/* CAPI structs */

/* this is compatible to the old union size */
#define MAX_CAPI_PARA_LEN 50

typedef struct {
	/* Header */
	__u16 Length;
	__u16 ApplId;
	__u8 Command;
	__u8 Subcommand;
	__u16 Messagenumber;

	/* Parameter */
	union {
		__u32 Controller;
		__u32 PLCI;
		__u32 NCCI;
	} adr;
	__u8 para[MAX_CAPI_PARA_LEN];
} capi_msg;

/*
 * Structure for exchanging above infos
 *
 */
typedef struct {
	int   driver;		/* Lowlevel-Driver-ID            */
	int   command;		/* Command or Status (see above) */
	ulong arg;		/* Additional Data               */
	union {
		ulong errcode;	/* Type of error with STAT_L1ERR	*/
		int length;	/* Amount of bytes sent with STAT_BSENT	*/
		u_char num[50];	/* Additional Data			*/
		setup_parm setup;/* For SETUP msg			*/
		capi_msg cmsg;	/* For CAPI like messages		*/
		char display[85];/* display message data		*/ 
		isdn_cmd_stat isdn_io; /* ISDN IO-parameter/result	*/
		aux_s aux;	/* for modem commands/indications	*/
#ifdef CONFIG_ISDN_TTY_FAX
		T30_s	*fax;	/* Pointer to ttys fax struct		*/
#endif
		ulong userdata;	/* User Data */
	} parm;
} isdn_ctrl;

#define dss1_io    isdn_io
#define ni1_io     isdn_io

/*
 * The interface-struct itself (initialized at load-time of lowlevel-driver)
 *
 * See Documentation/isdn/INTERFACE for a description, how the communication
 * between the ISDN subsystem and its drivers is done.
 *
 */
typedef struct {
  struct module *owner;

  /* Number of channels supported by this driver
   */
  int channels;

  /* 
   * Maximum Size of transmit/receive-buffer this driver supports.
   */
  int maxbufsize;

  /* Feature-Flags for this driver.
   * See defines ISDN_FEATURE_... for Values
   */
  unsigned long features;

  /*
   * Needed for calculating
   * dev->hard_header_len = linklayer header + hl_hdrlen;
   * Drivers, not supporting sk_buff's should set this to 0.
   */
  unsigned short hl_hdrlen;

  /*
   * Receive-Callback using sk_buff's
   * Parameters:
   *             int                    Driver-ID
   *             int                    local channel-number (0 ...)
   *             struct sk_buff *skb    received Data
   */
  void (*rcvcallb_skb)(int, int, struct sk_buff *);

  /* Status-Callback
   * Parameters:
   *             isdn_ctrl*
   *                   driver  = Driver ID.
   *                   command = One of above ISDN_STAT_... constants.
   *                   arg     = depending on status-type.
   *                   num     = depending on status-type.
   */
  int (*statcallb)(isdn_ctrl*);

  /* Send command
   * Parameters:
   *             isdn_ctrl*
   *                   driver  = Driver ID.
   *                   command = One of above ISDN_CMD_... constants.
   *                   arg     = depending on command.
   *                   num     = depending on command.
   */
  int (*command)(isdn_ctrl*);

  /*
   * Send data using sk_buff's
   * Parameters:
   *             int                    driverId
   *             int                    local channel-number (0...)
   *             int                    Flag: Need ACK for this packet.
   *             struct sk_buff *skb    Data to send
   */
  int (*writebuf_skb) (int, int, int, struct sk_buff *);

  /* Send raw D-Channel-Commands
   * Parameters:
   *             u_char pointer data
   *             int    length of data
   *             int    driverId
   *             int    local channel-number (0 ...)
   */
  int (*writecmd)(const u_char __user *, int, int, int);

  /* Read raw Status replies
   *             u_char pointer data (volatile)
   *             int    length of buffer
   *             int    driverId
   *             int    local channel-number (0 ...)
   */
  int (*readstat)(u_char __user *, int, int, int);

  char id[20];
} isdn_if;

/*
 * Function which must be called by lowlevel-driver at loadtime with
 * the following fields of above struct set:
 *
 * channels     Number of channels that will be supported.
 * hl_hdrlen    Space to preserve in sk_buff's when sending. Drivers, not
 *              supporting sk_buff's should set this to 0.
 * command      Address of Command-Handler.
 * features     Bitwise coded Features of this driver. (use ISDN_FEATURE_...)
 * writebuf_skb Address of Skbuff-Send-Handler.
 * writecmd        "    "  D-Channel  " which accepts raw D-Ch-Commands.
 * readstat        "    "  D-Channel  " which delivers raw Status-Data.
 *
 * The linklevel-driver fills the following fields:
 *
 * channels      Driver-ID assigned to this driver. (Must be used on all
 *               subsequent callbacks.
 * rcvcallb_skb  Address of handler for received Skbuff's.
 * statcallb        "    "     "    for status-changes.
 *
 */
extern int register_isdn(isdn_if*);
#include <asm/uaccess.h>

#endif /* __KERNEL__ */

#endif /* __ISDNIF_H__ */
