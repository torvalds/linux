/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 *  Copyright (c) 2009-2018 Alexander Motin <mav@FreeBSD.org>
 *  Copyright (c) 1997-2009 by Matthew Jacob
 *  All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 * 
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 
 *  THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 * 
 */

/*
 * Mailbox and Queue Entry Definitions for for Qlogic ISP SCSI adapters.
 */
#ifndef	_ISPMBOX_H
#define	_ISPMBOX_H

/*
 * Mailbox Command Opcodes
 */
#define MBOX_NO_OP			0x0000
#define MBOX_LOAD_RAM			0x0001
#define MBOX_EXEC_FIRMWARE		0x0002
#define MBOX_DUMP_RAM			0x0003
#define MBOX_WRITE_RAM_WORD		0x0004
#define MBOX_READ_RAM_WORD		0x0005
#define MBOX_MAILBOX_REG_TEST		0x0006
#define MBOX_VERIFY_CHECKSUM		0x0007
#define MBOX_ABOUT_FIRMWARE		0x0008
#define	MBOX_LOAD_RISC_RAM_2100		0x0009
					/*   a */
#define	MBOX_LOAD_RISC_RAM		0x000b
#define	MBOX_DUMP_RISC_RAM		0x000c
#define MBOX_WRITE_RAM_WORD_EXTENDED	0x000d
#define MBOX_CHECK_FIRMWARE		0x000e
#define	MBOX_READ_RAM_WORD_EXTENDED	0x000f
#define MBOX_INIT_REQ_QUEUE		0x0010
#define MBOX_INIT_RES_QUEUE		0x0011
#define MBOX_EXECUTE_IOCB		0x0012
#define MBOX_WAKE_UP			0x0013
#define MBOX_STOP_FIRMWARE		0x0014
#define MBOX_ABORT			0x0015
#define MBOX_ABORT_DEVICE		0x0016
#define MBOX_ABORT_TARGET		0x0017
#define MBOX_BUS_RESET			0x0018
#define MBOX_STOP_QUEUE			0x0019
#define MBOX_START_QUEUE		0x001a
#define MBOX_SINGLE_STEP_QUEUE		0x001b
#define MBOX_ABORT_QUEUE		0x001c
#define MBOX_GET_DEV_QUEUE_STATUS	0x001d
					/*  1e */
#define MBOX_GET_FIRMWARE_STATUS	0x001f
#define MBOX_GET_INIT_SCSI_ID		0x0020
#define MBOX_GET_SELECT_TIMEOUT		0x0021
#define MBOX_GET_RETRY_COUNT		0x0022
#define MBOX_GET_TAG_AGE_LIMIT		0x0023
#define MBOX_GET_CLOCK_RATE		0x0024
#define MBOX_GET_ACT_NEG_STATE		0x0025
#define MBOX_GET_ASYNC_DATA_SETUP_TIME	0x0026
#define MBOX_GET_SBUS_PARAMS		0x0027
#define		MBOX_GET_PCI_PARAMS	MBOX_GET_SBUS_PARAMS
#define MBOX_GET_TARGET_PARAMS		0x0028
#define MBOX_GET_DEV_QUEUE_PARAMS	0x0029
#define	MBOX_GET_RESET_DELAY_PARAMS	0x002a
					/*  2b */
					/*  2c */
					/*  2d */
					/*  2e */
					/*  2f */
#define MBOX_SET_INIT_SCSI_ID		0x0030
#define MBOX_SET_SELECT_TIMEOUT		0x0031
#define MBOX_SET_RETRY_COUNT		0x0032
#define MBOX_SET_TAG_AGE_LIMIT		0x0033
#define MBOX_SET_CLOCK_RATE		0x0034
#define MBOX_SET_ACT_NEG_STATE		0x0035
#define MBOX_SET_ASYNC_DATA_SETUP_TIME	0x0036
#define MBOX_SET_SBUS_CONTROL_PARAMS	0x0037
#define		MBOX_SET_PCI_PARAMETERS	0x0037
#define MBOX_SET_TARGET_PARAMS		0x0038
#define MBOX_SET_DEV_QUEUE_PARAMS	0x0039
#define	MBOX_SET_RESET_DELAY_PARAMS	0x003a
					/*  3b */
					/*  3c */
					/*  3d */
					/*  3e */
					/*  3f */
#define	MBOX_RETURN_BIOS_BLOCK_ADDR	0x0040
#define	MBOX_WRITE_FOUR_RAM_WORDS	0x0041
#define	MBOX_EXEC_BIOS_IOCB		0x0042
#define	MBOX_SET_FW_FEATURES		0x004a
#define	MBOX_GET_FW_FEATURES		0x004b
#define		FW_FEATURE_FAST_POST	0x1
#define		FW_FEATURE_LVD_NOTIFY	0x2
#define		FW_FEATURE_RIO_32BIT	0x4
#define		FW_FEATURE_RIO_16BIT	0x8

#define	MBOX_INIT_REQ_QUEUE_A64		0x0052
#define	MBOX_INIT_RES_QUEUE_A64		0x0053

#define	MBOX_ENABLE_TARGET_MODE		0x0055
#define		ENABLE_TARGET_FLAG	0x8000
#define		ENABLE_TQING_FLAG	0x0004
#define		ENABLE_MANDATORY_DISC	0x0002
#define	MBOX_GET_TARGET_STATUS		0x0056

/* These are for the ISP2X00 FC cards */
#define	MBOX_LOAD_FLASH_FIRMWARE	0x0003
#define	MBOX_WRITE_FC_SERDES_REG	0x0003	/* FC only */
#define	MBOX_READ_FC_SERDES_REG		0x0004	/* FC only */
#define	MBOX_GET_IO_STATUS		0x0012
#define	MBOX_SET_TRANSMIT_PARAMS	0x0019
#define	MBOX_SET_PORT_PARAMS		0x001a
#define	MBOX_LOAD_OP_FW_PARAMS		0x001b
#define	MBOX_INIT_MULTIPLE_QUEUE	0x001f
#define	MBOX_GET_LOOP_ID		0x0020
/* for 24XX cards, outgoing mailbox 7 has these values for F or FL topologies */
#define		ISP24XX_INORDER		0x0100
#define		ISP24XX_NPIV_SAN	0x0400
#define		ISP24XX_VSAN_SAN	0x1000
#define		ISP24XX_FC_SP_SAN	0x2000
#define	MBOX_GET_TIMEOUT_PARAMS		0x0022
#define	MBOX_GET_FIRMWARE_OPTIONS	0x0028
#define	MBOX_GENERATE_SYSTEM_ERROR	0x002a
#define	MBOX_WRITE_SFP			0x0030
#define	MBOX_READ_SFP			0x0031
#define	MBOX_SET_TIMEOUT_PARAMS		0x0032
#define	MBOX_SET_FIRMWARE_OPTIONS	0x0038
#define	MBOX_GET_SET_FC_LED_CONF	0x003b
#define	MBOX_RESTART_NIC_FIRMWARE	0x003d	/* FCoE only */
#define	MBOX_ACCESS_CONTROL		0x003e
#define	MBOX_LOOP_PORT_BYPASS		0x0040	/* FC only */
#define	MBOX_LOOP_PORT_ENABLE		0x0041	/* FC only */
#define	MBOX_GET_RESOURCE_COUNT		0x0042
#define	MBOX_REQUEST_OFFLINE_MODE	0x0043
#define	MBOX_DIAGNOSTIC_ECHO_TEST	0x0044
#define	MBOX_DIAGNOSTIC_LOOPBACK	0x0045
#define	MBOX_ENHANCED_GET_PDB		0x0047
#define	MBOX_INIT_FIRMWARE_MULTI_ID	0x0048	/* 2400 only */
#define	MBOX_GET_VP_DATABASE		0x0049	/* 2400 only */
#define	MBOX_GET_VP_DATABASE_ENTRY	0x004a	/* 2400 only */
#define	MBOX_GET_FCF_LIST		0x0050	/* FCoE only */
#define	MBOX_GET_DCBX_PARAMETERS	0x0051	/* FCoE only */
#define	MBOX_HOST_MEMORY_COPY		0x0053
#define	MBOX_EXEC_COMMAND_IOCB_A64	0x0054
#define	MBOX_SEND_RNID			0x0057
#define	MBOX_SET_PARAMETERS		0x0059
#define	MBOX_GET_PARAMETERS		0x005a
#define	MBOX_DRIVER_HEARTBEAT		0x005B	/* FC only */
#define	MBOX_FW_HEARTBEAT		0x005C
#define	MBOX_GET_SET_DATA_RATE		0x005D	/* >=23XX only */
#define		MBGSD_GET_RATE		0
#define		MBGSD_SET_RATE		1
#define		MBGSD_SET_RATE_NOW	2	/* 24XX only */
#define		MBGSD_1GB	0x00
#define		MBGSD_2GB	0x01
#define		MBGSD_AUTO	0x02
#define		MBGSD_4GB	0x03		/* 24XX only */
#define		MBGSD_8GB	0x04		/* 25XX only */
#define		MBGSD_16GB	0x05		/* 26XX only */
#define		MBGSD_32GB	0x06		/* 27XX only */
#define		MBGSD_10GB	0x13		/* 26XX only */
#define	MBOX_SEND_RNFT			0x005e
#define	MBOX_INIT_FIRMWARE		0x0060
#define	MBOX_GET_INIT_CONTROL_BLOCK	0x0061
#define	MBOX_INIT_LIP			0x0062
#define	MBOX_GET_FC_AL_POSITION_MAP	0x0063
#define	MBOX_GET_PORT_DB		0x0064
#define	MBOX_CLEAR_ACA			0x0065
#define	MBOX_TARGET_RESET		0x0066
#define	MBOX_CLEAR_TASK_SET		0x0067
#define	MBOX_ABORT_TASK_SET		0x0068
#define	MBOX_GET_FW_STATE		0x0069
#define	MBOX_GET_PORT_NAME		0x006A
#define	MBOX_GET_LINK_STATUS		0x006B
#define	MBOX_INIT_LIP_RESET		0x006C
#define	MBOX_GET_LINK_STAT_PR_DATA_CNT	0x006D
#define	MBOX_SEND_SNS			0x006E
#define	MBOX_FABRIC_LOGIN		0x006F
#define	MBOX_SEND_CHANGE_REQUEST	0x0070
#define	MBOX_FABRIC_LOGOUT		0x0071
#define	MBOX_INIT_LIP_LOGIN		0x0072
#define	MBOX_GET_PORT_NODE_NAME_LIST	0x0075
#define	MBOX_SET_VENDOR_ID		0x0076
#define	MBOX_GET_XGMAC_STATS		0x007a
#define	MBOX_GET_ID_LIST		0x007C
#define	MBOX_SEND_LFA			0x007d
#define	MBOX_LUN_RESET			0x007E

#define	ISP2100_SET_PCI_PARAM		0x00ff

#define	MBOX_BUSY			0x04

/*
 * Mailbox Command Complete Status Codes
 */
#define	MBOX_COMMAND_COMPLETE		0x4000
#define	MBOX_INVALID_COMMAND		0x4001
#define	MBOX_HOST_INTERFACE_ERROR	0x4002
#define	MBOX_TEST_FAILED		0x4003
#define	MBOX_COMMAND_ERROR		0x4005
#define	MBOX_COMMAND_PARAM_ERROR	0x4006
#define	MBOX_PORT_ID_USED		0x4007
#define	MBOX_LOOP_ID_USED		0x4008
#define	MBOX_ALL_IDS_USED		0x4009
#define	MBOX_NOT_LOGGED_IN		0x400A
#define	MBOX_LINK_DOWN_ERROR		0x400B
#define	MBOX_LOOPBACK_ERROR		0x400C
#define	MBOX_CHECKSUM_ERROR		0x4010
#define	MBOX_INVALID_PRODUCT_KEY	0x4020
/* pseudo mailbox completion codes */
#define	MBOX_REGS_BUSY			0x6000	/* registers in use */
#define	MBOX_TIMEOUT			0x6001	/* command timed out */

#define	MBLOGALL			0xffffffff
#define	MBLOGNONE			0x00000000
#define	MBLOGMASK(x)			(1 << (((x) - 1) & 0x1f))

/*
 * Asynchronous event status codes
 */
#define	ASYNC_BUS_RESET			0x8001
#define	ASYNC_SYSTEM_ERROR		0x8002
#define	ASYNC_RQS_XFER_ERR		0x8003
#define	ASYNC_RSP_XFER_ERR		0x8004
#define	ASYNC_QWAKEUP			0x8005
#define	ASYNC_TIMEOUT_RESET		0x8006
#define	ASYNC_DEVICE_RESET		0x8007
#define	ASYNC_EXTMSG_UNDERRUN		0x800A
#define	ASYNC_SCAM_INT			0x800B
#define	ASYNC_HUNG_SCSI			0x800C
#define	ASYNC_KILLED_BUS		0x800D
#define	ASYNC_BUS_TRANSIT		0x800E	/* LVD -> HVD, eg. */
#define	ASYNC_LIP_OCCURRED		0x8010	/* FC only */
#define	ASYNC_LOOP_UP			0x8011
#define	ASYNC_LOOP_DOWN			0x8012
#define	ASYNC_LOOP_RESET		0x8013	/* FC only */
#define	ASYNC_PDB_CHANGED		0x8014
#define	ASYNC_CHANGE_NOTIFY		0x8015
#define	ASYNC_LIP_NOS_OLS_RECV		0x8016	/* FC only */
#define	ASYNC_LIP_ERROR			0x8017	/* FC only */
#define	ASYNC_AUTO_PLOGI_RJT		0x8018
#define	ASYNC_SECURITY_UPDATE		0x801B
#define	ASYNC_CMD_CMPLT			0x8020
#define	ASYNC_CTIO_DONE			0x8021
#define	ASYNC_RIO32_1			0x8021
#define	ASYNC_RIO32_2			0x8022
#define	ASYNC_IP_XMIT_DONE		0x8022
#define	ASYNC_IP_RECV_DONE		0x8023
#define	ASYNC_IP_BROADCAST		0x8024
#define	ASYNC_IP_RCVQ_LOW		0x8025
#define	ASYNC_IP_RCVQ_EMPTY		0x8026
#define	ASYNC_IP_RECV_DONE_ALIGNED	0x8027
#define	ASYNC_ERR_LOGGING_DISABLED	0x8029
#define	ASYNC_PTPMODE			0x8030	/* FC only */
#define	ASYNC_RIO16_1			0x8031
#define	ASYNC_RIO16_2			0x8032
#define	ASYNC_RIO16_3			0x8033
#define	ASYNC_RIO16_4			0x8034
#define	ASYNC_RIO16_5			0x8035
#define	ASYNC_CONNMODE			0x8036
#define		ISP_CONN_LOOP		1
#define		ISP_CONN_PTP		2
#define		ISP_CONN_BADLIP		3
#define		ISP_CONN_FATAL		4
#define		ISP_CONN_LOOPBACK	5
#define	ASYNC_P2P_INIT_ERR		0x8037
#define	ASYNC_RIOZIO_STALL		0x8040	/* there's a RIO/ZIO entry that hasn't been serviced */
#define	ASYNC_RIO32_2_2200		0x8042	/* same as ASYNC_RIO32_2, but for 2100/2200 */
#define	ASYNC_RCV_ERR			0x8048
/*
 * 2.01.31 2200 Only. Need Bit 13 in Mailbox 1 for Set Firmware Options
 * mailbox command to enable this.
 */
