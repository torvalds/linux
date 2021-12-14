/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 - 2019 Intel Corporation
 */
#ifndef __PMSR_H
#define __PMSR_H
#include <net/cfg80211.h>
#include "core.h"
#include "nl80211.h"
#include "rdev-ops.h"

static int pmsr_parse_ftm(struct cfg80211_registered_device *rdev,
			  struct nlattr *ftmreq,
			  struct cfg80211_pmsr_request_peer *out,
			  struct genl_info *info)
{
	const struct cfg80211_pmsr_capabilities *capa = rdev->wiphy.pmsr_capa;
	struct nlattr *tb[NL80211_PMSR_FTM_REQ_ATTR_MAX + 1];
	u32 preamble = NL80211_PREAMBLE_DMG; /* only optional in DMG */

	/* validate existing data */
	if (!(rdev->wiphy.pmsr_capa->ftm.bandwidths & BIT(out->chandef.width))) {
		NL_SET_ERR_MSG(info->extack, "FTM: unsupported bandwidth");
		return -EINVAL;
	}

	/* no validation needed - was already done via nested policy */
	nla_parse_nested_deprecated(tb, NL80211_PMSR_FTM_REQ_ATTR_MAX, ftmreq,
				    NULL, NULL);

	if (tb[NL80211_PMSR_FTM_REQ_ATTR_PREAMBLE])
		preamble = nla_get_u32(tb[NL80211_PMSR_FTM_REQ_ATTR_PREAMBLE]);

	/* set up values - struct is 0-initialized */
	out->ftm.requested = true;

	switch (out->chandef.chan->band) {
	case NL80211_BAND_60GHZ:
		/* optional */
		break;
	default:
		if (!tb[NL80211_PMSR_FTM_REQ_ATTR_PREAMBLE]) {
			NL_SET_ERR_MSG(info->extack,
				       "FTM: must specify preamble");
			return -EINVAL;
		}
	}

