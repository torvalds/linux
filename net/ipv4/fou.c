#include <linux/module.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <net/genetlink.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/udp.h>
#include <net/udp_tunnel.h>
#include <net/xfrm.h>
#include <uapi/linux/fou.h>
#include <uapi/linux/genetlink.h>

static DEFINE_SPINLOCK(fou_lock);
static LIST_HEAD(fou_list);

struct fou {
	struct socket *sock;
	u8 protocol;
	u16 port;
	struct udp_offload udp_offloads;
	struct list_head list;
};

struct fou_cfg {
	u8 protocol;
	struct udp_port_cfg udp_config;
};

static inline struct fou *fou_from_sock(struct sock *sk)
{
	return sk->sk_user_data;
}

static int fou_udp_encap_recv_deliver(struct sk_buff *skb,
				      u8 protocol, size_t len)
{
	struct iphdr *iph = ip_hdr(skb);

	/* Remove 'len' bytes from the packet (UDP header and
	 * FOU header if present), modify the protocol to the one
	 * we found, and then call rcv_encap.
	 */
	iph->tot_len = htons(ntohs(iph->tot_len) - len);
	__skb_pull(skb, len);
	skb_postpull_rcsum(skb, udp_hdr(skb), len);
	skb_reset_transport_header(skb);

	return -protocol;
}

static int fou_udp_recv(struct sock *sk, struct sk_buff *skb)
{
	struct fou *fou = fou_from_sock(sk);

	if (!fou)
		return 1;

	return fou_udp_encap_recv_deliver(skb, fou->protocol,
					  sizeof(struct udphdr));
}

static struct sk_buff **fou_gro_receive(struct sk_buff **head,
					struct sk_buff *skb)
{
	const struct net_offload *ops;
	struct sk_buff **pp = NULL;
	u8 proto = NAPI_GRO_CB(skb)->proto;
	const struct net_offload **offloads;

	rcu_read_lock();
	offloads = NAPI_GRO_CB(skb)->is_ipv6 ? inet6_offloads : inet_offloads;
	ops = rcu_dereference(offloads[proto]);
	if (!ops || !ops->callbacks.gro_receive)
		goto out_unlock;

	pp = ops->callbacks.gro_receive(head, skb);

out_unlock:
	rcu_read_unlock();

	return pp;
}

static int fou_gro_complete(struct sk_buff *skb, int nhoff)
{
	const struct net_offload *ops;
	u8 proto = NAPI_GRO_CB(skb)->proto;
	int err = -ENOSYS;
	const struct net_offload **offloads;

	rcu_read_lock();
	offloads = NAPI_GRO_CB(skb)->is_ipv6 ? inet6_offloads : inet_offloads;
	ops = rcu_dereference(offloads[proto]);
	if (WARN_ON(!ops || !ops->callbacks.gro_complete))
		goto out_unlock;

	err = ops->callbacks.gro_complete(skb, nhoff);

out_unlock:
	rcu_read_unlock();

	return err;
}

static int fou_add_to_port_list(struct fou *fou)
{
	struct fou *fout;

	spin_lock(&fou_lock);
	list_for_each_entry(fout, &fou_list, list) {
		if (fou->port == fout->port) {
			spin_unlock(&fou_lock);
			return -EALREADY;
		}
	}

	list_add(&fou->list, &fou_list);
	spin_unlock(&fou_lock);

	return 0;
}

static void fou_release(struct fou *fou)
{
	struct socket *sock = fou->sock;
	struct sock *sk = sock->sk;

	udp_del_offload(&fou->udp_offloads);

	list_del(&fou->list);

	/* Remove hooks into tunnel socket */
	sk->sk_user_data = NULL;

	sock_release(sock);

	kfree(fou);
}

static int fou_create(struct net *net, struct fou_cfg *cfg,
		      struct socket **sockp)
{
	struct fou *fou = NULL;
	int err;
	struct socket *sock = NULL;
	struct sock *sk;

	/* Open UDP socket */
	err = udp_sock_create(net, &cfg->udp_config, &sock);
	if (err < 0)
		goto error;

	/* Allocate FOU port structure */
	fou = kzalloc(sizeof(*fou), GFP_KERNEL);
	if (!fou) {
		err = -ENOMEM;
		goto error;
	}

	sk = sock->sk;

	/* Mark socket as an encapsulation socket. See net/ipv4/udp.c */
	fou->protocol = cfg->protocol;
	fou->port =  cfg->udp_config.local_udp_port;
	udp_sk(sk)->encap_rcv = fou_udp_recv;

