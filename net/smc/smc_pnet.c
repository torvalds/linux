// SPDX-License-Identifier: GPL-2.0
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Generic netlink support functions to configure an SMC-R PNET table
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Thomas Richter <tmricht@linux.vnet.ibm.com>
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/ctype.h>
#include <linux/mutex.h>
#include <net/netlink.h>
#include <net/genetlink.h>

#include <uapi/linux/if.h>
#include <uapi/linux/smc.h>

#include <rdma/ib_verbs.h>

#include <net/netns/generic.h>
#include "smc_netns.h"

#include "smc_pnet.h"
#include "smc_ib.h"
#include "smc_ism.h"
#include "smc_core.h"

static struct net_device *__pnet_find_base_ndev(struct net_device *ndev);
static struct net_device *pnet_find_base_ndev(struct net_device *ndev);

static const struct nla_policy smc_pnet_policy[SMC_PNETID_MAX + 1] = {
	[SMC_PNETID_NAME] = {
		.type = NLA_NUL_STRING,
		.len = SMC_MAX_PNETID_LEN
	},
	[SMC_PNETID_ETHNAME] = {
		.type = NLA_NUL_STRING,
		.len = IFNAMSIZ - 1
	},
	[SMC_PNETID_IBNAME] = {
		.type = NLA_NUL_STRING,
		.len = IB_DEVICE_NAME_MAX - 1
	},
	[SMC_PNETID_IBPORT] = { .type = NLA_U8 }
};

static struct genl_family smc_pnet_nl_family;

enum smc_pnet_nametype {
	SMC_PNET_ETH	= 1,
	SMC_PNET_IB	= 2,
};

/* pnet entry stored in pnet table */
struct smc_pnetentry {
	struct list_head list;
	char pnet_name[SMC_MAX_PNETID_LEN + 1];
	enum smc_pnet_nametype type;
	union {
		struct {
			char eth_name[IFNAMSIZ + 1];
			struct net_device *ndev;
			netdevice_tracker dev_tracker;
		};
		struct {
			char ib_name[IB_DEVICE_NAME_MAX + 1];
			u8 ib_port;
		};
	};
};

/* Check if the pnetid is set */
bool smc_pnet_is_pnetid_set(u8 *pnetid)
{
	if (pnetid[0] == 0 || pnetid[0] == _S)
		return false;
	return true;
}

/* Check if two given pnetids match */
static bool smc_pnet_match(u8 *pnetid1, u8 *pnetid2)
{
	int i;

	for (i = 0; i < SMC_MAX_PNETID_LEN; i++) {
		if ((pnetid1[i] == 0 || pnetid1[i] == _S) &&
		    (pnetid2[i] == 0 || pnetid2[i] == _S))
			break;
		if (pnetid1[i] != pnetid2[i])
			return false;
	}
	return true;
}

/* Remove a pnetid from the pnet table.
 */
static int smc_pnet_remove_by_pnetid(struct net *net, char *pnet_name)
{
	struct smc_pnetentry *pnetelem, *tmp_pe;
	struct smc_pnettable *pnettable;
	struct smc_ib_device *ibdev;
	struct smcd_dev *smcd_dev;
	struct smc_net *sn;
	int rc = -ENOENT;
	int ibport;

	/* get pnettable for namespace */
	sn = net_generic(net, smc_net_id);
	pnettable = &sn->pnettable;

	/* remove table entry */
	mutex_lock(&pnettable->lock);
	list_for_each_entry_safe(pnetelem, tmp_pe, &pnettable->pnetlist,
				 list) {
		if (!pnet_name ||
		    smc_pnet_match(pnetelem->pnet_name, pnet_name)) {
			list_del(&pnetelem->list);
			if (pnetelem->type == SMC_PNET_ETH && pnetelem->ndev) {
				netdev_put(pnetelem->ndev,
					   &pnetelem->dev_tracker);
				pr_warn_ratelimited("smc: net device %s "
						    "erased user defined "
						    "pnetid %.16s\n",
						    pnetelem->eth_name,
						    pnetelem->pnet_name);
			}
			kfree(pnetelem);
			rc = 0;
		}
	}
	mutex_unlock(&pnettable->lock);

	/* if this is not the initial namespace, stop here */
	if (net != &init_net)
		return rc;

	/* remove ib devices */
	mutex_lock(&smc_ib_devices.mutex);
	list_for_each_entry(ibdev, &smc_ib_devices.list, list) {
		for (ibport = 0; ibport < SMC_MAX_PORTS; ibport++) {
			if (ibdev->pnetid_by_user[ibport] &&
			    (!pnet_name ||
			     smc_pnet_match(pnet_name,
					    ibdev->pnetid[ibport]))) {
				pr_warn_ratelimited("smc: ib device %s ibport "
						    "%d erased user defined "
						    "pnetid %.16s\n",
						    ibdev->ibdev->name,
						    ibport + 1,
						    ibdev->pnetid[ibport]);
				memset(ibdev->pnetid[ibport], 0,
				       SMC_MAX_PNETID_LEN);
				ibdev->pnetid_by_user[ibport] = false;
				rc = 0;
			}
		}
	}
	mutex_unlock(&smc_ib_devices.mutex);
	/* remove smcd devices */
	mutex_lock(&smcd_dev_list.mutex);
	list_for_each_entry(smcd_dev, &smcd_dev_list.list, list) {
		if (smcd_dev->pnetid_by_user &&
		    (!pnet_name ||
		     smc_pnet_match(pnet_name, smcd_dev->pnetid))) {
			pr_warn_ratelimited("smc: smcd device %s "
					    "erased user defined pnetid "
					    "%.16s\n", dev_name(&smcd_dev->dev),
					    smcd_dev->pnetid);
			memset(smcd_dev->pnetid, 0, SMC_MAX_PNETID_LEN);
			smcd_dev->pnetid_by_user = false;
			rc = 0;
		}
	}
	mutex_unlock(&smcd_dev_list.mutex);
	return rc;
}

