/* devlibif.h - contians wrapper functions declarations for devlib */
/*
 * Copyright (c) 2000 Atheros Communications, Inc., All Rights Reserved
 * $ATH_LICENSE_TARGET_C$
 */

#ifndef __INCdevlibifh
#define __INCdevlibifh

#include "wlantype.h"
#include "manlib.h"
#if defined(VXWORKS) || defined(SIM) 
#include "hw.h"
#else
#include "common_hw.h"
#endif
#include "perlarry.h"

A_UINT32 m_eepromRead
(
 	A_UINT32 devNum, 
 	A_UINT32 eepromOffset
);


void m_eepromWrite
(
 	A_UINT32 devNum, 
 	A_UINT32 eepromOffset, 
 	A_UINT32 eepromValue
);

PDWORDBUFFER m_eepromReadBlock
(
	A_UINT32 devNum,
	A_UINT32 startOffset,
	A_UINT32 length
);

void m_eepromWriteBlock
(
 	A_UINT32 devNum, 
 	A_UINT32 startOffset, 
	PDWORDBUFFER pDwordBuffer
);

A_UINT32 m_resetDevice
(
 	A_UINT32 devNum, 
 	PDATABUFFER mac, 
 	PDATABUFFER bss, 
 	A_UINT32 freq, 
 	A_UINT32 turbo 
);

void m_setResetParams
(
 	A_UINT32 devNum, 
 	A_CHAR fileName[256],
 	A_UINT32 eePromLoad,
	A_UINT32 eePromHeaderLoad,
	A_UINT32 mode,
	A_UINT16 initCodeFlag
);

void m_getDeviceInfo
(
 	A_UINT32 devNum, 
 	SUB_DEV_INFO *pDevStruct		
);

A_UINT32 m_checkRegs
(
 A_UINT32 devNum
);

void m_changeChannel
(
 	A_UINT32 devNum,
 	A_UINT32 freq
);

A_UINT32 m_checkProm
(
 	A_UINT32 devNum,
 	A_UINT32 enablePrint
);

void m_rereadProm
(
 	A_UINT32 devNum
);

void m_txDataSetup
(
 	A_UINT32 devNum, 
 	A_UINT32 rateMask, 
 	PDATABUFFER dest, 
 	A_UINT32 numDescPerRate, 
 	A_UINT32 dataBodyLength, 
 	PDATABUFFER dataPattern, 
 	A_UINT32 retries, 
 	A_UINT32 antenna, 
 	A_UINT32 broadcast
);

void m_txDataBegin
(
 	A_UINT32 devNum,
 	A_UINT32 timeout,
 	A_UINT32 remoteStats
);


void m_rxDataSetup
(
 	A_UINT32 devNum,
 	A_UINT32 numDesc,
 	A_UINT32 dataBodyLength,
 	A_UINT32 enablePPM
);

void m_rxDataBegin
(
 	A_UINT32 devNum,
 	A_UINT32 waitTime,
 	A_UINT32 timeout,
 	A_UINT32 remoteStats,
 	A_UINT32 enableCompare, 
 	PDATABUFFER dataPattern
);

void m_txrxDataBegin
(
 	A_UINT32 devNum,
 	A_UINT32 waitTime,
 	A_UINT32 timeout,
 	A_UINT32 remoteStats,
 	A_UINT32 enableCompare, 
 	PDATABUFFER dataPattern
);

void m_cleanupTxRxMemory
(
 	A_UINT32 devNum,
 	A_UINT32 flags
);

PDWORDBUFFER m_txGetStats
(
 	A_UINT32 devNum, 
 	A_UINT32 rateInMb,
 	A_UINT32 remote
);

PDWORDBUFFER m_txPrintStats
(
 	A_UINT32 devNum, 
 	A_UINT32 rateInMb,
 	A_UINT32 remote
);


PDWORDBUFFER m_rxGetStats
(
 	A_UINT32 devNum,
 	A_UINT32 rateInMb,
 	A_UINT32 remote
);


PDWORDBUFFER m_rxPrintStats
(
 	A_UINT32 devNum,
 	A_UINT32 rateInMb,
 	A_UINT32 remote
);

PDATABUFFER m_rxGetData
(
 	A_UINT32 devNum,
 	A_UINT32 bufferNum,
 	A_UINT16 sizeBuffer
);


void m_txContBegin
(
 	A_UINT32 devNum, 
 	A_UINT32 type, 
 	A_UINT32 typeOption1,
 	A_UINT32 typeOption2, 
 	A_UINT32 antenna
);

void m_txContEnd
(
 	A_UINT32 devNum
);

void m_setAntenna
(
 	A_UINT32 devNum, 
 	A_UINT32 antenna
);

void m_setPowerScale
(
 	A_UINT32 devNum, 
 	A_UINT32 powerScale
);

void m_setTransmitPower
(
 	A_UINT32 devNum, 
 	PDATABUFFER txPowerArray
);

