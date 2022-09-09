// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Ethernet-type device handling.
 *
 * Version:	@(#)eth.c	1.0.7	05/25/93
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Mark Evans, <evansmp@uhura.aston.ac.uk>
 *		Florian  La Roche, <rzsfl@rz.uni-sb.de>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *
 * Fixes:
 *		Mr Linux	: Arp problems
 *		Alan Cox	: Generic queue tidyup (very tiny here)
 *		Alan Cox	: eth_header ntohs should be htons
 *		Alan Cox	: eth_rebuild_header missing an htons and
 *				  minor other things.
 *		Tegge		: Arp bug fixes.
 *		Florian		: Removed many unnecessary functions, code cleanup
 *				  and changes for new arp and skbuff.
 *		Alan Cox	: Redid header building to reflect new format.
 *		Alan Cox	: ARP only when compiled with CONFIG_INET
 *		Greg Page	: 802.2 and SNAP stuff.
 *		Alan Cox	: MAC layer pointers/new format.
 *		Paul Gortmaker	: eth_copy_and_sum shouldn't csum padding.
 *		Alan Cox	: Protect against forwarding explosions with
 *				  older network drivers and IFF_ALLMULTI.
 *	Christer Weinigel	: Better rebuild header message.
 *             Andrew Morton    : 26Feb01: kill ether_setup() - use netdev_boot_setup().
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/ip.h>
#include <linux/netdevice.h>
#include <linux/nvmem-consumer.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/if_ether.h>
#include <linux/of_net.h>
#include <linux/pci.h>
#include <linux/property.h>
#include <net/dst.h>
#include <net/arp.h>
#include <net/sock.h>
#include <net/ipv6.h>
#include <net/ip.h>
#include <net/dsa.h>
#include <net/flow_dissector.h>
#include <net/gro.h>
#include <linux/uaccess.h>
#include <net/pkt_sched.h>

/**
 * eth_header - create the Ethernet header
 * @skb:	buffer to alter
 * @dev:	source device
 * @type:	Ethernet type field
 * @daddr: destination address (NULL leave destination address)
 * @saddr: source address (NULL use device source address)
 * @len:   packet length (<= skb->len)
 *
 *
 * Set the protocol type. For a packet of type ETH_P_802_3/2 we put the length
 * in here instead.
 */
int eth_header(struct sk_buff *skb, struct net_device *dev,
	       unsigned short type,
	       const void *daddr, const void *saddr, unsigned int len)
{
	struct ethhdr *eth = skb_push(skb, ETH_HLEN);

	if (type != ETH_P_802_3 && type != ETH_P_802_2)
		eth->h_proto = htons(type);
	else
		eth->h_proto = htons(len);

	/*
	 *      Set the source hardware address.
	 */

	if (!saddr)
		saddr = dev->dev_addr;
	memcpy(eth->h_source, saddr, ETH_ALEN);

	if (daddr) {
		memcpy(eth->h_dest, daddr, ETH_ALEN);
		return ETH_HLEN;
	}

	/*
	 *      Anyway, the loopback-device should never use this function...
	 */

	if (dev->flags & (IFF_LOOPBACK | IFF_NOARP)) {
		eth_zero_addr(eth->h_dest);
		return ETH_HLEN;
	}

	return -ETH_HLEN;
}
EXPORT_SYMBOL(eth_header);

/**
 * eth_get_headlen - determine the length of header for an ethernet frame
 * @dev: pointer to network device
 * @data: pointer to start of frame
 * @len: total length of frame
 *
 * Make a best effort attempt to pull the length for all of the headers for
 * a given frame in a linear buffer.
 */
u32 eth_get_headlen(const struct net_device *dev, const void *data, u32 len)
{
	const unsigned int flags = FLOW_DISSECTOR_F_PARSE_1ST_FRAG;
	const struct ethhdr *eth = (const struct ethhdr *)data;
	struct flow_keys_basic keys;

	/* this should never happen, but better safe than sorry */
	if (unlikely(len < sizeof(*eth)))
		return len;

	/* parse any remaining L2/L3 headers, check for L4 */
	if (!skb_flow_dissect_flow_keys_basic(dev_net(dev), NULL, &keys, data,
					      eth->h_proto, sizeof(*eth),
					      len, flags))
		return max_t(u32, keys.control.thoff, sizeof(*eth));

	/* parse for any L4 headers */
	return min_t(u32, __skb_get_poff(NULL, data, &keys, len), len);
}
EXPORT_SYMBOL(eth_get_headlen);

/**
 * eth_type_trans - determine the packet's protocol ID.
 * @skb: received socket data
 * @dev: receiving network device
 *
 * The rule here is that we
 * assume 802.3 if the type field is short enough to be a length.
 * This is normal practice and works for any 'now in use' protocol.
 */
__be16 eth_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	unsigned short _service_access_point;
	const unsigned short *sap;
	const struct ethhdr *eth;

	skb->dev = dev;
	skb_reset_mac_header(skb);

	eth = (struct ethhdr *)skb->data;
	skb_pull_inline(skb, ETH_HLEN);

	if (unlikely(!ether_addr_equal_64bits(eth->h_dest,
					      dev->dev_addr))) {
		if (unlikely(is_multicast_ether_addr_64bits(eth->h_dest))) {
			if (ether_addr_equal_64bits(eth->h_dest, dev->broadcast))
				skb->pkt_type = PACKET_BROADCAST;
			else
				skb->pkt_type = PACKET_MULTICAST;
		} else {
			skb->pkt_type = PACKET_OTHERHOST;
		}
	}

	/*
	 * Some variants of DSA tagging don't have an ethertype field
	 * at all, so we check here whether one of those tagging
	 * variants has been configured on the receiving interface,
	 * and if so, set skb->protocol without looking at the packet.
	 */
	if (unlikely(netdev_uses_dsa(dev)))
		return htons(ETH_P_XDSA);

	if (likely(eth_proto_is_802_3(eth->h_proto)))
		return eth->h_proto;

	/*
	 *      This is a magic hack to spot IPX packets. Older Novell breaks
	 *      the protocol design and runs IPX over 802.3 without an 802.2 LLC
	 *      layer. We look for FFFF which isn't a used 802.2 SSAP/DSAP. This
	 *      won't work for fault tolerant netware but does for the rest.
	 */
	sap = skb_header_pointer(skb, 0, sizeof(*sap), &_service_access_point);
	if (sap && *sap == 0xFFFF)
		return htons(ETH_P_802_3);

	/*
	 *      Real 802.2 LLC
	 */
	return htons(ETH_P_802_2);
}
EXPORT_SYMBOL(eth_type_trans);

