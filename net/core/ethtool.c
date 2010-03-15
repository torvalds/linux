/*
 * net/core/ethtool.c - Ethtool ioctl handler
 * Copyright (c) 2003 Matthew Wilcox <matthew@wil.cx>
 *
 * This file is where we call all the ethtool_ops commands to get
 * the information ethtool needs.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/bitops.h>
#include <asm/uaccess.h>

/*
 * Some useful ethtool_ops methods that're device independent.
 * If we find that all drivers want to do the same thing here,
 * we can turn these into dev_() function calls.
 */

u32 ethtool_op_get_link(struct net_device *dev)
{
	return netif_carrier_ok(dev) ? 1 : 0;
}

u32 ethtool_op_get_rx_csum(struct net_device *dev)
{
	return (dev->features & NETIF_F_ALL_CSUM) != 0;
}
EXPORT_SYMBOL(ethtool_op_get_rx_csum);

u32 ethtool_op_get_tx_csum(struct net_device *dev)
{
	return (dev->features & NETIF_F_ALL_CSUM) != 0;
}
EXPORT_SYMBOL(ethtool_op_get_tx_csum);

int ethtool_op_set_tx_csum(struct net_device *dev, u32 data)
{
	if (data)
		dev->features |= NETIF_F_IP_CSUM;
	else
		dev->features &= ~NETIF_F_IP_CSUM;

	return 0;
}

int ethtool_op_set_tx_hw_csum(struct net_device *dev, u32 data)
{
	if (data)
		dev->features |= NETIF_F_HW_CSUM;
	else
		dev->features &= ~NETIF_F_HW_CSUM;

	return 0;
}

int ethtool_op_set_tx_ipv6_csum(struct net_device *dev, u32 data)
{
	if (data)
		dev->features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
	else
		dev->features &= ~(NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM);

	return 0;
}

u32 ethtool_op_get_sg(struct net_device *dev)
{
	return (dev->features & NETIF_F_SG) != 0;
}

int ethtool_op_set_sg(struct net_device *dev, u32 data)
{
	if (data)
		dev->features |= NETIF_F_SG;
	else
		dev->features &= ~NETIF_F_SG;

	return 0;
}

u32 ethtool_op_get_tso(struct net_device *dev)
{
	return (dev->features & NETIF_F_TSO) != 0;
}

int ethtool_op_set_tso(struct net_device *dev, u32 data)
{
	if (data)
		dev->features |= NETIF_F_TSO;
	else
		dev->features &= ~NETIF_F_TSO;

	return 0;
}

u32 ethtool_op_get_ufo(struct net_device *dev)
{
	return (dev->features & NETIF_F_UFO) != 0;
}

int ethtool_op_set_ufo(struct net_device *dev, u32 data)
{
	if (data)
		dev->features |= NETIF_F_UFO;
	else
		dev->features &= ~NETIF_F_UFO;
	return 0;
}

/* the following list of flags are the same as their associated
 * NETIF_F_xxx values in include/linux/netdevice.h
 */
static const u32 flags_dup_features =
	(ETH_FLAG_LRO | ETH_FLAG_NTUPLE);

u32 ethtool_op_get_flags(struct net_device *dev)
{
	/* in the future, this function will probably contain additional
	 * handling for flags which are not so easily handled
	 * by a simple masking operation
	 */

	return dev->features & flags_dup_features;
}

int ethtool_op_set_flags(struct net_device *dev, u32 data)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	unsigned long features = dev->features;

	if (data & ETH_FLAG_LRO)
		features |= NETIF_F_LRO;
	else
		features &= ~NETIF_F_LRO;

	if (data & ETH_FLAG_NTUPLE) {
		if (!ops->set_rx_ntuple)
			return -EOPNOTSUPP;
		features |= NETIF_F_NTUPLE;
	} else {
		/* safe to clear regardless */
		features &= ~NETIF_F_NTUPLE;
	}

	dev->features = features;
	return 0;
}

void ethtool_ntuple_flush(struct net_device *dev)
{
	struct ethtool_rx_ntuple_flow_spec_container *fsc, *f;

	list_for_each_entry_safe(fsc, f, &dev->ethtool_ntuple_list.list, list) {
		list_del(&fsc->list);
		kfree(fsc);
	}
	dev->ethtool_ntuple_list.count = 0;
}
EXPORT_SYMBOL(ethtool_ntuple_flush);

/* Handlers for each ethtool command */

