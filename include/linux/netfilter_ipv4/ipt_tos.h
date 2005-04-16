#ifndef _IPT_TOS_H
#define _IPT_TOS_H

struct ipt_tos_info {
    u_int8_t tos;
    u_int8_t invert;
};

#ifndef IPTOS_NORMALSVC
#define IPTOS_NORMALSVC 0
#endif

#endif /*_IPT_TOS_H*/
