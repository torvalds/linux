/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2007-2009 Marvell Semiconductor, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

/*
 * Definitions for the Marvell Wireless LAN controller Hardware Access Layer.
 */
#ifndef _MWL_HALREG_H_
#define _MWL_HALREG_H_

#define MWL_ANT_INFO_SUPPORT		/* per-antenna data in rx descriptor */

#define	MACREG_REG_TSF_LOW	0xa600		/* TSF lo */
#define	MACREG_REG_TSF_HIGH	0xa604		/* TSF hi */
#define	MACREG_REG_CHIP_REV	0xa814		/* chip rev */

//          Map to 0x80000000 (Bus control) on BAR0
#define MACREG_REG_H2A_INTERRUPT_EVENTS     	0x00000C18 // (From host to ARM)
#define MACREG_REG_H2A_INTERRUPT_CAUSE      	0x00000C1C // (From host to ARM)
#define MACREG_REG_H2A_INTERRUPT_MASK       	0x00000C20 // (From host to ARM)
#define MACREG_REG_H2A_INTERRUPT_CLEAR_SEL      0x00000C24 // (From host to ARM)
#define MACREG_REG_H2A_INTERRUPT_STATUS_MASK	0x00000C28 // (From host to ARM)

#define MACREG_REG_A2H_INTERRUPT_EVENTS     	0x00000C2C // (From ARM to host)
#define MACREG_REG_A2H_INTERRUPT_CAUSE      	0x00000C30 // (From ARM to host)
#define MACREG_REG_A2H_INTERRUPT_MASK       	0x00000C34 // (From ARM to host)
#define MACREG_REG_A2H_INTERRUPT_CLEAR_SEL      0x00000C38 // (From ARM to host)
#define MACREG_REG_A2H_INTERRUPT_STATUS_MASK    0x00000C3C // (From ARM to host)


//  Map to 0x80000000 on BAR1
#define MACREG_REG_GEN_PTR                  0x00000C10
#define MACREG_REG_INT_CODE                 0x00000C14
#define MACREG_REG_SCRATCH                  0x00000C40
#define MACREG_REG_FW_PRESENT		0x0000BFFC

#define	MACREG_REG_PROMISCUOUS		0xA300

//	Bit definitio for MACREG_REG_A2H_INTERRUPT_CAUSE (A2HRIC)
#define MACREG_A2HRIC_BIT_TX_DONE       0x00000001 // bit 0
#define MACREG_A2HRIC_BIT_RX_RDY        0x00000002 // bit 1
#define MACREG_A2HRIC_BIT_OPC_DONE      0x00000004 // bit 2
#define MACREG_A2HRIC_BIT_MAC_EVENT     0x00000008 // bit 3
#define MACREG_A2HRIC_BIT_RX_PROBLEM    0x00000010 // bit 4

#define MACREG_A2HRIC_BIT_RADIO_OFF     0x00000020 // bit 5
#define MACREG_A2HRIC_BIT_RADIO_ON      0x00000040 // bit 6

#define MACREG_A2HRIC_BIT_RADAR_DETECT  0x00000080 // bit 7

#define MACREG_A2HRIC_BIT_ICV_ERROR     0x00000100 // bit 8
#define MACREG_A2HRIC_BIT_MIC_ERROR     0x00000200 // bit 9
#define MACREG_A2HRIC_BIT_QUEUE_EMPTY	0x00004000
#define MACREG_A2HRIC_BIT_QUEUE_FULL	0x00000800
#define MACREG_A2HRIC_BIT_CHAN_SWITCH   0x00001000
#define MACREG_A2HRIC_BIT_TX_WATCHDOG	0x00002000
#define MACREG_A2HRIC_BIT_BA_WATCHDOG	0x00000400
#define	MACREQ_A2HRIC_BIT_TX_ACK	0x00008000
#define ISR_SRC_BITS        ((MACREG_A2HRIC_BIT_RX_RDY)   | \
                             (MACREG_A2HRIC_BIT_TX_DONE)  | \
                             (MACREG_A2HRIC_BIT_OPC_DONE) | \
                             (MACREG_A2HRIC_BIT_MAC_EVENT)| \
                             (MACREG_A2HRIC_BIT_MIC_ERROR)| \
                             (MACREG_A2HRIC_BIT_ICV_ERROR)| \
                             (MACREG_A2HRIC_BIT_RADAR_DETECT)| \
                             (MACREG_A2HRIC_BIT_CHAN_SWITCH)| \
                             (MACREG_A2HRIC_BIT_TX_WATCHDOG)| \
                             (MACREG_A2HRIC_BIT_QUEUE_EMPTY)| \
                             (MACREG_A2HRIC_BIT_BA_WATCHDOG)| \
			     (MACREQ_A2HRIC_BIT_TX_ACK))

#define MACREG_A2HRIC_BIT_MASK      ISR_SRC_BITS                             


//	Bit definitio for MACREG_REG_H2A_INTERRUPT_CAUSE (H2ARIC)
#define MACREG_H2ARIC_BIT_PPA_READY	0x00000001 // bit 0
#define MACREG_H2ARIC_BIT_DOOR_BELL	0x00000002 // bit 1
#define ISR_RESET           				(1<<15)

//	INT code register event definition
#define MACREG_INT_CODE_CMD_FINISHED        0x00000005

/*
 * Host/Firmware Interface definitions.
 */

/**
 * Define total number of TX queues in the shared memory.
 * This count includes the EDCA queues, Block Ack queues, and HCCA queues
 * In addition to this, there could be a management packet queue some 
 * time in the future
 */
#define NUM_EDCA_QUEUES		4
#define NUM_HCCA_QUEUES		0
#define NUM_BA_QUEUES		0
#define NUM_MGMT_QUEUES		0
#define	NUM_ACK_EVENT_QUEUE	1
#define TOTAL_TX_QUEUES \
	(NUM_EDCA_QUEUES + NUM_HCCA_QUEUES + NUM_BA_QUEUES + NUM_MGMT_QUEUES + NUM_ACK_EVENT_QUEUE)
#define MAX_TXWCB_QUEUES	TOTAL_TX_QUEUES - NUM_ACK_EVENT_QUEUE
#define MAX_RXWCB_QUEUES	1

//=============================================================================
//          PUBLIC DEFINITIONS
//=============================================================================

#define RATE_INDEX_MAX_ARRAY        14
#define WOW_MAX_STATION         32

/*
 * Hardware tx/rx descriptors.
 *
 * NB: tx descriptor size must match f/w expected size
 * because f/w prefetch's the next descriptor linearly
 * and doesn't chase the next pointer.
 */
