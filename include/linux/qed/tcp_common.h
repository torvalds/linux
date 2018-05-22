/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __TCP_COMMON__
#define __TCP_COMMON__

/********************/
/* TCP FW CONSTANTS */
/********************/

#define TCP_INVALID_TIMEOUT_VAL	-1

/* OOO opaque data received from LL2 */
struct ooo_opaque {
	__le32 cid;
	u8 drop_isle;
	u8 drop_size;
	u8 ooo_opcode;
	u8 ooo_isle;
};

/* tcp connect mode enum */
enum tcp_connect_mode {
	TCP_CONNECT_ACTIVE,
	TCP_CONNECT_PASSIVE,
	MAX_TCP_CONNECT_MODE
};

/* tcp function init parameters */
struct tcp_init_params {
	__le32 two_msl_timer;
	__le16 tx_sws_timer;
	u8 max_fin_rt;
	u8 reserved[9];
};

/* tcp IPv4/IPv6 enum */
enum tcp_ip_version {
	TCP_IPV4,
	TCP_IPV6,
	MAX_TCP_IP_VERSION
};

/* tcp offload parameters */
struct tcp_offload_params {
	__le16 local_mac_addr_lo;
	__le16 local_mac_addr_mid;
	__le16 local_mac_addr_hi;
	__le16 remote_mac_addr_lo;
	__le16 remote_mac_addr_mid;
	__le16 remote_mac_addr_hi;
	__le16 vlan_id;
	__le16 flags;
#define TCP_OFFLOAD_PARAMS_TS_EN_MASK			0x1
#define TCP_OFFLOAD_PARAMS_TS_EN_SHIFT			0
#define TCP_OFFLOAD_PARAMS_DA_EN_MASK			0x1
#define TCP_OFFLOAD_PARAMS_DA_EN_SHIFT			1
#define TCP_OFFLOAD_PARAMS_KA_EN_MASK			0x1
#define TCP_OFFLOAD_PARAMS_KA_EN_SHIFT			2
#define TCP_OFFLOAD_PARAMS_ECN_SENDER_EN_MASK		0x1
#define TCP_OFFLOAD_PARAMS_ECN_SENDER_EN_SHIFT		3
#define TCP_OFFLOAD_PARAMS_ECN_RECEIVER_EN_MASK		0x1
#define TCP_OFFLOAD_PARAMS_ECN_RECEIVER_EN_SHIFT	4
#define TCP_OFFLOAD_PARAMS_NAGLE_EN_MASK		0x1
#define TCP_OFFLOAD_PARAMS_NAGLE_EN_SHIFT		5
#define TCP_OFFLOAD_PARAMS_DA_CNT_EN_MASK		0x1
#define TCP_OFFLOAD_PARAMS_DA_CNT_EN_SHIFT		6
#define TCP_OFFLOAD_PARAMS_FIN_SENT_MASK		0x1
#define TCP_OFFLOAD_PARAMS_FIN_SENT_SHIFT		7
#define TCP_OFFLOAD_PARAMS_FIN_RECEIVED_MASK		0x1
#define TCP_OFFLOAD_PARAMS_FIN_RECEIVED_SHIFT		8
#define TCP_OFFLOAD_PARAMS_RESERVED_MASK		0x7F
#define TCP_OFFLOAD_PARAMS_RESERVED_SHIFT		9
	u8 ip_version;
	u8 reserved0[3];
	__le32 remote_ip[4];
	__le32 local_ip[4];
	__le32 flow_label;
	u8 ttl;
	u8 tos_or_tc;
	__le16 remote_port;
	__le16 local_port;
	__le16 mss;
	u8 rcv_wnd_scale;
	u8 connect_mode;
	__le16 srtt;
	__le32 ss_thresh;
	__le32 rcv_wnd;
	__le32 cwnd;
	u8 ka_max_probe_cnt;
	u8 dup_ack_theshold;
	__le16 reserved1;
	__le32 ka_timeout;
	__le32 ka_interval;
	__le32 max_rt_time;
	__le32 initial_rcv_wnd;
	__le32 rcv_next;
	__le32 snd_una;
	__le32 snd_next;
	__le32 snd_max;
	__le32 snd_wnd;
	__le32 snd_wl1;
	__le32 ts_recent;
	__le32 ts_recent_age;
	__le32 total_rt;
	__le32 ka_timeout_delta;
	__le32 rt_timeout_delta;
	u8 dup_ack_cnt;
	u8 snd_wnd_probe_cnt;
	u8 ka_probe_cnt;
	u8 rt_cnt;
	__le16 rtt_var;
	__le16 fw_internal;
	u8 snd_wnd_scale;
	u8 ack_frequency;
	__le16 da_timeout_value;
	__le32 reserved3;
};

