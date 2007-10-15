/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The IP fragmentation functionality.
 *
 * Version:	$Id: ip_fragment.c,v 1.59 2002/01/12 07:54:56 davem Exp $
 *
 * Authors:	Fred N. van Kempen <waltje@uWalt.NL.Mugnet.ORG>
 *		Alan Cox <Alan.Cox@linux.org>
 *
 * Fixes:
 *		Alan Cox	:	Split from ip.c , see ip_input.c for history.
 *		David S. Miller :	Begin massive cleanup...
 *		Andi Kleen	:	Add sysctls.
 *		xxxx		:	Overlapfrag bug.
 *		Ultima          :       ip_expire() kernel panic.
 *		Bill Hawes	:	Frag accounting and evictor fixes.
 *		John McDonald	:	0 length frag bug.
 *		Alexey Kuznetsov:	SMP races, threading, cleanup.
 *		Patrick McHardy :	LRU queue of frag heads for evictor.
 */

#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/jiffies.h>
#include <linux/skbuff.h>
#include <linux/list.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/netdevice.h>
#include <linux/jhash.h>
#include <linux/random.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/checksum.h>
#include <net/inetpeer.h>
#include <net/inet_frag.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/inet.h>
#include <linux/netfilter_ipv4.h>

/* NOTE. Logic of IP defragmentation is parallel to corresponding IPv6
 * code now. If you change something here, _PLEASE_ update ipv6/reassembly.c
 * as well. Or notify me, at least. --ANK
 */

int sysctl_ipfrag_max_dist __read_mostly = 64;

struct ipfrag_skb_cb
{
	struct inet_skb_parm	h;
	int			offset;
};

#define FRAG_CB(skb)	((struct ipfrag_skb_cb*)((skb)->cb))

/* Describe an entry in the "incomplete datagrams" queue. */
struct ipq {
	struct inet_frag_queue q;

	u32		user;
	__be32		saddr;
	__be32		daddr;
	__be16		id;
	u8		protocol;
	int             iif;
	unsigned int    rid;
	struct inet_peer *peer;
};

struct inet_frags_ctl ip4_frags_ctl __read_mostly = {
	/*
	 * Fragment cache limits. We will commit 256K at one time. Should we
	 * cross that limit we will prune down to 192K. This should cope with
	 * even the most extreme cases without allowing an attacker to
	 * measurably harm machine performance.
	 */
	.high_thresh	 = 256 * 1024,
	.low_thresh	 = 192 * 1024,

	/*
	 * Important NOTE! Fragment queue must be destroyed before MSL expires.
	 * RFC791 is wrong proposing to prolongate timer each fragment arrival
	 * by TTL.
	 */
	.timeout	 = IP_FRAG_TIME,
	.secret_interval = 10 * 60 * HZ,
};

static struct inet_frags ip4_frags;

int ip_frag_nqueues(void)
{
	return ip4_frags.nqueues;
}

int ip_frag_mem(void)
{
	return atomic_read(&ip4_frags.mem);
}

static int ip_frag_reasm(struct ipq *qp, struct sk_buff *prev,
			 struct net_device *dev);

static unsigned int ipqhashfn(__be16 id, __be32 saddr, __be32 daddr, u8 prot)
{
	return jhash_3words((__force u32)id << 16 | prot,
			    (__force u32)saddr, (__force u32)daddr,
			    ip4_frags.rnd) & (INETFRAGS_HASHSZ - 1);
}

static unsigned int ip4_hashfn(struct inet_frag_queue *q)
{
	struct ipq *ipq;

	ipq = container_of(q, struct ipq, q);
	return ipqhashfn(ipq->id, ipq->saddr, ipq->daddr, ipq->protocol);
}

/* Memory Tracking Functions. */
static __inline__ void frag_kfree_skb(struct sk_buff *skb, int *work)
{
	if (work)
		*work -= skb->truesize;
	atomic_sub(skb->truesize, &ip4_frags.mem);
	kfree_skb(skb);
}

static __inline__ void ip4_frag_free(struct inet_frag_queue *q)
{
	struct ipq *qp;

	qp = container_of(q, struct ipq, q);
	if (qp->peer)
		inet_putpeer(qp->peer);
	kfree(qp);
}

