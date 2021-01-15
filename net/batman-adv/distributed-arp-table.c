// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2011-2020  B.A.T.M.A.N. contributors:
 *
 * Antonio Quartulli
 */

#include "distributed-arp-table.h"
#include "main.h"

#include <asm/unaligned.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/byteorder/generic.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/gfp.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/netlink.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/udp.h>
#include <linux/workqueue.h>
#include <net/arp.h>
#include <net/genetlink.h>
#include <net/netlink.h>
#include <net/sock.h>
#include <uapi/linux/batman_adv.h>

#include "bridge_loop_avoidance.h"
#include "hard-interface.h"
#include "hash.h"
#include "log.h"
#include "netlink.h"
#include "originator.h"
#include "send.h"
#include "soft-interface.h"
#include "translation-table.h"
#include "tvlv.h"

enum batadv_bootpop {
	BATADV_BOOTREPLY	= 2,
};

enum batadv_boothtype {
	BATADV_HTYPE_ETHERNET	= 1,
};

enum batadv_dhcpoptioncode {
	BATADV_DHCP_OPT_PAD		= 0,
	BATADV_DHCP_OPT_MSG_TYPE	= 53,
	BATADV_DHCP_OPT_END		= 255,
};

enum batadv_dhcptype {
	BATADV_DHCPACK		= 5,
};

/* { 99, 130, 83, 99 } */
#define BATADV_DHCP_MAGIC 1669485411

struct batadv_dhcp_packet {
	__u8 op;
	__u8 htype;
	__u8 hlen;
	__u8 hops;
	__be32 xid;
	__be16 secs;
	__be16 flags;
	__be32 ciaddr;
	__be32 yiaddr;
	__be32 siaddr;
	__be32 giaddr;
	__u8 chaddr[16];
	__u8 sname[64];
	__u8 file[128];
	__be32 magic;
	__u8 options[];
};

#define BATADV_DHCP_YIADDR_LEN sizeof(((struct batadv_dhcp_packet *)0)->yiaddr)
#define BATADV_DHCP_CHADDR_LEN sizeof(((struct batadv_dhcp_packet *)0)->chaddr)

static void batadv_dat_purge(struct work_struct *work);

/**
 * batadv_dat_start_timer() - initialise the DAT periodic worker
 * @bat_priv: the bat priv with all the soft interface information
 */
static void batadv_dat_start_timer(struct batadv_priv *bat_priv)
{
	INIT_DELAYED_WORK(&bat_priv->dat.work, batadv_dat_purge);
	queue_delayed_work(batadv_event_workqueue, &bat_priv->dat.work,
			   msecs_to_jiffies(10000));
}

/**
 * batadv_dat_entry_release() - release dat_entry from lists and queue for free
 *  after rcu grace period
 * @ref: kref pointer of the dat_entry
 */
static void batadv_dat_entry_release(struct kref *ref)
{
	struct batadv_dat_entry *dat_entry;

	dat_entry = container_of(ref, struct batadv_dat_entry, refcount);

	kfree_rcu(dat_entry, rcu);
}

/**
 * batadv_dat_entry_put() - decrement the dat_entry refcounter and possibly
 *  release it
 * @dat_entry: dat_entry to be free'd
 */
static void batadv_dat_entry_put(struct batadv_dat_entry *dat_entry)
{
	kref_put(&dat_entry->refcount, batadv_dat_entry_release);
}

/**
 * batadv_dat_to_purge() - check whether a dat_entry has to be purged or not
 * @dat_entry: the entry to check
 *
 * Return: true if the entry has to be purged now, false otherwise.
 */
static bool batadv_dat_to_purge(struct batadv_dat_entry *dat_entry)
{
	return batadv_has_timed_out(dat_entry->last_update,
				    BATADV_DAT_ENTRY_TIMEOUT);
}

/**
 * __batadv_dat_purge() - delete entries from the DAT local storage
 * @bat_priv: the bat priv with all the soft interface information
 * @to_purge: function in charge to decide whether an entry has to be purged or
 *	      not. This function takes the dat_entry as argument and has to
 *	      returns a boolean value: true is the entry has to be deleted,
 *	      false otherwise
 *
 * Loops over each entry in the DAT local storage and deletes it if and only if
 * the to_purge function passed as argument returns true.
 */
static void __batadv_dat_purge(struct batadv_priv *bat_priv,
			       bool (*to_purge)(struct batadv_dat_entry *))
{
	spinlock_t *list_lock; /* protects write access to the hash lists */
	struct batadv_dat_entry *dat_entry;
	struct hlist_node *node_tmp;
	struct hlist_head *head;
	u32 i;

	if (!bat_priv->dat.hash)
		return;

	for (i = 0; i < bat_priv->dat.hash->size; i++) {
		head = &bat_priv->dat.hash->table[i];
		list_lock = &bat_priv->dat.hash->list_locks[i];

		spin_lock_bh(list_lock);
		hlist_for_each_entry_safe(dat_entry, node_tmp, head,
					  hash_entry) {
			/* if a helper function has been passed as parameter,
			 * ask it if the entry has to be purged or not
			 */
			if (to_purge && !to_purge(dat_entry))
				continue;

			hlist_del_rcu(&dat_entry->hash_entry);
			batadv_dat_entry_put(dat_entry);
		}
		spin_unlock_bh(list_lock);
	}
}

/**
 * batadv_dat_purge() - periodic task that deletes old entries from the local
 *  DAT hash table
 * @work: kernel work struct
 */
static void batadv_dat_purge(struct work_struct *work)
{
	struct delayed_work *delayed_work;
	struct batadv_priv_dat *priv_dat;
	struct batadv_priv *bat_priv;

	delayed_work = to_delayed_work(work);
	priv_dat = container_of(delayed_work, struct batadv_priv_dat, work);
	bat_priv = container_of(priv_dat, struct batadv_priv, dat);

	__batadv_dat_purge(bat_priv, batadv_dat_to_purge);
	batadv_dat_start_timer(bat_priv);
}

/**
 * batadv_compare_dat() - comparing function used in the local DAT hash table
 * @node: node in the local table
 * @data2: second object to compare the node to
 *
 * Return: true if the two entries are the same, false otherwise.
 */
static bool batadv_compare_dat(const struct hlist_node *node, const void *data2)
{
	const void *data1 = container_of(node, struct batadv_dat_entry,
					 hash_entry);

	return memcmp(data1, data2, sizeof(__be32)) == 0;
}

/**
 * batadv_arp_hw_src() - extract the hw_src field from an ARP packet
 * @skb: ARP packet
 * @hdr_size: size of the possible header before the ARP packet
 *
 * Return: the value of the hw_src field in the ARP packet.
 */
static u8 *batadv_arp_hw_src(struct sk_buff *skb, int hdr_size)
{
	u8 *addr;

	addr = (u8 *)(skb->data + hdr_size);
	addr += ETH_HLEN + sizeof(struct arphdr);

	return addr;
}

/**
 * batadv_arp_ip_src() - extract the ip_src field from an ARP packet
 * @skb: ARP packet
 * @hdr_size: size of the possible header before the ARP packet
 *
 * Return: the value of the ip_src field in the ARP packet.
 */
static __be32 batadv_arp_ip_src(struct sk_buff *skb, int hdr_size)
{
	return *(__force __be32 *)(batadv_arp_hw_src(skb, hdr_size) + ETH_ALEN);
}

/**
 * batadv_arp_hw_dst() - extract the hw_dst field from an ARP packet
 * @skb: ARP packet
 * @hdr_size: size of the possible header before the ARP packet
 *
 * Return: the value of the hw_dst field in the ARP packet.
 */
static u8 *batadv_arp_hw_dst(struct sk_buff *skb, int hdr_size)
{
	return batadv_arp_hw_src(skb, hdr_size) + ETH_ALEN + 4;
}

/**
 * batadv_arp_ip_dst() - extract the ip_dst field from an ARP packet
 * @skb: ARP packet
 * @hdr_size: size of the possible header before the ARP packet
 *
 * Return: the value of the ip_dst field in the ARP packet.
 */
