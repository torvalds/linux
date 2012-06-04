#ifndef XT_HMARK_H_
#define XT_HMARK_H_

#include <linux/types.h>

enum {
	XT_HMARK_SADDR_MASK,
	XT_HMARK_DADDR_MASK,
	XT_HMARK_SPI,
	XT_HMARK_SPI_MASK,
	XT_HMARK_SPORT,
	XT_HMARK_DPORT,
	XT_HMARK_SPORT_MASK,
	XT_HMARK_DPORT_MASK,
	XT_HMARK_PROTO_MASK,
	XT_HMARK_RND,
	XT_HMARK_MODULUS,
	XT_HMARK_OFFSET,
	XT_HMARK_CT,
	XT_HMARK_METHOD_L3,
	XT_HMARK_METHOD_L3_4,
};
#define XT_HMARK_FLAG(flag)	(1 << flag)

union hmark_ports {
	struct {
		__u16	src;
		__u16	dst;
	} p16;
	__u32	v32;
};

struct xt_hmark_info {
	union nf_inet_addr	src_mask;
	union nf_inet_addr	dst_mask;
	union hmark_ports	port_mask;
	union hmark_ports	port_set;
	__u32			flags;
	__u16			proto_mask;
	__u32			hashrnd;
	__u32			hmodulus;
	__u32			hoffset;	/* Mark offset to start from */
};

#endif /* XT_HMARK_H_ */
