/*
 *	Definitions for the 'struct sk_buff' memory handlers.
 *
 *	Authors:
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Florian La Roche, <rzsfl@rz.uni-sb.de>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_SKBUFF_H
#define _LINUX_SKBUFF_H

#include <linux/kernel.h>
#include <linux/kmemcheck.h>
#include <linux/compiler.h>
#include <linux/time.h>
#include <linux/bug.h>
#include <linux/cache.h>

#include <linux/atomic.h>
#include <asm/types.h>
#include <linux/spinlock.h>
#include <linux/net.h>
#include <linux/textsearch.h>
#include <net/checksum.h>
#include <linux/rcupdate.h>
#include <linux/dmaengine.h>
#include <linux/hrtimer.h>
#include <linux/dma-mapping.h>
#include <linux/netdev_features.h>

/* Don't change this without changing skb_csum_unnecessary! */
#define CHECKSUM_NONE 0
#define CHECKSUM_UNNECESSARY 1
#define CHECKSUM_COMPLETE 2
#define CHECKSUM_PARTIAL 3

#define SKB_DATA_ALIGN(X)	(((X) + (SMP_CACHE_BYTES - 1)) & \
				 ~(SMP_CACHE_BYTES - 1))
#define SKB_WITH_OVERHEAD(X)	\
	((X) - SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))
#define SKB_MAX_ORDER(X, ORDER) \
	SKB_WITH_OVERHEAD((PAGE_SIZE << (ORDER)) - (X))
#define SKB_MAX_HEAD(X)		(SKB_MAX_ORDER((X), 0))
#define SKB_MAX_ALLOC		(SKB_MAX_ORDER(0, 2))

/* return minimum truesize of one skb containing X bytes of data */
#define SKB_TRUESIZE(X) ((X) +						\
			 SKB_DATA_ALIGN(sizeof(struct sk_buff)) +	\
			 SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

/* A. Checksumming of received packets by device.
 *
 *	NONE: device failed to checksum this packet.
 *		skb->csum is undefined.
 *
 *	UNNECESSARY: device parsed packet and wouldbe verified checksum.
 *		skb->csum is undefined.
 *	      It is bad option, but, unfortunately, many of vendors do this.
 *	      Apparently with secret goal to sell you new device, when you
 *	      will add new protocol to your host. F.e. IPv6. 8)
 *
 *	COMPLETE: the most generic way. Device supplied checksum of _all_
 *	    the packet as seen by netif_rx in skb->csum.
 *	    NOTE: Even if device supports only some protocols, but
 *	    is able to produce some skb->csum, it MUST use COMPLETE,
 *	    not UNNECESSARY.
 *
 *	PARTIAL: identical to the case for output below.  This may occur
 *	    on a packet received directly from another Linux OS, e.g.,
 *	    a virtualised Linux kernel on the same host.  The packet can
 *	    be treated in the same way as UNNECESSARY except that on
 *	    output (i.e., forwarding) the checksum must be filled in
 *	    by the OS or the hardware.
 *
 * B. Checksumming on output.
 *
 *	NONE: skb is checksummed by protocol or csum is not required.
 *
 *	PARTIAL: device is required to csum packet as seen by hard_start_xmit
 *	from skb->csum_start to the end and to record the checksum
 *	at skb->csum_start + skb->csum_offset.
 *
 *	Device must show its capabilities in dev->features, set
 *	at device setup time.
 *	NETIF_F_HW_CSUM	- it is clever device, it is able to checksum
 *			  everything.
 *	NETIF_F_IP_CSUM - device is dumb. It is able to csum only
 *			  TCP/UDP over IPv4. Sigh. Vendors like this
 *			  way by an unknown reason. Though, see comment above
 *			  about CHECKSUM_UNNECESSARY. 8)
 *	NETIF_F_IPV6_CSUM about as dumb as the last one but does IPv6 instead.
 *
 *	UNNECESSARY: device will do per protocol specific csum. Protocol drivers
 *	that do not want net to perform the checksum calculation should use
 *	this flag in their outgoing skbs.
 *	NETIF_F_FCOE_CRC  this indicates the device can do FCoE FC CRC
 *			  offload. Correspondingly, the FCoE protocol driver
 *			  stack should use CHECKSUM_UNNECESSARY.
 *
 *	Any questions? No questions, good. 		--ANK
 */

struct net_device;
struct scatterlist;
struct pipe_inode_info;

#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
struct nf_conntrack {
	atomic_t use;
};
#endif

#ifdef CONFIG_BRIDGE_NETFILTER
struct nf_bridge_info {
	atomic_t		use;
	unsigned int		mask;
	struct net_device	*physindev;
	struct net_device	*physoutdev;
	unsigned long		data[32 / sizeof(unsigned long)];
};
#endif

struct sk_buff_head {
	/* These two members must be first. */
	struct sk_buff	*next;
	struct sk_buff	*prev;

	__u32		qlen;
	spinlock_t	lock;
};

struct sk_buff;

/* To allow 64K frame to be packed as single skb without frag_list we
 * require 64K/PAGE_SIZE pages plus 1 additional page to allow for
 * buffers which do not start on a page boundary.
 *
 * Since GRO uses frags we allocate at least 16 regardless of page
 * size.
 */
#if (65536/PAGE_SIZE + 1) < 16
#define MAX_SKB_FRAGS 16UL
#else
#define MAX_SKB_FRAGS (65536/PAGE_SIZE + 1)
#endif

typedef struct skb_frag_struct skb_frag_t;

struct skb_frag_struct {
	struct {
		struct page *p;
	} page;
#if (BITS_PER_LONG > 32) || (PAGE_SIZE >= 65536)
	__u32 page_offset;
	__u32 size;
#else
	__u16 page_offset;
	__u16 size;
#endif
};

static inline unsigned int skb_frag_size(const skb_frag_t *frag)
{
	return frag->size;
}

static inline void skb_frag_size_set(skb_frag_t *frag, unsigned int size)
{
	frag->size = size;
}

static inline void skb_frag_size_add(skb_frag_t *frag, int delta)
{
	frag->size += delta;
}

static inline void skb_frag_size_sub(skb_frag_t *frag, int delta)
{
	frag->size -= delta;
}

#define HAVE_HW_TIME_STAMP

/**
 * struct skb_shared_hwtstamps - hardware time stamps
 * @hwtstamp:	hardware time stamp transformed into duration
 *		since arbitrary point in time
 * @syststamp:	hwtstamp transformed to system time base
 *
 * Software time stamps generated by ktime_get_real() are stored in
 * skb->tstamp. The relation between the different kinds of time
 * stamps is as follows:
 *
 * syststamp and tstamp can be compared against each other in
 * arbitrary combinations.  The accuracy of a
 * syststamp/tstamp/"syststamp from other device" comparison is
 * limited by the accuracy of the transformation into system time
 * base. This depends on the device driver and its underlying
 * hardware.
 *
 * hwtstamps can only be compared against other hwtstamps from
 * the same device.
 *
 * This structure is attached to packets as part of the
 * &skb_shared_info. Use skb_hwtstamps() to get a pointer.
 */
struct skb_shared_hwtstamps {
	ktime_t	hwtstamp;
	ktime_t	syststamp;
};

/* Definitions for tx_flags in struct skb_shared_info */
enum {
	/* generate hardware time stamp */
	SKBTX_HW_TSTAMP = 1 << 0,

	/* generate software time stamp */
	SKBTX_SW_TSTAMP = 1 << 1,

	/* device driver is going to provide hardware time stamp */
	SKBTX_IN_PROGRESS = 1 << 2,

	/* device driver supports TX zero-copy buffers */
	SKBTX_DEV_ZEROCOPY = 1 << 3,

	/* generate wifi status information (where possible) */
	SKBTX_WIFI_STATUS = 1 << 4,
};

/*
 * The callback notifies userspace to release buffers when skb DMA is done in
 * lower device, the skb last reference should be 0 when calling this.
 * The zerocopy_success argument is true if zero copy transmit occurred,
 * false on data copy or out of memory error caused by data copy attempt.
 * The ctx field is used to track device context.
 * The desc field is used to track userspace buffer index.
 */
struct ubuf_info {
	void (*callback)(struct ubuf_info *, bool zerocopy_success);
	void *ctx;
	unsigned long desc;
};

/* This data is invariant across clones and lives at
 * the end of the header data, ie. at skb->end.
 */
struct skb_shared_info {
	unsigned char	nr_frags;
	__u8		tx_flags;
	unsigned short	gso_size;
	/* Warning: this field is not always filled in (UFO)! */
	unsigned short	gso_segs;
	unsigned short  gso_type;
	struct sk_buff	*frag_list;
	struct skb_shared_hwtstamps hwtstamps;
	__be32          ip6_frag_id;

	/*
	 * Warning : all fields before dataref are cleared in __alloc_skb()
	 */
	atomic_t	dataref;

	/* Intermediate layers must ensure that destructor_arg
	 * remains valid until skb destructor */
	void *		destructor_arg;

	/* must be last field, see pskb_expand_head() */
	skb_frag_t	frags[MAX_SKB_FRAGS];
};

/* We divide dataref into two halves.  The higher 16 bits hold references
 * to the payload part of skb->data.  The lower 16 bits hold references to
 * the entire skb->data.  A clone of a headerless skb holds the length of
 * the header in skb->hdr_len.
 *
 * All users must obey the rule that the skb->data reference count must be
 * greater than or equal to the payload reference count.
 *
 * Holding a reference to the payload part means that the user does not
 * care about modifications to the header part of skb->data.
 */
#define SKB_DATAREF_SHIFT 16
#define SKB_DATAREF_MASK ((1 << SKB_DATAREF_SHIFT) - 1)


enum {
	SKB_FCLONE_UNAVAILABLE,
	SKB_FCLONE_ORIG,
	SKB_FCLONE_CLONE,
};

enum {
	SKB_GSO_TCPV4 = 1 << 0,
	SKB_GSO_UDP = 1 << 1,

	/* This indicates the skb is from an untrusted source. */
	SKB_GSO_DODGY = 1 << 2,

	/* This indicates the tcp segment has CWR set. */
	SKB_GSO_TCP_ECN = 1 << 3,

	SKB_GSO_TCPV6 = 1 << 4,

	SKB_GSO_FCOE = 1 << 5,

	/* This indicates at least one fragment might be overwritten
	 * (as in vmsplice(), sendfile() ...)
	 * If we need to compute a TX checksum, we'll need to copy
	 * all frags to avoid possible bad checksum
	 */
	SKB_GSO_SHARED_FRAG = 1 << 6,
};

#if BITS_PER_LONG > 32
#define NET_SKBUFF_DATA_USES_OFFSET 1
#endif

#ifdef NET_SKBUFF_DATA_USES_OFFSET
typedef unsigned int sk_buff_data_t;
#else
typedef unsigned char *sk_buff_data_t;
#endif

#if defined(CONFIG_NF_DEFRAG_IPV4) || defined(CONFIG_NF_DEFRAG_IPV4_MODULE) || \
    defined(CONFIG_NF_DEFRAG_IPV6) || defined(CONFIG_NF_DEFRAG_IPV6_MODULE)
#define NET_SKBUFF_NF_DEFRAG_NEEDED 1
#endif

