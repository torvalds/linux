#ifndef _XT_POLICY_H
#define _XT_POLICY_H

#define XT_POLICY_MAX_ELEM	4

enum xt_policy_flags
{
	XT_POLICY_MATCH_IN	= 0x1,
	XT_POLICY_MATCH_OUT	= 0x2,
	XT_POLICY_MATCH_NONE	= 0x4,
	XT_POLICY_MATCH_STRICT	= 0x8,
};

enum xt_policy_modes
{
	XT_POLICY_MODE_TRANSPORT,
	XT_POLICY_MODE_TUNNEL
};

struct xt_policy_spec
{
	u_int8_t	saddr:1,
			daddr:1,
			proto:1,
			mode:1,
			spi:1,
			reqid:1;
};

union xt_policy_addr
{
	struct in_addr	a4;
	struct in6_addr	a6;
};

struct xt_policy_elem
{
	union xt_policy_addr	saddr;
	union xt_policy_addr	smask;
	union xt_policy_addr	daddr;
	union xt_policy_addr	dmask;
	__be32			spi;
	u_int32_t		reqid;
	u_int8_t		proto;
	u_int8_t		mode;

	struct xt_policy_spec	match;
	struct xt_policy_spec	invert;
};

struct xt_policy_info
{
	struct xt_policy_elem pol[XT_POLICY_MAX_ELEM];
	u_int16_t flags;
	u_int16_t len;
};

#endif /* _XT_POLICY_H */
