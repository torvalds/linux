/**** AUTO GENERATED ****/

#ifndef MAC_WRAPPER_H
#define MAC_WRAPPER_H

#include "mac_mgmt_defs.h"

/*****************************************************************************
C O N S T A N T S / M A C R O S
*****************************************************************************/
#define MacWrapper_char                            HicWrapper_uint8_t
#define MacWrapper_int(_o,_b,_a)                   HicWrapper_uint32_t((uint32_t*)(_o), (_b), (_a))
#define MacWrapper_int8_t                          HicWrapper_int8_t
#define MacWrapper_uint8_t                         HicWrapper_uint8_t
#define MacWrapper_uint16_t                        HicWrapper_uint16_t
#define MacWrapper_uint32_t                        HicWrapper_uint32_t
#define MacWrapper_uint64_t                        HicWrapper_uint64_t
#define MacWrapper_array_t                         HicWrapper_array_t
#define MacWrapper_m80211_mac_addr_t(_o,_b,_a)     HicWrapper_array_t((char *)(_o), sizeof(m80211_mac_addr_t), (_b), (_a))
#define MacWrapper_mac_mmpdu_reassociate_rsp_t     MacWrapper_mac_mmpdu_associate_rsp_t
#define MacWrapper_common_IEs_t                    HicWrapper_common_IEs_t
#define MacWrapper_m80211_country_string_t         HicWrapper_m80211_country_string_t
#define MacWrapper_mac_mmpdu_probe_req_t           HicWrapper_common_IEs_t
#define MacWrapper_mac_mmpdu_beacon_ind_t          HicWrapper_mac_mmpdu_beacon_ind_t 
#define MacWrapper_mac_mmpdu_probe_rsp_t           HicWrapper_mac_mmpdu_beacon_ind_t 
#define MacWrapper_m80211_measurement_types_t      MacWrapper_uint8_t

typedef enum         /* Actions on blobs. */
{
   ACTION_PACK,      /* Pack a blob. */
   ACTION_UNPACK,    /* Unpack a blob. */
   ACTION_GET_SIZE   /* Calculate size of a blob. */
} WrapperAction_t;

/*****************************************************************************
G L O B A L   D A T A T Y P E S
*****************************************************************************/
typedef struct
{
   uint16_t length;
   uint16_t index;
   uint16_t ie_first_index;
   uint16_t ie_current_index;
   uint32_t ie_map;   
   bool_t   status;
   char*    buffer;
   void*    structure;
} Blob_t;

void    INIT_BLOB(Blob_t * _blob, char * _ptr, uint16_t _len);

#define BLOB_UNKNOWN_SIZE 0xFFFF
#define BLOB_GET_STATUS(_blob)            (_blob)->status

#define BLOB_RESET_IE(_blob)              (_blob)->ie_first_index = 0
#define BLOB_IS_FIRST_IE(_blob)           ((_blob)->ie_first_index == 0)
#define BLOB_PUSH_IE(_blob)               (_blob)->ie_first_index = (_blob)->index
#define BLOB_POP_IE(_blob)                (_blob)->index = (_blob)->ie_first_index
#define BLOB_SKIP_IE(_blob, _length)      (_blob)->ie_first_index = (_blob)->ie_current_index + (_length)
#define BLOB_RESTORE_IE(_blob1, _blob2)   (_blob1)->ie_first_index = (_blob2)->ie_first_index
#define BLOB_SET_CURRENT_IE(_blob)        (_blob)->ie_current_index = (_blob)->index
#define BLOB_POP_CURRENT_IE(_blob)        (_blob)->index = (_blob)->ie_current_index

#define BLOB_BUF_RESIZE(_blob, _length)   (_blob)->length = (_length)
#define BLOB_BUF_SIZE(_blob)              (_blob)->length
#define BLOB_SKIP(_blob, _length)         (_blob)->index += (_length)
#define BLOB_CURRENT_POS(_blob)         &((_blob)->buffer[(_blob)->index])
#define BLOB_CURRENT_SIZE(_blob)          (_blob)->index
#define BLOB_REMAINING_SIZE(_blob)        (((_blob)->length - (_blob)->index))
#define BLOB_PAD(_blob, _length)          (_blob)->index += (_length)

