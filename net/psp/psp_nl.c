// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ethtool.h>
#include <linux/net_namespace.h>
#include <linux/skbuff.h>
#include <linux/xarray.h>
#include <net/genetlink.h>
#include <net/psp.h>
#include <net/sock.h>

#include "psp-nl-gen.h"
#include "psp.h"

/* Netlink helpers */

static struct sk_buff *psp_nl_reply_new(struct genl_info *info)
{
	struct sk_buff *rsp;
	void *hdr;

	rsp = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!rsp)
		return NULL;

	hdr = genlmsg_iput(rsp, info);
	if (!hdr) {
		nlmsg_free(rsp);
		return NULL;
	}

	return rsp;
}

static int psp_nl_reply_send(struct sk_buff *rsp, struct genl_info *info)
{
	/* Note that this *only* works with a single message per skb! */
	nlmsg_end(rsp, (struct nlmsghdr *)rsp->data);

	return genlmsg_reply(rsp, info);
}

/**
 * psp_nl_multicast_per_ns() - multicast a notification to each unique netns
 * @psd: PSP device (must be locked)
 * @group: multicast group
 * @build_ntf: callback to build an skb for a given netns, or NULL on failure
 * @ctx: opaque context passed to @build_ntf
 *
 * Iterates all unique network namespaces from the associated device list
 * plus the main device's netns. For each unique netns, calls @build_ntf
 * to construct a notification skb and multicasts it.
 */
static void
psp_nl_multicast_per_ns(struct psp_dev *psd, unsigned int group,
			struct sk_buff *(*build_ntf)(struct psp_dev *,
						     struct net *,
						     void *),
			void *ctx)
{
	struct psp_assoc_dev *entry;
	struct xarray sent_nets;
	struct net *main_net;
	struct sk_buff *ntf;

	main_net = dev_net(psd->main_netdev);
	xa_init(&sent_nets);

	list_for_each_entry(entry, &psd->assoc_dev_list, dev_list) {
		struct net *assoc_net = dev_net(entry->assoc_dev);
		int ret;

		if (net_eq(assoc_net, main_net))
			continue;

		ret = xa_insert(&sent_nets, (unsigned long)assoc_net, assoc_net,
				GFP_KERNEL);
		if (ret == -EBUSY)
			continue;

		ntf = build_ntf(psd, assoc_net, ctx);
		if (!ntf)
			continue;

		genlmsg_multicast_netns(&psp_nl_family, assoc_net, ntf, 0,
					group, GFP_KERNEL);
	}
	xa_destroy(&sent_nets);

	/* Send to main device netns */
	ntf = build_ntf(psd, main_net, ctx);
	if (!ntf)
		return;
	genlmsg_multicast_netns(&psp_nl_family, main_net, ntf, 0, group,
				GFP_KERNEL);
}

static struct sk_buff *psp_nl_clone_ntf(struct psp_dev *psd, struct net *net,
					void *ctx)
{
	return skb_clone(ctx, GFP_KERNEL);
}

static void psp_nl_multicast_all_ns(struct psp_dev *psd, struct sk_buff *ntf,
				    unsigned int group)
{
	psp_nl_multicast_per_ns(psd, group, psp_nl_clone_ntf, ntf);
	nlmsg_consume(ntf);
}

/* Device stuff */

static struct psp_dev *
psp_device_get_and_lock(struct net *net, struct nlattr *dev_id,
			bool admin)
{
	struct psp_dev *psd;
	int err;

	mutex_lock(&psp_devs_lock);
	psd = xa_load(&psp_devs, nla_get_u32(dev_id));
	if (!psd) {
		mutex_unlock(&psp_devs_lock);
		return ERR_PTR(-ENODEV);
	}

	mutex_lock(&psd->lock);
	mutex_unlock(&psp_devs_lock);

	err = psp_dev_check_access(psd, net, admin);
	if (err) {
		mutex_unlock(&psd->lock);
		return ERR_PTR(err);
	}

	return psd;
}

