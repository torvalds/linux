#ifndef _IPT_TCPMSS_H
#define _IPT_TCPMSS_H

struct ipt_tcpmss_info {
	u_int16_t mss;
};

#define IPT_TCPMSS_CLAMP_PMTU 0xffff

#endif /*_IPT_TCPMSS_H*/
