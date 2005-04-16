/* Kernel module to match Hop-by-Hop and Destination parameters. */

/* (C) 2001-2002 Andras Kis-Szabo <kisza@sch.bme.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ipv6.h>
#include <linux/types.h>
#include <net/checksum.h>
#include <net/ipv6.h>

#include <asm/byteorder.h>

#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter_ipv6/ip6t_opts.h>

#define HOPBYHOP	0

MODULE_LICENSE("GPL");
#if HOPBYHOP
MODULE_DESCRIPTION("IPv6 HbH match");
#else
MODULE_DESCRIPTION("IPv6 DST match");
#endif
MODULE_AUTHOR("Andras Kis-Szabo <kisza@sch.bme.hu>");

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

/*
 * (Type & 0xC0) >> 6
 * 	0	-> ignorable
 * 	1	-> must drop the packet
 * 	2	-> send ICMP PARM PROB regardless and drop packet
 * 	3	-> Send ICMP if not a multicast address and drop packet
 *  (Type & 0x20) >> 5
 *  	0	-> invariant
 *  	1	-> can change the routing
 *  (Type & 0x1F) Type
 *      0	-> Pad1 (only 1 byte!)
 *      1	-> PadN LENGTH info (total length = length + 2)
 *      C0 | 2	-> JUMBO 4 x x x x ( xxxx > 64k )
 *      5	-> RTALERT 2 x x
 */

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
       struct ipv6_opt_hdr _optsh, *oh;
       const struct ip6t_opts *optinfo = matchinfo;
       unsigned int temp;
       unsigned int len;
       u8 nexthdr;
       unsigned int ptr;
       unsigned int hdrlen = 0;
       unsigned int ret = 0;
       u8 _opttype, *tp = NULL;
       u8 _optlen, *lp = NULL;
       unsigned int optlen;
       
       /* type of the 1st exthdr */
       nexthdr = skb->nh.ipv6h->nexthdr;
       /* pointer to the 1st exthdr */
       ptr = sizeof(struct ipv6hdr);
       /* available length */
       len = skb->len - ptr;
       temp = 0;

        while (ip6t_ext_hdr(nexthdr)) {
               struct ipv6_opt_hdr _hdr, *hp;

              DEBUGP("ipv6_opts header iteration \n");

              /* Is there enough space for the next ext header? */
                if (len < (int)sizeof(struct ipv6_opt_hdr))
                        return 0;
              /* No more exthdr -> evaluate */
                if (nexthdr == NEXTHDR_NONE) {
                     break;
              }
              /* ESP -> evaluate */
                if (nexthdr == NEXTHDR_ESP) {
                     break;
              }

	      hp = skb_header_pointer(skb, ptr, sizeof(_hdr), &_hdr);
	      BUG_ON(hp == NULL);

              /* Calculate the header length */
                if (nexthdr == NEXTHDR_FRAGMENT) {
                        hdrlen = 8;
                } else if (nexthdr == NEXTHDR_AUTH)
                        hdrlen = (hp->hdrlen+2)<<2;
                else
                        hdrlen = ipv6_optlen(hp);

              /* OPTS -> evaluate */
#if HOPBYHOP
                if (nexthdr == NEXTHDR_HOP) {
                     temp |= MASK_HOPOPTS;
#else
                if (nexthdr == NEXTHDR_DEST) {
                     temp |= MASK_DSTOPTS;
#endif
                     break;
              }


              /* set the flag */
              switch (nexthdr){
                     case NEXTHDR_HOP:
                     case NEXTHDR_ROUTING:
                     case NEXTHDR_FRAGMENT:
                     case NEXTHDR_AUTH:
                     case NEXTHDR_DEST:
                            break;
                     default:
                            DEBUGP("ipv6_opts match: unknown nextheader %u\n",nexthdr);
                            return 0;
                            break;
              }

                nexthdr = hp->nexthdr;
                len -= hdrlen;
                ptr += hdrlen;
		if ( ptr > skb->len ) {
			DEBUGP("ipv6_opts: new pointer is too large! \n");
			break;
		}
        }

       /* OPTIONS header not found */
#if HOPBYHOP
       if ( temp != MASK_HOPOPTS ) return 0;
#else
       if ( temp != MASK_DSTOPTS ) return 0;
#endif

       if (len < (int)sizeof(struct ipv6_opt_hdr)){
	       *hotdrop = 1;
       		return 0;
       }

       if (len < hdrlen){
	       /* Packet smaller than it's length field */
       		return 0;
       }

       oh = skb_header_pointer(skb, ptr, sizeof(_optsh), &_optsh);
       BUG_ON(oh == NULL);

       DEBUGP("IPv6 OPTS LEN %u %u ", hdrlen, oh->hdrlen);

       DEBUGP("len %02X %04X %02X ",
       		optinfo->hdrlen, hdrlen,
       		(!(optinfo->flags & IP6T_OPTS_LEN) ||
                           ((optinfo->hdrlen == hdrlen) ^
                           !!(optinfo->invflags & IP6T_OPTS_INV_LEN))));

       ret = (oh != NULL)
       		&&
	      	(!(optinfo->flags & IP6T_OPTS_LEN) ||
                           ((optinfo->hdrlen == hdrlen) ^
                           !!(optinfo->invflags & IP6T_OPTS_INV_LEN)));

       ptr += 2;
       hdrlen -= 2;
       if ( !(optinfo->flags & IP6T_OPTS_OPTS) ){
	       return ret;
	} else if (optinfo->flags & IP6T_OPTS_NSTRICT) {
		DEBUGP("Not strict - not implemented");
	} else {
		DEBUGP("Strict ");
		DEBUGP("#%d ",optinfo->optsnr);
		for(temp=0; temp<optinfo->optsnr; temp++){
			/* type field exists ? */
			if (hdrlen < 1)
				break;
			tp = skb_header_pointer(skb, ptr, sizeof(_opttype),
						&_opttype);
			if (tp == NULL)
				break;

			/* Type check */
			if (*tp != (optinfo->opts[temp] & 0xFF00)>>8){
				DEBUGP("Tbad %02X %02X\n",
				       *tp,
				       (optinfo->opts[temp] & 0xFF00)>>8);
				return 0;
			} else {
				DEBUGP("Tok ");
			}
			/* Length check */
			if (*tp) {
				u16 spec_len;

				/* length field exists ? */
				if (hdrlen < 2)
					break;
				lp = skb_header_pointer(skb, ptr + 1,
							sizeof(_optlen),
							&_optlen);
				if (lp == NULL)
					break;
				spec_len = optinfo->opts[temp] & 0x00FF;

				if (spec_len != 0x00FF && spec_len != *lp) {
					DEBUGP("Lbad %02X %04X\n", *lp,
					       spec_len);
					return 0;
				}
				DEBUGP("Lok ");
				optlen = *lp + 2;
			} else {
				DEBUGP("Pad1\n");
				optlen = 1;
			}

			/* Step to the next */
			DEBUGP("len%04X \n", optlen);

			if ((ptr > skb->len - optlen || hdrlen < optlen) &&
			    (temp < optinfo->optsnr - 1)) {
				DEBUGP("new pointer is too large! \n");
				break;
			}
			ptr += optlen;
			hdrlen -= optlen;
		}
		if (temp == optinfo->optsnr)
			return ret;
		else return 0;
	}

	return 0;
}

/* Called when user tries to insert an entry of this type. */
static int
checkentry(const char *tablename,
          const struct ip6t_ip6 *ip,
          void *matchinfo,
          unsigned int matchinfosize,
          unsigned int hook_mask)
{
       const struct ip6t_opts *optsinfo = matchinfo;

       if (matchinfosize != IP6T_ALIGN(sizeof(struct ip6t_opts))) {
              DEBUGP("ip6t_opts: matchsize %u != %u\n",
                      matchinfosize, IP6T_ALIGN(sizeof(struct ip6t_opts)));
              return 0;
       }
       if (optsinfo->invflags & ~IP6T_OPTS_INV_MASK) {
              DEBUGP("ip6t_opts: unknown flags %X\n",
                      optsinfo->invflags);
              return 0;
       }

       return 1;
}

static struct ip6t_match opts_match = {
#if HOPBYHOP
	.name		= "hbh",
#else
	.name		= "dst",
#endif
	.match		= &match,
	.checkentry	= &checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
       return ip6t_register_match(&opts_match);
}

static void __exit cleanup(void)
{
       ip6t_unregister_match(&opts_match);
}

module_init(init);
module_exit(cleanup);
