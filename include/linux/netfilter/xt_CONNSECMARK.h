#ifndef _XT_CONNSECMARK_H_target
#define _XT_CONNSECMARK_H_target

enum {
	CONNSECMARK_SAVE = 1,
	CONNSECMARK_RESTORE,
};

struct xt_connsecmark_target_info {
	u_int8_t mode;
};

#endif /*_XT_CONNSECMARK_H_target */
