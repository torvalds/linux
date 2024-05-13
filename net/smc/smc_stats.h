/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 * Macros for SMC statistics
 *
 * Copyright IBM Corp. 2021
 *
 * Author(s):  Guvenc Gulce
 */

#ifndef NET_SMC_SMC_STATS_H_
#define NET_SMC_SMC_STATS_H_
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/ctype.h>
#include <linux/smc.h>

#include "smc_clc.h"

#define SMC_MAX_FBACK_RSN_CNT 30

enum {
	SMC_BUF_8K,
	SMC_BUF_16K,
	SMC_BUF_32K,
	SMC_BUF_64K,
	SMC_BUF_128K,
	SMC_BUF_256K,
	SMC_BUF_512K,
	SMC_BUF_1024K,
	SMC_BUF_G_1024K,
	SMC_BUF_MAX,
};

struct smc_stats_fback {
	int	fback_code;
	u16	count;
};

struct smc_stats_rsn {
	struct	smc_stats_fback srv[SMC_MAX_FBACK_RSN_CNT];
	struct	smc_stats_fback clnt[SMC_MAX_FBACK_RSN_CNT];
	u64			srv_fback_cnt;
	u64			clnt_fback_cnt;
};

struct smc_stats_rmbcnt {
	u64	buf_size_small_peer_cnt;
	u64	buf_size_small_cnt;
	u64	buf_full_peer_cnt;
	u64	buf_full_cnt;
	u64	reuse_cnt;
	u64	alloc_cnt;
	u64	dgrade_cnt;
};

struct smc_stats_memsize {
	u64	buf[SMC_BUF_MAX];
};

struct smc_stats_tech {
	struct smc_stats_memsize tx_rmbsize;
	struct smc_stats_memsize rx_rmbsize;
	struct smc_stats_memsize tx_pd;
	struct smc_stats_memsize rx_pd;
	struct smc_stats_rmbcnt rmb_tx;
	struct smc_stats_rmbcnt rmb_rx;
	u64			clnt_v1_succ_cnt;
	u64			clnt_v2_succ_cnt;
	u64			srv_v1_succ_cnt;
	u64			srv_v2_succ_cnt;
	u64			urg_data_cnt;
	u64			splice_cnt;
	u64			cork_cnt;
	u64			ndly_cnt;
	u64			rx_bytes;
	u64			tx_bytes;
	u64			rx_cnt;
	u64			tx_cnt;
};

struct smc_stats {
	struct smc_stats_tech	smc[2];
	u64			clnt_hshake_err_cnt;
	u64			srv_hshake_err_cnt;
};

