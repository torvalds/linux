/*	$OpenBSD: igc_phy.c,v 1.3 2023/02/03 11:31:52 mbuhl Exp $	*/
/*-
 * Copyright 2021 Intel Corp
 * Copyright 2021 Rubicon Communications, LLC (Netgate)
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <dev/pci/igc_api.h>

/**
 *  igc_init_phy_ops_generic - Initialize PHY function pointers
 *  @hw: pointer to the HW structure
 *
 *  Setups up the function pointers to no-op functions
 **/
void
igc_init_phy_ops_generic(struct igc_hw *hw)
{
	struct igc_phy_info *phy = &hw->phy;
	DEBUGFUNC("igc_init_phy_ops_generic");

	/* Initialize function pointers */
	phy->ops.init_params = igc_null_ops_generic;
	phy->ops.acquire = igc_null_ops_generic;
	phy->ops.check_reset_block = igc_null_ops_generic;
	phy->ops.force_speed_duplex = igc_null_ops_generic;
	phy->ops.get_info = igc_null_ops_generic;
	phy->ops.set_page = igc_null_set_page;
	phy->ops.read_reg = igc_null_read_reg;
	phy->ops.read_reg_locked = igc_null_read_reg;
	phy->ops.read_reg_page = igc_null_read_reg;
	phy->ops.release = igc_null_phy_generic;
	phy->ops.reset = igc_null_ops_generic;
	phy->ops.set_d0_lplu_state = igc_null_lplu_state;
	phy->ops.set_d3_lplu_state = igc_null_lplu_state;
	phy->ops.write_reg = igc_null_write_reg;
	phy->ops.write_reg_locked = igc_null_write_reg;
	phy->ops.write_reg_page = igc_null_write_reg;
	phy->ops.power_up = igc_null_phy_generic;
	phy->ops.power_down = igc_null_phy_generic;
}

/**
 *  igc_null_set_page - No-op function, return 0
 *  @hw: pointer to the HW structure
 *  @data: dummy variable
 **/
int
igc_null_set_page(struct igc_hw IGC_UNUSEDARG *hw, uint16_t IGC_UNUSEDARG data)
{
	DEBUGFUNC("igc_null_set_page");
	return IGC_SUCCESS;
}

/**
 *  igc_null_read_reg - No-op function, return 0
 *  @hw: pointer to the HW structure
 *  @offset: dummy variable
 *  @data: dummy variable
 **/
int
igc_null_read_reg(struct igc_hw IGC_UNUSEDARG *hw, 
    uint32_t IGC_UNUSEDARG offset, uint16_t IGC_UNUSEDARG *data)
{
	DEBUGFUNC("igc_null_read_reg");
	return IGC_SUCCESS;
}

/**
 *  igc_null_phy_generic - No-op function, return void
 *  @hw: pointer to the HW structure
 **/
void
igc_null_phy_generic(struct igc_hw IGC_UNUSEDARG *hw)
{
	DEBUGFUNC("igc_null_phy_generic");
	return;
}

/**
 *  igc_null_lplu_state - No-op function, return 0
 *  @hw: pointer to the HW structure
 *  @active: dummy variable
 **/
int
igc_null_lplu_state(struct igc_hw IGC_UNUSEDARG *hw, bool IGC_UNUSEDARG active)
{
	DEBUGFUNC("igc_null_lplu_state");
	return IGC_SUCCESS;
}

/**
 *  igc_null_write_reg - No-op function, return 0
 *  @hw: pointer to the HW structure
 *  @offset: dummy variable
 *  @data: dummy variable
 **/
int
igc_null_write_reg(struct igc_hw IGC_UNUSEDARG *hw,
    uint32_t IGC_UNUSEDARG offset, uint16_t IGC_UNUSEDARG data)
{
	DEBUGFUNC("igc_null_write_reg");
	return IGC_SUCCESS;
}

/**
 *  igc_check_reset_block_generic - Check if PHY reset is blocked
 *  @hw: pointer to the HW structure
 *
 *  Read the PHY management control register and check whether a PHY reset
 *  is blocked.  If a reset is not blocked return IGC_SUCCESS, otherwise
 *  return IGC_BLK_PHY_RESET (12).
 **/
