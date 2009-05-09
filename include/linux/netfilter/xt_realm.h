#ifndef _XT_REALM_H
#define _XT_REALM_H

#include <linux/types.h>

struct xt_realm_info {
	__u32 id;
	__u32 mask;
	__u8 invert;
};

#endif /* _XT_REALM_H */
