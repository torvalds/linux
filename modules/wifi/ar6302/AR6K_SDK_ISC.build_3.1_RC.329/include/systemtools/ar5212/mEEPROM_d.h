#ifndef __INCmEEPROM_d
#define __INCmEEPROM_d
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define NUM_XPD_PER_CHANNEL		 4
#define	NUM_POINTS_XPD0			 4
#define	NUM_POINTS_XPD3			 3
#define IDEAL_10dB_INTERCEPT_2G	 35
#define IDEAL_10dB_INTERCEPT_5G	 55


typedef struct _rawDataPerXPD {
	A_UINT16	xpd_gain;
	A_UINT16	numPcdacs;
	A_UINT16	*pcdac;
	A_INT16		*pwr_t4;  // or gainF
} RAW_DATA_PER_XPD_GEN3;

typedef struct _rawDataPerChannel {
	A_UINT16				channelValue;
	A_INT16					maxPower_t4;				
	RAW_DATA_PER_XPD_GEN3	*pDataPerXPD;
} RAW_DATA_PER_CHANNEL_GEN3;


typedef struct _rawDataStruct {
	A_UINT16					*pChannels;
	A_UINT16					numChannels;
	A_UINT16					xpd_mask;			// mask inicating which xpd_gains are permitted
	RAW_DATA_PER_CHANNEL_GEN3	*pDataPerChannel;		//ptr to array of info held per channel
} RAW_DATA_STRUCT_GEN3;

typedef struct _eepromDataPerChannel {
	A_UINT16		channelValue;
	A_UINT16		pcd1_xg0;
	A_INT16			pwr1_xg0;
	A_UINT16		pcd2_delta_xg0;
	A_INT16			pwr2_xg0;
	A_UINT16		pcd3_delta_xg0;
	A_INT16			pwr3_xg0;
	A_UINT16		pcd4_delta_xg0;
	A_INT16			pwr4_xg0;
	A_INT16			maxPower_t4;
	A_INT16			pwr1_xg3;  // pcdac = 20
	A_INT16			pwr2_xg3;  // pcdac = 35
	A_INT16			pwr3_xg3;  // pcdac = 63
} EEPROM_DATA_PER_CHANNEL_GEN3;

typedef struct _eepromDataStruct {
	A_UINT16						*pChannels;
	A_UINT16						numChannels;
	A_UINT16						xpd_mask;			// mask inicating which xpd_gains are permitted
	EEPROM_DATA_PER_CHANNEL_GEN3	*pDataPerChannel;	//ptr to array of info held per channel
} EEPROM_DATA_STRUCT_GEN3;

//contiguous struct for passing from library to upper level software
typedef struct _eepromFullDataStruct {
	A_UINT16						numChannels11a;
	A_UINT16						xpd_mask11a;	
	EEPROM_DATA_PER_CHANNEL_GEN3	pDataPerChannel11a[NUM_11A_EEPROM_CHANNELS];
	A_UINT16						numChannels11b;
	A_UINT16						xpd_mask11b;		
	EEPROM_DATA_PER_CHANNEL_GEN3	pDataPerChannel11b[NUM_2_4_EEPROM_CHANNELS];
	A_UINT16						numChannels11g;
	A_UINT16						xpd_mask11g;	
	EEPROM_DATA_PER_CHANNEL_GEN3	pDataPerChannel11g[NUM_2_4_EEPROM_CHANNELS];
} EEPROM_FULL_DATA_STRUCT_GEN3;


MANLIB_API A_BOOL setup_raw_dataset_gen3(A_UINT32 devNum, RAW_DATA_STRUCT_GEN3 *pRawDataset_gen3, A_UINT16 myNumRawChannels, A_UINT16 *pMyRawChanList);
MANLIB_API A_BOOL setup_EEPROM_dataset_gen3(A_UINT32 devNum, EEPROM_DATA_STRUCT_GEN3 *pEEPROMDataset_gen3, A_UINT16 myNumRawChannels, A_UINT16 *pMyRawChanList);
A_BOOL allocateRawDataStruct_gen3(A_UINT32 devNum, RAW_DATA_STRUCT_GEN3  *pRawDataset_gen3, A_UINT16 numChannels);
A_BOOL allocateEEPROMDataStruct_gen3(A_UINT32 devNum, EEPROM_DATA_STRUCT_GEN3  *pEEPROMDataset_gen3, A_UINT16 numChannels);
A_BOOL read_Cal_Dataset_From_EEPROM(A_UINT32 devNum, EEPROM_DATA_STRUCT_GEN3 *pCalDataset, A_UINT32 start_offset, A_UINT32 maxPiers, A_UINT32 *words, A_UINT32 devlibMode);
void eeprom_to_raw_dataset_gen3(A_UINT32 devNum, EEPROM_DATA_STRUCT_GEN3 *pCalDataset, RAW_DATA_STRUCT_GEN3 *pRawDataset) ;
MANLIB_API A_INT16 mdk_GetInterpolatedValue_Signed16(A_UINT16 target, A_UINT16 srcLeft, A_UINT16 srcRight, A_INT16 targetLeft, A_INT16 targetRight);
MANLIB_API void mdk_GetLowerUpperIndex (A_UINT16 value, A_UINT16 *pList, A_UINT16 listSize, A_UINT32 *pLowerValue, A_UINT32 *pUpperValue);
A_UINT16 fbin2freq_gen3(A_UINT32 fbin, A_UINT32 mode);

A_BOOL get_xpd_gain_and_pcdacs_for_powers
(
 A_UINT32 devNum,                         // In
 A_UINT16 channel,                         // In       
 RAW_DATA_STRUCT_GEN3 *pRawDataset,        // In
 A_UINT32 numXpdGain,                      // In
 A_UINT32 xpdGainMask,                      // In     - desired xpd_gain
 A_INT16 *pPowerMin,                      // In/Out	(2 x power)
 A_INT16 *pPowerMax,                      // In/Out	(2 x power)
 A_INT16 *pPowerMid,                      // Out		(2 x power)
 A_UINT16 pXpdGainValues[],               // Out
 A_UINT16 pPCDACValues[]                  // Out 
);
A_BOOL initialize_datasets(A_UINT32 devNum, EEPROM_DATA_STRUCT_GEN3 *pCalDataset_gen3[], RAW_DATA_STRUCT_GEN3  *pRawDataset_gen3[]);
A_BOOL mdk_getFullPwrTable(A_UINT32 devNum, A_UINT16 numPcdacs, A_UINT16 *pcdacs, A_INT16 *power, A_INT16 maxPower, A_INT16 *retVals);
A_INT16 getPminAndPcdacTableFromPowerTable(A_UINT32 devNum, A_INT16 *pwrTable_t4, A_UINT16 retVals[]);
A_INT16 getPminAndPcdacTableFromTwoPowerTables(A_UINT32 devNum, A_INT16 *pwrTableLXPD_t4, A_INT16 *pwrTableHXPD_t4, A_UINT16 retVals[], A_INT16 *Pmid);
void
copyGen3EepromStruct(EEPROM_FULL_DATA_STRUCT_GEN3 *pFullCalDataset_gen3, EEPROM_DATA_STRUCT_GEN3 *pCalDataset_gen3[]);



#ifdef __cplusplus
}
#endif

#endif