struct mwl_txdesc {
	uint32_t	Status;
#define	EAGLE_TXD_STATUS_IDLE		0x00000000
#define	EAGLE_TXD_STATUS_USED		0x00000001 
#define	EAGLE_TXD_STATUS_OK		0x00000001
#define	EAGLE_TXD_STATUS_OK_RETRY	0x00000002
#define	EAGLE_TXD_STATUS_OK_MORE_RETRY	0x00000004
#define	EAGLE_TXD_STATUS_MULTICAST_TX	0x00000008
#define	EAGLE_TXD_STATUS_BROADCAST_TX	0x00000010
#define	EAGLE_TXD_STATUS_FAILED_LINK_ERROR		0x00000020
#define	EAGLE_TXD_STATUS_FAILED_EXCEED_LIMIT		0x00000040
#define	EAGLE_TXD_STATUS_FAILED_XRETRY	EAGLE_TXD_STATUS_FAILED_EXCEED_LIMIT
#define	EAGLE_TXD_STATUS_FAILED_AGING	0x00000080
#define	EAGLE_TXD_STATUS_FW_OWNED	0x80000000
	uint8_t		DataRate;
	uint8_t		TxPriority;
	uint16_t	QosCtrl;
	uint32_t	PktPtr;
	uint16_t	PktLen;
	uint8_t		DestAddr[6];
	uint32_t	pPhysNext;
	uint32_t	SapPktInfo;
#define	EAGLE_TXD_MODE_BONLY	1
#define	EAGLE_TXD_MODE_GONLY	2
#define	EAGLE_TXD_MODE_BG	3
#define	EAGLE_TXD_MODE_NONLY	4
#define	EAGLE_TXD_MODE_BN	5
#define	EAGLE_TXD_MODE_GN	6
#define	EAGLE_TXD_MODE_BGN	7
#define	EAGLE_TXD_MODE_AONLY	8
#define	EAGLE_TXD_MODE_AG	10
#define	EAGLE_TXD_MODE_AN	12
	uint16_t	Format;
#define	EAGLE_TXD_FORMAT	0x0001	/* frame format/rate */
#define	EAGLE_TXD_FORMAT_LEGACY	0x0000	/* legacy rate frame */
#define	EAGLE_TXD_FORMAT_HT	0x0001	/* HT rate frame */
#define	EAGLE_TXD_GI		0x0002	/* guard interval */
#define	EAGLE_TXD_GI_SHORT	0x0002	/* short guard interval */
#define	EAGLE_TXD_GI_LONG	0x0000	/* long guard interval */
#define	EAGLE_TXD_CHW		0x0004	/* channel width */
#define	EAGLE_TXD_CHW_20	0x0000	/* 20MHz channel width */
#define	EAGLE_TXD_CHW_40	0x0004	/* 40MHz channel width */
#define	EAGLE_TXD_RATE		0x01f8	/* tx rate (legacy)/ MCS */
#define	EAGLE_TXD_RATE_S	3
#define	EAGLE_TXD_ADV		0x0600	/* advanced coding */
#define	EAGLE_TXD_ADV_S		9
#define	EAGLE_TXD_ADV_NONE	0x0000
#define	EAGLE_TXD_ADV_LDPC	0x0200
#define	EAGLE_TXD_ADV_RS	0x0400
/* NB: 3 is reserved */
#define	EAGLE_TXD_ANTENNA	0x1800	/* antenna select */
#define	EAGLE_TXD_ANTENNA_S	11
#define	EAGLE_TXD_EXTCHAN	0x6000	/* extension channel */
#define	EAGLE_TXD_EXTCHAN_S	13
#define	EAGLE_TXD_EXTCHAN_HI	0x0000	/* above */
#define	EAGLE_TXD_EXTCHAN_LO	0x2000	/* below */
#define	EAGLE_TXD_PREAMBLE	0x8000
#define	EAGLE_TXD_PREAMBLE_SHORT 0x8000	/* short preamble */
#define	EAGLE_TXD_PREAMBLE_LONG 0x0000	/* long preamble */
	uint16_t	pad;		/* align to 4-byte boundary */
#define	EAGLE_TXD_FIXED_RATE	0x0100	/* get tx rate from Format */
#define	EAGLE_TXD_DONT_AGGR	0x0200	/* don't aggregate frame */
	uint32_t	ack_wcb_addr;
} __packed;

struct mwl_ant_info {
	uint8_t		rssi_a;	/* RSSI for antenna A */
	uint8_t		rssi_b;	/* RSSI for antenna B */
	uint8_t		rssi_c;	/* RSSI for antenna C */
	uint8_t		rsvd1;	/* Reserved */
	uint8_t		nf_a;	/* Noise floor for antenna A */
	uint8_t		nf_b;	/* Noise floor for antenna B */
	uint8_t		nf_c;	/* Noise floor for antenna C */
	uint8_t		rsvd2;	/* Reserved */
	uint8_t		nf;		/* Noise floor */
	uint8_t		rsvd3[3];   /* Reserved - To make word aligned */
} __packed;

struct mwl_rxdesc {
	uint8_t		RxControl;	/* control element */
#define	EAGLE_RXD_CTRL_DRIVER_OWN	0x00
#define	EAGLE_RXD_CTRL_OS_OWN		0x04
#define	EAGLE_RXD_CTRL_DMA_OWN		0x80
	uint8_t		RSSI;		/* received signal strengt indication */
	uint8_t		Status;		/* status field w/ USED bit */
#define	EAGLE_RXD_STATUS_IDLE		0x00
#define	EAGLE_RXD_STATUS_OK		0x01
#define	EAGLE_RXD_STATUS_MULTICAST_RX	0x02
#define	EAGLE_RXD_STATUS_BROADCAST_RX	0x04
#define	EAGLE_RXD_STATUS_FRAGMENT_RX	0x08
#define	EAGLE_RXD_STATUS_GENERAL_DECRYPT_ERR	0xff
#define	EAGLE_RXD_STATUS_DECRYPT_ERR_MASK	0x80
#define	EAGLE_RXD_STATUS_TKIP_MIC_DECRYPT_ERR	0x02
#define	EAGLE_RXD_STATUS_WEP_ICV_DECRYPT_ERR	0x04
#define	EAGLE_RXD_STATUS_TKIP_ICV_DECRYPT_ERR	0x08
	uint8_t		Channel;	/* channel # pkt received on */
	uint16_t	PktLen;		/* total length of received data */
	uint8_t		SQ2;		/* not used */
	uint8_t		Rate;		/* received data rate */
	uint32_t	pPhysBuffData;	/* physical address of payload data */
	uint32_t	pPhysNext;	/* physical address of next RX desc */ 
	uint16_t	QosCtrl;	/* received QosCtrl field variable */
	uint16_t	HtSig2;		/* like name states */
#ifdef MWL_ANT_INFO_SUPPORT
	struct mwl_ant_info ai;		/* antenna info */
#endif
} __packed;

/*
//          Define OpMode for SoftAP/Station mode
//
//  The following mode signature has to be written to PCI scratch register#0
//  right after successfully downloading the last block of firmware and
//  before waiting for firmware ready signature
 */
#define HostCmd_STA_MODE     0x5A
#define HostCmd_SOFTAP_MODE  0xA5

#define HostCmd_STA_FWRDY_SIGNATURE     0xF0F1F2F4
#define HostCmd_SOFTAP_FWRDY_SIGNATURE  0xF1F2F4A5

//***************************************************************************
//***************************************************************************

//***************************************************************************

#define HostCmd_CMD_CODE_DNLD                   0x0001
#define HostCmd_CMD_GET_HW_SPEC                 0x0003
#define HostCmd_CMD_SET_HW_SPEC			0x0004
#define HostCmd_CMD_MAC_MULTICAST_ADR           0x0010
#define HostCmd_CMD_802_11_GET_STAT             0x0014
#define HostCmd_CMD_MAC_REG_ACCESS              0x0019
#define HostCmd_CMD_BBP_REG_ACCESS              0x001a
#define HostCmd_CMD_RF_REG_ACCESS               0x001b
#define HostCmd_CMD_802_11_RADIO_CONTROL        0x001c
#define HostCmd_CMD_802_11_RF_TX_POWER          0x001e
#define HostCmd_CMD_802_11_RF_ANTENNA           0x0020
#define HostCmd_CMD_SET_BEACON                  0x0100
#define HostCmd_CMD_SET_AID                     0x010d
#define HostCmd_CMD_SET_RF_CHANNEL              0x010a
#define HostCmd_CMD_SET_INFRA_MODE              0x010e
#define HostCmd_CMD_SET_G_PROTECT_FLAG          0x010f
#define HostCmd_CMD_802_11_RTS_THSD             0x0113
#define HostCmd_CMD_802_11_SET_SLOT             0x0114

