/*
 *  Copyright ?2001 Atheros Communications, Inc.,  All Rights Reserved.
 */
/* manlib.h - Exported functions and defines for the manufacturing lib */

//#ifndef _IQV
//#define	_IQV
//#endif	// _IQV

#ifndef	__INCmanlibh
#define	__INCmanlibh
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef VXWORKS
#include <vxworks.h>
#endif

#ifndef MDK_AP 
#include <stdio.h>
#endif
#ifdef SOC_LINUX
#include <stdio.h>
#endif

#include "common_defs.h"

#define MRETRY 16
#define MSTREAM 2
#define MEVM 100
#define MCHAIN 1
#define MRSSI 100

//#define FJC_TEST 1

#define LIB_MAX_DEV	8		/* Number of maximum supported devices */

#define MAX_LB_FRAME_LEN		320
#define BLOCK_PKT_COUNT			1000

#define RATE_6		0x00000001
#define RATE_9		0x00000002
#define RATE_12		0x00000004
#define RATE_18		0x00000008
#define RATE_24		0x00000010
#define RATE_36		0x00000020
#define RATE_48		0x00000040
#define RATE_54		0x00000080
#define RATE_1L     0x00000100
#define RATE_2L     0x00000200
#define RATE_2S     0x00000400
#define RATE_5L     0x00000800
#define RATE_5S     0x00001000
#define RATE_11L    0x00002000
#define RATE_11S    0x00004000
#define RATE_QUART  0x00008000
#define RATE_HALF   0x00010000
#define RATE_1		0x00020000
#define RATE_2      0x00040000
#define RATE_3      0x00080000
#define RATE_GROUP	0x10000000
#define NUM_RATES			20

#define DESC_ANT_A   0x00000000
#define DESC_ANT_B   0x00000001
#define USE_DESC_ANT 0x00000002

#define DESC_CHAIN_2_ANT_A   0x00000000
#define DESC_CHAIN_2_ANT_B   0x00010000

#define CONT_DATA           1  // Continuous tx 100 Data
#define CONT_SINE           2  // Continuous Sine Transmission
#define CONT_SINGLE_CARRIER 3  // Continuous Transmission on one subcarrier
#define CONT_FRAMED_DATA    4  // Continuous tx 99 Data - multi frame

#define ZEROES_PATTERN 	 0x00000000
#define ONES_PATTERN     0x00000001
#define REPEATING_5A     0x00000002
#define COUNTING_PATTERN 0x00000003
#define PN7_PATTERN      0x00000004
#define PN9_PATTERN	     0x00000005
#define REPEATING_10 	 0x00000006
#define RANDOM_PATTERN 	 0x00000007

#define NO_REMOTE_STATS       0x00000000
#define ENABLE_STATS_SEND     0x00000001
#define ENABLE_STATS_RECEIVE  0x00000002
#define SKIP_STATS_COLLECTION     0x00000004 

#define SKIP_SOME_STATS		  0x00000010
#define LEAVE_DESC_STATUS  	  0x00000100
#define NUM_TO_SKIP_S		  16
#define NUM_TO_SKIP_M		  0xffff0000

//packet types
#define MDK_NORMAL_PKT		0x4d64         
#define MDK_LAST_PKT		0x4d65
#define MDK_TX_STATS_PKT	0x4d66
#define MDK_RX_STATS_PKT	0x4d67
#define MDK_TXRX_STATS_PKT	0x4d68
#define MDK_PROBE_PKT		0x4d69
#define MDK_PROBE_LAST_PKT	0x4d6a
#define MDK_SKIP_STATS_PKT	0x4d6b

// Last desc types
#define LAST_DESC_NEXT		0x0
#define LAST_DESC_NULL		0x1
#define LAST_DESC_LOOP		0x2
#define LAST_DESC_FIRST		0x3

// Desc info 
#define DESC_INFO_NUM_DESC_MASK				0xffff
#define DESC_INFO_NUM_DESC_BIT_START		0
#define DESC_INFO_NUM_DESC_WORDS_BIT_START	16
#define DESC_INFO_NUM_DESC_WORDS_MASK		(0xff << DESC_INFO_NUM_DESC_WORDS_BIT_START)
#define DESC_INFO_LAST_DESC_BIT_START		28
#define DESC_INFO_LAST_DESC_MASK			(0xf << DESC_INFO_LAST_DESC_BIT_START)

#define DESC_OP_INTR_BIT_START				0
#define DESC_OP_INTR_BIT_MASK				(0x1f << DESC_OP_INTR_BIT_START)
#define DESC_OP_WORD_OFFSET_BIT_START		8
#define DESC_OP_WORD_OFFSET_BIT_MASK		(0xff << DESC_OP_WORD_OFFSET_BIT_START)
#define DESC_OP_NDESC_OFFSET_BIT_START		16
#define DESC_OP_NDESC_OFFSET_BIT_MASK		(0xffff << DESC_OP_NDESC_OFFSET_BIT_START)

#define BUF_ADDR_INC_CLEAR_BUF_BIT_START		28
#define BUF_ADDR_INC_CLEAR_BUF_BIT_MASK		(0xf << BUF_ADDR_INC_CLEAR_BUF_BIT_START)


#define PROBE_PKT			0x10000		

// Defines for tpScale levels
#define TP_SCALE_LOWEST 0
#define TP_SCALE_MAX 0 
#define TP_SCALE_50  1
#define TP_SCALE_25  2
#define TP_SCALE_12  3  
#define TP_SCALE_MIN 4
#define TP_SCALE_HIGHEST 4

#define MAX_MODE				3		
#define QUARTER_CHANNEL_MASK	0x10000  //use the upper half word of turbo flag to specify the quarter channels 
	                                     //(ie cause 2.5 to be added to current freq
#define CLEAR_QUARTER_CHANNEL_MASK 0xfffeffff

#define NUM_TURBO_MASK_PTS	     512
#define NUM_BASE_MASK_PTS		 256     // must be .5 * NUM_TURBO_MASK_PTS

#define MAX_TEMP_HARDLIMIT     40  

#ifndef SWIG

//flags for tx and rx descriptor cleanup
#define TX_CLEAN				0x01
#define RX_CLEAN				0x02

#define SIZE_ERROR_BUFFER		256

// Library Globals
#define DO_OFSET_CAL             0x00000001
#define DO_NF_CAL	         0x00000002
#define DO_PAPRD                 0x04000000
#define DISABLE_5G_THERM         0x08000000
#define DO_TX_IQ_CAL             0x10000000
#define DO_SW_NF_CAL             0x20000000
#define DO_PAL_OFFSET_CAL        0x40000000
#define DO_DEBUG_CAL             0x80000000


#define DEVLIB_BB_2_ANTAB        0x80000000
#define DEVLIB_BB_2_ANTCD        0x40000000

//setting for derby refclk param
#define REF_CLK_DYNAMIC		0xffff
#define REF_CLK_2_5			0
#define REF_CLK_5			1
#define REF_CLK_10			2
#define REF_CLK_20			3

//CTL mode Flags
#define CTL_MODE_11A		0
#define CTL_MODE_11A_TURBO  0x3
#define CTL_MODE_11G		0x2
#define CTL_MODE_11G_TURBO  0x4
#define CTL_MODE_11B		0x1

#define NO_CTL_IN_EEPROM	0xff
#define NOT_CTL_LIMITED		0xfe

#define UNINITIALIZED_PHASE_DELTA   0xFFFF
#define UNINITIALIZED_CHANNEL       0xFFFF

#define GAIN_OVERRIDE       0xffff

#define  AR6000_TX_GAIN_TBL_SIZE  22
#define  TXGAIN_TABLE_STRING      "bb_tx_gain_table_"
#define  TXGAIN_TABLE_GAININHALFDB_LSB           12
#define  TXGAIN_TABLE_GAININHALFDB_MASK          0x7F
#define  TXGAIN_TABLE_GAININHALFDB_MASKFULL      0x7F000

// The following ifdef block is the standard way of creating macros which make exporting 
// from a DLL simpler. All files within this DLL are compiled with the MLIB_EXPORTS
// symbol defined on the command line. this symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see 
// MLIB_API functions as being imported from a DLL, wheras this DLL sees symbols
// defined with this macro as being exported.
#ifdef WIN32
#ifdef MAUILIB_EXPORTS
#define MANLIB_API __declspec( dllexport )
#else
#define MANLIB_API __declspec( dllimport )
#endif
#else	// #ifdef WIN32
#define MANLIB_API  
#endif	// #ifdef WIN32

#ifdef __ATH_DJGPPDOS__
#undef  MANLIB_API
#define MANLIB_API
#endif //__ATH_DJGPPDOS__

// Non-ANSI definitions
#if !defined(VXWORKS) && !defined (WIN32) 
#ifndef HANDLE
typedef	long HANDLE;
#endif
typedef	unsigned long DWORD;
#endif