/* tcp offload parameters */
struct tcp_offload_params_opt2 {
	__le16 local_mac_addr_lo;
	__le16 local_mac_addr_mid;
	__le16 local_mac_addr_hi;
	__le16 remote_mac_addr_lo;
	__le16 remote_mac_addr_mid;
	__le16 remote_mac_addr_hi;
	__le16 vlan_id;
	__le16 flags;
#define TCP_OFFLOAD_PARAMS_OPT2_TS_EN_MASK	0x1
#define TCP_OFFLOAD_PARAMS_OPT2_TS_EN_SHIFT	0
#define TCP_OFFLOAD_PARAMS_OPT2_DA_EN_MASK	0x1
#define TCP_OFFLOAD_PARAMS_OPT2_DA_EN_SHIFT	1
#define TCP_OFFLOAD_PARAMS_OPT2_KA_EN_MASK	0x1
#define TCP_OFFLOAD_PARAMS_OPT2_KA_EN_SHIFT	2
#define TCP_OFFLOAD_PARAMS_OPT2_ECN_EN_MASK	0x1
#define TCP_OFFLOAD_PARAMS_OPT2_ECN_EN_SHIFT	3
#define TCP_OFFLOAD_PARAMS_OPT2_RESERVED0_MASK	0xFFF
#define TCP_OFFLOAD_PARAMS_OPT2_RESERVED0_SHIFT	4
	u8 ip_version;
	u8 reserved1[3];
	__le32 remote_ip[4];
	__le32 local_ip[4];
	__le32 flow_label;
	u8 ttl;
	u8 tos_or_tc;
	__le16 remote_port;
	__le16 local_port;
	__le16 mss;
	u8 rcv_wnd_scale;
	u8 connect_mode;
	__le16 syn_ip_payload_length;
	__le32 syn_phy_addr_lo;
	__le32 syn_phy_addr_hi;
	__le32 cwnd;
	u8 ka_max_probe_cnt;
	u8 reserved2[3];
	__le32 ka_timeout;
	__le32 ka_interval;
	__le32 max_rt_time;
	__le32 reserved3[16];
};

/* tcp IPv4/IPv6 enum */
enum tcp_seg_placement_event {
	TCP_EVENT_ADD_PEN,
	TCP_EVENT_ADD_NEW_ISLE,
	TCP_EVENT_ADD_ISLE_RIGHT,
	TCP_EVENT_ADD_ISLE_LEFT,
	TCP_EVENT_JOIN,
	TCP_EVENT_DELETE_ISLES,
	TCP_EVENT_NOP,
	MAX_TCP_SEG_PLACEMENT_EVENT
};

