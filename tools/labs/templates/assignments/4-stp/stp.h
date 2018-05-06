/*
 * SO2 Transport Protocol
 */

#ifndef STP_H_
#define STP_H_	1

#ifdef __cplusplus
extern "C" {
#endif

#include <linux/types.h>

/* STP reuses the defines of ancient protocols like Econet and Xerox PUP
 * because adding a new protocol would involve patching the kernel, which we
 * don't want to do and besides that, they are probably not used anymore.
 */
#define AF_STP		19
#define PF_STP		AF_STP
#define ETH_P_STP	0x0a00

struct stp_hdr {
	__be16		dst;		/* Destination port */
	__be16		src;		/* Source port */
	__be16		len;		/* Total length, including header */
	__u8		flags;		/* */
	__u8		csum;		/* xor of all bytes, including header */
};

struct sockaddr_stp {
	unsigned short	sas_family;	/* Always AF_STP */
	int		sas_ifindex;	/* Interface index */
	__be16		sas_port;	/* Port */
	__u8		sas_addr[6];	/* MAC address */
};

/* STP protocol name; used as identifier in /proc/net/protocols */
#define STP_PROTO_NAME			"STP"

/*
 * STP uses proc interface to communicate statistical information to
 * user space (in /proc/net/).
 */
#define STP_PROC_NET_FILENAME		"stp_stats"
#define STP_PROC_FULL_FILENAME		"/proc/net/" STP_PROC_NET_FILENAME

#ifdef __cplusplus
}
#endif

#endif /* STP_H_ */
