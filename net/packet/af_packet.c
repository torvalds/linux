/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		PACKET - implements raw packet sockets.
 *
 * Version:	$Id: af_packet.c,v 1.61 2002/02/08 03:57:19 davem Exp $
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *
 * Fixes:	
 *		Alan Cox	:	verify_area() now used correctly
 *		Alan Cox	:	new skbuff lists, look ma no backlogs!
 *		Alan Cox	:	tidied skbuff lists.
 *		Alan Cox	:	Now uses generic datagram routines I
 *					added. Also fixed the peek/read crash
 *					from all old Linux datagram code.
 *		Alan Cox	:	Uses the improved datagram code.
 *		Alan Cox	:	Added NULL's for socket options.
 *		Alan Cox	:	Re-commented the code.
 *		Alan Cox	:	Use new kernel side addressing
 *		Rob Janssen	:	Correct MTU usage.
 *		Dave Platt	:	Counter leaks caused by incorrect
 *					interrupt locking and some slightly
 *					dubious gcc output. Can you read
 *					compiler: it said _VOLATILE_
 *	Richard Kooijman	:	Timestamp fixes.
 *		Alan Cox	:	New buffers. Use sk->mac.raw.
 *		Alan Cox	:	sendmsg/recvmsg support.
 *		Alan Cox	:	Protocol setting support
 *	Alexey Kuznetsov	:	Untied from IPv4 stack.
 *	Cyrus Durgin		:	Fixed kerneld for kmod.
 *	Michal Ostrowski        :       Module initialization cleanup.
 *         Ulises Alonso        :       Frame number limit removal and 
 *                                      packet_set_ring memory leak.
 *		Eric Biederman	:	Allow for > 8 byte hardware addresses.
 *					The convention is that longer addresses
 *					will simply extend the hardware address
 *					byte arrays at the end of sockaddr_ll 
 *					and packet_mreq.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */
 
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/capability.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_packet.h>
#include <linux/wireless.h>
#include <linux/kmod.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/ioctls.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/io.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/init.h>

#ifdef CONFIG_INET
#include <net/inet_common.h>
#endif

#define CONFIG_SOCK_PACKET	1

/*
   Proposed replacement for SIOC{ADD,DEL}MULTI and
   IFF_PROMISC, IFF_ALLMULTI flags.

   It is more expensive, but I believe,
   it is really correct solution: reentereble, safe and fault tolerant.

   IFF_PROMISC/IFF_ALLMULTI/SIOC{ADD/DEL}MULTI are faked by keeping
   reference count and global flag, so that real status is
   (gflag|(count != 0)), so that we can use obsolete faulty interface
   not harming clever users.
 */
#define CONFIG_PACKET_MULTICAST	1

/*
   Assumptions:
   - if device has no dev->hard_header routine, it adds and removes ll header
     inside itself. In this case ll header is invisible outside of device,
     but higher levels still should reserve dev->hard_header_len.
     Some devices are enough clever to reallocate skb, when header
     will not fit to reserved space (tunnel), another ones are silly
     (PPP).
   - packet socket receives packets with pulled ll header,
     so that SOCK_RAW should push it back.

On receive:
-----------

Incoming, dev->hard_header!=NULL
   mac.raw -> ll header
   data    -> data

Outgoing, dev->hard_header!=NULL
   mac.raw -> ll header
   data    -> ll header

Incoming, dev->hard_header==NULL
   mac.raw -> UNKNOWN position. It is very likely, that it points to ll header.
              PPP makes it, that is wrong, because introduce assymetry
	      between rx and tx paths.
   data    -> data

Outgoing, dev->hard_header==NULL
   mac.raw -> data. ll header is still not built!
   data    -> data

Resume
  If dev->hard_header==NULL we are unlikely to restore sensible ll header.


On transmit:
------------

dev->hard_header != NULL
   mac.raw -> ll header
   data    -> ll header

dev->hard_header == NULL (ll header is added by device, we cannot control it)
   mac.raw -> data
   data -> data

   We should set nh.raw on output to correct posistion,
   packet classifier depends on it.
 */

/* List of all packet sockets. */
static HLIST_HEAD(packet_sklist);
static DEFINE_RWLOCK(packet_sklist_lock);

static atomic_t packet_socks_nr;


/* Private packet socket structures. */

#ifdef CONFIG_PACKET_MULTICAST
struct packet_mclist
{
	struct packet_mclist	*next;
	int			ifindex;
	int			count;
	unsigned short		type;
	unsigned short		alen;
	unsigned char		addr[MAX_ADDR_LEN];
};
/* identical to struct packet_mreq except it has
 * a longer address field.
 */
struct packet_mreq_max
{
	int		mr_ifindex;
	unsigned short	mr_type;
	unsigned short	mr_alen;
	unsigned char	mr_address[MAX_ADDR_LEN];
};
#endif
#ifdef CONFIG_PACKET_MMAP
static int packet_set_ring(struct sock *sk, struct tpacket_req *req, int closing);
#endif

static void packet_flush_mclist(struct sock *sk);

struct packet_sock {
	/* struct sock has to be the first member of packet_sock */
	struct sock		sk;
	struct tpacket_stats	stats;
#ifdef CONFIG_PACKET_MMAP
	char *			*pg_vec;
	unsigned int		head;
	unsigned int            frames_per_block;
	unsigned int		frame_size;
	unsigned int		frame_max;
	int			copy_thresh;
#endif
	struct packet_type	prot_hook;
	spinlock_t		bind_lock;
	char			running;	/* prot_hook is attached*/
	int			ifindex;	/* bound device		*/
	__be16			num;
#ifdef CONFIG_PACKET_MULTICAST
	struct packet_mclist	*mclist;
#endif
#ifdef CONFIG_PACKET_MMAP
	atomic_t		mapped;
	unsigned int            pg_vec_order;
	unsigned int		pg_vec_pages;
	unsigned int		pg_vec_len;
#endif
};

#ifdef CONFIG_PACKET_MMAP

static inline char *packet_lookup_frame(struct packet_sock *po, unsigned int position)
{
	unsigned int pg_vec_pos, frame_offset;
	char *frame;

	pg_vec_pos = position / po->frames_per_block;
	frame_offset = position % po->frames_per_block;

	frame = po->pg_vec[pg_vec_pos] + (frame_offset * po->frame_size);
	
	return frame;
}
#endif

static inline struct packet_sock *pkt_sk(struct sock *sk)
{
	return (struct packet_sock *)sk;
}

static void packet_sock_destruct(struct sock *sk)
{
	BUG_TRAP(!atomic_read(&sk->sk_rmem_alloc));
	BUG_TRAP(!atomic_read(&sk->sk_wmem_alloc));

	if (!sock_flag(sk, SOCK_DEAD)) {
		printk("Attempt to release alive packet socket: %p\n", sk);
		return;
	}

	atomic_dec(&packet_socks_nr);
#ifdef PACKET_REFCNT_DEBUG
	printk(KERN_DEBUG "PACKET socket %p is free, %d are alive\n", sk, atomic_read(&packet_socks_nr));
#endif
}