static __be32 batadv_arp_ip_dst(struct sk_buff *skb, int hdr_size)
{
	u8 *dst = batadv_arp_hw_src(skb, hdr_size) + ETH_ALEN * 2 + 4;

	return *(__force __be32 *)dst;
}

/**
 * batadv_hash_dat() - compute the hash value for an IP address
 * @data: data to hash
 * @size: size of the hash table
 *
 * Return: the selected index in the hash table for the given data.
 */
static u32 batadv_hash_dat(const void *data, u32 size)
{
	u32 hash = 0;
	const struct batadv_dat_entry *dat = data;
	const unsigned char *key;
	__be16 vid;
	u32 i;

	key = (__force const unsigned char *)&dat->ip;
	for (i = 0; i < sizeof(dat->ip); i++) {
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	vid = htons(dat->vid);
	key = (__force const unsigned char *)&vid;
	for (i = 0; i < sizeof(dat->vid); i++) {
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return hash % size;
}

/**
 * batadv_dat_entry_hash_find() - look for a given dat_entry in the local hash
 * table
 * @bat_priv: the bat priv with all the soft interface information
 * @ip: search key
 * @vid: VLAN identifier
 *
 * Return: the dat_entry if found, NULL otherwise.
 */
static struct batadv_dat_entry *
batadv_dat_entry_hash_find(struct batadv_priv *bat_priv, __be32 ip,
			   unsigned short vid)
{
	struct hlist_head *head;
	struct batadv_dat_entry to_find, *dat_entry, *dat_entry_tmp = NULL;
	struct batadv_hashtable *hash = bat_priv->dat.hash;
	u32 index;

	if (!hash)
		return NULL;

	to_find.ip = ip;
	to_find.vid = vid;

	index = batadv_hash_dat(&to_find, hash->size);
	head = &hash->table[index];

	rcu_read_lock();
	hlist_for_each_entry_rcu(dat_entry, head, hash_entry) {
		if (dat_entry->ip != ip)
			continue;

		if (!kref_get_unless_zero(&dat_entry->refcount))
			continue;

		dat_entry_tmp = dat_entry;
		break;
	}
	rcu_read_unlock();

	return dat_entry_tmp;
}

/**
 * batadv_dat_entry_add() - add a new dat entry or update it if already exists
 * @bat_priv: the bat priv with all the soft interface information
 * @ip: ipv4 to add/edit
 * @mac_addr: mac address to assign to the given ipv4
 * @vid: VLAN identifier
 */
static void batadv_dat_entry_add(struct batadv_priv *bat_priv, __be32 ip,
				 u8 *mac_addr, unsigned short vid)
{
	struct batadv_dat_entry *dat_entry;
	int hash_added;

	dat_entry = batadv_dat_entry_hash_find(bat_priv, ip, vid);
	/* if this entry is already known, just update it */
	if (dat_entry) {
		if (!batadv_compare_eth(dat_entry->mac_addr, mac_addr))
			ether_addr_copy(dat_entry->mac_addr, mac_addr);
		dat_entry->last_update = jiffies;
		batadv_dbg(BATADV_DBG_DAT, bat_priv,
			   "Entry updated: %pI4 %pM (vid: %d)\n",
			   &dat_entry->ip, dat_entry->mac_addr,
			   batadv_print_vid(vid));
		goto out;
	}

	dat_entry = kmalloc(sizeof(*dat_entry), GFP_ATOMIC);
	if (!dat_entry)
		goto out;

	dat_entry->ip = ip;
	dat_entry->vid = vid;
	ether_addr_copy(dat_entry->mac_addr, mac_addr);
	dat_entry->last_update = jiffies;
	kref_init(&dat_entry->refcount);

	kref_get(&dat_entry->refcount);
	hash_added = batadv_hash_add(bat_priv->dat.hash, batadv_compare_dat,
				     batadv_hash_dat, dat_entry,
				     &dat_entry->hash_entry);

	if (unlikely(hash_added != 0)) {
		/* remove the reference for the hash */
		batadv_dat_entry_put(dat_entry);
		goto out;
	}

	batadv_dbg(BATADV_DBG_DAT, bat_priv, "New entry added: %pI4 %pM (vid: %d)\n",
		   &dat_entry->ip, dat_entry->mac_addr, batadv_print_vid(vid));

out:
	if (dat_entry)
		batadv_dat_entry_put(dat_entry);
}

#ifdef CONFIG_BATMAN_ADV_DEBUG

/**
 * batadv_dbg_arp() - print a debug message containing all the ARP packet
 *  details
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: ARP packet
 * @hdr_size: size of the possible header before the ARP packet
 * @msg: message to print together with the debugging information
 */
static void batadv_dbg_arp(struct batadv_priv *bat_priv, struct sk_buff *skb,
			   int hdr_size, char *msg)
{
	struct batadv_unicast_4addr_packet *unicast_4addr_packet;
	struct batadv_bcast_packet *bcast_pkt;
	u8 *orig_addr;
	__be32 ip_src, ip_dst;

	if (msg)
		batadv_dbg(BATADV_DBG_DAT, bat_priv, "%s\n", msg);

	ip_src = batadv_arp_ip_src(skb, hdr_size);
	ip_dst = batadv_arp_ip_dst(skb, hdr_size);
	batadv_dbg(BATADV_DBG_DAT, bat_priv,
		   "ARP MSG = [src: %pM-%pI4 dst: %pM-%pI4]\n",
		   batadv_arp_hw_src(skb, hdr_size), &ip_src,
		   batadv_arp_hw_dst(skb, hdr_size), &ip_dst);

	if (hdr_size < sizeof(struct batadv_unicast_packet))
		return;

	unicast_4addr_packet = (struct batadv_unicast_4addr_packet *)skb->data;

	switch (unicast_4addr_packet->u.packet_type) {
	case BATADV_UNICAST:
		batadv_dbg(BATADV_DBG_DAT, bat_priv,
			   "* encapsulated within a UNICAST packet\n");
		break;
	case BATADV_UNICAST_4ADDR:
		batadv_dbg(BATADV_DBG_DAT, bat_priv,
			   "* encapsulated within a UNICAST_4ADDR packet (src: %pM)\n",
			   unicast_4addr_packet->src);
		switch (unicast_4addr_packet->subtype) {
		case BATADV_P_DAT_DHT_PUT:
			batadv_dbg(BATADV_DBG_DAT, bat_priv, "* type: DAT_DHT_PUT\n");
			break;
		case BATADV_P_DAT_DHT_GET:
			batadv_dbg(BATADV_DBG_DAT, bat_priv, "* type: DAT_DHT_GET\n");
			break;
		case BATADV_P_DAT_CACHE_REPLY:
			batadv_dbg(BATADV_DBG_DAT, bat_priv,
				   "* type: DAT_CACHE_REPLY\n");
			break;
		case BATADV_P_DATA:
			batadv_dbg(BATADV_DBG_DAT, bat_priv, "* type: DATA\n");
			break;
		default:
			batadv_dbg(BATADV_DBG_DAT, bat_priv, "* type: Unknown (%u)!\n",
				   unicast_4addr_packet->u.packet_type);
		}
		break;
	case BATADV_BCAST:
		bcast_pkt = (struct batadv_bcast_packet *)unicast_4addr_packet;
		orig_addr = bcast_pkt->orig;
		batadv_dbg(BATADV_DBG_DAT, bat_priv,
			   "* encapsulated within a BCAST packet (src: %pM)\n",
			   orig_addr);
		break;
	default:
		batadv_dbg(BATADV_DBG_DAT, bat_priv,
			   "* encapsulated within an unknown packet type (0x%x)\n",
			   unicast_4addr_packet->u.packet_type);
	}
}

#else

static void batadv_dbg_arp(struct batadv_priv *bat_priv, struct sk_buff *skb,
			   int hdr_size, char *msg)
{
}

#endif /* CONFIG_BATMAN_ADV_DEBUG */

/**
 * batadv_is_orig_node_eligible() - check whether a node can be a DHT candidate
 * @res: the array with the already selected candidates
 * @select: number of already selected candidates
 * @tmp_max: address of the currently evaluated node
 * @max: current round max address
 * @last_max: address of the last selected candidate
 * @candidate: orig_node under evaluation
 * @max_orig_node: last selected candidate
 *
 * Return: true if the node has been elected as next candidate or false
 * otherwise.
 */
static bool batadv_is_orig_node_eligible(struct batadv_dat_candidate *res,
					 int select, batadv_dat_addr_t tmp_max,
					 batadv_dat_addr_t max,
					 batadv_dat_addr_t last_max,
					 struct batadv_orig_node *candidate,
					 struct batadv_orig_node *max_orig_node)
{
	bool ret = false;
	int j;

	/* check if orig node candidate is running DAT */
	if (!test_bit(BATADV_ORIG_CAPA_HAS_DAT, &candidate->capabilities))
		goto out;

	/* Check if this node has already been selected... */
	for (j = 0; j < select; j++)
		if (res[j].orig_node == candidate)
			break;
	/* ..and possibly skip it */
	if (j < select)
		goto out;
	/* sanity check: has it already been selected? This should not happen */
	if (tmp_max > last_max)
		goto out;
	/* check if during this iteration an originator with a closer dht
	 * address has already been found
	 */
	if (tmp_max < max)
		goto out;
	/* this is an hash collision with the temporary selected node. Choose
	 * the one with the lowest address
	 */
	if (tmp_max == max && max_orig_node &&
	    batadv_compare_eth(candidate->orig, max_orig_node->orig))
		goto out;

	ret = true;
out:
	return ret;
}

/**
 * batadv_choose_next_candidate() - select the next DHT candidate
 * @bat_priv: the bat priv with all the soft interface information
 * @cands: candidates array
 * @select: number of candidates already present in the array
 * @ip_key: key to look up in the DHT
 * @last_max: pointer where the address of the selected candidate will be saved
 */
static void batadv_choose_next_candidate(struct batadv_priv *bat_priv,
					 struct batadv_dat_candidate *cands,
					 int select, batadv_dat_addr_t ip_key,
					 batadv_dat_addr_t *last_max)
{
	batadv_dat_addr_t max = 0;
	batadv_dat_addr_t tmp_max = 0;
	struct batadv_orig_node *orig_node, *max_orig_node = NULL;
	struct batadv_hashtable *hash = bat_priv->orig_hash;
	struct hlist_head *head;
	int i;

	/* if no node is eligible as candidate, leave the candidate type as
	 * NOT_FOUND
	 */
	cands[select].type = BATADV_DAT_CANDIDATE_NOT_FOUND;

	/* iterate over the originator list and find the node with the closest
	 * dat_address which has not been selected yet
	 */
	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		rcu_read_lock();
		hlist_for_each_entry_rcu(orig_node, head, hash_entry) {
			/* the dht space is a ring using unsigned addresses */
			tmp_max = BATADV_DAT_ADDR_MAX - orig_node->dat_addr +
				  ip_key;

			if (!batadv_is_orig_node_eligible(cands, select,
							  tmp_max, max,
							  *last_max, orig_node,
							  max_orig_node))
				continue;

			if (!kref_get_unless_zero(&orig_node->refcount))
				continue;

			max = tmp_max;
			if (max_orig_node)
				batadv_orig_node_put(max_orig_node);
			max_orig_node = orig_node;
		}
		rcu_read_unlock();
	}
	if (max_orig_node) {
		cands[select].type = BATADV_DAT_CANDIDATE_ORIG;
		cands[select].orig_node = max_orig_node;
		batadv_dbg(BATADV_DBG_DAT, bat_priv,
			   "dat_select_candidates() %d: selected %pM addr=%u dist=%u\n",
			   select, max_orig_node->orig, max_orig_node->dat_addr,
			   max);
	}
	*last_max = max;
}

/**
 * batadv_dat_select_candidates() - select the nodes which the DHT message has
 *  to be sent to
 * @bat_priv: the bat priv with all the soft interface information
 * @ip_dst: ipv4 to look up in the DHT
 * @vid: VLAN identifier
 *
 * An originator O is selected if and only if its DHT_ID value is one of three
 * closest values (from the LEFT, with wrap around if needed) then the hash
 * value of the key. ip_dst is the key.
 *
 * Return: the candidate array of size BATADV_DAT_CANDIDATE_NUM.
 */
static struct batadv_dat_candidate *
batadv_dat_select_candidates(struct batadv_priv *bat_priv, __be32 ip_dst,
			     unsigned short vid)
{
	int select;
	batadv_dat_addr_t last_max = BATADV_DAT_ADDR_MAX, ip_key;
	struct batadv_dat_candidate *res;
	struct batadv_dat_entry dat;

	if (!bat_priv->orig_hash)
		return NULL;

	res = kmalloc_array(BATADV_DAT_CANDIDATES_NUM, sizeof(*res),
			    GFP_ATOMIC);
	if (!res)
		return NULL;

	dat.ip = ip_dst;
	dat.vid = vid;
	ip_key = (batadv_dat_addr_t)batadv_hash_dat(&dat,
						    BATADV_DAT_ADDR_MAX);

	batadv_dbg(BATADV_DBG_DAT, bat_priv,
		   "%s(): IP=%pI4 hash(IP)=%u\n", __func__, &ip_dst,
		   ip_key);

	for (select = 0; select < BATADV_DAT_CANDIDATES_NUM; select++)
		batadv_choose_next_candidate(bat_priv, res, select, ip_key,
					     &last_max);

	return res;
}

/**
 * batadv_dat_forward_data() - copy and send payload to the selected candidates
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: payload to send
 * @ip: the DHT key
 * @vid: VLAN identifier
 * @packet_subtype: unicast4addr packet subtype to use
 *
 * This function copies the skb with pskb_copy() and is sent as a unicast packet
 * to each of the selected candidates.
 *
 * Return: true if the packet is sent to at least one candidate, false
 * otherwise.
 */
static bool batadv_dat_forward_data(struct batadv_priv *bat_priv,
				    struct sk_buff *skb, __be32 ip,
				    unsigned short vid, int packet_subtype)
{
	int i;
	bool ret = false;
	int send_status;
	struct batadv_neigh_node *neigh_node = NULL;
	struct sk_buff *tmp_skb;
	struct batadv_dat_candidate *cand;

	cand = batadv_dat_select_candidates(bat_priv, ip, vid);
	if (!cand)
		goto out;

	batadv_dbg(BATADV_DBG_DAT, bat_priv, "DHT_SEND for %pI4\n", &ip);

	for (i = 0; i < BATADV_DAT_CANDIDATES_NUM; i++) {
		if (cand[i].type == BATADV_DAT_CANDIDATE_NOT_FOUND)
			continue;

		neigh_node = batadv_orig_router_get(cand[i].orig_node,
						    BATADV_IF_DEFAULT);
		if (!neigh_node)
			goto free_orig;

		tmp_skb = pskb_copy_for_clone(skb, GFP_ATOMIC);
		if (!batadv_send_skb_prepare_unicast_4addr(bat_priv, tmp_skb,
							   cand[i].orig_node,
							   packet_subtype)) {
			kfree_skb(tmp_skb);
			goto free_neigh;
		}

		send_status = batadv_send_unicast_skb(tmp_skb, neigh_node);
		if (send_status == NET_XMIT_SUCCESS) {
			/* count the sent packet */
			switch (packet_subtype) {
			case BATADV_P_DAT_DHT_GET:
				batadv_inc_counter(bat_priv,
						   BATADV_CNT_DAT_GET_TX);
				break;
			case BATADV_P_DAT_DHT_PUT:
				batadv_inc_counter(bat_priv,
						   BATADV_CNT_DAT_PUT_TX);
				break;
			}

			/* packet sent to a candidate: return true */
			ret = true;
		}
free_neigh:
		batadv_neigh_node_put(neigh_node);
free_orig:
		batadv_orig_node_put(cand[i].orig_node);
	}

out:
	kfree(cand);
	return ret;
}

/**
 * batadv_dat_tvlv_container_update() - update the dat tvlv container after dat
 *  setting change
 * @bat_priv: the bat priv with all the soft interface information
 */
static void batadv_dat_tvlv_container_update(struct batadv_priv *bat_priv)
{
	char dat_mode;

	dat_mode = atomic_read(&bat_priv->distributed_arp_table);

	switch (dat_mode) {
	case 0:
		batadv_tvlv_container_unregister(bat_priv, BATADV_TVLV_DAT, 1);
		break;
	case 1:
		batadv_tvlv_container_register(bat_priv, BATADV_TVLV_DAT, 1,
					       NULL, 0);
		break;
	}
}

/**
 * batadv_dat_status_update() - update the dat tvlv container after dat
 *  setting change
 * @net_dev: the soft interface net device
 */
void batadv_dat_status_update(struct net_device *net_dev)
{
	struct batadv_priv *bat_priv = netdev_priv(net_dev);

	batadv_dat_tvlv_container_update(bat_priv);
}

/**
 * batadv_dat_tvlv_ogm_handler_v1() - process incoming dat tvlv container
 * @bat_priv: the bat priv with all the soft interface information
 * @orig: the orig_node of the ogm
 * @flags: flags indicating the tvlv state (see batadv_tvlv_handler_flags)
 * @tvlv_value: tvlv buffer containing the gateway data
 * @tvlv_value_len: tvlv buffer length
 */
static void batadv_dat_tvlv_ogm_handler_v1(struct batadv_priv *bat_priv,
					   struct batadv_orig_node *orig,
					   u8 flags,
					   void *tvlv_value, u16 tvlv_value_len)
{
	if (flags & BATADV_TVLV_HANDLER_OGM_CIFNOTFND)
		clear_bit(BATADV_ORIG_CAPA_HAS_DAT, &orig->capabilities);
	else
		set_bit(BATADV_ORIG_CAPA_HAS_DAT, &orig->capabilities);
}

/**
 * batadv_dat_hash_free() - free the local DAT hash table
 * @bat_priv: the bat priv with all the soft interface information
 */
static void batadv_dat_hash_free(struct batadv_priv *bat_priv)
{
	if (!bat_priv->dat.hash)
		return;

	__batadv_dat_purge(bat_priv, NULL);

	batadv_hash_destroy(bat_priv->dat.hash);

	bat_priv->dat.hash = NULL;
}

/**
 * batadv_dat_init() - initialise the DAT internals
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Return: 0 in case of success, a negative error code otherwise
 */
int batadv_dat_init(struct batadv_priv *bat_priv)
{
	if (bat_priv->dat.hash)
		return 0;

	bat_priv->dat.hash = batadv_hash_new(1024);

	if (!bat_priv->dat.hash)
		return -ENOMEM;

	batadv_dat_start_timer(bat_priv);

	batadv_tvlv_handler_register(bat_priv, batadv_dat_tvlv_ogm_handler_v1,
				     NULL, BATADV_TVLV_DAT, 1,
				     BATADV_TVLV_HANDLER_OGM_CIFNOTFND);
	batadv_dat_tvlv_container_update(bat_priv);
	return 0;
}

/**
 * batadv_dat_free() - free the DAT internals
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_dat_free(struct batadv_priv *bat_priv)
{
	batadv_tvlv_container_unregister(bat_priv, BATADV_TVLV_DAT, 1);
	batadv_tvlv_handler_unregister(bat_priv, BATADV_TVLV_DAT, 1);

	cancel_delayed_work_sync(&bat_priv->dat.work);

	batadv_dat_hash_free(bat_priv);
}

/**
 * batadv_dat_cache_dump_entry() - dump one entry of the DAT cache table to a
 *  netlink socket
 * @msg: buffer for the message
 * @portid: netlink port
 * @cb: Control block containing additional options
 * @dat_entry: entry to dump
 *
 * Return: 0 or error code.
 */
static int
batadv_dat_cache_dump_entry(struct sk_buff *msg, u32 portid,
			    struct netlink_callback *cb,
			    struct batadv_dat_entry *dat_entry)
{
	int msecs;
	void *hdr;

	hdr = genlmsg_put(msg, portid, cb->nlh->nlmsg_seq,
			  &batadv_netlink_family, NLM_F_MULTI,
			  BATADV_CMD_GET_DAT_CACHE);
	if (!hdr)
		return -ENOBUFS;

	genl_dump_check_consistent(cb, hdr);

	msecs = jiffies_to_msecs(jiffies - dat_entry->last_update);

	if (nla_put_in_addr(msg, BATADV_ATTR_DAT_CACHE_IP4ADDRESS,
			    dat_entry->ip) ||
	    nla_put(msg, BATADV_ATTR_DAT_CACHE_HWADDRESS, ETH_ALEN,
		    dat_entry->mac_addr) ||
	    nla_put_u16(msg, BATADV_ATTR_DAT_CACHE_VID, dat_entry->vid) ||
	    nla_put_u32(msg, BATADV_ATTR_LAST_SEEN_MSECS, msecs)) {
		genlmsg_cancel(msg, hdr);
		return -EMSGSIZE;
	}

	genlmsg_end(msg, hdr);
	return 0;
}

/**
 * batadv_dat_cache_dump_bucket() - dump one bucket of the DAT cache table to
 *  a netlink socket
 * @msg: buffer for the message
 * @portid: netlink port
 * @cb: Control block containing additional options
 * @hash: hash to dump
 * @bucket: bucket index to dump
 * @idx_skip: How many entries to skip
 *
 * Return: 0 or error code.
 */
static int
batadv_dat_cache_dump_bucket(struct sk_buff *msg, u32 portid,
			     struct netlink_callback *cb,
			     struct batadv_hashtable *hash, unsigned int bucket,
			     int *idx_skip)
{
	struct batadv_dat_entry *dat_entry;
	int idx = 0;

	spin_lock_bh(&hash->list_locks[bucket]);
	cb->seq = atomic_read(&hash->generation) << 1 | 1;

	hlist_for_each_entry(dat_entry, &hash->table[bucket], hash_entry) {
		if (idx < *idx_skip)
			goto skip;

		if (batadv_dat_cache_dump_entry(msg, portid, cb, dat_entry)) {
			spin_unlock_bh(&hash->list_locks[bucket]);
			*idx_skip = idx;

			return -EMSGSIZE;
		}

skip:
		idx++;
	}
	spin_unlock_bh(&hash->list_locks[bucket]);

	return 0;
}

/**
 * batadv_dat_cache_dump() - dump DAT cache table to a netlink socket
 * @msg: buffer for the message
 * @cb: callback structure containing arguments
 *
 * Return: message length.
 */
int batadv_dat_cache_dump(struct sk_buff *msg, struct netlink_callback *cb)
{
	struct batadv_hard_iface *primary_if = NULL;
	int portid = NETLINK_CB(cb->skb).portid;
	struct net *net = sock_net(cb->skb->sk);
	struct net_device *soft_iface;
	struct batadv_hashtable *hash;
	struct batadv_priv *bat_priv;
	int bucket = cb->args[0];
	int idx = cb->args[1];
	int ifindex;
	int ret = 0;

	ifindex = batadv_netlink_get_ifindex(cb->nlh,
					     BATADV_ATTR_MESH_IFINDEX);
	if (!ifindex)
		return -EINVAL;

	soft_iface = dev_get_by_index(net, ifindex);
	if (!soft_iface || !batadv_softif_is_valid(soft_iface)) {
		ret = -ENODEV;
		goto out;
	}

	bat_priv = netdev_priv(soft_iface);
	hash = bat_priv->dat.hash;

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if || primary_if->if_status != BATADV_IF_ACTIVE) {
		ret = -ENOENT;
		goto out;
	}

	while (bucket < hash->size) {
		if (batadv_dat_cache_dump_bucket(msg, portid, cb, hash, bucket,
						 &idx))
			break;

		bucket++;
		idx = 0;
	}

	cb->args[0] = bucket;
	cb->args[1] = idx;

	ret = msg->len;

out:
	if (primary_if)
		batadv_hardif_put(primary_if);

	if (soft_iface)
		dev_put(soft_iface);

	return ret;
}

