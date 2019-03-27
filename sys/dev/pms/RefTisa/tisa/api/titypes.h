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
** Version Control Information:
**
**
*******************************************************************************/
/********************************************************************************
**
**   titypes.h
**
**   Abstract:   This module contains data structure definition used
**               by the Transport Independent API (TIAPI) Layer.
**
********************************************************************************/

#include <dev/pms/RefTisa/tisa/api/tidefs.h>

#ifndef TITYPES_H
#define TITYPES_H

/*****************************************************************************
 * SHARED TYPES
 *****************************************************************************/

typedef struct tiPortalContext
{
  void    *osData;
  void    *tdData;
} tiPortalContext_t;

typedef struct tiDeviceHandle
{
  void    *osData;
  void    *tdData;
} tiDeviceHandle_t;

typedef struct tiRoot
{
  void    *osData;
  void    *tdData;
} tiRoot_t;

typedef struct tiMem
{
  void    *virtPtr;
  void    *osHandle;
  bit32   physAddrUpper;
  bit32   physAddrLower;
  bit32   totalLength;
  bit32   numElements;
  bit32   singleElementLength;
  bit32   alignment;
  bit32   type;
  bit32   reserved;
} tiMem_t;

typedef struct tiLoLevelMem
{
  bit32       count;
  tiMem_t     mem[MAX_LL_LAYER_MEM_DESCRIPTORS];
} tiLoLevelMem_t;

typedef struct tiLoLevelOption
{
  bit32       usecsPerTick;
  bit32       numOfQueuesPerPort;
  bit32       mutexLockUsage;
  bit32       pciFunctionNumber;
  bit32       maxPortContext;
  bit32       maxNumOSLocks;
  agBOOLEAN   encryption;
  bit32       maxInterruptVectors;
  bit32       flag;
  bit32       max_MSI_InterruptVectors;
#ifdef SA_ENABLE_PCI_TRIGGER
  bit32       PCI_trigger; 
#endif /* SA_ENABLE_PCI_TRIGGER */

} tiLoLevelOption_t;

typedef struct tiLoLevelResource
{
  tiLoLevelOption_t   loLevelOption;
  tiLoLevelMem_t      loLevelMem;
} tiLoLevelResource_t;

typedef struct tiTdSharedMem
{
  tiMem_t     tdSharedCachedMem1;
} tiTdSharedMem_t;

typedef struct tiIORequest
{
  void    *osData;
  void    *tdData;
} tiIORequest_t;

typedef struct tiSgl_s
{
  bit32   lower;
  bit32   upper;
  bit32   len;
  bit32   type;
} tiSgl_t;

typedef struct tiSenseData
{
  void    *senseData;
  bit8    senseLen;
} tiSenseData_t;

typedef struct tiIOCTLPayload
{
  bit32       Signature;
  bit16       MajorFunction;
  bit16       MinorFunction;
  bit16       Length;
  bit16       Status;
  bit32       Reserved; /* required for 64 bit alignment */
  bit8        FunctionSpecificArea[1];
}tiIOCTLPayload_t;


typedef struct tiIOCTLPayload_wwn
{
  bit32       Signature;
  bit16       MajorFunction;
  bit16       MinorFunction;
  bit16       Length;
  bit16       Status;
  bit32       Reserved; /* required for 64 bit alignment */
  bit8        FunctionSpecificArea[8];
}tiIOCTLPayload_wwn_t;

typedef struct tiPortInfo
{
  char  *name;
  char  *address;
  char  *localName;
  char  *remoteName;
  bit32 localNameLen;
  bit32 remoteNameLen;
} tiPortInfo_t;

typedef struct tiDif_s
{
  agBOOLEAN   enableDIFPerLA;
  bit32       flags;
  bit16       initialIOSeed;
  bit16       reserved;
  bit32       DIFPerLAAddrLo;
  bit32       DIFPerLAAddrHi;
  bit16       DIFPerLARegion0SecCount;
  bit16       DIFPerLANumOfRegions;
  bit8        udtArray[DIF_UDT_SIZE];
  bit8        udtrArray[DIF_UDT_SIZE];
} tiDif_t;