	if (!(capa->ftm.preambles & BIT(preamble))) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[NL80211_PMSR_FTM_REQ_ATTR_PREAMBLE],
				    "FTM: invalid preamble");
		return -EINVAL;
	}

	out->ftm.preamble = preamble;

	out->ftm.burst_period = 0;
	if (tb[NL80211_PMSR_FTM_REQ_ATTR_BURST_PERIOD])
		out->ftm.burst_period =
			nla_get_u32(tb[NL80211_PMSR_FTM_REQ_ATTR_BURST_PERIOD]);

	out->ftm.asap = !!tb[NL80211_PMSR_FTM_REQ_ATTR_ASAP];
	if (out->ftm.asap && !capa->ftm.asap) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[NL80211_PMSR_FTM_REQ_ATTR_ASAP],
				    "FTM: ASAP mode not supported");
		return -EINVAL;
	}

	if (!out->ftm.asap && !capa->ftm.non_asap) {
		NL_SET_ERR_MSG(info->extack,
			       "FTM: non-ASAP mode not supported");
		return -EINVAL;
	}

	out->ftm.num_bursts_exp = 0;
	if (tb[NL80211_PMSR_FTM_REQ_ATTR_NUM_BURSTS_EXP])
		out->ftm.num_bursts_exp =
			nla_get_u32(tb[NL80211_PMSR_FTM_REQ_ATTR_NUM_BURSTS_EXP]);

	if (capa->ftm.max_bursts_exponent >= 0 &&
	    out->ftm.num_bursts_exp > capa->ftm.max_bursts_exponent) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[NL80211_PMSR_FTM_REQ_ATTR_NUM_BURSTS_EXP],
				    "FTM: max NUM_BURSTS_EXP must be set lower than the device limit");
		return -EINVAL;
	}

	out->ftm.burst_duration = 15;
	if (tb[NL80211_PMSR_FTM_REQ_ATTR_BURST_DURATION])
		out->ftm.burst_duration =
			nla_get_u32(tb[NL80211_PMSR_FTM_REQ_ATTR_BURST_DURATION]);

	out->ftm.ftms_per_burst = 0;
	if (tb[NL80211_PMSR_FTM_REQ_ATTR_FTMS_PER_BURST])
		out->ftm.ftms_per_burst =
			nla_get_u32(tb[NL80211_PMSR_FTM_REQ_ATTR_FTMS_PER_BURST]);

	if (capa->ftm.max_ftms_per_burst &&
	    (out->ftm.ftms_per_burst > capa->ftm.max_ftms_per_burst ||
	     out->ftm.ftms_per_burst == 0)) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[NL80211_PMSR_FTM_REQ_ATTR_FTMS_PER_BURST],
				    "FTM: FTMs per burst must be set lower than the device limit but non-zero");
		return -EINVAL;
	}

	out->ftm.ftmr_retries = 3;
	if (tb[NL80211_PMSR_FTM_REQ_ATTR_NUM_FTMR_RETRIES])
		out->ftm.ftmr_retries =
			nla_get_u32(tb[NL80211_PMSR_FTM_REQ_ATTR_NUM_FTMR_RETRIES]);

	out->ftm.request_lci = !!tb[NL80211_PMSR_FTM_REQ_ATTR_REQUEST_LCI];
	if (out->ftm.request_lci && !capa->ftm.request_lci) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[NL80211_PMSR_FTM_REQ_ATTR_REQUEST_LCI],
				    "FTM: LCI request not supported");
	}

	out->ftm.request_civicloc =
		!!tb[NL80211_PMSR_FTM_REQ_ATTR_REQUEST_CIVICLOC];
	if (out->ftm.request_civicloc && !capa->ftm.request_civicloc) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[NL80211_PMSR_FTM_REQ_ATTR_REQUEST_CIVICLOC],
			    "FTM: civic location request not supported");
	}

	out->ftm.trigger_based =
		!!tb[NL80211_PMSR_FTM_REQ_ATTR_TRIGGER_BASED];
	if (out->ftm.trigger_based && !capa->ftm.trigger_based) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[NL80211_PMSR_FTM_REQ_ATTR_TRIGGER_BASED],
				    "FTM: trigger based ranging is not supported");
		return -EINVAL;
	}

	out->ftm.non_trigger_based =
		!!tb[NL80211_PMSR_FTM_REQ_ATTR_NON_TRIGGER_BASED];
	if (out->ftm.non_trigger_based && !capa->ftm.non_trigger_based) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[NL80211_PMSR_FTM_REQ_ATTR_NON_TRIGGER_BASED],
				    "FTM: trigger based ranging is not supported");
		return -EINVAL;
	}

	if (out->ftm.trigger_based && out->ftm.non_trigger_based) {
		NL_SET_ERR_MSG(info->extack,
			       "FTM: can't set both trigger based and non trigger based");
		return -EINVAL;
	}

	if ((out->ftm.trigger_based || out->ftm.non_trigger_based) &&
	    out->ftm.preamble != NL80211_PREAMBLE_HE) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[NL80211_PMSR_FTM_REQ_ATTR_PREAMBLE],
				    "FTM: non EDCA based ranging must use HE preamble");
		return -EINVAL;
	}

	return 0;
}

static int pmsr_parse_peer(struct cfg80211_registered_device *rdev,
			   struct nlattr *peer,
			   struct cfg80211_pmsr_request_peer *out,
			   struct genl_info *info)
{
	struct nlattr *tb[NL80211_PMSR_PEER_ATTR_MAX + 1];
	struct nlattr *req[NL80211_PMSR_REQ_ATTR_MAX + 1];
	struct nlattr *treq;
	int err, rem;

	/* no validation needed - was already done via nested policy */
	nla_parse_nested_deprecated(tb, NL80211_PMSR_PEER_ATTR_MAX, peer,
				    NULL, NULL);

	if (!tb[NL80211_PMSR_PEER_ATTR_ADDR] ||
	    !tb[NL80211_PMSR_PEER_ATTR_CHAN] ||
	    !tb[NL80211_PMSR_PEER_ATTR_REQ]) {
		NL_SET_ERR_MSG_ATTR(info->extack, peer,
				    "insufficient peer data");
		return -EINVAL;
	}