static const struct proto_ops packet_ops;

#ifdef CONFIG_SOCK_PACKET
static const struct proto_ops packet_ops_spkt;

static int packet_rcv_spkt(struct sk_buff *skb, struct net_device *dev,  struct packet_type *pt, struct net_device *orig_dev)
{
	struct sock *sk;
	struct sockaddr_pkt *spkt;

	/*
	 *	When we registered the protocol we saved the socket in the data
	 *	field for just this event.
	 */

	sk = pt->af_packet_priv;
	
	/*
	 *	Yank back the headers [hope the device set this
	 *	right or kerboom...]
	 *
	 *	Incoming packets have ll header pulled,
	 *	push it back.
	 *
	 *	For outgoing ones skb->data == skb->mac.raw
	 *	so that this procedure is noop.
	 */

	if (skb->pkt_type == PACKET_LOOPBACK)
		goto out;

	if ((skb = skb_share_check(skb, GFP_ATOMIC)) == NULL)
		goto oom;

	/* drop any routing info */
	dst_release(skb->dst);
	skb->dst = NULL;

	/* drop conntrack reference */
	nf_reset(skb);

	spkt = (struct sockaddr_pkt*)skb->cb;

	skb_push(skb, skb->data-skb->mac.raw);

	/*
	 *	The SOCK_PACKET socket receives _all_ frames.
	 */

	spkt->spkt_family = dev->type;
	strlcpy(spkt->spkt_device, dev->name, sizeof(spkt->spkt_device));
	spkt->spkt_protocol = skb->protocol;

	/*
	 *	Charge the memory to the socket. This is done specifically
	 *	to prevent sockets using all the memory up.
	 */

	if (sock_queue_rcv_skb(sk,skb) == 0)
		return 0;

out:
	kfree_skb(skb);
oom:
	return 0;
}


/*
 *	Output a raw packet to a device layer. This bypasses all the other
 *	protocol layers and you must therefore supply it with a complete frame
 */
 
static int packet_sendmsg_spkt(struct kiocb *iocb, struct socket *sock,
			       struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct sockaddr_pkt *saddr=(struct sockaddr_pkt *)msg->msg_name;
	struct sk_buff *skb;
	struct net_device *dev;
	__be16 proto=0;
	int err;
	
	/*
	 *	Get and verify the address. 
	 */

	if (saddr)
	{
		if (msg->msg_namelen < sizeof(struct sockaddr))
			return(-EINVAL);
		if (msg->msg_namelen==sizeof(struct sockaddr_pkt))
			proto=saddr->spkt_protocol;
	}
	else
		return(-ENOTCONN);	/* SOCK_PACKET must be sent giving an address */

	/*
	 *	Find the device first to size check it 
	 */

	saddr->spkt_device[13] = 0;
	dev = dev_get_by_name(saddr->spkt_device);
	err = -ENODEV;
	if (dev == NULL)
		goto out_unlock;
	
	/*
	 *	You may not queue a frame bigger than the mtu. This is the lowest level
	 *	raw protocol and you must do your own fragmentation at this level.
	 */
	 
	err = -EMSGSIZE;
	if (len > dev->mtu + dev->hard_header_len)
		goto out_unlock;

	err = -ENOBUFS;
	skb = sock_wmalloc(sk, len + LL_RESERVED_SPACE(dev), 0, GFP_KERNEL);

	/*
	 *	If the write buffer is full, then tough. At this level the user gets to
	 *	deal with the problem - do your own algorithmic backoffs. That's far
	 *	more flexible.
	 */
	 
	if (skb == NULL) 
		goto out_unlock;

	/*
	 *	Fill it in 
	 */
	 
	/* FIXME: Save some space for broken drivers that write a
	 * hard header at transmission time by themselves. PPP is the
	 * notable one here. This should really be fixed at the driver level.
	 */
	skb_reserve(skb, LL_RESERVED_SPACE(dev));
	skb->nh.raw = skb->data;

	/* Try to align data part correctly */
	if (dev->hard_header) {
		skb->data -= dev->hard_header_len;
		skb->tail -= dev->hard_header_len;
		if (len < dev->hard_header_len)
			skb->nh.raw = skb->data;
	}

	/* Returns -EFAULT on error */
	err = memcpy_fromiovec(skb_put(skb,len), msg->msg_iov, len);
	skb->protocol = proto;
	skb->dev = dev;
	skb->priority = sk->sk_priority;
	if (err)
		goto out_free;

	err = -ENETDOWN;
	if (!(dev->flags & IFF_UP))
		goto out_free;

	/*
	 *	Now send it
	 */

	dev_queue_xmit(skb);
	dev_put(dev);
	return(len);

out_free:
	kfree_skb(skb);
out_unlock:
	if (dev)
		dev_put(dev);
	return err;
}
#endif

static inline int run_filter(struct sk_buff *skb, struct sock *sk,
							unsigned *snaplen)
{
	struct sk_filter *filter;
	int err = 0;

	rcu_read_lock_bh();
	filter = rcu_dereference(sk->sk_filter);
	if (filter != NULL) {
		err = sk_run_filter(skb, filter->insns, filter->len);
		if (!err)
			err = -EPERM;
		else if (*snaplen > err)
			*snaplen = err;
	}
	rcu_read_unlock_bh();

	return err;
}

/*
   This function makes lazy skb cloning in hope that most of packets
   are discarded by BPF.

   Note tricky part: we DO mangle shared skb! skb->data, skb->len
   and skb->cb are mangled. It works because (and until) packets
   falling here are owned by current CPU. Output packets are cloned
   by dev_queue_xmit_nit(), input packets are processed by net_bh
   sequencially, so that if we return skb to original state on exit,
   we will not harm anyone.
 */

