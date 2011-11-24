#ifndef __INCmeepromh
#define __INCmeepromh

#if defined(WIN32) || defined(WIN64)
#pragma pack (push, 1)
#endif

//IMPORTANT:    change SCALE and DELTA together
#define SCALE       100         //scale everything by this amount so get reasonalbe acuracy 
                                //but don't need to do float arithmatic
#define DELTA       10          //SCALE/10, but don't want do divide arithmatic each time
#define PWR_MIN     0           
#define PWR_MAX     3150        //31.5*SCALE
#define PWR_STEP    50          //0.5*SCALE
// Keep 2 above defines together


//#define   NUM_EEPROM_CHANNELS     8
#define NUM_11A_EEPROM_CHANNELS 10
#define NUM_PCDAC_VALUES        11
#define NUM_2_4_EEPROM_CHANNELS 3
#define NUM_2_4_EEPROM_CHANNELS_GEN5 4
//#define FREQ_MASK             0x3f
#define FREQ_MASK_16K           0x7f
#define NEW_FREQ_MASK_16K       0xff
#define PCDAC_MASK              0x3f
#define POWER_MASK              0x3f
#define NUM_TEST_FREQUENCIES    8

#define CHANNEL_START   5170 
#define CHANNEL_STOP    5800
#define CHANNEL_STEP    5
#define PCDAC_START     1
#define PCDAC_STOP      63
#define PCDAC_STEP      1
#define PWR_TABLE_SIZE  64
#define NUM_CTL_EEP3_2  16
#define NUM_CTL_EEP3_3  32
#define MAX_NUM_CTL     32

#define PDADC_TABLE_SIZE    128

/*
#define FCC             0x10                    // US
#define DOC             0x20                    // Canada
#define ETSI            0x30                    // most of Europe
#define SPAIN           0x31                    // Spain
#define FRANCE          0x32                    // France
#define MKK             0x40                    // Japan
#define LAB             0xfe                    // Full spectrum from 5170 - 5700
*/

#ifndef VXWORKS
#define NONE            0xff                    // not available
#endif

//#define CHANNEL_POWER_INFO    8
//#define NUM_EDGES     6

#define TENX_OFDM_CCK_DELTA_INIT    15  //(power 1.5 dbm)
#define TENX_CH14_FILTER_CCK_DELTA_INIT 15  //(power 1.5 dbm)
#define OFDM_CCK_GAIN_DELTA_INIT   15  //(PAR difference of 7.5 dbm)

//codes for the eeprom structure types
#define EEP_HEADER_16K               0
#define EEP_CHANNEL_INFO_16K         1
#define EEP_TRGT_POWER_16K           2
#define EEP_RD_POWER_16K             3
#define EEP_GEN3_CHANNEL_INFO        4
#define EEP_GEN5_CHANNEL_INFO        5
#define EEP_AR6000                   6
#define EEP_GEN3_CHANNEL_INFO_CHAIN1 7
#define EEP_HEADER_16K_CHAIN1        8
#define EEP_AR5416                   9

#define NUM_CORNER_CAL_POINTS       4

#define XR_POWER_INDEX              15

//eeprom offset locations
#define HDR_VERSION         0xc1

typedef enum ConformanceTestLimits {
    FCC        = 0x10,
    MKK        = 0x40,
    ETSI       = 0x30,
    SD_NO_CTL  = 0xE0,
    NO_CTL     = 0xFF,
    CTL_MODE_M = 0xF,
#ifdef NART_BUILD
    CTL_MODE_M_AR6000 = 7,
#endif
    CTL_11A    = 0,
    CTL_11B    = 1,
    CTL_11G    = 2,
    CTL_TURBO  = 3,
    CTL_108G   = 4,
    CTL_2GHT20 = 5,
    CTL_5GHT20 = 6,
    CTL_2GHT40 = 7,
    CTL_5GHT40 = 8,
} ATH_CTLS;

typedef enum Ar_Rates {
    rate6mb,  rate9mb,  rate12mb, rate18mb,
    rate24mb, rate36mb, rate48mb, rate54mb,
    rate1l,   rate2l,   rate2s,   rate5_5l,
    rate5_5s, rate11l,  rate11s,  rateXr,
    rateHt20_0, rateHt20_1, rateHt20_2, rateHt20_3,
    rateHt20_4, rateHt20_5, rateHt20_6, rateHt20_7,
    rateHt40_0, rateHt40_1, rateHt40_2, rateHt40_3,
    rateHt40_4, rateHt40_5, rateHt40_6, rateHt40_7,
    rateDupCck, rateDupOfdm, rateExtCck, rateExtOfdm,
    ArRateSize, Ar5416RateSize = ArRateSize,
} AR_RATES;

