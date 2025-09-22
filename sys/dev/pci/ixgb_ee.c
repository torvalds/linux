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

/* $OpenBSD: ixgb_ee.c,v 1.11 2025/06/29 19:32:08 miod Exp $ */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <uvm/uvm_extern.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/pci/ixgb_hw.h> 
#include <dev/pci/ixgb_ee.h> 

/* Local prototypes */
static uint16_t ixgb_shift_in_bits(struct ixgb_hw *hw);

static void ixgb_shift_out_bits(struct ixgb_hw *hw, uint16_t data,
                                uint16_t count);
static void ixgb_standby_eeprom(struct ixgb_hw *hw);

static boolean_t ixgb_wait_eeprom_command(struct ixgb_hw *hw);

static void ixgb_cleanup_eeprom(struct ixgb_hw *hw);

/******************************************************************************
 * Raises the EEPROM's clock input.
 *
 * hw - Struct containing variables accessed by shared code
 * eecd_reg - EECD's current value
 *****************************************************************************/
static void
ixgb_raise_clock(struct ixgb_hw *hw, uint32_t *eecd_reg)
{
	/* Raise the clock input to the EEPROM (by setting the SK bit), and
	 * then wait 50 microseconds. */
	*eecd_reg = *eecd_reg | IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, *eecd_reg);
	usec_delay(50);
	return;
}

/******************************************************************************
 * Lowers the EEPROM's clock input.
 *
 * hw - Struct containing variables accessed by shared code
 * eecd_reg - EECD's current value
 *****************************************************************************/
static void
ixgb_lower_clock(struct ixgb_hw *hw, uint32_t *eecd_reg)
{
	/* Lower the clock input to the EEPROM (by clearing the SK bit), and
	 * then wait 50 microseconds. */
	*eecd_reg = *eecd_reg & ~IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, *eecd_reg);
	usec_delay(50);
	return;
}

/******************************************************************************
 * Shift data bits out to the EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 * data - data to send to the EEPROM
 * count - number of bits to shift out
 *****************************************************************************/
static void
ixgb_shift_out_bits(struct ixgb_hw *hw, uint16_t data, uint16_t count)
{
	uint32_t eecd_reg;
	uint32_t mask;

	/* We need to shift "count" bits out to the EEPROM. So, value in the
	 * "data" parameter will be shifted out to the EEPROM one bit at a
	 * time. In order to do this, "data" must be broken down into bits. */
	mask = 0x01 << (count - 1);
	eecd_reg = IXGB_READ_REG(hw, EECD);
	eecd_reg &= ~(IXGB_EECD_DO | IXGB_EECD_DI);
	do {
		/* A "1" is shifted out to the EEPROM by setting bit "DI" to a
		 * "1", and then raising and then lowering the clock (the SK
		 * bit controls the clock input to the EEPROM).  A "0" is
		 * shifted out to the EEPROM by setting "DI" to "0" and then
		 * raising and then lowering the clock. */
		eecd_reg &= ~IXGB_EECD_DI;

		if(data & mask)
			eecd_reg |= IXGB_EECD_DI;

		IXGB_WRITE_REG(hw, EECD, eecd_reg);

		usec_delay(50);

		ixgb_raise_clock(hw, &eecd_reg);
		ixgb_lower_clock(hw, &eecd_reg);

		mask = mask >> 1;

	} while(mask);

	/* We leave the "DI" bit set to "0" when we leave this routine. */
	eecd_reg &= ~IXGB_EECD_DI;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	return;
}