static int packet_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev)
{
	struct sock *sk;
	struct sockaddr_ll *sll;
	struct packet_sock *po;
	u8 * skb_head = skb->data;
	int skb_len = skb->len;
	unsigned snaplen;

	if (skb->pkt_type == PACKET_LOOPBACK)
		goto drop;

	sk = pt->af_packet_priv;
	po = pkt_sk(sk);

	skb->dev = dev;

	if (dev->hard_header) {
		/* The device has an explicit notion of ll header,
		   exported to higher levels.

		   Otherwise, the device hides datails of it frame
		   structure, so that corresponding packet head
		   never delivered to user.
		 */
		if (sk->sk_type != SOCK_DGRAM)
			skb_push(skb, skb->data - skb->mac.raw);
		else if (skb->pkt_type == PACKET_OUTGOING) {
			/* Special case: outgoing packets have ll header at head */
			skb_pull(skb, skb->nh.raw - skb->data);
		}
	}

	snaplen = skb->len;

	if (run_filter(skb, sk, &snaplen) < 0)
		goto drop_n_restore;

	if (atomic_read(&sk->sk_rmem_alloc) + skb->truesize >=
	    (unsigned)sk->sk_rcvbuf)
		goto drop_n_acct;

	if (skb_shared(skb)) {
		struct sk_buff *nskb = skb_clone(skb, GFP_ATOMIC);
		if (nskb == NULL)
			goto drop_n_acct;

		if (skb_head != skb->data) {
			skb->data = skb_head;
			skb->len = skb_len;
		}
		kfree_skb(skb);
		skb = nskb;
	}

	sll = (struct sockaddr_ll*)skb->cb;
	sll->sll_family = AF_PACKET;
	sll->sll_hatype = dev->type;
	sll->sll_protocol = skb->protocol;
	sll->sll_pkttype = skb->pkt_type;
	sll->sll_ifindex = dev->ifindex;
	sll->sll_halen = 0;

	if (dev->hard_header_parse)
		sll->sll_halen = dev->hard_header_parse(skb, sll->sll_addr);

	if (pskb_trim(skb, snaplen))
		goto drop_n_acct;

	skb_set_owner_r(skb, sk);
	skb->dev = NULL;
	dst_release(skb->dst);
	skb->dst = NULL;

	/* drop conntrack reference */
	nf_reset(skb);

	spin_lock(&sk->sk_receive_queue.lock);
	po->stats.tp_packets++;
	__skb_queue_tail(&sk->sk_receive_queue, skb);
	spin_unlock(&sk->sk_receive_queue.lock);
	sk->sk_data_ready(sk, skb->len);
	return 0;

drop_n_acct:
	spin_lock(&sk->sk_receive_queue.lock);
	po->stats.tp_drops++;
	spin_unlock(&sk->sk_receive_queue.lock);

drop_n_restore:
	if (skb_head != skb->data && skb_shared(skb)) {
		skb->data = skb_head;
		skb->len = skb_len;
	}
drop:
	kfree_skb(skb);
	return 0;
}

#ifdef CONFIG_PACKET_MMAP
static int tpacket_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev)
{
	struct sock *sk;
	struct packet_sock *po;
	struct sockaddr_ll *sll;
	struct tpacket_hdr *h;
	u8 * skb_head = skb->data;
	int skb_len = skb->len;
	unsigned snaplen;
	unsigned long status = TP_STATUS_LOSING|TP_STATUS_USER;
	unsigned short macoff, netoff;
	struct sk_buff *copy_skb = NULL;

	if (skb->pkt_type == PACKET_LOOPBACK)
		goto drop;

	sk = pt->af_packet_priv;
	po = pkt_sk(sk);

	if (dev->hard_header) {
		if (sk->sk_type != SOCK_DGRAM)
			skb_push(skb, skb->data - skb->mac.raw);
		else if (skb->pkt_type == PACKET_OUTGOING) {
			/* Special case: outgoing packets have ll header at head */
			skb_pull(skb, skb->nh.raw - skb->data);
			if (skb->ip_summed == CHECKSUM_PARTIAL)
				status |= TP_STATUS_CSUMNOTREADY;
		}
	}

	snaplen = skb->len;

	if (run_filter(skb, sk, &snaplen) < 0)
		goto drop_n_restore;

	if (sk->sk_type == SOCK_DGRAM) {
		macoff = netoff = TPACKET_ALIGN(TPACKET_HDRLEN) + 16;
	} else {
		unsigned maclen = skb->nh.raw - skb->data;
		netoff = TPACKET_ALIGN(TPACKET_HDRLEN + (maclen < 16 ? 16 : maclen));
		macoff = netoff - maclen;
	}

	if (macoff + snaplen > po->frame_size) {
		if (po->copy_thresh &&
		    atomic_read(&sk->sk_rmem_alloc) + skb->truesize <
		    (unsigned)sk->sk_rcvbuf) {
			if (skb_shared(skb)) {
				copy_skb = skb_clone(skb, GFP_ATOMIC);
			} else {
				copy_skb = skb_get(skb);
				skb_head = skb->data;
			}
			if (copy_skb)
				skb_set_owner_r(copy_skb, sk);
		}
		snaplen = po->frame_size - macoff;
		if ((int)snaplen < 0)
			snaplen = 0;
	}

	spin_lock(&sk->sk_receive_queue.lock);
	h = (struct tpacket_hdr *)packet_lookup_frame(po, po->head);
	
	if (h->tp_status)
		goto ring_is_full;
	po->head = po->head != po->frame_max ? po->head+1 : 0;
	po->stats.tp_packets++;
	if (copy_skb) {
		status |= TP_STATUS_COPY;
		__skb_queue_tail(&sk->sk_receive_queue, copy_skb);
	}
	if (!po->stats.tp_drops)
		status &= ~TP_STATUS_LOSING;
	spin_unlock(&sk->sk_receive_queue.lock);

	skb_copy_bits(skb, 0, (u8*)h + macoff, snaplen);

	h->tp_len = skb->len;
	h->tp_snaplen = snaplen;
	h->tp_mac = macoff;
	h->tp_net = netoff;
	if (skb->tstamp.off_sec == 0) { 
		__net_timestamp(skb);
		sock_enable_timestamp(sk);
	}
	h->tp_sec = skb->tstamp.off_sec;
	h->tp_usec = skb->tstamp.off_usec;

	sll = (struct sockaddr_ll*)((u8*)h + TPACKET_ALIGN(sizeof(*h)));
	sll->sll_halen = 0;
	if (dev->hard_header_parse)
		sll->sll_halen = dev->hard_header_parse(skb, sll->sll_addr);
	sll->sll_family = AF_PACKET;
	sll->sll_hatype = dev->type;
	sll->sll_protocol = skb->protocol;
	sll->sll_pkttype = skb->pkt_type;
	sll->sll_ifindex = dev->ifindex;

	h->tp_status = status;
	smp_mb();

	{
		struct page *p_start, *p_end;
		u8 *h_end = (u8 *)h + macoff + snaplen - 1;

		p_start = virt_to_page(h);
		p_end = virt_to_page(h_end);
		while (p_start <= p_end) {
			flush_dcache_page(p_start);
			p_start++;
		}
	}

	sk->sk_data_ready(sk, 0);

drop_n_restore:
	if (skb_head != skb->data && skb_shared(skb)) {
		skb->data = skb_head;
		skb->len = skb_len;
	}
drop:
        kfree_skb(skb);
	return 0;

ring_is_full:
	po->stats.tp_drops++;
	spin_unlock(&sk->sk_receive_queue.lock);

	sk->sk_data_ready(sk, 0);
	if (copy_skb)
		kfree_skb(copy_skb);
	goto drop_n_restore;
}

#endif


