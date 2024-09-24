// SPDX-License-Identifier: GPL-2.0-only
#include <linux/netlink.h>
#include <linux/nospec.h>
#include <linux/rtnetlink.h>
#include <linux/types.h>
#include <net/ip.h>
#include <net/net_namespace.h>
#include <net/tcp.h>

static int ip_metrics_convert(struct nlattr *fc_mx,
			      int fc_mx_len, u32 *metrics,
			      struct netlink_ext_ack *extack)
{
	bool ecn_ca = false;
	struct nlattr *nla;
	int remaining;

	nla_for_each_attr(nla, fc_mx, fc_mx_len, remaining) {
		int type = nla_type(nla);
		u32 val;

		if (!type)
			continue;
		if (type > RTAX_MAX) {
			NL_SET_ERR_MSG(extack, "Invalid metric type");
			return -EINVAL;
		}

		type = array_index_nospec(type, RTAX_MAX + 1);
		if (type == RTAX_CC_ALGO) {
			char tmp[TCP_CA_NAME_MAX];

			nla_strscpy(tmp, nla, sizeof(tmp));
			val = tcp_ca_get_key_by_name(tmp, &ecn_ca);
			if (val == TCP_CA_UNSPEC) {
				NL_SET_ERR_MSG(extack, "Unknown tcp congestion algorithm");
				return -EINVAL;
			}
		} else {
			if (nla_len(nla) != sizeof(u32)) {
				NL_SET_ERR_MSG_ATTR(extack, nla,
						    "Invalid attribute in metrics");
				return -EINVAL;
			}
			val = nla_get_u32(nla);
		}
		if (type == RTAX_ADVMSS && val > 65535 - 40)
			val = 65535 - 40;
		if (type == RTAX_MTU && val > 65535 - 15)
			val = 65535 - 15;
		if (type == RTAX_HOPLIMIT && val > 255)
			val = 255;
		if (type == RTAX_FEATURES && (val & ~RTAX_FEATURE_MASK)) {
			NL_SET_ERR_MSG(extack, "Unknown flag set in feature mask in metrics attribute");
			return -EINVAL;
		}
		metrics[type - 1] = val;
	}

	if (ecn_ca)
		metrics[RTAX_FEATURES - 1] |= DST_FEATURE_ECN_CA;

	return 0;
}

struct dst_metrics *ip_fib_metrics_init(struct nlattr *fc_mx,
					int fc_mx_len,
					struct netlink_ext_ack *extack)
{
	struct dst_metrics *fib_metrics;
	int err;

	if (!fc_mx)
		return (struct dst_metrics *)&dst_default_metrics;

	fib_metrics = kzalloc(sizeof(*fib_metrics), GFP_KERNEL);
	if (unlikely(!fib_metrics))
		return ERR_PTR(-ENOMEM);

	err = ip_metrics_convert(fc_mx, fc_mx_len, fib_metrics->metrics,
				 extack);
	if (!err) {
		refcount_set(&fib_metrics->refcnt, 1);
	} else {
		kfree(fib_metrics);
		fib_metrics = ERR_PTR(err);
	}

	return fib_metrics;
}
EXPORT_SYMBOL_GPL(ip_fib_metrics_init);
