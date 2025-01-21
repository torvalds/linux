/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  NET  is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the Ethernet handlers.
 *
 * Version:	@(#)eth.h	1.0.4	05/13/93
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		Relocated to include/linux where it belongs by Alan Cox
 *							<gw4pts@gw4pts.ampr.org>
 */
#ifndef _LINUX_ETHERDEVICE_H
#define _LINUX_ETHERDEVICE_H

#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/random.h>
#include <linux/crc32.h>
#include <linux/unaligned.h>
#include <asm/bitsperlong.h>

#ifdef __KERNEL__
struct device;
struct fwnode_handle;

int eth_platform_get_mac_address(struct device *dev, u8 *mac_addr);
int platform_get_ethdev_address(struct device *dev, struct net_device *netdev);
unsigned char *arch_get_platform_mac_address(void);
int nvmem_get_mac_address(struct device *dev, void *addrbuf);
int device_get_mac_address(struct device *dev, char *addr);
int device_get_ethdev_address(struct device *dev, struct net_device *netdev);
int fwnode_get_mac_address(struct fwnode_handle *fwnode, char *addr);

u32 eth_get_headlen(const struct net_device *dev, const void *data, u32 len);
__be16 eth_type_trans(struct sk_buff *skb, struct net_device *dev);
extern const struct header_ops eth_header_ops;

int eth_header(struct sk_buff *skb, struct net_device *dev, unsigned short type,
	       const void *daddr, const void *saddr, unsigned len);
int eth_header_parse(const struct sk_buff *skb, unsigned char *haddr);
int eth_header_cache(const struct neighbour *neigh, struct hh_cache *hh,
		     __be16 type);
void eth_header_cache_update(struct hh_cache *hh, const struct net_device *dev,
			     const unsigned char *haddr);
__be16 eth_header_parse_protocol(const struct sk_buff *skb);
int eth_prepare_mac_addr_change(struct net_device *dev, void *p);
void eth_commit_mac_addr_change(struct net_device *dev, void *p);
int eth_mac_addr(struct net_device *dev, void *p);
int eth_validate_addr(struct net_device *dev);

struct net_device *alloc_etherdev_mqs(int sizeof_priv, unsigned int txqs,
					    unsigned int rxqs);
#define alloc_etherdev(sizeof_priv) alloc_etherdev_mq(sizeof_priv, 1)
#define alloc_etherdev_mq(sizeof_priv, count) alloc_etherdev_mqs(sizeof_priv, count, count)

struct net_device *devm_alloc_etherdev_mqs(struct device *dev, int sizeof_priv,
					   unsigned int txqs,
					   unsigned int rxqs);
#define devm_alloc_etherdev(dev, sizeof_priv) devm_alloc_etherdev_mqs(dev, sizeof_priv, 1, 1)

struct sk_buff *eth_gro_receive(struct list_head *head, struct sk_buff *skb);
int eth_gro_complete(struct sk_buff *skb, int nhoff);

/* Reserved Ethernet Addresses per IEEE 802.1Q */
static const u8 eth_reserved_addr_base[ETH_ALEN] __aligned(2) =
{ 0x01, 0x80, 0xc2, 0x00, 0x00, 0x00 };
#define eth_stp_addr eth_reserved_addr_base

static const u8 eth_ipv4_mcast_addr_base[ETH_ALEN] __aligned(2) =
{ 0x01, 0x00, 0x5e, 0x00, 0x00, 0x00 };

static const u8 eth_ipv6_mcast_addr_base[ETH_ALEN] __aligned(2) =
{ 0x33, 0x33, 0x00, 0x00, 0x00, 0x00 };

/**
 * is_link_local_ether_addr - Determine if given Ethernet address is link-local
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Return true if address is link local reserved addr (01:80:c2:00:00:0X) per
 * IEEE 802.1Q 8.6.3 Frame filtering.
 *
 * Please note: addr must be aligned to u16.
 */
static inline bool is_link_local_ether_addr(const u8 *addr)
{
	__be16 *a = (__be16 *)addr;
	static const __be16 *b = (const __be16 *)eth_reserved_addr_base;
	static const __be16 m = cpu_to_be16(0xfff0);

#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	return (((*(const u32 *)addr) ^ (*(const u32 *)b)) |
		(__force int)((a[2] ^ b[2]) & m)) == 0;
#else
	return ((a[0] ^ b[0]) | (a[1] ^ b[1]) | ((a[2] ^ b[2]) & m)) == 0;
#endif
}

