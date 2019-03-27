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
*******************************************************************************/
/*****************************************************************************
*
*   tdioctl.h
*
*   Abstract:   This module contains data structure definition used
*               by the Transport Dependent (TD) Layer IOCTL.
*
*
*   Notes:
*
*
** MODIFICATION HISTORY ******************************************************
*
* NAME        DATE        DESCRIPTION
* ----        ----        -----------
* IWN         12/11/02     Initial creation.
*
*
*****************************************************************************/


#ifndef TD_IOCTL_H

#define TD_IOCTL_H

//#include "global.h"

/*
 * PMC-Sierra IOCTL signature
 */
#define PMC_SIERRA_SIGNATURE                0x1234
#define PMC_SIERRA_IOCTL_SIGNATURE          "PMC-STRG"

/*
 * Major function code of IOCTL functions, common to target and initiator.
 */
#define IOCTL_MJ_CARD_PARAMETER             0x01
#define IOCTL_MJ_FW_CONTROL                 0x02
#define IOCTL_MJ_NVMD_GET                   0x03
#define IOCTL_MJ_NVMD_SET                   0x04
#define IOCTL_MJ_GET_EVENT_LOG1             0x05
#define IOCTL_MJ_GET_EVENT_LOG2             0x06
#define IOCTL_MJ_GET_CORE_DUMP              0x07
#define IOCTL_MJ_LL_TRACING                 0x08
#define IOCTL_MJ_FW_PROFILE                 0x09
#define IOCTL_MJ_MNID                       0x0A
#define IOCTL_MJ_ENCRYPTION_CTL             0x0B

#define IOCTL_MJ_FW_INFO                    0x0C

#define IOCTL_MJ_LL_API_TEST                0x11
#define IOCTL_MJ_CHECK_DPMC_EVENT           0x16
#define IOCTL_MJ_GET_FW_REV                 0x1A
#define IOCTL_MJ_GET_DEVICE_INFO            0x1B
#define IOCTL_MJ_GET_IO_ERROR_STATISTIC     0x1C
#define IOCTL_MJ_GET_IO_EVENT_STATISTIC     0x1D
#define IOCTL_MJ_GET_FORENSIC_DATA          0x1E
#define IOCTL_MJ_GET_DEVICE_LIST            0x1F
#define IOCTL_MJ_SMP_REQUEST				0x6D
#define IOCTL_MJ_GET_DEVICE_LUN               0x7A1
#define IOCTL_MJ_PHY_GENERAL_STATUS           0x7A6
#define IOCTL_MJ_PHY_DETAILS           	      0x7A7
#define IOCTL_MJ_SEND_BIST                  0x20
#define IOCTL_MJ_CHECK_FATAL_ERROR          0x70
#define IOCTL_MJ_FATAL_ERROR_DUMP_COMPLETE  0x71
#define IOCTL_MJ_GPIO                       0x41
#define IOCTL_MJ_SGPIO                      0x42
#define IOCTL_MJ_SEND_TMF					0x6E
#define	IOCTL_MJ_FATAL_ERROR_SOFT_RESET_TRIG 0x72
#define	IOCTL_MJ_FATAL_ERR_CHK_RET_FALSE    0x76
#define	IOCTL_MJ_FATAL_ERR_CHK_SEND_FALSE   0x76
#define	IOCTL_MJ_FATAL_ERR_CHK_SEND_TRUE    0x77


/*
 * Major function code of IOCTL functions, specific to initiator.
 */
#define IOCTL_MJ_INI_ISCSI_DISCOVERY        0x21
#define IOCTL_MJ_INI_SESSION_CONTROL        0x22
#define IOCTL_MJ_INI_SNIA_IMA               0x23
#define IOCTL_MJ_INI_SCSI                   0x24
#define IOCTL_MJ_INI_WMI                    0x25
#define IOCTL_MJ_INI_DRIVER_EVENT_LOG       0x26
#define IOCTL_MJ_INI_PERSISTENT_BINDING     0x27
#define IOCTL_MJ_INI_DRIVER_IDENTIFY        0x28

/* temp */
#define IOCTL_MJ_PORT_STOP        0x29
#define IOCTL_MJ_PORT_START       0x30

/* SPCv controller configuration page commands */
#define IOCTL_MJ_MODE_CTL_PAGE              0x40

