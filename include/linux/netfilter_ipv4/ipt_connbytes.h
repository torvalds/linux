#ifndef _IPT_CONNBYTES_H
#define _IPT_CONNBYTES_H

enum ipt_connbytes_what {
	IPT_CONNBYTES_PKTS,
	IPT_CONNBYTES_BYTES,
	IPT_CONNBYTES_AVGPKT,
};

enum ipt_connbytes_direction {
	IPT_CONNBYTES_DIR_ORIGINAL,
	IPT_CONNBYTES_DIR_REPLY,
	IPT_CONNBYTES_DIR_BOTH,
};

struct ipt_connbytes_info
{
	struct {
		aligned_u64 from;	/* count to be matched */
		aligned_u64 to;		/* count to be matched */
	} count;
	u_int8_t what;		/* ipt_connbytes_what */
	u_int8_t direction;	/* ipt_connbytes_direction */
};
#endif
