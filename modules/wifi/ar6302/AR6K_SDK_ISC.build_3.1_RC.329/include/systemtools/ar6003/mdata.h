/*
 *  Copyright © 2002 Atheros Communications, Inc.,  All Rights Reserved.
 */
/* mdata.h - Type definitions needed for data transfer functions */

/* Copyright 2000, T-Span Systems Corporation */
/* Copyright 2000, Atheros Communications Inc. */

#ifndef	__INCmdatah
#define	__INCmdatah

#ident  "ACI $Id: //depot/sw/releases/olca3.1-RC/include/systemtools/ar6003/mdata.h#1 $, $Header: //depot/sw/releases/olca3.1-RC/include/systemtools/ar6003/mdata.h#1 $"

//#ifdef LINUX
//#undef ARCH_BIG_ENDIAN
//#endif

#if defined(WIN32) || defined(WIN64)
#pragma pack (push, 1)
#endif

#include "wlanproto.h"

#define PPM_DATA_SIZE           56
#define PPM_DATA_SIZE_FALCON_SINGLE_CHAIN           63
#define PPM_DATA_SIZE_FALCON_DUAL_CHAIN             125
#define BUFF_BLOCK_SIZE			0x100  /* size of a buffer block */
//#define MAX_RETRIES             0x000fffff
#define MAX_RETRIES             0x0000000f
#define MDK_PKT_TIMEOUT         0x50
#define MAX_PKT_BODY_SIZE       4031   // 4095 - 64 bytes of fifo used for internal comm (per jsk)
#define STATS_BINS              65     // should be numRateCodes + 1    


//802.11 related 
#define FCS_FIELD				4		   /* size of FCS */				
#define WEP_IV_FIELD			4		   /* wep IV field size */
#define WEP_ICV_FIELD			4		   /* wep ICV field size */
#define WEP_FIELDS	(WEP_IV_FIELD + WEP_ICV_FIELD) /* total size of wep fields needed */
#define ADDRESS2_START			10
#define SEQ_CONTROL_START		22

#define MAX_FRAME_INFO_SIZE    2000

//descriptor offsets and field definitions
#define LINK_POINTER			0
#define BUFFER_POINTER			4
#define FIRST_CONTROL_WORD		8
#define SECOND_CONTROL_WORD		12
#define FIRST_STATUS_WORD		16
#define SECOND_STATUS_WORD		20
#define DESC_DONE				0x00000001
#define DESC_MORE				0x00001000
#define DESC_FRM_RCV_OK			0x00000002
#define DESC_FRM_XMIT_OK		0x00000001
#define DESC_TX_INTER_REQ_START	29
#define DESC_TX_INTER_REQ		(1<<DESC_TX_INTER_REQ_START)
#define DESC_RX_INTER_REQ_START	13
#define DESC_RX_INTER_REQ		(1<<DESC_RX_INTER_REQ_START)
#define DESC_CRC_ERR			0x00000004
#define DESC_FIFO_UNDERRUN		0x00000004
#define DESC_EXCESS_RETIES		0x00000002
#define DESC_DATA_LENGTH_FIELDS 0x00000fff
#define DESC_DECRYPT_ERROR      0x00000010
#define BITS_TO_SHORT_RETRIES	4
#define BITS_TO_LONG_RETRIES	8
#define BITS_TO_TX_XMIT_RATE    18
#define BITS_TO_TX_HDR_LEN      12
#define BITS_TO_TX_ANT_MODE     25
#define BITS_TO_TX_SIG_STRENGTH 13
#define BITS_TO_RX_DATA_RATE    15
#define BITS_TO_RX_SIG_STRENGTH 19
#define BITS_TO_ENCRYPT_VALID   30
#define BITS_TO_ENCRYPT_KEY     13
#define BITS_TO_NOACK           23		// WORD - 3
#define RETRIES_MASK			0xf
#define SIG_STRENGTH_MASK		0xff
#define DATA_RATE_MASK          0xf

//New defines for Venice, may move these somewhere else later
#define THIRD_CONTROL_WORD		    16
#define FOURTH_CONTROL_WORD		    20
#define FIRST_VENICE_STATUS_WORD	24
#define SECOND_VENICE_STATUS_WORD   28

