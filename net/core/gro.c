// SPDX-License-Identifier: GPL-2.0-or-later
#include <net/gro.h>
#include <net/dst_metadata.h>
#include <net/busy_poll.h>
#include <trace/events/net.h>
#include <linux/skbuff_ref.h>

#define MAX_GRO_SKBS 8

/* This should be increased if a protocol with a bigger head is added. */
#define GRO_MAX_HEAD (MAX_HEADER + 128)

static DEFINE_SPINLOCK(offload_lock);

/**
 *	dev_add_offload - register offload handlers
 *	@po: protocol offload declaration
 *
 *	Add protocol offload handlers to the networking stack. The passed
 *	&proto_offload is linked into kernel lists and may not be freed until
 *	it has been removed from the kernel lists.
 *
 *	This call does not sleep therefore it can not
 *	guarantee all CPU's that are in middle of receiving packets
 *	will see the new offload handlers (until the next received packet).
 */
void dev_add_offload(struct packet_offload *po)
{
	struct packet_offload *elem;

	spin_lock(&offload_lock);
	list_for_each_entry(elem, &net_hotdata.offload_base, list) {
		if (po->priority < elem->priority)
			break;
	}
	list_add_rcu(&po->list, elem->list.prev);
	spin_unlock(&offload_lock);
}
EXPORT_SYMBOL(dev_add_offload);

/**
 *	__dev_remove_offload	 - remove offload handler
 *	@po: packet offload declaration
 *
 *	Remove a protocol offload handler that was previously added to the
 *	kernel offload handlers by dev_add_offload(). The passed &offload_type
 *	is removed from the kernel lists and can be freed or reused once this
 *	function returns.
 *
 *      The packet type might still be in use by receivers
 *	and must not be freed until after all the CPU's have gone
 *	through a quiescent state.
 */
static void __dev_remove_offload(struct packet_offload *po)
{
	struct list_head *head = &net_hotdata.offload_base;
	struct packet_offload *po1;

	spin_lock(&offload_lock);

	list_for_each_entry(po1, head, list) {
		if (po == po1) {
			list_del_rcu(&po->list);
			goto out;
		}
	}

	pr_warn("dev_remove_offload: %p not found\n", po);
out:
	spin_unlock(&offload_lock);
}

/**
 *	dev_remove_offload	 - remove packet offload handler
 *	@po: packet offload declaration
 *
 *	Remove a packet offload handler that was previously added to the kernel
 *	offload handlers by dev_add_offload(). The passed &offload_type is
 *	removed from the kernel lists and can be freed or reused once this
 *	function returns.
 *
 *	This call sleeps to guarantee that no CPU is looking at the packet
 *	type after return.
 */
void dev_remove_offload(struct packet_offload *po)
{
	__dev_remove_offload(po);

	synchronize_net();
}
EXPORT_SYMBOL(dev_remove_offload);