static int ethtool_get_settings(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_cmd cmd = { .cmd = ETHTOOL_GSET };
	int err;

	if (!dev->ethtool_ops->get_settings)
		return -EOPNOTSUPP;

	err = dev->ethtool_ops->get_settings(dev, &cmd);
	if (err < 0)
		return err;

	if (copy_to_user(useraddr, &cmd, sizeof(cmd)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_settings(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_cmd cmd;

	if (!dev->ethtool_ops->set_settings)
		return -EOPNOTSUPP;

	if (copy_from_user(&cmd, useraddr, sizeof(cmd)))
		return -EFAULT;

	return dev->ethtool_ops->set_settings(dev, &cmd);
}

static noinline_for_stack int ethtool_get_drvinfo(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_drvinfo info;
	const struct ethtool_ops *ops = dev->ethtool_ops;

	if (!ops->get_drvinfo)
		return -EOPNOTSUPP;

	memset(&info, 0, sizeof(info));
	info.cmd = ETHTOOL_GDRVINFO;
	ops->get_drvinfo(dev, &info);

	/*
	 * this method of obtaining string set info is deprecated;
	 * Use ETHTOOL_GSSET_INFO instead.
	 */
	if (ops->get_sset_count) {
		int rc;

		rc = ops->get_sset_count(dev, ETH_SS_TEST);
		if (rc >= 0)
			info.testinfo_len = rc;
		rc = ops->get_sset_count(dev, ETH_SS_STATS);
		if (rc >= 0)
			info.n_stats = rc;
		rc = ops->get_sset_count(dev, ETH_SS_PRIV_FLAGS);
		if (rc >= 0)
			info.n_priv_flags = rc;
	}
	if (ops->get_regs_len)
		info.regdump_len = ops->get_regs_len(dev);
	if (ops->get_eeprom_len)
		info.eedump_len = ops->get_eeprom_len(dev);

	if (copy_to_user(useraddr, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

static noinline_for_stack int ethtool_get_sset_info(struct net_device *dev,
                                          void __user *useraddr)
{
	struct ethtool_sset_info info;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	u64 sset_mask;
	int i, idx = 0, n_bits = 0, ret, rc;
	u32 *info_buf = NULL;

	if (!ops->get_sset_count)
		return -EOPNOTSUPP;

	if (copy_from_user(&info, useraddr, sizeof(info)))
		return -EFAULT;

	/* store copy of mask, because we zero struct later on */
	sset_mask = info.sset_mask;
	if (!sset_mask)
		return 0;

	/* calculate size of return buffer */
	n_bits = hweight64(sset_mask);

	memset(&info, 0, sizeof(info));
	info.cmd = ETHTOOL_GSSET_INFO;

	info_buf = kzalloc(n_bits * sizeof(u32), GFP_USER);
	if (!info_buf)
		return -ENOMEM;

	/*
	 * fill return buffer based on input bitmask and successful
	 * get_sset_count return
	 */
	for (i = 0; i < 64; i++) {
		if (!(sset_mask & (1ULL << i)))
			continue;

		rc = ops->get_sset_count(dev, i);
		if (rc >= 0) {
			info.sset_mask |= (1ULL << i);
			info_buf[idx++] = rc;
		}
	}

	ret = -EFAULT;
	if (copy_to_user(useraddr, &info, sizeof(info)))
		goto out;

	useraddr += offsetof(struct ethtool_sset_info, data);
	if (copy_to_user(useraddr, info_buf, idx * sizeof(u32)))
		goto out;

	ret = 0;

out:
	kfree(info_buf);
	return ret;
}

static noinline_for_stack int ethtool_set_rxnfc(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_rxnfc cmd;

	if (!dev->ethtool_ops->set_rxnfc)
		return -EOPNOTSUPP;

	if (copy_from_user(&cmd, useraddr, sizeof(cmd)))
		return -EFAULT;

	return dev->ethtool_ops->set_rxnfc(dev, &cmd);
}

static noinline_for_stack int ethtool_get_rxnfc(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_rxnfc info;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	int ret;
	void *rule_buf = NULL;

	if (!ops->get_rxnfc)
		return -EOPNOTSUPP;

	if (copy_from_user(&info, useraddr, sizeof(info)))
		return -EFAULT;

	if (info.cmd == ETHTOOL_GRXCLSRLALL) {
		if (info.rule_cnt > 0) {
			rule_buf = kmalloc(info.rule_cnt * sizeof(u32),
					   GFP_USER);
			if (!rule_buf)
				return -ENOMEM;
		}
	}

	ret = ops->get_rxnfc(dev, &info, rule_buf);
	if (ret < 0)
		goto err_out;

	ret = -EFAULT;
	if (copy_to_user(useraddr, &info, sizeof(info)))
		goto err_out;

	if (rule_buf) {
		useraddr += offsetof(struct ethtool_rxnfc, rule_locs);
		if (copy_to_user(useraddr, rule_buf,
				 info.rule_cnt * sizeof(u32)))
			goto err_out;
	}
	ret = 0;

err_out:
	kfree(rule_buf);

	return ret;
}

static void __rx_ntuple_filter_add(struct ethtool_rx_ntuple_list *list,
                              struct ethtool_rx_ntuple_flow_spec *spec,
                              struct ethtool_rx_ntuple_flow_spec_container *fsc)
{

	/* don't add filters forever */
	if (list->count >= ETHTOOL_MAX_NTUPLE_LIST_ENTRY) {
		/* free the container */
		kfree(fsc);
		return;
	}

	/* Copy the whole filter over */
	fsc->fs.flow_type = spec->flow_type;
	memcpy(&fsc->fs.h_u, &spec->h_u, sizeof(spec->h_u));
	memcpy(&fsc->fs.m_u, &spec->m_u, sizeof(spec->m_u));

	fsc->fs.vlan_tag = spec->vlan_tag;
	fsc->fs.vlan_tag_mask = spec->vlan_tag_mask;
	fsc->fs.data = spec->data;
	fsc->fs.data_mask = spec->data_mask;
	fsc->fs.action = spec->action;

	/* add to the list */
	list_add_tail_rcu(&fsc->list, &list->list);
	list->count++;
}

static noinline_for_stack int ethtool_set_rx_ntuple(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_rx_ntuple cmd;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct ethtool_rx_ntuple_flow_spec_container *fsc = NULL;
	int ret;

	if (!(dev->features & NETIF_F_NTUPLE))
		return -EINVAL;

	if (copy_from_user(&cmd, useraddr, sizeof(cmd)))
		return -EFAULT;

	/*
	 * Cache filter in dev struct for GET operation only if
	 * the underlying driver doesn't have its own GET operation, and
	 * only if the filter was added successfully.  First make sure we
	 * can allocate the filter, then continue if successful.
	 */
	if (!ops->get_rx_ntuple) {
		fsc = kmalloc(sizeof(*fsc), GFP_ATOMIC);
		if (!fsc)
			return -ENOMEM;
	}

	ret = ops->set_rx_ntuple(dev, &cmd);
	if (ret) {
		kfree(fsc);
		return ret;
	}

	if (!ops->get_rx_ntuple)
		__rx_ntuple_filter_add(&dev->ethtool_ntuple_list, &cmd.fs, fsc);

	return ret;
}

static int ethtool_get_rx_ntuple(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_gstrings gstrings;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct ethtool_rx_ntuple_flow_spec_container *fsc;
	u8 *data;
	char *p;
	int ret, i, num_strings = 0;

	if (!ops->get_sset_count)
		return -EOPNOTSUPP;

	if (copy_from_user(&gstrings, useraddr, sizeof(gstrings)))
		return -EFAULT;

	ret = ops->get_sset_count(dev, gstrings.string_set);
	if (ret < 0)
		return ret;

	gstrings.len = ret;

	data = kmalloc(gstrings.len * ETH_GSTRING_LEN, GFP_USER);
	if (!data)
		return -ENOMEM;

	if (ops->get_rx_ntuple) {
		/* driver-specific filter grab */
		ret = ops->get_rx_ntuple(dev, gstrings.string_set, data);
		goto copy;
	}

	/* default ethtool filter grab */
	i = 0;
	p = (char *)data;
	list_for_each_entry(fsc, &dev->ethtool_ntuple_list.list, list) {
		sprintf(p, "Filter %d:\n", i);
		p += ETH_GSTRING_LEN;
		num_strings++;

		switch (fsc->fs.flow_type) {
		case TCP_V4_FLOW:
			sprintf(p, "\tFlow Type: TCP\n");
			p += ETH_GSTRING_LEN;
			num_strings++;
			break;
		case UDP_V4_FLOW:
			sprintf(p, "\tFlow Type: UDP\n");
			p += ETH_GSTRING_LEN;
			num_strings++;
			break;
		case SCTP_V4_FLOW:
			sprintf(p, "\tFlow Type: SCTP\n");
			p += ETH_GSTRING_LEN;
			num_strings++;
			break;
		case AH_ESP_V4_FLOW:
			sprintf(p, "\tFlow Type: AH ESP\n");
			p += ETH_GSTRING_LEN;
			num_strings++;
			break;
		case ESP_V4_FLOW:
			sprintf(p, "\tFlow Type: ESP\n");
			p += ETH_GSTRING_LEN;
			num_strings++;
			break;
		case IP_USER_FLOW:
			sprintf(p, "\tFlow Type: Raw IP\n");
			p += ETH_GSTRING_LEN;
			num_strings++;
			break;
		case IPV4_FLOW:
			sprintf(p, "\tFlow Type: IPv4\n");
			p += ETH_GSTRING_LEN;
			num_strings++;
			break;
		default:
			sprintf(p, "\tFlow Type: Unknown\n");
			p += ETH_GSTRING_LEN;
			num_strings++;
			goto unknown_filter;
		};

		/* now the rest of the filters */
		switch (fsc->fs.flow_type) {
		case TCP_V4_FLOW:
		case UDP_V4_FLOW:
		case SCTP_V4_FLOW:
			sprintf(p, "\tSrc IP addr: 0x%x\n",
			        fsc->fs.h_u.tcp_ip4_spec.ip4src);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tSrc IP mask: 0x%x\n",
			        fsc->fs.m_u.tcp_ip4_spec.ip4src);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tDest IP addr: 0x%x\n",
			        fsc->fs.h_u.tcp_ip4_spec.ip4dst);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tDest IP mask: 0x%x\n",
			        fsc->fs.m_u.tcp_ip4_spec.ip4dst);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tSrc Port: %d, mask: 0x%x\n",
			        fsc->fs.h_u.tcp_ip4_spec.psrc,
			        fsc->fs.m_u.tcp_ip4_spec.psrc);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tDest Port: %d, mask: 0x%x\n",
			        fsc->fs.h_u.tcp_ip4_spec.pdst,
			        fsc->fs.m_u.tcp_ip4_spec.pdst);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tTOS: %d, mask: 0x%x\n",
			        fsc->fs.h_u.tcp_ip4_spec.tos,
			        fsc->fs.m_u.tcp_ip4_spec.tos);
			p += ETH_GSTRING_LEN;
			num_strings++;
			break;
		case AH_ESP_V4_FLOW:
		case ESP_V4_FLOW:
			sprintf(p, "\tSrc IP addr: 0x%x\n",
			        fsc->fs.h_u.ah_ip4_spec.ip4src);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tSrc IP mask: 0x%x\n",
			        fsc->fs.m_u.ah_ip4_spec.ip4src);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tDest IP addr: 0x%x\n",
			        fsc->fs.h_u.ah_ip4_spec.ip4dst);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tDest IP mask: 0x%x\n",
			        fsc->fs.m_u.ah_ip4_spec.ip4dst);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tSPI: %d, mask: 0x%x\n",
			        fsc->fs.h_u.ah_ip4_spec.spi,
			        fsc->fs.m_u.ah_ip4_spec.spi);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tTOS: %d, mask: 0x%x\n",
			        fsc->fs.h_u.ah_ip4_spec.tos,
			        fsc->fs.m_u.ah_ip4_spec.tos);
			p += ETH_GSTRING_LEN;
			num_strings++;
			break;
		case IP_USER_FLOW:
			sprintf(p, "\tSrc IP addr: 0x%x\n",
			        fsc->fs.h_u.raw_ip4_spec.ip4src);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tSrc IP mask: 0x%x\n",
			        fsc->fs.m_u.raw_ip4_spec.ip4src);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tDest IP addr: 0x%x\n",
			        fsc->fs.h_u.raw_ip4_spec.ip4dst);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tDest IP mask: 0x%x\n",
			        fsc->fs.m_u.raw_ip4_spec.ip4dst);
			p += ETH_GSTRING_LEN;
			num_strings++;
			break;
		case IPV4_FLOW:
			sprintf(p, "\tSrc IP addr: 0x%x\n",
			        fsc->fs.h_u.usr_ip4_spec.ip4src);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tSrc IP mask: 0x%x\n",
			        fsc->fs.m_u.usr_ip4_spec.ip4src);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tDest IP addr: 0x%x\n",
			        fsc->fs.h_u.usr_ip4_spec.ip4dst);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tDest IP mask: 0x%x\n",
			        fsc->fs.m_u.usr_ip4_spec.ip4dst);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tL4 bytes: 0x%x, mask: 0x%x\n",
			        fsc->fs.h_u.usr_ip4_spec.l4_4_bytes,
			        fsc->fs.m_u.usr_ip4_spec.l4_4_bytes);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tTOS: %d, mask: 0x%x\n",
			        fsc->fs.h_u.usr_ip4_spec.tos,
			        fsc->fs.m_u.usr_ip4_spec.tos);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tIP Version: %d, mask: 0x%x\n",
			        fsc->fs.h_u.usr_ip4_spec.ip_ver,
			        fsc->fs.m_u.usr_ip4_spec.ip_ver);
			p += ETH_GSTRING_LEN;
			num_strings++;
			sprintf(p, "\tProtocol: %d, mask: 0x%x\n",
			        fsc->fs.h_u.usr_ip4_spec.proto,
			        fsc->fs.m_u.usr_ip4_spec.proto);
			p += ETH_GSTRING_LEN;
			num_strings++;
			break;
		};
		sprintf(p, "\tVLAN: %d, mask: 0x%x\n",
		        fsc->fs.vlan_tag, fsc->fs.vlan_tag_mask);
		p += ETH_GSTRING_LEN;
		num_strings++;
		sprintf(p, "\tUser-defined: 0x%Lx\n", fsc->fs.data);
		p += ETH_GSTRING_LEN;
		num_strings++;
		sprintf(p, "\tUser-defined mask: 0x%Lx\n", fsc->fs.data_mask);
		p += ETH_GSTRING_LEN;
		num_strings++;
		if (fsc->fs.action == ETHTOOL_RXNTUPLE_ACTION_DROP)
			sprintf(p, "\tAction: Drop\n");
		else
			sprintf(p, "\tAction: Direct to queue %d\n",
			        fsc->fs.action);
		p += ETH_GSTRING_LEN;
		num_strings++;