#define IOCTL_MJ_SET_OR_GET_REGISTER        0x41

#define IOCTL_MJ_GET_PHY_PROFILE            0x44
#define IOCTL_MJ_SET_PHY_PROFILE            0x43

#define IOCTL_MJ_GET_DRIVER_VERSION         0x101

#define IOCTL_MN_PHY_PROFILE_COUNTERS        0x01
#define IOCTL_MN_PHY_PROFILE_COUNTERS_CLR    0x02
#define IOCTL_MN_PHY_PROFILE_BW_COUNTERS     0x03
#define IOCTL_MN_PHY_PROFILE_ANALOG_SETTINGS 0x04

/* 
 * Minor functions for Card parameter IOCTL functions.
 */
#define IOCTL_MN_CARD_GET_VPD_INFO              0x01
#define IOCTL_MN_CARD_GET_PORTSTART_INFO        0x02
#define IOCTL_MN_CARD_GET_INTERRUPT_CONFIG      0x03
#define IOCTL_MN_CARD_GET_PHY_ANALOGSETTING     0x04
#define IOCTL_MN_CARD_GET_TIMER_CONFIG          0x05
#define IOCTL_MN_CARD_GET_TYPE_FATAL_DUMP       0x06

/*
 * Minor functions for FW control IOCTL functions.
 */

/* Send FW data requests.
 */
#define IOCTL_MN_FW_DOWNLOAD_DATA         0x01

/* Send the request for burning the new firmware.
 */
#define IOCTL_MN_FW_DOWNLOAD_BURN         0x02

/* Poll for the flash burn phases. Sequences of poll function calls are
 * needed following the IOCTL_MN_FW_DOWNLOAD_BURN, IOCTL_MN_FW_BURN_OSPD
 * and IOCTL_MN_FW_ROLL_BACK_FW functions.
 */
#define IOCTL_MN_FW_BURN_POLL             0x03

/* Instruct the FW to roll back FW to prior revision.
 */
#define IOCTL_MN_FW_ROLL_BACK_FW          0x04

/* Instruct the FW to return the current firmware revision number.
 */
#define IOCTL_MN_FW_VERSION               0x05

/* Retrieve the maximum size of the OS Persistent Data stored on the card.
 */
#define IOCTL_MN_FW_GET_OSPD_SIZE   0x06

/*  Retrieve the OS Persistent Data from the card.
 */
#define IOCTL_MN_FW_GET_OSPD              0x07

/* Send a new OS Persistent Data to the card and burn in flash.
 */
#define IOCTL_MN_FW_BURN_OSPD           0x08

/* Retrieve the trace buffer from the card FW. Only available on the debug
 * version of the FW.
 */
#define IOCTL_MN_FW_GET_TRACE_BUFFER            0x0f

#define IOCTL_MN_NVMD_GET_CONFIG                0x0A
#define IOCTL_MN_NVMD_SET_CONFIG                0x0B

#define IOCTL_MN_FW_GET_CORE_DUMP_AAP1          0x0C
#define IOCTL_MN_FW_GET_CORE_DUMP_IOP           0x0D
#define IOCTL_MN_FW_GET_CORE_DUMP_FLASH_AAP1    0x12
#define IOCTL_MN_FW_GET_CORE_DUMP_FLASH_IOP     0x13

#define IOCTL_MN_LL_RESET_TRACE_INDEX           0x0e
#define IOCTL_MN_LL_GET_TRACE_BUFFER_INFO       0x0f
#define IOCTL_MN_LL_GET_TRACE_BUFFER            0x10

