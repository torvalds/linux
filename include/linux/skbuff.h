/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	Definitions for the 'struct sk_buff' memory handlers.
 *
 *	Authors:
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Florian La Roche, <rzsfl@rz.uni-sb.de>
 */

#ifndef _LINUX_SKBUFF_H
#define _LINUX_SKBUFF_H

#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/time.h>
#include <linux/bug.h>
#include <linux/bvec.h>
#include <linux/cache.h>
#include <linux/rbtree.h>
#include <linux/socket.h>
#include <linux/refcount.h>

#include <linux/atomic.h>
#include <asm/types.h>
#include <linux/spinlock.h>
#include <net/checksum.h>
#include <linux/rcupdate.h>
#include <linux/dma-mapping.h>
#include <linux/netdev_features.h>
#include <net/flow_dissector.h>
#include <linux/in6.h>
#include <linux/if_packet.h>
#include <linux/llist.h>
#include <linux/page_frag_cache.h>
#include <net/flow.h>
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
#include <linux/netfilter/nf_conntrack_common.h>
#endif
#include <net/net_debug.h>
#include <net/dropreason-core.h>
#include <net/netmem.h>

/**
 * DOC: skb checksums
 *
 * The interface for checksum offload between the stack and networking drivers
 * is as follows...
 *
 * IP checksum related features
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Drivers advertise checksum offload capabilities in the features of a device.
 * From the stack's point of view these are capabilities offered by the driver.
 * A driver typically only advertises features that it is capable of offloading
 * to its device.
 *
 * .. flat-table:: Checksum related device features
 *   :widths: 1 10
 *
 *   * - %NETIF_F_HW_CSUM
 *     - The driver (or its device) is able to compute one
 *	 IP (one's complement) checksum for any combination
 *	 of protocols or protocol layering. The checksum is
 *	 computed and set in a packet per the CHECKSUM_PARTIAL
 *	 interface (see below).
 *
 *   * - %NETIF_F_IP_CSUM
 *     - Driver (device) is only able to checksum plain
 *	 TCP or UDP packets over IPv4. These are specifically
 *	 unencapsulated packets of the form IPv4|TCP or
 *	 IPv4|UDP where the Protocol field in the IPv4 header
 *	 is TCP or UDP. The IPv4 header may contain IP options.
 *	 This feature cannot be set in features for a device
 *	 with NETIF_F_HW_CSUM also set. This feature is being
 *	 DEPRECATED (see below).
 *
 *   * - %NETIF_F_IPV6_CSUM
 *     - Driver (device) is only able to checksum plain
 *	 TCP or UDP packets over IPv6. These are specifically
 *	 unencapsulated packets of the form IPv6|TCP or
 *	 IPv6|UDP where the Next Header field in the IPv6
 *	 header is either TCP or UDP. IPv6 extension headers
 *	 are not supported with this feature. This feature
 *	 cannot be set in features for a device with
 *	 NETIF_F_HW_CSUM also set. This feature is being
 *	 DEPRECATED (see below).
 *
 *   * - %NETIF_F_RXCSUM
 *     - Driver (device) performs receive checksum offload.
 *	 This flag is only used to disable the RX checksum
 *	 feature for a device. The stack will accept receive
 *	 checksum indication in packets received on a device
 *	 regardless of whether NETIF_F_RXCSUM is set.
 *
 * Checksumming of received packets by device
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Indication of checksum verification is set in &sk_buff.ip_summed.
 * Possible values are:
 *
 * - %CHECKSUM_NONE
 *
 *   Device did not checksum this packet e.g. due to lack of capabilities.
 *   The packet contains full (though not verified) checksum in packet but
 *   not in skb->csum. Thus, skb->csum is undefined in this case.
 *
 * - %CHECKSUM_UNNECESSARY
 *
 *   The hardware you're dealing with doesn't calculate the full checksum
 *   (as in %CHECKSUM_COMPLETE), but it does parse headers and verify checksums
 *   for specific protocols. For such packets it will set %CHECKSUM_UNNECESSARY
 *   if their checksums are okay. &sk_buff.csum is still undefined in this case
 *   though. A driver or device must never modify the checksum field in the
 *   packet even if checksum is verified.
 *
 *   %CHECKSUM_UNNECESSARY is applicable to following protocols:
 *
 *     - TCP: IPv6 and IPv4.
 *     - UDP: IPv4 and IPv6. A device may apply CHECKSUM_UNNECESSARY to a
 *       zero UDP checksum for either IPv4 or IPv6, the networking stack
 *       may perform further validation in this case.
 *     - GRE: only if the checksum is present in the header.
 *     - SCTP: indicates the CRC in SCTP header has been validated.
 *     - FCOE: indicates the CRC in FC frame has been validated.
 *
 *   &sk_buff.csum_level indicates the number of consecutive checksums found in
 *   the packet minus one that have been verified as %CHECKSUM_UNNECESSARY.
 *   For instance if a device receives an IPv6->UDP->GRE->IPv4->TCP packet
 *   and a device is able to verify the checksums for UDP (possibly zero),
 *   GRE (checksum flag is set) and TCP, &sk_buff.csum_level would be set to
 *   two. If the device were only able to verify the UDP checksum and not
 *   GRE, either because it doesn't support GRE checksum or because GRE
 *   checksum is bad, skb->csum_level would be set to zero (TCP checksum is
 *   not considered in this case).
 *
 * - %CHECKSUM_COMPLETE
 *
 *   This is the most generic way. The device supplied checksum of the _whole_
 *   packet as seen by netif_rx() and fills in &sk_buff.csum. This means the
 *   hardware doesn't need to parse L3/L4 headers to implement this.
 *
 *   Notes:
 *
 *   - Even if device supports only some protocols, but is able to produce
 *     skb->csum, it MUST use CHECKSUM_COMPLETE, not CHECKSUM_UNNECESSARY.
 *   - CHECKSUM_COMPLETE is not applicable to SCTP and FCoE protocols.
 *
 * - %CHECKSUM_PARTIAL
 *
 *   A checksum is set up to be offloaded to a device as described in the
 *   output description for CHECKSUM_PARTIAL. This may occur on a packet
 *   received directly from another Linux OS, e.g., a virtualized Linux kernel
 *   on the same host, or it may be set in the input path in GRO or remote
 *   checksum offload. For the purposes of checksum verification, the checksum
 *   referred to by skb->csum_start + skb->csum_offset and any preceding
 *   checksums in the packet are considered verified. Any checksums in the
 *   packet that are after the checksum being offloaded are not considered to
 *   be verified.
 *
 * Checksumming on transmit for non-GSO
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * The stack requests checksum offload in the &sk_buff.ip_summed for a packet.
 * Values are:
 *
 * - %CHECKSUM_PARTIAL
 *
 *   The driver is required to checksum the packet as seen by hard_start_xmit()
 *   from &sk_buff.csum_start up to the end, and to record/write the checksum at
 *   offset &sk_buff.csum_start + &sk_buff.csum_offset.
 *   A driver may verify that the
 *   csum_start and csum_offset values are valid values given the length and
 *   offset of the packet, but it should not attempt to validate that the
 *   checksum refers to a legitimate transport layer checksum -- it is the
 *   purview of the stack to validate that csum_start and csum_offset are set
 *   correctly.
 *
 *   When the stack requests checksum offload for a packet, the driver MUST
 *   ensure that the checksum is set correctly. A driver can either offload the
 *   checksum calculation to the device, or call skb_checksum_help (in the case
 *   that the device does not support offload for a particular checksum).
 *
 *   %NETIF_F_IP_CSUM and %NETIF_F_IPV6_CSUM are being deprecated in favor of
 *   %NETIF_F_HW_CSUM. New devices should use %NETIF_F_HW_CSUM to indicate
 *   checksum offload capability.
 *   skb_csum_hwoffload_help() can be called to resolve %CHECKSUM_PARTIAL based
 *   on network device checksumming capabilities: if a packet does not match
 *   them, skb_checksum_help() or skb_crc32c_help() (depending on the value of
 *   &sk_buff.csum_not_inet, see :ref:`crc`)
 *   is called to resolve the checksum.
 *
 * - %CHECKSUM_NONE
 *
 *   The skb was already checksummed by the protocol, or a checksum is not
 *   required.
 *
 * - %CHECKSUM_UNNECESSARY
 *
 *   This has the same meaning as CHECKSUM_NONE for checksum offload on
 *   output.
 *
 * - %CHECKSUM_COMPLETE
 *
 *   Not used in checksum output. If a driver observes a packet with this value
 *   set in skbuff, it should treat the packet as if %CHECKSUM_NONE were set.
 *
 * .. _crc:
 *
 * Non-IP checksum (CRC) offloads
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * .. flat-table::
 *   :widths: 1 10
 *
 *   * - %NETIF_F_SCTP_CRC
 *     - This feature indicates that a device is capable of
 *	 offloading the SCTP CRC in a packet. To perform this offload the stack
 *	 will set csum_start and csum_offset accordingly, set ip_summed to
 *	 %CHECKSUM_PARTIAL and set csum_not_inet to 1, to provide an indication
 *	 in the skbuff that the %CHECKSUM_PARTIAL refers to CRC32c.
 *	 A driver that supports both IP checksum offload and SCTP CRC32c offload
 *	 must verify which offload is configured for a packet by testing the
 *	 value of &sk_buff.csum_not_inet; skb_crc32c_csum_help() is provided to
 *	 resolve %CHECKSUM_PARTIAL on skbs where csum_not_inet is set to 1.
 *
 *   * - %NETIF_F_FCOE_CRC
 *     - This feature indicates that a device is capable of offloading the FCOE
 *	 CRC in a packet. To perform this offload the stack will set ip_summed
 *	 to %CHECKSUM_PARTIAL and set csum_start and csum_offset
 *	 accordingly. Note that there is no indication in the skbuff that the
 *	 %CHECKSUM_PARTIAL refers to an FCOE checksum, so a driver that supports
 *	 both IP checksum offload and FCOE CRC offload must verify which offload
 *	 is configured for a packet, presumably by inspecting packet headers.
 *
 * Checksumming on output with GSO
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * In the case of a GSO packet (skb_is_gso() is true), checksum offload
 * is implied by the SKB_GSO_* flags in gso_type. Most obviously, if the
 * gso_type is %SKB_GSO_TCPV4 or %SKB_GSO_TCPV6, TCP checksum offload as
 * part of the GSO operation is implied. If a checksum is being offloaded
 * with GSO then ip_summed is %CHECKSUM_PARTIAL, and both csum_start and
 * csum_offset are set to refer to the outermost checksum being offloaded
 * (two offloaded checksums are possible with UDP encapsulation).
 */

/* Don't change this without changing skb_csum_unnecessary! */
#define CHECKSUM_NONE		0
#define CHECKSUM_UNNECESSARY	1
#define CHECKSUM_COMPLETE	2
#define CHECKSUM_PARTIAL	3

/* Maximum value in skb->csum_level */
#define SKB_MAX_CSUM_LEVEL	3

#define SKB_DATA_ALIGN(X)	ALIGN(X, SMP_CACHE_BYTES)
#define SKB_WITH_OVERHEAD(X)	\
	((X) - SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

/* For X bytes available in skb->head, what is the minimal
 * allocation needed, knowing struct skb_shared_info needs
 * to be aligned.
 */
#define SKB_HEAD_ALIGN(X) (SKB_DATA_ALIGN(X) + \
	SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

#define SKB_MAX_ORDER(X, ORDER) \
	SKB_WITH_OVERHEAD((PAGE_SIZE << (ORDER)) - (X))
#define SKB_MAX_HEAD(X)		(SKB_MAX_ORDER((X), 0))
#define SKB_MAX_ALLOC		(SKB_MAX_ORDER(0, 2))

/* return minimum truesize of one skb containing X bytes of data */
#define SKB_TRUESIZE(X) ((X) +						\
			 SKB_DATA_ALIGN(sizeof(struct sk_buff)) +	\
			 SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

struct ahash_request;
struct net_device;
struct scatterlist;
struct pipe_inode_info;
struct iov_iter;
struct napi_struct;
struct bpf_prog;
union bpf_attr;
struct skb_ext;
struct ts_config;

#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
struct nf_bridge_info {
	enum {
		BRNF_PROTO_UNCHANGED,
		BRNF_PROTO_8021Q,
		BRNF_PROTO_PPPOE
	} orig_proto:8;
	u8			pkt_otherhost:1;
	u8			in_prerouting:1;
	u8			bridged_dnat:1;
	u8			sabotage_in_done:1;
	__u16			frag_max_size;
	int			physinif;

	/* always valid & non-NULL from FORWARD on, for physdev match */
	struct net_device	*physoutdev;
	union {
		/* prerouting: detect dnat in orig/reply direction */
		__be32          ipv4_daddr;
		struct in6_addr ipv6_daddr;

		/* after prerouting + nat detected: store original source
		 * mac since neigh resolution overwrites it, only used while
		 * skb is out in neigh layer.
		 */
		char neigh_header[8];
	};
};
#endif

#if IS_ENABLED(CONFIG_NET_TC_SKB_EXT)
/* Chain in tc_skb_ext will be used to share the tc chain with
 * ovs recirc_id. It will be set to the current chain by tc
 * and read by ovs to recirc_id.
 */
struct tc_skb_ext {
	union {
		u64 act_miss_cookie;
		__u32 chain;
	};
	__u16 mru;
	__u16 zone;
	u8 post_ct:1;
	u8 post_ct_snat:1;
	u8 post_ct_dnat:1;
	u8 act_miss:1; /* Set if act_miss_cookie is used */
	u8 l2_miss:1; /* Set by bridge upon FDB or MDB miss */
};
#endif

struct sk_buff_head {
	/* These two members must be first to match sk_buff. */
	struct_group_tagged(sk_buff_list, list,
		struct sk_buff	*next;
		struct sk_buff	*prev;
	);

	__u32		qlen;
	spinlock_t	lock;
};

struct sk_buff;

#ifndef CONFIG_MAX_SKB_FRAGS
# define CONFIG_MAX_SKB_FRAGS 17
#endif

#define MAX_SKB_FRAGS CONFIG_MAX_SKB_FRAGS

/* Set skb_shinfo(skb)->gso_size to this in case you want skb_segment to
 * segment using its current segmentation instead.
 */
#define GSO_BY_FRAGS	0xFFFF

typedef struct skb_frag {
	netmem_ref netmem;
	unsigned int len;
	unsigned int offset;
} skb_frag_t;

/**
 * skb_frag_size() - Returns the size of a skb fragment
 * @frag: skb fragment
 */
static inline unsigned int skb_frag_size(const skb_frag_t *frag)
{
	return frag->len;
}

/**
 * skb_frag_size_set() - Sets the size of a skb fragment
 * @frag: skb fragment
 * @size: size of fragment
 */
static inline void skb_frag_size_set(skb_frag_t *frag, unsigned int size)
{
	frag->len = size;
}

/**
 * skb_frag_size_add() - Increments the size of a skb fragment by @delta
 * @frag: skb fragment
 * @delta: value to add
 */
static inline void skb_frag_size_add(skb_frag_t *frag, int delta)
{
	frag->len += delta;
}

/**
 * skb_frag_size_sub() - Decrements the size of a skb fragment by @delta
 * @frag: skb fragment
 * @delta: value to subtract
 */
static inline void skb_frag_size_sub(skb_frag_t *frag, int delta)
{
	frag->len -= delta;
}

/**
 * skb_frag_must_loop - Test if %p is a high memory page
 * @p: fragment's page
 */
static inline bool skb_frag_must_loop(struct page *p)
{
#if defined(CONFIG_HIGHMEM)
	if (IS_ENABLED(CONFIG_DEBUG_KMAP_LOCAL_FORCE_MAP) || PageHighMem(p))
		return true;
#endif
	return false;
}

/**
 *	skb_frag_foreach_page - loop over pages in a fragment
 *
 *	@f:		skb frag to operate on
 *	@f_off:		offset from start of f->netmem
 *	@f_len:		length from f_off to loop over
 *	@p:		(temp var) current page
 *	@p_off:		(temp var) offset from start of current page,
 *	                           non-zero only on first page.
 *	@p_len:		(temp var) length in current page,
 *				   < PAGE_SIZE only on first and last page.
 *	@copied:	(temp var) length so far, excluding current p_len.
 *
 *	A fragment can hold a compound page, in which case per-page
 *	operations, notably kmap_atomic, must be called for each
 *	regular page.
 */
#define skb_frag_foreach_page(f, f_off, f_len, p, p_off, p_len, copied)	\
	for (p = skb_frag_page(f) + ((f_off) >> PAGE_SHIFT),		\
	     p_off = (f_off) & (PAGE_SIZE - 1),				\
	     p_len = skb_frag_must_loop(p) ?				\
	     min_t(u32, f_len, PAGE_SIZE - p_off) : f_len,		\
	     copied = 0;						\
	     copied < f_len;						\
	     copied += p_len, p++, p_off = 0,				\
	     p_len = min_t(u32, f_len - copied, PAGE_SIZE))		\

/**
 * struct skb_shared_hwtstamps - hardware time stamps
 * @hwtstamp:		hardware time stamp transformed into duration
 *			since arbitrary point in time
 * @netdev_data:	address/cookie of network device driver used as
 *			reference to actual hardware time stamp
 *
 * Software time stamps generated by ktime_get_real() are stored in
 * skb->tstamp.
 *
 * hwtstamps can only be compared against other hwtstamps from
 * the same device.
 *
 * This structure is attached to packets as part of the
 * &skb_shared_info. Use skb_hwtstamps() to get a pointer.
 */
struct skb_shared_hwtstamps {
	union {
		ktime_t	hwtstamp;
		void *netdev_data;
	};
};

/* Definitions for tx_flags in struct skb_shared_info */
enum {
	/* generate hardware time stamp */
	SKBTX_HW_TSTAMP = 1 << 0,

	/* generate software time stamp when queueing packet to NIC */
	SKBTX_SW_TSTAMP = 1 << 1,

	/* device driver is going to provide hardware time stamp */
	SKBTX_IN_PROGRESS = 1 << 2,

	/* generate hardware time stamp based on cycles if supported */
	SKBTX_HW_TSTAMP_USE_CYCLES = 1 << 3,

	/* generate wifi status information (where possible) */
	SKBTX_WIFI_STATUS = 1 << 4,

	/* determine hardware time stamp based on time or cycles */
	SKBTX_HW_TSTAMP_NETDEV = 1 << 5,

	/* generate software time stamp when entering packet scheduling */
	SKBTX_SCHED_TSTAMP = 1 << 6,
};

#define SKBTX_ANY_SW_TSTAMP	(SKBTX_SW_TSTAMP    | \
				 SKBTX_SCHED_TSTAMP)