static int __psp_device_get_locked(const struct genl_split_ops *ops,
				   struct sk_buff *skb, struct genl_info *info,
				   bool admin)
{
	if (GENL_REQ_ATTR_CHECK(info, PSP_A_DEV_ID))
		return -EINVAL;

	info->user_ptr[0] = psp_device_get_and_lock(genl_info_net(info),
						    info->attrs[PSP_A_DEV_ID],
						    admin);
	return PTR_ERR_OR_ZERO(info->user_ptr[0]);
}

int psp_device_get_locked_admin(const struct genl_split_ops *ops,
				struct sk_buff *skb, struct genl_info *info)
{
	return __psp_device_get_locked(ops, skb, info, true);
}

int psp_device_get_locked(const struct genl_split_ops *ops,
			  struct sk_buff *skb, struct genl_info *info)
{
	return __psp_device_get_locked(ops, skb, info, false);
}

/*
 * Non-admin version of psp_device_get_locked() + psp_attach_netdev_notifier()
 * only used for dev-assoc.
 */
int psp_device_get_locked_dev_assoc(const struct genl_split_ops *ops,
				    struct sk_buff *skb, struct genl_info *info)
{
	int err;

	err = psp_attach_netdev_notifier();
	if (err)
		return err;

	return __psp_device_get_locked(ops, skb, info, false);
}

static struct net *psp_nl_resolve_assoc_dev_ns(struct psp_dev *psd,
					       struct genl_info *info)
{
	struct net *net;
	int nsid;

	if (GENL_REQ_ATTR_CHECK(info, PSP_A_DEV_IFINDEX))
		return ERR_PTR(-EINVAL);

	if (info->attrs[PSP_A_DEV_NSID]) {
		/* Only callers in the main netns may specify nsid */
		if (dev_net(psd->main_netdev) != genl_info_net(info)) {
			NL_SET_BAD_ATTR(info->extack,
					info->attrs[PSP_A_DEV_NSID]);
			return ERR_PTR(-EPERM);
		}

		nsid = nla_get_s32(info->attrs[PSP_A_DEV_NSID]);

		net = get_net_ns_by_id(genl_info_net(info), nsid);
		if (!net) {
			NL_SET_BAD_ATTR(info->extack,
					info->attrs[PSP_A_DEV_NSID]);
			return ERR_PTR(-EINVAL);
		}
	} else {
		net = get_net(genl_info_net(info));
	}

	return net;
}

void
psp_device_unlock(const struct genl_split_ops *ops, struct sk_buff *skb,
		  struct genl_info *info)
{
	struct socket *socket = info->user_ptr[1];
	struct psp_dev *psd = info->user_ptr[0];

	mutex_unlock(&psd->lock);
	if (socket)
		sockfd_put(socket);
}

bool psp_has_assoc_dev_in_ns(struct psp_dev *psd, struct net *net)
{
	struct psp_assoc_dev *entry;

	list_for_each_entry(entry, &psd->assoc_dev_list, dev_list) {
		if (dev_net(entry->assoc_dev) == net)
			return true;
	}

	return false;
}

static int psp_nl_fill_assoc_dev_list(struct psp_dev *psd, struct sk_buff *rsp,
				      struct net *cur_net,
				      struct net *filter_net)
{
	struct psp_assoc_dev *entry;
	struct net *dev_net_ns;
	struct nlattr *nest;
	int nsid;

