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

#define SMC_ASCII_BLANK 32

static struct net_device *pnet_find_base_ndev(struct net_device *ndev);

static struct nla_policy smc_pnet_policy[SMC_PNETID_MAX + 1] = {
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

/**
 * struct smc_user_pnetentry - pnet identifier name entry for/from user
 * @list: List node.
 * @pnet_name: Pnet identifier name
 * @ndev: pointer to network device.
 * @smcibdev: Pointer to IB device.
 * @ib_port: Port of IB device.
 * @smcd_dev: Pointer to smcd device.
 */
struct smc_user_pnetentry {
	struct list_head list;
	char pnet_name[SMC_MAX_PNETID_LEN + 1];
	struct net_device *ndev;
	struct smc_ib_device *smcibdev;
	u8 ib_port;
	struct smcd_dev *smcd_dev;
};

/* pnet entry stored in pnet table */
struct smc_pnetentry {
	struct list_head list;
	char pnet_name[SMC_MAX_PNETID_LEN + 1];
	struct net_device *ndev;
};

/* Check if two given pnetids match */
static bool smc_pnet_match(u8 *pnetid1, u8 *pnetid2)
{
	int i;

	for (i = 0; i < SMC_MAX_PNETID_LEN; i++) {
		if ((pnetid1[i] == 0 || pnetid1[i] == SMC_ASCII_BLANK) &&
		    (pnetid2[i] == 0 || pnetid2[i] == SMC_ASCII_BLANK))
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

	/* remove netdevices */
	write_lock(&pnettable->lock);
	list_for_each_entry_safe(pnetelem, tmp_pe, &pnettable->pnetlist,
				 list) {
		if (!pnet_name ||
		    smc_pnet_match(pnetelem->pnet_name, pnet_name)) {
			list_del(&pnetelem->list);
			dev_put(pnetelem->ndev);
			kfree(pnetelem);
			rc = 0;
		}
	}
	write_unlock(&pnettable->lock);

	/* if this is not the initial namespace, stop here */
	if (net != &init_net)
		return rc;

	/* remove ib devices */
	spin_lock(&smc_ib_devices.lock);
	list_for_each_entry(ibdev, &smc_ib_devices.list, list) {
		for (ibport = 0; ibport < SMC_MAX_PORTS; ibport++) {
			if (ibdev->pnetid_by_user[ibport] &&
			    (!pnet_name ||
			     smc_pnet_match(pnet_name,
					    ibdev->pnetid[ibport]))) {
				memset(ibdev->pnetid[ibport], 0,
				       SMC_MAX_PNETID_LEN);
				ibdev->pnetid_by_user[ibport] = false;
				rc = 0;
			}
		}
	}
	spin_unlock(&smc_ib_devices.lock);
	/* remove smcd devices */
	spin_lock(&smcd_dev_list.lock);
	list_for_each_entry(smcd_dev, &smcd_dev_list.list, list) {
		if (smcd_dev->pnetid_by_user &&
		    (!pnet_name ||
		     smc_pnet_match(pnet_name, smcd_dev->pnetid))) {
			memset(smcd_dev->pnetid, 0, SMC_MAX_PNETID_LEN);
			smcd_dev->pnetid_by_user = false;
			rc = 0;
		}
	}
	spin_unlock(&smcd_dev_list.lock);
	return rc;
}

/* Remove a pnet entry mentioning a given network device from the pnet table.
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

	write_lock(&pnettable->lock);
	list_for_each_entry_safe(pnetelem, tmp_pe, &pnettable->pnetlist, list) {
		if (pnetelem->ndev == ndev) {
			list_del(&pnetelem->list);
			dev_put(pnetelem->ndev);
			kfree(pnetelem);
			rc = 0;
			break;
		}
	}
	write_unlock(&pnettable->lock);
	return rc;
}

/* Append a pnetid to the end of the pnet table if not already on this list.
 */
static int smc_pnet_enter(struct smc_pnettable *pnettable,
			  struct smc_user_pnetentry *new_pnetelem)
{
	u8 pnet_null[SMC_MAX_PNETID_LEN] = {0};
	u8 ndev_pnetid[SMC_MAX_PNETID_LEN];
	struct smc_pnetentry *tmp_pnetelem;
	struct smc_pnetentry *pnetelem;
	bool new_smcddev = false;
	struct net_device *ndev;
	bool new_netdev = true;
	bool new_ibdev = false;

	if (new_pnetelem->smcibdev) {
		struct smc_ib_device *ib_dev = new_pnetelem->smcibdev;
		int ib_port = new_pnetelem->ib_port;

		spin_lock(&smc_ib_devices.lock);
		if (smc_pnet_match(ib_dev->pnetid[ib_port - 1], pnet_null)) {
			memcpy(ib_dev->pnetid[ib_port - 1],
			       new_pnetelem->pnet_name, SMC_MAX_PNETID_LEN);
			ib_dev->pnetid_by_user[ib_port - 1] = true;
			new_ibdev = true;
		}
		spin_unlock(&smc_ib_devices.lock);
	}
	if (new_pnetelem->smcd_dev) {
		struct smcd_dev *smcd_dev = new_pnetelem->smcd_dev;

		spin_lock(&smcd_dev_list.lock);
		if (smc_pnet_match(smcd_dev->pnetid, pnet_null)) {
			memcpy(smcd_dev->pnetid, new_pnetelem->pnet_name,
			       SMC_MAX_PNETID_LEN);
			smcd_dev->pnetid_by_user = true;
			new_smcddev = true;
		}
		spin_unlock(&smcd_dev_list.lock);
	}

	if (!new_pnetelem->ndev)
		return (new_ibdev || new_smcddev) ? 0 : -EEXIST;

	/* check if (base) netdev already has a pnetid. If there is one, we do
	 * not want to add a pnet table entry
	 */
	ndev = pnet_find_base_ndev(new_pnetelem->ndev);
	if (!smc_pnetid_by_dev_port(ndev->dev.parent, ndev->dev_port,
				    ndev_pnetid))
		return (new_ibdev || new_smcddev) ? 0 : -EEXIST;

	/* add a new netdev entry to the pnet table if there isn't one */
	tmp_pnetelem = kzalloc(sizeof(*pnetelem), GFP_KERNEL);
	if (!tmp_pnetelem)
		return -ENOMEM;
	memcpy(tmp_pnetelem->pnet_name, new_pnetelem->pnet_name,
	       SMC_MAX_PNETID_LEN);
	tmp_pnetelem->ndev = new_pnetelem->ndev;

	write_lock(&pnettable->lock);
	list_for_each_entry(pnetelem, &pnettable->pnetlist, list) {
		if (pnetelem->ndev == new_pnetelem->ndev)
			new_netdev = false;
	}
	if (new_netdev) {
		dev_hold(tmp_pnetelem->ndev);
		list_add_tail(&tmp_pnetelem->list, &pnettable->pnetlist);
		write_unlock(&pnettable->lock);
	} else {
		write_unlock(&pnettable->lock);
		kfree(tmp_pnetelem);
	}

	return (new_netdev || new_ibdev || new_smcddev) ? 0 : -EEXIST;
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

	spin_lock(&smc_ib_devices.lock);
	list_for_each_entry(ibdev, &smc_ib_devices.list, list) {
		if (!strncmp(ibdev->ibdev->name, ib_name,
			     sizeof(ibdev->ibdev->name)) ||
		    !strncmp(dev_name(ibdev->ibdev->dev.parent), ib_name,
			     IB_DEVICE_NAME_MAX - 1)) {
			goto out;
		}
	}
	ibdev = NULL;
out:
	spin_unlock(&smc_ib_devices.lock);
	return ibdev;
}

/* Find an smcd device by a given name. The device might not exist. */
static struct smcd_dev *smc_pnet_find_smcd(char *smcd_name)
{
	struct smcd_dev *smcd_dev;

	spin_lock(&smcd_dev_list.lock);
	list_for_each_entry(smcd_dev, &smcd_dev_list.list, list) {
		if (!strncmp(dev_name(&smcd_dev->dev), smcd_name,
			     IB_DEVICE_NAME_MAX - 1))
			goto out;
	}
	smcd_dev = NULL;
out:
	spin_unlock(&smcd_dev_list.lock);
	return smcd_dev;
}

/* Parse the supplied netlink attributes and fill a pnetentry structure.
 * For ethernet and infiniband device names verify that the devices exist.
 */
static int smc_pnet_fill_entry(struct net *net,
			       struct smc_user_pnetentry *pnetelem,
			       struct nlattr *tb[])
{
	char *string, *ibname;
	int rc;

	memset(pnetelem, 0, sizeof(*pnetelem));
	INIT_LIST_HEAD(&pnetelem->list);

	rc = -EINVAL;
	if (!tb[SMC_PNETID_NAME])
		goto error;
	string = (char *)nla_data(tb[SMC_PNETID_NAME]);
	if (!smc_pnetid_valid(string, pnetelem->pnet_name))
		goto error;

	rc = -EINVAL;
	if (tb[SMC_PNETID_ETHNAME]) {
		string = (char *)nla_data(tb[SMC_PNETID_ETHNAME]);
		pnetelem->ndev = dev_get_by_name(net, string);
		if (!pnetelem->ndev)
			goto error;
	}

	/* if this is not the initial namespace, stop here */
	if (net != &init_net)
		return 0;

	rc = -EINVAL;
	if (tb[SMC_PNETID_IBNAME]) {
		ibname = (char *)nla_data(tb[SMC_PNETID_IBNAME]);
		ibname = strim(ibname);
		pnetelem->smcibdev = smc_pnet_find_ib(ibname);
		pnetelem->smcd_dev = smc_pnet_find_smcd(ibname);
		if (!pnetelem->smcibdev && !pnetelem->smcd_dev)
			goto error;
		if (pnetelem->smcibdev) {
			if (!tb[SMC_PNETID_IBPORT])
				goto error;
			pnetelem->ib_port = nla_get_u8(tb[SMC_PNETID_IBPORT]);
			if (pnetelem->ib_port < 1 ||
			    pnetelem->ib_port > SMC_MAX_PORTS)
				goto error;
		}
	}

	return 0;

error:
	return rc;
}

/* Convert an smc_pnetentry to a netlink attribute sequence */
static int smc_pnet_set_nla(struct sk_buff *msg,
			    struct smc_user_pnetentry *pnetelem)
{
	if (nla_put_string(msg, SMC_PNETID_NAME, pnetelem->pnet_name))
		return -1;
	if (pnetelem->ndev) {
		if (nla_put_string(msg, SMC_PNETID_ETHNAME,
				   pnetelem->ndev->name))
			return -1;
	} else {
		if (nla_put_string(msg, SMC_PNETID_ETHNAME, "n/a"))
			return -1;
	}
	if (pnetelem->smcibdev) {
		if (nla_put_string(msg, SMC_PNETID_IBNAME,
			dev_name(pnetelem->smcibdev->ibdev->dev.parent)) ||
		    nla_put_u8(msg, SMC_PNETID_IBPORT, pnetelem->ib_port))
			return -1;
	} else if (pnetelem->smcd_dev) {
		if (nla_put_string(msg, SMC_PNETID_IBNAME,
				   dev_name(&pnetelem->smcd_dev->dev)) ||
		    nla_put_u8(msg, SMC_PNETID_IBPORT, 1))
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
	struct smc_user_pnetentry pnetelem;
	struct smc_pnettable *pnettable;
	struct smc_net *sn;
	int rc;

	/* get pnettable for namespace */
	sn = net_generic(net, smc_net_id);
	pnettable = &sn->pnettable;

	rc = smc_pnet_fill_entry(net, &pnetelem, info->attrs);
	if (!rc)
		rc = smc_pnet_enter(pnettable, &pnetelem);
	if (pnetelem.ndev)
		dev_put(pnetelem.ndev);
	return rc;
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
			     struct smc_user_pnetentry *pnetelem)
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
	struct smc_user_pnetentry tmp_entry;
	struct smc_pnettable *pnettable;
	struct smc_pnetentry *pnetelem;
	struct smc_ib_device *ibdev;
	struct smcd_dev *smcd_dev;
	struct smc_net *sn;
	int idx = 0;
	int ibport;

	/* get pnettable for namespace */
	sn = net_generic(net, smc_net_id);
	pnettable = &sn->pnettable;

	/* dump netdevices */
	read_lock(&pnettable->lock);
	list_for_each_entry(pnetelem, &pnettable->pnetlist, list) {
		if (pnetid && !smc_pnet_match(pnetelem->pnet_name, pnetid))
			continue;
		if (idx++ < start_idx)
			continue;
		memset(&tmp_entry, 0, sizeof(tmp_entry));
		memcpy(&tmp_entry.pnet_name, pnetelem->pnet_name,
		       SMC_MAX_PNETID_LEN);
		tmp_entry.ndev = pnetelem->ndev;
		if (smc_pnet_dumpinfo(skb, portid, seq, NLM_F_MULTI,
				      &tmp_entry)) {
			--idx;
			break;
		}
	}
	read_unlock(&pnettable->lock);

	/* if this is not the initial namespace, stop here */
	if (net != &init_net)
		return idx;

	/* dump ib devices */
	spin_lock(&smc_ib_devices.lock);
	list_for_each_entry(ibdev, &smc_ib_devices.list, list) {
		for (ibport = 0; ibport < SMC_MAX_PORTS; ibport++) {
			if (ibdev->pnetid_by_user[ibport]) {
				if (pnetid &&
				    !smc_pnet_match(ibdev->pnetid[ibport],
						    pnetid))
					continue;
				if (idx++ < start_idx)
					continue;
				memset(&tmp_entry, 0, sizeof(tmp_entry));
				memcpy(&tmp_entry.pnet_name,
				       ibdev->pnetid[ibport],
				       SMC_MAX_PNETID_LEN);
				tmp_entry.smcibdev = ibdev;
				tmp_entry.ib_port = ibport + 1;
				if (smc_pnet_dumpinfo(skb, portid, seq,
						      NLM_F_MULTI,
						      &tmp_entry)) {
					--idx;
					break;
				}
			}
		}
	}
	spin_unlock(&smc_ib_devices.lock);

	/* dump smcd devices */
	spin_lock(&smcd_dev_list.lock);
	list_for_each_entry(smcd_dev, &smcd_dev_list.list, list) {
		if (smcd_dev->pnetid_by_user) {
			if (pnetid && !smc_pnet_match(smcd_dev->pnetid, pnetid))
				continue;
			if (idx++ < start_idx)
				continue;
			memset(&tmp_entry, 0, sizeof(tmp_entry));
			memcpy(&tmp_entry.pnet_name, smcd_dev->pnetid,
			       SMC_MAX_PNETID_LEN);
			tmp_entry.smcd_dev = smcd_dev;
			if (smc_pnet_dumpinfo(skb, portid, seq, NLM_F_MULTI,
					      &tmp_entry)) {
				--idx;
				break;
			}
		}
	}
	spin_unlock(&smcd_dev_list.lock);

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
		.flags = GENL_ADMIN_PERM,
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
	.n_ops =  ARRAY_SIZE(smc_pnet_ops)
};

static int smc_pnet_netdev_event(struct notifier_block *this,
				 unsigned long event, void *ptr)
{
	struct net_device *event_dev = netdev_notifier_info_to_dev(ptr);

	switch (event) {
	case NETDEV_REBOOT:
	case NETDEV_UNREGISTER:
		smc_pnet_remove_by_ndev(event_dev);
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

	INIT_LIST_HEAD(&pnettable->pnetlist);
	rwlock_init(&pnettable->lock);

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
}

void smc_pnet_exit(void)
{
	unregister_netdevice_notifier(&smc_netdev_notifier);
	genl_unregister_family(&smc_pnet_nl_family);
}

/* Determine one base device for stacked net devices.
 * If the lower device level contains more than one devices
 * (for instance with bonding slaves), just the first device
 * is used to reach a base device.
 */
static struct net_device *pnet_find_base_ndev(struct net_device *ndev)
{
	int i, nest_lvl;

	rtnl_lock();
	nest_lvl = ndev->lower_level;
	for (i = 0; i < nest_lvl; i++) {
		struct list_head *lower = &ndev->adj_list.lower;

		if (list_empty(lower))
			break;
		lower = lower->next;
		ndev = netdev_lower_get_next(ndev, &lower);
	}
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

	read_lock(&pnettable->lock);
	list_for_each_entry(pnetelem, &pnettable->pnetlist, list) {
		if (ndev == pnetelem->ndev) {
			/* get pnetid of netdev device */
			memcpy(pnetid, pnetelem->pnet_name, SMC_MAX_PNETID_LEN);
			rc = 0;
			break;
		}
	}
	read_unlock(&pnettable->lock);
	return rc;
}

/* if handshake network device belongs to a roce device, return its
 * IB device and port
 */
static void smc_pnet_find_rdma_dev(struct net_device *netdev,
				   struct smc_init_info *ini)
{
	struct smc_ib_device *ibdev;

	spin_lock(&smc_ib_devices.lock);
	list_for_each_entry(ibdev, &smc_ib_devices.list, list) {
		struct net_device *ndev;
		int i;

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
			    !smc_ib_determine_gid(ibdev, i, ini->vlan_id,
						  ini->ib_gid, NULL)) {
				ini->ib_dev = ibdev;
				ini->ib_port = i;
				break;
			}
		}
	}
	spin_unlock(&smc_ib_devices.lock);
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
	struct smc_ib_device *ibdev;
	int i;

	ndev = pnet_find_base_ndev(ndev);
	if (smc_pnetid_by_dev_port(ndev->dev.parent, ndev->dev_port,
				   ndev_pnetid) &&
	    smc_pnet_find_ndev_pnetid_by_table(ndev, ndev_pnetid)) {
		smc_pnet_find_rdma_dev(ndev, ini);
		return; /* pnetid could not be determined */
	}

	spin_lock(&smc_ib_devices.lock);
	list_for_each_entry(ibdev, &smc_ib_devices.list, list) {
		for (i = 1; i <= SMC_MAX_PORTS; i++) {
			if (!rdma_is_port_valid(ibdev->ibdev, i))
				continue;
			if (smc_pnet_match(ibdev->pnetid[i - 1], ndev_pnetid) &&
			    smc_ib_port_active(ibdev, i) &&
			    !smc_ib_determine_gid(ibdev, i, ini->vlan_id,
						  ini->ib_gid, NULL)) {
				ini->ib_dev = ibdev;
				ini->ib_port = i;
				goto out;
			}
		}
	}
out:
	spin_unlock(&smc_ib_devices.lock);
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

	spin_lock(&smcd_dev_list.lock);
	list_for_each_entry(ismdev, &smcd_dev_list.list, list) {
		if (smc_pnet_match(ismdev->pnetid, ndev_pnetid)) {
			ini->ism_dev = ismdev;
			break;
		}
	}
	spin_unlock(&smcd_dev_list.lock);
}

/* PNET table analysis for a given sock:
 * determine ib_device and port belonging to used internal TCP socket
 * ethernet interface.
 */
void smc_pnet_find_roce_resource(struct sock *sk, struct smc_init_info *ini)
{
	struct dst_entry *dst = sk_dst_get(sk);

	ini->ib_dev = NULL;
	ini->ib_port = 0;
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

	ini->ism_dev = NULL;
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
