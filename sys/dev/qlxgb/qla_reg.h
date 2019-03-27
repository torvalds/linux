/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011-2013 Qlogic Corporation
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
/*
 * File: qla_reg.h
 * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
 */

#ifndef _QLA_REG_H_
#define _QLA_REG_H_

/*
 * Begin Definitions for QLA82xx Registers
 */

/*
 * Register offsets for QLA8022
 */

/******************************
 * PCIe Registers
 ******************************/
#define Q8_CRB_WINDOW_2M		0x130060

#define Q8_INT_VECTOR			0x130100
#define Q8_INT_MASK			0x130104

#define Q8_INT_TARGET_STATUS_F0		0x130118
#define Q8_INT_TARGET_MASK_F0		0x130128
#define Q8_INT_TARGET_STATUS_F1		0x130160
#define Q8_INT_TARGET_MASK_F1		0x130170
#define Q8_INT_TARGET_STATUS_F2		0x130164
#define Q8_INT_TARGET_MASK_F2		0x130174
#define Q8_INT_TARGET_STATUS_F3		0x130168
#define Q8_INT_TARGET_MASK_F3		0x130178
#define Q8_INT_TARGET_STATUS_F4		0x130360
#define Q8_INT_TARGET_MASK_F4		0x130370
#define Q8_INT_TARGET_STATUS_F5		0x130364
#define Q8_INT_TARGET_MASK_F5		0x130374
#define Q8_INT_TARGET_STATUS_F6		0x130368
#define Q8_INT_TARGET_MASK_F6		0x130378
#define Q8_INT_TARGET_STATUS_F7		0x13036C
#define Q8_INT_TARGET_MASK_F7		0x13037C

#define Q8_SEM2_LOCK			0x13C010
#define Q8_SEM2_UNLOCK			0x13C014
#define Q8_SEM3_LOCK			0x13C018
#define Q8_SEM3_UNLOCK			0x13C01C
#define Q8_SEM5_LOCK			0x13C028
#define Q8_SEM5_UNLOCK			0x13C02C
#define Q8_SEM7_LOCK			0x13C038
#define Q8_SEM7_UNLOCK			0x13C03C

/* Valid bit for a SEM<N>_LOCK registers */
#define SEM_LOCK_BIT			0x00000001


#define Q8_ROM_LOCKID			0x1B2100

/*******************************
 * Firmware Interface Registers
 *******************************/
#define Q8_FW_VER_MAJOR			0x1B2150
#define Q8_FW_VER_MINOR			0x1B2154
#define Q8_FW_VER_SUB			0x1B2158
#define Q8_FW_VER_BUILD			0x1B2168

#define Q8_CMDPEG_STATE			0x1B2250
#define Q8_RCVPEG_STATE			0x1B233C
/*
 * definitions for Q8_CMDPEG_STATE
 */
#define CMDPEG_PHAN_INIT_COMPLETE	0xFF01

#define Q8_ROM_STATUS			0x1A0004
/*
 * definitions for Q8_ROM_STATUS
 * bit definitions for Q8_UNM_ROMUSB_GLB_STATUS 
 * 31:3 Reserved; Rest as below
 */
#define	ROM_STATUS_RDY			0x0004
#define	ROM_STATUS_DONE			0x0002
#define	ROM_STATUS_AUTO_ROM_SHDW	0x0001

#define Q8_ASIC_RESET			0x1A0008
/*
 * definitions for Q8_ASIC_RESET
 */
#define ASIC_RESET_RST_XDMA		0x00800000 /* Reset XDMA */
#define ASIC_RESET_PEG_ICACHE		0x00000020 /* Reset PEG_ICACHE */
#define ASIC_RESET_PEG_DCACHE		0x00000010 /* Reset PEG_DCACHE */
#define ASIC_RESET_PEG_3		0x00000008 /* Reset PEG_3 */
#define ASIC_RESET_PEG_2		0x00000004 /* Reset PEG_2 */
#define ASIC_RESET_PEG_1		0x00000002 /* Reset PEG_1 */
#define ASIC_RESET_PEG_0		0x00000001 /* Reset PEG_0 */

#define Q8_COLD_BOOT			0x1B21FC
/*
 * definitions for Q8_COLD_BOOT
 */
#define COLD_BOOT_VALUE		0x12345678


#define Q8_MIU_TEST_AGT_CTRL		0x180090
#define Q8_MIU_TEST_AGT_ADDR_LO		0x180094
#define Q8_MIU_TEST_AGT_ADDR_HI		0x180098
#define Q8_MIU_TEST_AGT_WRDATA_LO	0x1800A0
#define Q8_MIU_TEST_AGT_WRDATA_HI	0x1800A4
#define Q8_MIU_TEST_AGT_RDDATA_LO	0x1800A8
#define Q8_MIU_TEST_AGT_RDDATA_HI	0x1800AC
#define Q8_MIU_TEST_AGT_WRDATA_ULO	0x1800B0
#define Q8_MIU_TEST_AGT_WRDATA_UHI	0x1800B4
#define Q8_MIU_TEST_AGT_RDDATA_ULO	0x1800B8
#define Q8_MIU_TEST_AGT_RDDATA_UHI	0x1800BC

