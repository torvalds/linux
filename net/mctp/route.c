// SPDX-License-Identifier: GPL-2.0
/*
 * Management Component Transport Protocol (MCTP) - routing
 * implementation.
 *
 * This is currently based on a simple routing table, with no dst cache. The
 * number of routes should stay fairly small, so the lookup cost is small.
 *
 * Copyright (c) 2021 Code Construct
 * Copyright (c) 2021 Google
 */

#include <linux/idr.h>
#include <linux/kconfig.h>
#include <linux/mctp.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>

#include <uapi/linux/if_arp.h>

#include <net/mctp.h>
#include <net/mctpdevice.h>
#include <net/netlink.h>
#include <net/sock.h>

#include <trace/events/mctp.h>

static const unsigned int mctp_message_maxlen = 64 * 1024;
static const unsigned long mctp_key_lifetime = 6 * CONFIG_HZ;

static void mctp_flow_prepare_output(struct sk_buff *skb, struct mctp_dev *dev);

/* route output callbacks */
static int mctp_route_discard(struct mctp_route *route, struct sk_buff *skb)
{
	kfree_skb(skb);
	return 0;
}

static struct mctp_sock *mctp_lookup_bind(struct net *net, struct sk_buff *skb)
{
	struct mctp_skb_cb *cb = mctp_cb(skb);
	struct mctp_hdr *mh;
	struct sock *sk;
	u8 type;

	WARN_ON(!rcu_read_lock_held());

	/* TODO: look up in skb->cb? */
	mh = mctp_hdr(skb);

	if (!skb_headlen(skb))
		return NULL;

	type = (*(u8 *)skb->data) & 0x7f;

	sk_for_each_rcu(sk, &net->mctp.binds) {
		struct mctp_sock *msk = container_of(sk, struct mctp_sock, sk);

		if (msk->bind_net != MCTP_NET_ANY && msk->bind_net != cb->net)
			continue;

		if (msk->bind_type != type)
			continue;

		if (!mctp_address_matches(msk->bind_addr, mh->dest))
			continue;

		return msk;
	}

	return NULL;
}

static bool mctp_key_match(struct mctp_sk_key *key, mctp_eid_t local,
			   mctp_eid_t peer, u8 tag)
{
	if (!mctp_address_matches(key->local_addr, local))
		return false;

	if (key->peer_addr != peer)
		return false;

	if (key->tag != tag)
		return false;

	return true;
}

/* returns a key (with key->lock held, and refcounted), or NULL if no such
 * key exists.
 */
static struct mctp_sk_key *mctp_lookup_key(struct net *net, struct sk_buff *skb,
					   mctp_eid_t peer,
					   unsigned long *irqflags)
	__acquires(&key->lock)
{
	struct mctp_sk_key *key, *ret;
	unsigned long flags;
	struct mctp_hdr *mh;
	u8 tag;

	mh = mctp_hdr(skb);
	tag = mh->flags_seq_tag & (MCTP_HDR_TAG_MASK | MCTP_HDR_FLAG_TO);

	ret = NULL;
	spin_lock_irqsave(&net->mctp.keys_lock, flags);

	hlist_for_each_entry(key, &net->mctp.keys, hlist) {
		if (!mctp_key_match(key, mh->dest, peer, tag))
			continue;

		spin_lock(&key->lock);
		if (key->valid) {
			refcount_inc(&key->refs);
			ret = key;
			break;
		}
		spin_unlock(&key->lock);
	}

	if (ret) {
		spin_unlock(&net->mctp.keys_lock);
		*irqflags = flags;
	} else {
		spin_unlock_irqrestore(&net->mctp.keys_lock, flags);
	}

	return ret;
}

static struct mctp_sk_key *mctp_key_alloc(struct mctp_sock *msk,
					  mctp_eid_t local, mctp_eid_t peer,
					  u8 tag, gfp_t gfp)
{
	struct mctp_sk_key *key;

	key = kzalloc(sizeof(*key), gfp);
	if (!key)
		return NULL;

	key->peer_addr = peer;
	key->local_addr = local;
	key->tag = tag;
	key->sk = &msk->sk;
	key->valid = true;
	spin_lock_init(&key->lock);
	refcount_set(&key->refs, 1);
	sock_hold(key->sk);

	return key;
}

void mctp_key_unref(struct mctp_sk_key *key)
{
	unsigned long flags;

	if (!refcount_dec_and_test(&key->refs))
		return;

	/* even though no refs exist here, the lock allows us to stay
	 * consistent with the locking requirement of mctp_dev_release_key
	 */
	spin_lock_irqsave(&key->lock, flags);
	mctp_dev_release_key(key->dev, key);
	spin_unlock_irqrestore(&key->lock, flags);

	sock_put(key->sk);
	kfree(key);
}

static int mctp_key_add(struct mctp_sk_key *key, struct mctp_sock *msk)
{
	struct net *net = sock_net(&msk->sk);
	struct mctp_sk_key *tmp;
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&net->mctp.keys_lock, flags);

	if (sock_flag(&msk->sk, SOCK_DEAD)) {
		rc = -EINVAL;
		goto out_unlock;
	}

	hlist_for_each_entry(tmp, &net->mctp.keys, hlist) {
		if (mctp_key_match(tmp, key->local_addr, key->peer_addr,
				   key->tag)) {
			spin_lock(&tmp->lock);
			if (tmp->valid)
				rc = -EEXIST;
			spin_unlock(&tmp->lock);
			if (rc)
				break;
		}
	}

	if (!rc) {
		refcount_inc(&key->refs);
		key->expiry = jiffies + mctp_key_lifetime;
		timer_reduce(&msk->key_expiry, key->expiry);

		hlist_add_head(&key->hlist, &net->mctp.keys);
		hlist_add_head(&key->sklist, &msk->keys);
	}

