// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Isovalent */

#include <linux/bpf.h>
#include <linux/bpf_mprog.h>
#include <linux/netdevice.h>

#include <net/tcx.h>

int tcx_prog_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	bool created, ingress = attr->attach_type == BPF_TCX_INGRESS;
	struct net *net = current->nsproxy->net_ns;
	struct bpf_mprog_entry *entry, *entry_new;
	struct bpf_prog *replace_prog = NULL;
	struct net_device *dev;
	int ret;

	rtnl_lock();
	dev = __dev_get_by_index(net, attr->target_ifindex);
	if (!dev) {
		ret = -ENODEV;
		goto out;
	}
	if (attr->attach_flags & BPF_F_REPLACE) {
		replace_prog = bpf_prog_get_type(attr->replace_bpf_fd,
						 prog->type);
		if (IS_ERR(replace_prog)) {
			ret = PTR_ERR(replace_prog);
			replace_prog = NULL;
			goto out;
		}
	}
	entry = tcx_entry_fetch_or_create(dev, ingress, &created);
	if (!entry) {
		ret = -ENOMEM;
		goto out;
	}
	ret = bpf_mprog_attach(entry, &entry_new, prog, NULL, replace_prog,
			       attr->attach_flags, attr->relative_fd,
			       attr->expected_revision);
	if (!ret) {
		if (entry != entry_new) {
			tcx_entry_update(dev, entry_new, ingress);
			tcx_entry_sync();
			tcx_skeys_inc(ingress);
		}
		bpf_mprog_commit(entry);
	} else if (created) {
		tcx_entry_free(entry);
	}
out:
	if (replace_prog)
		bpf_prog_put(replace_prog);
	rtnl_unlock();
	return ret;
}

int tcx_prog_detach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	bool ingress = attr->attach_type == BPF_TCX_INGRESS;
	struct net *net = current->nsproxy->net_ns;
	struct bpf_mprog_entry *entry, *entry_new;
	struct net_device *dev;
	int ret;

	rtnl_lock();
	dev = __dev_get_by_index(net, attr->target_ifindex);
	if (!dev) {
		ret = -ENODEV;
		goto out;
	}
	entry = tcx_entry_fetch(dev, ingress);
	if (!entry) {
		ret = -ENOENT;
		goto out;
	}
	ret = bpf_mprog_detach(entry, &entry_new, prog, NULL, attr->attach_flags,
			       attr->relative_fd, attr->expected_revision);
	if (!ret) {
		if (!tcx_entry_is_active(entry_new))
			entry_new = NULL;
		tcx_entry_update(dev, entry_new, ingress);
		tcx_entry_sync();
		tcx_skeys_dec(ingress);
		bpf_mprog_commit(entry);
		if (!entry_new)
			tcx_entry_free(entry);
	}
out:
	rtnl_unlock();
	return ret;
}

void tcx_uninstall(struct net_device *dev, bool ingress)
{
	struct bpf_mprog_entry *entry, *entry_new = NULL;
	struct bpf_tuple tuple = {};
	struct bpf_mprog_fp *fp;
	struct bpf_mprog_cp *cp;
	bool active;

	entry = tcx_entry_fetch(dev, ingress);
	if (!entry)
		return;
	active = tcx_entry(entry)->miniq_active;
	if (active)
		bpf_mprog_clear_all(entry, &entry_new);
	tcx_entry_update(dev, entry_new, ingress);
	tcx_entry_sync();
	bpf_mprog_foreach_tuple(entry, fp, cp, tuple) {
		if (tuple.link)
			tcx_link(tuple.link)->dev = NULL;
		else
			bpf_prog_put(tuple.prog);
		tcx_skeys_dec(ingress);
	}
	if (!active)
		tcx_entry_free(entry);
}

int tcx_prog_query(const union bpf_attr *attr, union bpf_attr __user *uattr)
{
	bool ingress = attr->query.attach_type == BPF_TCX_INGRESS;
	struct net *net = current->nsproxy->net_ns;
	struct net_device *dev;
	int ret;

	rtnl_lock();
	dev = __dev_get_by_index(net, attr->query.target_ifindex);
	if (!dev) {
		ret = -ENODEV;
		goto out;
	}
	ret = bpf_mprog_query(attr, uattr, tcx_entry_fetch(dev, ingress));
out:
	rtnl_unlock();
	return ret;
}

static int tcx_link_prog_attach(struct bpf_link *link, u32 flags, u32 id_or_fd,
				u64 revision)
{
	struct tcx_link *tcx = tcx_link(link);
	bool created, ingress = tcx->location == BPF_TCX_INGRESS;
	struct bpf_mprog_entry *entry, *entry_new;
	struct net_device *dev = tcx->dev;
	int ret;

	ASSERT_RTNL();
	entry = tcx_entry_fetch_or_create(dev, ingress, &created);
	if (!entry)
		return -ENOMEM;
	ret = bpf_mprog_attach(entry, &entry_new, link->prog, link, NULL, flags,
			       id_or_fd, revision);
	if (!ret) {
		if (entry != entry_new) {
			tcx_entry_update(dev, entry_new, ingress);
			tcx_entry_sync();
			tcx_skeys_inc(ingress);
		}
		bpf_mprog_commit(entry);
	} else if (created) {
		tcx_entry_free(entry);
	}
	return ret;
}

