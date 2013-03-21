#include <linux/module.h>

#include <net/sock.h>
#include <linux/netlink.h>
#include <linux/sock_diag.h>
#include <linux/netlink_diag.h>

#include "af_netlink.h"

static int sk_diag_dump_groups(struct sock *sk, struct sk_buff *nlskb)
{
	struct netlink_sock *nlk = nlk_sk(sk);

	if (nlk->groups == NULL)
		return 0;

	return nla_put(nlskb, NETLINK_DIAG_GROUPS, NLGRPSZ(nlk->ngroups),
		       nlk->groups);
}

static int sk_diag_fill(struct sock *sk, struct sk_buff *skb,
			struct netlink_diag_req *req,
			u32 portid, u32 seq, u32 flags, int sk_ino)
{
	struct nlmsghdr *nlh;
	struct netlink_diag_msg *rep;
	struct netlink_sock *nlk = nlk_sk(sk);

	nlh = nlmsg_put(skb, portid, seq, SOCK_DIAG_BY_FAMILY, sizeof(*rep),
			flags);
	if (!nlh)
		return -EMSGSIZE;

	rep = nlmsg_data(nlh);
	rep->ndiag_family	= AF_NETLINK;
	rep->ndiag_type		= sk->sk_type;
	rep->ndiag_protocol	= sk->sk_protocol;
	rep->ndiag_state	= sk->sk_state;

	rep->ndiag_ino		= sk_ino;
	rep->ndiag_portid	= nlk->portid;
	rep->ndiag_dst_portid	= nlk->dst_portid;
	rep->ndiag_dst_group	= nlk->dst_group;
	sock_diag_save_cookie(sk, rep->ndiag_cookie);

	if ((req->ndiag_show & NDIAG_SHOW_GROUPS) &&
	    sk_diag_dump_groups(sk, skb))
		goto out_nlmsg_trim;

	if ((req->ndiag_show & NDIAG_SHOW_MEMINFO) &&
	    sock_diag_put_meminfo(sk, skb, NETLINK_DIAG_MEMINFO))
		goto out_nlmsg_trim;

	return nlmsg_end(skb, nlh);

out_nlmsg_trim:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int __netlink_diag_dump(struct sk_buff *skb, struct netlink_callback *cb,
				int protocol, int s_num)
{
	struct netlink_table *tbl = &nl_table[protocol];
	struct nl_portid_hash *hash = &tbl->hash;
	struct net *net = sock_net(skb->sk);
	struct netlink_diag_req *req;
	struct sock *sk;
	int ret = 0, num = 0, i;

	req = nlmsg_data(cb->nlh);

	for (i = 0; i <= hash->mask; i++) {
		sk_for_each(sk, &hash->table[i]) {
			if (!net_eq(sock_net(sk), net))
				continue;
			if (num < s_num) {
				num++;
				continue;
			}

			if (sk_diag_fill(sk, skb, req,
					 NETLINK_CB(cb->skb).portid,
					 cb->nlh->nlmsg_seq,
					 NLM_F_MULTI,
					 sock_i_ino(sk)) < 0) {
				ret = 1;
				goto done;
			}

			num++;
		}
	}

	sk_for_each_bound(sk, &tbl->mc_list) {
		if (sk_hashed(sk))
			continue;
		if (!net_eq(sock_net(sk), net))
			continue;
		if (num < s_num) {
			num++;
			continue;
		}

		if (sk_diag_fill(sk, skb, req,
				 NETLINK_CB(cb->skb).portid,
				 cb->nlh->nlmsg_seq,
				 NLM_F_MULTI,
				 sock_i_ino(sk)) < 0) {
			ret = 1;
			goto done;
		}
		num++;
	}
done:
	cb->args[0] = num;
	cb->args[1] = protocol;

	return ret;
}

static int netlink_diag_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct netlink_diag_req *req;
	int s_num = cb->args[0];

	req = nlmsg_data(cb->nlh);

	read_lock(&nl_table_lock);

	if (req->sdiag_protocol == NDIAG_PROTO_ALL) {
		int i;

		for (i = cb->args[1]; i < MAX_LINKS; i++) {
			if (__netlink_diag_dump(skb, cb, i, s_num))
				break;
			s_num = 0;
		}
	} else {
		if (req->sdiag_protocol >= MAX_LINKS) {
			read_unlock(&nl_table_lock);
			return -ENOENT;
		}

		__netlink_diag_dump(skb, cb, req->sdiag_protocol, s_num);
	}

	read_unlock(&nl_table_lock);

	return skb->len;
}

static int netlink_diag_handler_dump(struct sk_buff *skb, struct nlmsghdr *h)
{
	int hdrlen = sizeof(struct netlink_diag_req);
	struct net *net = sock_net(skb->sk);

	if (nlmsg_len(h) < hdrlen)
		return -EINVAL;

	if (h->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = netlink_diag_dump,
		};
		return netlink_dump_start(net->diag_nlsk, skb, h, &c);
	} else
		return -EOPNOTSUPP;
}

static const struct sock_diag_handler netlink_diag_handler = {
	.family = AF_NETLINK,
	.dump = netlink_diag_handler_dump,
};

static int __init netlink_diag_init(void)
{
	return sock_diag_register(&netlink_diag_handler);
}

static void __exit netlink_diag_exit(void)
{
	sock_diag_unregister(&netlink_diag_handler);
}

module_init(netlink_diag_init);
module_exit(netlink_diag_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 16 /* AF_NETLINK */);
