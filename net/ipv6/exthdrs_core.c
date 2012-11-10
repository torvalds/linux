/*
 * IPv6 library code, needed by static components when full IPv6 support is
 * not configured or static.
 */
#include <linux/export.h>
#include <net/ipv6.h>

/*
 * find out if nexthdr is a well-known extension header or a protocol
 */

bool ipv6_ext_hdr(u8 nexthdr)
{
	/*
	 * find out if nexthdr is an extension header or a protocol
	 */
	return   (nexthdr == NEXTHDR_HOP)	||
		 (nexthdr == NEXTHDR_ROUTING)	||
		 (nexthdr == NEXTHDR_FRAGMENT)	||
		 (nexthdr == NEXTHDR_AUTH)	||
		 (nexthdr == NEXTHDR_NONE)	||
		 (nexthdr == NEXTHDR_DEST);
}
EXPORT_SYMBOL(ipv6_ext_hdr);

/*
 * Skip any extension headers. This is used by the ICMP module.
 *
 * Note that strictly speaking this conflicts with RFC 2460 4.0:
 * ...The contents and semantics of each extension header determine whether
 * or not to proceed to the next header.  Therefore, extension headers must
 * be processed strictly in the order they appear in the packet; a
 * receiver must not, for example, scan through a packet looking for a
 * particular kind of extension header and process that header prior to
 * processing all preceding ones.
 *
 * We do exactly this. This is a protocol bug. We can't decide after a
 * seeing an unknown discard-with-error flavour TLV option if it's a
 * ICMP error message or not (errors should never be send in reply to
 * ICMP error messages).
 *
 * But I see no other way to do this. This might need to be reexamined
 * when Linux implements ESP (and maybe AUTH) headers.
 * --AK
 *
 * This function parses (probably truncated) exthdr set "hdr".
 * "nexthdrp" initially points to some place,
 * where type of the first header can be found.
 *
 * It skips all well-known exthdrs, and returns pointer to the start
 * of unparsable area i.e. the first header with unknown type.
 * If it is not NULL *nexthdr is updated by type/protocol of this header.
 *
 * NOTES: - if packet terminated with NEXTHDR_NONE it returns NULL.
 *        - it may return pointer pointing beyond end of packet,
 *	    if the last recognized header is truncated in the middle.
 *        - if packet is truncated, so that all parsed headers are skipped,
 *	    it returns NULL.
 *	  - First fragment header is skipped, not-first ones
 *	    are considered as unparsable.
 *	  - Reports the offset field of the final fragment header so it is
 *	    possible to tell whether this is a first fragment, later fragment,
 *	    or not fragmented.
 *	  - ESP is unparsable for now and considered like
 *	    normal payload protocol.
 *	  - Note also special handling of AUTH header. Thanks to IPsec wizards.
 *
 * --ANK (980726)
 */

int ipv6_skip_exthdr(const struct sk_buff *skb, int start, u8 *nexthdrp,
		     __be16 *frag_offp)
{
	u8 nexthdr = *nexthdrp;

	*frag_offp = 0;

	while (ipv6_ext_hdr(nexthdr)) {
		struct ipv6_opt_hdr _hdr, *hp;
		int hdrlen;

		if (nexthdr == NEXTHDR_NONE)
			return -1;
		hp = skb_header_pointer(skb, start, sizeof(_hdr), &_hdr);
		if (hp == NULL)
			return -1;
		if (nexthdr == NEXTHDR_FRAGMENT) {
			__be16 _frag_off, *fp;
			fp = skb_header_pointer(skb,
						start+offsetof(struct frag_hdr,
							       frag_off),
						sizeof(_frag_off),
						&_frag_off);
			if (fp == NULL)
				return -1;

			*frag_offp = *fp;
			if (ntohs(*frag_offp) & ~0x7)
				break;
			hdrlen = 8;
		} else if (nexthdr == NEXTHDR_AUTH)
			hdrlen = (hp->hdrlen+2)<<2;
		else
			hdrlen = ipv6_optlen(hp);

		nexthdr = hp->nexthdr;
		start += hdrlen;
	}

	*nexthdrp = nexthdr;
	return start;
}
EXPORT_SYMBOL(ipv6_skip_exthdr);

