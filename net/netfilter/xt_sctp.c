#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <linux/sctp.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_sctp.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kiran Kumar Immidi");
MODULE_DESCRIPTION("Match for SCTP protocol packets");
MODULE_ALIAS("ipt_sctp");

#ifdef DEBUG_SCTP
#define duprintf(format, args...) printk(format , ## args)
#else
#define duprintf(format, args...)
#endif

#define SCCHECK(cond, option, flag, invflag) (!((flag) & (option)) \
					      || (!!((invflag) & (option)) ^ (cond)))

static int
match_flags(const struct xt_sctp_flag_info *flag_info,
	    const int flag_count,
	    u_int8_t chunktype,
	    u_int8_t chunkflags)
{
	int i;

	for (i = 0; i < flag_count; i++) {
		if (flag_info[i].chunktype == chunktype) {
			return (chunkflags & flag_info[i].flag_mask) == flag_info[i].flag;
		}
	}

	return 1;
}

static inline int
match_packet(const struct sk_buff *skb,
	     unsigned int offset,
	     const u_int32_t *chunkmap,
	     int chunk_match_type,
	     const struct xt_sctp_flag_info *flag_info,
	     const int flag_count,
	     int *hotdrop)
{
	u_int32_t chunkmapcopy[256 / sizeof (u_int32_t)];
	sctp_chunkhdr_t _sch, *sch;

#ifdef DEBUG_SCTP
	int i = 0;
#endif

	if (chunk_match_type == SCTP_CHUNK_MATCH_ALL) {
		SCTP_CHUNKMAP_COPY(chunkmapcopy, chunkmap);
	}

	do {
		sch = skb_header_pointer(skb, offset, sizeof(_sch), &_sch);
		if (sch == NULL) {
			duprintf("Dropping invalid SCTP packet.\n");
			*hotdrop = 1;
			return 0;
        	}

		duprintf("Chunk num: %d\toffset: %d\ttype: %d\tlength: %d\tflags: %x\n", 
				++i, offset, sch->type, htons(sch->length), sch->flags);

		offset += (htons(sch->length) + 3) & ~3;

		duprintf("skb->len: %d\toffset: %d\n", skb->len, offset);

		if (SCTP_CHUNKMAP_IS_SET(chunkmap, sch->type)) {
			switch (chunk_match_type) {
			case SCTP_CHUNK_MATCH_ANY:
				if (match_flags(flag_info, flag_count, 
					sch->type, sch->flags)) {
					return 1;
				}
				break;

			case SCTP_CHUNK_MATCH_ALL:
				if (match_flags(flag_info, flag_count, 
					sch->type, sch->flags)) {
					SCTP_CHUNKMAP_CLEAR(chunkmapcopy, sch->type);
				}
				break;

			case SCTP_CHUNK_MATCH_ONLY:
				if (!match_flags(flag_info, flag_count, 
					sch->type, sch->flags)) {
					return 0;
				}
				break;
			}
		} else {
			switch (chunk_match_type) {
			case SCTP_CHUNK_MATCH_ONLY:
				return 0;
			}
		}
	} while (offset < skb->len);

	switch (chunk_match_type) {
	case SCTP_CHUNK_MATCH_ALL:
		return SCTP_CHUNKMAP_IS_CLEAR(chunkmap);
	case SCTP_CHUNK_MATCH_ANY:
		return 0;
	case SCTP_CHUNK_MATCH_ONLY:
		return 1;
	}

	/* This will never be reached, but required to stop compiler whine */
	return 0;
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
	const struct xt_sctp_info *info;
	sctp_sctphdr_t _sh, *sh;

	info = (const struct xt_sctp_info *)matchinfo;

	if (offset) {
		duprintf("Dropping non-first fragment.. FIXME\n");
		return 0;
	}
	
	sh = skb_header_pointer(skb, protoff, sizeof(_sh), &_sh);
	if (sh == NULL) {
		duprintf("Dropping evil TCP offset=0 tinygram.\n");
		*hotdrop = 1;
		return 0;
       	}
	duprintf("spt: %d\tdpt: %d\n", ntohs(sh->source), ntohs(sh->dest));

	return  SCCHECK(((ntohs(sh->source) >= info->spts[0]) 
			&& (ntohs(sh->source) <= info->spts[1])), 
		   	XT_SCTP_SRC_PORTS, info->flags, info->invflags)
		&& SCCHECK(((ntohs(sh->dest) >= info->dpts[0]) 
			&& (ntohs(sh->dest) <= info->dpts[1])), 
			XT_SCTP_DEST_PORTS, info->flags, info->invflags)
		&& SCCHECK(match_packet(skb, protoff,
					info->chunkmap, info->chunk_match_type,
 					info->flag_info, info->flag_count, 
					hotdrop),
			   XT_SCTP_CHUNK_TYPES, info->flags, info->invflags);
}