#ifndef NULL
#define NULL	0
#endif
#ifndef FALSE
#define FALSE	0
#endif
#ifndef TRUE
#define TRUE	1
#endif

//#define MAX_DESC_WORDS		8
#define MAX_DESC_WORDS		32
#define MAX_TX_DESC_WORDS		8

typedef struct ISREvent {
	A_UINT32 valid;
	A_UINT32 ISRValue;
	A_UINT32 additionalInfo[5];
} ISR_EVENT; 


typedef struct txStats {
	A_UINT32 goodPackets;
	A_UINT32 underruns;
#ifndef NART_BUILD	
	A_UINT32 ackSigStrengthMin;
	A_UINT32 ackSigStrengthMax;
	A_UINT32 ackSigStrengthAvg;
	A_UINT32 throughput;
#else
    A_UINT32 otherError;
#endif	
	A_UINT32 excessiveRetries;
#ifndef NART_BUILD	
	A_UINT32 shortRetry1;
	A_UINT32 shortRetry2;
	A_UINT32 shortRetry3;
	A_UINT32 shortRetry4;
	A_UINT32 shortRetry5;
	A_UINT32 shortRetry6to10;
	A_UINT32 shortRetry11to15;
	A_UINT32 longRetry1;
	A_UINT32 longRetry2;
	A_UINT32 longRetry3;
	A_UINT32 longRetry4;
	A_UINT32 longRetry5;
	A_UINT32 longRetry6to10;
	A_UINT32 longRetry11to15;
#else
    int shortRetry[MRETRY];
	int longRetry[MRETRY];
#endif	
	A_UINT32 newThroughput;
	A_UINT32 startTime;
	A_UINT32 endTime;
#ifndef NART_BUILD	
	A_UINT32 firstPktGood;
	A_UINT32 AckRSSIPerAntMin[4];
	A_UINT32 AckRSSIPerAntMax[4];
	A_UINT32 AckRSSIPerAntAvg[4];
	A_UINT32 AckChain0AntSel[2];
	A_UINT32 AckChain1AntSel[2];
	A_UINT32 AckChain0AntReq[2];
	A_UINT32 AckChain1AntReq[2];
#else
	//
	// rssi histogram for good packets
	//
	int rssi[MRSSI];
	int rssic[MCHAIN][MRSSI];
	int rssie[MCHAIN][MRSSI];
	//
	// evm histogram for good packets
	//
	int evm[MSTREAM][MEVM];
	//
	// rssi histogram for bad packets
	//
	int badrssi[MRSSI];
	int badrssic[MCHAIN][MRSSI];
	int badrssie[MCHAIN][MRSSI];
	//
	// evm histogram for bad packets
	//
	int badevm[MSTREAM][MEVM];
#endif	
	A_UINT32 TXAnt[2];
	A_UINT32 BFCount; // for falcon. total number of bemformed packets
} TX_STATS_STRUCT;

typedef struct rxStats {
	A_UINT32 goodPackets;
#ifndef NART_BUILD	
	A_INT32 DataSigStrengthMin;
	A_INT32 DataSigStrengthMax;
	A_INT32 DataSigStrengthAvg;
#else
    A_UINT32 otherError;
#endif
	A_UINT32 crcPackets;
	A_UINT32 singleDups;
	A_UINT32 multipleDups;
	A_UINT32 bitMiscompares;
    A_UINT32 bitErrorCompares;
	A_INT32 ppmMin;
	A_INT32 ppmMax;
	A_INT32 ppmAvg;
    A_UINT32 decrypErrors;

	// Added for RX tput calculation
	A_UINT32 rxThroughPut;
	A_UINT32 startTime;
	A_UINT32 endTime;
	A_UINT32 byteCount;
	
#ifndef NART_BUILD
	A_INT32 RSSIPerAntMin[4];
	A_INT32 RSSIPerAntMax[4];
	A_INT32 RSSIPerAntAvg[4];
	A_INT32 RSSIPerExtAntMin[4];
	A_INT32 RSSIPerExtAntMax[4];
	A_INT32 RSSIPerExtAntAvg[4];
	A_UINT32 maxRSSIAnt;
#else
	//
	// rssi histogram for good packets
	//
	int rssi[MRSSI];
	int rssic[MCHAIN][MRSSI];
	int rssie[MCHAIN][MRSSI];
	//
	// evm histogram for good packets
	//
	int evm[MSTREAM][MEVM];
	//
	// rssi histogram for bad packets
	//
	int badrssi[MRSSI];
	int badrssic[MCHAIN][MRSSI];
	int badrssie[MCHAIN][MRSSI];
	//
	// evm histogram for bad packets
	//
	int badevm[MSTREAM][MEVM];
#endif	
	A_UINT32 Chain0AntSel[2];
	A_UINT32 Chain1AntSel[2];
	A_UINT32 Chain0AntReq[2];
	A_UINT32 Chain1AntReq[2];
	A_UINT32 ChainStrong[2];
#ifndef NART_BUILD	
	A_UINT32 evm_stream0, evm_stream1;	
#endif	
	A_UINT32 total_pkt;
	A_UINT32 badPackets;
#ifndef NART_BUILD	
	A_UINT32 otherError;
	A_UINT32 phyError;  
#endif	
} RX_STATS_STRUCT;

typedef struct rxStatsSnapshot {
	A_UINT32 goodPackets;
	A_INT32 DataSigStrength;
	A_UINT32 dataRate;
	A_UINT32  bodySize;
	A_UINT32 crcPackets;
    A_UINT32 decrypErrors;
	A_INT32   DataSigStrengthPerAnt[4];
    A_INT32   DataSigStrengthPerExtAnt[4];
	A_UINT32  ch0Sel;
	A_UINT32  ch1Sel;
	A_UINT32  ch0Req;
	A_UINT32  ch1Req;
	A_UINT32  chStr;
	A_UINT32 evm_stream0, evm_stream1;
} RX_STATS_SNAPSHOT;

//subset of device information that gets passed between layers
typedef struct subDevInfo {
    A_CHAR           regFilename[128];  //register file name
    A_CHAR	     libRevStr[128];
    A_UINT32         aRevID;    //analog revID
    A_UINT32         hwDevID;    //pci devID read from hardware
	A_UINT32		 swDevID;    //software based ID 
								 //more unique than pci devID to identify chipsets
    A_UINT32		 bbRevID;  //baseband devision
    A_UINT32	     macRev;   // The Mac revision number
	A_UINT32		 subSystemID;
	A_UINT32		defaultConfig;
} SUB_DEV_INFO;
// API extended commands from this Library

//Structure to hold library params that can be tweeked from outside
//setup a structure, so number of params can be increased without changing
//library calls. 
//IMPORTANT NOTE: only add 32bit values to this or fix the endian swapping in 
//dk_client.c.  It is assuming this will grow 32 bit params only
typedef enum _LINK_RX_MODE{NO_MODE = 0, TX_MODE, RX_MODE, WAIT_MODE, LOOP_MODE, SIGGEN_MODE,PROM_MODE} LINK_RX_MODE;

