/*
 *  Copyright ?2001 Atheros Communications, Inc.,  All Rights Reserved.
 */

#ifndef	__INCmconfigh
#define	__INCmconfigh

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ident  "ACI $Id: //depot/sw/releases/olca3.1-RC/include/systemtools/ar6003/mConfig.h#4 $, $Header: //depot/sw/releases/olca3.1-RC/include/systemtools/ar6003/mConfig.h#4 $"

#ifndef WIN32
#include <unistd.h>
#ifndef EILSEQ  
    #define EILSEQ EIO
#endif	// EILSEQ
#endif	// #ifndef WIN32

//#ifdef LINUX
//#undef ARCH_BIG_ENDIAN
//#endif
#include "wlanproto.h"

#include "athreg.h"
#include "mdata.h"
#include "manlib.h"
#include "mEeprom.h"
#include "mEEPROM_d.h"
#include "mEEPROM_g.h"

#include "art_ani.h"

#include <time.h>

// Insert your headers here
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers

#ifdef WIN32
#ifndef __ATH_DJGPPDOS__
#include <windows.h>
#else //__ATH_DJGPPDOS__
#include <dos.h>
#endif //__ATH_DJGPPDOS__
#endif	// #ifdef WIN32

#if defined(WIN32) || defined(WIN64)
#pragma pack (push, 1)
#endif

/* PCI Config space mapping */
#define F2_PCI_CMD				0x04		/* address of F2 PCI config command reg */
#define F2_PCI_CACHELINESIZE    0x0C        /* address of F2 PCI cache line size value */

/* PCI Config space bitmaps */
#define MEM_ACCESS_ENABLE		0x002       /* bit mask to enable mem access for PCI */
#define MASTER_ENABLE           0x004       /* bit mask to enable bus mastering for PCI */
#define MEM_WRITE_INVALIDATE    0x010       /* bit mask to enable write and invalidate combos for PCI */
#define SYSTEMERROR_ENABLE      0x100		/* bit mask to enable system error */


// General Macros
#define LIB_CFG_RANGE 256
#define LIB_REG_RANGE 65536


/*
#ifdef THIN_CLIENT_BUILD  // For predator
#define LIB_MEMORY_RANGE (100*1024)
#define CACHE_LINE_SIZE 16
#else
#define LIB_MEMORY_RANGE 1048576
#define CACHE_LINE_SIZE 0x20
#endif
*/

#define UNKNOWN_INIT_INDEX     0xffffffff

#define FRAME_BODY_SIZE     4096

#define PCDAC_REG_STRING		"bb_pcdac_"
#define FEZ_PCDAC_REG_STRING	"rf_pcdac"
#define GAIN_DELTA_REG_STRING	"rf_gain_delta"
#define TXPOWER_REG_STRING		"bb_powertx_"

#define NOT_READ			0xffffffff

#define LOWEST_XR_RATE_INDEX		15
#define HIGHEST_XR_RATE_INDEX		19

// __TODO__  JUST USING THIS TO TEST WITH pre 1.6 MDK
//
//extract mdk packet header that gets added after 802.11 header
//typedef struct mdkPacketHeaderX
//{
//	A_UINT16 pktType;
//	A_UINT32 numPackets;
//} MDK_PACKET_HEADERX;

#define  AR2413_TX_GAIN_TBL_SIZE  26
#define  MERLIN_TX_GAIN_TBL_SIZE  22
#define  VENUS_TX_GAIN_TBL_SIZE   32

#define  TXGAIN_TABLE_STRING      "bb_tx_gain_table_"
#define  TXGAIN_TABLE_STRING_OPEN_LOOP "high_power_bb_tx_gain_table_"
#define  TXGAIN_TABLE_STRING_HIGH_POWER "high_power_bb_tx_gain_table_"
#define  TXGAIN_TABLE_FILENAME    "merlin_tx_gain_2.tbl"
#define  AR9287_PAL_ON         "_pal_on"

#define  VENUS_TXGAIN_TAB_STR1        "bb_tx_gain_tab_"
#define  VENUS_TXGAIN_TAB_STR2        "_tg_table"
#define  VENUS_TXGAIN_TAB_STR3        "bb_tx_gain_tab_pal_"
#define  VENUS_TXGAIN_TAB_STR4        "_pal_on"
#define  VENUS_TXGAIN_TAB_FILENAME    "venus_tx_gain_2.tbl"