#define	ASYNC_QFULL_SENT		0x8049
#define	ASYNC_RJT_SENT			0x8049	/* 24XX only */
#define	ASYNC_SEL_CLASS2_P_RJT_SENT	0x804f
#define	ASYNC_FW_RESTART_COMPLETE	0x8060
#define	ASYNC_TEMPERATURE_ALERT		0x8070
#define	ASYNC_INTER_DRIVER_COMP		0x8100	/* FCoE only */
#define	ASYNC_INTER_DRIVER_NOTIFY	0x8101	/* FCoE only */
#define	ASYNC_INTER_DRIVER_TIME_EXT	0x8102	/* FCoE only */
#define	ASYNC_TRANSCEIVER_INSERTION	0x8130
#define	ASYNC_TRANSCEIVER_REMOVAL	0x8131
#define	ASYNC_NIC_FW_STATE_CHANGE	0x8200	/* FCoE only */
#define	ASYNC_AUTOLOAD_FW_COMPLETE	0x8400
#define	ASYNC_AUTOLOAD_FW_FAILURE	0x8401

/*
 * Firmware Options. There are a lot of them.
 *
 * IFCOPTN - ISP Fibre Channel Option Word N
 */
#define	IFCOPT1_EQFQASYNC	(1 << 13)	/* enable QFULL notification */
#define	IFCOPT1_EAABSRCVD	(1 << 12)
#define	IFCOPT1_RJTASYNC	(1 << 11)	/* enable 8018 notification */
#define	IFCOPT1_ENAPURE		(1 << 10)
#define	IFCOPT1_ENA8017		(1 << 7)
#define	IFCOPT1_DISGPIO67	(1 << 6)
#define	IFCOPT1_LIPLOSSIMM	(1 << 5)
#define	IFCOPT1_DISF7SWTCH	(1 << 4)
#define	IFCOPT1_CTIO_RETRY	(1 << 3)
#define	IFCOPT1_LIPASYNC	(1 << 1)
#define	IFCOPT1_LIPF8		(1 << 0)

#define	IFCOPT2_LOOPBACK	(1 << 1)
#define	IFCOPT2_ATIO3_ONLY	(1 << 0)

#define	IFCOPT3_NOPRLI		(1 << 4)	/* disable automatic sending of PRLI on local loops */
#define	IFCOPT3_RNDASYNC	(1 << 1)

/*
 * All IOCB Queue entries are this size
 */
#define	QENTRY_LEN			64

/*
 * Command Structure Definitions
 */

typedef struct {
	uint32_t	ds_base;
	uint32_t	ds_count;
} ispds_t;

typedef struct {
	uint32_t	ds_base;
	uint32_t	ds_basehi;
	uint32_t	ds_count;
} ispds64_t;

#define	DSTYPE_32BIT	0
#define	DSTYPE_64BIT	1
typedef struct {
	uint16_t	ds_type;	/* 0-> ispds_t, 1-> ispds64_t */
	uint32_t	ds_segment;	/* unused */
	uint32_t	ds_base;	/* 32 bit address of DSD list */
} ispdslist_t;


typedef struct {
	uint8_t		rqs_entry_type;
	uint8_t		rqs_entry_count;
	uint8_t		rqs_seqno;
	uint8_t		rqs_flags;
} isphdr_t;

/* RQS Flag definitions */
#define	RQSFLAG_CONTINUATION	0x01
#define	RQSFLAG_FULL		0x02
#define	RQSFLAG_BADHEADER	0x04
#define	RQSFLAG_BADPACKET	0x08
#define	RQSFLAG_BADCOUNT	0x10
#define	RQSFLAG_BADORDER	0x20
#define	RQSFLAG_MASK		0x3f

/* RQS entry_type definitions */
#define	RQSTYPE_REQUEST		0x01
#define	RQSTYPE_DATASEG		0x02
#define	RQSTYPE_RESPONSE	0x03
#define	RQSTYPE_MARKER		0x04
#define	RQSTYPE_CMDONLY		0x05
#define	RQSTYPE_ATIO		0x06	/* Target Mode */
#define	RQSTYPE_CTIO		0x07	/* Target Mode */
#define	RQSTYPE_SCAM		0x08
#define	RQSTYPE_A64		0x09
#define	RQSTYPE_A64_CONT	0x0a
#define	RQSTYPE_ENABLE_LUN	0x0b	/* Target Mode */
#define	RQSTYPE_MODIFY_LUN	0x0c	/* Target Mode */
#define	RQSTYPE_NOTIFY		0x0d	/* Target Mode */
#define	RQSTYPE_NOTIFY_ACK	0x0e	/* Target Mode */
#define	RQSTYPE_CTIO1		0x0f	/* Target Mode */
#define	RQSTYPE_STATUS_CONT	0x10
#define	RQSTYPE_T2RQS		0x11
#define	RQSTYPE_CTIO7		0x12
#define	RQSTYPE_IP_XMIT		0x13
#define	RQSTYPE_TSK_MGMT	0x14
#define	RQSTYPE_T4RQS		0x15
#define	RQSTYPE_ATIO2		0x16	/* Target Mode */
#define	RQSTYPE_CTIO2		0x17	/* Target Mode */
#define	RQSTYPE_T7RQS		0x18
#define	RQSTYPE_T3RQS		0x19
#define	RQSTYPE_IP_XMIT_64	0x1b
#define	RQSTYPE_CTIO4		0x1e	/* Target Mode */
#define	RQSTYPE_CTIO3		0x1f	/* Target Mode */
#define	RQSTYPE_RIO1		0x21
#define	RQSTYPE_RIO2		0x22
#define	RQSTYPE_IP_RECV		0x23
#define	RQSTYPE_IP_RECV_CONT	0x24
#define	RQSTYPE_CT_PASSTHRU	0x29
#define	RQSTYPE_MS_PASSTHRU	0x29
#define	RQSTYPE_VP_CTRL		0x30	/* 24XX only */
#define	RQSTYPE_VP_MODIFY	0x31	/* 24XX only */
#define	RQSTYPE_RPT_ID_ACQ	0x32	/* 24XX only */
#define	RQSTYPE_ABORT_IO	0x33
#define	RQSTYPE_T6RQS		0x48
#define	RQSTYPE_LOGIN		0x52
#define	RQSTYPE_ABTS_RCVD	0x54	/* 24XX only */
#define	RQSTYPE_ABTS_RSP	0x55	/* 24XX only */


#define	ISP_RQDSEG	4
typedef struct {
	isphdr_t	req_header;
	uint32_t	req_handle;
	uint8_t		req_lun_trn;
	uint8_t		req_target;
	uint16_t	req_cdblen;
	uint16_t	req_flags;
	uint16_t	req_reserved;
	uint16_t	req_time;
	uint16_t	req_seg_count;
	uint8_t		req_cdb[12];
	ispds_t		req_dataseg[ISP_RQDSEG];
} ispreq_t;
#define	ISP_RQDSEG_A64	2

typedef struct {
	isphdr_t	mrk_header;
	uint32_t	mrk_handle;
	uint8_t		mrk_reserved0;
	uint8_t		mrk_target;
	uint16_t	mrk_modifier;
	uint16_t	mrk_flags;
	uint16_t	mrk_lun;
	uint8_t		mrk_reserved1[48];
} isp_marker_t;
	
typedef struct {
	isphdr_t	mrk_header;
	uint32_t	mrk_handle;
	uint16_t	mrk_nphdl;
	uint8_t		mrk_modifier;
	uint8_t		mrk_reserved0;
	uint8_t		mrk_reserved1;
	uint8_t		mrk_vphdl;
	uint16_t	mrk_reserved2;
	uint8_t		mrk_lun[8];
	uint8_t		mrk_reserved3[40];
} isp_marker_24xx_t;
	

#define SYNC_DEVICE	0
#define SYNC_TARGET	1
#define SYNC_ALL	2
#define SYNC_LIP	3

#define	ISP_RQDSEG_T2		3
typedef struct {
	isphdr_t	req_header;
	uint32_t	req_handle;
	uint8_t		req_lun_trn;
	uint8_t		req_target;
	uint16_t	req_scclun;
	uint16_t	req_flags;
	uint8_t		req_crn;
	uint8_t		req_reserved;
	uint16_t	req_time;
	uint16_t	req_seg_count;
	uint8_t		req_cdb[16];
	uint32_t	req_totalcnt;
	ispds_t		req_dataseg[ISP_RQDSEG_T2];
} ispreqt2_t;

typedef struct {
	isphdr_t	req_header;
	uint32_t	req_handle;
	uint16_t	req_target;
	uint16_t	req_scclun;
	uint16_t	req_flags;
	uint8_t		req_crn;
	uint8_t		req_reserved;
	uint16_t	req_time;
	uint16_t	req_seg_count;
	uint8_t		req_cdb[16];
	uint32_t	req_totalcnt;
	ispds_t		req_dataseg[ISP_RQDSEG_T2];
} ispreqt2e_t;

#define	ISP_RQDSEG_T3		2
typedef struct {
	isphdr_t	req_header;
	uint32_t	req_handle;
	uint8_t		req_lun_trn;
	uint8_t		req_target;
	uint16_t	req_scclun;
	uint16_t	req_flags;
	uint8_t		req_crn;
	uint8_t		req_reserved;
	uint16_t	req_time;
	uint16_t	req_seg_count;
	uint8_t		req_cdb[16];
	uint32_t	req_totalcnt;
	ispds64_t	req_dataseg[ISP_RQDSEG_T3];
} ispreqt3_t;
#define	ispreq64_t	ispreqt3_t	/* same as.... */

typedef struct {
	isphdr_t	req_header;
	uint32_t	req_handle;
	uint16_t	req_target;
	uint16_t	req_scclun;
	uint16_t	req_flags;
	uint8_t		req_crn;
	uint8_t		req_reserved;
	uint16_t	req_time;
	uint16_t	req_seg_count;
	uint8_t		req_cdb[16];
	uint32_t	req_totalcnt;
	ispds64_t	req_dataseg[ISP_RQDSEG_T3];
} ispreqt3e_t;

/* req_flag values */
#define	REQFLAG_NODISCON	0x0001
#define	REQFLAG_HTAG		0x0002
#define	REQFLAG_OTAG		0x0004
#define	REQFLAG_STAG		0x0008
#define	REQFLAG_TARGET_RTN	0x0010

#define	REQFLAG_NODATA		0x0000
#define	REQFLAG_DATA_IN		0x0020
#define	REQFLAG_DATA_OUT	0x0040
#define	REQFLAG_DATA_UNKNOWN	0x0060

#define	REQFLAG_DISARQ		0x0100
#define	REQFLAG_FRC_ASYNC	0x0200
#define	REQFLAG_FRC_SYNC	0x0400
#define	REQFLAG_FRC_WIDE	0x0800
#define	REQFLAG_NOPARITY	0x1000
#define	REQFLAG_STOPQ		0x2000
#define	REQFLAG_XTRASNS		0x4000
#define	REQFLAG_PRIORITY	0x8000

typedef struct {
	isphdr_t	req_header;
	uint32_t	req_handle;
	uint8_t		req_lun_trn;
	uint8_t		req_target;
	uint16_t	req_cdblen;
	uint16_t	req_flags;
	uint16_t	req_reserved;
	uint16_t	req_time;
	uint16_t	req_seg_count;
	uint8_t		req_cdb[44];
} ispextreq_t;


/*
 * ISP24XX structures
 */
typedef struct {
	isphdr_t	req_header;
	uint32_t	req_handle;
	uint16_t	req_nphdl;
	uint16_t	req_time;
	uint16_t	req_seg_count;
	uint16_t	req_reserved;
	uint8_t		req_lun[8];
	uint8_t		req_alen_datadir;
	uint8_t		req_task_management;
	uint8_t		req_task_attribute;
	uint8_t		req_crn;
	uint8_t		req_cdb[16];
	uint32_t	req_dl;
	uint16_t	req_tidlo;
	uint8_t		req_tidhi;
	uint8_t		req_vpidx;
	ispds64_t	req_dataseg;
} ispreqt7_t;

/* Task Management Request Function */
typedef struct {
	isphdr_t	tmf_header;
	uint32_t	tmf_handle;
	uint16_t	tmf_nphdl;
	uint8_t		tmf_reserved0[2];
	uint16_t	tmf_delay;
	uint16_t	tmf_timeout;
	uint8_t		tmf_lun[8];
	uint32_t	tmf_flags;
	uint8_t		tmf_reserved1[20];
	uint16_t	tmf_tidlo;
	uint8_t		tmf_tidhi;
	uint8_t		tmf_vpidx;
	uint8_t		tmf_reserved2[12];
} isp24xx_tmf_t;

#define	ISP24XX_TMF_NOSEND		0x80000000

#define	ISP24XX_TMF_LUN_RESET		0x00000010
#define	ISP24XX_TMF_ABORT_TASK_SET	0x00000008
#define	ISP24XX_TMF_CLEAR_TASK_SET	0x00000004
#define	ISP24XX_TMF_TARGET_RESET	0x00000002
#define	ISP24XX_TMF_CLEAR_ACA		0x00000001

/* I/O Abort Structure */
typedef struct {
	isphdr_t	abrt_header;
	uint32_t	abrt_handle;
	uint16_t	abrt_nphdl;
	uint16_t	abrt_options;
	uint32_t	abrt_cmd_handle;
	uint16_t	abrt_queue_number;
	uint8_t		abrt_reserved[30];
	uint16_t	abrt_tidlo;
	uint8_t		abrt_tidhi;
	uint8_t		abrt_vpidx;
	uint8_t		abrt_reserved1[12];
} isp24xx_abrt_t;

#define	ISP24XX_ABRT_NOSEND	0x01	/* don't actually send ABTS */
#define	ISP24XX_ABRT_OKAY	0x00	/* in nphdl on return */
#define	ISP24XX_ABRT_ENXIO	0x31	/* in nphdl on return */

#define	ISP_CDSEG	7
typedef struct {
	isphdr_t	req_header;
	uint32_t	req_reserved;
	ispds_t		req_dataseg[ISP_CDSEG];
} ispcontreq_t;

#define	ISP_CDSEG64	5
typedef struct {
	isphdr_t	req_header;
	ispds64_t	req_dataseg[ISP_CDSEG64];
} ispcontreq64_t;

typedef struct {
	isphdr_t	req_header;
	uint32_t	req_handle;
	uint16_t	req_scsi_status;
	uint16_t	req_completion_status;
	uint16_t	req_state_flags;
	uint16_t	req_status_flags;
	uint16_t	req_time;
#define	req_response_len	req_time	/* FC only */
	uint16_t	req_sense_len;
	uint32_t	req_resid;
	uint8_t		req_response[8];	/* FC only */
	uint8_t		req_sense_data[32];
} ispstatusreq_t;

