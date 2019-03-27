/******************************************************************************
  SPDX-License-Identifier: BSD-3-Clause

  Copyright (c) 2001-2017, Intel Corporation
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

******************************************************************************/
/*$FreeBSD$*/

#ifndef _IXGBE_COMMON_H_
#define _IXGBE_COMMON_H_

#include "ixgbe_type.h"
#define IXGBE_WRITE_REG64(hw, reg, value) \
	do { \
		IXGBE_WRITE_REG(hw, reg, (u32) value); \
		IXGBE_WRITE_REG(hw, reg + 4, (u32) (value >> 32)); \
	} while (0)
#define IXGBE_REMOVED(a) (0)
#if !defined(NO_READ_PBA_RAW) || !defined(NO_WRITE_PBA_RAW)
struct ixgbe_pba {
	u16 word[2];
	u16 *pba_block;
};
#endif

void ixgbe_dcb_get_rtrup2tc_generic(struct ixgbe_hw *hw, u8 *map);

u16 ixgbe_get_pcie_msix_count_generic(struct ixgbe_hw *hw);
s32 ixgbe_init_ops_generic(struct ixgbe_hw *hw);
s32 ixgbe_init_hw_generic(struct ixgbe_hw *hw);
s32 ixgbe_start_hw_generic(struct ixgbe_hw *hw);
s32 ixgbe_start_hw_gen2(struct ixgbe_hw *hw);
s32 ixgbe_clear_hw_cntrs_generic(struct ixgbe_hw *hw);
s32 ixgbe_read_pba_num_generic(struct ixgbe_hw *hw, u32 *pba_num);
s32 ixgbe_read_pba_string_generic(struct ixgbe_hw *hw, u8 *pba_num,
				  u32 pba_num_size);
s32 ixgbe_read_pba_raw(struct ixgbe_hw *hw, u16 *eeprom_buf,
		       u32 eeprom_buf_size, u16 max_pba_block_size,
		       struct ixgbe_pba *pba);
s32 ixgbe_write_pba_raw(struct ixgbe_hw *hw, u16 *eeprom_buf,
			u32 eeprom_buf_size, struct ixgbe_pba *pba);
s32 ixgbe_get_pba_block_size(struct ixgbe_hw *hw, u16 *eeprom_buf,
			     u32 eeprom_buf_size, u16 *pba_block_size);
s32 ixgbe_get_mac_addr_generic(struct ixgbe_hw *hw, u8 *mac_addr);
s32 ixgbe_get_bus_info_generic(struct ixgbe_hw *hw);
void ixgbe_set_pci_config_data_generic(struct ixgbe_hw *hw, u16 link_status);
void ixgbe_set_lan_id_multi_port_pcie(struct ixgbe_hw *hw);
s32 ixgbe_stop_adapter_generic(struct ixgbe_hw *hw);