	memcpy(out->addr, nla_data(tb[NL80211_PMSR_PEER_ATTR_ADDR]), ETH_ALEN);

	/* reuse info->attrs */
	memset(info->attrs, 0, sizeof(*info->attrs) * (NL80211_ATTR_MAX + 1));
	err = nla_parse_nested_deprecated(info->attrs, NL80211_ATTR_MAX,
					  tb[NL80211_PMSR_PEER_ATTR_CHAN],
					  NULL, info->extack);
	if (err)
		return err;

	err = nl80211_parse_chandef(rdev, info, &out->chandef);
	if (err)
		return err;

	/* no validation needed - was already done via nested policy */
	nla_parse_nested_deprecated(req, NL80211_PMSR_REQ_ATTR_MAX,
				    tb[NL80211_PMSR_PEER_ATTR_REQ], NULL,
				    NULL);

	if (!req[NL80211_PMSR_REQ_ATTR_DATA]) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    tb[NL80211_PMSR_PEER_ATTR_REQ],
				    "missing request type/data");
		return -EINVAL;
	}

	if (req[NL80211_PMSR_REQ_ATTR_GET_AP_TSF])
		out->report_ap_tsf = true;

	if (out->report_ap_tsf && !rdev->wiphy.pmsr_capa->report_ap_tsf) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    req[NL80211_PMSR_REQ_ATTR_GET_AP_TSF],
				    "reporting AP TSF is not supported");
		return -EINVAL;
	}

	nla_for_each_nested(treq, req[NL80211_PMSR_REQ_ATTR_DATA], rem) {
		switch (nla_type(treq)) {
		case NL80211_PMSR_TYPE_FTM:
			err = pmsr_parse_ftm(rdev, treq, out, info);
			break;
		default:
			NL_SET_ERR_MSG_ATTR(info->extack, treq,
					    "unsupported measurement type");
			err = -EINVAL;
		}
	}

	if (err)
		return err;

	return 0;
}

int nl80211_pmsr_start(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *reqattr = info->attrs[NL80211_ATTR_PEER_MEASUREMENTS];
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];
	struct cfg80211_pmsr_request *req;
	struct nlattr *peers, *peer;
	int count, rem, err, idx;

	if (!rdev->wiphy.pmsr_capa)
		return -EOPNOTSUPP;

	if (!reqattr)
		return -EINVAL;

	peers = nla_find(nla_data(reqattr), nla_len(reqattr),
			 NL80211_PMSR_ATTR_PEERS);
	if (!peers)
		return -EINVAL;

	count = 0;
	nla_for_each_nested(peer, peers, rem) {
		count++;

		if (count > rdev->wiphy.pmsr_capa->max_peers) {
			NL_SET_ERR_MSG_ATTR(info->extack, peer,
					    "Too many peers used");
			return -EINVAL;
		}
	}

	req = kzalloc(struct_size(req, peers, count), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	if (info->attrs[NL80211_ATTR_TIMEOUT])
		req->timeout = nla_get_u32(info->attrs[NL80211_ATTR_TIMEOUT]);

	if (info->attrs[NL80211_ATTR_MAC]) {
		if (!rdev->wiphy.pmsr_capa->randomize_mac_addr) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    info->attrs[NL80211_ATTR_MAC],
					    "device cannot randomize MAC address");
			err = -EINVAL;
			goto out_err;
		}

		err = nl80211_parse_random_mac(info->attrs, req->mac_addr,
					       req->mac_addr_mask);
		if (err)
			goto out_err;
	} else {
		memcpy(req->mac_addr, wdev_address(wdev), ETH_ALEN);
		eth_broadcast_addr(req->mac_addr_mask);
	}

	idx = 0;
	nla_for_each_nested(peer, peers, rem) {
		/* NB: this reuses info->attrs, but we no longer need it */
		err = pmsr_parse_peer(rdev, peer, &req->peers[idx], info);
		if (err)
			goto out_err;
		idx++;
	}

	req->n_peers = count;
	req->cookie = cfg80211_assign_cookie(rdev);
	req->nl_portid = info->snd_portid;

	err = rdev_start_pmsr(rdev, wdev, req);
	if (err)
		goto out_err;

	list_add_tail(&req->list, &wdev->pmsr_list);

	nl_set_extack_cookie_u64(info->extack, req->cookie);
	return 0;