int skb_gro_receive(struct sk_buff *p, struct sk_buff *skb)
{
	struct skb_shared_info *pinfo, *skbinfo = skb_shinfo(skb);
	unsigned int offset = skb_gro_offset(skb);
	unsigned int headlen = skb_headlen(skb);
	unsigned int len = skb_gro_len(skb);
	unsigned int delta_truesize;
	unsigned int new_truesize;
	struct sk_buff *lp;
	int segs;

	/* Do not splice page pool based packets w/ non-page pool
	 * packets. This can result in reference count issues as page
	 * pool pages will not decrement the reference count and will
	 * instead be immediately returned to the pool or have frag
	 * count decremented.
	 */
	if (p->pp_recycle != skb->pp_recycle)
		return -ETOOMANYREFS;

	if (unlikely(p->len + len >= netif_get_gro_max_size(p->dev, p) ||
		     NAPI_GRO_CB(skb)->flush))
		return -E2BIG;

	if (unlikely(p->len + len >= GRO_LEGACY_MAX_SIZE)) {
		if (NAPI_GRO_CB(skb)->proto != IPPROTO_TCP ||
		    (p->protocol == htons(ETH_P_IPV6) &&
		     skb_headroom(p) < sizeof(struct hop_jumbo_hdr)) ||
		    p->encapsulation)
			return -E2BIG;
	}

	segs = NAPI_GRO_CB(skb)->count;
	lp = NAPI_GRO_CB(p)->last;
	pinfo = skb_shinfo(lp);

	if (headlen <= offset) {
		skb_frag_t *frag;
		skb_frag_t *frag2;
		int i = skbinfo->nr_frags;
		int nr_frags = pinfo->nr_frags + i;

		if (nr_frags > MAX_SKB_FRAGS)
			goto merge;

		offset -= headlen;
		pinfo->nr_frags = nr_frags;
		skbinfo->nr_frags = 0;

		frag = pinfo->frags + nr_frags;
		frag2 = skbinfo->frags + i;
		do {
			*--frag = *--frag2;
		} while (--i);

		skb_frag_off_add(frag, offset);
		skb_frag_size_sub(frag, offset);

		/* all fragments truesize : remove (head size + sk_buff) */
		new_truesize = SKB_TRUESIZE(skb_end_offset(skb));
		delta_truesize = skb->truesize - new_truesize;

		skb->truesize = new_truesize;
		skb->len -= skb->data_len;
		skb->data_len = 0;

		NAPI_GRO_CB(skb)->free = NAPI_GRO_FREE;
		goto done;
	} else if (skb->head_frag) {
		int nr_frags = pinfo->nr_frags;
		skb_frag_t *frag = pinfo->frags + nr_frags;
		struct page *page = virt_to_head_page(skb->head);
		unsigned int first_size = headlen - offset;
		unsigned int first_offset;

		if (nr_frags + 1 + skbinfo->nr_frags > MAX_SKB_FRAGS)
			goto merge;

		first_offset = skb->data -
			       (unsigned char *)page_address(page) +
			       offset;

		pinfo->nr_frags = nr_frags + 1 + skbinfo->nr_frags;

		skb_frag_fill_page_desc(frag, page, first_offset, first_size);

		memcpy(frag + 1, skbinfo->frags, sizeof(*frag) * skbinfo->nr_frags);
		/* We dont need to clear skbinfo->nr_frags here */

		new_truesize = SKB_DATA_ALIGN(sizeof(struct sk_buff));
		delta_truesize = skb->truesize - new_truesize;
		skb->truesize = new_truesize;
		NAPI_GRO_CB(skb)->free = NAPI_GRO_FREE_STOLEN_HEAD;
		goto done;
	}

merge:
	/* sk ownership - if any - completely transferred to the aggregated packet */
	skb->destructor = NULL;
	skb->sk = NULL;
	delta_truesize = skb->truesize;
	if (offset > headlen) {
		unsigned int eat = offset - headlen;

		skb_frag_off_add(&skbinfo->frags[0], eat);
		skb_frag_size_sub(&skbinfo->frags[0], eat);
		skb->data_len -= eat;
		skb->len -= eat;
		offset = headlen;
	}

	__skb_pull(skb, offset);

	if (NAPI_GRO_CB(p)->last == p)
		skb_shinfo(p)->frag_list = skb;
	else
		NAPI_GRO_CB(p)->last->next = skb;
	NAPI_GRO_CB(p)->last = skb;
	__skb_header_release(skb);
	lp = p;

done:
	NAPI_GRO_CB(p)->count += segs;
	p->data_len += len;
	p->truesize += delta_truesize;
	p->len += len;
	if (lp != p) {
		lp->data_len += len;
		lp->truesize += delta_truesize;
		lp->len += len;
	}
	NAPI_GRO_CB(skb)->same_flow = 1;
	return 0;
}

int skb_gro_receive_list(struct sk_buff *p, struct sk_buff *skb)
{
	if (unlikely(p->len + skb->len >= 65536))
		return -E2BIG;

	if (NAPI_GRO_CB(p)->last == p)
		skb_shinfo(p)->frag_list = skb;
	else
		NAPI_GRO_CB(p)->last->next = skb;

	skb_pull(skb, skb_gro_offset(skb));

	NAPI_GRO_CB(p)->last = skb;
	NAPI_GRO_CB(p)->count++;
	p->data_len += skb->len;

	/* sk ownership - if any - completely transferred to the aggregated packet */
	skb->destructor = NULL;
	skb->sk = NULL;
	p->truesize += skb->truesize;
	p->len += skb->len;

	NAPI_GRO_CB(skb)->same_flow = 1;

	return 0;
}


