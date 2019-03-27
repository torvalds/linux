/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _VIRTIO_NET_H
#define _VIRTIO_NET_H

/* The feature bitmap for virtio net */
#define VIRTIO_NET_F_CSUM	0x00001 /* Host handles pkts w/ partial csum */
#define VIRTIO_NET_F_GUEST_CSUM 0x00002 /* Guest handles pkts w/ partial csum*/
#define VIRTIO_NET_F_MAC	0x00020 /* Host has given MAC address. */
#define VIRTIO_NET_F_GSO	0x00040 /* Host handles pkts w/ any GSO type */
#define VIRTIO_NET_F_GUEST_TSO4	0x00080 /* Guest can handle TSOv4 in. */
#define VIRTIO_NET_F_GUEST_TSO6	0x00100 /* Guest can handle TSOv6 in. */
#define VIRTIO_NET_F_GUEST_ECN	0x00200 /* Guest can handle TSO[6] w/ ECN in.*/
#define VIRTIO_NET_F_GUEST_UFO	0x00400 /* Guest can handle UFO in. */
#define VIRTIO_NET_F_HOST_TSO4	0x00800 /* Host can handle TSOv4 in. */
#define VIRTIO_NET_F_HOST_TSO6	0x01000 /* Host can handle TSOv6 in. */
#define VIRTIO_NET_F_HOST_ECN	0x02000 /* Host can handle TSO[6] w/ ECN in. */
#define VIRTIO_NET_F_HOST_UFO	0x04000 /* Host can handle UFO in. */
#define VIRTIO_NET_F_MRG_RXBUF	0x08000 /* Host can merge receive buffers. */
#define VIRTIO_NET_F_STATUS	0x10000 /* virtio_net_config.status available*/
#define VIRTIO_NET_F_CTRL_VQ	0x20000 /* Control channel available */
#define VIRTIO_NET_F_CTRL_RX	0x40000 /* Control channel RX mode support */
#define VIRTIO_NET_F_CTRL_VLAN	0x80000 /* Control channel VLAN filtering */
#define VIRTIO_NET_F_CTRL_RX_EXTRA 0x100000 /* Extra RX mode control support */
#define VIRTIO_NET_F_GUEST_ANNOUNCE 0x200000 /* Announce device on network */
#define VIRTIO_NET_F_MQ		0x400000 /* Device supports RFS */
#define VIRTIO_NET_F_CTRL_MAC_ADDR 0x800000 /* Set MAC address */

#define VIRTIO_NET_S_LINK_UP	1	/* Link is up */

struct virtio_net_config {
	/* The config defining mac address (if VIRTIO_NET_F_MAC) */
	uint8_t		mac[ETHER_ADDR_LEN];
	/* See VIRTIO_NET_F_STATUS and VIRTIO_NET_S_* above */
	uint16_t	status;
	/* Maximum number of each of transmit and receive queues;
	 * see VIRTIO_NET_F_MQ and VIRTIO_NET_CTRL_MQ.
	 * Legal values are between 1 and 0x8000.
	 */
	uint16_t	max_virtqueue_pairs;
} __packed;

/*
 * This is the first element of the scatter-gather list.  If you don't
 * specify GSO or CSUM features, you can simply ignore the header.
 */
struct virtio_net_hdr {
#define VIRTIO_NET_HDR_F_NEEDS_CSUM	1	/* Use csum_start,csum_offset*/
#define VIRTIO_NET_HDR_F_DATA_VALID	2	/* Csum is valid */
	uint8_t	flags;
#define VIRTIO_NET_HDR_GSO_NONE		0	/* Not a GSO frame */
#define VIRTIO_NET_HDR_GSO_TCPV4	1	/* GSO frame, IPv4 TCP (TSO) */
#define VIRTIO_NET_HDR_GSO_UDP		3	/* GSO frame, IPv4 UDP (UFO) */
#define VIRTIO_NET_HDR_GSO_TCPV6	4	/* GSO frame, IPv6 TCP */
#define VIRTIO_NET_HDR_GSO_ECN		0x80	/* TCP has ECN set */
	uint8_t gso_type;
	uint16_t hdr_len;	/* Ethernet + IP + tcp/udp hdrs */
	uint16_t gso_size;	/* Bytes to append to hdr_len per frame */
	uint16_t csum_start;	/* Position to start checksumming from */
	uint16_t csum_offset;	/* Offset after that to place checksum */
};

/*
 * This is the version of the header to use when the MRG_RXBUF
 * feature has been negotiated.
 */