typedef struct _libParams
{
	A_UINT32 refClock;				//used only by derby
	A_UINT32 beanie2928Mode;		//set derby to 2928 rather than 3.168
	A_UINT32 enableXR;
	A_UINT32 loadEar;
	A_UINT32 artAniEnable;          // enable ART Automatic Noise Immunity 
	A_UINT32 artAniReuse;           // reuse ANI levels for that channel 
	A_UINT32 eepStartLocation;       // start location of 2nd eeprom_block if exists
	A_UINT32 chainSelect;            // chainSelect for falcon
	A_UINT32 flashCalSectorErased;   // whether to erase cal sector on flash for falcon
	A_UINT32 useShadowEeprom;        // whether to use shadow EEPROM instead of real flash
	A_UINT32 phaseDelta;             // phase delta bet'n chain0 and chain1 for falcon
	A_UINT32 applyCtlLimit;			//apply ctl limit flag
	A_UINT32 ctlToApply;			//which ctl should be applied if above flag is true
	A_UINT32 printPciWrites;		//debug mode to enable dumping of pci writes
	A_UINT32 artAniNILevel;         // manual noise immunity NI level
	A_UINT32 artAniBILevel;         // manual noise immunity BI level
	A_UINT32 artAniSILevel;         // manual noise immunity SI level
	A_UINT32 applyPLLOverride;		// set if want to override new pll value
	A_UINT32 pllValue;				// New pll value to apply
	A_UINT32 noUnlockEeprom;    	// Set to keep eeprom write protection for griffin
	A_UINT32 antenna;               // antenna selection from configSetup
	A_UINT32 useEepromNotFlash;     // whether to use EEPROM instead of flash
	A_UINT32 tx_chain_mask;         // for falcon. value from eeprom gets precedence
	A_UINT32 rx_chain_mask;         // for falcon. value from eeprom gets precedence
	A_UINT32 chain_mask_updated_from_eeprom;
	A_UINT32 synthesizerOffset;     // offset between 2.5G synthesizer phases for 2 derbies
	A_UINT32 extended_channel_op;   // specify extended channel frequency direction 0 - lower, 1 - upper
	A_UINT32 extended_channel_sep;  // extended channel frequency 20, 25 (Mhz)
    A_UINT32 short_gi_enable;       // short gi (guard interval) 0 - disabled, 1 - enabled
    A_UINT32 ht40_enable;           // Enable dyanamic HT20/40 mode
    A_UINT32 rateMaskMcs20;         // Add MCS 20 Rate Masks
    A_UINT32 rateMaskMcs40;         // Add MCS 40 Rate Masks
    A_UINT32 enablePdadcLogging;    // log closed loop power to pdadc curves to file 
    A_UINT32 verifyPdadcTable;      // set to true if want to verify pdadc table on eeprom load
    A_UINT32 pdadcDelta;            // delta to check for pdadc table
    A_UINT32 femBandSel;            // FEM Band Select Pin to control LNA
    A_UINT32 spurChans[5];          // spur mitigation channels
    A_UINT32 stbc_enable;           // stbc mode on/off flag
    A_UINT32 cal_data_in_eeprom;    // Default value is 1 which means cal data in eeprom. 0 for cal data in flash (applies to AP)
    A_UINT32 fastClk5g;             // fast clk mode flag coming from eep file or eeprom
    A_UINT32 rxGainType;            // Rx gain table flag coming from eep file or eeprom
    A_UINT32 openLoopPwrCntl;       // open loop power control flag
    A_UINT32 txGainType;            // Tx gain table flag coming from eep file or eeprom
    A_UINT32 calPdadc;              // pdadc for chain 0 measured during calibration and stored in the eeprom
    A_UINT32 dacHiPwrMode_5G;       // dac mode for TB352 brd @ 5G
    A_UINT32 ANT_DIV_enable;        //
	A_UINT32 PAOffsetCal_enable;    //
    A_INT32  pwrTableOffset;        // pdadc vs pwr table offset for calibration
    A_UINT32 fracN5g;               // fracN flag from eep file or eeprom
    A_INT32  tempSensSlope;         // temperature sensor slope for kiwi olpc
    A_UINT32 fast_DIV_enable;       // 
    A_UINT32 LNA;                   // 
    A_INT32  tempSensSlopePalOn;    // temperature sensor slope for kiwi olpc, PAL ON
    A_UINT32 enable_xlnabias;       // to enable the xlna bias for kite
    A_UINT32 calPdadc_ch1;          // pdadc for chain 1 measured during calibration and stored in the eeprom
    A_UINT32 useManualMask;          // pdadc for chain 1 measured during calibration and stored in the eeprom
	A_UINT8  switch_table_type;
#ifndef VENUS_MDK_BUILD
    LINK_RX_MODE sp_rx_mode;       //
    A_UINT8 sp_rx_log;
#endif
    A_UINT32 pal_on;                // flag for turning on/off PAL
} LIB_PARAMS;

enum BitsForExtendedChannelOp {
    EXT_CHANNEL_LOWER      = 0,
    EXT_CHANNEL_HIGHER     = 1,
    EXT_CHANNEL_BIT_MASK   = 1,
    EXT_CHANNEL_ONLY_MASK  = 2,
    EXT_CHANNEL_DUP_MASK   = 4,
};

#define NUM_16K_EDGES	8

//NOTE: in order for endian swapping to work (and be easy),
//leave the next 2 structures containing all 32 bit values.
typedef struct {
	A_UINT32 lowChannel;
	A_UINT32 highChannel;
	A_INT32 twicePower;
} CTL_PWR_RANGE;

typedef struct {
	CTL_PWR_RANGE      ctlPowerChannelRangeOfdm[NUM_16K_EDGES];
	CTL_PWR_RANGE      ctlPowerChannelRangeCck[NUM_16K_EDGES];
	A_INT32			   powerOfdm;
	A_INT32			   powerCck;
	A_UINT32		   channelApplied;
	A_UINT32		   structureFilled;
} CTL_POWER_INFO;

/****************************************************************************************/
/* These definitions need to go somewhere where they can be shared between app & driver */
/* others are used in user space only but have "exact" counterparts in the driver       */
/****************************************************************************************/
/**********BAA: BEGIN HAL definitions shared with the driver ***********/
#if defined(ANWI_HAL_DRV)
/*
 * Channels are specified by frequency.
 */
typedef struct { /* BAA: changed types to match those known in mdk app */
    A_UINT16 channel;        /* setting in Mhz */
    A_UINT32 channelFlags;   /* see below */
    A_UCHAR  privFlags;
    A_CHAR   maxRegTxPower;  /* max regulatory tx power in dBm */
    A_CHAR   maxTxPower;     /* max true tx power in 0.5 dBm */
    A_CHAR   minTxPower;     /* min true tx power in 0.5 dBm */
} HAL_CHANNEL;

/* operation mode struct */
typedef enum {
    OP_M_STA     = 1, /* infrastructure station */
    OP_M_IBSS    = 0, /* IBSS (adhoc) station   */
    OP_M_HOSTAP  = 6, /* Software Access Point  */
    OP_M_MONITOR = 8  /* Monitor mode           */
}   OP_MODE;

typedef enum {
    HT_MACMODE_20       = 0,            /* 20 MHz operation */
    HT_MACMODE_2040     = 1,            /* 20/40 MHz operation */
}   HT_MACMODE;

typedef enum {
    HT_EXTPROTSPACING_20    = 0,            /* 20 MHZ spacing */
    HT_EXTPROTSPACING_25    = 1,            /* 25 MHZ spacing */
}   HT_EXTPROTSPACING;


typedef enum {
    H_FALSE = 0,       /* NB: lots of code assumes false is zero */
    H_TRUE  = 1,
} H_BOOL;

/* structure groups all arguments for a reset device call */
typedef struct reset_args_s {
    OP_MODE           opMode;
    HAL_CHANNEL*      pChan;
    HT_MACMODE        macMode;
    A_UCHAR           txChainMask;
    A_UCHAR           rxChainMask;
    HT_EXTPROTSPACING htSpacing;  /* control and extention chann separation in HT40 */
    H_BOOL            bChanChange;
    A_UINT32          ht40Enable;
    A_UINT32          halFriendly;
}   reset_args_t; 

/* channelFlags */
    #define CHANNEL_CW_INT    0x00002 /* CW interference detected on channel */
    #define CHANNEL_TURBO     0x00010 /* Turbo Channel */
    #define CHANNEL_CCK       0x00020 /* CCK channel */
    #define CHANNEL_OFDM      0x00040 /* OFDM channel */
    #define CHANNEL_2GHZ      0x00080 /* 2 GHz spectrum channel. */
    #define CHANNEL_5GHZ      0x00100 /* 5 GHz spectrum channel */
    #define CHANNEL_PASSIVE   0x00200 /* Only passive scan allowed in the channel */
    #define CHANNEL_DYN       0x00400 /* dynamic CCK-OFDM channel */
    #define CHANNEL_XR        0x00800 /* XR channel */
    #define CHANNEL_STURBO    0x02000 /* Static turbo, no 11a-only usage */
    #define CHANNEL_HALF      0x04000 /* Half rate channel */
    #define CHANNEL_QUARTER   0x08000 /* Quarter rate channel */
    #define CHANNEL_HT20      0x10000 /* HT20 channel */
    #define CHANNEL_HT40PLUS  0x20000 /* HT40 channel with extention channel above */
    #define CHANNEL_HT40MINUS 0x40000 /* HT40 channel with extention channel below */

/* privFlags */
    #define CHANNEL_INTERFERENCE    0x01 /* Software use: channel interference 
                                        used for as AR as well as RADAR 
                                        interference detection */
    #define CHANNEL_DFS             0x02 /* DFS required on channel */
    #define CHANNEL_4MS_LIMIT       0x04 /* 4msec packet limit on this channel */
    #define CHANNEL_DFS_CLEAR       0x08 /* if channel has been checked for DFS */

