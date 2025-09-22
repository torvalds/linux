/*******************************************************************************

  Copyright (c) 2001-2005, Intel Corporation
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
  
   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
  
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
  
   3. Neither the name of the Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

/* $OpenBSD: ixgb_ee.h,v 1.1 2005/11/14 23:25:43 brad Exp $ */

#ifndef _IXGB_EE_H_
#define _IXGB_EE_H_

#define IXGB_EEPROM_SIZE    64		    /* Size in words */

#define IXGB_ETH_LENGTH_OF_ADDRESS   6

/* EEPROM Commands */
#define EEPROM_READ_OPCODE  0x6		    /* EEPROM read opcode */
#define EEPROM_WRITE_OPCODE 0x5		    /* EEPROM write opcode */
#define EEPROM_ERASE_OPCODE 0x7		    /* EEPROM erase opcode */
#define EEPROM_EWEN_OPCODE  0x13	    /* EEPROM erase/write enable */
#define EEPROM_EWDS_OPCODE  0x10	    /* EEPROM erase/write disable */

/* EEPROM MAP (Word Offsets) */
#define EEPROM_IA_1_2_REG        0x0000
#define EEPROM_IA_3_4_REG        0x0001
#define EEPROM_IA_5_6_REG        0x0002
#define EEPROM_COMPATIBILITY_REG 0x0003
#define EEPROM_PBA_1_2_REG       0x0008
#define EEPROM_PBA_3_4_REG       0x0009
#define EEPROM_INIT_CONTROL1_REG 0x000A
#define EEPROM_SUBSYS_ID_REG     0x000B
#define EEPROM_SUBVEND_ID_REG    0x000C
#define EEPROM_DEVICE_ID_REG     0x000D
#define EEPROM_VENDOR_ID_REG     0x000E
#define EEPROM_INIT_CONTROL2_REG 0x000F
#define EEPROM_SWDPINS_REG       0x0020
#define EEPROM_CIRCUIT_CTRL_REG  0x0021
#define EEPROM_D0_D3_POWER_REG   0x0022
#define EEPROM_FLASH_VERSION     0x0032
#define EEPROM_CHECKSUM_REG      0x003F

/* Mask bits for fields in Word 0x0a of the EEPROM */

#define EEPROM_ICW1_SIGNATURE_MASK  0xC000
#define EEPROM_ICW1_SIGNATURE_VALID 0x4000
#define EEPROM_ICW1_SIGNATURE_CLEAR 0x0000

/* For checksumming, the sum of all words in the EEPROM should equal 0xBABA. */
#define EEPROM_SUM 0xBABA

/* EEPROM Map Sizes (Byte Counts) */
#define PBA_SIZE 4

/* EEPROM Map defines (WORD OFFSETS)*/

/* EEPROM structure */
struct ixgb_ee_map_type {
	uint8_t  mac_addr[IXGB_ETH_LENGTH_OF_ADDRESS];
	uint16_t compatibility;
	uint16_t reserved1[4];
	uint32_t pba_number;
	uint16_t init_ctrl_reg_1;
	uint16_t subsystem_id;
	uint16_t subvendor_id;
	uint16_t device_id;
	uint16_t vendor_id;
	uint16_t init_ctrl_reg_2;
	uint16_t oem_reserved[16];
	uint16_t swdpins_reg;
	uint16_t circuit_ctrl_reg;
	uint8_t  d3_power;
	uint8_t  d0_power;
	uint16_t reserved2[28];
	uint16_t checksum;
};

/* EEPROM Functions */
uint16_t ixgb_read_eeprom(struct ixgb_hw *hw, uint16_t reg);

boolean_t ixgb_validate_eeprom_checksum(struct ixgb_hw *hw);

void ixgb_update_eeprom_checksum(struct ixgb_hw *hw);

void ixgb_write_eeprom(struct ixgb_hw *hw, uint16_t reg, uint16_t data);

#endif /* IXGB_EE_H */
