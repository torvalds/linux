#ifndef _XT_CONNLIMIT_H
#define _XT_CONNLIMIT_H

struct xt_connlimit_data;

struct xt_connlimit_info {
	union {
		u_int32_t v4_mask;
		u_int32_t v6_mask[4];
	};
	unsigned int limit, inverse;

	/* this needs to be at the end */
	struct xt_connlimit_data *data __attribute__((aligned(8)));
};

#endif /* _XT_CONNLIMIT_H */
