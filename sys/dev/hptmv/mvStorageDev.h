/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005 MARVELL SEMICONDUCTOR ISRAEL, LTD.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef __INCmvStorageDevh
#define __INCmvStorageDevh

/* Definitions */

/* ATA register on the ATA drive*/

#define MV_EDMA_ATA_FEATURES_ADDR				0x11
#define MV_EDMA_ATA_SECTOR_COUNT_ADDR			0x12
#define MV_EDMA_ATA_LBA_LOW_ADDR				0x13
#define MV_EDMA_ATA_LBA_MID_ADDR				0x14
#define MV_EDMA_ATA_LBA_HIGH_ADDR				0x15
#define MV_EDMA_ATA_DEVICE_ADDR					0x16
#define MV_EDMA_ATA_COMMAND_ADDR				0x17

#define MV_ATA_ERROR_STATUS						0x00000001 /* MV_BIT0 */
#define MV_ATA_DATA_REQUEST_STATUS				0x00000008 /* MV_BIT3 */
#define MV_ATA_SERVICE_STATUS					0x00000010 /* MV_BIT4 */
#define MV_ATA_DEVICE_FAULT_STATUS				0x00000020 /* MV_BIT5 */
#define MV_ATA_READY_STATUS						0x00000040 /* MV_BIT6 */
#define MV_ATA_BUSY_STATUS						0x00000080 /* MV_BIT7 */


#define MV_ATA_COMMAND_READ_SECTORS				0x20
#define MV_ATA_COMMAND_READ_SECTORS_EXT         0x24
#define MV_ATA_COMMAND_READ_VERIFY_SECTORS		0x40
#define MV_ATA_COMMAND_READ_VERIFY_SECTORS_EXT	0x42
#define MV_ATA_COMMAND_READ_BUFFER				0xE4
#define MV_ATA_COMMAND_WRITE_BUFFER             0xE8
#define MV_ATA_COMMAND_WRITE_SECTORS			0x30
#define MV_ATA_COMMAND_WRITE_SECTORS_EXT        0x34
#define MV_ATA_COMMAND_DIAGNOSTIC				0x90
#define MV_ATA_COMMAND_SMART                    0xb0
#define MV_ATA_COMMAND_READ_MULTIPLE            0xc4
#define MV_ATA_COMMAND_WRITE_MULTIPLE           0xc5
#define MV_ATA_COMMAND_STANDBY_IMMEDIATE		0xe0
#define MV_ATA_COMMAND_IDLE_IMMEDIATE			0xe1
#define MV_ATA_COMMAND_STANDBY					0xe2
#define MV_ATA_COMMAND_IDLE						0xe3
#define MV_ATA_COMMAND_SLEEP					0xe6
#define MV_ATA_COMMAND_IDENTIFY					0xec
#define MV_ATA_COMMAND_DEVICE_CONFIG            0xb1
#define MV_ATA_COMMAND_SET_FEATURES				0xef	
#define MV_ATA_COMMAND_WRITE_DMA				0xca
#define MV_ATA_COMMAND_WRITE_DMA_EXT			0x35
#define MV_ATA_COMMAND_WRITE_DMA_QUEUED			0xcc
#define MV_ATA_COMMAND_WRITE_DMA_QUEUED_EXT		0x36
#define MV_ATA_COMMAND_WRITE_FPDMA_QUEUED_EXT   0x61
#define MV_ATA_COMMAND_READ_DMA					0xc8
#define MV_ATA_COMMAND_READ_DMA_EXT				0x25
#define MV_ATA_COMMAND_READ_DMA_QUEUED			0xc7
#define MV_ATA_COMMAND_READ_DMA_QUEUED_EXT		0x26
#define MV_ATA_COMMAND_READ_FPDMA_QUEUED_EXT    0x60
#define MV_ATA_COMMAND_FLUSH_CACHE              0xe7
#define MV_ATA_COMMAND_FLUSH_CACHE_EXT          0xea


#define MV_ATA_SET_FEATURES_DISABLE_8_BIT_PIO	0x01 
#define MV_ATA_SET_FEATURES_ENABLE_WCACHE		0x02  /* Enable write cache */
#define MV_ATA_SET_FEATURES_TRANSFER        	0x03  /* Set transfer mode	*/
#define MV_ATA_TRANSFER_UDMA_0     		        0x40
#define MV_ATA_TRANSFER_UDMA_1     		        0x41
#define MV_ATA_TRANSFER_UDMA_2     		        0x42
#define MV_ATA_TRANSFER_UDMA_3     		        0x43
#define MV_ATA_TRANSFER_UDMA_4     		        0x44
#define MV_ATA_TRANSFER_UDMA_5     		        0x45
#define MV_ATA_TRANSFER_UDMA_6     		        0x46
#define MV_ATA_TRANSFER_UDMA_7     		        0x47
#define MV_ATA_TRANSFER_PIO_SLOW           		0x00
#define MV_ATA_TRANSFER_PIO_0              		0x08
#define MV_ATA_TRANSFER_PIO_1              		0x09
#define MV_ATA_TRANSFER_PIO_2              		0x0A
#define MV_ATA_TRANSFER_PIO_3              		0x0B
#define MV_ATA_TRANSFER_PIO_4              		0x0C
/* Enable advanced power management */
#define MV_ATA_SET_FEATURES_ENABLE_APM			0x05 
/* Disable media status notification*/
#define MV_ATA_SET_FEATURES_DISABLE_MSN			0x31 
/* Disable read look-ahead		    */
#define MV_ATA_SET_FEATURES_DISABLE_RLA			0x55 
/* Enable release interrupt		    */
#define MV_ATA_SET_FEATURES_ENABLE_RI			0x5D 
/* Enable SERVICE interrupt		    */
#define MV_ATA_SET_FEATURES_ENABLE_SI			0x5E 
/* Disable revert power-on defaults */
#define MV_ATA_SET_FEATURES_DISABLE_RPOD		0x66 
/* Disable write cache			    */
#define MV_ATA_SET_FEATURES_DISABLE_WCACHE		0x82  
/* Disable advanced power management*/  	
#define MV_ATA_SET_FEATURES_DISABLE_APM			0x85 
/* Enable media status notification */
#define MV_ATA_SET_FEATURES_ENABLE_MSN			0x95 
/* Enable read look-ahead		    */
#define MV_ATA_SET_FEATURES_ENABLE_RLA			0xAA 
/* Enable revert power-on defaults  */
#define MV_ATA_SET_FEATURES_ENABLE_RPOD			0xCC 
/* Disable release interrupt	    */
#define MV_ATA_SET_FEATURES_DISABLE_RI			0xDD
/* Disable SERVICE interrupt	    */
#define MV_ATA_SET_FEATURES_DISABLE_SI			0xDE 

