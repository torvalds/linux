#ifndef _IPT_TOS_H_target
#define _IPT_TOS_H_target

#ifndef IPTOS_NORMALSVC
#define IPTOS_NORMALSVC 0
#endif

struct ipt_tos_target_info {
	u_int8_t tos;
};

#endif /*_IPT_TOS_H_target*/