/******************************************************************************
 * Shift data bits in from the EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static uint16_t
ixgb_shift_in_bits(struct ixgb_hw *hw)
{
	uint32_t eecd_reg;
	uint32_t i;
	uint16_t data;

	/* In order to read a register from the EEPROM, we need to shift 16
	 * bits in from the EEPROM. Bits are "shifted in" by raising the clock
	 * input to the EEPROM (setting the SK bit), and then reading the value
	 * of the "DO" bit.  During this "shifting in" process the "DI" bit
	 * should always be clear.. */

	eecd_reg = IXGB_READ_REG(hw, EECD);

	eecd_reg &= ~(IXGB_EECD_DO | IXGB_EECD_DI);
	data = 0;

	for(i = 0; i < 16; i++) {
		data = data << 1;
		ixgb_raise_clock(hw, &eecd_reg);

		eecd_reg = IXGB_READ_REG(hw, EECD);

		eecd_reg &= ~(IXGB_EECD_DI);
		if(eecd_reg & IXGB_EECD_DO)
			data |= 1;

		ixgb_lower_clock(hw, &eecd_reg);
	}

	return data;
}

/******************************************************************************
 * Prepares EEPROM for access
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Lowers EEPROM clock. Clears input pin. Sets the chip select pin. This
 * function should be called before issuing a command to the EEPROM.
 *****************************************************************************/
static void
ixgb_setup_eeprom(struct ixgb_hw *hw)
{
	uint32_t eecd_reg;

	eecd_reg = IXGB_READ_REG(hw, EECD);

	/* Clear SK and DI */
	eecd_reg &= ~(IXGB_EECD_SK | IXGB_EECD_DI);
	IXGB_WRITE_REG(hw, EECD, eecd_reg);

	/* Set CS */
	eecd_reg |= IXGB_EECD_CS;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	return;
}

/******************************************************************************
 * Returns EEPROM to a "standby" state
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
ixgb_standby_eeprom(struct ixgb_hw *hw)
{
	uint32_t eecd_reg;

	eecd_reg = IXGB_READ_REG(hw, EECD);

	/* Deselect EEPROM */
	eecd_reg &= ~(IXGB_EECD_CS | IXGB_EECD_SK);
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	usec_delay(50);

	/* Clock high */
	eecd_reg |= IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	usec_delay(50);

	/* Select EEPROM */
	eecd_reg |= IXGB_EECD_CS;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	usec_delay(50);

	/* Clock low */
	eecd_reg &= ~IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	usec_delay(50);
	return;
}

/******************************************************************************
 * Raises then lowers the EEPROM's clock pin
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
ixgb_clock_eeprom(struct ixgb_hw *hw)
{
	uint32_t eecd_reg;

	eecd_reg = IXGB_READ_REG(hw, EECD);

	/* Rising edge of clock */
	eecd_reg |= IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	usec_delay(50);

	/* Falling edge of clock */
	eecd_reg &= ~IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	usec_delay(50);
	return;
}

/******************************************************************************
 * Terminates a command by lowering the EEPROM's chip select pin
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
ixgb_cleanup_eeprom(struct ixgb_hw *hw)
{
	uint32_t eecd_reg;

	eecd_reg = IXGB_READ_REG(hw, EECD);

	eecd_reg &= ~(IXGB_EECD_CS | IXGB_EECD_DI);

	IXGB_WRITE_REG(hw, EECD, eecd_reg);

	ixgb_clock_eeprom(hw);
	return;
}

/******************************************************************************
 * Waits for the EEPROM to finish the current command.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * The command is done when the EEPROM's data out pin goes high.
 *
 * Returns:
 *      TRUE: EEPROM data pin is high before timeout.
 *      FALSE:  Time expired.
 *****************************************************************************/
static boolean_t
ixgb_wait_eeprom_command(struct ixgb_hw *hw)
{
	uint32_t eecd_reg;
	uint32_t i;

	/* Toggle the CS line.  This in effect tells to EEPROM to actually
	 * execute the command in question. */
	ixgb_standby_eeprom(hw);

	/* Now read DO repeatedly until is high (equal to '1').  The EEPROM
	 * will signal that the command has been completed by raising the DO
	 * signal. If DO does not go high in 10 milliseconds, then error out. */
	for(i = 0; i < 200; i++) {
		eecd_reg = IXGB_READ_REG(hw, EECD);

		if(eecd_reg & IXGB_EECD_DO)
			return (TRUE);

		usec_delay(50);
	}
	ASSERT(0);
	return (FALSE);
}

