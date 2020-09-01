/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#ifndef _QED_ETH_IF_H
#define _QED_ETH_IF_H

#include <linux/list.h>
#include <linux/if_link.h>
#include <linux/qed/eth_common.h>
#include <linux/qed/qed_if.h>
#include <linux/qed/qed_iov_if.h>

/* 64 max queues * (1 rx + 4 tx-cos + 1 xdp) */
#define QED_MIN_L2_CONS (2 + NUM_PHYS_TCS_4PORT_K2)
#define QED_MAX_L2_CONS (64 * (QED_MIN_L2_CONS))

struct qed_queue_start_common_params {
	/* Should always be relative to entity sending this. */
	u8 vport_id;
	u16 queue_id;

	/* Relative, but relevant only for PFs */
	u8 stats_id;

	struct qed_sb_info *p_sb;
	u8 sb_idx;

	u8 tc;
};

struct qed_rxq_start_ret_params {
	void __iomem *p_prod;
	void *p_handle;
};

struct qed_txq_start_ret_params {
	void __iomem *p_doorbell;
	void *p_handle;
};

enum qed_filter_config_mode {
	QED_FILTER_CONFIG_MODE_DISABLE,
	QED_FILTER_CONFIG_MODE_5_TUPLE,
	QED_FILTER_CONFIG_MODE_L4_PORT,
	QED_FILTER_CONFIG_MODE_IP_DEST,
	QED_FILTER_CONFIG_MODE_IP_SRC,
};

struct qed_ntuple_filter_params {
	/* Physically mapped address containing header of buffer to be used
	 * as filter.
	 */
	dma_addr_t addr;

	/* Length of header in bytes */
	u16 length;

	/* Relative queue-id to receive classified packet */
#define QED_RFS_NTUPLE_QID_RSS ((u16)-1)
	u16 qid;

	/* Identifier can either be according to vport-id or vfid */
	bool b_is_vf;
	u8 vport_id;
	u8 vf_id;

	/* true iff this filter is to be added. Else to be removed */
	bool b_is_add;

	/* If flow needs to be dropped */
	bool b_is_drop;
};

struct qed_dev_eth_info {
	struct qed_dev_info common;

	u8	num_queues;
	u8	num_tc;

	u8	port_mac[ETH_ALEN];
	u16	num_vlan_filters;
	u16	num_mac_filters;

	/* Legacy VF - this affects the datapath, so qede has to know */
	bool is_legacy;

	/* Might depend on available resources [in case of VF] */
	bool xdp_supported;
};

struct qed_update_vport_rss_params {
	void	*rss_ind_table[128];
	u32	rss_key[10];
	u8	rss_caps;
};

struct qed_update_vport_params {
	u8 vport_id;
	u8 update_vport_active_flg;
	u8 vport_active_flg;
	u8 update_tx_switching_flg;
	u8 tx_switching_flg;
	u8 update_accept_any_vlan_flg;
	u8 accept_any_vlan;
	u8 update_rss_flg;
	struct qed_update_vport_rss_params rss_params;
};

struct qed_start_vport_params {
	bool remove_inner_vlan;
	bool handle_ptp_pkts;
	bool gro_enable;
	bool drop_ttl0;
	u8 vport_id;
	u16 mtu;
	bool clear_stats;
};

enum qed_filter_rx_mode_type {
	QED_FILTER_RX_MODE_TYPE_REGULAR,
	QED_FILTER_RX_MODE_TYPE_MULTI_PROMISC,
	QED_FILTER_RX_MODE_TYPE_PROMISC,
};

enum qed_filter_xcast_params_type {
	QED_FILTER_XCAST_TYPE_ADD,
	QED_FILTER_XCAST_TYPE_DEL,
	QED_FILTER_XCAST_TYPE_REPLACE,
};

struct qed_filter_ucast_params {
	enum qed_filter_xcast_params_type type;
	u8 vlan_valid;
	u16 vlan;
	u8 mac_valid;
	unsigned char mac[ETH_ALEN];
};

struct qed_filter_mcast_params {
	enum qed_filter_xcast_params_type type;
	u8 num;
	unsigned char mac[64][ETH_ALEN];
};

union qed_filter_type_params {
	enum qed_filter_rx_mode_type accept_flags;
	struct qed_filter_ucast_params ucast;
	struct qed_filter_mcast_params mcast;
};

enum qed_filter_type {
	QED_FILTER_TYPE_UCAST,
	QED_FILTER_TYPE_MCAST,
	QED_FILTER_TYPE_RX_MODE,
	QED_MAX_FILTER_TYPES,
};

