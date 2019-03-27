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
*   tidefs.h 
*
*   Abstract:   This module contains enum and #define definition used
*               by Transport Independent API (TIAPI) Layer.
*     
********************************************************************************/

#ifndef TIDEFS_H

#define TIDEFS_H

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

/*****************************************************************************
 *  INITIATOR/TARGET SHARED DEFINES AND ENUMS
 *****************************************************************************/

/*
 *  Option mask parameter for tiCOMPortStart() 
 */
#define PORTAL_ADD_MASK   0x00000001

/* 
 * Maximum memory descriptor for Low-Level layer.
 */
#define MAX_LL_LAYER_MEM_DESCRIPTORS  64


/* 
 * TI API function return types 
 */
typedef enum
{
  tiSuccess,
  tiError,
  tiBusy,
  tiIONoDevice,
  tiMemoryTooLarge,
  tiMemoryNotAvail,
  tiInvalidHandle,
  tiNotSupported,
  tiReject,
  tiIncorrectLun,
  tiDeviceBusy,
} tiStatus_t;

/*
 * Type of memory, OR-ed the bit fields.
 */

/* Bit 0-1, cached or dma-uncached dma-cached */

#define TI_DMA_MEM        0x00000000      /* uncached DMA capable memory   */
#define TI_CACHED_MEM     0x00000001      /* cached non-DMA capable memory */
#define TI_CACHED_DMA_MEM 0x00000002      /* cached DMA capable memory */
#define TI_DMA_MEM_CHIP   0x00000003      /* Internal HW/chip memory  */

/* Bit2-3: location of memory */
#define TI_LOC_HOST     0x00000000      /* default, allocated from host */
#define TI_LOC_ON_CHIP  0x00000004      /* memory is from on-chip RAM   */
#define TI_LOC_ON_CARD  0x00000008      /* memory is from on-card RAM   */

/* Type of SGL list
 *
 */
typedef enum
{
  tiSgl=0,
  tiSglList=0x80000000,
  tiExtHdr
}tiSglType_t;

/* 
 * Type of mutex semaphoring/synchronization
 */
typedef enum
{
  tiSingleMutexLockPerPort,
  tiOneMutexLockPerQueue
}tiMutexType_t;

/* 
 * Context (interrupt or non-interrupt)
 */
typedef enum
{
  tiInterruptContext,
  tiNonInterruptContext
}tiIntContextType_t;

/*
 * Port Event type.
 */
typedef enum
{
  tiPortPanic,
  tiPortResetComplete,
  tiPortNameServerDown,
  tiPortLinkDown,
  tiPortLinkUp,
  tiPortStarted,
  tiPortStopped,
  tiPortShutdown,
  tiPortDiscoveryReady,
  tiPortResetNeeded,
  tiEncryptOperation,
  tiModePageOperation
} tiPortEvent_t;

/*
 * tiEncryptOperation Event types
 */
typedef enum
{
  tiEncryptGetInfo,
  tiEncryptSetMode,
  tiEncryptKekAdd,
  tiEncryptDekInvalidate,
  tiEncryptKekStore,
  tiEncryptKekLoad,
  tiEncryptAttribRegUpdate,
  tiEncryptDekAdd,
  /* new */
  tiEncryptOperatorManagement,
  tiEncryptSelfTest,
  tiEncryptSetOperator,
  tiEncryptGetOperator
} tiEncryptOp_t;

/* 
 * ostiPortEvent() status values for tiCOMOperatorManagement()
 */
typedef enum
{
  tiOMNotSupported,
  tiOMIllegalParam,
  tiOMKENUnwrapFail,
  tiOMNvramOpFailure,
} tiOperatorManagementStatus_t;

/* 
 * ostiInitiatorIOCompleted() and ostiTargetIOError() status values 
 */
typedef enum
{
  tiIOSuccess,
  tiIOOverRun,
  tiIOUnderRun,
  tiIOFailed,
  tiIODifError,
  tiIOEncryptError,
} tiIOStatus_t;

/* 
 * ostiInitiatorIOCompleted() and ostiTargetIOError() statusDetail values 
 */
typedef enum
{
  tiSMPSuccess,
  tiSMPAborted,
  tiSMPFailed,
} tiSMPStatus_t;