	list_for_each_entry(entry, &psd->assoc_dev_list, dev_list) {
		dev_net_ns = dev_net(entry->assoc_dev);

		if (filter_net && dev_net_ns != filter_net)
			continue;

		/* When filtering by namespace, all devices are in the caller's
		 * namespace so nsid is always NETNSA_NSID_NOT_ASSIGNED (-1).
		 * Otherwise, calculate the nsid relative to cur_net.
		 */
		nsid = filter_net ? NETNSA_NSID_NOT_ASSIGNED :
				    peernet2id_alloc(cur_net, dev_net_ns,
						     GFP_KERNEL);

		nest = nla_nest_start(rsp, PSP_A_DEV_ASSOC_LIST);
		if (!nest)
			return -EMSGSIZE;

		if (nla_put_u32(rsp, PSP_A_ASSOC_DEV_INFO_IFINDEX,
				entry->assoc_dev->ifindex) ||
		    nla_put_s32(rsp, PSP_A_ASSOC_DEV_INFO_NSID, nsid)) {
			nla_nest_cancel(rsp, nest);
			return -EMSGSIZE;
		}

		nla_nest_end(rsp, nest);
	}

	return 0;
}

static int
psp_nl_dev_fill(struct psp_dev *psd, struct sk_buff *rsp,
		const struct genl_info *info)
{
	struct net *cur_net;
	void *hdr;
	int err;

	cur_net = genl_info_net(info);

	hdr = genlmsg_iput(rsp, info);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_u32(rsp, PSP_A_DEV_ID, psd->id) ||
	    nla_put_u32(rsp, PSP_A_DEV_IFINDEX, psd->main_netdev->ifindex) ||
	    nla_put_u32(rsp, PSP_A_DEV_PSP_VERSIONS_CAP, psd->caps->versions) ||
	    nla_put_u32(rsp, PSP_A_DEV_PSP_VERSIONS_ENA, psd->config.versions))
		goto err_cancel_msg;

	if (cur_net == dev_net(psd->main_netdev)) {
		/* Primary device - dump assoc list */
		err = psp_nl_fill_assoc_dev_list(psd, rsp, cur_net, NULL);
		if (err)
			goto err_cancel_msg;
	} else {
		/* In netns: set by-association flag and dump filtered
		 * assoc list containing only devices in cur_net
		 */
		if (nla_put_flag(rsp, PSP_A_DEV_BY_ASSOCIATION))
			goto err_cancel_msg;
		err = psp_nl_fill_assoc_dev_list(psd, rsp, cur_net, cur_net);
		if (err)
			goto err_cancel_msg;
	}

	genlmsg_end(rsp, hdr);
	return 0;

err_cancel_msg:
	genlmsg_cancel(rsp, hdr);
	return -EMSGSIZE;
}

static struct sk_buff *psp_nl_build_dev_ntf(struct psp_dev *psd,
					    struct net *net, void *ctx)
{
	u32 cmd = *(u32 *)ctx;
	struct genl_info info;
	struct sk_buff *ntf;

	if (!genl_has_listeners(&psp_nl_family, net, PSP_NLGRP_MGMT))
		return NULL;

	ntf = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!ntf)
		return NULL;

	genl_info_init_ntf(&info, &psp_nl_family, cmd);
	genl_info_net_set(&info, net);
	if (psp_nl_dev_fill(psd, ntf, &info)) {
		nlmsg_free(ntf);
		return NULL;
	}

	return ntf;
}

void psp_nl_notify_dev(struct psp_dev *psd, u32 cmd)
{
	psp_nl_multicast_per_ns(psd, PSP_NLGRP_MGMT,
				psp_nl_build_dev_ntf, &cmd);
}

int psp_nl_dev_get_doit(struct sk_buff *req, struct genl_info *info)
{
	struct psp_dev *psd = info->user_ptr[0];
	struct sk_buff *rsp;
	int err;

	rsp = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!rsp)
		return -ENOMEM;

	err = psp_nl_dev_fill(psd, rsp, info);
	if (err)
		goto err_free_msg;

	return genlmsg_reply(rsp, info);

err_free_msg:
	nlmsg_free(rsp);
	return err;
}

static int
psp_nl_dev_get_dumpit_one(struct sk_buff *rsp, struct netlink_callback *cb,
			  struct psp_dev *psd)
{
	if (psp_dev_check_access(psd, sock_net(rsp->sk), false))
		return 0;