static void napi_gro_complete(struct napi_struct *napi, struct sk_buff *skb)
{
	struct list_head *head = &net_hotdata.offload_base;
	struct packet_offload *ptype;
	__be16 type = skb->protocol;
	int err = -ENOENT;

	BUILD_BUG_ON(sizeof(struct napi_gro_cb) > sizeof(skb->cb));

	if (NAPI_GRO_CB(skb)->count == 1) {
		skb_shinfo(skb)->gso_size = 0;
		goto out;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(ptype, head, list) {
		if (ptype->type != type || !ptype->callbacks.gro_complete)
			continue;

		err = INDIRECT_CALL_INET(ptype->callbacks.gro_complete,
					 ipv6_gro_complete, inet_gro_complete,
					 skb, 0);
		break;
	}
	rcu_read_unlock();

	if (err) {
		WARN_ON(&ptype->list == head);
		kfree_skb(skb);
		return;
	}

out:
	gro_normal_one(napi, skb, NAPI_GRO_CB(skb)->count);
}

static void __napi_gro_flush_chain(struct napi_struct *napi, u32 index,
				   bool flush_old)
{
	struct list_head *head = &napi->gro_hash[index].list;
	struct sk_buff *skb, *p;

	list_for_each_entry_safe_reverse(skb, p, head, list) {
		if (flush_old && NAPI_GRO_CB(skb)->age == jiffies)
			return;
		skb_list_del_init(skb);
		napi_gro_complete(napi, skb);
		napi->gro_hash[index].count--;
	}

	if (!napi->gro_hash[index].count)
		__clear_bit(index, &napi->gro_bitmask);
}

/* napi->gro_hash[].list contains packets ordered by age.
 * youngest packets at the head of it.
 * Complete skbs in reverse order to reduce latencies.
 */
void napi_gro_flush(struct napi_struct *napi, bool flush_old)
{
	unsigned long bitmask = napi->gro_bitmask;
	unsigned int i, base = ~0U;

	while ((i = ffs(bitmask)) != 0) {
		bitmask >>= i;
		base += i;
		__napi_gro_flush_chain(napi, base, flush_old);
	}
}
EXPORT_SYMBOL(napi_gro_flush);

static unsigned long gro_list_prepare_tc_ext(const struct sk_buff *skb,
					     const struct sk_buff *p,
					     unsigned long diffs)
{
#if IS_ENABLED(CONFIG_NET_TC_SKB_EXT)
	struct tc_skb_ext *skb_ext;
	struct tc_skb_ext *p_ext;

	skb_ext = skb_ext_find(skb, TC_SKB_EXT);
	p_ext = skb_ext_find(p, TC_SKB_EXT);

	diffs |= (!!p_ext) ^ (!!skb_ext);
	if (!diffs && unlikely(skb_ext))
		diffs |= p_ext->chain ^ skb_ext->chain;
#endif
	return diffs;
}

static void gro_list_prepare(const struct list_head *head,
			     const struct sk_buff *skb)
{
	unsigned int maclen = skb->dev->hard_header_len;
	u32 hash = skb_get_hash_raw(skb);
	struct sk_buff *p;

	list_for_each_entry(p, head, list) {
		unsigned long diffs;

		if (hash != skb_get_hash_raw(p)) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}

		diffs = (unsigned long)p->dev ^ (unsigned long)skb->dev;
		diffs |= p->vlan_all ^ skb->vlan_all;
		diffs |= skb_metadata_differs(p, skb);
		if (maclen == ETH_HLEN)
			diffs |= compare_ether_header(skb_mac_header(p),
						      skb_mac_header(skb));
		else if (!diffs)
			diffs = memcmp(skb_mac_header(p),
				       skb_mac_header(skb),
				       maclen);

		/* in most common scenarions 'slow_gro' is 0
		 * otherwise we are already on some slower paths
		 * either skip all the infrequent tests altogether or
		 * avoid trying too hard to skip each of them individually
		 */
		if (!diffs && unlikely(skb->slow_gro | p->slow_gro)) {
			diffs |= p->sk != skb->sk;
			diffs |= skb_metadata_dst_cmp(p, skb);
			diffs |= skb_get_nfct(p) ^ skb_get_nfct(skb);

			diffs |= gro_list_prepare_tc_ext(skb, p, diffs);
		}

		NAPI_GRO_CB(p)->same_flow = !diffs;
	}
}

static inline void skb_gro_reset_offset(struct sk_buff *skb, u32 nhoff)
{
	const struct skb_shared_info *pinfo;
	const skb_frag_t *frag0;
	unsigned int headlen;

	NAPI_GRO_CB(skb)->network_offset = 0;
	NAPI_GRO_CB(skb)->data_offset = 0;
	headlen = skb_headlen(skb);
	NAPI_GRO_CB(skb)->frag0 = skb->data;
	NAPI_GRO_CB(skb)->frag0_len = headlen;
	if (headlen)
		return;

	pinfo = skb_shinfo(skb);
	frag0 = &pinfo->frags[0];

	if (pinfo->nr_frags && !PageHighMem(skb_frag_page(frag0)) &&
	    (!NET_IP_ALIGN || !((skb_frag_off(frag0) + nhoff) & 3))) {
		NAPI_GRO_CB(skb)->frag0 = skb_frag_address(frag0);
		NAPI_GRO_CB(skb)->frag0_len = min_t(unsigned int,
						    skb_frag_size(frag0),
						    skb->end - skb->tail);
	}
}