#define BLOB_ATTACH_WRAPPER_STRUCTURE(_blob, _structure) (_blob)->structure = _structure

struct wrapper_alloc_buf
{
   void* next_in_chain;   
#ifdef SUPPORT_STRUCTURE_COPY
   size_t   this_buf_size;
#endif /*  SUPPORT_STRUCTURE_COPY */
};


/*****************************************************************************
G L O B A L   C O N S T A N T S / V A R I A B L E S
*****************************************************************************/
#define MacWrapper_VERSION_ID 0xAE344842L

/*****************************************************************************
G L O B A L   F U N C T I O N S
*****************************************************************************/

void* WrapperAllocStructure(Blob_t* blob, int size);
void* WrapperAttachStructure(void* context_p, int size);
void* WrapperCopyStructure(void* source_structure_p);
void  WrapperFreeStructure(void* structure_ref);

void WrapperCopy_m80211_ie_tim_t(void* context_p, m80211_ie_tim_t* dest, m80211_ie_tim_t* source);
void WrapperCopy_m80211_ie_country_t(void* context_p, m80211_ie_country_t* dest, m80211_ie_country_t* source);
void WrapperCopy_m80211_ie_wpa_parameter_set_t(void* context_p, m80211_ie_wpa_parameter_set_t* dest, m80211_ie_wpa_parameter_set_t* source);
void WrapperCopy_m80211_ie_wps_parameter_set_t(void* context_p, m80211_ie_wps_parameter_set_t* dest, m80211_ie_wps_parameter_set_t* source);
#if (DE_CCX == CFG_INCLUDED)
void WrapperCopy_m80211_ie_qbss_load_t(void* context_p, m80211_ie_qbss_load_t* dest, m80211_ie_qbss_load_t* source);
void WrapperCopy_m80211_ie_ccx_parameter_set_t(void* context_p, m80211_ie_ccx_parameter_set_t* dest, m80211_ie_ccx_parameter_set_t* source);
void WrapperCopy_m80211_ie_ccx_rm_parameter_set_t(void* context_p, m80211_ie_ccx_rm_parameter_set_t* dest, m80211_ie_ccx_rm_parameter_set_t* source);
void WrapperCopy_m80211_ie_ccx_cpl_parameter_set_t(void* context_p, m80211_ie_ccx_cpl_parameter_set_t* dest, m80211_ie_ccx_cpl_parameter_set_t* source);
void WrapperCopy_m80211_ie_ccx_tsm_parameter_set_t(void* context_p, m80211_ie_ccx_tsm_parameter_set_t* dest, m80211_ie_ccx_tsm_parameter_set_t* source);
void WrapperCopy_m80211_ie_wmm_tspec_parameter_set_t(void* context_p, m80211_wmm_tspec_ie_t* dest, m80211_wmm_tspec_ie_t* source);
void WrapperCopy_m80211_ie_ccx_reassoc_req_parameter_set_t(void* context_p, m80211_ie_ccx_reassoc_req_parameter_set_t* dest, m80211_ie_ccx_reassoc_req_parameter_set_t* source);
void WrapperCopy_m80211_ie_ccx_reassoc_rsp_parameter_set_t(void* context_p, m80211_ie_ccx_reassoc_rsp_parameter_set_t* dest, m80211_ie_ccx_reassoc_rsp_parameter_set_t* source);
#if 0
void WrapperCopy_m80211_ie_ccx_adj_parameter_set_t(void* context_p, m80211_ie_ccx_adj_parameter_set_t* dest, m80211_ie_ccx_adj_parameter_set_t* source);
#endif
#endif //DE_CCX
void WrapperCopy_m80211_ie_rsn_parameter_set_t(void* context_p, m80211_ie_rsn_parameter_set_t* dest, m80211_ie_rsn_parameter_set_t* source);
void WrapperCopy_m80211_ie_wapi_parameter_set_t(void* context_p, m80211_ie_wapi_parameter_set_t* dest, m80211_ie_wapi_parameter_set_t* source);
void WrapperCopy_m80211_remaining_IEs_t(void* context_p, m80211_remaining_IEs_t* dest_p, m80211_remaining_IEs_t* source_p);

void MacWrapper_m80211_ie_ht_capabilities_t(m80211_ie_ht_capabilities_t*, 
                                            Blob_t*, 
                                            WrapperAction_t);