#define SKBTX_ANY_TSTAMP	(SKBTX_HW_TSTAMP | \
				 SKBTX_HW_TSTAMP_USE_CYCLES | \
				 SKBTX_ANY_SW_TSTAMP)

/* Definitions for flags in struct skb_shared_info */
enum {
	/* use zcopy routines */
	SKBFL_ZEROCOPY_ENABLE = BIT(0),

	/* This indicates at least one fragment might be overwritten
	 * (as in vmsplice(), sendfile() ...)
	 * If we need to compute a TX checksum, we'll need to copy
	 * all frags to avoid possible bad checksum
	 */
	SKBFL_SHARED_FRAG = BIT(1),

	/* segment contains only zerocopy data and should not be
	 * charged to the kernel memory.
	 */
	SKBFL_PURE_ZEROCOPY = BIT(2),

	SKBFL_DONT_ORPHAN = BIT(3),

	/* page references are managed by the ubuf_info, so it's safe to
	 * use frags only up until ubuf_info is released
	 */
	SKBFL_MANAGED_FRAG_REFS = BIT(4),
};

#define SKBFL_ZEROCOPY_FRAG	(SKBFL_ZEROCOPY_ENABLE | SKBFL_SHARED_FRAG)
#define SKBFL_ALL_ZEROCOPY	(SKBFL_ZEROCOPY_FRAG | SKBFL_PURE_ZEROCOPY | \
				 SKBFL_DONT_ORPHAN | SKBFL_MANAGED_FRAG_REFS)

struct ubuf_info_ops {
	void (*complete)(struct sk_buff *, struct ubuf_info *,
			 bool zerocopy_success);
	/* has to be compatible with skb_zcopy_set() */
	int (*link_skb)(struct sk_buff *skb, struct ubuf_info *uarg);
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
	const struct ubuf_info_ops *ops;
	refcount_t refcnt;
	u8 flags;
};

struct ubuf_info_msgzc {
	struct ubuf_info ubuf;

	union {
		struct {
			unsigned long desc;
			void *ctx;
		};
		struct {
			u32 id;
			u16 len;
			u16 zerocopy:1;
			u32 bytelen;
		};
	};

	struct mmpin {
		struct user_struct *user;
		unsigned int num_pg;
	} mmp;
};

#define skb_uarg(SKB)	((struct ubuf_info *)(skb_shinfo(SKB)->destructor_arg))
#define uarg_to_msgzc(ubuf_ptr)	container_of((ubuf_ptr), struct ubuf_info_msgzc, \
					     ubuf)

int mm_account_pinned_pages(struct mmpin *mmp, size_t size);
void mm_unaccount_pinned_pages(struct mmpin *mmp);

/* Preserve some data across TX submission and completion.
 *
 * Note, this state is stored in the driver. Extending the layout
 * might need some special care.
 */
struct xsk_tx_metadata_compl {
	__u64 *tx_timestamp;
};

/* This data is invariant across clones and lives at
 * the end of the header data, ie. at skb->end.
 */
struct skb_shared_info {
	__u8		flags;
	__u8		meta_len;
	__u8		nr_frags;
	__u8		tx_flags;
	unsigned short	gso_size;
	/* Warning: this field is not always filled in (UFO)! */
	unsigned short	gso_segs;
	struct sk_buff	*frag_list;
	union {
		struct skb_shared_hwtstamps hwtstamps;
		struct xsk_tx_metadata_compl xsk_meta;
	};
	unsigned int	gso_type;
	u32		tskey;

	/*
	 * Warning : all fields before dataref are cleared in __alloc_skb()
	 */
	atomic_t	dataref;
	unsigned int	xdp_frags_size;

	/* Intermediate layers must ensure that destructor_arg
	 * remains valid until skb destructor */
	void *		destructor_arg;

	/* must be last field, see pskb_expand_head() */
	skb_frag_t	frags[MAX_SKB_FRAGS];
};

/**
 * DOC: dataref and headerless skbs
 *
 * Transport layers send out clones of payload skbs they hold for
 * retransmissions. To allow lower layers of the stack to prepend their headers
 * we split &skb_shared_info.dataref into two halves.
 * The lower 16 bits count the overall number of references.
 * The higher 16 bits indicate how many of the references are payload-only.
 * skb_header_cloned() checks if skb is allowed to add / write the headers.
 *
 * The creator of the skb (e.g. TCP) marks its skb as &sk_buff.nohdr
 * (via __skb_header_release()). Any clone created from marked skb will get
 * &sk_buff.hdr_len populated with the available headroom.
 * If there's the only clone in existence it's able to modify the headroom
 * at will. The sequence of calls inside the transport layer is::
 *
 *  <alloc skb>
 *  skb_reserve()
 *  __skb_header_release()
 *  skb_clone()
 *  // send the clone down the stack
 *
 * This is not a very generic construct and it depends on the transport layers
 * doing the right thing. In practice there's usually only one payload-only skb.
 * Having multiple payload-only skbs with different lengths of hdr_len is not
 * possible. The payload-only skbs should never leave their owner.
 */
#define SKB_DATAREF_SHIFT 16
#define SKB_DATAREF_MASK ((1 << SKB_DATAREF_SHIFT) - 1)


enum {
	SKB_FCLONE_UNAVAILABLE,	/* skb has no fclone (from head_cache) */
	SKB_FCLONE_ORIG,	/* orig skb (from fclone_cache) */
	SKB_FCLONE_CLONE,	/* companion fclone skb (from fclone_cache) */
};

enum {
	SKB_GSO_TCPV4 = 1 << 0,

	/* This indicates the skb is from an untrusted source. */
	SKB_GSO_DODGY = 1 << 1,

	/* This indicates the tcp segment has CWR set. */
	SKB_GSO_TCP_ECN = 1 << 2,

	SKB_GSO_TCP_FIXEDID = 1 << 3,

	SKB_GSO_TCPV6 = 1 << 4,

	SKB_GSO_FCOE = 1 << 5,

	SKB_GSO_GRE = 1 << 6,

	SKB_GSO_GRE_CSUM = 1 << 7,

	SKB_GSO_IPXIP4 = 1 << 8,

	SKB_GSO_IPXIP6 = 1 << 9,

	SKB_GSO_UDP_TUNNEL = 1 << 10,

	SKB_GSO_UDP_TUNNEL_CSUM = 1 << 11,

	SKB_GSO_PARTIAL = 1 << 12,

	SKB_GSO_TUNNEL_REMCSUM = 1 << 13,

	SKB_GSO_SCTP = 1 << 14,

	SKB_GSO_ESP = 1 << 15,

	SKB_GSO_UDP = 1 << 16,

	SKB_GSO_UDP_L4 = 1 << 17,

	SKB_GSO_FRAGLIST = 1 << 18,
};

#if BITS_PER_LONG > 32
#define NET_SKBUFF_DATA_USES_OFFSET 1
#endif

#ifdef NET_SKBUFF_DATA_USES_OFFSET
typedef unsigned int sk_buff_data_t;
#else
typedef unsigned char *sk_buff_data_t;
#endif

enum skb_tstamp_type {
	SKB_CLOCK_REALTIME,
	SKB_CLOCK_MONOTONIC,
	SKB_CLOCK_TAI,
	__SKB_CLOCK_MAX = SKB_CLOCK_TAI,
};

/**
 * DOC: Basic sk_buff geometry
 *
 * struct sk_buff itself is a metadata structure and does not hold any packet
 * data. All the data is held in associated buffers.
 *
 * &sk_buff.head points to the main "head" buffer. The head buffer is divided
 * into two parts:
 *
 *  - data buffer, containing headers and sometimes payload;
 *    this is the part of the skb operated on by the common helpers
 *    such as skb_put() or skb_pull();
 *  - shared info (struct skb_shared_info) which holds an array of pointers
 *    to read-only data in the (page, offset, length) format.
 *
 * Optionally &skb_shared_info.frag_list may point to another skb.
 *
 * Basic diagram may look like this::
 *
 *                                  ---------------
 *                                 | sk_buff       |
 *                                  ---------------
 *     ,---------------------------  + head
 *    /          ,-----------------  + data
 *   /          /      ,-----------  + tail
 *  |          |      |            , + end
 *  |          |      |           |
 *  v          v      v           v
 *   -----------------------------------------------
 *  | headroom | data |  tailroom | skb_shared_info |
 *   -----------------------------------------------
 *                                 + [page frag]
 *                                 + [page frag]
 *                                 + [page frag]
 *                                 + [page frag]       ---------
 *                                 + frag_list    --> | sk_buff |
 *                                                     ---------
 *
 */

/**
 *	struct sk_buff - socket buffer
 *	@next: Next buffer in list
 *	@prev: Previous buffer in list
 *	@tstamp: Time we arrived/left
 *	@skb_mstamp_ns: (aka @tstamp) earliest departure time; start point
 *		for retransmit timer
 *	@rbnode: RB tree node, alternative to next/prev for netem/tcp
 *	@list: queue head
 *	@ll_node: anchor in an llist (eg socket defer_list)
 *	@sk: Socket we are owned by
 *	@dev: Device we arrived on/are leaving by
 *	@dev_scratch: (aka @dev) alternate use of @dev when @dev would be %NULL
 *	@cb: Control buffer. Free for use by every layer. Put private vars here
 *	@_skb_refdst: destination entry (with norefcount bit)
 *	@len: Length of actual data
 *	@data_len: Data length
 *	@mac_len: Length of link layer header
 *	@hdr_len: writable header length of cloned skb
 *	@csum: Checksum (must include start/offset pair)
 *	@csum_start: Offset from skb->head where checksumming should start
 *	@csum_offset: Offset from csum_start where checksum should be stored
 *	@priority: Packet queueing priority
 *	@ignore_df: allow local fragmentation
 *	@cloned: Head may be cloned (check refcnt to be sure)
 *	@ip_summed: Driver fed us an IP checksum
 *	@nohdr: Payload reference only, must not modify header
 *	@pkt_type: Packet class
 *	@fclone: skbuff clone status
 *	@ipvs_property: skbuff is owned by ipvs
 *	@inner_protocol_type: whether the inner protocol is
 *		ENCAP_TYPE_ETHER or ENCAP_TYPE_IPPROTO
 *	@remcsum_offload: remote checksum offload is enabled
 *	@offload_fwd_mark: Packet was L2-forwarded in hardware
 *	@offload_l3_fwd_mark: Packet was L3-forwarded in hardware
 *	@tc_skip_classify: do not classify packet. set by IFB device
 *	@tc_at_ingress: used within tc_classify to distinguish in/egress
 *	@redirected: packet was redirected by packet classifier
 *	@from_ingress: packet was redirected from the ingress path
 *	@nf_skip_egress: packet shall skip nf egress - see netfilter_netdev.h
 *	@peeked: this packet has been seen already, so stats have been
 *		done for it, don't do them again
 *	@nf_trace: netfilter packet trace flag
 *	@protocol: Packet protocol from driver
 *	@destructor: Destruct function
 *	@tcp_tsorted_anchor: list structure for TCP (tp->tsorted_sent_queue)
 *	@_sk_redir: socket redirection information for skmsg
 *	@_nfct: Associated connection, if any (with nfctinfo bits)
 *	@skb_iif: ifindex of device we arrived on
 *	@tc_index: Traffic control index
 *	@hash: the packet hash
 *	@queue_mapping: Queue mapping for multiqueue devices
 *	@head_frag: skb was allocated from page fragments,
 *		not allocated by kmalloc() or vmalloc().
 *	@pfmemalloc: skbuff was allocated from PFMEMALLOC reserves
 *	@pp_recycle: mark the packet for recycling instead of freeing (implies
 *		page_pool support on driver)
 *	@active_extensions: active extensions (skb_ext_id types)
 *	@ndisc_nodetype: router type (from link layer)
 *	@ooo_okay: allow the mapping of a socket to a queue to be changed
 *	@l4_hash: indicate hash is a canonical 4-tuple hash over transport
 *		ports.
 *	@sw_hash: indicates hash was computed in software stack
 *	@wifi_acked_valid: wifi_acked was set
 *	@wifi_acked: whether frame was acked on wifi or not
 *	@no_fcs:  Request NIC to treat last 4 bytes as Ethernet FCS
 *	@encapsulation: indicates the inner headers in the skbuff are valid
 *	@encap_hdr_csum: software checksum is needed
 *	@csum_valid: checksum is already valid
 *	@csum_not_inet: use CRC32c to resolve CHECKSUM_PARTIAL
 *	@csum_complete_sw: checksum was completed by software
 *	@csum_level: indicates the number of consecutive checksums found in
 *		the packet minus one that have been verified as
 *		CHECKSUM_UNNECESSARY (max 3)
 *	@unreadable: indicates that at least 1 of the fragments in this skb is
 *		unreadable.
 *	@dst_pending_confirm: need to confirm neighbour
 *	@decrypted: Decrypted SKB
 *	@slow_gro: state present at GRO time, slower prepare step required
 *	@tstamp_type: When set, skb->tstamp has the
 *		delivery_time clock base of skb->tstamp.
 *	@napi_id: id of the NAPI struct this skb came from
 *	@sender_cpu: (aka @napi_id) source CPU in XPS
 *	@alloc_cpu: CPU which did the skb allocation.
 *	@secmark: security marking
 *	@mark: Generic packet mark
 *	@reserved_tailroom: (aka @mark) number of bytes of free space available
 *		at the tail of an sk_buff
 *	@vlan_all: vlan fields (proto & tci)
 *	@vlan_proto: vlan encapsulation protocol
 *	@vlan_tci: vlan tag control information
 *	@inner_protocol: Protocol (encapsulation)
 *	@inner_ipproto: (aka @inner_protocol) stores ipproto when
 *		skb->inner_protocol_type == ENCAP_TYPE_IPPROTO;
 *	@inner_transport_header: Inner transport layer header (encapsulation)
 *	@inner_network_header: Network layer header (encapsulation)
 *	@inner_mac_header: Link layer header (encapsulation)
 *	@transport_header: Transport layer header
 *	@network_header: Network layer header
 *	@mac_header: Link layer header
 *	@kcov_handle: KCOV remote handle for remote coverage collection
 *	@tail: Tail pointer
 *	@end: End pointer
 *	@head: Head of buffer
 *	@data: Data head pointer
 *	@truesize: Buffer size
 *	@users: User count - see {datagram,tcp}.c
 *	@extensions: allocated extensions, valid if active_extensions is nonzero
 */

struct sk_buff {
	union {
		struct {
			/* These two members must be first to match sk_buff_head. */
			struct sk_buff		*next;
			struct sk_buff		*prev;

			union {
				struct net_device	*dev;
				/* Some protocols might use this space to store information,
				 * while device pointer would be NULL.
				 * UDP receive path is one user.
				 */
				unsigned long		dev_scratch;
			};
		};
		struct rb_node		rbnode; /* used in netem, ip4 defrag, and tcp stack */
		struct list_head	list;
		struct llist_node	ll_node;
	};

	struct sock		*sk;

	union {
		ktime_t		tstamp;
		u64		skb_mstamp_ns; /* earliest departure time */
	};
	/*
	 * This is the control buffer. It is free to use for every
	 * layer. Please put your private variables there. If you
	 * want to keep them across layers you have to do a skb_clone()
	 * first. This is owned by whoever has the skb queued ATM.
	 */
	char			cb[48] __aligned(8);

	union {
		struct {
			unsigned long	_skb_refdst;
			void		(*destructor)(struct sk_buff *skb);
		};
		struct list_head	tcp_tsorted_anchor;
#ifdef CONFIG_NET_SOCK_MSG
		unsigned long		_sk_redir;
#endif
	};

#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
	unsigned long		 _nfct;
#endif
	unsigned int		len,
				data_len;
	__u16			mac_len,
				hdr_len;

	/* Following fields are _not_ copied in __copy_skb_header()
	 * Note that queue_mapping is here mostly to fill a hole.
	 */
	__u16			queue_mapping;

/* if you move cloned around you also must adapt those constants */
#ifdef __BIG_ENDIAN_BITFIELD
#define CLONED_MASK	(1 << 7)
#else
#define CLONED_MASK	1
#endif
#define CLONED_OFFSET		offsetof(struct sk_buff, __cloned_offset)

	/* private: */
	__u8			__cloned_offset[0];
	/* public: */
	__u8			cloned:1,
				nohdr:1,
				fclone:2,
				peeked:1,
				head_frag:1,
				pfmemalloc:1,
				pp_recycle:1; /* page_pool recycle indicator */
#ifdef CONFIG_SKB_EXTENSIONS
	__u8			active_extensions;
#endif

