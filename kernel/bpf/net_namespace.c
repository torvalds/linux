// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <linux/filter.h>
#include <net/net_namespace.h>

/*
 * Functions to manage BPF programs attached to netns
 */

/* Protects updates to netns_bpf */
DEFINE_MUTEX(netns_bpf_mutex);

int netns_bpf_prog_query(const union bpf_attr *attr,
			 union bpf_attr __user *uattr)
{
	__u32 __user *prog_ids = u64_to_user_ptr(attr->query.prog_ids);
	u32 prog_id, prog_cnt = 0, flags = 0;
	enum netns_bpf_attach_type type;
	struct bpf_prog *attached;
	struct net *net;

	if (attr->query.query_flags)
		return -EINVAL;

	type = to_netns_bpf_attach_type(attr->query.attach_type);
	if (type < 0)
		return -EINVAL;

	net = get_net_ns_by_fd(attr->query.target_fd);
	if (IS_ERR(net))
		return PTR_ERR(net);

	rcu_read_lock();
	attached = rcu_dereference(net->bpf.progs[type]);
	if (attached) {
		prog_cnt = 1;
		prog_id = attached->aux->id;
	}
	rcu_read_unlock();

	put_net(net);

	if (copy_to_user(&uattr->query.attach_flags, &flags, sizeof(flags)))
		return -EFAULT;
	if (copy_to_user(&uattr->query.prog_cnt, &prog_cnt, sizeof(prog_cnt)))
		return -EFAULT;

	if (!attr->query.prog_cnt || !prog_ids || !prog_cnt)
		return 0;

	if (copy_to_user(prog_ids, &prog_id, sizeof(u32)))
		return -EFAULT;

	return 0;
}

int netns_bpf_prog_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	enum netns_bpf_attach_type type;
	struct net *net;
	int ret;

	type = to_netns_bpf_attach_type(attr->attach_type);
	if (type < 0)
		return -EINVAL;

	net = current->nsproxy->net_ns;
	mutex_lock(&netns_bpf_mutex);
	switch (type) {
	case NETNS_BPF_FLOW_DISSECTOR:
		ret = flow_dissector_bpf_prog_attach(net, prog);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&netns_bpf_mutex);

	return ret;
}

/* Must be called with netns_bpf_mutex held. */
static int __netns_bpf_prog_detach(struct net *net,
				   enum netns_bpf_attach_type type)
{
	struct bpf_prog *attached;

	attached = rcu_dereference_protected(net->bpf.progs[type],
					     lockdep_is_held(&netns_bpf_mutex));
	if (!attached)
		return -ENOENT;
	RCU_INIT_POINTER(net->bpf.progs[type], NULL);
	bpf_prog_put(attached);
	return 0;
}

int netns_bpf_prog_detach(const union bpf_attr *attr)
{
	enum netns_bpf_attach_type type;
	int ret;

	type = to_netns_bpf_attach_type(attr->attach_type);
	if (type < 0)
		return -EINVAL;

	mutex_lock(&netns_bpf_mutex);
	ret = __netns_bpf_prog_detach(current->nsproxy->net_ns, type);
	mutex_unlock(&netns_bpf_mutex);

	return ret;
}

static void __net_exit netns_bpf_pernet_pre_exit(struct net *net)
{
	enum netns_bpf_attach_type type;

	mutex_lock(&netns_bpf_mutex);
	for (type = 0; type < MAX_NETNS_BPF_ATTACH_TYPE; type++)
		__netns_bpf_prog_detach(net, type);
	mutex_unlock(&netns_bpf_mutex);
}

static struct pernet_operations netns_bpf_pernet_ops __net_initdata = {
	.pre_exit = netns_bpf_pernet_pre_exit,
};

static int __init netns_bpf_init(void)
{
	return register_pernet_subsys(&netns_bpf_pernet_ops);
}

subsys_initcall(netns_bpf_init);