#define  TXGAIN_TABLE_TX1DBLOQGAIN_LSB           0
#define  TXGAIN_TABLE_TX1DBLOQGAIN_MASK          0x7
#define  TXGAIN_TABLE_TXV2IGAIN_LSB              3
#define  TXGAIN_TABLE_TXV2IGAIN_MASK             0x3
#define  TXGAIN_TABLE_PABUF5GN_LSB               5
#define  TXGAIN_TABLE_PABUF5GN_MASK              0x1
#define  TXGAIN_TABLE_PADRVGN_LSB                6
#define  TXGAIN_TABLE_PADRVGN_MASK               0x7
#define  TXGAIN_TABLE_PAOUT2GN_LSB               9
#define  TXGAIN_TABLE_PAOUT2GN_MASK              0x7
#define  TXGAIN_TABLE_GAININHALFDB_LSB           12
#define  TXGAIN_TABLE_GAININHALFDB_MASK          0x7F

#define  VENUS_TXGAIN_TAB_TXBB1DBGAIN_LSB        0
#define  VENUS_TXGAIN_TAB_TXBB1DBGAIN_MASK       0x7
#define  VENUS_TXGAIN_TAB_TXBB6DBGAIN_LSB        3
#define  VENUS_TXGAIN_TAB_TXBB6DBGAIN_MASK       0x3
#define  VENUS_TXGAIN_TAB_TXMXRGAIN_LSB          5
#define  VENUS_TXGAIN_TAB_TXMXRGAIN_MASK         0xf
#define  VENUS_TXGAIN_TAB_PADRVGNA_LSB           9
#define  VENUS_TXGAIN_TAB_PADRVGNA_MASK          0xf
#define  VENUS_TXGAIN_TAB_PADRVGNB_LSB           13
#define  VENUS_TXGAIN_TAB_PADRVGNB_MASK          0xf
#define  VENUS_TXGAIN_TAB_PADRVGNC_LSB           17
#define  VENUS_TXGAIN_TAB_PADRVGNC_MASK          0xf
#define  VENUS_TXGAIN_TAB_PADRVGND_LSB           21
#define  VENUS_TXGAIN_TAB_PADRVGND_MASK          0x3
#define  VENUS_TXGAIN_TAB_ENABLEPAL_LSB          23
#define  VENUS_TXGAIN_TAB_ENABLEPAL_MASK         0x1
#define  VENUS_TXGAIN_TAB_GAININHALFDB_LSB       24
#define  VENUS_TXGAIN_TAB_GAININHALFDB_MASK      0xff

#define  CREATE_TXGAIN_TABLE_FILE   1


#include "rate_constants.h"

// EEPROM defines
#define REGULATORY_DOMAIN_OFFSET 0xBF // EEPROM Location of the current RD
#define EEPROM_PROTECT_OFFSET 0x3F    // EEPROM Protect Bits
#define ATHEROS_EEPROM_OFFSET 0xC0    // Atheros EEPROM defined values start at 0xC0
#define ATHEROS_EEPROM_ENTRIES 64     // 128 bytes of EEPROM settings
#define REGULATORY_DOMAINS 4          // Number of Regulatory Domains supported
#define CHANNELS_SUPPORTED 5          // Number of Channel calibration groups supported
#define TP_SETTINGS_OFFSET 0x09       // Start location of the transmit power settings
#define TP_SETTINGS_SIZE 11           // Number of EEPROM locations per Channel group
#define TP_SCALING_ENTRIES 11         // Number of entries in the transmit power dBm->pcdac

typedef struct tpcMap
{
    A_UINT8 pcdac[TP_SCALING_ENTRIES];
    A_UINT8 gainF[TP_SCALING_ENTRIES];
    A_UINT8 rate36;
    A_UINT8 rate48;
    A_UINT8 rate54;
    A_UINT8 regdmn[REGULATORY_DOMAINS];
} TPC_MAP;

