/*******************************************************************************
*Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
*
*Redistribution and use in source and binary forms, with or without modification, are permitted provided 
*that the following conditions are met: 
*1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
*following disclaimer. 
*2. Redistributions in binary form must reproduce the above copyright notice, 
*this list of conditions and the following disclaimer in the documentation and/or other materials provided
*with the distribution. 
*
*THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED 
*WARRANTIES,INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
*FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
*NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR 
*BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
*LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
*SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
*
* $FreeBSD$
*
********************************************************************************/
/********************************************************************************
**    
*   sm.h 
*
*   Abstract:   This module defines the contants, enum and #define definition used
*               by SAT Moduled (SM).
*     
********************************************************************************/

#ifndef SM_H
#define SM_H

/*************************************************
 *   constants for type field in smMem_t
 *************************************************/
#define SM_CACHED_MEM                             0x00     /**< CACHED memory type */
#define SM_DMA_MEM                                0x01     /**< DMA memory type */
#define SM_CACHED_DMA_MEM                         0x02     /**< CACHED DMA memory type */

/*************************************************
 *   constants for API return values
 *************************************************/
typedef enum
{
  SM_RC_SUCCESS,
  SM_RC_FAILURE,
  SM_RC_BUSY,
  SM_RC_NODEVICE,
  SM_RC_VERSION_INCOMPATIBLE,
  SM_RC_VERSION_UNTESTED,
  SM_RC_RSV1,
  SM_RC_RSV2,
  SM_RC_RSV3,
  SM_RC_RSV4,
  SM_RC_DEVICE_BUSY, /* must be the same as tiDeviceBusy */
  
} smStatus_t;

typedef enum
{
  smIOSuccess,
  smIOOverRun,
  smIOUnderRun,
  smIOFailed,
  smIODifError,
  smIOEncryptError,
  smIORetry,           /* open retry timeout */
  smIOSTPResourceBusy, /* stp resource busy */
} smIOStatus_t;

typedef enum
{
  smDetailBusy,
  smDetailNotValid,
  smDetailNoLogin,
  smDetailAbortLogin,
  smDetailAbortReset,
  smDetailAborted,
  smDetailDifMismatch,
  smDetailDifAppTagMismatch,
  smDetailDifRefTagMismatch,
  smDetailDifCrcMismatch,
  smDetailDekKeyCacheMiss,
  smDetailCipherModeInvalid,
  smDetailDekIVMismatch,
  smDetailDekRamInterfaceError,
  smDetailDekIndexOutofBounds,
  smDetailOtherError
} smIOStatusDetail_t;

/*
 * Data direction for I/O request
 */
typedef enum
{
  smDirectionIn   = 0x0000,
  smDirectionOut  = 0x0001
}smDataDirection_t;

/*
 * Event types for tdsmEventCB()
 * do not change: Needs to be in sync with TISA API
 */
typedef enum
{
  smIntrEventTypeCnxError,
  smIntrEventTypeDiscovery,
  smIntrEventTypeTransportRecovery,
  smIntrEventTypeTaskManagement,
  smIntrEventTypeDeviceChange,
  smIntrEventTypeLogin,
  smIntrEventTypeLocalAbort  
} smIntrEventType_t;

typedef enum
{
  smTMOK,
  smTMFailed
} smTMEventStatus_t;

/*
 * Flags in smSuperScsiInitiatorRequest_t
 */
#define SM_SCSI_INITIATOR_DIF         0x00000001
#define SM_SCSI_INITIATOR_ENCRYPT     0x00000002

/*
 * Flags in smSuperScsiInitiatorRequest_t
 */
#define SM_SCSI_TARGET_DIF         0x00000001
#define SM_SCSI_TARGET_MIRROR      0x00000002
#define SM_SCSI_TARGET_ENCRYPT     0x00000004

typedef struct {
                void		*tdData;
                void		*smData;
} smContext_t;


typedef    smContext_t    smDeviceHandle_t;

typedef    smContext_t    smIORequest_t;

typedef    smContext_t    smRoot_t;