/* Add the reference to a given network device to the pnet table.
 */
static int smc_pnet_add_by_ndev(struct net_device *ndev)
{
	struct smc_pnetentry *pnetelem, *tmp_pe;
	struct smc_pnettable *pnettable;
	struct net *net = dev_net(ndev);
	struct smc_net *sn;
	int rc = -ENOENT;

	/* get pnettable for namespace */
	sn = net_generic(net, smc_net_id);
	pnettable = &sn->pnettable;

	mutex_lock(&pnettable->lock);
	list_for_each_entry_safe(pnetelem, tmp_pe, &pnettable->pnetlist, list) {
		if (pnetelem->type == SMC_PNET_ETH && !pnetelem->ndev &&
		    !strncmp(pnetelem->eth_name, ndev->name, IFNAMSIZ)) {
			netdev_hold(ndev, &pnetelem->dev_tracker, GFP_ATOMIC);
			pnetelem->ndev = ndev;
			rc = 0;
			pr_warn_ratelimited("smc: adding net device %s with "
					    "user defined pnetid %.16s\n",
					    pnetelem->eth_name,
					    pnetelem->pnet_name);
			break;
		}
	}
	mutex_unlock(&pnettable->lock);
	return rc;
}

/* Remove the reference to a given network device from the pnet table.
 */
static int smc_pnet_remove_by_ndev(struct net_device *ndev)
{
	struct smc_pnetentry *pnetelem, *tmp_pe;
	struct smc_pnettable *pnettable;
	struct net *net = dev_net(ndev);
	struct smc_net *sn;
	int rc = -ENOENT;

	/* get pnettable for namespace */
	sn = net_generic(net, smc_net_id);
	pnettable = &sn->pnettable;

	mutex_lock(&pnettable->lock);
	list_for_each_entry_safe(pnetelem, tmp_pe, &pnettable->pnetlist, list) {
		if (pnetelem->type == SMC_PNET_ETH && pnetelem->ndev == ndev) {
			netdev_put(pnetelem->ndev, &pnetelem->dev_tracker);
			pnetelem->ndev = NULL;
			rc = 0;
			pr_warn_ratelimited("smc: removing net device %s with "
					    "user defined pnetid %.16s\n",
					    pnetelem->eth_name,
					    pnetelem->pnet_name);
			break;
		}
	}
	mutex_unlock(&pnettable->lock);
	return rc;
}

/* Apply pnetid to ib device when no pnetid is set.
 */
static bool smc_pnet_apply_ib(struct smc_ib_device *ib_dev, u8 ib_port,
			      char *pnet_name)
{
	bool applied = false;

	mutex_lock(&smc_ib_devices.mutex);
	if (!smc_pnet_is_pnetid_set(ib_dev->pnetid[ib_port - 1])) {
		memcpy(ib_dev->pnetid[ib_port - 1], pnet_name,
		       SMC_MAX_PNETID_LEN);
		ib_dev->pnetid_by_user[ib_port - 1] = true;
		applied = true;
	}
	mutex_unlock(&smc_ib_devices.mutex);
	return applied;
}

/* Apply pnetid to smcd device when no pnetid is set.
 */
static bool smc_pnet_apply_smcd(struct smcd_dev *smcd_dev, char *pnet_name)
{
	bool applied = false;

	mutex_lock(&smcd_dev_list.mutex);
	if (!smc_pnet_is_pnetid_set(smcd_dev->pnetid)) {
		memcpy(smcd_dev->pnetid, pnet_name, SMC_MAX_PNETID_LEN);
		smcd_dev->pnetid_by_user = true;
		applied = true;
	}
	mutex_unlock(&smcd_dev_list.mutex);
	return applied;
}

/* The limit for pnetid is 16 characters.
 * Valid characters should be (single-byte character set) a-z, A-Z, 0-9.
 * Lower case letters are converted to upper case.
 * Interior blanks should not be used.
 */
static bool smc_pnetid_valid(const char *pnet_name, char *pnetid)
{
	char *bf = skip_spaces(pnet_name);
	size_t len = strlen(bf);
	char *end = bf + len;

	if (!len)
		return false;
	while (--end >= bf && isspace(*end))
		;
	if (end - bf >= SMC_MAX_PNETID_LEN)
		return false;
	while (bf <= end) {
		if (!isalnum(*bf))
			return false;
		*pnetid++ = islower(*bf) ? toupper(*bf) : *bf;
		bf++;
	}
	*pnetid = '\0';
	return true;
}