/** 
 *	struct sk_buff - socket buffer
 *	@next: Next buffer in list
 *	@prev: Previous buffer in list
 *	@tstamp: Time we arrived
 *	@sk: Socket we are owned by
 *	@dev: Device we arrived on/are leaving by
 *	@cb: Control buffer. Free for use by every layer. Put private vars here
 *	@_skb_refdst: destination entry (with norefcount bit)
 *	@sp: the security path, used for xfrm
 *	@len: Length of actual data
 *	@data_len: Data length
 *	@mac_len: Length of link layer header
 *	@hdr_len: writable header length of cloned skb
 *	@csum: Checksum (must include start/offset pair)
 *	@csum_start: Offset from skb->head where checksumming should start
 *	@csum_offset: Offset from csum_start where checksum should be stored
 *	@priority: Packet queueing priority
 *	@local_df: allow local fragmentation
 *	@cloned: Head may be cloned (check refcnt to be sure)
 *	@ip_summed: Driver fed us an IP checksum
 *	@nohdr: Payload reference only, must not modify header
 *	@nfctinfo: Relationship of this skb to the connection
 *	@pkt_type: Packet class
 *	@fclone: skbuff clone status
 *	@ipvs_property: skbuff is owned by ipvs
 *	@peeked: this packet has been seen already, so stats have been
 *		done for it, don't do them again
 *	@nf_trace: netfilter packet trace flag
 *	@protocol: Packet protocol from driver
 *	@destructor: Destruct function
 *	@nfct: Associated connection, if any
 *	@nfct_reasm: netfilter conntrack re-assembly pointer
 *	@nf_bridge: Saved data about a bridged frame - see br_netfilter.c
 *	@skb_iif: ifindex of device we arrived on
 *	@tc_index: Traffic control index
 *	@tc_verd: traffic control verdict
 *	@rxhash: the packet hash computed on receive
 *	@queue_mapping: Queue mapping for multiqueue devices
 *	@ndisc_nodetype: router type (from link layer)
 *	@ooo_okay: allow the mapping of a socket to a queue to be changed
 *	@l4_rxhash: indicate rxhash is a canonical 4-tuple hash over transport
 *		ports.
 *	@wifi_acked_valid: wifi_acked was set
 *	@wifi_acked: whether frame was acked on wifi or not
 *	@no_fcs:  Request NIC to treat last 4 bytes as Ethernet FCS
 *	@dma_cookie: a cookie to one of several possible DMA operations
 *		done by skb DMA functions
 *	@secmark: security marking
 *	@mark: Generic packet mark
 *	@dropcount: total number of sk_receive_queue overflows
 *	@vlan_tci: vlan tag control information
 *	@inner_transport_header: Inner transport layer header (encapsulation)
 *	@inner_network_header: Network layer header (encapsulation)
 *	@transport_header: Transport layer header
 *	@network_header: Network layer header
 *	@mac_header: Link layer header
 *	@tail: Tail pointer
 *	@end: End pointer
 *	@head: Head of buffer
 *	@data: Data head pointer
 *	@truesize: Buffer size
 *	@users: User count - see {datagram,tcp}.c
 */

struct sk_buff {
	/* These two members must be first. */
	struct sk_buff		*next;
	struct sk_buff		*prev;

	ktime_t			tstamp;

	struct sock		*sk;
	struct net_device	*dev;

	/*
	 * This is the control buffer. It is free to use for every
	 * layer. Please put your private variables there. If you
	 * want to keep them across layers you have to do a skb_clone()
	 * first. This is owned by whoever has the skb queued ATM.
	 */
	char			cb[48] __aligned(8);

	unsigned long		_skb_refdst;
#ifdef CONFIG_XFRM
	struct	sec_path	*sp;
#endif
	unsigned int		len,
				data_len;
	__u16			mac_len,
				hdr_len;
	union {
		__wsum		csum;
		struct {
			__u16	csum_start;
			__u16	csum_offset;
		};
	};
	__u32			priority;
	kmemcheck_bitfield_begin(flags1);
	__u8			local_df:1,
				cloned:1,
				ip_summed:2,
				nohdr:1,
				nfctinfo:3;
	__u8			pkt_type:3,
				fclone:2,
				ipvs_property:1,
				peeked:1,
				nf_trace:1;
	kmemcheck_bitfield_end(flags1);
	__be16			protocol;

	void			(*destructor)(struct sk_buff *skb);
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
	struct nf_conntrack	*nfct;
#endif
#ifdef NET_SKBUFF_NF_DEFRAG_NEEDED
	struct sk_buff		*nfct_reasm;
#endif
#ifdef CONFIG_BRIDGE_NETFILTER
	struct nf_bridge_info	*nf_bridge;
#endif

	int			skb_iif;

	__u32			rxhash;

	__u16			vlan_tci;

#ifdef CONFIG_NET_SCHED
	__u16			tc_index;	/* traffic control index */
#ifdef CONFIG_NET_CLS_ACT
	__u16			tc_verd;	/* traffic control verdict */
#endif
#endif

	__u16			queue_mapping;
	kmemcheck_bitfield_begin(flags2);
#ifdef CONFIG_IPV6_NDISC_NODETYPE
	__u8			ndisc_nodetype:2;
#endif
	__u8			pfmemalloc:1;
	__u8			ooo_okay:1;
	__u8			l4_rxhash:1;
	__u8			wifi_acked_valid:1;
	__u8			wifi_acked:1;
	__u8			no_fcs:1;
	__u8			head_frag:1;
	/* Encapsulation protocol and NIC drivers should use
	 * this flag to indicate to each other if the skb contains
	 * encapsulated packet or not and maybe use the inner packet
	 * headers if needed
	 */
	__u8			encapsulation:1;
	/* 7/9 bit hole (depending on ndisc_nodetype presence) */
	kmemcheck_bitfield_end(flags2);

#ifdef CONFIG_NET_DMA
	dma_cookie_t		dma_cookie;
#endif
#ifdef CONFIG_NETWORK_SECMARK
	__u32			secmark;
#endif
	union {
		__u32		mark;
		__u32		dropcount;
		__u32		avail_size;
	};

	sk_buff_data_t		inner_transport_header;
	sk_buff_data_t		inner_network_header;
	sk_buff_data_t		transport_header;
	sk_buff_data_t		network_header;
	sk_buff_data_t		mac_header;
	/* These elements must be at the end, see alloc_skb() for details.  */
	sk_buff_data_t		tail;
	sk_buff_data_t		end;
	unsigned char		*head,
				*data;
	unsigned int		truesize;
	atomic_t		users;
};

#ifdef __KERNEL__
/*
 *	Handling routines are only of interest to the kernel
 */
#include <linux/slab.h>


#define SKB_ALLOC_FCLONE	0x01
#define SKB_ALLOC_RX		0x02

/* Returns true if the skb was allocated from PFMEMALLOC reserves */
static inline bool skb_pfmemalloc(const struct sk_buff *skb)
{
	return unlikely(skb->pfmemalloc);
}

/*
 * skb might have a dst pointer attached, refcounted or not.
 * _skb_refdst low order bit is set if refcount was _not_ taken
 */
#define SKB_DST_NOREF	1UL
#define SKB_DST_PTRMASK	~(SKB_DST_NOREF)

/**
 * skb_dst - returns skb dst_entry
 * @skb: buffer
 *
 * Returns skb dst_entry, regardless of reference taken or not.
 */
static inline struct dst_entry *skb_dst(const struct sk_buff *skb)
{
	/* If refdst was not refcounted, check we still are in a 
	 * rcu_read_lock section
	 */
	WARN_ON((skb->_skb_refdst & SKB_DST_NOREF) &&
		!rcu_read_lock_held() &&
		!rcu_read_lock_bh_held());
	return (struct dst_entry *)(skb->_skb_refdst & SKB_DST_PTRMASK);
}

/**
 * skb_dst_set - sets skb dst
 * @skb: buffer
 * @dst: dst entry
 *
 * Sets skb dst, assuming a reference was taken on dst and should
 * be released by skb_dst_drop()
 */
static inline void skb_dst_set(struct sk_buff *skb, struct dst_entry *dst)
{
	skb->_skb_refdst = (unsigned long)dst;
}

extern void skb_dst_set_noref(struct sk_buff *skb, struct dst_entry *dst);

/**
 * skb_dst_is_noref - Test if skb dst isn't refcounted
 * @skb: buffer
 */
static inline bool skb_dst_is_noref(const struct sk_buff *skb)
{
	return (skb->_skb_refdst & SKB_DST_NOREF) && skb_dst(skb);
}

static inline struct rtable *skb_rtable(const struct sk_buff *skb)
{
	return (struct rtable *)skb_dst(skb);
}

extern void kfree_skb(struct sk_buff *skb);
extern void skb_tx_error(struct sk_buff *skb);
extern void consume_skb(struct sk_buff *skb);
extern void	       __kfree_skb(struct sk_buff *skb);
extern struct kmem_cache *skbuff_head_cache;

extern void kfree_skb_partial(struct sk_buff *skb, bool head_stolen);
extern bool skb_try_coalesce(struct sk_buff *to, struct sk_buff *from,
			     bool *fragstolen, int *delta_truesize);

extern struct sk_buff *__alloc_skb(unsigned int size,
				   gfp_t priority, int flags, int node);
extern struct sk_buff *build_skb(void *data, unsigned int frag_size);
static inline struct sk_buff *alloc_skb(unsigned int size,
					gfp_t priority)
{
	return __alloc_skb(size, priority, 0, NUMA_NO_NODE);
}

static inline struct sk_buff *alloc_skb_fclone(unsigned int size,
					       gfp_t priority)
{
	return __alloc_skb(size, priority, SKB_ALLOC_FCLONE, NUMA_NO_NODE);
}

extern struct sk_buff *skb_morph(struct sk_buff *dst, struct sk_buff *src);
extern int skb_copy_ubufs(struct sk_buff *skb, gfp_t gfp_mask);
extern struct sk_buff *skb_clone(struct sk_buff *skb,
				 gfp_t priority);
extern struct sk_buff *skb_copy(const struct sk_buff *skb,
				gfp_t priority);
extern struct sk_buff *__pskb_copy(struct sk_buff *skb,
				 int headroom, gfp_t gfp_mask);

extern int	       pskb_expand_head(struct sk_buff *skb,
					int nhead, int ntail,
					gfp_t gfp_mask);
extern struct sk_buff *skb_realloc_headroom(struct sk_buff *skb,
					    unsigned int headroom);
extern struct sk_buff *skb_copy_expand(const struct sk_buff *skb,
				       int newheadroom, int newtailroom,
				       gfp_t priority);
extern int	       skb_to_sgvec(struct sk_buff *skb,
				    struct scatterlist *sg, int offset,
				    int len);
extern int	       skb_cow_data(struct sk_buff *skb, int tailbits,
				    struct sk_buff **trailer);
extern int	       skb_pad(struct sk_buff *skb, int pad);
#define dev_kfree_skb(a)	consume_skb(a)

extern int skb_append_datato_frags(struct sock *sk, struct sk_buff *skb,
			int getfrag(void *from, char *to, int offset,
			int len,int odd, struct sk_buff *skb),
			void *from, int length);

