#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/ip.h>
#include <linux/sctp.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_sctp.h>

#ifdef DEBUG_SCTP
#define duprintf(format, args...) printk(format , ## args)
#else
#define duprintf(format, args...)
#endif

#define SCCHECK(cond, option, flag, invflag) (!((flag) & (option)) \
					      || (!!((invflag) & (option)) ^ (cond)))

static int
match_flags(const struct ipt_sctp_flag_info *flag_info,
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

static int
match_packet(const struct sk_buff *skb,
	     const u_int32_t *chunkmap,
	     int chunk_match_type,
	     const struct ipt_sctp_flag_info *flag_info,
	     const int flag_count,
	     int *hotdrop)
{
	int offset;
	u_int32_t chunkmapcopy[256 / sizeof (u_int32_t)];
	sctp_chunkhdr_t _sch, *sch;

#ifdef DEBUG_SCTP
	int i = 0;
#endif

	if (chunk_match_type == SCTP_CHUNK_MATCH_ALL) {
		SCTP_CHUNKMAP_COPY(chunkmapcopy, chunkmap);
	}

	offset = skb->nh.iph->ihl * 4 + sizeof (sctp_sctphdr_t);
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
      int *hotdrop)
{
	const struct ipt_sctp_info *info;
	sctp_sctphdr_t _sh, *sh;

	info = (const struct ipt_sctp_info *)matchinfo;

	if (offset) {
		duprintf("Dropping non-first fragment.. FIXME\n");
		return 0;
	}
	
	sh = skb_header_pointer(skb, skb->nh.iph->ihl*4, sizeof(_sh), &_sh);
	if (sh == NULL) {
		duprintf("Dropping evil TCP offset=0 tinygram.\n");
		*hotdrop = 1;
		return 0;
       	}
	duprintf("spt: %d\tdpt: %d\n", ntohs(sh->source), ntohs(sh->dest));

	return  SCCHECK(((ntohs(sh->source) >= info->spts[0]) 
			&& (ntohs(sh->source) <= info->spts[1])), 
		   	IPT_SCTP_SRC_PORTS, info->flags, info->invflags)
		&& SCCHECK(((ntohs(sh->dest) >= info->dpts[0]) 
			&& (ntohs(sh->dest) <= info->dpts[1])), 
			IPT_SCTP_DEST_PORTS, info->flags, info->invflags)
		&& SCCHECK(match_packet(skb, info->chunkmap, info->chunk_match_type,
 					info->flag_info, info->flag_count, 
					hotdrop),
			   IPT_SCTP_CHUNK_TYPES, info->flags, info->invflags);
}

static int
checkentry(const char *tablename,
	   const struct ipt_ip *ip,
	   void *matchinfo,
	   unsigned int matchsize,
	   unsigned int hook_mask)
{
	const struct ipt_sctp_info *info;

	info = (const struct ipt_sctp_info *)matchinfo;

	return ip->proto == IPPROTO_SCTP
		&& !(ip->invflags & IPT_INV_PROTO)
		&& matchsize == IPT_ALIGN(sizeof(struct ipt_sctp_info))
		&& !(info->flags & ~IPT_SCTP_VALID_FLAGS)
		&& !(info->invflags & ~IPT_SCTP_VALID_FLAGS)
		&& !(info->invflags & ~info->flags)
		&& ((!(info->flags & IPT_SCTP_CHUNK_TYPES)) || 
			(info->chunk_match_type &
				(SCTP_CHUNK_MATCH_ALL 
				| SCTP_CHUNK_MATCH_ANY
				| SCTP_CHUNK_MATCH_ONLY)));
}

static struct ipt_match sctp_match = 
{ 
	.list = { NULL, NULL},
	.name = "sctp",
	.match = &match,
	.checkentry = &checkentry,
	.destroy = NULL,
	.me = THIS_MODULE
};

static int __init init(void)
{
	return ipt_register_match(&sctp_match);
}

static void __exit fini(void)
{
	ipt_unregister_match(&sctp_match);
}

module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kiran Kumar Immidi");
MODULE_DESCRIPTION("Match for SCTP protocol packets");