/**
 * is_zero_ether_addr - Determine if give Ethernet address is all zeros.
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Return true if the address is all zeroes.
 *
 * Please note: addr must be aligned to u16.
 */
static inline bool is_zero_ether_addr(const u8 *addr)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	return ((*(const u32 *)addr) | (*(const u16 *)(addr + 4))) == 0;
#else
	return (*(const u16 *)(addr + 0) |
		*(const u16 *)(addr + 2) |
		*(const u16 *)(addr + 4)) == 0;
#endif
}

/**
 * is_multicast_ether_addr - Determine if the Ethernet address is a multicast.
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Return true if the address is a multicast address.
 * By definition the broadcast address is also a multicast address.
 */
static inline bool is_multicast_ether_addr(const u8 *addr)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	u32 a = *(const u32 *)addr;
#else
	u16 a = *(const u16 *)addr;
#endif
#ifdef __BIG_ENDIAN
	return 0x01 & (a >> ((sizeof(a) * 8) - 8));
#else
	return 0x01 & a;
#endif
}

static inline bool is_multicast_ether_addr_64bits(const u8 *addr)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
#ifdef __BIG_ENDIAN
	return 0x01 & ((*(const u64 *)addr) >> 56);
#else
	return 0x01 & (*(const u64 *)addr);
#endif
#else
	return is_multicast_ether_addr(addr);
#endif
}

/**
 * is_local_ether_addr - Determine if the Ethernet address is locally-assigned one (IEEE 802).
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Return true if the address is a local address.
 */
static inline bool is_local_ether_addr(const u8 *addr)
{
	return 0x02 & addr[0];
}

/**
 * is_broadcast_ether_addr - Determine if the Ethernet address is broadcast
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Return true if the address is the broadcast address.
 *
 * Please note: addr must be aligned to u16.
 */
static inline bool is_broadcast_ether_addr(const u8 *addr)
{
	return (*(const u16 *)(addr + 0) &
		*(const u16 *)(addr + 2) &
		*(const u16 *)(addr + 4)) == 0xffff;
}

/**
 * is_unicast_ether_addr - Determine if the Ethernet address is unicast
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Return true if the address is a unicast address.
 */
static inline bool is_unicast_ether_addr(const u8 *addr)
{
	return !is_multicast_ether_addr(addr);
}

/**
 * is_valid_ether_addr - Determine if the given Ethernet address is valid
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Check that the Ethernet address (MAC) is not 00:00:00:00:00:00, is not
 * a multicast address, and is not FF:FF:FF:FF:FF:FF.
 *
 * Return true if the address is valid.
 *
 * Please note: addr must be aligned to u16.
 */
static inline bool is_valid_ether_addr(const u8 *addr)
{
	/* FF:FF:FF:FF:FF:FF is a multicast address so we don't need to
	 * explicitly check for it here. */
	return !is_multicast_ether_addr(addr) && !is_zero_ether_addr(addr);
}

/**
 * eth_proto_is_802_3 - Determine if a given Ethertype/length is a protocol
 * @proto: Ethertype/length value to be tested
 *
 * Check that the value from the Ethertype/length field is a valid Ethertype.
 *
 * Return true if the valid is an 802.3 supported Ethertype.
 */
static inline bool eth_proto_is_802_3(__be16 proto)
{
#ifndef __BIG_ENDIAN
	/* if CPU is little endian mask off bits representing LSB */
	proto &= htons(0xFF00);
#endif
	/* cast both to u16 and compare since LSB can be ignored */
	return (__force u16)proto >= (__force u16)htons(ETH_P_802_3_MIN);
}

/**
 * eth_random_addr - Generate software assigned random Ethernet address
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Generate a random Ethernet address (MAC) that is not multicast
 * and has the local assigned bit set.
 */
static inline void eth_random_addr(u8 *addr)
{
	get_random_bytes(addr, ETH_ALEN);
	addr[0] &= 0xfe;	/* clear multicast bit */
	addr[0] |= 0x02;	/* set local assignment bit (IEEE802) */
}

/**
 * eth_broadcast_addr - Assign broadcast address
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Assign the broadcast address to the given address array.
 */
static inline void eth_broadcast_addr(u8 *addr)
{
	memset(addr, 0xff, ETH_ALEN);
}

/**
 * eth_zero_addr - Assign zero address
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Assign the zero address to the given address array.
 */
