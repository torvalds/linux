/*
 * Copyright (C) 2017-2018 Netronome Systems, Inc.
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
#include <linux/lockdep.h>
#include <linux/netdevice.h>
#include <linux/printk.h>
#include <linux/proc_ns.h>
#include <linux/rhashtable.h>
#include <linux/rtnetlink.h>
#include <linux/rwsem.h>

/* Protects offdevs, members of bpf_offload_netdev and offload members
 * of all progs.
 * RTNL lock cannot be taken when holding this lock.
 */
static DECLARE_RWSEM(bpf_devs_lock);

struct bpf_offload_dev {
	const struct bpf_prog_offload_ops *ops;
	struct list_head netdevs;
	void *priv;
};

struct bpf_offload_netdev {
	struct rhash_head l;
	struct net_device *netdev;
	struct bpf_offload_dev *offdev; /* NULL when bound-only */
	struct list_head progs;
	struct list_head maps;
	struct list_head offdev_netdevs;
};

static const struct rhashtable_params offdevs_params = {
	.nelem_hint		= 4,
	.key_len		= sizeof(struct net_device *),
	.key_offset		= offsetof(struct bpf_offload_netdev, netdev),
	.head_offset		= offsetof(struct bpf_offload_netdev, l),
	.automatic_shrinking	= true,
};

static struct rhashtable offdevs;

static int bpf_dev_offload_check(struct net_device *netdev)
{
	if (!netdev)
		return -EINVAL;
	if (!netdev->netdev_ops->ndo_bpf)
		return -EOPNOTSUPP;
	return 0;
}

static struct bpf_offload_netdev *
bpf_offload_find_netdev(struct net_device *netdev)
{
	lockdep_assert_held(&bpf_devs_lock);

	return rhashtable_lookup_fast(&offdevs, &netdev, offdevs_params);
}

static int __bpf_offload_dev_netdev_register(struct bpf_offload_dev *offdev,
					     struct net_device *netdev)
{
	struct bpf_offload_netdev *ondev;
	int err;

	ondev = kzalloc(sizeof(*ondev), GFP_KERNEL);
	if (!ondev)
		return -ENOMEM;

	ondev->netdev = netdev;
	ondev->offdev = offdev;
	INIT_LIST_HEAD(&ondev->progs);
	INIT_LIST_HEAD(&ondev->maps);

	err = rhashtable_insert_fast(&offdevs, &ondev->l, offdevs_params);
	if (err) {
		netdev_warn(netdev, "failed to register for BPF offload\n");
		goto err_free;
	}

	if (offdev)
		list_add(&ondev->offdev_netdevs, &offdev->netdevs);
	return 0;

err_free:
	kfree(ondev);
	return err;
}

static void __bpf_prog_offload_destroy(struct bpf_prog *prog)
{
	struct bpf_prog_offload *offload = prog->aux->offload;

	if (offload->dev_state)
		offload->offdev->ops->destroy(prog);

	list_del_init(&offload->offloads);
	kfree(offload);
	prog->aux->offload = NULL;
}

static int bpf_map_offload_ndo(struct bpf_offloaded_map *offmap,
			       enum bpf_netdev_command cmd)
{
	struct netdev_bpf data = {};
	struct net_device *netdev;

	ASSERT_RTNL();

	data.command = cmd;
	data.offmap = offmap;
	/* Caller must make sure netdev is valid */
	netdev = offmap->netdev;

	return netdev->netdev_ops->ndo_bpf(netdev, &data);
}

static void __bpf_map_offload_destroy(struct bpf_offloaded_map *offmap)
{
	WARN_ON(bpf_map_offload_ndo(offmap, BPF_OFFLOAD_MAP_FREE));
	/* Make sure BPF_MAP_GET_NEXT_ID can't find this dead map */
	bpf_map_free_id(&offmap->map);
	list_del_init(&offmap->offloads);
	offmap->netdev = NULL;
}

static void __bpf_offload_dev_netdev_unregister(struct bpf_offload_dev *offdev,
						struct net_device *netdev)
{
	struct bpf_offload_netdev *ondev, *altdev = NULL;
	struct bpf_offloaded_map *offmap, *mtmp;
	struct bpf_prog_offload *offload, *ptmp;