//New defines for Falcon
#define FIFTH_CONTROL_WORD				24
#define SIXTH_CONTROL_WORD				28
#define FALCON_ANT_RSSI_TX_STATUS_WORD	32
#define FIRST_FALCON_TX_STATUS_WORD	    36
#define SECOND_FALCON_TX_STATUS_WORD    40

#define FALCON_ANT_RSSI_RX_STATUS_WORD	16  // new status3
#define FIRST_FALCON_RX_STATUS_WORD	    20  // legacy status1
#define SECOND_FALCON_RX_STATUS_WORD    24  // legacy status2

//Defines for 5416
#define OWL_ANT_RSSI_TX_STATUS_WORD	    60
#define FIRST_5416_TX_STATUS_WORD	    44
#define SECOND_5416_TX_STATUS_WORD      76
#define FIRST_5416_2_TX_STATUS_WORD	    60
#define SECOND_5416_2_TX_STATUS_WORD    92
#define OWL_ANT_RSSI_RX_STATUS_WORD	    32  // new status3
#define OWL_RX_TIME_STAMP_WORD          24  // for tput
#define FIRST_OWL_RX_STATUS_WORD	    20  // legacy status1
#define SECOND_OWL_RX_STATUS_WORD       48  // legacy status2
#define OWL_RX_PRIM_CHAIN_STATUS_WORD   16  // holds Owl control per chain rssi's

#define CHAIN_0_ANT_SEL_S               28
#define CHAIN_1_ANT_SEL_S               29
#define CHAIN_0_ANT_REQ_S               30
#define CHAIN_1_ANT_REQ_S               31

#define CHAIN_STRONG_S                  14

#define ACK_CHAIN_0_ANT_SEL_S               28
#define ACK_CHAIN_1_ANT_SEL_S               29
#define ACK_CHAIN_0_ANT_REQ_S               30
#define ACK_CHAIN_1_ANT_REQ_S               31

#define ACK_CHAIN_STRONG_S                  27

#define DESC_COMPRESSION_DISABLE	0x3
#define DESC_ENABLE_CTS				0x80000000
#define BITS_TO_RTS_CTS_RATE		20
#define BITS_TO_COMPRESSION			25
#define BITS_TO_DATA_TRIES0			16
#define BITS_TO_TX_DATA_RATE0		0
#define OWL_BITS_TO_TX_DATA_RATE0	0
#define BITS_TO_DATA_TRIES1			20
#define BITS_TO_TX_DATA_RATE1		5
#define OWL_BITS_TO_TX_DATA_RATE1   8
#define BITS_TO_DATA_TRIES2			24
#define BITS_TO_TX_DATA_RATE2		10
#define OWL_BITS_TO_TX_DATA_RATE2   16
#define BITS_TO_DATA_TRIES3			28
#define BITS_TO_TX_DATA_RATE3		15
#define OWL_BITS_TO_TX_DATA_RATE3	24
#define BITS_TO_VENICE_NOACK		24
#define OWL_BIT_MORE				12
#define OWL_BIT_MORE_AGGR			29
#define OWL_BIT_IS_AGGR				30
#define VENICE_DESC_DECRYPT_ERROR   0x00000008
#define VENICE_BITS_TO_RX_SIG_STRENGTH 20
#define VENICE_DATA_RATE_MASK       0x1f
#define OWL_DATA_RATE_MASK          0xff

#define OWL_AGG_START_DELIM			1
#define OWL_AGG_PAD_DELIM			7
#define OWL_AGG_DELIM				(OWL_AGG_START_DELIM + OWL_AGG_PAD_DELIM)
#define OWL_BA_STATUS				56
#define OWL_BLK_ACK_0_31			68
#define OWL_BLK_ACK_32_63			72

//End of Venice Defines

#define FALCON_BROADCAST_DEST_INDEX  0  // key cache index for broadcast packets
#define FALCON_UNICAST_DEST_INDEX    4  // key cache index for unicast packets

#ifdef FALCON_ART_CLIENT
#define FALCON_DESC_ADDR_MASK        0xFFFFFFFF  // mem_read to not strip off 31st bit for falcon
#else
#define FALCON_DESC_ADDR_MASK        0xFFFFFFFF  // mem_read to strip off 31st bit for falcon
#endif