#define DIF_INSERT                  0
#define DIF_VERIFY_FORWARD          1
#define DIF_VERIFY_DELETE           2
#define DIF_VERIFY_REPLACE          3
#define DIF_VERIFY_UDT_REPLACE_CRC  5
#define DIF_REPLACE_UDT_REPLACE_CRC 7

#define DIF_BLOCK_SIZE_512          0x00
#define DIF_BLOCK_SIZE_520          0x01
#define DIF_BLOCK_SIZE_4096         0x02
#define DIF_BLOCK_SIZE_4160         0x03

#define DIF_ACTION_FLAG_MASK        0x00000007 /* 0 - 2 */
#define DIF_CRC_VERIFICATION        0x00000008 /* 3 */
#define DIF_CRC_INVERSION           0x00000010 /* 4 */
#define DIF_CRC_IO_SEED             0x00000020 /* 5 */
#define DIF_UDT_REF_BLOCK_COUNT     0x00000040 /* 6 */
#define DIF_UDT_APP_BLOCK_COUNT     0x00000080 /* 7 */
#define DIF_UDTR_REF_BLOCK_COUNT    0x00000100 /* 8 */
#define DIF_UDTR_APP_BLOCK_COUNT    0x00000200 /* 9 */
#define DIF_CUST_APP_TAG            0x00000C00 /* 10 - 11 */
#define DIF_FLAG_RESERVED           0x0000F000 /* 12 - 15 */
#define DIF_DATA_BLOCK_SIZE_MASK    0x000F0000 /* 16 - 19 */
#define DIF_DATA_BLOCK_SIZE_SHIFT   16
#define DIF_TAG_VERIFY_MASK         0x03F00000 /* 20 - 25 */
#define DIF_TAG_UPDATE_MASK         0xFC000000 /* 26 - 31 */


#define NORMAL_BLOCK_SIZE_512       512
#define NORMAL_BLOCK_SIZE_4K        4096

#define DIF_PHY_BLOCK_SIZE_512      512
#define DIF_PHY_BLOCK_SIZE_520      520
#define DIF_PHY_BLOCK_SIZE_4096     4096
#define DIF_PHY_BLOCK_SIZE_4160     4160

#define DIF_LOGIC_BLOCK_SIZE_520    520
#define DIF_LOGIC_BLOCK_SIZE_528    528
#define DIF_LOGIC_BLOCK_SIZE_4104   4104
#define DIF_LOGIC_BLOCK_SIZE_4168   4168




typedef struct tiDetailedDeviceInfo
{
  bit8    devType_S_Rate;
    /* Bit 6-7: reserved
       Bit 4-5: Two bits flag to specify a SAS or SATA (STP) device:
                00: SATA or STP device
                01: SSP or SMP device
                10: Direct SATA device
       Bit 0-3: Connection Rate field when opening the device.
                Code Description:
        00h:  Device has not been registered
                08h:  1,5 Gbps
                09h:  3,0 Gbps
                0ah:  6.0 Gbps
                All others Reserved
    */
  bit8    reserved1;
  bit16   reserved2;
} tiDetailedDeviceInfo_t;

typedef struct tiDeviceInfo
{
  char                   *localName;
  char                   *localAddress;
  char                   *remoteName;
  char                   *remoteAddress;
  bit16                  osAddress1;
  bit16                  osAddress2;
  bit32                  loginState;
  tiDetailedDeviceInfo_t info;
} tiDeviceInfo_t;


#define KEK_BLOB_SIZE           48
#define KEK_AUTH_SIZE           40
#define KEK_MAX_TABLE_ENTRIES   8

#define DEK_MAX_TABLES          2
#define DEK_MAX_TABLE_ENTRIES   (1024*4)

#define DEK_BLOB_SIZE_07        72
#define DEK_BLOB_SIZE_08        80

#define OPERATOR_ROLE_ID_SIZE   1024

#define HMAC_SECRET_KEY_SIZE    72

typedef struct tiEncryptKekBlob
{
  bit8    kekBlob[KEK_BLOB_SIZE];
} tiEncryptKekBlob_t;