	ASSERT_RTNL();

	ondev = rhashtable_lookup_fast(&offdevs, &netdev, offdevs_params);
	if (WARN_ON(!ondev))
		return;

	WARN_ON(rhashtable_remove_fast(&offdevs, &ondev->l, offdevs_params));

	/* Try to move the objects to another netdev of the device */
	if (offdev) {
		list_del(&ondev->offdev_netdevs);
		altdev = list_first_entry_or_null(&offdev->netdevs,
						  struct bpf_offload_netdev,
						  offdev_netdevs);
	}

	if (altdev) {
		list_for_each_entry(offload, &ondev->progs, offloads)
			offload->netdev = altdev->netdev;
		list_splice_init(&ondev->progs, &altdev->progs);

		list_for_each_entry(offmap, &ondev->maps, offloads)
			offmap->netdev = altdev->netdev;
		list_splice_init(&ondev->maps, &altdev->maps);
	} else {
		list_for_each_entry_safe(offload, ptmp, &ondev->progs, offloads)
			__bpf_prog_offload_destroy(offload->prog);
		list_for_each_entry_safe(offmap, mtmp, &ondev->maps, offloads)
			__bpf_map_offload_destroy(offmap);
	}

	WARN_ON(!list_empty(&ondev->progs));
	WARN_ON(!list_empty(&ondev->maps));
	kfree(ondev);
}

static int __bpf_prog_dev_bound_init(struct bpf_prog *prog, struct net_device *netdev)
{
	struct bpf_offload_netdev *ondev;
	struct bpf_prog_offload *offload;
	int err;

	offload = kzalloc(sizeof(*offload), GFP_USER);
	if (!offload)
		return -ENOMEM;

	offload->prog = prog;
	offload->netdev = netdev;

	ondev = bpf_offload_find_netdev(offload->netdev);
	if (!ondev) {
		if (bpf_prog_is_offloaded(prog->aux)) {
			err = -EINVAL;
			goto err_free;
		}

		/* When only binding to the device, explicitly
		 * create an entry in the hashtable.
		 */
		err = __bpf_offload_dev_netdev_register(NULL, offload->netdev);
		if (err)
			goto err_free;
		ondev = bpf_offload_find_netdev(offload->netdev);
	}
	offload->offdev = ondev->offdev;
	prog->aux->offload = offload;
	list_add_tail(&offload->offloads, &ondev->progs);

	return 0;
err_free:
	kfree(offload);
	return err;
}

int bpf_prog_dev_bound_init(struct bpf_prog *prog, union bpf_attr *attr)
{
	struct net_device *netdev;
	int err;

	if (attr->prog_type != BPF_PROG_TYPE_SCHED_CLS &&
	    attr->prog_type != BPF_PROG_TYPE_XDP)
		return -EINVAL;

	if (attr->prog_flags & ~BPF_F_XDP_DEV_BOUND_ONLY)
		return -EINVAL;

	if (attr->prog_type == BPF_PROG_TYPE_SCHED_CLS &&
	    attr->prog_flags & BPF_F_XDP_DEV_BOUND_ONLY)
		return -EINVAL;

	netdev = dev_get_by_index(current->nsproxy->net_ns, attr->prog_ifindex);
	if (!netdev)
		return -EINVAL;

	err = bpf_dev_offload_check(netdev);
	if (err)
		goto out;

	prog->aux->offload_requested = !(attr->prog_flags & BPF_F_XDP_DEV_BOUND_ONLY);

	down_write(&bpf_devs_lock);
	err = __bpf_prog_dev_bound_init(prog, netdev);
	up_write(&bpf_devs_lock);

out:
	dev_put(netdev);
	return err;
}