	return psp_nl_dev_fill(psd, rsp, genl_info_dump(cb));
}

int psp_nl_dev_get_dumpit(struct sk_buff *rsp, struct netlink_callback *cb)
{
	struct psp_dev *psd;
	int err = 0;

	mutex_lock(&psp_devs_lock);
	xa_for_each_start(&psp_devs, cb->args[0], psd, cb->args[0]) {
		mutex_lock(&psd->lock);
		err = psp_nl_dev_get_dumpit_one(rsp, cb, psd);
		mutex_unlock(&psd->lock);
		if (err)
			break;
	}
	mutex_unlock(&psp_devs_lock);

	return err;
}

int psp_nl_dev_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct psp_dev *psd = info->user_ptr[0];
	struct psp_dev_config new_config;
	struct sk_buff *rsp;
	int err;

	memcpy(&new_config, &psd->config, sizeof(new_config));

	if (info->attrs[PSP_A_DEV_PSP_VERSIONS_ENA]) {
		new_config.versions =
			nla_get_u32(info->attrs[PSP_A_DEV_PSP_VERSIONS_ENA]);
		if (new_config.versions & ~psd->caps->versions) {
			NL_SET_ERR_MSG(info->extack, "Requested PSP versions not supported by the device");
			return -EINVAL;
		}
	} else {
		NL_SET_ERR_MSG(info->extack, "No settings present");
		return -EINVAL;
	}

	rsp = psp_nl_reply_new(info);
	if (!rsp)
		return -ENOMEM;

	if (memcmp(&new_config, &psd->config, sizeof(new_config))) {
		err = psd->ops->set_config(psd, &new_config, info->extack);
		if (err)
			goto err_free_rsp;

		memcpy(&psd->config, &new_config, sizeof(new_config));
	}

	psp_nl_notify_dev(psd, PSP_CMD_DEV_CHANGE_NTF);

	return psp_nl_reply_send(rsp, info);

err_free_rsp:
	nlmsg_free(rsp);
	return err;
}

int psp_nl_key_rotate_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct psp_dev *psd = info->user_ptr[0];
	struct genl_info ntf_info;
	struct sk_buff *ntf, *rsp;
	u8 prev_gen;
	int err;

	rsp = psp_nl_reply_new(info);
	if (!rsp)
		return -ENOMEM;

	genl_info_init_ntf(&ntf_info, &psp_nl_family, PSP_CMD_KEY_ROTATE_NTF);
	ntf = psp_nl_reply_new(&ntf_info);
	if (!ntf) {
		err = -ENOMEM;
		goto err_free_rsp;
	}

	if (nla_put_u32(rsp, PSP_A_DEV_ID, psd->id) ||
	    nla_put_u32(ntf, PSP_A_DEV_ID, psd->id)) {
		err = -EMSGSIZE;
		goto err_free_ntf;
	}

	/* suggest the next gen number, driver can override */
	prev_gen = psd->generation;
	psd->generation = (prev_gen + 1) & PSP_GEN_VALID_MASK;

	err = psd->ops->key_rotate(psd, info->extack);
	if (err)
		goto err_free_ntf;

	WARN_ON_ONCE((psd->generation && psd->generation == prev_gen) ||
		     psd->generation & ~PSP_GEN_VALID_MASK);

	psp_assocs_key_rotated(psd);
	psd->stats.rotations++;

	nlmsg_end(ntf, (struct nlmsghdr *)ntf->data);

	psp_nl_multicast_all_ns(psd, ntf, PSP_NLGRP_USE);

	return psp_nl_reply_send(rsp, info);

err_free_ntf:
	nlmsg_free(ntf);
err_free_rsp:
	nlmsg_free(rsp);
	return err;
}