#define IOCTL_MN_ENCRYPTION_GET_INFO          0x13
#define IOCTL_MN_ENCRYPTION_SET_MODE          0x14
#define IOCTL_MN_ENCRYPTION_KEK_ADD           0x15
#define IOCTL_MN_ENCRYPTION_DEK_ADD           0x16
#define IOCTL_MN_ENCRYPTION_DEK_INVALID       0x17
#define IOCTL_MN_ENCRYPTION_KEK_NVRAM         0x18
#define IOCTL_MN_ENCRYPTION_DEK_ASSIGN        0x19
#define IOCTL_MN_ENCRYPTION_LUN_QUERY         0x1A
#define IOCTL_MN_ENCRYPTION_KEK_LOAD_NVRAM    0x1B
#define IOCTL_MN_ENCRYPTION_ERROR_QUERY       0x1C
#define IOCTL_MN_ENCRYPTION_DEK_TABLE_INIT    0x1D
#define IOCTL_MN_ENCRYPT_LUN_VERIFY           0x1E
#define IOCTL_MN_ENCRYPT_OPERATOR_MGMT        0x1F
#define IOCTL_MN_ENCRYPT_SET_DEK_CONFIG_PAGE  0x21
#define IOCTL_MN_ENCRYPT_SET_CONTROL_PAGE     0x22
#define IOCTL_MN_ENCRYPT_SET_OPERATOR_CMD     0x23
#define IOCTL_MN_ENCRYPT_TEST_EXECUTE         0x24
#define IOCTL_MN_ENCRYPT_SET_HMAC_CONFIG_PAGE 0x25
#define IOCTL_MN_ENCRYPT_GET_OPERATOR_CMD     0x26
#define IOCTL_MN_ENCRYPT_RESCAN               0x27
#ifdef SOFT_RESET_TEST
#define IOCTL_MN_SOFT_RESET                   0x28
#endif
/* SPCv configuration pages */
#define IOCTL_MN_MODE_SENSE                   0x30
#define IOCTL_MN_MODE_SELECT                  0x31

#define IOCTL_MN_TISA_TEST_ENCRYPT_DEK_DUMP   0x51

#define IOCTL_MN_FW_GET_EVENT_FLASH_LOG1        0x5A
#define IOCTL_MN_FW_GET_EVENT_FLASH_LOG2        0x6A
#define IOCTL_MN_GET_EVENT_LOG1                 0x5B
#define IOCTL_MN_GET_EVENT_LOG2                 0x6B

#define IOCTL_MN_GPIO_PINSETUP	            	0x01
#define IOCTL_MN_GPIO_EVENTSETUP             	0x02
#define IOCTL_MN_GPIO_READ 		                0x03
#define IOCTL_MN_GPIO_WRITE	                	0x04

#define IOCTL_MN_TMF_DEVICE_RESET				0x6F
#define IOCTL_MN_TMF_LUN_RESET					0x70
typedef struct tdFWControl
{
  bit32   retcode;    /* ret code (status) = (bit32)oscmCtrlEvnt_e      */
  bit32   phase;      /* ret code phase    = (bit32)agcmCtrlFwPhase_e   */
  bit32   phaseCmplt; /* percent complete for the current update phase  */
  bit32   version;    /* Hex encoded firmware version number            */
  bit32   offset;     /* Used for downloading firmware                  */
  bit32   len;        /* len of buffer                                  */
  bit32   size;       /* Used in OS VPD and Trace get size operations.  */
  bit32   reserved;   /* padding required for 64 bit alignment          */
  bit8    buffer[1];  /* Start of buffer                                */
} tdFWControl_t;


typedef struct tdFWControlEx
{
  tdFWControl_t *tdFWControl;
  bit8    *buffer;    // keep buffer pointer to be freed when the responce comes
  bit8    *virtAddr;  /* keep virtual address of the data */
  bit8    *usrAddr;   /* keep virtual address of the user data */
  bit32   len;        /* len of buffer                                  */
  void    *payload;   /* pointer to IOCTL Payload */
  bit8    inProgress;  /* if 1 - the IOCTL request is in progress */
  void    *param1;
  void    *param2;
  void    *param3;
} tdFWControlEx_t;

/************************************************************/
//This flag and datastructure are specific for fw profiling, Now defined as
// compiler flag
//#define SPC_ENABLE_PROFILE

#ifdef SPC_ENABLE_PROFILE
typedef struct tdFWProfile
{
    bit32   status;
    bit32   tcid;
    bit32   processor;  /* processor name "iop/aap1"      */
    bit32   cmd;        /* cmd to fw   */
    bit32   len;        /* len of buffer                                  */
    bit32   codeStartAdd;
    bit32   codeEndAdd;
    bit32   reserved;   /* padding required for 64 bit alignment          */
    bit8    buffer[1];  /* Start of buffer                                */
} tdFWProfile_t;