int bpf_prog_dev_bound_inherit(struct bpf_prog *new_prog, struct bpf_prog *old_prog)
{
	int err;

	if (!bpf_prog_is_dev_bound(old_prog->aux))
		return 0;

	if (bpf_prog_is_offloaded(old_prog->aux))
		return -EINVAL;

	new_prog->aux->dev_bound = old_prog->aux->dev_bound;
	new_prog->aux->offload_requested = old_prog->aux->offload_requested;

	down_write(&bpf_devs_lock);
	if (!old_prog->aux->offload) {
		err = -EINVAL;
		goto out;
	}

	err = __bpf_prog_dev_bound_init(new_prog, old_prog->aux->offload->netdev);

out:
	up_write(&bpf_devs_lock);
	return err;
}

int bpf_prog_offload_verifier_prep(struct bpf_prog *prog)
{
	struct bpf_prog_offload *offload;
	int ret = -ENODEV;

	down_read(&bpf_devs_lock);
	offload = prog->aux->offload;
	if (offload) {
		ret = offload->offdev->ops->prepare(prog);
		offload->dev_state = !ret;
	}
	up_read(&bpf_devs_lock);

	return ret;
}

int bpf_prog_offload_verify_insn(struct bpf_verifier_env *env,
				 int insn_idx, int prev_insn_idx)
{
	struct bpf_prog_offload *offload;
	int ret = -ENODEV;

	down_read(&bpf_devs_lock);
	offload = env->prog->aux->offload;
	if (offload)
		ret = offload->offdev->ops->insn_hook(env, insn_idx,
						      prev_insn_idx);
	up_read(&bpf_devs_lock);

	return ret;
}

int bpf_prog_offload_finalize(struct bpf_verifier_env *env)
{
	struct bpf_prog_offload *offload;
	int ret = -ENODEV;

	down_read(&bpf_devs_lock);
	offload = env->prog->aux->offload;
	if (offload) {
		if (offload->offdev->ops->finalize)
			ret = offload->offdev->ops->finalize(env);
		else
			ret = 0;
	}
	up_read(&bpf_devs_lock);

	return ret;
}

void
bpf_prog_offload_replace_insn(struct bpf_verifier_env *env, u32 off,
			      struct bpf_insn *insn)
{
	const struct bpf_prog_offload_ops *ops;
	struct bpf_prog_offload *offload;
	int ret = -EOPNOTSUPP;

	down_read(&bpf_devs_lock);
	offload = env->prog->aux->offload;
	if (offload) {
		ops = offload->offdev->ops;
		if (!offload->opt_failed && ops->replace_insn)
			ret = ops->replace_insn(env, off, insn);
		offload->opt_failed |= ret;
	}
	up_read(&bpf_devs_lock);
}

void
bpf_prog_offload_remove_insns(struct bpf_verifier_env *env, u32 off, u32 cnt)
{
	struct bpf_prog_offload *offload;
	int ret = -EOPNOTSUPP;

	down_read(&bpf_devs_lock);
	offload = env->prog->aux->offload;
	if (offload) {
		if (!offload->opt_failed && offload->offdev->ops->remove_insns)
			ret = offload->offdev->ops->remove_insns(env, off, cnt);
		offload->opt_failed |= ret;
	}
	up_read(&bpf_devs_lock);
}

void bpf_prog_dev_bound_destroy(struct bpf_prog *prog)
{
	struct bpf_offload_netdev *ondev;
	struct net_device *netdev;

	rtnl_lock();
	down_write(&bpf_devs_lock);
	if (prog->aux->offload) {
		list_del_init(&prog->aux->offload->offloads);

		netdev = prog->aux->offload->netdev;
		__bpf_prog_offload_destroy(prog);

		ondev = bpf_offload_find_netdev(netdev);
		if (!ondev->offdev && list_empty(&ondev->progs))
			__bpf_offload_dev_netdev_unregister(NULL, netdev);
	}
	up_write(&bpf_devs_lock);
	rtnl_unlock();
}

