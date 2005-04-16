#ifndef _IP6T_MARK_H
#define _IP6T_MARK_H

struct ip6t_mark_info {
    unsigned long mark, mask;
    u_int8_t invert;
};

#endif /*_IPT_MARK_H*/