static inline void eth_zero_addr(u8 *addr)
{
	memset(addr, 0x00, ETH_ALEN);
}

/**
 * eth_hw_addr_random - Generate software assigned random Ethernet and
 * set device flag
 * @dev: pointer to net_device structure
 *
 * Generate a random Ethernet address (MAC) to be used by a net device
 * and set addr_assign_type so the state can be read by sysfs and be
 * used by userspace.
 */
static inline void eth_hw_addr_random(struct net_device *dev)
{
	u8 addr[ETH_ALEN];

	eth_random_addr(addr);
	__dev_addr_set(dev, addr, ETH_ALEN);
	dev->addr_assign_type = NET_ADDR_RANDOM;
}

/**
 * eth_hw_addr_crc - Calculate CRC from netdev_hw_addr
 * @ha: pointer to hardware address
 *
 * Calculate CRC from a hardware address as basis for filter hashes.
 */
static inline u32 eth_hw_addr_crc(struct netdev_hw_addr *ha)
{
	return ether_crc(ETH_ALEN, ha->addr);
}

/**
 * ether_addr_copy - Copy an Ethernet address
 * @dst: Pointer to a six-byte array Ethernet address destination
 * @src: Pointer to a six-byte array Ethernet address source
 *
 * Please note: dst & src must both be aligned to u16.
 */
static inline void ether_addr_copy(u8 *dst, const u8 *src)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	*(u32 *)dst = *(const u32 *)src;
	*(u16 *)(dst + 4) = *(const u16 *)(src + 4);
#else
	u16 *a = (u16 *)dst;
	const u16 *b = (const u16 *)src;

	a[0] = b[0];
	a[1] = b[1];
	a[2] = b[2];
#endif
}

/**
 * eth_hw_addr_set - Assign Ethernet address to a net_device
 * @dev: pointer to net_device structure
 * @addr: address to assign
 *
 * Assign given address to the net_device, addr_assign_type is not changed.
 */
static inline void eth_hw_addr_set(struct net_device *dev, const u8 *addr)
{
	__dev_addr_set(dev, addr, ETH_ALEN);
}

/**
 * eth_hw_addr_inherit - Copy dev_addr from another net_device
 * @dst: pointer to net_device to copy dev_addr to
 * @src: pointer to net_device to copy dev_addr from
 *
 * Copy the Ethernet address from one net_device to another along with
 * the address attributes (addr_assign_type).
 */
static inline void eth_hw_addr_inherit(struct net_device *dst,
				       struct net_device *src)
{
	dst->addr_assign_type = src->addr_assign_type;
	eth_hw_addr_set(dst, src->dev_addr);
}

/**
 * ether_addr_equal - Compare two Ethernet addresses
 * @addr1: Pointer to a six-byte array containing the Ethernet address
 * @addr2: Pointer other six-byte array containing the Ethernet address
 *
 * Compare two Ethernet addresses, returns true if equal
 *
 * Please note: addr1 & addr2 must both be aligned to u16.
 */
static inline bool ether_addr_equal(const u8 *addr1, const u8 *addr2)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	u32 fold = ((*(const u32 *)addr1) ^ (*(const u32 *)addr2)) |
		   ((*(const u16 *)(addr1 + 4)) ^ (*(const u16 *)(addr2 + 4)));

	return fold == 0;
#else
	const u16 *a = (const u16 *)addr1;
	const u16 *b = (const u16 *)addr2;

	return ((a[0] ^ b[0]) | (a[1] ^ b[1]) | (a[2] ^ b[2])) == 0;
#endif
}

/**
 * ether_addr_equal_64bits - Compare two Ethernet addresses
 * @addr1: Pointer to an array of 8 bytes
 * @addr2: Pointer to an other array of 8 bytes
 *
 * Compare two Ethernet addresses, returns true if equal, false otherwise.
 *
 * The function doesn't need any conditional branches and possibly uses
 * word memory accesses on CPU allowing cheap unaligned memory reads.
 * arrays = { byte1, byte2, byte3, byte4, byte5, byte6, pad1, pad2 }
 *
 * Please note that alignment of addr1 & addr2 are only guaranteed to be 16 bits.
 */

static inline bool ether_addr_equal_64bits(const u8 *addr1, const u8 *addr2)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
	u64 fold = (*(const u64 *)addr1) ^ (*(const u64 *)addr2);

#ifdef __BIG_ENDIAN
	return (fold >> 16) == 0;
#else
	return (fold << 16) == 0;