typedef struct tiEncryptDekBlob
{
  bit8    dekBlob[DEK_BLOB_SIZE_08];
} tiEncryptDekBlob_t;

typedef struct DEK_Table_s {
  tiEncryptDekBlob_t  Dek[DEK_MAX_TABLE_ENTRIES];
}tiDEK_Table_t;

typedef struct DEK_Tables_s {
  tiDEK_Table_t  DekTable[DEK_MAX_TABLES];
} tiDEK_Tables_t;

/*sTSDK  4.38  */
#define OPR_MGMT_ID_STRING_SIZE 31

typedef struct tiID_s {
   bit8   ID[OPR_MGMT_ID_STRING_SIZE];
} tiID_t;

typedef struct tiEncryptInfo
{
  bit32   securityCipherMode;
  bit32   status;
  bit32   sectorSize[6];
} tiEncryptInfo_t;

typedef struct tiEncryptPort
{
  bit32   encryptEvent;
  bit32   subEvent;
  void    *pData;
} tiEncryptPort_t;

typedef struct tiEncryptDek
{
  bit32    dekTable;
  bit32    dekIndex;
} tiEncryptDek_t;

typedef struct tiEncrypt
{
    tiEncryptDek_t dekInfo;
    bit32          kekIndex;
    agBOOLEAN      keyTagCheck;
    agBOOLEAN      enableEncryptionPerLA;
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
} tiEncrypt_t;

typedef struct tiHWEventMode_s
{
    bit32          modePageOperation;
    bit32          status;
    bit32          modePageLen;
    void           *modePage;
    void           *context;
} tiHWEventMode_t;

/*****************************************************************************
 * INITIATOR TYPES
 *****************************************************************************/

typedef struct tiInitiatorMem
{
  bit32       count;
  tiMem_t     tdCachedMem[6];
} tiInitiatorMem_t;

typedef struct tiInitiatorOption
{
  bit32       usecsPerTick;
  bit32       pageSize;
  tiMem_t     dynamicDmaMem;
  tiMem_t     dynamicCachedMem;
  bit32       ioRequestBodySize;
} tiInitiatorOption_t;


typedef struct tiInitiatorResource
{
  tiInitiatorOption_t     initiatorOption;
  tiInitiatorMem_t        initiatorMem;
} tiInitiatorResource_t;

typedef struct tiLUN
{
  bit8    lun[8];
} tiLUN_t;

typedef struct tiIniScsiCmnd
{
  tiLUN_t     lun;
  bit32       expDataLength;
  bit32       taskAttribute;
  bit32       crn;
  bit8        cdb[16];
} tiIniScsiCmnd_t;

typedef struct tiScsiInitiatorRequest
{
  void                *sglVirtualAddr;
  tiIniScsiCmnd_t     scsiCmnd;
  tiSgl_t             agSgl1;
  tiDataDirection_t   dataDirection;
} tiScsiInitiatorRequest_t;

/* This is the standard request body for I/O that requires DIF or encryption. */
typedef struct tiSuperScsiInitiatorRequest
{
  void                *sglVirtualAddr;
  tiIniScsiCmnd_t     scsiCmnd;
  tiSgl_t             agSgl1;
  tiDataDirection_t   dataDirection;
  bit32               flags;
#ifdef CCBUILD_INDIRECT_CDB
  bit32               IndCDBLowAddr;       /* The low physical address of indirect CDB buffer in host memory */
  bit32               IndCDBHighAddr;      /* The high physical address of indirect CDB buffer in host memory */
  bit32               IndCDBLength;        /* Indirect CDB length */
  void                *IndCDBBuffer;       /* Indirect SSPIU buffer */
#endif
  tiDif_t             Dif;
  tiEncrypt_t         Encrypt;
} tiSuperScsiInitiatorRequest_t;

typedef struct tiSMPFrame
{
  void        *outFrameBuf;
  bit32       outFrameAddrUpper32;
  bit32       outFrameAddrLower32;
  bit32       outFrameLen;
  bit32       inFrameAddrUpper32;
  bit32       inFrameAddrLower32;
  bit32       inFrameLen;
  bit32       expectedRespLen;
  bit32       flag;
} tiSMPFrame_t;
typedef struct tiEVTData
{
  bit32   SequenceNo;
  bit32   TimeStamp;
  bit32   Source;
  bit32   Code;
  bit8    Reserved;
  bit8    BinaryDataLength;
  bit8    DataAndMessage[EVENTLOG_MAX_MSG_LEN];
} tiEVTData_t;