struct qed_filter_params {
	enum qed_filter_type type;
	union qed_filter_type_params filter;
};

struct qed_tunn_params {
	u16 vxlan_port;
	u8 update_vxlan_port;
	u16 geneve_port;
	u8 update_geneve_port;
};

struct qed_eth_cb_ops {
	struct qed_common_cb_ops common;
	void (*force_mac) (void *dev, u8 *mac, bool forced);
	void (*ports_update)(void *dev, u16 vxlan_port, u16 geneve_port);
};

#define QED_MAX_PHC_DRIFT_PPB   291666666

enum qed_ptp_filter_type {
	QED_PTP_FILTER_NONE,
	QED_PTP_FILTER_ALL,
	QED_PTP_FILTER_V1_L4_EVENT,
	QED_PTP_FILTER_V1_L4_GEN,
	QED_PTP_FILTER_V2_L4_EVENT,
	QED_PTP_FILTER_V2_L4_GEN,
	QED_PTP_FILTER_V2_L2_EVENT,
	QED_PTP_FILTER_V2_L2_GEN,
	QED_PTP_FILTER_V2_EVENT,
	QED_PTP_FILTER_V2_GEN
};

enum qed_ptp_hwtstamp_tx_type {
	QED_PTP_HWTSTAMP_TX_OFF,
	QED_PTP_HWTSTAMP_TX_ON,
};

#ifdef CONFIG_DCB
/* Prototype declaration of qed_eth_dcbnl_ops should match with the declaration
 * of dcbnl_rtnl_ops structure.
 */
struct qed_eth_dcbnl_ops {
	/* IEEE 802.1Qaz std */
	int (*ieee_getpfc)(struct qed_dev *cdev, struct ieee_pfc *pfc);
	int (*ieee_setpfc)(struct qed_dev *cdev, struct ieee_pfc *pfc);
	int (*ieee_getets)(struct qed_dev *cdev, struct ieee_ets *ets);
	int (*ieee_setets)(struct qed_dev *cdev, struct ieee_ets *ets);
	int (*ieee_peer_getets)(struct qed_dev *cdev, struct ieee_ets *ets);
	int (*ieee_peer_getpfc)(struct qed_dev *cdev, struct ieee_pfc *pfc);
	int (*ieee_getapp)(struct qed_dev *cdev, struct dcb_app *app);
	int (*ieee_setapp)(struct qed_dev *cdev, struct dcb_app *app);

	/* CEE std */
	u8 (*getstate)(struct qed_dev *cdev);
	u8 (*setstate)(struct qed_dev *cdev, u8 state);
	void (*getpgtccfgtx)(struct qed_dev *cdev, int prio, u8 *prio_type,
			     u8 *pgid, u8 *bw_pct, u8 *up_map);
	void (*getpgbwgcfgtx)(struct qed_dev *cdev, int pgid, u8 *bw_pct);
	void (*getpgtccfgrx)(struct qed_dev *cdev, int prio, u8 *prio_type,
			     u8 *pgid, u8 *bw_pct, u8 *up_map);
	void (*getpgbwgcfgrx)(struct qed_dev *cdev, int pgid, u8 *bw_pct);
	void (*getpfccfg)(struct qed_dev *cdev, int prio, u8 *setting);
	void (*setpfccfg)(struct qed_dev *cdev, int prio, u8 setting);
	u8 (*getcap)(struct qed_dev *cdev, int capid, u8 *cap);
	int (*getnumtcs)(struct qed_dev *cdev, int tcid, u8 *num);
	u8 (*getpfcstate)(struct qed_dev *cdev);
	int (*getapp)(struct qed_dev *cdev, u8 idtype, u16 id);
	u8 (*getfeatcfg)(struct qed_dev *cdev, int featid, u8 *flags);

	/* DCBX configuration */
	u8 (*getdcbx)(struct qed_dev *cdev);
	void (*setpgtccfgtx)(struct qed_dev *cdev, int prio,
			     u8 pri_type, u8 pgid, u8 bw_pct, u8 up_map);
	void (*setpgtccfgrx)(struct qed_dev *cdev, int prio,
			     u8 pri_type, u8 pgid, u8 bw_pct, u8 up_map);
	void (*setpgbwgcfgtx)(struct qed_dev *cdev, int pgid, u8 bw_pct);
	void (*setpgbwgcfgrx)(struct qed_dev *cdev, int pgid, u8 bw_pct);
	u8 (*setall)(struct qed_dev *cdev);
	int (*setnumtcs)(struct qed_dev *cdev, int tcid, u8 num);
	void (*setpfcstate)(struct qed_dev *cdev, u8 state);
	int (*setapp)(struct qed_dev *cdev, u8 idtype, u16 idval, u8 up);
	u8 (*setdcbx)(struct qed_dev *cdev, u8 state);
	u8 (*setfeatcfg)(struct qed_dev *cdev, int featid, u8 flags);