out_unlock:
	spin_unlock_irqrestore(&net->mctp.keys_lock, flags);

	return rc;
}

/* Helper for mctp_route_input().
 * We're done with the key; unlock and unref the key.
 * For the usual case of automatic expiry we remove the key from lists.
 * In the case that manual allocation is set on a key we release the lock
 * and local ref, reset reassembly, but don't remove from lists.
 */
static void __mctp_key_done_in(struct mctp_sk_key *key, struct net *net,
			       unsigned long flags, unsigned long reason)
__releases(&key->lock)
{
	struct sk_buff *skb;

	trace_mctp_key_release(key, reason);
	skb = key->reasm_head;
	key->reasm_head = NULL;

	if (!key->manual_alloc) {
		key->reasm_dead = true;
		key->valid = false;
		mctp_dev_release_key(key->dev, key);
	}
	spin_unlock_irqrestore(&key->lock, flags);

	if (!key->manual_alloc) {
		spin_lock_irqsave(&net->mctp.keys_lock, flags);
		if (!hlist_unhashed(&key->hlist)) {
			hlist_del_init(&key->hlist);
			hlist_del_init(&key->sklist);
			mctp_key_unref(key);
		}
		spin_unlock_irqrestore(&net->mctp.keys_lock, flags);
	}

	/* and one for the local reference */
	mctp_key_unref(key);

	kfree_skb(skb);
}

#ifdef CONFIG_MCTP_FLOWS
static void mctp_skb_set_flow(struct sk_buff *skb, struct mctp_sk_key *key)
{
	struct mctp_flow *flow;

	flow = skb_ext_add(skb, SKB_EXT_MCTP);
	if (!flow)
		return;

	refcount_inc(&key->refs);
	flow->key = key;
}

static void mctp_flow_prepare_output(struct sk_buff *skb, struct mctp_dev *dev)
{
	struct mctp_sk_key *key;
	struct mctp_flow *flow;

	flow = skb_ext_find(skb, SKB_EXT_MCTP);
	if (!flow)
		return;

	key = flow->key;

	if (WARN_ON(key->dev && key->dev != dev))
		return;

	mctp_dev_set_key(dev, key);
}
#else
static void mctp_skb_set_flow(struct sk_buff *skb, struct mctp_sk_key *key) {}
static void mctp_flow_prepare_output(struct sk_buff *skb, struct mctp_dev *dev) {}
#endif

static int mctp_frag_queue(struct mctp_sk_key *key, struct sk_buff *skb)
{
	struct mctp_hdr *hdr = mctp_hdr(skb);
	u8 exp_seq, this_seq;

	this_seq = (hdr->flags_seq_tag >> MCTP_HDR_SEQ_SHIFT)
		& MCTP_HDR_SEQ_MASK;

	if (!key->reasm_head) {
		key->reasm_head = skb;
		key->reasm_tailp = &(skb_shinfo(skb)->frag_list);
		key->last_seq = this_seq;
		return 0;
	}

	exp_seq = (key->last_seq + 1) & MCTP_HDR_SEQ_MASK;

	if (this_seq != exp_seq)
		return -EINVAL;

	if (key->reasm_head->len + skb->len > mctp_message_maxlen)
		return -EINVAL;

	skb->next = NULL;
	skb->sk = NULL;
	*key->reasm_tailp = skb;
	key->reasm_tailp = &skb->next;

	key->last_seq = this_seq;

	key->reasm_head->data_len += skb->len;
	key->reasm_head->len += skb->len;
	key->reasm_head->truesize += skb->truesize;

	return 0;
}