typedef struct eepromChannelFreqMhz {
    A_UINT16 synthCenter;
    A_UINT16 ctlCenter;
    A_UINT16 extCenter;
    A_UINT16 ht40Center;
    A_UINT16 txMask;
    A_UINT16 activeTxChains;
    A_BOOL   is2GHz;
    A_BOOL   isHt40;
} __ATTRIB_PACK EEPROM_CHANNEL;

//eeprom offsets
typedef struct eepOffsetStruct {
    A_UINT16 HDR_COUNTRY_CODE;
    A_UINT16 HDR_MODE_DEVICE_INFO;
    A_UINT16 HDR_ANTENNA_GAIN;
    A_UINT16 HEADER_MAC_FEATURES;
    A_UINT16 HDR_11A_COMMON;
    A_UINT16 HDR_11B_COMMON;
    A_UINT16 HDR_11G_COMMON;
    A_UINT16 HDR_CTL;
    A_UINT16 HDR_11A_SPECIFIC;
    A_UINT16 HDR_11B_SPECIFIC;
    A_UINT16 HDR_11G_SPECIFIC;
    A_UINT16 GROUP1_11A_FREQ_PIERS;
    A_UINT16 GROUP2_11A_RAW_PWR;
    A_UINT16 GROUP3_11B_RAW_PWR;
    A_UINT16 GROUP4_11G_RAW_PWR;
    A_UINT16 GROUP5_11A_TRGT_PWR;
    A_UINT16 GROUP6_11B_TRGT_PWR;
    A_UINT16 GROUP7_11G_TRGT_PWR;
    A_UINT16 GROUP8_CTL_INFO;
} EEP_OFFSET_STRUCT;

typedef struct mdk_dataPerChannel {
    A_UINT16        channelValue;
    A_UINT16        pcdacMin;
    A_UINT16        pcdacMax;
    A_UINT16        numPcdacValues;
    A_UINT16        PcdacValues[NUM_PCDAC_VALUES];
    A_INT16         PwrValues[NUM_PCDAC_VALUES];        
} MDK_DATA_PER_CHANNEL;

typedef struct mdk_pcdacsAllModes {     
    //lla info
    A_UINT16            Channels_11a[NUM_11A_EEPROM_CHANNELS];
    A_UINT16            numChannels_11a;
    MDK_DATA_PER_CHANNEL    DataPerChannel_11a[NUM_11A_EEPROM_CHANNELS];        

    A_UINT16            numChannels_2_4;
    A_UINT16            Channels_11b[NUM_2_4_EEPROM_CHANNELS];
    A_UINT16            Channels_11g[NUM_2_4_EEPROM_CHANNELS];

    //11g info
    MDK_DATA_PER_CHANNEL    DataPerChannel_11g[NUM_2_4_EEPROM_CHANNELS];        

    //11b info
    MDK_DATA_PER_CHANNEL    DataPerChannel_11b[NUM_2_4_EEPROM_CHANNELS];        
} MDK_PCDACS_ALL_MODES;

//points to the appropriate pcdac structs in the above struct based on mode
typedef struct mdk_pcdacsEeprom {       
    A_UINT16            *pChannelList;
    A_UINT16            numChannels;
    MDK_DATA_PER_CHANNEL    *pDataPerChannel;       
} MDK_PCDACS_EEPROM;

typedef struct mdk_fullPcdacStruct {
    A_UINT16        channelValue;
    A_UINT16        pcdacMin;
    A_UINT16        pcdacMax;
    A_UINT16        numPcdacValues;
    A_UINT16        PcdacValues[PWR_TABLE_SIZE];
    A_INT16         PwrValues[PWR_TABLE_SIZE];  
} MDK_FULL_PCDAC_STRUCT;


typedef struct powerTable {
    A_UINT16            channelValue;
    A_UINT16            pcdacValues[PWR_TABLE_SIZE];
} POWER_TABLE;

typedef struct rdMaxPower {
    A_INT16 twicePwr54;
    A_INT16 twicePwr48;
    A_INT16 twicePwr36;
    A_INT16 twicePwrRD1;
    A_INT16 twicebkoffRD1;
//  A_UINT16    regDomain;
} RD_MAX_POWER;