static void gro_pull_from_frag0(struct sk_buff *skb, int grow)
{
	struct skb_shared_info *pinfo = skb_shinfo(skb);

	BUG_ON(skb->end - skb->tail < grow);

	memcpy(skb_tail_pointer(skb), NAPI_GRO_CB(skb)->frag0, grow);

	skb->data_len -= grow;
	skb->tail += grow;

	skb_frag_off_add(&pinfo->frags[0], grow);
	skb_frag_size_sub(&pinfo->frags[0], grow);

	if (unlikely(!skb_frag_size(&pinfo->frags[0]))) {
		skb_frag_unref(skb, 0);
		memmove(pinfo->frags, pinfo->frags + 1,
			--pinfo->nr_frags * sizeof(pinfo->frags[0]));
	}
}

static void gro_try_pull_from_frag0(struct sk_buff *skb)
{
	int grow = skb_gro_offset(skb) - skb_headlen(skb);

	if (grow > 0)
		gro_pull_from_frag0(skb, grow);
}

static void gro_flush_oldest(struct napi_struct *napi, struct list_head *head)
{
	struct sk_buff *oldest;

	oldest = list_last_entry(head, struct sk_buff, list);

	/* We are called with head length >= MAX_GRO_SKBS, so this is
	 * impossible.
	 */
	if (WARN_ON_ONCE(!oldest))
		return;

	/* Do not adjust napi->gro_hash[].count, caller is adding a new
	 * SKB to the chain.
	 */
	skb_list_del_init(oldest);
	napi_gro_complete(napi, oldest);
}

static enum gro_result dev_gro_receive(struct napi_struct *napi, struct sk_buff *skb)
{
	u32 bucket = skb_get_hash_raw(skb) & (GRO_HASH_BUCKETS - 1);
	struct gro_list *gro_list = &napi->gro_hash[bucket];
	struct list_head *head = &net_hotdata.offload_base;
	struct packet_offload *ptype;
	__be16 type = skb->protocol;
	struct sk_buff *pp = NULL;
	enum gro_result ret;
	int same_flow;

	if (netif_elide_gro(skb->dev))
		goto normal;

	gro_list_prepare(&gro_list->list, skb);

	rcu_read_lock();
	list_for_each_entry_rcu(ptype, head, list) {
		if (ptype->type == type && ptype->callbacks.gro_receive)
			goto found_ptype;
	}
	rcu_read_unlock();
	goto normal;

found_ptype:
	skb_set_network_header(skb, skb_gro_offset(skb));
	skb_reset_mac_len(skb);
	BUILD_BUG_ON(sizeof_field(struct napi_gro_cb, zeroed) != sizeof(u32));
	BUILD_BUG_ON(!IS_ALIGNED(offsetof(struct napi_gro_cb, zeroed),
					sizeof(u32))); /* Avoid slow unaligned acc */
	*(u32 *)&NAPI_GRO_CB(skb)->zeroed = 0;
	NAPI_GRO_CB(skb)->flush = skb_has_frag_list(skb);
	NAPI_GRO_CB(skb)->count = 1;
	if (unlikely(skb_is_gso(skb))) {
		NAPI_GRO_CB(skb)->count = skb_shinfo(skb)->gso_segs;
		/* Only support TCP and non DODGY users. */
		if (!skb_is_gso_tcp(skb) ||
		    (skb_shinfo(skb)->gso_type & SKB_GSO_DODGY))
			NAPI_GRO_CB(skb)->flush = 1;
	}

	/* Setup for GRO checksum validation */
	switch (skb->ip_summed) {
	case CHECKSUM_COMPLETE:
		NAPI_GRO_CB(skb)->csum = skb->csum;
		NAPI_GRO_CB(skb)->csum_valid = 1;
		break;
	case CHECKSUM_UNNECESSARY:
		NAPI_GRO_CB(skb)->csum_cnt = skb->csum_level + 1;
		break;
	}

