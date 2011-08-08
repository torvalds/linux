/*
 * DECnet       An implementation of the DECnet protocol suite for the LINUX
 *              operating system.  DECnet is implemented using the  BSD Socket
 *              interface as the means of communication with the user level.
 *
 *              DECnet Neighbour Functions (Adjacency Database and
 *                                                        On-Ethernet Cache)
 *
 * Author:      Steve Whitehouse <SteveW@ACM.org>
 *
 *
 * Changes:
 *     Steve Whitehouse     : Fixed router listing routine
 *     Steve Whitehouse     : Added error_report functions
 *     Steve Whitehouse     : Added default router detection
 *     Steve Whitehouse     : Hop counts in outgoing messages
 *     Steve Whitehouse     : Fixed src/dst in outgoing messages so
 *                            forwarding now stands a good chance of
 *                            working.
 *     Steve Whitehouse     : Fixed neighbour states (for now anyway).
 *     Steve Whitehouse     : Made error_report functions dummies. This
 *                            is not the right place to return skbs.
 *     Steve Whitehouse     : Convert to seq_file
 *
 */

#include <linux/net.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/if_arp.h>
#include <linux/slab.h>
#include <linux/if_ether.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/netfilter_decnet.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#include <linux/rcupdate.h>
#include <linux/jhash.h>
#include <linux/atomic.h>
#include <net/net_namespace.h>
#include <net/neighbour.h>
#include <net/dst.h>
#include <net/flow.h>
#include <net/dn.h>
#include <net/dn_dev.h>
#include <net/dn_neigh.h>
#include <net/dn_route.h>

static int dn_neigh_construct(struct neighbour *);
static void dn_long_error_report(struct neighbour *, struct sk_buff *);
static void dn_short_error_report(struct neighbour *, struct sk_buff *);
static int dn_long_output(struct neighbour *, struct sk_buff *);
static int dn_short_output(struct neighbour *, struct sk_buff *);
static int dn_phase3_output(struct neighbour *, struct sk_buff *);


/*
 * For talking to broadcast devices: Ethernet & PPP
 */
static const struct neigh_ops dn_long_ops = {
	.family =		AF_DECnet,
	.error_report =		dn_long_error_report,
	.output =		dn_long_output,
	.connected_output =	dn_long_output,
};

/*
 * For talking to pointopoint and multidrop devices: DDCMP and X.25
 */
static const struct neigh_ops dn_short_ops = {
	.family =		AF_DECnet,
	.error_report =		dn_short_error_report,
	.output =		dn_short_output,
	.connected_output =	dn_short_output,
};

/*
 * For talking to DECnet phase III nodes
 */
static const struct neigh_ops dn_phase3_ops = {
	.family =		AF_DECnet,
	.error_report =		dn_short_error_report, /* Can use short version here */
	.output =		dn_phase3_output,
	.connected_output =	dn_phase3_output,
};

static u32 dn_neigh_hash(const void *pkey,
			 const struct net_device *dev,
			 __u32 hash_rnd)
{
	return jhash_2words(*(__u16 *)pkey, 0, hash_rnd);
}

struct neigh_table dn_neigh_table = {
	.family =			PF_DECnet,
	.entry_size =			sizeof(struct dn_neigh),
	.key_len =			sizeof(__le16),
	.hash =				dn_neigh_hash,
	.constructor =			dn_neigh_construct,
	.id =				"dn_neigh_cache",
	.parms ={
		.tbl =			&dn_neigh_table,
		.base_reachable_time =	30 * HZ,
		.retrans_time =	1 * HZ,
		.gc_staletime =	60 * HZ,
		.reachable_time =		30 * HZ,
		.delay_probe_time =	5 * HZ,
		.queue_len =		3,
		.ucast_probes =	0,
		.app_probes =		0,
		.mcast_probes =	0,
		.anycast_delay =	0,
		.proxy_delay =		0,
		.proxy_qlen =		0,
		.locktime =		1 * HZ,
	},
	.gc_interval =			30 * HZ,
	.gc_thresh1 =			128,
	.gc_thresh2 =			512,
	.gc_thresh3 =			1024,
};