/******************************************************************************
 * Verifies that the EEPROM has a valid checksum
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Reads the first 64 16 bit words of the EEPROM and sums the values read.
 * If the sum of the 64 16 bit words is 0xBABA, the EEPROM's checksum is
 * valid.
 *
 * Returns:
 *  TRUE: Checksum is valid
 *  FALSE: Checksum is not valid.
 *****************************************************************************/
boolean_t
ixgb_validate_eeprom_checksum(struct ixgb_hw *hw)
{
	uint16_t checksum = 0;
	uint16_t i;

	for(i = 0; i < (EEPROM_CHECKSUM_REG + 1); i++)
		checksum += ixgb_read_eeprom(hw, i);

	if(checksum == (uint16_t)EEPROM_SUM)
		return (TRUE);
	else
		return (FALSE);
}

/******************************************************************************
 * Calculates the EEPROM checksum and writes it to the EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Sums the first 63 16 bit words of the EEPROM. Subtracts the sum from 0xBABA.
 * Writes the difference to word offset 63 of the EEPROM.
 *****************************************************************************/
void
ixgb_update_eeprom_checksum(struct ixgb_hw *hw)
{
	uint16_t checksum = 0;
	uint16_t i;

	for(i = 0; i < EEPROM_CHECKSUM_REG; i++)
		checksum += ixgb_read_eeprom(hw, i);

	checksum = (uint16_t)EEPROM_SUM - checksum;

	ixgb_write_eeprom(hw, EEPROM_CHECKSUM_REG, checksum);
	return;
}

/******************************************************************************
 * Writes a 16 bit word to a given offset in the EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 * reg - offset within the EEPROM to be written to
 * data - 16 bit word to be written to the EEPROM
 *
 * If ixgb_update_eeprom_checksum is not called after this function, the
 * EEPROM will most likely contain an invalid checksum.
 *
 *****************************************************************************/
void
ixgb_write_eeprom(struct ixgb_hw *hw, uint16_t offset, uint16_t data)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	/* Prepare the EEPROM for writing */
	ixgb_setup_eeprom(hw);

	/* Send the 9-bit EWEN (write enable) command to the EEPROM (5-bit
	 * opcode plus 4-bit dummy).  This puts the EEPROM into write/erase
	 * mode. */
	ixgb_shift_out_bits(hw, EEPROM_EWEN_OPCODE, 5);
	ixgb_shift_out_bits(hw, 0, 4);

	/* Prepare the EEPROM */
	ixgb_standby_eeprom(hw);

	/* Send the Write command (3-bit opcode + 6-bit addr) */
	ixgb_shift_out_bits(hw, EEPROM_WRITE_OPCODE, 3);
	ixgb_shift_out_bits(hw, offset, 6);

	/* Send the data */
	ixgb_shift_out_bits(hw, data, 16);

	ixgb_wait_eeprom_command(hw);

	/* Recover from write */
	ixgb_standby_eeprom(hw);

	/* Send the 9-bit EWDS (write disable) command to the EEPROM (5-bit
	 * opcode plus 4-bit dummy).  This takes the EEPROM out of write/erase
	 * mode. */
	ixgb_shift_out_bits(hw, EEPROM_EWDS_OPCODE, 5);
	ixgb_shift_out_bits(hw, 0, 4);

	/* Done with writing */
	ixgb_cleanup_eeprom(hw);

	/* clear the init_ctrl_reg_1 to signify that the cache is invalidated */
	ee_map->init_ctrl_reg_1 = le16_to_cpu(EEPROM_ICW1_SIGNATURE_CLEAR);

	return;
}

/******************************************************************************
 * Reads a 16 bit word from the EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset of 16 bit word in the EEPROM to read
 *
 * Returns:
 *  The 16-bit value read from the eeprom
 *****************************************************************************/
