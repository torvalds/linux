#ifndef _XT_MARK_H_target
#define _XT_MARK_H_target

#include <linux/types.h>

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
	__u8 mode;
};

struct xt_mark_tginfo2 {
	__u32 mark, mask;
};

#endif /*_XT_MARK_H_target */
