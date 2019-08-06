#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/types.h>
#include <net/net_namespace.h>
#include <net/netlink.h>
#include <linux/in6.h>
#include <net/ip.h>

int rtm_getroute_parse_ip_proto(struct nlattr *attr, u8 *ip_proto, u8 family,
				struct netlink_ext_ack *extack)
{
	*ip_proto = nla_get_u8(attr);

	switch (*ip_proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		return 0;
	case IPPROTO_ICMP:
		if (family != AF_INET)
			break;
		return 0;
#if IS_ENABLED(CONFIG_IPV6)
	case IPPROTO_ICMPV6:
		if (family != AF_INET6)
			break;
		return 0;
#endif
	}
	NL_SET_ERR_MSG(extack, "Unsupported ip proto");
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(rtm_getroute_parse_ip_proto);