typedef bit32 (*IsrHandler_t)(
                        tiRoot_t    *tiRoot,
                        bit32       channelNum
                        );
typedef void (*DeferedHandler_t)(
                        tiRoot_t    *tiRoot,
                        bit32       channelNum,
                        bit32       count,
                        bit32       context
                        );

/*****************************************************************************
 * TARGET TYPES
 *****************************************************************************/

typedef struct tiTargetMem {
  bit32     count;
  tiMem_t   tdMem[10];
} tiTargetMem_t;

typedef struct tiTargetOption {
  bit32     usecsPerTick;
  bit32     pageSize;
  bit32     numLgns;
  bit32     numSessions;
  bit32     numXchgs;
  tiMem_t   dynamicDmaMem;
  tiMem_t   dynamicCachedMem;
} tiTargetOption_t;

typedef struct
{
  tiTargetOption_t     targetOption;
  tiTargetMem_t        targetMem;
} tiTargetResource_t;

typedef struct
{
  bit8      *reqCDB;
  bit8      *scsiLun;
  bit32     taskAttribute;
  bit32     taskId;
  bit32     crn;
} tiTargetScsiCmnd_t;

typedef struct tiSuperScsiTargetRequest
{
  bit32               flags;
  tiDif_t             Dif;
  tiEncrypt_t         Encrypt;
  tiSgl_t             agSgl;
  void                *sglVirtualAddr;
  tiSgl_t             agSglMirror;
  void                *sglVirtualAddrMirror;
  bit32               Offset;
  bit32               DataLength;
} tiSuperScsiTargetRequest_t;

/* SPCv controller mode page definitions */
typedef struct tiEncryptGeneralPage_s {
  bit32             pageCode;           /* 0x20 */
  bit32             numberOfDeks;
} tiEncryptGeneralPage_t;

#define TD_ENC_CONFIG_PAGE_KEK_NUMBER 0x0000FF00
#define TD_ENC_CONFIG_PAGE_KEK_SHIFT  8

typedef struct tiEncryptDekConfigPage
{
  bit32 pageCode;                      /* 0x21 */
  bit32 table0AddrLo;
  bit32 table0AddrHi;
  bit32 table0Entries;
  bit32 table0Config;
  bit32 table1AddrLo;
  bit32 table1AddrHi;
  bit32 table1Entries;
  bit32 table1Config;
} tiEncryptDekConfigPage_t;

#define TD_ENC_DEK_CONFIG_PAGE_DEK_TABLE_NUMBER 0xF0000000
#define TD_ENC_DEK_CONFIG_PAGE_DEK_CACHE_WAYS   0x0F000000
#define TD_ENC_DEK_CONFIG_PAGE_DPR              0x00000200
#define TD_ENC_DEK_CONFIG_PAGE_DER              0x00000100
#define TD_ENC_DEK_CONFIG_PAGE_DEK_CACHE_SHIFT  24
#define TD_ENC_DEK_CONFIG_PAGE_DEK_TABLE_SHIFT  28
#define TD_ENC_DEK_CONFIG_PAGE_DEK_HDP_SHIFT    8


/* CCS (Current Crypto Services)  and NOPR (Number of Operators) are valid only in GET_CONTROLLER_CONFIG */
/* NAR, CORCAP and USRCAP are valid only when AUT==1 */
typedef struct tiEncryptControlParamPage_s {
  bit32          PageCode;           /* 0x22 */
  bit32          CORCAP;             /* Crypto Officer Role Capabilities */
  bit32          USRCAP;             /* User Role Capabilities */
  bit32          CCS;                /* Current Crypto Services */
  bit32          NOPR;               /* Number of Operators */
} tiEncryptControlParamPage_t;

typedef struct tiEncryptHMACConfigPage_s
{
  bit32  PageCode;
  bit32  CustomerTag;
  bit32  KeyAddrLo;
  bit32  KeyAddrHi;
} tiEncryptHMACConfigPage_t;