	/* Fields enclosed in headers group are copied
	 * using a single memcpy() in __copy_skb_header()
	 */
	struct_group(headers,

	/* private: */
	__u8			__pkt_type_offset[0];
	/* public: */
	__u8			pkt_type:3; /* see PKT_TYPE_MAX */
	__u8			ignore_df:1;
	__u8			dst_pending_confirm:1;
	__u8			ip_summed:2;
	__u8			ooo_okay:1;

	/* private: */
	__u8			__mono_tc_offset[0];
	/* public: */
	__u8			tstamp_type:2;	/* See skb_tstamp_type */
#ifdef CONFIG_NET_XGRESS
	__u8			tc_at_ingress:1;	/* See TC_AT_INGRESS_MASK */
	__u8			tc_skip_classify:1;
#endif
	__u8			remcsum_offload:1;
	__u8			csum_complete_sw:1;
	__u8			csum_level:2;
	__u8			inner_protocol_type:1;

	__u8			l4_hash:1;
	__u8			sw_hash:1;
#ifdef CONFIG_WIRELESS
	__u8			wifi_acked_valid:1;
	__u8			wifi_acked:1;
#endif
	__u8			no_fcs:1;
	/* Indicates the inner headers are valid in the skbuff. */
	__u8			encapsulation:1;
	__u8			encap_hdr_csum:1;
	__u8			csum_valid:1;
#ifdef CONFIG_IPV6_NDISC_NODETYPE
	__u8			ndisc_nodetype:2;
#endif

#if IS_ENABLED(CONFIG_IP_VS)
	__u8			ipvs_property:1;
#endif
#if IS_ENABLED(CONFIG_NETFILTER_XT_TARGET_TRACE) || IS_ENABLED(CONFIG_NF_TABLES)
	__u8			nf_trace:1;
#endif
#ifdef CONFIG_NET_SWITCHDEV
	__u8			offload_fwd_mark:1;
	__u8			offload_l3_fwd_mark:1;
#endif
	__u8			redirected:1;
#ifdef CONFIG_NET_REDIRECT
	__u8			from_ingress:1;
#endif
#ifdef CONFIG_NETFILTER_SKIP_EGRESS
	__u8			nf_skip_egress:1;
#endif
#ifdef CONFIG_SKB_DECRYPTED
	__u8			decrypted:1;
#endif
	__u8			slow_gro:1;
#if IS_ENABLED(CONFIG_IP_SCTP)
	__u8			csum_not_inet:1;
#endif
	__u8			unreadable:1;
#if defined(CONFIG_NET_SCHED) || defined(CONFIG_NET_XGRESS)
	__u16			tc_index;	/* traffic control index */
#endif

	u16			alloc_cpu;

	union {
		__wsum		csum;
		struct {
			__u16	csum_start;
			__u16	csum_offset;
		};
	};
	__u32			priority;
	int			skb_iif;
	__u32			hash;
	union {
		u32		vlan_all;
		struct {
			__be16	vlan_proto;
			__u16	vlan_tci;
		};
	};
#if defined(CONFIG_NET_RX_BUSY_POLL) || defined(CONFIG_XPS)
	union {
		unsigned int	napi_id;
		unsigned int	sender_cpu;
	};
#endif
#ifdef CONFIG_NETWORK_SECMARK
	__u32		secmark;
#endif

	union {
		__u32		mark;
		__u32		reserved_tailroom;
	};

	union {
		__be16		inner_protocol;
		__u8		inner_ipproto;
	};

	__u16			inner_transport_header;
	__u16			inner_network_header;
	__u16			inner_mac_header;

	__be16			protocol;
	__u16			transport_header;
	__u16			network_header;
	__u16			mac_header;

#ifdef CONFIG_KCOV
	u64			kcov_handle;
#endif

	); /* end headers group */

	/* These elements must be at the end, see alloc_skb() for details.  */
	sk_buff_data_t		tail;
	sk_buff_data_t		end;
	unsigned char		*head,
				*data;
	unsigned int		truesize;
	refcount_t		users;

#ifdef CONFIG_SKB_EXTENSIONS
	/* only usable after checking ->active_extensions != 0 */
	struct skb_ext		*extensions;
#endif
};

/* if you move pkt_type around you also must adapt those constants */
#ifdef __BIG_ENDIAN_BITFIELD
#define PKT_TYPE_MAX	(7 << 5)
#else
#define PKT_TYPE_MAX	7
#endif
#define PKT_TYPE_OFFSET		offsetof(struct sk_buff, __pkt_type_offset)

/* if you move tc_at_ingress or tstamp_type
 * around, you also must adapt these constants.
 */
#ifdef __BIG_ENDIAN_BITFIELD
#define SKB_TSTAMP_TYPE_MASK		(3 << 6)
#define SKB_TSTAMP_TYPE_RSHIFT		(6)
#define TC_AT_INGRESS_MASK		(1 << 5)
#else
#define SKB_TSTAMP_TYPE_MASK		(3)
#define TC_AT_INGRESS_MASK		(1 << 2)
#endif
#define SKB_BF_MONO_TC_OFFSET		offsetof(struct sk_buff, __mono_tc_offset)

#ifdef __KERNEL__
/*
 *	Handling routines are only of interest to the kernel
 */

#define SKB_ALLOC_FCLONE	0x01
#define SKB_ALLOC_RX		0x02
#define SKB_ALLOC_NAPI		0x04

/**
 * skb_pfmemalloc - Test if the skb was allocated from PFMEMALLOC reserves
 * @skb: buffer
 */
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
	skb->slow_gro |= !!dst;
	skb->_skb_refdst = (unsigned long)dst;
}

/**
 * skb_dst_set_noref - sets skb dst, hopefully, without taking reference
 * @skb: buffer
 * @dst: dst entry
 *
 * Sets skb dst, assuming a reference was not taken on dst.
 * If dst entry is cached, we do not take reference and dst_release
 * will be avoided by refdst_drop. If dst entry is not cached, we take
 * reference, so that last dst_release can destroy the dst immediately.
 */
static inline void skb_dst_set_noref(struct sk_buff *skb, struct dst_entry *dst)
{
	WARN_ON(!rcu_read_lock_held() && !rcu_read_lock_bh_held());
	skb->slow_gro |= !!dst;
	skb->_skb_refdst = (unsigned long)dst | SKB_DST_NOREF;
}

/**
 * skb_dst_is_noref - Test if skb dst isn't refcounted
 * @skb: buffer
 */
static inline bool skb_dst_is_noref(const struct sk_buff *skb)
{
	return (skb->_skb_refdst & SKB_DST_NOREF) && skb_dst(skb);
}

/* For mangling skb->pkt_type from user space side from applications
 * such as nft, tc, etc, we only allow a conservative subset of
 * possible pkt_types to be set.
*/
static inline bool skb_pkt_type_ok(u32 ptype)
{
	return ptype <= PACKET_OTHERHOST;
}

/**
 * skb_napi_id - Returns the skb's NAPI id
 * @skb: buffer
 */
static inline unsigned int skb_napi_id(const struct sk_buff *skb)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	return skb->napi_id;
#else
	return 0;
#endif
}

static inline bool skb_wifi_acked_valid(const struct sk_buff *skb)
{
#ifdef CONFIG_WIRELESS
	return skb->wifi_acked_valid;
#else
	return 0;
#endif
}

/**
 * skb_unref - decrement the skb's reference count
 * @skb: buffer
 *
 * Returns true if we can free the skb.
 */
static inline bool skb_unref(struct sk_buff *skb)
{
	if (unlikely(!skb))
		return false;
	if (!IS_ENABLED(CONFIG_DEBUG_NET) && likely(refcount_read(&skb->users) == 1))
		smp_rmb();
	else if (likely(!refcount_dec_and_test(&skb->users)))
		return false;

	return true;
}

static inline bool skb_data_unref(const struct sk_buff *skb,
				  struct skb_shared_info *shinfo)
{
	int bias;

	if (!skb->cloned)
		return true;

	bias = skb->nohdr ? (1 << SKB_DATAREF_SHIFT) + 1 : 1;

	if (atomic_read(&shinfo->dataref) == bias)
		smp_rmb();
	else if (atomic_sub_return(bias, &shinfo->dataref))
		return false;

	return true;
}

void __fix_address sk_skb_reason_drop(struct sock *sk, struct sk_buff *skb,
				      enum skb_drop_reason reason);

static inline void
kfree_skb_reason(struct sk_buff *skb, enum skb_drop_reason reason)
{
	sk_skb_reason_drop(NULL, skb, reason);
}

/**
 *	kfree_skb - free an sk_buff with 'NOT_SPECIFIED' reason
 *	@skb: buffer to free
 */
static inline void kfree_skb(struct sk_buff *skb)
{
	kfree_skb_reason(skb, SKB_DROP_REASON_NOT_SPECIFIED);
}

void skb_release_head_state(struct sk_buff *skb);
void kfree_skb_list_reason(struct sk_buff *segs,
			   enum skb_drop_reason reason);
void skb_dump(const char *level, const struct sk_buff *skb, bool full_pkt);
void skb_tx_error(struct sk_buff *skb);

static inline void kfree_skb_list(struct sk_buff *segs)
{
	kfree_skb_list_reason(segs, SKB_DROP_REASON_NOT_SPECIFIED);
}

#ifdef CONFIG_TRACEPOINTS
void consume_skb(struct sk_buff *skb);
#else
static inline void consume_skb(struct sk_buff *skb)
{
	return kfree_skb(skb);
}
#endif

void __consume_stateless_skb(struct sk_buff *skb);
void  __kfree_skb(struct sk_buff *skb);

void kfree_skb_partial(struct sk_buff *skb, bool head_stolen);
bool skb_try_coalesce(struct sk_buff *to, struct sk_buff *from,
		      bool *fragstolen, int *delta_truesize);

struct sk_buff *__alloc_skb(unsigned int size, gfp_t priority, int flags,
			    int node);
struct sk_buff *__build_skb(void *data, unsigned int frag_size);
struct sk_buff *build_skb(void *data, unsigned int frag_size);
struct sk_buff *build_skb_around(struct sk_buff *skb,
				 void *data, unsigned int frag_size);
void skb_attempt_defer_free(struct sk_buff *skb);

struct sk_buff *napi_build_skb(void *data, unsigned int frag_size);
struct sk_buff *slab_build_skb(void *data);

/**
 * alloc_skb - allocate a network buffer
 * @size: size to allocate
 * @priority: allocation mask
 *
 * This function is a convenient wrapper around __alloc_skb().
 */
static inline struct sk_buff *alloc_skb(unsigned int size,
					gfp_t priority)
{
	return __alloc_skb(size, priority, 0, NUMA_NO_NODE);
}

struct sk_buff *alloc_skb_with_frags(unsigned long header_len,
				     unsigned long data_len,
				     int max_page_order,
				     int *errcode,
				     gfp_t gfp_mask);
struct sk_buff *alloc_skb_for_msg(struct sk_buff *first);

/* Layout of fast clones : [skb1][skb2][fclone_ref] */
struct sk_buff_fclones {
	struct sk_buff	skb1;

	struct sk_buff	skb2;

	refcount_t	fclone_ref;
};

/**
 *	skb_fclone_busy - check if fclone is busy
 *	@sk: socket
 *	@skb: buffer
 *
 * Returns true if skb is a fast clone, and its clone is not freed.
 * Some drivers call skb_orphan() in their ndo_start_xmit(),
 * so we also check that didn't happen.
 */
static inline bool skb_fclone_busy(const struct sock *sk,
				   const struct sk_buff *skb)
{
	const struct sk_buff_fclones *fclones;

	fclones = container_of(skb, struct sk_buff_fclones, skb1);

	return skb->fclone == SKB_FCLONE_ORIG &&
	       refcount_read(&fclones->fclone_ref) > 1 &&
	       READ_ONCE(fclones->skb2.sk) == sk;
}

/**
 * alloc_skb_fclone - allocate a network buffer from fclone cache
 * @size: size to allocate
 * @priority: allocation mask
 *
 * This function is a convenient wrapper around __alloc_skb().
 */
static inline struct sk_buff *alloc_skb_fclone(unsigned int size,
					       gfp_t priority)
{
	return __alloc_skb(size, priority, SKB_ALLOC_FCLONE, NUMA_NO_NODE);
}

struct sk_buff *skb_morph(struct sk_buff *dst, struct sk_buff *src);
void skb_headers_offset_update(struct sk_buff *skb, int off);
int skb_copy_ubufs(struct sk_buff *skb, gfp_t gfp_mask);
struct sk_buff *skb_clone(struct sk_buff *skb, gfp_t priority);
void skb_copy_header(struct sk_buff *new, const struct sk_buff *old);
struct sk_buff *skb_copy(const struct sk_buff *skb, gfp_t priority);
struct sk_buff *__pskb_copy_fclone(struct sk_buff *skb, int headroom,
				   gfp_t gfp_mask, bool fclone);
static inline struct sk_buff *__pskb_copy(struct sk_buff *skb, int headroom,
					  gfp_t gfp_mask)
{
	return __pskb_copy_fclone(skb, headroom, gfp_mask, false);
}

int pskb_expand_head(struct sk_buff *skb, int nhead, int ntail, gfp_t gfp_mask);
struct sk_buff *skb_realloc_headroom(struct sk_buff *skb,
				     unsigned int headroom);
struct sk_buff *skb_expand_head(struct sk_buff *skb, unsigned int headroom);
struct sk_buff *skb_copy_expand(const struct sk_buff *skb, int newheadroom,
				int newtailroom, gfp_t priority);
int __must_check skb_to_sgvec_nomark(struct sk_buff *skb, struct scatterlist *sg,
				     int offset, int len);
int __must_check skb_to_sgvec(struct sk_buff *skb, struct scatterlist *sg,
			      int offset, int len);
int skb_cow_data(struct sk_buff *skb, int tailbits, struct sk_buff **trailer);
int __skb_pad(struct sk_buff *skb, int pad, bool free_on_error);

/**
 *	skb_pad			-	zero pad the tail of an skb
 *	@skb: buffer to pad
 *	@pad: space to pad
 *
 *	Ensure that a buffer is followed by a padding area that is zero
 *	filled. Used by network drivers which may DMA or transfer data
 *	beyond the buffer end onto the wire.
 *
 *	May return error in out of memory cases. The skb is freed on error.
 */
static inline int skb_pad(struct sk_buff *skb, int pad)
{
	return __skb_pad(skb, pad, true);
}
#define dev_kfree_skb(a)	consume_skb(a)

int skb_append_pagefrags(struct sk_buff *skb, struct page *page,
			 int offset, size_t size, size_t max_frags);

struct skb_seq_state {
	__u32		lower_offset;
	__u32		upper_offset;
	__u32		frag_idx;
	__u32		stepped_offset;
	struct sk_buff	*root_skb;
	struct sk_buff	*cur_skb;
	__u8		*frag_data;
	__u32		frag_off;
};

void skb_prepare_seq_read(struct sk_buff *skb, unsigned int from,
			  unsigned int to, struct skb_seq_state *st);
unsigned int skb_seq_read(unsigned int consumed, const u8 **data,
			  struct skb_seq_state *st);
void skb_abort_seq_read(struct skb_seq_state *st);
int skb_copy_seq_read(struct skb_seq_state *st, int offset, void *to, int len);

unsigned int skb_find_text(struct sk_buff *skb, unsigned int from,
			   unsigned int to, struct ts_config *config);

/*
 * Packet hash types specify the type of hash in skb_set_hash.
 *
 * Hash types refer to the protocol layer addresses which are used to
 * construct a packet's hash. The hashes are used to differentiate or identify
 * flows of the protocol layer for the hash type. Hash types are either
 * layer-2 (L2), layer-3 (L3), or layer-4 (L4).
 *
 * Properties of hashes:
 *
 * 1) Two packets in different flows have different hash values
 * 2) Two packets in the same flow should have the same hash value
 *
 * A hash at a higher layer is considered to be more specific. A driver should
 * set the most specific hash possible.
 *
 * A driver cannot indicate a more specific hash than the layer at which a hash
 * was computed. For instance an L3 hash cannot be set as an L4 hash.
 *
 * A driver may indicate a hash level which is less specific than the
 * actual layer the hash was computed on. For instance, a hash computed
 * at L4 may be considered an L3 hash. This should only be done if the
 * driver can't unambiguously determine that the HW computed the hash at
 * the higher layer. Note that the "should" in the second property above
 * permits this.
 */
enum pkt_hash_types {
	PKT_HASH_TYPE_NONE,	/* Undefined type */
	PKT_HASH_TYPE_L2,	/* Input: src_MAC, dest_MAC */
	PKT_HASH_TYPE_L3,	/* Input: src_IP, dst_IP */
	PKT_HASH_TYPE_L4,	/* Input: src_IP, dst_IP, src_port, dst_port */
};

static inline void skb_clear_hash(struct sk_buff *skb)
{
	skb->hash = 0;
	skb->sw_hash = 0;
	skb->l4_hash = 0;
}

static inline void skb_clear_hash_if_not_l4(struct sk_buff *skb)
{
	if (!skb->l4_hash)
		skb_clear_hash(skb);
}

static inline void
__skb_set_hash(struct sk_buff *skb, __u32 hash, bool is_sw, bool is_l4)
{
	skb->l4_hash = is_l4;
	skb->sw_hash = is_sw;
	skb->hash = hash;
}

static inline void
skb_set_hash(struct sk_buff *skb, __u32 hash, enum pkt_hash_types type)
{
	/* Used by drivers to set hash from HW */
	__skb_set_hash(skb, hash, false, type == PKT_HASH_TYPE_L4);
}

static inline void
__skb_set_sw_hash(struct sk_buff *skb, __u32 hash, bool is_l4)
{
	__skb_set_hash(skb, hash, true, is_l4);
}