int psp_nl_dev_assoc_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct psp_dev *psd = info->user_ptr[0];
	struct psp_assoc_dev *psp_assoc_dev;
	struct net_device *assoc_dev;
	struct sk_buff *rsp;
	u32 assoc_ifindex;
	struct net *net;
	int err;

	if (psd->assoc_dev_cnt >= PSP_ASSOC_DEV_MAX) {
		NL_SET_ERR_MSG(info->extack,
			       "Maximum number of associated devices reached");
		return -ENOSPC;
	}

	net = psp_nl_resolve_assoc_dev_ns(psd, info);
	if (IS_ERR(net))
		return PTR_ERR(net);

	psp_assoc_dev = kzalloc_obj(*psp_assoc_dev);
	if (!psp_assoc_dev) {
		err = -ENOMEM;
		goto err_put_net;
	}

	assoc_ifindex = nla_get_u32(info->attrs[PSP_A_DEV_IFINDEX]);
	assoc_dev = netdev_get_by_index(net, assoc_ifindex,
					&psp_assoc_dev->dev_tracker,
					GFP_KERNEL);
	if (!assoc_dev) {
		NL_SET_BAD_ATTR(info->extack, info->attrs[PSP_A_DEV_IFINDEX]);
		err = -ENODEV;
		goto err_free_assoc;
	}

	/* Check if device is already associated with a PSP device */
	if (cmpxchg(&assoc_dev->psp_dev, NULL, RCU_INITIALIZER(psd))) {
		NL_SET_ERR_MSG(info->extack,
			       "Device already associated with a PSP device");
		err = -EBUSY;
		goto err_put_dev;
	}

	psp_assoc_dev->assoc_dev = assoc_dev;

	/* Check for race with NETDEV_UNREGISTER. The cmpxchg above is a
	 * full barrier, and the unregister path has synchronize_net()
	 * between setting NETREG_UNREGISTERING and reading psp_dev in the
	 * notifier. So at least one side would do the clean-up if we are in
	 * the middle of unregitering assoc_dev.
	 * And the clean-up is serialized by psd->lock.
	 */
	if (READ_ONCE(assoc_dev->reg_state) != NETREG_REGISTERED) {
		err = -ENODEV;
		goto err_clean_ptr;
	}

	rsp = psp_nl_reply_new(info);
	if (!rsp) {
		err = -ENOMEM;
		goto err_clean_ptr;
	}

	list_add_tail(&psp_assoc_dev->dev_list, &psd->assoc_dev_list);
	psd->assoc_dev_cnt++;

	put_net(net);

	psp_nl_notify_dev(psd, PSP_CMD_DEV_CHANGE_NTF);

	return psp_nl_reply_send(rsp, info);

err_clean_ptr:
	rcu_assign_pointer(assoc_dev->psp_dev, NULL);
err_put_dev:
	netdev_put(assoc_dev, &psp_assoc_dev->dev_tracker);
err_free_assoc:
	kfree(psp_assoc_dev);
err_put_net:
	put_net(net);

	return err;
}

int psp_nl_dev_disassoc_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct psp_assoc_dev *entry, *found = NULL;
	struct psp_dev *psd = info->user_ptr[0];
	struct sk_buff *rsp;
	u32 assoc_ifindex;
	struct net *net;

	net = psp_nl_resolve_assoc_dev_ns(psd, info);
	if (IS_ERR(net))
		return PTR_ERR(net);

	assoc_ifindex = nla_get_u32(info->attrs[PSP_A_DEV_IFINDEX]);

	/* Search the association list by ifindex and netns */
	list_for_each_entry(entry, &psd->assoc_dev_list, dev_list) {
		if (entry->assoc_dev->ifindex == assoc_ifindex &&
		    dev_net(entry->assoc_dev) == net) {
			found = entry;
			break;
		}
	}

	if (!found) {
		put_net(net);
		NL_SET_BAD_ATTR(info->extack, info->attrs[PSP_A_DEV_IFINDEX]);
		return -ENODEV;
	}

	rsp = psp_nl_reply_new(info);
	if (!rsp) {
		put_net(net);
		return -ENOMEM;
	}

	put_net(net);

	/* Notify before removal so listeners in the disassociated namespace
	 * still receive the notification.
	 */
	psp_nl_notify_dev(psd, PSP_CMD_DEV_CHANGE_NTF);

	/* Remove from the association list */
	list_del(&found->dev_list);
	psd->assoc_dev_cnt--;
	rcu_assign_pointer(found->assoc_dev->psp_dev, NULL);
	netdev_put(found->assoc_dev, &found->dev_tracker);
	kfree(found);

	return psp_nl_reply_send(rsp, info);
}

