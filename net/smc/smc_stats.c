// SPDX-License-Identifier: GPL-2.0-only
/*
 * Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 * SMC statistics netlink routines
 *
 * Copyright IBM Corp. 2021
 *
 * Author(s):  Guvenc Gulce
 */
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/ctype.h>
#include <linux/smc.h>
#include <net/genetlink.h>
#include <net/sock.h>
#include "smc_netlink.h"
#include "smc_stats.h"

int smc_stats_init(struct net *net)
{
	net->smc.fback_rsn = kzalloc(sizeof(*net->smc.fback_rsn), GFP_KERNEL);
	if (!net->smc.fback_rsn)
		goto err_fback;
	net->smc.smc_stats = alloc_percpu(struct smc_stats);
	if (!net->smc.smc_stats)
		goto err_stats;
	mutex_init(&net->smc.mutex_fback_rsn);
	return 0;

err_stats:
	kfree(net->smc.fback_rsn);
err_fback:
	return -ENOMEM;
}

void smc_stats_exit(struct net *net)
{
	kfree(net->smc.fback_rsn);
	if (net->smc.smc_stats)
		free_percpu(net->smc.smc_stats);
}

static int smc_nl_fill_stats_rmb_data(struct sk_buff *skb,
				      struct smc_stats *stats, int tech,
				      int type)
{
	struct smc_stats_rmbcnt *stats_rmb_cnt;
	struct nlattr *attrs;

	if (type == SMC_NLA_STATS_T_TX_RMB_STATS)
		stats_rmb_cnt = &stats->smc[tech].rmb_tx;
	else
		stats_rmb_cnt = &stats->smc[tech].rmb_rx;

	attrs = nla_nest_start(skb, type);
	if (!attrs)
		goto errout;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_RMB_REUSE_CNT,
			      stats_rmb_cnt->reuse_cnt,
			      SMC_NLA_STATS_RMB_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_RMB_SIZE_SM_PEER_CNT,
			      stats_rmb_cnt->buf_size_small_peer_cnt,
			      SMC_NLA_STATS_RMB_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_RMB_SIZE_SM_CNT,
			      stats_rmb_cnt->buf_size_small_cnt,
			      SMC_NLA_STATS_RMB_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_RMB_FULL_PEER_CNT,
			      stats_rmb_cnt->buf_full_peer_cnt,
			      SMC_NLA_STATS_RMB_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_RMB_FULL_CNT,
			      stats_rmb_cnt->buf_full_cnt,
			      SMC_NLA_STATS_RMB_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_RMB_ALLOC_CNT,
			      stats_rmb_cnt->alloc_cnt,
			      SMC_NLA_STATS_RMB_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_RMB_DGRADE_CNT,
			      stats_rmb_cnt->dgrade_cnt,
			      SMC_NLA_STATS_RMB_PAD))
		goto errattr;

	nla_nest_end(skb, attrs);
	return 0;

errattr:
	nla_nest_cancel(skb, attrs);
errout:
	return -EMSGSIZE;
}

static int smc_nl_fill_stats_bufsize_data(struct sk_buff *skb,
					  struct smc_stats *stats, int tech,
					  int type)
{
	struct smc_stats_memsize *stats_pload;
	struct nlattr *attrs;

	if (type == SMC_NLA_STATS_T_TXPLOAD_SIZE)
		stats_pload = &stats->smc[tech].tx_pd;
	else if (type == SMC_NLA_STATS_T_RXPLOAD_SIZE)
		stats_pload = &stats->smc[tech].rx_pd;
	else if (type == SMC_NLA_STATS_T_TX_RMB_SIZE)
		stats_pload = &stats->smc[tech].tx_rmbsize;
	else if (type == SMC_NLA_STATS_T_RX_RMB_SIZE)
		stats_pload = &stats->smc[tech].rx_rmbsize;
	else
		goto errout;

	attrs = nla_nest_start(skb, type);
	if (!attrs)
		goto errout;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_PLOAD_8K,
			      stats_pload->buf[SMC_BUF_8K],
			      SMC_NLA_STATS_PLOAD_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_PLOAD_16K,
			      stats_pload->buf[SMC_BUF_16K],
			      SMC_NLA_STATS_PLOAD_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_PLOAD_32K,
			      stats_pload->buf[SMC_BUF_32K],
			      SMC_NLA_STATS_PLOAD_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_PLOAD_64K,
			      stats_pload->buf[SMC_BUF_64K],
			      SMC_NLA_STATS_PLOAD_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_PLOAD_128K,
			      stats_pload->buf[SMC_BUF_128K],
			      SMC_NLA_STATS_PLOAD_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_PLOAD_256K,
			      stats_pload->buf[SMC_BUF_256K],
			      SMC_NLA_STATS_PLOAD_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_PLOAD_512K,
			      stats_pload->buf[SMC_BUF_512K],
			      SMC_NLA_STATS_PLOAD_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_PLOAD_1024K,
			      stats_pload->buf[SMC_BUF_1024K],
			      SMC_NLA_STATS_PLOAD_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_PLOAD_G_1024K,
			      stats_pload->buf[SMC_BUF_G_1024K],
			      SMC_NLA_STATS_PLOAD_PAD))
		goto errattr;

	nla_nest_end(skb, attrs);
	return 0;