struct skb_seq_state {
	__u32		lower_offset;
	__u32		upper_offset;
	__u32		frag_idx;
	__u32		stepped_offset;
	struct sk_buff	*root_skb;
	struct sk_buff	*cur_skb;
	__u8		*frag_data;
};

extern void	      skb_prepare_seq_read(struct sk_buff *skb,
					   unsigned int from, unsigned int to,
					   struct skb_seq_state *st);
extern unsigned int   skb_seq_read(unsigned int consumed, const u8 **data,
				   struct skb_seq_state *st);
extern void	      skb_abort_seq_read(struct skb_seq_state *st);

extern unsigned int   skb_find_text(struct sk_buff *skb, unsigned int from,
				    unsigned int to, struct ts_config *config,
				    struct ts_state *state);

extern void __skb_get_rxhash(struct sk_buff *skb);
static inline __u32 skb_get_rxhash(struct sk_buff *skb)
{
	if (!skb->l4_rxhash)
		__skb_get_rxhash(skb);

	return skb->rxhash;
}

#ifdef NET_SKBUFF_DATA_USES_OFFSET
static inline unsigned char *skb_end_pointer(const struct sk_buff *skb)
{
	return skb->head + skb->end;
}

static inline unsigned int skb_end_offset(const struct sk_buff *skb)
{
	return skb->end;
}
#else
static inline unsigned char *skb_end_pointer(const struct sk_buff *skb)
{
	return skb->end;
}

static inline unsigned int skb_end_offset(const struct sk_buff *skb)
{
	return skb->end - skb->head;
}
#endif

/* Internal */
#define skb_shinfo(SKB)	((struct skb_shared_info *)(skb_end_pointer(SKB)))

static inline struct skb_shared_hwtstamps *skb_hwtstamps(struct sk_buff *skb)
{
	return &skb_shinfo(skb)->hwtstamps;
}

/**
 *	skb_queue_empty - check if a queue is empty
 *	@list: queue head
 *
 *	Returns true if the queue is empty, false otherwise.
 */
static inline int skb_queue_empty(const struct sk_buff_head *list)
{
	return list->next == (struct sk_buff *)list;
}

/**
 *	skb_queue_is_last - check if skb is the last entry in the queue
 *	@list: queue head
 *	@skb: buffer
 *
 *	Returns true if @skb is the last buffer on the list.
 */
static inline bool skb_queue_is_last(const struct sk_buff_head *list,
				     const struct sk_buff *skb)
{
	return skb->next == (struct sk_buff *)list;
}

/**
 *	skb_queue_is_first - check if skb is the first entry in the queue
 *	@list: queue head
 *	@skb: buffer
 *
 *	Returns true if @skb is the first buffer on the list.
 */
static inline bool skb_queue_is_first(const struct sk_buff_head *list,
				      const struct sk_buff *skb)
{
	return skb->prev == (struct sk_buff *)list;
}

/**
 *	skb_queue_next - return the next packet in the queue
 *	@list: queue head
 *	@skb: current buffer
 *
 *	Return the next packet in @list after @skb.  It is only valid to
 *	call this if skb_queue_is_last() evaluates to false.
 */
static inline struct sk_buff *skb_queue_next(const struct sk_buff_head *list,
					     const struct sk_buff *skb)
{
	/* This BUG_ON may seem severe, but if we just return then we
	 * are going to dereference garbage.
	 */
	BUG_ON(skb_queue_is_last(list, skb));
	return skb->next;
}

/**
 *	skb_queue_prev - return the prev packet in the queue
 *	@list: queue head
 *	@skb: current buffer
 *
 *	Return the prev packet in @list before @skb.  It is only valid to
 *	call this if skb_queue_is_first() evaluates to false.
 */
static inline struct sk_buff *skb_queue_prev(const struct sk_buff_head *list,
					     const struct sk_buff *skb)
{
	/* This BUG_ON may seem severe, but if we just return then we
	 * are going to dereference garbage.
	 */
	BUG_ON(skb_queue_is_first(list, skb));
	return skb->prev;
}

/**
 *	skb_get - reference buffer
 *	@skb: buffer to reference
 *
 *	Makes another reference to a socket buffer and returns a pointer
 *	to the buffer.
 */
static inline struct sk_buff *skb_get(struct sk_buff *skb)
{
	atomic_inc(&skb->users);
	return skb;
}

/*
 * If users == 1, we are the only owner and are can avoid redundant
 * atomic change.
 */

/**
 *	skb_cloned - is the buffer a clone
 *	@skb: buffer to check
 *
 *	Returns true if the buffer was generated with skb_clone() and is
 *	one of multiple shared copies of the buffer. Cloned buffers are
 *	shared data so must not be written to under normal circumstances.
 */
static inline int skb_cloned(const struct sk_buff *skb)
{
	return skb->cloned &&
	       (atomic_read(&skb_shinfo(skb)->dataref) & SKB_DATAREF_MASK) != 1;
}

/**
 *	skb_header_cloned - is the header a clone
 *	@skb: buffer to check
 *
 *	Returns true if modifying the header part of the buffer requires
 *	the data to be copied.
 */
static inline int skb_header_cloned(const struct sk_buff *skb)
{
	int dataref;

	if (!skb->cloned)
		return 0;

	dataref = atomic_read(&skb_shinfo(skb)->dataref);
	dataref = (dataref & SKB_DATAREF_MASK) - (dataref >> SKB_DATAREF_SHIFT);
	return dataref != 1;
}

/**
 *	skb_header_release - release reference to header
 *	@skb: buffer to operate on
 *
 *	Drop a reference to the header part of the buffer.  This is done
 *	by acquiring a payload reference.  You must not read from the header
 *	part of skb->data after this.
 */
static inline void skb_header_release(struct sk_buff *skb)
{
	BUG_ON(skb->nohdr);
	skb->nohdr = 1;
	atomic_add(1 << SKB_DATAREF_SHIFT, &skb_shinfo(skb)->dataref);
}

/**
 *	skb_shared - is the buffer shared
 *	@skb: buffer to check
 *
 *	Returns true if more than one person has a reference to this
 *	buffer.
 */
static inline int skb_shared(const struct sk_buff *skb)
{
	return atomic_read(&skb->users) != 1;
}

/**
 *	skb_share_check - check if buffer is shared and if so clone it
 *	@skb: buffer to check
 *	@pri: priority for memory allocation
 *
 *	If the buffer is shared the buffer is cloned and the old copy
 *	drops a reference. A new clone with a single reference is returned.
 *	If the buffer is not shared the original buffer is returned. When
 *	being called from interrupt status or with spinlocks held pri must
 *	be GFP_ATOMIC.
 *
 *	NULL is returned on a memory allocation failure.
 */
static inline struct sk_buff *skb_share_check(struct sk_buff *skb, gfp_t pri)
{
	might_sleep_if(pri & __GFP_WAIT);
	if (skb_shared(skb)) {
		struct sk_buff *nskb = skb_clone(skb, pri);

		if (likely(nskb))
			consume_skb(skb);
		else
			kfree_skb(skb);
		skb = nskb;
	}
	return skb;
}

/*
 *	Copy shared buffers into a new sk_buff. We effectively do COW on
 *	packets to handle cases where we have a local reader and forward
 *	and a couple of other messy ones. The normal one is tcpdumping
 *	a packet thats being forwarded.
 */

/**
 *	skb_unshare - make a copy of a shared buffer
 *	@skb: buffer to check
 *	@pri: priority for memory allocation
 *
 *	If the socket buffer is a clone then this function creates a new
 *	copy of the data, drops a reference count on the old copy and returns
 *	the new copy with the reference count at 1. If the buffer is not a clone
 *	the original buffer is returned. When called with a spinlock held or
 *	from interrupt state @pri must be %GFP_ATOMIC
 *
 *	%NULL is returned on a memory allocation failure.
 */
static inline struct sk_buff *skb_unshare(struct sk_buff *skb,
					  gfp_t pri)
{
	might_sleep_if(pri & __GFP_WAIT);
	if (skb_cloned(skb)) {
		struct sk_buff *nskb = skb_copy(skb, pri);
		kfree_skb(skb);	/* Free our shared copy */
		skb = nskb;
	}
	return skb;
}

/**
 *	skb_peek - peek at the head of an &sk_buff_head
 *	@list_: list to peek at
 *
 *	Peek an &sk_buff. Unlike most other operations you _MUST_
 *	be careful with this one. A peek leaves the buffer on the
 *	list and someone else may run off with it. You must hold
 *	the appropriate locks or have a private queue to do this.
 *
 *	Returns %NULL for an empty list or a pointer to the head element.
 *	The reference count is not incremented and the reference is therefore
 *	volatile. Use with caution.
 */
static inline struct sk_buff *skb_peek(const struct sk_buff_head *list_)
{
	struct sk_buff *skb = list_->next;

	if (skb == (struct sk_buff *)list_)
		skb = NULL;
	return skb;
}

/**
 *	skb_peek_next - peek skb following the given one from a queue
 *	@skb: skb to start from
 *	@list_: list to peek at
 *
 *	Returns %NULL when the end of the list is met or a pointer to the
 *	next element. The reference count is not incremented and the
 *	reference is therefore volatile. Use with caution.
 */
static inline struct sk_buff *skb_peek_next(struct sk_buff *skb,
		const struct sk_buff_head *list_)
{
	struct sk_buff *next = skb->next;

	if (next == (struct sk_buff *)list_)
		next = NULL;
	return next;
}

/**
 *	skb_peek_tail - peek at the tail of an &sk_buff_head
 *	@list_: list to peek at
 *
 *	Peek an &sk_buff. Unlike most other operations you _MUST_
 *	be careful with this one. A peek leaves the buffer on the
 *	list and someone else may run off with it. You must hold
 *	the appropriate locks or have a private queue to do this.
 *
 *	Returns %NULL for an empty list or a pointer to the tail element.
 *	The reference count is not incremented and the reference is therefore
 *	volatile. Use with caution.
 */
static inline struct sk_buff *skb_peek_tail(const struct sk_buff_head *list_)
{
	struct sk_buff *skb = list_->prev;

	if (skb == (struct sk_buff *)list_)
		skb = NULL;
	return skb;

}

/**
 *	skb_queue_len	- get queue length
 *	@list_: list to measure
 *
 *	Return the length of an &sk_buff queue.
 */
static inline __u32 skb_queue_len(const struct sk_buff_head *list_)
{
	return list_->qlen;
}

/**
 *	__skb_queue_head_init - initialize non-spinlock portions of sk_buff_head
 *	@list: queue to initialize
 *
 *	This initializes only the list and queue length aspects of
 *	an sk_buff_head object.  This allows to initialize the list
 *	aspects of an sk_buff_head without reinitializing things like
 *	the spinlock.  It can also be used for on-stack sk_buff_head
 *	objects where the spinlock is known to not be used.
 */
static inline void __skb_queue_head_init(struct sk_buff_head *list)
{
	list->prev = list->next = (struct sk_buff *)list;
	list->qlen = 0;
}

/*
 * This function creates a split out lock class for each invocation;
 * this is needed for now since a whole lot of users of the skb-queue
 * infrastructure in drivers have different locking usage (in hardirq)
 * than the networking core (in softirq only). In the long run either the
 * network layer or drivers should need annotation to consolidate the
 * main types of usage into 3 classes.
 */