static __inline__ struct ipq *frag_alloc_queue(void)
{
	struct ipq *qp = kzalloc(sizeof(struct ipq), GFP_ATOMIC);

	if (!qp)
		return NULL;
	atomic_add(sizeof(struct ipq), &ip4_frags.mem);
	return qp;
}


/* Destruction primitives. */

static __inline__ void ipq_put(struct ipq *ipq)
{
	inet_frag_put(&ipq->q, &ip4_frags);
}

/* Kill ipq entry. It is not destroyed immediately,
 * because caller (and someone more) holds reference count.
 */
static void ipq_kill(struct ipq *ipq)
{
	inet_frag_kill(&ipq->q, &ip4_frags);
}

/* Memory limiting on fragments.  Evictor trashes the oldest
 * fragment queue until we are back under the threshold.
 */
static void ip_evictor(void)
{
	int evicted;

	evicted = inet_frag_evictor(&ip4_frags);
	if (evicted)
		IP_ADD_STATS_BH(IPSTATS_MIB_REASMFAILS, evicted);
}

/*
 * Oops, a fragment queue timed out.  Kill it and send an ICMP reply.
 */
static void ip_expire(unsigned long arg)
{
	struct ipq *qp = (struct ipq *) arg;

	spin_lock(&qp->q.lock);

	if (qp->q.last_in & COMPLETE)
		goto out;

	ipq_kill(qp);

	IP_INC_STATS_BH(IPSTATS_MIB_REASMTIMEOUT);
	IP_INC_STATS_BH(IPSTATS_MIB_REASMFAILS);

	if ((qp->q.last_in&FIRST_IN) && qp->q.fragments != NULL) {
		struct sk_buff *head = qp->q.fragments;
		/* Send an ICMP "Fragment Reassembly Timeout" message. */
		if ((head->dev = dev_get_by_index(&init_net, qp->iif)) != NULL) {
			icmp_send(head, ICMP_TIME_EXCEEDED, ICMP_EXC_FRAGTIME, 0);
			dev_put(head->dev);
		}
	}
out:
	spin_unlock(&qp->q.lock);
	ipq_put(qp);
}

/* Creation primitives. */

static struct ipq *ip_frag_intern(struct ipq *qp_in)
{
	struct ipq *qp;
#ifdef CONFIG_SMP
	struct hlist_node *n;
#endif
	unsigned int hash;

	write_lock(&ip4_frags.lock);
	hash = ipqhashfn(qp_in->id, qp_in->saddr, qp_in->daddr,
			 qp_in->protocol);
#ifdef CONFIG_SMP
	/* With SMP race we have to recheck hash table, because
	 * such entry could be created on other cpu, while we
	 * promoted read lock to write lock.
	 */
	hlist_for_each_entry(qp, n, &ip4_frags.hash[hash], q.list) {
		if (qp->id == qp_in->id		&&
		    qp->saddr == qp_in->saddr	&&
		    qp->daddr == qp_in->daddr	&&
		    qp->protocol == qp_in->protocol &&
		    qp->user == qp_in->user) {
			atomic_inc(&qp->q.refcnt);
			write_unlock(&ip4_frags.lock);
			qp_in->q.last_in |= COMPLETE;
			ipq_put(qp_in);
			return qp;
		}
	}
#endif
	qp = qp_in;

	if (!mod_timer(&qp->q.timer, jiffies + ip4_frags_ctl.timeout))
		atomic_inc(&qp->q.refcnt);

	atomic_inc(&qp->q.refcnt);
	hlist_add_head(&qp->q.list, &ip4_frags.hash[hash]);
	INIT_LIST_HEAD(&qp->q.lru_list);
	list_add_tail(&qp->q.lru_list, &ip4_frags.lru_list);
	ip4_frags.nqueues++;
	write_unlock(&ip4_frags.lock);
	return qp;
}

/* Add an entry to the 'ipq' queue for a newly received IP datagram. */
static struct ipq *ip_frag_create(struct iphdr *iph, u32 user)
{
	struct ipq *qp;

	if ((qp = frag_alloc_queue()) == NULL)
		goto out_nomem;

	qp->protocol = iph->protocol;
	qp->id = iph->id;
	qp->saddr = iph->saddr;
	qp->daddr = iph->daddr;
	qp->user = user;
	qp->peer = sysctl_ipfrag_max_dist ? inet_getpeer(iph->saddr, 1) : NULL;

