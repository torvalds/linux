#ifndef _UAPI_XT_CGROUP_H
#define _UAPI_XT_CGROUP_H

#include <linux/types.h>

struct xt_cgroup_info {
	__u32 id;
	__u32 invert;
};

#endif /* _UAPI_XT_CGROUP_H */