#ifdef FALCON_ART_CLIENT
#define FALCON_MEM_ADDR_MASK        0
#else
#define FALCON_MEM_ADDR_MASK        (1 << 31)
#endif

#define OWL_BITS_TO_RX_SIG_STRENGTH 24


#define RX_NORMAL				1
#define RX_FIXED_NUMBER			0

//temp redefinition of some register bits that changed
//#undef F2_STA_ID1_DESC_ANT
//#define F2_STA_ID1_DESC_ANT    0x01000000 // Descriptor selects antenna
//#undef F2_STA_ID1_DEFAULT_ANT
//#define F2_STA_ID1_DEFAULT_ANT 0x02000000 // Toggles the antenna setting

//#undef F2_IMR
//#define F2_IMR  0x00a0  // MAC Primary interrupt mask register

typedef struct mdkAtherosDesc		/* hardware descriptor structure */
	{
	A_UINT32	nextPhysPtr;		/* phys address of next descriptor */
	A_UINT32	bufferPhysPtr;		/* phys address of this descriptior buffer */
	A_UINT32    hwControl[2];		/* hardware control variables */
	A_UINT32    hwStatus[2];		/* hardware status varables */
	A_UINT32    hwExtra[2];			/* Added for Venice support */
	A_UINT32    hwExtraBuffer[24];	/* Extended for Falcon support */
	} MDK_ATHEROS_DESC;

typedef struct mdkVeniceDesc		/* To help with readability also define this for venice */
	{
	A_UINT32	nextPhysPtr;		/* phys address of next descriptor */
	A_UINT32	bufferPhysPtr;		/* phys address of this descriptior buffer */
	A_UINT32    hwControl[4];		/* hardware control variables */
	A_UINT32    hwStatus[2];		/* hardware status varables */
	A_UINT32    hwExtraBuffer[24];	/* Extended for Falcon support */
	} MDK_VENICE_DESC;

typedef struct mdkFalconDesc		/* Falcon WMAC Descr */
	{
	A_UINT32	nextPhysPtr;		/* phys address of next descriptor */
	A_UINT32	bufferPhysPtr;		/* phys address of this descriptior buffer */
	A_UINT32    hwControl[6];		/* hardware control variables */
	A_UINT32    hwStatus[3];		/* hardware status variables */
	A_UINT32    hwExtraBuffer[21];	/* Extended for Falcon support */
	} MDK_FALCON_DESC;

typedef struct mdk5416Desc		    /* 5416 WMAC Descr */
	{
	A_UINT32	nextPhysPtr;		/* phys address of next descriptor */
	A_UINT32	bufferPhysPtr;		/* phys address of this descriptior buffer */
	A_UINT32    hwControl[8];		/* hardware control variables */
	A_UINT32    hwStatus[10];		/* hardware status variables */
	A_UINT32    hwExtraBuffer[12];	/* Extended for Falcon support */
	} MDK_5416_DESC;

typedef struct mdk5416v2Desc		/* 5416 WMAC Descr */ /* used in merlin */
	{
	A_UINT32	nextPhysPtr;		/* phys address of next descriptor */
	A_UINT32	bufferPhysPtr;		/* phys address of this descriptior buffer */
	A_UINT32    hwControl[12];		/* hardware control variables */
	A_UINT32    hwStatus[10];		/* hardware status variables */
	A_UINT32    hwExtraBuffer[8];	/* Extended for Falcon support */
	} MDK_5416_2_DESC;

typedef struct sigStrengthStats
	{
	volatile A_INT8 rxAvrgSignalStrength;	/* Note order is important here */
	volatile A_INT8 rxMinSigStrength;		/* the ave signal strength, max and */
	volatile A_INT8 rxMaxSigStrength;		/* min stats need to be first */
	volatile A_INT32 rxAccumSignalStrength;
	volatile A_UINT32 rxNumSigStrengthSamples;
	} SIG_STRENGTH_STATS;

typedef struct genericBinaryStats
{
	A_UINT32    Count[2];  // increment count for the appropriate index 0 or 1
	A_UINT32    totalNum;
} GENERIC_BINARY_STATS;

