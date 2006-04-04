#ifndef _IPT_MULTIPORT_H
#define _IPT_MULTIPORT_H

#include <linux/netfilter/xt_multiport.h>

#define IPT_MULTIPORT_SOURCE		XT_MULTIPORT_SOURCE
#define IPT_MULTIPORT_DESTINATION	XT_MULTIPORT_DESTINATION
#define IPT_MULTIPORT_EITHER		XT_MULTIPORT_EITHER

#define IPT_MULTI_PORTS			XT_MULTI_PORTS

#define ipt_multiport			xt_multiport
#define ipt_multiport_v1		xt_multiport_v1

#endif /*_IPT_MULTIPORT_H*/