/* Defines for parsing the IDENTIFY command results*/
#define IDEN_SERIAL_NUM_OFFSET 					10
#define IDEN_SERIAL_NUM_SIZE   					19-10
#define IDEN_FIRMWARE_OFFSET 					23
#define IDEN_FIRMWARE_SIZE  					26-23
#define IDEN_MODEL_OFFSET   					27
#define IDEN_MODEL_SIZE     					46-27
#define IDEN_CAPACITY_1_OFFSET  				49
#define IDEN_VALID								53
#define IDEN_NUM_OF_ADDRESSABLE_SECTORS			60
#define	IDEN_PIO_MODE_SPPORTED					64
#define IDEN_QUEUE_DEPTH						75
#define IDEN_SATA_CAPABILITIES                  76
#define IDEN_SATA_FEATURES_SUPPORTED            78
#define IDEN_SATA_FEATURES_ENABLED              79
#define IDEN_ATA_VERSION						80
#define IDEN_SUPPORTED_COMMANDS1				82
#define IDEN_SUPPORTED_COMMANDS2				83
#define IDEN_ENABLED_COMMANDS1					85
#define IDEN_ENABLED_COMMANDS2					86
#define IDEN_UDMA_MODE							88
#define IDEN_SATA_CAPABILITY					76


/* Typedefs    */

/* Structures  */
typedef struct mvStorageDevRegisters 
{
    /* Fields set by CORE driver */
    MV_U8    errorRegister;
    MV_U16   sectorCountRegister;
    MV_U16   lbaLowRegister;
    MV_U16   lbaMidRegister;
    MV_U16   lbaHighRegister;
    MV_U8    deviceRegister;
    MV_U8    statusRegister;
} MV_STORAGE_DEVICE_REGISTERS;

/* Bits for HD_ERROR */
#define NM_ERR			0x02	/* media present */
#define ABRT_ERR		0x04	/* Command aborted */
#define MCR_ERR         0x08	/* media change request */
#define IDNF_ERR        0x10	/* ID field not found */
#define MC_ERR          0x20	/* media changed */
#define UNC_ERR         0x40	/* Uncorrect data */
#define WP_ERR          0x40	/* write protect */
#define ICRC_ERR        0x80	/* new meaning:  CRC error during transfer */

/* Function */

MV_BOOLEAN HPTLIBAPI mvStorageDevATAExecuteNonUDMACommand(MV_SATA_ADAPTER *pAdapter,
												MV_U8 channelIndex,
											MV_NON_UDMA_PROTOCOL protocolType,
												MV_BOOLEAN  isEXT,
												MV_U16 FAR *bufPtr, MV_U32 count,
												MV_U16 features, 
												MV_U16 sectorCount,
												MV_U16 lbaLow, MV_U16 lbaMid,
												MV_U16 lbaHigh, MV_U8 device,
												MV_U8 command);

MV_BOOLEAN HPTLIBAPI mvStorageDevATAIdentifyDevice(MV_SATA_ADAPTER *pAdapter,
										 MV_U8 channelIndex);

MV_BOOLEAN HPTLIBAPI mvStorageDevATASetFeatures(MV_SATA_ADAPTER *pAdapter,
									  MV_U8 channelIndex, MV_U8 subCommand,
									  MV_U8 subCommandSpecific1,
									  MV_U8 subCommandSpecific2,
									  MV_U8 subCommandSpecific3,
									  MV_U8 subCommandSpecific4);

MV_BOOLEAN HPTLIBAPI mvStorageDevATAIdleImmediate(MV_SATA_ADAPTER *pAdapter,
										MV_U8 channelIndex);

MV_BOOLEAN HPTLIBAPI mvStorageDevATAFlushWriteCache(MV_SATA_ADAPTER *pAdapter,
										  MV_U8 channelIndex);

MV_BOOLEAN HPTLIBAPI mvStorageDevATASoftResetDevice(MV_SATA_ADAPTER *pAdapter,
										  MV_U8 channelIndex);

MV_BOOLEAN HPTLIBAPI mvStorageDevWaitStat(MV_SATA_CHANNEL *pSataChannel,
								MV_U8 good, MV_U8 bad, MV_U32 loops, MV_U32 delay);

MV_BOOLEAN HPTLIBAPI mvReadWrite(MV_SATA_CHANNEL *pSataChannel, LBA_T Lba, UCHAR Cmd, void *tmpBuffer);

#endif
