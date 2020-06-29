// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <linux/filter.h>
#include <net/net_namespace.h>

/*
 * Functions to manage BPF programs attached to netns
 */

struct bpf_netns_link {
	struct bpf_link	link;
	enum bpf_attach_type type;
	enum netns_bpf_attach_type netns_type;

	/* We don't hold a ref to net in order to auto-detach the link
	 * when netns is going away. Instead we rely on pernet
	 * pre_exit callback to clear this pointer. Must be accessed
	 * with netns_bpf_mutex held.
	 */
	struct net *net;
};

/* Protects updates to netns_bpf */
DEFINE_MUTEX(netns_bpf_mutex);

/* Must be called with netns_bpf_mutex held. */
static void __net_exit bpf_netns_link_auto_detach(struct bpf_link *link)
{
	struct bpf_netns_link *net_link =
		container_of(link, struct bpf_netns_link, link);

	net_link->net = NULL;
}

static void bpf_netns_link_release(struct bpf_link *link)
{
	struct bpf_netns_link *net_link =
		container_of(link, struct bpf_netns_link, link);
	enum netns_bpf_attach_type type = net_link->netns_type;
	struct net *net;

	/* Link auto-detached by dying netns. */
	if (!net_link->net)
		return;

	mutex_lock(&netns_bpf_mutex);

	/* Recheck after potential sleep. We can race with cleanup_net
	 * here, but if we see a non-NULL struct net pointer pre_exit
	 * has not happened yet and will block on netns_bpf_mutex.
	 */
	net = net_link->net;
	if (!net)
		goto out_unlock;

	net->bpf.links[type] = NULL;
	RCU_INIT_POINTER(net->bpf.progs[type], NULL);

out_unlock:
	mutex_unlock(&netns_bpf_mutex);
}

static void bpf_netns_link_dealloc(struct bpf_link *link)
{
	struct bpf_netns_link *net_link =
		container_of(link, struct bpf_netns_link, link);

	kfree(net_link);
}

static int bpf_netns_link_update_prog(struct bpf_link *link,
				      struct bpf_prog *new_prog,
				      struct bpf_prog *old_prog)
{
	struct bpf_netns_link *net_link =
		container_of(link, struct bpf_netns_link, link);
	enum netns_bpf_attach_type type = net_link->netns_type;
	struct net *net;
	int ret = 0;

	if (old_prog && old_prog != link->prog)
		return -EPERM;
	if (new_prog->type != link->prog->type)
		return -EINVAL;

	mutex_lock(&netns_bpf_mutex);

	net = net_link->net;
	if (!net || !check_net(net)) {
		/* Link auto-detached or netns dying */
		ret = -ENOLINK;
		goto out_unlock;
	}

	old_prog = xchg(&link->prog, new_prog);
	rcu_assign_pointer(net->bpf.progs[type], new_prog);
	bpf_prog_put(old_prog);

out_unlock:
	mutex_unlock(&netns_bpf_mutex);
	return ret;
}

static int bpf_netns_link_fill_info(const struct bpf_link *link,
				    struct bpf_link_info *info)
{
	const struct bpf_netns_link *net_link =
		container_of(link, struct bpf_netns_link, link);
	unsigned int inum = 0;
	struct net *net;

	mutex_lock(&netns_bpf_mutex);
	net = net_link->net;
	if (net && check_net(net))
		inum = net->ns.inum;
	mutex_unlock(&netns_bpf_mutex);

	info->netns.netns_ino = inum;
	info->netns.attach_type = net_link->type;
	return 0;
}

static void bpf_netns_link_show_fdinfo(const struct bpf_link *link,
				       struct seq_file *seq)
{
	struct bpf_link_info info = {};

	bpf_netns_link_fill_info(link, &info);
	seq_printf(seq,
		   "netns_ino:\t%u\n"
		   "attach_type:\t%u\n",
		   info.netns.netns_ino,
		   info.netns.attach_type);
}

