/* $FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997 Marc Horowitz.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Marc Horowitz.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * CIS Tuples */

/* Layer 1 Basic Compatibility Tuples */
#define	CISTPL_NULL			0x00
#define	CISTPL_DEVICE			0x01
#define	PCCARD_DTYPE_MASK			0xF0
#define	PCCARD_DTYPE_NULL				0x00
#define	PCCARD_DTYPE_ROM				0x10
#define	PCCARD_DTYPE_OTPROM				0x20
#define	PCCARD_DTYPE_EPROM				0x30
#define	PCCARD_DTYPE_EEPROM				0x40
#define	PCCARD_DTYPE_FLASH				0x50
#define	PCCARD_DTYPE_SRAM				0x60
#define	PCCARD_DTYPE_DRAM				0x70
#define	PCCARD_DTYPE_FUNCSPEC				0xD0
#define	PCCARD_DTYPE_EXTEND				0xE0
#define	PCCARD_DSPEED_MASK			0x07
#define	PCCARD_DSPEED_NULL				0x00
#define	PCCARD_DSPEED_250NS				0x01
#define	PCCARD_DSPEED_200NS				0x02
#define	PCCARD_DSPEED_150NS				0x03
#define	PCCARD_DSPEED_100NS				0x04
#define	PCCARD_DSPEED_EXT				0x07

/*
 * the 2.1 docs have 0x02-0x07 as reserved, but the linux drivers list the
 * follwing tuple code values.  I have at least one card (3com 3c562
 * lan+modem) which has a code 0x06 tuple, so I'm going to assume that these
 * are for real
 */

#define	CISTPL_LONGLINK_CB		0x02
#define	CISTPL_INDIRECT			0x03
#define	CISTPL_CONFIG_CB		0x04
#define	CISTPL_CFTABLE_ENTRY_CB		0x05
#define	CISTPL_LONGLINK_MFC		0x06
#define	PCCARD_MFC_MEM_ATTR				0x00
#define	PCCARD_MFC_MEM_COMMON				0x01
#define	CISTPL_BAR			0x07
#define	CISTPL_PWR_MGMNT		0x08
#define CISTPL_EXTDEVICE		0x09

