/**** AUTO GENERATED ****/

#ifndef HIC_WRAPPER_H
#define HIC_WRAPPER_H
#include "macWrapper.h"
#include "mac_api.h"
#include "mac_mgmt_defs.h"

/*****************************************************************************
C O N S T A N T S / M A C R O S
*****************************************************************************/
#define HicWrapper_VERSION_ID 0x4A1F6001L

/*****************************************************************************
G L O B A L   D A T A T Y P E S
*****************************************************************************/
typedef void (* hic_wrapper_cb_t)(void* object_p, Blob_t* blob, WrapperAction_t action);

/*****************************************************************************
G L O B A L   C O N S T A N T S / V A R I A B L E S
*****************************************************************************/
#ifdef DE_ASSERT
#define HIC_ASSERT(_a)      DE_ASSERT((_a))      
#else
#define HIC_ASSERT(_a)
#endif

/*****************************************************************************
G L O B A L   F U N C T I O N S
*****************************************************************************/
#define HicWrapper_char(_o,_b,_a)     HicWrapper_uint8_t((uint8_t*)(_o), (_b), (_a))
#define HicWrapper_int(_o,_b,_a)      HicWrapper_uint32_t((uint32_t*)(_o), (_b), (_a))
#define HicWrapper_int8_t(_o,_b,_a)   HicWrapper_uint8_t((uint8_t*)(_o), (_b), (_a))
#define HicWrapper_int32_t(_o,_b,_a)  HicWrapper_uint32_t((uint32_t*)(_o), (_b), (_a))
#define HicWrapper_tsf_time_t         HicWrapper_uint32_t
#define HicWrapper_mlme_rssi_dbm_t    HicWrapper_int32_t
#define HicWrapper_uintptr_t          HicWrapper_uint32_t

#define HicWrapper_m80211_ie_hdr_t                       MacWrapper_m80211_ie_hdr_t         
#define HicWrapper_m80211_remaining_IEs_t                MacWrapper_m80211_remaining_IEs_t
#define HicWrapper_m80211_ie_vendor_specific_hdr_t       MacWrapper_m80211_ie_vendor_specific_hdr_t
#define HicWrapper_m80211_ie_supported_rates_t           MacWrapper_m80211_ie_supported_rates_t
#define HicWrapper_m80211_ie_ext_supported_rates_t       MacWrapper_m80211_ie_ext_supported_rates_t
#define HicWrapper_m80211_ie_ssid_t                      MacWrapper_m80211_ie_ssid_t
#define HicWrapper_m80211_ie_WMM_header_t                MacWrapper_m80211_ie_WMM_header_t
#define HicWrapper_m80211_ie_tim_t                       MacWrapper_m80211_ie_tim_t
#define HicWrapper_m80211_ie_rsn_parameter_set_t         MacWrapper_m80211_ie_rsn_parameter_set_t
#define HicWrapper_m80211_ie_challenge_text_t            MacWrapper_m80211_ie_challenge_text_t
#define HicWrapper_m80211_ie_ibss_par_set_t              MacWrapper_m80211_ie_ibss_par_set_t
#define HicWrapper_m80211_ie_cf_par_set_t                MacWrapper_m80211_ie_cf_par_set_t
#define HicWrapper_m80211_ie_ds_par_set_t                MacWrapper_m80211_ie_ds_par_set_t
#define HicWrapper_m80211_ie_fh_par_set_t                MacWrapper_m80211_ie_fh_par_set_t
#define HicWrapper_m80211_ie_wpa_parameter_set_t         MacWrapper_m80211_ie_wpa_parameter_set_t
#define HicWrapper_m80211_ie_wps_parameter_set_t         MacWrapper_m80211_ie_wps_parameter_set_t
#if (DE_CCX == CFG_INCLUDED)
#define HicWrapper_m80211_ie_qbss_load_t                 MacWrapper_m80211_ie_qbss_load_t
#define HicWrapper_m80211_ie_ccx_parameter_set_t         MacWrapper_m80211_ie_ccx_parameter_set_t
#define HicWrapper_m80211_ie_ccx_rm_parameter_set_t      MacWrapper_m80211_ie_ccx_rm_parameter_set_t
#define HicWrapper_m80211_ie_ccx_cpl_parameter_set_t     MacWrapper_m80211_ie_ccx_cpl_parameter_set_t
#define HicWrapper_m80211_ie_ccx_tsm_parameter_set_t     MacWrapper_m80211_ie_ccx_tsm_parameter_set_t
#define HicWrapper_m80211_wmm_tspec_ie_t                 MacWrapper_m80211_ie_wmm_tspec_parameter_set_t
#define HicWrapper_m80211_ie_ccx_reassoc_req_parameter_set_t MacWrapper_m80211_ie_ccx_reassoc_req_parameter_set_t
#define HicWrapper_m80211_ie_ccx_reassoc_rsp_parameter_set_t MacWrapper_m80211_ie_ccx_reassoc_rsp_parameter_set_t
#if 0
#define HicWrapper_m80211_ie_ccx_adj_parameter_set_t     MacWrapper_m80211_ie_ccx_adj_parameter_set_t
#endif
#endif //DE_CCX
#define HicWrapper_m80211_ie_wapi_parameter_set_t        MacWrapper_m80211_ie_wapi_parameter_set_t
#define HicWrapper_m80211_ie_ht_capabilities_t           MacWrapper_m80211_ie_ht_capabilities_t
#define HicWrapper_m80211_ie_ht_operation_t              MacWrapper_m80211_ie_ht_operation_t
#define HicWrapper_m80211_ie_WMM_information_element_t   MacWrapper_m80211_ie_WMM_information_element_t
#define HicWrapper_m80211_ie_WMM_parameter_element_t     MacWrapper_m80211_ie_WMM_parameter_element_t
#define HicWrapper_m80211_ie_qos_capability_t            MacWrapper_m80211_ie_qos_capability_t
#define HicWrapper_m80211_ie_country_t                   MacWrapper_m80211_ie_country_t   
#define HicWrapper_m80211_ie_request_info_t              MacWrapper_m80211_ie_request_info_t
#define HicWrapper_m80211_ie_erp_t                       MacWrapper_m80211_ie_erp_t
#define HicWrapper_m80211_mac_addr_t(_o,_b,_a)           HicWrapper_array_t((char *)(_o), sizeof(((m80211_mac_addr_t*)0)->octet), (_b), (_a))
#define HicWrapper_m80211_mlme_reassociate_req_t         HicWrapper_m80211_mlme_associate_req_t

