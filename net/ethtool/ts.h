/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _NET_ETHTOOL_TS_H
#define _NET_ETHTOOL_TS_H

#include "netlink.h"

/* phc_index is an int and -1 means no PHC, so keep the request non-negative */
static const struct netlink_range_validation ethnl_ts_prov_index_range = {
	.max = INT_MAX,
};

static const struct nla_policy
ethnl_ts_hwtst_prov_policy[ETHTOOL_A_TS_HWTSTAMP_PROVIDER_MAX + 1] = {
	[ETHTOOL_A_TS_HWTSTAMP_PROVIDER_INDEX] =
		NLA_POLICY_FULL_RANGE(NLA_U32, &ethnl_ts_prov_index_range),
	[ETHTOOL_A_TS_HWTSTAMP_PROVIDER_QUALIFIER] =
		NLA_POLICY_MAX(NLA_U32, HWTSTAMP_PROVIDER_QUALIFIER_CNT - 1)
};

int ts_parse_hwtst_provider(const struct nlattr *nest,
			    struct hwtstamp_provider_desc *hwprov_desc,
			    struct netlink_ext_ack *extack,
			    bool *mod);

#endif /* _NET_ETHTOOL_TS_H */