#define HostCmd_CMD_802_11H_DETECT_RADAR	0x0120
#define HostCmd_CMD_SET_WMM_MODE                0x0123
#define HostCmd_CMD_HT_GUARD_INTERVAL		0x0124
#define HostCmd_CMD_SET_FIXED_RATE              0x0126 
#define HostCmd_CMD_SET_LINKADAPT_CS_MODE	0x0129
#define HostCmd_CMD_SET_MAC_ADDR                0x0202
#define HostCmd_CMD_SET_RATE_ADAPT_MODE		0x0203
#define HostCmd_CMD_GET_WATCHDOG_BITMAP		0x0205

//SoftAP command code
#define HostCmd_CMD_BSS_START                   0x1100	
#define HostCmd_CMD_SET_NEW_STN              	0x1111
#define HostCmd_CMD_SET_KEEP_ALIVE           	0x1112
#define HostCmd_CMD_SET_APMODE           	0x1114
#define HostCmd_CMD_SET_SWITCH_CHANNEL          0x1121

/*
	@HWENCR@
	Command to update firmware encryption keys.
*/
#define HostCmd_CMD_UPDATE_ENCRYPTION		0x1122
/*
	@11E-BA@
	Command to create/destroy block ACK
*/
#define HostCmd_CMD_BASTREAM			0x1125
#define HostCmd_CMD_SET_RIFS                	0x1126
#define HostCmd_CMD_SET_N_PROTECT_FLAG          0x1131
#define HostCmd_CMD_SET_N_PROTECT_OPMODE        0x1132
#define HostCmd_CMD_SET_OPTIMIZATION_LEVEL      0x1133
#define HostCmd_CMD_GET_CALTABLE                0x1134
#define HostCmd_CMD_SET_MIMOPSHT                0x1135
#define HostCmd_CMD_GET_BEACON                  0x1138
#define HostCmd_CMD_SET_REGION_CODE            0x1139
#define HostCmd_CMD_SET_POWERSAVESTATION	0x1140
#define HostCmd_CMD_SET_TIM			0x1141
#define HostCmd_CMD_GET_TIM			0x1142
#define	HostCmd_CMD_GET_SEQNO			0x1143
#define	HostCmd_CMD_DWDS_ENABLE			0x1144
#define	HostCmd_CMD_AMPDU_RETRY_RATEDROP_MODE	0x1145
#define	HostCmd_CMD_CFEND_ENABLE		0x1146

/*
//          Define general result code for each command
 */
#define HostCmd_RESULT_OK                       0x0000 // OK
#define HostCmd_RESULT_ERROR                    0x0001 // Genenral error
#define HostCmd_RESULT_NOT_SUPPORT              0x0002 // Command is not valid
#define HostCmd_RESULT_PENDING                  0x0003 // Command is pending (will be processed)
#define HostCmd_RESULT_BUSY                     0x0004 // System is busy (command ignored)
#define HostCmd_RESULT_PARTIAL_DATA             0x0005 // Data buffer is not big enough


/*
//          Definition of action or option for each command
//
//          Define general purpose action
 */
#define HostCmd_ACT_GEN_READ                    0x0000
#define HostCmd_ACT_GEN_WRITE                   0x0001
#define HostCmd_ACT_GEN_GET                     0x0000
#define HostCmd_ACT_GEN_SET                     0x0001
#define HostCmd_ACT_GEN_OFF                     0x0000
#define HostCmd_ACT_GEN_ON                      0x0001

#define HostCmd_ACT_DIFF_CHANNEL                0x0002
#define HostCmd_ACT_GEN_SET_LIST                0x0002

//          Define action or option for HostCmd_FW_USE_FIXED_RATE
#define HostCmd_ACT_USE_FIXED_RATE              0x0001
#define HostCmd_ACT_NOT_USE_FIXED_RATE          0x0002

//          Define action or option for HostCmd_CMD_802_11_SET_WEP
//#define HostCmd_ACT_ENABLE                    0x0001 // Use MAC control for WEP on/off
//#define HostCmd_ACT_DISABLE                   0x0000
#define HostCmd_ACT_ADD                         0x0002
#define HostCmd_ACT_REMOVE                      0x0004
#define HostCmd_ACT_USE_DEFAULT                 0x0008

#define HostCmd_TYPE_WEP_40_BIT                 0x0001 // 40 bit
#define HostCmd_TYPE_WEP_104_BIT                0x0002 // 104 bit
#define HostCmd_TYPE_WEP_128_BIT                0x0003 // 128 bit
#define HostCmd_TYPE_WEP_TX_KEY                 0x0004 // TX WEP

#define HostCmd_NUM_OF_WEP_KEYS                 4

#define HostCmd_WEP_KEY_INDEX_MASK              0x3fffffff


//          Define action or option for HostCmd_CMD_802_11_RESET
#define HostCmd_ACT_HALT                        0x0001
#define HostCmd_ACT_RESTART                     0x0002

//          Define action or option for HostCmd_CMD_802_11_RADIO_CONTROL 
#define HostCmd_TYPE_AUTO_PREAMBLE              0x0001
#define HostCmd_TYPE_SHORT_PREAMBLE             0x0002
#define HostCmd_TYPE_LONG_PREAMBLE              0x0003

//          Define action or option for CMD_802_11_RF_CHANNEL
#define HostCmd_TYPE_802_11A                    0x0001
#define HostCmd_TYPE_802_11B                    0x0002

//          Define action or option for HostCmd_CMD_802_11_RF_TX_POWER
#define HostCmd_ACT_TX_POWER_OPT_SET_HIGH       0x0003
#define HostCmd_ACT_TX_POWER_OPT_SET_MID        0x0002
#define HostCmd_ACT_TX_POWER_OPT_SET_LOW        0x0001
#define HostCmd_ACT_TX_POWER_OPT_SET_AUTO        0x0000

#define HostCmd_ACT_TX_POWER_LEVEL_MIN          0x000e // in dbm
#define HostCmd_ACT_TX_POWER_LEVEL_GAP          0x0001 // in dbm
//          Define action or option for HostCmd_CMD_802_11_DATA_RATE 
#define HostCmd_ACT_SET_TX_AUTO			0x0000
#define HostCmd_ACT_SET_TX_FIX_RATE		0x0001
#define HostCmd_ACT_GET_TX_RATE			0x0002

#define HostCmd_ACT_SET_RX                      0x0001
#define HostCmd_ACT_SET_TX                      0x0002
#define HostCmd_ACT_SET_BOTH                    0x0003
#define HostCmd_ACT_GET_RX                      0x0004
#define HostCmd_ACT_GET_TX                      0x0008
#define HostCmd_ACT_GET_BOTH                    0x000c

#define TYPE_ANTENNA_DIVERSITY                  0xffff

//          Define action or option for HostCmd_CMD_802_11_PS_MODE 
#define HostCmd_TYPE_CAM                        0x0000
#define HostCmd_TYPE_MAX_PSP                    0x0001
#define HostCmd_TYPE_FAST_PSP                   0x0002

#define HostCmd_CMD_SET_EDCA_PARAMS             0x0115