#define HicWrapper_m80211_mlme_set_key_cfm_t             HicWrapper_m80211_mlme_parameter_cfm_t
#define HicWrapper_m80211_mlme_delete_key_cfm_t          HicWrapper_m80211_mlme_parameter_cfm_t
#define HicWrapper_m80211_mlme_set_protection_cfm_t      HicWrapper_m80211_mlme_parameter_cfm_t
#define HicWrapper_m80211_mlme_bg_scan_start_cfm_t       HicWrapper_m80211_mlme_parameter_cfm_t
#define HicWrapper_m80211_mlme_bg_scan_stop_req_t        HicWrapper_m80211_mlme_parameter_cfm_t
#define HicWrapper_m80211_mlme_bg_scan_stop_cfm_t        HicWrapper_m80211_mlme_parameter_cfm_t
#define HicWrapper_m80211_mlme_authenticate_ind_t        HicWrapper_m80211_mlme_addr_and_short_ind_t
#define HicWrapper_m80211_mlme_deauthenticate_req_t      HicWrapper_m80211_mlme_addr_and_short_ind_t
#define HicWrapper_m80211_mlme_deauthenticate_cfm_t      HicWrapper_m80211_mlme_addr_and_short_ind_t
#define HicWrapper_m80211_mlme_deauthenticate_ind_t      HicWrapper_m80211_mlme_addr_and_short_ind_t
#define HicWrapper_m80211_mlme_associate_cfm_t           HicWrapper_m80211_mlme_association_cfm_t
#define HicWrapper_m80211_mlme_reassociate_cfm_t         HicWrapper_m80211_mlme_association_cfm_t
#define HicWrapper_m80211_mlme_join_cfm_t                HicWrapper_m80211_mlme_status_cfm_t
#define HicWrapper_m80211_mlme_power_mgmt_cfm_t          HicWrapper_m80211_mlme_status_cfm_t
#define HicWrapper_m80211_mlme_disassociate_cfm_t        HicWrapper_m80211_mlme_status_cfm_t
#define HicWrapper_m80211_mlme_reset_cfm_t               HicWrapper_m80211_mlme_status_cfm_t
#define HicWrapper_m80211_mlme_start_cfm_t               HicWrapper_m80211_mlme_status_cfm_t
#define HicWrapper_m80211_nrp_mlme_bss_leave_cfm_t       HicWrapper_m80211_mlme_status_cfm_t
#define HicWrapper_m80211_nrp_mlme_ibss_leave_cfm_t      HicWrapper_m80211_mlme_status_cfm_t
#define HicWrapper_m80211_mlme_scan_cfm_t                HicWrapper_m80211_mlme_status_cfm_t

#define HicWrapper_m80211_bss_description_t              HicWrapper_mac_mmpdu_beacon_ind_t

