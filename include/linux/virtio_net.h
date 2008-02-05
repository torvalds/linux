#ifndef _LINUX_VIRTIO_NET_H
#define _LINUX_VIRTIO_NET_H
#include <linux/virtio_config.h>

/* The ID for virtio_net */
#define VIRTIO_ID_NET	1

/* The feature bitmap for virtio net */
#define VIRTIO_NET_F_CSUM	0	/* Can handle pkts w/ partial csum */
#define VIRTIO_NET_F_MAC	5	/* Host has given MAC address. */
#define VIRTIO_NET_F_GSO	6	/* Can handle pkts w/ any GSO type */

struct virtio_net_config
{
	/* The config defining mac address (if VIRTIO_NET_F_MAC) */
	__u8 mac[6];
} __attribute__((packed));

/* This is the first element of the scatter-gather list.  If you don't
 * specify GSO or CSUM features, you can simply ignore the header. */
struct virtio_net_hdr
{
#define VIRTIO_NET_HDR_F_NEEDS_CSUM	1	// Use csum_start, csum_offset
	__u8 flags;
#define VIRTIO_NET_HDR_GSO_NONE		0	// Not a GSO frame
#define VIRTIO_NET_HDR_GSO_TCPV4	1	// GSO frame, IPv4 TCP (TSO)
#define VIRTIO_NET_HDR_GSO_UDP		3	// GSO frame, IPv4 UDP (UFO)
#define VIRTIO_NET_HDR_GSO_TCPV6	4	// GSO frame, IPv6 TCP
#define VIRTIO_NET_HDR_GSO_ECN		0x80	// TCP has ECN set
	__u8 gso_type;
	__u16 hdr_len;		/* Ethernet + IP + tcp/udp hdrs */
	__u16 gso_size;		/* Bytes to append to gso_hdr_len per frame */
	__u16 csum_start;	/* Position to start checksumming from */
	__u16 csum_offset;	/* Offset after that to place checksum */
};
#endif /* _LINUX_VIRTIO_NET_H */
