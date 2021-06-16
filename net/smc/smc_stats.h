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

extern struct smc_stats __percpu *smc_stats;	/* per cpu counters for SMC */
extern struct smc_stats_reason fback_rsn;
extern struct mutex smc_stat_fback_rsn;

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

struct smc_stats_reason {
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
	u64			sendpage_cnt;
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

#define SMC_STAT_PAYLOAD_SUB(_tech, key, _len, _rc) \
do { \
	typeof(_tech) t = (_tech); \
	typeof(_len) l = (_len); \
	int _pos = fls64((l) >> 13); \
	typeof(_rc) r = (_rc); \
	int m = SMC_BUF_MAX - 1; \
	this_cpu_inc((*smc_stats).smc[t].key ## _cnt); \
	if (r <= 0) \
		break; \
	_pos = (_pos < m) ? ((l == 1 << (_pos + 12)) ? _pos - 1 : _pos) : m; \
	this_cpu_inc((*smc_stats).smc[t].key ## _pd.buf[_pos]); \
	this_cpu_add((*smc_stats).smc[t].key ## _bytes, r); \
} \
while (0)

#define SMC_STAT_TX_PAYLOAD(_smc, length, rcode) \
do { \
	typeof(_smc) __smc = _smc; \
	typeof(length) _len = (length); \
	typeof(rcode) _rc = (rcode); \
	bool is_smcd = !__smc->conn.lnk; \
	if (is_smcd) \
		SMC_STAT_PAYLOAD_SUB(SMC_TYPE_D, tx, _len, _rc); \
	else \
		SMC_STAT_PAYLOAD_SUB(SMC_TYPE_R, tx, _len, _rc); \
} \
while (0)

#define SMC_STAT_RX_PAYLOAD(_smc, length, rcode) \
do { \
	typeof(_smc) __smc = _smc; \
	typeof(length) _len = (length); \
	typeof(rcode) _rc = (rcode); \
	bool is_smcd = !__smc->conn.lnk; \
	if (is_smcd) \
		SMC_STAT_PAYLOAD_SUB(SMC_TYPE_D, rx, _len, _rc); \
	else \
		SMC_STAT_PAYLOAD_SUB(SMC_TYPE_R, rx, _len, _rc); \
} \
while (0)

#define SMC_STAT_RMB_SIZE_SUB(_tech, k, _len) \
do { \
	typeof(_len) _l = (_len); \
	typeof(_tech) t = (_tech); \
	int _pos = fls((_l) >> 13); \
	int m = SMC_BUF_MAX - 1; \
	_pos = (_pos < m) ? ((_l == 1 << (_pos + 12)) ? _pos - 1 : _pos) : m; \
	this_cpu_inc((*smc_stats).smc[t].k ## _rmbsize.buf[_pos]); \
} \
while (0)

#define SMC_STAT_RMB_SUB(type, t, key) \
	this_cpu_inc((*smc_stats).smc[t].rmb ## _ ## key.type ## _cnt)

#define SMC_STAT_RMB_SIZE(_is_smcd, _is_rx, _len) \
do { \
	typeof(_is_smcd) is_d = (_is_smcd); \
	typeof(_is_rx) is_r = (_is_rx); \
	typeof(_len) l = (_len); \
	if ((is_d) && (is_r)) \
		SMC_STAT_RMB_SIZE_SUB(SMC_TYPE_D, rx, l); \
	if ((is_d) && !(is_r)) \
		SMC_STAT_RMB_SIZE_SUB(SMC_TYPE_D, tx, l); \
	if (!(is_d) && (is_r)) \
		SMC_STAT_RMB_SIZE_SUB(SMC_TYPE_R, rx, l); \
	if (!(is_d) && !(is_r)) \
		SMC_STAT_RMB_SIZE_SUB(SMC_TYPE_R, tx, l); \
} \
while (0)

#define SMC_STAT_RMB(type, _is_smcd, _is_rx) \
do { \
	typeof(_is_smcd) is_d = (_is_smcd); \
	typeof(_is_rx) is_r = (_is_rx); \
	if ((is_d) && (is_r)) \
		SMC_STAT_RMB_SUB(type, SMC_TYPE_D, rx); \
	if ((is_d) && !(is_r)) \
		SMC_STAT_RMB_SUB(type, SMC_TYPE_D, tx); \
	if (!(is_d) && (is_r)) \
		SMC_STAT_RMB_SUB(type, SMC_TYPE_R, rx); \
	if (!(is_d) && !(is_r)) \
		SMC_STAT_RMB_SUB(type, SMC_TYPE_R, tx); \
} \
while (0)

#define SMC_STAT_BUF_REUSE(is_smcd, is_rx) \
	SMC_STAT_RMB(reuse, is_smcd, is_rx)

#define SMC_STAT_RMB_ALLOC(is_smcd, is_rx) \
	SMC_STAT_RMB(alloc, is_smcd, is_rx)

#define SMC_STAT_RMB_DOWNGRADED(is_smcd, is_rx) \
	SMC_STAT_RMB(dgrade, is_smcd, is_rx)

#define SMC_STAT_RMB_TX_PEER_FULL(is_smcd) \
	SMC_STAT_RMB(buf_full_peer, is_smcd, false)

#define SMC_STAT_RMB_TX_FULL(is_smcd) \
	SMC_STAT_RMB(buf_full, is_smcd, false)

#define SMC_STAT_RMB_TX_PEER_SIZE_SMALL(is_smcd) \
	SMC_STAT_RMB(buf_size_small_peer, is_smcd, false)

#define SMC_STAT_RMB_TX_SIZE_SMALL(is_smcd) \
	SMC_STAT_RMB(buf_size_small, is_smcd, false)

#define SMC_STAT_RMB_RX_SIZE_SMALL(is_smcd) \
	SMC_STAT_RMB(buf_size_small, is_smcd, true)

#define SMC_STAT_RMB_RX_FULL(is_smcd) \
	SMC_STAT_RMB(buf_full, is_smcd, true)

#define SMC_STAT_INC(is_smcd, type) \
do { \
	if ((is_smcd)) \
		this_cpu_inc(smc_stats->smc[SMC_TYPE_D].type); \
	else \
		this_cpu_inc(smc_stats->smc[SMC_TYPE_R].type); \
} \
while (0)

#define SMC_STAT_CLNT_SUCC_INC(_aclc) \
do { \
	typeof(_aclc) acl = (_aclc); \
	bool is_v2 = (acl->hdr.version == SMC_V2); \
	bool is_smcd = (acl->hdr.typev1 == SMC_TYPE_D); \
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

#define SMC_STAT_SERV_SUCC_INC(_ini) \
do { \
	typeof(_ini) i = (_ini); \
	bool is_v2 = (i->smcd_version & SMC_V2); \
	bool is_smcd = (i->is_smcd); \
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

int smc_stats_init(void) __init;
void smc_stats_exit(void);

#endif /* NET_SMC_SMC_STATS_H_ */