typedef struct mdk_eepMap
{
    A_BOOL eepromChecked;
    A_BOOL infoValid;
    A_BOOL turboDisabled;  // Turbo Disabled Bit
    A_UINT16 protect;      // Protect Bit Mask
    A_UINT16 version;      // Version field
    A_UINT16 antenna;      // Antenna Settings
    A_UINT16 biasCurrents; // OB, DB
    A_UINT8 currentRD;     // The currently set regulatory domain
    A_UINT8 thresh62;      // thresh62
    A_UINT8 xlnaOn;        // external LNA timing
    A_UINT8 xpaOff;        // external output stage timing
    A_UINT8 xpaOn;         // extneral output stage timing
    A_UINT8 rfKill;        // Single low bit signalling if RF Kill is implemented
    A_UINT8 devType;       // Relates the board's card type - PCI, miniPCI, CB
    A_UINT8 regDomain[REGULATORY_DOMAINS];
	A_UINT8 XPD;
	A_UINT8 XPD_GAIN;
        // Records the regulatory domain's that have been calibrated
    TPC_MAP tpc[CHANNELS_SUPPORTED];
        // Structs to the EEPROM Map;
    A_UINT16 xpdGainValues[MAX_NUM_PDGAINS_PER_CHANNEL];
    A_UINT16 numPdGain;
    A_INT16  midPower;
} mdk_EEP_MAP;

#define EEPROM_ENTRIES_FOR_TXPOWER 32 // 64 bytes of EEPROM calibration data
#define EEPROM_TXPOWER_OFFSET 0xE0    // Power values start at 0xE0
#define MAX_TX_QUEUE_GEN	16 
#define MAX_TX_QUEUE	5  // Venus
#define PROBE_QUEUE 2 //(MAX_TX_QUEUE - 1)

typedef struct txPowerSetup
{
	A_UINT16 eepromPwrTable[EEPROM_ENTRIES_FOR_TXPOWER];
    A_UINT16 tpScale;             // Scaling factor to be applied to max TP 
} TXPOWER_SETUP;

typedef struct txSetup
{
	A_UINT32		txEnable;     // Transmit has been setup
	A_UINT32		pktAddress;	  // physical address of transmit packet
	A_UINT32		descAddress;  // Physical address to start of descriptors				  
    A_UINT32        barPktAddress; // Physical address of BAR packet
	A_UINT32        barDescAddress; // Physical address of barDescAddress
	A_UINT32		endPktAddr;	  // Address of special end packet
	A_UINT32		endPktDesc;	  // Address of descriptor of special end packet
	A_UINT32		numDesc;	  // number of descriptors created
    A_UINT32        dataBodyLen;  // number of bytes in pkt body
    A_UINT32        retryValue;   // copy of retry register value (to write back)
	WLAN_MACADDR    destAddr;     // destination address for packets
	TX_STATS_STRUCT txStats[STATS_BINS];   // Transmit stats 
    A_UINT32        haveStats;    // set to true when txStats contains stats values
    A_UINT32        broadcast;    // set to true if broadcast is enabled
    A_UINT32        contType;     // type of continuous transmit that has been enabled
	A_UINT16		dcuIndex;	  // DCU index of for this tx queue is setup
} TX_SETUP;

typedef struct rxSetup
{
	A_UINT32		rxEnable;     // Transmit has been setup
	A_UINT32		descAddress;  // Physical address to start of descriptors				  
	A_UINT32		bufferSize;	  // Size, in bytes, of packet
	A_UINT32		bufferAddress;// physical address to start of memory buffers
	A_UINT32		lastDescAddress;
	A_UINT32		numDesc;	  // number of descriptors created
	RX_STATS_STRUCT rxStats[MAX_TX_QUEUE][STATS_BINS];   // Receive stats 
	//RX_STATS_STRUCT rxStats[STATS_BINS];   // Receive stats 
    A_UINT32        haveStats;    // set to true when rxStats contains stats values
    A_UINT32        enablePPM;    // PPM was enabled by setup
	A_UINT32		rxMode;		  //receive mode
	A_UINT32		numExpectedPackets; 
	A_BOOL			overlappingDesc;
} RX_SETUP;

typedef struct memSetup
{
	A_UINT32      allocMapSize;  // Size of the allocation map
	A_UCHAR       *pAllocMap;    // The bitMap for tracking memory allocation	
	A_UINT16      *pIndexBlocks; // Number of blocks for the specified index
    A_BOOL        usingExternalMemory; // TRUE if using other driver memory map
} MEM_SETUP;

