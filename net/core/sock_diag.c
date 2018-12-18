/* License: GPL */

#include <linux/mutex.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <net/netlink.h>
#include <net/net_namespace.h>
#include <linux/module.h>
#include <net/sock.h>
#include <linux/kernel.h>
#include <linux/tcp.h>
#include <linux/workqueue.h>
#include <linux/nospec.h>

#include <linux/inet_diag.h>
#include <linux/sock_diag.h>

static const struct sock_diag_handler *sock_diag_handlers[AF_MAX];
static int (*inet_rcv_compat)(struct sk_buff *skb, struct nlmsghdr *nlh);
static DEFINE_MUTEX(sock_diag_table_mutex);
static struct workqueue_struct *broadcast_wq;

u64 sock_gen_cookie(struct sock *sk)
{
	while (1) {
		u64 res = atomic64_read(&sk->sk_cookie);

		if (res)
			return res;
		res = atomic64_inc_return(&sock_net(sk)->cookie_gen);
		atomic64_cmpxchg(&sk->sk_cookie, 0, res);
	}
}

int sock_diag_check_cookie(struct sock *sk, const __u32 *cookie)
{
	u64 res;

	if (cookie[0] == INET_DIAG_NOCOOKIE && cookie[1] == INET_DIAG_NOCOOKIE)
		return 0;

	res = sock_gen_cookie(sk);
	if ((u32)res != cookie[0] || (u32)(res >> 32) != cookie[1])
		return -ESTALE;

	return 0;
}
EXPORT_SYMBOL_GPL(sock_diag_check_cookie);

void sock_diag_save_cookie(struct sock *sk, __u32 *cookie)
{
	u64 res = sock_gen_cookie(sk);

	cookie[0] = (u32)res;
	cookie[1] = (u32)(res >> 32);
}
EXPORT_SYMBOL_GPL(sock_diag_save_cookie);

int sock_diag_put_meminfo(struct sock *sk, struct sk_buff *skb, int attrtype)
{
	u32 mem[SK_MEMINFO_VARS];

	sk_get_meminfo(sk, mem);

	return nla_put(skb, attrtype, sizeof(mem), &mem);
}
EXPORT_SYMBOL_GPL(sock_diag_put_meminfo);

int sock_diag_put_filterinfo(bool may_report_filterinfo, struct sock *sk,
			     struct sk_buff *skb, int attrtype)
{
	struct sock_fprog_kern *fprog;
	struct sk_filter *filter;
	struct nlattr *attr;
	unsigned int flen;
	int err = 0;

	if (!may_report_filterinfo) {
		nla_reserve(skb, attrtype, 0);
		return 0;
	}

	rcu_read_lock();
	filter = rcu_dereference(sk->sk_filter);
	if (!filter)
		goto out;

	fprog = filter->prog->orig_prog;
	if (!fprog)
		goto out;

	flen = bpf_classic_proglen(fprog);

	attr = nla_reserve(skb, attrtype, flen);
	if (attr == NULL) {
		err = -EMSGSIZE;
		goto out;
	}

	memcpy(nla_data(attr), fprog->filter, flen);
out:
	rcu_read_unlock();
	return err;
}
EXPORT_SYMBOL(sock_diag_put_filterinfo);

struct broadcast_sk {
	struct sock *sk;
	struct work_struct work;
};

static size_t sock_diag_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct inet_diag_msg)
	       + nla_total_size(sizeof(u8)) /* INET_DIAG_PROTOCOL */
	       + nla_total_size_64bit(sizeof(struct tcp_info))); /* INET_DIAG_INFO */
}

static void sock_diag_broadcast_destroy_work(struct work_struct *work)
{
	struct broadcast_sk *bsk =
		container_of(work, struct broadcast_sk, work);
	struct sock *sk = bsk->sk;
	const struct sock_diag_handler *hndl;
	struct sk_buff *skb;
	const enum sknetlink_groups group = sock_diag_destroy_group(sk);
	int err = -1;

	WARN_ON(group == SKNLGRP_NONE);

	skb = nlmsg_new(sock_diag_nlmsg_size(), GFP_KERNEL);
	if (!skb)
		goto out;

	mutex_lock(&sock_diag_table_mutex);
	hndl = sock_diag_handlers[sk->sk_family];
	if (hndl && hndl->get_info)
		err = hndl->get_info(skb, sk);
	mutex_unlock(&sock_diag_table_mutex);

	if (!err)
		nlmsg_multicast(sock_net(sk)->diag_nlsk, skb, 0, group,
				GFP_KERNEL);
	else
		kfree_skb(skb);
out:
	sk_destruct(sk);
	kfree(bsk);
}

void sock_diag_broadcast_destroy(struct sock *sk)
{
	/* Note, this function is often called from an interrupt context. */
	struct broadcast_sk *bsk =
		kmalloc(sizeof(struct broadcast_sk), GFP_ATOMIC);
	if (!bsk)
		return sk_destruct(sk);
	bsk->sk = sk;
	INIT_WORK(&bsk->work, sock_diag_broadcast_destroy_work);
	queue_work(broadcast_wq, &bsk->work);
}

void sock_diag_register_inet_compat(int (*fn)(struct sk_buff *skb, struct nlmsghdr *nlh))
{
	mutex_lock(&sock_diag_table_mutex);
	inet_rcv_compat = fn;
	mutex_unlock(&sock_diag_table_mutex);
}
EXPORT_SYMBOL_GPL(sock_diag_register_inet_compat);

