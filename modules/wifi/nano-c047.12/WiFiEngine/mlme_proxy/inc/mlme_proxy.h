#ifndef MLME_PROXY_H
#define MLME_PROXY_H

#include "driverenv.h"
#include "wifi_engine_internal.h"

/******************************************************************************
S T R U C T U R E S / E X T E R N A L S
******************************************************************************/

#define MAX_CANDIDATE_INFO 16
typedef struct 
{
   m80211_mac_addr_t            bssId;
   int32_t         rssi_info;
   uint16_t        flag;
} CandidateInfo;

extern CandidateInfo   listCandidateInfo[MAX_CANDIDATE_INFO];
extern int             num_candidates;
extern m80211_ie_ssid_t candidatesOfSsid;

typedef bool_t (*mlme_primitive_fn_t)(hic_message_context_t* msg_ref, int param);
typedef int    (*mlme_send_fn_t)(hic_message_context_t *ctx); 

typedef int (*mlme_scan_filter_t)(mlme_bss_description_t *bss_p);
void mlme_set_scan_filter(mlme_scan_filter_t filter);

/******************************************************************************
C O N S T A N T S / M A C R O S
******************************************************************************/
#define NET_BROADCAST   -1
#define NET_BY_PRIORITY -2

#ifdef packed
#undef packed /* RTK-E has this as a macro */
#endif

#define Mlme_CreateMessageContext(_context) \
   (_context).raw = NULL; \
   (_context).packed = NULL;

#define Mlme_ReleaseMessageContext(_context) \
   if ((_context).raw != NULL) WrapperFreeStructure((_context).raw);    \
   if ((_context).packed != NULL) DriverEnvironment_TX_Free((_context).packed);


/******************************************************************************
F U N C T I O N  P R O T O Y P E S
******************************************************************************/
bool_t   Mlme_Send(mlme_primitive_fn_t mlme_primitive, int param, mlme_send_fn_t send_fn);

int      Mlme_CreateScanRequest(hic_message_context_t* msg_ref, uint8_t scan_type, 
                                uint32_t job_id, 
                                uint16_t channel_interval, uint32_t probe_delay, 
                                uint16_t min_ch_time, uint16_t max_ch_time);
bool_t   Mlme_HandleScanInd(hic_message_context_t* msg_ref);
int      Mlme_CreateJoinRequest(hic_message_context_t* msg_ref, int dummy);
bool_t   Mlme_HandleJoinConfirm(m80211_mlme_join_cfm_t *join_cfm_p);
int      Mlme_CreateAuthenticateRequest(hic_message_context_t* msg_ref, int dummy);
int      Mlme_CreateAuthenticateCfm(hic_message_context_t* msg_ref, int dummy);
bool_t   Mlme_HandleAuthenticateConfirm(m80211_mlme_authenticate_cfm_t *authenticate_cfm_p);
int      Mlme_CreateDeauthenticateRequest(hic_message_context_t* msg_ref, int dummy);
bool_t   Mlme_HandleDeauthenticateConfirm(m80211_mlme_deauthenticate_cfm_t *deauthenticate_cfm_p);
bool_t   Mlme_HandleAuthenticateInd(m80211_mlme_authenticate_ind_t *ind_p);
bool_t   Mlme_HandleDeauthenticateInd(m80211_mlme_deauthenticate_ind_t *ind_p);
int      Mlme_CreateAssociateRequest(hic_message_context_t* msg_ref, int dummy);
bool_t   Mlme_HandleAssociateConfirm(m80211_mlme_associate_cfm_t *associate_cfm_p);
int      Mlme_CreateDisassociateRequest(hic_message_context_t* msg_ref, int dummy);
bool_t   Mlme_HandleDisassociateConfirm(m80211_mlme_disassociate_cfm_t *disassociate_cfm_p);
bool_t   Mlme_HandleDisassociateInd(m80211_mlme_disassociate_ind_t *ind_p);
int      Mlme_CreateReassociateRequest(hic_message_context_t* msg_ref, int dummy);
bool_t   Mlme_HandleReassociateConfirm(m80211_mlme_associate_cfm_t *associate_cfm_p);
bool_t   Mlme_CreateMIBGetRequest(hic_message_context_t* msg_ref, mib_id_t mib_id, uint32_t *trans_id);
bool_t   Mlme_CreateMIBSetRequest(hic_message_context_t* msg_ref, mib_id_t mib_id,
                                  const void *inbuf, uint16_t inbuflen, uint32_t *trans_id);