static int packet_sendmsg(struct kiocb *iocb, struct socket *sock,
			  struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct sockaddr_ll *saddr=(struct sockaddr_ll *)msg->msg_name;
	struct sk_buff *skb;
	struct net_device *dev;
	__be16 proto;
	unsigned char *addr;
	int ifindex, err, reserve = 0;

	/*
	 *	Get and verify the address. 
	 */
	 
	if (saddr == NULL) {
		struct packet_sock *po = pkt_sk(sk);

		ifindex	= po->ifindex;
		proto	= po->num;
		addr	= NULL;
	} else {
		err = -EINVAL;
		if (msg->msg_namelen < sizeof(struct sockaddr_ll))
			goto out;
		if (msg->msg_namelen < (saddr->sll_halen + offsetof(struct sockaddr_ll, sll_addr)))
			goto out;
		ifindex	= saddr->sll_ifindex;
		proto	= saddr->sll_protocol;
		addr	= saddr->sll_addr;
	}


	dev = dev_get_by_index(ifindex);
	err = -ENXIO;
	if (dev == NULL)
		goto out_unlock;
	if (sock->type == SOCK_RAW)
		reserve = dev->hard_header_len;

	err = -EMSGSIZE;
	if (len > dev->mtu+reserve)
		goto out_unlock;

	skb = sock_alloc_send_skb(sk, len + LL_RESERVED_SPACE(dev),
				msg->msg_flags & MSG_DONTWAIT, &err);
	if (skb==NULL)
		goto out_unlock;

	skb_reserve(skb, LL_RESERVED_SPACE(dev));
	skb->nh.raw = skb->data;

	if (dev->hard_header) {
		int res;
		err = -EINVAL;
		res = dev->hard_header(skb, dev, ntohs(proto), addr, NULL, len);
		if (sock->type != SOCK_DGRAM) {
			skb->tail = skb->data;
			skb->len = 0;
		} else if (res < 0)
			goto out_free;
	}

	/* Returns -EFAULT on error */
	err = memcpy_fromiovec(skb_put(skb,len), msg->msg_iov, len);
	if (err)
		goto out_free;

	skb->protocol = proto;
	skb->dev = dev;
	skb->priority = sk->sk_priority;

	err = -ENETDOWN;
	if (!(dev->flags & IFF_UP))
		goto out_free;

	/*
	 *	Now send it
	 */

	err = dev_queue_xmit(skb);
	if (err > 0 && (err = net_xmit_errno(err)) != 0)
		goto out_unlock;

	dev_put(dev);

	return(len);

out_free:
	kfree_skb(skb);
out_unlock:
	if (dev)
		dev_put(dev);
out:
	return err;
}

/*
 *	Close a PACKET socket. This is fairly simple. We immediately go
 *	to 'closed' state and remove our protocol entry in the device list.
 */

static int packet_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct packet_sock *po;

	if (!sk)
		return 0;

	po = pkt_sk(sk);

	write_lock_bh(&packet_sklist_lock);
	sk_del_node_init(sk);
	write_unlock_bh(&packet_sklist_lock);

	/*
	 *	Unhook packet receive handler.
	 */

	if (po->running) {
		/*
		 *	Remove the protocol hook
		 */
		dev_remove_pack(&po->prot_hook);
		po->running = 0;
		po->num = 0;
		__sock_put(sk);
	}

#ifdef CONFIG_PACKET_MULTICAST
	packet_flush_mclist(sk);
#endif

#ifdef CONFIG_PACKET_MMAP
	if (po->pg_vec) {
		struct tpacket_req req;
		memset(&req, 0, sizeof(req));
		packet_set_ring(sk, &req, 1);
	}
#endif

	/*
	 *	Now the socket is dead. No more input will appear.
	 */

	sock_orphan(sk);
	sock->sk = NULL;

	/* Purge queues */

	skb_queue_purge(&sk->sk_receive_queue);

	sock_put(sk);
	return 0;
}

/*
 *	Attach a packet hook.
 */

static int packet_do_bind(struct sock *sk, struct net_device *dev, __be16 protocol)
{
	struct packet_sock *po = pkt_sk(sk);
	/*
	 *	Detach an existing hook if present.
	 */

	lock_sock(sk);

	spin_lock(&po->bind_lock);
	if (po->running) {
		__sock_put(sk);
		po->running = 0;
		po->num = 0;
		spin_unlock(&po->bind_lock);
		dev_remove_pack(&po->prot_hook);
		spin_lock(&po->bind_lock);
	}

	po->num = protocol;
	po->prot_hook.type = protocol;
	po->prot_hook.dev = dev;

	po->ifindex = dev ? dev->ifindex : 0;

	if (protocol == 0)
		goto out_unlock;

	if (dev) {
		if (dev->flags&IFF_UP) {
			dev_add_pack(&po->prot_hook);
			sock_hold(sk);
			po->running = 1;
		} else {
			sk->sk_err = ENETDOWN;
			if (!sock_flag(sk, SOCK_DEAD))
				sk->sk_error_report(sk);
		}
	} else {
		dev_add_pack(&po->prot_hook);
		sock_hold(sk);
		po->running = 1;
	}

out_unlock:
	spin_unlock(&po->bind_lock);
	release_sock(sk);
	return 0;
}

/*
 *	Bind a packet socket to a device
 */

#ifdef CONFIG_SOCK_PACKET

static int packet_bind_spkt(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk=sock->sk;
	char name[15];
	struct net_device *dev;
	int err = -ENODEV;
	
	/*
	 *	Check legality
	 */
	 
	if (addr_len != sizeof(struct sockaddr))
		return -EINVAL;
	strlcpy(name,uaddr->sa_data,sizeof(name));

	dev = dev_get_by_name(name);
	if (dev) {
		err = packet_do_bind(sk, dev, pkt_sk(sk)->num);
		dev_put(dev);
	}
	return err;
}
#endif

static int packet_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_ll *sll = (struct sockaddr_ll*)uaddr;
	struct sock *sk=sock->sk;
	struct net_device *dev = NULL;
	int err;


	/*
	 *	Check legality
	 */
	 
	if (addr_len < sizeof(struct sockaddr_ll))
		return -EINVAL;
	if (sll->sll_family != AF_PACKET)
		return -EINVAL;

	if (sll->sll_ifindex) {
		err = -ENODEV;
		dev = dev_get_by_index(sll->sll_ifindex);
		if (dev == NULL)
			goto out;
	}
	err = packet_do_bind(sk, dev, sll->sll_protocol ? : pkt_sk(sk)->num);
	if (dev)
		dev_put(dev);

out:
	return err;
}

static struct proto packet_proto = {
	.name	  = "PACKET",
	.owner	  = THIS_MODULE,
	.obj_size = sizeof(struct packet_sock),
};

/*
 *	Create a packet of type SOCK_PACKET. 
 */