int
igc_check_reset_block_generic(struct igc_hw *hw)
{
	uint32_t manc;

	DEBUGFUNC("igc_check_reset_block");

	manc = IGC_READ_REG(hw, IGC_MANC);

	return (manc & IGC_MANC_BLK_PHY_RST_ON_IDE) ?
	    IGC_BLK_PHY_RESET : IGC_SUCCESS;
}

/**
 *  igc_get_phy_id - Retrieve the PHY ID and revision
 *  @hw: pointer to the HW structure
 *
 *  Reads the PHY registers and stores the PHY ID and possibly the PHY
 *  revision in the hardware structure.
 **/
int
igc_get_phy_id(struct igc_hw *hw)
{
	struct igc_phy_info *phy = &hw->phy;
	uint16_t phy_id;
	int ret_val = IGC_SUCCESS;

	DEBUGFUNC("igc_get_phy_id");

	if (!phy->ops.read_reg)
		return IGC_SUCCESS;

	ret_val = phy->ops.read_reg(hw, PHY_ID1, &phy_id);
	if (ret_val)
		return ret_val;

	phy->id = (uint32_t)(phy_id << 16);
	DELAY(200);
	ret_val = phy->ops.read_reg(hw, PHY_ID2, &phy_id);
	if (ret_val)
		return ret_val;

	phy->id |= (uint32_t)(phy_id & PHY_REVISION_MASK);
	phy->revision = (uint32_t)(phy_id & ~PHY_REVISION_MASK);

	return IGC_SUCCESS;
}

/**
 *  igc_read_phy_reg_mdic - Read MDI control register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Reads the MDI control register in the PHY at offset and stores the
 *  information read to data.
 **/
int
igc_read_phy_reg_mdic(struct igc_hw *hw, uint32_t offset, uint16_t *data)
{
	struct igc_phy_info *phy = &hw->phy;
	uint32_t i, mdic = 0;

	DEBUGFUNC("igc_read_phy_reg_mdic");

	if (offset > MAX_PHY_REG_ADDRESS) {
		DEBUGOUT1("PHY Address %d is out of range\n", offset);
		return -IGC_ERR_PARAM;
	}

	/* Set up Op-code, Phy Address, and register offset in the MDI
	 * Control register.  The MAC will take care of interfacing with the
	 * PHY to retrieve the desired data.
	 */
	mdic = ((offset << IGC_MDIC_REG_SHIFT) |
	    (phy->addr << IGC_MDIC_PHY_SHIFT) | (IGC_MDIC_OP_READ));

	IGC_WRITE_REG(hw, IGC_MDIC, mdic);

	/* Poll the ready bit to see if the MDI read completed
	 * Increasing the time out as testing showed failures with
	 * the lower time out
	 */
	for (i = 0; i < (IGC_GEN_POLL_TIMEOUT * 3); i++) {
		DELAY(50);
		mdic = IGC_READ_REG(hw, IGC_MDIC);
		if (mdic & IGC_MDIC_READY)
			break;
	}
	if (!(mdic & IGC_MDIC_READY)) {
		DEBUGOUT("MDI Read did not complete\n");
		return -IGC_ERR_PHY;
	}
	if (mdic & IGC_MDIC_ERROR) {
		DEBUGOUT("MDI Error\n");
		return -IGC_ERR_PHY;
	}
	if (((mdic & IGC_MDIC_REG_MASK) >> IGC_MDIC_REG_SHIFT) != offset) {
		DEBUGOUT2("MDI Read offset error - requested %d, returned %d\n",
		    offset, (mdic & IGC_MDIC_REG_MASK) >> IGC_MDIC_REG_SHIFT);
		return -IGC_ERR_PHY;
	}
	*data = (uint16_t)mdic;

	return IGC_SUCCESS;
}

/**
 *  igc_write_phy_reg_mdic - Write MDI control register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write to register at offset
 *
 *  Writes data to MDI control register in the PHY at offset.
 **/
