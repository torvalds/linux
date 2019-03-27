/******************************************************************************

  Copyright (c) 2013-2018, Intel Corporation
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

#ifndef _I40E_PROTOTYPE_H_
#define _I40E_PROTOTYPE_H_

#include "i40e_type.h"
#include "i40e_alloc.h"
#include "virtchnl.h"

/* Prototypes for shared code functions that are not in
 * the standard function pointer structures.  These are
 * mostly because they are needed even before the init
 * has happened and will assist in the early SW and FW
 * setup.
 */

/* adminq functions */
enum i40e_status_code i40e_init_adminq(struct i40e_hw *hw);
enum i40e_status_code i40e_shutdown_adminq(struct i40e_hw *hw);
enum i40e_status_code i40e_init_asq(struct i40e_hw *hw);
enum i40e_status_code i40e_init_arq(struct i40e_hw *hw);
enum i40e_status_code i40e_alloc_adminq_asq_ring(struct i40e_hw *hw);
enum i40e_status_code i40e_alloc_adminq_arq_ring(struct i40e_hw *hw);
enum i40e_status_code i40e_shutdown_asq(struct i40e_hw *hw);
enum i40e_status_code i40e_shutdown_arq(struct i40e_hw *hw);
u16 i40e_clean_asq(struct i40e_hw *hw);
void i40e_free_adminq_asq(struct i40e_hw *hw);
void i40e_free_adminq_arq(struct i40e_hw *hw);
enum i40e_status_code i40e_validate_mac_addr(u8 *mac_addr);
void i40e_adminq_init_ring_data(struct i40e_hw *hw);
enum i40e_status_code i40e_clean_arq_element(struct i40e_hw *hw,
					     struct i40e_arq_event_info *e,
					     u16 *events_pending);
enum i40e_status_code i40e_asq_send_command(struct i40e_hw *hw,
				struct i40e_aq_desc *desc,
				void *buff, /* can be NULL */
				u16  buff_size,
				struct i40e_asq_cmd_details *cmd_details);
bool i40e_asq_done(struct i40e_hw *hw);

/* debug function for adminq */
void i40e_debug_aq(struct i40e_hw *hw, enum i40e_debug_mask mask,
		   void *desc, void *buffer, u16 buf_len);

void i40e_idle_aq(struct i40e_hw *hw);
bool i40e_check_asq_alive(struct i40e_hw *hw);
enum i40e_status_code i40e_aq_queue_shutdown(struct i40e_hw *hw, bool unloading);

enum i40e_status_code i40e_aq_get_rss_lut(struct i40e_hw *hw, u16 seid,
					  bool pf_lut, u8 *lut, u16 lut_size);
enum i40e_status_code i40e_aq_set_rss_lut(struct i40e_hw *hw, u16 seid,
					  bool pf_lut, u8 *lut, u16 lut_size);
enum i40e_status_code i40e_aq_get_rss_key(struct i40e_hw *hw,
				     u16 seid,
				     struct i40e_aqc_get_set_rss_key_data *key);
enum i40e_status_code i40e_aq_set_rss_key(struct i40e_hw *hw,
				     u16 seid,
				     struct i40e_aqc_get_set_rss_key_data *key);
const char *i40e_aq_str(struct i40e_hw *hw, enum i40e_admin_queue_err aq_err);
const char *i40e_stat_str(struct i40e_hw *hw, enum i40e_status_code stat_err);


u32 i40e_led_get(struct i40e_hw *hw);
void i40e_led_set(struct i40e_hw *hw, u32 mode, bool blink);
enum i40e_status_code i40e_led_set_phy(struct i40e_hw *hw, bool on,
				       u16 led_addr, u32 mode);
enum i40e_status_code i40e_led_get_phy(struct i40e_hw *hw, u16 *led_addr,
				       u16 *val);
enum i40e_status_code i40e_blink_phy_link_led(struct i40e_hw *hw,
					      u32 time, u32 interval);

/* admin send queue commands */

