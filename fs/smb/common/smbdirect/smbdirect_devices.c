// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *   Copyright (C) 2018, LG Electronics.
 *   Copyright (c) 2025 Stefan Metzmacher
 */

#include "smbdirect_internal.h"

static u8 smbdirect_ib_device_rdma_capable_node_type(struct ib_device *ib_dev)
{
	if (!smbdirect_frwr_is_supported(&ib_dev->attrs))
		return RDMA_NODE_UNSPECIFIED;

	switch (ib_dev->node_type) {
	case RDMA_NODE_IB_CA: /* Infiniband, RoCE v1 and v2 */
	case RDMA_NODE_RNIC:  /* iWarp */
		return ib_dev->node_type;
	}

	return RDMA_NODE_UNSPECIFIED;
}

static int smbdirect_ib_client_add(struct ib_device *ib_dev)
{
	u8 node_type = smbdirect_ib_device_rdma_capable_node_type(ib_dev);
	struct smbdirect_device *sdev;
	const char *node_str;
	const char *action;
	u32 pidx;

	switch (node_type) {
	case RDMA_NODE_IB_CA:
		node_str = "IB_CA";
		action = "added";
		break;
	case RDMA_NODE_RNIC:
		node_str = "RNIC";
		action = "added";
		break;
	case RDMA_NODE_UNSPECIFIED:
		node_str = "UNSPECIFIED";
		action = "ignored";
		break;
	default:
		node_str = "UNKNOWN";
		action = "ignored";
		node_type = RDMA_NODE_UNSPECIFIED;
		break;
	}

	pr_info("ib_dev[%.*s]: %s: %s %s=%u %s=0x%llx %s=0x%llx %s=0x%llx\n",
		IB_DEVICE_NAME_MAX,
		ib_dev->name,
		action,
		node_str,
		"max_fast_reg_page_list_len",
		ib_dev->attrs.max_fast_reg_page_list_len,
		"device_cap_flags",
		ib_dev->attrs.device_cap_flags,
		"kernel_cap_flags",
		ib_dev->attrs.kernel_cap_flags,
		"page_size_cap",
		ib_dev->attrs.page_size_cap);

	if (node_type == RDMA_NODE_UNSPECIFIED)
		return 0;

	pr_info("ib_dev[%.*s]: %s=%u %s=%u %s=%u %s=%u %s=%u %s=%u %s=%u %s=%u %s=%u\n",
		IB_DEVICE_NAME_MAX,
		ib_dev->name,
		"num_ports",
		rdma_end_port(ib_dev),
		"max_qp_rd_atom",
		ib_dev->attrs.max_qp_rd_atom,
		"max_qp_init_rd_atom",
		ib_dev->attrs.max_qp_init_rd_atom,
		"max_sgl_rd",
		ib_dev->attrs.max_sgl_rd,
		"max_sge_rd",
		ib_dev->attrs.max_sge_rd,
		"max_cqe",
		ib_dev->attrs.max_cqe,
		"max_qp_wr",
		ib_dev->attrs.max_qp_wr,
		"max_send_sge",
		ib_dev->attrs.max_send_sge,
		"max_recv_sge",
		ib_dev->attrs.max_recv_sge);

	rdma_for_each_port(ib_dev, pidx) {
		const struct ib_port_immutable *ib_pi =
			ib_port_immutable_read(ib_dev, pidx);
		u32 core_cap_flags = ib_pi ? ib_pi->core_cap_flags : 0;

		pr_info("ib_dev[%.*s]PORT[%u]: %s=%u %s=%u %s=%u %s=%u %s=%u %s=0x%x\n",
			IB_DEVICE_NAME_MAX,
			ib_dev->name,
			pidx,
			"iwarp",
			rdma_protocol_iwarp(ib_dev, pidx),
			"ib",
			rdma_protocol_ib(ib_dev, pidx),
			"roce",
			rdma_protocol_roce(ib_dev, pidx),
			"v1",
			rdma_protocol_roce_eth_encap(ib_dev, pidx),
			"v2",
			rdma_protocol_roce_udp_encap(ib_dev, pidx),
			"core_cap_flags",
			core_cap_flags);
	}

	sdev = kzalloc_obj(*sdev);
	if (!sdev)
		return -ENOMEM;
	sdev->ib_dev = ib_dev;
	snprintf(sdev->ib_name, ARRAY_SIZE(sdev->ib_name), "%.*s",
		 IB_DEVICE_NAME_MAX, ib_dev->name);

	write_lock(&smbdirect_globals.devices.lock);
	list_add(&sdev->list, &smbdirect_globals.devices.list);
	write_unlock(&smbdirect_globals.devices.lock);

	return 0;
}

static void smbdirect_ib_client_remove(struct ib_device *ib_dev, void *client_data)
{
	struct smbdirect_device *sdev, *tmp;

	write_lock(&smbdirect_globals.devices.lock);
	list_for_each_entry_safe(sdev, tmp, &smbdirect_globals.devices.list, list) {
		if (sdev->ib_dev == ib_dev) {
			list_del(&sdev->list);
			pr_info("ib_dev[%.*s] removed\n",
				IB_DEVICE_NAME_MAX, sdev->ib_name);
			kfree(sdev);
			break;
		}
	}
	write_unlock(&smbdirect_globals.devices.lock);
}