static inline void skb_queue_head_init(struct sk_buff_head *list)
{
	spin_lock_init(&list->lock);
	__skb_queue_head_init(list);
}

static inline void skb_queue_head_init_class(struct sk_buff_head *list,
		struct lock_class_key *class)
{
	skb_queue_head_init(list);
	lockdep_set_class(&list->lock, class);
}

/*
 *	Insert an sk_buff on a list.
 *
 *	The "__skb_xxxx()" functions are the non-atomic ones that
 *	can only be called with interrupts disabled.
 */
extern void        skb_insert(struct sk_buff *old, struct sk_buff *newsk, struct sk_buff_head *list);
static inline void __skb_insert(struct sk_buff *newsk,
				struct sk_buff *prev, struct sk_buff *next,
				struct sk_buff_head *list)
{
	newsk->next = next;
	newsk->prev = prev;
	next->prev  = prev->next = newsk;
	list->qlen++;
}

static inline void __skb_queue_splice(const struct sk_buff_head *list,
				      struct sk_buff *prev,
				      struct sk_buff *next)
{
	struct sk_buff *first = list->next;
	struct sk_buff *last = list->prev;

	first->prev = prev;
	prev->next = first;

	last->next = next;
	next->prev = last;
}

/**
 *	skb_queue_splice - join two skb lists, this is designed for stacks
 *	@list: the new list to add
 *	@head: the place to add it in the first list
 */
static inline void skb_queue_splice(const struct sk_buff_head *list,
				    struct sk_buff_head *head)
{
	if (!skb_queue_empty(list)) {
		__skb_queue_splice(list, (struct sk_buff *) head, head->next);
		head->qlen += list->qlen;
	}
}

/**
 *	skb_queue_splice_init - join two skb lists and reinitialise the emptied list
 *	@list: the new list to add
 *	@head: the place to add it in the first list
 *
 *	The list at @list is reinitialised
 */
static inline void skb_queue_splice_init(struct sk_buff_head *list,
					 struct sk_buff_head *head)
{
	if (!skb_queue_empty(list)) {
		__skb_queue_splice(list, (struct sk_buff *) head, head->next);
		head->qlen += list->qlen;
		__skb_queue_head_init(list);
	}
}

/**
 *	skb_queue_splice_tail - join two skb lists, each list being a queue
 *	@list: the new list to add
 *	@head: the place to add it in the first list
 */
static inline void skb_queue_splice_tail(const struct sk_buff_head *list,
					 struct sk_buff_head *head)
{
	if (!skb_queue_empty(list)) {
		__skb_queue_splice(list, head->prev, (struct sk_buff *) head);
		head->qlen += list->qlen;
	}
}

/**
 *	skb_queue_splice_tail_init - join two skb lists and reinitialise the emptied list
 *	@list: the new list to add
 *	@head: the place to add it in the first list
 *
 *	Each of the lists is a queue.
 *	The list at @list is reinitialised
 */
static inline void skb_queue_splice_tail_init(struct sk_buff_head *list,
					      struct sk_buff_head *head)
{
	if (!skb_queue_empty(list)) {
		__skb_queue_splice(list, head->prev, (struct sk_buff *) head);
		head->qlen += list->qlen;
		__skb_queue_head_init(list);
	}
}

/**
 *	__skb_queue_after - queue a buffer at the list head
 *	@list: list to use
 *	@prev: place after this buffer
 *	@newsk: buffer to queue
 *
 *	Queue a buffer int the middle of a list. This function takes no locks
 *	and you must therefore hold required locks before calling it.
 *
 *	A buffer cannot be placed on two lists at the same time.
 */
static inline void __skb_queue_after(struct sk_buff_head *list,
				     struct sk_buff *prev,
				     struct sk_buff *newsk)
{
	__skb_insert(newsk, prev, prev->next, list);
}

extern void skb_append(struct sk_buff *old, struct sk_buff *newsk,
		       struct sk_buff_head *list);

static inline void __skb_queue_before(struct sk_buff_head *list,
				      struct sk_buff *next,
				      struct sk_buff *newsk)
{
	__skb_insert(newsk, next->prev, next, list);
}

/**
 *	__skb_queue_head - queue a buffer at the list head
 *	@list: list to use
 *	@newsk: buffer to queue
 *
 *	Queue a buffer at the start of a list. This function takes no locks
 *	and you must therefore hold required locks before calling it.
 *
 *	A buffer cannot be placed on two lists at the same time.
 */
extern void skb_queue_head(struct sk_buff_head *list, struct sk_buff *newsk);
static inline void __skb_queue_head(struct sk_buff_head *list,
				    struct sk_buff *newsk)
{
	__skb_queue_after(list, (struct sk_buff *)list, newsk);
}

/**
 *	__skb_queue_tail - queue a buffer at the list tail
 *	@list: list to use
 *	@newsk: buffer to queue
 *
 *	Queue a buffer at the end of a list. This function takes no locks
 *	and you must therefore hold required locks before calling it.
 *
 *	A buffer cannot be placed on two lists at the same time.
 */
extern void skb_queue_tail(struct sk_buff_head *list, struct sk_buff *newsk);
static inline void __skb_queue_tail(struct sk_buff_head *list,
				   struct sk_buff *newsk)
{
	__skb_queue_before(list, (struct sk_buff *)list, newsk);
}

/*
 * remove sk_buff from list. _Must_ be called atomically, and with
 * the list known..
 */
extern void	   skb_unlink(struct sk_buff *skb, struct sk_buff_head *list);
static inline void __skb_unlink(struct sk_buff *skb, struct sk_buff_head *list)
{
	struct sk_buff *next, *prev;

	list->qlen--;
	next	   = skb->next;
	prev	   = skb->prev;
	skb->next  = skb->prev = NULL;
	next->prev = prev;
	prev->next = next;
}

/**
 *	__skb_dequeue - remove from the head of the queue
 *	@list: list to dequeue from
 *
 *	Remove the head of the list. This function does not take any locks
 *	so must be used with appropriate locks held only. The head item is
 *	returned or %NULL if the list is empty.
 */
extern struct sk_buff *skb_dequeue(struct sk_buff_head *list);
static inline struct sk_buff *__skb_dequeue(struct sk_buff_head *list)
{
	struct sk_buff *skb = skb_peek(list);
	if (skb)
		__skb_unlink(skb, list);
	return skb;
}

/**
 *	__skb_dequeue_tail - remove from the tail of the queue
 *	@list: list to dequeue from
 *
 *	Remove the tail of the list. This function does not take any locks
 *	so must be used with appropriate locks held only. The tail item is
 *	returned or %NULL if the list is empty.
 */
extern struct sk_buff *skb_dequeue_tail(struct sk_buff_head *list);
static inline struct sk_buff *__skb_dequeue_tail(struct sk_buff_head *list)
{
	struct sk_buff *skb = skb_peek_tail(list);
	if (skb)
		__skb_unlink(skb, list);
	return skb;
}


static inline bool skb_is_nonlinear(const struct sk_buff *skb)
{
	return skb->data_len;
}

static inline unsigned int skb_headlen(const struct sk_buff *skb)
{
	return skb->len - skb->data_len;
}

static inline int skb_pagelen(const struct sk_buff *skb)
{
	int i, len = 0;

	for (i = (int)skb_shinfo(skb)->nr_frags - 1; i >= 0; i--)
		len += skb_frag_size(&skb_shinfo(skb)->frags[i]);
	return len + skb_headlen(skb);
}

/**
 * __skb_fill_page_desc - initialise a paged fragment in an skb
 * @skb: buffer containing fragment to be initialised
 * @i: paged fragment index to initialise
 * @page: the page to use for this fragment
 * @off: the offset to the data with @page
 * @size: the length of the data
 *
 * Initialises the @i'th fragment of @skb to point to &size bytes at
 * offset @off within @page.
 *
 * Does not take any additional reference on the fragment.
 */
static inline void __skb_fill_page_desc(struct sk_buff *skb, int i,
					struct page *page, int off, int size)
{
	skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

	/*
	 * Propagate page->pfmemalloc to the skb if we can. The problem is
	 * that not all callers have unique ownership of the page. If
	 * pfmemalloc is set, we check the mapping as a mapping implies
	 * page->index is set (index and pfmemalloc share space).
	 * If it's a valid mapping, we cannot use page->pfmemalloc but we
	 * do not lose pfmemalloc information as the pages would not be
	 * allocated using __GFP_MEMALLOC.
	 */
	if (page->pfmemalloc && !page->mapping)
		skb->pfmemalloc	= true;
	frag->page.p		  = page;
	frag->page_offset	  = off;
	skb_frag_size_set(frag, size);
}

/**
 * skb_fill_page_desc - initialise a paged fragment in an skb
 * @skb: buffer containing fragment to be initialised
 * @i: paged fragment index to initialise
 * @page: the page to use for this fragment
 * @off: the offset to the data with @page
 * @size: the length of the data
 *
 * As per __skb_fill_page_desc() -- initialises the @i'th fragment of
 * @skb to point to &size bytes at offset @off within @page. In
 * addition updates @skb such that @i is the last fragment.
 *
 * Does not take any additional reference on the fragment.
 */
static inline void skb_fill_page_desc(struct sk_buff *skb, int i,
				      struct page *page, int off, int size)
{
	__skb_fill_page_desc(skb, i, page, off, size);
	skb_shinfo(skb)->nr_frags = i + 1;
}

extern void skb_add_rx_frag(struct sk_buff *skb, int i, struct page *page,
			    int off, int size, unsigned int truesize);

#define SKB_PAGE_ASSERT(skb) 	BUG_ON(skb_shinfo(skb)->nr_frags)
#define SKB_FRAG_ASSERT(skb) 	BUG_ON(skb_has_frag_list(skb))
#define SKB_LINEAR_ASSERT(skb)  BUG_ON(skb_is_nonlinear(skb))

#ifdef NET_SKBUFF_DATA_USES_OFFSET
static inline unsigned char *skb_tail_pointer(const struct sk_buff *skb)
{
	return skb->head + skb->tail;
}

static inline void skb_reset_tail_pointer(struct sk_buff *skb)
{
	skb->tail = skb->data - skb->head;
}

static inline void skb_set_tail_pointer(struct sk_buff *skb, const int offset)
{
	skb_reset_tail_pointer(skb);
	skb->tail += offset;
}
#else /* NET_SKBUFF_DATA_USES_OFFSET */
static inline unsigned char *skb_tail_pointer(const struct sk_buff *skb)
{
	return skb->tail;
}

static inline void skb_reset_tail_pointer(struct sk_buff *skb)
{
	skb->tail = skb->data;
}

static inline void skb_set_tail_pointer(struct sk_buff *skb, const int offset)
{
	skb->tail = skb->data + offset;
}

#endif /* NET_SKBUFF_DATA_USES_OFFSET */

/*
 *	Add data to an sk_buff
 */
extern unsigned char *skb_put(struct sk_buff *skb, unsigned int len);
static inline unsigned char *__skb_put(struct sk_buff *skb, unsigned int len)
{
	unsigned char *tmp = skb_tail_pointer(skb);
	SKB_LINEAR_ASSERT(skb);
	skb->tail += len;
	skb->len  += len;
	return tmp;
}