static int mctp_route_input(struct mctp_route *route, struct sk_buff *skb)
{
	struct mctp_sk_key *key, *any_key = NULL;
	struct net *net = dev_net(skb->dev);
	struct mctp_sock *msk;
	struct mctp_hdr *mh;
	unsigned long f;
	u8 tag, flags;
	int rc;

	msk = NULL;
	rc = -EINVAL;

	/* we may be receiving a locally-routed packet; drop source sk
	 * accounting
	 */
	skb_orphan(skb);

	/* ensure we have enough data for a header and a type */
	if (skb->len < sizeof(struct mctp_hdr) + 1)
		goto out;

	/* grab header, advance data ptr */
	mh = mctp_hdr(skb);
	skb_pull(skb, sizeof(struct mctp_hdr));

	if (mh->ver != 1)
		goto out;

	flags = mh->flags_seq_tag & (MCTP_HDR_FLAG_SOM | MCTP_HDR_FLAG_EOM);
	tag = mh->flags_seq_tag & (MCTP_HDR_TAG_MASK | MCTP_HDR_FLAG_TO);

	rcu_read_lock();

	/* lookup socket / reasm context, exactly matching (src,dest,tag).
	 * we hold a ref on the key, and key->lock held.
	 */
	key = mctp_lookup_key(net, skb, mh->src, &f);

	if (flags & MCTP_HDR_FLAG_SOM) {
		if (key) {
			msk = container_of(key->sk, struct mctp_sock, sk);
		} else {
			/* first response to a broadcast? do a more general
			 * key lookup to find the socket, but don't use this
			 * key for reassembly - we'll create a more specific
			 * one for future packets if required (ie, !EOM).
			 */
			any_key = mctp_lookup_key(net, skb, MCTP_ADDR_ANY, &f);
			if (any_key) {
				msk = container_of(any_key->sk,
						   struct mctp_sock, sk);
				spin_unlock_irqrestore(&any_key->lock, f);
			}
		}

		if (!key && !msk && (tag & MCTP_HDR_FLAG_TO))
			msk = mctp_lookup_bind(net, skb);

		if (!msk) {
			rc = -ENOENT;
			goto out_unlock;
		}

		/* single-packet message? deliver to socket, clean up any
		 * pending key.
		 */
		if (flags & MCTP_HDR_FLAG_EOM) {
			sock_queue_rcv_skb(&msk->sk, skb);
			if (key) {
				/* we've hit a pending reassembly; not much we
				 * can do but drop it
				 */
				__mctp_key_done_in(key, net, f,
						   MCTP_TRACE_KEY_REPLIED);
				key = NULL;
			}
			rc = 0;
			goto out_unlock;
		}

		/* broadcast response or a bind() - create a key for further
		 * packets for this message
		 */
		if (!key) {
			key = mctp_key_alloc(msk, mh->dest, mh->src,
					     tag, GFP_ATOMIC);
			if (!key) {
				rc = -ENOMEM;
				goto out_unlock;
			}

			/* we can queue without the key lock here, as the
			 * key isn't observable yet
			 */
			mctp_frag_queue(key, skb);

			/* if the key_add fails, we've raced with another
			 * SOM packet with the same src, dest and tag. There's
			 * no way to distinguish future packets, so all we
			 * can do is drop; we'll free the skb on exit from
			 * this function.
			 */
			rc = mctp_key_add(key, msk);
			if (!rc)
				trace_mctp_key_acquire(key);

			/* we don't need to release key->lock on exit, so
			 * clean up here and suppress the unlock via
			 * setting to NULL
			 */
			mctp_key_unref(key);
			key = NULL;

		} else {
			if (key->reasm_head || key->reasm_dead) {
				/* duplicate start? drop everything */
				__mctp_key_done_in(key, net, f,
						   MCTP_TRACE_KEY_INVALIDATED);
				rc = -EEXIST;
				key = NULL;
			} else {
				rc = mctp_frag_queue(key, skb);
			}
		}

	} else if (key) {
		/* this packet continues a previous message; reassemble
		 * using the message-specific key
		 */

		/* we need to be continuing an existing reassembly... */
		if (!key->reasm_head)
			rc = -EINVAL;
		else
			rc = mctp_frag_queue(key, skb);

		/* end of message? deliver to socket, and we're done with
		 * the reassembly/response key
		 */
		if (!rc && flags & MCTP_HDR_FLAG_EOM) {
			sock_queue_rcv_skb(key->sk, key->reasm_head);
			key->reasm_head = NULL;
			__mctp_key_done_in(key, net, f, MCTP_TRACE_KEY_REPLIED);
			key = NULL;
		}

	} else {
		/* not a start, no matching key */
		rc = -ENOENT;
	}

out_unlock:
	rcu_read_unlock();
	if (key) {
		spin_unlock_irqrestore(&key->lock, f);
		mctp_key_unref(key);
	}
	if (any_key)
		mctp_key_unref(any_key);
out:
	if (rc)
		kfree_skb(skb);
	return rc;
}

static unsigned int mctp_route_mtu(struct mctp_route *rt)
{
	return rt->mtu ?: READ_ONCE(rt->dev->dev->mtu);
}

static int mctp_route_output(struct mctp_route *route, struct sk_buff *skb)
{
	struct mctp_skb_cb *cb = mctp_cb(skb);
	struct mctp_hdr *hdr = mctp_hdr(skb);
	char daddr_buf[MAX_ADDR_LEN];
	char *daddr = NULL;
	unsigned int mtu;
	int rc;

	skb->protocol = htons(ETH_P_MCTP);

	mtu = READ_ONCE(skb->dev->mtu);
	if (skb->len > mtu) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	if (cb->ifindex) {
		/* direct route; use the hwaddr we stashed in sendmsg */
		if (cb->halen != skb->dev->addr_len) {
			/* sanity check, sendmsg should have already caught this */
			kfree_skb(skb);
			return -EMSGSIZE;
		}
		daddr = cb->haddr;
	} else {
		/* If lookup fails let the device handle daddr==NULL */
		if (mctp_neigh_lookup(route->dev, hdr->dest, daddr_buf) == 0)
			daddr = daddr_buf;
	}

	rc = dev_hard_header(skb, skb->dev, ntohs(skb->protocol),
			     daddr, skb->dev->dev_addr, skb->len);
	if (rc < 0) {
		kfree_skb(skb);
		return -EHOSTUNREACH;
	}

	mctp_flow_prepare_output(skb, route->dev);

	rc = dev_queue_xmit(skb);
	if (rc)
		rc = net_xmit_errno(rc);

	return rc;
}

/* route alloc/release */
static void mctp_route_release(struct mctp_route *rt)
{
	if (refcount_dec_and_test(&rt->refs)) {
		mctp_dev_put(rt->dev);
		kfree_rcu(rt, rcu);
	}
}

/* returns a route with the refcount at 1 */
static struct mctp_route *mctp_route_alloc(void)
{
	struct mctp_route *rt;

