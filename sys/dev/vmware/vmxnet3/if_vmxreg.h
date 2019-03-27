/*-
 * Copyright (c) 2013 Tsubai Masanari
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $OpenBSD: src/sys/dev/pci/if_vmxreg.h,v 1.2 2013/06/12 01:07:33 uebayasi Exp $
 *
 * $FreeBSD$
 */

#ifndef _IF_VMXREG_H
#define _IF_VMXREG_H

struct UPT1_TxStats {
	uint64_t	TSO_packets;
	uint64_t	TSO_bytes;
	uint64_t	ucast_packets;
	uint64_t	ucast_bytes;
	uint64_t	mcast_packets;
	uint64_t	mcast_bytes;
	uint64_t	bcast_packets;
	uint64_t	bcast_bytes;
	uint64_t	error;
	uint64_t	discard;
} __packed;

struct UPT1_RxStats {
	uint64_t	LRO_packets;
	uint64_t	LRO_bytes;
	uint64_t	ucast_packets;
	uint64_t	ucast_bytes;
	uint64_t	mcast_packets;
	uint64_t	mcast_bytes;
	uint64_t	bcast_packets;
	uint64_t	bcast_bytes;
	uint64_t	nobuffer;
	uint64_t	error;
} __packed;

/* Interrupt moderation levels */
#define UPT1_IMOD_NONE		0	/* No moderation */
#define UPT1_IMOD_HIGHEST	7	/* Least interrupts */
#define UPT1_IMOD_ADAPTIVE	8	/* Adaptive interrupt moderation */

/* Hardware features */
#define UPT1_F_CSUM	0x0001		/* Rx checksum verification */
#define UPT1_F_RSS	0x0002		/* Receive side scaling */
#define UPT1_F_VLAN	0x0004		/* VLAN tag stripping */
#define UPT1_F_LRO	0x0008		/* Large receive offloading */

#define VMXNET3_BAR0_IMASK(irq)	(0x000 + (irq) * 8)	/* Interrupt mask */
#define VMXNET3_BAR0_TXH(q)	(0x600 + (q) * 8)	/* Tx head */
#define VMXNET3_BAR0_RXH1(q)	(0x800 + (q) * 8)	/* Ring1 Rx head */
#define VMXNET3_BAR0_RXH2(q)	(0xA00 + (q) * 8)	/* Ring2 Rx head */
#define VMXNET3_BAR1_VRRS	0x000	/* VMXNET3 revision report selection */
#define VMXNET3_BAR1_UVRS	0x008	/* UPT version report selection */
#define VMXNET3_BAR1_DSL	0x010	/* Driver shared address low */
#define VMXNET3_BAR1_DSH	0x018	/* Driver shared address high */
#define VMXNET3_BAR1_CMD	0x020	/* Command */
#define VMXNET3_BAR1_MACL	0x028	/* MAC address low */
#define VMXNET3_BAR1_MACH	0x030	/* MAC address high */
#define VMXNET3_BAR1_INTR	0x038	/* Interrupt status */
#define VMXNET3_BAR1_EVENT	0x040	/* Event status */

#define VMXNET3_CMD_ENABLE	0xCAFE0000	/* Enable VMXNET3 */
#define VMXNET3_CMD_DISABLE	0xCAFE0001	/* Disable VMXNET3 */
#define VMXNET3_CMD_RESET	0xCAFE0002	/* Reset device */
#define VMXNET3_CMD_SET_RXMODE	0xCAFE0003	/* Set interface flags */
#define VMXNET3_CMD_SET_FILTER	0xCAFE0004	/* Set address filter */
#define VMXNET3_CMD_VLAN_FILTER	0xCAFE0005	/* Set VLAN filter */
#define VMXNET3_CMD_GET_STATUS	0xF00D0000	/* Get queue errors */
#define VMXNET3_CMD_GET_STATS	0xF00D0001	/* Get queue statistics */
#define VMXNET3_CMD_GET_LINK	0xF00D0002	/* Get link status */
#define VMXNET3_CMD_GET_MACL	0xF00D0003	/* Get MAC address low */
#define VMXNET3_CMD_GET_MACH	0xF00D0004	/* Get MAC address high */
#define VMXNET3_CMD_GET_INTRCFG	0xF00D0008	/* Get interrupt config */

#define VMXNET3_DMADESC_ALIGN	128
#define VMXNET3_INIT_GEN	1

struct vmxnet3_txdesc {
	uint64_t	addr;

