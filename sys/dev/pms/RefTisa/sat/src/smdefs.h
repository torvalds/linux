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
#ifndef __SMDEFS_H__
#define __SMDEFS_H__

#include <dev/pms/RefTisa/tisa/sassata/common/ossa.h>

/* the index for memory requirement, must be continious */
#define SM_ROOT_MEM_INDEX                          0                       /**< the index of dm root memory */
#define SM_DEVICE_MEM_INDEX                        1                       /**< the index of Device descriptors memory */
#define SM_IO_MEM_INDEX                            2                       /**< the index of IO command descriptors memory */


#define SM_MAX_DEV                              256
#define SM_MAX_IO                               1024

#define SM_USECS_PER_TICK                       1000000                   /**< defines the heart beat of the LL layer 10ms */

enum sm_locks_e
{
  SM_TIMER_LOCK = 0,
  SM_DEVICE_LOCK,
  SM_INTERNAL_IO_LOCK,
  SM_EXTERNAL_IO_LOCK,
  SM_NCQ_TAG_LOCK,
  SM_TBD_LOCK,
  SM_MAX_LOCKS
};

/* ATA device type */
#define SATA_ATA_DEVICE                           0x01                       /**< ATA ATA device type */
#define SATA_ATAPI_DEVICE                         0x02                       /**< ATA ATAPI device type */
#define SATA_PM_DEVICE                            0x03                       /**< ATA PM device type */
#define SATA_SEMB_DEVICE                          0x04                       /**< ATA SEMB device type */
#define SATA_SEMB_WO_SEP_DEVICE                   0x05                       /**< ATA SEMB without SEP device type */
#define UNKNOWN_DEVICE                            0xFF

/*
 *  FIS type 
 */
#define PIO_SETUP_DEV_TO_HOST_FIS   0x5F
#define REG_DEV_TO_HOST_FIS         0x34 
#define SET_DEV_BITS_FIS            0xA1

/* 
 * ATA Command code 
 */
#define SAT_READ_FPDMA_QUEUED                 0x60
#define SAT_READ_DMA_EXT                      0x25
#define SAT_READ_DMA                          0xC8
#define SAT_WRITE_FPDMA_QUEUED                0x61
#define SAT_WRITE_DMA_EXT                     0x35
#define SAT_WRITE_DMA_FUA_EXT                 0x3D
#define SAT_WRITE_DMA                         0xCA
#define SAT_CHECK_POWER_MODE                  0xE5
#define SAT_READ_LOG_EXT                      0x2F
#define SAT_READ_VERIFY_SECTORS               0x40
#define SAT_READ_VERIFY_SECTORS_EXT           0x42
#define SAT_SMART                             0xB0
#define SAT_SMART_EXEUTE_OFF_LINE_IMMEDIATE   0xD4
#define SAT_SMART_RETURN_STATUS               0xDA
#define SAT_SMART_READ_LOG                    0xD5
#define SAT_SMART_ENABLE_OPERATIONS           0xD8
#define SAT_SMART_DISABLE_OPERATIONS          0xD9
#define SAT_FLUSH_CACHE                       0xE7
#define SAT_FLUSH_CACHE_EXT                   0xEA
#define SAT_STANDBY                           0xE2
#define SAT_MEDIA_EJECT                       0xED
#define SAT_WRITE_SECTORS                     0x30
#define SAT_WRITE_SECTORS_EXT                 0x34
#define SAT_READ_SECTORS                      0x20
#define SAT_READ_SECTORS_EXT                  0x24
#define SAT_GET_MEDIA_STATUS                  0xDA
#define SAT_SET_FEATURES                      0xEF
#define SAT_IDENTIFY_DEVICE                   0xEC
#define SAT_READ_BUFFER                       0xE4
#define SAT_WRITE_BUFFER                      0xE8

/* 
 * ATAPI Command code 
*/
#define SAT_IDENTIFY_PACKET_DEVICE            0xA1
#define SAT_PACKET                            0xA0
#define SAT_DEVICE_RESET                      0x08
#define SAT_EXECUTE_DEVICE_DIAGNOSTIC         0x90


/* 
 * ATA Status Register Mask 
 */
#define ERR_ATA_STATUS_MASK                   0x01    /* Error/check bit  */
#define DRQ_ATA_STATUS_MASK                   0x08    /* Data Request bit */
#define DF_ATA_STATUS_MASK                    0x20    /* Device Fault bit */
#define DRDY_ATA_STATUS_MASK                  0x40    /* Device Ready bit */
#define BSY_ATA_STATUS_MASK                   0x80    /* Busy bit         */

/* 
 * ATA Error Register Mask 
 */
#define NM_ATA_ERROR_MASK                     0x02    /* No media present bit         */
#define ABRT_ATA_ERROR_MASK                   0x04    /* Command aborted bit          */
#define MCR_ATA_ERROR_MASK                    0x08    /* Media change request bit     */
#define IDNF_ATA_ERROR_MASK                   0x10    /* Address not found bit        */
#define MC_ATA_ERROR_MASK                     0x20    /* Media has changed bit        */
#define UNC_ATA_ERROR_MASK                    0x40    /* Uncorrectable data error bit */
#define ICRC_ATA_ERROR_MASK                   0x80    /* Interface CRC error bit      */




/*
 *  transfer length and LBA limit 2^28 See identify device data word 61:60
 *  ATA spec p125
 *  7 zeros
 */
