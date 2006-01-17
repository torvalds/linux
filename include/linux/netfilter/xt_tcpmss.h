#ifndef _XT_TCPMSS_MATCH_H
#define _XT_TCPMSS_MATCH_H

struct xt_tcpmss_match_info {
    u_int16_t mss_min, mss_max;
    u_int8_t invert;
};

#endif /*_XT_TCPMSS_MATCH_H*/