	uint32_t	len:14;
	uint32_t	gen:1;		/* Generation */
	uint32_t	pad1:1;
	uint32_t	dtype:1;	/* Descriptor type */
	uint32_t	pad2:1;
	uint32_t	offload_pos:14;	/* Offloading position */

	uint32_t	hlen:10;	/* Header len */
	uint32_t	offload_mode:2;	/* Offloading mode */
	uint32_t	eop:1;		/* End of packet */
	uint32_t	compreq:1;	/* Completion request */
	uint32_t	pad3:1;
	uint32_t	vtag_mode:1;	/* VLAN tag insertion mode */
	uint32_t	vtag:16;	/* VLAN tag */
} __packed;

/* Offloading modes */
#define VMXNET3_OM_NONE	0
#define VMXNET3_OM_CSUM 2
#define VMXNET3_OM_TSO  3

struct vmxnet3_txcompdesc {
	uint32_t	eop_idx:12;	/* EOP index in Tx ring */
	uint32_t	pad1:20;

	uint32_t	pad2:32;
	uint32_t	pad3:32;

	uint32_t	rsvd:24;
	uint32_t	type:7;
	uint32_t	gen:1;
} __packed;

struct vmxnet3_rxdesc {
	uint64_t	addr;

	uint32_t	len:14;
	uint32_t	btype:1;	/* Buffer type */
	uint32_t	dtype:1;	/* Descriptor type */
	uint32_t	rsvd:15;
	uint32_t	gen:1;

	uint32_t	pad1:32;
} __packed;

/* Buffer types */
#define VMXNET3_BTYPE_HEAD	0	/* Head only */
#define VMXNET3_BTYPE_BODY	1	/* Body only */

struct vmxnet3_rxcompdesc {
	uint32_t	rxd_idx:12;	/* Rx descriptor index */
	uint32_t	pad1:2;
	uint32_t	eop:1;		/* End of packet */
	uint32_t	sop:1;		/* Start of packet */
	uint32_t	qid:10;
	uint32_t	rss_type:4;
	uint32_t	no_csum:1;	/* No checksum calculated */
	uint32_t	pad2:1;

	uint32_t	rss_hash:32;	/* RSS hash value */

	uint32_t	len:14;
	uint32_t	error:1;
	uint32_t	vlan:1;		/* 802.1Q VLAN frame */
	uint32_t	vtag:16;	/* VLAN tag */

	uint32_t	csum:16;
	uint32_t	csum_ok:1;	/* TCP/UDP checksum ok */
	uint32_t	udp:1;
	uint32_t	tcp:1;
	uint32_t	ipcsum_ok:1;	/* IP checksum OK */
	uint32_t	ipv6:1;
	uint32_t	ipv4:1;
	uint32_t	fragment:1;	/* IP fragment */
	uint32_t	fcs:1;		/* Frame CRC correct */
	uint32_t	type:7;
	uint32_t	gen:1;
} __packed;

#define VMXNET3_RCD_RSS_TYPE_NONE	0
#define VMXNET3_RCD_RSS_TYPE_IPV4	1
#define VMXNET3_RCD_RSS_TYPE_TCPIPV4	2
#define VMXNET3_RCD_RSS_TYPE_IPV6	3
#define VMXNET3_RCD_RSS_TYPE_TCPIPV6	4

#define VMXNET3_REV1_MAGIC	0XBABEFEE1

#define VMXNET3_GOS_UNKNOWN	0x00
#define VMXNET3_GOS_LINUX	0x04
#define VMXNET3_GOS_WINDOWS	0x08
#define VMXNET3_GOS_SOLARIS	0x0C
#define VMXNET3_GOS_FREEBSD	0x10
#define VMXNET3_GOS_PXE		0x14

#define VMXNET3_GOS_32BIT	0x01
#define VMXNET3_GOS_64BIT	0x02

#define VMXNET3_MAX_TX_QUEUES	8
#define VMXNET3_MAX_RX_QUEUES	16
#define VMXNET3_MAX_INTRS \
    (VMXNET3_MAX_TX_QUEUES + VMXNET3_MAX_RX_QUEUES + 1)

#define VMXNET3_ICTRL_DISABLE_ALL	0x01

#define VMXNET3_RXMODE_UCAST	0x01
#define VMXNET3_RXMODE_MCAST	0x02
#define VMXNET3_RXMODE_BCAST	0x04
#define VMXNET3_RXMODE_ALLMULTI	0x08
#define VMXNET3_RXMODE_PROMISC	0x10

#define VMXNET3_EVENT_RQERROR	0x01
#define VMXNET3_EVENT_TQERROR	0x02
#define VMXNET3_EVENT_LINK	0x04
#define VMXNET3_EVENT_DIC	0x08
#define VMXNET3_EVENT_DEBUG	0x10