#define CHANNEL_A           (CHANNEL_5GHZ|CHANNEL_OFDM)
#define CHANNEL_B           (CHANNEL_2GHZ|CHANNEL_CCK)
#define CHANNEL_PUREG       (CHANNEL_2GHZ|CHANNEL_OFDM)
#ifdef notdef
#define CHANNEL_G           (CHANNEL_2GHZ|CHANNEL_DYN)
#else
#define CHANNEL_G           (CHANNEL_2GHZ|CHANNEL_OFDM)
#endif
#define CHANNEL_T           (CHANNEL_5GHZ|CHANNEL_OFDM|CHANNEL_TURBO)
#define CHANNEL_ST          (CHANNEL_T|CHANNEL_STURBO)
#define CHANNEL_108G        (CHANNEL_2GHZ|CHANNEL_OFDM|CHANNEL_TURBO)
#define CHANNEL_108A        CHANNEL_T
#define CHANNEL_X           (CHANNEL_5GHZ|CHANNEL_OFDM|CHANNEL_XR)
#define CHANNEL_G_HT20      (CHANNEL_2GHZ|CHANNEL_HT20)
#define CHANNEL_A_HT20      (CHANNEL_5GHZ|CHANNEL_HT20)
#define CHANNEL_G_HT40PLUS  (CHANNEL_2GHZ|CHANNEL_HT40PLUS)
#define CHANNEL_G_HT40MINUS (CHANNEL_2GHZ|CHANNEL_HT40MINUS)
#define CHANNEL_A_HT40PLUS  (CHANNEL_5GHZ|CHANNEL_HT40PLUS)
#define CHANNEL_A_HT40MINUS (CHANNEL_5GHZ|CHANNEL_HT40MINUS)
#define CHANNEL_ALL \
        (CHANNEL_OFDM|CHANNEL_CCK| CHANNEL_2GHZ | CHANNEL_5GHZ | CHANNEL_TURBO | CHANNEL_HT20 | CHANNEL_HT40PLUS | CHANNEL_HT40MINUS)
#define CHANNEL_ALL_NOTURBO (CHANNEL_ALL &~ CHANNEL_TURBO)
#endif /* #if defined(HAL_ANWI_DRV) */
/**********BAA: END HAL definitions shared with the driver ***********/

typedef struct struct_GenericFnCall{
	A_UINT32 fnId;
	A_UINT32 arg1;
	A_UINT32 arg2;
	A_UINT32 arg3;
	A_UINT32 arg4;
	A_UINT32 arg5;
	A_UINT32 arg6;
	A_UINT32 arg7;
	A_UINT32 arg8;
}ST_GENERIC_FN_CALL;

typedef struct iq_factor {
	A_UINT32 i0;
	A_UINT32 q0;
	A_UINT32 i1;
	A_UINT32 q1;
	A_UINT32 i2;
	A_UINT32 q2;
} IQ_FACTOR;

typedef struct DeviceMap {
	A_UINT32 DEV_MEMORY_ADDRESS; // Base location of memory access functions
	A_UINT32 DEV_MEMORY_RANGE;   // Range in bytes for memory access 
	A_UINT32 DEV_REG_ADDRESS;    // Base location of register access functions
	A_UINT32 DEV_REG_RANGE;      // Range in bytes for memory access 
	A_UINT32 DEV_CFG_ADDRESS;    // Base location of PCIconfig access functions
	A_UINT32 DEV_CFG_RANGE;      // Range in bytes for PCIconfig access 

	A_UINT16  devIndex;	// device index of the low level structure

	void (* OSmemRead)(A_UINT32 devNum, A_UINT32 address, A_UCHAR *memBytes, A_UINT32 length);
	void (* OSmemWrite)(A_UINT32 devNum, A_UINT32 address, A_UCHAR *memBytes, A_UINT32 length);
	A_UINT32 (* OSregRead)(A_UINT32 devNum, A_UINT32 address);
	void (* OSregWrite)(A_UINT32 devNum, A_UINT32 address, A_UINT32 regValue);   
	A_UINT32 (* OScfgRead)(A_UINT32 devNum, A_UINT32 address);
	void (* OScfgWrite)(A_UINT32 devNum, A_UINT32 address, A_UINT32 cfgValue);
	ISR_EVENT (* getISREvent)(A_UINT32 devNum); // Get a pointer to the latest ISREvent struct

    A_UINT16 remoteLib;
	void (* r_eepromReadBlock) ( A_UINT32 devNum, A_UINT32 startOffset, A_UINT32 length, A_UINT32 *buf);
        void (* r_eepromReadLocs) ( A_UINT32 devNum, A_UINT32 startOffset, A_UINT32 length, A_UINT8 *buf);
    A_UINT32 (* r_eepromRead) (A_UINT32 devNum, A_UINT32 eepromOffset);
    void (* r_eepromWrite) (A_UINT32 devNum, A_UINT32 eepromOffset, A_UINT32 eepromValue);
    A_UINT32 (* r_hwReset) (A_UINT32 devNum, A_UINT32 resetMask );
    void (* r_pllProgram) (A_UINT32 devNum, A_UINT32 turbo, A_UINT32 mode );
    void (* r_pciWrite) (A_UINT32 devNum, PCI_VALUES *pPciValues, A_UINT32 size);
    A_UINT32 (* r_calCheck) (A_UINT32 devNum, A_UINT32 enableCal, A_UINT32 timeout);
    void (* r_fillTxStats) ( A_UINT32 devNum, A_UINT32 descAddress, A_UINT32 numDesc, A_UINT32 dataBodyLen, A_UINT32 txTime, TX_STATS_STRUCT *txStats);
    void (* r_createDescriptors)(A_UINT32 devNumIndex, A_UINT32 descBaseAddress, A_UINT32 descInfo, A_UINT32 bufAddrIncrement, A_UINT32 descOp, A_UINT32 *descWords);
    A_UINT32 (*send_generic_fn_call_cmd) (A_UINT32 devNum, void *stGenericFnCall);
    A_UINT32 (* OSapRegRead32)(A_UINT16 devNum, A_UINT32 address);
    void (* OSapRegWrite32)(A_UINT16 devNum, A_UINT32 address, A_UINT32 regValue);   
    A_UINT32 (* OSmem32Read)(A_UINT32 devNum, A_UINT32 address);
    void (* OSmem32Write)(A_UINT32 devNum, A_UINT32 address, A_UINT32 regValue);
    A_UINT32 (* r_whalResetDevice)(A_UINT32 devNum, A_UCHAR *mac, A_UCHAR *bss, A_UINT32 freq, A_UINT32 turbo, A_UINT8 wlanMode);

} DEVICE_MAP;   

#define  PAPRD_EN_2G   0x1
#define  PAPRD_EN_5G   0x2

typedef struct moreResetParms
{
    A_BOOL set_isForcedPALOn;
    A_BOOL isForcedPALOn;
    A_BOOL set_eepromFileRead;
    A_BOOL eepromFileRead;
    A_BOOL set_paprdEnabled;
    A_UINT32 paprdEnabled;
#ifdef SUPER_TX_GAIN_CFG
    A_BOOL set_xpaCfgEnable;
    A_BOOL xpaCfgEnable;
#endif
} MORE_RESET_PARMS;

typedef struct _tPCDebugStruct {
    A_UINT32 txIdx;
    A_INT32  dacGain;
} TPCDebugStruct;

// Setup/Configuration Functions
MANLIB_API A_INT32 getNextDevNum();
MANLIB_API A_UINT32 initializeDevice(struct DeviceMap map);
MANLIB_API A_UINT32 eepromRead(A_UINT32 devNum, A_UINT32 eepromOffset);
MANLIB_API void eepromWrite(A_UINT32 devNum, A_UINT32 eepromOffset, A_UINT32 eepromValue);
MANLIB_API void eepromReadBlock(A_UINT32 devNum,A_UINT32 startOffset,A_UINT32 length,A_UINT32 *eepromValue);
MANLIB_API void eepromWriteBlock(A_UINT32 devNum,A_UINT32 startOffset,A_UINT32 length,A_UINT32 *eepromValue);
MANLIB_API void resetDevice(A_UINT32 devNum, A_UCHAR *mac, A_UCHAR *bss, A_UINT32 freq, A_UINT32 turbo);
MANLIB_API void mini_resetDevice(A_UINT32 devNum, A_UCHAR *mac, A_UCHAR *bss, A_UINT32 freq, A_UINT32 turbo, A_BOOL hwRst);
MANLIB_API A_UINT32 checkRegs(A_UINT32 devNum);
MANLIB_API void changeChannel(A_UINT32 devNum, A_UINT32 freq);
MANLIB_API void loadSwitchTableParams( A_UINT32 devNum, A_UINT32 ant );
MANLIB_API A_BOOL ar5416ChannelChangeTx(A_UINT32 devNum, A_UINT32 freq);
MANLIB_API A_BOOL setAllChannelAr5211(A_UINT32 devNum,A_UINT32 somFreq, A_UINT32 beanieFreq);
MANLIB_API void setAntenna(A_UINT32 devNum, A_UINT32 antenna);
MANLIB_API void setTransmitPower(A_UINT32 devNum, A_UCHAR txPowerArray[17]);
MANLIB_API void setSingleTransmitPower(A_UINT32 devNum, A_UCHAR pcdac);
MANLIB_API void setPowerScale(A_UINT32 devNum, A_UINT32 powerScale);
MANLIB_API void devSleep(A_UINT32 devNum);
MANLIB_API void closeDevice(A_UINT32 devNum) ;
MANLIB_API A_UINT32 checkProm(A_UINT32 devNum, A_UINT32 enablePrint);
MANLIB_API A_UINT32 spurConvertChanToIndex( A_UINT32 freq, A_INT32 is2GHz ,A_INT32 isHt40);
MANLIB_API void rereadProm(A_UINT32 devNum);
MANLIB_API void setResetParams(A_UINT32 devNum, A_CHAR *pFilename, A_BOOL eePromLoad, A_BOOL eePromHeaderLoad, A_UCHAR mode, A_UINT16 initCodeFlag);
MANLIB_API void changeField(A_UINT32 devNum, A_CHAR *fieldName, A_UINT32 newValue);
MANLIB_API void dumpPciRegValues(A_UINT32 devNum); 
MANLIB_API void displayPciRegWrites(A_UINT32 devNum); 
MANLIB_API void getField(A_UINT32 devNum, A_CHAR   *fieldName, A_UINT32 *baseValue,	
 A_UINT32 *turboValue);