/*
 * Status Continuation
 */
typedef struct {
	isphdr_t	req_header;
	uint8_t		req_sense_data[60];
} ispstatus_cont_t;

/*
 * 24XX Type 0 status
 */
typedef struct {
	isphdr_t	req_header;
	uint32_t	req_handle;
	uint16_t	req_completion_status;
	uint16_t	req_oxid;
	uint32_t	req_resid;
	uint16_t	req_reserved0;
	uint16_t	req_state_flags;
	uint16_t	req_retry_delay;	/* aka Status Qualifier */
	uint16_t	req_scsi_status;
	uint32_t	req_fcp_residual;
	uint32_t	req_sense_len;
	uint32_t	req_response_len;
	uint8_t		req_rsp_sense[28];
} isp24xx_statusreq_t;

/* 
 * For Qlogic 2X00, the high order byte of SCSI status has
 * additional meaning.
 */
#define	RQCS_CR	0x1000	/* Confirmation Request */
#define	RQCS_RU	0x0800	/* Residual Under */
#define	RQCS_RO	0x0400	/* Residual Over */
#define	RQCS_RESID	(RQCS_RU|RQCS_RO)
#define	RQCS_SV	0x0200	/* Sense Length Valid */
#define	RQCS_RV	0x0100	/* FCP Response Length Valid */

/*
 * CT Passthru IOCB
 */
typedef struct {
	isphdr_t	ctp_header;
	uint32_t	ctp_handle;
	uint16_t	ctp_status;
	uint16_t	ctp_nphdl;	/* n-port handle */
	uint16_t	ctp_cmd_cnt;	/* Command DSD count */
	uint8_t		ctp_vpidx;
	uint8_t		ctp_reserved0;
	uint16_t	ctp_time;
	uint16_t	ctp_reserved1;
	uint16_t	ctp_rsp_cnt;	/* Response DSD count */
	uint16_t	ctp_reserved2[5];
	uint32_t	ctp_rsp_bcnt;	/* Response byte count */
	uint32_t	ctp_cmd_bcnt;	/* Command byte count */
	ispds64_t	ctp_dataseg[2];
} isp_ct_pt_t;

/*
 * MS Passthru IOCB
 */
typedef struct {
	isphdr_t	ms_header;
	uint32_t	ms_handle;
	uint16_t	ms_nphdl;	/* handle in high byte for !2k f/w */
	uint16_t	ms_status;
	uint16_t	ms_flags;
	uint16_t	ms_reserved1;	/* low 8 bits */
	uint16_t	ms_time;
	uint16_t	ms_cmd_cnt;	/* Command DSD count */
	uint16_t	ms_tot_cnt;	/* Total DSD Count */
	uint8_t		ms_type;	/* MS type */
	uint8_t		ms_r_ctl;	/* R_CTL */
	uint16_t	ms_rxid;	/* RX_ID */
	uint16_t	ms_reserved2;
	uint32_t	ms_handle2;
	uint32_t	ms_rsp_bcnt;	/* Response byte count */
	uint32_t	ms_cmd_bcnt;	/* Command byte count */
	ispds64_t	ms_dataseg[2];
} isp_ms_t;

/* 
 * Completion Status Codes.
 */
#define RQCS_COMPLETE			0x0000
#define RQCS_DMA_ERROR			0x0002
#define RQCS_RESET_OCCURRED		0x0004
#define RQCS_ABORTED			0x0005
#define RQCS_TIMEOUT			0x0006
#define RQCS_DATA_OVERRUN		0x0007
#define RQCS_DATA_UNDERRUN		0x0015
#define	RQCS_QUEUE_FULL			0x001C

/* 1X00 Only Completion Codes */
#define RQCS_INCOMPLETE			0x0001
#define RQCS_TRANSPORT_ERROR		0x0003
#define RQCS_COMMAND_OVERRUN		0x0008
#define RQCS_STATUS_OVERRUN		0x0009
#define RQCS_BAD_MESSAGE		0x000a
#define RQCS_NO_MESSAGE_OUT		0x000b
#define RQCS_EXT_ID_FAILED		0x000c
#define RQCS_IDE_MSG_FAILED		0x000d
#define RQCS_ABORT_MSG_FAILED		0x000e
#define RQCS_REJECT_MSG_FAILED		0x000f
#define RQCS_NOP_MSG_FAILED		0x0010
#define RQCS_PARITY_ERROR_MSG_FAILED	0x0011
#define RQCS_DEVICE_RESET_MSG_FAILED	0x0012
#define RQCS_ID_MSG_FAILED		0x0013
#define RQCS_UNEXP_BUS_FREE		0x0014
#define	RQCS_XACT_ERR1			0x0018
#define	RQCS_XACT_ERR2			0x0019
#define	RQCS_XACT_ERR3			0x001A
#define	RQCS_BAD_ENTRY			0x001B
#define	RQCS_PHASE_SKIPPED		0x001D
#define	RQCS_ARQS_FAILED		0x001E
#define	RQCS_WIDE_FAILED		0x001F
#define	RQCS_SYNCXFER_FAILED		0x0020
#define	RQCS_LVD_BUSERR			0x0021

/* 2X00 Only Completion Codes */
#define	RQCS_PORT_UNAVAILABLE		0x0028
#define	RQCS_PORT_LOGGED_OUT		0x0029
#define	RQCS_PORT_CHANGED		0x002A
#define	RQCS_PORT_BUSY			0x002B

/* 24XX Only Completion Codes */
#define	RQCS_24XX_DRE			0x0011	/* data reassembly error */
#define	RQCS_24XX_TABORT		0x0013	/* aborted by target */
#define	RQCS_24XX_ENOMEM		0x002C	/* f/w resource unavailable */
#define	RQCS_24XX_TMO			0x0030	/* task management overrun */


/*
 * 1X00 specific State Flags 
 */
#define RQSF_GOT_BUS			0x0100
#define RQSF_GOT_TARGET			0x0200
#define RQSF_SENT_CDB			0x0400
#define RQSF_XFRD_DATA			0x0800
#define RQSF_GOT_STATUS			0x1000
#define RQSF_GOT_SENSE			0x2000
#define	RQSF_XFER_COMPLETE		0x4000

/*
 * 2X00 specific State Flags
 * (same as 1X00 except RQSF_GOT_BUS/RQSF_GOT_TARGET are not available)
 */
#define	RQSF_DATA_IN			0x0020
#define	RQSF_DATA_OUT			0x0040
#define	RQSF_STAG			0x0008
#define	RQSF_OTAG			0x0004
#define	RQSF_HTAG			0x0002
/*
 * 1X00 Status Flags
 */
#define RQSTF_DISCONNECT		0x0001
#define RQSTF_SYNCHRONOUS		0x0002
#define RQSTF_PARITY_ERROR		0x0004
#define RQSTF_BUS_RESET			0x0008
#define RQSTF_DEVICE_RESET		0x0010
#define RQSTF_ABORTED			0x0020
#define RQSTF_TIMEOUT			0x0040
#define RQSTF_NEGOTIATION		0x0080

/*
 * 2X00 specific state flags
 */
/* RQSF_SENT_CDB	*/
/* RQSF_XFRD_DATA	*/
/* RQSF_GOT_STATUS	*/
/* RQSF_XFER_COMPLETE	*/

/*
 * 2X00 specific status flags
 */
/* RQSTF_ABORTED */
/* RQSTF_TIMEOUT */
#define	RQSTF_DMA_ERROR			0x0080
#define	RQSTF_LOGOUT			0x2000

/*
 * Miscellaneous
 */
#ifndef	ISP_EXEC_THROTTLE
#define	ISP_EXEC_THROTTLE	16
#endif

/*
 * About Firmware returns an 'attribute' word in mailbox 6.
 * These attributes are for 2200 and 2300.
 */
#define	ISP_FW_ATTR_TMODE	0x0001
#define	ISP_FW_ATTR_SCCLUN	0x0002
#define	ISP_FW_ATTR_FABRIC	0x0004
#define	ISP_FW_ATTR_CLASS2	0x0008
#define	ISP_FW_ATTR_FCTAPE	0x0010
#define	ISP_FW_ATTR_IP		0x0020
#define	ISP_FW_ATTR_VI		0x0040
#define	ISP_FW_ATTR_VI_SOLARIS	0x0080
#define	ISP_FW_ATTR_2KLOGINS	0x0100	/* just a guess... */

/* and these are for the 2400 */
#define	ISP2400_FW_ATTR_CLASS2	0x0001
#define	ISP2400_FW_ATTR_IP	0x0002
#define	ISP2400_FW_ATTR_MULTIID	0x0004
#define	ISP2400_FW_ATTR_SB2	0x0008
#define	ISP2400_FW_ATTR_T10CRC	0x0010
#define	ISP2400_FW_ATTR_VI	0x0020
#define	ISP2400_FW_ATTR_MQ	0x0040
#define	ISP2400_FW_ATTR_MSIX	0x0080
#define	ISP2400_FW_ATTR_FCOE	0x0800
#define	ISP2400_FW_ATTR_VP0	0x1000
#define	ISP2400_FW_ATTR_EXPFW	0x2000
#define	ISP2400_FW_ATTR_HOTFW	0x4000
#define	ISP2400_FW_ATTR_EXTNDED	0x8000
#define	ISP2400_FW_ATTR_EXTVP	0x00010000
#define	ISP2400_FW_ATTR_VN2VN	0x00040000
#define	ISP2400_FW_ATTR_EXMOFF	0x00080000
#define	ISP2400_FW_ATTR_NPMOFF	0x00100000
#define	ISP2400_FW_ATTR_DIFCHOP	0x00400000
#define	ISP2400_FW_ATTR_SRIOV	0x02000000
#define	ISP2400_FW_ATTR_ASICTMP	0x0200000000
#define	ISP2400_FW_ATTR_ATIOMQ	0x0400000000

/*
 * These are either manifestly true or are dependent on f/w attributes
 */
#define	ISP_CAP_TMODE(isp)	\
	(IS_24XX(isp)? 1 : (isp->isp_fwattr & ISP_FW_ATTR_TMODE))
#define	ISP_CAP_SCCFW(isp)	\
	(IS_24XX(isp)? 1 : (isp->isp_fwattr & ISP_FW_ATTR_SCCLUN))
#define	ISP_CAP_2KLOGIN(isp)	\
	(IS_24XX(isp)? 1 : (isp->isp_fwattr & ISP_FW_ATTR_2KLOGINS))

/*
 * This is only true for 24XX cards with this f/w attribute
 */
#define	ISP_CAP_MULTI_ID(isp)	\
	(IS_24XX(isp)? (isp->isp_fwattr & ISP2400_FW_ATTR_MULTIID) : 0)
#define	ISP_GET_VPIDX(isp, tag) \
	(ISP_CAP_MULTI_ID(isp) ? tag : 0)
#define	ISP_CAP_MSIX(isp)	\
	(IS_24XX(isp)? (isp->isp_fwattr & ISP2400_FW_ATTR_MSIX) : 0)
#define	ISP_CAP_VP0(isp)	\
	(IS_24XX(isp)? (isp->isp_fwattr & ISP2400_FW_ATTR_VP0) : 0)

/*
 * This is true manifestly or is dependent on a f/w attribute
 * but may or may not actually be *enabled*. In any case, it
 * is enabled on a per-channel basis.
 */
#define	ISP_CAP_FCTAPE(isp)	\
	(IS_24XX(isp)? 1 : (isp->isp_fwattr & ISP_FW_ATTR_FCTAPE))

#define	ISP_FCTAPE_ENABLED(isp, chan)	\
	(IS_24XX(isp)? (FCPARAM(isp, chan)->isp_xfwoptions & ICB2400_OPT2_FCTAPE) != 0 : (FCPARAM(isp, chan)->isp_xfwoptions & ICBXOPT_FCTAPE) != 0)

/*
 * Reduced Interrupt Operation Response Queue Entries
 */

typedef struct {
	isphdr_t	req_header;
	uint32_t	req_handles[15];
} isp_rio1_t;

typedef struct {
	isphdr_t	req_header;
	uint16_t	req_handles[30];
} isp_rio2_t;

/*
 * FC (ISP2100/ISP2200/ISP2300/ISP2400) specific data structures
 */

/*
 * Initialization Control Block
 *
 * Version One (prime) format.
 */
typedef struct {
	uint8_t		icb_version;
	uint8_t		icb_reserved0;
	uint16_t	icb_fwoptions;
	uint16_t	icb_maxfrmlen;
	uint16_t	icb_maxalloc;
	uint16_t	icb_execthrottle;
	uint8_t		icb_retry_count;
	uint8_t		icb_retry_delay;
	uint8_t		icb_portname[8];
	uint16_t	icb_hardaddr;
	uint8_t		icb_iqdevtype;
	uint8_t		icb_logintime;
	uint8_t		icb_nodename[8];
	uint16_t	icb_rqstout;
	uint16_t	icb_rspnsin;
	uint16_t	icb_rqstqlen;
	uint16_t	icb_rsltqlen;
	uint16_t	icb_rqstaddr[4];
	uint16_t	icb_respaddr[4];
	uint16_t	icb_lunenables;
	uint8_t		icb_ccnt;
	uint8_t		icb_icnt;
	uint16_t	icb_lunetimeout;
	uint16_t	icb_reserved1;
	uint16_t	icb_xfwoptions;
	uint8_t		icb_racctimer;
	uint8_t		icb_idelaytimer;
	uint16_t	icb_zfwoptions;
	uint16_t	icb_reserved2[13];
} isp_icb_t;

#define	ICB_VERSION1	1

#define	ICBOPT_EXTENDED		0x8000
#define	ICBOPT_BOTH_WWNS	0x4000
#define	ICBOPT_FULL_LOGIN	0x2000
#define	ICBOPT_STOP_ON_QFULL	0x1000	/* 2200/2100 only */
#define	ICBOPT_PREV_ADDRESS	0x0800
#define	ICBOPT_SRCHDOWN		0x0400
#define	ICBOPT_NOLIP		0x0200
#define	ICBOPT_PDBCHANGE_AE	0x0100
#define	ICBOPT_TGT_TYPE		0x0080
#define	ICBOPT_INI_ADISC	0x0040
#define	ICBOPT_INI_DISABLE	0x0020
#define	ICBOPT_TGT_ENABLE	0x0010
#define	ICBOPT_FAST_POST	0x0008
#define	ICBOPT_FULL_DUPLEX	0x0004
#define	ICBOPT_FAIRNESS		0x0002
#define	ICBOPT_HARD_ADDRESS	0x0001