	rt = kzalloc(sizeof(*rt), GFP_KERNEL);
	if (!rt)
		return NULL;

	INIT_LIST_HEAD(&rt->list);
	refcount_set(&rt->refs, 1);
	rt->output = mctp_route_discard;

	return rt;
}

unsigned int mctp_default_net(struct net *net)
{
	return READ_ONCE(net->mctp.default_net);
}

int mctp_default_net_set(struct net *net, unsigned int index)
{
	if (index == 0)
		return -EINVAL;
	WRITE_ONCE(net->mctp.default_net, index);
	return 0;
}

/* tag management */
static void mctp_reserve_tag(struct net *net, struct mctp_sk_key *key,
			     struct mctp_sock *msk)
{
	struct netns_mctp *mns = &net->mctp;

	lockdep_assert_held(&mns->keys_lock);

	key->expiry = jiffies + mctp_key_lifetime;
	timer_reduce(&msk->key_expiry, key->expiry);

	/* we hold the net->key_lock here, allowing updates to both
	 * then net and sk
	 */
	hlist_add_head_rcu(&key->hlist, &mns->keys);
	hlist_add_head_rcu(&key->sklist, &msk->keys);
	refcount_inc(&key->refs);
}

/* Allocate a locally-owned tag value for (saddr, daddr), and reserve
 * it for the socket msk
 */
struct mctp_sk_key *mctp_alloc_local_tag(struct mctp_sock *msk,
					 mctp_eid_t daddr, mctp_eid_t saddr,
					 bool manual, u8 *tagp)
{
	struct net *net = sock_net(&msk->sk);
	struct netns_mctp *mns = &net->mctp;
	struct mctp_sk_key *key, *tmp;
	unsigned long flags;
	u8 tagbits;

	/* for NULL destination EIDs, we may get a response from any peer */
	if (daddr == MCTP_ADDR_NULL)
		daddr = MCTP_ADDR_ANY;

	/* be optimistic, alloc now */
	key = mctp_key_alloc(msk, saddr, daddr, 0, GFP_KERNEL);
	if (!key)
		return ERR_PTR(-ENOMEM);

	/* 8 possible tag values */
	tagbits = 0xff;

	spin_lock_irqsave(&mns->keys_lock, flags);

	/* Walk through the existing keys, looking for potential conflicting
	 * tags. If we find a conflict, clear that bit from tagbits
	 */
	hlist_for_each_entry(tmp, &mns->keys, hlist) {
		/* We can check the lookup fields (*_addr, tag) without the
		 * lock held, they don't change over the lifetime of the key.
		 */

		/* if we don't own the tag, it can't conflict */
		if (tmp->tag & MCTP_HDR_FLAG_TO)
			continue;

		if (!(mctp_address_matches(tmp->peer_addr, daddr) &&
		      mctp_address_matches(tmp->local_addr, saddr)))
			continue;

		spin_lock(&tmp->lock);
		/* key must still be valid. If we find a match, clear the
		 * potential tag value
		 */
		if (tmp->valid)
			tagbits &= ~(1 << tmp->tag);
		spin_unlock(&tmp->lock);

		if (!tagbits)
			break;
	}

	if (tagbits) {
		key->tag = __ffs(tagbits);
		mctp_reserve_tag(net, key, msk);
		trace_mctp_key_acquire(key);

		key->manual_alloc = manual;
		*tagp = key->tag;
	}

	spin_unlock_irqrestore(&mns->keys_lock, flags);

	if (!tagbits) {
		kfree(key);
		return ERR_PTR(-EBUSY);
	}

	return key;
}

static struct mctp_sk_key *mctp_lookup_prealloc_tag(struct mctp_sock *msk,
						    mctp_eid_t daddr,
						    u8 req_tag, u8 *tagp)
{
	struct net *net = sock_net(&msk->sk);
	struct netns_mctp *mns = &net->mctp;
	struct mctp_sk_key *key, *tmp;
	unsigned long flags;

	req_tag &= ~(MCTP_TAG_PREALLOC | MCTP_TAG_OWNER);
	key = NULL;

	spin_lock_irqsave(&mns->keys_lock, flags);

	hlist_for_each_entry(tmp, &mns->keys, hlist) {
		if (tmp->tag != req_tag)
			continue;

		if (!mctp_address_matches(tmp->peer_addr, daddr))
			continue;

		if (!tmp->manual_alloc)
			continue;

		spin_lock(&tmp->lock);
		if (tmp->valid) {
			key = tmp;
			refcount_inc(&key->refs);
			spin_unlock(&tmp->lock);
			break;
		}
		spin_unlock(&tmp->lock);
	}
	spin_unlock_irqrestore(&mns->keys_lock, flags);

	if (!key)
		return ERR_PTR(-ENOENT);

	if (tagp)
		*tagp = key->tag;

	return key;
}

/* routing lookups */
static bool mctp_rt_match_eid(struct mctp_route *rt,
			      unsigned int net, mctp_eid_t eid)
{
	return READ_ONCE(rt->dev->net) == net &&
		rt->min <= eid && rt->max >= eid;
}

/* compares match, used for duplicate prevention */
static bool mctp_rt_compare_exact(struct mctp_route *rt1,
				  struct mctp_route *rt2)
{
	ASSERT_RTNL();
	return rt1->dev->net == rt2->dev->net &&
		rt1->min == rt2->min &&
		rt1->max == rt2->max;
}