u32 __skb_get_hash_symmetric_net(const struct net *net, const struct sk_buff *skb);

static inline u32 __skb_get_hash_symmetric(const struct sk_buff *skb)
{
	return __skb_get_hash_symmetric_net(NULL, skb);
}

void __skb_get_hash_net(const struct net *net, struct sk_buff *skb);
u32 skb_get_poff(const struct sk_buff *skb);
u32 __skb_get_poff(const struct sk_buff *skb, const void *data,
		   const struct flow_keys_basic *keys, int hlen);
__be32 __skb_flow_get_ports(const struct sk_buff *skb, int thoff, u8 ip_proto,
			    const void *data, int hlen_proto);

static inline __be32 skb_flow_get_ports(const struct sk_buff *skb,
					int thoff, u8 ip_proto)
{
	return __skb_flow_get_ports(skb, thoff, ip_proto, NULL, 0);
}

void skb_flow_dissector_init(struct flow_dissector *flow_dissector,
			     const struct flow_dissector_key *key,
			     unsigned int key_count);

struct bpf_flow_dissector;
u32 bpf_flow_dissect(struct bpf_prog *prog, struct bpf_flow_dissector *ctx,
		     __be16 proto, int nhoff, int hlen, unsigned int flags);

bool __skb_flow_dissect(const struct net *net,
			const struct sk_buff *skb,
			struct flow_dissector *flow_dissector,
			void *target_container, const void *data,
			__be16 proto, int nhoff, int hlen, unsigned int flags);

static inline bool skb_flow_dissect(const struct sk_buff *skb,
				    struct flow_dissector *flow_dissector,
				    void *target_container, unsigned int flags)
{
	return __skb_flow_dissect(NULL, skb, flow_dissector,
				  target_container, NULL, 0, 0, 0, flags);
}

static inline bool skb_flow_dissect_flow_keys(const struct sk_buff *skb,
					      struct flow_keys *flow,
					      unsigned int flags)
{
	memset(flow, 0, sizeof(*flow));
	return __skb_flow_dissect(NULL, skb, &flow_keys_dissector,
				  flow, NULL, 0, 0, 0, flags);
}

static inline bool
skb_flow_dissect_flow_keys_basic(const struct net *net,
				 const struct sk_buff *skb,
				 struct flow_keys_basic *flow,
				 const void *data, __be16 proto,
				 int nhoff, int hlen, unsigned int flags)
{
	memset(flow, 0, sizeof(*flow));
	return __skb_flow_dissect(net, skb, &flow_keys_basic_dissector, flow,
				  data, proto, nhoff, hlen, flags);
}

void skb_flow_dissect_meta(const struct sk_buff *skb,
			   struct flow_dissector *flow_dissector,
			   void *target_container);

/* Gets a skb connection tracking info, ctinfo map should be a
 * map of mapsize to translate enum ip_conntrack_info states
 * to user states.
 */
void
skb_flow_dissect_ct(const struct sk_buff *skb,
		    struct flow_dissector *flow_dissector,
		    void *target_container,
		    u16 *ctinfo_map, size_t mapsize,
		    bool post_ct, u16 zone);
void
skb_flow_dissect_tunnel_info(const struct sk_buff *skb,
			     struct flow_dissector *flow_dissector,
			     void *target_container);

void skb_flow_dissect_hash(const struct sk_buff *skb,
			   struct flow_dissector *flow_dissector,
			   void *target_container);

static inline __u32 skb_get_hash_net(const struct net *net, struct sk_buff *skb)
{
	if (!skb->l4_hash && !skb->sw_hash)
		__skb_get_hash_net(net, skb);

	return skb->hash;
}

static inline __u32 skb_get_hash(struct sk_buff *skb)
{
	if (!skb->l4_hash && !skb->sw_hash)
		__skb_get_hash_net(NULL, skb);

	return skb->hash;
}

static inline __u32 skb_get_hash_flowi6(struct sk_buff *skb, const struct flowi6 *fl6)
{
	if (!skb->l4_hash && !skb->sw_hash) {
		struct flow_keys keys;
		__u32 hash = __get_hash_from_flowi6(fl6, &keys);

		__skb_set_sw_hash(skb, hash, flow_keys_have_l4(&keys));
	}

	return skb->hash;
}

__u32 skb_get_hash_perturb(const struct sk_buff *skb,
			   const siphash_key_t *perturb);

static inline __u32 skb_get_hash_raw(const struct sk_buff *skb)
{
	return skb->hash;
}

static inline void skb_copy_hash(struct sk_buff *to, const struct sk_buff *from)
{
	to->hash = from->hash;
	to->sw_hash = from->sw_hash;
	to->l4_hash = from->l4_hash;
};

static inline int skb_cmp_decrypted(const struct sk_buff *skb1,
				    const struct sk_buff *skb2)
{
#ifdef CONFIG_SKB_DECRYPTED
	return skb2->decrypted - skb1->decrypted;
#else
	return 0;
#endif
}

static inline bool skb_is_decrypted(const struct sk_buff *skb)
{
#ifdef CONFIG_SKB_DECRYPTED
	return skb->decrypted;
#else
	return false;
#endif
}