#define Q8_PEG_0_RESET			0x160018
#define Q8_PEG_0_CLR1			0x160008
#define Q8_PEG_0_CLR2			0x16000C
#define Q8_PEG_1_CLR1			0x161008
#define Q8_PEG_1_CLR2			0x16100C
#define Q8_PEG_2_CLR1			0x162008
#define Q8_PEG_2_CLR2			0x16200C
#define Q8_PEG_3_CLR1			0x163008
#define Q8_PEG_3_CLR2			0x16300C
#define Q8_PEG_4_CLR1			0x164008
#define Q8_PEG_4_CLR2			0x16400C
#define Q8_PEG_D_RESET1			0x1650EC
#define Q8_PEG_D_RESET2			0x16504C
#define Q8_PEG_HALT_STATUS1		0x1B20A8
#define Q8_PEG_HALT_STATUS2		0x1B20AC
#define Q8_FIRMWARE_HEARTBEAT		0x1B20B0
#define Q8_PEG_I_RESET			0x16604C

#define Q8_CRB_MAC_BLOCK_START		0x1B21C0

/***************************************************
 * Flash ROM Access Registers ( Indirect Registers )
 ***************************************************/

#define Q8_ROM_INSTR_OPCODE		0x03310004
/*
 * bit definitions for Q8_ROM_INSTR_OPCODE 
 * 31:8 Reserved; Rest Below
 */
#define ROM_OPCODE_WR_STATUS_REG	0x01
#define ROM_OPCODE_PROG_PAGE		0x02
#define ROM_OPCODE_RD_BYTE		0x03
#define ROM_OPCODE_WR_DISABLE		0x04
#define ROM_OPCODE_RD_STATUS_REG	0x05
#define ROM_OPCODE_WR_ENABLE		0x06
#define ROM_OPCODE_FAST_RD		0x0B
#define ROM_OPCODE_REL_DEEP_PWR_DWN	0xAB
#define ROM_OPCODE_BULK_ERASE		0xC7
#define ROM_OPCODE_DEEP_PWR_DWN		0xC9
#define ROM_OPCODE_SECTOR_ERASE		0xD8

#define Q8_ROM_ADDRESS			0x03310008
/*
 * bit definitions for Q8_ROM_ADDRESS 
 * 31:24 Reserved;
 * 23:0  Physical ROM Address in bytes
 */

#define Q8_ROM_ADDR_BYTE_COUNT		0x03310010
/*
 * bit definitions for Q8_ROM_ADDR_BYTE_COUNT 
 * 31:2 Reserved;
 * 1:0  max address bytes for ROM Interface
 */
 
#define Q8_ROM_DUMMY_BYTE_COUNT		0x03310014
/*
 * bit definitions for Q8_ROM_DUMMY_BYTE_COUNT 
 * 31:2 Reserved;
 * 1:0 dummy bytes for ROM Instructions
 */

#define Q8_ROM_RD_DATA			0x03310018
#define Q8_ROM_WR_DATA                  0x0331000C
#define Q8_ROM_DIRECT_WINDOW            0x03310030
#define Q8_ROM_DIRECT_DATA_OFFSET       0x03310000


#define Q8_NX_CDRP_CMD_RSP		0x1B2218
#define Q8_NX_CDRP_ARG1			0x1B221C
#define Q8_NX_CDRP_ARG2			0x1B2220
#define Q8_NX_CDRP_ARG3			0x1B2224
#define Q8_NX_CDRP_SIGNATURE		0x1B2228

#define Q8_LINK_STATE			0x1B2298
#define Q8_LINK_SPEED_0			0x1B22E8
/*
 * Macros for reading and writing registers
 */

#if defined(__i386__) || defined(__amd64__)
#define Q8_MB()    __asm volatile("mfence" ::: "memory")
#define Q8_WMB()   __asm volatile("sfence" ::: "memory")
#define Q8_RMB()   __asm volatile("lfence" ::: "memory")
#else
#define Q8_MB()
#define Q8_WMB()
#define Q8_RMB()
#endif

#define READ_REG32(ha, reg) bus_read_4((ha->pci_reg), reg)
#define READ_OFFSET32(ha, off) READ_REG32(ha, off)

#define WRITE_REG32(ha, reg, val) \
	{\
		bus_write_4((ha->pci_reg), reg, val);\
		bus_read_4((ha->pci_reg), reg);\
	}

#define WRITE_REG32_MB(ha, reg, val) \
	{\
		Q8_WMB();\
		bus_write_4((ha->pci_reg), reg, val);\
	}

#define WRITE_OFFSET32(ha, off, val)\
		{\
			bus_write_4((ha->pci_reg), off, val);\
			bus_read_4((ha->pci_reg), off);\
		}

#endif /* #ifndef _QLA_REG_H_ */