typedef struct multAntSigStrengthStats
	{
	volatile A_INT8 rxAvrgSignalStrengthAnt[4];	/* Note order is important here */
	volatile A_INT8 rxMinSigStrengthAnt[4];		/* the ave signal strength, max and */
	volatile A_INT8 rxMaxSigStrengthAnt[4];		/* min stats need to be first */
	volatile A_INT32 rxAccumSignalStrengthAnt[4];
	volatile A_UINT32 rxNumSigStrengthSamplesAnt[4];
	} MULT_ANT_SIG_STRENGTH_STATS;

typedef struct MDataFnTable
{
	int (*sendTxEndPacket)(A_UINT32 devNum,  A_UINT16 queueIndex);
	int (*setupAntenna)(A_UINT32 devNum, A_UINT32 antenna, A_UINT32* antModePtr);
	void (*setRetryLimit)(A_UINT32 devNum, A_UINT16 queueIndex);
	void (*txBeginConfig)(A_UINT32 devNum);
	void (*beginSendStatsPkt)(A_UINT32 devNum, A_UINT32 DescAddress);
	void (*setDescriptor)(A_UINT32, MDK_ATHEROS_DESC*, A_UINT32, A_UINT32, 
								A_UINT32, A_UINT32, A_UINT32);

} MDATA_FNTABLE;

// some temp info need to hold while processing stats
typedef struct rxStatsTempInfo
{
	A_UINT32	descToRead;		// address of next descriptor to check complete	
    A_UINT32    descRate;       // Rate for this descriptor
	A_UINT32	status1;		// first status word of descriptor
	A_UINT32	status2;		// second status word of descriptor
	A_UINT32	bufferAddr;		// adddress of buffer containing packet
	A_UINT32	totalBytes;		// count of number of bytes received
	A_BOOL   	gotHeaderInfo;			// set when get the header info for duplicate processing 
	A_BOOL		controlFrameReceived;	// set to true if this is a control frame 
	A_BOOL		illegalBuffSize;		// set if not able to get addr and sequence because
										// not fully contained in first buffer of packet
										//  may add support for this later 
	WLAN_MACADDR addrPacket;			// mac address expect to see on received packet 
	A_BOOL		 gotMacAddr;			// set to true when we have the address for received packets 
	SEQ_CONTROL	 seqCurrentPacket;		// sequence control of current packet received 
	A_BOOL		 retry;					// retry bit of header 
	A_BOOL		 badPacket;				// set on a bad address match 
	A_UINT32	 lastRxSeqNo;			// Last frame's sequence number 
	A_UINT32	 lastRxFragNo;			// Last frame's fragment number 
	A_UINT32	 oneBeforeLastRxSeqNo;	// Memory of the sequence no. before last
	SIG_STRENGTH_STATS sigStrength[STATS_BINS];		// accumulated signal strength stats
    SIG_STRENGTH_STATS ctlAntStrength[3][STATS_BINS];
    SIG_STRENGTH_STATS extAntStrength[3][STATS_BINS];

    double       ppmAccum[STATS_BINS];              // accumulated ppm value
    A_INT32      ppmSamples[STATS_BINS];            // total number of ppm samples accumulated
	A_UINT16	 mdkPktType;			// type of packet received, ie normal, last or stats packet
    A_UCHAR      *pCompareData;         // pointer to data to use for pkt comparison
	A_UINT16	 qcuIndex;				// QCU index from where the pkt came

	// added for falcon
	A_UINT32     status3;               // multiple antenna rssi status descriptor word
    A_UINT32     status4;               // control channel rssi for 5416+
	MULT_ANT_SIG_STRENGTH_STATS multAntSigStrength[STATS_BINS];  // rssi for 4 ant of falcon
	GENERIC_BINARY_STATS	Chain0AntSel[STATS_BINS];
	GENERIC_BINARY_STATS	Chain1AntSel[STATS_BINS];
	GENERIC_BINARY_STATS	Chain0AntReq[STATS_BINS];
	GENERIC_BINARY_STATS	Chain1AntReq[STATS_BINS];
	GENERIC_BINARY_STATS	ChainStrong[STATS_BINS];
	A_UCHAR		frame_info[MAX_FRAME_INFO_SIZE];
	A_UINT32 	frame_info_len;
	A_UCHAR		ppm_data[MAX_FRAME_INFO_SIZE];
	A_UINT32 	ppm_data_len;
	MDK_ATHEROS_DESC desc;
	A_UINT32    descNumber;
	A_UINT32    descPreviousTime;
	A_UINT32    lastDescRate;
	A_UINT32    bitsReceived;
	A_UINT32    totalTPBytes;		//total number of bytes for tp calculates
	A_UINT32 evm_stream0, evm_stream1;
	// added for tput calculation
	A_UINT32   rxTimeStamp;
} RX_STATS_TEMP_INFO;