static void smbdirect_ib_client_rename(struct ib_device *ib_dev, void *client_data)
{
	struct smbdirect_device *sdev;

	write_lock(&smbdirect_globals.devices.lock);
	list_for_each_entry(sdev, &smbdirect_globals.devices.list, list) {
		if (sdev->ib_dev == ib_dev) {
			pr_info("ib_dev[%.*s] renamed to [%.*s]\n",
				IB_DEVICE_NAME_MAX, sdev->ib_name,
				IB_DEVICE_NAME_MAX, ib_dev->name);
			snprintf(sdev->ib_name, ARRAY_SIZE(sdev->ib_name), "%.*s",
				 IB_DEVICE_NAME_MAX, ib_dev->name);
			break;
		}
	}
	write_unlock(&smbdirect_globals.devices.lock);
}

static struct ib_client smbdirect_ib_client = {
	.name	= "smbdirect_ib_client",
	.add	= smbdirect_ib_client_add,
	.remove	= smbdirect_ib_client_remove,
	.rename	= smbdirect_ib_client_rename,
};

static u8 smbdirect_netdev_find_rdma_capable_node_type(struct net_device *netdev)
{
	struct smbdirect_device *sdev;
	u8 node_type = RDMA_NODE_UNSPECIFIED;

	read_lock(&smbdirect_globals.devices.lock);
	list_for_each_entry(sdev, &smbdirect_globals.devices.list, list) {
		u32 pi;

		rdma_for_each_port(sdev->ib_dev, pi) {
			struct net_device *ndev;

			ndev = ib_device_get_netdev(sdev->ib_dev, pi);
			if (!ndev)
				continue;

			if (ndev == netdev) {
				dev_put(ndev);
				node_type = sdev->ib_dev->node_type;
				goto out;
			}
			dev_put(ndev);
		}
	}
out:
	read_unlock(&smbdirect_globals.devices.lock);

	if (node_type == RDMA_NODE_UNSPECIFIED) {
		struct ib_device *ibdev;

		ibdev = ib_device_get_by_netdev(netdev, RDMA_DRIVER_UNKNOWN);
		if (ibdev) {
			node_type = smbdirect_ib_device_rdma_capable_node_type(ibdev);
			ib_device_put(ibdev);
		}
	}

	return node_type;
}

/*
 * Returns RDMA_NODE_UNSPECIFIED when the netdev has
 * no support for smbdirect capable rdma.
 *
 * Otherwise RDMA_NODE_RNIC is returned for iwarp devices
 * and RDMA_NODE_IB_CA or Infiniband and RoCE (v1 and v2)
 */
u8 smbdirect_netdev_rdma_capable_node_type(struct net_device *netdev)
{
	struct net_device *lower_dev;
	struct list_head *iter;
	u8 node_type = RDMA_NODE_UNSPECIFIED;

	node_type = smbdirect_netdev_find_rdma_capable_node_type(netdev);
	if (node_type != RDMA_NODE_UNSPECIFIED)
		return node_type;

	/* check if netdev is bridge or VLAN */
	if (netif_is_bridge_master(netdev) || netdev->priv_flags & IFF_802_1Q_VLAN)
		netdev_for_each_lower_dev(netdev, lower_dev, iter) {
			node_type = smbdirect_netdev_find_rdma_capable_node_type(lower_dev);
			if (node_type != RDMA_NODE_UNSPECIFIED)
				return node_type;
		}

	/* check if netdev is IPoIB safely without layer violation */
	if (netdev->type == ARPHRD_INFINIBAND)
		return RDMA_NODE_IB_CA;

	return RDMA_NODE_UNSPECIFIED;
}
__SMBDIRECT_EXPORT_SYMBOL__(smbdirect_netdev_rdma_capable_node_type);

__init int smbdirect_devices_init(void)
{
	int ret;

	rwlock_init(&smbdirect_globals.devices.lock);
	INIT_LIST_HEAD(&smbdirect_globals.devices.list);

	ret = ib_register_client(&smbdirect_ib_client);
	if (ret) {
		pr_crit("failed to ib_register_client: %d %1pe\n",
			ret, SMBDIRECT_DEBUG_ERR_PTR(ret));
		return ret;
	}

	return 0;
}

__exit void smbdirect_devices_exit(void)
{
	struct smbdirect_device *sdev, *tmp;

	/*
	 * On exist we just cleanup so that
	 * smbdirect_ib_client_remove() won't
	 * print removals of devices.
	 */
	write_lock(&smbdirect_globals.devices.lock);
	list_for_each_entry_safe(sdev, tmp, &smbdirect_globals.devices.list, list) {
		list_del(&sdev->list);
		kfree(sdev);
	}
	write_unlock(&smbdirect_globals.devices.lock);

	ib_unregister_client(&smbdirect_ib_client);
}