struct virtio_net_hdr_mrg_rxbuf {
	struct virtio_net_hdr hdr;
	uint16_t num_buffers;	/* Number of merged rx buffers */
};

/*
 * Control virtqueue data structures
 *
 * The control virtqueue expects a header in the first sg entry
 * and an ack/status response in the last entry.  Data for the
 * command goes in between.
 */
struct virtio_net_ctrl_hdr {
	uint8_t class;
	uint8_t cmd;
} __packed;

#define VIRTIO_NET_OK	0
#define VIRTIO_NET_ERR	1

/*
 * Control the RX mode, ie. promiscuous, allmulti, etc...
 * All commands require an "out" sg entry containing a 1 byte
 * state value, zero = disable, non-zero = enable.  Commands
 * 0 and 1 are supported with the VIRTIO_NET_F_CTRL_RX feature.
 * Commands 2-5 are added with VIRTIO_NET_F_CTRL_RX_EXTRA.
 */
#define VIRTIO_NET_CTRL_RX	0
#define VIRTIO_NET_CTRL_RX_PROMISC	0
#define VIRTIO_NET_CTRL_RX_ALLMULTI	1
#define VIRTIO_NET_CTRL_RX_ALLUNI	2
#define VIRTIO_NET_CTRL_RX_NOMULTI	3
#define VIRTIO_NET_CTRL_RX_NOUNI	4
#define VIRTIO_NET_CTRL_RX_NOBCAST	5

/*
 * Control the MAC filter table.
 *
 * The MAC filter table is managed by the hypervisor, the guest should
 * assume the size is infinite.  Filtering should be considered
 * non-perfect, ie. based on hypervisor resources, the guest may
 * received packets from sources not specified in the filter list.
 *
 * In addition to the class/cmd header, the TABLE_SET command requires
 * two out scatterlists.  Each contains a 4 byte count of entries followed
 * by a concatenated byte stream of the ETH_ALEN MAC addresses.  The
 * first sg list contains unicast addresses, the second is for multicast.
 * This functionality is present if the VIRTIO_NET_F_CTRL_RX feature
 * is available.
 *
 * The ADDR_SET command requests one out scatterlist, it contains a
 * 6 bytes MAC address. This functionality is present if the
 * VIRTIO_NET_F_CTRL_MAC_ADDR feature is available.
 */
struct virtio_net_ctrl_mac {
	uint32_t	entries;
	uint8_t		macs[][ETHER_ADDR_LEN];
} __packed;

#define VIRTIO_NET_CTRL_MAC	1
#define VIRTIO_NET_CTRL_MAC_TABLE_SET	0
#define VIRTIO_NET_CTRL_MAC_ADDR_SET	1

/*
 * Control VLAN filtering
 *
 * The VLAN filter table is controlled via a simple ADD/DEL interface.
 * VLAN IDs not added may be filtered by the hypervisor.  Del is the
 * opposite of add.  Both commands expect an out entry containing a 2
 * byte VLAN ID.  VLAN filtering is available with the
 * VIRTIO_NET_F_CTRL_VLAN feature bit.
 */
#define VIRTIO_NET_CTRL_VLAN	2
#define VIRTIO_NET_CTRL_VLAN_ADD	0
#define VIRTIO_NET_CTRL_VLAN_DEL	1

/*
 * Control link announce acknowledgement
 *
 * The command VIRTIO_NET_CTRL_ANNOUNCE_ACK is used to indicate that
 * driver has recevied the notification; device would clear the
 * VIRTIO_NET_S_ANNOUNCE bit in the status field after it receives
 * this command.
 */
#define VIRTIO_NET_CTRL_ANNOUNCE	3
#define VIRTIO_NET_CTRL_ANNOUNCE_ACK	0

/*
 * Control Receive Flow Steering
 *
 * The command VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET enables Receive Flow
 * Steering, specifying the number of the transmit and receive queues
 * that will be used. After the command is consumed and acked by the
 * device, the device will not steer new packets on receive virtqueues
 * other than specified nor read from transmit virtqueues other than
 * specified. Accordingly, driver should not transmit new packets on
 * virtqueues other than specified.
 */
struct virtio_net_ctrl_mq {
	uint16_t	virtqueue_pairs;
} __packed;

#define VIRTIO_NET_CTRL_MQ	4
#define VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET		0
#define VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MIN		1
#define VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MAX		0x8000

#endif /* _VIRTIO_NET_H */