void MacWrapper_m80211_ie_ht_operation_t(m80211_ie_ht_operation_t*, 
                                         Blob_t*, 
                                         WrapperAction_t);

ie_ref_t MacWrapper_m80211_remaining_IEs_t_Insert(void* context_p, m80211_remaining_IEs_t* dest_p, ie_ref_t ie_p);
ie_ref_t MacWrapper_m80211_remaining_IEs_t_Locate(m80211_remaining_IEs_t* remaining_ies, m80211_ie_id_t id, int vendor_specific_oui, int oui_sub_type);
ie_ref_t MacWrapper_m80211_remaining_IEs_t_LocateNext(ie_ref_t ie_ref, bool_t skip_current, m80211_ie_id_t id, int vendor_specific_oui, int oui_sub_type);
void     MacWrapper_m80211_remaining_IEs_t_Remove(m80211_remaining_IEs_t* remaining_ies, m80211_ie_id_t id);
ie_ref_t MacWrapper_m80211_remaining_IEs_t_InsertNew(void* context_p, m80211_remaining_IEs_t* dest_p, m80211_ie_id_t id, int ie_length);      


/*****************************************************************************
O P T I M I Z E D   M A C R O S
*****************************************************************************/
#define IE_HDR_LENGTH   2

#define IE_ID(ie_p)     (ie_p)[0]
#define IE_LENGTH(ie_p) (ie_p)[1]

#define SET_SINGLE_REMAINING_IE(_remaining_p, _ie_p) (_remaining_p)->count = 1;(_remaining_p)->buffer_ref = (ie_ref_t)(_ie_p)
     
#define IE_COUNTRY_LENGTH(count)       (IE_HDR_LENGTH + sizeof(m80211_country_string_t) + count*M80211_IE_CHANNEL_INFO_TRIPLET_SIZE)
#define IE_COUNTRY_STRING(ie_p)        &(ie_p)[2]
#define IE_COUNTRY_CHANNEL_INFO(ie_p)  &(ie_p)[2+M80211_IE_LEN_COUNTRY_STRING]
#define IE_COUNTRY_CHANNEL_INFO_FIRST_CHANNEL(ie_p, _index)  ((char*)IE_COUNTRY_CHANNEL_INFO(ie_p)+(M80211_IE_CHANNEL_INFO_TRIPLET_SIZE*_index))
#define IE_COUNTRY_CHANNEL_INFO_NUM_CHANNELS(ie_p, _index)   ((char*)IE_COUNTRY_CHANNEL_INFO(ie_p)+(M80211_IE_CHANNEL_INFO_TRIPLET_SIZE*_index+1))
#define IE_COUNTRY_CHANNEL_INFO_MAX_TX_POWER(ie_p, _index)   ((char*)IE_COUNTRY_CHANNEL_INFO(ie_p)+(M80211_IE_CHANNEL_INFO_TRIPLET_SIZE*_index+2))

#define IE_DS_SIZE                        (IE_HDR_LENGTH + 1)
#define IE_DS_CHANNEL(_ie_p)              (_ie_p)[IE_HDR_LENGTH]

#define IE_ERP_INFO(_ie_p)                (_ie_p)[IE_HDR_LENGTH]
#define IE_REQ_INFO_SET_INFO(_ie_p)       (_ie_p)[IE_HDR_LENGTH]
#define IE_ERP_SIZE                       (IE_HDR_LENGTH + 1)

#define IE_TIM_DTIM_TIMING_REF(_ie_p)     ((m80211_tbtt_timing_t*)&((_ie_p)[IE_HDR_LENGTH]))
#define IE_TIM_BITMAP_CONTROL(_ie_p)      (_ie_p)[IE_HDR_LENGTH+2]
#define IE_TIM_BITMAP(_ie_p)              ((uint8_t*)((char*)_ie_p + IE_HDR_LENGTH + 3))

#define IE_VENDOR_SPECIFIC_HEADER(_ie_p)        ((m80211_ie_vendor_specific_hdr_t*)(_ie_p))
#define IE_VENDOR_SPECIFIC_HEADER_OUI_1(_ie_p)  ((uint8_t*)((char*)(_ie_p) + IE_HDR_LENGTH))