	/* Initialize a timer for this entry. */
	init_timer(&qp->q.timer);
	qp->q.timer.data = (unsigned long) qp;	/* pointer to queue	*/
	qp->q.timer.function = ip_expire;		/* expire function	*/
	spin_lock_init(&qp->q.lock);
	atomic_set(&qp->q.refcnt, 1);

	return ip_frag_intern(qp);

out_nomem:
	LIMIT_NETDEBUG(KERN_ERR "ip_frag_create: no memory left !\n");
	return NULL;
}

/* Find the correct entry in the "incomplete datagrams" queue for
 * this IP datagram, and create new one, if nothing is found.
 */
static inline struct ipq *ip_find(struct iphdr *iph, u32 user)
{
	__be16 id = iph->id;
	__be32 saddr = iph->saddr;
	__be32 daddr = iph->daddr;
	__u8 protocol = iph->protocol;
	unsigned int hash;
	struct ipq *qp;
	struct hlist_node *n;

	read_lock(&ip4_frags.lock);
	hash = ipqhashfn(id, saddr, daddr, protocol);
	hlist_for_each_entry(qp, n, &ip4_frags.hash[hash], q.list) {
		if (qp->id == id		&&
		    qp->saddr == saddr	&&
		    qp->daddr == daddr	&&
		    qp->protocol == protocol &&
		    qp->user == user) {
			atomic_inc(&qp->q.refcnt);
			read_unlock(&ip4_frags.lock);
			return qp;
		}
	}
	read_unlock(&ip4_frags.lock);

	return ip_frag_create(iph, user);
}

/* Is the fragment too far ahead to be part of ipq? */
static inline int ip_frag_too_far(struct ipq *qp)
{
	struct inet_peer *peer = qp->peer;
	unsigned int max = sysctl_ipfrag_max_dist;
	unsigned int start, end;

	int rc;

	if (!peer || !max)
		return 0;

	start = qp->rid;
	end = atomic_inc_return(&peer->rid);
	qp->rid = end;

	rc = qp->q.fragments && (end - start) > max;

	if (rc) {
		IP_INC_STATS_BH(IPSTATS_MIB_REASMFAILS);
	}

	return rc;
}

static int ip_frag_reinit(struct ipq *qp)
{
	struct sk_buff *fp;

	if (!mod_timer(&qp->q.timer, jiffies + ip4_frags_ctl.timeout)) {
		atomic_inc(&qp->q.refcnt);
		return -ETIMEDOUT;
	}

	fp = qp->q.fragments;
	do {
		struct sk_buff *xp = fp->next;
		frag_kfree_skb(fp, NULL);
		fp = xp;
	} while (fp);

	qp->q.last_in = 0;
	qp->q.len = 0;
	qp->q.meat = 0;
	qp->q.fragments = NULL;
	qp->iif = 0;

	return 0;
}