/**
 * eth_header_parse - extract hardware address from packet
 * @skb: packet to extract header from
 * @haddr: destination buffer
 */
int eth_header_parse(const struct sk_buff *skb, unsigned char *haddr)
{
	const struct ethhdr *eth = eth_hdr(skb);
	memcpy(haddr, eth->h_source, ETH_ALEN);
	return ETH_ALEN;
}
EXPORT_SYMBOL(eth_header_parse);

/**
 * eth_header_cache - fill cache entry from neighbour
 * @neigh: source neighbour
 * @hh: destination cache entry
 * @type: Ethernet type field
 *
 * Create an Ethernet header template from the neighbour.
 */
int eth_header_cache(const struct neighbour *neigh, struct hh_cache *hh, __be16 type)
{
	struct ethhdr *eth;
	const struct net_device *dev = neigh->dev;

	eth = (struct ethhdr *)
	    (((u8 *) hh->hh_data) + (HH_DATA_OFF(sizeof(*eth))));

	if (type == htons(ETH_P_802_3))
		return -1;

	eth->h_proto = type;
	memcpy(eth->h_source, dev->dev_addr, ETH_ALEN);
	memcpy(eth->h_dest, neigh->ha, ETH_ALEN);

	/* Pairs with READ_ONCE() in neigh_resolve_output(),
	 * neigh_hh_output() and neigh_update_hhs().
	 */
	smp_store_release(&hh->hh_len, ETH_HLEN);

	return 0;
}
EXPORT_SYMBOL(eth_header_cache);

