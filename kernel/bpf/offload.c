#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/bug.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/printk.h>
#include <linux/rtnetlink.h>

/* protected by RTNL */
static LIST_HEAD(bpf_prog_offload_devs);

int bpf_prog_offload_init(struct bpf_prog *prog, union bpf_attr *attr)
{
	struct net *net = current->nsproxy->net_ns;
	struct bpf_dev_offload *offload;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (attr->prog_flags)
		return -EINVAL;

	offload = kzalloc(sizeof(*offload), GFP_USER);
	if (!offload)
		return -ENOMEM;

	offload->prog = prog;
	init_waitqueue_head(&offload->verifier_done);

	rtnl_lock();
	offload->netdev = __dev_get_by_index(net, attr->prog_target_ifindex);
	if (!offload->netdev) {
		rtnl_unlock();
		kfree(offload);
		return -EINVAL;
	}

	prog->aux->offload = offload;
	list_add_tail(&offload->offloads, &bpf_prog_offload_devs);
	rtnl_unlock();

	return 0;
}

static int __bpf_offload_ndo(struct bpf_prog *prog, enum bpf_netdev_command cmd,
			     struct netdev_bpf *data)
{
	struct net_device *netdev = prog->aux->offload->netdev;

	ASSERT_RTNL();

	if (!netdev)
		return -ENODEV;
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

	env->dev_ops = data.verifier.ops;

	env->prog->aux->offload->dev_state = true;
	env->prog->aux->offload->verifier_running = true;
exit_unlock:
	rtnl_unlock();
	return err;
}

static void __bpf_prog_offload_destroy(struct bpf_prog *prog)
{
	struct bpf_dev_offload *offload = prog->aux->offload;
	struct netdev_bpf data = {};

	data.offload.prog = prog;

	if (offload->verifier_running)
		wait_event(offload->verifier_done, !offload->verifier_running);

	if (offload->dev_state)
		WARN_ON(__bpf_offload_ndo(prog, BPF_OFFLOAD_DESTROY, &data));

	offload->dev_state = false;
	list_del_init(&offload->offloads);
	offload->netdev = NULL;
}

void bpf_prog_offload_destroy(struct bpf_prog *prog)
{
	struct bpf_dev_offload *offload = prog->aux->offload;

	offload->verifier_running = false;
	wake_up(&offload->verifier_done);

	rtnl_lock();
	__bpf_prog_offload_destroy(prog);
	rtnl_unlock();

	kfree(offload);
}

static int bpf_prog_offload_translate(struct bpf_prog *prog)
{
	struct bpf_dev_offload *offload = prog->aux->offload;
	struct netdev_bpf data = {};
	int ret;

	data.offload.prog = prog;

	offload->verifier_running = false;
	wake_up(&offload->verifier_done);

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

u32 bpf_prog_offload_ifindex(struct bpf_prog *prog)
{
	struct bpf_dev_offload *offload = prog->aux->offload;
	u32 ifindex;

	rtnl_lock();
	ifindex = offload->netdev ? offload->netdev->ifindex : 0;
	rtnl_unlock();

	return ifindex;
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
		list_for_each_entry_safe(offload, tmp, &bpf_prog_offload_devs,
					 offloads) {
			if (offload->netdev == netdev)
				__bpf_prog_offload_destroy(offload->prog);
		}
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