MANLIB_API void readField(A_UINT32	devNum, A_CHAR *fieldName, A_UINT32	*pUnsignedValue, 
						  A_INT32 *pSignedValue, A_BOOL	*pSignedFlag);
MANLIB_API void writeField(A_UINT32 devNum, A_CHAR *fieldName, A_UINT32 newValue);
MANLIB_API void updateAntenna(A_UINT32 devNum, A_UINT32 antenna, A_UCHAR mode, A_UINT32 turbo);
#ifdef HAL_BUILD
MANLIB_API void writeField_direct(A_UINT32 devNum, A_CHAR *fieldName);
MANLIB_API void restoreRegName(A_UINT32 devNum,A_CHAR *regName,A_UINT32 turbo);
MANLIB_API void initUseInitMode(A_UINT32 devNum,A_UINT16 use_init);
MANLIB_API void dumpRegs_ALL(A_UINT32 devNum, char *fileName);
#endif
MANLIB_API void forceAntennaTbl5211(A_UINT32 devNum, A_UINT16 *pAntennaTbl);
MANLIB_API void forceAntennaTbl5513(A_UINT32 devNum, A_UINT16 *pAntennaTbl);
MANLIB_API void forceAntennaTbl5416(A_UINT32 devNum, A_UINT16 *pAntennaTbl);
MANLIB_API void forceAntennaTbl6003(A_UINT32 devNum, A_UINT16 *pAntennaTbl);

MANLIB_API void setAntennaTbl5211(A_UINT32 devNum, A_UINT16 *pAntennaTbl);
MANLIB_API void readAntennaTbl5211(A_UINT32 devNum, A_UINT16 *pAntennaTbl);
MANLIB_API void specifySubSystemID(A_UINT32 devNum, A_UINT32	subsystemID);
MANLIB_API void     ar5416SetGpio( A_UINT32 devNum, A_UINT32 uiGpio, A_UINT32 uiVal );
MANLIB_API A_UINT32 ar5416ReadGpio( A_UINT32 devNum, A_UINT32 uiGpio );

MANLIB_API void PAOffsetCal(A_UINT32 devNum);
MANLIB_API void PAOffsetCal_Kiwi(A_UINT32 devNum);
// MDK library-use specific call
MANLIB_API void useMDKMemory(A_UINT32 devNum, A_UCHAR *pExtAllocMap, A_UINT16 *pExtIndexBlocks);
MANLIB_API void iq_calibration(A_UINT32 devNum, IQ_FACTOR *iq_coeff);

// Data Frame Functions
MANLIB_API void txDataAggSetup(A_UINT32 devNum, A_UINT32 rateMask, A_UCHAR *dest, 
							A_UINT32 numDescPerRate, A_UINT32 dataBodyLength, 
							A_UCHAR *dataPattern, A_UINT32 dataPatternLength, 
							A_UINT32 retries, A_UINT32 antenna, A_UINT32 broadcast, A_UINT32 aggSize);

MANLIB_API void txDataSetup(A_UINT32 devNum, A_UINT32 rateMask, A_UCHAR *dest, 
							A_UINT32 numDescPerRate, A_UINT32 dataBodyLength, 
							A_UCHAR *dataPattern, A_UINT32 dataPatternLength, 
							A_UINT32 retries, A_UINT32 antenna, A_UINT32 broadcast);
MANLIB_API void txDataSetupNoEndPacket(A_UINT32 devNum, A_UINT32 rateMask, A_UCHAR *dest, 
							A_UINT32 numDescPerRate, A_UINT32 dataBodyLength, 
							A_UCHAR *dataPattern, A_UINT32 dataPatternLength, 
							A_UINT32 retries, A_UINT32 antenna, A_UINT32 broadcast);
MANLIB_API void txDataBegin(A_UINT32 devNum, A_UINT32 timeout, A_UINT32 remoteStats);
MANLIB_API void txDataStart(A_UINT32 devNum);
MANLIB_API void txDataComplete(A_UINT32 devNum, A_UINT32 timeout, A_UINT32 remoteStats);

MANLIB_API void rxDataSetup(A_UINT32 devNum, A_UINT32 numDesc, A_UINT32 dataBodyLength, A_UINT32 enablePPM);
MANLIB_API void rxDataAggSetup(A_UINT32 devNum, A_UINT32 numDesc, A_UINT32 dataBodyLength, A_UINT32 enablePPM, A_UINT32 aggSize);
MANLIB_API void rxDataSetupFixedNumber(A_UINT32 devNum, A_UINT32 numDesc, A_UINT32 dataBodyLength, A_UINT32 enablePPM);
MANLIB_API void rxDataBeginFixedNumber_1(A_UINT32 devNum, A_UINT32 timeout, A_UINT32 remoteStats,
				A_UINT32 enableCompare, A_UCHAR *dataPattern, A_UINT32 dataPatternLength, A_CHAR *GIVEN_SSID);
#ifndef NART_BUILD
MANLIB_API void SaveMacAddress(A_UINT32 devNum, A_CHAR *file_name);
#else
MANLIB_API void SaveMacAddress(A_UINT32 devNum, A_CHAR *file_name,A_INT32 * error);
#endif

// LinkRx.h #############################################

MANLIB_API void LinkRxSetup(A_UINT32 devNum, A_UINT32 numDesc, A_UINT32 dataBodyLength, A_UINT32 enablePPM, A_UINT32 mode, A_UINT32 aggSize);


MANLIB_API void LinkRxStart(A_UINT32 devNum, int promiscuous);


MANLIB_API void LinkRxComplete(A_UINT32 devNum, A_UINT32 waitTime, A_UINT32 timeout, A_UINT32 remoteStats, 
	A_UINT32 enableCompare, A_UCHAR *dataPattern, A_UINT32 dataPatternLength, int (*done)());


MANLIB_API void LinkRxGetStats(A_UINT32 devNum, A_UINT32 rateInMb, A_UINT32 remote, RX_STATS_STRUCT *pReturnStats);


MANLIB_API void Ar5416Select();// Register.h #############################################
MANLIB_API void Ar6003Select();// Register.h #############################################
MANLIB_API void Ar6003SetNartLogInfo (A_UINT8 value);

MANLIB_API unsigned long RegisterRead(unsigned long DevNum, unsigned long address);

MANLIB_API int RegisterWrite(unsigned long DevNum, unsigned long address, unsigned long value);
MANLIB_API unsigned long Memory32Read(unsigned long DevNum, unsigned long address);
MANLIB_API int Memory32Write(unsigned long DevNum, unsigned long address, unsigned long value);


// LinkTx.h #############################################


MANLIB_API void LinkTxSetup(
 A_UINT32 devNum,
 A_UINT32 rateMask,
 A_UCHAR *dest,
 A_UINT32 numDescPerRate,
 A_UINT32 dataBodyLength,
 A_UCHAR *dataPattern,
 A_UINT32 dataPatternLength,
 A_UINT32 retries,
 A_UINT32 antenna,
 A_UINT32 broadcast,
 int ifs);

MANLIB_API void LinkTxStart(A_UINT32 devNum);

MANLIB_API void LinkTxComplete(A_UINT32 devNum, A_UINT32 timeout, A_UINT32 remoteStats, int (*ison)(A_UINT32 devNum), int (*done)(A_UINT32 devNum));

MANLIB_API void LinkTxGetStats(A_UINT32 devNum, A_UINT32 rateInMb, A_UINT32 remote, TX_STATS_STRUCT *pReturnStats);

// end LinkTx.h #########################################################