	/* Peer apps */
	int (*peer_getappinfo)(struct qed_dev *cdev,
			       struct dcb_peer_app_info *info,
			       u16 *app_count);
	int (*peer_getapptable)(struct qed_dev *cdev, struct dcb_app *table);

	/* CEE peer */
	int (*cee_peer_getpfc)(struct qed_dev *cdev, struct cee_pfc *pfc);
	int (*cee_peer_getpg)(struct qed_dev *cdev, struct cee_pg *pg);
};
#endif

struct qed_eth_ptp_ops {
	int (*cfg_filters)(struct qed_dev *, enum qed_ptp_filter_type,
			   enum qed_ptp_hwtstamp_tx_type);
	int (*read_rx_ts)(struct qed_dev *, u64 *);
	int (*read_tx_ts)(struct qed_dev *, u64 *);
	int (*read_cc)(struct qed_dev *, u64 *);
	int (*disable)(struct qed_dev *);
	int (*adjfreq)(struct qed_dev *, s32);
	int (*enable)(struct qed_dev *);
};

struct qed_eth_ops {
	const struct qed_common_ops *common;
#ifdef CONFIG_QED_SRIOV
	const struct qed_iov_hv_ops *iov;
#endif
#ifdef CONFIG_DCB
	const struct qed_eth_dcbnl_ops *dcb;
#endif
	const struct qed_eth_ptp_ops *ptp;

	int (*fill_dev_info)(struct qed_dev *cdev,
			     struct qed_dev_eth_info *info);

	void (*register_ops)(struct qed_dev *cdev,
			     struct qed_eth_cb_ops *ops,
			     void *cookie);

	 bool(*check_mac) (struct qed_dev *cdev, u8 *mac);

	int (*vport_start)(struct qed_dev *cdev,
			   struct qed_start_vport_params *params);

	int (*vport_stop)(struct qed_dev *cdev,
			  u8 vport_id);

	int (*vport_update)(struct qed_dev *cdev,
			    struct qed_update_vport_params *params);

	int (*q_rx_start)(struct qed_dev *cdev,
			  u8 rss_num,
			  struct qed_queue_start_common_params *params,
			  u16 bd_max_bytes,
			  dma_addr_t bd_chain_phys_addr,
			  dma_addr_t cqe_pbl_addr,
			  u16 cqe_pbl_size,
			  struct qed_rxq_start_ret_params *ret_params);

	int (*q_rx_stop)(struct qed_dev *cdev, u8 rss_id, void *handle);

	int (*q_tx_start)(struct qed_dev *cdev,
			  u8 rss_num,
			  struct qed_queue_start_common_params *params,
			  dma_addr_t pbl_addr,
			  u16 pbl_size,
			  struct qed_txq_start_ret_params *ret_params);

	int (*q_tx_stop)(struct qed_dev *cdev, u8 rss_id, void *handle);

	int (*filter_config)(struct qed_dev *cdev,
			     struct qed_filter_params *params);

	int (*fastpath_stop)(struct qed_dev *cdev);

	int (*eth_cqe_completion)(struct qed_dev *cdev,
				  u8 rss_id,
				  struct eth_slow_path_rx_cqe *cqe);

	void (*get_vport_stats)(struct qed_dev *cdev,
				struct qed_eth_stats *stats);

	int (*tunn_config)(struct qed_dev *cdev,
			   struct qed_tunn_params *params);

	int (*ntuple_filter_config)(struct qed_dev *cdev,
				    void *cookie,
				    struct qed_ntuple_filter_params *params);

	int (*configure_arfs_searcher)(struct qed_dev *cdev,
				       enum qed_filter_config_mode mode);
	int (*get_coalesce)(struct qed_dev *cdev, u16 *coal, void *handle);
	int (*req_bulletin_update_mac)(struct qed_dev *cdev, u8 *mac);
};

const struct qed_eth_ops *qed_get_eth_ops(void);
void qed_put_eth_ops(void);

#endif