void sock_diag_unregister_inet_compat(int (*fn)(struct sk_buff *skb, struct nlmsghdr *nlh))
{
	mutex_lock(&sock_diag_table_mutex);
	inet_rcv_compat = NULL;
	mutex_unlock(&sock_diag_table_mutex);
}
EXPORT_SYMBOL_GPL(sock_diag_unregister_inet_compat);

int sock_diag_register(const struct sock_diag_handler *hndl)
{
	int err = 0;

	if (hndl->family >= AF_MAX)
		return -EINVAL;

	mutex_lock(&sock_diag_table_mutex);
	if (sock_diag_handlers[hndl->family])
		err = -EBUSY;
	else
		sock_diag_handlers[hndl->family] = hndl;
	mutex_unlock(&sock_diag_table_mutex);

	return err;
}
EXPORT_SYMBOL_GPL(sock_diag_register);

void sock_diag_unregister(const struct sock_diag_handler *hnld)
{
	int family = hnld->family;

	if (family >= AF_MAX)
		return;

	mutex_lock(&sock_diag_table_mutex);
	BUG_ON(sock_diag_handlers[family] != hnld);
	sock_diag_handlers[family] = NULL;
	mutex_unlock(&sock_diag_table_mutex);
}
EXPORT_SYMBOL_GPL(sock_diag_unregister);

static int __sock_diag_cmd(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	int err;
	struct sock_diag_req *req = nlmsg_data(nlh);
	const struct sock_diag_handler *hndl;

	if (nlmsg_len(nlh) < sizeof(*req))
		return -EINVAL;

	if (req->sdiag_family >= AF_MAX)
		return -EINVAL;
	req->sdiag_family = array_index_nospec(req->sdiag_family, AF_MAX);

	if (sock_diag_handlers[req->sdiag_family] == NULL)
		sock_load_diag_module(req->sdiag_family, 0);

	mutex_lock(&sock_diag_table_mutex);
	hndl = sock_diag_handlers[req->sdiag_family];
	if (hndl == NULL)
		err = -ENOENT;
	else if (nlh->nlmsg_type == SOCK_DIAG_BY_FAMILY)
		err = hndl->dump(skb, nlh);
	else if (nlh->nlmsg_type == SOCK_DESTROY && hndl->destroy)
		err = hndl->destroy(skb, nlh);
	else
		err = -EOPNOTSUPP;
	mutex_unlock(&sock_diag_table_mutex);

	return err;
}

static int sock_diag_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh,
			     struct netlink_ext_ack *extack)
{
	int ret;

	switch (nlh->nlmsg_type) {
	case TCPDIAG_GETSOCK:
	case DCCPDIAG_GETSOCK:
		if (inet_rcv_compat == NULL)
			sock_load_diag_module(AF_INET, 0);

		mutex_lock(&sock_diag_table_mutex);
		if (inet_rcv_compat != NULL)
			ret = inet_rcv_compat(skb, nlh);
		else
			ret = -EOPNOTSUPP;
		mutex_unlock(&sock_diag_table_mutex);

		return ret;
	case SOCK_DIAG_BY_FAMILY:
	case SOCK_DESTROY:
		return __sock_diag_cmd(skb, nlh);
	default:
		return -EINVAL;
	}
}

static DEFINE_MUTEX(sock_diag_mutex);

static void sock_diag_rcv(struct sk_buff *skb)
{
	mutex_lock(&sock_diag_mutex);
	netlink_rcv_skb(skb, &sock_diag_rcv_msg);
	mutex_unlock(&sock_diag_mutex);
}

static int sock_diag_bind(struct net *net, int group)
{
	switch (group) {
	case SKNLGRP_INET_TCP_DESTROY:
	case SKNLGRP_INET_UDP_DESTROY:
		if (!sock_diag_handlers[AF_INET])
			sock_load_diag_module(AF_INET, 0);
		break;
	case SKNLGRP_INET6_TCP_DESTROY:
	case SKNLGRP_INET6_UDP_DESTROY:
		if (!sock_diag_handlers[AF_INET6])
			sock_load_diag_module(AF_INET6, 0);
		break;
	}
	return 0;
}

int sock_diag_destroy(struct sock *sk, int err)
{
	if (!ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	if (!sk->sk_prot->diag_destroy)
		return -EOPNOTSUPP;

	return sk->sk_prot->diag_destroy(sk, err);
}
EXPORT_SYMBOL_GPL(sock_diag_destroy);

static int __net_init diag_net_init(struct net *net)
{
	struct netlink_kernel_cfg cfg = {
		.groups	= SKNLGRP_MAX,
		.input	= sock_diag_rcv,
		.bind	= sock_diag_bind,
		.flags	= NL_CFG_F_NONROOT_RECV,
	};

	net->diag_nlsk = netlink_kernel_create(net, NETLINK_SOCK_DIAG, &cfg);
	return net->diag_nlsk == NULL ? -ENOMEM : 0;
}

static void __net_exit diag_net_exit(struct net *net)
{
	netlink_kernel_release(net->diag_nlsk);
	net->diag_nlsk = NULL;
}

static struct pernet_operations diag_net_ops = {
	.init = diag_net_init,
	.exit = diag_net_exit,
};

static int __init sock_diag_init(void)
{
	broadcast_wq = alloc_workqueue("sock_diag_events", 0, 0);
	BUG_ON(!broadcast_wq);
	return register_pernet_subsys(&diag_net_ops);
}
device_initcall(sock_diag_init);