enum i40e_status_code i40e_aq_get_firmware_version(struct i40e_hw *hw,
				u16 *fw_major_version, u16 *fw_minor_version,
				u32 *fw_build,
				u16 *api_major_version, u16 *api_minor_version,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_debug_write_register(struct i40e_hw *hw,
				u32 reg_addr, u64 reg_val,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_debug_read_register(struct i40e_hw *hw,
				u32  reg_addr, u64 *reg_val,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_phy_debug(struct i40e_hw *hw, u8 cmd_flags,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_default_vsi(struct i40e_hw *hw, u16 vsi_id,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_clear_default_vsi(struct i40e_hw *hw, u16 vsi_id,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_get_phy_capabilities(struct i40e_hw *hw,
			bool qualified_modules, bool report_init,
			struct i40e_aq_get_phy_abilities_resp *abilities,
			struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_phy_config(struct i40e_hw *hw,
				struct i40e_aq_set_phy_config *config,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_set_fc(struct i40e_hw *hw, u8 *aq_failures,
				  bool atomic_reset);
enum i40e_status_code i40e_aq_set_phy_int_mask(struct i40e_hw *hw, u16 mask,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_mac_config(struct i40e_hw *hw,
				u16 max_frame_size, bool crc_en, u16 pacing,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_get_local_advt_reg(struct i40e_hw *hw,
				u64 *advt_reg,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_get_partner_advt(struct i40e_hw *hw,
				u64 *advt_reg,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code
i40e_aq_set_lb_modes(struct i40e_hw *hw, u8 lb_level, u8 lb_type, u8 speed,
		     struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_clear_pxe_mode(struct i40e_hw *hw,
			struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_link_restart_an(struct i40e_hw *hw,
		bool enable_link, struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_get_link_info(struct i40e_hw *hw,
				bool enable_lse, struct i40e_link_status *link,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_local_advt_reg(struct i40e_hw *hw,
				u64 advt_reg,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_send_driver_version(struct i40e_hw *hw,
				struct i40e_driver_version *dv,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_add_vsi(struct i40e_hw *hw,
				struct i40e_vsi_context *vsi_ctx,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_vsi_broadcast(struct i40e_hw *hw,
				u16 vsi_id, bool set_filter,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_vsi_unicast_promiscuous(struct i40e_hw *hw,
		u16 vsi_id, bool set, struct i40e_asq_cmd_details *cmd_details,
		bool rx_only_promisc);
enum i40e_status_code i40e_aq_set_vsi_multicast_promiscuous(struct i40e_hw *hw,
		u16 vsi_id, bool set, struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_vsi_full_promiscuous(struct i40e_hw *hw,
				u16 seid, bool set,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_vsi_mc_promisc_on_vlan(struct i40e_hw *hw,
				u16 seid, bool enable, u16 vid,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_vsi_uc_promisc_on_vlan(struct i40e_hw *hw,
				u16 seid, bool enable, u16 vid,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_vsi_bc_promisc_on_vlan(struct i40e_hw *hw,
				u16 seid, bool enable, u16 vid,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_vsi_vlan_promisc(struct i40e_hw *hw,
				u16 seid, bool enable,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_get_vsi_params(struct i40e_hw *hw,
				struct i40e_vsi_context *vsi_ctx,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_update_vsi_params(struct i40e_hw *hw,
				struct i40e_vsi_context *vsi_ctx,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_add_veb(struct i40e_hw *hw, u16 uplink_seid,
				u16 downlink_seid, u8 enabled_tc,
				bool default_port, u16 *pveb_seid,
				bool enable_stats,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_get_veb_parameters(struct i40e_hw *hw,
				u16 veb_seid, u16 *switch_id, bool *floating,
				u16 *statistic_index, u16 *vebs_used,
				u16 *vebs_free,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_add_macvlan(struct i40e_hw *hw, u16 vsi_id,
			struct i40e_aqc_add_macvlan_element_data *mv_list,
			u16 count, struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_remove_macvlan(struct i40e_hw *hw, u16 vsi_id,
			struct i40e_aqc_remove_macvlan_element_data *mv_list,
			u16 count, struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_add_mirrorrule(struct i40e_hw *hw, u16 sw_seid,
			u16 rule_type, u16 dest_vsi, u16 count, __le16 *mr_list,
			struct i40e_asq_cmd_details *cmd_details,
			u16 *rule_id, u16 *rules_used, u16 *rules_free);
enum i40e_status_code i40e_aq_delete_mirrorrule(struct i40e_hw *hw, u16 sw_seid,
			u16 rule_type, u16 rule_id, u16 count, __le16 *mr_list,
			struct i40e_asq_cmd_details *cmd_details,
			u16 *rules_used, u16 *rules_free);

enum i40e_status_code i40e_aq_add_vlan(struct i40e_hw *hw, u16 vsi_id,
			struct i40e_aqc_add_remove_vlan_element_data *v_list,
			u8 count, struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_remove_vlan(struct i40e_hw *hw, u16 vsi_id,
			struct i40e_aqc_add_remove_vlan_element_data *v_list,
			u8 count, struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_send_msg_to_vf(struct i40e_hw *hw, u16 vfid,
				u32 v_opcode, u32 v_retval, u8 *msg, u16 msglen,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_get_switch_config(struct i40e_hw *hw,
				struct i40e_aqc_get_switch_config_resp *buf,
				u16 buf_size, u16 *start_seid,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_switch_config(struct i40e_hw *hw,
				u16 flags, u16 valid_flags, u8 mode,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_request_resource(struct i40e_hw *hw,
				enum i40e_aq_resources_ids resource,
				enum i40e_aq_resource_access_type access,
				u8 sdp_number, u64 *timeout,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_release_resource(struct i40e_hw *hw,
				enum i40e_aq_resources_ids resource,
				u8 sdp_number,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_read_nvm(struct i40e_hw *hw, u8 module_pointer,
				u32 offset, u16 length, void *data,
				bool last_command,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_erase_nvm(struct i40e_hw *hw, u8 module_pointer,
				u32 offset, u16 length, bool last_command,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_read_nvm_config(struct i40e_hw *hw,
				u8 cmd_flags, u32 field_id, void *data,
				u16 buf_size, u16 *element_count,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_write_nvm_config(struct i40e_hw *hw,
				u8 cmd_flags, void *data, u16 buf_size,
				u16 element_count,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_oem_post_update(struct i40e_hw *hw,
				void *buff, u16 buff_size,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_discover_capabilities(struct i40e_hw *hw,
				void *buff, u16 buff_size, u16 *data_size,
				enum i40e_admin_queue_opc list_type_opc,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_update_nvm(struct i40e_hw *hw, u8 module_pointer,
				u32 offset, u16 length, void *data,
				bool last_command, u8 preservation_flags,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_nvm_progress(struct i40e_hw *hw, u8 *progress,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_get_lldp_mib(struct i40e_hw *hw, u8 bridge_type,
				u8 mib_type, void *buff, u16 buff_size,
				u16 *local_len, u16 *remote_len,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_lldp_mib(struct i40e_hw *hw,
				u8 mib_type, void *buff, u16 buff_size,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_cfg_lldp_mib_change_event(struct i40e_hw *hw,
				bool enable_update,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_add_lldp_tlv(struct i40e_hw *hw, u8 bridge_type,
				void *buff, u16 buff_size, u16 tlv_len,
				u16 *mib_len,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_update_lldp_tlv(struct i40e_hw *hw,
				u8 bridge_type, void *buff, u16 buff_size,
				u16 old_len, u16 new_len, u16 offset,
				u16 *mib_len,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_delete_lldp_tlv(struct i40e_hw *hw,
				u8 bridge_type, void *buff, u16 buff_size,
				u16 tlv_len, u16 *mib_len,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_stop_lldp(struct i40e_hw *hw, bool shutdown_agent,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_dcb_parameters(struct i40e_hw *hw,
						 bool dcb_enable,
						 struct i40e_asq_cmd_details
						 *cmd_details);
enum i40e_status_code i40e_aq_start_lldp(struct i40e_hw *hw,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_get_cee_dcb_config(struct i40e_hw *hw,
				void *buff, u16 buff_size,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_start_stop_dcbx(struct i40e_hw *hw,
				bool start_agent,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_add_udp_tunnel(struct i40e_hw *hw,
				u16 udp_port, u8 protocol_index,
				u8 *filter_index,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_del_udp_tunnel(struct i40e_hw *hw, u8 index,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_get_switch_resource_alloc(struct i40e_hw *hw,
			u8 *num_entries,
			struct i40e_aqc_switch_resource_alloc_element_resp *buf,
			u16 count,
			struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_add_pvirt(struct i40e_hw *hw, u16 flags,
				       u16 mac_seid, u16 vsi_seid,
				       u16 *ret_seid);
enum i40e_status_code i40e_aq_add_tag(struct i40e_hw *hw, bool direct_to_queue,
				u16 vsi_seid, u16 tag, u16 queue_num,
				u16 *tags_used, u16 *tags_free,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_remove_tag(struct i40e_hw *hw, u16 vsi_seid,
				u16 tag, u16 *tags_used, u16 *tags_free,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_add_mcast_etag(struct i40e_hw *hw, u16 pe_seid,
				u16 etag, u8 num_tags_in_buf, void *buf,
				u16 *tags_used, u16 *tags_free,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_remove_mcast_etag(struct i40e_hw *hw, u16 pe_seid,
				u16 etag, u16 *tags_used, u16 *tags_free,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_update_tag(struct i40e_hw *hw, u16 vsi_seid,
				u16 old_tag, u16 new_tag, u16 *tags_used,
				u16 *tags_free,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_add_statistics(struct i40e_hw *hw, u16 seid,
				u16 vlan_id, u16 *stat_index,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_remove_statistics(struct i40e_hw *hw, u16 seid,
				u16 vlan_id, u16 stat_index,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_port_parameters(struct i40e_hw *hw,
				u16 bad_frame_vsi, bool save_bad_pac,
				bool pad_short_pac, bool double_vlan,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_delete_element(struct i40e_hw *hw, u16 seid,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_mac_address_write(struct i40e_hw *hw,
				    u16 flags, u8 *mac_addr,
				    struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_config_vsi_bw_limit(struct i40e_hw *hw,
				u16 seid, u16 credit, u8 max_credit,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_dcb_ignore_pfc(struct i40e_hw *hw,
				u8 tcmap, bool request, u8 *tcmap_ret,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_config_switch_comp_ets_bw_limit(
	struct i40e_hw *hw, u16 seid,
	struct i40e_aqc_configure_switching_comp_ets_bw_limit_data *bw_data,
	struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_config_vsi_ets_sla_bw_limit(struct i40e_hw *hw,
			u16 seid,
			struct i40e_aqc_configure_vsi_ets_sla_bw_data *bw_data,
			struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_dcb_updated(struct i40e_hw *hw,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_config_switch_comp_bw_limit(struct i40e_hw *hw,
				u16 seid, u16 credit, u8 max_bw,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_config_vsi_tc_bw(struct i40e_hw *hw, u16 seid,
			struct i40e_aqc_configure_vsi_tc_bw_data *bw_data,
			struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_query_vsi_bw_config(struct i40e_hw *hw,
			u16 seid,
			struct i40e_aqc_query_vsi_bw_config_resp *bw_data,
			struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_query_vsi_ets_sla_config(struct i40e_hw *hw,
			u16 seid,
			struct i40e_aqc_query_vsi_ets_sla_config_resp *bw_data,
			struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_query_switch_comp_ets_config(struct i40e_hw *hw,
		u16 seid,
		struct i40e_aqc_query_switching_comp_ets_config_resp *bw_data,
		struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_query_port_ets_config(struct i40e_hw *hw,
		u16 seid,
		struct i40e_aqc_query_port_ets_config_resp *bw_data,
		struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_query_switch_comp_bw_config(struct i40e_hw *hw,
		u16 seid,
		struct i40e_aqc_query_switching_comp_bw_config_resp *bw_data,
		struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_resume_port_tx(struct i40e_hw *hw,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_read_lldp_cfg(struct i40e_hw *hw,
					struct i40e_lldp_variables *lldp_cfg);
enum i40e_status_code i40e_aq_add_cloud_filters(struct i40e_hw *hw,
		u16 vsi,
		struct i40e_aqc_add_remove_cloud_filters_element_data *filters,
		u8 filter_count);

enum i40e_status_code i40e_aq_remove_cloud_filters(struct i40e_hw *hw,
		u16 vsi,
		struct i40e_aqc_add_remove_cloud_filters_element_data *filters,
		u8 filter_count);
enum i40e_status_code i40e_aq_alternate_read(struct i40e_hw *hw,
				u32 reg_addr0, u32 *reg_val0,
				u32 reg_addr1, u32 *reg_val1);
enum i40e_status_code i40e_aq_alternate_read_indirect(struct i40e_hw *hw,
				u32 addr, u32 dw_count, void *buffer);
enum i40e_status_code i40e_aq_alternate_write(struct i40e_hw *hw,
				u32 reg_addr0, u32 reg_val0,
				u32 reg_addr1, u32 reg_val1);
enum i40e_status_code i40e_aq_alternate_write_indirect(struct i40e_hw *hw,
				u32 addr, u32 dw_count, void *buffer);
enum i40e_status_code i40e_aq_alternate_clear(struct i40e_hw *hw);
enum i40e_status_code i40e_aq_alternate_write_done(struct i40e_hw *hw,
				u8 bios_mode, bool *reset_needed);
enum i40e_status_code i40e_aq_set_oem_mode(struct i40e_hw *hw,
				u8 oem_mode);

/* i40e_common */
enum i40e_status_code i40e_init_shared_code(struct i40e_hw *hw);
enum i40e_status_code i40e_pf_reset(struct i40e_hw *hw);
void i40e_clear_hw(struct i40e_hw *hw);
void i40e_clear_pxe_mode(struct i40e_hw *hw);
enum i40e_status_code i40e_get_link_status(struct i40e_hw *hw, bool *link_up);
enum i40e_status_code i40e_update_link_info(struct i40e_hw *hw);
enum i40e_status_code i40e_get_mac_addr(struct i40e_hw *hw, u8 *mac_addr);
enum i40e_status_code i40e_read_bw_from_alt_ram(struct i40e_hw *hw,
		u32 *max_bw, u32 *min_bw, bool *min_valid, bool *max_valid);
enum i40e_status_code i40e_aq_configure_partition_bw(struct i40e_hw *hw,
			struct i40e_aqc_configure_partition_bw_data *bw_data,
			struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_get_port_mac_addr(struct i40e_hw *hw, u8 *mac_addr);
enum i40e_status_code i40e_read_pba_string(struct i40e_hw *hw, u8 *pba_num,
					    u32 pba_num_size);
void i40e_pre_tx_queue_cfg(struct i40e_hw *hw, u32 queue, bool enable);
enum i40e_aq_link_speed i40e_get_link_speed(struct i40e_hw *hw);
/* prototype for functions used for NVM access */
enum i40e_status_code i40e_init_nvm(struct i40e_hw *hw);
enum i40e_status_code i40e_acquire_nvm(struct i40e_hw *hw,
				      enum i40e_aq_resource_access_type access);
void i40e_release_nvm(struct i40e_hw *hw);
enum i40e_status_code i40e_read_nvm_word(struct i40e_hw *hw, u16 offset,
					 u16 *data);
enum i40e_status_code i40e_read_nvm_buffer(struct i40e_hw *hw, u16 offset,
					   u16 *words, u16 *data);
enum i40e_status_code i40e_write_nvm_aq(struct i40e_hw *hw, u8 module,
					u32 offset, u16 words, void *data,
					bool last_command);
enum i40e_status_code __i40e_read_nvm_word(struct i40e_hw *hw, u16 offset,
					   u16 *data);
enum i40e_status_code __i40e_read_nvm_buffer(struct i40e_hw *hw, u16 offset,
					     u16 *words, u16 *data);
enum i40e_status_code __i40e_write_nvm_word(struct i40e_hw *hw, u32 offset,
					  void *data);
enum i40e_status_code __i40e_write_nvm_buffer(struct i40e_hw *hw, u8 module,
					    u32 offset, u16 words, void *data);
enum i40e_status_code i40e_calc_nvm_checksum(struct i40e_hw *hw, u16 *checksum);
enum i40e_status_code i40e_update_nvm_checksum(struct i40e_hw *hw);
enum i40e_status_code i40e_validate_nvm_checksum(struct i40e_hw *hw,
						 u16 *checksum);
enum i40e_status_code i40e_nvmupd_command(struct i40e_hw *hw,
					  struct i40e_nvm_access *cmd,
					  u8 *bytes, int *);
void i40e_nvmupd_check_wait_event(struct i40e_hw *hw, u16 opcode,
				  struct i40e_aq_desc *desc);
void i40e_nvmupd_clear_wait_state(struct i40e_hw *hw);
void i40e_set_pci_config_data(struct i40e_hw *hw, u16 link_status);

enum i40e_status_code i40e_set_mac_type(struct i40e_hw *hw);

extern struct i40e_rx_ptype_decoded i40e_ptype_lookup[];

static INLINE struct i40e_rx_ptype_decoded decode_rx_desc_ptype(u8 ptype)
{
	return i40e_ptype_lookup[ptype];
}

/**
 * i40e_virtchnl_link_speed - Convert AdminQ link_speed to virtchnl definition
 * @link_speed: the speed to convert
 *
 * Returns the link_speed in terms of the virtchnl interface, for use in
 * converting link_speed as reported by the AdminQ into the format used for
 * talking to virtchnl devices. If we can't represent the link speed properly,
 * report LINK_SPEED_UNKNOWN.
 **/
static INLINE enum virtchnl_link_speed
i40e_virtchnl_link_speed(enum i40e_aq_link_speed link_speed)
{
	switch (link_speed) {
	case I40E_LINK_SPEED_100MB:
		return VIRTCHNL_LINK_SPEED_100MB;
	case I40E_LINK_SPEED_1GB:
		return VIRTCHNL_LINK_SPEED_1GB;
	case I40E_LINK_SPEED_10GB:
		return VIRTCHNL_LINK_SPEED_10GB;
	case I40E_LINK_SPEED_40GB:
		return VIRTCHNL_LINK_SPEED_40GB;
	case I40E_LINK_SPEED_20GB:
		return VIRTCHNL_LINK_SPEED_20GB;
	case I40E_LINK_SPEED_25GB:
		return VIRTCHNL_LINK_SPEED_25GB;
	case I40E_LINK_SPEED_UNKNOWN:
	default:
		return VIRTCHNL_LINK_SPEED_UNKNOWN;
	}
}

/* prototype for functions used for SW spinlocks */
void i40e_init_spinlock(struct i40e_spinlock *sp);
void i40e_acquire_spinlock(struct i40e_spinlock *sp);
void i40e_release_spinlock(struct i40e_spinlock *sp);
void i40e_destroy_spinlock(struct i40e_spinlock *sp);

/* i40e_common for VF drivers*/
void i40e_vf_parse_hw_config(struct i40e_hw *hw,
			     struct virtchnl_vf_resource *msg);
enum i40e_status_code i40e_vf_reset(struct i40e_hw *hw);
enum i40e_status_code i40e_aq_send_msg_to_pf(struct i40e_hw *hw,
				enum virtchnl_ops v_opcode,
				enum i40e_status_code v_retval,
				u8 *msg, u16 msglen,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_set_filter_control(struct i40e_hw *hw,
				struct i40e_filter_control_settings *settings);
enum i40e_status_code i40e_aq_add_rem_control_packet_filter(struct i40e_hw *hw,
				u8 *mac_addr, u16 ethtype, u16 flags,
				u16 vsi_seid, u16 queue, bool is_add,
				struct i40e_control_filter_stats *stats,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_debug_dump(struct i40e_hw *hw, u8 cluster_id,
				u8 table_id, u32 start_index, u16 buff_size,
				void *buff, u16 *ret_buff_size,
				u8 *ret_next_table, u32 *ret_next_index,
				struct i40e_asq_cmd_details *cmd_details);
void i40e_add_filter_to_drop_tx_flow_control_frames(struct i40e_hw *hw,
						    u16 vsi_seid);
enum i40e_status_code i40e_aq_rx_ctl_read_register(struct i40e_hw *hw,
				u32 reg_addr, u32 *reg_val,
				struct i40e_asq_cmd_details *cmd_details);
u32 i40e_read_rx_ctl(struct i40e_hw *hw, u32 reg_addr);
enum i40e_status_code i40e_aq_rx_ctl_write_register(struct i40e_hw *hw,
				u32 reg_addr, u32 reg_val,
				struct i40e_asq_cmd_details *cmd_details);
void i40e_write_rx_ctl(struct i40e_hw *hw, u32 reg_addr, u32 reg_val);
enum i40e_status_code i40e_aq_set_phy_register(struct i40e_hw *hw,
				u8 phy_select, u8 dev_addr,
				u32 reg_addr, u32 reg_val,
				struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_get_phy_register(struct i40e_hw *hw,
				u8 phy_select, u8 dev_addr,
				u32 reg_addr, u32 *reg_val,
				struct i40e_asq_cmd_details *cmd_details);

enum i40e_status_code i40e_aq_set_arp_proxy_config(struct i40e_hw *hw,
			struct i40e_aqc_arp_proxy_data *proxy_config,
			struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_ns_proxy_table_entry(struct i40e_hw *hw,
			struct i40e_aqc_ns_proxy_data *ns_proxy_table_entry,
			struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_set_clear_wol_filter(struct i40e_hw *hw,
			u8 filter_index,
			struct i40e_aqc_set_wol_filter_data *filter,
			bool set_filter, bool no_wol_tco,
			bool filter_valid, bool no_wol_tco_valid,
			struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_get_wake_event_reason(struct i40e_hw *hw,
			u16 *wake_reason,
			struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_aq_clear_all_wol_filters(struct i40e_hw *hw,
			struct i40e_asq_cmd_details *cmd_details);
enum i40e_status_code i40e_read_phy_register_clause22(struct i40e_hw *hw,
					u16 reg, u8 phy_addr, u16 *value);
enum i40e_status_code i40e_write_phy_register_clause22(struct i40e_hw *hw,
					u16 reg, u8 phy_addr, u16 value);
enum i40e_status_code i40e_read_phy_register_clause45(struct i40e_hw *hw,
				u8 page, u16 reg, u8 phy_addr, u16 *value);
enum i40e_status_code i40e_write_phy_register_clause45(struct i40e_hw *hw,
				u8 page, u16 reg, u8 phy_addr, u16 value);
enum i40e_status_code i40e_read_phy_register(struct i40e_hw *hw,
				u8 page, u16 reg, u8 phy_addr, u16 *value);
enum i40e_status_code i40e_write_phy_register(struct i40e_hw *hw,
				u8 page, u16 reg, u8 phy_addr, u16 value);
u8 i40e_get_phy_address(struct i40e_hw *hw, u8 dev_num);
#endif /* _I40E_PROTOTYPE_H_ */