struct mctp_route *mctp_route_lookup(struct net *net, unsigned int dnet,
				     mctp_eid_t daddr)
{
	struct mctp_route *tmp, *rt = NULL;

	rcu_read_lock();

	list_for_each_entry_rcu(tmp, &net->mctp.routes, list) {
		/* TODO: add metrics */
		if (mctp_rt_match_eid(tmp, dnet, daddr)) {
			if (refcount_inc_not_zero(&tmp->refs)) {
				rt = tmp;
				break;
			}
		}
	}

	rcu_read_unlock();

	return rt;
}

static struct mctp_route *mctp_route_lookup_null(struct net *net,
						 struct net_device *dev)
{
	struct mctp_route *tmp, *rt = NULL;

	rcu_read_lock();

	list_for_each_entry_rcu(tmp, &net->mctp.routes, list) {
		if (tmp->dev->dev == dev && tmp->type == RTN_LOCAL &&
		    refcount_inc_not_zero(&tmp->refs)) {
			rt = tmp;
			break;
		}
	}

	rcu_read_unlock();

	return rt;
}

static int mctp_do_fragment_route(struct mctp_route *rt, struct sk_buff *skb,
				  unsigned int mtu, u8 tag)
{
	const unsigned int hlen = sizeof(struct mctp_hdr);
	struct mctp_hdr *hdr, *hdr2;
	unsigned int pos, size, headroom;
	struct sk_buff *skb2;
	int rc;
	u8 seq;

	hdr = mctp_hdr(skb);
	seq = 0;
	rc = 0;

	if (mtu < hlen + 1) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	/* keep same headroom as the original skb */
	headroom = skb_headroom(skb);

	/* we've got the header */
	skb_pull(skb, hlen);

	for (pos = 0; pos < skb->len;) {
		/* size of message payload */
		size = min(mtu - hlen, skb->len - pos);

		skb2 = alloc_skb(headroom + hlen + size, GFP_KERNEL);
		if (!skb2) {
			rc = -ENOMEM;
			break;
		}

		/* generic skb copy */
		skb2->protocol = skb->protocol;
		skb2->priority = skb->priority;
		skb2->dev = skb->dev;
		memcpy(skb2->cb, skb->cb, sizeof(skb2->cb));

		if (skb->sk)
			skb_set_owner_w(skb2, skb->sk);

		/* establish packet */
		skb_reserve(skb2, headroom);
		skb_reset_network_header(skb2);
		skb_put(skb2, hlen + size);
		skb2->transport_header = skb2->network_header + hlen;

		/* copy header fields, calculate SOM/EOM flags & seq */
		hdr2 = mctp_hdr(skb2);
		hdr2->ver = hdr->ver;
		hdr2->dest = hdr->dest;
		hdr2->src = hdr->src;
		hdr2->flags_seq_tag = tag &
			(MCTP_HDR_TAG_MASK | MCTP_HDR_FLAG_TO);

		if (pos == 0)
			hdr2->flags_seq_tag |= MCTP_HDR_FLAG_SOM;

		if (pos + size == skb->len)
			hdr2->flags_seq_tag |= MCTP_HDR_FLAG_EOM;

		hdr2->flags_seq_tag |= seq << MCTP_HDR_SEQ_SHIFT;

		/* copy message payload */
		skb_copy_bits(skb, pos, skb_transport_header(skb2), size);

		/* do route */
		rc = rt->output(rt, skb2);
		if (rc)
			break;

		seq = (seq + 1) & MCTP_HDR_SEQ_MASK;
		pos += size;
	}

	consume_skb(skb);
	return rc;
}

int mctp_local_output(struct sock *sk, struct mctp_route *rt,
		      struct sk_buff *skb, mctp_eid_t daddr, u8 req_tag)
{
	struct mctp_sock *msk = container_of(sk, struct mctp_sock, sk);
	struct mctp_skb_cb *cb = mctp_cb(skb);
	struct mctp_route tmp_rt = {0};
	struct mctp_sk_key *key;
	struct mctp_hdr *hdr;
	unsigned long flags;
	unsigned int mtu;
	mctp_eid_t saddr;
	bool ext_rt;
	int rc;
	u8 tag;

	rc = -ENODEV;

	if (rt) {
		ext_rt = false;
		if (WARN_ON(!rt->dev))
			goto out_release;

	} else if (cb->ifindex) {
		struct net_device *dev;

		ext_rt = true;
		rt = &tmp_rt;

		rcu_read_lock();
		dev = dev_get_by_index_rcu(sock_net(sk), cb->ifindex);
		if (!dev) {
			rcu_read_unlock();
			return rc;
		}
		rt->dev = __mctp_dev_get(dev);
		rcu_read_unlock();

		if (!rt->dev)
			goto out_release;

		/* establish temporary route - we set up enough to keep
		 * mctp_route_output happy
		 */
		rt->output = mctp_route_output;
		rt->mtu = 0;

	} else {
		return -EINVAL;
	}

	spin_lock_irqsave(&rt->dev->addrs_lock, flags);
	if (rt->dev->num_addrs == 0) {
		rc = -EHOSTUNREACH;
	} else {
		/* use the outbound interface's first address as our source */
		saddr = rt->dev->addrs[0];
		rc = 0;
	}
	spin_unlock_irqrestore(&rt->dev->addrs_lock, flags);

	if (rc)
		goto out_release;