/**
 * batadv_arp_get_type() - parse an ARP packet and gets the type
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: packet to analyse
 * @hdr_size: size of the possible header before the ARP packet in the skb
 *
 * Return: the ARP type if the skb contains a valid ARP packet, 0 otherwise.
 */
static u16 batadv_arp_get_type(struct batadv_priv *bat_priv,
			       struct sk_buff *skb, int hdr_size)
{
	struct arphdr *arphdr;
	struct ethhdr *ethhdr;
	__be32 ip_src, ip_dst;
	u8 *hw_src, *hw_dst;
	u16 type = 0;

	/* pull the ethernet header */
	if (unlikely(!pskb_may_pull(skb, hdr_size + ETH_HLEN)))
		goto out;

	ethhdr = (struct ethhdr *)(skb->data + hdr_size);

	if (ethhdr->h_proto != htons(ETH_P_ARP))
		goto out;

	/* pull the ARP payload */
	if (unlikely(!pskb_may_pull(skb, hdr_size + ETH_HLEN +
				    arp_hdr_len(skb->dev))))
		goto out;

	arphdr = (struct arphdr *)(skb->data + hdr_size + ETH_HLEN);

	/* check whether the ARP packet carries a valid IP information */
	if (arphdr->ar_hrd != htons(ARPHRD_ETHER))
		goto out;