/* Key etc. */

int psp_assoc_device_get_locked(const struct genl_split_ops *ops,
				struct sk_buff *skb, struct genl_info *info)
{
	struct socket *socket;
	struct psp_dev *psd;
	struct nlattr *id;
	int fd, err;

	if (GENL_REQ_ATTR_CHECK(info, PSP_A_ASSOC_SOCK_FD))
		return -EINVAL;

	fd = nla_get_u32(info->attrs[PSP_A_ASSOC_SOCK_FD]);
	socket = sockfd_lookup(fd, &err);
	if (!socket)
		return err;

	if (!sk_is_tcp(socket->sk)) {
		NL_SET_ERR_MSG_ATTR(info->extack,
				    info->attrs[PSP_A_ASSOC_SOCK_FD],
				    "Unsupported socket family and type");
		err = -EOPNOTSUPP;
		goto err_sock_put;
	}

	psd = psp_dev_get_for_sock(socket->sk);
	if (psd) {
		/* Extra care needed here, psp_dev_get_for_sock() only gives
		 * us access to struct psp_dev's memory, which is quite weak.
		 */
		mutex_lock(&psd->lock);
		if (!psp_dev_is_registered(psd) ||
		    psp_dev_check_access(psd, genl_info_net(info), false)) {
			mutex_unlock(&psd->lock);
			psp_dev_put(psd);
			psd = NULL;
		}
	}

	if (!psd && GENL_REQ_ATTR_CHECK(info, PSP_A_ASSOC_DEV_ID)) {
		err = -EINVAL;
		goto err_sock_put;
	}

	id = info->attrs[PSP_A_ASSOC_DEV_ID];
	if (psd) {
		if (id && psd->id != nla_get_u32(id)) {
			mutex_unlock(&psd->lock);
			NL_SET_ERR_MSG_ATTR(info->extack, id,
					    "Device id vs socket mismatch");
			err = -EINVAL;
			goto err_psd_put;
		}

		psp_dev_put(psd);
	} else {
		psd = psp_device_get_and_lock(genl_info_net(info), id, false);
		if (IS_ERR(psd)) {
			err = PTR_ERR(psd);
			goto err_sock_put;
		}
	}

	info->user_ptr[0] = psd;
	info->user_ptr[1] = socket;

	return 0;

err_psd_put:
	psp_dev_put(psd);
err_sock_put:
	sockfd_put(socket);
	return err;
}

static int
psp_nl_parse_key(struct genl_info *info, u32 attr, struct psp_key_parsed *key,
		 unsigned int key_sz)
{
	struct nlattr *nest = info->attrs[attr];
	struct nlattr *tb[PSP_A_KEYS_SPI + 1];
	u32 spi;
	int err;

	err = nla_parse_nested(tb, ARRAY_SIZE(tb) - 1, nest,
			       psp_keys_nl_policy, info->extack);
	if (err)
		return err;

	if (NL_REQ_ATTR_CHECK(info->extack, nest, tb, PSP_A_KEYS_KEY) ||
	    NL_REQ_ATTR_CHECK(info->extack, nest, tb, PSP_A_KEYS_SPI))
		return -EINVAL;

	if (nla_len(tb[PSP_A_KEYS_KEY]) != key_sz) {
		NL_SET_ERR_MSG_ATTR(info->extack, tb[PSP_A_KEYS_KEY],
				    "incorrect key length");
		return -EINVAL;
	}

	spi = nla_get_u32(tb[PSP_A_KEYS_SPI]);
	if (!(spi & PSP_SPI_KEY_ID)) {
		NL_SET_ERR_MSG_ATTR(info->extack, tb[PSP_A_KEYS_KEY],
				    "invalid SPI: lower 31b must be non-zero");
		return -EINVAL;
	}

	key->spi = cpu_to_be32(spi);
	memcpy(key->key, nla_data(tb[PSP_A_KEYS_KEY]), key_sz);

	return 0;
}