	udp_sk(sk)->encap_type = 1;
	udp_encap_enable();

	sk->sk_user_data = fou;
	fou->sock = sock;

	udp_set_convert_csum(sk, true);

	sk->sk_allocation = GFP_ATOMIC;

	fou->udp_offloads.callbacks.gro_receive = fou_gro_receive;
	fou->udp_offloads.callbacks.gro_complete = fou_gro_complete;
	fou->udp_offloads.port = cfg->udp_config.local_udp_port;
	fou->udp_offloads.ipproto = cfg->protocol;

	if (cfg->udp_config.family == AF_INET) {
		err = udp_add_offload(&fou->udp_offloads);
		if (err)
			goto error;
	}

	err = fou_add_to_port_list(fou);
	if (err)
		goto error;

	if (sockp)
		*sockp = sock;

	return 0;

error:
	kfree(fou);
	if (sock)
		sock_release(sock);

	return err;
}

static int fou_destroy(struct net *net, struct fou_cfg *cfg)
{
	struct fou *fou;
	u16 port = cfg->udp_config.local_udp_port;
	int err = -EINVAL;

	spin_lock(&fou_lock);
	list_for_each_entry(fou, &fou_list, list) {
		if (fou->port == port) {
			udp_del_offload(&fou->udp_offloads);
			fou_release(fou);
			err = 0;
			break;
		}
	}
	spin_unlock(&fou_lock);

	return err;
}

static struct genl_family fou_nl_family = {
	.id		= GENL_ID_GENERATE,
	.hdrsize	= 0,
	.name		= FOU_GENL_NAME,
	.version	= FOU_GENL_VERSION,
	.maxattr	= FOU_ATTR_MAX,
	.netnsok	= true,
};

static struct nla_policy fou_nl_policy[FOU_ATTR_MAX + 1] = {
	[FOU_ATTR_PORT] = { .type = NLA_U16, },
	[FOU_ATTR_AF] = { .type = NLA_U8, },
	[FOU_ATTR_IPPROTO] = { .type = NLA_U8, },
};

static int parse_nl_config(struct genl_info *info,
			   struct fou_cfg *cfg)
{
	memset(cfg, 0, sizeof(*cfg));

	cfg->udp_config.family = AF_INET;

	if (info->attrs[FOU_ATTR_AF]) {
		u8 family = nla_get_u8(info->attrs[FOU_ATTR_AF]);

		if (family != AF_INET && family != AF_INET6)
			return -EINVAL;

		cfg->udp_config.family = family;
	}

	if (info->attrs[FOU_ATTR_PORT]) {
		u16 port = nla_get_u16(info->attrs[FOU_ATTR_PORT]);

		cfg->udp_config.local_udp_port = port;
	}

	if (info->attrs[FOU_ATTR_IPPROTO])
		cfg->protocol = nla_get_u8(info->attrs[FOU_ATTR_IPPROTO]);

	return 0;
}

static int fou_nl_cmd_add_port(struct sk_buff *skb, struct genl_info *info)
{
	struct fou_cfg cfg;
	int err;

	err = parse_nl_config(info, &cfg);
	if (err)
		return err;

	return fou_create(&init_net, &cfg, NULL);
}

static int fou_nl_cmd_rm_port(struct sk_buff *skb, struct genl_info *info)
{
	struct fou_cfg cfg;

	parse_nl_config(info, &cfg);

	return fou_destroy(&init_net, &cfg);
}

static const struct genl_ops fou_nl_ops[] = {
	{
		.cmd = FOU_CMD_ADD,
		.doit = fou_nl_cmd_add_port,
		.policy = fou_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = FOU_CMD_DEL,
		.doit = fou_nl_cmd_rm_port,
		.policy = fou_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
};

static int __init fou_init(void)
{
	int ret;

	ret = genl_register_family_with_ops(&fou_nl_family,
					    fou_nl_ops);

	return ret;
}

static void __exit fou_fini(void)
{
	struct fou *fou, *next;

	genl_unregister_family(&fou_nl_family);

	/* Close all the FOU sockets */

	spin_lock(&fou_lock);
	list_for_each_entry_safe(fou, next, &fou_list, list)
		fou_release(fou);
	spin_unlock(&fou_lock);
}

module_init(fou_init);
module_exit(fou_fini);
MODULE_AUTHOR("Tom Herbert <therbert@google.com>");
MODULE_LICENSE("GPL");
