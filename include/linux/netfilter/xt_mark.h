#ifndef _XT_MARK_H
#define _XT_MARK_H

struct xt_mark_info {
    unsigned long mark, mask;
    u_int8_t invert;
};

#endif /*_XT_MARK_H*/