//=============================================================================
//			HOST COMMAND DEFINITIONS
//=============================================================================

//
//          Definition of data structure for each command
//
//          Define general data structure
typedef struct {
    uint16_t     Cmd;
    uint16_t     Length;
#ifdef MWL_MBSS_SUPPORT
    uint8_t      SeqNum;
    uint8_t      MacId;
#else
    uint16_t     SeqNum;
#endif
    uint16_t     Result; 
} __packed FWCmdHdr;  

typedef struct {
    FWCmdHdr	CmdHdr;
    uint8_t	Version;		// HW revision
    uint8_t	HostIf; 		// Host interface
    uint16_t	NumOfMCastAdr;		// Max. number of Multicast address FW can handle
    uint8_t	PermanentAddr[6];	// MAC address
    uint16_t	RegionCode; 		// Region Code
    uint32_t	FWReleaseNumber;	// 4 byte of FW release number
    uint32_t	ulFwAwakeCookie;	// Firmware awake cookie
    uint32_t	DeviceCaps; 		// Device capabilities (see above)
    uint32_t	RxPdWrPtr;		// Rx shared memory queue
    uint32_t	NumTxQueues;		// # TX queues in WcbBase array
    uint32_t	WcbBase[MAX_TXWCB_QUEUES];	// TX WCB Rings
    uint32_t	Flags;
#define	SET_HW_SPEC_DISABLEMBSS		0x08
#define	SET_HW_SPEC_HOSTFORM_BEACON	0x10
#define	SET_HW_SPEC_HOSTFORM_PROBERESP	0x20
#define	SET_HW_SPEC_HOST_POWERSAVE	0x40
#define	SET_HW_SPEC_HOSTENCRDECR_MGMT	0x80
    uint32_t	TxWcbNumPerQueue;
    uint32_t	TotalRxWcb;
} __packed HostCmd_DS_SET_HW_SPEC;

typedef struct {
    FWCmdHdr    CmdHdr;
    u_int8_t    Version;          /* version of the HW                    */
    u_int8_t    HostIf;           /* host interface                       */
    u_int16_t   NumOfWCB;         /* Max. number of WCB FW can handle     */
    u_int16_t   NumOfMCastAddr;   /* MaxNbr of MC addresses FW can handle */
    u_int8_t    PermanentAddr[6]; /* MAC address programmed in HW         */
    u_int16_t   RegionCode;         
    u_int16_t   NumberOfAntenna;  /* Number of antenna used      */
    u_int32_t   FWReleaseNumber;  /* 4 byte of FW release number */
    u_int32_t   WcbBase0;
    u_int32_t   RxPdWrPtr;
    u_int32_t   RxPdRdPtr;
    u_int32_t   ulFwAwakeCookie;
    u_int32_t   WcbBase1[TOTAL_TX_QUEUES-1];
} __packed HostCmd_DS_GET_HW_SPEC;

typedef struct {
    FWCmdHdr    CmdHdr;
    u_int32_t   Enable;   /* FALSE: Disable or TRUE: Enable */
} __packed HostCmd_DS_BSS_START;


typedef struct {
    u_int8_t    ElemId;
    u_int8_t    Len;
    u_int8_t    OuiType[4]; /* 00:50:f2:01 */
    u_int8_t    Ver[2];
    u_int8_t    GrpKeyCipher[4];
    u_int8_t    PwsKeyCnt[2];
    u_int8_t    PwsKeyCipherList[4];
    u_int8_t    AuthKeyCnt[2];
    u_int8_t    AuthKeyList[4];
} __packed RsnIE_t;

typedef struct {
    u_int8_t    ElemId;
    u_int8_t    Len;
    u_int8_t    Ver[2];
    u_int8_t    GrpKeyCipher[4];
    u_int8_t    PwsKeyCnt[2];
    u_int8_t    PwsKeyCipherList[4];
    u_int8_t    AuthKeyCnt[2];
    u_int8_t    AuthKeyList[4];
    u_int8_t    RsnCap[2];
} __packed Rsn48IE_t;

typedef struct {
    u_int8_t    ElementId;
    u_int8_t    Len;
    u_int8_t    CfpCnt;
    u_int8_t    CfpPeriod;
    u_int16_t   CfpMaxDuration;
    u_int16_t   CfpDurationRemaining;
} __packed CfParams_t;

typedef struct {
    u_int8_t    ElementId;
    u_int8_t    Len;
    u_int16_t   AtimWindow;
} __packed IbssParams_t;

typedef union {
    CfParams_t   CfParamSet;
    IbssParams_t IbssParamSet;
} __packed SsParams_t;

typedef struct {
    u_int8_t    ElementId;
    u_int8_t    Len;
    u_int16_t   DwellTime;
    u_int8_t    HopSet;
    u_int8_t    HopPattern;
    u_int8_t    HopIndex;
} __packed FhParams_t;

typedef struct {
    u_int8_t    ElementId;
    u_int8_t    Len;
    u_int8_t    CurrentChan;
} __packed DsParams_t;

typedef union {
    FhParams_t  FhParamSet;
    DsParams_t  DsParamSet;
} __packed PhyParams_t;

typedef struct {
    u_int8_t    FirstChannelNum;
    u_int8_t    NumOfChannels;
    u_int8_t    MaxTxPwrLevel;
} __packed ChannelInfo_t;

typedef struct {
    u_int8_t       ElementId;
    u_int8_t       Len;
    u_int8_t       CountryStr[3];
    ChannelInfo_t  ChannelInfo[40];
} __packed Country_t;

typedef struct {
    u_int8_t AIFSN : 4;
    u_int8_t ACM : 1;
    u_int8_t ACI : 2;
    u_int8_t rsvd : 1;

}__packed ACIAIFSN_field_t;

typedef  struct {
    u_int8_t ECW_min : 4;
    u_int8_t ECW_max : 4;
}__packed  ECWmin_max_field_t;

typedef struct {
    ACIAIFSN_field_t ACI_AIFSN;
    ECWmin_max_field_t ECW_min_max;
    u_int16_t TXOP_lim;
}__packed  ACparam_rcd_t;

typedef struct {
    u_int8_t    ElementId;
    u_int8_t    Len;
    u_int8_t    OUI[3];
    u_int8_t    Type;
    u_int8_t    Subtype;
    u_int8_t    version;
    u_int8_t    rsvd;
    ACparam_rcd_t AC_BE;
    ACparam_rcd_t AC_BK;
    ACparam_rcd_t AC_VI;
    ACparam_rcd_t AC_VO;
} __packed WMM_param_elem_t ;

typedef struct {
#ifdef MWL_MBSS_SUPPORT
    u_int8_t	StaMacAddr[6];
#endif
    u_int8_t    SsId[32];
    u_int8_t    BssType;
    u_int16_t   BcnPeriod;
    u_int8_t    DtimPeriod;
    SsParams_t  SsParamSet;
    PhyParams_t PhyParamSet;
    u_int16_t   ProbeDelay;
    u_int16_t   CapInfo;		/* see below */
    u_int8_t    BssBasicRateSet[14];
    u_int8_t    OpRateSet[14];
    RsnIE_t     RsnIE;
    Rsn48IE_t   Rsn48IE;
    WMM_param_elem_t  WMMParam;
    Country_t   Country;
    u_int32_t   ApRFType; /* 0->B, 1->G, 2->Mixed, 3->A, 4->11J */
} __packed StartCmd_t;