	if (req_tag & MCTP_TAG_OWNER) {
		if (req_tag & MCTP_TAG_PREALLOC)
			key = mctp_lookup_prealloc_tag(msk, daddr,
						       req_tag, &tag);
		else
			key = mctp_alloc_local_tag(msk, daddr, saddr,
						   false, &tag);

		if (IS_ERR(key)) {
			rc = PTR_ERR(key);
			goto out_release;
		}
		mctp_skb_set_flow(skb, key);
		/* done with the key in this scope */
		mctp_key_unref(key);
		tag |= MCTP_HDR_FLAG_TO;
	} else {
		key = NULL;
		tag = req_tag & MCTP_TAG_MASK;
	}

	skb->protocol = htons(ETH_P_MCTP);
	skb->priority = 0;
	skb_reset_transport_header(skb);
	skb_push(skb, sizeof(struct mctp_hdr));
	skb_reset_network_header(skb);
	skb->dev = rt->dev->dev;

	/* cb->net will have been set on initial ingress */
	cb->src = saddr;

	/* set up common header fields */
	hdr = mctp_hdr(skb);
	hdr->ver = 1;
	hdr->dest = daddr;
	hdr->src = saddr;

	mtu = mctp_route_mtu(rt);

	if (skb->len + sizeof(struct mctp_hdr) <= mtu) {
		hdr->flags_seq_tag = MCTP_HDR_FLAG_SOM |
			MCTP_HDR_FLAG_EOM | tag;
		rc = rt->output(rt, skb);
	} else {
		rc = mctp_do_fragment_route(rt, skb, mtu, tag);
	}

out_release:
	if (!ext_rt)
		mctp_route_release(rt);

	mctp_dev_put(tmp_rt.dev);

	return rc;
}

/* route management */
static int mctp_route_add(struct mctp_dev *mdev, mctp_eid_t daddr_start,
			  unsigned int daddr_extent, unsigned int mtu,
			  unsigned char type)
{
	int (*rtfn)(struct mctp_route *rt, struct sk_buff *skb);
	struct net *net = dev_net(mdev->dev);
	struct mctp_route *rt, *ert;

	if (!mctp_address_unicast(daddr_start))
		return -EINVAL;

	if (daddr_extent > 0xff || daddr_start + daddr_extent >= 255)
		return -EINVAL;

	switch (type) {
	case RTN_LOCAL:
		rtfn = mctp_route_input;
		break;
	case RTN_UNICAST:
		rtfn = mctp_route_output;
		break;
	default:
		return -EINVAL;
	}

	rt = mctp_route_alloc();
	if (!rt)
		return -ENOMEM;

	rt->min = daddr_start;
	rt->max = daddr_start + daddr_extent;
	rt->mtu = mtu;
	rt->dev = mdev;
	mctp_dev_hold(rt->dev);
	rt->type = type;
	rt->output = rtfn;

	ASSERT_RTNL();
	/* Prevent duplicate identical routes. */
	list_for_each_entry(ert, &net->mctp.routes, list) {
		if (mctp_rt_compare_exact(rt, ert)) {
			mctp_route_release(rt);
			return -EEXIST;
		}
	}

	list_add_rcu(&rt->list, &net->mctp.routes);

	return 0;
}

static int mctp_route_remove(struct mctp_dev *mdev, mctp_eid_t daddr_start,
			     unsigned int daddr_extent, unsigned char type)
{
	struct net *net = dev_net(mdev->dev);
	struct mctp_route *rt, *tmp;
	mctp_eid_t daddr_end;
	bool dropped;

	if (daddr_extent > 0xff || daddr_start + daddr_extent >= 255)
		return -EINVAL;

	daddr_end = daddr_start + daddr_extent;
	dropped = false;

	ASSERT_RTNL();

	list_for_each_entry_safe(rt, tmp, &net->mctp.routes, list) {
		if (rt->dev == mdev &&
		    rt->min == daddr_start && rt->max == daddr_end &&
		    rt->type == type) {
			list_del_rcu(&rt->list);
			/* TODO: immediate RTM_DELROUTE */
			mctp_route_release(rt);
			dropped = true;
		}
	}

	return dropped ? 0 : -ENOENT;
}

int mctp_route_add_local(struct mctp_dev *mdev, mctp_eid_t addr)
{
	return mctp_route_add(mdev, addr, 0, 0, RTN_LOCAL);
}

int mctp_route_remove_local(struct mctp_dev *mdev, mctp_eid_t addr)
{
	return mctp_route_remove(mdev, addr, 0, RTN_LOCAL);
}

/* removes all entries for a given device */
void mctp_route_remove_dev(struct mctp_dev *mdev)
{
	struct net *net = dev_net(mdev->dev);
	struct mctp_route *rt, *tmp;

	ASSERT_RTNL();
	list_for_each_entry_safe(rt, tmp, &net->mctp.routes, list) {
		if (rt->dev == mdev) {
			list_del_rcu(&rt->list);
			/* TODO: immediate RTM_DELROUTE */
			mctp_route_release(rt);
		}
	}
}

/* Incoming packet-handling */

static int mctp_pkttype_receive(struct sk_buff *skb, struct net_device *dev,
				struct packet_type *pt,
				struct net_device *orig_dev)
{
	struct net *net = dev_net(dev);
	struct mctp_dev *mdev;
	struct mctp_skb_cb *cb;
	struct mctp_route *rt;
	struct mctp_hdr *mh;