/*
 * find the offset to specified header or the protocol number of last header
 * if target < 0. "last header" is transport protocol header, ESP, or
 * "No next header".
 *
 * Note that *offset is used as input/output parameter. an if it is not zero,
 * then it must be a valid offset to an inner IPv6 header. This can be used
 * to explore inner IPv6 header, eg. ICMPv6 error messages.
 *
 * If target header is found, its offset is set in *offset and return protocol
 * number. Otherwise, return -1.
 *
 * If the first fragment doesn't contain the final protocol header or
 * NEXTHDR_NONE it is considered invalid.
 *
 * Note that non-1st fragment is special case that "the protocol number
 * of last header" is "next header" field in Fragment header. In this case,
 * *offset is meaningless and fragment offset is stored in *fragoff if fragoff
 * isn't NULL.
 *
 * if flags is not NULL and it's a fragment, then the frag flag
 * IP6_FH_F_FRAG will be set. If it's an AH header, the
 * IP6_FH_F_AUTH flag is set and target < 0, then this function will
 * stop at the AH header. If IP6_FH_F_SKIP_RH flag was passed, then this
 * function will skip all those routing headers, where segements_left was 0.
 */
int ipv6_find_hdr(const struct sk_buff *skb, unsigned int *offset,
		  int target, unsigned short *fragoff, int *flags)
{
	unsigned int start = skb_network_offset(skb) + sizeof(struct ipv6hdr);
	u8 nexthdr = ipv6_hdr(skb)->nexthdr;
	unsigned int len;
	bool found;

	if (fragoff)
		*fragoff = 0;

	if (*offset) {
		struct ipv6hdr _ip6, *ip6;

		ip6 = skb_header_pointer(skb, *offset, sizeof(_ip6), &_ip6);
		if (!ip6 || (ip6->version != 6)) {
			printk(KERN_ERR "IPv6 header not found\n");
			return -EBADMSG;
		}
		start = *offset + sizeof(struct ipv6hdr);
		nexthdr = ip6->nexthdr;
	}
	len = skb->len - start;

	do {
		struct ipv6_opt_hdr _hdr, *hp;
		unsigned int hdrlen;
		found = (nexthdr == target);

		if ((!ipv6_ext_hdr(nexthdr)) || nexthdr == NEXTHDR_NONE) {
			if (target < 0)
				break;
			return -ENOENT;
		}

		hp = skb_header_pointer(skb, start, sizeof(_hdr), &_hdr);
		if (hp == NULL)
			return -EBADMSG;

		if (nexthdr == NEXTHDR_ROUTING) {
			struct ipv6_rt_hdr _rh, *rh;

			rh = skb_header_pointer(skb, start, sizeof(_rh),
						&_rh);
			if (rh == NULL)
				return -EBADMSG;

			if (flags && (*flags & IP6_FH_F_SKIP_RH) &&
			    rh->segments_left == 0)
				found = false;
		}

		if (nexthdr == NEXTHDR_FRAGMENT) {
			unsigned short _frag_off;
			__be16 *fp;

			if (flags)	/* Indicate that this is a fragment */
				*flags |= IP6_FH_F_FRAG;
			fp = skb_header_pointer(skb,
						start+offsetof(struct frag_hdr,
							       frag_off),
						sizeof(_frag_off),
						&_frag_off);
			if (fp == NULL)
				return -EBADMSG;

			_frag_off = ntohs(*fp) & ~0x7;
			if (_frag_off) {
				if (target < 0 &&
				    ((!ipv6_ext_hdr(hp->nexthdr)) ||
				     hp->nexthdr == NEXTHDR_NONE)) {
					if (fragoff)
						*fragoff = _frag_off;
					return hp->nexthdr;
				}
				return -ENOENT;
			}
			hdrlen = 8;
		} else if (nexthdr == NEXTHDR_AUTH) {
			if (flags && (*flags & IP6_FH_F_AUTH) && (target < 0))
				break;
			hdrlen = (hp->hdrlen + 2) << 2;
		} else
			hdrlen = ipv6_optlen(hp);

		if (!found) {
			nexthdr = hp->nexthdr;
			len -= hdrlen;
			start += hdrlen;
		}
	} while (!found);

	*offset = start;
	return nexthdr;
}
EXPORT_SYMBOL(ipv6_find_hdr);