#define	ICBXOPT_NO_LOGOUT	0x8000	/* no logout on link failure */
#define	ICBXOPT_FCTAPE_CCQ	0x4000	/* FC-Tape Command Queueing */
#define	ICBXOPT_FCTAPE_CONFIRM	0x2000
#define	ICBXOPT_FCTAPE		0x1000
#define	ICBXOPT_CLASS2_ACK0	0x0200
#define	ICBXOPT_CLASS2		0x0100
#define	ICBXOPT_NO_PLAY		0x0080	/* don't play if can't get hard addr */
#define	ICBXOPT_TOPO_MASK	0x0070
#define	ICBXOPT_LOOP_ONLY	0x0000
#define	ICBXOPT_PTP_ONLY	0x0010
#define	ICBXOPT_LOOP_2_PTP	0x0020
#define	ICBXOPT_PTP_2_LOOP	0x0030
/*
 * The lower 4 bits of the xfwoptions field are the OPERATION MODE bits.
 * RIO is not defined for the 23XX cards (just 2200)
 */
#define	ICBXOPT_RIO_OFF		0
#define	ICBXOPT_RIO_16BIT	1
#define	ICBXOPT_RIO_32BIT	2
#define	ICBXOPT_RIO_16BIT_IOCB	3
#define	ICBXOPT_RIO_32BIT_IOCB	4
#define	ICBXOPT_ZIO		5	
#define	ICBXOPT_TIMER_MASK	0x7

#define	ICBZOPT_RATE_MASK	0xC000
#define	ICBZOPT_RATE_1GB	0x0000
#define	ICBZOPT_RATE_AUTO	0x8000
#define	ICBZOPT_RATE_2GB	0x4000
#define	ICBZOPT_50_OHM		0x2000
#define	ICBZOPT_NO_LOCAL_PLOGI	0x0080
#define	ICBZOPT_ENA_OOF		0x0040	/* out of order frame handling */
#define	ICBZOPT_RSPSZ_MASK	0x0030
#define	ICBZOPT_RSPSZ_24	0x0000
#define	ICBZOPT_RSPSZ_12	0x0010
#define	ICBZOPT_RSPSZ_24A	0x0020
#define	ICBZOPT_RSPSZ_32	0x0030
#define	ICBZOPT_SOFTID		0x0002
#define	ICBZOPT_ENA_RDXFR_RDY	0x0001

/* 2400 F/W options */
#define	ICB2400_OPT1_BOTH_WWNS		0x00004000
#define	ICB2400_OPT1_FULL_LOGIN		0x00002000
#define	ICB2400_OPT1_PREV_ADDRESS	0x00000800
#define	ICB2400_OPT1_SRCHDOWN		0x00000400
#define	ICB2400_OPT1_NOLIP		0x00000200
#define	ICB2400_OPT1_INI_DISABLE	0x00000020
#define	ICB2400_OPT1_TGT_ENABLE		0x00000010
#define	ICB2400_OPT1_FULL_DUPLEX	0x00000004
#define	ICB2400_OPT1_FAIRNESS		0x00000002
#define	ICB2400_OPT1_HARD_ADDRESS	0x00000001

#define	ICB2400_OPT2_ENA_ATIOMQ		0x08000000
#define	ICB2400_OPT2_ENA_IHA		0x04000000
#define	ICB2400_OPT2_QOS		0x02000000
#define	ICB2400_OPT2_IOCBS		0x01000000
#define	ICB2400_OPT2_ENA_IHR		0x00400000
#define	ICB2400_OPT2_ENA_VMS		0x00200000
#define	ICB2400_OPT2_ENA_TA		0x00100000
#define	ICB2400_OPT2_TPRLIC		0x00004000
#define	ICB2400_OPT2_FCTAPE		0x00001000
#define	ICB2400_OPT2_FCSP		0x00000800
#define	ICB2400_OPT2_CLASS2_ACK0	0x00000200
#define	ICB2400_OPT2_CLASS2		0x00000100
#define	ICB2400_OPT2_NO_PLAY		0x00000080
#define	ICB2400_OPT2_TOPO_MASK		0x00000070
#define	ICB2400_OPT2_LOOP_ONLY		0x00000000
#define	ICB2400_OPT2_PTP_ONLY		0x00000010
#define	ICB2400_OPT2_LOOP_2_PTP		0x00000020
#define	ICB2400_OPT2_TIMER_MASK		0x0000000f
#define	ICB2400_OPT2_ZIO		0x00000005
#define	ICB2400_OPT2_ZIO1		0x00000006

#define	ICB2400_OPT3_NO_CTXDIS		0x40000000
#define	ICB2400_OPT3_ENA_ETH_RESP	0x08000000
#define	ICB2400_OPT3_ENA_ETH_ATIO	0x04000000
#define	ICB2400_OPT3_ENA_MFCF		0x00020000
#define	ICB2400_OPT3_SKIP_4GB		0x00010000
#define	ICB2400_OPT3_RATE_MASK		0x0000E000
#define	ICB2400_OPT3_RATE_1GB		0x00000000
#define	ICB2400_OPT3_RATE_2GB		0x00002000
#define	ICB2400_OPT3_RATE_AUTO		0x00004000
#define	ICB2400_OPT3_RATE_4GB		0x00006000
#define	ICB2400_OPT3_RATE_8GB		0x00008000
#define	ICB2400_OPT3_RATE_16GB		0x0000A000
#define	ICB2400_OPT3_RATE_32GB		0x0000C000
#define	ICB2400_OPT3_ENA_OOF_XFRDY	0x00000200
#define	ICB2400_OPT3_NO_N2N_LOGI	0x00000100
#define	ICB2400_OPT3_NO_LOCAL_PLOGI	0x00000080
#define	ICB2400_OPT3_ENA_OOF		0x00000040
/* note that a response size flag of zero is reserved! */
#define	ICB2400_OPT3_RSPSZ_MASK		0x00000030
#define	ICB2400_OPT3_RSPSZ_12		0x00000010
#define	ICB2400_OPT3_RSPSZ_24		0x00000020
#define	ICB2400_OPT3_RSPSZ_32		0x00000030
#define	ICB2400_OPT3_SOFTID		0x00000002

#define	ICB_MIN_FRMLEN		256
#define	ICB_MAX_FRMLEN		2112
#define	ICB_DFLT_FRMLEN		1024
#define	ICB_DFLT_ALLOC		256
#define	ICB_DFLT_THROTTLE	16
#define	ICB_DFLT_RDELAY		5
#define	ICB_DFLT_RCOUNT		3

#define	ICB_LOGIN_TOV		10
#define	ICB_LUN_ENABLE_TOV	15


/*
 * And somebody at QLogic had a great idea that you could just change
 * the structure *and* keep the version number the same as the other cards.
 */
typedef struct {
	uint16_t	icb_version;
	uint16_t	icb_reserved0;
	uint16_t	icb_maxfrmlen;
	uint16_t	icb_execthrottle;
	uint16_t	icb_xchgcnt;
	uint16_t	icb_hardaddr;
	uint8_t		icb_portname[8];
	uint8_t		icb_nodename[8];
	uint16_t	icb_rspnsin;
	uint16_t	icb_rqstout;
	uint16_t	icb_retry_count;
	uint16_t	icb_priout;
	uint16_t	icb_rsltqlen;
	uint16_t	icb_rqstqlen;
	uint16_t	icb_ldn_nols;
	uint16_t	icb_prqstqlen;
	uint16_t	icb_rqstaddr[4];
	uint16_t	icb_respaddr[4];
	uint16_t	icb_priaddr[4];
	uint16_t	icb_msixresp;
	uint16_t	icb_msixatio;
	uint16_t	icb_reserved1[2];
	uint16_t	icb_atio_in;
	uint16_t	icb_atioqlen;
	uint16_t	icb_atioqaddr[4];
	uint16_t	icb_idelaytimer;
	uint16_t	icb_logintime;
	uint32_t	icb_fwoptions1;
	uint32_t	icb_fwoptions2;
	uint32_t	icb_fwoptions3;
	uint16_t	icb_qos;
	uint16_t	icb_reserved2[3];
	uint16_t	icb_enodemac[3];
	uint16_t	icb_disctime;
	uint16_t	icb_reserved3[4];
} isp_icb_2400_t;

#define	RQRSP_ADDR0015	0
#define	RQRSP_ADDR1631	1
#define	RQRSP_ADDR3247	2
#define	RQRSP_ADDR4863	3


#define	ICB_NNM0	7
#define	ICB_NNM1	6
#define	ICB_NNM2	5
#define	ICB_NNM3	4
#define	ICB_NNM4	3
#define	ICB_NNM5	2
#define	ICB_NNM6	1
#define	ICB_NNM7	0

#define	MAKE_NODE_NAME_FROM_WWN(array, wwn)	\
	array[ICB_NNM0] = (uint8_t) ((wwn >>  0) & 0xff), \
	array[ICB_NNM1] = (uint8_t) ((wwn >>  8) & 0xff), \
	array[ICB_NNM2] = (uint8_t) ((wwn >> 16) & 0xff), \
	array[ICB_NNM3] = (uint8_t) ((wwn >> 24) & 0xff), \
	array[ICB_NNM4] = (uint8_t) ((wwn >> 32) & 0xff), \
	array[ICB_NNM5] = (uint8_t) ((wwn >> 40) & 0xff), \
	array[ICB_NNM6] = (uint8_t) ((wwn >> 48) & 0xff), \
	array[ICB_NNM7] = (uint8_t) ((wwn >> 56) & 0xff)

#define	MAKE_WWN_FROM_NODE_NAME(wwn, array)	\
	wwn =	((uint64_t) array[ICB_NNM0]) | \
		((uint64_t) array[ICB_NNM1] <<  8) | \
		((uint64_t) array[ICB_NNM2] << 16) | \
		((uint64_t) array[ICB_NNM3] << 24) | \
		((uint64_t) array[ICB_NNM4] << 32) | \
		((uint64_t) array[ICB_NNM5] << 40) | \
		((uint64_t) array[ICB_NNM6] << 48) | \
		((uint64_t) array[ICB_NNM7] << 56)


/*
 * For MULTI_ID firmware, this describes a
 * virtual port entity for getting status.
 */
typedef struct {
	uint16_t	vp_port_status;
	uint8_t		vp_port_options;
	uint8_t		vp_port_loopid;
	uint8_t		vp_port_portname[8];
	uint8_t		vp_port_nodename[8];
	uint16_t	vp_port_portid_lo;	/* not present when trailing icb */
	uint16_t	vp_port_portid_hi;	/* not present when trailing icb */
} vp_port_info_t;

#define	ICB2400_VPOPT_ENA_SNSLOGIN	0x00000040	/* Enable SNS Login and SCR for Virtual Ports */
#define	ICB2400_VPOPT_TGT_DISABLE	0x00000020	/* Target Mode Disabled */
#define	ICB2400_VPOPT_INI_ENABLE	0x00000010	/* Initiator Mode Enabled */
#define	ICB2400_VPOPT_ENABLED		0x00000008	/* VP Enabled */
#define	ICB2400_VPOPT_NOPLAY		0x00000004	/* ID Not Acquired */
#define	ICB2400_VPOPT_PREV_ADDRESS	0x00000002	/* Previously Assigned ID */
#define	ICB2400_VPOPT_HARD_ADDRESS	0x00000001	/* Hard Assigned ID */

#define	ICB2400_VPOPT_WRITE_SIZE	20

/*
 * For MULTI_ID firmware, we append this structure
 * to the isp_icb_2400_t above, followed by a list
 * structures that are *most* of the vp_port_info_t.
 */
typedef struct {
	uint16_t	vp_count;
	uint16_t	vp_global_options;
} isp_icb_2400_vpinfo_t;

#define	ICB2400_VPINFO_OFF	0x80	/* offset from start of ICB */
#define	ICB2400_VPINFO_PORT_OFF(chan)		\
    (ICB2400_VPINFO_OFF + 			\
     sizeof (isp_icb_2400_vpinfo_t) + ((chan) * ICB2400_VPOPT_WRITE_SIZE))

#define	ICB2400_VPGOPT_FCA		0x01	/* Assume Clean Address bit in FLOGI ACC set (works only in static configurations) */
#define	ICB2400_VPGOPT_MID_DISABLE	0x02	/* when set, connection mode2 will work with NPIV-capable switched */
#define	ICB2400_VPGOPT_VP0_DECOUPLE	0x04	/* Allow VP0 decoupling if firmware supports it */
#define	ICB2400_VPGOPT_SUSP_FDISK	0x10	/* Suspend FDISC for Enabled VPs */
#define	ICB2400_VPGOPT_GEN_RIDA		0x20	/* Generate RIDA if FLOGI Fails */

typedef struct {
	isphdr_t	vp_ctrl_hdr;
	uint32_t	vp_ctrl_handle;
	uint16_t	vp_ctrl_index_fail;
	uint16_t	vp_ctrl_status;
	uint16_t	vp_ctrl_command;
	uint16_t	vp_ctrl_vp_count;
	uint16_t	vp_ctrl_idmap[16];
	uint16_t	vp_ctrl_reserved[7];
	uint16_t	vp_ctrl_fcf_index;
} vp_ctrl_info_t;

#define	VP_CTRL_CMD_ENABLE_VP			0x00
#define	VP_CTRL_CMD_DISABLE_VP			0x08
#define	VP_CTRL_CMD_DISABLE_VP_REINIT_LINK	0x09
#define	VP_CTRL_CMD_DISABLE_VP_LOGO		0x0A
#define	VP_CTRL_CMD_DISABLE_VP_LOGO_ALL		0x0B

/*
 * We can use this structure for modifying either one or two VP ports after initialization
 */
typedef struct {
	isphdr_t	vp_mod_hdr;
	uint32_t	vp_mod_hdl;
	uint16_t	vp_mod_reserved0;
	uint16_t	vp_mod_status;
	uint8_t		vp_mod_cmd;
	uint8_t		vp_mod_cnt;
	uint8_t		vp_mod_idx0;
	uint8_t		vp_mod_idx1;
	struct {
		uint8_t		options;
		uint8_t		loopid;
		uint16_t	reserved1;
		uint8_t		wwpn[8];
		uint8_t		wwnn[8];
	} vp_mod_ports[2];
	uint8_t		vp_mod_reserved2[8];
} vp_modify_t;

#define	VP_STS_OK	0x00
#define	VP_STS_ERR	0x01
#define	VP_CNT_ERR	0x02
#define	VP_GEN_ERR	0x03
#define	VP_IDX_ERR	0x04
#define	VP_STS_BSY	0x05

#define	VP_MODIFY	0x00
#define	VP_MODIFY_ENA	0x01
#define	VP_MODIFY_OPT	0x02
#define	VP_RESUME	0x03

/*
 * Port Data Base Element
 */

