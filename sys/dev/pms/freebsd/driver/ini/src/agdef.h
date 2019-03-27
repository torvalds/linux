/*******************************************************************************
 **
 **
*Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
 *
*Redistribution and use in source and binary forms, with or without modification, are permitted provided 
*that the following conditions are met: 
*1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer. 
*2. Redistributions in binary form must reproduce the above copyright notice, 
*this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution. 
*
*THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
*
*INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
*ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
*SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
*OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
*WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
*THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
*
* $FreeBSD$
*
**
*******************************************************************************/
 /******************************************************************************
Note:
*******************************************************************************
Module Name:  
  agdef.h
Abstract:  
  Linux iSCSI/FC Initiator driver module constant define header file
Authors:  
  EW - Yiding(Eddie) Wang
Environment:  
  Kernel or loadable module  

Version Control Information:  
  $ver. 1.0.0
    
Revision History:
  $Revision: 115514 $0.1.0
  $Date: 2012-01-06 17:12:27 -0800 (Fri, 06 Jan 2012) $09-27-2001
  $Modtime: 11/12/01 11:15a $15:56:00

Notes:
**************************** MODIFICATION HISTORY ***************************** 
NAME     DATE         Rev.          DESCRIPTION
----     ----         ----          -----------
EW     09-17-2004     1.0.0     Constant definitions
******************************************************************************/


#ifndef __AGTIAPI_DEF_H__
#define __AGTIAPI_DEF_H__

/*
** Max device supported
*/
#define AGTIAPI_HW_LIMIT_DEVICE     4096
#define AGTIAPI_MAX_LUN             256    /* Max # luns per target */
#define AGTIAPI_MAX_DEVICE          128 //64 //2048//1024 /* Max # device per channel */
#define AGTIAPI_MAX_DEVICE_7H       256 /*Max devices per channel in 7H */
#define AGTIAPI_MAX_DEVICE_8H       512 /*Max devices per channel in 8H*/
#define AGTIAPI_MAX_CAM_Q_DEPTH     1024
#define AGTIAPI_NSEGS               (MAXPHYS / PAGE_SIZE)
/*
** Adapter specific defines 
*/
#define AGTIAPI_IO_RANGE  256      /* IO mapped address range */

/*
**  Scatter/Gather DMA Segment Descriptor
**  Note, MAX_Q_DEPTH could be set larger for iscsi "AcceptQueueSize"
**  parameter matching.  One thing to do is to make it to be an adjustable 
**  parameter.  Currently suggest this value set to be same as 
**  "AcceptQueueSize" but not required.  
*/

#define AGTIAPI_MAX_DMA_SEGS     128//256 
#define AGTIAPI_DEFAULT_Q_DEPTH  4
#define AGTIAPI_MAX_Q_DEPTH      AGSA_MAX_INBOUND_Q * 512 // *INBOUND_DEPTH_SIZE 

/*
** CCB and device flags defines
*/
#define ACTIVE           0x00000001
#define TIMEDOUT         0x00000002
#define REQ_DONE         0x00000004
#define AGTIAPI_INQUIRY  0x00000008
#define AGTIAPI_ABORT    0x00000010
#define AGTIAPI_RETRY    0x00000020
#define TASK_SUCCESS     0x00000040
/* reserved for card flag
#define AGTIAPI_RESERVED 0x00000080  
*/
#define AGTIAPI_CNX_UP   0x00000100
#define DEV_RESET        0x00000400    /* device reset */
#define DEV_SHIFT        0x00000800    /* device shift physical position */
#define AGTIAPI_YAM      0x00001000
#define TASK_TIMEOUT     0x00002000
#define ENCRYPTED_IO     0x00010000    /* encrypted IO */
#define SATA_DIF         0x00020000    /* SATA DIF */
#define EDC_DATA         0x00040000
#define EDC_DATA_CRC     0x00080000
#define TAG_SMP          0x40000000
#define TASK_MANAGEMENT  0x80000000

