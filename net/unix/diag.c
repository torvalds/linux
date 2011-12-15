#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/sock_diag.h>
#include <linux/unix_diag.h>
#include <linux/skbuff.h>
#include <net/netlink.h>
#include <net/af_unix.h>
#include <net/tcp_states.h>

#define UNIX_DIAG_PUT(skb, attrtype, attrlen) \
	RTA_DATA(__RTA_PUT(skb, attrtype, attrlen))

static int unix_diag_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	return 0;
}

static int unix_diag_get_exact(struct sk_buff *in_skb,
			       const struct nlmsghdr *nlh,
			       struct unix_diag_req *req)
{
	return -EAFNOSUPPORT;
}

static int unix_diag_handler_dump(struct sk_buff *skb, struct nlmsghdr *h)
{
	int hdrlen = sizeof(struct unix_diag_req);

	if (nlmsg_len(h) < hdrlen)
		return -EINVAL;

	if (h->nlmsg_flags & NLM_F_DUMP)
		return netlink_dump_start(sock_diag_nlsk, skb, h,
					  unix_diag_dump, NULL, 0);
	else
		return unix_diag_get_exact(skb, h, (struct unix_diag_req *)NLMSG_DATA(h));
}

static struct sock_diag_handler unix_diag_handler = {
	.family = AF_UNIX,
	.dump = unix_diag_handler_dump,
};

static int __init unix_diag_init(void)
{
	return sock_diag_register(&unix_diag_handler);
}

static void __exit unix_diag_exit(void)
{
	sock_diag_unregister(&unix_diag_handler);
}

module_init(unix_diag_init);
module_exit(unix_diag_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 1 /* AF_LOCAL */);
