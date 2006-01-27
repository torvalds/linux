#ifndef _IPT_MARK_H_target
#define _IPT_MARK_H_target

/* Backwards compatibility for old userspace */

#include <linux/netfilter/xt_MARK.h>

/* Version 0 */
#define ipt_mark_target_info xt_mark_target_info

/* Version 1 */
#define IPT_MARK_SET	XT_MARK_SET
#define IPT_MARK_AND	XT_MARK_AND
#define	IPT_MARK_OR	XT_MARK_OR

#define ipt_mark_target_info_v1 xt_mark_target_info_v1

#endif /*_IPT_MARK_H_target*/