/* Find an infiniband device by a given name. The device might not exist. */
static struct smc_ib_device *smc_pnet_find_ib(char *ib_name)
{
	struct smc_ib_device *ibdev;

	mutex_lock(&smc_ib_devices.mutex);
	list_for_each_entry(ibdev, &smc_ib_devices.list, list) {
		if (!strncmp(ibdev->ibdev->name, ib_name,
			     sizeof(ibdev->ibdev->name)) ||
		    (ibdev->ibdev->dev.parent &&
		     !strncmp(dev_name(ibdev->ibdev->dev.parent), ib_name,
			     IB_DEVICE_NAME_MAX - 1))) {
			goto out;
		}
	}
	ibdev = NULL;
out:
	mutex_unlock(&smc_ib_devices.mutex);
	return ibdev;
}

/* Find an smcd device by a given name. The device might not exist. */
static struct smcd_dev *smc_pnet_find_smcd(char *smcd_name)
{
	struct smcd_dev *smcd_dev;

	mutex_lock(&smcd_dev_list.mutex);
	list_for_each_entry(smcd_dev, &smcd_dev_list.list, list) {
		if (!strncmp(dev_name(&smcd_dev->dev), smcd_name,
			     IB_DEVICE_NAME_MAX - 1))
			goto out;
	}
	smcd_dev = NULL;
out:
	mutex_unlock(&smcd_dev_list.mutex);
	return smcd_dev;
}

static int smc_pnet_add_eth(struct smc_pnettable *pnettable, struct net *net,
			    char *eth_name, char *pnet_name)
{
	struct smc_pnetentry *tmp_pe, *new_pe;
	struct net_device *ndev, *base_ndev;
	u8 ndev_pnetid[SMC_MAX_PNETID_LEN];
	bool new_netdev;
	int rc;

	/* check if (base) netdev already has a pnetid. If there is one, we do
	 * not want to add a pnet table entry
	 */
	rc = -EEXIST;
	ndev = dev_get_by_name(net, eth_name);	/* dev_hold() */
	if (ndev) {
		base_ndev = pnet_find_base_ndev(ndev);
		if (!smc_pnetid_by_dev_port(base_ndev->dev.parent,
					    base_ndev->dev_port, ndev_pnetid))
			goto out_put;
	}

	/* add a new netdev entry to the pnet table if there isn't one */
	rc = -ENOMEM;
	new_pe = kzalloc(sizeof(*new_pe), GFP_KERNEL);
	if (!new_pe)
		goto out_put;
	new_pe->type = SMC_PNET_ETH;
	memcpy(new_pe->pnet_name, pnet_name, SMC_MAX_PNETID_LEN);
	strncpy(new_pe->eth_name, eth_name, IFNAMSIZ);
	rc = -EEXIST;
	new_netdev = true;
	mutex_lock(&pnettable->lock);
	list_for_each_entry(tmp_pe, &pnettable->pnetlist, list) {
		if (tmp_pe->type == SMC_PNET_ETH &&
		    !strncmp(tmp_pe->eth_name, eth_name, IFNAMSIZ)) {
			new_netdev = false;
			break;
		}
	}
	if (new_netdev) {
		if (ndev) {
			new_pe->ndev = ndev;
			netdev_tracker_alloc(ndev, &new_pe->dev_tracker,
					     GFP_ATOMIC);
		}
		list_add_tail(&new_pe->list, &pnettable->pnetlist);
		mutex_unlock(&pnettable->lock);
	} else {
		mutex_unlock(&pnettable->lock);
		kfree(new_pe);
		goto out_put;
	}
	if (ndev)
		pr_warn_ratelimited("smc: net device %s "
				    "applied user defined pnetid %.16s\n",
				    new_pe->eth_name, new_pe->pnet_name);
	return 0;

out_put:
	dev_put(ndev);
	return rc;
}

static int smc_pnet_add_ib(struct smc_pnettable *pnettable, char *ib_name,
			   u8 ib_port, char *pnet_name)
{
	struct smc_pnetentry *tmp_pe, *new_pe;
	struct smc_ib_device *ib_dev;
	bool smcddev_applied = true;
	bool ibdev_applied = true;
	struct smcd_dev *smcd_dev;
	bool new_ibdev;

	/* try to apply the pnetid to active devices */
	ib_dev = smc_pnet_find_ib(ib_name);
	if (ib_dev) {
		ibdev_applied = smc_pnet_apply_ib(ib_dev, ib_port, pnet_name);
		if (ibdev_applied)
			pr_warn_ratelimited("smc: ib device %s ibport %d "
					    "applied user defined pnetid "
					    "%.16s\n", ib_dev->ibdev->name,
					    ib_port,
					    ib_dev->pnetid[ib_port - 1]);
	}
	smcd_dev = smc_pnet_find_smcd(ib_name);
	if (smcd_dev) {
		smcddev_applied = smc_pnet_apply_smcd(smcd_dev, pnet_name);
		if (smcddev_applied)
			pr_warn_ratelimited("smc: smcd device %s "
					    "applied user defined pnetid "
					    "%.16s\n", dev_name(&smcd_dev->dev),
					    smcd_dev->pnetid);
	}
	/* Apply fails when a device has a hardware-defined pnetid set, do not
	 * add a pnet table entry in that case.
	 */
	if (!ibdev_applied || !smcddev_applied)
		return -EEXIST;

	/* add a new ib entry to the pnet table if there isn't one */
	new_pe = kzalloc(sizeof(*new_pe), GFP_KERNEL);
	if (!new_pe)
		return -ENOMEM;
	new_pe->type = SMC_PNET_IB;
	memcpy(new_pe->pnet_name, pnet_name, SMC_MAX_PNETID_LEN);
	strncpy(new_pe->ib_name, ib_name, IB_DEVICE_NAME_MAX);
	new_pe->ib_port = ib_port;

	new_ibdev = true;
	mutex_lock(&pnettable->lock);
	list_for_each_entry(tmp_pe, &pnettable->pnetlist, list) {
		if (tmp_pe->type == SMC_PNET_IB &&
		    !strncmp(tmp_pe->ib_name, ib_name, IB_DEVICE_NAME_MAX)) {
			new_ibdev = false;
			break;
		}
	}
	if (new_ibdev) {
		list_add_tail(&new_pe->list, &pnettable->pnetlist);
		mutex_unlock(&pnettable->lock);
	} else {
		mutex_unlock(&pnettable->lock);
		kfree(new_pe);
	}
	return (new_ibdev) ? 0 : -EEXIST;
}