typedef enum
{
  tiDetailBusy,
  tiDetailNotValid,
  tiDetailNoLogin,
  tiDetailAbortLogin,
  tiDetailAbortReset,
  tiDetailAborted,
  tiDetailDifMismatch,
  tiDetailDifAppTagMismatch,
  tiDetailDifRefTagMismatch,
  tiDetailDifCrcMismatch,
  tiDetailDekKeyCacheMiss,
  tiDetailCipherModeInvalid,
  tiDetailDekIVMismatch,
  tiDetailDekRamInterfaceError,
  tiDetailDekIndexOutofBounds,
  tiDetailOtherError,
  tiDetailOtherErrorNoRetry,
} tiIOStatusDetail_t;

/* 
 * IOCTL Status Codes
 */
#define IOCTL_ERR_STATUS_OK                  0x00
#define IOCTL_ERR_STATUS_MORE_DATA           0x01
#define IOCTL_ERR_STATUS_NO_MORE_DATA        0x02
#define IOCTL_ERR_STATUS_INVALID_CODE        0x03
#define IOCTL_ERR_STATUS_INVALID_DEVICE      0x04
#define IOCTL_ERR_STATUS_NOT_RESPONDING      0x05
#define IOCTL_ERR_STATUS_INTERNAL_ERROR      0x06
#define IOCTL_ERR_STATUS_NOT_SUPPORTED       0x07
#define IOCTL_ERR_FW_EVENTLOG_DISABLED       0x08
#define IOCTL_MJ_FATAL_ERROR_SOFT_RESET_TRIG 0x72
#define IOCTL_MJ_FATAL_ERR_CHK_SEND_TRUE     0x77
#define IOCTL_MJ_FATAL_ERR_CHK_SEND_FALSE    0x76
#define IOCTL_ERROR_NO_FATAL_ERROR           0x77

#define ADAPTER_WWN_START_OFFSET	     0x804
#define ADAPTER_WWN_END_OFFSET		     0x80b
#define ADAPTER_WWN_SPC_START_OFFSET	     0x704
#define ADAPTER_WWN_SPC_END_OFFSET	     0x70b

/*
 * IOCTL Return Codes 
 */
#define IOCTL_CALL_SUCCESS                  0x00
#define IOCTL_CALL_FAIL                     0x01
#define IOCTL_CALL_PENDING                  0x02
#define IOCTL_CALL_INVALID_CODE             0x03
#define IOCTL_CALL_INVALID_DEVICE           0x04
#define IOCTL_CALL_TIMEOUT                  0x08

/*
 * DIF operation
 */
#define DIF_INSERT                0
#define DIF_VERIFY_FORWARD        1
#define DIF_VERIFY_DELETE         2
#define DIF_VERIFY_REPLACE        3

#define DIF_UDT_SIZE              6

/*
 * Login state in tiDeviceInfo_t
 */
#define INI_LGN_STATE_FREE            0x00000000
#define INI_LGN_STATE_LOGIN           0x00000001
#define INI_LGN_STATE_FAIL            0x00000002
#define INI_LGN_STATE_OTHERS          0x0000000F

/*
 * SecurityCipherMode in tiEncryptInfo_t and tiCOMEncryptSetMode()
 */
#define TI_ENCRYPT_SEC_MODE_FACT_INIT 0x00000000
#define TI_ENCRYPT_SEC_MODE_A         0x40000000
#define TI_ENCRYPT_SEC_MODE_B         0x80000000
#define TI_ENCRYPT_ATTRIB_ALLOW_SMF   0x00000200
#define TI_ENCRYPT_ATTRIB_AUTH_REQ    0x00000100
#define TI_ENCRYPT_ATTRIB_CIPHER_XTS  0x00000002
#define TI_ENCRYPT_ATTRIB_CIPHER_ECB  0x00000001

/*
 * Status in tiEncryptInfo_t 
 */
#define TI_ENCRYPT_STATUS_NO_NVRAM        0x00000001
#define TI_ENCRYPT_STATUS_NVRAM_ERROR     0x00000002
#define TI_ENCRYPT_STATUS_ENGINE_ERROR    0x00000004

/*
 * EncryptMode in tiEncrypt_t
 */