//++JC++
typedef struct AR2413_txgain_tbl {
  A_UCHAR desired_gain ;
  A_UCHAR bb1_gain ;
  A_UCHAR bb2_gain ;
  A_UCHAR if_gain ;
  A_UCHAR rf_gain ;
 } AR2413_TXGAIN_TBL; 
//++JC++

typedef struct merlin_txgain_tbl {
  A_UCHAR desired_gain ;
  A_UCHAR paout2gn;
  A_UCHAR padrvgn;
  A_UCHAR pabuf5gn;
  A_UCHAR txV2Igain;
  A_UCHAR txldBloqgain;
 } MERLIN_TXGAIN_TBL; 

typedef struct venus_txgain_tbl {
  A_UCHAR desired_gain ;
  A_UCHAR txbb1dbgain;
  A_UCHAR txbb6dbgain;
  A_UCHAR txmxrgain;
  A_UCHAR padrvgna;
  A_UCHAR padrvgnb;
  A_UCHAR padrvgnc;
  A_UCHAR padrvgnd;
  A_UCHAR enablepal;
 } VENUS_TXGAIN_TBL; 


// devState Defines
#define INIT_STATE 1
#define CONT_ACTIVE_STATE 2
#define RESET_STATE 3

typedef	struct chainSpecificParams
{
	MDK_EEP_HEADER_INFO		*p16kEepHeader;
	RAW_DATA_STRUCT_GEN3 *pGen3RawData[3];
	EEPROM_FULL_DATA_STRUCT_GEN3 *pGen3CalData;
} CHAIN_SPECIFIC_PARAM;