#endif
#else
	return ether_addr_equal(addr1, addr2);
#endif
}

/**
 * ether_addr_equal_unaligned - Compare two not u16 aligned Ethernet addresses
 * @addr1: Pointer to a six-byte array containing the Ethernet address
 * @addr2: Pointer other six-byte array containing the Ethernet address
 *
 * Compare two Ethernet addresses, returns true if equal
 *
 * Please note: Use only when any Ethernet address may not be u16 aligned.
 */
static inline bool ether_addr_equal_unaligned(const u8 *addr1, const u8 *addr2)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	return ether_addr_equal(addr1, addr2);
#else
	return memcmp(addr1, addr2, ETH_ALEN) == 0;
#endif
}

/**
 * ether_addr_equal_masked - Compare two Ethernet addresses with a mask
 * @addr1: Pointer to a six-byte array containing the 1st Ethernet address
 * @addr2: Pointer to a six-byte array containing the 2nd Ethernet address
 * @mask: Pointer to a six-byte array containing the Ethernet address bitmask
 *
 * Compare two Ethernet addresses with a mask, returns true if for every bit
 * set in the bitmask the equivalent bits in the ethernet addresses are equal.
 * Using a mask with all bits set is a slower ether_addr_equal.
 */
static inline bool ether_addr_equal_masked(const u8 *addr1, const u8 *addr2,
					   const u8 *mask)
{
	int i;

	for (i = 0; i < ETH_ALEN; i++) {
		if ((addr1[i] ^ addr2[i]) & mask[i])
			return false;
	}

	return true;
}

static inline bool ether_addr_is_ipv4_mcast(const u8 *addr)
{
	u8 mask[ETH_ALEN] = { 0xff, 0xff, 0xff, 0x80, 0x00, 0x00 };

	return ether_addr_equal_masked(addr, eth_ipv4_mcast_addr_base, mask);
}

static inline bool ether_addr_is_ipv6_mcast(const u8 *addr)
{
	u8 mask[ETH_ALEN] = { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 };

	return ether_addr_equal_masked(addr, eth_ipv6_mcast_addr_base, mask);
}

static inline bool ether_addr_is_ip_mcast(const u8 *addr)
{
	return ether_addr_is_ipv4_mcast(addr) ||
		ether_addr_is_ipv6_mcast(addr);
}

/**
 * ether_addr_to_u64 - Convert an Ethernet address into a u64 value.
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Return a u64 value of the address
 */
static inline u64 ether_addr_to_u64(const u8 *addr)
{
	u64 u = 0;
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		u = u << 8 | addr[i];

	return u;
}

/**
 * u64_to_ether_addr - Convert a u64 to an Ethernet address.
 * @u: u64 to convert to an Ethernet MAC address
 * @addr: Pointer to a six-byte array to contain the Ethernet address
 */
static inline void u64_to_ether_addr(u64 u, u8 *addr)
{
	int i;

	for (i = ETH_ALEN - 1; i >= 0; i--) {
		addr[i] = u & 0xff;
		u = u >> 8;
	}
}

/**
 * eth_addr_dec - Decrement the given MAC address
 *
 * @addr: Pointer to a six-byte array containing Ethernet address to decrement
 */
static inline void eth_addr_dec(u8 *addr)
{
	u64 u = ether_addr_to_u64(addr);

	u--;
	u64_to_ether_addr(u, addr);
}

/**
 * eth_addr_inc() - Increment the given MAC address.
 * @addr: Pointer to a six-byte array containing Ethernet address to increment.
 */
static inline void eth_addr_inc(u8 *addr)
{
	u64 u = ether_addr_to_u64(addr);

	u++;
	u64_to_ether_addr(u, addr);
}

/**
 * eth_addr_add() - Add (or subtract) an offset to/from the given MAC address.
 *
 * @offset: Offset to add.
 * @addr: Pointer to a six-byte array containing Ethernet address to increment.
 */
static inline void eth_addr_add(u8 *addr, long offset)
{
	u64 u = ether_addr_to_u64(addr);

	u += offset;
	u64_to_ether_addr(u, addr);
}

/**
 * is_etherdev_addr - Tell if given Ethernet address belongs to the device.
 * @dev: Pointer to a device structure
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Compare passed address with all addresses of the device. Return true if the
 * address if one of the device addresses.
 *
 * Note that this function calls ether_addr_equal_64bits() so take care of
 * the right padding.
 */