static int packet_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	struct packet_sock *po;
	__be16 proto = (__force __be16)protocol; /* weird, but documented */
	int err;

	if (!capable(CAP_NET_RAW))
		return -EPERM;
	if (sock->type != SOCK_DGRAM && sock->type != SOCK_RAW
#ifdef CONFIG_SOCK_PACKET
	    && sock->type != SOCK_PACKET
#endif
	    )
		return -ESOCKTNOSUPPORT;

	sock->state = SS_UNCONNECTED;

	err = -ENOBUFS;
	sk = sk_alloc(PF_PACKET, GFP_KERNEL, &packet_proto, 1);
	if (sk == NULL)
		goto out;

	sock->ops = &packet_ops;
#ifdef CONFIG_SOCK_PACKET
	if (sock->type == SOCK_PACKET)
		sock->ops = &packet_ops_spkt;
#endif
	sock_init_data(sock, sk);

	po = pkt_sk(sk);
	sk->sk_family = PF_PACKET;
	po->num = proto;

	sk->sk_destruct = packet_sock_destruct;
	atomic_inc(&packet_socks_nr);

	/*
	 *	Attach a protocol block
	 */

	spin_lock_init(&po->bind_lock);
	po->prot_hook.func = packet_rcv;
#ifdef CONFIG_SOCK_PACKET
	if (sock->type == SOCK_PACKET)
		po->prot_hook.func = packet_rcv_spkt;
#endif
	po->prot_hook.af_packet_priv = sk;

	if (proto) {
		po->prot_hook.type = proto;
		dev_add_pack(&po->prot_hook);
		sock_hold(sk);
		po->running = 1;
	}

	write_lock_bh(&packet_sklist_lock);
	sk_add_node(sk, &packet_sklist);
	write_unlock_bh(&packet_sklist_lock);
	return(0);
out:
	return err;
}

/*
 *	Pull a packet from our receive queue and hand it to the user.
 *	If necessary we block.
 */

static int packet_recvmsg(struct kiocb *iocb, struct socket *sock,
			  struct msghdr *msg, size_t len, int flags)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int copied, err;
	struct sockaddr_ll *sll;

	err = -EINVAL;
	if (flags & ~(MSG_PEEK|MSG_DONTWAIT|MSG_TRUNC|MSG_CMSG_COMPAT))
		goto out;

#if 0
	/* What error should we return now? EUNATTACH? */
	if (pkt_sk(sk)->ifindex < 0)
		return -ENODEV;
#endif

	/*
	 *	Call the generic datagram receiver. This handles all sorts
	 *	of horrible races and re-entrancy so we can forget about it
	 *	in the protocol layers.
	 *
	 *	Now it will return ENETDOWN, if device have just gone down,
	 *	but then it will block.
	 */

	skb=skb_recv_datagram(sk,flags,flags&MSG_DONTWAIT,&err);

	/*
	 *	An error occurred so return it. Because skb_recv_datagram() 
	 *	handles the blocking we don't see and worry about blocking
	 *	retries.
	 */

	if (skb == NULL)
		goto out;

	/*
	 *	If the address length field is there to be filled in, we fill
	 *	it in now.
	 */

	sll = (struct sockaddr_ll*)skb->cb;
	if (sock->type == SOCK_PACKET)
		msg->msg_namelen = sizeof(struct sockaddr_pkt);
	else
		msg->msg_namelen = sll->sll_halen + offsetof(struct sockaddr_ll, sll_addr);

	/*
	 *	You lose any data beyond the buffer you gave. If it worries a
	 *	user program they can ask the device for its MTU anyway.
	 */

	copied = skb->len;
	if (copied > len)
	{
		copied=len;
		msg->msg_flags|=MSG_TRUNC;
	}

	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	if (err)
		goto out_free;

	sock_recv_timestamp(msg, sk, skb);

	if (msg->msg_name)
		memcpy(msg->msg_name, skb->cb, msg->msg_namelen);

	/*
	 *	Free or return the buffer as appropriate. Again this
	 *	hides all the races and re-entrancy issues from us.
	 */
	err = (flags&MSG_TRUNC) ? skb->len : copied;

out_free:
	skb_free_datagram(sk, skb);
out:
	return err;
}

#ifdef CONFIG_SOCK_PACKET
static int packet_getname_spkt(struct socket *sock, struct sockaddr *uaddr,
			       int *uaddr_len, int peer)
{
	struct net_device *dev;
	struct sock *sk	= sock->sk;

	if (peer)
		return -EOPNOTSUPP;

	uaddr->sa_family = AF_PACKET;
	dev = dev_get_by_index(pkt_sk(sk)->ifindex);
	if (dev) {
		strlcpy(uaddr->sa_data, dev->name, 15);
		dev_put(dev);
	} else
		memset(uaddr->sa_data, 0, 14);
	*uaddr_len = sizeof(*uaddr);

	return 0;
}
#endif

static int packet_getname(struct socket *sock, struct sockaddr *uaddr,
			  int *uaddr_len, int peer)
{
	struct net_device *dev;
	struct sock *sk = sock->sk;
	struct packet_sock *po = pkt_sk(sk);
	struct sockaddr_ll *sll = (struct sockaddr_ll*)uaddr;

	if (peer)
		return -EOPNOTSUPP;

	sll->sll_family = AF_PACKET;
	sll->sll_ifindex = po->ifindex;
	sll->sll_protocol = po->num;
	dev = dev_get_by_index(po->ifindex);
	if (dev) {
		sll->sll_hatype = dev->type;
		sll->sll_halen = dev->addr_len;
		memcpy(sll->sll_addr, dev->dev_addr, dev->addr_len);
		dev_put(dev);
	} else {
		sll->sll_hatype = 0;	/* Bad: we have no ARPHRD_UNSPEC */
		sll->sll_halen = 0;
	}
	*uaddr_len = offsetof(struct sockaddr_ll, sll_addr) + sll->sll_halen;

	return 0;
}

#ifdef CONFIG_PACKET_MULTICAST
static void packet_dev_mc(struct net_device *dev, struct packet_mclist *i, int what)
{
	switch (i->type) {
	case PACKET_MR_MULTICAST:
		if (what > 0)
			dev_mc_add(dev, i->addr, i->alen, 0);
		else
			dev_mc_delete(dev, i->addr, i->alen, 0);
		break;
	case PACKET_MR_PROMISC:
		dev_set_promiscuity(dev, what);
		break;
	case PACKET_MR_ALLMULTI:
		dev_set_allmulti(dev, what);
		break;
	default:;
	}
}

static void packet_dev_mclist(struct net_device *dev, struct packet_mclist *i, int what)
{
	for ( ; i; i=i->next) {
		if (i->ifindex == dev->ifindex)
			packet_dev_mc(dev, i, what);
	}
}