typedef	struct libDevInfo1
{
	A_UINT32 devNum;	   //Copy of the devices devNum
	DEVICE_MAP    devMap;      // The deviceMap given during deviceInitialize
	A_UINT32		 ar5kInitIndex;

	A_UINT32      macRev;      // The Mac revision number
	TXPOWER_SETUP txPowerData; // Struct for the EEPROM calibration data
	mdk_EEP_MAP       eepData;     // Struct for holding the new EEPROM calibration data
	WLAN_MACADDR  macAddr;	   // MAC address for this device
	WLAN_MACADDR  bssAddr;	   // BSS address
	TX_SETUP      tx[MAX_TX_QUEUE];		// Struct with data frame transmit info
	A_BOOL      txProbePacketNext;		// Struct with data frame transmit info
	A_UINT16    backupSelectQueueIndex;
	RX_SETUP      rx;		// Struct with data frame receive info
	MEM_SETUP     mem;         // Struct with memory setup info
	A_UINT32      turbo;       // Turbo state of this device
	A_UINT32      devState;    // Current state of the device
	A_UINT32      remoteStats; // set if have remote stats - specifies stats type
	TX_STATS_STRUCT txRemoteStats[MAX_TX_QUEUE][STATS_BINS]; // transmit stats from remote station 
	RX_STATS_STRUCT rxRemoteStats[MAX_TX_QUEUE][STATS_BINS]; // Receive stats from remote station 
	ATHEROS_REG_FILE *regArray;
	PCI_REG_VALUES   *pciValuesArray;
	A_UINT16         sizeRegArray;
	A_UINT16         sizePciValuesArray;
	A_UINT32         rfRegArrayIndex;
	PCI_REG_VALUES   *pRfPciValues;
	RF_REG_INFO		 rfBankInfo[NUM_RF_BANKS];
	MODE_INFO *pModeArray;
	A_UINT16  sizeModeArray;
	A_BOOL           regFileRead;
	A_CHAR           regFilename[128];
	A_UINT32         aRevID;    //analog revID
	A_UINT32		 aBeanieRevID;
	A_UINT32         hwDevID;    //pci devID read from hardware
	A_UINT32         swDevID;    //more unique identifier of chipsets used by sw
	A_UINT32		 bbRevID;
	A_UINT32		 subSystemID;
	A_BOOL           wepEnable;
	A_UCHAR          wepKey;
	A_BOOL			 eePromLoad;		//set by user on whether to load eeprom
	A_BOOL			 eePromHeaderLoad;
	A_BOOL			eepromHeaderChecked;
	A_UINT16		sizePowerTable;
	A_UINT16		selQueueIndex;
	A_UINT32			start;						// start time for tx start/begin
	A_UINT16		enablePAPreDist;
	A_UINT16		paRate;
	A_UINT32		paPower;
	A_UCHAR			mode;
	MDK_PCDACS_ALL_MODES	*pCalibrationInfo;
	MDK_EEP_HEADER_INFO		*p16kEepHeader;
	MDK_TRGT_POWER_ALL_MODES *p16KTrgtPowerInfo;
//	MDK_TRGT_POWER_INFO		 *p16KTrgtPowerInfo;
	MDK_RD_EDGES_POWER		*p16KRdEdgesPower;
	A_UINT32		freqForResetDevice;			//frequency value set in resetDevice
	A_BOOL			adjustTxThresh;
	A_BOOL			specialTx100Pkt;
	A_BOOL			readThrBeforeBackoff;
	A_UINT32		suppliedFalseDetBackoff[3];
	A_UINT32		txDescStatus1;
	A_UINT32		txDescStatus2;
	A_UINT32		decryptErrMsk;
	A_UINT32		bitsToRxSigStrength;
	A_UINT32		rxDataRateMsk;
	RAW_DATA_STRUCT_GEN3 *pGen3RawData[3];
	EEPROM_FULL_DATA_STRUCT_GEN3 *pGen3CalData;
	LIB_PARAMS		libCfgParams;	
	A_INT32 mdkErrno;
    A_CHAR mdkErrStr[SIZE_ERROR_BUFFER];
    struct earHeader    *pEarHead;          /* All EAR information */
    A_UINT16		use_init;
	A_INT16         pwrIndexOffset;
	ART_ANI_LADDER  artAniLadder[3];  // 1 for each NI/BI/SI
	ART_ANI_SETUP   artAniSetup;
	A_UINT32        eepromStartLoc; // eeprom start location. default = 0x00, but for dual 11a set to 0x400
	A_INT32         maxLinPwrx4;    // 4 x max linear power at current operating channel. 
	                                // valid only for eep_map = 1 format
	A_UINT32        startOfRfPciValues;  // index of beginning of rf pci writes in the big pci writes array
	A_BOOL			eepromHeaderApplied[4];
	RAW_DATA_STRUCT_GEN5 *pGen5RawData[3];
	EEPROM_FULL_DATA_STRUCT_GEN5 *pGen5CalData;
	A_UINT32		channelMasks;  //get passed into resetDevice via the turbo param
	A_UINT32        antRssiDescStatus;
	CHAIN_SPECIFIC_PARAM  chain[4];  // information specific to specific chain
	A_BOOL          blockFlashWriteOn;  // for single location writes for falcon (flash based) designs
	A_BOOL          noEndPacket; // for use with falcon 11g synthesizer phase offset
	A_UINT16		mdkPacketType;
	A_BOOL			yesAgg;
	A_UINT32        aggSize;

    struct ar6kEeprom   *ar6kEep;
    struct ar5416Eeprom *ar5416Eep;
	struct ar9285Eeprom *ar9285Eep;
    struct ar9287Eeprom* ar9287Eep;
    
	A_BOOL          generatedMerlinGainTable;
    A_UINT32        femBandSel;
    A_UINT32        spurChans[5];
    A_UINT32        fastClk5g;       /* fast clk mode in 5G */
    A_BOOL          isForcedPALOn;
    A_UINT32        eepromFileRead;
    A_UINT32        paprdEnabled; 
    FILE*	    prom_file_handle;
#ifdef SUPER_TX_GAIN_CFG
    A_BOOL          xpaCfgEnable;
#endif
} LIB_DEV_INFO;


typedef	struct pwrCtrlParams
{
	A_BOOL        initialized[3];	   //flag to capture the first time
	A_UINT32      rf_wait_I[3];       // pwr ctl params need to be stored.
	A_UINT32      rf_wait_S[3];       // pwr ctl params need to be stored.
	A_UINT32      rf_max_time[3];       // pwr ctl params need to be stored.
	A_UINT32      bb_active_to_receive[3];       // pwr ctl params need to be stored.
	A_UINT32      rf_pd_period_a[3];       // pwr ctl params need to be stored.
	A_UINT32      rf_pd_period_xr[3];       // pwr ctl params need to be stored.
	A_UINT32      rf_pd_delay_a[3];       // pwr ctl params need to be stored.
	A_UINT32      rf_pd_delay_xr[3];       // pwr ctl params need to be stored.
	A_UINT32      bb_tx_frame_to_tx_d_start[3];  
} PWR_CTL_PARAMS;

