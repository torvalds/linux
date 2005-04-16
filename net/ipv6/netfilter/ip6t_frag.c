/* Kernel module to match FRAG parameters. */

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

#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter_ipv6/ip6t_frag.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IPv6 FRAG match");
MODULE_AUTHOR("Andras Kis-Szabo <kisza@sch.bme.hu>");

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

/* Returns 1 if the id is matched by the range, 0 otherwise */
static inline int
id_match(u_int32_t min, u_int32_t max, u_int32_t id, int invert)
{
       int r=0;
       DEBUGP("frag id_match:%c 0x%x <= 0x%x <= 0x%x",invert? '!':' ',
              min,id,max);
       r=(id >= min && id <= max) ^ invert;
       DEBUGP(" result %s\n",r? "PASS" : "FAILED");
       return r;
}

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
       struct frag_hdr _frag, *fh = NULL;
       const struct ip6t_frag *fraginfo = matchinfo;
       unsigned int temp;
       int len;
       u8 nexthdr;
       unsigned int ptr;
       unsigned int hdrlen = 0;

       /* type of the 1st exthdr */
       nexthdr = skb->nh.ipv6h->nexthdr;
       /* pointer to the 1st exthdr */
       ptr = sizeof(struct ipv6hdr);
       /* available length */
       len = skb->len - ptr;
       temp = 0;

        while (ip6t_ext_hdr(nexthdr)) {
               struct ipv6_opt_hdr _hdr, *hp;

              DEBUGP("ipv6_frag header iteration \n");

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

              /* FRAG -> evaluate */
                if (nexthdr == NEXTHDR_FRAGMENT) {
                     temp |= MASK_FRAGMENT;
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
                            DEBUGP("ipv6_frag match: unknown nextheader %u\n",nexthdr);
                            return 0;
                            break;
              }

                nexthdr = hp->nexthdr;
                len -= hdrlen;
                ptr += hdrlen;
		if ( ptr > skb->len ) {
			DEBUGP("ipv6_frag: new pointer too large! \n");
			break;
		}
        }

       /* FRAG header not found */
       if ( temp != MASK_FRAGMENT ) return 0;

       if (len < sizeof(struct frag_hdr)){
	       *hotdrop = 1;
       		return 0;
       }

       fh = skb_header_pointer(skb, ptr, sizeof(_frag), &_frag);
       BUG_ON(fh == NULL);

       DEBUGP("INFO %04X ", fh->frag_off);
       DEBUGP("OFFSET %04X ", ntohs(fh->frag_off) & ~0x7);
       DEBUGP("RES %02X %04X", fh->reserved, ntohs(fh->frag_off) & 0x6);
       DEBUGP("MF %04X ", fh->frag_off & htons(IP6_MF));
       DEBUGP("ID %u %08X\n", ntohl(fh->identification),
	      ntohl(fh->identification));

       DEBUGP("IPv6 FRAG id %02X ",
       		(id_match(fraginfo->ids[0], fraginfo->ids[1],
                           ntohl(fh->identification),
                           !!(fraginfo->invflags & IP6T_FRAG_INV_IDS))));
       DEBUGP("res %02X %02X%04X %02X ", 
       		(fraginfo->flags & IP6T_FRAG_RES), fh->reserved,
		ntohs(fh->frag_off) & 0x6,
       		!((fraginfo->flags & IP6T_FRAG_RES)
			&& (fh->reserved || (ntohs(fh->frag_off) & 0x06))));
       DEBUGP("first %02X %02X %02X ", 
       		(fraginfo->flags & IP6T_FRAG_FST),
		ntohs(fh->frag_off) & ~0x7,
       		!((fraginfo->flags & IP6T_FRAG_FST)
			&& (ntohs(fh->frag_off) & ~0x7)));
       DEBUGP("mf %02X %02X %02X ", 
       		(fraginfo->flags & IP6T_FRAG_MF),
		ntohs(fh->frag_off) & IP6_MF,
       		!((fraginfo->flags & IP6T_FRAG_MF)
			&& !((ntohs(fh->frag_off) & IP6_MF))));
       DEBUGP("last %02X %02X %02X\n", 
       		(fraginfo->flags & IP6T_FRAG_NMF),
		ntohs(fh->frag_off) & IP6_MF,
       		!((fraginfo->flags & IP6T_FRAG_NMF)
			&& (ntohs(fh->frag_off) & IP6_MF)));

       return (fh != NULL)
       		&&
       		(id_match(fraginfo->ids[0], fraginfo->ids[1],
			  ntohl(fh->identification),
                           !!(fraginfo->invflags & IP6T_FRAG_INV_IDS)))
		&&
		!((fraginfo->flags & IP6T_FRAG_RES)
			&& (fh->reserved || (ntohs(fh->frag_off) & 0x6)))
		&&
		!((fraginfo->flags & IP6T_FRAG_FST)
			&& (ntohs(fh->frag_off) & ~0x7))
		&&
		!((fraginfo->flags & IP6T_FRAG_MF)
			&& !(ntohs(fh->frag_off) & IP6_MF))
		&&
		!((fraginfo->flags & IP6T_FRAG_NMF)
			&& (ntohs(fh->frag_off) & IP6_MF));
}

/* Called when user tries to insert an entry of this type. */
static int
checkentry(const char *tablename,
          const struct ip6t_ip6 *ip,
          void *matchinfo,
          unsigned int matchinfosize,
          unsigned int hook_mask)
{
       const struct ip6t_frag *fraginfo = matchinfo;

       if (matchinfosize != IP6T_ALIGN(sizeof(struct ip6t_frag))) {
              DEBUGP("ip6t_frag: matchsize %u != %u\n",
                      matchinfosize, IP6T_ALIGN(sizeof(struct ip6t_frag)));
              return 0;
       }
       if (fraginfo->invflags & ~IP6T_FRAG_INV_MASK) {
              DEBUGP("ip6t_frag: unknown flags %X\n",
                      fraginfo->invflags);
              return 0;
       }

       return 1;
}

static struct ip6t_match frag_match = {
	.name		= "frag",
	.match		= &match,
	.checkentry	= &checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
       return ip6t_register_match(&frag_match);
}

static void __exit cleanup(void)
{
       ip6t_unregister_match(&frag_match);
}

module_init(init);
module_exit(cleanup);