#define SAT_TR_LBA_LIMIT                      0x10000000

/*
 *  transfer length and LBA limit 2^48 See identify device data word 61:60
 *  ATA spec p125
 *  12 zeros
 */
#define SAT_EXT_TR_LBA_LIMIT                  0x1000000000000


/*
 * ATA command type. This is for setting LBA, Sector Count
 */
#define SAT_NON_EXT_TYPE                      0
#define SAT_EXT_TYPE                          1
#define SAT_FP_TYPE                           2


/*
 * Report LUNs response data.
 */
typedef struct smScsiReportLun_s
{
  bit8              len[4];
  bit32             reserved;
  tiLUN_t           lunList[1];
} smScsiReportLun_t;

/* Inquiry vendor string */
#define AG_SAT_VENDOR_ID_STRING               "ATA     "

/*
 * Simple form of SATA Identify Device Data, similar definition is defined by
 * LL Layer as agsaSATAIdentifyData_t.
 */
typedef struct satSimpleSATAIdentifyData_s
{
  bit16   word[256];
} satSimpleSATAIdentifyData_t;


/*
 * READ LOG EXT page 10h
 */
typedef struct satReadLogExtPage10h_s
{
  bit8   byte[512];
} satReadLogExtPage10h_t;

/*
 * READ LOG EXT Extended Self-test log
 * ATA Table27 p196
 */
typedef struct satReadLogExtSelfTest_s
{
  bit8   byte[512];
} satReadLogExtSelfTest_t;

/*
 * SMART READ LOG Self-test log 
 * ATA Table60 p296
 */
typedef struct satSmartReadLogSelfTest_s
{
  bit8   byte[512];
} satSmartReadLogSelfTest_t;


/* 
 * Flag definition for satIntFlag field in smSatInternalIo_t.
 */

/* Original NCQ I/O already completed, so at the completion of READ LOG EXT
 *  page 10h, ignore the TAG tranaltion to get the failed I/O
 */
#define AG_SAT_INT_IO_FLAG_ORG_IO_COMPLETED   0x00000001

#define INQUIRY_SUPPORTED_VPD_PAGE                          0x00
#define INQUIRY_UNIT_SERIAL_NUMBER_VPD_PAGE                 0x80
#define INQUIRY_DEVICE_IDENTIFICATION_VPD_PAGE              0x83
#define INQUIRY_ATA_INFORMATION_VPD_PAGE                    0x89
#define INQUIRY_BLOCK_DEVICE_CHARACTERISTICS_VPD_PAGE       0xB1

#define MODESENSE_CONTROL_PAGE                            0x0A
#define MODESENSE_READ_WRITE_ERROR_RECOVERY_PAGE          0x01
#define MODESENSE_CACHING                                 0x08
#define MODESENSE_INFORMATION_EXCEPTION_CONTROL_PAGE      0x1C
#define MODESENSE_RETURN_ALL_PAGES                        0x3F
#define MODESENSE_VENDOR_SPECIFIC_PAGE                    0x00

#define MODESELECT_CONTROL_PAGE                           0x0A
#define MODESELECT_READ_WRITE_ERROR_RECOVERY_PAGE         0x01
#define MODESELECT_CACHING                                0x08
#define MODESELECT_INFORMATION_EXCEPTION_CONTROL_PAGE     0x1C
#define MODESELECT_RETURN_ALL_PAGES                       0x3F
#define MODESELECT_VENDOR_SPECIFIC_PAGE                   0x00

#define LOGSENSE_SUPPORTED_LOG_PAGES                      0x00
#define LOGSENSE_SELFTEST_RESULTS_PAGE                    0x10
#define LOGSENSE_INFORMATION_EXCEPTIONS_PAGE              0x2F


/*
 *  Bit mask definition
 */