int
igc_write_phy_reg_mdic(struct igc_hw *hw, uint32_t offset, uint16_t data)
{
	struct igc_phy_info *phy = &hw->phy;
	uint32_t i, mdic = 0;

	DEBUGFUNC("igc_write_phy_reg_mdic");

	if (offset > MAX_PHY_REG_ADDRESS) {
		DEBUGOUT1("PHY Address %d is out of range\n", offset);
		return -IGC_ERR_PARAM;
	}

	/* Set up Op-code, Phy Address, and register offset in the MDI
	 * Control register.  The MAC will take care of interfacing with the
	 * PHY to retrieve the desired data.
	 */
	mdic = (((uint32_t)data) | (offset << IGC_MDIC_REG_SHIFT) |
	    (phy->addr << IGC_MDIC_PHY_SHIFT) | (IGC_MDIC_OP_WRITE));

	IGC_WRITE_REG(hw, IGC_MDIC, mdic);

	/* Poll the ready bit to see if the MDI read completed
	 * Increasing the time out as testing showed failures with
	 * the lower time out
	 */
	for (i = 0; i < (IGC_GEN_POLL_TIMEOUT * 3); i++) {
		DELAY(50);
		mdic = IGC_READ_REG(hw, IGC_MDIC);
		if (mdic & IGC_MDIC_READY)
			break;
	}
	if (!(mdic & IGC_MDIC_READY)) {
		DEBUGOUT("MDI Write did not complete\n");
		return -IGC_ERR_PHY;
	}
	if (mdic & IGC_MDIC_ERROR) {
		DEBUGOUT("MDI Error\n");
		return -IGC_ERR_PHY;
	}
	if (((mdic & IGC_MDIC_REG_MASK) >> IGC_MDIC_REG_SHIFT) != offset)
		return -IGC_ERR_PHY;

	return IGC_SUCCESS;
}

/**
 *  igc_phy_setup_autoneg - Configure PHY for auto-negotiation
 *  @hw: pointer to the HW structure
 *
 *  Reads the MII auto-neg advertisement register and/or the 1000T control
 *  register and if the PHY is already setup for auto-negotiation, then
 *  return successful.  Otherwise, setup advertisement and flow control to
 *  the appropriate values for the wanted auto-negotiation.
 **/
