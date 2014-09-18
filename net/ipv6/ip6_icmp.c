#include <linux/export.h>
#include <linux/icmpv6.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>

#include <net/ipv6.h>

#if IS_ENABLED(CONFIG_IPV6)

static ip6_icmp_send_t __rcu *ip6_icmp_send;

int inet6_register_icmp_sender(ip6_icmp_send_t *fn)
{
	return (cmpxchg((ip6_icmp_send_t **)&ip6_icmp_send, NULL, fn) == NULL) ?
	        0 : -EBUSY;
}
EXPORT_SYMBOL(inet6_register_icmp_sender);

int inet6_unregister_icmp_sender(ip6_icmp_send_t *fn)
{
	int ret;

	ret = (cmpxchg((ip6_icmp_send_t **)&ip6_icmp_send, fn, NULL) == fn) ?
	      0 : -EINVAL;

	synchronize_net();

	return ret;
}
EXPORT_SYMBOL(inet6_unregister_icmp_sender);

void icmpv6_send(struct sk_buff *skb, u8 type, u8 code, __u32 info)
{
	ip6_icmp_send_t *send;

	rcu_read_lock();
	send = rcu_dereference(ip6_icmp_send);

	if (!send)
		goto out;
	send(skb, type, code, info);
out:
	rcu_read_unlock();
}
EXPORT_SYMBOL(icmpv6_send);
#endif
