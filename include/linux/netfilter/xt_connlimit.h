#ifndef _XT_CONNLIMIT_H
#define _XT_CONNLIMIT_H

struct xt_connlimit_data;

struct xt_connlimit_info {
	union {
		__be32 v4_mask;
		__be32 v6_mask[4];
	};
	unsigned int limit, inverse;

	/* this needs to be at the end */
	struct xt_connlimit_data *data __attribute__((aligned(8)));
};

#endif /* _XT_CONNLIMIT_H */
