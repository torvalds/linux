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

#include <linux/config.h>
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
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/inet.h>
#include <linux/netfilter_ipv4.h>

/* NOTE. Logic of IP defragmentation is parallel to corresponding IPv6
 * code now. If you change something here, _PLEASE_ update ipv6/reassembly.c
 * as well. Or notify me, at least. --ANK
 */

/* Fragment cache limits. We will commit 256K at one time. Should we
 * cross that limit we will prune down to 192K. This should cope with
 * even the most extreme cases without allowing an attacker to measurably
 * harm machine performance.
 */
int sysctl_ipfrag_high_thresh = 256*1024;
int sysctl_ipfrag_low_thresh = 192*1024;

/* Important NOTE! Fragment queue must be destroyed before MSL expires.
 * RFC791 is wrong proposing to prolongate timer each fragment arrival by TTL.
 */
int sysctl_ipfrag_time = IP_FRAG_TIME;

struct ipfrag_skb_cb
{
	struct inet_skb_parm	h;
	int			offset;
};

#define FRAG_CB(skb)	((struct ipfrag_skb_cb*)((skb)->cb))

/* Describe an entry in the "incomplete datagrams" queue. */
struct ipq {
	struct ipq	*next;		/* linked list pointers			*/
	struct list_head lru_list;	/* lru list member 			*/
	u32		user;
	u32		saddr;
	u32		daddr;
	u16		id;
	u8		protocol;
	u8		last_in;
#define COMPLETE		4
#define FIRST_IN		2
#define LAST_IN			1

	struct sk_buff	*fragments;	/* linked list of received fragments	*/
	int		len;		/* total length of original datagram	*/
	int		meat;
	spinlock_t	lock;
	atomic_t	refcnt;
	struct timer_list timer;	/* when will this queue expire?		*/
	struct ipq	**pprev;
	int		iif;
	struct timeval	stamp;
};

/* Hash table. */

#define IPQ_HASHSZ	64

/* Per-bucket lock is easy to add now. */
static struct ipq *ipq_hash[IPQ_HASHSZ];
static DEFINE_RWLOCK(ipfrag_lock);
static u32 ipfrag_hash_rnd;
static LIST_HEAD(ipq_lru_list);
int ip_frag_nqueues = 0;

static __inline__ void __ipq_unlink(struct ipq *qp)
{
	if(qp->next)
		qp->next->pprev = qp->pprev;
	*qp->pprev = qp->next;
	list_del(&qp->lru_list);
	ip_frag_nqueues--;
}

static __inline__ void ipq_unlink(struct ipq *ipq)
{
	write_lock(&ipfrag_lock);
	__ipq_unlink(ipq);
	write_unlock(&ipfrag_lock);
}

static unsigned int ipqhashfn(u16 id, u32 saddr, u32 daddr, u8 prot)
{
	return jhash_3words((u32)id << 16 | prot, saddr, daddr,
			    ipfrag_hash_rnd) & (IPQ_HASHSZ - 1);
}

static struct timer_list ipfrag_secret_timer;
int sysctl_ipfrag_secret_interval = 10 * 60 * HZ;

static void ipfrag_secret_rebuild(unsigned long dummy)
{
	unsigned long now = jiffies;
	int i;

	write_lock(&ipfrag_lock);
	get_random_bytes(&ipfrag_hash_rnd, sizeof(u32));
	for (i = 0; i < IPQ_HASHSZ; i++) {
		struct ipq *q;

		q = ipq_hash[i];
		while (q) {
			struct ipq *next = q->next;
			unsigned int hval = ipqhashfn(q->id, q->saddr,
						      q->daddr, q->protocol);

			if (hval != i) {
				/* Unlink. */
				if (q->next)
					q->next->pprev = q->pprev;
				*q->pprev = q->next;

				/* Relink to new hash chain. */
				if ((q->next = ipq_hash[hval]) != NULL)
					q->next->pprev = &q->next;
				ipq_hash[hval] = q;
				q->pprev = &ipq_hash[hval];
			}

			q = next;
		}
	}
	write_unlock(&ipfrag_lock);

	mod_timer(&ipfrag_secret_timer, now + sysctl_ipfrag_secret_interval);
}

atomic_t ip_frag_mem = ATOMIC_INIT(0);	/* Memory used for fragments */

/* Memory Tracking Functions. */
static __inline__ void frag_kfree_skb(struct sk_buff *skb, int *work)
{
	if (work)
		*work -= skb->truesize;
	atomic_sub(skb->truesize, &ip_frag_mem);
	kfree_skb(skb);
}

static __inline__ void frag_free_queue(struct ipq *qp, int *work)
{
	if (work)
		*work -= sizeof(struct ipq);
	atomic_sub(sizeof(struct ipq), &ip_frag_mem);
	kfree(qp);
}