/**
 * eth_header_cache_update - update cache entry
 * @hh: destination cache entry
 * @dev: network device
 * @haddr: new hardware address
 *
 * Called by Address Resolution module to notify changes in address.
 */
void eth_header_cache_update(struct hh_cache *hh,
			     const struct net_device *dev,
			     const unsigned char *haddr)
{
	memcpy(((u8 *) hh->hh_data) + HH_DATA_OFF(sizeof(struct ethhdr)),
	       haddr, ETH_ALEN);
}
EXPORT_SYMBOL(eth_header_cache_update);

/**
 * eth_header_parse_protocol - extract protocol from L2 header
 * @skb: packet to extract protocol from
 */
__be16 eth_header_parse_protocol(const struct sk_buff *skb)
{
	const struct ethhdr *eth = eth_hdr(skb);

	return eth->h_proto;
}
EXPORT_SYMBOL(eth_header_parse_protocol);

/**
 * eth_prepare_mac_addr_change - prepare for mac change
 * @dev: network device
 * @p: socket address
 */
int eth_prepare_mac_addr_change(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;

	if (!(dev->priv_flags & IFF_LIVE_ADDR_CHANGE) && netif_running(dev))
		return -EBUSY;
	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;
	return 0;
}
EXPORT_SYMBOL(eth_prepare_mac_addr_change);

/**
 * eth_commit_mac_addr_change - commit mac change
 * @dev: network device
 * @p: socket address
 */
void eth_commit_mac_addr_change(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;

	eth_hw_addr_set(dev, addr->sa_data);
}
EXPORT_SYMBOL(eth_commit_mac_addr_change);

/**
 * eth_mac_addr - set new Ethernet hardware address
 * @dev: network device
 * @p: socket address
 *
 * Change hardware address of device.
 *
 * This doesn't change hardware matching, so needs to be overridden
 * for most real devices.
 */
int eth_mac_addr(struct net_device *dev, void *p)
{
	int ret;

	ret = eth_prepare_mac_addr_change(dev, p);
	if (ret < 0)
		return ret;
	eth_commit_mac_addr_change(dev, p);
	return 0;
}
EXPORT_SYMBOL(eth_mac_addr);

int eth_validate_addr(struct net_device *dev)
{
	if (!is_valid_ether_addr(dev->dev_addr))
		return -EADDRNOTAVAIL;

	return 0;
}
EXPORT_SYMBOL(eth_validate_addr);

const struct header_ops eth_header_ops ____cacheline_aligned = {
	.create		= eth_header,
	.parse		= eth_header_parse,
	.cache		= eth_header_cache,
	.cache_update	= eth_header_cache_update,
	.parse_protocol	= eth_header_parse_protocol,
};

/**
 * ether_setup - setup Ethernet network device
 * @dev: network device
 *
 * Fill in the fields of the device structure with Ethernet-generic values.
 */
void ether_setup(struct net_device *dev)
{
	dev->header_ops		= &eth_header_ops;
	dev->type		= ARPHRD_ETHER;
	dev->hard_header_len 	= ETH_HLEN;
	dev->min_header_len	= ETH_HLEN;
	dev->mtu		= ETH_DATA_LEN;
	dev->min_mtu		= ETH_MIN_MTU;
	dev->max_mtu		= ETH_DATA_LEN;
	dev->addr_len		= ETH_ALEN;
	dev->tx_queue_len	= DEFAULT_TX_QUEUE_LEN;
	dev->flags		= IFF_BROADCAST|IFF_MULTICAST;
	dev->priv_flags		|= IFF_TX_SKB_SHARING;

	eth_broadcast_addr(dev->broadcast);

}
EXPORT_SYMBOL(ether_setup);

/**
 * alloc_etherdev_mqs - Allocates and sets up an Ethernet device
 * @sizeof_priv: Size of additional driver-private structure to be allocated
 *	for this Ethernet device
 * @txqs: The number of TX queues this device has.
 * @rxqs: The number of RX queues this device has.
 *
 * Fill in the fields of the device structure with Ethernet-generic
 * values. Basically does everything except registering the device.
 *
 * Constructs a new net device, complete with a private data area of
 * size (sizeof_priv).  A 32-byte (not bit) alignment is enforced for
 * this private data area.
 */