#if DE_MIB_TABLE_SUPPORT == CFG_ON
bool_t   Mlme_CreateMIBGetRawRequest(hic_message_context_t* msg_ref, mib_id_t mib_id, uint32_t *trans_id);
bool_t   Mlme_CreateMIBSetRawRequest(hic_message_context_t* msg_ref, mib_id_t mib_id,
                                  const void *inbuf, uint16_t inbuflen, uint32_t *trans_id);
#endif
bool_t   Mlme_CreatePowerManagementRequest(hic_message_context_t* msg_ref, int mode);
bool_t   Mlme_HandlePowerManagementConfirm(m80211_mlme_power_mgmt_cfm_t *cfm_p);
bool_t   Mlme_CreateSetKeyRequest(hic_message_context_t* msg_ref, int key_idx, size_t key_len, 
                                  const void *key, m80211_cipher_suite_t suite, 
                                  m80211_key_type_t key_type, bool_t config_by_authenticator, 
                                  m80211_mac_addr_t *bssid, receive_seq_cnt_t *rsc);
bool_t   Mlme_CreateDeleteKeyRequest(hic_message_context_t* msg_ref, int key_idx, m80211_key_type_t key_type, m80211_mac_addr_t *bssid);
bool_t   Mlme_CreateSetProtectionReq(hic_message_context_t* msg_ref, m80211_mac_addr_t *bssid,
                                     m80211_protect_type_t *prot, m80211_key_type_t *key_type);
bool_t   Mlme_CreateLeaveBSSRequest(hic_message_context_t* msg_ref, int dummy);
bool_t   Mlme_HandleLeaveBSSConfirm(m80211_nrp_mlme_bss_leave_cfm_t *cfm);
bool_t   Mlme_CreateLeaveIBSSRequest(hic_message_context_t* msg_ref, int dummy);
bool_t   Mlme_HandleLeaveIBSSConfirm(m80211_nrp_mlme_ibss_leave_cfm_t *cfm);
int      Mlme_CreateStartRequest(hic_message_context_t* msg_ref, int dummy);
bool_t   Mlme_HandleStartConfirm(m80211_mlme_start_cfm_t *start_cfm_p);
bool_t   Mlme_CreateSetAlignmentReq(hic_message_context_t* msg_ref,
                                    uint16_t min_sz,
                                    uint16_t padding_sz,
                                    uint8_t  rx_hdr_payload_offset,
                                    uint32_t trans_id,
                                    uint8_t  hAttention,
                                    uint8_t  swap,
                                    uint8_t  hWakeup,
                                    uint8_t  hForceInterval,
                                    uint8_t  tx_window_size);
bool_t   Mlme_HandleSetAlignmentConfirm(hic_message_context_t* msg_ref, uint32_t *trans_id);
bool_t   Mlme_HandleHICWakeupInd(hic_ctrl_wakeup_ind_t *ind_p);
bool_t   Mlme_CreateWakeUpInd(hic_message_context_t* msg_ref);
bool_t   Mlme_CreateHICWMMPeriodStartReq(hic_message_context_t* msg_ref, int dummy);
bool_t   Mlme_HandleHICWMMPeriodStartCfm(m80211_nrp_mlme_wmm_ps_period_start_cfm_t *cfm_p, uint32_t *result);
bool_t   Mlme_CreateHICInterfaceDown(hic_message_context_t* msg_ref, int dummy);
bool_t   Mlme_HandleBssPeerStatusInd(m80211_nrp_mlme_peer_status_ind_t *sta_ind_p);
bool_t   Mlme_CreateScbErrorReq(hic_message_context_t* msg_ref, char *dst_str);
bool_t   Mlme_CreateHeartbeatReq(hic_message_context_t* msg_ref, uint8_t control, uint32_t interval);
bool_t   Mlme_CreateCommitSuicideReq(hic_message_context_t* msg_ref);
bool_t   Mlme_CreateInitCompleteReq(hic_message_context_t* msg_ref, int dummy);
bool_t   Mlme_CreateSleepForeverReq(hic_message_context_t* msg_ref, int dummy);
bool_t   Mlme_HandleHICInterfaceSleepForeverConfirm(hic_ctrl_sleep_forever_cfm_t *cfm_p);
bool_t   Mlme_HandleHICInitCompleteConfirm(hic_ctrl_init_completed_cfm_t *cfm_p);
bool_t   Mlme_InitBssDescription(mlme_bss_description_t* bss_p);
bool_t   Mlme_HandleMichaelMICFailureInd(m80211_mlme_michael_mic_failure_ind_t *ind);
bool_t   Mlme_CreateMIBSetTriggerRequest(hic_message_context_t* msg_ref, mib_id_t mib_id, 
                   uint32_t          gating_trig_id,
                   uint32_t          supv_interval,
                   int32_t           level,
                   uint16_t          event,
                   uint16_t          event_count,
                   uint16_t          triggmode,
                   uint32_t *trans_id);