/* Add new segment to existing queue. */
static int ip_frag_queue(struct ipq *qp, struct sk_buff *skb)
{
	struct sk_buff *prev, *next;
	struct net_device *dev;
	int flags, offset;
	int ihl, end;
	int err = -ENOENT;

	if (qp->q.last_in & COMPLETE)
		goto err;

	if (!(IPCB(skb)->flags & IPSKB_FRAG_COMPLETE) &&
	    unlikely(ip_frag_too_far(qp)) &&
	    unlikely(err = ip_frag_reinit(qp))) {
		ipq_kill(qp);
		goto err;
	}

	offset = ntohs(ip_hdr(skb)->frag_off);
	flags = offset & ~IP_OFFSET;
	offset &= IP_OFFSET;
	offset <<= 3;		/* offset is in 8-byte chunks */
	ihl = ip_hdrlen(skb);

	/* Determine the position of this fragment. */
	end = offset + skb->len - ihl;
	err = -EINVAL;

	/* Is this the final fragment? */
	if ((flags & IP_MF) == 0) {
		/* If we already have some bits beyond end
		 * or have different end, the segment is corrrupted.
		 */
		if (end < qp->q.len ||
		    ((qp->q.last_in & LAST_IN) && end != qp->q.len))
			goto err;
		qp->q.last_in |= LAST_IN;
		qp->q.len = end;
	} else {
		if (end&7) {
			end &= ~7;
			if (skb->ip_summed != CHECKSUM_UNNECESSARY)
				skb->ip_summed = CHECKSUM_NONE;
		}
		if (end > qp->q.len) {
			/* Some bits beyond end -> corruption. */
			if (qp->q.last_in & LAST_IN)
				goto err;
			qp->q.len = end;
		}
	}
	if (end == offset)
		goto err;

	err = -ENOMEM;
	if (pskb_pull(skb, ihl) == NULL)
		goto err;

	err = pskb_trim_rcsum(skb, end - offset);
	if (err)
		goto err;

	/* Find out which fragments are in front and at the back of us
	 * in the chain of fragments so far.  We must know where to put
	 * this fragment, right?
	 */
	prev = NULL;
	for (next = qp->q.fragments; next != NULL; next = next->next) {
		if (FRAG_CB(next)->offset >= offset)
			break;	/* bingo! */
		prev = next;
	}

	/* We found where to put this one.  Check for overlap with
	 * preceding fragment, and, if needed, align things so that
	 * any overlaps are eliminated.
	 */
	if (prev) {
		int i = (FRAG_CB(prev)->offset + prev->len) - offset;

		if (i > 0) {
			offset += i;
			err = -EINVAL;
			if (end <= offset)
				goto err;
			err = -ENOMEM;
			if (!pskb_pull(skb, i))
				goto err;
			if (skb->ip_summed != CHECKSUM_UNNECESSARY)
				skb->ip_summed = CHECKSUM_NONE;
		}
	}

	err = -ENOMEM;

	while (next && FRAG_CB(next)->offset < end) {
		int i = end - FRAG_CB(next)->offset; /* overlap is 'i' bytes */

		if (i < next->len) {
			/* Eat head of the next overlapped fragment
			 * and leave the loop. The next ones cannot overlap.
			 */
			if (!pskb_pull(next, i))
				goto err;
			FRAG_CB(next)->offset += i;
			qp->q.meat -= i;
			if (next->ip_summed != CHECKSUM_UNNECESSARY)
				next->ip_summed = CHECKSUM_NONE;
			break;
		} else {
			struct sk_buff *free_it = next;

			/* Old fragment is completely overridden with
			 * new one drop it.
			 */
			next = next->next;

			if (prev)
				prev->next = next;
			else
				qp->q.fragments = next;

			qp->q.meat -= free_it->len;
			frag_kfree_skb(free_it, NULL);
		}
	}

	FRAG_CB(skb)->offset = offset;

	/* Insert this fragment in the chain of fragments. */
	skb->next = next;
	if (prev)
		prev->next = skb;
	else
		qp->q.fragments = skb;

	dev = skb->dev;
	if (dev) {
		qp->iif = dev->ifindex;
		skb->dev = NULL;
	}
	qp->q.stamp = skb->tstamp;
	qp->q.meat += skb->len;
	atomic_add(skb->truesize, &ip4_frags.mem);
	if (offset == 0)
		qp->q.last_in |= FIRST_IN;

	if (qp->q.last_in == (FIRST_IN | LAST_IN) && qp->q.meat == qp->q.len)
		return ip_frag_reasm(qp, prev, dev);

	write_lock(&ip4_frags.lock);
	list_move_tail(&qp->q.lru_list, &ip4_frags.lru_list);
	write_unlock(&ip4_frags.lock);
	return -EINPROGRESS;

err:
	kfree_skb(skb);
	return err;
}


/* Build a new IP datagram from all its fragments. */

static int ip_frag_reasm(struct ipq *qp, struct sk_buff *prev,
			 struct net_device *dev)
{
	struct iphdr *iph;
	struct sk_buff *fp, *head = qp->q.fragments;
	int len;
	int ihlen;
	int err;

	ipq_kill(qp);

	/* Make the one we just received the head. */
	if (prev) {
		head = prev->next;
		fp = skb_clone(head, GFP_ATOMIC);

		if (!fp)
			goto out_nomem;

		fp->next = head->next;
		prev->next = fp;

		skb_morph(head, qp->q.fragments);
		head->next = qp->q.fragments->next;

		kfree_skb(qp->q.fragments);
		qp->q.fragments = head;
	}