struct net_device *alloc_etherdev_mqs(int sizeof_priv, unsigned int txqs,
				      unsigned int rxqs)
{
	return alloc_netdev_mqs(sizeof_priv, "eth%d", NET_NAME_ENUM,
				ether_setup, txqs, rxqs);
}
EXPORT_SYMBOL(alloc_etherdev_mqs);

ssize_t sysfs_format_mac(char *buf, const unsigned char *addr, int len)
{
	return scnprintf(buf, PAGE_SIZE, "%*phC\n", len, addr);
}
EXPORT_SYMBOL(sysfs_format_mac);

struct sk_buff *eth_gro_receive(struct list_head *head, struct sk_buff *skb)
{
	const struct packet_offload *ptype;
	unsigned int hlen, off_eth;
	struct sk_buff *pp = NULL;
	struct ethhdr *eh, *eh2;
	struct sk_buff *p;
	__be16 type;
	int flush = 1;

	off_eth = skb_gro_offset(skb);
	hlen = off_eth + sizeof(*eh);
	eh = skb_gro_header_fast(skb, off_eth);
	if (skb_gro_header_hard(skb, hlen)) {
		eh = skb_gro_header_slow(skb, hlen, off_eth);
		if (unlikely(!eh))
			goto out;
	}

	flush = 0;

	list_for_each_entry(p, head, list) {
		if (!NAPI_GRO_CB(p)->same_flow)
			continue;

		eh2 = (struct ethhdr *)(p->data + off_eth);
		if (compare_ether_header(eh, eh2)) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}
	}

	type = eh->h_proto;

	ptype = gro_find_receive_by_type(type);
	if (ptype == NULL) {
		flush = 1;
		goto out;
	}

	skb_gro_pull(skb, sizeof(*eh));
	skb_gro_postpull_rcsum(skb, eh, sizeof(*eh));

	pp = indirect_call_gro_receive_inet(ptype->callbacks.gro_receive,
					    ipv6_gro_receive, inet_gro_receive,
					    head, skb);

out:
	skb_gro_flush_final(skb, pp, flush);

	return pp;
}
EXPORT_SYMBOL(eth_gro_receive);

int eth_gro_complete(struct sk_buff *skb, int nhoff)
{
	struct ethhdr *eh = (struct ethhdr *)(skb->data + nhoff);
	__be16 type = eh->h_proto;
	struct packet_offload *ptype;
	int err = -ENOSYS;

	if (skb->encapsulation)
		skb_set_inner_mac_header(skb, nhoff);

	ptype = gro_find_complete_by_type(type);
	if (ptype != NULL)
		err = INDIRECT_CALL_INET(ptype->callbacks.gro_complete,
					 ipv6_gro_complete, inet_gro_complete,
					 skb, nhoff + sizeof(*eh));

	return err;
}
EXPORT_SYMBOL(eth_gro_complete);

static struct packet_offload eth_packet_offload __read_mostly = {
	.type = cpu_to_be16(ETH_P_TEB),
	.priority = 10,
	.callbacks = {
		.gro_receive = eth_gro_receive,
		.gro_complete = eth_gro_complete,
	},
};

static int __init eth_offload_init(void)
{
	dev_add_offload(&eth_packet_offload);

	return 0;
}

fs_initcall(eth_offload_init);

unsigned char * __weak arch_get_platform_mac_address(void)
{
	return NULL;
}

int eth_platform_get_mac_address(struct device *dev, u8 *mac_addr)
{
	unsigned char *addr;
	int ret;

	ret = of_get_mac_address(dev->of_node, mac_addr);
	if (!ret)
		return 0;

	addr = arch_get_platform_mac_address();
	if (!addr)
		return -ENODEV;

	ether_addr_copy(mac_addr, addr);

	return 0;
}
EXPORT_SYMBOL(eth_platform_get_mac_address);

/**
 * platform_get_ethdev_address - Set netdev's MAC address from a given device
 * @dev:	Pointer to the device
 * @netdev:	Pointer to netdev to write the address to
 *
 * Wrapper around eth_platform_get_mac_address() which writes the address
 * directly to netdev->dev_addr.
 */
