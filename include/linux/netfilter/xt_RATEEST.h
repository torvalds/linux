#ifndef _XT_RATEEST_TARGET_H
#define _XT_RATEEST_TARGET_H

struct xt_rateest_target_info {
	char			name[IFNAMSIZ];
	int8_t			interval;
	u_int8_t		ewma_log;

	/* Used internally by the kernel */
	struct xt_rateest	*est __attribute__((aligned(8)));
};

#endif /* _XT_RATEEST_TARGET_H */