/************************************************/
/**Definations for FW profile*/
#define FW_PROFILE_PROCESSOR_ID_IOP  0x00
#define FW_PROFILE_PROCESSOR_ID_AAP1 0x02
/* definitions for sub operation */
#define START_TIMER_PROFILE          0x01
#define START_CODE_PROFILE           0x02
#define STOP_TIMER_PROFILE           0x81
#define STOP_CODE_PROFILE            0x82
/************************************************/

typedef struct tdFWProfileEx
{
  tdFWProfile_t *tdFWProfile;
  bit8    *buffer;    // keep buffer pointer to be freed when the responce comes
  bit8    *virtAddr;  /* keep virtual address of the data */
  bit8    *usrAddr;   /* keep virtual address of the user data */
  bit32   len;        /* len of buffer                                  */
  void    *payload;   /* pointer to IOCTL Payload */
  bit8    inProgress;  /* if 1 - the IOCTL request is in progress */
  void    *param1;
  void    *param2;
  void    *param3;
} tdFWProfileEx_t;
#endif
/************************************************************/
typedef struct tdVPDControl
{
  bit32   retcode;    /* ret code (status)                              */
  bit32   phase;      /* ret code phase                                 */
  bit32   phaseCmplt; /* percent complete for the current update phase  */
  bit32   version;    /* Hex encoded firmware version number            */
  bit32   offset;     /* Used for downloading firmware                  */
  bit32   len;        /* len of buffer                                  */
  bit32   size;       /* Used in OS VPD and Trace get size operations.  */
  bit8    deviceID;   /* padding required for 64 bit alignment          */
  bit8    reserved1;
  bit16   reserved2;
  bit32   signature;
  bit8    buffer[1];  /* Start of buffer                                */
} tdVPDControl_t;

typedef struct tdDeviceInfoIOCTL_s
{
  bit8       deviceType;   // TD_SATA_DEVICE or TD_SAS_DEVICE
  bit8       linkRate;     // 0x08: 1.5 Gbit/s; 0x09: 3.0; 0x0A: 6.0 Gbit/s.
  bit8       phyId;
  bit8       reserved;
  bit32      sasAddressHi; // SAS address high
  bit32      sasAddressLo; // SAS address low
  bit32      up_sasAddressHi; // upstream SAS address high
  bit32      up_sasAddressLo; // upstream SAS address low
  bit32      ishost;
  bit32      isEncryption;    // is encryption enabled
  bit32      isDIF;           // is DIF enabled
  unsigned long DeviceHandle;
  bit32      host_num;
  bit32      channel;
  bit32      id;
  bit32      lun;
}tdDeviceInfoIOCTL_t;

/* Payload of IOCTL dump device list at OS layer */
typedef struct tdDeviceInfoPayload_s
{
  bit32      PathId;
  bit32      TargetId;
  bit32      Lun;
  bit32      Reserved;         /* Had better aligned to 64-bit. */

  /* output */
  tdDeviceInfoIOCTL_t  devInfo;
}tdDeviceInfoPayload_t;

typedef struct tdDeviceListPayload_s
{
  bit32  realDeviceCount;// the real device out in the array, returned by driver
  bit32  deviceLength;   // the length of tdDeviceInfoIOCTL_t array
  bit8   pDeviceInfo[1]; // point to tdDeviceInfoIOCTL_t array
}tdDeviceListPayload_t;