#define HostCmd_CAPINFO_DEFAULT                0x0000
#define HostCmd_CAPINFO_ESS                    0x0001
#define HostCmd_CAPINFO_IBSS                   0x0002
#define HostCmd_CAPINFO_CF_POLLABLE            0x0004
#define HostCmd_CAPINFO_CF_REQUEST             0x0008
#define HostCmd_CAPINFO_PRIVACY                0x0010
#define HostCmd_CAPINFO_SHORT_PREAMBLE         0x0020
#define HostCmd_CAPINFO_PBCC                   0x0040
#define HostCmd_CAPINFO_CHANNEL_AGILITY        0x0080
#define HostCmd_CAPINFO_SHORT_SLOT             0x0400
#define HostCmd_CAPINFO_DSSS_OFDM              0x2000

typedef struct {
    FWCmdHdr    CmdHdr;
    StartCmd_t  StartCmd;
} __packed HostCmd_DS_AP_BEACON;

typedef struct {
    FWCmdHdr    CmdHdr;
    uint16_t	FrmBodyLen;
    uint8_t	FrmBody[1];		/* NB: variable length */
} __packed HostCmd_DS_SET_BEACON;

//          Define data structure for HostCmd_CMD_MAC_MULTICAST_ADR
typedef struct {
   FWCmdHdr    CmdHdr;
   uint16_t      Action;
   uint16_t      NumOfAdrs;
#define	MWL_HAL_MCAST_MAX	32
   uint8_t       MACList[6*32];
} __packed HostCmd_DS_MAC_MULTICAST_ADR;

// Indicate to FW the current state of AP ERP info
typedef struct {
   FWCmdHdr    CmdHdr;
   uint32_t      GProtectFlag;
} __packed HostCmd_FW_SET_G_PROTECT_FLAG;

typedef struct {
   FWCmdHdr    CmdHdr;
} __packed HostCmd_FW_SET_INFRA_MODE;

//          Define data structure for HostCmd_CMD_802_11_RF_CHANNEL
typedef struct {
   FWCmdHdr    CmdHdr;
   uint16_t      Action;
   uint8_t       CurrentChannel;	/* channel # */
   uint32_t  	ChannelFlags;		/* see below */
} __packed HostCmd_FW_SET_RF_CHANNEL;

/* bits 0-5 specify frequency band */
#define FREQ_BAND_2DOT4GHZ	0x0001
#define FREQ_BAND_4DOT9GHZ	0x0002	/* XXX not implemented */
#define FREQ_BAND_5GHZ      	0x0004
#define FREQ_BAND_5DOT2GHZ	0x0008 	/* XXX not implemented */
/* bits 6-10 specify channel width */
#define CH_AUTO_WIDTH  		0x0000	/* XXX not used? */
#define CH_10_MHz_WIDTH  	0x0040
#define CH_20_MHz_WIDTH  	0x0080
#define CH_40_MHz_WIDTH  	0x0100
/* bits 11-12 specify extension channel */
#define EXT_CH_NONE		0x0000	/* no extension channel */
#define EXT_CH_ABOVE_CTRL_CH 	0x0800	/* extension channel above */
#define EXT_CH_AUTO		0x1000	/* XXX not used? */
#define EXT_CH_BELOW_CTRL_CH 	0x1800	/* extension channel below */
/* bits 13-31 are reserved */

#define FIXED_RATE_WITH_AUTO_RATE_DROP           0
#define FIXED_RATE_WITHOUT_AUTORATE_DROP        1

#define LEGACY_RATE_TYPE   0
#define HT_RATE_TYPE  	1

#define RETRY_COUNT_VALID   0 
#define RETRY_COUNT_INVALID     1

typedef  struct {
    							// lower rate after the retry count
    uint32_t   FixRateType;	//0: legacy, 1: HT
    uint32_t   RetryCountValid; //0: retry count is not valid, 1: use retry count specified
} __packed FIX_RATE_FLAG;

typedef  struct {
    FIX_RATE_FLAG FixRateTypeFlags;
    uint32_t 	FixedRate;	// legacy rate(not index) or an MCS code.
    uint32_t	RetryCount;
} __packed FIXED_RATE_ENTRY;
	
typedef  struct {
    FWCmdHdr	CmdHdr;
    uint32_t    Action;	//HostCmd_ACT_GEN_GET		0x0000
			//HostCmd_ACT_GEN_SET 		0x0001
			//HostCmd_ACT_NOT_USE_FIXED_RATE 0x0002
    uint32_t   	AllowRateDrop;  // use fixed rate specified but firmware can drop to 
    uint32_t	EntryCount;
    FIXED_RATE_ENTRY FixedRateTable[4];
    uint8_t	MulticastRate;
    uint8_t	MultiRateTxType;
    uint8_t	ManagementRate;
} __packed HostCmd_FW_USE_FIXED_RATE;

typedef struct {
    uint32_t   	AllowRateDrop;   
    uint32_t	EntryCount;
    FIXED_RATE_ENTRY FixedRateTable[4];
} __packed USE_FIXED_RATE_INFO;
 
typedef struct {
   FWCmdHdr    CmdHdr;
   uint32_t    Action;
   uint32_t     GIType;  
#define	GI_TYPE_LONG	0x0001
#define	GI_TYPE_SHORT	0x0002
} __packed HostCmd_FW_HT_GUARD_INTERVAL;

typedef struct {
   FWCmdHdr	CmdHdr;
   uint32_t    	Action; 
   uint8_t	RxAntennaMap;
   uint8_t	TxAntennaMap;
} __packed HostCmd_FW_HT_MIMO_CONFIG;

typedef struct {
   FWCmdHdr    CmdHdr;
   uint16_t    Action;
   uint8_t     Slot;   // Slot=0 if regular, Slot=1 if short.
} __packed HostCmd_FW_SET_SLOT;


//          Define data structure for HostCmd_CMD_802_11_GET_STAT
typedef struct {
    FWCmdHdr    CmdHdr;
    uint32_t	TxRetrySuccesses;
    uint32_t	TxMultipleRetrySuccesses;
    uint32_t	TxFailures;
    uint32_t	RTSSuccesses;
    uint32_t	RTSFailures;
    uint32_t	AckFailures;
    uint32_t	RxDuplicateFrames;
    uint32_t	FCSErrorCount;
    uint32_t	TxWatchDogTimeouts;
    uint32_t 	RxOverflows;		//used
    uint32_t 	RxFragErrors;		//used
    uint32_t 	RxMemErrors;		//used
    uint32_t 	PointerErrors;		//used
    uint32_t 	TxUnderflows;		//used
    uint32_t 	TxDone;
    uint32_t 	TxDoneBufTryPut;
    uint32_t 	TxDoneBufPut;
    uint32_t 	Wait4TxBuf;		// Put size of requested buffer in here
    uint32_t 	TxAttempts;
    uint32_t 	TxSuccesses;
    uint32_t 	TxFragments;
    uint32_t 	TxMulticasts;
    uint32_t 	RxNonCtlPkts;
    uint32_t 	RxMulticasts;
    uint32_t 	RxUndecryptableFrames;
    uint32_t 	RxICVErrors;
    uint32_t 	RxExcludedFrames;
} __packed HostCmd_DS_802_11_GET_STAT;


//          Define data structure for HostCmd_CMD_MAC_REG_ACCESS
typedef struct {
   FWCmdHdr    CmdHdr;
   uint16_t      Action;
   uint16_t      Offset;
   uint32_t      Value;
   uint16_t      Reserved;
} __packed HostCmd_DS_MAC_REG_ACCESS;