/* BEGIN GENERATED PROTOS */

bool_t HicWrapper_VerifyInterfaceVersion (uint32_t version);

void HicWrapper_uint8_t (uint8_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_uint16_t (uint16_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_uint32_t (uint32_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_uint64_t (uint64_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_array_t (char* object_p, int length, Blob_t* blob, WrapperAction_t action);

void WrapperCopy_mlme_bss_description_t (void * context_p, mlme_bss_description_t* dest, mlme_bss_description_t* source);

void HicWrapper_mlme_bss_description_t (mlme_bss_description_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_scan_ind_t (m80211_mlme_scan_ind_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_varstring_t (varstring_t* object_p, Blob_t* blob, WrapperAction_t action);

void WrapperCopy_common_IEs_t (void* context_p, common_IEs_t* dest, common_IEs_t* source);

void HicWrapper_m80211_mlme_host_header_t (m80211_mlme_host_header_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_tbtt_timing_t (m80211_tbtt_timing_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_oui_id_t (m80211_oui_id_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_cipher_suite_selector_t (m80211_cipher_suite_selector_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_country_string_t (m80211_country_string_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_country_channels_t (m80211_country_channels_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_AC_parameters_t (AC_parameters_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_key_t (m80211_key_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_message_control_t (hic_message_control_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_message_header_t (hic_message_header_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_interface_wrapper_t (hic_interface_wrapper_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_status_cfm_t (m80211_mlme_status_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_addr_and_short_ind_t (m80211_mlme_addr_and_short_ind_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_parameter_cfm_t (m80211_mlme_parameter_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_association_cfm_t (m80211_mlme_association_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mlme_mgmt_body_t (mlme_mgmt_body_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_msg_t (hic_ctrl_msg_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mac_mmpdu_beacon_ind_t (mac_mmpdu_beacon_ind_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_common_IEs_t (common_IEs_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_receive_seq_cnt_t (receive_seq_cnt_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_set_key_descriptor_t (m80211_set_key_descriptor_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_delete_key_descriptor_t (m80211_delete_key_descriptor_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_protect_list_element_t (m80211_protect_list_element_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_michael_mic_failure_ind_descriptor_t (m80211_michael_mic_failure_ind_descriptor_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_power_mgmt_req_t (m80211_mlme_power_mgmt_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_join_req_t (m80211_mlme_join_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_authenticate_req_t (m80211_mlme_authenticate_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_associate_req_t (m80211_mlme_associate_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_disassociate_req_t (m80211_mlme_disassociate_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_reset_req_t (m80211_mlme_reset_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_start_req_t (m80211_mlme_start_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_set_key_req_t (m80211_mlme_set_key_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_delete_key_req_t (m80211_mlme_delete_key_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_set_protection_req_t (m80211_mlme_set_protection_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_bss_leave_req_t (m80211_nrp_mlme_bss_leave_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_ibss_leave_req_t (m80211_nrp_mlme_ibss_leave_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mlme_direct_scan_req_t (mlme_direct_scan_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_authenticate_cfm_t (m80211_mlme_authenticate_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_associate_ind_t (m80211_mlme_associate_ind_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_reassociate_ind_t (m80211_mlme_reassociate_ind_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_disassociate_ind_t (m80211_mlme_disassociate_ind_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_mlme_michael_mic_failure_ind_t (m80211_mlme_michael_mic_failure_ind_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_peer_status_ind_t (m80211_nrp_mlme_peer_status_ind_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mlme_direct_scan_cfm_t (mlme_direct_scan_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mlme_mib_get_req_t (mlme_mib_get_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mlme_mib_get_raw_req_t(mlme_mib_get_raw_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mlme_mib_set_req_t (mlme_mib_set_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mlme_mib_set_raw_req_t(mlme_mib_set_raw_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mlme_mib_get_next_req_t (mlme_mib_get_next_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mlme_mib_set_trigger_req_t (mlme_mib_set_trigger_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mlme_mib_set_gatingtrigger_req_t (mlme_mib_set_gatingtrigger_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mlme_mib_remove_trigger_req_t (mlme_mib_remove_trigger_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mlme_mib_get_cfm_t (mlme_mib_get_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mlme_mib_set_cfm_t (mlme_mib_set_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mlme_mib_set_trigger_cfm_t (mlme_mib_set_trigger_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mlme_mib_set_gatingtrigger_cfm_t (mlme_mib_set_gatingtrigger_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mlme_mib_remove_trigger_cfm_t (mlme_mib_remove_trigger_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mlme_mib_trigger_ind_t (mlme_mib_trigger_ind_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_mac_mib_body_t (mac_mib_body_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_scan_config_t (m80211_nrp_mlme_scan_config_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_version_req_t (hic_ctrl_version_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_heartbeat_req_t (hic_ctrl_heartbeat_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_set_alignment_req_t (hic_ctrl_set_alignment_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_init_completed_req_t(hic_ctrl_init_completed_req_t* object_p, Blob_t* blob, WrapperAction_t action);
void HicWrapper_hic_ctrl_init_completed_cfm_t(hic_ctrl_init_completed_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);
void HicWrapper_hic_ctrl_interface_down_t (hic_ctrl_interface_down_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_commit_suicide_req_t (hic_ctrl_commit_suicide_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_scb_error_req_t (hic_ctrl_scb_error_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_sleep_forever_req_t (hic_ctrl_sleep_forever_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_wmm_ps_period_start_req_t (m80211_nrp_mlme_wmm_ps_period_start_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_wakeup_ind_t (hic_ctrl_wakeup_ind_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_version_cfm_t (hic_ctrl_version_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_heartbeat_ind_t (hic_ctrl_heartbeat_ind_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_heartbeat_cfm_t (hic_ctrl_heartbeat_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_set_alignment_cfm_t (hic_ctrl_set_alignment_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_scb_error_cfm_t (hic_ctrl_scb_error_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_sleep_forever_cfm_t (hic_ctrl_sleep_forever_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_wmm_ps_period_start_cfm_t (m80211_nrp_mlme_wmm_ps_period_start_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_scb_error_ind_t (hic_ctrl_scb_error_ind_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_set_scanparam_req_t (m80211_nrp_mlme_set_scanparam_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_set_scanparam_cfm_t (m80211_nrp_mlme_set_scanparam_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_add_scanfilter_req_t (m80211_nrp_mlme_add_scanfilter_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_add_scanfilter_cfm_t (m80211_nrp_mlme_add_scanfilter_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_remove_scanfilter_req_t (m80211_nrp_mlme_remove_scanfilter_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_remove_scanfilter_cfm_t (m80211_nrp_mlme_remove_scanfilter_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_add_scanjob_req_t (m80211_nrp_mlme_add_scanjob_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_add_scanjob_cfm_t (m80211_nrp_mlme_add_scanjob_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_remove_scanjob_req_t (m80211_nrp_mlme_remove_scanjob_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_remove_scanjob_cfm_t (m80211_nrp_mlme_remove_scanjob_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_get_scanfilter_req_t (m80211_nrp_mlme_get_scanfilter_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_get_scanfilter_cfm_t (m80211_nrp_mlme_get_scanfilter_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_set_scanjobstate_req_t (m80211_nrp_mlme_set_scanjobstate_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_set_scanjobstate_cfm_t (m80211_nrp_mlme_set_scanjobstate_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_scannotification_ind_t (m80211_nrp_mlme_scannotification_ind_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_set_scancountryinfo_req_t (m80211_nrp_mlme_set_scancountryinfo_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_set_scancountryinfo_cfm_t (m80211_nrp_mlme_set_scancountryinfo_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

#if (DE_CCX == CFG_INCLUDED)
void HicWrapper_m80211_nrp_mlme_addts_req_t (m80211_nrp_mlme_addts_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_addts_req_body_t(m80211_nrp_mlme_addts_req_body_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_addts_ind_t(m80211_nrp_mlme_addts_ind_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_delts_req_t(m80211_nrp_mlme_delts_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_delts_cfm_t (m80211_nrp_mlme_delts_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_wmm_action_t (m80211_wmm_action_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_addts_cfm_t (m80211_nrp_mlme_addts_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_fw_stats_req_t(m80211_nrp_mlme_fw_stats_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_m80211_nrp_mlme_fw_stats_cfm_t(m80211_nrp_mlme_fw_stats_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);
#endif //DE_CCX
void HicWrapper_hic_ctrl_msg_body_t (hic_ctrl_msg_body_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_mac_console_req_t (hic_mac_console_req_t *object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_mac_console_cfm_t (hic_mac_console_cfm_t *object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_mac_console_ind_t (hic_mac_console_ind_t *object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_dlm_load_req_t (hic_dlm_load_req_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_dlm_load_failed_ind_t(hic_dlm_load_failed_ind_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_dlm_load_cfm_t (hic_dlm_load_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_dlm_swap_ind_t (hic_dlm_swap_ind_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_hl_sync_cfm_t(hic_ctrl_hl_sync_cfm_t* object_p, Blob_t* blob, WrapperAction_t action);

void HicWrapper_hic_ctrl_hl_sync_req_t(hic_ctrl_hl_sync_req_t* object_p, Blob_t* blob, WrapperAction_t action);

/* END GENERATED PROTOS */

#endif /* HIC_WRAPPER_H */