static inline bool is_etherdev_addr(const struct net_device *dev,
				    const u8 addr[6 + 2])
{
	struct netdev_hw_addr *ha;
	bool res = false;

	rcu_read_lock();
	for_each_dev_addr(dev, ha) {
		res = ether_addr_equal_64bits(addr, ha->addr);
		if (res)
			break;
	}
	rcu_read_unlock();
	return res;
}
#endif	/* __KERNEL__ */

/**
 * compare_ether_header - Compare two Ethernet headers
 * @a: Pointer to Ethernet header
 * @b: Pointer to Ethernet header
 *
 * Compare two Ethernet headers, returns 0 if equal.
 * This assumes that the network header (i.e., IP header) is 4-byte
 * aligned OR the platform can handle unaligned access.  This is the
 * case for all packets coming into netif_receive_skb or similar
 * entry points.
 */

static inline unsigned long compare_ether_header(const void *a, const void *b)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
	unsigned long fold;

	/*
	 * We want to compare 14 bytes:
	 *  [a0 ... a13] ^ [b0 ... b13]
	 * Use two long XOR, ORed together, with an overlap of two bytes.
	 *  [a0  a1  a2  a3  a4  a5  a6  a7 ] ^ [b0  b1  b2  b3  b4  b5  b6  b7 ] |
	 *  [a6  a7  a8  a9  a10 a11 a12 a13] ^ [b6  b7  b8  b9  b10 b11 b12 b13]
	 * This means the [a6 a7] ^ [b6 b7] part is done two times.
	*/
	fold = *(unsigned long *)a ^ *(unsigned long *)b;
	fold |= *(unsigned long *)(a + 6) ^ *(unsigned long *)(b + 6);
	return fold;
#else
	u32 *a32 = (u32 *)((u8 *)a + 2);
	u32 *b32 = (u32 *)((u8 *)b + 2);

	return (*(u16 *)a ^ *(u16 *)b) | (a32[0] ^ b32[0]) |
	       (a32[1] ^ b32[1]) | (a32[2] ^ b32[2]);
#endif
}

/**
 * eth_hw_addr_gen - Generate and assign Ethernet address to a port
 * @dev: pointer to port's net_device structure
 * @base_addr: base Ethernet address
 * @id: offset to add to the base address
 *
 * Generate a MAC address using a base address and an offset and assign it
 * to a net_device. Commonly used by switch drivers which need to compute
 * addresses for all their ports. addr_assign_type is not changed.
 */
static inline void eth_hw_addr_gen(struct net_device *dev, const u8 *base_addr,
				   unsigned int id)
{
	u64 u = ether_addr_to_u64(base_addr);
	u8 addr[ETH_ALEN];

	u += id;
	u64_to_ether_addr(u, addr);
	eth_hw_addr_set(dev, addr);
}

/**
 * eth_skb_pkt_type - Assign packet type if destination address does not match
 * @skb: Assigned a packet type if address does not match @dev address
 * @dev: Network device used to compare packet address against
 *
 * If the destination MAC address of the packet does not match the network
 * device address, assign an appropriate packet type.
 */
static inline void eth_skb_pkt_type(struct sk_buff *skb,
				    const struct net_device *dev)
{
	const struct ethhdr *eth = eth_hdr(skb);

	if (unlikely(!ether_addr_equal_64bits(eth->h_dest, dev->dev_addr))) {
		if (unlikely(is_multicast_ether_addr_64bits(eth->h_dest))) {
			if (ether_addr_equal_64bits(eth->h_dest, dev->broadcast))
				skb->pkt_type = PACKET_BROADCAST;
			else
				skb->pkt_type = PACKET_MULTICAST;
		} else {
			skb->pkt_type = PACKET_OTHERHOST;
		}
	}
}

static inline struct ethhdr *eth_skb_pull_mac(struct sk_buff *skb)
{
	struct ethhdr *eth = (struct ethhdr *)skb->data;

	skb_pull_inline(skb, ETH_HLEN);
	return eth;
}

/**
 * eth_skb_pad - Pad buffer to minimum number of octets for Ethernet frame
 * @skb: Buffer to pad
 *
 * An Ethernet frame should have a minimum size of 60 bytes.  This function
 * takes short frames and pads them with zeros up to the 60 byte limit.
 */
static inline int eth_skb_pad(struct sk_buff *skb)
{
	return skb_put_padto(skb, ETH_ZLEN);
}

#endif	/* _LINUX_ETHERDEVICE_H */
