/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree.
 *
 * THE COPYRIGHT HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM "AS IS"
 * WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE
 * OF THE PROGRAM IS WITH YOU. SHOULD THE PROGRAM PROVE DEFECTIVE, YOU ASSUME
 * THE COST OF ALL NECESSARY SERVICING, REPAIR OR CORRECTION.
 */

#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/bug.h>
#include <linux/kdev_t.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/printk.h>
#include <linux/proc_ns.h>
#include <linux/rtnetlink.h>
#include <linux/rwsem.h>

/* Protects bpf_prog_offload_devs and offload members of all progs.
 * RTNL lock cannot be taken when holding this lock.
 */
static DECLARE_RWSEM(bpf_devs_lock);
static LIST_HEAD(bpf_prog_offload_devs);

int bpf_prog_offload_init(struct bpf_prog *prog, union bpf_attr *attr)
{
	struct bpf_dev_offload *offload;

	if (attr->prog_type != BPF_PROG_TYPE_SCHED_CLS &&
	    attr->prog_type != BPF_PROG_TYPE_XDP)
		return -EINVAL;

	if (attr->prog_flags)
		return -EINVAL;

	offload = kzalloc(sizeof(*offload), GFP_USER);
	if (!offload)
		return -ENOMEM;

	offload->prog = prog;

	offload->netdev = dev_get_by_index(current->nsproxy->net_ns,
					   attr->prog_ifindex);
	if (!offload->netdev)
		goto err_free;

	down_write(&bpf_devs_lock);
	if (offload->netdev->reg_state != NETREG_REGISTERED)
		goto err_unlock;
	prog->aux->offload = offload;
	list_add_tail(&offload->offloads, &bpf_prog_offload_devs);
	dev_put(offload->netdev);
	up_write(&bpf_devs_lock);

	return 0;
err_unlock:
	up_write(&bpf_devs_lock);
	dev_put(offload->netdev);
err_free:
	kfree(offload);
	return -EINVAL;
}

static int __bpf_offload_ndo(struct bpf_prog *prog, enum bpf_netdev_command cmd,
			     struct netdev_bpf *data)
{
	struct bpf_dev_offload *offload = prog->aux->offload;
	struct net_device *netdev;

	ASSERT_RTNL();

	if (!offload)
		return -ENODEV;
	netdev = offload->netdev;
	if (!netdev->netdev_ops->ndo_bpf)
		return -EOPNOTSUPP;

	data->command = cmd;

	return netdev->netdev_ops->ndo_bpf(netdev, data);
}

int bpf_prog_offload_verifier_prep(struct bpf_verifier_env *env)
{
	struct netdev_bpf data = {};
	int err;

	data.verifier.prog = env->prog;

	rtnl_lock();
	err = __bpf_offload_ndo(env->prog, BPF_OFFLOAD_VERIFIER_PREP, &data);
	if (err)
		goto exit_unlock;

	env->prog->aux->offload->dev_ops = data.verifier.ops;
	env->prog->aux->offload->dev_state = true;
exit_unlock:
	rtnl_unlock();
	return err;
}

int bpf_prog_offload_verify_insn(struct bpf_verifier_env *env,
				 int insn_idx, int prev_insn_idx)
{
	struct bpf_dev_offload *offload;
	int ret = -ENODEV;

	down_read(&bpf_devs_lock);
	offload = env->prog->aux->offload;
	if (offload)
		ret = offload->dev_ops->insn_hook(env, insn_idx, prev_insn_idx);
	up_read(&bpf_devs_lock);

	return ret;
}

static void __bpf_prog_offload_destroy(struct bpf_prog *prog)
{
	struct bpf_dev_offload *offload = prog->aux->offload;
	struct netdev_bpf data = {};

	data.offload.prog = prog;

	if (offload->dev_state)
		WARN_ON(__bpf_offload_ndo(prog, BPF_OFFLOAD_DESTROY, &data));

	/* Make sure BPF_PROG_GET_NEXT_ID can't find this dead program */
	bpf_prog_free_id(prog, true);

	list_del_init(&offload->offloads);
	kfree(offload);
	prog->aux->offload = NULL;
}