static int dn_neigh_construct(struct neighbour *neigh)
{
	struct net_device *dev = neigh->dev;
	struct dn_neigh *dn = (struct dn_neigh *)neigh;
	struct dn_dev *dn_db;
	struct neigh_parms *parms;

	rcu_read_lock();
	dn_db = rcu_dereference(dev->dn_ptr);
	if (dn_db == NULL) {
		rcu_read_unlock();
		return -EINVAL;
	}

	parms = dn_db->neigh_parms;
	if (!parms) {
		rcu_read_unlock();
		return -EINVAL;
	}

	__neigh_parms_put(neigh->parms);
	neigh->parms = neigh_parms_clone(parms);

	if (dn_db->use_long)
		neigh->ops = &dn_long_ops;
	else
		neigh->ops = &dn_short_ops;
	rcu_read_unlock();

	if (dn->flags & DN_NDFLAG_P3)
		neigh->ops = &dn_phase3_ops;

	neigh->nud_state = NUD_NOARP;
	neigh->output = neigh->ops->connected_output;

	if ((dev->type == ARPHRD_IPGRE) || (dev->flags & IFF_POINTOPOINT))
		memcpy(neigh->ha, dev->broadcast, dev->addr_len);
	else if ((dev->type == ARPHRD_ETHER) || (dev->type == ARPHRD_LOOPBACK))
		dn_dn2eth(neigh->ha, dn->addr);
	else {
		if (net_ratelimit())
			printk(KERN_DEBUG "Trying to create neigh for hw %d\n",  dev->type);
		return -EINVAL;
	}

	/*
	 * Make an estimate of the remote block size by assuming that its
	 * two less then the device mtu, which it true for ethernet (and
	 * other things which support long format headers) since there is
	 * an extra length field (of 16 bits) which isn't part of the
	 * ethernet headers and which the DECnet specs won't admit is part
	 * of the DECnet routing headers either.
	 *
	 * If we over estimate here its no big deal, the NSP negotiations
	 * will prevent us from sending packets which are too large for the
	 * remote node to handle. In any case this figure is normally updated
	 * by a hello message in most cases.
	 */
	dn->blksize = dev->mtu - 2;

	return 0;
}

static void dn_long_error_report(struct neighbour *neigh, struct sk_buff *skb)
{
	printk(KERN_DEBUG "dn_long_error_report: called\n");
	kfree_skb(skb);
}


static void dn_short_error_report(struct neighbour *neigh, struct sk_buff *skb)
{
	printk(KERN_DEBUG "dn_short_error_report: called\n");
	kfree_skb(skb);
}

static int dn_neigh_output_packet(struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct dn_route *rt = (struct dn_route *)dst;
	struct neighbour *neigh = dst_get_neighbour(dst);
	struct net_device *dev = neigh->dev;
	char mac_addr[ETH_ALEN];

	dn_dn2eth(mac_addr, rt->rt_local_src);
	if (dev_hard_header(skb, dev, ntohs(skb->protocol), neigh->ha,
			    mac_addr, skb->len) >= 0)
		return dev_queue_xmit(skb);

	if (net_ratelimit())
		printk(KERN_DEBUG "dn_neigh_output_packet: oops, can't send packet\n");

	kfree_skb(skb);
	return -EINVAL;
}