MANLIB_API void CarrierTx( A_UINT32 devNum,  A_UINT32 timeout, int (*ison)(A_UINT32 devNum), int (*done)(A_UINT32 devNum));


MANLIB_API void rxDataBegin(A_UINT32 devNum, A_UINT32 waitTime, A_UINT32 timeout, A_UINT32 remoteStats,
				A_UINT32 enableCompare, A_UCHAR *dataPattern, A_UINT32 dataPatternLength);
MANLIB_API void rxDataBeginSG(A_UINT32 devNum, A_UINT32 waitTime, A_UINT32 timeout, A_UINT32 remoteStats,
                                A_UINT32 enableCompare, A_UCHAR *dataPattern, A_UINT32 dataPatternLength, A_UINT32 sgpacketnumber);
MANLIB_API void LinkRxComplete(A_UINT32 devNum, A_UINT32 waitTime, A_UINT32 timeout, A_UINT32 remoteStats, 
A_UINT32 enableCompare, A_UCHAR *dataPattern, A_UINT32 dataPatternLength, int (*done)());
MANLIB_API void LinkRxComplete_promiscous(A_UINT32 devNum, A_UINT32 waitTime, A_UINT32 timeout, A_UINT32 remoteStats, 
A_UINT32 enableCompare, A_UCHAR *dataPattern, A_UINT32 dataPatternLength, int (*done)());


MANLIB_API void cleanupTxRxMemory(A_UINT32 devNum, A_UINT32 flags);
MANLIB_API void rxDataStart(A_UINT32 devNum);
MANLIB_API void rxDataStartSG(A_UINT32 devNum);
MANLIB_API void rxDataComplete(A_UINT32 devNum, A_UINT32 waitTime, A_UINT32 timeout, A_UINT32 remoteStats,
				A_UINT32 enableCompare, A_UCHAR *dataPattern, A_UINT32 dataPatternLength);
MANLIB_API void rxDataCompleteSG(A_UINT32 devNum, A_UINT32 waitTime, A_UINT32 timeout, A_UINT32 remoteStats,
                                A_UINT32 enableCompare, A_UCHAR *dataPattern, A_UINT32 dataPatternLength, A_UINT32 sgpacketnumber);
MANLIB_API void rxDataBeginFixedNumber(A_UINT32 devNum, A_UINT32 timeout, A_UINT32 remoteStats,
				A_UINT32 enableCompare, A_UCHAR *dataPattern, A_UINT32 dataPatternLength);
MANLIB_API void rxDataCompleteFixedNumber(A_UINT32 devNum, A_UINT32 timeout, A_UINT32 remoteStats,
				A_UINT32 enableCompare, A_UCHAR *dataPattern, A_UINT32 dataPatternLength);
MANLIB_API void rxDataCompleteFixedNumber_1(A_UINT32 devNum, A_UINT32 timeout, A_UINT32 remoteStats,
				A_UINT32 enableCompare, A_UCHAR *dataPattern, A_UINT32 dataPatternLength,A_CHAR *GIVEN_SSID);
MANLIB_API void checkBeaconRSSI(A_UINT32 devNum, A_UINT32 timeout, A_UINT32 remoteStats,
				A_UINT32 enableCompare, A_UCHAR *dataPattern, A_UINT32 dataPatternLength,A_CHAR *GIVEN_SSID,A_INT32 rssiThreshold,A_UINT16 antenna,A_INT32 * result);
MANLIB_API void txrxDataBegin(A_UINT32 devNum, A_UINT32 waitTime, A_UINT32 timeout, A_UINT32 remoteStats,
				A_UINT32 enableCompare, A_UCHAR *dataPattern, A_UINT32 dataPatternLength);
MANLIB_API void txGetStats(A_UINT32 devNum, A_UINT32 rateInMb, A_UINT32 remote, TX_STATS_STRUCT *pReturnStats);
MANLIB_API void rxGetStats(A_UINT32 devNum, A_UINT32 rateInMb, A_UINT32 remote, RX_STATS_STRUCT *pReturnStats);
MANLIB_API void rxGetData(A_UINT32 devNum, A_UINT32 bufferNum, A_UCHAR *pReturnBuffer, A_UINT32 sizeBuffer);
MANLIB_API void enableWep(A_UINT32 devNum, A_UCHAR key);
MANLIB_API A_BOOL testLib(A_UINT32 devNum, A_UINT32 timeout);
MANLIB_API void enablePAPreDist(A_UINT32 devNum, A_UINT16 rate, A_UINT32 power);
MANLIB_API A_BOOL setupOlpcCAL(A_UINT32 devNum);
MANLIB_API void forcePowerTxMax (A_UINT32 devNum, A_INT16 *pRatesPower);
MANLIB_API void forceChainPowerTxMax (A_UINT32 devNum, A_INT16 *pRatesPower, A_UINT32 chainNum);
MANLIB_API void forcePowerTx_Venice_Hack(A_UINT32 devNum, A_BOOL maxRange);
MANLIB_API A_INT32 getMaxLinPowerx4(A_UINT32 devNum);

MANLIB_API void TempComp( A_UINT32 devNum );
MANLIB_API void forceSinglePowerTxMax(A_UINT32 devNum, A_INT16 powerValue);
MANLIB_API void ForceSinglePCDACTable(A_UINT32 devNum, A_UINT16 pcdac);
MANLIB_API void ForceSinglePCDACTableAnalog(A_UINT32 devNum, A_UINT16 pcdac, A_BOOL turnOn);
MANLIB_API A_UINT32 ForceSinglePCDACTableVenus(A_UINT32 devNum, A_UINT16 pcdac, A_UINT16 offset, A_BOOL setIt, TPCDebugStruct *pTpcData);
MANLIB_API A_BOOL setupSinglePCDACVenus(A_UINT32 devNum, A_UINT16 pcdac, A_BOOL turnOn);
MANLIB_API void ForceSinglePCDACTableGriffin(A_UINT32 devNum, A_UINT16 pcdac, A_UINT16 offset);
MANLIB_API A_BOOL generateTxGainTblFromCfg(A_UINT32 devNum, A_UINT32 modeMask);
MANLIB_API void forcePCDACTable (A_UINT32 devNum, A_UINT16 *pPcdacs);
MANLIB_API void forceChainPCDACTable (A_UINT32	devNum, A_UINT16 *pPcdacs, A_UINT32 chainNum);
MANLIB_API void getMacAddr(A_UINT32 devNum, A_UINT16 wmac, A_UINT16 instNo, A_UINT8 *macAddr);
MANLIB_API void getEepromStruct(A_UINT32 devNum,A_UINT16 eepStructFlag,	void **ppReturnStruct,A_UINT32 *pNumBytes);
MANLIB_API void getDeviceInfo(A_UINT32 devNum, SUB_DEV_INFO *pInfoStruct);
MANLIB_API void getMdkErrStr(A_UINT32 devNum, A_CHAR *pBuffer);
MANLIB_API A_INT32 getMdkErrNo(A_UINT32 devNum);
MANLIB_API A_BOOL rxLastDescStatsSnapshot(A_UINT32 devNum, RX_STATS_SNAPSHOT *pRxStats);
#ifndef MDK_AP
MANLIB_API void enableLogging(A_CHAR *);
MANLIB_API void disableLogging(void);
#endif

// Continuous Transmit Functions
MANLIB_API void txContBegin(A_UINT32 devNum, A_UINT32 type, A_UINT32 typeOption1,
							A_UINT32 typeOption2, A_UINT32 antenna);
MANLIB_API void txContFrameBegin(A_UINT32 devNum, A_UINT32 length, A_UINT32 ifswait,
                                  A_UINT32 typeOption1, A_UINT32 typeOption2, A_UINT32 antenna,
								   A_BOOL   performStabilizePower, A_UINT32 numDescriptors, A_UCHAR *dest);
MANLIB_API void txContEnd(A_UINT32 devNum);
MANLIB_API void devlibCleanup();
MANLIB_API A_UINT16 getDevIndex(A_UINT32 devNum);
MANLIB_API void PushTxGainTbl(A_UINT32 devNum, A_UINT32 pcdac);

MANLIB_API void setQueue
(
	A_UINT32 devNum, 
	A_UINT32 qcuNumber
);

MANLIB_API void mapQueue
(
	A_UINT32 devNum, 
	A_UINT32 qcuNumber,
	A_UINT32 dcuNumber
);

MANLIB_API void clearKeyCache
(
	A_UINT32 devNum
);

MANLIB_API A_INT32 getFieldForMode
(
 A_UINT32 devNum,
 A_CHAR   *fieldName,
 A_UINT32  mode,			//desired mode 
 A_UINT32  turbo		//Flag for base or turbo value
);

MANLIB_API void changeMultipleFieldsAllModes
(
 A_UINT32		  devNum,
 PARSE_MODE_INFO *pFieldsToChange,
 A_UINT32		  numFields
);

