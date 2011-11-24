#ifndef __INCmEEPROM_g
#define __INCmEEPROM_g
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define MAX_NUM_PDGAINS_PER_CHANNEL	 4
#define NUM_PDGAINS_PER_CHANNEL		 2
#define	NUM_POINTS_LAST_PDGAIN		 5
#define	NUM_POINTS_OTHER_PDGAINS	 4
#define	XPD_GAIN1_GEN5	 			 3
#define	XPD_GAIN2_GEN5	 			 1

#define MAX_PWR_RANGE_IN_HALF_DB	 64

#define PD_GAIN_BOUNDARY_STRETCH_IN_HALF_DB  0

typedef struct _rawDataPerPDGAIN {
	A_UINT16	pd_gain;
	A_UINT16	numVpd;
	A_UINT16	*Vpd;
	A_INT16		*pwr_t4;  // or gainF
} RAW_DATA_PER_PDGAIN_GEN5;

typedef struct _rawDataPerChannelGen5 {
	A_UINT16					channelValue;
	A_INT16						maxPower_t4;	
	A_UINT16                    numPdGains;       // # Pd Gains per channel
	RAW_DATA_PER_PDGAIN_GEN5	*pDataPerPDGain;
} RAW_DATA_PER_CHANNEL_GEN5;


typedef struct _rawDataStructGen5 {
	A_UINT16					*pChannels;
	A_UINT16					numChannels;
	A_UINT16					xpd_mask;		  // mask inicating which xpd_gains are permitted	
	RAW_DATA_PER_CHANNEL_GEN5	*pDataPerChannel; //ptr to array of info held per channel
} RAW_DATA_STRUCT_GEN5;

typedef struct _eepromDataPerChannelGen5 {
	A_UINT16		channelValue;
	A_UINT16        numPdGains;  // number of pdGains to be stored on eeprom per channel
	A_UINT16		Vpd_I[MAX_NUM_PDGAINS_PER_CHANNEL];    
	A_INT16			pwr_I[MAX_NUM_PDGAINS_PER_CHANNEL];
	A_UINT16		Vpd_delta[NUM_POINTS_LAST_PDGAIN][MAX_NUM_PDGAINS_PER_CHANNEL];
	A_INT16			pwr_delta_t2[NUM_POINTS_LAST_PDGAIN][MAX_NUM_PDGAINS_PER_CHANNEL];
	A_INT16			maxPower_t4;
} EEPROM_DATA_PER_CHANNEL_GEN5;

typedef struct _eepromDataStructGen5 {
	A_UINT16						*pChannels;
	A_UINT16						numChannels;
	A_UINT16						xpd_mask;			// mask inicating which xpd_gains are permitted
	EEPROM_DATA_PER_CHANNEL_GEN5	*pDataPerChannel;	// ptr to array of info held per channel
} EEPROM_DATA_STRUCT_GEN5;

//contiguous struct for passing from library to upper level software
typedef struct _eepromFullDataStructGen5 {
	A_UINT16						numChannels11a;
	A_UINT16						xpd_mask11a;		
	EEPROM_DATA_PER_CHANNEL_GEN5	pDataPerChannel11a[NUM_11A_EEPROM_CHANNELS];
	A_UINT16						numChannels11b;
	A_UINT16						xpd_mask11b;		
	EEPROM_DATA_PER_CHANNEL_GEN5	pDataPerChannel11b[NUM_2_4_EEPROM_CHANNELS_GEN5];
	A_UINT16						numChannels11g;
	A_UINT16						xpd_mask11g;	
	EEPROM_DATA_PER_CHANNEL_GEN5	pDataPerChannel11g[NUM_2_4_EEPROM_CHANNELS_GEN5];
} EEPROM_FULL_DATA_STRUCT_GEN5;