static int
psp_nl_put_key(struct sk_buff *skb, u32 attr, u32 version,
	       struct psp_key_parsed *key)
{
	int key_sz = psp_key_size(version);
	void *nest;

	nest = nla_nest_start(skb, attr);

	if (nla_put_u32(skb, PSP_A_KEYS_SPI, be32_to_cpu(key->spi)) ||
	    nla_put(skb, PSP_A_KEYS_KEY, key_sz, key->key)) {
		nla_nest_cancel(skb, nest);
		return -EMSGSIZE;
	}

	nla_nest_end(skb, nest);

	return 0;
}

int psp_nl_rx_assoc_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct socket *socket = info->user_ptr[1];
	struct psp_dev *psd = info->user_ptr[0];
	struct psp_key_parsed key;
	struct psp_assoc *pas;
	struct sk_buff *rsp;
	u32 version;
	int err;

	if (GENL_REQ_ATTR_CHECK(info, PSP_A_ASSOC_VERSION))
		return -EINVAL;

	version = nla_get_u32(info->attrs[PSP_A_ASSOC_VERSION]);
	if (!(psd->caps->versions & (1 << version))) {
		NL_SET_BAD_ATTR(info->extack, info->attrs[PSP_A_ASSOC_VERSION]);
		return -EOPNOTSUPP;
	}

	rsp = psp_nl_reply_new(info);
	if (!rsp)
		return -ENOMEM;

	pas = psp_assoc_create(psd);
	if (!pas) {
		err = -ENOMEM;
		goto err_free_rsp;
	}
	pas->version = version;

	err = psd->ops->rx_spi_alloc(psd, version, &key, info->extack);
	if (err)
		goto err_free_pas;

	if (nla_put_u32(rsp, PSP_A_ASSOC_DEV_ID, psd->id) ||
	    psp_nl_put_key(rsp, PSP_A_ASSOC_RX_KEY, version, &key)) {
		err = -EMSGSIZE;
		goto err_free_pas;
	}

	err = psp_sock_assoc_set_rx(socket->sk, pas, &key, info->extack);
	if (err) {
		NL_SET_BAD_ATTR(info->extack, info->attrs[PSP_A_ASSOC_SOCK_FD]);
		goto err_free_pas;
	}
	psp_assoc_put(pas);

	return psp_nl_reply_send(rsp, info);

err_free_pas:
	psp_assoc_put(pas);
err_free_rsp:
	nlmsg_free(rsp);
	return err;
}

int psp_nl_tx_assoc_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct socket *socket = info->user_ptr[1];
	struct psp_dev *psd = info->user_ptr[0];
	struct psp_key_parsed key;
	struct sk_buff *rsp;
	unsigned int key_sz;
	u32 version;
	int err;

	if (GENL_REQ_ATTR_CHECK(info, PSP_A_ASSOC_VERSION) ||
	    GENL_REQ_ATTR_CHECK(info, PSP_A_ASSOC_TX_KEY))
		return -EINVAL;

	version = nla_get_u32(info->attrs[PSP_A_ASSOC_VERSION]);
	if (!(psd->caps->versions & (1 << version))) {
		NL_SET_BAD_ATTR(info->extack, info->attrs[PSP_A_ASSOC_VERSION]);
		return -EOPNOTSUPP;
	}

	key_sz = psp_key_size(version);
	if (!key_sz)
		return -EINVAL;

	err = psp_nl_parse_key(info, PSP_A_ASSOC_TX_KEY, &key, key_sz);
	if (err < 0)
		return err;

	rsp = psp_nl_reply_new(info);
	if (!rsp)
		return -ENOMEM;

	err = psp_sock_assoc_set_tx(socket->sk, psd, version, &key,
				    info->extack);
	if (err)
		goto err_free_msg;

	return psp_nl_reply_send(rsp, info);

