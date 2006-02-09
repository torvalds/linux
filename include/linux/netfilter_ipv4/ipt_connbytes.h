#ifndef _IPT_CONNBYTES_H
#define _IPT_CONNBYTES_H

#include <linux/netfilter/xt_connbytes.h>
#define ipt_connbytes_what xt_connbytes_what

#define IPT_CONNBYTES_PKTS	XT_CONNBYTES_PKTS
#define IPT_CONNBYTES_BYTES	XT_CONNBYTES_BYTES
#define IPT_CONNBYTES_AVGPKT	XT_CONNBYTES_AVGPKT

#define ipt_connbytes_direction 	xt_connbytes_direction
#define IPT_CONNBYTES_DIR_ORIGINAL 	XT_CONNBYTES_DIR_ORIGINAL
#define IPT_CONNBYTES_DIR_REPLY 	XT_CONNBYTES_DIR_REPLY
#define IPT_CONNBYTES_DIR_BOTH		XT_CONNBYTES_DIR_BOTH

#define ipt_connbytes_info xt_connbytes_info

#endif
