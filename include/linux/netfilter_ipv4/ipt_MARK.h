#ifndef _IPT_MARK_H_target
#define _IPT_MARK_H_target

/* Version 0 */
struct ipt_mark_target_info {
	unsigned long mark;
};

/* Version 1 */
enum {
	IPT_MARK_SET=0,
	IPT_MARK_AND,
	IPT_MARK_OR
};

struct ipt_mark_target_info_v1 {
	unsigned long mark;
	u_int8_t mode;
};
#endif /*_IPT_MARK_H_target*/