int platform_get_ethdev_address(struct device *dev, struct net_device *netdev)
{
	u8 addr[ETH_ALEN] __aligned(2);
	int ret;

	ret = eth_platform_get_mac_address(dev, addr);
	if (!ret)
		eth_hw_addr_set(netdev, addr);
	return ret;
}
EXPORT_SYMBOL(platform_get_ethdev_address);

/**
 * nvmem_get_mac_address - Obtain the MAC address from an nvmem cell named
 * 'mac-address' associated with given device.
 *
 * @dev:	Device with which the mac-address cell is associated.
 * @addrbuf:	Buffer to which the MAC address will be copied on success.
 *
 * Returns 0 on success or a negative error number on failure.
 */
int nvmem_get_mac_address(struct device *dev, void *addrbuf)
{
	struct nvmem_cell *cell;
	const void *mac;
	size_t len;

	cell = nvmem_cell_get(dev, "mac-address");
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	mac = nvmem_cell_read(cell, &len);
	nvmem_cell_put(cell);

	if (IS_ERR(mac))
		return PTR_ERR(mac);

	if (len != ETH_ALEN || !is_valid_ether_addr(mac)) {
		kfree(mac);
		return -EINVAL;
	}

	ether_addr_copy(addrbuf, mac);
	kfree(mac);

	return 0;
}

static int fwnode_get_mac_addr(struct fwnode_handle *fwnode,
			       const char *name, char *addr)
{
	int ret;

	ret = fwnode_property_read_u8_array(fwnode, name, addr, ETH_ALEN);
	if (ret)
		return ret;

	if (!is_valid_ether_addr(addr))
		return -EINVAL;
	return 0;
}

/**
 * fwnode_get_mac_address - Get the MAC from the firmware node
 * @fwnode:	Pointer to the firmware node
 * @addr:	Address of buffer to store the MAC in
 *
 * Search the firmware node for the best MAC address to use.  'mac-address' is
 * checked first, because that is supposed to contain to "most recent" MAC
 * address. If that isn't set, then 'local-mac-address' is checked next,
 * because that is the default address.  If that isn't set, then the obsolete
 * 'address' is checked, just in case we're using an old device tree.
 *
 * Note that the 'address' property is supposed to contain a virtual address of
 * the register set, but some DTS files have redefined that property to be the
 * MAC address.
 *
 * All-zero MAC addresses are rejected, because those could be properties that
 * exist in the firmware tables, but were not updated by the firmware.  For
 * example, the DTS could define 'mac-address' and 'local-mac-address', with
 * zero MAC addresses.  Some older U-Boots only initialized 'local-mac-address'.
 * In this case, the real MAC is in 'local-mac-address', and 'mac-address'
 * exists but is all zeros.
 */
int fwnode_get_mac_address(struct fwnode_handle *fwnode, char *addr)
{
	if (!fwnode_get_mac_addr(fwnode, "mac-address", addr) ||
	    !fwnode_get_mac_addr(fwnode, "local-mac-address", addr) ||
	    !fwnode_get_mac_addr(fwnode, "address", addr))
		return 0;

	return -ENOENT;
}
EXPORT_SYMBOL(fwnode_get_mac_address);

/**
 * device_get_mac_address - Get the MAC for a given device
 * @dev:	Pointer to the device
 * @addr:	Address of buffer to store the MAC in
 */
int device_get_mac_address(struct device *dev, char *addr)
{
	return fwnode_get_mac_address(dev_fwnode(dev), addr);
}
EXPORT_SYMBOL(device_get_mac_address);

/**
 * device_get_ethdev_address - Set netdev's MAC address from a given device
 * @dev:	Pointer to the device
 * @netdev:	Pointer to netdev to write the address to
 *
 * Wrapper around device_get_mac_address() which writes the address
 * directly to netdev->dev_addr.
 */
int device_get_ethdev_address(struct device *dev, struct net_device *netdev)
{
	u8 addr[ETH_ALEN];
	int ret;

	ret = device_get_mac_address(dev, addr);
	if (!ret)
		eth_hw_addr_set(netdev, addr);
	return ret;
}
EXPORT_SYMBOL(device_get_ethdev_address);