typedef struct {
	uint16_t	pdb_options;
	uint8_t		pdb_mstate;
	uint8_t		pdb_sstate;
	uint8_t		pdb_hardaddr_bits[4];
	uint8_t		pdb_portid_bits[4];
	uint8_t		pdb_nodename[8];
	uint8_t		pdb_portname[8];
	uint16_t	pdb_execthrottle;
	uint16_t	pdb_exec_count;
	uint8_t		pdb_retry_count;
	uint8_t		pdb_retry_delay;
	uint16_t	pdb_resalloc;
	uint16_t	pdb_curalloc;
	uint16_t	pdb_qhead;
	uint16_t	pdb_qtail;
	uint16_t	pdb_tl_next;
	uint16_t	pdb_tl_last;
	uint16_t	pdb_features;	/* PLOGI, Common Service */
	uint16_t	pdb_pconcurrnt;	/* PLOGI, Common Service */
	uint16_t	pdb_roi;	/* PLOGI, Common Service */
	uint8_t		pdb_target;
	uint8_t		pdb_initiator;	/* PLOGI, Class 3 Control Flags */
	uint16_t	pdb_rdsiz;	/* PLOGI, Class 3 */
	uint16_t	pdb_ncseq;	/* PLOGI, Class 3 */
	uint16_t	pdb_noseq;	/* PLOGI, Class 3 */
	uint16_t	pdb_labrtflg;
	uint16_t	pdb_lstopflg;
	uint16_t	pdb_sqhead;
	uint16_t	pdb_sqtail;
	uint16_t	pdb_ptimer;
	uint16_t	pdb_nxt_seqid;
	uint16_t	pdb_fcount;
	uint16_t	pdb_prli_len;
	uint16_t	pdb_prli_svc0;
	uint16_t	pdb_prli_svc3;
	uint16_t	pdb_loopid;
	uint16_t	pdb_il_ptr;
	uint16_t	pdb_sl_ptr;
} isp_pdb_21xx_t;

#define	PDB_OPTIONS_XMITTING	(1<<11)
#define	PDB_OPTIONS_LNKXMIT	(1<<10)
#define	PDB_OPTIONS_ABORTED	(1<<9)
#define	PDB_OPTIONS_ADISC	(1<<1)

#define	PDB_STATE_DISCOVERY	0
#define	PDB_STATE_WDISC_ACK	1
#define	PDB_STATE_PLOGI		2
#define	PDB_STATE_PLOGI_ACK	3
#define	PDB_STATE_PRLI		4
#define	PDB_STATE_PRLI_ACK	5
#define	PDB_STATE_LOGGED_IN	6
#define	PDB_STATE_PORT_UNAVAIL	7
#define	PDB_STATE_PRLO		8
#define	PDB_STATE_PRLO_ACK	9
#define	PDB_STATE_PLOGO		10
#define	PDB_STATE_PLOG_ACK	11

#define	SVC3_ROLE_MASK		0x30
#define	SVC3_ROLE_SHIFT		4

#define	BITS2WORD(x)		((x)[0] << 16 | (x)[3] << 8 | (x)[2])
#define	BITS2WORD_24XX(x)	((x)[0] << 16 | (x)[1] << 8 | (x)[2])

/*
 * Port Data Base Element- 24XX cards
 */
typedef struct {
	uint16_t	pdb_flags;
	uint8_t		pdb_curstate;
	uint8_t		pdb_laststate;
	uint8_t		pdb_hardaddr_bits[4];
	uint8_t		pdb_portid_bits[4];
#define		pdb_nxt_seqid_2400	pdb_portid_bits[3]
	uint16_t	pdb_retry_timer;
	uint16_t	pdb_handle;
	uint16_t	pdb_rcv_dsize;
	uint16_t	pdb_reserved0;
	uint16_t	pdb_prli_svc0;
	uint16_t	pdb_prli_svc3;
	uint8_t		pdb_portname[8];
	uint8_t		pdb_nodename[8];
	uint8_t		pdb_reserved1[24];
} isp_pdb_24xx_t;

#define	PDB2400_TID_SUPPORTED	0x4000
#define	PDB2400_FC_TAPE		0x0080
#define	PDB2400_CLASS2_ACK0	0x0040
#define	PDB2400_FCP_CONF	0x0020
#define	PDB2400_CLASS2		0x0010
#define	PDB2400_ADDR_VALID	0x0002

#define	PDB2400_STATE_PLOGI_PEND	0x03
#define	PDB2400_STATE_PLOGI_DONE	0x04
#define	PDB2400_STATE_PRLI_PEND		0x05
#define	PDB2400_STATE_LOGGED_IN		0x06
#define	PDB2400_STATE_PORT_UNAVAIL	0x07
#define	PDB2400_STATE_PRLO_PEND		0x09
#define	PDB2400_STATE_LOGO_PEND		0x0B

/*
 * Common elements from the above two structures that are actually useful to us.
 */
typedef struct {
	uint16_t	handle;
	uint16_t	prli_word0;
	uint16_t	prli_word3;
	uint32_t		: 8,
			portid	: 24;
	uint8_t		portname[8];
	uint8_t		nodename[8];
} isp_pdb_t;

/*
 * Port/Node Name List Element
 */
typedef struct {
	uint8_t		pnnle_name[8];
	uint16_t	pnnle_handle;
	uint16_t	pnnle_reserved;
} isp_pnnle_t;

#define	PNNL_OPTIONS_NODE_NAMES	(1<<0)
#define	PNNL_OPTIONS_PORT_DATA	(1<<2)
#define	PNNL_OPTIONS_INITIATORS	(1<<3)

/*
 * Port and N-Port Handle List Element
 */
typedef struct {
	uint16_t	pnhle_port_id_lo;
	uint16_t	pnhle_port_id_hi_handle;
} isp_pnhle_21xx_t;

typedef struct {
	uint16_t	pnhle_port_id_lo;
	uint16_t	pnhle_port_id_hi;
	uint16_t	pnhle_handle;
} isp_pnhle_23xx_t;

typedef struct {
	uint16_t	pnhle_port_id_lo;
	uint16_t	pnhle_port_id_hi;
	uint16_t	pnhle_handle;
	uint16_t	pnhle_reserved;
} isp_pnhle_24xx_t;

/*
 * Port Database Changed Async Event information for 24XX cards
 */
/* N-Port Handle */
#define PDB24XX_AE_GLOBAL	0xFFFF

/* Reason Codes */
#define	PDB24XX_AE_OK		0x00
#define	PDB24XX_AE_IMPL_LOGO_1	0x01
#define	PDB24XX_AE_IMPL_LOGO_2	0x02
#define	PDB24XX_AE_IMPL_LOGO_3	0x03
#define	PDB24XX_AE_PLOGI_RCVD	0x04
#define	PDB24XX_AE_PLOGI_RJT	0x05
#define	PDB24XX_AE_PRLI_RCVD	0x06
#define	PDB24XX_AE_PRLI_RJT	0x07
#define	PDB24XX_AE_TPRLO	0x08
#define	PDB24XX_AE_TPRLO_RJT	0x09
#define	PDB24XX_AE_PRLO_RCVD	0x0a
#define	PDB24XX_AE_LOGO_RCVD	0x0b
#define	PDB24XX_AE_TOPO_CHG	0x0c
#define	PDB24XX_AE_NPORT_CHG	0x0d
#define	PDB24XX_AE_FLOGI_RJT	0x0e
#define	PDB24XX_AE_BAD_FANN	0x0f
#define	PDB24XX_AE_FLOGI_TIMO	0x10
#define	PDB24XX_AE_ABX_LOGO	0x11
#define	PDB24XX_AE_PLOGI_DONE	0x12
#define	PDB24XX_AE_PRLI_DONE	0x13
#define	PDB24XX_AE_OPN_1	0x14
#define	PDB24XX_AE_OPN_2	0x15
#define	PDB24XX_AE_TXERR	0x16
#define	PDB24XX_AE_FORCED_LOGO	0x17
#define	PDB24XX_AE_DISC_TIMO	0x18

/*
 * Genericized Port Login/Logout software structure
 */
typedef struct {
	uint16_t	handle;
	uint16_t	channel;
	uint32_t
		flags	: 8,
		portid	: 24;
} isp_plcmd_t;
/* the flags to use are those for PLOGX_FLG_* below */

/*
 * ISP24XX- Login/Logout Port IOCB
 */
typedef struct {
	isphdr_t	plogx_header;
	uint32_t	plogx_handle;
	uint16_t	plogx_status;
	uint16_t	plogx_nphdl;
	uint16_t	plogx_flags;
	uint16_t	plogx_vphdl;		/* low 8 bits */
	uint16_t	plogx_portlo;		/* low 16 bits */
	uint16_t	plogx_rspsz_porthi;
	struct {
		uint16_t	lo16;
		uint16_t	hi16;
	} plogx_ioparm[11];
} isp_plogx_t;

#define	PLOGX_STATUS_OK		0x00
#define	PLOGX_STATUS_UNAVAIL	0x28
#define	PLOGX_STATUS_LOGOUT	0x29
#define	PLOGX_STATUS_IOCBERR	0x31

#define	PLOGX_IOCBERR_NOLINK	0x01
#define	PLOGX_IOCBERR_NOIOCB	0x02
#define	PLOGX_IOCBERR_NOXGHG	0x03
#define	PLOGX_IOCBERR_FAILED	0x04	/* further info in IOPARM 1 */
#define	PLOGX_IOCBERR_NOFABRIC	0x05
#define	PLOGX_IOCBERR_NOTREADY	0x07
#define	PLOGX_IOCBERR_NOLOGIN	0x09	/* further info in IOPARM 1 */
#define	PLOGX_IOCBERR_NOPCB	0x0a
#define	PLOGX_IOCBERR_REJECT	0x18	/* further info in IOPARM 1 */
#define	PLOGX_IOCBERR_EINVAL	0x19	/* further info in IOPARM 1 */
#define	PLOGX_IOCBERR_PORTUSED	0x1a	/* further info in IOPARM 1 */
#define	PLOGX_IOCBERR_HNDLUSED	0x1b	/* further info in IOPARM 1 */
#define	PLOGX_IOCBERR_NOHANDLE	0x1c
#define	PLOGX_IOCBERR_NOFLOGI	0x1f	/* further info in IOPARM 1 */

#define	PLOGX_FLG_CMD_MASK	0xf
#define	PLOGX_FLG_CMD_PLOGI	0
#define	PLOGX_FLG_CMD_PRLI	1
#define	PLOGX_FLG_CMD_PDISC	2
#define	PLOGX_FLG_CMD_LOGO	8
#define	PLOGX_FLG_CMD_PRLO	9
#define	PLOGX_FLG_CMD_TPRLO	10

#define	PLOGX_FLG_COND_PLOGI		0x10	/* if with PLOGI */
#define	PLOGX_FLG_IMPLICIT		0x10	/* if with LOGO, PRLO, TPRLO */
#define	PLOGX_FLG_SKIP_PRLI		0x20	/* if with PLOGI */
#define	PLOGX_FLG_IMPLICIT_LOGO_ALL	0x20	/* if with LOGO */
#define	PLOGX_FLG_EXPLICIT_LOGO		0x40	/* if with LOGO */
#define	PLOGX_FLG_COMMON_FEATURES	0x80	/* if with PLOGI */
#define	PLOGX_FLG_FREE_NPHDL		0x80	/* if with with LOGO */

#define	PLOGX_FLG_CLASS2		0x100	/* if with PLOGI */
#define	PLOGX_FLG_FCP2_OVERRIDE		0x200	/* if with PRLOG, PRLI */

/*
 * Report ID Acquisistion (24XX multi-id firmware)
 */
typedef struct {
	isphdr_t	ridacq_hdr;
	uint32_t	ridacq_handle;
	uint8_t		ridacq_vp_acquired;
	uint8_t		ridacq_vp_setup;
	uint8_t		ridacq_vp_index;
	uint8_t		ridacq_vp_status;
	uint16_t	ridacq_vp_port_lo;
	uint8_t		ridacq_vp_port_hi;
	uint8_t		ridacq_format;		/* 0 or 1 */
	uint16_t	ridacq_map[8];
	uint8_t		ridacq_reserved1[32];
} isp_ridacq_t;

#define	RIDACQ_STS_COMPLETE	0
#define	RIDACQ_STS_UNACQUIRED	1
#define	RIDACQ_STS_CHANGED	2
#define	RIDACQ_STS_SNS_TIMEOUT	3
#define	RIDACQ_STS_SNS_REJECTED	4
#define	RIDACQ_STS_SCR_TIMEOUT	5
#define	RIDACQ_STS_SCR_REJECTED	6

/*
 * Simple Name Server Data Structures
 */
#define	SNS_GA_NXT	0x100
#define	SNS_GPN_ID	0x112
#define	SNS_GNN_ID	0x113
#define	SNS_GFT_ID	0x117
#define	SNS_GFF_ID	0x11F
#define	SNS_GID_FT	0x171
#define	SNS_GID_PT	0x1A1
#define	SNS_RFT_ID	0x217
#define	SNS_RSPN_ID	0x218
#define	SNS_RFF_ID	0x21F
#define	SNS_RSNN_NN	0x239
typedef struct {
	uint16_t	snscb_rblen;	/* response buffer length (words) */
	uint16_t	snscb_reserved0;
	uint16_t	snscb_addr[4];	/* response buffer address */
	uint16_t	snscb_sblen;	/* subcommand buffer length (words) */
	uint16_t	snscb_reserved1;
	uint16_t	snscb_data[];	/* variable data */
} sns_screq_t;	/* Subcommand Request Structure */

typedef struct {
	uint16_t	snscb_rblen;	/* response buffer length (words) */
	uint16_t	snscb_reserved0;
	uint16_t	snscb_addr[4];	/* response buffer address */
	uint16_t	snscb_sblen;	/* subcommand buffer length (words) */
	uint16_t	snscb_reserved1;
	uint16_t	snscb_cmd;
	uint16_t	snscb_reserved2;
	uint32_t	snscb_reserved3;
	uint32_t	snscb_port;
} sns_ga_nxt_req_t;
#define	SNS_GA_NXT_REQ_SIZE	(sizeof (sns_ga_nxt_req_t))

typedef struct {			/* Used for GFT_ID, GFF_ID, etc. */
	uint16_t	snscb_rblen;	/* response buffer length (words) */
	uint16_t	snscb_reserved0;
	uint16_t	snscb_addr[4];	/* response buffer address */
	uint16_t	snscb_sblen;	/* subcommand buffer length (words) */
	uint16_t	snscb_reserved1;
	uint16_t	snscb_cmd;
	uint16_t	snscb_mword_div_2;
	uint32_t	snscb_reserved3;
	uint32_t	snscb_portid;
} sns_gxx_id_req_t;
#define	SNS_GXX_ID_REQ_SIZE	(sizeof (sns_gxx_id_req_t))

typedef struct {
	uint16_t	snscb_rblen;	/* response buffer length (words) */
	uint16_t	snscb_reserved0;
	uint16_t	snscb_addr[4];	/* response buffer address */
	uint16_t	snscb_sblen;	/* subcommand buffer length (words) */
	uint16_t	snscb_reserved1;
	uint16_t	snscb_cmd;
	uint16_t	snscb_mword_div_2;
	uint32_t	snscb_reserved3;
	uint32_t	snscb_fc4_type;
} sns_gid_ft_req_t;
#define	SNS_GID_FT_REQ_SIZE	(sizeof (sns_gid_ft_req_t))