	pp = INDIRECT_CALL_INET(ptype->callbacks.gro_receive,
				ipv6_gro_receive, inet_gro_receive,
				&gro_list->list, skb);

	rcu_read_unlock();

	if (PTR_ERR(pp) == -EINPROGRESS) {
		ret = GRO_CONSUMED;
		goto ok;
	}

	same_flow = NAPI_GRO_CB(skb)->same_flow;
	ret = NAPI_GRO_CB(skb)->free ? GRO_MERGED_FREE : GRO_MERGED;

	if (pp) {
		skb_list_del_init(pp);
		napi_gro_complete(napi, pp);
		gro_list->count--;
	}

	if (same_flow)
		goto ok;

	if (NAPI_GRO_CB(skb)->flush)
		goto normal;

	if (unlikely(gro_list->count >= MAX_GRO_SKBS))
		gro_flush_oldest(napi, &gro_list->list);
	else
		gro_list->count++;

	/* Must be called before setting NAPI_GRO_CB(skb)->{age|last} */
	gro_try_pull_from_frag0(skb);
	NAPI_GRO_CB(skb)->age = jiffies;
	NAPI_GRO_CB(skb)->last = skb;
	if (!skb_is_gso(skb))
		skb_shinfo(skb)->gso_size = skb_gro_len(skb);
	list_add(&skb->list, &gro_list->list);
	ret = GRO_HELD;
ok:
	if (gro_list->count) {
		if (!test_bit(bucket, &napi->gro_bitmask))
			__set_bit(bucket, &napi->gro_bitmask);
	} else if (test_bit(bucket, &napi->gro_bitmask)) {
		__clear_bit(bucket, &napi->gro_bitmask);
	}

	return ret;

normal:
	ret = GRO_NORMAL;
	gro_try_pull_from_frag0(skb);
	goto ok;
}

struct packet_offload *gro_find_receive_by_type(__be16 type)
{
	struct list_head *offload_head = &net_hotdata.offload_base;
	struct packet_offload *ptype;

	list_for_each_entry_rcu(ptype, offload_head, list) {
		if (ptype->type != type || !ptype->callbacks.gro_receive)
			continue;
		return ptype;
	}
	return NULL;
}
EXPORT_SYMBOL(gro_find_receive_by_type);

struct packet_offload *gro_find_complete_by_type(__be16 type)
{
	struct list_head *offload_head = &net_hotdata.offload_base;
	struct packet_offload *ptype;

	list_for_each_entry_rcu(ptype, offload_head, list) {
		if (ptype->type != type || !ptype->callbacks.gro_complete)
			continue;
		return ptype;
	}
	return NULL;
}
EXPORT_SYMBOL(gro_find_complete_by_type);

static gro_result_t napi_skb_finish(struct napi_struct *napi,
				    struct sk_buff *skb,
				    gro_result_t ret)
{
	switch (ret) {
	case GRO_NORMAL:
		gro_normal_one(napi, skb, 1);
		break;

	case GRO_MERGED_FREE:
		if (NAPI_GRO_CB(skb)->free == NAPI_GRO_FREE_STOLEN_HEAD)
			napi_skb_free_stolen_head(skb);
		else if (skb->fclone != SKB_FCLONE_UNAVAILABLE)
			__kfree_skb(skb);
		else
			__napi_kfree_skb(skb, SKB_CONSUMED);
		break;

	case GRO_HELD:
	case GRO_MERGED:
	case GRO_CONSUMED:
		break;
	}

	return ret;
}

gro_result_t napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb)
{
	gro_result_t ret;

	skb_mark_napi_id(skb, napi);
	trace_napi_gro_receive_entry(skb);

	skb_gro_reset_offset(skb, 0);

	ret = napi_skb_finish(napi, skb, dev_gro_receive(napi, skb));
	trace_napi_gro_receive_exit(ret);

	return ret;
}
EXPORT_SYMBOL(napi_gro_receive);

static void napi_reuse_skb(struct napi_struct *napi, struct sk_buff *skb)
{
	if (unlikely(skb->pfmemalloc)) {
		consume_skb(skb);
		return;
	}
	__skb_pull(skb, skb_headlen(skb));
	/* restore the reserve we had after netdev_alloc_skb_ip_align() */
	skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN - skb_headroom(skb));
	__vlan_hwaccel_clear_tag(skb);
	skb->dev = napi->dev;
	skb->skb_iif = 0;

	/* eth_type_trans() assumes pkt_type is PACKET_HOST */
	skb->pkt_type = PACKET_HOST;

	skb->encapsulation = 0;
	skb_shinfo(skb)->gso_type = 0;
	skb_shinfo(skb)->gso_size = 0;
	if (unlikely(skb->slow_gro)) {
		skb_orphan(skb);
		skb_ext_reset(skb);
		nf_reset_ct(skb);
		skb->slow_gro = 0;
	}

	napi->skb = skb;
}

