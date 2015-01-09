#ifndef __6LOWPAN_NHC_H
#define __6LOWPAN_NHC_H

#include <linux/skbuff.h>
#include <linux/rbtree.h>
#include <linux/module.h>

#include <net/6lowpan.h>
#include <net/ipv6.h>

#define LOWPAN_NHC_MAX_ID_LEN	1

/**
 * LOWPAN_NHC - helper macro to generate nh id fields and lowpan_nhc struct
 *
 * @__nhc: variable name of the lowpan_nhc struct.
 * @_name: const char * of common header compression name.
 * @_nexthdr: ipv6 nexthdr field for the header compression.
 * @_nexthdrlen: ipv6 nexthdr len for the reserved space.
 * @_idsetup: callback to setup id and mask values.
 * @_idlen: len for the next header id and mask, should be always the same.
 * @_uncompress: callback for uncompression call.
 * @_compress: callback for compression call.
 */
#define LOWPAN_NHC(__nhc, _name, _nexthdr,	\
		   _hdrlen, _idsetup, _idlen,	\
		   _uncompress, _compress)	\
static u8 __nhc##_val[_idlen];			\
static u8 __nhc##_mask[_idlen];			\
static struct lowpan_nhc __nhc = {		\
	.name		= _name,		\
	.nexthdr	= _nexthdr,		\
	.nexthdrlen	= _hdrlen,		\
	.id		= __nhc##_val,		\
	.idmask		= __nhc##_mask,		\
	.idlen		= _idlen,		\
	.idsetup	= _idsetup,		\
	.uncompress	= _uncompress,		\
	.compress	= _compress,		\
}

#define module_lowpan_nhc(__nhc)		\
static int __init __nhc##_init(void)		\
{						\
	return lowpan_nhc_add(&(__nhc));	\
}						\
module_init(__nhc##_init);			\
static void __exit __nhc##_exit(void)		\
{						\
	lowpan_nhc_del(&(__nhc));		\
}						\
module_exit(__nhc##_exit);

/**
 * struct lowpan_nhc - hold 6lowpan next hdr compression ifnformation
 *
 * @node: holder for the rbtree.
 * @name: name of the specific next header compression
 * @nexthdr: next header value of the protocol which should be compressed.
 * @nexthdrlen: ipv6 nexthdr len for the reserved space.
 * @id: array for nhc id. Note this need to be in network byteorder.
 * @mask: array for nhc id mask. Note this need to be in network byteorder.
 * @len: the length of the next header id and mask.
 * @setup: callback to setup fill the next header id value and mask.
 * @compress: callback to do the header compression.
 * @uncompress: callback to do the header uncompression.
 */
struct lowpan_nhc {
	struct rb_node	node;
	const char	*name;
	const u8	nexthdr;
	const size_t	nexthdrlen;
	u8		*id;
	u8		*idmask;
	const size_t	idlen;

	void		(*idsetup)(struct lowpan_nhc *nhc);
	int		(*uncompress)(struct sk_buff *skb, size_t needed);
	int		(*compress)(struct sk_buff *skb, u8 **hc_ptr);
};

/**
 * lowpan_nhc_by_nexthdr - return the 6lowpan nhc by ipv6 nexthdr.
 *
 * @nexthdr: ipv6 nexthdr value.
 */
struct lowpan_nhc *lowpan_nhc_by_nexthdr(u8 nexthdr);

/**
 * lowpan_nhc_check_compression - checks if we support compression format. If
 *	we support the nhc by nexthdr field, the 6LoWPAN iphc NHC bit will be
 *	set. If we don't support nexthdr will be added as inline data to the
 *	6LoWPAN header.
 *
 * @skb: skb of 6LoWPAN header to read nhc and replace header.
 * @hdr: ipv6hdr to check the nexthdr value
 * @hc_ptr: pointer for 6LoWPAN header which should increment at the end of
 *	    replaced header.
 * @iphc0: iphc0 pointer to set the 6LoWPAN NHC bit
 */
int lowpan_nhc_check_compression(struct sk_buff *skb,
				 const struct ipv6hdr *hdr, u8 **hc_ptr,
				 u8 *iphc0);

/**
 * lowpan_nhc_do_compression - calling compress callback for nhc
 *
 * @skb: skb of 6LoWPAN header to read nhc and replace header.
 * @hdr: ipv6hdr to set the nexthdr value
 * @hc_ptr: pointer for 6LoWPAN header which should increment at the end of
 *	    replaced header.
 */
int lowpan_nhc_do_compression(struct sk_buff *skb, const struct ipv6hdr *hdr,
			      u8 **hc_ptr);

/**
 * lowpan_nhc_do_uncompression - calling uncompress callback for nhc
 *
 * @nhc: 6LoWPAN nhc context, get by lowpan_nhc_by_ functions.
 * @skb: skb of 6LoWPAN header, skb->data should be pointed to nhc id value.
 * @dev: netdevice for print logging information.
 * @hdr: ipv6hdr for setting nexthdr value.
 */
int lowpan_nhc_do_uncompression(struct sk_buff *skb, struct net_device *dev,
				struct ipv6hdr *hdr);

/**
 * lowpan_nhc_add - register a next header compression to framework
 *
 * @nhc: nhc which should be add.
 */
int lowpan_nhc_add(struct lowpan_nhc *nhc);

/**
 * lowpan_nhc_del - delete a next header compression from framework
 *
 * @nhc: nhc which should be delete.
 */
void lowpan_nhc_del(struct lowpan_nhc *nhc);

/**
 * lowpan_nhc_init - adding all default nhcs
 */
void lowpan_nhc_init(void);

#endif /* __6LOWPAN_NHC_H */