	if (arphdr->ar_pro != htons(ETH_P_IP))
		goto out;

	if (arphdr->ar_hln != ETH_ALEN)
		goto out;

	if (arphdr->ar_pln != 4)
		goto out;

	/* Check for bad reply/request. If the ARP message is not sane, DAT
	 * will simply ignore it
	 */
	ip_src = batadv_arp_ip_src(skb, hdr_size);
	ip_dst = batadv_arp_ip_dst(skb, hdr_size);
	if (ipv4_is_loopback(ip_src) || ipv4_is_multicast(ip_src) ||
	    ipv4_is_loopback(ip_dst) || ipv4_is_multicast(ip_dst) ||
	    ipv4_is_zeronet(ip_src) || ipv4_is_lbcast(ip_src) ||
	    ipv4_is_zeronet(ip_dst) || ipv4_is_lbcast(ip_dst))
		goto out;

	hw_src = batadv_arp_hw_src(skb, hdr_size);
	if (is_zero_ether_addr(hw_src) || is_multicast_ether_addr(hw_src))
		goto out;

	/* don't care about the destination MAC address in ARP requests */
	if (arphdr->ar_op != htons(ARPOP_REQUEST)) {
		hw_dst = batadv_arp_hw_dst(skb, hdr_size);
		if (is_zero_ether_addr(hw_dst) ||
		    is_multicast_ether_addr(hw_dst))
			goto out;
	}