typedef struct mdk_trgtPowerInfo {
    A_INT16 twicePwr54;
    A_INT16 twicePwr48;
    A_INT16 twicePwr36;
    A_INT16 twicePwr6_24;
    A_INT16 testChannel;
} MDK_TRGT_POWER_INFO;

typedef struct mdk_trgtPowerAllModes {
    MDK_TRGT_POWER_INFO trgtPwr_11a[NUM_TEST_FREQUENCIES];
    A_UINT16        numTargetPwr_11a;

    MDK_TRGT_POWER_INFO trgtPwr_11g[3];
    A_UINT16        numTargetPwr_11g;

    MDK_TRGT_POWER_INFO trgtPwr_11b[2];
    A_UINT16        numTargetPwr_11b;

} MDK_TRGT_POWER_ALL_MODES;

typedef struct mdk_rdEdgesPower {
    A_UINT16    rdEdge;
    A_INT16 twice_rdEdgePower;
    A_UINT16        flag;
} MDK_RD_EDGES_POWER;

// Variable are made 16bits to support swapping(if necessary due to endian change) 
// for remote clients.
typedef struct turboHeaderInfo {
    A_INT16 max2wPower;
    A_UINT16 switchSettling;
    A_UINT16 txrxAtten;
    A_UINT16 rxtxMargin;
    A_INT16 adcDesiredSize; // 8-bit signed value
    A_INT16 pgaDesiredSize; // 8-bit signed value
} TURBO_HEADER_INFO;

typedef struct modeHeaderInfo
{
    A_UINT16 switchSettling;
    A_UINT16 txrxAtten;
    A_UINT16 antennaControl[11];
    A_INT16 adcDesiredSize; // 8-bit signed value
    A_UINT16 ob_1;
    A_UINT16 db_1;
    A_UINT16 ob_2;
    A_UINT16 db_2;
    A_UINT16 ob_3;
    A_UINT16 db_3;
    A_UINT16 ob_4;          //hold ob_11 b and g
    A_UINT16 db_4;          //hold db_11 b and g
    A_UINT16 txEndToXLNAOn;
    A_UINT16 thresh62;           
    A_UINT16 txEndToXPAOff;  
    A_UINT16 txFrameToXPAOn; 
    A_INT16 pgaDesiredSize; // 8-bit signed value
    A_INT16 noisefloorThresh;
    A_UINT16 xlnaGain;
    A_UINT16 xgain; 
    A_UINT16 xpd; 

    //added for 3.4 eeprom header
    A_UINT16 initialGainI;
    A_UINT16 calPier1;
    A_UINT16 calPier2;
    A_UINT16 calPier3;  //4.0 eeprom header
    A_INT16 turbo2wMaxPower;
    //eventually will have mode specific section
    A_UINT16            falseDetectBackoff;
    A_INT16 xrTargetPower;

    //added for 4.0n eeprom header
    A_UINT16 iqCalI;
    A_UINT16 iqCalQ;
    A_UINT16 rxtxMargin;
    A_UINT16 turboDisable; // Added as part of modespecific variable; 
    TURBO_HEADER_INFO turbo;

    // added for 4.9 header. this is a special falcon branch off of 4.8 branch
    // mainline is at 5.2 at this time
    A_UINT32 phaseCal[10];

} MODE_HEADER_INFO;


typedef struct cornerCalInfo {
    A_UINT16 gSel;
    A_UINT16 pd84;
    A_UINT16 pd90;
    A_UINT16 clip;
} CORNER_CAL_INFO;