#define SCSI_EVPD_MASK               0x01
#define SCSI_IMMED_MASK              0x01
#define SCSI_NACA_MASK               0x04
#define SCSI_LINK_MASK               0x01
#define SCSI_PF_MASK                 0x10
#define SCSI_DEVOFFL_MASK            0x02
#define SCSI_UNITOFFL_MASK           0x01
#define SCSI_START_MASK              0x01
#define SCSI_LOEJ_MASK               0x02
#define SCSI_NM_MASK                 0x02
#define SCSI_FLUSH_CACHE_IMMED_MASK              0x02
#define SCSI_FUA_NV_MASK                         0x02
#define SCSI_VERIFY_BYTCHK_MASK                  0x02
#define SCSI_FORMAT_UNIT_IMMED_MASK              0x02
#define SCSI_FORMAT_UNIT_FOV_MASK                0x80
#define SCSI_FORMAT_UNIT_DCRT_MASK               0x20
#define SCSI_FORMAT_UNIT_IP_MASK                 0x08
#define SCSI_WRITE_SAME_LBDATA_MASK              0x02
#define SCSI_WRITE_SAME_PBDATA_MASK              0x04
#define SCSI_SYNC_CACHE_IMMED_MASK               0x02
#define SCSI_WRITE_N_VERIFY_BYTCHK_MASK          0x02
#define SCSI_SEND_DIAGNOSTIC_SELFTEST_MASK       0x04
#define SCSI_FORMAT_UNIT_DEFECT_LIST_FORMAT_MASK 0x07
#define SCSI_FORMAT_UNIT_FMTDATA_MASK            0x10
#define SCSI_FORMAT_UNIT_DCRT_MASK               0x20
#define SCSI_FORMAT_UNIT_CMPLIST_MASK            0x08
#define SCSI_FORMAT_UNIT_LONGLIST_MASK           0x20
#define SCSI_READ10_FUA_MASK                     0x08
#define SCSI_READ12_FUA_MASK                     0x08
#define SCSI_READ16_FUA_MASK                     0x08
#define SCSI_WRITE10_FUA_MASK                    0x08
#define SCSI_WRITE12_FUA_MASK                    0x08
#define SCSI_WRITE16_FUA_MASK                    0x08
#define SCSI_READ_CAPACITY10_PMI_MASK            0x01
#define SCSI_READ_CAPACITY16_PMI_MASK            0x01
#define SCSI_MODE_SENSE6_PC_MASK                 0xC0
#define SCSI_MODE_SENSE6_PAGE_CODE_MASK          0x3F
#define SCSI_MODE_SENSE10_PC_MASK                0xC0
#define SCSI_MODE_SENSE10_LLBAA_MASK             0x10
#define SCSI_MODE_SENSE10_PAGE_CODE_MASK         0x3F
#define SCSI_SEND_DIAGNOSTIC_TEST_CODE_MASK      0xE0
#define SCSI_LOG_SENSE_PAGE_CODE_MASK            0x3F
#define SCSI_MODE_SELECT6_PF_MASK                0x10
#define SCSI_MODE_SELECT6_AWRE_MASK              0x80
#define SCSI_MODE_SELECT6_RC_MASK                0x10
#define SCSI_MODE_SELECT6_EER_MASK               0x08
#define SCSI_MODE_SELECT6_PER_MASK               0x04
#define SCSI_MODE_SELECT6_DTE_MASK               0x02
#define SCSI_MODE_SELECT6_DCR_MASK               0x01
#define SCSI_MODE_SELECT6_WCE_MASK               0x04
#define SCSI_MODE_SELECT6_DRA_MASK               0x20
#define SCSI_MODE_SELECT6_PERF_MASK              0x80
#define SCSI_MODE_SELECT6_TEST_MASK              0x04
#define SCSI_MODE_SELECT6_DEXCPT_MASK            0x08
#define SCSI_MODE_SELECT10_PF_MASK               0x10
#define SCSI_MODE_SELECT10_LONGLBA_MASK          0x01
#define SCSI_MODE_SELECT10_AWRE_MASK             0x80
#define SCSI_MODE_SELECT10_RC_MASK               0x10
#define SCSI_MODE_SELECT10_EER_MASK              0x08
#define SCSI_MODE_SELECT10_PER_MASK              0x04
#define SCSI_MODE_SELECT10_DTE_MASK              0x02
#define SCSI_MODE_SELECT10_DCR_MASK              0x01
#define SCSI_MODE_SELECT10_WCE_MASK              0x04
#define SCSI_MODE_SELECT10_DRA_MASK              0x20
#define SCSI_MODE_SELECT10_PERF_MASK             0x80
#define SCSI_MODE_SELECT10_TEST_MASK             0x04
#define SCSI_MODE_SELECT10_DEXCPT_MASK           0x08
#define SCSI_WRITE_N_VERIFY10_FUA_MASK           0x08
#define SCSI_REQUEST_SENSE_DESC_MASK             0x01
#define SCSI_READ_BUFFER_MODE_MASK               0x1F

#define ATA_REMOVABLE_MEDIA_DEVICE_MASK          0x80
#define SCSI_REASSIGN_BLOCKS_LONGLIST_MASK       0x01
#define SCSI_REASSIGN_BLOCKS_LONGLBA_MASK        0x02


#define SENSE_DATA_LENGTH                        0x12 /* 18 */    
#define SELFTEST_RESULTS_LOG_PAGE_LENGTH         404
#define INFORMATION_EXCEPTIONS_LOG_PAGE_LENGTH   11
#define ZERO_MEDIA_SERIAL_NUMBER_LENGTH          8                  

#define LOG_SENSE_0 0
#define LOG_SENSE_1 1
#define LOG_SENSE_2 2

#define READ_BUFFER_DATA_MODE                    0x02
#define READ_BUFFER_DESCRIPTOR_MODE              0x03
#define READ_BUFFER_DESCRIPTOR_MODE_DATA_LEN     0x04

#define WRITE_BUFFER_DATA_MODE                   0x02
#define WRITE_BUFFER_DL_MICROCODE_SAVE_MODE      0x05

/* bit mask */
#define BIT0_MASK                                0x01
#define BIT1_MASK                                0x02
#define BIT2_MASK                                0x04
#define BIT3_MASK                                0x08
#define BIT4_MASK                                0x10
#define BIT5_MASK                                0x20
#define BIT6_MASK                                0x40
#define BIT7_MASK                                0x80

#define MODE_SENSE6_RETURN_ALL_PAGES_LEN         68
#define MODE_SENSE6_CONTROL_PAGE_LEN             24
#define MODE_SENSE6_READ_WRITE_ERROR_RECOVERY_PAGE_LEN 24
#define MODE_SENSE6_CACHING_LEN                  32
#define MODE_SENSE6_INFORMATION_EXCEPTION_CONTROL_PAGE_LEN 24