void bpf_prog_offload_destroy(struct bpf_prog *prog)
{
	rtnl_lock();
	down_write(&bpf_devs_lock);
	if (prog->aux->offload)
		__bpf_prog_offload_destroy(prog);
	up_write(&bpf_devs_lock);
	rtnl_unlock();
}

static int bpf_prog_offload_translate(struct bpf_prog *prog)
{
	struct netdev_bpf data = {};
	int ret;

	data.offload.prog = prog;

	rtnl_lock();
	ret = __bpf_offload_ndo(prog, BPF_OFFLOAD_TRANSLATE, &data);
	rtnl_unlock();

	return ret;
}

static unsigned int bpf_prog_warn_on_exec(const void *ctx,
					  const struct bpf_insn *insn)
{
	WARN(1, "attempt to execute device eBPF program on the host!");
	return 0;
}

int bpf_prog_offload_compile(struct bpf_prog *prog)
{
	prog->bpf_func = bpf_prog_warn_on_exec;

	return bpf_prog_offload_translate(prog);
}

struct ns_get_path_bpf_prog_args {
	struct bpf_prog *prog;
	struct bpf_prog_info *info;
};

static struct ns_common *bpf_prog_offload_info_fill_ns(void *private_data)
{
	struct ns_get_path_bpf_prog_args *args = private_data;
	struct bpf_prog_aux *aux = args->prog->aux;
	struct ns_common *ns;
	struct net *net;

	rtnl_lock();
	down_read(&bpf_devs_lock);

	if (aux->offload) {
		args->info->ifindex = aux->offload->netdev->ifindex;
		net = dev_net(aux->offload->netdev);
		get_net(net);
		ns = &net->ns;
	} else {
		args->info->ifindex = 0;
		ns = NULL;
	}

	up_read(&bpf_devs_lock);
	rtnl_unlock();

	return ns;
}

int bpf_prog_offload_info_fill(struct bpf_prog_info *info,
			       struct bpf_prog *prog)
{
	struct ns_get_path_bpf_prog_args args = {
		.prog	= prog,
		.info	= info,
	};
	struct inode *ns_inode;
	struct path ns_path;
	void *res;

	res = ns_get_path_cb(&ns_path, bpf_prog_offload_info_fill_ns, &args);
	if (IS_ERR(res)) {
		if (!info->ifindex)
			return -ENODEV;
		return PTR_ERR(res);
	}

	ns_inode = ns_path.dentry->d_inode;
	info->netns_dev = new_encode_dev(ns_inode->i_sb->s_dev);
	info->netns_ino = ns_inode->i_ino;
	path_put(&ns_path);

	return 0;
}

const struct bpf_prog_ops bpf_offload_prog_ops = {
};

static int bpf_offload_notification(struct notifier_block *notifier,
				    ulong event, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct bpf_dev_offload *offload, *tmp;

	ASSERT_RTNL();

	switch (event) {
	case NETDEV_UNREGISTER:
		/* ignore namespace changes */
		if (netdev->reg_state != NETREG_UNREGISTERING)
			break;

		down_write(&bpf_devs_lock);
		list_for_each_entry_safe(offload, tmp, &bpf_prog_offload_devs,
					 offloads) {
			if (offload->netdev == netdev)
				__bpf_prog_offload_destroy(offload->prog);
		}
		up_write(&bpf_devs_lock);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block bpf_offload_notifier = {
	.notifier_call = bpf_offload_notification,
};

static int __init bpf_offload_init(void)
{
	register_netdevice_notifier(&bpf_offload_notifier);
	return 0;
}

subsys_initcall(bpf_offload_init);