static int bpf_prog_offload_translate(struct bpf_prog *prog)
{
	struct bpf_prog_offload *offload;
	int ret = -ENODEV;

	down_read(&bpf_devs_lock);
	offload = prog->aux->offload;
	if (offload)
		ret = offload->offdev->ops->translate(prog);
	up_read(&bpf_devs_lock);

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
	struct bpf_prog_aux *aux = prog->aux;
	struct inode *ns_inode;
	struct path ns_path;
	char __user *uinsns;
	int res;
	u32 ulen;

	res = ns_get_path_cb(&ns_path, bpf_prog_offload_info_fill_ns, &args);
	if (res) {
		if (!info->ifindex)
			return -ENODEV;
		return res;
	}

	down_read(&bpf_devs_lock);

	if (!aux->offload) {
		up_read(&bpf_devs_lock);
		return -ENODEV;
	}

	ulen = info->jited_prog_len;
	info->jited_prog_len = aux->offload->jited_len;
	if (info->jited_prog_len && ulen) {
		uinsns = u64_to_user_ptr(info->jited_prog_insns);
		ulen = min_t(u32, info->jited_prog_len, ulen);
		if (copy_to_user(uinsns, aux->offload->jited_image, ulen)) {
			up_read(&bpf_devs_lock);
			return -EFAULT;
		}
	}

	up_read(&bpf_devs_lock);

	ns_inode = ns_path.dentry->d_inode;
	info->netns_dev = new_encode_dev(ns_inode->i_sb->s_dev);
	info->netns_ino = ns_inode->i_ino;
	path_put(&ns_path);

	return 0;
}

const struct bpf_prog_ops bpf_offload_prog_ops = {
};

struct bpf_map *bpf_map_offload_map_alloc(union bpf_attr *attr)
{
	struct net *net = current->nsproxy->net_ns;
	struct bpf_offload_netdev *ondev;
	struct bpf_offloaded_map *offmap;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return ERR_PTR(-EPERM);
	if (attr->map_type != BPF_MAP_TYPE_ARRAY &&
	    attr->map_type != BPF_MAP_TYPE_HASH)
		return ERR_PTR(-EINVAL);

	offmap = bpf_map_area_alloc(sizeof(*offmap), NUMA_NO_NODE);
	if (!offmap)
		return ERR_PTR(-ENOMEM);

	bpf_map_init_from_attr(&offmap->map, attr);

	rtnl_lock();
	down_write(&bpf_devs_lock);
	offmap->netdev = __dev_get_by_index(net, attr->map_ifindex);
	err = bpf_dev_offload_check(offmap->netdev);
	if (err)
		goto err_unlock;

	ondev = bpf_offload_find_netdev(offmap->netdev);
	if (!ondev) {
		err = -EINVAL;
		goto err_unlock;
	}

	err = bpf_map_offload_ndo(offmap, BPF_OFFLOAD_MAP_ALLOC);
	if (err)
		goto err_unlock;

	list_add_tail(&offmap->offloads, &ondev->maps);
	up_write(&bpf_devs_lock);
	rtnl_unlock();

	return &offmap->map;

err_unlock:
	up_write(&bpf_devs_lock);
	rtnl_unlock();
	bpf_map_area_free(offmap);
	return ERR_PTR(err);
}

void bpf_map_offload_map_free(struct bpf_map *map)
{
	struct bpf_offloaded_map *offmap = map_to_offmap(map);

	rtnl_lock();
	down_write(&bpf_devs_lock);
	if (offmap->netdev)
		__bpf_map_offload_destroy(offmap);
	up_write(&bpf_devs_lock);
	rtnl_unlock();

	bpf_map_area_free(offmap);
}

u64 bpf_map_offload_map_mem_usage(const struct bpf_map *map)
{
	/* The memory dynamically allocated in netdev dev_ops is not counted */
	return sizeof(struct bpf_offloaded_map);
}

int bpf_map_offload_lookup_elem(struct bpf_map *map, void *key, void *value)
{
	struct bpf_offloaded_map *offmap = map_to_offmap(map);
	int ret = -ENODEV;

	down_read(&bpf_devs_lock);
	if (offmap->netdev)
		ret = offmap->dev_ops->map_lookup_elem(offmap, key, value);
	up_read(&bpf_devs_lock);

	return ret;
}

