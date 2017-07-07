#ifndef _UAPI_VSOCKMON_H
#define _UAPI_VSOCKMON_H

#include <linux/virtio_vsock.h>

/*
 * vsockmon is the AF_VSOCK packet capture device.  Packets captured have the
 * following layout:
 *
 *   +-----------------------------------+
 *   |           vsockmon header         |
 *   |      (struct af_vsockmon_hdr)     |
 *   +-----------------------------------+
 *   |          transport header         |
 *   | (af_vsockmon_hdr->len bytes long) |
 *   +-----------------------------------+
 *   |              payload              |
 *   |       (until end of packet)       |
 *   +-----------------------------------+
 *
 * The vsockmon header is a transport-independent description of the packet.
 * It duplicates some of the information from the transport header so that
 * no transport-specific knowledge is necessary to process packets.
 *
 * The transport header is useful for low-level transport-specific packet
 * analysis.  Transport type is given in af_vsockmon_hdr->transport and
 * transport header length is given in af_vsockmon_hdr->len.
 *
 * If af_vsockmon_hdr->op is AF_VSOCK_OP_PAYLOAD then the payload follows the
 * transport header.  Other ops do not have a payload.
 */

struct af_vsockmon_hdr {
	__le64 src_cid;
	__le64 dst_cid;
	__le32 src_port;
	__le32 dst_port;
	__le16 op;			/* enum af_vsockmon_op */
	__le16 transport;		/* enum af_vsockmon_transport */
	__le16 len;			/* Transport header length */
	__u8 reserved[2];
};

enum af_vsockmon_op {
	AF_VSOCK_OP_UNKNOWN = 0,
	AF_VSOCK_OP_CONNECT = 1,
	AF_VSOCK_OP_DISCONNECT = 2,
	AF_VSOCK_OP_CONTROL = 3,
	AF_VSOCK_OP_PAYLOAD = 4,
};

enum af_vsockmon_transport {
	AF_VSOCK_TRANSPORT_UNKNOWN = 0,
	AF_VSOCK_TRANSPORT_NO_INFO = 1,	/* No transport information */

	/* Transport header type: struct virtio_vsock_hdr */
	AF_VSOCK_TRANSPORT_VIRTIO = 2,
};

#endif