static int dn_long_output(struct neighbour *neigh, struct sk_buff *skb)
{
	struct net_device *dev = neigh->dev;
	int headroom = dev->hard_header_len + sizeof(struct dn_long_packet) + 3;
	unsigned char *data;
	struct dn_long_packet *lp;
	struct dn_skb_cb *cb = DN_SKB_CB(skb);


	if (skb_headroom(skb) < headroom) {
		struct sk_buff *skb2 = skb_realloc_headroom(skb, headroom);
		if (skb2 == NULL) {
			if (net_ratelimit())
				printk(KERN_CRIT "dn_long_output: no memory\n");
			kfree_skb(skb);
			return -ENOBUFS;
		}
		kfree_skb(skb);
		skb = skb2;
		if (net_ratelimit())
			printk(KERN_INFO "dn_long_output: Increasing headroom\n");
	}

	data = skb_push(skb, sizeof(struct dn_long_packet) + 3);
	lp = (struct dn_long_packet *)(data+3);

	*((__le16 *)data) = cpu_to_le16(skb->len - 2);
	*(data + 2) = 1 | DN_RT_F_PF; /* Padding */

	lp->msgflg   = DN_RT_PKT_LONG|(cb->rt_flags&(DN_RT_F_IE|DN_RT_F_RQR|DN_RT_F_RTS));
	lp->d_area   = lp->d_subarea = 0;
	dn_dn2eth(lp->d_id, cb->dst);
	lp->s_area   = lp->s_subarea = 0;
	dn_dn2eth(lp->s_id, cb->src);
	lp->nl2      = 0;
	lp->visit_ct = cb->hops & 0x3f;
	lp->s_class  = 0;
	lp->pt       = 0;

	skb_reset_network_header(skb);

	return NF_HOOK(NFPROTO_DECNET, NF_DN_POST_ROUTING, skb, NULL,
		       neigh->dev, dn_neigh_output_packet);
}

static int dn_short_output(struct neighbour *neigh, struct sk_buff *skb)
{
	struct net_device *dev = neigh->dev;
	int headroom = dev->hard_header_len + sizeof(struct dn_short_packet) + 2;
	struct dn_short_packet *sp;
	unsigned char *data;
	struct dn_skb_cb *cb = DN_SKB_CB(skb);


	if (skb_headroom(skb) < headroom) {
		struct sk_buff *skb2 = skb_realloc_headroom(skb, headroom);
		if (skb2 == NULL) {
			if (net_ratelimit())
				printk(KERN_CRIT "dn_short_output: no memory\n");
			kfree_skb(skb);
			return -ENOBUFS;
		}
		kfree_skb(skb);
		skb = skb2;
		if (net_ratelimit())
			printk(KERN_INFO "dn_short_output: Increasing headroom\n");
	}

	data = skb_push(skb, sizeof(struct dn_short_packet) + 2);
	*((__le16 *)data) = cpu_to_le16(skb->len - 2);
	sp = (struct dn_short_packet *)(data+2);

	sp->msgflg     = DN_RT_PKT_SHORT|(cb->rt_flags&(DN_RT_F_RQR|DN_RT_F_RTS));
	sp->dstnode    = cb->dst;
	sp->srcnode    = cb->src;
	sp->forward    = cb->hops & 0x3f;

	skb_reset_network_header(skb);

	return NF_HOOK(NFPROTO_DECNET, NF_DN_POST_ROUTING, skb, NULL,
		       neigh->dev, dn_neigh_output_packet);
}

/*
 * Phase 3 output is the same is short output, execpt that
 * it clears the area bits before transmission.
 */
static int dn_phase3_output(struct neighbour *neigh, struct sk_buff *skb)
{
	struct net_device *dev = neigh->dev;
	int headroom = dev->hard_header_len + sizeof(struct dn_short_packet) + 2;
	struct dn_short_packet *sp;
	unsigned char *data;
	struct dn_skb_cb *cb = DN_SKB_CB(skb);

	if (skb_headroom(skb) < headroom) {
		struct sk_buff *skb2 = skb_realloc_headroom(skb, headroom);
		if (skb2 == NULL) {
			if (net_ratelimit())
				printk(KERN_CRIT "dn_phase3_output: no memory\n");
			kfree_skb(skb);
			return -ENOBUFS;
		}
		kfree_skb(skb);
		skb = skb2;
		if (net_ratelimit())
			printk(KERN_INFO "dn_phase3_output: Increasing headroom\n");
	}

	data = skb_push(skb, sizeof(struct dn_short_packet) + 2);
	*((__le16 *)data) = cpu_to_le16(skb->len - 2);
	sp = (struct dn_short_packet *)(data + 2);

	sp->msgflg   = DN_RT_PKT_SHORT|(cb->rt_flags&(DN_RT_F_RQR|DN_RT_F_RTS));
	sp->dstnode  = cb->dst & cpu_to_le16(0x03ff);
	sp->srcnode  = cb->src & cpu_to_le16(0x03ff);
	sp->forward  = cb->hops & 0x3f;

	skb_reset_network_header(skb);

	return NF_HOOK(NFPROTO_DECNET, NF_DN_POST_ROUTING, skb, NULL,
		       neigh->dev, dn_neigh_output_packet);
}

