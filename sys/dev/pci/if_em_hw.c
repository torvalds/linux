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

/* $OpenBSD: if_em_hw.c,v 1.124 2025/07/14 23:49:08 jsg Exp $ */
/*
 * if_em_hw.c Shared functions for accessing and configuring the MAC
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/pci/if_em.h>
#include <dev/pci/if_em_hw.h>
#include <dev/pci/if_em_soc.h>

#include <dev/mii/rgephyreg.h>

#define STATIC

static int32_t	em_swfw_sync_acquire(struct em_hw *, uint16_t);
static void	em_swfw_sync_release(struct em_hw *, uint16_t);
static int32_t	em_read_kmrn_reg(struct em_hw *, uint32_t, uint16_t *);
static int32_t	em_write_kmrn_reg(struct em_hw *hw, uint32_t, uint16_t);
static int32_t	em_get_software_semaphore(struct em_hw *);
static void	em_release_software_semaphore(struct em_hw *);

static int32_t	em_check_downshift(struct em_hw *);
static void	em_clear_vfta(struct em_hw *);
void		em_clear_vfta_i350(struct em_hw *);
static int32_t	em_commit_shadow_ram(struct em_hw *);
static int32_t	em_config_dsp_after_link_change(struct em_hw *, boolean_t);
static int32_t	em_config_fc_after_link_up(struct em_hw *);
static int32_t	em_match_gig_phy(struct em_hw *);
static int32_t	em_detect_gig_phy(struct em_hw *);
static int32_t	em_erase_ich8_4k_segment(struct em_hw *, uint32_t);
static int32_t	em_get_auto_rd_done(struct em_hw *);
static int32_t	em_get_cable_length(struct em_hw *, uint16_t *, uint16_t *);
static int32_t	em_get_hw_eeprom_semaphore(struct em_hw *);
static int32_t	em_get_phy_cfg_done(struct em_hw *);
static int32_t	em_get_software_flag(struct em_hw *);
static int32_t	em_ich8_cycle_init(struct em_hw *);
static int32_t	em_ich8_flash_cycle(struct em_hw *, uint32_t);
static int32_t	em_id_led_init(struct em_hw *);
static int32_t	em_init_lcd_from_nvm_config_region(struct em_hw *,  uint32_t,
		    uint32_t);
static int32_t	em_init_lcd_from_nvm(struct em_hw *);
static int32_t	em_phy_no_cable_workaround(struct em_hw *);
static void	em_init_rx_addrs(struct em_hw *);
static void	em_initialize_hardware_bits(struct em_softc *);
static void	em_toggle_lanphypc_pch_lpt(struct em_hw *);
static int	em_disable_ulp_lpt_lp(struct em_hw *hw, bool force);
static boolean_t em_is_onboard_nvm_eeprom(struct em_hw *);
static int32_t	em_kumeran_lock_loss_workaround(struct em_hw *);
static int32_t	em_mng_enable_host_if(struct em_hw *);
static int32_t	em_read_eeprom_eerd(struct em_hw *, uint16_t, uint16_t, 
		    uint16_t *);
static int32_t	em_write_eeprom_eewr(struct em_hw *, uint16_t, uint16_t,
		    uint16_t *data);
static int32_t	em_poll_eerd_eewr_done(struct em_hw *, int);
static void	em_put_hw_eeprom_semaphore(struct em_hw *);
static int32_t	em_read_ich8_byte(struct em_hw *, uint32_t, uint8_t *);
static int32_t	em_verify_write_ich8_byte(struct em_hw *, uint32_t, uint8_t);
static int32_t	em_write_ich8_byte(struct em_hw *, uint32_t, uint8_t);
static int32_t	em_read_ich8_word(struct em_hw *, uint32_t, uint16_t *);
static int32_t	em_read_ich8_dword(struct em_hw *, uint32_t, uint32_t *);
static int32_t	em_read_ich8_data(struct em_hw *, uint32_t, uint32_t,
		    uint16_t *);
static int32_t	em_write_ich8_data(struct em_hw *, uint32_t, uint32_t,
		    uint16_t);
static int32_t	em_read_eeprom_ich8(struct em_hw *, uint16_t, uint16_t,
		    uint16_t *);
static int32_t	em_write_eeprom_ich8(struct em_hw *, uint16_t, uint16_t,
		    uint16_t *);
static int32_t	em_read_invm_i210(struct em_hw *, uint16_t, uint16_t,
		    uint16_t *);
static int32_t	em_read_invm_word_i210(struct em_hw *, uint16_t, uint16_t *);
static void	em_release_software_flag(struct em_hw *);
static int32_t	em_set_d3_lplu_state(struct em_hw *, boolean_t);
static int32_t	em_set_d0_lplu_state(struct em_hw *, boolean_t);
static int32_t	em_set_lplu_state_pchlan(struct em_hw *, boolean_t);
static int32_t	em_set_pci_ex_no_snoop(struct em_hw *, uint32_t);
static void	em_set_pci_express_master_disable(struct em_hw *);
static int32_t	em_wait_autoneg(struct em_hw *);
static void	em_write_reg_io(struct em_hw *, uint32_t, uint32_t);
static int32_t	em_set_phy_type(struct em_hw *);
static void	em_phy_init_script(struct em_hw *);
static int32_t	em_setup_copper_link(struct em_hw *);
static int32_t	em_setup_fiber_serdes_link(struct em_hw *);
static int32_t	em_adjust_serdes_amplitude(struct em_hw *);
static int32_t	em_phy_force_speed_duplex(struct em_hw *);
static int32_t	em_config_mac_to_phy(struct em_hw *);
static void	em_raise_mdi_clk(struct em_hw *, uint32_t *);
static void	em_lower_mdi_clk(struct em_hw *, uint32_t *);
static void	em_shift_out_mdi_bits(struct em_hw *, uint32_t, uint16_t);
static uint16_t	em_shift_in_mdi_bits(struct em_hw *);
static int32_t	em_phy_reset_dsp(struct em_hw *);
static int32_t	em_write_eeprom_spi(struct em_hw *, uint16_t, uint16_t,
		    uint16_t *);
static int32_t	em_write_eeprom_microwire(struct em_hw *, uint16_t, uint16_t,
		    uint16_t *);
static int32_t	em_spi_eeprom_ready(struct em_hw *);
static void	em_raise_ee_clk(struct em_hw *, uint32_t *);
static void	em_lower_ee_clk(struct em_hw *, uint32_t *);
static void	em_shift_out_ee_bits(struct em_hw *, uint16_t, uint16_t);
static int32_t	em_write_phy_reg_ex(struct em_hw *, uint32_t, uint16_t);
static int32_t	em_read_phy_reg_ex(struct em_hw *, uint32_t, uint16_t *);
static uint16_t	em_shift_in_ee_bits(struct em_hw *, uint16_t);
static int32_t	em_acquire_eeprom(struct em_hw *);
static void	em_release_eeprom(struct em_hw *);
static void	em_standby_eeprom(struct em_hw *);
static int32_t	em_set_vco_speed(struct em_hw *);
static int32_t	em_polarity_reversal_workaround(struct em_hw *);
static int32_t	em_set_phy_mode(struct em_hw *);
static int32_t	em_host_if_read_cookie(struct em_hw *, uint8_t *);
static uint8_t	em_calculate_mng_checksum(char *, uint32_t);
static int32_t	em_configure_kmrn_for_10_100(struct em_hw *, uint16_t);
static int32_t	em_configure_kmrn_for_1000(struct em_hw *);
static int32_t	em_set_pciex_completion_timeout(struct em_hw *hw);
static int32_t	em_set_mdio_slow_mode_hv(struct em_hw *);
int32_t		em_hv_phy_workarounds_ich8lan(struct em_hw *);
int32_t		em_lv_phy_workarounds_ich8lan(struct em_hw *);
int32_t		em_link_stall_workaround_hv(struct em_hw *);
int32_t		em_k1_gig_workaround_hv(struct em_hw *, boolean_t);
int32_t		em_k1_workaround_lv(struct em_hw *);
int32_t		em_k1_workaround_lpt_lp(struct em_hw *, boolean_t);
int32_t		em_configure_k1_ich8lan(struct em_hw *, boolean_t);
void		em_gate_hw_phy_config_ich8lan(struct em_hw *, boolean_t);
int32_t		em_access_phy_wakeup_reg_bm(struct em_hw *, uint32_t,
		    uint16_t *, boolean_t);
int32_t		em_access_phy_debug_regs_hv(struct em_hw *, uint32_t,
		    uint16_t *, boolean_t);
int32_t		em_access_phy_reg_hv(struct em_hw *, uint32_t, uint16_t *,
		    boolean_t);
int32_t		em_oem_bits_config_pchlan(struct em_hw *, boolean_t);
void		em_power_up_serdes_link_82575(struct em_hw *);
int32_t		em_get_pcs_speed_and_duplex_82575(struct em_hw *, uint16_t *,
    uint16_t *);
int32_t		em_set_eee_i350(struct em_hw *);
int32_t		em_set_eee_pchlan(struct em_hw *);
int32_t		em_valid_nvm_bank_detect_ich8lan(struct em_hw *, uint32_t *);
int32_t		em_initialize_M88E1512_phy(struct em_hw *);

/* IGP cable length table */
static const uint16_t 
em_igp_cable_length_table[IGP01E1000_AGC_LENGTH_TABLE_SIZE] =
    {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 10, 10, 10, 10, 10, 10, 10, 20, 20, 20, 20, 20, 25, 25, 25,
    25, 25, 25, 25, 30, 30, 30, 30, 40, 40, 40, 40, 40, 40, 40, 40,
    40, 50, 50, 50, 50, 50, 50, 50, 60, 60, 60, 60, 60, 60, 60, 60,
    60, 70, 70, 70, 70, 70, 70, 80, 80, 80, 80, 80, 80, 90, 90, 90,
    90, 90, 90, 90, 90, 90, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
    100, 100, 100, 100, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110,
    110,
    110, 110, 110, 110, 110, 110, 120, 120, 120, 120, 120, 120, 120, 120, 120,
    120};

static const uint16_t 
em_igp_2_cable_length_table[IGP02E1000_AGC_LENGTH_TABLE_SIZE] =
    {0, 0, 0, 0, 0, 0, 0, 0, 3, 5, 8, 11, 13, 16, 18, 21,
    0, 0, 0, 3, 6, 10, 13, 16, 19, 23, 26, 29, 32, 35, 38, 41,
    6, 10, 14, 18, 22, 26, 30, 33, 37, 41, 44, 48, 51, 54, 58, 61,
    21, 26, 31, 35, 40, 44, 49, 53, 57, 61, 65, 68, 72, 75, 79, 82,
    40, 45, 51, 56, 61, 66, 70, 75, 79, 83, 87, 91, 94, 98, 101, 104,
    60, 66, 72, 77, 82, 87, 92, 96, 100, 104, 108, 111, 114, 117, 119, 121,
    83, 89, 95, 100, 105, 109, 113, 116, 119, 122, 124, 104, 109, 114, 118,
    121, 124};

/******************************************************************************
 * Set the phy type member in the hw struct.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
STATIC int32_t
em_set_phy_type(struct em_hw *hw)
{
	DEBUGFUNC("em_set_phy_type");

	if (hw->mac_type == em_undefined)
		return -E1000_ERR_PHY_TYPE;

	switch (hw->phy_id) {
	case M88E1000_E_PHY_ID:
	case M88E1000_I_PHY_ID:
	case M88E1011_I_PHY_ID:
	case M88E1111_I_PHY_ID:
	case M88E1112_E_PHY_ID:
	case M88E1543_E_PHY_ID:
	case M88E1512_E_PHY_ID:
	case I210_I_PHY_ID:
	case I347AT4_E_PHY_ID:
		hw->phy_type = em_phy_m88;
		break;
	case IGP01E1000_I_PHY_ID:
		if (hw->mac_type == em_82541 ||
		    hw->mac_type == em_82541_rev_2 ||
		    hw->mac_type == em_82547 ||
		    hw->mac_type == em_82547_rev_2) {
			hw->phy_type = em_phy_igp;
			break;
		}
	case IGP03E1000_E_PHY_ID:
	case IGP04E1000_E_PHY_ID:
		hw->phy_type = em_phy_igp_3;
		break;
	case IFE_E_PHY_ID:
	case IFE_PLUS_E_PHY_ID:
	case IFE_C_E_PHY_ID:
		hw->phy_type = em_phy_ife;
		break;
	case M88E1141_E_PHY_ID:
		hw->phy_type = em_phy_oem;
		break;
	case I82577_E_PHY_ID:
		hw->phy_type = em_phy_82577;
		break;
	case I82578_E_PHY_ID:
		hw->phy_type = em_phy_82578;
		break;
	case I82579_E_PHY_ID:
		hw->phy_type = em_phy_82579;
		break;
	case I217_E_PHY_ID:
		hw->phy_type = em_phy_i217;
		break;
	case I82580_I_PHY_ID:
	case I350_I_PHY_ID:
		hw->phy_type = em_phy_82580;
		break;
	case RTL8211_E_PHY_ID:
		hw->phy_type = em_phy_rtl8211;
		break;
	case BME1000_E_PHY_ID:
		if (hw->phy_revision == 1) {
			hw->phy_type = em_phy_bm;
			break;
		}
		/* FALLTHROUGH */
	case GG82563_E_PHY_ID:
		if (hw->mac_type == em_80003es2lan) {
			hw->phy_type = em_phy_gg82563;
			break;
		}
		/* FALLTHROUGH */
	default:
		/* Should never have loaded on this device */
		hw->phy_type = em_phy_undefined;
		return -E1000_ERR_PHY_TYPE;
	}

	return E1000_SUCCESS;
}

/******************************************************************************
 * IGP phy init script - initializes the GbE PHY
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
em_phy_init_script(struct em_hw *hw)
{
	uint16_t phy_saved_data;
	DEBUGFUNC("em_phy_init_script");

	if (hw->phy_init_script) {
		msec_delay(20);
		/*
		 * Save off the current value of register 0x2F5B to be
		 * restored at the end of this routine.
		 */
		em_read_phy_reg(hw, 0x2F5B, &phy_saved_data);

		/* Disabled the PHY transmitter */
		em_write_phy_reg(hw, 0x2F5B, 0x0003);
		msec_delay(20);
		em_write_phy_reg(hw, 0x0000, 0x0140);
		msec_delay(5);

		switch (hw->mac_type) {
		case em_82541:
		case em_82547:
			em_write_phy_reg(hw, 0x1F95, 0x0001);
			em_write_phy_reg(hw, 0x1F71, 0xBD21);
			em_write_phy_reg(hw, 0x1F79, 0x0018);
			em_write_phy_reg(hw, 0x1F30, 0x1600);
			em_write_phy_reg(hw, 0x1F31, 0x0014);
			em_write_phy_reg(hw, 0x1F32, 0x161C);
			em_write_phy_reg(hw, 0x1F94, 0x0003);
			em_write_phy_reg(hw, 0x1F96, 0x003F);
			em_write_phy_reg(hw, 0x2010, 0x0008);
			break;
		case em_82541_rev_2:
		case em_82547_rev_2:
			em_write_phy_reg(hw, 0x1F73, 0x0099);
			break;
		default:
			break;
		}

		em_write_phy_reg(hw, 0x0000, 0x3300);
		msec_delay(20);

		/* Now enable the transmitter */
		em_write_phy_reg(hw, 0x2F5B, phy_saved_data);

		if (hw->mac_type == em_82547) {
			uint16_t fused, fine, coarse;
			/* Move to analog registers page */
			em_read_phy_reg(hw, 
			    IGP01E1000_ANALOG_SPARE_FUSE_STATUS, &fused);

			if (!(fused & IGP01E1000_ANALOG_SPARE_FUSE_ENABLED)) {
				em_read_phy_reg(hw, 
				    IGP01E1000_ANALOG_FUSE_STATUS, &fused);

				fine = fused &
				    IGP01E1000_ANALOG_FUSE_FINE_MASK;
				coarse = fused &
				    IGP01E1000_ANALOG_FUSE_COARSE_MASK;

				if (coarse > 
				    IGP01E1000_ANALOG_FUSE_COARSE_THRESH) {
					coarse -= 
					    IGP01E1000_ANALOG_FUSE_COARSE_10;
					fine -= 
					    IGP01E1000_ANALOG_FUSE_FINE_1;
				} else if (coarse == 
				    IGP01E1000_ANALOG_FUSE_COARSE_THRESH)
					fine -= IGP01E1000_ANALOG_FUSE_FINE_10;

				fused = (fused & 
				    IGP01E1000_ANALOG_FUSE_POLY_MASK) |
				    (fine & 
				    IGP01E1000_ANALOG_FUSE_FINE_MASK) |
				    (coarse & 
				    IGP01E1000_ANALOG_FUSE_COARSE_MASK);

				em_write_phy_reg(hw, 
				    IGP01E1000_ANALOG_FUSE_CONTROL, 
				    fused);

				em_write_phy_reg(hw, 
				    IGP01E1000_ANALOG_FUSE_BYPASS,
				    IGP01E1000_ANALOG_FUSE_ENABLE_SW_CONTROL);
			}
		}
	}
}

/******************************************************************************
 * Set the mac type member in the hw struct.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
int32_t
em_set_mac_type(struct em_hw *hw)
{
	DEBUGFUNC("em_set_mac_type");

	switch (hw->device_id) {
	case E1000_DEV_ID_82542:
		switch (hw->revision_id) {
		case E1000_82542_2_0_REV_ID:
			hw->mac_type = em_82542_rev2_0;
			break;
		case E1000_82542_2_1_REV_ID:
			hw->mac_type = em_82542_rev2_1;
			break;
		default:
			/* Invalid 82542 revision ID */
			return -E1000_ERR_MAC_TYPE;
		}
		break;
	case E1000_DEV_ID_82543GC_FIBER:
	case E1000_DEV_ID_82543GC_COPPER:
		hw->mac_type = em_82543;
		break;
	case E1000_DEV_ID_82544EI_COPPER:
	case E1000_DEV_ID_82544EI_FIBER:
	case E1000_DEV_ID_82544GC_COPPER:
	case E1000_DEV_ID_82544GC_LOM:
		hw->mac_type = em_82544;
		break;
	case E1000_DEV_ID_82540EM:
	case E1000_DEV_ID_82540EM_LOM:
	case E1000_DEV_ID_82540EP:
	case E1000_DEV_ID_82540EP_LOM:
	case E1000_DEV_ID_82540EP_LP:
		hw->mac_type = em_82540;
		break;
	case E1000_DEV_ID_82545EM_COPPER:
	case E1000_DEV_ID_82545EM_FIBER:
		hw->mac_type = em_82545;
		break;
	case E1000_DEV_ID_82545GM_COPPER:
	case E1000_DEV_ID_82545GM_FIBER:
	case E1000_DEV_ID_82545GM_SERDES:
		hw->mac_type = em_82545_rev_3;
		break;
	case E1000_DEV_ID_82546EB_COPPER:
	case E1000_DEV_ID_82546EB_FIBER:
	case E1000_DEV_ID_82546EB_QUAD_COPPER:
		hw->mac_type = em_82546;
		break;
	case E1000_DEV_ID_82546GB_COPPER:
	case E1000_DEV_ID_82546GB_FIBER:
	case E1000_DEV_ID_82546GB_SERDES:
	case E1000_DEV_ID_82546GB_PCIE:
	case E1000_DEV_ID_82546GB_QUAD_COPPER:
	case E1000_DEV_ID_82546GB_QUAD_COPPER_KSP3:
	case E1000_DEV_ID_82546GB_2:
		hw->mac_type = em_82546_rev_3;
		break;
	case E1000_DEV_ID_82541EI:
	case E1000_DEV_ID_82541EI_MOBILE:
	case E1000_DEV_ID_82541ER_LOM:
		hw->mac_type = em_82541;
		break;
	case E1000_DEV_ID_82541ER:
	case E1000_DEV_ID_82541GI:
	case E1000_DEV_ID_82541GI_LF:
	case E1000_DEV_ID_82541GI_MOBILE:
		hw->mac_type = em_82541_rev_2;
		break;
	case E1000_DEV_ID_82547EI:
	case E1000_DEV_ID_82547EI_MOBILE:
		hw->mac_type = em_82547;
		break;
	case E1000_DEV_ID_82547GI:
		hw->mac_type = em_82547_rev_2;
		break;
	case E1000_DEV_ID_82571EB_AF:
	case E1000_DEV_ID_82571EB_AT:
	case E1000_DEV_ID_82571EB_COPPER:
	case E1000_DEV_ID_82571EB_FIBER:
	case E1000_DEV_ID_82571EB_SERDES:
	case E1000_DEV_ID_82571EB_QUAD_COPPER:
	case E1000_DEV_ID_82571EB_QUAD_FIBER:
	case E1000_DEV_ID_82571EB_QUAD_COPPER_LP:
	case E1000_DEV_ID_82571EB_SERDES_DUAL:
	case E1000_DEV_ID_82571EB_SERDES_QUAD:
	case E1000_DEV_ID_82571PT_QUAD_COPPER:
		hw->mac_type = em_82571;
		break;
	case E1000_DEV_ID_82572EI_COPPER:
	case E1000_DEV_ID_82572EI_FIBER:
	case E1000_DEV_ID_82572EI_SERDES:
	case E1000_DEV_ID_82572EI:
		hw->mac_type = em_82572;
		break;
	case E1000_DEV_ID_82573E:
	case E1000_DEV_ID_82573E_IAMT:
	case E1000_DEV_ID_82573E_PM:
	case E1000_DEV_ID_82573L:
	case E1000_DEV_ID_82573L_PL_1:
	case E1000_DEV_ID_82573L_PL_2:
	case E1000_DEV_ID_82573V_PM:
		hw->mac_type = em_82573;
		break;
	case E1000_DEV_ID_82574L:
	case E1000_DEV_ID_82574LA:
	case E1000_DEV_ID_82583V:
		hw->mac_type = em_82574;
		break;
	case E1000_DEV_ID_82575EB_PT:
	case E1000_DEV_ID_82575EB_PF:
	case E1000_DEV_ID_82575GB_QP:
	case E1000_DEV_ID_82575GB_QP_PM:
		hw->mac_type = em_82575;
		hw->initialize_hw_bits_disable = 1;
		break;
	case E1000_DEV_ID_82576:
	case E1000_DEV_ID_82576_FIBER:
	case E1000_DEV_ID_82576_SERDES:
	case E1000_DEV_ID_82576_QUAD_COPPER:
	case E1000_DEV_ID_82576_QUAD_CU_ET2:
	case E1000_DEV_ID_82576_NS:
	case E1000_DEV_ID_82576_NS_SERDES:
	case E1000_DEV_ID_82576_SERDES_QUAD:
		hw->mac_type = em_82576;
		hw->initialize_hw_bits_disable = 1;
		break;
	case E1000_DEV_ID_82580_COPPER:
	case E1000_DEV_ID_82580_FIBER:
	case E1000_DEV_ID_82580_QUAD_FIBER:
	case E1000_DEV_ID_82580_SERDES:
	case E1000_DEV_ID_82580_SGMII:
	case E1000_DEV_ID_82580_COPPER_DUAL:
	case E1000_DEV_ID_DH89XXCC_SGMII:
	case E1000_DEV_ID_DH89XXCC_SERDES:
	case E1000_DEV_ID_DH89XXCC_BACKPLANE:
	case E1000_DEV_ID_DH89XXCC_SFP:
		hw->mac_type = em_82580;
		hw->initialize_hw_bits_disable = 1;
		break;
	case E1000_DEV_ID_I210_COPPER:
	case E1000_DEV_ID_I210_COPPER_OEM1:
	case E1000_DEV_ID_I210_COPPER_IT:
	case E1000_DEV_ID_I210_FIBER:
	case E1000_DEV_ID_I210_SERDES:
	case E1000_DEV_ID_I210_SGMII:
	case E1000_DEV_ID_I210_COPPER_FLASHLESS:
	case E1000_DEV_ID_I210_SERDES_FLASHLESS:
	case E1000_DEV_ID_I211_COPPER:
		hw->mac_type = em_i210;
		hw->initialize_hw_bits_disable = 1;
		hw->eee_enable = 1;
		break;
	case E1000_DEV_ID_I350_COPPER:
	case E1000_DEV_ID_I350_FIBER:
	case E1000_DEV_ID_I350_SERDES:
	case E1000_DEV_ID_I350_SGMII:
	case E1000_DEV_ID_I350_DA4:
	case E1000_DEV_ID_I354_BACKPLANE_1GBPS:
	case E1000_DEV_ID_I354_SGMII:
	case E1000_DEV_ID_I354_BACKPLANE_2_5GBPS:
		hw->mac_type = em_i350;
		hw->initialize_hw_bits_disable = 1;
		hw->eee_enable = 1;
		break;
	case E1000_DEV_ID_80003ES2LAN_COPPER_SPT:
	case E1000_DEV_ID_80003ES2LAN_SERDES_SPT:
	case E1000_DEV_ID_80003ES2LAN_COPPER_DPT:
	case E1000_DEV_ID_80003ES2LAN_SERDES_DPT:
		hw->mac_type = em_80003es2lan;
		break;
	case E1000_DEV_ID_ICH8_IFE:
	case E1000_DEV_ID_ICH8_IFE_G:
	case E1000_DEV_ID_ICH8_IFE_GT:
	case E1000_DEV_ID_ICH8_IGP_AMT:
	case E1000_DEV_ID_ICH8_IGP_C:
	case E1000_DEV_ID_ICH8_IGP_M:
	case E1000_DEV_ID_ICH8_IGP_M_AMT:
	case E1000_DEV_ID_ICH8_82567V_3:
		hw->mac_type = em_ich8lan;
		break;
	case E1000_DEV_ID_ICH9_BM:
	case E1000_DEV_ID_ICH9_IFE:
	case E1000_DEV_ID_ICH9_IFE_G:
	case E1000_DEV_ID_ICH9_IFE_GT:
	case E1000_DEV_ID_ICH9_IGP_AMT:
	case E1000_DEV_ID_ICH9_IGP_C:
	case E1000_DEV_ID_ICH9_IGP_M:
	case E1000_DEV_ID_ICH9_IGP_M_AMT:
	case E1000_DEV_ID_ICH9_IGP_M_V:
	case E1000_DEV_ID_ICH10_R_BM_LF:
	case E1000_DEV_ID_ICH10_R_BM_LM:
	case E1000_DEV_ID_ICH10_R_BM_V:
		hw->mac_type = em_ich9lan;
		break;
	case E1000_DEV_ID_ICH10_D_BM_LF:
	case E1000_DEV_ID_ICH10_D_BM_LM:
	case E1000_DEV_ID_ICH10_D_BM_V:
		hw->mac_type = em_ich10lan;
		break;
	case E1000_DEV_ID_PCH_M_HV_LC:
	case E1000_DEV_ID_PCH_M_HV_LM:
	case E1000_DEV_ID_PCH_D_HV_DC:
	case E1000_DEV_ID_PCH_D_HV_DM:
		hw->mac_type = em_pchlan;
		hw->eee_enable = 1;
		break;
	case E1000_DEV_ID_PCH2_LV_LM:
	case E1000_DEV_ID_PCH2_LV_V:
		hw->mac_type = em_pch2lan;
		break;
	case E1000_DEV_ID_PCH_LPT_I217_LM:
	case E1000_DEV_ID_PCH_LPT_I217_V:
	case E1000_DEV_ID_PCH_LPTLP_I218_LM:
	case E1000_DEV_ID_PCH_LPTLP_I218_V:
	case E1000_DEV_ID_PCH_I218_LM2:
	case E1000_DEV_ID_PCH_I218_V2:
	case E1000_DEV_ID_PCH_I218_LM3:
	case E1000_DEV_ID_PCH_I218_V3:
		hw->mac_type = em_pch_lpt;
		break;
	case E1000_DEV_ID_PCH_SPT_I219_LM:
	case E1000_DEV_ID_PCH_SPT_I219_V:
	case E1000_DEV_ID_PCH_SPT_I219_LM2:
	case E1000_DEV_ID_PCH_SPT_I219_V2:
	case E1000_DEV_ID_PCH_LBG_I219_LM3:
	case E1000_DEV_ID_PCH_SPT_I219_LM4:
	case E1000_DEV_ID_PCH_SPT_I219_V4:
	case E1000_DEV_ID_PCH_SPT_I219_LM5:
	case E1000_DEV_ID_PCH_SPT_I219_V5:
	case E1000_DEV_ID_PCH_CMP_I219_LM12:
	case E1000_DEV_ID_PCH_CMP_I219_V12:
		hw->mac_type = em_pch_spt;
		break;
	case E1000_DEV_ID_PCH_CNP_I219_LM6:
	case E1000_DEV_ID_PCH_CNP_I219_V6:
	case E1000_DEV_ID_PCH_CNP_I219_LM7:
	case E1000_DEV_ID_PCH_CNP_I219_V7:
	case E1000_DEV_ID_PCH_ICP_I219_LM8:
	case E1000_DEV_ID_PCH_ICP_I219_V8:
	case E1000_DEV_ID_PCH_ICP_I219_LM9:
	case E1000_DEV_ID_PCH_ICP_I219_V9:
	case E1000_DEV_ID_PCH_CMP_I219_LM10:
	case E1000_DEV_ID_PCH_CMP_I219_V10:
	case E1000_DEV_ID_PCH_CMP_I219_LM11:
	case E1000_DEV_ID_PCH_CMP_I219_V11:
		hw->mac_type = em_pch_cnp;
		break;
	case E1000_DEV_ID_PCH_TGP_I219_LM13:
	case E1000_DEV_ID_PCH_TGP_I219_V13:
	case E1000_DEV_ID_PCH_TGP_I219_LM14:
	case E1000_DEV_ID_PCH_TGP_I219_V14:
	case E1000_DEV_ID_PCH_TGP_I219_LM15:
	case E1000_DEV_ID_PCH_TGP_I219_V15:
		hw->mac_type = em_pch_tgp;
		break;
	case E1000_DEV_ID_PCH_ADP_I219_LM16:
	case E1000_DEV_ID_PCH_ADP_I219_V16:
	case E1000_DEV_ID_PCH_ADP_I219_LM17:
	case E1000_DEV_ID_PCH_ADP_I219_V17:
	case E1000_DEV_ID_PCH_RPL_I219_LM22:
	case E1000_DEV_ID_PCH_RPL_I219_V22:
	case E1000_DEV_ID_PCH_RPL_I219_LM23:
	case E1000_DEV_ID_PCH_RPL_I219_V23:
		hw->mac_type = em_pch_adp;
		break;
	case E1000_DEV_ID_PCH_MTP_I219_LM18:
	case E1000_DEV_ID_PCH_MTP_I219_V18:
	case E1000_DEV_ID_PCH_MTP_I219_LM19:
	case E1000_DEV_ID_PCH_MTP_I219_V19:
	case E1000_DEV_ID_PCH_LNP_I219_LM20:
	case E1000_DEV_ID_PCH_LNP_I219_V20:
	case E1000_DEV_ID_PCH_LNP_I219_LM21:
	case E1000_DEV_ID_PCH_LNP_I219_V21:
	case E1000_DEV_ID_PCH_ARL_I219_LM24:
	case E1000_DEV_ID_PCH_ARL_I219_V24:
		hw->mac_type = em_pch_adp;	/* pch_mtp */
		break;
	case E1000_DEV_ID_EP80579_LAN_1:
		hw->mac_type = em_icp_xxxx;
		hw->icp_xxxx_port_num = 0;
		break;
	case E1000_DEV_ID_EP80579_LAN_2:
	case E1000_DEV_ID_EP80579_LAN_4:
		hw->mac_type = em_icp_xxxx;
		hw->icp_xxxx_port_num = 1;
		break;
	case E1000_DEV_ID_EP80579_LAN_3:
	case E1000_DEV_ID_EP80579_LAN_5:
		hw->mac_type = em_icp_xxxx;
		hw->icp_xxxx_port_num = 2;
		break;
	case E1000_DEV_ID_EP80579_LAN_6:
		hw->mac_type = em_icp_xxxx;
		hw->icp_xxxx_port_num = 3;
		break;
	default:
		/* Should never have loaded on this device */
		return -E1000_ERR_MAC_TYPE;
	}

	switch (hw->mac_type) {
	case em_ich8lan:
	case em_ich9lan:
	case em_ich10lan:
	case em_pchlan:
	case em_pch2lan:
	case em_pch_lpt:
	case em_pch_spt:
	case em_pch_cnp:
	case em_pch_tgp:
	case em_pch_adp:
		hw->swfwhw_semaphore_present = TRUE;
		hw->asf_firmware_present = TRUE;
		break;
	case em_80003es2lan:
	case em_82575:
	case em_82576:
	case em_82580:
	case em_i210:
	case em_i350:
		hw->swfw_sync_present = TRUE;
		/* FALLTHROUGH */
	case em_82571:
	case em_82572:
	case em_82573:
	case em_82574:
		hw->eeprom_semaphore_present = TRUE;
		/* FALLTHROUGH */
	case em_82541:
	case em_82547:
	case em_82541_rev_2:
	case em_82547_rev_2:
		hw->asf_firmware_present = TRUE;
		break;
	default:
		break;
	}

	return E1000_SUCCESS;
}

/**
 *  em_set_sfp_media_type_82575 - derives SFP module media type.
 *  @hw: pointer to the HW structure
 *
 *  The media type is chosen based on SFP module.
 *  compatibility flags retrieved from SFP ID EEPROM.
 **/
STATIC int32_t em_set_sfp_media_type_82575(struct em_hw *hw)
{
	struct sfp_e1000_flags eth_flags;
	int32_t ret_val = E1000_ERR_CONFIG;
	uint32_t ctrl_ext = 0;
	uint8_t transceiver_type = 0;
	int32_t timeout = 3;

	/* Turn I2C interface ON and power on sfp cage */
	ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
	ctrl_ext &= ~E1000_CTRL_EXT_SDP3_DATA;
	E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext | E1000_CTRL_I2C_ENA);

	E1000_WRITE_FLUSH(hw);

	/* Read SFP module data */
	while (timeout) {
		ret_val = em_read_sfp_data_byte(hw,
			E1000_I2CCMD_SFP_DATA_ADDR(E1000_SFF_IDENTIFIER_OFFSET),
			&transceiver_type);
		if (ret_val == E1000_SUCCESS)
			break;
		msec_delay(100);
		timeout--;
	}
	if (ret_val != E1000_SUCCESS)
		goto out;

	ret_val = em_read_sfp_data_byte(hw,
			E1000_I2CCMD_SFP_DATA_ADDR(E1000_SFF_ETH_FLAGS_OFFSET),
			(uint8_t *)&eth_flags);
	if (ret_val != E1000_SUCCESS)
		goto out;

	/* Check if there is some SFP module plugged and powered */
	if ((transceiver_type == E1000_SFF_IDENTIFIER_SFP) ||
	    (transceiver_type == E1000_SFF_IDENTIFIER_SFF)) {
		if (eth_flags.e1000_base_lx || eth_flags.e1000_base_sx) {
			hw->media_type = em_media_type_internal_serdes;
		} else if (eth_flags.e100_base_fx || eth_flags.e100_base_lx) {
			hw->media_type = em_media_type_internal_serdes;
			hw->sgmii_active = TRUE;
		} else if (eth_flags.e1000_base_t) {
			hw->media_type = em_media_type_copper;
			hw->sgmii_active = TRUE;
		} else {
			DEBUGOUT("PHY module has not been recognized\n");
			ret_val = E1000_ERR_CONFIG;
			goto out;
		}
	} else {
		ret_val = E1000_ERR_CONFIG;
		goto out;
	}
	ret_val = E1000_SUCCESS;
out:
	/* Restore I2C interface setting */
	E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
	return ret_val;
}


/*****************************************************************************
 * Set media type and TBI compatibility.
 *
 * hw - Struct containing variables accessed by shared code
 * **************************************************************************/
void
em_set_media_type(struct em_hw *hw)
{
	uint32_t status, ctrl_ext, mdic;
	DEBUGFUNC("em_set_media_type");

	if (hw->mac_type != em_82543) {
		/* tbi_compatibility is only valid on 82543 */
		hw->tbi_compatibility_en = FALSE;
	}

	if (hw->mac_type == em_82575 || hw->mac_type == em_82580 ||
	    hw->mac_type == em_82576 ||
	    hw->mac_type == em_i210 || hw->mac_type == em_i350) {
		hw->media_type = em_media_type_copper;
		hw->sgmii_active = FALSE;

		ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
		switch (ctrl_ext & E1000_CTRL_EXT_LINK_MODE_MASK) {
		case E1000_CTRL_EXT_LINK_MODE_1000BASE_KX:
			hw->media_type = em_media_type_internal_serdes;
			ctrl_ext |= E1000_CTRL_I2C_ENA;
			break;
		case E1000_CTRL_EXT_LINK_MODE_SGMII:
			mdic = EM_READ_REG(hw, E1000_MDICNFG);
			ctrl_ext |= E1000_CTRL_I2C_ENA;
			if (mdic & E1000_MDICNFG_EXT_MDIO) {
				hw->media_type = em_media_type_copper;
				hw->sgmii_active = TRUE;
				break;
			}
			/* FALLTHROUGH */
		case E1000_CTRL_EXT_LINK_MODE_PCIE_SERDES:
			ctrl_ext |= E1000_CTRL_I2C_ENA;
			if (em_set_sfp_media_type_82575(hw) != 0) {
				hw->media_type = em_media_type_internal_serdes;
				if ((ctrl_ext & E1000_CTRL_EXT_LINK_MODE_MASK) ==
				    E1000_CTRL_EXT_LINK_MODE_SGMII) {
					hw->media_type = em_media_type_copper;
					hw->sgmii_active = TRUE;
				}
			}

			ctrl_ext &= ~E1000_CTRL_EXT_LINK_MODE_MASK;
			if (hw->sgmii_active)
				ctrl_ext |= E1000_CTRL_EXT_LINK_MODE_SGMII;
			else
				ctrl_ext |= E1000_CTRL_EXT_LINK_MODE_PCIE_SERDES;
			break;
		default:
			ctrl_ext &= ~E1000_CTRL_I2C_ENA;
			break;
		}
		E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
		return;
	}

	switch (hw->device_id) {
	case E1000_DEV_ID_82545GM_SERDES:
	case E1000_DEV_ID_82546GB_SERDES:
	case E1000_DEV_ID_82571EB_SERDES:
	case E1000_DEV_ID_82571EB_SERDES_DUAL:
	case E1000_DEV_ID_82571EB_SERDES_QUAD:
	case E1000_DEV_ID_82572EI_SERDES:
	case E1000_DEV_ID_80003ES2LAN_SERDES_DPT:
		hw->media_type = em_media_type_internal_serdes;
		break;
	case E1000_DEV_ID_EP80579_LAN_1:
	case E1000_DEV_ID_EP80579_LAN_2:
	case E1000_DEV_ID_EP80579_LAN_3:
	case E1000_DEV_ID_EP80579_LAN_4:
	case E1000_DEV_ID_EP80579_LAN_5:
	case E1000_DEV_ID_EP80579_LAN_6:
		hw->media_type = em_media_type_copper;
		break;
	default:
		switch (hw->mac_type) {
		case em_82542_rev2_0:
		case em_82542_rev2_1:
			hw->media_type = em_media_type_fiber;
			break;
		case em_ich8lan:
		case em_ich9lan:
		case em_ich10lan:
		case em_pchlan:
		case em_pch2lan:
		case em_pch_lpt:
		case em_pch_spt:
		case em_pch_cnp:
		case em_pch_tgp:
		case em_pch_adp:
		case em_82573:
		case em_82574:
			/*
			 * The STATUS_TBIMODE bit is reserved or reused for
			 * the this device.
			 */
			hw->media_type = em_media_type_copper;
			break;
		default:
			status = E1000_READ_REG(hw, STATUS);
			if (status & E1000_STATUS_TBIMODE) {
				hw->media_type = em_media_type_fiber;
				/* tbi_compatibility not valid on fiber */
				hw->tbi_compatibility_en = FALSE;
			} else {
				hw->media_type = em_media_type_copper;
			}
			break;
		}
	}
}
/******************************************************************************
 * Reset the transmit and receive units; mask and clear all interrupts.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
int32_t
em_reset_hw(struct em_hw *hw)
{
	uint32_t ctrl;
	uint32_t ctrl_ext;
	uint32_t icr;
	uint32_t manc;
	uint32_t led_ctrl;
	uint32_t timeout;
	uint32_t extcnf_ctrl;
	int32_t  ret_val;
	DEBUGFUNC("em_reset_hw");

	/* For 82542 (rev 2.0), disable MWI before issuing a device reset */
	if (hw->mac_type == em_82542_rev2_0) {
		DEBUGOUT("Disabling MWI on 82542 rev 2.0\n");
		em_pci_clear_mwi(hw);
	}
	if (hw->bus_type == em_bus_type_pci_express) {
		/*
		 * Prevent the PCI-E bus from sticking if there is no TLP
		 * connection on the last TLP read/write transaction when MAC
		 * is reset.
		 */
		if (em_disable_pciex_master(hw) != E1000_SUCCESS) {
			DEBUGOUT("PCI-E Master disable polling has failed.\n");
		}
	}

        /* Set the completion timeout for 82575 chips */
        if (hw->mac_type == em_82575 || hw->mac_type == em_82580 ||
	    hw->mac_type == em_82576 ||
	    hw->mac_type == em_i210 || hw->mac_type == em_i350) {
                ret_val = em_set_pciex_completion_timeout(hw);
                if (ret_val) {              
                        DEBUGOUT("PCI-E Set completion timeout has failed.\n");
                }
        }

	/* Clear interrupt mask to stop board from generating interrupts */
	DEBUGOUT("Masking off all interrupts\n");
	E1000_WRITE_REG(hw, IMC, 0xffffffff);
	/*
	 * Disable the Transmit and Receive units.  Then delay to allow any
	 * pending transactions to complete before we hit the MAC with the
	 * global reset.
	 */
	E1000_WRITE_REG(hw, RCTL, 0);
	E1000_WRITE_REG(hw, TCTL, E1000_TCTL_PSP);
	E1000_WRITE_FLUSH(hw);
	/*
	 * The tbi_compatibility_on Flag must be cleared when Rctl is
	 * cleared.
	 */
	hw->tbi_compatibility_on = FALSE;
	/*
	 * Delay to allow any outstanding PCI transactions to complete before
	 * resetting the device
	 */
	msec_delay(10);

	ctrl = E1000_READ_REG(hw, CTRL);

	/* Must reset the PHY before resetting the MAC */
	if ((hw->mac_type == em_82541) || (hw->mac_type == em_82547)) {
		E1000_WRITE_REG(hw, CTRL, (ctrl | E1000_CTRL_PHY_RST));
		msec_delay(5);
	}
	/*
	 * Must acquire the MDIO ownership before MAC reset. Ownership
	 * defaults to firmware after a reset.
	 */
	if ((hw->mac_type == em_82573) || (hw->mac_type == em_82574)) {
		timeout = 10;

		extcnf_ctrl = E1000_READ_REG(hw, EXTCNF_CTRL);
		extcnf_ctrl |= E1000_EXTCNF_CTRL_MDIO_SW_OWNERSHIP;

		do {
			E1000_WRITE_REG(hw, EXTCNF_CTRL, extcnf_ctrl);
			extcnf_ctrl = E1000_READ_REG(hw, EXTCNF_CTRL);

			if (extcnf_ctrl & E1000_EXTCNF_CTRL_MDIO_SW_OWNERSHIP)
				break;
			else
				extcnf_ctrl |= 
				    E1000_EXTCNF_CTRL_MDIO_SW_OWNERSHIP;

			msec_delay(2);
			timeout--;
		} while (timeout);
	}
	/* Workaround for ICH8 bit corruption issue in FIFO memory */
	if (hw->mac_type == em_ich8lan) {
		/* Set Tx and Rx buffer allocation to 8k apiece. */
		E1000_WRITE_REG(hw, PBA, E1000_PBA_8K);
		/* Set Packet Buffer Size to 16k. */
		E1000_WRITE_REG(hw, PBS, E1000_PBS_16K);
	}
	/*
	 * Issue a global reset to the MAC.  This will reset the chip's
	 * transmit, receive, DMA, and link units.  It will not effect the
	 * current PCI configuration.  The global reset bit is self-
	 * clearing, and should clear within a microsecond.
	 */
	DEBUGOUT("Issuing a global reset to MAC\n");

	switch (hw->mac_type) {
	case em_82544:
	case em_82540:
	case em_82545:
	case em_82546:
	case em_82541:
	case em_82541_rev_2:
		/*
		 * These controllers can't ack the 64-bit write when issuing
		 * the reset, so use IO-mapping as a workaround to issue the
		 * reset
		 */
		E1000_WRITE_REG_IO(hw, CTRL, (ctrl | E1000_CTRL_RST));
		break;
	case em_82545_rev_3:
	case em_82546_rev_3:
		/* Reset is performed on a shadow of the control register */
		E1000_WRITE_REG(hw, CTRL_DUP, (ctrl | E1000_CTRL_RST));
		break;
	case em_ich8lan:
	case em_ich9lan:
	case em_ich10lan:
	case em_pchlan:
	case em_pch2lan:
	case em_pch_lpt:
	case em_pch_spt:
	case em_pch_cnp:
	case em_pch_tgp:
	case em_pch_adp:
		if (!hw->phy_reset_disable &&
		    em_check_phy_reset_block(hw) == E1000_SUCCESS) {
			/*
			 * PHY HW reset requires MAC CORE reset at the same
			 * time to make sure the interface between MAC and
			 * the external PHY is reset.
			 */
			ctrl |= E1000_CTRL_PHY_RST;
			/*
			 * Gate automatic PHY configuration by hardware on
			 * non-managed 82579
			 */
			if ((hw->mac_type == em_pch2lan) &&
				!(E1000_READ_REG(hw, FWSM) & E1000_FWSM_FW_VALID)) {
				em_gate_hw_phy_config_ich8lan(hw, TRUE);
			}
		}
		em_get_software_flag(hw);
		E1000_WRITE_REG(hw, CTRL, (ctrl | E1000_CTRL_RST));
		/* HW reset releases software_flag */
		hw->sw_flag = 0;
		msec_delay(20);

		/* Ungate automatic PHY configuration on non-managed 82579 */
		if (hw->mac_type == em_pch2lan && !hw->phy_reset_disable &&
		    !(E1000_READ_REG(hw, FWSM) & E1000_FWSM_FW_VALID)) {
			msec_delay(10);
			em_gate_hw_phy_config_ich8lan(hw, FALSE);
		}
 		break;
	default:
		E1000_WRITE_REG(hw, CTRL, (ctrl | E1000_CTRL_RST));
		break;
	}

	if (em_check_phy_reset_block(hw) == E1000_SUCCESS) {
		if (hw->mac_type == em_pchlan) {
			ret_val = em_hv_phy_workarounds_ich8lan(hw);
			if (ret_val)
				return ret_val;
		}
		else if (hw->mac_type == em_pch2lan) {
			ret_val = em_lv_phy_workarounds_ich8lan(hw);
			if (ret_val)
				return ret_val;
		}
	}

	/*
	 * After MAC reset, force reload of EEPROM to restore power-on
	 * settings to device.  Later controllers reload the EEPROM
	 * automatically, so just wait for reload to complete.
	 */
	switch (hw->mac_type) {
	case em_82542_rev2_0:
	case em_82542_rev2_1:
	case em_82543:
	case em_82544:
		/* Wait for reset to complete */
		usec_delay(10);
		ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
		ctrl_ext |= E1000_CTRL_EXT_EE_RST;
		E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
		E1000_WRITE_FLUSH(hw);
		/* Wait for EEPROM reload */
		msec_delay(2);
		break;
	case em_82541:
	case em_82541_rev_2:
	case em_82547:
	case em_82547_rev_2:
		/* Wait for EEPROM reload */
		msec_delay(20);
		break;
	case em_82573:
	case em_82574:
		if (em_is_onboard_nvm_eeprom(hw) == FALSE) {
			usec_delay(10);
			ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
			ctrl_ext |= E1000_CTRL_EXT_EE_RST;
			E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
			E1000_WRITE_FLUSH(hw);
		}
		/* FALLTHROUGH */

		/* Auto read done will delay 5ms or poll based on mac type */
		ret_val = em_get_auto_rd_done(hw);
		if (ret_val)
			return ret_val;
		break;
	default:
		/* Wait for EEPROM reload (it happens automatically) */
		msec_delay(5);
		break;
	}

	/* Disable HW ARPs on ASF enabled adapters */
	if (hw->mac_type >= em_82540 && hw->mac_type <= em_82547_rev_2 &&
	    hw->mac_type != em_icp_xxxx) {
		manc = E1000_READ_REG(hw, MANC);
		manc &= ~(E1000_MANC_ARP_EN);
		E1000_WRITE_REG(hw, MANC, manc);
	}
	if ((hw->mac_type == em_82541) || (hw->mac_type == em_82547)) {
		em_phy_init_script(hw);

		/* Configure activity LED after PHY reset */
		led_ctrl = E1000_READ_REG(hw, LEDCTL);
		led_ctrl &= IGP_ACTIVITY_LED_MASK;
		led_ctrl |= (IGP_ACTIVITY_LED_ENABLE | IGP_LED3_MODE);
		E1000_WRITE_REG(hw, LEDCTL, led_ctrl);
	}

	/*
	 * For PCH, this write will make sure that any noise
	 * will be detected as a CRC error and be dropped rather than show up
	 * as a bad packet to the DMA engine.
	 */
	if (hw->mac_type == em_pchlan)
		E1000_WRITE_REG(hw, CRC_OFFSET, 0x65656565);
	
	/* Clear interrupt mask to stop board from generating interrupts */
	DEBUGOUT("Masking off all interrupts\n");
	E1000_WRITE_REG(hw, IMC, 0xffffffff);

	/* Clear any pending interrupt events. */
	icr = E1000_READ_REG(hw, ICR);

	/* If MWI was previously enabled, reenable it. */
	if (hw->mac_type == em_82542_rev2_0) {
		if (hw->pci_cmd_word & CMD_MEM_WRT_INVALIDATE)
			em_pci_set_mwi(hw);
	}
	if (IS_ICH8(hw->mac_type)) {
		uint32_t kab = E1000_READ_REG(hw, KABGTXD);
		kab |= E1000_KABGTXD_BGSQLBIAS;
		E1000_WRITE_REG(hw, KABGTXD, kab);
	}

	if (hw->mac_type == em_82580 || hw->mac_type == em_i350) {
		uint32_t mdicnfg;
		uint16_t nvm_data;

		/* clear global device reset status bit */
		EM_WRITE_REG(hw, E1000_STATUS, E1000_STATUS_DEV_RST_SET);

		em_read_eeprom(hw, EEPROM_INIT_CONTROL3_PORT_A +
		    NVM_82580_LAN_FUNC_OFFSET(hw->bus_func), 1,
		    &nvm_data);

		mdicnfg = EM_READ_REG(hw, E1000_MDICNFG);
		if (nvm_data & NVM_WORD24_EXT_MDIO)
			mdicnfg |= E1000_MDICNFG_EXT_MDIO;
		if (nvm_data & NVM_WORD24_COM_MDIO)
			mdicnfg |= E1000_MDICNFG_COM_MDIO;
		EM_WRITE_REG(hw, E1000_MDICNFG, mdicnfg);
	}

	if (hw->mac_type == em_i210 || hw->mac_type == em_i350)
		em_set_eee_i350(hw);

	return E1000_SUCCESS;
}

/******************************************************************************
 *
 * Initialize a number of hardware-dependent bits
 *
 * hw: Struct containing variables accessed by shared code
 *
 *****************************************************************************/
STATIC void
em_initialize_hardware_bits(struct em_softc *sc)
{
	struct em_hw *hw = &sc->hw;
	struct em_queue *que;

	DEBUGFUNC("em_initialize_hardware_bits");

	if ((hw->mac_type >= em_82571) && (!hw->initialize_hw_bits_disable)) {
		/* Settings common to all silicon */
		uint32_t reg_ctrl, reg_ctrl_ext;
		uint32_t reg_tarc0, reg_tarc1;
		uint32_t reg_tctl;
		uint32_t reg_txdctl;
		reg_tarc0 = E1000_READ_REG(hw, TARC0);
		reg_tarc0 &= ~0x78000000;	/* Clear bits 30, 29, 28, and
						 * 27 */
		FOREACH_QUEUE(sc, que) {
			reg_txdctl = E1000_READ_REG(hw, TXDCTL(que->me));
			reg_txdctl |= E1000_TXDCTL_COUNT_DESC;	/* Set bit 22 */
			E1000_WRITE_REG(hw, TXDCTL(que->me), reg_txdctl);
		}

		/*
		 * Old code always initialized queue 1,
		 * even when unused, keep behaviour
		 */
		if (sc->num_queues == 1) {
			reg_txdctl = E1000_READ_REG(hw, TXDCTL(1));
			reg_txdctl |= E1000_TXDCTL_COUNT_DESC;
			E1000_WRITE_REG(hw, TXDCTL(1), reg_txdctl);
		}

		switch (hw->mac_type) {
		case em_82571:
		case em_82572:
			reg_tarc1 = E1000_READ_REG(hw, TARC1);
			reg_tctl = E1000_READ_REG(hw, TCTL);

			/* Set the phy Tx compatible mode bits */
			reg_tarc1 &= ~0x60000000; /* Clear bits 30 and 29 */

			reg_tarc0 |= 0x07800000; /* Set TARC0 bits 23-26 */
			reg_tarc1 |= 0x07000000; /* Set TARC1 bits 24-26 */

			if (reg_tctl & E1000_TCTL_MULR)
				/* Clear bit 28 if MULR is 1b */ 
				reg_tarc1 &= ~0x10000000;
			else
				/* Set bit 28 if MULR is 0b */
				reg_tarc1 |= 0x10000000;

			E1000_WRITE_REG(hw, TARC1, reg_tarc1);
			break;
		case em_82573:
		case em_82574:
			reg_ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
			reg_ctrl = E1000_READ_REG(hw, CTRL);

			reg_ctrl_ext &= ~0x00800000;	/* Clear bit 23 */
			reg_ctrl_ext |= 0x00400000;	/* Set bit 22 */
			reg_ctrl &= ~0x20000000;	/* Clear bit 29 */

			E1000_WRITE_REG(hw, CTRL_EXT, reg_ctrl_ext);
			E1000_WRITE_REG(hw, CTRL, reg_ctrl);
			break;
		case em_80003es2lan:
			if ((hw->media_type == em_media_type_fiber) ||
			(hw->media_type == em_media_type_internal_serdes)) {
				/* Clear bit 20 */
				reg_tarc0 &= ~0x00100000;
			}
			reg_tctl = E1000_READ_REG(hw, TCTL);
			reg_tarc1 = E1000_READ_REG(hw, TARC1);
			if (reg_tctl & E1000_TCTL_MULR)
				/* Clear bit 28 if MULR is 1b */
				reg_tarc1 &= ~0x10000000;
			else
				/* Set bit 28 if MULR is 0b */
				reg_tarc1 |= 0x10000000;

			E1000_WRITE_REG(hw, TARC1, reg_tarc1);
			break;
		case em_ich8lan:
		case em_ich9lan:
		case em_ich10lan:
		case em_pchlan:
		case em_pch2lan:
		case em_pch_lpt:
		case em_pch_spt:
		case em_pch_cnp:
		case em_pch_tgp:
		case em_pch_adp:
			if (hw->mac_type == em_ich8lan)
				/* Set TARC0 bits 29 and 28 */
				reg_tarc0 |= 0x30000000;

			reg_ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
			reg_ctrl_ext |= 0x00400000;	/* Set bit 22 */
			/* Enable PHY low-power state when MAC is at D3 w/o WoL */
			if (hw->mac_type >= em_pchlan)
				reg_ctrl_ext |= E1000_CTRL_EXT_PHYPDEN;
			E1000_WRITE_REG(hw, CTRL_EXT, reg_ctrl_ext);

			reg_tarc0 |= 0x0d800000;	/* Set TARC0 bits 23,
							 * 24, 26, 27 */

			reg_tarc1 = E1000_READ_REG(hw, TARC1);
			reg_tctl = E1000_READ_REG(hw, TCTL);

			if (reg_tctl & E1000_TCTL_MULR)
				/* Clear bit 28 if MULR is 1b */
				reg_tarc1 &= ~0x10000000;
			else
				/* Set bit 28 if MULR is 0b */
				reg_tarc1 |= 0x10000000;

			reg_tarc1 |= 0x45000000;	/* Set bit 24, 26 and 
							 * 30 */

			E1000_WRITE_REG(hw, TARC1, reg_tarc1);
			break;
		default:
			break;
		}

		E1000_WRITE_REG(hw, TARC0, reg_tarc0);
	}
}

/**
 *  e1000_toggle_lanphypc_pch_lpt - toggle the LANPHYPC pin value
 *  @hw: pointer to the HW structure
 *
 *  Toggling the LANPHYPC pin value fully power-cycles the PHY and is
 *  used to reset the PHY to a quiescent state when necessary.
 **/
static void
em_toggle_lanphypc_pch_lpt(struct em_hw *hw)
{
	uint32_t mac_reg;

	DEBUGFUNC("e1000_toggle_lanphypc_pch_lpt");

	/* Set Phy Config Counter to 50msec */
	mac_reg = E1000_READ_REG(hw, FEXTNVM3);
	mac_reg &= ~E1000_FEXTNVM3_PHY_CFG_COUNTER_MASK;
	mac_reg |= E1000_FEXTNVM3_PHY_CFG_COUNTER_50MSEC;
	E1000_WRITE_REG(hw, FEXTNVM3, mac_reg);

	/* Toggle LANPHYPC Value bit */
	mac_reg = E1000_READ_REG(hw, CTRL);
	mac_reg |= E1000_CTRL_LANPHYPC_OVERRIDE;
	mac_reg &= ~E1000_CTRL_LANPHYPC_VALUE;
	E1000_WRITE_REG(hw, CTRL, mac_reg);
	E1000_WRITE_FLUSH(hw);
	msec_delay(1);
	mac_reg &= ~E1000_CTRL_LANPHYPC_OVERRIDE;
	E1000_WRITE_REG(hw, CTRL, mac_reg);
	E1000_WRITE_FLUSH(hw);

	if (hw->mac_type < em_pch_lpt) {
		msec_delay(50);
	} else {
		uint16_t count = 20;

		do {
			msec_delay(5);
		} while (!(E1000_READ_REG(hw, CTRL_EXT) &
		    E1000_CTRL_EXT_LPCD) && count--);

		msec_delay(30);
	}
}

/**
 *  em_disable_ulp_lpt_lp - unconfigure Ultra Low Power mode for LynxPoint-LP
 *  @hw: pointer to the HW structure
 *  @force: boolean indicating whether or not to force disabling ULP
 *
 *  Un-configure ULP mode when link is up, the system is transitioned from
 *  Sx or the driver is unloaded.  If on a Manageability Engine (ME) enabled
 *  system, poll for an indication from ME that ULP has been un-configured.
 *  If not on an ME enabled system, un-configure the ULP mode by software.
 *
 *  During nominal operation, this function is called when link is acquired
 *  to disable ULP mode (force=FALSE); otherwise, for example when unloading
 *  the driver or during Sx->S0 transitions, this is called with force=TRUE
 *  to forcibly disable ULP.
 */
static int
em_disable_ulp_lpt_lp(struct em_hw *hw, bool force)
{
	int ret_val = E1000_SUCCESS;
	uint32_t mac_reg;
	uint16_t phy_reg;
	int i = 0;

	if ((hw->mac_type < em_pch_lpt) ||
	    (hw->device_id == E1000_DEV_ID_PCH_LPT_I217_LM) ||
	    (hw->device_id == E1000_DEV_ID_PCH_LPT_I217_V) ||
	    (hw->device_id == E1000_DEV_ID_PCH_I218_LM2) ||
	    (hw->device_id == E1000_DEV_ID_PCH_I218_V2))
		return 0;

	if (E1000_READ_REG(hw, FWSM) & E1000_FWSM_FW_VALID) {
		if (force) {
			/* Request ME un-configure ULP mode in the PHY */
			mac_reg = E1000_READ_REG(hw, H2ME);
			mac_reg &= ~E1000_H2ME_ULP;
			mac_reg |= E1000_H2ME_ENFORCE_SETTINGS;
			E1000_WRITE_REG(hw, H2ME, mac_reg);
		}

		/* Poll up to 300msec for ME to clear ULP_CFG_DONE. */
		while (E1000_READ_REG(hw, FWSM) & E1000_FWSM_ULP_CFG_DONE) {
			if (i++ == 30) {
				ret_val = -E1000_ERR_PHY;
				goto out;
			}

			msec_delay(10);
		}
		DEBUGOUT1("ULP_CONFIG_DONE cleared after %dmsec\n", i * 10);

		if (force) {
			mac_reg = E1000_READ_REG(hw, H2ME);
			mac_reg &= ~E1000_H2ME_ENFORCE_SETTINGS;
			E1000_WRITE_REG(hw, H2ME, mac_reg);
		} else {
			/* Clear H2ME.ULP after ME ULP configuration */
			mac_reg = E1000_READ_REG(hw, H2ME);
			mac_reg &= ~E1000_H2ME_ULP;
			E1000_WRITE_REG(hw, H2ME, mac_reg);
		}

		goto out;
	}

	ret_val = em_get_software_flag(hw);
	if (ret_val)
		goto out;

	if (force)
		/* Toggle LANPHYPC Value bit */
		em_toggle_lanphypc_pch_lpt(hw);

	/* Unforce SMBus mode in PHY */
	ret_val = em_read_phy_reg(hw, CV_SMB_CTRL, &phy_reg);
	if (ret_val) {
		/* The MAC might be in PCIe mode, so temporarily force to
		 * SMBus mode in order to access the PHY.
		 */
		mac_reg = E1000_READ_REG(hw, CTRL_EXT);
		mac_reg |= E1000_CTRL_EXT_FORCE_SMBUS;
		E1000_WRITE_REG(hw, CTRL_EXT, mac_reg);

		msec_delay(50);

		ret_val = em_read_phy_reg(hw, CV_SMB_CTRL, &phy_reg);
		if (ret_val)
			goto release;
	}
	phy_reg &= ~CV_SMB_CTRL_FORCE_SMBUS;
	em_write_phy_reg(hw, CV_SMB_CTRL, phy_reg);

	/* Unforce SMBus mode in MAC */
	mac_reg = E1000_READ_REG(hw, CTRL_EXT);
	mac_reg &= ~E1000_CTRL_EXT_FORCE_SMBUS;
	E1000_WRITE_REG(hw, CTRL_EXT, mac_reg);

	/* When ULP mode was previously entered, K1 was disabled by the
	 * hardware.  Re-Enable K1 in the PHY when exiting ULP.
	 */
	ret_val = em_read_phy_reg(hw, HV_PM_CTRL, &phy_reg);
	if (ret_val)
		goto release;
	phy_reg |= HV_PM_CTRL_K1_ENABLE;
	em_write_phy_reg(hw, HV_PM_CTRL, phy_reg);

	/* Clear ULP enabled configuration */
	ret_val = em_read_phy_reg(hw, I218_ULP_CONFIG1, &phy_reg);
	if (ret_val)
		goto release;
	phy_reg &= ~(I218_ULP_CONFIG1_IND |
		     I218_ULP_CONFIG1_STICKY_ULP |
		     I218_ULP_CONFIG1_RESET_TO_SMBUS |
		     I218_ULP_CONFIG1_WOL_HOST |
		     I218_ULP_CONFIG1_INBAND_EXIT |
		     I218_ULP_CONFIG1_EN_ULP_LANPHYPC |
		     I218_ULP_CONFIG1_DIS_CLR_STICKY_ON_PERST |
		     I218_ULP_CONFIG1_DISABLE_SMB_PERST);
	em_write_phy_reg(hw, I218_ULP_CONFIG1, phy_reg);

	/* Commit ULP changes by starting auto ULP configuration */
	phy_reg |= I218_ULP_CONFIG1_START;
	em_write_phy_reg(hw, I218_ULP_CONFIG1, phy_reg);

	/* Clear Disable SMBus Release on PERST# in MAC */
	mac_reg = E1000_READ_REG(hw, FEXTNVM7);
	mac_reg &= ~E1000_FEXTNVM7_DISABLE_SMB_PERST;
	E1000_WRITE_REG(hw, FEXTNVM7, mac_reg);

release:
	em_release_software_flag(hw);
	if (force) {
		em_phy_reset(hw);
		msec_delay(50);
	}
out:
	if (ret_val)
		DEBUGOUT1("Error in ULP disable flow: %d\n", ret_val);

	return ret_val;
}

/******************************************************************************
 * Performs basic configuration of the adapter.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Assumes that the controller has previously been reset and is in a
 * post-reset uninitialized state. Initializes the receive address registers,
 * multicast table, and VLAN filter table. Calls routines to setup link
 * configuration and flow control settings. Clears all on-chip counters. Leaves
 * the transmit and receive units disabled and uninitialized.
 *****************************************************************************/
int32_t
em_init_hw(struct em_softc *sc)
{
	struct em_hw *hw = &sc->hw;
	struct em_queue *que;
	uint32_t ctrl;
	uint32_t i;
	int32_t  ret_val;
	uint16_t pcix_cmd_word;
	uint16_t pcix_stat_hi_word;
	uint16_t cmd_mmrbc;
	uint16_t stat_mmrbc;
	uint32_t mta_size;
	uint32_t reg_data;
	uint32_t ctrl_ext;
	uint32_t snoop;
	uint32_t fwsm;
	DEBUGFUNC("em_init_hw");

	/* force full DMA clock frequency for ICH8 */
	if (hw->mac_type == em_ich8lan) {
		reg_data = E1000_READ_REG(hw, STATUS);
		reg_data &= ~0x80000000;
		E1000_WRITE_REG(hw, STATUS, reg_data);
	}

	if (hw->mac_type == em_pchlan ||
		hw->mac_type == em_pch2lan ||
		hw->mac_type == em_pch_lpt ||
		hw->mac_type == em_pch_spt ||
		hw->mac_type == em_pch_cnp ||
		hw->mac_type == em_pch_tgp ||
		hw->mac_type == em_pch_adp) {
		/*
		 * The MAC-PHY interconnect may still be in SMBus mode
		 * after Sx->S0.  Toggle the LANPHYPC Value bit to force
		 * the interconnect to PCIe mode, but only if there is no
		 * firmware present otherwise firmware will have done it.
		 */
		fwsm = E1000_READ_REG(hw, FWSM);
		if ((fwsm & E1000_FWSM_FW_VALID) == 0) {
			ctrl = E1000_READ_REG(hw, CTRL);
			ctrl |=  E1000_CTRL_LANPHYPC_OVERRIDE;
			ctrl &= ~E1000_CTRL_LANPHYPC_VALUE;
			E1000_WRITE_REG(hw, CTRL, ctrl);
			usec_delay(10);
			ctrl &= ~E1000_CTRL_LANPHYPC_OVERRIDE;
			E1000_WRITE_REG(hw, CTRL, ctrl);
			msec_delay(50);
		}

		/* Gate automatic PHY configuration on non-managed 82579 */
		if (hw->mac_type == em_pch2lan)
			em_gate_hw_phy_config_ich8lan(hw, TRUE);

		em_disable_ulp_lpt_lp(hw, TRUE);
		/*
		 * Reset the PHY before any access to it.  Doing so,
		 * ensures that the PHY is in a known good state before
		 * we read/write PHY registers.  The generic reset is
		 * sufficient here, because we haven't determined
		 * the PHY type yet.
		 */
		em_phy_reset(hw);

		/* Ungate automatic PHY configuration on non-managed 82579 */
		if (hw->mac_type == em_pch2lan &&
			(fwsm & E1000_FWSM_FW_VALID) == 0)
			em_gate_hw_phy_config_ich8lan(hw, FALSE);

		/* Set MDIO slow mode before any other MDIO access */
		ret_val = em_set_mdio_slow_mode_hv(hw);
		if (ret_val)
			return ret_val;
	}

	/* Initialize Identification LED */
	ret_val = em_id_led_init(hw);
	if (ret_val) {
		DEBUGOUT("Error Initializing Identification LED\n");
		return ret_val;
	}
	/* Set the media type and TBI compatibility */
	em_set_media_type(hw);

	/* Magic delay that improves problems with i219LM on HP Elitebook */
	msec_delay(1);
	/* Must be called after em_set_media_type because media_type is used */
	em_initialize_hardware_bits(sc);

	/* Disabling VLAN filtering. */
	DEBUGOUT("Initializing the IEEE VLAN\n");
	/* VET hardcoded to standard value and VFTA removed in ICH8/ICH9 LAN */
	if (!IS_ICH8(hw->mac_type)) {
		if (hw->mac_type < em_82545_rev_3)
			E1000_WRITE_REG(hw, VET, 0);
		if (hw->mac_type == em_i350)
			em_clear_vfta_i350(hw);
		else
			em_clear_vfta(hw);
	}
	/* For 82542 (rev 2.0), disable MWI and put the receiver into reset */
	if (hw->mac_type == em_82542_rev2_0) {
		DEBUGOUT("Disabling MWI on 82542 rev 2.0\n");
		em_pci_clear_mwi(hw);
		E1000_WRITE_REG(hw, RCTL, E1000_RCTL_RST);
		E1000_WRITE_FLUSH(hw);
		msec_delay(5);
	}
	/*
	 * Setup the receive address. This involves initializing all of the
	 * Receive Address Registers (RARs 0 - 15).
	 */
	em_init_rx_addrs(hw);

	/* For 82542 (rev 2.0), take the receiver out of reset and enable MWI*/
	if (hw->mac_type == em_82542_rev2_0) {
		E1000_WRITE_REG(hw, RCTL, 0);
		E1000_WRITE_FLUSH(hw);
		msec_delay(1);
		if (hw->pci_cmd_word & CMD_MEM_WRT_INVALIDATE)
			em_pci_set_mwi(hw);
	}
	/* Zero out the Multicast HASH table */
	DEBUGOUT("Zeroing the MTA\n");
	mta_size = E1000_MC_TBL_SIZE;
	if (IS_ICH8(hw->mac_type))
		mta_size = E1000_MC_TBL_SIZE_ICH8LAN;
	for (i = 0; i < mta_size; i++) {
		E1000_WRITE_REG_ARRAY(hw, MTA, i, 0);
		/*
		 * use write flush to prevent Memory Write Block (MWB) from
		 * occurring when accessing our register space
		 */
		E1000_WRITE_FLUSH(hw);
	}
	/*
	 * Set the PCI priority bit correctly in the CTRL register.  This
	 * determines if the adapter gives priority to receives, or if it
	 * gives equal priority to transmits and receives.  Valid only on
	 * 82542 and 82543 silicon.
	 */
	if (hw->dma_fairness && hw->mac_type <= em_82543) {
		ctrl = E1000_READ_REG(hw, CTRL);
		E1000_WRITE_REG(hw, CTRL, ctrl | E1000_CTRL_PRIOR);
	}
	switch (hw->mac_type) {
	case em_82545_rev_3:
	case em_82546_rev_3:
		break;
	default:
		/*
		 * Workaround for PCI-X problem when BIOS sets MMRBC
		 * incorrectly.
		 */
		if (hw->bus_type == em_bus_type_pcix) {
			em_read_pci_cfg(hw, PCIX_COMMAND_REGISTER, 
			    &pcix_cmd_word);
			em_read_pci_cfg(hw, PCIX_STATUS_REGISTER_HI,
			    &pcix_stat_hi_word);
			cmd_mmrbc = (pcix_cmd_word & PCIX_COMMAND_MMRBC_MASK) 
			    >> PCIX_COMMAND_MMRBC_SHIFT;
			stat_mmrbc = (pcix_stat_hi_word & 
			    PCIX_STATUS_HI_MMRBC_MASK) >> 
			    PCIX_STATUS_HI_MMRBC_SHIFT;

			if (stat_mmrbc == PCIX_STATUS_HI_MMRBC_4K)
				stat_mmrbc = PCIX_STATUS_HI_MMRBC_2K;
			if (cmd_mmrbc > stat_mmrbc) {
				pcix_cmd_word &= ~PCIX_COMMAND_MMRBC_MASK;
				pcix_cmd_word |= stat_mmrbc << 
				    PCIX_COMMAND_MMRBC_SHIFT;
				em_write_pci_cfg(hw, PCIX_COMMAND_REGISTER,
				    &pcix_cmd_word);
			}
		}
		break;
	}

	/* More time needed for PHY to initialize */
	if (IS_ICH8(hw->mac_type))
		msec_delay(15);

        /*
	 * The 82578 Rx buffer will stall if wakeup is enabled in host and
	 * the ME.  Reading the BM_WUC register will clear the host wakeup bit.
	 * Reset the phy after disabling host wakeup to reset the Rx buffer.
	 */
	if (hw->phy_type == em_phy_82578) {
		em_read_phy_reg(hw, PHY_REG(BM_WUC_PAGE, 1),
		    (uint16_t *)&reg_data);
		ret_val = em_phy_reset(hw);
		if (ret_val)
			return ret_val;
	}

	/* Call a subroutine to configure the link and setup flow control. */
	ret_val = em_setup_link(hw);

	/* Set the transmit descriptor write-back policy */
	if (hw->mac_type > em_82544) {
		FOREACH_QUEUE(sc, que) {
			ctrl = E1000_READ_REG(hw, TXDCTL(que->me));
			ctrl = (ctrl & ~E1000_TXDCTL_WTHRESH) |
			    E1000_TXDCTL_FULL_TX_DESC_WB;
			E1000_WRITE_REG(hw, TXDCTL(que->me), ctrl);
		}
	}
	if ((hw->mac_type == em_82573) || (hw->mac_type == em_82574)) {
		em_enable_tx_pkt_filtering(hw);
	}
	switch (hw->mac_type) {
	default:
		break;
	case em_80003es2lan:
		/* Enable retransmit on late collisions */
		reg_data = E1000_READ_REG(hw, TCTL);
		reg_data |= E1000_TCTL_RTLC;
		E1000_WRITE_REG(hw, TCTL, reg_data);

		/* Configure Gigabit Carry Extend Padding */
		reg_data = E1000_READ_REG(hw, TCTL_EXT);
		reg_data &= ~E1000_TCTL_EXT_GCEX_MASK;
		reg_data |= DEFAULT_80003ES2LAN_TCTL_EXT_GCEX;
		E1000_WRITE_REG(hw, TCTL_EXT, reg_data);

		/* Configure Transmit Inter-Packet Gap */
		reg_data = E1000_READ_REG(hw, TIPG);
		reg_data &= ~E1000_TIPG_IPGT_MASK;
		reg_data |= DEFAULT_80003ES2LAN_TIPG_IPGT_1000;
		E1000_WRITE_REG(hw, TIPG, reg_data);

		reg_data = E1000_READ_REG_ARRAY(hw, FFLT, 0x0001);
		reg_data &= ~0x00100000;
		E1000_WRITE_REG_ARRAY(hw, FFLT, 0x0001, reg_data);
		/* FALLTHROUGH */
	case em_82571:
	case em_82572:
	case em_82575:
	case em_82576:
	case em_82580:
	case em_i210:
	case em_i350:
	case em_ich8lan:
	case em_ich9lan:
	case em_ich10lan:
	case em_pchlan:
	case em_pch2lan:
	case em_pch_lpt:
	case em_pch_spt:
	case em_pch_cnp:
	case em_pch_tgp:
	case em_pch_adp:
		/*
		 * Old code always initialized queue 1,
		 * even when unused, keep behaviour
		 */
		if (sc->num_queues == 1) {
			ctrl = E1000_READ_REG(hw, TXDCTL(1));
			ctrl = (ctrl & ~E1000_TXDCTL_WTHRESH) | 
			    E1000_TXDCTL_FULL_TX_DESC_WB;
			E1000_WRITE_REG(hw, TXDCTL(1), ctrl);
		}
		break;
	}

	if ((hw->mac_type == em_82573) || (hw->mac_type == em_82574)) {
		uint32_t gcr = E1000_READ_REG(hw, GCR);
		gcr |= E1000_GCR_L1_ACT_WITHOUT_L0S_RX;
		E1000_WRITE_REG(hw, GCR, gcr);
	}
	/*
	 * Clear all of the statistics registers (clear on read).  It is
	 * important that we do this after we have tried to establish link
	 * because the symbol error count will increment wildly if there is
	 * no link.
	 */
	em_clear_hw_cntrs(hw);
	/*
	 * ICH8 No-snoop bits are opposite polarity. Set to snoop by default
	 * after reset.
	 */
	if (IS_ICH8(hw->mac_type)) {
		if (hw->mac_type == em_ich8lan)
			snoop = PCI_EX_82566_SNOOP_ALL;
		else
			snoop = (u_int32_t) ~ (PCI_EX_NO_SNOOP_ALL);
			
		em_set_pci_ex_no_snoop(hw, snoop);
	}

	/* ungate DMA clock to avoid packet loss */
	if (hw->mac_type >= em_pch_tgp) {
		uint32_t fflt_dbg = E1000_READ_REG(hw, FFLT_DBG);
		fflt_dbg |= (1 << 12);
		E1000_WRITE_REG(hw, FFLT_DBG, fflt_dbg);
	}

	if (hw->device_id == E1000_DEV_ID_82546GB_QUAD_COPPER ||
	    hw->device_id == E1000_DEV_ID_82546GB_QUAD_COPPER_KSP3) {
		ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
		/*
		 * Relaxed ordering must be disabled to avoid a parity error
		 * crash in a PCI slot.
		 */
		ctrl_ext |= E1000_CTRL_EXT_RO_DIS;
		E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
	}
	return ret_val;
}

/******************************************************************************
 * Adjust SERDES output amplitude based on EEPROM setting.
 *
 * hw - Struct containing variables accessed by shared code.
 *****************************************************************************/
static int32_t
em_adjust_serdes_amplitude(struct em_hw *hw)
{
	uint16_t eeprom_data;
	int32_t  ret_val;
	DEBUGFUNC("em_adjust_serdes_amplitude");

	if (hw->media_type != em_media_type_internal_serdes ||
	    hw->mac_type >= em_82575)
		return E1000_SUCCESS;

	switch (hw->mac_type) {
	case em_82545_rev_3:
	case em_82546_rev_3:
		break;
	default:
		return E1000_SUCCESS;
	}

	ret_val = em_read_eeprom(hw, EEPROM_SERDES_AMPLITUDE, 1, &eeprom_data);
	if (ret_val) {
		return ret_val;
	}
	if (eeprom_data != EEPROM_RESERVED_WORD) {
		/* Adjust SERDES output amplitude only. */
		eeprom_data &= EEPROM_SERDES_AMPLITUDE_MASK;
		ret_val = em_write_phy_reg(hw, M88E1000_PHY_EXT_CTRL, 
		    eeprom_data);
		if (ret_val)
			return ret_val;
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 * Configures flow control and link settings.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Determines which flow control settings to use. Calls the appropriate media-
 * specific link configuration function. Configures the flow control settings.
 * Assuming the adapter has a valid link partner, a valid link should be
 * established. Assumes the hardware has previously been reset and the
 * transmitter and receiver are not enabled.
 *****************************************************************************/
int32_t
em_setup_link(struct em_hw *hw)
{
	uint32_t ctrl_ext;
	int32_t  ret_val;
	uint16_t eeprom_data;
	uint16_t eeprom_control2_reg_offset;
	DEBUGFUNC("em_setup_link");

	eeprom_control2_reg_offset =
	    (hw->mac_type != em_icp_xxxx)
	    ? EEPROM_INIT_CONTROL2_REG
	    : EEPROM_INIT_CONTROL3_ICP_xxxx(hw->icp_xxxx_port_num);
	/*
	 * In the case of the phy reset being blocked, we already have a
	 * link. We do not have to set it up again.
	 */
	if (em_check_phy_reset_block(hw))
		return E1000_SUCCESS;
	/*
	 * Read and store word 0x0F of the EEPROM. This word contains bits
	 * that determine the hardware's default PAUSE (flow control) mode, a
	 * bit that determines whether the HW defaults to enabling or
	 * disabling auto-negotiation, and the direction of the SW defined
	 * pins. If there is no SW over-ride of the flow control setting,
	 * then the variable hw->fc will be initialized based on a value in
	 * the EEPROM.
	 */
	if (hw->fc == E1000_FC_DEFAULT) {
		switch (hw->mac_type) {
		case em_ich8lan:
		case em_ich9lan:
		case em_ich10lan:
		case em_pchlan:
		case em_pch2lan:
		case em_pch_lpt:
		case em_pch_spt:
		case em_pch_cnp:
		case em_pch_tgp:
		case em_pch_adp:
		case em_82573:
		case em_82574:
			hw->fc = E1000_FC_FULL;
			break;
		default:
			ret_val = em_read_eeprom(hw, 
			    eeprom_control2_reg_offset, 1, &eeprom_data);
			if (ret_val) {
				DEBUGOUT("EEPROM Read Error\n");
				return -E1000_ERR_EEPROM;
			}
			if ((eeprom_data & EEPROM_WORD0F_PAUSE_MASK) == 0)
				hw->fc = E1000_FC_NONE;
			else if ((eeprom_data & EEPROM_WORD0F_PAUSE_MASK) ==
			    EEPROM_WORD0F_ASM_DIR)
				hw->fc = E1000_FC_TX_PAUSE;
			else
				hw->fc = E1000_FC_FULL;
			break;
		}
	}
	/*
	 * We want to save off the original Flow Control configuration just
	 * in case we get disconnected and then reconnected into a different
	 * hub or switch with different Flow Control capabilities.
	 */
	if (hw->mac_type == em_82542_rev2_0)
		hw->fc &= (~E1000_FC_TX_PAUSE);

	if ((hw->mac_type < em_82543) && (hw->report_tx_early == 1))
		hw->fc &= (~E1000_FC_RX_PAUSE);

	hw->original_fc = hw->fc;

	DEBUGOUT1("After fix-ups FlowControl is now = %x\n", hw->fc);
	/*
	 * Take the 4 bits from EEPROM word 0x0F that determine the initial
	 * polarity value for the SW controlled pins, and setup the Extended
	 * Device Control reg with that info. This is needed because one of
	 * the SW controlled pins is used for signal detection.  So this
	 * should be done before em_setup_pcs_link() or em_phy_setup() is
	 * called.
	 */
	if (hw->mac_type == em_82543) {
		ret_val = em_read_eeprom(hw, EEPROM_INIT_CONTROL2_REG,
		    1, &eeprom_data);
		if (ret_val) {
			DEBUGOUT("EEPROM Read Error\n");
			return -E1000_ERR_EEPROM;
		}
		ctrl_ext = ((eeprom_data & EEPROM_WORD0F_SWPDIO_EXT) <<
		    SWDPIO__EXT_SHIFT);
		E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
	}
	/* Make sure we have a valid PHY */
	ret_val = em_detect_gig_phy(hw);
	if (ret_val) {
		DEBUGOUT("Error, did not detect valid phy.\n");
		if (hw->mac_type == em_icp_xxxx)
			return E1000_DEFER_INIT;
		else
			return ret_val;
	}
	DEBUGOUT1("Phy ID = %x \n", hw->phy_id);

	/* Call the necessary subroutine to configure the link. */
	switch (hw->media_type) {
	case em_media_type_copper:
	case em_media_type_oem:
		ret_val = em_setup_copper_link(hw);
		break;
	default:
		ret_val = em_setup_fiber_serdes_link(hw);
		break;
	}
	/*
	 * Initialize the flow control address, type, and PAUSE timer
	 * registers to their default values.  This is done even if flow
	 * control is disabled, because it does not hurt anything to
	 * initialize these registers.
	 */
	DEBUGOUT("Initializing the Flow Control address, type and timer regs\n"
	    );

	/*
         * FCAL/H and FCT are hardcoded to standard values in
         * em_ich8lan / em_ich9lan / em_ich10lan.
         */
	if (!IS_ICH8(hw->mac_type)) {
		E1000_WRITE_REG(hw, FCT, FLOW_CONTROL_TYPE);
		E1000_WRITE_REG(hw, FCAH, FLOW_CONTROL_ADDRESS_HIGH);
		E1000_WRITE_REG(hw, FCAL, FLOW_CONTROL_ADDRESS_LOW);
	}
	E1000_WRITE_REG(hw, FCTTV, hw->fc_pause_time);

	if (hw->phy_type == em_phy_82577 ||
	    hw->phy_type == em_phy_82578 ||
	    hw->phy_type == em_phy_82579 ||
	    hw->phy_type == em_phy_i217) {
		E1000_WRITE_REG(hw, FCRTV_PCH, 0x1000);
		em_write_phy_reg(hw, PHY_REG(BM_PORT_CTRL_PAGE, 27),
		    hw->fc_pause_time);
	}

	/*
	 * Set the flow control receive threshold registers.  Normally, these
	 * registers will be set to a default threshold that may be adjusted
	 * later by the driver's runtime code.  However, if the ability to
	 * transmit pause frames in not enabled, then these registers will be
	 * set to 0.
	 */
	if (!(hw->fc & E1000_FC_TX_PAUSE)) {
		E1000_WRITE_REG(hw, FCRTL, 0);
		E1000_WRITE_REG(hw, FCRTH, 0);
	} else {
		/*
		 * We need to set up the Receive Threshold high and low water
		 * marks as well as (optionally) enabling the transmission of
		 * XON frames.
		 */
		if (hw->fc_send_xon) {
			E1000_WRITE_REG(hw, FCRTL, (hw->fc_low_water 
			    | E1000_FCRTL_XONE));
			E1000_WRITE_REG(hw, FCRTH, hw->fc_high_water);
		} else {
			E1000_WRITE_REG(hw, FCRTL, hw->fc_low_water);
			E1000_WRITE_REG(hw, FCRTH, hw->fc_high_water);
		}
	}
	return ret_val;
}

void
em_power_up_serdes_link_82575(struct em_hw *hw)
{
	uint32_t reg;

	if (hw->media_type != em_media_type_internal_serdes &&
	    hw->sgmii_active == FALSE)
		return;

	/* Enable PCS to turn on link */
	reg = E1000_READ_REG(hw, PCS_CFG0);
	reg |= E1000_PCS_CFG_PCS_EN;
	E1000_WRITE_REG(hw, PCS_CFG0, reg);

	/* Power up the laser */
	reg = E1000_READ_REG(hw, CTRL_EXT);
	reg &= ~E1000_CTRL_EXT_SDP3_DATA;
	E1000_WRITE_REG(hw, CTRL_EXT, reg);

	/* flush the write to verify completion */
	E1000_WRITE_FLUSH(hw);
	delay(5);
}

/******************************************************************************
 * Sets up link for a fiber based or serdes based adapter
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Manipulates Physical Coding Sublayer functions in order to configure
 * link. Assumes the hardware has been previously reset and the transmitter
 * and receiver are not enabled.
 *****************************************************************************/
static int32_t
em_setup_fiber_serdes_link(struct em_hw *hw)
{
	uint32_t ctrl, ctrl_ext, reg;
	uint32_t status;
	uint32_t txcw = 0;
	uint32_t i;
	uint32_t signal = 0;
	int32_t  ret_val;
	DEBUGFUNC("em_setup_fiber_serdes_link");

	if (hw->media_type != em_media_type_internal_serdes &&
	    hw->sgmii_active == FALSE)
		return -E1000_ERR_CONFIG;

	/*
	 * On 82571 and 82572 Fiber connections, SerDes loopback mode
	 * persists until explicitly turned off or a power cycle is
	 * performed.  A read to the register does not indicate its status.
	 * Therefore, we ensure loopback mode is disabled during
	 * initialization.
	 */
	if (hw->mac_type == em_82571 || hw->mac_type == em_82572 ||
	    hw->mac_type >= em_82575)
		E1000_WRITE_REG(hw, SCTL, E1000_DISABLE_SERDES_LOOPBACK);

	if (hw->mac_type >= em_82575)
		em_power_up_serdes_link_82575(hw);
		
	/*
	 * On adapters with a MAC newer than 82544, SWDP 1 will be set when
	 * the optics detect a signal. On older adapters, it will be cleared
	 * when there is a signal.  This applies to fiber media only. If
	 * we're on serdes media, adjust the output amplitude to value set in
	 * the EEPROM.
	 */
	ctrl = E1000_READ_REG(hw, CTRL);
	if (hw->media_type == em_media_type_fiber)
		signal = (hw->mac_type > em_82544) ? E1000_CTRL_SWDPIN1 : 0;

	ret_val = em_adjust_serdes_amplitude(hw);
	if (ret_val)
		return ret_val;

	/* Take the link out of reset */
	ctrl &= ~(E1000_CTRL_LRST);

	if (hw->mac_type >= em_82575) {
		/* set both sw defined pins on 82575/82576*/
		ctrl |= E1000_CTRL_SWDPIN0 | E1000_CTRL_SWDPIN1;

		ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
		switch (ctrl_ext & E1000_CTRL_EXT_LINK_MODE_MASK) {
		case E1000_CTRL_EXT_LINK_MODE_1000BASE_KX:
		case E1000_CTRL_EXT_LINK_MODE_PCIE_SERDES:
			/* the backplane is always connected */
			reg = E1000_READ_REG(hw, PCS_LCTL);
			reg |= E1000_PCS_LCTL_FORCE_FCTRL;
			reg |= E1000_PCS_LCTL_FSV_1000 | E1000_PCS_LCTL_FDV_FULL;
			reg |= E1000_PCS_LCTL_FSD;        /* Force Speed */
			DEBUGOUT("Configuring Forced Link\n");
			E1000_WRITE_REG(hw, PCS_LCTL, reg);
			em_force_mac_fc(hw);
			hw->autoneg_failed = 0;
			return E1000_SUCCESS;
			break;
		default:
			/* Set switch control to serdes energy detect */
			reg = E1000_READ_REG(hw, CONNSW);
			reg |= E1000_CONNSW_ENRGSRC;
			E1000_WRITE_REG(hw, CONNSW, reg);
			break;
		}
	}

	/* Adjust VCO speed to improve BER performance */
	ret_val = em_set_vco_speed(hw);
	if (ret_val)
		return ret_val;

	em_config_collision_dist(hw);
	/*
	 * Check for a software override of the flow control settings, and
	 * setup the device accordingly.  If auto-negotiation is enabled,
	 * then software will have to set the "PAUSE" bits to the correct
	 * value in the Transmit Config Word Register (TXCW) and re-start
	 * auto-negotiation.  However, if auto-negotiation is disabled, then
	 * software will have to manually configure the two flow control
	 * enable bits in the CTRL register.
	 *
	 * The possible values of the "fc" parameter are: 0:  Flow control is
	 * completely disabled 1:  Rx flow control is enabled (we can receive
	 * pause frames, but not send pause frames). 2:  Tx flow control is
	 * enabled (we can send pause frames but we do not support receiving
	 * pause frames). 3:  Both Rx and TX flow control (symmetric) are
	 * enabled.
	 */
	switch (hw->fc) {
	case E1000_FC_NONE:
		/*
		 * Flow control is completely disabled by a software
		 * over-ride.
		 */
		txcw = (E1000_TXCW_ANE | E1000_TXCW_FD);
		break;
	case E1000_FC_RX_PAUSE:
		/*
		 * RX Flow control is enabled and TX Flow control is disabled
		 * by a software over-ride. Since there really isn't a way to
		 * advertise that we are capable of RX Pause ONLY, we will
		 * advertise that we support both symmetric and asymmetric RX
		 * PAUSE. Later, we will disable the adapter's ability to
		 * send PAUSE frames.
		 */
		txcw = (E1000_TXCW_ANE | E1000_TXCW_FD | 
		    E1000_TXCW_PAUSE_MASK);
		break;
	case E1000_FC_TX_PAUSE:
		/*
		 * TX Flow control is enabled, and RX Flow control is
		 * disabled, by a software over-ride.
		 */
		txcw = (E1000_TXCW_ANE | E1000_TXCW_FD | E1000_TXCW_ASM_DIR);
		break;
	case E1000_FC_FULL:
		/*
		 * Flow control (both RX and TX) is enabled by a software
		 * over-ride.
		 */
		txcw = (E1000_TXCW_ANE | E1000_TXCW_FD | 
		    E1000_TXCW_PAUSE_MASK);
		break;
	default:
		DEBUGOUT("Flow control param set incorrectly\n");
		return -E1000_ERR_CONFIG;
		break;
	}
	/*
	 * Since auto-negotiation is enabled, take the link out of reset (the
	 * link will be in reset, because we previously reset the chip). This
	 * will restart auto-negotiation.  If auto-negotiation is successful
	 * then the link-up status bit will be set and the flow control
	 * enable bits (RFCE and TFCE) will be set according to their
	 * negotiated value.
	 */
	DEBUGOUT("Auto-negotiation enabled\n");

	E1000_WRITE_REG(hw, TXCW, txcw);
	E1000_WRITE_REG(hw, CTRL, ctrl);
	E1000_WRITE_FLUSH(hw);

	hw->txcw = txcw;
	msec_delay(1);
	/*
	 * If we have a signal (the cable is plugged in) then poll for a
	 * "Link-Up" indication in the Device Status Register.  Time-out if a
	 * link isn't seen in 500 milliseconds seconds (Auto-negotiation
	 * should complete in less than 500 milliseconds even if the other
	 * end is doing it in SW). For internal serdes, we just assume a
	 * signal is present, then poll.
	 */
	if (hw->media_type == em_media_type_internal_serdes ||
	    (E1000_READ_REG(hw, CTRL) & E1000_CTRL_SWDPIN1) == signal) {
		DEBUGOUT("Looking for Link\n");
		for (i = 0; i < (LINK_UP_TIMEOUT / 10); i++) {
			msec_delay(10);
			status = E1000_READ_REG(hw, STATUS);
			if (status & E1000_STATUS_LU)
				break;
		}
		if (i == (LINK_UP_TIMEOUT / 10)) {
			DEBUGOUT("Never got a valid link from auto-neg!!!\n");
			hw->autoneg_failed = 1;
			/*
			 * AutoNeg failed to achieve a link, so we'll call
			 * em_check_for_link. This routine will force the
			 * link up if we detect a signal. This will allow us
			 * to communicate with non-autonegotiating link
			 * partners.
			 */
			ret_val = em_check_for_link(hw);
			if (ret_val) {
				DEBUGOUT("Error while checking for link\n");
				return ret_val;
			}
			hw->autoneg_failed = 0;
		} else {
			hw->autoneg_failed = 0;
			DEBUGOUT("Valid Link Found\n");
		}
	} else {
		DEBUGOUT("No Signal Detected\n");
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 * Make sure we have a valid PHY and change PHY mode before link setup.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static int32_t
em_copper_link_preconfig(struct em_hw *hw)
{
	uint32_t ctrl;
	int32_t  ret_val;
	uint16_t phy_data;
	DEBUGFUNC("em_copper_link_preconfig");

	ctrl = E1000_READ_REG(hw, CTRL);
	/*
	 * With 82543, we need to force speed and duplex on the MAC equal to
	 * what the PHY speed and duplex configuration is. In addition, we
	 * need to perform a hardware reset on the PHY to take it out of
	 * reset.
	 */
	if (hw->mac_type > em_82543) {
		ctrl |= E1000_CTRL_SLU;
		ctrl &= ~(E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
		E1000_WRITE_REG(hw, CTRL, ctrl);
	} else {
		ctrl |= (E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX | 
		    E1000_CTRL_SLU);
		E1000_WRITE_REG(hw, CTRL, ctrl);
		ret_val = em_phy_hw_reset(hw);
		if (ret_val)
			return ret_val;
	}

	/* Set PHY to class A mode (if necessary) */
	ret_val = em_set_phy_mode(hw);
	if (ret_val)
		return ret_val;

	if ((hw->mac_type == em_82545_rev_3) ||
	    (hw->mac_type == em_82546_rev_3)) {
		ret_val = em_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, 
		    &phy_data);
		phy_data |= 0x00000008;
		ret_val = em_write_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, 
		    phy_data);
	}
	if (hw->mac_type <= em_82543 ||
	    hw->mac_type == em_82541 || hw->mac_type == em_82547 ||
	    hw->mac_type == em_82541_rev_2 || hw->mac_type == em_82547_rev_2)
		hw->phy_reset_disable = FALSE;
	if ((hw->mac_type == em_82575 || hw->mac_type == em_82580 ||
	    hw->mac_type == em_82576 ||
	    hw->mac_type == em_i210 || hw->mac_type == em_i350) &&
	    hw->sgmii_active) {
		/* allow time for SFP cage time to power up phy */
		msec_delay(300);

		/*
		 * SFP documentation requires the following to configure the SFP module
		 * to work on SGMII.  No further documentation is given.
		 */
		em_write_phy_reg(hw, 0x1B, 0x8084);
		em_phy_hw_reset(hw);
	}

	return E1000_SUCCESS;
}

/******************************************************************************
 * Copper link setup for em_phy_igp series.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static int32_t
em_copper_link_igp_setup(struct em_hw *hw)
{
	uint32_t led_ctrl;
	int32_t  ret_val;
	uint16_t phy_data;
	DEBUGFUNC("em_copper_link_igp_setup");

	if (hw->phy_reset_disable)
		return E1000_SUCCESS;

	ret_val = em_phy_reset(hw);
	if (ret_val) {
		DEBUGOUT("Error Resetting the PHY\n");
		return ret_val;
	}
	/* Wait 15ms for MAC to configure PHY from eeprom settings */
	msec_delay(15);
	if (hw->mac_type != em_ich8lan &&
	    hw->mac_type != em_ich9lan &&
	    hw->mac_type != em_ich10lan) {
		/* Configure activity LED after PHY reset */
		led_ctrl = E1000_READ_REG(hw, LEDCTL);
		led_ctrl &= IGP_ACTIVITY_LED_MASK;
		led_ctrl |= (IGP_ACTIVITY_LED_ENABLE | IGP_LED3_MODE);
		E1000_WRITE_REG(hw, LEDCTL, led_ctrl);
	}
	/* The NVM settings will configure LPLU in D3 for IGP2 and IGP3 PHYs */
	if (hw->phy_type == em_phy_igp) {
		/* disable lplu d3 during driver init */
		ret_val = em_set_d3_lplu_state(hw, FALSE);
		if (ret_val) {
			DEBUGOUT("Error Disabling LPLU D3\n");
			return ret_val;
		}
	}
	/* disable lplu d0 during driver init */
	if (hw->mac_type == em_pchlan ||
		hw->mac_type == em_pch2lan ||
		hw->mac_type == em_pch_lpt ||
		hw->mac_type == em_pch_spt ||
		hw->mac_type == em_pch_cnp ||
		hw->mac_type == em_pch_tgp ||
		hw->mac_type == em_pch_adp)
		ret_val = em_set_lplu_state_pchlan(hw, FALSE);
	else
		ret_val = em_set_d0_lplu_state(hw, FALSE);
	if (ret_val) {
		DEBUGOUT("Error Disabling LPLU D0\n");
		return ret_val;
	}
	/* Configure mdi-mdix settings */
	ret_val = em_read_phy_reg(hw, IGP01E1000_PHY_PORT_CTRL, &phy_data);
	if (ret_val)
		return ret_val;

	if ((hw->mac_type == em_82541) || (hw->mac_type == em_82547)) {
		hw->dsp_config_state = em_dsp_config_disabled;
		/* Force MDI for earlier revs of the IGP PHY */
		phy_data &= ~(IGP01E1000_PSCR_AUTO_MDIX | 
		    IGP01E1000_PSCR_FORCE_MDI_MDIX);
		hw->mdix = 1;

	} else {
		hw->dsp_config_state = em_dsp_config_enabled;
		phy_data &= ~IGP01E1000_PSCR_AUTO_MDIX;

		switch (hw->mdix) {
		case 1:
			phy_data &= ~IGP01E1000_PSCR_FORCE_MDI_MDIX;
			break;
		case 2:
			phy_data |= IGP01E1000_PSCR_FORCE_MDI_MDIX;
			break;
		case 0:
		default:
			phy_data |= IGP01E1000_PSCR_AUTO_MDIX;
			break;
		}
	}
	ret_val = em_write_phy_reg(hw, IGP01E1000_PHY_PORT_CTRL, phy_data);
	if (ret_val)
		return ret_val;

	/* set auto-master slave resolution settings */
	if (hw->autoneg) {
		em_ms_type phy_ms_setting = hw->master_slave;
		if (hw->ffe_config_state == em_ffe_config_active)
			hw->ffe_config_state = em_ffe_config_enabled;

		if (hw->dsp_config_state == em_dsp_config_activated)
			hw->dsp_config_state = em_dsp_config_enabled;
		/*
		 * when autonegotiation advertisement is only 1000Mbps then
		 * we should disable SmartSpeed and enable Auto MasterSlave
		 * resolution as hardware default.
		 */
		if (hw->autoneg_advertised == ADVERTISE_1000_FULL) {
			/* Disable SmartSpeed */
			ret_val = em_read_phy_reg(hw, 
			    IGP01E1000_PHY_PORT_CONFIG, &phy_data);
			if (ret_val)
				return ret_val;

			phy_data &= ~IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = em_write_phy_reg(hw, 
			    IGP01E1000_PHY_PORT_CONFIG, phy_data);
			if (ret_val)
				return ret_val;
			/* Set auto Master/Slave resolution process */
			ret_val = em_read_phy_reg(hw, PHY_1000T_CTRL, 
			    &phy_data);
			if (ret_val)
				return ret_val;

			phy_data &= ~CR_1000T_MS_ENABLE;
			ret_val = em_write_phy_reg(hw, PHY_1000T_CTRL, 
			    phy_data);
			if (ret_val)
				return ret_val;
		}
		ret_val = em_read_phy_reg(hw, PHY_1000T_CTRL, &phy_data);
		if (ret_val)
			return ret_val;

		/* load defaults for future use */
		hw->original_master_slave = (phy_data & CR_1000T_MS_ENABLE) ?
		    ((phy_data & CR_1000T_MS_VALUE) ? em_ms_force_master : 
		    em_ms_force_slave) : em_ms_auto;

		switch (phy_ms_setting) {
		case em_ms_force_master:
			phy_data |= (CR_1000T_MS_ENABLE | CR_1000T_MS_VALUE);
			break;
		case em_ms_force_slave:
			phy_data |= CR_1000T_MS_ENABLE;
			phy_data &= ~(CR_1000T_MS_VALUE);
			break;
		case em_ms_auto:
			phy_data &= ~CR_1000T_MS_ENABLE;
			break;
		default:
			break;
		}
		ret_val = em_write_phy_reg(hw, PHY_1000T_CTRL, phy_data);
		if (ret_val)
			return ret_val;
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 * Copper link setup for em_phy_gg82563 series.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static int32_t
em_copper_link_ggp_setup(struct em_hw *hw)
{
	int32_t  ret_val;
	uint16_t phy_data;
	uint32_t reg_data;
	DEBUGFUNC("em_copper_link_ggp_setup");

	if (!hw->phy_reset_disable) {

		/* Enable CRS on TX for half-duplex operation. */
		ret_val = em_read_phy_reg(hw, GG82563_PHY_MAC_SPEC_CTRL,
		    &phy_data);
		if (ret_val)
			return ret_val;

		phy_data |= GG82563_MSCR_ASSERT_CRS_ON_TX;
		/* Use 25MHz for both link down and 1000BASE-T for Tx clock */
		phy_data |= GG82563_MSCR_TX_CLK_1000MBPS_25MHZ;

		ret_val = em_write_phy_reg(hw, GG82563_PHY_MAC_SPEC_CTRL,
		    phy_data);
		if (ret_val)
			return ret_val;
		/*
		 * Options: MDI/MDI-X = 0 (default) 0 - Auto for all speeds 1
		 * - MDI mode 2 - MDI-X mode 3 - Auto for 1000Base-T only
		 * (MDI-X for 10/100Base-T modes)
		 */
		ret_val = em_read_phy_reg(hw, GG82563_PHY_SPEC_CTRL,
		    &phy_data);

		if (ret_val)
			return ret_val;

		phy_data &= ~GG82563_PSCR_CROSSOVER_MODE_MASK;

		switch (hw->mdix) {
		case 1:
			phy_data |= GG82563_PSCR_CROSSOVER_MODE_MDI;
			break;
		case 2:
			phy_data |= GG82563_PSCR_CROSSOVER_MODE_MDIX;
			break;
		case 0:
		default:
			phy_data |= GG82563_PSCR_CROSSOVER_MODE_AUTO;
			break;
		}
		/*
		 * Options: disable_polarity_correction = 0 (default)
		 * Automatic Correction for Reversed Cable Polarity 0 -
		 * Disabled 1 - Enabled
		 */
		phy_data &= ~GG82563_PSCR_POLARITY_REVERSAL_DISABLE;
		if (hw->disable_polarity_correction == 1)
			phy_data |= GG82563_PSCR_POLARITY_REVERSAL_DISABLE;
		ret_val = em_write_phy_reg(hw, GG82563_PHY_SPEC_CTRL,
		    phy_data);

		if (ret_val)
			return ret_val;

		/* SW Reset the PHY so all changes take effect */
		ret_val = em_phy_reset(hw);
		if (ret_val) {
			DEBUGOUT("Error Resetting the PHY\n");
			return ret_val;
		}
	}			/* phy_reset_disable */
	if (hw->mac_type == em_80003es2lan) {
		/* Bypass RX and TX FIFO's */
		ret_val = em_write_kmrn_reg(hw, 
		    E1000_KUMCTRLSTA_OFFSET_FIFO_CTRL,
		    E1000_KUMCTRLSTA_FIFO_CTRL_RX_BYPASS |
		    E1000_KUMCTRLSTA_FIFO_CTRL_TX_BYPASS);
		if (ret_val)
			return ret_val;

		ret_val = em_read_phy_reg(hw, GG82563_PHY_SPEC_CTRL_2, 
		    &phy_data);
		if (ret_val)
			return ret_val;

		phy_data &= ~GG82563_PSCR2_REVERSE_AUTO_NEG;
		ret_val = em_write_phy_reg(hw, GG82563_PHY_SPEC_CTRL_2, 
		    phy_data);

		if (ret_val)
			return ret_val;

		reg_data = E1000_READ_REG(hw, CTRL_EXT);
		reg_data &= ~(E1000_CTRL_EXT_LINK_MODE_MASK);
		E1000_WRITE_REG(hw, CTRL_EXT, reg_data);

		ret_val = em_read_phy_reg(hw, GG82563_PHY_PWR_MGMT_CTRL,
		    &phy_data);
		if (ret_val)
			return ret_val;
		/*
		 * Do not init these registers when the HW is in IAMT mode,
		 * since the firmware will have already initialized them. We
		 * only initialize them if the HW is not in IAMT mode.
		 */
		if (em_check_mng_mode(hw) == FALSE) {
			/* Enable Electrical Idle on the PHY */
			phy_data |= GG82563_PMCR_ENABLE_ELECTRICAL_IDLE;
			ret_val = em_write_phy_reg(hw, 
			    GG82563_PHY_PWR_MGMT_CTRL, phy_data);
			if (ret_val)
				return ret_val;

			ret_val = em_read_phy_reg(hw, 
			    GG82563_PHY_KMRN_MODE_CTRL, &phy_data);
			if (ret_val)
				return ret_val;

			phy_data &= ~GG82563_KMCR_PASS_FALSE_CARRIER;
			ret_val = em_write_phy_reg(hw, 
			    GG82563_PHY_KMRN_MODE_CTRL, phy_data);

			if (ret_val)
				return ret_val;
		}
		/*
		 * Workaround: Disable padding in Kumeran interface in the
		 * MAC and in the PHY to avoid CRC errors.
		 */
		ret_val = em_read_phy_reg(hw, GG82563_PHY_INBAND_CTRL,
		    &phy_data);
		if (ret_val)
			return ret_val;
		phy_data |= GG82563_ICR_DIS_PADDING;
		ret_val = em_write_phy_reg(hw, GG82563_PHY_INBAND_CTRL, 
		    phy_data);
		if (ret_val)
			return ret_val;
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 * Copper link setup for em_phy_m88 series.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static int32_t
em_copper_link_mgp_setup(struct em_hw *hw)
{
	int32_t  ret_val;
	uint16_t phy_data;
	DEBUGFUNC("em_copper_link_mgp_setup");

	if (hw->phy_reset_disable)
		return E1000_SUCCESS;

	/* disable lplu d0 during driver init */
	if (hw->mac_type == em_pchlan ||
		hw->mac_type == em_pch2lan ||
		hw->mac_type == em_pch_lpt ||
		hw->mac_type == em_pch_spt ||
		hw->mac_type == em_pch_cnp ||
		hw->mac_type == em_pch_tgp ||
		hw->mac_type == em_pch_adp)
		ret_val = em_set_lplu_state_pchlan(hw, FALSE);

	/* Enable CRS on TX. This must be set for half-duplex operation. */
	ret_val = em_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
	if (ret_val)
		return ret_val;

	if (hw->phy_id == M88E1141_E_PHY_ID) {
		phy_data |= 0x00000008;
		ret_val = em_write_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, 
		    phy_data);
		if (ret_val)
			return ret_val;

		ret_val = em_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, 
		    &phy_data);
		if (ret_val)
			return ret_val;

		phy_data &= ~M88E1000_PSCR_ASSERT_CRS_ON_TX;

	}
	/* For BM PHY this bit is downshift enable */
	else if (hw->phy_type != em_phy_bm)
		phy_data |= M88E1000_PSCR_ASSERT_CRS_ON_TX;
	/*
	 * Options: MDI/MDI-X = 0 (default) 0 - Auto for all speeds 1 - MDI
	 * mode 2 - MDI-X mode 3 - Auto for 1000Base-T only (MDI-X for
	 * 10/100Base-T modes)
	 */
	phy_data &= ~M88E1000_PSCR_AUTO_X_MODE;

	switch (hw->mdix) {
	case 1:
		phy_data |= M88E1000_PSCR_MDI_MANUAL_MODE;
		break;
	case 2:
		phy_data |= M88E1000_PSCR_MDIX_MANUAL_MODE;
		break;
	case 3:
		phy_data |= M88E1000_PSCR_AUTO_X_1000T;
		break;
	case 0:
	default:
		phy_data |= M88E1000_PSCR_AUTO_X_MODE;
		break;
	}
	/*
	 * Options: disable_polarity_correction = 0 (default) Automatic
	 * Correction for Reversed Cable Polarity 0 - Disabled 1 - Enabled
	 */
	phy_data &= ~M88E1000_PSCR_POLARITY_REVERSAL;
	if (hw->disable_polarity_correction == 1)
		phy_data |= M88E1000_PSCR_POLARITY_REVERSAL;

	/* Enable downshift on BM (disabled by default) */
	if (hw->phy_type == em_phy_bm)
		phy_data |= BME1000_PSCR_ENABLE_DOWNSHIFT;

	ret_val = em_write_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, phy_data);
	if (ret_val)
		return ret_val;

	if (((hw->phy_type == em_phy_m88) &&
	    (hw->phy_revision < M88E1011_I_REV_4) &&
	    (hw->phy_id != BME1000_E_PHY_ID)) ||
	    (hw->phy_type == em_phy_oem)) {
		/*
		 * Force TX_CLK in the Extended PHY Specific Control Register
		 * to 25MHz clock.
		 */
		ret_val = em_read_phy_reg(hw, M88E1000_EXT_PHY_SPEC_CTRL,
		    &phy_data);
		if (ret_val)
			return ret_val;

		if (hw->phy_type == em_phy_oem) {
			phy_data |= M88E1000_EPSCR_TX_TIME_CTRL;
			phy_data |= M88E1000_EPSCR_RX_TIME_CTRL;
		}
		phy_data |= M88E1000_EPSCR_TX_CLK_25;

		if ((hw->phy_revision == E1000_REVISION_2) &&
		    (hw->phy_id == M88E1111_I_PHY_ID)) {
			/* Vidalia Phy, set the downshift counter to 5x */
			phy_data &= ~(M88EC018_EPSCR_DOWNSHIFT_COUNTER_MASK);
			phy_data |= M88EC018_EPSCR_DOWNSHIFT_COUNTER_5X;
			ret_val = em_write_phy_reg(hw,
			    M88E1000_EXT_PHY_SPEC_CTRL, phy_data);
			if (ret_val)
				return ret_val;
		} else {
			/* Configure Master and Slave downshift values */
			phy_data &= ~(M88E1000_EPSCR_MASTER_DOWNSHIFT_MASK |
			    M88E1000_EPSCR_SLAVE_DOWNSHIFT_MASK);
			phy_data |= (M88E1000_EPSCR_MASTER_DOWNSHIFT_1X |
			    M88E1000_EPSCR_SLAVE_DOWNSHIFT_1X);
			ret_val = em_write_phy_reg(hw,
			    M88E1000_EXT_PHY_SPEC_CTRL, phy_data);
			if (ret_val)
				return ret_val;
		}
	}
	if ((hw->phy_type == em_phy_bm) && (hw->phy_revision == 1)) {
		/*
	    	 * Set PHY page 0, register 29 to 0x0003
	         * The next two writes are supposed to lower BER for gig
	         * connection
		 */
		ret_val = em_write_phy_reg(hw, BM_REG_BIAS1, 0x0003);
		if (ret_val)
			return ret_val;

		/* Set PHY page 0, register 30 to 0x0000 */
		ret_val = em_write_phy_reg(hw, BM_REG_BIAS2, 0x0000);
		if (ret_val)
			return ret_val;
	}
	if (hw->phy_type == em_phy_82578) {
		ret_val = em_read_phy_reg(hw, M88E1000_EXT_PHY_SPEC_CTRL,
		    &phy_data);
		if (ret_val)
			return ret_val;

		/* 82578 PHY - set the downshift count to 1x. */
		phy_data |= I82578_EPSCR_DOWNSHIFT_ENABLE;
		phy_data &= ~I82578_EPSCR_DOWNSHIFT_COUNTER_MASK;
		ret_val = em_write_phy_reg(hw, M88E1000_EXT_PHY_SPEC_CTRL,
		    phy_data);
		if (ret_val)
			return ret_val;
	}
	/* SW Reset the PHY so all changes take effect */
	ret_val = em_phy_reset(hw);
	if (ret_val) {
		DEBUGOUT("Error Resetting the PHY\n");
		return ret_val;
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 * Copper link setup for em_phy_82577 series.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static int32_t
em_copper_link_82577_setup(struct em_hw *hw)
{
	int32_t  ret_val;
	uint16_t phy_data;
	uint32_t led_ctl;
	DEBUGFUNC("em_copper_link_82577_setup");

	if (hw->phy_reset_disable)
		return E1000_SUCCESS;

	/* Enable CRS on TX for half-duplex operation. */
	ret_val = em_read_phy_reg(hw, I82577_PHY_CFG_REG, &phy_data);
	if (ret_val)
		return ret_val;

	phy_data |= I82577_PHY_CFG_ENABLE_CRS_ON_TX |
	    I82577_PHY_CFG_ENABLE_DOWNSHIFT;

	ret_val = em_write_phy_reg(hw, I82577_PHY_CFG_REG, phy_data);
	if (ret_val)
		return ret_val;

	/* Wait 15ms for MAC to configure PHY from eeprom settings */
	msec_delay(15);
	led_ctl = hw->ledctl_mode1;

	/* disable lplu d0 during driver init */
	ret_val = em_set_lplu_state_pchlan(hw, FALSE);
	if (ret_val) {
		DEBUGOUT("Error Disabling LPLU D0\n");
		return ret_val;
	}

	E1000_WRITE_REG(hw, LEDCTL, led_ctl);

	return E1000_SUCCESS;
}

static int32_t
em_copper_link_82580_setup(struct em_hw *hw)
{
	int32_t ret_val;
	uint16_t phy_data;

	if (hw->phy_reset_disable)
		return E1000_SUCCESS;

	ret_val = em_phy_reset(hw);
	if (ret_val)
		goto out;

	/* Enable CRS on TX. This must be set for half-duplex operation. */
	ret_val = em_read_phy_reg(hw, I82580_CFG_REG, &phy_data);
	if (ret_val)
		goto out;

	phy_data |= I82580_CFG_ASSERT_CRS_ON_TX |
	    I82580_CFG_ENABLE_DOWNSHIFT;

	ret_val = em_write_phy_reg(hw, I82580_CFG_REG, phy_data);

out:
	return ret_val;
}

static int32_t
em_copper_link_rtl8211_setup(struct em_hw *hw)
{
	int32_t ret_val;
	uint16_t phy_data;

	DEBUGFUNC("em_copper_link_rtl8211_setup: begin");

	if (!hw) {
		return -1;
	}
	/* SW Reset the PHY so all changes take effect */
	em_phy_hw_reset(hw);

	/* Enable CRS on TX. This must be set for half-duplex operation. */
	phy_data = 0;

	ret_val = em_read_phy_reg_ex(hw, RGEPHY_CR, &phy_data);
	if (ret_val) {
		printf("Unable to read RGEPHY_CR register\n");
		return ret_val;
	}
	DEBUGOUT3("RTL8211: Rx phy_id=%X addr=%X SPEC_CTRL=%X\n", hw->phy_id,
	    hw->phy_addr, phy_data);
	phy_data |= RGEPHY_CR_ASSERT_CRS;

	ret_val = em_write_phy_reg_ex(hw, RGEPHY_CR, phy_data);
	if (ret_val) {
		printf("Unable to write RGEPHY_CR register\n");
		return ret_val;
	}

	phy_data = 0; /* LED Control Register 0x18 */
	ret_val = em_read_phy_reg_ex(hw, RGEPHY_LC, &phy_data);
	if (ret_val) {
		printf("Unable to read RGEPHY_LC register\n");
		return ret_val;
	}

	phy_data &= 0x80FF; /* bit-15=0 disable, clear bit 8-10 */
	ret_val = em_write_phy_reg_ex(hw, RGEPHY_LC, phy_data);
	if (ret_val) {
		printf("Unable to write RGEPHY_LC register\n");
		return ret_val;
	}
	/* LED Control and Definition Register 0x11, PHY spec status reg */
	phy_data = 0;
	ret_val = em_read_phy_reg_ex(hw, RGEPHY_SR, &phy_data);
	if (ret_val) {
		printf("Unable to read RGEPHY_SR register\n");
		return ret_val;
	}

	phy_data |= 0x0010; /* LED active Low */
	ret_val = em_write_phy_reg_ex(hw, RGEPHY_SR, phy_data);
	if (ret_val) {
		printf("Unable to write RGEPHY_SR register\n");
		return ret_val;
	}

	phy_data = 0;
	ret_val = em_read_phy_reg_ex(hw, RGEPHY_SR, &phy_data);
	if (ret_val) {
		printf("Unable to read RGEPHY_SR register\n");
		return ret_val;
	}

	/* Switch to Page2 */
	phy_data = RGEPHY_PS_PAGE_2;
	ret_val = em_write_phy_reg_ex(hw, RGEPHY_PS, phy_data);
	if (ret_val) {
		printf("Unable to write PHY RGEPHY_PS register\n");
		return ret_val;
	}

	phy_data = 0x0000;
	ret_val = em_write_phy_reg_ex(hw, RGEPHY_LC_P2, phy_data);
	if (ret_val) {
		printf("Unable to write RGEPHY_LC_P2 register\n");
		return ret_val;
	}
	usec_delay(5);


	/* LED Configuration Control Reg for setting for 0x1A Register */
	phy_data = 0;
	ret_val = em_read_phy_reg_ex(hw, RGEPHY_LC_P2, &phy_data);
	if (ret_val) {
		printf("Unable to read RGEPHY_LC_P2 register\n");
		return ret_val;
	}

	phy_data &= 0xF000;
	phy_data |= 0x0F24;
	ret_val = em_write_phy_reg_ex(hw, RGEPHY_LC_P2, phy_data);
	if (ret_val) {
		printf("Unable to write RGEPHY_LC_P2 register\n");
		return ret_val;
	}
	phy_data = 0;
	ret_val= em_read_phy_reg_ex(hw, RGEPHY_LC_P2, &phy_data);
	if (ret_val) {
		printf("Unable to read RGEPHY_LC_P2 register\n");
		return ret_val;
	}
	DEBUGOUT1("RTL8211:ReadBack for check, LED_CFG->data=%X\n", phy_data);


	/* After setting Page2, go back to Page 0 */
	phy_data = 0;
	ret_val = em_write_phy_reg_ex(hw, RGEPHY_PS, phy_data);
	if (ret_val) {
		printf("Unable to write PHY RGEPHY_PS register\n");
		return ret_val;
	}

	/* pulse stretching= 42-84ms, blink rate=84mm */
	phy_data = 0x140 | RGEPHY_LC_PULSE_42MS | RGEPHY_LC_LINK | 
	    RGEPHY_LC_DUPLEX | RGEPHY_LC_RX;

	ret_val = em_write_phy_reg_ex(hw, RGEPHY_LC, phy_data);
	if (ret_val) {
		printf("Unable to write RGEPHY_LC register\n");
		return ret_val;
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 * Setup auto-negotiation and flow control advertisements,
 * and then perform auto-negotiation.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
int32_t
em_copper_link_autoneg(struct em_hw *hw)
{
	int32_t  ret_val;
	uint16_t phy_data;
	DEBUGFUNC("em_copper_link_autoneg");
	/*
	 * Perform some bounds checking on the hw->autoneg_advertised
	 * parameter.  If this variable is zero, then set it to the default.
	 */
	hw->autoneg_advertised &= AUTONEG_ADVERTISE_SPEED_DEFAULT;
	/*
	 * If autoneg_advertised is zero, we assume it was not defaulted by
	 * the calling code so we set to advertise full capability.
	 */
	if (hw->autoneg_advertised == 0)
		hw->autoneg_advertised = AUTONEG_ADVERTISE_SPEED_DEFAULT;

	/* IFE phy only supports 10/100 */
	if (hw->phy_type == em_phy_ife)
		hw->autoneg_advertised &= AUTONEG_ADVERTISE_10_100_ALL;

	DEBUGOUT("Reconfiguring auto-neg advertisement params\n");
	ret_val = em_phy_setup_autoneg(hw);
	if (ret_val) {
		DEBUGOUT("Error Setting up Auto-Negotiation\n");
		return ret_val;
	}
	DEBUGOUT("Restarting Auto-Neg\n");
	/*
	 * Restart auto-negotiation by setting the Auto Neg Enable bit and
	 * the Auto Neg Restart bit in the PHY control register.
	 */
	ret_val = em_read_phy_reg(hw, PHY_CTRL, &phy_data);
	if (ret_val)
		return ret_val;

	phy_data |= (MII_CR_AUTO_NEG_EN | MII_CR_RESTART_AUTO_NEG);
	ret_val = em_write_phy_reg(hw, PHY_CTRL, phy_data);
	if (ret_val)
		return ret_val;
	/*
	 * Does the user want to wait for Auto-Neg to complete here, or check
	 * at a later time (for example, callback routine).
	 */
	if (hw->wait_autoneg_complete) {
		ret_val = em_wait_autoneg(hw);
		if (ret_val) {
			DEBUGOUT("Error while waiting for autoneg to complete\n"
			    );
			return ret_val;
		}
	}
	hw->get_link_status = TRUE;

	return E1000_SUCCESS;
}

/******************************************************************************
 * Config the MAC and the PHY after link is up.
 *   1) Set up the MAC to the current PHY speed/duplex
 *      if we are on 82543.  If we
 *      are on newer silicon, we only need to configure
 *      collision distance in the Transmit Control Register.
 *   2) Set up flow control on the MAC to that established with
 *      the link partner.
 *   3) Config DSP to improve Gigabit link quality for some PHY revisions.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
int32_t
em_copper_link_postconfig(struct em_hw *hw)
{
	int32_t ret_val;
	DEBUGFUNC("em_copper_link_postconfig");

	if (hw->mac_type >= em_82544 &&
	    hw->mac_type != em_icp_xxxx) {
		em_config_collision_dist(hw);
	} else {
		ret_val = em_config_mac_to_phy(hw);
		if (ret_val) {
			DEBUGOUT("Error configuring MAC to PHY settings\n");
			return ret_val;
		}
	}
	ret_val = em_config_fc_after_link_up(hw);
	if (ret_val) {
		DEBUGOUT("Error Configuring Flow Control\n");
		return ret_val;
	}
	/* Config DSP to improve Giga link quality */
	if (hw->phy_type == em_phy_igp) {
		ret_val = em_config_dsp_after_link_change(hw, TRUE);
		if (ret_val) {
			DEBUGOUT("Error Configuring DSP after link up\n");
			return ret_val;
		}
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 * Detects which PHY is present and setup the speed and duplex
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static int32_t
em_setup_copper_link(struct em_hw *hw)
{
	int32_t         ret_val;
	uint16_t        i;
	uint16_t        phy_data;
	uint16_t        reg_data;
	DEBUGFUNC("em_setup_copper_link");

	switch (hw->mac_type) {
	case em_80003es2lan:
	case em_ich8lan:
	case em_ich9lan:
	case em_ich10lan:
	case em_pchlan:
	case em_pch2lan:
	case em_pch_lpt:
	case em_pch_spt:
	case em_pch_cnp:
	case em_pch_tgp:
	case em_pch_adp:
		/*
		 * Set the mac to wait the maximum time between each
		 * iteration and increase the max iterations when polling the
		 * phy; this fixes erroneous timeouts at 10Mbps.
		 */
		ret_val = em_write_kmrn_reg(hw, GG82563_REG(0x34, 4), 0xFFFF);
		if (ret_val)
			return ret_val;
		ret_val = em_read_kmrn_reg(hw, GG82563_REG(0x34, 9), 
		    &reg_data);
		if (ret_val)
			return ret_val;
		reg_data |= 0x3F;
		ret_val = em_write_kmrn_reg(hw, GG82563_REG(0x34, 9), 
		    reg_data);
		if (ret_val)
			return ret_val;
	default:
		break;
	}

	/* Check if it is a valid PHY and set PHY mode if necessary. */
	ret_val = em_copper_link_preconfig(hw);
	if (ret_val)
		return ret_val;

	switch (hw->mac_type) {
	case em_80003es2lan:
		/* Kumeran registers are written-only */
		reg_data = 
		    E1000_KUMCTRLSTA_INB_CTRL_LINK_STATUS_TX_TIMEOUT_DEFAULT;
		reg_data |= E1000_KUMCTRLSTA_INB_CTRL_DIS_PADDING;
		ret_val = em_write_kmrn_reg(hw, 
		    E1000_KUMCTRLSTA_OFFSET_INB_CTRL, reg_data);
		if (ret_val)
			return ret_val;
		break;
	default:
		break;
	}

	if (hw->phy_type == em_phy_igp ||
	    hw->phy_type == em_phy_igp_3 ||
	    hw->phy_type == em_phy_igp_2) {
		ret_val = em_copper_link_igp_setup(hw);
		if (ret_val)
			return ret_val;
	} else if (hw->phy_type == em_phy_m88 ||
	    hw->phy_type == em_phy_bm ||
	    hw->phy_type == em_phy_oem ||
	    hw->phy_type == em_phy_82578) {
		ret_val = em_copper_link_mgp_setup(hw);
		if (ret_val)
			return ret_val;
	} else if (hw->phy_type == em_phy_gg82563) {
		ret_val = em_copper_link_ggp_setup(hw);
		if (ret_val)
			return ret_val;
	} else if (hw->phy_type == em_phy_82577 ||
		hw->phy_type == em_phy_82579 ||
		hw->phy_type == em_phy_i217) {
		ret_val = em_copper_link_82577_setup(hw);
		if (ret_val)
			return ret_val;
	} else if (hw->phy_type == em_phy_82580) {
		ret_val = em_copper_link_82580_setup(hw);
		if (ret_val)
			return ret_val;
	} else if (hw->phy_type == em_phy_rtl8211) {
		ret_val = em_copper_link_rtl8211_setup(hw);
		if (ret_val)
			return ret_val;
	}
	if (hw->autoneg) {
		/*
		 * Setup autoneg and flow control advertisement and perform
		 * autonegotiation
		 */
		ret_val = em_copper_link_autoneg(hw);
		if (ret_val)
			return ret_val;
	} else {
		/*
		 * PHY will be set to 10H, 10F, 100H,or 100F depending on
		 * value from forced_speed_duplex.
		 */
		DEBUGOUT("Forcing speed and duplex\n");
		ret_val = em_phy_force_speed_duplex(hw);
		if (ret_val) {
			DEBUGOUT("Error Forcing Speed and Duplex\n");
			return ret_val;
		}
	}
	/*
	 * Check link status. Wait up to 100 microseconds for link to become
	 * valid.
	 */
	for (i = 0; i < 10; i++) {
		ret_val = em_read_phy_reg(hw, PHY_STATUS, &phy_data);
		if (ret_val)
			return ret_val;
		ret_val = em_read_phy_reg(hw, PHY_STATUS, &phy_data);
		if (ret_val)
			return ret_val;

		hw->icp_xxxx_is_link_up = (phy_data & MII_SR_LINK_STATUS) != 0;

		if (phy_data & MII_SR_LINK_STATUS) {
			/* Config the MAC and PHY after link is up */
			ret_val = em_copper_link_postconfig(hw);
			if (ret_val)
				return ret_val;

			DEBUGOUT("Valid link established!!!\n");
			return E1000_SUCCESS;
		}
		usec_delay(10);
	}

	DEBUGOUT("Unable to establish link!!!\n");
	return E1000_SUCCESS;
}

/******************************************************************************
 * Configure the MAC-to-PHY interface for 10/100Mbps
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static int32_t
em_configure_kmrn_for_10_100(struct em_hw *hw, uint16_t duplex)
{
	int32_t  ret_val = E1000_SUCCESS;
	uint32_t tipg;
	uint16_t reg_data;
	DEBUGFUNC("em_configure_kmrn_for_10_100");

	reg_data = E1000_KUMCTRLSTA_HD_CTRL_10_100_DEFAULT;
	ret_val = em_write_kmrn_reg(hw, E1000_KUMCTRLSTA_OFFSET_HD_CTRL,
	    reg_data);
	if (ret_val)
		return ret_val;

	/* Configure Transmit Inter-Packet Gap */
	tipg = E1000_READ_REG(hw, TIPG);
	tipg &= ~E1000_TIPG_IPGT_MASK;
	tipg |= DEFAULT_80003ES2LAN_TIPG_IPGT_10_100;
	E1000_WRITE_REG(hw, TIPG, tipg);

	ret_val = em_read_phy_reg(hw, GG82563_PHY_KMRN_MODE_CTRL, &reg_data);

	if (ret_val)
		return ret_val;

	if (duplex == HALF_DUPLEX)
		reg_data |= GG82563_KMCR_PASS_FALSE_CARRIER;
	else
		reg_data &= ~GG82563_KMCR_PASS_FALSE_CARRIER;

	ret_val = em_write_phy_reg(hw, GG82563_PHY_KMRN_MODE_CTRL, reg_data);

	return ret_val;
}

static int32_t
em_configure_kmrn_for_1000(struct em_hw *hw)
{
	int32_t  ret_val = E1000_SUCCESS;
	uint16_t reg_data;
	uint32_t tipg;
	DEBUGFUNC("em_configure_kmrn_for_1000");

	reg_data = E1000_KUMCTRLSTA_HD_CTRL_1000_DEFAULT;
	ret_val = em_write_kmrn_reg(hw, E1000_KUMCTRLSTA_OFFSET_HD_CTRL,
	    reg_data);
	if (ret_val)
		return ret_val;

	/* Configure Transmit Inter-Packet Gap */
	tipg = E1000_READ_REG(hw, TIPG);
	tipg &= ~E1000_TIPG_IPGT_MASK;
	tipg |= DEFAULT_80003ES2LAN_TIPG_IPGT_1000;
	E1000_WRITE_REG(hw, TIPG, tipg);

	ret_val = em_read_phy_reg(hw, GG82563_PHY_KMRN_MODE_CTRL, &reg_data);

	if (ret_val)
		return ret_val;

	reg_data &= ~GG82563_KMCR_PASS_FALSE_CARRIER;
	ret_val = em_write_phy_reg(hw, GG82563_PHY_KMRN_MODE_CTRL, reg_data);

	return ret_val;
}

/******************************************************************************
 * Configures PHY autoneg and flow control advertisement settings
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
int32_t
em_phy_setup_autoneg(struct em_hw *hw)
{
	int32_t  ret_val;
	uint16_t mii_autoneg_adv_reg;
	uint16_t mii_1000t_ctrl_reg;
	DEBUGFUNC("em_phy_setup_autoneg");

	/* Read the MII Auto-Neg Advertisement Register (Address 4). */
	ret_val = em_read_phy_reg(hw, PHY_AUTONEG_ADV, &mii_autoneg_adv_reg);
	if (ret_val)
		return ret_val;

	if (hw->phy_type != em_phy_ife) {
		/* Read the MII 1000Base-T Control Register (Address 9). */
		ret_val = em_read_phy_reg(hw, PHY_1000T_CTRL, 
		    &mii_1000t_ctrl_reg);
		if (ret_val)
			return ret_val;
	} else
		mii_1000t_ctrl_reg = 0;
	/*
	 * Need to parse both autoneg_advertised and fc and set up the
	 * appropriate PHY registers.  First we will parse for
	 * autoneg_advertised software override.  Since we can advertise a
	 * plethora of combinations, we need to check each bit individually.
	 */
	/*
	 * First we clear all the 10/100 mb speed bits in the Auto-Neg
	 * Advertisement Register (Address 4) and the 1000 mb speed bits in
	 * the  1000Base-T Control Register (Address 9).
	 */
	mii_autoneg_adv_reg &= ~REG4_SPEED_MASK;
	mii_1000t_ctrl_reg &= ~REG9_SPEED_MASK;

	DEBUGOUT1("autoneg_advertised %x\n", hw->autoneg_advertised);

	/* Do we want to advertise 10 Mb Half Duplex? */
	if (hw->autoneg_advertised & ADVERTISE_10_HALF) {
		DEBUGOUT("Advertise 10mb Half duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_10T_HD_CAPS;
	}
	/* Do we want to advertise 10 Mb Full Duplex? */
	if (hw->autoneg_advertised & ADVERTISE_10_FULL) {
		DEBUGOUT("Advertise 10mb Full duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_10T_FD_CAPS;
	}
	/* Do we want to advertise 100 Mb Half Duplex? */
	if (hw->autoneg_advertised & ADVERTISE_100_HALF) {
		DEBUGOUT("Advertise 100mb Half duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_100TX_HD_CAPS;
	}
	/* Do we want to advertise 100 Mb Full Duplex? */
	if (hw->autoneg_advertised & ADVERTISE_100_FULL) {
		DEBUGOUT("Advertise 100mb Full duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_100TX_FD_CAPS;
	}
	/* We do not allow the Phy to advertise 1000 Mb Half Duplex */
	if (hw->autoneg_advertised & ADVERTISE_1000_HALF) {
		DEBUGOUT("Advertise 1000mb Half duplex requested, request"
		    " denied!\n");
	}
	/* Do we want to advertise 1000 Mb Full Duplex? */
	if (hw->autoneg_advertised & ADVERTISE_1000_FULL) {
		DEBUGOUT("Advertise 1000mb Full duplex\n");
		mii_1000t_ctrl_reg |= CR_1000T_FD_CAPS;
		if (hw->phy_type == em_phy_ife) {
			DEBUGOUT("em_phy_ife is a 10/100 PHY. Gigabit speed is"
			    " not supported.\n");
		}
	}
	/*
	 * Check for a software override of the flow control settings, and
	 * setup the PHY advertisement registers accordingly.  If
	 * auto-negotiation is enabled, then software will have to set the
	 * "PAUSE" bits to the correct value in the Auto-Negotiation
	 * Advertisement Register (PHY_AUTONEG_ADV) and re-start
	 * auto-negotiation.
	 *
	 * The possible values of the "fc" parameter are: 0:  Flow control is
	 * completely disabled 1:  Rx flow control is enabled (we can receive
	 * pause frames but not send pause frames). 2:  Tx flow control is
	 * enabled (we can send pause frames but we do not support receiving
	 * pause frames). 3:  Both Rx and TX flow control (symmetric) are
	 * enabled. other:  No software override.  The flow control
	 * configuration in the EEPROM is used.
	 */
	switch (hw->fc) {
	case E1000_FC_NONE:	/* 0 */
		/*
		 * Flow control (RX & TX) is completely disabled by a
		 * software over-ride.
		 */
		mii_autoneg_adv_reg &= ~(NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	case E1000_FC_RX_PAUSE:/* 1 */
		/*
		 * RX Flow control is enabled, and TX Flow control is
		 * disabled, by a software over-ride.
		 */
		/*
		 * Since there really isn't a way to advertise that we are
		 * capable of RX Pause ONLY, we will advertise that we
		 * support both symmetric and asymmetric RX PAUSE.  Later (in
		 * em_config_fc_after_link_up) we will disable the hw's
		 * ability to send PAUSE frames.
		 */
		mii_autoneg_adv_reg |= (NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	case E1000_FC_TX_PAUSE:/* 2 */
		/*
		 * TX Flow control is enabled, and RX Flow control is
		 * disabled, by a software over-ride.
		 */
		mii_autoneg_adv_reg |= NWAY_AR_ASM_DIR;
		mii_autoneg_adv_reg &= ~NWAY_AR_PAUSE;
		break;
	case E1000_FC_FULL:	/* 3 */
		/*
		 * Flow control (both RX and TX) is enabled by a software
		 * over-ride.
		 */
		mii_autoneg_adv_reg |= (NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	default:
		DEBUGOUT("Flow control param set incorrectly\n");
		return -E1000_ERR_CONFIG;
	}

	ret_val = em_write_phy_reg(hw, PHY_AUTONEG_ADV, mii_autoneg_adv_reg);
	if (ret_val)
		return ret_val;

	DEBUGOUT1("Auto-Neg Advertising %x\n", mii_autoneg_adv_reg);

	if (hw->phy_type != em_phy_ife) {
		ret_val = em_write_phy_reg(hw, PHY_1000T_CTRL,
		    mii_1000t_ctrl_reg);
		if (ret_val)
			return ret_val;
	}
	return E1000_SUCCESS;
}
/******************************************************************************
 * Force PHY speed and duplex settings to hw->forced_speed_duplex
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static int32_t
em_phy_force_speed_duplex(struct em_hw *hw)
{
	uint32_t ctrl;
	int32_t  ret_val;
	uint16_t mii_ctrl_reg;
	uint16_t mii_status_reg;
	uint16_t phy_data;
	uint16_t i;
	DEBUGFUNC("em_phy_force_speed_duplex");

	/* Turn off Flow control if we are forcing speed and duplex. */
	hw->fc = E1000_FC_NONE;

	DEBUGOUT1("hw->fc = %d\n", hw->fc);

	/* Read the Device Control Register. */
	ctrl = E1000_READ_REG(hw, CTRL);

	/* Set the bits to Force Speed and Duplex in the Device Ctrl Reg. */
	ctrl |= (E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
	ctrl &= ~(DEVICE_SPEED_MASK);

	/* Clear the Auto Speed Detect Enable bit. */
	ctrl &= ~E1000_CTRL_ASDE;

	/* Read the MII Control Register. */
	ret_val = em_read_phy_reg(hw, PHY_CTRL, &mii_ctrl_reg);
	if (ret_val)
		return ret_val;

	/* We need to disable autoneg in order to force link and duplex. */

	mii_ctrl_reg &= ~MII_CR_AUTO_NEG_EN;

	/* Are we forcing Full or Half Duplex? */
	if (hw->forced_speed_duplex == em_100_full ||
	    hw->forced_speed_duplex == em_10_full) {
		/*
		 * We want to force full duplex so we SET the full duplex
		 * bits in the Device and MII Control Registers.
		 */
		ctrl |= E1000_CTRL_FD;
		mii_ctrl_reg |= MII_CR_FULL_DUPLEX;
		DEBUGOUT("Full Duplex\n");
	} else {
		/*
		 * We want to force half duplex so we CLEAR the full duplex
		 * bits in the Device and MII Control Registers.
		 */
		ctrl &= ~E1000_CTRL_FD;
		mii_ctrl_reg &= ~MII_CR_FULL_DUPLEX;
		DEBUGOUT("Half Duplex\n");
	}

	/* Are we forcing 100Mbps??? */
	if (hw->forced_speed_duplex == em_100_full ||
	    hw->forced_speed_duplex == em_100_half) {
		/* Set the 100Mb bit and turn off the 1000Mb and 10Mb bits. */
		ctrl |= E1000_CTRL_SPD_100;
		mii_ctrl_reg |= MII_CR_SPEED_100;
		mii_ctrl_reg &= ~(MII_CR_SPEED_1000 | MII_CR_SPEED_10);
		DEBUGOUT("Forcing 100mb ");
	} else {
		/* Set the 10Mb bit and turn off the 1000Mb and 100Mb bits. */
		ctrl &= ~(E1000_CTRL_SPD_1000 | E1000_CTRL_SPD_100);
		mii_ctrl_reg |= MII_CR_SPEED_10;
		mii_ctrl_reg &= ~(MII_CR_SPEED_1000 | MII_CR_SPEED_100);
		DEBUGOUT("Forcing 10mb ");
	}

	em_config_collision_dist(hw);

	/* Write the configured values back to the Device Control Reg. */
	E1000_WRITE_REG(hw, CTRL, ctrl);

	if ((hw->phy_type == em_phy_m88) ||
	    (hw->phy_type == em_phy_gg82563) ||
	    (hw->phy_type == em_phy_bm) ||
	    (hw->phy_type == em_phy_oem ||
	    (hw->phy_type == em_phy_82578))) {
		ret_val = em_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL,
		    &phy_data);
		if (ret_val)
			return ret_val;
		/*
		 * Clear Auto-Crossover to force MDI manually. M88E1000
		 * requires MDI forced whenever speed are duplex are forced.
		 */
		phy_data &= ~M88E1000_PSCR_AUTO_X_MODE;
		ret_val = em_write_phy_reg(hw, M88E1000_PHY_SPEC_CTRL,
		    phy_data);
		if (ret_val)
			return ret_val;

		DEBUGOUT1("M88E1000 PSCR: %x \n", phy_data);

		/* Need to reset the PHY or these changes will be ignored */
		mii_ctrl_reg |= MII_CR_RESET;

	}
	else if (hw->phy_type == em_phy_rtl8211) {
		ret_val = em_read_phy_reg_ex(hw, RGEPHY_CR, &phy_data);
		if(ret_val) {
			printf("Unable to read RGEPHY_CR register\n"
			    );
			return ret_val;
		}

		/*
		 * Clear Auto-Crossover to force MDI manually. RTL8211 requires
		 * MDI forced whenever speed are duplex are forced.
		 */

		phy_data |= RGEPHY_CR_MDI_MASK;  // enable MDIX
		ret_val = em_write_phy_reg_ex(hw, RGEPHY_CR, phy_data);
		if(ret_val) {
			printf("Unable to write RGEPHY_CR register\n");
			return ret_val;
		}
		mii_ctrl_reg |= MII_CR_RESET;

	}
	/* Disable MDI-X support for 10/100 */
	else if (hw->phy_type == em_phy_ife) {
		ret_val = em_read_phy_reg(hw, IFE_PHY_MDIX_CONTROL, &phy_data);
		if (ret_val)
			return ret_val;

		phy_data &= ~IFE_PMC_AUTO_MDIX;
		phy_data &= ~IFE_PMC_FORCE_MDIX;

		ret_val = em_write_phy_reg(hw, IFE_PHY_MDIX_CONTROL, phy_data);
		if (ret_val)
			return ret_val;
	} else {
		/*
		 * Clear Auto-Crossover to force MDI manually.  IGP requires
		 * MDI forced whenever speed or duplex are forced.
		 */
		ret_val = em_read_phy_reg(hw, IGP01E1000_PHY_PORT_CTRL,
		    &phy_data);
		if (ret_val)
			return ret_val;

		phy_data &= ~IGP01E1000_PSCR_AUTO_MDIX;
		phy_data &= ~IGP01E1000_PSCR_FORCE_MDI_MDIX;

		ret_val = em_write_phy_reg(hw, IGP01E1000_PHY_PORT_CTRL,
		    phy_data);
		if (ret_val)
			return ret_val;
	}

	/* Write back the modified PHY MII control register. */
	ret_val = em_write_phy_reg(hw, PHY_CTRL, mii_ctrl_reg);
	if (ret_val)
		return ret_val;

	usec_delay(1);
	/*
	 * The wait_autoneg_complete flag may be a little misleading here.
	 * Since we are forcing speed and duplex, Auto-Neg is not enabled.
	 * But we do want to delay for a period while forcing only so we
	 * don't generate false No Link messages.  So we will wait here only
	 * if the user has set wait_autoneg_complete to 1, which is the
	 * default.
	 */
	if (hw->wait_autoneg_complete) {
		/* We will wait for autoneg to complete. */
		DEBUGOUT("Waiting for forced speed/duplex link.\n");
		mii_status_reg = 0;
		/*
		 * We will wait for autoneg to complete or 4.5 seconds to
		 * expire.
		 */
		for (i = PHY_FORCE_TIME; i > 0; i--) {
			/*
			 * Read the MII Status Register and wait for Auto-Neg
			 * Complete bit to be set.
			 */
			ret_val = em_read_phy_reg(hw, PHY_STATUS,
			    &mii_status_reg);
			if (ret_val)
				return ret_val;

			ret_val = em_read_phy_reg(hw, PHY_STATUS,
			    &mii_status_reg);
			if (ret_val)
				return ret_val;

			if (mii_status_reg & MII_SR_LINK_STATUS)
				break;
			msec_delay(100);
		}
		if ((i == 0) &&
		    ((hw->phy_type == em_phy_m88) ||
		    (hw->phy_type == em_phy_gg82563) ||
		    (hw->phy_type == em_phy_bm))) {
			/*
			 * We didn't get link.  Reset the DSP and wait again
			 * for link.
			 */
			ret_val = em_phy_reset_dsp(hw);
			if (ret_val) {
				DEBUGOUT("Error Resetting PHY DSP\n");
				return ret_val;
			}
		}
		/*
		 * This loop will early-out if the link condition has been
		 * met.
		 */
		for (i = PHY_FORCE_TIME; i > 0; i--) {
			if (mii_status_reg & MII_SR_LINK_STATUS)
				break;
			msec_delay(100);
			/*
			 * Read the MII Status Register and wait for Auto-Neg
			 * Complete bit to be set.
			 */
			ret_val = em_read_phy_reg(hw, PHY_STATUS,
			    &mii_status_reg);
			if (ret_val)
				return ret_val;

			ret_val = em_read_phy_reg(hw, PHY_STATUS,
			    &mii_status_reg);
			if (ret_val)
				return ret_val;
		}
	}
	if (hw->phy_type == em_phy_m88 ||
	    hw->phy_type == em_phy_bm ||
	    hw->phy_type == em_phy_oem) {
		/*
		 * Because we reset the PHY above, we need to re-force TX_CLK
		 * in the Extended PHY Specific Control Register to 25MHz
		 * clock.  This value defaults back to a 2.5MHz clock when
		 * the PHY is reset.
		 */
		ret_val = em_read_phy_reg(hw, M88E1000_EXT_PHY_SPEC_CTRL,
		    &phy_data);
		if (ret_val)
			return ret_val;

		phy_data |= M88E1000_EPSCR_TX_CLK_25;
		ret_val = em_write_phy_reg(hw, M88E1000_EXT_PHY_SPEC_CTRL,
		    phy_data);
		if (ret_val)
			return ret_val;
		/*
		 * In addition, because of the s/w reset above, we need to
		 * enable CRS on TX.  This must be set for both full and half
		 * duplex operation.
		 */
		ret_val = em_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL,
		    &phy_data);
		if (ret_val)
			return ret_val;

		if (hw->phy_id == M88E1141_E_PHY_ID)
			phy_data &= ~M88E1000_PSCR_ASSERT_CRS_ON_TX;
		else
			phy_data |= M88E1000_PSCR_ASSERT_CRS_ON_TX;

		ret_val = em_write_phy_reg(hw, M88E1000_PHY_SPEC_CTRL,
		    phy_data);
		if (ret_val)
			return ret_val;

		if ((hw->mac_type == em_82544 || hw->mac_type == em_82543) &&
		    (!hw->autoneg) && (hw->forced_speed_duplex == em_10_full ||
		    hw->forced_speed_duplex == em_10_half)) {
			ret_val = em_polarity_reversal_workaround(hw);
			if (ret_val)
				return ret_val;
		}
	} else if (hw->phy_type == em_phy_rtl8211) {
		/*
		* In addition, because of the s/w reset above, we need to enable
		* CRX on TX.  This must be set for both full and half duplex
		* operation.
		*/

		ret_val = em_read_phy_reg_ex(hw, RGEPHY_CR, &phy_data);
		if(ret_val) {
			printf("Unable to read RGEPHY_CR register\n");
			return ret_val;
		}

		phy_data &= ~RGEPHY_CR_ASSERT_CRS;
		ret_val = em_write_phy_reg_ex(hw, RGEPHY_CR, phy_data);
		if(ret_val) {
			printf("Unable to write RGEPHY_CR register\n");
			return ret_val;
		}
	} else if (hw->phy_type == em_phy_gg82563) {
		/*
		 * The TX_CLK of the Extended PHY Specific Control Register
		 * defaults to 2.5MHz on a reset.  We need to re-force it
		 * back to 25MHz, if we're not in a forced 10/duplex
		 * configuration.
		 */
		ret_val = em_read_phy_reg(hw, GG82563_PHY_MAC_SPEC_CTRL,
		    &phy_data);
		if (ret_val)
			return ret_val;

		phy_data &= ~GG82563_MSCR_TX_CLK_MASK;
		if ((hw->forced_speed_duplex == em_10_full) ||
		    (hw->forced_speed_duplex == em_10_half))
			phy_data |= GG82563_MSCR_TX_CLK_10MBPS_2_5MHZ;
		else
			phy_data |= GG82563_MSCR_TX_CLK_100MBPS_25MHZ;

		/* Also due to the reset, we need to enable CRS on Tx. */
		phy_data |= GG82563_MSCR_ASSERT_CRS_ON_TX;

		ret_val = em_write_phy_reg(hw, GG82563_PHY_MAC_SPEC_CTRL,
		    phy_data);
		if (ret_val)
			return ret_val;
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 * Sets the collision distance in the Transmit Control register
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Link should have been established previously. Reads the speed and duplex
 * information from the Device Status register.
 *****************************************************************************/
void
em_config_collision_dist(struct em_hw *hw)
{
	uint32_t tctl, coll_dist;
	DEBUGFUNC("em_config_collision_dist");

	if (hw->mac_type < em_82543)
		coll_dist = E1000_COLLISION_DISTANCE_82542;
	else
		coll_dist = E1000_COLLISION_DISTANCE;

	tctl = E1000_READ_REG(hw, TCTL);

	tctl &= ~E1000_TCTL_COLD;
	tctl |= coll_dist << E1000_COLD_SHIFT;

	E1000_WRITE_REG(hw, TCTL, tctl);
	E1000_WRITE_FLUSH(hw);
}

/******************************************************************************
 * Sets MAC speed and duplex settings to reflect the those in the PHY
 *
 * hw - Struct containing variables accessed by shared code
 * mii_reg - data to write to the MII control register
 *
 * The contents of the PHY register containing the needed information need to
 * be passed in.
 *****************************************************************************/
static int32_t
em_config_mac_to_phy(struct em_hw *hw)
{
	uint32_t ctrl;
	int32_t  ret_val;
	uint16_t phy_data;
	DEBUGFUNC("em_config_mac_to_phy");
	/*
	 * 82544 or newer MAC, Auto Speed Detection takes care of MAC
	 * speed/duplex configuration.
	 */
	if (hw->mac_type >= em_82544
	    && hw->mac_type != em_icp_xxxx)
		return E1000_SUCCESS;
	/*
	 * Read the Device Control Register and set the bits to Force Speed
	 * and Duplex.
	 */
	ctrl = E1000_READ_REG(hw, CTRL);
	ctrl |= (E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
	ctrl &= ~(E1000_CTRL_SPD_SEL | E1000_CTRL_ILOS);
	/*
	 * Set up duplex in the Device Control and Transmit Control registers
	 * depending on negotiated values.
	 */
	ret_val = em_read_phy_reg(hw, M88E1000_PHY_SPEC_STATUS, &phy_data);
	if (ret_val)
		return ret_val;

	if (phy_data & M88E1000_PSSR_DPLX)
		ctrl |= E1000_CTRL_FD;
	else
		ctrl &= ~E1000_CTRL_FD;

	em_config_collision_dist(hw);
	/*
	 * Set up speed in the Device Control register depending on
	 * negotiated values.
	 */
	if ((phy_data & M88E1000_PSSR_SPEED) == M88E1000_PSSR_1000MBS)
		ctrl |= E1000_CTRL_SPD_1000;
	else if ((phy_data & M88E1000_PSSR_SPEED) == M88E1000_PSSR_100MBS)
		ctrl |= E1000_CTRL_SPD_100;

	/* Write the configured values back to the Device Control Reg. */
	E1000_WRITE_REG(hw, CTRL, ctrl);
	return E1000_SUCCESS;
}

/******************************************************************************
 * Forces the MAC's flow control settings.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Sets the TFCE and RFCE bits in the device control register to reflect
 * the adapter settings. TFCE and RFCE need to be explicitly set by
 * software when a Copper PHY is used because autonegotiation is managed
 * by the PHY rather than the MAC. Software must also configure these
 * bits when link is forced on a fiber connection.
 *****************************************************************************/
int32_t
em_force_mac_fc(struct em_hw *hw)
{
	uint32_t ctrl;
	DEBUGFUNC("em_force_mac_fc");

	/* Get the current configuration of the Device Control Register */
	ctrl = E1000_READ_REG(hw, CTRL);
	/*
	 * Because we didn't get link via the internal auto-negotiation
	 * mechanism (we either forced link or we got link via PHY auto-neg),
	 * we have to manually enable/disable transmit an receive flow
	 * control.
	 *
	 * The "Case" statement below enables/disable flow control according to
	 * the "hw->fc" parameter.
	 *
	 * The possible values of the "fc" parameter are: 0:  Flow control is
	 * completely disabled 1:  Rx flow control is enabled (we can receive
	 * pause frames but not send pause frames). 2:  Tx flow control is
	 * enabled (we can send pause frames but we do not receive
	 * pause frames). 3:  Both Rx and TX flow control (symmetric) is
	 * enabled. other:  No other values should be possible at this point.
	 */

	switch (hw->fc) {
	case E1000_FC_NONE:
		ctrl &= (~(E1000_CTRL_TFCE | E1000_CTRL_RFCE));
		break;
	case E1000_FC_RX_PAUSE:
		ctrl &= (~E1000_CTRL_TFCE);
		ctrl |= E1000_CTRL_RFCE;
		break;
	case E1000_FC_TX_PAUSE:
		ctrl &= (~E1000_CTRL_RFCE);
		ctrl |= E1000_CTRL_TFCE;
		break;
	case E1000_FC_FULL:
		ctrl |= (E1000_CTRL_TFCE | E1000_CTRL_RFCE);
		break;
	default:
		DEBUGOUT("Flow control param set incorrectly\n");
		return -E1000_ERR_CONFIG;
	}

	/* Disable TX Flow Control for 82542 (rev 2.0) */
	if (hw->mac_type == em_82542_rev2_0)
		ctrl &= (~E1000_CTRL_TFCE);

	E1000_WRITE_REG(hw, CTRL, ctrl);
	return E1000_SUCCESS;
}
/******************************************************************************
 * Configures flow control settings after link is established
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Should be called immediately after a valid link has been established.
 * Forces MAC flow control settings if link was forced. When in MII/GMII mode
 * and autonegotiation is enabled, the MAC flow control settings will be set
 * based on the flow control negotiated by the PHY. In TBI mode, the TFCE
 * and RFCE bits will be automatically set to the negotiated flow control mode.
 *****************************************************************************/
STATIC int32_t
em_config_fc_after_link_up(struct em_hw *hw)
{
	int32_t  ret_val;
	uint16_t mii_status_reg;
	uint16_t mii_nway_adv_reg;
	uint16_t mii_nway_lp_ability_reg;
	uint16_t speed;
	uint16_t duplex;
	DEBUGFUNC("em_config_fc_after_link_up");
	/*
	 * Check for the case where we have fiber media and auto-neg failed
	 * so we had to force link.  In this case, we need to force the
	 * configuration of the MAC to match the "fc" parameter.
	 */
	if (((hw->media_type == em_media_type_fiber) && (hw->autoneg_failed))
	    || ((hw->media_type == em_media_type_internal_serdes) &&
	    (hw->autoneg_failed)) ||
	    ((hw->media_type == em_media_type_copper) && (!hw->autoneg)) ||
	    ((hw->media_type == em_media_type_oem) && (!hw->autoneg))) {
		ret_val = em_force_mac_fc(hw);
		if (ret_val) {
			DEBUGOUT("Error forcing flow control settings\n");
			return ret_val;
		}
	}
	/*
	 * Check for the case where we have copper media and auto-neg is
	 * enabled.  In this case, we need to check and see if Auto-Neg has
	 * completed, and if so, how the PHY and link partner has flow
	 * control configured.
	 */
	if ((hw->media_type == em_media_type_copper ||
	    (hw->media_type == em_media_type_oem)) &&
	    hw->autoneg) {
		/*
		 * Read the MII Status Register and check to see if AutoNeg
		 * has completed.  We read this twice because this reg has
		 * some "sticky" (latched) bits.
		 */
		ret_val = em_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
		if (ret_val)
			return ret_val;
		ret_val = em_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
		if (ret_val)
			return ret_val;

		if (mii_status_reg & MII_SR_AUTONEG_COMPLETE) {
			/*
			 * The AutoNeg process has completed, so we now need
			 * to read both the Auto Negotiation Advertisement
			 * Register (Address 4) and the Auto_Negotiation Base
			 * Page Ability Register (Address 5) to determine how
			 * flow control was negotiated.
			 */
			ret_val = em_read_phy_reg(hw, PHY_AUTONEG_ADV,
			    &mii_nway_adv_reg);
			if (ret_val)
				return ret_val;
			ret_val = em_read_phy_reg(hw, PHY_LP_ABILITY,
			    &mii_nway_lp_ability_reg);
			if (ret_val)
				return ret_val;
			/*
			 * Two bits in the Auto Negotiation Advertisement
			 * Register (Address 4) and two bits in the Auto
			 * Negotiation Base Page Ability Register (Address 5)
			 * determine flow control for both the PHY and the
			 * link partner.  The following table, taken out of
			 * the IEEE 802.3ab/D6.0 dated March 25, 1999,
			 * describes these PAUSE resolution bits and how flow
			 * control is determined based upon these settings.
			 * NOTE:  DC = Don't Care
			 *
			 *   LOCAL DEVICE   |   LINK PARTNER  |
			 *  PAUSE | ASM_DIR | PAUSE | ASM_DIR | NIC Resolution
			 * -------|---------|-------|---------|---------------
			 *    0   |    0    |  DC   |   DC    | em_fc_none
 			 *    0   |    1    |   0   |   DC    | em_fc_none
			 *    0   |    1    |   1   |    0    | em_fc_none
			 *    0   |    1    |   1   |    1    | em_fc_tx_pause
			 *    1   |    0    |   0   |   DC    | em_fc_none
			 *    1   |   DC    |   1   |   DC    | em_fc_full
			 *    1   |    1    |   0   |    0    | em_fc_none
			 *    1   |    1    |   0   |    1    | em_fc_rx_pause
			 *
			 */
			/*
			 * Are both PAUSE bits set to 1?  If so, this implies
			 * Symmetric Flow Control is enabled at both ends.
			 * The ASM_DIR bits are irrelevant per the spec.
			 *
			 * For Symmetric Flow Control:
			 *
			 *   LOCAL DEVICE  |   LINK PARTNER
			 * PAUSE | ASM_DIR | PAUSE | ASM_DIR | Result
			 * -------|---------|-------|---------|---------------
			 *    1   |   DC    |   1   |   DC    | em_fc_full
			 *
			 */
			if ((mii_nway_adv_reg & NWAY_AR_PAUSE) &&
			    (mii_nway_lp_ability_reg & NWAY_LPAR_PAUSE)) {
				/*
				 * Now we need to check if the user selected
				 * RX ONLY of pause frames.  In this case, we
				 * had to advertise FULL flow control because
				 * we could not advertise RX ONLY. Hence, we
				 * must now check to see if we need to turn
				 * OFF  the TRANSMISSION of PAUSE frames.
				 */
				if (hw->original_fc == E1000_FC_FULL) {
					hw->fc = E1000_FC_FULL;
					DEBUGOUT("Flow Control = FULL.\n");
				} else {
					hw->fc = E1000_FC_RX_PAUSE;
					DEBUGOUT("Flow Control = RX PAUSE"
					    " frames only.\n");
				}
			}
			/*
			 * For receiving PAUSE frames ONLY.
			 *
			 * LOCAL DEVICE  |   LINK PARTNER PAUSE | ASM_DIR |
			 * PAUSE | ASM_DIR | Result
			 * -------|---------|-------|---------|---------------
			 * ----- 0   |    1    |   1   |    1    |
			 * em_fc_tx_pause
			 *
			 */
			else if (!(mii_nway_adv_reg & NWAY_AR_PAUSE) &&
			    (mii_nway_adv_reg & NWAY_AR_ASM_DIR) &&
			    (mii_nway_lp_ability_reg & NWAY_LPAR_PAUSE) &&
			    (mii_nway_lp_ability_reg & NWAY_LPAR_ASM_DIR)) {
				hw->fc = E1000_FC_TX_PAUSE;
				DEBUGOUT("Flow Control = TX PAUSE frames only."
				    "\n");
			}
			/*
			 * For transmitting PAUSE frames ONLY.
			 *
			 *    LOCAL DEVICE  |   LINK PARTNER
			 *  PAUSE | ASM_DIR | PAUSE | ASM_DIR | Result
			 * -------|---------|-------|---------|---------------
			 *    1   |    1    |   0   |    1    | em_fc_rx_pause
			 *
			 */
			else if ((mii_nway_adv_reg & NWAY_AR_PAUSE) &&
			    (mii_nway_adv_reg & NWAY_AR_ASM_DIR) &&
			    !(mii_nway_lp_ability_reg & NWAY_LPAR_PAUSE) &&
			    (mii_nway_lp_ability_reg & NWAY_LPAR_ASM_DIR)) {
				hw->fc = E1000_FC_RX_PAUSE;
				DEBUGOUT("Flow Control = RX PAUSE frames only."
				    "\n");
			}
			/*
			 * Per the IEEE spec, at this point flow control
			 * should be disabled.  However, we want to consider
			 * that we could be connected to a legacy switch that
			 * doesn't advertise desired flow control, but can be
			 * forced on the link partner.  So if we advertised
			 * no flow control, that is what we will resolve to.
			 * If we advertised some kind of receive capability
			 * (Rx Pause Only or Full Flow Control) and the link
			 * partner advertised none, we will configure
			 * ourselves to enable Rx Flow Control only.  We can
			 * do this safely for two reasons:  If the link
			 * partner really didn't want flow control enabled,
			 * and we enable Rx, no harm done since we won't be
			 * receiving any PAUSE frames anyway.  If the intent
			 * on the link partner was to have flow control
			 * enabled, then by us enabling RX only, we can at
			 * least receive pause frames and process them. This
			 * is a good idea because in most cases, since we are
			 * predominantly a server NIC, more times than not we
			 * will be asked to delay transmission of packets
			 * than asking our link partner to pause transmission
			 * of frames.
			 */
			else if ((hw->original_fc == E1000_FC_NONE ||
			    hw->original_fc == E1000_FC_TX_PAUSE) ||
			    hw->fc_strict_ieee) {
				hw->fc = E1000_FC_NONE;
				DEBUGOUT("Flow Control = NONE.\n");
			} else {
				hw->fc = E1000_FC_RX_PAUSE;
				DEBUGOUT("Flow Control = RX PAUSE frames only."
				    "\n");
			}
			/*
			 * Now we need to do one last check...  If we auto-
			 * negotiated to HALF DUPLEX, flow control should not
			 * be enabled per IEEE 802.3 spec.
			 */
			ret_val = em_get_speed_and_duplex(hw, &speed, &duplex);
			if (ret_val) {
				DEBUGOUT("Error getting link speed and duplex"
				    "\n");
				return ret_val;
			}
			if (duplex == HALF_DUPLEX)
				hw->fc = E1000_FC_NONE;
			/*
			 * Now we call a subroutine to actually force the MAC
			 * controller to use the correct flow control
			 * settings.
			 */
			ret_val = em_force_mac_fc(hw);
			if (ret_val) {
				DEBUGOUT("Error forcing flow control settings"
				    "\n");
				return ret_val;
			}
		} else {
			DEBUGOUT("Copper PHY and Auto Neg has not completed."
			    "\n");
		}
	}
	return E1000_SUCCESS;
}
/******************************************************************************
 * Checks to see if the link status of the hardware has changed.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Called by any function that needs to check the link status of the adapter.
 *****************************************************************************/
int32_t
em_check_for_link(struct em_hw *hw)
{
	uint32_t rxcw = 0;
	uint32_t ctrl;
	uint32_t status;
	uint32_t rctl;
	uint32_t icr;
	uint32_t signal = 0;
	int32_t  ret_val;
	uint16_t phy_data;
	DEBUGFUNC("em_check_for_link");
	uint16_t speed, duplex;

	if (hw->mac_type >= em_82575 &&
	    hw->media_type != em_media_type_copper) {
		ret_val = em_get_pcs_speed_and_duplex_82575(hw, &speed,
		    &duplex);
		hw->get_link_status = hw->serdes_link_down;

		return (ret_val);
	}

	ctrl = E1000_READ_REG(hw, CTRL);
	status = E1000_READ_REG(hw, STATUS);
	/*
	 * On adapters with a MAC newer than 82544, SW Definable pin 1 will
	 * be set when the optics detect a signal. On older adapters, it will
	 * be cleared when there is a signal.  This applies to fiber media
	 * only.
	 */
	if ((hw->media_type == em_media_type_fiber) ||
	    (hw->media_type == em_media_type_internal_serdes)) {
		rxcw = E1000_READ_REG(hw, RXCW);

		if (hw->media_type == em_media_type_fiber) {
			signal = (hw->mac_type > em_82544) ? 
			    E1000_CTRL_SWDPIN1 : 0;
			if (status & E1000_STATUS_LU)
				hw->get_link_status = FALSE;
		}
	}
	/*
	 * If we have a copper PHY then we only want to go out to the PHY
	 * registers to see if Auto-Neg has completed and/or if our link
	 * status has changed.  The get_link_status flag will be set if we
	 * receive a Link Status Change interrupt or we have Rx Sequence
	 * Errors.
	 */
	if ((hw->media_type == em_media_type_copper ||
	    (hw->media_type == em_media_type_oem)) &&
	    hw->get_link_status) {
		/*
		 * First we want to see if the MII Status Register reports
		 * link.  If so, then we want to get the current speed/duplex
		 * of the PHY. Read the register twice since the link bit is
		 * sticky.
		 */
		ret_val = em_read_phy_reg(hw, PHY_STATUS, &phy_data);
		if (ret_val)
			return ret_val;
		ret_val = em_read_phy_reg(hw, PHY_STATUS, &phy_data);
		if (ret_val)
			return ret_val;

		hw->icp_xxxx_is_link_up = (phy_data & MII_SR_LINK_STATUS) != 0;

		if (hw->mac_type == em_pchlan) {
			ret_val = em_k1_gig_workaround_hv(hw,
			    hw->icp_xxxx_is_link_up);
			if (ret_val)
				return ret_val;
		}

		if (phy_data & MII_SR_LINK_STATUS) {
			hw->get_link_status = FALSE;

			if (hw->phy_type == em_phy_82578) {
				ret_val = em_link_stall_workaround_hv(hw);
				if (ret_val)
					return ret_val;
			}

			if (hw->mac_type == em_pch2lan) {
				ret_val = em_k1_workaround_lv(hw);
				if (ret_val)
					return ret_val;
			}
			/* Work-around I218 hang issue */
			if ((hw->device_id == E1000_DEV_ID_PCH_LPTLP_I218_LM) ||
			    (hw->device_id == E1000_DEV_ID_PCH_LPTLP_I218_V) ||
			    (hw->device_id == E1000_DEV_ID_PCH_I218_LM3) ||
			    (hw->device_id == E1000_DEV_ID_PCH_I218_V3)) {
				ret_val = em_k1_workaround_lpt_lp(hw,
				    hw->icp_xxxx_is_link_up);
				if (ret_val)
					return ret_val;
			}

			/*
			 * Check if there was DownShift, must be checked
			 * immediately after link-up
			 */
			em_check_downshift(hw);

			/* Enable/Disable EEE after link up */
			if (hw->mac_type == em_pch2lan ||
			    hw->mac_type == em_pch_lpt ||
			    hw->mac_type == em_pch_spt ||
			    hw->mac_type == em_pch_cnp ||
			    hw->mac_type == em_pch_tgp ||
			    hw->mac_type == em_pch_adp) {
				ret_val = em_set_eee_pchlan(hw);
				if (ret_val)
					return ret_val;
			}

			/*
			 * If we are on 82544 or 82543 silicon and
			 * speed/duplex are forced to 10H or 10F, then we
			 * will implement the polarity reversal workaround.
			 * We disable interrupts first, and upon returning,
			 * place the devices interrupt state to its previous
			 * value except for the link status change interrupt
			 * which will happen due to the execution of this
			 * workaround.
			 */
			if ((hw->mac_type == em_82544 ||
			    hw->mac_type == em_82543) && (!hw->autoneg) &&
			    (hw->forced_speed_duplex == em_10_full ||
			    hw->forced_speed_duplex == em_10_half)) {
				E1000_WRITE_REG(hw, IMC, 0xffffffff);
				ret_val = em_polarity_reversal_workaround(hw);
				icr = E1000_READ_REG(hw, ICR);
				E1000_WRITE_REG(hw, ICS, 
				    (icr & ~E1000_ICS_LSC));
				E1000_WRITE_REG(hw, IMS, IMS_ENABLE_MASK);
			}
		} else {
			/* No link detected */
			em_config_dsp_after_link_change(hw, FALSE);
			return 0;
		}
		/*
		 * If we are forcing speed/duplex, then we simply return
		 * since we have already determined whether we have link or
		 * not.
		 */
		if (!hw->autoneg)
			return -E1000_ERR_CONFIG;

		/* optimize the dsp settings for the igp phy */
		em_config_dsp_after_link_change(hw, TRUE);
		/*
		 * We have a M88E1000 PHY and Auto-Neg is enabled.  If we
		 * have Si on board that is 82544 or newer, Auto Speed
		 * Detection takes care of MAC speed/duplex configuration.
		 * So we only need to configure Collision Distance in the
		 * MAC.  Otherwise, we need to force speed/duplex on the MAC
		 * to the current PHY speed/duplex settings.
		 */
		if (hw->mac_type >= em_82544 && hw->mac_type != em_icp_xxxx) {
			em_config_collision_dist(hw);
		} else {
			ret_val = em_config_mac_to_phy(hw);
			if (ret_val) {
				DEBUGOUT("Error configuring MAC to PHY"
				    " settings\n");
				return ret_val;
			}
		}
		/*
		 * Configure Flow Control now that Auto-Neg has completed.
		 * First, we need to restore the desired flow control
		 * settings because we may have had to re-autoneg with a
		 * different link partner.
		 */
		ret_val = em_config_fc_after_link_up(hw);
		if (ret_val) {
			DEBUGOUT("Error configuring flow control\n");
			return ret_val;
		}
		/*
		 * At this point we know that we are on copper and we have
		 * auto-negotiated link.  These are conditions for checking
		 * the link partner capability register.  We use the link
		 * speed to determine if TBI compatibility needs to be turned
		 * on or off.  If the link is not at gigabit speed, then TBI
		 * compatibility is not needed.  If we are at gigabit speed,
		 * we turn on TBI compatibility.
		 */
		if (hw->tbi_compatibility_en) {
			uint16_t speed, duplex;
			ret_val = em_get_speed_and_duplex(hw, &speed, &duplex);
			if (ret_val) {
				DEBUGOUT("Error getting link speed and duplex"
				    "\n");
				return ret_val;
			}
			if (speed != SPEED_1000) {
				/*
				 * If link speed is not set to gigabit speed,
				 * we do not need to enable TBI
				 * compatibility.
				 */
				if (hw->tbi_compatibility_on) {
					/*
					 * If we previously were in the mode,
					 * turn it off.
					 */
					rctl = E1000_READ_REG(hw, RCTL);
					rctl &= ~E1000_RCTL_SBP;
					E1000_WRITE_REG(hw, RCTL, rctl);
					hw->tbi_compatibility_on = FALSE;
				}
			} else {
				/*
				 * If TBI compatibility is was previously
				 * off, turn it on. For compatibility with a
				 * TBI link partner, we will store bad
				 * packets. Some frames have an additional
				 * byte on the end and will look like CRC
				 * errors to the hardware.
				 */
				if (!hw->tbi_compatibility_on) {
					hw->tbi_compatibility_on = TRUE;
					rctl = E1000_READ_REG(hw, RCTL);
					rctl |= E1000_RCTL_SBP;
					E1000_WRITE_REG(hw, RCTL, rctl);
				}
			}
		}
	}
	/*
	 * If we don't have link (auto-negotiation failed or link partner
	 * cannot auto-negotiate), the cable is plugged in (we have signal),
	 * and our link partner is not trying to auto-negotiate with us (we
	 * are receiving idles or data), we need to force link up. We also
	 * need to give auto-negotiation time to complete, in case the cable
	 * was just plugged in. The autoneg_failed flag does this.
	 */
	else if ((((hw->media_type == em_media_type_fiber) &&
	    ((ctrl & E1000_CTRL_SWDPIN1) == signal)) ||
	    (hw->media_type == em_media_type_internal_serdes)) &&
	    (!(status & E1000_STATUS_LU)) && (!(rxcw & E1000_RXCW_C))) {
		if (hw->autoneg_failed == 0) {
			hw->autoneg_failed = 1;
			return 0;
		}
		DEBUGOUT("NOT RXing /C/, disable AutoNeg and force link.\n");

		/* Disable auto-negotiation in the TXCW register */
		E1000_WRITE_REG(hw, TXCW, (hw->txcw & ~E1000_TXCW_ANE));

		/* Force link-up and also force full-duplex. */
		ctrl = E1000_READ_REG(hw, CTRL);
		ctrl |= (E1000_CTRL_SLU | E1000_CTRL_FD);
		E1000_WRITE_REG(hw, CTRL, ctrl);

		/* Configure Flow Control after forcing link up. */
		ret_val = em_config_fc_after_link_up(hw);
		if (ret_val) {
			DEBUGOUT("Error configuring flow control\n");
			return ret_val;
		}
	}
	/*
	 * If we are forcing link and we are receiving /C/ ordered sets,
	 * re-enable auto-negotiation in the TXCW register and disable forced
	 * link in the Device Control register in an attempt to
	 * auto-negotiate with our link partner.
	 */
	else if (((hw->media_type == em_media_type_fiber) ||
	    (hw->media_type == em_media_type_internal_serdes)) &&
	    (ctrl & E1000_CTRL_SLU) && (rxcw & E1000_RXCW_C)) {
		DEBUGOUT("RXing /C/, enable AutoNeg and stop forcing link.\n");
		E1000_WRITE_REG(hw, TXCW, hw->txcw);
		E1000_WRITE_REG(hw, CTRL, (ctrl & ~E1000_CTRL_SLU));

		hw->serdes_link_down = FALSE;
	}
	/*
	 * If we force link for non-auto-negotiation switch, check link
	 * status based on MAC synchronization for internal serdes media
	 * type.
	 */
	else if ((hw->media_type == em_media_type_internal_serdes) &&
	    !(E1000_TXCW_ANE & E1000_READ_REG(hw, TXCW))) {
		/* SYNCH bit and IV bit are sticky. */
		usec_delay(10);
		if (E1000_RXCW_SYNCH & E1000_READ_REG(hw, RXCW)) {
			if (!(rxcw & E1000_RXCW_IV)) {
				hw->serdes_link_down = FALSE;
				DEBUGOUT("SERDES: Link is up.\n");
			}
		} else {
			hw->serdes_link_down = TRUE;
			DEBUGOUT("SERDES: Link is down.\n");
		}
	}
	if ((hw->media_type == em_media_type_internal_serdes) &&
	    (E1000_TXCW_ANE & E1000_READ_REG(hw, TXCW))) {
		hw->serdes_link_down = !(E1000_STATUS_LU & 
		    E1000_READ_REG(hw, STATUS));
	}
	return E1000_SUCCESS;
}

int32_t
em_get_pcs_speed_and_duplex_82575(struct em_hw *hw, uint16_t *speed,
    uint16_t *duplex)
{
	uint32_t pcs;

	hw->serdes_link_down = TRUE;
	*speed = 0;
	*duplex = 0;

	/*
	 * Read the PCS Status register for link state. For non-copper mode,
	 * the status register is not accurate. The PCS status register is
	 * used instead.
	 */
	pcs = E1000_READ_REG(hw, PCS_LSTAT);

	/*
	 * The link up bit determines when link is up on autoneg. The sync ok
	 * gets set once both sides sync up and agree upon link. Stable link
	 * can be determined by checking for both link up and link sync ok
	 */
	if ((pcs & E1000_PCS_LSTS_LINK_OK) && (pcs & E1000_PCS_LSTS_SYNK_OK)) {
		hw->serdes_link_down = FALSE;
	
		/* Detect and store PCS speed */
		if (pcs & E1000_PCS_LSTS_SPEED_1000) {
			*speed = SPEED_1000;
		} else if (pcs & E1000_PCS_LSTS_SPEED_100) {
			*speed = SPEED_100;
		} else {
			*speed = SPEED_10;
		}

		/* Detect and store PCS duplex */
		if (pcs & E1000_PCS_LSTS_DUPLEX_FULL) {
			*duplex = FULL_DUPLEX;
		} else {
			*duplex = HALF_DUPLEX;
		}
	}

	return (0);
}


/******************************************************************************
 * Detects the current speed and duplex settings of the hardware.
 *
 * hw - Struct containing variables accessed by shared code
 * speed - Speed of the connection
 * duplex - Duplex setting of the connection
 *****************************************************************************/
int32_t
em_get_speed_and_duplex(struct em_hw *hw, uint16_t *speed, uint16_t *duplex)
{
	uint32_t status;
	int32_t  ret_val;
	uint16_t phy_data;
	DEBUGFUNC("em_get_speed_and_duplex");

	if (hw->mac_type >= em_82575 && hw->media_type != em_media_type_copper)
		return em_get_pcs_speed_and_duplex_82575(hw, speed, duplex);

	if (hw->mac_type >= em_82543) {
		status = E1000_READ_REG(hw, STATUS);
		if (status & E1000_STATUS_SPEED_1000) {
			*speed = SPEED_1000;
			DEBUGOUT("1000 Mbs, ");
		} else if (status & E1000_STATUS_SPEED_100) {
			*speed = SPEED_100;
			DEBUGOUT("100 Mbs, ");
		} else {
			*speed = SPEED_10;
			DEBUGOUT("10 Mbs, ");
		}

		if (status & E1000_STATUS_FD) {
			*duplex = FULL_DUPLEX;
			DEBUGOUT("Full Duplex\n");
		} else {
			*duplex = HALF_DUPLEX;
			DEBUGOUT(" Half Duplex\n");
		}
	} else {
		DEBUGOUT("1000 Mbs, Full Duplex\n");
		*speed = SPEED_1000;
		*duplex = FULL_DUPLEX;
	}
	/*
	 * IGP01 PHY may advertise full duplex operation after speed
	 * downgrade even if it is operating at half duplex.  Here we set the
	 * duplex settings to match the duplex in the link partner's
	 * capabilities.
	 */
	if (hw->phy_type == em_phy_igp && hw->speed_downgraded) {
		ret_val = em_read_phy_reg(hw, PHY_AUTONEG_EXP, &phy_data);
		if (ret_val)
			return ret_val;

		if (!(phy_data & NWAY_ER_LP_NWAY_CAPS))
			*duplex = HALF_DUPLEX;
		else {
			ret_val = em_read_phy_reg(hw, PHY_LP_ABILITY,
			    &phy_data);
			if (ret_val)
				return ret_val;
			if ((*speed == SPEED_100 && 
			    !(phy_data & NWAY_LPAR_100TX_FD_CAPS)) ||
			    (*speed == SPEED_10 && 
			    !(phy_data & NWAY_LPAR_10T_FD_CAPS)))
				*duplex = HALF_DUPLEX;
		}
	}
	if ((hw->mac_type == em_80003es2lan) &&
	    (hw->media_type == em_media_type_copper)) {
		if (*speed == SPEED_1000)
			ret_val = em_configure_kmrn_for_1000(hw);
		else
			ret_val = em_configure_kmrn_for_10_100(hw, *duplex);
		if (ret_val)
			return ret_val;
	}
	if ((hw->mac_type == em_ich8lan) &&
	    (hw->phy_type == em_phy_igp_3) &&
	    (*speed == SPEED_1000)) {
		ret_val = em_kumeran_lock_loss_workaround(hw);
		if (ret_val)
			return ret_val;
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 * Blocks until autoneg completes or times out (~4.5 seconds)
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
STATIC int32_t
em_wait_autoneg(struct em_hw *hw)
{
	int32_t  ret_val;
	uint16_t i;
	uint16_t phy_data;
	DEBUGFUNC("em_wait_autoneg");
	DEBUGOUT("Waiting for Auto-Neg to complete.\n");

	/* We will wait for autoneg to complete or 4.5 seconds to expire. */
	for (i = PHY_AUTO_NEG_TIME; i > 0; i--) {
		/*
		 * Read the MII Status Register and wait for Auto-Neg
		 * Complete bit to be set.
		 */
		ret_val = em_read_phy_reg(hw, PHY_STATUS, &phy_data);
		if (ret_val)
			return ret_val;
		ret_val = em_read_phy_reg(hw, PHY_STATUS, &phy_data);
		if (ret_val)
			return ret_val;
		if (phy_data & MII_SR_AUTONEG_COMPLETE) {
			return E1000_SUCCESS;
		}
		msec_delay(100);
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 * Raises the Management Data Clock
 *
 * hw - Struct containing variables accessed by shared code
 * ctrl - Device control register's current value
 *****************************************************************************/
static void
em_raise_mdi_clk(struct em_hw *hw, uint32_t *ctrl)
{
	/*
	 * Raise the clock input to the Management Data Clock (by setting the
	 * MDC bit), and then delay 10 microseconds.
	 */
	E1000_WRITE_REG(hw, CTRL, (*ctrl | E1000_CTRL_MDC));
	E1000_WRITE_FLUSH(hw);
	usec_delay(10);
}

/******************************************************************************
 * Lowers the Management Data Clock
 *
 * hw - Struct containing variables accessed by shared code
 * ctrl - Device control register's current value
 *****************************************************************************/
static void
em_lower_mdi_clk(struct em_hw *hw, uint32_t *ctrl)
{
	/*
	 * Lower the clock input to the Management Data Clock (by clearing
	 * the MDC bit), and then delay 10 microseconds.
	 */
	E1000_WRITE_REG(hw, CTRL, (*ctrl & ~E1000_CTRL_MDC));
	E1000_WRITE_FLUSH(hw);
	usec_delay(10);
}

/******************************************************************************
 * Shifts data bits out to the PHY
 *
 * hw - Struct containing variables accessed by shared code
 * data - Data to send out to the PHY
 * count - Number of bits to shift out
 *
 * Bits are shifted out in MSB to LSB order.
 *****************************************************************************/
static void
em_shift_out_mdi_bits(struct em_hw *hw, uint32_t data, uint16_t count)
{
	uint32_t ctrl;
	uint32_t mask;
	/*
	 * We need to shift "count" number of bits out to the PHY. So, the
	 * value in the "data" parameter will be shifted out to the PHY one
	 * bit at a time. In order to do this, "data" must be broken down
	 * into bits.
	 */
	mask = 0x01;
	mask <<= (count - 1);

	ctrl = E1000_READ_REG(hw, CTRL);

	/* Set MDIO_DIR and MDC_DIR direction bits to be used as output 
	 * pins. 
	 */
	ctrl |= (E1000_CTRL_MDIO_DIR | E1000_CTRL_MDC_DIR);

	while (mask) {
		/*
		 * A "1" is shifted out to the PHY by setting the MDIO bit to
		 * "1" and then raising and lowering the Management Data
		 * Clock. A "0" is shifted out to the PHY by setting the MDIO
		 * bit to "0" and then raising and lowering the clock.
		 */
		if (data & mask)
			ctrl |= E1000_CTRL_MDIO;
		else
			ctrl &= ~E1000_CTRL_MDIO;

		E1000_WRITE_REG(hw, CTRL, ctrl);
		E1000_WRITE_FLUSH(hw);

		usec_delay(10);

		em_raise_mdi_clk(hw, &ctrl);
		em_lower_mdi_clk(hw, &ctrl);

		mask = mask >> 1;
	}
}

/******************************************************************************
 * Shifts data bits in from the PHY
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Bits are shifted in in MSB to LSB order.
 *****************************************************************************/
static uint16_t
em_shift_in_mdi_bits(struct em_hw *hw)
{
	uint32_t ctrl;
	uint16_t data = 0;
	uint8_t  i;
	/*
	 * In order to read a register from the PHY, we need to shift in a
	 * total of 18 bits from the PHY. The first two bit (turnaround)
	 * times are used to avoid contention on the MDIO pin when a read
	 * operation is performed. These two bits are ignored by us and
	 * thrown away. Bits are "shifted in" by raising the input to the
	 * Management Data Clock (setting the MDC bit), and then reading the
	 * value of the MDIO bit.
	 */
	ctrl = E1000_READ_REG(hw, CTRL);
	/*
	 * Clear MDIO_DIR (SWDPIO1) to indicate this bit is to be used as
	 * input.
	 */
	ctrl &= ~E1000_CTRL_MDIO_DIR;
	ctrl &= ~E1000_CTRL_MDIO;

	E1000_WRITE_REG(hw, CTRL, ctrl);
	E1000_WRITE_FLUSH(hw);
	/*
	 * Raise and Lower the clock before reading in the data. This
	 * accounts for the turnaround bits. The first clock occurred when we
	 * clocked out the last bit of the Register Address.
	 */
	em_raise_mdi_clk(hw, &ctrl);
	em_lower_mdi_clk(hw, &ctrl);

	for (data = 0, i = 0; i < 16; i++) {
		data = data << 1;
		em_raise_mdi_clk(hw, &ctrl);
		ctrl = E1000_READ_REG(hw, CTRL);
		/* Check to see if we shifted in a "1". */
		if (ctrl & E1000_CTRL_MDIO)
			data |= 1;
		em_lower_mdi_clk(hw, &ctrl);
	}

	em_raise_mdi_clk(hw, &ctrl);
	em_lower_mdi_clk(hw, &ctrl);

	return data;
}

STATIC int32_t
em_swfw_sync_acquire(struct em_hw *hw, uint16_t mask)
{
	uint32_t swfw_sync = 0;
	uint32_t swmask = mask;
	uint32_t fwmask = mask << 16;
	int32_t  timeout = 200;
	DEBUGFUNC("em_swfw_sync_acquire");

	if (hw->swfwhw_semaphore_present)
		return em_get_software_flag(hw);

	if (!hw->swfw_sync_present)
		return em_get_hw_eeprom_semaphore(hw);

	while (timeout) {
		if (em_get_hw_eeprom_semaphore(hw))
			return -E1000_ERR_SWFW_SYNC;

		swfw_sync = E1000_READ_REG(hw, SW_FW_SYNC);
		if (!(swfw_sync & (fwmask | swmask))) {
			break;
		}
		/* 
		 * firmware currently using resource (fwmask)
		 * or other software thread currently using resource (swmask)
		 */
		em_put_hw_eeprom_semaphore(hw);
		msec_delay_irq(5);
		timeout--;
	}

	if (!timeout) {
		DEBUGOUT("Driver can't access resource, SW_FW_SYNC timeout."
		    "\n");
		return -E1000_ERR_SWFW_SYNC;
	}
	swfw_sync |= swmask;
	E1000_WRITE_REG(hw, SW_FW_SYNC, swfw_sync);

	em_put_hw_eeprom_semaphore(hw);
	return E1000_SUCCESS;
}

STATIC void
em_swfw_sync_release(struct em_hw *hw, uint16_t mask)
{
	uint32_t swfw_sync;
	uint32_t swmask = mask;
	DEBUGFUNC("em_swfw_sync_release");

	if (hw->swfwhw_semaphore_present) {
		em_release_software_flag(hw);
		return;
	}
	if (!hw->swfw_sync_present) {
		em_put_hw_eeprom_semaphore(hw);
		return;
	}
	/*
	 * if (em_get_hw_eeprom_semaphore(hw)) return -E1000_ERR_SWFW_SYNC;
	 */
	while (em_get_hw_eeprom_semaphore(hw) != E1000_SUCCESS);
	/* empty */

	swfw_sync = E1000_READ_REG(hw, SW_FW_SYNC);
	swfw_sync &= ~swmask;
	E1000_WRITE_REG(hw, SW_FW_SYNC, swfw_sync);

	em_put_hw_eeprom_semaphore(hw);
}

/****************************************************************************
 *  Read BM PHY wakeup register.  It works as such:
 *  1) Set page 769, register 17, bit 2 = 1
 *  2) Set page to 800 for host (801 if we were manageability)
 *  3) Write the address using the address opcode (0x11)
 *  4) Read or write the data using the data opcode (0x12)
 *  5) Restore 769_17.2 to its original value
 ****************************************************************************/
int32_t
em_access_phy_wakeup_reg_bm(struct em_hw *hw, uint32_t reg_addr,
    uint16_t *phy_data, boolean_t read)
{
	int32_t ret_val;
	uint16_t reg = BM_PHY_REG_NUM(reg_addr);
	uint16_t phy_reg = 0;

	/* All operations in this function are phy address 1 */
	hw->phy_addr = 1;

	/* Set page 769 */
	em_write_phy_reg_ex(hw, IGP01E1000_PHY_PAGE_SELECT,
	    (BM_WUC_ENABLE_PAGE << PHY_PAGE_SHIFT));

	ret_val = em_read_phy_reg_ex(hw, BM_WUC_ENABLE_REG, &phy_reg);
	if (ret_val)
		goto out;

	/* First clear bit 4 to avoid a power state change */
	phy_reg &= ~(BM_WUC_HOST_WU_BIT);
	ret_val = em_write_phy_reg_ex(hw, BM_WUC_ENABLE_REG, phy_reg);
	if (ret_val)
		goto out;

	/* Write bit 2 = 1, and clear bit 4 to 769_17 */
	ret_val = em_write_phy_reg_ex(hw, BM_WUC_ENABLE_REG,
	    phy_reg | BM_WUC_ENABLE_BIT);
	if (ret_val)
		goto out;

	/* Select page 800 */
	ret_val = em_write_phy_reg_ex(hw, IGP01E1000_PHY_PAGE_SELECT,
	    (BM_WUC_PAGE << PHY_PAGE_SHIFT));

	/* Write the page 800 offset value using opcode 0x11 */
	ret_val = em_write_phy_reg_ex(hw, BM_WUC_ADDRESS_OPCODE, reg);
	if (ret_val)
		goto out;

	if (read)
	        /* Read the page 800 value using opcode 0x12 */
		ret_val = em_read_phy_reg_ex(hw, BM_WUC_DATA_OPCODE,
		    phy_data);
	else
	        /* Write the page 800 value using opcode 0x12 */
		ret_val = em_write_phy_reg_ex(hw, BM_WUC_DATA_OPCODE,
		    *phy_data);

	if (ret_val)
		goto out;

	/*
	 * Restore 769_17.2 to its original value
	 * Set page 769
	 */
	em_write_phy_reg_ex(hw, IGP01E1000_PHY_PAGE_SELECT,
	    (BM_WUC_ENABLE_PAGE << PHY_PAGE_SHIFT));

	/* Clear 769_17.2 */
	ret_val = em_write_phy_reg_ex(hw, BM_WUC_ENABLE_REG, phy_reg);
	if (ret_val)
		goto out;

out:
	return ret_val;
}

/***************************************************************************
 *  Read HV PHY vendor specific high registers
 ***************************************************************************/
int32_t
em_access_phy_debug_regs_hv(struct em_hw *hw, uint32_t reg_addr,
    uint16_t *phy_data, boolean_t read)
{
	int32_t ret_val;
	uint32_t addr_reg = 0;
	uint32_t data_reg = 0;

	/* This takes care of the difference with desktop vs mobile phy */
	addr_reg = (hw->phy_type == em_phy_82578) ?
	           I82578_PHY_ADDR_REG : I82577_PHY_ADDR_REG;
	data_reg = addr_reg + 1;

	/* All operations in this function are phy address 2 */
	hw->phy_addr = 2;

	/* masking with 0x3F to remove the page from offset */
	ret_val = em_write_phy_reg_ex(hw, addr_reg, (uint16_t)reg_addr & 0x3F);
	if (ret_val) {
		printf("Could not write PHY the HV address register\n");
		goto out;
	}

	/* Read or write the data value next */
	if (read)
		ret_val = em_read_phy_reg_ex(hw, data_reg, phy_data);
	else
		ret_val = em_write_phy_reg_ex(hw, data_reg, *phy_data);

	if (ret_val) {
		printf("Could not read data value from HV data register\n");
		goto out;
	}

out:
	return ret_val;
}

/******************************************************************************
 * Reads or writes the value from a PHY register, if the value is on a specific
 * non zero page, sets the page first.
 * hw - Struct containing variables accessed by shared code
 * reg_addr - address of the PHY register to read
 *****************************************************************************/
int32_t
em_access_phy_reg_hv(struct em_hw *hw, uint32_t reg_addr, uint16_t *phy_data,
    boolean_t read)
{
	uint32_t ret_val;
	uint16_t swfw;
	uint16_t page = BM_PHY_REG_PAGE(reg_addr);
	uint16_t reg = BM_PHY_REG_NUM(reg_addr);

	DEBUGFUNC("em_access_phy_reg_hv");

	swfw = E1000_SWFW_PHY0_SM;

	if (em_swfw_sync_acquire(hw, swfw))
		return -E1000_ERR_SWFW_SYNC;

	if (page == BM_WUC_PAGE) {
		ret_val = em_access_phy_wakeup_reg_bm(hw, reg_addr,
		    phy_data, read);
		goto release;
	}

	if (page >= HV_INTC_FC_PAGE_START)
		hw->phy_addr = 1;
	else
		hw->phy_addr = 2;

	if (page == HV_INTC_FC_PAGE_START)
		page = 0;

	/*
	 * Workaround MDIO accesses being disabled after entering IEEE Power
	 * Down (whenever bit 11 of the PHY Control register is set)
	 */
	if (!read &&
	    (hw->phy_type == em_phy_82578) &&
	    (hw->phy_revision >= 1) &&
	    (hw->phy_addr == 2) &&
	    ((MAX_PHY_REG_ADDRESS & reg) == 0) &&
	    (*phy_data & (1 << 11))) {
		uint16_t data2 = 0x7EFF;

		ret_val = em_access_phy_debug_regs_hv(hw, (1 << 6) | 0x3,
		    &data2, FALSE);
		if (ret_val)
			return ret_val;
	}

	if (reg_addr > MAX_PHY_MULTI_PAGE_REG) {
		ret_val = em_write_phy_reg_ex(hw, IGP01E1000_PHY_PAGE_SELECT,
		    (page << PHY_PAGE_SHIFT));
		if (ret_val)
			return ret_val;
	}
	if (read)
		ret_val = em_read_phy_reg_ex(hw, MAX_PHY_REG_ADDRESS & reg,
		    phy_data);
	else
		ret_val = em_write_phy_reg_ex(hw, MAX_PHY_REG_ADDRESS & reg,
		    *phy_data);
release:
	em_swfw_sync_release(hw, swfw);
	return ret_val;
}

/******************************************************************************
 * Reads the value from a PHY register, if the value is on a specific non zero
 * page, sets the page first.
 * hw - Struct containing variables accessed by shared code
 * reg_addr - address of the PHY register to read
 *****************************************************************************/
int32_t
em_read_phy_reg(struct em_hw *hw, uint32_t reg_addr, uint16_t *phy_data)
{
	uint32_t ret_val;
	uint16_t swfw;
	DEBUGFUNC("em_read_phy_reg");

	if (hw->mac_type == em_pchlan ||
		hw->mac_type == em_pch2lan ||
		hw->mac_type == em_pch_lpt ||
		hw->mac_type == em_pch_spt ||
		hw->mac_type == em_pch_cnp ||
		hw->mac_type == em_pch_tgp ||
		hw->mac_type == em_pch_adp)
		return (em_access_phy_reg_hv(hw, reg_addr, phy_data, TRUE));

	if (((hw->mac_type == em_80003es2lan) || (hw->mac_type == em_82575) ||
	    (hw->mac_type == em_82576)) &&
	    (E1000_READ_REG(hw, STATUS) & E1000_STATUS_FUNC_1)) {
		swfw = E1000_SWFW_PHY1_SM;
	} else {
		swfw = E1000_SWFW_PHY0_SM;
	}
	if (em_swfw_sync_acquire(hw, swfw))
		return -E1000_ERR_SWFW_SYNC;

	if ((hw->phy_type == em_phy_igp ||
	    hw->phy_type == em_phy_igp_3 ||
	    hw->phy_type == em_phy_igp_2) &&
	    (reg_addr > MAX_PHY_MULTI_PAGE_REG)) {
		ret_val = em_write_phy_reg_ex(hw, IGP01E1000_PHY_PAGE_SELECT,
		    (uint16_t) reg_addr);
		if (ret_val) {
			em_swfw_sync_release(hw, swfw);
			return ret_val;
		}
	} else if (hw->phy_type == em_phy_gg82563) {
		if (((reg_addr & MAX_PHY_REG_ADDRESS) > MAX_PHY_MULTI_PAGE_REG) ||
		    (hw->mac_type == em_80003es2lan)) {
			/* Select Configuration Page */
			if ((reg_addr & MAX_PHY_REG_ADDRESS) <
			    GG82563_MIN_ALT_REG) {
				ret_val = em_write_phy_reg_ex(hw,
				    GG82563_PHY_PAGE_SELECT, 
				    (uint16_t) ((uint16_t) reg_addr >> 
				    GG82563_PAGE_SHIFT));
			} else {
				/*
				 * Use Alternative Page Select register to
				 * access registers 30 and 31
				 */
				ret_val = em_write_phy_reg_ex(hw,
				    GG82563_PHY_PAGE_SELECT_ALT,
				    (uint16_t) ((uint16_t) reg_addr >> 
				    GG82563_PAGE_SHIFT));
			}

			if (ret_val) {
				em_swfw_sync_release(hw, swfw);
				return ret_val;
			}
		}
	} else if ((hw->phy_type == em_phy_bm) && (hw->phy_revision == 1)) {
		if (reg_addr > MAX_PHY_MULTI_PAGE_REG) {
			ret_val = em_write_phy_reg_ex(hw, BM_PHY_PAGE_SELECT,
			    (uint16_t) ((uint16_t) reg_addr >> 
			    PHY_PAGE_SHIFT));
			if (ret_val)
				return ret_val;
		}
	}
	ret_val = em_read_phy_reg_ex(hw, MAX_PHY_REG_ADDRESS & reg_addr,
	    phy_data);

	em_swfw_sync_release(hw, swfw);
	return ret_val;
}

STATIC int32_t
em_read_phy_reg_ex(struct em_hw *hw, uint32_t reg_addr, uint16_t *phy_data)
{
	uint32_t i;
	uint32_t mdic = 0;
	DEBUGFUNC("em_read_phy_reg_ex");

	/* SGMII active is only set on some specific chips */
	if (hw->sgmii_active && !em_sgmii_uses_mdio_82575(hw)) {
		if (reg_addr > E1000_MAX_SGMII_PHY_REG_ADDR) {
			DEBUGOUT1("PHY Address %d is out of range\n", reg_addr);
			return -E1000_ERR_PARAM;
		}
		return em_read_phy_reg_i2c(hw, reg_addr, phy_data);
	}
	if (reg_addr > MAX_PHY_REG_ADDRESS) {
		DEBUGOUT1("PHY Address %d is out of range\n", reg_addr);
		return -E1000_ERR_PARAM;
	}
	if (hw->mac_type == em_icp_xxxx) {
		*phy_data = gcu_miibus_readreg(hw, hw->icp_xxxx_port_num,
		    reg_addr);
		return E1000_SUCCESS;
	}
	if (hw->mac_type > em_82543) {
		/*
		 * Set up Op-code, Phy Address, and register address in the
		 * MDI Control register.  The MAC will take care of
		 * interfacing with the PHY to retrieve the desired data.
		 */
		mdic = ((reg_addr << E1000_MDIC_REG_SHIFT) |
		    (hw->phy_addr << E1000_MDIC_PHY_SHIFT) |
		    (E1000_MDIC_OP_READ));

		E1000_WRITE_REG(hw, MDIC, mdic);

		/*
		 * Poll the ready bit to see if the MDI read completed
		 * Increasing the time out as testing showed failures with
		 * the lower time out (from FreeBSD driver)
		 */
		for (i = 0; i < 1960; i++) {
			usec_delay(50);
			mdic = E1000_READ_REG(hw, MDIC);
			if (mdic & E1000_MDIC_READY)
				break;
		}
		if (!(mdic & E1000_MDIC_READY)) {
			DEBUGOUT("MDI Read did not complete\n");
			return -E1000_ERR_PHY;
		}
		if (mdic & E1000_MDIC_ERROR) {
			DEBUGOUT("MDI Error\n");
			return -E1000_ERR_PHY;
		}
		*phy_data = (uint16_t) mdic;

		if (hw->mac_type == em_pch2lan || hw->mac_type == em_pch_lpt ||
		    hw->mac_type == em_pch_spt || hw->mac_type == em_pch_cnp ||
		    hw->mac_type == em_pch_tgp || hw->mac_type == em_pch_adp)
			usec_delay(100);
	} else {
		/*
		 * We must first send a preamble through the MDIO pin to
		 * signal the beginning of an MII instruction.  This is done
		 * by sending 32 consecutive "1" bits.
		 */
		em_shift_out_mdi_bits(hw, PHY_PREAMBLE, PHY_PREAMBLE_SIZE);
		/*
		 * Now combine the next few fields that are required for a
		 * read operation.  We use this method instead of calling the
		 * em_shift_out_mdi_bits routine five different times. The
		 * format of a MII read instruction consists of a shift out
		 * of 14 bits and is defined as follows: <Preamble><SOF><Op
		 * Code><Phy Addr><Reg Addr> followed by a shift in of 18
		 * bits.  This first two bits shifted in are TurnAround bits
		 * used to avoid contention on the MDIO pin when a READ
		 * operation is performed.  These two bits are thrown away
		 * followed by a shift in of 16 bits which contains the
		 * desired data.
		 */
		mdic = ((reg_addr) | (hw->phy_addr << 5) |
		    (PHY_OP_READ << 10) | (PHY_SOF << 12));

		em_shift_out_mdi_bits(hw, mdic, 14);
		/*
		 * Now that we've shifted out the read command to the MII, we
		 * need to "shift in" the 16-bit value (18 total bits) of the
		 * requested PHY register address.
		 */
		*phy_data = em_shift_in_mdi_bits(hw);
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 * Writes a value to a PHY register
 *
 * hw - Struct containing variables accessed by shared code
 * reg_addr - address of the PHY register to write
 * data - data to write to the PHY
 *****************************************************************************/
int32_t
em_write_phy_reg(struct em_hw *hw, uint32_t reg_addr, uint16_t phy_data)
{
	uint32_t ret_val;
	DEBUGFUNC("em_write_phy_reg");

	if (hw->mac_type == em_pchlan ||
		hw->mac_type == em_pch2lan ||
		hw->mac_type == em_pch_lpt ||
		hw->mac_type == em_pch_spt ||
		hw->mac_type == em_pch_cnp ||
		hw->mac_type == em_pch_tgp ||
		hw->mac_type == em_pch_adp)
		return (em_access_phy_reg_hv(hw, reg_addr, &phy_data, FALSE));

	if (em_swfw_sync_acquire(hw, hw->swfw))
		return -E1000_ERR_SWFW_SYNC;

	if ((hw->phy_type == em_phy_igp ||
	    hw->phy_type == em_phy_igp_3 ||
	    hw->phy_type == em_phy_igp_2) &&
	    (reg_addr > MAX_PHY_MULTI_PAGE_REG)) {
		ret_val = em_write_phy_reg_ex(hw, IGP01E1000_PHY_PAGE_SELECT,
		    (uint16_t) reg_addr);
		if (ret_val) {
			em_swfw_sync_release(hw, hw->swfw);
			return ret_val;
		}
	} else if (hw->phy_type == em_phy_gg82563) {
		if (((reg_addr & MAX_PHY_REG_ADDRESS) > MAX_PHY_MULTI_PAGE_REG) ||
		    (hw->mac_type == em_80003es2lan)) {
			/* Select Configuration Page */
			if ((reg_addr & MAX_PHY_REG_ADDRESS) < 
			    GG82563_MIN_ALT_REG) {
				ret_val = em_write_phy_reg_ex(hw, 
				    GG82563_PHY_PAGE_SELECT,
				    (uint16_t) ((uint16_t) reg_addr >> 
				    GG82563_PAGE_SHIFT));
			} else {
				/*
				 * Use Alternative Page Select register to
				 * access registers 30 and 31
				 */
				ret_val = em_write_phy_reg_ex(hw,
				    GG82563_PHY_PAGE_SELECT_ALT,
				    (uint16_t) ((uint16_t) reg_addr >> 
				    GG82563_PAGE_SHIFT));
			}

			if (ret_val) {
				em_swfw_sync_release(hw, hw->swfw);
				return ret_val;
			}
		}
	} else if ((hw->phy_type == em_phy_bm) && (hw->phy_revision == 1)) {
		if (reg_addr > MAX_PHY_MULTI_PAGE_REG) {
			ret_val = em_write_phy_reg_ex(hw, BM_PHY_PAGE_SELECT,
			    (uint16_t) ((uint16_t) reg_addr >> 
			    PHY_PAGE_SHIFT));
			if (ret_val)
				return ret_val;
		}
	}
	ret_val = em_write_phy_reg_ex(hw, MAX_PHY_REG_ADDRESS & reg_addr,
	    phy_data);

	em_swfw_sync_release(hw, hw->swfw);
	return ret_val;
}

STATIC int32_t
em_write_phy_reg_ex(struct em_hw *hw, uint32_t reg_addr, uint16_t phy_data)
{
	uint32_t i;
	uint32_t mdic = 0;
	DEBUGFUNC("em_write_phy_reg_ex");

	/* SGMII active is only set on some specific chips */
	if (hw->sgmii_active && !em_sgmii_uses_mdio_82575(hw)) {
		if (reg_addr > E1000_MAX_SGMII_PHY_REG_ADDR) {
			DEBUGOUT1("PHY Address %d is out of range\n", reg_addr);
			return -E1000_ERR_PARAM;
		}
		return em_write_phy_reg_i2c(hw, reg_addr, phy_data);
	}
	if (reg_addr > MAX_PHY_REG_ADDRESS) {
		DEBUGOUT1("PHY Address %d is out of range\n", reg_addr);
		return -E1000_ERR_PARAM;
	}
	if (hw->mac_type == em_icp_xxxx) {
		gcu_miibus_writereg(hw, hw->icp_xxxx_port_num,
		    reg_addr, phy_data);
		return E1000_SUCCESS;
	}
	if (hw->mac_type > em_82543) {
		/*
		 * Set up Op-code, Phy Address, register address, and data
		 * intended for the PHY register in the MDI Control register.
		 * The MAC will take care of interfacing with the PHY to send
		 * the desired data.
		 */
		mdic = (((uint32_t) phy_data) |
			(reg_addr << E1000_MDIC_REG_SHIFT) |
			(hw->phy_addr << E1000_MDIC_PHY_SHIFT) |
			(E1000_MDIC_OP_WRITE));

		E1000_WRITE_REG(hw, MDIC, mdic);

		/* Poll the ready bit to see if the MDI read completed */
		for (i = 0; i < 641; i++) {
			usec_delay(5);
			mdic = E1000_READ_REG(hw, MDIC);
			if (mdic & E1000_MDIC_READY)
				break;
		}
		if (!(mdic & E1000_MDIC_READY)) {
			DEBUGOUT("MDI Write did not complete\n");
			return -E1000_ERR_PHY;
		}

		if (hw->mac_type == em_pch2lan || hw->mac_type == em_pch_lpt ||
		    hw->mac_type == em_pch_spt || hw->mac_type == em_pch_cnp ||
		    hw->mac_type == em_pch_tgp || hw->mac_type == em_pch_adp)
			usec_delay(100);
	} else {
		/*
		 * We'll need to use the SW defined pins to shift the write
		 * command out to the PHY. We first send a preamble to the
		 * PHY to signal the beginning of the MII instruction.  This
		 * is done by sending 32 consecutive "1" bits.
		 */
		em_shift_out_mdi_bits(hw, PHY_PREAMBLE, PHY_PREAMBLE_SIZE);
		/*
		 * Now combine the remaining required fields that will
		 * indicate a write operation. We use this method instead of
		 * calling the em_shift_out_mdi_bits routine for each field
		 * in the command. The format of a MII write instruction is
		 * as follows: <Preamble><SOF><Op Code><Phy Addr><Reg
		 * Addr><Turnaround><Data>.
		 */
		mdic = ((PHY_TURNAROUND) | (reg_addr << 2) | 
		    (hw->phy_addr << 7) | (PHY_OP_WRITE << 12) | 
		    (PHY_SOF << 14));
		mdic <<= 16;
		mdic |= (uint32_t) phy_data;

		em_shift_out_mdi_bits(hw, mdic, 32);
	}

	return E1000_SUCCESS;
}

STATIC int32_t
em_read_kmrn_reg(struct em_hw *hw, uint32_t reg_addr, uint16_t *data)
{
	uint32_t reg_val;
	DEBUGFUNC("em_read_kmrn_reg");

	if (em_swfw_sync_acquire(hw, hw->swfw))
		return -E1000_ERR_SWFW_SYNC;

	/* Write register address */
	reg_val = ((reg_addr << E1000_KUMCTRLSTA_OFFSET_SHIFT) &
	    E1000_KUMCTRLSTA_OFFSET) |
	    E1000_KUMCTRLSTA_REN;

	E1000_WRITE_REG(hw, KUMCTRLSTA, reg_val);
	usec_delay(2);

	/* Read the data returned */
	reg_val = E1000_READ_REG(hw, KUMCTRLSTA);
	*data = (uint16_t) reg_val;

	em_swfw_sync_release(hw, hw->swfw);
	return E1000_SUCCESS;
}

STATIC int32_t
em_write_kmrn_reg(struct em_hw *hw, uint32_t reg_addr, uint16_t data)
{
	uint32_t reg_val;
	DEBUGFUNC("em_write_kmrn_reg");

	if (em_swfw_sync_acquire(hw, hw->swfw))
		return -E1000_ERR_SWFW_SYNC;

	reg_val = ((reg_addr << E1000_KUMCTRLSTA_OFFSET_SHIFT) &
	    E1000_KUMCTRLSTA_OFFSET) | data;

	E1000_WRITE_REG(hw, KUMCTRLSTA, reg_val);
	usec_delay(2);

	em_swfw_sync_release(hw, hw->swfw);
	return E1000_SUCCESS;
}

/**
 *  em_sgmii_uses_mdio_82575 - Determine if I2C pins are for external MDIO
 *  @hw: pointer to the HW structure
 *
 *  Called to determine if the I2C pins are being used for I2C or as an
 *  external MDIO interface since the two options are mutually exclusive.
 **/
int
em_sgmii_uses_mdio_82575(struct em_hw *hw)
{
	uint32_t reg = 0;
	int ext_mdio = 0;

	DEBUGFUNC("em_sgmii_uses_mdio_82575");

	switch (hw->mac_type) {
	case em_82575:
	case em_82576:
		reg = E1000_READ_REG(hw, MDIC);
		ext_mdio = !!(reg & E1000_MDIC_DEST);
		break;
	case em_82580:
	case em_i350:
	case em_i210:
		reg = E1000_READ_REG(hw, MDICNFG);
		ext_mdio = !!(reg & E1000_MDICNFG_EXT_MDIO);
		break;
	default:
		break;
	}
	return ext_mdio;
}

/**
 *  em_read_phy_reg_i2c - Read PHY register using i2c
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Reads the PHY register at offset using the i2c interface and stores the
 *  retrieved information in data.
 **/
int32_t
em_read_phy_reg_i2c(struct em_hw *hw, uint32_t offset, uint16_t *data)
{
	uint32_t i, i2ccmd = 0;

	DEBUGFUNC("em_read_phy_reg_i2c");

	/* Set up Op-code, Phy Address, and register address in the I2CCMD
	 * register.  The MAC will take care of interfacing with the
	 * PHY to retrieve the desired data.
	 */
	i2ccmd = ((offset << E1000_I2CCMD_REG_ADDR_SHIFT) |
		  (hw->phy_addr << E1000_I2CCMD_PHY_ADDR_SHIFT) |
		  (E1000_I2CCMD_OPCODE_READ));

	E1000_WRITE_REG(hw, I2CCMD, i2ccmd);

	/* Poll the ready bit to see if the I2C read completed */
	for (i = 0; i < E1000_I2CCMD_PHY_TIMEOUT; i++) {
		usec_delay(50);
		i2ccmd = E1000_READ_REG(hw, I2CCMD);
		if (i2ccmd & E1000_I2CCMD_READY)
			break;
	}
	if (!(i2ccmd & E1000_I2CCMD_READY)) {
		DEBUGOUT("I2CCMD Read did not complete\n");
		return -E1000_ERR_PHY;
	}
	if (i2ccmd & E1000_I2CCMD_ERROR) {
		DEBUGOUT("I2CCMD Error bit set\n");
		return -E1000_ERR_PHY;
	}

	/* Need to byte-swap the 16-bit value. */
	*data = ((i2ccmd >> 8) & 0x00FF) | ((i2ccmd << 8) & 0xFF00);

	return E1000_SUCCESS;
}

/**
 *  em_write_phy_reg_i2c - Write PHY register using i2c
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Writes the data to PHY register at the offset using the i2c interface.
 **/
int32_t
em_write_phy_reg_i2c(struct em_hw *hw, uint32_t offset, uint16_t data)
{
	uint32_t i, i2ccmd = 0;
	uint16_t phy_data_swapped;

	DEBUGFUNC("em_write_phy_reg_i2c");

	/* Prevent overwriting SFP I2C EEPROM which is at A0 address.*/
	if ((hw->phy_addr == 0) || (hw->phy_addr > 7)) {
		DEBUGOUT1("PHY I2C Address %d is out of range.\n",
			  hw->phy_addr);
		return -E1000_ERR_CONFIG;
	}

	/* Swap the data bytes for the I2C interface */
	phy_data_swapped = ((data >> 8) & 0x00FF) | ((data << 8) & 0xFF00);

	/* Set up Op-code, Phy Address, and register address in the I2CCMD
	 * register.  The MAC will take care of interfacing with the
	 * PHY to retrieve the desired data.
	 */
	i2ccmd = ((offset << E1000_I2CCMD_REG_ADDR_SHIFT) |
		  (hw->phy_addr << E1000_I2CCMD_PHY_ADDR_SHIFT) |
		  E1000_I2CCMD_OPCODE_WRITE |
		  phy_data_swapped);

	E1000_WRITE_REG(hw, I2CCMD, i2ccmd);

	/* Poll the ready bit to see if the I2C read completed */
	for (i = 0; i < E1000_I2CCMD_PHY_TIMEOUT; i++) {
		usec_delay(50);
		i2ccmd = E1000_READ_REG(hw, I2CCMD);
		if (i2ccmd & E1000_I2CCMD_READY)
			break;
	}
	if (!(i2ccmd & E1000_I2CCMD_READY)) {
		DEBUGOUT("I2CCMD Write did not complete\n");
		return -E1000_ERR_PHY;
	}
	if (i2ccmd & E1000_I2CCMD_ERROR) {
		DEBUGOUT("I2CCMD Error bit set\n");
		return -E1000_ERR_PHY;
	}

	return E1000_SUCCESS;
}

/**
 *  em_read_sfp_data_byte - Reads SFP module data.
 *  @hw: pointer to the HW structure
 *  @offset: byte location offset to be read
 *  @data: read data buffer pointer
 *
 *  Reads one byte from SFP module data stored
 *  in SFP resided EEPROM memory or SFP diagnostic area.
 *  Function should be called with
 *  E1000_I2CCMD_SFP_DATA_ADDR(<byte offset>) for SFP module database access
 *  E1000_I2CCMD_SFP_DIAG_ADDR(<byte offset>) for SFP diagnostics parameters
 *  access
 **/
int32_t
em_read_sfp_data_byte(struct em_hw *hw, uint16_t offset, uint8_t *data)
{
	uint32_t i = 0;
	uint32_t i2ccmd = 0;
	uint32_t data_local = 0;

	DEBUGFUNC("em_read_sfp_data_byte");

	if (offset > E1000_I2CCMD_SFP_DIAG_ADDR(255)) {
		DEBUGOUT("I2CCMD command address exceeds upper limit\n");
		return -E1000_ERR_PHY;
	}

	/* Set up Op-code, EEPROM Address,in the I2CCMD
	 * register. The MAC will take care of interfacing with the
	 * EEPROM to retrieve the desired data.
	 */
	i2ccmd = ((offset << E1000_I2CCMD_REG_ADDR_SHIFT) |
		  E1000_I2CCMD_OPCODE_READ);

	E1000_WRITE_REG(hw, I2CCMD, i2ccmd);

	/* Poll the ready bit to see if the I2C read completed */
	for (i = 0; i < E1000_I2CCMD_PHY_TIMEOUT; i++) {
		usec_delay(50);
		data_local = E1000_READ_REG(hw, I2CCMD);
		if (data_local & E1000_I2CCMD_READY)
			break;
	}
	if (!(data_local & E1000_I2CCMD_READY)) {
		DEBUGOUT("I2CCMD Read did not complete\n");
		return -E1000_ERR_PHY;
	}
	if (data_local & E1000_I2CCMD_ERROR) {
		DEBUGOUT("I2CCMD Error bit set\n");
		return -E1000_ERR_PHY;
	}
	*data = (uint8_t) data_local & 0xFF;

	return E1000_SUCCESS;
}

/******************************************************************************
 * Returns the PHY to the power-on reset state
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
int32_t
em_phy_hw_reset(struct em_hw *hw)
{
	uint32_t ctrl, ctrl_ext;
	uint32_t led_ctrl;
	int32_t  ret_val;
	DEBUGFUNC("em_phy_hw_reset");
	/*
	 * In the case of the phy reset being blocked, it's not an error, we
	 * simply return success without performing the reset.
	 */
	ret_val = em_check_phy_reset_block(hw);
	if (ret_val)
		return E1000_SUCCESS;

	DEBUGOUT("Resetting Phy...\n");

	if (hw->mac_type > em_82543 && hw->mac_type != em_icp_xxxx) {
		if (em_swfw_sync_acquire(hw, hw->swfw)) {
			DEBUGOUT("Unable to acquire swfw sync\n");
			return -E1000_ERR_SWFW_SYNC;
		}
		/*
		 * Read the device control register and assert the
		 * E1000_CTRL_PHY_RST bit. Then, take it out of reset. For
		 * pre-em_82571 hardware, we delay for 10ms between the
		 * assert and deassert.  For em_82571 hardware and later, we
		 * instead delay for 50us between and 10ms after the
		 * deassertion.
		 */
		ctrl = E1000_READ_REG(hw, CTRL);
		E1000_WRITE_REG(hw, CTRL, ctrl | E1000_CTRL_PHY_RST);
		E1000_WRITE_FLUSH(hw);

		if (hw->mac_type < em_82571)
			msec_delay(10);
		else
			usec_delay(100);

		E1000_WRITE_REG(hw, CTRL, ctrl);
		E1000_WRITE_FLUSH(hw);

		if (hw->mac_type >= em_82571)
			msec_delay_irq(10);
		em_swfw_sync_release(hw, hw->swfw);
		/*
		 * the M88E1141_E_PHY_ID might need reset here, but nothing
		 * proves it
		 */
	} else {
		/*
		 * Read the Extended Device Control Register, assert the
		 * PHY_RESET_DIR bit to put the PHY into reset. Then, take it
		 * out of reset.
		 */
		ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
		ctrl_ext |= E1000_CTRL_EXT_SDP4_DIR;
		ctrl_ext &= ~E1000_CTRL_EXT_SDP4_DATA;
		E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
		E1000_WRITE_FLUSH(hw);
		msec_delay(10);
		ctrl_ext |= E1000_CTRL_EXT_SDP4_DATA;
		E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
		E1000_WRITE_FLUSH(hw);
	}
	usec_delay(150);

	if ((hw->mac_type == em_82541) || (hw->mac_type == em_82547)) {
		/* Configure activity LED after PHY reset */
		led_ctrl = E1000_READ_REG(hw, LEDCTL);
		led_ctrl &= IGP_ACTIVITY_LED_MASK;
		led_ctrl |= (IGP_ACTIVITY_LED_ENABLE | IGP_LED3_MODE);
		E1000_WRITE_REG(hw, LEDCTL, led_ctrl);
	}
	/* Wait for FW to finish PHY configuration. */
	ret_val = em_get_phy_cfg_done(hw);
	if (ret_val != E1000_SUCCESS)
		return ret_val;
	em_release_software_semaphore(hw);

	if ((hw->mac_type == em_ich8lan) && (hw->phy_type == em_phy_igp_3))
		ret_val = em_init_lcd_from_nvm(hw);

	return ret_val;
}

/*****************************************************************************
 *  SW-based LCD Configuration.
 *  SW will configure Gbe Disable and LPLU based on the NVM. The four bits are
 *  collectively called OEM bits.  The OEM Write Enable bit and SW Config bit
 *  in NVM determines whether HW should configure LPLU and Gbe Disable.
 *****************************************************************************/
int32_t
em_oem_bits_config_pchlan(struct em_hw *hw, boolean_t d0_state)
{
	int32_t  ret_val = E1000_SUCCESS;
	uint32_t mac_reg;
	uint16_t oem_reg;
	uint16_t swfw = E1000_SWFW_PHY0_SM;

	if (hw->mac_type < em_pchlan)
		return ret_val;

	ret_val = em_swfw_sync_acquire(hw, swfw);
	if (ret_val)
		return ret_val;

	if (hw->mac_type == em_pchlan) {
		mac_reg = E1000_READ_REG(hw, EXTCNF_CTRL);
		if (mac_reg & E1000_EXTCNF_CTRL_OEM_WRITE_ENABLE)
			goto out;
	}

	mac_reg = E1000_READ_REG(hw, FEXTNVM);
	if (!(mac_reg & FEXTNVM_SW_CONFIG_ICH8M))
		goto out;

	mac_reg = E1000_READ_REG(hw, PHY_CTRL);

	ret_val = em_read_phy_reg(hw, HV_OEM_BITS, &oem_reg);
	if (ret_val)
		goto out;

	oem_reg &= ~(HV_OEM_BITS_GBE_DIS | HV_OEM_BITS_LPLU);

	if (d0_state) {
		if (mac_reg & E1000_PHY_CTRL_GBE_DISABLE)
			oem_reg |= HV_OEM_BITS_GBE_DIS;

		if (mac_reg & E1000_PHY_CTRL_D0A_LPLU)
			oem_reg |= HV_OEM_BITS_LPLU;
		/* Restart auto-neg to activate the bits */
		if (!em_check_phy_reset_block(hw))
			oem_reg |= HV_OEM_BITS_RESTART_AN;

	} else {
		if (mac_reg & (E1000_PHY_CTRL_GBE_DISABLE |
		    E1000_PHY_CTRL_NOND0A_GBE_DISABLE))
			oem_reg |= HV_OEM_BITS_GBE_DIS;

		if (mac_reg & (E1000_PHY_CTRL_D0A_LPLU |
		    E1000_PHY_CTRL_NOND0A_LPLU))
			oem_reg |= HV_OEM_BITS_LPLU;
	}

	ret_val = em_write_phy_reg(hw, HV_OEM_BITS, oem_reg);

out:
	em_swfw_sync_release(hw, swfw);

	return ret_val;
}


/******************************************************************************
 * Resets the PHY
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Sets bit 15 of the MII Control register
 *****************************************************************************/
int32_t
em_phy_reset(struct em_hw *hw)
{
	int32_t  ret_val;
	uint16_t phy_data;
	DEBUGFUNC("em_phy_reset");
	/*
	 * In the case of the phy reset being blocked, it's not an error, we
	 * simply return success without performing the reset.
	 */
	ret_val = em_check_phy_reset_block(hw);
	if (ret_val)
		return E1000_SUCCESS;

	switch (hw->phy_type) {
	case em_phy_igp:
	case em_phy_igp_2:
	case em_phy_igp_3:
	case em_phy_ife:
		ret_val = em_phy_hw_reset(hw);
		if (ret_val)
			return ret_val;
		break;
	default:
		ret_val = em_read_phy_reg(hw, PHY_CTRL, &phy_data);
		if (ret_val)
			return ret_val;

		phy_data |= MII_CR_RESET;
		ret_val = em_write_phy_reg(hw, PHY_CTRL, phy_data);
		if (ret_val)
			return ret_val;

		usec_delay(1);
		break;
	}

	/* Allow time for h/w to get to a quiescent state after reset */
	msec_delay(10);

	if (hw->phy_type == em_phy_igp || hw->phy_type == em_phy_igp_2)
		em_phy_init_script(hw);

	if (hw->mac_type == em_pchlan) {
		ret_val = em_hv_phy_workarounds_ich8lan(hw);
		if (ret_val)
			return ret_val;
	} else if (hw->mac_type == em_pch2lan) {
		ret_val = em_lv_phy_workarounds_ich8lan(hw);
		if (ret_val)
			return ret_val;
	}
	
	if (hw->mac_type >= em_pchlan) {
		ret_val = em_oem_bits_config_pchlan(hw, TRUE);
		if (ret_val)
			return ret_val;
	}

	/* Ungate automatic PHY configuration on non-managed 82579 */
	if ((hw->mac_type == em_pch2lan) &&
	    !(E1000_READ_REG(hw, FWSM) & E1000_FWSM_FW_VALID)) {
		msec_delay(10);
		em_gate_hw_phy_config_ich8lan(hw, FALSE);
	}

	if (hw->phy_id == M88E1512_E_PHY_ID) {
		ret_val = em_initialize_M88E1512_phy(hw);
		if (ret_val)
			return ret_val;
	}

	return E1000_SUCCESS;
}

/******************************************************************************
 * Work-around for 82566 Kumeran PCS lock loss:
 * On link status change (i.e. PCI reset, speed change) and link is up and
 * speed is gigabit-
 * 0) if workaround is optionally disabled do nothing
 * 1) wait 1ms for Kumeran link to come up
 * 2) check Kumeran Diagnostic register PCS lock loss bit
 * 3) if not set the link is locked (all is good), otherwise...
 * 4) reset the PHY
 * 5) repeat up to 10 times
 * Note: this is only called for IGP3 copper when speed is 1gb.
 *
 * hw - struct containing variables accessed by shared code
 *****************************************************************************/
STATIC int32_t
em_kumeran_lock_loss_workaround(struct em_hw *hw)
{
	int32_t  ret_val;
	int32_t  reg;
	int32_t  cnt;
	uint16_t phy_data;
	if (hw->kmrn_lock_loss_workaround_disabled)
		return E1000_SUCCESS;
	/*
	 * Make sure link is up before proceeding.  If not just return.
	 * Attempting this while link is negotiating fouled up link stability
	 */
	ret_val = em_read_phy_reg(hw, PHY_STATUS, &phy_data);
	ret_val = em_read_phy_reg(hw, PHY_STATUS, &phy_data);

	if (phy_data & MII_SR_LINK_STATUS) {
		for (cnt = 0; cnt < 10; cnt++) {
			/* read once to clear */
			ret_val = em_read_phy_reg(hw, IGP3_KMRN_DIAG,
			    &phy_data);
			if (ret_val)
				return ret_val;
			/* and again to get new status */
			ret_val = em_read_phy_reg(hw, IGP3_KMRN_DIAG, &phy_data);
			if (ret_val)
				return ret_val;

			/* check for PCS lock */
			if (!(phy_data & IGP3_KMRN_DIAG_PCS_LOCK_LOSS))
				return E1000_SUCCESS;

			/* Issue PHY reset */
			em_phy_hw_reset(hw);
			msec_delay_irq(5);
		}
		/* Disable GigE link negotiation */
		reg = E1000_READ_REG(hw, PHY_CTRL);
		E1000_WRITE_REG(hw, PHY_CTRL, reg | E1000_PHY_CTRL_GBE_DISABLE
		    | E1000_PHY_CTRL_NOND0A_GBE_DISABLE);

		/* unable to acquire PCS lock */
		return E1000_ERR_PHY;
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 * Reads and matches the expected PHY address for known PHY IDs
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
STATIC int32_t
em_match_gig_phy(struct em_hw *hw)
{
	int32_t   phy_init_status, ret_val;
	uint16_t  phy_id_high, phy_id_low;
	boolean_t match = FALSE;
	DEBUGFUNC("em_match_gig_phy");

	ret_val = em_read_phy_reg(hw, PHY_ID1, &phy_id_high);
	if (ret_val)
		return ret_val;

	hw->phy_id = (uint32_t) (phy_id_high << 16);
	usec_delay(20);
	ret_val = em_read_phy_reg(hw, PHY_ID2, &phy_id_low);
	if (ret_val)
		return ret_val;

	hw->phy_id |= (uint32_t) (phy_id_low & PHY_REVISION_MASK);
	hw->phy_revision = (uint32_t) phy_id_low & ~PHY_REVISION_MASK;

	switch (hw->mac_type) {
	case em_82543:
		if (hw->phy_id == M88E1000_E_PHY_ID)
			match = TRUE;
		break;
	case em_82544:
		if (hw->phy_id == M88E1000_I_PHY_ID)
			match = TRUE;
		break;
	case em_82540:
	case em_82545:
	case em_82545_rev_3:
	case em_82546:
	case em_82546_rev_3:
		if (hw->phy_id == M88E1011_I_PHY_ID)
			match = TRUE;
		break;
	case em_82541:
	case em_82541_rev_2:
	case em_82547:
	case em_82547_rev_2:
		if (hw->phy_id == IGP01E1000_I_PHY_ID)
			match = TRUE;
		break;
	case em_82573:
		if (hw->phy_id == M88E1111_I_PHY_ID)
			match = TRUE;
		break;
	case em_82574:
		if (hw->phy_id == BME1000_E_PHY_ID)
			match = TRUE;
		break;
	case em_82575:
	case em_82576:
		if (hw->phy_id == M88E1000_E_PHY_ID)
			match = TRUE;
		if (hw->phy_id == IGP01E1000_I_PHY_ID)
			match = TRUE;
		if (hw->phy_id == IGP03E1000_E_PHY_ID)
			match = TRUE;
		break;
	case em_82580:
	case em_i210:
	case em_i350:
		if (hw->phy_id == I82580_I_PHY_ID ||
		    hw->phy_id == I210_I_PHY_ID ||
		    hw->phy_id == I347AT4_E_PHY_ID ||
		    hw->phy_id == I350_I_PHY_ID ||
		    hw->phy_id == M88E1111_I_PHY_ID ||
		    hw->phy_id == M88E1112_E_PHY_ID ||
		    hw->phy_id == M88E1543_E_PHY_ID ||
		    hw->phy_id == M88E1512_E_PHY_ID) {
			uint32_t mdic;

			mdic = EM_READ_REG(hw, E1000_MDICNFG);
			if (mdic & E1000_MDICNFG_EXT_MDIO) {
				mdic &= E1000_MDICNFG_PHY_MASK;
				hw->phy_addr = mdic >> E1000_MDICNFG_PHY_SHIFT;
				DEBUGOUT1("MDICNFG PHY ADDR %d",
				    mdic >> E1000_MDICNFG_PHY_SHIFT);
			}
			match = TRUE;
		}
		break;
	case em_80003es2lan:
		if (hw->phy_id == GG82563_E_PHY_ID)
			match = TRUE;
		break;
	case em_ich8lan:
	case em_ich9lan:
	case em_ich10lan:
	case em_pchlan:
	case em_pch2lan:
		if (hw->phy_id == IGP03E1000_E_PHY_ID)
			match = TRUE;
		if (hw->phy_id == IFE_E_PHY_ID)
			match = TRUE;
		if (hw->phy_id == IFE_PLUS_E_PHY_ID)
			match = TRUE;
		if (hw->phy_id == IFE_C_E_PHY_ID)
			match = TRUE;
		if (hw->phy_id == BME1000_E_PHY_ID)
			match = TRUE;
		if (hw->phy_id == I82577_E_PHY_ID)
			match = TRUE;
		if (hw->phy_id == I82578_E_PHY_ID)
			match = TRUE;
		if (hw->phy_id == I82579_E_PHY_ID)
			match = TRUE;
		break;
	case em_pch_lpt:
	case em_pch_spt:
	case em_pch_cnp:
	case em_pch_tgp:
	case em_pch_adp:
		if (hw->phy_id == I217_E_PHY_ID)
			match = TRUE;
		break;
	case em_icp_xxxx:
		if (hw->phy_id == M88E1141_E_PHY_ID)
			match = TRUE;
		if (hw->phy_id == RTL8211_E_PHY_ID)
			match = TRUE;
		break;
	default:
		DEBUGOUT1("Invalid MAC type %d\n", hw->mac_type);
		return -E1000_ERR_CONFIG;
	}
	phy_init_status = em_set_phy_type(hw);

	if ((match) && (phy_init_status == E1000_SUCCESS)) {
		DEBUGOUT1("PHY ID 0x%X detected\n", hw->phy_id);
		return E1000_SUCCESS;
	}
	DEBUGOUT1("Invalid PHY ID 0x%X\n", hw->phy_id);
	return -E1000_ERR_PHY;
}

/******************************************************************************
 * Probes the expected PHY address for known PHY IDs
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
STATIC int32_t
em_detect_gig_phy(struct em_hw *hw)
{
	int32_t ret_val, i;
	DEBUGFUNC("em_detect_gig_phy");

	if (hw->phy_id != 0)
		return E1000_SUCCESS;

	/* default phy address, most phys reside here, but not all (ICH10) */
	if (hw->mac_type != em_icp_xxxx)
		hw->phy_addr = 1;
	else
		hw->phy_addr = 0; /* There is a phy at phy_addr 0 on EP80579 */

	/*
	 * The 82571 firmware may still be configuring the PHY.  In this
	 * case, we cannot access the PHY until the configuration is done.
	 * So we explicitly set the PHY values.
	 */
	if (hw->mac_type == em_82571 ||
	    hw->mac_type == em_82572) {
		hw->phy_id = IGP01E1000_I_PHY_ID;
		hw->phy_type = em_phy_igp_2;
		return E1000_SUCCESS;
	}

	/*
	 * Some of the fiber cards dont have a phy, so we must exit cleanly
	 * here
	 */
	if ((hw->media_type == em_media_type_fiber) &&
	    (hw->mac_type == em_82542_rev2_0 ||
	    hw->mac_type == em_82542_rev2_1 ||
	    hw->mac_type == em_82543 ||
	    hw->mac_type == em_82573 ||
	    hw->mac_type == em_82574 ||
	    hw->mac_type == em_80003es2lan)) {
		hw->phy_type = em_phy_undefined;
		return E1000_SUCCESS;
	}

	if ((hw->media_type == em_media_type_internal_serdes ||
	    hw->media_type == em_media_type_fiber) &&
	    hw->mac_type >= em_82575) {
		hw->phy_type = em_phy_undefined;
		return E1000_SUCCESS;
	}

	/*
	 * Up to 82543 (incl), we need reset the phy, or it might not get
	 * detected
	 */
	if (hw->mac_type <= em_82543) {
		ret_val = em_phy_hw_reset(hw);
		if (ret_val)
			return ret_val;
	}
	/*
	 * ESB-2 PHY reads require em_phy_gg82563 to be set because of a
	 * work- around that forces PHY page 0 to be set or the reads fail.
	 * The rest of the code in this routine uses em_read_phy_reg to read
	 * the PHY ID. So for ESB-2 we need to have this set so our reads
	 * won't fail.  If the attached PHY is not a em_phy_gg82563, the
	 * routines below will figure this out as well.
	 */
	if (hw->mac_type == em_80003es2lan)
		hw->phy_type = em_phy_gg82563;

	/* Power on SGMII phy if it is disabled */
	if (hw->mac_type == em_82580 || hw->mac_type == em_i210 ||
	    hw->mac_type == em_i350) {
		uint32_t ctrl_ext = EM_READ_REG(hw, E1000_CTRL_EXT);
		EM_WRITE_REG(hw, E1000_CTRL_EXT,
		    ctrl_ext & ~E1000_CTRL_EXT_SDP3_DATA);
		E1000_WRITE_FLUSH(hw);
		msec_delay(300);
	}

	/* Read the PHY ID Registers to identify which PHY is onboard. */
	for (i = 1; i < 8; i++) {
		/*
		 * hw->phy_addr may be modified down in the call stack,
		 * we can't use it as loop variable.
		 */
		hw->phy_addr = i;
		ret_val = em_match_gig_phy(hw);
		if (ret_val == E1000_SUCCESS)
			return E1000_SUCCESS;
	}
	return -E1000_ERR_PHY;
}

/******************************************************************************
 * Resets the PHY's DSP
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static int32_t
em_phy_reset_dsp(struct em_hw *hw)
{
	int32_t ret_val;
	DEBUGFUNC("em_phy_reset_dsp");

	do {
		if (hw->phy_type != em_phy_gg82563) {
			ret_val = em_write_phy_reg(hw, 29, 0x001d);
			if (ret_val)
				break;
		}
		ret_val = em_write_phy_reg(hw, 30, 0x00c1);
		if (ret_val)
			break;
		ret_val = em_write_phy_reg(hw, 30, 0x0000);
		if (ret_val)
			break;
		ret_val = E1000_SUCCESS;
	} while (0);

	return ret_val;
}

/******************************************************************************
 * Sets up eeprom variables in the hw struct.  Must be called after mac_type
 * is configured.  Additionally, if this is ICH8, the flash controller GbE
 * registers must be mapped, or this will crash.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
int32_t
em_init_eeprom_params(struct em_hw *hw)
{
	struct em_eeprom_info *eeprom = &hw->eeprom;
	uint32_t eecd = E1000_READ_REG(hw, EECD);
	int32_t  ret_val = E1000_SUCCESS;
	uint16_t eeprom_size;
	DEBUGFUNC("em_init_eeprom_params");

	switch (hw->mac_type) {
	case em_82542_rev2_0:
	case em_82542_rev2_1:
	case em_82543:
	case em_82544:
		eeprom->type = em_eeprom_microwire;
		eeprom->word_size = 64;
		eeprom->opcode_bits = 3;
		eeprom->address_bits = 6;
		eeprom->delay_usec = 50;
		eeprom->use_eerd = FALSE;
		eeprom->use_eewr = FALSE;
		break;
	case em_82540:
	case em_82545:
	case em_82545_rev_3:
	case em_icp_xxxx:
	case em_82546:
	case em_82546_rev_3:
		eeprom->type = em_eeprom_microwire;
		eeprom->opcode_bits = 3;
		eeprom->delay_usec = 50;
		if (eecd & E1000_EECD_SIZE) {
			eeprom->word_size = 256;
			eeprom->address_bits = 8;
		} else {
			eeprom->word_size = 64;
			eeprom->address_bits = 6;
		}
		eeprom->use_eerd = FALSE;
		eeprom->use_eewr = FALSE;
		break;
	case em_82541:
	case em_82541_rev_2:
	case em_82547:
	case em_82547_rev_2:
		if (eecd & E1000_EECD_TYPE) {
			eeprom->type = em_eeprom_spi;
			eeprom->opcode_bits = 8;
			eeprom->delay_usec = 1;
			if (eecd & E1000_EECD_ADDR_BITS) {
				eeprom->page_size = 32;
				eeprom->address_bits = 16;
			} else {
				eeprom->page_size = 8;
				eeprom->address_bits = 8;
			}
		} else {
			eeprom->type = em_eeprom_microwire;
			eeprom->opcode_bits = 3;
			eeprom->delay_usec = 50;
			if (eecd & E1000_EECD_ADDR_BITS) {
				eeprom->word_size = 256;
				eeprom->address_bits = 8;
			} else {
				eeprom->word_size = 64;
				eeprom->address_bits = 6;
			}
		}
		eeprom->use_eerd = FALSE;
		eeprom->use_eewr = FALSE;
		break;
	case em_82571:
	case em_82572:
		eeprom->type = em_eeprom_spi;
		eeprom->opcode_bits = 8;
		eeprom->delay_usec = 1;
		if (eecd & E1000_EECD_ADDR_BITS) {
			eeprom->page_size = 32;
			eeprom->address_bits = 16;
		} else {
			eeprom->page_size = 8;
			eeprom->address_bits = 8;
		}
		eeprom->use_eerd = FALSE;
		eeprom->use_eewr = FALSE;
		break;
	case em_82573:
	case em_82574:
	case em_82575:
	case em_82576:
	case em_82580:
	case em_i210:
	case em_i350:
		eeprom->type = em_eeprom_spi;
		eeprom->opcode_bits = 8;
		eeprom->delay_usec = 1;
		if (eecd & E1000_EECD_ADDR_BITS) {
			eeprom->page_size = 32;
			eeprom->address_bits = 16;
		} else {
			eeprom->page_size = 8;
			eeprom->address_bits = 8;
		}
		eeprom->use_eerd = TRUE;
		eeprom->use_eewr = TRUE;
		if (em_is_onboard_nvm_eeprom(hw) == FALSE) {
			eeprom->type = em_eeprom_flash;
			eeprom->word_size = 2048;
			/*
			 * Ensure that the Autonomous FLASH update bit is
			 * cleared due to Flash update issue on parts which
			 * use a FLASH for NVM.
			 */
			eecd &= ~E1000_EECD_AUPDEN;
			E1000_WRITE_REG(hw, EECD, eecd);
		}
		if (em_get_flash_presence_i210(hw) == FALSE) {
			eeprom->type = em_eeprom_invm;
			eeprom->word_size = INVM_SIZE;
			eeprom->use_eerd = FALSE;
			eeprom->use_eewr = FALSE;
		}
		break;
	case em_80003es2lan:
		eeprom->type = em_eeprom_spi;
		eeprom->opcode_bits = 8;
		eeprom->delay_usec = 1;
		if (eecd & E1000_EECD_ADDR_BITS) {
			eeprom->page_size = 32;
			eeprom->address_bits = 16;
		} else {
			eeprom->page_size = 8;
			eeprom->address_bits = 8;
		}
		eeprom->use_eerd = TRUE;
		eeprom->use_eewr = FALSE;
		break;
	case em_ich8lan:
	case em_ich9lan:
	case em_ich10lan:
	case em_pchlan:
	case em_pch2lan:
	case em_pch_lpt:
		{
			int32_t		i = 0;
			uint32_t	flash_size = 
			    E1000_READ_ICH_FLASH_REG(hw, ICH_FLASH_GFPREG);
			eeprom->type = em_eeprom_ich8;
			eeprom->use_eerd = FALSE;
			eeprom->use_eewr = FALSE;
			eeprom->word_size = E1000_SHADOW_RAM_WORDS;
			/*
			 * Zero the shadow RAM structure. But don't load it
			 * from NVM so as to save time for driver init
			 */
			if (hw->eeprom_shadow_ram != NULL) {
				for (i = 0; i < E1000_SHADOW_RAM_WORDS; i++) {
					hw->eeprom_shadow_ram[i].modified = 
					    FALSE;
					hw->eeprom_shadow_ram[i].eeprom_word = 
					    0xFFFF;
				}
			}
			hw->flash_base_addr = (flash_size & 
			    ICH_GFPREG_BASE_MASK) * ICH_FLASH_SECTOR_SIZE;

			hw->flash_bank_size = ((flash_size >> 16) & 
			    ICH_GFPREG_BASE_MASK) + 1;
			hw->flash_bank_size -= (flash_size &
			    ICH_GFPREG_BASE_MASK);

			hw->flash_bank_size *= ICH_FLASH_SECTOR_SIZE;

			hw->flash_bank_size /= 2 * sizeof(uint16_t);

			break;
		}
	case em_pch_spt:
	case em_pch_cnp:
	case em_pch_tgp:
	case em_pch_adp:
		{
			int32_t         i = 0;
			uint32_t        flash_size = EM_READ_REG(hw, 0xc /* STRAP */);

			eeprom->type = em_eeprom_ich8;
			eeprom->use_eerd = FALSE;
			eeprom->use_eewr = FALSE;
			eeprom->word_size = E1000_SHADOW_RAM_WORDS;
			/*
			 * Zero the shadow RAM structure. But don't load it
			 * from NVM so as to save time for driver init
			 */
			if (hw->eeprom_shadow_ram != NULL) {
				for (i = 0; i < E1000_SHADOW_RAM_WORDS; i++) {
					hw->eeprom_shadow_ram[i].modified = 
					    FALSE;
					hw->eeprom_shadow_ram[i].eeprom_word = 
					    0xFFFF;
				}
			}
			hw->flash_base_addr = 0;
			flash_size = ((flash_size >> 1) & 0x1f) + 1;
			flash_size *= 4096;
			hw->flash_bank_size = flash_size / 4;
		}
		break;
	default:
		break;
	}

	if (eeprom->type == em_eeprom_spi) {
		/*
		 * eeprom_size will be an enum [0..8] that maps to eeprom
		 * sizes 128B to 32KB (incremented by powers of 2).
		 */
		if (hw->mac_type <= em_82547_rev_2) {
			/* Set to default value for initial eeprom read. */
			eeprom->word_size = 64;
			ret_val = em_read_eeprom(hw, EEPROM_CFG, 1,
			    &eeprom_size);
			if (ret_val)
				return ret_val;
			eeprom_size = (eeprom_size & EEPROM_SIZE_MASK) >> 
			    EEPROM_SIZE_SHIFT;
			/*
			 * 256B eeprom size was not supported in earlier
			 * hardware, so we bump eeprom_size up one to ensure
			 * that "1" (which maps to 256B) is never the result
			 * used in the shifting logic below.
			 */
			if (eeprom_size)
				eeprom_size++;
		} else {
			eeprom_size = (uint16_t) (
			    (eecd & E1000_EECD_SIZE_EX_MASK) >>
			    E1000_EECD_SIZE_EX_SHIFT);
		}

		/* EEPROM access above 16k is unsupported */
		if (eeprom_size + EEPROM_WORD_SIZE_SHIFT >
		    EEPROM_WORD_SIZE_SHIFT_MAX) {
			eeprom->word_size = 1 << EEPROM_WORD_SIZE_SHIFT_MAX;
		} else {
			eeprom->word_size = 1 << 
			    (eeprom_size + EEPROM_WORD_SIZE_SHIFT);
		}
	}
	return ret_val;
}

/******************************************************************************
 * Raises the EEPROM's clock input.
 *
 * hw - Struct containing variables accessed by shared code
 * eecd - EECD's current value
 *****************************************************************************/
static void
em_raise_ee_clk(struct em_hw *hw, uint32_t *eecd)
{
	/*
	 * Raise the clock input to the EEPROM (by setting the SK bit), and
	 * then wait <delay> microseconds.
	 */
	*eecd = *eecd | E1000_EECD_SK;
	E1000_WRITE_REG(hw, EECD, *eecd);
	E1000_WRITE_FLUSH(hw);
	usec_delay(hw->eeprom.delay_usec);
}

/******************************************************************************
 * Lowers the EEPROM's clock input.
 *
 * hw - Struct containing variables accessed by shared code
 * eecd - EECD's current value
 *****************************************************************************/
static void
em_lower_ee_clk(struct em_hw *hw, uint32_t *eecd)
{
	/*
	 * Lower the clock input to the EEPROM (by clearing the SK bit), and
	 * then wait 50 microseconds.
	 */
	*eecd = *eecd & ~E1000_EECD_SK;
	E1000_WRITE_REG(hw, EECD, *eecd);
	E1000_WRITE_FLUSH(hw);
	usec_delay(hw->eeprom.delay_usec);
}

/******************************************************************************
 * Shift data bits out to the EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 * data - data to send to the EEPROM
 * count - number of bits to shift out
 *****************************************************************************/
static void
em_shift_out_ee_bits(struct em_hw *hw, uint16_t data, uint16_t count)
{
	struct em_eeprom_info *eeprom = &hw->eeprom;
	uint32_t eecd;
	uint32_t mask;
	/*
	 * We need to shift "count" bits out to the EEPROM. So, value in the
	 * "data" parameter will be shifted out to the EEPROM one bit at a
	 * time. In order to do this, "data" must be broken down into bits.
	 */
	mask = 0x01 << (count - 1);
	eecd = E1000_READ_REG(hw, EECD);
	if (eeprom->type == em_eeprom_microwire) {
		eecd &= ~E1000_EECD_DO;
	} else if (eeprom->type == em_eeprom_spi) {
		eecd |= E1000_EECD_DO;
	}
	do {
		/*
		 * A "1" is shifted out to the EEPROM by setting bit "DI" to
		 * a "1", and then raising and then lowering the clock (the
		 * SK bit controls the clock input to the EEPROM).  A "0" is
		 * shifted out to the EEPROM by setting "DI" to "0" and then
		 * raising and then lowering the clock.
		 */
		eecd &= ~E1000_EECD_DI;

		if (data & mask)
			eecd |= E1000_EECD_DI;

		E1000_WRITE_REG(hw, EECD, eecd);
		E1000_WRITE_FLUSH(hw);

		usec_delay(eeprom->delay_usec);

		em_raise_ee_clk(hw, &eecd);
		em_lower_ee_clk(hw, &eecd);

		mask = mask >> 1;

	} while (mask);

	/* We leave the "DI" bit set to "0" when we leave this routine. */
	eecd &= ~E1000_EECD_DI;
	E1000_WRITE_REG(hw, EECD, eecd);
}

/******************************************************************************
 * Shift data bits in from the EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static uint16_t
em_shift_in_ee_bits(struct em_hw *hw, uint16_t count)
{
	uint32_t eecd;
	uint32_t i;
	uint16_t data;
	/*
	 * In order to read a register from the EEPROM, we need to shift
	 * 'count' bits in from the EEPROM. Bits are "shifted in" by raising
	 * the clock input to the EEPROM (setting the SK bit), and then
	 * reading the value of the "DO" bit.  During this "shifting in"
	 * process the "DI" bit should always be clear.
	 */

	eecd = E1000_READ_REG(hw, EECD);

	eecd &= ~(E1000_EECD_DO | E1000_EECD_DI);
	data = 0;

	for (i = 0; i < count; i++) {
		data = data << 1;
		em_raise_ee_clk(hw, &eecd);

		eecd = E1000_READ_REG(hw, EECD);

		eecd &= ~(E1000_EECD_DI);
		if (eecd & E1000_EECD_DO)
			data |= 1;

		em_lower_ee_clk(hw, &eecd);
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
static int32_t
em_acquire_eeprom(struct em_hw *hw)
{
	struct em_eeprom_info *eeprom = &hw->eeprom;
	uint32_t eecd, i = 0;
	DEBUGFUNC("em_acquire_eeprom");

	if (em_swfw_sync_acquire(hw, E1000_SWFW_EEP_SM))
		return -E1000_ERR_SWFW_SYNC;
	eecd = E1000_READ_REG(hw, EECD);

	if ((hw->mac_type != em_82573) && (hw->mac_type != em_82574)) {
		/* Request EEPROM Access */
		if (hw->mac_type > em_82544) {
			eecd |= E1000_EECD_REQ;
			E1000_WRITE_REG(hw, EECD, eecd);
			eecd = E1000_READ_REG(hw, EECD);
			while ((!(eecd & E1000_EECD_GNT)) &&
			    (i < E1000_EEPROM_GRANT_ATTEMPTS)) {
				i++;
				usec_delay(5);
				eecd = E1000_READ_REG(hw, EECD);
			}
			if (!(eecd & E1000_EECD_GNT)) {
				eecd &= ~E1000_EECD_REQ;
				E1000_WRITE_REG(hw, EECD, eecd);
				DEBUGOUT("Could not acquire EEPROM grant\n");
				em_swfw_sync_release(hw, E1000_SWFW_EEP_SM);
				return -E1000_ERR_EEPROM;
			}
		}
	}

	/* Setup EEPROM for Read/Write */
	if (eeprom->type == em_eeprom_microwire) {
		/* Clear SK and DI */
		eecd &= ~(E1000_EECD_DI | E1000_EECD_SK);
		E1000_WRITE_REG(hw, EECD, eecd);

		/* Set CS */
		eecd |= E1000_EECD_CS;
		E1000_WRITE_REG(hw, EECD, eecd);
	} else if (eeprom->type == em_eeprom_spi) {
		/* Clear SK and CS */
		eecd &= ~(E1000_EECD_CS | E1000_EECD_SK);
		E1000_WRITE_REG(hw, EECD, eecd);
		usec_delay(1);
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 * Returns EEPROM to a "standby" state
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
em_standby_eeprom(struct em_hw *hw)
{
	struct em_eeprom_info *eeprom = &hw->eeprom;
	uint32_t eecd;
	eecd = E1000_READ_REG(hw, EECD);

	if (eeprom->type == em_eeprom_microwire) {
		eecd &= ~(E1000_EECD_CS | E1000_EECD_SK);
		E1000_WRITE_REG(hw, EECD, eecd);
		E1000_WRITE_FLUSH(hw);
		usec_delay(eeprom->delay_usec);

		/* Clock high */
		eecd |= E1000_EECD_SK;
		E1000_WRITE_REG(hw, EECD, eecd);
		E1000_WRITE_FLUSH(hw);
		usec_delay(eeprom->delay_usec);

		/* Select EEPROM */
		eecd |= E1000_EECD_CS;
		E1000_WRITE_REG(hw, EECD, eecd);
		E1000_WRITE_FLUSH(hw);
		usec_delay(eeprom->delay_usec);

		/* Clock low */
		eecd &= ~E1000_EECD_SK;
		E1000_WRITE_REG(hw, EECD, eecd);
		E1000_WRITE_FLUSH(hw);
		usec_delay(eeprom->delay_usec);
	} else if (eeprom->type == em_eeprom_spi) {
		/* Toggle CS to flush commands */
		eecd |= E1000_EECD_CS;
		E1000_WRITE_REG(hw, EECD, eecd);
		E1000_WRITE_FLUSH(hw);
		usec_delay(eeprom->delay_usec);
		eecd &= ~E1000_EECD_CS;
		E1000_WRITE_REG(hw, EECD, eecd);
		E1000_WRITE_FLUSH(hw);
		usec_delay(eeprom->delay_usec);
	}
}

/******************************************************************************
 * Terminates a command by inverting the EEPROM's chip select pin
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
em_release_eeprom(struct em_hw *hw)
{
	uint32_t eecd;
	DEBUGFUNC("em_release_eeprom");

	eecd = E1000_READ_REG(hw, EECD);

	if (hw->eeprom.type == em_eeprom_spi) {
		eecd |= E1000_EECD_CS;	/* Pull CS high */
		eecd &= ~E1000_EECD_SK;	/* Lower SCK */

		E1000_WRITE_REG(hw, EECD, eecd);

		usec_delay(hw->eeprom.delay_usec);
	} else if (hw->eeprom.type == em_eeprom_microwire) {
		/* cleanup eeprom */

		/* CS on Microwire is active-high */
		eecd &= ~(E1000_EECD_CS | E1000_EECD_DI);

		E1000_WRITE_REG(hw, EECD, eecd);

		/* Rising edge of clock */
		eecd |= E1000_EECD_SK;
		E1000_WRITE_REG(hw, EECD, eecd);
		E1000_WRITE_FLUSH(hw);
		usec_delay(hw->eeprom.delay_usec);

		/* Falling edge of clock */
		eecd &= ~E1000_EECD_SK;
		E1000_WRITE_REG(hw, EECD, eecd);
		E1000_WRITE_FLUSH(hw);
		usec_delay(hw->eeprom.delay_usec);
	}
	/* Stop requesting EEPROM access */
	if (hw->mac_type > em_82544) {
		eecd &= ~E1000_EECD_REQ;
		E1000_WRITE_REG(hw, EECD, eecd);
	}
	em_swfw_sync_release(hw, E1000_SWFW_EEP_SM);
}

/******************************************************************************
 * Reads a 16 bit word from the EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
STATIC int32_t
em_spi_eeprom_ready(struct em_hw *hw)
{
	uint16_t retry_count = 0;
	uint8_t  spi_stat_reg;
	DEBUGFUNC("em_spi_eeprom_ready");
	/*
	 * Read "Status Register" repeatedly until the LSB is cleared.  The
	 * EEPROM will signal that the command has been completed by clearing
	 * bit 0 of the internal status register.  If it's not cleared within
	 * 5 milliseconds, then error out.
	 */
	retry_count = 0;
	do {
		em_shift_out_ee_bits(hw, EEPROM_RDSR_OPCODE_SPI,
		    hw->eeprom.opcode_bits);
		spi_stat_reg = (uint8_t) em_shift_in_ee_bits(hw, 8);
		if (!(spi_stat_reg & EEPROM_STATUS_RDY_SPI))
			break;

		usec_delay(5);
		retry_count += 5;

		em_standby_eeprom(hw);
	} while (retry_count < EEPROM_MAX_RETRY_SPI);
	/*
	 * ATMEL SPI write time could vary from 0-20mSec on 3.3V devices (and
	 * only 0-5mSec on 5V devices)
	 */
	if (retry_count >= EEPROM_MAX_RETRY_SPI) {
		DEBUGOUT("SPI EEPROM Status error\n");
		return -E1000_ERR_EEPROM;
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 * Reads a 16 bit word from the EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset of  word in the EEPROM to read
 * data - word read from the EEPROM
 * words - number of words to read
 *****************************************************************************/
int32_t
em_read_eeprom(struct em_hw *hw, uint16_t offset, uint16_t words,
    uint16_t *data)
{
	struct em_eeprom_info *eeprom = &hw->eeprom;
	uint32_t i = 0;
	DEBUGFUNC("em_read_eeprom");

	/* If eeprom is not yet detected, do so now */
	if (eeprom->word_size == 0)
		em_init_eeprom_params(hw);
	/*
	 * A check for invalid values:  offset too large, too many words, and
	 * not enough words.
	 */
	if ((offset >= eeprom->word_size) || 
	    (words > eeprom->word_size - offset) ||
	    (words == 0)) {
		DEBUGOUT2("\"words\" parameter out of bounds. Words = %d,"
		    " size = %d\n", offset, eeprom->word_size);
		return -E1000_ERR_EEPROM;
	}
	/*
	 * EEPROM's that don't use EERD to read require us to bit-bang the
	 * SPI directly. In this case, we need to acquire the EEPROM so that
	 * FW or other port software does not interrupt.
	 */
	if (em_is_onboard_nvm_eeprom(hw) == TRUE &&
	    em_get_flash_presence_i210(hw) == TRUE &&
	    hw->eeprom.use_eerd == FALSE) {
		/* Prepare the EEPROM for bit-bang reading */
		if (em_acquire_eeprom(hw) != E1000_SUCCESS)
			return -E1000_ERR_EEPROM;
	}
	/* Eerd register EEPROM access requires no eeprom acquire/release */
	if (eeprom->use_eerd == TRUE)
		return em_read_eeprom_eerd(hw, offset, words, data);

	/* ICH EEPROM access is done via the ICH flash controller */
	if (eeprom->type == em_eeprom_ich8)
		return em_read_eeprom_ich8(hw, offset, words, data);

	/* Some i210/i211 have a special OTP chip */
	if (eeprom->type == em_eeprom_invm)
		return em_read_invm_i210(hw, offset, words, data);

	/*
	 * Set up the SPI or Microwire EEPROM for bit-bang reading.  We have
	 * acquired the EEPROM at this point, so any returns should release it
	 */
	if (eeprom->type == em_eeprom_spi) {
		uint16_t word_in;
		uint8_t  read_opcode = EEPROM_READ_OPCODE_SPI;
		if (em_spi_eeprom_ready(hw)) {
			em_release_eeprom(hw);
			return -E1000_ERR_EEPROM;
		}
		em_standby_eeprom(hw);
		/*
		 * Some SPI eeproms use the 8th address bit embedded in the
		 * opcode
		 */
		if ((eeprom->address_bits == 8) && (offset >= 128))
			read_opcode |= EEPROM_A8_OPCODE_SPI;

		/* Send the READ command (opcode + addr)  */
		em_shift_out_ee_bits(hw, read_opcode, eeprom->opcode_bits);
		em_shift_out_ee_bits(hw, (uint16_t) (offset * 2), 
		    eeprom->address_bits);
		/*
		 * Read the data.  The address of the eeprom internally
		 * increments with each byte (spi) being read, saving on the
		 * overhead of eeprom setup and tear-down.  The address
		 * counter will roll over if reading beyond the size of the
		 * eeprom, thus allowing the entire memory to be read
		 * starting from any offset.
		 */
		for (i = 0; i < words; i++) {
			word_in = em_shift_in_ee_bits(hw, 16);
			data[i] = (word_in >> 8) | (word_in << 8);
		}
	} else if (eeprom->type == em_eeprom_microwire) {
		for (i = 0; i < words; i++) {
			/* Send the READ command (opcode + addr)  */
			em_shift_out_ee_bits(hw, EEPROM_READ_OPCODE_MICROWIRE,
			    eeprom->opcode_bits);
			em_shift_out_ee_bits(hw, (uint16_t) (offset + i),
			    eeprom->address_bits);
			/*
			 * Read the data.  For microwire, each word requires
			 * the overhead of eeprom setup and tear-down.
			 */
			data[i] = em_shift_in_ee_bits(hw, 16);
			em_standby_eeprom(hw);
		}
	}
	/* End this read operation */
	em_release_eeprom(hw);

	return E1000_SUCCESS;
}

/******************************************************************************
 * Reads a 16 bit word from the EEPROM using the EERD register.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset of  word in the EEPROM to read
 * data - word read from the EEPROM
 * words - number of words to read
 *****************************************************************************/
STATIC int32_t
em_read_eeprom_eerd(struct em_hw *hw, uint16_t offset, uint16_t words,
    uint16_t *data)
{
	uint32_t i, eerd = 0;
	int32_t  error = 0;
	for (i = 0; i < words; i++) {
		eerd = ((offset + i) << E1000_EEPROM_RW_ADDR_SHIFT) +
		    E1000_EEPROM_RW_REG_START;

		E1000_WRITE_REG(hw, EERD, eerd);
		error = em_poll_eerd_eewr_done(hw, E1000_EEPROM_POLL_READ);

		if (error) {
			break;
		}
		data[i] = (E1000_READ_REG(hw, EERD) >> 
		    E1000_EEPROM_RW_REG_DATA);

	}

	return error;
}

/******************************************************************************
 * Writes a 16 bit word from the EEPROM using the EEWR register.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset of  word in the EEPROM to read
 * data - word read from the EEPROM
 * words - number of words to read
 *****************************************************************************/
STATIC int32_t
em_write_eeprom_eewr(struct em_hw *hw, uint16_t offset, uint16_t words,
    uint16_t *data)
{
	uint32_t register_value = 0;
	uint32_t i = 0;
	int32_t  error = 0;
	if (em_swfw_sync_acquire(hw, E1000_SWFW_EEP_SM))
		return -E1000_ERR_SWFW_SYNC;

	for (i = 0; i < words; i++) {
		register_value = (data[i] << E1000_EEPROM_RW_REG_DATA) |
		    ((offset + i) << E1000_EEPROM_RW_ADDR_SHIFT) |
		    E1000_EEPROM_RW_REG_START;

		error = em_poll_eerd_eewr_done(hw, E1000_EEPROM_POLL_WRITE);
		if (error) {
			break;
		}
		E1000_WRITE_REG(hw, EEWR, register_value);

		error = em_poll_eerd_eewr_done(hw, E1000_EEPROM_POLL_WRITE);

		if (error) {
			break;
		}
	}

	em_swfw_sync_release(hw, E1000_SWFW_EEP_SM);
	return error;
}

/******************************************************************************
 * Polls the status bit (bit 1) of the EERD to determine when the read is done.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
STATIC int32_t
em_poll_eerd_eewr_done(struct em_hw *hw, int eerd)
{
	uint32_t attempts = 100000;
	uint32_t i, reg = 0;
	int32_t  done = E1000_ERR_EEPROM;
	for (i = 0; i < attempts; i++) {
		if (eerd == E1000_EEPROM_POLL_READ)
			reg = E1000_READ_REG(hw, EERD);
		else
			reg = E1000_READ_REG(hw, EEWR);

		if (reg & E1000_EEPROM_RW_REG_DONE) {
			done = E1000_SUCCESS;
			break;
		}
		usec_delay(5);
	}

	return done;
}

/******************************************************************************
 * Description:     Determines if the onboard NVM is FLASH or EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
STATIC boolean_t
em_is_onboard_nvm_eeprom(struct em_hw *hw)
{
	uint32_t eecd = 0;
	DEBUGFUNC("em_is_onboard_nvm_eeprom");

	if (IS_ICH8(hw->mac_type))
		return FALSE;

	if ((hw->mac_type == em_82573) || (hw->mac_type == em_82574)) {
		eecd = E1000_READ_REG(hw, EECD);

		/* Isolate bits 15 & 16 */
		eecd = ((eecd >> 15) & 0x03);

		/* If both bits are set, device is Flash type */
		if (eecd == 0x03) {
			return FALSE;
		}
	}
	return TRUE;
}

/******************************************************************************
 * Check if flash device is detected.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
boolean_t
em_get_flash_presence_i210(struct em_hw *hw)
{
	uint32_t eecd;
	DEBUGFUNC("em_get_flash_presence_i210");

	if (hw->mac_type != em_i210)
		return TRUE;

	eecd = E1000_READ_REG(hw, EECD);

	if (eecd & E1000_EECD_FLUPD)
		return TRUE;

	return FALSE;
}

/******************************************************************************
 * Verifies that the EEPROM has a valid checksum
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Reads the first 64 16 bit words of the EEPROM and sums the values read.
 * If the sum of the 64 16 bit words is 0xBABA, the EEPROM's checksum is
 * valid.
 *****************************************************************************/
int32_t
em_validate_eeprom_checksum(struct em_hw *hw)
{
	uint16_t checksum = 0;
	uint16_t i, eeprom_data;
	uint16_t checksum_reg;
	DEBUGFUNC("em_validate_eeprom_checksum");

	checksum_reg = hw->mac_type != em_icp_xxxx ? 
	    EEPROM_CHECKSUM_REG : 
	    EEPROM_CHECKSUM_REG_ICP_xxxx;

	if (((hw->mac_type == em_82573) || (hw->mac_type == em_82574)) &&
	    (em_is_onboard_nvm_eeprom(hw) == FALSE)) {
		/*
		 * Check bit 4 of word 10h.  If it is 0, firmware is done
		 * updating 10h-12h.  Checksum may need to be fixed.
		 */
		em_read_eeprom(hw, 0x10, 1, &eeprom_data);
		if ((eeprom_data & 0x10) == 0) {
			/*
			 * Read 0x23 and check bit 15.  This bit is a 1 when
			 * the checksum has already been fixed.  If the
			 * checksum is still wrong and this bit is a 1, we
			 * need to return bad checksum.  Otherwise, we need
			 * to set this bit to a 1 and update the checksum.
			 */
			em_read_eeprom(hw, 0x23, 1, &eeprom_data);
			if ((eeprom_data & 0x8000) == 0) {
				eeprom_data |= 0x8000;
				em_write_eeprom(hw, 0x23, 1, &eeprom_data);
				em_update_eeprom_checksum(hw);
			}
		}
	}
	if (IS_ICH8(hw->mac_type)) {
		uint16_t word;
		uint16_t valid_csum_mask;
		
		/*
		 * Drivers must allocate the shadow ram structure for the
		 * EEPROM checksum to be updated.  Otherwise, this bit as
		 * well as the checksum must both be set correctly for this
		 * validation to pass.
		 */
		switch (hw->mac_type) {
		case em_pch_lpt:
		case em_pch_spt:
		case em_pch_cnp:
		case em_pch_tgp:
		case em_pch_adp:
			word = EEPROM_COMPAT;
			valid_csum_mask = EEPROM_COMPAT_VALID_CSUM;
			break;
		default:
			word = EEPROM_FUTURE_INIT_WORD1;
			valid_csum_mask = EEPROM_FUTURE_INIT_WORD1_VALID_CSUM;
			break;
		}
		em_read_eeprom(hw, word, 1, &eeprom_data);
		if ((eeprom_data & valid_csum_mask) == 0) {
			eeprom_data |= valid_csum_mask;
			em_write_eeprom(hw, word, 1, &eeprom_data);
			em_update_eeprom_checksum(hw);
		}
	}
	for (i = 0; i < (checksum_reg + 1); i++) {
		if (em_read_eeprom(hw, i, 1, &eeprom_data) < 0) {
			DEBUGOUT("EEPROM Read Error\n");
			return -E1000_ERR_EEPROM;
		}
		checksum += eeprom_data;
	}

	if (checksum == (uint16_t) EEPROM_SUM)
		return E1000_SUCCESS;
	else {
		DEBUGOUT("EEPROM Checksum Invalid\n");
		return -E1000_ERR_EEPROM;
	}
}

/******************************************************************************
 * Calculates the EEPROM checksum and writes it to the EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Sums the first 63 16 bit words of the EEPROM. Subtracts the sum from 0xBABA.
 * Writes the difference to word offset 63 of the EEPROM.
 *****************************************************************************/
int32_t
em_update_eeprom_checksum(struct em_hw *hw)
{
	uint32_t ctrl_ext;
	uint16_t checksum = 0;
	uint16_t i, eeprom_data;
	DEBUGFUNC("em_update_eeprom_checksum");

	for (i = 0; i < EEPROM_CHECKSUM_REG; i++) {
		if (em_read_eeprom(hw, i, 1, &eeprom_data) < 0) {
			DEBUGOUT("EEPROM Read Error\n");
			return -E1000_ERR_EEPROM;
		}
		checksum += eeprom_data;
	}
	checksum = (uint16_t) EEPROM_SUM - checksum;
	if (em_write_eeprom(hw, EEPROM_CHECKSUM_REG, 1, &checksum) < 0) {
		DEBUGOUT("EEPROM Write Error\n");
		return -E1000_ERR_EEPROM;
	} else if (hw->eeprom.type == em_eeprom_flash) {
		em_commit_shadow_ram(hw);
	} else if (hw->eeprom.type == em_eeprom_ich8) {
		em_commit_shadow_ram(hw);
		/*
		 * Reload the EEPROM, or else modifications will not appear
		 * until after next adapter reset.
		 */
		ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
		ctrl_ext |= E1000_CTRL_EXT_EE_RST;
		E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
		msec_delay(10);
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 * Parent function for writing words to the different EEPROM types.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset within the EEPROM to be written to
 * words - number of words to write
 * data - 16 bit word to be written to the EEPROM
 *
 * If em_update_eeprom_checksum is not called after this function, the
 * EEPROM will most likely contain an invalid checksum.
 *****************************************************************************/
int32_t
em_write_eeprom(struct em_hw *hw, uint16_t offset, uint16_t words,
    uint16_t *data)
{
	struct em_eeprom_info *eeprom = &hw->eeprom;
	int32_t status = 0;
	DEBUGFUNC("em_write_eeprom");

	/* If eeprom is not yet detected, do so now */
	if (eeprom->word_size == 0)
		em_init_eeprom_params(hw);
	/*
	 * A check for invalid values:  offset too large, too many words, and
	 * not enough words.
	 */
	if ((offset >= eeprom->word_size) ||
	    (words > eeprom->word_size - offset) ||
	    (words == 0)) {
		DEBUGOUT("\"words\" parameter out of bounds\n");
		return -E1000_ERR_EEPROM;
	}
	/* 82573/4 writes only through eewr */
	if (eeprom->use_eewr == TRUE)
		return em_write_eeprom_eewr(hw, offset, words, data);

	if (eeprom->type == em_eeprom_ich8)
		return em_write_eeprom_ich8(hw, offset, words, data);

	/* Prepare the EEPROM for writing  */
	if (em_acquire_eeprom(hw) != E1000_SUCCESS)
		return -E1000_ERR_EEPROM;

	if (eeprom->type == em_eeprom_microwire) {
		status = em_write_eeprom_microwire(hw, offset, words, data);
	} else {
		status = em_write_eeprom_spi(hw, offset, words, data);
		msec_delay(10);
	}

	/* Done with writing */
	em_release_eeprom(hw);

	return status;
}

/******************************************************************************
 * Writes a 16 bit word to a given offset in an SPI EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset within the EEPROM to be written to
 * words - number of words to write
 * data - pointer to array of 8 bit words to be written to the EEPROM
 *
 *****************************************************************************/
STATIC int32_t
em_write_eeprom_spi(struct em_hw *hw, uint16_t offset, uint16_t words,
    uint16_t *data)
{
	struct em_eeprom_info *eeprom = &hw->eeprom;
	uint16_t widx = 0;
	DEBUGFUNC("em_write_eeprom_spi");

	while (widx < words) {
		uint8_t write_opcode = EEPROM_WRITE_OPCODE_SPI;
		if (em_spi_eeprom_ready(hw))
			return -E1000_ERR_EEPROM;

		em_standby_eeprom(hw);

		/* Send the WRITE ENABLE command (8 bit opcode )  */
		em_shift_out_ee_bits(hw, EEPROM_WREN_OPCODE_SPI,
		    eeprom->opcode_bits);

		em_standby_eeprom(hw);
		/*
		 * Some SPI eeproms use the 8th address bit embedded in the
		 * opcode
		 */
		if ((eeprom->address_bits == 8) && (offset >= 128))
			write_opcode |= EEPROM_A8_OPCODE_SPI;

		/* Send the Write command (8-bit opcode + addr) */
		em_shift_out_ee_bits(hw, write_opcode, eeprom->opcode_bits);

		em_shift_out_ee_bits(hw, (uint16_t) ((offset + widx) * 2),
		    eeprom->address_bits);

		/* Send the data */
		/*
		 * Loop to allow for up to whole page write (32 bytes) of
		 * eeprom
		 */
		while (widx < words) {
			uint16_t word_out = data[widx];
			word_out = (word_out >> 8) | (word_out << 8);
			em_shift_out_ee_bits(hw, word_out, 16);
			widx++;
			/*
			 * Some larger eeprom sizes are capable of a 32-byte
			 * PAGE WRITE operation, while the smaller eeproms
			 * are capable of an 8-byte PAGE WRITE operation.
			 * Break the inner loop to pass new address
			 */
			if ((((offset + widx) * 2) % eeprom->page_size) == 0) {
				em_standby_eeprom(hw);
				break;
			}
		}
	}

	return E1000_SUCCESS;
}

/******************************************************************************
 * Writes a 16 bit word to a given offset in a Microwire EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset within the EEPROM to be written to
 * words - number of words to write
 * data - pointer to array of 16 bit words to be written to the EEPROM
 *
 *****************************************************************************/
STATIC int32_t
em_write_eeprom_microwire(struct em_hw *hw, uint16_t offset, uint16_t words,
    uint16_t *data)
{
	struct em_eeprom_info *eeprom = &hw->eeprom;
	uint32_t eecd;
	uint16_t words_written = 0;
	uint16_t i = 0;
	DEBUGFUNC("em_write_eeprom_microwire");
	/*
	 * Send the write enable command to the EEPROM (3-bit opcode plus
	 * 6/8-bit dummy address beginning with 11).  It's less work to
	 * include the 11 of the dummy address as part of the opcode than it
	 * is to shift it over the correct number of bits for the address.
	 * This puts the EEPROM into write/erase mode.
	 */
	em_shift_out_ee_bits(hw, EEPROM_EWEN_OPCODE_MICROWIRE,
	    (uint16_t) (eeprom->opcode_bits + 2));

	em_shift_out_ee_bits(hw, 0, (uint16_t) (eeprom->address_bits - 2));

	/* Prepare the EEPROM */
	em_standby_eeprom(hw);

	while (words_written < words) {
		/* Send the Write command (3-bit opcode + addr) */
		em_shift_out_ee_bits(hw, EEPROM_WRITE_OPCODE_MICROWIRE,
		    eeprom->opcode_bits);

		em_shift_out_ee_bits(hw, (uint16_t) (offset + words_written),
		    eeprom->address_bits);

		/* Send the data */
		em_shift_out_ee_bits(hw, data[words_written], 16);
		/*
		 * Toggle the CS line.  This in effect tells the EEPROM to
		 * execute the previous command.
		 */
		em_standby_eeprom(hw);
		/*
		 * Read DO repeatedly until it is high (equal to '1').  The
		 * EEPROM will signal that the command has been completed by
		 * raising the DO signal. If DO does not go high in 10
		 * milliseconds, then error out.
		 */
		for (i = 0; i < 200; i++) {
			eecd = E1000_READ_REG(hw, EECD);
			if (eecd & E1000_EECD_DO)
				break;
			usec_delay(50);
		}
		if (i == 200) {
			DEBUGOUT("EEPROM Write did not complete\n");
			return -E1000_ERR_EEPROM;
		}
		/* Recover from write */
		em_standby_eeprom(hw);

		words_written++;
	}
	/*
	 * Send the write disable command to the EEPROM (3-bit opcode plus
	 * 6/8-bit dummy address beginning with 10).  It's less work to
	 * include the 10 of the dummy address as part of the opcode than it
	 * is to shift it over the correct number of bits for the address.
	 * This takes the EEPROM out of write/erase mode.
	 */
	em_shift_out_ee_bits(hw, EEPROM_EWDS_OPCODE_MICROWIRE,
	    (uint16_t) (eeprom->opcode_bits + 2));

	em_shift_out_ee_bits(hw, 0, (uint16_t) (eeprom->address_bits - 2));

	return E1000_SUCCESS;
}

/******************************************************************************
 * Flushes the cached eeprom to NVM. This is done by saving the modified values
 * in the eeprom cache and the non modified values in the currently active bank
 * to the new bank.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset of  word in the EEPROM to read
 * data - word read from the EEPROM
 * words - number of words to read
 *****************************************************************************/
STATIC int32_t
em_commit_shadow_ram(struct em_hw *hw)
{
	uint32_t  attempts = 100000;
	uint32_t  eecd = 0;
	uint32_t  flop = 0;
	uint32_t  i = 0;
	int32_t   error = E1000_SUCCESS;
	uint32_t  old_bank_offset = 0;
	uint32_t  new_bank_offset = 0;
	uint8_t   low_byte = 0;
	uint8_t   high_byte = 0;
	boolean_t sector_write_failed = FALSE;
	if ((hw->mac_type == em_82573) || (hw->mac_type == em_82574)) {
		/*
		 * The flop register will be used to determine if flash type
		 * is STM
		 */
		flop = E1000_READ_REG(hw, FLOP);
		for (i = 0; i < attempts; i++) {
			eecd = E1000_READ_REG(hw, EECD);
			if ((eecd & E1000_EECD_FLUPD) == 0) {
				break;
			}
			usec_delay(5);
		}

		if (i == attempts) {
			return -E1000_ERR_EEPROM;
		}
		/* 
		 * If STM opcode located in bits 15:8 of flop, reset firmware
		 */
		if ((flop & 0xFF00) == E1000_STM_OPCODE) {
			E1000_WRITE_REG(hw, HICR, E1000_HICR_FW_RESET);
		}
		/* Perform the flash update */
		E1000_WRITE_REG(hw, EECD, eecd | E1000_EECD_FLUPD);

		for (i = 0; i < attempts; i++) {
			eecd = E1000_READ_REG(hw, EECD);
			if ((eecd & E1000_EECD_FLUPD) == 0) {
				break;
			}
			usec_delay(5);
		}

		if (i == attempts) {
			return -E1000_ERR_EEPROM;
		}
	}
	if ((hw->mac_type == em_ich8lan || hw->mac_type == em_ich9lan) &&
	    hw->eeprom_shadow_ram != NULL) {
		/*
		 * We're writing to the opposite bank so if we're on bank 1,
		 * write to bank 0 etc.  We also need to erase the segment
		 * that is going to be written
		 */
		if (!(E1000_READ_REG(hw, EECD) & E1000_EECD_SEC1VAL)) {
			new_bank_offset = hw->flash_bank_size * 2;
			old_bank_offset = 0;
			em_erase_ich8_4k_segment(hw, 1);
		} else {
			old_bank_offset = hw->flash_bank_size * 2;
			new_bank_offset = 0;
			em_erase_ich8_4k_segment(hw, 0);
		}

		sector_write_failed = FALSE;
		/*
		 * Loop for every byte in the shadow RAM, which is in units
		 * of words.
		 */
		for (i = 0; i < E1000_SHADOW_RAM_WORDS; i++) {
			/*
			 * Determine whether to write the value stored in the
			 * other NVM bank or a modified value stored in the
			 * shadow RAM
			 */
			if (hw->eeprom_shadow_ram[i].modified == TRUE) {
				low_byte = (uint8_t) 
				    hw->eeprom_shadow_ram[i].eeprom_word;
				usec_delay(100);
				error = em_verify_write_ich8_byte(hw,
				    (i << 1) + new_bank_offset, low_byte);

				if (error != E1000_SUCCESS)
					sector_write_failed = TRUE;
				else {
					high_byte = (uint8_t)
					    (hw->eeprom_shadow_ram
					    [i].eeprom_word >> 8);
					usec_delay(100);
				}
			} else {
				em_read_ich8_byte(hw, (i << 1) + 
				    old_bank_offset, &low_byte);
				usec_delay(100);
				error = em_verify_write_ich8_byte(hw,
				      (i << 1) + new_bank_offset, low_byte);

				if (error != E1000_SUCCESS)
					sector_write_failed = TRUE;
				else {
					em_read_ich8_byte(hw, (i << 1) +
					    old_bank_offset + 1, &high_byte);
					usec_delay(100);
				}
			}
			/*
			 * If the write of the low byte was successful, go
			 * ahread and write the high byte while checking to
			 * make sure that if it is the signature byte, then
			 * it is handled properly
			 */
			if (sector_write_failed == FALSE) {
				/*
				 * If the word is 0x13, then make sure the
				 * signature bits (15:14) are 11b until the
				 * commit has completed. This will allow us
				 * to write 10b which indicates the signature
				 * is valid.  We want to do this after the
				 * write has completed so that we don't mark
				 * the segment valid while the write is still
				 * in progress
				 */
				if (i == E1000_ICH_NVM_SIG_WORD)
					high_byte = E1000_ICH_NVM_VALID_SIG_MASK |
					    high_byte;

				error = em_verify_write_ich8_byte(hw, (i << 1)
				    + new_bank_offset + 1, high_byte);
				if (error != E1000_SUCCESS)
					sector_write_failed = TRUE;

			} else {
				/*
				 * If the write failed then break from the
				 * loop and return an error
				 */
				break;
			}
		}
		/*
		 * Don't bother writing the segment valid bits if sector
		 * programming failed.
		 */
		if (sector_write_failed == FALSE) {
			/*
			 * Finally validate the new segment by setting bit
			 * 15:14 to 10b in word 0x13 , this can be done
			 * without an erase as well since these bits are 11
			 * to start with and we need to change bit 14 to 0b
			 */
			em_read_ich8_byte(hw, E1000_ICH_NVM_SIG_WORD * 2 + 1 +
			    new_bank_offset, &high_byte);
			high_byte &= 0xBF;
			error = em_verify_write_ich8_byte(hw,
			    E1000_ICH_NVM_SIG_WORD * 2 + 1 + new_bank_offset,
			    high_byte);
			/*
			 * And invalidate the previously valid segment by
			 * setting its signature word (0x13) high_byte to 0b.
			 * This can be done without an erase because flash
			 * erase sets all bits to 1's. We can write 1's to
			 * 0's without an erase
			 */
			if (error == E1000_SUCCESS) {
				error = em_verify_write_ich8_byte(hw,
				    E1000_ICH_NVM_SIG_WORD * 2 + 1 +
				    old_bank_offset, 0);
			}
			/* Clear the now not used entry in the cache */
			for (i = 0; i < E1000_SHADOW_RAM_WORDS; i++) {
				hw->eeprom_shadow_ram[i].modified = FALSE;
				hw->eeprom_shadow_ram[i].eeprom_word = 0xFFFF;
			}
		}
	}
	return error;
}

/******************************************************************************
 * Reads the adapter's part number from the EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 * part_num - Adapter's part number
 *****************************************************************************/
int32_t
em_read_part_num(struct em_hw *hw, uint32_t *part_num)
{
	uint16_t offset = EEPROM_PBA_BYTE_1;
	uint16_t eeprom_data;
	DEBUGFUNC("em_read_part_num");

	/* Get word 0 from EEPROM */
	if (em_read_eeprom(hw, offset, 1, &eeprom_data) < 0) {
		DEBUGOUT("EEPROM Read Error\n");
		return -E1000_ERR_EEPROM;
	}
	/* Save word 0 in upper half of part_num */
	*part_num = (uint32_t)eeprom_data << 16;

	/* Get word 1 from EEPROM */
	if (em_read_eeprom(hw, ++offset, 1, &eeprom_data) < 0) {
		DEBUGOUT("EEPROM Read Error\n");
		return -E1000_ERR_EEPROM;
	}
	/* Save word 1 in lower half of part_num */
	*part_num |= eeprom_data;

	return E1000_SUCCESS;
}

/******************************************************************************
 * Reads the adapter's MAC address from the EEPROM and inverts the LSB for the
 * second function of dual function devices
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
int32_t
em_read_mac_addr(struct em_hw *hw)
{
	uint16_t offset;
	uint16_t eeprom_data, i;
	uint16_t ia_base_addr = 0;
	DEBUGFUNC("em_read_mac_addr");

	if (hw->mac_type == em_icp_xxxx) {
		ia_base_addr = (uint16_t)
		EEPROM_IA_START_ICP_xxxx(hw->icp_xxxx_port_num);
	} else if (hw->mac_type == em_82580 || hw->mac_type == em_i350) {
		ia_base_addr = NVM_82580_LAN_FUNC_OFFSET(hw->bus_func);
	}
	for (i = 0; i < NODE_ADDRESS_SIZE; i += 2) {
		offset = i >> 1;
		if (em_read_eeprom(hw, offset + ia_base_addr, 1, &eeprom_data)
		    < 0) {
			DEBUGOUT("EEPROM Read Error\n");
			return -E1000_ERR_EEPROM;
		}
		hw->perm_mac_addr[i] = (uint8_t) (eeprom_data & 0x00FF);
		hw->perm_mac_addr[i + 1] = (uint8_t) (eeprom_data >> 8);
	}

	switch (hw->mac_type) {
	default:
		break;
	case em_82546:
	case em_82546_rev_3:
	case em_82571:
	case em_82575:
	case em_82576:
	case em_80003es2lan:
		if (E1000_READ_REG(hw, STATUS) & E1000_STATUS_FUNC_1)
			hw->perm_mac_addr[5] ^= 0x01;
		break;
	}

	for (i = 0; i < NODE_ADDRESS_SIZE; i++)
		hw->mac_addr[i] = hw->perm_mac_addr[i];
	return E1000_SUCCESS;
}

/******************************************************************************
 * Explicitly disables jumbo frames and resets some PHY registers back to hw-
 * defaults. This is necessary in case the ethernet cable was inserted AFTER
 * the firmware initialized the PHY. Otherwise it is left in a state where
 * it is possible to transmit but not receive packets. Observed on I217-LM and
 * fixed in FreeBSD's sys/dev/e1000/e1000_ich8lan.c.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
STATIC int32_t
em_phy_no_cable_workaround(struct em_hw *hw) {
	int32_t ret_val, dft_ret_val;
	uint32_t mac_reg;
	uint16_t data, phy_reg;

	/* disable Rx path while enabling workaround */
	em_read_phy_reg(hw, I2_DFT_CTRL, &phy_reg);
	ret_val = em_write_phy_reg(hw, I2_DFT_CTRL, phy_reg | (1 << 14));
	if (ret_val)
		return ret_val;

	/* Write MAC register values back to h/w defaults */
	mac_reg = E1000_READ_REG(hw, FFLT_DBG);
	mac_reg &= ~(0xF << 14);
	E1000_WRITE_REG(hw, FFLT_DBG, mac_reg);

	mac_reg = E1000_READ_REG(hw, RCTL);
	mac_reg &= ~E1000_RCTL_SECRC;
	E1000_WRITE_REG(hw, RCTL, mac_reg);

	ret_val = em_read_kmrn_reg(hw, E1000_KUMCTRLSTA_OFFSET_CTRL, &data);
	if (ret_val)
		goto out;
	ret_val = em_write_kmrn_reg(hw, E1000_KUMCTRLSTA_OFFSET_CTRL,
	    data & ~(1 << 0));
	if (ret_val)
		goto out;

	ret_val = em_read_kmrn_reg(hw, E1000_KUMCTRLSTA_OFFSET_HD_CTRL, &data);
	if (ret_val)
		goto out;

	data &= ~(0xF << 8);
	data |= (0xB << 8);
	ret_val = em_write_kmrn_reg(hw, E1000_KUMCTRLSTA_OFFSET_HD_CTRL, data);
	if (ret_val)
		goto out;

	/* Write PHY register values back to h/w defaults */
	em_read_phy_reg(hw, I2_SMBUS_CTRL, &data);
	data &= ~(0x7F << 5);
	ret_val = em_write_phy_reg(hw, I2_SMBUS_CTRL, data);
	if (ret_val)
		goto out;

	em_read_phy_reg(hw, I2_MODE_CTRL, &data);
	data |= (1 << 13);
	ret_val = em_write_phy_reg(hw, I2_MODE_CTRL, data);
	if (ret_val)
		goto out;

	/*
	 * 776.20 and 776.23 are not documented in
	 * i217-ethernet-controller-datasheet.pdf...
	 */
	em_read_phy_reg(hw, PHY_REG(776, 20), &data);
	data &= ~(0x3FF << 2);
	data |= (0x8 << 2);
	ret_val = em_write_phy_reg(hw, PHY_REG(776, 20), data);
	if (ret_val)
		goto out;

	ret_val = em_write_phy_reg(hw, PHY_REG(776, 23), 0x7E00);
	if (ret_val)
		goto out;

	em_read_phy_reg(hw, I2_PCIE_POWER_CTRL, &data);
	ret_val = em_write_phy_reg(hw, I2_PCIE_POWER_CTRL, data & ~(1 << 10));
	if (ret_val)
		goto out;

out:
	/* re-enable Rx path after enabling workaround */
	dft_ret_val = em_write_phy_reg(hw, I2_DFT_CTRL, phy_reg & ~(1 << 14));
	if (ret_val)
		return ret_val;
	else
		return dft_ret_val;
}

/******************************************************************************
 * Initializes receive address filters.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Places the MAC address in receive address register 0 and clears the rest
 * of the receive address registers. Clears the multicast table. Assumes
 * the receiver is in reset when the routine is called.
 *****************************************************************************/
STATIC void
em_init_rx_addrs(struct em_hw *hw)
{
	uint32_t i;
	uint32_t rar_num;
	DEBUGFUNC("em_init_rx_addrs");

	if (hw->mac_type == em_pch_lpt || hw->mac_type == em_pch_spt ||
	    hw->mac_type == em_pch_cnp || hw->mac_type == em_pch_tgp ||
	    hw->mac_type == em_pch_adp || hw->mac_type == em_pch2lan)
		if (em_phy_no_cable_workaround(hw))
			printf(" ...failed to apply em_phy_no_cable_"
			    "workaround.\n");

	/* Setup the receive address. */
	DEBUGOUT("Programming MAC Address into RAR[0]\n");

	em_rar_set(hw, hw->mac_addr, 0);

	rar_num = E1000_RAR_ENTRIES;
	/*
	 * Reserve a spot for the Locally Administered Address to work around
	 * an 82571 issue in which a reset on one port will reload the MAC on
	 * the other port.
	 */
	if ((hw->mac_type == em_82571) && (hw->laa_is_present == TRUE))
		rar_num -= 1;
	if (IS_ICH8(hw->mac_type))
		rar_num = E1000_RAR_ENTRIES_ICH8LAN;
	if (hw->mac_type == em_ich8lan)
		rar_num -= 1;
	if (hw->mac_type == em_82580)
		rar_num = E1000_RAR_ENTRIES_82580;
	if (hw->mac_type == em_i210)
		rar_num = E1000_RAR_ENTRIES_82575;
	if (hw->mac_type == em_i350)
		rar_num = E1000_RAR_ENTRIES_I350;

	/* Zero out the other 15 receive addresses. */
	DEBUGOUT("Clearing RAR[1-15]\n");
	for (i = 1; i < rar_num; i++) {
		E1000_WRITE_REG_ARRAY(hw, RA, (i << 1), 0);
		E1000_WRITE_FLUSH(hw);
		E1000_WRITE_REG_ARRAY(hw, RA, ((i << 1) + 1), 0);
		E1000_WRITE_FLUSH(hw);
	}
}

/******************************************************************************
 * Updates the MAC's list of multicast addresses.
 *
 * hw - Struct containing variables accessed by shared code
 * mc_addr_list - the list of new multicast addresses
 * mc_addr_count - number of addresses
 * pad - number of bytes between addresses in the list
 *
 * The given list replaces any existing list and hashes the addresses into the
 * multicast table.
 *****************************************************************************/
void
em_mc_addr_list_update(struct em_hw *hw, uint8_t *mc_addr_list,
    uint32_t mc_addr_count, uint32_t pad)
{
	uint32_t hash_value;
	uint32_t i;
	uint32_t num_mta_entry;
	DEBUGFUNC("em_mc_addr_list_update");
	/*
	 * Set the new number of MC addresses that we are being requested to
	 * use.
	 */
	hw->num_mc_addrs = mc_addr_count;

	/* Clear the MTA */
	DEBUGOUT(" Clearing MTA\n");
	num_mta_entry = E1000_NUM_MTA_REGISTERS;
	if (IS_ICH8(hw->mac_type))
		num_mta_entry = E1000_NUM_MTA_REGISTERS_ICH8LAN;

	for (i = 0; i < num_mta_entry; i++) {
		E1000_WRITE_REG_ARRAY(hw, MTA, i, 0);
		E1000_WRITE_FLUSH(hw);
	}

	/* Add the new addresses */
	for (i = 0; i < mc_addr_count; i++) {
		DEBUGOUT(" Adding the multicast addresses:\n");
		DEBUGOUT7(" MC Addr #%d =%.2X %.2X %.2X %.2X %.2X %.2X\n", i,
		    mc_addr_list[i * (ETH_LENGTH_OF_ADDRESS + pad)],
		    mc_addr_list[i * (ETH_LENGTH_OF_ADDRESS + pad) + 1],
		    mc_addr_list[i * (ETH_LENGTH_OF_ADDRESS + pad) + 2],
		    mc_addr_list[i * (ETH_LENGTH_OF_ADDRESS + pad) + 3],
		    mc_addr_list[i * (ETH_LENGTH_OF_ADDRESS + pad) + 4],
		    mc_addr_list[i * (ETH_LENGTH_OF_ADDRESS + pad) + 5]);

		hash_value = em_hash_mc_addr(hw, mc_addr_list +
		    (i * (ETH_LENGTH_OF_ADDRESS + pad)));

		DEBUGOUT1(" Hash value = 0x%03X\n", hash_value);
		em_mta_set(hw, hash_value);
	}
	DEBUGOUT("MC Update Complete\n");
}

/******************************************************************************
 * Hashes an address to determine its location in the multicast table
 *
 * hw - Struct containing variables accessed by shared code
 * mc_addr - the multicast address to hash
 *****************************************************************************/
uint32_t
em_hash_mc_addr(struct em_hw *hw, uint8_t *mc_addr)
{
	uint32_t hash_value = 0;
	/*
	 * The portion of the address that is used for the hash table is
	 * determined by the mc_filter_type setting.
	 */
	switch (hw->mc_filter_type) {
		/*
		 * [0] [1] [2] [3] [4] [5] 01  AA  00  12  34  56 LSB
		 * MSB
		 */
	case 0:
		if (IS_ICH8(hw->mac_type)) {
			/* [47:38] i.e. 0x158 for above example address */
			hash_value = ((mc_addr[4] >> 6) | 
			    (((uint16_t) mc_addr[5]) << 2));
		} else {
			/* [47:36] i.e. 0x563 for above example address */
			hash_value = ((mc_addr[4] >> 4) | 
			    (((uint16_t) mc_addr[5]) << 4));
		}
		break;
	case 1:
		if (IS_ICH8(hw->mac_type)) {
			/* [46:37] i.e. 0x2B1 for above example address */
			hash_value = ((mc_addr[4] >> 5) |
			    (((uint16_t) mc_addr[5]) << 3));
		} else {
			/* [46:35] i.e. 0xAC6 for above example address */
			hash_value = ((mc_addr[4] >> 3) |
			    (((uint16_t) mc_addr[5]) << 5));
		}
		break;
	case 2:
		if (IS_ICH8(hw->mac_type)) {
			/* [45:36] i.e. 0x163 for above example address */
			hash_value = ((mc_addr[4] >> 4) |
			    (((uint16_t) mc_addr[5]) << 4));
		} else {
			/* [45:34] i.e. 0x5D8 for above example address */
			hash_value = ((mc_addr[4] >> 2) |
			    (((uint16_t) mc_addr[5]) << 6));
		}
		break;
	case 3:
		if (IS_ICH8(hw->mac_type)) {
			/* [43:34] i.e. 0x18D for above example address */
			hash_value = ((mc_addr[4] >> 2) |
			    (((uint16_t) mc_addr[5]) << 6));
		} else {
			/* [43:32] i.e. 0x634 for above example address */
			hash_value = ((mc_addr[4]) |
			    (((uint16_t) mc_addr[5]) << 8));
		}
		break;
	}

	hash_value &= 0xFFF;
	if (IS_ICH8(hw->mac_type))
		hash_value &= 0x3FF;

	return hash_value;
}

/******************************************************************************
 * Sets the bit in the multicast table corresponding to the hash value.
 *
 * hw - Struct containing variables accessed by shared code
 * hash_value - Multicast address hash value
 *****************************************************************************/
void
em_mta_set(struct em_hw *hw, uint32_t hash_value)
{
	uint32_t hash_bit, hash_reg;
	uint32_t mta;
	uint32_t temp;
	/*
	 * The MTA is a register array of 128 32-bit registers. It is treated
	 * like an array of 4096 bits.  We want to set bit
	 * BitArray[hash_value]. So we figure out what register the bit is
	 * in, read it, OR in the new bit, then write back the new value.
	 * The register is determined by the upper 7 bits of the hash value
	 * and the bit within that register are determined by the lower 5
	 * bits of the value.
	 */
	hash_reg = (hash_value >> 5) & 0x7F;
	if (IS_ICH8(hw->mac_type))
		hash_reg &= 0x1F;

	hash_bit = hash_value & 0x1F;

	mta = E1000_READ_REG_ARRAY(hw, MTA, hash_reg);

	mta |= (1 << hash_bit);
	/*
	 * If we are on an 82544 and we are trying to write an odd offset in
	 * the MTA, save off the previous entry before writing and restore
	 * the old value after writing.
	 */
	if ((hw->mac_type == em_82544) && ((hash_reg & 0x1) == 1)) {
		temp = E1000_READ_REG_ARRAY(hw, MTA, (hash_reg - 1));
		E1000_WRITE_REG_ARRAY(hw, MTA, hash_reg, mta);
		E1000_WRITE_FLUSH(hw);
		E1000_WRITE_REG_ARRAY(hw, MTA, (hash_reg - 1), temp);
		E1000_WRITE_FLUSH(hw);
	} else {
		E1000_WRITE_REG_ARRAY(hw, MTA, hash_reg, mta);
		E1000_WRITE_FLUSH(hw);
	}
}

/******************************************************************************
 * Puts an ethernet address into a receive address register.
 *
 * hw - Struct containing variables accessed by shared code
 * addr - Address to put into receive address register
 * index - Receive address register to write
 *****************************************************************************/
void
em_rar_set(struct em_hw *hw, uint8_t *addr, uint32_t index)
{
	uint32_t rar_low, rar_high;
	/*
	 * HW expects these in little endian so we reverse the byte order
	 * from network order (big endian) to little endian
	 */
	rar_low = ((uint32_t) addr[0] | ((uint32_t) addr[1] << 8) |
	    ((uint32_t) addr[2] << 16) | ((uint32_t) addr[3] << 24));
	rar_high = ((uint32_t) addr[4] | ((uint32_t) addr[5] << 8));
	/*
	 * Disable Rx and flush all Rx frames before enabling RSS to avoid Rx
	 * unit hang.
	 *
	 * Description: If there are any Rx frames queued up or otherwise
	 * present in the HW before RSS is enabled, and then we enable RSS,
	 * the HW Rx unit will hang.  To work around this issue, we have to
	 * disable receives and flush out all Rx frames before we enable RSS.
	 * To do so, we modify we redirect all Rx traffic to manageability
	 * and then reset the HW. This flushes away Rx frames, and (since the
	 * redirections to manageability persists across resets) keeps new
	 * ones from coming in while we work.  Then, we clear the Address
	 * Valid AV bit for all MAC addresses and undo the re-direction to
	 * manageability. Now, frames are coming in again, but the MAC won't
	 * accept them, so far so good.  We now proceed to initialize RSS (if
	 * necessary) and configure the Rx unit.  Last, we re-enable the AV
	 * bits and continue on our merry way.
	 */
	switch (hw->mac_type) {
	case em_82571:
	case em_82572:
	case em_80003es2lan:
		if (hw->leave_av_bit_off == TRUE)
			break;
	default:
		/* Indicate to hardware the Address is Valid. */
		rar_high |= E1000_RAH_AV;
		break;
	}

	E1000_WRITE_REG_ARRAY(hw, RA, (index << 1), rar_low);
	E1000_WRITE_FLUSH(hw);
	E1000_WRITE_REG_ARRAY(hw, RA, ((index << 1) + 1), rar_high);
	E1000_WRITE_FLUSH(hw);
}

/******************************************************************************
 * Clears the VLAN filer table
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
STATIC void
em_clear_vfta(struct em_hw *hw)
{
	uint32_t offset;
	uint32_t vfta_value = 0;
	uint32_t vfta_offset = 0;
	uint32_t vfta_bit_in_reg = 0;
	if (IS_ICH8(hw->mac_type))
		return;

	if ((hw->mac_type == em_82573) || (hw->mac_type == em_82574)) {
		if (hw->mng_cookie.vlan_id != 0) {
			/*
			 * The VFTA is a 4096b bit-field, each identifying a
			 * single VLAN ID.  The following operations
			 * determine which 32b entry (i.e. offset) into the
			 * array we want to set the VLAN ID (i.e. bit) of the
			 * manageability unit.
			 */
			vfta_offset = (hw->mng_cookie.vlan_id >>
			    E1000_VFTA_ENTRY_SHIFT) & E1000_VFTA_ENTRY_MASK;

			vfta_bit_in_reg = 1 << (hw->mng_cookie.vlan_id &
			    E1000_VFTA_ENTRY_BIT_SHIFT_MASK);
		}
	}
	for (offset = 0; offset < E1000_VLAN_FILTER_TBL_SIZE; offset++) {
		/*
		 * If the offset we want to clear is the same offset of the
		 * manageability VLAN ID, then clear all bits except that of
		 * the manageability unit
		 */
		vfta_value = (offset == vfta_offset) ? vfta_bit_in_reg : 0;
		E1000_WRITE_REG_ARRAY(hw, VFTA, offset, vfta_value);
		E1000_WRITE_FLUSH(hw);
	}
}

/*
 * Due to hw errata, if the host tries to configure the VFTA register
 * while performing queries from the BMC or DMA, then the VFTA in some
 * cases won't be written.
 */
void
em_clear_vfta_i350(struct em_hw *hw)
{
	uint32_t offset;
	int i;
	
	for (offset = 0; offset < E1000_VLAN_FILTER_TBL_SIZE; offset++) {
		for (i = 0; i < 10; i++)
			E1000_WRITE_REG_ARRAY(hw, VFTA, offset, 0);
		E1000_WRITE_FLUSH(hw);
	}
}

STATIC int32_t
em_id_led_init(struct em_hw *hw)
{
	uint32_t       ledctl;
	const uint32_t ledctl_mask = 0x000000FF;
	const uint32_t ledctl_on = E1000_LEDCTL_MODE_LED_ON;
	const uint32_t ledctl_off = E1000_LEDCTL_MODE_LED_OFF;
	uint16_t       eeprom_data, i, temp;
	const uint16_t led_mask = 0x0F;
	DEBUGFUNC("em_id_led_init");

	if (hw->mac_type < em_82540 || hw->mac_type == em_icp_xxxx) {
		/* Nothing to do */
		return E1000_SUCCESS;
	}
	ledctl = E1000_READ_REG(hw, LEDCTL);
	hw->ledctl_default = ledctl;
	hw->ledctl_mode1 = hw->ledctl_default;
	hw->ledctl_mode2 = hw->ledctl_default;

	if (em_read_eeprom(hw, EEPROM_ID_LED_SETTINGS, 1, &eeprom_data) < 0) {
		DEBUGOUT("EEPROM Read Error\n");
		return -E1000_ERR_EEPROM;
	}
	if ((hw->mac_type == em_82573) &&
	    (eeprom_data == ID_LED_RESERVED_82573))
		eeprom_data = ID_LED_DEFAULT_82573;
	else if ((eeprom_data == ID_LED_RESERVED_0000) ||
	    (eeprom_data == ID_LED_RESERVED_FFFF)) {
		if (hw->mac_type == em_ich8lan ||
		    hw->mac_type == em_ich9lan ||
		    hw->mac_type == em_ich10lan) // XXX
			eeprom_data = ID_LED_DEFAULT_ICH8LAN;
		else
			eeprom_data = ID_LED_DEFAULT;
	}
	for (i = 0; i < 4; i++) {
		temp = (eeprom_data >> (i << 2)) & led_mask;
		switch (temp) {
		case ID_LED_ON1_DEF2:
		case ID_LED_ON1_ON2:
		case ID_LED_ON1_OFF2:
			hw->ledctl_mode1 &= ~(ledctl_mask << (i << 3));
			hw->ledctl_mode1 |= ledctl_on << (i << 3);
			break;
		case ID_LED_OFF1_DEF2:
		case ID_LED_OFF1_ON2:
		case ID_LED_OFF1_OFF2:
			hw->ledctl_mode1 &= ~(ledctl_mask << (i << 3));
			hw->ledctl_mode1 |= ledctl_off << (i << 3);
			break;
		default:
			/* Do nothing */
			break;
		}
		switch (temp) {
		case ID_LED_DEF1_ON2:
		case ID_LED_ON1_ON2:
		case ID_LED_OFF1_ON2:
			hw->ledctl_mode2 &= ~(ledctl_mask << (i << 3));
			hw->ledctl_mode2 |= ledctl_on << (i << 3);
			break;
		case ID_LED_DEF1_OFF2:
		case ID_LED_ON1_OFF2:
		case ID_LED_OFF1_OFF2:
			hw->ledctl_mode2 &= ~(ledctl_mask << (i << 3));
			hw->ledctl_mode2 |= ledctl_off << (i << 3);
			break;
		default:
			/* Do nothing */
			break;
		}
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 * Clears all hardware statistics counters.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
void
em_clear_hw_cntrs(struct em_hw *hw)
{
	volatile uint32_t temp;
	temp = E1000_READ_REG(hw, CRCERRS);
	temp = E1000_READ_REG(hw, SYMERRS);
	temp = E1000_READ_REG(hw, MPC);
	temp = E1000_READ_REG(hw, SCC);
	temp = E1000_READ_REG(hw, ECOL);
	temp = E1000_READ_REG(hw, MCC);
	temp = E1000_READ_REG(hw, LATECOL);
	temp = E1000_READ_REG(hw, COLC);
	temp = E1000_READ_REG(hw, DC);
	temp = E1000_READ_REG(hw, SEC);
	temp = E1000_READ_REG(hw, RLEC);
	temp = E1000_READ_REG(hw, XONRXC);
	temp = E1000_READ_REG(hw, XONTXC);
	temp = E1000_READ_REG(hw, XOFFRXC);
	temp = E1000_READ_REG(hw, XOFFTXC);
	temp = E1000_READ_REG(hw, FCRUC);

	if (!IS_ICH8(hw->mac_type)) {
		temp = E1000_READ_REG(hw, PRC64);
		temp = E1000_READ_REG(hw, PRC127);
		temp = E1000_READ_REG(hw, PRC255);
		temp = E1000_READ_REG(hw, PRC511);
		temp = E1000_READ_REG(hw, PRC1023);
		temp = E1000_READ_REG(hw, PRC1522);
	}
	temp = E1000_READ_REG(hw, GPRC);
	temp = E1000_READ_REG(hw, BPRC);
	temp = E1000_READ_REG(hw, MPRC);
	temp = E1000_READ_REG(hw, GPTC);
	temp = E1000_READ_REG(hw, GORCL);
	temp = E1000_READ_REG(hw, GORCH);
	temp = E1000_READ_REG(hw, GOTCL);
	temp = E1000_READ_REG(hw, GOTCH);
	temp = E1000_READ_REG(hw, RNBC);
	temp = E1000_READ_REG(hw, RUC);
	temp = E1000_READ_REG(hw, RFC);
	temp = E1000_READ_REG(hw, ROC);
	temp = E1000_READ_REG(hw, RJC);
	temp = E1000_READ_REG(hw, TORL);
	temp = E1000_READ_REG(hw, TORH);
	temp = E1000_READ_REG(hw, TOTL);
	temp = E1000_READ_REG(hw, TOTH);
	temp = E1000_READ_REG(hw, TPR);
	temp = E1000_READ_REG(hw, TPT);

	if (!IS_ICH8(hw->mac_type)) {
		temp = E1000_READ_REG(hw, PTC64);
		temp = E1000_READ_REG(hw, PTC127);
		temp = E1000_READ_REG(hw, PTC255);
		temp = E1000_READ_REG(hw, PTC511);
		temp = E1000_READ_REG(hw, PTC1023);
		temp = E1000_READ_REG(hw, PTC1522);
	}
	temp = E1000_READ_REG(hw, MPTC);
	temp = E1000_READ_REG(hw, BPTC);

	if (hw->mac_type < em_82543)
		return;

	temp = E1000_READ_REG(hw, ALGNERRC);
	temp = E1000_READ_REG(hw, RXERRC);
	temp = E1000_READ_REG(hw, TNCRS);
	temp = E1000_READ_REG(hw, CEXTERR);
	temp = E1000_READ_REG(hw, TSCTC);
	temp = E1000_READ_REG(hw, TSCTFC);

	if (hw->mac_type <= em_82544
	    || hw->mac_type == em_icp_xxxx)
		return;

	temp = E1000_READ_REG(hw, MGTPRC);
	temp = E1000_READ_REG(hw, MGTPDC);
	temp = E1000_READ_REG(hw, MGTPTC);

	if (hw->mac_type <= em_82547_rev_2)
		return;

	temp = E1000_READ_REG(hw, IAC);
	temp = E1000_READ_REG(hw, ICRXOC);

	if (hw->phy_type == em_phy_82577 ||
	    hw->phy_type == em_phy_82578 ||
	    hw->phy_type == em_phy_82579 ||
	    hw->phy_type == em_phy_i217) {
		uint16_t phy_data;

		em_read_phy_reg(hw, HV_SCC_UPPER, &phy_data);
		em_read_phy_reg(hw, HV_SCC_LOWER, &phy_data);
		em_read_phy_reg(hw, HV_ECOL_UPPER, &phy_data);
		em_read_phy_reg(hw, HV_ECOL_LOWER, &phy_data);
		em_read_phy_reg(hw, HV_MCC_UPPER, &phy_data);
		em_read_phy_reg(hw, HV_MCC_LOWER, &phy_data);
		em_read_phy_reg(hw, HV_LATECOL_UPPER, &phy_data);
		em_read_phy_reg(hw, HV_LATECOL_LOWER, &phy_data);
		em_read_phy_reg(hw, HV_COLC_UPPER, &phy_data);
		em_read_phy_reg(hw, HV_COLC_LOWER, &phy_data);
		em_read_phy_reg(hw, HV_DC_UPPER, &phy_data);
		em_read_phy_reg(hw, HV_DC_LOWER, &phy_data);
		em_read_phy_reg(hw, HV_TNCRS_UPPER, &phy_data);
		em_read_phy_reg(hw, HV_TNCRS_LOWER, &phy_data);
	}

	if (hw->mac_type == em_ich8lan ||
	    hw->mac_type == em_ich9lan ||
	    hw->mac_type == em_ich10lan ||
	    hw->mac_type == em_pchlan ||
	    (hw->mac_type != em_pch2lan && hw->mac_type != em_pch_lpt &&
	     hw->mac_type != em_pch_spt && hw->mac_type != em_pch_cnp &&
	     hw->mac_type != em_pch_tgp && hw->mac_type != em_pch_adp))
		return;

	temp = E1000_READ_REG(hw, ICRXPTC);
	temp = E1000_READ_REG(hw, ICRXATC);
	temp = E1000_READ_REG(hw, ICTXPTC);
	temp = E1000_READ_REG(hw, ICTXATC);
	temp = E1000_READ_REG(hw, ICTXQEC);
	temp = E1000_READ_REG(hw, ICTXQMTC);
	temp = E1000_READ_REG(hw, ICRXDMTC);
}

/******************************************************************************
 * Gets the current PCI bus type, speed, and width of the hardware
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
void
em_get_bus_info(struct em_hw *hw)
{
	int32_t  ret_val;
	uint16_t pci_ex_link_status;
	uint32_t status;
	switch (hw->mac_type) {
	case em_82542_rev2_0:
	case em_82542_rev2_1:
		hw->bus_type = em_bus_type_unknown;
		hw->bus_speed = em_bus_speed_unknown;
		hw->bus_width = em_bus_width_unknown;
		break;
	case em_icp_xxxx:
		hw->bus_type = em_bus_type_cpp;
		hw->bus_speed = em_bus_speed_unknown;
		hw->bus_width = em_bus_width_unknown;
		break;
	case em_82571:
	case em_82572:
	case em_82573:
	case em_82574:
	case em_82575:
	case em_82576:
	case em_82580:
	case em_80003es2lan:
	case em_i210:
	case em_i350:
		hw->bus_type = em_bus_type_pci_express;
		hw->bus_speed = em_bus_speed_2500;
		ret_val = em_read_pcie_cap_reg(hw, PCI_EX_LINK_STATUS,
		    &pci_ex_link_status);
		if (ret_val)
			hw->bus_width = em_bus_width_unknown;
		else
			hw->bus_width = (pci_ex_link_status & 
			    PCI_EX_LINK_WIDTH_MASK) >> PCI_EX_LINK_WIDTH_SHIFT;
		break;
	case em_ich8lan:
	case em_ich9lan:
	case em_ich10lan:
	case em_pchlan:
	case em_pch2lan:
	case em_pch_lpt:
	case em_pch_spt:
	case em_pch_cnp:
	case em_pch_tgp:
	case em_pch_adp:
		hw->bus_type = em_bus_type_pci_express;
		hw->bus_speed = em_bus_speed_2500;
		hw->bus_width = em_bus_width_pciex_1;
		break;
	default:
		status = E1000_READ_REG(hw, STATUS);
		hw->bus_type = (status & E1000_STATUS_PCIX_MODE) ?
		    em_bus_type_pcix : em_bus_type_pci;

		if (hw->device_id == E1000_DEV_ID_82546EB_QUAD_COPPER) {
			hw->bus_speed = (hw->bus_type == em_bus_type_pci) ?
			    em_bus_speed_66 : em_bus_speed_120;
		} else if (hw->bus_type == em_bus_type_pci) {
			hw->bus_speed = (status & E1000_STATUS_PCI66) ?
			    em_bus_speed_66 : em_bus_speed_33;
		} else {
			switch (status & E1000_STATUS_PCIX_SPEED) {
			case E1000_STATUS_PCIX_SPEED_66:
				hw->bus_speed = em_bus_speed_66;
				break;
			case E1000_STATUS_PCIX_SPEED_100:
				hw->bus_speed = em_bus_speed_100;
				break;
			case E1000_STATUS_PCIX_SPEED_133:
				hw->bus_speed = em_bus_speed_133;
				break;
			default:
				hw->bus_speed = em_bus_speed_reserved;
				break;
			}
		}
		hw->bus_width = (status & E1000_STATUS_BUS64) ?
		    em_bus_width_64 : em_bus_width_32;
		break;
	}
}

/******************************************************************************
 * Writes a value to one of the devices registers using port I/O (as opposed to
 * memory mapped I/O). Only 82544 and newer devices support port I/O.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset to write to
 * value - value to write
 *****************************************************************************/
STATIC void
em_write_reg_io(struct em_hw *hw, uint32_t offset, uint32_t value)
{
	unsigned long io_addr = hw->io_base;
	unsigned long io_data = hw->io_base + 4;
	em_io_write(hw, io_addr, offset);
	em_io_write(hw, io_data, value);
}

/******************************************************************************
 * Estimates the cable length.
 *
 * hw - Struct containing variables accessed by shared code
 * min_length - The estimated minimum length
 * max_length - The estimated maximum length
 *
 * returns: - E1000_ERR_XXX
 *            E1000_SUCCESS
 *
 * This function always returns a ranged length (minimum & maximum).
 * So for M88 phy's, this function interprets the one value returned from the
 * register to the minimum and maximum range.
 * For IGP phy's, the function calculates the range by the AGC registers.
 *****************************************************************************/
STATIC int32_t
em_get_cable_length(struct em_hw *hw, uint16_t *min_length,
    uint16_t *max_length)
{
	int32_t  ret_val;
	uint16_t agc_value = 0;
	uint16_t i, phy_data;
	uint16_t cable_length;
	DEBUGFUNC("em_get_cable_length");

	*min_length = *max_length = 0;

	/* Use old method for Phy older than IGP */
	if (hw->phy_type == em_phy_m88 ||
	    hw->phy_type == em_phy_oem ||
	    hw->phy_type == em_phy_82578) {

		ret_val = em_read_phy_reg(hw, M88E1000_PHY_SPEC_STATUS,
		    &phy_data);
		if (ret_val)
			return ret_val;
		cable_length = (phy_data & M88E1000_PSSR_CABLE_LENGTH) >>
		    M88E1000_PSSR_CABLE_LENGTH_SHIFT;

		/* Convert the enum value to ranged values */
		switch (cable_length) {
		case em_cable_length_50:
			*min_length = 0;
			*max_length = em_igp_cable_length_50;
			break;
		case em_cable_length_50_80:
			*min_length = em_igp_cable_length_50;
			*max_length = em_igp_cable_length_80;
			break;
		case em_cable_length_80_110:
			*min_length = em_igp_cable_length_80;
			*max_length = em_igp_cable_length_110;
			break;
		case em_cable_length_110_140:
			*min_length = em_igp_cable_length_110;
			*max_length = em_igp_cable_length_140;
			break;
		case em_cable_length_140:
			*min_length = em_igp_cable_length_140;
			*max_length = em_igp_cable_length_170;
			break;
		default:
			return -E1000_ERR_PHY;
			break;
		}
	} else if (hw->phy_type == em_phy_rtl8211) {
		/* no cable length info on RTL8211, fake */
		*min_length = 0;
		*max_length = em_igp_cable_length_50;
	} else if (hw->phy_type == em_phy_gg82563) {
		ret_val = em_read_phy_reg(hw, GG82563_PHY_DSP_DISTANCE,
		    &phy_data);
		if (ret_val)
			return ret_val;
		cable_length = phy_data & GG82563_DSPD_CABLE_LENGTH;

		switch (cable_length) {
		case em_gg_cable_length_60:
			*min_length = 0;
			*max_length = em_igp_cable_length_60;
			break;
		case em_gg_cable_length_60_115:
			*min_length = em_igp_cable_length_60;
			*max_length = em_igp_cable_length_115;
			break;
		case em_gg_cable_length_115_150:
			*min_length = em_igp_cable_length_115;
			*max_length = em_igp_cable_length_150;
			break;
		case em_gg_cable_length_150:
			*min_length = em_igp_cable_length_150;
			*max_length = em_igp_cable_length_180;
			break;
		default:
			return -E1000_ERR_PHY;
			break;
		}
	} else if (hw->phy_type == em_phy_igp) {	/* For IGP PHY */
		uint16_t        cur_agc_value;
		uint16_t        min_agc_value = 
		    IGP01E1000_AGC_LENGTH_TABLE_SIZE;
		uint16_t        agc_reg_array[IGP01E1000_PHY_CHANNEL_NUM] =
		    {IGP01E1000_PHY_AGC_A, IGP01E1000_PHY_AGC_B,
		    IGP01E1000_PHY_AGC_C, IGP01E1000_PHY_AGC_D};

		/* Read the AGC registers for all channels */
		for (i = 0; i < IGP01E1000_PHY_CHANNEL_NUM; i++) {
			ret_val = em_read_phy_reg(hw, agc_reg_array[i], 
			    &phy_data);
			if (ret_val)
				return ret_val;

			cur_agc_value = phy_data >>
			    IGP01E1000_AGC_LENGTH_SHIFT;

			/* Value bound check. */
			if ((cur_agc_value >= 
			    IGP01E1000_AGC_LENGTH_TABLE_SIZE - 1) ||
			    (cur_agc_value == 0))
				return -E1000_ERR_PHY;

			agc_value += cur_agc_value;

			/* Update minimal AGC value. */
			if (min_agc_value > cur_agc_value)
				min_agc_value = cur_agc_value;
		}

		/* Remove the minimal AGC result for length < 50m */
		if (agc_value < IGP01E1000_PHY_CHANNEL_NUM * 
		    em_igp_cable_length_50) {
			agc_value -= min_agc_value;

			/*
			 * Get the average length of the remaining 3 channels
			 */
			agc_value /= (IGP01E1000_PHY_CHANNEL_NUM - 1);
		} else {
			/* Get the average length of all the 4 channels. */
			agc_value /= IGP01E1000_PHY_CHANNEL_NUM;
		}

		/* Set the range of the calculated length. */
		*min_length = ((em_igp_cable_length_table[agc_value] -
		    IGP01E1000_AGC_RANGE) > 0) ?
		    (em_igp_cable_length_table[agc_value] -
		    IGP01E1000_AGC_RANGE) : 0;
		*max_length = em_igp_cable_length_table[agc_value] +
		    IGP01E1000_AGC_RANGE;
	} else if (hw->phy_type == em_phy_igp_2 ||
	    hw->phy_type == em_phy_igp_3) {
		uint16_t cur_agc_index, max_agc_index = 0;
		uint16_t min_agc_index = IGP02E1000_AGC_LENGTH_TABLE_SIZE - 1;
		uint16_t agc_reg_array[IGP02E1000_PHY_CHANNEL_NUM] =
		    {IGP02E1000_PHY_AGC_A, IGP02E1000_PHY_AGC_B,
		    IGP02E1000_PHY_AGC_C, IGP02E1000_PHY_AGC_D};
		/* Read the AGC registers for all channels */
		for (i = 0; i < IGP02E1000_PHY_CHANNEL_NUM; i++) {
			ret_val = em_read_phy_reg(hw, agc_reg_array[i],
			    &phy_data);
			if (ret_val)
				return ret_val;
			/*
			 * Getting bits 15:9, which represent the combination
			 * of course and fine gain values.  The result is a
			 * number that can be put into the lookup table to
			 * obtain the approximate cable length.
			 */
			cur_agc_index = (phy_data >>
			    IGP02E1000_AGC_LENGTH_SHIFT) &
			    IGP02E1000_AGC_LENGTH_MASK;

			/* Array index bound check. */
			if ((cur_agc_index >= IGP02E1000_AGC_LENGTH_TABLE_SIZE)
			    || (cur_agc_index == 0))
				return -E1000_ERR_PHY;

			/* Remove min & max AGC values from calculation. */
			if (em_igp_2_cable_length_table[min_agc_index] >
			    em_igp_2_cable_length_table[cur_agc_index])
				min_agc_index = cur_agc_index;
			if (em_igp_2_cable_length_table[max_agc_index] <
			    em_igp_2_cable_length_table[cur_agc_index])
				max_agc_index = cur_agc_index;

			agc_value += em_igp_2_cable_length_table
			    [cur_agc_index];
		}

		agc_value -= (em_igp_2_cable_length_table[min_agc_index] +
		    em_igp_2_cable_length_table[max_agc_index]);
		agc_value /= (IGP02E1000_PHY_CHANNEL_NUM - 2);
		/*
		 * Calculate cable length with the error range of +/- 10
		 * meters.
		 */
		*min_length = ((agc_value - IGP02E1000_AGC_RANGE) > 0) ?
		    (agc_value - IGP02E1000_AGC_RANGE) : 0;
		*max_length = agc_value + IGP02E1000_AGC_RANGE;
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 * Check if Downshift occurred
 *
 * hw - Struct containing variables accessed by shared code
 * downshift - output parameter : 0 - No Downshift occurred.
 *                                1 - Downshift occurred.
 *
 * returns: - E1000_ERR_XXX
 *            E1000_SUCCESS
 *
 * For phy's older then IGP, this function reads the Downshift bit in the Phy
 * Specific Status register.  For IGP phy's, it reads the Downgrade bit in the
 * Link Health register.  In IGP this bit is latched high, so the driver must
 * read it immediately after link is established.
 *****************************************************************************/
STATIC int32_t
em_check_downshift(struct em_hw *hw)
{
	int32_t  ret_val;
	uint16_t phy_data;
	DEBUGFUNC("em_check_downshift");

	if (hw->phy_type == em_phy_igp ||
	    hw->phy_type == em_phy_igp_3 ||
	    hw->phy_type == em_phy_igp_2) {
		ret_val = em_read_phy_reg(hw, IGP01E1000_PHY_LINK_HEALTH,
		    &phy_data);
		if (ret_val)
			return ret_val;

		hw->speed_downgraded = (phy_data &
		    IGP01E1000_PLHR_SS_DOWNGRADE) ? 1 : 0;
	} else if ((hw->phy_type == em_phy_m88) ||
	    (hw->phy_type == em_phy_gg82563) ||
	    (hw->phy_type == em_phy_oem) ||
	    (hw->phy_type == em_phy_82578)) {
		ret_val = em_read_phy_reg(hw, M88E1000_PHY_SPEC_STATUS,
		    &phy_data);
		if (ret_val)
			return ret_val;

		hw->speed_downgraded = (phy_data & M88E1000_PSSR_DOWNSHIFT) >>
		    M88E1000_PSSR_DOWNSHIFT_SHIFT;
	} else if (hw->phy_type == em_phy_ife) {
		/* em_phy_ife supports 10/100 speed only */
		hw->speed_downgraded = FALSE;
	}
	return E1000_SUCCESS;
}

/*****************************************************************************
 *
 * 82541_rev_2 & 82547_rev_2 have the capability to configure the DSP when a
 * gigabit link is achieved to improve link quality.
 *
 * hw: Struct containing variables accessed by shared code
 *
 * returns: - E1000_ERR_PHY if fail to read/write the PHY
 *            E1000_SUCCESS at any other case.
 *
 ****************************************************************************/
STATIC int32_t
em_config_dsp_after_link_change(struct em_hw *hw, boolean_t link_up)
{
	int32_t  ret_val;
	uint16_t phy_data, phy_saved_data, speed, duplex, i;
	uint16_t dsp_reg_array[IGP01E1000_PHY_CHANNEL_NUM] =
	    {IGP01E1000_PHY_AGC_PARAM_A, IGP01E1000_PHY_AGC_PARAM_B,
	    IGP01E1000_PHY_AGC_PARAM_C, IGP01E1000_PHY_AGC_PARAM_D};
	uint16_t min_length, max_length;
	DEBUGFUNC("em_config_dsp_after_link_change");

	if (hw->phy_type != em_phy_igp)
		return E1000_SUCCESS;

	if (link_up) {
		ret_val = em_get_speed_and_duplex(hw, &speed, &duplex);
		if (ret_val) {
			DEBUGOUT("Error getting link speed and duplex\n");
			return ret_val;
		}
		if (speed == SPEED_1000) {

			ret_val = em_get_cable_length(hw, &min_length, &max_length);
			if (ret_val)
				return ret_val;

			if ((hw->dsp_config_state == em_dsp_config_enabled) &&
			    min_length >= em_igp_cable_length_50) {

				for (i = 0; i < IGP01E1000_PHY_CHANNEL_NUM;
				    i++) {
					ret_val = em_read_phy_reg(hw,
					    dsp_reg_array[i], &phy_data);
					if (ret_val)
						return ret_val;

					phy_data &=
					    ~IGP01E1000_PHY_EDAC_MU_INDEX;

					ret_val = em_write_phy_reg(hw,
					    dsp_reg_array[i], phy_data);
					if (ret_val)
						return ret_val;
				}
				hw->dsp_config_state = em_dsp_config_activated;
			}
			if ((hw->ffe_config_state == em_ffe_config_enabled) &&
			    (min_length < em_igp_cable_length_50)) {

				uint16_t ffe_idle_err_timeout =
				    FFE_IDLE_ERR_COUNT_TIMEOUT_20;
				uint32_t idle_errs = 0;
				/* clear previous idle error counts */
				ret_val = em_read_phy_reg(hw, PHY_1000T_STATUS,
				    &phy_data);
				if (ret_val)
					return ret_val;

				for (i = 0; i < ffe_idle_err_timeout; i++) {
					usec_delay(1000);
					ret_val = em_read_phy_reg(hw,
					    PHY_1000T_STATUS, &phy_data);
					if (ret_val)
						return ret_val;

					idle_errs += (phy_data &
					    SR_1000T_IDLE_ERROR_CNT);
					if (idle_errs >
					    SR_1000T_PHY_EXCESSIVE_IDLE_ERR_COUNT) {
						hw->ffe_config_state =
						    em_ffe_config_active;

						ret_val = em_write_phy_reg(hw,
						    IGP01E1000_PHY_DSP_FFE,
						    IGP01E1000_PHY_DSP_FFE_CM_CP);
						if (ret_val)
							return ret_val;
						break;
					}
					if (idle_errs)
						ffe_idle_err_timeout =
						    FFE_IDLE_ERR_COUNT_TIMEOUT_100;
				}
			}
		}
	} else {
		if (hw->dsp_config_state == em_dsp_config_activated) {
			/*
			 * Save off the current value of register 0x2F5B to
			 * be restored at the end of the routines.
			 */
			ret_val = em_read_phy_reg(hw, 0x2F5B, &phy_saved_data);

			if (ret_val)
				return ret_val;

			/* Disable the PHY transmitter */
			ret_val = em_write_phy_reg(hw, 0x2F5B, 0x0003);

			if (ret_val)
				return ret_val;

			msec_delay_irq(20);

			ret_val = em_write_phy_reg(hw, 0x0000,
			    IGP01E1000_IEEE_FORCE_GIGA);
			if (ret_val)
				return ret_val;
			for (i = 0; i < IGP01E1000_PHY_CHANNEL_NUM; i++) {
				ret_val = em_read_phy_reg(hw, dsp_reg_array[i],
				    &phy_data);
				if (ret_val)
					return ret_val;

				phy_data &= ~IGP01E1000_PHY_EDAC_MU_INDEX;
				phy_data |=
				    IGP01E1000_PHY_EDAC_SIGN_EXT_9_BITS;

				ret_val = em_write_phy_reg(hw,
				    dsp_reg_array[i], phy_data);
				if (ret_val)
					return ret_val;
			}

			ret_val = em_write_phy_reg(hw, 0x0000,
			    IGP01E1000_IEEE_RESTART_AUTONEG);
			if (ret_val)
				return ret_val;

			msec_delay_irq(20);

			/* Now enable the transmitter */
			ret_val = em_write_phy_reg(hw, 0x2F5B, phy_saved_data);

			if (ret_val)
				return ret_val;

			hw->dsp_config_state = em_dsp_config_enabled;
		}
		if (hw->ffe_config_state == em_ffe_config_active) {
			/*
			 * Save off the current value of register 0x2F5B to
			 * be restored at the end of the routines.
			 */
			ret_val = em_read_phy_reg(hw, 0x2F5B, &phy_saved_data);

			if (ret_val)
				return ret_val;

			/* Disable the PHY transmitter */
			ret_val = em_write_phy_reg(hw, 0x2F5B, 0x0003);

			if (ret_val)
				return ret_val;

			msec_delay_irq(20);

			ret_val = em_write_phy_reg(hw, 0x0000,
			    IGP01E1000_IEEE_FORCE_GIGA);
			if (ret_val)
				return ret_val;
			ret_val = em_write_phy_reg(hw, IGP01E1000_PHY_DSP_FFE,
			    IGP01E1000_PHY_DSP_FFE_DEFAULT);
			if (ret_val)
				return ret_val;

			ret_val = em_write_phy_reg(hw, 0x0000,
			    IGP01E1000_IEEE_RESTART_AUTONEG);
			if (ret_val)
				return ret_val;

			msec_delay_irq(20);

			/* Now enable the transmitter */
			ret_val = em_write_phy_reg(hw, 0x2F5B, phy_saved_data);

			if (ret_val)
				return ret_val;

			hw->ffe_config_state = em_ffe_config_enabled;
		}
	}
	return E1000_SUCCESS;
}

/*****************************************************************************
 * Set PHY to class A mode
 * Assumes the following operations will follow to enable the new class mode.
 *  1. Do a PHY soft reset
 *  2. Restart auto-negotiation or force link.
 *
 * hw - Struct containing variables accessed by shared code
 ****************************************************************************/
static int32_t
em_set_phy_mode(struct em_hw *hw)
{
	int32_t  ret_val;
	uint16_t eeprom_data;
	DEBUGFUNC("em_set_phy_mode");

	if ((hw->mac_type == em_82545_rev_3) &&
	    (hw->media_type == em_media_type_copper)) {
		ret_val = em_read_eeprom(hw, EEPROM_PHY_CLASS_WORD, 1,
		    &eeprom_data);
		if (ret_val) {
			return ret_val;
		}
		if ((eeprom_data != EEPROM_RESERVED_WORD) &&
		    (eeprom_data & EEPROM_PHY_CLASS_A)) {
			ret_val = em_write_phy_reg(hw,
			    M88E1000_PHY_PAGE_SELECT, 0x000B);
			if (ret_val)
				return ret_val;
			ret_val = em_write_phy_reg(hw,
			    M88E1000_PHY_GEN_CONTROL, 0x8104);
			if (ret_val)
				return ret_val;

			hw->phy_reset_disable = FALSE;
		}
	}
	return E1000_SUCCESS;
}

/*****************************************************************************
 *
 * This function sets the lplu state according to the active flag.  When
 * activating lplu this function also disables smart speed and vice versa.
 * lplu will not be activated unless the device autonegotiation advertisement
 * meets standards of either 10 or 10/100 or 10/100/1000 at all duplexes.
 * hw: Struct containing variables accessed by shared code
 * active - true to enable lplu false to disable lplu.
 *
 * returns: - E1000_ERR_PHY if fail to read/write the PHY
 *            E1000_SUCCESS at any other case.
 *
 ****************************************************************************/
STATIC int32_t
em_set_d3_lplu_state(struct em_hw *hw, boolean_t active)
{
	uint32_t phy_ctrl = 0;
	int32_t  ret_val;
	uint16_t phy_data;
	DEBUGFUNC("em_set_d3_lplu_state");

	if (hw->phy_type != em_phy_igp && hw->phy_type != em_phy_igp_2
	    && hw->phy_type != em_phy_igp_3)
		return E1000_SUCCESS;
	/*
	 * During driver activity LPLU should not be used or it will attain
	 * link from the lowest speeds starting from 10Mbps. The capability
	 * is used for Dx transitions and states
	 */
	if (hw->mac_type == em_82541_rev_2 || hw->mac_type == em_82547_rev_2) {
		ret_val = em_read_phy_reg(hw, IGP01E1000_GMII_FIFO, &phy_data);
		if (ret_val)
			return ret_val;
	} else if (IS_ICH8(hw->mac_type)) {
		/*
		 * MAC writes into PHY register based on the state transition
		 * and start auto-negotiation. SW driver can overwrite the
		 * settings in CSR PHY power control E1000_PHY_CTRL register.
		 */
		phy_ctrl = E1000_READ_REG(hw, PHY_CTRL);
	} else {
		ret_val = em_read_phy_reg(hw, IGP02E1000_PHY_POWER_MGMT,
		    &phy_data);
		if (ret_val)
			return ret_val;
	}

	if (!active) {
		if (hw->mac_type == em_82541_rev_2 ||
		    hw->mac_type == em_82547_rev_2) {
			phy_data &= ~IGP01E1000_GMII_FLEX_SPD;
			ret_val = em_write_phy_reg(hw, IGP01E1000_GMII_FIFO,
			    phy_data);
			if (ret_val)
				return ret_val;
		} else {
			if (IS_ICH8(hw->mac_type)) {
				phy_ctrl &= ~E1000_PHY_CTRL_NOND0A_LPLU;
				E1000_WRITE_REG(hw, PHY_CTRL, phy_ctrl);
			} else {
				phy_data &= ~IGP02E1000_PM_D3_LPLU;
				ret_val = em_write_phy_reg(hw,
				    IGP02E1000_PHY_POWER_MGMT, phy_data);
				if (ret_val)
					return ret_val;
			}
		}
		/*
		 * LPLU and SmartSpeed are mutually exclusive.  LPLU is used
		 * during Dx states where the power conservation is most
		 * important.  During driver activity we should enable
		 * SmartSpeed, so performance is maintained.
		 */
		if (hw->smart_speed == em_smart_speed_on) {
			ret_val = em_read_phy_reg(hw,
			    IGP01E1000_PHY_PORT_CONFIG, &phy_data);
			if (ret_val)
				return ret_val;

			phy_data |= IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = em_write_phy_reg(hw,
			    IGP01E1000_PHY_PORT_CONFIG, phy_data);
			if (ret_val)
				return ret_val;
		} else if (hw->smart_speed == em_smart_speed_off) {
			ret_val = em_read_phy_reg(hw,
			    IGP01E1000_PHY_PORT_CONFIG, &phy_data);
			if (ret_val)
				return ret_val;

			phy_data &= ~IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = em_write_phy_reg(hw,
			    IGP01E1000_PHY_PORT_CONFIG, phy_data);
			if (ret_val)
				return ret_val;
		}
	} else if ((hw->autoneg_advertised == AUTONEG_ADVERTISE_SPEED_DEFAULT)
	    || (hw->autoneg_advertised == AUTONEG_ADVERTISE_10_ALL) ||
	    (hw->autoneg_advertised == AUTONEG_ADVERTISE_10_100_ALL)) {

		if (hw->mac_type == em_82541_rev_2 ||
		    hw->mac_type == em_82547_rev_2) {
			phy_data |= IGP01E1000_GMII_FLEX_SPD;
			ret_val = em_write_phy_reg(hw, IGP01E1000_GMII_FIFO,
			    phy_data);
			if (ret_val)
				return ret_val;
		} else {
			if (IS_ICH8(hw->mac_type)) {
				phy_ctrl |= E1000_PHY_CTRL_NOND0A_LPLU;
				E1000_WRITE_REG(hw, PHY_CTRL, phy_ctrl);
			} else {
				phy_data |= IGP02E1000_PM_D3_LPLU;
				ret_val = em_write_phy_reg(hw,
				    IGP02E1000_PHY_POWER_MGMT, phy_data);
				if (ret_val)
					return ret_val;
			}
		}

		/* When LPLU is enabled we should disable SmartSpeed */
		ret_val = em_read_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
		    &phy_data);
		if (ret_val)
			return ret_val;

		phy_data &= ~IGP01E1000_PSCFR_SMART_SPEED;
		ret_val = em_write_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
		    phy_data);
		if (ret_val)
			return ret_val;

	}
	return E1000_SUCCESS;
}

/*****************************************************************************
 *
 * This function sets the lplu d0 state according to the active flag.  When
 * activating lplu this function also disables smart speed and vice versa.
 * lplu will not be activated unless the device autonegotiation advertisement
 * meets standards of either 10 or 10/100 or 10/100/1000 at all duplexes.
 * hw: Struct containing variables accessed by shared code
 * active - true to enable lplu false to disable lplu.
 *
 * returns: - E1000_ERR_PHY if fail to read/write the PHY
 *            E1000_SUCCESS at any other case.
 *
 ****************************************************************************/
STATIC int32_t
em_set_d0_lplu_state(struct em_hw *hw, boolean_t active)
{
	uint32_t phy_ctrl = 0;
	int32_t  ret_val;
	uint16_t phy_data;
	DEBUGFUNC("em_set_d0_lplu_state");

	if (hw->mac_type <= em_82547_rev_2)
		return E1000_SUCCESS;

	if (IS_ICH8(hw->mac_type)) {
		phy_ctrl = E1000_READ_REG(hw, PHY_CTRL);
	} else {
		ret_val = em_read_phy_reg(hw, IGP02E1000_PHY_POWER_MGMT,
		    &phy_data);
		if (ret_val)
			return ret_val;
	}

	if (!active) {
		if (IS_ICH8(hw->mac_type)) {
			phy_ctrl &= ~E1000_PHY_CTRL_D0A_LPLU;
			E1000_WRITE_REG(hw, PHY_CTRL, phy_ctrl);
		} else {
			phy_data &= ~IGP02E1000_PM_D0_LPLU;
			ret_val = em_write_phy_reg(hw,
			    IGP02E1000_PHY_POWER_MGMT, phy_data);
			if (ret_val)
				return ret_val;
		}
		/*
		 * LPLU and SmartSpeed are mutually exclusive.  LPLU is used
		 * during Dx states where the power conservation is most
		 * important.  During driver activity we should enable
		 * SmartSpeed, so performance is maintained.
		 */
		if (hw->smart_speed == em_smart_speed_on) {
			ret_val = em_read_phy_reg(hw,
			    IGP01E1000_PHY_PORT_CONFIG, &phy_data);
			if (ret_val)
				return ret_val;

			phy_data |= IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = em_write_phy_reg(hw,
			    IGP01E1000_PHY_PORT_CONFIG, phy_data);
			if (ret_val)
				return ret_val;
		} else if (hw->smart_speed == em_smart_speed_off) {
			ret_val = em_read_phy_reg(hw,
			    IGP01E1000_PHY_PORT_CONFIG, &phy_data);
			if (ret_val)
				return ret_val;

			phy_data &= ~IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = em_write_phy_reg(hw,
			    IGP01E1000_PHY_PORT_CONFIG, phy_data);
			if (ret_val)
				return ret_val;
		}
	} else {
		if (IS_ICH8(hw->mac_type)) {
			phy_ctrl |= E1000_PHY_CTRL_D0A_LPLU;
			E1000_WRITE_REG(hw, PHY_CTRL, phy_ctrl);
		} else {
			phy_data |= IGP02E1000_PM_D0_LPLU;
			ret_val = em_write_phy_reg(hw,
			    IGP02E1000_PHY_POWER_MGMT, phy_data);
			if (ret_val)
				return ret_val;
		}

		/* When LPLU is enabled we should disable SmartSpeed */
		ret_val = em_read_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
		    &phy_data);
		if (ret_val)
			return ret_val;

		phy_data &= ~IGP01E1000_PSCFR_SMART_SPEED;
		ret_val = em_write_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
		    phy_data);
		if (ret_val)
			return ret_val;

	}
	return E1000_SUCCESS;
}

/***************************************************************************
 *  Set Low Power Link Up state
 *
 *  Sets the LPLU state according to the active flag.  For PCH, if OEM write
 *  bit are disabled in the NVM, writing the LPLU bits in the MAC will not set
 *  the phy speed. This function will manually set the LPLU bit and restart
 *  auto-neg as hw would do. D3 and D0 LPLU will call the same function
 *  since it configures the same bit.
 ***************************************************************************/
int32_t
em_set_lplu_state_pchlan(struct em_hw *hw, boolean_t active)
{
	int32_t ret_val = E1000_SUCCESS;
	uint16_t oem_reg;

	DEBUGFUNC("e1000_set_lplu_state_pchlan");

	ret_val = em_read_phy_reg(hw, HV_OEM_BITS, &oem_reg);
	if (ret_val)
		goto out;

	if (active)
		oem_reg |= HV_OEM_BITS_LPLU;
	else
		oem_reg &= ~HV_OEM_BITS_LPLU;

	oem_reg |= HV_OEM_BITS_RESTART_AN;
	ret_val = em_write_phy_reg(hw, HV_OEM_BITS, oem_reg);

out:
	return ret_val;
}

/******************************************************************************
 * Change VCO speed register to improve Bit Error Rate performance of SERDES.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static int32_t
em_set_vco_speed(struct em_hw *hw)
{
	int32_t  ret_val;
	uint16_t default_page = 0;
	uint16_t phy_data;
	DEBUGFUNC("em_set_vco_speed");

	switch (hw->mac_type) {
	case em_82545_rev_3:
	case em_82546_rev_3:
		break;
	default:
		return E1000_SUCCESS;
	}

	/* Set PHY register 30, page 5, bit 8 to 0 */

	ret_val = em_read_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, &default_page);
	if (ret_val)
		return ret_val;

	ret_val = em_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0005);
	if (ret_val)
		return ret_val;

	ret_val = em_read_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, &phy_data);
	if (ret_val)
		return ret_val;

	phy_data &= ~M88E1000_PHY_VCO_REG_BIT8;
	ret_val = em_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, phy_data);
	if (ret_val)
		return ret_val;

	/* Set PHY register 30, page 4, bit 11 to 1 */

	ret_val = em_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0004);
	if (ret_val)
		return ret_val;

	ret_val = em_read_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, &phy_data);
	if (ret_val)
		return ret_val;

	phy_data |= M88E1000_PHY_VCO_REG_BIT11;
	ret_val = em_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, phy_data);
	if (ret_val)
		return ret_val;

	ret_val = em_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, default_page);
	if (ret_val)
		return ret_val;

	return E1000_SUCCESS;
}

/*****************************************************************************
 * This function reads the cookie from ARC ram.
 *
 * returns: - E1000_SUCCESS .
 ****************************************************************************/
STATIC int32_t
em_host_if_read_cookie(struct em_hw *hw, uint8_t *buffer)
{
	uint8_t  i;
	uint32_t offset = E1000_MNG_DHCP_COOKIE_OFFSET;
	uint8_t  length = E1000_MNG_DHCP_COOKIE_LENGTH;
	length = (length >> 2);
	offset = (offset >> 2);

	for (i = 0; i < length; i++) {
		*((uint32_t *) buffer + i) =
		    E1000_READ_REG_ARRAY_DWORD(hw, HOST_IF, offset + i);
	}
	return E1000_SUCCESS;
}

/*****************************************************************************
 * This function checks whether the HOST IF is enabled for command operation
 * and also checks whether the previous command is completed.
 * It busy waits in case of previous command is not completed.
 *
 * returns: - E1000_ERR_HOST_INTERFACE_COMMAND in case if is not ready or
 *            timeout
 *          - E1000_SUCCESS for success.
 ****************************************************************************/
STATIC int32_t
em_mng_enable_host_if(struct em_hw *hw)
{
	uint32_t hicr;
	uint8_t  i;
	/* Check that the host interface is enabled. */
	hicr = E1000_READ_REG(hw, HICR);
	if ((hicr & E1000_HICR_EN) == 0) {
		DEBUGOUT("E1000_HOST_EN bit disabled.\n");
		return -E1000_ERR_HOST_INTERFACE_COMMAND;
	}
	/* check the previous command is completed */
	for (i = 0; i < E1000_MNG_DHCP_COMMAND_TIMEOUT; i++) {
		hicr = E1000_READ_REG(hw, HICR);
		if (!(hicr & E1000_HICR_C))
			break;
		msec_delay_irq(1);
	}

	if (i == E1000_MNG_DHCP_COMMAND_TIMEOUT) {
		DEBUGOUT("Previous command timeout failed .\n");
		return -E1000_ERR_HOST_INTERFACE_COMMAND;
	}
	return E1000_SUCCESS;
}

/*****************************************************************************
 * This function checks the mode of the firmware.
 *
 * returns  - TRUE when the mode is IAMT or FALSE.
 ****************************************************************************/
boolean_t
em_check_mng_mode(struct em_hw *hw)
{
	uint32_t fwsm;
	fwsm = E1000_READ_REG(hw, FWSM);

	if (IS_ICH8(hw->mac_type)) {
		if ((fwsm & E1000_FWSM_MODE_MASK) ==
		    (E1000_MNG_ICH_IAMT_MODE << E1000_FWSM_MODE_SHIFT))
			return TRUE;
	} else if ((fwsm & E1000_FWSM_MODE_MASK) ==
	    (E1000_MNG_IAMT_MODE << E1000_FWSM_MODE_SHIFT))
		return TRUE;

	return FALSE;
}

/*****************************************************************************
 * This function calculates the checksum.
 *
 * returns  - checksum of buffer contents.
 ****************************************************************************/
STATIC uint8_t
em_calculate_mng_checksum(char *buffer, uint32_t length)
{
	uint8_t  sum = 0;
	uint32_t i;
	if (!buffer)
		return 0;

	for (i = 0; i < length; i++)
		sum += buffer[i];

	return (uint8_t) (0 - sum);
}

/*****************************************************************************
 * This function checks whether tx pkt filtering needs to be enabled or not.
 *
 * returns  - TRUE for packet filtering or FALSE.
 ****************************************************************************/
boolean_t
em_enable_tx_pkt_filtering(struct em_hw *hw)
{
	/* called in init as well as watchdog timer functions */
	int32_t   ret_val, checksum;
	boolean_t tx_filter = FALSE;
	struct em_host_mng_dhcp_cookie *hdr = &(hw->mng_cookie);
	uint8_t   *buffer = (uint8_t *) & (hw->mng_cookie);
	if (em_check_mng_mode(hw)) {
		ret_val = em_mng_enable_host_if(hw);
		if (ret_val == E1000_SUCCESS) {
			ret_val = em_host_if_read_cookie(hw, buffer);
			if (ret_val == E1000_SUCCESS) {
				checksum = hdr->checksum;
				hdr->checksum = 0;
				if ((hdr->signature == E1000_IAMT_SIGNATURE) &&
				    checksum == em_calculate_mng_checksum(
				    (char *) buffer, 
				    E1000_MNG_DHCP_COOKIE_LENGTH)) {
					if (hdr->status &
					    E1000_MNG_DHCP_COOKIE_STATUS_PARSING_SUPPORT)
						tx_filter = TRUE;
				} else
					tx_filter = TRUE;
			} else
				tx_filter = TRUE;
		}
	}
	hw->tx_pkt_filtering = tx_filter;
	return tx_filter;
}

static int32_t
em_polarity_reversal_workaround(struct em_hw *hw)
{
	int32_t  ret_val;
	uint16_t mii_status_reg;
	uint16_t i;
	/* Polarity reversal workaround for forced 10F/10H links. */

	/* Disable the transmitter on the PHY */
	ret_val = em_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0019);
	if (ret_val)
		return ret_val;
	ret_val = em_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, 0xFFFF);
	if (ret_val)
		return ret_val;

	ret_val = em_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0000);
	if (ret_val)
		return ret_val;

	/* This loop will early-out if the NO link condition has been met. */
	for (i = PHY_FORCE_TIME; i > 0; i--) {
		/*
		 * Read the MII Status Register and wait for Link Status bit
		 * to be clear.
		 */

		ret_val = em_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
		if (ret_val)
			return ret_val;

		ret_val = em_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
		if (ret_val)
			return ret_val;

		if ((mii_status_reg & ~MII_SR_LINK_STATUS) == 0)
			break;
		msec_delay_irq(100);
	}

	/* Recommended delay time after link has been lost */
	msec_delay_irq(1000);

	/* Now we will re-enable the transmitter on the PHY */

	ret_val = em_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0019);
	if (ret_val)
		return ret_val;
	msec_delay_irq(50);
	ret_val = em_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, 0xFFF0);
	if (ret_val)
		return ret_val;
	msec_delay_irq(50);
	ret_val = em_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, 0xFF00);
	if (ret_val)
		return ret_val;
	msec_delay_irq(50);
	ret_val = em_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, 0x0000);
	if (ret_val)
		return ret_val;

	ret_val = em_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0000);
	if (ret_val)
		return ret_val;

	/* This loop will early-out if the link condition has been met. */
	for (i = PHY_FORCE_TIME; i > 0; i--) {
		/*
		 * Read the MII Status Register and wait for Link Status bit
		 * to be set.
		 */

		ret_val = em_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
		if (ret_val)
			return ret_val;

		ret_val = em_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
		if (ret_val)
			return ret_val;

		if (mii_status_reg & MII_SR_LINK_STATUS)
			break;
		msec_delay_irq(100);
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 *
 * Disables PCI-Express master access.
 *
 * hw: Struct containing variables accessed by shared code
 *
 * returns: - none.
 *
 *****************************************************************************/
STATIC void
em_set_pci_express_master_disable(struct em_hw *hw)
{
	uint32_t ctrl;
	DEBUGFUNC("em_set_pci_express_master_disable");

	if (hw->bus_type != em_bus_type_pci_express)
		return;

	ctrl = E1000_READ_REG(hw, CTRL);
	ctrl |= E1000_CTRL_GIO_MASTER_DISABLE;
	E1000_WRITE_REG(hw, CTRL, ctrl);
}

/******************************************************************************
 *
 * Disables PCI-Express master access and verifies there are no pending 
 * requests
 *
 * hw: Struct containing variables accessed by shared code
 *
 * returns: - E1000_ERR_MASTER_REQUESTS_PENDING if master disable bit hasn't
 *            caused the master requests to be disabled.
 *            E1000_SUCCESS master requests disabled.
 *
 ******************************************************************************/
int32_t
em_disable_pciex_master(struct em_hw *hw)
{
	int32_t timeout = MASTER_DISABLE_TIMEOUT;	/* 80ms */
	DEBUGFUNC("em_disable_pciex_master");

	if (hw->bus_type != em_bus_type_pci_express)
		return E1000_SUCCESS;

	em_set_pci_express_master_disable(hw);

	while (timeout) {
		if (!(E1000_READ_REG(hw, STATUS) & 
		    E1000_STATUS_GIO_MASTER_ENABLE))
			break;
		else
			usec_delay(100);
		timeout--;
	}

	if (!timeout) {
		DEBUGOUT("Master requests are pending.\n");
		return -E1000_ERR_MASTER_REQUESTS_PENDING;
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 *
 * Check for EEPROM Auto Read bit done.
 *
 * hw: Struct containing variables accessed by shared code
 *
 * returns: - E1000_ERR_RESET if fail to reset MAC
 *            E1000_SUCCESS at any other case.
 *
 ******************************************************************************/
STATIC int32_t
em_get_auto_rd_done(struct em_hw *hw)
{
	int32_t timeout = AUTO_READ_DONE_TIMEOUT;
	DEBUGFUNC("em_get_auto_rd_done");

	switch (hw->mac_type) {
	default:
		msec_delay(5);
		break;
	case em_82571:
	case em_82572:
	case em_82573:
	case em_82574:
	case em_82575:
	case em_82576:
	case em_82580:
	case em_80003es2lan:
	case em_i210:
	case em_i350:
	case em_ich8lan:
	case em_ich9lan:
	case em_ich10lan:
	case em_pchlan:
	case em_pch2lan:
	case em_pch_lpt:
	case em_pch_spt:
	case em_pch_cnp:
	case em_pch_tgp:
	case em_pch_adp:
		while (timeout) {
			if (E1000_READ_REG(hw, EECD) & E1000_EECD_AUTO_RD)
				break;
			else
				msec_delay(1);
			timeout--;
		}

		if (!timeout) {
			DEBUGOUT("Auto read by HW from EEPROM has not"
			    " completed.\n");
			return -E1000_ERR_RESET;
		}
		break;
	}
	/*
	 * PHY configuration from NVM just starts after EECD_AUTO_RD sets to
	 * high. Need to wait for PHY configuration completion before
	 * accessing NVM and PHY.
	 */
	if ((hw->mac_type == em_82573) || (hw->mac_type == em_82574))
		msec_delay(25);

	return E1000_SUCCESS;
}

/***************************************************************************
 * Checks if the PHY configuration is done
 *
 * hw: Struct containing variables accessed by shared code
 *
 * returns: - E1000_ERR_RESET if fail to reset MAC
 *            E1000_SUCCESS at any other case.
 *
 ***************************************************************************/
STATIC int32_t
em_get_phy_cfg_done(struct em_hw *hw)
{
	int32_t  timeout = PHY_CFG_TIMEOUT;
	uint32_t cfg_mask = E1000_NVM_CFG_DONE_PORT_0;
	DEBUGFUNC("em_get_phy_cfg_done");

	switch (hw->mac_type) {
	default:
		msec_delay_irq(10);
		break;
	case em_80003es2lan:
	case em_82575:
	case em_82576:
	case em_82580:
	case em_i350:
		switch (hw->bus_func) {
		case 1:
			cfg_mask = E1000_NVM_CFG_DONE_PORT_1;
			break;
		case 2:
			cfg_mask = E1000_NVM_CFG_DONE_PORT_2;
			break;
		case 3:
			cfg_mask = E1000_NVM_CFG_DONE_PORT_3;
			break;
		}
		/* FALLTHROUGH */
	case em_82571:
	case em_82572:
		while (timeout) {
			if (E1000_READ_REG(hw, EEMNGCTL) & cfg_mask)
				break;
			else
				msec_delay(1);
			timeout--;
		}
		if (!timeout) {
			DEBUGOUT("MNG configuration cycle has not completed."
			    "\n");
		}
		break;
	}

	return E1000_SUCCESS;
}

/***************************************************************************
 *
 * Using the combination of SMBI and SWESMBI semaphore bits when resetting
 * adapter or Eeprom access.
 *
 * hw: Struct containing variables accessed by shared code
 *
 * returns: - E1000_ERR_EEPROM if fail to access EEPROM.
 *            E1000_SUCCESS at any other case.
 *
 ***************************************************************************/
STATIC int32_t
em_get_hw_eeprom_semaphore(struct em_hw *hw)
{
	int32_t  timeout;
	uint32_t swsm;
	DEBUGFUNC("em_get_hw_eeprom_semaphore");

	if (!hw->eeprom_semaphore_present)
		return E1000_SUCCESS;

	if (hw->mac_type == em_80003es2lan) {
		/* Get the SW semaphore. */
		if (em_get_software_semaphore(hw) != E1000_SUCCESS)
			return -E1000_ERR_EEPROM;
	}
	/* Get the FW semaphore. */
	timeout = hw->eeprom.word_size + 1;
	while (timeout) {
		swsm = E1000_READ_REG(hw, SWSM);
		swsm |= E1000_SWSM_SWESMBI;
		E1000_WRITE_REG(hw, SWSM, swsm);
		/* if we managed to set the bit we got the semaphore. */
		swsm = E1000_READ_REG(hw, SWSM);
		if (swsm & E1000_SWSM_SWESMBI)
			break;

		usec_delay(50);
		timeout--;
	}

	if (!timeout) {
		/* Release semaphores */
		em_put_hw_eeprom_semaphore(hw);
		DEBUGOUT("Driver can't access the Eeprom - SWESMBI bit is set."
		    "\n");
		return -E1000_ERR_EEPROM;
	}
	return E1000_SUCCESS;
}

/***************************************************************************
 * This function clears HW semaphore bits.
 *
 * hw: Struct containing variables accessed by shared code
 *
 * returns: - None.
 *
 ***************************************************************************/
STATIC void
em_put_hw_eeprom_semaphore(struct em_hw *hw)
{
	uint32_t swsm;
	DEBUGFUNC("em_put_hw_eeprom_semaphore");

	if (!hw->eeprom_semaphore_present)
		return;

	swsm = E1000_READ_REG(hw, SWSM);
	if (hw->mac_type == em_80003es2lan) {
		/* Release both semaphores. */
		swsm &= ~(E1000_SWSM_SMBI | E1000_SWSM_SWESMBI);
	} else
		swsm &= ~(E1000_SWSM_SWESMBI);
	E1000_WRITE_REG(hw, SWSM, swsm);
}

/***************************************************************************
 *
 * Obtaining software semaphore bit (SMBI) before resetting PHY.
 *
 * hw: Struct containing variables accessed by shared code
 *
 * returns: - E1000_ERR_RESET if fail to obtain semaphore.
 *            E1000_SUCCESS at any other case.
 *
 ***************************************************************************/
STATIC int32_t
em_get_software_semaphore(struct em_hw *hw)
{
	int32_t  timeout = hw->eeprom.word_size + 1;
	uint32_t swsm;
	DEBUGFUNC("em_get_software_semaphore");

	if (hw->mac_type != em_80003es2lan)
		return E1000_SUCCESS;

	while (timeout) {
		swsm = E1000_READ_REG(hw, SWSM);
		/*
		 * If SMBI bit cleared, it is now set and we hold the
		 * semaphore
		 */
		if (!(swsm & E1000_SWSM_SMBI))
			break;
		msec_delay_irq(1);
		timeout--;
	}

	if (!timeout) {
		DEBUGOUT("Driver can't access device - SMBI bit is set.\n");
		return -E1000_ERR_RESET;
	}
	return E1000_SUCCESS;
}

/***************************************************************************
 *
 * Release semaphore bit (SMBI).
 *
 * hw: Struct containing variables accessed by shared code
 *
 ***************************************************************************/
STATIC void
em_release_software_semaphore(struct em_hw *hw)
{
	uint32_t swsm;
	DEBUGFUNC("em_release_software_semaphore");

	if (hw->mac_type != em_80003es2lan)
		return;

	swsm = E1000_READ_REG(hw, SWSM);
	/* Release the SW semaphores. */
	swsm &= ~E1000_SWSM_SMBI;
	E1000_WRITE_REG(hw, SWSM, swsm);
}

/******************************************************************************
 * Checks if PHY reset is blocked due to SOL/IDER session, for example.
 * Returning E1000_BLK_PHY_RESET isn't necessarily an error.  But it's up to
 * the caller to figure out how to deal with it.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * returns: - E1000_BLK_PHY_RESET
 *            E1000_SUCCESS
 *
 *****************************************************************************/
int32_t
em_check_phy_reset_block(struct em_hw *hw)
{
	uint32_t manc = 0;
	uint32_t fwsm = 0;
	DEBUGFUNC("em_check_phy_reset_block\n");

	if (IS_ICH8(hw->mac_type)) {
		int i = 0;
		int blocked = 0;
		do {
			fwsm = E1000_READ_REG(hw, FWSM);
			if (!(fwsm & E1000_FWSM_RSPCIPHY)) {
				blocked = 1;
				msec_delay(10);
				continue;
			}
			blocked = 0;
		} while (blocked && (i++ < 30));
		return blocked ? E1000_BLK_PHY_RESET : E1000_SUCCESS;
	}
	if (hw->mac_type > em_82547_rev_2)
		manc = E1000_READ_REG(hw, MANC);
	return (manc & E1000_MANC_BLK_PHY_RST_ON_IDE) ?
	    E1000_BLK_PHY_RESET : E1000_SUCCESS;
}

/******************************************************************************
 * Configure PCI-Ex no-snoop
 *
 * hw - Struct containing variables accessed by shared code.
 * no_snoop - Bitmap of no-snoop events.
 *
 * returns: E1000_SUCCESS
 *
 *****************************************************************************/
STATIC int32_t
em_set_pci_ex_no_snoop(struct em_hw *hw, uint32_t no_snoop)
{
	uint32_t gcr_reg = 0;
	DEBUGFUNC("em_set_pci_ex_no_snoop");

	if (hw->bus_type == em_bus_type_unknown)
		em_get_bus_info(hw);

	if (hw->bus_type != em_bus_type_pci_express)
		return E1000_SUCCESS;

	if (no_snoop) {
		gcr_reg = E1000_READ_REG(hw, GCR);
		gcr_reg &= ~(PCI_EX_NO_SNOOP_ALL);
		gcr_reg |= no_snoop;
		E1000_WRITE_REG(hw, GCR, gcr_reg);
	}
	if (IS_ICH8(hw->mac_type)) {
		uint32_t ctrl_ext;
		ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
		ctrl_ext |= E1000_CTRL_EXT_RO_DIS;
		E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
	}
	return E1000_SUCCESS;
}

/***************************************************************************
 *
 * Get software semaphore FLAG bit (SWFLAG).
 * SWFLAG is used to synchronize the access to all shared resource between
 * SW, FW and HW.
 *
 * hw: Struct containing variables accessed by shared code
 *
 ***************************************************************************/
STATIC int32_t
em_get_software_flag(struct em_hw *hw)
{
	int32_t  timeout = PHY_CFG_TIMEOUT;
	uint32_t extcnf_ctrl;
	DEBUGFUNC("em_get_software_flag");

	if (IS_ICH8(hw->mac_type)) {
		if (hw->sw_flag) {
			hw->sw_flag++;
			return E1000_SUCCESS;
		}
		while (timeout) {
			extcnf_ctrl = E1000_READ_REG(hw, EXTCNF_CTRL);
			if (!(extcnf_ctrl & E1000_EXTCNF_CTRL_SWFLAG))
				break;
			msec_delay_irq(1);
			timeout--;
		}
		if (!timeout) {
			printf("%s: SW has already locked the resource?\n",
			    __func__);
			return -E1000_ERR_CONFIG;
		}
		timeout = SW_FLAG_TIMEOUT;
		extcnf_ctrl |= E1000_EXTCNF_CTRL_SWFLAG;
		E1000_WRITE_REG(hw, EXTCNF_CTRL, extcnf_ctrl);

		while (timeout) {
			extcnf_ctrl = E1000_READ_REG(hw, EXTCNF_CTRL);
			if (extcnf_ctrl & E1000_EXTCNF_CTRL_SWFLAG)
				break;
			msec_delay_irq(1);
			timeout--;
		}

		if (!timeout) {
			printf("Failed to acquire the semaphore, FW or HW "
			    "has it: FWSM=0x%8.8x EXTCNF_CTRL=0x%8.8x)\n",
			    E1000_READ_REG(hw, FWSM), extcnf_ctrl);
			extcnf_ctrl &= ~E1000_EXTCNF_CTRL_SWFLAG;
			E1000_WRITE_REG(hw, EXTCNF_CTRL, extcnf_ctrl);
			return -E1000_ERR_CONFIG;
		}
	}
	hw->sw_flag++;
	return E1000_SUCCESS;
}

/***************************************************************************
 *
 * Release software semaphore FLAG bit (SWFLAG).
 * SWFLAG is used to synchronize the access to all shared resource between
 * SW, FW and HW.
 *
 * hw: Struct containing variables accessed by shared code
 *
 ***************************************************************************/
STATIC void
em_release_software_flag(struct em_hw *hw)
{
	uint32_t extcnf_ctrl;
	DEBUGFUNC("em_release_software_flag");

	if (IS_ICH8(hw->mac_type)) {
		KASSERT(hw->sw_flag > 0);
		if (--hw->sw_flag > 0)
			return;
		extcnf_ctrl = E1000_READ_REG(hw, EXTCNF_CTRL);
		extcnf_ctrl &= ~E1000_EXTCNF_CTRL_SWFLAG;
		E1000_WRITE_REG(hw, EXTCNF_CTRL, extcnf_ctrl);
	}
	return;
}

/**
 *  em_valid_nvm_bank_detect_ich8lan - finds out the valid bank 0 or 1
 *  @hw: pointer to the HW structure
 *  @bank:  pointer to the variable that returns the active bank
 *
 *  Reads signature byte from the NVM using the flash access registers.
 *  Word 0x13 bits 15:14 = 10b indicate a valid signature for that bank.
 **/
int32_t
em_valid_nvm_bank_detect_ich8lan(struct em_hw *hw, uint32_t *bank)
{
	uint32_t eecd;
	uint32_t bank1_offset = hw->flash_bank_size * sizeof(uint16_t);
	uint32_t act_offset = E1000_ICH_NVM_SIG_WORD * 2 + 1;
	uint32_t nvm_dword = 0;
	uint8_t sig_byte = 0;
	int32_t ret_val;

	DEBUGFUNC("em_valid_nvm_bank_detect_ich8lan");

	switch (hw->mac_type) {
	case em_pch_spt:
	case em_pch_cnp:
	case em_pch_tgp:
	case em_pch_adp:
		bank1_offset = hw->flash_bank_size * 2;
		act_offset = E1000_ICH_NVM_SIG_WORD * 2;

		/* set bank to 0 in case flash read fails. */
		*bank = 0;

		/* Check bank 0 */
		ret_val = em_read_ich8_dword(hw, act_offset, &nvm_dword);
		if (ret_val)
			return ret_val;
		sig_byte = (uint8_t)((nvm_dword & 0xFF00) >> 8);
		if ((sig_byte & E1000_ICH_NVM_VALID_SIG_MASK) ==
		    E1000_ICH_NVM_SIG_VALUE) {
			*bank = 0;
			return 0;
		}

		/* Check bank 1 */
		ret_val = em_read_ich8_dword(hw, act_offset + bank1_offset,
		    &nvm_dword);
		if (ret_val)
			return ret_val;
		sig_byte = (uint8_t)((nvm_dword & 0xFF00) >> 8);
		if ((sig_byte & E1000_ICH_NVM_VALID_SIG_MASK) ==
		    E1000_ICH_NVM_SIG_VALUE) {
			*bank = 1;
			return 0;
		}

		DEBUGOUT("ERROR: No valid NVM bank present\n");
		return -1;
	case em_ich8lan:
	case em_ich9lan:
		eecd = E1000_READ_REG(hw, EECD);
		if ((eecd & E1000_EECD_SEC1VAL_VALID_MASK) ==
		    E1000_EECD_SEC1VAL_VALID_MASK) {
			if (eecd & E1000_EECD_SEC1VAL)
				*bank = 1;
			else
				*bank = 0;

			return E1000_SUCCESS;
		}
		DEBUGOUT("Unable to determine valid NVM bank via EEC - reading flash signature\n");
		/* fall-thru */
	default:
		/* set bank to 0 in case flash read fails */
		*bank = 0;

		/* Check bank 0 */
		ret_val = em_read_ich8_byte(hw, act_offset,
							&sig_byte);
		if (ret_val)
			return ret_val;
		if ((sig_byte & E1000_ICH_NVM_VALID_SIG_MASK) ==
		    E1000_ICH_NVM_SIG_VALUE) {
			*bank = 0;
			return E1000_SUCCESS;
		}

		/* Check bank 1 */
		ret_val = em_read_ich8_byte(hw, act_offset +
							bank1_offset,
							&sig_byte);
		if (ret_val)
			return ret_val;
		if ((sig_byte & E1000_ICH_NVM_VALID_SIG_MASK) ==
		    E1000_ICH_NVM_SIG_VALUE) {
			*bank = 1;
			return E1000_SUCCESS;
		}

		DEBUGOUT("ERROR: No valid NVM bank present\n");
		return -1;
	}
}

STATIC int32_t
em_read_eeprom_spt(struct em_hw *hw, uint16_t offset, uint16_t words,
    uint16_t *data)
{
	int32_t  error = E1000_SUCCESS;
	uint32_t flash_bank = 0;
	uint32_t act_offset = 0;
	uint32_t bank_offset = 0;
	uint32_t dword = 0;
	uint16_t i = 0, add;

	/*
	 * We need to know which is the valid flash bank.  In the event that
	 * we didn't allocate eeprom_shadow_ram, we may not be managing
	 * flash_bank.  So it cannot be trusted and needs to be updated with
	 * each read.
	 */

	if (hw->mac_type < em_pch_spt)
		return -E1000_ERR_EEPROM;

	error = em_get_software_flag(hw);
	if (error != E1000_SUCCESS)
		return error;

	error = em_valid_nvm_bank_detect_ich8lan(hw, &flash_bank);
	if (error != E1000_SUCCESS) {
		DEBUGOUT("Could not detect valid bank, assuming bank 0\n");
		flash_bank = 0;
	}

	/*
	 * Adjust offset appropriately if we're on bank 1 - adjust for word
	 * size
	 */
	bank_offset = flash_bank * (hw->flash_bank_size * 2);

	for (i = add = 0; i < words; i += add) {
		if ((offset + i) % 2) {
			add = 1;
			if (hw->eeprom_shadow_ram != NULL
			    && hw->eeprom_shadow_ram[offset + i].modified) {
				data[i] =
				    hw->eeprom_shadow_ram[offset+i].eeprom_word;
				continue;
			}
			act_offset = bank_offset + (offset + i - 1) * 2;
		} else {
			add = 2;
			if (hw->eeprom_shadow_ram != NULL
			    && hw->eeprom_shadow_ram[offset+i].modified
			    && hw->eeprom_shadow_ram[offset+i+1].modified) {
				data[i] = hw->eeprom_shadow_ram[offset+i].eeprom_word;
				data[i+1] = hw->eeprom_shadow_ram[offset+i+1].eeprom_word;
				continue;
			}
			act_offset = bank_offset + (offset + i) * 2;
		}
		error = em_read_ich8_dword(hw, act_offset, &dword);
		if (error != E1000_SUCCESS)
			break;
		if (hw->eeprom_shadow_ram != NULL
		    && hw->eeprom_shadow_ram[offset+i].modified) {
			data[i] = hw->eeprom_shadow_ram[offset+i].eeprom_word;
		} else {
			if (add == 1)
				data[i] = dword >> 16;
			else
				data[i] = dword & 0xFFFFUL;
		}
		if (add == 1 || words-i == 1)
			continue;
		if (hw->eeprom_shadow_ram != NULL
		    && hw->eeprom_shadow_ram[offset+i+1].modified) {
			data[i+1] =
			    hw->eeprom_shadow_ram[offset+i+1].eeprom_word;
		} else {
			data[i+1] = dword >> 16;
		}
	}

	em_release_software_flag(hw);

	return error;
}

/******************************************************************************
 * Reads a 16 bit word or words from the EEPROM using the ICH8's flash access
 * register.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset of word in the EEPROM to read
 * data - word read from the EEPROM
 * words - number of words to read
 *****************************************************************************/
STATIC int32_t
em_read_eeprom_ich8(struct em_hw *hw, uint16_t offset, uint16_t words,
    uint16_t *data)
{
	int32_t  error = E1000_SUCCESS;
	uint32_t flash_bank = 0;
	uint32_t act_offset = 0;
	uint32_t bank_offset = 0;
	uint16_t word = 0;
	uint16_t i = 0;
	/*
	 * We need to know which is the valid flash bank.  In the event that
	 * we didn't allocate eeprom_shadow_ram, we may not be managing
	 * flash_bank.  So it cannot be trusted and needs to be updated with
	 * each read.
	 */

	if (hw->mac_type >= em_pch_spt)
		return em_read_eeprom_spt(hw, offset, words, data);

	error = em_get_software_flag(hw);
	if (error != E1000_SUCCESS)
		return error;

	error = em_valid_nvm_bank_detect_ich8lan(hw, &flash_bank);
	if (error != E1000_SUCCESS) {
		DEBUGOUT("Could not detect valid bank, assuming bank 0\n");
		flash_bank = 0;
	}

	/*
	 * Adjust offset appropriately if we're on bank 1 - adjust for word
	 * size
	 */
	bank_offset = flash_bank * (hw->flash_bank_size * 2);

	for (i = 0; i < words; i++) {
		if (hw->eeprom_shadow_ram != NULL &&
		    hw->eeprom_shadow_ram[offset + i].modified == TRUE) {
			data[i] =
			    hw->eeprom_shadow_ram[offset + i].eeprom_word;
		} else {
			/* The NVM part needs a byte offset, hence * 2 */
			act_offset = bank_offset + ((offset + i) * 2);
			error = em_read_ich8_word(hw, act_offset, &word);
			if (error != E1000_SUCCESS)
				break;
			data[i] = word;
		}
	}

	em_release_software_flag(hw);

	return error;
}

/******************************************************************************
 * Writes a 16 bit word or words to the EEPROM using the ICH8's flash access
 * register.  Actually, writes are written to the shadow ram cache in the hw
 * structure hw->em_shadow_ram.  em_commit_shadow_ram flushes this to
 * the NVM, which occurs when the NVM checksum is updated.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset of word in the EEPROM to write
 * words - number of words to write
 * data - words to write to the EEPROM
 *****************************************************************************/
STATIC int32_t
em_write_eeprom_ich8(struct em_hw *hw, uint16_t offset, uint16_t words,
    uint16_t *data)
{
	uint32_t i = 0;
	int32_t  error = E1000_SUCCESS;
	error = em_get_software_flag(hw);
	if (error != E1000_SUCCESS)
		return error;
	/*
	 * A driver can write to the NVM only if it has eeprom_shadow_ram
	 * allocated.  Subsequent reads to the modified words are read from
	 * this cached structure as well.  Writes will only go into this
	 * cached structure unless it's followed by a call to
	 * em_update_eeprom_checksum() where it will commit the changes and
	 * clear the "modified" field.
	 */
	if (hw->eeprom_shadow_ram != NULL) {
		for (i = 0; i < words; i++) {
			if ((offset + i) < E1000_SHADOW_RAM_WORDS) {
				hw->eeprom_shadow_ram[offset + i].modified =
				    TRUE;
				hw->eeprom_shadow_ram[offset + i].eeprom_word =
				    data[i];
			} else {
				error = -E1000_ERR_EEPROM;
				break;
			}
		}
	} else {
		/*
		 * Drivers have the option to not allocate eeprom_shadow_ram
		 * as long as they don't perform any NVM writes.  An attempt
		 * in doing so will result in this error.
		 */
		error = -E1000_ERR_EEPROM;
	}

	em_release_software_flag(hw);

	return error;
}

/******************************************************************************
 * This function does initial flash setup so that a new read/write/erase cycle
 * can be started.
 *
 * hw - The pointer to the hw structure
 ****************************************************************************/
STATIC int32_t
em_ich8_cycle_init(struct em_hw *hw)
{
	union ich8_hws_flash_status hsfsts;
	int32_t error = E1000_ERR_EEPROM;
	int32_t i = 0;
	DEBUGFUNC("em_ich8_cycle_init");

	if (hw->mac_type >= em_pch_spt)
		hsfsts.regval = E1000_READ_ICH_FLASH_REG32(hw,
		    ICH_FLASH_HSFSTS) & 0xFFFFUL;
	else
		hsfsts.regval = E1000_READ_ICH_FLASH_REG16(hw,
		    ICH_FLASH_HSFSTS);

	/* May be check the Flash Des Valid bit in Hw status */
	if (hsfsts.hsf_status.fldesvalid == 0) {
		DEBUGOUT("Flash descriptor invalid.  SW Sequencing must be"
		    " used.");
		return error;
	}
	/* Clear FCERR in Hw status by writing 1 */
	/* Clear DAEL in Hw status by writing a 1 */
	hsfsts.hsf_status.flcerr = 1;
	hsfsts.hsf_status.dael = 1;
	if (hw->mac_type >= em_pch_spt)
		E1000_WRITE_ICH_FLASH_REG32(hw, ICH_FLASH_HSFSTS,
		    hsfsts.regval & 0xFFFFUL);
	else
		E1000_WRITE_ICH_FLASH_REG16(hw, ICH_FLASH_HSFSTS,
		    hsfsts.regval);
	/*
	 * Either we should have a hardware SPI cycle in progress bit to
	 * check against, in order to start a new cycle or FDONE bit should
	 * be changed in the hardware so that it is 1 after hardware reset,
	 * which can then be used as an indication whether a cycle is in
	 * progress or has been completed .. we should also have some
	 * software semaphore mechanism to guard FDONE or the cycle in
	 * progress bit so that two threads access to those bits can be
	 * sequentiallized or a way so that 2 threads dont start the cycle at
	 * the same time
	 */

	if (hsfsts.hsf_status.flcinprog == 0) {
		/*
		 * There is no cycle running at present, so we can start a
		 * cycle
		 */
		/* Begin by setting Flash Cycle Done. */
		hsfsts.hsf_status.flcdone = 1;
		if (hw->mac_type >= em_pch_spt)
			E1000_WRITE_ICH_FLASH_REG32(hw, ICH_FLASH_HSFSTS,
			    hsfsts.regval & 0xFFFFUL);
		else
			E1000_WRITE_ICH_FLASH_REG16(hw, ICH_FLASH_HSFSTS,
			    hsfsts.regval);
		error = E1000_SUCCESS;
	} else {
		/*
		 * otherwise poll for sometime so the current cycle has a
		 * chance to end before giving up.
		 */
		for (i = 0; i < ICH_FLASH_COMMAND_TIMEOUT; i++) {
			if (hw->mac_type >= em_pch_spt)
				hsfsts.regval = E1000_READ_ICH_FLASH_REG32(
				    hw, ICH_FLASH_HSFSTS) & 0xFFFFUL;
			else
				hsfsts.regval = E1000_READ_ICH_FLASH_REG16(
				    hw, ICH_FLASH_HSFSTS);
			if (hsfsts.hsf_status.flcinprog == 0) {
				error = E1000_SUCCESS;
				break;
			}
			usec_delay(1);
		}
		if (error == E1000_SUCCESS) {
			/*
			 * Successful in waiting for previous cycle to
			 * timeout, now set the Flash Cycle Done.
			 */
			hsfsts.hsf_status.flcdone = 1;
			if (hw->mac_type >= em_pch_spt)
				E1000_WRITE_ICH_FLASH_REG32(hw,
				    ICH_FLASH_HSFSTS, hsfsts.regval & 0xFFFFUL);
			else
				E1000_WRITE_ICH_FLASH_REG16(hw,
				    ICH_FLASH_HSFSTS, hsfsts.regval);
		} else {
			DEBUGOUT("Flash controller busy, cannot get access");
		}
	}
	return error;
}

/******************************************************************************
 * This function starts a flash cycle and waits for its completion
 *
 * hw - The pointer to the hw structure
 *****************************************************************************/
STATIC int32_t
em_ich8_flash_cycle(struct em_hw *hw, uint32_t timeout)
{
	union ich8_hws_flash_ctrl hsflctl;
	union ich8_hws_flash_status hsfsts;
	int32_t  error = E1000_ERR_EEPROM;
	uint32_t i = 0;

	/* Start a cycle by writing 1 in Flash Cycle Go in Hw Flash Control */
	if (hw->mac_type >= em_pch_spt)
		hsflctl.regval = E1000_READ_ICH_FLASH_REG32(hw,
		    ICH_FLASH_HSFSTS) >> 16;
	else
		hsflctl.regval = E1000_READ_ICH_FLASH_REG16(hw,
		    ICH_FLASH_HSFCTL);
	hsflctl.hsf_ctrl.flcgo = 1;

	if (hw->mac_type >= em_pch_spt)
		E1000_WRITE_ICH_FLASH_REG32(hw, ICH_FLASH_HSFSTS,
		    (uint32_t)hsflctl.regval << 16);
	else
		E1000_WRITE_ICH_FLASH_REG16(hw, ICH_FLASH_HSFCTL,
		    hsflctl.regval);

	/* wait till FDONE bit is set to 1 */
	do {
		if (hw->mac_type >= em_pch_spt)
			hsfsts.regval = E1000_READ_ICH_FLASH_REG32(hw,
			    ICH_FLASH_HSFSTS) & 0xFFFFUL;
		else
			hsfsts.regval = E1000_READ_ICH_FLASH_REG16(hw,
			    ICH_FLASH_HSFSTS);
		if (hsfsts.hsf_status.flcdone == 1)
			break;
		usec_delay(1);
		i++;
	} while (i < timeout);
	if (hsfsts.hsf_status.flcdone == 1 && hsfsts.hsf_status.flcerr == 0) {
		error = E1000_SUCCESS;
	}
	return error;
}

/******************************************************************************
 * Reads a byte or word from the NVM using the ICH8 flash access registers.
 *
 * hw - The pointer to the hw structure
 * index - The index of the byte or word to read.
 * size - Size of data to read, 1=byte 2=word
 * data - Pointer to the word to store the value read.
 *****************************************************************************/
STATIC int32_t
em_read_ich8_data(struct em_hw *hw, uint32_t index, uint32_t size,
    uint16_t *data)
{
	union ich8_hws_flash_status hsfsts;
	union ich8_hws_flash_ctrl hsflctl;
	uint32_t flash_linear_address;
	uint32_t flash_data = 0;
	int32_t  error = -E1000_ERR_EEPROM;
	int32_t  count = 0;
	DEBUGFUNC("em_read_ich8_data");

	if (size < 1 || size > 2 || data == 0x0 ||
	    index > ICH_FLASH_LINEAR_ADDR_MASK)
		return error;

	flash_linear_address = (ICH_FLASH_LINEAR_ADDR_MASK & index) +
	    hw->flash_base_addr;

	do {
		usec_delay(1);
		/* Steps */
		error = em_ich8_cycle_init(hw);
		if (error != E1000_SUCCESS)
			break;

		hsflctl.regval = E1000_READ_ICH_FLASH_REG16(hw,
		    ICH_FLASH_HSFCTL);
		/* 0b/1b corresponds to 1 or 2 byte size, respectively. */
		hsflctl.hsf_ctrl.fldbcount = size - 1;
		hsflctl.hsf_ctrl.flcycle = ICH_CYCLE_READ;
		E1000_WRITE_ICH_FLASH_REG16(hw, ICH_FLASH_HSFCTL,
		    hsflctl.regval);
		/*
		 * Write the last 24 bits of index into Flash Linear address
		 * field in Flash Address
		 */
		/* TODO: TBD maybe check the index against the size of flash */

		E1000_WRITE_ICH_FLASH_REG32(hw, ICH_FLASH_FADDR,
		    flash_linear_address);

		error = em_ich8_flash_cycle(hw, ICH_FLASH_COMMAND_TIMEOUT);
		/*
		 * Check if FCERR is set to 1, if set to 1, clear it and try
		 * the whole sequence a few more times, else read in (shift
		 * in) the Flash Data0, the order is least significant byte
		 * first msb to lsb
		 */
		if (error == E1000_SUCCESS) {
			flash_data = E1000_READ_ICH_FLASH_REG(hw,
			    ICH_FLASH_FDATA0);
			if (size == 1) {
				*data = (uint8_t) (flash_data & 0x000000FF);
			} else if (size == 2) {
				*data = (uint16_t) (flash_data & 0x0000FFFF);
			}
			break;
		} else {
			/*
			 * If we've gotten here, then things are probably
			 * completely hosed, but if the error condition is
			 * detected, it won't hurt to give it another
			 * try...ICH_FLASH_CYCLE_REPEAT_COUNT times.
			 */
			hsfsts.regval = E1000_READ_ICH_FLASH_REG16(hw,
			    ICH_FLASH_HSFSTS);
			if (hsfsts.hsf_status.flcerr == 1) {
				/* Repeat for some time before giving up. */
				continue;
			} else if (hsfsts.hsf_status.flcdone == 0) {
				DEBUGOUT("Timeout error - flash cycle did not"
				    " complete.");
				break;
			}
		}
	} while (count++ < ICH_FLASH_CYCLE_REPEAT_COUNT);

	return error;
}

STATIC int32_t
em_read_ich8_data32(struct em_hw *hw, uint32_t offset, uint32_t *data)
{
	union ich8_hws_flash_status hsfsts;
	union ich8_hws_flash_ctrl hsflctl;
	uint32_t flash_linear_address;
	int32_t  error = -E1000_ERR_EEPROM;
	uint32_t  count = 0;
	DEBUGFUNC("em_read_ich8_data32");

	if (hw->mac_type < em_pch_spt)
		return error;
	if (offset > ICH_FLASH_LINEAR_ADDR_MASK)
		return error;
	flash_linear_address = (ICH_FLASH_LINEAR_ADDR_MASK & offset) +
	    hw->flash_base_addr;

	do {
		usec_delay(1);
		/* Steps */
		error = em_ich8_cycle_init(hw);
		if (error != E1000_SUCCESS)
			break;

		/* 32 bit accesses in SPT. */
		hsflctl.regval = E1000_READ_ICH_FLASH_REG32(hw,
		    ICH_FLASH_HSFSTS) >> 16;

		hsflctl.hsf_ctrl.fldbcount = sizeof(uint32_t) - 1;
		hsflctl.hsf_ctrl.flcycle = ICH_CYCLE_READ;

		E1000_WRITE_ICH_FLASH_REG32(hw, ICH_FLASH_HSFSTS,
		    (uint32_t)hsflctl.regval << 16);
		/*
		 * Write the last 24 bits of offset into Flash Linear address
		 * field in Flash Address
		 */
		/* TODO: TBD maybe check the offset against the size of flash */

		E1000_WRITE_ICH_FLASH_REG32(hw, ICH_FLASH_FADDR,
		    flash_linear_address);

		error = em_ich8_flash_cycle(hw, ICH_FLASH_COMMAND_TIMEOUT);
		/*
		 * Check if FCERR is set to 1, if set to 1, clear it and try
		 * the whole sequence a few more times, else read in (shift
		 * in) the Flash Data0, the order is least significant byte
		 * first msb to lsb
		 */
		if (error == E1000_SUCCESS) {
			(*data) = (uint32_t)E1000_READ_ICH_FLASH_REG32(hw,
			    ICH_FLASH_FDATA0);
			break;
		} else {
			/*
			 * If we've gotten here, then things are probably
			 * completely hosed, but if the error condition is
			 * detected, it won't hurt to give it another
			 * try...ICH_FLASH_CYCLE_REPEAT_COUNT times.
			 */
			hsfsts.regval = E1000_READ_ICH_FLASH_REG16(hw,
			    ICH_FLASH_HSFSTS);
			if (hsfsts.hsf_status.flcerr == 1) {
				/* Repeat for some time before giving up. */
				continue;
			} else if (hsfsts.hsf_status.flcdone == 0) {
				DEBUGOUT("Timeout error - flash cycle did not"
				    " complete.");
				break;
			}
		}
	} while (count++ < ICH_FLASH_CYCLE_REPEAT_COUNT);

	return error;
}


/******************************************************************************
 * Writes One /two bytes to the NVM using the ICH8 flash access registers.
 *
 * hw - The pointer to the hw structure
 * index - The index of the byte/word to write.
 * size - Size of data to read, 1=byte 2=word
 * data - The byte(s) to write to the NVM.
 *****************************************************************************/
STATIC int32_t
em_write_ich8_data(struct em_hw *hw, uint32_t index, uint32_t size,
    uint16_t data)
{
	union ich8_hws_flash_status hsfsts;
	union ich8_hws_flash_ctrl hsflctl;
	uint32_t flash_linear_address;
	uint32_t flash_data = 0;
	int32_t  error = -E1000_ERR_EEPROM;
	int32_t  count = 0;
	DEBUGFUNC("em_write_ich8_data");

	if (hw->mac_type >= em_pch_spt)
		return -E1000_ERR_EEPROM;
	if (size < 1 || size > 2 || data > size * 0xff ||
	    index > ICH_FLASH_LINEAR_ADDR_MASK)
		return error;

	flash_linear_address = (ICH_FLASH_LINEAR_ADDR_MASK & index) +
	    hw->flash_base_addr;

	do {
		usec_delay(1);
		/* Steps */
		error = em_ich8_cycle_init(hw);
		if (error != E1000_SUCCESS)
			break;

		hsflctl.regval = E1000_READ_ICH_FLASH_REG16(hw,
		    ICH_FLASH_HSFCTL);
		/* 0b/1b corresponds to 1 or 2 byte size, respectively. */
		hsflctl.hsf_ctrl.fldbcount = size - 1;
		hsflctl.hsf_ctrl.flcycle = ICH_CYCLE_WRITE;
		E1000_WRITE_ICH_FLASH_REG16(hw, ICH_FLASH_HSFCTL,
		    hsflctl.regval);
		/*
		 * Write the last 24 bits of index into Flash Linear address
		 * field in Flash Address
		 */
		E1000_WRITE_ICH_FLASH_REG32(hw, ICH_FLASH_FADDR,
		    flash_linear_address);

		if (size == 1)
			flash_data = (uint32_t) data & 0x00FF;
		else
			flash_data = (uint32_t) data;

		E1000_WRITE_ICH_FLASH_REG32(hw, ICH_FLASH_FDATA0, flash_data);
		/*
		 * check if FCERR is set to 1 , if set to 1, clear it and try
		 * the whole sequence a few more times else done
		 */
		error = em_ich8_flash_cycle(hw, ICH_FLASH_COMMAND_TIMEOUT);
		if (error == E1000_SUCCESS) {
			break;
		} else {
			/*
			 * If we're here, then things are most likely
			 * completely hosed, but if the error condition is
			 * detected, it won't hurt to give it another
			 * try...ICH_FLASH_CYCLE_REPEAT_COUNT times.
			 */
			hsfsts.regval = E1000_READ_ICH_FLASH_REG16(hw,
			    ICH_FLASH_HSFSTS);
			if (hsfsts.hsf_status.flcerr == 1) {
				/* Repeat for some time before giving up. */
				continue;
			} else if (hsfsts.hsf_status.flcdone == 0) {
				DEBUGOUT("Timeout error - flash cycle did not"
				    " complete.");
				break;
			}
		}
	} while (count++ < ICH_FLASH_CYCLE_REPEAT_COUNT);

	return error;
}

/******************************************************************************
 * Reads a single byte from the NVM using the ICH8 flash access registers.
 *
 * hw - pointer to em_hw structure
 * index - The index of the byte to read.
 * data - Pointer to a byte to store the value read.
 *****************************************************************************/
STATIC int32_t
em_read_ich8_byte(struct em_hw *hw, uint32_t index, uint8_t *data)
{
	int32_t  status = E1000_SUCCESS;
	uint16_t word = 0;

	if (hw->mac_type >= em_pch_spt)
		return -E1000_ERR_EEPROM;
	else
		status = em_read_ich8_data(hw, index, 1, &word);
	if (status == E1000_SUCCESS) {
		*data = (uint8_t) word;
	}
	return status;
}

/******************************************************************************
 * Writes a single byte to the NVM using the ICH8 flash access registers.
 * Performs verification by reading back the value and then going through
 * a retry algorithm before giving up.
 *
 * hw - pointer to em_hw structure
 * index - The index of the byte to write.
 * byte - The byte to write to the NVM.
 *****************************************************************************/
STATIC int32_t
em_verify_write_ich8_byte(struct em_hw *hw, uint32_t index, uint8_t byte)
{
	int32_t error = E1000_SUCCESS;
	int32_t program_retries = 0;
	DEBUGOUT2("Byte := %2.2X Offset := %d\n", byte, index);

	error = em_write_ich8_byte(hw, index, byte);

	if (error != E1000_SUCCESS) {
		for (program_retries = 0; program_retries < 100;
		    program_retries++) {
			DEBUGOUT2("Retrying \t Byte := %2.2X Offset := %d\n",
			    byte, index);
			error = em_write_ich8_byte(hw, index, byte);
			usec_delay(100);
			if (error == E1000_SUCCESS)
				break;
		}
	}
	if (program_retries == 100)
		error = E1000_ERR_EEPROM;

	return error;
}

/******************************************************************************
 * Writes a single byte to the NVM using the ICH8 flash access registers.
 *
 * hw - pointer to em_hw structure
 * index - The index of the byte to read.
 * data - The byte to write to the NVM.
 *****************************************************************************/
STATIC int32_t
em_write_ich8_byte(struct em_hw *hw, uint32_t index, uint8_t data)
{
	int32_t  status = E1000_SUCCESS;
	uint16_t word = (uint16_t) data;
	status = em_write_ich8_data(hw, index, 1, word);

	return status;
}

/******************************************************************************
 * Reads a dword from the NVM using the ICH8 flash access registers.
 *
 * hw - pointer to em_hw structure
 * index - The starting BYTE index of the word to read.
 * data - Pointer to a word to store the value read.
 *****************************************************************************/
STATIC int32_t
em_read_ich8_dword(struct em_hw *hw, uint32_t index, uint32_t *data)
{
	int32_t status = E1000_SUCCESS;
	status = em_read_ich8_data32(hw, index, data);
	return status;
}

/******************************************************************************
 * Reads a word from the NVM using the ICH8 flash access registers.
 *
 * hw - pointer to em_hw structure
 * index - The starting byte index of the word to read.
 * data - Pointer to a word to store the value read.
 *****************************************************************************/
STATIC int32_t
em_read_ich8_word(struct em_hw *hw, uint32_t index, uint16_t *data)
{
	int32_t status = E1000_SUCCESS;
	status = em_read_ich8_data(hw, index, 2, data);
	return status;
}

/******************************************************************************
 * Erases the bank specified. Each bank may be a 4, 8 or 64k block. Banks are 0
 * based.
 *
 * hw - pointer to em_hw structure
 * bank - 0 for first bank, 1 for second bank
 *
 * Note that this function may actually erase as much as 8 or 64 KBytes.  The
 * amount of NVM used in each bank is a *minimum* of 4 KBytes, but in fact the
 * bank size may be 4, 8 or 64 KBytes
 *****************************************************************************/
int32_t
em_erase_ich8_4k_segment(struct em_hw *hw, uint32_t bank)
{
	union ich8_hws_flash_status hsfsts;
	union ich8_hws_flash_ctrl hsflctl;
	uint32_t flash_linear_address;
	int32_t  count = 0;
	int32_t  error = E1000_ERR_EEPROM;
	int32_t  iteration;
	int32_t  sub_sector_size = 0;
	int32_t  bank_size;
	int32_t  j = 0;
	int32_t  error_flag = 0;
	hsfsts.regval = E1000_READ_ICH_FLASH_REG16(hw, ICH_FLASH_HSFSTS);
	/*
	 * Determine HW Sector size: Read BERASE bits of Hw flash Status
	 * register
	 */
	/*
	 * 00: The Hw sector is 256 bytes, hence we need to erase 16
	 * consecutive sectors.  The start index for the nth Hw sector can be
	 * calculated as bank * 4096 + n * 256 01: The Hw sector is 4K bytes,
	 * hence we need to erase 1 sector. The start index for the nth Hw
	 * sector can be calculated as bank * 4096 10: The HW sector is 8K
	 * bytes 11: The Hw sector size is 64K bytes
	 */
	if (hsfsts.hsf_status.berasesz == 0x0) {
		/* Hw sector size 256 */
		sub_sector_size = ICH_FLASH_SEG_SIZE_256;
		bank_size = ICH_FLASH_SECTOR_SIZE;
		iteration = ICH_FLASH_SECTOR_SIZE / ICH_FLASH_SEG_SIZE_256;
	} else if (hsfsts.hsf_status.berasesz == 0x1) {
		bank_size = ICH_FLASH_SEG_SIZE_4K;
		iteration = 1;
	} else if (hsfsts.hsf_status.berasesz == 0x2) {
		if (hw->mac_type == em_ich9lan) {
			uint32_t gfpreg, sector_base_addr, sector_end_addr;
			gfpreg = E1000_READ_ICH_FLASH_REG(hw,
			    ICH_FLASH_GFPREG);
			/*
		         * sector_X_addr is a "sector"-aligned address (4096 bytes)
		         * Add 1 to sector_end_addr since this sector is included in
		         * the overall size.
		         */
			sector_base_addr = gfpreg & ICH_GFPREG_BASE_MASK;
			sector_end_addr = 
			    ((gfpreg >> 16) & ICH_GFPREG_BASE_MASK) + 1;

			/*
		         * find total size of the NVM, then cut in half since the total
		         * size represents two separate NVM banks.
		         */
			bank_size = (sector_end_addr - sector_base_addr)
			    << ICH_FLASH_SECT_ADDR_SHIFT;
			bank_size /= 2;
			/* Word align */
			bank_size = 
			    (bank_size / sizeof(uint16_t)) * sizeof(uint16_t);

			sub_sector_size = ICH_FLASH_SEG_SIZE_8K;
			iteration = bank_size / ICH_FLASH_SEG_SIZE_8K;
		} else {
			return error;
		}
	} else if (hsfsts.hsf_status.berasesz == 0x3) {
		bank_size = ICH_FLASH_SEG_SIZE_64K;
		iteration = 1;
	} else {
		return error;
	}

	for (j = 0; j < iteration; j++) {
		do {
			count++;
			/* Steps */
			error = em_ich8_cycle_init(hw);
			if (error != E1000_SUCCESS) {
				error_flag = 1;
				break;
			}
			/*
			 * Write a value 11 (block Erase) in Flash Cycle
			 * field in Hw flash Control
			 */
			hsflctl.regval = E1000_READ_ICH_FLASH_REG16(hw,
			    ICH_FLASH_HSFCTL);
			hsflctl.hsf_ctrl.flcycle = ICH_CYCLE_ERASE;
			E1000_WRITE_ICH_FLASH_REG16(hw, ICH_FLASH_HSFCTL,
			    hsflctl.regval);
			/*
			 * Write the last 24 bits of an index within the
			 * block into Flash Linear address field in Flash
			 * Address.  This probably needs to be calculated
			 * here based off the on-chip erase sector size and
			 * the software bank size (4, 8 or 64 KBytes)
			 */
			flash_linear_address = 
			    bank * bank_size + j * sub_sector_size;
			flash_linear_address += hw->flash_base_addr;
			flash_linear_address &= ICH_FLASH_LINEAR_ADDR_MASK;

			E1000_WRITE_ICH_FLASH_REG32(hw, ICH_FLASH_FADDR,
			    flash_linear_address);

			error =
			    em_ich8_flash_cycle(hw, ICH_FLASH_ERASE_TIMEOUT);
			/*
			 * Check if FCERR is set to 1.  If 1, clear it and
			 * try the whole sequence a few more times else Done
			 */
			if (error == E1000_SUCCESS) {
				break;
			} else {
				hsfsts.regval = E1000_READ_ICH_FLASH_REG16(hw,
				    ICH_FLASH_HSFSTS);
				if (hsfsts.hsf_status.flcerr == 1) {
					/*
					 * repeat for some time before giving
					 * up
					 */
					continue;
				} else if (hsfsts.hsf_status.flcdone == 0) {
					error_flag = 1;
					break;
				}
			}
		} while ((count < ICH_FLASH_CYCLE_REPEAT_COUNT) && !error_flag);
		if (error_flag == 1)
			break;
	}
	if (error_flag != 1)
		error = E1000_SUCCESS;
	return error;
}

/******************************************************************************
 * Reads 16-bit words from the OTP. Return error when the word is not
 * stored in OTP.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset of word in the OTP to read
 * data - word read from the OTP
 * words - number of words to read
 *****************************************************************************/
STATIC int32_t
em_read_invm_i210(struct em_hw *hw, uint16_t offset, uint16_t words,
    uint16_t *data)
{
	int32_t  ret_val = E1000_SUCCESS;

	switch (offset)
	{
	case EEPROM_MAC_ADDR_WORD0:
	case EEPROM_MAC_ADDR_WORD1:
	case EEPROM_MAC_ADDR_WORD2:
		/* Generate random MAC address if there's none. */
		ret_val = em_read_invm_word_i210(hw, offset, data);
		if (ret_val != E1000_SUCCESS) {
			DEBUGOUT("MAC Addr not found in iNVM\n");
			*data = 0xFFFF;
			ret_val = E1000_SUCCESS;
		}
		break;
	case EEPROM_INIT_CONTROL2_REG:
		ret_val = em_read_invm_word_i210(hw, offset, data);
		if (ret_val != E1000_SUCCESS) {
			*data = NVM_INIT_CTRL_2_DEFAULT_I211;
			ret_val = E1000_SUCCESS;
		}
		break;
	case EEPROM_INIT_CONTROL4_REG:
		ret_val = em_read_invm_word_i210(hw, offset, data);
		if (ret_val != E1000_SUCCESS) {
			*data = NVM_INIT_CTRL_4_DEFAULT_I211;
			ret_val = E1000_SUCCESS;
		}
		break;
	case EEPROM_LED_1_CFG:
		ret_val = em_read_invm_word_i210(hw, offset, data);
		if (ret_val != E1000_SUCCESS) {
			*data = NVM_LED_1_CFG_DEFAULT_I211;
			ret_val = E1000_SUCCESS;
		}
		break;
	case EEPROM_LED_0_2_CFG:
		ret_val = em_read_invm_word_i210(hw, offset, data);
		if (ret_val != E1000_SUCCESS) {
			*data = NVM_LED_0_2_CFG_DEFAULT_I211;
			ret_val = E1000_SUCCESS;
		}
		break;
	case EEPROM_ID_LED_SETTINGS:
		ret_val = em_read_invm_word_i210(hw, offset, data);
		if (ret_val != E1000_SUCCESS) {
			*data = ID_LED_RESERVED_FFFF;
			ret_val = E1000_SUCCESS;
		}
		break;
	default:
		DEBUGOUT1("NVM word 0x%02x is not mapped.\n", offset);
		*data = NVM_RESERVED_WORD;
		break;
	}

	return ret_val;
}

/******************************************************************************
 * Reads 16-bit words from the OTP. Return error when the word is not
 * stored in OTP.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset of word in the OTP to read
 * data - word read from the OTP
 *****************************************************************************/
STATIC int32_t
em_read_invm_word_i210(struct em_hw *hw, uint16_t address, uint16_t *data)
{
	int32_t  error = -E1000_NOT_IMPLEMENTED;
	uint32_t invm_dword;
	uint16_t i;
	uint8_t record_type, word_address;

	for (i = 0; i < INVM_SIZE; i++) {
		invm_dword = EM_READ_REG(hw, E1000_INVM_DATA_REG(i));
		/* Get record type */
		record_type = INVM_DWORD_TO_RECORD_TYPE(invm_dword);
		if (record_type == INVM_UNINITIALIZED_STRUCTURE)
			break;
		if (record_type == INVM_CSR_AUTOLOAD_STRUCTURE)
			i += INVM_CSR_AUTOLOAD_DATA_SIZE_IN_DWORDS;
		if (record_type == INVM_RSA_KEY_SHA256_STRUCTURE)
			i += INVM_RSA_KEY_SHA256_DATA_SIZE_IN_DWORDS;
		if (record_type == INVM_WORD_AUTOLOAD_STRUCTURE) {
			word_address = INVM_DWORD_TO_WORD_ADDRESS(invm_dword);
			if (word_address == address) {
				*data = INVM_DWORD_TO_WORD_DATA(invm_dword);
				error = E1000_SUCCESS;
				break;
			}
		}
	}

	return error;
}

STATIC int32_t
em_init_lcd_from_nvm_config_region(struct em_hw *hw, uint32_t cnf_base_addr,
    uint32_t cnf_size)
{
	uint32_t ret_val = E1000_SUCCESS;
	uint16_t word_addr, reg_data, reg_addr;
	uint16_t i;
	/* cnf_base_addr is in DWORD */
	word_addr = (uint16_t) (cnf_base_addr << 1);

	/* cnf_size is returned in size of dwords */
	for (i = 0; i < cnf_size; i++) {
		ret_val =
		    em_read_eeprom(hw, (word_addr + i * 2), 1, &reg_data);
		if (ret_val)
			return ret_val;

		ret_val =
		    em_read_eeprom(hw, (word_addr + i * 2 + 1), 1, &reg_addr);
		if (ret_val)
			return ret_val;

		ret_val = em_get_software_flag(hw);
		if (ret_val != E1000_SUCCESS)
			return ret_val;

		ret_val =
		    em_write_phy_reg_ex(hw, (uint32_t) reg_addr, reg_data);

		em_release_software_flag(hw);
	}

	return ret_val;
}

/******************************************************************************
 * This function initializes the PHY from the NVM on ICH8 platforms. This
 * is needed due to an issue where the NVM configuration is not properly
 * autoloaded after power transitions. Therefore, after each PHY reset, we
 * will load the configuration data out of the NVM manually.
 *
 * hw: Struct containing variables accessed by shared code
 *****************************************************************************/
STATIC int32_t
em_init_lcd_from_nvm(struct em_hw *hw)
{
	uint32_t reg_data, cnf_base_addr, cnf_size, ret_val, loop, sw_cfg_mask;
	if (hw->phy_type != em_phy_igp_3)
		return E1000_SUCCESS;

	/* Check if SW needs configure the PHY */
	if (hw->device_id == E1000_DEV_ID_ICH8_IGP_M_AMT ||
	    hw->device_id == E1000_DEV_ID_ICH8_IGP_M ||
	    hw->mac_type == em_pchlan ||
	    hw->mac_type == em_pch2lan ||
	    hw->mac_type == em_pch_lpt ||
	    hw->mac_type == em_pch_spt ||
	    hw->mac_type == em_pch_cnp ||
	    hw->mac_type == em_pch_tgp ||
	    hw->mac_type == em_pch_adp)
		sw_cfg_mask = FEXTNVM_SW_CONFIG_ICH8M;
	else
		sw_cfg_mask = FEXTNVM_SW_CONFIG;

	reg_data = E1000_READ_REG(hw, FEXTNVM);
	if (!(reg_data & sw_cfg_mask))
		return E1000_SUCCESS;

	/* Wait for basic configuration completes before proceeding */
	loop = 0;
	do {
		reg_data =
		    E1000_READ_REG(hw, STATUS) & E1000_STATUS_LAN_INIT_DONE;
		usec_delay(100);
		loop++;
	} while ((!reg_data) && (loop < 50));

	/* Clear the Init Done bit for the next init event */
	reg_data = E1000_READ_REG(hw, STATUS);
	reg_data &= ~E1000_STATUS_LAN_INIT_DONE;
	E1000_WRITE_REG(hw, STATUS, reg_data);
	/*
	 * Make sure HW does not configure LCD from PHY extended
	 * configuration before SW configuration
	 */
	reg_data = E1000_READ_REG(hw, EXTCNF_CTRL);
	if ((reg_data & E1000_EXTCNF_CTRL_LCD_WRITE_ENABLE) == 0x0000) {
		reg_data = E1000_READ_REG(hw, EXTCNF_SIZE);
		cnf_size = reg_data & E1000_EXTCNF_SIZE_EXT_PCIE_LENGTH;
		cnf_size >>= 16;
		if (cnf_size) {
			reg_data = E1000_READ_REG(hw, EXTCNF_CTRL);
			cnf_base_addr = reg_data & E1000_EXTCNF_CTRL_EXT_CNF_POINTER;
			/* cnf_base_addr is in DWORD */
			cnf_base_addr >>= 16;

			/* Configure LCD from extended configuration region. */
			ret_val = em_init_lcd_from_nvm_config_region(hw,
			    cnf_base_addr, cnf_size);
			if (ret_val)
				return ret_val;
		}
	}
	return E1000_SUCCESS;
}

/******************************************************************************
 *  em_set_pciex_completion_timeout - set pci-e completion timeout
 *
 *  The defaults for 82575 and 82576 should be in the range of 50us to 50ms,
 *  however the hardware default for these parts is 500us to 1ms which is less
 *  than the 10ms recommended by the pci-e spec.  To address this we need to
 *  increase the value to either 10ms to 200ms for capability version 1 config,
 *  or 16ms to 55ms for version 2.
 *
 *  * hw - pointer to em_hw structure
 *****************************************************************************/
int32_t
em_set_pciex_completion_timeout(struct em_hw *hw)
{
	uint32_t gcr = E1000_READ_REG(hw, GCR);
	int32_t ret_val = E1000_SUCCESS;

	/* Only take action if timeout value is not set by system BIOS */
	if (gcr & E1000_GCR_CMPL_TMOUT_MASK)
		goto out;

	DEBUGOUT("PCIe completion timeout not set by system BIOS.");

	/*
	 * If capabilities version is type 1 we can write the
	 * timeout of 10ms to 200ms through the GCR register
	 */

	if (!(gcr & E1000_GCR_CAP_VER2)) {
		gcr |= E1000_GCR_CMPL_TMOUT_10ms;
		DEBUGOUT("PCIe capability version 1 detected, setting \
		    completion timeout to 10ms.");
		goto out;
	}

	/*
	 * For version 2 capabilities we need to write the config space
	 * directly in order to set the completion timeout value for
	 * 16ms to 55ms
	 *
	 * XXX: Implement em_*_pcie_cap_reg() first.
	 */
#if 0
	ret_val = em_read_pcie_cap_reg(hw, PCIE_DEVICE_CONTROL2,
	    &pciex_devctl2);

	if (ret_val)
		goto out;

	pciex_devctl2 |= PCIE_DEVICE_CONTROL2_16ms;

	ret_val = em_write_pcie_cap_reg(hw, PCIE_DEVICE_CONTROL2,
	    &pciex_devctl2); 
#endif

out:

	/* Disable completion timeout resend */
	gcr &= ~E1000_GCR_CMPL_TMOUT_RESEND;

	DEBUGOUT("PCIe completion timeout resend disabled.");

	E1000_WRITE_REG(hw, GCR, gcr);
	return ret_val;
}

/***************************************************************************
 *  Set slow MDIO access mode
 ***************************************************************************/
static int32_t
em_set_mdio_slow_mode_hv(struct em_hw *hw)
{
	int32_t ret_val;
	uint16_t data;
	DEBUGFUNC("em_set_mdio_slow_mode_hv");

	ret_val = em_read_phy_reg(hw, HV_KMRN_MODE_CTRL, &data);
	if (ret_val)
		return ret_val;

	data |= HV_KMRN_MDIO_SLOW;

	ret_val = em_write_phy_reg(hw, HV_KMRN_MODE_CTRL, data);

	return ret_val;
}

/***************************************************************************
 *  A series of Phy workarounds to be done after every PHY reset.
 ***************************************************************************/
int32_t
em_hv_phy_workarounds_ich8lan(struct em_hw *hw)
{
	int32_t ret_val = E1000_SUCCESS;
	uint16_t phy_data;
	uint16_t swfw;
	DEBUGFUNC("em_hv_phy_workarounds_ich8lan");

	if (hw->mac_type != em_pchlan)
		goto out;

	swfw = E1000_SWFW_PHY0_SM;

	/* Set MDIO slow mode before any other MDIO access */
	if (hw->phy_type == em_phy_82577 ||
	    hw->phy_type == em_phy_82578) {
		ret_val = em_set_mdio_slow_mode_hv(hw);
		if (ret_val)
			goto out;
	}

	/* Hanksville M Phy init for IEEE. */
	if ((hw->revision_id == 2) &&
	    (hw->phy_type == em_phy_82577) &&
	    ((hw->phy_revision == 2) || (hw->phy_revision == 3))) {
		em_write_phy_reg(hw, 0x10, 0x8823);
		em_write_phy_reg(hw, 0x11, 0x0018);
		em_write_phy_reg(hw, 0x10, 0x8824);
		em_write_phy_reg(hw, 0x11, 0x0016);
		em_write_phy_reg(hw, 0x10, 0x8825);
		em_write_phy_reg(hw, 0x11, 0x001A);
		em_write_phy_reg(hw, 0x10, 0x888C);
		em_write_phy_reg(hw, 0x11, 0x0007);
		em_write_phy_reg(hw, 0x10, 0x888D);
		em_write_phy_reg(hw, 0x11, 0x0007);
		em_write_phy_reg(hw, 0x10, 0x888E);
		em_write_phy_reg(hw, 0x11, 0x0007);
		em_write_phy_reg(hw, 0x10, 0x8827);
		em_write_phy_reg(hw, 0x11, 0x0001);
		em_write_phy_reg(hw, 0x10, 0x8835);
		em_write_phy_reg(hw, 0x11, 0x0001);
		em_write_phy_reg(hw, 0x10, 0x8834);
		em_write_phy_reg(hw, 0x11, 0x0001);
		em_write_phy_reg(hw, 0x10, 0x8833);
		em_write_phy_reg(hw, 0x11, 0x0002);
	}

	if (((hw->phy_type == em_phy_82577) &&
	     ((hw->phy_revision == 1) || (hw->phy_revision == 2))) ||
	    ((hw->phy_type == em_phy_82578) && (hw->phy_revision == 1))) {
		/* Disable generation of early preamble */
		ret_val = em_write_phy_reg(hw, PHY_REG(769, 25), 0x4431);
		if (ret_val)
			goto out;

		/* Preamble tuning for SSC */
		ret_val = em_write_phy_reg(hw, PHY_REG(770, 16), 0xA204);
		if (ret_val)
			goto out;
	}

	if (hw->phy_type == em_phy_82578) {
		/*
		 * Return registers to default by doing a soft reset then
		 * writing 0x3140 to the control register.
		 */
		if (hw->phy_revision < 2) {
			em_phy_reset(hw);
			ret_val = em_write_phy_reg(hw, PHY_CTRL, 0x3140);
		}
	}

	if ((hw->revision_id == 2) &&
	    (hw->phy_type == em_phy_82577) &&
	    ((hw->phy_revision == 2) || (hw->phy_revision == 3))) {
		/*
		 * Workaround for OEM (GbE) not operating after reset -
		 * restart AN (twice)
		 */
		ret_val = em_write_phy_reg(hw, PHY_REG(0, 25), 0x0400);
		if (ret_val)
			goto out;
		ret_val = em_write_phy_reg(hw, PHY_REG(0, 25), 0x0400);
		if (ret_val)
			goto out;
	}

	/* Select page 0 */
	ret_val = em_swfw_sync_acquire(hw, swfw);
	if (ret_val)
		goto out;

	hw->phy_addr = 1;
	ret_val = em_write_phy_reg(hw, IGP01E1000_PHY_PAGE_SELECT, 0);
	em_swfw_sync_release(hw, swfw);
	if (ret_val)
		goto out;

	/* Workaround for link disconnects on a busy hub in half duplex */
	ret_val = em_read_phy_reg(hw,
	                                      PHY_REG(BM_PORT_CTRL_PAGE, 17),
	                                      &phy_data);
	if (ret_val)
		goto release;
	ret_val = em_write_phy_reg(hw,
	                                       PHY_REG(BM_PORT_CTRL_PAGE, 17),
	                                       phy_data & 0x00FF);
release:
out:
	return ret_val;
}


/***************************************************************************
 *  Si workaround
 *
 *  This function works around a Si bug where the link partner can get
 *  a link up indication before the PHY does.  If small packets are sent
 *  by the link partner they can be placed in the packet buffer without
 *  being properly accounted for by the PHY and will stall preventing
 *  further packets from being received.  The workaround is to clear the
 *  packet buffer after the PHY detects link up.
 ***************************************************************************/
int32_t
em_link_stall_workaround_hv(struct em_hw *hw)
{
	int32_t ret_val = E1000_SUCCESS;
	uint16_t phy_data;

	if (hw->phy_type != em_phy_82578)
		goto out;

	/* Do not apply workaround if in PHY loopback bit 14 set */
	em_read_phy_reg(hw, PHY_CTRL, &phy_data);
	if (phy_data & E1000_PHY_CTRL_LOOPBACK)
		goto out;

	/* check if link is up and at 1Gbps */
	ret_val = em_read_phy_reg(hw, BM_CS_STATUS, &phy_data);
	if (ret_val)
		goto out;

	phy_data &= BM_CS_STATUS_LINK_UP |
		    BM_CS_STATUS_RESOLVED |
		    BM_CS_STATUS_SPEED_MASK;

	if (phy_data != (BM_CS_STATUS_LINK_UP |
			 BM_CS_STATUS_RESOLVED |
			 BM_CS_STATUS_SPEED_1000))
		goto out;

	msec_delay(200);

	/* flush the packets in the fifo buffer */
	ret_val = em_write_phy_reg(hw, HV_MUX_DATA_CTRL,
	    HV_MUX_DATA_CTRL_GEN_TO_MAC | HV_MUX_DATA_CTRL_FORCE_SPEED);
	if (ret_val)
		goto out;

	ret_val = em_write_phy_reg(hw, HV_MUX_DATA_CTRL,
	    HV_MUX_DATA_CTRL_GEN_TO_MAC);

out:
	return ret_val;
}

/****************************************************************************
 *  K1 Si workaround
 *
 *  If K1 is enabled for 1Gbps, the MAC might stall when transitioning
 *  from a lower speed.  This workaround disables K1 whenever link is at 1Gig.
 *  If link is down, the function will restore the default K1 setting located
 *  in the NVM.
 ****************************************************************************/
int32_t
em_k1_gig_workaround_hv(struct em_hw *hw, boolean_t link)
{
	int32_t ret_val;
	uint16_t phy_data;
	boolean_t k1_enable;

	DEBUGFUNC("em_k1_gig_workaround_hv");

	if (hw->mac_type != em_pchlan)
		return E1000_SUCCESS;

	ret_val = em_read_eeprom_ich8(hw, E1000_NVM_K1_CONFIG, 1, &phy_data);
	if (ret_val)
		return ret_val;

	k1_enable = phy_data & E1000_NVM_K1_ENABLE ? TRUE : FALSE;

	/* Disable K1 when link is 1Gbps, otherwise use the NVM setting */
	if (link) {
		if (hw->phy_type == em_phy_82578) {
			ret_val = em_read_phy_reg(hw, BM_CS_STATUS,
			    &phy_data);
			if (ret_val)
				return ret_val;

			phy_data &= BM_CS_STATUS_LINK_UP |
				    BM_CS_STATUS_RESOLVED |
				    BM_CS_STATUS_SPEED_MASK;

			if (phy_data == (BM_CS_STATUS_LINK_UP |
					 BM_CS_STATUS_RESOLVED |
					 BM_CS_STATUS_SPEED_1000))
				k1_enable = FALSE;
		}

		if (hw->phy_type == em_phy_82577) {
			ret_val = em_read_phy_reg(hw, HV_M_STATUS,
			    &phy_data);
			if (ret_val)
				return ret_val;

			phy_data &= HV_M_STATUS_LINK_UP |
				    HV_M_STATUS_AUTONEG_COMPLETE |
				    HV_M_STATUS_SPEED_MASK;

			if (phy_data == (HV_M_STATUS_LINK_UP |
					 HV_M_STATUS_AUTONEG_COMPLETE |
					 HV_M_STATUS_SPEED_1000))
				k1_enable = FALSE;
		}

		/* Link stall fix for link up */
		ret_val = em_write_phy_reg(hw, PHY_REG(770, 19),
		    0x0100);
		if (ret_val)
			return ret_val;

	} else {
		/* Link stall fix for link down */
		ret_val = em_write_phy_reg(hw, PHY_REG(770, 19),
		    0x4100);
		if (ret_val)
			return ret_val;
	}

	ret_val = em_configure_k1_ich8lan(hw, k1_enable);

	return ret_val;
}

/* Workaround to set the K1 beacon duration for 82579 parts */
int32_t
em_k1_workaround_lv(struct em_hw *hw)
{
	int32_t ret_val;
	uint16_t phy_data;
	uint32_t mac_reg;

	ret_val = em_read_phy_reg(hw, BM_CS_STATUS, &phy_data);
	if (ret_val)
		return ret_val;

	if ((phy_data & (HV_M_STATUS_LINK_UP | HV_M_STATUS_AUTONEG_COMPLETE))
	    == (HV_M_STATUS_LINK_UP | HV_M_STATUS_AUTONEG_COMPLETE)) {
		mac_reg = E1000_READ_REG(hw, FEXTNVM4);
		mac_reg &= ~E1000_FEXTNVM4_BEACON_DURATION_MASK;

		if (phy_data & HV_M_STATUS_SPEED_1000)
			mac_reg |= E1000_FEXTNVM4_BEACON_DURATION_8USEC;
		else
			mac_reg |= E1000_FEXTNVM4_BEACON_DURATION_16USEC;

		E1000_WRITE_REG(hw, FEXTNVM4, mac_reg);
	}
	
	return E1000_SUCCESS;
}

/**
 *  em_k1_workaround_lpt_lp - K1 workaround on Lynxpoint-LP
 *
 *  When K1 is enabled for 1Gbps, the MAC can miss 2 DMA completion indications
 *  preventing further DMA write requests.  Workaround the issue by disabling
 *  the de-assertion of the clock request when in 1Gbps mode.
 *  Also, set appropriate Tx re-transmission timeouts for 10 and 100Half link
 *  speeds in order to avoid Tx hangs.
 **/
int32_t
em_k1_workaround_lpt_lp(struct em_hw *hw, boolean_t link)
{
	uint32_t fextnvm6 = E1000_READ_REG(hw, FEXTNVM6);
	uint32_t status = E1000_READ_REG(hw, STATUS);
	int32_t ret_val = E1000_SUCCESS;
	uint16_t reg;

	if (link && (status & E1000_STATUS_SPEED_1000)) {
		ret_val = em_read_kmrn_reg(hw, E1000_KMRNCTRLSTA_K1_CONFIG,
		    &reg);
		if (ret_val)
			return ret_val;

		ret_val = em_write_kmrn_reg(hw, E1000_KMRNCTRLSTA_K1_CONFIG,
		    reg & ~E1000_KMRNCTRLSTA_K1_ENABLE);
		if (ret_val)
			return ret_val;

		usec_delay(10);

		E1000_WRITE_REG(hw, FEXTNVM6,
				fextnvm6 | E1000_FEXTNVM6_REQ_PLL_CLK);

		ret_val = em_write_kmrn_reg(hw, E1000_KMRNCTRLSTA_K1_CONFIG,
		    reg);
	} else {
		/* clear FEXTNVM6 bit 8 on link down or 10/100 */
		fextnvm6 &= ~E1000_FEXTNVM6_REQ_PLL_CLK;

		if (!link || ((status & E1000_STATUS_SPEED_100) &&
			      (status & E1000_STATUS_FD)))
			goto update_fextnvm6;

		ret_val = em_read_phy_reg(hw, I217_INBAND_CTRL, &reg);
		if (ret_val)
			return ret_val;

		/* Clear link status transmit timeout */
		reg &= ~I217_INBAND_CTRL_LINK_STAT_TX_TIMEOUT_MASK;

		if (status & E1000_STATUS_SPEED_100) {
			/* Set inband Tx timeout to 5x10us for 100Half */
			reg |= 5 << I217_INBAND_CTRL_LINK_STAT_TX_TIMEOUT_SHIFT;

			/* Do not extend the K1 entry latency for 100Half */
			fextnvm6 &= ~E1000_FEXTNVM6_ENABLE_K1_ENTRY_CONDITION;
		} else {
			/* Set inband Tx timeout to 50x10us for 10Full/Half */
			reg |= 50 <<
			       I217_INBAND_CTRL_LINK_STAT_TX_TIMEOUT_SHIFT;

			/* Extend the K1 entry latency for 10 Mbps */
			fextnvm6 |= E1000_FEXTNVM6_ENABLE_K1_ENTRY_CONDITION;
		}

		ret_val = em_write_phy_reg(hw, I217_INBAND_CTRL, reg);
		if (ret_val)
			return ret_val;

update_fextnvm6:
		E1000_WRITE_REG(hw, FEXTNVM6, fextnvm6);
	}

	return ret_val;

}


/***************************************************************************
 *  e1000_gate_hw_phy_config_ich8lan - disable PHY config via hardware
 *  @hw:   pointer to the HW structure
 *  @gate: boolean set to TRUE to gate, FALSE to ungate
 *
 *  Gate/ungate the automatic PHY configuration via hardware; perform
 *  the configuration via software instead.
 ***************************************************************************/
void
em_gate_hw_phy_config_ich8lan(struct em_hw *hw, boolean_t gate)
{
       uint32_t extcnf_ctrl;

       DEBUGFUNC("em_gate_hw_phy_config_ich8lan");

       if (hw->mac_type != em_pch2lan)
               return;

       extcnf_ctrl = E1000_READ_REG(hw, EXTCNF_CTRL);

       if (gate)
               extcnf_ctrl |= E1000_EXTCNF_CTRL_GATE_PHY_CFG;
       else
               extcnf_ctrl &= ~E1000_EXTCNF_CTRL_GATE_PHY_CFG;

       E1000_WRITE_REG(hw, EXTCNF_CTRL, extcnf_ctrl);
}

/***************************************************************************
 *  Configure K1 power state
 *
 *  Configure the K1 power state based on the provided parameter.
 *  Assumes semaphore already acquired.
 *
 *  Success returns 0, Failure returns -E1000_ERR_PHY (-2)
 ***************************************************************************/
int32_t
em_configure_k1_ich8lan(struct em_hw *hw, boolean_t k1_enable)
{
	int32_t ret_val = E1000_SUCCESS;
	uint32_t ctrl_reg = 0;
	uint32_t ctrl_ext = 0;
	uint32_t reg = 0;
	uint16_t kmrn_reg = 0;

	ret_val = em_read_kmrn_reg(hw, E1000_KMRNCTRLSTA_K1_CONFIG,
	    &kmrn_reg);
	if (ret_val)
		goto out;

	if (k1_enable)
		kmrn_reg |= E1000_KMRNCTRLSTA_K1_ENABLE;
	else
		kmrn_reg &= ~E1000_KMRNCTRLSTA_K1_ENABLE;

	ret_val = em_write_kmrn_reg(hw, E1000_KMRNCTRLSTA_K1_CONFIG,
	    kmrn_reg);
	if (ret_val)
		goto out;

	usec_delay(20);
	ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
	ctrl_reg = E1000_READ_REG(hw, CTRL);

	reg = ctrl_reg & ~(E1000_CTRL_SPD_1000 | E1000_CTRL_SPD_100);
	reg |= E1000_CTRL_FRCSPD;
	E1000_WRITE_REG(hw, CTRL, reg);

	E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext | E1000_CTRL_EXT_SPD_BYPS);
	usec_delay(20);
	E1000_WRITE_REG(hw, CTRL, ctrl_reg);
	E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
	usec_delay(20);

out:
	return ret_val;
}

/***************************************************************************
 *  em_lv_phy_workarounds_ich8lan - A series of Phy workarounds to be
 *  done after every PHY reset.
 ***************************************************************************/
int32_t
em_lv_phy_workarounds_ich8lan(struct em_hw *hw)
{
       int32_t ret_val = E1000_SUCCESS;
       uint16_t swfw;

       DEBUGFUNC("e1000_lv_phy_workarounds_ich8lan");

       if (hw->mac_type != em_pch2lan)
               goto out;

       /* Set MDIO slow mode before any other MDIO access */
       ret_val = em_set_mdio_slow_mode_hv(hw);

       swfw = E1000_SWFW_PHY0_SM;
       ret_val = em_swfw_sync_acquire(hw, swfw);
       if (ret_val)
               goto out;
       ret_val = em_write_phy_reg(hw, I82579_EMI_ADDR,
                                              I82579_MSE_THRESHOLD);
       if (ret_val)
               goto release;
       /* set MSE higher to enable link to stay up when noise is high */
       ret_val = em_write_phy_reg(hw, I82579_EMI_DATA,
                                              0x0034);
       if (ret_val)
               goto release;
       ret_val = em_write_phy_reg(hw, I82579_EMI_ADDR,
                                              I82579_MSE_LINK_DOWN);
       if (ret_val)
               goto release;
       /* drop link after 5 times MSE threshold was reached */
       ret_val = em_write_phy_reg(hw, I82579_EMI_DATA,
                                              0x0005);
release:
       em_swfw_sync_release(hw, swfw);

out:
       return ret_val;
}

int32_t
em_set_eee_i350(struct em_hw *hw) 
{
	int32_t ret_val = E1000_SUCCESS;
	uint32_t ipcnfg, eeer;

	if ((hw->mac_type < em_i350) ||
	    (hw->media_type != em_media_type_copper))
		goto out;
	ipcnfg = EM_READ_REG(hw, E1000_IPCNFG);
	eeer = EM_READ_REG(hw, E1000_EEER);

	if (hw->eee_enable) {
		ipcnfg |= (E1000_IPCNFG_EEE_1G_AN | E1000_IPCNFG_EEE_100M_AN);
		eeer |= (E1000_EEER_TX_LPI_EN | E1000_EEER_RX_LPI_EN |
		    E1000_EEER_LPI_FC);
	} else {
		ipcnfg &= ~(E1000_IPCNFG_EEE_1G_AN | E1000_IPCNFG_EEE_100M_AN);
		eeer &= ~(E1000_EEER_TX_LPI_EN | E1000_EEER_RX_LPI_EN |
		    E1000_EEER_LPI_FC);
	}
	EM_WRITE_REG(hw, E1000_IPCNFG, ipcnfg);
	EM_WRITE_REG(hw, E1000_EEER, eeer);
	EM_READ_REG(hw, E1000_IPCNFG);
	EM_READ_REG(hw, E1000_EEER);
out:
	return ret_val;
}

/***************************************************************************
 *  em_set_eee_pchlan - Enable/disable EEE support
 *  @hw: pointer to the HW structure
 *
 *  Enable/disable EEE based on setting in dev_spec structure.  The bits in
 *  the LPI Control register will remain set only if/when link is up.
 ***************************************************************************/
int32_t
em_set_eee_pchlan(struct em_hw *hw)
{
       int32_t ret_val = E1000_SUCCESS;
       uint16_t phy_reg;

       DEBUGFUNC("em_set_eee_pchlan");

       if (hw->phy_type != em_phy_82579 &&
	   hw->phy_type != em_phy_i217)
               goto out;

       ret_val = em_read_phy_reg(hw, I82579_LPI_CTRL, &phy_reg);
       if (ret_val)
               goto out;

       if (hw->eee_enable)
               phy_reg &= ~I82579_LPI_CTRL_ENABLE_MASK;
       else
               phy_reg |= I82579_LPI_CTRL_ENABLE_MASK;

       ret_val = em_write_phy_reg(hw, I82579_LPI_CTRL, phy_reg);
out:
       return ret_val;
}

/**
 *  em_initialize_M88E1512_phy - Initialize M88E1512 PHY
 *  @hw: pointer to the HW structure
 *
 *  Initialize Marvell 1512 to work correctly with Avoton.
 **/
int32_t
em_initialize_M88E1512_phy(struct em_hw *hw)
{
	int32_t ret_val = E1000_SUCCESS;

	DEBUGFUNC("e1000_initialize_M88E1512_phy");

	/* Check if this is correct PHY. */
	if (hw->phy_id != M88E1512_E_PHY_ID)
		goto out;

	/* Switch to PHY page 0xFF. */
	ret_val = em_write_phy_reg(hw, M88E1543_PAGE_ADDR, 0x00FF);
	if (ret_val)
		goto out;

	ret_val = em_write_phy_reg(hw, M88E1512_CFG_REG_2, 0x214B);
	if (ret_val)
		goto out;

	ret_val = em_write_phy_reg(hw, M88E1512_CFG_REG_1, 0x2144);
	if (ret_val)
		goto out;

	ret_val = em_write_phy_reg(hw, M88E1512_CFG_REG_2, 0x0C28);
	if (ret_val)
		goto out;

	ret_val = em_write_phy_reg(hw, M88E1512_CFG_REG_1, 0x2146);
	if (ret_val)
		goto out;

	ret_val = em_write_phy_reg(hw, M88E1512_CFG_REG_2, 0xB233);
	if (ret_val)
		goto out;

	ret_val = em_write_phy_reg(hw, M88E1512_CFG_REG_1, 0x214D);
	if (ret_val)
		goto out;

	ret_val = em_write_phy_reg(hw, M88E1512_CFG_REG_2, 0xCC0C);
	if (ret_val)
		goto out;

	ret_val = em_write_phy_reg(hw, M88E1512_CFG_REG_1, 0x2159);
	if (ret_val)
		goto out;

	/* Switch to PHY page 0xFB. */
	ret_val = em_write_phy_reg(hw, M88E1543_PAGE_ADDR, 0x00FB);
	if (ret_val)
		goto out;

	ret_val = em_write_phy_reg(hw, M88E1512_CFG_REG_3, 0x000D);
	if (ret_val)
		goto out;

	/* Switch to PHY page 0x12. */
	ret_val = em_write_phy_reg(hw, M88E1543_PAGE_ADDR, 0x12);
	if (ret_val)
		goto out;

	/* Change mode to SGMII-to-Copper */
	ret_val = em_write_phy_reg(hw, M88E1512_MODE, 0x8001);
	if (ret_val)
		goto out;

	/* Return the PHY to page 0. */
	ret_val = em_write_phy_reg(hw, M88E1543_PAGE_ADDR, 0);
	if (ret_val)
		goto out;

	ret_val = em_phy_hw_reset(hw);
	if (ret_val) {
		DEBUGOUT("Error committing the PHY changes\n");
		return ret_val;
	}

	msec_delay(1000);
out:
	return ret_val;
}

uint32_t
em_translate_82542_register(uint32_t reg)
{
	/*
	 * Some of the 82542 registers are located at different
	 * offsets than they are in newer adapters.
	 * Despite the difference in location, the registers
	 * function in the same manner.
	 */
	switch (reg) {
	case E1000_RA:
		reg = 0x00040;
		break;
	case E1000_RDTR:
		reg = 0x00108;
		break;
	case E1000_RDBAL(0):
		reg = 0x00110;
		break;
	case E1000_RDBAH(0):
		reg = 0x00114;
		break;
	case E1000_RDLEN(0):
		reg = 0x00118;
		break;
	case E1000_RDH(0):
		reg = 0x00120;
		break;
	case E1000_RDT(0):
		reg = 0x00128;
		break;
	case E1000_RDBAL(1):
		reg = 0x00138;
		break;
	case E1000_RDBAH(1):
		reg = 0x0013C;
		break;
	case E1000_RDLEN(1):
		reg = 0x00140;
		break;
	case E1000_RDH(1):
		reg = 0x00148;
		break;
	case E1000_RDT(1):
		reg = 0x00150;
		break;
	case E1000_FCRTH:
		reg = 0x00160;
		break;
	case E1000_FCRTL:
		reg = 0x00168;
		break;
	case E1000_MTA:
		reg = 0x00200;
		break;
	case E1000_TDBAL(0):
		reg = 0x00420;
		break;
	case E1000_TDBAH(0):
		reg = 0x00424;
		break;
	case E1000_TDLEN(0):
		reg = 0x00428;
		break;
	case E1000_TDH(0):
		reg = 0x00430;
		break;
	case E1000_TDT(0):
		reg = 0x00438;
		break;
	case E1000_TIDV:
		reg = 0x00440;
		break;
	case E1000_VFTA:
		reg = 0x00600;
		break;
	case E1000_TDFH:
		reg = 0x08010;
		break;
	case E1000_TDFT:
		reg = 0x08018;
		break;
	default:
		break;
	}

	return (reg);
}