MANLIB_API void changeRegValueField
(
 A_UINT32 devNum,
 A_CHAR *fieldName, 
 A_UINT32 newValue
);

MANLIB_API void changeMultipleFields
(
 A_UINT32		  devNum,
 PARSE_FIELD_INFO *pFieldsToChange,
 A_UINT32		  numFields
);

MANLIB_API void changeAddacField
(
 A_UINT32		  devNum,
 PARSE_FIELD_INFO *pFieldToChange
);

MANLIB_API void saveXpaBiasLvlFreq
(
  A_UINT32		  devNum,
  PARSE_FIELD_INFO *pFieldToChange,
  A_UINT16        biasLevel
);

MANLIB_API A_INT16 getMaxPowerForRate
(
 A_UINT32 devNum,
 A_UINT32 freq,
 A_UINT32 rate
);

MANLIB_API A_UINT16 getXpdgainForPower
(
 A_UINT32	devNum,
 A_INT32    powerIn
);

MANLIB_API A_UINT16 getPcdacForPower
(
	A_UINT32 devNum,
	A_UINT32 freq,
	A_INT32 twicePower
);

MANLIB_API A_UINT16 getPowerIndex
(
	A_UINT32 devNum,
	A_INT32 twicePower
);

MANLIB_API A_UINT16 getInterpolatedValue 
(
 A_UINT16	target,
 A_UINT16	srcLeft,
 A_UINT16	srcRight,
 A_UINT16	targetLeft,
 A_UINT16	targetRight,
 A_BOOL		scaleUp
);

MANLIB_API A_UINT32 getArtAniLevel
(
	A_UINT32 devNum,
	A_UINT32 artAniType
);

MANLIB_API void setArtAniLevel
(
	A_UINT32 devNum,
	A_UINT32 artAniType,
	A_UINT32 artAniLevel
);

MANLIB_API void setChain
(
	A_UINT32 devNum,
	A_UINT32 chain,
	A_UINT32 phase
);

MANLIB_API A_BOOL updateMacAddrAR5513 
(
	A_UINT32 devNum,
	A_UINT8  wmacAddr[6] // wmac addr [0]-->lsb, [5]-->msb
);

MANLIB_API A_BOOL updateSectorChunkAR5513 
(
	A_UINT32 devNum,
	A_UINT32 *pData, // intended for pci config data
	A_UINT32 sizeData,
	A_UINT32 sectorNum,
	A_UINT32 dataOffset
);

MANLIB_API A_BOOL updateSingleEepromValueAR5513 
(
	A_UINT32 devNum,
	A_UINT32 address,
	A_UINT32 value
);

MANLIB_API A_BOOL flashWriteSectorAr5513
(
 A_UINT32 devNum, 
 A_UINT32 sectorNum,
 A_UINT32 *retList,
 A_UINT32 retListSize
);

MANLIB_API A_BOOL flashReadSectorAr5513
(
 A_UINT32 devNum, 
 A_UINT32 sectorNum,
 A_UINT32 *retList,
 A_UINT32 retListSize
);


MANLIB_API A_UINT32 getPhaseCal
(
 A_UINT32 devNum,
 A_UINT32 freq
);

MANLIB_API void supplyFalseDetectbackoff
(
	A_UINT32 devNum,
	A_UINT32 *pBackoffValues
);

MANLIB_API void configureLibParams
(
 A_UINT32 devNum,
 LIB_PARAMS *pLibParamsInfo
);

MANLIB_API void set_dev_nums(A_UINT32 gold_dev, A_UINT32 dut_dev) ;
MANLIB_API void force_minccapwr (A_UINT32 maxccapwr) ;
MANLIB_API double detect_signal (A_UINT32 desc_cnt, A_UINT32 adc_des_size, A_UINT32 mode, A_UINT32 *gain) ;
MANLIB_API void config_capture (A_UINT32 dut_dev, A_UCHAR *RX_ID, A_UCHAR *BSS_ID, A_UINT32 channel, A_UINT32 turbo, A_UINT32 *gain, A_UINT32 mode) ;
MANLIB_API A_UINT32 trigger_sweep (A_UINT32 dut_dev, A_UINT32 channel, A_UINT32 mode, A_UINT32 averages, A_UINT32 path_loss, A_BOOL return_spectrum, double *psd) ;
MANLIB_API void
getCtlPowerInfo
(
 A_UINT32 devNum,
 CTL_POWER_INFO *pCtlStruct	
);

MANLIB_API A_BOOL isFalconEmul(A_UINT32 devNum);
MANLIB_API A_BOOL isFalcon(A_UINT32 devNum);
MANLIB_API A_BOOL large_pci_addresses(A_UINT32 devNum);
MANLIB_API A_BOOL isFalconDeviceID(A_UINT32 swDevID) ;

MANLIB_API A_BOOL needsUartPciCfg(A_UINT32 swDeviceID);
MANLIB_API A_BOOL isPredator(A_UINT32 swDeviceID);
MANLIB_API A_BOOL isGriffin(A_UINT32 swDeviceID);
MANLIB_API A_BOOL isGriffin_1_0(A_UINT32 macRev);
MANLIB_API A_BOOL isGriffin_1_1(A_UINT32 macRev);
MANLIB_API A_BOOL isGriffin_2_0(A_UINT32 macRev);
MANLIB_API A_BOOL isGriffin_lite(A_UINT32 macRev);
MANLIB_API A_BOOL isGriffin_2_1(A_UINT32 macRev);
MANLIB_API A_BOOL isEagle(A_UINT32 swDeviceID);
MANLIB_API A_BOOL isEagle_1_0(A_UINT32 macRev);
MANLIB_API A_BOOL isEagle_lite(A_UINT32 macRev);
MANLIB_API A_BOOL isCondor(A_UINT32 swDeviceID);
MANLIB_API A_BOOL isSpider(A_UINT32 swDeviceID);
MANLIB_API A_BOOL isCobra(A_UINT32 swDeviceID);
MANLIB_API A_BOOL isOwl(A_UINT32 swDeviceID);
MANLIB_API A_BOOL isMerlin(A_UINT32 swDeviceID);
MANLIB_API A_BOOL isMerlinPowerControl(A_UINT32 swDeviceID);
MANLIB_API A_BOOL isMerlin_Eng(A_UINT32 macRev);
MANLIB_API A_BOOL isMerlin_0108(A_UINT32 macRev);
MANLIB_API A_BOOL isMerlin_0208(A_UINT32 macRev);
MANLIB_API A_BOOL isMerlinPcie(A_UINT32 macRev);
MANLIB_API A_BOOL isOwl_2_Plus(A_UINT32 macRev);
MANLIB_API A_BOOL isBB_single_chain(A_UINT32 macRev);
MANLIB_API A_BOOL isSowl_Emulation(A_UINT32 macRev);
MANLIB_API A_BOOL isSowl(A_UINT32 macRev);
MANLIB_API A_BOOL isSowl1_1(A_UINT32 macRev);
MANLIB_API A_BOOL isHowlAP(A_UINT32 devIndex);
MANLIB_API A_BOOL isPythonAP(A_UINT32 devIndex);
MANLIB_API A_BOOL isNala(A_UINT32 swDeviceID);
MANLIB_API A_BOOL isMerlin_Fowl_Emulation(A_UINT32 macRev);
MANLIB_API A_BOOL isKite(A_UINT32 devNum);
MANLIB_API A_BOOL isKiteSW(A_UINT32 swDeviceID);
MANLIB_API A_BOOL isKite11gSW(A_UINT32 swDeviceID);
MANLIB_API A_BOOL isKite1_0mac(A_UINT32 macRev);
MANLIB_API A_BOOL isKite1_1mac(A_UINT32 macRev);
MANLIB_API A_BOOL isKite1_2mac(A_UINT32 macRev);
MANLIB_API A_BOOL isKite11gmac(A_UINT32 macRev);
MANLIB_API A_BOOL isKiwi(A_UINT32 swDeviceID);
MANLIB_API A_BOOL isKiwiPcie(A_UINT32 macRev);
MANLIB_API A_BOOL isKiwi_Merlin_Emulation(A_UINT32 macRev);
MANLIB_API A_BOOL is11nDeviceID(A_UINT32 swDeviceID);
MANLIB_API A_BOOL isDragon(A_UINT32 devNum);
MANLIB_API A_BOOL isVenus(A_UINT32 devNum);
MANLIB_API A_BOOL isVenusID(A_UINT32 swDeviceID);
MANLIB_API A_BOOL isDragon_sd(A_UINT32 swDevID);
MANLIB_API A_BOOL isMercury(A_UINT32 devNum);
MANLIB_API A_BOOL isMercury2_0(A_UINT32 devNum);
MANLIB_API A_BOOL isMercury_sd(A_UINT32 swDevID);
MANLIB_API A_BOOL isMercuryLNA2(A_UINT32 subsystemID);
MANLIB_API void dumpRegs(A_UINT32 devNum, char *fileName);
MANLIB_API void prtRegs(A_UINT32 devNum, A_UINT32 start, A_UINT32 end);
MANLIB_API A_BOOL isEagle_d(A_UINT32 devNum);
MANLIB_API A_BOOL isFlashCalData();
MANLIB_API void setKeepAGCDisable(void);
MANLIB_API void changeLNAdebugsetting(A_BOOL flag);
MANLIB_API void clearKeepAGCDisable(void);