static void tcx_link_release(struct bpf_link *link)
{
	struct tcx_link *tcx = tcx_link(link);
	bool ingress = tcx->location == BPF_TCX_INGRESS;
	struct bpf_mprog_entry *entry, *entry_new;
	struct net_device *dev;
	int ret = 0;

	rtnl_lock();
	dev = tcx->dev;
	if (!dev)
		goto out;
	entry = tcx_entry_fetch(dev, ingress);
	if (!entry) {
		ret = -ENOENT;
		goto out;
	}
	ret = bpf_mprog_detach(entry, &entry_new, link->prog, link, 0, 0, 0);
	if (!ret) {
		if (!tcx_entry_is_active(entry_new))
			entry_new = NULL;
		tcx_entry_update(dev, entry_new, ingress);
		tcx_entry_sync();
		tcx_skeys_dec(ingress);
		bpf_mprog_commit(entry);
		if (!entry_new)
			tcx_entry_free(entry);
		tcx->dev = NULL;
	}
out:
	WARN_ON_ONCE(ret);
	rtnl_unlock();
}

static int tcx_link_update(struct bpf_link *link, struct bpf_prog *nprog,
			   struct bpf_prog *oprog)
{
	struct tcx_link *tcx = tcx_link(link);
	bool ingress = tcx->location == BPF_TCX_INGRESS;
	struct bpf_mprog_entry *entry, *entry_new;
	struct net_device *dev;
	int ret = 0;

	rtnl_lock();
	dev = tcx->dev;
	if (!dev) {
		ret = -ENOLINK;
		goto out;
	}
	if (oprog && link->prog != oprog) {
		ret = -EPERM;
		goto out;
	}
	oprog = link->prog;
	if (oprog == nprog) {
		bpf_prog_put(nprog);
		goto out;
	}
	entry = tcx_entry_fetch(dev, ingress);
	if (!entry) {
		ret = -ENOENT;
		goto out;
	}
	ret = bpf_mprog_attach(entry, &entry_new, nprog, link, oprog,
			       BPF_F_REPLACE | BPF_F_ID,
			       link->prog->aux->id, 0);
	if (!ret) {
		WARN_ON_ONCE(entry != entry_new);
		oprog = xchg(&link->prog, nprog);
		bpf_prog_put(oprog);
		bpf_mprog_commit(entry);
	}
out:
	rtnl_unlock();
	return ret;
}

static void tcx_link_dealloc(struct bpf_link *link)
{
	kfree(tcx_link(link));
}

static void tcx_link_fdinfo(const struct bpf_link *link, struct seq_file *seq)
{
	const struct tcx_link *tcx = tcx_link(link);
	u32 ifindex = 0;

	rtnl_lock();
	if (tcx->dev)
		ifindex = tcx->dev->ifindex;
	rtnl_unlock();

	seq_printf(seq, "ifindex:\t%u\n", ifindex);
	seq_printf(seq, "attach_type:\t%u (%s)\n",
		   tcx->location,
		   tcx->location == BPF_TCX_INGRESS ? "ingress" : "egress");
}

static int tcx_link_fill_info(const struct bpf_link *link,
			      struct bpf_link_info *info)
{
	const struct tcx_link *tcx = tcx_link(link);
	u32 ifindex = 0;

	rtnl_lock();
	if (tcx->dev)
		ifindex = tcx->dev->ifindex;
	rtnl_unlock();

	info->tcx.ifindex = ifindex;
	info->tcx.attach_type = tcx->location;
	return 0;
}

static int tcx_link_detach(struct bpf_link *link)
{
	tcx_link_release(link);
	return 0;
}

static const struct bpf_link_ops tcx_link_lops = {
	.release	= tcx_link_release,
	.detach		= tcx_link_detach,
	.dealloc	= tcx_link_dealloc,
	.update_prog	= tcx_link_update,
	.show_fdinfo	= tcx_link_fdinfo,
	.fill_link_info	= tcx_link_fill_info,
};

static int tcx_link_init(struct tcx_link *tcx,
			 struct bpf_link_primer *link_primer,
			 const union bpf_attr *attr,
			 struct net_device *dev,
			 struct bpf_prog *prog)
{
	bpf_link_init(&tcx->link, BPF_LINK_TYPE_TCX, &tcx_link_lops, prog);
	tcx->location = attr->link_create.attach_type;
	tcx->dev = dev;
	return bpf_link_prime(&tcx->link, link_primer);
}

int tcx_link_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	struct net *net = current->nsproxy->net_ns;
	struct bpf_link_primer link_primer;
	struct net_device *dev;
	struct tcx_link *tcx;
	int ret;

	rtnl_lock();
	dev = __dev_get_by_index(net, attr->link_create.target_ifindex);
	if (!dev) {
		ret = -ENODEV;
		goto out;
	}
	tcx = kzalloc(sizeof(*tcx), GFP_USER);
	if (!tcx) {
		ret = -ENOMEM;
		goto out;
	}
	ret = tcx_link_init(tcx, &link_primer, attr, dev, prog);
	if (ret) {
		kfree(tcx);
		goto out;
	}
	ret = tcx_link_prog_attach(&tcx->link, attr->link_create.flags,
				   attr->link_create.tcx.relative_fd,
				   attr->link_create.tcx.expected_revision);
	if (ret) {
		tcx->dev = NULL;
		bpf_link_cleanup(&link_primer);
		goto out;
	}
	ret = bpf_link_settle(&link_primer);
out:
	rtnl_unlock();
	return ret;
}