A_BOOL initialize_datasets_gen5(A_UINT32 devNum, EEPROM_DATA_STRUCT_GEN5 *pCalDataset_gen5[], RAW_DATA_STRUCT_GEN5  *pRawDataset_gen3[]);
MANLIB_API A_BOOL setup_EEPROM_dataset_gen5(A_UINT32 devNum, EEPROM_DATA_STRUCT_GEN5 *pEEPROMDataset_gen3, A_UINT16 myNumRawChannels, A_UINT16 *pMyRawChanList);
A_BOOL allocateEEPROMDataStruct_gen5(A_UINT32 devNum, EEPROM_DATA_STRUCT_GEN5  *pEEPROMDataset_gen3, A_UINT16 numChannels);
A_BOOL read_Cal_Dataset_From_EEPROM_gen5(A_UINT32 devNum, EEPROM_DATA_STRUCT_GEN5 *pCalDataset, A_UINT32 start_offset, A_UINT32 maxPiers, A_UINT32 *words, A_UINT32 devlibMode);

MANLIB_API A_BOOL setup_raw_dataset_gen5(A_UINT32 devNum, RAW_DATA_STRUCT_GEN5 *pRawDataset_gen3, A_UINT16 myNumRawChannels, A_UINT16 *pMyRawChanList, A_UINT32 swDeviceID);
A_BOOL allocateRawDataStruct_gen5(A_UINT32 devNum, RAW_DATA_STRUCT_GEN5  *pRawDataset_gen3, A_UINT16 numChannels);
void eeprom_to_raw_dataset_gen5(A_UINT32 devNum, EEPROM_DATA_STRUCT_GEN5 *pCalDataset, RAW_DATA_STRUCT_GEN5 *pRawDataset) ;


A_BOOL get_gain_boundaries_and_pdadcs_for_powers
(
 A_UINT32 devNum,                          // In
 A_UINT16 channel,                         // In       
 RAW_DATA_STRUCT_GEN5 *pRawDataset,        // In
 A_UINT16 pdGainOverlap_t2,                // In
 A_INT16 *pMinCalPower,                    // Out	(2 x min calibrated power)
 A_UINT16 pPdGainBoundaries[],             // Out
 A_UINT16 pPdGainValues[],                 // Out
 A_UINT16 pPDADCValues[]                   // Out 
);

void fill_Vpd_Table(A_UINT32 pdGainIdx, A_INT16 Pmin, A_INT16  Pmax, A_INT16 *pwrList, 
					A_UINT16 *VpdList, A_UINT16 numIntercepts, A_UINT16 retVpdList[][64]);
MANLIB_API void mdk_GetLowerUpperIndex_Signed16 (A_INT16 value, A_INT16 *pList, A_UINT16 listSize, A_UINT32 *pLowerValue, A_UINT32 *pUpperValue);
A_UINT16 fbin2freq_gen5(A_UINT32 fbin, A_UINT32 mode);

/*

A_BOOL mdk_getFullPwrTable(A_UINT32 devNum, A_UINT16 numPcdacs, A_UINT16 *pcdacs, A_INT16 *power, A_INT16 maxPower, A_INT16 *retVals);
A_INT16 getPminAndPcdacTableFromPowerTable(A_INT16 *pwrTable_t4, A_UINT16 retVals[]);
A_INT16 getPminAndPcdacTableFromTwoPowerTables(A_INT16 *pwrTableLXPD_t4, A_INT16 *pwrTableHXPD_t4, A_UINT16 retVals[], A_INT16 *Pmid);
*/

void copyGen5EepromStruct(EEPROM_FULL_DATA_STRUCT_GEN5 *pFullCalDataset_gen5, EEPROM_DATA_STRUCT_GEN5 *pCalDataset_gen5[]);

A_BOOL initialize_datasets_forced_eeprom_gen5(A_UINT32 devNum, EEPROM_DATA_STRUCT_GEN5 **pDummyCalDataset_gen5, RAW_DATA_STRUCT_GEN5  **pDummyRawDataset_gen5, A_UINT32 *words, A_UINT16 xpd_mask);


#ifdef __cplusplus
}
#endif

#endif