	type = ntohs(arphdr->ar_op);
out:
	return type;
}

/**
 * batadv_dat_get_vid() - extract the VLAN identifier from skb if any
 * @skb: the buffer containing the packet to extract the VID from
 * @hdr_size: the size of the batman-adv header encapsulating the packet
 *
 * Return: If the packet embedded in the skb is vlan tagged this function
 * returns the VID with the BATADV_VLAN_HAS_TAG flag. Otherwise BATADV_NO_FLAGS
 * is returned.
 */
static unsigned short batadv_dat_get_vid(struct sk_buff *skb, int *hdr_size)
{
	unsigned short vid;

	vid = batadv_get_vid(skb, *hdr_size);

	/* ARP parsing functions jump forward of hdr_size + ETH_HLEN.
	 * If the header contained in the packet is a VLAN one (which is longer)
	 * hdr_size is updated so that the functions will still skip the
	 * correct amount of bytes.
	 */
	if (vid & BATADV_VLAN_HAS_TAG)
		*hdr_size += VLAN_HLEN;

	return vid;
}

/**
 * batadv_dat_arp_create_reply() - create an ARP Reply
 * @bat_priv: the bat priv with all the soft interface information
 * @ip_src: ARP sender IP
 * @ip_dst: ARP target IP
 * @hw_src: Ethernet source and ARP sender MAC
 * @hw_dst: Ethernet destination and ARP target MAC
 * @vid: VLAN identifier (optional, set to zero otherwise)
 *
 * Creates an ARP Reply from the given values, optionally encapsulated in a
 * VLAN header.
 *
 * Return: An skb containing an ARP Reply.
 */
static struct sk_buff *
batadv_dat_arp_create_reply(struct batadv_priv *bat_priv, __be32 ip_src,
			    __be32 ip_dst, u8 *hw_src, u8 *hw_dst,
			    unsigned short vid)
{
	struct sk_buff *skb;

	skb = arp_create(ARPOP_REPLY, ETH_P_ARP, ip_dst, bat_priv->soft_iface,
			 ip_src, hw_dst, hw_src, hw_dst);
	if (!skb)
		return NULL;

	skb_reset_mac_header(skb);

	if (vid & BATADV_VLAN_HAS_TAG)
		skb = vlan_insert_tag(skb, htons(ETH_P_8021Q),
				      vid & VLAN_VID_MASK);

	return skb;
}

/**
 * batadv_dat_snoop_outgoing_arp_request() - snoop the ARP request and try to
 * answer using DAT
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: packet to check
 *
 * Return: true if the message has been sent to the dht candidates, false
 * otherwise. In case of a positive return value the message has to be enqueued
 * to permit the fallback.
 */
bool batadv_dat_snoop_outgoing_arp_request(struct batadv_priv *bat_priv,
					   struct sk_buff *skb)
{
	u16 type = 0;
	__be32 ip_dst, ip_src;
	u8 *hw_src;
	bool ret = false;
	struct batadv_dat_entry *dat_entry = NULL;
	struct sk_buff *skb_new;
	struct net_device *soft_iface = bat_priv->soft_iface;
	int hdr_size = 0;
	unsigned short vid;

	if (!atomic_read(&bat_priv->distributed_arp_table))
		goto out;

	vid = batadv_dat_get_vid(skb, &hdr_size);

	type = batadv_arp_get_type(bat_priv, skb, hdr_size);
	/* If the node gets an ARP_REQUEST it has to send a DHT_GET unicast
	 * message to the selected DHT candidates
	 */
	if (type != ARPOP_REQUEST)
		goto out;

	batadv_dbg_arp(bat_priv, skb, hdr_size, "Parsing outgoing ARP REQUEST");

	ip_src = batadv_arp_ip_src(skb, hdr_size);
	hw_src = batadv_arp_hw_src(skb, hdr_size);
	ip_dst = batadv_arp_ip_dst(skb, hdr_size);

	batadv_dat_entry_add(bat_priv, ip_src, hw_src, vid);

	dat_entry = batadv_dat_entry_hash_find(bat_priv, ip_dst, vid);
	if (dat_entry) {
		/* If the ARP request is destined for a local client the local
		 * client will answer itself. DAT would only generate a
		 * duplicate packet.
		 *
		 * Moreover, if the soft-interface is enslaved into a bridge, an
		 * additional DAT answer may trigger kernel warnings about
		 * a packet coming from the wrong port.
		 */
		if (batadv_is_my_client(bat_priv, dat_entry->mac_addr, vid)) {
			ret = true;
			goto out;
		}

		/* If BLA is enabled, only send ARP replies if we have claimed
		 * the destination for the ARP request or if no one else of
		 * the backbone gws belonging to our backbone has claimed the
		 * destination.
		 */
		if (!batadv_bla_check_claim(bat_priv,
					    dat_entry->mac_addr, vid)) {
			batadv_dbg(BATADV_DBG_DAT, bat_priv,
				   "Device %pM claimed by another backbone gw. Don't send ARP reply!",
				   dat_entry->mac_addr);
			ret = true;
			goto out;
		}

		skb_new = batadv_dat_arp_create_reply(bat_priv, ip_dst, ip_src,
						      dat_entry->mac_addr,
						      hw_src, vid);
		if (!skb_new)
			goto out;

		skb_new->protocol = eth_type_trans(skb_new, soft_iface);

		batadv_inc_counter(bat_priv, BATADV_CNT_RX);
		batadv_add_counter(bat_priv, BATADV_CNT_RX_BYTES,
				   skb->len + ETH_HLEN + hdr_size);

		netif_rx(skb_new);
		batadv_dbg(BATADV_DBG_DAT, bat_priv, "ARP request replied locally\n");
		ret = true;
	} else {
		/* Send the request to the DHT */
		ret = batadv_dat_forward_data(bat_priv, skb, ip_dst, vid,
					      BATADV_P_DAT_DHT_GET);
	}
out:
	if (dat_entry)
		batadv_dat_entry_put(dat_entry);
	return ret;
}

