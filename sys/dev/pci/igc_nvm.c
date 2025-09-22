/*	$OpenBSD: igc_nvm.c,v 1.1 2021/10/31 14:52:57 patrick Exp $	*/
/*-
 * Copyright 2021 Intel Corp
 * Copyright 2021 Rubicon Communications, LLC (Netgate)
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <dev/pci/igc_api.h>

/**
 *  igc_init_nvm_ops_generic - Initialize NVM function pointers
 *  @hw: pointer to the HW structure
 *
 *  Setups up the function pointers to no-op functions
 **/
void
igc_init_nvm_ops_generic(struct igc_hw *hw)
{
	struct igc_nvm_info *nvm = &hw->nvm;
	DEBUGFUNC("igc_init_nvm_ops_generic");

	/* Initialize function pointers */
	nvm->ops.init_params = igc_null_ops_generic;
	nvm->ops.acquire = igc_null_ops_generic;
	nvm->ops.read = igc_null_read_nvm;
	nvm->ops.release = igc_null_nvm_generic;
	nvm->ops.reload = igc_reload_nvm_generic;
	nvm->ops.update = igc_null_ops_generic;
	nvm->ops.validate = igc_null_ops_generic;
	nvm->ops.write = igc_null_write_nvm;
}

/**
 *  igc_null_nvm_read - No-op function, return 0
 *  @hw: pointer to the HW structure
 *  @a: dummy variable
 *  @b: dummy variable
 *  @c: dummy variable
 **/
int
igc_null_read_nvm(struct igc_hw IGC_UNUSEDARG *hw, uint16_t IGC_UNUSEDARG a,
    uint16_t IGC_UNUSEDARG b, uint16_t IGC_UNUSEDARG *c)
{
	DEBUGFUNC("igc_null_read_nvm");
	return IGC_SUCCESS;
}

/**
 *  igc_null_nvm_generic - No-op function, return void
 *  @hw: pointer to the HW structure
 **/
void
igc_null_nvm_generic(struct igc_hw IGC_UNUSEDARG *hw)
{
	DEBUGFUNC("igc_null_nvm_generic");
	return;
}

/**
 *  igc_reload_nvm_generic - Reloads EEPROM
 *  @hw: pointer to the HW structure
 *
 *  Reloads the EEPROM by setting the "Reinitialize from EEPROM" bit in the
 *  extended control register.
 **/
void
igc_reload_nvm_generic(struct igc_hw *hw)
{
	uint32_t ctrl_ext;

	DEBUGFUNC("igc_reload_nvm_generic");

	DELAY(10);
	ctrl_ext = IGC_READ_REG(hw, IGC_CTRL_EXT);
	ctrl_ext |= IGC_CTRL_EXT_EE_RST;
	IGC_WRITE_REG(hw, IGC_CTRL_EXT, ctrl_ext);
	IGC_WRITE_FLUSH(hw);
}

/**
 *  igc_null_write_nvm - No-op function, return 0
 *  @hw: pointer to the HW structure
 *  @a: dummy variable
 *  @b: dummy variable
 *  @c: dummy variable
 **/
int
igc_null_write_nvm(struct igc_hw IGC_UNUSEDARG *hw, uint16_t IGC_UNUSEDARG a,
    uint16_t IGC_UNUSEDARG b, uint16_t IGC_UNUSEDARG *c)
{
	DEBUGFUNC("igc_null_write_nvm");
	return IGC_SUCCESS;
}

/**
 *  igc_poll_eerd_eewr_done - Poll for EEPROM read/write completion
 *  @hw: pointer to the HW structure
 *  @ee_reg: EEPROM flag for polling
 *
 *  Polls the EEPROM status bit for either read or write completion based
 *  upon the value of 'ee_reg'.
 **/
int
igc_poll_eerd_eewr_done(struct igc_hw *hw, int ee_reg)
{
	uint32_t attempts = 100000;
	uint32_t i, reg = 0;

	DEBUGFUNC("igc_poll_eerd_eewr_done");

	for (i = 0; i < attempts; i++) {
		if (ee_reg == IGC_NVM_POLL_READ)
			reg = IGC_READ_REG(hw, IGC_EERD);
		else
			reg = IGC_READ_REG(hw, IGC_EEWR);

		if (reg & IGC_NVM_RW_REG_DONE)
			return IGC_SUCCESS;

		DELAY(5);
	}

	return -IGC_ERR_NVM;
}