out_err:
	kfree(req);
	return err;
}

void cfg80211_pmsr_complete(struct wireless_dev *wdev,
			    struct cfg80211_pmsr_request *req,
			    gfp_t gfp)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct cfg80211_pmsr_request *tmp, *prev, *to_free = NULL;
	struct sk_buff *msg;
	void *hdr;

	trace_cfg80211_pmsr_complete(wdev->wiphy, wdev, req->cookie);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		goto free_request;

	hdr = nl80211hdr_put(msg, 0, 0, 0,
			     NL80211_CMD_PEER_MEASUREMENT_COMPLETE);
	if (!hdr)
		goto free_msg;

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u64_64bit(msg, NL80211_ATTR_WDEV, wdev_id(wdev),
			      NL80211_ATTR_PAD))
		goto free_msg;

	if (nla_put_u64_64bit(msg, NL80211_ATTR_COOKIE, req->cookie,
			      NL80211_ATTR_PAD))
		goto free_msg;

	genlmsg_end(msg, hdr);
	genlmsg_unicast(wiphy_net(wdev->wiphy), msg, req->nl_portid);
	goto free_request;
free_msg:
	nlmsg_free(msg);
free_request:
	spin_lock_bh(&wdev->pmsr_lock);
	/*
	 * cfg80211_pmsr_process_abort() may have already moved this request
	 * to the free list, and will free it later. In this case, don't free
	 * it here.
	 */
	list_for_each_entry_safe(tmp, prev, &wdev->pmsr_list, list) {
		if (tmp == req) {
			list_del(&req->list);
			to_free = req;
			break;
		}
	}
	spin_unlock_bh(&wdev->pmsr_lock);
	kfree(to_free);
}
EXPORT_SYMBOL_GPL(cfg80211_pmsr_complete);

static int nl80211_pmsr_send_ftm_res(struct sk_buff *msg,
				     struct cfg80211_pmsr_result *res)
{
	if (res->status == NL80211_PMSR_STATUS_FAILURE) {
		if (nla_put_u32(msg, NL80211_PMSR_FTM_RESP_ATTR_FAIL_REASON,
				res->ftm.failure_reason))
			goto error;

		if (res->ftm.failure_reason ==
			NL80211_PMSR_FTM_FAILURE_PEER_BUSY &&
		    res->ftm.busy_retry_time &&
		    nla_put_u32(msg, NL80211_PMSR_FTM_RESP_ATTR_BUSY_RETRY_TIME,
				res->ftm.busy_retry_time))
			goto error;

		return 0;
	}

#define PUT(tp, attr, val)						\
	do {								\
		if (nla_put_##tp(msg,					\
				 NL80211_PMSR_FTM_RESP_ATTR_##attr,	\
				 res->ftm.val))				\
			goto error;					\
	} while (0)

