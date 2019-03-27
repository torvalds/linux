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
/*******************************************************************************/
/** \file
 *
 *
 * The file defines the constants, data structure, and functions defined by SAT
 * layer.
 *
 */

#ifndef  __SAT_H__
#define __SAT_H__

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
typedef struct scsiReportLun_s
{
  bit8              len[4];
  bit32             reserved;
  tiLUN_t           lunList[1];
} scsiReportLun_t;

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
 * Flag definition for satIntFlag field in satInternalIo_t.
 */

/* Original NCQ I/O already completed, so at the completion of READ LOG EXT
 *  page 10h, ignore the TAG tranaltion to get the failed I/O
 */
#define AG_SAT_INT_IO_FLAG_ORG_IO_COMPLETED   0x00000001

#define INQUIRY_SUPPORTED_VPD_PAGE             0x00
#define INQUIRY_UNIT_SERIAL_NUMBER_VPD_PAGE    0x80
#define INQUIRY_DEVICE_IDENTIFICATION_VPD_PAGE 0x83
#define INQUIRY_ATA_INFORMATION_VPD_PAGE       0x89

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

#endif  /*__SAT_H__ */