int bpf_map_offload_update_elem(struct bpf_map *map,
				void *key, void *value, u64 flags)
{
	struct bpf_offloaded_map *offmap = map_to_offmap(map);
	int ret = -ENODEV;

	if (unlikely(flags > BPF_EXIST))
		return -EINVAL;

	down_read(&bpf_devs_lock);
	if (offmap->netdev)
		ret = offmap->dev_ops->map_update_elem(offmap, key, value,
						       flags);
	up_read(&bpf_devs_lock);

	return ret;
}

int bpf_map_offload_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_offloaded_map *offmap = map_to_offmap(map);
	int ret = -ENODEV;

	down_read(&bpf_devs_lock);
	if (offmap->netdev)
		ret = offmap->dev_ops->map_delete_elem(offmap, key);
	up_read(&bpf_devs_lock);

	return ret;
}

int bpf_map_offload_get_next_key(struct bpf_map *map, void *key, void *next_key)
{
	struct bpf_offloaded_map *offmap = map_to_offmap(map);
	int ret = -ENODEV;

	down_read(&bpf_devs_lock);
	if (offmap->netdev)
		ret = offmap->dev_ops->map_get_next_key(offmap, key, next_key);
	up_read(&bpf_devs_lock);

	return ret;
}

struct ns_get_path_bpf_map_args {
	struct bpf_offloaded_map *offmap;
	struct bpf_map_info *info;
};

static struct ns_common *bpf_map_offload_info_fill_ns(void *private_data)
{
	struct ns_get_path_bpf_map_args *args = private_data;
	struct ns_common *ns;
	struct net *net;

	rtnl_lock();
	down_read(&bpf_devs_lock);