s32 ixgbe_led_on_generic(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_led_off_generic(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_init_led_link_act_generic(struct ixgbe_hw *hw);

s32 ixgbe_init_eeprom_params_generic(struct ixgbe_hw *hw);
s32 ixgbe_write_eeprom_generic(struct ixgbe_hw *hw, u16 offset, u16 data);
s32 ixgbe_write_eeprom_buffer_bit_bang_generic(struct ixgbe_hw *hw, u16 offset,
					       u16 words, u16 *data);
s32 ixgbe_read_eerd_generic(struct ixgbe_hw *hw, u16 offset, u16 *data);
s32 ixgbe_read_eerd_buffer_generic(struct ixgbe_hw *hw, u16 offset,
				   u16 words, u16 *data);
s32 ixgbe_write_eewr_generic(struct ixgbe_hw *hw, u16 offset, u16 data);
s32 ixgbe_write_eewr_buffer_generic(struct ixgbe_hw *hw, u16 offset,
				    u16 words, u16 *data);
s32 ixgbe_read_eeprom_bit_bang_generic(struct ixgbe_hw *hw, u16 offset,
				       u16 *data);
s32 ixgbe_read_eeprom_buffer_bit_bang_generic(struct ixgbe_hw *hw, u16 offset,
					      u16 words, u16 *data);
s32 ixgbe_calc_eeprom_checksum_generic(struct ixgbe_hw *hw);
s32 ixgbe_validate_eeprom_checksum_generic(struct ixgbe_hw *hw,
					   u16 *checksum_val);
s32 ixgbe_update_eeprom_checksum_generic(struct ixgbe_hw *hw);
s32 ixgbe_poll_eerd_eewr_done(struct ixgbe_hw *hw, u32 ee_reg);

s32 ixgbe_set_rar_generic(struct ixgbe_hw *hw, u32 index, u8 *addr, u32 vmdq,
			  u32 enable_addr);
s32 ixgbe_clear_rar_generic(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_init_rx_addrs_generic(struct ixgbe_hw *hw);
s32 ixgbe_update_mc_addr_list_generic(struct ixgbe_hw *hw, u8 *mc_addr_list,
				      u32 mc_addr_count,
				      ixgbe_mc_addr_itr func, bool clear);
s32 ixgbe_update_uc_addr_list_generic(struct ixgbe_hw *hw, u8 *addr_list,
				      u32 addr_count, ixgbe_mc_addr_itr func);
s32 ixgbe_enable_mc_generic(struct ixgbe_hw *hw);
s32 ixgbe_disable_mc_generic(struct ixgbe_hw *hw);
s32 ixgbe_enable_rx_dma_generic(struct ixgbe_hw *hw, u32 regval);
s32 ixgbe_disable_sec_rx_path_generic(struct ixgbe_hw *hw);
s32 ixgbe_enable_sec_rx_path_generic(struct ixgbe_hw *hw);

s32 ixgbe_fc_enable_generic(struct ixgbe_hw *hw);
bool ixgbe_device_supports_autoneg_fc(struct ixgbe_hw *hw);
void ixgbe_fc_autoneg(struct ixgbe_hw *hw);
s32 ixgbe_setup_fc_generic(struct ixgbe_hw *hw);

s32 ixgbe_validate_mac_addr(u8 *mac_addr);
s32 ixgbe_acquire_swfw_sync(struct ixgbe_hw *hw, u32 mask);
void ixgbe_release_swfw_sync(struct ixgbe_hw *hw, u32 mask);
s32 ixgbe_disable_pcie_master(struct ixgbe_hw *hw);

s32 prot_autoc_read_generic(struct ixgbe_hw *hw, bool *, u32 *reg_val);
s32 prot_autoc_write_generic(struct ixgbe_hw *hw, u32 reg_val, bool locked);

s32 ixgbe_blink_led_start_generic(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_blink_led_stop_generic(struct ixgbe_hw *hw, u32 index);

s32 ixgbe_get_san_mac_addr_generic(struct ixgbe_hw *hw, u8 *san_mac_addr);
s32 ixgbe_set_san_mac_addr_generic(struct ixgbe_hw *hw, u8 *san_mac_addr);

s32 ixgbe_set_vmdq_generic(struct ixgbe_hw *hw, u32 rar, u32 vmdq);
s32 ixgbe_set_vmdq_san_mac_generic(struct ixgbe_hw *hw, u32 vmdq);
s32 ixgbe_clear_vmdq_generic(struct ixgbe_hw *hw, u32 rar, u32 vmdq);
s32 ixgbe_insert_mac_addr_generic(struct ixgbe_hw *hw, u8 *addr, u32 vmdq);
s32 ixgbe_init_uta_tables_generic(struct ixgbe_hw *hw);
s32 ixgbe_set_vfta_generic(struct ixgbe_hw *hw, u32 vlan,
			 u32 vind, bool vlan_on, bool vlvf_bypass);
s32 ixgbe_set_vlvf_generic(struct ixgbe_hw *hw, u32 vlan, u32 vind,
			   bool vlan_on, u32 *vfta_delta, u32 vfta,
			   bool vlvf_bypass);
s32 ixgbe_clear_vfta_generic(struct ixgbe_hw *hw);
s32 ixgbe_find_vlvf_slot(struct ixgbe_hw *hw, u32 vlan, bool vlvf_bypass);

s32 ixgbe_check_mac_link_generic(struct ixgbe_hw *hw,
			       ixgbe_link_speed *speed,
			       bool *link_up, bool link_up_wait_to_complete);

s32 ixgbe_get_wwn_prefix_generic(struct ixgbe_hw *hw, u16 *wwnn_prefix,
				 u16 *wwpn_prefix);

s32 ixgbe_get_fcoe_boot_status_generic(struct ixgbe_hw *hw, u16 *bs);
void ixgbe_set_mac_anti_spoofing(struct ixgbe_hw *hw, bool enable, int vf);
void ixgbe_set_vlan_anti_spoofing(struct ixgbe_hw *hw, bool enable, int vf);
s32 ixgbe_get_device_caps_generic(struct ixgbe_hw *hw, u16 *device_caps);
void ixgbe_set_rxpba_generic(struct ixgbe_hw *hw, int num_pb, u32 headroom,
			     int strategy);
void ixgbe_enable_relaxed_ordering_gen2(struct ixgbe_hw *hw);
s32 ixgbe_set_fw_drv_ver_generic(struct ixgbe_hw *hw, u8 maj, u8 min,
				 u8 build, u8 ver, u16 len, const char *str);
u8 ixgbe_calculate_checksum(u8 *buffer, u32 length);
s32 ixgbe_host_interface_command(struct ixgbe_hw *hw, u32 *buffer,
				 u32 length, u32 timeout, bool return_data);
s32 ixgbe_hic_unlocked(struct ixgbe_hw *, u32 *buffer, u32 length, u32 timeout);
s32 ixgbe_shutdown_fw_phy(struct ixgbe_hw *);
s32 ixgbe_fw_phy_activity(struct ixgbe_hw *, u16 activity,
			  u32 (*data)[FW_PHY_ACT_DATA_COUNT]);
void ixgbe_clear_tx_pending(struct ixgbe_hw *hw);
s32 ixgbe_bypass_rw_generic(struct ixgbe_hw *hw, u32 cmd, u32 *status);
bool ixgbe_bypass_valid_rd_generic(u32 in_reg, u32 out_reg);
s32 ixgbe_bypass_set_generic(struct ixgbe_hw *hw, u32 ctrl, u32 event,
			     u32 action);
s32 ixgbe_bypass_rd_eep_generic(struct ixgbe_hw *hw, u32 addr, u8 *value);

extern s32 ixgbe_reset_pipeline_82599(struct ixgbe_hw *hw);
extern void ixgbe_stop_mac_link_on_d3_82599(struct ixgbe_hw *hw);
bool ixgbe_mng_present(struct ixgbe_hw *hw);
bool ixgbe_mng_enabled(struct ixgbe_hw *hw);


void ixgbe_get_etk_id(struct ixgbe_hw *hw, struct ixgbe_nvm_version *nvm_ver);
void ixgbe_get_oem_prod_version(struct ixgbe_hw *hw,
				struct ixgbe_nvm_version *nvm_ver);
void ixgbe_get_orom_version(struct ixgbe_hw *hw,
			    struct ixgbe_nvm_version *nvm_ver);
void ixgbe_disable_rx_generic(struct ixgbe_hw *hw);
void ixgbe_enable_rx_generic(struct ixgbe_hw *hw);
s32 ixgbe_setup_mac_link_multispeed_fiber(struct ixgbe_hw *hw,
					  ixgbe_link_speed speed,
					  bool autoneg_wait_to_complete);
void ixgbe_set_soft_rate_select_speed(struct ixgbe_hw *hw,
				      ixgbe_link_speed speed);
#endif /* IXGBE_COMMON */