#define PUTOPT(tp, attr, val)						\
	do {								\
		if (res->ftm.val##_valid)				\
			PUT(tp, attr, val);				\
	} while (0)

#define PUT_U64(attr, val)						\
	do {								\
		if (nla_put_u64_64bit(msg,				\
				      NL80211_PMSR_FTM_RESP_ATTR_##attr,\
				      res->ftm.val,			\
				      NL80211_PMSR_FTM_RESP_ATTR_PAD))	\
			goto error;					\
	} while (0)

#define PUTOPT_U64(attr, val)						\
	do {								\
		if (res->ftm.val##_valid)				\
			PUT_U64(attr, val);				\
	} while (0)

	if (res->ftm.burst_index >= 0)
		PUT(u32, BURST_INDEX, burst_index);
	PUTOPT(u32, NUM_FTMR_ATTEMPTS, num_ftmr_attempts);
	PUTOPT(u32, NUM_FTMR_SUCCESSES, num_ftmr_successes);
	PUT(u8, NUM_BURSTS_EXP, num_bursts_exp);
	PUT(u8, BURST_DURATION, burst_duration);
	PUT(u8, FTMS_PER_BURST, ftms_per_burst);
	PUTOPT(s32, RSSI_AVG, rssi_avg);
	PUTOPT(s32, RSSI_SPREAD, rssi_spread);
	if (res->ftm.tx_rate_valid &&
	    !nl80211_put_sta_rate(msg, &res->ftm.tx_rate,
				  NL80211_PMSR_FTM_RESP_ATTR_TX_RATE))
		goto error;
	if (res->ftm.rx_rate_valid &&
	    !nl80211_put_sta_rate(msg, &res->ftm.rx_rate,
				  NL80211_PMSR_FTM_RESP_ATTR_RX_RATE))
		goto error;
	PUTOPT_U64(RTT_AVG, rtt_avg);
	PUTOPT_U64(RTT_VARIANCE, rtt_variance);
	PUTOPT_U64(RTT_SPREAD, rtt_spread);
	PUTOPT_U64(DIST_AVG, dist_avg);
	PUTOPT_U64(DIST_VARIANCE, dist_variance);
	PUTOPT_U64(DIST_SPREAD, dist_spread);
	if (res->ftm.lci && res->ftm.lci_len &&
	    nla_put(msg, NL80211_PMSR_FTM_RESP_ATTR_LCI,
		    res->ftm.lci_len, res->ftm.lci))
		goto error;
	if (res->ftm.civicloc && res->ftm.civicloc_len &&
	    nla_put(msg, NL80211_PMSR_FTM_RESP_ATTR_CIVICLOC,
		    res->ftm.civicloc_len, res->ftm.civicloc))
		goto error;
#undef PUT
#undef PUTOPT
#undef PUT_U64
#undef PUTOPT_U64

	return 0;
error:
	return -ENOSPC;
}

static int nl80211_pmsr_send_result(struct sk_buff *msg,
				    struct cfg80211_pmsr_result *res)
{
	struct nlattr *pmsr, *peers, *peer, *resp, *data, *typedata;

	pmsr = nla_nest_start_noflag(msg, NL80211_ATTR_PEER_MEASUREMENTS);
	if (!pmsr)
		goto error;

	peers = nla_nest_start_noflag(msg, NL80211_PMSR_ATTR_PEERS);
	if (!peers)
		goto error;

	peer = nla_nest_start_noflag(msg, 1);
	if (!peer)
		goto error;

	if (nla_put(msg, NL80211_PMSR_PEER_ATTR_ADDR, ETH_ALEN, res->addr))
		goto error;

	resp = nla_nest_start_noflag(msg, NL80211_PMSR_PEER_ATTR_RESP);
	if (!resp)
		goto error;

	if (nla_put_u32(msg, NL80211_PMSR_RESP_ATTR_STATUS, res->status) ||
	    nla_put_u64_64bit(msg, NL80211_PMSR_RESP_ATTR_HOST_TIME,
			      res->host_time, NL80211_PMSR_RESP_ATTR_PAD))
		goto error;

	if (res->ap_tsf_valid &&
	    nla_put_u64_64bit(msg, NL80211_PMSR_RESP_ATTR_AP_TSF,
			      res->ap_tsf, NL80211_PMSR_RESP_ATTR_PAD))
		goto error;

	if (res->final && nla_put_flag(msg, NL80211_PMSR_RESP_ATTR_FINAL))
		goto error;

	data = nla_nest_start_noflag(msg, NL80211_PMSR_RESP_ATTR_DATA);
	if (!data)
		goto error;

	typedata = nla_nest_start_noflag(msg, res->type);
	if (!typedata)
		goto error;

	switch (res->type) {
	case NL80211_PMSR_TYPE_FTM:
		if (nl80211_pmsr_send_ftm_res(msg, res))
			goto error;
		break;
	default:
		WARN_ON(1);
	}

	nla_nest_end(msg, typedata);
	nla_nest_end(msg, data);
	nla_nest_end(msg, resp);
	nla_nest_end(msg, peer);
	nla_nest_end(msg, peers);
	nla_nest_end(msg, pmsr);

	return 0;
error:
	return -ENOSPC;
}

void cfg80211_pmsr_report(struct wireless_dev *wdev,
			  struct cfg80211_pmsr_request *req,
			  struct cfg80211_pmsr_result *result,
			  gfp_t gfp)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct sk_buff *msg;
	void *hdr;
	int err;

	trace_cfg80211_pmsr_report(wdev->wiphy, wdev, req->cookie,
				   result->addr);

	/*
	 * Currently, only variable items are LCI and civic location,
	 * both of which are reasonably short so we don't need to
	 * worry about them here for the allocation.
	 */
	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_PEER_MEASUREMENT_RESULT);
	if (!hdr)
		goto free;

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u64_64bit(msg, NL80211_ATTR_WDEV, wdev_id(wdev),
			      NL80211_ATTR_PAD))
		goto free;

	if (nla_put_u64_64bit(msg, NL80211_ATTR_COOKIE, req->cookie,
			      NL80211_ATTR_PAD))
		goto free;

	err = nl80211_pmsr_send_result(msg, result);
	if (err) {
		pr_err_ratelimited("peer measurement result: message didn't fit!");
		goto free;
	}

	genlmsg_end(msg, hdr);
	genlmsg_unicast(wiphy_net(wdev->wiphy), msg, req->nl_portid);
	return;