#define TI_ENCRYPT_MODE_XTS_AES       0x00400000
#define TI_ENCRYPT_MODE_ECB_AES       0x00000000

/*
 * Encrypt blob types
 */
#define TI_PLAINTEXT          0
#define TI_ENCRYPTED_KEK_PMCA 1
#define TI_ENCRYPTED_KEK_PMCB 2

/*
 * Encrypt DEK table key entry sizes
 */
#define TI_DEK_TABLE_KEY_SIZE16 0
#define TI_DEK_TABLE_KEY_SIZE24 1
#define TI_DEK_TABLE_KEY_SIZE32 2
#define TI_DEK_TABLE_KEY_SIZE40 3
#define TI_DEK_TABLE_KEY_SIZE48 4
#define TI_DEK_TABLE_KEY_SIZE56 5
#define TI_DEK_TABLE_KEY_SIZE64 6
#define TI_DEK_TABLE_KEY_SIZE72 7
#define TI_DEK_TABLE_KEY_SIZE80 8

/* KEK blob size and DEK blob size and host DEK table entry number */
#define TI_KEK_BLOB_SIZE           48
#define TI_KEK_MAX_TABLE_ENTRIES   8

#define TI_DEK_MAX_TABLES          2
#define TI_DEK_MAX_TABLE_ENTRIES   (1024*4)

#define TI_DEK_BLOB_SIZE           80


/************************************************************
*  tiHWEventMode_t page operation definitions
************************************************************/
#define tiModePageGet                                    1
#define tiModePageSet                                    2

/* controller configuration page code */
#define TI_SAS_PROTOCOL_TIMER_CONFIG_PAGE     0x04
#define TI_INTERRUPT_CONFIGURATION_PAGE       0x05
#define TI_ENCRYPTION_GENERAL_CONFIG_PAGE     0x20
#define TI_ENCRYPTION_DEK_CONFIG_PAGE         0x21
#define TI_ENCRYPTION_CONTROL_PARM_PAGE       0x22
#define TI_ENCRYPTION_HMAC_CONFIG_PAGE        0x23


/* encryption self test type */
#define TI_ENCRYPTION_TEST_TYPE_BIST          0x01
#define TI_ENCRYPTION_TEST_TYPE_HMAC          0x02

/* SHA algorithm type */
#define TI_SHA_ALG_1                          0x04
#define TI_SHA_ALG_256                        0x08
#define TI_SHA_ALG_224                        0x10
#define TI_SHA_ALG_512                        0x20
#define TI_SHA_ALG_384                        0x40

#define TI_SHA_1_DIGEST_SIZE                    20
#define TI_SHA_256_DIGEST_SIZE                  32
#define TI_SHA_224_DIGEST_SIZE                  28
#define TI_SHA_512_DIGEST_SIZE                  64
#define TI_SHA_384_DIGEST_SIZE                  48


/*****************************************************************************
 *  INITIATOR SPECIFIC DEFINES AND ENUMS
 *****************************************************************************/

/* 
 * ostiInitiatorIOCompleted() statusDetail contains SCSI status,
 * when status passed in ostiInitiatorIOCompleted() is tiIOSuccess.
 */
#define SCSI_STAT_GOOD              0x00
#define SCSI_STAT_CHECK_CONDITION   0x02
#define SCSI_STAT_CONDITION_MET     0x04
#define SCSI_STAT_BUSY              0x08
#define SCSI_STAT_INTERMEDIATE      0x10
#define SCSI_STAT_INTER_CONDIT_MET  0x14
#define SCSI_STAT_RESV_CONFLICT     0x18
#define SCSI_STAT_COMMANDTERMINATED 0x22
#define SCSI_STAT_TASK_SET_FULL     0x28
#define SCSI_STAT_ACA_ACTIVE        0x30
#define SCSI_STAT_TASK_ABORTED      0x40