static int packet_mc_add(struct sock *sk, struct packet_mreq_max *mreq)
{
	struct packet_sock *po = pkt_sk(sk);
	struct packet_mclist *ml, *i;
	struct net_device *dev;
	int err;

	rtnl_lock();

	err = -ENODEV;
	dev = __dev_get_by_index(mreq->mr_ifindex);
	if (!dev)
		goto done;

	err = -EINVAL;
	if (mreq->mr_alen > dev->addr_len)
		goto done;

	err = -ENOBUFS;
	i = kmalloc(sizeof(*i), GFP_KERNEL);
	if (i == NULL)
		goto done;

	err = 0;
	for (ml = po->mclist; ml; ml = ml->next) {
		if (ml->ifindex == mreq->mr_ifindex &&
		    ml->type == mreq->mr_type &&
		    ml->alen == mreq->mr_alen &&
		    memcmp(ml->addr, mreq->mr_address, ml->alen) == 0) {
			ml->count++;
			/* Free the new element ... */
			kfree(i);
			goto done;
		}
	}

	i->type = mreq->mr_type;
	i->ifindex = mreq->mr_ifindex;
	i->alen = mreq->mr_alen;
	memcpy(i->addr, mreq->mr_address, i->alen);
	i->count = 1;
	i->next = po->mclist;
	po->mclist = i;
	packet_dev_mc(dev, i, +1);

done:
	rtnl_unlock();
	return err;
}

static int packet_mc_drop(struct sock *sk, struct packet_mreq_max *mreq)
{
	struct packet_mclist *ml, **mlp;

	rtnl_lock();

	for (mlp = &pkt_sk(sk)->mclist; (ml = *mlp) != NULL; mlp = &ml->next) {
		if (ml->ifindex == mreq->mr_ifindex &&
		    ml->type == mreq->mr_type &&
		    ml->alen == mreq->mr_alen &&
		    memcmp(ml->addr, mreq->mr_address, ml->alen) == 0) {
			if (--ml->count == 0) {
				struct net_device *dev;
				*mlp = ml->next;
				dev = dev_get_by_index(ml->ifindex);
				if (dev) {
					packet_dev_mc(dev, ml, -1);
					dev_put(dev);
				}
				kfree(ml);
			}
			rtnl_unlock();
			return 0;
		}
	}
	rtnl_unlock();
	return -EADDRNOTAVAIL;
}

static void packet_flush_mclist(struct sock *sk)
{
	struct packet_sock *po = pkt_sk(sk);
	struct packet_mclist *ml;

	if (!po->mclist)
		return;

	rtnl_lock();
	while ((ml = po->mclist) != NULL) {
		struct net_device *dev;

		po->mclist = ml->next;
		if ((dev = dev_get_by_index(ml->ifindex)) != NULL) {
			packet_dev_mc(dev, ml, -1);
			dev_put(dev);
		}
		kfree(ml);
	}
	rtnl_unlock();
}
#endif

static int
packet_setsockopt(struct socket *sock, int level, int optname, char __user *optval, int optlen)
{
	struct sock *sk = sock->sk;
	int ret;

	if (level != SOL_PACKET)
		return -ENOPROTOOPT;

	switch(optname)	{
#ifdef CONFIG_PACKET_MULTICAST
	case PACKET_ADD_MEMBERSHIP:	
	case PACKET_DROP_MEMBERSHIP:
	{
		struct packet_mreq_max mreq;
		int len = optlen;
		memset(&mreq, 0, sizeof(mreq));
		if (len < sizeof(struct packet_mreq))
			return -EINVAL;
		if (len > sizeof(mreq))
			len = sizeof(mreq);
		if (copy_from_user(&mreq,optval,len))
			return -EFAULT;
		if (len < (mreq.mr_alen + offsetof(struct packet_mreq, mr_address)))
			return -EINVAL;
		if (optname == PACKET_ADD_MEMBERSHIP)
			ret = packet_mc_add(sk, &mreq);
		else
			ret = packet_mc_drop(sk, &mreq);
		return ret;
	}
#endif
#ifdef CONFIG_PACKET_MMAP
	case PACKET_RX_RING:
	{
		struct tpacket_req req;

		if (optlen<sizeof(req))
			return -EINVAL;
		if (copy_from_user(&req,optval,sizeof(req)))
			return -EFAULT;
		return packet_set_ring(sk, &req, 0);
	}
	case PACKET_COPY_THRESH:
	{
		int val;

		if (optlen!=sizeof(val))
			return -EINVAL;
		if (copy_from_user(&val,optval,sizeof(val)))
			return -EFAULT;

		pkt_sk(sk)->copy_thresh = val;
		return 0;
	}
#endif
	default:
		return -ENOPROTOOPT;
	}
}

static int packet_getsockopt(struct socket *sock, int level, int optname,
			     char __user *optval, int __user *optlen)
{
	int len;
	struct sock *sk = sock->sk;
	struct packet_sock *po = pkt_sk(sk);

	if (level != SOL_PACKET)
		return -ENOPROTOOPT;

	if (get_user(len, optlen))
		return -EFAULT;

	if (len < 0)
		return -EINVAL;
		
	switch(optname)	{
	case PACKET_STATISTICS:
	{
		struct tpacket_stats st;

		if (len > sizeof(struct tpacket_stats))
			len = sizeof(struct tpacket_stats);
		spin_lock_bh(&sk->sk_receive_queue.lock);
		st = po->stats;
		memset(&po->stats, 0, sizeof(st));
		spin_unlock_bh(&sk->sk_receive_queue.lock);
		st.tp_packets += st.tp_drops;

		if (copy_to_user(optval, &st, len))
			return -EFAULT;
		break;
	}
	default:
		return -ENOPROTOOPT;
	}

	if (put_user(len, optlen))
		return -EFAULT;
	return 0;
}


static int packet_notifier(struct notifier_block *this, unsigned long msg, void *data)
{
	struct sock *sk;
	struct hlist_node *node;
	struct net_device *dev = (struct net_device*)data;

	read_lock(&packet_sklist_lock);
	sk_for_each(sk, node, &packet_sklist) {
		struct packet_sock *po = pkt_sk(sk);

		switch (msg) {
		case NETDEV_UNREGISTER:
#ifdef CONFIG_PACKET_MULTICAST
			if (po->mclist)
				packet_dev_mclist(dev, po->mclist, -1);
			// fallthrough
#endif
		case NETDEV_DOWN:
			if (dev->ifindex == po->ifindex) {
				spin_lock(&po->bind_lock);
				if (po->running) {
					__dev_remove_pack(&po->prot_hook);
					__sock_put(sk);
					po->running = 0;
					sk->sk_err = ENETDOWN;
					if (!sock_flag(sk, SOCK_DEAD))
						sk->sk_error_report(sk);
				}
				if (msg == NETDEV_UNREGISTER) {
					po->ifindex = -1;
					po->prot_hook.dev = NULL;
				}
				spin_unlock(&po->bind_lock);
			}
			break;
		case NETDEV_UP:
			spin_lock(&po->bind_lock);
			if (dev->ifindex == po->ifindex && po->num &&
			    !po->running) {
				dev_add_pack(&po->prot_hook);
				sock_hold(sk);
				po->running = 1;
			}
			spin_unlock(&po->bind_lock);
			break;
		}
	}
	read_unlock(&packet_sklist_lock);
	return NOTIFY_DONE;
}