typedef struct txStatsTempInfo
{
	A_UINT32	descToRead;		// address of next descriptor to check complete	
    A_UINT32    descRate;       // Rate for this descriptor
	A_UINT32	status1;		// first status word of descriptor
	A_UINT32	status2;		// second status word of descriptor
	A_UINT32	totalBytes;		// count of number of bytes transmitted
	SIG_STRENGTH_STATS sigStrength[STATS_BINS];		//  accumulated signal strength stats
	MULT_ANT_SIG_STRENGTH_STATS ackMultAntSigStrength[STATS_BINS];  // rssi for 4 ant of falcon
	GENERIC_BINARY_STATS	ackChain0AntSel[STATS_BINS];
	GENERIC_BINARY_STATS	ackChain1AntSel[STATS_BINS];
	GENERIC_BINARY_STATS	ackChain0AntReq[STATS_BINS];
	GENERIC_BINARY_STATS	ackChain1AntReq[STATS_BINS];
	GENERIC_BINARY_STATS	ackChainStrong[STATS_BINS];
	A_UCHAR		frame_info[MAX_FRAME_INFO_SIZE];
	MDK_ATHEROS_DESC desc;

	// to support aggegration mode
	A_UINT32   baStatus;
	A_UINT32   blockAck0_31;
	A_UINT32   blockAck32_63;


} TX_STATS_TEMP_INFO;


//extract mdk packet header that gets added after 802.11 header
typedef struct mdkPacketHeader
{
	A_UINT16 pktType;
	A_UINT32 numPackets;
	//A_UINT16 qcuIndex;
} __ATTRIB_PACK MDK_PACKET_HEADER;

void createTransmitPacket (A_UINT32 devNum,  A_UINT16 mdkType, A_UCHAR *dest,  A_UINT32 numDesc, 
	A_UINT32 dataBodyLength, A_UCHAR *dataPattern, A_UINT32 dataPatternLength, A_UINT32 broadcast, 
 	A_UINT16 queueIndex, A_UINT32 *pPktSize, A_UINT32 *pPktAddress);

void createTransmitPacketAggr (A_UINT32 devNum,  A_UINT16 mdkType, A_UCHAR *dest,  A_UINT32 numDesc, 
	A_UINT32 dataBodyLength, A_UCHAR *dataPattern, A_UINT32 dataPatternLength, A_UINT32 broadcast, 
 	A_UINT16 queueIndex,  A_UINT32 aggSize, A_UINT32 *pPktSize, A_UINT32 *pPktAddress);

void writeDescriptor(A_UINT32 devNum, A_UINT32 descAddress, MDK_ATHEROS_DESC *pDesc);
void
sendEndPacket
(
 A_UINT32 devNum,
 A_UINT16 queueIndex
);

void
createEndPacket
(
 A_UINT32 devNum,
 A_UINT16 queueIndex,
 A_UCHAR  *dest,
 A_UINT32 antMode,
 A_BOOL probePkt
);

void internalRxDataSetup
(
 A_UINT32 devNum, 
 A_UINT32 numDesc, 
 A_UINT32 dataBodyLength,
 A_UINT32 enablePPM, 		
 A_UINT32 mode
);

void mdkProcessMultiAntSigStrength
(
 A_UINT32 devNum,
 RX_STATS_TEMP_INFO *pStatsInfo
);

extern void zeroDescriptorStatus ( A_UINT32	devNumIndex, MDK_ATHEROS_DESC *pDesc, A_UINT32 swDevID );
extern void writeDescriptors ( A_UINT32	devNumIndex, A_UINT32	descAddress, MDK_ATHEROS_DESC *pDesc, A_UINT32   numDescriptors);
extern void writeDescriptor ( A_UINT32	devNum, A_UINT32	descAddress, MDK_ATHEROS_DESC *pDesc);

#if defined(WIN32) || defined(WIN64)
#pragma pack (pop)
#endif
#endif //__INCmdatah