// Payload of IO error and event statistic IOCTL.
typedef struct tdIoErrorEventStatisticIOCTL_s
{
  bit32  agOSSA_IO_COMPLETED_ERROR_SCSI_STATUS;
  bit32  agOSSA_IO_ABORTED;
  bit32  agOSSA_IO_OVERFLOW;
  bit32  agOSSA_IO_UNDERFLOW;
  bit32  agOSSA_IO_FAILED;
  bit32  agOSSA_IO_ABORT_RESET;
  bit32  agOSSA_IO_NOT_VALID;
  bit32  agOSSA_IO_NO_DEVICE;
  bit32  agOSSA_IO_ILLEGAL_PARAMETER;
  bit32  agOSSA_IO_LINK_FAILURE;
  bit32  agOSSA_IO_PROG_ERROR;
  bit32  agOSSA_IO_DIF_IN_ERROR;
  bit32  agOSSA_IO_DIF_OUT_ERROR;
  bit32  agOSSA_IO_ERROR_HW_TIMEOUT;
  bit32  agOSSA_IO_XFER_ERROR_BREAK;
  bit32  agOSSA_IO_XFER_ERROR_PHY_NOT_READY;
  bit32  agOSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED;
  bit32  agOSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION;
  bit32  agOSSA_IO_OPEN_CNX_ERROR_BREAK;
  bit32  agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS;
  bit32  agOSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION;
  bit32  agOSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED;
  bit32  agOSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY;
  bit32  agOSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION;
  bit32  agOSSA_IO_OPEN_CNX_ERROR_UNKNOWN_ERROR;
  bit32  agOSSA_IO_XFER_ERROR_NAK_RECEIVED;
  bit32  agOSSA_IO_XFER_ERROR_ACK_NAK_TIMEOUT;
  bit32  agOSSA_IO_XFER_ERROR_PEER_ABORTED;
  bit32  agOSSA_IO_XFER_ERROR_RX_FRAME;
  bit32  agOSSA_IO_XFER_ERROR_DMA;
  bit32  agOSSA_IO_XFER_ERROR_CREDIT_TIMEOUT;
  bit32  agOSSA_IO_XFER_ERROR_SATA_LINK_TIMEOUT;
  bit32  agOSSA_IO_XFER_ERROR_SATA;
  bit32  agOSSA_IO_XFER_ERROR_ABORTED_DUE_TO_SRST;
  bit32  agOSSA_IO_XFER_ERROR_REJECTED_NCQ_MODE;
  bit32  agOSSA_IO_XFER_ERROR_ABORTED_NCQ_MODE;
  bit32  agOSSA_IO_XFER_OPEN_RETRY_TIMEOUT;
  bit32  agOSSA_IO_XFER_SMP_RESP_CONNECTION_ERROR;
  bit32  agOSSA_IO_XFER_ERROR_UNEXPECTED_PHASE;
  bit32  agOSSA_IO_XFER_ERROR_XFER_RDY_OVERRUN;
  bit32  agOSSA_IO_XFER_ERROR_XFER_RDY_NOT_EXPECTED;
  bit32  agOSSA_IO_XFER_ERROR_CMD_ISSUE_ACK_NAK_TIMEOUT;
  bit32  agOSSA_IO_XFER_ERROR_CMD_ISSUE_BREAK_BEFORE_ACK_NAK;
  bit32  agOSSA_IO_XFER_ERROR_CMD_ISSUE_PHY_DOWN_BEFORE_ACK_NAK;
  bit32  agOSSA_IO_XFER_ERROR_OFFSET_MISMATCH;
  bit32  agOSSA_IO_XFER_ERROR_XFER_ZERO_DATA_LEN;
  bit32  agOSSA_IO_XFER_CMD_FRAME_ISSUED;
  bit32  agOSSA_IO_ERROR_INTERNAL_SMP_RESOURCE;
  bit32  agOSSA_IO_PORT_IN_RESET;
  bit32  agOSSA_IO_DS_NON_OPERATIONAL;
  bit32  agOSSA_IO_DS_IN_RECOVERY;
  bit32  agOSSA_IO_TM_TAG_NOT_FOUND;
  bit32  agOSSA_IO_XFER_PIO_SETUP_ERROR;
  bit32  agOSSA_IO_SSP_EXT_IU_ZERO_LEN_ERROR;
  bit32  agOSSA_IO_DS_IN_ERROR;
  bit32  agOSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY;
  bit32  agOSSA_IO_ABORT_IN_PROGRESS;
  bit32  agOSSA_IO_ABORT_DELAYED;
  bit32  agOSSA_IO_INVALID_LENGTH;
  bit32  agOSSA_IO_OPEN_CNX_ERROR_HW_RESOURCE_BUSY_ALT;
  bit32  agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED;
  bit32  agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO;
  bit32  agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST;
  bit32  agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE;
  bit32  agOSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED;
  bit32  agOSSA_IO_DS_INVALID;
  bit32  agOSSA_IO_XFER_READ_COMPL_ERR;
  bit32  agOSSA_IO_XFER_ERR_LAST_PIO_DATAIN_CRC_ERR;
  bit32  agOSSA_IO_XFR_ERROR_INTERNAL_CRC_ERROR;
  bit32  agOSSA_MPI_IO_RQE_BUSY_FULL;
  bit32  agOSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE;
  bit32  agOSSA_MPI_ERR_ATAPI_DEVICE_BUSY;
  bit32  agOSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS;
  bit32  agOSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH;
  bit32  agOSSA_IO_XFR_ERROR_CIPHER_MODE_INVALID;
  bit32  agOSSA_IO_XFR_ERROR_DEK_IV_MISMATCH;
  bit32  agOSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR;
  bit32  agOSSA_IO_XFR_ERROR_INTERNAL_RAM;
  bit32  agOSSA_IO_XFR_ERROR_DIF_MISMATCH;
  bit32  agOSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH;
  bit32  agOSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH;
  bit32  agOSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH;
  bit32  agOSSA_IO_XFR_ERROR_INVALID_SSP_RSP_FRAME;
  bit32  agOSSA_IO_XFER_ERR_EOB_DATA_OVERRUN;
  bit32  agOSSA_IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS;
  bit32  agOSSA_IO_OPEN_CNX_ERROR_OPEN_PREEMPTED;
  bit32  agOSSA_IO_XFR_ERROR_DEK_ILLEGAL_TABLE;
  bit32  agOSSA_IO_XFER_ERROR_DIF_INTERNAL_ERROR;
  bit32  agOSSA_MPI_ERR_OFFLOAD_DIF_OR_ENC_NOT_ENABLED;
  bit32  agOSSA_IO_UNKNOWN_ERROR;

} tdIoErrorEventStatisticIOCTL_t;