unknown_filter:
		i++;
	}
copy:
	/* indicate to userspace how many strings we actually have */
	gstrings.len = num_strings;
	ret = -EFAULT;
	if (copy_to_user(useraddr, &gstrings, sizeof(gstrings)))
		goto out;
	useraddr += sizeof(gstrings);
	if (copy_to_user(useraddr, data, gstrings.len * ETH_GSTRING_LEN))
		goto out;
	ret = 0;

out:
	kfree(data);
	return ret;
}

static int ethtool_get_regs(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_regs regs;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	void *regbuf;
	int reglen, ret;

	if (!ops->get_regs || !ops->get_regs_len)
		return -EOPNOTSUPP;

	if (copy_from_user(&regs, useraddr, sizeof(regs)))
		return -EFAULT;

	reglen = ops->get_regs_len(dev);
	if (regs.len > reglen)
		regs.len = reglen;

	regbuf = kmalloc(reglen, GFP_USER);
	if (!regbuf)
		return -ENOMEM;

	ops->get_regs(dev, &regs, regbuf);

	ret = -EFAULT;
	if (copy_to_user(useraddr, &regs, sizeof(regs)))
		goto out;
	useraddr += offsetof(struct ethtool_regs, data);
	if (copy_to_user(useraddr, regbuf, regs.len))
		goto out;
	ret = 0;

 out:
	kfree(regbuf);
	return ret;
}