struct sk_buff *napi_get_frags(struct napi_struct *napi)
{
	struct sk_buff *skb = napi->skb;

	if (!skb) {
		skb = napi_alloc_skb(napi, GRO_MAX_HEAD);
		if (skb) {
			napi->skb = skb;
			skb_mark_napi_id(skb, napi);
		}
	}
	return skb;
}
EXPORT_SYMBOL(napi_get_frags);

static gro_result_t napi_frags_finish(struct napi_struct *napi,
				      struct sk_buff *skb,
				      gro_result_t ret)
{
	switch (ret) {
	case GRO_NORMAL:
	case GRO_HELD:
		__skb_push(skb, ETH_HLEN);
		skb->protocol = eth_type_trans(skb, skb->dev);
		if (ret == GRO_NORMAL)
			gro_normal_one(napi, skb, 1);
		break;

	case GRO_MERGED_FREE:
		if (NAPI_GRO_CB(skb)->free == NAPI_GRO_FREE_STOLEN_HEAD)
			napi_skb_free_stolen_head(skb);
		else
			napi_reuse_skb(napi, skb);
		break;

	case GRO_MERGED:
	case GRO_CONSUMED:
		break;
	}

	return ret;
}

/* Upper GRO stack assumes network header starts at gro_offset=0
 * Drivers could call both napi_gro_frags() and napi_gro_receive()
 * We copy ethernet header into skb->data to have a common layout.
 */
static struct sk_buff *napi_frags_skb(struct napi_struct *napi)
{
	struct sk_buff *skb = napi->skb;
	const struct ethhdr *eth;
	unsigned int hlen = sizeof(*eth);

	napi->skb = NULL;

	skb_reset_mac_header(skb);
	skb_gro_reset_offset(skb, hlen);

	if (unlikely(!skb_gro_may_pull(skb, hlen))) {
		eth = skb_gro_header_slow(skb, hlen, 0);
		if (unlikely(!eth)) {
			net_warn_ratelimited("%s: dropping impossible skb from %s\n",
					     __func__, napi->dev->name);
			napi_reuse_skb(napi, skb);
			return NULL;
		}
	} else {
		eth = (const struct ethhdr *)skb->data;

		if (NAPI_GRO_CB(skb)->frag0 != skb->data)
			gro_pull_from_frag0(skb, hlen);

		NAPI_GRO_CB(skb)->frag0 += hlen;
		NAPI_GRO_CB(skb)->frag0_len -= hlen;
	}
	__skb_pull(skb, hlen);

	/*
	 * This works because the only protocols we care about don't require
	 * special handling.
	 * We'll fix it up properly in napi_frags_finish()
	 */
	skb->protocol = eth->h_proto;

	return skb;
}

gro_result_t napi_gro_frags(struct napi_struct *napi)
{
	gro_result_t ret;
	struct sk_buff *skb = napi_frags_skb(napi);

	trace_napi_gro_frags_entry(skb);

	ret = napi_frags_finish(napi, skb, dev_gro_receive(napi, skb));
	trace_napi_gro_frags_exit(ret);

	return ret;
}
EXPORT_SYMBOL(napi_gro_frags);

/* Compute the checksum from gro_offset and return the folded value
 * after adding in any pseudo checksum.
 */
__sum16 __skb_gro_checksum_complete(struct sk_buff *skb)
{
	__wsum wsum;
	__sum16 sum;

	wsum = skb_checksum(skb, skb_gro_offset(skb), skb_gro_len(skb), 0);

	/* NAPI_GRO_CB(skb)->csum holds pseudo checksum */
	sum = csum_fold(csum_add(NAPI_GRO_CB(skb)->csum, wsum));
	/* See comments in __skb_checksum_complete(). */
	if (likely(!sum)) {
		if (unlikely(skb->ip_summed == CHECKSUM_COMPLETE) &&
		    !skb->csum_complete_sw)
			netdev_rx_csum_fault(skb->dev, skb);
	}

	NAPI_GRO_CB(skb)->csum = wsum;
	NAPI_GRO_CB(skb)->csum_valid = 1;

	return sum;
}
EXPORT_SYMBOL(__skb_gro_checksum_complete);