free:
	nlmsg_free(msg);
}
EXPORT_SYMBOL_GPL(cfg80211_pmsr_report);

static void cfg80211_pmsr_process_abort(struct wireless_dev *wdev)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct cfg80211_pmsr_request *req, *tmp;
	LIST_HEAD(free_list);

	lockdep_assert_held(&wdev->mtx);

	spin_lock_bh(&wdev->pmsr_lock);
	list_for_each_entry_safe(req, tmp, &wdev->pmsr_list, list) {
		if (req->nl_portid)
			continue;
		list_move_tail(&req->list, &free_list);
	}
	spin_unlock_bh(&wdev->pmsr_lock);

	list_for_each_entry_safe(req, tmp, &free_list, list) {
		rdev_abort_pmsr(rdev, wdev, req);

		kfree(req);
	}
}

void cfg80211_pmsr_free_wk(struct work_struct *work)
{
	struct wireless_dev *wdev = container_of(work, struct wireless_dev,
						 pmsr_free_wk);

	wdev_lock(wdev);
	cfg80211_pmsr_process_abort(wdev);
	wdev_unlock(wdev);
}

void cfg80211_pmsr_wdev_down(struct wireless_dev *wdev)
{
	struct cfg80211_pmsr_request *req;
	bool found = false;

	spin_lock_bh(&wdev->pmsr_lock);
	list_for_each_entry(req, &wdev->pmsr_list, list) {
		found = true;
		req->nl_portid = 0;
	}
	spin_unlock_bh(&wdev->pmsr_lock);

	if (found)
		cfg80211_pmsr_process_abort(wdev);

	WARN_ON(!list_empty(&wdev->pmsr_list));
}

void cfg80211_release_pmsr(struct wireless_dev *wdev, u32 portid)
{
	struct cfg80211_pmsr_request *req;

	spin_lock_bh(&wdev->pmsr_lock);
	list_for_each_entry(req, &wdev->pmsr_list, list) {
		if (req->nl_portid == portid) {
			req->nl_portid = 0;
			schedule_work(&wdev->pmsr_free_wk);
		}
	}
	spin_unlock_bh(&wdev->pmsr_lock);
}

#endif /* __PMSR_H */