static __inline__ struct ipq *frag_alloc_queue(void)
{
	struct ipq *qp = kmalloc(sizeof(struct ipq), GFP_ATOMIC);

	if(!qp)
		return NULL;
	atomic_add(sizeof(struct ipq), &ip_frag_mem);
	return qp;
}


/* Destruction primitives. */

/* Complete destruction of ipq. */
static void ip_frag_destroy(struct ipq *qp, int *work)
{
	struct sk_buff *fp;

	BUG_TRAP(qp->last_in&COMPLETE);
	BUG_TRAP(del_timer(&qp->timer) == 0);

	/* Release all fragment data. */
	fp = qp->fragments;
	while (fp) {
		struct sk_buff *xp = fp->next;

		frag_kfree_skb(fp, work);
		fp = xp;
	}

	/* Finally, release the queue descriptor itself. */
	frag_free_queue(qp, work);
}

static __inline__ void ipq_put(struct ipq *ipq, int *work)
{
	if (atomic_dec_and_test(&ipq->refcnt))
		ip_frag_destroy(ipq, work);
}

/* Kill ipq entry. It is not destroyed immediately,
 * because caller (and someone more) holds reference count.
 */
static void ipq_kill(struct ipq *ipq)
{
	if (del_timer(&ipq->timer))
		atomic_dec(&ipq->refcnt);

	if (!(ipq->last_in & COMPLETE)) {
		ipq_unlink(ipq);
		atomic_dec(&ipq->refcnt);
		ipq->last_in |= COMPLETE;
	}
}

/* Memory limiting on fragments.  Evictor trashes the oldest 
 * fragment queue until we are back under the threshold.
 */
static void ip_evictor(void)
{
	struct ipq *qp;
	struct list_head *tmp;
	int work;

	work = atomic_read(&ip_frag_mem) - sysctl_ipfrag_low_thresh;
	if (work <= 0)
		return;

	while (work > 0) {
		read_lock(&ipfrag_lock);
		if (list_empty(&ipq_lru_list)) {
			read_unlock(&ipfrag_lock);
			return;
		}
		tmp = ipq_lru_list.next;
		qp = list_entry(tmp, struct ipq, lru_list);
		atomic_inc(&qp->refcnt);
		read_unlock(&ipfrag_lock);

		spin_lock(&qp->lock);
		if (!(qp->last_in&COMPLETE))
			ipq_kill(qp);
		spin_unlock(&qp->lock);

		ipq_put(qp, &work);
		IP_INC_STATS_BH(IPSTATS_MIB_REASMFAILS);
	}
}

/*
 * Oops, a fragment queue timed out.  Kill it and send an ICMP reply.
 */
static void ip_expire(unsigned long arg)
{
	struct ipq *qp = (struct ipq *) arg;

	spin_lock(&qp->lock);

	if (qp->last_in & COMPLETE)
		goto out;

	ipq_kill(qp);

	IP_INC_STATS_BH(IPSTATS_MIB_REASMTIMEOUT);
	IP_INC_STATS_BH(IPSTATS_MIB_REASMFAILS);

	if ((qp->last_in&FIRST_IN) && qp->fragments != NULL) {
		struct sk_buff *head = qp->fragments;
		/* Send an ICMP "Fragment Reassembly Timeout" message. */
		if ((head->dev = dev_get_by_index(qp->iif)) != NULL) {
			icmp_send(head, ICMP_TIME_EXCEEDED, ICMP_EXC_FRAGTIME, 0);
			dev_put(head->dev);
		}
	}
out:
	spin_unlock(&qp->lock);
	ipq_put(qp, NULL);
}

/* Creation primitives. */

static struct ipq *ip_frag_intern(unsigned int hash, struct ipq *qp_in)
{
	struct ipq *qp;

	write_lock(&ipfrag_lock);
#ifdef CONFIG_SMP
	/* With SMP race we have to recheck hash table, because
	 * such entry could be created on other cpu, while we
	 * promoted read lock to write lock.
	 */
	for(qp = ipq_hash[hash]; qp; qp = qp->next) {
		if(qp->id == qp_in->id		&&
		   qp->saddr == qp_in->saddr	&&
		   qp->daddr == qp_in->daddr	&&
		   qp->protocol == qp_in->protocol &&
		   qp->user == qp_in->user) {
			atomic_inc(&qp->refcnt);
			write_unlock(&ipfrag_lock);
			qp_in->last_in |= COMPLETE;
			ipq_put(qp_in, NULL);
			return qp;
		}
	}
#endif
	qp = qp_in;

	if (!mod_timer(&qp->timer, jiffies + sysctl_ipfrag_time))
		atomic_inc(&qp->refcnt);