	rcu_read_lock();
	mdev = __mctp_dev_get(dev);
	rcu_read_unlock();
	if (!mdev) {
		/* basic non-data sanity checks */
		goto err_drop;
	}

	if (!pskb_may_pull(skb, sizeof(struct mctp_hdr)))
		goto err_drop;

	skb_reset_transport_header(skb);
	skb_reset_network_header(skb);

	/* We have enough for a header; decode and route */
	mh = mctp_hdr(skb);
	if (mh->ver < MCTP_VER_MIN || mh->ver > MCTP_VER_MAX)
		goto err_drop;

	/* source must be valid unicast or null; drop reserved ranges and
	 * broadcast
	 */
	if (!(mctp_address_unicast(mh->src) || mctp_address_null(mh->src)))
		goto err_drop;

	/* dest address: as above, but allow broadcast */
	if (!(mctp_address_unicast(mh->dest) || mctp_address_null(mh->dest) ||
	      mctp_address_broadcast(mh->dest)))
		goto err_drop;

	/* MCTP drivers must populate halen/haddr */
	if (dev->type == ARPHRD_MCTP) {
		cb = mctp_cb(skb);
	} else {
		cb = __mctp_cb(skb);
		cb->halen = 0;
	}
	cb->net = READ_ONCE(mdev->net);
	cb->ifindex = dev->ifindex;

	rt = mctp_route_lookup(net, cb->net, mh->dest);

	/* NULL EID, but addressed to our physical address */
	if (!rt && mh->dest == MCTP_ADDR_NULL && skb->pkt_type == PACKET_HOST)
		rt = mctp_route_lookup_null(net, dev);

	if (!rt)
		goto err_drop;

	rt->output(rt, skb);
	mctp_route_release(rt);
	mctp_dev_put(mdev);

	return NET_RX_SUCCESS;

err_drop:
	kfree_skb(skb);
	mctp_dev_put(mdev);
	return NET_RX_DROP;
}

static struct packet_type mctp_packet_type = {
	.type = cpu_to_be16(ETH_P_MCTP),
	.func = mctp_pkttype_receive,
};

/* netlink interface */

static const struct nla_policy rta_mctp_policy[RTA_MAX + 1] = {
	[RTA_DST]		= { .type = NLA_U8 },
	[RTA_METRICS]		= { .type = NLA_NESTED },
	[RTA_OIF]		= { .type = NLA_U32 },
};

/* Common part for RTM_NEWROUTE and RTM_DELROUTE parsing.
 * tb must hold RTA_MAX+1 elements.
 */
static int mctp_route_nlparse(struct sk_buff *skb, struct nlmsghdr *nlh,
			      struct netlink_ext_ack *extack,
			      struct nlattr **tb, struct rtmsg **rtm,
			      struct mctp_dev **mdev, mctp_eid_t *daddr_start)
{
	struct net *net = sock_net(skb->sk);
	struct net_device *dev;
	unsigned int ifindex;
	int rc;

	rc = nlmsg_parse(nlh, sizeof(struct rtmsg), tb, RTA_MAX,
			 rta_mctp_policy, extack);
	if (rc < 0) {
		NL_SET_ERR_MSG(extack, "incorrect format");
		return rc;
	}

	if (!tb[RTA_DST]) {
		NL_SET_ERR_MSG(extack, "dst EID missing");
		return -EINVAL;
	}
	*daddr_start = nla_get_u8(tb[RTA_DST]);

	if (!tb[RTA_OIF]) {
		NL_SET_ERR_MSG(extack, "ifindex missing");
		return -EINVAL;
	}
	ifindex = nla_get_u32(tb[RTA_OIF]);

	*rtm = nlmsg_data(nlh);
	if ((*rtm)->rtm_family != AF_MCTP) {
		NL_SET_ERR_MSG(extack, "route family must be AF_MCTP");
		return -EINVAL;
	}

	dev = __dev_get_by_index(net, ifindex);
	if (!dev) {
		NL_SET_ERR_MSG(extack, "bad ifindex");
		return -ENODEV;
	}
	*mdev = mctp_dev_get_rtnl(dev);
	if (!*mdev)
		return -ENODEV;

	if (dev->flags & IFF_LOOPBACK) {
		NL_SET_ERR_MSG(extack, "no routes to loopback");
		return -EINVAL;
	}

	return 0;
}

static const struct nla_policy rta_metrics_policy[RTAX_MAX + 1] = {
	[RTAX_MTU]		= { .type = NLA_U32 },
};

static int mctp_newroute(struct sk_buff *skb, struct nlmsghdr *nlh,
			 struct netlink_ext_ack *extack)
{
	struct nlattr *tb[RTA_MAX + 1];
	struct nlattr *tbx[RTAX_MAX + 1];
	mctp_eid_t daddr_start;
	struct mctp_dev *mdev;
	struct rtmsg *rtm;
	unsigned int mtu;
	int rc;

	rc = mctp_route_nlparse(skb, nlh, extack, tb,
				&rtm, &mdev, &daddr_start);
	if (rc < 0)
		return rc;

	if (rtm->rtm_type != RTN_UNICAST) {
		NL_SET_ERR_MSG(extack, "rtm_type must be RTN_UNICAST");
		return -EINVAL;
	}

	mtu = 0;
	if (tb[RTA_METRICS]) {
		rc = nla_parse_nested(tbx, RTAX_MAX, tb[RTA_METRICS],
				      rta_metrics_policy, NULL);
		if (rc < 0)
			return rc;
		if (tbx[RTAX_MTU])
			mtu = nla_get_u32(tbx[RTAX_MTU]);
	}