#define MODE_SENSE10_RETURN_ALL_PAGES_LEN         68 + 4
#define MODE_SENSE10_CONTROL_PAGE_LEN             24 + 4
#define MODE_SENSE10_READ_WRITE_ERROR_RECOVERY_PAGE_LEN 24 + 4
#define MODE_SENSE10_CACHING_LEN                  32 + 4
#define MODE_SENSE10_INFORMATION_EXCEPTION_CONTROL_PAGE_LEN 24 + 4

#define MODE_SENSE10_RETURN_ALL_PAGES_LLBAA_LEN         68 + 4 + 8
#define MODE_SENSE10_CONTROL_PAGE_LLBAA_LEN             24 + 4 + 8
#define MODE_SENSE10_READ_WRITE_ERROR_RECOVERY_PAGE_LLBAA_LEN 24 + 4 + 8
#define MODE_SENSE10_CACHING_LLBAA_LEN                  32 + 4 + 8
#define MODE_SENSE10_INFORMATION_EXCEPTION_CONTROL_PAGE_LLBAA_LEN 24 + 4 + 8

/*****************************************************************************
** SCSI SENSE KEY VALUES
*****************************************************************************/

#define SCSI_SNSKEY_NO_SENSE           0x00
#define SCSI_SNSKEY_RECOVERED_ERROR    0x01
#define SCSI_SNSKEY_NOT_READY          0x02
#define SCSI_SNSKEY_MEDIUM_ERROR       0x03
#define SCSI_SNSKEY_HARDWARE_ERROR     0x04
#define SCSI_SNSKEY_ILLEGAL_REQUEST    0x05
#define SCSI_SNSKEY_UNIT_ATTENTION     0x06
#define SCSI_SNSKEY_DATA_PROTECT       0x07
#define SCSI_SNSKEY_ABORTED_COMMAND    0x0B
#define SCSI_SNSKEY_MISCOMPARE         0x0E

/*****************************************************************************
** SCSI Additional Sense Codes and Qualifiers combo two-bytes
*****************************************************************************/

#define SCSI_SNSCODE_NO_ADDITIONAL_INFO                         0x0000
#define SCSI_SNSCODE_LUN_CRC_ERROR_DETECTED                     0x0803
#define SCSI_SNSCODE_INVALID_COMMAND                            0x2000
#define SCSI_SNSCODE_LOGICAL_BLOCK_OUT                          0x2100
#define SCSI_SNSCODE_INVALID_FIELD_IN_CDB                       0x2400
#define SCSI_SNSCODE_LOGICAL_NOT_SUPPORTED                      0x2500
#define SCSI_SNSCODE_POWERON_RESET                              0x2900
#define SCSI_SNSCODE_EVERLAPPED_CMDS                            0x4e00
#define SCSI_SNSCODE_INTERNAL_TARGET_FAILURE                    0x4400
#define SCSI_SNSCODE_MEDIUM_NOT_PRESENT                         0x3a00
#define SCSI_SNSCODE_UNRECOVERED_READ_ERROR                     0x1100
#define SCSI_SNSCODE_RECORD_NOT_FOUND                           0x1401
#define SCSI_SNSCODE_NOT_READY_TO_READY_CHANGE                  0x2800
#define SCSI_SNSCODE_OPERATOR_MEDIUM_REMOVAL_REQUEST            0x5a01
#define SCSI_SNSCODE_INFORMATION_UNIT_CRC_ERROR                 0x4703
#define SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_FORMAT_IN_PROGRESS  0x0404
#define SCSI_SNSCODE_HARDWARE_IMPENDING_FAILURE                 0x5d10
#define SCSI_SNSCODE_LOW_POWER_CONDITION_ON                     0x5e00
#define SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_INIT_REQUIRED       0x0402
#define SCSI_SNSCODE_INVALID_FIELD_PARAMETER_LIST               0x2600
#define SCSI_SNSCODE_ATA_DEVICE_FAILED_SET_FEATURES             0x4471
#define SCSI_SNSCODE_ATA_DEVICE_FEATURE_NOT_ENABLED             0x670B
#define SCSI_SNSCODE_LOGICAL_UNIT_FAILED_SELF_TEST              0x3E03
#define SCSI_SNSCODE_COMMAND_SEQUENCE_ERROR                     0x2C00
#define SCSI_SNSCODE_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE         0x2100
#define SCSI_SNSCODE_LOGICAL_UNIT_FAILURE                       0x3E01
#define SCSI_SNSCODE_MEDIA_LOAD_OR_EJECT_FAILED                 0x5300
#define SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_INITIALIZING_COMMAND_REQUIRED 0x0402
#define SCSI_SNSCODE_LOGICAL_UNIT_NOT_READY_CAUSE_NOT_REPORTABLE          0x0400
#define SCSI_SNSCODE_LOGICAL_UNIT_DOES_NOT_RESPOND_TO_SELECTION           0x0500
#define SCSI_SNSCODE_DIAGNOSTIC_FAILURE_ON_COMPONENT_NN         0x4000
#define SCSI_SNSCODE_COMMANDS_CLEARED_BY_ANOTHER_INITIATOR      0x2F00
#define SCSI_SNSCODE_WRITE_ERROR_AUTO_REALLOCATION_FAILED       0x0C02
#define SCSI_SNSCODE_ATA_PASS_THROUGH_INFORMATION_AVAILABLE     0x001D

/*****************************************************************************
** SCSI Additional Sense Codes and Qualifiers saparate bytes
*****************************************************************************/