//          Define data structure for HostCmd_CMD_BBP_REG_ACCESS
typedef struct {
   FWCmdHdr    CmdHdr;
   uint16_t      Action;
   uint16_t      Offset;
   uint8_t       Value;
   uint8_t       Reserverd[3];
} __packed HostCmd_DS_BBP_REG_ACCESS;

//          Define data structure for HostCmd_CMD_RF_REG_ACCESS
typedef struct {
   FWCmdHdr    CmdHdr;
   uint16_t      Action;
   uint16_t      Offset;
   uint8_t       Value;
   uint8_t       Reserverd[3];
} __packed HostCmd_DS_RF_REG_ACCESS;


//          Define data structure for HostCmd_CMD_802_11_RADIO_CONTROL
typedef struct {
   FWCmdHdr    CmdHdr;
   uint16_t      Action;                   
   uint16_t      Control;	// @bit0: 1/0,on/off, @bit1: 1/0, long/short @bit2: 1/0,auto/fix
   uint16_t      RadioOn;
} __packed HostCmd_DS_802_11_RADIO_CONTROL;


#define TX_POWER_LEVEL_TOTAL  8
//          Define data structure for HostCmd_CMD_802_11_RF_TX_POWER
typedef struct {
   FWCmdHdr    CmdHdr;
   uint16_t      Action;
   uint16_t      SupportTxPowerLevel;     
   uint16_t      CurrentTxPowerLevel;     
   uint16_t      Reserved;
   uint16_t      PowerLevelList[TX_POWER_LEVEL_TOTAL];
} __packed HostCmd_DS_802_11_RF_TX_POWER;

//          Define data structure for HostCmd_CMD_802_11_RF_ANTENNA
typedef struct _HostCmd_DS_802_11_RF_ANTENNA {
   FWCmdHdr    CmdHdr;
   uint16_t      Action;
   uint16_t      AntennaMode;             // Number of antennas or 0xffff(diversity)
} __packed HostCmd_DS_802_11_RF_ANTENNA;

//          Define data structure for HostCmd_CMD_802_11_PS_MODE
typedef struct {
   FWCmdHdr    CmdHdr;
   uint16_t      Action;
   uint16_t      PowerMode;               // CAM, Max.PSP or Fast PSP
} __packed HostCmd_DS_802_11_PS_MODE;

typedef struct {
   FWCmdHdr		CmdHdr;
   uint16_t		Action;
   uint16_t		Threshold;
} __packed HostCmd_DS_802_11_RTS_THSD;

// used for stand alone bssid sets/clears
typedef struct {
   FWCmdHdr    CmdHdr;
#ifdef MWL_MBSS_SUPPORT
   uint16_t	 MacType;
#define	WL_MAC_TYPE_PRIMARY_CLIENT	0
#define	WL_MAC_TYPE_SECONDARY_CLIENT	1
#define	WL_MAC_TYPE_PRIMARY_AP		2
#define	WL_MAC_TYPE_SECONDARY_AP	3
#endif
   uint8_t       MacAddr[6];
} __packed HostCmd_DS_SET_MAC,
  HostCmd_FW_SET_BSSID,
  HostCmd_FW_SET_MAC;

// Indicate to FW to send out PS Poll
typedef struct {
   FWCmdHdr    CmdHdr;
   uint32_t      PSPoll;
} __packed HostCmd_FW_TX_POLL;

// used for AID sets/clears
typedef struct {
   FWCmdHdr    CmdHdr;
   uint16_t      AssocID;
   uint8_t       MacAddr[6]; //AP's Mac Address(BSSID)
   uint32_t      GProtection;
   uint8_t       ApRates[ RATE_INDEX_MAX_ARRAY];
} __packed HostCmd_FW_SET_AID;

typedef struct {
   uint32_t	LegacyRateBitMap;
   uint32_t	HTRateBitMap;
   uint16_t	CapInfo;
   uint16_t	HTCapabilitiesInfo;
   uint8_t	MacHTParamInfo;
   uint8_t	Rev;
   struct {
	uint8_t	ControlChan;
	uint8_t	AddChan;
	uint16_t OpMode;
	uint16_t stbc;
   } __packed AddHtInfo;
} __packed PeerInfo_t;

typedef struct {
   FWCmdHdr    CmdHdr;
   uint16_t      AID;
   uint8_t       MacAddr[6]; 
   uint16_t      StnId;
   uint16_t      Action;
   uint16_t      Reserved;
   PeerInfo_t	 PeerInfo;
   uint8_t       Qosinfo;
   uint8_t       isQosSta;
   uint32_t      FwStaPtr;
} __packed HostCmd_FW_SET_NEW_STN;

typedef struct {
   FWCmdHdr    CmdHdr;
   uint8_t           tick;
} __packed HostCmd_FW_SET_KEEP_ALIVE_TICK;

typedef struct {
   FWCmdHdr    CmdHdr;
   uint8_t           QNum;
} __packed HostCmd_FW_SET_RIFS;

typedef struct {
   FWCmdHdr    CmdHdr;
   uint8_t	ApMode;
} __packed HostCmd_FW_SET_APMODE;

typedef struct {
    FWCmdHdr    CmdHdr;
    uint16_t Action;			// see following
    uint16_t RadarTypeCode;
} __packed HostCmd_802_11h_Detect_Radar;

#define DR_DFS_DISABLE				0
#define DR_CHK_CHANNEL_AVAILABLE_START		1
#define DR_CHK_CHANNEL_AVAILABLE_STOP		2
#define DR_IN_SERVICE_MONITOR_START		3

//New Structure for Update Tim 30/9/2003
typedef	struct	{
   FWCmdHdr    CmdHdr;
   uint16_t	   Aid;
   uint32_t      Set;
} __packed HostCmd_UpdateTIM;

typedef struct {
    FWCmdHdr	CmdHdr;
    uint32_t    SsidBroadcastEnable;
} __packed HostCmd_SSID_BROADCAST;

typedef struct {
    FWCmdHdr	CmdHdr;
    uint32_t    WdsEnable;
} __packed HostCmd_WDS;

typedef struct {
    FWCmdHdr    CmdHdr;
    uint32_t    Next11hChannel;
    uint32_t    Mode;
    uint32_t    InitialCount;
	uint32_t ChannelFlags ;
} __packed HostCmd_SET_SWITCH_CHANNEL;

typedef struct {
    FWCmdHdr    CmdHdr;
    uint32_t   	SpectrumMgmt;
} __packed HostCmd_SET_SPECTRUM_MGMT;

typedef struct {
    FWCmdHdr    CmdHdr;
    int32_t    	PowerConstraint;
} __packed HostCmd_SET_POWER_CONSTRAINT;

typedef  struct {
    uint8_t FirstChannelNo;
    uint8_t NoofChannel;
    uint8_t MaxTransmitPw;
} __packed DomainChannelEntry;

typedef  struct {
    uint8_t CountryString[3];
    uint8_t GChannelLen;
    DomainChannelEntry DomainEntryG[1]; /** Assume only 1 G zone **/
    uint8_t AChannelLen;
    DomainChannelEntry DomainEntryA[20]; /** Assume max of 5 A zone **/
} __packed DomainCountryInfo;

typedef struct {
    FWCmdHdr    CmdHdr;
    uint32_t	Action ; // 0 -> unset, 1 ->set
    DomainCountryInfo DomainInfo ;
} __packed HostCmd_SET_COUNTRY_INFO;

typedef struct {
	FWCmdHdr    CmdHdr;
	uint16_t    regionCode ; 
} __packed HostCmd_SET_REGIONCODE_INFO;