/*
 * Unfortunately, the neighbour code uses the device in its hash
 * function, so we don't get any advantage from it. This function
 * basically does a neigh_lookup(), but without comparing the device
 * field. This is required for the On-Ethernet cache
 */

/*
 * Pointopoint link receives a hello message
 */
void dn_neigh_pointopoint_hello(struct sk_buff *skb)
{
	kfree_skb(skb);
}

/*
 * Ethernet router hello message received
 */
int dn_neigh_router_hello(struct sk_buff *skb)
{
	struct rtnode_hello_message *msg = (struct rtnode_hello_message *)skb->data;

	struct neighbour *neigh;
	struct dn_neigh *dn;
	struct dn_dev *dn_db;
	__le16 src;

	src = dn_eth2dn(msg->id);

	neigh = __neigh_lookup(&dn_neigh_table, &src, skb->dev, 1);

	dn = (struct dn_neigh *)neigh;

	if (neigh) {
		write_lock(&neigh->lock);

		neigh->used = jiffies;
		dn_db = rcu_dereference(neigh->dev->dn_ptr);

		if (!(neigh->nud_state & NUD_PERMANENT)) {
			neigh->updated = jiffies;

			if (neigh->dev->type == ARPHRD_ETHER)
				memcpy(neigh->ha, &eth_hdr(skb)->h_source, ETH_ALEN);

			dn->blksize  = le16_to_cpu(msg->blksize);
			dn->priority = msg->priority;

			dn->flags &= ~DN_NDFLAG_P3;

			switch (msg->iinfo & DN_RT_INFO_TYPE) {
			case DN_RT_INFO_L1RT:
				dn->flags &=~DN_NDFLAG_R2;
				dn->flags |= DN_NDFLAG_R1;
				break;
			case DN_RT_INFO_L2RT:
				dn->flags |= DN_NDFLAG_R2;
			}
		}

		/* Only use routers in our area */
		if ((le16_to_cpu(src)>>10) == (le16_to_cpu((decnet_address))>>10)) {
			if (!dn_db->router) {
				dn_db->router = neigh_clone(neigh);
			} else {
				if (msg->priority > ((struct dn_neigh *)dn_db->router)->priority)
					neigh_release(xchg(&dn_db->router, neigh_clone(neigh)));
			}
		}
		write_unlock(&neigh->lock);
		neigh_release(neigh);
	}

	kfree_skb(skb);
	return 0;
}

/*
 * Endnode hello message received
 */
int dn_neigh_endnode_hello(struct sk_buff *skb)
{
	struct endnode_hello_message *msg = (struct endnode_hello_message *)skb->data;
	struct neighbour *neigh;
	struct dn_neigh *dn;
	__le16 src;

	src = dn_eth2dn(msg->id);

	neigh = __neigh_lookup(&dn_neigh_table, &src, skb->dev, 1);

	dn = (struct dn_neigh *)neigh;

	if (neigh) {
		write_lock(&neigh->lock);

		neigh->used = jiffies;

		if (!(neigh->nud_state & NUD_PERMANENT)) {
			neigh->updated = jiffies;

			if (neigh->dev->type == ARPHRD_ETHER)
				memcpy(neigh->ha, &eth_hdr(skb)->h_source, ETH_ALEN);
			dn->flags   &= ~(DN_NDFLAG_R1 | DN_NDFLAG_R2);
			dn->blksize  = le16_to_cpu(msg->blksize);
			dn->priority = 0;
		}

		write_unlock(&neigh->lock);
		neigh_release(neigh);
	}

	kfree_skb(skb);
	return 0;
}