#define SCSI_ASC_NOTREADY_INIT_CMD_REQ    0x04
#define SCSI_ASCQ_NOTREADY_INIT_CMD_REQ   0x02


/*****************************************************************************
** Inquiry command fields and response sizes
*****************************************************************************/
#define SCSIOP_INQUIRY_CMDDT        0x02
#define SCSIOP_INQUIRY_EVPD         0x01
#define STANDARD_INQUIRY_SIZE       36
#define SATA_PAGE83_INQUIRY_WWN_SIZE       16      /* SAT, revision8, Table81, p78, 12 + 4 */
#define SATA_PAGE83_INQUIRY_NO_WWN_SIZE    76      /* SAT, revision8, Table81, p78, 72 + 4 */
#define SATA_PAGE89_INQUIRY_SIZE    572     /* SAT, revision8, Table87, p84 */
#define SATA_PAGE0_INQUIRY_SIZE     9       /* SPC-4, 7.6.9   Table331, p345 */
#define SATA_PAGE80_INQUIRY_SIZE    24     /* SAT, revision8, Table79, p77 */
#define SATA_PAGEB1_INQUIRY_SIZE    64     /* SBC-3, revision31, Table193, p273 */

/*****************************************************************************
** SCSI Operation Codes (first byte in CDB)
*****************************************************************************/


#define SCSIOPC_TEST_UNIT_READY     0x00
#define SCSIOPC_INQUIRY             0x12
#define SCSIOPC_MODE_SENSE_6        0x1A
#define SCSIOPC_MODE_SENSE_10       0x5A
#define SCSIOPC_MODE_SELECT_6       0x15
#define SCSIOPC_START_STOP_UNIT     0x1B
#define SCSIOPC_READ_CAPACITY_10    0x25
#define SCSIOPC_READ_CAPACITY_16    0x9E
#define SCSIOPC_READ_6              0x08
#define SCSIOPC_READ_10             0x28
#define SCSIOPC_READ_12             0xA8
#define SCSIOPC_READ_16             0x88
#define SCSIOPC_WRITE_6             0x0A 
#define SCSIOPC_WRITE_10            0x2A
#define SCSIOPC_WRITE_12            0xAA
#define SCSIOPC_WRITE_16            0x8A
#define SCSIOPC_WRITE_VERIFY        0x2E
#define SCSIOPC_VERIFY_10           0x2F
#define SCSIOPC_VERIFY_12           0xAF
#define SCSIOPC_VERIFY_16           0x8F
#define SCSIOPC_REQUEST_SENSE       0x03
#define SCSIOPC_REPORT_LUN          0xA0
#define SCSIOPC_FORMAT_UNIT         0x04
#define SCSIOPC_SEND_DIAGNOSTIC     0x1D
#define SCSIOPC_WRITE_SAME_10       0x41
#define SCSIOPC_WRITE_SAME_16       0x93
#define SCSIOPC_READ_BUFFER         0x3C
#define SCSIOPC_WRITE_BUFFER        0x3B

#define SCSIOPC_LOG_SENSE           0x4D
#define SCSIOPC_LOG_SELECT          0x4C
#define SCSIOPC_MODE_SELECT_6       0x15
#define SCSIOPC_MODE_SELECT_10      0x55
#define SCSIOPC_SYNCHRONIZE_CACHE_10 0x35
#define SCSIOPC_SYNCHRONIZE_CACHE_16 0x91
#define SCSIOPC_WRITE_AND_VERIFY_10 0x2E
#define SCSIOPC_WRITE_AND_VERIFY_12 0xAE
#define SCSIOPC_WRITE_AND_VERIFY_16 0x8E
#define SCSIOPC_READ_MEDIA_SERIAL_NUMBER 0xAB
#define SCSIOPC_REASSIGN_BLOCKS     0x07

#define SCSIOPC_GET_CONFIG          0x46
#define SCSIOPC_GET_EVENT_STATUS_NOTIFICATION        0x4a
#define SCSIOPC_REPORT_KEY          0xA4
#define SCSIOPC_SEND_KEY            0xA3
#define SCSIOPC_READ_DVD_STRUCTURE  0xAD
#define SCSIOPC_TOC                 0x43
#define SCSIOPC_PREVENT_ALLOW_MEDIUM_REMOVAL         0x1E
#define SCSIOPC_READ_VERIFY         0x42
#define SCSIOPC_ATA_PASS_THROUGH12	0xA1
#define SCSIOPC_ATA_PASS_THROUGH16	0x85


/*! \def MIN(a,b)
* \brief MIN macro
*
* use to find MIN of two values
*/
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

/*! \def MAX(a,b)
* \brief MAX macro
*
* use to find MAX of two values
*/
#ifndef MAX
#define MAX(a,b) ((a) < (b) ? (b) : (a))
#endif

/* for debugging print */
#if defined(SM_DEBUG)
  
/*
* for debugging purposes.  
*/
extern bit32 gSMDebugLevel;

#define SM_DBG0(format) tdsmLogDebugString(gSMDebugLevel, 0, format)
#define SM_DBG1(format) tdsmLogDebugString(gSMDebugLevel, 1, format)
#define SM_DBG2(format) tdsmLogDebugString(gSMDebugLevel, 2, format)
#define SM_DBG3(format) tdsmLogDebugString(gSMDebugLevel, 3, format)
#define SM_DBG4(format) tdsmLogDebugString(gSMDebugLevel, 4, format)
#define SM_DBG5(format) tdsmLogDebugString(gSMDebugLevel, 5, format)
#define SM_DBG6(format) tdsmLogDebugString(gSMDebugLevel, 6, format)