/**
 * batadv_dat_snoop_incoming_arp_request() - snoop the ARP request and try to
 * answer using the local DAT storage
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: packet to check
 * @hdr_size: size of the encapsulation header
 *
 * Return: true if the request has been answered, false otherwise.
 */
bool batadv_dat_snoop_incoming_arp_request(struct batadv_priv *bat_priv,
					   struct sk_buff *skb, int hdr_size)
{
	u16 type;
	__be32 ip_src, ip_dst;
	u8 *hw_src;
	struct sk_buff *skb_new;
	struct batadv_dat_entry *dat_entry = NULL;
	bool ret = false;
	unsigned short vid;
	int err;

	if (!atomic_read(&bat_priv->distributed_arp_table))
		goto out;

	vid = batadv_dat_get_vid(skb, &hdr_size);

	type = batadv_arp_get_type(bat_priv, skb, hdr_size);
	if (type != ARPOP_REQUEST)
		goto out;

	hw_src = batadv_arp_hw_src(skb, hdr_size);
	ip_src = batadv_arp_ip_src(skb, hdr_size);
	ip_dst = batadv_arp_ip_dst(skb, hdr_size);

	batadv_dbg_arp(bat_priv, skb, hdr_size, "Parsing incoming ARP REQUEST");

	batadv_dat_entry_add(bat_priv, ip_src, hw_src, vid);

	dat_entry = batadv_dat_entry_hash_find(bat_priv, ip_dst, vid);
	if (!dat_entry)
		goto out;

	skb_new = batadv_dat_arp_create_reply(bat_priv, ip_dst, ip_src,
					      dat_entry->mac_addr, hw_src, vid);
	if (!skb_new)
		goto out;

	/* To preserve backwards compatibility, the node has choose the outgoing
	 * format based on the incoming request packet type. The assumption is
	 * that a node not using the 4addr packet format doesn't support it.
	 */
	if (hdr_size == sizeof(struct batadv_unicast_4addr_packet))
		err = batadv_send_skb_via_tt_4addr(bat_priv, skb_new,
						   BATADV_P_DAT_CACHE_REPLY,
						   NULL, vid);
	else
		err = batadv_send_skb_via_tt(bat_priv, skb_new, NULL, vid);

	if (err != NET_XMIT_DROP) {
		batadv_inc_counter(bat_priv, BATADV_CNT_DAT_CACHED_REPLY_TX);
		ret = true;
	}
out:
	if (dat_entry)
		batadv_dat_entry_put(dat_entry);
	if (ret)
		kfree_skb(skb);
	return ret;
}

/**
 * batadv_dat_snoop_outgoing_arp_reply() - snoop the ARP reply and fill the DHT
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: packet to check
 */
void batadv_dat_snoop_outgoing_arp_reply(struct batadv_priv *bat_priv,
					 struct sk_buff *skb)
{
	u16 type;
	__be32 ip_src, ip_dst;
	u8 *hw_src, *hw_dst;
	int hdr_size = 0;
	unsigned short vid;

	if (!atomic_read(&bat_priv->distributed_arp_table))
		return;

	vid = batadv_dat_get_vid(skb, &hdr_size);

	type = batadv_arp_get_type(bat_priv, skb, hdr_size);
	if (type != ARPOP_REPLY)
		return;

	batadv_dbg_arp(bat_priv, skb, hdr_size, "Parsing outgoing ARP REPLY");

	hw_src = batadv_arp_hw_src(skb, hdr_size);
	ip_src = batadv_arp_ip_src(skb, hdr_size);
	hw_dst = batadv_arp_hw_dst(skb, hdr_size);
	ip_dst = batadv_arp_ip_dst(skb, hdr_size);

	batadv_dat_entry_add(bat_priv, ip_src, hw_src, vid);
	batadv_dat_entry_add(bat_priv, ip_dst, hw_dst, vid);

	/* Send the ARP reply to the candidates for both the IP addresses that
	 * the node obtained from the ARP reply
	 */
	batadv_dat_forward_data(bat_priv, skb, ip_src, vid,
				BATADV_P_DAT_DHT_PUT);
	batadv_dat_forward_data(bat_priv, skb, ip_dst, vid,
				BATADV_P_DAT_DHT_PUT);
}

/**
 * batadv_dat_snoop_incoming_arp_reply() - snoop the ARP reply and fill the
 *  local DAT storage only
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: packet to check
 * @hdr_size: size of the encapsulation header
 *
 * Return: true if the packet was snooped and consumed by DAT. False if the
 * packet has to be delivered to the interface
 */
bool batadv_dat_snoop_incoming_arp_reply(struct batadv_priv *bat_priv,
					 struct sk_buff *skb, int hdr_size)
{
	struct batadv_dat_entry *dat_entry = NULL;
	u16 type;
	__be32 ip_src, ip_dst;
	u8 *hw_src, *hw_dst;
	bool dropped = false;
	unsigned short vid;

	if (!atomic_read(&bat_priv->distributed_arp_table))
		goto out;

	vid = batadv_dat_get_vid(skb, &hdr_size);

	type = batadv_arp_get_type(bat_priv, skb, hdr_size);
	if (type != ARPOP_REPLY)
		goto out;

	batadv_dbg_arp(bat_priv, skb, hdr_size, "Parsing incoming ARP REPLY");

	hw_src = batadv_arp_hw_src(skb, hdr_size);
	ip_src = batadv_arp_ip_src(skb, hdr_size);
	hw_dst = batadv_arp_hw_dst(skb, hdr_size);
	ip_dst = batadv_arp_ip_dst(skb, hdr_size);

	/* If ip_dst is already in cache and has the right mac address,
	 * drop this frame if this ARP reply is destined for us because it's
	 * most probably an ARP reply generated by another node of the DHT.
	 * We have most probably received already a reply earlier. Delivering
	 * this frame would lead to doubled receive of an ARP reply.
	 */
	dat_entry = batadv_dat_entry_hash_find(bat_priv, ip_src, vid);
	if (dat_entry && batadv_compare_eth(hw_src, dat_entry->mac_addr)) {
		batadv_dbg(BATADV_DBG_DAT, bat_priv, "Doubled ARP reply removed: ARP MSG = [src: %pM-%pI4 dst: %pM-%pI4]; dat_entry: %pM-%pI4\n",
			   hw_src, &ip_src, hw_dst, &ip_dst,
			   dat_entry->mac_addr,	&dat_entry->ip);
		dropped = true;
	}

	/* Update our internal cache with both the IP addresses the node got
	 * within the ARP reply
	 */
	batadv_dat_entry_add(bat_priv, ip_src, hw_src, vid);
	batadv_dat_entry_add(bat_priv, ip_dst, hw_dst, vid);

	if (dropped)
		goto out;

	/* If BLA is enabled, only forward ARP replies if we have claimed the
	 * source of the ARP reply or if no one else of the same backbone has
	 * already claimed that client. This prevents that different gateways
	 * to the same backbone all forward the ARP reply leading to multiple
	 * replies in the backbone.
	 */
	if (!batadv_bla_check_claim(bat_priv, hw_src, vid)) {
		batadv_dbg(BATADV_DBG_DAT, bat_priv,
			   "Device %pM claimed by another backbone gw. Drop ARP reply.\n",
			   hw_src);
		dropped = true;
		goto out;
	}