errattr:
	nla_nest_cancel(skb, attrs);
errout:
	return -EMSGSIZE;
}

static int smc_nl_fill_stats_tech_data(struct sk_buff *skb,
				       struct smc_stats *stats, int tech)
{
	struct smc_stats_tech *smc_tech;
	struct nlattr *attrs;

	smc_tech = &stats->smc[tech];
	if (tech == SMC_TYPE_D)
		attrs = nla_nest_start(skb, SMC_NLA_STATS_SMCD_TECH);
	else
		attrs = nla_nest_start(skb, SMC_NLA_STATS_SMCR_TECH);

	if (!attrs)
		goto errout;
	if (smc_nl_fill_stats_rmb_data(skb, stats, tech,
				       SMC_NLA_STATS_T_TX_RMB_STATS))
		goto errattr;
	if (smc_nl_fill_stats_rmb_data(skb, stats, tech,
				       SMC_NLA_STATS_T_RX_RMB_STATS))
		goto errattr;
	if (smc_nl_fill_stats_bufsize_data(skb, stats, tech,
					   SMC_NLA_STATS_T_TXPLOAD_SIZE))
		goto errattr;
	if (smc_nl_fill_stats_bufsize_data(skb, stats, tech,
					   SMC_NLA_STATS_T_RXPLOAD_SIZE))
		goto errattr;
	if (smc_nl_fill_stats_bufsize_data(skb, stats, tech,
					   SMC_NLA_STATS_T_TX_RMB_SIZE))
		goto errattr;
	if (smc_nl_fill_stats_bufsize_data(skb, stats, tech,
					   SMC_NLA_STATS_T_RX_RMB_SIZE))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_T_CLNT_V1_SUCC,
			      smc_tech->clnt_v1_succ_cnt,
			      SMC_NLA_STATS_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_T_CLNT_V2_SUCC,
			      smc_tech->clnt_v2_succ_cnt,
			      SMC_NLA_STATS_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_T_SRV_V1_SUCC,
			      smc_tech->srv_v1_succ_cnt,
			      SMC_NLA_STATS_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_T_SRV_V2_SUCC,
			      smc_tech->srv_v2_succ_cnt,
			      SMC_NLA_STATS_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_T_RX_BYTES,
			      smc_tech->rx_bytes,
			      SMC_NLA_STATS_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_T_TX_BYTES,
			      smc_tech->tx_bytes,
			      SMC_NLA_STATS_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_T_RX_CNT,
			      smc_tech->rx_cnt,
			      SMC_NLA_STATS_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_T_TX_CNT,
			      smc_tech->tx_cnt,
			      SMC_NLA_STATS_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_T_SENDPAGE_CNT,
			      0,
			      SMC_NLA_STATS_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_T_CORK_CNT,
			      smc_tech->cork_cnt,
			      SMC_NLA_STATS_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_T_NDLY_CNT,
			      smc_tech->ndly_cnt,
			      SMC_NLA_STATS_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_T_SPLICE_CNT,
			      smc_tech->splice_cnt,
			      SMC_NLA_STATS_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_T_URG_DATA_CNT,
			      smc_tech->urg_data_cnt,
			      SMC_NLA_STATS_PAD))
		goto errattr;

	nla_nest_end(skb, attrs);
	return 0;

errattr:
	nla_nest_cancel(skb, attrs);
errout:
	return -EMSGSIZE;
}

int smc_nl_get_stats(struct sk_buff *skb,
		     struct netlink_callback *cb)
{
	struct smc_nl_dmp_ctx *cb_ctx = smc_nl_dmp_ctx(cb);
	struct net *net = sock_net(skb->sk);
	struct smc_stats *stats;
	struct nlattr *attrs;
	int cpu, i, size;
	void *nlh;
	u64 *src;
	u64 *sum;