// for HostCmd_CMD_SET_WMM_MODE
typedef struct {
    FWCmdHdr    CmdHdr;
    uint16_t    Action;  // 0->unset, 1->set
} __packed HostCmd_FW_SetWMMMode;

typedef struct {
    FWCmdHdr    CmdHdr;
    uint16_t    Action;  // 0->unset, 1->set
    uint16_t    IeListLen;
    uint8_t     IeList[200];
} __packed HostCmd_FW_SetIEs;

#define EDCA_PARAM_SIZE				18
#define BA_PARAM_SIZE				2

typedef struct {
    FWCmdHdr    CmdHdr;
    uint16_t	Action;   //0 = get all, 0x1 =set CWMin/Max,  0x2 = set TXOP , 0x4 =set AIFSN
    uint16_t	TxOP;     // in unit of 32 us
    uint32_t	CWMax;    // 0~15
    uint32_t	CWMin;    // 0~15
    uint8_t	AIFSN;
    uint8_t	TxQNum;   // Tx Queue number.
} __packed HostCmd_FW_SET_EDCA_PARAMS;

/******************************************************************************
	@HWENCR@
	Hardware Encryption related data structures and constant definitions.
	Note that all related changes are marked with the @HWENCR@ tag.
*******************************************************************************/

#define MAX_ENCR_KEY_LENGTH	16	/* max 128 bits - depends on type */
#define MIC_KEY_LENGTH		8	/* size of Tx/Rx MIC key - 8 bytes*/

#define ENCR_KEY_TYPE_ID_WEP	0x00	/* Key type is WEP		*/
#define ENCR_KEY_TYPE_ID_TKIP	0x01	/* Key type is TKIP		*/
#define ENCR_KEY_TYPE_ID_AES	0x02	/* Key type is AES-CCMP	*/

/* flags used in structure - same as driver EKF_XXX flags */
#define ENCR_KEY_FLAG_INUSE	0x00000001	/* indicate key is in use */
#define ENCR_KEY_FLAG_RXGROUPKEY 0x00000002	/* Group key for RX only */
#define ENCR_KEY_FLAG_TXGROUPKEY 0x00000004	/* Group key for TX */
#define ENCR_KEY_FLAG_PAIRWISE	0x00000008	/* pairwise */
#define ENCR_KEY_FLAG_RXONLY	0x00000010	/* only used for RX */
// These flags are new additions - for hardware encryption commands only.
#define ENCR_KEY_FLAG_AUTHENTICATOR	0x00000020	/* Key is for Authenticator */
#define ENCR_KEY_FLAG_TSC_VALID	0x00000040	/* Sequence counters valid */
#define ENCR_KEY_FLAG_WEP_TXKEY	0x01000000	/* Tx key for WEP */
#define ENCR_KEY_FLAG_MICKEY_VALID 0x02000000	/* Tx/Rx MIC keys are valid */

/*
	UPDATE_ENCRYPTION command action type.
*/
typedef enum {
	// request to enable/disable HW encryption
	EncrActionEnableHWEncryption,
	// request to set encryption key
	EncrActionTypeSetKey,
	// request to remove one or more keys
	EncrActionTypeRemoveKey,
	EncrActionTypeSetGroupKey
} ENCR_ACTION_TYPE;

/*
	Key material definitions (for WEP, TKIP, & AES-CCMP)
*/

/* 
	WEP Key material definition
	----------------------------
	WEPKey	--> An array of 'MAX_ENCR_KEY_LENGTH' bytes.
				Note that we do not support 152bit WEP keys
*/
typedef struct {
    // WEP key material (max 128bit)
    uint8_t   KeyMaterial[ MAX_ENCR_KEY_LENGTH ];
} __packed WEP_TYPE_KEY;

/*
	TKIP Key material definition
	----------------------------
	This structure defines TKIP key material. Note that
	the TxMicKey and RxMicKey may or may not be valid.
*/
/* TKIP Sequence counter - 24 bits */
/* Incremented on each fragment MPDU */
typedef struct {
    uint16_t low;
    uint32_t high;
} __packed ENCR_TKIPSEQCNT;

typedef struct {
    // TKIP Key material. Key type (group or pairwise key) is
    // determined by flags in KEY_PARAM_SET structure.
    uint8_t		KeyMaterial[ MAX_ENCR_KEY_LENGTH ];
    uint8_t		TkipTxMicKey[ MIC_KEY_LENGTH ];
    uint8_t		TkipRxMicKey[ MIC_KEY_LENGTH ];
    ENCR_TKIPSEQCNT	TkipRsc;
    ENCR_TKIPSEQCNT	TkipTsc;
} __packed TKIP_TYPE_KEY;

/*
	AES-CCMP Key material definition
	--------------------------------
	This structure defines AES-CCMP key material.
*/
typedef struct {
    // AES Key material
    uint8_t   KeyMaterial[ MAX_ENCR_KEY_LENGTH ];
} __packed AES_TYPE_KEY;

/*
	Encryption key definition.
	--------------------------
	This structure provides all required/essential
	information about the key being set/removed.
*/
typedef struct {
    uint16_t  Length;		// Total length of this structure
    uint16_t  KeyTypeId;	// Key type - WEP, TKIP or AES-CCMP.
    uint32_t  KeyInfo;		// key flags (ENCR_KEY_FLAG_XXX_
    uint32_t  KeyIndex; 	// For WEP only - actual key index
    uint16_t  KeyLen;		// Size of the key
    union {			// Key material (variable size array)
	WEP_TYPE_KEY	WepKey;
	TKIP_TYPE_KEY	TkipKey;
	AES_TYPE_KEY	AesKey;
    }__packed Key;
#ifdef MWL_MBSS_SUPPORT
    uint8_t   Macaddr[6];
#endif
} __packed KEY_PARAM_SET;

/*
	HostCmd_FW_UPDATE_ENCRYPTION
	----------------------------
	Define data structure for updating firmware encryption keys.

*/
typedef struct {
    FWCmdHdr    CmdHdr;
    uint32_t	ActionType;		// ENCR_ACTION_TYPE
    uint32_t	DataLength;		// size of the data buffer attached.
#ifdef MWL_MBSS_SUPPORT
    uint8_t	macaddr[6];
#endif
    uint8_t	ActionData[1];
} __packed HostCmd_FW_UPDATE_ENCRYPTION;


typedef struct {
    FWCmdHdr    CmdHdr;
    uint32_t	ActionType;		// ENCR_ACTION_TYPE
    uint32_t	DataLength;		// size of the data buffer attached.
    KEY_PARAM_SET KeyParam;
#ifndef MWL_MBSS_SUPPORT
    uint8_t     Macaddr[8];		/* XXX? */
#endif
} __packed HostCmd_FW_UPDATE_ENCRYPTION_SET_KEY;

typedef struct {
	// Rate flags - see above.
	uint32_t	Flags;
	// Rate in 500Kbps units.
	uint8_t		RateKbps;
	// 802.11 rate to conversion table index value.
	// This is the value required by the firmware/hardware.
	uint16_t	RateCodeToIndex;
}__packed RATE_INFO;

/*
	UPDATE_STADB command action type.
*/
typedef enum {
	// request to add entry to stainfo db
	StaInfoDbActionAddEntry,
	// request to modify peer entry
	StaInfoDbActionModifyEntry,
	// request to remove peer from stainfo db
	StaInfoDbActionRemoveEntry
}__packed STADB_ACTION_TYPE;