static int
checkentry(const char *tablename,
	   const void *inf,
	   void *matchinfo,
	   unsigned int matchsize,
	   unsigned int hook_mask)
{
	const struct xt_sctp_info *info;
	const struct ipt_ip *ip = inf;

	info = (const struct xt_sctp_info *)matchinfo;

	return ip->proto == IPPROTO_SCTP
		&& !(ip->invflags & XT_INV_PROTO)
		&& matchsize == XT_ALIGN(sizeof(struct xt_sctp_info))
		&& !(info->flags & ~XT_SCTP_VALID_FLAGS)
		&& !(info->invflags & ~XT_SCTP_VALID_FLAGS)
		&& !(info->invflags & ~info->flags)
		&& ((!(info->flags & XT_SCTP_CHUNK_TYPES)) || 
			(info->chunk_match_type &
				(SCTP_CHUNK_MATCH_ALL 
				| SCTP_CHUNK_MATCH_ANY
				| SCTP_CHUNK_MATCH_ONLY)));
}

static int
checkentry6(const char *tablename,
	   const void *inf,
	   void *matchinfo,
	   unsigned int matchsize,
	   unsigned int hook_mask)
{
	const struct xt_sctp_info *info;
	const struct ip6t_ip6 *ip = inf;

	info = (const struct xt_sctp_info *)matchinfo;

	return ip->proto == IPPROTO_SCTP
		&& !(ip->invflags & XT_INV_PROTO)
		&& matchsize == XT_ALIGN(sizeof(struct xt_sctp_info))
		&& !(info->flags & ~XT_SCTP_VALID_FLAGS)
		&& !(info->invflags & ~XT_SCTP_VALID_FLAGS)
		&& !(info->invflags & ~info->flags)
		&& ((!(info->flags & XT_SCTP_CHUNK_TYPES)) || 
			(info->chunk_match_type &
				(SCTP_CHUNK_MATCH_ALL 
				| SCTP_CHUNK_MATCH_ANY
				| SCTP_CHUNK_MATCH_ONLY)));
}


static struct xt_match sctp_match = 
{ 
	.name = "sctp",
	.match = &match,
	.checkentry = &checkentry,
	.me = THIS_MODULE
};
static struct xt_match sctp6_match = 
{ 
	.name = "sctp",
	.match = &match,
	.checkentry = &checkentry6,
	.me = THIS_MODULE
};


static int __init init(void)
{
	int ret;
	ret = xt_register_match(AF_INET, &sctp_match);
	if (ret)
		return ret;

	ret = xt_register_match(AF_INET6, &sctp6_match);
	if (ret)
		xt_unregister_match(AF_INET, &sctp_match);

	return ret;
}

static void __exit fini(void)
{
	xt_unregister_match(AF_INET6, &sctp6_match);
	xt_unregister_match(AF_INET, &sctp_match);
}

module_init(init);
module_exit(fini);
