#ifndef _XT_TCPMSS_H
#define _XT_TCPMSS_H

struct xt_tcpmss_info {
	u_int16_t mss;
};

#define XT_TCPMSS_CLAMP_PMTU 0xffff

#endif /* _XT_TCPMSS_H */