#define SMC_STAT_PAYLOAD_SUB(_smc_stats, _tech, key, _len, _rc) \
do { \
	typeof(_smc_stats) stats = (_smc_stats); \
	typeof(_tech) t = (_tech); \
	typeof(_len) l = (_len); \
	int _pos; \
	typeof(_rc) r = (_rc); \
	int m = SMC_BUF_MAX - 1; \
	this_cpu_inc((*stats).smc[t].key ## _cnt); \
	if (r <= 0 || l <= 0) \
		break; \
	_pos = fls64((l - 1) >> 13); \
	_pos = (_pos <= m) ? _pos : m; \
	this_cpu_inc((*stats).smc[t].key ## _pd.buf[_pos]); \
	this_cpu_add((*stats).smc[t].key ## _bytes, r); \
} \
while (0)

#define SMC_STAT_TX_PAYLOAD(_smc, length, rcode) \
do { \
	typeof(_smc) __smc = _smc; \
	struct net *_net = sock_net(&__smc->sk); \
	struct smc_stats __percpu *_smc_stats = _net->smc.smc_stats; \
	typeof(length) _len = (length); \
	typeof(rcode) _rc = (rcode); \
	bool is_smcd = !__smc->conn.lnk; \
	if (is_smcd) \
		SMC_STAT_PAYLOAD_SUB(_smc_stats, SMC_TYPE_D, tx, _len, _rc); \
	else \
		SMC_STAT_PAYLOAD_SUB(_smc_stats, SMC_TYPE_R, tx, _len, _rc); \
} \
while (0)

#define SMC_STAT_RX_PAYLOAD(_smc, length, rcode) \
do { \
	typeof(_smc) __smc = _smc; \
	struct net *_net = sock_net(&__smc->sk); \
	struct smc_stats __percpu *_smc_stats = _net->smc.smc_stats; \
	typeof(length) _len = (length); \
	typeof(rcode) _rc = (rcode); \
	bool is_smcd = !__smc->conn.lnk; \
	if (is_smcd) \
		SMC_STAT_PAYLOAD_SUB(_smc_stats, SMC_TYPE_D, rx, _len, _rc); \
	else \
		SMC_STAT_PAYLOAD_SUB(_smc_stats, SMC_TYPE_R, rx, _len, _rc); \
} \
while (0)

#define SMC_STAT_RMB_SIZE_SUB(_smc_stats, _tech, k, _len) \
do { \
	typeof(_len) _l = (_len); \
	typeof(_tech) t = (_tech); \
	int _pos; \
	int m = SMC_BUF_MAX - 1; \
	if (_l <= 0) \
		break; \
	_pos = fls((_l - 1) >> 13); \
	_pos = (_pos <= m) ? _pos : m; \
	this_cpu_inc((*(_smc_stats)).smc[t].k ## _rmbsize.buf[_pos]); \
} \
while (0)

#define SMC_STAT_RMB_SUB(_smc_stats, type, t, key) \
	this_cpu_inc((*(_smc_stats)).smc[t].rmb ## _ ## key.type ## _cnt)

#define SMC_STAT_RMB_SIZE(_smc, _is_smcd, _is_rx, _len) \
do { \
	struct net *_net = sock_net(&(_smc)->sk); \
	struct smc_stats __percpu *_smc_stats = _net->smc.smc_stats; \
	typeof(_is_smcd) is_d = (_is_smcd); \
	typeof(_is_rx) is_r = (_is_rx); \
	typeof(_len) l = (_len); \
	if ((is_d) && (is_r)) \
		SMC_STAT_RMB_SIZE_SUB(_smc_stats, SMC_TYPE_D, rx, l); \
	if ((is_d) && !(is_r)) \
		SMC_STAT_RMB_SIZE_SUB(_smc_stats, SMC_TYPE_D, tx, l); \
	if (!(is_d) && (is_r)) \
		SMC_STAT_RMB_SIZE_SUB(_smc_stats, SMC_TYPE_R, rx, l); \
	if (!(is_d) && !(is_r)) \
		SMC_STAT_RMB_SIZE_SUB(_smc_stats, SMC_TYPE_R, tx, l); \
} \
while (0)

#define SMC_STAT_RMB(_smc, type, _is_smcd, _is_rx) \
do { \
	struct net *net = sock_net(&(_smc)->sk); \
	struct smc_stats __percpu *_smc_stats = net->smc.smc_stats; \
	typeof(_is_smcd) is_d = (_is_smcd); \
	typeof(_is_rx) is_r = (_is_rx); \
	if ((is_d) && (is_r)) \
		SMC_STAT_RMB_SUB(_smc_stats, type, SMC_TYPE_D, rx); \
	if ((is_d) && !(is_r)) \
		SMC_STAT_RMB_SUB(_smc_stats, type, SMC_TYPE_D, tx); \
	if (!(is_d) && (is_r)) \
		SMC_STAT_RMB_SUB(_smc_stats, type, SMC_TYPE_R, rx); \
	if (!(is_d) && !(is_r)) \
		SMC_STAT_RMB_SUB(_smc_stats, type, SMC_TYPE_R, tx); \
} \
while (0)

#define SMC_STAT_BUF_REUSE(smc, is_smcd, is_rx) \
	SMC_STAT_RMB(smc, reuse, is_smcd, is_rx)

#define SMC_STAT_RMB_ALLOC(smc, is_smcd, is_rx) \
	SMC_STAT_RMB(smc, alloc, is_smcd, is_rx)

#define SMC_STAT_RMB_DOWNGRADED(smc, is_smcd, is_rx) \
	SMC_STAT_RMB(smc, dgrade, is_smcd, is_rx)

#define SMC_STAT_RMB_TX_PEER_FULL(smc, is_smcd) \
	SMC_STAT_RMB(smc, buf_full_peer, is_smcd, false)

#define SMC_STAT_RMB_TX_FULL(smc, is_smcd) \
	SMC_STAT_RMB(smc, buf_full, is_smcd, false)

#define SMC_STAT_RMB_TX_PEER_SIZE_SMALL(smc, is_smcd) \
	SMC_STAT_RMB(smc, buf_size_small_peer, is_smcd, false)

#define SMC_STAT_RMB_TX_SIZE_SMALL(smc, is_smcd) \
	SMC_STAT_RMB(smc, buf_size_small, is_smcd, false)

#define SMC_STAT_RMB_RX_SIZE_SMALL(smc, is_smcd) \
	SMC_STAT_RMB(smc, buf_size_small, is_smcd, true)

#define SMC_STAT_RMB_RX_FULL(smc, is_smcd) \
	SMC_STAT_RMB(smc, buf_full, is_smcd, true)

#define SMC_STAT_INC(_smc, type) \
do { \
	typeof(_smc) __smc = _smc; \
	bool is_smcd = !(__smc)->conn.lnk; \
	struct net *net = sock_net(&(__smc)->sk); \
	struct smc_stats __percpu *smc_stats = net->smc.smc_stats; \
	if ((is_smcd)) \
		this_cpu_inc(smc_stats->smc[SMC_TYPE_D].type); \
	else \
		this_cpu_inc(smc_stats->smc[SMC_TYPE_R].type); \
} \
while (0)

#define SMC_STAT_CLNT_SUCC_INC(net, _aclc) \
do { \
	typeof(_aclc) acl = (_aclc); \
	bool is_v2 = (acl->hdr.version == SMC_V2); \
	bool is_smcd = (acl->hdr.typev1 == SMC_TYPE_D); \
	struct smc_stats __percpu *smc_stats = (net)->smc.smc_stats; \
	if (is_v2 && is_smcd) \
		this_cpu_inc(smc_stats->smc[SMC_TYPE_D].clnt_v2_succ_cnt); \
	else if (is_v2 && !is_smcd) \
		this_cpu_inc(smc_stats->smc[SMC_TYPE_R].clnt_v2_succ_cnt); \
	else if (!is_v2 && is_smcd) \
		this_cpu_inc(smc_stats->smc[SMC_TYPE_D].clnt_v1_succ_cnt); \
	else if (!is_v2 && !is_smcd) \
		this_cpu_inc(smc_stats->smc[SMC_TYPE_R].clnt_v1_succ_cnt); \
} \
while (0)

#define SMC_STAT_SERV_SUCC_INC(net, _ini) \
do { \
	typeof(_ini) i = (_ini); \
	bool is_smcd = (i->is_smcd); \
	u8 version = is_smcd ? i->smcd_version : i->smcr_version; \
	bool is_v2 = (version & SMC_V2); \
	typeof(net->smc.smc_stats) smc_stats = (net)->smc.smc_stats; \
	if (is_v2 && is_smcd) \
		this_cpu_inc(smc_stats->smc[SMC_TYPE_D].srv_v2_succ_cnt); \
	else if (is_v2 && !is_smcd) \
		this_cpu_inc(smc_stats->smc[SMC_TYPE_R].srv_v2_succ_cnt); \
	else if (!is_v2 && is_smcd) \
		this_cpu_inc(smc_stats->smc[SMC_TYPE_D].srv_v1_succ_cnt); \
	else if (!is_v2 && !is_smcd) \
		this_cpu_inc(smc_stats->smc[SMC_TYPE_R].srv_v1_succ_cnt); \
} \
while (0)

int smc_nl_get_stats(struct sk_buff *skb, struct netlink_callback *cb);
int smc_nl_get_fback_stats(struct sk_buff *skb, struct netlink_callback *cb);
int smc_stats_init(struct net *net);
void smc_stats_exit(struct net *net);

#endif /* NET_SMC_SMC_STATS_H_ */