	atomic_inc(&qp->refcnt);
	if((qp->next = ipq_hash[hash]) != NULL)
		qp->next->pprev = &qp->next;
	ipq_hash[hash] = qp;
	qp->pprev = &ipq_hash[hash];
	INIT_LIST_HEAD(&qp->lru_list);
	list_add_tail(&qp->lru_list, &ipq_lru_list);
	ip_frag_nqueues++;
	write_unlock(&ipfrag_lock);
	return qp;
}

/* Add an entry to the 'ipq' queue for a newly received IP datagram. */
static struct ipq *ip_frag_create(unsigned hash, struct iphdr *iph, u32 user)
{
	struct ipq *qp;

	if ((qp = frag_alloc_queue()) == NULL)
		goto out_nomem;

	qp->protocol = iph->protocol;
	qp->last_in = 0;
	qp->id = iph->id;
	qp->saddr = iph->saddr;
	qp->daddr = iph->daddr;
	qp->user = user;
	qp->len = 0;
	qp->meat = 0;
	qp->fragments = NULL;
	qp->iif = 0;

	/* Initialize a timer for this entry. */
	init_timer(&qp->timer);
	qp->timer.data = (unsigned long) qp;	/* pointer to queue	*/
	qp->timer.function = ip_expire;		/* expire function	*/
	spin_lock_init(&qp->lock);
	atomic_set(&qp->refcnt, 1);

	return ip_frag_intern(hash, qp);

out_nomem:
	LIMIT_NETDEBUG(KERN_ERR "ip_frag_create: no memory left !\n");
	return NULL;
}

/* Find the correct entry in the "incomplete datagrams" queue for
 * this IP datagram, and create new one, if nothing is found.
 */
static inline struct ipq *ip_find(struct iphdr *iph, u32 user)
{
	__u16 id = iph->id;
	__u32 saddr = iph->saddr;
	__u32 daddr = iph->daddr;
	__u8 protocol = iph->protocol;
	unsigned int hash = ipqhashfn(id, saddr, daddr, protocol);
	struct ipq *qp;

	read_lock(&ipfrag_lock);
	for(qp = ipq_hash[hash]; qp; qp = qp->next) {
		if(qp->id == id		&&
		   qp->saddr == saddr	&&
		   qp->daddr == daddr	&&
		   qp->protocol == protocol &&
		   qp->user == user) {
			atomic_inc(&qp->refcnt);
			read_unlock(&ipfrag_lock);
			return qp;
		}
	}
	read_unlock(&ipfrag_lock);

	return ip_frag_create(hash, iph, user);
}

/* Add new segment to existing queue. */
static void ip_frag_queue(struct ipq *qp, struct sk_buff *skb)
{
	struct sk_buff *prev, *next;
	int flags, offset;
	int ihl, end;

	if (qp->last_in & COMPLETE)
		goto err;

 	offset = ntohs(skb->nh.iph->frag_off);
	flags = offset & ~IP_OFFSET;
	offset &= IP_OFFSET;
	offset <<= 3;		/* offset is in 8-byte chunks */
 	ihl = skb->nh.iph->ihl * 4;

	/* Determine the position of this fragment. */
 	end = offset + skb->len - ihl;

	/* Is this the final fragment? */
	if ((flags & IP_MF) == 0) {
		/* If we already have some bits beyond end
		 * or have different end, the segment is corrrupted.
		 */
		if (end < qp->len ||
		    ((qp->last_in & LAST_IN) && end != qp->len))
			goto err;
		qp->last_in |= LAST_IN;
		qp->len = end;
	} else {
		if (end&7) {
			end &= ~7;
			if (skb->ip_summed != CHECKSUM_UNNECESSARY)
				skb->ip_summed = CHECKSUM_NONE;
		}
		if (end > qp->len) {
			/* Some bits beyond end -> corruption. */
			if (qp->last_in & LAST_IN)
				goto err;
			qp->len = end;
		}
	}
	if (end == offset)
		goto err;

	if (pskb_pull(skb, ihl) == NULL)
		goto err;
	if (pskb_trim_rcsum(skb, end-offset))
		goto err;

	/* Find out which fragments are in front and at the back of us
	 * in the chain of fragments so far.  We must know where to put
	 * this fragment, right?
	 */
	prev = NULL;
	for(next = qp->fragments; next != NULL; next = next->next) {
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
			if (end <= offset)
				goto err;
			if (!pskb_pull(skb, i))
				goto err;
			if (skb->ip_summed != CHECKSUM_UNNECESSARY)
				skb->ip_summed = CHECKSUM_NONE;
		}
	}

	while (next && FRAG_CB(next)->offset < end) {
		int i = end - FRAG_CB(next)->offset; /* overlap is 'i' bytes */

		if (i < next->len) {
			/* Eat head of the next overlapped fragment
			 * and leave the loop. The next ones cannot overlap.
			 */
			if (!pskb_pull(next, i))
				goto err;
			FRAG_CB(next)->offset += i;
			qp->meat -= i;
			if (next->ip_summed != CHECKSUM_UNNECESSARY)
				next->ip_summed = CHECKSUM_NONE;
			break;
		} else {
			struct sk_buff *free_it = next;

			/* Old fragmnet is completely overridden with
			 * new one drop it.
			 */
			next = next->next;

			if (prev)
				prev->next = next;
			else
				qp->fragments = next;

			qp->meat -= free_it->len;
			frag_kfree_skb(free_it, NULL);
		}
	}

	FRAG_CB(skb)->offset = offset;

	/* Insert this fragment in the chain of fragments. */
	skb->next = next;
	if (prev)
		prev->next = skb;
	else
		qp->fragments = skb;

 	if (skb->dev)
 		qp->iif = skb->dev->ifindex;
	skb->dev = NULL;
	skb_get_timestamp(skb, &qp->stamp);
	qp->meat += skb->len;
	atomic_add(skb->truesize, &ip_frag_mem);
	if (offset == 0)
		qp->last_in |= FIRST_IN;

	write_lock(&ipfrag_lock);
	list_move_tail(&qp->lru_list, &ipq_lru_list);
	write_unlock(&ipfrag_lock);

	return;