int
igc_phy_setup_autoneg(struct igc_hw *hw)
{
	struct igc_phy_info *phy = &hw->phy;
	uint16_t mii_autoneg_adv_reg;
	uint16_t mii_1000t_ctrl_reg = 0;
	uint16_t aneg_multigbt_an_ctrl = 0;
	int ret_val;

	DEBUGFUNC("igc_phy_setup_autoneg");

	phy->autoneg_advertised &= phy->autoneg_mask;

	/* Read the MII Auto-Neg Advertisement Register (Address 4). */
	ret_val = phy->ops.read_reg(hw, PHY_AUTONEG_ADV, &mii_autoneg_adv_reg);
	if (ret_val)
		return ret_val;

	if (phy->autoneg_mask & ADVERTISE_1000_FULL) {
		/* Read the MII 1000Base-T Control Register (Address 9). */
		ret_val = phy->ops.read_reg(hw, PHY_1000T_CTRL,
		    &mii_1000t_ctrl_reg);
		if (ret_val)
			return ret_val;
	}

	if (phy->autoneg_mask & ADVERTISE_2500_FULL) {
		/* Read the MULTI GBT AN Control Register - reg 7.32 */
		ret_val = phy->ops.read_reg(hw, (STANDARD_AN_REG_MASK <<
		    MMD_DEVADDR_SHIFT) | ANEG_MULTIGBT_AN_CTRL,
		    &aneg_multigbt_an_ctrl);
		if (ret_val)
			return ret_val;
	}

	/* Need to parse both autoneg_advertised and fc and set up
	 * the appropriate PHY registers.  First we will parse for
	 * autoneg_advertised software override.  Since we can advertise
	 * a plethora of combinations, we need to check each bit
	 * individually.
	 */

	/* First we clear all the 10/100 mb speed bits in the Auto-Neg
	 * Advertisement Register (Address 4) and the 1000 mb speed bits in
	 * the  1000Base-T Control Register (Address 9).
	 */
	mii_autoneg_adv_reg &= ~(NWAY_AR_100TX_FD_CAPS | NWAY_AR_100TX_HD_CAPS |
	    NWAY_AR_10T_FD_CAPS | NWAY_AR_10T_HD_CAPS);
	mii_1000t_ctrl_reg &= ~(CR_1000T_HD_CAPS | CR_1000T_FD_CAPS);

	DEBUGOUT1("autoneg_advertised %x\n", phy->autoneg_advertised);

	/* Do we want to advertise 10 Mb Half Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_10_HALF) {
		DEBUGOUT("Advertise 10mb Half duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_10T_HD_CAPS;
	}

	/* Do we want to advertise 10 Mb Full Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_10_FULL) {
		DEBUGOUT("Advertise 10mb Full duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_10T_FD_CAPS;
	}

	/* Do we want to advertise 100 Mb Half Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_100_HALF) {
		DEBUGOUT("Advertise 100mb Half duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_100TX_HD_CAPS;
	}

	/* Do we want to advertise 100 Mb Full Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_100_FULL) {
		DEBUGOUT("Advertise 100mb Full duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_100TX_FD_CAPS;
	}

	/* We do not allow the Phy to advertise 1000 Mb Half Duplex */
	if (phy->autoneg_advertised & ADVERTISE_1000_HALF)
		DEBUGOUT("Advertise 1000mb Half duplex request denied!\n");

	/* Do we want to advertise 1000 Mb Full Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_1000_FULL) {
		DEBUGOUT("Advertise 1000mb Full duplex\n");
		mii_1000t_ctrl_reg |= CR_1000T_FD_CAPS;
	}

	/* We do not allow the Phy to advertise 2500 Mb Half Duplex */
	if (phy->autoneg_advertised & ADVERTISE_2500_HALF)
		DEBUGOUT("Advertise 2500mb Half duplex request denied!\n");

	/* Do we want to advertise 2500 Mb Full Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_2500_FULL) {
		DEBUGOUT("Advertise 2500mb Full duplex\n");
		aneg_multigbt_an_ctrl |= CR_2500T_FD_CAPS;
	} else
		aneg_multigbt_an_ctrl &= ~CR_2500T_FD_CAPS;

	/* Check for a software override of the flow control settings, and
	 * setup the PHY advertisement registers accordingly.  If
	 * auto-negotiation is enabled, then software will have to set the
	 * "PAUSE" bits to the correct value in the Auto-Negotiation
	 * Advertisement Register (PHY_AUTONEG_ADV) and re-start auto-
	 * negotiation.
	 *
	 * The possible values of the "fc" parameter are:
	 *      0:  Flow control is completely disabled
	 *      1:  Rx flow control is enabled (we can receive pause frames
	 *          but not send pause frames).
	 *      2:  Tx flow control is enabled (we can send pause frames
	 *          but we do not support receiving pause frames).
	 *      3:  Both Rx and Tx flow control (symmetric) are enabled.
	 *  other:  No software override.  The flow control configuration
	 *          in the EEPROM is used.
	 */
	switch (hw->fc.current_mode) {
	case igc_fc_none:
		/* Flow control (Rx & Tx) is completely disabled by a
		 * software over-ride.
		 */
		mii_autoneg_adv_reg &= ~(NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	case igc_fc_rx_pause:
		/* Rx Flow control is enabled, and Tx Flow control is
		 * disabled, by a software over-ride.
		 *
		 * Since there really isn't a way to advertise that we are
		 * capable of Rx Pause ONLY, we will advertise that we
		 * support both symmetric and asymmetric Rx PAUSE.  Later
		 * (in igc_config_fc_after_link_up) we will disable the
		 * hw's ability to send PAUSE frames.
		 */
		mii_autoneg_adv_reg |= (NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	case igc_fc_tx_pause:
		/* Tx Flow control is enabled, and Rx Flow control is
		 * disabled, by a software over-ride.
		 */
		mii_autoneg_adv_reg |= NWAY_AR_ASM_DIR;
		mii_autoneg_adv_reg &= ~NWAY_AR_PAUSE;
		break;
	case igc_fc_full:
		/* Flow control (both Rx and Tx) is enabled by a software
		 * over-ride.
		 */
		mii_autoneg_adv_reg |= (NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	default:
		DEBUGOUT("Flow control param set incorrectly\n");
		return -IGC_ERR_CONFIG;
	}

	ret_val = phy->ops.write_reg(hw, PHY_AUTONEG_ADV, mii_autoneg_adv_reg);
	if (ret_val)
		return ret_val;

	DEBUGOUT1("Auto-Neg Advertising %x\n", mii_autoneg_adv_reg);

	if (phy->autoneg_mask & ADVERTISE_1000_FULL)
		ret_val = phy->ops.write_reg(hw, PHY_1000T_CTRL,
		    mii_1000t_ctrl_reg);

	if (phy->autoneg_mask & ADVERTISE_2500_FULL)
		ret_val = phy->ops.write_reg(hw,
		    (STANDARD_AN_REG_MASK << MMD_DEVADDR_SHIFT) |
		    ANEG_MULTIGBT_AN_CTRL, aneg_multigbt_an_ctrl);

	return ret_val;
}

/**
 *  igc_copper_link_autoneg - Setup/Enable autoneg for copper link
 *  @hw: pointer to the HW structure
 *
 *  Performs initial bounds checking on autoneg advertisement parameter, then
 *  configure to advertise the full capability.  Setup the PHY to autoneg
 *  and restart the negotiation process between the link partner.  If
 *  autoneg_wait_to_complete, then wait for autoneg to complete before exiting.
 **/
int
igc_copper_link_autoneg(struct igc_hw *hw)
{
	struct igc_phy_info *phy = &hw->phy;
	uint16_t phy_ctrl;
	int ret_val;

	DEBUGFUNC("igc_copper_link_autoneg");

	/* Perform some bounds checking on the autoneg advertisement
	 * parameter.
	 */
	phy->autoneg_advertised &= phy->autoneg_mask;

	/* If autoneg_advertised is zero, we assume it was not defaulted
	 * by the calling code so we set to advertise full capability.
	 */
	if (!phy->autoneg_advertised)
		phy->autoneg_advertised = phy->autoneg_mask;

	DEBUGOUT("Reconfiguring auto-neg advertisement params\n");
	ret_val = igc_phy_setup_autoneg(hw);
	if (ret_val) {
		DEBUGOUT("Error Setting up Auto-Negotiation\n");
		return ret_val;
	}
	DEBUGOUT("Restarting Auto-Neg\n");

	/* Restart auto-negotiation by setting the Auto Neg Enable bit and
	 * the Auto Neg Restart bit in the PHY control register.
	 */
	ret_val = phy->ops.read_reg(hw, PHY_CONTROL, &phy_ctrl);
	if (ret_val)
		return ret_val;

	phy_ctrl |= (MII_CR_AUTO_NEG_EN | MII_CR_RESTART_AUTO_NEG);
	ret_val = phy->ops.write_reg(hw, PHY_CONTROL, phy_ctrl);
	if (ret_val)
		return ret_val;

	/* Does the user want to wait for Auto-Neg to complete here, or
	 * check at a later time (for example, callback routine).
	 */
	if (phy->autoneg_wait_to_complete) {
		ret_val = igc_wait_autoneg(hw);
		if (ret_val)
			return ret_val;
	}

	hw->mac.get_link_status = true;

	return ret_val;
}

/**
 *  igc_setup_copper_link_generic - Configure copper link settings
 *  @hw: pointer to the HW structure
 *
 *  Calls the appropriate function to configure the link for auto-neg or forced
 *  speed and duplex.  Then we check for link, once link is established calls
 *  to configure collision distance and flow control are called.  If link is
 *  not established, we return -IGC_ERR_PHY (-2).
 **/
int
igc_setup_copper_link_generic(struct igc_hw *hw)
{
	int ret_val;
	bool link;

	DEBUGFUNC("igc_setup_copper_link_generic");

	if (hw->mac.autoneg) {
		/* Setup autoneg and flow control advertisement and perform
		 * autonegotiation.
		 */
		ret_val = igc_copper_link_autoneg(hw);
		if (ret_val)
			return ret_val;
	} else {
		/* PHY will be set to 10H, 10F, 100H or 100F
		 * depending on user settings.
		 */
		DEBUGOUT("Forcing Speed and Duplex\n");
		ret_val = hw->phy.ops.force_speed_duplex(hw);
		if (ret_val) {
			DEBUGOUT("Error Forcing Speed and Duplex\n");
			return ret_val;
		}
	}

	/* Check link status. Wait up to 100 microseconds for link to become
	 * valid.
	 */
	ret_val = igc_phy_has_link_generic(hw, COPPER_LINK_UP_LIMIT, 10,
	    &link);
	if (ret_val)
		return ret_val;

	if (link) {
		DEBUGOUT("Valid link established!!!\n");
		hw->mac.ops.config_collision_dist(hw);
		ret_val = igc_config_fc_after_link_up_generic(hw);
	} else
		DEBUGOUT("Unable to establish link!!!\n");

	return ret_val;
}

/**
 *  igc_check_downshift_generic - Checks whether a downshift in speed occurred
 *  @hw: pointer to the HW structure
 *
 *  Success returns 0, Failure returns 1
 *
 *  A downshift is detected by querying the PHY link health.
 **/
int
igc_check_downshift_generic(struct igc_hw *hw)
{
	struct igc_phy_info *phy = &hw->phy;
	int ret_val;

	DEBUGFUNC("igc_check_downshift_generic");

	switch (phy->type) {
	case igc_phy_i225:
	default:
		/* speed downshift not supported */
		phy->speed_downgraded = false;
		return IGC_SUCCESS;
	}

	return ret_val;
}

/**
 *  igc_wait_autoneg - Wait for auto-neg completion
 *  @hw: pointer to the HW structure
 *
 *  Waits for auto-negotiation to complete or for the auto-negotiation time
 *  limit to expire, which ever happens first.
 **/
int
igc_wait_autoneg(struct igc_hw *hw)
{
	uint16_t i, phy_status;
	int ret_val = IGC_SUCCESS;

	DEBUGFUNC("igc_wait_autoneg");

	if (!hw->phy.ops.read_reg)
		return IGC_SUCCESS;

	/* Break after autoneg completes or PHY_AUTO_NEG_LIMIT expires. */
	for (i = PHY_AUTO_NEG_LIMIT; i > 0; i--) {
		ret_val = hw->phy.ops.read_reg(hw, PHY_STATUS, &phy_status);
		if (ret_val)
			break;
		ret_val = hw->phy.ops.read_reg(hw, PHY_STATUS, &phy_status);
		if (ret_val)
			break;
		if (phy_status & MII_SR_AUTONEG_COMPLETE)
			break;
		msec_delay(100);
	}

	/* PHY_AUTO_NEG_TIME expiration doesn't guarantee auto-negotiation
	 * has completed.
	 */
	return ret_val;
}

/**
 *  igc_phy_has_link_generic - Polls PHY for link
 *  @hw: pointer to the HW structure
 *  @iterations: number of times to poll for link
 *  @usec_interval: delay between polling attempts
 *  @success: pointer to whether polling was successful or not
 *
 *  Polls the PHY status register for link, 'iterations' number of times.
 **/
int
igc_phy_has_link_generic(struct igc_hw *hw, uint32_t iterations,
    uint32_t usec_interval, bool *success)
{
	uint16_t i, phy_status;
	int ret_val = IGC_SUCCESS;

	DEBUGFUNC("igc_phy_has_link_generic");

	if (!hw->phy.ops.read_reg)
		return IGC_SUCCESS;

	for (i = 0; i < iterations; i++) {
		/* Some PHYs require the PHY_STATUS register to be read
		 * twice due to the link bit being sticky.  No harm doing
		 * it across the board.
		 */
		ret_val = hw->phy.ops.read_reg(hw, PHY_STATUS, &phy_status);
		if (ret_val) {
			/* If the first read fails, another entity may have
			 * ownership of the resources, wait and try again to
			 * see if they have relinquished the resources yet.
			 */
			if (usec_interval >= 1000)
				msec_delay(usec_interval/1000);
			else
				DELAY(usec_interval);
		}
		ret_val = hw->phy.ops.read_reg(hw, PHY_STATUS, &phy_status);
		if (ret_val)
			break;
		if (phy_status & MII_SR_LINK_STATUS)
			break;
		if (usec_interval >= 1000)
			msec_delay(usec_interval/1000);
		else
			DELAY(usec_interval);
	}

	*success = (i < iterations);

	return ret_val;
}

/**
 *  igc_phy_hw_reset_generic - PHY hardware reset
 *  @hw: pointer to the HW structure
 *
 *  Verify the reset block is not blocking us from resetting.  Acquire
 *  semaphore (if necessary) and read/set/write the device control reset
 *  bit in the PHY.  Wait the appropriate delay time for the device to
 *  reset and release the semaphore (if necessary).
 **/
int
igc_phy_hw_reset_generic(struct igc_hw *hw)
{
	struct igc_phy_info *phy = &hw->phy;
	uint32_t ctrl, timeout = 10000, phpm = 0;
	int ret_val;

	DEBUGFUNC("igc_phy_hw_reset_generic");

	if (phy->ops.check_reset_block) {
		ret_val = phy->ops.check_reset_block(hw);
		if (ret_val)
			return IGC_SUCCESS;
	}

	ret_val = phy->ops.acquire(hw);
	if (ret_val)
		return ret_val;

	phpm = IGC_READ_REG(hw, IGC_I225_PHPM);

	ctrl = IGC_READ_REG(hw, IGC_CTRL);
	IGC_WRITE_REG(hw, IGC_CTRL, ctrl | IGC_CTRL_PHY_RST);
	IGC_WRITE_FLUSH(hw);

	DELAY(phy->reset_delay_us);

	IGC_WRITE_REG(hw, IGC_CTRL, ctrl);
	IGC_WRITE_FLUSH(hw);

	DELAY(150);

	do {
		phpm = IGC_READ_REG(hw, IGC_I225_PHPM);
		timeout--;
		DELAY(1);
	} while (!(phpm & IGC_I225_PHPM_RST_COMPL) && timeout);

	if (!timeout)
		DEBUGOUT("Timeout expired after a phy reset\n");

	phy->ops.release(hw);

	return ret_val;
}

/**
 * igc_power_up_phy_copper - Restore copper link in case of PHY power down
 * @hw: pointer to the HW structure
 *
 * In the case of a PHY power down to save power, or to turn off link during a
 * driver unload, or wake on lan is not enabled, restore the link to previous
 * settings.
 **/
void
igc_power_up_phy_copper(struct igc_hw *hw)
{
	uint16_t mii_reg = 0;

	/* The PHY will retain its settings across a power down/up cycle */
	hw->phy.ops.read_reg(hw, PHY_CONTROL, &mii_reg);
	mii_reg &= ~MII_CR_POWER_DOWN;
	hw->phy.ops.write_reg(hw, PHY_CONTROL, mii_reg);
	DELAY(300);
}

/**
 * igc_power_down_phy_copper - Restore copper link in case of PHY power down
 * @hw: pointer to the HW structure
 *
 * In the case of a PHY power down to save power, or to turn off link during a
 * driver unload, or wake on lan is not enabled, restore the link to previous
 * settings.
 **/
void
igc_power_down_phy_copper(struct igc_hw *hw)
{
	uint16_t mii_reg = 0;

	/* The PHY will retain its settings across a power down/up cycle */
	hw->phy.ops.read_reg(hw, PHY_CONTROL, &mii_reg);
	mii_reg |= MII_CR_POWER_DOWN;
	hw->phy.ops.write_reg(hw, PHY_CONTROL, mii_reg);
	msec_delay(1);
}

/**
 *  igc_write_phy_reg_gpy - Write GPY PHY register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Acquires semaphore, if necessary, then writes the data to PHY register
 *  at the offset.  Release any acquired semaphores before exiting.
 **/
int
igc_write_phy_reg_gpy(struct igc_hw *hw, uint32_t offset, uint16_t data)
{
	uint8_t dev_addr = (offset & GPY_MMD_MASK) >> GPY_MMD_SHIFT;
	int ret_val;

	DEBUGFUNC("igc_write_phy_reg_gpy");

	offset = offset & GPY_REG_MASK;

	if (!dev_addr) {
		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val)
			return ret_val;
		ret_val = igc_write_phy_reg_mdic(hw, offset, data);
		if (ret_val)
			return ret_val;
		hw->phy.ops.release(hw);
	} else {
		ret_val = igc_write_xmdio_reg(hw, (uint16_t)offset, dev_addr,
		    data);
	}

	return ret_val;
}

/**
 *  igc_read_phy_reg_gpy - Read GPY PHY register
 *  @hw: pointer to the HW structure
 *  @offset: lower half is register offset to read to
 *     upper half is MMD to use.
 *  @data: data to read at register offset
 *
 *  Acquires semaphore, if necessary, then reads the data in the PHY register
 *  at the offset.  Release any acquired semaphores before exiting.
 **/
int
igc_read_phy_reg_gpy(struct igc_hw *hw, uint32_t offset, uint16_t *data)
{
	uint8_t dev_addr = (offset & GPY_MMD_MASK) >> GPY_MMD_SHIFT;
	int ret_val;

	DEBUGFUNC("igc_read_phy_reg_gpy");

	offset = offset & GPY_REG_MASK;

	if (!dev_addr) {
		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val)
			return ret_val;
		ret_val = igc_read_phy_reg_mdic(hw, offset, data);
		if (ret_val)
			return ret_val;
		hw->phy.ops.release(hw);
	} else {
		ret_val = igc_read_xmdio_reg(hw, (uint16_t)offset, dev_addr,
		    data);
	}

	return ret_val;
}

/**
 *  __igc_access_xmdio_reg - Read/write XMDIO register
 *  @hw: pointer to the HW structure
 *  @address: XMDIO address to program
 *  @dev_addr: device address to program
 *  @data: pointer to value to read/write from/to the XMDIO address
 *  @read: boolean flag to indicate read or write
 **/
int
__igc_access_xmdio_reg(struct igc_hw *hw, uint16_t address, uint8_t dev_addr,
    uint16_t *data, bool read)
{
	int ret_val;

	DEBUGFUNC("__igc_access_xmdio_reg");

	ret_val = hw->phy.ops.write_reg(hw, IGC_MMDAC, dev_addr);
	if (ret_val)
		return ret_val;

	ret_val = hw->phy.ops.write_reg(hw, IGC_MMDAAD, address);
	if (ret_val)
		return ret_val;

	ret_val = hw->phy.ops.write_reg(hw, IGC_MMDAC, IGC_MMDAC_FUNC_DATA |
	    dev_addr);
	if (ret_val)
		return ret_val;

	if (read)
		ret_val = hw->phy.ops.read_reg(hw, IGC_MMDAAD, data);
	else
		ret_val = hw->phy.ops.write_reg(hw, IGC_MMDAAD, *data);
	if (ret_val)
		return ret_val;

	/* Recalibrate the device back to 0 */
	ret_val = hw->phy.ops.write_reg(hw, IGC_MMDAC, 0);
	if (ret_val)
		return ret_val;

	return ret_val;
}

/**
 *  igc_read_xmdio_reg - Read XMDIO register
 *  @hw: pointer to the HW structure
 *  @addr: XMDIO address to program
 *  @dev_addr: device address to program
 *  @data: value to be read from the EMI address
 **/
int
igc_read_xmdio_reg(struct igc_hw *hw, uint16_t addr, uint8_t dev_addr,
    uint16_t *data)
{
	DEBUGFUNC("igc_read_xmdio_reg");

	return __igc_access_xmdio_reg(hw, addr, dev_addr, data, true);
}

/**
 *  igc_write_xmdio_reg - Write XMDIO register
 *  @hw: pointer to the HW structure
 *  @addr: XMDIO address to program
 *  @dev_addr: device address to program
 *  @data: value to be written to the XMDIO address
 **/
int
igc_write_xmdio_reg(struct igc_hw *hw, uint16_t addr, uint8_t dev_addr,
    uint16_t data)
{
	DEBUGFUNC("igc_write_xmdio_reg");

	return __igc_access_xmdio_reg(hw, addr, dev_addr, &data, false);
}