extern unsigned char *skb_push(struct sk_buff *skb, unsigned int len);
static inline unsigned char *__skb_push(struct sk_buff *skb, unsigned int len)
{
	skb->data -= len;
	skb->len  += len;
	return skb->data;
}

extern unsigned char *skb_pull(struct sk_buff *skb, unsigned int len);
static inline unsigned char *__skb_pull(struct sk_buff *skb, unsigned int len)
{
	skb->len -= len;
	BUG_ON(skb->len < skb->data_len);
	return skb->data += len;
}

static inline unsigned char *skb_pull_inline(struct sk_buff *skb, unsigned int len)
{
	return unlikely(len > skb->len) ? NULL : __skb_pull(skb, len);
}

extern unsigned char *__pskb_pull_tail(struct sk_buff *skb, int delta);

static inline unsigned char *__pskb_pull(struct sk_buff *skb, unsigned int len)
{
	if (len > skb_headlen(skb) &&
	    !__pskb_pull_tail(skb, len - skb_headlen(skb)))
		return NULL;
	skb->len -= len;
	return skb->data += len;
}

static inline unsigned char *pskb_pull(struct sk_buff *skb, unsigned int len)
{
	return unlikely(len > skb->len) ? NULL : __pskb_pull(skb, len);
}

static inline int pskb_may_pull(struct sk_buff *skb, unsigned int len)
{
	if (likely(len <= skb_headlen(skb)))
		return 1;
	if (unlikely(len > skb->len))
		return 0;
	return __pskb_pull_tail(skb, len - skb_headlen(skb)) != NULL;
}

/**
 *	skb_headroom - bytes at buffer head
 *	@skb: buffer to check
 *
 *	Return the number of bytes of free space at the head of an &sk_buff.
 */
static inline unsigned int skb_headroom(const struct sk_buff *skb)
{
	return skb->data - skb->head;
}

/**
 *	skb_tailroom - bytes at buffer end
 *	@skb: buffer to check
 *
 *	Return the number of bytes of free space at the tail of an sk_buff
 */
static inline int skb_tailroom(const struct sk_buff *skb)
{
	return skb_is_nonlinear(skb) ? 0 : skb->end - skb->tail;
}

/**
 *	skb_availroom - bytes at buffer end
 *	@skb: buffer to check
 *
 *	Return the number of bytes of free space at the tail of an sk_buff
 *	allocated by sk_stream_alloc()
 */
static inline int skb_availroom(const struct sk_buff *skb)
{
	return skb_is_nonlinear(skb) ? 0 : skb->avail_size - skb->len;
}

/**
 *	skb_reserve - adjust headroom
 *	@skb: buffer to alter
 *	@len: bytes to move
 *
 *	Increase the headroom of an empty &sk_buff by reducing the tail
 *	room. This is only allowed for an empty buffer.
 */
static inline void skb_reserve(struct sk_buff *skb, int len)
{
	skb->data += len;
	skb->tail += len;
}

static inline void skb_reset_inner_headers(struct sk_buff *skb)
{
	skb->inner_network_header = skb->network_header;
	skb->inner_transport_header = skb->transport_header;
}

static inline void skb_reset_mac_len(struct sk_buff *skb)
{
	skb->mac_len = skb->network_header - skb->mac_header;
}

#ifdef NET_SKBUFF_DATA_USES_OFFSET
static inline unsigned char *skb_inner_transport_header(const struct sk_buff
							*skb)
{
	return skb->head + skb->inner_transport_header;
}

static inline void skb_reset_inner_transport_header(struct sk_buff *skb)
{
	skb->inner_transport_header = skb->data - skb->head;
}

static inline void skb_set_inner_transport_header(struct sk_buff *skb,
						   const int offset)
{
	skb_reset_inner_transport_header(skb);
	skb->inner_transport_header += offset;
}

static inline unsigned char *skb_inner_network_header(const struct sk_buff *skb)
{
	return skb->head + skb->inner_network_header;
}

static inline void skb_reset_inner_network_header(struct sk_buff *skb)
{
	skb->inner_network_header = skb->data - skb->head;
}

static inline void skb_set_inner_network_header(struct sk_buff *skb,
						const int offset)
{
	skb_reset_inner_network_header(skb);
	skb->inner_network_header += offset;
}

static inline bool skb_transport_header_was_set(const struct sk_buff *skb)
{
	return skb->transport_header != ~0U;
}

static inline unsigned char *skb_transport_header(const struct sk_buff *skb)
{
	return skb->head + skb->transport_header;
}

static inline void skb_reset_transport_header(struct sk_buff *skb)
{
	skb->transport_header = skb->data - skb->head;
}

static inline void skb_set_transport_header(struct sk_buff *skb,
					    const int offset)
{
	skb_reset_transport_header(skb);
	skb->transport_header += offset;
}

static inline unsigned char *skb_network_header(const struct sk_buff *skb)
{
	return skb->head + skb->network_header;
}

static inline void skb_reset_network_header(struct sk_buff *skb)
{
	skb->network_header = skb->data - skb->head;
}

static inline void skb_set_network_header(struct sk_buff *skb, const int offset)
{
	skb_reset_network_header(skb);
	skb->network_header += offset;
}

static inline unsigned char *skb_mac_header(const struct sk_buff *skb)
{
	return skb->head + skb->mac_header;
}

static inline int skb_mac_header_was_set(const struct sk_buff *skb)
{
	return skb->mac_header != ~0U;
}

static inline void skb_reset_mac_header(struct sk_buff *skb)
{
	skb->mac_header = skb->data - skb->head;
}

static inline void skb_set_mac_header(struct sk_buff *skb, const int offset)
{
	skb_reset_mac_header(skb);
	skb->mac_header += offset;
}

#else /* NET_SKBUFF_DATA_USES_OFFSET */
static inline unsigned char *skb_inner_transport_header(const struct sk_buff
							*skb)
{
	return skb->inner_transport_header;
}

static inline void skb_reset_inner_transport_header(struct sk_buff *skb)
{
	skb->inner_transport_header = skb->data;
}

static inline void skb_set_inner_transport_header(struct sk_buff *skb,
						   const int offset)
{
	skb->inner_transport_header = skb->data + offset;
}

static inline unsigned char *skb_inner_network_header(const struct sk_buff *skb)
{
	return skb->inner_network_header;
}

static inline void skb_reset_inner_network_header(struct sk_buff *skb)
{
	skb->inner_network_header = skb->data;
}

static inline void skb_set_inner_network_header(struct sk_buff *skb,
						const int offset)
{
	skb->inner_network_header = skb->data + offset;
}

static inline bool skb_transport_header_was_set(const struct sk_buff *skb)
{
	return skb->transport_header != NULL;
}

static inline unsigned char *skb_transport_header(const struct sk_buff *skb)
{
	return skb->transport_header;
}

static inline void skb_reset_transport_header(struct sk_buff *skb)
{
	skb->transport_header = skb->data;
}

static inline void skb_set_transport_header(struct sk_buff *skb,
					    const int offset)
{
	skb->transport_header = skb->data + offset;
}

static inline unsigned char *skb_network_header(const struct sk_buff *skb)
{
	return skb->network_header;
}

static inline void skb_reset_network_header(struct sk_buff *skb)
{
	skb->network_header = skb->data;
}

static inline void skb_set_network_header(struct sk_buff *skb, const int offset)
{
	skb->network_header = skb->data + offset;
}

static inline unsigned char *skb_mac_header(const struct sk_buff *skb)
{
	return skb->mac_header;
}

static inline int skb_mac_header_was_set(const struct sk_buff *skb)
{
	return skb->mac_header != NULL;
}

static inline void skb_reset_mac_header(struct sk_buff *skb)
{
	skb->mac_header = skb->data;
}

static inline void skb_set_mac_header(struct sk_buff *skb, const int offset)
{
	skb->mac_header = skb->data + offset;
}
#endif /* NET_SKBUFF_DATA_USES_OFFSET */

static inline void skb_mac_header_rebuild(struct sk_buff *skb)
{
	if (skb_mac_header_was_set(skb)) {
		const unsigned char *old_mac = skb_mac_header(skb);

		skb_set_mac_header(skb, -skb->mac_len);
		memmove(skb_mac_header(skb), old_mac, skb->mac_len);
	}
}

static inline int skb_checksum_start_offset(const struct sk_buff *skb)
{
	return skb->csum_start - skb_headroom(skb);
}

static inline int skb_transport_offset(const struct sk_buff *skb)
{
	return skb_transport_header(skb) - skb->data;
}

static inline u32 skb_network_header_len(const struct sk_buff *skb)
{
	return skb->transport_header - skb->network_header;
}

static inline u32 skb_inner_network_header_len(const struct sk_buff *skb)
{
	return skb->inner_transport_header - skb->inner_network_header;
}

static inline int skb_network_offset(const struct sk_buff *skb)
{
	return skb_network_header(skb) - skb->data;
}

static inline int skb_inner_network_offset(const struct sk_buff *skb)
{
	return skb_inner_network_header(skb) - skb->data;
}

static inline int pskb_network_may_pull(struct sk_buff *skb, unsigned int len)
{
	return pskb_may_pull(skb, skb_network_offset(skb) + len);
}

/*
 * CPUs often take a performance hit when accessing unaligned memory
 * locations. The actual performance hit varies, it can be small if the
 * hardware handles it or large if we have to take an exception and fix it
 * in software.
 *
 * Since an ethernet header is 14 bytes network drivers often end up with
 * the IP header at an unaligned offset. The IP header can be aligned by
 * shifting the start of the packet by 2 bytes. Drivers should do this
 * with:
 *
 * skb_reserve(skb, NET_IP_ALIGN);
 *
 * The downside to this alignment of the IP header is that the DMA is now
 * unaligned. On some architectures the cost of an unaligned DMA is high
 * and this cost outweighs the gains made by aligning the IP header.
 *
 * Since this trade off varies between architectures, we allow NET_IP_ALIGN
 * to be overridden.
 */
#ifndef NET_IP_ALIGN
#define NET_IP_ALIGN	2
#endif

/*
 * The networking layer reserves some headroom in skb data (via
 * dev_alloc_skb). This is used to avoid having to reallocate skb data when
 * the header has to grow. In the default case, if the header has to grow
 * 32 bytes or less we avoid the reallocation.
 *
 * Unfortunately this headroom changes the DMA alignment of the resulting
 * network packet. As for NET_IP_ALIGN, this unaligned DMA is expensive
 * on some architectures. An architecture can override this value,
 * perhaps setting it to a cacheline in size (since that will maintain
 * cacheline alignment of the DMA). It must be a power of 2.
 *
 * Various parts of the networking layer expect at least 32 bytes of
 * headroom, you should not reduce this.
 *
 * Using max(32, L1_CACHE_BYTES) makes sense (especially with RPS)
 * to reduce average number of cache lines per packet.
 * get_rps_cpus() for example only access one 64 bytes aligned block :
 * NET_IP_ALIGN(2) + ethernet_header(14) + IP_header(20/40) + ports(8)
 */
#ifndef NET_SKB_PAD
#define NET_SKB_PAD	max(32, L1_CACHE_BYTES)
#endif

extern int ___pskb_trim(struct sk_buff *skb, unsigned int len);