#define 	MDK_INIT_CODE		0
#define 	DRIVER_INIT_CODE	1

// Data prototypes

/* LIB_INFO structure will hold the library global information.
 */
typedef struct libInfo	{
	A_UINT32           devCount;                  // No. of currently connected devices 
	struct libDevInfo1 *pLibDevArray[LIB_MAX_DEV]; // Array of devinfo pointers 
} LIB_INFO;

// Macros to devMap defined functions
#define REGR(x, y) (gLibInfo.pLibDevArray[x]->devMap.OSregRead(x, y + (gLibInfo.pLibDevArray[x]->devMap.DEV_REG_ADDRESS)))
#define REGW(x, y, z) (gLibInfo.pLibDevArray[x]->devMap.OSregWrite(x, y + (gLibInfo.pLibDevArray[x]->devMap.DEV_REG_ADDRESS), z))

#define MEM32READ(x, y) (gLibInfo.pLibDevArray[x]->devMap.OSmem32Read(x, y ))
#define MEM32WRITE(x, y, z) (gLibInfo.pLibDevArray[x]->devMap.OSmem32Write(x, y, z))

#if defined(SOC_LINUX)
#define sysRegRead(y) (gLibInfo.pLibDevArray[devNum]->devMap.OSapRegRead32(devNum, y))
#define sysRegWrite(y, z) (gLibInfo.pLibDevArray[devNum]->devMap.OSapRegWrite32(devNum, y, z))
#endif

// Func Prototypes
int mError(A_UINT32 devNum, A_UINT32 error, const char * format, ...);
A_UINT32 reverseBits(A_UINT32 val, int bit_count);
A_BOOL checkDevNum(A_UINT32 devNum); 
A_BOOL parseAtherosRegFile(LIB_DEV_INFO *pLibDev, char *filename);
#ifdef NART_BUILD
A_BOOL parseAcSettingRegFile(LIB_DEV_INFO *pLibDev, char *filename);
#endif
A_BOOL createPciRegWrites(A_UINT32 devNum);
//A_UINT16 new_createRfPciValues(A_UINT32 devNum, A_UINT32 bank, A_BOOL writePCI);
void init_extern_bb_fb40(A_UINT32 devNum);
A_BOOL  setupEEPromMap(A_UINT32 devNum);
A_INT32 findDevTableEntry(A_UINT32 devNum);
A_BOOL borrowAnalogAccess(A_UINT32 devNum);
void returnAnalogAccess(A_UINT32 devNum);
A_INT32 InfoPrintf (const char * format, ...);

//extern void memDisplay(A_UINT32 devNum, A_UINT32 address, A_UINT32 nWords);

void griffin_cl_cal(A_UINT32 devNum);   //++JC++
void sleep_cal(clock_t wait );          //++JC++

#define A_MIN(x, y)  (((x) < (y)) ? (x) : (y))
// Integer divide of x/y with round-up
#define A_DIV_UP(x, y) ( ((x) + (y) - 1) / y)
#define I2DBM(x) ((A_UCHAR)((x * 2) + 3))


#include "common_defs.h"

extern MANLIB_API A_BOOL generateTxGainTblFromCfg(A_UINT32 devNum, A_UINT32 modeMask);
extern MANLIB_API A_BOOL generateVenusTxGainTblFromCfg(A_UINT32 devNum, A_UINT32 modeMask);
extern void  GetTxGainTable(MERLIN_TXGAIN_TBL** pGainTbl, A_UCHAR mode);

extern A_UINT32 checkSumLengthLib; //default to 16k
extern A_UINT32 eepromSizeLib;
#ifndef THIN_CLIENT_BUILD
// Global externs
extern LIB_INFO gLibInfo;
extern A_UINT32 lib_memory_range;
extern A_UINT32 cache_line_size;
extern PCI_VALUES pciValues[MAX_PCI_ENTRIES];
#endif

//-----------------------------------------------------------------------------
// Chip dependent constants functions
//-----------------------------------------------------------------------------
A_UINT16 GetMaxTxQueue();

//#define MAX_TX_QUEUE        GetMaxTxQueue()

#if defined(WIN32) || defined(WIN64)
#pragma pack (pop)
#endif	

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif // __INCmconfigh
