#ifndef _IPT_REALM_H
#define _IPT_REALM_H

struct ipt_realm_info {
	u_int32_t id;
	u_int32_t mask;
	u_int8_t invert;
};

#endif /* _IPT_REALM_H */
