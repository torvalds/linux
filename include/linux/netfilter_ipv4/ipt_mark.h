#ifndef _IPT_MARK_H
#define _IPT_MARK_H

struct ipt_mark_info {
    unsigned long mark, mask;
    u_int8_t invert;
};

#endif /*_IPT_MARK_H*/