/* Append a pnetid to the end of the pnet table if not already on this list.
 */
static int smc_pnet_enter(struct net *net, struct nlattr *tb[])
{
	char pnet_name[SMC_MAX_PNETID_LEN + 1];
	struct smc_pnettable *pnettable;
	bool new_netdev = false;
	bool new_ibdev = false;
	struct smc_net *sn;
	u8 ibport = 1;
	char *string;
	int rc;

	/* get pnettable for namespace */
	sn = net_generic(net, smc_net_id);
	pnettable = &sn->pnettable;

	rc = -EINVAL;
	if (!tb[SMC_PNETID_NAME])
		goto error;
	string = (char *)nla_data(tb[SMC_PNETID_NAME]);
	if (!smc_pnetid_valid(string, pnet_name))
		goto error;

	if (tb[SMC_PNETID_ETHNAME]) {
		string = (char *)nla_data(tb[SMC_PNETID_ETHNAME]);
		rc = smc_pnet_add_eth(pnettable, net, string, pnet_name);
		if (!rc)
			new_netdev = true;
		else if (rc != -EEXIST)
			goto error;
	}

	/* if this is not the initial namespace, stop here */
	if (net != &init_net)
		return new_netdev ? 0 : -EEXIST;

	rc = -EINVAL;
	if (tb[SMC_PNETID_IBNAME]) {
		string = (char *)nla_data(tb[SMC_PNETID_IBNAME]);
		string = strim(string);
		if (tb[SMC_PNETID_IBPORT]) {
			ibport = nla_get_u8(tb[SMC_PNETID_IBPORT]);
			if (ibport < 1 || ibport > SMC_MAX_PORTS)
				goto error;
		}
		rc = smc_pnet_add_ib(pnettable, string, ibport, pnet_name);
		if (!rc)
			new_ibdev = true;
		else if (rc != -EEXIST)
			goto error;
	}
	return (new_netdev || new_ibdev) ? 0 : -EEXIST;

error:
	return rc;
}

/* Convert an smc_pnetentry to a netlink attribute sequence */
static int smc_pnet_set_nla(struct sk_buff *msg,
			    struct smc_pnetentry *pnetelem)
{
	if (nla_put_string(msg, SMC_PNETID_NAME, pnetelem->pnet_name))
		return -1;
	if (pnetelem->type == SMC_PNET_ETH) {
		if (nla_put_string(msg, SMC_PNETID_ETHNAME,
				   pnetelem->eth_name))
			return -1;
	} else {
		if (nla_put_string(msg, SMC_PNETID_ETHNAME, "n/a"))
			return -1;
	}
	if (pnetelem->type == SMC_PNET_IB) {
		if (nla_put_string(msg, SMC_PNETID_IBNAME, pnetelem->ib_name) ||
		    nla_put_u8(msg, SMC_PNETID_IBPORT, pnetelem->ib_port))
			return -1;
	} else {
		if (nla_put_string(msg, SMC_PNETID_IBNAME, "n/a") ||
		    nla_put_u8(msg, SMC_PNETID_IBPORT, 0xff))
			return -1;
	}

	return 0;
}

static int smc_pnet_add(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);

	return smc_pnet_enter(net, info->attrs);
}

static int smc_pnet_del(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);

	if (!info->attrs[SMC_PNETID_NAME])
		return -EINVAL;
	return smc_pnet_remove_by_pnetid(net,
				(char *)nla_data(info->attrs[SMC_PNETID_NAME]));
}

static int smc_pnet_dump_start(struct netlink_callback *cb)
{
	cb->args[0] = 0;
	return 0;
}

static int smc_pnet_dumpinfo(struct sk_buff *skb,
			     u32 portid, u32 seq, u32 flags,
			     struct smc_pnetentry *pnetelem)
{
	void *hdr;