typedef struct {
	uint16_t	snscb_rblen;	/* response buffer length (words) */
	uint16_t	snscb_reserved0;
	uint16_t	snscb_addr[4];	/* response buffer address */
	uint16_t	snscb_sblen;	/* subcommand buffer length (words) */
	uint16_t	snscb_reserved1;
	uint16_t	snscb_cmd;
	uint16_t	snscb_mword_div_2;
	uint32_t	snscb_reserved3;
	uint8_t		snscb_port_type;
	uint8_t		snscb_domain;
	uint8_t		snscb_area;
	uint8_t		snscb_flags;
} sns_gid_pt_req_t;
#define	SNS_GID_PT_REQ_SIZE	(sizeof (sns_gid_pt_req_t))

typedef struct {
	uint16_t	snscb_rblen;	/* response buffer length (words) */
	uint16_t	snscb_reserved0;
	uint16_t	snscb_addr[4];	/* response buffer address */
	uint16_t	snscb_sblen;	/* subcommand buffer length (words) */
	uint16_t	snscb_reserved1;
	uint16_t	snscb_cmd;
	uint16_t	snscb_reserved2;
	uint32_t	snscb_reserved3;
	uint32_t	snscb_port;
	uint32_t	snscb_fc4_types[8];
} sns_rft_id_req_t;
#define	SNS_RFT_ID_REQ_SIZE	(sizeof (sns_rft_id_req_t))

typedef struct {
	ct_hdr_t	snscb_cthdr;
	uint8_t		snscb_port_type;
	uint8_t		snscb_port_id[3];
	uint8_t		snscb_portname[8];
	uint16_t	snscb_data[];	/* variable data */
} sns_scrsp_t;	/* Subcommand Response Structure */

typedef struct {
	ct_hdr_t	snscb_cthdr;
	uint8_t		snscb_port_type;
	uint8_t		snscb_port_id[3];
	uint8_t		snscb_portname[8];
	uint8_t		snscb_pnlen;		/* symbolic port name length */
	uint8_t		snscb_pname[255];	/* symbolic port name */
	uint8_t		snscb_nodename[8];
	uint8_t		snscb_nnlen;		/* symbolic node name length */
	uint8_t		snscb_nname[255];	/* symbolic node name */
	uint8_t		snscb_ipassoc[8];
	uint8_t		snscb_ipaddr[16];
	uint8_t		snscb_svc_class[4];
	uint8_t		snscb_fc4_types[32];
	uint8_t		snscb_fpname[8];
	uint8_t		snscb_reserved;
	uint8_t		snscb_hardaddr[3];
} sns_ga_nxt_rsp_t;	/* Subcommand Response Structure */
#define	SNS_GA_NXT_RESP_SIZE	(sizeof (sns_ga_nxt_rsp_t))

typedef struct {
	ct_hdr_t	snscb_cthdr;
	uint8_t		snscb_wwn[8];
} sns_gxn_id_rsp_t;
#define	SNS_GXN_ID_RESP_SIZE	(sizeof (sns_gxn_id_rsp_t))

typedef struct {
	ct_hdr_t	snscb_cthdr;
	uint32_t	snscb_fc4_types[8];
} sns_gft_id_rsp_t;
#define	SNS_GFT_ID_RESP_SIZE	(sizeof (sns_gft_id_rsp_t))

typedef struct {
	ct_hdr_t	snscb_cthdr;
	uint32_t	snscb_fc4_features[32];
} sns_gff_id_rsp_t;
#define	SNS_GFF_ID_RESP_SIZE	(sizeof (sns_gff_id_rsp_t))

typedef struct {			/* Used for GID_FT, GID_PT, etc. */
	ct_hdr_t	snscb_cthdr;
	struct {
		uint8_t		control;
		uint8_t		portid[3];
	} snscb_ports[1];
} sns_gid_xx_rsp_t;
#define	SNS_GID_XX_RESP_SIZE(x)	((sizeof (sns_gid_xx_rsp_t)) + ((x - 1) << 2))

/*
 * Other Misc Structures
 */

/* ELS Pass Through */
typedef struct {
	isphdr_t	els_hdr;
	uint32_t	els_handle;
	uint16_t	els_status;
	uint16_t	els_nphdl;
	uint16_t	els_xmit_dsd_count;	/* outgoing only */
	uint8_t		els_vphdl;
	uint8_t		els_sof;
	uint32_t	els_rxid;
	uint16_t	els_recv_dsd_count;	/* outgoing only */
	uint8_t		els_opcode;
	uint8_t		els_reserved1;
	uint8_t		els_did_lo;
	uint8_t		els_did_mid;
	uint8_t		els_did_hi;
	uint8_t		els_reserved2;
	uint16_t	els_reserved3;
	uint16_t	els_ctl_flags;
	union {
		struct {
			uint32_t	_els_bytecnt;
			uint32_t	_els_subcode1;
			uint32_t	_els_subcode2;
			uint8_t		_els_reserved4[20];
		} in;
		struct {
			uint32_t	_els_recv_bytecnt;
			uint32_t	_els_xmit_bytecnt;
			uint32_t	_els_xmit_dsd_length;
			uint16_t	_els_xmit_dsd_a1500;
			uint16_t	_els_xmit_dsd_a3116;
			uint16_t	_els_xmit_dsd_a4732;
			uint16_t	_els_xmit_dsd_a6348;
			uint32_t	_els_recv_dsd_length;
			uint16_t	_els_recv_dsd_a1500;
			uint16_t	_els_recv_dsd_a3116;
			uint16_t	_els_recv_dsd_a4732;
			uint16_t	_els_recv_dsd_a6348;
		} out;
	} inout;
#define	els_bytecnt		inout.in._els_bytecnt
#define	els_subcode1		inout.in._els_subcode1
#define	els_subcode2		inout.in._els_subcode2
#define	els_reserved4		inout.in._els_reserved4
#define	els_recv_bytecnt	inout.out._els_recv_bytecnt
#define	els_xmit_bytecnt	inout.out._els_xmit_bytecnt
#define	els_xmit_dsd_length	inout.out._els_xmit_dsd_length
#define	els_xmit_dsd_a1500	inout.out._els_xmit_dsd_a1500
#define	els_xmit_dsd_a3116	inout.out._els_xmit_dsd_a3116
#define	els_xmit_dsd_a4732	inout.out._els_xmit_dsd_a4732
#define	els_xmit_dsd_a6348	inout.out._els_xmit_dsd_a6348
#define	els_recv_dsd_length	inout.out._els_recv_dsd_length
#define	els_recv_dsd_a1500	inout.out._els_recv_dsd_a1500
#define	els_recv_dsd_a3116	inout.out._els_recv_dsd_a3116
#define	els_recv_dsd_a4732	inout.out._els_recv_dsd_a4732
#define	els_recv_dsd_a6348	inout.out._els_recv_dsd_a6348
} els_t;

/*
 * A handy package structure for running FC-SCSI commands internally
 */
typedef struct {
	uint16_t	handle;
	uint16_t	lun;
	uint32_t	
		channel : 8,
		portid	: 24;
	uint32_t	timeout;
	union {
		struct {
			uint32_t data_length;
			uint32_t 
				no_wait : 1,
				do_read : 1;
			uint8_t cdb[16];
			void *data_ptr;
		} beg;
		struct {
			uint32_t data_residual;
			uint8_t status;
			uint8_t pad;
			uint16_t sense_length;
			uint8_t sense_data[32];
		} end;
	} fcd;
} isp_xcmd_t;

/*
 * Target Mode related definitions
 */
#define	QLTM_SENSELEN	18	/* non-FC cards only */
#define QLTM_SVALID	0x80

/*
 * Structure for Enable Lun and Modify Lun queue entries
 */
typedef struct {
	isphdr_t	le_header;
	uint32_t	le_reserved;
	uint8_t		le_lun;
	uint8_t		le_rsvd;
	uint8_t		le_ops;		/* Modify LUN only */
	uint8_t		le_tgt;		/* Not for FC */
	uint32_t	le_flags;	/* Not for FC */
	uint8_t		le_status;
	uint8_t		le_reserved2;
	uint8_t		le_cmd_count;
	uint8_t		le_in_count;
	uint8_t		le_cdb6len;	/* Not for FC */
	uint8_t		le_cdb7len;	/* Not for FC */
	uint16_t	le_timeout;
	uint16_t	le_reserved3[20];
} lun_entry_t;

/*
 * le_flags values
 */
#define LUN_TQAE	0x00000002	/* bit1  Tagged Queue Action Enable */
#define LUN_DSSM	0x01000000	/* bit24 Disable Sending SDP Message */
#define	LUN_DISAD	0x02000000	/* bit25 Disable autodisconnect */
#define LUN_DM		0x40000000	/* bit30 Disconnects Mandatory */

/*
 * le_ops values
 */
#define LUN_CCINCR	0x01	/* increment command count */
#define LUN_CCDECR	0x02	/* decrement command count */
#define LUN_ININCR	0x40	/* increment immed. notify count */
#define LUN_INDECR	0x80	/* decrement immed. notify count */

/*
 * le_status values
 */
#define	LUN_OK		0x01	/* we be rockin' */
#define LUN_ERR		0x04	/* request completed with error */
#define LUN_INVAL	0x06	/* invalid request */
#define LUN_NOCAP	0x16	/* can't provide requested capability */
#define LUN_ENABLED	0x3E	/* LUN already enabled */

/*
 * Immediate Notify Entry structure
 */
#define IN_MSGLEN	8	/* 8 bytes */
#define IN_RSVDLEN	8	/* 8 words */
typedef struct {
	isphdr_t	in_header;
	uint32_t	in_reserved;
	uint8_t		in_lun;		/* lun */
	uint8_t		in_iid;		/* initiator */
	uint8_t		in_reserved2;
	uint8_t		in_tgt;		/* target */
	uint32_t	in_flags;
	uint8_t		in_status;
	uint8_t		in_rsvd2;
	uint8_t		in_tag_val;	/* tag value */
	uint8_t		in_tag_type;	/* tag type */
	uint16_t	in_seqid;	/* sequence id */
	uint8_t		in_msg[IN_MSGLEN];	/* SCSI message bytes */
	uint16_t	in_reserved3[IN_RSVDLEN];
	uint8_t		in_sense[QLTM_SENSELEN];/* suggested sense data */
} in_entry_t;

typedef struct {
	isphdr_t	in_header;
	uint32_t	in_reserved;
	uint8_t		in_lun;		/* lun */
	uint8_t		in_iid;		/* initiator */
	uint16_t	in_scclun;
	uint32_t	in_reserved2;
	uint16_t	in_status;
	uint16_t	in_task_flags;
	uint16_t	in_seqid;	/* sequence id */
} in_fcentry_t;

typedef struct {
	isphdr_t	in_header;
	uint32_t	in_reserved;
	uint16_t	in_iid;		/* initiator */
	uint16_t	in_scclun;
	uint32_t	in_reserved2;
	uint16_t	in_status;
	uint16_t	in_task_flags;
	uint16_t	in_seqid;	/* sequence id */
} in_fcentry_e_t;

/*
 * Values for the in_status field
 */
#define	IN_REJECT	0x0D	/* Message Reject message received */
#define IN_RESET	0x0E	/* Bus Reset occurred */
#define IN_NO_RCAP	0x16	/* requested capability not available */
#define IN_IDE_RECEIVED	0x33	/* Initiator Detected Error msg received */
#define IN_RSRC_UNAVAIL	0x34	/* resource unavailable */
#define IN_MSG_RECEIVED	0x36	/* SCSI message received */
#define	IN_ABORT_TASK	0x20	/* task named in RX_ID is being aborted (FC) */
#define	IN_PORT_LOGOUT	0x29	/* port has logged out (FC) */
#define	IN_PORT_CHANGED	0x2A	/* port changed */
#define	IN_GLOBAL_LOGO	0x2E	/* all ports logged out */
#define	IN_NO_NEXUS	0x3B	/* Nexus not established */
#define	IN_SRR_RCVD	0x45	/* SRR received */

/*
 * Values for the in_task_flags field- should only get one at a time!
 */
#define	TASK_FLAGS_RESERVED_MASK	(0xe700)
#define	TASK_FLAGS_CLEAR_ACA		(1<<14)
#define	TASK_FLAGS_TARGET_RESET		(1<<13)
#define	TASK_FLAGS_LUN_RESET		(1<<12)
#define	TASK_FLAGS_CLEAR_TASK_SET	(1<<10)
#define	TASK_FLAGS_ABORT_TASK_SET	(1<<9)

/*
 * ISP24XX Immediate Notify
 */
typedef struct {
	isphdr_t	in_header;
	uint32_t	in_reserved;
	uint16_t	in_nphdl;
	uint16_t	in_reserved1;
	uint16_t	in_flags;
	uint16_t	in_srr_rxid;
	uint16_t	in_status;
	uint8_t		in_status_subcode;
	uint8_t		in_fwhandle;
	uint32_t	in_rxid;
	uint16_t	in_srr_reloff_lo;
	uint16_t	in_srr_reloff_hi;
	uint16_t	in_srr_iu;
	uint16_t	in_srr_oxid;
	/*
	 * If bit 2 is set in in_flags, the N-Port and
	 * handle tags are valid. If the received ELS is
	 * a LOGO, then these tags contain the N Port ID
	 * from the LOGO payload. If the received ELS
	 * request is TPRLO, these tags contain the
	 * Third Party Originator N Port ID.
	 */
	uint16_t	in_nport_id_hi;
#define	in_prli_options in_nport_id_hi
	uint8_t		in_nport_id_lo;
	uint8_t		in_reserved3;
	uint16_t	in_np_handle;
	uint8_t		in_reserved4[12];
	uint8_t		in_reserved5;
	uint8_t		in_vpidx;
	uint32_t	in_reserved6;
	uint16_t	in_portid_lo;
	uint8_t		in_portid_hi;
	uint8_t		in_reserved7;
	uint16_t	in_reserved8;
	uint16_t	in_oxid;
} in_fcentry_24xx_t;

#define	IN24XX_FLAG_PUREX_IOCB		0x1
#define	IN24XX_FLAG_GLOBAL_LOGOUT	0x2
#define	IN24XX_FLAG_NPHDL_VALID		0x4
#define	IN24XX_FLAG_N2N_PRLI		0x8
#define	IN24XX_FLAG_PN_NN_VALID		0x10

#define	IN24XX_LIP_RESET	0x0E
#define	IN24XX_LINK_RESET	0x0F
#define	IN24XX_PORT_LOGOUT	0x29
#define	IN24XX_PORT_CHANGED	0x2A
#define	IN24XX_LINK_FAILED	0x2E
#define	IN24XX_SRR_RCVD		0x45
#define	IN24XX_ELS_RCVD		0x46	/*
					 * login-affectin ELS received- check
					 * subcode for specific opcode
					 */

/*
 * For f/w > 4.0.25, these offsets in the Immediate Notify contain
 * the WWNN/WWPN if the ELS is PLOGI, PDISC or ADISC. The WWN is in
 * Big Endian format.
 */