static int packet_ioctl(struct socket *sock, unsigned int cmd,
			unsigned long arg)
{
	struct sock *sk = sock->sk;

	switch(cmd) {
		case SIOCOUTQ:
		{
			int amount = atomic_read(&sk->sk_wmem_alloc);
			return put_user(amount, (int __user *)arg);
		}
		case SIOCINQ:
		{
			struct sk_buff *skb;
			int amount = 0;

			spin_lock_bh(&sk->sk_receive_queue.lock);
			skb = skb_peek(&sk->sk_receive_queue);
			if (skb)
				amount = skb->len;
			spin_unlock_bh(&sk->sk_receive_queue.lock);
			return put_user(amount, (int __user *)arg);
		}
		case SIOCGSTAMP:
			return sock_get_timestamp(sk, (struct timeval __user *)arg);
			
#ifdef CONFIG_INET
		case SIOCADDRT:
		case SIOCDELRT:
		case SIOCDARP:
		case SIOCGARP:
		case SIOCSARP:
		case SIOCGIFADDR:
		case SIOCSIFADDR:
		case SIOCGIFBRDADDR:
		case SIOCSIFBRDADDR:
		case SIOCGIFNETMASK:
		case SIOCSIFNETMASK:
		case SIOCGIFDSTADDR:
		case SIOCSIFDSTADDR:
		case SIOCSIFFLAGS:
			return inet_dgram_ops.ioctl(sock, cmd, arg);
#endif

		default:
			return -ENOIOCTLCMD;
	}
	return 0;
}

#ifndef CONFIG_PACKET_MMAP
#define packet_mmap sock_no_mmap
#define packet_poll datagram_poll
#else

static unsigned int packet_poll(struct file * file, struct socket *sock,
				poll_table *wait)
{
	struct sock *sk = sock->sk;
	struct packet_sock *po = pkt_sk(sk);
	unsigned int mask = datagram_poll(file, sock, wait);

	spin_lock_bh(&sk->sk_receive_queue.lock);
	if (po->pg_vec) {
		unsigned last = po->head ? po->head-1 : po->frame_max;
		struct tpacket_hdr *h;

		h = (struct tpacket_hdr *)packet_lookup_frame(po, last);

		if (h->tp_status)
			mask |= POLLIN | POLLRDNORM;
	}
	spin_unlock_bh(&sk->sk_receive_queue.lock);
	return mask;
}


/* Dirty? Well, I still did not learn better way to account
 * for user mmaps.
 */

static void packet_mm_open(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;
	struct socket * sock = file->private_data;
	struct sock *sk = sock->sk;
	
	if (sk)
		atomic_inc(&pkt_sk(sk)->mapped);
}

static void packet_mm_close(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;
	struct socket * sock = file->private_data;
	struct sock *sk = sock->sk;
	
	if (sk)
		atomic_dec(&pkt_sk(sk)->mapped);
}

static struct vm_operations_struct packet_mmap_ops = {
	.open =	packet_mm_open,
	.close =packet_mm_close,
};

static inline struct page *pg_vec_endpage(char *one_pg_vec, unsigned int order)
{
	return virt_to_page(one_pg_vec + (PAGE_SIZE << order) - 1);
}

static void free_pg_vec(char **pg_vec, unsigned int order, unsigned int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (likely(pg_vec[i]))
			free_pages((unsigned long) pg_vec[i], order);
	}
	kfree(pg_vec);
}

static inline char *alloc_one_pg_vec_page(unsigned long order)
{
	return (char *) __get_free_pages(GFP_KERNEL | __GFP_COMP | __GFP_ZERO,
					 order);
}

static char **alloc_pg_vec(struct tpacket_req *req, int order)
{
	unsigned int block_nr = req->tp_block_nr;
	char **pg_vec;
	int i;

	pg_vec = kzalloc(block_nr * sizeof(char *), GFP_KERNEL);
	if (unlikely(!pg_vec))
		goto out;

	for (i = 0; i < block_nr; i++) {
		pg_vec[i] = alloc_one_pg_vec_page(order);
		if (unlikely(!pg_vec[i]))
			goto out_free_pgvec;
	}

out:
	return pg_vec;

out_free_pgvec:
	free_pg_vec(pg_vec, order, block_nr);
	pg_vec = NULL;
	goto out;
}

static int packet_set_ring(struct sock *sk, struct tpacket_req *req, int closing)
{
	char **pg_vec = NULL;
	struct packet_sock *po = pkt_sk(sk);
	int was_running, order = 0;
	__be16 num;
	int err = 0;
	
	if (req->tp_block_nr) {
		int i, l;

		/* Sanity tests and some calculations */

		if (unlikely(po->pg_vec))
			return -EBUSY;

		if (unlikely((int)req->tp_block_size <= 0))
			return -EINVAL;
		if (unlikely(req->tp_block_size & (PAGE_SIZE - 1)))
			return -EINVAL;
		if (unlikely(req->tp_frame_size < TPACKET_HDRLEN))
			return -EINVAL;
		if (unlikely(req->tp_frame_size & (TPACKET_ALIGNMENT - 1)))
			return -EINVAL;

		po->frames_per_block = req->tp_block_size/req->tp_frame_size;
		if (unlikely(po->frames_per_block <= 0))
			return -EINVAL;
		if (unlikely((po->frames_per_block * req->tp_block_nr) !=
			     req->tp_frame_nr))
			return -EINVAL;

		err = -ENOMEM;
		order = get_order(req->tp_block_size);
		pg_vec = alloc_pg_vec(req, order);
		if (unlikely(!pg_vec))
			goto out;

		l = 0;
		for (i = 0; i < req->tp_block_nr; i++) {
			char *ptr = pg_vec[i];
			struct tpacket_hdr *header;
			int k;

			for (k = 0; k < po->frames_per_block; k++) {
				header = (struct tpacket_hdr *) ptr;
				header->tp_status = TP_STATUS_KERNEL;
				ptr += req->tp_frame_size;
			}
		}
		/* Done */
	} else {
		if (unlikely(req->tp_frame_nr))
			return -EINVAL;
	}

	lock_sock(sk);

	/* Detach socket from network */
	spin_lock(&po->bind_lock);
	was_running = po->running;
	num = po->num;
	if (was_running) {
		__dev_remove_pack(&po->prot_hook);
		po->num = 0;
		po->running = 0;
		__sock_put(sk);
	}
	spin_unlock(&po->bind_lock);
		
	synchronize_net();

	err = -EBUSY;
	if (closing || atomic_read(&po->mapped) == 0) {
		err = 0;
#define XC(a, b) ({ __typeof__ ((a)) __t; __t = (a); (a) = (b); __t; })

		spin_lock_bh(&sk->sk_receive_queue.lock);
		pg_vec = XC(po->pg_vec, pg_vec);
		po->frame_max = (req->tp_frame_nr - 1);
		po->head = 0;
		po->frame_size = req->tp_frame_size;
		spin_unlock_bh(&sk->sk_receive_queue.lock);

		order = XC(po->pg_vec_order, order);
		req->tp_block_nr = XC(po->pg_vec_len, req->tp_block_nr);

		po->pg_vec_pages = req->tp_block_size/PAGE_SIZE;
		po->prot_hook.func = po->pg_vec ? tpacket_rcv : packet_rcv;
		skb_queue_purge(&sk->sk_receive_queue);
#undef XC
		if (atomic_read(&po->mapped))
			printk(KERN_DEBUG "packet_mmap: vma is busy: %d\n", atomic_read(&po->mapped));
	}

	spin_lock(&po->bind_lock);
	if (was_running && !po->running) {
		sock_hold(sk);
		po->running = 1;
		po->num = num;
		dev_add_pack(&po->prot_hook);
	}
	spin_unlock(&po->bind_lock);

	release_sock(sk);

	if (pg_vec)
		free_pg_vec(pg_vec, order, req->tp_block_nr);
out:
	return err;
}