uint16_t
ixgb_read_eeprom(struct ixgb_hw *hw, uint16_t offset)
{
	uint16_t data;

	/* Prepare the EEPROM for reading */
	ixgb_setup_eeprom(hw);

	/* Send the READ command (opcode + addr) */
	ixgb_shift_out_bits(hw, EEPROM_READ_OPCODE, 3);
	/*
	 * We have a 64 word EEPROM, there are 6 address bits
	 */
	ixgb_shift_out_bits(hw, offset, 6);

	/* Read the data */
	data = ixgb_shift_in_bits(hw);

	/* End this read operation */
	ixgb_standby_eeprom(hw);

	return (data);
}

/******************************************************************************
 * Reads eeprom and stores data in shared structure.
 * Validates eeprom checksum and eeprom signature.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *      TRUE: if eeprom read is successful
 *      FALSE: otherwise.
 *****************************************************************************/
boolean_t
ixgb_get_eeprom_data(struct ixgb_hw *hw)
{
	uint16_t i;
	uint16_t checksum = 0;
	struct ixgb_ee_map_type *ee_map;

	DEBUGFUNC("ixgb_get_eeprom_data");

	ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	DEBUGOUT("ixgb_ee: Reading eeprom data\n");
	for(i = 0; i < IXGB_EEPROM_SIZE; i++) {
		uint16_t ee_data;

		ee_data = ixgb_read_eeprom(hw, i);
		checksum += ee_data;
		hw->eeprom[i] = le16_to_cpu(ee_data);
	}

	if(checksum != (uint16_t)EEPROM_SUM) {
		DEBUGOUT("ixgb_ee: Checksum invalid.\n");
		/* clear the init_ctrl_reg_1 to signify that the cache is
		 * invalidated */
		ee_map->init_ctrl_reg_1 = le16_to_cpu(EEPROM_ICW1_SIGNATURE_CLEAR);
		return (FALSE);
	}

	if((ee_map->init_ctrl_reg_1 & le16_to_cpu(EEPROM_ICW1_SIGNATURE_MASK))
	   != le16_to_cpu(EEPROM_ICW1_SIGNATURE_VALID)) {
		DEBUGOUT("ixgb_ee: Signature invalid.\n");
		return (FALSE);
	}

	return (TRUE);
}

/******************************************************************************
 * Local function to check if the eeprom signature is good
 * If the eeprom signature is good, calls ixgb)get_eeprom_data.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *      TRUE: eeprom signature was good and the eeprom read was successful
 *      FALSE: otherwise.
 ******************************************************************************/
static boolean_t
ixgb_check_and_get_eeprom_data(struct ixgb_hw *hw)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	if((ee_map->init_ctrl_reg_1 & le16_to_cpu(EEPROM_ICW1_SIGNATURE_MASK))
	   == le16_to_cpu(EEPROM_ICW1_SIGNATURE_VALID)) {
		return (TRUE);
	} else {
		return ixgb_get_eeprom_data(hw);
	}
}

/******************************************************************************
 * return the mac address from EEPROM
 *
 * hw       - Struct containing variables accessed by shared code
 * mac_addr - Ethernet Address if EEPROM contents are valid, 0 otherwise
 *
 * Returns: None.
 ******************************************************************************/
void
ixgb_get_ee_mac_addr(struct ixgb_hw *hw, uint8_t *mac_addr)
{
	int i;
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	DEBUGFUNC("ixgb_get_ee_mac_addr");

	if(ixgb_check_and_get_eeprom_data(hw) == TRUE) {
		for(i = 0; i < IXGB_ETH_LENGTH_OF_ADDRESS; i++) {
			mac_addr[i] = ee_map->mac_addr[i];
			DEBUGOUT2("mac(%d) = %.2X\n", i, mac_addr[i]);
		}
	}
}


/******************************************************************************
 * return the Device Id from EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *          Device Id if EEPROM contents are valid, 0 otherwise
 ******************************************************************************/
uint16_t
ixgb_get_ee_device_id(struct ixgb_hw *hw)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	if(ixgb_check_and_get_eeprom_data(hw) == TRUE)
		return (le16_to_cpu(ee_map->device_id));

	return (0);
}