typedef struct mdk_eepHeaderInfo 
{
    A_UINT16 countryRegCode;
    A_UINT16 countryCodeFlag;
    A_UINT16 worldwideRoaming;
    //A_UINT16 turboDisable; // need to be commented once it is replaced
    A_UINT16 RFKill;
    A_UINT16 deviceType;
    A_UINT16 Amode;
    A_UINT16 Bmode;
    A_UINT16 Gmode;
    A_UINT16 minorVersion;
    A_UINT16 majorVersion;
    A_UINT16 xtnd11a;
    A_UINT16 antennaGain5;
    A_UINT16 antennaGain2_4;
    A_UINT16 numCtl;

    //11a parameters (5GHz)
    MODE_HEADER_INFO    info11a;

    //11g parameters 
    MODE_HEADER_INFO    info11g;

    //11b parameters
    MODE_HEADER_INFO    info11b;

    A_UINT16 testGroups[MAX_NUM_CTL];

    //corner calibration information
    CORNER_CAL_INFO     cornerCal[NUM_CORNER_CAL_POINTS];   

    A_UINT16        scaledOfdmCckDelta;
    A_UINT16        scaledCh14FilterCckDelta;

    //eeprom 4.0 header info
    A_UINT16        earStartLocation;
    A_UINT16        trgtPowerStartLocation;
    A_UINT16        calStartLocation;
    A_UINT16        eepMap;
    A_UINT16        fixedBiasA;
    A_UINT16        fixedBiasB;
    A_UINT16        enable32khz;
    A_UINT16        oldEnable32khz;
    A_UINT16        maskRadio0;
    A_UINT16        maskRadio1;

    //introduced in eeprom 4.2 
    A_INT16         ofdmCckGainDeltaX2;

    //introduced in eeprom 4.4
    A_UINT16        eepFileVersion;
    A_UINT16        earFileVersion;
    A_UINT16        earFileIdentifier;
    A_UINT16        artBuildNumber;

        // Introduced for Falcon
    A_UINT16        tx_chain_mask;
    A_UINT16        rx_chain_mask;

    // Introduced to display MAC Settings
    
    A_UINT16        keyCacheSize;
    A_UINT16        enableClip;
    A_UINT16        maxNumQCU;
    A_UINT16        burstingDisable;
    A_UINT16        fastFrameDisable;
    A_UINT16        aesDisable;
    A_UINT16        compressionDisable;
    A_UINT16        disableXR;

    //new regulatory flags
    A_UINT16   enableFCCMid;
    A_UINT16   enableJapanEvenU1;
    A_UINT16   enableJapenU2;
    A_UINT16   enableJapnMid;
    A_UINT16   disableJapanOddU1;
    A_UINT16   enableJapanMode11aNew;
    A_UINT16   enableFCCDfsHt40;
    A_UINT16   enableJapanHt40;
    A_UINT16   enableJapanDfsHt40;

} MDK_EEP_HEADER_INFO;


void 
mapPcdacTable
(
 MDK_PCDACS_EEPROM *pSrcStruct, 
 MDK_FULL_PCDAC_STRUCT *pPcdacStruct
);


A_INT16 getScaledPower
(
 A_UINT16           Channel,
 A_UINT16           pcdacValue,
 MDK_PCDACS_EEPROM      *pSrcStruct
);


A_BOOL
findValueInList
(
 A_UINT16           channel,
 A_UINT16           pcdacValue,
 MDK_PCDACS_EEPROM      *pSrcStruct,
 A_INT16            *powerValue         //return power value found in src struct
);

void getLowerUpperValues 
(
 A_UINT16   value,          //value to search for
 A_UINT16   *pList,         //ptr to the list to search
 A_UINT16   listSize,       //number of entries in list
 A_UINT16   *pLowerValue,   //return the lower value
 A_UINT16   *pUpperValue    //return the upper value    
);

void getLowerUpperValues 
(
 A_UINT16   value,          //value to search for
 A_UINT16   *pList,         //ptr to the list to search
 A_UINT16   listSize,       //number of entries in list
 A_UINT16   *pLowerValue,   //return the lower value
 A_UINT16   *pUpperValue    //return the upper value    
);

void getLeftRightChannels
(
 A_UINT16           channel,            //channel to search for
 MDK_PCDACS_EEPROM      *pSrcStruct,        //ptr to struct to search
 A_UINT16           *pLowerChannel,     //return lower channel
 A_UINT16           *pUpperChannel      //return upper channel
);

void getLowerUpperPcdacs 
(
 A_UINT16           pcdac,              //pcdac to search for
 A_UINT16           channel,            //current channel
 MDK_PCDACS_EEPROM      *pSrcStruct,        //ptr to struct to search
 A_UINT16           *pLowerPcdac,       //return lower pcdac
 A_UINT16           *pUpperPcdac        //return upper pcdac
);

void getPwrTable 
(
 MDK_FULL_PCDAC_STRUCT *pPcdacStruct,
 A_UINT16           *pPwrTable      //ptr to power table to fill
);