/*
	@11E-BA@
	802.11e/WMM Related command(s)/data structures
*/

// Flag to indicate if the stream is an immediate block ack stream.
// if this bit is not set, the stream is delayed block ack stream.
#define BASTREAM_FLAG_DELAYED_TYPE		0x00
#define BASTREAM_FLAG_IMMEDIATE_TYPE		0x01

// Flag to indicate the direction of the stream (upstream/downstream).
// If this bit is not set, the direction is downstream.
#define BASTREAM_FLAG_DIRECTION_UPSTREAM	0x00
#define BASTREAM_FLAG_DIRECTION_DOWNSTREAM	0x02
#define BASTREAM_FLAG_DIRECTION_DLP		0x04
#define BASTREAM_FLAG_DIRECTION_BOTH		0x06

typedef enum {
	BaCreateStream,
	BaUpdateStream,
	BaDestroyStream,
	BaFlushStream,
	BaCheckCreateStream 
} BASTREAM_ACTION_TYPE;

typedef struct {
	uint32_t	Context;
} __packed BASTREAM_CONTEXT;

// parameters for block ack creation
typedef struct {
	// BA Creation flags - see above
	uint32_t	Flags;
	// idle threshold
	uint32_t	IdleThrs;
	// block ack transmit threshold (after how many pkts should we send BAR?)
	uint32_t	BarThrs;
	// receiver window size
	uint32_t	WindowSize;
	// MAC Address of the BA partner
	uint8_t		PeerMacAddr[6];
	// Dialog Token
	uint8_t		DialogToken;
	//TID for the traffic stream in this BA
	uint8_t		Tid;
	// shared memory queue ID (not sure if this is required)
	uint8_t		QueueId;
	uint8_t         ParamInfo;
	// returned by firmware - firmware context pointer.
	// this context pointer will be passed to firmware for all future commands.
	BASTREAM_CONTEXT FwBaContext;
	uint8_t		ResetSeqNo;  /** 0 or 1**/
	uint16_t	StartSeqNo; 
    
	// proxy sta MAC Address
	uint8_t		StaSrcMacAddr[6];
}__packed BASTREAM_CREATE_STREAM;

// new transmit sequence number information 
typedef struct {
	// BA flags - see above
	uint32_t	Flags;
	// returned by firmware in the create ba stream response
	BASTREAM_CONTEXT FwBaContext;
	// new sequence number for this block ack stream
	uint16_t			 BaSeqNum;
}__packed BASTREAM_UPDATE_STREAM;

typedef struct {
	// BA Stream flags
	uint32_t	 Flags;
	// returned by firmware in the create ba stream response
	BASTREAM_CONTEXT FwBaContext;
}__packed BASTREAM_STREAM_INFO;

//Command to create/destroy block ACK
typedef struct {
	FWCmdHdr	CmdHdr;
	uint32_t	ActionType;
	union
	{
		// information required to create BA Stream...
		BASTREAM_CREATE_STREAM	CreateParams;
		// update starting/new sequence number etc.
		BASTREAM_UPDATE_STREAM	UpdtSeqNum;
		// destroy an existing stream...
		BASTREAM_STREAM_INFO	DestroyParams;
		// destroy an existing stream...
		BASTREAM_STREAM_INFO	FlushParams;
	}__packed BaInfo;
}__packed HostCmd_FW_BASTREAM;

//          Define data structure for HostCmd_CMD_GET_WATCHDOG_BITMAP
typedef struct {
   FWCmdHdr	CmdHdr;
   uint8_t	Watchdogbitmap;		// for SW/BA
} __packed HostCmd_FW_GET_WATCHDOG_BITMAP;



//          Define data structure for HostCmd_CMD_SET_REGION_POWER
typedef struct {
   FWCmdHdr    CmdHdr;
   uint16_t    MaxPowerLevel;     
   uint16_t    Reserved;
} __packed HostCmd_DS_SET_REGION_POWER;

//          Define data structure for HostCmd_CMD_SET_RATE_ADAPT_MODE
typedef struct {
   FWCmdHdr	CmdHdr;
   uint16_t	Action;
   uint16_t	RateAdaptMode;     
} __packed HostCmd_DS_SET_RATE_ADAPT_MODE;

//          Define data structure for HostCmd_CMD_SET_LINKADAPT_CS_MODE
typedef struct {
   FWCmdHdr	CmdHdr;
   uint16_t	Action;
   uint16_t	CSMode;     
} __packed HostCmd_DS_SET_LINKADAPT_CS_MODE;

typedef struct {
   FWCmdHdr    CmdHdr;
   uint32_t     NProtectFlag;
} __packed HostCmd_FW_SET_N_PROTECT_FLAG;

typedef struct {
   FWCmdHdr    CmdHdr;
   uint8_t       NProtectOpMode;
} __packed HostCmd_FW_SET_N_PROTECT_OPMODE;

typedef struct {
   FWCmdHdr    CmdHdr;
   uint8_t       OptLevel;
} __packed HostCmd_FW_SET_OPTIMIZATION_LEVEL;

typedef struct {
   FWCmdHdr    CmdHdr;
   uint8_t     annex; 
   uint8_t     index;
   uint8_t     len;
   uint8_t     Reserverd; 
#define CAL_TBL_SIZE        160
   uint8_t     calTbl[CAL_TBL_SIZE];
} __packed HostCmd_FW_GET_CALTABLE;

typedef struct {
   FWCmdHdr    CmdHdr;
   uint8_t     Addr[6]; 
   uint8_t     Enable;
   uint8_t     Mode;
} __packed HostCmd_FW_SET_MIMOPSHT;

#define MAX_BEACON_SIZE        1024
typedef struct {
   FWCmdHdr    CmdHdr;
   uint16_t    Bcnlen;
   uint8_t     Reserverd[2]; 
   uint8_t     Bcn[MAX_BEACON_SIZE];
} __packed HostCmd_FW_GET_BEACON;

typedef struct {
	FWCmdHdr CmdHdr;
	uint8_t	NumberOfPowersave;
	uint8_t	reserved;
} __packed HostCmd_SET_POWERSAVESTATION;

typedef struct {
	FWCmdHdr CmdHdr;
	uint16_t Aid;
	uint32_t Set;
	uint8_t	reserved;
} __packed HostCmd_SET_TIM;

typedef struct {
	FWCmdHdr CmdHdr;
	uint8_t	TrafficMap[251];
	uint8_t	reserved;
} __packed HostCmd_GET_TIM;

typedef struct {
	FWCmdHdr CmdHdr;
	uint8_t	MacAddr[6]; 
	uint8_t	TID;
	uint16_t SeqNo;
	uint8_t	reserved;
} __packed HostCmd_GET_SEQNO;

typedef struct {
	FWCmdHdr    CmdHdr;
	uint32_t    Enable;    //0 -- Disbale. or 1 -- Enable.
} __packed HostCmd_DWDS_ENABLE;

typedef struct {
	FWCmdHdr    CmdHdr;
	uint16_t    Action;  /* 0: Get. 1:Set */
	uint32_t    Option;  /* 0: default. 1:Aggressive */
	uint32_t    Threshold;  /* Range 0-200, default 8 */
}__packed HostCmd_FW_AMPDU_RETRY_RATEDROP_MODE;

typedef struct {
	FWCmdHdr    CmdHdr;
	uint32_t    Enable; /* 0 -- Disable. or 1 -- Enable */
}__packed HostCmd_CFEND_ENABLE;
#endif /* _MWL_HALREG_H_ */