	BUG_TRAP(head != NULL);
	BUG_TRAP(FRAG_CB(head)->offset == 0);

	/* Allocate a new buffer for the datagram. */
	ihlen = ip_hdrlen(head);
	len = ihlen + qp->q.len;

	err = -E2BIG;
	if (len > 65535)
		goto out_oversize;

	/* Head of list must not be cloned. */
	err = -ENOMEM;
	if (skb_cloned(head) && pskb_expand_head(head, 0, 0, GFP_ATOMIC))
		goto out_nomem;

	/* If the first fragment is fragmented itself, we split
	 * it to two chunks: the first with data and paged part
	 * and the second, holding only fragments. */
	if (skb_shinfo(head)->frag_list) {
		struct sk_buff *clone;
		int i, plen = 0;

		if ((clone = alloc_skb(0, GFP_ATOMIC)) == NULL)
			goto out_nomem;
		clone->next = head->next;
		head->next = clone;
		skb_shinfo(clone)->frag_list = skb_shinfo(head)->frag_list;
		skb_shinfo(head)->frag_list = NULL;
		for (i=0; i<skb_shinfo(head)->nr_frags; i++)
			plen += skb_shinfo(head)->frags[i].size;
		clone->len = clone->data_len = head->data_len - plen;
		head->data_len -= clone->len;
		head->len -= clone->len;
		clone->csum = 0;
		clone->ip_summed = head->ip_summed;
		atomic_add(clone->truesize, &ip4_frags.mem);
	}

	skb_shinfo(head)->frag_list = head->next;
	skb_push(head, head->data - skb_network_header(head));
	atomic_sub(head->truesize, &ip4_frags.mem);

	for (fp=head->next; fp; fp = fp->next) {
		head->data_len += fp->len;
		head->len += fp->len;
		if (head->ip_summed != fp->ip_summed)
			head->ip_summed = CHECKSUM_NONE;
		else if (head->ip_summed == CHECKSUM_COMPLETE)
			head->csum = csum_add(head->csum, fp->csum);
		head->truesize += fp->truesize;
		atomic_sub(fp->truesize, &ip4_frags.mem);
	}

	head->next = NULL;
	head->dev = dev;
	head->tstamp = qp->q.stamp;

	iph = ip_hdr(head);
	iph->frag_off = 0;
	iph->tot_len = htons(len);
	IP_INC_STATS_BH(IPSTATS_MIB_REASMOKS);
	qp->q.fragments = NULL;
	return 0;

out_nomem:
	LIMIT_NETDEBUG(KERN_ERR "IP: queue_glue: no memory for gluing "
			      "queue %p\n", qp);
	goto out_fail;
out_oversize:
	if (net_ratelimit())
		printk(KERN_INFO
			"Oversized IP packet from %d.%d.%d.%d.\n",
			NIPQUAD(qp->saddr));
out_fail:
	IP_INC_STATS_BH(IPSTATS_MIB_REASMFAILS);
	return err;
}

/* Process an incoming IP datagram fragment. */
int ip_defrag(struct sk_buff *skb, u32 user)
{
	struct ipq *qp;

	IP_INC_STATS_BH(IPSTATS_MIB_REASMREQDS);

	/* Start by cleaning up the memory. */
	if (atomic_read(&ip4_frags.mem) > ip4_frags_ctl.high_thresh)
		ip_evictor();

	/* Lookup (or create) queue header */
	if ((qp = ip_find(ip_hdr(skb), user)) != NULL) {
		int ret;

		spin_lock(&qp->q.lock);

		ret = ip_frag_queue(qp, skb);

		spin_unlock(&qp->q.lock);
		ipq_put(qp);
		return ret;
	}

	IP_INC_STATS_BH(IPSTATS_MIB_REASMFAILS);
	kfree_skb(skb);
	return -ENOMEM;
}

void __init ipfrag_init(void)
{
	ip4_frags.ctl = &ip4_frags_ctl;
	ip4_frags.hashfn = ip4_hashfn;
	ip4_frags.destructor = ip4_frag_free;
	ip4_frags.skb_free = NULL;
	ip4_frags.qsize = sizeof(struct ipq);
	inet_frags_init(&ip4_frags);
}

EXPORT_SYMBOL(ip_defrag);
