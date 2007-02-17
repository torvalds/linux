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
		if (sch == NULL || sch->length == 0) {
			duprintf("Dropping invalid SCTP packet.\n");
			*hotdrop = 1;
			return 0;
		}

		duprintf("Chunk num: %d\toffset: %d\ttype: %d\tlength: %d\tflags: %x\n",
				++i, offset, sch->type, htons(sch->length), sch->flags);

		offset += (ntohs(sch->length) + 3) & ~3;

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
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	const struct xt_sctp_info *info = matchinfo;
	sctp_sctphdr_t _sh, *sh;

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
		&& SCCHECK(match_packet(skb, protoff + sizeof (sctp_sctphdr_t),
					info->chunkmap, info->chunk_match_type,
					info->flag_info, info->flag_count,
					hotdrop),
			   XT_SCTP_CHUNK_TYPES, info->flags, info->invflags);
}

static int
checkentry(const char *tablename,
	   const void *inf,
	   const struct xt_match *match,
	   void *matchinfo,
	   unsigned int hook_mask)
{
	const struct xt_sctp_info *info = matchinfo;

	return !(info->flags & ~XT_SCTP_VALID_FLAGS)
		&& !(info->invflags & ~XT_SCTP_VALID_FLAGS)
		&& !(info->invflags & ~info->flags)
		&& ((!(info->flags & XT_SCTP_CHUNK_TYPES)) ||
			(info->chunk_match_type &
				(SCTP_CHUNK_MATCH_ALL
				| SCTP_CHUNK_MATCH_ANY
				| SCTP_CHUNK_MATCH_ONLY)));
}

static struct xt_match xt_sctp_match[] = {
	{
		.name		= "sctp",
		.family		= AF_INET,
		.checkentry	= checkentry,
		.match		= match,
		.matchsize	= sizeof(struct xt_sctp_info),
		.proto		= IPPROTO_SCTP,
		.me		= THIS_MODULE
	},
	{
		.name		= "sctp",
		.family		= AF_INET6,
		.checkentry	= checkentry,
		.match		= match,
		.matchsize	= sizeof(struct xt_sctp_info),
		.proto		= IPPROTO_SCTP,
		.me		= THIS_MODULE
	},
};

static int __init xt_sctp_init(void)
{
	return xt_register_matches(xt_sctp_match, ARRAY_SIZE(xt_sctp_match));
}

static void __exit xt_sctp_fini(void)
{
	xt_unregister_matches(xt_sctp_match, ARRAY_SIZE(xt_sctp_match));
}

module_init(xt_sctp_init);
module_exit(xt_sctp_fini);