MANLIB_API A_UINT16 getEARCalAtChannel(A_UINT32 devNum, A_BOOL atCal, A_UINT16 channel, A_UINT32 *word, A_UINT16 xpd_mask, A_UINT32 version_mask);
MANLIB_API void memDisplay(A_UINT32 devNum, A_UINT32 address, A_UINT32 nWords);
MANLIB_API A_UINT32 memAlloc(A_UINT32 devNum, A_UINT32 numBytes);
MANLIB_API void memFree(A_UINT32 devNum, A_UINT32 physAddress);

MANLIB_API void setMoreResetParams ( A_UINT32 devNum, MORE_RESET_PARMS *moreResetParms);

MANLIB_API void printRegField (A_UINT32 devNum, A_CHAR *fieldName);
MANLIB_API void printAntDivFields(A_UINT32 devNum);
MANLIB_API void printSwitchTableFields(A_UINT32 devNum);

//for cus99
MANLIB_API void setPowerRef(A_INT32 __PowerOffset);
MANLIB_API A_INT32 getPowerRef(void);
MANLIB_API void setZerodBm(A_UINT32 flag0dBm);
MANLIB_API A_UINT32 isZerodBm(void);
#ifdef NART_BUILD
MANLIB_API void setAcSetting(A_UINT8 ac);
MANLIB_API A_UINT8 getAcSetting(void);
#endif

MANLIB_API A_BOOL checkTemperature(A_UINT32 devNum, A_UINT32 threshold, A_BOOL rtc, A_UINT8 *temp);
MANLIB_API A_BOOL configSharedRxMode(A_UINT32 devNum, A_BOOL sharedRxOn, A_BOOL btCoexOn);
MANLIB_API A_BOOL setRxGainFromCfg(A_UINT32 devNum, A_INT32 gain, A_UINT32 mode, A_UINT32 turbo, A_INT32* minRxGain, A_INT32* maxRxGain, A_UINT32 bbant);
MANLIB_API A_BOOL resetRxGainParmsFromCfg(A_UINT32 devNum, A_UINT32 mode, A_UINT32 turbo);
MANLIB_API A_BOOL palCal(A_UINT32 devNum, A_UINT32 mode);
MANLIB_API A_BOOL monitorTempReadingHang(A_UINT32 devNum, A_UINT32 mode, void* parmPtr);
MANLIB_API A_BOOL dumpDebugStats(A_UINT32 devNum, A_UINT32 mode);
MANLIB_API A_BOOL dumpDebugStatsToFile(A_UINT32 devNum, A_UINT32 mode);
MANLIB_API A_BOOL afterResetProc(A_UINT32 devNum, A_UINT32 freq, A_UINT32 mode);
MANLIB_API void *channelSpecificCfg(A_UINT32 devNum, A_UINT32 freq, A_UINT32 mode);
MANLIB_API void *channelSpecificCfgRX(A_UINT32 devNum, A_UINT32 freq, A_UINT32 mode);
MANLIB_API A_BOOL swNfCal(A_UINT32 devNum, A_UINT32 mode);
MANLIB_API A_BOOL txIQCal(A_UINT32 devNum, A_UINT32 mode);
MANLIB_API A_BOOL paprdTraining(A_UINT32 devNum, A_UINT32 freq, A_UINT32 mode);
MANLIB_API A_BOOL generateVenusTxGainTblFromCfg(A_UINT32 devNum, A_UINT32 modeMask);
MANLIB_API A_UINT32 ForceSingleTxGainEntry(A_UINT32 devNum, A_UINT32 i, A_UINT16 pcdac, A_UCHAR *desiredGain);
MANLIB_API void *xtalSpecificCfg(A_UINT32 devNum, A_INT32 refClk);
#ifdef SUPER_TX_GAIN_CFG
MANLIB_API void *xpaSpecificCfg(A_UINT32 devNum, A_UINT32 freq, A_UINT32 mode);
#endif
#if defined(_DEBUG_BOTH)
MANLIB_API void TestPaprdDebugPrint(A_UINT32 devNum);
#endif //_DEBUG_BOTH
MANLIB_API void setCurTgtPwr(A_UINT32 devNum, A_INT16 power_t2);
MANLIB_API void dumpTPCStats(A_UINT32 devNum);
#ifndef ENABLE_ALPHATHERMTBL	
MANLIB_API A_BOOL readAlphaThermTable4TestingOnly(A_UINT32 devNum);
#endif
MANLIB_API void setCurRateAr6003(A_UINT32 devNum, A_UINT32 curRate);

extern MANLIB_API A_UINT32  enableCal;
extern MANLIB_API A_UINT8 dummyEepromWriteArea[];
extern MANLIB_API A_UINT32  AR6003_EEPROM_SIZE;

// for temporary testing PAPRD
extern MANLIB_API A_UINT32  paprdTrainingdBAboveTgtpwr;
extern MANLIB_API A_UINT32  debugBitmap;
#define  DEBUG_BIT0       0x00000001
#define  DEBUG_BIT1       0x00000002
#define  DEBUG_BIT2       0x00000004
#define  DEBUG_BIT3       0x00000008
#define  DEBUG_BIT4       0x00000010
#define  DEBUG_BIT5       0x00000020
#define  DEBUG_BIT6       0x00000040
#define  DEBUG_BIT7       0x00000080
#define  DEBUG_BIT8       0x00000100
#define  DEBUG_BIT9       0x00000200
#define  DEBUG_BIT10      0x00000400 
#define  DEBUG_BIT11      0x00000800
#define  DEBUG_BIT27      0x08000000
#define  DEBUG_BIT31      0x80000000


#define MAX_NAME_LEN 50
#define MAX_DEBUG_COUNTERS 100
enum DebugCounter {
    RESERVED = 0,
    XTAL_SPECIFIC_CFG,
    
    OTP_READ_ERROR_AFTER_WRITE,
    OTP_WROTE_BAD_STREAM,
    OTP_READ_ERROR,
    OTP_RESET_ERROR,
    OTP_WRITE_ERROR,
    OTP_POP_COUNT,
    OTP_GOOD_PROGRAM,
    OTP_STREAM_READ_LENGTH_MISMATCH,

    STABILIZE_POWER_TIMEOUT,

    PWR_INTERPOLATED, 

    DESIRED_SCALE_OUTOFRANGE,

    THERM_VALUE_INVALID,
#ifdef SUPER_TX_GAIN_CFG
    XPA_SPECIFIC_CFG,
#endif

    LAST_DBG_COUNTER,
};

typedef struct debugCounters {
    char name[MAX_NAME_LEN];
    A_UINT32 counter;
} DebugCounters;

extern DebugCounters debugCounters[];

#ifdef HAL_BUILD
typedef struct _RegTableDU {
    unsigned int base_address;
    unsigned int reg[500];
    unsigned int numReg;
} RegTableDU;
#endif /*HAL_BUILD*/

MANLIB_API void incrDebugCounter(A_UINT32 devNum, enum DebugCounter whichOne);

//extern MANLIB_API A_UINT32 checkSumLength;
//extern MANLIB_API A_UINT32 eepromSize;

MANLIB_API void GetCardLabel (A_UINT32 devNum, A_UINT8 *pCardLabel);

// Calibration related
MANLIB_API void DeviceTransmitPowerSet(A_UINT32 devNum, int mode, A_UINT16 txp);
MANLIB_API int DeviceTransmitGainSet(A_UINT32 devNum, int mode, int pcdac);

MANLIB_API void DeviceTargetPowerApply(A_UINT32 devNum, int frequency);
MANLIB_API void DevicePowerControlOverride(int frequency, int *correction, int *voltage, int *temperature);
MANLIB_API void DeviceCalibrationPierSet(int pier, int frequency, int chain, int correction, int voltage, int temperature);

MANLIB_API int DeviceTemperatureGet(A_UINT32 devNum, int);
MANLIB_API int DeviceVoltageGet(A_UINT32 devNum);

// Chip dependent constants related
MANLIB_API void InitChipDependentConstants(char *ChipName);

#endif // #ifndef SWIG

#ifdef __cplusplus
}
#endif

#endif // #define __INCmanlibh