/**
 *  igc_read_nvm_eerd - Reads EEPROM using EERD register
 *  @hw: pointer to the HW structure
 *  @offset: offset of word in the EEPROM to read
 *  @words: number of words to read
 *  @data: word read from the EEPROM
 *
 *  Reads a 16 bit word from the EEPROM using the EERD register.
 **/
int
igc_read_nvm_eerd(struct igc_hw *hw, uint16_t offset, uint16_t words,
    uint16_t *data)
{
	struct igc_nvm_info *nvm = &hw->nvm;
	uint32_t i, eerd = 0;
	int ret_val = IGC_SUCCESS;

	DEBUGFUNC("igc_read_nvm_eerd");

	/* A check for invalid values:  offset too large, too many words,
	 * too many words for the offset, and not enough words.
	 */
	if ((offset >= nvm->word_size) || (words > (nvm->word_size - offset)) ||
	    (words == 0)) {
		DEBUGOUT("nvm parameter(s) out of bounds\n");
		return -IGC_ERR_NVM;
	}

	for (i = 0; i < words; i++) {
		eerd = ((offset + i) << IGC_NVM_RW_ADDR_SHIFT) +
		    IGC_NVM_RW_REG_START;

		IGC_WRITE_REG(hw, IGC_EERD, eerd);
		ret_val = igc_poll_eerd_eewr_done(hw, IGC_NVM_POLL_READ);
		if (ret_val)
			break;

		data[i] = (IGC_READ_REG(hw, IGC_EERD) >> IGC_NVM_RW_REG_DATA);
	}

	if (ret_val)
		DEBUGOUT1("NVM read error: %d\n", ret_val);

	return ret_val;
}

/**
 *  igc_read_mac_addr_generic - Read device MAC address
 *  @hw: pointer to the HW structure
 *
 *  Reads the device MAC address from the EEPROM and stores the value.
 *  Since devices with two ports use the same EEPROM, we increment the
 *  last bit in the MAC address for the second port.
 **/
int
igc_read_mac_addr_generic(struct igc_hw *hw)
{
	uint32_t rar_high, rar_low;
	uint16_t i;

	rar_high = IGC_READ_REG(hw, IGC_RAH(0));
	rar_low = IGC_READ_REG(hw, IGC_RAL(0));

	for (i = 0; i < IGC_RAL_MAC_ADDR_LEN; i++)
		hw->mac.perm_addr[i] = (uint8_t)(rar_low >> (i * 8));

	for (i = 0; i < IGC_RAH_MAC_ADDR_LEN; i++)
		hw->mac.perm_addr[i+4] = (uint8_t)(rar_high >> (i * 8));

	for (i = 0; i < ETHER_ADDR_LEN; i++)
		hw->mac.addr[i] = hw->mac.perm_addr[i];

	return IGC_SUCCESS;
}

/**
 *  igc_validate_nvm_checksum_generic - Validate EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Calculates the EEPROM checksum by reading/adding each word of the EEPROM
 *  and then verifies that the sum of the EEPROM is equal to 0xBABA.
 **/
int
igc_validate_nvm_checksum_generic(struct igc_hw *hw)
{
	uint16_t checksum = 0;
	uint16_t i, nvm_data;
	int ret_val;

	DEBUGFUNC("igc_validate_nvm_checksum_generic");

	for (i = 0; i < (NVM_CHECKSUM_REG + 1); i++) {
		ret_val = hw->nvm.ops.read(hw, i, 1, &nvm_data);
		if (ret_val) {
			DEBUGOUT("NVM Read Error\n");
			return ret_val;
		}
		checksum += nvm_data;
	}

	if (checksum != (uint16_t) NVM_SUM) {
		DEBUGOUT("NVM Checksum Invalid\n");
		return -IGC_ERR_NVM;
	}

	return IGC_SUCCESS;
}