typedef struct tiInterruptConfigPage_s {
   bit32  pageCode;                        /* 0x05 */
   bit32  vectorMask;
   bit32  reserved;
   bit32  ICTC0;
   bit32  ICTC1;
   bit32  ICTC2;
   bit32  ICTC3;
   bit32  ICTC4;
   bit32  ICTC5;
   bit32  ICTC6;
   bit32  ICTC7;
} tiInterruptConfigPage_t;

/* brief data structure for SAS protocol timer configuration page. */
typedef struct  tiSASProtocolTimerConfigurationPage_s{
  bit32 pageCode;                       /* 0x04 */
  bit32 MST_MSI;
  bit32 STP_SSP_MCT_TMO;
  bit32 STP_FRM_TMO;
  bit32 STP_IDLE_TMO;
  bit32 OPNRJT_RTRY_INTVL;
  bit32 Data_Cmd_OPNRJT_RTRY_TMO;
  bit32 Data_Cmd_OPNRJT_RTRY_THR;
} tiSASProtocolTimerConfigurationPage_t;

/*sTSDK 4.19   */

/* The command is for an operator to login to/logout from SPCve. */
/* Only when all IOs are quiesced, can an operator logout. */
typedef struct tiOperatorCommandSet_s {
  bit32 OPRIDX_PIN_ACS;    /* Access type (ACS) [4 bits] */
                          /* KEYopr pinned in the KEK RAM (PIN) [1 bit] */
                          /* KEYopr Index in the KEK RAM (OPRIDX) [8 bits] */
  bit8   cert[40];          /* Operator Certificate (CERT) [40 bytes] */
  bit32 reserved[3];       /* reserved */
} tiOperatorCommandSet_t;

#define FIPS_SELFTEST_MAX_MSG_LEN       (128*1024)
#define FIPS_SELFTEST_MAX_DIGEST_SIZE   64

typedef struct tiEncryptSelfTestDescriptor_s {
  bit32         AESNTC_AESPTC;       /* AES Negative/Positive Test Case Bit Map */
  bit32         KWPNTC_PKWPPTC;      /* Key Wrap Negative/Positive Test Case Bit Map */
  bit32         HMACNTC_HMACPTC;     /* HMAC Negative Test Case Bit Map */
} tiEncryptSelfTestDescriptor_t;

typedef struct  tiEncryptSelfTestResult_s{
  bit32         AESNTCS_AESPTCS;       /* AES Negative/Positive Test Case Status */
  bit32         KWPNTCS_PKWPPTCS;      /* Key Wrap Negative/Positive Test Case Status */
  bit32         HMACNTCS_HMACPTCS;     /* HMAC Negative Test Case Status */
} tiEncryptSelfTestResult_t;

/*
   Tell SPCve controller the underlying SHA algorithm, where to fetch the message,
   the size of the message, where to store the digest, where to fetch the secret key and the size of the key.
*/
typedef struct tiEncryptHMACTestDescriptor_s
{
   bit32    Tlen_SHAAlgo;
   bit32    MsgAddrLo;
   bit32    MsgAddrHi;
   bit32    MsgLen;
   bit32    DigestAddrLo;
   bit32    DigestAddrHi;
   bit32    KeyAddrLo;
   bit32    KeyAddrHi;
   bit32    KeyLen;
} tiEncryptHMACTestDescriptor_t;

typedef struct tiEncryptHMACTestResult_s
{
  bit32  Tlen_SHAAlgo;
   bit32    Reserved[12];
} tiEncryptHMACTestResult_t;

typedef struct tiEncryptSHATestDescriptor_s
{
   bit32    Dword0;
   bit32    MsgAddrLo;
   bit32    MsgAddrHi;
   bit32    MsgLen;
   bit32    DigestAddrLo;
   bit32    DigestAddrHi;
} tiEncryptSHATestDescriptor_t;

typedef struct tiEncryptSHATestResult_s
{
   bit32    Dword0;
   bit32    Dword[12];
} tiEncryptSHATestResult_t;


#endif  /* TITYPES_H */