	hdr = genlmsg_put(skb, portid, seq, &smc_pnet_nl_family,
			  flags, SMC_PNETID_GET);
	if (!hdr)
		return -ENOMEM;
	if (smc_pnet_set_nla(skb, pnetelem) < 0) {
		genlmsg_cancel(skb, hdr);
		return -EMSGSIZE;
	}
	genlmsg_end(skb, hdr);
	return 0;
}

static int _smc_pnet_dump(struct net *net, struct sk_buff *skb, u32 portid,
			  u32 seq, u8 *pnetid, int start_idx)
{
	struct smc_pnettable *pnettable;
	struct smc_pnetentry *pnetelem;
	struct smc_net *sn;
	int idx = 0;

	/* get pnettable for namespace */
	sn = net_generic(net, smc_net_id);
	pnettable = &sn->pnettable;

	/* dump pnettable entries */
	mutex_lock(&pnettable->lock);
	list_for_each_entry(pnetelem, &pnettable->pnetlist, list) {
		if (pnetid && !smc_pnet_match(pnetelem->pnet_name, pnetid))
			continue;
		if (idx++ < start_idx)
			continue;
		/* if this is not the initial namespace, dump only netdev */
		if (net != &init_net && pnetelem->type != SMC_PNET_ETH)
			continue;
		if (smc_pnet_dumpinfo(skb, portid, seq, NLM_F_MULTI,
				      pnetelem)) {
			--idx;
			break;
		}
	}
	mutex_unlock(&pnettable->lock);
	return idx;
}

static int smc_pnet_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	int idx;

	idx = _smc_pnet_dump(net, skb, NETLINK_CB(cb->skb).portid,
			     cb->nlh->nlmsg_seq, NULL, cb->args[0]);

	cb->args[0] = idx;
	return skb->len;
}

/* Retrieve one PNETID entry */
static int smc_pnet_get(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct sk_buff *msg;
	void *hdr;

	if (!info->attrs[SMC_PNETID_NAME])
		return -EINVAL;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	_smc_pnet_dump(net, msg, info->snd_portid, info->snd_seq,
		       nla_data(info->attrs[SMC_PNETID_NAME]), 0);

	/* finish multi part message and send it */
	hdr = nlmsg_put(msg, info->snd_portid, info->snd_seq, NLMSG_DONE, 0,
			NLM_F_MULTI);
	if (!hdr) {
		nlmsg_free(msg);
		return -EMSGSIZE;
	}
	return genlmsg_reply(msg, info);
}

/* Remove and delete all pnetids from pnet table.
 */
static int smc_pnet_flush(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);

	smc_pnet_remove_by_pnetid(net, NULL);
	return 0;
}

/* SMC_PNETID generic netlink operation definition */
static const struct genl_ops smc_pnet_ops[] = {
	{
		.cmd = SMC_PNETID_GET,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		/* can be retrieved by unprivileged users */
		.doit = smc_pnet_get,
		.dumpit = smc_pnet_dump,
		.start = smc_pnet_dump_start
	},
	{
		.cmd = SMC_PNETID_ADD,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.flags = GENL_ADMIN_PERM,
		.doit = smc_pnet_add
	},
	{
		.cmd = SMC_PNETID_DEL,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.flags = GENL_ADMIN_PERM,
		.doit = smc_pnet_del
	},
	{
		.cmd = SMC_PNETID_FLUSH,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.flags = GENL_ADMIN_PERM,
		.doit = smc_pnet_flush
	}
};

/* SMC_PNETID family definition */
static struct genl_family smc_pnet_nl_family __ro_after_init = {
	.hdrsize = 0,
	.name = SMCR_GENL_FAMILY_NAME,
	.version = SMCR_GENL_FAMILY_VERSION,
	.maxattr = SMC_PNETID_MAX,
	.policy = smc_pnet_policy,
	.netnsok = true,
	.module = THIS_MODULE,
	.ops = smc_pnet_ops,
	.n_ops =  ARRAY_SIZE(smc_pnet_ops),
	.resv_start_op = SMC_PNETID_FLUSH + 1,
};

bool smc_pnet_is_ndev_pnetid(struct net *net, u8 *pnetid)
{
	struct smc_net *sn = net_generic(net, smc_net_id);
	struct smc_pnetids_ndev_entry *pe;
	bool rc = false;

	read_lock(&sn->pnetids_ndev.lock);
	list_for_each_entry(pe, &sn->pnetids_ndev.list, list) {
		if (smc_pnet_match(pnetid, pe->pnetid)) {
			rc = true;
			goto unlock;
		}
	}

unlock:
	read_unlock(&sn->pnetids_ndev.lock);
	return rc;
}

static int smc_pnet_add_pnetid(struct net *net, u8 *pnetid)
{
	struct smc_net *sn = net_generic(net, smc_net_id);
	struct smc_pnetids_ndev_entry *pe, *pi;

	pe = kzalloc(sizeof(*pe), GFP_KERNEL);
	if (!pe)
		return -ENOMEM;

	write_lock(&sn->pnetids_ndev.lock);
	list_for_each_entry(pi, &sn->pnetids_ndev.list, list) {
		if (smc_pnet_match(pnetid, pe->pnetid)) {
			refcount_inc(&pi->refcnt);
			kfree(pe);
			goto unlock;
		}
	}
	refcount_set(&pe->refcnt, 1);
	memcpy(pe->pnetid, pnetid, SMC_MAX_PNETID_LEN);
	list_add_tail(&pe->list, &sn->pnetids_ndev.list);

unlock:
	write_unlock(&sn->pnetids_ndev.lock);
	return 0;
}

