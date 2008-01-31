#ifndef _XT_OWNER_MATCH_H
#define _XT_OWNER_MATCH_H

enum {
	XT_OWNER_UID    = 1 << 0,
	XT_OWNER_GID    = 1 << 1,
	XT_OWNER_SOCKET = 1 << 2,
};

struct xt_owner_match_info {
	u_int32_t uid_min, uid_max;
	u_int32_t gid_min, gid_max;
	u_int8_t match, invert;
};

#endif /* _XT_OWNER_MATCH_H */