static inline void __skb_trim(struct sk_buff *skb, unsigned int len)
{
	if (unlikely(skb_is_nonlinear(skb))) {
		WARN_ON(1);
		return;
	}
	skb->len = len;
	skb_set_tail_pointer(skb, len);
}

extern void skb_trim(struct sk_buff *skb, unsigned int len);

static inline int __pskb_trim(struct sk_buff *skb, unsigned int len)
{
	if (skb->data_len)
		return ___pskb_trim(skb, len);
	__skb_trim(skb, len);
	return 0;
}

static inline int pskb_trim(struct sk_buff *skb, unsigned int len)
{
	return (len < skb->len) ? __pskb_trim(skb, len) : 0;
}

/**
 *	pskb_trim_unique - remove end from a paged unique (not cloned) buffer
 *	@skb: buffer to alter
 *	@len: new length
 *
 *	This is identical to pskb_trim except that the caller knows that
 *	the skb is not cloned so we should never get an error due to out-
 *	of-memory.
 */
static inline void pskb_trim_unique(struct sk_buff *skb, unsigned int len)
{
	int err = pskb_trim(skb, len);
	BUG_ON(err);
}

/**
 *	skb_orphan - orphan a buffer
 *	@skb: buffer to orphan
 *
 *	If a buffer currently has an owner then we call the owner's
 *	destructor function and make the @skb unowned. The buffer continues
 *	to exist but is no longer charged to its former owner.
 */
static inline void skb_orphan(struct sk_buff *skb)
{
	if (skb->destructor)
		skb->destructor(skb);
	skb->destructor = NULL;
	skb->sk		= NULL;
}

/**
 *	skb_orphan_frags - orphan the frags contained in a buffer
 *	@skb: buffer to orphan frags from
 *	@gfp_mask: allocation mask for replacement pages
 *
 *	For each frag in the SKB which needs a destructor (i.e. has an
 *	owner) create a copy of that frag and release the original
 *	page by calling the destructor.
 */
static inline int skb_orphan_frags(struct sk_buff *skb, gfp_t gfp_mask)
{
	if (likely(!(skb_shinfo(skb)->tx_flags & SKBTX_DEV_ZEROCOPY)))
		return 0;
	return skb_copy_ubufs(skb, gfp_mask);
}

/**
 *	__skb_queue_purge - empty a list
 *	@list: list to empty
 *
 *	Delete all buffers on an &sk_buff list. Each buffer is removed from
 *	the list and one reference dropped. This function does not take the
 *	list lock and the caller must hold the relevant locks to use it.
 */
extern void skb_queue_purge(struct sk_buff_head *list);
static inline void __skb_queue_purge(struct sk_buff_head *list)
{
	struct sk_buff *skb;
	while ((skb = __skb_dequeue(list)) != NULL)
		kfree_skb(skb);
}

#define NETDEV_FRAG_PAGE_MAX_ORDER get_order(32768)
#define NETDEV_FRAG_PAGE_MAX_SIZE  (PAGE_SIZE << NETDEV_FRAG_PAGE_MAX_ORDER)
#define NETDEV_PAGECNT_MAX_BIAS	   NETDEV_FRAG_PAGE_MAX_SIZE

extern void *netdev_alloc_frag(unsigned int fragsz);

extern struct sk_buff *__netdev_alloc_skb(struct net_device *dev,
					  unsigned int length,
					  gfp_t gfp_mask);

/**
 *	netdev_alloc_skb - allocate an skbuff for rx on a specific device
 *	@dev: network device to receive on
 *	@length: length to allocate
 *
 *	Allocate a new &sk_buff and assign it a usage count of one. The
 *	buffer has unspecified headroom built in. Users should allocate
 *	the headroom they think they need without accounting for the
 *	built in space. The built in space is used for optimisations.
 *
 *	%NULL is returned if there is no free memory. Although this function
 *	allocates memory it can be called from an interrupt.
 */
static inline struct sk_buff *netdev_alloc_skb(struct net_device *dev,
					       unsigned int length)
{
	return __netdev_alloc_skb(dev, length, GFP_ATOMIC);
}

/* legacy helper around __netdev_alloc_skb() */
static inline struct sk_buff *__dev_alloc_skb(unsigned int length,
					      gfp_t gfp_mask)
{
	return __netdev_alloc_skb(NULL, length, gfp_mask);
}

/* legacy helper around netdev_alloc_skb() */
static inline struct sk_buff *dev_alloc_skb(unsigned int length)
{
	return netdev_alloc_skb(NULL, length);
}


static inline struct sk_buff *__netdev_alloc_skb_ip_align(struct net_device *dev,
		unsigned int length, gfp_t gfp)
{
	struct sk_buff *skb = __netdev_alloc_skb(dev, length + NET_IP_ALIGN, gfp);

	if (NET_IP_ALIGN && skb)
		skb_reserve(skb, NET_IP_ALIGN);
	return skb;
}

static inline struct sk_buff *netdev_alloc_skb_ip_align(struct net_device *dev,
		unsigned int length)
{
	return __netdev_alloc_skb_ip_align(dev, length, GFP_ATOMIC);
}

/*
 *	__skb_alloc_page - allocate pages for ps-rx on a skb and preserve pfmemalloc data
 *	@gfp_mask: alloc_pages_node mask. Set __GFP_NOMEMALLOC if not for network packet RX
 *	@skb: skb to set pfmemalloc on if __GFP_MEMALLOC is used
 *	@order: size of the allocation
 *
 * 	Allocate a new page.
 *
 * 	%NULL is returned if there is no free memory.
*/
static inline struct page *__skb_alloc_pages(gfp_t gfp_mask,
					      struct sk_buff *skb,
					      unsigned int order)
{
	struct page *page;

	gfp_mask |= __GFP_COLD;

	if (!(gfp_mask & __GFP_NOMEMALLOC))
		gfp_mask |= __GFP_MEMALLOC;

	page = alloc_pages_node(NUMA_NO_NODE, gfp_mask, order);
	if (skb && page && page->pfmemalloc)
		skb->pfmemalloc = true;

	return page;
}

/**
 *	__skb_alloc_page - allocate a page for ps-rx for a given skb and preserve pfmemalloc data
 *	@gfp_mask: alloc_pages_node mask. Set __GFP_NOMEMALLOC if not for network packet RX
 *	@skb: skb to set pfmemalloc on if __GFP_MEMALLOC is used
 *
 * 	Allocate a new page.
 *
 * 	%NULL is returned if there is no free memory.
 */
static inline struct page *__skb_alloc_page(gfp_t gfp_mask,
					     struct sk_buff *skb)
{
	return __skb_alloc_pages(gfp_mask, skb, 0);
}

/**
 *	skb_propagate_pfmemalloc - Propagate pfmemalloc if skb is allocated after RX page
 *	@page: The page that was allocated from skb_alloc_page
 *	@skb: The skb that may need pfmemalloc set
 */
static inline void skb_propagate_pfmemalloc(struct page *page,
					     struct sk_buff *skb)
{
	if (page && page->pfmemalloc)
		skb->pfmemalloc = true;
}

/**
 * skb_frag_page - retrieve the page refered to by a paged fragment
 * @frag: the paged fragment
 *
 * Returns the &struct page associated with @frag.
 */
static inline struct page *skb_frag_page(const skb_frag_t *frag)
{
	return frag->page.p;
}

/**
 * __skb_frag_ref - take an addition reference on a paged fragment.
 * @frag: the paged fragment
 *
 * Takes an additional reference on the paged fragment @frag.
 */
static inline void __skb_frag_ref(skb_frag_t *frag)
{
	get_page(skb_frag_page(frag));
}

/**
 * skb_frag_ref - take an addition reference on a paged fragment of an skb.
 * @skb: the buffer
 * @f: the fragment offset.
 *
 * Takes an additional reference on the @f'th paged fragment of @skb.
 */
static inline void skb_frag_ref(struct sk_buff *skb, int f)
{
	__skb_frag_ref(&skb_shinfo(skb)->frags[f]);
}

/**
 * __skb_frag_unref - release a reference on a paged fragment.
 * @frag: the paged fragment
 *
 * Releases a reference on the paged fragment @frag.
 */
static inline void __skb_frag_unref(skb_frag_t *frag)
{
	put_page(skb_frag_page(frag));
}

/**
 * skb_frag_unref - release a reference on a paged fragment of an skb.
 * @skb: the buffer
 * @f: the fragment offset
 *
 * Releases a reference on the @f'th paged fragment of @skb.
 */
static inline void skb_frag_unref(struct sk_buff *skb, int f)
{
	__skb_frag_unref(&skb_shinfo(skb)->frags[f]);
}

/**
 * skb_frag_address - gets the address of the data contained in a paged fragment
 * @frag: the paged fragment buffer
 *
 * Returns the address of the data within @frag. The page must already
 * be mapped.
 */
static inline void *skb_frag_address(const skb_frag_t *frag)
{
	return page_address(skb_frag_page(frag)) + frag->page_offset;
}

/**
 * skb_frag_address_safe - gets the address of the data contained in a paged fragment
 * @frag: the paged fragment buffer
 *
 * Returns the address of the data within @frag. Checks that the page
 * is mapped and returns %NULL otherwise.
 */
static inline void *skb_frag_address_safe(const skb_frag_t *frag)
{
	void *ptr = page_address(skb_frag_page(frag));
	if (unlikely(!ptr))
		return NULL;

	return ptr + frag->page_offset;
}

/**
 * __skb_frag_set_page - sets the page contained in a paged fragment
 * @frag: the paged fragment
 * @page: the page to set
 *
 * Sets the fragment @frag to contain @page.
 */
static inline void __skb_frag_set_page(skb_frag_t *frag, struct page *page)
{
	frag->page.p = page;
}

/**
 * skb_frag_set_page - sets the page contained in a paged fragment of an skb
 * @skb: the buffer
 * @f: the fragment offset
 * @page: the page to set
 *
 * Sets the @f'th fragment of @skb to contain @page.
 */
static inline void skb_frag_set_page(struct sk_buff *skb, int f,
				     struct page *page)
{
	__skb_frag_set_page(&skb_shinfo(skb)->frags[f], page);
}

/**
 * skb_frag_dma_map - maps a paged fragment via the DMA API
 * @dev: the device to map the fragment to
 * @frag: the paged fragment to map
 * @offset: the offset within the fragment (starting at the
 *          fragment's own offset)
 * @size: the number of bytes to map
 * @dir: the direction of the mapping (%PCI_DMA_*)
 *
 * Maps the page associated with @frag to @device.
 */
static inline dma_addr_t skb_frag_dma_map(struct device *dev,
					  const skb_frag_t *frag,
					  size_t offset, size_t size,
					  enum dma_data_direction dir)
{
	return dma_map_page(dev, skb_frag_page(frag),
			    frag->page_offset + offset, size, dir);
}

static inline struct sk_buff *pskb_copy(struct sk_buff *skb,
					gfp_t gfp_mask)
{
	return __pskb_copy(skb, skb_headroom(skb), gfp_mask);
}

/**
 *	skb_clone_writable - is the header of a clone writable
 *	@skb: buffer to check
 *	@len: length up to which to write
 *
 *	Returns true if modifying the header part of the cloned buffer
 *	does not requires the data to be copied.
 */