static void smc_pnet_remove_pnetid(struct net *net, u8 *pnetid)
{
	struct smc_net *sn = net_generic(net, smc_net_id);
	struct smc_pnetids_ndev_entry *pe, *pe2;

	write_lock(&sn->pnetids_ndev.lock);
	list_for_each_entry_safe(pe, pe2, &sn->pnetids_ndev.list, list) {
		if (smc_pnet_match(pnetid, pe->pnetid)) {
			if (refcount_dec_and_test(&pe->refcnt)) {
				list_del(&pe->list);
				kfree(pe);
			}
			break;
		}
	}
	write_unlock(&sn->pnetids_ndev.lock);
}

static void smc_pnet_add_base_pnetid(struct net *net, struct net_device *dev,
				     u8 *ndev_pnetid)
{
	struct net_device *base_dev;

	base_dev = __pnet_find_base_ndev(dev);
	if (base_dev->flags & IFF_UP &&
	    !smc_pnetid_by_dev_port(base_dev->dev.parent, base_dev->dev_port,
				    ndev_pnetid)) {
		/* add to PNETIDs list */
		smc_pnet_add_pnetid(net, ndev_pnetid);
	}
}

/* create initial list of netdevice pnetids */
static void smc_pnet_create_pnetids_list(struct net *net)
{
	u8 ndev_pnetid[SMC_MAX_PNETID_LEN];
	struct net_device *dev;

	/* Newly created netns do not have devices.
	 * Do not even acquire rtnl.
	 */
	if (list_empty(&net->dev_base_head))
		return;

	/* Note: This might not be needed, because smc_pnet_netdev_event()
	 * is also calling smc_pnet_add_base_pnetid() when handling
	 * NETDEV_UP event.
	 */
	rtnl_lock();
	for_each_netdev(net, dev)
		smc_pnet_add_base_pnetid(net, dev, ndev_pnetid);
	rtnl_unlock();
}

/* clean up list of netdevice pnetids */
static void smc_pnet_destroy_pnetids_list(struct net *net)
{
	struct smc_net *sn = net_generic(net, smc_net_id);
	struct smc_pnetids_ndev_entry *pe, *temp_pe;

	write_lock(&sn->pnetids_ndev.lock);
	list_for_each_entry_safe(pe, temp_pe, &sn->pnetids_ndev.list, list) {
		list_del(&pe->list);
		kfree(pe);
	}
	write_unlock(&sn->pnetids_ndev.lock);
}