#define	IN24XX_PRLI_WWNN_OFF	0x18
#define	IN24XX_PRLI_WWPN_OFF	0x28
#define	IN24XX_PLOGI_WWNN_OFF	0x20
#define	IN24XX_PLOGI_WWPN_OFF	0x28

/*
 * For f/w > 4.0.25, this offset in the Immediate Notify contain
 * the WWPN if the ELS is LOGO. The WWN is in Big Endian format.
 */
#define	IN24XX_LOGO_WWPN_OFF	0x28

/*
 * Immediate Notify Status Subcodes for IN24XX_PORT_LOGOUT
 */
#define	IN24XX_PORT_LOGOUT_PDISC_TMO	0x00
#define	IN24XX_PORT_LOGOUT_UXPR_DISC	0x01
#define	IN24XX_PORT_LOGOUT_OWN_OPN	0x02
#define	IN24XX_PORT_LOGOUT_OWN_OPN_SFT	0x03
#define	IN24XX_PORT_LOGOUT_ABTS_TMO	0x04
#define	IN24XX_PORT_LOGOUT_DISC_RJT	0x05
#define	IN24XX_PORT_LOGOUT_LOGIN_NEEDED	0x06
#define	IN24XX_PORT_LOGOUT_BAD_DISC	0x07
#define	IN24XX_PORT_LOGOUT_LOST_ALPA	0x08
#define	IN24XX_PORT_LOGOUT_XMIT_FAILURE	0x09

/*
 * Immediate Notify Status Subcodes for IN24XX_PORT_CHANGED
 */
#define	IN24XX_PORT_CHANGED_BADFAN	0x00
#define	IN24XX_PORT_CHANGED_TOPO_CHANGE	0x01
#define	IN24XX_PORT_CHANGED_FLOGI_ACC	0x02
#define	IN24XX_PORT_CHANGED_FLOGI_RJT	0x03
#define	IN24XX_PORT_CHANGED_TIMEOUT	0x04
#define	IN24XX_PORT_CHANGED_PORT_CHANGE	0x05

/*
 * Notify Acknowledge Entry structure
 */
#define NA_RSVDLEN	22
typedef struct {
	isphdr_t	na_header;
	uint32_t	na_reserved;
	uint8_t		na_lun;		/* lun */
	uint8_t		na_iid;		/* initiator */
	uint8_t		na_reserved2;
	uint8_t		na_tgt;		/* target */
	uint32_t	na_flags;
	uint8_t		na_status;
	uint8_t		na_event;
	uint16_t	na_seqid;	/* sequence id */
	uint16_t	na_reserved3[NA_RSVDLEN];
} na_entry_t;

/*
 * Value for the na_event field
 */
#define NA_RST_CLRD	0x80	/* Clear an async event notification */
#define	NA_OK		0x01	/* Notify Acknowledge Succeeded */
#define	NA_INVALID	0x06	/* Invalid Notify Acknowledge */

#define	NA2_RSVDLEN	21
typedef struct {
	isphdr_t	na_header;
	uint32_t	na_reserved;
	uint8_t		na_reserved1;
	uint8_t		na_iid;		/* initiator loop id */
	uint16_t	na_response;
	uint16_t	na_flags;
	uint16_t	na_reserved2;
	uint16_t	na_status;
	uint16_t	na_task_flags;
	uint16_t	na_seqid;	/* sequence id */
	uint16_t	na_reserved3[NA2_RSVDLEN];
} na_fcentry_t;

typedef struct {
	isphdr_t	na_header;
	uint32_t	na_reserved;
	uint16_t	na_iid;		/* initiator loop id */
	uint16_t	na_response;	/* response code */
	uint16_t	na_flags;
	uint16_t	na_reserved2;
	uint16_t	na_status;
	uint16_t	na_task_flags;
	uint16_t	na_seqid;	/* sequence id */
	uint16_t	na_reserved3[NA2_RSVDLEN];
} na_fcentry_e_t;

#define	NAFC_RCOUNT	0x80	/* increment resource count */
#define NAFC_RST_CLRD	0x20	/* Clear LIP Reset */
#define	NAFC_TVALID	0x10	/* task mangement response code is valid */

/*
 * ISP24XX Notify Acknowledge
 */

typedef struct {
	isphdr_t	na_header;
	uint32_t	na_handle;
	uint16_t	na_nphdl;
	uint16_t	na_reserved1;
	uint16_t	na_flags;
	uint16_t	na_srr_rxid;
	uint16_t	na_status;
	uint8_t		na_status_subcode;
	uint8_t		na_fwhandle;
	uint32_t	na_rxid;
	uint16_t	na_srr_reloff_lo;
	uint16_t	na_srr_reloff_hi;
	uint16_t	na_srr_iu;
	uint16_t	na_srr_flags;
	uint8_t		na_reserved3[18];
	uint8_t		na_reserved4;
	uint8_t		na_vpidx;
	uint8_t		na_srr_reject_vunique;
	uint8_t		na_srr_reject_explanation;
	uint8_t		na_srr_reject_code;
	uint8_t		na_reserved5;
	uint8_t		na_reserved6[6];
	uint16_t	na_oxid;
} na_fcentry_24xx_t;

/*
 * Accept Target I/O Entry structure
 */
#define ATIO_CDBLEN	26

typedef struct {
	isphdr_t	at_header;
	uint16_t	at_reserved;
	uint16_t	at_handle;
	uint8_t		at_lun;		/* lun */
	uint8_t		at_iid;		/* initiator */
	uint8_t		at_cdblen; 	/* cdb length */
	uint8_t		at_tgt;		/* target */
	uint32_t	at_flags;
	uint8_t		at_status;	/* firmware status */
	uint8_t		at_scsi_status;	/* scsi status */
	uint8_t		at_tag_val;	/* tag value */
	uint8_t		at_tag_type;	/* tag type */
	uint8_t		at_cdb[ATIO_CDBLEN];	/* received CDB */
	uint8_t		at_sense[QLTM_SENSELEN];/* suggested sense data */
} at_entry_t;

/*
 * at_flags values
 */
#define AT_NODISC	0x00008000	/* disconnect disabled */
#define AT_TQAE		0x00000002	/* Tagged Queue Action enabled */

/*
 * at_status values
 */
#define AT_PATH_INVALID	0x07	/* ATIO sent to firmware for disabled lun */
#define	AT_RESET	0x0E	/* SCSI Bus Reset Occurred */
#define AT_PHASE_ERROR	0x14	/* Bus phase sequence error */
#define AT_NOCAP	0x16	/* Requested capability not available */
#define AT_BDR_MSG	0x17	/* Bus Device Reset msg received */
#define AT_CDB		0x3D	/* CDB received */
/*
 * Macros to create and fetch and test concatenated handle and tag value macros
 * (SPI only)
 */
#define	AT_MAKE_TAGID(tid, aep)						\
	tid = aep->at_handle;						\
	if (aep->at_flags & AT_TQAE) {					\
		tid |= (aep->at_tag_val << 16);				\
		tid |= (1 << 24);					\
	}

#define	CT_MAKE_TAGID(tid, ct)						\
	tid = ct->ct_fwhandle;						\
	if (ct->ct_flags & CT_TQAE) {					\
		tid |= (ct->ct_tag_val << 16);				\
		tid |= (1 << 24);					\
	}

#define	AT_HAS_TAG(val)		((val) & (1 << 24))
#define	AT_GET_TAG(val)		(((val) >> 16) & 0xff)
#define	AT_GET_HANDLE(val)	((val) & 0xffff)

#define	IN_MAKE_TAGID(tid, inp)						\
	tid = inp->in_seqid;						\
	tid |= (inp->in_tag_val << 16);					\
	tid |= (1 << 24)

/*
 * Accept Target I/O Entry structure, Type 2
 */
#define ATIO2_CDBLEN	16

typedef struct {
	isphdr_t	at_header;
	uint32_t	at_reserved;
	uint8_t		at_lun;		/* lun or reserved */
	uint8_t		at_iid;		/* initiator */
	uint16_t	at_rxid; 	/* response ID */
	uint16_t	at_flags;
	uint16_t	at_status;	/* firmware status */
	uint8_t		at_crn;		/* command reference number */
	uint8_t		at_taskcodes;
	uint8_t		at_taskflags;
	uint8_t		at_execodes;
	uint8_t		at_cdb[ATIO2_CDBLEN];	/* received CDB */
	uint32_t	at_datalen;		/* allocated data len */
	uint16_t	at_scclun;		/* SCC Lun or reserved */
	uint16_t	at_wwpn[4];		/* WWPN of initiator */
	uint16_t	at_reserved2[6];
	uint16_t	at_oxid;
} at2_entry_t;

typedef struct {
	isphdr_t	at_header;
	uint32_t	at_reserved;
	uint16_t	at_iid;		/* initiator */
	uint16_t	at_rxid; 	/* response ID */
	uint16_t	at_flags;
	uint16_t	at_status;	/* firmware status */
	uint8_t		at_crn;		/* command reference number */
	uint8_t		at_taskcodes;
	uint8_t		at_taskflags;
	uint8_t		at_execodes;
	uint8_t		at_cdb[ATIO2_CDBLEN];	/* received CDB */
	uint32_t	at_datalen;		/* allocated data len */
	uint16_t	at_scclun;		/* SCC Lun or reserved */
	uint16_t	at_wwpn[4];		/* WWPN of initiator */
	uint16_t	at_reserved2[6];
	uint16_t	at_oxid;
} at2e_entry_t;

#define	ATIO2_WWPN_OFFSET	0x2A
#define	ATIO2_OXID_OFFSET	0x3E

#define	ATIO2_TC_ATTR_MASK	0x7
#define	ATIO2_TC_ATTR_SIMPLEQ	0
#define	ATIO2_TC_ATTR_HEADOFQ	1
#define	ATIO2_TC_ATTR_ORDERED	2
#define	ATIO2_TC_ATTR_ACAQ	4
#define	ATIO2_TC_ATTR_UNTAGGED	5

#define	ATIO2_EX_WRITE		0x1
#define	ATIO2_EX_READ		0x2
/*
 * Macros to create and fetch and test concatenated handle and tag value macros
 */
#define	AT2_MAKE_TAGID(tid, bus, inst, aep)				\
	tid = aep->at_rxid;						\
	tid |= (((uint64_t)inst) << 32);				\
	tid |= (((uint64_t)bus) << 48)

#define	CT2_MAKE_TAGID(tid, bus, inst, ct)				\
	tid = ct->ct_rxid;						\
	tid |= (((uint64_t)inst) << 32);				\
	tid |= (((uint64_t)(bus & 0xff)) << 48)

#define	AT2_HAS_TAG(val)	1
#define	AT2_GET_TAG(val)	((val) & 0xffffffff)
#define	AT2_GET_INST(val)	(((val) >> 32) & 0xffff)
#define	AT2_GET_HANDLE		AT2_GET_TAG
#define	AT2_GET_BUS(val)	(((val) >> 48) & 0xff)

#define	FC_HAS_TAG	AT2_HAS_TAG
#define	FC_GET_TAG	AT2_GET_TAG
#define	FC_GET_INST	AT2_GET_INST
#define	FC_GET_HANDLE	AT2_GET_HANDLE

#define	IN_FC_MAKE_TAGID(tid, bus, inst, seqid)				\
	tid = seqid;							\
	tid |= (((uint64_t)inst) << 32);				\
	tid |= (((uint64_t)(bus & 0xff)) << 48)

#define	FC_TAG_INSERT_INST(tid, inst)					\
	tid &= ~0x0000ffff00000000ull;					\
	tid |= (((uint64_t)inst) << 32)

/*
 * 24XX ATIO Definition
 *
 * This is *quite* different from other entry types.
 * First of all, it has its own queue it comes in on.
 *
 * Secondly, it doesn't have a normal header.
 *
 * Thirdly, it's just a passthru of the FCP CMND IU
 * which is recorded in big endian mode.
 */
typedef struct {
	uint8_t		at_type;
	uint8_t		at_count;
	/*
	 * Task attribute in high four bits,
	 * the rest is the FCP CMND IU Length.
	 * NB: the command can extend past the
	 * length for a single queue entry.
	 */
	uint16_t	at_ta_len;
	uint32_t	at_rxid;
	fc_hdr_t	at_hdr;
	fcp_cmnd_iu_t	at_cmnd;
} at7_entry_t;
#define	AT7_NORESRC_RXID	0xffffffff


/*
 * Continue Target I/O Entry structure
 * Request from driver. The response from the
 * ISP firmware is the same except that the last 18
 * bytes are overwritten by suggested sense data if
 * the 'autosense valid' bit is set in the status byte.
 */
typedef struct {
	isphdr_t	ct_header;
	uint16_t	ct_syshandle;
	uint16_t	ct_fwhandle;	/* required by f/w */
	uint8_t		ct_lun;	/* lun */
	uint8_t		ct_iid;	/* initiator id */
	uint8_t		ct_reserved2;
	uint8_t		ct_tgt;	/* our target id */
	uint32_t	ct_flags;
	uint8_t 	ct_status;	/* isp status */
	uint8_t 	ct_scsi_status;	/* scsi status */
	uint8_t 	ct_tag_val;	/* tag value */
	uint8_t 	ct_tag_type;	/* tag type */
	uint32_t	ct_xfrlen;	/* transfer length */
	uint32_t	ct_resid;	/* residual length */
	uint16_t	ct_timeout;
	uint16_t	ct_seg_count;
	ispds_t		ct_dataseg[ISP_RQDSEG];
} ct_entry_t;

/*
 * For some of the dual port SCSI adapters, port (bus #) is reported
 * in the MSbit of ct_iid. Bit fields are a bit too awkward here.
 *
 * Note that this does not apply to FC adapters at all which can and
 * do report IIDs between 0x81 && 0xfe (or 0x7ff) which represent devices
 * that have logged in across a SCSI fabric.
 */
#define	GET_IID_VAL(x)		(x & 0x3f)
#define	GET_BUS_VAL(x)		((x >> 7) & 0x1)
#define	SET_IID_VAL(y, x)	y = ((y & ~0x3f) | (x & 0x3f))
#define	SET_BUS_VAL(y, x)	y = ((y & 0x3f) | ((x & 0x1) << 7))

/*
 * ct_flags values
 */
#define CT_TQAE		0x00000002	/* bit  1, Tagged Queue Action enable */
#define CT_DATA_IN	0x00000040	/* bits 6&7, Data direction - *to* initiator */
#define CT_DATA_OUT	0x00000080	/* bits 6&7, Data direction - *from* initiator */
#define CT_NO_DATA	0x000000C0	/* bits 6&7, Data direction */
#define	CT_CCINCR	0x00000100	/* bit 8, autoincrement atio count */
#define CT_DATAMASK	0x000000C0	/* bits 6&7, Data direction */
#define	CT_INISYNCWIDE	0x00004000	/* bit 14, Do Sync/Wide Negotiation */
#define CT_NODISC	0x00008000	/* bit 15, Disconnects disabled */
#define CT_DSDP		0x01000000	/* bit 24, Disable Save Data Pointers */
#define CT_SENDRDP	0x04000000	/* bit 26, Send Restore Pointers msg */
#define CT_SENDSTATUS	0x80000000	/* bit 31, Send SCSI status byte */