static inline int skb_clone_writable(const struct sk_buff *skb, unsigned int len)
{
	return !skb_header_cloned(skb) &&
	       skb_headroom(skb) + len <= skb->hdr_len;
}

static inline int __skb_cow(struct sk_buff *skb, unsigned int headroom,
			    int cloned)
{
	int delta = 0;

	if (headroom > skb_headroom(skb))
		delta = headroom - skb_headroom(skb);

	if (delta || cloned)
		return pskb_expand_head(skb, ALIGN(delta, NET_SKB_PAD), 0,
					GFP_ATOMIC);
	return 0;
}

/**
 *	skb_cow - copy header of skb when it is required
 *	@skb: buffer to cow
 *	@headroom: needed headroom
 *
 *	If the skb passed lacks sufficient headroom or its data part
 *	is shared, data is reallocated. If reallocation fails, an error
 *	is returned and original skb is not changed.
 *
 *	The result is skb with writable area skb->head...skb->tail
 *	and at least @headroom of space at head.
 */
static inline int skb_cow(struct sk_buff *skb, unsigned int headroom)
{
	return __skb_cow(skb, headroom, skb_cloned(skb));
}

/**
 *	skb_cow_head - skb_cow but only making the head writable
 *	@skb: buffer to cow
 *	@headroom: needed headroom
 *
 *	This function is identical to skb_cow except that we replace the
 *	skb_cloned check by skb_header_cloned.  It should be used when
 *	you only need to push on some header and do not need to modify
 *	the data.
 */
static inline int skb_cow_head(struct sk_buff *skb, unsigned int headroom)
{
	return __skb_cow(skb, headroom, skb_header_cloned(skb));
}

/**
 *	skb_padto	- pad an skbuff up to a minimal size
 *	@skb: buffer to pad
 *	@len: minimal length
 *
 *	Pads up a buffer to ensure the trailing bytes exist and are
 *	blanked. If the buffer already contains sufficient data it
 *	is untouched. Otherwise it is extended. Returns zero on
 *	success. The skb is freed on error.
 */
 
static inline int skb_padto(struct sk_buff *skb, unsigned int len)
{
	unsigned int size = skb->len;
	if (likely(size >= len))
		return 0;
	return skb_pad(skb, len - size);
}

static inline int skb_add_data(struct sk_buff *skb,
			       char __user *from, int copy)
{
	const int off = skb->len;

	if (skb->ip_summed == CHECKSUM_NONE) {
		int err = 0;
		__wsum csum = csum_and_copy_from_user(from, skb_put(skb, copy),
							    copy, 0, &err);
		if (!err) {
			skb->csum = csum_block_add(skb->csum, csum, off);
			return 0;
		}
	} else if (!copy_from_user(skb_put(skb, copy), from, copy))
		return 0;

	__skb_trim(skb, off);
	return -EFAULT;
}

static inline bool skb_can_coalesce(struct sk_buff *skb, int i,
				    const struct page *page, int off)
{
	if (i) {
		const struct skb_frag_struct *frag = &skb_shinfo(skb)->frags[i - 1];

		return page == skb_frag_page(frag) &&
		       off == frag->page_offset + skb_frag_size(frag);
	}
	return false;
}

static inline int __skb_linearize(struct sk_buff *skb)
{
	return __pskb_pull_tail(skb, skb->data_len) ? 0 : -ENOMEM;
}

/**
 *	skb_linearize - convert paged skb to linear one
 *	@skb: buffer to linarize
 *
 *	If there is no free memory -ENOMEM is returned, otherwise zero
 *	is returned and the old skb data released.
 */
static inline int skb_linearize(struct sk_buff *skb)
{
	return skb_is_nonlinear(skb) ? __skb_linearize(skb) : 0;
}

/**
 * skb_has_shared_frag - can any frag be overwritten
 * @skb: buffer to test
 *
 * Return true if the skb has at least one frag that might be modified
 * by an external entity (as in vmsplice()/sendfile())
 */
static inline bool skb_has_shared_frag(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->gso_type & SKB_GSO_SHARED_FRAG;
}

/**
 *	skb_linearize_cow - make sure skb is linear and writable
 *	@skb: buffer to process
 *
 *	If there is no free memory -ENOMEM is returned, otherwise zero
 *	is returned and the old skb data released.
 */
static inline int skb_linearize_cow(struct sk_buff *skb)
{
	return skb_is_nonlinear(skb) || skb_cloned(skb) ?
	       __skb_linearize(skb) : 0;
}

/**
 *	skb_postpull_rcsum - update checksum for received skb after pull
 *	@skb: buffer to update
 *	@start: start of data before pull
 *	@len: length of data pulled
 *
 *	After doing a pull on a received packet, you need to call this to
 *	update the CHECKSUM_COMPLETE checksum, or set ip_summed to
 *	CHECKSUM_NONE so that it can be recomputed from scratch.
 */

static inline void skb_postpull_rcsum(struct sk_buff *skb,
				      const void *start, unsigned int len)
{
	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->csum = csum_sub(skb->csum, csum_partial(start, len, 0));
}

unsigned char *skb_pull_rcsum(struct sk_buff *skb, unsigned int len);

/**
 *	pskb_trim_rcsum - trim received skb and update checksum
 *	@skb: buffer to trim
 *	@len: new length
 *
 *	This is exactly the same as pskb_trim except that it ensures the
 *	checksum of received packets are still valid after the operation.
 */

static inline int pskb_trim_rcsum(struct sk_buff *skb, unsigned int len)
{
	if (likely(len >= skb->len))
		return 0;
	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->ip_summed = CHECKSUM_NONE;
	return __pskb_trim(skb, len);
}

#define skb_queue_walk(queue, skb) \
		for (skb = (queue)->next;					\
		     skb != (struct sk_buff *)(queue);				\
		     skb = skb->next)

#define skb_queue_walk_safe(queue, skb, tmp)					\
		for (skb = (queue)->next, tmp = skb->next;			\
		     skb != (struct sk_buff *)(queue);				\
		     skb = tmp, tmp = skb->next)

#define skb_queue_walk_from(queue, skb)						\
		for (; skb != (struct sk_buff *)(queue);			\
		     skb = skb->next)

#define skb_queue_walk_from_safe(queue, skb, tmp)				\
		for (tmp = skb->next;						\
		     skb != (struct sk_buff *)(queue);				\
		     skb = tmp, tmp = skb->next)

#define skb_queue_reverse_walk(queue, skb) \
		for (skb = (queue)->prev;					\
		     skb != (struct sk_buff *)(queue);				\
		     skb = skb->prev)

#define skb_queue_reverse_walk_safe(queue, skb, tmp)				\
		for (skb = (queue)->prev, tmp = skb->prev;			\
		     skb != (struct sk_buff *)(queue);				\
		     skb = tmp, tmp = skb->prev)

#define skb_queue_reverse_walk_from_safe(queue, skb, tmp)			\
		for (tmp = skb->prev;						\
		     skb != (struct sk_buff *)(queue);				\
		     skb = tmp, tmp = skb->prev)

static inline bool skb_has_frag_list(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->frag_list != NULL;
}

static inline void skb_frag_list_init(struct sk_buff *skb)
{
	skb_shinfo(skb)->frag_list = NULL;
}

static inline void skb_frag_add_head(struct sk_buff *skb, struct sk_buff *frag)
{
	frag->next = skb_shinfo(skb)->frag_list;
	skb_shinfo(skb)->frag_list = frag;
}

#define skb_walk_frags(skb, iter)	\
	for (iter = skb_shinfo(skb)->frag_list; iter; iter = iter->next)

extern struct sk_buff *__skb_recv_datagram(struct sock *sk, unsigned flags,
					   int *peeked, int *off, int *err);
extern struct sk_buff *skb_recv_datagram(struct sock *sk, unsigned flags,
					 int noblock, int *err);
extern unsigned int    datagram_poll(struct file *file, struct socket *sock,
				     struct poll_table_struct *wait);
extern int	       skb_copy_datagram_iovec(const struct sk_buff *from,
					       int offset, struct iovec *to,
					       int size);
extern int	       skb_copy_and_csum_datagram_iovec(struct sk_buff *skb,
							int hlen,
							struct iovec *iov);
extern int	       skb_copy_datagram_from_iovec(struct sk_buff *skb,
						    int offset,
						    const struct iovec *from,
						    int from_offset,
						    int len);
extern int	       skb_copy_datagram_const_iovec(const struct sk_buff *from,
						     int offset,
						     const struct iovec *to,
						     int to_offset,
						     int size);
extern void	       skb_free_datagram(struct sock *sk, struct sk_buff *skb);
extern void	       skb_free_datagram_locked(struct sock *sk,
						struct sk_buff *skb);
extern int	       skb_kill_datagram(struct sock *sk, struct sk_buff *skb,
					 unsigned int flags);
extern __wsum	       skb_checksum(const struct sk_buff *skb, int offset,
				    int len, __wsum csum);
extern int	       skb_copy_bits(const struct sk_buff *skb, int offset,
				     void *to, int len);
extern int	       skb_store_bits(struct sk_buff *skb, int offset,
				      const void *from, int len);
extern __wsum	       skb_copy_and_csum_bits(const struct sk_buff *skb,
					      int offset, u8 *to, int len,
					      __wsum csum);
extern int             skb_splice_bits(struct sk_buff *skb,
						unsigned int offset,
						struct pipe_inode_info *pipe,
						unsigned int len,
						unsigned int flags);
extern void	       skb_copy_and_csum_dev(const struct sk_buff *skb, u8 *to);
extern void	       skb_split(struct sk_buff *skb,
				 struct sk_buff *skb1, const u32 len);
extern int	       skb_shift(struct sk_buff *tgt, struct sk_buff *skb,
				 int shiftlen);

extern struct sk_buff *skb_segment(struct sk_buff *skb,
				   netdev_features_t features);

static inline void *skb_header_pointer(const struct sk_buff *skb, int offset,
				       int len, void *buffer)
{
	int hlen = skb_headlen(skb);

	if (hlen - offset >= len)
		return skb->data + offset;

	if (skb_copy_bits(skb, offset, buffer, len) < 0)
		return NULL;

	return buffer;
}

static inline void skb_copy_from_linear_data(const struct sk_buff *skb,
					     void *to,
					     const unsigned int len)
{
	memcpy(to, skb->data, len);
}

static inline void skb_copy_from_linear_data_offset(const struct sk_buff *skb,
						    const int offset, void *to,
						    const unsigned int len)
{
	memcpy(to, skb->data + offset, len);
}

static inline void skb_copy_to_linear_data(struct sk_buff *skb,
					   const void *from,
					   const unsigned int len)
{
	memcpy(skb->data, from, len);
}

static inline void skb_copy_to_linear_data_offset(struct sk_buff *skb,
						  const int offset,
						  const void *from,
						  const unsigned int len)
{
	memcpy(skb->data + offset, from, len);
}

extern void skb_init(void);

static inline ktime_t skb_get_ktime(const struct sk_buff *skb)
{
	return skb->tstamp;
}

/**
 *	skb_get_timestamp - get timestamp from a skb
 *	@skb: skb to get stamp from
 *	@stamp: pointer to struct timeval to store stamp in
 *
 *	Timestamps are stored in the skb as offsets to a base timestamp.
 *	This function converts the offset back to a struct timeval and stores
 *	it in stamp.
 */