static const struct bpf_link_ops bpf_netns_link_ops = {
	.release = bpf_netns_link_release,
	.dealloc = bpf_netns_link_dealloc,
	.update_prog = bpf_netns_link_update_prog,
	.fill_link_info = bpf_netns_link_fill_info,
	.show_fdinfo = bpf_netns_link_show_fdinfo,
};

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

	/* Attaching prog directly is not compatible with links */
	if (net->bpf.links[type]) {
		ret = -EEXIST;
		goto out_unlock;
	}

	switch (type) {
	case NETNS_BPF_FLOW_DISSECTOR:
		ret = flow_dissector_bpf_prog_attach(net, prog);
		break;
	default:
		ret = -EINVAL;
		break;
	}
out_unlock:
	mutex_unlock(&netns_bpf_mutex);

	return ret;
}

/* Must be called with netns_bpf_mutex held. */
static int __netns_bpf_prog_detach(struct net *net,
				   enum netns_bpf_attach_type type)
{
	struct bpf_prog *attached;

	/* Progs attached via links cannot be detached */
	if (net->bpf.links[type])
		return -EINVAL;

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

static int netns_bpf_link_attach(struct net *net, struct bpf_link *link,
				 enum netns_bpf_attach_type type)
{
	struct bpf_prog *prog;
	int err;

	mutex_lock(&netns_bpf_mutex);

	/* Allow attaching only one prog or link for now */
	if (net->bpf.links[type]) {
		err = -E2BIG;
		goto out_unlock;
	}
	/* Links are not compatible with attaching prog directly */
	prog = rcu_dereference_protected(net->bpf.progs[type],
					 lockdep_is_held(&netns_bpf_mutex));
	if (prog) {
		err = -EEXIST;
		goto out_unlock;
	}

	switch (type) {
	case NETNS_BPF_FLOW_DISSECTOR:
		err = flow_dissector_bpf_prog_attach(net, link->prog);
		break;
	default:
		err = -EINVAL;
		break;
	}
	if (err)
		goto out_unlock;

	net->bpf.links[type] = link;

out_unlock:
	mutex_unlock(&netns_bpf_mutex);
	return err;
}

int netns_bpf_link_create(const union bpf_attr *attr, struct bpf_prog *prog)
{
	enum netns_bpf_attach_type netns_type;
	struct bpf_link_primer link_primer;
	struct bpf_netns_link *net_link;
	enum bpf_attach_type type;
	struct net *net;
	int err;

	if (attr->link_create.flags)
		return -EINVAL;

	type = attr->link_create.attach_type;
	netns_type = to_netns_bpf_attach_type(type);
	if (netns_type < 0)
		return -EINVAL;

	net = get_net_ns_by_fd(attr->link_create.target_fd);
	if (IS_ERR(net))
		return PTR_ERR(net);

	net_link = kzalloc(sizeof(*net_link), GFP_USER);
	if (!net_link) {
		err = -ENOMEM;
		goto out_put_net;
	}
	bpf_link_init(&net_link->link, BPF_LINK_TYPE_NETNS,
		      &bpf_netns_link_ops, prog);
	net_link->net = net;
	net_link->type = type;
	net_link->netns_type = netns_type;

	err = bpf_link_prime(&net_link->link, &link_primer);
	if (err) {
		kfree(net_link);
		goto out_put_net;
	}

	err = netns_bpf_link_attach(net, &net_link->link, netns_type);
	if (err) {
		bpf_link_cleanup(&link_primer);
		goto out_put_net;
	}

	put_net(net);
	return bpf_link_settle(&link_primer);

out_put_net:
	put_net(net);
	return err;
}

static void __net_exit netns_bpf_pernet_pre_exit(struct net *net)
{
	enum netns_bpf_attach_type type;
	struct bpf_link *link;

	mutex_lock(&netns_bpf_mutex);
	for (type = 0; type < MAX_NETNS_BPF_ATTACH_TYPE; type++) {
		link = net->bpf.links[type];
		if (link)
			bpf_netns_link_auto_detach(link);
		else
			__netns_bpf_prog_detach(net, type);
	}
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