static char *dn_find_slot(char *base, int max, int priority)
{
	int i;
	unsigned char *min = NULL;

	base += 6; /* skip first id */

	for(i = 0; i < max; i++) {
		if (!min || (*base < *min))
			min = base;
		base += 7; /* find next priority */
	}

	if (!min)
		return NULL;

	return (*min < priority) ? (min - 6) : NULL;
}

struct elist_cb_state {
	struct net_device *dev;
	unsigned char *ptr;
	unsigned char *rs;
	int t, n;
};

static void neigh_elist_cb(struct neighbour *neigh, void *_info)
{
	struct elist_cb_state *s = _info;
	struct dn_neigh *dn;

	if (neigh->dev != s->dev)
		return;

	dn = (struct dn_neigh *) neigh;
	if (!(dn->flags & (DN_NDFLAG_R1|DN_NDFLAG_R2)))
		return;

	if (s->t == s->n)
		s->rs = dn_find_slot(s->ptr, s->n, dn->priority);
	else
		s->t++;
	if (s->rs == NULL)
		return;

	dn_dn2eth(s->rs, dn->addr);
	s->rs += 6;
	*(s->rs) = neigh->nud_state & NUD_CONNECTED ? 0x80 : 0x0;
	*(s->rs) |= dn->priority;
	s->rs++;
}

int dn_neigh_elist(struct net_device *dev, unsigned char *ptr, int n)
{
	struct elist_cb_state state;

	state.dev = dev;
	state.t = 0;
	state.n = n;
	state.ptr = ptr;
	state.rs = ptr;

	neigh_for_each(&dn_neigh_table, neigh_elist_cb, &state);

	return state.t;
}


#ifdef CONFIG_PROC_FS

static inline void dn_neigh_format_entry(struct seq_file *seq,
					 struct neighbour *n)
{
	struct dn_neigh *dn = (struct dn_neigh *) n;
	char buf[DN_ASCBUF_LEN];

	read_lock(&n->lock);
	seq_printf(seq, "%-7s %s%s%s   %02x    %02d  %07ld %-8s\n",
		   dn_addr2asc(le16_to_cpu(dn->addr), buf),
		   (dn->flags&DN_NDFLAG_R1) ? "1" : "-",
		   (dn->flags&DN_NDFLAG_R2) ? "2" : "-",
		   (dn->flags&DN_NDFLAG_P3) ? "3" : "-",
		   dn->n.nud_state,
		   atomic_read(&dn->n.refcnt),
		   dn->blksize,
		   (dn->n.dev) ? dn->n.dev->name : "?");
	read_unlock(&n->lock);
}

static int dn_neigh_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "Addr    Flags State Use Blksize Dev\n");
	} else {
		dn_neigh_format_entry(seq, v);
	}

	return 0;
}

static void *dn_neigh_seq_start(struct seq_file *seq, loff_t *pos)
{
	return neigh_seq_start(seq, pos, &dn_neigh_table,
			       NEIGH_SEQ_NEIGH_ONLY);
}

static const struct seq_operations dn_neigh_seq_ops = {
	.start = dn_neigh_seq_start,
	.next  = neigh_seq_next,
	.stop  = neigh_seq_stop,
	.show  = dn_neigh_seq_show,
};

static int dn_neigh_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &dn_neigh_seq_ops,
			    sizeof(struct neigh_seq_state));
}

static const struct file_operations dn_neigh_seq_fops = {
	.owner		= THIS_MODULE,
	.open		= dn_neigh_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_net,
};

#endif

void __init dn_neigh_init(void)
{
	neigh_table_init(&dn_neigh_table);
	proc_net_fops_create(&init_net, "decnet_neigh", S_IRUGO, &dn_neigh_seq_fops);
}

void __exit dn_neigh_cleanup(void)
{
	proc_net_remove(&init_net, "decnet_neigh");
	neigh_table_clear(&dn_neigh_table);
}