bool_t Mlme_ChangeGatingMIBSetTriggerRequest(hic_message_context_t* msg_ref,
                                             uint32_t          gating_trig_id);
bool_t Mlme_CreateMIBRemoveTriggerRequest(hic_message_context_t* msg_ref,
                 uint32_t trig_id);
bool_t Mlme_CreateMIBSetGatingtriggerRequest(hic_message_context_t* msg_ref,
                                             uint32_t trig_id,
                                             uint32_t gating_trig_id,
                                             uint32_t *trans_id);

/* New scan interface */
int Mlme_CreateSetScanParamRequest(hic_message_context_t* msg_ref, 
                                   m80211_nrp_mlme_scan_config_t *sp,
                                   uint32_t *trans_id);
bool_t Mlme_HandleSetScanParamCfm(hic_message_context_t* msg_ref);
int Mlme_CreateAddScanFilterRequest(hic_message_context_t* msg_ref, 
                                    m80211_nrp_mlme_add_scanfilter_req_t *sp,
                                    uint32_t *trans_id);
bool_t Mlme_HandleAddScanFilterCfm(hic_message_context_t* msg_ref);
int Mlme_CreateRemoveScanFilterRequest(hic_message_context_t* msg_ref, 
                                       m80211_nrp_mlme_remove_scanfilter_req_t *sp,
                                       uint32_t *trans_id);
bool_t Mlme_HandleRemoveScanFilterCfm(hic_message_context_t* msg_ref);
int Mlme_CreateAddScanJobRequest(hic_message_context_t* msg_ref, 
                                 m80211_nrp_mlme_add_scanjob_req_t *sp,
                                 uint32_t *trans_id);
bool_t Mlme_HandleAddScanJobCfm(hic_message_context_t* msg_ref);
int Mlme_CreateRemoveScanJobRequest(hic_message_context_t* msg_ref, 
                                    m80211_nrp_mlme_remove_scanjob_req_t *sp,
                                    uint32_t *trans_id);
bool_t Mlme_HandleRemoveScanJobCfm(hic_message_context_t* msg_ref);
int Mlme_CreateSetScanJobStateRequest(hic_message_context_t* msg_ref, 
                                      m80211_nrp_mlme_set_scanjobstate_req_t *sp,
                                      uint32_t *trans_id);
bool_t Mlme_HandleSetScanJobStateCfm(hic_message_context_t* msg_ref);
int Mlme_CreateGetScanFilterRequest(hic_message_context_t* msg_ref, 
                                    m80211_nrp_mlme_get_scanfilter_req_t *sp,
                                    uint32_t *trans_id);
bool_t Mlme_HandleGetScanFilterCfm(hic_message_context_t* msg_ref);


int Mlme_CreateScanCountryInfoRequest(hic_message_context_t* msg_ref, 
                                      m80211_ie_country_t *ci);
bool_t Mlme_HandleScanCountryInfoCfm(hic_message_context_t* msg_ref);
#if (DE_CCX == CFG_INCLUDED)
int    Mlme_CreateADDTSRequest(hic_message_context_t* msg_ref, m80211_nrp_mlme_addts_req_t* myreq, uint32_t *trans_id);
bool_t Mlme_HandleADDTSConfirm(hic_message_context_t* msg_ref);
int    Mlme_CreateDELTSRequest(hic_message_context_t* msg_ref, m80211_nrp_mlme_delts_req_t* myreq, uint32_t *trans_id);
bool_t Mlme_HandleDeltsConfirm(hic_message_context_t* msg_ref);
int    Mlme_CreateFWStatsRequest(hic_message_context_t* msg_ref, bool_t init, uint32_t *trans_id);
void   Mlme_HandleFWStatsCfm(hic_message_context_t *msg_ref);
#endif //DE_CCX

bool_t Mlme_CreateConsoleRequest(hic_message_context_t *msg_ref, 
             const char *command, 
             uint32_t *trans_id);

bool_t Mlme_CreateSyncRequest(hic_message_context_t* msg_ref,
             uint32_t *trans_id);

int wei_build_assoc_req(WiFiEngine_net_t *net);

#endif /* MLME_PROXY_H */