void m_setSingleTransmitPower
(
 	A_UINT32 devNum, 
 	A_UCHAR pcdac 
);

void m_devSleep
(
 	A_UINT32 devNum
);

A_UINT16 m_closeDevice
(
    A_UINT32 devNum
);

A_UINT32 m_selectDevice
(
	A_UINT32 devNum,
	A_UINT32 deviceType
);

void m_getEepromStruct
(
 	A_UINT32 devNum,
 	A_UINT16 eepStructFlag,
	void **ppReturnStruct,
	A_UINT32 *pSizeStruct
);

A_INT32 initializeMLIB
(
	MDK_WLAN_DEV_INFO *pdevInfo
);

void closeMLIB
(
	A_UINT32 devNum
);

void m_changeField
(
 	A_UINT32 devNum,
 	A_CHAR *fieldName, 
 	A_UINT32 newValue
);

void m_enableWep
(
 	A_UINT32 devNum,
 	A_UCHAR  key
);

void m_enablePAPreDist
(
 	A_UINT32 devNum,
 	A_UINT16 rate,
 	A_UINT32 power
);


void m_dumpRegs
(
 	A_UINT32 devNum
);


void m_dumpPciWrites
(
 	A_UINT32 devNum
);


A_BOOL m_testLib
(
 	A_UINT32 devNum,
 	A_UINT32 timeout
);


void m_displayFieldValues 
(
 	A_UINT32 devNum,
 	A_CHAR *fieldName,
	A_UINT32 *baseValue,
	A_UINT32 *turboValue

);



A_UINT32 m_getFieldValue
(
 	A_UINT32 devNum,
 	A_CHAR   *fieldName,
 	A_UINT32 turbo,
	A_UINT32 *baseValue,
	A_UINT32 *turboValue

);

A_INT32 m_getFieldForMode
(
 	A_UINT32 devNum,
 	A_CHAR   *fieldName,
 	A_UINT32 mode,
	A_UINT32 turbo

);

A_UINT32 m_readField
(
 	A_UINT32 devNum,
 	A_CHAR   *fieldName,
 	A_UINT32 printValue,
	A_UINT32 *unsignedValue,
	A_INT32 *signedValue,
	A_BOOL  *signedFlag

);

void m_writeField
(
 	A_UINT32 devNum,
 	A_CHAR *fieldName, 
 	A_UINT32 newValue
);

void m_forceSinglePCDACTable
(
	A_UINT32 devNum,
	A_UINT16 pcdac
);

void m_forcePCDACTable
(
	A_UINT32 devNum,
	A_UINT16 *pcdac
);

void m_forcePowerTxMax
(
	A_UINT32 devNum,
	A_UINT32 length,
	A_UINT16 *pRatesPower
);

void m_forceSinglePowerTxMax
(
	A_UINT32 devNum,
	A_UINT16 txPower
);

void m_setQueue
(
 	A_UINT32 devNum,
	A_UINT32 qcuNumber
);

void m_mapQueue
(
 	A_UINT32 devNum,
	A_UINT32 qcuNumber,
	A_UINT32 dcuNumber
);

void m_clearKeyCache
(
 	A_UINT32 devNum
);

A_BOOL m_readEepromFile
(
    A_CHAR *fileName);
);

void m_devlibCleanup
(
	void
);

void m_specifySubSystemID
(
 	A_UINT32 devNum, 
 	A_UINT16 subsystemID 
);


void m_changeMultipleFieldsAllModes
(
 	A_UINT32		devNum,
 	PARSE_MODE_INFO *fieldsToChange, 
 	A_UINT32 numFields
);

void m_changeMultipleFields
(
 	A_UINT32		devNum,
 	PARSE_FIELD_INFO *fieldsToChange, 
 	A_UINT32 numFields
);

void m_txContFrameBegin
(
 	A_UINT32 devNum, 
	A_UINT32 length,
	A_UINT32 ifswait,
 	A_UINT32 typeOption1,
 	A_UINT32 typeOption2, 
 	A_UINT32 antenna,
	A_BOOL   performStabilizePower,
	A_UINT32 numDescriptors,
	A_UCHAR  *dest
);

void m_rxDataStart
(
	A_UINT32 devNum
);

void m_txDataStart
(
	A_UINT32 devNum
);

void m_changeCal
(
  A_UINT32 calFlags
);

void m_getMacAddr
(
 	A_UINT32 devNum,
	A_UINT16 wmac,
	A_UINT16 instNo,
	A_UINT8 *macAddr
);

void changePciWritesFlag
(
     A_UINT32 devNum,
         A_UINT32 flag
);

void createDescriptors(A_UINT32 devNumIndex, A_UINT32 descBaseAddress,  A_UINT32 descInfo, A_UINT32 bufAddrIncrement, A_UINT32 descOp, A_UINT32 *descWords);

#endif 

