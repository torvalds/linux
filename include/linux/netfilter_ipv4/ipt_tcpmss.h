#ifndef _IPT_TCPMSS_MATCH_H
#define _IPT_TCPMSS_MATCH_H

struct ipt_tcpmss_match_info {
    u_int16_t mss_min, mss_max;
    u_int8_t invert;
};

#endif /*_IPT_TCPMSS_MATCH_H*/