/*
01: soft error 
02: not ready 
03: medium error 
04: hardware error 
05: illegal request 
06: unit attention 
0b: abort command 
*/ 
#define SCSI_SENSE_KEY_NO_SENSE         0x00
#define SCSI_SENSE_KEY_RECOVERED_ERROR  0x01
#define SCSI_SENSE_KEY_NOT_READY        0x02
#define SCSI_SENSE_KEY_MEDIUM_ERROR     0x03
#define SCSI_SENSE_KEY_HARDWARE_ERROR   0x04
#define SCSI_SENSE_KEY_ILLEGAL_REQUEST  0x05
#define SCSI_SENSE_KEY_UNIT_ATTENTION   0x06
#define SCSI_SENSE_KEY_DATA_PROTECT     0x07
#define SCSI_SENSE_KEY_BLANK_CHECK      0x08
#define SCSI_SENSE_KEY_UNIQUE           0x09
#define SCSI_SENSE_KEY_COPY_ABORTED     0x0A
#define SCSI_SENSE_KEY_ABORTED_COMMAND  0x0B
#define SCSI_SENSE_KEY_EQUAL            0x0C
#define SCSI_SENSE_KEY_VOL_OVERFLOW     0x0D
#define SCSI_SENSE_KEY_MISCOMPARE       0x0E
#define SCSI_SENSE_KEY_RESERVED         0x0F




/* 
 * Reset option in tiCOMReset() 
 */
typedef enum
{
  tiSoftReset,
  tiHardReset,
  tiAutoReset
} tiReset_t;

/* 
 * Bit 0 Mask for the persistent option in tiINIDiscoverTargets() 
 */
#define NORMAL_ASSIGN_MASK            0x00000000
#define FORCE_PERSISTENT_ASSIGN_MASK  0x00000001

/* 
 * Bit 1 Mask for the auto login option in tiINIDiscoverTargets() 
 */
#define AUTO_LOGIN_MASK               0x00000000
#define NO_AUTO_LOGIN_MASK            0x00000002


/* 
 * Task Management task used in tiINITaskManagement()
 *
 * 1 AG_ABORT TASK - aborts the task identified by the Referenced  Task Tag field.
 * 2 AG_ABORT TASK SET - aborts all Tasks issued by this initiator on the Logical Unit 
 * 3 AG_CLEAR ACA - clears the Auto Contingent Allegiance condition.
 * 4 AG_CLEAR TASK SET - Aborts all Tasks (from all initiators) for the Logical Unit.
 * 5 AG_LOGICAL UNIT RESET 
 * 6 AG_TARGET WARM RESET  - iSCSI only
 * 7 AG_TARGET_COLD_RESET  - iSCSI only
 * 8 AG_TASK_REASSIGN      - iSCSI only
 * 9 AG_QUERY_TASK         - SAS only
 */

#define AG_ABORT_TASK          1
#define AG_ABORT_TASK_SET      2
#define AG_CLEAR_ACA           3
#define AG_CLEAR_TASK_SET      4
#define AG_LOGICAL_UNIT_RESET  5
#define AG_TARGET_WARM_RESET   6    /* iSCSI only */
#define AG_TARGET_COLD_RESET   7    /* iSCSI only */
#define AG_TASK_REASSIGN       8    /* iSCSI only */
#define AG_QUERY_TASK          9    /* SAS only   */


/*
 * Event types for ostiInitiatorEvent()
 */
typedef enum
{
  tiIntrEventTypeCnxError,
  tiIntrEventTypeDiscovery,
  tiIntrEventTypeTransportRecovery,
  tiIntrEventTypeTaskManagement,
  tiIntrEventTypeDeviceChange,
  tiIntrEventTypeLogin,
  tiIntrEventTypeLocalAbort  
} tiIntrEventType_t;

/*
 * Event status for ostiInitiatorEvent()
 */
typedef enum
{
  tiCnxUp,
  tiCnxDown
} tiCnxEventStatus_t;

typedef enum
{
  tiDiscOK,
  tiDiscFailed
} tiDiscEventStatus_t;

typedef enum
{
  tiLoginOK,
  tiLoginFailed,
  tiLogoutOK,
  tiLogoutFailed
} tiLoginEventStatus_t;

typedef enum
{
  tiRecOK,
  tiRecFailed,
  tiRecStarted
} tiRecEventStatus_t;

typedef enum
{
  tiTMOK,
  tiTMFailed
} tiTMEventStatus_t;

typedef enum
{
  tiDeviceRemoval,
  tiDeviceArrival,
  tiDeviceLoginReceived
} tiDevEventStatus_t;

