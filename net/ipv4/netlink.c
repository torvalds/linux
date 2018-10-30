#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/types.h>
#include <net/net_namespace.h>
#include <net/netlink.h>
#include <net/ip.h>

int rtm_getroute_parse_ip_proto(struct nlattr *attr, u8 *ip_proto,
				struct netlink_ext_ack *extack)
{
	*ip_proto = nla_get_u8(attr);

	switch (*ip_proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_ICMP:
		return 0;
	default:
		NL_SET_ERR_MSG(extack, "Unsupported ip proto");
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL_GPL(rtm_getroute_parse_ip_proto);