/*
01: soft error
02: not ready
03: medium error
04: hardware error
05: illegal request
06: unit attention
0b: abort command
*/
typedef struct tdSenseKeyCount_s{
  bit32     SoftError;
  bit32     MediumNotReady;
  bit32     MediumError;
  bit32     HardwareError;
  bit32     IllegalRequest;
  bit32     UnitAttention;
  bit32     AbortCommand;
  bit32     OtherKeyType;
}tdSenseKeyCount_t;

/*
Code Status Command  completed Service response
00h GOOD Yes COMMAND COMPLETE
02h CHECK CONDITION Yes COMMAND COMPLETE
04h CONDITION MET Yes COMMAND COMPLETE
08h BUSY Yes COMMAND COMPLETE
10h Obsolete
14h Obsolete
18h RESERVATION CONFLICT Yes COMMAND COMPLETE
22h Obsolete
28h TASK SET FULL Yes COMMAND COMPLETE
30h ACA ACTIVE Yes COMMAND COMPLETE
40h TASK ABORTED Yes COMMAND COMPLETE
*/
typedef struct tdSCSIStatusCount_s{
  bit32     GoodStatus;
  bit32     CheckCondition;
  bit32     ConditionMet;
  bit32     BusyStatus;
  bit32     ResvConflict;
  bit32     TaskSetFull;
  bit32     AcaActive;
  bit32     TaskAborted;
  bit32     ObsoleteStatus;
}tdSCSIStatusCount_t;

/* Payload of Io Error Statistic IOCTL. */
typedef struct tdIoErrorStatisticPayload_s
{
  bit32         flag;
  bit32         Reserved;         /* Had better aligned to 64-bit. */

  /* output */
  tdIoErrorEventStatisticIOCTL_t  IoError;
  tdSCSIStatusCount_t             ScsiStatusCounter;
  tdSenseKeyCount_t               SenseKeyCounter;
} tdIoErrorStatisticPayload_t;

/* Payload of Io Error Statistic IOCTL. */
typedef struct tdIoEventStatisticPayload_s
{
  bit32         flag;
  bit32         Reserved;         /* Had better aligned to 64-bit. */

  /* output */
  tdIoErrorEventStatisticIOCTL_t  IoEvent;
} tdIoEventStatisticPayload_t;

/* Payload of Register IOCTL. */
typedef struct tdRegisterPayload_s
{
  bit32         flag;
  bit32         busNum;
  bit32         RegAddr;         /* Register address */
  bit32         RegValue;        /* Register value */

} tdRegisterPayload_t;


