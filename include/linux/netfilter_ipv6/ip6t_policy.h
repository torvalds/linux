#ifndef _IP6T_POLICY_H
#define _IP6T_POLICY_H

#define IP6T_POLICY_MAX_ELEM	4

enum ip6t_policy_flags
{
	IP6T_POLICY_MATCH_IN		= 0x1,
	IP6T_POLICY_MATCH_OUT		= 0x2,
	IP6T_POLICY_MATCH_NONE		= 0x4,
	IP6T_POLICY_MATCH_STRICT	= 0x8,
};

enum ip6t_policy_modes
{
	IP6T_POLICY_MODE_TRANSPORT,
	IP6T_POLICY_MODE_TUNNEL
};

struct ip6t_policy_spec
{
	u_int8_t	saddr:1,
			daddr:1,
			proto:1,
			mode:1,
			spi:1,
			reqid:1;
};

struct ip6t_policy_elem
{
	struct in6_addr	saddr;
	struct in6_addr	smask;
	struct in6_addr	daddr;
	struct in6_addr	dmask;
	u_int32_t	spi;
	u_int32_t	reqid;
	u_int8_t	proto;
	u_int8_t	mode;

	struct ip6t_policy_spec	match;
	struct ip6t_policy_spec	invert;
};

struct ip6t_policy_info
{
	struct ip6t_policy_elem pol[IP6T_POLICY_MAX_ELEM];
	u_int16_t flags;
	u_int16_t len;
};

#endif /* _IP6T_POLICY_H */