static inline void skb_copy_decrypted(struct sk_buff *to,
				      const struct sk_buff *from)
{
#ifdef CONFIG_SKB_DECRYPTED
	to->decrypted = from->decrypted;
#endif
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

static inline void skb_set_end_offset(struct sk_buff *skb, unsigned int offset)
{
	skb->end = offset;
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

static inline void skb_set_end_offset(struct sk_buff *skb, unsigned int offset)
{
	skb->end = skb->head + offset;
}
#endif

extern const struct ubuf_info_ops msg_zerocopy_ubuf_ops;

struct ubuf_info *msg_zerocopy_realloc(struct sock *sk, size_t size,
				       struct ubuf_info *uarg);

void msg_zerocopy_put_abort(struct ubuf_info *uarg, bool have_uref);

int __zerocopy_sg_from_iter(struct msghdr *msg, struct sock *sk,
			    struct sk_buff *skb, struct iov_iter *from,
			    size_t length);

int zerocopy_fill_skb_from_iter(struct sk_buff *skb,
				struct iov_iter *from, size_t length);

static inline int skb_zerocopy_iter_dgram(struct sk_buff *skb,
					  struct msghdr *msg, int len)
{
	return __zerocopy_sg_from_iter(msg, skb->sk, skb, &msg->msg_iter, len);
}

int skb_zerocopy_iter_stream(struct sock *sk, struct sk_buff *skb,
			     struct msghdr *msg, int len,
			     struct ubuf_info *uarg);

/* Internal */
#define skb_shinfo(SKB)	((struct skb_shared_info *)(skb_end_pointer(SKB)))

static inline struct skb_shared_hwtstamps *skb_hwtstamps(struct sk_buff *skb)
{
	return &skb_shinfo(skb)->hwtstamps;
}

static inline struct ubuf_info *skb_zcopy(struct sk_buff *skb)
{
	bool is_zcopy = skb && skb_shinfo(skb)->flags & SKBFL_ZEROCOPY_ENABLE;

	return is_zcopy ? skb_uarg(skb) : NULL;
}

static inline bool skb_zcopy_pure(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->flags & SKBFL_PURE_ZEROCOPY;
}

static inline bool skb_zcopy_managed(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->flags & SKBFL_MANAGED_FRAG_REFS;
}

static inline bool skb_pure_zcopy_same(const struct sk_buff *skb1,
				       const struct sk_buff *skb2)
{
	return skb_zcopy_pure(skb1) == skb_zcopy_pure(skb2);
}

static inline void net_zcopy_get(struct ubuf_info *uarg)
{
	refcount_inc(&uarg->refcnt);
}

static inline void skb_zcopy_init(struct sk_buff *skb, struct ubuf_info *uarg)
{
	skb_shinfo(skb)->destructor_arg = uarg;
	skb_shinfo(skb)->flags |= uarg->flags;
}

static inline void skb_zcopy_set(struct sk_buff *skb, struct ubuf_info *uarg,
				 bool *have_ref)
{
	if (skb && uarg && !skb_zcopy(skb)) {
		if (unlikely(have_ref && *have_ref))
			*have_ref = false;
		else
			net_zcopy_get(uarg);
		skb_zcopy_init(skb, uarg);
	}
}

static inline void skb_zcopy_set_nouarg(struct sk_buff *skb, void *val)
{
	skb_shinfo(skb)->destructor_arg = (void *)((uintptr_t) val | 0x1UL);
	skb_shinfo(skb)->flags |= SKBFL_ZEROCOPY_FRAG;
}

static inline bool skb_zcopy_is_nouarg(struct sk_buff *skb)
{
	return (uintptr_t) skb_shinfo(skb)->destructor_arg & 0x1UL;
}

static inline void *skb_zcopy_get_nouarg(struct sk_buff *skb)
{
	return (void *)((uintptr_t) skb_shinfo(skb)->destructor_arg & ~0x1UL);
}

static inline void net_zcopy_put(struct ubuf_info *uarg)
{
	if (uarg)
		uarg->ops->complete(NULL, uarg, true);
}

static inline void net_zcopy_put_abort(struct ubuf_info *uarg, bool have_uref)
{
	if (uarg) {
		if (uarg->ops == &msg_zerocopy_ubuf_ops)
			msg_zerocopy_put_abort(uarg, have_uref);
		else if (have_uref)
			net_zcopy_put(uarg);
	}
}

/* Release a reference on a zerocopy structure */
static inline void skb_zcopy_clear(struct sk_buff *skb, bool zerocopy_success)
{
	struct ubuf_info *uarg = skb_zcopy(skb);

	if (uarg) {
		if (!skb_zcopy_is_nouarg(skb))
			uarg->ops->complete(skb, uarg, zerocopy_success);

		skb_shinfo(skb)->flags &= ~SKBFL_ALL_ZEROCOPY;
	}
}

void __skb_zcopy_downgrade_managed(struct sk_buff *skb);

static inline void skb_zcopy_downgrade_managed(struct sk_buff *skb)
{
	if (unlikely(skb_zcopy_managed(skb)))
		__skb_zcopy_downgrade_managed(skb);
}

/* Return true if frags in this skb are readable by the host. */
static inline bool skb_frags_readable(const struct sk_buff *skb)
{
	return !skb->unreadable;
}

static inline void skb_mark_not_on_list(struct sk_buff *skb)
{
	skb->next = NULL;
}

static inline void skb_poison_list(struct sk_buff *skb)
{
#ifdef CONFIG_DEBUG_NET
	skb->next = SKB_LIST_POISON_NEXT;
#endif
}

/* Iterate through singly-linked GSO fragments of an skb. */
#define skb_list_walk_safe(first, skb, next_skb)                               \
	for ((skb) = (first), (next_skb) = (skb) ? (skb)->next : NULL; (skb);  \
	     (skb) = (next_skb), (next_skb) = (skb) ? (skb)->next : NULL)

static inline void skb_list_del_init(struct sk_buff *skb)
{
	__list_del_entry(&skb->list);
	skb_mark_not_on_list(skb);
}

/**
 *	skb_queue_empty - check if a queue is empty
 *	@list: queue head
 *
 *	Returns true if the queue is empty, false otherwise.
 */
static inline int skb_queue_empty(const struct sk_buff_head *list)
{
	return list->next == (const struct sk_buff *) list;
}

/**
 *	skb_queue_empty_lockless - check if a queue is empty
 *	@list: queue head
 *
 *	Returns true if the queue is empty, false otherwise.
 *	This variant can be used in lockless contexts.
 */
static inline bool skb_queue_empty_lockless(const struct sk_buff_head *list)
{
	return READ_ONCE(list->next) == (const struct sk_buff *) list;
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
	return skb->next == (const struct sk_buff *) list;
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
	return skb->prev == (const struct sk_buff *) list;
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
	refcount_inc(&skb->users);
	return skb;
}

/*
 * If users == 1, we are the only owner and can avoid redundant atomic changes.
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

static inline int skb_unclone(struct sk_buff *skb, gfp_t pri)
{
	might_sleep_if(gfpflags_allow_blocking(pri));

	if (skb_cloned(skb))
		return pskb_expand_head(skb, 0, 0, pri);

	return 0;
}

/* This variant of skb_unclone() makes sure skb->truesize
 * and skb_end_offset() are not changed, whenever a new skb->head is needed.
 *
 * Indeed there is no guarantee that ksize(kmalloc(X)) == ksize(kmalloc(X))
 * when various debugging features are in place.
 */
int __skb_unclone_keeptruesize(struct sk_buff *skb, gfp_t pri);
static inline int skb_unclone_keeptruesize(struct sk_buff *skb, gfp_t pri)
{
	might_sleep_if(gfpflags_allow_blocking(pri));

	if (skb_cloned(skb))
		return __skb_unclone_keeptruesize(skb, pri);
	return 0;
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

static inline int skb_header_unclone(struct sk_buff *skb, gfp_t pri)
{
	might_sleep_if(gfpflags_allow_blocking(pri));

	if (skb_header_cloned(skb))
		return pskb_expand_head(skb, 0, 0, pri);

	return 0;
}

/**
 * __skb_header_release() - allow clones to use the headroom
 * @skb: buffer to operate on
 *
 * See "DOC: dataref and headerless skbs".
 */
static inline void __skb_header_release(struct sk_buff *skb)
{
	skb->nohdr = 1;
	atomic_set(&skb_shinfo(skb)->dataref, 1 + (1 << SKB_DATAREF_SHIFT));
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
	return refcount_read(&skb->users) != 1;
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
	might_sleep_if(gfpflags_allow_blocking(pri));
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
 *	a packet that's being forwarded.
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
	might_sleep_if(gfpflags_allow_blocking(pri));
	if (skb_cloned(skb)) {
		struct sk_buff *nskb = skb_copy(skb, pri);

		/* Free our shared copy */
		if (likely(nskb))
			consume_skb(skb);
		else
			kfree_skb(skb);
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
 *	__skb_peek - peek at the head of a non-empty &sk_buff_head
 *	@list_: list to peek at
 *
 *	Like skb_peek(), but the caller knows that the list is not empty.
 */
static inline struct sk_buff *__skb_peek(const struct sk_buff_head *list_)
{
	return list_->next;
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
	struct sk_buff *skb = READ_ONCE(list_->prev);

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
 *	skb_queue_len_lockless	- get queue length
 *	@list_: list to measure
 *
 *	Return the length of an &sk_buff queue.
 *	This variant can be used in lockless contexts.
 */
static inline __u32 skb_queue_len_lockless(const struct sk_buff_head *list_)
{
	return READ_ONCE(list_->qlen);
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
static inline void __skb_insert(struct sk_buff *newsk,
				struct sk_buff *prev, struct sk_buff *next,
				struct sk_buff_head *list)
{
	/* See skb_queue_empty_lockless() and skb_peek_tail()
	 * for the opposite READ_ONCE()
	 */
	WRITE_ONCE(newsk->next, next);
	WRITE_ONCE(newsk->prev, prev);
	WRITE_ONCE(((struct sk_buff_list *)next)->prev, newsk);
	WRITE_ONCE(((struct sk_buff_list *)prev)->next, newsk);
	WRITE_ONCE(list->qlen, list->qlen + 1);
}

static inline void __skb_queue_splice(const struct sk_buff_head *list,
				      struct sk_buff *prev,
				      struct sk_buff *next)
{
	struct sk_buff *first = list->next;
	struct sk_buff *last = list->prev;

	WRITE_ONCE(first->prev, prev);
	WRITE_ONCE(prev->next, first);

	WRITE_ONCE(last->next, next);
	WRITE_ONCE(next->prev, last);
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
	__skb_insert(newsk, prev, ((struct sk_buff_list *)prev)->next, list);
}

void skb_append(struct sk_buff *old, struct sk_buff *newsk,
		struct sk_buff_head *list);

static inline void __skb_queue_before(struct sk_buff_head *list,
				      struct sk_buff *next,
				      struct sk_buff *newsk)
{
	__skb_insert(newsk, ((struct sk_buff_list *)next)->prev, next, list);
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
static inline void __skb_queue_head(struct sk_buff_head *list,
				    struct sk_buff *newsk)
{
	__skb_queue_after(list, (struct sk_buff *)list, newsk);
}
void skb_queue_head(struct sk_buff_head *list, struct sk_buff *newsk);

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
static inline void __skb_queue_tail(struct sk_buff_head *list,
				   struct sk_buff *newsk)
{
	__skb_queue_before(list, (struct sk_buff *)list, newsk);
}
void skb_queue_tail(struct sk_buff_head *list, struct sk_buff *newsk);

/*
 * remove sk_buff from list. _Must_ be called atomically, and with
 * the list known..
 */
void skb_unlink(struct sk_buff *skb, struct sk_buff_head *list);
static inline void __skb_unlink(struct sk_buff *skb, struct sk_buff_head *list)
{
	struct sk_buff *next, *prev;

	WRITE_ONCE(list->qlen, list->qlen - 1);
	next	   = skb->next;
	prev	   = skb->prev;
	skb->next  = skb->prev = NULL;
	WRITE_ONCE(next->prev, prev);
	WRITE_ONCE(prev->next, next);
}

/**
 *	__skb_dequeue - remove from the head of the queue
 *	@list: list to dequeue from
 *
 *	Remove the head of the list. This function does not take any locks
 *	so must be used with appropriate locks held only. The head item is
 *	returned or %NULL if the list is empty.
 */
static inline struct sk_buff *__skb_dequeue(struct sk_buff_head *list)
{
	struct sk_buff *skb = skb_peek(list);
	if (skb)
		__skb_unlink(skb, list);
	return skb;
}
struct sk_buff *skb_dequeue(struct sk_buff_head *list);

/**
 *	__skb_dequeue_tail - remove from the tail of the queue
 *	@list: list to dequeue from
 *
 *	Remove the tail of the list. This function does not take any locks
 *	so must be used with appropriate locks held only. The tail item is
 *	returned or %NULL if the list is empty.
 */
static inline struct sk_buff *__skb_dequeue_tail(struct sk_buff_head *list)
{
	struct sk_buff *skb = skb_peek_tail(list);
	if (skb)
		__skb_unlink(skb, list);
	return skb;
}
struct sk_buff *skb_dequeue_tail(struct sk_buff_head *list);


static inline bool skb_is_nonlinear(const struct sk_buff *skb)
{
	return skb->data_len;
}

static inline unsigned int skb_headlen(const struct sk_buff *skb)
{
	return skb->len - skb->data_len;
}

static inline unsigned int __skb_pagelen(const struct sk_buff *skb)
{
	unsigned int i, len = 0;

	for (i = skb_shinfo(skb)->nr_frags - 1; (int)i >= 0; i--)
		len += skb_frag_size(&skb_shinfo(skb)->frags[i]);
	return len;
}

static inline unsigned int skb_pagelen(const struct sk_buff *skb)
{
	return skb_headlen(skb) + __skb_pagelen(skb);
}

static inline void skb_frag_fill_netmem_desc(skb_frag_t *frag,
					     netmem_ref netmem, int off,
					     int size)
{
	frag->netmem = netmem;
	frag->offset = off;
	skb_frag_size_set(frag, size);
}

static inline void skb_frag_fill_page_desc(skb_frag_t *frag,
					   struct page *page,
					   int off, int size)
{
	skb_frag_fill_netmem_desc(frag, page_to_netmem(page), off, size);
}

static inline void __skb_fill_netmem_desc_noacc(struct skb_shared_info *shinfo,
						int i, netmem_ref netmem,
						int off, int size)
{
	skb_frag_t *frag = &shinfo->frags[i];

	skb_frag_fill_netmem_desc(frag, netmem, off, size);
}

static inline void __skb_fill_page_desc_noacc(struct skb_shared_info *shinfo,
					      int i, struct page *page,
					      int off, int size)
{
	__skb_fill_netmem_desc_noacc(shinfo, i, page_to_netmem(page), off,
				     size);
}

/**
 * skb_len_add - adds a number to len fields of skb
 * @skb: buffer to add len to
 * @delta: number of bytes to add
 */
static inline void skb_len_add(struct sk_buff *skb, int delta)
{
	skb->len += delta;
	skb->data_len += delta;
	skb->truesize += delta;
}

/**
 * __skb_fill_netmem_desc - initialise a fragment in an skb
 * @skb: buffer containing fragment to be initialised
 * @i: fragment index to initialise
 * @netmem: the netmem to use for this fragment
 * @off: the offset to the data with @page
 * @size: the length of the data
 *
 * Initialises the @i'th fragment of @skb to point to &size bytes at
 * offset @off within @page.
 *
 * Does not take any additional reference on the fragment.
 */
static inline void __skb_fill_netmem_desc(struct sk_buff *skb, int i,
					  netmem_ref netmem, int off, int size)
{
	struct page *page;

	__skb_fill_netmem_desc_noacc(skb_shinfo(skb), i, netmem, off, size);

	if (netmem_is_net_iov(netmem)) {
		skb->unreadable = true;
		return;
	}

	page = netmem_to_page(netmem);

	/* Propagate page pfmemalloc to the skb if we can. The problem is
	 * that not all callers have unique ownership of the page but rely
	 * on page_is_pfmemalloc doing the right thing(tm).
	 */
	page = compound_head(page);
	if (page_is_pfmemalloc(page))
		skb->pfmemalloc = true;
}

static inline void __skb_fill_page_desc(struct sk_buff *skb, int i,
					struct page *page, int off, int size)
{
	__skb_fill_netmem_desc(skb, i, page_to_netmem(page), off, size);
}

static inline void skb_fill_netmem_desc(struct sk_buff *skb, int i,
					netmem_ref netmem, int off, int size)
{
	__skb_fill_netmem_desc(skb, i, netmem, off, size);
	skb_shinfo(skb)->nr_frags = i + 1;
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
 * @skb to point to @size bytes at offset @off within @page. In
 * addition updates @skb such that @i is the last fragment.
 *
 * Does not take any additional reference on the fragment.
 */
static inline void skb_fill_page_desc(struct sk_buff *skb, int i,
				      struct page *page, int off, int size)
{
	skb_fill_netmem_desc(skb, i, page_to_netmem(page), off, size);
}

/**
 * skb_fill_page_desc_noacc - initialise a paged fragment in an skb
 * @skb: buffer containing fragment to be initialised
 * @i: paged fragment index to initialise
 * @page: the page to use for this fragment
 * @off: the offset to the data with @page
 * @size: the length of the data
 *
 * Variant of skb_fill_page_desc() which does not deal with
 * pfmemalloc, if page is not owned by us.
 */
static inline void skb_fill_page_desc_noacc(struct sk_buff *skb, int i,
					    struct page *page, int off,
					    int size)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);

	__skb_fill_page_desc_noacc(shinfo, i, page, off, size);
	shinfo->nr_frags = i + 1;
}

void skb_add_rx_frag_netmem(struct sk_buff *skb, int i, netmem_ref netmem,
			    int off, int size, unsigned int truesize);

static inline void skb_add_rx_frag(struct sk_buff *skb, int i,
				   struct page *page, int off, int size,
				   unsigned int truesize)
{
	skb_add_rx_frag_netmem(skb, i, page_to_netmem(page), off, size,
			       truesize);
}

void skb_coalesce_rx_frag(struct sk_buff *skb, int i, int size,
			  unsigned int truesize);

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

static inline void skb_assert_len(struct sk_buff *skb)
{
#ifdef CONFIG_DEBUG_NET
	if (WARN_ONCE(!skb->len, "%s\n", __func__))
		DO_ONCE_LITE(skb_dump, KERN_ERR, skb, false);
#endif /* CONFIG_DEBUG_NET */
}

#if defined(CONFIG_FAIL_SKB_REALLOC)
void skb_might_realloc(struct sk_buff *skb);
#else
static inline void skb_might_realloc(struct sk_buff *skb) {}
#endif

/*
 *	Add data to an sk_buff
 */
void *pskb_put(struct sk_buff *skb, struct sk_buff *tail, int len);
void *skb_put(struct sk_buff *skb, unsigned int len);
static inline void *__skb_put(struct sk_buff *skb, unsigned int len)
{
	void *tmp = skb_tail_pointer(skb);
	SKB_LINEAR_ASSERT(skb);
	skb->tail += len;
	skb->len  += len;
	return tmp;
}

static inline void *__skb_put_zero(struct sk_buff *skb, unsigned int len)
{
	void *tmp = __skb_put(skb, len);

	memset(tmp, 0, len);
	return tmp;
}

static inline void *__skb_put_data(struct sk_buff *skb, const void *data,
				   unsigned int len)
{
	void *tmp = __skb_put(skb, len);

	memcpy(tmp, data, len);
	return tmp;
}

static inline void __skb_put_u8(struct sk_buff *skb, u8 val)
{
	*(u8 *)__skb_put(skb, 1) = val;
}

static inline void *skb_put_zero(struct sk_buff *skb, unsigned int len)
{
	void *tmp = skb_put(skb, len);

	memset(tmp, 0, len);

	return tmp;
}

static inline void *skb_put_data(struct sk_buff *skb, const void *data,
				 unsigned int len)
{
	void *tmp = skb_put(skb, len);

	memcpy(tmp, data, len);

	return tmp;
}

static inline void skb_put_u8(struct sk_buff *skb, u8 val)
{
	*(u8 *)skb_put(skb, 1) = val;
}

void *skb_push(struct sk_buff *skb, unsigned int len);
static inline void *__skb_push(struct sk_buff *skb, unsigned int len)
{
	DEBUG_NET_WARN_ON_ONCE(len > INT_MAX);

	skb->data -= len;
	skb->len  += len;
	return skb->data;
}

void *skb_pull(struct sk_buff *skb, unsigned int len);
static inline void *__skb_pull(struct sk_buff *skb, unsigned int len)
{
	DEBUG_NET_WARN_ON_ONCE(len > INT_MAX);

	skb->len -= len;
	if (unlikely(skb->len < skb->data_len)) {
#if defined(CONFIG_DEBUG_NET)
		skb->len += len;
		pr_err("__skb_pull(len=%u)\n", len);
		skb_dump(KERN_ERR, skb, false);
#endif
		BUG();
	}
	return skb->data += len;
}

static inline void *skb_pull_inline(struct sk_buff *skb, unsigned int len)
{
	return unlikely(len > skb->len) ? NULL : __skb_pull(skb, len);
}

void *skb_pull_data(struct sk_buff *skb, size_t len);

void *__pskb_pull_tail(struct sk_buff *skb, int delta);

static inline enum skb_drop_reason
pskb_may_pull_reason(struct sk_buff *skb, unsigned int len)
{
	DEBUG_NET_WARN_ON_ONCE(len > INT_MAX);
	skb_might_realloc(skb);

	if (likely(len <= skb_headlen(skb)))
		return SKB_NOT_DROPPED_YET;

	if (unlikely(len > skb->len))
		return SKB_DROP_REASON_PKT_TOO_SMALL;

	if (unlikely(!__pskb_pull_tail(skb, len - skb_headlen(skb))))
		return SKB_DROP_REASON_NOMEM;

	return SKB_NOT_DROPPED_YET;
}

static inline bool pskb_may_pull(struct sk_buff *skb, unsigned int len)
{
	return pskb_may_pull_reason(skb, len) == SKB_NOT_DROPPED_YET;
}

static inline void *pskb_pull(struct sk_buff *skb, unsigned int len)
{
	if (!pskb_may_pull(skb, len))
		return NULL;

	skb->len -= len;
	return skb->data += len;
}

void skb_condense(struct sk_buff *skb);

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
	if (skb_is_nonlinear(skb))
		return 0;

	return skb->end - skb->tail - skb->reserved_tailroom;
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

/**
 *	skb_tailroom_reserve - adjust reserved_tailroom
 *	@skb: buffer to alter
 *	@mtu: maximum amount of headlen permitted
 *	@needed_tailroom: minimum amount of reserved_tailroom
 *
 *	Set reserved_tailroom so that headlen can be as large as possible but
 *	not larger than mtu and tailroom cannot be smaller than
 *	needed_tailroom.
 *	The required headroom should already have been reserved before using
 *	this function.
 */
static inline void skb_tailroom_reserve(struct sk_buff *skb, unsigned int mtu,
					unsigned int needed_tailroom)
{
	SKB_LINEAR_ASSERT(skb);
	if (mtu < skb_tailroom(skb) - needed_tailroom)
		/* use at most mtu */
		skb->reserved_tailroom = skb_tailroom(skb) - mtu;
	else
		/* use up to all available space */
		skb->reserved_tailroom = needed_tailroom;
}

#define ENCAP_TYPE_ETHER	0
#define ENCAP_TYPE_IPPROTO	1

static inline void skb_set_inner_protocol(struct sk_buff *skb,
					  __be16 protocol)
{
	skb->inner_protocol = protocol;
	skb->inner_protocol_type = ENCAP_TYPE_ETHER;
}

static inline void skb_set_inner_ipproto(struct sk_buff *skb,
					 __u8 ipproto)
{
	skb->inner_ipproto = ipproto;
	skb->inner_protocol_type = ENCAP_TYPE_IPPROTO;
}

static inline void skb_reset_inner_headers(struct sk_buff *skb)
{
	skb->inner_mac_header = skb->mac_header;
	skb->inner_network_header = skb->network_header;
	skb->inner_transport_header = skb->transport_header;
}

static inline int skb_mac_header_was_set(const struct sk_buff *skb)
{
	return skb->mac_header != (typeof(skb->mac_header))~0U;
}

static inline void skb_reset_mac_len(struct sk_buff *skb)
{
	if (!skb_mac_header_was_set(skb)) {
		DEBUG_NET_WARN_ON_ONCE(1);
		skb->mac_len = 0;
	} else {
		skb->mac_len = skb->network_header - skb->mac_header;
	}
}

static inline unsigned char *skb_inner_transport_header(const struct sk_buff
							*skb)
{
	return skb->head + skb->inner_transport_header;
}

static inline int skb_inner_transport_offset(const struct sk_buff *skb)
{
	return skb_inner_transport_header(skb) - skb->data;
}

static inline void skb_reset_inner_transport_header(struct sk_buff *skb)
{
	long offset = skb->data - skb->head;

	DEBUG_NET_WARN_ON_ONCE(offset != (typeof(skb->inner_transport_header))offset);
	skb->inner_transport_header = offset;
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
	long offset = skb->data - skb->head;

	DEBUG_NET_WARN_ON_ONCE(offset != (typeof(skb->inner_network_header))offset);
	skb->inner_network_header = offset;
}

static inline void skb_set_inner_network_header(struct sk_buff *skb,
						const int offset)
{
	skb_reset_inner_network_header(skb);
	skb->inner_network_header += offset;
}

static inline bool skb_inner_network_header_was_set(const struct sk_buff *skb)
{
	return skb->inner_network_header > 0;
}

static inline unsigned char *skb_inner_mac_header(const struct sk_buff *skb)
{
	return skb->head + skb->inner_mac_header;
}

static inline void skb_reset_inner_mac_header(struct sk_buff *skb)
{
	long offset = skb->data - skb->head;

	DEBUG_NET_WARN_ON_ONCE(offset != (typeof(skb->inner_mac_header))offset);
	skb->inner_mac_header = offset;
}

static inline void skb_set_inner_mac_header(struct sk_buff *skb,
					    const int offset)
{
	skb_reset_inner_mac_header(skb);
	skb->inner_mac_header += offset;
}
static inline bool skb_transport_header_was_set(const struct sk_buff *skb)
{
	return skb->transport_header != (typeof(skb->transport_header))~0U;
}

static inline unsigned char *skb_transport_header(const struct sk_buff *skb)
{
	DEBUG_NET_WARN_ON_ONCE(!skb_transport_header_was_set(skb));
	return skb->head + skb->transport_header;
}

static inline void skb_reset_transport_header(struct sk_buff *skb)
{
	long offset = skb->data - skb->head;

	DEBUG_NET_WARN_ON_ONCE(offset != (typeof(skb->transport_header))offset);
	skb->transport_header = offset;
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
	long offset = skb->data - skb->head;

	DEBUG_NET_WARN_ON_ONCE(offset != (typeof(skb->network_header))offset);
	skb->network_header = offset;
}

static inline void skb_set_network_header(struct sk_buff *skb, const int offset)
{
	skb_reset_network_header(skb);
	skb->network_header += offset;
}

static inline unsigned char *skb_mac_header(const struct sk_buff *skb)
{
	DEBUG_NET_WARN_ON_ONCE(!skb_mac_header_was_set(skb));
	return skb->head + skb->mac_header;
}

static inline int skb_mac_offset(const struct sk_buff *skb)
{
	return skb_mac_header(skb) - skb->data;
}

static inline u32 skb_mac_header_len(const struct sk_buff *skb)
{
	DEBUG_NET_WARN_ON_ONCE(!skb_mac_header_was_set(skb));
	return skb->network_header - skb->mac_header;
}

static inline void skb_unset_mac_header(struct sk_buff *skb)
{
	skb->mac_header = (typeof(skb->mac_header))~0U;
}

static inline void skb_reset_mac_header(struct sk_buff *skb)
{
	long offset = skb->data - skb->head;

	DEBUG_NET_WARN_ON_ONCE(offset != (typeof(skb->mac_header))offset);
	skb->mac_header = offset;
}

static inline void skb_set_mac_header(struct sk_buff *skb, const int offset)
{
	skb_reset_mac_header(skb);
	skb->mac_header += offset;
}

static inline void skb_pop_mac_header(struct sk_buff *skb)
{
	skb->mac_header = skb->network_header;
}

static inline void skb_probe_transport_header(struct sk_buff *skb)
{
	struct flow_keys_basic keys;

	if (skb_transport_header_was_set(skb))
		return;

	if (skb_flow_dissect_flow_keys_basic(NULL, skb, &keys,
					     NULL, 0, 0, 0, 0))
		skb_set_transport_header(skb, keys.control.thoff);
}

static inline void skb_mac_header_rebuild(struct sk_buff *skb)
{
	if (skb_mac_header_was_set(skb)) {
		const unsigned char *old_mac = skb_mac_header(skb);

		skb_set_mac_header(skb, -skb->mac_len);
		memmove(skb_mac_header(skb), old_mac, skb->mac_len);
	}
}

/* Move the full mac header up to current network_header.
 * Leaves skb->data pointing at offset skb->mac_len into the mac_header.
 * Must be provided the complete mac header length.
 */
static inline void skb_mac_header_rebuild_full(struct sk_buff *skb, u32 full_mac_len)
{
	if (skb_mac_header_was_set(skb)) {
		const unsigned char *old_mac = skb_mac_header(skb);

		skb_set_mac_header(skb, -full_mac_len);
		memmove(skb_mac_header(skb), old_mac, full_mac_len);
		__skb_push(skb, full_mac_len - skb->mac_len);
	}
}

static inline int skb_checksum_start_offset(const struct sk_buff *skb)
{
	return skb->csum_start - skb_headroom(skb);
}

static inline unsigned char *skb_checksum_start(const struct sk_buff *skb)
{
	return skb->head + skb->csum_start;
}

static inline int skb_transport_offset(const struct sk_buff *skb)
{
	return skb_transport_header(skb) - skb->data;
}

static inline u32 skb_network_header_len(const struct sk_buff *skb)
{
	DEBUG_NET_WARN_ON_ONCE(!skb_transport_header_was_set(skb));
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

static inline enum skb_drop_reason
pskb_network_may_pull_reason(struct sk_buff *skb, unsigned int len)
{
	return pskb_may_pull_reason(skb, skb_network_offset(skb) + len);
}

static inline int pskb_network_may_pull(struct sk_buff *skb, unsigned int len)
{
	return pskb_network_may_pull_reason(skb, len) == SKB_NOT_DROPPED_YET;
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
 * get_rps_cpu() for example only access one 64 bytes aligned block :
 * NET_IP_ALIGN(2) + ethernet_header(14) + IP_header(20/40) + ports(8)
 */
#ifndef NET_SKB_PAD
#define NET_SKB_PAD	max(32, L1_CACHE_BYTES)
#endif

int ___pskb_trim(struct sk_buff *skb, unsigned int len);

static inline void __skb_set_length(struct sk_buff *skb, unsigned int len)
{
	if (WARN_ON(skb_is_nonlinear(skb)))
		return;
	skb->len = len;
	skb_set_tail_pointer(skb, len);
}

static inline void __skb_trim(struct sk_buff *skb, unsigned int len)
{
	__skb_set_length(skb, len);
}

void skb_trim(struct sk_buff *skb, unsigned int len);

static inline int __pskb_trim(struct sk_buff *skb, unsigned int len)
{
	if (skb->data_len)
		return ___pskb_trim(skb, len);
	__skb_trim(skb, len);
	return 0;
}

static inline int pskb_trim(struct sk_buff *skb, unsigned int len)
{
	skb_might_realloc(skb);
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

static inline int __skb_grow(struct sk_buff *skb, unsigned int len)
{
	unsigned int diff = len - skb->len;

	if (skb_tailroom(skb) < diff) {
		int ret = pskb_expand_head(skb, 0, diff - skb_tailroom(skb),
					   GFP_ATOMIC);
		if (ret)
			return ret;
	}
	__skb_set_length(skb, len);
	return 0;
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
	if (skb->destructor) {
		skb->destructor(skb);
		skb->destructor = NULL;
		skb->sk		= NULL;
	} else {
		BUG_ON(skb->sk);
	}
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
	if (likely(!skb_zcopy(skb)))
		return 0;
	if (skb_shinfo(skb)->flags & SKBFL_DONT_ORPHAN)
		return 0;
	return skb_copy_ubufs(skb, gfp_mask);
}

/* Frags must be orphaned, even if refcounted, if skb might loop to rx path */
static inline int skb_orphan_frags_rx(struct sk_buff *skb, gfp_t gfp_mask)
{
	if (likely(!skb_zcopy(skb)))
		return 0;
	return skb_copy_ubufs(skb, gfp_mask);
}

/**
 *	__skb_queue_purge_reason - empty a list
 *	@list: list to empty
 *	@reason: drop reason
 *
 *	Delete all buffers on an &sk_buff list. Each buffer is removed from
 *	the list and one reference dropped. This function does not take the
 *	list lock and the caller must hold the relevant locks to use it.
 */
static inline void __skb_queue_purge_reason(struct sk_buff_head *list,
					    enum skb_drop_reason reason)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(list)) != NULL)
		kfree_skb_reason(skb, reason);
}

static inline void __skb_queue_purge(struct sk_buff_head *list)
{
	__skb_queue_purge_reason(list, SKB_DROP_REASON_QUEUE_PURGE);
}

void skb_queue_purge_reason(struct sk_buff_head *list,
			    enum skb_drop_reason reason);

static inline void skb_queue_purge(struct sk_buff_head *list)
{
	skb_queue_purge_reason(list, SKB_DROP_REASON_QUEUE_PURGE);
}

unsigned int skb_rbtree_purge(struct rb_root *root);
void skb_errqueue_purge(struct sk_buff_head *list);

void *__netdev_alloc_frag_align(unsigned int fragsz, unsigned int align_mask);

/**
 * netdev_alloc_frag - allocate a page fragment
 * @fragsz: fragment size
 *
 * Allocates a frag from a page for receive buffer.
 * Uses GFP_ATOMIC allocations.
 */
static inline void *netdev_alloc_frag(unsigned int fragsz)
{
	return __netdev_alloc_frag_align(fragsz, ~0u);
}

static inline void *netdev_alloc_frag_align(unsigned int fragsz,
					    unsigned int align)
{
	WARN_ON_ONCE(!is_power_of_2(align));
	return __netdev_alloc_frag_align(fragsz, -align);
}

struct sk_buff *__netdev_alloc_skb(struct net_device *dev, unsigned int length,
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

static inline void skb_free_frag(void *addr)
{
	page_frag_free(addr);
}

void *__napi_alloc_frag_align(unsigned int fragsz, unsigned int align_mask);

static inline void *napi_alloc_frag(unsigned int fragsz)
{
	return __napi_alloc_frag_align(fragsz, ~0u);
}

static inline void *napi_alloc_frag_align(unsigned int fragsz,
					  unsigned int align)
{
	WARN_ON_ONCE(!is_power_of_2(align));
	return __napi_alloc_frag_align(fragsz, -align);
}

struct sk_buff *napi_alloc_skb(struct napi_struct *napi, unsigned int length);
void napi_consume_skb(struct sk_buff *skb, int budget);

void napi_skb_free_stolen_head(struct sk_buff *skb);
void __napi_kfree_skb(struct sk_buff *skb, enum skb_drop_reason reason);

/**
 * __dev_alloc_pages - allocate page for network Rx
 * @gfp_mask: allocation priority. Set __GFP_NOMEMALLOC if not for network Rx
 * @order: size of the allocation
 *
 * Allocate a new page.
 *
 * %NULL is returned if there is no free memory.
*/
static inline struct page *__dev_alloc_pages_noprof(gfp_t gfp_mask,
					     unsigned int order)
{
	/* This piece of code contains several assumptions.
	 * 1.  This is for device Rx, therefore a cold page is preferred.
	 * 2.  The expectation is the user wants a compound page.
	 * 3.  If requesting a order 0 page it will not be compound
	 *     due to the check to see if order has a value in prep_new_page
	 * 4.  __GFP_MEMALLOC is ignored if __GFP_NOMEMALLOC is set due to
	 *     code in gfp_to_alloc_flags that should be enforcing this.
	 */
	gfp_mask |= __GFP_COMP | __GFP_MEMALLOC;

	return alloc_pages_node_noprof(NUMA_NO_NODE, gfp_mask, order);
}
#define __dev_alloc_pages(...)	alloc_hooks(__dev_alloc_pages_noprof(__VA_ARGS__))

/*
 * This specialized allocator has to be a macro for its allocations to be
 * accounted separately (to have a separate alloc_tag).
 */
#define dev_alloc_pages(_order) __dev_alloc_pages(GFP_ATOMIC | __GFP_NOWARN, _order)

/**
 * __dev_alloc_page - allocate a page for network Rx
 * @gfp_mask: allocation priority. Set __GFP_NOMEMALLOC if not for network Rx
 *
 * Allocate a new page.
 *
 * %NULL is returned if there is no free memory.
 */
static inline struct page *__dev_alloc_page_noprof(gfp_t gfp_mask)
{
	return __dev_alloc_pages_noprof(gfp_mask, 0);
}
#define __dev_alloc_page(...)	alloc_hooks(__dev_alloc_page_noprof(__VA_ARGS__))

/*
 * This specialized allocator has to be a macro for its allocations to be
 * accounted separately (to have a separate alloc_tag).
 */
#define dev_alloc_page()	dev_alloc_pages(0)

/**
 * dev_page_is_reusable - check whether a page can be reused for network Rx
 * @page: the page to test
 *
 * A page shouldn't be considered for reusing/recycling if it was allocated
 * under memory pressure or at a distant memory node.
 *
 * Returns false if this page should be returned to page allocator, true
 * otherwise.
 */
static inline bool dev_page_is_reusable(const struct page *page)
{
	return likely(page_to_nid(page) == numa_mem_id() &&
		      !page_is_pfmemalloc(page));
}

/**
 *	skb_propagate_pfmemalloc - Propagate pfmemalloc if skb is allocated after RX page
 *	@page: The page that was allocated from skb_alloc_page
 *	@skb: The skb that may need pfmemalloc set
 */
static inline void skb_propagate_pfmemalloc(const struct page *page,
					    struct sk_buff *skb)
{
	if (page_is_pfmemalloc(page))
		skb->pfmemalloc = true;
}

/**
 * skb_frag_off() - Returns the offset of a skb fragment
 * @frag: the paged fragment
 */
static inline unsigned int skb_frag_off(const skb_frag_t *frag)
{
	return frag->offset;
}

/**
 * skb_frag_off_add() - Increments the offset of a skb fragment by @delta
 * @frag: skb fragment
 * @delta: value to add
 */
static inline void skb_frag_off_add(skb_frag_t *frag, int delta)
{
	frag->offset += delta;
}

/**
 * skb_frag_off_set() - Sets the offset of a skb fragment
 * @frag: skb fragment
 * @offset: offset of fragment
 */
static inline void skb_frag_off_set(skb_frag_t *frag, unsigned int offset)
{
	frag->offset = offset;
}

/**
 * skb_frag_off_copy() - Sets the offset of a skb fragment from another fragment
 * @fragto: skb fragment where offset is set
 * @fragfrom: skb fragment offset is copied from
 */
static inline void skb_frag_off_copy(skb_frag_t *fragto,
				     const skb_frag_t *fragfrom)
{
	fragto->offset = fragfrom->offset;
}

/* Return: true if the skb_frag contains a net_iov. */
static inline bool skb_frag_is_net_iov(const skb_frag_t *frag)
{
	return netmem_is_net_iov(frag->netmem);
}

/**
 * skb_frag_net_iov - retrieve the net_iov referred to by fragment
 * @frag: the fragment
 *
 * Return: the &struct net_iov associated with @frag. Returns NULL if this
 * frag has no associated net_iov.
 */
static inline struct net_iov *skb_frag_net_iov(const skb_frag_t *frag)
{
	if (!skb_frag_is_net_iov(frag))
		return NULL;

	return netmem_to_net_iov(frag->netmem);
}

/**
 * skb_frag_page - retrieve the page referred to by a paged fragment
 * @frag: the paged fragment
 *
 * Return: the &struct page associated with @frag. Returns NULL if this frag
 * has no associated page.
 */
static inline struct page *skb_frag_page(const skb_frag_t *frag)
{
	if (skb_frag_is_net_iov(frag))
		return NULL;

	return netmem_to_page(frag->netmem);
}

/**
 * skb_frag_netmem - retrieve the netmem referred to by a fragment
 * @frag: the fragment
 *
 * Return: the &netmem_ref associated with @frag.
 */
static inline netmem_ref skb_frag_netmem(const skb_frag_t *frag)
{
	return frag->netmem;
}

int skb_pp_cow_data(struct page_pool *pool, struct sk_buff **pskb,
		    unsigned int headroom);
int skb_cow_data_for_xdp(struct page_pool *pool, struct sk_buff **pskb,
			 struct bpf_prog *prog);

/**
 * skb_frag_address - gets the address of the data contained in a paged fragment
 * @frag: the paged fragment buffer
 *
 * Returns the address of the data within @frag. The page must already
 * be mapped.
 */
static inline void *skb_frag_address(const skb_frag_t *frag)
{
	if (!skb_frag_page(frag))
		return NULL;

	return page_address(skb_frag_page(frag)) + skb_frag_off(frag);
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

	return ptr + skb_frag_off(frag);
}

/**
 * skb_frag_page_copy() - sets the page in a fragment from another fragment
 * @fragto: skb fragment where page is set
 * @fragfrom: skb fragment page is copied from
 */
static inline void skb_frag_page_copy(skb_frag_t *fragto,
				      const skb_frag_t *fragfrom)
{
	fragto->netmem = fragfrom->netmem;
}

bool skb_page_frag_refill(unsigned int sz, struct page_frag *pfrag, gfp_t prio);

/**
 * skb_frag_dma_map - maps a paged fragment via the DMA API
 * @dev: the device to map the fragment to
 * @frag: the paged fragment to map
 * @offset: the offset within the fragment (starting at the
 *          fragment's own offset)
 * @size: the number of bytes to map
 * @dir: the direction of the mapping (``PCI_DMA_*``)
 *
 * Maps the page associated with @frag to @device.
 */
static inline dma_addr_t skb_frag_dma_map(struct device *dev,
					  const skb_frag_t *frag,
					  size_t offset, size_t size,
					  enum dma_data_direction dir)
{
	return dma_map_page(dev, skb_frag_page(frag),
			    skb_frag_off(frag) + offset, size, dir);
}

static inline struct sk_buff *pskb_copy(struct sk_buff *skb,
					gfp_t gfp_mask)
{
	return __pskb_copy(skb, skb_headroom(skb), gfp_mask);
}


static inline struct sk_buff *pskb_copy_for_clone(struct sk_buff *skb,
						  gfp_t gfp_mask)
{
	return __pskb_copy_fclone(skb, skb_headroom(skb), gfp_mask, true);
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

static inline int skb_try_make_writable(struct sk_buff *skb,
					unsigned int write_len)
{
	return skb_cloned(skb) && !skb_clone_writable(skb, write_len) &&
	       pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
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

/**
 *	__skb_put_padto - increase size and pad an skbuff up to a minimal size
 *	@skb: buffer to pad
 *	@len: minimal length
 *	@free_on_error: free buffer on error
 *
 *	Pads up a buffer to ensure the trailing bytes exist and are
 *	blanked. If the buffer already contains sufficient data it
 *	is untouched. Otherwise it is extended. Returns zero on
 *	success. The skb is freed on error if @free_on_error is true.
 */
static inline int __must_check __skb_put_padto(struct sk_buff *skb,
					       unsigned int len,
					       bool free_on_error)
{
	unsigned int size = skb->len;

	if (unlikely(size < len)) {
		len -= size;
		if (__skb_pad(skb, len, free_on_error))
			return -ENOMEM;
		__skb_put(skb, len);
	}
	return 0;
}

/**
 *	skb_put_padto - increase size and pad an skbuff up to a minimal size
 *	@skb: buffer to pad
 *	@len: minimal length
 *
 *	Pads up a buffer to ensure the trailing bytes exist and are
 *	blanked. If the buffer already contains sufficient data it
 *	is untouched. Otherwise it is extended. Returns zero on
 *	success. The skb is freed on error.
 */
static inline int __must_check skb_put_padto(struct sk_buff *skb, unsigned int len)
{
	return __skb_put_padto(skb, len, true);
}

bool csum_and_copy_from_iter_full(void *addr, size_t bytes, __wsum *csum, struct iov_iter *i)
	__must_check;

static inline int skb_add_data(struct sk_buff *skb,
			       struct iov_iter *from, int copy)
{
	const int off = skb->len;

	if (skb->ip_summed == CHECKSUM_NONE) {
		__wsum csum = 0;
		if (csum_and_copy_from_iter_full(skb_put(skb, copy), copy,
					         &csum, from)) {
			skb->csum = csum_block_add(skb->csum, csum, off);
			return 0;
		}
	} else if (copy_from_iter_full(skb_put(skb, copy), copy, from))
		return 0;

	__skb_trim(skb, off);
	return -EFAULT;
}

static inline bool skb_can_coalesce(struct sk_buff *skb, int i,
				    const struct page *page, int off)
{
	if (skb_zcopy(skb))
		return false;
	if (i) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i - 1];

		return page == skb_frag_page(frag) &&
		       off == skb_frag_off(frag) + skb_frag_size(frag);
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
	return skb_is_nonlinear(skb) &&
	       skb_shinfo(skb)->flags & SKBFL_SHARED_FRAG;
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

static __always_inline void
__skb_postpull_rcsum(struct sk_buff *skb, const void *start, unsigned int len,
		     unsigned int off)
{
	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->csum = csum_block_sub(skb->csum,
					   csum_partial(start, len, 0), off);
	else if (skb->ip_summed == CHECKSUM_PARTIAL &&
		 skb_checksum_start_offset(skb) < 0)
		skb->ip_summed = CHECKSUM_NONE;
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
		skb->csum = wsum_negate(csum_partial(start, len,
						     wsum_negate(skb->csum)));
	else if (skb->ip_summed == CHECKSUM_PARTIAL &&
		 skb_checksum_start_offset(skb) < 0)
		skb->ip_summed = CHECKSUM_NONE;
}

static __always_inline void
__skb_postpush_rcsum(struct sk_buff *skb, const void *start, unsigned int len,
		     unsigned int off)
{
	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->csum = csum_block_add(skb->csum,
					   csum_partial(start, len, 0), off);
}

/**
 *	skb_postpush_rcsum - update checksum for received skb after push
 *	@skb: buffer to update
 *	@start: start of data after push
 *	@len: length of data pushed
 *
 *	After doing a push on a received packet, you need to call this to
 *	update the CHECKSUM_COMPLETE checksum.
 */
static inline void skb_postpush_rcsum(struct sk_buff *skb,
				      const void *start, unsigned int len)
{
	__skb_postpush_rcsum(skb, start, len, 0);
}

void *skb_pull_rcsum(struct sk_buff *skb, unsigned int len);

/**
 *	skb_push_rcsum - push skb and update receive checksum
 *	@skb: buffer to update
 *	@len: length of data pulled
 *
 *	This function performs an skb_push on the packet and updates
 *	the CHECKSUM_COMPLETE checksum.  It should be used on
 *	receive path processing instead of skb_push unless you know
 *	that the checksum difference is zero (e.g., a valid IP header)
 *	or you are setting ip_summed to CHECKSUM_NONE.
 */
static inline void *skb_push_rcsum(struct sk_buff *skb, unsigned int len)
{
	skb_push(skb, len);
	skb_postpush_rcsum(skb, skb->data, len);
	return skb->data;
}

int pskb_trim_rcsum_slow(struct sk_buff *skb, unsigned int len);
/**
 *	pskb_trim_rcsum - trim received skb and update checksum
 *	@skb: buffer to trim
 *	@len: new length
 *
 *	This is exactly the same as pskb_trim except that it ensures the
 *	checksum of received packets are still valid after the operation.
 *	It can change skb pointers.
 */

static inline int pskb_trim_rcsum(struct sk_buff *skb, unsigned int len)
{
	skb_might_realloc(skb);
	if (likely(len >= skb->len))
		return 0;
	return pskb_trim_rcsum_slow(skb, len);
}

static inline int __skb_trim_rcsum(struct sk_buff *skb, unsigned int len)
{
	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->ip_summed = CHECKSUM_NONE;
	__skb_trim(skb, len);
	return 0;
}

static inline int __skb_grow_rcsum(struct sk_buff *skb, unsigned int len)
{
	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->ip_summed = CHECKSUM_NONE;
	return __skb_grow(skb, len);
}

#define rb_to_skb(rb) rb_entry_safe(rb, struct sk_buff, rbnode)
#define skb_rb_first(root) rb_to_skb(rb_first(root))
#define skb_rb_last(root)  rb_to_skb(rb_last(root))
#define skb_rb_next(skb)   rb_to_skb(rb_next(&(skb)->rbnode))
#define skb_rb_prev(skb)   rb_to_skb(rb_prev(&(skb)->rbnode))

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

#define skb_rbtree_walk(skb, root)						\
		for (skb = skb_rb_first(root); skb != NULL;			\
		     skb = skb_rb_next(skb))

#define skb_rbtree_walk_from(skb)						\
		for (; skb != NULL;						\
		     skb = skb_rb_next(skb))

#define skb_rbtree_walk_from_safe(skb, tmp)					\
		for (; tmp = skb ? skb_rb_next(skb) : NULL, (skb != NULL);	\
		     skb = tmp)

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

#define skb_walk_frags(skb, iter)	\
	for (iter = skb_shinfo(skb)->frag_list; iter; iter = iter->next)


int __skb_wait_for_more_packets(struct sock *sk, struct sk_buff_head *queue,
				int *err, long *timeo_p,
				const struct sk_buff *skb);
struct sk_buff *__skb_try_recv_from_queue(struct sock *sk,
					  struct sk_buff_head *queue,
					  unsigned int flags,
					  int *off, int *err,
					  struct sk_buff **last);
struct sk_buff *__skb_try_recv_datagram(struct sock *sk,
					struct sk_buff_head *queue,
					unsigned int flags, int *off, int *err,
					struct sk_buff **last);
struct sk_buff *__skb_recv_datagram(struct sock *sk,
				    struct sk_buff_head *sk_queue,
				    unsigned int flags, int *off, int *err);
struct sk_buff *skb_recv_datagram(struct sock *sk, unsigned int flags, int *err);
__poll_t datagram_poll(struct file *file, struct socket *sock,
			   struct poll_table_struct *wait);
int skb_copy_datagram_iter(const struct sk_buff *from, int offset,
			   struct iov_iter *to, int size);
static inline int skb_copy_datagram_msg(const struct sk_buff *from, int offset,
					struct msghdr *msg, int size)
{
	return skb_copy_datagram_iter(from, offset, &msg->msg_iter, size);
}
int skb_copy_and_csum_datagram_msg(struct sk_buff *skb, int hlen,
				   struct msghdr *msg);
int skb_copy_and_hash_datagram_iter(const struct sk_buff *skb, int offset,
			   struct iov_iter *to, int len,
			   struct ahash_request *hash);
int skb_copy_datagram_from_iter(struct sk_buff *skb, int offset,
				 struct iov_iter *from, int len);
int zerocopy_sg_from_iter(struct sk_buff *skb, struct iov_iter *frm);
void skb_free_datagram(struct sock *sk, struct sk_buff *skb);
int skb_kill_datagram(struct sock *sk, struct sk_buff *skb, unsigned int flags);
int skb_copy_bits(const struct sk_buff *skb, int offset, void *to, int len);
int skb_store_bits(struct sk_buff *skb, int offset, const void *from, int len);
__wsum skb_copy_and_csum_bits(const struct sk_buff *skb, int offset, u8 *to,
			      int len);
int skb_splice_bits(struct sk_buff *skb, struct sock *sk, unsigned int offset,
		    struct pipe_inode_info *pipe, unsigned int len,
		    unsigned int flags);
int skb_send_sock_locked(struct sock *sk, struct sk_buff *skb, int offset,
			 int len);
int skb_send_sock(struct sock *sk, struct sk_buff *skb, int offset, int len);
void skb_copy_and_csum_dev(const struct sk_buff *skb, u8 *to);
unsigned int skb_zerocopy_headlen(const struct sk_buff *from);
int skb_zerocopy(struct sk_buff *to, struct sk_buff *from,
		 int len, int hlen);
void skb_split(struct sk_buff *skb, struct sk_buff *skb1, const u32 len);
int skb_shift(struct sk_buff *tgt, struct sk_buff *skb, int shiftlen);
void skb_scrub_packet(struct sk_buff *skb, bool xnet);
struct sk_buff *skb_segment(struct sk_buff *skb, netdev_features_t features);
struct sk_buff *skb_segment_list(struct sk_buff *skb, netdev_features_t features,
				 unsigned int offset);
struct sk_buff *skb_vlan_untag(struct sk_buff *skb);
int skb_ensure_writable(struct sk_buff *skb, unsigned int write_len);
int skb_ensure_writable_head_tail(struct sk_buff *skb, struct net_device *dev);
int __skb_vlan_pop(struct sk_buff *skb, u16 *vlan_tci);
int skb_vlan_pop(struct sk_buff *skb);
int skb_vlan_push(struct sk_buff *skb, __be16 vlan_proto, u16 vlan_tci);
int skb_eth_pop(struct sk_buff *skb);
int skb_eth_push(struct sk_buff *skb, const unsigned char *dst,
		 const unsigned char *src);
int skb_mpls_push(struct sk_buff *skb, __be32 mpls_lse, __be16 mpls_proto,
		  int mac_len, bool ethernet);
int skb_mpls_pop(struct sk_buff *skb, __be16 next_proto, int mac_len,
		 bool ethernet);
int skb_mpls_update_lse(struct sk_buff *skb, __be32 mpls_lse);
int skb_mpls_dec_ttl(struct sk_buff *skb);
struct sk_buff *pskb_extract(struct sk_buff *skb, int off, int to_copy,
			     gfp_t gfp);

static inline int memcpy_from_msg(void *data, struct msghdr *msg, int len)
{
	return copy_from_iter_full(data, len, &msg->msg_iter) ? 0 : -EFAULT;
}

static inline int memcpy_to_msg(struct msghdr *msg, void *data, int len)
{
	return copy_to_iter(data, len, &msg->msg_iter) == len ? 0 : -EFAULT;
}

struct skb_checksum_ops {
	__wsum (*update)(const void *mem, int len, __wsum wsum);
	__wsum (*combine)(__wsum csum, __wsum csum2, int offset, int len);
};

extern const struct skb_checksum_ops *crc32c_csum_stub __read_mostly;

__wsum __skb_checksum(const struct sk_buff *skb, int offset, int len,
		      __wsum csum, const struct skb_checksum_ops *ops);
__wsum skb_checksum(const struct sk_buff *skb, int offset, int len,
		    __wsum csum);

static inline void * __must_check
__skb_header_pointer(const struct sk_buff *skb, int offset, int len,
		     const void *data, int hlen, void *buffer)
{
	if (likely(hlen - offset >= len))
		return (void *)data + offset;

	if (!skb || unlikely(skb_copy_bits(skb, offset, buffer, len) < 0))
		return NULL;

	return buffer;
}

static inline void * __must_check
skb_header_pointer(const struct sk_buff *skb, int offset, int len, void *buffer)
{
	return __skb_header_pointer(skb, offset, len, skb->data,
				    skb_headlen(skb), buffer);
}

static inline void * __must_check
skb_pointer_if_linear(const struct sk_buff *skb, int offset, int len)
{
	if (likely(skb_headlen(skb) - offset >= len))
		return skb->data + offset;
	return NULL;
}

/**
 *	skb_needs_linearize - check if we need to linearize a given skb
 *			      depending on the given device features.
 *	@skb: socket buffer to check
 *	@features: net device features
 *
 *	Returns true if either:
 *	1. skb has frag_list and the device doesn't support FRAGLIST, or
 *	2. skb is fragmented and the device does not support SG.
 */
static inline bool skb_needs_linearize(struct sk_buff *skb,
				       netdev_features_t features)
{
	return skb_is_nonlinear(skb) &&
	       ((skb_has_frag_list(skb) && !(features & NETIF_F_FRAGLIST)) ||
		(skb_shinfo(skb)->nr_frags && !(features & NETIF_F_SG)));
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

void skb_init(void);

static inline ktime_t skb_get_ktime(const struct sk_buff *skb)
{
	return skb->tstamp;
}

/**
 *	skb_get_timestamp - get timestamp from a skb
 *	@skb: skb to get stamp from
 *	@stamp: pointer to struct __kernel_old_timeval to store stamp in
 *
 *	Timestamps are stored in the skb as offsets to a base timestamp.
 *	This function converts the offset back to a struct timeval and stores
 *	it in stamp.
 */
static inline void skb_get_timestamp(const struct sk_buff *skb,
				     struct __kernel_old_timeval *stamp)
{
	*stamp = ns_to_kernel_old_timeval(skb->tstamp);
}

static inline void skb_get_new_timestamp(const struct sk_buff *skb,
					 struct __kernel_sock_timeval *stamp)
{
	struct timespec64 ts = ktime_to_timespec64(skb->tstamp);

	stamp->tv_sec = ts.tv_sec;
	stamp->tv_usec = ts.tv_nsec / 1000;
}

static inline void skb_get_timestampns(const struct sk_buff *skb,
				       struct __kernel_old_timespec *stamp)
{
	struct timespec64 ts = ktime_to_timespec64(skb->tstamp);

	stamp->tv_sec = ts.tv_sec;
	stamp->tv_nsec = ts.tv_nsec;
}

static inline void skb_get_new_timestampns(const struct sk_buff *skb,
					   struct __kernel_timespec *stamp)
{
	struct timespec64 ts = ktime_to_timespec64(skb->tstamp);

	stamp->tv_sec = ts.tv_sec;
	stamp->tv_nsec = ts.tv_nsec;
}

static inline void __net_timestamp(struct sk_buff *skb)
{
	skb->tstamp = ktime_get_real();
	skb->tstamp_type = SKB_CLOCK_REALTIME;
}

static inline ktime_t net_timedelta(ktime_t t)
{
	return ktime_sub(ktime_get_real(), t);
}

static inline void skb_set_delivery_time(struct sk_buff *skb, ktime_t kt,
					 u8 tstamp_type)
{
	skb->tstamp = kt;

	if (kt)
		skb->tstamp_type = tstamp_type;
	else
		skb->tstamp_type = SKB_CLOCK_REALTIME;
}

static inline void skb_set_delivery_type_by_clockid(struct sk_buff *skb,
						    ktime_t kt, clockid_t clockid)
{
	u8 tstamp_type = SKB_CLOCK_REALTIME;

	switch (clockid) {
	case CLOCK_REALTIME:
		break;
	case CLOCK_MONOTONIC:
		tstamp_type = SKB_CLOCK_MONOTONIC;
		break;
	case CLOCK_TAI:
		tstamp_type = SKB_CLOCK_TAI;
		break;
	default:
		WARN_ON_ONCE(1);
		kt = 0;
	}

	skb_set_delivery_time(skb, kt, tstamp_type);
}

DECLARE_STATIC_KEY_FALSE(netstamp_needed_key);

/* It is used in the ingress path to clear the delivery_time.
 * If needed, set the skb->tstamp to the (rcv) timestamp.
 */
static inline void skb_clear_delivery_time(struct sk_buff *skb)
{
	if (skb->tstamp_type) {
		skb->tstamp_type = SKB_CLOCK_REALTIME;
		if (static_branch_unlikely(&netstamp_needed_key))
			skb->tstamp = ktime_get_real();
		else
			skb->tstamp = 0;
	}
}

static inline void skb_clear_tstamp(struct sk_buff *skb)
{
	if (skb->tstamp_type)
		return;

	skb->tstamp = 0;
}

static inline ktime_t skb_tstamp(const struct sk_buff *skb)
{
	if (skb->tstamp_type)
		return 0;

	return skb->tstamp;
}

static inline ktime_t skb_tstamp_cond(const struct sk_buff *skb, bool cond)
{
	if (skb->tstamp_type != SKB_CLOCK_MONOTONIC && skb->tstamp)
		return skb->tstamp;

	if (static_branch_unlikely(&netstamp_needed_key) || cond)
		return ktime_get_real();

	return 0;
}

static inline u8 skb_metadata_len(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->meta_len;
}

static inline void *skb_metadata_end(const struct sk_buff *skb)
{
	return skb_mac_header(skb);
}

static inline bool __skb_metadata_differs(const struct sk_buff *skb_a,
					  const struct sk_buff *skb_b,
					  u8 meta_len)
{
	const void *a = skb_metadata_end(skb_a);
	const void *b = skb_metadata_end(skb_b);
	u64 diffs = 0;

	if (!IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) ||
	    BITS_PER_LONG != 64)
		goto slow;

	/* Using more efficient variant than plain call to memcmp(). */
	switch (meta_len) {
#define __it(x, op) (x -= sizeof(u##op))
#define __it_diff(a, b, op) (*(u##op *)__it(a, op)) ^ (*(u##op *)__it(b, op))
	case 32: diffs |= __it_diff(a, b, 64);
		fallthrough;
	case 24: diffs |= __it_diff(a, b, 64);
		fallthrough;
	case 16: diffs |= __it_diff(a, b, 64);
		fallthrough;
	case  8: diffs |= __it_diff(a, b, 64);
		break;
	case 28: diffs |= __it_diff(a, b, 64);
		fallthrough;
	case 20: diffs |= __it_diff(a, b, 64);
		fallthrough;
	case 12: diffs |= __it_diff(a, b, 64);
		fallthrough;
	case  4: diffs |= __it_diff(a, b, 32);
		break;
	default:
slow:
		return memcmp(a - meta_len, b - meta_len, meta_len);
	}
	return diffs;
}

static inline bool skb_metadata_differs(const struct sk_buff *skb_a,
					const struct sk_buff *skb_b)
{
	u8 len_a = skb_metadata_len(skb_a);
	u8 len_b = skb_metadata_len(skb_b);

	if (!(len_a | len_b))
		return false;

	return len_a != len_b ?
	       true : __skb_metadata_differs(skb_a, skb_b, len_a);
}

static inline void skb_metadata_set(struct sk_buff *skb, u8 meta_len)
{
	skb_shinfo(skb)->meta_len = meta_len;
}

static inline void skb_metadata_clear(struct sk_buff *skb)
{
	skb_metadata_set(skb, 0);
}

struct sk_buff *skb_clone_sk(struct sk_buff *skb);

#ifdef CONFIG_NETWORK_PHY_TIMESTAMPING

void skb_clone_tx_timestamp(struct sk_buff *skb);
bool skb_defer_rx_timestamp(struct sk_buff *skb);

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
 * must call this function to return the skb back to the stack with a
 * timestamp.
 *
 * @skb: clone of the original outgoing packet
 * @hwtstamps: hardware time stamps
 *
 */
void skb_complete_tx_timestamp(struct sk_buff *skb,
			       struct skb_shared_hwtstamps *hwtstamps);

void __skb_tstamp_tx(struct sk_buff *orig_skb, const struct sk_buff *ack_skb,
		     struct skb_shared_hwtstamps *hwtstamps,
		     struct sock *sk, int tstype);

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
void skb_tstamp_tx(struct sk_buff *orig_skb,
		   struct skb_shared_hwtstamps *hwtstamps);

/**
 * skb_tx_timestamp() - Driver hook for transmit timestamping
 *
 * Ethernet MAC Drivers should call this function in their hard_xmit()
 * function immediately before giving the sk_buff to the MAC hardware.
 *
 * Specifically, one should make absolutely sure that this function is
 * called before TX completion of this packet can trigger.  Otherwise
 * the packet could potentially already be freed.
 *
 * @skb: A socket buffer.
 */
static inline void skb_tx_timestamp(struct sk_buff *skb)
{
	skb_clone_tx_timestamp(skb);
	if (skb_shinfo(skb)->tx_flags & SKBTX_SW_TSTAMP)
		skb_tstamp_tx(skb, NULL);
}

/**
 * skb_complete_wifi_ack - deliver skb with wifi status
 *
 * @skb: the original outgoing packet
 * @acked: ack status
 *
 */
void skb_complete_wifi_ack(struct sk_buff *skb, bool acked);

__sum16 __skb_checksum_complete_head(struct sk_buff *skb, int len);
__sum16 __skb_checksum_complete(struct sk_buff *skb);

static inline int skb_csum_unnecessary(const struct sk_buff *skb)
{
	return ((skb->ip_summed == CHECKSUM_UNNECESSARY) ||
		skb->csum_valid ||
		(skb->ip_summed == CHECKSUM_PARTIAL &&
		 skb_checksum_start_offset(skb) >= 0));
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

static inline void __skb_decr_checksum_unnecessary(struct sk_buff *skb)
{
	if (skb->ip_summed == CHECKSUM_UNNECESSARY) {
		if (skb->csum_level == 0)
			skb->ip_summed = CHECKSUM_NONE;
		else
			skb->csum_level--;
	}
}

static inline void __skb_incr_checksum_unnecessary(struct sk_buff *skb)
{
	if (skb->ip_summed == CHECKSUM_UNNECESSARY) {
		if (skb->csum_level < SKB_MAX_CSUM_LEVEL)
			skb->csum_level++;
	} else if (skb->ip_summed == CHECKSUM_NONE) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		skb->csum_level = 0;
	}
}

static inline void __skb_reset_checksum_unnecessary(struct sk_buff *skb)
{
	if (skb->ip_summed == CHECKSUM_UNNECESSARY) {
		skb->ip_summed = CHECKSUM_NONE;
		skb->csum_level = 0;
	}
}

/* Check if we need to perform checksum complete validation.
 *
 * Returns true if checksum complete is needed, false otherwise
 * (either checksum is unnecessary or zero checksum is allowed).
 */
static inline bool __skb_checksum_validate_needed(struct sk_buff *skb,
						  bool zero_okay,
						  __sum16 check)
{
	if (skb_csum_unnecessary(skb) || (zero_okay && !check)) {
		skb->csum_valid = 1;
		__skb_decr_checksum_unnecessary(skb);
		return false;
	}

	return true;
}

/* For small packets <= CHECKSUM_BREAK perform checksum complete directly
 * in checksum_init.
 */
#define CHECKSUM_BREAK 76

/* Unset checksum-complete
 *
 * Unset checksum complete can be done when packet is being modified
 * (uncompressed for instance) and checksum-complete value is
 * invalidated.
 */
static inline void skb_checksum_complete_unset(struct sk_buff *skb)
{
	if (skb->ip_summed == CHECKSUM_COMPLETE)
		skb->ip_summed = CHECKSUM_NONE;
}

/* Validate (init) checksum based on checksum complete.
 *
 * Return values:
 *   0: checksum is validated or try to in skb_checksum_complete. In the latter
 *	case the ip_summed will not be CHECKSUM_UNNECESSARY and the pseudo
 *	checksum is stored in skb->csum for use in __skb_checksum_complete
 *   non-zero: value of invalid checksum
 *
 */
static inline __sum16 __skb_checksum_validate_complete(struct sk_buff *skb,
						       bool complete,
						       __wsum psum)
{
	if (skb->ip_summed == CHECKSUM_COMPLETE) {
		if (!csum_fold(csum_add(psum, skb->csum))) {
			skb->csum_valid = 1;
			return 0;
		}
	}

	skb->csum = psum;

	if (complete || skb->len <= CHECKSUM_BREAK) {
		__sum16 csum;

		csum = __skb_checksum_complete(skb);
		skb->csum_valid = !csum;
		return csum;
	}

	return 0;
}

static inline __wsum null_compute_pseudo(struct sk_buff *skb, int proto)
{
	return 0;
}

/* Perform checksum validate (init). Note that this is a macro since we only
 * want to calculate the pseudo header which is an input function if necessary.
 * First we try to validate without any computation (checksum unnecessary) and
 * then calculate based on checksum complete calling the function to compute
 * pseudo header.
 *
 * Return values:
 *   0: checksum is validated or try to in skb_checksum_complete
 *   non-zero: value of invalid checksum
 */
#define __skb_checksum_validate(skb, proto, complete,			\
				zero_okay, check, compute_pseudo)	\
({									\
	__sum16 __ret = 0;						\
	skb->csum_valid = 0;						\
	if (__skb_checksum_validate_needed(skb, zero_okay, check))	\
		__ret = __skb_checksum_validate_complete(skb,		\
				complete, compute_pseudo(skb, proto));	\
	__ret;								\
})

#define skb_checksum_init(skb, proto, compute_pseudo)			\
	__skb_checksum_validate(skb, proto, false, false, 0, compute_pseudo)

#define skb_checksum_init_zero_check(skb, proto, check, compute_pseudo)	\
	__skb_checksum_validate(skb, proto, false, true, check, compute_pseudo)

#define skb_checksum_validate(skb, proto, compute_pseudo)		\
	__skb_checksum_validate(skb, proto, true, false, 0, compute_pseudo)

#define skb_checksum_validate_zero_check(skb, proto, check,		\
					 compute_pseudo)		\
	__skb_checksum_validate(skb, proto, true, true, check, compute_pseudo)

#define skb_checksum_simple_validate(skb)				\
	__skb_checksum_validate(skb, 0, true, false, 0, null_compute_pseudo)

static inline bool __skb_checksum_convert_check(struct sk_buff *skb)
{
	return (skb->ip_summed == CHECKSUM_NONE && skb->csum_valid);
}

static inline void __skb_checksum_convert(struct sk_buff *skb, __wsum pseudo)
{
	skb->csum = ~pseudo;
	skb->ip_summed = CHECKSUM_COMPLETE;
}

#define skb_checksum_try_convert(skb, proto, compute_pseudo)	\
do {									\
	if (__skb_checksum_convert_check(skb))				\
		__skb_checksum_convert(skb, compute_pseudo(skb, proto)); \
} while (0)

static inline void skb_remcsum_adjust_partial(struct sk_buff *skb, void *ptr,
					      u16 start, u16 offset)
{
	skb->ip_summed = CHECKSUM_PARTIAL;
	skb->csum_start = ((unsigned char *)ptr + start) - skb->head;
	skb->csum_offset = offset - start;
}

/* Update skbuf and packet to reflect the remote checksum offload operation.
 * When called, ptr indicates the starting point for skb->csum when
 * ip_summed is CHECKSUM_COMPLETE. If we need create checksum complete
 * here, skb_postpull_rcsum is done so skb->csum start is ptr.
 */
static inline void skb_remcsum_process(struct sk_buff *skb, void *ptr,
				       int start, int offset, bool nopartial)
{
	__wsum delta;

	if (!nopartial) {
		skb_remcsum_adjust_partial(skb, ptr, start, offset);
		return;
	}

	if (unlikely(skb->ip_summed != CHECKSUM_COMPLETE)) {
		__skb_checksum_complete(skb);
		skb_postpull_rcsum(skb, skb->data, ptr - (void *)skb->data);
	}

	delta = remcsum_adjust(ptr, skb->csum, start, offset);

	/* Adjust skb->csum since we changed the packet */
	skb->csum = csum_add(skb->csum, delta);
}

static inline struct nf_conntrack *skb_nfct(const struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	return (void *)(skb->_nfct & NFCT_PTRMASK);
#else
	return NULL;
#endif
}

static inline unsigned long skb_get_nfct(const struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	return skb->_nfct;
#else
	return 0UL;
#endif
}

static inline void skb_set_nfct(struct sk_buff *skb, unsigned long nfct)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK)
	skb->slow_gro |= !!nfct;
	skb->_nfct = nfct;
#endif
}

#ifdef CONFIG_SKB_EXTENSIONS
enum skb_ext_id {
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	SKB_EXT_BRIDGE_NF,
#endif
#ifdef CONFIG_XFRM
	SKB_EXT_SEC_PATH,
#endif
#if IS_ENABLED(CONFIG_NET_TC_SKB_EXT)
	TC_SKB_EXT,
#endif
#if IS_ENABLED(CONFIG_MPTCP)
	SKB_EXT_MPTCP,
#endif
#if IS_ENABLED(CONFIG_MCTP_FLOWS)
	SKB_EXT_MCTP,
#endif
	SKB_EXT_NUM, /* must be last */
};

/**
 *	struct skb_ext - sk_buff extensions
 *	@refcnt: 1 on allocation, deallocated on 0
 *	@offset: offset to add to @data to obtain extension address
 *	@chunks: size currently allocated, stored in SKB_EXT_ALIGN_SHIFT units
 *	@data: start of extension data, variable sized
 *
 *	Note: offsets/lengths are stored in chunks of 8 bytes, this allows
 *	to use 'u8' types while allowing up to 2kb worth of extension data.
 */
struct skb_ext {
	refcount_t refcnt;
	u8 offset[SKB_EXT_NUM]; /* in chunks of 8 bytes */
	u8 chunks;		/* same */
	char data[] __aligned(8);
};

struct skb_ext *__skb_ext_alloc(gfp_t flags);
void *__skb_ext_set(struct sk_buff *skb, enum skb_ext_id id,
		    struct skb_ext *ext);
void *skb_ext_add(struct sk_buff *skb, enum skb_ext_id id);
void __skb_ext_del(struct sk_buff *skb, enum skb_ext_id id);
void __skb_ext_put(struct skb_ext *ext);

static inline void skb_ext_put(struct sk_buff *skb)
{
	if (skb->active_extensions)
		__skb_ext_put(skb->extensions);
}

static inline void __skb_ext_copy(struct sk_buff *dst,
				  const struct sk_buff *src)
{
	dst->active_extensions = src->active_extensions;

	if (src->active_extensions) {
		struct skb_ext *ext = src->extensions;

		refcount_inc(&ext->refcnt);
		dst->extensions = ext;
	}
}

static inline void skb_ext_copy(struct sk_buff *dst, const struct sk_buff *src)
{
	skb_ext_put(dst);
	__skb_ext_copy(dst, src);
}

static inline bool __skb_ext_exist(const struct skb_ext *ext, enum skb_ext_id i)
{
	return !!ext->offset[i];
}

static inline bool skb_ext_exist(const struct sk_buff *skb, enum skb_ext_id id)
{
	return skb->active_extensions & (1 << id);
}

static inline void skb_ext_del(struct sk_buff *skb, enum skb_ext_id id)
{
	if (skb_ext_exist(skb, id))
		__skb_ext_del(skb, id);
}

static inline void *skb_ext_find(const struct sk_buff *skb, enum skb_ext_id id)
{
	if (skb_ext_exist(skb, id)) {
		struct skb_ext *ext = skb->extensions;

		return (void *)ext + (ext->offset[id] << 3);
	}

	return NULL;
}

static inline void skb_ext_reset(struct sk_buff *skb)
{
	if (unlikely(skb->active_extensions)) {
		__skb_ext_put(skb->extensions);
		skb->active_extensions = 0;
	}
}

static inline bool skb_has_extensions(struct sk_buff *skb)
{
	return unlikely(skb->active_extensions);
}
#else
static inline void skb_ext_put(struct sk_buff *skb) {}
static inline void skb_ext_reset(struct sk_buff *skb) {}
static inline void skb_ext_del(struct sk_buff *skb, int unused) {}
static inline void __skb_ext_copy(struct sk_buff *d, const struct sk_buff *s) {}
static inline void skb_ext_copy(struct sk_buff *dst, const struct sk_buff *s) {}
static inline bool skb_has_extensions(struct sk_buff *skb) { return false; }
#endif /* CONFIG_SKB_EXTENSIONS */

static inline void nf_reset_ct(struct sk_buff *skb)
{
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
	nf_conntrack_put(skb_nfct(skb));
	skb->_nfct = 0;
#endif
}

static inline void nf_reset_trace(struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_NETFILTER_XT_TARGET_TRACE) || IS_ENABLED(CONFIG_NF_TABLES)
	skb->nf_trace = 0;
#endif
}

static inline void ipvs_reset(struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_IP_VS)
	skb->ipvs_property = 0;
#endif
}

/* Note: This doesn't put any conntrack info in dst. */
static inline void __nf_copy(struct sk_buff *dst, const struct sk_buff *src,
			     bool copy)
{
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
	dst->_nfct = src->_nfct;
	nf_conntrack_get(skb_nfct(src));
#endif
#if IS_ENABLED(CONFIG_NETFILTER_XT_TARGET_TRACE) || IS_ENABLED(CONFIG_NF_TABLES)
	if (copy)
		dst->nf_trace = src->nf_trace;
#endif
}

static inline void nf_copy(struct sk_buff *dst, const struct sk_buff *src)
{
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
	nf_conntrack_put(skb_nfct(dst));
#endif
	dst->slow_gro = src->slow_gro;
	__nf_copy(dst, src, true);
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

static inline int secpath_exists(const struct sk_buff *skb)
{
#ifdef CONFIG_XFRM
	return skb_ext_exist(skb, SKB_EXT_SEC_PATH);
#else
	return 0;
#endif
}

static inline bool skb_irq_freeable(const struct sk_buff *skb)
{
	return !skb->destructor &&
		!secpath_exists(skb) &&
		!skb_nfct(skb) &&
		!skb->_skb_refdst &&
		!skb_has_frag_list(skb);
}

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

static inline void skb_set_dst_pending_confirm(struct sk_buff *skb, u32 val)
{
	skb->dst_pending_confirm = val;
}

static inline bool skb_get_dst_pending_confirm(const struct sk_buff *skb)
{
	return skb->dst_pending_confirm != 0;
}

static inline struct sec_path *skb_sec_path(const struct sk_buff *skb)
{
#ifdef CONFIG_XFRM
	return skb_ext_find(skb, SKB_EXT_SEC_PATH);
#else
	return NULL;
#endif
}

static inline bool skb_is_gso(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->gso_size;
}

/* Note: Should be called only if skb_is_gso(skb) is true */
static inline bool skb_is_gso_v6(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6;
}

/* Note: Should be called only if skb_is_gso(skb) is true */
static inline bool skb_is_gso_sctp(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->gso_type & SKB_GSO_SCTP;
}

/* Note: Should be called only if skb_is_gso(skb) is true */
static inline bool skb_is_gso_tcp(const struct sk_buff *skb)
{
	return skb_shinfo(skb)->gso_type & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6);
}

static inline void skb_gso_reset(struct sk_buff *skb)
{
	skb_shinfo(skb)->gso_size = 0;
	skb_shinfo(skb)->gso_segs = 0;
	skb_shinfo(skb)->gso_type = 0;
}

static inline void skb_increase_gso_size(struct skb_shared_info *shinfo,
					 u16 increment)
{
	if (WARN_ON_ONCE(shinfo->gso_size == GSO_BY_FRAGS))
		return;
	shinfo->gso_size += increment;
}

static inline void skb_decrease_gso_size(struct skb_shared_info *shinfo,
					 u16 decrement)
{
	if (WARN_ON_ONCE(shinfo->gso_size == GSO_BY_FRAGS))
		return;
	shinfo->gso_size -= decrement;
}

void __skb_warn_lro_forwarding(const struct sk_buff *skb);

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
	DEBUG_NET_WARN_ON_ONCE(skb->ip_summed != CHECKSUM_NONE);
}

bool skb_partial_csum_set(struct sk_buff *skb, u16 start, u16 off);

int skb_checksum_setup(struct sk_buff *skb, bool recalculate);
struct sk_buff *skb_checksum_trimmed(struct sk_buff *skb,
				     unsigned int transport_len,
				     __sum16(*skb_chkf)(struct sk_buff *skb));

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

/* Local Checksum Offload.
 * Compute outer checksum based on the assumption that the
 * inner checksum will be offloaded later.
 * See Documentation/networking/checksum-offloads.rst for
 * explanation of how this works.
 * Fill in outer checksum adjustment (e.g. with sum of outer
 * pseudo-header) before calling.
 * Also ensure that inner checksum is in linear data area.
 */
static inline __wsum lco_csum(struct sk_buff *skb)
{
	unsigned char *csum_start = skb_checksum_start(skb);
	unsigned char *l4_hdr = skb_transport_header(skb);
	__wsum partial;

	/* Start with complement of inner checksum adjustment */
	partial = ~csum_unfold(*(__force __sum16 *)(csum_start +
						    skb->csum_offset));

	/* Add in checksum of our headers (incl. outer checksum
	 * adjustment filled in by caller) and return result.
	 */
	return csum_partial(l4_hdr, csum_start - l4_hdr, partial);
}

static inline bool skb_is_redirected(const struct sk_buff *skb)
{
	return skb->redirected;
}

static inline void skb_set_redirected(struct sk_buff *skb, bool from_ingress)
{
	skb->redirected = 1;
#ifdef CONFIG_NET_REDIRECT
	skb->from_ingress = from_ingress;
	if (skb->from_ingress)
		skb_clear_tstamp(skb);
#endif
}

static inline void skb_reset_redirect(struct sk_buff *skb)
{
	skb->redirected = 0;
}

static inline void skb_set_redirected_noclear(struct sk_buff *skb,
					      bool from_ingress)
{
	skb->redirected = 1;
#ifdef CONFIG_NET_REDIRECT
	skb->from_ingress = from_ingress;
#endif
}

static inline bool skb_csum_is_sctp(struct sk_buff *skb)
{
#if IS_ENABLED(CONFIG_IP_SCTP)
	return skb->csum_not_inet;
#else
	return 0;
#endif
}

static inline void skb_reset_csum_not_inet(struct sk_buff *skb)
{
	skb->ip_summed = CHECKSUM_NONE;
#if IS_ENABLED(CONFIG_IP_SCTP)
	skb->csum_not_inet = 0;
#endif
}

static inline void skb_set_kcov_handle(struct sk_buff *skb,
				       const u64 kcov_handle)
{
#ifdef CONFIG_KCOV
	skb->kcov_handle = kcov_handle;
#endif
}

static inline u64 skb_get_kcov_handle(struct sk_buff *skb)
{
#ifdef CONFIG_KCOV
	return skb->kcov_handle;
#else
	return 0;
#endif
}

static inline void skb_mark_for_recycle(struct sk_buff *skb)
{
#ifdef CONFIG_PAGE_POOL
	skb->pp_recycle = 1;
#endif
}

ssize_t skb_splice_from_iter(struct sk_buff *skb, struct iov_iter *iter,
			     ssize_t maxsize, gfp_t gfp);

#endif	/* __KERNEL__ */
#endif	/* _LINUX_SKBUFF_H */
