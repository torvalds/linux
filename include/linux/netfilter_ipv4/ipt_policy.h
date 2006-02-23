#ifndef _IPT_POLICY_H
#define _IPT_POLICY_H

#define IPT_POLICY_MAX_ELEM	4

enum ipt_policy_flags
{
	IPT_POLICY_MATCH_IN	= 0x1,
	IPT_POLICY_MATCH_OUT	= 0x2,
	IPT_POLICY_MATCH_NONE	= 0x4,
	IPT_POLICY_MATCH_STRICT	= 0x8,
};

enum ipt_policy_modes
{
	IPT_POLICY_MODE_TRANSPORT,
	IPT_POLICY_MODE_TUNNEL
};

struct ipt_policy_spec
{
	u_int8_t	saddr:1,
			daddr:1,
			proto:1,
			mode:1,
			spi:1,
			reqid:1;
};

union ipt_policy_addr
{
	struct in_addr	a4;
	struct in6_addr	a6;
};

struct ipt_policy_elem
{
	union ipt_policy_addr	saddr;
	union ipt_policy_addr	smask;
	union ipt_policy_addr	daddr;
	union ipt_policy_addr	dmask;
	u_int32_t		spi;
	u_int32_t		reqid;
	u_int8_t		proto;
	u_int8_t		mode;

	struct ipt_policy_spec	match;
	struct ipt_policy_spec	invert;
};

struct ipt_policy_info
{
	struct ipt_policy_elem pol[IPT_POLICY_MAX_ELEM];
	u_int16_t flags;
	u_int16_t len;
};

#endif /* _IPT_POLICY_H */
