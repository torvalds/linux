#ifndef _LINUX_VIRTIO_NET_H
#define _LINUX_VIRTIO_NET_H
#include <linux/virtio_config.h>

/* The ID for virtio_net */
#define VIRTIO_ID_NET	1

/* The bitmap of config for virtio net */
#define VIRTIO_CONFIG_NET_F	0x40
#define VIRTIO_NET_F_NO_CSUM	0
#define VIRTIO_NET_F_TSO4	1
#define VIRTIO_NET_F_UFO	2
#define VIRTIO_NET_F_TSO4_ECN	3
#define VIRTIO_NET_F_TSO6	4

/* The config defining mac address. */
#define VIRTIO_CONFIG_NET_MAC_F	0x41

/* This is the first element of the scatter-gather list.  If you don't
 * specify GSO or CSUM features, you can simply ignore the header. */
struct virtio_net_hdr
{
#define VIRTIO_NET_HDR_F_NEEDS_CSUM	1	// Use csum_start, csum_offset
      __u8 flags;
#define VIRTIO_NET_HDR_GSO_NONE		0	// Not a GSO frame
#define VIRTIO_NET_HDR_GSO_TCPV4	1	// GSO frame, IPv4 TCP (TSO)
/* FIXME: Do we need this?  If they said they can handle ECN, do they care? */
#define VIRTIO_NET_HDR_GSO_TCPV4_ECN	2	// GSO frame, IPv4 TCP w/ ECN
#define VIRTIO_NET_HDR_GSO_UDP		3	// GSO frame, IPv4 UDP (UFO)
#define VIRTIO_NET_HDR_GSO_TCPV6	4	// GSO frame, IPv6 TCP
      __u8 gso_type;
      __u16 gso_size;
      __u16 csum_start;
      __u16 csum_offset;
};
#endif /* _LINUX_VIRTIO_NET_H */
