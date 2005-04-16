#ifndef _IP6T_MAC_H
#define _IP6T_MAC_H

struct ip6t_mac_info {
    unsigned char srcaddr[ETH_ALEN];
    int invert;
};
#endif /*_IPT_MAC_H*/
