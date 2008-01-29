#ifndef _XT_RATEEST_MATCH_H
#define _XT_RATEEST_MATCH_H

enum xt_rateest_match_flags {
	XT_RATEEST_MATCH_INVERT	= 1<<0,
	XT_RATEEST_MATCH_ABS	= 1<<1,
	XT_RATEEST_MATCH_REL	= 1<<2,
	XT_RATEEST_MATCH_DELTA	= 1<<3,
	XT_RATEEST_MATCH_BPS	= 1<<4,
	XT_RATEEST_MATCH_PPS	= 1<<5,
};

enum xt_rateest_match_mode {
	XT_RATEEST_MATCH_NONE,
	XT_RATEEST_MATCH_EQ,
	XT_RATEEST_MATCH_LT,
	XT_RATEEST_MATCH_GT,
};

struct xt_rateest_match_info {
	char			name1[IFNAMSIZ];
	char			name2[IFNAMSIZ];
	u_int16_t		flags;
	u_int16_t		mode;
	u_int32_t		bps1;
	u_int32_t		pps1;
	u_int32_t		bps2;
	u_int32_t		pps2;

	/* Used internally by the kernel */
	struct xt_rateest	*est1 __attribute__((aligned(8)));
	struct xt_rateest	*est2 __attribute__((aligned(8)));
};

#endif /* _XT_RATEEST_MATCH_H */
