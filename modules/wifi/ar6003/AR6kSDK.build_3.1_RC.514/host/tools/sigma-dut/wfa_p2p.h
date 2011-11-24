
#ifndef _WFA_P2P_H_
#define _WFA_P2P_H_

#include "wfa_portall.h"
#include "wfa_debug.h"
#include "wfa_ver.h"
#include "wfa_main.h"
#include "wfa_types.h"
#include "wfa_ca.h"
#include "wfa_tlv.h"
#include "wfa_sock.h"
#include "wfa_tg.h"
#include "wfa_miscs.h"
#include "wfa_agt.h"
#include "wfa_rsp.h"
#include "wfa_cmds.h"


extern int cmd_device_get_info(struct sigma_dut *dut, dutCommand_t *command,
                        		      dutCmdResponse_t *resp);

extern int cmd_sta_get_p2p_dev_address(struct sigma_dut *dut, dutCommand_t *command,
                        		      dutCmdResponse_t *resp);

extern int cmd_sta_set_p2p(struct sigma_dut *dut, caStaSetP2p_t *command, dutCmdResponse_t *resp);

extern int cmd_sta_start_autonomous_go(struct sigma_dut *dut, caStaStartAutoGo_t *command, dutCmdResponse_t *resp);
extern int cmd_sta_p2p_connect(struct sigma_dut *dut, caStaP2pConnect_t *command, dutCmdResponse_t *resp);

extern int cmd_sta_p2p_start_group_formation(struct sigma_dut *dut,
					     caStaP2pStartGrpForm_t *command,
					     dutCmdResponse_t *cmdresp);

extern int cmd_sta_p2p_dissolve(struct sigma_dut *dut, caStaP2pDissolve_t *command,
                    dutCmdResponse_t *cmdresp);

extern int cmd_sta_send_p2p_invitation_req(struct sigma_dut *dut,
					   caStaSendP2pInvReq_t *command,
					   dutCmdResponse_t *cmdresp);

extern int cmd_sta_accept_p2p_invitation_req(struct sigma_dut *dut,
					     caStaAcceptP2pInvReq_t *command,
					     dutCmdResponse_t *cmdresp);

extern int cmd_sta_send_p2p_provision_dis_req(struct sigma_dut *dut,
					      caStaSendP2pProvDisReq_t *command,
					      dutCmdResponse_t *cmdresp);

extern int cmd_sta_set_wps_pbc(struct sigma_dut *dut, caStaSetWpsPbc_t *command,
			       dutCmdResponse_t *cmdresp);


extern int cmd_sta_wps_read_pin(struct sigma_dut *dut, caStaWpsReadPin_t *command,
				dutCmdResponse_t *cmdresp);

extern int cmd_sta_wps_read_label(struct sigma_dut *dut,
				  caStaWpsReadLabel_t *command,
				  dutCmdResponse_t *cmdresp);

extern  int cmd_sta_wps_enter_pin(struct sigma_dut *dut,
				 caStaWpsEnterPin_t *command,
				 dutCmdResponse_t *cmdresp);

extern int cmd_sta_get_psk(struct sigma_dut *dut, caStaGetPsk_t *command,
			   dutCmdResponse_t *cmdresp);


extern int cmd_sta_p2p_reset(struct sigma_dut *dut, dutCommand_t *command,
		      dutCmdResponse_t *resp);

extern int cmd_sta_get_p2p_ip_config(struct sigma_dut *dut,
				     caStaGetP2pIpConfig_t *command,
				     dutCmdResponse_t *cmdRes);


extern int cmd_sta_send_p2p_presence_req(struct sigma_dut *dut,
					 caStaSendP2pPresenceReq_t *command,
					 dutCmdResponse_t *cmdRes);

extern int cmd_sta_set_sleep(struct sigma_dut *dut, caStaSetSleep_t *command,
                     dutCmdResponse_t *cmdRes);

extern int cmd_sta_send_service_discovery_req(struct sigma_dut *dut,
					      caStaSendServiceDiscoveryReq_t *command,
					      dutCmdResponse_t *cmdRes);

extern int cmd_sta_set_opportunistic_ps(struct sigma_dut *dut,
					caStaSetOpprPs_t *command,
					dutCmdResponse_t *cmdRes);

extern int cmd_sta_add_arp_table_entry(struct sigma_dut *dut,
				       caStaAddARPTableEntry_t *command,
				       dutCmdResponse_t *cmdRes);

extern int cmd_sta_block_icmp_response(struct sigma_dut *dut,
				       caStaBlockICMPResponse_t *command,
				       dutCmdResponse_t *cmdRes);

extern int cmd_traffic_send_ping(struct sigma_dut *dut,
                                        tgPingStart_t *staPing, 
                                        dutCmdResponse_t *spresp);

int cmd_traffic_stop_ping(struct sigma_dut *dut,
				 int      streamID,
				 dutCmdResponse_t *spresp);


#endif