static inline void skb_get_timestamp(const struct sk_buff *skb,
				     struct timeval *stamp)
{
	*stamp = ktime_to_timeval(skb->tstamp);
}

static inline void skb_get_timestampns(const struct sk_buff *skb,
				       struct timespec *stamp)
{
	*stamp = ktime_to_timespec(skb->tstamp);
}

static inline void __net_timestamp(struct sk_buff *skb)
{
	skb->tstamp = ktime_get_real();
}

static inline ktime_t net_timedelta(ktime_t t)
{
	return ktime_sub(ktime_get_real(), t);
}

static inline ktime_t net_invalid_timestamp(void)
{
	return ktime_set(0, 0);
}

extern void skb_timestamping_init(void);

#ifdef CONFIG_NETWORK_PHY_TIMESTAMPING

extern void skb_clone_tx_timestamp(struct sk_buff *skb);
extern bool skb_defer_rx_timestamp(struct sk_buff *skb);

#else /* CONFIG_NETWORK_PHY_TIMESTAMPING */

static inline void skb_clone_tx_timestamp(struct sk_buff *skb)
{
}

static inline bool skb_defer_rx_timestamp(struct sk_buff *skb)
{
	return false;
}

#endif /* !CONFIG_NETWORK_PHY_TIMESTAMPING */

/**
 * skb_complete_tx_timestamp() - deliver cloned skb with tx timestamps
 *
 * PHY drivers may accept clones of transmitted packets for
 * timestamping via their phy_driver.txtstamp method. These drivers
 * must call this function to return the skb back to the stack, with
 * or without a timestamp.
 *
 * @skb: clone of the the original outgoing packet
 * @hwtstamps: hardware time stamps, may be NULL if not available
 *
 */
void skb_complete_tx_timestamp(struct sk_buff *skb,
			       struct skb_shared_hwtstamps *hwtstamps);

/**
 * skb_tstamp_tx - queue clone of skb with send time stamps
 * @orig_skb:	the original outgoing packet
 * @hwtstamps:	hardware time stamps, may be NULL if not available
 *
 * If the skb has a socket associated, then this function clones the
 * skb (thus sharing the actual data and optional structures), stores
 * the optional hardware time stamping information (if non NULL) or
 * generates a software time stamp (otherwise), then queues the clone
 * to the error queue of the socket.  Errors are silently ignored.
 */
extern void skb_tstamp_tx(struct sk_buff *orig_skb,
			struct skb_shared_hwtstamps *hwtstamps);

static inline void sw_tx_timestamp(struct sk_buff *skb)
{
	if (skb_shinfo(skb)->tx_flags & SKBTX_SW_TSTAMP &&
	    !(skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS))
		skb_tstamp_tx(skb, NULL);
}

/**
 * skb_tx_timestamp() - Driver hook for transmit timestamping
 *
 * Ethernet MAC Drivers should call this function in their hard_xmit()
 * function immediately before giving the sk_buff to the MAC hardware.
 *
 * @skb: A socket buffer.
 */
static inline void skb_tx_timestamp(struct sk_buff *skb)
{
	skb_clone_tx_timestamp(skb);
	sw_tx_timestamp(skb);
}

/**
 * skb_complete_wifi_ack - deliver skb with wifi status
 *
 * @skb: the original outgoing packet
 * @acked: ack status
 *
 */
void skb_complete_wifi_ack(struct sk_buff *skb, bool acked);

extern __sum16 __skb_checksum_complete_head(struct sk_buff *skb, int len);
extern __sum16 __skb_checksum_complete(struct sk_buff *skb);

static inline int skb_csum_unnecessary(const struct sk_buff *skb)
{
	return skb->ip_summed & CHECKSUM_UNNECESSARY;
}

/**
 *	skb_checksum_complete - Calculate checksum of an entire packet
 *	@skb: packet to process
 *
 *	This function calculates the checksum over the entire packet plus
 *	the value of skb->csum.  The latter can be used to supply the
 *	checksum of a pseudo header as used by TCP/UDP.  It returns the
 *	checksum.
 *
 *	For protocols that contain complete checksums such as ICMP/TCP/UDP,
 *	this function can be used to verify that checksum on received
 *	packets.  In that case the function should return zero if the
 *	checksum is correct.  In particular, this function will return zero
 *	if skb->ip_summed is CHECKSUM_UNNECESSARY which indicates that the
 *	hardware has already verified the correctness of the checksum.
 */
static inline __sum16 skb_checksum_complete(struct sk_buff *skb)
{
	return skb_csum_unnecessary(skb) ?
	       0 : __skb_checksum_complete(skb);
}

#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
extern void nf_conntrack_destroy(struct nf_conntrack *nfct);
static inline void nf_conntrack_put(struct nf_conntrack *nfct)
{
	if (nfct && atomic_dec_and_test(&nfct->use))
		nf_conntrack_destroy(nfct);
}
static inline void nf_conntrack_get(struct nf_conntrack *nfct)
{
	if (nfct)
		atomic_inc(&nfct->use);
}
#endif
#ifdef NET_SKBUFF_NF_DEFRAG_NEEDED
static inline void nf_conntrack_get_reasm(struct sk_buff *skb)
{
	if (skb)
		atomic_inc(&skb->users);
}
static inline void nf_conntrack_put_reasm(struct sk_buff *skb)
{
	if (skb)
		kfree_skb(skb);
}
#endif
#ifdef CONFIG_BRIDGE_NETFILTER
static inline void nf_bridge_put(struct nf_bridge_info *nf_bridge)
{
	if (nf_bridge && atomic_dec_and_test(&nf_bridge->use))
		kfree(nf_bridge);
}
static inline void nf_bridge_get(struct nf_bridge_info *nf_bridge)
{
	if (nf_bridge)
		atomic_inc(&nf_bridge->use);
}
#endif /* CONFIG_BRIDGE_NETFILTER */
static inline void nf_reset(struct sk_buff *skb)
{
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
	nf_conntrack_put(skb->nfct);
	skb->nfct = NULL;
#endif
#ifdef NET_SKBUFF_NF_DEFRAG_NEEDED
	nf_conntrack_put_reasm(skb->nfct_reasm);
	skb->nfct_reasm = NULL;
#endif
#ifdef CONFIG_BRIDGE_NETFILTER
	nf_bridge_put(skb->nf_bridge);
	skb->nf_bridge = NULL;
#endif
}

/* Note: This doesn't put any conntrack and bridge info in dst. */
static inline void __nf_copy(struct sk_buff *dst, const struct sk_buff *src)
{
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
	dst->nfct = src->nfct;
	nf_conntrack_get(src->nfct);
	dst->nfctinfo = src->nfctinfo;
#endif
#ifdef NET_SKBUFF_NF_DEFRAG_NEEDED
	dst->nfct_reasm = src->nfct_reasm;
	nf_conntrack_get_reasm(src->nfct_reasm);
#endif
#ifdef CONFIG_BRIDGE_NETFILTER
	dst->nf_bridge  = src->nf_bridge;
	nf_bridge_get(src->nf_bridge);
#endif
}

static inline void nf_copy(struct sk_buff *dst, const struct sk_buff *src)
{
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
	nf_conntrack_put(dst->nfct);
#endif
#ifdef NET_SKBUFF_NF_DEFRAG_NEEDED
	nf_conntrack_put_reasm(dst->nfct_reasm);
#endif
#ifdef CONFIG_BRIDGE_NETFILTER
	nf_bridge_put(dst->nf_bridge);
#endif
	__nf_copy(dst, src);
}

#ifdef CONFIG_NETWORK_SECMARK
static inline void skb_copy_secmark(struct sk_buff *to, const struct sk_buff *from)
{
	to->secmark = from->secmark;
}

static inline void skb_init_secmark(struct sk_buff *skb)
{
	skb->secmark = 0;
}
#else
static inline void skb_copy_secmark(struct sk_buff *to, const struct sk_buff *from)
{ }

static inline void skb_init_secmark(struct sk_buff *skb)
{ }
#endif

static inline void skb_set_queue_mapping(struct sk_buff *skb, u16 queue_mapping)
{
	skb->queue_mapping = queue_mapping;
}

static inline u16 skb_get_queue_mapping(const struct sk_buff *skb)
{
	return skb->queue_mapping;
}

static inline void skb_copy_queue_mapping(struct sk_buff *to, const struct sk_buff *from)
{
	to->queue_mapping = from->queue_mapping;
}

static inline void skb_record_rx_queue(struct sk_buff *skb, u16 rx_queue)
{
	skb->queue_mapping = rx_queue + 1;
}

static inline u16 skb_get_rx_queue(const struct sk_buff *skb)
{
	return skb->queue_mapping - 1;
}

static inline bool skb_rx_queue_recorded(const struct sk_buff *skb)
{
	return skb->queue_mapping != 0;
}

extern u16 __skb_tx_hash(const struct net_device *dev,
			 const struct sk_buff *skb,
			 unsigned int num_tx_queues);

#ifdef CONFIG_XFRM
static inline struct sec_path *skb_sec_path(struct sk_buff *skb)
{
	return skb->sp;
}
#else
static inline struct sec_path *skb_sec_path(struct sk_buff *skb)
{
	return NULL;
}
#endif

static inline bool skb_is_gso(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->gso_size;
}

static inline bool skb_is_gso_v6(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6;
}

extern void __skb_warn_lro_forwarding(const struct sk_buff *skb);

static inline bool skb_warn_if_lro(const struct sk_buff *skb)
{
	/* LRO sets gso_size but not gso_type, whereas if GSO is really
	 * wanted then gso_type will be set. */
	const struct skb_shared_info *shinfo = skb_shinfo(skb);

	if (skb_is_nonlinear(skb) && shinfo->gso_size != 0 &&
	    unlikely(shinfo->gso_type == 0)) {
		__skb_warn_lro_forwarding(skb);
		return true;
	}
	return false;
}

static inline void skb_forward_csum(struct sk_buff *skb)
{
	/* Unfortunately we don't support this one.  Any brave souls? */
	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->ip_summed = CHECKSUM_NONE;
}

/**
 * skb_checksum_none_assert - make sure skb ip_summed is CHECKSUM_NONE
 * @skb: skb to check
 *
 * fresh skbs have their ip_summed set to CHECKSUM_NONE.
 * Instead of forcing ip_summed to CHECKSUM_NONE, we can
 * use this helper, to document places where we make this assertion.
 */
static inline void skb_checksum_none_assert(const struct sk_buff *skb)
{
#ifdef DEBUG
	BUG_ON(skb->ip_summed != CHECKSUM_NONE);
#endif
}

bool skb_partial_csum_set(struct sk_buff *skb, u16 start, u16 off);

/**
 * skb_head_is_locked - Determine if the skb->head is locked down
 * @skb: skb to check
 *
 * The head on skbs build around a head frag can be removed if they are
 * not cloned.  This function returns true if the skb head is locked down
 * due to either being allocated via kmalloc, or by being a clone with
 * multiple references to the head.
 */
static inline bool skb_head_is_locked(const struct sk_buff *skb)
{
	return !skb->head_frag || skb_cloned(skb);
}
#endif	/* __KERNEL__ */
#endif	/* _LINUX_SKBUFF_H */