	/* if this REPLY is directed to a client of mine, let's deliver the
	 * packet to the interface
	 */
	dropped = !batadv_is_my_client(bat_priv, hw_dst, vid);

	/* if this REPLY is sent on behalf of a client of mine, let's drop the
	 * packet because the client will reply by itself
	 */
	dropped |= batadv_is_my_client(bat_priv, hw_src, vid);
out:
	if (dropped)
		kfree_skb(skb);
	if (dat_entry)
		batadv_dat_entry_put(dat_entry);
	/* if dropped == false -> deliver to the interface */
	return dropped;
}

/**
 * batadv_dat_check_dhcp_ipudp() - check skb for IP+UDP headers valid for DHCP
 * @skb: the packet to check
 * @ip_src: a buffer to store the IPv4 source address in
 *
 * Checks whether the given skb has an IP and UDP header valid for a DHCP
 * message from a DHCP server. And if so, stores the IPv4 source address in
 * the provided buffer.
 *
 * Return: True if valid, false otherwise.
 */
static bool
batadv_dat_check_dhcp_ipudp(struct sk_buff *skb, __be32 *ip_src)
{
	unsigned int offset = skb_network_offset(skb);
	struct udphdr *udphdr, _udphdr;
	struct iphdr *iphdr, _iphdr;

	iphdr = skb_header_pointer(skb, offset, sizeof(_iphdr), &_iphdr);
	if (!iphdr || iphdr->version != 4 || iphdr->ihl * 4 < sizeof(_iphdr))
		return false;

	if (iphdr->protocol != IPPROTO_UDP)
		return false;

	offset += iphdr->ihl * 4;
	skb_set_transport_header(skb, offset);

	udphdr = skb_header_pointer(skb, offset, sizeof(_udphdr), &_udphdr);
	if (!udphdr || udphdr->source != htons(67))
		return false;

	*ip_src = get_unaligned(&iphdr->saddr);

	return true;
}

/**
 * batadv_dat_check_dhcp() - examine packet for valid DHCP message
 * @skb: the packet to check
 * @proto: ethernet protocol hint (behind a potential vlan)
 * @ip_src: a buffer to store the IPv4 source address in
 *
 * Checks whether the given skb is a valid DHCP packet. And if so, stores the
 * IPv4 source address in the provided buffer.
 *
 * Caller needs to ensure that the skb network header is set correctly.
 *
 * Return: If skb is a valid DHCP packet, then returns its op code
 * (e.g. BOOTREPLY vs. BOOTREQUEST). Otherwise returns -EINVAL.
 */
static int
batadv_dat_check_dhcp(struct sk_buff *skb, __be16 proto, __be32 *ip_src)
{
	__be32 *magic, _magic;
	unsigned int offset;
	struct {
		__u8 op;
		__u8 htype;
		__u8 hlen;
		__u8 hops;
	} *dhcp_h, _dhcp_h;

	if (proto != htons(ETH_P_IP))
		return -EINVAL;

	if (!batadv_dat_check_dhcp_ipudp(skb, ip_src))
		return -EINVAL;

	offset = skb_transport_offset(skb) + sizeof(struct udphdr);
	if (skb->len < offset + sizeof(struct batadv_dhcp_packet))
		return -EINVAL;

	dhcp_h = skb_header_pointer(skb, offset, sizeof(_dhcp_h), &_dhcp_h);
	if (!dhcp_h || dhcp_h->htype != BATADV_HTYPE_ETHERNET ||
	    dhcp_h->hlen != ETH_ALEN)
		return -EINVAL;

	offset += offsetof(struct batadv_dhcp_packet, magic);

	magic = skb_header_pointer(skb, offset, sizeof(_magic), &_magic);
	if (!magic || get_unaligned(magic) != htonl(BATADV_DHCP_MAGIC))
		return -EINVAL;

	return dhcp_h->op;
}

/**
 * batadv_dat_get_dhcp_message_type() - get message type of a DHCP packet
 * @skb: the DHCP packet to parse
 *
 * Iterates over the DHCP options of the given DHCP packet to find a
 * DHCP Message Type option and parse it.
 *
 * Caller needs to ensure that the given skb is a valid DHCP packet and
 * that the skb transport header is set correctly.
 *
 * Return: The found DHCP message type value, if found. -EINVAL otherwise.
 */
static int batadv_dat_get_dhcp_message_type(struct sk_buff *skb)
{
	unsigned int offset = skb_transport_offset(skb) + sizeof(struct udphdr);
	u8 *type, _type;
	struct {
		u8 type;
		u8 len;
	} *tl, _tl;

	offset += sizeof(struct batadv_dhcp_packet);

	while ((tl = skb_header_pointer(skb, offset, sizeof(_tl), &_tl))) {
		if (tl->type == BATADV_DHCP_OPT_MSG_TYPE)
			break;

		if (tl->type == BATADV_DHCP_OPT_END)
			break;

		if (tl->type == BATADV_DHCP_OPT_PAD)
			offset++;
		else
			offset += tl->len + sizeof(_tl);
	}

	/* Option Overload Code not supported */
	if (!tl || tl->type != BATADV_DHCP_OPT_MSG_TYPE ||
	    tl->len != sizeof(_type))
		return -EINVAL;

	offset += sizeof(_tl);

	type = skb_header_pointer(skb, offset, sizeof(_type), &_type);
	if (!type)
		return -EINVAL;

	return *type;
}

/**
 * batadv_dat_get_dhcp_yiaddr() - get yiaddr from a DHCP packet
 * @skb: the DHCP packet to parse
 * @buf: a buffer to store the yiaddr in
 *
 * Caller needs to ensure that the given skb is a valid DHCP packet and
 * that the skb transport header is set correctly.
 *
 * Return: True on success, false otherwise.
 */
static bool batadv_dat_dhcp_get_yiaddr(struct sk_buff *skb, __be32 *buf)
{
	unsigned int offset = skb_transport_offset(skb) + sizeof(struct udphdr);
	__be32 *yiaddr;

	offset += offsetof(struct batadv_dhcp_packet, yiaddr);
	yiaddr = skb_header_pointer(skb, offset, BATADV_DHCP_YIADDR_LEN, buf);

	if (!yiaddr)
		return false;

	if (yiaddr != buf)
		*buf = get_unaligned(yiaddr);

	return true;
}

/**
 * batadv_dat_get_dhcp_chaddr() - get chaddr from a DHCP packet
 * @skb: the DHCP packet to parse
 * @buf: a buffer to store the chaddr in
 *
 * Caller needs to ensure that the given skb is a valid DHCP packet and
 * that the skb transport header is set correctly.
 *
 * Return: True on success, false otherwise
 */
static bool batadv_dat_get_dhcp_chaddr(struct sk_buff *skb, u8 *buf)
{
	unsigned int offset = skb_transport_offset(skb) + sizeof(struct udphdr);
	u8 *chaddr;

	offset += offsetof(struct batadv_dhcp_packet, chaddr);
	chaddr = skb_header_pointer(skb, offset, BATADV_DHCP_CHADDR_LEN, buf);

	if (!chaddr)
		return false;

	if (chaddr != buf)
		memcpy(buf, chaddr, BATADV_DHCP_CHADDR_LEN);

	return true;
}

/**
 * batadv_dat_put_dhcp() - puts addresses from a DHCP packet into the DHT and
 *  DAT cache
 * @bat_priv: the bat priv with all the soft interface information
 * @chaddr: the DHCP client MAC address
 * @yiaddr: the DHCP client IP address
 * @hw_dst: the DHCP server MAC address
 * @ip_dst: the DHCP server IP address
 * @vid: VLAN identifier
 *
 * Adds given MAC/IP pairs to the local DAT cache and propagates them further
 * into the DHT.
 *
 * For the DHT propagation, client MAC + IP will appear as the ARP Reply
 * transmitter (and hw_dst/ip_dst as the target).
 */