err:
	kfree_skb(skb);
}


/* Build a new IP datagram from all its fragments. */

static struct sk_buff *ip_frag_reasm(struct ipq *qp, struct net_device *dev)
{
	struct iphdr *iph;
	struct sk_buff *fp, *head = qp->fragments;
	int len;
	int ihlen;

	ipq_kill(qp);

	BUG_TRAP(head != NULL);
	BUG_TRAP(FRAG_CB(head)->offset == 0);

	/* Allocate a new buffer for the datagram. */
	ihlen = head->nh.iph->ihl*4;
	len = ihlen + qp->len;

	if(len > 65535)
		goto out_oversize;

	/* Head of list must not be cloned. */
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
		atomic_add(clone->truesize, &ip_frag_mem);
	}

	skb_shinfo(head)->frag_list = head->next;
	skb_push(head, head->data - head->nh.raw);
	atomic_sub(head->truesize, &ip_frag_mem);

	for (fp=head->next; fp; fp = fp->next) {
		head->data_len += fp->len;
		head->len += fp->len;
		if (head->ip_summed != fp->ip_summed)
			head->ip_summed = CHECKSUM_NONE;
		else if (head->ip_summed == CHECKSUM_HW)
			head->csum = csum_add(head->csum, fp->csum);
		head->truesize += fp->truesize;
		atomic_sub(fp->truesize, &ip_frag_mem);
	}

	head->next = NULL;
	head->dev = dev;
	skb_set_timestamp(head, &qp->stamp);

	iph = head->nh.iph;
	iph->frag_off = 0;
	iph->tot_len = htons(len);
	IP_INC_STATS_BH(IPSTATS_MIB_REASMOKS);
	qp->fragments = NULL;
	return head;

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
	return NULL;
}

/* Process an incoming IP datagram fragment. */
struct sk_buff *ip_defrag(struct sk_buff *skb, u32 user)
{
	struct iphdr *iph = skb->nh.iph;
	struct ipq *qp;
	struct net_device *dev;
	
	IP_INC_STATS_BH(IPSTATS_MIB_REASMREQDS);

	/* Start by cleaning up the memory. */
	if (atomic_read(&ip_frag_mem) > sysctl_ipfrag_high_thresh)
		ip_evictor();

	dev = skb->dev;

	/* Lookup (or create) queue header */
	if ((qp = ip_find(iph, user)) != NULL) {
		struct sk_buff *ret = NULL;

		spin_lock(&qp->lock);

		ip_frag_queue(qp, skb);

		if (qp->last_in == (FIRST_IN|LAST_IN) &&
		    qp->meat == qp->len)
			ret = ip_frag_reasm(qp, dev);

		spin_unlock(&qp->lock);
		ipq_put(qp, NULL);
		return ret;
	}

	IP_INC_STATS_BH(IPSTATS_MIB_REASMFAILS);
	kfree_skb(skb);
	return NULL;
}

void ipfrag_init(void)
{
	ipfrag_hash_rnd = (u32) ((num_physpages ^ (num_physpages>>7)) ^
				 (jiffies ^ (jiffies >> 6)));

	init_timer(&ipfrag_secret_timer);
	ipfrag_secret_timer.function = ipfrag_secret_rebuild;
	ipfrag_secret_timer.expires = jiffies + sysctl_ipfrag_secret_interval;
	add_timer(&ipfrag_secret_timer);
}

EXPORT_SYMBOL(ip_defrag);