	rc = mctp_route_add(mdev, daddr_start, rtm->rtm_dst_len, mtu,
			    rtm->rtm_type);
	return rc;
}

static int mctp_delroute(struct sk_buff *skb, struct nlmsghdr *nlh,
			 struct netlink_ext_ack *extack)
{
	struct nlattr *tb[RTA_MAX + 1];
	mctp_eid_t daddr_start;
	struct mctp_dev *mdev;
	struct rtmsg *rtm;
	int rc;

	rc = mctp_route_nlparse(skb, nlh, extack, tb,
				&rtm, &mdev, &daddr_start);
	if (rc < 0)
		return rc;

	/* we only have unicast routes */
	if (rtm->rtm_type != RTN_UNICAST)
		return -EINVAL;

	rc = mctp_route_remove(mdev, daddr_start, rtm->rtm_dst_len, RTN_UNICAST);
	return rc;
}

static int mctp_fill_rtinfo(struct sk_buff *skb, struct mctp_route *rt,
			    u32 portid, u32 seq, int event, unsigned int flags)
{
	struct nlmsghdr *nlh;
	struct rtmsg *hdr;
	void *metrics;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*hdr), flags);
	if (!nlh)
		return -EMSGSIZE;

	hdr = nlmsg_data(nlh);
	hdr->rtm_family = AF_MCTP;

	/* we use the _len fields as a number of EIDs, rather than
	 * a number of bits in the address
	 */
	hdr->rtm_dst_len = rt->max - rt->min;
	hdr->rtm_src_len = 0;
	hdr->rtm_tos = 0;
	hdr->rtm_table = RT_TABLE_DEFAULT;
	hdr->rtm_protocol = RTPROT_STATIC; /* everything is user-defined */
	hdr->rtm_scope = RT_SCOPE_LINK; /* TODO: scope in mctp_route? */
	hdr->rtm_type = rt->type;

	if (nla_put_u8(skb, RTA_DST, rt->min))
		goto cancel;

	metrics = nla_nest_start_noflag(skb, RTA_METRICS);
	if (!metrics)
		goto cancel;

	if (rt->mtu) {
		if (nla_put_u32(skb, RTAX_MTU, rt->mtu))
			goto cancel;
	}

	nla_nest_end(skb, metrics);

	if (rt->dev) {
		if (nla_put_u32(skb, RTA_OIF, rt->dev->dev->ifindex))
			goto cancel;
	}

	/* TODO: conditional neighbour physaddr? */

	nlmsg_end(skb, nlh);

	return 0;

cancel:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int mctp_dump_rtinfo(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct mctp_route *rt;
	int s_idx, idx;

	/* TODO: allow filtering on route data, possibly under
	 * cb->strict_check
	 */

	/* TODO: change to struct overlay */
	s_idx = cb->args[0];
	idx = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(rt, &net->mctp.routes, list) {
		if (idx++ < s_idx)
			continue;
		if (mctp_fill_rtinfo(skb, rt,
				     NETLINK_CB(cb->skb).portid,
				     cb->nlh->nlmsg_seq,
				     RTM_NEWROUTE, NLM_F_MULTI) < 0)
			break;
	}

	rcu_read_unlock();
	cb->args[0] = idx;

	return skb->len;
}

/* net namespace implementation */
static int __net_init mctp_routes_net_init(struct net *net)
{
	struct netns_mctp *ns = &net->mctp;

	INIT_LIST_HEAD(&ns->routes);
	INIT_HLIST_HEAD(&ns->binds);
	mutex_init(&ns->bind_lock);
	INIT_HLIST_HEAD(&ns->keys);
	spin_lock_init(&ns->keys_lock);
	WARN_ON(mctp_default_net_set(net, MCTP_INITIAL_DEFAULT_NET));
	return 0;
}

static void __net_exit mctp_routes_net_exit(struct net *net)
{
	struct mctp_route *rt;

	rcu_read_lock();
	list_for_each_entry_rcu(rt, &net->mctp.routes, list)
		mctp_route_release(rt);
	rcu_read_unlock();
}

static struct pernet_operations mctp_net_ops = {
	.init = mctp_routes_net_init,
	.exit = mctp_routes_net_exit,
};

int __init mctp_routes_init(void)
{
	dev_add_pack(&mctp_packet_type);

	rtnl_register_module(THIS_MODULE, PF_MCTP, RTM_GETROUTE,
			     NULL, mctp_dump_rtinfo, 0);
	rtnl_register_module(THIS_MODULE, PF_MCTP, RTM_NEWROUTE,
			     mctp_newroute, NULL, 0);
	rtnl_register_module(THIS_MODULE, PF_MCTP, RTM_DELROUTE,
			     mctp_delroute, NULL, 0);

	return register_pernet_subsys(&mctp_net_ops);
}

void mctp_routes_exit(void)
{
	unregister_pernet_subsys(&mctp_net_ops);
	rtnl_unregister(PF_MCTP, RTM_DELROUTE);
	rtnl_unregister(PF_MCTP, RTM_NEWROUTE);
	rtnl_unregister(PF_MCTP, RTM_GETROUTE);
	dev_remove_pack(&mctp_packet_type);
}

#if IS_ENABLED(CONFIG_MCTP_TEST)
#include "test/route-test.c"
#endif