	if (args->offmap->netdev) {
		args->info->ifindex = args->offmap->netdev->ifindex;
		net = dev_net(args->offmap->netdev);
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

int bpf_map_offload_info_fill(struct bpf_map_info *info, struct bpf_map *map)
{
	struct ns_get_path_bpf_map_args args = {
		.offmap	= map_to_offmap(map),
		.info	= info,
	};
	struct inode *ns_inode;
	struct path ns_path;
	int res;

	res = ns_get_path_cb(&ns_path, bpf_map_offload_info_fill_ns, &args);
	if (res) {
		if (!info->ifindex)
			return -ENODEV;
		return res;
	}

	ns_inode = ns_path.dentry->d_inode;
	info->netns_dev = new_encode_dev(ns_inode->i_sb->s_dev);
	info->netns_ino = ns_inode->i_ino;
	path_put(&ns_path);

	return 0;
}

static bool __bpf_offload_dev_match(struct bpf_prog *prog,
				    struct net_device *netdev)
{
	struct bpf_offload_netdev *ondev1, *ondev2;
	struct bpf_prog_offload *offload;

	if (!bpf_prog_is_dev_bound(prog->aux))
		return false;

	offload = prog->aux->offload;
	if (!offload)
		return false;
	if (offload->netdev == netdev)
		return true;

	ondev1 = bpf_offload_find_netdev(offload->netdev);
	ondev2 = bpf_offload_find_netdev(netdev);

	return ondev1 && ondev2 && ondev1->offdev == ondev2->offdev;
}

bool bpf_offload_dev_match(struct bpf_prog *prog, struct net_device *netdev)
{
	bool ret;

	down_read(&bpf_devs_lock);
	ret = __bpf_offload_dev_match(prog, netdev);
	up_read(&bpf_devs_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(bpf_offload_dev_match);

bool bpf_prog_dev_bound_match(const struct bpf_prog *lhs, const struct bpf_prog *rhs)
{
	bool ret;

	if (bpf_prog_is_offloaded(lhs->aux) != bpf_prog_is_offloaded(rhs->aux))
		return false;

	down_read(&bpf_devs_lock);
	ret = lhs->aux->offload && rhs->aux->offload &&
	      lhs->aux->offload->netdev &&
	      lhs->aux->offload->netdev == rhs->aux->offload->netdev;
	up_read(&bpf_devs_lock);

	return ret;
}

bool bpf_offload_prog_map_match(struct bpf_prog *prog, struct bpf_map *map)
{
	struct bpf_offloaded_map *offmap;
	bool ret;

	if (!bpf_map_is_offloaded(map))
		return bpf_map_offload_neutral(map);
	offmap = map_to_offmap(map);

	down_read(&bpf_devs_lock);
	ret = __bpf_offload_dev_match(prog, offmap->netdev);
	up_read(&bpf_devs_lock);

	return ret;
}

int bpf_offload_dev_netdev_register(struct bpf_offload_dev *offdev,
				    struct net_device *netdev)
{
	int err;

	down_write(&bpf_devs_lock);
	err = __bpf_offload_dev_netdev_register(offdev, netdev);
	up_write(&bpf_devs_lock);
	return err;
}
EXPORT_SYMBOL_GPL(bpf_offload_dev_netdev_register);

void bpf_offload_dev_netdev_unregister(struct bpf_offload_dev *offdev,
				       struct net_device *netdev)
{
	down_write(&bpf_devs_lock);
	__bpf_offload_dev_netdev_unregister(offdev, netdev);
	up_write(&bpf_devs_lock);
}
EXPORT_SYMBOL_GPL(bpf_offload_dev_netdev_unregister);

struct bpf_offload_dev *
bpf_offload_dev_create(const struct bpf_prog_offload_ops *ops, void *priv)
{
	struct bpf_offload_dev *offdev;

	offdev = kzalloc(sizeof(*offdev), GFP_KERNEL);
	if (!offdev)
		return ERR_PTR(-ENOMEM);

	offdev->ops = ops;
	offdev->priv = priv;
	INIT_LIST_HEAD(&offdev->netdevs);

	return offdev;
}
EXPORT_SYMBOL_GPL(bpf_offload_dev_create);

void bpf_offload_dev_destroy(struct bpf_offload_dev *offdev)
{
	WARN_ON(!list_empty(&offdev->netdevs));
	kfree(offdev);
}
EXPORT_SYMBOL_GPL(bpf_offload_dev_destroy);

void *bpf_offload_dev_priv(struct bpf_offload_dev *offdev)
{
	return offdev->priv;
}
EXPORT_SYMBOL_GPL(bpf_offload_dev_priv);

void bpf_dev_bound_netdev_unregister(struct net_device *dev)
{
	struct bpf_offload_netdev *ondev;

	ASSERT_RTNL();

	down_write(&bpf_devs_lock);
	ondev = bpf_offload_find_netdev(dev);
	if (ondev && !ondev->offdev)
		__bpf_offload_dev_netdev_unregister(NULL, ondev->netdev);
	up_write(&bpf_devs_lock);
}

int bpf_dev_bound_kfunc_check(struct bpf_verifier_log *log,
			      struct bpf_prog_aux *prog_aux)
{
	if (!bpf_prog_is_dev_bound(prog_aux)) {
		bpf_log(log, "metadata kfuncs require device-bound program\n");
		return -EINVAL;
	}

	if (bpf_prog_is_offloaded(prog_aux)) {
		bpf_log(log, "metadata kfuncs can't be offloaded\n");
		return -EINVAL;
	}

	return 0;
}

void *bpf_dev_bound_resolve_kfunc(struct bpf_prog *prog, u32 func_id)
{
	const struct xdp_metadata_ops *ops;
	void *p = NULL;

	/* We don't hold bpf_devs_lock while resolving several
	 * kfuncs and can race with the unregister_netdevice().
	 * We rely on bpf_dev_bound_match() check at attach
	 * to render this program unusable.
	 */
	down_read(&bpf_devs_lock);
	if (!prog->aux->offload)
		goto out;

	ops = prog->aux->offload->netdev->xdp_metadata_ops;
	if (!ops)
		goto out;

	if (func_id == bpf_xdp_metadata_kfunc_id(XDP_METADATA_KFUNC_RX_TIMESTAMP))
		p = ops->xmo_rx_timestamp;
	else if (func_id == bpf_xdp_metadata_kfunc_id(XDP_METADATA_KFUNC_RX_HASH))
		p = ops->xmo_rx_hash;
out:
	up_read(&bpf_devs_lock);

	return p;
}

static int __init bpf_offload_init(void)
{
	return rhashtable_init(&offdevs, &offdevs_params);
}

late_initcall(bpf_offload_init);