/* tcp init parameters */
struct tcp_update_params {
	__le16 flags;
#define TCP_UPDATE_PARAMS_REMOTE_MAC_ADDR_CHANGED_MASK		0x1
#define TCP_UPDATE_PARAMS_REMOTE_MAC_ADDR_CHANGED_SHIFT		0
#define TCP_UPDATE_PARAMS_MSS_CHANGED_MASK			0x1
#define TCP_UPDATE_PARAMS_MSS_CHANGED_SHIFT			1
#define TCP_UPDATE_PARAMS_TTL_CHANGED_MASK			0x1
#define TCP_UPDATE_PARAMS_TTL_CHANGED_SHIFT			2
#define TCP_UPDATE_PARAMS_TOS_OR_TC_CHANGED_MASK		0x1
#define TCP_UPDATE_PARAMS_TOS_OR_TC_CHANGED_SHIFT		3
#define TCP_UPDATE_PARAMS_KA_TIMEOUT_CHANGED_MASK		0x1
#define TCP_UPDATE_PARAMS_KA_TIMEOUT_CHANGED_SHIFT		4
#define TCP_UPDATE_PARAMS_KA_INTERVAL_CHANGED_MASK		0x1
#define TCP_UPDATE_PARAMS_KA_INTERVAL_CHANGED_SHIFT		5
#define TCP_UPDATE_PARAMS_MAX_RT_TIME_CHANGED_MASK		0x1
#define TCP_UPDATE_PARAMS_MAX_RT_TIME_CHANGED_SHIFT		6
#define TCP_UPDATE_PARAMS_FLOW_LABEL_CHANGED_MASK		0x1
#define TCP_UPDATE_PARAMS_FLOW_LABEL_CHANGED_SHIFT		7
#define TCP_UPDATE_PARAMS_INITIAL_RCV_WND_CHANGED_MASK		0x1
#define TCP_UPDATE_PARAMS_INITIAL_RCV_WND_CHANGED_SHIFT		8
#define TCP_UPDATE_PARAMS_KA_MAX_PROBE_CNT_CHANGED_MASK		0x1
#define TCP_UPDATE_PARAMS_KA_MAX_PROBE_CNT_CHANGED_SHIFT	9
#define TCP_UPDATE_PARAMS_KA_EN_CHANGED_MASK			0x1
#define TCP_UPDATE_PARAMS_KA_EN_CHANGED_SHIFT			10
#define TCP_UPDATE_PARAMS_NAGLE_EN_CHANGED_MASK			0x1
#define TCP_UPDATE_PARAMS_NAGLE_EN_CHANGED_SHIFT		11
#define TCP_UPDATE_PARAMS_KA_EN_MASK				0x1
#define TCP_UPDATE_PARAMS_KA_EN_SHIFT				12
#define TCP_UPDATE_PARAMS_NAGLE_EN_MASK				0x1
#define TCP_UPDATE_PARAMS_NAGLE_EN_SHIFT			13
#define TCP_UPDATE_PARAMS_KA_RESTART_MASK			0x1
#define TCP_UPDATE_PARAMS_KA_RESTART_SHIFT			14
#define TCP_UPDATE_PARAMS_RETRANSMIT_RESTART_MASK		0x1
#define TCP_UPDATE_PARAMS_RETRANSMIT_RESTART_SHIFT		15
	__le16 remote_mac_addr_lo;
	__le16 remote_mac_addr_mid;
	__le16 remote_mac_addr_hi;
	__le16 mss;
	u8 ttl;
	u8 tos_or_tc;
	__le32 ka_timeout;
	__le32 ka_interval;
	__le32 max_rt_time;
	__le32 flow_label;
	__le32 initial_rcv_wnd;
	u8 ka_max_probe_cnt;
	u8 reserved1[7];
};

/* toe upload parameters */
struct tcp_upload_params {
	__le32 rcv_next;
	__le32 snd_una;
	__le32 snd_next;
	__le32 snd_max;
	__le32 snd_wnd;
	__le32 rcv_wnd;
	__le32 snd_wl1;
	__le32 cwnd;
	__le32 ss_thresh;
	__le16 srtt;
	__le16 rtt_var;
	__le32 ts_time;
	__le32 ts_recent;
	__le32 ts_recent_age;
	__le32 total_rt;
	__le32 ka_timeout_delta;
	__le32 rt_timeout_delta;
	u8 dup_ack_cnt;
	u8 snd_wnd_probe_cnt;
	u8 ka_probe_cnt;
	u8 rt_cnt;
	__le32 reserved;
};

#endif /* __TCP_COMMON__ */
