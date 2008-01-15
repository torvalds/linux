#ifndef _XT_MARK_H_target
#define _XT_MARK_H_target

/* Version 0 */
struct xt_mark_target_info {
	unsigned long mark;
};

/* Version 1 */
enum {
	XT_MARK_SET=0,
	XT_MARK_AND,
	XT_MARK_OR,
};

struct xt_mark_target_info_v1 {
	unsigned long mark;
	u_int8_t mode;
};

struct xt_mark_tginfo2 {
	u_int32_t mark, mask;
};

#endif /*_XT_MARK_H_target */