#else

#define SM_DBG0(format)
#define SM_DBG1(format)
#define SM_DBG2(format)
#define SM_DBG3(format)
#define SM_DBG4(format)
#define SM_DBG5(format)
#define SM_DBG6(format)

#endif /* SM_DEBUG */

//#define SM_ASSERT OS_ASSERT
//#define tdsmLogDebugString TIDEBUG_MSG

/*
 * SAT specific structure per SATA drive 
 */
#define SAT_NONNCQ_MAX  1
#define SAT_NCQ_MAX     32
#define SAT_MAX_INT_IO  16
#define SAT_APAPI_CMDQ_MAX 2

/* Device state */
#define SAT_DEV_STATE_NORMAL                  0  /* Normal */
#define SAT_DEV_STATE_IN_RECOVERY             1  /* SAT in recovery mode */
#define SAT_DEV_STATE_FORMAT_IN_PROGRESS      2  /* Format unit in progress */
#define SAT_DEV_STATE_SMART_THRESHOLD         3  /* SMART Threshold Exceeded Condition*/
#define SAT_DEV_STATE_LOW_POWER               4  /* Low Power State*/

#ifndef agNULL
#define agNULL     ((void *)0)
#endif

#define SM_SET_ESGL_EXTEND(val) \
 ((val) = (val) | 0x80000000)

#define SM_CLEAR_ESGL_EXTEND(val) \
 ((val) = (val) & 0x7FFFFFFF)

#ifndef OPEN_RETRY_RETRIES
#define OPEN_RETRY_RETRIES	10
#endif

/*********************************************************************
* CPU buffer access macro                                            *
*                                                                    *
*/

#define OSSA_OFFSET_OF(STRUCT_TYPE, FEILD)              \
        (bitptr)&(((STRUCT_TYPE *)0)->FEILD)
        

#if defined(SA_CPU_LITTLE_ENDIAN)

#define OSSA_WRITE_LE_16(AGROOT, DMA_ADDR, OFFSET, VALUE16)     \
        (*((bit16 *)(((bit8 *)DMA_ADDR)+(OFFSET)))) = (bit16)(VALUE16);

#define OSSA_WRITE_LE_32(AGROOT, DMA_ADDR, OFFSET, VALUE32)     \
        (*((bit32 *)(((bit8 *)DMA_ADDR)+(OFFSET)))) = (bit32)(VALUE32);

#define OSSA_READ_LE_16(AGROOT, ADDR16, DMA_ADDR, OFFSET)       \
        (*((bit16 *)ADDR16)) = (*((bit16 *)(((bit8 *)DMA_ADDR)+(OFFSET))))
    
#define OSSA_READ_LE_32(AGROOT, ADDR32, DMA_ADDR, OFFSET)       \
        (*((bit32 *)ADDR32)) = (*((bit32 *)(((bit8 *)DMA_ADDR)+(OFFSET))))

#define OSSA_WRITE_BE_16(AGROOT, DMA_ADDR, OFFSET, VALUE16)     \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))))   = (bit8)((((bit16)VALUE16)>>8)&0xFF);  \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))) = (bit8)(((bit16)VALUE16)&0xFF);
     
#define OSSA_WRITE_BE_32(AGROOT, DMA_ADDR, OFFSET, VALUE32)     \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))))   = (bit8)((((bit32)VALUE32)>>24)&0xFF); \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))) = (bit8)((((bit32)VALUE32)>>16)&0xFF); \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+2))) = (bit8)((((bit32)VALUE32)>>8)&0xFF);  \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+3))) = (bit8)(((bit32)VALUE32)&0xFF);

#define OSSA_READ_BE_16(AGROOT, ADDR16, DMA_ADDR, OFFSET)       \
        (*(bit8 *)(((bit8 *)ADDR16)+1)) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))));   \
        (*(bit8 *)(((bit8 *)ADDR16)))   = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1)));

#define OSSA_READ_BE_32(AGROOT, ADDR32, DMA_ADDR, OFFSET)       \
        (*(bit8 *)(((bit8 *)ADDR32)+3)) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))));   \
        (*(bit8 *)(((bit8 *)ADDR32)+2)) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))); \
        (*(bit8 *)(((bit8 *)ADDR32)+1)) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+2))); \
        (*(bit8 *)(((bit8 *)ADDR32)))   = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+3)));

#define OSSA_WRITE_BYTE_STRING(AGROOT, DEST_ADDR, SRC_ADDR, LEN)                        \
        si_memcpy(DEST_ADDR, SRC_ADDR, LEN);


#elif defined(SA_CPU_BIG_ENDIAN)

#define OSSA_WRITE_LE_16(AGROOT, DMA_ADDR, OFFSET, VALUE16)     \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))) = (bit8)((((bit16)VALUE16)>>8)&0xFF);   \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))))   = (bit8)(((bit16)VALUE16)&0xFF);

#define OSSA_WRITE_LE_32(AGROOT, DMA_ADDR, OFFSET, VALUE32)     \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+3))) = (bit8)((((bit32)VALUE32)>>24)&0xFF);  \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+2))) = (bit8)((((bit32)VALUE32)>>16)&0xFF);  \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))) = (bit8)((((bit32)VALUE32)>>8)&0xFF);   \
        (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))))   = (bit8)(((bit32)VALUE32)&0xFF);