	if (cb_ctx->pos[0])
		goto errmsg;
	nlh = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &smc_gen_nl_family, NLM_F_MULTI,
			  SMC_NETLINK_GET_STATS);
	if (!nlh)
		goto errmsg;

	attrs = nla_nest_start(skb, SMC_GEN_STATS);
	if (!attrs)
		goto errnest;
	stats = kzalloc(sizeof(*stats), GFP_KERNEL);
	if (!stats)
		goto erralloc;
	size = sizeof(*stats) / sizeof(u64);
	for_each_possible_cpu(cpu) {
		src = (u64 *)per_cpu_ptr(net->smc.smc_stats, cpu);
		sum = (u64 *)stats;
		for (i = 0; i < size; i++)
			*(sum++) += *(src++);
	}
	if (smc_nl_fill_stats_tech_data(skb, stats, SMC_TYPE_D))
		goto errattr;
	if (smc_nl_fill_stats_tech_data(skb, stats, SMC_TYPE_R))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_CLNT_HS_ERR_CNT,
			      stats->clnt_hshake_err_cnt,
			      SMC_NLA_STATS_PAD))
		goto errattr;
	if (nla_put_u64_64bit(skb, SMC_NLA_STATS_SRV_HS_ERR_CNT,
			      stats->srv_hshake_err_cnt,
			      SMC_NLA_STATS_PAD))
		goto errattr;

	nla_nest_end(skb, attrs);
	genlmsg_end(skb, nlh);
	cb_ctx->pos[0] = 1;
	kfree(stats);
	return skb->len;

errattr:
	kfree(stats);
erralloc:
	nla_nest_cancel(skb, attrs);
errnest:
	genlmsg_cancel(skb, nlh);
errmsg:
	return skb->len;
}

static int smc_nl_get_fback_details(struct sk_buff *skb,
				    struct netlink_callback *cb, int pos,
				    bool is_srv)
{
	struct smc_nl_dmp_ctx *cb_ctx = smc_nl_dmp_ctx(cb);
	struct net *net = sock_net(skb->sk);
	int cnt_reported = cb_ctx->pos[2];
	struct smc_stats_fback *trgt_arr;
	struct nlattr *attrs;
	int rc = 0;
	void *nlh;

	if (is_srv)
		trgt_arr = &net->smc.fback_rsn->srv[0];
	else
		trgt_arr = &net->smc.fback_rsn->clnt[0];
	if (!trgt_arr[pos].fback_code)
		return -ENODATA;
	nlh = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &smc_gen_nl_family, NLM_F_MULTI,
			  SMC_NETLINK_GET_FBACK_STATS);
	if (!nlh)
		goto errmsg;
	attrs = nla_nest_start(skb, SMC_GEN_FBACK_STATS);
	if (!attrs)
		goto errout;
	if (nla_put_u8(skb, SMC_NLA_FBACK_STATS_TYPE, is_srv))
		goto errattr;
	if (!cnt_reported) {
		if (nla_put_u64_64bit(skb, SMC_NLA_FBACK_STATS_SRV_CNT,
				      net->smc.fback_rsn->srv_fback_cnt,
				      SMC_NLA_FBACK_STATS_PAD))
			goto errattr;
		if (nla_put_u64_64bit(skb, SMC_NLA_FBACK_STATS_CLNT_CNT,
				      net->smc.fback_rsn->clnt_fback_cnt,
				      SMC_NLA_FBACK_STATS_PAD))
			goto errattr;
		cnt_reported = 1;
	}

	if (nla_put_u32(skb, SMC_NLA_FBACK_STATS_RSN_CODE,
			trgt_arr[pos].fback_code))
		goto errattr;
	if (nla_put_u16(skb, SMC_NLA_FBACK_STATS_RSN_CNT,
			trgt_arr[pos].count))
		goto errattr;

	cb_ctx->pos[2] = cnt_reported;
	nla_nest_end(skb, attrs);
	genlmsg_end(skb, nlh);
	return rc;

errattr:
	nla_nest_cancel(skb, attrs);
errout:
	genlmsg_cancel(skb, nlh);
errmsg:
	return -EMSGSIZE;
}

int smc_nl_get_fback_stats(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct smc_nl_dmp_ctx *cb_ctx = smc_nl_dmp_ctx(cb);
	struct net *net = sock_net(skb->sk);
	int rc_srv = 0, rc_clnt = 0, k;
	int skip_serv = cb_ctx->pos[1];
	int snum = cb_ctx->pos[0];
	bool is_srv = true;

	mutex_lock(&net->smc.mutex_fback_rsn);
	for (k = 0; k < SMC_MAX_FBACK_RSN_CNT; k++) {
		if (k < snum)
			continue;
		if (!skip_serv) {
			rc_srv = smc_nl_get_fback_details(skb, cb, k, is_srv);
			if (rc_srv && rc_srv != -ENODATA)
				break;
		} else {
			skip_serv = 0;
		}
		rc_clnt = smc_nl_get_fback_details(skb, cb, k, !is_srv);
		if (rc_clnt && rc_clnt != -ENODATA) {
			skip_serv = 1;
			break;
		}
		if (rc_clnt == -ENODATA && rc_srv == -ENODATA)
			break;
	}
	mutex_unlock(&net->smc.mutex_fback_rsn);
	cb_ctx->pos[1] = skip_serv;
	cb_ctx->pos[0] = k;
	return skb->len;
}