static void batadv_dat_put_dhcp(struct batadv_priv *bat_priv, u8 *chaddr,
				__be32 yiaddr, u8 *hw_dst, __be32 ip_dst,
				unsigned short vid)
{
	struct sk_buff *skb;

	skb = batadv_dat_arp_create_reply(bat_priv, yiaddr, ip_dst, chaddr,
					  hw_dst, vid);
	if (!skb)
		return;

	skb_set_network_header(skb, ETH_HLEN);

	batadv_dat_entry_add(bat_priv, yiaddr, chaddr, vid);
	batadv_dat_entry_add(bat_priv, ip_dst, hw_dst, vid);

	batadv_dat_forward_data(bat_priv, skb, yiaddr, vid,
				BATADV_P_DAT_DHT_PUT);
	batadv_dat_forward_data(bat_priv, skb, ip_dst, vid,
				BATADV_P_DAT_DHT_PUT);

	consume_skb(skb);

	batadv_dbg(BATADV_DBG_DAT, bat_priv,
		   "Snooped from outgoing DHCPACK (server address): %pI4, %pM (vid: %i)\n",
		   &ip_dst, hw_dst, batadv_print_vid(vid));
	batadv_dbg(BATADV_DBG_DAT, bat_priv,
		   "Snooped from outgoing DHCPACK (client address): %pI4, %pM (vid: %i)\n",
		   &yiaddr, chaddr, batadv_print_vid(vid));
}

/**
 * batadv_dat_check_dhcp_ack() - examine packet for valid DHCP message
 * @skb: the packet to check
 * @proto: ethernet protocol hint (behind a potential vlan)
 * @ip_src: a buffer to store the IPv4 source address in
 * @chaddr: a buffer to store the DHCP Client Hardware Address in
 * @yiaddr: a buffer to store the DHCP Your IP Address in
 *
 * Checks whether the given skb is a valid DHCPACK. And if so, stores the
 * IPv4 server source address (ip_src), client MAC address (chaddr) and client
 * IPv4 address (yiaddr) in the provided buffers.
 *
 * Caller needs to ensure that the skb network header is set correctly.
 *
 * Return: True if the skb is a valid DHCPACK. False otherwise.
 */
static bool
batadv_dat_check_dhcp_ack(struct sk_buff *skb, __be16 proto, __be32 *ip_src,
			  u8 *chaddr, __be32 *yiaddr)
{
	int type;

	type = batadv_dat_check_dhcp(skb, proto, ip_src);
	if (type != BATADV_BOOTREPLY)
		return false;

	type = batadv_dat_get_dhcp_message_type(skb);
	if (type != BATADV_DHCPACK)
		return false;

	if (!batadv_dat_dhcp_get_yiaddr(skb, yiaddr))
		return false;

	if (!batadv_dat_get_dhcp_chaddr(skb, chaddr))
		return false;

	return true;
}

/**
 * batadv_dat_snoop_outgoing_dhcp_ack() - snoop DHCPACK and fill DAT with it
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: the packet to snoop
 * @proto: ethernet protocol hint (behind a potential vlan)
 * @vid: VLAN identifier
 *
 * This function first checks whether the given skb is a valid DHCPACK. If
 * so then its source MAC and IP as well as its DHCP Client Hardware Address
 * field and DHCP Your IP Address field are added to the local DAT cache and
 * propagated into the DHT.
 *
 * Caller needs to ensure that the skb mac and network headers are set
 * correctly.
 */
void batadv_dat_snoop_outgoing_dhcp_ack(struct batadv_priv *bat_priv,
					struct sk_buff *skb,
					__be16 proto,
					unsigned short vid)
{
	u8 chaddr[BATADV_DHCP_CHADDR_LEN];
	__be32 ip_src, yiaddr;

	if (!atomic_read(&bat_priv->distributed_arp_table))
		return;

	if (!batadv_dat_check_dhcp_ack(skb, proto, &ip_src, chaddr, &yiaddr))
		return;

	batadv_dat_put_dhcp(bat_priv, chaddr, yiaddr, eth_hdr(skb)->h_source,
			    ip_src, vid);
}

/**
 * batadv_dat_snoop_incoming_dhcp_ack() - snoop DHCPACK and fill DAT cache
 * @bat_priv: the bat priv with all the soft interface information
 * @skb: the packet to snoop
 * @hdr_size: header size, up to the tail of the batman-adv header
 *
 * This function first checks whether the given skb is a valid DHCPACK. If
 * so then its source MAC and IP as well as its DHCP Client Hardware Address
 * field and DHCP Your IP Address field are added to the local DAT cache.
 */
void batadv_dat_snoop_incoming_dhcp_ack(struct batadv_priv *bat_priv,
					struct sk_buff *skb, int hdr_size)
{
	u8 chaddr[BATADV_DHCP_CHADDR_LEN];
	struct ethhdr *ethhdr;
	__be32 ip_src, yiaddr;
	unsigned short vid;
	__be16 proto;
	u8 *hw_src;

	if (!atomic_read(&bat_priv->distributed_arp_table))
		return;

	if (unlikely(!pskb_may_pull(skb, hdr_size + ETH_HLEN)))
		return;

	ethhdr = (struct ethhdr *)(skb->data + hdr_size);
	skb_set_network_header(skb, hdr_size + ETH_HLEN);
	proto = ethhdr->h_proto;

	if (!batadv_dat_check_dhcp_ack(skb, proto, &ip_src, chaddr, &yiaddr))
		return;

	hw_src = ethhdr->h_source;
	vid = batadv_dat_get_vid(skb, &hdr_size);

	batadv_dat_entry_add(bat_priv, yiaddr, chaddr, vid);
	batadv_dat_entry_add(bat_priv, ip_src, hw_src, vid);

	batadv_dbg(BATADV_DBG_DAT, bat_priv,
		   "Snooped from incoming DHCPACK (server address): %pI4, %pM (vid: %i)\n",
		   &ip_src, hw_src, batadv_print_vid(vid));
	batadv_dbg(BATADV_DBG_DAT, bat_priv,
		   "Snooped from incoming DHCPACK (client address): %pI4, %pM (vid: %i)\n",
		   &yiaddr, chaddr, batadv_print_vid(vid));
}

/**
 * batadv_dat_drop_broadcast_packet() - check if an ARP request has to be
 *  dropped (because the node has already obtained the reply via DAT) or not
 * @bat_priv: the bat priv with all the soft interface information
 * @forw_packet: the broadcast packet
 *
 * Return: true if the node can drop the packet, false otherwise.
 */
bool batadv_dat_drop_broadcast_packet(struct batadv_priv *bat_priv,
				      struct batadv_forw_packet *forw_packet)
{
	u16 type;
	__be32 ip_dst;
	struct batadv_dat_entry *dat_entry = NULL;
	bool ret = false;
	int hdr_size = sizeof(struct batadv_bcast_packet);
	unsigned short vid;

	if (!atomic_read(&bat_priv->distributed_arp_table))
		goto out;

	/* If this packet is an ARP_REQUEST and the node already has the
	 * information that it is going to ask, then the packet can be dropped
	 */
	if (batadv_forw_packet_is_rebroadcast(forw_packet))
		goto out;

	vid = batadv_dat_get_vid(forw_packet->skb, &hdr_size);

	type = batadv_arp_get_type(bat_priv, forw_packet->skb, hdr_size);
	if (type != ARPOP_REQUEST)
		goto out;

	ip_dst = batadv_arp_ip_dst(forw_packet->skb, hdr_size);
	dat_entry = batadv_dat_entry_hash_find(bat_priv, ip_dst, vid);
	/* check if the node already got this entry */
	if (!dat_entry) {
		batadv_dbg(BATADV_DBG_DAT, bat_priv,
			   "ARP Request for %pI4: fallback\n", &ip_dst);
		goto out;
	}

	batadv_dbg(BATADV_DBG_DAT, bat_priv,
		   "ARP Request for %pI4: fallback prevented\n", &ip_dst);
	ret = true;

out:
	if (dat_entry)
		batadv_dat_entry_put(dat_entry);
	return ret;
}