#define OSSA_READ_LE_16(AGROOT, ADDR16, DMA_ADDR, OFFSET)       \
        (*(bit8 *)(((bit8 *)ADDR16)+1)) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))));   \
        (*(bit8 *)(((bit8 *)ADDR16)))   = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1)));

#define OSSA_READ_LE_32(AGROOT, ADDR32, DMA_ADDR, OFFSET)       \
        (*((bit8 *)(((bit8 *)ADDR32)+3))) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET))));   \
        (*((bit8 *)(((bit8 *)ADDR32)+2))) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+1))); \
        (*((bit8 *)(((bit8 *)ADDR32)+1))) = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+2))); \
        (*((bit8 *)(((bit8 *)ADDR32))))   = (*((bit8 *)(((bit8 *)DMA_ADDR)+(OFFSET)+3)));

#define OSSA_WRITE_BE_16(AGROOT, DMA_ADDR, OFFSET, VALUE16)         \
        (*((bit16 *)(((bit8 *)DMA_ADDR)+(OFFSET)))) = (bit16)(VALUE16);

#define OSSA_WRITE_BE_32(AGROOT, DMA_ADDR, OFFSET, VALUE32)         \
        (*((bit32 *)(((bit8 *)DMA_ADDR)+(OFFSET)))) = (bit32)(VALUE32);

#define OSSA_READ_BE_16(AGROOT, ADDR16, DMA_ADDR, OFFSET)           \
        (*((bit16 *)ADDR16)) = (*((bit16 *)(((bit8 *)DMA_ADDR)+(OFFSET))));

#define OSSA_READ_BE_32(AGROOT, ADDR32, DMA_ADDR, OFFSET)           \
        (*((bit32 *)ADDR32)) = (*((bit32 *)(((bit8 *)DMA_ADDR)+(OFFSET))));

#define OSSA_WRITE_BYTE_STRING(AGROOT, DEST_ADDR, SRC_ADDR, LEN)    \
        si_memcpy(DEST_ADDR, SRC_ADDR, LEN);

#else

#error (Host CPU endianess undefined!!)

#endif


#if defined(SA_CPU_LITTLE_ENDIAN)

#ifndef LEBIT16_TO_BIT16
#define LEBIT16_TO_BIT16(_x)   (_x)
#endif

#ifndef BIT16_TO_LEBIT16
#define BIT16_TO_LEBIT16(_x)   (_x)
#endif

#ifndef BIT16_TO_BEBIT16
#define BIT16_TO_BEBIT16(_x)   AGSA_FLIP_2_BYTES(_x)
#endif

#ifndef BEBIT16_TO_BIT16
#define BEBIT16_TO_BIT16(_x)   AGSA_FLIP_2_BYTES(_x)
#endif

#ifndef LEBIT32_TO_BIT32
#define LEBIT32_TO_BIT32(_x)   (_x)
#endif

#ifndef BIT32_TO_LEBIT32
#define BIT32_TO_LEBIT32(_x)   (_x)
#endif


#ifndef BEBIT32_TO_BIT32
#define BEBIT32_TO_BIT32(_x)   AGSA_FLIP_4_BYTES(_x)
#endif

#ifndef BIT32_TO_BEBIT32
#define BIT32_TO_BEBIT32(_x)   AGSA_FLIP_4_BYTES(_x)
#endif

#elif defined(SA_CPU_BIG_ENDIAN)

#ifndef LEBIT16_TO_BIT16
#define LEBIT16_TO_BIT16(_x)   AGSA_FLIP_2_BYTES(_x)
#endif

#ifndef BIT16_TO_LEBIT16
#define BIT16_TO_LEBIT16(_x)   AGSA_FLIP_2_BYTES(_x)
#endif

#ifndef BIT16_TO_BEBIT16
#define BIT16_TO_BEBIT16(_x)   (_x)
#endif

#ifndef BEBIT16_TO_BIT16
#define BEBIT16_TO_BIT16(_x)   (_x)
#endif

#ifndef LEBIT32_TO_BIT32
#define LEBIT32_TO_BIT32(_x)   AGSA_FLIP_4_BYTES(_x)
#endif

#ifndef BIT32_TO_LEBIT32
#define BIT32_TO_LEBIT32(_x)   AGSA_FLIP_4_BYTES(_x)
#endif

#ifndef BEBIT32_TO_BIT32
#define BEBIT32_TO_BIT32(_x)   (_x)
#endif

#ifndef BIT32_TO_BEBIT32
#define BIT32_TO_BEBIT32(_x)   (_x)
#endif

#else  

#error No definition of SA_CPU_BIG_ENDIAN or SA_CPU_LITTLE_ENDIAN

#endif 


/* 
 * Task Management task used in tiINITaskManagement()
 *
 * 1 SM_ABORT TASK - aborts the task identified by the Referenced  Task Tag field.
 * 2 SM_ABORT TASK SET - aborts all Tasks issued by this initiator on the Logical Unit 
 * 3 SM_CLEAR ACA - clears the Auto Contingent Allegiance condition.
 * 4 SM_CLEAR TASK SET - Aborts all Tasks (from all initiators) for the Logical Unit.
 * 5 SM_LOGICAL UNIT RESET 
 * 6 SM_TARGET WARM RESET  - iSCSI only
 * 7 SM_TARGET_COLD_RESET  - iSCSI only
 * 8 SM_TASK_REASSIGN      - iSCSI only
 * 9 SM_QUERY_TASK         - SAS only
 */