#define AGTIAPI_CCB_PER_DEVICE  64  
#define AGTIAPI_CMD_PER_LUN     512 

/*
** Max time to call agtiapi_GetDevHandle
** to make sure that no devices are attached
*/
#define AGTIAPI_GET_DEV_MAX  2

/*
** Device address mode
*/
#define AGTIAPI_ADDRMODE_SHIFT  6
#define AGTIAPI_PERIPHERAL   0x00
#define AGTIAPI_VOLUME_SET   0x01
#define AGTIAPI_LUN_ADDR     0x02

/*      
** Device mapping method
*/      
#define SOFT_MAPPED        0x0001
#define HARD_MAPPED        0x0002

/*
** bd_dev_type definitions
*/
#define DIRECT_DEVICE        0x00
#define TAPE_DEVICE          0x01
#define SLOW_DEVICE          0x02
#define ARRAY_DEVICE         0x04

/* 
** SCSI CDB  
*/
#define SCSI_CDB_SIZE        16

/* 
** SCSI status  
*/
#define SCSI_GOOD                   0x00
#define SCSI_CHECK_CONDITION        0x02
#define SCSI_CONDITION_MET          0x04
#define SCSI_BUSY                   0x08
#define SCSI_INTERMEDIATE           0x10
#define SCSI_INTERMEDIATE_COND_MET  0x14
#define SCSI_RESERVATION_CONFLICT   0x18
#define SCSI_TASK_ABORTED           0x40
#define SCSI_TASK_SET_FULL          0x28
#define SCSI_ACA_ACTIVE             0x30

/*
** Peripheral device types
*/
#define DTYPE_DIRECT         0x00
#define DTYPE_SEQUENTIAL     0x01
#define DTYPE_PRINTER        0x02
#define DTYPE_PROCESSOR      0x03
#define DTYPE_WORM           0x04
#define DTYPE_RODIRECT       0x05
#define DTYPE_SCANNER        0x06
#define DTYPE_OPTICAL        0x07
#define DTYPE_CHANGER        0x08
#define DTYPE_COMM           0x09
#define DTYPE_ARRAY_CTRL     0x0C
#define DTYPE_ESI            0x0D
/*
** Device types 0x0E-0x1E are reserved
*/
#define DTYPE_MASK           0x1F

/*
** Driver capability defines
*/
#define AGTIAPI_TIMEOUT_SECS        10            /* Default timer interval */
#define AGTIAPI_RESET_MAX           0x7FFFFFFF    /* Default max. reset */
#define AGTIAPI_DEV_RESET_MAX       0x10          /* Default max. reset */
#define AGTIAPI_RETRY_MAX           10            /* Default ccb retry cnt */
#define AGTIAPI_MAX_CHANNEL_NUM     0             /* Max channel # per card */
#define AGTIAPI_PERIPHERAL_CHANNEL  0 
#define AGTIAPI_VOLUMESET_CHANNEL   1
#define AGTIAPI_LUNADDR_CHANNEL     2
#define AGTIAPI_EXTRA_DELAY         10000         /* extra 10 seconds delay */

/*
** Scsi ioctl test case only
*/
#define AGTIAPI_TEST_ABORT          0xabcd
#define AGTIAPI_TEST_ABORT_DONE     0xabce
#define AGTIAPI_IOCTL_SIGNATURE     "AGTIAPI_IOCTL"

#define AGTIAPI_HBA_SCSI_ID         (AGTIAPI_MAX_DEVICE - 1)
#define AGTIAPI_NO_RESEND           0x01   /* Don't resend command */
#define AGTIAPI_RESEND              0x02   /* Resend command */
//#define AGTIAPI_UPPER               0x04   /* Call from upper layer */
#define AGTIAPI_CALLBACK            0x08   /* CMD call back required */

#endif  /* __AGTIAPI_DEF_H__ */