#define VMXNET3_MIN_MTU		60
#define VMXNET3_MAX_MTU		9000

/* Interrupt mask mode. */
#define VMXNET3_IMM_AUTO	0x00
#define VMXNET3_IMM_ACTIVE	0x01
#define VMXNET3_IMM_LAZY	0x02

/* Interrupt type. */
#define VMXNET3_IT_AUTO		0x00
#define VMXNET3_IT_LEGACY	0x01
#define VMXNET3_IT_MSI		0x02
#define VMXNET3_IT_MSIX		0x03

struct vmxnet3_driver_shared {
	uint32_t	magic;
	uint32_t	pad1;

	/* Misc. control */
	uint32_t	version;		/* Driver version */
	uint32_t	guest;			/* Guest OS */
	uint32_t	vmxnet3_revision;	/* Supported VMXNET3 revision */
	uint32_t	upt_version;		/* Supported UPT version */
	uint64_t	upt_features;
	uint64_t	driver_data;
	uint64_t	queue_shared;
	uint32_t	driver_data_len;
	uint32_t	queue_shared_len;
	uint32_t	mtu;
	uint16_t	nrxsg_max;
	uint8_t		ntxqueue;
	uint8_t		nrxqueue;
	uint32_t	reserved1[4];

	/* Interrupt control */
	uint8_t		automask;
	uint8_t		nintr;
	uint8_t		evintr;
	uint8_t		modlevel[VMXNET3_MAX_INTRS];
	uint32_t	ictrl;
	uint32_t	reserved2[2];

	/* Receive filter parameters */
	uint32_t	rxmode;
	uint16_t	mcast_tablelen;
	uint16_t	pad2;
	uint64_t	mcast_table;
	uint32_t	vlan_filter[4096 / 32];

	struct {
		uint32_t version;
		uint32_t len;
		uint64_t paddr;
	}		rss, pm, plugin;

	uint32_t	event;
	uint32_t	reserved3[5];
} __packed;

struct vmxnet3_txq_shared {
	/* Control */
	uint32_t	npending;
	uint32_t	intr_threshold;
	uint64_t	reserved1;

	/* Config */
	uint64_t	cmd_ring;
	uint64_t	data_ring;
	uint64_t	comp_ring;
	uint64_t	driver_data;
	uint64_t	reserved2;
	uint32_t	cmd_ring_len;
	uint32_t	data_ring_len;
	uint32_t	comp_ring_len;
	uint32_t	driver_data_len;
	uint8_t		intr_idx;
	uint8_t		pad1[7];

	/* Queue status */
	uint8_t		stopped;
	uint8_t		pad2[3];
	uint32_t	error;

	struct		UPT1_TxStats stats;

	uint8_t		pad3[88];
} __packed;

struct vmxnet3_rxq_shared {
	uint8_t		update_rxhead;
	uint8_t		pad1[7];
	uint64_t	reserved1;

	uint64_t	cmd_ring[2];
	uint64_t	comp_ring;
	uint64_t	driver_data;
	uint64_t	reserved2;
	uint32_t	cmd_ring_len[2];
	uint32_t	comp_ring_len;
	uint32_t	driver_data_len;
	uint8_t		intr_idx;
	uint8_t		pad2[7];

	uint8_t		stopped;
	uint8_t		pad3[3];
	uint32_t	error;

	struct		UPT1_RxStats stats;

	uint8_t		pad4[88];
} __packed;

#define UPT1_RSS_HASH_TYPE_NONE		0x00
#define UPT1_RSS_HASH_TYPE_IPV4		0x01
#define UPT1_RSS_HASH_TYPE_TCP_IPV4	0x02
#define UPT1_RSS_HASH_TYPE_IPV6		0x04
#define UPT1_RSS_HASH_TYPE_TCP_IPV6	0x08

#define UPT1_RSS_HASH_FUNC_NONE		0x00
#define UPT1_RSS_HASH_FUNC_TOEPLITZ	0x01

#define UPT1_RSS_MAX_KEY_SIZE		40
#define UPT1_RSS_MAX_IND_TABLE_SIZE	128

struct vmxnet3_rss_shared {
	uint16_t		hash_type;
	uint16_t		hash_func;
	uint16_t		hash_key_size;
	uint16_t		ind_table_size;
	uint8_t			hash_key[UPT1_RSS_MAX_KEY_SIZE];
	uint8_t			ind_table[UPT1_RSS_MAX_IND_TABLE_SIZE];
} __packed;

#endif /* _IF_VMXREG_H */