#define	CISTPL_CHECKSUM			0x10
#define	CISTPL_LONGLINK_A		0x11
#define	CISTPL_LONGLINK_C		0x12
#define	CISTPL_LINKTARGET		0x13
#define	CISTPL_NO_LINK			0x14
#define	CISTPL_VERS_1			0x15
#define	CISTPL_ALTSTR			0x16
#define	CISTPL_DEVICE_A			0x17
#define	CISTPL_JEDEC_C			0x18
#define	CISTPL_JEDEC_A			0x19
#define	CISTPL_CONFIG			0x1A
#define	PCCARD_TPCC_RASZ_MASK				0x03
#define	PCCARD_TPCC_RASZ_SHIFT				0
#define	PCCARD_TPCC_RMSZ_MASK				0x3C
#define	PCCARD_TPCC_RMSZ_SHIFT				2
#define	PCCARD_TPCC_RFSZ_MASK				0xC0
#define	PCCARD_TPCC_RFSZ_SHIFT				6
#define	CISTPL_CFTABLE_ENTRY		0x1B
#define	PCCARD_TPCE_INDX_INTFACE			0x80
#define	PCCARD_TPCE_INDX_DEFAULT			0x40
#define	PCCARD_TPCE_INDX_NUM_MASK			0x3F
#define	PCCARD_TPCE_IF_MWAIT				0x80
#define	PCCARD_TPCE_IF_RDYBSY				0x40
#define	PCCARD_TPCE_IF_WP				0x20
#define	PCCARD_TPCE_IF_BVD				0x10
#define	PCCARD_TPCE_IF_IFTYPE				0x0F
#define	PCCARD_IFTYPE_MEMORY					0
#define	PCCARD_IFTYPE_IO					1
#define	PCCARD_TPCE_FS_MISC				0x80
#define	PCCARD_TPCE_FS_MEMSPACE_MASK			0x60
#define	PCCARD_TPCE_FS_MEMSPACE_NONE				0x00
#define	PCCARD_TPCE_FS_MEMSPACE_LENGTH				0x20
#define	PCCARD_TPCE_FS_MEMSPACE_LENGTHADDR			0x40
#define	PCCARD_TPCE_FS_MEMSPACE_TABLE				0x60
#define	PCCARD_TPCE_FS_IRQ				0x10
#define	PCCARD_TPCE_FS_IOSPACE				0x08
#define	PCCARD_TPCE_FS_TIMING				0x04
#define	PCCARD_TPCE_FS_POWER_MASK			0x03
#define	PCCARD_TPCE_FS_POWER_NONE				0x00
#define	PCCARD_TPCE_FS_POWER_VCC				0x01
#define	PCCARD_TPCE_FS_POWER_VCCVPP1				0x02
#define	PCCARD_TPCE_FS_POWER_VCCVPP1VPP2			0x03
#define	PCCARD_TPCE_TD_RESERVED_MASK			0xE0
#define	PCCARD_TPCE_TD_RDYBSY_MASK			0x1C
#define	PCCARD_TPCE_TD_WAIT_MASK			0x03
#define	PCCARD_TPCE_IO_HASRANGE				0x80
#define	PCCARD_TPCE_IO_BUSWIDTH_16BIT			0x40
#define	PCCARD_TPCE_IO_BUSWIDTH_8BIT			0x20
#define	PCCARD_TPCE_IO_IOADDRLINES_MASK			0x1F
#define	PCCARD_TPCE_IO_RANGE_LENGTHSIZE_MASK		0xC0
#define	PCCARD_TPCE_IO_RANGE_LENGTHSIZE_NONE			0x00
#define	PCCARD_TPCE_IO_RANGE_LENGTHSIZE_ONE			0x40
#define	PCCARD_TPCE_IO_RANGE_LENGTHSIZE_TWO			0x80
#define	PCCARD_TPCE_IO_RANGE_LENGTHSIZE_FOUR			0xC0
#define	PCCARD_TPCE_IO_RANGE_ADDRSIZE_MASK		0x30
#define	PCCARD_TPCE_IO_RANGE_ADDRSIZE_NONE			0x00
#define	PCCARD_TPCE_IO_RANGE_ADDRSIZE_ONE			0x10
#define	PCCARD_TPCE_IO_RANGE_ADDRSIZE_TWO			0x20
#define	PCCARD_TPCE_IO_RANGE_ADDRSIZE_FOUR			0x30
#define	PCCARD_TPCE_IO_RANGE_COUNT			0x0F
#define	PCCARD_TPCE_IR_SHARE				0x80
#define	PCCARD_TPCE_IR_PULSE				0x40
#define	PCCARD_TPCE_IR_LEVEL				0x20
#define	PCCARD_TPCE_IR_HASMASK				0x10
#define	PCCARD_TPCE_IR_IRQ				0x0F
#define	PCCARD_TPCE_MS_HOSTADDR				0x80
#define	PCCARD_TPCE_MS_CARDADDR_SIZE_MASK		0x60
#define	PCCARD_TPCE_MS_CARDADDR_SIZE_SHIFT		5
#define	PCCARD_TPCE_MS_LENGTH_SIZE_MASK			0x18
#define	PCCARD_TPCE_MS_LENGTH_SIZE_SHIFT		3
#define	PCCARD_TPCE_MS_COUNT				0x07
#define	PCCARD_TPCE_MI_EXT				0x80
#define	PCCARD_TPCE_MI_RESERVED				0x40
#define	PCCARD_TPCE_MI_PWRDOWN				0x20
#define	PCCARD_TPCE_MI_READONLY				0x10
#define	PCCARD_TPCE_MI_AUDIO				0x08
#define	PCCARD_TPCE_MI_MAXTWINS				0x07
#define	CISTPL_DEVICE_OC			0x1C
#define	CISTPL_DEVICE_OA			0x1D
#define	CISTPL_DEVICE_GEO			0x1E
#define	CISTPL_DEVICE_GEO_A			0x1F
#define	CISTPL_MANFID				0x20
#define	CISTPL_FUNCID				0x21
#define	PCCARD_FUNCTION_UNSPEC		-1
#define	PCCARD_FUNCTION_MULTIFUNCTION	0
#define	PCCARD_FUNCTION_MEMORY		1
#define	PCCARD_FUNCTION_SERIAL		2
#define	PCCARD_FUNCTION_PARALLEL	3
#define	PCCARD_FUNCTION_DISK		4
#define	PCCARD_FUNCTION_VIDEO		5
#define	PCCARD_FUNCTION_NETWORK		6
#define	PCCARD_FUNCTION_AIMS		7
#define	PCCARD_FUNCTION_SCSI		8
#define	PCCARD_FUNCTION_SECURITY	9
#define	PCCARD_FUNCTION_INSTRUMENT	10
#define CISTPL_FUNCE				0x22
#define	PCCARD_TPLFE_TYPE_LAN_TECH			0x01
#define	PCCARD_TPLFE_TYPE_LAN_SPEED			0x02
#define	PCCARD_TPLFE_TYPE_LAN_MEDIA			0x03
#define	PCCARD_TPLFE_TYPE_LAN_NID			0x04
#define	PCCARD_TPLFE_TYPE_LAN_CONN			0x05
#define	PCCARD_TPLFE_TYPE_DISK_DEVICE_INTERFACE		0x01
#define	PCCARD_TPLFE_DDI_PCCARD_ATA				0x01
#define	CISTPL_END				0xFF

/* Layer 2 Data Recording Format Tuples */

#define	CISTPL_SWIL				0x23
/* #define	CISTPL_RESERVED		0x24-0x3F */
#define	CISTPL_VERS_2				0x40
#define	CISTPL_FORMAT				0x41
#define	CISTPL_GEOMETRY				0x42
#define	CISTPL_BYTEORDER			0x43
#define	CISTPL_DATE				0x44
#define	CISTPL_BATTERY				0x45
#define	CISTPL_FORAMT_A				0x47

/* Layer 3 Data Organization Tuples */

#define	CISTPL_ORG				0x46
/* #define	CISTPL_RESERVED		0x47-0x7F */

/* Layer 4 System-Specific Standard Tuples */

/* #define	CISTPL_RESERVED		0x80-0x8F */
#define	CISTPL_SPCL				0x90
/* #define	CISTPL_RESERVED		0x90-0xFE */

#define CISTPL_GENERIC		-1