void getPcdacInterceptsFromPcdacMinMax 
(
    A_UINT16    pcdacMin,
    A_UINT16    pcdacMax,
    A_UINT16    *pPcdacValues   //return the pcdac value
);

void    freeEepromStruct
(
 MDK_PCDACS_EEPROM  *pEepromStruct
);

void printEepromStruct
(
 MDK_PCDACS_EEPROM  *pEepromData
);

void getMaxRDPowerlistForFreq
(
 A_UINT32       devNum,
 A_UINT16       channel,
 A_INT16        *pRatesPower
);

A_BOOL  buildSingleChannelSkeletonStruct
(
 MDK_FULL_PCDAC_STRUCT  **ppPcdacStruct,
 A_UINT16 channelValue

);


void printPcdacTable
(
 A_UINT16       *pPcdacs,
 A_UINT16       numChannels
);

void printPowerTxMax 
(
 A_INT16        *pRatesPower
);

void printRDTable
( 
 RD_MAX_POWER   *pRdMaxPwrInfo,
 char *string
);

void printToLogFile
(
    const char * format,
    ...
);

void printEepromStructFormated
(
 MDK_PCDACS_EEPROM  *pEepromData
);


 A_BOOL readEepromIntoDataset
(
 A_UINT32           devNum
);

void  read_chain1_antenna_control
(
 A_UINT32           devNum,
 MDK_EEP_HEADER_INFO    *pHeaderInfo    
);

void  readHeaderInfo
(
 A_UINT32           devNum,
 MDK_EEP_HEADER_INFO    *pHeaderInfo
);

void  programHeaderInfo
(
 A_UINT32           devNum,
 MDK_EEP_HEADER_INFO    *pHeaderInfo,
 A_UINT16           freq,
 A_UCHAR            mode
);

A_BOOL eepromVerifyChecksum
(
 A_UINT32   devNum
);

void free16KEepStructs
(
 A_UINT32           devNum
);

A_BOOL allocateEepStructs
(
 A_UINT32           devNum
);

void freeEepStructs
(
 A_UINT32           devNum
);

void readFreqPiers
(
 A_UINT32 devNum, 
 A_UINT16 *pChannels, 
 A_UINT16 numChannels
);

A_BOOL readTrgtPowers
(
 A_UINT32 devNum,
 A_UINT16 offset,
 MDK_TRGT_POWER_INFO *pPowerInfo,
 A_UINT16 mode
);

A_UINT16 readCtlInfo
(
 A_UINT32 devNum,
 A_UINT16 offset,
 MDK_RD_EDGES_POWER *pRdEdgePwrInfo
);

void applyFalseDetectBackoff
(
 A_UINT32 devNum,
 A_UINT32 freq,
 A_UINT32 backoffAmount
);

void getPwrTableByModeCh 
(
 A_UINT32 devNum, 
 A_UINT32 mode, 
 A_UINT32 freq, 
 A_UINT16 *powerValues
 );

void forceChainPowerTxMaxVenice
(
 A_UINT32   devNum,
 A_INT16    *pRatesPower, 
 A_UINT16   *pPowerValues, 
 double     ofdm_cck_delta,
 A_UINT32   chainNum
);

/*
void forcePowerTxMaxVenice
(
 A_UINT32   devNum,
 A_INT16    *pRatesPower, 
 A_UINT16   *pPowerValues, 
 double     ofdm_cck_delta
);
*/

void fillCCKMaxPower
(
 A_UINT32       devNum,
 A_UINT16       channel,            //input channel value
 A_INT16        *pRatesPower        //pointer to power/rate table to fill
);

A_BOOL readEEPData_16K
(
 A_UINT32   devNum
);

void
getMaxMinPower
(
 A_INT16 powerArray[],
 A_INT16 *minPower,
 A_INT16 *maxPower
);

A_INT32
getCtlPower
(
 A_UINT32 devNum,
 A_UINT32 ctl,
 A_UINT32 freq,
 A_UINT32 mode,
 A_UINT32 turbo
);

A_UINT32 getEepromSize(A_UINT32 devNum,A_UINT32 *eepromSize,A_UINT32 *checkSumLength);

extern EEP_OFFSET_STRUCT *pOffsets;

#if defined(WIN32) || defined(WIN64)
#pragma pack (pop)
#endif	

#define HASH2GCHAN(freq)   (((freq) - 2410) /5)
#define HASH5GCHAN(freq)   (((((freq) - 5000) /5) - 34) /16)

#endif