static int smc_pnet_netdev_event(struct notifier_block *this,
				 unsigned long event, void *ptr)
{
	struct net_device *event_dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(event_dev);
	u8 ndev_pnetid[SMC_MAX_PNETID_LEN];

	switch (event) {
	case NETDEV_REBOOT:
	case NETDEV_UNREGISTER:
		smc_pnet_remove_by_ndev(event_dev);
		smc_ib_ndev_change(event_dev, event);
		return NOTIFY_OK;
	case NETDEV_REGISTER:
		smc_pnet_add_by_ndev(event_dev);
		smc_ib_ndev_change(event_dev, event);
		return NOTIFY_OK;
	case NETDEV_UP:
		smc_pnet_add_base_pnetid(net, event_dev, ndev_pnetid);
		return NOTIFY_OK;
	case NETDEV_DOWN:
		event_dev = __pnet_find_base_ndev(event_dev);
		if (!smc_pnetid_by_dev_port(event_dev->dev.parent,
					    event_dev->dev_port, ndev_pnetid)) {
			/* remove from PNETIDs list */
			smc_pnet_remove_pnetid(net, ndev_pnetid);
		}
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

static struct notifier_block smc_netdev_notifier = {
	.notifier_call = smc_pnet_netdev_event
};

/* init network namespace */
int smc_pnet_net_init(struct net *net)
{
	struct smc_net *sn = net_generic(net, smc_net_id);
	struct smc_pnettable *pnettable = &sn->pnettable;
	struct smc_pnetids_ndev *pnetids_ndev = &sn->pnetids_ndev;

	INIT_LIST_HEAD(&pnettable->pnetlist);
	mutex_init(&pnettable->lock);
	INIT_LIST_HEAD(&pnetids_ndev->list);
	rwlock_init(&pnetids_ndev->lock);

	smc_pnet_create_pnetids_list(net);

	/* disable handshake limitation by default */
	net->smc.limit_smc_hs = 0;

	return 0;
}

int __init smc_pnet_init(void)
{
	int rc;

	rc = genl_register_family(&smc_pnet_nl_family);
	if (rc)
		return rc;
	rc = register_netdevice_notifier(&smc_netdev_notifier);
	if (rc)
		genl_unregister_family(&smc_pnet_nl_family);

	return rc;
}

/* exit network namespace */
void smc_pnet_net_exit(struct net *net)
{
	/* flush pnet table */
	smc_pnet_remove_by_pnetid(net, NULL);
	smc_pnet_destroy_pnetids_list(net);
}

void smc_pnet_exit(void)
{
	unregister_netdevice_notifier(&smc_netdev_notifier);
	genl_unregister_family(&smc_pnet_nl_family);
}

static struct net_device *__pnet_find_base_ndev(struct net_device *ndev)
{
	int i, nest_lvl;

	ASSERT_RTNL();
	nest_lvl = ndev->lower_level;
	for (i = 0; i < nest_lvl; i++) {
		struct list_head *lower = &ndev->adj_list.lower;

		if (list_empty(lower))
			break;
		lower = lower->next;
		ndev = netdev_lower_get_next(ndev, &lower);
	}
	return ndev;
}

/* Determine one base device for stacked net devices.
 * If the lower device level contains more than one devices
 * (for instance with bonding slaves), just the first device
 * is used to reach a base device.
 */
static struct net_device *pnet_find_base_ndev(struct net_device *ndev)
{
	rtnl_lock();
	ndev = __pnet_find_base_ndev(ndev);
	rtnl_unlock();
	return ndev;
}

static int smc_pnet_find_ndev_pnetid_by_table(struct net_device *ndev,
					      u8 *pnetid)
{
	struct smc_pnettable *pnettable;
	struct net *net = dev_net(ndev);
	struct smc_pnetentry *pnetelem;
	struct smc_net *sn;
	int rc = -ENOENT;

	/* get pnettable for namespace */
	sn = net_generic(net, smc_net_id);
	pnettable = &sn->pnettable;

	mutex_lock(&pnettable->lock);
	list_for_each_entry(pnetelem, &pnettable->pnetlist, list) {
		if (pnetelem->type == SMC_PNET_ETH && ndev == pnetelem->ndev) {
			/* get pnetid of netdev device */
			memcpy(pnetid, pnetelem->pnet_name, SMC_MAX_PNETID_LEN);
			rc = 0;
			break;
		}
	}
	mutex_unlock(&pnettable->lock);
	return rc;
}

static int smc_pnet_determine_gid(struct smc_ib_device *ibdev, int i,
				  struct smc_init_info *ini)
{
	if (!ini->check_smcrv2 &&
	    !smc_ib_determine_gid(ibdev, i, ini->vlan_id, ini->ib_gid, NULL,
				  NULL)) {
		ini->ib_dev = ibdev;
		ini->ib_port = i;
		return 0;
	}
	if (ini->check_smcrv2 &&
	    !smc_ib_determine_gid(ibdev, i, ini->vlan_id, ini->smcrv2.ib_gid_v2,
				  NULL, &ini->smcrv2)) {
		ini->smcrv2.ib_dev_v2 = ibdev;
		ini->smcrv2.ib_port_v2 = i;
		return 0;
	}
	return -ENODEV;
}

/* find a roce device for the given pnetid */
static void _smc_pnet_find_roce_by_pnetid(u8 *pnet_id,
					  struct smc_init_info *ini,
					  struct smc_ib_device *known_dev,
					  struct net *net)
{
	struct smc_ib_device *ibdev;
	int i;

	mutex_lock(&smc_ib_devices.mutex);
	list_for_each_entry(ibdev, &smc_ib_devices.list, list) {
		if (ibdev == known_dev ||
		    !rdma_dev_access_netns(ibdev->ibdev, net))
			continue;
		for (i = 1; i <= SMC_MAX_PORTS; i++) {
			if (!rdma_is_port_valid(ibdev->ibdev, i))
				continue;
			if (smc_pnet_match(ibdev->pnetid[i - 1], pnet_id) &&
			    smc_ib_port_active(ibdev, i) &&
			    !test_bit(i - 1, ibdev->ports_going_away)) {
				if (!smc_pnet_determine_gid(ibdev, i, ini))
					goto out;
			}
		}
	}
out:
	mutex_unlock(&smc_ib_devices.mutex);
}

/* find alternate roce device with same pnet_id, vlan_id and net namespace */
void smc_pnet_find_alt_roce(struct smc_link_group *lgr,
			    struct smc_init_info *ini,
			    struct smc_ib_device *known_dev)
{
	struct net *net = lgr->net;

	_smc_pnet_find_roce_by_pnetid(lgr->pnet_id, ini, known_dev, net);
}

/* if handshake network device belongs to a roce device, return its
 * IB device and port
 */
static void smc_pnet_find_rdma_dev(struct net_device *netdev,
				   struct smc_init_info *ini)
{
	struct net *net = dev_net(netdev);
	struct smc_ib_device *ibdev;

	mutex_lock(&smc_ib_devices.mutex);
	list_for_each_entry(ibdev, &smc_ib_devices.list, list) {
		struct net_device *ndev;
		int i;

		/* check rdma net namespace */
		if (!rdma_dev_access_netns(ibdev->ibdev, net))
			continue;

		for (i = 1; i <= SMC_MAX_PORTS; i++) {
			if (!rdma_is_port_valid(ibdev->ibdev, i))
				continue;
			if (!ibdev->ibdev->ops.get_netdev)
				continue;
			ndev = ibdev->ibdev->ops.get_netdev(ibdev->ibdev, i);
			if (!ndev)
				continue;
			dev_put(ndev);
			if (netdev == ndev &&
			    smc_ib_port_active(ibdev, i) &&
			    !test_bit(i - 1, ibdev->ports_going_away)) {
				if (!smc_pnet_determine_gid(ibdev, i, ini))
					break;
			}
		}
	}
	mutex_unlock(&smc_ib_devices.mutex);
}

/* Determine the corresponding IB device port based on the hardware PNETID.
 * Searching stops at the first matching active IB device port with vlan_id
 * configured.
 * If nothing found, check pnetid table.
 * If nothing found, try to use handshake device
 */
static void smc_pnet_find_roce_by_pnetid(struct net_device *ndev,
					 struct smc_init_info *ini)
{
	u8 ndev_pnetid[SMC_MAX_PNETID_LEN];
	struct net *net;

	ndev = pnet_find_base_ndev(ndev);
	net = dev_net(ndev);
	if (smc_pnetid_by_dev_port(ndev->dev.parent, ndev->dev_port,
				   ndev_pnetid) &&
	    smc_pnet_find_ndev_pnetid_by_table(ndev, ndev_pnetid)) {
		smc_pnet_find_rdma_dev(ndev, ini);
		return; /* pnetid could not be determined */
	}
	_smc_pnet_find_roce_by_pnetid(ndev_pnetid, ini, NULL, net);
}

static void smc_pnet_find_ism_by_pnetid(struct net_device *ndev,
					struct smc_init_info *ini)
{
	u8 ndev_pnetid[SMC_MAX_PNETID_LEN];
	struct smcd_dev *ismdev;

	ndev = pnet_find_base_ndev(ndev);
	if (smc_pnetid_by_dev_port(ndev->dev.parent, ndev->dev_port,
				   ndev_pnetid) &&
	    smc_pnet_find_ndev_pnetid_by_table(ndev, ndev_pnetid))
		return; /* pnetid could not be determined */

	mutex_lock(&smcd_dev_list.mutex);
	list_for_each_entry(ismdev, &smcd_dev_list.list, list) {
		if (smc_pnet_match(ismdev->pnetid, ndev_pnetid) &&
		    !ismdev->going_away &&
		    (!ini->ism_peer_gid[0] ||
		     !smc_ism_cantalk(ini->ism_peer_gid[0], ini->vlan_id,
				      ismdev))) {
			ini->ism_dev[0] = ismdev;
			break;
		}
	}
	mutex_unlock(&smcd_dev_list.mutex);
}

/* PNET table analysis for a given sock:
 * determine ib_device and port belonging to used internal TCP socket
 * ethernet interface.
 */
void smc_pnet_find_roce_resource(struct sock *sk, struct smc_init_info *ini)
{
	struct dst_entry *dst = sk_dst_get(sk);

	if (!dst)
		goto out;
	if (!dst->dev)
		goto out_rel;

	smc_pnet_find_roce_by_pnetid(dst->dev, ini);

out_rel:
	dst_release(dst);
out:
	return;
}

void smc_pnet_find_ism_resource(struct sock *sk, struct smc_init_info *ini)
{
	struct dst_entry *dst = sk_dst_get(sk);

	ini->ism_dev[0] = NULL;
	if (!dst)
		goto out;
	if (!dst->dev)
		goto out_rel;

	smc_pnet_find_ism_by_pnetid(dst->dev, ini);

out_rel:
	dst_release(dst);
out:
	return;
}

/* Lookup and apply a pnet table entry to the given ib device.
 */
int smc_pnetid_by_table_ib(struct smc_ib_device *smcibdev, u8 ib_port)
{
	char *ib_name = smcibdev->ibdev->name;
	struct smc_pnettable *pnettable;
	struct smc_pnetentry *tmp_pe;
	struct smc_net *sn;
	int rc = -ENOENT;

	/* get pnettable for init namespace */
	sn = net_generic(&init_net, smc_net_id);
	pnettable = &sn->pnettable;

	mutex_lock(&pnettable->lock);
	list_for_each_entry(tmp_pe, &pnettable->pnetlist, list) {
		if (tmp_pe->type == SMC_PNET_IB &&
		    !strncmp(tmp_pe->ib_name, ib_name, IB_DEVICE_NAME_MAX) &&
		    tmp_pe->ib_port == ib_port) {
			smc_pnet_apply_ib(smcibdev, ib_port, tmp_pe->pnet_name);
			rc = 0;
			break;
		}
	}
	mutex_unlock(&pnettable->lock);

	return rc;
}

/* Lookup and apply a pnet table entry to the given smcd device.
 */
int smc_pnetid_by_table_smcd(struct smcd_dev *smcddev)
{
	const char *ib_name = dev_name(&smcddev->dev);
	struct smc_pnettable *pnettable;
	struct smc_pnetentry *tmp_pe;
	struct smc_net *sn;
	int rc = -ENOENT;

	/* get pnettable for init namespace */
	sn = net_generic(&init_net, smc_net_id);
	pnettable = &sn->pnettable;

	mutex_lock(&pnettable->lock);
	list_for_each_entry(tmp_pe, &pnettable->pnetlist, list) {
		if (tmp_pe->type == SMC_PNET_IB &&
		    !strncmp(tmp_pe->ib_name, ib_name, IB_DEVICE_NAME_MAX)) {
			smc_pnet_apply_smcd(smcddev, tmp_pe->pnet_name);
			rc = 0;
			break;
		}
	}
	mutex_unlock(&pnettable->lock);

	return rc;
}