err_free_msg:
	nlmsg_free(rsp);
	return err;
}

static int
psp_nl_stats_fill(struct psp_dev *psd, struct sk_buff *rsp,
		  const struct genl_info *info)
{
	unsigned int required_cnt = sizeof(struct psp_dev_stats) / sizeof(u64);
	struct psp_dev_stats stats;
	void *hdr;
	int i;

	memset(&stats, 0xff, sizeof(stats));
	psd->ops->get_stats(psd, &stats);

	for (i = 0; i < required_cnt; i++)
		if (WARN_ON_ONCE(stats.required[i] == ETHTOOL_STAT_NOT_SET))
			return -EOPNOTSUPP;

	hdr = genlmsg_iput(rsp, info);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_u32(rsp, PSP_A_STATS_DEV_ID, psd->id) ||
	    nla_put_uint(rsp, PSP_A_STATS_KEY_ROTATIONS,
			 psd->stats.rotations) ||
	    nla_put_uint(rsp, PSP_A_STATS_STALE_EVENTS, psd->stats.stales) ||
	    nla_put_uint(rsp, PSP_A_STATS_RX_PACKETS, stats.rx_packets) ||
	    nla_put_uint(rsp, PSP_A_STATS_RX_BYTES, stats.rx_bytes) ||
	    nla_put_uint(rsp, PSP_A_STATS_RX_AUTH_FAIL, stats.rx_auth_fail) ||
	    nla_put_uint(rsp, PSP_A_STATS_RX_ERROR, stats.rx_error) ||
	    nla_put_uint(rsp, PSP_A_STATS_RX_BAD, stats.rx_bad) ||
	    nla_put_uint(rsp, PSP_A_STATS_TX_PACKETS, stats.tx_packets) ||
	    nla_put_uint(rsp, PSP_A_STATS_TX_BYTES, stats.tx_bytes) ||
	    nla_put_uint(rsp, PSP_A_STATS_TX_ERROR, stats.tx_error))
		goto err_cancel_msg;

	genlmsg_end(rsp, hdr);
	return 0;

err_cancel_msg:
	genlmsg_cancel(rsp, hdr);
	return -EMSGSIZE;
}

int psp_nl_get_stats_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct psp_dev *psd = info->user_ptr[0];
	struct sk_buff *rsp;
	int err;

	rsp = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!rsp)
		return -ENOMEM;

	err = psp_nl_stats_fill(psd, rsp, info);
	if (err)
		goto err_free_msg;

	return genlmsg_reply(rsp, info);

err_free_msg:
	nlmsg_free(rsp);
	return err;
}

static int
psp_nl_stats_get_dumpit_one(struct sk_buff *rsp, struct netlink_callback *cb,
			    struct psp_dev *psd)
{
	if (psp_dev_check_access(psd, sock_net(rsp->sk), false))
		return 0;

	return psp_nl_stats_fill(psd, rsp, genl_info_dump(cb));
}

int psp_nl_get_stats_dumpit(struct sk_buff *rsp, struct netlink_callback *cb)
{
	struct psp_dev *psd;
	int err = 0;

	mutex_lock(&psp_devs_lock);
	xa_for_each_start(&psp_devs, cb->args[0], psd, cb->args[0]) {
		mutex_lock(&psd->lock);
		err = psp_nl_stats_get_dumpit_one(rsp, cb, psd);
		mutex_unlock(&psd->lock);
		if (err)
			break;
	}
	mutex_unlock(&psp_devs_lock);

	return err;
}
