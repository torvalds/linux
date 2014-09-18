#ifndef _XT_MAC_H
#define _XT_MAC_H

struct xt_mac_info {
    unsigned char srcaddr[ETH_ALEN];
    int invert;
};
#endif /*_XT_MAC_H*/
