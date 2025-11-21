// SPDX-License-Identifier: GPL-2.0-only

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

/* Device stuff */

static struct psp_dev *
psp_device_get_and_lock(struct net *net, struct nlattr *dev_id)
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

	err = psp_dev_check_access(psd, net);
	if (err) {
		mutex_unlock(&psd->lock);
		return ERR_PTR(err);
	}

	return psd;
}

int psp_device_get_locked(const struct genl_split_ops *ops,
			  struct sk_buff *skb, struct genl_info *info)
{
	if (GENL_REQ_ATTR_CHECK(info, PSP_A_DEV_ID))
		return -EINVAL;

	info->user_ptr[0] = psp_device_get_and_lock(genl_info_net(info),
						    info->attrs[PSP_A_DEV_ID]);
	return PTR_ERR_OR_ZERO(info->user_ptr[0]);
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

static int
psp_nl_dev_fill(struct psp_dev *psd, struct sk_buff *rsp,
		const struct genl_info *info)
{
	void *hdr;

	hdr = genlmsg_iput(rsp, info);
	if (!hdr)
		return -EMSGSIZE;

	if (nla_put_u32(rsp, PSP_A_DEV_ID, psd->id) ||
	    nla_put_u32(rsp, PSP_A_DEV_IFINDEX, psd->main_netdev->ifindex) ||
	    nla_put_u32(rsp, PSP_A_DEV_PSP_VERSIONS_CAP, psd->caps->versions) ||
	    nla_put_u32(rsp, PSP_A_DEV_PSP_VERSIONS_ENA, psd->config.versions))
		goto err_cancel_msg;

	genlmsg_end(rsp, hdr);
	return 0;

err_cancel_msg:
	genlmsg_cancel(rsp, hdr);
	return -EMSGSIZE;
}

void psp_nl_notify_dev(struct psp_dev *psd, u32 cmd)
{
	struct genl_info info;
	struct sk_buff *ntf;

	if (!genl_has_listeners(&psp_nl_family, dev_net(psd->main_netdev),
				PSP_NLGRP_MGMT))
		return;

	ntf = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!ntf)
		return;

	genl_info_init_ntf(&info, &psp_nl_family, cmd);
	if (psp_nl_dev_fill(psd, ntf, &info)) {
		nlmsg_free(ntf);
		return;
	}

	genlmsg_multicast_netns(&psp_nl_family, dev_net(psd->main_netdev), ntf,
				0, PSP_NLGRP_MGMT, GFP_KERNEL);
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
	if (psp_dev_check_access(psd, sock_net(rsp->sk)))
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

	nlmsg_end(ntf, (struct nlmsghdr *)ntf->data);
	genlmsg_multicast_netns(&psp_nl_family, dev_net(psd->main_netdev), ntf,
				0, PSP_NLGRP_USE, GFP_KERNEL);
	return psp_nl_reply_send(rsp, info);

err_free_ntf:
	nlmsg_free(ntf);
err_free_rsp:
	nlmsg_free(rsp);
	return err;
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
		err = psp_dev_check_access(psd, genl_info_net(info));
		if (err) {
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
		mutex_lock(&psd->lock);
		if (id && psd->id != nla_get_u32(id)) {
			mutex_unlock(&psd->lock);
			NL_SET_ERR_MSG_ATTR(info->extack, id,
					    "Device id vs socket mismatch");
			err = -EINVAL;
			goto err_psd_put;
		}

		psp_dev_put(psd);
	} else {
		psd = psp_device_get_and_lock(genl_info_net(info), id);
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