#define IE_WMM_HEADER(_ie_p)                    ((m80211_ie_WMM_header_t*)(_ie_p))

#define IE_WPA_GROUP_CIPHER_SELECTOR(_ie_p)     (uint8_t*)((char*)_ie_p + IE_HDR_LENGTH + 6)
#define IE_WPA_GROUP_CIPHER_TYPE(_ie_p)         (_ie_p)[IE_HDR_LENGTH + 9]
#define IE_WPA_PAIRWISE_CIPHER_COUNT_REF(_ie_p) (uint16_t*)((char*)_ie_p + IE_HDR_LENGTH + 10)
#define IE_WPA_PAIRWISE_CIPHER_SELECTORS(_ie_p) (uint8_t*)(IE_WPA_PAIRWISE_CIPHER_COUNT_REF(_ie_p)+1)
#define IE_WPA_PAIRWISE_AKM_COUNT_REF(_ie_p)    (uint16_t*)(IE_WPA_PAIRWISE_CIPHER_SELECTORS(_ie_p)+*IE_WPA_PAIRWISE_CIPHER_COUNT_REF(_ie_p))
#define IE_WPA_PAIRWISE_AKM_SELECTORS(_ie_p)    (uint8_t*)(IE_WPA_PAIRWISE_AKM_COUNT_REF(_ie_p)+1)