static int ethtool_reset(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_value reset;
	int ret;

	if (!dev->ethtool_ops->reset)
		return -EOPNOTSUPP;

	if (copy_from_user(&reset, useraddr, sizeof(reset)))
		return -EFAULT;

	ret = dev->ethtool_ops->reset(dev, &reset.data);
	if (ret)
		return ret;

	if (copy_to_user(useraddr, &reset, sizeof(reset)))
		return -EFAULT;
	return 0;
}

static int ethtool_get_wol(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_wolinfo wol = { .cmd = ETHTOOL_GWOL };

	if (!dev->ethtool_ops->get_wol)
		return -EOPNOTSUPP;

	dev->ethtool_ops->get_wol(dev, &wol);

	if (copy_to_user(useraddr, &wol, sizeof(wol)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_wol(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_wolinfo wol;

	if (!dev->ethtool_ops->set_wol)
		return -EOPNOTSUPP;

	if (copy_from_user(&wol, useraddr, sizeof(wol)))
		return -EFAULT;

	return dev->ethtool_ops->set_wol(dev, &wol);
}

static int ethtool_nway_reset(struct net_device *dev)
{
	if (!dev->ethtool_ops->nway_reset)
		return -EOPNOTSUPP;

	return dev->ethtool_ops->nway_reset(dev);
}

static int ethtool_get_eeprom(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_eeprom eeprom;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	void __user *userbuf = useraddr + sizeof(eeprom);
	u32 bytes_remaining;
	u8 *data;
	int ret = 0;

	if (!ops->get_eeprom || !ops->get_eeprom_len)
		return -EOPNOTSUPP;

	if (copy_from_user(&eeprom, useraddr, sizeof(eeprom)))
		return -EFAULT;

	/* Check for wrap and zero */
	if (eeprom.offset + eeprom.len <= eeprom.offset)
		return -EINVAL;

	/* Check for exceeding total eeprom len */
	if (eeprom.offset + eeprom.len > ops->get_eeprom_len(dev))
		return -EINVAL;

	data = kmalloc(PAGE_SIZE, GFP_USER);
	if (!data)
		return -ENOMEM;

	bytes_remaining = eeprom.len;
	while (bytes_remaining > 0) {
		eeprom.len = min(bytes_remaining, (u32)PAGE_SIZE);

		ret = ops->get_eeprom(dev, &eeprom, data);
		if (ret)
			break;
		if (copy_to_user(userbuf, data, eeprom.len)) {
			ret = -EFAULT;
			break;
		}
		userbuf += eeprom.len;
		eeprom.offset += eeprom.len;
		bytes_remaining -= eeprom.len;
	}

	eeprom.len = userbuf - (useraddr + sizeof(eeprom));
	eeprom.offset -= eeprom.len;
	if (copy_to_user(useraddr, &eeprom, sizeof(eeprom)))
		ret = -EFAULT;

	kfree(data);
	return ret;
}

static int ethtool_set_eeprom(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_eeprom eeprom;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	void __user *userbuf = useraddr + sizeof(eeprom);
	u32 bytes_remaining;
	u8 *data;
	int ret = 0;

	if (!ops->set_eeprom || !ops->get_eeprom_len)
		return -EOPNOTSUPP;

	if (copy_from_user(&eeprom, useraddr, sizeof(eeprom)))
		return -EFAULT;

	/* Check for wrap and zero */
	if (eeprom.offset + eeprom.len <= eeprom.offset)
		return -EINVAL;

	/* Check for exceeding total eeprom len */
	if (eeprom.offset + eeprom.len > ops->get_eeprom_len(dev))
		return -EINVAL;

	data = kmalloc(PAGE_SIZE, GFP_USER);
	if (!data)
		return -ENOMEM;

	bytes_remaining = eeprom.len;
	while (bytes_remaining > 0) {
		eeprom.len = min(bytes_remaining, (u32)PAGE_SIZE);

		if (copy_from_user(data, userbuf, eeprom.len)) {
			ret = -EFAULT;
			break;
		}
		ret = ops->set_eeprom(dev, &eeprom, data);
		if (ret)
			break;
		userbuf += eeprom.len;
		eeprom.offset += eeprom.len;
		bytes_remaining -= eeprom.len;
	}

	kfree(data);
	return ret;
}

static noinline_for_stack int ethtool_get_coalesce(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_coalesce coalesce = { .cmd = ETHTOOL_GCOALESCE };

	if (!dev->ethtool_ops->get_coalesce)
		return -EOPNOTSUPP;

	dev->ethtool_ops->get_coalesce(dev, &coalesce);

	if (copy_to_user(useraddr, &coalesce, sizeof(coalesce)))
		return -EFAULT;
	return 0;
}

static noinline_for_stack int ethtool_set_coalesce(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_coalesce coalesce;

	if (!dev->ethtool_ops->set_coalesce)
		return -EOPNOTSUPP;

	if (copy_from_user(&coalesce, useraddr, sizeof(coalesce)))
		return -EFAULT;

	return dev->ethtool_ops->set_coalesce(dev, &coalesce);
}

static int ethtool_get_ringparam(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_ringparam ringparam = { .cmd = ETHTOOL_GRINGPARAM };

	if (!dev->ethtool_ops->get_ringparam)
		return -EOPNOTSUPP;

	dev->ethtool_ops->get_ringparam(dev, &ringparam);

	if (copy_to_user(useraddr, &ringparam, sizeof(ringparam)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_ringparam(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_ringparam ringparam;

	if (!dev->ethtool_ops->set_ringparam)
		return -EOPNOTSUPP;

	if (copy_from_user(&ringparam, useraddr, sizeof(ringparam)))
		return -EFAULT;

	return dev->ethtool_ops->set_ringparam(dev, &ringparam);
}

static int ethtool_get_pauseparam(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_pauseparam pauseparam = { ETHTOOL_GPAUSEPARAM };

	if (!dev->ethtool_ops->get_pauseparam)
		return -EOPNOTSUPP;

	dev->ethtool_ops->get_pauseparam(dev, &pauseparam);

	if (copy_to_user(useraddr, &pauseparam, sizeof(pauseparam)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_pauseparam(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_pauseparam pauseparam;

	if (!dev->ethtool_ops->set_pauseparam)
		return -EOPNOTSUPP;

	if (copy_from_user(&pauseparam, useraddr, sizeof(pauseparam)))
		return -EFAULT;

	return dev->ethtool_ops->set_pauseparam(dev, &pauseparam);
}

static int __ethtool_set_sg(struct net_device *dev, u32 data)
{
	int err;

	if (!data && dev->ethtool_ops->set_tso) {
		err = dev->ethtool_ops->set_tso(dev, 0);
		if (err)
			return err;
	}

	if (!data && dev->ethtool_ops->set_ufo) {
		err = dev->ethtool_ops->set_ufo(dev, 0);
		if (err)
			return err;
	}
	return dev->ethtool_ops->set_sg(dev, data);
}

static int ethtool_set_tx_csum(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_value edata;
	int err;

	if (!dev->ethtool_ops->set_tx_csum)
		return -EOPNOTSUPP;

	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;

	if (!edata.data && dev->ethtool_ops->set_sg) {
		err = __ethtool_set_sg(dev, 0);
		if (err)
			return err;
	}

	return dev->ethtool_ops->set_tx_csum(dev, edata.data);
}

static int ethtool_set_rx_csum(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_value edata;

	if (!dev->ethtool_ops->set_rx_csum)
		return -EOPNOTSUPP;

	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;

	if (!edata.data && dev->ethtool_ops->set_sg)
		dev->features &= ~NETIF_F_GRO;

	return dev->ethtool_ops->set_rx_csum(dev, edata.data);
}

static int ethtool_set_sg(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_value edata;

	if (!dev->ethtool_ops->set_sg)
		return -EOPNOTSUPP;

	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;

	if (edata.data &&
	    !(dev->features & NETIF_F_ALL_CSUM))
		return -EINVAL;

	return __ethtool_set_sg(dev, edata.data);
}

static int ethtool_set_tso(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_value edata;

	if (!dev->ethtool_ops->set_tso)
		return -EOPNOTSUPP;

	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;

	if (edata.data && !(dev->features & NETIF_F_SG))
		return -EINVAL;

	return dev->ethtool_ops->set_tso(dev, edata.data);
}

static int ethtool_set_ufo(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_value edata;

	if (!dev->ethtool_ops->set_ufo)
		return -EOPNOTSUPP;
	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;
	if (edata.data && !(dev->features & NETIF_F_SG))
		return -EINVAL;
	if (edata.data && !(dev->features & NETIF_F_HW_CSUM))
		return -EINVAL;
	return dev->ethtool_ops->set_ufo(dev, edata.data);
}

static int ethtool_get_gso(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_value edata = { ETHTOOL_GGSO };

	edata.data = dev->features & NETIF_F_GSO;
	if (copy_to_user(useraddr, &edata, sizeof(edata)))
		 return -EFAULT;
	return 0;
}

static int ethtool_set_gso(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_value edata;

	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;
	if (edata.data)
		dev->features |= NETIF_F_GSO;
	else
		dev->features &= ~NETIF_F_GSO;
	return 0;
}

static int ethtool_get_gro(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_value edata = { ETHTOOL_GGRO };

	edata.data = dev->features & NETIF_F_GRO;
	if (copy_to_user(useraddr, &edata, sizeof(edata)))
		 return -EFAULT;
	return 0;
}

static int ethtool_set_gro(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_value edata;

	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;

	if (edata.data) {
		if (!dev->ethtool_ops->get_rx_csum ||
		    !dev->ethtool_ops->get_rx_csum(dev))
			return -EINVAL;
		dev->features |= NETIF_F_GRO;
	} else
		dev->features &= ~NETIF_F_GRO;

	return 0;
}

static int ethtool_self_test(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_test test;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	u64 *data;
	int ret, test_len;

	if (!ops->self_test || !ops->get_sset_count)
		return -EOPNOTSUPP;

	test_len = ops->get_sset_count(dev, ETH_SS_TEST);
	if (test_len < 0)
		return test_len;
	WARN_ON(test_len == 0);

	if (copy_from_user(&test, useraddr, sizeof(test)))
		return -EFAULT;

	test.len = test_len;
	data = kmalloc(test_len * sizeof(u64), GFP_USER);
	if (!data)
		return -ENOMEM;

	ops->self_test(dev, &test, data);

	ret = -EFAULT;
	if (copy_to_user(useraddr, &test, sizeof(test)))
		goto out;
	useraddr += sizeof(test);
	if (copy_to_user(useraddr, data, test.len * sizeof(u64)))
		goto out;
	ret = 0;

 out:
	kfree(data);
	return ret;
}

static int ethtool_get_strings(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_gstrings gstrings;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	u8 *data;
	int ret;

	if (!ops->get_strings || !ops->get_sset_count)
		return -EOPNOTSUPP;

	if (copy_from_user(&gstrings, useraddr, sizeof(gstrings)))
		return -EFAULT;

	ret = ops->get_sset_count(dev, gstrings.string_set);
	if (ret < 0)
		return ret;

	gstrings.len = ret;

	data = kmalloc(gstrings.len * ETH_GSTRING_LEN, GFP_USER);
	if (!data)
		return -ENOMEM;

	ops->get_strings(dev, gstrings.string_set, data);

	ret = -EFAULT;
	if (copy_to_user(useraddr, &gstrings, sizeof(gstrings)))
		goto out;
	useraddr += sizeof(gstrings);
	if (copy_to_user(useraddr, data, gstrings.len * ETH_GSTRING_LEN))
		goto out;
	ret = 0;

 out:
	kfree(data);
	return ret;
}

static int ethtool_phys_id(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_value id;

	if (!dev->ethtool_ops->phys_id)
		return -EOPNOTSUPP;

	if (copy_from_user(&id, useraddr, sizeof(id)))
		return -EFAULT;

	return dev->ethtool_ops->phys_id(dev, id.data);
}

static int ethtool_get_stats(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_stats stats;
	const struct ethtool_ops *ops = dev->ethtool_ops;
	u64 *data;
	int ret, n_stats;

	if (!ops->get_ethtool_stats || !ops->get_sset_count)
		return -EOPNOTSUPP;

	n_stats = ops->get_sset_count(dev, ETH_SS_STATS);
	if (n_stats < 0)
		return n_stats;
	WARN_ON(n_stats == 0);

	if (copy_from_user(&stats, useraddr, sizeof(stats)))
		return -EFAULT;

	stats.n_stats = n_stats;
	data = kmalloc(n_stats * sizeof(u64), GFP_USER);
	if (!data)
		return -ENOMEM;

	ops->get_ethtool_stats(dev, &stats, data);

	ret = -EFAULT;
	if (copy_to_user(useraddr, &stats, sizeof(stats)))
		goto out;
	useraddr += sizeof(stats);
	if (copy_to_user(useraddr, data, stats.n_stats * sizeof(u64)))
		goto out;
	ret = 0;

 out:
	kfree(data);
	return ret;
}

static int ethtool_get_perm_addr(struct net_device *dev, void __user *useraddr)
{
	struct ethtool_perm_addr epaddr;

	if (copy_from_user(&epaddr, useraddr, sizeof(epaddr)))
		return -EFAULT;

	if (epaddr.size < dev->addr_len)
		return -ETOOSMALL;
	epaddr.size = dev->addr_len;

	if (copy_to_user(useraddr, &epaddr, sizeof(epaddr)))
		return -EFAULT;
	useraddr += sizeof(epaddr);
	if (copy_to_user(useraddr, dev->perm_addr, epaddr.size))
		return -EFAULT;
	return 0;
}

static int ethtool_get_value(struct net_device *dev, char __user *useraddr,
			     u32 cmd, u32 (*actor)(struct net_device *))
{
	struct ethtool_value edata = { .cmd = cmd };

	if (!actor)
		return -EOPNOTSUPP;

	edata.data = actor(dev);

	if (copy_to_user(useraddr, &edata, sizeof(edata)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_value_void(struct net_device *dev, char __user *useraddr,
			     void (*actor)(struct net_device *, u32))
{
	struct ethtool_value edata;

	if (!actor)
		return -EOPNOTSUPP;

	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;

	actor(dev, edata.data);
	return 0;
}

static int ethtool_set_value(struct net_device *dev, char __user *useraddr,
			     int (*actor)(struct net_device *, u32))
{
	struct ethtool_value edata;

	if (!actor)
		return -EOPNOTSUPP;

	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;

	return actor(dev, edata.data);
}

static noinline_for_stack int ethtool_flash_device(struct net_device *dev, char __user *useraddr)
{
	struct ethtool_flash efl;

	if (copy_from_user(&efl, useraddr, sizeof(efl)))
		return -EFAULT;

	if (!dev->ethtool_ops->flash_device)
		return -EOPNOTSUPP;

	return dev->ethtool_ops->flash_device(dev, &efl);
}

/* The main entry point in this file.  Called from net/core/dev.c */

int dev_ethtool(struct net *net, struct ifreq *ifr)
{
	struct net_device *dev = __dev_get_by_name(net, ifr->ifr_name);
	void __user *useraddr = ifr->ifr_data;
	u32 ethcmd;
	int rc;
	unsigned long old_features;

	if (!dev || !netif_device_present(dev))
		return -ENODEV;

	if (!dev->ethtool_ops)
		return -EOPNOTSUPP;

	if (copy_from_user(&ethcmd, useraddr, sizeof (ethcmd)))
		return -EFAULT;

	/* Allow some commands to be done by anyone */
	switch(ethcmd) {
	case ETHTOOL_GDRVINFO:
	case ETHTOOL_GMSGLVL:
	case ETHTOOL_GCOALESCE:
	case ETHTOOL_GRINGPARAM:
	case ETHTOOL_GPAUSEPARAM:
	case ETHTOOL_GRXCSUM:
	case ETHTOOL_GTXCSUM:
	case ETHTOOL_GSG:
	case ETHTOOL_GSTRINGS:
	case ETHTOOL_GTSO:
	case ETHTOOL_GPERMADDR:
	case ETHTOOL_GUFO:
	case ETHTOOL_GGSO:
	case ETHTOOL_GGRO:
	case ETHTOOL_GFLAGS:
	case ETHTOOL_GPFLAGS:
	case ETHTOOL_GRXFH:
	case ETHTOOL_GRXRINGS:
	case ETHTOOL_GRXCLSRLCNT:
	case ETHTOOL_GRXCLSRULE:
	case ETHTOOL_GRXCLSRLALL:
		break;
	default:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
	}

	if (dev->ethtool_ops->begin)
		if ((rc = dev->ethtool_ops->begin(dev)) < 0)
			return rc;

	old_features = dev->features;

	switch (ethcmd) {
	case ETHTOOL_GSET:
		rc = ethtool_get_settings(dev, useraddr);
		break;
	case ETHTOOL_SSET:
		rc = ethtool_set_settings(dev, useraddr);
		break;
	case ETHTOOL_GDRVINFO:
		rc = ethtool_get_drvinfo(dev, useraddr);
		break;
	case ETHTOOL_GREGS:
		rc = ethtool_get_regs(dev, useraddr);
		break;
	case ETHTOOL_GWOL:
		rc = ethtool_get_wol(dev, useraddr);
		break;
	case ETHTOOL_SWOL:
		rc = ethtool_set_wol(dev, useraddr);
		break;
	case ETHTOOL_GMSGLVL:
		rc = ethtool_get_value(dev, useraddr, ethcmd,
				       dev->ethtool_ops->get_msglevel);
		break;
	case ETHTOOL_SMSGLVL:
		rc = ethtool_set_value_void(dev, useraddr,
				       dev->ethtool_ops->set_msglevel);
		break;
	case ETHTOOL_NWAY_RST:
		rc = ethtool_nway_reset(dev);
		break;
	case ETHTOOL_GLINK:
		rc = ethtool_get_value(dev, useraddr, ethcmd,
				       dev->ethtool_ops->get_link);
		break;
	case ETHTOOL_GEEPROM:
		rc = ethtool_get_eeprom(dev, useraddr);
		break;
	case ETHTOOL_SEEPROM:
		rc = ethtool_set_eeprom(dev, useraddr);
		break;
	case ETHTOOL_GCOALESCE:
		rc = ethtool_get_coalesce(dev, useraddr);
		break;
	case ETHTOOL_SCOALESCE:
		rc = ethtool_set_coalesce(dev, useraddr);
		break;
	case ETHTOOL_GRINGPARAM:
		rc = ethtool_get_ringparam(dev, useraddr);
		break;
	case ETHTOOL_SRINGPARAM:
		rc = ethtool_set_ringparam(dev, useraddr);
		break;
	case ETHTOOL_GPAUSEPARAM:
		rc = ethtool_get_pauseparam(dev, useraddr);
		break;
	case ETHTOOL_SPAUSEPARAM:
		rc = ethtool_set_pauseparam(dev, useraddr);
		break;
	case ETHTOOL_GRXCSUM:
		rc = ethtool_get_value(dev, useraddr, ethcmd,
				       (dev->ethtool_ops->get_rx_csum ?
					dev->ethtool_ops->get_rx_csum :
					ethtool_op_get_rx_csum));
		break;
	case ETHTOOL_SRXCSUM:
		rc = ethtool_set_rx_csum(dev, useraddr);
		break;
	case ETHTOOL_GTXCSUM:
		rc = ethtool_get_value(dev, useraddr, ethcmd,
				       (dev->ethtool_ops->get_tx_csum ?
					dev->ethtool_ops->get_tx_csum :
					ethtool_op_get_tx_csum));
		break;
	case ETHTOOL_STXCSUM:
		rc = ethtool_set_tx_csum(dev, useraddr);
		break;
	case ETHTOOL_GSG:
		rc = ethtool_get_value(dev, useraddr, ethcmd,
				       (dev->ethtool_ops->get_sg ?
					dev->ethtool_ops->get_sg :
					ethtool_op_get_sg));
		break;
	case ETHTOOL_SSG:
		rc = ethtool_set_sg(dev, useraddr);
		break;
	case ETHTOOL_GTSO:
		rc = ethtool_get_value(dev, useraddr, ethcmd,
				       (dev->ethtool_ops->get_tso ?
					dev->ethtool_ops->get_tso :
					ethtool_op_get_tso));
		break;
	case ETHTOOL_STSO:
		rc = ethtool_set_tso(dev, useraddr);
		break;
	case ETHTOOL_TEST:
		rc = ethtool_self_test(dev, useraddr);
		break;
	case ETHTOOL_GSTRINGS:
		rc = ethtool_get_strings(dev, useraddr);
		break;
	case ETHTOOL_PHYS_ID:
		rc = ethtool_phys_id(dev, useraddr);
		break;
	case ETHTOOL_GSTATS:
		rc = ethtool_get_stats(dev, useraddr);
		break;
	case ETHTOOL_GPERMADDR:
		rc = ethtool_get_perm_addr(dev, useraddr);
		break;
	case ETHTOOL_GUFO:
		rc = ethtool_get_value(dev, useraddr, ethcmd,
				       (dev->ethtool_ops->get_ufo ?
					dev->ethtool_ops->get_ufo :
					ethtool_op_get_ufo));
		break;
	case ETHTOOL_SUFO:
		rc = ethtool_set_ufo(dev, useraddr);
		break;
	case ETHTOOL_GGSO:
		rc = ethtool_get_gso(dev, useraddr);
		break;
	case ETHTOOL_SGSO:
		rc = ethtool_set_gso(dev, useraddr);
		break;
	case ETHTOOL_GFLAGS:
		rc = ethtool_get_value(dev, useraddr, ethcmd,
				       (dev->ethtool_ops->get_flags ?
					dev->ethtool_ops->get_flags :
					ethtool_op_get_flags));
		break;
	case ETHTOOL_SFLAGS:
		rc = ethtool_set_value(dev, useraddr,
				       dev->ethtool_ops->set_flags);
		break;
	case ETHTOOL_GPFLAGS:
		rc = ethtool_get_value(dev, useraddr, ethcmd,
				       dev->ethtool_ops->get_priv_flags);
		break;
	case ETHTOOL_SPFLAGS:
		rc = ethtool_set_value(dev, useraddr,
				       dev->ethtool_ops->set_priv_flags);
		break;
	case ETHTOOL_GRXFH:
	case ETHTOOL_GRXRINGS:
	case ETHTOOL_GRXCLSRLCNT:
	case ETHTOOL_GRXCLSRULE:
	case ETHTOOL_GRXCLSRLALL:
		rc = ethtool_get_rxnfc(dev, useraddr);
		break;
	case ETHTOOL_SRXFH:
	case ETHTOOL_SRXCLSRLDEL:
	case ETHTOOL_SRXCLSRLINS:
		rc = ethtool_set_rxnfc(dev, useraddr);
		break;
	case ETHTOOL_GGRO:
		rc = ethtool_get_gro(dev, useraddr);
		break;
	case ETHTOOL_SGRO:
		rc = ethtool_set_gro(dev, useraddr);
		break;
	case ETHTOOL_FLASHDEV:
		rc = ethtool_flash_device(dev, useraddr);
		break;
	case ETHTOOL_RESET:
		rc = ethtool_reset(dev, useraddr);
		break;
	case ETHTOOL_SRXNTUPLE:
		rc = ethtool_set_rx_ntuple(dev, useraddr);
		break;
	case ETHTOOL_GRXNTUPLE:
		rc = ethtool_get_rx_ntuple(dev, useraddr);
		break;
	case ETHTOOL_GSSET_INFO:
		rc = ethtool_get_sset_info(dev, useraddr);
		break;
	default:
		rc = -EOPNOTSUPP;
	}

	if (dev->ethtool_ops->complete)
		dev->ethtool_ops->complete(dev);

	if (old_features != dev->features)
		netdev_features_change(dev);

	return rc;
}

EXPORT_SYMBOL(ethtool_op_get_link);
EXPORT_SYMBOL(ethtool_op_get_sg);
EXPORT_SYMBOL(ethtool_op_get_tso);
EXPORT_SYMBOL(ethtool_op_set_sg);
EXPORT_SYMBOL(ethtool_op_set_tso);
EXPORT_SYMBOL(ethtool_op_set_tx_csum);
EXPORT_SYMBOL(ethtool_op_set_tx_hw_csum);
EXPORT_SYMBOL(ethtool_op_set_tx_ipv6_csum);
EXPORT_SYMBOL(ethtool_op_set_ufo);
EXPORT_SYMBOL(ethtool_op_get_ufo);
EXPORT_SYMBOL(ethtool_op_set_flags);
EXPORT_SYMBOL(ethtool_op_get_flags);