#define SM_ABORT_TASK          1
#define SM_ABORT_TASK_SET      2
#define SM_CLEAR_ACA           3
#define SM_CLEAR_TASK_SET      4
#define SM_LOGICAL_UNIT_RESET  5
#define SM_TARGET_WARM_RESET   6    /* iSCSI only */
#define SM_TARGET_COLD_RESET   7    /* iSCSI only */
#define SM_TASK_REASSIGN       8    /* iSCSI only */
#define SM_QUERY_TASK          9    /* SAS only   */

/* SMP PHY CONTROL OPERATION */
#define SMP_PHY_CONTROL_NOP                        0x00
#define SMP_PHY_CONTROL_LINK_RESET                 0x01
#define SMP_PHY_CONTROL_HARD_RESET                 0x02
#define SMP_PHY_CONTROL_DISABLE                    0x03
#define SMP_PHY_CONTROL_CLEAR_ERROR_LOG            0x05
#define SMP_PHY_CONTROL_CLEAR_AFFILIATION          0x06
#define SMP_PHY_CONTROL_XMIT_SATA_PS_SIGNAL        0x07

/****************************************************************
 *            Phy Control request
 ****************************************************************/
typedef struct smpReqPhyControl_s
{
  bit8   reserved1[4];
  bit8   reserved2;
  bit8   phyIdentifier;
  bit8   phyOperation;
  bit8   updatePartialPathwayTOValue;
    /* b7-1 : reserved */
    /* b0   : update partial pathway timeout value */
  bit8   reserved3[20];
  bit8   programmedMinPhysicalLinkRate;
    /* b7-4 : programmed Minimum Physical Link Rate*/
    /* b3-0 : reserved */
  bit8   programmedMaxPhysicalLinkRate;
    /* b7-4 : programmed Maximum Physical Link Rate*/
    /* b3-0 : reserved */
  bit8   reserved4[2];
  bit8   partialPathwayTOValue;
    /* b7-4 : reserved */
    /* b3-0 : partial Pathway TO Value */
  bit8   reserved5[3];
} smpReqPhyControl_t;


typedef struct smSMPFrameHeader_s
{
    bit8   smpFrameType;      /* The first byte of SMP frame represents the SMP FRAME TYPE */ 
    bit8   smpFunction;       /* The second byte of the SMP frame represents the SMP FUNCTION */
    bit8   smpFunctionResult; /* The third byte of SMP frame represents FUNCTION RESULT of the SMP response. */
    bit8   smpReserved;       /* reserved */
} smSMPFrameHeader_t;

/* SMP direct payload size limit: IOMB direct payload size = 48 */
#define SMP_DIRECT_PAYLOAD_LIMIT 44

#define SMP_REQUEST        0x40
#define SMP_RESPONSE       0x41

#define SMP_PHY_CONTROL                            0x91

/* SMP function results */
#define SMP_FUNCTION_ACCEPTED                      0x00

/* bit8 array[4] -> bit32 */
#define SM_GET_SAS_ADDRESSLO(sasAddressLo)                  \
    DMA_BEBIT32_TO_BIT32(*(bit32 *)sasAddressLo)

#define SM_GET_SAS_ADDRESSHI(sasAddressHi)                  \
    DMA_BEBIT32_TO_BIT32(*(bit32 *)sasAddressHi)

/* SATA sector size 512 bytes = 0x200 bytes */
#define SATA_SECTOR_SIZE                          0x200
/* TL limit in sector */
/* for SAT_READ/WRITE_DMA and SAT_READ/WRITE_SECTORS ATA command */
#define NON_BIT48_ADDRESS_TL_LIMIT                0x100
/* for SAT_READ/WRITE_DMA_EXT and SAT_READ/WRITE_SECTORS_EXT and  SAT_READ/WRITE_FPDMA_QUEUEDATA command */
#define BIT48_ADDRESS_TL_LIMIT                    0xFFFF

#define VEN_DEV_SPC                               0x800111f8
#define VEN_DEV_SPCv                              0x800811f8
#define VEN_DEV_SPCve                             0x800911f8
#define VEN_DEV_SPCvplus                          0x801811f8
#define VEN_DEV_SPCveplus                         0x801911f8

#define SMIsSPC(agr) (VEN_DEV_SPC  == ossaHwRegReadConfig32(agr,0 ) ? 1 : 0) /* returns true config space read is SPC */
#define SMIsSPCv(agr)  (VEN_DEV_SPCv == ossaHwRegReadConfig32(agr,0 ) ? 1 : 0) /* returns true config space read is SPCv */
#define SMIsSPCve(agr) (VEN_DEV_SPCve  == ossaHwRegReadConfig32(agr,0 ) ? 1 : 0) /* returns true config space read is SPCve */
#define SMIsSPCvplus(agr)  (VEN_DEV_SPCvplus == ossaHwRegReadConfig32(agr,0 ) ? 1 : 0) /* returns true config space read is SPCv+ */
#define SMIsSPCveplus(agr)  (VEN_DEV_SPCveplus == ossaHwRegReadConfig32(agr,0 ) ? 1 : 0) /* returns true config space read is SPCve+ */

#define DEFAULT_KEY_BUFFER_SIZE     64


#endif /* __SMDEFS_H__ */