typedef struct 
{
	bit8  lun[8];               /* logical unit number  */
} smLUN_t;

typedef struct{
		smLUN_t         lun;
		bit32           expDataLength;
		bit32           taskAttribute;
		bit32           crn;
		bit8            cdb[16];
} smIniScsiCmnd_t;



typedef struct{
               void 	*virtPtr;
               void 	*osHandle;
               bit32 	physAddrUpper;
               bit32 	physAddrLower;
               bit32 	totalLength;
               bit32 	numElements;
               bit32 	singleElementLength;
               bit32 	alignment;
               bit32 	type;
               bit32 	reserved;
} smMem_t;

#define SM_NUM_MEM_CHUNKS 8

typedef struct{
               bit32            count;
               smMem_t          smMemory[SM_NUM_MEM_CHUNKS];
} smMemoryRequirement_t;

typedef struct{
	       bit32 	lower;
	       bit32 	upper;
	       bit32 	len;
	       bit32 	type;
} smSgl_t;

/*
 * DIF operation
 */
#define DIF_INSERT                     0
#define DIF_VERIFY_FORWARD             1
#define DIF_VERIFY_DELETE              2
#define DIF_VERIFY_REPLACE             3
#define DIF_VERIFY_UDT_REPLACE_CRC     5
#define DIF_REPLACE_UDT_REPLACE_CRC    7

#define DIF_UDT_SIZE              6

typedef struct smDif
{
  agBOOLEAN   enableDIFPerLA;
  bit32       flag;
  bit16       initialIOSeed;
  bit16       reserved;
  bit32       DIFPerLAAddrLo;
  bit32       DIFPerLAAddrHi;
  bit16       DIFPerLARegion0SecCount;
  bit16       DIFPerLANumOfRegions;
  bit8        udtArray[DIF_UDT_SIZE];
  bit8        udrtArray[DIF_UDT_SIZE];  
} smDif_t;

typedef struct smEncryptDek {
    bit32          dekTable;
    bit32          dekIndex;
} smEncryptDek_t;

typedef struct smEncrypt {
  smEncryptDek_t     dekInfo;	
  bit32          kekIndex;
  agBOOLEAN      keyTagCheck;
  agBOOLEAN  	   enableEncryptionPerLA;    
  bit32          sectorSizeIndex;
  bit32          encryptMode;
  bit32          keyTag_W0;
  bit32          keyTag_W1;
  bit32          tweakVal_W0;
  bit32          tweakVal_W1;
  bit32          tweakVal_W2;
  bit32          tweakVal_W3;
  bit32          EncryptionPerLAAddrLo;
  bit32          EncryptionPerLAAddrHi;
  bit16          EncryptionPerLRegion0SecCount;
  bit16          reserved;
} smEncrypt_t;

typedef struct smScsiInitiatorRequest {
	      void                     *sglVirtualAddr;
	      smIniScsiCmnd_t          scsiCmnd;
	      smSgl_t                  smSgl1;
	      smDataDirection_t        dataDirection;
} smScsiInitiatorRequest_t;

typedef struct smSuperScsiInitiatorRequest
{
  void                *sglVirtualAddr;
  smIniScsiCmnd_t     scsiCmnd;     
  smSgl_t             smSgl1;
  smDataDirection_t   dataDirection;
  bit32               flags; /* 
                               bit 0-1: reserved
                               bit 2: enable encryption
                               bit 3: enable dif
                               bit 4-7: reserved
                               bit 8-23: DIF SKIP Bytes
                               bit 24-31: Reserved
                             */
  smDif_t             Dif;
  smEncrypt_t         Encrypt;
} smSuperScsiInitiatorRequest_t;

typedef struct{
	       void     *senseData;
	       bit8 	senseLen;
} smSenseData_t;

typedef struct{
               bit32			maxActiveIOs;
               bit32			numDevHandles;
#ifdef SM_DEBUG
               bit32			SMDebugLevel;
#endif
} smSwConfig_t;


#define smBOOLEAN  bit32










#endif  /* SM_H */