typedef enum
{
  tiAbortOK,
  tiAbortFailed,
  tiAbortDelayed,  
  tiAbortInProgress
} tiAbortEventStatus_t;

/* 
 * SCSI SAM-2 Task Attribute
 */
#define TASK_UNTAGGED       0       /* Untagged      */
#define TASK_SIMPLE         1       /* Simple        */
#define TASK_ORDERED        2       /* Ordered       */
#define TASK_HEAD_OF_QUEUE  3       /* Head of Queue */
#define TASK_ACA            4       /* ACA           */

/*
 * Data direction for I/O request
 */
typedef enum
{
  tiDirectionIn   = 0x0000,
  tiDirectionOut  = 0x0001
}tiDataDirection_t;

/*
 * NVRAM error subEvents for encryption 
 */
typedef enum
{
    tiNVRAMSuccess       = 0x0000,
    tiNVRAMWriteFail     = 0x0001,
    tiNVRAMReadFail      = 0x0002,
    tiNVRAMNotFound      = 0x0003,
    tiNVRAMAccessTimeout = 0x0004
}tiEncryptSubEvent_t;

/* Event Logging */

/* Event Severity Codes */
#define IOCTL_EVT_SEV_OFF            0x00
#define IOCTL_EVT_SEV_ALWAYS_ON      0x01
#define IOCTL_EVT_SEV_ERROR          0x02
#define IOCTL_EVT_SEV_WARNING        0x03
#define IOCTL_EVT_SEV_INFORMATIONAL  0x04
#define IOCTL_EVT_SEV_DEBUG_L1       0x05
#define IOCTL_EVT_SEV_DEBUG_L2       0x06
#define IOCTL_EVT_SEV_DEBUG_L3       0x07

/* Event Source */
#define IOCTL_EVT_SRC_HW            0xF0000000
#define IOCTL_EVT_SRC_ITSDK         0x0F000000
#define IOCTL_EVT_SRC_FW            0x00F00000
#define IOCTL_EVT_SRC_TD_LAYER      0x000F0000
#define IOCTL_EVT_SRC_TARGET        0x0000F000
#define IOCTL_EVT_SRC_OSLAYER       0x00000F00
#define IOCTL_EVT_SRC_RESERVED      0x000000F0
#define IOCTL_EVT_SRC_RESERVED1     0x0000000F
/* Event Shifter */
#define IOCTL_EVT_SRC_HW_SHIFTER            28
#define IOCTL_EVT_SRC_ITSDK_SHIFTER         24
#define IOCTL_EVT_SRC_FW_SHIFTER            20
#define IOCTL_EVT_SRC_COMMON_LAYER_SHIFTER  16
#define IOCTL_EVT_SRC_TARGET_SHIFTER        12
#define IOCTL_EVT_SRC_OSLAYER_SHIFTER       8
#define IOCTL_EVT_SRC_RESERVED_SHIFTER      4
#define IOCTL_EVT_SRC_RESERVED1_SHIFTER     0

#define EVENTLOG_MAX_MSG_LEN          110

#define EVENT_ID_MAX        0xffffffff

#define DISCOVERY_IN_PROGRESS 0xFFFFFFFF

#define TI_SSP_INDIRECT_CDB_SIZE         64
/*
 * Flags in tiSuperScsiInitiatorRequest_t
 */
#define TI_SCSI_INITIATOR_DIF             0x00000001
#define TI_SCSI_INITIATOR_ENCRYPT         0x00000002
#define TI_SCSI_INITIATOR_INDIRECT_CDB    0x00000004
/*****************************************************************************
 *  TARGET SPECIFIC DEFINES AND ENUMS
 *****************************************************************************/

/*
 * Event types for ostiTargetEvent()
 */
typedef enum
{
  tiTgtEventTypeCnxError,
  tiTgtEventTypeDeviceChange
} tiTgtEventType_t;

/*
 * Flags in tiSuperScsiTargetRequest_t
 */
#define TI_SCSI_TARGET_DIF         0x00000001
#define TI_SCSI_TARGET_MIRROR      0x00000002
#define TI_SCSI_TARGET_ENCRYPT     0x00000004
#endif  /* TIDEFS_H */