static int packet_mmap(struct file *file, struct socket *sock, struct vm_area_struct *vma)
{
	struct sock *sk = sock->sk;
	struct packet_sock *po = pkt_sk(sk);
	unsigned long size;
	unsigned long start;
	int err = -EINVAL;
	int i;

	if (vma->vm_pgoff)
		return -EINVAL;

	size = vma->vm_end - vma->vm_start;

	lock_sock(sk);
	if (po->pg_vec == NULL)
		goto out;
	if (size != po->pg_vec_len*po->pg_vec_pages*PAGE_SIZE)
		goto out;

	start = vma->vm_start;
	for (i = 0; i < po->pg_vec_len; i++) {
		struct page *page = virt_to_page(po->pg_vec[i]);
		int pg_num;

		for (pg_num = 0; pg_num < po->pg_vec_pages; pg_num++, page++) {
			err = vm_insert_page(vma, start, page);
			if (unlikely(err))
				goto out;
			start += PAGE_SIZE;
		}
	}
	atomic_inc(&po->mapped);
	vma->vm_ops = &packet_mmap_ops;
	err = 0;

out:
	release_sock(sk);
	return err;
}
#endif


#ifdef CONFIG_SOCK_PACKET
static const struct proto_ops packet_ops_spkt = {
	.family =	PF_PACKET,
	.owner =	THIS_MODULE,
	.release =	packet_release,
	.bind =		packet_bind_spkt,
	.connect =	sock_no_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	sock_no_accept,
	.getname =	packet_getname_spkt,
	.poll =		datagram_poll,
	.ioctl =	packet_ioctl,
	.listen =	sock_no_listen,
	.shutdown =	sock_no_shutdown,
	.setsockopt =	sock_no_setsockopt,
	.getsockopt =	sock_no_getsockopt,
	.sendmsg =	packet_sendmsg_spkt,
	.recvmsg =	packet_recvmsg,
	.mmap =		sock_no_mmap,
	.sendpage =	sock_no_sendpage,
};
#endif

static const struct proto_ops packet_ops = {
	.family =	PF_PACKET,
	.owner =	THIS_MODULE,
	.release =	packet_release,
	.bind =		packet_bind,
	.connect =	sock_no_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	sock_no_accept,
	.getname =	packet_getname, 
	.poll =		packet_poll,
	.ioctl =	packet_ioctl,
	.listen =	sock_no_listen,
	.shutdown =	sock_no_shutdown,
	.setsockopt =	packet_setsockopt,
	.getsockopt =	packet_getsockopt,
	.sendmsg =	packet_sendmsg,
	.recvmsg =	packet_recvmsg,
	.mmap =		packet_mmap,
	.sendpage =	sock_no_sendpage,
};

static struct net_proto_family packet_family_ops = {
	.family =	PF_PACKET,
	.create =	packet_create,
	.owner	=	THIS_MODULE,
};

static struct notifier_block packet_netdev_notifier = {
	.notifier_call =packet_notifier,
};

#ifdef CONFIG_PROC_FS
static inline struct sock *packet_seq_idx(loff_t off)
{
	struct sock *s;
	struct hlist_node *node;

	sk_for_each(s, node, &packet_sklist) {
		if (!off--)
			return s;
	}
	return NULL;
}

static void *packet_seq_start(struct seq_file *seq, loff_t *pos)
{
	read_lock(&packet_sklist_lock);
	return *pos ? packet_seq_idx(*pos - 1) : SEQ_START_TOKEN;
}

static void *packet_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return  (v == SEQ_START_TOKEN) 
		? sk_head(&packet_sklist) 
		: sk_next((struct sock*)v) ;
}

static void packet_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock(&packet_sklist_lock);		
}

static int packet_seq_show(struct seq_file *seq, void *v) 
{
	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "sk       RefCnt Type Proto  Iface R Rmem   User   Inode\n");
	else {
		struct sock *s = v;
		const struct packet_sock *po = pkt_sk(s);

		seq_printf(seq,
			   "%p %-6d %-4d %04x   %-5d %1d %-6u %-6u %-6lu\n",
			   s,
			   atomic_read(&s->sk_refcnt),
			   s->sk_type,
			   ntohs(po->num),
			   po->ifindex,
			   po->running,
			   atomic_read(&s->sk_rmem_alloc),
			   sock_i_uid(s),
			   sock_i_ino(s) );
	}

	return 0;
}

static struct seq_operations packet_seq_ops = {
	.start	= packet_seq_start,
	.next	= packet_seq_next,
	.stop	= packet_seq_stop,
	.show	= packet_seq_show,
};

static int packet_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &packet_seq_ops);
}

static struct file_operations packet_seq_fops = {
	.owner		= THIS_MODULE,
	.open		= packet_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

#endif

static void __exit packet_exit(void)
{
	proc_net_remove("packet");
	unregister_netdevice_notifier(&packet_netdev_notifier);
	sock_unregister(PF_PACKET);
	proto_unregister(&packet_proto);
}

static int __init packet_init(void)
{
	int rc = proto_register(&packet_proto, 0);

	if (rc != 0)
		goto out;

	sock_register(&packet_family_ops);
	register_netdevice_notifier(&packet_netdev_notifier);
	proc_net_fops_create("packet", 0, &packet_seq_fops);
out:
	return rc;
}

module_init(packet_init);
module_exit(packet_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_PACKET);