#define FORENSIC_DATA_TYPE_GSM_SPACE        1
#define FORENSIC_DATA_TYPE_QUEUE            2
#define FORENSIC_DATA_TYPE_FATAL            3
#define FORENSIC_DATA_TYPE_NON_FATAL        4
#define FORENSIC_DATA_TYPE_IB_QUEUE         5
#define FORENSIC_DATA_TYPE_OB_QUEUE          6
#define FORENSIC_DATA_TYPE_CHECK_FATAL      0x70

#define FORENSIC_Q_TYPE_INBOUND          1
#define FORENSIC_Q_TYPE_OUTBOUND         2

/* get forensic data IOCTL payload */
typedef struct tdForensicDataPayload_s
{
  bit32   DataType;
  union
  {
    struct
    {
      bit32   directLen;
      bit32   directOffset;
      bit32   readLen;
      bit8   directData[1]; 
    } gsmBuffer;

    struct
    {
      bit16  queueType;
      bit16  queueIndex;
      bit32  directLen;
      bit8   directData[1];
    } queueBuffer;

    struct
    {
      bit32 directLen;
      bit32 directOffset;
      bit32 readLen;
      bit8  directData[1];
    } dataBuffer;
  }; 
}tdForensicDataPayload_t;

typedef struct tdBistPayload_s
{
  bit32  testType;
  bit32  testLength;
  bit32  testData[29];
}tdBistPayload_t;

typedef struct _TSTMTID_CARD_LOCATION_INFO
{
  bit32               CardNo;
  bit32               Bus;
  bit32               Slot;
  bit32               Device;
  bit32               Function;
  bit32               IOLower;
  bit32               IO_Upper;
  bit32               VidDid;
  bit32               PhyMem;
  bit32               Flag;

} TSTMTID_CARD_LOCATION_INFO;

typedef struct _TSTMTID_TRACE_BUFFER_INFO
{
  bit32               CardNo;
  bit32               TraceCompiled;
  bit32               BufferSize;
  bit32               CurrentIndex;
  bit32               TraceWrap;
  bit32               CurrentTraceIndexWrapCount;
  bit32               TraceMask;
  bit32               Flag;

} TSTMTID_TRACE_BUFFER_INFO;

#define FetchBufferSIZE  32
#define LowFence32Bits   0xFCFD1234
#define HighFence32Bits  0x5678ABDC

typedef struct _TSTMTID_TRACE_BUFFER_FETCH
{
  bit32               CardNo;
  bit32               BufferOffsetBegin;
  bit32               LowFence;
  bit8                Data[FetchBufferSIZE];
  bit32               HighFence;
  bit32               Flag;

} TSTMTID_TRACE_BUFFER_FETCH;


typedef struct _TSTMTID_TRACE_BUFFER_RESET
{
  bit32               CardNo;
  bit32               Reset;
  bit32               TraceMask;
  bit32               Flag;

} TSTMTID_TRACE_BUFFER_RESET;



typedef struct tdPhyCount_s{
  bit32 Phy;
  bit32 BW_tx;
  bit32 BW_rx;
  bit32 InvalidDword;
  bit32 runningDisparityError;
  bit32 codeViolation;
  bit32 LossOfSyncDW;
  bit32 phyResetProblem;
  bit32 inboundCRCError;
}tdPhyCount_t;


typedef struct _PHY_GENERAL_STATE
{
	bit32 Dword0;
	bit32 Dword1;

}GetPhyGenState_t;
typedef struct agsaPhyGeneralState_s
{
  GetPhyGenState_t  PhyGenData[16];
  bit32 Reserved1;
  bit32 Reserved2;
} agsaPhyGeneralState_t;

typedef struct _PHY_DETAILS_
{       
  bit8    sasAddressLo[4];
  bit8    sasAddressHi[4];
  bit8    attached_sasAddressLo[4];
  bit8    attached_sasAddressHi[4];
  bit8    attached_phy;
  bit8    attached_dev_type ;
}PhyDetails_t;

enum SAS_SATA_DEVICE_TYPE {
  SAS_PHY_NO_DEVICE ,
  SAS_PHY_END_DEVICE,
  SAS_PHY_EXPANDER_DEVICE,
  SAS_PHY_SATA_DEVICE = 0x11,
};
#define PHY_SETTINGS_LEN   1024

#endif  /* TD_IOCTL_H */