/*
 * ct_status values
 * - set by the firmware when it returns the CTIO
 */
#define CT_OK		0x01	/* completed without error */
#define CT_ABORTED	0x02	/* aborted by host */
#define CT_ERR		0x04	/* see sense data for error */
#define CT_INVAL	0x06	/* request for disabled lun */
#define CT_NOPATH	0x07	/* invalid ITL nexus */
#define	CT_INVRXID	0x08	/* (FC only) Invalid RX_ID */
#define	CT_DATA_OVER	0x09	/* (FC only) Data Overrun */
#define CT_RSELTMO	0x0A	/* reselection timeout after 2 tries */
#define CT_TIMEOUT	0x0B	/* timed out */
#define CT_RESET	0x0E	/* SCSI Bus Reset occurred */
#define	CT_PARITY	0x0F	/* Uncorrectable Parity Error */
#define	CT_BUS_ERROR	0x10	/* (FC Only) DMA PCI Error */
#define	CT_PANIC	0x13	/* Unrecoverable Error */
#define CT_PHASE_ERROR	0x14	/* Bus phase sequence error */
#define	CT_DATA_UNDER	0x15	/* (FC only) Data Underrun */
#define CT_BDR_MSG	0x17	/* Bus Device Reset msg received */
#define CT_TERMINATED	0x19	/* due to Terminate Transfer mbox cmd */
#define	CT_PORTUNAVAIL	0x28	/* port not available */
#define	CT_LOGOUT	0x29	/* port logout */
#define	CT_PORTCHANGED	0x2A	/* port changed */
#define	CT_IDE		0x33	/* Initiator Detected Error */
#define CT_NOACK	0x35	/* Outstanding Immed. Notify. entry */
#define	CT_SRR		0x45	/* SRR Received */
#define	CT_LUN_RESET	0x48	/* Lun Reset Received */

#define	CT_HBA_RESET	0xffff	/* pseudo error - command destroyed by HBA reset*/

/*
 * When the firmware returns a CTIO entry, it may overwrite the last
 * part of the structure with sense data. This starts at offset 0x2E
 * into the entry, which is in the middle of ct_dataseg[1]. Rather
 * than define a new struct for this, I'm just using the sense data
 * offset.
 */
#define CTIO_SENSE_OFFSET	0x2E

/*
 * Entry length in u_longs. All entries are the same size so
 * any one will do as the numerator.
 */
#define UINT32_ENTRY_SIZE	(sizeof(at_entry_t)/sizeof(uint32_t))

/*
 * QLA2100 CTIO (type 2) entry
 */
#define	MAXRESPLEN	26
typedef struct {
	isphdr_t	ct_header;
	uint32_t	ct_syshandle;
	uint8_t		ct_lun;		/* lun */
	uint8_t		ct_iid;		/* initiator id */
	uint16_t	ct_rxid;	/* response ID */
	uint16_t	ct_flags;
	uint16_t 	ct_status;	/* isp status */
	uint16_t	ct_timeout;
	uint16_t	ct_seg_count;
	uint32_t	ct_reloff;	/* relative offset */
	uint32_t	ct_resid;	/* residual length */
	union {
		/*
		 * The three different modes that the target driver
		 * can set the CTIO{2,3,4} up as.
		 *
		 * The first is for sending FCP_DATA_IUs as well as
		 * (optionally) sending a terminal SCSI status FCP_RSP_IU.
		 *
		 * The second is for sending SCSI sense data in an FCP_RSP_IU.
		 * Note that no FCP_DATA_IUs will be sent.
		 *
		 * The third is for sending FCP_RSP_IUs as built specifically
		 * in system memory as located by the isp_dataseg.
		 */
		struct {
			uint32_t _reserved;
			uint16_t _reserved2;
			uint16_t ct_scsi_status;
			uint32_t ct_xfrlen;
			union {
				ispds_t ct_dataseg[ISP_RQDSEG_T2];
				ispds64_t ct_dataseg64[ISP_RQDSEG_T3];
				ispdslist_t ct_dslist;
			} u;
		} m0;
		struct {
			uint16_t _reserved;
			uint16_t _reserved2;
			uint16_t ct_senselen;
			uint16_t ct_scsi_status;
			uint16_t ct_resplen;
			uint8_t  ct_resp[MAXRESPLEN];
		} m1;
		struct {
			uint32_t _reserved;
			uint16_t _reserved2;
			uint16_t _reserved3;
			uint32_t ct_datalen;
			union {
				ispds_t	ct_fcp_rsp_iudata_32;
				ispds64_t ct_fcp_rsp_iudata_64;
			} u;
		} m2;
	} rsp;
} ct2_entry_t;

typedef struct {
	isphdr_t	ct_header;
	uint32_t	ct_syshandle;
	uint16_t	ct_iid;		/* initiator id */
	uint16_t	ct_rxid;	/* response ID */
	uint16_t	ct_flags;
	uint16_t 	ct_status;	/* isp status */
	uint16_t	ct_timeout;
	uint16_t	ct_seg_count;
	uint32_t	ct_reloff;	/* relative offset */
	uint32_t	ct_resid;	/* residual length */
	union {
		struct {
			uint32_t _reserved;
			uint16_t _reserved2;
			uint16_t ct_scsi_status;
			uint32_t ct_xfrlen;
			union {
				ispds_t ct_dataseg[ISP_RQDSEG_T2];
				ispds64_t ct_dataseg64[ISP_RQDSEG_T3];
				ispdslist_t ct_dslist;
			} u;
		} m0;
		struct {
			uint16_t _reserved;
			uint16_t _reserved2;
			uint16_t ct_senselen;
			uint16_t ct_scsi_status;
			uint16_t ct_resplen;
			uint8_t  ct_resp[MAXRESPLEN];
		} m1;
		struct {
			uint32_t _reserved;
			uint16_t _reserved2;
			uint16_t _reserved3;
			uint32_t ct_datalen;
			union {
				ispds_t	ct_fcp_rsp_iudata_32;
				ispds64_t ct_fcp_rsp_iudata_64;
			} u;
		} m2;
	} rsp;
} ct2e_entry_t;

/*
 * ct_flags values for CTIO2
 */
#define	CT2_FLAG_MODE0	0x0000
#define	CT2_FLAG_MODE1	0x0001
#define	CT2_FLAG_MODE2	0x0002
#define		CT2_FLAG_MMASK	0x0003
#define CT2_DATA_IN	0x0040	/* *to* initiator */
#define CT2_DATA_OUT	0x0080	/* *from* initiator */
#define CT2_NO_DATA	0x00C0
#define 	CT2_DATAMASK	0x00C0
#define	CT2_CCINCR	0x0100
#define	CT2_FASTPOST	0x0200
#define	CT2_CONFIRM	0x2000
#define	CT2_TERMINATE	0x4000
#define CT2_SENDSTATUS	0x8000

/*
 * ct_status values are (mostly) the same as that for ct_entry.
 */

/*
 * ct_scsi_status values- the low 8 bits are the normal SCSI status
 * we know and love. The upper 8 bits are validity markers for FCP_RSP_IU
 * fields.
 */
#define	CT2_RSPLEN_VALID	0x0100
#define	CT2_SNSLEN_VALID	0x0200
#define	CT2_DATA_OVER		0x0400
#define	CT2_DATA_UNDER		0x0800

/*
 * ISP24XX CTIO
 */
#define	MAXRESPLEN_24XX	24
typedef struct {
	isphdr_t	ct_header;
	uint32_t	ct_syshandle;
	uint16_t	ct_nphdl;	/* status on returned CTIOs */
	uint16_t	ct_timeout;
	uint16_t	ct_seg_count;
	uint8_t		ct_vpidx;
	uint8_t		ct_xflags;
	uint16_t	ct_iid_lo;	/* low 16 bits of portid */
	uint8_t		ct_iid_hi;	/* hi 8 bits of portid */
	uint8_t		ct_reserved;
	uint32_t	ct_rxid;
	uint16_t	ct_senselen;	/* mode 1 only */
	uint16_t	ct_flags;
	uint32_t	ct_resid;	/* residual length */
	uint16_t	ct_oxid;
	uint16_t	ct_scsi_status;	/* modes 0 && 1 only */
	union {
		struct {
			uint32_t	reloff;
			uint32_t	reserved0;
			uint32_t	ct_xfrlen;
			uint32_t	reserved1;
			ispds64_t	ds;
		} m0;
		struct {
			uint16_t ct_resplen;
			uint16_t reserved;
			uint8_t  ct_resp[MAXRESPLEN_24XX];
		} m1;
		struct {
			uint32_t reserved0;
			uint32_t reserved1;
			uint32_t ct_datalen;
			uint32_t reserved2;
			ispds64_t ct_fcp_rsp_iudata;
		} m2;
	} rsp;
} ct7_entry_t;

/*
 * ct_flags values for CTIO7
 */
#define CT7_NO_DATA	0x0000
#define CT7_DATA_OUT	0x0001	/* *from* initiator */
#define CT7_DATA_IN	0x0002	/* *to* initiator */
#define 	CT7_DATAMASK	0x3
#define	CT7_DSD_ENABLE	0x0004
#define	CT7_CONF_STSFD	0x0010
#define	CT7_EXPLCT_CONF	0x0020
#define	CT7_FLAG_MODE0	0x0000
#define	CT7_FLAG_MODE1	0x0040
#define	CT7_FLAG_MODE2	0x0080
#define		CT7_FLAG_MMASK	0x00C0
#define	CT7_NOACK	    0x0100
#define	CT7_TASK_ATTR_SHIFT	9
#define	CT7_CONFIRM     0x2000
#define	CT7_TERMINATE	0x4000
#define CT7_SENDSTATUS	0x8000

/*
 * Type 7 CTIO status codes
 */
#define CT7_OK		0x01	/* completed without error */
#define CT7_ABORTED	0x02	/* aborted by host */
#define CT7_ERR		0x04	/* see sense data for error */
#define CT7_INVAL	0x06	/* request for disabled lun */
#define	CT7_INVRXID	0x08	/* Invalid RX_ID */
#define	CT7_DATA_OVER	0x09	/* Data Overrun */
#define CT7_TIMEOUT	0x0B	/* timed out */
#define CT7_RESET	0x0E	/* LIP Rset Received */
#define	CT7_BUS_ERROR	0x10	/* DMA PCI Error */
#define	CT7_REASSY_ERR	0x11	/* DMA reassembly error */
#define	CT7_DATA_UNDER	0x15	/* Data Underrun */
#define	CT7_PORTUNAVAIL	0x28	/* port not available */
#define	CT7_LOGOUT	0x29	/* port logout */
#define	CT7_PORTCHANGED	0x2A	/* port changed */
#define	CT7_SRR		0x45	/* SRR Received */

/*
 * Other 24XX related target IOCBs
 */

/*
 * ABTS Received
 */
typedef struct {
	isphdr_t	abts_header;
	uint8_t		abts_reserved0[6];
	uint16_t	abts_nphdl;
	uint16_t	abts_reserved1;
	uint16_t	abts_sof;
	uint32_t	abts_rxid_abts;
	uint16_t	abts_did_lo;
	uint8_t		abts_did_hi;
	uint8_t		abts_r_ctl;
	uint16_t	abts_sid_lo;
	uint8_t		abts_sid_hi;
	uint8_t		abts_cs_ctl;
	uint16_t	abts_fs_ctl;
	uint8_t		abts_f_ctl;
	uint8_t		abts_type;
	uint16_t	abts_seq_cnt;
	uint8_t		abts_df_ctl;
	uint8_t		abts_seq_id;
	uint16_t	abts_rx_id;
	uint16_t	abts_ox_id;
	uint32_t	abts_param;
	uint8_t		abts_reserved2[16];
	uint32_t	abts_rxid_task;
} abts_t;

typedef struct {
	isphdr_t	abts_rsp_header;
	uint32_t	abts_rsp_handle;
	uint16_t	abts_rsp_status;
	uint16_t	abts_rsp_nphdl;
	uint16_t	abts_rsp_ctl_flags;
	uint16_t	abts_rsp_sof;
	uint32_t	abts_rsp_rxid_abts;
	uint16_t	abts_rsp_did_lo;
	uint8_t		abts_rsp_did_hi;
	uint8_t		abts_rsp_r_ctl;
	uint16_t	abts_rsp_sid_lo;
	uint8_t		abts_rsp_sid_hi;
	uint8_t		abts_rsp_cs_ctl;
	uint16_t	abts_rsp_f_ctl_lo;
	uint8_t		abts_rsp_f_ctl_hi;
	uint8_t		abts_rsp_type;
	uint16_t	abts_rsp_seq_cnt;
	uint8_t		abts_rsp_df_ctl;
	uint8_t		abts_rsp_seq_id;
	uint16_t	abts_rsp_rx_id;
	uint16_t	abts_rsp_ox_id;
	uint32_t	abts_rsp_param;
	union {
		struct {
			uint16_t reserved;
			uint8_t	last_seq_id;
			uint8_t seq_id_valid;
			uint16_t aborted_rx_id;
			uint16_t aborted_ox_id;
			uint16_t high_seq_cnt;
			uint16_t low_seq_cnt;
			uint8_t reserved2[4];
		} ba_acc;
		struct {
			uint8_t vendor_unique;
			uint8_t	explanation;
			uint8_t reason;
			uint8_t reserved;
			uint8_t reserved2[12];
		} ba_rjt;
		struct {
			uint8_t reserved[8];
			uint32_t subcode1;
			uint32_t subcode2;
		} rsp;
		uint8_t reserved[16];
	} abts_rsp_payload;
	uint32_t	abts_rsp_rxid_task;
} abts_rsp_t;

/* terminate this ABTS exchange */
#define	ISP24XX_ABTS_RSP_TERMINATE	0x01

#define	ISP24XX_ABTS_RSP_COMPLETE	0x00
#define	ISP24XX_ABTS_RSP_RESET		0x04
#define	ISP24XX_ABTS_RSP_ABORTED	0x05
#define	ISP24XX_ABTS_RSP_TIMEOUT	0x06
#define	ISP24XX_ABTS_RSP_INVXID		0x08
#define	ISP24XX_ABTS_RSP_LOGOUT		0x29
#define	ISP24XX_ABTS_RSP_SUBCODE	0x31

#define	ISP24XX_NO_TASK			0xffffffff

/*
 * Miscellaneous
 *
 * These are the limits of the number of dma segments we
 * can deal with based not on the size of the segment counter
 * (which is 16 bits), but on the size of the number of 
 * queue entries field (which is 8 bits). We assume no
 * segments in the first queue entry, so we can either
 * have 7 dma segments per continuation entry or 5
 * (for 64 bit dma).. multiplying out by 254....
 */
#define	ISP_NSEG_MAX	1778
#define	ISP_NSEG64_MAX	1270

#endif	/* _ISPMBOX_H */
