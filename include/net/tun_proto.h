#ifndef __NET_TUN_PROTO_H
#define __NET_TUN_PROTO_H

#include <linux/if_ether.h>
#include <linux/types.h>

/* One byte protocol values as defined by VXLAN-GPE and NSH. These will
 * hopefully get a shared IANA registry.
 */
#define TUN_P_IPV4      0x01
#define TUN_P_IPV6      0x02
#define TUN_P_ETHERNET  0x03
#define TUN_P_NSH       0x04
#define TUN_P_MPLS_UC   0x05

static inline __be16 tun_p_to_eth_p(u8 proto)
{
	switch (proto) {
	case TUN_P_IPV4:
		return htons(ETH_P_IP);
	case TUN_P_IPV6:
		return htons(ETH_P_IPV6);
	case TUN_P_ETHERNET:
		return htons(ETH_P_TEB);
	case TUN_P_NSH:
		return htons(ETH_P_NSH);
	case TUN_P_MPLS_UC:
		return htons(ETH_P_MPLS_UC);
	}
	return 0;
}

static inline u8 tun_p_from_eth_p(__be16 proto)
{
	switch (proto) {
	case htons(ETH_P_IP):
		return TUN_P_IPV4;
	case htons(ETH_P_IPV6):
		return TUN_P_IPV6;
	case htons(ETH_P_TEB):
		return TUN_P_ETHERNET;
	case htons(ETH_P_NSH):
		return TUN_P_NSH;
	case htons(ETH_P_MPLS_UC):
		return TUN_P_MPLS_UC;
	}
	return 0;
}

#endif