/*****************************************************************************
G L O B A L   A C C E S S   F U N C T I O N S
*****************************************************************************/
bool_t MacWrapper_m80211_ie_hdr_t(m80211_ie_hdr_t* object_p, Blob_t* blob, WrapperAction_t action, uint8_t id, uint16_t min, uint16_t max);
bool_t MacWrapper_m80211_ie_vendor_specific_hdr_t(m80211_ie_vendor_specific_hdr_t* object_p, Blob_t* blob, WrapperAction_t action, uint8_t OUI_type, int *ie_index);
bool_t MacWrapper_m80211_ie_wapi_vendor_specific_hdr_t(m80211_ie_wapi_vendor_specific_hdr_t* object_p, Blob_t* blob, WrapperAction_t action);
bool_t MacWrapper_m80211_ie_WMM_header_t(m80211_ie_WMM_header_t* object_p, Blob_t* blob, WrapperAction_t action, uint8_t sub_type);
void   MacWrapper_m80211_ie_ssid_t(m80211_ie_ssid_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_supported_rates_t(m80211_ie_supported_rates_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_ext_supported_rates_t(m80211_ie_ext_supported_rates_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_tim_t(m80211_ie_tim_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_rsn_parameter_set_t(m80211_ie_rsn_parameter_set_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_wpa_parameter_set_t(m80211_ie_wpa_parameter_set_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_challenge_text_t(m80211_ie_challenge_text_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_ibss_par_set_t(m80211_ie_ibss_par_set_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_request_info_t(m80211_ie_request_info_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_country_t(m80211_ie_country_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_cf_par_set_t(m80211_ie_cf_par_set_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_ds_par_set_t(m80211_ie_ds_par_set_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_fh_par_set_t(m80211_ie_fh_par_set_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_erp_t(m80211_ie_erp_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_WMM_information_element_t(m80211_ie_WMM_information_element_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_WMM_parameter_element_t(m80211_ie_WMM_parameter_element_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_qos_capability_t(m80211_ie_qos_capability_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_wps_parameter_set_t(m80211_ie_wps_parameter_set_t* object_p, Blob_t* blob, WrapperAction_t action);
#if (DE_CCX == CFG_INCLUDED)
void   MacWrapper_m80211_ie_qbss_load_t(m80211_ie_qbss_load_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_ccx_parameter_set_t(m80211_ie_ccx_parameter_set_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_ccx_rm_parameter_set_t(m80211_ie_ccx_rm_parameter_set_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_ccx_cpl_parameter_set_t(m80211_ie_ccx_cpl_parameter_set_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_ccx_tsm_parameter_set_t(m80211_ie_ccx_tsm_parameter_set_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_wmm_tspec_parameter_set_t(m80211_wmm_tspec_ie_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_ccx_reassoc_req_parameter_set_t(m80211_ie_ccx_reassoc_req_parameter_set_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_ie_ccx_reassoc_rsp_parameter_set_t(m80211_ie_ccx_reassoc_rsp_parameter_set_t* object_p, Blob_t* blob, WrapperAction_t action);
#if 0
void   MacWrapper_m80211_ie_ccx_adj_parameter_set_t(m80211_ie_ccx_adj_parameter_set_t* object_p, Blob_t* blob, WrapperAction_t action);
#endif
#endif //DE_CCX
void   MacWrapper_m80211_ie_wapi_parameter_set_t(m80211_ie_wapi_parameter_set_t* object_p, Blob_t* blob, WrapperAction_t action);
void   MacWrapper_m80211_remaining_IEs_t(m80211_remaining_IEs_t* object_p, Blob_t* blob, WrapperAction_t action);

void MacWrapper_m80211_mgmt_status_t(m80211_mgmt_status_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_ie_id_t(m80211_ie_id_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_ie_len_t(m80211_ie_len_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_oui_id_t(m80211_oui_id_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_cipher_suite_t(m80211_cipher_suite_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_akm_suite_t(m80211_akm_suite_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_measurement_mode_t(m80211_measurement_mode_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_measurement_report_mode_t(m80211_measurement_report_mode_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_action_category_t(m80211_action_category_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_radio_measurement_action_detail_t(m80211_radio_measurement_action_detail_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_beacon_mode_t(m80211_beacon_mode_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_beacon_report_condition_t(m80211_beacon_report_condition_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_threshold_offset(m80211_threshold_offset* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_location_subject_t(m80211_location_subject_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_latitude_requested_resolution_t(m80211_latitude_requested_resolution_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_longitude_requested_resolution_t(m80211_longitude_requested_resolution_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_altitude_requested_resolution_t(m80211_altitude_requested_resolution_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_azimuth_request_t(m80211_azimuth_request_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_cipher_suite_selector_t(m80211_cipher_suite_selector_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_akm_suite_selector_t(m80211_akm_suite_selector_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_pmkid_selector_t(m80211_pmkid_selector_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_suite_selector_t(suite_selector_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_max_regulatory_power_t(m80211_max_regulatory_power_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_max_tx_power_t(m80211_max_tx_power_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_tx_power_used_t(m80211_tx_power_used_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_trx_noise_floor_t(m80211_trx_noise_floor_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_meas_req_hdr_t(meas_req_hdr_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_meas_rep_hdr_t(meas_rep_hdr_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_radio_meas_req_hdr_t(radio_meas_req_hdr_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_radio_meas_rep_hdr_t(radio_meas_rep_hdr_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_lci_req_t(lci_req_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_channel_load_req_t(channel_load_req_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_channel_load_rep_t(channel_load_rep_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_noise_histogram_req_t(noise_histogram_req_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_noise_histogram_rep_t(noise_histogram_rep_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_beacon_req_t(beacon_req_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_beacon_rep_t(beacon_rep_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211k_request(m80211k_request* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211k_report(m80211k_report* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_ie_measurement_request_t(m80211_ie_measurement_request_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_ie_measurement_report_t(m80211_ie_measurement_report_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_radio_measurement_req_t(m80211_radio_measurement_req_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_m80211_radio_measurement_rep_t(m80211_radio_measurement_rep_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_AC_parameters_t(AC_parameters_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_mac_mmpdu_mpilot_t(mac_mmpdu_mpilot_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_mac_mmpdu_authenticate_t(mac_mmpdu_authenticate_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_mac_mmpdu_deauthenticate_t(mac_mmpdu_deauthenticate_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_mac_mmpdu_associate_req_t(mac_mmpdu_associate_req_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_mac_mmpdu_associate_rsp_t(mac_mmpdu_associate_rsp_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_mac_mmpdu_reassociate_req_t(mac_mmpdu_reassociate_req_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_mac_mmpdu_disassociate_t(mac_mmpdu_disassociate_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_mac_mmpdu_radio_measurement_req_t(mac_mmpdu_radio_measurement_req_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_mac_mmpdu_radio_measurement_rep_t(mac_mmpdu_radio_measurement_rep_t* object_p, Blob_t* blob, WrapperAction_t action);
void MacWrapper_mac_mgmt_body_t(mac_mgmt_body_t* object_p, Blob_t* blob, WrapperAction_t action);

#endif /* MAC_WRAPPER_H */

/* E N D  O F  F I L E *******************************************************/

