#ifndef _UAPI_LWTUNNEL_H_
#define _UAPI_LWTUNNEL_H_

#include <linux/types.h>

enum lwtunnel_encap_types {
	LWTUNNEL_ENCAP_NONE,
	LWTUNNEL_ENCAP_MPLS,
	LWTUNNEL_ENCAP_IP,
	__LWTUNNEL_ENCAP_MAX,
};

#define LWTUNNEL_ENCAP_MAX (__LWTUNNEL_ENCAP_MAX - 1)


#endif /* _UAPI_LWTUNNEL_H_ */
